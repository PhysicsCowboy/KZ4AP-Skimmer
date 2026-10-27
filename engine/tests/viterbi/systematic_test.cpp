#include "kz4ap/viterbi/systematic.hpp"

#include "kz4ap/channelizer.hpp"
#include "kz4ap/viterbi/front_filter.hpp"
#include "kz4ap/viterbi/mark_template.hpp"
#include "kz4ap/viterbi/observation.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <optional>
#include <random>
#include <string>
#include <vector>

using namespace kz4ap;
using namespace kz4ap::viterbi;

namespace {

constexpr double kFloor = 0.02;  // f, the floor (placeholder; owner, 2026-10-23)

// The template's tables on the 192 kHz chain (mark_template.hpp): 4 097 channel taps, decimation 128, the 25 front taps,
// a 2.5 ms template rise and transmitters' rises of 0.8 to 4.7 ms.
const TemplateTables& tables192() {
    static const TemplateTables t = [] {
        ChannelizerConfig config;
        config.sample_rate = 192000;
        config.fft_size = 8192;
        return make_template_tables(channel_filter_taps(config), config.sample_rate,
                                    config.fft_size / config.channel_bins, front_filter_taps(1500.0, 0.012, 0.0025), 4,
                                    0.0025, 0.0008, 0.0047);
    }();
    return t;
}

}  // namespace

TEST(Systematic, WindowIsTheLargestPowerOver21Samples) {
    // On 10 s of random e^2 (3 750 samples), the window's peak for every sample k equals a brute-force maximum over
    // the samples k - 10 ... k + 10 that exist, the stream's ends included, whether the samples come one at a time or in
    // blocks of 7 or 32 (the peaks drained after each block, the last ones after finish()).
    constexpr int kSamples = 3750;
    std::mt19937 rng(11);
    std::exponential_distribution<double> power(1.0);
    std::vector<double> e2(kSamples);
    for (double& x : e2) x = power(rng);
    std::vector<double> expected(kSamples);
    for (int k = 0; k < kSamples; ++k) {
        const int from = std::max(0, k - 10);
        const int to = std::min(kSamples - 1, k + 10);
        expected[static_cast<std::size_t>(k)] = *std::max_element(e2.begin() + from, e2.begin() + to + 1);
    }
    for (const int block : {1, 7, 32}) {
        SCOPED_TRACE("blocks of " + std::to_string(block));
        CarrierWindow window(10, 10);
        std::vector<double> peaks;
        const auto drain = [&] {
            while (const std::optional<double> peak = window.next_peak_fs2()) peaks.push_back(*peak);
        };
        for (int at = 0; at < kSamples; at += block) {
            for (int k = at; k < std::min(kSamples, at + block); ++k) window.push(e2[static_cast<std::size_t>(k)]);
            drain();
            // A peak is ready only once its look-ahead has arrived.
            EXPECT_EQ(static_cast<int>(peaks.size()), std::max(0, std::min(kSamples, at + block) - 10));
        }
        window.finish();
        drain();
        ASSERT_EQ(peaks.size(), expected.size());
        for (std::size_t k = 0; k < expected.size(); ++k) EXPECT_EQ(peaks[k], expected[k]) << "sample " << k;
        EXPECT_FALSE(window.next_peak_fs2().has_value());
    }
}

TEST(Systematic, OwnPartOnEverySampleWithATemplate) {
    // C^2 is the sample's own e^2 / s^2 wherever s != 0 and that is above the window's peak, even at s = 0.005; the
    // window's peak where s = 0; and a space beyond the template's tail (s = 0, v = 0) has V' = sigma_y^2.
    const double sigma2 = 3e-6;  // FS^2
    const double peak = 0.04;    // FS^2
    const double s = 0.005;
    const double v = 2e-4;
    const double e = 0.002;      // e^2 / s^2 = 0.16 FS^2, above the peak
    EXPECT_DOUBLE_EQ(v_prime_fs2(sigma2, peak, e, s, v, kFloor),
                     sigma2 + (e * e / (s * s)) * (kFloor * kFloor * s * s + v));
    // Below the peak, the peak.
    EXPECT_DOUBLE_EQ(v_prime_fs2(sigma2, peak, 1e-4, s, v, kFloor), sigma2 + peak * (kFloor * kFloor * s * s + v));
    // A negative template counts by its square.
    EXPECT_DOUBLE_EQ(v_prime_fs2(sigma2, peak, e, -s, v, kFloor), v_prime_fs2(sigma2, peak, e, s, v, kFloor));
    // s = 0: the window's peak, whatever e.
    EXPECT_DOUBLE_EQ(v_prime_fs2(sigma2, peak, 5.0, 0.0, v, kFloor), sigma2 + peak * v);
    // Beyond the tail: the noise alone.
    EXPECT_EQ(v_prime_fs2(sigma2, peak, 5.0, 0.0, 0.0, kFloor), sigma2);
}

