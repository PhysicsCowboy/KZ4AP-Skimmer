#include "kz4ap/viterbi/observation.hpp"

#include "kz4ap/channelizer.hpp"
#include "kz4ap/types.hpp"
#include "kz4ap/viterbi/config.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <numbers>
#include <numeric>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using namespace kz4ap::viterbi;

namespace {

constexpr double kInf = std::numeric_limits<double>::infinity();
constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
constexpr double kPi = std::numbers::pi;

// The largest error seen, and where. A NaN error is kept as the largest and never replaced, so that a check on it
// fails; std::max(worst, NaN) would have dropped it.
struct Worst {
    double error = 0.0;
    std::string where;
    template <class Describe>
    void add(double e, Describe describe) {
        if (std::isnan(error)) return;
        if (std::isnan(e) || e > error) {
            error = e;
            where = describe();
        }
    }
};

std::string number(double x) { return std::to_string(x); }

// The engine's channelizer at an input rate: an FFT of 2 048 points at 48 kHz and 8 192 at 192 kHz, so that a bin is
// 23.4375 Hz, the decimation fft_size / 64 and the channel rate 1 500 samples/s.
kz4ap::ChannelizerConfig channelizer_at(int rate_hz) {
    kz4ap::ChannelizerConfig config;
    config.sample_rate = rate_hz;
    config.fft_size = rate_hz / 48000 * 2048;
    return config;
}

// c(m), Hz, m = 0 ... max_samples, on the channelizer's own filter at an input rate.
std::vector<double> sum_noise_at(int rate_hz, int max_samples) {
    const kz4ap::ChannelizerConfig config = channelizer_at(rate_hz);
    return sum_noise_hz(kz4ap::channel_filter_taps(config), rate_hz, config.fft_size / config.channel_bins,
                        max_samples);
}

// ln I0(x), independently of ln_i0: std::cyl_bessel_i up to x = 700 (I0(700) = 1.5e302, below the largest double),
// and above it the asymptotic series (Abramowitz and Stegun 9.7.1), ln I0(x) = x - ln(2 pi x) / 2 + ln(1 + 1/(8x) +
// 9/(128 x^2) + 225/(3072 x^3) + 11025/(98304 x^4)), whose next term is below 1.4e-15 there.
double reference_ln_i0(double x) {
    x = std::fabs(x);
    if (x <= 700.0) return std::log(std::cyl_bessel_i(0.0, x));
    const double y = 1.0 / x;
    const double series =
        1.0 + y * (1.0 / 8.0 + y * (9.0 / 128.0 + y * (225.0 / 3072.0 + y * (11025.0 / 98304.0))));
    return x - 0.5 * std::log(2.0 * kPi * x) + std::log(series);
}

// The standard normal's distribution function.
double normal_cdf(double x) { return 0.5 * std::erfc(-x / std::numbers::sqrt2); }

// Simpson's weight of point i of 0 ... intervals (an even number).
double simpson_weight(int i, int intervals) {
    if (i == 0 || i == intervals) return 1.0;
    return i % 2 != 0 ? 4.0 : 2.0;
}

// An evidence (nats) with the posterior's mean, variance and mode beside it (the mode from dense_uncertain only).
struct Dense {
    double llr;
    double mean;
    double var;
    double mode = kNaN;
};

// Lambda_P and the level's posterior by direct integration over a (FS), from a_from to a_to: Simpson's rule on 20 001
// points of N(a; mu, p) e^Lambda(S; a), Lambda = -b^2 + ln I0(2 b z) with reference_ln_i0, in the log so that nothing
// overflows, less ln Phi(mu / sqrt(p)) for the truncation at a = 0. The posterior's mean and mode are in FS, its
// variance FS^2; the mode is the grid's highest point, refined by the parabola through it and its neighbors.
Dense dense_uncertain(double s_abs, double m_signal, double mu, double p, double v, double a_from, double a_to) {
    constexpr int kIntervals = 20000;
    const double step = (a_to - a_from) / kIntervals;
    const double root_v = std::sqrt(v);
    const double z = s_abs / root_v;
    std::vector<double> a(kIntervals + 1);
    std::vector<double> log_f(kIntervals + 1);
    double top = -kInf;
    for (int i = 0; i <= kIntervals; ++i) {
        a[i] = a_from + i * step;
        const double b = m_signal * a[i] / root_v;
        log_f[i] = -0.5 * (a[i] - mu) * (a[i] - mu) / p - 0.5 * std::log(2.0 * kPi * p) - b * b +
                   reference_ln_i0(2.0 * b * z);
        top = std::max(top, log_f[i]);
    }
    double s0 = 0.0;
    double s1 = 0.0;
    std::vector<double> f(kIntervals + 1);
    for (int i = 0; i <= kIntervals; ++i) {
        f[i] = simpson_weight(i, kIntervals) * std::exp(log_f[i] - top);
        s0 += f[i];
        s1 += f[i] * a[i];
    }
    const double mean = s1 / s0;
    double s2 = 0.0;
    for (int i = 0; i <= kIntervals; ++i) s2 += f[i] * (a[i] - mean) * (a[i] - mean);
    const auto peak = static_cast<std::size_t>(std::max_element(log_f.begin(), log_f.end()) - log_f.begin());
    double mode = a[peak];
    if (peak > 0 && peak + 1 < log_f.size()) {
        const double bend = log_f[peak - 1] - 2.0 * log_f[peak] + log_f[peak + 1];
        if (bend < 0.0) mode += 0.5 * step * (log_f[peak - 1] - log_f[peak + 1]) / bend;
    }
    return {top + std::log(s0 * step / 3.0) - std::log(normal_cdf(mu / std::sqrt(p))), mean, s2 / s0, mode};
}

// The same over the whole of a >= 0 where the integrand is not negligible: from 0 to (max(b, z) + 15) sqrt(v) /
// m_signal, b = m_signal mu / sqrt(v) the prior's center and z = s_abs / sqrt(v). The posterior's mode lies below
// max(b, z), and above it the integrand falls at least as fast as the likelihood's bound e^(z^2 - (b - z)^2)
// (ln I0(y) <= y).
Dense dense_uncertain(double s_abs, double m_signal, double mu, double p, double v) {
    const double root_v = std::sqrt(v);
    const double top_b = std::max(m_signal * mu / root_v, s_abs / root_v) + 15.0;
    return dense_uncertain(s_abs, m_signal, mu, p, v, 0.0, top_b * root_v / m_signal);
}

// Lambda_flat and the posterior of b by direct integration over the level x, dB S500, from level_min_db to
// level_max_db: Simpson's rule on 50 001 points (steps of 0.002 dB of x, x in dB S500) of e^Lambda(S; a(x)) /
// (level_max_db - level_min_db), with reference_ln_i0, in the log. The block's pieces are independent at the long-run
// rate, rate_hz per channel sample, and m_signal (channel samples) is the carrier's expected weighted sum over a, so
// V = N0 x rate_hz x m_signal and b(x)^2 = (m_signal a)^2 / V = 10^(x / 10) x 500 Hz x m_signal / rate_hz. Points
// whose bound on Lambda, z^2 - (b - z)^2 (ln I0(y) <= y), lies more than 1 000 nats below 0 are left out: Lambda at the
// lowest level, where b <= 6, is above -37 nats.
Dense direct_flat(const ViterbiConfig& config, double m_signal, double rate_hz, double z) {
    constexpr int kIntervals = 50000;
    const double gain = 500.0 * m_signal / rate_hz;
    const double width_db = config.level_max_db - config.level_min_db;
    const double step = width_db / kIntervals;
    std::vector<double> b(kIntervals + 1);
    std::vector<double> log_f(kIntervals + 1);
    double top = -kInf;
    for (int i = 0; i <= kIntervals; ++i) {
        const double x = config.level_min_db + i * step;
        b[i] = std::sqrt(std::pow(10.0, x / 10.0) * gain);
        const double bound = z * z - (b[i] - z) * (b[i] - z);
        log_f[i] = bound < -1000.0 ? -kInf : -b[i] * b[i] + reference_ln_i0(2.0 * b[i] * z);
        top = std::max(top, log_f[i]);
    }
    double s0 = 0.0;
    double s1 = 0.0;
    std::vector<double> f(kIntervals + 1);
    for (int i = 0; i <= kIntervals; ++i) {
        f[i] = simpson_weight(i, kIntervals) * std::exp(log_f[i] - top);
        s0 += f[i];
        s1 += f[i] * b[i];
    }
    const double mean = s1 / s0;
    double s2 = 0.0;
    for (int i = 0; i <= kIntervals; ++i) s2 += f[i] * (b[i] - mean) * (b[i] - mean);
    return {top + std::log(s0 * step / 3.0 / width_db), mean, s2 / s0};
}

// Complex white Gaussian noise of variance sigma^2 per sample (sigma^2 / 2 in I and in Q): Marsaglia's polar method on
// a splitmix64 stream, the same on every platform up to the last bit of std::log and std::sqrt.
class WhiteNoise {
public:
    WhiteNoise(std::uint64_t seed, double sigma) : state_(seed), scale_(sigma / std::numbers::sqrt2) {}
    kz4ap::Sample next() {
        double u = 0.0;
        double v = 0.0;
        double s = 0.0;
        do {
            u = static_cast<double>(draw() >> 11) * 0x1.0p-52 - 1.0;  // [-1, 1)
            v = static_cast<double>(draw() >> 11) * 0x1.0p-52 - 1.0;
            s = u * u + v * v;
        } while (s >= 1.0 || s == 0.0);
        const double r = scale_ * std::sqrt(-2.0 * std::log(s) / s);
        return {static_cast<float>(r * u), static_cast<float>(r * v)};
    }

private:
    std::uint64_t draw() {
        std::uint64_t x = (state_ += 0x9E3779B97F4A7C15ull);
        x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
        x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
        return x ^ (x >> 31);
    }
    std::uint64_t state_;
    double scale_;
};

// The mean square of the sums of m consecutive channel samples, for each m of `lengths`, over every position in every
// channel once the channel's first 96 samples (while the filter fills) have passed: sliding sums, kept sample by
// sample.
class SumSquares {
public:
    SumSquares(int channels, std::vector<int> lengths)
        : lengths_(std::move(lengths)),
          ring_size_(static_cast<std::size_t>(*std::max_element(lengths_.begin(), lengths_.end())) + 1),
          channels_(static_cast<std::size_t>(channels)),
          total_(lengths_.size(), 0.0),
          count_(lengths_.size(), 0) {
        for (Channel& channel : channels_) {
            channel.ring.assign(ring_size_, {});
            channel.sums.assign(lengths_.size(), {});
        }
    }

    void add(std::uint32_t id, kz4ap::Sample sample) {
        constexpr std::size_t kWarmup = 96;
        Channel& channel = channels_[id];
        if (channel.warmup < kWarmup) {
            ++channel.warmup;
            return;
        }
        const std::complex<double> y(sample.real(), sample.imag());
        channel.ring[channel.next] = y;
        ++channel.seen;
        for (std::size_t l = 0; l < lengths_.size(); ++l) {
            const auto m = static_cast<std::size_t>(lengths_[l]);
            std::complex<double>& sum = channel.sums[l];
            sum += y;
            if (channel.seen > m)  // the sample m before the newest leaves the sum
                sum -= channel.ring[channel.next >= m ? channel.next - m : channel.next + ring_size_ - m];
            if (channel.seen >= m) {
                total_[l] += std::norm(sum);
                ++count_[l];
            }
        }
        channel.next = channel.next + 1 == ring_size_ ? 0 : channel.next + 1;
    }
    double mean_square(std::size_t l) const { return total_[l] / static_cast<double>(count_[l]); }

private:
    struct Channel {
        std::vector<std::complex<double>> ring;  // the last ring_size_ samples; the newest at next - 1
        std::vector<std::complex<double>> sums;  // the sliding sum for each length
        std::size_t next = 0;
        std::size_t seen = 0;    // samples after the warm-up
        std::size_t warmup = 0;  // samples left out so far
    };
    std::vector<int> lengths_;
    std::size_t ring_size_;
    std::vector<Channel> channels_;
    std::vector<double> total_;
    std::vector<std::uint64_t> count_;
};

// The noise variance V(m), FS^2, of a sum of m channel samples at 48 kHz with sigma = 0.02 FS per input sample.
double block_variance(int m) {
    const double n0 = 0.02 * 0.02 / 48000.0;  // FS^2/Hz
    return n0 * sum_noise_at(48000, m)[static_cast<std::size_t>(m)];
}

}  // namespace

