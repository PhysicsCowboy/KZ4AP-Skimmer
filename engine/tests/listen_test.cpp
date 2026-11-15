// kz4ap-listen's signal processing (listen.hpp): the station at the tone and its level, a neighbor 1 kHz away, the mirror, the
// time window, the listening filter's stated passband and stopband, and the WAV it writes.
#include "kz4ap/listen.hpp"

#include "kz4ap/recording_reader.hpp"

#include "temp_dir.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <numbers>
#include <string>
#include <tuple>
#include <utility>
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

// A stereo 32-bit float I/Q WAV (left I, right Q), so that the test signals are not rounded to 16 bits.
void write_iq_wav(const std::filesystem::path& path, int rate, const std::vector<Sample>& x) {
    const auto data_bytes = static_cast<std::uint32_t>(x.size() * 8);
    std::string h = "RIFF";
    put_u32(h, 36 + data_bytes);
    h += "WAVEfmt ";
    put_u32(h, 16);
    put_u16(h, 3);  // IEEE float
    put_u16(h, 2);
    put_u32(h, static_cast<std::uint32_t>(rate));
    put_u32(h, static_cast<std::uint32_t>(rate) * 8);
    put_u16(h, 8);
    put_u16(h, 32);
    h += "data";
    put_u32(h, data_bytes);
    std::ofstream f(path, std::ios::binary);
    f.write(h.data(), static_cast<std::streamsize>(h.size()));
    f.write(reinterpret_cast<const char*>(x.data()), static_cast<std::streamsize>(data_bytes));
}

// Adds a complex tone A exp(j 2 pi f n / rate) to x over the samples [from, to).
void add_tone(std::vector<Sample>& x, int rate, double f_hz, double amplitude, std::size_t from = 0, std::size_t to = SIZE_MAX) {
    to = std::min(to, x.size());
    for (std::size_t n = from; n < to; ++n) {
        const double ph = 2.0 * std::numbers::pi * std::fmod(f_hz * static_cast<double>(n) / rate, 1.0);
        x[n] += Sample(static_cast<float>(amplitude * std::cos(ph)), static_cast<float>(amplitude * std::sin(ph)));
    }
}

// A real tone's amplitude from its power: sqrt(2 mean x^2), FS.
double amplitude_of(const std::vector<float>& a) {
    double p = 0.0;
    for (const float v : a) p += static_cast<double>(v) * v;
    return std::sqrt(2.0 * p / static_cast<double>(a.size()));
}

// A real tone's frequency from its upward zero crossings (interpolated linearly), Hz.
double frequency_of(const std::vector<float>& a, int rate) {
    double first = -1.0, last = -1.0;
    int crossings = 0;
    for (std::size_t i = 1; i < a.size(); ++i)
        if (a[i - 1] < 0.0f && a[i] >= 0.0f) {
            const double t = (static_cast<double>(i - 1) + a[i - 1] / (a[i - 1] - a[i])) / rate;
            if (first < 0.0) first = t;
            last = t;
            ++crossings;
        }
    return (crossings - 1) / (last - first);
}

// The times |z| crosses half the burst's amplitude, rising and falling (interpolated linearly), s from the clip's start.
std::pair<double, double> edges_of(const std::vector<Sample>& z, int rate, double amplitude) {
    const double h = 0.5 * amplitude;
    double rise = -1.0, fall = -1.0;
    for (std::size_t i = 1; i < z.size(); ++i) {
        const double a = std::abs(z[i - 1]), b = std::abs(z[i]);
        if (rise < 0.0 && a < h && b >= h) rise = (static_cast<double>(i - 1) + (h - a) / (b - a)) / rate;
        if (rise >= 0.0 && fall < 0.0 && a >= h && b < h) fall = (static_cast<double>(i - 1) + (a - h) / (a - b)) / rate;
    }
    return {rise, fall};
}

double db20(double ratio) { return 20.0 * std::log10(ratio); }

