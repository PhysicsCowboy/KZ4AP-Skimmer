#include "kz4ap/viterbi/front_filter.hpp"

#include "keyed_signals.hpp"
#include "kz4ap/channelizer.hpp"
#include "kz4ap/types.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <numbers>
#include <random>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using namespace kz4ap;
using namespace kz4ap::viterbi;
namespace signals = kz4ap::test;

namespace {

constexpr double kRate = 1500.0;  // samples/s: the channel's rate
constexpr double kDotS = 0.012;   // s: a 100 WPM dot
constexpr double kRiseS = 0.0025; // s: the 10-90% rise

std::vector<double> default_taps() { return front_filter_taps(kRate, kDotS, kRiseS); }

// |H(f)|^2 of the taps at f_hz, in dB relative to 0 Hz (dB re the power at 0 Hz).
double power_response_db(const std::vector<double>& h, double f_hz) {
    std::complex<double> acc = 0.0;
    std::complex<double> dc = 0.0;
    const auto m = static_cast<double>((h.size() - 1) / 2);
    for (std::size_t i = 0; i < h.size(); ++i) {
        const double j = static_cast<double>(i) - m;
        acc += h[i] * std::polar(1.0, -2.0 * std::numbers::pi * f_hz * j / kRate);
        dc += h[i];
    }
    return 10.0 * std::log10(std::norm(acc) / std::norm(dc));
}

// The channel filter's taps and decimation of the engine's channelizer at the given input rate and FFT size (bins of
// 23.4375 Hz, 1 500 channel samples/s).
struct ChannelCase {
    ChannelizerConfig config;
    int decimation;
};
ChannelCase channel_case(int sample_rate, int fft_size) {
    ChannelizerConfig config;
    config.sample_rate = sample_rate;
    config.fft_size = fft_size;
    return {config, fft_size / config.channel_bins};
}

OutputNoise noise_of(const ChannelCase& c) {
    return output_noise(channel_filter_taps(c.config), c.config.sample_rate, c.decimation, default_taps(), 4);
}

}  // namespace

TEST(FrontFilter, TapsAreThePulse) {
    const auto h = default_taps();
    ASSERT_EQ(h.size(), 25u);
    const std::array<double, 12> half = {0.00011, 0.00457, 0.01459, 0.02778, 0.04096, 0.05099,
                                         0.05545, 0.05556, 0.05556, 0.05556, 0.05556, 0.05556};
    // 0.00011 ... 0.05545, 0.05556 (x 11), 0.05545 ... 0.00011.
    std::vector<double> expected(half.begin(), half.begin() + 7);
    for (int i = 0; i < 11; ++i) expected.push_back(0.05556);
    for (int i = 6; i >= 0; --i) expected.push_back(half[static_cast<std::size_t>(i)]);
    ASSERT_EQ(expected.size(), 25u);
    double sum = 0.0;
    for (std::size_t i = 0; i < 25; ++i) {
        EXPECT_NEAR(h[i], expected[i], 5e-6) << i;
        EXPECT_NEAR(h[i], h[24 - i], 1e-15) << i;
        sum += h[i];
    }
    EXPECT_NEAR(sum, 1.0, 1e-12);
}

TEST(FrontFilter, TapCountFollowsTheDotAndTheRise) {
    EXPECT_EQ(front_filter_taps(1500.0, 0.012, 0.0025).size(), 25u);
    EXPECT_THROW(front_filter_taps(1500.0, 0.024, 0.0025), std::invalid_argument);  // a 50 WPM dot: 41 taps
    EXPECT_THROW(front_filter_taps(0.0, 0.012, 0.0025), std::invalid_argument);
    EXPECT_THROW(front_filter_taps(1500.0, 0.0, 0.0025), std::invalid_argument);
    EXPECT_THROW(front_filter_taps(1500.0, 0.012, 0.0), std::invalid_argument);
    // Other rates give 2 floor((T + E) r / 2) + 1 taps: 6 000 samples/s, 97 taps.
    EXPECT_EQ(front_filter_taps(6000.0, 0.012, 0.0025).size(), 97u);
}

TEST(FrontFilter, NoiseBandwidthIs76Hz) {
    const auto h = default_taps();
    double sum2 = 0.0;
    for (double v : h) sum2 += v * v;
    EXPECT_NEAR(kRate * sum2, 76.0, 0.05);  // Hz
}

