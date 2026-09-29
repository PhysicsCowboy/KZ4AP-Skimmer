#include "kz4ap/frequency_tracker.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <complex>
#include <cstddef>
#include <numbers>
#include <random>
#include <stdexcept>

using namespace kz4ap;

namespace {

constexpr double kRate = 1500.0;

// A tone whose frequency starts at f0_hz and changes linearly at slope_hz_per_s.
struct Tone {
    double f0_hz;
    double slope_hz_per_s = 0;
    double amplitude = 1.0;
    Sample at(std::size_t n) const {
        const double t = static_cast<double>(n) / kRate;
        const double ph = 0.3 + 2 * std::numbers::pi * (f0_hz * t + 0.5 * slope_hz_per_s * t * t);
        return Sample(static_cast<float>(amplitude * std::cos(ph)), static_cast<float>(amplitude * std::sin(ph)));
    }
    double freq_at(std::size_t n) const { return f0_hz + slope_hz_per_s * static_cast<double>(n) / kRate; }
};

// Feeds count samples of the tone plus complex white noise (total power noise_sigma^2),
// observing the mixed stream itself with the given weight. Returns the next sample index.
std::size_t feed(FrequencyTracker& tracker, const Tone& tone, std::size_t first, std::size_t count, float weight,
                 double noise_sigma = 0.0, unsigned seed = 1) {
    std::mt19937 rng(seed);
    std::normal_distribution<double> gauss(0.0, noise_sigma > 0 ? noise_sigma / std::sqrt(2.0) : 1.0);
    for (std::size_t n = first; n < first + count; ++n) {
        Sample y = tone.at(n);
        if (noise_sigma > 0) y += Sample(static_cast<float>(gauss(rng)), static_cast<float>(gauss(rng)));
        tracker.observe(tracker.mix(y), weight);
    }
    return first + count;
}

std::size_t seconds(double s) { return static_cast<std::size_t>(s * kRate); }

}  // namespace

TEST(FrequencyTracker, MixRemovesTheOffsetItStartsWith) {
    FrequencyTracker tracker(kRate, 10.0);
    const Tone tone{10.0};
    const Sample first = tracker.mix(tone.at(0));
    for (std::size_t n = 1; n < seconds(1.0); ++n) {
        const Sample u = tracker.mix(tone.at(n));
        ASSERT_LT(std::abs(std::arg(u / first)), 1e-3) << n;
    }
}

TEST(FrequencyTracker, ConvergesOnASteadyTone) {
    FrequencyTracker tracker(kRate, 0.0);
    feed(tracker, Tone{9.0}, 0, seconds(3.0), 1.0f);
    EXPECT_NEAR(tracker.offset_hz(), 9.0, 0.05);
}

TEST(FrequencyTracker, ConvergesOnANegativeOffset) {
    FrequencyTracker tracker(kRate, -15.0);  // the detector's estimate, 5 Hz off
    feed(tracker, Tone{-20.0}, 0, seconds(3.0), 1.0f);
    EXPECT_NEAR(tracker.offset_hz(), -20.0, 0.05);
}

TEST(FrequencyTracker, FollowsSlowDrift) {
    // Owner, 2026-09-29: only slow drift matters. The engine moves the anchor with the detector's
    // peak, which lags a 1 Hz/s ramp by about 1 Hz (a 1 s power average lags a ramp by fdot * tau,
    // derived); here the anchor is set every 21.3 ms to the tone's frequency minus 1 Hz. The tracker
    // lags by about slope x tau_s = 0.5 Hz (derived for weight 1). The tone ends 20 Hz from where it
    // started, beyond the +/-12 Hz the tracker could reach around a fixed anchor.
    FrequencyTracker tracker(kRate, 5.0);
    const Tone tone{5.0, 1.0};
    std::size_t n = 0;
    while (n < seconds(20.0)) {
        tracker.set_anchor(tone.freq_at(n) - 1.0);
        n = feed(tracker, tone, n, 32, 1.0f);
    }
    EXPECT_NEAR(tracker.offset_hz(), tone.freq_at(n), 1.0);
}

TEST(FrequencyTracker, ZeroWeightFreezesTheEstimate) {
    FrequencyTracker tracker(kRate, 0.0);
    // A whole number of 32-sample update periods, so `locked` reflects every sample fed.
    const auto n = feed(tracker, Tone{9.0}, 0, 32 * 140, 1.0f);
    const double locked = tracker.offset_hz();
    feed(tracker, Tone{0.0, 0.0, 0.0}, n, seconds(10.0), 0.0f, 1.0);  // a 10 s pause: noise only, weight 0
    EXPECT_EQ(tracker.offset_hz(), locked);
}

TEST(FrequencyTracker, LittleWeightKeepsTheInitialOffset) {
    FrequencyTracker tracker(kRate, 4.0);
    feed(tracker, Tone{9.0}, 0, seconds(1.0), 0.001f);
    EXPECT_EQ(tracker.offset_hz(), 4.0);
}

TEST(FrequencyTracker, ClampsToTheMaximumOffset) {
    FrequencyTracker tracker(kRate, 70.0);  // a station near the edge; 80 Hz is within 12 Hz of the anchor
    feed(tracker, Tone{80.0}, 0, seconds(3.0), 1.0f);
    EXPECT_DOUBLE_EQ(tracker.offset_hz(), 75.0);
}

