# Standard practice for detecting Morse on HF: a blind survey

**Status:** survey for the owner, 2026-10-22, asked for after the owner's question "Should we be doing amplitude
detection instead?"; it led to plan D16 (`docs/plans/2026-10-20-new-decoder-prototype-1.md`). Committed 2026-10-25
(owner: "yes (a)") as the evidence the design notes cite; the text is as written on 2026-10-22, except that
references to scratch files and to working files not in the repository are marked as such. Its figures for the
design then under discussion (80 Hz, 3 ms) are superseded by plan D16–D17.

**How claims are marked.**
- *[read]*: read in the primary source (quoted or checked against the page, the code or the manual).
- *[secondary]*: read in a secondary source (named), not in the primary.
- *[recollection]*: from memory, unchecked.
- *[derived]*: computed here from a stated formula; the scratch programs are named.

**Scratch files** (not in the repository): `ook_losses.py` (sections A to G of the derived tables) and `windows.py`
(sections H to J), in NumPy only (SciPy was not available, so the chi-square and Marcum integrals are exact series
coded by hand and cross-checked against the coherent/noncoherent table already in
`docs/research/proakis-ook-notes.md` §2.6, which they reproduce to 0.02 dB).

**Symbols and units.**
- T: the duration of one element or window, in s. A dit lasts T_dit = 1.2 s / WPM.
- E/N₀: key-on energy of one element (or of one window) over the one-sided noise density, dimensionless, quoted in
  dB re 1. From S₅₀₀ (dB SNR in 500 Hz, key-down carrier power), E/N₀ per dit = S₅₀₀ + 10·log₁₀(500 Hz × T_dit):
  S₅₀₀ + 20.8 dB at 5 WPM, + 14.8 dB at 20 WPM, + 10.0 dB at 60 WPM (derived, section G).
- "Element error": the probability of a wrong key-down/key-up decision on one element, equal priors, optimum
  threshold. It is the per-element yardstick a sequence decoder's character error is built from; the error rates
  that matter for a decoder working near its threshold are 10⁻² to 10⁻¹.
- A loss "in dB" is the ratio of the E/N₀ (or S₅₀₀) two detectors need for the same element error.
- f_D: the fading's frequency spread in Hz, defined as 2σ of the Gaussian Doppler spectrum (Watterson; ITU-R
  F.1487). The complex gain then correlates as ρ(τ) = exp(−(π f_D τ)²/2).
- Coherent / noncoherent: coherent detection uses the carrier's phase (known or estimated); noncoherent detection
  uses only the magnitude or energy of a filtered sum, so the phase drops out.

---

## 1. Summary

1. **Practice: every working Morse decoder found detects noncoherently.** Each takes the magnitude (envelope) of the
   tone behind a predetection filter about 30 to 100 Hz wide, that is, a coherent window of roughly 10 to 35 ms, and
   smooths the envelope over about half a dit or feeds it to a sequence model. CW Skimmer (50 Hz decoders), fldigi
   (|z| behind a 5/T_dit-wide filter), Bell 1977 (envelope behind a 64 to 100 Hz band-pass, 5 ms samples), VE3NEA's
   DeepCW (STFT magnitudes, 50 ms window, 29.4 Hz noise bandwidth) and the manta HSMM decoder (82 Hz channel,
   magnitude) all do this. **None tracks the carrier phase across marks.** The one coherent attempt found, Bell's 1975
   pre-detection I/Q Kalman filter, assumed a known carrier frequency, was tested on about 100 bits of a square wave,
   and was dropped by Bell himself in 1977 for an envelope detector. In 1977 Bell kept the predetection filter at
   100 Hz on purpose, to pass about 50 Hz of chirp.
2. **Practice: strength is tracked as an amplitude, never as a phase.** Two forms are used. Channel-level AGC and
   peak/floor trackers (fldigi: fast attack, slow decay; CW Skimmer; manta: sliding maximum and running minimum)
   normalize the envelope. Bell 1977 runs one scalar Kalman filter on the AGC-normalized amplitude per hypothesis,
   with small process noise inside a mark and a large one after a long space, so a new or recovered level is cheap
   to accept.
3. **Theory: coherent detection of on/off keying gains little and is hard to realize.**
   - In AWGN with the phase known, coherent detection beats the optimum noncoherent detector by 2.0, 1.3, 1.0 and
     0.7 dB at element errors of 10⁻¹, 3×10⁻², 10⁻² and 10⁻³.
   - In Rayleigh fading, with the complex gain known perfectly, it beats noncoherent detection with the amplitude
     known perfectly by 2.1 to 2.5 dB at 10⁻² to 10⁻¹ (derived, sections A and C).
   - These are upper bounds. The realizable part needs the phase to be predictable across the key-up gaps, which
     requires f_D below about 1 Hz, a residual frequency error well under 1/(2π × the gap), and no chirp or phase
     breaks.
   - The textbook optimum for a Gaussian fading gain, the estimator–correlator, reduces to a noncoherent energy
     detector over the coherence time when the energy per coherence time is low (derived, section 3.3). Practice
     differs from the optimum for robustness (chirp, drift, phase breaks, impulses), not for want of theory.
