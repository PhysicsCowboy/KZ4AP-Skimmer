#include "kz4ap/viterbi/long_run.hpp"

#include "../test_signals.hpp"
#include "kz4ap/channelizer.hpp"
#include "kz4ap/viterbi/config.hpp"
#include "kz4ap/viterbi/front_filter.hpp"
#include "kz4ap/viterbi/mark_template.hpp"
#include "kz4ap/viterbi_decoder.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numbers>
#include <random>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using namespace kz4ap;
using namespace kz4ap::viterbi;

namespace {

constexpr double kDelta = 4.0 / 1500.0;  // s: the sample interval (D17)
constexpr double kSpreadHz = 2.0;        // f_D (owner, 2026-10-22)
constexpr int kBinSamples = 16;          // samples: nu's bins, 42.67 ms (owner, D17)
constexpr int kBinsKept = 10;            // bins (owner, D17)

double rel(double a, double b) { return std::abs(a - b) / std::abs(b); }

ChannelizerConfig chain192() {
    ChannelizerConfig config;
    config.sample_rate = 192000;
    config.fft_size = 8192;
    return config;
}

// The template's tables on the 192 kHz chain (mark_template.hpp).
const TemplateTables& tables192() {
    static const TemplateTables t = [] {
        const ChannelizerConfig config = chain192();
        return make_template_tables(channel_filter_taps(config), config.sample_rate,
                                    config.fft_size / config.channel_bins, front_filter_taps(1500.0, 0.012, 0.0025), 4,
                                    0.0025, 0.0008, 0.0047);
    }();
    return t;
}

// The output noise of the 192 kHz chain (front_filter.hpp).
const OutputNoise& noise192() {
    static const OutputNoise n = [] {
        const ChannelizerConfig config = chain192();
        return output_noise(channel_filter_taps(config), config.sample_rate, config.fft_size / config.channel_bins,
                            front_filter_taps(1500.0, 0.012, 0.0025), 4);
    }();
    return n;
}

// The template s_k (1) of a story claiming the given claimed edges (samples, alternating from a key-down), for
// k = 0 ... the last edge + 16 (the end of the last tail).
std::vector<double> template_of_edges(const std::vector<std::int64_t>& edges) {
    std::vector<double> s;
    EdgeHistory h;
    std::size_t next = 0;
    for (std::int64_t k = 0; k <= edges.back() + TemplateTables::kSpan; ++k) {
        const bool edge = next < edges.size() && edges[next] == k;
        if (edge) ++next;
        advance(h, edge);
        s.push_back(template_value(h, tables192()).s);
    }
    return s;
}

// The claimed edges of keyed intervals (s): each edge at the first sample at or after it.
std::vector<std::int64_t> claimed_edges(const std::vector<std::pair<double, double>>& intervals) {
    std::vector<std::int64_t> edges;
    for (const auto& [on, off] : intervals) {
        edges.push_back(claimed_edge_sample(on, kDelta));
        edges.push_back(claimed_edge_sample(off, kDelta));
    }
    return edges;
}

// The template of text at wpm, keyed from start_s (s).
std::vector<double> template_of_text(const std::string& text, double wpm, double start_s = 0.5) {
    return template_of_edges(claimed_edges(test::keying(text, wpm, start_s)));
}

// nu on the samples themselves (no bins): (sum w)^2 / sum_j sum_l w_j w_l rho^2((j - l) Delta), w_k = s_k^4.
double exact_nu(const std::vector<double>& weights, double spread_hz = kSpreadHz) {
    std::vector<double> rho2;
    for (int m = 0;; ++m) {
        const double tau = m * kDelta;
        const double r = std::exp(-std::numbers::pi * std::numbers::pi * spread_hz * spread_hz * tau * tau);
        if (r < 1e-18 || m >= static_cast<int>(weights.size())) break;
        rho2.push_back(r);
    }
    long double total = 0.0L;
    long double pairs = 0.0L;
    for (std::size_t j = 0; j < weights.size(); ++j) {
        total += weights[j];
        if (weights[j] == 0.0) continue;
        pairs += static_cast<long double>(weights[j]) * weights[j];
        for (std::size_t m = 1; m < rho2.size() && m <= j; ++m)
            pairs += 2.0L * weights[j] * weights[j - m] * rho2[m];
    }
    return static_cast<double>(total * total / pairs);
}

// A LongRun fed with the template s at samples first ... first + s.size() - 1, e = 0 and sigma^2 = 1.
LongRun run_template(const std::vector<double>& s, int bins_kept = kBinsKept, std::int64_t first = 0) {
    LongRun run(kSpreadHz, kBinSamples, bins_kept, kDelta);
    for (std::size_t k = 0; k < s.size(); ++k) {
        run.at_sample(first + static_cast<std::int64_t>(k));
        run.add(s[k], 0.0, 1.0);
    }
    return run;
}

// Complex Gaussian noise of unit power whose correlation at lags of Delta is rho_0 = 1 ... rho_6 (zero beyond): n = L w,
// L the banded Cholesky factor (lower bandwidth 6) of the n x n Toeplitz covariance, w white. Exact for the truncated
// correlation (the chain's beyond lag 6 is below 3 x 10^-4, design part 7).
class CorrelatedNoise {
public:
    CorrelatedNoise(const std::array<double, 7>& rho, std::size_t n) : l_(n) {
        for (std::size_t i = 0; i < n; ++i) {
            for (int d = 6; d >= 0; --d) {
                if (static_cast<std::size_t>(d) > i) continue;
                const std::size_t j = i - static_cast<std::size_t>(d);
                double sum = rho[static_cast<std::size_t>(d)];
                // sum over k from max(i, j) - 6 to j - 1 of L(i, k) L(j, k)
                for (int e = d + 1; e <= 6; ++e) {
                    if (static_cast<std::size_t>(e) > i) break;
                    const std::size_t k = i - static_cast<std::size_t>(e);  // L(i, k), k = i - e < j
                    const std::size_t dj = j - k;                            // L(j, k) = l_[j][dj]
                    if (dj > 6) continue;
                    sum -= l_[i][static_cast<std::size_t>(e)] * l_[j][dj];
                }
                if (d == 0) {
                    if (!(sum > 0.0)) throw std::runtime_error("the covariance is not positive definite");
                    l_[i][0] = std::sqrt(sum);
                } else {
                    l_[i][static_cast<std::size_t>(d)] = sum / l_[j][0];
                }
            }
        }
    }
    void draw(std::mt19937_64& rng, std::vector<std::complex<double>>& out) {
        std::normal_distribution<double> gauss(0.0, std::sqrt(0.5));
        const std::size_t n = l_.size();
        white_.resize(n);
        for (auto& w : white_) w = {gauss(rng), gauss(rng)};
        out.assign(n, 0.0);
        for (std::size_t i = 0; i < n; ++i) {
            std::complex<double> x = 0.0;
            for (std::size_t d = 0; d <= 6 && d <= i; ++d) x += l_[i][d] * white_[i - d];
            out[i] = x;
        }
    }

private:
    std::vector<std::array<double, 7>> l_;  // l_[i][d] = L(i, i - d)
    std::vector<std::complex<double>> white_;
};

// FS^2 per sigma_y^2: the power of L dB S500, 10^(L/10) x 500 Hz / 74.4998 Hz.
double power_of_db(double db) { return std::pow(10.0, db / 10.0) * kS500BandwidthHz / kOmegaBarSigma2PerN0Hz; }

}  // namespace

