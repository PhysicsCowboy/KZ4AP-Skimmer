// The new decoder's carrier-frequency tracker (design notes, topic 4, item 8; the first prototype's plan, 2.9;
// redesigned by the owner's decision B of 2026-10-20, Task 6b). One tracker per channel follows the offset f^ of the
// station's carrier from the channel's center, Hz, and its drift rate d, Hz/s, so that every hypothesis of the decoder
// reads the same re-centered samples. It uses none of the decoder's decisions: on/off keying leaves a steady line at
// the carrier frequency whose share of the power is the square of the key-down fraction, 0.44^2 = 0.19 of the key-down
// power for PARIS text (derived: of the 50 units of P, A, R, I, S and a word gap, 22 are key-down). The design is the
// standard one: the maximum-likelihood estimate of a tone's frequency at the peak of its spectrum (Rife and Boorstyn
// 1974), tracked with its drift rate by a Kalman filter (Bar-Shalom, Li and Kirubarajan 2001), behind a gate against
// the noise floor (a constant false-alarm rate detector), with a new track confirmed by two agreeing measurements.
//
// Use. Before each block the engine's hiss estimator gives N0, the noise density around the channel, FS^2/Hz
// (hiss.hpp). The decoder pushes the block with it, then re-centers the block's samples with offset_hz(t), a
// numerically controlled oscillator at -f^(t) whose phase the decoder integrates. The tracker is causal: a window is
// measured when its last sample arrives, and its measurement belongs to its center, half a window earlier.
//
// Resolution, spacing and accuracy.
//   - The sample rate, r = 1 500 complex samples/s, sets the range: +/-750 Hz.
//   - The 1 s window sets the resolution: about 1 Hz (1 / T). With the Hann taper the main lobe is +/-2 Hz wide and the
//     noise bandwidth 1.5 Hz. Two carriers within about 2 Hz look like one.
//   - The zero padding to 8 192 points only samples the same continuous spectrum every r / 8 192 = 0.183 Hz. It is
//     interpolation, not resolution.
//   - A single tone's frequency is located far more finely than the resolution. The peak of the continuous spectrum is
//     the maximum-likelihood estimate (Rife and Boorstyn 1974), and its accuracy is set by the signal-to-noise ratio
//     and the window's length: at 0 dB S500 the first tracker measured a scatter of 0.064 Hz (PARIS at 25 WPM, its
//     windows above its gate) and this one 0.098 Hz rms (all its windows; Measured), against the Cramer-Rao bound's
//     0.040 Hz for the carrier line alone.
//   - The fine spacing and the parabola only find that peak.
//
// Stage by stage (r = rate_hz; the values are ViterbiConfig's tracker_* fields unless a constant of tracker.cpp is
// named; t_s is the time of the state, f^ and d, and of its covariance P):
//
//  0. The clock. The first push's t0_s is the time of sample 0, and sample j is at t0_s + j / r. Blocks must be
//     contiguous (to within half a sample), and the tracker counts samples, so the way the input is split into blocks
//     changes nothing, to the last bit.
//  1. The start. f^ = start_offset_hz (the engine's frequency anchor), d = 0, P = diag((5 Hz)^2, (0.3 Hz/s)^2)
//     (kInitialOffsetSdHz, tracker_drift_sd_hz_per_s), t_s the first sample's time. 3 sigma_f is then 15 Hz: the track
//     starts unconfirmed, and the oscillator holds the anchor until two windows agree (2h).
//  2. Every tracker_hop_s = 0.5 s (750 samples), once tracker_window_s = 1.0 s (N = 1 500 samples) have arrived, a
//     window is measured; t_c, its center, is N / (2 r) after its first sample.
//     a. Prediction and the trust rule. With dt = t_c - t_s,
//            f_p = f^ + d dt,   P_p = F P F' + Q,   F = [[1, dt], [0, 1]],
//            Q = q [[dt^3 / 3, dt^2 / 2], [dt^2 / 2, dt]],
//        q = tracker_drift_noise = 1.5e-4 Hz^2/s^3: the continuous white-noise acceleration model, the drift rate a
//        random walk of spectral density q (Bar-Shalom, Li and Kirubarajan 2001). sigma_f = sqrt(P_p[0][0]). The track
//        is confirmed while 3 sigma_f <= 0.67 Hz (kToleranceHz, the decoder's tolerance), and unconfirmed otherwise.
//     b. The shift. Sample i, tau_i = (i - N / 2) / r from the center, is multiplied by exp(-j 2 pi phi(tau_i)), phi
//        the oscillator's phase about the center in cycles, the integral of offset_hz (stage 3) from t_c to t_c + tau:
//            phi(tau) = f_o tau + d J(tau),   J(tau) = integral_0^tau (min(u, h) - min(0, h)) du,
//        f_o = offset_hz(t_c), h = t_hold - t_c. On a confirmed track this is the predicted line, f_p + d (t - t_c),
//        so a steady drift is taken out of the window; on an unconfirmed one it is a steady tone at the held frequency.
//     c. The window is Hann-weighted (periodic: w_i = 1/2 - cos(2 pi i / N) / 2, symmetric about i = N / 2),
//        zero-padded to 8 192 points (kFftPoints) and transformed by the engine's double-precision FFT, unnormalized:
//        X_k = sum_i w_i y_i e^(-j 2 pi k i / 8 192), bins of r / 8 192 = 0.1831 Hz.
//     d. The gate, against the noise floor. The peak is the largest |X_k|^2 within +/-tracker_search_hz = 15 Hz of the
//        shift (k = -81 ... 81, 163 bins). The noise power expected in one bin (derived):
//          - White noise of density N0 has the variance N0 r per sample (FS^2), so E|X_k|^2 = N0 r sum(w^2) in every
//            bin. The zero padding adds nothing to it; it only makes neighboring bins correlated.
//          - A tone of amplitude A at a bin's center gives |X_k|^2 = A^2 sum(w)^2. In the normalization in which the
//            tone reads A^2 (FS^2), the noise per bin is N0 r sum(w^2) / sum(w)^2 = N0 x 1.5 Hz, since sum w = N / 2
//            and sum w^2 = 3 N / 8 for the periodic Hann window: 1.5 / T Hz with T = N / r = 1 s, its noise bandwidth.
//          - In the channel the noise density at f Hz from the center is N0 |H(f)|^2, H the channelizer's low-pass
//            filter (unit gain at 0 Hz) and N0 the hiss estimator's density of the input. Every searched bin of a
//            station within the detector's +/-11.7 Hz of the center lies within 27 Hz of it, and there |H|^2 is 1
//            within 0.003 dB re its gain at 0 Hz (0.017 dB below it at 40 Hz, 0.052 dB at 50 Hz, 0.12 dB at 60 Hz;
//            computed from the taps, the same at 192 kHz and at 48 kHz). The tracker takes it as 1; where it is less,
//            the gate is stricter by that much, never looser, as the line falls with the noise.
//        The window is measured only if N0 > 0, the peak is at least tracker_gate_db = 9 dB above the mean noise power
//        per bin (10^0.9 = 7.94 times N0 x 1.5 Hz), and the peak is a strict local maximum with a strictly concave
//        parabola (e): at the range's edge the bin outside may be larger (a line out there, or its skirt), and a flat
//        top has no vertex. Otherwise nothing changes, except that the pending measurement (h) is dropped.
//     e. The measurement. A parabola through the peak and its two neighbors, their powers in dB re any one reference
//        (the vertex does not depend on it), has its vertex v bins from the peak: z = f_o + (k_peak + v) r / 8 192 Hz,
//        the offset at t_c (a window's residual is its weighted average, the value at its center under a steady drift;
//        the controller's Ruling 26).
//     f. Its variance, from the line's signal-to-noise ratio rho = |X_peak|^2 / (N0 r sum(w^2)) - 1, the peak's power
//        over the noise per bin less the noise's own share (positive, the gate being above 0 dB re the mean noise
//        power per bin):
//            R = c / rho + (0.08 mHz)^2,
//        the Cramer-Rao form, a variance inversely proportional to the signal-to-noise ratio, with c = 0.2833 Hz^2
//        (kVarianceScaleHz2) and a floor (kBiasFloorHz) for the parabola's own bias on a clean tone.
//     g. A confirmed track. The acceptance window is min(3 sqrt(S), tracker_search_hz), S = P_p[0][0] + R. A
//        measurement within it of f_p updates the filter: K = P_p H' / S with H = [1 0], f^ and d corrected, P in the
//        Joseph form, t_s = t_c; the pending measurement is dropped. The cap never changes what is accepted: a
//        measurement lies within 81.5 bins, 14.92 Hz, of f_o, which is f_p on a confirmed track, where 3 sqrt(S) is at
//        most 3 sqrt(0.0499 + 0.2833 / 6.94) Hz = 0.90 Hz at the default gate.
//     h. Otherwise (an unconfirmed track, or a measurement outside the acceptance window) the measurement must agree
//        with the previous window's pending one:
//            |z2 - z1| <= 3 sqrt(R1 + R2 + (sigma_d g)^2),   g = t_c2 - t_c1 = 0.5 s,
//        sigma_d = tracker_drift_sd_hz_per_s = 0.3 Hz/s (see Interpretations). Two that agree restart the filter at the
//        second, with the two-point estimate:
//            f^ = z2,   d = (z2 - z1) / g,   P = [[R2, R2 / g], [R2 / g, (R1 + R2) / g^2]],   t_s = t_c2.
//        A measurement that does not update the filter becomes the pending one for the next window (one that restarted
//        it too); a window that is not measured drops it, so the two are always consecutive windows. So, at the start
//        and whenever the track is unconfirmed, two consecutive measured windows that agree acquire a carrier; on a
//        confirmed track two consecutive measurements outside the acceptance window that agree are a new carrier (a QSO
//        turnover), and one alone is ignored.
//  3. The oscillator. offset_hz(t) = f^ + d min(t - t_s, t_hold - t_s), t_hold the last time at which 3 sigma_f(t) <=
//     0.67 Hz, with sigma_f(t)^2 = P[0][0] + 2 (t - t_s) P[0][1] + (t - t_s)^2 P[1][1] + q (t - t_s)^3 / 3 (found by
//     bisection to 1e-12 relative). It follows the drift while the track is trusted and holds its frequency from then
//     on: no drift extrapolation while unconfirmed (the owner, 2026-10-20). sigma_f(t) does not decrease after t_s, as
//     P[0][1] is never negative (0 at the start; a prediction adds dt P[1][1] + q dt^2 / 2; an update multiplies it by
//     R / S; a restart sets R2 / g), so the trusted times are one interval. Before t_s the line is carried backward.
//
// Values, their classes and sources.
//   - tracker_window_s 1.0 s, tracker_hop_s 0.5 s, tracker_search_hz 15 Hz: placeholders (plan, D6). 8 192 points:
//     heuristic (the plan's choice).
//   - tracker_gate_db 9 dB re the mean noise power per bin: derived (the owner's decision B; Derived).
//   - The noise per bin, N0 x 1.5 Hz: derived (2d).
//   - The decoder's tolerance, 0.67 Hz: derived (plan 2.9, item 5): a residual Delta f costs under 1 dB of signal power
//     while Delta f T <= 0.26 (sinc^2(0.26) = 0.797), and the default fading spread's blocks are T = 390 ms.
//   - 3 sigma, in the trust rule, the acceptance window and the agreement test: the standard choice (heuristic).
//   - c = 0.2833 Hz^2: measured (the fit, Derived). The floor, 0.08 mHz: measured (the first tracker: a calculation of
//     the same estimator on clean tones).
//   - The start's offset sd, 5 Hz: placeholder (the controller's ruling; the detector's anchor is within +/-11.7 Hz).
//   - tracker_drift_sd_hz_per_s, 0.3 Hz/s: sourced. tracker_drift_noise, 1.5e-4 Hz^2/s^3: derived from it and a
//     heuristic ten minutes (Derived).
//
// Derived.
//   - The gate. A keyed carrier's line stands 10 log10(0.44^2 / (1.5 Hz / 500 Hz)) = 18.1 dB + S500 above the mean
//     noise power per bin. The perfect decoder's threshold at 5.2 WPM, the plan's slowest speed, is S500 = 12 dB -
//     10 log10(600 / 5.2) = -8.6 dB, where the line stands 9.5 dB above it, and a steady line there passes the 9 dB
//     gate in 63.6% of windows (the Rician distribution of |line + noise|^2; 88.2% for a gate of 7 dB and 24.3% for
//     one of 11 dB re the mean noise power per bin): the tracker updates at least half the time there.
//   - Noise alone. One bin's power is exponential and passes 9 dB re its mean with probability e^-7.94 = 3.55e-4; 163
//     independent bins would put 5.6% of windows over the gate. The bins are correlated (the Hann window, the 5.46-fold
//     zero padding), and 1.48% is measured, as from 42 independent bins.
//   - The variance's fit. For a unit carrier rho = 0.44^2 / (N0 x 1.5 Hz) = 64.5 x 10^(S500 / 10) (18.1 dB re the
//     mean noise power per bin at 0 dB S500). The least-squares fit of ln(var) = ln c - ln rho (the slope fixed at the
//     Cramer-Rao form's -1) to the first tracker's scatter of the keyed carrier gives c = 0.2833 Hz^2, the geometric
//     mean of var x rho: 0.2746, 0.2643, 0.2794, 0.2846 and 0.3162 Hz^2 at -5, 0, 5, 10 and 20 dB S500. The residuals
//     (the measured sd over the fit's, minus 1) are -1.5%, -3.4%, -0.7%, +0.2% and +5.6%: sd 0.116, 0.064, 0.037,
//     0.021 and 0.007 Hz against 0.118, 0.066, 0.037, 0.021 and 0.0066 Hz. With the slope free the fit is 0.2434 Hz^2
//     x rho^-0.973. Beside it, the theory: the Cramer-Rao bound for a tone in complex white noise is 1 / (pi^2 T^2 rho)
//     = 0.1013 Hz^2 / rho with this rho (the Hann bin's peak signal-to-noise ratio), the Hann window's peak has the
//     asymptotic variance 2.343 times that ((integral w^2 u^2) / (integral w u^2)^2 / 12), 0.2374 Hz^2 / rho, and c is
//     1.19 times that.
//   - The drift (sourced unless marked; the owner asked for sources). Older rigs' specified drift: a Kenwood TS-830S
//     stays within 1 kHz in the first hour after a 1-minute warm-up and within 100 Hz per 30 minutes after; a Yaesu
//     FT-77 drifts under 300 Hz in the first 30 minutes after a 10-minute warm-up. That is at most about 0.3 Hz/s
//     (1 kHz / 3 600 s = 0.28 Hz/s), and 0.06 Hz/s after warm-up. Ionospheric Doppler: 0.5 to 1.2 Hz around sunrise
//     and sunset, over about an hour. Modern synthesized rigs: under 0.01 Hz/s (general knowledge, not sourced). The
//     start's drift sd is the 0.3 Hz/s, and the drift may change by about that over ten minutes (heuristic):
//     q = (0.3 Hz/s)^2 / 600 s = 1.5e-4 Hz^2/s^3. The drift does not decay; the trust rule's hold (stage 3) takes its
//     place.
//
// Measured (Task 6b; Windows, MSVC; engine/tests/test_signals.hpp's signals, PARIS with 5 ms edges and white noise,
// N0 the noise's own; 1 000 seeds of each test as tracker_test.cpp defines it, unless said). They bear on the values
// above; they verify none of them.
//   - The plan's five tests, the share of seeds that meet them: HoldsAKeyedCarrier 94.8% (the worst error from 3 s on:
//     median 0.22 Hz, 90th percentile 0.52 Hz, 99th 10.3 Hz), FollowsDrift 99.6% (median 0.071 Hz, 99th 0.55 Hz, worst
//     0.92 Hz; the slope at the end within 0.021 Hz/s of 0.3 Hz/s), StaysPutOnNoise 99.5% (five runs over 2 Hz rms, the
//     worst 6.5 Hz rms; none left +/-15 Hz), RelocksAtATurnover 99.9%, ResistsAStrongNeighbor 43.1% and 43.0% (the
//     neighbor 50 Hz above and below).
//   - The gate at 7, 9 and 11 dB re the mean noise power per bin: noise alone measured 20.2%, 1.48% and 0.013% of its
//     windows (one per 2.5 s, 34 s and 62 min) and restarted the filter once per 1.6 min, once per 167 min and never in
//     1 000 min; StaysPutOnNoise 62.3%, 99.5% and 100%; the other four tests within 0.2% of the same at all three. At
//     the perfect decoder's threshold (PARIS at 5.2 WPM, -8.6 dB S500; 200 runs of 60 s) 80.6%, 65.6% and 43.9% of the
//     windows were measured, and 58.7%, 54.0% and 40.4% within 0.67 Hz of the line.
//   - The variance model (200 runs of 20 s per level, the tracker started on the carrier, the windows within 1 Hz of
//     it): a steady carrier's mean squared error is 0.86 times R at every level from -5 to +20 dB S500 (0.2374 /
//     0.2833 = 0.84, the Hann peak's theory); the keyed carrier's (PARIS, 25 WPM) is 1.78, 1.65, 1.55, 1.53 and 1.54
//     times R at -5, 0, 5, 10 and 20 dB S500 (rms 0.163, 0.098, 0.055, 0.030 and 0.0096 Hz). The first tracker's
//     scatter, which the fit follows, came from the windows above its 13 dB gate re the median bin: those with strong
//     lines.
//   - Off the line: of the keyed carrier's measured windows 10.2%, 4.6% and 0.3% lay more than 1 Hz from it at -5, 0
//     and +5 dB S500 (a keying sideband or a noise peak was larger), none at +10 dB S500 and above.
//   - Consecutive windows share half their samples, and their measurements' errors correlate at -0.35 (the keyed
//     carrier at 0 dB S500).
//   - Drift beyond the physical range (FollowsDrift's signal at other rates, 200 seeds each): 99.5%, 98.5% and 77.5% of
//     seeds meet 0.67 Hz at 0.5, 0.7 and 1.0 Hz/s, none at 1.5, 2.0 and 2.7 Hz/s, where two windows 0.5 s apart differ
//     by more than the agreement test's limit (about 0.45 to 0.55 Hz) and the carrier is never acquired.
//
// Limitations, known.
//   - A strong neighbor's keying sidebands. A keyed carrier at +20 dB S500 50 Hz away puts sideband lines into the
//     search range: alone, the largest bin within +/-15 Hz of the weak station's frequency stands 13.7 dB (median; 10.0
//     to 15.7 dB from the 10th to the 90th percentile) above the mean noise power per bin and passes the gate in 96.7%
//     of windows. Where they outshine the weak line in two consecutive windows, which share half their samples, the two
//     agree and the track moves to them (ResistsAStrongNeighbor, 43%). The plan expected only nearer neighbors'
//     sidebands in the search range (Review focus 5).
//   - The station's own sidebands. At 0 dB S500 the line of a keyed carrier falls below one of its sidebands (10.4 Hz
//     away at 25 WPM, the dots' rate) in some windows; in 2.1% of HoldsAKeyedCarrier's runs two such windows came in a
//     row and the track moved there for about a second.
//   - A partly filled window. A station's first window holds only part of its keying: its peak is broad, its
//     measurement much poorer than its signal-to-noise ratio says (-0.49 Hz off against an R of (0.085 Hz)^2 in one
//     run), and, under drift, it belongs to the keying's centroid rather than to the window's center. The restart's
//     two-point drift then starts wrong (near 0.9 Hz/s for a steady carrier), with too small a covariance, and the
//     trusted oscillator carries it until the next restart: in 3.1% of HoldsAKeyedCarrier's runs the worst error from
//     3 s on was 0.67 to 2 Hz this way.
//   - The variance model underestimates a keyed carrier's scatter by a factor of 1.5 to 1.8 in variance (Measured), so
//     the covariance, the trust rule and the acceptance window are optimistic for keyed signals.
//   - The agreement test and the two-point covariance take consecutive windows' errors as independent; at a
//     correlation of -0.35 the difference of two has 1.35 times the variance assumed.
//   - The hiss estimate's own error moves the gate: an N0 2% low (its relative rms error in steady state) puts it at
//     8.9 dB re the true mean noise power per bin, and one 18% low (one standard deviation after one frame) at 8.1 dB
//     re it.
//   - Two carriers inside the search range give a peak at the larger, and two within about 2 Hz look like one.
//   - A drift faster than about 1 Hz/s is not acquired (Measured): the development set's tuning group, up to 2.7 Hz/s,
//     is beyond the physical range and not tuned for (the owner, 2026-10-20).
//
// Interpretations (the brief left these to the builder).
//   - sigma_d in the agreement test, "the filter's drift standard deviation", is tracker_drift_sd_hz_per_s, the drift's
//     standard deviation in the filter's model, not sqrt(P[1][1]): the test compares z2 with z1, not with z1 + d g, so
//     the drift's spread about 0 belongs in it, and the two may come from a new carrier, whose drift the old track's
//     covariance does not describe. Measured with sqrt(P[1][1]) (1 000 seeds): FollowsDrift 97.0% (a restart from a
//     poor first window leaves it near 0.06 Hz/s, and the true 0.15 Hz between windows fails the test until 3.5 s),
//     HoldsAKeyedCarrier 95.3%, ResistsAStrongNeighbor 46% and 47%, the others the same.
//   - "The next measured window" is the next window, 0.5 s later, and it must be measured. A measurement kept across
//     unmeasured windows would meet the next false peak whenever it came, with a drift term (sigma_d x gap)^2 that
//     grows without bound.
//   - The measurement that restarted the filter stays pending, so a weak track, unconfirmed after each restart,
//     restarts at every window that agrees with the one before.
//   - The hold begins where 3 sigma_f reaches 0.67 Hz, computed from the state, not at the next window, and the
//     window's shift (2b) follows the oscillator, so the search is centered on the held frequency.
//   - The decoder's tolerance is the constant 0.67 Hz, as the brief gives it; it does not follow the configured block
//     length (fading_spread_hz, block_loss_db).
#pragma once