TEST(Observation, ChannelFilterTapsAreSymmetricAndSumToOne) {
    // fft_size / 2 + 1 taps, symmetric about the middle, summing to 1: 1 025 at 48 kHz and 4 097 at 192 kHz. (That they
    // are the Channelizer's own rests on its building its filter from them, and on its unchanged tests.)
    for (const int rate : {48000, 192000}) {
        SCOPED_TRACE(std::to_string(rate) + " samples/s");
        const kz4ap::ChannelizerConfig config = channelizer_at(rate);
        const std::vector<double> taps = kz4ap::channel_filter_taps(config);
        ASSERT_EQ(taps.size(), static_cast<std::size_t>(config.fft_size / 2 + 1));
        double sum = 0.0;
        for (const double tap : taps) sum += tap;
        EXPECT_NEAR(sum, 1.0, 1e-12);
        for (std::size_t i = 0; i < taps.size(); ++i) EXPECT_NEAR(taps[i], taps[taps.size() - 1 - i], 1e-15) << i;
    }
    // It refuses what the Channelizer refuses.
    EXPECT_THROW((void)kz4ap::channel_filter_taps({.fft_size = 8191}), std::invalid_argument);
    EXPECT_THROW((void)kz4ap::channel_filter_taps({.sample_rate = 0}), std::invalid_argument);
    EXPECT_THROW((void)kz4ap::channel_filter_taps({.cutoff_hz = 700.0}), std::invalid_argument);
    EXPECT_NO_THROW((void)kz4ap::channel_filter_taps({.cutoff_hz = 620.0}));
}

TEST(Observation, SumNoiseMatchesNoiseThroughTheChannelizer) {
    // 600 s of complex white Gaussian noise of sigma = 0.02 FS (sigma^2 per complex sample) through the channelizer at
    // 48 kHz and at 192 kHz: the mean square of the sums of 3, 24 and 585 consecutive channel samples is N0 c(m),
    // N0 = sigma^2 / fs, within 2%. One channel's 600 s holds only 1 538 sums of 585 samples that do not overlap, and
    // the mean square over every position scatters by 2.1% (one standard deviation, derived from R[k]), as much as the
    // tolerance; so the test reads 32 channels at once, 1 500 Hz apart, which share no bin of the FFT and so carry
    // independent noise: the scatter is then 0.37% at 585 samples, 0.08% at 24 and 0.04% at 3.
    constexpr double kSigma = 0.02;
    constexpr int kSeconds = 600;
    constexpr int kChannels = 32;
    const std::vector<int> lengths = {3, 24, 585};
    for (const int rate : {48000, 192000}) {
        SCOPED_TRACE(std::to_string(rate) + " samples/s");
        const std::vector<double> c = sum_noise_at(rate, 585);
        kz4ap::Channelizer channelizer(channelizer_at(rate));
        for (int k = 0; k < kChannels; ++k) channelizer.add_channel(static_cast<std::uint32_t>(k), -992 + 64 * k);
        SumSquares sums(kChannels, lengths);
        const auto sink = [&sums](std::uint32_t id, std::uint64_t, std::span<const kz4ap::Sample> samples) {
            for (const kz4ap::Sample& s : samples) sums.add(id, s);
        };
        WhiteNoise noise(20261020u + static_cast<std::uint64_t>(rate), kSigma);
        std::vector<kz4ap::Sample> block(static_cast<std::size_t>(rate / 10));  // 0.1 s
        for (int pushed = 0; pushed < kSeconds * 10; ++pushed) {
            for (kz4ap::Sample& s : block) s = noise.next();
            channelizer.push(block, sink);
        }
        const double n0 = kSigma * kSigma / rate;  // FS^2/Hz
        for (std::size_t l = 0; l < lengths.size(); ++l) {
            const auto m = static_cast<std::size_t>(lengths[l]);
            EXPECT_NEAR(sums.mean_square(l) / (n0 * c[m]), 1.0, 0.02) << "sums of " << m << " samples";
        }
    }
}

TEST(Observation, LongSumsGrowAt1500Hz) {
    // The R[k] add up to N0 x 1 500 Hz, the channel rate (the filter passes nothing at the multiples of the rate,
    // where the decimation folds the spectrum), so c(m) / m -> 1 500 Hz, less sum |k| R[k] / (N0 m) = 1 942 Hz / m:
    // within 0.5% from m = 300 (200 ms) on, 0.43% at 300. c(1) = R[0] / N0 is the filter's noise bandwidth, 251.86 Hz.
    for (const int rate : {48000, 192000}) {
        SCOPED_TRACE(std::to_string(rate) + " samples/s");
        const std::vector<double> c = sum_noise_at(rate, 3000);
        ASSERT_EQ(c.size(), 3001u);
        EXPECT_EQ(c[0], 0.0);
        EXPECT_NEAR(c[1], 251.86, 0.01);
        Worst worst;
        for (int m = 300; m <= 3000; ++m)
            worst.add(std::fabs(c[static_cast<std::size_t>(m)] / m / 1500.0 - 1.0),
                      [m] { return "m = " + std::to_string(m); });
        EXPECT_LE(worst.error, 0.005) << worst.where;
        EXPECT_NEAR(c[3000] / 3000.0, 1500.0 - 1942.25 / 3000.0, 0.01);
    }
}

TEST(Observation, IndependentPiecesUnderstateTheNoise) {
    // Summed as if its pieces of 3 samples were independent, a mark of n pieces would have the noise variance
    // n V(3); it has V(3 n). On the real filter V(3 n) lies 2.8 to 3.3 dB above n V(3), in dB of noise variance, for
    // marks of 8 to 720 ms (4 to 360 pieces; design notes, topic 4, item 7): a ratio of 1.91 to 2.14, rising with the
    // mark's length toward its limit for long marks, 4 500 Hz / c(3) = 2.138 (3.30 dB of noise variance), as
    // c(m) -> 1 500 Hz x m (derived).
    for (const int rate : {48000, 192000}) {
        SCOPED_TRACE(std::to_string(rate) + " samples/s");
        const std::vector<double> c = sum_noise_at(rate, 3 * 360);
        Worst below;
        Worst above;
        for (int n = 4; n <= 360; ++n) {
            const double ratio_db = 10.0 * std::log10(c[static_cast<std::size_t>(3 * n)] / (n * c[3]));
            const auto at = [n, ratio_db] {
                return std::to_string(n) + " pieces: " + number(ratio_db) + " dB of noise variance";
            };
            below.add(2.8 - ratio_db, at);
            above.add(ratio_db - 3.3, at);
        }
        EXPECT_LE(below.error, 0.0) << "below 2.8 dB of noise variance at " << below.where;
        EXPECT_LE(above.error, 0.0) << "above 3.3 dB of noise variance at " << above.where;
        EXPECT_NEAR(4500.0 / c[3], 2.138, 0.0005);
    }
}

TEST(Observation, LnI0IsAccurate) {
    // Against std::cyl_bessel_i on [0, 700] (140 001 points) and against the asymptotic series above, up to 1e9, where
    // the double's spacing reaches 1.2e-7; the errors Abramowitz and Stegun state for 9.8.1 and 9.8.2 make it exact to
    // 4.8e-7 nats. Beyond 1e9 (2 b z reaches 2e10 at z = b = 1e5) the series is met to the double's own spacing.
    Worst low;
    for (int i = 0; i <= 140000; ++i) {
        const double x = i * 0.005;
        low.add(std::fabs(ln_i0(x) - reference_ln_i0(x)), [x] { return "x = " + number(x); });
    }
    EXPECT_LE(low.error, 1e-6) << low.where;
    Worst high;
    for (double x = 700.0; x <= 1e9; x *= 1.001)
        high.add(std::fabs(ln_i0(x) - reference_ln_i0(x)), [x] { return "x = " + number(x); });
    EXPECT_LE(high.error, 1e-6) << high.where;
    Worst highest;
    for (double x = 1e9; x <= 1e15; x *= 1.01)
        highest.add(std::fabs(ln_i0(x) - reference_ln_i0(x)) / (x * std::numeric_limits<double>::epsilon()),
                    [x] { return "x = " + number(x); });
    EXPECT_LE(highest.error, 4.0) << "spacings of a double at " << highest.where;
    // I0 is even, I0(0) = 1, and nothing is lost at the ends.
    EXPECT_EQ(ln_i0(0.0), 0.0);
    EXPECT_EQ(ln_i0(-2.5), ln_i0(2.5));
    EXPECT_EQ(ln_i0(-1234.5), ln_i0(1234.5));
    EXPECT_EQ(ln_i0(kInf), kInf);
    EXPECT_TRUE(std::isnan(ln_i0(kNaN)));
}

TEST(Observation, QuadratureMatchesADenseIntegral) {
    // block_llr_uncertain at the configured order, 5, against a 20 001-point integral over a, for prior centers
    // b = m mu / sqrt(v) of 1, 3, 10 and 30, z = 0, b/2, b and 2b, and relative spreads sqrt(p) / mu of 0.03, 0.1,
    // 0.3 and 0.5, on a block of 24 samples at 48 kHz (sigma = 0.02 FS). level_posterior's mean and variance against
    // the same integral.
    const int order = ViterbiConfig{}.quadrature_order;
    ASSERT_EQ(order, 5);
    const int m = 24;
    const double v = block_variance(m);
    const double root_v = std::sqrt(v);
    Worst llr;
    Worst mean;
    Worst var;
    for (const double b : {1.0, 3.0, 10.0, 30.0}) {
        for (const double z : {0.0, b / 2.0, b, 2.0 * b}) {
            for (const double spread : {0.03, 0.1, 0.3, 0.5}) {
                const auto at = [b, z, spread] {
                    return "b = " + number(b) + ", z = " + number(z) + ", spread " + number(spread);
                };
                const double mu = b * root_v / m;
                const double p = (spread * mu) * (spread * mu);
                const double s_abs = z * root_v;
                const Dense dense = dense_uncertain(s_abs, m, mu, p, v);
                llr.add(std::fabs(block_llr_uncertain(s_abs, m, mu, p, v, order) - dense.llr), at);
                const Moments posterior = level_posterior(s_abs, m, mu, p, v, order);
                mean.add(std::fabs(posterior.mean - dense.mean) / std::sqrt(dense.var), at);
                var.add(std::fabs(posterior.var / dense.var - 1.0), at);
            }
        }
    }
    EXPECT_LE(llr.error, 0.01) << "nats, at " << llr.where;
    EXPECT_LE(mean.error, 0.01) << "posterior standard deviations, at " << mean.where;
    EXPECT_LE(var.error, 0.02) << "relative, at " << var.where;
}

TEST(Observation, UncertainLevelIsExactOnAnEmptyBlock) {
    // With |S| = 0 the likelihood, e^-b^2, is Gaussian in b, so the integral has a closed form: with
    // b' = b / (1 + 2 q) and q' = q / (1 + 2 q) (b and q the prior's mean and variance in units of sqrt(v) / m and
    // v / m^2),
    //     Lambda_P = -b^2 / (1 + 2 q) - ln(1 + 2 q) / 2 + ln Phi(b' / sqrt(q')) - ln Phi(b / sqrt(q)),
    // and the posterior is N(b', q') truncated at 0. The quadrature's nodes, from the Gaussian at the mode truncated
    // at 0 and mapped onto the standard normal, make it exact to rounding, however much the truncation cuts off; its
    // moments are close (the truncated normal's mean and variance).
    const int m = 24;
    const double v = block_variance(m);
    const double unit = std::sqrt(v) / m;  // FS per unit of b
    Worst llr;
    Worst mean;
    Worst var;
    for (const double b : {0.0, 0.5, 1.0, 3.0, 10.0, 30.0}) {
        for (const double q : {1e-4, 0.01, 0.25, 1.0, 9.0, 100.0, 1e4}) {
            const auto at = [b, q] { return "b = " + number(b) + ", q = " + number(q); };
            const double shrunk = b / (1.0 + 2.0 * q);
            const double shrunk_q = q / (1.0 + 2.0 * q);
            const double alpha = shrunk / std::sqrt(shrunk_q);
            const double closed = -b * b / (1.0 + 2.0 * q) - 0.5 * std::log1p(2.0 * q) +
                                  std::log(normal_cdf(alpha)) - std::log(normal_cdf(b / std::sqrt(q)));
            const double mu = b * unit;
            const double p = q * unit * unit;
            llr.add(std::fabs(block_llr_uncertain(0.0, m, mu, p, v, 5) - closed), at);
            const double ratio = std::exp(-0.5 * alpha * alpha) / std::sqrt(2.0 * kPi) / normal_cdf(alpha);
            const double true_mean = shrunk + std::sqrt(shrunk_q) * ratio;
            const double true_var = shrunk_q * (1.0 - alpha * ratio - ratio * ratio);
            const Moments posterior = level_posterior(0.0, m, mu, p, v, 5);
            mean.add(std::fabs(posterior.mean / unit - true_mean) / std::sqrt(true_var), at);
            var.add(std::fabs(posterior.var / (unit * unit) / true_var - 1.0), at);
        }
    }
    EXPECT_LE(llr.error, 1e-9) << "nats, at " << llr.where;
    EXPECT_LE(mean.error, 0.002) << "posterior standard deviations, at " << mean.where;
    EXPECT_LE(var.error, 0.01) << "relative, at " << var.where;
}