TEST(LongRun, OmegaHatIsOrdinaryLeastSquares) {
    // On random (s, e, sigma^2), Omega_hat = sum s^2 (e^2 - sigma^2) / sum s^4 within 1e-12 relative; samples with
    // s = 0 (whatever their e) change nothing; sum_s2 and sum_s4 are the sums.
    std::mt19937_64 rng(7);
    std::uniform_real_distribution<double> unit(0.0, 1.0);
    LongRun with_zeros(kSpreadHz, kBinSamples, kBinsKept, kDelta);
    LongRun without(kSpreadHz, kBinSamples, kBinsKept, kDelta);
    long double numerator = 0.0L;
    long double s4 = 0.0L;
    long double s2 = 0.0L;
    std::int64_t k = 0;
    for (int i = 0; i < 5000; ++i, ++k) {
        const double sigma2 = 0.5 + unit(rng);
        const double e = 3.0 * unit(rng);
        const double pick = unit(rng);
        const double s = pick < 0.3 ? 0.0 : (pick < 0.35 ? -0.0008 : unit(rng));
        with_zeros.at_sample(k);
        with_zeros.add(s, s == 0.0 ? 1e6 * unit(rng) : e, sigma2);
        if (s != 0.0) {
            without.at_sample(k);
            without.add(s, e, sigma2);
            numerator += static_cast<long double>(s) * s * (static_cast<long double>(e) * e - sigma2);
            s4 += static_cast<long double>(s) * s * s * s;
            s2 += static_cast<long double>(s) * s;
        }
    }
    const double expected = static_cast<double>(numerator / s4);
    EXPECT_NEAR(with_zeros.omega_hat_fs2(), expected, 1e-12 * std::abs(expected));
    EXPECT_EQ(with_zeros.omega_hat_fs2(), without.omega_hat_fs2());
    EXPECT_EQ(with_zeros.nu(), without.nu());
    EXPECT_NEAR(with_zeros.sum_s4(), static_cast<double>(s4), 1e-12 * static_cast<double>(s4));
    EXPECT_NEAR(with_zeros.sum_s2(), static_cast<double>(s2), 1e-12 * static_cast<double>(s2));
    // Before any sample with s != 0: Omega_hat 0, nu 1; a restart returns there.
    LongRun fresh(kSpreadHz, kBinSamples, kBinsKept, kDelta);
    EXPECT_EQ(fresh.omega_hat_fs2(), 0.0);
    EXPECT_EQ(fresh.nu(), 1.0);
    fresh.at_sample(3);
    fresh.add(0.0, 5.0, 1.0);
    EXPECT_EQ(fresh.omega_hat_fs2(), 0.0);
    EXPECT_EQ(fresh.nu(), 1.0);
    with_zeros.restart();
    EXPECT_EQ(with_zeros.omega_hat_fs2(), 0.0);
    EXPECT_EQ(with_zeros.nu(), 1.0);
    EXPECT_EQ(with_zeros.sum_s4(), 0.0);
    // What it refuses: add() before at_sample() since the restart, a sample in an earlier bin, bad construction.
    EXPECT_THROW(with_zeros.add(1.0, 1.0, 1.0), std::logic_error);
    with_zeros.at_sample(100);
    EXPECT_THROW(with_zeros.at_sample(95), std::invalid_argument);  // bin 5 before the open bin 6
    EXPECT_NO_THROW(with_zeros.at_sample(97));                       // the same bin
    EXPECT_THROW(LongRun(-1.0, 16, 10, kDelta), std::invalid_argument);
    EXPECT_THROW(LongRun(std::nan(""), 16, 10, kDelta), std::invalid_argument);
    EXPECT_THROW(LongRun(2.0, 0, 10, kDelta), std::invalid_argument);
    EXPECT_THROW(LongRun(2.0, 16, 0, kDelta), std::invalid_argument);
    EXPECT_THROW(LongRun(2.0, 16, 10, 0.0), std::invalid_argument);
}

