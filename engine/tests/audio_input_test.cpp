// Receiver audio: the analytic step (audio_input.hpp), the audio reader (WavAudioReader, RecordingReader's audio input) and
// the engine on its output with the receiver's passband (EngineConfig::passband_lo_hz). docs/signal-processing.md, section 1.
#include "kz4ap/audio_input.hpp"
#include "kz4ap/engine.hpp"
#include "kz4ap/recording_reader.hpp"
#include "kz4ap/spectrum.hpp"
#include "kz4ap/viterbi/hiss.hpp"
#include "kz4ap/wav_reader.hpp"

#include "temp_dir.hpp"
#include "test_signals.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <map>
#include <numbers>
#include <random>
#include <set>
#include <span>
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

// A WAV of `channels` interleaved channels (frames x channels values, FS): 16-bit PCM, or 32-bit float with as_float.
void write_wav(const std::filesystem::path& path, int rate, int channels, const std::vector<float>& interleaved, bool as_float = false) {
    const int bytes = as_float ? 4 : 2;
    std::string data;
    for (const float v : interleaved) {
        if (as_float) {
            std::uint32_t u;
            std::memcpy(&u, &v, 4);
            put_u32(data, u);
        } else {
            const long q = std::lround(std::clamp(static_cast<double>(v), -1.0, 32767.0 / 32768.0) * 32768.0);
            put_u16(data, static_cast<std::uint32_t>(static_cast<std::uint16_t>(static_cast<std::int16_t>(q))));
        }
    }
    std::string b = "RIFF";
    put_u32(b, static_cast<std::uint32_t>(36 + data.size()));
    b += "WAVEfmt ";
    put_u32(b, 16);
    put_u16(b, as_float ? 3 : 1);
    put_u16(b, static_cast<std::uint32_t>(channels));
    put_u32(b, static_cast<std::uint32_t>(rate));
    put_u32(b, static_cast<std::uint32_t>(rate * channels * bytes));
    put_u16(b, static_cast<std::uint32_t>(channels * bytes));
    put_u16(b, static_cast<std::uint32_t>(8 * bytes));
    b += "data";
    put_u32(b, static_cast<std::uint32_t>(data.size()));
    b += data;
    std::ofstream(path, std::ios::binary) << b;
}

// Real white noise of standard deviation sigma FS, band-limited to [lo_hz, hi_hz] by a windowed-sinc band-pass (Hann, 511 taps):
// a receiver's audio, its in-band one-sided density 2 sigma^2 / rate FS^2/Hz (the analytic signal's N0 = 4 sigma^2 / rate).
std::vector<double> receiver_noise(std::size_t n, int rate, double sigma, double lo_hz, double hi_hz, unsigned seed) {
    std::mt19937 rng(seed);
    std::normal_distribution<double> gauss(0.0, sigma);
    constexpr int kTaps = 511;
    std::vector<double> h(kTaps);
    for (int m = 0; m < kTaps; ++m) {
        const double t = m - (kTaps - 1) / 2.0;
        const auto lowpass = [&](double fc) {
            const double x = 2.0 * fc / rate;
            return t == 0.0 ? x : std::sin(std::numbers::pi * x * t) / (std::numbers::pi * t);
        };
        const double w = 0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * m / (kTaps - 1));
        h[static_cast<std::size_t>(m)] = w * (lowpass(hi_hz) - lowpass(lo_hz));
    }
    std::vector<double> white(n + kTaps);
    for (double& v : white) v = gauss(rng);
    std::vector<double> out(n);
    for (std::size_t i = 0; i < n; ++i) {
        double s = 0.0;
        for (int m = 0; m < kTaps; ++m) s += h[static_cast<std::size_t>(m)] * white[i + static_cast<std::size_t>(kTaps - 1 - m)];
        out[i] = s;
    }
    return out;
}

// The amplitude of a real tone that stands s500_db dB SNR in 500 Hz over receiver_noise(sigma) at rate: a^2 / (N0 x 500 Hz),
// N0 = 4 sigma^2 / rate, the analytic signal's density (a tone of amplitude a FS reads a^2 there).
double tone_amplitude(double s500_db, double sigma, int rate) {
    return std::sqrt(std::pow(10.0, s500_db / 10.0) * 4.0 * sigma * sigma / rate * 500.0);
}

