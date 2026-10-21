#include "kz4ap/viterbi/tracker.hpp"

#include "../test_signals.hpp"
#include "kz4ap/types.hpp"
#include "kz4ap/viterbi/config.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using kz4ap::Sample;
using kz4ap::viterbi::CarrierTracker;
using kz4ap::viterbi::ViterbiConfig;
namespace signals = kz4ap::test;

namespace {

constexpr double kRate = 1500.0;  // channel samples/s
constexpr double kInf = std::numeric_limits<double>::infinity();
constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

// FS: the total (complex) noise sigma on a 1 500 samples/s stream that puts a key-down carrier of amplitude 1 FS at
// s500_db dB S500 (key-down carrier power over the noise power in 500 Hz): S500 = 1 / (sigma^2 x 500 Hz / 1500 Hz).
double sigma_for_s500(double s500_db) { return std::sqrt(3.0 / std::pow(10.0, s500_db / 10.0)); }

// A station sending text at wpm from start_s: amplitude (FS) at freq_hz + drift_hz_per_s x t (Hz), no noise.
std::vector<Sample> station(const std::string& text, double wpm, double start_s, double duration_s, double freq_hz,
                            double amplitude = 1.0, double drift_hz_per_s = 0.0) {
    return signals::keyed_signal_of(signals::keying(text, wpm, start_s), kRate, duration_s, freq_hz, amplitude, 0.0, 1,
                                    drift_hz_per_s);
}

// An unkeyed carrier from on_s to off_s (5 ms edges), amplitude (FS) at freq_hz + drift_hz_per_s x t (Hz), no noise.
std::vector<Sample> carrier(double on_s, double off_s, double duration_s, double freq_hz, double amplitude = 1.0,
                            double drift_hz_per_s = 0.0) {
    return signals::keyed_signal_of({{on_s, off_s}}, kRate, duration_s, freq_hz, amplitude, 0.0, 1, drift_hz_per_s);
}

// White noise of total power sigma^2 (FS^2) alone.
std::vector<Sample> noise(double duration_s, double sigma, unsigned seed) {
    return signals::keyed_signal_of({}, kRate, duration_s, 0.0, 0.0, sigma, seed);
}

std::vector<Sample> sum_of(std::vector<Sample> a, const std::vector<Sample>& b) {
    for (std::size_t i = 0; i < a.size() && i < b.size(); ++i) a[i] += b[i];
    return a;
}

// A text repeated until it lasts at least duration_s at wpm.
std::string text_for(const std::string& unit, double wpm, double duration_s) {
    std::string text;
    while (signals::keying(text, wpm, 0.0).empty() || signals::keying(text, wpm, 0.0).back().second < duration_s)
        text += unit + " ";
    return text;
}

struct Point {
    double t_s;        // s: a sample's time
    double offset_hz;  // Hz: the oscillator's offset at that time, asked right after the sample's block was pushed
};

// The tracker as the decoder uses it: the samples in blocks of `block` (the first sample at 0 s), and after each block
// the oscillator's offset at the block's first and last samples, which is what re-centers them.
std::vector<Point> run(CarrierTracker& tracker, const std::vector<Sample>& x, std::size_t block = 30) {
    std::vector<Point> trace;
    for (std::size_t first = 0; first < x.size(); first += block) {
        const std::size_t n = std::min(block, x.size() - first);
        tracker.push(std::span<const Sample>(x).subspan(first, n), static_cast<double>(first) / kRate);
        for (const std::size_t i : {first, first + n - 1}) {
            const double t = static_cast<double>(i) / kRate;
            trace.push_back({t, tracker.offset_hz(t)});
        }
    }
    return trace;
}

// The largest |offset - truth(t)|, Hz, over the points with from_s <= t <= to_s.
template <class Truth>
double worst_error_hz(const std::vector<Point>& trace, Truth truth, double from_s, double to_s = kInf) {
    double worst = 0.0;
    for (const Point& p : trace)
        if (p.t_s >= from_s && p.t_s <= to_s) worst = std::max(worst, std::abs(p.offset_hz - truth(p.t_s)));
    return worst;
}

}  // namespace