TEST(LongRun, OmegaHatIsUnbiased) {
    // 20 000 trials of a steady carrier of amplitude a on the template of "PARIS" at 25 WPM, in complex Gaussian noise
    // with the 192 kHz chain's correlation rho_m (sigma_y^2 = 1): the mean of Omega_hat is a^2 within 3 standard errors
    // at 0 and +10 dB S500 (a^2 = 6.71 and 67.1 sigma_y^2).
    const std::vector<double> s = template_of_text("PARIS", 25.0);
    CorrelatedNoise noise(noise192().rho, s.size());
    std::mt19937_64 rng(2026);
    std::vector<std::complex<double>> n;
    std::array<std::complex<double>, 8> lag_sums{};  // sum of n_k n_(k+m)^* at k = 100 ... 199, m = 0 ... 7
    for (const double db : {0.0, 10.0}) {
        const double a2 = power_of_db(db);
        const double a = std::sqrt(a2);
        constexpr int kTrials = 20000;
        double sum = 0.0;
        double sum2 = 0.0;
        for (int trial = 0; trial < kTrials; ++trial) {
            noise.draw(rng, n);
            if (db == 0.0) {
                for (std::size_t k = 100; k < 200; ++k)
                    for (std::size_t m = 0; m < lag_sums.size(); ++m) lag_sums[m] += n[k] * std::conj(n[k + m]);
            }
            LongRun run(kSpreadHz, kBinSamples, kBinsKept, kDelta);
            for (std::size_t k = 0; k < s.size(); ++k) {
                run.at_sample(static_cast<std::int64_t>(k));
                run.add(s[k], std::abs(a * s[k] + n[k]), 1.0);
            }
            const double x = run.omega_hat_fs2();
            sum += x;
            sum2 += x * x;
        }
        const double mean = sum / kTrials;
        const double se = std::sqrt((sum2 / kTrials - mean * mean) / kTrials);
        std::cout << std::setprecision(5) << db << " dB S500: mean Omega_hat " << mean << " sigma_y^2 against a^2 = "
                  << a2 << " sigma_y^2, standard error " << se << " sigma_y^2\n";
        EXPECT_NEAR(mean, a2, 3.0 * se) << db << " dB S500";
    }
    // The noise has the chain's correlation: within 0.01 of rho_m at m = 0 ... 6 and of 0 at m = 7 (2 x 10^6 products
    // per lag; their standard error is below 0.002).
    for (std::size_t m = 0; m < lag_sums.size(); ++m) {
        const std::complex<double> r = lag_sums[m] / (100.0 * 20000.0);
        EXPECT_NEAR(r.real(), m < 7 ? noise192().rho[m] : 0.0, 0.01) << "lag " << m;
        EXPECT_NEAR(r.imag(), 0.0, 0.01) << "lag " << m;
    }
}