// The analytic output of audio pushed in chunks of `chunk`, with enough zeros after it that every output it stands for is out.
std::vector<Sample> analytic_of(const std::vector<float>& audio, int rate, std::size_t chunk = 1000) {
    AudioAnalytic a(rate);
    std::vector<Sample> out;
    for (std::size_t i = 0; i < audio.size(); i += chunk)
        a.push(std::span<const float>(audio).subspan(i, std::min(chunk, audio.size() - i)), out);
    const std::uint64_t want = AudioAnalytic::outputs_for(audio.size(), rate);
    const std::vector<float> zeros(4096, 0.0f);
    while (out.size() < want) a.push(zeros, out);
    out.resize(static_cast<std::size_t>(want));
    return out;
}

}  // namespace

// A real tone a cos(2 pi f t + phi) comes out as the complex tone a exp(j (2 pi f t + phi)) at 48 kHz, the delays taken out
// exactly, and its image at -f is more than 80 dB below it: every input rate, tones across the receiver's band.
TEST(AudioAnalytic, ARealToneBecomesItsAnalyticSignal) {
    for (const int rate : {8000, 12000, 44100, 48000}) {
        for (const int f : {100, 300, 600, 1500, 2900}) {
            SCOPED_TRACE("rate " + std::to_string(rate) + " samples/s, tone " + std::to_string(f) + " Hz");
            const double a = 0.5, phi = 0.7;
            std::vector<float> x(static_cast<std::size_t>(3 * rate));
            for (std::size_t i = 0; i < x.size(); ++i)
                x[i] = static_cast<float>(a * std::cos(2.0 * std::numbers::pi * f * static_cast<double>(i) / rate + phi));
            const std::vector<Sample> z = analytic_of(x, rate);
            ASSERT_EQ(z.size(), static_cast<std::size_t>(3 * kAudioEngineRate));
            // One second in the middle, a whole number of cycles of f and 2f: the sums at +f and -f are exact.
            std::complex<double> plus = 0.0, minus = 0.0;
            for (int k = kAudioEngineRate; k < 2 * kAudioEngineRate; ++k) {
                const double ph = 2.0 * std::numbers::pi * f * k / kAudioEngineRate;
                plus += std::complex<double>(z[static_cast<std::size_t>(k)]) * std::polar(1.0, -ph);
                minus += std::complex<double>(z[static_cast<std::size_t>(k)]) * std::polar(1.0, ph);
            }
            plus /= kAudioEngineRate;
            minus /= kAudioEngineRate;
            // Measured (2026-11-19): within 0.0007 dB re the input, the image at most -84.1 dB re it (at 100 Hz; -95 dB from 300 Hz),
            // the phase within 4e-7 rad.
            EXPECT_NEAR(20.0 * std::log10(std::abs(plus) / a), 0.0, 0.002);  // dB re the input's amplitude
            EXPECT_LT(20.0 * std::log10(std::abs(minus) / a), -80.0);
            EXPECT_NEAR(std::arg(plus), phi, 1e-5);  // rad: the delays taken out exactly (1e-5 rad is 0.6 ns at 2.9 kHz)
        }
    }
}

TEST(AudioAnalytic, TheFilterAndItsDelays) {
    const std::vector<Sample> taps = analytic_filter_taps(48000);
    EXPECT_EQ(taps.size(), 1240u);  // 2 (D + 1), D = ceil(1237.8 / 2) = 619
    AudioAnalytic at48(48000);
    EXPECT_EQ(at48.resampler(), nullptr);
    EXPECT_EQ(at48.removed_samples(), 619u);
    EXPECT_DOUBLE_EQ(at48.analytic_center(), 619.0);
    EXPECT_EQ(at48.fft_size(), 4096);
    AudioAnalytic at8(8000);
    ASSERT_NE(at8.resampler(), nullptr);
    // The resampler's delay (600 - 1) / 2 = 299.5 output samples: 300 dropped, 0.5 added to the analytic filter's center.
    EXPECT_DOUBLE_EQ(at8.analytic_center(), 619.5);
    EXPECT_EQ(at8.removed_samples(), 919u);
    EXPECT_EQ(AudioAnalytic::outputs_for(44100, 44100), 48000u);
    EXPECT_EQ(AudioAnalytic::outputs_for(1, 44100), 2u);
    EXPECT_EQ(AudioAnalytic::outputs_for(147, 44100), 160u);
    EXPECT_THROW(analytic_filter_taps(48000, 1.0), std::invalid_argument);
}

