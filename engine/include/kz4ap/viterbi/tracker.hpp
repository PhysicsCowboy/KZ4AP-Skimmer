// The new decoder's carrier-frequency tracker (design notes, topic 4, item 8; the first prototype's plan, section 2.9;
// the standard forms the owner chose on 2026-10-20, with the refinements measured on 2026-10-21). One tracker per
// channel follows the offset f^ of the station's carrier from the channel's center, Hz, and its drift rate d, Hz/s, so
// that every hypothesis of the decoder reads the same re-centered samples. It uses none of the decoder's decisions:
// on/off keying leaves a steady line at the carrier frequency whose share of the power is the square of the key-down
// fraction, 0.44^2 = 0.19 of the key-down power for PARIS text (derived: of the 50 units of P, A, R, I, S and a word
// gap, 22 are key-down). The design is the standard one: the maximum-likelihood estimate of a tone's frequency at the
// peak of its spectrum (Rife and Boorstyn 1974), tracked with its drift rate by a Kalman filter (Bar-Shalom, Li and
// Kirubarajan 2001), behind a gate against the noise floor (a constant false-alarm rate detector), with a new track
// confirmed by two agreeing measurements. Each measurement's variance follows from its line's signal-to-noise ratio and
// from how the line fills the window, and the filter allows for the correlation of consecutive windows, which share
// half their samples.
//
// Use. Before each block the engine's hiss estimator gives N0, the noise density around the channel, FS^2/Hz
// (hiss.hpp). The decoder pushes the block with it, then re-centers the block's samples with offset_hz(t), a
// numerically controlled oscillator at -f^(t) whose phase the decoder integrates. The tracker is causal: a window is
// measured when its last sample arrives, and its measurement belongs to its center, half a window earlier. It reads no
// fading spread, the configured one or the genie's (2a).
//
// Resolution, spacing and accuracy.
//   - The sample rate, r = 1 500 complex samples/s, sets the range: +/-750 Hz.
//   - The 1 s window sets the resolution: about 1 Hz (1 / T). With the Hann taper the main lobe is +/-2 Hz wide and the
//     noise bandwidth 1.5 Hz. Two carriers within about 2 Hz look like one.
//   - The zero padding to 8 192 points only samples the same continuous spectrum every r / 8 192 = 0.183 Hz. It is
//     interpolation, not resolution.
//   - A single tone's frequency is located far more finely than the resolution. The peak of the continuous spectrum is
//     the maximum-likelihood estimate (Rife and Boorstyn 1974), and its accuracy is set by the signal-to-noise ratio
//     and the window's length: at 0 dB S500 a keyed carrier (PARIS, 25 WPM) is measured to 0.10 Hz rms in this tracker
//     (Measured), a steady one to 0.027 Hz, against the Cramer-Rao bound's 0.040 Hz for the keyed carrier's line alone.
//   - The fine spacing and the parabola only find that peak.
//
// Stage by stage (r = rate_hz; the values are ViterbiConfig's tracker_* fields unless a constant of tracker.cpp is
// named; t_s is the time of the state, f^ and d, and of its covariance P):
//
//  0. The clock. The first push's t0_s is the time of sample 0, and sample j is at t0_s + j / r. Blocks must be
//     contiguous (to within half a sample), and the tracker counts samples, so for one N0 the way the input is split
//     into blocks changes nothing, to the last bit. A window is measured with the N0 passed with the samples that
//     complete it: where N0 changes from block to block, the split decides which value a window uses.
//  1. The start. f^ = start_offset_hz (the engine's frequency anchor), d = 0, P = diag((5 Hz)^2, (0.3 Hz/s)^2)
//     (kInitialOffsetSdHz, tracker_drift_sd_hz_per_s), t_s the first sample's time, and the previous window's noise
//     component (2g) unknown. 3 sigma_f is then 15 Hz: the track starts unconfirmed, and the oscillator holds the
//     anchor until two windows agree (2h).
//  2. Every tracker_hop_s = 0.5 s (750 samples), once tracker_window_s = 1.0 s (N = 1 500 samples) have arrived, a
//     window is measured; t_c, its center, is N / (2 r) after its first sample. The window holds 8 samples at least
//     (2f), and the hop is at least half the window, so that windows two hops apart share no sample (2g).
//     a. Prediction and the trust rule. With dt = t_c - t_s,
//            f_p = f^ + d dt,   P_p = F P F' + Q,   F = [[1, dt], [0, 1]],
//            Q = q [[dt^3 / 3, dt^2 / 2], [dt^2 / 2, dt]],
//        q = tracker_drift_noise = 1.5e-4 Hz^2/s^3: the continuous white-noise acceleration model, the drift rate a
//        random walk of spectral density q (Bar-Shalom, Li and Kirubarajan 2001). sigma_f = sqrt(P_p[0][0]). The track
//        is confirmed while 3 sigma_f <= tau and unconfirmed otherwise, tau = 0.667 Hz (kToleranceHz): 0.26 cycles
//        over the decoder's default block, 195 pieces of 2 ms = 0.390 s at the default fading spread of 1 Hz (a
//        residual Delta f costs a coherent sum of length T under 1 dB of signal power while Delta f T <= 0.26). tau is
//        fixed, whatever the configured fading spread or the genie's (decided 2026-10-21 from the measurements under
//        Measured): it guards against a false update moving the frequency further than the default block tolerates.
//        Making it stricter, to follow a longer block, does not make the estimate better: an unconfirmed track is not
//        updated and only restarts from agreeing pairs of windows, the stricter tau the more tracks stay unconfirmed
//        (Limitations, the confirmation boundary), and their error grows (Measured).
//     b. The shift. Sample i, tau_i = (i - N / 2) / r from the center, is multiplied by exp(-j 2 pi phi(tau_i)), phi
//        the oscillator's phase about the center in cycles, the integral of offset_hz (stage 3) from t_c to t_c + tau:
//            phi(tau) = f_o tau + d J(tau),   J(tau) = integral_0^tau (min(u, h) - min(0, h)) du,
//        f_o = offset_hz(t_c), h = t_hold - t_c. On a confirmed track this is the predicted line, f_p + d (t - t_c),
//        so a steady drift is taken out of the window; on an unconfirmed one it is a steady tone at the held frequency.
//     c. The window is Hann-weighted (periodic: w_i = 1/2 - cos(2 pi i / N) / 2, symmetric about i = N / 2),
//        zero-padded to 8 192 points (kFftPoints) and transformed by the engine's double-precision FFT, unnormalized:
//        X_k = sum_i w_i y_i e^(-j 2 pi k i / 8 192), bins of r / 8 192 = 0.1831 Hz.
//     d. The peak and the gate, against the noise floor. The peak is the largest |X_k|^2 within +/-tracker_search_hz =
//        15 Hz of the shift (k = -81 ... 81, 163 bins). On a confirmed track the largest within the acceptance window
//        of a filled window's peak at the gate (3 sqrt(P_p[0][0] + c / (10^0.9 - 1)), 0.61 Hz on a settled track)
//        comes first, if it passes the gate and is a strict local maximum: a confirmed track looks for its own line
//        before a stronger one elsewhere in the search range. The noise power expected in one bin (derived):
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
//        top has no vertex. Otherwise nothing changes, except that the pending measurement (h) is dropped and the
//        previous window's noise component (g) becomes unknown.
//     e. The measurement. A parabola through the peak and its two neighbors, their powers in dB re any one reference
//        (the vertex does not depend on it), has its vertex v bins from the peak: z = f_o + (k_peak + v) r / 8 192 Hz,
//        the offset at t_c. A window's residual is its line's weighted average over the window, which under a steady
//        drift is the value at the window's center, so the measurement belongs to the center.
//     f. Its variance:
//            R = c / rho x F(p) / F(1) + (sigma_d u)^2 + (0.08 mHz)^2.
//        - c / rho is the Cramer-Rao form, a variance inversely proportional to the line's signal-to-noise ratio, rho
//          = |X_peak|^2 / (N0 r sum(w^2)) - 1 (the peak's power over the noise per bin, less the noise's own share;
//          positive, the gate being above 0 dB re the mean noise power per bin), c = 0.2833 Hz^2 (kVarianceScaleHz2).
//        - F(p) / F(1), the fill factor, is how much less the window measures because of how its line fills it
//          (derived). For a line whose weight in the window is g_i = w_i p_i (p its presence, 1 for a steady carrier,
//          the keying envelope for a keyed one), in noise weighted by w_i, the peak's frequency has the variance
//              var(omega) = F(p) / (2 rho),   F(p) = (sum g)^2 sum w^2 (tau - u)^2 / (sum w^2 (sum g (tau - u)^2)^2),
//          u = sum g tau / sum g the line's centroid (s from the center), rho as above; F(1) = 18.74 s^-2 gives the
//          Hann window's 0.2374 Hz^2 / rho. The profile p is read from the window itself, in kProfileBlocks = 4
//          quarters: the line's coherent sum over quarter j at the measured frequency, |S_j|^2 less the noise's
//          expected N0 r sum_j w^2 (not below 0), gives p_j = sqrt(that) / sum_j w. A station present from half the
//          window on has F(p) / F(1) = 20.0 (exactly, from 0.4 of it 6.2, from 0.6 of it 81; the quarters read some of
//          these low, Limitations): its first window cannot set a restart's drift. If no quarter is above its noise
//          (possible only under a gate below 10 log10(4) = 6.02 dB re the mean noise power per bin), the profile is
//          taken as filled. The window holds 8 samples at least, so that every quarter holds a sample of positive
//          weight (the periodic Hann window's first sample is 0).
//        - (sigma_d u)^2, sigma_d = tracker_drift_sd_hz_per_s, is the drift's share of attributing to the center a
//          measurement whose line's centroid lies u away (derived; 0.045 Hz for a half-filled window). sigma_d is the
//          drift's prior spread, which errs as follows (derived). On a confirmed track the window is shifted by the
//          estimated drift (2b): the drift left in it, d - d^, has the standard deviation sqrt(P[1][1]), usually far
//          below sigma_d, and is correlated with the state, so the term is larger than needed (the standard form, u in
//          the measurement's row, H = [1, u, ...], is not used). In the agreement test (2h) the two windows' terms and
//          the gap's (sigma_d g)^2 come from one drift, whose exact share is sigma_d^2 (g + u2 - u1)^2; their sum
//          exceeds it by 2 sigma_d^2 (g (u1 - u2) + u1 u2), which is positive whenever u1 > u2, as at a station's start
//          or end (0.272 s^2 x sigma_d^2 against 0.124 s^2 x sigma_d^2 for a start at half the first window), and
//          negative where the line is weaker in the half the two windows share, u1 < 0 < u2 (0.294 s^2 x sigma_d^2
//          against 0.634 s^2 x sigma_d^2 for two windows half filled that way, 0.031 Hz^2 short at 0.3 Hz/s, beside
//          each one's own variance, 20 times a filled window's).
//        - The floor (kBiasFloorHz) is the parabola's own bias on a clean tone.
//     g. A confirmed track: the Kalman update when the innovation lies within the acceptance window, 3 sqrt(S) capped
//        at the search range. Consecutive windows share samples, so their errors are correlated: rho_c = -0.35 at a hop
//        of half the window (kHalfHopCorrelation, measured), and at another hop the noise-only correlation derived for
//        it (Derived) times the same ratio, -0.35 / -0.4526 (heuristic: the ratio is measured at half the window
//        only). Each error is modeled as the moving average e_k = s_k (a_k + theta a_(k-1)), a white of unit variance,
//        s_k = sqrt(R_k / (1 + theta^2)), theta the root with |theta| < 1 of theta / (1 + theta^2) = rho_c (derived;
//        -0.408 at the default hop). The model holds while windows two hops apart share no sample, which the hop of
//        at least half the window ensures. The state carries a_(k-1), the previous window's component, with its
//        covariances (P_fa, P_da, P_aa). The measurement is z = f + s (theta a_(k-1) + a_k), the state augmented with
//        a_k (variance 1) and no other noise:
//            H = [1, 0, s theta, s],   S = P_p[0][0] + 2 s theta P_fa + s^2 theta^2 P_aa + s^2,
//            innovation = z - (f_p + s theta a^),   K = P H' / S,   P = (I - K H) P (I - K H)' (the Joseph form),
//        then t_s = t_c, a_k kept as the next window's previous component, and the pending measurement dropped. A
//        window that is not measured, or not used, leaves the next one's previous component unknown (estimate 0,
//        variance 1, independent of the state). The cap changes what is accepted only where |s theta a^| > 0.08 Hz: a
//        measurement lies within 81.5 bins, 14.92 Hz, of f_o, which is f_p on a confirmed track, and the prediction
//        adds s theta a^ to f_p.
//     h. Otherwise (an unconfirmed track, or a measurement outside the acceptance window) the measurement must agree
//        with the previous window's pending one:
//            |z2 - z1| <= 3 sqrt(R1 + R2 - 2 c12 + (sigma_d g)^2),   g = t_c2 - t_c1 = 0.5 s,
//        sigma_d = tracker_drift_sd_hz_per_s = 0.3 Hz/s (see Interpretations), c12 = rho_c sqrt(R1 R2) the two errors'
//        covariance (0 for windows that share no sample). Two that agree restart the filter at the second, with the
//        two-point estimate:
//            f^ = z2,   d = (z2 - z1) / g,   P = [[R2, (R2 - c12) / g], [(R2 - c12) / g, (R1 + R2 - 2 c12) / g^2]],
//            t_s = t_c2,   P_fa = -s2,   P_da = -s2 / g,   P_aa = 1 (the offset's error is minus window 2's).
//        A measurement that does not update the filter becomes the pending one for the next window (one that restarted
//        it too); a window that is not measured drops it, so the two are always consecutive windows. So, at the start
//        and whenever the track is unconfirmed, two consecutive measured windows that agree acquire a carrier; on a
//        confirmed track two consecutive measurements outside the acceptance window that agree are a new carrier (a QSO
//        turnover), and one alone is ignored.
//  3. The oscillator. offset_hz(t) = f^ + d min(t - t_s, t_hold - t_s), t_hold the last time at which 3 sigma_f(t) <=
//     tau, with sigma_f(t)^2 = P[0][0] + 2 (t - t_s) P[0][1] + (t - t_s)^2 P[1][1] + q (t - t_s)^3 / 3 (found by
//     bisection to 1e-12 relative). It follows the drift while the track is trusted and holds its frequency from then
//     on: no drift extrapolation while unconfirmed (the owner, 2026-10-20). That cubic's derivative, 2 P[0][1] +
//     2 P[1][1] (t - t_s) + q (t - t_s)^2, does not decrease after t_s, so the variance falls at most once and then
//     rises: if the track is trusted at t_s, the trusted times are one interval from t_s; if not, the oscillator holds
//     from t_s. Before t_s the line is carried backward.
//
// Values, their classes and sources.
//   - tracker_window_s 1.0 s, tracker_hop_s 0.5 s, tracker_search_hz 15 Hz: placeholders (the plan, section 2.9).
//     8 192 points: heuristic (the plan's choice).
//   - tracker_gate_db 9 dB re the mean noise power per bin: derived (Derived).
//   - The noise per bin, N0 x 1.5 Hz: derived (2d).
//   - tau, 0.667 Hz: derived for the decoder's default block (the plan, section 2.9: a residual Delta f costs under
//     1 dB of signal power while Delta f T <= 0.26, sinc^2(0.26) = 0.797), and held at that value at every fading
//     spread (decided 2026-10-21 from measurements; the reason in 2a).
//   - The window's 8 samples at least (2f) and the hop of at least half the window (2g): derived.
//   - 3 sigma, in the trust rule, the acceptance window and the agreement test: the standard choice (heuristic).
//   - c = 0.2833 Hz^2: measured (the fit, Derived). The floor, 0.08 mHz: measured (a calculation of the same estimator
//     on clean tones).
//   - The fill factor F(p) / F(1): derived; its 4 quarters (kProfileBlocks): measured (the choice among 2, 3, 4, 8).
//   - rho_c = -0.35 at a hop of half the window: measured (the keyed carrier at 0 dB S500, PARIS at 25 WPM); at other
//     hops, heuristic (2g). theta = -0.408: derived from it.
//   - The start's offset sd, 5 Hz: placeholder (the detector's anchor is within +/-11.7 Hz of the carrier).
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
//     Cramer-Rao form's -1) to the scatter of the keyed carrier measured with the tracker's first design (a fixed
//     measurement variance, a gate against the median bin) gives c = 0.2833 Hz^2, the geometric mean of var x rho:
//     0.2746, 0.2643, 0.2794, 0.2846 and 0.3162 Hz^2 at -5, 0, 5, 10 and 20 dB S500. The residuals (the measured sd
//     over the fit's, minus 1) are -1.5%, -3.4%, -0.7%, +0.2% and +5.6%: sd 0.116, 0.064, 0.037, 0.021 and 0.007 Hz
//     against 0.118, 0.066, 0.037, 0.021 and 0.0066 Hz. With the slope free the fit is 0.2434 Hz^2 x rho^-0.973.
//     Beside it, the theory: the Cramer-Rao bound for a tone in complex white noise is 1 / (pi^2 T^2 rho) =
//     0.1013 Hz^2 / rho with this rho (the Hann bin's peak signal-to-noise ratio), the Hann window's peak has the
//     asymptotic variance 2.343 times that (F(1) / (2 (2 pi)^2) = 0.2374 Hz^2 / rho), and c is 1.19 times that.
//   - The fill factor. The peak's frequency error is, to first order, Re(N1) / (A sum g (tau - u)^2), N1 = sum w_i n_i
//     (-j (tau_i - u)) e^(...) the noise's weighted derivative: the variance above, in which the signal's weights g
//     and the noise's w differ once the line does not fill the window (a station's start, its end, its keying).
//   - The consecutive windows' correlation. A window's frequency error is, from the noise, proportional to
//     sum w_i tau_i Im(n_i e^(...)); two windows a hop apart share the samples of their overlap, and the correlation is
//     sum_i b_i b_(i + hop) / sum_i b_i^2, b_i = w_i tau_i: -0.4526 for the noise alone at half the window (measured
//     on a steady carrier: -0.44), -0.276 at 0.6 of it, -0.052 at 0.75, 0 from a whole window on. On the keyed
//     carrier -0.35 is measured at half the window and used there.
//   - The confirmation boundary. After a restart from two windows of variances R1 and R2 and covariance c12, the
//     offset predicted to the next window has the variance 4 R2 + R1 - 4 c12 + q g^3 / 3 (the last 6e-6 Hz^2):
//     6.4 R for R1 = R2 = R and c12 = -0.35 R. That window is confirmed only if 3 sqrt(6.4 R) <= tau, that is
//     R <= tau^2 / 57.6 = 7.7e-3 Hz^2: for keyed PARIS at a fill factor of 1, R = 0.2833 Hz^2 / rho with
//     rho = 64.5 x 10^(S500 / 10) at its expected value, above -2.45 dB S500. Each window's R moves with its noise and
//     its keying (the measured rho and the fill factor), so the boundary is soft (Measured).
//   - The drift (sourced unless marked). Older rigs' specified drift: a Kenwood TS-830S stays within 1 kHz in the
//     first hour after a 1-minute warm-up and within 100 Hz per 30 minutes after (rigpix's specification page,
//     https://www.rigpix.com/kenwood/ts830s.htm); a Yaesu FT-77 drifts under 300 Hz in the first
//     30 minutes after a 10-minute warm-up (rigpix, https://www.rigpix.com/yaesu/ft77.htm). That is at most about
//     0.3 Hz/s (1 kHz / 3 600 s = 0.28 Hz/s), and 0.06 Hz/s after warm-up. Ionospheric Doppler: 0.5 to 1.2 Hz around
//     sunrise and sunset, over about an hour (WWV received over years: "HF Signal Propagation and Frequency Accuracy",
//     febo.com, https://febo.com/pages/hf_stability/; 3.5 and 7 MHz paths: "Reflection Height", RSGB RadCom, August
//     2024, pages 42 to 44,
//     https://hamsci.org/sites/default/files/publications/p42_44_Reflection_Height_RSGB_RadCom_Aug24.pdf). Modern
//     synthesized rigs: under 0.01 Hz/s (general knowledge, not sourced). The start's drift sd is the 0.3 Hz/s, and
//     the drift may change by about that over ten minutes (heuristic): q = (0.3 Hz/s)^2 / 600 s = 1.5e-4 Hz^2/s^3.
//     The drift does not decay; the trust rule's hold (stage 3) takes its place.
//
// Measured (Windows, MSVC; engine/tests/test_signals.hpp's signals, PARIS with 5 ms edges and white noise, N0 the
// noise's own; 1 000 seeds of each test as tracker_test.cpp defines it, at its 0.6667 Hz, unless said). They bear on
// the values above; they verify none of them.
//   - The plan's five tests, the share of seeds that meet them: HoldsAKeyedCarrier 99.9% (the worst error from 3 s on:
//     median 0.13 Hz, 90th percentile 0.28 Hz, 99th 0.53 Hz, worst 0.68 Hz; with the station's start drawn anywhere
//     in [0.5 s, 1 s), 99.8%), FollowsDrift 100% (worst 0.21 Hz; the slope at the end within 0.015 Hz/s of 0.3 Hz/s),
//     StaysPutOnNoise 99.2% (eight runs over 2 Hz rms, the worst 9.0 Hz rms; none left +/-15 Hz), RelocksAtATurnover
//     100% (worst 0.53 Hz), ResistsAStrongNeighbor 72.3% and 72.4% (the neighbor 50 Hz above and below; median 0.41
//     and 0.43 Hz, 90th percentile 0.87 and 0.89 Hz). With the fading spread at 0 Hz and at 0.3 Hz every one of these
//     is the same, to the last digit: the tracker does not read it.
//   - The trust rule's tolerance following the decoder's block instead, 0.26 / T_block (measured by an independent
//     review, 2026-10-21, on seeds 1 to 200 of HoldsAKeyedCarrier's signal, 0 dB S500; a run "captured" where the
//     oscillator is more than 2 Hz off the carrier at some window centered after 3 s): at a fading spread of 0.3 Hz
//     (0.200 Hz) and of 0.1 Hz (0.067 Hz) no window from 3 s on was confirmed, 96.5% of runs stayed within 0.667 Hz
//     and the median worst error was 0.267 Hz, against every window confirmed, 100% and 0.145 Hz with tau; at 0.3 Hz
//     and +5 dB S500 still no window was confirmed (a median worst error of 0.168 Hz against 0.081 Hz).
//     ResistsAStrongNeighbor's signal at 0.3 Hz: 71 of 200 runs captured each way, against 4 and 5 of 200 with tau.
//   - The confirmation boundary (HoldsAKeyedCarrier's signal at -4 dB S500): 53.1% of the windows from 3 s on were
//     confirmed, 85.7% of runs stayed within 0.667 Hz from 3 s on (the worst error's median 0.42 Hz, 90th percentile
//     0.79 Hz), and 51 of 1 000 runs were captured.
//   - The design's refinements, added one by one on the same seeds (2026-10-21; the five tests' shares, in order, at
//     0.67 Hz, without and with each): the design without them 94.8, 99.6, 99.5, 99.9, 43 / 43; the search inside the
//     acceptance window (2d): 96.5, 99.6, 99.5, 99.8, 57 / 59; then the fill factor (2f): 100, 100, 99.3, 100,
//     71 / 70; then the consecutive windows' correlation (2g, 2h): 99.9, 100, 99.2, 100, 73 / 73. Not kept:
//     confirming only from windows 1 s apart, which share no sample (with the search inside the acceptance window:
//     81.9, 100, 100, 91.8, 39 / 37; with all the rest: 96.7, 100, 99.9, 92.4, 48 / 48: a pair 1 s apart gives its
//     drift a quarter of the variance, so a poor first window's drift is trusted longer, and a new carrier is acquired
//     a window later), and multiplying the variance by 1.6 for keyed carriers (100, 100, 99.2, 100, 73 / 72 with the
//     search inside the acceptance window and the fill factor, whose keyed calibration at 25 WPM is 0.91 to 0.98
//     already: the variance would be 1.6 times the scatter).
//   - The gate at 7, 9 and 11 dB re the mean noise power per bin: noise alone measured 20.2%, 1.48% and 0.013% of its
//     windows (one per 2.5 s, 34 s and 62 min) and restarted the filter once per 0.9 min, once per 91 min and never in
//     1 000 min; StaysPutOnNoise 44.2%, 99.2% and 100%; ResistsAStrongNeighbor 71% / 70%, 73% / 73% and 78% / 78%
//     (at 0.67 Hz); the other three within 0.1% of the same at all three. At the perfect decoder's threshold (PARIS at
//     5.2 WPM, -8.6 dB S500; 200 runs of 60 s) 80.5%, 65.5% and 43.9% of the windows were measured, and 58.8%, 54.1%
//     and 40.4% within 0.67 Hz of the line.
//   - The variance against the scatter (200 runs of 20 s per level, the tracker started on the carrier, the measured
//     windows within 1 Hz of it): the keyed carrier's mean squared error over R is 0.98, 0.96, 0.91, 0.85 and 0.57 at
//     -5, 0, 5, 10 and 20 dB S500 at 25 WPM (rms 0.163, 0.102, 0.055, 0.030 and 0.0096 Hz); 0.78, 0.79, 0.79, 0.74 and
//     0.50 at 40 WPM; 1.27, 1.18, 1.13, 1.08 and 1.41 at 12 WPM, where the error stops falling at about 0.13 Hz rms
//     from +5 dB S500 on (conjectured: the periodic test text's keying lines, 0.2 Hz apart at 12 WPM, inside the line's
//     main lobe). A steady carrier's is 0.84 to 0.86 (0.2374 / 0.2833 = 0.84, the Hann peak's theory). Before the fill
//     factor, the keyed carrier's was 1.5 to 1.8 at 25 WPM: the keying's uneven spread through the window is that
//     excess.
//   - Consecutive windows' errors correlate at -0.37 to -0.41 for the keyed carrier at 25 WPM, -0.30 to -0.37 at
//     40 WPM, -0.08 to -0.33 at 12 WPM, and -0.44 for a steady carrier; -0.349 over HoldsAKeyedCarrier's runs.
//   - Off the line: 0.30% of the keyed carrier's measured windows (0 dB S500) lay more than 1 Hz from it (4.7% in the
//     design without the refinements: the search inside the acceptance window keeps a confirmed track off the
//     keying's own sidebands).
//   - Drift beyond the physical range (FollowsDrift's signal at other rates, 200 seeds each, at 0.67 Hz): 100% of
//     seeds meet it at 0.5 and 0.7 Hz/s, 99% at 1.0, 64.5% at 1.5, 9% at 2.0, none at 2.7 Hz/s.
//
// Limitations, known.
//   - The confirmation boundary (Derived): after a restart from two windows whose variances are over tau^2 / 57.6 =
//     7.7e-3 Hz^2 (equal variances; for keyed PARIS at a fill factor of 1, on average below -2.45 dB S500), the next
//     window is not confirmed. A track that stays so is never updated: it restarts from each agreeing pair of windows,
//     averages no more than two, and loses the search inside the acceptance window (2d), which keeps it off the
//     keying's own sidebands. The boundary is soft, each window's variance moving with its noise and its keying: at
//     -4 dB S500 and 25 WPM, 53.1% of the windows from 3 s on are confirmed, 85.7% of runs stay within 0.667 Hz and
//     5.1% are captured (Measured). Slower stations decode at lower levels than the tracker holds: the perfect
//     decoder's threshold is -8.6 dB S500 at 5.2 WPM.
//   - The decoder's need at longer blocks. At fading spreads below 1 Hz the decoder's blocks are longer than 0.390 s
//     (1.30 s at 0.3 Hz), and a coherent sum of length T loses under 1 dB of signal power only while the residual
//     stays within 0.26 / T (0.20 Hz at 1.30 s); the decoder sums coherently only within a mark, so T is at most the
//     mark's length (a dash's median: 0.144 s at 25 WPM, 0.691 s at 5.2 WPM). The tracker's error is what it is at
//     every spread: the worst from 3 s on at 0 dB S500 and 25 WPM has a median of 0.13 Hz and a 90th percentile of
//     0.28 Hz (Measured). The plan's fading-spread run (0.3, 2 and 5 Hz against 1 Hz) measures the cost.
//   - A strong neighbor's keying sidebands. A keyed carrier at +20 dB S500 50 Hz away puts sideband lines into the
//     search range: alone, the largest bin within +/-15 Hz of the weak station's frequency stands 13.7 dB (median; 10.0
//     to 15.7 dB from the 10th to the 90th percentile) above the mean noise power per bin and passes the gate in 96.7%
//     of windows. A confirmed track keeps to its own line (2d), but an unconfirmed one (at the start, or after a hold)
//     takes the largest bin, and where two consecutive windows find the same sideband line they agree and the track
//     moves there (about 2% of runs). In most of the runs that fail, the sidebands' energy near the weak line pulls one
//     of its measurements off by half a hertz or more, far beyond its variance; inside the acceptance window, it
//     updates the filter, kicks the drift, and the oscillator passes 0.667 Hz before the next restart corrects it (one
//     run: 0.58 Hz off against an sd of 0.12 Hz at 2.5 s, 0.7 Hz of error near 3.4 s, corrected at 3.5 s).
//     ResistsAStrongNeighbor meets its 0.667 Hz in 72.3% and 72.4% of seeds (78% at a gate of 11 dB re the mean noise
//     power per bin, at 0.67 Hz). The plan expected only nearer neighbors' sidebands in the search range.
//   - The fill factor's quarters resolve a station's start to a quarter of the window: a start within a quarter is
//     averaged over it, and the factor read low. For a station present from 0.3, 0.4, 0.5, 0.6 and 0.7 of the window
//     on, the quarters read 1.90, 2.68, 20.04, 16.18 and 23.13 against the exact 2.53, 6.18, 20.04, 80.97 and 415.08
//     (derived): for these starts up to 18 times low. A start anywhere in the last quarter reads 1 075, so a later
//     start is read lower still: 3.0 times at 0.8 of the window (exact 3 229), 69 times at 0.9 (exact 74 258;
//     derived). Starts drawn anywhere in [0.5 s, 1 s) meet HoldsAKeyedCarrier in 99.8% of seeds.
//   - The variance model is right within about 25% for keyed carriers from 12 to 40 WPM below +10 dB S500, and
//     conservative above it (Measured); at slow speeds an error floor of about 0.13 Hz rms, independent of the
//     signal-to-noise ratio, is not in it.
//   - Two consecutive weak noise peaks have wide variances and agree easily: noise alone restarts the filter once per
//     91 min, and StaysPutOnNoise is met in 99.2% of seeds.
//   - The hiss estimate's own error moves the gate: an N0 2% low (its relative rms error in steady state) puts it at
//     8.9 dB re the true mean noise power per bin, and one 18% low (one standard deviation after one frame) at 8.1 dB
//     re it.
//   - Two carriers inside the search range give a peak at the larger (but for 2d's preference), and two within about
//     2 Hz look like one.
//   - A drift faster than about 1.5 Hz/s is rarely acquired: two windows 0.5 s apart then differ by more than the
//     agreement test's limit, about 0.5 to 0.9 Hz (Measured). The development set's tuning group, up to 2.7 Hz/s, is
//     beyond the physical range and not tuned for (the owner, 2026-10-20).
//
// Interpretations (readings chosen where the design notes and the plan left the detail open).
//   - sigma_d in the agreement test, "the filter's drift standard deviation", is tracker_drift_sd_hz_per_s, the drift's
//     standard deviation in the filter's model, not sqrt(P[1][1]): the test compares z2 with z1, not with z1 + d g, so
//     the drift's spread about 0 belongs in it, and the two may come from a new carrier, whose drift the old track's
//     covariance does not describe. Measured with sqrt(P[1][1]) in the design without the refinements (1 000 seeds,
//     at 0.67 Hz): FollowsDrift 97.0% against 99.6%, HoldsAKeyedCarrier 95.3% against 94.8%, ResistsAStrongNeighbor 46%
//     and 47% against 43% and 43%.
//   - "The next measured window" is the next window, 0.5 s later, and it must be measured. A measurement kept across
//     unmeasured windows would meet the next false peak whenever it came, with a drift term (sigma_d x gap)^2 that
//     grows without bound.
//   - The measurement that restarted the filter stays pending, so a weak track, unconfirmed after each restart,
//     restarts at every window that agrees with the one before.
//   - The hold begins where 3 sigma_f reaches tau, computed from the state, not at the next window, and the window's
//     shift (2b) follows the oscillator, so the search is centered on the held frequency.
//   - The first window's larger uncertainty is derived (the fill factor), not measured, and applies to every window:
//     a station's last window, and one that its keying fills unevenly, take theirs too.
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

    // The latest window, for tests and for the plan's measurement of the tracker's frequency error. NaN where a value
    // does not apply to the window.
    struct Window {
        double center_s = 0.0;                // s: the window's center, the time of its measurement
        Outcome outcome = Outcome::NotMeasured;
        bool confirmed = false;               // 3 sigma_f <= the tolerance at center_s (2a)
        double frequency_sd_hz = 0.0;         // Hz: sigma_f, the predicted frequency's standard deviation at center_s
        double noise_per_bin_fs2 = 0.0;       // FS^2: the noise power expected in one bin, N0 x 1.5 Hz (2d)
        double peak_fs2 = 0.0;                // FS^2: the peak bin's power (2d), in the same normalization
        double measured_hz = std::numeric_limits<double>::quiet_NaN();    // Hz: the measurement z (2e)
        double variance_hz2 = std::numeric_limits<double>::quiet_NaN();   // Hz^2: its variance R (2f)
        double acceptance_hz = std::numeric_limits<double>::quiet_NaN();  // Hz: min(3 sqrt(S), search), confirmed (2g)
        double agreement_hz = std::numeric_limits<double>::quiet_NaN();   // Hz: the limit against the pending one (2h)
        double fill_factor = std::numeric_limits<double>::quiet_NaN();    // F(p) / F(1), the line's fill factor (2f)
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
    // channel's center. The tracker_* fields of config are read once, and no other. Throws std::invalid_argument for a
    // rate_hz that is not positive and finite, a start_offset_hz that is not finite, a window outside 8 ... 8 192
    // samples (8 at least, so that each quarter of the presence profile holds a sample of positive weight, 2f), a hop
    // that rounds to no sample or is under half the window (windows two hops apart must share no sample, 2g), a search
    // range under one bin (0.18 Hz at 1 500 samples/s) or over half the sample rate, a gate that is not positive and
    // finite (dB re the mean noise power per bin), and a drift sd or drift noise that is negative or not finite.
    CarrierTracker(double rate_hz, const ViterbiConfig& config, double start_offset_hz);

    // The channel's next samples (complex, FS), the first at t0_s, s, with the hiss estimator's noise density around
    // the channel, FS^2/Hz (0: no estimate yet, and nothing is measured). Every window that the samples complete is
    // measured, in order (stage 2), with this density, so a call may make none, one or several. An empty block does
    // nothing. Throws std::invalid_argument for a t0_s that is not finite, a density that is negative or not finite
    // and, after the first block, a t0_s that is not where the previous block ended (the first block's t0_s plus the
    // samples so far over rate_hz, to within half a sample); the tracker is left as it was.
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
    // blocks: the window is cut into this many equal parts to read the line's presence profile (2f). Measured: 4 kept
    // HoldsAKeyedCarrier at 99.9% of 1 000 seeds and the same test with the station's start anywhere in a window at
    // 99.8%, against 99.7% and 99.5% for 2, 98.3% and 98.9% for 3, 99.5% and 99.8% for 8 (at 0.67 Hz, 2026-10-21).
    static constexpr std::size_t kProfileBlocks = 4;

    // Measures the window of window_samples_ samples that begins at sample index `first` (stage 2).
    void measure(std::uint64_t first, double noise_density_fs2_per_hz);

    // Hz^2: the variance of the state's line predicted dt_s seconds from the state's time (stage 3).
    double predicted_variance_hz2(double dt_s) const;

    // Sets hold_after_s_ from the state's covariance (stage 3).
    void set_hold();

    // The variance's fill factor F(p) / F(1) for a line whose amplitude per unit of window weight is levels[j] in
    // block j, and the line's centroid, s from the window's center (2f).
    double fill_factor(const double* levels, double& centroid_s) const;

    // The previous window's noise component, unknown: estimate 0, variance 1, independent of the state (2g).
    void forget_noise_state();

    // Fixed by the constructor.
    double rate_hz_ = 0.0;                   // samples/s
    std::size_t window_samples_ = 0;         // samples: tracker_window_s, rounded
    std::size_t hop_samples_ = 0;            // samples: tracker_hop_s, rounded
    std::size_t search_bins_ = 0;            // bins: K, the searched bins are k = -K ... K
    double search_hz_ = 0.0;                 // Hz: tracker_search_hz, the acceptance window's cap
    double gate_ratio_ = 0.0;                // the peak's power over the noise per bin: 10^(tracker_gate_db / 10)
    double drift_noise_ = 0.0;               // Hz^2/s^3: q
    double drift_sd_hz_per_s_ = 0.0;         // Hz/s: sigma_d, of the agreement test and the attribution (2f, 2h)
    double correlation_ = 0.0;               // the correlation of consecutive windows' errors at this hop (2g)
    double theta_ = 0.0;                     // the moving-average coefficient of the consecutive errors (2g)
    double sum_w_ = 0.0;                     // the window's sum
    double sum_w2_ = 0.0;                    // the sum of its squares
    double noise_moment1_ = 0.0;             // s: sum w^2 tau
    double noise_moment2_ = 0.0;             // s^2: sum w^2 tau^2
    std::size_t block_first_[kProfileBlocks + 1] = {};  // the blocks' first samples, and the window's length
    double block_moment0_[kProfileBlocks] = {};         // per block: sum w
    double block_moment1_[kProfileBlocks] = {};         // s: sum w tau
    double block_moment2_[kProfileBlocks] = {};         // s^2: sum w tau^2
    double block_w2_[kProfileBlocks] = {};              // sum w^2
    double full_profile_ = 1.0;              // s^-2: F(1), a filled window's
    std::vector<double> hann_;               // the window, window_samples_ values
    std::vector<std::complex<double>> spectrum_in_;   // the shifted, windowed, zero-padded window (8 192 points)
    std::vector<std::complex<double>> spectrum_out_;  // its transform

    // The filter: the state at state_time_s_, its covariance, the previous window's noise component, and the hold.
    double state_time_s_ = 0.0;      // s: t_s
    double offset_hz_ = 0.0;         // Hz: f^ at state_time_s_
    double drift_hz_per_s_ = 0.0;    // Hz/s: d
    double p_offset_ = 0.0;          // Hz^2: P[0][0]
    double p_cross_ = 0.0;           // Hz^2/s: P[0][1]
    double p_drift_ = 0.0;           // Hz^2/s^2: P[1][1]
    double noise_hat_ = 0.0;         // the estimate of a_(k-1), the previous window's white noise component (unit sd)
    double p_fa_ = 0.0;              // Hz: its covariance with the offset
    double p_da_ = 0.0;              // Hz/s: with the drift
    double p_aa_ = 1.0;              // its variance
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
