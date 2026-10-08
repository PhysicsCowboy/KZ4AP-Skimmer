#include "kz4ap/matched_front_end.hpp"

#include "test_signals.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <complex>
#include <limits>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

using namespace kz4ap;
using kz4ap::test::duration_for;
using kz4ap::test::keyed_signal;
using kz4ap::test::keying;

namespace {

constexpr double kRate = 1500.0;

std::vector<FrontEndSample> run(MatchedFrontEnd& fe, const std::vector<Sample>& x) {
    std::vector<FrontEndSample> out;
    out.reserve(x.size());
    for (const auto& s : x) out.push_back(fe.step(s));
    return out;
}

std::vector<Sample> white_noise(std::size_t n, double sigma, unsigned seed) {
    std::mt19937 rng(seed);
    std::normal_distribution<double> gauss(0.0, sigma / std::sqrt(2.0));
    std::vector<Sample> x(n);
    for (auto& s : x) s = Sample(static_cast<float>(gauss(rng)), static_cast<float>(gauss(rng)));
    return x;
}

// Expected noise RMS per real component after a K-sample boxcar, for white input
// noise of total power sigma_in^2 per sample.
double boxcar_sigma(double sigma_in, int k) { return sigma_in / std::sqrt(2.0) / std::sqrt(static_cast<double>(k)); }

}  // namespace

TEST(MatchedFrontEnd, LogBesselI0MatchesReferenceValues) {
    EXPECT_EQ(log_bessel_i0(0.0), 0.0);
    EXPECT_NEAR(log_bessel_i0(1.0), std::log(1.2660658777520084), 1e-6);
    EXPECT_NEAR(log_bessel_i0(5.0), std::log(27.239871823604446), 1e-6);
    EXPECT_NEAR(log_bessel_i0(10.0), std::log(2815.7166284662544), 1e-6);
    EXPECT_NEAR(log_bessel_i0(100.0), 96.7797326899426, 1e-5);  // exact value from the power series
    EXPECT_TRUE(std::isfinite(log_bessel_i0(1e8)));
}

TEST(MatchedFrontEnd, EnvelopeLlrIsZeroWithoutSignal) {
    EXPECT_EQ(envelope_llr(2.0, 0.0), 0.0);
}

TEST(MatchedFrontEnd, EnvelopeLlrCrossesZeroAtTheOptimumThreshold) {
    // a = sqrt(2 E/N0) with E/N0 = 10 (10 dB re 1): optimum threshold b/a = 0.61
    // (docs/research/proakis-ook-notes.md, section 2.3).
    const double a = std::sqrt(20.0);
    EXPECT_LT(envelope_llr(0.59 * a, a), 0.0);
    EXPECT_GT(envelope_llr(0.63 * a, a), 0.0);
}

TEST(MatchedFrontEnd, FilterLengthFollowsTheDit) {
    MatchedFrontEnd fe(kRate);
    EXPECT_EQ(fe.length(), 24);  // 0.8 x 20 ms (60 WPM) x 1500 samples/s
    fe.set_dit(0.048);
    EXPECT_EQ(fe.length(), 24);  // the warm-up (0.32 s = 480 samples) runs at the acquisition width...
    run(fe, white_noise(480, 1.0, 1));
    EXPECT_EQ(fe.length(), 58);  // ...and then takes the dit: 0.8 x 48 ms x 1500 samples/s = 57.6
    fe.set_dit(10.0);
    EXPECT_EQ(fe.length(), 288);  // clamped at 5 WPM
}

