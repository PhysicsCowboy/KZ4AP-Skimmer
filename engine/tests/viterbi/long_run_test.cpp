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
#include <chrono>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numbers>
#include <random>
#include <sstream>
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

// dB S500 of a power in units of sigma_y^2 (the inverse of power_of_db).
double db_of_power(double ratio) {
    return 10.0 * std::log10(ratio * kOmegaBarSigma2PerN0Hz / kS500BandwidthHz);
}

// The 192 kHz chain's output-noise correlation rho_m at lags m = 0 ... lags of Delta, by output_noise's formula
// (front_filter.hpp, stage 3) carried past lag 6.
std::vector<double> chain_rho(int lags) {
    const ChannelizerConfig config = chain192();
    const std::vector<double> g = channel_filter_taps(config);
    const std::vector<double> h = front_filter_taps(1500.0, 0.012, 0.0025);
    const std::size_t step = static_cast<std::size_t>(config.fft_size / config.channel_bins);
    const std::size_t max_lag = static_cast<std::size_t>(lags) * 4 + h.size() - 1;
    std::vector<double> r_channel(max_lag + 1, 0.0);
    for (std::size_t d = 0; d <= max_lag; ++d) {
        double acc = 0.0;
        for (std::size_t n = 0; n + d * step < g.size(); ++n) acc += g[n] * g[n + d * step];
        r_channel[d] = acc;
    }
    std::vector<double> r(static_cast<std::size_t>(lags) + 1, 0.0);
    for (std::size_t m = 0; m < r.size(); ++m) {
        double acc = 0.0;
        for (std::size_t a = 0; a < h.size(); ++a) {
            for (std::size_t b = 0; b < h.size(); ++b) {
                const long long d = static_cast<long long>(m * 4 + a) - static_cast<long long>(b);
                acc += h[a] * h[b] * r_channel[static_cast<std::size_t>(std::llabs(d))];
            }
        }
        r[m] = acc;
    }
    for (std::size_t m = r.size(); m-- > 0;) r[m] /= r[0];
    return r;
}

// a2 = sum_j sum_l s_j^2 s_l^2 rho_|j-l|^2 and b2 = sum_j sum_l s_j^3 s_l^3 rho_|j-l| over the lags rho holds.
std::pair<double, double> double_sums(const std::vector<double>& s, const std::vector<double>& rho) {
    long double a2 = 0.0L;
    long double b2 = 0.0L;
    for (std::size_t j = 0; j < s.size(); ++j) {
        for (std::size_t l = 0; l < s.size(); ++l) {
            const std::size_t m = j > l ? j - l : l - j;
            if (m >= rho.size()) continue;
            const long double sj = s[j];
            const long double sl = s[l];
            a2 += sj * sj * sl * sl * rho[m] * rho[m];
            b2 += sj * sj * sj * sl * sl * sl * rho[m];
        }
    }
    return {static_cast<double>(a2), static_cast<double>(b2)};
}

// One story's sums over a template: nu, sum s^4 (LongRun) and a2, b2 (NoiseVarianceSums, the 192 kHz chain's rho).
struct StorySums {
    double nu;
    double sum_s4;
    double a2;
    double b2;
    double zeta() const { return std::sqrt(a2) / sum_s4; }                 // SE0 / sigma_y^2
    double lambda() const { return 2.0 * b2 / (sum_s4 * std::sqrt(a2)); }  // B / SE0
};

StorySums story_sums(const std::vector<double>& s) {
    const LongRun run = run_template(s);
    NoiseVarianceSums sums;
    for (const double x : s) sums.add(x, noise192().rho);
    return {run.nu(), run.sum_s4(), sums.a2(), sums.b2()};
}