TEST(FrontFilter, PowerResponse) {
    const auto h = default_taps();
    const std::array<std::pair<double, double>, 9> tight = {{{2, -0.01}, {10, -0.22}, {15, -0.50}, {20, -0.90},
                                                              {25, -1.42}, {30, -2.07}, {40, -3.82}, {50, -6.31},
                                                              {0, 0.0}}};
    for (const auto& [f, db] : tight) EXPECT_NEAR(power_response_db(h, f), db, 0.03) << f << " Hz";
    const std::array<std::pair<double, double>, 3> loose = {{{70, -15.5}, {120, -15.4}, {200, -24.5}}};
    for (const auto& [f, db] : loose) EXPECT_NEAR(power_response_db(h, f), db, 0.1) << f << " Hz";
}

TEST(FrontFilter, ASteadyCarrierGivesItsAmplitude) {
    // A carrier of amplitude 0.625 FS (exact in float) starting at channel sample 40: from 24 samples later e_k = a.
    EnvelopeSampler sampler(default_taps(), 4);
    std::vector<Sample> x(200);
    for (std::size_t i = 40; i < x.size(); ++i) x[i] = Sample(0.375f, 0.5f);
    std::vector<EnvelopeSample> out;
    sampler.push(x, 0, out);
    ASSERT_EQ(out.size(), 50u);
    for (const auto& s : out) {
        if (s.k * 4 >= 40 + 24) {
            EXPECT_NEAR(s.e_fs, 0.625, 0.625 * 1e-9) << s.k;
            EXPECT_NEAR(s.y_fs.real(), 0.375, 0.375 * 1e-9) << s.k;
            EXPECT_NEAR(s.y_fs.imag(), 0.5, 0.5 * 1e-9) << s.k;
        }
        if (s.k * 4 < 40) EXPECT_EQ(s.e_fs, 0.0) << s.k;
    }
}

TEST(OutputNoise, MatchesTheDesign) {
    for (const auto& c : {channel_case(48000, 2048), channel_case(192000, 8192)}) {
        SCOPED_TRACE("input rate " + std::to_string(c.config.sample_rate) + " samples/s");
        const OutputNoise n = noise_of(c);
        EXPECT_NEAR(n.sigma2_per_n0_hz, 74.48, 0.05);  // Hz
        const std::array<double, 7> rho = {1.0, 0.878, 0.626, 0.367, 0.138, 0.014, -0.005};
        for (std::size_t m = 0; m < rho.size(); ++m) EXPECT_NEAR(n.rho[m], rho[m], m == 0 ? 1e-12 : 0.002) << m;
        EXPECT_NEAR(n.w_strong, 0.1986, 0.0003);
        EXPECT_NEAR(n.w_weak, 0.2754, 0.0005);
        // Poisson summation (Part 4): w_strong = Delta x sigma_y^2 / N0, Delta = 4 / 1500 s.
        const double delta_s = 4.0 / kRate;
        EXPECT_NEAR(n.w_strong, delta_s * n.sigma2_per_n0_hz, 0.001 * n.w_strong);
    }
}

TEST(OutputNoise, WhiteNoiseThroughTheFrontFilterAloneHasItsNoiseBandwidth) {
    // A channel filter of one tap (unit gain, no decimation) at 1 500 samples/s is white noise of density N0 into the front
    // filter: sigma_y^2 / N0 = r sum h^2 = 76.0 Hz.
    const std::vector<double> one = {1.0};
    const OutputNoise n = output_noise(one, 1500.0, 1, default_taps(), 4);
    EXPECT_NEAR(n.sigma2_per_n0_hz, 76.0, 0.05);
    EXPECT_DOUBLE_EQ(n.rho[0], 1.0);
}

TEST(OutputNoise, RejectsBadArguments) {
    const std::vector<double> one = {1.0};
    EXPECT_THROW(output_noise({}, 1500.0, 1, default_taps(), 4), std::invalid_argument);
    EXPECT_THROW(output_noise(one, 1500.0, 1, {}, 4), std::invalid_argument);
    EXPECT_THROW(output_noise(one, 0.0, 1, default_taps(), 4), std::invalid_argument);
    EXPECT_THROW(output_noise(one, 1500.0, 0, default_taps(), 4), std::invalid_argument);
    EXPECT_THROW(output_noise(one, 1500.0, 1, default_taps(), 0), std::invalid_argument);
}