class ListenTest : public ::testing::Test {
protected:
    std::filesystem::path path_;
    void SetUp() override {
        const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
        path_ = kz4ap::test::temp_path(std::string("kz4ap_listen_") + info->name() + ".wav");
    }
    void TearDown() override {
        std::error_code ec;
        std::filesystem::remove(path_, ec);
    }
    ListenClip clip(const std::vector<Sample>& x, int rate, const ListenConfig& c, double start_s, double duration_s,
                    bool mirror = false) {
        write_iq_wav(path_, rate, x);
        RecordingReader reader(path_, RecordingOptions{.mirror = mirror});
        return listen_clip(reader, c, start_s, duration_s);
    }
};

constexpr int kRate = 48000;

}  // namespace

// The listening filter as listen.hpp states it, at both audio rates and two widths: within 0.01 dB of the passband for
// |f| <= 0.3 B, 6.02 dB below it (within 0.1 dB) at B / 2, at least 80 dB below it for |f| >= 0.7 B up to half the audio rate.
TEST(ListenFilter, MeetsItsStatedPassbandAndStopband) {
    for (const int rate : {8000, 12000})
        for (const double b : {500.0, 200.0}) {
            const std::vector<double> h = listen_filter_taps(b, rate);
            ASSERT_EQ(h.size() % 2, 1u);
            auto gain_db = [&](double f) {
                std::complex<double> s = 0.0;
                for (std::size_t n = 0; n < h.size(); ++n) s += h[n] * std::polar(1.0, -2.0 * std::numbers::pi * f * static_cast<double>(n) / rate);
                return db20(std::abs(s));
            };
            for (double f = 0.0; f <= 0.3 * b; f += 1.0) EXPECT_NEAR(gain_db(f), 0.0, 0.01) << rate << " " << b << " " << f;
            EXPECT_NEAR(gain_db(0.5 * b), -6.02, 0.1) << rate << " " << b;
            for (double f = 0.7 * b; f <= 0.5 * rate; f += 1.0) EXPECT_LT(gain_db(f), -80.0) << rate << " " << b << " " << f;
        }
    EXPECT_EQ(listen_filter_taps(500.0, 12000).size(), 311u);
    EXPECT_THROW(listen_filter_taps(10.0, 12000), std::invalid_argument);
}

// A station of 0.25 FS at a known offset comes out as a real tone at the chosen tone, within 0.05 dB re 0.25 FS, through
// the listening filter and through the track's channel filter; 40 Hz off the frequency, at the tone plus 40 Hz.
TEST_F(ListenTest, StationComesOutAtTheToneAndItsLevel) {
    std::vector<Sample> x(3 * kRate);
    add_tone(x, kRate, 3000.0, 0.25);
    for (const bool track : {false, true})
        for (const auto& [listen_at, tone, heard] : {std::tuple{3000.0, 600.0, 600.0}, std::tuple{3000.0, 700.0, 700.0},
                                                     std::tuple{2960.0, 600.0, 640.0}}) {
            ListenConfig c;
            c.offset_hz = listen_at;
            c.tone_hz = tone;
            c.track_filter = track;
            const ListenClip out = clip(x, kRate, c, 0.5, 2.0);
            ASSERT_EQ(out.audio.size(), 24000u);
            EXPECT_NEAR(frequency_of(out.audio, c.audio_rate), heard, 0.05) << track << " " << listen_at << " " << tone;
            EXPECT_NEAR(db20(amplitude_of(out.audio) / 0.25), 0.0, 0.05) << track << " " << listen_at << " " << tone;
            EXPECT_NEAR(10.0 * std::log10(out.band_power_fs2 / (0.25 * 0.25)), 0.0, 0.05);
        }
}

// Neighbors 1 kHz either side of the station, 0.5 FS each, with no station: at least 80 dB below their level through the
// listening filter (its stopband from 350 Hz) and 74 dB below it through the track's (its stopband from about 279 Hz). The side below
// is the image a real mixer would fold onto the tone's other side.
TEST_F(ListenTest, NeighborOneKilohertzAwayIsAttenuatedByTheStopband) {
    for (const double side : {+1000.0, -1000.0})
        for (const bool track : {false, true}) {
            std::vector<Sample> x(3 * kRate);
            add_tone(x, kRate, 3000.0 + side, 0.5);
            ListenConfig c;
            c.offset_hz = 3000.0;
            c.track_filter = track;
            const ListenClip out = clip(x, kRate, c, 0.5, 2.0);
            EXPECT_LT(db20(amplitude_of(out.audio) / 0.5), track ? -74.0 : -80.0) << side << " " << track;
        }
}