// Complex Gaussian Rayleigh fading of unit power with a Gaussian Doppler spectrum: the gain's correlation
// exp(-pi^2 f_D^2 tau^2 / 2) (amplitude.hpp), by spectral synthesis on a 2 048-sample circle (5.46 s, over twice the
// longest template here, 2.1 s; the correlation across the wrap is below e^-200): alpha_k = sum_i sqrt(S_i) w_i
// e^(j 2 pi f_i k Delta), f_i = i / (2 048 Delta) within 4.5 sigma_f, S_i proportional to exp(-f_i^2 / (2 sigma_f^2))
// and summing to 1, sigma_f = f_D / 2, w_i complex Gaussian of unit power.
class Fading {
public:
    Fading(double spread_hz, std::size_t n) : n_(n) {
        const double df = 1.0 / (2048.0 * kDelta);
        const double sigma = spread_hz / 2.0;
        const int bins = static_cast<int>(std::ceil(4.5 * sigma / df));
        double total = 0.0;
        for (int i = -bins; i <= bins; ++i) {
            const double f = i * df;
            const double weight = std::exp(-f * f / (2.0 * sigma * sigma));
            amplitude_.push_back(weight);
            rotation_.push_back(std::polar(1.0, 2.0 * std::numbers::pi * f * kDelta));
            total += weight;
        }
        for (double& a : amplitude_) a = std::sqrt(a / total);
    }
    void draw(std::mt19937_64& rng, std::vector<std::complex<double>>& out) {
        std::normal_distribution<double> gauss(0.0, std::sqrt(0.5));
        out.assign(n_, 0.0);
        for (std::size_t i = 0; i < amplitude_.size(); ++i) {
            std::complex<double> z = amplitude_[i] * std::complex<double>(gauss(rng), gauss(rng));
            for (std::size_t k = 0; k < n_; ++k) {
                out[k] += z;
                z *= rotation_[i];
            }
        }
    }

private:
    std::size_t n_;
    std::vector<double> amplitude_;
    std::vector<std::complex<double>> rotation_;
};

// A path in the system's temporary directory that no other run shares (the stem, the process's clock and a random
// 64-bit suffix, in hexadecimal), whose file is removed when the object goes out of scope.
class TemporaryFile {
public:
    explicit TemporaryFile(const std::string& stem) {
        std::random_device device;
        const auto now = static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
        const std::uint64_t suffix = ((static_cast<std::uint64_t>(device()) << 32) | device()) ^ now;
        char hex[17];
        std::snprintf(hex, sizeof hex, "%016llx", static_cast<unsigned long long>(suffix));
        path_ = std::filesystem::temp_directory_path() / (stem + "_" + hex + ".bin");
    }
    TemporaryFile(const TemporaryFile&) = delete;
    TemporaryFile& operator=(const TemporaryFile&) = delete;
    ~TemporaryFile() {
        std::error_code ignored;
        std::filesystem::remove(path_, ignored);
    }
    const std::filesystem::path& path() const { return path_; }

private:
    std::filesystem::path path_;
};

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
    const TemporaryFile file("kz4ap_omega_bar_bounds_test");
    const std::filesystem::path& path = file.path();
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
    const TemporaryFile missing("kz4ap_no_such_table");  // never written
    EXPECT_THROW((void)OmegaBarTable::load(missing.path().string()), std::runtime_error);
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

TEST(NoiseVariance, RunningSumsAreTheDoubleSums) {
    // On a 100 WPM dot claimed as 4 samples, a 25 WPM dot and "PARIS" at 100 WPM (each with its tail): the running sums
    // equal the double sums over 6 lags within 1e-12 relative, and the double sums over all 11 lags (the chain's
    // correlation carried to lag 11) within 0.01% (design part 7).
    const std::vector<double> rho11 = chain_rho(11);
    for (std::size_t m = 0; m <= 6; ++m) EXPECT_NEAR(rho11[m], noise192().rho[m], 1e-12) << m;
    const std::vector<double> rho6(rho11.begin(), rho11.begin() + 7);
    struct Case {
        const char* name;
        std::vector<double> s;
    };
    const std::vector<Case> cases = {{"a 100 WPM dot claimed as 4 samples", template_of_edges({0, 4})},
                                     {"a 25 WPM dot", template_of_text("E", 25.0)},
                                     {"\"PARIS\" at 100 WPM", template_of_text("PARIS", 100.0)}};
    for (const Case& c : cases) {
        NoiseVarianceSums sums;
        for (const double x : c.s) sums.add(x, noise192().rho);
        const auto [a6, b6] = double_sums(c.s, rho6);
        const auto [a11, b11] = double_sums(c.s, rho11);
        std::cout << c.name << ": a2 = " << std::setprecision(8) << sums.a2() << " (11 lags " << a11 << ", "
                  << std::showpos << 100.0 * (a6 / a11 - 1.0) << "%), b2 = " << std::noshowpos << sums.b2()
                  << " (11 lags " << b11 << ", " << std::showpos << 100.0 * (b6 / b11 - 1.0) << std::noshowpos
                  << "%)\n";
        EXPECT_NEAR(sums.a2(), a6, 1e-12 * a6) << c.name;
        EXPECT_NEAR(sums.b2(), b6, 1e-12 * b6) << c.name;
        EXPECT_NEAR(a6, a11, 1e-4 * a11) << c.name;
        EXPECT_NEAR(b6, b11, 1e-4 * b11) << c.name;
    }
    // A restart forgets the sums and the history: the 4-sample mark after a restart gives the same sums as alone.
    NoiseVarianceSums sums;
    for (const double x : template_of_text("PARIS", 100.0)) sums.add(x, noise192().rho);
    sums.restart();
    EXPECT_EQ(sums.a2(), 0.0);
    EXPECT_EQ(sums.b2(), 0.0);
    NoiseVarianceSums alone;
    for (const double x : template_of_edges({0, 4})) {
        sums.add(x, noise192().rho);
        alone.add(x, noise192().rho);
    }
    EXPECT_EQ(sums.a2(), alone.a2());
    EXPECT_EQ(sums.b2(), alone.b2());
}