// The five tests below are the task's, with its thresholds. Each runs a set of noise seeds, since the standard
// library's normal distribution differs between platforms and a test on one realization would pass or fail by luck of
// the platform. With the values of ViterbiConfig as specified, FollowsDrift and RelocksAtATurnover pass on every one of
// 100 seeds, and the other three do not (measured, MSVC): HoldsAKeyedCarrier has its worst error from 3 s on at 0.2 to
// 0.6 Hz and above (4 of 100 seeds within 0.2 Hz), ResistsAStrongNeighbor passes on none (the median worst error is
// the anchor's own 2.3 Hz: the weak station is not found), and StaysPutOnNoise is left by about 1 run in 8 of 60 s.
// The header's Measured and Limitations sections give the causes.

TEST(Tracker, HoldsAKeyedCarrier) {
    // PARIS at 25 WPM, S500 0 dB, 7.3 Hz from the channel's center; the tracker starts 2.3 Hz off, at the anchor 5 Hz.
    const std::string text = text_for("PARIS", 25.0, 19.0);
    for (const unsigned seed : {1u, 2u, 3u, 4u, 5u}) {
        SCOPED_TRACE("seed " + std::to_string(seed));
        const auto x = signals::keyed_signal(text, 25.0, kRate, 20.0, 7.3, 1.0, sigma_for_s500(0.0), seed);
        CarrierTracker tracker(kRate, ViterbiConfig{}, 5.0);
        const auto trace = run(tracker, x);
        EXPECT_LE(worst_error_hz(trace, [](double) { return 7.3; }, 3.0), 0.2);
    }
}

TEST(Tracker, FollowsDrift) {
    // A carrier drifting 1 Hz/s for 20 s, from -5 Hz to +15 Hz, PARIS at 25 WPM, S500 +10 dB; the anchor is 3 Hz off.
    const std::string text = text_for("PARIS", 25.0, 19.0);
    const auto truth = [](double t_s) { return -5.0 + 1.0 * t_s; };
    for (const unsigned seed : {1u, 2u, 3u, 4u, 5u}) {
        SCOPED_TRACE("seed " + std::to_string(seed));
        const auto x = signals::keyed_signal(text, 25.0, kRate, 20.0, -5.0, 1.0, sigma_for_s500(10.0), seed, 0.5, 1.0);
        CarrierTracker tracker(kRate, ViterbiConfig{}, -2.0);
        const auto trace = run(tracker, x);
        EXPECT_LE(worst_error_hz(trace, truth, 3.0), 0.67);
        // The oscillator runs at the carrier's rate between updates: its slope over the last second is the drift.
        EXPECT_NEAR(tracker.offset_hz(20.0) - tracker.offset_hz(19.0), 1.0, 0.3);
    }
}

TEST(Tracker, StaysPutOnNoise) {
    // 60 s of noise and no station: no peak should stand out, nothing is measured, and the oscillator stays where it
    // began. Twenty seeds, since a run is 120 windows and a false alarm in any of them can move it.
    for (unsigned seed = 1; seed <= 20; ++seed) {
        SCOPED_TRACE("seed " + std::to_string(seed));
        const auto x = noise(60.0, sigma_for_s500(0.0), seed);
        CarrierTracker tracker(kRate, ViterbiConfig{}, 3.0);
        const auto trace = run(tracker, x);
        double sum_squares = 0.0;
        double worst = 0.0;
        for (const Point& p : trace) {
            sum_squares += (p.offset_hz - 3.0) * (p.offset_hz - 3.0);
            worst = std::max(worst, std::abs(p.offset_hz - 3.0));
        }
        EXPECT_LE(worst, 15.0);
        EXPECT_LE(std::sqrt(sum_squares / static_cast<double>(trace.size())), 2.0);
    }
}

