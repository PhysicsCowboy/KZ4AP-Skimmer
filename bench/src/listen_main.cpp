// kz4ap-listen: one station of an I/Q recording as audio, to check a decoding by ear: the signal at one frequency, shifted to
// an audio tone and filtered narrow, written as a mono 16-bit PCM WAV (docs/signal-processing.md, A.13).
//   kz4ap-listen IN.wav OUT.wav (--khz KHZ | --offset-hz HZ) --start S --seconds S [--mirror] [--center-khz KHZ]
//                [--tone-hz HZ] [--bandwidth-hz HZ] [--rate 8000|12000] [--track-filter] [--gain-db DB]
// It writes a file only; it plays nothing.
#include "kz4ap/listen.hpp"
#include "kz4ap/recording_reader.hpp"
#include "kz4ap/utc_time.hpp"
#include "kz4ap/wav_reader.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>

using namespace kz4ap;

namespace {

// The peak the gain sets when --gain-db is not given, dBFS (20 log10 of the peak over 1 FS): 1 dB below full scale, for a
// player's own resampling (heuristic).
constexpr double kPeakDbfs = -1.0;

constexpr const char* kUsage =
    "usage: kz4ap-listen IN.wav OUT.wav (--khz KHZ | --offset-hz HZ) --start S --seconds S [--mirror] [--center-khz KHZ]\n"
    "                    [--tone-hz HZ] [--bandwidth-hz HZ] [--rate 8000|12000] [--track-filter] [--gain-db DB]\n"
    "  --khz          the station's frequency, kHz (needs the recording's center: header, file name or --center-khz)\n"
    "  --offset-hz    the station's frequency as an offset from the recording's center, Hz\n"
    "  --start        the clip's start, s from the recording's first sample\n"
    "  --seconds      the clip's length, s (shortened to the recording's end)\n"
    "  --mirror       the receiver's spectrum is inverted (an SDR on a transceiver's IF output)\n"
    "  --tone-hz      the audio tone the station is heard at, Hz (default 600)\n"
    "  --bandwidth-hz the listening filter's width at -6 dB, Hz (default 500)\n"
    "  --rate         the audio's rate, samples/s (default 12000)\n"
    "  --track-filter the engine's own channel filter instead (+/-150 Hz at -6 dB): what a track's decoder reads\n"
    "  --gain-db      a fixed gain, dB re unity (1 FS in, 1 FS out), instead of setting the clip's peak to -1 dBFS\n";

double number(const std::string& name, const char* text) {
    std::size_t used = 0;
    const double v = std::stod(text, &used);
    if (used != std::string(text).size() || !std::isfinite(v)) throw std::runtime_error(name + " wants a number: " + text);
    return v;
}

double db(double ratio) { return ratio > 0.0 ? 10.0 * std::log10(ratio) : -INFINITY; }

}  // namespace