TEST(Systematic, CapsMatchTheDesign) {
    // Part 9: w = 0.1986, f = 0.02 and the 192 kHz template give caps of 0.50, 2.51, 8.65, 19.8, 51.6, 265 and 493 nats
    // on a rise's samples j = 5 ... 11 (within 1% or 0.01 nats, the design's rounding) and w / f^2 = 496.5 nats on the
    // plateau (within 0.5 nats).
    const TemplateTables& t = tables192();
    const std::array<double, 7> caps = {0.50, 2.51, 8.65, 19.8, 51.6, 265.0, 493.0};
    for (int j = 5; j <= 11; ++j) {
        const double expected = caps[static_cast<std::size_t>(j - 5)];
        const auto at = static_cast<std::size_t>(j);
        EXPECT_NEAR(score_cap_nats(0.1986, t.s[at], t.v[at], kFloor), expected, std::max(0.01 * expected, 0.01))
            << "j = " << j;
    }
    EXPECT_NEAR(score_cap_nats(0.1986, 1.0, 0.0, kFloor), 496.5, 0.5);
    EXPECT_EQ(score_cap_nats(0.1986, 0.0, 0.0, kFloor), 0.0);
}

TEST(Systematic, EvidenceNeverExceedsTheCap) {
    // 1e5 random draws of a sample and a story (e, s, mu, P, w, v and the window's peak), with V' from v_prime_fs2, so
    // that C^2 >= e^2 / s^2: the score is at most the cap w s^2 / (f^2 s^2 + v) + 0.01 nats (the quadrature).
    std::mt19937 rng(5);
    std::uniform_real_distribution<double> unit(0.0, 1.0);
    const double sigma2 = 1.0;  // FS^2
    int scored = 0;
    double worst = -1e300;  // nats: the largest score less its cap
    std::string where;
    for (int draw = 0; draw < 100000; ++draw) {
        const double s = -0.001 + 1.006 * unit(rng);  // the template's range, -0.0008 to 1.0023
        if (s == 0.0) continue;
        const double v = unit(rng) < 0.5 ? 0.0 : 0.02 * std::pow(unit(rng), 3.0);
        const double e = std::pow(10.0, -1.0 + 5.0 * unit(rng));           // FS: 0.1 to 1e4
        const double mu = std::pow(10.0, -2.0 + 6.0 * unit(rng));          // FS: 0.01 to 1e4
        const double p = mu * mu * std::pow(10.0, -6.0 + 7.0 * unit(rng));  // FS^2: relative spreads 1e-3 to 3
        const double w = 0.1986 + (0.2754 - 0.1986) * unit(rng);
        const double peak = std::pow(10.0, -2.0 + 8.0 * unit(rng));        // FS^2
        const double v_prime = v_prime_fs2(sigma2, peak, e, s, v, kFloor);
        const SampleEvidence evidence = sample_evidence(e, s, mu, p, v_prime, w, 5);
        const double cap = score_cap_nats(w, s, v, kFloor);
        const double excess = evidence.score_nats - cap;
        if (!(excess <= worst)) {
            worst = excess;
            where = "e = " + std::to_string(e) + ", s = " + std::to_string(s) + ", mu = " + std::to_string(mu) +
                    ", P = " + std::to_string(p) + ", w = " + std::to_string(w) + ", v = " + std::to_string(v);
        }
        ++scored;
    }
    EXPECT_GT(scored, 99000);
    std::cout << "largest score less its cap: " << worst << " nats, at " << where << "\n";
    EXPECT_LE(worst, 0.01) << "nats, at " << where;
}