TEST(Tracker, RelocksAtATurnover) {
    // Station A at 2 Hz, 1 s of silence, then station B at 10 Hz, 8 Hz away. Both PARIS at 25 WPM, S500 +10 dB.
    const std::string text = "PARIS PARIS PARIS";
    const double a_end_s = signals::keying(text, 25.0, 0.5).back().second;
    const double b_start_s = a_end_s + 1.0;
    const auto b_marks = signals::keying(text, 25.0, b_start_s);
    const double b_first_mark_s = b_marks.front().first;
    const double duration_s = b_marks.back().second + 1.0;
    for (const unsigned seed : {1u, 2u, 3u, 4u, 5u}) {
        SCOPED_TRACE("seed " + std::to_string(seed));
        const auto x = sum_of(sum_of(station(text, 25.0, 0.5, duration_s, 2.0),
                                     station(text, 25.0, b_start_s, duration_s, 10.0)),
                              noise(duration_s, sigma_for_s500(10.0), seed));
        CarrierTracker tracker(kRate, ViterbiConfig{}, 0.0);
        const auto trace = run(tracker, x);
        EXPECT_LE(worst_error_hz(trace, [](double) { return 2.0; }, 3.0, a_end_s), 0.67);
        EXPECT_LE(worst_error_hz(trace, [](double) { return 10.0; }, b_first_mark_s + 2.0), 0.67);
    }
}

TEST(Tracker, ResistsAStrongNeighbor) {
    // The weak station: PARIS at 25 WPM, S500 0 dB, 7.3 Hz from the center; the tracker starts at the anchor 5 Hz. The
    // neighbor: 20 dB stronger (S500 +20 dB, ten times the amplitude), 50 Hz above or below it, another text at another
    // speed.
    const std::string weak_text = text_for("PARIS", 25.0, 19.0);
    const std::string neighbor_text = text_for("CQ TEST DE KZ4AP", 22.0, 19.0);
    for (const double separation_hz : {50.0, -50.0}) {
        for (const unsigned seed : {1u, 2u, 3u}) {
            SCOPED_TRACE("neighbor " + std::to_string(separation_hz) + " Hz away, seed " + std::to_string(seed));
            const auto x = sum_of(sum_of(station(weak_text, 25.0, 0.5, 20.0, 7.3),
                                         station(neighbor_text, 22.0, 0.3, 20.0, 7.3 + separation_hz, 10.0)),
                                  noise(20.0, sigma_for_s500(0.0), seed));
            CarrierTracker tracker(kRate, ViterbiConfig{}, 5.0);
            const auto trace = run(tracker, x);
            EXPECT_LE(worst_error_hz(trace, [](double) { return 7.3; }, 3.0), 0.67);
        }
    }
}

// The tests below pin the mechanics the design describes, each on a signal where the right answer is known without a
// simulation.

TEST(Tracker, AFirstWindowNeedsAllItsSamples) {
    // A clean carrier at 8 Hz, the tracker's start 5 Hz: nothing moves until the 1 500th sample (1 s) has arrived, and
    // then the state, at the window's center 0.5 s, is on the carrier (to the 0.04% of the 3 Hz that the start's 5 Hz
    // standard deviation keeps, 1.2 mHz).
    const auto x = carrier(0.0, 3.0, 3.0, 8.0);
    CarrierTracker tracker(kRate, ViterbiConfig{}, 5.0);
    tracker.push(std::span<const Sample>(x).subspan(0, 1499), 0.0);
    EXPECT_EQ(tracker.offset_hz(0.5), 5.0);
    EXPECT_EQ(tracker.offset_hz(100.0), 5.0);
    tracker.push(std::span<const Sample>(x).subspan(1499, 1), 1499.0 / kRate);
    EXPECT_NEAR(tracker.offset_hz(0.5), 8.0, 0.01);
}

TEST(Tracker, ACleanToneIsMeasuredToUnderAMillihertz) {
    // The parabola through the peak's three bins in dB, on the Hann-windowed zero-padded transform, places a clean
    // carrier to under 1 mHz wherever it falls between the 0.18 Hz bins (0.08 mHz at most by a calculation of the same
    // estimator; 0.12 mHz at most here, with the 0.04% of the offset that the prior keeps). The carrier is within
    // 0.21 Hz of the start (more than a bin), so that the offset the filter has to explain is small, and the state is
    // read at the window's center, where it is the measurement.
    for (const double residual_hz : {0.0, 0.03, 0.06, 0.09, 0.12, 0.15, 0.18, 0.21, -0.07}) {
        SCOPED_TRACE("carrier " + std::to_string(residual_hz) + " Hz from the start");
        const auto x = carrier(0.0, 1.2, 1.2, 5.0 + residual_hz);
        CarrierTracker tracker(kRate, ViterbiConfig{}, 5.0);
        tracker.push(x, 0.0);
        EXPECT_NEAR(tracker.offset_hz(0.5), 5.0 + residual_hz, 0.001);
    }
}