TEST(MatchedFrontEnd, IgnoresInvalidDitsAndClampsToTheAcquisitionWidth) {
    MatchedFrontEnd fe(kRate);
    fe.set_dit(std::nan(""));  // ignored during the warm-up too: 0.048 s must survive
    fe.set_dit(0.048);
    fe.set_dit(-1.0);
    run(fe, white_noise(480, 1.0, 13));
    EXPECT_EQ(fe.length(), 58);
    for (const double bad : {0.0, -0.048, std::nan(""), std::numeric_limits<double>::infinity(),
                             -std::numeric_limits<double>::infinity()}) {
        fe.set_dit(bad);
        EXPECT_EQ(fe.length(), 58) << bad;
    }
    fe.set_dit(0.001);  // faster than 60 WPM: clamped to the acquisition width (16 ms)
    EXPECT_EQ(fe.length(), 24);
    fe.set_dit(1e300);  // clamped to the 5 WPM width (192 ms)
    EXPECT_EQ(fe.length(), 288);
}

TEST(MatchedFrontEnd, SquelchScalesWithTheFilterLength) {
    // a_min = 3 (K/24)^(1/4): the same chance that noise alone lifts a-hat past it at every K.
    MatchedFrontEnd fe(kRate);
    run(fe, white_noise(480, 1.0, 2));
    EXPECT_DOUBLE_EQ(fe.squelch(), 3.0);
    fe.set_dit(0.048);
    EXPECT_NEAR(fe.squelch(), 3.0 * std::pow(58.0 / 24.0, 0.25), 1e-12);  // 3.74
    fe.set_dit(10.0);
    EXPECT_NEAR(fe.squelch(), 3.0 * std::pow(12.0, 0.25), 1e-12);  // 5.58 at K = 288
}

TEST(MatchedFrontEnd, SteadyToneComesOutAtItsAmplitude) {
    MatchedFrontEnd fe(kRate);
    FrontEndSample out;
    for (int i = 0; i < 100; ++i) out = fe.step(Sample(0.3f, 0.4f));
    EXPECT_NEAR(std::abs(out.filtered), 0.5, 1e-6);
    EXPECT_FLOAT_EQ(out.weight, 1.0f / 24.0f);
    EXPECT_FALSE(out.ready);  // still warming up (0.32 s = 480 samples)
}

TEST(MatchedFrontEnd, CorrelationOfFilteredNoiseSumsToTheFilterLength) {
    MatchedFrontEnd fe(kRate);
    run(fe, white_noise(480, 1.0, 4));  // the warm-up
    fe.set_dit(0.048);
    const int k = fe.length();
    std::vector<std::complex<double>> v;
    for (const auto& s : white_noise(300000, 1.0, 3)) v.emplace_back(fe.step(s).filtered);
    const std::size_t start = 1000;
    const auto corr = [&](int lag) {
        std::complex<double> sum = 0;
        std::size_t count = 0;
        for (std::size_t i = start + static_cast<std::size_t>(lag); i < v.size(); ++i, ++count)
            sum += v[i] * std::conj(v[i - static_cast<std::size_t>(lag)]);
        return (sum / static_cast<double>(count)).real();
    };
    const double r0 = corr(0);
    EXPECT_NEAR(r0, 1.0 / k, 0.05 / k);  // the boxcar passes 1/K of white noise's power
    double total = r0;
    for (int lag = 1; lag < k; ++lag) total += 2 * corr(lag);
    EXPECT_NEAR(total / r0, k, 0.1 * k);  // so the per-sample weight is 1/K
}

TEST(MatchedFrontEnd, EstimatesNoiseAndAmplitude) {
    // S500 = 4.8 dB, 12 s of PARIS, read at the last mark's end. Over 400 simulated seeds sigma-hat
    // was 0.72-1.23 of sigma (mean 1.005, standard deviation 0.095; only word spaces update it here),
    // the mean of 20 seeds 0.95-1.05, and s-hat 0.80-0.90. The floor never lifted (final check F-3:
    // with c = 0.4 it lifted in 61 of 400 seeds and failed 3 of them).
    const std::string msg = "PARIS PARIS PARIS PARIS PARIS";
    double sum = 0;
    for (unsigned seed = 5; seed < 25; ++seed) {
        const auto x = keyed_signal(msg, 25, kRate, duration_for(msg, 25, 0.0), 0, 1.0, 1.0, seed);
        MatchedFrontEnd fe(kRate);
        fe.set_dit(0.048);
        run(fe, x);
        const double ratio = fe.noise_sigma() / boxcar_sigma(1.0, fe.length());
        sum += ratio;
        EXPECT_NEAR(ratio, 1.0, 0.35) << "seed " << seed;
        EXPECT_GT(fe.amplitude(), 0.75) << "seed " << seed;
        EXPECT_LT(fe.amplitude(), 1.1) << "seed " << seed;
    }
    EXPECT_NEAR(sum / 20.0, 1.00, 0.07);
}

