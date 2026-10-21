// The new decoder's carrier-frequency tracker (design notes, topic 4, item 8; the first prototype's plan, 2.9). One
// tracker per channel follows the offset f^ of the station's carrier from the channel's center, Hz, and its drift rate,
// Hz/s, so that every hypothesis of the decoder reads the same re-centered samples. It uses none of the decoder's
// decisions: on/off keying leaves a steady line at the carrier frequency in the spectrum, whose share of the power is
// the square of the key-down fraction, 0.44^2 = 0.19 of the key-down power for PARIS text (derived: of the 50 units of
// time of P, A, R, I, S and a word gap, 22 are key-down), and the tracker follows that line. The design is the standard
// one: the maximum-likelihood estimate of a tone's frequency at the peak of its spectrum (Rife and Boorstyn 1974),
// tracked with its drift rate by a Kalman filter (Bar-Shalom, Li and Kirubarajan 2001). Every value below is a
// placeholder unless it says otherwise, to be measured on the development set (plan, R9).
//
// Use. The decoder pushes each block of the channel's samples first, and then re-centers that block's samples with
// offset_hz(t), a numerically controlled oscillator at -f^(t) whose phase the decoder integrates, so that it is
// continuous across updates. The tracker is causal: a measurement is made when a window is complete and belongs to the
// window's center, half a window earlier, and the oscillator runs on from there with the drift rate.
//
// Stage by stage (r = rate_hz, 1 500 samples/s; the values are ViterbiConfig's tracker_* fields unless a constant of
// tracker.cpp is named):
//
//  0. The clock. The first push's t0_s is the time of sample 0, and sample j is at t0_s + j / r. Blocks must be
//     contiguous (to within half a sample), and the tracker counts samples, so the way the input is split into blocks
//     changes nothing, to the last bit.
//  1. The start. f^ = start_offset_hz (the engine's frequency anchor), drift d = 0. The covariance P is diagonal at the
//     first sample: offset sd 5 Hz (the anchor: the detector's is within +/-11.7 Hz of the carrier, the labels' exact)
//     and drift sd 1 Hz/s (kInitialOffsetSdHz, kInitialDriftSdHzPerS; the controller's rulings; placeholders).
//  2. Every tracker_hop_s = 0.5 s (750 samples), once tracker_window_s = 1.0 s (N = 1 500 samples) have arrived, a
//     window is measured: the first when the first second is complete, then every hop (both rounded to whole samples
//     where used). t_c, the window's center, is half a window after its first sample.
//     a. Prediction to t_c from the state at t_s (the time of the last update or restart, at first the first sample):
//            f_p = f^ + d dt,   P_p = F P F' + Q,   F = [[1, dt], [0, 1]],   dt = t_c - t_s,
//            Q = q [[dt^3 / 3, dt^2 / 2], [dt^2 / 2, dt]],   q = tracker_drift_sd_hz_per_s^2 = (0.1 Hz/s)^2 per s.
//        This is the continuous white-noise acceleration model: the drift rate is a random walk of spectral density q,
//        Hz^2/s^3 (derived; Bar-Shalom, Li and Kirubarajan 2001). P and Q are in Hz^2, Hz^2/s and Hz^2/s^2. The
//        covariance is propagated from t_s, so it grows with the time since the last update.
//     b. The window is shifted by the predicted frequency f_p + d (t - t_c): sample i, tau_i = (i - N/2) / r s from the
//        center, is multiplied by
//            exp(-j 2 pi (f_p tau_i + d tau_i^2 / 2))
//        so that a line where the filter expects it falls at 0 Hz and a steady drift is taken out of it.
//     c. It is Hann-windowed (w_i = 1/2 - cos(2 pi i / N) / 2, symmetric about the center) and zero-padded to 8 192
//        points (kFftPoints; heuristic: 5.46 times the window, bins of r / 8 192 = 0.1831 Hz), and transformed in
//        double precision by the engine's FFT. For white noise of variance sigma^2 (FS^2) per sample a bin's power has
//        the mean sigma^2 sum(w^2) = 3 N sigma^2 / 8, and a line of amplitude a (FS) stands at (a N / 2)^2 (derived).
//     d. The searched bins are those within +/-tracker_search_hz = 15 Hz of 0: k = -81 ... 81, 163 bins. The largest
//        power among them is the peak, and the median of their powers is the reference.
//     e. The gate. The window is measured only if the peak is at least tracker_gate_db = 13 dB (of power) above the
//        median searched bin. It is not measured either when the peak is not a local maximum (at the range's edge the
//        bin outside is larger: a line outside the range, or its skirt), when a neighbor of the peak has power 0, or
//        when a power is not finite (a window of exact zeros included). Nothing changes then: the state stays at t_s.
//     f. The residual. A parabola through the peak and its two neighbors, their powers in dB (the vertex does not
//        depend on the reference), has its vertex v bins from the peak. The window's residual from the shift is
//            delta = (k_peak + v) r / 8 192 Hz,
//        and the measurement is z = f_p + delta, Hz, the offset at t_c. The residual of a window is its (weighted)
//        average, the value at the center under a steady drift (controller's ruling). The measurement's variance is R =
//        tracker_measurement_sd_hz^2 = (0.1 Hz)^2.
//     g. Validation. The innovation is delta, of variance S = P_p[0][0] + R. If |delta| <= 3 sqrt(S)
//        (kValidationSigmas, the standard gate) the filter is updated: K = P_p H' / S with H = [1 0], f^ = f_p + K[0]
//        delta, d = d + K[1] delta, P in the Joseph form, t_s = t_c, and a held measurement is dropped. Otherwise the
//        measurement is held (in place of any held before it), unless one is held already and z lies within 1 Hz of it
//        (kRestartAgreementHz): two consistent measurements off the track are a new carrier, a turnover, and the filter
//        restarts at z, with f^ = z of sd 0.1 Hz, d = 0 of sd 1 Hz/s, t_s = t_c, nothing held. "Next" means the next
//        window measured: windows that are not measured in between leave the held measurement alone.
//  3. The oscillator. offset_hz(t) = f^ + d (t - t_s) for any t, the latest update's line carried on, forward or
//     backward in time.
//
// Derived. A residual error Delta f costs under 1 dB (of signal power) while Delta f T <= 0.26 for a coherent sum of
// length T, since sinc^2(0.26) = 0.797: 0.67 Hz for the 390 ms blocks of the default fading spread (design notes, topic
// 4, item 8). Against noise alone (white, independent bins, the median bin 0.693 times the mean) a carrier line of
// share d (0.44 for PARIS) stands at 10 log10(2 N d^2 10^(S500 / 10) / (9 ln 2)) dB re the median bin, 14.7 dB re the
// median bin at -5 dB S500 (N = 1 500; the plan's "about 15 dB"). Run at the update rate, the filter has the steady
// state (the Riccati recursion at the default values and hop) of the posterior sd 0.075 Hz and 0.099 Hz/s, the
// innovation sd 0.152 Hz (so 3 sqrt(S) = 0.46 Hz), and an error of the oscillator, with the model's own random walk of
// the drift, of sd 0.115 Hz at 0.5 s and 0.168 Hz at 1.0 s after an update.
//
// Measured while building the tracker (the first prototype's Task 6; Windows, MSVC; the test signals of
// engine/tests/test_signals.hpp, PARIS with 5 ms edges, white noise; a row is 238 windows, 2 seeds of 60 s, unless
// said). They bear on the values above; they verify none of them.
//   - The peak over the median searched bin (dB re that bin), its mean, and the share of windows that pass the 13 dB
//     gate. A steady carrier at -5, 0 and 10 dB S500 (119 windows): 20.9, 25.8 and 35.5 dB, every window. The keyed
//     carrier, 25 WPM, at -5, 0, 5, 10, 20 and 30 dB S500: 10.7, 12.2, 13.0, 13.3, 13.4 and 13.5 dB, passing 17%, 40%,
//     50%, 56%, 61% and 63% of the windows, and with no noise at all 13.5 dB and 62%. At 15 WPM: 11.3, 13.7, 15.3,
//     15.9, 16.0 and 16.0 dB, 32%, 48%, 68%, 77%, 82% and 82%. The keying's own sidebands fill the searched range and
//     raise the median bin beyond the noise's, which the derivation above leaves out. A neighbor of +20 dB S500 raises
//     it more (the weak carrier, 0 dB S500: 37% alone, 3.8% with the neighbor 50 Hz above, 4.1% 50 Hz below, 26% 100 Hz
//     above; 390 windows each).
//   - The scatter of the measurement z (its error, in the windows that pass), sd: 0.116, 0.064, 0.037, 0.021 and 0.007
//     Hz at -5, 0, 5, 10 and 20 dB S500 for the keyed carrier, 25 WPM; 0.046 and 0.026 Hz at -5 and 0 dB S500 for the
//     steady one. The parabola's own bias on a clean tone is at most 0.08 mHz (calculated).
//   - Noise alone: 6.6% and 0.17% of 4 760 windows pass 10 and 13 dB re the median bin (40 seeds of 60 s), and 6.5% and
//     0.095% of the 40 000 windows of a calculation (the same estimator on white noise), not the 15% and 0.016% (once
//     in 52 minutes) that 163 independent bins give. The bins are strongly correlated (the Hann window, the 5.46-fold
//     zero padding), which lowers the maximum but makes the median bin itself uncertain; against the true median, 0.693
//     times the mean power, 13 dB is passed by 0.007% of the calculation's windows. In noise alone about one window in
//     a thousand is measured at 13 dB, one in 8 minutes.
//
// Limitations, known.
//   - A window measured in noise alone is a false alarm, and what it does depends on the covariance. The validation
//     gate 3 sqrt(S) grows with the time since the last update (S has dt^2 P_drift and q dt^3 / 3 in it): 15.3 Hz at
//     the first window, 150 Hz after 46 s. After a long silence nothing is outside it, so the false peak updates the
//     filter, moves the oscillator to wherever it lay in the searched range, and sets a drift rate from the innovation
//     (-0.65 Hz/s in one case), which offset_hz then carries on without limit. 8 of 40 noise-only runs of 60 s had such
//     an update.
//   - Updates are sparse for a keyed carrier below about +10 dB S500 (the gate above), and between them the oscillator
//     runs on the drift rate, which two or three updates leave known to a few tenths of a Hz/s: gaps of 2 to 3 s gave
//     errors of 0.3 to 0.6 Hz.
//   - The measurement noise is the one fixed R whatever the signal to noise ratio, where the scatter above runs from
//     0.007 to 0.116 Hz.
//   - The first update credits part of the start's error to drift (the start's drift sd is 1 Hz/s: 0.02 of the
//     innovation, so 0.06 Hz/s for 3 Hz), and the oscillator carries that on.
//   - Two carriers inside the searched range give a peak at the larger of them.
#pragma once

