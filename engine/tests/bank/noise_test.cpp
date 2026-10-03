// The bank decoder's noise estimates against the prototype's golden values
// (engine/tests/data/bank/noise.json, from training/kz4ap_proto/golden.py golden_noise): sigma2 to
// relative 1e-9 (floor 1e-12 absolute), segment counts exactly. The estimates are driven as
// ChannelDecoder.run drives them: P = |boxcar(u, N_k)|^2 rounded to float32 (as run stores it), one update
// per block of round(block_s r) samples, sigma2() after each.
#include "kz4ap/bank/noise.hpp"

#include "golden.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <span>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using namespace kz4ap::bank;
using kz4ap::test::expect_close;
using kz4ap::test::load_golden;

constexpr double kRate = 1500.0;  // samples/s

using kz4ap::test::powers;
using kz4ap::test::stream;

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

// The columns [from, to) of P as a window (column 0 = absolute sample from).
Matrix window_of(const Matrix& P, int from, int to) {
    Matrix w;
    w.rows = P.rows;
    w.cols = to - from;
    w.v.resize(static_cast<std::size_t>(w.rows) * static_cast<std::size_t>(w.cols));
    for (int k = 0; k < P.rows; ++k)
        for (int i = from; i < to; ++i) w.at(k, i - from) = P.at(k, i);
    return w;
}

// Sample indices past 2^31 (a channel open for more than 16.6 days at 1500 samples/s): the same stream with
// every absolute index shifted by 2^31 + 7 (the stream's first sample there), given the whole of P and then
// only a sliding window of recent columns, gives bit-identical estimates after every block.
void check_large_indices(const std::string& method) {
    const auto g = load_golden("noise");
    const auto u = stream(g);
    const auto n = ladder(kRate);
    const Matrix P = powers(u, n);
    BankConfig cfg;
    cfg.noise_method = method;
    const std::int64_t offset = (std::int64_t{1} << 31) + 7;
    auto small = make_noise(cfg, kRate, n);
    auto large = make_noise(cfg, kRate, n, offset);
    auto sliding = make_noise(cfg, kRate, n, offset);
    const int total = static_cast<int>(u.size());
    const int block = block_samples(kRate);
    constexpr int kWindow = 2000;  // samples kept back: more than every look-back of the noise estimates
    for (int n0 = 0; n0 < total; n0 += block) {
        const int n1 = std::min(n0 + block, total);
        small->update(u, P, n0, n1);
        large->update(u, P, offset + n0, offset + n1, offset);
        // the sliding window starts at the stream's start until the warm-up is over, as the channel's does
        const int from = n1 <= kWindow ? 0 : n1 - kWindow;
        const Matrix w = window_of(P, from, n1);
        sliding->update(std::span(u).subspan(static_cast<std::size_t>(from), static_cast<std::size_t>(n1 - from)),
                        w, offset + n0, offset + n1, offset + from);
        const auto s = small->sigma2();
        const auto l = large->sigma2();
        const auto sl = sliding->sigma2();
        ASSERT_EQ(l.size(), s.size());
        for (std::size_t k = 0; k < s.size(); ++k) {
            const bool both_nan = std::isnan(s[k]) && std::isnan(l[k]) && std::isnan(sl[k]);
            ASSERT_TRUE(both_nan || (l[k] == s[k] && sl[k] == s[k])) << method << " block at " << n0 << " branch " << k;
        }
    }
    if (method != "branch") {
        EXPECT_EQ(dynamic_cast<const SpectrumNoise&>(*large).segments(),
                  dynamic_cast<const SpectrumNoise&>(*small).segments());
        EXPECT_EQ(dynamic_cast<const SpectrumNoise&>(*sliding).segments(),
                  dynamic_cast<const SpectrumNoise&>(*small).segments());
        EXPECT_GT(dynamic_cast<const SpectrumNoise&>(*small).segments(), 0);
    }
}

TEST(BankNoise, SampleIndicesPast2To31GiveTheSameEstimates) {
    check_large_indices("spectrum");
    check_large_indices("spectrum-level");
    check_large_indices("branch");
}

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
// DISABLED by the controller's ruling (2026-10-03): the prototype itself fails it (task-3 report), Plan A
// keeps the prototype's behavior, and the fix is a Plan B item; enable this test with it. On exact zeros the
// three-tap level falls to kMinVar = 1e-20 FS^2 and, once noise arrives, no tap passes the guard again, so
// it stays there; the spectrum's first accepted all-zero segment (n1 = 576 samples, 0.384 s) makes the
// shape all zeros, so variant (a)'s ratio is 0/0 = NaN from then on, and variant (b)'s level is 0 FS^2.
// The port is faithful (docs/signal-processing.md section 8c, "Exact zeros").
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
// behavior is visible until Plan B fixes it: branch stays at kMinVar; spectrum-level reads 0 FS^2 and spectrum
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