4. **Practice: what is a limitation.**
   - **Chirp** is absorbed by a predetection bandwidth wide enough to pass it (Bell). It is otherwise a limitation.
   - **Key clicks** are not modeled by any decoder. CW Skimmer's "Anti-Click" acts on its waterfall display.
   - **Impulses** are handled by a wideband noise blanker ahead of the narrow filter (CW Skimmer's, optional), by
     discarding marks shorter than a fraction of a dit (fldigi), or by nothing. Static crashes taken for faded marks
     were among Bell's field failures.
5. **This project.** Today's decoder already has the standard noncoherent statistic (ln I₀ on a coherent sum). It
   departs from practice in two ways, and the two match the fading diagnosis's causes:
   - its coherent window may grow to 390 ms, against 10 to 35 ms in practice and 75 ms coherence at f_D = 5 Hz;
   - its level is held constant within a window, and across gaps its spread is scaled by the story's current level
     rather than by the long-run (AGC) level.

   The fully coherent draft departs further. A complex-gain Kalman filter in every story is standard for PSK and QAM
   (per-survivor processing) but is used by no Morse decoder found. Its benefit over a corrected noncoherent design is
   bounded above by the 1 to 2.5 dB of point 3 and is not shown to be realizable.

   **The standard-practice design (section 5):**
   - keep the noncoherent evidence;
   - cap each coherent window at about 60 ms, which loses at most 0.31 dB of energy at f_D = 5 Hz;
   - track an amplitude per story whose spread across gaps and within marks is scaled by a long-run level from a
     decision-free channel-level estimate;
   - declare chirp beyond about ±10 Hz, key clicks and static crashes known limitations, except for a wideband blanker
     ahead of the channelizer if the owner wants one.

   Its cost against an ideal fully coherent receiver on a steady signal is 1.0 to 2.0 dB at speeds of 20 WPM and
   up (the noncoherent penalty itself), plus 1.4 to 1.9 dB at 5 WPM from splitting a 240 ms dit into four windows
   (derived, section 5). In fading the bound is 2.1 to 2.5 dB.

---

## 2. Practice: what working decoders do

### 2.1 CW Skimmer (VE3NEA, Afreet Software) and the Reverse Beacon Network

- **Decoder.** The only published description: "a very sensitive CW decoding algorithm based on the methods of
  Bayesian statistics" [read: CW Skimmer manual, 2024 PDF, p. 1]. The manual says nothing more about the detector.
- **Channel width.** VE3NEA, quoted verbatim by N4ZR on the RBN blog (2014-03-05): the signal is extracted from the I/Q
  stream "through a 50 Hz filter", and the signal strength is estimated in that 50 Hz passband [read]. The RBN's
  sender guide says each decoder is 50 Hz wide [read]. A 50 Hz filter corresponds to a coherent window of about
  1/(50 Hz) = 20 ms [derived]. That equals a dit at 60 WPM and a third of a dit at 20 WPM.
- **Key decisions and strength.** From the same VE3NEA quotation:
  - "First the key-on/key-off decision is made for every sample in the signal, as part of CW detection." Key-off and
    transition samples are discarded for the SNR.
  - The strength is computed so that "signal strength variations due to QSB are expected and taken into account"
    under a Rayleigh model.
  - The noise density comes from the flat part of the whole receiver's spectrum and is scaled to 500 Hz [read].

  So Skimmer models fading as **amplitude statistics** (Rayleigh) [read]. That its detector is noncoherent is an
  inference from the 50 Hz channel and the absence of any phase-related setting or statement [inference].
- **Private advice from VE3NEA to AG1LE (2013):** carry probabilities, not decisions, "all the way to the word
  recognition unit" [secondary: AG1LE blog, relayed in `docs/research/decoder-survey.md`].
- **Impulses, clicks, AGC.** The manual's DSP processor has a noise blanker, "efficient against impulsive noise, such
  as static crashes and powerline noise", which "works best when no strong signals are present in the passband" and
  should stay off unless impulsive noise is present [read, manual "DSP Processor"]. Its AGC "tracks both the upper and
  lower ends of signal's dynamic range" and does not "breathe" [read]. "Anti-Click … removes key clicks from the
  waterfall display" [read]. These sit in the manual's audio-output DSP section. Whether the decoders see the blanker's
  output is not stated [gap].
- **Frequency.** No statement on AFC, chirp or drift was found. RBN asks senders not to "change frequency enough to
  confuse the software" and to send clean, constant-speed code [read: RBN sender guide]. G4ILO found Skimmer "fussy"
  about tuning and MRP40's AFC better at following drifting stations [secondary: G4ILO 2012, as recorded in
  `docs/research/g4ilo-2012-notes.md`].
- **RBN.** RBN CW nodes run CW Skimmer / Skimmer Server, so the RBN's practice is Skimmer's [read: RBN sender guide].
- **Measured behavior in fading.** VE3NEA's own benchmark: Skimmer's character error stays near 40% at f_D = 3 Hz
  even at S₅₀₀ = +27.8 dB, while DeepCW reaches 1% there [secondary: VE3NEA DeepCW `accuracy_charts.ipynb`, as
  tabulated in `docs/research/decoder-survey.md`]. Fast fading defeats even the market-leading decoder. The cause is
  not documented.

### 2.2 fldigi's CW decoder (W1HKJ; current `master`)

All from `src/cw_rtty/cw.cxx` and `src/include/cw.h` [read, code].
- **Mixing and filter.** A complex oscillator at the tone, then a complex FFT low-pass filter. In "matched filter"
  mode its bandwidth setting is `5.0 * CWspeed / 1.2` Hz = 5/T_dit: 83 Hz at 20 WPM, against 1/T_dit = 16.7 Hz for a
  true matched filter. Otherwise the user's bandwidth applies.
- **Detector.** `FFTvalue = abs(zp[i])`, the **magnitude** of the complex filter output: noncoherent envelope. It is
  decimated by 16 (8 000 → 500 samples/s), then a moving average of length `symbollen/(2·DEC_RATIO)`, half a dit.