TEST(NoiseVariance, MatchesSimulation) {
    // Design part 15, item 1: 20 000 trials each on a 25 WPM dot and "PARIS" at 25 WPM, complex Gaussian noise with the
    // chain's rho_m (sigma_y^2 = 1), a steady carrier and 2 Hz Rayleigh fading, noise alone and -10, 0 and +10 dB S500:
    // N = Omega_hat - Omega G, the mean of N^2 / (A + B Omega G) in [0.96, 1.04] (A + B Omega G per trial, G the
    // realized gain sum s^4 |alpha|^2 / sum s^4; the design measured 0.981 to 1.010 on 60 000 trials).
    struct Case {
        const char* name;
        std::vector<double> s;
    };
    const std::vector<Case> cases = {{"a 25 WPM dot", template_of_text("E", 25.0)},
                                     {"\"PARIS\" at 25 WPM", template_of_text("PARIS", 25.0)}};
    constexpr int kTrials = 20000;
    const std::array<double, 3> levels = {-10.0, 0.0, 10.0};  // dB S500
    std::mt19937_64 rng(4242);
    for (const Case& c : cases) {
        const StorySums sums = story_sums(c.s);
        const double s4 = sums.sum_s4;
        const double a = sums.a2 / (s4 * s4);        // A, sigma_y^4 = 1
        const double b = 2.0 * sums.b2 / (s4 * s4);  // B, sigma_y^2 = 1
        CorrelatedNoise noise(noise192().rho, c.s.size());
        Fading fading(kSpreadHz, c.s.size());
        std::vector<std::complex<double>> n;
        std::vector<std::complex<double>> alpha;
        // Index 0: noise alone; 1 ... 3 steady; 4 ... 6 Rayleigh.
        std::array<double, 7> ratio_sum{};
        std::array<double, 7> n2_sum{};
        std::array<double, 7> variance_sum{};
        for (int trial = 0; trial < kTrials; ++trial) {
            noise.draw(rng, n);
            fading.draw(rng, alpha);
            double gain = 0.0;  // sum s^4 |alpha|^2 / sum s^4
            for (std::size_t k = 0; k < c.s.size(); ++k) gain += std::pow(c.s[k], 4) * std::norm(alpha[k]);
            gain /= s4;
            for (std::size_t v = 0; v < 7; ++v) {
                const bool faded = v >= 4;
                const double omega = v == 0 ? 0.0 : power_of_db(levels[(v - 1) % 3]);
                const double amplitude = std::sqrt(omega);
                double numerator = 0.0;
                for (std::size_t k = 0; k < c.s.size(); ++k) {
                    if (c.s[k] == 0.0) continue;
                    const std::complex<double> carrier = faded ? amplitude * alpha[k] : std::complex<double>(amplitude);
                    numerator += c.s[k] * c.s[k] * (std::norm(c.s[k] * carrier + n[k]) - 1.0);
                }
                const double realized = omega * (faded ? gain : 1.0);  // Omega G
                const double big_n = numerator / s4 - realized;
                const double variance = a + b * realized;
                ratio_sum[v] += big_n * big_n / variance;
                n2_sum[v] += big_n * big_n;
                variance_sum[v] += variance;
            }
        }
        std::cout << c.name << " (A = " << std::setprecision(4) << a << ", B = " << b << " sigma_y^2):\n";
        for (std::size_t v = 0; v < 7; ++v) {
            const std::string label = v == 0 ? std::string("noise alone")
                                             : (v >= 4 ? std::string("Rayleigh, ") : std::string("steady, ")) +
                                                   std::to_string(static_cast<int>(levels[(v - 1) % 3])) + " dB S500";
            const double ratio = ratio_sum[v] / kTrials;
            std::cout << "  " << label << ": E[N^2 / (A + B Omega G)] = " << std::setprecision(4) << ratio
                      << ", E[N^2] / E[A + B Omega G] = " << n2_sum[v] / variance_sum[v] << "\n";
            EXPECT_GE(ratio, 0.96) << c.name << ", " << label;
            EXPECT_LE(ratio, 1.04) << c.name << ", " << label;
        }
    }
}