TEST(Observation, ReproducesTheNotesExample) {
    // The notes' units (topic 3, item 1): independent pieces of noise of unit variance, V = n for a sum of n pieces,
    // in which a piece's carrier amplitude is sqrt(S500) (a piece of 2 ms is 1 / (500 Hz)). A 20 WPM dot lasts 60 ms,
    // 30 pieces; at 10 dB S500 its noise-free sum is 30 sqrt(10); the stored level, 10 dB above it, is 20 dB S500, an
    // amplitude of 10. Taken as exact, the level makes the dot 1 107 nats better as noise; uncertain by +/-10%
    // (sqrt(p) = 1), the dot is 271 nats better as a mark, and it moves the level 98% of the way to its own.
    const int n = 30;
    const double own = std::sqrt(10.0);
    const double stored = 10.0;
    const double s_abs = n * own;
    EXPECT_NEAR(block_llr(s_abs, n, stored, n), -1107.0, 11.07);
    EXPECT_NEAR(block_llr_uncertain(s_abs, n, stored, 1.0, n, 5), 271.0, 2.71);
    const Moments posterior = level_posterior(s_abs, n, stored, 1.0, n, 5);
    EXPECT_NEAR((stored - posterior.mean) / (stored - own), 0.98, 0.01);
}

TEST(Observation, UpdateIsTheKalmanGainAtHighSnr) {
    // For b >= 30 (prior centers of 30, 100 and 300, blocks from b/2 to 2b) at a relative spread of 0.1, the
    // posterior is the Kalman update: the likelihood is N(|S| / m, V / (2 m^2)) in a, so the mean is
    // mu + P / (P + V / (2 m^2)) (|S| / m - mu) and the variance P (V / (2 m^2)) / (P + V / (2 m^2)), within 1%.
    const int m = 24;
    const double v = block_variance(m);
    const double root_v = std::sqrt(v);
    const double r = v / (2.0 * m * m);  // FS^2: the likelihood's variance in a
    for (const double b : {30.0, 100.0, 300.0}) {
        for (const double z : {b / 2.0, b, 2.0 * b}) {
            SCOPED_TRACE("b = " + number(b) + ", z = " + number(z));
            const double mu = b * root_v / m;
            const double p = (0.1 * mu) * (0.1 * mu);
            const double s_abs = z * root_v;
            const double gain = p / (p + r);
            const double kalman_mean = mu + gain * (s_abs / m - mu);
            const Moments posterior = level_posterior(s_abs, m, mu, p, v, 5);
            EXPECT_NEAR(posterior.mean, kalman_mean, 0.01 * kalman_mean);
            EXPECT_NEAR(posterior.var, gain * r, 0.01 * gain * r);
        }
    }
}

TEST(Observation, CombinedCallMatchesTheSeparateOnes) {
    // block_evidence is block_llr_uncertain and level_posterior from one quadrature: on the tests' grid its evidence
    // and moments are theirs to the last bit, and its mode_a is the maximum of prior x likelihood, met within 0.01 of
    // the posterior's standard deviation by the dense integral's highest point. With p = 0: {block_llr, {mu, 0}, mu}.
    const int m = 24;
    const double v = block_variance(m);
    const double root_v = std::sqrt(v);
    Worst mode;
    for (const double b : {1.0, 3.0, 10.0, 30.0}) {
        for (const double z : {0.0, b / 2.0, b, 2.0 * b}) {
            for (const double spread : {0.03, 0.1, 0.3, 0.5}) {
                const auto at = [b, z, spread] {
                    return "b = " + number(b) + ", z = " + number(z) + ", spread " + number(spread);
                };
                SCOPED_TRACE(at());
                const double mu = b * root_v / m;
                const double p = (spread * mu) * (spread * mu);
                const double s_abs = z * root_v;
                const BlockEvidence evidence = block_evidence(s_abs, m, mu, p, v, 5);
                EXPECT_EQ(evidence.llr, block_llr_uncertain(s_abs, m, mu, p, v, 5));
                const Moments level = level_posterior(s_abs, m, mu, p, v, 5);
                EXPECT_EQ(evidence.level.mean, level.mean);
                EXPECT_EQ(evidence.level.var, level.var);
                const Dense dense = dense_uncertain(s_abs, m, mu, p, v);
                mode.add(std::fabs(evidence.mode_a - dense.mode) / std::sqrt(dense.var), at);
            }
        }
    }
    EXPECT_LE(mode.error, 0.01) << "posterior standard deviations, at " << mode.where;
    for (const double z : {0.0, 3.0, 30.0}) {
        const BlockEvidence exact = block_evidence(z * root_v, m, 1e-3, 0.0, v, 5);
        EXPECT_EQ(exact.llr, block_llr(z * root_v, m, 1e-3, v)) << "z = " << z;
        EXPECT_EQ(exact.level.mean, 1e-3);
        EXPECT_EQ(exact.level.var, 0.0);
        EXPECT_EQ(exact.mode_a, 1e-3);
    }
}

TEST(Observation, TruncatedNodeTableMatchesTheDirectNodes) {
    // Where the Gaussian at the mode has mass below 0 (b^ < 8.5 sigma^), the quadrature reads its nodes from a table in
    // tau = b^ / sigma^ (steps of 1/256, linear interpolation) instead of computing each by Phi^-1. Against the direct
    // computation: on the tests' grid (b = m mu / sqrt(v) of 1, 3, 10 and 30, z = 0, b/2, b and 2b, relative spreads
    // sqrt(p) / mu of 0.03 to 0.5, blocks of 24 samples, order 5) the evidence within 1e-6 nats, the posterior's mean
    // within 1e-6 of its standard deviation, its variance within 1e-6 relatively, and the mode the same (Newton's
    // method runs before the nodes are laid); and the same over weak and broad priors, where most quadratures are
    // truncated (b of 0.1 to 10, relative spreads of 0.5 to 3, z of 0 to 3 b), at orders 2, 5 and 12. Some values
    // differ: the table is read.
    const int m = 24;
    const double v = block_variance(m);
    const double root_v = std::sqrt(v);
    Worst llr;
    Worst mean;
    Worst var;
    int differing = 0;
    const auto compare = [&](double b, double z, double spread, int order) {
        const auto at = [b, z, spread, order] {
            return "order " + std::to_string(order) + ", b = " + number(b) + ", z = " + number(z) + ", spread " +
                   number(spread);
        };
        const double mu = b * root_v / m;
        const double p = (spread * mu) * (spread * mu);
        const double s_abs = z * root_v;
        const BlockEvidence table = block_evidence(s_abs, m, mu, p, v, order);
        const BlockEvidence direct = detail::block_evidence_direct_nodes(s_abs, m, mu, p, v, order);
        llr.add(std::fabs(table.llr - direct.llr), at);
        mean.add(std::fabs(table.level.mean - direct.level.mean) / std::sqrt(direct.level.var), at);
        var.add(std::fabs(table.level.var / direct.level.var - 1.0), at);
        EXPECT_EQ(table.mode_a, direct.mode_a) << at();
        if (table.llr != direct.llr) ++differing;
    };
    for (const double b : {1.0, 3.0, 10.0, 30.0})
        for (const double z : {0.0, b / 2.0, b, 2.0 * b})
            for (const double spread : {0.03, 0.1, 0.3, 0.5}) compare(b, z, spread, 5);
    EXPECT_LE(llr.error, 1e-6) << "nats, on the tests' grid, at " << llr.where;
    EXPECT_LE(mean.error, 1e-6) << "posterior standard deviations, on the tests' grid, at " << mean.where;
    EXPECT_LE(var.error, 1e-6) << "relative, on the tests' grid, at " << var.where;
    for (const int order : {2, 5, 12})
        for (const double b : {0.1, 0.3, 1.0, 3.0, 10.0})
            for (const double spread : {0.5, 1.0, 2.0, 3.0})
                for (const double z_over_b : {0.0, 0.5, 1.0, 2.0, 3.0}) compare(b, z_over_b * b, spread, order);
    EXPECT_LE(llr.error, 1e-6) << "nats, at " << llr.where;
    EXPECT_LE(mean.error, 1e-6) << "posterior standard deviations, at " << mean.where;
    EXPECT_LE(var.error, 1e-6) << "relative, at " << var.where;
    EXPECT_GT(differing, 0);
}

TEST(Observation, StartingPointChangesNothing) {
    // Started at 0, at 0.9 and 1.1 times the mode found from the default start, and at 10 times it (beyond the bracket,
    // so clamped to its top), and from outside the bracket on either side, Newton's method ends at the same mode within
    // its tolerance: on the tests' grid the evidence agrees within 1e-9 nats, the moments within 1e-9 of their values
    // and the mode within 1e-9 of the posterior's standard deviation.
    const int m = 24;
    const double v = block_variance(m);
    const double root_v = std::sqrt(v);
    Worst llr;
    Worst mean;
    Worst var;
    Worst mode;
    for (const double b : {1.0, 3.0, 10.0, 30.0}) {
        for (const double z : {0.0, b / 2.0, b, 2.0 * b}) {
            for (const double spread : {0.03, 0.1, 0.3, 0.5}) {
                const double mu = b * root_v / m;
                const double p = (spread * mu) * (spread * mu);
                const double s_abs = z * root_v;
                const BlockEvidence cold = block_evidence(s_abs, m, mu, p, v, 5);
                const double sd = std::sqrt(cold.level.var);
                const double mode_a = cold.mode_a;
                for (const double start : {0.0, 0.9 * mode_a, 1.1 * mode_a, 10.0 * mode_a, -1.0, 1e300}) {
                    const auto at = [b, z, spread, start] {
                        return "b = " + number(b) + ", z = " + number(z) + ", spread " + number(spread) +
                               ", start " + number(start) + " FS";
                    };
                    const BlockEvidence warm = block_evidence(s_abs, m, mu, p, v, 5, start);
                    llr.add(std::fabs(warm.llr - cold.llr), at);
                    mean.add(std::fabs(warm.level.mean / cold.level.mean - 1.0), at);
                    var.add(std::fabs(warm.level.var / cold.level.var - 1.0), at);
                    mode.add(std::fabs(warm.mode_a - cold.mode_a) / sd, at);
                }
            }
        }
    }
    // A prior centered at 0 (mu = 0), where g'(0) = beta / q = 0: above z* = sqrt(1 + 1/(2 q)) the curvature there is
    // negative, b = 0 is a minimum and the mode lies above it. Started at 0, or below 0 and so clamped to it, Newton's
    // method must still find that mode.
    const double unit = root_v / m;  // FS per unit of b
    for (const double q : {0.25, 1.0, 9.0, 100.0}) {
        const double z_star = std::sqrt(1.0 + 1.0 / (2.0 * q));
        for (const double z : {1.2 * z_star, 2.0 * z_star, 10.0}) {
            const double p = q * unit * unit;
            const double s_abs = z * root_v;
            const BlockEvidence cold = block_evidence(s_abs, m, 0.0, p, v, 5);
            const double sd = std::sqrt(cold.level.var);
            for (const double start : {0.0, -1.0}) {
                const auto at = [q, z, start] {
                    return "mu = 0, q = " + number(q) + ", z = " + number(z) + ", start " + number(start) + " FS";
                };
                const BlockEvidence warm = block_evidence(s_abs, m, 0.0, p, v, 5, start);
                llr.add(std::fabs(warm.llr - cold.llr), at);
                mean.add(std::fabs(warm.level.mean / cold.level.mean - 1.0), at);
                var.add(std::fabs(warm.level.var / cold.level.var - 1.0), at);
                mode.add(std::fabs(warm.mode_a - cold.mode_a) / sd, at);
            }
        }
    }
    EXPECT_LE(llr.error, 1e-9) << "nats, at " << llr.where;
    EXPECT_LE(mean.error, 1e-9) << "relative, at " << mean.where;
    EXPECT_LE(var.error, 1e-9) << "relative, at " << var.where;
    EXPECT_LE(mode.error, 1e-9) << "posterior standard deviations, at " << mode.where;
}