TEST(MatchedFrontEnd, LlrSeparatesKeyDownFromKeyUp) {
    const std::string msg = "PARIS PARIS PARIS PARIS PARIS";
    const auto x = keyed_signal(msg, 25, kRate, duration_for(msg, 25, 0.0), 0, 1.0, 1.0, 6);
    MatchedFrontEnd fe(kRate);
    fe.set_dit(0.048);
    const auto out = run(fe, x);
    const double delay_s = (fe.length() - 1) / 2.0 / kRate;  // the boxcar's group delay
    const auto marks = keying(msg, 25, 0.5);
    for (std::size_t i = 14; i + 1 < marks.size(); ++i) {  // skip the first word: the estimates are still settling
        const auto [on, off] = marks[i];
        if (off - on > 0.1) {  // a dah: its middle is key-down
            EXPECT_GT(out[static_cast<std::size_t>(((on + off) / 2 + delay_s) * kRate)].llr, 5.0f) << on;
        }
        const double gap = marks[i + 1].first - off;
        if (gap > 0.1) {  // a character or word space: its middle is key-up
            EXPECT_LT(out[static_cast<std::size_t>((off + gap / 2 + delay_s) * kRate)].llr, -5.0f) << off;
        }
    }
}

TEST(MatchedFrontEnd, ReacquireWidensTheFilterAndForgetsTheAmplitude) {
    const std::string msg = "PARIS PARIS PARIS";
    MatchedFrontEnd fe(kRate);
    fe.set_dit(0.048);
    run(fe, keyed_signal(msg, 25, kRate, duration_for(msg, 25, 0.0), 0, 1.0, 1.0, 12));
    const double sigma = fe.noise_sigma();
    ASSERT_GT(fe.amplitude(), 0.5);
    fe.reacquire();
    EXPECT_EQ(fe.length(), 24);
    EXPECT_EQ(fe.amplitude(), 0.0);
    EXPECT_NEAR(fe.noise_sigma(), sigma * std::sqrt(58.0 / 24.0), 1e-9);  // rescaled, not forgotten
}

TEST(MatchedFrontEnd, NoiseAloneKeepsTheNoiseEstimateAndNeverKeys) {
    // Review finding C1: with the old posterior guard, most seeds settled at sigma-hat = 0.55 sigma
    // and keyed noise. 10 seeds x 120 s, warm-up at the acquisition width as the decoder does, then
    // K = 24 or K = 58. Simulated (40 seeds): sigma-hat/sigma mean 1.00, standard deviation 0.020
    // (K = 24) and 0.032 (K = 58); no signal flag and no floor lift at all.
    for (const double dit : {0.0, 0.048}) {
        for (unsigned seed = 100; seed < 110; ++seed) {
            MatchedFrontEnd fe(kRate);
            if (dit > 0) fe.set_dit(dit);
            std::size_t signal = 0, ready = 0, run_length = 0, longest = 0;
            for (const auto& o : run(fe, white_noise(static_cast<std::size_t>(120 * kRate), 1.0, seed))) {
                if (!o.ready) continue;
                ++ready;
                if (o.signal) ++signal;
                if (!o.signal) EXPECT_EQ(o.p_key_down, 0.0f);
                run_length = (o.signal && o.log_odds > 1.0f) ? run_length + 1 : 0;
                longest = std::max(longest, run_length);
            }
            const double expected = boxcar_sigma(1.0, fe.length());
            EXPECT_NEAR(fe.noise_sigma(), expected, (dit > 0 ? 0.15 : 0.10) * expected) << "seed " << seed;
            EXPECT_LT(signal, ready / 1000) << "seed " << seed;
            // A key-down needs log-odds above +1 nat; the decoder drops marks under 0.3 dit (22 samples at 25 WPM).
            EXPECT_LT(longest, 22u) << "seed " << seed;
        }
    }
}