TEST(OmegaBarVariant, DirectIntegralLimits) {
    // Design part 7: at chi = 100, 10^3 and 10^4 the direct integral's ratio to the default's closed form (Omega_hat =
    // chi zeta sigma_y^2) lies in [0.9986, 1.0000], [0.9998, 1.0000] and 1.0000 +/- 10^-4, at nu = 1, 2, 6.7 and 50 and
    // zeta = 0.02, 0.3 and 1.6, kappa = 2.6 (lambda = kappa zeta); "1.0000" read as at most 1.00005.
    for (const double nu : {1.0, 2.0, 6.7, 50.0}) {
        for (const double zeta : {0.02, 0.3, 1.6}) {
            const double lambda = 2.6 * zeta;
            for (const double chi : {100.0, 1e3, 1e4}) {
                const double direct = omega_bar_variant_direct(nu, chi, zeta, lambda);
                const double closed = omega_bar_closed_form(nu, chi * zeta, 1.0, -15.0, 85.0);
                const double ratio = direct / closed;
                std::cout << "nu " << nu << ", zeta " << zeta << ", chi " << chi << ": ratio " << std::setprecision(7)
                          << ratio << "\n";
                const double low = chi == 100.0 ? 0.9986 : (chi == 1e3 ? 0.9998 : 0.9999);
                EXPECT_GE(ratio, low) << "nu " << nu << ", zeta " << zeta << ", chi " << chi;
                EXPECT_LE(ratio, 1.00005) << "nu " << nu << ", zeta " << zeta << ", chi " << chi;
            }
        }
    }
    EXPECT_THROW((void)omega_bar_variant_direct(std::nan(""), 1.0, 1.0, 1.0), std::invalid_argument);
    EXPECT_THROW((void)omega_bar_variant_direct(2.0, std::nan(""), 1.0, 1.0), std::invalid_argument);
    EXPECT_THROW((void)omega_bar_variant_direct(2.0, 1.0, 0.0, 1.0), std::invalid_argument);
    EXPECT_THROW((void)omega_bar_variant_direct(2.0, 1.0, 1.0, -1.0), std::invalid_argument);
}

TEST(OmegaBarVariant, DesignValuesAtZero) {
    // Design part 7: at Omega_hat = 0 and kappa = 2.6, nu = 1 and zeta = 1.6, 0.3 and 0.02 give 6.5, 1.1 and
    // -1.2 dB S500, nu = 6.7 gives -4.9, -11.0 and -14.2 dB S500, each +/-0.1 dB.
    struct Case {
        double nu;
        double zeta;
        double db;
    };
    const std::vector<Case> cases = {{1.0, 1.6, 6.5},  {1.0, 0.3, 1.1},   {1.0, 0.02, -1.2},
                                     {6.7, 1.6, -4.9}, {6.7, 0.3, -11.0}, {6.7, 0.02, -14.2}};
    for (const Case& c : cases) {
        const double db = db_of_power(omega_bar_variant_direct(c.nu, 0.0, c.zeta, 2.6 * c.zeta));
        std::cout << "nu " << c.nu << ", zeta " << c.zeta << ": " << std::setprecision(4) << db << " dB S500 (design "
                  << c.db << ")\n";
        EXPECT_NEAR(db, c.db, 0.1) << "nu " << c.nu << ", zeta " << c.zeta;
    }
}