TEST(Observation, WarmStartsFollowARunningSum) {
    // The search's use: along a mark's running sum, each piece's call starts from the previous piece's mode_a. A prior
    // centered at 0 (mu = 0, p = 1 FS^2), noise of unit variance per sample (V(m) = m FS^2, so q = m), a carrier of
    // 0.3 FS and 3 samples per piece, for 200 pieces. Without noise the sum is |S| = 0.3 m FS, so z = 0.3 sqrt(m) rises
    // through z* = sqrt(1 + 1/(2 m)): the mode lies at 0 for the first 3 pieces (to 9 samples) and above it from 12
    // samples on. With noise (the same carrier and noise) the sum wanders. In both, at every piece the warm call's
    // evidence and moments match a cold call's within 1e-9 nats and 1e-9 of their values.
    const std::complex<double> carrier = std::polar(0.3, 0.7);
    for (const bool noisy : {false, true}) {
        SCOPED_TRACE(noisy ? "with noise" : "without noise");
        WhiteNoise noise(20261021u, 1.0);
        std::complex<double> sum(0.0, 0.0);
        std::optional<double> start;
        Worst llr;
        Worst mean;
        Worst var;
        int at_zero = 0;    // pieces whose mode lies at a = 0
        int left_zero = 0;  // pieces whose mode lies above 0 after one at 0
        for (int n = 1; n <= 200; ++n) {
            for (int k = 0; k < 3; ++k) {
                sum += carrier;
                if (noisy) {
                    const kz4ap::Sample w = noise.next();
                    sum += std::complex<double>(w.real(), w.imag());
                }
            }
            const int m = 3 * n;
            const double v = m;  // FS^2
            const BlockEvidence warm = block_evidence(std::abs(sum), m, 0.0, 1.0, v, 5, start);
            const BlockEvidence cold = block_evidence(std::abs(sum), m, 0.0, 1.0, v, 5);
            const auto at = [n] { return std::to_string(n) + " pieces"; };
            llr.add(std::fabs(warm.llr - cold.llr), at);
            mean.add(std::fabs(warm.level.mean / cold.level.mean - 1.0), at);
            var.add(std::fabs(warm.level.var / cold.level.var - 1.0), at);
            if (cold.mode_a <= 1e-6 * std::sqrt(cold.level.var))
                ++at_zero;
            else if (at_zero > 0)
                ++left_zero;
            start = warm.mode_a;
        }
        EXPECT_LE(llr.error, 1e-9) << "nats, at " << llr.where;
        EXPECT_LE(mean.error, 1e-9) << "relative, at " << mean.where;
        EXPECT_LE(var.error, 1e-9) << "relative, at " << var.where;
        if (!noisy) {
            EXPECT_EQ(at_zero, 3) << "pieces with the mode at 0";
            EXPECT_EQ(left_zero, 197) << "pieces with the mode above 0 after it";
        }
    }
}

TEST(Observation, DegenerateCurvatureStaysFinite) {
    // A strong station's story, b = 1e5, its level uncertain by a relative spread of 30, against a block of noise,
    // z = 1 (m = 1 and v = 1 FS^2, so that b is in FS): the curvature at the mode all but vanishes, sigma^ is about 263
    // against an integrand about 1 wide, and curvature-scaled nodes would land where the posterior has underflowed. The
    // broad-prior regime's tables serve it: the evidence, the moments and the mode are finite at orders 2, 3 and 5,
    // cold and warm, and the evidence is within 0.01 nats of a dense integral at every order (it no longer depends on
    // the order there).
    const double beta = 1e5;
    const double q = (30.0 * beta) * (30.0 * beta);
    const double z = 1.0;
    const Dense dense = dense_uncertain(z, 1, beta, q, 1.0, 0.0, 30.0);
    for (const int order : {2, 3, 5}) {
        SCOPED_TRACE("order " + std::to_string(order));
        for (const std::optional<double> start : {std::optional<double>(), std::optional<double>(0.0)}) {
            const BlockEvidence evidence = block_evidence(z, 1, beta, q, 1.0, order, start);
            EXPECT_TRUE(std::isfinite(evidence.llr)) << evidence.llr;
            EXPECT_TRUE(std::isfinite(evidence.level.mean)) << evidence.level.mean;
            EXPECT_TRUE(std::isfinite(evidence.level.var)) << evidence.level.var;
            EXPECT_TRUE(std::isfinite(evidence.mode_a)) << evidence.mode_a;
            EXPECT_NEAR(evidence.llr, dense.llr, 0.01);
        }
    }
}

TEST(Observation, BroadPriorsMatchDenseIntegrals) {
    // Where the prior is broad against the likelihood, the curvature at the mode degenerates and the evidence and the
    // level come from the exact rescaling's tables (observation.hpp). Against dense integrals at orders 3 and 5, for
    // blocks of z = 0 to 4 (m = 1 and v = 1 FS^2, so that b is in FS):
    //   - strong stations, b of 100 to 1e6 at relative spreads of 0.3 to 2;
    //   - weak broad priors, b of 0.3 to 1 with sqrt(q) = 10;
    //   - b = 1e5 at a relative spread of 30.
    // Lambda_P within 0.01 nats, and the posterior's mean and variance within 1%: the variance after a block of noise
    // (z = 1) is near 0.36, where curvature-scaled nodes made it 0 to 3.2 times that.
    Worst llr;
    Worst mean;
    Worst var;
    const auto check = [&](double beta, double q, double z) {
        const Dense dense = dense_uncertain(z, 1, beta, q, 1.0, 0.0, 30.0);
        for (const int order : {3, 5}) {
            const auto at = [beta, q, z, order] {
                return "b = " + number(beta) + ", q = " + number(q) + ", z = " + number(z) + ", order " +
                       std::to_string(order);
            };
            const BlockEvidence evidence = block_evidence(z, 1, beta, q, 1.0, order);
            llr.add(std::fabs(evidence.llr - dense.llr), at);
            mean.add(std::fabs(evidence.level.mean / dense.mean - 1.0), at);
            var.add(std::fabs(evidence.level.var / dense.var - 1.0), at);
        }
    };
    const double blocks[] = {0.0, 0.5, 1.0, 1.5, 2.0, 3.0, 4.0};
    for (const double beta : {100.0, 1e3, 1e4, 1e5, 1e6})
        for (const double spread : {0.3, 0.5, 1.0, 2.0})
            for (const double z : blocks) check(beta, (spread * beta) * (spread * beta), z);
    for (const double beta : {0.3, 0.5, 1.0})
        for (const double z : blocks) check(beta, 100.0, z);
    check(1e5, (30.0 * 1e5) * (30.0 * 1e5), 1.0);
    EXPECT_LE(llr.error, 0.01) << "nats, at " << llr.where;
    EXPECT_LE(mean.error, 0.01) << "relative, at " << mean.where;
    EXPECT_LE(var.error, 0.01) << "relative, at " << var.where;
}

TEST(Observation, RegimeEdgesHaveNoJump) {
    // The broad-prior regime is kappa' <= 1 and z' <= 4, with kappa' = (b / q) / s, z' = z / s and
    // s = sqrt(1 + 1/(2 q)) (b and q the prior's mean and variance in units of b). Across its edges, where the evidence
    // passes between the tables and the adaptive quadrature, Lambda_P moves by under 0.01 nats between points 1e-15
    // inside and 1e-15 outside the edge, relatively; by itself it moves there by under kappa b x 2e-15 (0.002 nats at
    // q = 1e12).
    Worst jump;
    for (const double q : {1e-3, 1e-2, 0.1, 1.0, 10.0, 100.0, 1e4, 1e6, 1e8, 1e10, 1e12}) {
        const double s = std::sqrt(1.0 + 0.5 / q);
        for (const double scaled : {0.0, 0.25, 0.5, 0.75, 1.0, 1.25, 1.5, 2.0, 2.5, 3.0, 3.5, 3.999}) {
            const double z = scaled * s;
            const double inside = q * s * (1.0 - 1e-15);
            const double outside = q * s * (1.0 + 1e-15);
            const double step =
                block_llr_uncertain(z, 1, inside, q, 1.0, 5) - block_llr_uncertain(z, 1, outside, q, 1.0, 5);
            jump.add(std::fabs(step), [q, z] { return "kappa' = 1, q = " + number(q) + ", z = " + number(z); });
        }
        for (const double scaled : {0.0, 0.1, 0.25, 0.5, 0.75, 0.999}) {
            const double beta = scaled * s * q;
            const double inside = 4.0 * s * (1.0 - 1e-15);
            const double outside = 4.0 * s * (1.0 + 1e-15);
            const double step =
                block_llr_uncertain(inside, 1, beta, q, 1.0, 5) - block_llr_uncertain(outside, 1, beta, q, 1.0, 5);
            jump.add(std::fabs(step), [q, beta] { return "z' = 4, q = " + number(q) + ", b = " + number(beta); });
        }
    }
    EXPECT_LE(jump.error, 0.01) << "nats, at " << jump.where;
}

TEST(Observation, TinySpreadsTendToTheExactLevel) {
    // A level uncertain by a relative spread of 1e-6 down to 1e-17, at b = 10, against z = 10 and z = 12. Lambda_P
    // tends to block_llr: within 1e-6 nats wherever the spread is 1e-9 or below, where the level counts as exact, and
    // above it too, where the true difference, about q (g_L')^2 / 2 with g_L' the likelihood's slope (at most 4), is
    // below 1e-9 nats. The posterior keeps the prior's mean and variance within 1e-6 of them.
    const int m = 24;
    const double v = block_variance(m);
    const double root_v = std::sqrt(v);
    const double mu = 10.0 * root_v / m;
    Worst llr;
    Worst mean;
    Worst var;
    for (const double z : {10.0, 12.0}) {
        const double exact = block_llr(z * root_v, m, mu, v);
        for (int e = 6; e <= 17; ++e) {
            const double spread = std::pow(10.0, -e);
            const double p = (spread * mu) * (spread * mu);
            const auto at = [z, e] { return "z = " + number(z) + ", relative spread 1e-" + std::to_string(e); };
            llr.add(std::fabs(block_llr_uncertain(z * root_v, m, mu, p, v, 5) - exact), at);
            const Moments posterior = level_posterior(z * root_v, m, mu, p, v, 5);
            mean.add(std::fabs(posterior.mean / mu - 1.0), at);
            var.add(std::fabs(posterior.var / p - 1.0), at);
        }
    }
    EXPECT_LE(llr.error, 1e-6) << "nats, at " << llr.where;
    EXPECT_LE(mean.error, 1e-6) << "relative, at " << mean.where;
    EXPECT_LE(var.error, 1e-6) << "relative, at " << var.where;
}

TEST(Observation, EvidenceStaysFiniteAtTheExtremes) {
    const int m = 24;
    const double v = block_variance(m);
    const double root_v = std::sqrt(v);
    // No sum (|S| = 0): Lambda = -b^2, I0(0) being 1. No carrier (a = 0): 0.
    EXPECT_DOUBLE_EQ(block_llr(0.0, 10, 0.5, 2.0), -12.5);
    EXPECT_EQ(block_llr(3.0, 10, 0.0, 2.0), 0.0);
    // A level known exactly (p = 0) is block_llr to the last bit, and its posterior the level itself.
    for (const double z : {0.0, 0.3, 3.0, 30.0, 1e5}) {
        const double s_abs = z * root_v;
        EXPECT_EQ(block_llr_uncertain(s_abs, m, 1e-3, 0.0, v, 5), block_llr(s_abs, m, 1e-3, v)) << "z = " << z;
        const Moments exact = level_posterior(s_abs, m, 1e-3, 0.0, v, 5);
        EXPECT_EQ(exact.mean, 1e-3);
        EXPECT_EQ(exact.var, 0.0);
    }
    // A block 100 dB above its noise, z = 1e5, with the prior's center there and 10% around it: Lambda_P is near
    // z^2 = 1e10 and is met within 0.01 nats by a dense integral over the 60 units of b around the mode, as is the
    // posterior.
    {
        const double z = 1e5;
        const double mu = z * root_v / m;
        const double p = (0.1 * mu) * (0.1 * mu);
        const double unit = root_v / m;
        const Dense dense = dense_uncertain(z * root_v, m, mu, p, v, (z - 30.0) * unit, (z + 30.0) * unit);
        const double llr = block_llr_uncertain(z * root_v, m, mu, p, v, 5);
        EXPECT_TRUE(std::isfinite(llr));
        EXPECT_NEAR(llr, dense.llr, 0.01);
        const Moments posterior = level_posterior(z * root_v, m, mu, p, v, 5);
        EXPECT_NEAR(posterior.mean, dense.mean, 1e-3 * std::sqrt(dense.var));
        EXPECT_NEAR(posterior.var, dense.var, 1e-3 * dense.var);
        // The same block against a level known exactly, and against one far too weak (b = 1).
        EXPECT_TRUE(std::isfinite(block_llr(z * root_v, m, mu, v)));
        EXPECT_TRUE(std::isfinite(block_llr_uncertain(z * root_v, m, unit, 0.01 * unit * unit, v, 5)));
    }
    // A level near 0 (b = 1e-6, and b = 0 with the prior a half-normal) and an uncertain level against a block of
    // nothing and of a little: finite, and within 0.01 nats of the dense integral at these points, taken over b from 0
    // to where the posterior ends (40 prior standard deviations, moved by at most 2 sd^2 z toward z, or 15 beyond z for
    // a broad prior). Every one of these points lies in the broad-prior regime (kappa' from 0 to 1.4e-3, z' up to 1.95),
    // so the regime's tables serve them, within 3.4e-5 nats (observation.hpp); the curvature-scaled quadrature they
    // replaced missed near z* = sqrt(1 + 1/(2 sd^2)), where the mode leaves 0 and its curvature vanishes.
    for (const double b : {0.0, 1e-6}) {
        for (const double z : {0.0, 0.5, 2.0}) {
            for (const double sd : {1e-3, 0.3, 3.0}) {
                SCOPED_TRACE("b = " + number(b) + ", z = " + number(z) + ", sd " + number(sd));
                const double unit = root_v / m;
                const double mu = b * unit;
                const double p = sd * sd * unit * unit;
                const double s_abs = z * root_v;
                const double top_b = std::min(z + 15.0 * (1.0 + sd), b + 2.0 * sd * sd * z + 40.0 * sd);
                const Dense dense = dense_uncertain(s_abs, m, mu, p, v, 0.0, top_b * unit);
                const double llr = block_llr_uncertain(s_abs, m, mu, p, v, 5);
                EXPECT_TRUE(std::isfinite(llr));
                EXPECT_NEAR(llr, dense.llr, 0.01);
                const Moments posterior = level_posterior(s_abs, m, mu, p, v, 5);
                EXPECT_GE(posterior.mean, 0.0);
                EXPECT_GT(posterior.var, 0.0);
            }
        }
    }
}