TEST(Tracker, AMeasurementBelongsToTheWindowsCenter) {
    // A clean carrier sweeping 3 Hz/s, f(t) = 4 + 3 t Hz. The first window is [0 s, 1 s]; the tracker, which expects no
    // drift, measures its average frequency, the carrier's at the window's center, 0.5 s: 5.5 Hz, not the 4 Hz of the
    // window's start nor the 7 Hz of its end. The state it leaves is at that time (0.6 mHz off 5.5 Hz here).
    const auto x = carrier(0.0, 1.2, 1.2, 4.0, 1.0, 3.0);
    CarrierTracker tracker(kRate, ViterbiConfig{}, 4.0);
    tracker.push(x, 0.0);
    EXPECT_NEAR(tracker.offset_hz(0.5), 5.5, 0.005);
}

TEST(Tracker, ALoneOtherCarrierIsHeldNotFollowed) {
    // A steady carrier at 7.3 Hz and, for 100 ms at the center of the window [5 s, 6 s], another at 17.3 Hz eight times
    // as strong, which outshines the steady one in that window alone (the two windows beside it barely see it, at the
    // Hann window's ends). That peak lies 10 Hz from the prediction, outside 3 sqrt(S): it is held, and the next window
    // (the steady carrier again, inside) drops it. The oscillator never leaves the steady carrier.
    const auto x = sum_of(sum_of(carrier(0.0, 12.0, 12.0, 7.3), carrier(5.45, 5.55, 12.0, 17.3, 8.0)),
                          noise(12.0, sigma_for_s500(30.0), 1));
    CarrierTracker tracker(kRate, ViterbiConfig{}, 7.3);
    const auto trace = run(tracker, x);
    EXPECT_LE(worst_error_hz(trace, [](double) { return 7.3; }, 0.0), 0.05);
}

TEST(Tracker, TwoOffTrackMeasurementsAreANewCarrier) {
    // The carrier at 7.3 Hz is replaced at 10 s by another at 17.3 Hz. The first window that sees only the new one is
    // outside the old one's 3 sqrt(S) and is held; the next agrees with it within 1 Hz, so the filter restarts there
    // with its drift reset to 0, and the oscillator is on the new carrier before 13 s.
    const auto x = sum_of(sum_of(carrier(0.0, 10.0, 20.0, 7.3), carrier(10.0, 20.0, 20.0, 17.3)),
                          noise(20.0, sigma_for_s500(30.0), 1));
    CarrierTracker tracker(kRate, ViterbiConfig{}, 7.3);
    const auto trace = run(tracker, x);
    EXPECT_LE(worst_error_hz(trace, [](double) { return 7.3; }, 0.0, 9.0), 0.05);
    EXPECT_LE(worst_error_hz(trace, [](double) { return 17.3; }, 13.0), 0.05);
    EXPECT_NEAR(tracker.offset_hz(20.0) - tracker.offset_hz(19.0), 0.0, 0.05);
}

TEST(Tracker, AWindowWithNoLineMeasuresNothing) {
    // A single impulse at the center of the first window has a flat spectrum: its peak is no higher than its median,
    // 0 dB re the median bin, short of the 13 dB gate. The next window sees it at its very first sample, where the Hann
    // window is 0, and an all-zero window has no peak at all. The oscillator stays where it began.
    std::vector<Sample> x(3000, Sample(0.0f, 0.0f));
    x[750] = Sample(1.0f, 0.0f);
    CarrierTracker tracker(kRate, ViterbiConfig{}, 3.0);
    const auto trace = run(tracker, x);
    EXPECT_EQ(worst_error_hz(trace, [](double) { return 3.0; }, 0.0), 0.0);
}