TEST(LongRun, PhysicalFloor) {
    // With every e = 0, Omega_hat is its floor -sigma_y^2 sum s^2 / sum s^4 (+/-0.01 sigma_y^2): -2.09 after a 100 WPM dot
    // claimed as 4 samples with its tail and -1.05 after "PARIS" at 25 WPM (design part 7). "PARIS" at 100 WPM depends on
    // where its edges fall on the grid (a 12 ms dot is 4.5 samples, claimed as 4 or 5): -1.344 with the first dot
    // claimed as 4 samples (keyed from 0.5 s, 187.5 samples) and -1.288 as 5 (keyed from 187.75 samples), recomputed here;
    // the design's -1.30 (measured in scratch, its placement not recorded) lies between them and matches neither within
    // 0.01 (Task 7's report).
    struct Case {
        const char* name;
        std::vector<double> s;
        double floor;
    };
    const std::vector<Case> cases = {
        {"a 100 WPM dot claimed as 4 samples", template_of_edges({0, 4}), -2.09},
        {"\"PARIS\" at 100 WPM, its first dot claimed as 4 samples", template_of_text("PARIS", 100.0, 0.5), -1.344},
        {"\"PARIS\" at 100 WPM, its first dot claimed as 5 samples",
         template_of_text("PARIS", 100.0, 0.5 + kDelta / 4.0), -1.288},
        {"\"PARIS\" at 25 WPM", template_of_text("PARIS", 25.0), -1.05}};
    for (const Case& c : cases) {
        const LongRun run = run_template(c.s);
        const double floor = -run.sum_s2() / run.sum_s4();
        std::cout << c.name << ": floor " << std::setprecision(4) << floor << " sigma_y^2\n";
        EXPECT_NEAR(floor, c.floor, 0.01) << c.name;
        EXPECT_NEAR(run.omega_hat_fs2(), floor, 1e-12) << c.name;
    }
}

TEST(LongRun, BinnedNuMatchesTheExactSum) {
    // "PARIS" once, twice and four times at 8, 25 and 100 WPM, weights s^4 of the template, after the last tail: the
    // binned nu within 2.5% of the exact sum over samples (design part 15, item 1). "PARIS" at 25 WPM (2.064 s keyed,
    // 2.107 s to the end of its tail): nu = 6.90 +/- 0.05 binned (6.85 exact), recomputed here; the design's 6.55
    // (measured in scratch on "1.94 s", its placement not recorded) is not reproduced (Task 7's report).
    for (const double wpm : {8.0, 25.0, 100.0}) {
        for (const char* text : {"PARIS", "PARIS PARIS", "PARIS PARIS PARIS PARIS"}) {
            const std::vector<double> s = template_of_text(text, wpm);
            std::vector<double> w;
            for (const double x : s) w.push_back(x * x * x * x);
            const double binned = run_template(s).nu();
            const double exact = exact_nu(w);
            std::cout << std::setw(24) << text << " at " << wpm << " WPM: nu binned " << std::setprecision(4) << binned
                      << ", exact " << exact << " (" << std::showpos << 100.0 * (binned / exact - 1.0)
                      << std::noshowpos << "%)\n";
            EXPECT_LE(std::abs(binned / exact - 1.0), 0.025) << text << " at " << wpm << " WPM";
            if (wpm == 25.0 && std::string(text) == "PARIS") EXPECT_NEAR(binned, 6.90, 0.05);
        }
    }
}