TEST(Observation, FlatPriorTableMatchesDirectIntegration) {
    // Blocks of 1 to 195 pieces, the block length at the default 1 Hz spread (0.39037 s / 2 ms), on the channelizer's
    // filter at 48 kHz, with no rise (every weight 1, m_signal = 3 n). At 20 points (n, z), on and between the table's
    // nodes, Lambda_flat within 0.01 nats of a direct integration over the level x; the posterior's mean within 0.01
    // standard deviations and its variance within 1%. The rate is c's last increment, the long-run rate: 1 500 Hz.
    const ViterbiConfig config{};
    const int max_pieces = static_cast<int>(block_length_s(config.fading_spread_hz, config.block_loss_db) /
                                            config.piece_s);
    ASSERT_EQ(max_pieces, 195);
    const std::vector<double> c = sum_noise_at(48000, 3 * max_pieces);
    const double rate = c[c.size() - 1] - c[c.size() - 2];
    ASSERT_NEAR(rate, 1500.0, 1e-6);
    const FlatPrior prior(config, c, max_pieces, 3);
    EXPECT_EQ(prior.noise_rate_hz(), rate);
    const std::pair<int, double> points[] = {
        {1, 0.0},    {1, 0.37},  {1, 2.5},   {1, 39.99},  {2, 1.0},    {3, 7.255},  {7, 0.05},
        {7, 4.321},  {20, 1.234}, {20, 12.5}, {33, 0.8},  {64, 3.3},   {100, 0.0},  {100, 19.876},
        {150, 0.6},  {150, 30.01}, {195, 0.0}, {195, 2.0}, {195, 6.666}, {195, 40.0},
    };
    Worst llr;
    Worst mean;
    Worst var;
    for (const auto& [pieces, z] : points) {
        const auto at = [pieces, z] { return std::to_string(pieces) + " pieces, z = " + number(z); };
        const Dense direct = direct_flat(config, 3.0 * pieces, rate, z);
        llr.add(std::fabs(prior.llr(pieces, z) - direct.llr), at);
        const Moments posterior = prior.posterior(pieces, z);
        mean.add(std::fabs(posterior.mean - direct.mean) / std::sqrt(direct.var), at);
        var.add(std::fabs(posterior.var / direct.var - 1.0), at);
    }
    EXPECT_LE(llr.error, 0.01) << "nats, at " << llr.where;
    EXPECT_LE(mean.error, 0.01) << "posterior standard deviations, at " << mean.where;
    EXPECT_LE(var.error, 0.01) << "relative, at " << var.where;

    // The large-z form, z^2 - ln(2 z^2) + ln(20 / (ln 10 x 100)), 100 being the range's width in dB S500, meets the
    // table: its error, which falls as 1 / z^2, is 0.0102 nats at z = 10, 0.0025 at 20 and 0.0011 at 30 (measured), so
    // within 0.01 from z = 20 on, and 6.3e-4 at the table's end, z = 40, where the form takes over. Its posterior of b,
    // proportional to e^-(b - z)^2 b^(-3/2), has by Laplace's method one order further the mean z - 3 / (4 z) and the
    // variance 1/2 + 3 / (8 z^2): just above z = 40 they meet the table's at 40 within 1e-3.
    const double constant = std::log(20.0 / (std::numbers::ln10 * 100.0));
    const auto large_z = [constant](double z) { return z * z - std::log(2.0 * z * z) + constant; };
    for (const int pieces : {1, 60, 195}) {
        SCOPED_TRACE(std::to_string(pieces) + " pieces");
        for (const double z : {20.0, 25.0, 30.0, 35.0, 39.99, 40.0})
            EXPECT_NEAR(prior.llr(pieces, z), large_z(z), 0.01) << "z = " << z;
        EXPECT_NEAR(prior.llr(pieces, 40.0), large_z(40.0), 0.001);
        const Moments at_end = prior.posterior(pieces, 40.0);
        const Moments just_above = prior.posterior(pieces, std::nextafter(40.0, 41.0));
        EXPECT_NEAR(just_above.mean, at_end.mean, 1e-3);
        EXPECT_NEAR(just_above.var, at_end.var, 1e-3);
        for (const double z : {std::nextafter(40.0, 41.0), 40.5, 77.7, 1234.5}) {
            EXPECT_DOUBLE_EQ(prior.llr(pieces, z), large_z(z)) << "z = " << z;
            const Moments above = prior.posterior(pieces, z);
            EXPECT_DOUBLE_EQ(above.mean, z - 0.75 / z) << "z = " << z;
            EXPECT_DOUBLE_EQ(above.var, 0.5 + 0.375 / (z * z)) << "z = " << z;
        }
    }
}

TEST(Observation, FlatPriorWithTheRise) {
    // The flat prior of a mark's first block with the rise (rise_fractions at 48 kHz, 5 ms keying edges), the matched
    // filter: weight(j) is the rise's fraction g_j and 1 past it, and signal_samples(n) is 3 (g_1^2 + ... + g_n^2),
    // summed here. At points (n, z) on and between the table's nodes, from blocks inside the rise to the longest,
    // Lambda_flat within 0.01 nats of the direct integration with b(x)^2 = 10^(x / 10) x 500 Hz x m_signal(n) / rate
    // (m_signal(n) from the sums here), the posterior's mean within 0.01 standard deviations and its variance within
    // 1%.
    const ViterbiConfig config{};
    const kz4ap::ChannelizerConfig channelizer = channelizer_at(48000);
    const std::vector<double> rise = rise_fractions(kz4ap::channel_filter_taps(channelizer), 48000.0,
                                                    channelizer.fft_size / channelizer.channel_bins, 3,
                                                    config.keying_rise_s);
    ASSERT_EQ(rise.size(), 4u);
    const std::vector<double> c = sum_noise_at(48000, 3 * 195);
    const double rate = c[c.size() - 1] - c[c.size() - 2];
    const auto started = std::chrono::steady_clock::now();
    const FlatPrior prior(config, c, 195, 3, rise);
    const double build_s = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    ::testing::Test::RecordProperty("build_s_195_pieces", std::to_string(build_s));
    ASSERT_EQ(prior.rise().size(), rise.size());
    std::vector<double> m_signal(196, 0.0);  // [n]: 3 x the sum of the first n squared weights
    double squares = 0.0;
    for (int j = 1; j <= 195; ++j) {
        const double g = j <= 4 ? rise[static_cast<std::size_t>(j) - 1] : 1.0;
        EXPECT_EQ(prior.weight(j), g) << "piece " << j;
        squares += g * g;
        m_signal[static_cast<std::size_t>(j)] = 3.0 * squares;
        EXPECT_NEAR(prior.signal_samples(j), 3.0 * squares, 1e-12 * squares) << j << " pieces";
    }
    EXPECT_EQ(prior.weight(400), 1.0);
    const std::pair<int, double> points[] = {{1, 0.0},   {1, 0.37},  {1, 2.5},  {1, 39.99}, {2, 1.0},
                                             {2, 6.0},   {3, 0.05},  {3, 7.255}, {4, 4.321}, {5, 1.234},
                                             {7, 12.5},  {20, 0.8},  {64, 3.3}, {195, 0.0}, {195, 19.876}};
    Worst llr;
    Worst mean;
    Worst var;
    for (const auto& [pieces, z] : points) {
        const auto at = [pieces, z] { return std::to_string(pieces) + " pieces, z = " + number(z); };
        const Dense direct = direct_flat(config, m_signal[static_cast<std::size_t>(pieces)], rate, z);
        llr.add(std::fabs(prior.llr(pieces, z) - direct.llr), at);
        const Moments posterior = prior.posterior(pieces, z);
        mean.add(std::fabs(posterior.mean - direct.mean) / std::sqrt(direct.var), at);
        var.add(std::fabs(posterior.var / direct.var - 1.0), at);
    }
    // The errors measured, left in the test's record (observation.hpp cites them).
    ::testing::Test::RecordProperty("worst_llr_error_nats", std::to_string(llr.error));
    ::testing::Test::RecordProperty("worst_mean_error_sd", std::to_string(mean.error));
    ::testing::Test::RecordProperty("worst_var_error_relative", std::to_string(var.error));
    EXPECT_LE(llr.error, 0.01) << "nats, at " << llr.where;
    EXPECT_LE(mean.error, 0.01) << "posterior standard deviations, at " << mean.where;
    EXPECT_LE(var.error, 0.01) << "relative, at " << var.where;
}