#include "kz4ap/types.hpp"
#include "kz4ap/viterbi/config.hpp"

#include <complex>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <vector>

namespace kz4ap::viterbi {

class CarrierTracker {
public:
    // What a window did (stage 2).
    enum class Outcome {
        NotMeasured,  // no noise estimate (N0 = 0), no peak at or above the gate, or no vertex (2d)
        Updated,      // a confirmed track, the measurement inside the acceptance window: the Kalman update (2g)
        Pending,      // kept for the next window's agreement test (2h)
        Restarted,    // agreed with the previous window's pending measurement: the filter restarted here (2h)
    };

    // The latest window, for tests and for the plan's measurement of the tracker (R9). NaN where a value does not apply
    // to the window.
    struct Window {
        double center_s = 0.0;                // s: the window's center, the time of its measurement
        Outcome outcome = Outcome::NotMeasured;
        bool confirmed = false;               // 3 sigma_f <= 0.67 Hz at center_s (2a)
        double frequency_sd_hz = 0.0;         // Hz: sigma_f, the predicted frequency's standard deviation at center_s
        double noise_per_bin_fs2 = 0.0;       // FS^2: the noise power expected in one bin, N0 x 1.5 Hz (2d)
        double peak_fs2 = 0.0;                // FS^2: the largest searched bin's power, in the same normalization
        double measured_hz = std::numeric_limits<double>::quiet_NaN();    // Hz: the measurement z (2e)
        double variance_hz2 = std::numeric_limits<double>::quiet_NaN();   // Hz^2: its variance R (2f)
        double acceptance_hz = std::numeric_limits<double>::quiet_NaN();  // Hz: min(3 sqrt(S), search), confirmed (2g)
        double agreement_hz = std::numeric_limits<double>::quiet_NaN();   // Hz: the limit against the pending one (2h)
    };