TEST(LongRun, NuOnKeyDownTimes) {
    // Contiguous samples with s = 1 from sample 0: nu = 2.17 after 0.5 s and 7.42 after 2 s at 2 Hz (the exact sample sum;
    // the binning adds up to 0.6%): the exact sum within 0.5% (the values' rounding) and the binned within 0.7%.
    for (const auto& [seconds, expected] : {std::pair{0.5, 2.17}, std::pair{2.0, 7.42}}) {
        const auto n = static_cast<std::size_t>(std::ceil(seconds / kDelta));  // the samples with t_k < seconds
        const std::vector<double> s(n, 1.0);
        const double exact = exact_nu(s);
        const double binned = run_template(s).nu();
        std::cout << "s = 1 for " << seconds << " s (" << n << " samples): nu exact " << std::setprecision(4) << exact
                  << ", binned " << binned << "\n";
        EXPECT_NEAR(exact, expected, 0.005 * expected) << seconds << " s";
        EXPECT_NEAR(binned, expected, 0.007 * expected) << seconds << " s";
    }
    // s = 1 on the key-down samples of "PARIS PARIS" at 25 WPM (from each claimed key-down to the sample before its claimed
    // key-up), 0 elsewhere, counted 0.5 s and 2 s from the first key-down: reported beside the design's 2.10 and 6.72
    // (derived in scratch on key-down times not in the repository) and within 3% of them. Keeping 10 bins changes nu by
    // under 0.01% against keeping every bin.
    const std::vector<std::int64_t> edges = claimed_edges(test::keying("PARIS PARIS", 25.0, 0.5));
    for (const auto& [seconds, expected] : {std::pair{0.5, 2.10}, std::pair{2.0, 6.72}}) {
        const auto n = static_cast<std::int64_t>(std::ceil(seconds / kDelta));
        std::vector<double> s(static_cast<std::size_t>(n), 0.0);
        for (std::size_t e = 0; e + 1 < edges.size(); e += 2) {
            for (std::int64_t k = edges[e]; k < edges[e + 1]; ++k) {
                if (k - edges[0] < n) s[static_cast<std::size_t>(k - edges[0])] = 1.0;
            }
        }
        const double binned = run_template(s, kBinsKept, edges[0]).nu();
        const double every = run_template(s, 1000000, edges[0]).nu();
        const double exact = exact_nu(s);
        std::cout << "key-down samples of \"PARIS PARIS\" at 25 WPM, " << seconds << " s: nu binned "
                  << std::setprecision(4) << binned << ", exact " << exact << ", the design's " << expected
                  << "; every bin kept: " << std::setprecision(8) << every << "\n";
        EXPECT_NEAR(binned, expected, 0.03 * expected) << seconds << " s";
        EXPECT_LE(std::abs(binned / every - 1.0), 1e-4) << seconds << " s";
    }
}

TEST(LongRun, AnHourOfKeyDownStaysExact) {
    // One hour of contiguous samples with s = 1 (1 350 000 samples, 84 375 bins of 16): Omega_hat of a constant e^2 is
    // e^2 - sigma^2 within 1e-12 relative, and nu within 1e-9 relative of the closed sum
    // nu = N^2 / (N + 2 sum_{m=1..10} (N - m) rho^2(m b)), N the bins (Review focus 4).
    constexpr std::int64_t kSamples = 3600 * 375;
    constexpr double kE = 1.2345;     // FS
    constexpr double kSigma2 = 0.3;   // FS^2
    LongRun run(kSpreadHz, kBinSamples, kBinsKept, kDelta);
    for (std::int64_t k = 0; k < kSamples; ++k) {
        run.at_sample(k);
        run.add(1.0, kE, kSigma2);
    }
    const double expected_omega = kE * kE - kSigma2;
    EXPECT_NEAR(run.omega_hat_fs2(), expected_omega, 1e-12 * expected_omega);
    const long double bins = static_cast<long double>(kSamples / kBinSamples);
    long double pairs = bins;
    for (int m = 1; m <= kBinsKept; ++m) {
        const long double tau = static_cast<long double>(m) * kBinSamples * kDelta;
        const long double r2 = std::exp(-static_cast<long double>(std::numbers::pi * std::numbers::pi) * kSpreadHz *
                                        kSpreadHz * tau * tau);
        pairs += 2.0L * (bins - m) * r2;
    }
    const double expected_nu = static_cast<double>(bins * bins / pairs);
    std::cout << "an hour: nu " << std::setprecision(12) << run.nu() << " against " << expected_nu << "\n";
    EXPECT_NEAR(run.nu(), expected_nu, 1e-9 * expected_nu);
}