TEST(Observation, RiseFractionsMatchTheChannelizer) {
    // The rise's fractions before the average (detail::rise_fractions_by_position) against the real Channelizer: a
    // noise-free carrier at the channel's center, keyed on with the generator's 5 ms raised cosine at input sample
    // 200 x 96 + q at 48 kHz (200 x 384 + q at 192 kHz), the edge at a piece's start, middle and end, its channel
    // samples summed into pieces of 3 as the decoder does. Each piece's sum over a full piece's, signed by its phase
    // against the steady carrier's, matches the fraction within 5e-7 absolute: the channelizer computes in float and
    // keeps the channel's 64 bins (1.6e-7 at most, measured), so no relative tolerance near 1e-9 is reachable.
    // rise_fractions is their average over every position, from the first piece whose average reaches 0.01 to the last
    // that differs from 1 by more than 0.005 (4 pieces at both rates). With no keying edge it is the filter's own rise,
    // which the taps' cumulative sum gives in closed form.
    for (const int rate : {48000, 192000}) {
        SCOPED_TRACE(std::to_string(rate) + " Hz");
        const kz4ap::ChannelizerConfig config = channelizer_at(rate);
        const std::vector<double> taps = kz4ap::channel_filter_taps(config);
        const int decimation = config.fft_size / config.channel_bins;
        constexpr int kPerPiece = 3;
        constexpr int kPieces = 30;
        const int positions = decimation * kPerPiece;
        const std::vector<std::vector<double>> by_position =
            detail::rise_fractions_by_position(taps, rate, decimation, kPerPiece, 0.005, kPieces);
        ASSERT_EQ(by_position.size(), static_cast<std::size_t>(positions));
        const double amplitude = 0.01;                           // FS
        const double center_hz = 32.0 * rate / config.fft_size;  // bin 32, the channel's center: 750 Hz
        const double rise_samples = 0.005 * rate;
        constexpr int kStart = 200;  // the piece the edge falls in, far from the stream's start
        Worst worst;
        for (const int q : {0, positions / 2, positions - 1}) {
            const long long edge = static_cast<long long>(kStart) * positions + q;
            std::vector<kz4ap::Sample> input(static_cast<std::size_t>(edge + (kPieces + 10) * positions));
            for (std::size_t i = 0; i < input.size(); ++i) {
                const double t = static_cast<double>(static_cast<long long>(i) - edge);
                const double envelope =
                    t < 0.0 ? 0.0 : t < rise_samples ? 0.5 - 0.5 * std::cos(kPi * t / rise_samples) : 1.0;
                const double phase = 2.0 * kPi * center_hz * static_cast<double>(i) / rate;
                input[i] = kz4ap::Sample(static_cast<float>(amplitude * envelope * std::cos(phase)),
                                         static_cast<float>(amplitude * envelope * std::sin(phase)));
            }
            kz4ap::Channelizer channelizer(config);
            channelizer.add_channel(1, 32);
            std::vector<std::complex<double>> channel;
            channelizer.push(input, [&](std::uint32_t, std::uint64_t, std::span<const kz4ap::Sample> samples) {
                for (const kz4ap::Sample& s : samples) channel.emplace_back(s.real(), s.imag());
            });
            ASSERT_GE(channel.size(), static_cast<std::size_t>((kStart + kPieces) * kPerPiece));
            const std::complex<double> steady = channel[static_cast<std::size_t>((kStart + 25) * kPerPiece)];
            for (int p = 0; p < kPieces; ++p) {
                std::complex<double> sum = 0.0;
                for (int i = 0; i < kPerPiece; ++i)
                    sum += channel[static_cast<std::size_t>((kStart + p) * kPerPiece + i)];
                const double sign = std::real(sum * std::conj(steady)) >= 0.0 ? 1.0 : -1.0;
                const double measured = sign * std::abs(sum) / (kPerPiece * amplitude);
                worst.add(std::fabs(measured - by_position[static_cast<std::size_t>(q)][static_cast<std::size_t>(p)]),
                          [q, p] { return "the edge at " + std::to_string(q) + ", piece " + std::to_string(p); });
            }
        }
        EXPECT_LE(worst.error, 5e-7) << "of a full piece, at " << worst.where;

        std::vector<double> average(kPieces, 0.0);
        for (const std::vector<double>& row : by_position)
            for (int p = 0; p < kPieces; ++p) average[static_cast<std::size_t>(p)] += row[static_cast<std::size_t>(p)];
        for (double& fraction : average) fraction /= static_cast<double>(by_position.size());
        int first = 0;
        while (!(average[static_cast<std::size_t>(first)] >= 0.01)) ++first;
        int last = kPieces - 1;
        while (!(std::fabs(average[static_cast<std::size_t>(last)] - 1.0) > 0.005)) --last;
        const std::vector<double> rise = rise_fractions(taps, rate, decimation, kPerPiece, 0.005);
        ASSERT_EQ(rise.size(), static_cast<std::size_t>(last - first + 1));
        EXPECT_EQ(rise.size(), 4u);
        for (std::size_t j = 0; j < rise.size(); ++j)
            EXPECT_NEAR(rise[j], average[static_cast<std::size_t>(first) + j], 1e-12) << "g_" << j + 1;

        // With no keying edge (rise_s = 0) the envelope is a step, and the filter's output t input samples after it is
        // the cumulative sum of the taps, h[0] + ... + h[t] (their whole sum once t passes the last tap): the filter's own
        // rise, here from std::partial_sum, piece by piece at every position, then averaged and trimmed as above.
        std::vector<double> cumulative(taps.size());
        std::partial_sum(taps.begin(), taps.end(), cumulative.begin());
        const double total = cumulative.back();
        const auto step_response = [&](long long t) {
            if (t < 0) return 0.0;
            return t < static_cast<long long>(cumulative.size()) ? cumulative[static_cast<std::size_t>(t)] : total;
        };
        const std::vector<std::vector<double>> own_by_position =
            detail::rise_fractions_by_position(taps, rate, decimation, kPerPiece, 0.0, kPieces);
        std::vector<double> own_average(kPieces, 0.0);
        Worst own_worst;
        for (int q = 0; q < positions; ++q) {
            for (int p = 0; p < kPieces; ++p) {
                double sum = 0.0;
                for (int i = 0; i < kPerPiece; ++i)
                    sum += step_response(static_cast<long long>(p * kPerPiece + i) * decimation - q);
                const double fraction = sum / (kPerPiece * total);
                own_average[static_cast<std::size_t>(p)] += fraction / positions;
                own_worst.add(std::fabs(own_by_position[static_cast<std::size_t>(q)][static_cast<std::size_t>(p)] -
                                        fraction),
                              [q, p] { return "the step at " + std::to_string(q) + ", piece " + std::to_string(p); });
            }
        }
        EXPECT_LE(own_worst.error, 1e-12) << "of a full piece, at " << own_worst.where;
        int own_first = 0;
        while (!(own_average[static_cast<std::size_t>(own_first)] >= 0.01)) ++own_first;
        int own_last = kPieces - 1;
        while (!(std::fabs(own_average[static_cast<std::size_t>(own_last)] - 1.0) > 0.005)) --own_last;
        const std::vector<double> own = rise_fractions(taps, rate, decimation, kPerPiece, 0.0);
        ASSERT_EQ(own.size(), static_cast<std::size_t>(own_last - own_first + 1));
        for (std::size_t j = 0; j < own.size(); ++j)
            EXPECT_NEAR(own[j], own_average[static_cast<std::size_t>(own_first) + j], 1e-12) << "own g_" << j + 1;
        EXPECT_GT(own.front(), rise.front());  // the filter alone rises faster than with a 5 ms edge
    }
    // The values the design records state, at 48 kHz with 5 ms edges.
    const kz4ap::ChannelizerConfig config = channelizer_at(48000);
    const std::vector<double> rise = rise_fractions(kz4ap::channel_filter_taps(config), 48000.0, 32, 3, 0.005);
    ASSERT_EQ(rise.size(), 4u);
    EXPECT_NEAR(rise[0], 0.193444, 5e-6);
    EXPECT_NEAR(rise[1], 0.613661, 5e-6);
    EXPECT_NEAR(rise[2], 0.942699, 5e-6);
    EXPECT_NEAR(rise[3], 1.019171, 5e-6);
}

TEST(Observation, TemplateErrorsMatchThePerPositionFractions) {
    // The systematic term's edge share, rise_template_errors (search.hpp, "The systematic term"), recomputed here from
    // the per-position fractions that RiseFractionsMatchTheChannelizer holds against the real channelizer, at 48 kHz
    // with the 5 ms edge: over the template's pieces from its first (the first whose average over the 96 positions
    // reaches 0.01), t_j the rise's average (rise_fractions) and 1 past it, the error of the weighted sum of a mark's
    // first n pieces for an edge at position p is e_n(p) = sum_{j <= n} t_j (f_j(p) - t_j), and E_n is the mean of
    // e_n(p)^2 over the positions (derived: one edge makes every piece's error, so they are summed before squaring).
    // Equal within 1e-12 for every n of the table, which ends where every position's pieces are full (its last two
    // entries equal: E_n has settled). The values the design records state: E_1 = 0.00037, E_2 = 0.0095,
    // E_3 = 0.0224 and 0.0210 from n = 7. The errors are positively correlated: as independent pieces (each piece's
    // mean square, summed over j) the error at n = 3 would be 0.0093, less than half of E_3.
    const kz4ap::ChannelizerConfig config = channelizer_at(48000);
    const std::vector<double> taps = kz4ap::channel_filter_taps(config);
    const int decimation = config.fft_size / config.channel_bins;
    constexpr int kPerPiece = 3;
    constexpr int kPieces = 40;
    const std::vector<std::vector<double>> by_position =
        detail::rise_fractions_by_position(taps, 48000.0, decimation, kPerPiece, 0.005, kPieces);
    const std::vector<double> rise = rise_fractions(taps, 48000.0, decimation, kPerPiece, 0.005);
    const std::vector<double> errors = rise_template_errors(taps, 48000.0, decimation, kPerPiece, 0.005);
    std::vector<double> average(kPieces, 0.0);
    for (const std::vector<double>& row : by_position)
        for (int p = 0; p < kPieces; ++p) average[static_cast<std::size_t>(p)] += row[static_cast<std::size_t>(p)];
    for (double& fraction : average) fraction /= static_cast<double>(by_position.size());
    std::size_t first = 0;
    while (!(average[first] >= 0.01)) ++first;
    ASSERT_GT(errors.size(), rise.size());
    ASSERT_LE(first + errors.size(), static_cast<std::size_t>(kPieces));
    std::vector<double> sums(by_position.size(), 0.0);
    double independent = 0.0;
    Worst worst;
    for (std::size_t n = 1; n <= errors.size(); ++n) {
        const std::size_t j = first + n - 1;
        const double t = n <= rise.size() ? rise[n - 1] : 1.0;
        double mean_square = 0.0;
        double piece_square = 0.0;
        for (std::size_t p = 0; p < by_position.size(); ++p) {
            const double error = t * (by_position[p][j] - t);
            sums[p] += error;
            mean_square += sums[p] * sums[p];
            piece_square += error * error;
        }
        mean_square /= static_cast<double>(by_position.size());
        independent += piece_square / static_cast<double>(by_position.size());
        worst.add(std::fabs(errors[n - 1] - mean_square), [n] { return "n = " + std::to_string(n); });
        if (n == 3) {
            EXPECT_NEAR(independent, 0.0093, 5e-5);
            EXPECT_LT(independent, 0.5 * mean_square);
        }
    }
    EXPECT_LE(worst.error, 1e-12) << "at " << worst.where;
    EXPECT_NEAR(errors.back(), errors[errors.size() - 2], 1e-12);
    for (const std::vector<double>& row : by_position) EXPECT_NEAR(row[first + errors.size() - 1], 1.0, 1e-9);
    EXPECT_NEAR(errors[0], 0.00037, 5e-6);
    EXPECT_NEAR(errors[1], 0.0095, 5e-5);
    EXPECT_NEAR(errors[2], 0.0224, 5e-5);
    ASSERT_GE(errors.size(), 7u);
    for (std::size_t n = 7; n <= errors.size(); ++n) EXPECT_NEAR(errors[n - 1], 0.0210, 5e-5) << "n = " << n;
}

TEST(Observation, FlatPriorHoldsTheTemplateErrors) {
    // FlatPrior::template_error(n), the systematic term's edge share of a mark's first block of n pieces: the table's
    // E_n for n = 1 ... N and E_N past it; 0 with no table. n below 1 is refused (std::out_of_range), and so is a table
    // with an entry that is negative or not finite (std::invalid_argument).
    const ViterbiConfig config{};
    const std::vector<double> c = sum_noise_at(48000, 600);
    const std::vector<double> rise = {0.2, 0.6, 0.95};
    const std::vector<double> errors = {0.001, 0.01, 0.02};
    const FlatPrior prior(config, c, 8, 3, rise, errors);
    EXPECT_EQ(prior.template_error(1), 0.001);
    EXPECT_EQ(prior.template_error(2), 0.01);
    EXPECT_EQ(prior.template_error(3), 0.02);
    EXPECT_EQ(prior.template_error(4), 0.02);
    EXPECT_EQ(prior.template_error(1000), 0.02);
    EXPECT_THROW((void)prior.template_error(0), std::out_of_range);
    const FlatPrior none(config, c, 8, 3, rise);
    EXPECT_EQ(none.template_error(1), 0.0);
    EXPECT_EQ(none.template_error(8), 0.0);
    for (const double bad : {-1e-3, kNaN, kInf}) {
        const std::vector<double> broken = {0.001, bad};
        EXPECT_THROW(FlatPrior(config, c, 8, 3, rise, broken), std::invalid_argument) << bad;
    }
}

TEST(Observation, EvidenceTakesTheSignalCount) {
    // m_signal, the carrier's expected sum over a in channel samples, need not be whole: in a mark's first block it is
    // 3 x the sum of the squared weights. For 3 x 0.193444^2 (the rise's first piece), 3 x (0.193444^2 + 0.613661^2)
    // and the whole 6: block_llr is -b^2 + ln I0(2 b z) with b = m_signal a / sqrt(v) and z = |S| / sqrt(v), computed
    // here (within 1e-6 nats, the accuracy of ln_i0), and block_evidence's evidence and posterior, in FS through
    // m_signal, match a dense integration over a (within 0.002 nats, 0.01 posterior standard deviations and 1%).
    const double v = 2.0e-6;  // FS^2
    for (const double m_signal : {3.0 * 0.193444 * 0.193444, 3.0 * (0.193444 * 0.193444 + 0.613661 * 0.613661), 6.0}) {
        SCOPED_TRACE("m_signal " + number(m_signal));
        const double root_v = std::sqrt(v);
        const double mu = 8.0 * root_v / m_signal;  // FS: the prior's b is 8
        const double s_abs = 7.5 * root_v;           // FS: z = 7.5
        for (const double a : {0.5 * mu, mu, 1.7 * mu}) {
            const double b = m_signal * a / root_v;
            EXPECT_NEAR(block_llr(s_abs, m_signal, a, v), -b * b + reference_ln_i0(2.0 * b * 7.5), 1e-6) << "a " << a;
        }
        const double p = (0.2 * mu) * (0.2 * mu);
        const BlockEvidence evidence = block_evidence(s_abs, m_signal, mu, p, v, 5);
        const Dense dense = dense_uncertain(s_abs, m_signal, mu, p, v);
        EXPECT_NEAR(evidence.llr, dense.llr, 0.002);
        EXPECT_LE(std::fabs(evidence.level.mean - dense.mean) / std::sqrt(dense.var), 0.01);
        EXPECT_LE(std::fabs(evidence.level.var / dense.var - 1.0), 0.01);
    }
}

