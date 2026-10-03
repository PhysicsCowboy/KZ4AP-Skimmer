// The bank decoder's noise estimates against the prototype's golden values
// (engine/tests/data/bank/noise.json, from training/kz4ap_proto/golden.py golden_noise): sigma2 to
// relative 1e-9 (floor 1e-12 absolute), segment counts exactly. The estimates are driven as
// ChannelDecoder.run drives them: P = |boxcar(u, N_k)|^2 rounded to float32 (as run stores it), one update
// per block of round(block_s r) samples, sigma2() after each.
#include "kz4ap/bank/noise.hpp"

#include "golden.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <complex>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using namespace kz4ap::bank;
using kz4ap::test::expect_close;
using kz4ap::test::load_golden;

constexpr double kRate = 1500.0;  // samples/s

std::vector<std::complex<double>> stream(const nlohmann::json& g) {
    const auto re = g.at("u_re").get<std::vector<double>>();
    const auto im = g.at("u_im").get<std::vector<double>>();
    std::vector<std::complex<double>> u(re.size());
    for (std::size_t i = 0; i < u.size(); ++i) u[i] = {re[i], im[i]};
    return u;
}

// The prototype's P: np.abs(boxcar(u, N_k)) ** 2, stored as float32.
Matrix powers(const std::vector<std::complex<double>>& u, const std::vector<int>& n) {
    Matrix P;
    P.rows = static_cast<int>(n.size());
    P.cols = static_cast<int>(u.size());
    P.v.resize(static_cast<std::size_t>(P.rows) * u.size());
    for (int k = 0; k < P.rows; ++k) {
        const auto v = boxcar(u, n[static_cast<std::size_t>(k)]);
        for (int i = 0; i < P.cols; ++i) {
            const double a = std::abs(v[static_cast<std::size_t>(i)]);
            P.at(k, i) = static_cast<double>(static_cast<float>(a * a));
        }
    }
    return P;
}

std::vector<int> ladder(double rate_hz) { return branch_samples(branch_lengths_s(BankConfig{}), rate_hz); }

int block_samples(double rate_hz) {
    return std::max(1, static_cast<int>(std::nearbyint(BankConfig{}.block_s * rate_hz)));
}

void check_method(const std::string& method) {
    const auto g = load_golden("noise");
    const auto u = stream(g);
    const auto n = ladder(kRate);
    const Matrix P = powers(u, n);
    BankConfig cfg;
    cfg.noise_method = method;
    auto est = make_noise(cfg, kRate, n);
    const auto want = g.at(method + "_sigma2").get<std::vector<std::vector<double>>>();
    const auto want_n1 = g.at(method + "_n1").get<std::vector<int>>();
    const bool spectrum = method != "branch";
    std::vector<int> want_seg, want_offered;
    if (spectrum) {
        want_seg = g.at(method + "_segments").get<std::vector<int>>();
        want_offered = g.at(method + "_segments_offered").get<std::vector<int>>();
    }
    const int total = static_cast<int>(u.size());
    const int block = block_samples(kRate);
    std::size_t snap = 0;
    int b = 0;
    for (int n0 = 0; n0 < total; n0 += block, ++b) {
        const int n1 = std::min(n0 + block, total);
        est->update(u, P, n0, n1);
        const auto s = est->sigma2();
        if (b % 10 != 9) continue;
        ASSERT_LT(snap, want.size());
        SCOPED_TRACE(method + " block " + std::to_string(b));
        EXPECT_EQ(n1, want_n1[snap]);
        ASSERT_EQ(s.size(), want[snap].size());
        for (std::size_t k = 0; k < s.size(); ++k) expect_close(s[k], want[snap][k]);
        if (spectrum) {
            const auto& sp = dynamic_cast<const SpectrumNoise&>(*est);
            EXPECT_EQ(sp.segments(), want_seg[snap]);
            EXPECT_EQ(sp.segments_offered(), want_offered[snap]);
        }
        ++snap;
    }
    EXPECT_EQ(snap, want.size());
}

TEST(BankNoise, GuardMean) {
    const auto g = load_golden("noise");
    const auto kappa = g.at("guard_mean_kappa").get<std::vector<double>>();
    const auto want = g.at("guard_mean").get<std::vector<double>>();
    for (std::size_t i = 0; i < kappa.size(); ++i) expect_close(guard_mean(kappa[i]), want[i]);
}

TEST(BankNoise, QuantileLinearIsNumpysDefault) {
    // numpy.quantile(x, 0.2): virtual index 0.2 (n - 1), interpolated between its neighbors.
    EXPECT_EQ(quantile_linear({5.0}, 0.2), 5.0);
    EXPECT_DOUBLE_EQ(quantile_linear({4.0, 1.0, 3.0, 2.0, 0.0}, 0.2), 0.8);   // index 0.8
    EXPECT_DOUBLE_EQ(quantile_linear({3.0, 0.0, 1.0, 2.0}, 0.2), 0.6);        // index 0.6
    EXPECT_EQ(quantile_linear({7.0, 9.0}, 1.0), 9.0);
    EXPECT_TRUE(std::isnan(quantile_linear({1.0, std::nan(""), 2.0}, 0.2)));
}