TEST(OmegaBar, ClosedFormMatchesADenseIntegral) {
    // The quadrature against a trapezoid rule on 2 000 001 points over the whole of [ln Omega_lo, ln Omega_hi], at
    // nu from 1 to 1 000 and Omega_hat across the range: within 1e-7 relative. At beta = 0 the closed form is exact
    // (header); a tiny beta reads it within 1e-9.
    const double lo_db = -15.0;
    const double hi_db = 85.0;
    const double sigma2 = 2.5e-7;  // FS^2, arbitrary
    const double hi = power_of_db(hi_db) * sigma2;
    const double l = (hi_db - lo_db) / 10.0 * std::numbers::ln10;
    const auto dense = [&](double nu, double omega_hat) {
        const double b = nu * std::max(omega_hat, 0.0) / hi;
        constexpr int kPoints = 2000001;
        const double h = l / (kPoints - 1);
        double peak = -1e300;
        for (int i = 0; i < kPoints; ++i) {
            const double y = -l + i * h;
            peak = std::max(peak, -nu * y - b * std::exp(-y));
        }
        double num = 0.0;
        double den = 0.0;
        for (int i = 0; i < kPoints; ++i) {
            const double y = -l + i * h;
            const double f = std::exp(-nu * y - b * std::exp(-y) - peak) * (i == 0 || i == kPoints - 1 ? 0.5 : 1.0);
            den += f;
            num += f * std::exp(y);
        }
        return hi * num / den;
    };
    for (const double nu : {1.0, 1.01, 1.5, 3.0, 30.0, 1000.0}) {
        for (const double ratio : {1e-12, 1e-9, 0.1, 1.0, 1e4, 1e9, 3e9}) {
            const double omega_hat = ratio * sigma2;
            const double got = omega_bar_closed_form(nu, omega_hat, sigma2, lo_db, hi_db);
            const double want = dense(nu, omega_hat);
            EXPECT_NEAR(got, want, 1e-7 * want) << "nu " << nu << ", Omega_hat " << ratio << " sigma_y^2";
        }
        const double at_zero = omega_bar_closed_form(nu, 0.0, sigma2, lo_db, hi_db);
        EXPECT_NEAR(omega_bar_closed_form(nu, 1e-30 * sigma2, sigma2, lo_db, hi_db), at_zero, 1e-9 * at_zero) << nu;
    }
    EXPECT_THROW(omega_bar_closed_form(0.0, 1.0, 1.0, lo_db, hi_db), std::invalid_argument);
    EXPECT_THROW(omega_bar_closed_form(2.0, 1.0, 0.0, lo_db, hi_db), std::invalid_argument);
    EXPECT_THROW(omega_bar_closed_form(2.0, std::nan(""), 1.0, lo_db, hi_db), std::invalid_argument);
    EXPECT_THROW(omega_bar_closed_form(2.0, 1.0, 1.0, hi_db, lo_db), std::invalid_argument);
}

TEST(OmegaBar, KnownValues) {
    // Design part 7 (D16): at Omega_hat = 10^3 sigma_y^2, Omega_bar / Omega_hat = 3.00, 2.00 and 1.25 at nu = 1.5, 2 and 5
    // within 0.5%; at nu = 1, 14.4 at +20 dB S500 and 12.1 at +30 dB S500 within +/-0.1. At Omega_hat <= 0 (beta = 0):
    // Omega_lo nu / (nu - 1) at nu = 2 and 5, 23.0 Omega_lo at nu = 1, the quadrature's exact value at nu = 1.01 (about
    // 21 Omega_lo), each within 0.1%; a negative Omega_hat gives Omega_hat = 0's value. Each for the closed form and the
    // compiled table.
    const OmegaBarTable table = OmegaBarTable::compiled();
    const double sigma2 = 4e-9;  // FS^2, arbitrary
    const double lo = power_of_db(-15.0) * sigma2;
    using Form = std::function<double(double, double)>;
    const std::vector<std::pair<const char*, Form>> forms = {
        {"closed form", [&](double nu, double oh) { return omega_bar_closed_form(nu, oh, sigma2, -15.0, 85.0); }},
        {"table", [&](double nu, double oh) { return table.omega_bar_fs2(nu, oh, sigma2); }}};
    for (const auto& [name, f] : forms) {
        SCOPED_TRACE(name);
        const double oh = 1e3 * sigma2;
        EXPECT_NEAR(f(1.5, oh) / oh, 3.00, 0.005 * 3.00);
        EXPECT_NEAR(f(2.0, oh) / oh, 2.00, 0.005 * 2.00);
        EXPECT_NEAR(f(5.0, oh) / oh, 1.25, 0.005 * 1.25);
        const double at20 = power_of_db(20.0) * sigma2;
        const double at30 = power_of_db(30.0) * sigma2;
        EXPECT_NEAR(f(1.0, at20) / at20, 14.4, 0.1);
        EXPECT_NEAR(f(1.0, at30) / at30, 12.1, 0.1);
        EXPECT_NEAR(f(2.0, 0.0) / lo, 2.0, 0.001 * 2.0);
        EXPECT_NEAR(f(5.0, 0.0) / lo, 1.25, 0.001 * 1.25);
        EXPECT_NEAR(f(1.0, 0.0) / lo, 23.03, 0.001 * 23.03);
        // nu = 1.01: E(0.01) / E(1.01) with l = ln 10^10 (header), 20.77 (about 21, not 101).
        const double l = 10.0 * std::numbers::ln10;
        const double exact = (-std::expm1(-0.01 * l) / 0.01) / (-std::expm1(-1.01 * l) / 1.01);
        EXPECT_NEAR(f(1.01, 0.0) / lo, exact, 0.001 * exact);
        EXPECT_NEAR(exact, 20.77, 0.01);
        EXPECT_EQ(f(2.0, -3.0 * sigma2), f(2.0, 0.0));
        EXPECT_EQ(f(1.0, -1e9 * sigma2), f(1.0, 0.0));
    }
}