namespace {

// A multilinear function of the axes' interpolation coordinates (p, asinh chi, ln zeta, ln lambda), which multilinear
// interpolation reproduces exactly; and one of (p, ln zeta, ln lambda) for the side table.
double multilinear(double p, double x, double y, double z) {
    return 0.3 + 0.2 * p - 0.1 * x + 0.05 * y + 0.02 * z + 0.01 * p * x - 0.03 * y * z + 0.004 * p * x * y * z;
}
double side(double p, double y, double z) { return -2.0 + 0.5 * p + 0.1 * y - 0.05 * z + 0.02 * p * y * z; }

OmegaBarVariantTable small_table(const OmegaBarVariantTable::Axes& axes) {
    std::vector<float> values;
    std::vector<float> minimum;
    const auto coordinate = [](double lo, double hi, int nodes, int i) { return lo + (hi - lo) * i / (nodes - 1); };
    for (int i = 0; i < axes.upsilon_nodes; ++i) {
        const double p = static_cast<double>(i) / (axes.upsilon_nodes - 1);
        for (int j = 0; j < axes.chi_nodes; ++j) {
            const double x = coordinate(std::asinh(axes.chi_min), std::asinh(axes.chi_max), axes.chi_nodes, j);
            for (int k = 0; k < axes.zeta_nodes; ++k) {
                const double y = coordinate(std::log(axes.zeta_min), std::log(axes.zeta_max), axes.zeta_nodes, k);
                for (int m = 0; m < axes.lambda_nodes; ++m) {
                    const double z =
                        coordinate(std::log(axes.lambda_min), std::log(axes.lambda_max), axes.lambda_nodes, m);
                    values.push_back(static_cast<float>(multilinear(p, x, y, z)));
                }
            }
        }
        for (int k = 0; k < axes.zeta_nodes; ++k) {
            const double y = coordinate(std::log(axes.zeta_min), std::log(axes.zeta_max), axes.zeta_nodes, k);
            for (int m = 0; m < axes.lambda_nodes; ++m) {
                const double z = coordinate(std::log(axes.lambda_min), std::log(axes.lambda_max), axes.lambda_nodes, m);
                minimum.push_back(static_cast<float>(side(p, y, z)));
            }
        }
    }
    return OmegaBarVariantTable(axes, std::move(values), std::move(minimum));
}

}  // namespace