    // How many windows had each outcome, and how many found the track unconfirmed, since the start.
    struct Counts {
        std::uint64_t windows = 0;
        std::uint64_t not_measured = 0;
        std::uint64_t updated = 0;
        std::uint64_t pending = 0;
        std::uint64_t restarted = 0;
        std::uint64_t unconfirmed = 0;
    };

    // rate_hz: the channel's sample rate, samples/s. start_offset_hz: the engine's frequency anchor, Hz from the
    // channel's center. The tracker_* fields of config are read once. Throws std::invalid_argument for a rate_hz that
    // is not positive and finite, a start_offset_hz that is not finite, a window outside 2 ... 8 192 samples, a hop
    // under half a sample, a search range under one bin (0.18 Hz at 1 500 samples/s) or over half the sample rate, a
    // gate that is not positive and finite (dB re the mean noise power per bin), and a drift sd or drift noise that is
    // negative or not finite.
    CarrierTracker(double rate_hz, const ViterbiConfig& config, double start_offset_hz);

    // The channel's next samples (complex, FS), the first at t0_s, s, with the hiss estimator's noise density around
    // the channel, FS^2/Hz (0: no estimate yet, and nothing is measured). Every window that the samples complete is
    // measured, in order (stage 2), so a call may make none, one or several. An empty block does nothing. Throws
    // std::invalid_argument for a t0_s that is not finite, a density that is negative or not finite and, after the
    // first block, a t0_s that is not where the previous block ended (the first block's t0_s plus the samples so far
    // over rate_hz, to within half a sample); the tracker is left as it was.
    void push(std::span<const Sample> samples, double t0_s, double noise_density_fs2_per_hz);

