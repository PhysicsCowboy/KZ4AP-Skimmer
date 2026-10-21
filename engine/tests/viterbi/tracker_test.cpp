#include "kz4ap/viterbi/tracker.hpp"

#include "../test_signals.hpp"
#include "kz4ap/channelizer.hpp"
#include "kz4ap/types.hpp"
#include "kz4ap/viterbi/config.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numbers>
#include <optional>
#include <random>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using kz4ap::Sample;
using kz4ap::viterbi::CarrierTracker;
using kz4ap::viterbi::ViterbiConfig;
namespace signals = kz4ap::test;
using Outcome = CarrierTracker::Outcome;

namespace {

constexpr double kRate = 1500.0;  // channel samples/s
constexpr double kInf = std::numeric_limits<double>::infinity();
constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
constexpr double kPi = std::numbers::pi;

// The design's values, restated here so that the tests compute what the tracker should do on their own.
constexpr double kNoiseBandwidthHz = 1.5;      // Hz: the 1 s Hann window's noise bandwidth, 1.5 / T
constexpr double kToleranceHz = 0.26 / 0.390;  // Hz: the trust rule's, 0.26 cycles over the default 390 ms block
constexpr double kVarianceScaleHz2 = 0.2833;   // Hz^2: the variance of a measurement is this over the line's SNR
constexpr double kBiasFloorHz = 0.08e-3;       // Hz: the floor of a measurement's standard deviation
constexpr double kGateRatio = 7.943282347242815;  // 10^(9 dB / 10): the gate, re the mean noise power per bin
constexpr double kCorrelation = -0.35;         // the correlation of two consecutive windows' errors
constexpr double kDriftSdHzPerS = 0.3;         // Hz/s: tracker_drift_sd_hz_per_s
constexpr double kDriftNoise = 1.5e-4;         // Hz^2/s^3: tracker_drift_noise

// The moving-average coefficient whose lag-one correlation is kCorrelation: theta / (1 + theta^2) = rho, |theta| < 1.
double theta() { return (1.0 - std::sqrt(1.0 - 4.0 * kCorrelation * kCorrelation)) / (2.0 * kCorrelation); }

// The fill factor and the line's centroid (s from the window's center) that the design gives a 1 s window whose line
// has the amplitude amplitude[i] (FS) at sample i, all of it at the measured frequency, under the hiss density n0
// (FS^2/Hz): in each quarter j the line's coherent sum sum_j w_i a_i, less the noise's expected power N0 r sum_j w_i^2,
// gives the level sqrt(...) / sum_j w_i; with the weights g_i = w_i x level,
//     F = (sum g)^2 sum w^2 (tau - u)^2 / (sum w^2 (sum g (tau - u)^2)^2),   u = sum g tau / sum g,
// and the factor is F over a filled window's F.
struct Profile {
    double factor;
    double centroid_s;
};

Profile profile_of(const Sample* x, double n0) {
    constexpr int kN = 1500;
    std::vector<double> w(kN);
    std::vector<double> tau(kN);
    for (int i = 0; i < kN; ++i) {
        w[static_cast<std::size_t>(i)] = 0.5 - 0.5 * std::cos(2.0 * kPi * i / kN);
        tau[static_cast<std::size_t>(i)] = (i - kN / 2) / kRate;
    }
    const auto f_of = [&](const std::vector<double>& level) {
        double g0 = 0.0, g1 = 0.0, w2 = 0.0;
        for (std::size_t i = 0; i < w.size(); ++i) {
            g0 += w[i] * level[i];
            g1 += w[i] * level[i] * tau[i];
            w2 += w[i] * w[i];
        }
        const double u = g1 / g0;
        double g2 = 0.0, noise = 0.0;
        for (std::size_t i = 0; i < w.size(); ++i) {
            g2 += w[i] * level[i] * (tau[i] - u) * (tau[i] - u);
            noise += w[i] * w[i] * (tau[i] - u) * (tau[i] - u);
        }
        return Profile{g0 * g0 * noise / (w2 * g2 * g2), u};
    };
    std::vector<double> level(kN);
    for (int j = 0; j < 4; ++j) {
        double sum = 0.0, sw = 0.0, sw2 = 0.0;
        for (int i = j * kN / 4; i < (j + 1) * kN / 4; ++i) {
            const auto k = static_cast<std::size_t>(i);
            sum += w[k] * std::abs(std::complex<double>(x[i].real(), x[i].imag()));
            sw += w[k];
            sw2 += w[k] * w[k];
        }
        const double value = std::sqrt(std::max(sum * sum - n0 * kRate * sw2, 0.0)) / sw;
        for (int i = j * kN / 4; i < (j + 1) * kN / 4; ++i) level[static_cast<std::size_t>(i)] = value;
    }
    if (*std::max_element(level.begin(), level.end()) <= 0.0) return {1.0, 0.0};  // no quarter above its noise
    const Profile full = f_of(std::vector<double>(kN, 1.0));
    const Profile got = f_of(level);
    return {got.factor / full.factor, got.centroid_s};
}

// FS: the total (complex) noise sigma on a 1 500 samples/s stream that puts a key-down carrier of amplitude 1 FS at
// s500_db dB S500 (key-down carrier power over the noise power in 500 Hz): S500 = 1 / (sigma^2 x 500 Hz / 1500 Hz).
double sigma_for_s500(double s500_db) { return std::sqrt(3.0 / std::pow(10.0, s500_db / 10.0)); }

// FS^2/Hz: the noise density of that stream, N0 = sigma^2 / 1500 Hz, what the hiss estimator would report.
double n0_for_s500(double s500_db) { return sigma_for_s500(s500_db) * sigma_for_s500(s500_db) / kRate; }

// FS^2/Hz: the density that puts a line of power line_fs2 (FS^2) at `snr` times the mean noise power per bin.
double n0_for_line_snr(double line_fs2, double snr) { return line_fs2 / (snr * kNoiseBandwidthHz); }

// Hz^2: a measurement's variance for a peak at `snr` times the mean noise power per bin, with the line's profile.
double variance_for(double snr, const Profile& profile) {
    const double attribution_hz = kDriftSdHzPerS * profile.centroid_s;
    return kVarianceScaleHz2 / (snr - 1.0) * profile.factor + attribution_hz * attribution_hz +
           kBiasFloorHz * kBiasFloorHz;
}

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

// The tracker as the decoder uses it: the samples in blocks of `block` (the first sample at 0 s) with the hiss density
// n0 (FS^2/Hz), and after each block the oscillator's offset at the block's first and last samples, which is what
// re-centers them.
std::vector<Point> run(CarrierTracker& tracker, const std::vector<Sample>& x, double n0, std::size_t block = 30) {
    std::vector<Point> trace;
    for (std::size_t first = 0; first < x.size(); first += block) {
        const std::size_t n = std::min(block, x.size() - first);
        tracker.push(std::span<const Sample>(x).subspan(first, n), static_cast<double>(first) / kRate, n0);
        for (const std::size_t i : {first, first + n - 1}) {
            const double t = static_cast<double>(i) / kRate;
            trace.push_back({t, tracker.offset_hz(t)});
        }
    }
    return trace;
}

// Pushes samples [first, last) of x in one block with the density n0.
void push_range(CarrierTracker& tracker, const std::vector<Sample>& x, std::size_t first, std::size_t last, double n0) {
    tracker.push(std::span<const Sample>(x).subspan(first, last - first), static_cast<double>(first) / kRate, n0);
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

// The five tests below are the plan's, with the owner's changes of 2026-10-20 (the decoder's tolerance for
// HoldsAKeyedCarrier, a physical drift of 0.3 Hz/s for FollowsDrift). Each runs several noise seeds, since the standard
// library's normal distribution differs between platforms and a test on one realization would pass or fail by luck of
// the platform. Measured over 1 000 seeds at the tests' own 0.6667 Hz (MSVC; tracker.hpp, "Measured"), the share of
// seeds that meet each: HoldsAKeyedCarrier 99.9%, FollowsDrift 100%, StaysPutOnNoise 99.2%, RelocksAtATurnover 100%,
// ResistsAStrongNeighbor 72.3% and 72.4% (neighbor above and below). HoldsAKeyedCarrier, FollowsDrift and
// RelocksAtATurnover run 5 seeds each: at the measured 1 failing seed in 1 000, a run of HoldsAKeyedCarrier's 5 seeds
// on another platform's random numbers fails about once in 200 (1 - 0.999^5), and the other two failed no seed in
// 1 000. StaysPutOnNoise holds a count condition over 20 seeds (below).
//
// ResistsAStrongNeighbor is a strict expected failure (tracker.hpp, "Limitations"): it runs enough seeds that some fail
// on any platform; it fails if they all pass, so that the marker is removed once the limitation is gone, and if more
// fail than its measured rate allows, a regression; it is skipped otherwise.

TEST(Tracker, HoldsAKeyedCarrier) {
    // PARIS at 25 WPM, S500 0 dB, 7.3 Hz from the channel's center; the tracker starts 2.3 Hz off, at the anchor 5 Hz.
    // From 3 s on it must stay within the decoder's tolerance, 0.667 Hz (0.26 cycles over its default 390 ms block).
    const std::string text = text_for("PARIS", 25.0, 19.0);
    for (const unsigned seed : {1u, 2u, 3u, 4u, 5u}) {
        SCOPED_TRACE("seed " + std::to_string(seed));
        const auto x = signals::keyed_signal(text, 25.0, kRate, 20.0, 7.3, 1.0, sigma_for_s500(0.0), seed);
        CarrierTracker tracker(kRate, ViterbiConfig{}, 5.0);
        const auto trace = run(tracker, x, n0_for_s500(0.0));
        EXPECT_LE(worst_error_hz(trace, [](double) { return 7.3; }, 3.0), kToleranceHz);
    }
}

TEST(Tracker, FollowsDrift) {
    // A carrier drifting 0.3 Hz/s (the older rigs' specified warm-up drift, at most) for 20 s, from -5 Hz to +1 Hz,
    // PARIS at 25 WPM, S500 +10 dB; the anchor is 3 Hz off. Within 0.667 Hz from 3 s on.
    const std::string text = text_for("PARIS", 25.0, 19.0);
    const auto truth = [](double t_s) { return -5.0 + 0.3 * t_s; };
    for (const unsigned seed : {1u, 2u, 3u, 4u, 5u}) {
        SCOPED_TRACE("seed " + std::to_string(seed));
        const auto x = signals::keyed_signal(text, 25.0, kRate, 20.0, -5.0, 1.0, sigma_for_s500(10.0), seed, 0.5, 0.3);
        CarrierTracker tracker(kRate, ViterbiConfig{}, -2.0);
        const auto trace = run(tracker, x, n0_for_s500(10.0));
        EXPECT_LE(worst_error_hz(trace, truth, 3.0), kToleranceHz);
        // The oscillator runs at the carrier's rate between updates: its slope over the last second is the drift.
        EXPECT_NEAR(tracker.offset_hz(20.0) - tracker.offset_hz(19.0), 0.3, 0.05);
    }
}

TEST(Tracker, StaysPutOnNoise) {
    // 60 s of noise and no station: the oscillator stays within +/-15 Hz of where it began on every seed, and within
    // 2 Hz rms on all but a few. A false restart (two consecutive noise peaks that agree) moves it on 0.8% of seeds
    // (measured, 8 of 1 000); of 20 seeds, at most 2 may then be over 2 Hz rms. The binomial tail fails that with
    // probability 5.3e-4 on any platform's noise (P(X >= 3) for X ~ B(20, 0.008)).
    unsigned over = 0;
    for (unsigned seed = 1; seed <= 20; ++seed) {
        SCOPED_TRACE("seed " + std::to_string(seed));
        const auto x = noise(60.0, sigma_for_s500(0.0), seed);
        CarrierTracker tracker(kRate, ViterbiConfig{}, 3.0);
        const auto trace = run(tracker, x, n0_for_s500(0.0));
        double sum_squares = 0.0;
        double worst = 0.0;
        for (const Point& p : trace) {
            sum_squares += (p.offset_hz - 3.0) * (p.offset_hz - 3.0);
            worst = std::max(worst, std::abs(p.offset_hz - 3.0));
        }
        EXPECT_LE(worst, 15.0);
        over += std::sqrt(sum_squares / static_cast<double>(trace.size())) > 2.0 ? 1u : 0u;
    }
    EXPECT_LE(over, 2u);
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
        const auto trace = run(tracker, x, n0_for_s500(10.0));
        EXPECT_LE(worst_error_hz(trace, [](double) { return 2.0; }, 3.0, a_end_s), kToleranceHz);
        EXPECT_LE(worst_error_hz(trace, [](double) { return 10.0; }, b_first_mark_s + 2.0), kToleranceHz);
    }
}

TEST(Tracker, ResistsAStrongNeighbor) {
    // The weak station: PARIS at 25 WPM, S500 0 dB, 7.3 Hz from the center; the tracker starts at the anchor 5 Hz. The
    // neighbor: 20 dB stronger (S500 +20 dB, ten times the amplitude), 50 Hz above or below it, another text at another
    // speed. The hiss density is the noise's own: the neighbor's keying sidebands are not in it.
    const std::string weak_text = text_for("PARIS", 25.0, 19.0);
    const std::string neighbor_text = text_for("CQ TEST DE KZ4AP", 22.0, 19.0);
    // 2 x 20 runs. Measured, 27.7% of runs fail (1 000 seeds each way, at 0.6667 Hz): all 40 pass with probability
    // 2.3e-6, and more than 20 fail with probability 7.9e-4 (the binomial tail of B(40, 0.277)), which is a regression.
    constexpr unsigned kSeeds = 20;
    constexpr unsigned kMostFailures = 20;
    unsigned failures = 0;
    double worst = 0.0;
    for (const double separation_hz : {50.0, -50.0}) {
        for (unsigned seed = 1; seed <= kSeeds; ++seed) {
            const auto x = sum_of(sum_of(station(weak_text, 25.0, 0.5, 20.0, 7.3),
                                         station(neighbor_text, 22.0, 0.3, 20.0, 7.3 + separation_hz, 10.0)),
                                  noise(20.0, sigma_for_s500(0.0), seed));
            CarrierTracker tracker(kRate, ViterbiConfig{}, 5.0);
            const double error = worst_error_hz(run(tracker, x, n0_for_s500(0.0)), [](double) { return 7.3; }, 3.0);
            failures += error > kToleranceHz ? 1u : 0u;
            worst = std::max(worst, error);
        }
    }
    if (failures == 0)
        FAIL() << "the known limitation is gone (all " << 2 * kSeeds << " runs within 0.667 Hz from 3 s on): run a few "
               << "seeds and remove the expected-failure marker here and the limitation from tracker.hpp";
    if (failures > kMostFailures)
        FAIL() << "a regression: " << failures << " of " << 2 * kSeeds << " runs exceed 0.667 Hz from 3 s on (the "
               << "worst " << worst << " Hz), more than the " << kMostFailures
               << " that the measured 27.7% per run allows";
    GTEST_SKIP() << "known limitation (tracker.hpp, \"Limitations\"): " << failures << " of " << 2 * kSeeds
                 << " runs exceed 0.667 Hz from 3 s on (the worst " << worst << " Hz). The neighbor's keying puts "
                 << "sideband lines into the weak station's search range: alone, their largest stands 10 to 16 dB "
                 << "above the mean noise power per bin (10th to 90th percentile), against about 17 dB re the same for "
                 << "the weak line. Most often their energy near the weak line pulls one of its measurements off by "
                 << "half a hertz or more, far beyond its variance; inside the acceptance window it updates the filter "
                 << "and kicks the drift. Less often, while the track is unconfirmed (at the start, or after a hold), "
                 << "two windows in a row find the same sideband line, agree, and the track moves there.";
}

// The tests below pin the design's parts: the gate against the noise floor, the measurement's variance and its fill
// factor, the trust rule and its hold, the agreement test, the acceptance window and its cap, the search inside it, and
// the fixed tolerance. Each uses a signal on which the right answer is computed here, independently of the tracker.

TEST(Tracker, TheNoisePowerPerBinIsTheHissDensityTimesTheWindowsNoiseBandwidth) {
    // In the normalization in which a tone of amplitude A reads A^2, one bin of white noise of density N0 (FS^2/Hz)
    // holds N0 r sum(w^2) / sum(w)^2 = N0 x 1.5 Hz for the periodic Hann window of 1 500 samples at r = 1 500
    // samples/s (sum w = N / 2, sum w^2 = 3 N / 8). The zero padding does not change it.
    double sum_w = 0.0;
    double sum_w2 = 0.0;
    for (int i = 0; i < 1500; ++i) {
        const double w = 0.5 - 0.5 * std::cos(2.0 * kPi * i / 1500.0);
        sum_w += w;
        sum_w2 += w * w;
    }
    const double bandwidth_hz = kRate * sum_w2 / (sum_w * sum_w);
    EXPECT_NEAR(bandwidth_hz, kNoiseBandwidthHz, 1e-12);
    CarrierTracker tracker(kRate, ViterbiConfig{}, 0.0);
    for (const double n0 : {1e-12, 2e-3, 0.37}) {
        SCOPED_TRACE("N0 " + std::to_string(n0) + " FS^2/Hz");
        EXPECT_NEAR(tracker.noise_power_per_bin_fs2(n0), n0 * bandwidth_hz, 1e-12 * n0);
    }
    EXPECT_EQ(tracker.noise_power_per_bin_fs2(0.0), 0.0);
}

TEST(Tracker, TheChannelsNoisePerBinIsTheWidebandDensityTimesOnePointFiveHertz) {
    // White noise at 48 kHz of sigma 0.02 FS, so N0 = sigma^2 / 48 000 Hz (FS^2/Hz), the density the hiss estimator
    // measures, through the engine's channelizer (2 048 bins, a channel of 64 bins at 1 500 samples/s, cutoff 150 Hz).
    // Disjoint 1 s windows of the channel, Hann-windowed and transformed by a direct sum at 27 frequencies 2 Hz apart
    // within +/-26 Hz of the channel's center (nearly independent bins): their mean power, a tone of amplitude A
    // reading A^2, is the tracker's noise power per bin, N0 x 1.5 Hz, within the simulation's error (about 1.1% here;
    // the channel filter itself passes these frequencies within 0.003 dB of its gain at 0 Hz, by its taps).
    const kz4ap::ChannelizerConfig config{.sample_rate = 48000, .fft_size = 2048, .channel_bins = 64};
    kz4ap::Channelizer channelizer(config);
    ASSERT_DOUBLE_EQ(channelizer.output_rate(), kRate);
    channelizer.add_channel(0, channelizer.hz_to_bin(3000.0));
    const double sigma = 0.02;
    const double n0 = sigma * sigma / 48000.0;
    std::mt19937 rng(20261020);
    std::normal_distribution<double> gauss(0.0, sigma / std::sqrt(2.0));
    std::vector<Sample> channel;
    const auto sink = [&channel](std::uint32_t, std::uint64_t, std::span<const Sample> samples) {
        channel.insert(channel.end(), samples.begin(), samples.end());
    };
    constexpr int kWindows = 300;
    std::vector<Sample> input(48000);
    while (channel.size() < static_cast<std::size_t>(kWindows + 2) * 1500) {
        for (Sample& s : input) s = Sample(static_cast<float>(gauss(rng)), static_cast<float>(gauss(rng)));
        channelizer.push(input, sink);
    }
    // The Hann window times each frequency's complex exponential, once.
    double sum_w = 0.0;
    std::vector<std::vector<std::complex<double>>> kernels;
    for (int f = -26; f <= 26; f += 2) {
        std::vector<std::complex<double>> kernel(1500);
        for (int i = 0; i < 1500; ++i) {
            const double w = 0.5 - 0.5 * std::cos(2.0 * kPi * i / 1500.0);
            if (f == 0) sum_w += w;
            kernel[static_cast<std::size_t>(i)] = w * std::polar(1.0, -2.0 * kPi * f * i / kRate);
        }
        kernels.push_back(std::move(kernel));
    }
    double total = 0.0;
    int bins = 0;
    for (int k = 1; k <= kWindows; ++k) {  // skips the first second, while the channelizer's history fills
        const Sample* x = channel.data() + static_cast<std::size_t>(k) * 1500;
        for (const auto& kernel : kernels) {
            std::complex<double> sum(0.0, 0.0);
            for (std::size_t i = 0; i < 1500; ++i) sum += kernel[i] * std::complex<double>(x[i].real(), x[i].imag());
            total += std::norm(sum) / (sum_w * sum_w);
            ++bins;
        }
    }
    const double mean_fs2 = total / bins;
    CarrierTracker tracker(kRate, ViterbiConfig{}, 0.0);
    EXPECT_NEAR(mean_fs2 / tracker.noise_power_per_bin_fs2(n0), 1.0, 0.05);
}

TEST(Tracker, TheGateMeasuresAPeakNineDecibelsAboveTheNoisePerBin) {
    // A clean carrier of amplitude 0.5 FS exactly at the start's frequency, at a bin's center: its peak bin reads
    // 0.25 FS^2. With the hiss density that puts it 1.0001 times the gate (9 dB re the mean noise power per bin) above
    // the noise per bin, every window is measured: the first is kept, the second agrees and the filter restarts. With
    // 0.9999 times the gate, nothing is measured.
    const auto x = carrier(0.0, 3.0, 3.0, 4.0, 0.5);
    for (const double margin : {1.0001, 0.9999}) {
        SCOPED_TRACE("peak at " + std::to_string(margin) + " times the gate");
        CarrierTracker tracker(kRate, ViterbiConfig{}, 4.0);
        const double n0 = n0_for_line_snr(0.25, kGateRatio * margin);
        push_range(tracker, x, 0, 1500, n0);
        EXPECT_NEAR(tracker.last_window().peak_fs2, 0.25, 0.25e-6);
        EXPECT_NEAR(tracker.last_window().noise_per_bin_fs2, n0 * kNoiseBandwidthHz, 1e-15);
        push_range(tracker, x, 1500, 2250, n0);
        if (margin > 1.0) {
            EXPECT_EQ(tracker.counts().not_measured, 0u);
            EXPECT_EQ(tracker.counts().restarted, 1u);
            EXPECT_EQ(tracker.last_window().outcome, Outcome::Restarted);
        } else {
            EXPECT_EQ(tracker.counts().not_measured, 2u);
            EXPECT_EQ(tracker.last_window().outcome, Outcome::NotMeasured);
        }
    }
}

TEST(Tracker, TheMeasurementVarianceComesFromThePeaksSignalToNoiseRatio) {
    // The Cramer-Rao form, var = c / rho, rho the line's power over the mean noise power per bin, estimated from the
    // peak as its power over the noise per bin less 1 (the noise's own share), with c = 0.2833 Hz^2 fitted to the first
    // design's measured scatter; times the fill factor of the line's presence profile, plus the drift's share of
    // attributing the measurement to the window's center (both computed here by profile_of), and a floor of
    // (0.08 mHz)^2 for the parabola's own bias. A clean carrier at a bin's center has a known peak, 0.25 FS^2 here, so
    // rho is set exactly by the hiss density; filling the window, its profile is flat but for the noise the tracker is
    // told to expect, which the profile subtracts though none is there: a factor of 1.17 at 8 times the noise per bin,
    // 1.03 at 30, 1.0007 at 1 000 (the outer quarters, of small window weight, lose the larger share).
    const auto x = carrier(0.0, 1.2, 1.2, 4.0, 0.5);
    for (const double snr : {8.0, 30.0, 1000.0, 1e9}) {
        SCOPED_TRACE("peak at " + std::to_string(snr) + " times the noise per bin");
        CarrierTracker tracker(kRate, ViterbiConfig{}, 4.0);
        const double n0 = n0_for_line_snr(0.25, snr);
        push_range(tracker, x, 0, 1500, n0);
        ASSERT_EQ(tracker.last_window().outcome, Outcome::Pending);
        const Profile profile = profile_of(x.data(), n0);
        EXPECT_NEAR(tracker.last_window().fill_factor, profile.factor, 1e-6 * profile.factor);
        EXPECT_GE(profile.factor, 1.0);
        EXPECT_LE(profile.factor, 1.2);
        EXPECT_NEAR(tracker.last_window().variance_hz2, variance_for(snr, profile), 1e-5 * variance_for(snr, profile));
        EXPECT_NEAR(tracker.last_window().measured_hz, 4.0, 1e-4);
    }
}

TEST(Tracker, APartlyFilledWindowGetsItsDerivedFillFactor) {
    // A station that starts halfway through a window fills only its second half. Its peak is broad, and at the same
    // signal-to-noise ratio its frequency is 20 times as uncertain as a filled window's (derived: F of the weights
    // g = w on the second half only, over F of the whole window; 20.04 for a start at exactly half the window). The
    // quarters' coherent sums show the profile, and the measurement's variance takes the factor, so that this window
    // cannot set a restart's drift. Its line's centroid lies 0.148 s after the window's center, which the variance's
    // drift share also takes.
    const auto x = carrier(0.5, 3.0, 3.0, 4.0);
    CarrierTracker tracker(kRate, ViterbiConfig{}, 4.0);
    const double n0 = 1e-9;
    push_range(tracker, x, 0, 1500, n0);
    ASSERT_EQ(tracker.last_window().outcome, Outcome::Pending);
    const Profile profile = profile_of(x.data(), n0);
    EXPECT_NEAR(profile.factor, 20.0, 0.5);
    EXPECT_NEAR(profile.centroid_s, 0.148, 0.002);
    EXPECT_NEAR(tracker.last_window().fill_factor, profile.factor, 1e-6 * profile.factor);
    const double snr = tracker.last_window().peak_fs2 / tracker.last_window().noise_per_bin_fs2;
    EXPECT_NEAR(tracker.last_window().variance_hz2, variance_for(snr, profile), 1e-6 * variance_for(snr, profile));
    // The next window, [0.5 s, 1.5 s], is filled: its factor is about 1.
    push_range(tracker, x, 1500, 2250, n0);
    EXPECT_NEAR(tracker.last_window().fill_factor, 1.0, 0.01);
}

namespace {

// A clean carrier drifting 0.2 Hz/s from 4 Hz, present for t in [0 s, 1.5 s]: two filled windows, centered at 0.5 s
// and 1.0 s, each with the line at `snr` times the noise per bin, which agree and restart the filter at 1.0 s. Returns
// after the second window; the signal is returned too, for the third window ([1 s, 2 s], half of the line).
struct TwoWindows {
    std::vector<Sample> x;  // the signal
    double n0;              // FS^2/Hz: the hiss density the tracker is told
    double z1_hz, z2_hz;    // the two measurements
    double r1_hz2, r2_hz2;  // their variances
};

TwoWindows two_windows(CarrierTracker& tracker, double snr) {
    TwoWindows w{carrier(0.0, 1.5, 3.0, 4.0, 1.0, 0.2), n0_for_line_snr(1.0, snr), 0.0, 0.0, 0.0, 0.0};
    push_range(tracker, w.x, 0, 1500, w.n0);
    w.z1_hz = tracker.last_window().measured_hz;
    w.r1_hz2 = tracker.last_window().variance_hz2;
    push_range(tracker, w.x, 1500, 2250, w.n0);
    w.z2_hz = tracker.last_window().measured_hz;
    w.r2_hz2 = tracker.last_window().variance_hz2;
    EXPECT_EQ(tracker.last_window().outcome, Outcome::Restarted);
    return w;
}

// Hz^2: the variance of the offset predicted dt seconds after a restart from two consecutive measurements 0.5 s apart,
// of variances r1 and r2 (Hz^2) and covariance c12 = -0.35 sqrt(r1 r2): the two-point estimate (f = z2, drift
// (z2 - z1) / 0.5 s), P = [[r2, (r2 - c12) / g], [(r2 - c12) / g, (r1 + r2 - 2 c12) / g^2]], carried by the
// constant-drift model with the drift's process noise.
double restart_variance_hz2(double r1, double r2, double dt) {
    const double c12 = kCorrelation * std::sqrt(r1 * r2);
    const double p00 = r2;
    const double p01 = (r2 - c12) / 0.5;
    const double p11 = (r1 + r2 - 2.0 * c12) / 0.25;
    return p00 + 2.0 * dt * p01 + dt * dt * p11 + kDriftNoise * dt * dt * dt / 3.0;
}

}  // namespace

TEST(Tracker, TheTrustRuleSwitchesWhereThreeSigmaReachesTheTolerance) {
    // After the restart at 1.0 s, the third window's center is 0.5 s later. Its offset's predicted standard deviation
    // sigma_f, from the two-point estimate, puts 3 sigma_f under the decoder's tolerance, 0.26 / 0.390 s = 0.667 Hz,
    // for a line at 50 times the noise per bin (about 0.58 Hz), and over it at 30 (about 0.75 Hz). The track is
    // confirmed in the first case and not in the second.
    for (const double snr : {50.0, 30.0}) {
        SCOPED_TRACE("line at " + std::to_string(snr) + " times the noise per bin");
        CarrierTracker tracker(kRate, ViterbiConfig{}, 4.0);
        const TwoWindows w = two_windows(tracker, snr);
        const double sigma_f = std::sqrt(restart_variance_hz2(w.r1_hz2, w.r2_hz2, 0.5));
        EXPECT_NEAR(tracker.frequency_sd_hz(1.5), sigma_f, 1e-9);
        push_range(tracker, w.x, 2250, 3000, w.n0);
        EXPECT_NEAR(tracker.last_window().frequency_sd_hz, sigma_f, 1e-9);
        EXPECT_EQ(tracker.last_window().confirmed, snr > 40.0);
        EXPECT_EQ(3.0 * sigma_f <= kToleranceHz, snr > 40.0);
        EXPECT_EQ(tracker.counts().unconfirmed, snr > 40.0 ? 2u : 3u);
    }
}

TEST(Tracker, AnUnconfirmedTrackHoldsItsFrequency) {
    // The same two windows, the line at 25 times the noise per bin (a variance of about 0.012 Hz^2 each). After the
    // restart at 1.0 s the oscillator runs on at the measured drift while 3 sigma_f <= 0.667 Hz, until about 0.34 s
    // later, and from then on it holds its frequency: no drift extrapolation while the track is unconfirmed. The third
    // window holds a quarter of the line's power, under the gate.
    CarrierTracker tracker(kRate, ViterbiConfig{}, 4.0);
    const TwoWindows w = two_windows(tracker, 25.0);
    push_range(tracker, w.x, 2250, 3000, w.n0);
    EXPECT_EQ(tracker.last_window().outcome, Outcome::NotMeasured);
    const double drift = (w.z2_hz - w.z1_hz) / 0.5;
    EXPECT_NEAR(drift, 0.2, 0.01);
    // The hold's time, found here by bisection on the two-point estimate's variance.
    double low = 0.0;
    double high = 5.0;
    const double limit = (kToleranceHz / 3.0) * (kToleranceHz / 3.0);
    for (int i = 0; i < 200; ++i) {
        const double middle = 0.5 * (low + high);
        (restart_variance_hz2(w.r1_hz2, w.r2_hz2, middle) <= limit ? low : high) = middle;
    }
    const double hold_s = 1.0 + low;
    EXPECT_NEAR(hold_s, 1.34, 0.03);
    for (const double t : {1.0, 1.1, 1.3, hold_s - 1e-6})
        EXPECT_NEAR(tracker.offset_hz(t), w.z2_hz + drift * (t - 1.0), 1e-9);
    const double held = w.z2_hz + drift * (hold_s - 1.0);
    for (const double t : {hold_s + 1e-6, 1.5, 2.0, 30.0, 1000.0}) EXPECT_NEAR(tracker.offset_hz(t), held, 1e-6);
    EXPECT_FALSE(tracker.last_window().confirmed);
}

TEST(Tracker, TwoMeasurementsAgreeWithinThreeSigmas) {
    // Windows of 1 s every 1 s (tracker_hop_s = 1 s), so that consecutive windows share no sample: a clean carrier at
    // 4.2 Hz fills the first window, [0 s, 1 s], and another Delta Hz away fills the second, [1 s, 2 s] (their 5 ms
    // edges fall where the Hann window is 0). A short burst would not do: its main lobe is broad, and the other burst's
    // leakage moves its vertex by tenths of a hertz. The track is unconfirmed at the start, so the second measurement
    // must agree with the first: |Delta| <= 3 sqrt(r1 + r2 + (sigma_d x 1 s)^2), sigma_d = tracker_drift_sd_hz_per_s =
    // 0.3 Hz/s, r1 and r2 the measurements' variances (about 1e-8 Hz^2 here). Agreeing, the filter restarts at the
    // second carrier with the drift Delta / 1 s; otherwise the second waits for the next window.
    ViterbiConfig config;
    config.tracker_hop_s = 1.0;
    const double n0 = 1e-8;  // FS^2/Hz: the carriers 6.7e7 times the noise per bin
    const auto run_pair = [&](double delta_hz, CarrierTracker& tracker) {
        const auto x = sum_of(carrier(0.0, 1.0, 3.0, 4.2), carrier(1.0, 2.0, 3.0, 4.2 + delta_hz));
        push_range(tracker, x, 0, 1500, n0);
        EXPECT_EQ(tracker.last_window().outcome, Outcome::Pending);
        EXPECT_NEAR(tracker.last_window().measured_hz, 4.2, 1e-4);
        const double r1 = tracker.last_window().variance_hz2;
        push_range(tracker, x, 1500, 3000, n0);
        return r1;
    };
    // The limit, from the variances of a first run.
    CarrierTracker probe(kRate, config, 4.0);
    const double r1 = run_pair(0.0, probe);
    const double r2 = probe.last_window().variance_hz2;
    const double limit_hz = 3.0 * std::sqrt(r1 + r2 + (0.3 * 1.0) * (0.3 * 1.0));
    EXPECT_NEAR(probe.last_window().agreement_hz, limit_hz, 1e-12);
    EXPECT_NEAR(limit_hz, 0.9, 1e-6);
    for (const double delta_hz : {limit_hz - 0.01, -(limit_hz - 0.01), limit_hz + 0.01, -(limit_hz + 0.01)}) {
        SCOPED_TRACE("carriers " + std::to_string(delta_hz) + " Hz apart");
        CarrierTracker tracker(kRate, config, 4.0);
        run_pair(delta_hz, tracker);
        if (std::abs(delta_hz) < limit_hz) {
            EXPECT_EQ(tracker.last_window().outcome, Outcome::Restarted);
            EXPECT_NEAR(tracker.offset_hz(1.5), 4.2 + delta_hz, 1e-4);
            EXPECT_NEAR(tracker.offset_hz(1.6) - tracker.offset_hz(1.5), 0.1 * delta_hz, 1e-4);
        } else {
            EXPECT_EQ(tracker.last_window().outcome, Outcome::Pending);
            EXPECT_EQ(tracker.offset_hz(1.5), 4.0);
        }
    }
}

TEST(Tracker, TheAcceptanceWindowIsCappedAtTheSearchRange) {
    // A clean steady carrier at 4 Hz acquired at a high signal-to-noise ratio: the track is confirmed, and its third
    // window's acceptance window is 3 sqrt(S), S the innovation's variance with the consecutive windows' correlation.
    // Then, with a gate of 0.01 dB re the mean noise power per bin and a hiss density that puts the carrier only 1.003
    // times the noise per bin (rho = 0.003, a variance of 94 Hz^2), 3 sqrt(S) is 29 Hz and the window is capped at the
    // search range's 15 Hz. (A measurement lies within 14.92 Hz of the oscillator, so the cap never changes what is
    // accepted.)
    const auto x = carrier(0.0, 3.0, 3.0, 4.0, 0.5);
    {
        // S, computed here: the restart at 1.0 s from windows 1 and 2 (variances r1, r2, covariance c12 = -0.35
        // sqrt(r1 r2)) leaves the offset's error equal to minus window 2's, s2 (theta a1 + a2), so a2, which window 3
        // shares, has the covariance -s2 with the offset and -s2 / 0.5 s with the drift. Predicted 0.5 s to window 3,
        // whose error is s3 (theta a2 + a3): S = P00 + 2 s3 theta P(f, a2) + s3^2 theta^2 + s3^2, s_k = sqrt(r_k /
        // (1 + theta^2)).
        CarrierTracker tracker(kRate, ViterbiConfig{}, 4.0);
        const double n0 = n0_for_line_snr(0.25, 1e4);
        push_range(tracker, x, 0, 1500, n0);
        const double r1 = tracker.last_window().variance_hz2;
        push_range(tracker, x, 1500, 2250, n0);
        ASSERT_EQ(tracker.last_window().outcome, Outcome::Restarted);
        const double r2 = tracker.last_window().variance_hz2;
        push_range(tracker, x, 2250, 3000, n0);
        const auto& last = tracker.last_window();
        ASSERT_TRUE(last.confirmed);
        EXPECT_EQ(last.outcome, Outcome::Updated);
        const double r3 = last.variance_hz2;
        const double t = theta();
        const double s2 = std::sqrt(r2 / (1.0 + t * t));
        const double s3 = std::sqrt(r3 / (1.0 + t * t));
        const double p00 = restart_variance_hz2(r1, r2, 0.5);
        const double p_fa = -s2 + 0.5 * (-s2 / 0.5);
        const double s = p00 + 2.0 * s3 * t * p_fa + s3 * s3 * t * t + s3 * s3;
        EXPECT_NEAR(last.frequency_sd_hz, std::sqrt(p00), 1e-12);
        EXPECT_NEAR(last.acceptance_hz, 3.0 * std::sqrt(s), 1e-9);
        EXPECT_GT(s, p00 + r3);  // the window's common samples widen the innovation (theta < 0, P(f, a2) < 0)
    }
    {
        ViterbiConfig config;
        config.tracker_gate_db = 0.01;
        CarrierTracker tracker(kRate, config, 4.0);
        push_range(tracker, x, 0, 2250, n0_for_line_snr(0.25, 1e4));
        ASSERT_EQ(tracker.last_window().outcome, Outcome::Restarted);
        push_range(tracker, x, 2250, 3000, n0_for_line_snr(0.25, 1.003));
        const auto& last = tracker.last_window();
        ASSERT_TRUE(last.confirmed);
        EXPECT_EQ(last.fill_factor, 1.0);  // no quarter above the noise the tracker is told: taken as filled
        EXPECT_GT(3.0 * std::sqrt(last.frequency_sd_hz * last.frequency_sd_hz + last.variance_hz2), 25.0);
        EXPECT_EQ(last.acceptance_hz, config.tracker_search_hz);
        EXPECT_EQ(last.outcome, Outcome::Updated);
    }
}

TEST(Tracker, TheToleranceIsFixedWhateverTheFadingSpread) {
    // The trust rule's tolerance is 0.26 cycles over the decoder's default block, 0.390 s: 0.667 Hz, whatever the
    // configured fading spread or the genie's (a stricter one, following a longer block, leaves weak stations
    // unconfirmed and makes their error larger, not smaller). The same windows confirm the track, or do not, and the
    // oscillator is the same, bit for bit, at every spread.
    const auto config_of = [](double spread_hz, std::optional<double> genie_hz) {
        ViterbiConfig config;
        config.fading_spread_hz = spread_hz;
        config.genie_fading_spread_hz = genie_hz;
        return config;
    };
    const ViterbiConfig configs[] = {config_of(0.0, std::nullopt), config_of(0.3, std::nullopt),
                                     config_of(5.0, std::nullopt), config_of(1.0, 0.3), config_of(1.0, 0.0)};
    for (const double snr : {50.0, 30.0}) {
        CarrierTracker reference(kRate, ViterbiConfig{}, 4.0);
        const TwoWindows expected = two_windows(reference, snr);
        push_range(reference, expected.x, 2250, 3000, expected.n0);
        for (const ViterbiConfig& config : configs) {
            SCOPED_TRACE("line at " + std::to_string(snr) + " times the noise per bin, spread " +
                         std::to_string(config.fading_spread_hz) + " Hz, genie's " +
                         std::to_string(config.genie_fading_spread_hz.value_or(-1.0)) + " Hz");
            CarrierTracker tracker(kRate, config, 4.0);
            const TwoWindows w = two_windows(tracker, snr);
            push_range(tracker, w.x, 2250, 3000, w.n0);
            EXPECT_EQ(tracker.last_window().confirmed, reference.last_window().confirmed);
            for (const double t : {1.0, 1.2, 1.5, 3.0, 30.0}) EXPECT_EQ(tracker.offset_hz(t), reference.offset_hz(t));
        }
    }
}

TEST(Tracker, TheCorrelationOfConsecutiveWindowsFollowsTheHop) {
    // At a hop longer than half the window, consecutive windows share fewer samples and their errors correlate less:
    // rho_c is the measured -0.35 at half the window times the noise-only correlation at the hop over that at half the
    // window, sum_i b_i b_(i + hop) / sum_i b_i^2 with b_i = w_i tau_i (computed here): -0.2134 at a hop of 0.6 s,
    // -0.0402 at 0.75 s and 0 at 1 s, for 1 s windows. A clean steady carrier at 300 times the noise per bin: the
    // second window's agreement limit is 3 sqrt(r1 + r2 - 2 c12 + (0.3 Hz/s x hop)^2), c12 = rho_c sqrt(r1 r2), and
    // the third, confirmed after the restart at the second, has S = P00 + 2 s3 theta P(f, a2) + s3^2 theta^2 + s3^2,
    // as at half the window but with this hop for the gap and this rho_c's theta.
    std::vector<double> b(1500);
    for (std::size_t i = 0; i < b.size(); ++i) {
        const double w = 0.5 - 0.5 * std::cos(2.0 * kPi * static_cast<double>(i) / 1500.0);
        b[i] = w * (static_cast<double>(i) - 750.0);
    }
    const auto noise_only = [&b](std::size_t hop) {
        double products = 0.0;
        double squares = 0.0;
        for (std::size_t i = 0; i < b.size(); ++i) {
            squares += b[i] * b[i];
            if (i + hop < b.size()) products += b[i] * b[i + hop];
        }
        return products / squares;
    };
    const auto x = carrier(0.0, 4.0, 4.0, 4.0, 0.5);
    const double n0 = n0_for_line_snr(0.25, 300.0);
    for (const auto& [hop_s, expected] : {std::pair{0.6, -0.2134}, std::pair{0.75, -0.0402}, std::pair{1.0, 0.0}}) {
        SCOPED_TRACE("hop " + std::to_string(hop_s) + " s");
        const auto hop = static_cast<std::size_t>(std::llround(hop_s * kRate));
        const double rho = kCorrelation * noise_only(hop) / noise_only(750);
        EXPECT_NEAR(rho, expected, 1e-4);
        const double t = rho == 0.0 ? 0.0 : (1.0 - std::sqrt(1.0 - 4.0 * rho * rho)) / (2.0 * rho);
        ViterbiConfig config;
        config.tracker_hop_s = hop_s;
        CarrierTracker tracker(kRate, config, 4.0);
        push_range(tracker, x, 0, 1500, n0);
        ASSERT_EQ(tracker.last_window().outcome, Outcome::Pending);
        const double r1 = tracker.last_window().variance_hz2;
        push_range(tracker, x, 1500, 1500 + hop, n0);
        ASSERT_EQ(tracker.last_window().outcome, Outcome::Restarted);
        const double r2 = tracker.last_window().variance_hz2;
        const double c12 = rho * std::sqrt(r1 * r2);
        const double drift_hz = kDriftSdHzPerS * hop_s;
        EXPECT_NEAR(tracker.last_window().agreement_hz, 3.0 * std::sqrt(r1 + r2 - 2.0 * c12 + drift_hz * drift_hz),
                    1e-12);
        push_range(tracker, x, 1500 + hop, 1500 + 2 * hop, n0);
        const auto& last = tracker.last_window();
        ASSERT_TRUE(last.confirmed);
        EXPECT_EQ(last.outcome, Outcome::Updated);
        // The two-point restart at the second window (P00 r2, P01 (r2 - c12) / g, P11 (r1 + r2 - 2 c12) / g^2, the
        // offset's covariance with a2 -s2 and the drift's -s2 / g), predicted one hop, g, to the third.
        const double g = hop_s;
        const double p00 = r2 + 2.0 * (r2 - c12) + (r1 + r2 - 2.0 * c12) + kDriftNoise * g * g * g / 3.0;
        const double s2 = std::sqrt(r2 / (1.0 + t * t));
        const double s3 = std::sqrt(last.variance_hz2 / (1.0 + t * t));
        const double p_fa = -s2 + g * (-s2 / g);
        const double s = p00 + 2.0 * s3 * t * p_fa + s3 * s3 * t * t + s3 * s3;
        EXPECT_NEAR(last.frequency_sd_hz, std::sqrt(p00), 1e-12);
        EXPECT_NEAR(last.acceptance_hz, 3.0 * std::sqrt(s), 1e-9);
    }
}

TEST(Tracker, ALineInsideTheAcceptanceWindowComesFirst) {
    // A steady carrier at 7.3 Hz, acquired, and from 3 s on another 8 Hz above it and twice as strong. The largest bin
    // of the search range is then the other one's, but a confirmed track looks first within the acceptance window of
    // a filled window's peak at the gate (+/-0.61 Hz here), where its own line passes the gate: the oscillator never
    // leaves 7.3 Hz. (Taking the largest bin, the track would keep the other carrier's measurement for the next
    // window, which would agree, and move there.)
    const auto x = sum_of(sum_of(carrier(0.0, 12.0, 12.0, 7.3), carrier(3.0, 12.0, 12.0, 15.3, 2.0)),
                          noise(12.0, sigma_for_s500(30.0), 1));
    CarrierTracker tracker(kRate, ViterbiConfig{}, 7.3);
    const auto trace = run(tracker, x, n0_for_s500(30.0));
    EXPECT_LE(worst_error_hz(trace, [](double) { return 7.3; }, 0.0), 0.05);
    EXPECT_EQ(tracker.counts().restarted, 1u);  // the acquisition
    EXPECT_EQ(tracker.counts().pending, 1u);    // its first window
}

// The tests below pin the mechanics the design keeps from the first tracker, each on a signal where the right answer
// is known without a simulation.

TEST(Tracker, ThePairOfWindowsThatAcquiresNeedsAllItsSamples) {
    // A clean carrier at 8 Hz, the tracker's start 5 Hz: the first window (complete at the 1 500th sample, 1 s) is
    // measured and kept, and nothing moves until the second window agrees with it, at the 2 250th sample (1.5 s). The
    // filter then restarts there, at the second window's center, 1.0 s. The two windows share half their samples, so
    // the agreement test allows for their errors' covariance c12 = -0.35 sqrt(r1 r2): the limit is 3 sqrt(r1 + r2 -
    // 2 c12 + (0.3 Hz/s x 0.5 s)^2).
    const auto x = carrier(0.0, 3.0, 3.0, 8.0);
    const double n0 = 1e-10;
    CarrierTracker tracker(kRate, ViterbiConfig{}, 5.0);
    push_range(tracker, x, 0, 1500, n0);
    EXPECT_EQ(tracker.last_window().outcome, Outcome::Pending);
    const double r1 = tracker.last_window().variance_hz2;
    push_range(tracker, x, 1500, 2249, n0);
    EXPECT_EQ(tracker.offset_hz(1.0), 5.0);
    EXPECT_EQ(tracker.offset_hz(100.0), 5.0);
    push_range(tracker, x, 2249, 2250, n0);
    EXPECT_EQ(tracker.last_window().outcome, Outcome::Restarted);
    EXPECT_NEAR(tracker.offset_hz(1.0), 8.0, 0.001);
    const double r2 = tracker.last_window().variance_hz2;
    const double c12 = kCorrelation * std::sqrt(r1 * r2);
    EXPECT_NEAR(tracker.last_window().agreement_hz,
                3.0 * std::sqrt(r1 + r2 - 2.0 * c12 + (kDriftSdHzPerS * 0.5) * (kDriftSdHzPerS * 0.5)), 1e-12);
}

TEST(Tracker, ACleanToneIsMeasuredToUnderAMillihertz) {
    // The parabola through the peak's three bins (their powers in dB re any one reference), on the Hann-windowed
    // zero-padded transform, places a clean carrier to under 1 mHz wherever it falls between the 0.18 Hz bins
    // (0.08 mHz at most by a calculation of the same estimator). The restart puts the state on the second window's
    // measurement, read at its center, 1.0 s.
    for (const double residual_hz : {0.0, 0.03, 0.06, 0.09, 0.12, 0.15, 0.18, 0.21, -0.07, 3.0, -11.0}) {
        SCOPED_TRACE("carrier " + std::to_string(residual_hz) + " Hz from the start");
        const auto x = carrier(0.0, 1.7, 1.7, 5.0 + residual_hz);
        CarrierTracker tracker(kRate, ViterbiConfig{}, 5.0);
        push_range(tracker, x, 0, 2250, 1e-10);
        ASSERT_EQ(tracker.last_window().outcome, Outcome::Restarted);
        EXPECT_NEAR(tracker.offset_hz(1.0), 5.0 + residual_hz, 0.001);
    }
}

TEST(Tracker, AMeasurementBelongsToTheWindowsCenter) {
    // A clean carrier sweeping 0.3 Hz/s, f(t) = 4 + 0.3 t Hz, and an unconfirmed tracker holding 4 Hz. Each window
    // measures its average frequency, the carrier's at the window's center: 4.15 Hz for [0 s, 1 s] and 4.3 Hz for
    // [0.5 s, 1.5 s], not the 4.15 Hz or 4.45 Hz of the second window's ends. They agree, and the filter restarts at
    // 4.3 Hz at 1.0 s with the drift (4.3 - 4.15) / 0.5 s = 0.3 Hz/s.
    const auto x = carrier(0.0, 1.7, 1.7, 4.0, 1.0, 0.3);
    CarrierTracker tracker(kRate, ViterbiConfig{}, 4.0);
    push_range(tracker, x, 0, 1500, 1e-10);
    EXPECT_NEAR(tracker.last_window().measured_hz, 4.15, 0.001);
    push_range(tracker, x, 1500, 2250, 1e-10);
    ASSERT_EQ(tracker.last_window().outcome, Outcome::Restarted);
    EXPECT_NEAR(tracker.offset_hz(1.0), 4.3, 0.001);
    EXPECT_NEAR(tracker.offset_hz(1.5) - tracker.offset_hz(1.0), 0.15, 0.001);
}

TEST(Tracker, ALoneOtherCarrierIsHeldNotFollowed) {
    // A steady carrier at 7.3 Hz and, for 100 ms at the center of the window [5 s, 6 s], another at 17.3 Hz eight times
    // as strong, which outshines the steady one in that window alone (the two windows beside it barely see it, at the
    // Hann window's ends). The confirmed track takes its own line, inside its acceptance window, first, so the burst's
    // peak never becomes a measurement; but the burst's skirt pulls the steady line's measurement in that window to
    // about 7.43 Hz, 0.13 Hz off, which at 55 dB re the mean noise power per bin lies far outside the acceptance
    // window. That measurement is kept pending, and the next window, on the steady carrier again and inside, drops it.
    // The oscillator never leaves the steady carrier.
    const auto x = sum_of(sum_of(carrier(0.0, 12.0, 12.0, 7.3), carrier(5.45, 5.55, 12.0, 17.3, 8.0)),
                          noise(12.0, sigma_for_s500(30.0), 1));
    CarrierTracker tracker(kRate, ViterbiConfig{}, 7.3);
    const auto trace = run(tracker, x, n0_for_s500(30.0));
    EXPECT_LE(worst_error_hz(trace, [](double) { return 7.3; }, 0.0), 0.05);
    EXPECT_EQ(tracker.counts().restarted, 1u);  // the acquisition
}

TEST(Tracker, TwoOffTrackMeasurementsAreANewCarrier) {
    // The carrier at 7.3 Hz is replaced at 10 s by another at 17.3 Hz. The first window that sees mostly the new one is
    // outside the old one's acceptance window and is kept; the next agrees with it, so the filter restarts there with
    // the drift of the two, about 0, and the oscillator is on the new carrier before 13 s.
    const auto x = sum_of(sum_of(carrier(0.0, 10.0, 20.0, 7.3), carrier(10.0, 20.0, 20.0, 17.3)),
                          noise(20.0, sigma_for_s500(30.0), 1));
    CarrierTracker tracker(kRate, ViterbiConfig{}, 7.3);
    const auto trace = run(tracker, x, n0_for_s500(30.0));
    EXPECT_LE(worst_error_hz(trace, [](double) { return 7.3; }, 0.0, 9.0), 0.05);
    EXPECT_LE(worst_error_hz(trace, [](double) { return 17.3; }, 13.0), 0.05);
    EXPECT_NEAR(tracker.offset_hz(20.0) - tracker.offset_hz(19.0), 0.0, 0.05);
}

TEST(Tracker, WithoutANoiseEstimateNothingIsMeasured) {
    // N0 = 0 is the hiss estimator's "no estimate yet": a strong carrier 3 Hz from the start moves nothing, though the
    // clock runs. Once N0 is known, the next two windows acquire it. Exact zeros are never measured either.
    const auto x = carrier(0.0, 6.0, 6.0, 8.0);
    CarrierTracker tracker(kRate, ViterbiConfig{}, 5.0);
    push_range(tracker, x, 0, 4500, 0.0);
    EXPECT_EQ(tracker.counts().windows, 5u);
    EXPECT_EQ(tracker.counts().not_measured, 5u);
    EXPECT_EQ(tracker.offset_hz(3.0), 5.0);
    push_range(tracker, x, 4500, 9000, 1e-10);
    EXPECT_EQ(tracker.counts().restarted, 1u);
    EXPECT_NEAR(tracker.offset_hz(6.0), 8.0, 0.001);

    const std::vector<Sample> zeros(4500, Sample(0.0f, 0.0f));
    CarrierTracker quiet(kRate, ViterbiConfig{}, 5.0);
    quiet.push(zeros, 0.0, 1e-10);
    EXPECT_EQ(quiet.counts().not_measured, quiet.counts().windows);
    EXPECT_EQ(quiet.offset_hz(3.0), 5.0);
}

TEST(Tracker, SplittingTheInputChangesNothing) {
    // Whatever the blocks, the same samples give the same oscillator, bit for bit: the windows are cut from the stream,
    // not from the blocks, and the clock counts samples. The reference is the whole signal (18 000 samples) in one
    // push; blocks of 7 and 4 501 samples straddle the hops, and blocks of 1 complete a window with a single sample.
    const std::string text = text_for("PARIS", 25.0, 11.0);
    const auto x = signals::keyed_signal(text, 25.0, kRate, 12.0, 7.3, 1.0, sigma_for_s500(5.0), 7);
    const double n0 = n0_for_s500(5.0);
    CarrierTracker reference(kRate, ViterbiConfig{}, 5.0);
    reference.push(x, 0.0, n0);
    ASSERT_GT(reference.counts().updated, 0u);
    for (const std::size_t block : {1u, 7u, 750u, 1500u, 4501u}) {
        SCOPED_TRACE("blocks of " + std::to_string(block));
        CarrierTracker split(kRate, ViterbiConfig{}, 5.0);
        for (std::size_t first = 0; first < x.size(); first += block)
            split.push(std::span<const Sample>(x).subspan(first, std::min(block, x.size() - first)),
                       static_cast<double>(first) / kRate, n0);
        for (const double t_s : {0.0, 6.0, 12.0, 20.0}) EXPECT_EQ(split.offset_hz(t_s), reference.offset_hz(t_s));
        EXPECT_EQ(split.counts().updated, reference.counts().updated);
        EXPECT_EQ(split.counts().restarted, reference.counts().restarted);
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
    refused([](ViterbiConfig& c) { c.tracker_search_hz = 0.1; });    // under one bin of 0.18 Hz
    refused([](ViterbiConfig& c) { c.tracker_search_hz = 750.0; });  // half the sample rate: no neighbors left
    refused([](ViterbiConfig& c) { c.tracker_gate_db = kNaN; });
    refused([](ViterbiConfig& c) { c.tracker_gate_db = 0.0; });  // a peak at the noise's own level has no SNR
    refused([](ViterbiConfig& c) { c.tracker_gate_db = -3.0; });
    refused([](ViterbiConfig& c) { c.tracker_drift_sd_hz_per_s = -0.1; });
    refused([](ViterbiConfig& c) { c.tracker_drift_sd_hz_per_s = kInf; });
    refused([](ViterbiConfig& c) { c.tracker_drift_noise = -1e-4; });
    refused([](ViterbiConfig& c) { c.tracker_drift_noise = kNaN; });
    refused([](ViterbiConfig& c) {  // 6 samples: the profile's first block would hold only the zero-weight sample
        c.tracker_window_s = 0.004;
        c.tracker_hop_s = 0.002;
    });
    refused([](ViterbiConfig& c) { c.tracker_hop_s = 0.4; });  // under half the window: two hops apart would overlap
    ViterbiConfig smallest;  // 8 samples, every block with weight
    smallest.tracker_window_s = 8.0 / kRate;
    smallest.tracker_hop_s = 4.0 / kRate;
    EXPECT_NO_THROW(CarrierTracker(kRate, smallest, 0.0));
    ViterbiConfig odd_spreads;  // the fading spread is not the tracker's business: no refusal follows from it
    odd_spreads.fading_spread_hz = 1000.0;
    odd_spreads.genie_fading_spread_hz = kNaN;
    EXPECT_NO_THROW(CarrierTracker(kRate, odd_spreads, 0.0));
    ViterbiConfig no_drift;
    no_drift.tracker_drift_sd_hz_per_s = 0.0;  // a drift known to be zero, and never changing, is a model, not an error
    no_drift.tracker_drift_noise = 0.0;
    EXPECT_NO_THROW(CarrierTracker(kRate, no_drift, 0.0));

    // A block must continue the previous one (to within half a sample), and the first block's time is the clock. The
    // hiss density must be finite and not negative.
    const std::vector<Sample> block(100, Sample(0.0f, 0.0f));
    CarrierTracker tracker(kRate, config, 0.0);
    EXPECT_NO_THROW(tracker.push({}, 123.0, 1e-6));  // an empty block is nothing, whatever its time
    EXPECT_THROW(tracker.push(block, kNaN, 1e-6), std::invalid_argument);
    EXPECT_THROW(tracker.push(block, 10.0, -1e-6), std::invalid_argument);
    EXPECT_THROW(tracker.push(block, 10.0, kNaN), std::invalid_argument);
    EXPECT_THROW(tracker.push(block, 10.0, kInf), std::invalid_argument);
    EXPECT_NO_THROW(tracker.push(block, 10.0, 1e-6));
    EXPECT_THROW(tracker.push(block, 10.0, 1e-6), std::invalid_argument);  // 100 samples later is 10.0667 s, not 10 s
    EXPECT_THROW(tracker.push(block, 10.0 + 100.0 / kRate + 0.6 / kRate, 1e-6), std::invalid_argument);
    EXPECT_NO_THROW(tracker.push(block, 10.0 + 100.0 / kRate + 0.4 / kRate, 1e-6));  // within half a sample
    EXPECT_NO_THROW(tracker.push(block, 10.0 + 200.0 / kRate, 0.0));
}