TEST(OmegaBarVariant, LookupInterpolates) {
    // A table of 3 nodes per axis holding a multilinear function of (p, asinh chi, ln zeta, ln lambda): the lookup
    // equals it within float storage's rounding (1e-6 relative in Omega_bar) at random points inside the axes, and the
    // side table likewise; the clamps and the fallback act as the header states.
    OmegaBarVariantTable::Axes axes;
    axes.upsilon_nodes = 3;
    axes.chi_nodes = 3;
    axes.zeta_nodes = 3;
    axes.lambda_nodes = 3;
    const OmegaBarVariantTable table = small_table(axes);
    const OmegaBarTable fallback = OmegaBarTable::compiled();
    const double sigma2 = 2e-6;  // FS^2, arbitrary
    const double s4 = 50.0;      // sum s^4, arbitrary
    // The story's sums that give zeta and lambda: zeta = sqrt(a2) / s4, lambda = 2 b2 / (s4 sqrt(a2)).
    const auto lookup = [&](double nu, double chi, double zeta, double lambda) {
        const double a2 = (zeta * s4) * (zeta * s4);
        const double b2 = lambda * s4 * std::sqrt(a2) / 2.0;
        return table.omega_bar_fs2(nu, chi * zeta * sigma2, a2, b2, s4, sigma2, fallback);
    };
    const auto expected = [&](double nu, double chi, double zeta, double lambda) {
        return sigma2 * std::exp(multilinear(OmegaBarVariantTable::p_of_nu(nu), std::asinh(chi), std::log(zeta),
                                             std::log(lambda)));
    };
    std::mt19937_64 rng(17);
    std::uniform_real_distribution<double> unit(0.0, 1.0);
    for (int i = 0; i < 2000; ++i) {
        const double nu = std::exp(unit(rng) * std::log(1e4));
        const double chi = std::sinh(std::asinh(-8.0) + unit(rng) * (std::asinh(1e4) - std::asinh(-8.0)));
        const double zeta = std::exp(std::log(0.003) + unit(rng) * (std::log(400.0) - std::log(0.003)));
        const double lambda = std::exp(std::log(1e-3) + unit(rng) * (std::log(1120.0) - std::log(1e-3)));
        const double want = expected(nu, chi, zeta, lambda);
        ASSERT_NEAR(lookup(nu, chi, zeta, lambda), want, 1e-6 * want)
            << "nu " << nu << ", chi " << chi << ", zeta " << zeta << ", lambda " << lambda;
        const double want_side = side(OmegaBarVariantTable::p_of_nu(nu), std::log(zeta), std::log(lambda));
        ASSERT_NEAR(table.minimum_chi(nu, zeta, lambda), want_side, 1e-6);
    }
    // The clamps: chi below -8 reads -8; lambda below 10^-3 (0 included) reads 10^-3; zeta outside its axis reads its
    // end; nu below 1 or not a number reads 1; nu = infinity is p = 0.
    const double inf = std::numeric_limits<double>::infinity();
    EXPECT_NEAR(lookup(3.0, -20.0, 0.5, 2.0), expected(3.0, -8.0, 0.5, 2.0), 1e-6 * expected(3.0, -8.0, 0.5, 2.0));
    for (const double small : {0.0, 1e-5})
        EXPECT_NEAR(lookup(3.0, 5.0, 0.5, small), expected(3.0, 5.0, 0.5, 1e-3), 1e-6 * expected(3.0, 5.0, 0.5, 1e-3));
    EXPECT_NEAR(lookup(3.0, 5.0, 1e-4, 2.0), expected(3.0, 5.0, 0.003, 2.0), 1e-6 * expected(3.0, 5.0, 0.003, 2.0));
    EXPECT_NEAR(lookup(3.0, 5.0, 1e3, 2.0), expected(3.0, 5.0, 400.0, 2.0), 1e-6 * expected(3.0, 5.0, 400.0, 2.0));
    EXPECT_NEAR(table.minimum_chi(3.0, 1e3, 0.0),
                side(OmegaBarVariantTable::p_of_nu(3.0), std::log(400.0), std::log(1e-3)), 1e-6);
    EXPECT_EQ(lookup(0.5, 5.0, 0.5, 2.0), lookup(1.0, 5.0, 0.5, 2.0));
    EXPECT_EQ(lookup(std::nan(""), 5.0, 0.5, 2.0), lookup(1.0, 5.0, 0.5, 2.0));
    EXPECT_NEAR(lookup(inf, 5.0, 0.5, 2.0), expected(inf, 5.0, 0.5, 2.0), 1e-6 * expected(inf, 5.0, 0.5, 2.0));
    EXPECT_EQ(OmegaBarVariantTable::p_of_nu(inf), 0.0);
    EXPECT_EQ(OmegaBarVariantTable::p_of_nu(1.0), 1.0);
    // chi above 10^4 reads the default's table; so does a story with no sample yet (a2 = 0 or sum s^4 = 0).
    EXPECT_EQ(lookup(3.0, 2e4, 0.5, 2.0), fallback.omega_bar_fs2(3.0, 2e4 * 0.5 * sigma2, sigma2));
    EXPECT_EQ(table.omega_bar_fs2(3.0, 0.0, 0.0, 0.0, 0.0, sigma2, fallback), fallback.omega_bar_fs2(3.0, 0.0, sigma2));
    // What it refuses: sigma_y^2 not positive, a fallback for other bounds, values of the wrong count, bad axes.
    EXPECT_THROW((void)table.omega_bar_fs2(3.0, 1.0, 1.0, 1.0, 1.0, 0.0, fallback), std::invalid_argument);
    const OmegaBarTable other(-5.0, 60.0, OmegaBarLayout{}, {}, {});
    EXPECT_THROW((void)table.omega_bar_fs2(3.0, 1.0, 1.0, 1.0, 1.0, sigma2, other), std::invalid_argument);
    EXPECT_THROW(OmegaBarVariantTable(axes, std::vector<float>(80, 0.0f), std::vector<float>(27, 0.0f)),
                 std::invalid_argument);
    EXPECT_THROW(OmegaBarVariantTable(axes, std::vector<float>(81, 0.0f), std::vector<float>(26, 0.0f)),
                 std::invalid_argument);
    OmegaBarVariantTable::Axes bad = axes;
    bad.zeta_min = 0.0;
    EXPECT_THROW(OmegaBarVariantTable(bad, std::vector<float>(81, 0.0f), std::vector<float>(27, 0.0f)),
                 std::invalid_argument);
}