TEST(MatchedFrontEnd, NoiseEstimateRecoversFromANoiseStep) {
    // Re-review finding I-1: the noise rises 6 dB at 30 s. The guard alone climbs back in about
    // 43 s (derived from its dynamics; the floor does not act after a 6 dB rise). Simulated over
    // 40 seeds: back within 0.9 of the new sigma 24-41 s after the step, the last signal flag at
    // most 16 s after it, sigma-hat ended within 0.95-1.03. Allowed here: 60 s, then within 15%
    // and no key-down.
    for (unsigned seed = 200; seed < 210; ++seed) {
        auto x = white_noise(static_cast<std::size_t>(120 * kRate), 1.0, seed);
        for (std::size_t i = static_cast<std::size_t>(30 * kRate); i < x.size(); ++i) x[i] *= 2.0f;
        MatchedFrontEnd fe(kRate);
        std::size_t run_length = 0, longest_late = 0;
        const auto out = run(fe, x);
        for (std::size_t i = 0; i < out.size(); ++i) {
            run_length = (out[i].signal && out[i].log_odds > 1.0f) ? run_length + 1 : 0;
            if (static_cast<double>(i) > 90 * kRate) longest_late = std::max(longest_late, run_length);
        }
        const double expected = boxcar_sigma(2.0, fe.length());
        EXPECT_NEAR(fe.noise_sigma(), expected, 0.15 * expected) << "seed " << seed;
        EXPECT_LT(longest_late, 22u) << "seed " << seed;
    }
}

TEST(MatchedFrontEnd, NoiseEstimateAtMinusFiveDbS500HasItsKnownLeak) {
    // Continuous PARIS at 25 WPM, S500 = -5 dB (a = 3.5): some weak marks pass the guard and lift
    // sigma-hat. Simulated over 40 seeds (mean of the last 30 s of 60 s): 1.05-1.26 of sigma,
    // mean 1.17. The test pins that bias, so a change in it is noticed.
    const double sigma_in = std::sqrt(3.0 / std::pow(10.0, -0.5));
    std::string msg;
    for (int i = 0; i < 25; ++i) msg += "PARIS ";
    double sum = 0;
    for (unsigned seed = 11; seed < 31; ++seed) {
        const auto x = keyed_signal(msg, 25, kRate, duration_for(msg, 25, 0.0), 0, 1.0, sigma_in, seed);
        MatchedFrontEnd fe(kRate);
        fe.set_dit(0.048);
        double acc = 0;
        std::size_t count = 0;
        for (std::size_t i = 0; i < x.size(); ++i) {
            fe.step(x[i]);
            if (static_cast<double>(i) > static_cast<double>(x.size()) - 30 * kRate && i % 150 == 0) {
                acc += fe.noise_sigma();
                ++count;
            }
        }
        const double ratio = acc / static_cast<double>(count) / boxcar_sigma(sigma_in, fe.length());
        sum += ratio;
        EXPECT_GT(ratio, 0.95) << "seed " << seed;
        EXPECT_LT(ratio, 1.40) << "seed " << seed;
    }
    EXPECT_NEAR(sum / 20.0, 1.17, 0.08);
}