TEST(AudioAnalytic, TheOutputDoesNotDependOnHowTheInputIsSplit) {
    std::mt19937 rng(3);
    std::normal_distribution<float> gauss(0.0f, 0.1f);
    std::vector<float> x(30000);
    for (float& v : x) v = gauss(rng);
    const std::vector<Sample> one = analytic_of(x, 12000, 30000), many = analytic_of(x, 12000, 777);
    ASSERT_EQ(one.size(), many.size());
    for (std::size_t i = 0; i < one.size(); ++i) ASSERT_EQ(one[i], many[i]) << i;
}

TEST(RecordingReaderAudio, AMonoFileIsReceiverAudioAndAStereoOneNeedsToBeSaid) {
    const auto mono = test::temp_path("audio_mono.wav"), stereo = test::temp_path("audio_stereo.wav");
    const int rate = 12000;
    std::vector<float> m(static_cast<std::size_t>(2 * rate)), s(2 * m.size());
    for (std::size_t i = 0; i < m.size(); ++i) {
        const double t = static_cast<double>(i) / rate;
        m[i] = static_cast<float>(0.25 * std::cos(2.0 * std::numbers::pi * 700.0 * t));
        s[2 * i] = static_cast<float>(0.25 * std::cos(2.0 * std::numbers::pi * 1100.0 * t));      // left: 1100 Hz
        s[2 * i + 1] = static_cast<float>(0.125 * std::cos(2.0 * std::numbers::pi * 1900.0 * t)); // right: 1900 Hz
    }
    write_wav(mono, rate, 1, m);
    write_wav(stereo, rate, 2, s, true);
    EXPECT_EQ(wav_channels(mono), 1);
    EXPECT_EQ(wav_channels(stereo), 2);

    RecordingReader auto_mono(mono);
    EXPECT_TRUE(auto_mono.is_audio());
    EXPECT_EQ(auto_mono.sample_rate(), 48000);
    EXPECT_EQ(auto_mono.file_rate(), rate);
    EXPECT_EQ(auto_mono.total_samples(), 96000u);
    EXPECT_FALSE(RecordingReader(stereo).is_audio());  // a stereo file is I/Q unless said
    RecordingReader lower(mono, RecordingOptions{.mirror = true});  // a lower sideband: the 700 Hz tone at -700 Hz
    EXPECT_THROW(RecordingReader(mono, RecordingOptions{.input = RecordingInput::Iq}), std::runtime_error);
    EXPECT_THROW(RecordingReader(stereo, RecordingOptions{.input = RecordingInput::Audio, .audio_channel = 2}), std::runtime_error);

    // The tone's level at f in a read of the whole file, dB re 1 FS of amplitude.
    const auto level_db = [](RecordingReader& r, double f) {
        std::vector<Sample> z(static_cast<std::size_t>(r.total_samples()));
        EXPECT_EQ(r.read(z), z.size());
        std::complex<double> sum = 0.0;
        for (int k = 24000; k < 72000; ++k) sum += std::complex<double>(z[static_cast<std::size_t>(k)]) * std::polar(1.0, -2.0 * std::numbers::pi * f * k / 48000.0);
        return 20.0 * std::log10(std::abs(sum) / 48000.0 + 1e-30);
    };
    EXPECT_NEAR(level_db(auto_mono, 700.0), 20.0 * std::log10(0.25), 0.01);  // 16-bit rounding only
    EXPECT_NEAR(level_db(lower, -700.0), 20.0 * std::log10(0.25), 0.01);
    RecordingReader right(stereo, RecordingOptions{.input = RecordingInput::Audio, .audio_channel = 1});
    EXPECT_EQ(right.file_channels(), 2);
    EXPECT_NEAR(level_db(right, 1900.0), 20.0 * std::log10(0.125), 0.01);
    RecordingReader right_again(stereo, RecordingOptions{.input = RecordingInput::Audio, .audio_channel = 1});
    EXPECT_LT(level_db(right_again, 1100.0), -100.0);  // the left channel's tone is not in it
    RecordingReader left(stereo, RecordingOptions{.input = RecordingInput::Audio});
    EXPECT_NEAR(level_db(left, 1100.0), 20.0 * std::log10(0.25), 0.001);
}