#include "kz4ap/types.hpp"
#include "kz4ap/viterbi/config.hpp"

#include <complex>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace kz4ap::viterbi {

class CarrierTracker {
public:
    // rate_hz: the channel's sample rate, samples/s. start_offset_hz: the engine's frequency anchor, Hz from the
    // channel's center. The tracker_* fields of config are read once. Throws std::invalid_argument for a rate_hz that
    // is not positive and finite, a start_offset_hz that is not finite, a window outside 2 ... 8 192 samples, a hop
    // under half a sample, a search range under one bin (0.18 Hz at 1 500 samples/s) or over half the sample rate, a
    // gate that is not finite, a measurement sd that is not positive and finite, and a drift sd that is negative or
    // not finite.
    CarrierTracker(double rate_hz, const ViterbiConfig& config, double start_offset_hz);

    // The channel's next samples (complex, FS), the first at t0_s, s. Every window that the samples complete is
    // measured, in order (stage 2), so a call may make none, one or several. An empty block does nothing. Throws
    // std::invalid_argument for a t0_s that is not finite and, after the first block, for one that is not where the
    // previous block ended, the first block's t0_s plus the samples so far over rate_hz, to within half a sample; the
    // tracker is left as it was.
    void push(std::span<const Sample> samples, double t0_s);

