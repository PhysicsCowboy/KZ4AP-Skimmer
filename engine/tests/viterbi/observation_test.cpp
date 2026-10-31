#include "kz4ap/viterbi/observation.hpp"

#include "kz4ap/channelizer.hpp"
#include "kz4ap/types.hpp"

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

kz4ap::ChannelizerConfig channelizer_at(int rate_hz) {
    kz4ap::ChannelizerConfig config;
    config.sample_rate = rate_hz;
    config.fft_size = rate_hz / 48000 * 2048;
    return config;
}

std::string number(double x) { return std::to_string(x); }

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
    // broad-prior region included), and w = 0.1986, the one constant (D23). Order 5.
    Worst llr;
    Worst mean;
    Worst var;
    int over = 0;  // points whose score misses by more than 0.01 nats
    const double root_v = std::sqrt(kSampleVariance);
    for (const double w : {0.1986}) {
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
              << "; points over 0.01 nats: " << over << " of 700\n";
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

TEST(Evidence, FullFormMatchesADenseIntegral) {
    // D23's score against the shared reference within 0.01 nats of a 20 001-point dense reference over a >= 0, a log-sum-
    // exp of the pointwise full density w [ln p(e | a, V) - ln p0(e | sigma_y^2)] + ln N(a; mu, P) (so it checks the
    // decomposition into the ratio plus three terms independently): e / sigma_y of 0.01 to 100, s of -0.0008 to 1, mu /
    // sigma_y of 0.1 to 30, sqrt(P) / mu of 0.01 to 3, w = 0.1986, V from the prediction at f = 0.02 and v of the
    // template's rise.
    constexpr double kW = 0.1986;
    constexpr double kFloor = 0.02;
    const double sigma2 = 2.5;
    const double sigma = std::sqrt(sigma2);
    Worst worst;
    for (const double e_ratio : {0.01, 0.5, 1.0, 2.0, 5.0, 20.0, 100.0}) {
        for (const auto& [s, v] : std::vector<std::pair<double, double>>{{-0.0008, 2.8e-5}, {0.0505, 1.0e-3}, {0.4334, 4.2e-3}, {1.0, 0.0}}) {
            for (const double mu_ratio : {0.1, 1.0, 3.0, 10.0, 30.0}) {
                for (const double spread : {0.01, 0.1, 0.5, 1.0, 3.0}) {
                    const double e = e_ratio * sigma;
                    const double mu = mu_ratio * sigma;
                    const double p = (spread * mu) * (spread * mu);
                    const double variance = sigma2 + (mu * mu + p) * (kFloor * kFloor * s * s + v);
                    // Far enough that the prior's cost outweighs the most the likelihood can pay, w e^2 / sigma_y^2.
                    const double a_top = mu + spread * mu * std::sqrt(2.0 * (kW * e * e / sigma2 + 50.0));
                    // The integrand's peak can be narrow: the grid is fine where the prior and the likelihood sit.
                    constexpr int kPoints = 20001;
                    const double step = a_top / (kPoints - 1);
                    std::vector<double> g(kPoints);
                    double top = -kInf;
                    double prior_top = -kInf;
                    std::vector<double> prior(kPoints);
                    for (int i = 0; i < kPoints; ++i) {
                        const double a = i * step;
                        const double exponent = -0.5 * (a - mu) * (a - mu) / p;
                        const double density = -std::log(variance / sigma2) - (e * e + s * s * a * a) / variance +
                                               e * e / sigma2 + ln_i0(2.0 * e * std::fabs(s) * a / variance);
                        g[i] = kW * density + exponent;
                        prior[i] = exponent;
                        top = std::max(top, g[i]);
                        prior_top = std::max(prior_top, exponent);
                    }
                    double sum = 0.0;
                    double prior_sum = 0.0;
                    for (int i = 0; i < kPoints; ++i) {
                        sum += std::exp(g[i] - top);
                        prior_sum += std::exp(prior[i] - prior_top);
                    }
                    const double dense = top + std::log(sum) - prior_top - std::log(prior_sum);
                    const double score = story_evidence(e, s, mu, p, variance, sigma2, kW, 5).score_nats;
                    worst.add(std::fabs(score - dense), [=] {
                        return "e/sigma " + number(e_ratio) + ", s " + number(s) + ", mu/sigma " + number(mu_ratio) +
                               ", spread " + number(spread);
                    });
                }
            }
        }
    }
    std::cout << "measured: full form against the dense integral: worst " << worst.error << " nats at " << worst.where << "\n";
    EXPECT_LE(worst.error, 0.01) << worst.where;
}

TEST(Evidence, SharedReferenceScoresNoSignalZero) {
    // s = 0 (v = 0): V = sigma_y^2 and the score is exactly 0, whatever the sample.
    const SampleEvidence zero = story_evidence(3.0, 0.0, 1.0, 0.1, 2.0, 2.0, 0.1986, 5);
    EXPECT_EQ(zero.score_nats, 0.0);
    EXPECT_EQ(zero.amplitude.mean, 1.0);
    EXPECT_EQ(zero.amplitude.var, 0.1);
    EXPECT_THROW((void)story_evidence(3.0, 0.5, 1.0, 0.1, 2.0, 0.0, 0.1986, 5), std::invalid_argument);
}

TEST(Evidence, TruncatedNodeTableMatchesTheDirectNodes) {
    // Where the Gaussian at the mode has mass below 0 the quadrature reads its nodes from a table in tau: against the
    // nodes computed by Phi^-1, within 1e-6 nats, on weak and broad priors at orders 2, 5 and 12.
    Worst worst;
    for (const int order : {2, 5, 12})
        for (const double b : {0.1, 0.3, 1.0, 3.0, 10.0})
            for (const double spread : {0.5, 1.0, 2.0, 3.0})
                for (const double z_over_b : {0.0, 0.5, 1.0, 2.0, 3.0}) {
                    const double p = (spread * b) * (spread * b);
                    const SampleEvidence table = sample_evidence(z_over_b * b, 1.0, b, p, 1.0, 0.1986, order);
                    const SampleEvidence direct = detail::sample_evidence_direct_nodes(z_over_b * b, 1.0, b, p, 1.0, 0.1986, order);
                    worst.add(std::fabs(table.score_nats - direct.score_nats), [=] { return number(b); });
                }
    EXPECT_LE(worst.error, 1e-6) << worst.where;
}

TEST(Evidence, StartingPointChangesNothing) {
    // The start of Newton's method changes the score by at most 1e-9 nats.
    for (const double start : {0.0, 0.5, 2.0, 40.0}) {
        const SampleEvidence cold = sample_evidence(3.0, 0.8, 4.0, 0.5, 1.0, 0.1986, 5);
        const SampleEvidence warm = sample_evidence(3.0, 0.8, 4.0, 0.5, 1.0, 0.1986, 5, start);
        EXPECT_NEAR(cold.score_nats, warm.score_nats, 1e-9) << start;
    }
}
