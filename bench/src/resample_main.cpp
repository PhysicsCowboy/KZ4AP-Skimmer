// kz4ap-resample: a copy of an I/Q recording at a rate the decoders and CW Skimmer take (48, 96 or 192 kHz and up), as the engine
// itself reads it (RecordingReader: resampled exactly by a rational ratio, mirrored if asked), written as 16-bit stereo PCM with an
// auxi chunk that keeps the center frequency and the times.
//   kz4ap-resample IN.wav OUT.wav [--mirror] [--center-khz KHZ] [--gain-db DB]
// The copy's spectrum is the true one when --mirror is given for a mirrored recording. --gain-db scales the amplitude (dB re
// unity) before the 16-bit rounding, for a recording too quiet for 16 bits (a 24-bit file's noise a few counts of 32767).
#include "kz4ap/recording_reader.hpp"
#include "kz4ap/utc_time.hpp"
#include "kz4ap/wav_reader.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

using namespace kz4ap;

namespace {

void put_u16(std::string& b, std::uint32_t v) {
    b.push_back(static_cast<char>(v & 0xFF));
    b.push_back(static_cast<char>((v >> 8) & 0xFF));
}

void put_u32(std::string& b, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) b.push_back(static_cast<char>((v >> (8 * i)) & 0xFF));
}

void put_system_time(std::string& b, std::optional<std::int64_t> unix_ms) {
    if (!unix_ms) {
        b += std::string(16, '\0');
        return;
    }
    const CivilTime c = civil_from_unix_ms(*unix_ms);
    for (const int v : {c.year, c.month, c.day_of_week, c.day, c.hour, c.minute, c.second, c.millisecond})
        put_u16(b, static_cast<std::uint32_t>(v));
}

// RIFF header, fmt (PCM, 2 channels, 16 bits), auxi (164 bytes, SpectraVue's layout), and the data chunk's header.
std::string header(int rate, std::uint64_t samples, std::optional<double> center_hz, std::optional<std::int64_t> start_ms,
                   std::optional<std::int64_t> stop_ms) {
    std::string auxi;
    put_system_time(auxi, start_ms);
    put_system_time(auxi, stop_ms);
    put_u32(auxi, center_hz ? static_cast<std::uint32_t>(std::llround(*center_hz)) : 0u);
    auxi += std::string(164 - auxi.size(), '\0');
    const auto data_bytes = static_cast<std::uint32_t>(samples * 4);
    std::string h = "RIFF";
    put_u32(h, static_cast<std::uint32_t>(4 + (8 + 16) + (8 + auxi.size()) + 8 + data_bytes));
    h += "WAVEfmt ";
    put_u32(h, 16);
    put_u16(h, 1);
    put_u16(h, 2);
    put_u32(h, static_cast<std::uint32_t>(rate));
    put_u32(h, static_cast<std::uint32_t>(rate) * 4);
    put_u16(h, 4);
    put_u16(h, 16);
    h += "auxi";
    put_u32(h, static_cast<std::uint32_t>(auxi.size()));
    h += auxi;
    h += "data";
    put_u32(h, data_bytes);
    return h;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        std::string in, out;
        bool mirror = false;
        std::optional<double> center_hz;
        double gain = 1.0;
        for (int i = 1; i < argc; ++i) {
            const std::string a = argv[i];
            if (a == "--mirror") mirror = true;
            else if (a == "--center-khz" && i + 1 < argc) center_hz = 1000.0 * std::stod(argv[++i]);
            else if (a == "--gain-db" && i + 1 < argc) gain = std::pow(10.0, std::stod(argv[++i]) / 20.0);
            else if (!a.empty() && a[0] != '-' && in.empty()) in = a;
            else if (!a.empty() && a[0] != '-' && out.empty()) out = a;
            else throw std::runtime_error("unknown argument: " + a);
        }
        if (in.empty() || out.empty()) throw std::runtime_error("usage: kz4ap-resample IN.wav OUT.wav [--mirror] [--center-khz KHZ] [--gain-db DB]");
        if (std::filesystem::exists(out) && std::filesystem::equivalent(in, out)) throw std::runtime_error("the copy would overwrite the recording");
        RecordingReader reader(in, RecordingOptions{.mirror = mirror, .input = RecordingInput::Iq});
        if (!center_hz) center_hz = reader.info().center_hz;
        if (!center_hz) center_hz = center_hz_from_file_name(path_text(std::filesystem::path(in).filename()), reader.file_rate());
        const std::uint64_t total = reader.total_samples();
        if (total * 4 > 0xFFFFFFFFull - 512) throw std::runtime_error("the copy would pass 4 GiB, a WAV's limit");
        std::ofstream file(out, std::ios::binary);
        if (!file) throw std::runtime_error("cannot create " + out);
        const std::string h = header(reader.sample_rate(), total, center_hz, reader.info().start_unix_ms, reader.info().stop_unix_ms);
        file.write(h.data(), static_cast<std::streamsize>(h.size()));
        std::vector<Sample> block(65536);
        std::vector<std::int16_t> pcm;
        std::uint64_t written = 0, clipped = 0;
        double peak = 0.0, power = 0.0;
        for (;;) {
            const std::size_t n = reader.read(block);
            if (n == 0) break;
            pcm.resize(2 * n);
            for (std::size_t i = 0; i < n; ++i) {
                const float parts[2] = {block[i].real(), block[i].imag()};
                for (int k = 0; k < 2; ++k) {
                    const double v = std::round(static_cast<double>(parts[k]) * gain * 32768.0);
                    peak = std::max(peak, std::abs(v));
                    power += v * v;
                    if (v > 32767.0 || v < -32768.0) ++clipped;
                    pcm[2 * i + k] = static_cast<std::int16_t>(std::clamp(v, -32768.0, 32767.0));
                }
            }
            file.write(reinterpret_cast<const char*>(pcm.data()), static_cast<std::streamsize>(pcm.size() * sizeof(std::int16_t)));
            written += n;
        }
        if (!file) throw std::runtime_error("cannot write " + out);
        if (written != total) throw std::runtime_error("wrote " + std::to_string(written) + " samples, expected " + std::to_string(total));
        std::fprintf(stderr, "%s: %d samples/s to %d", in.c_str(), reader.file_rate(), reader.sample_rate());
        if (const RationalResampler* r = reader.resampler())
            std::fprintf(stderr, " (x %d/%d, %d taps per phase, delay %.1f us)", r->up(), r->down(), r->taps_per_phase(), 1e6 * r->delay_s());
        std::fprintf(stderr, "; %llu samples, %s; gain %+.1f dB re unity; center %s Hz; peak %.0f counts of 32767, rms %.1f counts, %llu clipped\n",
                     static_cast<unsigned long long>(written), mirror ? "mirrored back" : "not mirrored", 20.0 * std::log10(gain),
                     center_hz ? std::to_string(*center_hz).c_str() : "unknown", peak,
                     written ? std::sqrt(power / (2.0 * static_cast<double>(written))) : 0.0, static_cast<unsigned long long>(clipped));
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "kz4ap-resample: %s\n", e.what());
        return 1;
    }
}