namespace {

bool full_sweep() {
    const char* value = std::getenv("KZ4AP_FRONT_FILTER_FULL_SWEEP");
    return value != nullptr && std::string(value) == "1";
}

// Complex white noise of total power sigma^2 (FS^2) per sample from the Mersenne twister's bits alone: the power
// -sigma^2 ln u1 is exponential with mean sigma^2 and the phase 2 pi u2 uniform.
class WhiteNoise {
public:
    WhiteNoise(unsigned seed, double sigma) : rng_(seed), sigma_(sigma) {}
    Sample next() {
        constexpr double kScale = 1.0 / 4294967296.0;
        const double u1 = (static_cast<double>(rng_()) + 1.0) * kScale;
        const double u2 = static_cast<double>(rng_()) * kScale;
        const double radius = sigma_ * std::sqrt(-std::log(u1));
        const double phase = 2.0 * std::numbers::pi * u2;
        return {static_cast<float>(radius * std::cos(phase)), static_cast<float>(radius * std::sin(phase))};
    }

private:
    std::mt19937 rng_;
    double sigma_;
};

}  // namespace

TEST(OutputNoise, MeasuredThroughTheChannelizer) {
    // Part 15, item 1; labeled full-sweep (the presets windows-full-sweep and linux-full-sweep run it, through the test
    // OutputNoise.FullSweep of engine/CMakeLists.txt, which sets KZ4AP_FRONT_FILTER_FULL_SWEEP=1; skipped otherwise): 600 s
    // of complex white noise, sigma = 0.02 FS, at 48 and 192 kHz through the Channelizer and the EnvelopeSampler: the
    // output's power within 1.5% of the design's and its lag correlations within 0.01.
    if (!full_sweep()) GTEST_SKIP() << "runs under the full-sweep presets (KZ4AP_FRONT_FILTER_FULL_SWEEP=1)";
    constexpr double kSigma = 0.02;        // FS per input sample
    constexpr double kDurationS = 600.0;   // s
    constexpr double kWarmupS = 1.0;       // s skipped while the filters fill
    for (const auto& c : {channel_case(48000, 2048), channel_case(192000, 8192)}) {
        SCOPED_TRACE("input rate " + std::to_string(c.config.sample_rate) + " samples/s");
        const OutputNoise design = noise_of(c);
        const double n0 = kSigma * kSigma / c.config.sample_rate;  // FS^2/Hz
        Channelizer channelizer(c.config);
        const int bin = channelizer.hz_to_bin(1000.0);
        channelizer.add_channel(1, bin);
        EnvelopeSampler sampler(default_taps(), 4);
        WhiteNoise noise(7, kSigma);
        std::vector<EnvelopeSample> out;
        std::vector<std::complex<double>> y;  // the envelope samples' y after the warm-up
        const auto total = static_cast<std::size_t>(kDurationS * c.config.sample_rate);
        const std::size_t chunk = static_cast<std::size_t>(c.config.sample_rate);  // one second
        std::vector<Sample> block;
        for (std::size_t done = 0; done < total; done += chunk) {
            block.assign(std::min(chunk, total - done), Sample{});
            for (Sample& s : block) s = noise.next();
            channelizer.push(block, [&](std::uint32_t, std::uint64_t first, std::span<const Sample> samples) {
                out.clear();
                sampler.push(samples, first, out);
                for (const auto& e : out) {
                    if (static_cast<double>(e.k) * 4.0 / kRate >= kWarmupS) y.push_back(e.y_fs);
                }
            });
        }
        ASSERT_GT(y.size(), 200000u);
        double power = 0.0;
        for (const auto& v : y) power += std::norm(v);
        power /= static_cast<double>(y.size());
        std::cout << "measured: Var(y) / N0 = " << power / n0 << " Hz (design " << design.sigma2_per_n0_hz << " Hz)";
        EXPECT_NEAR(power / n0, design.sigma2_per_n0_hz, 0.015 * design.sigma2_per_n0_hz) << "Hz";
        for (std::size_t m = 1; m <= 6; ++m) {
            double acc = 0.0;
            for (std::size_t i = 0; i + m < y.size(); ++i) acc += (y[i + m] * std::conj(y[i])).real();
            acc /= static_cast<double>(y.size() - m);
            std::cout << "; rho_" << m << " = " << acc / power << " (design " << design.rho[m] << ")";
            EXPECT_NEAR(acc / power, design.rho[m], 0.01) << "lag " << m;
        }
        std::cout << "\n";
    }
}