    // The oscillator's offset at time t_s, Hz from the channel's center (stage 3): the latest update's line until the
    // hold, its frequency from then on. Before the first restart it is the start. The decoder asks for the times of
    // the block it has just pushed.
    double offset_hz(double t_s) const;

    // Hz: sigma_f at time t_s, the standard deviation of the state's line predicted to t_s (stage 3): the trust rule's
    // quantity.
    double frequency_sd_hz(double t_s) const;

    // FS^2: the noise power the gate expects in one bin for a hiss density noise_density_fs2_per_hz (FS^2/Hz),
    // N0 r sum(w^2) / sum(w)^2, N0 x 1.5 Hz for the default window (2d).
    double noise_power_per_bin_fs2(double noise_density_fs2_per_hz) const;

    const Window& last_window() const { return last_; }

    const Counts& counts() const { return counts_; }

private:
    // Measures the window of window_samples_ samples that begins at sample index `first` (stage 2).
    void measure(std::uint64_t first, double noise_density_fs2_per_hz);

    // Hz^2: the variance of the state's line predicted dt_s seconds from the state's time (stage 3).
    double predicted_variance_hz2(double dt_s) const;

    // Sets hold_after_s_ from the state's covariance (stage 3).
    void set_hold();

    // Fixed by the constructor.
    double rate_hz_ = 0.0;                   // samples/s
    std::size_t window_samples_ = 0;         // samples: tracker_window_s, rounded
    std::size_t hop_samples_ = 0;            // samples: tracker_hop_s, rounded
    std::size_t search_bins_ = 0;            // bins: K, the searched bins are k = -K ... K
    double search_hz_ = 0.0;                 // Hz: tracker_search_hz, the acceptance window's cap
    double gate_ratio_ = 0.0;                // the peak's power over the noise per bin: 10^(tracker_gate_db / 10)
    double drift_noise_ = 0.0;               // Hz^2/s^3: q
    double agreement_drift_sd_hz_per_s_ = 0.0;  // Hz/s: sigma_d of the agreement test
    double sum_w_ = 0.0;                     // the window's sum
    double sum_w2_ = 0.0;                    // the sum of its squares
    std::vector<double> hann_;               // the window, window_samples_ values
    std::vector<std::complex<double>> spectrum_in_;   // the shifted, windowed, zero-padded window (8 192 points)
    std::vector<std::complex<double>> spectrum_out_;  // its transform