TEST(Observation, FlatPriorDependsOnlyOnTheSamplesInABlock) {
    // The tables depend on a block only through m_signal, which with no rise is its number of samples, m: with 6
    // samples per piece, n pieces read what 3 samples per piece read at 2 n pieces, on and between the table's nodes
    // and above its top (the grid in u is the same for both, so the two agree to rounding).
    const ViterbiConfig config{};
    const std::vector<double> c = sum_noise_at(48000, 600);
    const FlatPrior threes(config, c, 200, 3);
    const FlatPrior sixes(config, c, 100, 6);
    EXPECT_EQ(threes.max_pieces(), 200);
    EXPECT_EQ(threes.samples_per_piece(), 3);
    EXPECT_EQ(sixes.max_pieces(), 100);
    EXPECT_EQ(sixes.samples_per_piece(), 6);
    Worst llr;
    Worst mean;
    Worst var;
    for (const int n : {1, 2, 7, 50, 100}) {
        for (const double z : {0.0, 0.37, 3.3, 12.5, 39.99, 40.0, 45.0}) {
            const auto at = [n, z] { return std::to_string(n) + " pieces of 6 samples, z = " + number(z); };
            llr.add(std::fabs(sixes.llr(n, z) - threes.llr(2 * n, z)), at);
            const Moments six = sixes.posterior(n, z);
            const Moments three = threes.posterior(2 * n, z);
            mean.add(std::fabs(six.mean / three.mean - 1.0), at);
            var.add(std::fabs(six.var / three.var - 1.0), at);
        }
    }
    EXPECT_LE(llr.error, 1e-12) << "nats, at " << llr.where;
    EXPECT_LE(mean.error, 1e-12) << "relative, at " << mean.where;
    EXPECT_LE(var.error, 1e-12) << "relative, at " << var.where;
    // Blocks of different lengths read different entries: one piece of 6 samples is not one piece of 3.
    EXPECT_GT(std::fabs(sixes.llr(1, 1.0) - threes.llr(1, 1.0)), 0.01);
}

TEST(Observation, RefusesWhatItCannotRead) {
    const auto refuses = [](auto call) { EXPECT_THROW(call(), std::invalid_argument); };
    // A block: an m_signal positive and finite (any real number of samples), a positive and finite noise variance, a
    // finite |S| and level, not negative.
    refuses([] { return block_llr(1.0, 0, 1.0, 1.0); });
    refuses([] { return block_llr(1.0, -0.5, 1.0, 1.0); });
    refuses([] { return block_llr(1.0, kNaN, 1.0, 1.0); });
    refuses([] { return block_llr(1.0, kInf, 1.0, 1.0); });
    EXPECT_NO_THROW((void)block_llr(1.0, 0.112, 1.0, 1.0));
    refuses([] { return block_llr(1.0, 3, 1.0, 0.0); });
    refuses([] { return block_llr(1.0, 3, 1.0, kInf); });
    refuses([] { return block_llr(-1.0, 3, 1.0, 1.0); });
    refuses([] { return block_llr(kNaN, 3, 1.0, 1.0); });
    refuses([] { return block_llr(1.0, 3, -1.0, 1.0); });
    refuses([] { return block_llr(1.0, 3, kNaN, 1.0); });
    // An uncertain level: mu and p finite and not negative, an order of 1 to 20 (2 to 20 for the posterior).
    refuses([] { return block_llr_uncertain(1.0, 3, -1.0, 0.1, 1.0, 5); });
    refuses([] { return block_llr_uncertain(1.0, 3, 1.0, -0.1, 1.0, 5); });
    refuses([] { return block_llr_uncertain(1.0, 3, 1.0, kInf, 1.0, 5); });
    refuses([] { return block_llr_uncertain(1.0, 3, 1.0, 0.1, 1.0, 0); });
    refuses([] { return block_llr_uncertain(1.0, 3, 1.0, 0.1, 1.0, 21); });
    refuses([] { return block_llr_uncertain(1.0, 0, 1.0, 0.1, 1.0, 5); });
    refuses([] { return level_posterior(1.0, 3, 1.0, 0.1, 1.0, 1); });
    refuses([] { return level_posterior(1.0, 3, kNaN, 0.1, 1.0, 5); });
    EXPECT_NO_THROW((void)block_llr_uncertain(1.0, 3, 1.0, 0.1, 1.0, 1));
    EXPECT_NO_THROW((void)block_llr_uncertain(1.0, 3, 1.0, 0.1, 1.0, 20));
    // The combined call: what level_posterior refuses, and a starting point that is not finite (one outside the
    // bracket is clamped into it).
    refuses([] { return block_evidence(1.0, 3, 1.0, 0.1, 1.0, 1); });
    refuses([] { return block_evidence(1.0, 0, 1.0, 0.1, 1.0, 5); });
    refuses([] { return block_evidence(1.0, 3, 1.0, 0.1, 1.0, 5, kNaN); });
    refuses([] { return block_evidence(1.0, 3, 1.0, 0.1, 1.0, 5, kInf); });
    refuses([] { return block_evidence(1.0, 3, 1.0, 0.0, 1.0, 5, -kInf); });
    EXPECT_NO_THROW((void)block_evidence(1.0, 3, 1.0, 0.1, 1.0, 5, -3.0));
    // The noise of a sum: taps, a positive rate, a decimation of at least 1, at least the empty sum.
    const std::vector<double> taps = kz4ap::channel_filter_taps(channelizer_at(48000));
    refuses([] { return sum_noise_hz({}, 48000.0, 32, 10); });
    refuses([&taps] { return sum_noise_hz(taps, 0.0, 32, 10); });
    refuses([&taps] { return sum_noise_hz(taps, kNaN, 32, 10); });
    refuses([&taps] { return sum_noise_hz(taps, 48000.0, 0, 10); });
    refuses([&taps] { return sum_noise_hz(taps, 48000.0, 32, -1); });
    EXPECT_EQ(sum_noise_hz(taps, 48000.0, 32, 0), std::vector<double>{0.0});
    // The flat prior: at least one piece of at least one sample, a c long enough for its blocks and past the filter's
    // correlation length, a level range it can tabulate.
    const std::vector<double> c = sum_noise_hz(taps, 48000.0, 32, 60);
    const ViterbiConfig config{};
    refuses([&] { return FlatPrior(config, c, 0, 3); });
    refuses([&] { return FlatPrior(config, c, 10, 0); });
    refuses([&] { return FlatPrior(config, c, 21, 3); });  // c reaches 60 samples: 20 pieces of 3, 10 of 6
    refuses([&] { return FlatPrior(config, c, 11, 6); });
    EXPECT_NO_THROW((void)FlatPrior(config, c, 10, 6));
    // c's last increment is the long-run rate only once c has passed the filter's correlation length, where its last
    // two increments agree within 1e-9 of the last: from 31 samples on (they differ by 2 R_30 = 6.5e-10 of it there and
    // by 2 R_29 = 2.1e-9 at 30 samples). 12 samples would give 1 482.27 Hz, 1.2% low.
    const std::vector<double> c12 = sum_noise_hz(taps, 48000.0, 32, 12);
    EXPECT_NEAR(c12[12] - c12[11], 1482.27, 0.01);
    refuses([&] { return FlatPrior(config, c12, 4, 3); });
    refuses([&] { return FlatPrior(config, sum_noise_hz(taps, 48000.0, 32, 30), 10, 3); });
    EXPECT_NO_THROW((void)FlatPrior(config, sum_noise_hz(taps, 48000.0, 32, 31), 10, 3));
    ViterbiConfig empty_range = config;
    empty_range.level_max_db = empty_range.level_min_db;
    refuses([&] { return FlatPrior(empty_range, c, 10, 3); });
    ViterbiConfig too_low = config;  // b at level_max_db must reach 50 for every block: 10 at 20 dB S500, 1 piece
    too_low.level_max_db = 20.0;
    refuses([&] { return FlatPrior(too_low, c, 10, 3); });
    ViterbiConfig too_high = config;  // b at level_min_db must stay at most 6 for every block: 31.6 at 30 dB S500
    too_high.level_min_db = 30.0;
    refuses([&] { return FlatPrior(too_high, c, 10, 3); });
    // c is read only for its last increment, the noise's long-run rate, which must be positive and finite.
    std::vector<double> flat_end = c;
    flat_end.back() = flat_end[flat_end.size() - 2];
    refuses([&] { return FlatPrior(config, flat_end, 10, 3); });
    std::vector<double> broken_end = c;
    broken_end.back() = kNaN;
    refuses([&] { return FlatPrior(config, broken_end, 10, 3); });
    // Every rise fraction must be positive and finite.
    refuses([&] { return FlatPrior(config, c, 10, 3, std::vector<double>{0.2, 0.0}); });
    refuses([&] { return FlatPrior(config, c, 10, 3, std::vector<double>{0.2, kNaN}); });
    // The rise: taps that add up to a positive, finite sum, a positive and finite input rate, a decimation and samples
    // per piece of at least 1, and a rise_s of 0 to 1 s, each checked before anything is converted (an absurd rise_s,
    // 1e10 s, would overflow the count of pieces), and an edge and filter that an int counts in input samples.
    refuses([] { return rise_fractions({}, 48000.0, 32, 3, 0.005); });
    refuses([] { return rise_fractions(std::vector<double>{0.5, -0.5}, 48000.0, 32, 3, 0.005); });
    refuses([] { return rise_fractions(std::vector<double>{1.0, kInf}, 48000.0, 32, 3, 0.005); });
    refuses([&taps] { return rise_fractions(taps, 0.0, 32, 3, 0.005); });
    refuses([&taps] { return rise_fractions(taps, kNaN, 32, 3, 0.005); });
    refuses([&taps] { return rise_fractions(taps, kInf, 32, 3, 0.005); });
    refuses([&taps] { return rise_fractions(taps, 48000.0, 0, 3, 0.005); });
    refuses([&taps] { return rise_fractions(taps, 48000.0, 32, 0, 0.005); });
    for (const double rise_s : {-1e-9, kNaN, kInf, -kInf, 1.0001, 1e10, 1e300})
        refuses([&taps, rise_s] { return rise_fractions(taps, 48000.0, 32, 3, rise_s); });
    refuses([&taps] { return rise_fractions(taps, 1e12, 32, 3, 0.005); });  // an edge of 5e9 input samples
    EXPECT_NO_THROW((void)rise_fractions(taps, 48000.0, 32, 3, 0.0));
    EXPECT_NO_THROW((void)rise_fractions(taps, 48000.0, 32, 3, 0.02));
    refuses([&taps] { return detail::rise_fractions_by_position(taps, 48000.0, 32, 3, 0.005, -1); });
    refuses([&taps] { return detail::rise_fractions_by_position(taps, 48000.0, 32, 3, 1e10, 4); });
    refuses([] { return detail::rise_fractions_by_position({}, 48000.0, 32, 3, 0.005, 4); });
    const FlatPrior prior(config, c, 10, 3);
    EXPECT_THROW((void)prior.llr(0, 1.0), std::out_of_range);
    EXPECT_THROW((void)prior.llr(11, 1.0), std::out_of_range);
    EXPECT_THROW((void)prior.posterior(-1, 1.0), std::out_of_range);
    EXPECT_THROW((void)prior.weight(0), std::out_of_range);
    EXPECT_THROW((void)prior.signal_samples(0), std::out_of_range);
    refuses([&prior] { return prior.llr(1, -0.5); });
    refuses([&prior] { return prior.llr(1, kNaN); });
    refuses([&prior] { return prior.posterior(1, kInf); });
}

// --- The evidence per sample (plan 1b, Task 5) ---

namespace {

// The per-sample score and the amplitude's posterior by direct integration over a >= 0 (FS): Simpson's rule on 20 001
// points of N(a; mu, p) e^(w Lambda(a)), Lambda = -b^2 + ln I0(2 b z), b = |s| a / sqrt(v'), z = e / sqrt(v'), with
// reference_ln_i0, in the log so that nothing overflows, less ln Phi(mu / sqrt(p)) for the truncation at a = 0. The
// interval is where the log integrand lies within 60 nats of its highest value: a first scan over a = 0 and 20 000
// points spaced evenly in ln a from 1e-12 of the top to the top finds the highest value and the scan's points above
// that threshold, and the integral runs between the scan's points just outside them. The top: beyond m = max(beta, z)
// (beta = |s| mu / sqrt(v'), in units of b) the log integrand is at most w z^2 - (b - m)^2 (1/(2 q) + w) (ln I0(y) <= y;
// q = s^2 p / v'), and at b = beta it is at least -w beta^2, so 100 nats below that lie within
// sqrt((w (z^2 + beta^2) + 100) / (1/(2 q) + w)) of m.
Dense dense_tempered(double e, double s, double mu, double p, double v_prime, double w) {
    const double root_v = std::sqrt(v_prime);
    const double scale = std::fabs(s) / root_v;  // units of b per FS
    const double z = e / root_v;
    const double beta = scale * mu;
    const double q = scale * scale * p;
    const auto log_f = [&](double a) {
        const double b = scale * a;
        const double d = a - mu;
        return -0.5 * d * d / p + w * (-b * b + reference_ln_i0(2.0 * b * z));
    };
    const double m = std::max(beta, z);
    const double top = (m + std::sqrt((w * (z * z + beta * beta) + 100.0) / (0.5 / q + w)) + 1.0) / scale;
    constexpr int kScan = 20000;
    std::vector<double> xs(kScan + 1);
    std::vector<double> ls(kScan + 1);
    double highest = -kInf;
    for (int i = 0; i <= kScan; ++i) {
        xs[i] = i == 0 ? 0.0 : top * std::pow(10.0, -12.0 + 12.0 * (i - 1) / (kScan - 1));
        ls[i] = log_f(xs[i]);
        highest = std::max(highest, ls[i]);
    }
    int first = kScan;
    int last = 0;
    for (int i = 0; i <= kScan; ++i) {
        if (ls[i] > highest - 60.0) {
            first = std::min(first, i);
            last = std::max(last, i);
        }
    }
    const double from = first == 0 ? 0.0 : xs[first - 1];
    const double to = last == kScan ? top : xs[last + 1];
    constexpr int kIntervals = 20000;
    const double step = (to - from) / kIntervals;
    std::vector<double> a(kIntervals + 1);
    std::vector<double> lf(kIntervals + 1);
    double peak = -kInf;
    for (int i = 0; i <= kIntervals; ++i) {
        a[i] = from + i * step;
        lf[i] = log_f(a[i]);
        peak = std::max(peak, lf[i]);
    }
    double s0 = 0.0;
    double s1 = 0.0;
    std::vector<double> f(kIntervals + 1);
    for (int i = 0; i <= kIntervals; ++i) {
        f[i] = simpson_weight(i, kIntervals) * std::exp(lf[i] - peak);
        s0 += f[i];
        s1 += f[i] * a[i];
    }
    const double mean = s1 / s0;
    double s2 = 0.0;
    for (int i = 0; i <= kIntervals; ++i) s2 += f[i] * (a[i] - mean) * (a[i] - mean);
    return {peak + std::log(s0 * step / 3.0) - 0.5 * std::log(2.0 * kPi * p) - std::log(normal_cdf(mu / std::sqrt(p))),
            mean, s2 / s0};
}

constexpr double kSampleVariance = 2.5e-5;  // FS^2: V' in the tests below (any value; b and z are in its units)

}  // namespace