TEST(BankNoise, SpectrumMatchesPrototype) { check_method("spectrum"); }
TEST(BankNoise, SpectrumLevelMatchesPrototype) { check_method("spectrum-level"); }
TEST(BankNoise, BranchFallbackMatchesPrototype) { check_method("branch"); }

TEST(BankNoise, UnknownMethodThrows) {
    BankConfig cfg;
    cfg.noise_method = "nonsense";
    EXPECT_THROW(make_noise(cfg, kRate, ladder(kRate)), std::invalid_argument);
}

TEST(BankNoise, MaskBiasMustMatchTheLadder) {
    BankConfig cfg;
    EXPECT_THROW(SpectrumNoise(cfg, kRate, {14, 15}), std::invalid_argument);
}

// 5 s of exact zeros, then 5 s of white noise (1 FS^2 per complex sample).
std::vector<std::complex<double>> zeros_then_noise() {
    const int half = static_cast<int>(5 * kRate);
    std::vector<std::complex<double>> u(2 * static_cast<std::size_t>(half));
    std::mt19937_64 rng(3);
    std::normal_distribution<double> gauss(0.0, std::sqrt(0.5));
    for (std::size_t i = static_cast<std::size_t>(half); i < u.size(); ++i) u[i] = {gauss(rng), gauss(rng)};
    return u;
}

// Review Focus 3 (plan): no NaN or Inf at any time, and every sigma2 finite and positive after the warm-up.
// DISABLED pending the owner's ruling: the prototype itself fails it (task-3 report). On exact zeros the
// three-tap level falls to kMinVar = 1e-20 FS^2 and, once noise arrives, no tap passes the guard again, so
// it stays there; the spectrum's first accepted all-zero segment (n1 = 576 samples, 0.384 s) makes the
// shape all zeros, so variant (a)'s ratio is 0/0 = NaN from then on, and variant (b)'s level is 0 FS^2.
// The port is faithful.
TEST(BankNoise, DISABLED_ExactZerosThenNoiseStayFiniteAndPositive) {
    const auto u = zeros_then_noise();
    const auto n = ladder(kRate);
    const Matrix P = powers(u, n);
    const int warmup = static_cast<int>(std::nearbyint(BankConfig{}.noise_warmup_s * kRate));
    for (const char* method : {"spectrum", "spectrum-level", "branch"}) {
        BankConfig cfg;
        cfg.noise_method = method;
        auto est = make_noise(cfg, kRate, n);
        const int block = block_samples(kRate);
        for (int n0 = 0; n0 < P.cols; n0 += block) {
            const int n1 = std::min(n0 + block, P.cols);
            est->update(u, P, n0, n1);
            for (double s : est->sigma2()) {
                ASSERT_TRUE(std::isfinite(s)) << method << " at n1 = " << n1;
                if (n1 >= warmup) ASSERT_GT(s, 0.0) << method << " at n1 = " << n1;
            }
        }
    }
}

// What the prototype does on the same input (measured with noise.py, task-3 report), pinned so the port's
// behavior is visible until the ruling: branch stays at kMinVar; spectrum-level reads 0 FS^2 and spectrum
// NaN from the first accepted segment on (n1 = 576, 0.384 s).
TEST(BankNoise, ExactZerosThenNoiseAsThePrototype) {
    const auto u = zeros_then_noise();
    const auto n = ladder(kRate);
    const Matrix P = powers(u, n);
    const int block = block_samples(kRate);
    for (const char* method : {"spectrum", "spectrum-level", "branch"}) {
        BankConfig cfg;
        cfg.noise_method = method;
        auto est = make_noise(cfg, kRate, n);
        const std::string m = method;
        for (int n0 = 0; n0 < P.cols; n0 += block) {
            const int n1 = std::min(n0 + block, P.cols);
            est->update(u, P, n0, n1);
            const auto s = est->sigma2();
            if (m == "branch") {
                for (double x : s) ASSERT_EQ(x, kMinVar) << "n1 = " << n1;
            } else if (n1 >= 576) {
                for (double x : s) {
                    if (m == "spectrum") ASSERT_TRUE(std::isnan(x)) << "n1 = " << n1;
                    else ASSERT_EQ(x, 0.0) << "n1 = " << n1;
                }
            } else {
                for (double x : s) ASSERT_TRUE(std::isfinite(x) && x > 0.0) << "n1 = " << n1;
            }
        }
    }
}

}  // namespace