    // The filter: the state at state_time_s_, its covariance, and the hold.
    double state_time_s_ = 0.0;      // s: t_s
    double offset_hz_ = 0.0;         // Hz: f^ at state_time_s_
    double drift_hz_per_s_ = 0.0;    // Hz/s: d
    double p_offset_ = 0.0;          // Hz^2: P[0][0]
    double p_cross_ = 0.0;           // Hz^2/s: P[0][1]
    double p_drift_ = 0.0;           // Hz^2/s^2: P[1][1]
    double hold_after_s_ = 0.0;      // s: t_hold - t_s (0: holding from t_s; +infinity: never)
    bool pending_ = false;           // the previous window's measurement waits for this window's agreement
    double pending_hz_ = 0.0;        // Hz: its value
    double pending_variance_hz2_ = 0.0;  // Hz^2: its variance
    double pending_time_s_ = 0.0;    // s: its window's center

    // The stream.
    bool started_ = false;            // a block has been pushed
    double origin_s_ = 0.0;           // s: the time of sample 0
    std::uint64_t seen_ = 0;          // samples received
    std::uint64_t next_window_ = 0;   // the index of the first sample of the next window
    std::uint64_t buffer_first_ = 0;  // the index of buffer_[0]
    std::vector<Sample> buffer_;      // the samples from buffer_first_ on that a later window needs

    Window last_;
    Counts counts_;
};

}  // namespace kz4ap::viterbi