// --mirror flips the sign of the offset: a station at +3 kHz in the true spectrum, written mirrored (at -3 kHz in the file),
// is heard at +3 kHz with the mirror and at -3 kHz without it, and not at all (80 dB below 0.25 FS) at the other sign.
TEST_F(ListenTest, MirrorFlipsTheSign) {
    std::vector<Sample> x(3 * kRate);
    add_tone(x, kRate, 3000.0, 0.25);
    for (Sample& s : x) s = std::conj(s);  // the mirrored receiver's file
    for (const bool mirror : {true, false})
        for (const double sign : {+1.0, -1.0}) {
            ListenConfig c;
            c.offset_hz = sign * 3000.0;
            const ListenClip out = clip(x, kRate, c, 0.5, 2.0, mirror);
            const bool heard = (sign > 0) == mirror;
            if (heard) EXPECT_NEAR(db20(amplitude_of(out.audio) / 0.25), 0.0, 0.05) << mirror << " " << sign;
            else EXPECT_LT(db20(amplitude_of(out.audio) / 0.25), -80.0) << mirror << " " << sign;
        }
}

// The time window is exact: a 250 kHz recording (resampled to 192 kHz by the reader, as HDSDR's), a burst keyed down from
// sample 308 625 to 425 000 (its edges at 1.234 498 and 1.699 998 s, half a sample before those samples); clips from 1.0 s
// for 1.5 s and from 0 s for 2.5 s hold exactly 1.5 and 2.5 s of samples at 8 and 12 kHz, and |z| crosses half the burst's
// amplitude within 1 us of each edge (both filters linear-phase, so the half point is the edge), with either filter. The
// start's rounding to a whole input sample is taken out by the fractional delay (listen.cpp), so the errors are the
// edge-finding's (0.04 us at most, measured at 48, 96, 192 and 250 kHz), and a bookkeeping error of one input sample (5.2 us)
// fails (mutation-checked, review 13b). The burst sits at a bin's center, 213 x 23.4375 Hz: the track path's channel filter
// is symmetric about the bin, not about a station off it, so off the center the edges' half points spread apart (by 1.2 us
// each, rise earlier and fall later, measured at 7.8 Hz off): the burst's shape, not a shift of the clip.
TEST_F(ListenTest, TimeWindowIsExact) {
    constexpr int rate = 250000;
    constexpr std::size_t on = 308625, off = 425000;
    constexpr double station_hz = 213 * 23.4375;  // 4992.1875 Hz, a bin's center at 192 kHz and 8192 bins
    std::vector<Sample> x(3 * rate);
    add_tone(x, rate, station_hz, 0.25, on, off);
    const double rise = (on - 0.5) / rate, fall = (off - 0.5) / rate;
    write_iq_wav(path_, rate, x);
    for (const int audio_rate : {8000, 12000})
        for (const bool track : {false, true})
            for (const auto& [start, duration] : {std::pair{1.0, 1.5}, std::pair{0.0, 2.5}}) {
                RecordingReader reader(path_);
                ASSERT_TRUE(reader.resampled());
                ListenConfig c;
                c.audio_rate = audio_rate;
                c.offset_hz = station_hz;
                c.track_filter = track;
                const ListenClip out = listen_clip(reader, c, start, duration);
                ASSERT_EQ(out.audio.size(), static_cast<std::size_t>(std::llround(duration * audio_rate)));
                ASSERT_EQ(out.baseband.size(), out.audio.size());
                const auto [r, f] = edges_of(out.baseband, audio_rate, 0.25);
                EXPECT_NEAR(start + r, rise, 1e-6) << audio_rate << " " << track << " " << start;
                EXPECT_NEAR(start + f, fall, 1e-6) << audio_rate << " " << track << " " << start;
            }
}