TEST(Tracker, SplittingTheInputChangesNothing) {
    // Whatever the blocks, the same samples give the same oscillator, bit for bit: the windows are cut from the stream,
    // not from the blocks, and the clock counts samples. The reference is the whole signal (18 000 samples) in one
    // push; blocks of 7 and 4 501 samples straddle the hops, and blocks of 1 complete a window with a single sample.
    const std::string text = text_for("PARIS", 25.0, 11.0);
    const auto x = signals::keyed_signal(text, 25.0, kRate, 12.0, 7.3, 1.0, sigma_for_s500(5.0), 7);
    CarrierTracker reference(kRate, ViterbiConfig{}, 5.0);
    reference.push(x, 0.0);
    for (const std::size_t block : {1u, 7u, 750u, 1500u, 4501u}) {
        SCOPED_TRACE("blocks of " + std::to_string(block));
        CarrierTracker split(kRate, ViterbiConfig{}, 5.0);
        for (std::size_t first = 0; first < x.size(); first += block)
            split.push(std::span<const Sample>(x).subspan(first, std::min(block, x.size() - first)),
                       static_cast<double>(first) / kRate);
        for (const double t_s : {0.0, 6.0, 12.0, 20.0}) EXPECT_EQ(split.offset_hz(t_s), reference.offset_hz(t_s));
    }
}

TEST(Tracker, RefusesWhatItCannotRun) {
    const ViterbiConfig config;
    EXPECT_THROW(CarrierTracker(0.0, config, 0.0), std::invalid_argument);
    EXPECT_THROW(CarrierTracker(-kRate, config, 0.0), std::invalid_argument);
    EXPECT_THROW(CarrierTracker(kNaN, config, 0.0), std::invalid_argument);
    EXPECT_THROW(CarrierTracker(kRate, config, kNaN), std::invalid_argument);
    EXPECT_THROW(CarrierTracker(kRate, config, kInf), std::invalid_argument);

    const auto refused = [&](auto set) {
        ViterbiConfig bad;
        set(bad);
        EXPECT_THROW(CarrierTracker(kRate, bad, 0.0), std::invalid_argument);
    };
    refused([](ViterbiConfig& c) { c.tracker_window_s = 0.0; });
    refused([](ViterbiConfig& c) { c.tracker_window_s = kNaN; });
    refused([](ViterbiConfig& c) { c.tracker_window_s = 6.0; });  // 9 000 samples: more than the 8 192 points
    refused([](ViterbiConfig& c) { c.tracker_hop_s = 0.0; });
    refused([](ViterbiConfig& c) { c.tracker_hop_s = 0.0001; });  // under half a sample
    refused([](ViterbiConfig& c) { c.tracker_hop_s = kInf; });
    refused([](ViterbiConfig& c) { c.tracker_search_hz = 0.0; });
    refused([](ViterbiConfig& c) { c.tracker_search_hz = -15.0; });
    refused([](ViterbiConfig& c) { c.tracker_search_hz = 0.1; });  // under one bin of 0.18 Hz
    refused([](ViterbiConfig& c) { c.tracker_search_hz = 750.0; });  // half the sample rate: no neighbors left
    refused([](ViterbiConfig& c) { c.tracker_gate_db = kNaN; });
    refused([](ViterbiConfig& c) { c.tracker_measurement_sd_hz = 0.0; });
    refused([](ViterbiConfig& c) { c.tracker_drift_sd_hz_per_s = -0.1; });
    ViterbiConfig no_drift_noise;
    no_drift_noise.tracker_drift_sd_hz_per_s = 0.0;  // a drift that never changes is a model, not an error
    EXPECT_NO_THROW(CarrierTracker(kRate, no_drift_noise, 0.0));

    // A block must continue the previous one (to within half a sample), and the first block's time is the clock.
    const std::vector<Sample> block(100, Sample(0.0f, 0.0f));
    CarrierTracker tracker(kRate, config, 0.0);
    EXPECT_NO_THROW(tracker.push({}, 123.0));  // an empty block is nothing, whatever its time
    EXPECT_THROW(tracker.push(block, kNaN), std::invalid_argument);
    EXPECT_NO_THROW(tracker.push(block, 10.0));
    EXPECT_THROW(tracker.push(block, 10.0), std::invalid_argument);  // 100 samples later is 10.0667 s, not 10 s
    EXPECT_THROW(tracker.push(block, 10.0 + 100.0 / kRate + 0.6 / kRate), std::invalid_argument);
    EXPECT_NO_THROW(tracker.push(block, 10.0 + 100.0 / kRate + 0.4 / kRate));  // within half a sample
    EXPECT_NO_THROW(tracker.push(block, 10.0 + 200.0 / kRate));
}