TEST(OmegaBarVariant, LoadChecksTheHeader) {
    // A table on this build's axes reads back as written; one on other axes, or a file that is not such a table, is
    // refused with std::runtime_error naming the reason.
    const OmegaBarVariantTable::Axes axes;
    const std::size_t count = static_cast<std::size_t>(axes.upsilon_nodes) * axes.chi_nodes * axes.zeta_nodes *
                              axes.lambda_nodes;
    const std::size_t side_count = static_cast<std::size_t>(axes.upsilon_nodes) * axes.zeta_nodes * axes.lambda_nodes;
    EXPECT_EQ(count, 9462285u);  // 31 x 85 x 63 x 57
    std::vector<float> values(count);
    for (std::size_t i = 0; i < count; ++i) values[i] = static_cast<float>(std::sin(0.001 * static_cast<double>(i)));
    std::vector<float> minimum(side_count);
    for (std::size_t i = 0; i < side_count; ++i) minimum[i] = -1.0f - static_cast<float>(i % 7) / 4.0f;
    const OmegaBarVariantTable table(axes, values, minimum);
    const TemporaryFile file("kz4ap_omega_bar_variant_test");
    table.save(file.path().string());
    const OmegaBarVariantTable loaded = OmegaBarVariantTable::load(file.path().string());
    EXPECT_EQ(loaded.values(), values);
    EXPECT_EQ(loaded.minimum_chi_values(), minimum);
    EXPECT_EQ(loaded.axes(), axes);

    OmegaBarVariantTable::Axes other;
    other.upsilon_nodes = 3;
    other.chi_nodes = 3;
    other.zeta_nodes = 3;
    other.lambda_nodes = 3;
    const TemporaryFile small("kz4ap_omega_bar_variant_small_test");
    small_table(other).save(small.path().string());
    std::string message;
    try {
        (void)OmegaBarVariantTable::load(small.path().string());
    } catch (const std::runtime_error& e) {
        message = e.what();
    }
    std::cout << "refused: " << message << "\n";
    EXPECT_NE(message.find("axes"), std::string::npos) << message;
    const TemporaryFile missing("kz4ap_no_such_variant_table");  // never written
    EXPECT_THROW((void)OmegaBarVariantTable::load(missing.path().string()), std::runtime_error);
    const TemporaryFile wrong("kz4ap_not_a_variant_table");
    OmegaBarTable::compiled().save(wrong.path().string());
    EXPECT_THROW((void)OmegaBarVariantTable::load(wrong.path().string()), std::runtime_error);
}

