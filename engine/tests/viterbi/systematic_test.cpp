#include "kz4ap/viterbi/systematic.hpp"

#include "kz4ap/channelizer.hpp"
#include "kz4ap/viterbi/front_filter.hpp"
#include "kz4ap/viterbi/mark_template.hpp"
#include "kz4ap/viterbi/observation.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <random>
#include <vector>

using namespace kz4ap;
using namespace kz4ap::viterbi;

namespace {

constexpr double kFloor = 0.02;  // f, the floor (placeholder; owner, 2026-10-23)
constexpr double kW = 0.1986;    // the one constant weight (D23)

// The template's tables on the 192 kHz chain (mark_template.hpp).
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

TEST(Systematic, VarianceFromThePrediction) {
    // V = sigma_y^2 + (mu^2 + P)(f^2 s^2 + v), and sigma_y^2 exactly where s = 0 and v = 0 (D23).
    const double mu = 3.0;
    const double p = 0.25;
    const double s = 0.6;
    const double v = 1e-3;
    EXPECT_NEAR(systematic_variance_fs2(1.5, mu * mu + p, s, v, kFloor),
                1.5 + (mu * mu + p) * (kFloor * kFloor * s * s + v), 1e-14);
    EXPECT_EQ(systematic_variance_fs2(1.5, mu * mu + p, 0.0, 0.0, kFloor), 1.5);
}

TEST(Systematic, MisfitBound) {
    // Part 9 (derived): noise-free e = (s + delta) a at +20 to +85 dB S500, the prediction at mu = a, P = 1e-6 a^2, the
    // right story (template s) and the perfect-template story (template s + delta) scored with the same V, the right
    // story's: the right story falls short by at most w [delta^2 / (f^2 s^2 + v) + (1/2) max(0, -ln(1 + delta / s))] +
    // 0.02 nats, over 10^5 draws of s and v from the template table (j >= 5, s >= 0.05, and the plateau) and delta uniform
    // on +/-0.0383 with s + delta > 0. The plateau's value at delta = +0.0383 is 0.73 +/- 0.01 nats.
    const TemplateTables& tables = tables192();
    std::vector<std::pair<double, double>> entries{{1.0, 0.0}};
    for (std::size_t j = 5; j < TemplateTables::kSpan; ++j)
        if (tables.s[j] >= 0.05) entries.emplace_back(tables.s[j], tables.v[j]);
    std::mt19937 rng(20261029);
    std::uniform_int_distribution<std::size_t> pick(0, entries.size() - 1);
    std::uniform_real_distribution<double> uniform_delta(-0.0383, 0.0383);
    std::uniform_real_distribution<double> uniform_level(20.0, 85.0);
    const double sigma2 = 1.0;
    double largest_excess = -1e9;
    for (int draw = 0; draw < 100000; ++draw) {
        const auto [s, v] = entries[pick(rng)];
        const double delta = uniform_delta(rng);
        if (!(s + delta > 0.0)) continue;
        const double a = std::sqrt(std::pow(10.0, uniform_level(rng) / 10.0) * 500.0 / 74.5);
        const double e = (s + delta) * a;
        const double p = 1e-6 * a * a;
        const double variance = systematic_variance_fs2(sigma2, a * a + p, s, v, kFloor);
        const double right = story_evidence(e, s, a, p, variance, sigma2, kW, 5).score_nats;
        const double perfect = story_evidence(e, s + delta, a, p, variance, sigma2, kW, 5).score_nats;
        const double bound = kW * (delta * delta / (kFloor * kFloor * s * s + v) +
                                   0.5 * std::max(0.0, -std::log(1.0 + delta / s)));
        largest_excess = std::max(largest_excess, perfect - right - bound);
    }
    std::cout << "measured: largest excess over the bound " << largest_excess << " nats\n";
    EXPECT_LE(largest_excess, 0.02);
    const double plateau = kW * 0.0383 * 0.0383 / (kFloor * kFloor);
    EXPECT_NEAR(plateau, 0.73, 0.01);
}