    // The oscillator's offset at time t_s, Hz from the channel's center: f^ + d (t_s - t_s of the latest update),
    // stage 3. Before the first update it is the start. The decoder asks for the times of the block it has just
    // pushed.
    double offset_hz(double t_s) const;

private:
    // Measures the window of window_samples_ samples that begins at sample index `first` (stage 2).
    void measure(std::uint64_t first);

    // Fixed by the constructor.
    double rate_hz_ = 0.0;                   // samples/s
    std::size_t window_samples_ = 0;         // samples: tracker_window_s, rounded
    std::size_t hop_samples_ = 0;            // samples: tracker_hop_s, rounded
    std::size_t search_bins_ = 0;            // bins: K, the searched bins are k = -K ... K
    double gate_ratio_ = 0.0;                // peak power over median power: 10^(tracker_gate_db / 10)
    double measurement_variance_hz2_ = 0.0;  // Hz^2: R
    double drift_density_hz2_per_s3_ = 0.0;  // Hz^2/s^3: q
    std::vector<double> hann_;               // the window, window_samples_ values
    std::vector<std::complex<double>> spectrum_in_;   // the shifted, windowed, zero-padded window (8 192 points)
    std::vector<std::complex<double>> spectrum_out_;  // its transform
    std::vector<double> powers_;                      // the searched bins' powers, reordered by the median

    // The filter: the state at state_time_s_ and its covariance.
    double state_time_s_ = 0.0;     // s: t_s
    double offset_hz_ = 0.0;        // Hz: f^ at state_time_s_
    double drift_hz_per_s_ = 0.0;   // Hz/s: d
    double p_offset_ = 0.0;         // Hz^2: P[0][0]
    double p_cross_ = 0.0;          // Hz^2/s: P[0][1]
    double p_drift_ = 0.0;          // Hz^2/s^2: P[1][1]
    bool held_ = false;             // a measurement outside the validation gate is held
    double held_hz_ = 0.0;          // Hz: its offset

    // The stream.
    bool started_ = false;            // a block has been pushed
    double origin_s_ = 0.0;           // s: the time of sample 0
    std::uint64_t seen_ = 0;          // samples received
    std::uint64_t next_window_ = 0;   // the index of the first sample of the next window
    std::uint64_t buffer_first_ = 0;  // the index of buffer_[0]
    std::vector<Sample> buffer_;      // the samples from buffer_first_ on that a later window needs
};

}  // namespace kz4ap::viterbi