- **Strength.** Three running averages of the envelope, each with an attack weight and a decay weight (defaults 200
  and 1 000 samples; at 500 samples/s, about 0.4 s and 2 s if `decayavg` is the usual
  avg + (x − avg)/w [recollection for decayavg's form]):
  - a signal average;
  - a noise floor that falls fast and rises slowly;
  - a peak (AGC) that rises fast and falls slowly.

  The envelope is divided by the peak.
- **Thresholds.** Hysteresis between key-down at `norm_sig − 0.2·diff` and key-up at `norm_noise + 0.7·diff`, where
  diff = norm_sig − norm_noise. Both are fractions of the way from the normalized noise floor to the normalized signal.
- **Impulses.** A mark shorter than a set threshold is discarded as a spike: half a received dot in
  `sync_parameters`, a quarter of two dots elsewhere.
- **Frequency.** No AFC in the decoder; the tone frequency is fixed from the user's choice.
- **Chirp and clicks.** Nothing beyond the filter.

### 2.3 Bell (1977) and Bell (1975), detection and level tracking only

Bell 1977, pp. 109–116 [read, page images of the local scan, pp. 109–115; the listing's values from the project's
transcription, `docs/research/bell-1977-notes.md`, secondary]:
- **The detector.** "Since the noise process observed at the output of the detector is the result of envelope
  detection of a narrowband gaussian process, the resulting process is neither zero-mean, gaussian, nor white"
  (p. 110). The chain is a band-pass filter, an envelope detector and sampling every 5 ms.
  - The thesis text says 100 Hz.
  - The listing has a 64.4 Hz −3 dB band (78.5 Hz noise bandwidth) and |x| followed by a 31.1 Hz low-pass
    [secondary].
- **Why the filter is wide.** The optimum two-resonator bandwidth for a 24 ms mark at 50 WPM is 0.613/0.024 s = 25 Hz,
  0.56 dB from the matched filter (citing Skolnik). Bell continues: "it is clearly too narrow to accept signals which
  have a significant carrier instability due to chirp or drift. Since it is not uncommon to observe carriers with a
  chirp on the order of 50 or so Hz, the bandwidth required is on the order of 100 Hz" (pp. 111–112). He prices the
  100 Hz filter at 6 dB at 50 WPM and 10 dB at 20 WPM, and calls for adaptive filtering [read].
  - These prices compare predetection SNRs and ignore what post-detection smoothing recovers. Section 3.2's table
    gives the actual post-detection integration loss: at 20 WPM, M = 100 Hz × 60 ms = 6 windows cost 1.9 to
    2.5 dB, not 10 dB [derived].
- **Level.** One scalar Kalman filter per path on the **amplitude**; "the detected signal has been gain-leveled by an
  AGC, so that the average detected output power is normalized" (p. 110) [read].
  - Within a mark the amplitude follows a first-order Gauss–Markov model. The text gives γ = exp(−2π·1 Hz·5 ms) = 0.97
    per 5 ms for a 1 Hz fade rate, with Var = 10⁻⁴ (normalized amplitude)² per step. The latter is sized from "during a
    1 sec fade period the amplitude can change by about 3 dB for a typical receiver AGC circuit" (p. 115) [read].
  - Entering a mark after a long space, the process noise is much larger: 0.25 in the text, 0.160 in the listing, in
    (normalized amplitude)² per step. This lets "a new transmitter power appear".
  - During a space the estimate decays slowly [read p. 116; listing values secondary].
- **Noise level.** Estimated from a running minimum of the envelope, with R = 2μ_n² taken from Davenport (p. 114)
  [read]. The listing differs; see the notes [secondary].
- **Field failures.** Static bursts were accepted as faded marks, and weak interferers dominated in the wanted
  station's spaces when the AGC raised the gain [secondary: `bell-1977-notes.md` §6.5, pp. 142–143].

Bell 1975 (NPS thesis, *Processing of the Manual Morse Signal Using Optimal Linear Filtering, Smoothing and
Decoding*) [read, Internet Archive text]:
- **Post-detection.** A Kalman smoother on a square-law detector, tested in 2 kHz with and without a 100 Hz pre-filter.
  With the pre-filter, about 10% letter error at −7 dB SNR in 2 kHz.
- **Pre-detection.** A Kalman filter on an I/Q state, "of known frequency and unknown amplitude and phase", with "the
  receiver … tuned to the signal carrier frequency". Demodulation is by "squaring and averaging" the state estimate,
  so the final decision is still on energy. It gave about 1% bit error at −14 dB in 2 kHz, on roughly 100 bits of a
  square-wave test signal, with no fading, drift or chirp.
- **Bell 1977 dropped it** for envelope detection; the 1977 pages read here state no reason beyond the chirp and
  drift argument for the wide filter [read].

### 2.4 VE3NEA's DeepCW (2024)

- **Input.** An STFT of 512 points at 6 kHz, Kaiser window β = 6 over 300 samples (50 ms), hop 64 samples, then
  **magnitudes** [secondary: `docs/research/deepcw-generator-notes.md` §3, from `training_settings.py`; the
  repository's front page names only the notebooks, read]. The window's noise bandwidth is 29.4 Hz [derived].
- **Normalization.** No AGC. Each 512-frame spectrogram is divided by 4× its standard deviation.
- **Frequency.** Training draws a pitch error uniform in ±30 Hz, so frequency tolerance is learned.
- **Fading.** Training uses flat Rayleigh at 0.1 to 3 Hz, so fading is learned. There are no chirp, clicks, QRN or
  drift [secondary, same notes].
- **Detection.** Noncoherent (magnitude) on about 30 Hz bins, with the network doing the rest.
- **Result.** It is the decoder that survives f_D = 3 Hz in VE3NEA's benchmark (section 2.1).

### 2.5 MRP40 and Morse Expert

- **MRP40** (Norbert Pieper): no technical description found. One user report credits its AFC with tracking drifting
  stations [secondary: G4ILO 2012]. Its detector is undocumented.
- **Morse Expert** (VE3NEA's Android decoder): its author says it uses CW Skimmer's algorithms [secondary: cq.sk
  review]. It is undocumented beyond that.

### 2.6 Other published Morse decoders

- **Gold 1959, MAUDE (MIT Lincoln Laboratory).** Rules and adaptive thresholds on mark and space durations, from a
  clean keying signal. No receiver or SNR appears in the paper [secondary: `gold-1959-notes.md`].
- **Blair 1959 (J. ACM)** and **Guenther 1973 (AFIT).** Duration classification after a hard on/off decision [Guenther
  secondary: transcription cited in the decoder survey; Blair recollection].
- **AG1LE's port of Bell (2013–14).** Envelope input, per-path amplitude Kalman. He reported an unexplained 3%
  error floor and a noise estimator that did not track [secondary: decoder survey].
- **manta `hsmm` (2026).** Magnitude of an 82.2 Hz-noise-bandwidth channel at 375 samples/s, smoothed over 8 ms,
  normalized between a running-minimum noise level and a sliding maximum over ±4 dits [secondary: `manta-notes.md`].
- **A 2002 Korean paper (KICS).** Spectral peaks and an LMS-adaptive tone/space decision "under severe fading"
  [secondary: abstract only].
- **Arduino-class decoders** (for example OZ1JHM's). The Goertzel magnitude over a block, a noncoherent single-bin
  DFT [recollection].
- **Adjacent HF practice, WSPR (WSJT-X `wsprd.c`).** The decoder is noncoherent. Its optional "noncoherent block
  detection" over 1, 2, 3, 6 or 9 symbols notes that "longer block lengths require longer channel coherence time", and
  `-B` falls back to single-symbol noncoherent demodulation [read, code comments in a GitHub mirror of WSJT-X].
  Amateur weak-signal practice on HF is noncoherent, with multi-symbol coherence used only where the channel allows
  and tried alongside the single-symbol detector.

### 2.7 Practice in one table

| Decoder | Detected quantity | Predetection width (coherent window) | Post-detection | Strength through fading | Frequency | Chirp, clicks, impulses | Mark |
|---|---|---|---|---|---|---|---|
| CW Skimmer / RBN | key-on/off per sample; SNR on key-on samples | 50 Hz (≈ 20 ms) | Bayesian, undocumented | Rayleigh amplitude model for SNR; AGC on audio | not stated | blanker (optional, audio DSP); anti-click is display | read / inference |
| fldigi | \|z\| | 5/T_dit (83 Hz at 20 WPM) or user's | moving average T_dit/2, 500 samples/s | peak / floor / average trackers, attack and decay | fixed tone | spike rejection below T_dit/2 | read (code) |
| Bell 1977 | envelope | 100 Hz text; 64.4 Hz listing (≈ 10–15 ms) | 31 Hz low-pass, 5 ms samples, tree search | AGC plus scalar amplitude Kalman per path, Q larger after long spaces | fixed | filter wide for 50 Hz chirp; static a failure mode | read |
| Bell 1975 | squared I/Q Kalman estimate | 100 Hz + Kalman, known carrier | Viterbi on smoothed output | none (AWGN only) | known | none | read |
| DeepCW | STFT magnitude | 29.4 Hz (50 ms window) | LSTM + CTC | learned (Rayleigh 0.1–3 Hz) | learned (±30 Hz) | none | secondary |
| manta hsmm | magnitude | 82.2 Hz | 8 ms smoothing; HSMM | min/max normalization | channelizer | none | secondary |
| WSPR (adjacent) | noncoherent symbol energies | per-tone | Fano/OSD | — | drift search | block detection only if coherence allows | read |

---

## 3. Theory

### 3.1 OOK with unknown phase: coherent against noncoherent (AWGN)

- **Optimum noncoherent detector.** With the phase uniform, the MAP rule depends on the received signal only through
  the envelope of the matched-filter output (Proakis and Salehi, 5th ed., §4.5-1, eqs. 4.5-21 to 4.5-24, Fig. 4.5-1,
  pp. 212–214 [secondary: `proakis-ook-notes.md`, which checked the pages]).
- **Error probabilities.** With x the envelope normalized by the noise σ, the OOK log-likelihood ratio is
  −a²/2 + ln I₀(a·x), with a = √(2E/N₀). The error probability is P = ½e^(−b²/2) + ½[1 − Q₁(a, b)] at the optimum
  threshold b. Coherent OOK gives P = Q(√(E/(2N₀))) (Proakis eq. 4.2-37, d² = E).

Required key-on E/N₀ per element [derived, `ook_losses.py` section A]:

| Element error | Coherent (dB re 1) | Noncoherent (dB re 1) | Noncoherent penalty |
|---|---|---|---|
| 10⁻¹ | 5.17 | 7.17 | 2.01 dB |
| 3×10⁻² | 8.50 | 9.82 | 1.32 dB |
| 10⁻² | 10.34 | 11.37 | 1.02 dB |
| 10⁻³ | 12.81 | 13.52 | 0.71 dB |

The penalty is largest exactly where a sequence decoder near its threshold works, but it never exceeds about 2 dB.
Proakis states the same shrinkage, under 0.8 dB below about 10⁻⁴, for noncoherent FSK and DPSK (pp. 218, 224)
[secondary, same notes].

### 3.2 Windows shorter than an element: the noncoherent integration loss

Split an element of total key-on energy E into M equal coherent windows, detect each noncoherently, and add the
energies (square-law post-detection integration). Required total E/N₀, exact noncentral chi-square with 2M degrees
of freedom, equal priors, optimum threshold [derived, `ook_losses.py` section B]:

| M | Loss re M = 1, at 10⁻¹ | Loss re M = 1, at 10⁻² | Total re coherent, at 10⁻² |
|---|---|---|---|
| 1 | 0 | 0 | 1.02 dB |
| 2 | 0.91 dB | 0.64 dB | 1.67 dB |
| 3 | 1.47 dB | 1.06 dB | 2.09 dB |
| 4 | 1.88 dB | 1.38 dB | 2.41 dB |
| 6 | 2.49 dB | 1.86 dB | 2.89 dB |
| 8 | 2.94 dB | 2.23 dB | 3.25 dB |
| 12 | 3.59 dB | 2.77 dB | 3.80 dB |
| 30 | 5.17 dB | 4.14 dB | 5.16 dB |
| 60 | 6.45 dB | 5.28 dB | 6.31 dB |

- **Check.** The design notes' own Monte Carlo (topic 4 item 9: 0.5 and 1.0 dB for 2 and 3 blocks at 1% per mark)
  agrees within 0.15 dB.
- **Bell's 100 Hz.** At 20 WPM, 100 Hz × 60 ms gives M ≈ 6: about 1.9 to 2.5 dB, not 10 dB.
- **Per-piece detection.** Taking the magnitude of every 2 ms piece of a 60 ms dit (M = 30) would cost 4.1 to 5.2 dB.
  That is why any noncoherent design for this decoder must sum pieces coherently into windows before taking
  magnitudes, as today's decoder does.
- **Reference forms.** The square-law sum is the standard noncoherent integrator (Proakis §4.5; radar texts call this
  "noncoherent integration loss", e.g. Skolnik, whom Bell cites) [recollection for the radar texts]. With the
  amplitude known, the exact per-window statistic Σ ln I₀(a·x_m) is slightly better than the square-law sum at high
  per-window SNR and the same at low [derived from the LLR's small-argument form, ln I₀(z) ≈ z²/4].

### 3.3 OOK over Rayleigh flat fading: the optimum receiver against the practical one

**Bounds with perfect channel knowledge.** For binary FSK in slow Rayleigh fading, coherent detection with a noiseless
phase estimate gives P ≈ 1/(2γ̄) and noncoherent P = 1/(2 + γ̄) ≈ 1/γ̄: 3 dB apart asymptotically. Proakis calls
the coherent figures "the best achievable performance" because they assume a noiseless phase estimate (Proakis and
Salehi, §13.3, eqs. 13.3-7 to 13.3-13, pp. 846–848) [read, local PDF]. The same calculation for OOK, the gain
constant over the element [derived, `ook_losses.py` section C]:

| Element error | Coherent, complex gain known (dB re 1, mean key-on E/N₀) | Noncoherent, amplitude known | Gap | Noncoherent, only the mean power known |
|---|---|---|---|---|
| 10⁻¹ | 8.52 | 11.04 | 2.52 dB | 12.41 |
| 3×10⁻² | 14.82 | 17.03 | 2.21 dB | 19.48 |
| 10⁻² | 19.87 | 22.00 | 2.13 dB | 25.29 |

- **The fading's own cost.** About 10.6 dB at 10⁻², the noncoherent column against AWGN's 11.37 dB.
- **Phase knowledge** is worth at most 2.1 to 2.5 dB.
- **Amplitude knowledge** (tracking the level instead of knowing only its long-run mean) is worth 1.4 to 3.3 dB.
- **Amplitude first.** Amplitude tracking comes first in value, and it needs no phase.

**The optimum receiver.** When the fading gain is a Gaussian process with a known spectrum, the likelihood ratio is
quadratic in the received samples: l = xᴴ[V⁻¹I − (R_s + VI)⁻¹]x. This is the estimator–correlator: an MMSE (Kalman or
Wiener) estimate of the faded signal, correlated with the input (Price 1956; Kailath 1960; Van Trees, *Detection,
Estimation, and Modulation Theory*, Part III, 1971, chapters 2 and 11 on Doppler-spread channels [recollection,
chapter numbers unchecked]).
- **The low-SNR limit [derived].** When the signal energy per coherence time is small against N₀ (R_s ≪ VI), the
  kernel tends to R_s/V². The receiver becomes a quadratic form weighted by the gain's correlation: a noncoherent
  energy detector whose coherent window is the fading's correlation time (a "filter–squarer–integrator").
  - Van Trees names this regime "low-energy-coherence" [recollection].
  - So the textbook optimum is itself noncoherent when coherence carries little energy.
- **When it is not low.** Energy per half-correlation time T_c = 0.375/f_D, at a key-down fraction of 0.44
  [derived, section F]:

| S₅₀₀ | f_D = 0.1 Hz (T_c 3.7 s) | f_D = 1 Hz (T_c 375 ms) | f_D = 5 Hz (T_c 75 ms) |
|---|---|---|---|
| −5 dB | 24.2 dB re 1 | 14.2 dB re 1 | 7.2 dB re 1 |
| 0 dB | 29.2 | 19.2 | 12.2 |
| +5 dB | 34.2 | 24.2 | 17.2 |

  At slow fading the energy per coherence time is high, so a phase estimate is in principle good enough to recover
  much of the 1 to 2.5 dB.
- **What blocks it in practice.** The phase must be *predictable across the key-up gaps*, because the carrier is off
  during them.
  - At 20 WPM the gain's correlation across a 3-dit gap (180 ms) is 0.85 at f_D = 1 Hz, 0.53 at 2 Hz and 0.02 at
    5 Hz. Across a 7-dit gap (420 ms) it is 0.42 at 1 Hz [derived, section E].
  - A residual frequency error δf turns the phase by 360°·δf·τ: 65° over a 180 ms gap at δf = 1 Hz, 151° over 420 ms
    [derived].
  - A transmitter that restarts its phase at key-down, or chirps, breaks the prediction outright.
- **Literature on sequence detection.** Noncoherent sequence detection (Colavolpe and Raheli 1999) and multiple-symbol
  differential detection (Divsalar and Simon 1990) show that coherence across a few symbols recovers most of the
  noncoherent gap *when the phase is stable over those symbols* [recollection]. Per-survivor processing (Raheli,
  Polydoros and Tzou 1995) runs a channel estimator, often a Kalman filter, per survivor [recollection]. These are
  standard for PSK and QAM, where phase carries the data. For OOK, phase carries no data and its only use is a
  better amplitude estimate.

### 3.4 Frequency uncertainty and chirp against coherent windows

- **Frequency offset.** A coherent sum over T of a carrier δf off center keeps |sinc(δf·T)|² of its power (derived from
  Proakis eq. 4.5-28 [secondary]). Losses in dB re a centered carrier [derived, section D]:

| Window | δf = 0.5 Hz | 1 Hz | 2 Hz | 5 Hz |
|---|---|---|---|---|
| 20 ms | 0.00 | 0.01 | 0.02 | 0.14 |
| 60 ms (20 WPM dit) | 0.01 | 0.05 | 0.21 | 1.33 |
| 180 ms (20 WPM dash) | 0.12 | 0.47 | 1.94 | 19.2 |
| 240 ms (5 WPM dit) | 0.21 | 0.84 | 3.58 | 16.1 |
| 720 ms (5 WPM dash) | 1.94 | 9.35 | 13.3 | 21.5 |

- **Fading.** A coherent sum over T keeps (2/T²)∫₀^T (T − τ)ρ(τ)dτ of the faded energy. In dB re the faded energy
  [derived, section H]:

| Window | f_D = 1 Hz | 2 Hz | 5 Hz |
|---|---|---|---|
| 20 ms | 0.00 | 0.01 | 0.04 |
| 40 ms | 0.01 | 0.02 | 0.14 |
| 60 ms | 0.01 | 0.05 | 0.31 |
| 78 ms | 0.02 | 0.09 | 0.50 |
| 180 ms | 0.11 | 0.43 | 1.96 |
| 390 ms | 0.50 | 1.60 | 4.49 |

- **Chirp.** Model a key-down frequency offset δf₀·e^(−t/τ_c). The loss of a mark's first coherent window, dB re no
  chirp [derived, section I]:

| δf₀, τ_c | Total phase | 10 ms | 20 ms | 40 ms | 60 ms |
|---|---|---|---|---|---|
| 10 Hz, 20 ms | 72° | 0.09 | 0.23 | 0.40 | 0.45 |
| 25 Hz, 20 ms | 180° | 0.56 | 1.45 | 2.59 | 2.80 |
| 50 Hz, 5 ms | 90° | 0.63 | 0.69 | 0.48 | 0.35 |
| 50 Hz, 20 ms | 360° | 2.32 | 6.40 | 9.68 | 7.58 |

  - A short window passes chirp; that is Bell's wide filter.
  - For a coherent tracker the chirp is worse than for a window. The phase excursion persists after the chirp ends, so
    the mark's whole remaining sum is rotated relative to any phase predicted from the previous mark. And a tracker
    whose Doppler model allows a few hertz cannot follow a 25 to 50 Hz excursion within the first 20 to 40 ms
    [inference from the same model].

### 3.5 Impulsive noise in noncoherent detectors

- **The energy detector is fragile.** It is the optimum for Gaussian noise, and a single large impulse dominates its
  sum. Under Middleton Class A noise the optimum incoherent detector applies a limiting (clipping) nonlinearity before
  the quadratic form (Spaulding and Middleton, *IEEE Trans. Commun.* 25(9):910–934, 1977, Parts I and II
  [citation read in DBLP; content recollection]).
- **Practice blanks before the narrow filter.** A narrow filter stretches an impulse to about 1/B, which makes it
  harder to recognize and to remove. That is why receivers blank in the wide band, before narrow filtering, and why
  blankers work worse with strong signals in the passband (CW Skimmer's manual says the same) [recollection for the
  general practice; read for Skimmer].
- **Key clicks** are the transmitter's own broadband sidebands at each keying edge. In the keying station's own
  narrow channel they are mostly outside the passband. In a neighbor's channel they arrive as short pulses at the
  clicking station's edges, which to that neighbor's detector are impulses correlated with another station's keying
  [derived from the definition].

### 3.6 What theory calls optimal, and why practice differs

| Choice | Theory's optimum | Practice | Why practice differs |
|---|---|---|---|
| Phase | estimator–correlator: coherent prediction of the complex gain | noncoherent magnitude | gain ≤ 1–2.5 dB at best; needs phase predictable across gaps; chirp, drift and phase breaks defeat it; cost |
| Window | matched to the element (1/T) | fixed 30–100 Hz (10–35 ms) | one filter for all speeds; chirp and drift tolerance; fading coherence |
| Strength | Kalman on the gain from the Doppler spectrum | AGC normalization plus amplitude Kalman (Bell) or trackers | robust; no phase needed; the amplitude carries the OOK information |
| Likelihood | exact Rician / Rayleigh, or the quadratic form | Gaussian on the envelope (Bell), thresholds (fldigi), learned (DeepCW) | simplicity; Bell found the loss "moderate" above S₁₀₀ ≈ 4 dB |
| Impulses | locally optimum nonlinearity (clipper) before the detector | wideband blanker; minimum-duration rejection; nothing | simplicity; blanker harms strong-signal bands |

---

## 4. The comparison with this project

### 4.1 Today's decoder (`docs/signal-processing.md` §8d)

| Item | Today | Standard practice | Departs? |
|---|---|---|---|
| Detected quantity | per story, a coherent sum S of 2 ms pieces over a block; Λ = −b² + ln I₀(2bz) | noncoherent magnitude of a coherent window; Rician/Rayleigh LLR is the textbook form | **No.** This is the textbook noncoherent statistic, better than Bell's Gaussian approximation. |
| Coherent window | the mark up to B = 195 pieces = 390 ms (from f_D = 1 Hz, 0.5 dB) | 10–35 ms in practice; ≤ coherence time at the worst spread in theory (75 ms at 5 Hz) | **Yes.** 390 ms keeps the energy only below about 1 Hz (4.49 dB lost at 5 Hz, section 3.4), and needs δf ≤ 0.667 Hz. |
| Level within a window | one constant amplitude (and phase) per block | Bell: amplitude Gauss–Markov per 5 ms sample, inside marks too | **Yes.** This is the diagnosis's cause 2. |
| Level across gaps | P(t) = ρ²P + (sμ)²(1 − ρ²): the spread scales with the story's *current* level μ | Bell: process noise in AGC-normalized units, i.e. scaled to the *long-run* level, larger after long spaces | **Yes.** This is the diagnosis's cause 1. |
| Long-run level | none: a newborn's level is flat over −15 to +85 dB S₅₀₀; then per story | AGC / peak tracker at channel level (fldigi, Skimmer, Bell's assumption, manta) | **Yes, by design** (topic 3's no-channel-decision rule). An AGC needs no key decisions, so the rule does not forbid it. |
| Frequency | channel tracker, decision-free, trusted within 0.667 Hz | AFC is common (MRP40), absent in fldigi and Bell; wide windows make it less critical | Standard in form; its tolerance is driven by the 390 ms window. |
| Chirp, clicks, impulses | none, beyond the systematic term's 2% floor | widen the window for chirp; blanker; spike rejection; otherwise limitations | Consistent with practice. |

### 4.2 The design notes, topic 4 items 8 and 9

- **Item 8 (the frequency reference) agrees with practice:** a decision-free tracker ahead of the stories. Its
  0.667 Hz tolerance is a consequence of item 9's long windows, not a need of the detector. With 60 ms windows,
  2 Hz costs 0.21 dB (section 3.4).
- **Item 9 (blocks) states the standard correctly.** "Sum coherently only over blocks shorter than the fading's
  coherence time …, and combine the blocks noncoherently; track the level block by block with a Gauss–Markov model."
- **Where item 9 departs is the value.** It chose one spread "by measurement" with a placeholder of 1 Hz, which gives
  390 ms. Practice sizes the window for the worst case it must survive, chirp included (Bell: 100 Hz for 50 Hz of
  chirp), and pays the noncoherent integration loss on slow, steady signals. Item 9's own numbers (about 2 dB on
  steady slow signals against up to about 7 dB on fast-fading slow ones) favor the short window once its loss is
  confined to speeds under about 16 WPM.
- **Item 9 calls the estimator–correlator "a refinement" (Kailath).** That is correct as theory and uncorroborated as
  Morse practice.

### 4.3 The earlier check, P1.1 to P1.8

| Item | The check's answer | Against practice |
|---|---|---|
| P1.1 | noncoherent; costs 1.0 dB at 1% | Agrees with practice and with section 3.1. |
| P1.2 | matched filter 1/T, following the speed hypothesis | Theory's answer. Practice uses a fixed 30–100 Hz (fldigi 5/T). A per-story matched window is a legitimate refinement, valid only below the coherence and chirp limits. |
| P1.3 | estimate and track the frequency | Agrees with practice where AFC exists; practice widens the filter instead when chirp is expected (Bell). |
| P1.4 | exact Rician/Rayleigh; **estimator–correlator under fading**, "a causal (Kalman) predictor of the complex gain" | The first half agrees. **The second half is theory's optimum, not practice.** No Morse decoder found does it, and the check does not note that it reduces to noncoherent detection at low energy per coherence time. This is where the coherent proposal enters. |
| P1.5 | Gauss–Markov gain from the Doppler spread, Kalman-tracked; noncoherent combining when fading is fast | Agrees if "gain" is read as amplitude (Bell). The wording admits a complex gain, which practice does not use. |
| P1.6 | whiten or decimate correlated noise | Agrees (Bell: sample at ≥ 1/(2B); today's decoder uses the long-run rate). |
| P1.7 | soft decisions | Agrees (VE3NEA's advice; Bell). |
| P1.8 | frame step ≤ T/4 | Agrees (Bell 5 ms; fldigi 2 ms). |

### 4.4 The fully coherent draft (a working draft not in the repository, sections 0 and 7; put on hold by plan D16)

| Recommendation | Against practice |
|---|---|
| 1. Kalman filter on each story's complex gain, per-piece estimator–correlator | **Departs.** Textbook-optimal for a Gaussian gain of known spectrum and per-survivor-standard for PSK/QAM; used by no Morse decoder found. Bell's only coherent attempt (1975) assumed a known carrier and was dropped. For OOK its benefit over noncoherent detection with a tracked amplitude is bounded by 1.0–2.0 dB (AWGN) and 2.1–2.5 dB (Rayleigh, perfect knowledge). By the draft's own Monte Carlo (§2.3) it matches a whole-mark noncoherent sum on steady marks, so it does not claim that bound. |
| 2. Second-order model | A detail of 1; no practice. Bell used first order on the amplitude. |
| 3. Three fading rates per story | Multiple models are standard in tracking (Blom and Bar-Shalom); not in Morse practice. |
| 4. 17-point mixture over long-run power per story | **Departs.** Practice gets the long-run level from a channel-level AGC or peak tracker. |
| 5. No per-story frequency | Agrees with practice. |
| 6. Systematic term per piece | Project-specific. Practice saturates instead: AGC normalization (fldigi), and Bell's exponent clamp. |
| 7. Clicks: an explicit click hypothesis | **Beyond practice.** The draft finds its own design worse on clicks than today's (81 to 2 560 nats against 46 to 210). |
| 8. Merging unchanged | — |
| 9. Slow speeds in fast fading by the same filter; chirp unmodeled | Chirp unmodeled agrees with practice. **But** a coherent model is the design most hurt by chirp (section 3.4), and practice's answer to chirp, a wide window, is not available to it. |
| 3g. Phase across key-up: both assumptions per story | The question exists only because the design is coherent. A noncoherent window makes it moot. |
| Cost 2.1–3.9× today | Practice is cheaper. |

The owner's question "are we crazy to do phase-sensitive detection for Morse?" has this answer:
- standard practice says yes, for a Morse skimmer;
- theory says the prize for doing it right is at most about 1 to 2.5 dB;
- the risks are in exactly the impairments the owner names: chirp, phase breaks, clicks and drift, which practice
  handles by not depending on phase.

---

## 5. The standard-practice design for this decoder (one page)

What follows keeps today's search, timing, text and systematic term, and changes only the detection and the level.
Each cost is against an ideal fully coherent receiver (complex gain known) unless stated.

1. **What is detected:** noncoherent. Per story, the pieces of the current mark are summed coherently into
   **windows**, and each window is scored with today's Λ = −b² + ln I₀(2bz), integrated over the story's level prior
   (unchanged). Windows of one mark add noncoherently (unchanged).
   - **Cost on a steady carrier:** the noncoherent penalty, 1.02 dB at 10⁻² and 2.01 dB at 10⁻¹ element error
     (derived, section 3.1).
   - **Cost in Rayleigh fading:** at most 2.13 to 2.52 dB (section 3.3, perfect-knowledge bound), of which the
     realizable part is unknown and, by section 3.3, small at f_D ≥ 2 Hz or with chirp, drift or phase breaks.
2. **Over what window:** the story's mark so far, capped at **T_w = 60 ms** (30 pieces). Windows restart at the cap.
   The first window keeps today's rise weighting.
   - **Why 60 ms** (heuristic, from derived losses):
     - it is the longest window losing ≤ 0.31 dB of energy at f_D = 5 Hz (0.05 dB at 2 Hz);
     - ≤ 0.21 dB at a 2 Hz residual frequency error;
     - and ≤ 0.45 dB on the first window of a 10 Hz/20 ms chirp.
   - **Why not shorter:** practice's 20 to 35 ms would tolerate 25 Hz chirps better (1.45 dB at 20 ms against 2.80 dB
     at 60 ms) but doubles M below 25 WPM.
   - **Cost on a steady carrier, re one window per element.** Dits are never split from 20 WPM up (T_dit ≤ 60 ms):
     0 dB. At 12 WPM a 100 ms dit is 2 windows: 0.64 to 0.91 dB. At 5 WPM a 240 ms dit is 4 windows: 1.38 to
     1.88 dB (section 3.2, element errors 10⁻² to 10⁻¹). Dashes split more but carry three times the energy, and the
     dit decides.
   - **Against the draft's estimator–correlator on steady marks:** its own Monte Carlo loses 0.00 dB on a 25 WPM dot,
     where this design also loses 0. On a 5.2 WPM dash it loses 0.24 to 1.56 dB, where this design loses about 2.8 dB
     (12 windows, section 3.2). So the draft would buy about 1 to 2.5 dB on slow, steady signals only, at 2.1 to 3.9
     times the cost.
   - **A standard refinement if that matters:** two or three window caps per story as multiple models, weighted by
     evidence (the draft's §3a idea applied to windows).
3. **How strength is tracked:** an **amplitude** per story (today's Gaussian N(μ, P)), updated after every window,
   with Bell's structure:
   - **Within a mark,** across the window boundary: predicted variance P ← ρ²P + σ_L²(1 − ρ²), ρ from f_D over the
     window's span.
   - **Across a gap:** the same with the gap's length, so a long space returns the spread to σ_L².
   - **σ_L comes from a long-run level L, never from the story's current μ.** It is the spread of a Rayleigh amplitude
     around its mean, 0.523·L.
   - **L** is a decision-free channel-level estimate, an AGC: for example a slow peak or high-percentile tracker of
     the channel's window magnitudes over a few seconds, fast attack and slow decay as in fldigi.
   - **Rate.** One fixed f_D for the prediction. Practice uses one rate (Bell: 1 Hz, after AGC); a fast assumption such
     as 2 to 3 Hz costs only prediction sharpness on slow fades, since the window cap already bounds the coherent part.
   - **Cost:** none in dB against the coherent bound as such. It is what turns "only the mean power known" into
     "amplitude known" in section 3.3, a 1.4 to 3.3 dB gain the coherent draft also gets.
   - **Risk:** σ_L is wrong for a story that belongs to a different, weaker station than the one setting L. A newborn
     keeps today's flat level prior, so it is not shut out.
4. **What is declared a limitation:**
   - **Chirp beyond about ±10 Hz** (with τ_c ≈ 20 ms): costs 1.5 to 10 dB on each mark's first window (section 3.4).
     Declared, as every decoder found does, after Bell widened his filter.
   - **Key clicks:** not modeled; a neighbor's clicks look like impulses. Declared, as everywhere in practice.
   - **Static crashes:** not modeled in the decoder. Practice offers a wideband noise blanker ahead of the narrow
     filtering (in this engine, ahead of the channelizer), optional because it harms crowded bands. Otherwise
     declared, as Bell's field failures show is the norm. The systematic term's floor already bounds what one strong
     piece can earn.
   - **Phase continuity across key-up:** irrelevant to this design.
5. **What it removes from today's decoder:** the 390 ms block length and its dependence on a 1 Hz placeholder. Also
   the scaling of the level's spread by μ.
   - The tracker's 0.667 Hz tolerance can be relaxed to the window's: 2 Hz costs 0.21 dB at 60 ms.

The design is derived and unmeasured. The fading diagnosis's own grid (f_D = 0.1 to 2 Hz, +10 to +30 dB S₅₀₀,
"PARIS PARIS" at 25 WPM) is the natural first test of it against the coherent draft.

---

## 6. Sources, each with how it was checked

**Practice**
- CW Skimmer manual (Afreet, PDF created 2024-05-27, 98 pp.), <http://dxatlas.com/CWSkimmer/Files/CwSkimmer.pdf>:
  read; text extracted and searched for the decoder, DSP processor, SNR and band map sections.
- VE3NEA quoted by N4ZR, "Understanding Signal-to-Noise Ratio (SNR)", RBN blog, 2014-03-05,
  <http://reversebeacon.blogspot.com/2014/03/understanding-signal-to-noise-ratio-snr.html>: read through a fetch
  summary that quotes the passages. The summary named the author "Alex Ryzhov"; VE3NEA is Alex Shovkoplyas, so the
  name is the summarizer's error.
- RBN, "How to get spotted by the RBN", <https://www.reversebeacon.net/pages/How+to+get+spotted+by+the+RBN+44>: read
  through a fetch summary.
- fldigi `src/cw_rtty/cw.cxx` and `src/include/cw.h`, `master`, <https://github.com/w1hkj/fldigi>: read, code
  downloaded and the cited lines inspected (cw.cxx lines 354–361, 597–653, 690–712, 818; cw.h DEC_RATIO 16,
  CW_SAMPLERATE 8000). `decayavg`'s formula: recollection.
- E. L. Bell, *Optimal Bayesian Estimation of the State of a Probabilistically Mapped Memory-Conditional Markov
  Process with Application to Manual Morse Decoding*, D.Eng. thesis, NPS, 1977: pp. 109–115 read in the local scan
  (`C:\KZ4APSkimmer-papers\Bell1977-optimal-bayesian-estimation-morse.pdf`, PDF pages 221–233). Listing values and
  pp. 116, 142–143 are secondary, from `docs/research/bell-1977-notes.md`.
- E. L. Bell, *Processing of the Manual Morse Signal Using Optimal Linear Filtering, Smoothing and Decoding*, NPS
  thesis, 1975, <https://archive.org/stream/processingofmanu00bellpdf/processingofmanu00bell_djvu.txt>: read through a
  fetch summary of the first 100 000 characters (of 156 000), with quoted sentences. Page numbers approximate.
- VE3NEA DeepCW, <https://github.com/VE3NEA/DeepCW>: repository front page read (notebook names only). Front-end
  details are secondary, from `docs/research/deepcw-generator-notes.md`. The benchmark is secondary, from
  `docs/research/decoder-survey.md`.
- MRP40 and G4ILO 2012: secondary, `docs/research/g4ilo-2012-notes.md`.
- Morse Expert: secondary, the cq.sk review (search result).
- Gold 1959, Guenther 1973, AG1LE's Bell port, manta: secondary, from `docs/research/decoder-survey.md`,
  `gold-1959-notes.md` and `manta-notes.md`.
- Blair 1959; OZ1JHM's decoder: recollection.
- KICS 2002 paper "Signal Processing Techniques for Recognition of CW Morse Signals": abstract only, via search.
- WSJT-X `lib/wsprd/wsprd.c` (GitHub mirror `saitohirga/WSJT-X`): read through a fetch summary quoting the comments.

**Theory**
- J. G. Proakis and M. Salehi, *Digital Communications*, 5th ed., McGraw-Hill, 2008. §13.3, pp. 846–848: read in the
  local PDF. §§4.2, 4.5: secondary, through `docs/research/proakis-ook-notes.md`, which checked the pages. All OOK
  numbers in sections 3.1–3.3 are derived here (`ook_losses.py`).
- H. L. Van Trees, *Detection, Estimation, and Modulation Theory*, Part III, Wiley, 1971: recollection (estimator–
  correlator, Doppler-spread channels, "low-energy-coherence"); chapters unchecked. The low-SNR limit in section 3.3
  is derived here, not taken from the book.
- R. Price, "Optimum detection of random signals in noise …", *IRE Trans. Inf. Theory*, 1956; T. Kailath,
  "Correlation detection of signals perturbed by a random channel", *IRE Trans. Inf. Theory*, 1960: recollection.
- G. Colavolpe and R. Raheli, "Noncoherent sequence detection", *IEEE Trans. Commun.* 47(9), 1999; D. Divsalar and
  M. K. Simon, "Multiple-symbol differential detection of MPSK", *IEEE Trans. Commun.* 38(3), 1990; R. Raheli,
  A. Polydoros and C.-K. Tzou, "Per-survivor processing", *IEEE Trans. Commun.* 43, 1995: recollection.
- A. D. Spaulding and D. Middleton, "Optimum reception in an impulsive interference environment", Parts I and II,
  *IEEE Trans. Commun.* 25(9):910–923, 924–934, 1977: citation read in DBLP; content recollection.
- C. C. Watterson et al. 1970 and ITU-R F.1487: the Gaussian Doppler spectrum and the 2σ spread convention.
  Secondary, as used in `docs/signal-processing.md` and the earlier check.
- M. K. Simon and M.-S. Alouini, *Digital Communication over Fading Channels*: not consulted. The fading averages
  here are computed directly.

**This project (step 3 only)**
- `docs/signal-processing.md` §8d; `docs/design/2026-10-09-new-decoder-design-notes.md` topic 4 items 8–9;
  `docs/research/2026-10-14-standard-practice-check/step-1-standard-practice.md` P1.1–P1.8;
  the fully coherent working draft (not in the repository) §0, §7 and the §2.3 table it cites: read.
- The prototype's fading diagnosis of 2026-10-21 (a working report not in the repository; summarized in the plan,
  D16, and in `docs/backlog.md`, "Fading splits marks in the new decoder") §1–2 was also read, for the causes the
  comparison cites.