TEST(RecordingReaderAudio, ASeekGivesTheSamplesOfAReadThrough) {
    const auto path = test::temp_path("audio_seek.wav");
    const int rate = 44100;
    std::mt19937 rng(5);
    std::normal_distribution<float> gauss(0.0f, 0.1f);
    std::vector<float> x(static_cast<std::size_t>(3 * rate));
    for (float& v : x) v = gauss(rng);
    write_wav(path, rate, 1, x, true);
    RecordingReader through(path);
    std::vector<Sample> all(static_cast<std::size_t>(through.total_samples()));
    ASSERT_EQ(through.read(all), all.size());
    for (const std::uint64_t at : {std::uint64_t{0}, std::uint64_t{17}, std::uint64_t{50000}, std::uint64_t{100003}}) {
        RecordingReader seeking(path);
        seeking.seek(at);
        std::vector<Sample> part(20000);
        const std::size_t n = seeking.read(part);
        ASSERT_EQ(n, std::min<std::size_t>(part.size(), all.size() - static_cast<std::size_t>(at)));
        double worst = 0.0;
        for (std::size_t i = 0; i < n; ++i) worst = std::max(worst, static_cast<double>(std::abs(part[i] - all[static_cast<std::size_t>(at) + i])));
        EXPECT_LT(worst, 1e-5) << "seek to " << at;  // FS: float rounding of the fast convolution's other blocks
    }
}

// The hiss estimator on a receiver's audio: noise only within 300-3000 Hz. Restricted to the passband it reads the in-band
// density; over the whole neighborhood (+/-2 kHz, half of it near-silence) it reads far below it.
TEST(RecordingReaderAudio, TheHissInThePassbandIsTheReceiversNoise) {
    const int rate = 12000;
    const double sigma = 0.05;
    const std::vector<double> noise = receiver_noise(static_cast<std::size_t>(6 * rate), rate, sigma, 300.0, 3000.0, 11);
    std::vector<float> audio(noise.size());
    std::transform(noise.begin(), noise.end(), audio.begin(), [](double v) { return static_cast<float>(v); });
    const std::vector<Sample> z = analytic_of(audio, rate);
    SpectrumAnalyzer spectrum(48000, 2048, 1024);
    viterbi::ViterbiConfig config;
    viterbi::HissEstimator band(48000, 2048, config), whole(48000, 2048, config);
    band.set_band(300.0, 3000.0);
    for (const SpectrumFrame& frame : spectrum.push(z)) {
        band.push(frame);
        whole.push(frame);
    }
    const double n0 = 4.0 * sigma * sigma / rate;  // FS^2/Hz
    const int bin_700 = static_cast<int>(std::lround(700.0 / (48000.0 / 2048)));
    EXPECT_NEAR(band.noise_density_fs2_per_hz(bin_700) / n0, 1.0, 0.08);
    EXPECT_LT(whole.noise_density_fs2_per_hz(bin_700) / n0, 0.1);
}