TEST(OmegaBar, TableMatchesTheClosedForm) {
    // 10 000 random points, nu log-uniform on [1, 1 000], Omega_hat / sigma_y^2 uniform in asinh on [-5, 10^9]: the
    // compiled table within 0.1% of the closed form (plan O8).
    const OmegaBarTable table = OmegaBarTable::compiled();
    EXPECT_EQ(table.lo_db(), -15.0);
    EXPECT_EQ(table.hi_db(), 85.0);
    std::mt19937_64 rng(31);
    std::uniform_real_distribution<double> log_nu(0.0, std::log(1000.0));
    std::uniform_real_distribution<double> asinh_ratio(std::asinh(-5.0), std::asinh(1e9));
    const double sigma2 = 3e-8;  // FS^2, arbitrary
    double worst = 0.0;
    double worst_nu = 0.0;
    double worst_ratio = 0.0;
    for (int i = 0; i < 10000; ++i) {
        const double nu = std::exp(log_nu(rng));
        const double ratio = std::sinh(asinh_ratio(rng));
        const double want = omega_bar_closed_form(nu, ratio * sigma2, sigma2, -15.0, 85.0);
        const double got = table.omega_bar_fs2(nu, ratio * sigma2, sigma2);
        const double error = rel(got, want);
        if (error > worst) {
            worst = error;
            worst_nu = nu;
            worst_ratio = ratio;
        }
    }
    std::cout << "largest relative error " << std::setprecision(3) << worst << " at nu = " << worst_nu
              << ", Omega_hat = " << worst_ratio << " sigma_y^2\n";
    EXPECT_LE(worst, 1e-3);
    EXPECT_THROW(table.omega_bar_fs2(2.0, 1.0, 0.0), std::invalid_argument);
    // nu below 1 or not a number reads as 1; nu = infinity is Omega_hat clipped to the prior's range.
    EXPECT_EQ(table.omega_bar_fs2(0.5, 10.0 * sigma2, sigma2), table.omega_bar_fs2(1.0, 10.0 * sigma2, sigma2));
    EXPECT_EQ(table.omega_bar_fs2(std::nan(""), 10.0 * sigma2, sigma2),
              table.omega_bar_fs2(1.0, 10.0 * sigma2, sigma2));
    const double inf = std::numeric_limits<double>::infinity();
    EXPECT_NEAR(table.omega_bar_fs2(inf, 10.0 * sigma2, sigma2), 10.0 * sigma2, 1e-6 * 10.0 * sigma2);
    EXPECT_NEAR(table.omega_bar_fs2(inf, 0.0, sigma2), power_of_db(-15.0) * sigma2, 1e-6 * power_of_db(-15.0) * sigma2);
}