TEST(OmegaBarVariant, TableMatchesTheDirectIntegral) {
    // Labeled needs-table: ctest's OmegaBarVariant.NeedsTable sets KZ4AP_OMEGA_BAR_VARIANT_TABLE to
    // build/viterbi-tables/omega-bar-variant-v1.bin; skipped when the file is absent. 400 points drawn from the design's
    // two halves (part 7): zeta log-uniform on [0.003, 4] with kappa uniform on [0.4, 2.8] and nu log-uniform on
    // [1, 1 000]; first marks, zeta log-uniform on [4, 400] with kappa uniform on [0, 1.2] (0 on a tenth of them) and nu
    // log-uniform on [1, 3]; chi uniform in asinh(chi) from max(-8, -max(2.1 / zeta, 1.3)) (Omega_hat's floor) to 30 on
    // half of each half and to 10^4 on the other: the 99th percentile of |relative error| at most 1%, the largest at
    // most 3% (design: 0.87% and 2.5% on 4 000 points). minimum_chi on part 7's cases (below).
    const char* path = std::getenv("KZ4AP_OMEGA_BAR_VARIANT_TABLE");
    if (path == nullptr || *path == '\0')
        GTEST_SKIP() << "runs as OmegaBarVariant.NeedsTable (label needs-table), which names the table";
    if (!std::filesystem::exists(path))
        GTEST_SKIP() << "the table " << path << " is absent; generate it with kz4ap-viterbi-tables omega-bar-variant "
                     << "--out build/viterbi-tables/omega-bar-variant-v1.bin --threads 7";
    const OmegaBarVariantTable table = OmegaBarVariantTable::load(path);
    const OmegaBarTable fallback = OmegaBarTable::compiled();
    std::mt19937_64 rng(2610);
    std::uniform_real_distribution<double> unit(0.0, 1.0);
    const auto log_uniform = [&](double lo, double hi) {
        return std::exp(std::log(lo) + unit(rng) * std::log(hi / lo));
    };
    std::vector<double> errors;
    double worst = 0.0;
    std::string worst_at;
    const double sigma2 = 1.0;  // FS^2: the relative error does not depend on it
    for (int i = 0; i < 400; ++i) {
        const bool first_marks = i >= 200;
        const double zeta = first_marks ? log_uniform(4.0, 400.0) : log_uniform(0.003, 4.0);
        const double kappa = first_marks ? (i % 10 == 0 ? 0.0 : 1.2 * unit(rng)) : 0.4 + 2.4 * unit(rng);
        const double nu = first_marks ? log_uniform(1.0, 3.0) : log_uniform(1.0, 1000.0);
        const double chi_lo = std::max(-8.0, -std::max(2.1 / zeta, 1.3));
        const double chi_hi = i % 2 == 0 ? 30.0 : 1e4;
        const double chi = std::sinh(std::asinh(chi_lo) + unit(rng) * (std::asinh(chi_hi) - std::asinh(chi_lo)));
        const double lambda = kappa * zeta;
        const double want = omega_bar_variant_direct(nu, chi, zeta, lambda);
        const double s4 = 10.0;  // arbitrary: zeta and lambda fix a2 and b2
        const double a2 = (zeta * s4) * (zeta * s4);
        const double b2 = lambda * s4 * std::sqrt(a2) / 2.0;
        const double got = table.omega_bar_fs2(nu, chi * zeta * sigma2, a2, b2, s4, sigma2, fallback);
        const double error = std::abs(got / want - 1.0);
        errors.push_back(error);
        if (error > worst) {
            worst = error;
            std::ostringstream at;
            at << "nu " << nu << ", chi " << chi << ", zeta " << zeta << ", kappa " << kappa;
            worst_at = at.str();
        }
    }
    std::sort(errors.begin(), errors.end());
    const double p99 = errors[static_cast<std::size_t>(0.99 * static_cast<double>(errors.size() - 1))];
    std::cout << "relative error: median " << std::setprecision(3) << 100.0 * errors[errors.size() / 2]
              << "%, 99th percentile " << 100.0 * p99 << "%, largest " << 100.0 * worst << "% (at " << worst_at << ")\n";
    EXPECT_LE(p99, 0.01);
    EXPECT_LE(worst, 0.03);
    // Part 7's cases: the chi where Omega_bar stops falling, from the side table, within 0.05 of the direct integral's
    // own minimum (golden-section search on [-8, 0]) and between -3.7 and -0.5 (design part 7). "PARIS" at 25 WPM is the
    // exception to the range: the design's -3.7 was measured on a shorter keying of it (nu = 6.55; the decided keying
    // gives 6.90, design part 7, corrected 2026-10-27 and not rerun there), and with larger sums the minimum lies lower
    // (design part 7); on the decided keying the direct integral's minimum is -3.74 (Task 8's report), so that case is
    // checked against the direct integral alone and its value reported.
    struct Case {
        const char* name;
        std::vector<double> s;
        bool design_keying;
    };
    const std::vector<Case> cases = {{"a 100 WPM dot claimed as 4 samples", template_of_edges({0, 4}), true},
                                     {"a 25 WPM dot", template_of_text("E", 25.0), true},
                                     {"\"E E\" at 25 WPM", template_of_text("E E", 25.0), true},
                                     {"\"PARIS\" at 100 WPM", template_of_text("PARIS", 100.0), true},
                                     {"\"PARIS\" at 25 WPM", template_of_text("PARIS", 25.0), false}};
    for (const Case& c : cases) {
        const StorySums sums = story_sums(c.s);
        const double minimum = table.minimum_chi(sums.nu, sums.zeta(), sums.lambda());
        const auto f = [&](double chi) { return omega_bar_variant_direct(sums.nu, chi, sums.zeta(), sums.lambda()); };
        double a = -8.0;
        double b = 0.0;
        const double golden = (std::sqrt(5.0) - 1.0) / 2.0;
        double x1 = b - golden * (b - a);
        double x2 = a + golden * (b - a);
        double f1 = f(x1);
        double f2 = f(x2);
        for (int iteration = 0; iteration < 40; ++iteration) {
            if (f1 <= f2) {
                b = x2;
                x2 = x1;
                f2 = f1;
                x1 = b - golden * (b - a);
                f1 = f(x1);
            } else {
                a = x1;
                x1 = x2;
                f1 = f2;
                x2 = a + golden * (b - a);
                f2 = f(x2);
            }
        }
        const double direct = 0.5 * (a + b);
        std::cout << c.name << ": nu " << std::setprecision(4) << sums.nu << ", zeta " << sums.zeta() << ", lambda "
                  << sums.lambda() << ", kappa " << sums.lambda() / sums.zeta() << "; minimum_chi " << minimum
                  << " (the direct integral's " << direct << ")\n";
        EXPECT_NEAR(minimum, direct, 0.05) << c.name;
        if (c.design_keying) {
            EXPECT_GE(minimum, -3.7) << c.name;
            EXPECT_LE(minimum, -0.5) << c.name;
        }
    }
}
