#include "kz4ap/viterbi/weight.hpp"

#include "kz4ap/channelizer.hpp"
#include "kz4ap/viterbi/front_filter.hpp"
#include "kz4ap/viterbi/observation.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <utility>

using namespace kz4ap;
using namespace kz4ap::viterbi;

namespace {

// The ratio q (1) of a level in dB SNR in the filter's 74.48 Hz.
double q_of_db(double db) { return std::pow(10.0, db / 10.0); }

}  // namespace

TEST(Weight, MatchesTheCurve) {
    // The design's curve (part 4, measured; seed-to-seed spread about +/-0.0006): the weak limit 0.2754 at and below
    // q = 1e-4 (-40 dB SNR in the filter's 74.48 Hz), the strong limit 0.1986 at and above q = 1e4 (+40 dB), each within
    // 0.0002 (the curve's distance from its limit at the clamp); between them the design's table within 0.0015 (the
    // Monte Carlo's +/-0.0006 and the table's rounding to 3 or 4 figures); and non-increasing in q within 0.0006.
    for (const double q : {0.0, 1e-9, 1e-6, 1e-4}) EXPECT_NEAR(sample_weight(q), 0.2754, 0.0002) << "q = " << q;
    for (const double q : {1e4, 1e6, 1e12, 1e300}) EXPECT_NEAR(sample_weight(q), 0.1986, 0.0002) << "q = " << q;
    const std::array<std::pair<double, double>, 10> curve = {{{-20.0, 0.273},
                                                              {-10.0, 0.259},
                                                              {-5.0, 0.240},
                                                              {0.0, 0.217},
                                                              {3.0, 0.208},
                                                              {6.0, 0.203},
                                                              {10.0, 0.200},
                                                              {15.0, 0.199},
                                                              {20.0, 0.1987},
                                                              {30.0, 0.1986}}};
    for (const auto& [db, w] : curve)
        EXPECT_NEAR(sample_weight(q_of_db(db)), w, 0.0015) << db << " dB SNR in the filter's 74.48 Hz";
    // Non-increasing: every value at most the smallest value at a lower q plus 0.0006, on 2 001 points from -50 to
    // +50 dB SNR in the filter's 74.48 Hz.
    double lowest = sample_weight(q_of_db(-50.0));
    for (int i = 1; i <= 2000; ++i) {
        const double db = -50.0 + 0.05 * i;
        const double w = sample_weight(q_of_db(db));
        EXPECT_LE(w, lowest + 0.0006) << db << " dB SNR in the filter's 74.48 Hz";
        lowest = std::min(lowest, w);
    }
}

TEST(Weight, TableLiesWithinTheBroadPriorRegimesAxis) {
    // Every node of the table lies within the w axis of the per-sample score's broad-prior regime,
    // [kBroadPriorLowestW, kBroadPriorHighestW] = [0.1986, 0.2755] (observation.hpp): outside it the regime's tables do
    // not serve and broad priors fall back to the bare quadrature (plan 1b, Task 5's review). Today the nodes lie 5e-5
    // and 6e-5 inside.
    for (std::size_t i = 0; i < kWeightTable.size(); ++i) {
        EXPECT_GE(kWeightTable[i], kBroadPriorLowestW) << "node " << i;
        EXPECT_LE(kWeightTable[i], kBroadPriorHighestW) << "node " << i;
    }
}

TEST(Weight, EndNodesAreTheChainsLimits) {
    // The table's end nodes are the derived limits of the 192 kHz chain (output_noise): w_weak at xi = 0 and w_strong at
    // xi = 1, within 1e-6.
    ChannelizerConfig config;
    config.sample_rate = 192000;
    config.fft_size = 8192;
    const OutputNoise noise = output_noise(channel_filter_taps(config), config.sample_rate,
                                           config.fft_size / config.channel_bins,
                                           front_filter_taps(1500.0, 0.012, 0.0025), 4);
    EXPECT_NEAR(kWeightTable.front(), noise.w_weak, 1e-6);
    EXPECT_NEAR(kWeightTable.back(), noise.w_strong, 1e-6);
}