TEST(OmegaBar, BoundsAreChecked) {
    // A configuration at -5 and +60 dB S500 is refused without a matching table, naming both pairs of bounds; with a table
    // generated for them, Omega_bar matches the closed form at those bounds within 0.1% (Ruling 72, N14).
    ViterbiConfig config;
    config.level_min_db = -5.0;
    config.level_max_db = 60.0;
    std::string message;
    try {
        (void)make_viterbi_shared(config, chain192());
    } catch (const std::invalid_argument& e) {
        message = e.what();
    }
    std::cout << "refused: " << message << "\n";
    for (const char* bound : {"-5", "60", "-15", "85"})
        EXPECT_NE(message.find(bound), std::string::npos) << bound << " in: " << message;
    EXPECT_THROW((void)omega_bar_default_table(-5.0, 60.0, ""), std::invalid_argument);
    EXPECT_NO_THROW((void)omega_bar_default_table(-15.0, 85.0, ""));
    // The default configuration's shared parts hold the compiled table.
    const auto defaults = make_viterbi_shared(ViterbiConfig{}, chain192());
    EXPECT_EQ(defaults->omega_bar().lo_db(), -15.0);
    EXPECT_EQ(defaults->omega_bar().hi_db(), 85.0);
    EXPECT_EQ(defaults->omega_bar().values(), OmegaBarTable::compiled().values());

    const OmegaBarTable built = OmegaBarTable::build(-5.0, 60.0, OmegaBarLayout{});
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "kz4ap_omega_bar_bounds_test.bin";
    built.save(path.string());
    const OmegaBarTable loaded = OmegaBarTable::load(path.string());
    EXPECT_EQ(loaded.values(), built.values());
    EXPECT_EQ(loaded.zero_row(), built.zero_row());
    EXPECT_EQ(loaded.layout(), built.layout());
    // The table that make_viterbi_shared takes for those bounds from that file. (make_viterbi_shared itself passes its
    // Omega_bar check with the file; until Task 10 removes the block form, its flat prior refuses +60 dB S500 for a
    // reason of its own, b at level_max_db below 50, the only refusal left.)
    const OmegaBarTable table = omega_bar_default_table(-5.0, 60.0, path.string());
    std::string after;
    try {
        (void)make_viterbi_shared(config, chain192(), path.string());
    } catch (const std::invalid_argument& e) {
        after = e.what();
    }
    EXPECT_EQ(after.find("Omega_bar"), std::string::npos) << after;
    // The default bounds with that table: refused, naming both pairs of bounds.
    std::string mismatch;
    try {
        (void)omega_bar_default_table(-15.0, 85.0, path.string());
    } catch (const std::invalid_argument& e) {
        mismatch = e.what();
    }
    for (const char* bound : {"-5", "60", "-15", "85"})
        EXPECT_NE(mismatch.find(bound), std::string::npos) << bound << " in: " << mismatch;
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
    EXPECT_EQ(table.lo_db(), -5.0);
    EXPECT_EQ(table.hi_db(), 60.0);
    std::mt19937_64 rng(5);
    std::uniform_real_distribution<double> log_nu(0.0, std::log(1000.0));
    std::uniform_real_distribution<double> asinh_ratio(std::asinh(-5.0), std::asinh(1e7));
    const double sigma2 = 1e-6;
    double worst = 0.0;
    for (int i = 0; i < 2000; ++i) {
        const double nu = std::exp(log_nu(rng));
        const double oh = std::sinh(asinh_ratio(rng)) * sigma2;
        const double want = omega_bar_closed_form(nu, oh, sigma2, -5.0, 60.0);
        worst = std::max(worst, rel(table.omega_bar_fs2(nu, oh, sigma2), want));
    }
    std::cout << "the -5 to +60 dB S500 table: largest relative error " << std::setprecision(3) << worst << "\n";
    EXPECT_LE(worst, 1e-3);
    EXPECT_THROW((void)OmegaBarTable::load((std::filesystem::temp_directory_path() / "kz4ap_no_such_table.bin").string()),
                 std::runtime_error);
}

TEST(OmegaBar, NoiseBandwidthIsTheChains) {
    // kOmegaBarSigma2PerN0Hz is output_noise's sigma_y^2 / N0 on the 192 kHz chain within 1e-4 Hz, and the 48 kHz
    // chain's within 0.05% (derived, front_filter.hpp).
    EXPECT_NEAR(noise192().sigma2_per_n0_hz, kOmegaBarSigma2PerN0Hz, 1e-4);
    ChannelizerConfig config48;
    config48.sample_rate = 48000;
    config48.fft_size = 2048;
    const OutputNoise n48 = output_noise(channel_filter_taps(config48), config48.sample_rate,
                                         config48.fft_size / config48.channel_bins,
                                         front_filter_taps(1500.0, 0.012, 0.0025), 4);
    EXPECT_NEAR(n48.sigma2_per_n0_hz, kOmegaBarSigma2PerN0Hz, 5e-4 * kOmegaBarSigma2PerN0Hz);
}