namespace {

std::vector<Sample> noise_block(std::size_t n, unsigned seed) {
    WhiteNoise noise(seed, 1.0);
    std::vector<Sample> x(n);
    for (Sample& s : x) s = noise.next();
    return x;
}

}  // namespace

TEST(EnvelopeSampler, SplitsDoNotMatter) {
    const std::vector<Sample> x = noise_block(5003, 3);
    std::vector<EnvelopeSample> whole;
    EnvelopeSampler(default_taps(), 4).push(x, 0, whole);
    ASSERT_EQ(whole.size(), (x.size() + 3) / 4);
    for (const std::size_t block : {1u, 7u, 32u, 1000u}) {
        SCOPED_TRACE("blocks of " + std::to_string(block));
        EnvelopeSampler sampler(default_taps(), 4);
        std::vector<EnvelopeSample> split;
        for (std::size_t at = 0; at < x.size(); at += block) {
            const std::size_t len = std::min(block, x.size() - at);
            sampler.push(std::span<const Sample>(x).subspan(at, len), at, split);
        }
        ASSERT_EQ(split.size(), whole.size());
        for (std::size_t i = 0; i < whole.size(); ++i) {
            EXPECT_EQ(split[i].k, whole[i].k);
            EXPECT_EQ(split[i].e_fs, whole[i].e_fs);
            EXPECT_EQ(split[i].y_fs, whole[i].y_fs);
        }
    }
}

TEST(EnvelopeSampler, EveryFourthChannelSample) {
    const std::vector<Sample> x = noise_block(100, 5);
    for (const std::uint64_t first : {0u, 1u, 2u, 3u, 4u, 4001u, 4002u, 4003u}) {
        SCOPED_TRACE("first index " + std::to_string(first));
        EnvelopeSampler sampler(default_taps(), 4);
        std::vector<EnvelopeSample> out;
        sampler.push(x, first, out);
        std::vector<std::int64_t> expected;
        for (std::uint64_t i = first; i < first + x.size(); ++i) {
            if (i % 4 == 0) expected.push_back(static_cast<std::int64_t>(i / 4));
        }
        ASSERT_EQ(out.size(), expected.size());
        for (std::size_t i = 0; i < out.size(); ++i) {
            EXPECT_EQ(out[i].k, expected[i]);
            EXPECT_DOUBLE_EQ(out[i].e_fs, std::abs(out[i].y_fs));
        }
    }
}

TEST(EnvelopeSampler, ComputesTheCausalConvolution) {
    // y[n] = sum_i h[i] x[n - i] with the history before the first sample zero.
    const auto h = default_taps();
    const std::vector<Sample> x = noise_block(120, 9);
    EnvelopeSampler sampler(h, 4);
    std::vector<EnvelopeSample> out;
    sampler.push(x, 0, out);
    for (const auto& s : out) {
        std::complex<double> acc = 0.0;
        for (std::size_t i = 0; i < h.size(); ++i) {
            const std::int64_t n = s.k * 4 - static_cast<std::int64_t>(i);
            if (n >= 0) acc += h[i] * std::complex<double>(x[static_cast<std::size_t>(n)]);
        }
        EXPECT_NEAR(s.y_fs.real(), acc.real(), 1e-12) << s.k;
        EXPECT_NEAR(s.y_fs.imag(), acc.imag(), 1e-12) << s.k;
    }
}

TEST(EnvelopeSampler, ResetForgetsTheHistory) {
    const std::vector<Sample> x = noise_block(60, 11);
    EnvelopeSampler sampler(default_taps(), 4);
    std::vector<EnvelopeSample> first;
    sampler.push(x, 0, first);
    sampler.reset();
    std::vector<EnvelopeSample> second;
    sampler.push(x, 0, second);
    ASSERT_EQ(first.size(), second.size());
    for (std::size_t i = 0; i < first.size(); ++i) EXPECT_EQ(first[i].y_fs, second[i].y_fs);
}