namespace {

struct AudioRun {
    std::map<std::uint32_t, std::string> text;
    std::map<std::uint32_t, double> freq_hz;
};

// The segmental decoder on the detector's path over receiver audio read from a WAV file, with the receiver's passband.
AudioRun decode_audio(const std::filesystem::path& path, double lo_hz, double hi_hz) {
    AudioRun r;
    EventBus bus;
    std::map<std::uint32_t, std::vector<std::string>> symbols;
    bus.subscribe([&](const Event& e) {
        if (const auto* t = std::get_if<TrackEvent>(&e); t && t->kind == TrackEvent::Kind::Born) r.freq_hz[t->track.id] = t->track.freq_hz;
        if (const auto* d = std::get_if<DecodedTextEvent>(&e)) {
            std::vector<std::string>& list = symbols[d->track_id];
            for (const auto& c : d->chars) list.push_back(c.text);
            for (const auto& k : d->corrections) {
                list.resize(std::min(k.from_index, list.size()));
                for (const auto& c : k.chars) list.push_back(c.text);
            }
        }
    });
    RecordingReader reader(path);
    EngineConfig config;
    config.sample_rate = reader.sample_rate();
    config.decoder.front_end = FrontEnd::Segmental;
    config.passband_lo_hz = lo_hz;
    config.passband_hi_hz = hi_hz;
    Engine engine(config, bus);
    std::vector<Sample> block(16384);
    for (std::size_t n; (n = reader.read(block)) > 0;) engine.process(std::span<const Sample>(block).first(n));
    engine.finish();
    for (const auto& [id, list] : symbols)
        for (const std::string& s : list) r.text[id] += s;
    return r;
}

// Receiver audio at rate: noise within 300-3000 Hz (sigma 0.05 FS before the filter) and a station keyed with text at
// 25 WPM on a tone at tone_hz, s500_db dB SNR in 500 Hz, written as a mono 16-bit WAV.
std::filesystem::path station_audio(const std::string& name, int rate, double tone_hz, double s500_db, const std::string& text,
                                    unsigned seed) {
    const double sigma = 0.05;
    const double duration = test::duration_for(text, 25.0, 2.0);
    const auto n = static_cast<std::size_t>(duration * rate);
    const std::vector<double> noise = receiver_noise(n, rate, sigma, 300.0, 3000.0, seed);
    const std::vector<Sample> carrier = test::keyed_signal(text, 25.0, rate, duration, tone_hz, tone_amplitude(s500_db, sigma, rate));
    std::vector<float> audio(n);
    for (std::size_t i = 0; i < n; ++i) audio[i] = static_cast<float>(noise[i] + carrier[i].real());
    const auto path = test::temp_path(name);
    write_wav(path, rate, 1, audio);
    return path;
}

}  // namespace

// A station in receiver audio decodes to its text at every input rate, its frequency the audio tone's, Hz.
TEST(RecordingReaderAudio, AStationInReceiverAudioDecodes) {
    const std::string text = "CQ TEST DE K1ABC K1ABC K";
    struct Case {
        int rate;
        double tone_hz;
        double s500_db;
    };
    for (const Case c : {Case{8000, 600.0, 20.0}, Case{12000, 1200.0, 10.0}, Case{44100, 2500.0, 6.0}, Case{48000, 800.0, 8.0}}) {
        SCOPED_TRACE(std::to_string(c.rate) + " samples/s, " + std::to_string(c.tone_hz) + " Hz, " + std::to_string(c.s500_db) +
                     " dB S500");
        const AudioRun r = decode_audio(station_audio("station_" + std::to_string(c.rate) + ".wav", c.rate, c.tone_hz, c.s500_db, text, 7), 300.0, 3000.0);
        ASSERT_EQ(r.text.size(), 1u);
        EXPECT_NEAR(r.freq_hz.begin()->second, c.tone_hz, 6.0);  // Hz: within a detector bin (5.86 Hz)
        EXPECT_NE(r.text.begin()->second.find("TEST DE K1ABC K1ABC K"), std::string::npos) << r.text.begin()->second;
    }
}

// A strong tone outside the passband (3.5 kHz, 30 dB S500 against the in-band noise) opens no track; with the passband
// widened over it, it does.
TEST(RecordingReaderAudio, NoStationOutsideThePassband) {
    const auto path = station_audio("outside.wav", 12000, 3500.0, 30.0, "CQ TEST DE K1ABC K", 9);
    EXPECT_TRUE(decode_audio(path, 300.0, 3000.0).freq_hz.empty());
    const AudioRun wide = decode_audio(path, 300.0, 4000.0);
    ASSERT_FALSE(wide.freq_hz.empty());
    EXPECT_NEAR(wide.freq_hz.begin()->second, 3500.0, 6.0);
}
