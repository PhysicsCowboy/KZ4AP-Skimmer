#include "kz4ap/viterbi/observation.hpp"

#include "kz4ap/channelizer.hpp"
#include "kz4ap/types.hpp"
#include "kz4ap/viterbi/config.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numbers>
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
Dense dense_uncertain(double s_abs, int m, double mu, double p, double v, double a_from, double a_to) {
    constexpr int kIntervals = 20000;
    const double step = (a_to - a_from) / kIntervals;
    const double root_v = std::sqrt(v);
    const double z = s_abs / root_v;
    std::vector<double> a(kIntervals + 1);
    std::vector<double> log_f(kIntervals + 1);
    double top = -kInf;
    for (int i = 0; i <= kIntervals; ++i) {
        a[i] = a_from + i * step;
        const double b = m * a[i] / root_v;
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

// The same over the whole of a >= 0 where the integrand is not negligible: from 0 to (max(b, z) + 15) sqrt(v) / m,
// b = m mu / sqrt(v) the prior's center and z = s_abs / sqrt(v). The posterior's mode lies below max(b, z), and above
// it the integrand falls at least as fast as the likelihood's bound e^(z^2 - (b - z)^2) (ln I0(y) <= y).
Dense dense_uncertain(double s_abs, int m, double mu, double p, double v) {
    const double root_v = std::sqrt(v);
    const double top_b = std::max(m * mu / root_v, s_abs / root_v) + 15.0;
    return dense_uncertain(s_abs, m, mu, p, v, 0.0, top_b * root_v / m);
}

// Lambda_flat and the posterior of b by direct integration over the level x, dB S500, from level_min_db to
// level_max_db: Simpson's rule on 50 001 points (steps of 0.002 dB) of e^Lambda(S; a(x)) / (level_max_db -
// level_min_db), b(x)^2 = 10^(x / 10) x 500 Hz x m^2 / c(m), m = 3 pieces, with reference_ln_i0, in the log. Points
// whose bound on Lambda, z^2 - (b - z)^2 (ln I0(y) <= y), lies more than 1 000 nats below 0 are left out: Lambda at
// the lowest level, where b <= 6, is above -37 nats.
Dense direct_flat(const ViterbiConfig& config, const std::vector<double>& c, int pieces, double z) {
    constexpr int kIntervals = 50000;
    const int m = 3 * pieces;
    const double gain = 500.0 * m * m / c[static_cast<std::size_t>(m)];
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

TEST(Observation, ChannelFilterTapsAreTheChannelizers) {
    // fft_size / 2 + 1 taps, symmetric about the middle, summing to 1: 1 025 at 48 kHz and 4 097 at 192 kHz.
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
    // n V(3); it has V(3 n). On the real filter their ratio lies between 2.8 and 3.3 dB for marks of 8 to 720 ms (4 to
    // 360 pieces; design notes, topic 4, item 7): 1.91 to 2.14, rising with the mark's length toward its limit for long
    // marks, 4 500 Hz / c(3) = 2.138 (3.30 dB), as c(m) -> 1 500 Hz x m (derived).
    for (const int rate : {48000, 192000}) {
        SCOPED_TRACE(std::to_string(rate) + " samples/s");
        const std::vector<double> c = sum_noise_at(rate, 3 * 360);
        Worst below;
        Worst above;
        for (int n = 4; n <= 360; ++n) {
            const double ratio_db = 10.0 * std::log10(c[static_cast<std::size_t>(3 * n)] / (n * c[3]));
            const auto at = [n, ratio_db] { return std::to_string(n) + " pieces: " + number(ratio_db) + " dB"; };
            below.add(2.8 - ratio_db, at);
            above.add(ratio_db - 3.3, at);
        }
        EXPECT_LE(below.error, 0.0) << "below 2.8 dB at " << below.where;
        EXPECT_LE(above.error, 0.0) << "above 3.3 dB at " << above.where;
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
    EXPECT_LE(llr.error, 1e-9) << "nats, at " << llr.where;
    EXPECT_LE(mean.error, 1e-9) << "relative, at " << mean.where;
    EXPECT_LE(var.error, 1e-9) << "relative, at " << var.where;
    EXPECT_LE(mode.error, 1e-9) << "posterior standard deviations, at " << mode.where;
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
    // nothing and of a little: within 0.01 nats of the dense integral, taken over b from 0 to where the posterior
    // ends (40 prior standard deviations, moved by at most 2 sd^2 z toward z, or 15 beyond z for a broad prior).
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
    // filter at 48 kHz. At 20 points (n, z), on and between the table's nodes, Lambda_flat within 0.01 nats of a direct
    // integration over the level x; the posterior's mean within 0.01 standard deviations and its variance within 1%.
    const ViterbiConfig config{};
    const int max_pieces = static_cast<int>(block_length_s(config.fading_spread_hz, config.block_loss_db) /
                                            config.piece_s);
    ASSERT_EQ(max_pieces, 195);
    const std::vector<double> c = sum_noise_at(48000, 3 * max_pieces);
    const FlatPrior prior(config, c, max_pieces, 3);
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
        const Dense direct = direct_flat(config, c, pieces, z);
        llr.add(std::fabs(prior.llr(pieces, z) - direct.llr), at);
        const Moments posterior = prior.posterior(pieces, z);
        mean.add(std::fabs(posterior.mean - direct.mean) / std::sqrt(direct.var), at);
        var.add(std::fabs(posterior.var / direct.var - 1.0), at);
    }
    EXPECT_LE(llr.error, 0.01) << "nats, at " << llr.where;
    EXPECT_LE(mean.error, 0.01) << "posterior standard deviations, at " << mean.where;
    EXPECT_LE(var.error, 0.01) << "relative, at " << var.where;

    // The large-z form, z^2 - ln(2 z^2) + ln(20 / (ln 10 x 100 dB)), with b ~ N(z, 1/2), meets the table: its error,
    // which falls as 1 / z^2, is 0.0102 nats at z = 10, 0.0025 at 20 and 0.0011 at 30 (measured), so within 0.01 from
    // z = 20 on, and 6.3e-4 at the table's end, z = 40, where the form takes over.
    const double constant = std::log(20.0 / (std::numbers::ln10 * 100.0));
    const auto large_z = [constant](double z) { return z * z - std::log(2.0 * z * z) + constant; };
    for (const int pieces : {1, 60, 195}) {
        SCOPED_TRACE(std::to_string(pieces) + " pieces");
        for (const double z : {20.0, 25.0, 30.0, 35.0, 39.99, 40.0})
            EXPECT_NEAR(prior.llr(pieces, z), large_z(z), 0.01) << "z = " << z;
        EXPECT_NEAR(prior.llr(pieces, 40.0), large_z(40.0), 0.001);
        const Moments at_end = prior.posterior(pieces, 40.0);
        EXPECT_NEAR(at_end.mean, 40.0, 0.05);
        EXPECT_NEAR(at_end.var, 0.5, 0.005);
        for (const double z : {std::nextafter(40.0, 41.0), 40.5, 77.7, 1234.5}) {
            EXPECT_DOUBLE_EQ(prior.llr(pieces, z), large_z(z)) << "z = " << z;
            const Moments above = prior.posterior(pieces, z);
            EXPECT_EQ(above.mean, z);
            EXPECT_EQ(above.var, 0.5);
        }
    }
}

TEST(Observation, FlatPriorDependsOnlyOnTheSamplesInABlock) {
    // The tables depend on a block only through its number of samples, m: with 6 samples per piece, n pieces read what
    // 3 samples per piece read at 2 n pieces, on and between the table's nodes and above its top (the grid in u is the
    // same for both, so the two agree to rounding).
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
    // A block: at least one sample, a positive and finite noise variance, a finite |S| and level, not negative.
    refuses([] { return block_llr(1.0, 0, 1.0, 1.0); });
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
    // The flat prior: at least one piece of at least one sample, a c long enough for its blocks, a level range it can
    // tabulate.
    const std::vector<double> c = sum_noise_hz(taps, 48000.0, 32, 30);
    const ViterbiConfig config{};
    refuses([&] { return FlatPrior(config, c, 0, 3); });
    refuses([&] { return FlatPrior(config, c, 10, 0); });
    refuses([&] { return FlatPrior(config, c, 11, 3); });  // c reaches 30 samples: 10 pieces of 3, 5 of 6
    refuses([&] { return FlatPrior(config, c, 6, 6); });
    EXPECT_NO_THROW((void)FlatPrior(config, c, 5, 6));
    ViterbiConfig empty_range = config;
    empty_range.level_max_db = empty_range.level_min_db;
    refuses([&] { return FlatPrior(empty_range, c, 10, 3); });
    ViterbiConfig too_low = config;  // b at level_max_db must reach 50 for every block: 14.6 at 20 dB S500, 1 piece
    too_low.level_max_db = 20.0;
    refuses([&] { return FlatPrior(too_low, c, 10, 3); });
    ViterbiConfig too_high = config;  // b at level_min_db must stay at most 6 for every block: 46 at 30 dB S500
    too_high.level_min_db = 30.0;
    refuses([&] { return FlatPrior(too_high, c, 10, 3); });
    std::vector<double> broken = c;
    broken[6] = 0.0;
    refuses([&] { return FlatPrior(config, broken, 10, 3); });
    const FlatPrior prior(config, c, 10, 3);
    EXPECT_THROW((void)prior.llr(0, 1.0), std::out_of_range);
    EXPECT_THROW((void)prior.llr(11, 1.0), std::out_of_range);
    EXPECT_THROW((void)prior.posterior(-1, 1.0), std::out_of_range);
    refuses([&prior] { return prior.llr(1, -0.5); });
    refuses([&prior] { return prior.llr(1, kNaN); });
    refuses([&prior] { return prior.posterior(1, kInf); });
}