// What the tool refuses: a tone too low for the band (its lower side would fold through 0 Hz), a rate other than 8 or 12 kHz,
// a frequency outside the recording's band, a negative start, a clip shorter than one audio sample (0.04 ms rounds to none at
// 12 kHz; 0.05 ms rounds to one and is written).
TEST_F(ListenTest, RefusesWhatDoesNotFit) {
    std::vector<Sample> x(kRate);
    write_iq_wav(path_, kRate, x);
    RecordingReader reader(path_);
    ListenConfig c;
    c.tone_hz = 300.0;  // below 0.7 x 500 Hz
    EXPECT_THROW(listen_clip(reader, c, 0.0, 0.5), std::invalid_argument);
    c = ListenConfig{};
    c.audio_rate = 11025;
    EXPECT_THROW(listen_clip(reader, c, 0.0, 0.5), std::invalid_argument);
    c = ListenConfig{};
    c.offset_hz = 23900.0;
    EXPECT_THROW(listen_clip(reader, c, 0.0, 0.5), std::invalid_argument);
    c = ListenConfig{};
    EXPECT_THROW(listen_clip(reader, c, -1.0, 0.5), std::invalid_argument);
    EXPECT_THROW(listen_clip(reader, c, 0.5, 0.04e-3), std::invalid_argument);
    EXPECT_EQ(listen_clip(reader, c, 0.5, 0.05e-3).audio.size(), 1u);
}

// A recording the reader resamples holds its band flat only to the resampler's passband: 86 kHz either side of the center at
// 192 kHz (A.1). A station whose listening band (0.7 x 500 Hz = 350 Hz either side) would pass it is refused; one inside is not.
TEST_F(ListenTest, RefusesAFrequencyBeyondTheResamplersFlatBand) {
    std::vector<Sample> x(250000 / 4);
    write_iq_wav(path_, 250000, x);
    RecordingReader reader(path_);
    ASSERT_TRUE(reader.resampled());
    ListenConfig c;
    c.offset_hz = 86000.0 - 352.0;
    EXPECT_NO_THROW(listen_clip(reader, c, 0.0, 0.1));
    c.offset_hz = 86000.0 - 348.0;
    EXPECT_THROW(listen_clip(reader, c, 0.0, 0.1), std::invalid_argument);
    c.offset_hz = -(86000.0 - 348.0);
    EXPECT_THROW(listen_clip(reader, c, 0.0, 0.1), std::invalid_argument);
}

// The WAV: a 44-byte header (PCM, mono, 16 bits, the rate), then the samples x gain x 32768, rounded, clipped and counted.
TEST_F(ListenTest, WritesAMono16BitWav) {
    const std::vector<float> audio = {0.0f, 0.5f, -0.5f, 0.999f, -1.0f};
    EXPECT_EQ(write_mono_wav16(path_, 12000, audio, 2.0), 3u);
    std::ifstream f(path_, std::ios::binary);
    std::vector<unsigned char> b((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    ASSERT_EQ(b.size(), 44u + 10u);
    auto u16 = [&](std::size_t i) { return static_cast<std::uint32_t>(b[i] | (b[i + 1] << 8)); };
    auto u32 = [&](std::size_t i) { return u16(i) | (u16(i + 2) << 16); };
    EXPECT_EQ(std::string(b.begin(), b.begin() + 4), "RIFF");
    EXPECT_EQ(u32(4), 46u);
    EXPECT_EQ(u16(20), 1u);   // PCM
    EXPECT_EQ(u16(22), 1u);   // mono
    EXPECT_EQ(u32(24), 12000u);
    EXPECT_EQ(u16(34), 16u);
    EXPECT_EQ(u32(40), 10u);
    const std::vector<std::int16_t> expected = {0, 32767, -32768, 32767, -32768};
    for (std::size_t i = 0; i < expected.size(); ++i) EXPECT_EQ(static_cast<std::int16_t>(u16(44 + 2 * i)), expected[i]) << i;
}