TEST(FrequencyTracker, FineTunesOnlyNearItsAnchor) {
    // Owner decisions 2026-09-29, option 1: the tracker accepts its own estimate only within
    // +/-12 Hz of its anchor (here the initial offset, 0 Hz); the detector decides which station
    // the channel follows. A tone 10 Hz away is followed (a pure tone's average converges to its
    // frequency, derived); tones 20, 60 and 100 Hz away (100 Hz aliases to -87.5 Hz through the
    // 5.33 ms lag) are rejected: the average is emptied and the NCO returns exactly to the anchor.
    FrequencyTracker tracker(kRate, 0.0);
    auto n = feed(tracker, Tone{10.0}, 0, seconds(3.0), 1.0f);
    EXPECT_NEAR(tracker.offset_hz(), 10.0, 0.05);
    for (const double away_hz : {20.0, 60.0, 100.0}) {
        n = feed(tracker, Tone{away_hz}, n, seconds(3.0), 1.0f);
        EXPECT_EQ(tracker.offset_hz(), 0.0) << away_hz << " Hz";
        EXPECT_EQ(tracker.anchor_hz(), 0.0) << away_hz << " Hz";  // the anchor never follows the tracker
    }
}

TEST(FrequencyTracker, SetAnchorJumpsOnlyBeyondTheFineTuneRange) {
    FrequencyTracker tracker(kRate, 0.0);
    auto n = feed(tracker, Tone{9.0}, 0, 32 * 140, 1.0f);
    const double locked = tracker.offset_hz();
    EXPECT_NEAR(locked, 9.0, 0.05);
    // The detector's frequency moved 5 Hz: within 12 Hz of the NCO, so fine-tuning continues.
    tracker.set_anchor(5.0);
    EXPECT_EQ(tracker.anchor_hz(), 5.0);
    EXPECT_EQ(tracker.offset_hz(), locked);
    // The detector's track moved to another peak (a turnover within D): the NCO jumps there.
    tracker.set_anchor(30.0);
    EXPECT_EQ(tracker.offset_hz(), 30.0);
    n = feed(tracker, Tone{31.0}, n, seconds(3.0), 1.0f);
    EXPECT_NEAR(tracker.offset_hz(), 31.0, 0.05);
    // The anchor is clamped to the NCO range.
    tracker.set_anchor(100.0);
    EXPECT_EQ(tracker.anchor_hz(), 75.0);
    EXPECT_EQ(tracker.offset_hz(), 75.0);  // 44 Hz from the NCO: a jump
}

TEST(FrequencyTracker, EstimateIsAccurateInNoise) {
    FrequencyTracker tracker(kRate, 0.0);
    // 10 dB SNR per sample at 1500 samples/s
    feed(tracker, Tone{9.0}, 0, seconds(10.0), 1.0f, std::sqrt(0.1));
    EXPECT_NEAR(tracker.offset_hz(), 9.0, 1.0);
}

TEST(FrequencyTracker, ReacquireKeepsTheOffsetButStartsAFreshAverage) {
    FrequencyTracker tracker(kRate, 0.0);
    auto n = feed(tracker, Tone{9.0}, 0, 32 * 140, 1.0f);
    tracker.reacquire();
    EXPECT_NEAR(tracker.offset_hz(), 9.0, 0.05);
    // A new station at -9 Hz (within 12 Hz of the anchor): with a fresh average, little weight does
    // not move the NCO...
    n = feed(tracker, Tone{-9.0}, n, seconds(1.0), 0.001f);
    EXPECT_NEAR(tracker.offset_hz(), 9.0, 0.05);
    // ...and 0.6 s of full weight moves it to the new station. Without reacquire() the old average
    // would still hold e^-1.2 = 0.30 of the weight and leave the estimate near -3.7 Hz (derived from
    // the two phasors' weights), outside this tolerance.
    feed(tracker, Tone{-9.0}, n, seconds(0.6), 1.0f);
    EXPECT_NEAR(tracker.offset_hz(), -9.0, 0.25);
}

TEST(FrequencyTracker, ResetReturnsToTheGivenOffset) {
    FrequencyTracker tracker(kRate, 0.0);
    feed(tracker, Tone{9.0}, 0, seconds(3.0), 1.0f);
    tracker.set_anchor(20.0);
    tracker.reset(3.0);
    EXPECT_EQ(tracker.offset_hz(), 3.0);
    EXPECT_EQ(tracker.anchor_hz(), 3.0);
}

TEST(FrequencyTracker, RejectsInvalidConfig) {
    EXPECT_THROW(FrequencyTracker(0.0, 0.0), std::invalid_argument);
    FrequencyTrackerConfig c;
    c.lag_s = 0.0001;  // rounds to 0 samples at 1500 samples/s
    EXPECT_THROW(FrequencyTracker(kRate, 0.0, c), std::invalid_argument);
    c = {};
    c.tau_s = 0;
    EXPECT_THROW(FrequencyTracker(kRate, 0.0, c), std::invalid_argument);
    c = {};
    c.min_weight = 1.0;
    EXPECT_THROW(FrequencyTracker(kRate, 0.0, c), std::invalid_argument);
    c = {};
    c.max_offset_hz = 100.0;  // beyond the +/-93.75 Hz unambiguous range of the 5.33 ms lag
    EXPECT_THROW(FrequencyTracker(kRate, 0.0, c), std::invalid_argument);
    c = {};
    c.fine_tune_hz = 0.0;
    EXPECT_THROW(FrequencyTracker(kRate, 0.0, c), std::invalid_argument);
    c = {};
    c.min_coherence = 1.0;
    EXPECT_THROW(FrequencyTracker(kRate, 0.0, c), std::invalid_argument);
    c = {};
    c.update_interval_s = 0.0;
    EXPECT_THROW(FrequencyTracker(kRate, 0.0, c), std::invalid_argument);
}