int main(int argc, char** argv) {
    try {
        std::string in, out;
        bool mirror = false;
        std::optional<double> khz, offset_hz, start_s, seconds, center_hz, gain_db;
        bool bandwidth_given = false;
        ListenConfig config;
        for (int i = 1; i < argc; ++i) {
            const std::string a = argv[i];
            auto value = [&]() -> double {
                if (i + 1 >= argc) throw std::runtime_error(a + " wants a value");
                return number(a, argv[++i]);
            };
            if (a == "--help" || a == "-h") {
                std::fputs(kUsage, stdout);
                return 0;
            } else if (a == "--mirror") mirror = true;
            else if (a == "--track-filter") config.track_filter = true;
            else if (a == "--khz") khz = value();
            else if (a == "--offset-hz") offset_hz = value();
            else if (a == "--start") start_s = value();
            else if (a == "--seconds") seconds = value();
            else if (a == "--center-khz") center_hz = 1000.0 * value();
            else if (a == "--tone-hz") config.tone_hz = value();
            else if (a == "--bandwidth-hz") {
                config.bandwidth_hz = value();
                bandwidth_given = true;
            } else if (a == "--rate") config.audio_rate = static_cast<int>(value());
            else if (a == "--gain-db") gain_db = value();
            else if (!a.empty() && a[0] != '-' && in.empty()) in = a;
            else if (!a.empty() && a[0] != '-' && out.empty()) out = a;
            else throw std::runtime_error("unknown argument: " + a + "\n" + kUsage);
        }
        if (in.empty() || out.empty() || !start_s || !seconds || khz.has_value() == offset_hz.has_value())
            throw std::runtime_error(std::string("give IN.wav, OUT.wav, one of --khz and --offset-hz, --start and --seconds\n") + kUsage);
        if (bandwidth_given && config.track_filter)
            throw std::runtime_error("--bandwidth-hz and --track-filter exclude each other: the track's filter has its own width");
        if (std::filesystem::exists(out) && std::filesystem::equivalent(in, out)) throw std::runtime_error("the clip would overwrite the recording");

        RecordingReader reader(in, RecordingOptions{.mirror = mirror});
        if (!center_hz) center_hz = reader.info().center_hz;
        if (!center_hz) center_hz = center_hz_from_file_name(path_text(std::filesystem::path(in).filename()), reader.file_rate());
        if (khz) {
            if (!center_hz) throw std::runtime_error("the recording's center is unknown (no header, no frequency in its name): give --center-khz, or --offset-hz");
            config.offset_hz = 1000.0 * *khz - *center_hz;
        } else {
            config.offset_hz = *offset_hz;
        }
        const double length_s = static_cast<double>(reader.total_samples()) / reader.sample_rate();
        if (*start_s >= length_s)
            throw std::runtime_error("the recording lasts " + std::to_string(length_s) + " s; the start is past its end");
        double duration = *seconds;
        if (*start_s + duration > length_s) {
            duration = length_s - *start_s;
            std::fprintf(stderr, "kz4ap-listen: the recording ends at %.3f s; the clip is shortened to %.3f s\n", length_s, duration);
        }

        const ListenClip clip = listen_clip(reader, config, *start_s, duration);
        float peak = 0.0f;
        double power = 0.0;
        for (const float v : clip.audio) {
            peak = std::max(peak, std::abs(v));
            power += static_cast<double>(v) * v;
        }
        power /= static_cast<double>(std::max<std::size_t>(1, clip.audio.size()));
        const double gain = gain_db ? std::pow(10.0, *gain_db / 20.0)
                                    : (peak > 0.0f ? std::pow(10.0, kPeakDbfs / 20.0) / peak : 1.0);
        const std::uint64_t clipped = write_mono_wav16(out, config.audio_rate, clip.audio, gain);

        std::fprintf(stderr, "%s: %.1f Hz from the center", in.c_str(), config.offset_hz);
        if (center_hz) std::fprintf(stderr, " (%.3f kHz, center %.3f kHz)", (*center_hz + config.offset_hz) / 1000.0, *center_hz / 1000.0);
        std::fprintf(stderr, ", %s; %.3f s from %.3f s", mirror ? "mirrored" : "not mirrored", duration, *start_s);
        if (reader.info().start_unix_ms) {
            const auto ms = *reader.info().start_unix_ms + static_cast<std::int64_t>(std::llround(1000.0 * *start_s));
            const CivilTime c = civil_from_unix_ms(ms);
            std::fprintf(stderr, " (%04d-%02d-%02d %02d:%02d:%02d.%03d UTC)", c.year, c.month, c.day, c.hour, c.minute, c.second, c.millisecond);
        }
        if (config.track_filter) std::fprintf(stderr, "\n  the track's channel filter (+/-150 Hz at -6 dB re the passband)");
        else std::fprintf(stderr, "\n  listening filter %.0f Hz wide at -6 dB re the passband", config.bandwidth_hz);
        std::fprintf(stderr, ", tone %.0f Hz, %d samples/s\n", config.tone_hz, config.audio_rate);
        std::fprintf(stderr, "  power in the band %.2f dBFS (re 1 FS^2, the recording's scale); gain %+.2f dB re unity (%s)\n",
                     db(clip.band_power_fs2), 20.0 * std::log10(gain), gain_db ? "fixed" : "the clip's peak to -1 dBFS");
        std::fprintf(stderr, "  %s: peak %.2f dBFS, rms %.2f dBFS (re 1 FS^2; a full-scale sine reads -3.01), %llu samples clipped\n",
                     out.c_str(), db(static_cast<double>(peak) * peak * gain * gain), db(power * gain * gain),
                     static_cast<unsigned long long>(clipped));
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "kz4ap-listen: %s\n", e.what());
        return 1;
    }
}