TEST(MatchedFrontEnd, StrongSignalKeepsNoiseEstimate) {
    // S500 = 60 dB: key-down power over the noise in 500 Hz of a white 1500 samples/s stream.
    // Simulated over 400 seeds: sigma-hat 0.75-1.31 of sigma, s-hat 0.83 (the ramp bias). The floor
    // lifted in 23 of them without restarting s-hat (final check F-3: with c = 0.4 and every lift
    // restarting s-hat, 6 of 400 seeds failed, s-hat down to 0.67).
    const double sigma_in = std::sqrt(3.0 * 1e-6);
    const std::string msg = "PARIS PARIS PARIS PARIS PARIS";
    for (unsigned seed = 8; seed < 18; ++seed) {
        const auto x = keyed_signal(msg, 25, kRate, duration_for(msg, 25, 0.0), 0, 1.0, sigma_in, seed);
        MatchedFrontEnd fe(kRate);
        fe.set_dit(0.048);
        run(fe, x);
        const double expected = boxcar_sigma(sigma_in, fe.length());
        EXPECT_GT(fe.noise_sigma(), expected / 1.6) << "seed " << seed;
        EXPECT_LT(fe.noise_sigma(), expected * 1.6) << "seed " << seed;
        EXPECT_GT(fe.amplitude(), 0.70) << "seed " << seed;
        EXPECT_LT(fe.amplitude(), 1.1) << "seed " << seed;
    }
}

TEST(MatchedFrontEnd, NoiseEstimateScalesWhenTheFilterChanges) {
    // Simulated over 40 seeds: 0.88-1.10 of sigma, mean 1.00.
    double sum = 0;
    for (unsigned seed = 9; seed < 29; ++seed) {
        MatchedFrontEnd fe(kRate);  // K = 24
        run(fe, white_noise(static_cast<std::size_t>(5 * kRate), 1.0, seed));
        fe.set_dit(0.048);  // K = 58
        const double ratio = fe.noise_sigma() / boxcar_sigma(1.0, 58);
        sum += ratio;
        EXPECT_NEAR(ratio, 1.0, 0.20) << "seed " << seed;
    }
    EXPECT_NEAR(sum / 20.0, 1.0, 0.05);
}

TEST(MatchedFrontEnd, ResetStartsOver) {
    MatchedFrontEnd fe(kRate);
    fe.set_dit(0.048);
    run(fe, white_noise(1000, 1.0, 10));
    fe.reset();
    EXPECT_EQ(fe.length(), 24);
    EXPECT_FALSE(fe.step(Sample(0.1f, 0.0f)).ready);
}

TEST(MatchedFrontEnd, RejectsInvalidConfig) {
    EXPECT_THROW(MatchedFrontEnd(0.0), std::invalid_argument);
    MatchedFrontEndConfig c;
    c.length_dits = 0;
    EXPECT_THROW(MatchedFrontEnd(kRate, c), std::invalid_argument);
    c = {};
    c.prior_key_down = 1.0;
    EXPECT_THROW(MatchedFrontEnd(kRate, c), std::invalid_argument);
    c = {};
    c.initial_wpm = 4.0;  // below min_wpm
    EXPECT_THROW(MatchedFrontEnd(kRate, c), std::invalid_argument);
    c = {};
    c.noise_guard = 0.0;
    EXPECT_THROW(MatchedFrontEnd(kRate, c), std::invalid_argument);
    c = {};
    c.neighbor_guard = 1.0;  // below noise_guard
    EXPECT_THROW(MatchedFrontEnd(kRate, c), std::invalid_argument);
    c = {};
    c.floor_quantile = 0.5;  // not below floor_min_clean
    EXPECT_THROW(MatchedFrontEnd(kRate, c), std::invalid_argument);
    c = {};
    c.floor_samples = 1;
    EXPECT_THROW(MatchedFrontEnd(kRate, c), std::invalid_argument);
    c = {};
    c.floor_restart_ratio = 0.5;  // must be at least 1: a lift already means the floor exceeds sigma^2
    EXPECT_THROW(MatchedFrontEnd(kRate, c), std::invalid_argument);
}