namespace {

// The time (s) at which the envelope first crosses `level` going up (rising) or down, linearly interpolated, searching
// from instant `from`.
double crossing_s(const std::vector<double>& env, double rate, std::size_t from, double level, bool rising) {
    for (std::size_t i = from; i + 1 < env.size(); ++i) {
        const bool hit = rising ? (env[i] < level && env[i + 1] >= level) : (env[i] > level && env[i + 1] <= level);
        if (hit) return (static_cast<double>(i) + (level - env[i]) / (env[i + 1] - env[i])) / rate;
    }
    return -1.0;
}

}  // namespace

TEST(KeyedSignals, D17EdgesAtHalfAmplitude) {
    constexpr double kFs = 48000.0;
    const double edge_s = 0.0025 / signals::raised_cosine_10_90();  // 4.2349 ms
    const std::vector<std::pair<double, double>> dot = {{0.5, 0.512}};
    const auto n = static_cast<std::size_t>(1.0 * kFs);
    // Start: half amplitude at on + E / 2 and off + E / 2.
    const auto env = signals::keying_envelope_d17(dot, kFs, n);
    EXPECT_NEAR(crossing_s(env, kFs, 0, 0.5, true), 0.5 + edge_s / 2, 1e-7);
    EXPECT_NEAR(crossing_s(env, kFs, 0, 0.5, false), 0.512 + edge_s / 2, 1e-7);
    // The 10% and 90% points of the rise are 2.5 ms apart.
    EXPECT_NEAR(crossing_s(env, kFs, 0, 0.9, true) - crossing_s(env, kFs, 0, 0.1, true), 0.0025, 1e-6);
    // Nothing before the key-down.
    EXPECT_EQ(env[static_cast<std::size_t>(0.4999 * kFs)], 0.0);
    // Centered: half amplitude at on and off themselves.
    const auto centered = signals::keying_envelope_d17(dot, kFs, n, 0.0025, signals::Placement::Centered);
    EXPECT_NEAR(crossing_s(centered, kFs, 0, 0.5, true), 0.5, 1e-7);
    EXPECT_NEAR(crossing_s(centered, kFs, 0, 0.5, false), 0.512, 1e-7);
    // The carrier: a long mark reaches amplitude 1 (a 100 WPM dot is 65% flat), with the keyed amplitude.
    const auto x = signals::keyed_signal_d17(dot, kFs, 1.0, 0.0, 0.3);
    double peak = 0.0;
    for (const Sample& s : x) peak = std::max(peak, static_cast<double>(std::abs(s)));
    EXPECT_NEAR(peak, 0.3, 1e-6);
}

TEST(KeyedSignals, TextKeyingMatchesTheIntervals) {
    const auto a = signals::keyed_text_d17("PARIS", 20.0, 8000.0, 4.0, 100.0, 0.5, 0.0, 1, 0.5);
    const auto b = signals::keyed_signal_d17(signals::keying("PARIS", 20.0, 0.5), 8000.0, 4.0, 100.0, 0.5, 0.0, 1);
    ASSERT_EQ(a.size(), b.size());
    for (std::size_t i = 0; i < a.size(); ++i) EXPECT_EQ(a[i], b[i]) << i;
}

TEST(KeyedSignals, RayleighGainHasUnitPowerAndItsSpread) {
    // Gaussian Doppler spectrum of two-sigma width 2 Hz (sigma_f = 1 Hz): autocorrelation exp(-2 pi^2 sigma_f^2 tau^2),
    // 0.454 at tau = 0.2 s. 1 200 s at 200 samples/s.
    constexpr double kRateHz = 200.0;
    const auto g = signals::rayleigh_gain(static_cast<std::size_t>(1200 * kRateHz), kRateHz, 2.0, 4);
    double power = 0.0;
    for (const auto& v : g) power += std::norm(v);
    power /= static_cast<double>(g.size());
    EXPECT_NEAR(power, 1.0, 0.08);
    const auto lag = static_cast<std::size_t>(0.2 * kRateHz);
    std::complex<double> acc = 0.0;
    for (std::size_t i = 0; i + lag < g.size(); ++i) acc += g[i + lag] * std::conj(g[i]);
    acc /= static_cast<double>(g.size() - lag);
    EXPECT_NEAR(std::abs(acc) / power, std::exp(-2.0 * std::numbers::pi * std::numbers::pi * 0.2 * 0.2), 0.06);
    const auto again = signals::rayleigh_gain(1000, kRateHz, 2.0, 4);
    EXPECT_EQ(again[500], signals::rayleigh_gain(1000, kRateHz, 2.0, 4)[500]);
}
