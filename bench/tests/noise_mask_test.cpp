// The spectrum noise estimate's mask (Plan B, B4b): the white-noise source of the measurement, and the mask-bias
// table BankConfig::mask_bias as its own measurement by stage 1's Task 5 method on the C++ estimator.
#include "noise_mask.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <complex>
#include <cstdint>
#include <vector>

namespace {

using kz4ap::bank::BankConfig;
using kz4ap::bench::measure_mask_bias;
using kz4ap::bench::white_noise;

constexpr double kRate = 1500.0;  // samples/s

TEST(NoiseMask, WhiteNoiseHasUnitPowerAndIsReproducible) {
    const auto u = white_noise(600000, 5);
    double p = 0.0, re = 0.0, im = 0.0;
    for (const auto& x : u) {
        p += std::norm(x);
        re += x.real() * x.real();
        im += x.imag() * x.imag();
    }
    const double n = static_cast<double>(u.size());
    // 1 FS^2 per complex sample, half per component; the standard error of each mean is about 1.3e-3 (derived:
    // sqrt(2 / n) for a chi-square of 2n degrees of freedom per complex sample), so 0.01 is about 7 of them
    EXPECT_NEAR(p / n, 1.0, 0.01);
    EXPECT_NEAR(re / n, 0.5, 0.01);
    EXPECT_NEAR(im / n, 0.5, 0.01);
    EXPECT_EQ(white_noise(1000, 5)[999], u[999]);
    EXPECT_NE(white_noise(1000, 6)[999], u[999]);
}

// The table BankConfig::mask_bias was measured on stage 1's Task 5 noise (numpy's default_rng, seeds 101-110, 60 s
// each, rounded to complex64) through this estimate (kz4ap-noise-mask stream; docs/signal-processing.md section 8c).
// That noise is not reproducible here, so this test re-measures with this file's white noise, the same seeds and
// length, and requires each branch's value within 3 standard errors of the difference of two independent 10-seed
// means (sqrt(2) x the per-seed scatter / sqrt(10); a consistency check, not an identity). Measured (Windows): the
// largest difference is 2.5 of them at k = 32 (+0.0287); with 200 seeds (1001-1200) this noise gives 0.8381 (k = 1)
// to 0.7812 (k = 32), between the table and this 10-seed measurement.
void expect_consistent(const BankConfig& cfg, const std::vector<double>& table) {
    std::vector<std::uint64_t> seeds;
    for (std::uint64_t s = 101; s <= 110; ++s) seeds.push_back(s);
    const auto b = measure_mask_bias(cfg, kRate, seeds, 60.0);
    ASSERT_EQ(b.bias.size(), table.size());
    ASSERT_EQ(b.per_seed.size(), seeds.size());
    double worst = 0.0;
    for (std::size_t k = 0; k < table.size(); ++k) {
        const double se_diff = std::sqrt(2.0) * b.scatter[k] * b.bias[k] / std::sqrt(10.0);
        EXPECT_LE(std::abs(b.bias[k] - table[k]), 3.0 * se_diff) << "branch " << k + 1 << ": measured " << b.bias[k];
        worst = std::max(worst, std::abs(b.bias[k] - table[k]) / se_diff);
        EXPECT_GT(b.bias[k], 0.0);
        EXPECT_LT(b.bias[k], 1.0);
    }
    std::printf("[ info ] guard margin %.4f s: %d of %d segments accepted, kept fraction %.4f; largest difference "
                "from the table %.2f standard errors\n",
                cfg.guard_margin_s, b.segments, b.segments_offered, b.kept_fraction, worst);
}

TEST(NoiseMask, TheTableIsAWhiteNoiseMeasurementAtTheDefaultMargin) {
    const BankConfig cfg;
    ASSERT_EQ(cfg.mask_bias.size(), 32u);
    expect_consistent(cfg, cfg.mask_bias);
}

// The method's check (B4b Step 1): at stage 1's 20 ms margin the same measurement agrees with stage 1's table.
// Measured (Windows): the largest difference 1.9 single-measurement standard errors (1.35 of the difference).
TEST(NoiseMask, AtStageOnesMarginItAgreesWithStageOnesTable) {
    BankConfig cfg;
    cfg.guard_margin_s = kz4ap::bank::kStage1GuardMarginS;
    cfg.mask_bias = kz4ap::bank::kStage1MaskBias;
    expect_consistent(cfg, kz4ap::bank::kStage1MaskBias);
}

// The counting window of run_mask: segments of M = 256 samples starting in [from, to) only.
TEST(NoiseMask, TheWindowCountsTheSegmentsStartingInIt) {
    const auto u = white_noise(static_cast<std::size_t>(20 * kRate), 9);
    const BankConfig cfg;
    const auto all = kz4ap::bench::run_mask(cfg, u, kRate);
    const auto w = kz4ap::bench::run_mask(cfg, u, kRate, 5.0, 15.0);
    EXPECT_EQ(w.segments, all.segments);  // the whole-stream counts do not depend on the window
    EXPECT_EQ(w.segments_offered, all.segments_offered);
    // segment starts j 256 / 1500 s in [5, 15): j = 30 ... 87, 58 segments, all offered (noise only, past the warm-up)
    EXPECT_EQ(w.window_offered, 58);
    EXPECT_LE(w.window_segments, 58);
    EXPECT_GT(w.window_segments, 0);
    EXPECT_DOUBLE_EQ(w.window_s, 10.0);
}

}  // namespace