TEST(Evidence, ScoreMatchesADenseIntegral) {
    // Part 15, item 1 (D4): the per-sample score within 0.01 nats of a dense integral over a >= 0, and the posterior's
    // mean within 1e-3 and its variance within 1% of the dense integral's, relatively, for z = e / sqrt(V') of 0 to 100,
    // templates s of -0.0008 (the channel filter's precursor: a negative s scores as |s|), 0.0505, 0.4334 and 1, the
    // prior's center at b = |s| mu / sqrt(V') of 0.1 to 30, its relative spread sqrt(P) / mu of 0.01 to 3 (the
    // broad-prior region included), and w of 0.1986, 0.23, 0.2754 and 1 (the untempered form). Order 5.
    Worst llr;
    Worst mean;
    Worst var;
    int over = 0;  // points whose score misses by more than 0.01 nats
    const double root_v = std::sqrt(kSampleVariance);
    for (const double w : {0.1986, 0.23, 0.2754, 1.0}) {
        for (const double z : {0.0, 0.5, 1.0, 2.0, 5.0, 20.0, 100.0}) {
            for (const double beta : {0.1, 1.0, 3.0, 10.0, 30.0}) {
                for (const double spread : {0.01, 0.1, 0.5, 1.0, 3.0}) {
                    for (const double s : {-0.0008, 0.0505, 0.4334, 1.0}) {
                        const double mu = beta * root_v / std::fabs(s);
                        const double p = (spread * mu) * (spread * mu);
                        const double e = z * root_v;
                        const auto at = [=] {
                            return "w = " + number(w) + ", z = " + number(z) + ", b = " + number(beta) +
                                   ", spread " + number(spread) + ", s = " + number(s);
                        };
                        const Dense dense = dense_tempered(e, s, mu, p, kSampleVariance, w);
                        const SampleEvidence evidence = sample_evidence(e, s, mu, p, kSampleVariance, w, 5);
                        const double miss = std::fabs(evidence.score_nats - dense.llr);
                        if (!(miss <= 0.01)) ++over;
                        llr.add(miss, at);
                        mean.add(std::fabs(evidence.amplitude.mean / dense.mean - 1.0), at);
                        var.add(std::fabs(evidence.amplitude.var / dense.var - 1.0), at);
                    }
                }
            }
        }
    }
    std::cout << "worst: score " << llr.error << " nats at " << llr.where << "; mean " << mean.error
              << " relative at " << mean.where << "; variance " << var.error << " relative at " << var.where
              << "; points over 0.01 nats: " << over << " of 2800\n";
    EXPECT_LE(llr.error, 0.01) << "nats, at " << llr.where;
    EXPECT_LE(mean.error, 1e-3) << "relative, at " << mean.where;
    EXPECT_LE(var.error, 0.01) << "relative, at " << var.where;
}

TEST(Evidence, BroadPriorsMatchDenseIntegralsAcrossW) {
    // The broad-prior regime's tables (O9) between their nodes of w: in the rescaled coordinates y = sqrt(w) z / s_w
    // and kappa' = (b / q) / s_w, s_w = sqrt(w + 1/(2 q)) (b and q the prior's mean and variance in units of b), at
    // y of 0 to 3.9 around the degenerate point y = 1, kappa' of 0 to 1.95, q of 0.09 to 1e6, and w at 17 points
    // over 0.1986 to 0.2755 and at 1: the score within 0.01 nats of the dense integral, and the posterior's mean within
    // 1e-3 and its variance within 1%, relatively (s = 1).
    Worst llr;
    Worst mean;
    Worst var;
    const double root_v = std::sqrt(kSampleVariance);
    std::vector<double> weights;
    for (int i = 0; i <= 16; ++i) weights.push_back(0.1986 + (0.2755 - 0.1986) * i / 16.0);
    weights.push_back(1.0);
    for (const double w : weights) {
        for (const double q : {0.09, 1.0, 10.0, 100.0, 1e4, 1e6}) {
            const double s_w = std::sqrt(w + 0.5 / q);
            for (const double y : {0.0, 0.5, 0.9, 1.0, 1.1, 1.5, 2.0, 3.0, 3.9}) {
                for (const double kappa : {0.0, 0.3, 0.9, 1.5, 1.95}) {
                    const double beta = kappa * s_w * q;
                    const double z = y * s_w / std::sqrt(w);
                    const double mu = std::max(beta, 1e-9) * root_v;  // b = 0 as a level just above it
                    const double p = q * kSampleVariance;
                    const double e = z * root_v;
                    const auto at = [=] {
                        return "w = " + number(w) + ", q = " + number(q) + ", y = " + number(y) + ", kappa' = " +
                               number(kappa);
                    };
                    const Dense dense = dense_tempered(e, 1.0, mu, p, kSampleVariance, w);
                    const SampleEvidence evidence = sample_evidence(e, 1.0, mu, p, kSampleVariance, w, 5);
                    llr.add(std::fabs(evidence.score_nats - dense.llr), at);
                    mean.add(std::fabs(evidence.amplitude.mean / dense.mean - 1.0), at);
                    var.add(std::fabs(evidence.amplitude.var / dense.var - 1.0), at);
                }
            }
        }
    }
    std::cout << "worst: score " << llr.error << " nats at " << llr.where << "; mean " << mean.error
              << " relative at " << mean.where << "; variance " << var.error << " relative at " << var.where << "\n";
    EXPECT_LE(llr.error, 0.01) << "nats, at " << llr.where;
    EXPECT_LE(mean.error, 1e-3) << "relative, at " << mean.where;
    EXPECT_LE(var.error, 0.01) << "relative, at " << var.where;
}

TEST(Evidence, ZeroTemplateAddsNothing) {
    // s = 0: Lambda = 0, the score 0 nats, the posterior the prior and the mode the start (mu without one).
    const SampleEvidence cold = sample_evidence(0.3, 0.0, 0.01, 1e-4, 1e-5, 0.2, 5);
    EXPECT_EQ(cold.score_nats, 0.0);
    EXPECT_EQ(cold.amplitude.mean, 0.01);
    EXPECT_EQ(cold.amplitude.var, 1e-4);
    EXPECT_EQ(cold.mode_a_fs, 0.01);
    const SampleEvidence warm = sample_evidence(0.3, 0.0, 0.01, 1e-4, 1e-5, 0.2, 5, 0.02);
    EXPECT_EQ(warm.score_nats, 0.0);
    EXPECT_EQ(warm.amplitude.mean, 0.01);
    EXPECT_EQ(warm.amplitude.var, 1e-4);
    EXPECT_EQ(warm.mode_a_fs, 0.02);
}

TEST(Evidence, UnitWeightIsTodaysBlockEvidence) {
    // w = 1 is the block form's evidence with m_signal = s and V = V': block_evidence's value within 1e-12 nats, and its
    // posterior and mode within 1e-12 relatively, wherever both use the adaptive quadrature (outside both broad-prior
    // regimes: the block form's, z' <= 4 and kappa' <= 1, and the per-sample score's, which at w = 1 is z' <= 4 and
    // kappa' <= 2, with s_r = sqrt(1 + 1/(2 q)), z' = z / s_r, kappa' = beta / q / s_r) and s > 0 (block_evidence
    // refuses a signal count that is not positive). Cold and warm. Deleted with block_evidence in Task 10.
    Worst llr;
    Worst moments;
    int compared = 0;
    const double root_v = std::sqrt(kSampleVariance);
    for (const double z : {0.0, 0.5, 1.0, 2.0, 5.0, 20.0, 100.0}) {
        for (const double beta : {0.1, 1.0, 3.0, 10.0, 30.0}) {
            for (const double spread : {0.01, 0.1, 0.5, 1.0, 3.0}) {
                const double q = (spread * beta) * (spread * beta);
                const double s_r = std::sqrt(1.0 + 0.5 / q);
                if (z / s_r <= 4.0 && beta / q / s_r <= 2.0) continue;  // either regime
                for (const double s : {0.0505, 0.4334, 1.0}) {
                    const double mu = beta * root_v / s;
                    const double p = (spread * mu) * (spread * mu);
                    const double e = z * root_v;
                    for (const std::optional<double> start :
                         {std::optional<double>(), std::optional<double>(0.9 * mu)}) {
                        const auto at = [=] {
                            return "z = " + number(z) + ", b = " + number(beta) + ", spread " + number(spread) +
                                   ", s = " + number(s) + (start ? ", warm" : ", cold");
                        };
                        const SampleEvidence tempered = sample_evidence(e, s, mu, p, kSampleVariance, 1.0, 5, start);
                        const BlockEvidence block = block_evidence(e, s, mu, p, kSampleVariance, 5, start);
                        llr.add(std::fabs(tempered.score_nats - block.llr), at);
                        moments.add(std::fabs(tempered.amplitude.mean / block.level.mean - 1.0), at);
                        moments.add(std::fabs(tempered.amplitude.var / block.level.var - 1.0), at);
                        moments.add(std::fabs(tempered.mode_a_fs - block.mode_a) / mu, at);
                        ++compared;
                    }
                }
            }
        }
    }
    EXPECT_GT(compared, 500);
    EXPECT_LE(llr.error, 1e-12) << "nats, at " << llr.where;
    EXPECT_LE(moments.error, 1e-12) << "relative, at " << moments.where;
}

TEST(Evidence, StaysFiniteAtTheExtremes) {
    // Expected SNRs q = mu^2 s^2 / V' up to 2.1e9 (+85 dB S500, the newborn prior's top), the prior's variance from
    // 1e-12 mu^2 to 1e4 mu^2, samples from nothing to twice the prediction, w of 0.1986 to 1, cold and started at 0:
    // the score, the posterior and the mode are finite, the mean and variance not negative, and the score is below its
    // bound w z^2 (+0.01 nats for the quadrature).
    for (const double q : {1e-6, 1.0, 1e3, 1e6, 2.1e9}) {
        const double mu = std::sqrt(q);  // V' = 1 FS^2, s = 1
        for (const double e : {0.0, 1.0, 0.5 * mu, mu, 2.0 * mu}) {
            for (const double relative_p : {1e-12, 1e-6, 1.0, 1e4}) {
                for (const double w : {0.1986, 0.2754, 1.0}) {
                    for (const std::optional<double> start : {std::optional<double>(), std::optional<double>(0.0)}) {
                        SCOPED_TRACE("q = " + number(q) + ", e = " + number(e) + ", P / mu^2 = " +
                                     number(relative_p) + ", w = " + number(w) + (start ? ", from 0" : ", cold"));
                        const SampleEvidence evidence =
                            sample_evidence(e, 1.0, mu, relative_p * mu * mu, 1.0, w, 5, start);
                        EXPECT_TRUE(std::isfinite(evidence.score_nats)) << evidence.score_nats;
                        EXPECT_TRUE(std::isfinite(evidence.amplitude.mean)) << evidence.amplitude.mean;
                        EXPECT_TRUE(std::isfinite(evidence.amplitude.var)) << evidence.amplitude.var;
                        EXPECT_TRUE(std::isfinite(evidence.mode_a_fs)) << evidence.mode_a_fs;
                        EXPECT_GE(evidence.amplitude.mean, 0.0);
                        EXPECT_GE(evidence.amplitude.var, 0.0);
                        EXPECT_LE(evidence.score_nats, w * e * e + 0.01);
                    }
                }
            }
        }
    }
}
