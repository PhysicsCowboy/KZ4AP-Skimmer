# Milestone 2, Part 1: Benchmark Scenarios, Frequency Re-centering, and the Dit-Matched Front End — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Extend the synthetic benchmark so it exposes the decoder's known weaknesses, then build step 1 of the decoder plan (spec §5.2): per-station frequency re-centering with drift tracking, and a dit-matched pre-detection filter whose envelope becomes a Rician-versus-Rayleigh log-likelihood ratio that the existing baseline decoder can key from, selectable against the old path and measured against it.

**Architecture:** The Python generator (`training/kz4ap_synth`) gains signal options (pauses, tune-up carriers, drift, keying styles, speed changes, Rayleigh fading, interferers, crowding) and named suites with a runner that calls `kz4ap-bench` and summarizes results. `kz4ap-bench` gains an oracle mode (channels at labeled frequencies, detector bypassed), separate scoring of word spaces and of each transmission's first word, and CPU time per channel-second. In the engine, two new per-station components run at the channel rate r = 1500 samples/s inside the Classical decoder when `FrontEnd::Matched` is selected: a `FrequencyTracker` (numerically controlled oscillator plus a lag-product frequency discriminator) and a `MatchedFrontEnd` (boxcar filter matched to the dit, running noise and amplitude estimates, per-sample LLR). The baseline path (`FrontEnd::Envelope`) stays the default and stays bit-identical.

**Tech Stack:** C++20, CMake ≥ 3.25, GoogleTest 1.17.0, nlohmann/json 3.12.0, Python 3.12 with numpy and pytest, GitHub Actions. No new dependencies.

**Spec:** `docs/design/2026-09-25-kz4ap-skimmer-design.md` (binding), especially §3.1 (development order), §5.2 step 1 (front end and its prerequisite, re-centering), §5.4 (the benchmark decides). Supporting documents: `docs/signal-processing.md` (what the code does now), `docs/backlog.md` section 1, `docs/research/decoder-survey.md` (option 1 and the benchmark scenario table), `docs/research/proakis-ook-notes.md`.

## Scope of this plan

Implements, from the spec and backlog:

- **A. Benchmark scenarios** (spec §5.4; backlog "Benchmark scenarios to add first"; survey scenarios A–F): pauses between transmissions, stations present from the first sample, tune-up carriers, carrier offset and slow drift, hand-keying timing models, speed changes, Rayleigh fading with a Doppler-spread parameter (VE3NEA's grid reproduced as an external anchor), interferers at a stated relative power and spacing, strong signals up to S₅₀₀ = 60 dB, a configurable minimum station spacing down to zero, and a 10–60 WPM speed range. `kz4ap-bench` scores word spaces separately from characters, scores each transmission's first word separately, reports CPU time per channel-second, and stays deterministic. Named suites: `smoke` (CI, unchanged) and `full` (local).
- **B. Frequency re-centering and drift tracking** (spec §5.2 step 1, prerequisite; backlog "Track frequency drift").
- **C. The dit-matched front end with soft likelihoods** (spec §5.2 step 1; survey option 1; backlog "Channel filtering, two stages", stage 2), consumed by the existing baseline decoder through LLR keying, with the old path kept selectable.
- **D. Measurement** of the baseline against the new front end on the new suites, written into the documents, and a CI guard for the new path.

Deliberately **not** in this plan (each gets its own plan later):

- The neural decoder (spec §5.2 step 2), the Bell-style explicit-duration HMM (step 3), hybrids (step 4), and the benchmark candidates of §5.4.
- morseformer integration as a reference decoder (spec §5.3; backlog "Integrate morseformer as a reference decoder").
- Callsign matching (spec §6), the telnet server, and the GUI and live display (spec §3.1 items 2–5).
- Real-recording scoring with manta's oracle method. This plan builds the oracle *mechanism* (channels at given frequencies, detector bypassed) for synthetic recordings; running it on real recordings needs recordings and RBN spot files and is later work.
- Moving a channel's center bin as a station drifts (the channelizer stays where the track was born; the NCO covers ±75 Hz around it), replay of the first seconds of a transmission ("Wrong or missing first characters"), the tune-up-carrier speed bug, ghost tracks beside strong signals, and separating station identity from decoding. The new scenarios *measure* all of these; fixing them is later work.
- Restating the detector's bin-counted settings in Hz (backlog). Decision: **not needed for this plan**, because no task changes the FFT bin width; the item's purpose is to make a bin-width sweep change only one variable. It stays on the backlog, ahead of that sweep.
- Impulsive noise (QRN), chirp, and CPU measurements on a Raspberry Pi 5 (no Pi in the loop yet).

## Global Constraints

- C++20; CMake minimum 3.25; the engine must not depend on Qt; no new third-party dependencies.
- Namespace `kz4ap`; public headers in `engine/include/kz4ap/`, sources in `engine/src/`, tests in `engine/tests/`; bench code in `bench/src/`, `bench/tests/`; Python in `training/kz4ap_synth/`, `training/tests/`.
- Determinism (spec §4.1): the same recording and settings must produce identical output, regardless of how input is split into calls. Every new per-sample stage runs sample by sample inside the decoder, so chunking cannot change it; every random draw in the generator comes from a seeded generator.
- **The baseline stays bit-identical.** With default settings (`FrontEnd::Envelope`, no oracle), the engine's output and the `smoke` recording's bytes must not change. `bench/baselines/smoke.json` is not edited by any task.
- **`docs/signal-processing.md` describes the engine's signal processing as it actually is.** Any change to signal processing — a parameter value, an algorithm, the order of stages, a new stage — updates that document in the same commit, including its parameter table and whether each choice is derived, measured or heuristic. Treat a stale description as a bug. (Every implementer of this plan must be told this rule.)
- **Units** (project `CLAUDE.md`): every displayed, logged or documented quantity carries an explicit unit. Every dB value names its reference (dBFS, dB SNR in a stated bandwidth, dB relative to a centered station, etc.); a bare "dB" is a bug. Linear signal values are in FS (amplitude) and FS² (power). S₅₀₀ always means key-down carrier power over noise power in 500 Hz, dB.
- American spelling in code, comments and docs.
- **Git:** all work happens on the `milestone-2` branch; commit at the end of each task without waiting for approval. Never commit to `main`, merge, or push. Never rewrite commits (no `--amend`, rebase, or resetting commits); fix mistakes with a new commit. No `Co-Authored-By` or other attribution lines. Run each git command as its own command (no `git -C`, no `cd … &&` chains).
- I/Q convention: left channel = I (real), right channel = Q (imaginary).

## Environment (Windows, run once per PowerShell session)

CMake and Ninja ship with Visual Studio Build Tools 2026 but are not on `PATH`:

```powershell
$env:Path = "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;$env:Path"
```

Common commands (from the repository root):

- Configure: `cmake --preset windows`
- Build: `cmake --build --preset windows`
- Run all C++ tests: `ctest --preset windows`
- Run matching C++ tests: `ctest --preset windows -R <regex>`
- Python tests: `.venv\Scripts\python -m pytest training -q`
- Bench executable: `build\windows\bench\Release\kz4ap-bench.exe`
- Smoke check (Git Bash): `bash bench/smoke.sh build/windows`

## Symbols

Every symbol used in this plan and in the code comments it asks for. Where a symbol already exists in `docs/signal-processing.md` §0, the meaning is the same.

| Symbol | Meaning | Unit / default |
|---|---|---|
| r | channel (decoder input) sample rate | 1500 samples/s |
| y[n] | channel sample from the channelizer | FS (complex) |
| Δf | residual carrier offset: the station's carrier minus its channel's center | Hz |
| f̂ | the tracker's estimate of Δf, which is also the NCO frequency | Hz |
| φ[n] | NCO phase | rad |
| u[n] | re-centered sample, y[n]·e^(−jφ[n]) | FS |
| L | lag of the frequency discriminator | 8 samples |
| z[n] | lag product v[n]·conj(v[n−L]) | FS² |
| Z̄ | weighted average of lag products, rotated to absolute offset | FS² |
| τ_f | time constant of that average, counted in samples of weight 1 | 0.5 s |
| T, T̂ | dit duration and the decoder's estimate of it (T = 1.2 s / WPM) | s |
| β | matched-filter length as a fraction of T̂ | 0.8 |
| K | matched-filter (boxcar) length, round(β·T̂·r) | samples |
| v[n] | matched-filter output, (1/K)·Σ of the last K values of u | FS |
| B_v | noise bandwidth of the boxcar, r/K | Hz |
| σ², σ̂² | noise variance per real component of v, and its estimate | FS² |
| s, ŝ | key-down amplitude of v, and its estimate | FS |
| x | normalized envelope, \|v\|/σ̂ | dimensionless |
| a | normalized key-down amplitude, ŝ/σ̂ | dimensionless |
| Λ | log-likelihood ratio, key-down (Rician) over key-up (Rayleigh): −a²/2 + ln I₀(a·x) | nats |
| I₀ | modified Bessel function of the first kind, order zero | dimensionless |
| P₁, P₀ | prior probability of key-down and key-up, P₀ = 1 − P₁ | 0.44, 0.56 |
| g | posterior log-odds, Λ + ln(P₁/P₀) | nats |
| p | posterior probability of key-down, 1/(1 + e^(−g)) | 0…1 |
| h | keying hysteresis on g | 1.0 nats |
| a_min | squelch: no key-down unless a ≥ a_min | 3.0 |
| c | noise-update clip: noise updates skip samples with \|v\|²/(2σ̂²) > c | 9.0 |
| — | noise guard: σ̂² is updated from v[n−K] only if g stayed at or below +1 nat over the last 2K samples | 1 nat, 2K samples |
| τ_n, τ_a | time constants of the noise and amplitude estimates, counted in samples of weight 1 | 2 s, 0.5 s |
| S₅₀₀ | key-down carrier power over noise power in 500 Hz | dB |
| E/N₀ | key-on energy per element over one-sided noise density (Proakis) | dB re 1 |
| f_D | Rayleigh-fading frequency spread (2σ of a Gaussian Doppler spectrum) | Hz |

## Design decisions

Each choice is labeled **derived** (follows from the math, with its source), **heuristic** (a judgment call), or **to be measured** (Task 12 measures it). All numbers assume r = 1500 samples/s.

### Where the new stages sit

- **Inside the Classical decoder, per sample** (heuristic, for determinism and simplicity). The decoder already owns the dit estimate the filter must follow and already processes sample by sample; putting the tracker and the front end there keeps the output independent of chunking and lets the filter follow speed changes on the very sample the estimate changes. The decoder reports its frequency estimate in each `DecodeUpdate`, so the engine can publish the refined frequency and retune the detector's track.
- **Two-stage filtering** (spec §5.2): the channelizer's ±150 Hz filter is unchanged; the narrow filter is a second stage at r.
- **Selectable:** `ClassicalDecoderConfig::front_end` is `FrontEnd::Envelope` (today's path, default) or `FrontEnd::Matched`. `kz4ap-bench --front-end baseline|matched` selects it.

### B. Frequency re-centering

- **Initial estimate: the detector's interpolated peak** (existing; measured +999.8 Hz for a station at +1000.0 Hz). The engine passes Δf₀ = (track frequency − channel center) to the decoder, which starts the NCO there. This removes most of the ±11.7 Hz bin-rounding offset before any signal-based estimate exists.
- **NCO:** u[n] = y[n]·e^(−jφ[n]), φ[n+1] = φ[n] + 2π·f̂/r, wrapped to [−π, π) (derived: a complex frequency shift).
- **Estimator: a lag-product (phase-increment) discriminator on the matched-filter output**, gated by the front end's key-down posterior (heuristic choice among standard estimators; the discriminator itself is derived). For a tone at Δf, v is still a tone at Δf − f̂ (any linear filter passes a tone unchanged in frequency), so arg(v[n]·conj(v[n−L])) = 2π(Δf − f̂)·L/r. Rotating each product by e^(+j2π·f̂·L/r) turns it into a measurement of the absolute Δf, independent of the NCO setting, so there is no feedback loop to stabilize:
  - Z̄ ← Z̄ + α·p·(z·e^(j2π·f̂·L/r) − Z̄), α = 1 − e^(−1/(τ_f·r));
  - every 32 samples (one channelizer block, 21.3 ms), if the average holds enough weight (below), f̂ ← arg(Z̄)·r/(2πL), clamped to ±75 Hz.
  - Weighting by p freezes the estimate during key-up and pauses (derived: a zero-weight update changes nothing), so it holds through gaps between transmissions.
  - The estimate moves the NCO only once the accumulated weight W (W ← W + α·p·(1 − W), from 0 toward 1) reaches 0.3 (heuristic), so the first few noisy products cannot throw away the detector's initial estimate.
- **L = 8 samples** (heuristic within derived bounds): the unambiguous range is ±r/(2L) = ±93.75 Hz (derived), which covers the ±11.7 Hz bin rounding plus drift up to the ±75 Hz clamp. A larger L would lower the noise (the error scales as 1/L) but narrow the range.
- **±75 Hz clamp** (heuristic): the channel filter loses 0.34 dB relative to the passband at 75 Hz (measured, signal-processing.md §7). Beyond that the channel itself would have to move (out of scope).
- **Why on v, not on u:** v is only B_v = r/K wide (about 26 Hz at 25 WPM), so the estimate sees about 10 dB less noise than on the 252 Hz channel, and a neighbor 100 Hz away is attenuated by the boxcar's sinc response (for example 29.7 dB relative to the passband at 25 WPM, K = 58: |sinc(100 Hz · 58/1500 s)| = 0.033). Pull-in is limited to the boxcar's main lobe, ±r/K; that is why the filter starts wide (60 WPM, ±62.5 Hz) until the speed estimate is trusted (below).
- **Expected accuracy (derived, approximate; to be measured):** with the phase noise of each product set by the per-sample SNR in B_v, and about B_v independent products per second of key-down, the RMS error at 25 WPM is roughly 0.5 Hz at S₅₀₀ = 0 dB and 0.9 Hz at S₅₀₀ = −5 dB. The lag behind a linear drift of rate ḟ (Hz/s) is about ḟ·τ_f/P₁ (derived for a first-order average of a phasor whose frequency ramps, updated only during key-down): 1.1 Hz at 1 Hz/s.
- **Test target (spec §5.2):** residual |f̂ − Δf| ≤ 2 Hz after 5 s of keying at S₅₀₀ = 10 dB, 20 WPM, from an initial error of 11 Hz (Task 10). Through a filter of length T that is a loss of |sinc(2 Hz · 60 ms)|² = 0.2 dB relative to a centered station (derived); through this plan's βT = 0.8T filter it is 0.13 dB.

### C. The matched front end

- **Filter: a boxcar (moving average) of K = round(β·T̂·r) samples, normalized by 1/K**, run before envelope detection. A boxcar of duration T is the matched filter for a rectangular element of duration T and has noise bandwidth exactly 1/T (derived: Proakis §4.2–2, proakis-ook-notes.md §2.7). Its cost is O(1) per sample (running sum over a ring buffer, recomputed exactly every 4096 samples and whenever K changes, to stop rounding drift).
- **β = 0.8** (heuristic; to be measured against 0.6 and 1.0): a filter slightly shorter than the dit costs 10·log₁₀(1/0.8) = 0.97 dB of output SNR relative to the matched filter (derived), and keeps the filter shorter than an element space even when the speed estimate is 25% too slow or a hand-keyed space is short. A filter longer than the gaps would merge successive dits, the speed estimate would then lock onto the merged marks, and the filter would never recover.
- **Following speed** (heuristic): the filter starts at the fastest code, 60 WPM (T = 20 ms, K = 24, B_v = 62.5 Hz), and follows the decoder's dit estimate once the decoder's speed window holds at least 8 marks; before that the estimate can be far off (it starts at 25 WPM). It then changes K every time the estimate changes, which happens only after a mark. K is clamped to [1, round(β · 1.2/5 · r)] = [1, 288] (5 WPM). When K changes, σ̂² is rescaled by K_old/K_new (derived: the boxcar's output noise power is proportional to 1/K when the input noise is flat across its passband, which holds because the channel filter is flat within 0.015 dB relative to the passband to ±39 Hz and K ≥ 24 gives B_v ≤ 62.5 Hz); ŝ is unchanged (a centered tone passes a normalized boxcar at unity gain).
- **LLR (derived: Proakis eq. 4.5–21, OOK case; proakis-ook-notes.md §2.2):** Λ = −a²/2 + ln I₀(a·x), with x = |v|/σ̂ and a = ŝ/σ̂, in nats. ln I₀ is computed without overflow from Abramowitz & Stegun 9.8.1–9.8.2 (relative error below 2×10⁻⁷ in I₀): the power series below 3.75, and ln I₀(z) = z − ½·ln z + ln(poly(3.75/z)) above. The relation to S₅₀₀ (derived, for noise flat across B_v): a² = 2·S₅₀₀·(500 Hz)·K/r, where S₅₀₀ is a linear power ratio here. At 25 WPM (K = 58) and S₅₀₀ = 0 dB, a = 6.2. The noise-bandwidth reduction from the 252 Hz channel to B_v is 10·log₁₀(252/25.9) = 9.9 dB at 25 WPM (derived); how much CER that buys is to be measured.
- **Prior P₁ = 0.44** (derived from PARIS timing: key-down 22 of 50 dit units). The posterior log-odds is g = Λ + ln(P₁/P₀).
- **Amplitude estimate: an online EM update for the Rician component** (derived from the densities; the running form is heuristic). Per sample, with p the posterior: ŝ² ← max(0, ŝ² + p·max(α_a, 1/W_a)·(|v|² − 2σ̂² − ŝ²)), using the Rician mean square 2σ² + s² (Proakis eq. 2.3–58). W_a accumulates the weights p, so the estimate is a weighted running mean at first and an exponential average (α_a = 1 − e^(−1/(τ_a·r))) afterwards, as the detector's power average is. Samples on the boxcar's ramps (a mark entering or leaving the window) with p ≈ 1 pull ŝ low: about 11% in amplitude for 25 WPM dits at high SNR (derived: plateau of 14 samples and two ramps of 29 samples above half amplitude per dit). Through the decision threshold (near ŝ/2 at high SNR) that lengthens marks by about 4 ms at 25 WPM; to be measured.
- **Noise estimate: key-up samples with a guard band** (heuristic). A plain EM update for σ̂² fails on the boxcar's ramps: while a mark enters or leaves the window, |v| rises through the noise level toward s, those samples have p ≈ 0 until |v| nears s/2, and at 25 WPM and S₅₀₀ ≈ 5 dB they would inflate σ̂² about 2× (derived: about 23 ramp samples per edge at roughly 4× the noise power, against about 500 clean key-up samples per second). So σ̂² is updated only from the sample K back, v[n−K], and only if no sample in the last 2K had posterior log-odds g above +1 nat (the window of v[n−K] then holds no mark, as judged by the posterior), and only if |v[n−K]|²/(2σ̂²) ≤ c: σ̂² ← σ̂² + max(α_n, 1/W_n)·(|v[n−K]|²/2 − σ̂²), W_n counting the updates. The clip c = 9 is a second guard for marks too weak or short to raise g; in noise alone |v|²/(2σ²) is exponentially distributed with mean 1 (Proakis eq. 2.3–43), so the clip drops a fraction e⁻⁹ ≈ 1.2×10⁻⁴ of noise samples and biases σ̂² 0.005 dB below the true noise power (derived). Without the guard, a station at S₅₀₀ = 60 dB would inflate σ̂² by orders of magnitude and the LLR would collapse; this is a Review Focus item.
- **Time constants τ_n = 2 s and τ_a = 0.5 s** (heuristic; to be measured on the fading suite): noise is stationary, so it can be averaged longer; the amplitude must follow fading (f_D up to 3 Hz) but still average several elements.
- **Warm-up (heuristic):** for the first 0.2 s the front end only collects |v|². It then starts σ̂² at the 20th percentile divided by 2·(−ln 0.8) (the 20th percentile of an exponential with mean 2σ² is 2σ²·(−ln 0.8), derived) and ŝ² at max(0, 90th percentile − 2σ̂²). Starting ŝ above zero matters: the mixture fit started from equal components never separates them. The warm-up then counts as weight W_n = 0.8 and W_a = 0.1 times its sample count, so the first samples after it refine the estimates instead of replacing them. Nothing is keyed during warm-up.
- **Squelch a_min = 3** (heuristic; to be measured): E/N₀ = a²/2 = 4.5 (6.5 dB re 1) for a matched filter (proakis-ook-notes.md §2.1), where a hard per-element decision already errs about 10% of the time (§2.6), so decoding below it produces mostly garbage. Analysis of the noise-only fixed point (for small a, the p-weighted mean of |v|² − 2σ² is about P₀·a²·σ², so each time constant multiplies a² by about P₀ = 0.56) says noise alone drives a toward 0, well below 3. At 25 WPM a = 3 corresponds to S₅₀₀ ≈ −6.3 dB.
- **Correlated samples (derived):** the boxcar's output noise autocorrelation is triangular over ±(K − 1) samples and sums to exactly K. So per-sample LLRs overcount the evidence by a factor K; every `FrontEndSample` carries `weight = 1/K`, the factor a sequence decoder must multiply each Λ by before summing (scaling, not decimation, so 0.67 ms timing resolution is kept for the edges). The baseline decoder does not sum LLRs, so it ignores the weight; the HMM plan will use it. A unit test checks the sum of the autocorrelation.
- **How the baseline decoder consumes it (heuristic):** key down when g > +h, key up when g < −h, h = 1 nat, replacing the 40%/60% thresholds, the envelope smoother, the warm-up and the mark/space squelch; key up and no key-down while a < a_min. Everything after keying (glitch rejection, element classification, gaps, speed estimation) is unchanged. At high SNR the decision point on x is near a/2 (proakis-ook-notes.md §2.3), so both edges are delayed by about K/2 samples and mark lengths are preserved; at lower SNR the threshold rises (b/a = 0.61 at E/N₀ = 10 dB re 1) and marks shorten by about (2b/a − 1)·K samples. The decoder's existing edge-shortening correction (dah/dit ratio 3.0–3.85) absorbs that. Decoded times include the filter's group delay, (K − 1)/2 samples.

### A. Benchmark design

- **Oracle mode** (heuristic, following manta's oracle idea): for the sensitivity, fading, fist, speed, interference and tuning suites, channels open at the labeled frequencies *rounded to the FFT bin* from the first sample, and the detector is bypassed. Reason: the detector's 6 dB-per-bin threshold stops at about S₅₀₀ ≈ 0 dB, so without an oracle the front end's gain below that would be invisible. Rounding to the bin (and starting the NCO at 0) leaves the tracker the full ±11.7 Hz to find, which is the worst case. The end-to-end suites (band, crowded, strong, pauses, tune-up, first sample) keep the detector.
- **Word spaces and first words** (spec §5.4): one minimum-edit alignment of decoded against reference symbols; each edit is charged to one reference symbol; an edit that involves a word space on either side is a space edit, the rest are character edits. Character CER = character edits / reference characters; space error rate = space edits / reference word spaces; first-word CER = edits charged to the first word of each transmission / that word's symbols. Total edits are unchanged (same Levenshtein optimum), so the existing CER stays comparable.
- **CPU time per channel-second:** process CPU time over the whole run divided by the total duration of channel output delivered to decoders (an upper bound, since it includes the shared FFT and detector), plus the time spent inside decoders per channel-second (steady clock, single thread).
- **Fading model:** complex Gaussian gain, E|g|² = 1, Gaussian Doppler power spectrum with frequency spread f_D = 2σ (the Watterson / CCIR 520 HF convention). S₅₀₀ with fading is the *mean* key-down power over the noise in 500 Hz. Whether VE3NEA used the same spread convention is an open question (it affects how exactly his numbers anchor ours).
- **Keying styles** (heuristic except where noted): element and space durations are log-normal, with medians at the nominal 1, 3, 1, 3, 7 dit units and a standard deviation of ln(duration) per style. Only the hand-key dah (median e^1.5 ≈ 4.48 dits, σ = 0.3) comes from VE3NEA's generator (research notes, `neural_decoders.md`); the other values are this project's, pending a transcription of VE3NEA's per-style table.

## Review Focus

Inputs the spec implies but does not spell out, most likely to bite first. Each has a test in the owning task.

1. **Strong signals (S₅₀₀ up to 60 dB):** the noise estimate must not be inflated by the filter's ramps at key-up and key-down, and the decoder must decode exactly — Task 9 (`StrongSignalKeepsNoiseEstimate`), Task 10 (`MatchedDecodesStrongSignal`).
2. **Drifting carriers:** the tracker follows a 1 Hz/s drift within 2 Hz, the reported frequency follows the station, and the detector keeps one track as the station moves across bins — Task 8 (`FollowsSlowDrift`), Task 11 (`MatchedReportsDriftingFrequency`, `SignalDetector.RetuneMovesTrackBin`).
3. **Speed changes mid-transmission (20 → 35 WPM):** the matched filter shortens with the speed estimate and decoding continues after the change — Task 10 (`MatchedFollowsSpeedChange`).
4. **Pauses and stations that stop:** the frequency and amplitude estimates hold through 10 s of noise, and no text is decoded from noise after a station stops (the "stray E's" of the backlog) — Task 8 (`ZeroWeightFreezesEstimate`), Task 10 (`MatchedHoldsThroughPause`, `MatchedNoiseAfterStationStopsDecodesNothing`).
5. **Closely spaced stations:** a station 10 dB stronger 100 Hz away inside the same ±150 Hz channel must not capture the frequency tracker, and the wanted station must still decode — Task 10 (`MatchedIgnoresStrongerNeighbor`).

## File map

| File | Responsibility |
|---|---|
| `training/kz4ap_synth/keying.py` (new) | Keying styles: log-normal timing, imbalance, speed changes |
| `training/kz4ap_synth/fading.py` (new) | Rayleigh fading gain with a Gaussian Doppler spectrum |
| `training/kz4ap_synth/generate.py` | Signal options, interval planning, labels with transmissions, band-scenario settings |
| `training/kz4ap_synth/suites.py` (new) | Named suites, recording generation, bench runner, summaries |
| `training/tests/test_keying.py`, `test_fading.py`, `test_suites.py` (new); `test_generate.py` | Tests for the above |
| `bench/src/labels.*` | Labels with transmissions and a `score` flag |
| `bench/src/scoring.*` | Alignment, character/space/first-word error rates, match by order |
| `bench/src/cpu_time.*` (new) | Portable process CPU time |
| `bench/src/main.cpp` | `--oracle`, `--front-end`, new outputs, CPU per channel-second |
| `bench/tests/*` | Tests for labels and scoring |
| `engine/include/kz4ap/frequency_tracker.hpp`, `engine/src/frequency_tracker.cpp` (new) | NCO and frequency discriminator |
| `engine/include/kz4ap/matched_front_end.hpp`, `engine/src/matched_front_end.cpp` (new) | ln I₀, LLR, boxcar filter, noise and amplitude estimates |
| `engine/include/kz4ap/classical_decoder.hpp`, `engine/src/classical_decoder.cpp` | `FrontEnd::Matched` mode |
| `engine/include/kz4ap/decoder.hpp` | `DecodeUpdate::freq_offset_hz` |
| `engine/include/kz4ap/signal_detector.hpp`, `engine/src/signal_detector.cpp` | `retune()` |
| `engine/include/kz4ap/engine.hpp`, `engine/src/engine.cpp` | Oracle channels, statistics, initial offset, refined frequency, retune |
| `engine/tests/*` | New and extended tests; `test_signals.hpp` gains carrier drift |
| `bench/smoke.sh`, `bench/baselines/smoke-matched.json` (new) | Smoke check of the matched front end (Task 12) |
| `docs/signal-processing.md`, `docs/backlog.md`, `docs/research/decoder-survey.md`, `README.md` | Documentation |

## Tasks

- Task 1: Generator — transmissions with pauses, tune-up carriers, carrier drift (smoke recording guarded)
- Task 2: Generator — keying styles, key imbalance, speed changes
- Task 3: Generator — Rayleigh fading
- Task 4: Generator — band-scenario settings, interferers, tags
- Task 5: Bench — word spaces and first words scored separately
- Task 6: Engine and bench — oracle channels and CPU time per channel-second
- Task 7: Benchmark suites and runner
- Task 8: Frequency tracker
- Task 9: Matched front end
- Task 10: Classical decoder — the Matched mode
- Task 11: Engine — re-centering from the detector, refined frequency, drift retune, `--front-end`
- Task 12: Measure, document, and guard the new path in CI

---

### Task 1: Generator — transmissions with pauses, tune-up carriers, carrier drift

A signal can now send its text several times with pauses between (as between CQs), start with an unkeyed tune-up carrier, start at the very first sample, and drift in frequency. The labels file lists each transmission, so the bench can score each one's first word (Task 5). Random keying (Task 2) and fading (Task 3) will draw from per-signal generators, so this task also restructures interval planning around them. **The milestone-1 output must not change**: a test compares the new generator against a frozen copy of the old one.

**Files:**
- Modify: `training/kz4ap_synth/generate.py` (whole file shown below)
- Modify: `training/tests/test_generate.py`
- Modify: `docs/signal-processing.md` (§11)

**Interfaces:**
- Produces (Python, `kz4ap_synth.generate`):
  - `SignalSpec` gains `repeats: int = 1`, `pause_s: float = 0.0`, `tune_s: float = 0.0`, `drift_hz_per_s: float = 0.0`.
  - `TUNE_GAP_S = 0.5` (s of silence between a tune-up carrier and the first element).
  - `SignalPlan(intervals: list[tuple[float, float]], transmissions: list[tuple[float, float]])`, times in s from `spec.start_s`.
  - `sending_intervals(spec: SignalSpec, rng: np.random.Generator) -> list[tuple[float, float]]` (key-down intervals of one sending of `spec.text`, from 0 s; Task 2 replaces its body).
  - `plan_signal(spec, rng) -> SignalPlan`; `plan_intervals(signals, seed: int) -> list[SignalPlan]` (signal i uses `np.random.default_rng([seed, i, 2])`).
  - `reference_text(spec) -> str` (the text repeated `repeats` times, space-separated).
  - `signal_end_s(spec, plan) -> float`.
  - `labels(signals, sample_rate, duration_s, seed) -> dict` (seed is now required: it must be the seed passed to `generate`). Each label entry adds `"transmissions": [{"text", "start_s", "end_s"}]`, and its `"text"` is `reference_text(spec)`.

- [ ] **Step 1: Write the failing tests**

In `training/tests/test_generate.py`, extend the import block to:

```python
from kz4ap_synth.generate import (
    DEFAULT_NOISE_SIGMA,
    TUNE_GAP_S,
    SignalSpec,
    amplitude_for_snr,
    generate,
    keying_envelope,
    labels,
    main,
    plan_intervals,
    scenario_band,
    write_wav,
)
```

and append:

```python
def _milestone1_generate(signals, sample_rate, duration_s, seed):
    """Frozen copy of the milestone-1 generator, to prove the defaults did not change."""
    rng = np.random.default_rng(seed)
    n = int(round(duration_s * sample_rate))
    iq = np.zeros(n, dtype=np.complex128)
    iq += (rng.standard_normal(n) + 1j * rng.standard_normal(n)) * (DEFAULT_NOISE_SIGMA / np.sqrt(2))
    for s in signals:
        phase = rng.uniform(0, 2 * np.pi)
        intervals = keying_intervals(s.text, s.wpm)
        if not intervals:
            continue
        i0 = max(0, int(s.start_s * sample_rate))
        i1 = min(n, int(np.ceil((s.start_s + intervals[-1][1]) * sample_rate)) + 1)
        if i1 <= i0:
            continue
        env = keying_envelope(intervals, s.start_s - i0 / sample_rate, i1 - i0, sample_rate)
        t = np.arange(i0, i1) / sample_rate
        carrier = np.exp(1j * (2 * np.pi * s.freq_offset_hz * t + phase))
        iq[i0:i1] += amplitude_for_snr(s.snr_db, sample_rate) * env * carrier
    return iq


def test_default_signals_match_milestone_1_generator():
    specs = scenario_band(np.random.default_rng(3), 6, 20.0, 48000)
    new = generate(specs, 48000, 20.0, seed=4)
    old = _milestone1_generate(specs, 48000, 20.0, seed=4)
    assert np.array_equal(new, old)


def test_repeats_send_the_text_again_after_each_pause():
    spec = SignalSpec("TEST", 1000.0, 20.0, 20.0, 1.0, repeats=3, pause_s=2.0)
    plan = plan_intervals([spec], seed=1)[0]
    single = keying_intervals("TEST", 20.0)
    assert len(plan.intervals) == 3 * len(single)
    assert len(plan.transmissions) == 3
    first, second, third = plan.transmissions
    assert first == pytest.approx((0.0, single[-1][1]))
    assert second[0] == pytest.approx(first[1] + 2.0)
    assert third[0] == pytest.approx(second[1] + 2.0)


def test_labels_list_transmissions_and_the_full_reference_text():
    spec = SignalSpec("CQ K1ABC", 1000.0, 25.0, 20.0, 0.5, repeats=2, pause_s=3.0)
    entry = labels([spec], 8000, 20.0, seed=1)["signals"][0]
    assert entry["text"] == "CQ K1ABC CQ K1ABC"
    assert [t["text"] for t in entry["transmissions"]] == ["CQ K1ABC", "CQ K1ABC"]
    assert entry["transmissions"][0]["start_s"] == pytest.approx(0.5)
    assert entry["transmissions"][1]["start_s"] == pytest.approx(
        entry["transmissions"][0]["end_s"] + 3.0, abs=2e-3)
    assert entry["end_s"] == pytest.approx(entry["transmissions"][1]["end_s"], abs=1e-3)


def test_tune_up_carrier_precedes_the_keying():
    fs = 8000
    spec = SignalSpec("E", 500.0, 20.0, 20.0, 1.0, tune_s=1.0)
    plan = plan_intervals([spec], seed=1)[0]
    assert plan.intervals[0] == (0.0, 1.0)
    assert plan.intervals[1][0] == pytest.approx(1.0 + TUNE_GAP_S)
    assert plan.transmissions == [plan.intervals[1]]
    iq = generate([spec], fs, 4.0, seed=1, add_noise=False)
    assert abs(iq[int(1.5 * fs)]) == pytest.approx(amplitude_for_snr(20.0, fs), rel=1e-6)  # mid-carrier
    assert abs(iq[int(2.25 * fs)]) < 1e-9                                                 # the gap


def test_drift_moves_the_carrier_linearly():
    fs = 8000
    spec = SignalSpec("E", 100.0, 20.0, 20.0, 0.5, tune_s=4.0, drift_hz_per_s=5.0)
    iq = generate([spec], fs, 6.0, seed=1, add_noise=False)

    def freq_at(t_s):
        i = int(t_s * fs)
        return np.angle(iq[i + 1] * np.conj(iq[i])) * fs / (2 * np.pi)

    assert freq_at(1.5) == pytest.approx(100.0 + 5.0 * 1.0, abs=0.1)
    assert freq_at(3.5) == pytest.approx(100.0 + 5.0 * 3.0, abs=0.1)


def test_signal_can_start_at_the_first_sample():
    fs = 8000
    spec = SignalSpec("T", 500.0, 20.0, 20.0, 0.0)
    iq = generate([spec], fs, 2.0, seed=1, add_noise=False)
    assert abs(iq[0]) > 0.0
    assert abs(iq[int(0.05 * fs)]) == pytest.approx(amplitude_for_snr(20.0, fs), rel=1e-6)
    assert labels([spec], fs, 2.0, seed=1)["signals"][0]["transmissions"][0]["start_s"] == 0.0
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `.venv\Scripts\python -m pytest training/tests/test_generate.py -q`
Expected: collection error, `ImportError: cannot import name 'TUNE_GAP_S'`.

- [ ] **Step 3: Implement**

Replace `training/kz4ap_synth/generate.py` with:

```python
"""Synthetic CW I/Q recordings with known answers, for testing and benchmarking.

Writes a 16-bit stereo WAV file (left = I, right = Q) and, next to it, a JSON
labels file describing every signal in it.
"""

from __future__ import annotations

import argparse
import json
import wave
from dataclasses import asdict, dataclass
from pathlib import Path

import numpy as np

from .morse import keying_intervals

SNR_BANDWIDTH_HZ = 500.0
DEFAULT_NOISE_SIGMA = 0.02
RISE_S = 0.005
TUNE_GAP_S = 0.5  # silence between a tune-up carrier and the first element, s

CALL_PREFIXES = ["K", "W", "N", "AA", "KB", "DL", "G", "F", "JA", "VE",
                 "EA", "I", "OH", "SM", "UA", "PY", "VK", "ZL"]
MESSAGES = ["CQ TEST {c} {c}", "CQ CQ DE {c} {c} K", "TU {c}", "{c} 5NN 14", "CQ {c} {c} TEST"]


@dataclass
class SignalSpec:
    text: str
    freq_offset_hz: float        # carrier offset from the recording's center at start_s, Hz
    wpm: float
    snr_db: float                # S500: key-down carrier power over noise power in 500 Hz, dB
    start_s: float
    repeats: int = 1             # the text is sent this many times
    pause_s: float = 0.0         # silence between sendings, s
    tune_s: float = 0.0          # unkeyed carrier before the first sending, s (0 = none)
    drift_hz_per_s: float = 0.0  # carrier frequency change from start_s on, Hz/s


@dataclass
class SignalPlan:
    intervals: list[tuple[float, float]]      # every key-down interval, s from start_s
    transmissions: list[tuple[float, float]]  # (first key-down, last key-up) of each sending, s from start_s


def amplitude_for_snr(snr_db: float, sample_rate: int) -> float:
    """Carrier amplitude giving snr_db against DEFAULT_NOISE_SIGMA noise, measured in 500 Hz."""
    noise_in_band = DEFAULT_NOISE_SIGMA**2 * SNR_BANDWIDTH_HZ / sample_rate
    return float(np.sqrt(10 ** (snr_db / 10) * noise_in_band))


def keying_envelope(intervals, offset_s: float, n: int, sample_rate: int) -> np.ndarray:
    """0..1 envelope with raised-cosine edges; intervals are shifted by offset_s."""
    env = np.zeros(n)
    ramp_len = max(1, int(round(RISE_S * sample_rate)))
    ramp = 0.5 - 0.5 * np.cos(np.pi * (np.arange(ramp_len) + 0.5) / ramp_len)
    for on, off in intervals:
        i0 = max(0, int(round((offset_s + on) * sample_rate)))
        i1 = min(n, int(round((offset_s + off) * sample_rate)))
        if i1 <= i0:
            continue
        env[i0:i1] = 1.0
        r = min(ramp_len, (i1 - i0) // 2)
        if r > 0:
            env[i0:i0 + r] *= ramp[:r]
            env[i1 - r:i1] *= ramp[:r][::-1]
    return env


def sending_intervals(spec: SignalSpec, rng: np.random.Generator) -> list[tuple[float, float]]:
    """Key-down intervals of one sending of spec.text, from 0 s."""
    return keying_intervals(spec.text, spec.wpm)


def plan_signal(spec: SignalSpec, rng: np.random.Generator) -> SignalPlan:
    """All key-down intervals of one signal: the tune-up carrier, then each sending."""
    intervals: list[tuple[float, float]] = []
    transmissions: list[tuple[float, float]] = []
    t = 0.0
    if spec.tune_s > 0:
        intervals.append((0.0, spec.tune_s))
        t = spec.tune_s + TUNE_GAP_S
    for _ in range(spec.repeats):
        sent = sending_intervals(spec, rng)
        if not sent:
            break
        if transmissions:
            t = transmissions[-1][1] + spec.pause_s
        intervals.extend((t + on, t + off) for on, off in sent)
        transmissions.append((t + sent[0][0], t + sent[-1][1]))
    return SignalPlan(intervals, transmissions)


def plan_intervals(signals, seed: int) -> list[SignalPlan]:
    """Plans every signal. Random timing for signal i comes from its own generator,
    seeded by (seed, i), so a random feature of one signal never changes another."""
    return [plan_signal(s, np.random.default_rng([seed, i, 2])) for i, s in enumerate(signals)]


def generate(signals, sample_rate: int, duration_s: float, seed: int, add_noise: bool = True) -> np.ndarray:
    """Complex I/Q samples containing the given signals plus white noise."""
    rng = np.random.default_rng(seed)
    n = int(round(duration_s * sample_rate))
    iq = np.zeros(n, dtype=np.complex128)
    if add_noise:
        iq += (rng.standard_normal(n) + 1j * rng.standard_normal(n)) * (DEFAULT_NOISE_SIGMA / np.sqrt(2))
    for s, plan in zip(signals, plan_intervals(signals, seed)):
        phase = rng.uniform(0, 2 * np.pi)
        intervals = plan.intervals
        if not intervals:
            continue
        i0 = max(0, int(s.start_s * sample_rate))
        i1 = min(n, int(np.ceil((s.start_s + intervals[-1][1]) * sample_rate)) + 1)
        if i1 <= i0:
            continue
        env = keying_envelope(intervals, s.start_s - i0 / sample_rate, i1 - i0, sample_rate)
        t = np.arange(i0, i1) / sample_rate
        angle = 2 * np.pi * s.freq_offset_hz * t + phase
        if s.drift_hz_per_s:
            angle = angle + np.pi * s.drift_hz_per_s * (t - s.start_s) ** 2
        iq[i0:i1] += amplitude_for_snr(s.snr_db, sample_rate) * env * np.exp(1j * angle)
    return iq


def write_wav(path, iq: np.ndarray, sample_rate: int) -> None:
    """Write I/Q as 16-bit stereo PCM, scaling everything down if needed to avoid clipping."""
    peak = max(float(np.max(np.abs(iq.real))), float(np.max(np.abs(iq.imag))), 1e-12)
    scale = min(1.0, 0.95 / peak)
    stereo = np.empty((len(iq), 2), dtype="<i2")
    stereo[:, 0] = np.round(iq.real * scale * 32767)
    stereo[:, 1] = np.round(iq.imag * scale * 32767)
    with wave.open(str(path), "wb") as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(sample_rate)
        w.writeframes(stereo.tobytes())


def reference_text(spec: SignalSpec) -> str:
    """What a perfect decoder would print for the whole signal."""
    return " ".join([spec.text] * spec.repeats)


def signal_end_s(spec: SignalSpec, plan: SignalPlan) -> float:
    """Time the signal's keying finishes, relative to the recording start."""
    return spec.start_s + (plan.intervals[-1][1] if plan.intervals else 0.0)


def labels(signals, sample_rate: int, duration_s: float, seed: int) -> dict:
    """Labels for a recording; seed must be the one passed to generate()."""
    entries = []
    for s, plan in zip(signals, plan_intervals(signals, seed)):
        entries.append({
            **asdict(s),
            "text": reference_text(s),
            "end_s": round(signal_end_s(s, plan), 3),
            "transmissions": [
                {"text": s.text, "start_s": round(s.start_s + on, 3), "end_s": round(s.start_s + off, 3)}
                for on, off in plan.transmissions
            ],
        })
    return {
        "sample_rate": sample_rate,
        "duration_s": duration_s,
        "snr_bandwidth_hz": SNR_BANDWIDTH_HZ,
        "signals": entries,
    }


def random_callsign(rng) -> str:
    letters = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    prefix = CALL_PREFIXES[rng.integers(len(CALL_PREFIXES))]
    suffix = "".join(letters[rng.integers(26)] for _ in range(int(rng.integers(1, 4))))
    return f"{prefix}{rng.integers(10)}{suffix}"


def fill_text(message: str, wpm: float, available_s: float) -> str:
    """Repeat message, space-separated, as many times as fits in available_s."""
    text = message
    while True:
        candidate = f"{text} {message}"
        if keying_intervals(candidate, wpm)[-1][1] > available_s:
            return text
        text = candidate


def scenario_single() -> list[SignalSpec]:
    return [SignalSpec("CQ TEST K1ABC K1ABC", 1000.0, 25.0, 20.0, 0.5)]


def scenario_band(rng, count: int, duration_s: float, sample_rate: int) -> list[SignalSpec]:
    """count signals spread over 80% of the span, at least 1 kHz apart."""
    span = 0.4 * sample_rate
    specs: list[SignalSpec] = []
    max_attempts = 1000 * count
    for _attempt in range(max_attempts):
        if len(specs) >= count:
            break
        freq = round(float(rng.uniform(-span, span)), 1)
        if any(abs(freq - s.freq_offset_hz) < 1000 for s in specs):
            continue
        wpm = round(float(rng.uniform(18, 36)), 1)
        snr = round(float(rng.uniform(10, 30)), 1)
        start = round(float(rng.uniform(0, 2)), 3)
        message = MESSAGES[rng.integers(len(MESSAGES))].format(c=random_callsign(rng))
        text = fill_text(message, wpm, duration_s - start - 1.0)
        specs.append(SignalSpec(text, freq, wpm, snr, start))
    if len(specs) < count:
        raise ValueError(
            f"cannot place {count} signals at least 1 kHz apart within +/-{span:.0f} Hz"
        )
    return specs


def main(argv=None) -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--scenario", choices=["single", "band"], default="single")
    parser.add_argument("--signals", type=int, default=10)
    parser.add_argument("--duration", type=float, default=30.0)
    parser.add_argument("--sample-rate", type=int, default=192000)
    parser.add_argument("--seed", type=int, default=1)
    parser.add_argument("--out", type=Path, required=True,
                        help="output .wav file; the labels go next to it as .json")
    args = parser.parse_args(argv)

    rng = np.random.default_rng(args.seed)
    if args.scenario == "single":
        specs = scenario_single()
    else:
        try:
            specs = scenario_band(rng, args.signals, args.duration, args.sample_rate)
        except ValueError as exc:
            parser.error(str(exc))

    noise_seed = args.seed + 1
    for spec, plan in zip(specs, plan_intervals(specs, noise_seed)):
        end = signal_end_s(spec, plan)
        if end > args.duration:
            parser.error(
                f"signal {spec.text!r} needs at least {end:.3f}s of recording "
                f"but --duration is {args.duration}s"
            )

    iq = generate(specs, args.sample_rate, args.duration, seed=noise_seed)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    write_wav(args.out, iq, args.sample_rate)
    args.out.with_suffix(".json").write_text(
        json.dumps(labels(specs, args.sample_rate, args.duration, seed=noise_seed), indent=2) + "\n")


if __name__ == "__main__":
    main()
```

The phase is still drawn before the empty-intervals check, and the drift term is added only when it is nonzero: that keeps milestone 1's order of random draws and its arithmetic, which the first test pins bit for bit.

- [ ] **Step 4: Run the tests to verify they pass**

Run: `.venv\Scripts\python -m pytest training -q`
Expected: all tests pass (the existing ones plus 6 new).

- [ ] **Step 5: Confirm the smoke check is unaffected**

Build if needed (`cmake --build --preset windows`), then in Git Bash: `bash bench/smoke.sh build/windows`
Expected: `smoke test passed`, with the same `CER …, detected 8 of 8 …` line as before this task. Write that CER value down and pass it to the reviewer: Tasks 4–12 compare against it.

- [ ] **Step 6: Document the definitions**

In `docs/signal-processing.md`, section 11, after the **SNR** bullet, insert:

```markdown
- **Transmissions** (synthetic recordings): a signal may send its text
  several times (`repeats`), with `pause_s` seconds of silence between the
  last key-up of one sending and the first key-down of the next. The labels
  file lists each sending as a transmission with its start and end time, s;
  the signal's reference text is the sendings joined by word spaces. A
  signal may start at 0 s, the first sample of the recording.
- **Tune-up carrier:** an unkeyed carrier of `tune_s` seconds before the
  first sending, followed by 0.5 s of silence. It is not part of the
  reference text.
- **Drift:** the carrier's frequency changes linearly at `drift_hz_per_s`
  (Hz/s) from `freq_offset_hz` at the signal's start time t₀; its phase is
  2π·(f·t + ½·ḟ·(t − t₀)²).
```

- [ ] **Step 7: Commit on the `milestone-2` branch**

```powershell
git add training/kz4ap_synth/generate.py training/tests/test_generate.py docs/signal-processing.md
```
```powershell
git commit -m "Add transmissions with pauses, tune-up carriers and drift to the generator"
```

---

### Task 2: Generator — keying styles, key imbalance, speed changes

Senders other than a keyer: element and space durations drawn from log-normal distributions per keying style (survey scenario C), a key-on/off imbalance, and speed changes inside a message, as a step or a ramp (scenario D).

**Files:**
- Create: `training/kz4ap_synth/keying.py`
- Modify: `training/kz4ap_synth/generate.py` (`SignalSpec`, `sending_intervals`)
- Test: `training/tests/test_keying.py` (new); `training/tests/test_generate.py`
- Modify: `docs/signal-processing.md` (§11)

**Interfaces:**
- Consumes: `kz4ap_synth.morse.CODES`, `symbols`, `keying_intervals`; `generate.SignalSpec`, `plan_intervals` (Task 1).
- Produces (Python):
  - `kz4ap_synth.keying.STYLES: dict[str, KeyingStyle]` with keys `"machine"`, `"paddle"`, `"bug"`, `"hand"`; `Duration(median_dits: float, sigma: float)`; `KeyingStyle(dit, dah, element_gap, char_gap, word_gap)`; `MIN_DITS = 0.2`.
  - `timed_intervals(text: str, wpm: float, style: str = "machine", rng: np.random.Generator | None = None, wpm_end: float | None = None, profile: str = "step", imbalance_dits: float = 0.0) -> list[tuple[float, float]]`.
  - `SignalSpec` gains `keying: str = "machine"`, `wpm_end: float | None = None`, `speed_profile: str = "step"`, `imbalance_dits: float = 0.0`. These appear in the labels file automatically.

- [ ] **Step 1: Write the failing tests**

Create `training/tests/test_keying.py`:

```python
import math

import numpy as np
import pytest

from kz4ap_synth.keying import STYLES, timed_intervals
from kz4ap_synth.morse import keying_intervals


def marks(intervals):
    return [off - on for on, off in intervals]


def spaces(intervals):
    return [b[0] - a[1] for a, b in zip(intervals, intervals[1:])]


def test_machine_style_is_exact_paris_timing():
    assert timed_intervals("PARIS CQ", 20.0) == keying_intervals("PARIS CQ", 20.0)


def test_paddle_elements_are_exact_and_character_spaces_vary():
    rng = np.random.default_rng(1)
    iv = timed_intervals(" ".join(["CQ"] * 30), 20.0, "paddle", rng)
    dit = 1.2 / 20.0
    for m in marks(iv):
        assert m == pytest.approx(dit) or m == pytest.approx(3 * dit)
    gaps = spaces(iv)
    element_gaps = [g for g in gaps if g < 1.5 * dit]
    assert all(g == pytest.approx(dit) for g in element_gaps)
    long_gaps = [g for g in gaps if g >= 1.5 * dit]
    assert np.std(long_gaps) > 0.05 * dit


def test_hand_key_dah_median_and_spread_follow_ve3nea():
    rng = np.random.default_rng(2)
    iv = timed_intervals(" ".join(["TE"] * 400), 20.0, "hand", rng)
    dit = 1.2 / 20.0
    lengths = np.array(marks(iv)) / dit
    dahs = lengths[0::2]
    dits = lengths[1::2]
    assert np.median(dahs) / np.median(dits) == pytest.approx(math.exp(1.5), rel=0.06)
    assert np.std(np.log(dahs)) == pytest.approx(0.3, abs=0.03)


def test_same_generator_state_gives_the_same_timing():
    a = timed_intervals("CQ TEST", 25.0, "bug", np.random.default_rng(5))
    b = timed_intervals("CQ TEST", 25.0, "bug", np.random.default_rng(5))
    c = timed_intervals("CQ TEST", 25.0, "bug", np.random.default_rng(6))
    assert a == b
    assert a != c


def test_speed_step_switches_at_the_middle_word():
    iv = timed_intervals("E E E E", 20.0, wpm_end=40.0, profile="step")
    assert marks(iv) == pytest.approx([0.06, 0.06, 0.03, 0.03])


def test_speed_ramp_changes_linearly_per_word():
    iv = timed_intervals("E E E E E", 20.0, wpm_end=40.0, profile="ramp")
    assert marks(iv) == pytest.approx([1.2 / w for w in (20.0, 25.0, 30.0, 35.0, 40.0)])


def test_imbalance_lengthens_marks_and_shortens_spaces():
    iv = timed_intervals("EE", 20.0, imbalance_dits=0.1)
    assert marks(iv) == pytest.approx([1.1 * 0.06, 1.1 * 0.06])
    assert spaces(iv) == pytest.approx([2.9 * 0.06])


def test_unknown_style_or_profile_raises():
    with pytest.raises(ValueError):
        timed_intervals("E", 20.0, "straight")
    with pytest.raises(ValueError):
        timed_intervals("E E", 20.0, wpm_end=30.0, profile="sine")


def test_random_style_without_a_generator_raises():
    with pytest.raises(ValueError):
        timed_intervals("E", 20.0, "hand")


def test_every_style_is_defined():
    assert set(STYLES) == {"machine", "paddle", "bug", "hand"}
```

Append to `training/tests/test_generate.py`:

```python
def test_hand_keyed_signal_is_reproducible_and_independent_of_its_neighbors():
    a = SignalSpec("CQ TEST K1ABC", 1000.0, 25.0, 20.0, 0.5, keying="hand")
    b = SignalSpec("CQ TEST W9XYZ", 3000.0, 22.0, 20.0, 0.5, keying="hand")
    other = SignalSpec("TU", 1000.0, 25.0, 20.0, 0.5, keying="paddle")
    first = plan_intervals([a, b], seed=7)
    again = plan_intervals([a, b], seed=7)
    assert first[1].intervals == again[1].intervals
    assert plan_intervals([other, b], seed=7)[1].intervals == first[1].intervals
    entry = labels([a, b], 8000, 10.0, seed=7)["signals"][0]
    assert entry["keying"] == "hand"
    assert entry["end_s"] == pytest.approx(0.5 + first[0].intervals[-1][1], abs=1e-3)
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `.venv\Scripts\python -m pytest training -q`
Expected: `ModuleNotFoundError: No module named 'kz4ap_synth.keying'`, and `TypeError: ... unexpected keyword argument 'keying'`.

- [ ] **Step 3: Implement**

Create `training/kz4ap_synth/keying.py`:

```python
"""Keying timing: how long each element and space lasts for a given sender.

Machine keying is exact PARIS timing. The other styles draw every duration
from a log-normal distribution around its nominal length, as VE3NEA's DeepCW
generator does. Only the hand-key dah (median e^1.5 = 4.48 dits, standard
deviation of ln(duration) 0.3) is VE3NEA's published value; every other
parameter here is this project's own (heuristic), pending his per-style table.
"""

from __future__ import annotations

import math
from dataclasses import dataclass

import numpy as np

from .morse import CODES, keying_intervals, symbols

MIN_DITS = 0.2  # no element or space is drawn shorter than this, dits


@dataclass(frozen=True)
class Duration:
    median_dits: float  # median duration, dits
    sigma: float        # standard deviation of ln(duration); 0 = exact


@dataclass(frozen=True)
class KeyingStyle:
    dit: Duration
    dah: Duration
    element_gap: Duration  # space inside a character
    char_gap: Duration     # space between characters
    word_gap: Duration     # space between words


STYLES = {
    # An electronic keyer or a computer: exact.
    "machine": KeyingStyle(Duration(1, 0), Duration(3, 0), Duration(1, 0), Duration(3, 0), Duration(7, 0)),
    # An iambic keyer: elements and element spaces exact, the operator spaces characters and words.
    "paddle": KeyingStyle(Duration(1, 0), Duration(3, 0), Duration(1, 0), Duration(3, 0.15), Duration(7, 0.25)),
    # A semi-automatic key: machine-made dits, hand-made dahs, somewhat heavy.
    "bug": KeyingStyle(Duration(1, 0.05), Duration(3.3, 0.15), Duration(1, 0.1), Duration(3, 0.2), Duration(7, 0.3)),
    # A straight key: everything by hand; dah from VE3NEA's generator.
    "hand": KeyingStyle(Duration(1, 0.2), Duration(math.exp(1.5), 0.3), Duration(1, 0.25), Duration(3, 0.3),
                        Duration(7, 0.3)),
}

SPEED_PROFILES = ("step", "ramp")


def word_wpm(index: int, count: int, wpm: float, wpm_end: float | None, profile: str) -> float:
    """Speed of word `index` of `count`."""
    if profile not in SPEED_PROFILES:
        raise ValueError(f"unknown speed profile {profile!r}")
    if wpm_end is None or count < 2:
        return wpm
    if profile == "step":
        return wpm if index < count // 2 else wpm_end
    return wpm + (wpm_end - wpm) * index / (count - 1)


def timed_intervals(text: str, wpm: float, style: str = "machine", rng: np.random.Generator | None = None,
                    wpm_end: float | None = None, profile: str = "step",
                    imbalance_dits: float = 0.0) -> list[tuple[float, float]]:
    """Key-down intervals (start_s, end_s) for text, from 0 s.

    wpm_end: the speed at the end (None = constant); profile "step" switches at
    the middle word, "ramp" changes linearly from word to word. imbalance_dits:
    every mark is longer, and every space shorter, by this many dits (a
    transmitter that keys on and off at different speeds). With machine keying,
    constant speed and no imbalance the result is exactly keying_intervals().
    """
    if style not in STYLES:
        raise ValueError(f"unknown keying style {style!r}")
    if profile not in SPEED_PROFILES:
        raise ValueError(f"unknown speed profile {profile!r}")
    if style == "machine" and wpm_end is None and imbalance_dits == 0.0:
        return keying_intervals(text, wpm)
    k = STYLES[style]
    if rng is None and any(d.sigma > 0 for d in (k.dit, k.dah, k.element_gap, k.char_gap, k.word_gap)):
        raise ValueError(f"keying style {style!r} needs a random generator")

    def length(d: Duration, dit_s: float, extra_dits: float) -> float:
        dits = d.median_dits if d.sigma == 0 else d.median_dits * math.exp(d.sigma * rng.standard_normal())
        return max(dits + extra_dits, MIN_DITS) * dit_s

    words = [[CODES[s] for s in symbols(w) if s in CODES] for w in text.upper().split()]
    words = [w for w in words if w]
    out: list[tuple[float, float]] = []
    t = 0.0
    for wi, patterns in enumerate(words):
        dit_s = 1.2 / word_wpm(wi, len(words), wpm, wpm_end, profile)
        for ci, pattern in enumerate(patterns):
            for ei, element in enumerate(pattern):
                mark = length(k.dit if element == "." else k.dah, dit_s, imbalance_dits)
                out.append((t, t + mark))
                t += mark
                if ei < len(pattern) - 1:
                    t += length(k.element_gap, dit_s, -imbalance_dits)
            if ci < len(patterns) - 1:
                t += length(k.char_gap, dit_s, -imbalance_dits)
        if wi < len(words) - 1:
            t += length(k.word_gap, dit_s, -imbalance_dits)
    return out
```

In `training/kz4ap_synth/generate.py`, add the import `from .keying import timed_intervals`, add these fields at the end of `SignalSpec`:

```python
    keying: str = "machine"          # keying style, a key of keying.STYLES
    wpm_end: float | None = None     # speed at the end of each sending; None = constant
    speed_profile: str = "step"      # "step" (at the middle word) or "ramp" (linear per word)
    imbalance_dits: float = 0.0      # marks longer and spaces shorter by this, dits
```

and replace `sending_intervals` with:

```python
def sending_intervals(spec: SignalSpec, rng: np.random.Generator) -> list[tuple[float, float]]:
    """Key-down intervals of one sending of spec.text, from 0 s."""
    return timed_intervals(spec.text, spec.wpm, spec.keying, rng, spec.wpm_end, spec.speed_profile,
                           spec.imbalance_dits)
```

- [ ] **Step 4: Run the tests to verify they pass**

Run: `.venv\Scripts\python -m pytest training -q`
Expected: all pass, including `test_default_signals_match_milestone_1_generator` (machine keying still goes through `keying_intervals`).

- [ ] **Step 5: Document the keying styles**

In `docs/signal-processing.md`, section 11, after the **Drift** bullet added in Task 1, insert:

```markdown
- **Keying styles** (synthetic recordings, `keying`): every element and
  space duration is drawn as median × exp(σ·N(0, 1)) dits, never below
  0.2 dit. Medians are the nominal 1 (dit), 3 (dah), 1 (element space),
  3 (character space) and 7 (word space) dits, except where stated:

  | Style | dit σ | dah median, σ | element space σ | character space σ | word space σ |
  |---|---|---|---|---|---|
  | machine | 0 | 3, 0 | 0 | 0 | 0 |
  | paddle | 0 | 3, 0 | 0 | 0.15 | 0.25 |
  | bug | 0.05 | 3.3, 0.15 | 0.1 | 0.2 | 0.3 |
  | hand | 0.2 | 4.48, 0.3 | 0.25 | 0.3 | 0.3 |

  The hand-key dah (median e^1.5 ≈ 4.48 dits, σ = 0.3) is VE3NEA's DeepCW
  value; all other values are **heuristic**. `imbalance_dits` lengthens
  every mark and shortens every space by that many dits. `wpm_end` changes
  the speed within a sending: `step` switches at the middle word, `ramp`
  changes linearly from word to word.
```

- [ ] **Step 6: Commit on the `milestone-2` branch**

```powershell
git add training/kz4ap_synth/keying.py training/kz4ap_synth/generate.py training/tests/test_keying.py training/tests/test_generate.py docs/signal-processing.md
```
```powershell
git commit -m "Add keying styles, key imbalance and speed changes to the generator"
```

---

### Task 3: Generator — Rayleigh fading

Flat Rayleigh fading (QSB) with a Doppler-spread parameter f_D, so VE3NEA's grid (f_D = 0.1–3 Hz; survey scenario B) can be reproduced as an external anchor.

**Model (derived from the definition; the spread convention is a choice):** the carrier is multiplied by a complex Gaussian gain g(t) with E|g|² = 1, whose Doppler power spectrum is Gaussian, S(f) ∝ exp(−f²/(2σ_D²)), with frequency spread f_D = 2σ_D (the Watterson / CCIR 520 HF convention). The process is synthesized at 50 samples/s by filtering white complex Gaussian noise in the frequency domain (amplitude response √S(f), scaled so the expected power is exactly 1), then linearly interpolated to the recording's rate. At 50 samples/s and f_D ≤ 3 Hz the spectrum is negligible beyond ±9 Hz (6σ_D), well inside ±25 Hz, and linear interpolation loses at most 0.04 dB of power between points (derived: adjacent-sample correlation ≥ 0.982 at f_D = 3 Hz). S₅₀₀ of a fading signal is its **mean** key-down power over the noise in 500 Hz.

**Files:**
- Create: `training/kz4ap_synth/fading.py`
- Modify: `training/kz4ap_synth/generate.py` (`SignalSpec`, `generate`)
- Test: `training/tests/test_fading.py` (new); `training/tests/test_generate.py`
- Modify: `docs/signal-processing.md` (§11)

**Interfaces:**
- Produces: `kz4ap_synth.fading.rayleigh_gain(n: int, sample_rate: int, spread_hz: float, rng: np.random.Generator) -> np.ndarray` (complex, length n); `FADING_RATE_HZ = 50.0`. `SignalSpec` gains `fading_hz: float = 0.0` (f_D, Hz; 0 = no fading). Signal i's fading draws from `np.random.default_rng([seed, i, 1])`.

- [ ] **Step 1: Write the failing tests**

Create `training/tests/test_fading.py`:

```python
import numpy as np
import pytest

from kz4ap_synth.fading import rayleigh_gain


def test_mean_power_is_one():
    g = rayleigh_gain(600 * 200, 200, 3.0, np.random.default_rng(1))
    assert np.mean(np.abs(g) ** 2) == pytest.approx(1.0, rel=0.1)


def test_power_is_exponentially_distributed():
    g = rayleigh_gain(600 * 200, 200, 3.0, np.random.default_rng(2))
    power = np.abs(g) ** 2 / np.mean(np.abs(g) ** 2)
    # Rayleigh envelope: P(|g|^2 < 0.1 mean) = 1 - exp(-0.1) = 0.095
    assert np.mean(power < 0.1) == pytest.approx(0.095, abs=0.02)


def test_correlation_time_matches_the_spread():
    fs = 200
    f_d = 1.0
    g = rayleigh_gain(1200 * fs, fs, f_d, np.random.default_rng(3))
    sigma = f_d / 2
    # Gaussian Doppler spectrum: correlation exp(-2 pi^2 sigma^2 tau^2) is 0.5 at this lag
    tau = np.sqrt(np.log(2) / (2 * np.pi**2 * sigma**2))
    lag = int(round(tau * fs))
    rho = np.vdot(g[:-lag], g[lag:]) / np.vdot(g, g)
    assert abs(rho) == pytest.approx(0.5, abs=0.1)


def test_same_generator_state_gives_the_same_fading():
    a = rayleigh_gain(1000, 100, 1.0, np.random.default_rng(4))
    b = rayleigh_gain(1000, 100, 1.0, np.random.default_rng(4))
    assert np.array_equal(a, b)
```

Append to `training/tests/test_generate.py`:

```python
def test_faded_signal_keeps_its_mean_key_down_power():
    fs = 2000
    still = SignalSpec("E", 300.0, 5.0, 20.0, 0.0, tune_s=300.0)
    faded = SignalSpec("E", -300.0, 5.0, 20.0, 0.0, tune_s=300.0, fading_hz=3.0)
    iq = generate([still, faded], fs, 302.0, seed=1, add_noise=False)
    t = np.arange(len(iq)) / fs
    kernel = np.ones(40) / 40  # 20 ms boxcar: its nulls at multiples of 50 Hz remove the other carrier, 600 Hz away

    def power_at(freq):
        base = np.convolve(iq * np.exp(-2j * np.pi * freq * t), kernel, mode="valid")
        return np.abs(base[fs:299 * fs]) ** 2  # inside the 300 s carriers

    a2 = amplitude_for_snr(20.0, fs) ** 2
    assert np.mean(power_at(300.0)) == pytest.approx(a2, rel=0.01)
    assert np.mean(power_at(-300.0)) == pytest.approx(a2, rel=0.15)
    assert np.std(power_at(-300.0)) > 0.5 * a2  # exponentially distributed power: std = mean
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `.venv\Scripts\python -m pytest training -q`
Expected: `ModuleNotFoundError: No module named 'kz4ap_synth.fading'`, and `TypeError: ... unexpected keyword argument 'fading_hz'`.

- [ ] **Step 3: Implement**

Create `training/kz4ap_synth/fading.py`:

```python
"""Flat Rayleigh fading (QSB) for synthetic signals.

The gain g(t) is complex Gaussian with E|g|^2 = 1 and a Gaussian Doppler power
spectrum S(f) ~ exp(-f^2 / (2 sigma^2)). The frequency spread is 2 sigma (the
Watterson / CCIR 520 HF convention), so spread_hz = f_D = 2 sigma.
"""

from __future__ import annotations

import numpy as np

FADING_RATE_HZ = 50.0  # rate the fading process is synthesized at before interpolation, samples/s


def rayleigh_gain(n: int, sample_rate: int, spread_hz: float, rng: np.random.Generator) -> np.ndarray:
    """n samples of complex fading gain at sample_rate, frequency spread spread_hz (Hz)."""
    if spread_hz <= 0:
        raise ValueError("spread_hz must be positive")
    m = int(np.ceil(n / sample_rate * FADING_RATE_HZ)) + 2
    freqs = np.fft.fftfreq(m, 1.0 / FADING_RATE_HZ)
    sigma = spread_hz / 2.0
    shape = np.exp(-freqs**2 / (4.0 * sigma**2))  # amplitude response: sqrt of the Gaussian power spectrum
    shape *= np.sqrt(m / np.sum(shape**2))        # expected power of the output is exactly 1
    white = (rng.standard_normal(m) + 1j * rng.standard_normal(m)) / np.sqrt(2.0)
    slow = np.fft.ifft(white * shape, norm="ortho")
    t_slow = np.arange(m) / FADING_RATE_HZ
    t = np.arange(n) / sample_rate
    return np.interp(t, t_slow, slow.real) + 1j * np.interp(t, t_slow, slow.imag)
```

In `training/kz4ap_synth/generate.py`, add `from .fading import rayleigh_gain`, add the field at the end of `SignalSpec`:

```python
    fading_hz: float = 0.0           # Rayleigh fading frequency spread f_D (2 sigma), Hz; 0 = none
```

and in `generate`, change the loop header and the last line of the loop body to:

```python
    for index, (s, plan) in enumerate(zip(signals, plan_intervals(signals, seed))):
```
```python
        signal = amplitude_for_snr(s.snr_db, sample_rate) * env * np.exp(1j * angle)
        if s.fading_hz > 0:
            signal = signal * rayleigh_gain(i1 - i0, sample_rate, s.fading_hz,
                                            np.random.default_rng([seed, index, 1]))
        iq[i0:i1] += signal
```

(The unfaded expression is unchanged, so `test_default_signals_match_milestone_1_generator` still holds.)

- [ ] **Step 4: Run the tests to verify they pass**

Run: `.venv\Scripts\python -m pytest training -q`
Expected: all pass.

- [ ] **Step 5: Document the fading model**

In `docs/signal-processing.md`, section 11, change the **SNR** bullet's first sentence to "key-down carrier power A² (for a fading signal, its mean) over the noise power in a **500 Hz** bandwidth, σ²·500 Hz / fs." and, after the **Keying styles** bullet, insert:

```markdown
- **Fading** (synthetic recordings, `fading_hz`): flat Rayleigh fading. The
  carrier is multiplied by a complex Gaussian gain g(t) with E|g|² = 1 and a
  Gaussian Doppler power spectrum; `fading_hz` is the frequency spread
  f_D = 2σ of that spectrum, Hz (the Watterson / CCIR 520 HF convention;
  whether VE3NEA's f_D uses the same convention is not established). The
  gain is synthesized at 50 samples/s and linearly interpolated (at most
  0.04 dB of power lost between points at f_D = 3 Hz, derived).
```

- [ ] **Step 6: Commit on the `milestone-2` branch**

```powershell
git add training/kz4ap_synth/fading.py training/kz4ap_synth/generate.py training/tests/test_fading.py training/tests/test_generate.py docs/signal-processing.md
```
```powershell
git commit -m "Add Rayleigh fading with a Doppler-spread setting to the generator"
```

---

### Task 4: Generator — band-scenario settings, interferers, tags

Crowded bands need a minimum station spacing that can go down to zero and a narrower span; strong signals need S₅₀₀ up to 60 dB; the speed range must reach 10–60 WPM. Interferers are labeled signals that are not scored. A `tag` names each signal's condition for the suite summaries (Task 7).

**Files:**
- Modify: `training/kz4ap_synth/generate.py` (`SignalSpec`, `scenario_band`, new `with_interferer`, `main`)
- Test: `training/tests/test_generate.py`
- Modify: `docs/signal-processing.md` (§11)

**Interfaces:**
- Produces (Python, `kz4ap_synth.generate`):
  - `SignalSpec` gains `score: bool = True` (False: an interferer, excluded from the score) and `tag: str = ""` (the condition this signal represents, for summaries).
  - `scenario_band(rng, count, duration_s, sample_rate, min_spacing_hz: float = 1000.0, span_hz: float | None = None, wpm_range: tuple[float, float] = (18.0, 36.0), snr_range: tuple[float, float] = (10.0, 30.0)) -> list[SignalSpec]` (span_hz: stations lie within ±span_hz; None = ±0.4·sample_rate).
  - `with_interferer(wanted: SignalSpec, offset_hz: float, relative_db: float, wpm: float, text: str) -> list[SignalSpec]`: `[wanted, interferer]`, the interferer `offset_hz` from the wanted carrier, `relative_db` dB stronger in key-down power (S₅₀₀ = wanted's + relative_db), same start, `score=False`, same tag.
  - CLI options `--min-spacing HZ` (default 1000), `--span HZ` (default 0.4 × sample rate), `--wpm-min`/`--wpm-max` (default 18/36), `--snr-min`/`--snr-max` (dB S₅₀₀, default 10/30).

- [ ] **Step 1: Write the failing tests**

Append to `training/tests/test_generate.py` (and add `with_interferer`, `MESSAGES`, `fill_text`, `random_callsign` to the import list):

```python
def _milestone1_scenario_band(rng, count, duration_s, sample_rate):
    """Frozen copy of milestone 1's band scenario."""
    span = 0.4 * sample_rate
    specs = []
    for _attempt in range(1000 * count):
        if len(specs) >= count:
            break
        freq = round(float(rng.uniform(-span, span)), 1)
        if any(abs(freq - s.freq_offset_hz) < 1000 for s in specs):
            continue
        wpm = round(float(rng.uniform(18, 36)), 1)
        snr = round(float(rng.uniform(10, 30)), 1)
        start = round(float(rng.uniform(0, 2)), 3)
        message = MESSAGES[rng.integers(len(MESSAGES))].format(c=random_callsign(rng))
        text = fill_text(message, wpm, duration_s - start - 1.0)
        specs.append(SignalSpec(text, freq, wpm, snr, start))
    return specs


def test_band_defaults_match_milestone_1():
    assert scenario_band(np.random.default_rng(1), 8, 30.0, 192000) == \
        _milestone1_scenario_band(np.random.default_rng(1), 8, 30.0, 192000)


def test_band_spacing_can_go_down_to_zero():
    specs = scenario_band(np.random.default_rng(2), 30, 20.0, 48000, min_spacing_hz=0.0, span_hz=500.0)
    freqs = sorted(s.freq_offset_hz for s in specs)
    assert len(freqs) == 30
    assert all(-500.0 <= f <= 500.0 for f in freqs)
    assert min(b - a for a, b in zip(freqs, freqs[1:])) < 50.0


def test_band_spacing_is_respected():
    specs = scenario_band(np.random.default_rng(3), 20, 20.0, 48000, min_spacing_hz=200.0, span_hz=5000.0)
    freqs = sorted(s.freq_offset_hz for s in specs)
    assert min(b - a for a, b in zip(freqs, freqs[1:])) >= 200.0


def test_band_speed_and_snr_ranges():
    specs = scenario_band(np.random.default_rng(4), 12, 30.0, 48000, min_spacing_hz=500.0,
                          wpm_range=(10.0, 60.0), snr_range=(30.0, 60.0))
    assert all(10.0 <= s.wpm <= 60.0 for s in specs)
    assert all(30.0 <= s.snr_db <= 60.0 for s in specs)
    assert max(s.wpm for s in specs) > 40.0


def test_interferer_is_offset_stronger_and_unscored():
    wanted = SignalSpec("CQ K1ABC", 1000.0, 25.0, 10.0, 0.5, tag="qrm")
    pair = with_interferer(wanted, 100.0, 10.0, 30.0, "TU W9XYZ")
    assert pair[0] is wanted
    assert pair[1].freq_offset_hz == 1100.0
    assert pair[1].snr_db == 20.0
    assert pair[1].wpm == 30.0
    assert pair[1].score is False
    assert pair[1].tag == "qrm"


def test_cli_band_settings_reach_the_labels(tmp_path):
    out = tmp_path / "band.wav"
    main(["--scenario", "band", "--signals", "5", "--sample-rate", "48000", "--duration", "20",
          "--min-spacing", "100", "--span", "3000", "--wpm-min", "10", "--wpm-max", "60",
          "--snr-min", "30", "--snr-max", "60", "--out", str(out)])
    signals = json.loads(out.with_suffix(".json").read_text())["signals"]
    assert len(signals) == 5
    for s in signals:
        assert -3000.0 <= s["freq_offset_hz"] <= 3000.0
        assert 10.0 <= s["wpm"] <= 60.0
        assert 30.0 <= s["snr_db"] <= 60.0
        assert s["score"] is True
        assert s["tag"] == ""
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `.venv\Scripts\python -m pytest training/tests/test_generate.py -q`
Expected: `ImportError: cannot import name 'with_interferer'`.

- [ ] **Step 3: Implement**

In `training/kz4ap_synth/generate.py`, add the fields at the end of `SignalSpec`:

```python
    score: bool = True               # False: an interferer, left out of the score
    tag: str = ""                    # the condition this signal represents, for summaries
```

Replace `scenario_band` with:

```python
def scenario_band(rng, count: int, duration_s: float, sample_rate: int, min_spacing_hz: float = 1000.0,
                  span_hz: float | None = None, wpm_range: tuple[float, float] = (18.0, 36.0),
                  snr_range: tuple[float, float] = (10.0, 30.0)) -> list[SignalSpec]:
    """count signals within +/-span_hz (default: 80% of the recording's span), at least
    min_spacing_hz apart (0 allows any overlap), with speeds and S500 drawn uniformly
    from the given ranges."""
    span = 0.4 * sample_rate if span_hz is None else span_hz
    specs: list[SignalSpec] = []
    max_attempts = 1000 * count
    for _attempt in range(max_attempts):
        if len(specs) >= count:
            break
        freq = round(float(rng.uniform(-span, span)), 1)
        if any(abs(freq - s.freq_offset_hz) < min_spacing_hz for s in specs):
            continue
        wpm = round(float(rng.uniform(*wpm_range)), 1)
        snr = round(float(rng.uniform(*snr_range)), 1)
        start = round(float(rng.uniform(0, 2)), 3)
        message = MESSAGES[rng.integers(len(MESSAGES))].format(c=random_callsign(rng))
        text = fill_text(message, wpm, duration_s - start - 1.0)
        specs.append(SignalSpec(text, freq, wpm, snr, start))
    if len(specs) < count:
        raise ValueError(
            f"cannot place {count} signals at least {min_spacing_hz:.0f} Hz apart within +/-{span:.0f} Hz"
        )
    return specs


def with_interferer(wanted: SignalSpec, offset_hz: float, relative_db: float, wpm: float,
                    text: str) -> list[SignalSpec]:
    """The wanted signal plus an unscored interferer offset_hz from it, relative_db dB
    stronger in key-down power, at its own speed, starting at the same time."""
    interferer = SignalSpec(text, wanted.freq_offset_hz + offset_hz, wpm, wanted.snr_db + relative_db,
                            wanted.start_s, score=False, tag=wanted.tag)
    return [wanted, interferer]
```

In `main`, add after the `--seed` argument:

```python
    parser.add_argument("--min-spacing", type=float, default=1000.0,
                        help="band: minimum spacing between stations, Hz (0 allows overlap)")
    parser.add_argument("--span", type=float, default=None,
                        help="band: stations lie within +/- this many Hz (default: 0.4 x sample rate)")
    parser.add_argument("--wpm-min", type=float, default=18.0)
    parser.add_argument("--wpm-max", type=float, default=36.0)
    parser.add_argument("--snr-min", type=float, default=10.0, help="band: lowest S500, dB")
    parser.add_argument("--snr-max", type=float, default=30.0, help="band: highest S500, dB")
```

and change the `scenario_band` call to:

```python
            specs = scenario_band(rng, args.signals, args.duration, args.sample_rate,
                                  min_spacing_hz=args.min_spacing, span_hz=args.span,
                                  wpm_range=(args.wpm_min, args.wpm_max),
                                  snr_range=(args.snr_min, args.snr_max))
```

- [ ] **Step 4: Run the tests to verify they pass**

Run: `.venv\Scripts\python -m pytest training -q`
Expected: all pass. (`rng.uniform(*(18.0, 36.0))` draws exactly what `rng.uniform(18, 36)` drew, so the defaults are unchanged; `test_band_defaults_match_milestone_1` pins it.)

- [ ] **Step 5: Confirm the smoke check is unaffected**

In Git Bash: `bash bench/smoke.sh build/windows`
Expected: `smoke test passed` with the same CER line as before.

- [ ] **Step 6: Document**

In `docs/signal-processing.md`, section 11, after the **Fading** bullet, insert:

```markdown
- **Interferers and tags:** a labeled signal with `score: false` is an
  interferer. The benchmark matches it to its track (so that track is not
  counted as a false track) but leaves it out of every error rate and of
  detection recall. `tag` names the condition a signal represents
  (for example `df 100 Hz rel +10 dB`), and suite summaries group by it.
```

- [ ] **Step 7: Commit on the `milestone-2` branch**

```powershell
git add training/kz4ap_synth/generate.py training/tests/test_generate.py docs/signal-processing.md
```
```powershell
git commit -m "Add band spacing, span, speed and SNR settings and unscored interferers to the generator"
```

---

### Task 5: Bench — word spaces and first words scored separately

Spec §5.4 asks for word spaces to be scored separately from characters, and the backlog for each transmission's first word, where wrong characters concentrate today. One minimum-edit alignment gives all three; the total edit count stays exactly the Levenshtein distance, so the existing CER (and the smoke baseline) keep their meaning.

**Definitions (this task writes them into signal-processing.md §11):**
- The alignment is a minimum-edit (Levenshtein) alignment of decoded against reference symbols. Ties are broken, walking back from the end, in the order: match or substitution, deletion, insertion (fixed, so results are deterministic).
- Each edit is **charged** to one reference symbol: a substitution or deletion to its own symbol; an insertion to the reference symbol it precedes, or to the last reference symbol if it comes after the end.
- An edit is a **space edit** if the reference symbol or the decoded symbol it involves is a word space; otherwise it is a **character edit**.
- Character CER = character edits / reference symbols that are not word spaces. Space error rate = space edits / reference word spaces. First-word CER = edits charged to the first word of each transmission / the symbols of those words. Each aggregate is summed over scored signals before dividing.

**Files:**
- Modify: `bench/src/labels.hpp`, `bench/src/labels.cpp`
- Modify: `bench/src/scoring.hpp`, `bench/src/scoring.cpp`
- Modify: `bench/src/main.cpp`
- Test: `bench/tests/labels_test.cpp`, `bench/tests/scoring_test.cpp`
- Modify: `docs/signal-processing.md` (§11)

**Interfaces:**
- Consumes: the labels file of Tasks 1 and 4 (`transmissions`, `score`).
- Produces (C++, namespace `kz4ap::bench`):
  - `struct Transmission { std::string text; double start_s; double end_s; };`
  - `LabeledSignal` gains `std::vector<Transmission> transmissions;` (empty: the whole text is one transmission) and `bool score = true;`.
  - `struct EditCounts { std::size_t char_edits = 0; std::size_t space_edits = 0; std::size_t total() const; };`
  - `struct Alignment { EditCounts counts; std::vector<std::size_t> charged; };`
  - `Alignment align(const std::vector<std::string_view>& reference, const std::vector<std::string_view>& decoded);`
  - `std::vector<std::pair<std::size_t, std::size_t>> first_word_ranges(const LabeledSignal& label);` — `[first, last)` reference-symbol ranges; throws `std::runtime_error` if the transmissions do not add up to the text.
  - `SignalScore` gains `symbols, chars, spaces, char_edits, space_edits, first_word_symbols, first_word_edits` (all `std::size_t`, default 0).
  - `Score` gains `double char_cer, space_error_rate, first_word_cer;` and `std::size_t scored;` (signals with `score = true`); `detected` now counts scored signals only.
  - `kz4ap-bench --json` output: `score` gains `char_cer`, `space_error_rate`, `first_word_cer`, `scored`; each entry of `score.signals` gains `index`, `snr_db`, `wpm`, `scored`, `symbols`, `edits`, `chars`, `char_edits`, `spaces`, `space_edits`, `first_word_symbols`, `first_word_edits`. Detection recall = detected / scored.

- [ ] **Step 1: Write the failing tests**

Append to `bench/tests/labels_test.cpp`:

```cpp
TEST(Labels, ParsesTransmissionsAndScoreFlag) {
    const auto labels = parse_labels(R"({
      "sample_rate": 48000, "duration_s": 30.0,
      "signals": [{"text": "CQ K1ABC CQ K1ABC", "freq_offset_hz": 100.0, "wpm": 25.0, "snr_db": 15.0,
                   "start_s": 0.5, "end_s": 12.0, "score": false,
                   "transmissions": [{"text": "CQ K1ABC", "start_s": 0.5, "end_s": 3.0},
                                     {"text": "CQ K1ABC", "start_s": 9.5, "end_s": 12.0}]}]})");
    const auto& s = labels.signals.at(0);
    EXPECT_FALSE(s.score);
    ASSERT_EQ(s.transmissions.size(), 2u);
    EXPECT_EQ(s.transmissions[1].text, "CQ K1ABC");
    EXPECT_DOUBLE_EQ(s.transmissions[1].start_s, 9.5);
}

TEST(Labels, OlderFilesDefaultToOneScoredTransmission) {
    const auto labels = parse_labels(R"({"sample_rate": 48000, "duration_s": 30.0,
      "signals": [{"text": "CQ", "freq_offset_hz": 0.0, "wpm": 25.0, "snr_db": 15.0,
                   "start_s": 0.5, "end_s": 2.0}]})");
    EXPECT_TRUE(labels.signals.at(0).score);
    EXPECT_TRUE(labels.signals.at(0).transmissions.empty());
}
```

Append to `bench/tests/scoring_test.cpp` (add `#include <stdexcept>`, `#include <string>`, `#include <utility>`, `#include <vector>` at the top):

```cpp
namespace {

LabeledSignal make_label(std::string text, double freq, std::vector<Transmission> transmissions = {},
                         bool scored = true) {
    LabeledSignal l{std::move(text), freq, 25, 20, 0, 5};
    l.transmissions = std::move(transmissions);
    l.score = scored;
    return l;
}

Alignment align_text(std::string_view reference, std::string_view decoded) {
    return align(kz4ap::morse::symbols(reference), kz4ap::morse::symbols(decoded));
}

}  // namespace

TEST(Scoring, AlignmentTotalEqualsEditDistance) {
    for (const auto& [a, b] : std::vector<std::pair<std::string, std::string>>{
             {"KITTEN", "SITTING"}, {"CQ TEST K1ABC", "CQTEST K1AB"}, {"", "ABC"}, {"ABC", ""}, {"TU", "TU"}}) {
        EXPECT_EQ(align_text(a, b).counts.total(), edit_distance(a, b)) << a << " / " << b;
    }
}

TEST(Scoring, MissingWordSpaceIsASpaceEdit) {
    const auto a = align_text("CQ TEST", "CQTEST");
    EXPECT_EQ(a.counts.space_edits, 1u);
    EXPECT_EQ(a.counts.char_edits, 0u);
}

TEST(Scoring, WrongLetterIsACharacterEdit) {
    const auto a = align_text("CQ", "RQ");
    EXPECT_EQ(a.counts.char_edits, 1u);
    EXPECT_EQ(a.counts.space_edits, 0u);
    EXPECT_EQ(a.charged, (std::vector<std::size_t>{1, 0}));
}

TEST(Scoring, InsertionIsChargedToTheFollowingReferenceSymbol) {
    EXPECT_EQ(align_text("AB", "AXB").charged, (std::vector<std::size_t>{0, 1}));
    EXPECT_EQ(align_text("AB", "ABX").charged, (std::vector<std::size_t>{0, 1}));
    EXPECT_EQ(align_text("AB", "XAB").charged, (std::vector<std::size_t>{1, 0}));
}

TEST(Scoring, FirstWordOfEachTransmissionIsScored) {
    const auto label = make_label("CQ K1ABC CQ K1ABC", 1000.0, {{"CQ K1ABC", 0.5, 3.0}, {"CQ K1ABC", 9.0, 12.0}});
    EXPECT_EQ(first_word_ranges(label),
              (std::vector<std::pair<std::size_t, std::size_t>>{{0, 2}, {9, 11}}));
    const auto s = score({label}, {{1, 1000.0, "RQ K1ABC CQ K1ABC"}});
    EXPECT_EQ(s.signals[0].first_word_symbols, 4u);
    EXPECT_EQ(s.signals[0].first_word_edits, 1u);
    EXPECT_DOUBLE_EQ(s.first_word_cer, 0.25);
}

TEST(Scoring, WithoutTransmissionsTheWholeTextIsOneTransmission) {
    EXPECT_EQ(first_word_ranges(make_label("CQ TEST", 0.0)),
              (std::vector<std::pair<std::size_t, std::size_t>>{{0, 2}}));
}

TEST(Scoring, TransmissionsThatDoNotAddUpThrow) {
    EXPECT_THROW(first_word_ranges(make_label("CQ TEST", 0.0, {{"CQ", 0.0, 1.0}})), std::runtime_error);
}

TEST(Scoring, CharacterAndSpaceRatesAreSeparate) {
    const auto s = score({make_label("CQ TEST K1ABC", 1000.0)}, {{1, 1000.0, "CQTEST K1ABD"}});
    EXPECT_EQ(s.signals[0].chars, 11u);
    EXPECT_EQ(s.signals[0].spaces, 2u);
    EXPECT_DOUBLE_EQ(s.char_cer, 1.0 / 11.0);
    EXPECT_DOUBLE_EQ(s.space_error_rate, 0.5);
    EXPECT_DOUBLE_EQ(s.cer, 2.0 / 13.0);
}

TEST(Scoring, UnscoredSignalIsMatchedButNotCounted) {
    const auto s = score({make_label("CQ", 1000.0), make_label("TU", 1100.0, {}, false)},
                         {{1, 1000.0, "CQ"}, {2, 1100.0, "EEE"}});
    EXPECT_EQ(s.scored, 1u);
    EXPECT_EQ(s.detected, 1u);
    EXPECT_EQ(s.false_tracks, 0u);
    EXPECT_DOUBLE_EQ(s.cer, 0.0);
    EXPECT_EQ(s.signals[1].track_id, 2u);
}
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `cmake --build --preset windows`
Expected: compile errors (`Transmission`, `align`, `first_word_ranges` not declared).

- [ ] **Step 3: Implement labels**

In `bench/src/labels.hpp`, before `LabeledSignal`, add:

```cpp
struct Transmission {
    std::string text;
    double start_s;
    double end_s;
};
```

and add to the end of `LabeledSignal`:

```cpp
    std::vector<Transmission> transmissions;  // empty in older labels files: the whole text is one transmission
    bool score = true;                        // false: an interferer, left out of every error rate
```

In `bench/src/labels.cpp`, replace the loop over `j.at("signals")` with:

```cpp
        for (const auto& s : j.at("signals")) {
            LabeledSignal signal{s.at("text").get<std::string>(), s.at("freq_offset_hz").get<double>(),
                                 s.at("wpm").get<double>(),       s.at("snr_db").get<double>(),
                                 s.at("start_s").get<double>(),   s.at("end_s").get<double>()};
            if (const auto t = s.find("transmissions"); t != s.end()) {
                for (const auto& tx : *t) {
                    signal.transmissions.push_back({tx.at("text").get<std::string>(), tx.at("start_s").get<double>(),
                                                    tx.at("end_s").get<double>()});
                }
            }
            signal.score = s.value("score", true);
            labels.signals.push_back(std::move(signal));
        }
```

(add `#include <utility>`).

- [ ] **Step 4: Implement scoring**

In `bench/src/scoring.hpp`, add `#include <utility>`, extend `SignalScore` and `Score`, and declare the new functions:

```cpp
struct SignalScore {
    LabeledSignal label;
    std::optional<std::uint32_t> track_id;  // empty if no track matched
    std::string decoded;                    // normalized
    std::size_t edits;                      // symbol edits (a prosign counts as one)
    double cer;
    std::size_t symbols = 0;                // reference symbols
    std::size_t chars = 0;                  // reference symbols that are not word spaces
    std::size_t spaces = 0;                 // reference word spaces
    std::size_t char_edits = 0;
    std::size_t space_edits = 0;
    std::size_t first_word_symbols = 0;     // symbols in the first word of each transmission
    std::size_t first_word_edits = 0;       // edits charged to those symbols
};

struct Score {
    std::vector<SignalScore> signals;
    double cer = 0;                // total edits / total reference symbols (scored signals)
    double char_cer = 0;           // character edits / reference characters
    double space_error_rate = 0;   // word-space edits / reference word spaces
    double first_word_cer = 0;     // edits charged to first words / their symbols
    std::size_t scored = 0;        // labeled signals with score = true
    std::size_t detected = 0;      // scored signals matched to a track
    std::size_t false_tracks = 0;  // unmatched tracks that decoded some text
};

struct EditCounts {
    std::size_t char_edits = 0;   // edits in which neither symbol is a word space
    std::size_t space_edits = 0;  // edits in which the reference or the decoded symbol is a word space
    std::size_t total() const { return char_edits + space_edits; }
};

struct Alignment {
    EditCounts counts;
    std::vector<std::size_t> charged;  // edits charged to each reference symbol
};

// Minimum-edit alignment of decoded against reference symbols. Walking back
// from the end, ties prefer a match or substitution, then a deletion, then an
// insertion. A substitution or deletion is charged to its reference symbol; an
// insertion to the reference symbol it precedes (the last one if it comes
// after the end).
Alignment align(const std::vector<std::string_view>& reference, const std::vector<std::string_view>& decoded);

// [first, last) reference-symbol ranges of the first word of each transmission
// (the whole text is one transmission if the label lists none). Throws
// std::runtime_error if the transmissions do not add up to the label's text.
std::vector<std::pair<std::size_t, std::size_t>> first_word_ranges(const LabeledSignal& label);
```

In `bench/src/scoring.cpp` add `#include <cstdint>`, `#include <stdexcept>`, and implement, below `edit_distance`:

```cpp
Alignment align(const std::vector<std::string_view>& reference, const std::vector<std::string_view>& decoded) {
    const std::size_t n = reference.size();
    const std::size_t m = decoded.size();
    std::vector<std::uint32_t> d((n + 1) * (m + 1));
    const auto at = [&](std::size_t i, std::size_t j) -> std::uint32_t& { return d[i * (m + 1) + j]; };
    for (std::size_t i = 0; i <= n; ++i) at(i, 0) = static_cast<std::uint32_t>(i);
    for (std::size_t j = 0; j <= m; ++j) at(0, j) = static_cast<std::uint32_t>(j);
    for (std::size_t i = 1; i <= n; ++i) {
        for (std::size_t j = 1; j <= m; ++j) {
            const std::uint32_t substitute = at(i - 1, j - 1) + (reference[i - 1] == decoded[j - 1] ? 0u : 1u);
            at(i, j) = std::min({at(i - 1, j) + 1, at(i, j - 1) + 1, substitute});
        }
    }

    Alignment result;
    result.charged.assign(n, 0);
    const auto is_space = [](std::string_view s) { return s == " "; };
    const auto charge = [&](std::size_t ref_index, bool space) {
        if (space) ++result.counts.space_edits;
        else ++result.counts.char_edits;
        if (n > 0) ++result.charged[std::min(ref_index, n - 1)];
    };
    std::size_t i = n;
    std::size_t j = m;
    while (i > 0 || j > 0) {
        if (i > 0 && j > 0) {
            const bool same = reference[i - 1] == decoded[j - 1];
            if (at(i, j) == at(i - 1, j - 1) + (same ? 0u : 1u)) {
                if (!same) charge(i - 1, is_space(reference[i - 1]) || is_space(decoded[j - 1]));
                --i;
                --j;
                continue;
            }
        }
        if (i > 0 && at(i, j) == at(i - 1, j) + 1) {  // reference[i-1] was dropped
            charge(i - 1, is_space(reference[i - 1]));
            --i;
            continue;
        }
        charge(i, is_space(decoded[j - 1]));  // decoded[j-1] was inserted before reference[i]
        --j;
    }
    return result;
}

std::vector<std::pair<std::size_t, std::size_t>> first_word_ranges(const LabeledSignal& label) {
    const std::string reference = normalize_text(label.text);
    const std::size_t total = kz4ap::morse::symbols(reference).size();
    std::vector<std::string> texts;
    for (const auto& t : label.transmissions) texts.push_back(normalize_text(t.text));
    if (texts.empty()) texts.push_back(reference);
    std::vector<std::pair<std::size_t, std::size_t>> ranges;
    std::size_t pos = 0;
    for (std::size_t k = 0; k < texts.size(); ++k) {
        const std::string first_word = texts[k].substr(0, texts[k].find(' '));
        ranges.emplace_back(pos, pos + kz4ap::morse::symbols(first_word).size());
        pos += kz4ap::morse::symbols(texts[k]).size();
        if (k + 1 < texts.size()) ++pos;  // the word space between transmissions
    }
    if (pos != total) throw std::runtime_error("labels: transmissions do not add up to the signal's text");
    return ranges;
}
```

Replace `score` with:

```cpp
namespace {

double ratio(std::size_t num, std::size_t den) {
    return den == 0 ? 0.0 : static_cast<double>(num) / static_cast<double>(den);
}

}  // namespace

Score score(const std::vector<LabeledSignal>& labels, const std::vector<DecodedTrack>& tracks,
            double match_tolerance_hz) {
    Score result;
    std::set<std::uint32_t> used;
    SignalScore totals{LabeledSignal{}, std::nullopt, "", 0, 0.0};
    for (const auto& label : labels) {
        const std::string reference = normalize_text(label.text);
        const DecodedTrack* best = nullptr;
        std::size_t best_len = 0;
        for (const auto& t : tracks) {
            if (used.count(t.id) || std::abs(t.freq_hz - label.freq_offset_hz) > match_tolerance_hz) continue;
            const std::size_t len = normalize_text(t.text).size();
            if (!best || len > best_len ||
                (len == best_len && std::abs(t.freq_hz - label.freq_offset_hz) <
                                        std::abs(best->freq_hz - label.freq_offset_hz))) {
                best = &t;
                best_len = len;
            }
        }
        SignalScore s{label, std::nullopt, "", 0, 0.0};
        if (best) {
            used.insert(best->id);
            s.track_id = best->id;
            s.decoded = normalize_text(best->text);
            if (label.score) ++result.detected;
        }
        const auto reference_symbols = kz4ap::morse::symbols(reference);
        const auto decoded_symbols = kz4ap::morse::symbols(s.decoded);
        const Alignment a = align(reference_symbols, decoded_symbols);
        s.edits = a.counts.total();
        s.symbols = reference_symbols.size();
        s.spaces = static_cast<std::size_t>(
            std::count(reference_symbols.begin(), reference_symbols.end(), std::string_view(" ")));
        s.chars = s.symbols - s.spaces;
        s.char_edits = a.counts.char_edits;
        s.space_edits = a.counts.space_edits;
        for (const auto& [first, last] : first_word_ranges(label)) {
            s.first_word_symbols += last - first;
            for (std::size_t i = first; i < last && i < a.charged.size(); ++i) s.first_word_edits += a.charged[i];
        }
        s.cer = ratio(s.edits, s.symbols);
        if (label.score) {
            ++result.scored;
            totals.edits += s.edits;
            totals.symbols += s.symbols;
            totals.char_edits += s.char_edits;
            totals.chars += s.chars;
            totals.space_edits += s.space_edits;
            totals.spaces += s.spaces;
            totals.first_word_edits += s.first_word_edits;
            totals.first_word_symbols += s.first_word_symbols;
        }
        result.signals.push_back(std::move(s));
    }
    result.cer = ratio(totals.edits, totals.symbols);
    result.char_cer = ratio(totals.char_edits, totals.chars);
    result.space_error_rate = ratio(totals.space_edits, totals.spaces);
    result.first_word_cer = ratio(totals.first_word_edits, totals.first_word_symbols);
    for (const auto& t : tracks) {
        if (!used.count(t.id) && !normalize_text(t.text).empty()) ++result.false_tracks;
    }
    return result;
}
```

(The matching loop is unchanged from milestone 1.)

- [ ] **Step 5: Report the new numbers in `kz4ap-bench`**

In `bench/src/main.cpp`, replace the block from `const Score s = score(labels.signals, track_list);` down to the `std::printf("CER %.4f, …` statement with:

```cpp
            const Score s = score(labels.signals, track_list);
            const double recall =
                s.scored == 0 ? 1.0 : static_cast<double>(s.detected) / static_cast<double>(s.scored);
            nlohmann::json signals = nlohmann::json::array();
            for (std::size_t i = 0; i < s.signals.size(); ++i) {
                const auto& sig = s.signals[i];
                signals.push_back({{"index", i},
                                   {"freq_offset_hz", sig.label.freq_offset_hz},
                                   {"snr_db", sig.label.snr_db},
                                   {"wpm", sig.label.wpm},
                                   {"scored", sig.label.score},
                                   {"reference", normalize_text(sig.label.text)},
                                   {"decoded", sig.decoded},
                                   {"track_id", sig.track_id ? nlohmann::json(*sig.track_id) : nlohmann::json()},
                                   {"cer", sig.cer},
                                   {"symbols", sig.symbols},
                                   {"edits", sig.edits},
                                   {"chars", sig.chars},
                                   {"char_edits", sig.char_edits},
                                   {"spaces", sig.spaces},
                                   {"space_edits", sig.space_edits},
                                   {"first_word_symbols", sig.first_word_symbols},
                                   {"first_word_edits", sig.first_word_edits}});
                std::printf("label %+10.1f Hz  CER %5.3f  %s%s\n", sig.label.freq_offset_hz, sig.cer,
                            sig.track_id ? "" : "(not detected)", sig.label.score ? "" : " (not scored)");
            }
            out["score"] = {{"cer", s.cer},
                            {"char_cer", s.char_cer},
                            {"space_error_rate", s.space_error_rate},
                            {"first_word_cer", s.first_word_cer},
                            {"detected", s.detected},
                            {"labels", labels.signals.size()},
                            {"scored", s.scored},
                            {"detection_recall", recall},
                            {"false_tracks", s.false_tracks},
                            {"signals", signals}};
            std::printf("CER %.4f (characters %.4f, word spaces %.4f, first words %.4f), detected %zu of %zu, "
                        "%zu false tracks\n",
                        s.cer, s.char_cer, s.space_error_rate, s.first_word_cer, s.detected, s.scored,
                        s.false_tracks);
```

- [ ] **Step 6: Build and run the tests**

```powershell
cmake --build --preset windows
ctest --preset windows -R "Scoring|Labels"
```
Expected: all `Scoring.*` and `Labels.*` tests pass, including the milestone-1 ones.

- [ ] **Step 7: Smoke check**

In Git Bash: `bash bench/smoke.sh build/windows`
Expected: `smoke test passed`; the `CER` value on the summary line equals the one printed before this task (the total edit count is the same Levenshtein distance).

- [ ] **Step 8: Document the metrics**

In `docs/signal-processing.md`, section 11, replace the **Character error rate (CER)** bullet with:

```markdown
- **Character error rate (CER):** the minimum number of symbol insertions,
  deletions and substitutions to turn the decoded text into the reference,
  divided by the number of reference symbols. A prosign token counts as one
  symbol, and word spaces count as symbols.
- **Where the edits are** (spec §5.4): the benchmark takes one minimum-edit
  alignment (ties broken, from the end, as match or substitution, then
  deletion, then insertion) and charges each edit to one reference symbol:
  a substitution or deletion to its own symbol, an insertion to the
  reference symbol it precedes (the last one after the end). An edit that
  involves a word space on either side is a **space edit**; the rest are
  **character edits**. Reported alongside CER, each summed over scored
  signals before dividing:
  - **character CER** = character edits / reference symbols that are not
    word spaces;
  - **space error rate** = space edits / reference word spaces;
  - **first-word CER** = edits charged to the first word of each
    transmission / the symbols of those words.
  Character and space edits add up to the CER's edit count.
```

- [ ] **Step 9: Commit on the `milestone-2` branch**

```powershell
git add bench/src/labels.hpp bench/src/labels.cpp bench/src/scoring.hpp bench/src/scoring.cpp bench/src/main.cpp bench/tests/labels_test.cpp bench/tests/scoring_test.cpp docs/signal-processing.md
```
```powershell
git commit -m "Score word spaces and each transmission's first word separately in the bench"
```

---

### Task 6: Engine and bench — oracle channels and CPU time per channel-second

Two measurement tools. **Oracle mode** opens a channel at each labeled frequency, rounded to the FFT bin, from the first sample, and bypasses the detector, so the decoder can be measured below the detector's threshold (about S₅₀₀ = 0 dB) and apart from detection errors; the rounding leaves the frequency tracker (Task 8) the whole ±11.7 Hz bin-rounding offset to find. **CPU time per channel-second** (spec §5.4) is process CPU time divided by the total duration of channel output the decoders received; a second figure counts only the time spent inside decoders.

**Files:**
- Modify: `engine/include/kz4ap/engine.hpp`, `engine/src/engine.cpp`
- Create: `bench/src/cpu_time.hpp`, `bench/src/cpu_time.cpp`
- Modify: `bench/src/scoring.hpp`, `bench/src/scoring.cpp` (match by order), `bench/src/main.cpp`, `bench/CMakeLists.txt`
- Test: `engine/tests/engine_test.cpp`, `bench/tests/scoring_test.cpp`, `bench/tests/cpu_time_test.cpp` (new)
- Modify: `docs/signal-processing.md` (§6, §11)

**Interfaces:**
- Consumes: `score()`, `LabeledSignal` (Task 5).
- Produces:
  - `EngineConfig::oracle_frequencies_hz` (`std::vector<double>`, Hz; empty = normal operation). In oracle mode the engine publishes a `Born` event for each frequency from its constructor, with track ids 1, 2, … in order and `freq_hz` = the bin center; no track ever dies; the detector is not run.
  - `struct EngineStats { std::uint64_t channel_samples = 0; double channel_seconds = 0; double decoder_seconds = 0; };` and `EngineStats Engine::stats() const`.
  - `struct Engine::Channel` gains `int bin` (the channel's center bin; Task 11 uses it).
  - `double kz4ap::bench::process_cpu_seconds();`
  - `score(labels, tracks, double match_tolerance_hz = 50.0, bool match_by_order = false)`: with `match_by_order`, label i matches the track with id i + 1.
  - `kz4ap-bench --oracle` (needs `--labels`). JSON output gains `channel_seconds` (always) and, unless `--no-timing`, `timing: {wall_s, cpu_s, cpu_ms_per_channel_s, decoder_ms_per_channel_s}`.

- [ ] **Step 1: Write the failing tests**

Append to `engine/tests/engine_test.cpp`:

```cpp
TEST(Engine, OracleOpensChannelsAtTheGivenFrequencies) {
    EventBus bus;
    std::vector<Track> born;
    std::map<std::uint32_t, std::string> text;
    bus.subscribe([&](const Event& e) {
        if (const auto* t = std::get_if<TrackEvent>(&e); t && t->kind == TrackEvent::Kind::Born) born.push_back(t->track);
        if (const auto* d = std::get_if<DecodedTextEvent>(&e)) {
            for (const auto& c : d->chars) text[d->track_id] += c.text;
        }
    });
    EngineConfig config;
    config.oracle_frequencies_hz = {12003.0, -30000.0};
    Engine engine(config, bus);
    ASSERT_EQ(born.size(), 2u);  // published by the constructor
    EXPECT_EQ(born[0].id, 1u);
    EXPECT_DOUBLE_EQ(born[0].freq_hz, 512 * 23.4375);  // 12003 Hz rounded to its bin
    EXPECT_EQ(born[1].id, 2u);
    EXPECT_DOUBLE_EQ(born[1].freq_hz, -30000.0);
    const auto x = band(9.0);
    engine.process(x);
    engine.finish();
    EXPECT_EQ(born.size(), 2u);  // the detector made no tracks of its own
    EXPECT_NE(text[1].find("CQ K1ABC"), std::string::npos) << text[1];
    EXPECT_NE(text[2].find("CQ W9XYZ"), std::string::npos) << text[2];
    EXPECT_NEAR(engine.stats().channel_seconds, 2 * 9.0, 0.05);
    EXPECT_EQ(engine.stats().channel_samples, static_cast<std::uint64_t>(std::llround(engine.stats().channel_seconds * 1500.0)));
}

TEST(Engine, StatsCountChannelTimeOfDetectedTracks) {
    EventBus bus;
    EngineConfig config;
    Engine engine(config, bus);
    engine.process(band(9.0));
    engine.finish();
    // Two stations, each tracked from about 1.5 s to the end.
    EXPECT_GT(engine.stats().channel_seconds, 2 * 6.0);
    EXPECT_LT(engine.stats().channel_seconds, 2 * 9.0);
    EXPECT_GE(engine.stats().decoder_seconds, 0.0);
}
```

(add `#include <cmath>` to the includes).

Append to `bench/tests/scoring_test.cpp`:

```cpp
TEST(Scoring, MatchByOrderIgnoresFrequencyAndLength) {
    const std::vector<LabeledSignal> labels{{"CQ", 1000.0, 25, 20, 0, 5}, {"CQ TEST", 1020.0, 25, 20, 0, 5}};
    const std::vector<DecodedTrack> tracks{{1, 1000.0, "E"}, {2, 1020.0, "CQ TEST CQ TEST"}};
    const auto s = score(labels, tracks, 50.0, true);
    EXPECT_EQ(s.signals[0].track_id, 1u);
    EXPECT_EQ(s.signals[1].track_id, 2u);
}
```

Create `bench/tests/cpu_time_test.cpp`:

```cpp
#include "cpu_time.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <cmath>

TEST(CpuTime, BusyLoopUsesCpuTime) {
    const double before = kz4ap::bench::process_cpu_seconds();
    const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(300);
    volatile double sink = 0;
    while (std::chrono::steady_clock::now() < until) sink = sink + std::sqrt(static_cast<double>(sink) + 1.0);
    const double used = kz4ap::bench::process_cpu_seconds() - before;
    EXPECT_GT(used, 0.1);
    EXPECT_LT(used, 5.0);
}
```

In `bench/CMakeLists.txt`, add `src/cpu_time.cpp` to `kz4ap_bench_lib` and `tests/cpu_time_test.cpp` to `kz4ap_bench_tests`.

- [ ] **Step 2: Run the tests to verify they fail**

Run: `cmake --build --preset windows`
Expected: compile errors (`oracle_frequencies_hz`, `stats`, `cpu_time.hpp` unknown).

- [ ] **Step 3: Implement oracle mode and statistics in the engine**

In `engine/include/kz4ap/engine.hpp`, add to `EngineConfig`:

```cpp
    // Oracle mode, for the benchmark: when not empty, a channel is opened at each of
    // these frequencies (Hz from the span's center, rounded to the nearest FFT bin)
    // from the first sample, with track ids 1, 2, ... in this order, and the signal
    // detector is bypassed: no other track is born and none dies.
    std::vector<double> oracle_frequencies_hz;
```

Add before `class Engine`:

```cpp
struct EngineStats {
    std::uint64_t channel_samples = 0;  // channel samples delivered to decoders, summed over channels
    double channel_seconds = 0;         // the same, in s of channel output
    double decoder_seconds = 0;         // steady-clock time spent inside decoders, s
};
```

In `class Engine`, add the public method `EngineStats stats() const;`, change `struct Channel` to

```cpp
    struct Channel {
        Track track;
        int bin;  // the channel's center bin
        std::unique_ptr<Decoder> decoder;
    };
```

and add the private members `void open_channel(const Track& track, int bin);`, `bool oracle_ = false;`, `std::uint64_t channel_samples_ = 0;`, `double decoder_seconds_ = 0;`.

In `engine/src/engine.cpp` (add `#include <chrono>`), change the constructor body and `process_hop`, and add `open_channel` and `stats`:

```cpp
Engine::Engine(const EngineConfig& config, EventBus& bus)
    : config_(resolved(config)),
      bus_(bus),
      hop_(config_.fft_size / 2),
      spectrum_(config_.sample_rate, config_.fft_size, config_.fft_size / 2),
      detector_(detector_config(config_)),
      channelizer_(ChannelizerConfig{config_.sample_rate, config_.fft_size, config_.channel_bins,
                                     config_.channel_cutoff_hz}) {
    pending_.reserve(static_cast<std::size_t>(hop_));
    oracle_ = !config_.oracle_frequencies_hz.empty();
    for (std::size_t i = 0; i < config_.oracle_frequencies_hz.size(); ++i) {
        const int bin = channelizer_.hz_to_bin(config_.oracle_frequencies_hz[i]);
        Track track;
        track.id = static_cast<std::uint32_t>(i + 1);
        track.freq_hz = channelizer_.bin_to_hz(bin);
        open_channel(track, bin);
    }
}

EngineStats Engine::stats() const {
    EngineStats s;
    s.channel_samples = channel_samples_;
    s.channel_seconds = static_cast<double>(channel_samples_) / channelizer_.output_rate();
    s.decoder_seconds = decoder_seconds_;
    return s;
}

void Engine::process_hop(std::span<const Sample> hop) {
    for (auto& frame : spectrum_.push(hop)) {
        DetectorUpdate update;
        if (!oracle_) update = detector_.process(frame);
        bus_.publish(Event{std::move(frame)});
        for (const auto id : update.died) close_channel(id);
        for (const auto& track : update.born) open_channel(track, channelizer_.hz_to_bin(track.freq_hz));
    }
    channelizer_.push(hop, [this](std::uint32_t id, std::uint64_t first_index, std::span<const Sample> s) {
        const auto it = channels_.find(id);
        if (it == channels_.end()) return;
        const double t0 = static_cast<double>(first_index) / channelizer_.output_rate();
        const auto started = std::chrono::steady_clock::now();
        auto update = it->second.decoder->process(s, t0);
        decoder_seconds_ += std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
        channel_samples_ += s.size();
        publish_update(id, std::move(update));
    });
}

void Engine::open_channel(const Track& track, int bin) {
    channelizer_.add_channel(track.id, bin);
    channels_.emplace(track.id, Channel{track, bin, std::make_unique<ClassicalDecoder>(
                                                        channelizer_.output_rate(), config_.decoder)});
    bus_.publish(Event{TrackEvent{TrackEvent::Kind::Born, track}});
}
```

Declare the members in `engine.hpp` in this order after `channels_`: `bool oracle_ = false; std::uint64_t channel_samples_ = 0; double decoder_seconds_ = 0;`. (Detection, births and deaths happen in the same order as before, so normal operation is unchanged.)

- [ ] **Step 4: Implement match-by-order scoring**

In `bench/src/scoring.hpp`, change the declaration to:

```cpp
// Matches each labeled signal to the track within match_tolerance_hz that
// decoded the most text or, with match_by_order, label i to the track with
// id i + 1 (oracle mode), and scores the symbol error rate (a prosign counts
// as one symbol, same as space between words).
Score score(const std::vector<LabeledSignal>& labels, const std::vector<DecodedTrack>& tracks,
            double match_tolerance_hz = 50.0, bool match_by_order = false);
```

In `bench/src/scoring.cpp`, change the signature the same way, change `for (const auto& label : labels) {` to

```cpp
    for (std::size_t li = 0; li < labels.size(); ++li) {
        const auto& label = labels[li];
```

and wrap the frequency-matching loop so it reads:

```cpp
        const DecodedTrack* best = nullptr;
        std::size_t best_len = 0;
        if (match_by_order) {
            for (const auto& t : tracks) {
                if (t.id == li + 1 && !used.count(t.id)) best = &t;
            }
        } else {
            for (const auto& t : tracks) {
                if (used.count(t.id) || std::abs(t.freq_hz - label.freq_offset_hz) > match_tolerance_hz) continue;
                const std::size_t len = normalize_text(t.text).size();
                if (!best || len > best_len ||
                    (len == best_len && std::abs(t.freq_hz - label.freq_offset_hz) <
                                            std::abs(best->freq_hz - label.freq_offset_hz))) {
                    best = &t;
                    best_len = len;
                }
            }
        }
```

- [ ] **Step 5: Implement CPU time**

Create `bench/src/cpu_time.hpp`:

```cpp
#pragma once

namespace kz4ap::bench {

// CPU time this process has used so far, user plus kernel, s. Resolution is
// about 16 ms on Windows.
double process_cpu_seconds();

}  // namespace kz4ap::bench
```

Create `bench/src/cpu_time.cpp`:

```cpp
#include "cpu_time.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <ctime>
#endif

namespace kz4ap::bench {

double process_cpu_seconds() {
#if defined(_WIN32)
    FILETIME creation, exit, kernel, user;
    if (!GetProcessTimes(GetCurrentProcess(), &creation, &exit, &kernel, &user)) return 0.0;
    const auto ticks = [](const FILETIME& f) {
        return (static_cast<unsigned long long>(f.dwHighDateTime) << 32) | f.dwLowDateTime;
    };
    return static_cast<double>(ticks(kernel) + ticks(user)) * 1e-7;  // FILETIME counts 100 ns
#else
    timespec ts{};
    if (clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &ts) != 0) return 0.0;
    return static_cast<double>(ts.tv_sec) + static_cast<double>(ts.tv_nsec) * 1e-9;
#endif
}

}  // namespace kz4ap::bench
```

- [ ] **Step 6: Wire both into `kz4ap-bench`**

In `bench/src/main.cpp`:

- add `#include "cpu_time.hpp"`;
- add `bool oracle = false;` to `Args`, parse `else if (a == "--oracle") args.oracle = true;`, and after the existing `--baseline` check add `if (args.oracle && !args.labels) throw std::runtime_error("--oracle needs --labels");`;
- change `kUsage` to

```cpp
constexpr const char* kUsage =
    "usage: kz4ap-bench RECORDING.wav [--labels LABELS.json] [--json OUT.json]\n"
    "                   [--no-timing] [--baseline BASELINE.json] [--oracle]\n";
```

- load the labels before the engine runs (move the `load_labels` call and its sample-rate check up, right after `WavIqReader reader(args.recording);`):

```cpp
        std::optional<Labels> labels;
        if (args.labels) {
            labels = load_labels(*args.labels);
            if (labels->sample_rate != reader.sample_rate())
                throw std::runtime_error("labels sample rate does not match the recording");
        }
```

then, in the scoring block further down, delete its own `const Labels labels = load_labels(*args.labels);` line and the sample-rate check after it, change `if (args.labels) {` to `if (labels) {`, and write `labels->signals` wherever that block wrote `labels.signals` (including `{"labels", labels->signals.size()}`);
- configure the oracle and measure CPU time:

```cpp
        EngineConfig config;
        config.sample_rate = reader.sample_rate();
        if (args.oracle) {
            for (const auto& s : labels->signals) config.oracle_frequencies_hz.push_back(s.freq_offset_hz);
        }
        Engine engine(config, bus);
        std::vector<Sample> block(65536);
        const double cpu_started = process_cpu_seconds();
        const auto started = std::chrono::steady_clock::now();
        while (const auto n = reader.read(block)) engine.process(std::span<const Sample>(block).first(n));
        engine.finish();
        const double wall_s = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
        const double cpu_s = process_cpu_seconds() - cpu_started;
        const EngineStats stats = engine.stats();
```

- replace the `if (args.timing) { … }` block with:

```cpp
        out["channel_seconds"] = stats.channel_seconds;
        if (args.timing) {
            const auto per_channel_ms = [&](double seconds) {
                return stats.channel_seconds > 0 ? 1000.0 * seconds / stats.channel_seconds : 0.0;
            };
            out["realtime_factor"] = duration_s / wall_s;
            out["timing"] = {{"wall_s", wall_s},
                             {"cpu_s", cpu_s},
                             {"cpu_ms_per_channel_s", per_channel_ms(cpu_s)},
                             {"decoder_ms_per_channel_s", per_channel_ms(stats.decoder_seconds)}};
            std::printf("processed %.1f s of audio in %.2f s (%.1fx real time); CPU %.2f ms per channel-second "
                        "(decoders %.3f ms)\n",
                        duration_s, wall_s, duration_s / wall_s, per_channel_ms(cpu_s),
                        per_channel_ms(stats.decoder_seconds));
        }
```

- call `score(labels->signals, track_list, 50.0, args.oracle)`.

- [ ] **Step 7: Build and run the tests**

```powershell
cmake --build --preset windows
ctest --preset windows
```
Expected: all tests pass, including `Engine.OracleOpensChannelsAtTheGivenFrequencies`, `Engine.StatsCountChannelTimeOfDetectedTracks`, `Scoring.MatchByOrderIgnoresFrequencyAndLength`, `CpuTime.BusyLoopUsesCpuTime`.

- [ ] **Step 8: Smoke check and an oracle run**

In Git Bash: `bash bench/smoke.sh build/windows` → `smoke test passed`, same CER as before.
Then: `build/windows/bench/Release/kz4ap-bench.exe build/windows/smoke/band.wav --labels build/windows/smoke/band.json --oracle`
Expected: 8 `label` lines, `detected 8 of 8, 0 false tracks`, and a `CPU … ms per channel-second` figure.

- [ ] **Step 9: Document**

In `docs/signal-processing.md`, section 6, after the **Cap** subsection, add:

```markdown
### Oracle mode (benchmark only)

`EngineConfig::oracle_frequencies_hz` (`kz4ap-bench --oracle`) bypasses the
detector: a channel is opened at each given frequency, rounded to the nearest
FFT bin, from the first sample, and stays open to the end. It measures the
decoder apart from detection, including below the detector's threshold
(roughly S₅₀₀ = 0 dB for a keyed station), and leaves the station up to
±½ bin (±11.7 Hz) off its channel's center, the worst case for frequency
re-centering. Normal operation never uses it.
```

In section 11, add at the end:

```markdown
- **CPU time per channel-second:** the process's CPU time (user plus
  kernel) for the whole run divided by the total duration of channel output
  delivered to decoders, summed over channels, in ms per channel-second. It
  includes the shared FFTs and the detector, so it is an upper bound on the
  per-channel cost; `decoder_ms_per_channel_s` counts only steady-clock time
  inside decoders (the engine runs on one thread). Measured on a desktop; a
  Raspberry Pi 5 is not yet measured.
```

- [ ] **Step 10: Commit on the `milestone-2` branch**

```powershell
git add engine/include/kz4ap/engine.hpp engine/src/engine.cpp engine/tests/engine_test.cpp bench/src/cpu_time.hpp bench/src/cpu_time.cpp bench/src/scoring.hpp bench/src/scoring.cpp bench/src/main.cpp bench/CMakeLists.txt bench/tests/scoring_test.cpp bench/tests/cpu_time_test.cpp docs/signal-processing.md
```
```powershell
git commit -m "Add oracle channels and CPU time per channel-second to the engine and bench"
```

---

### Task 7: Benchmark suites and runner

Named suites turn the generator's options into a fixed, seeded set of recordings; a runner scores each with `kz4ap-bench` for each front end; a summary reports, per scenario and condition: CER, character CER, space error rate, first-word CER, detections, the S₅₀₀ at which CER crosses 0.10 and 0.05, and CPU time per channel-second.

**Suites.** `smoke` is exactly the recording `bench/smoke.sh` makes (band scenario, 8 signals, 30 s, seed 1, 192 kHz); CI keeps using `smoke.sh`. `full` is for local runs; `--seeds N` repeats it with N seeds. Single-station scenarios run at 48 kHz (the engine then uses N = 2048, the same 23.4 Hz bins and r = 1500 samples/s) with many stations per recording, 1.2–5 kHz apart, so one recording covers a whole S₅₀₀ sweep:

| Group | Recordings (per seed) | Bench mode | What varies |
|---|---|---|---|
| A sensitivity | 12, 25, 40 WPM; 60 s | oracle | S₅₀₀ −10 … +20 dB in 2 dB steps (16 stations), machine keying |
| B fading | paddle 24 WPM f_D 0.1/0.3/1/3 Hz; hand 24 WPM f_D 0.1/3 Hz; paddle 12 WPM f_D 0.1 Hz; 60 s | oracle | S₅₀₀ at VE3NEA's points 1.8, 7.8, 13.8, 17.8, 27.8 dB, 3 stations each |
| C fists | machine, paddle, bug, hand at 25 WPM; 60 s | oracle | S₅₀₀ 5, 10, 20 dB × imbalance 0, +0.1, −0.1 dit |
| D speed | one recording, 60 s | oracle | steps 20→35 and 35→20 WPM, ramps 15→30 and 30→15 WPM, 10 WPM, 60 WPM; S₅₀₀ 15 dB |
| E interference | one recording, 60 s | oracle | wanted 25 WPM at S₅₀₀ 10 dB; unscored interferer at 30 WPM, Δf = 20/50/100/150 Hz, −10/0/+10/+20 dB relative (key-down power) |
| F tuning | offsets 0/2.9/5.9/8.8/11.7 Hz from the bin center × 20, 25 WPM × S₅₀₀ 0, 5 dB (60 s); drift 0.2/0.5/1/2 Hz/s at 25 WPM, S₅₀₀ 5 dB (30 s) | oracle | carrier offset and drift |
| strong | S₅₀₀ 30, 40, 50, 60 dB; 30 s | detector | ghost tracks (false tracks), CER |
| pauses | a CQ sent 3 times with 2, 5, 10, 20 s pauses; S₅₀₀ 15 dB; 80 s | detector | track lifetime, first words |
| tune-up | 0.3, 0.6, 1.0, 2.0 s carriers before keying; S₅₀₀ 15 dB; 30 s | detector | the speed-estimate bug |
| first sample | 4 stations keying from 0 s; 20 s | detector | stations present at the start |
| crowded | 25 stations within ±5 kHz at minimum spacing 200, 100, 50, 0 Hz; 10–60 WPM; 40 s | detector | crowding, very different speeds side by side |
| band | 20 stations over 192 kHz, 10–60 WPM, S₅₀₀ 10–60 dB; 30 s | detector | the whole pipeline |

**Crossing S₅₀₀ (definition, written to signal-processing.md §11):** for a condition with at least three S₅₀₀ points, CER is computed per point (edits over symbols, pooled across stations); scanning down from the highest S₅₀₀, the first point whose CER exceeds the threshold and the point above it bracket the crossing, which is interpolated linearly in dB. If the top point already fails, there is no crossing; if no point fails, the lowest point is reported (an upper bound).

**Files:**
- Create: `training/kz4ap_synth/suites.py`
- Test: `training/tests/test_suites.py` (new)
- Modify: `README.md` (Benchmark section), `docs/signal-processing.md` (§11)

**Interfaces:**
- Consumes: `SignalSpec`, `generate`, `labels`, `plan_intervals`, `signal_end_s`, `scenario_band`, `with_interferer`, `fill_text`, `random_callsign`, `MESSAGES`, `write_wav` (Tasks 1–4); `kz4ap-bench --oracle` and its JSON fields (Tasks 5–6); `--front-end` (Task 11; the runner passes it only for front ends other than `baseline`).
- Produces (Python, `kz4ap_synth.suites`): `Recording(name, group, sample_rate, duration_s, noise_seed, oracle, specs)`; `SUITES: dict[str, Callable[[int], list[Recording]]]` with `"smoke"` and `"full"`; `check_recording(rec) -> None` (raises `ValueError`); `write_suite(recordings, out_dir, suite_name) -> None`; `run_suite(out_dir, bench, front_ends) -> None`; `crossing_snr(points, threshold) -> float | None`; `aggregate(rows) -> dict[tuple[str, str, str], dict]`; `write_summary(out_dir) -> None` (writes `summary.json` and `summary.md`). CLI: `python -m kz4ap_synth.suites generate|run|summarize`.

- [ ] **Step 1: Write the failing tests**

Create `training/tests/test_suites.py`:

```python
import json

import numpy as np
import pytest

from kz4ap_synth.generate import SignalSpec, scenario_band
from kz4ap_synth.suites import (
    SUITES,
    VE3NEA_SNR_DB,
    Recording,
    aggregate,
    check_recording,
    crossing_snr,
    write_suite,
    write_summary,
)


def test_smoke_suite_is_the_smoke_script_recording():
    (rec,) = SUITES["smoke"](1)
    assert rec.specs == scenario_band(np.random.default_rng(1), 8, 30.0, 192000)
    assert (rec.sample_rate, rec.duration_s, rec.noise_seed, rec.oracle) == (192000, 30.0, 2, False)


def test_full_suite_recordings_are_valid_and_uniquely_named():
    recs = SUITES["full"](1)
    names = [r.name for r in recs]
    assert len(names) == len(set(names))
    for r in recs:
        check_recording(r)
    assert {r.group for r in recs} == {
        "A sensitivity", "B fading", "C fists", "D speed", "E interference", "F tuning",
        "strong", "pauses", "tune-up", "first sample", "crowded", "band"}


def test_full_suite_covers_the_scenarios():
    specs = [s for r in SUITES["full"](1) for s in r.specs]
    assert max(s.snr_db for s in specs if s.score) == 60.0
    assert min(s.snr_db for s in specs) == -10.0
    assert set(VE3NEA_SNR_DB) <= {s.snr_db for s in specs if s.fading_hz > 0}
    assert {s.fading_hz for s in specs} >= {0.1, 0.3, 1.0, 3.0}
    assert {s.keying for s in specs} == {"machine", "paddle", "bug", "hand"}
    assert any(not s.score for s in specs)
    assert {s.pause_s for s in specs if s.repeats > 1} == {2.0, 5.0, 10.0, 20.0}
    assert {s.tune_s for s in specs if s.tune_s > 0} == {0.3, 0.6, 1.0, 2.0}
    assert {s.drift_hz_per_s for s in specs if s.drift_hz_per_s} == {0.2, 0.5, 1.0, 2.0}
    assert any(s.start_s == 0.0 for s in specs)
    assert min(s.wpm for s in specs) <= 10.0 and max(s.wpm for s in specs) >= 60.0
    assert any(s.wpm_end is not None for s in specs)


def test_more_seeds_make_more_recordings():
    assert len(SUITES["full"](2)) == 2 * len(SUITES["full"](1))


def test_check_rejects_a_signal_that_runs_past_the_end():
    rec = Recording("x", "g", 8000, 1.0, 1, True, [SignalSpec("CQ CQ CQ", 100.0, 20.0, 10.0, 0.5)])
    with pytest.raises(ValueError):
        check_recording(rec)


def test_crossing_interpolates_between_bracketing_points():
    points = [(-4.0, 0.9), (-2.0, 0.4), (0.0, 0.08), (2.0, 0.02), (4.0, 0.0)]
    assert crossing_snr(points, 0.10) == pytest.approx(-2.0 + (0.4 - 0.10) / (0.4 - 0.08) * 2.0)


def test_crossing_starts_from_the_highest_failing_point():
    points = [(-2.0, 0.05), (0.0, 0.3), (2.0, 0.02)]
    assert crossing_snr(points, 0.10) == pytest.approx(0.0 + (0.3 - 0.10) / (0.3 - 0.02) * 2.0)


def test_crossing_is_none_when_never_reached_and_lowest_point_when_always_below():
    assert crossing_snr([(0.0, 0.5), (2.0, 0.3)], 0.10) is None
    assert crossing_snr([(0.0, 0.01), (2.0, 0.0)], 0.10) == 0.0


def _row(snr, symbols, edits, scored=True):
    return {"front_end": "baseline", "group": "A", "tag": "25 wpm", "snr_db": snr, "scored": scored,
            "detected": True, "symbols": symbols, "edits": edits, "chars": symbols - 2,
            "char_edits": edits, "spaces": 2, "space_edits": 0, "first_word_symbols": 2,
            "first_word_edits": 0}


def test_aggregate_pools_symbols_and_skips_unscored_signals():
    agg = aggregate([_row(0.0, 10, 5), _row(2.0, 30, 3), _row(2.0, 100, 100, scored=False)])
    v = agg[("baseline", "A", "25 wpm")]
    assert v["signals"] == 2
    assert v["cer"] == pytest.approx(8 / 40)
    assert v["space_error_rate"] == 0.0
    assert v["cer_by_snr"] == [(0.0, 0.5), (2.0, 0.1)]


def test_write_suite_and_summary_round_trip(tmp_path):
    rec = Recording("tiny", "A sensitivity", 8000, 3.0, 5, True,
                    [SignalSpec("CQ", 1000.0, 25.0, 10.0, 0.5, tag="25 wpm")])
    write_suite([rec], tmp_path, "test")
    manifest = json.loads((tmp_path / "manifest.json").read_text())
    assert manifest["recordings"][0]["oracle"] is True
    assert (tmp_path / "tiny.wav").exists()
    # A fake bench result, as kz4ap-bench would write it:
    results = tmp_path / "results" / "baseline"
    results.mkdir(parents=True)
    (results / "tiny.json").write_text(json.dumps({
        "channel_seconds": 3.0, "timing": {"cpu_s": 0.03, "decoder_ms_per_channel_s": 1.0},
        "score": {"signals": [{"index": 0, "snr_db": 10.0, "wpm": 25.0, "scored": True, "track_id": 1,
                               "symbols": 2, "edits": 1, "chars": 2, "char_edits": 1, "spaces": 0,
                               "space_edits": 0, "first_word_symbols": 2, "first_word_edits": 1}]}}))
    write_summary(tmp_path)
    text = (tmp_path / "summary.md").read_text(encoding="utf-8")
    assert "## A sensitivity" in text
    assert "| 25 wpm | baseline | 1 | 1 | 0.500 |" in text
    assert "| baseline | 3.0 | 10.000 | 1.000 |" in text
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `.venv\Scripts\python -m pytest training/tests/test_suites.py -q`
Expected: `ModuleNotFoundError: No module named 'kz4ap_synth.suites'`.

- [ ] **Step 3: Implement**

Create `training/kz4ap_synth/suites.py`:

```python
"""Named benchmark suites: which synthetic recordings to make, a runner that
scores each with kz4ap-bench, and a summary.

    python -m kz4ap_synth.suites generate --suite full --out build/suite/full [--seeds 2]
    python -m kz4ap_synth.suites run --out build/suite/full --bench PATH/kz4ap-bench \
        --front-end baseline --front-end matched
    python -m kz4ap_synth.suites summarize --out build/suite/full

"smoke" is the recording bench/smoke.sh makes (the CI check); "full" is for
local runs. S500 everywhere: key-down carrier power over noise power in
500 Hz, dB.
"""

from __future__ import annotations

import argparse
import json
import subprocess
from dataclasses import dataclass
from pathlib import Path

import numpy as np

from .generate import (MESSAGES, SignalSpec, fill_text, generate, labels, plan_intervals, random_callsign,
                       scenario_band, signal_end_s, with_interferer, write_wav)

BIN_HZ = 48000 / 2048  # the engine's FFT bin width at 48 kHz (and at 192 kHz), Hz
SWEEP_SNR_DB = [float(x) for x in range(-10, 22, 2)]  # S500 sweep: -10 ... +20 dB
VE3NEA_SNR_DB = [1.8, 7.8, 13.8, 17.8, 27.8]         # VE3NEA's grid as S500 (his 3 kHz key-on SNR + 7.8 dB)
CER_THRESHOLDS = (0.05, 0.10)
FRONT_ENDS = ("baseline", "matched")
COUNT_KEYS = ("symbols", "edits", "chars", "char_edits", "spaces", "space_edits",
              "first_word_symbols", "first_word_edits")

FADING_ROWS = [("paddle", 24.0, 0.1), ("paddle", 24.0, 0.3), ("paddle", 24.0, 1.0), ("paddle", 24.0, 3.0),
               ("hand", 24.0, 0.1), ("hand", 24.0, 3.0), ("paddle", 12.0, 0.1)]  # (keying, WPM, f_D Hz)
SPEED_CASES = [(20.0, 35.0, "step", "step 20->35"), (35.0, 20.0, "step", "step 35->20"),
               (15.0, 30.0, "ramp", "ramp 15->30"), (30.0, 15.0, "ramp", "ramp 30->15"),
               (10.0, None, "step", "10 wpm"), (60.0, None, "step", "60 wpm")]
QRM_OFFSETS_HZ = (20.0, 50.0, 100.0, 150.0)
QRM_RELATIVE_DB = (-10.0, 0.0, 10.0, 20.0)
TUNING_OFFSETS_HZ = (0.0, 2.9, 5.9, 8.8, 11.7)  # from the bin center: 0 to 1/2 bin
DRIFT_HZ_PER_S = (0.2, 0.5, 1.0, 2.0)
PAUSES_S = (2.0, 5.0, 10.0, 20.0)
TUNE_UP_S = (0.3, 0.6, 1.0, 2.0)
CROWDED_SPACING_HZ = (200.0, 100.0, 50.0, 0.0)


@dataclass
class Recording:
    name: str
    group: str         # scenario group, used in summaries
    sample_rate: int
    duration_s: float
    noise_seed: int    # the seed passed to generate() and labels()
    oracle: bool       # score with kz4ap-bench --oracle
    specs: list[SignalSpec]


def _message(rng) -> str:
    return MESSAGES[rng.integers(len(MESSAGES))].format(c=random_callsign(rng))


def _text(rng, wpm: float, available_s: float, keying: str = "machine") -> str:
    """A repeated contest message that fits available_s; random keying runs longer, so it gets 30% slack."""
    return fill_text(_message(rng), wpm, available_s * (1.0 if keying == "machine" else 0.7))


def _start(rng) -> float:
    return round(float(rng.uniform(0.5, 2.0)), 3)


def _slots(count: int, spacing_hz: float, rng, jitter_hz: float) -> list[float]:
    """count frequencies spacing_hz apart, centered on 0 Hz, each moved by a uniform offset
    within +/-jitter_hz so stations fall anywhere relative to the FFT bins."""
    centers = (np.arange(count) - (count - 1) / 2) * spacing_hz
    return [round(float(c + rng.uniform(-jitter_hz, jitter_hz)), 1) for c in centers]


def _bin_centers(count: int, spacing_bins: int) -> list[float]:
    """count exact FFT-bin centers spacing_bins apart around 0 Hz (48 kHz recordings)."""
    return [(k - count // 2) * spacing_bins * BIN_HZ for k in range(count)]


def sensitivity(seed: int) -> list[Recording]:
    recs = []
    for code, wpm in enumerate((12.0, 25.0, 40.0)):
        rng = np.random.default_rng([seed, 1, code])
        specs = []
        for f, snr in zip(_slots(len(SWEEP_SNR_DB), 1200.0, rng, BIN_HZ / 2), SWEEP_SNR_DB):
            start = _start(rng)
            specs.append(SignalSpec(_text(rng, wpm, 58.0 - start), f, wpm, snr, start, tag=f"{wpm:g} wpm"))
        recs.append(Recording(f"A-awgn-{wpm:g}wpm-s{seed}", "A sensitivity", 48000, 60.0, 1000 * seed + 10 + code,
                              True, specs))
    return recs


def fading(seed: int) -> list[Recording]:
    recs = []
    for code, (keying, wpm, f_d) in enumerate(FADING_ROWS):
        rng = np.random.default_rng([seed, 2, code])
        points = [snr for snr in VE3NEA_SNR_DB for _ in range(3)]
        specs = []
        for f, snr in zip(_slots(len(points), 1200.0, rng, BIN_HZ / 2), points):
            start = _start(rng)
            specs.append(SignalSpec(_text(rng, wpm, 58.0 - start, keying), f, wpm, snr, start, keying=keying,
                                    fading_hz=f_d, tag=f"{keying} {wpm:g} wpm fD {f_d:g} Hz"))
        recs.append(Recording(f"B-fading-{keying}-{wpm:g}wpm-{f_d:g}Hz-s{seed}", "B fading", 48000, 60.0,
                              1000 * seed + 20 + code, True, specs))
    return recs


def fists(seed: int) -> list[Recording]:
    recs = []
    for code, keying in enumerate(("machine", "paddle", "bug", "hand")):
        rng = np.random.default_rng([seed, 3, code])
        combos = [(snr, imb) for snr in (5.0, 10.0, 20.0) for imb in (0.0, 0.1, -0.1)]
        specs = []
        for f, (snr, imb) in zip(_slots(len(combos), 1200.0, rng, BIN_HZ / 2), combos):
            start = _start(rng)
            specs.append(SignalSpec(_text(rng, 25.0, 58.0 - start, keying), f, 25.0, snr, start, keying=keying,
                                    imbalance_dits=imb, tag=f"{keying} imbalance {imb:+.1f}"))
        recs.append(Recording(f"C-fists-{keying}-s{seed}", "C fists", 48000, 60.0, 1000 * seed + 30 + code,
                              True, specs))
    return recs


def speed(seed: int) -> list[Recording]:
    rng = np.random.default_rng([seed, 4])
    cases = SPEED_CASES * 2
    specs = []
    for f, (wpm, wpm_end, profile, tag) in zip(_slots(len(cases), 1500.0, rng, BIN_HZ / 2), cases):
        start = _start(rng)
        slowest = min(wpm, wpm_end if wpm_end is not None else wpm)  # timed at the slowest speed, so it fits
        specs.append(SignalSpec(_text(rng, slowest, 58.0 - start), f, wpm, 15.0, start, wpm_end=wpm_end,
                                speed_profile=profile, tag=tag))
    return [Recording(f"D-speed-s{seed}", "D speed", 48000, 60.0, 1000 * seed + 40, True, specs)]


def interference(seed: int) -> list[Recording]:
    rng = np.random.default_rng([seed, 5])
    pairs = [(df, rel) for df in QRM_OFFSETS_HZ for rel in QRM_RELATIVE_DB]
    specs = []
    for f, (df, rel) in zip(_slots(len(pairs), 1200.0, rng, BIN_HZ / 2), pairs):
        start = _start(rng)
        wanted = SignalSpec(_text(rng, 25.0, 58.0 - start), f, 25.0, 10.0, start, tag=f"df {df:g} Hz rel {rel:+g} dB")
        specs += with_interferer(wanted, df, rel, 30.0, _text(rng, 30.0, 58.0 - start))
    return [Recording(f"E-qrm-s{seed}", "E interference", 48000, 60.0, 1000 * seed + 50, True, specs)]


def tuning(seed: int) -> list[Recording]:
    rng = np.random.default_rng([seed, 6, 0])
    cases = [(wpm, off, snr) for wpm in (20.0, 25.0) for off in TUNING_OFFSETS_HZ for snr in (0.0, 5.0)]
    specs = []
    for c, (wpm, off, snr) in zip(_bin_centers(len(cases), 51), cases):
        start = _start(rng)
        specs.append(SignalSpec(_text(rng, wpm, 58.0 - start), round(c + off, 3), wpm, snr, start,
                                tag=f"offset {off:g} Hz {wpm:g} wpm"))
    offsets = Recording(f"F-offset-s{seed}", "F tuning", 48000, 60.0, 1000 * seed + 60, True, specs)
    rng = np.random.default_rng([seed, 6, 1])
    drifts = [d for d in DRIFT_HZ_PER_S for _ in range(2)]
    specs = []
    for c, d in zip(_bin_centers(len(drifts), 51), drifts):
        start = _start(rng)
        specs.append(SignalSpec(_text(rng, 25.0, 28.0 - start), round(c, 3), 25.0, 5.0, start, drift_hz_per_s=d,
                                tag=f"drift {d:g} Hz/s"))
    drift = Recording(f"F-drift-s{seed}", "F tuning", 48000, 30.0, 1000 * seed + 61, True, specs)
    return [offsets, drift]


def strong(seed: int) -> list[Recording]:
    rng = np.random.default_rng([seed, 7])
    snrs = [s for s in (30.0, 40.0, 50.0, 60.0) for _ in range(2)]
    specs = []
    for f, snr in zip(_slots(len(snrs), 5000.0, rng, BIN_HZ / 2), snrs):
        start = _start(rng)
        specs.append(SignalSpec(_text(rng, 25.0, 28.0 - start), f, 25.0, snr, start, tag=f"S500 {snr:g} dB"))
    return [Recording(f"strong-s{seed}", "strong", 48000, 30.0, 1000 * seed + 70, False, specs)]


def pauses(seed: int) -> list[Recording]:
    rng = np.random.default_rng([seed, 8])
    cases = [p for p in PAUSES_S for _ in range(2)]
    specs = []
    for f, p in zip(_slots(len(cases), 2400.0, rng, BIN_HZ / 2), cases):
        start = _start(rng)
        call = random_callsign(rng)
        specs.append(SignalSpec(f"CQ TEST {call} {call}", f, 25.0, 15.0, start, repeats=3, pause_s=p,
                                tag=f"pause {p:g} s"))
    return [Recording(f"pauses-s{seed}", "pauses", 48000, 80.0, 1000 * seed + 80, False, specs)]


def tune_up(seed: int) -> list[Recording]:
    rng = np.random.default_rng([seed, 9])
    cases = [t for t in TUNE_UP_S for _ in range(2)]
    specs = []
    for f, t in zip(_slots(len(cases), 2400.0, rng, BIN_HZ / 2), cases):
        start = _start(rng)
        specs.append(SignalSpec(_text(rng, 25.0, 27.5 - start - t), f, 25.0, 15.0, start, tune_s=t,
                                tag=f"tune-up {t:g} s"))
    return [Recording(f"tune-up-s{seed}", "tune-up", 48000, 30.0, 1000 * seed + 90, False, specs)]


def first_sample(seed: int) -> list[Recording]:
    rng = np.random.default_rng([seed, 10])
    specs = [SignalSpec(_text(rng, 25.0, 18.0), f, 25.0, 15.0, 0.0, tag="from the first sample")
             for f in _slots(4, 3000.0, rng, BIN_HZ / 2)]
    return [Recording(f"first-sample-s{seed}", "first sample", 48000, 20.0, 1000 * seed + 100, False, specs)]


def crowded(seed: int) -> list[Recording]:
    recs = []
    for code, spacing in enumerate(CROWDED_SPACING_HZ):
        rng = np.random.default_rng([seed, 11, code])
        specs = scenario_band(rng, 25, 40.0, 48000, min_spacing_hz=spacing, span_hz=5000.0,
                              wpm_range=(10.0, 60.0), snr_range=(10.0, 30.0))
        for s in specs:
            s.tag = f"spacing {spacing:g} Hz"
        recs.append(Recording(f"crowded-{spacing:g}Hz-s{seed}", "crowded", 48000, 40.0, 1000 * seed + 110 + code,
                              False, specs))
    return recs


def band(seed: int) -> list[Recording]:
    rng = np.random.default_rng([seed, 12])
    specs = scenario_band(rng, 20, 30.0, 192000, wpm_range=(10.0, 60.0), snr_range=(10.0, 60.0))
    for s in specs:
        s.tag = "band"
    return [Recording(f"band-s{seed}", "band", 192000, 30.0, 1000 * seed + 120, False, specs)]


def smoke_suite(seeds: int = 1) -> list[Recording]:
    """The recording bench/smoke.sh makes: band scenario, 8 signals, 30 s, seed 1 (noise seed 2)."""
    specs = scenario_band(np.random.default_rng(1), 8, 30.0, 192000)
    return [Recording("smoke-band", "smoke", 192000, 30.0, 2, False, specs)]


def full_suite(seeds: int = 1) -> list[Recording]:
    recs = []
    for seed in range(1, seeds + 1):
        for build in (sensitivity, fading, fists, speed, interference, tuning, strong, pauses, tune_up,
                      first_sample, crowded, band):
            recs += build(seed)
    return recs


SUITES = {"smoke": smoke_suite, "full": full_suite}


def check_recording(rec: Recording) -> None:
    """Raises ValueError if a signal runs past the end or lies outside the span."""
    for spec, plan in zip(rec.specs, plan_intervals(rec.specs, rec.noise_seed)):
        end = signal_end_s(spec, plan)
        if end > rec.duration_s:
            raise ValueError(f"{rec.name}: signal at {spec.freq_offset_hz} Hz ends at {end:.2f} s, "
                             f"after the recording's {rec.duration_s} s")
        if abs(spec.freq_offset_hz) > 0.45 * rec.sample_rate:
            raise ValueError(f"{rec.name}: signal at {spec.freq_offset_hz} Hz is outside the span")


def write_suite(recordings: list[Recording], out_dir: Path, suite_name: str) -> None:
    """Writes every recording (WAV plus labels) and manifest.json into out_dir."""
    out_dir.mkdir(parents=True, exist_ok=True)
    entries = []
    for rec in recordings:
        check_recording(rec)
        wav = out_dir / f"{rec.name}.wav"
        write_wav(wav, generate(rec.specs, rec.sample_rate, rec.duration_s, seed=rec.noise_seed), rec.sample_rate)
        lab = labels(rec.specs, rec.sample_rate, rec.duration_s, seed=rec.noise_seed)
        lab.update({"recording": rec.name, "group": rec.group, "oracle": rec.oracle})
        wav.with_suffix(".json").write_text(json.dumps(lab, indent=2) + "\n")
        entries.append({"name": rec.name, "group": rec.group, "oracle": rec.oracle,
                        "wav": wav.name, "labels": wav.with_suffix(".json").name})
        print(f"wrote {wav.name}")
    (out_dir / "manifest.json").write_text(json.dumps({"suite": suite_name, "recordings": entries}, indent=2) + "\n")


def run_suite(out_dir: Path, bench: Path, front_ends) -> None:
    """Scores every recording of the manifest with kz4ap-bench, once per front end."""
    manifest = json.loads((out_dir / "manifest.json").read_text())
    for fe in front_ends:
        if fe not in FRONT_ENDS:
            raise ValueError(f"unknown front end {fe!r}")
        results = out_dir / "results" / fe
        results.mkdir(parents=True, exist_ok=True)
        for rec in manifest["recordings"]:
            cmd = [str(bench), str(out_dir / rec["wav"]), "--labels", str(out_dir / rec["labels"]),
                   "--json", str(results / f"{rec['name']}.json")]
            if rec["oracle"]:
                cmd.append("--oracle")
            if fe != "baseline":
                cmd += ["--front-end", fe]
            done = subprocess.run(cmd, capture_output=True, text=True)
            if done.returncode != 0:
                raise RuntimeError(f"kz4ap-bench failed on {rec['name']} ({fe}):\n{done.stderr}")
            print(f"{fe:9s} {rec['name']}: {done.stdout.strip().splitlines()[-1]}")


def crossing_snr(points, threshold: float) -> float | None:
    """S500 (dB) at which CER falls to threshold. points: (S500 dB, CER) pairs. Scanning
    down from the highest S500, the first point above threshold and the point above it
    bracket the crossing (linear interpolation in dB). None if the highest point already
    fails; the lowest S500 if no point fails (an upper bound)."""
    pts = sorted(points)
    if not pts or pts[-1][1] > threshold:
        return None
    for i in range(len(pts) - 1, 0, -1):
        lo, hi = pts[i - 1], pts[i]
        if lo[1] > threshold:
            return lo[0] + (lo[1] - threshold) / (lo[1] - hi[1]) * (hi[0] - lo[0])
    return pts[0][0]


def _ratio(num, den) -> float:
    return num / den if den else 0.0


def aggregate(rows) -> dict:
    """Pools scored signals by (front end, group, tag); rates are summed edits over summed symbols."""
    groups: dict = {}
    for r in rows:
        if not r["scored"]:
            continue
        key = (r["front_end"], r["group"], r["tag"])
        g = groups.setdefault(key, {"signals": 0, "detected": 0, "by_snr": {}, **{k: 0 for k in COUNT_KEYS}})
        g["signals"] += 1
        g["detected"] += int(r["detected"])
        for k in COUNT_KEYS:
            g[k] += r[k]
        edits, symbols = g["by_snr"].get(r["snr_db"], (0, 0))
        g["by_snr"][r["snr_db"]] = (edits + r["edits"], symbols + r["symbols"])
    out = {}
    for key, g in groups.items():
        points = [(snr, _ratio(e, s)) for snr, (e, s) in sorted(g["by_snr"].items())]
        out[key] = {
            "signals": g["signals"],
            "detected": g["detected"],
            "cer": _ratio(g["edits"], g["symbols"]),
            "char_cer": _ratio(g["char_edits"], g["chars"]),
            "space_error_rate": _ratio(g["space_edits"], g["spaces"]),
            "first_word_cer": _ratio(g["first_word_edits"], g["first_word_symbols"]),
            "cer_by_snr": points,
            "snr_at_cer": {f"{t:g}": crossing_snr(points, t) for t in CER_THRESHOLDS} if len(points) >= 3 else {},
        }
    return out


def load_results(out_dir: Path):
    """Per-signal rows and per-recording timings from every results/<front end>/ directory."""
    manifest = json.loads((out_dir / "manifest.json").read_text())
    rows, timings = [], []
    for fe_dir in sorted(p for p in (out_dir / "results").iterdir() if p.is_dir()):
        for rec in manifest["recordings"]:
            path = fe_dir / f"{rec['name']}.json"
            if not path.exists():
                continue
            result = json.loads(path.read_text())
            label_signals = json.loads((out_dir / rec["labels"]).read_text())["signals"]
            for sig in result["score"]["signals"]:
                label = label_signals[sig["index"]]
                rows.append({"front_end": fe_dir.name, "recording": rec["name"], "group": rec["group"],
                             "tag": label.get("tag", ""), "snr_db": sig["snr_db"], "scored": sig["scored"],
                             "detected": sig["track_id"] is not None, **{k: sig[k] for k in COUNT_KEYS}})
            timing = result.get("timing", {})
            channel_s = result.get("channel_seconds", 0.0)
            timings.append({"front_end": fe_dir.name, "channel_seconds": channel_s,
                            "cpu_s": timing.get("cpu_s", 0.0),
                            "decoder_s": timing.get("decoder_ms_per_channel_s", 0.0) * channel_s / 1000.0})
    return rows, timings


def cpu_summary(timings) -> dict:
    out: dict = {}
    for t in timings:
        c = out.setdefault(t["front_end"], {"channel_seconds": 0.0, "cpu_s": 0.0, "decoder_s": 0.0})
        for k in ("channel_seconds", "cpu_s", "decoder_s"):
            c[k] += t[k]
    return {fe: {"channel_seconds": c["channel_seconds"],
                 "cpu_ms_per_channel_s": 1000.0 * _ratio(c["cpu_s"], c["channel_seconds"]),
                 "decoder_ms_per_channel_s": 1000.0 * _ratio(c["decoder_s"], c["channel_seconds"])}
            for fe, c in out.items()}


def _db(x) -> str:
    return "—" if x is None else f"{x:.1f}"


def format_markdown(agg: dict, cpu: dict) -> str:
    lines = ["# Benchmark summary", "",
             "S₅₀₀: key-down carrier power over noise power in 500 Hz, dB. CER counts word spaces; "
             "character CER and space error rate split its edits; first-word CER scores the first word "
             "of each transmission. — : not reached, or fewer than three S₅₀₀ points.", ""]
    for group in sorted({k[1] for k in agg}):
        lines += [f"## {group}", "",
                  "| tag | front end | signals | detected | CER | character CER | space error rate | "
                  "first-word CER | S₅₀₀ at CER 0.10 (dB) | S₅₀₀ at CER 0.05 (dB) |",
                  "|---|---|---|---|---|---|---|---|---|---|"]
        for (fe, g, tag), v in sorted(agg.items(), key=lambda kv: (kv[0][2], kv[0][0])):
            if g != group:
                continue
            at = v["snr_at_cer"]
            lines.append(f"| {tag} | {fe} | {v['signals']} | {v['detected']} | {v['cer']:.3f} | "
                         f"{v['char_cer']:.3f} | {v['space_error_rate']:.3f} | {v['first_word_cer']:.3f} | "
                         f"{_db(at.get('0.1'))} | {_db(at.get('0.05'))} |")
        lines.append("")
    lines += ["## CPU", "",
              "| front end | channel-seconds (s) | CPU per channel-second (ms/s) | decoders per channel-second (ms/s) |",
              "|---|---|---|---|"]
    for fe, c in sorted(cpu.items()):
        lines.append(f"| {fe} | {c['channel_seconds']:.1f} | {c['cpu_ms_per_channel_s']:.3f} | "
                     f"{c['decoder_ms_per_channel_s']:.3f} |")
    return "\n".join(lines) + "\n"


def write_summary(out_dir: Path) -> None:
    rows, timings = load_results(out_dir)
    agg = aggregate(rows)
    cpu = cpu_summary(timings)
    groups = [{"front_end": fe, "group": g, "tag": tag, **v} for (fe, g, tag), v in sorted(agg.items())]
    (out_dir / "summary.json").write_text(json.dumps({"groups": groups, "cpu": cpu}, indent=2) + "\n")
    (out_dir / "summary.md").write_text(format_markdown(agg, cpu), encoding="utf-8")
    print(f"wrote {out_dir / 'summary.md'}")


def main(argv=None) -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)
    g = sub.add_parser("generate", help="write a suite's recordings and manifest")
    g.add_argument("--suite", choices=sorted(SUITES), required=True)
    g.add_argument("--seeds", type=int, default=1)
    g.add_argument("--out", type=Path, required=True)
    r = sub.add_parser("run", help="score every recording with kz4ap-bench")
    r.add_argument("--out", type=Path, required=True)
    r.add_argument("--bench", type=Path, required=True)
    r.add_argument("--front-end", dest="front_ends", action="append", choices=FRONT_ENDS)
    s = sub.add_parser("summarize", help="write summary.json and summary.md")
    s.add_argument("--out", type=Path, required=True)
    args = parser.parse_args(argv)
    if args.command == "generate":
        write_suite(SUITES[args.suite](args.seeds), args.out, args.suite)
    elif args.command == "run":
        run_suite(args.out, args.bench, args.front_ends or ["baseline"])
    else:
        write_summary(args.out)


if __name__ == "__main__":
    main()
```

- [ ] **Step 4: Run the tests to verify they pass**

Run: `.venv\Scripts\python -m pytest training -q`
Expected: all pass. If `test_full_suite_recordings_are_valid_and_uniquely_named` reports a signal that runs past the end (random keying ran long), shorten that builder's `available_s` by 2 s and re-run; do not lengthen the recording.

- [ ] **Step 5: Run the full suite once with the baseline**

```powershell
$env:PYTHONPATH = "training"
.venv\Scripts\python -m kz4ap_synth.suites generate --suite full --out build/suite/full
.venv\Scripts\python -m kz4ap_synth.suites run --out build/suite/full --bench build/windows/bench/Release/kz4ap-bench.exe --front-end baseline
.venv\Scripts\python -m kz4ap_synth.suites summarize --out build/suite/full
```
Expected: one `baseline <name>: CER …` line per recording (about 30), then `wrote build\suite\full\summary.md`. Open it: every group has a table; group A has S₅₀₀ crossings for 12, 25 and 40 WPM. These are the baseline's numbers before any front-end work; keep the file (it is under `build/`, not committed) for Task 12.

- [ ] **Step 6: Document**

In `docs/signal-processing.md`, section 11, add at the end:

```markdown
- **Suites** (`training/kz4ap_synth/suites.py`): `smoke` is the CI
  recording; `full` covers sensitivity (oracle, S₅₀₀ −10 … +20 dB at 12, 25
  and 40 WPM), fading (VE3NEA's grid), fists, speed changes, interference,
  tuning offsets and drift, strong signals, pauses, tune-up carriers,
  stations present from the first sample, crowded bands and a whole band.
  **S₅₀₀ at a CER threshold:** for a condition with at least three S₅₀₀
  points, CER per point is pooled over its stations; scanning down from the
  highest S₅₀₀, the first point above the threshold and the one above it
  bracket the crossing, interpolated linearly in dB. No crossing is
  reported if the highest point already fails; if none fails, the lowest
  point is reported (an upper bound).
```

In `README.md`, append to the Benchmark section:

```markdown
Named suites generate many recordings at once, score them, and summarize
CER, character and word-space errors, first-word errors, the S₅₀₀ where CER
crosses 0.10 and 0.05, and CPU time per channel-second:

    $env:PYTHONPATH = "training"
    .venv\Scripts\python -m kz4ap_synth.suites generate --suite full --out build/suite/full
    .venv\Scripts\python -m kz4ap_synth.suites run --out build/suite/full --bench build\windows\bench\Release\kz4ap-bench.exe --front-end baseline
    .venv\Scripts\python -m kz4ap_synth.suites summarize --out build/suite/full
```

- [ ] **Step 7: Commit on the `milestone-2` branch**

```powershell
git add training/kz4ap_synth/suites.py training/tests/test_suites.py README.md docs/signal-processing.md
```
```powershell
git commit -m "Add named benchmark suites with a runner and summary"
```

---

### Task 8: Frequency tracker

The prerequisite of spec §5.2 step 1: re-center each station finely and follow its drift. `FrequencyTracker` is a numerically controlled oscillator (NCO) plus a lag-product frequency discriminator, as specified under "Design decisions, B". This task builds and tests it on its own; Task 10 puts it in front of the matched filter.

**Files:**
- Create: `engine/include/kz4ap/frequency_tracker.hpp`, `engine/src/frequency_tracker.cpp`
- Test: `engine/tests/frequency_tracker_test.cpp` (new)
- Modify: `engine/CMakeLists.txt`, `docs/signal-processing.md` (§0, §7, §10)

**Interfaces:**
- Produces (C++, namespace `kz4ap`):
  - `struct FrequencyTrackerConfig { int lag = 8; double tau_s = 0.5; double min_weight = 0.3; double max_offset_hz = 75.0; int update_every = 32; };`
  - `class FrequencyTracker` with `FrequencyTracker(double sample_rate, double initial_offset_hz, FrequencyTrackerConfig config = {})`, `Sample mix(Sample y)`, `void observe(Sample v, float weight)`, `double offset_hz() const`, `void reset(double initial_offset_hz)`.
  - Contract: `mix` returns y·e^(−jφ) and advances φ by 2π·offset_hz()/sample_rate. `observe` takes the narrow-filtered output of the mixed stream and a weight in [0, 1] (the key-down probability); a weight of 0 changes nothing. The offset is updated every `update_every` calls to `observe`, only once the average holds at least `min_weight`, and is clamped to ±`max_offset_hz`.

- [ ] **Step 1: Write the failing tests**

Create `engine/tests/frequency_tracker_test.cpp`:

```cpp
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
    FrequencyTracker tracker(kRate, 0.0);
    feed(tracker, Tone{-20.0}, 0, seconds(3.0), 1.0f);
    EXPECT_NEAR(tracker.offset_hz(), -20.0, 0.05);
}

TEST(FrequencyTracker, FollowsSlowDrift) {
    FrequencyTracker tracker(kRate, 5.0);
    const Tone tone{5.0, 1.0};
    const auto end = feed(tracker, tone, 0, seconds(20.0), 1.0f);
    EXPECT_NEAR(tracker.offset_hz(), tone.freq_at(end), 1.0);  // lags by about slope x tau_s = 0.5 Hz
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
    FrequencyTracker tracker(kRate, 0.0);
    feed(tracker, Tone{85.0}, 0, seconds(3.0), 1.0f);
    EXPECT_DOUBLE_EQ(tracker.offset_hz(), 75.0);
}

TEST(FrequencyTracker, EstimateIsAccurateInNoise) {
    FrequencyTracker tracker(kRate, 0.0);
    // 10 dB SNR per sample at 1500 samples/s
    feed(tracker, Tone{9.0}, 0, seconds(10.0), 1.0f, std::sqrt(0.1));
    EXPECT_NEAR(tracker.offset_hz(), 9.0, 1.0);
}

TEST(FrequencyTracker, ResetReturnsToTheGivenOffset) {
    FrequencyTracker tracker(kRate, 0.0);
    feed(tracker, Tone{9.0}, 0, seconds(3.0), 1.0f);
    tracker.reset(3.0);
    EXPECT_EQ(tracker.offset_hz(), 3.0);
}

TEST(FrequencyTracker, RejectsInvalidConfig) {
    EXPECT_THROW(FrequencyTracker(0.0, 0.0), std::invalid_argument);
    FrequencyTrackerConfig c;
    c.lag = 0;
    EXPECT_THROW(FrequencyTracker(kRate, 0.0, c), std::invalid_argument);
    c = {};
    c.tau_s = 0;
    EXPECT_THROW(FrequencyTracker(kRate, 0.0, c), std::invalid_argument);
    c = {};
    c.min_weight = 1.0;
    EXPECT_THROW(FrequencyTracker(kRate, 0.0, c), std::invalid_argument);
    c = {};
    c.max_offset_hz = 100.0;  // beyond the +/-93.75 Hz unambiguous range of lag 8 at 1500 samples/s
    EXPECT_THROW(FrequencyTracker(kRate, 0.0, c), std::invalid_argument);
    c = {};
    c.update_every = 0;
    EXPECT_THROW(FrequencyTracker(kRate, 0.0, c), std::invalid_argument);
}
```

Add `tests/frequency_tracker_test.cpp` to `kz4ap_engine_tests` and `src/frequency_tracker.cpp` to `kz4ap_engine` in `engine/CMakeLists.txt`.

- [ ] **Step 2: Run the tests to verify they fail**

Run: `cmake --build --preset windows`
Expected: compile error, `kz4ap/frequency_tracker.hpp: No such file or directory`.

- [ ] **Step 3: Implement**

Create `engine/include/kz4ap/frequency_tracker.hpp`:

```cpp
#pragma once

#include "kz4ap/types.hpp"

#include <complex>
#include <cstddef>
#include <vector>

namespace kz4ap {

struct FrequencyTrackerConfig {
    int lag = 8;                  // L, samples between the two samples of each phase difference
    double tau_s = 0.5;           // time constant of the average, s at weight 1
    double min_weight = 0.3;      // the average must hold this much weight (0..1) before it moves the NCO
    double max_offset_hz = 75.0;  // the NCO frequency is clamped to +/- this, Hz
    int update_every = 32;        // samples between NCO frequency updates
};

// Re-centers one station's channel on its carrier. A numerically controlled
// oscillator (NCO) mixes the channel down by f, the estimated residual offset
// of the carrier from the channel's center, Hz. f starts at the detector's
// estimate and then follows the carrier: the phase advance of the narrow-
// filtered, re-centered stream over `lag` samples measures what is left of the
// offset, the NCO's own advance is added back to make it absolute, and the
// result is averaged with the key-down probability as weight.
class FrequencyTracker {
public:
    // Throws std::invalid_argument for a non-positive sample rate, an invalid config,
    // or a max_offset_hz outside the unambiguous range +/- sample_rate / (2 lag).
    FrequencyTracker(double sample_rate, double initial_offset_hz, FrequencyTrackerConfig config = {});

    // Mixes one channel sample down by the current offset and advances the NCO.
    Sample mix(Sample y);

    // Feeds one narrow-filtered sample of the mixed stream. weight (0..1) is the
    // probability that the key is down; 0 leaves the estimate unchanged.
    void observe(Sample v, float weight);

    double offset_hz() const { return offset_hz_; }  // the NCO frequency, Hz
    void reset(double initial_offset_hz);

private:
    double rate_;
    FrequencyTrackerConfig config_;
    double alpha_;
    double offset_hz_ = 0;
    double phase_ = 0;                // NCO phase, rad, kept in [-pi, pi)
    std::vector<Sample> history_;     // the last `lag` observed samples, circular
    std::size_t head_ = 0;
    std::size_t seen_ = 0;
    std::complex<double> average_{};  // weighted average of lag products at the absolute offset, FS^2
    double weight_ = 0;               // weight the average holds, 0..1
    int until_update_ = 0;
};

}  // namespace kz4ap
```

Create `engine/src/frequency_tracker.cpp`:

```cpp
#include "kz4ap/frequency_tracker.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>

namespace kz4ap {
namespace {

constexpr double kTwoPi = 2 * std::numbers::pi;

// Validates the parameters and returns sample_rate. Runs as rate_'s initializer,
// before alpha_ divides by them.
double validated_rate(double sample_rate, const FrequencyTrackerConfig& c) {
    if (!(sample_rate > 0) || c.lag < 1 || !(c.tau_s > 0) || !(c.min_weight >= 0) || !(c.min_weight < 1) ||
        !(c.max_offset_hz > 0) || c.update_every < 1)
        throw std::invalid_argument("invalid frequency tracker config");
    if (!(c.max_offset_hz < sample_rate / (2.0 * c.lag)))
        throw std::invalid_argument("frequency tracker max_offset_hz must be below sample_rate / (2 lag)");
    return sample_rate;
}

}  // namespace

FrequencyTracker::FrequencyTracker(double sample_rate, double initial_offset_hz, FrequencyTrackerConfig config)
    : rate_(validated_rate(sample_rate, config)),
      config_(config),
      alpha_(1.0 - std::exp(-1.0 / (config.tau_s * sample_rate))) {
    reset(initial_offset_hz);
}

void FrequencyTracker::reset(double initial_offset_hz) {
    offset_hz_ = std::clamp(initial_offset_hz, -config_.max_offset_hz, config_.max_offset_hz);
    phase_ = 0;
    history_.assign(static_cast<std::size_t>(config_.lag), Sample{});
    head_ = 0;
    seen_ = 0;
    average_ = {};
    weight_ = 0;
    until_update_ = config_.update_every;
}

Sample FrequencyTracker::mix(Sample y) {
    const Sample lo(static_cast<float>(std::cos(phase_)), static_cast<float>(-std::sin(phase_)));
    phase_ += kTwoPi * offset_hz_ / rate_;
    if (phase_ >= std::numbers::pi) phase_ -= kTwoPi;
    else if (phase_ < -std::numbers::pi) phase_ += kTwoPi;
    return y * lo;
}

void FrequencyTracker::observe(Sample v, float weight) {
    const Sample old = history_[head_];
    history_[head_] = v;
    head_ = (head_ + 1) % history_.size();
    const double w = std::clamp(static_cast<double>(weight), 0.0, 1.0);
    if (seen_ < history_.size()) {
        ++seen_;  // `old` is not a real sample yet
    } else if (w > 0) {
        // The product's phase is the residual advance over `lag` samples; adding the
        // NCO's own advance turns it into a measurement of the absolute offset, so
        // the average does not depend on where the NCO happens to be.
        const std::complex<double> z = std::complex<double>(v) * std::conj(std::complex<double>(old));
        const std::complex<double> absolute = z * std::polar(1.0, kTwoPi * offset_hz_ * config_.lag / rate_);
        const double a = alpha_ * w;
        average_ += a * (absolute - average_);
        weight_ += a * (1.0 - weight_);
    }
    if (--until_update_ == 0) {
        until_update_ = config_.update_every;
        if (weight_ >= config_.min_weight && std::abs(average_) > 0) {
            offset_hz_ = std::clamp(std::arg(average_) * rate_ / (kTwoPi * config_.lag), -config_.max_offset_hz,
                                    config_.max_offset_hz);
        }
    }
}

}  // namespace kz4ap
```

- [ ] **Step 4: Build and run the tests**

```powershell
cmake --build --preset windows
ctest --preset windows -R FrequencyTracker
```
Expected: 10 tests pass. If `EstimateIsAccurateInNoise` fails, do not widen its tolerance: print the error and compare it with the derived estimate in "Design decisions, B" (about 0.25 Hz RMS here); a large disagreement means a bug in the rotation or the averaging.

- [ ] **Step 5: Document**

In `docs/signal-processing.md`, add to the symbols table in section 0:

```markdown
| f̂ | frequency tracker's estimate of a station's residual offset from its channel center, and the NCO frequency | Hz |
| L | lag of the frequency discriminator | 8 samples |
| τ_f | time constant of the frequency average, counted in samples of weight 1 | 0.5 s |
```

At the end of section 7, after "Residual frequency offset", add:

```markdown
### Frequency re-centering (Matched front end only)

Used only when the decoder's front end is `Matched` (section 8b); the
default pipeline does not re-center. `FrequencyTracker`
(frequency_tracker.cpp) runs per station at r = 1500 samples/s:

- **NCO (derived):** u[n] = y[n]·e^(−jφ[n]), φ advancing by 2π·f̂/r per
  sample. f̂ starts at the detector's residual (track frequency minus the
  channel's center; about 0.2 Hz error measured for a clean station) and
  is clamped to ±75 Hz, where the channel filter is 0.34 dB down relative
  to the passband (**heuristic**; beyond it the channel itself would have to
  move, which the code does not do).
- **Discriminator (derived):** on the narrow-filtered, re-centered stream
  v[n], the product z = v[n]·conj(v[n−L]) has phase 2π·(Δf − f̂)·L/r for a
  station at Δf. Rotating it by e^(j2π·f̂·L/r) makes it a measurement of Δf
  itself, so the average below does not depend on the NCO and there is no
  loop to stabilize. L = 8 gives an unambiguous range of ±r/(2L) =
  ±93.75 Hz (**derived**; the value 8 is **heuristic**).
- **Average (heuristic):** Z̄ ← Z̄ + α·p·(rotated z − Z̄), α = 1 − e^(−1/(τ_f·r)),
  τ_f = 0.5 s, weighted by p, the front end's key-down probability, so
  key-up and pauses leave it unchanged. Every 32 samples (21.3 ms)
  f̂ ← arg(Z̄)·r/(2πL), once the average holds weight 0.3 or more.
- **Expected accuracy (derived, approximate; not yet measured):** about
  0.5 Hz RMS at S₅₀₀ = 0 dB and 0.9 Hz at −5 dB, 25 WPM; a linear drift of
  ḟ Hz/s is followed with a lag of about ḟ·τ_f/P₁ (1.1 Hz at 1 Hz/s,
  P₁ = 0.44). Target (spec §5.2): within ±2 Hz, a loss of 0.2 dB relative
  to a centered station at 20 WPM through a filter of length T.
```

In section 10, add rows:

```markdown
| Frequency discriminator lag | L = 8 samples (±93.75 Hz unambiguous) | `FrequencyTrackerConfig::lag` | heuristic within derived range |
| Frequency average | τ_f = 0.5 s of key-down weight; moves the NCO at weight ≥ 0.3, every 32 samples | `FrequencyTrackerConfig` | heuristic |
| NCO range | ±75 Hz | `FrequencyTrackerConfig::max_offset_hz` | heuristic |
```

- [ ] **Step 6: Commit on the `milestone-2` branch**

```powershell
git add engine/include/kz4ap/frequency_tracker.hpp engine/src/frequency_tracker.cpp engine/tests/frequency_tracker_test.cpp engine/CMakeLists.txt docs/signal-processing.md
```
```powershell
git commit -m "Add a per-station frequency tracker: NCO and lag-product discriminator"
```

---

### Task 9: Matched front end

Spec §5.2 step 1 proper: a complex filter matched to the current dit estimate, before envelope detection, and a Rician-versus-Rayleigh log-likelihood ratio from running estimates of the noise and the key-down amplitude, as specified under "Design decisions, C". This task builds and tests `MatchedFrontEnd` on its own; Task 10 wires it into the decoder.

**Files:**
- Create: `engine/include/kz4ap/matched_front_end.hpp`, `engine/src/matched_front_end.cpp`
- Test: `engine/tests/matched_front_end_test.cpp` (new)
- Modify: `engine/CMakeLists.txt`, `docs/signal-processing.md` (§0, new §8b, §10)

**Interfaces:**
- Consumes: `kz4ap::test::keyed_signal`, `keying`, `duration_for` from `engine/tests/test_signals.hpp` (tests only).
- Produces (C++, namespace `kz4ap`):
  - `double log_bessel_i0(double z);` and `double envelope_llr(double x, double a);`
  - `struct MatchedFrontEndConfig { double initial_wpm = 60.0; double min_wpm = 5.0; double length_dits = 0.8; double warmup_s = 0.2; double noise_tau_s = 2.0; double amplitude_tau_s = 0.5; double noise_clip = 9.0; double noise_guard_log_odds = 1.0; double prior_key_down = 0.44; double squelch_a = 3.0; };`
  - `struct FrontEndSample { Sample filtered; float llr; float log_odds; float p_key_down; float weight; bool ready; bool signal; };`
  - `class MatchedFrontEnd` with `explicit MatchedFrontEnd(double sample_rate, MatchedFrontEndConfig config = {})`, `FrontEndSample step(Sample u)`, `void set_dit(double dit_s)`, `void reset()`, `int length() const`, `double noise_sigma() const`, `double amplitude() const`.
  - Contract: `filtered` is the mean of the last K inputs (FS); `llr` is Λ (nats); `log_odds` is Λ + ln(P₁/P₀); `p_key_down` is the posterior, forced to 0 while `signal` is false; `weight` is 1/K; `ready` is false during warm-up, when only `filtered` and `weight` are valid.

- [ ] **Step 1: Write the failing tests**

Create `engine/tests/matched_front_end_test.cpp`:

```cpp
#include "kz4ap/matched_front_end.hpp"

#include "test_signals.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <complex>
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
    EXPECT_EQ(fe.length(), 58);  // 0.8 x 48 ms x 1500 samples/s = 57.6
    fe.set_dit(10.0);
    EXPECT_EQ(fe.length(), 288);  // clamped at 5 WPM
}

TEST(MatchedFrontEnd, SteadyToneComesOutAtItsAmplitude) {
    MatchedFrontEnd fe(kRate);
    FrontEndSample out;
    for (int i = 0; i < 100; ++i) out = fe.step(Sample(0.3f, 0.4f));
    EXPECT_NEAR(std::abs(out.filtered), 0.5, 1e-6);
    EXPECT_FLOAT_EQ(out.weight, 1.0f / 24.0f);
    EXPECT_FALSE(out.ready);  // still warming up (0.2 s = 300 samples)
}

TEST(MatchedFrontEnd, CorrelationOfFilteredNoiseSumsToTheFilterLength) {
    MatchedFrontEnd fe(kRate);
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
    const std::string msg = "PARIS PARIS PARIS PARIS PARIS";
    const auto x = keyed_signal(msg, 25, kRate, duration_for(msg, 25), 0, 1.0, 1.0, 5);
    MatchedFrontEnd fe(kRate);
    fe.set_dit(0.048);
    run(fe, x);
    const double expected = boxcar_sigma(1.0, fe.length());
    EXPECT_NEAR(fe.noise_sigma(), expected, 0.2 * expected);
    EXPECT_GT(fe.amplitude(), 0.75);
    EXPECT_LT(fe.amplitude(), 1.1);
}

TEST(MatchedFrontEnd, LlrSeparatesKeyDownFromKeyUp) {
    const std::string msg = "PARIS PARIS PARIS PARIS PARIS";
    const auto x = keyed_signal(msg, 25, kRate, duration_for(msg, 25), 0, 1.0, 1.0, 6);
    MatchedFrontEnd fe(kRate);
    fe.set_dit(0.048);
    const auto out = run(fe, x);
    const double delay_s = (fe.length() - 1) / 2.0 / kRate;  // the boxcar's group delay
    const auto marks = keying(msg, 25, 0.5);
    for (std::size_t i = 2; i + 1 < marks.size(); ++i) {  // skip the first marks: the estimates are still settling
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

TEST(MatchedFrontEnd, NoiseAloneStaysSquelched) {
    MatchedFrontEnd fe(kRate);
    std::size_t signal = 0;
    std::size_t ready = 0;
    for (const auto& o : run(fe, white_noise(static_cast<std::size_t>(30 * kRate), 1.0, 7))) {
        if (!o.ready) continue;
        ++ready;
        if (o.signal) ++signal;
        if (!o.signal) EXPECT_EQ(o.p_key_down, 0.0f);
    }
    EXPECT_LT(signal, ready / 1000);
}

TEST(MatchedFrontEnd, StrongSignalKeepsNoiseEstimate) {
    // S500 = 60 dB: key-down power over the noise in 500 Hz of a white 1500 samples/s stream.
    const double sigma_in = std::sqrt(3.0 * 1e-6);
    const std::string msg = "PARIS PARIS PARIS PARIS PARIS";
    const auto x = keyed_signal(msg, 25, kRate, duration_for(msg, 25), 0, 1.0, sigma_in, 8);
    MatchedFrontEnd fe(kRate);
    fe.set_dit(0.048);
    run(fe, x);
    const double expected = boxcar_sigma(sigma_in, fe.length());
    EXPECT_GT(fe.noise_sigma(), expected / 1.5);
    EXPECT_LT(fe.noise_sigma(), expected * 1.5);
    EXPECT_GT(fe.amplitude(), 0.75);
    EXPECT_LT(fe.amplitude(), 1.1);
}

TEST(MatchedFrontEnd, NoiseEstimateScalesWhenTheFilterChanges) {
    MatchedFrontEnd fe(kRate);  // K = 24
    run(fe, white_noise(static_cast<std::size_t>(5 * kRate), 1.0, 9));
    fe.set_dit(0.048);  // K = 58
    const double expected = boxcar_sigma(1.0, 58);
    EXPECT_NEAR(fe.noise_sigma(), expected, 0.15 * expected);
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
    c.noise_clip = 1.0;
    EXPECT_THROW(MatchedFrontEnd(kRate, c), std::invalid_argument);
}
```

Add `tests/matched_front_end_test.cpp` to `kz4ap_engine_tests` and `src/matched_front_end.cpp` to `kz4ap_engine` in `engine/CMakeLists.txt`.

- [ ] **Step 2: Run the tests to verify they fail**

Run: `cmake --build --preset windows`
Expected: compile error, `kz4ap/matched_front_end.hpp: No such file or directory`.

- [ ] **Step 3: Implement**

Create `engine/include/kz4ap/matched_front_end.hpp`:

```cpp
#pragma once

#include "kz4ap/types.hpp"

#include <complex>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace kz4ap {

// ln I0(z), I0 the modified Bessel function of the first kind, order zero, for
// any z >= 0 without overflow. Abramowitz & Stegun 9.8.1 (z < 3.75) and 9.8.2;
// relative error in I0 below 2e-7.
double log_bessel_i0(double z);

// Log-likelihood ratio, nats, of key-down (Rician envelope) against key-up
// (Rayleigh envelope) for the normalized envelope x = R / sigma and the
// normalized key-down amplitude a = s / sigma: -a^2/2 + ln I0(a x)
// (Proakis & Salehi, eq. 4.5-21, the on-off keying case).
double envelope_llr(double x, double a);

struct MatchedFrontEndConfig {
    double initial_wpm = 60.0;          // filter length until the decoder's speed is trusted: the fastest code
    double min_wpm = 5.0;               // sets the longest filter
    double length_dits = 0.8;           // beta: filter length as a fraction of the dit
    double warmup_s = 0.2;              // collected before the first noise and amplitude estimates, s
    double noise_tau_s = 2.0;           // noise estimate's time constant, s of noise updates
    double amplitude_tau_s = 0.5;       // amplitude estimate's time constant, s of key-down weight
    double noise_clip = 9.0;            // noise updates skip samples with |v|^2 / (2 sigma^2) above this
    double noise_guard_log_odds = 1.0;  // no noise update within 2K samples of log-odds above this, nats
    double prior_key_down = 0.44;       // P1: PARIS keys down 22 of 50 dit units
    double squelch_a = 3.0;             // key-down evidence is ignored while a = s / sigma is below this
};

struct FrontEndSample {
    Sample filtered;       // v: matched-filter output, FS
    float llr = 0;         // Lambda = ln p1(x) / p0(x), nats (no prior)
    float log_odds = 0;    // g = Lambda + ln(P1 / P0), nats
    float p_key_down = 0;  // posterior probability of key-down, 0..1; 0 while squelched or warming up
    float weight = 0;      // 1/K: scale each llr by this before summing over samples (they are correlated)
    bool ready = false;    // warm-up is over; llr, log_odds and p_key_down are valid
    bool signal = false;   // a >= squelch_a
};

// One station's front end at the channel rate: a boxcar filter matched to the
// dit (K = round(beta * dit * rate) samples, normalized to unity gain), then the
// envelope, then a Rician-versus-Rayleigh log-likelihood ratio from running
// estimates of the noise and of the key-down amplitude.
class MatchedFrontEnd {
public:
    // Throws std::invalid_argument for a non-positive sample rate or an invalid config.
    explicit MatchedFrontEnd(double sample_rate, MatchedFrontEndConfig config = {});

    FrontEndSample step(Sample u);
    void set_dit(double dit_s);  // follows the decoder's dit estimate, s
    void reset();

    int length() const { return length_; }  // K, samples
    double noise_sigma() const;             // sigma: noise RMS per real component of v, FS
    double amplitude() const;               // s: key-down amplitude of v, FS

private:
    int length_for(double dit_s) const;
    void resum();
    void start_estimates();
    void update_noise();

    double rate_;
    MatchedFrontEndConfig config_;
    double noise_alpha_;
    double amplitude_alpha_;
    double log_prior_odds_;
    int max_length_;
    int length_ = 1;
    std::vector<Sample> ring_;          // the last max_length_ inputs, circular
    std::size_t head_ = 0;              // where the next input goes
    std::complex<double> sum_{};        // sum of the last length_ inputs
    std::size_t since_resum_ = 0;
    std::vector<double> power_ring_;    // |v|^2 of the last max_length_ + 1 outputs, circular
    std::uint64_t count_ = 0;           // samples stepped since reset
    std::uint64_t last_key_down_ = 0;   // count_ when log-odds last exceeded noise_guard_log_odds
    std::size_t warmup_left_ = 0;
    std::vector<double> warmup_power_;
    double noise_var_ = 0;              // sigma^2, FS^2
    double amp2_ = 0;                   // s^2, FS^2
    double noise_weight_ = 0;           // W_n: noise updates so far (the warm-up counts as some)
    double amplitude_weight_ = 0;       // W_a: key-down weight so far (the warm-up counts as some)
};

}  // namespace kz4ap
```

Create `engine/src/matched_front_end.cpp`:

```cpp
#include "kz4ap/matched_front_end.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace kz4ap {
namespace {

constexpr std::size_t kResumEvery = 4096;             // recompute the running sum this often, samples
constexpr double kMinNoiseVar = 1e-20;                // FS^2 (-200 dBFS): keeps x and a finite on noise-free input
constexpr double kMinusLn08 = 0.22314355131420976;    // -ln 0.8: an exponential's 20th percentile, in means

// Validates the parameters and returns sample_rate. Runs as rate_'s initializer,
// before anything divides by them.
double validated_rate(double sample_rate, const MatchedFrontEndConfig& c) {
    if (!(sample_rate > 0) || !(c.min_wpm > 0) || !(c.initial_wpm >= c.min_wpm) || !(c.length_dits > 0) ||
        !(c.warmup_s > 0) || !(c.noise_tau_s > 0) || !(c.amplitude_tau_s > 0) || !(c.noise_clip > 1) ||
        !(c.prior_key_down > 0) || !(c.prior_key_down < 1) || !(c.squelch_a >= 0))
        throw std::invalid_argument("invalid matched front end config");
    return sample_rate;
}

double alpha_for(double tau_s, double rate) { return 1.0 - std::exp(-1.0 / (tau_s * rate)); }

double logistic(double g) { return 1.0 / (1.0 + std::exp(-std::clamp(g, -50.0, 50.0))); }

}  // namespace

double log_bessel_i0(double z) {
    z = std::abs(z);
    if (z < 3.75) {
        const double t = (z / 3.75) * (z / 3.75);
        return std::log(1.0 + t * (3.5156229 + t * (3.0899424 + t * (1.2067492 + t * (0.2659732 +
                                                                                     t * (0.0360768 + t * 0.0045813))))));
    }
    const double t = 3.75 / z;
    const double poly =
        0.39894228 +
        t * (0.01328592 +
             t * (0.00225319 +
                  t * (-0.00157565 +
                       t * (0.00916281 + t * (-0.02057706 + t * (0.02635537 + t * (-0.01647633 + t * 0.00392377)))))));
    return z - 0.5 * std::log(z) + std::log(poly);
}

double envelope_llr(double x, double a) { return -0.5 * a * a + log_bessel_i0(a * x); }

MatchedFrontEnd::MatchedFrontEnd(double sample_rate, MatchedFrontEndConfig config)
    : rate_(validated_rate(sample_rate, config)),
      config_(config),
      noise_alpha_(alpha_for(config.noise_tau_s, sample_rate)),
      amplitude_alpha_(alpha_for(config.amplitude_tau_s, sample_rate)),
      log_prior_odds_(std::log(config.prior_key_down / (1.0 - config.prior_key_down))),
      max_length_(std::max(1, static_cast<int>(std::lround(config.length_dits * 1.2 / config.min_wpm * sample_rate)))) {
    reset();
}

int MatchedFrontEnd::length_for(double dit_s) const {
    const long k = std::lround(config_.length_dits * dit_s * rate_);
    return static_cast<int>(std::clamp<long>(k, 1, max_length_));
}

void MatchedFrontEnd::reset() {
    ring_.assign(static_cast<std::size_t>(max_length_), Sample{});
    head_ = 0;
    length_ = length_for(1.2 / config_.initial_wpm);
    sum_ = {};
    since_resum_ = 0;
    power_ring_.assign(static_cast<std::size_t>(max_length_) + 1, 0.0);
    count_ = 0;
    last_key_down_ = 0;
    warmup_left_ = std::max<std::size_t>(1, static_cast<std::size_t>(std::lround(config_.warmup_s * rate_)));
    warmup_power_.clear();
    noise_var_ = amp2_ = 0;
    noise_weight_ = amplitude_weight_ = 0;
}

double MatchedFrontEnd::noise_sigma() const { return std::sqrt(noise_var_); }

double MatchedFrontEnd::amplitude() const { return std::sqrt(amp2_); }

void MatchedFrontEnd::resum() {
    const std::size_t n = ring_.size();
    sum_ = {};
    for (int i = 1; i <= length_; ++i) sum_ += std::complex<double>(ring_[(head_ + n - static_cast<std::size_t>(i)) % n]);
    since_resum_ = 0;
}

void MatchedFrontEnd::set_dit(double dit_s) {
    const int k = length_for(dit_s);
    if (k == length_) return;
    // The boxcar's output noise power is proportional to 1/K for noise flat across its passband.
    if (warmup_left_ == 0) noise_var_ = std::max(kMinNoiseVar, noise_var_ * length_ / k);
    length_ = k;
    resum();
}

void MatchedFrontEnd::start_estimates() {
    std::vector<double> sorted = warmup_power_;
    std::sort(sorted.begin(), sorted.end());
    const auto quantile = [&](double q) {
        return sorted[static_cast<std::size_t>(q * static_cast<double>(sorted.size() - 1))];
    };
    // In noise alone |v|^2 is exponential with mean 2 sigma^2; its 20th percentile is 2 sigma^2 (-ln 0.8).
    noise_var_ = std::max(kMinNoiseVar, quantile(0.2) / (2.0 * kMinusLn08));
    amp2_ = std::max(0.0, quantile(0.9) - 2.0 * noise_var_);
    // The warm-up counts as this much weight, so the samples after it refine the estimates.
    noise_weight_ = 0.8 * static_cast<double>(sorted.size());
    amplitude_weight_ = 0.1 * static_cast<double>(sorted.size());
    warmup_power_.clear();
}

void MatchedFrontEnd::update_noise() {
    // v[count_ - 1 - K] averages inputs that hold no mark if the log-odds stayed low for
    // the last 2K samples. Filter ramps (a mark entering or leaving the window) would
    // otherwise pass as noise: their log-odds stay low until |v| nears s/2.
    const auto k = static_cast<std::uint64_t>(length_);
    if (count_ <= k || count_ - last_key_down_ <= 2 * k) return;
    const double past = power_ring_[(count_ - 1 - k) % power_ring_.size()];
    if (past > config_.noise_clip * 2.0 * noise_var_) return;
    noise_weight_ += 1.0;
    noise_var_ = std::max(kMinNoiseVar,
                          noise_var_ + std::max(noise_alpha_, 1.0 / noise_weight_) * (0.5 * past - noise_var_));
}

FrontEndSample MatchedFrontEnd::step(Sample u) {
    // Boxcar: add the new input, drop the one length_ inputs back.
    const std::size_t n = ring_.size();
    const Sample leaving = ring_[(head_ + n - static_cast<std::size_t>(length_)) % n];
    ring_[head_] = u;
    head_ = (head_ + 1) % n;
    sum_ += std::complex<double>(u) - std::complex<double>(leaving);
    if (++since_resum_ >= kResumEvery) resum();

    FrontEndSample out;
    const std::complex<double> v = sum_ / static_cast<double>(length_);
    out.filtered = Sample(static_cast<float>(v.real()), static_cast<float>(v.imag()));
    out.weight = 1.0f / static_cast<float>(length_);
    const double power = std::norm(v);
    power_ring_[count_ % power_ring_.size()] = power;
    ++count_;
    if (warmup_left_ > 0) {
        warmup_power_.push_back(power);
        if (--warmup_left_ == 0) start_estimates();
        return out;
    }

    const double sigma = std::sqrt(noise_var_);
    const double a = std::sqrt(amp2_) / sigma;
    const double llr = envelope_llr(std::sqrt(power) / sigma, a);
    const double g = llr + log_prior_odds_;
    const double p = logistic(g);
    out.ready = true;
    out.signal = a >= config_.squelch_a;
    out.llr = static_cast<float>(llr);
    out.log_odds = static_cast<float>(g);
    out.p_key_down = out.signal ? static_cast<float>(p) : 0.0f;

    if (g > config_.noise_guard_log_odds) last_key_down_ = count_;
    update_noise();
    // Online EM for the Rician component: its mean square is 2 sigma^2 + s^2.
    amplitude_weight_ += p;
    const double step = p * std::max(amplitude_alpha_, 1.0 / amplitude_weight_);
    amp2_ = std::max(0.0, amp2_ + step * (power - 2.0 * noise_var_ - amp2_));
    return out;
}

}  // namespace kz4ap
```

- [ ] **Step 4: Build and run the tests**

```powershell
cmake --build --preset windows
ctest --preset windows -R MatchedFrontEnd
```
Expected: 13 tests pass. These tests pin behavior the design derived; if one fails, find out why before touching a tolerance (superpowers:systematic-debugging). In particular, `EstimatesNoiseAndAmplitude` and `StrongSignalKeepsNoiseEstimate` fail if the noise guard lets filter ramps through, and `NoiseAloneStaysSquelched` fails if the amplitude estimate does not decay in noise alone; if the squelch is the problem, measure the value of a = amplitude()/noise_sigma() reached in noise, set `squelch_a` to 1.5 times its maximum, and record the measurement in signal-processing.md §8b.

- [ ] **Step 5: Document**

In `docs/signal-processing.md`, add to the symbols table in section 0:

```markdown
| K | matched-filter (boxcar) length, round(β·dit·r) | samples |
| β | matched-filter length as a fraction of the dit | 0.8 |
| v[n] | matched-filter output | FS |
| σ, s | noise RMS per real component of v, and v's key-down amplitude | FS |
| x, a | normalized envelope \|v\|/σ and amplitude s/σ | dimensionless |
| Λ | log-likelihood ratio, key-down over key-up: −a²/2 + ln I₀(a·x) | nats |
| g, p | posterior log-odds Λ + ln(P₁/P₀), and the posterior probability of key-down | nats, 0…1 |
| P₁ | prior probability of key-down | 0.44 |
```

After section 8, add a new section:

```markdown
## 8b. Matched front end (optional, per station)

Used only when `ClassicalDecoderConfig::front_end` is `Matched`; the default
decoder (section 8) does not use it. `MatchedFrontEnd`
(matched_front_end.cpp) runs per station at r = 1500 samples/s on the
re-centered stream u[n] (section 7, "Frequency re-centering"), before any
envelope is taken.

- **Filter (derived):** a boxcar (moving average) of K samples, normalized
  to unity gain. A boxcar of duration T is the matched filter of a
  rectangular element of duration T, with noise bandwidth exactly 1/T
  (Proakis §4.2–2; proakis-ook-notes.md §2.7). K = round(β·dit·r), β = 0.8
  (**heuristic**: 0.97 dB of output SNR below the matched filter, derived,
  in exchange for staying shorter than an element space when the speed
  estimate is up to 25% slow). The noise bandwidth is r/K: 25.9 Hz at
  25 WPM (K = 58), 10·log₁₀(252/25.9) = 9.9 dB less noise than the channel
  filter passes (derived).
- **Following speed (heuristic):** K starts at 60 WPM (K = 24, 62.5 Hz),
  the widest filter, and follows the decoder's dit estimate once its speed
  window holds 8 marks; K is clamped to 288 (5 WPM). When K changes, σ̂² is
  scaled by K_old/K_new (derived for noise flat across the filter).
- **Likelihood (derived; Proakis eq. 4.5–21):** Λ = −a²/2 + ln I₀(a·x),
  x = |v|/σ̂, a = ŝ/σ̂, in nats; ln I₀ from Abramowitz & Stegun 9.8.1–9.8.2
  without overflow. g = Λ + ln(P₁/P₀) with P₁ = 0.44 (derived from PARIS:
  key-down 22 of 50 dit units); p = 1/(1 + e^(−g)). For noise flat across
  the filter, a² = 2·S₅₀₀·(500 Hz)·K/r, S₅₀₀ as a linear ratio (derived):
  a = 6.2 at S₅₀₀ = 0 dB and 25 WPM.
- **Amplitude estimate (heuristic running form of an EM update):**
  ŝ² ← max(0, ŝ² + p·max(α_a, 1/W_a)·(|v|² − 2σ̂² − ŝ²)), the Rician mean
  square being 2σ² + s²; τ_a = 0.5 s of key-down weight. Ramp samples bias
  ŝ low by about 11% at high SNR (derived; not yet measured).
- **Noise estimate (heuristic):** σ̂² ← σ̂² + max(α_n, 1/W_n)·(|v[n−K]|²/2 − σ̂²),
  τ_n = 2 s, using the sample K back only if g stayed at or below +1 nat
  for the last 2K samples (its window holds no mark) and
  |v[n−K]|²/(2σ̂²) ≤ 9 (drops 1.2×10⁻⁴ of pure noise, biasing σ̂² 0.005 dB below the true noise power,
  derived). The guard keeps filter ramps, which look like noise to the
  likelihood until |v| nears s/2, out of the estimate; at S₅₀₀ = 60 dB they
  would otherwise inflate σ̂² by orders of magnitude.
- **Warm-up (heuristic):** the first 0.2 s only collect |v|². Then
  σ̂² = (20th percentile)/(2·(−ln 0.8)) (derived for noise alone) and
  ŝ² = max(0, 90th percentile − 2σ̂²), and the warm-up counts as 0.8 and
  0.1 times its length in noise and amplitude weight.
- **Squelch (heuristic):** p is forced to 0 while a < 3 (E/N₀ = a²/2 =
  6.5 dB re 1 for a matched filter; about S₅₀₀ = −6.3 dB at 25 WPM). In
  noise alone the amplitude update shrinks a² by about P₀ = 0.56 per time
  constant (derived for small a), so a decays toward 0.
- **Correlated samples (derived):** successive outputs of a K-sample boxcar
  share inputs; their noise autocorrelation is triangular and sums to K.
  Each `FrontEndSample` carries weight 1/K, the factor a sequence decoder
  must apply to Λ before summing over samples (scaling rather than
  decimation keeps the 0.67 ms timing resolution). The baseline decoder
  keys from g sample by sample and does not sum, so it ignores the weight.
```

In section 10, add rows:

```markdown
| Matched filter | boxcar, K = round(0.8·dit·r), starts at 60 WPM, max 288 samples | `MatchedFrontEndConfig` | derived shape; β and start heuristic |
| Likelihood | Λ = −a²/2 + ln I₀(a·x); prior P₁ = 0.44 | matched_front_end.cpp | derived |
| Amplitude / noise estimates | τ_a = 0.5 s (EM, p-weighted) / τ_n = 2 s (guard 2K at g > 1 nat, clip 9) | `MatchedFrontEndConfig` | heuristic |
| Front-end warm-up | 0.2 s; 20th / 90th percentiles | `MatchedFrontEndConfig::warmup_s` | heuristic |
| Front-end squelch | a ≥ 3 | `MatchedFrontEndConfig::squelch_a` | heuristic |
```

- [ ] **Step 6: Commit on the `milestone-2` branch**

```powershell
git add engine/include/kz4ap/matched_front_end.hpp engine/src/matched_front_end.cpp engine/tests/matched_front_end_test.cpp engine/CMakeLists.txt docs/signal-processing.md
```
```powershell
git commit -m "Add the dit-matched front end with Rician/Rayleigh log-likelihood ratios"
```

---

### Task 10: Classical decoder — the Matched mode

The minimal way for the baseline decoder to consume the front end (Design decisions, C): in `FrontEnd::Matched` each sample goes through the tracker's NCO, then the matched filter; the tracker observes the filter's output weighted by the key-down posterior; the key goes down when the posterior log-odds g exceeds +h and up when it falls below −h (h = 1 nat), and never goes down while a < a_min. The envelope smoother, the 40%/60% thresholds, the mark/space followers, the warm-up and the M ≥ 3·S squelch are not used in this mode; glitch rejection, element classification, gaps, symbols and speed estimation are shared. `FrontEnd::Envelope` stays the default and must produce exactly what it produced before (the existing tests and the smoke CER pin it).

**Files:**
- Modify: `engine/include/kz4ap/decoder.hpp` (`DecodeUpdate::freq_offset_hz`)
- Modify: `engine/include/kz4ap/classical_decoder.hpp`, `engine/src/classical_decoder.cpp`
- Test: `engine/tests/classical_decoder_test.cpp`
- Modify: `docs/signal-processing.md` (§8, §8b, §9, §10)

**Interfaces:**
- Consumes: `FrequencyTracker`, `FrequencyTrackerConfig` (Task 8); `MatchedFrontEnd`, `MatchedFrontEndConfig`, `FrontEndSample` (Task 9).
- Produces:
  - `DecodeUpdate` gains `std::optional<double> freq_offset_hz;` — the decoder's estimate of the station's offset from its channel center, Hz; set only by decoders that track frequency.
  - `enum class FrontEnd { Envelope, Matched };`
  - `ClassicalDecoderConfig` gains `FrontEnd front_end = FrontEnd::Envelope;`, `double llr_hysteresis = 1.0;` (nats), `std::size_t follow_after_marks = 8;`, `MatchedFrontEndConfig matched;`, `FrequencyTrackerConfig tracker;`.
  - `explicit ClassicalDecoder(double sample_rate, ClassicalDecoderConfig config = {}, double initial_offset_hz = 0.0);`
  - `double ClassicalDecoder::frequency_offset_hz() const;` (Matched: the tracker's f̂; Envelope: the initial offset) and `int ClassicalDecoder::filter_length() const;` (Matched: K; Envelope: 0).
  - In Matched mode every `DecodeUpdate` from `process` and `flush` carries `freq_offset_hz`.

- [ ] **Step 1: Write the failing tests**

Append to `engine/tests/classical_decoder_test.cpp` (add `#include <cmath>` and `using kz4ap::test::keying;`):

```cpp
namespace {

ClassicalDecoderConfig matched() {
    ClassicalDecoderConfig c;
    c.front_end = FrontEnd::Matched;
    return c;
}

// Noise RMS (total, complex) giving s500_db for a unit-amplitude carrier in a
// white 1500 samples/s stream: S500 = A^2 / (sigma^2 * 500 Hz / 1500 Hz).
double sigma_for_s500(double s500_db) { return std::sqrt(3.0 / std::pow(10.0, s500_db / 10.0)); }

std::vector<Sample> add(std::vector<Sample> a, const std::vector<Sample>& b) {
    for (std::size_t i = 0; i < a.size() && i < b.size(); ++i) a[i] += b[i];
    return a;
}

}  // namespace

TEST(ClassicalDecoder, EnvelopeModeTracksNoFrequency) {
    ClassicalDecoder d(kRate, {}, 3.0);
    const auto u = d.process(keyed_signal("E", 25, kRate, 1.0), 0.0);
    EXPECT_FALSE(u.freq_offset_hz.has_value());
    EXPECT_EQ(d.filter_length(), 0);
    EXPECT_EQ(d.frequency_offset_hz(), 3.0);
}

TEST(ClassicalDecoder, MatchedDecodesCleanSignal) {
    ClassicalDecoder d(kRate, matched());
    const std::string msg = "CQ TEST K1ABC";
    EXPECT_EQ(text(decode_all(d, keyed_signal(msg, 25, kRate, duration_for(msg, 25), 0, 1.0,
                                              sigma_for_s500(30), 21))), msg);
}

TEST(ClassicalDecoder, MatchedDecodesAtThreeDbS500) {
    ClassicalDecoder d(kRate, matched());
    const std::string msg = "CQ TEST K1ABC DE W9XYZ";
    const auto t = text(decode_all(d, keyed_signal(msg, 25, kRate, duration_for(msg, 25), 0, 1.0,
                                                   sigma_for_s500(3), 22)));
    EXPECT_NE(t.find("K1ABC DE W9XYZ"), std::string::npos) << t;
}

TEST(ClassicalDecoder, MatchedDecodesStrongSignal) {
    ClassicalDecoder d(kRate, matched());
    const std::string msg = "CQ TEST K1ABC";
    EXPECT_EQ(text(decode_all(d, keyed_signal(msg, 25, kRate, duration_for(msg, 25), 0, 1.0,
                                              sigma_for_s500(60), 23))), msg);
}

TEST(ClassicalDecoder, MatchedFollowsSpeedChange) {
    const std::string first = "CQ CQ CQ";
    const std::string second = "TEST K1ABC K1ABC";
    const double start2 = keying(first, 20, 0.5).back().second + 7 * 1.2 / 20;
    const double total = keying(second, 35, start2).back().second + 1.5;
    const auto x = add(keyed_signal(first, 20, kRate, total, 0, 1.0, sigma_for_s500(20), 24),
                       keyed_signal(second, 35, kRate, total, 0, 1.0, 0.0, 25, start2));
    ClassicalDecoder d(kRate, matched());
    const auto t = text(decode_all(d, x));
    EXPECT_TRUE(ends_with(t, "K1ABC")) << t;
    EXPECT_NEAR(d.filter_length(), std::lround(0.8 * 1.2 / 35 * kRate), 5);
}

TEST(ClassicalDecoder, MatchedTracksTheResidualOffset) {
    // Spec 5.2 target: within 2 Hz, starting 11 Hz off, as bin rounding alone can leave a station.
    const std::string msg = "PARIS PARIS PARIS PARIS PARIS PARIS";
    ClassicalDecoder d(kRate, matched(), 0.0);
    const auto t = text(decode_all(d, keyed_signal(msg, 20, kRate, duration_for(msg, 20), 11.0, 1.0,
                                                   sigma_for_s500(10), 26)));
    EXPECT_NEAR(d.frequency_offset_hz(), 11.0, 2.0);
    EXPECT_NE(t.find("PARIS PARIS"), std::string::npos) << t;
}

TEST(ClassicalDecoder, MatchedHoldsThroughPause) {
    const std::string msg = "CQ TEST K1ABC";
    const double end1 = keying(msg, 25, 0.5).back().second;
    const double start2 = end1 + 10.0;
    const double total = keying(msg, 25, start2).back().second + 1.5;
    const auto x = add(keyed_signal(msg, 25, kRate, total, 6.0, 1.0, sigma_for_s500(20), 27),
                       keyed_signal(msg, 25, kRate, total, 6.0, 1.0, 0.0, 28, start2));
    ClassicalDecoder d(kRate, matched());
    const auto split = static_cast<std::size_t>((start2 - 0.1) * kRate);
    std::vector<DecodedSymbol> chars;
    for (std::size_t i = 0; i < x.size(); i += 256) {
        const auto n = std::min<std::size_t>(256, x.size() - i);
        if (i <= split && split < i + n) {
            // Process up to the resume point, then compare the estimate with the one before the pause.
            auto a = d.process(std::span<const Sample>(x).subspan(i, split - i), static_cast<double>(i) / kRate);
            chars.insert(chars.end(), a.chars.begin(), a.chars.end());
            const double after_pause = d.frequency_offset_hz();
            EXPECT_NEAR(after_pause, 6.0, 2.0);
            auto b = d.process(std::span<const Sample>(x).subspan(split, i + n - split), static_cast<double>(split) / kRate);
            chars.insert(chars.end(), b.chars.begin(), b.chars.end());
            continue;
        }
        auto u = d.process(std::span<const Sample>(x).subspan(i, n), static_cast<double>(i) / kRate);
        chars.insert(chars.end(), u.chars.begin(), u.chars.end());
    }
    auto f = d.flush();
    chars.insert(chars.end(), f.chars.begin(), f.chars.end());
    EXPECT_EQ(text(chars), msg + " " + msg);
    EXPECT_NEAR(d.frequency_offset_hz(), 6.0, 2.0);
}

TEST(ClassicalDecoder, MatchedNoiseAfterStationStopsDecodesNothing) {
    const std::string msg = "CQ TEST K1ABC";
    const double end = keying(msg, 25, 0.5).back().second;
    ClassicalDecoder d(kRate, matched());
    const auto chars = decode_all(d, keyed_signal(msg, 25, kRate, end + 10.0, 0, 1.0, sigma_for_s500(20), 29));
    EXPECT_EQ(text(chars), msg);
    for (const auto& c : chars) {
        if (c.text != " ") EXPECT_LT(c.start_s, end + 0.2) << c.text << " at " << c.start_s;
    }
}

TEST(ClassicalDecoder, MatchedIgnoresStrongerNeighbor) {
    // Wanted at 0 Hz, S500 = 15 dB; a neighbor 100 Hz away inside the same channel,
    // 10 dB stronger (key-down power), at another speed.
    const std::string msg = "CQ TEST K1ABC K1ABC K1ABC";
    const double total = duration_for(msg, 25);
    const auto x = add(keyed_signal(msg, 25, kRate, total, 0.0, 1.0, sigma_for_s500(15), 30),
                       keyed_signal("TU W9XYZ 5NN TU W9XYZ 5NN TU W9XYZ", 30, kRate, total, 100.0,
                                    std::sqrt(10.0), 0.0, 31, 0.3));
    ClassicalDecoder d(kRate, matched(), 0.0);
    const auto t = text(decode_all(d, x));
    EXPECT_NEAR(d.frequency_offset_hz(), 0.0, 2.0);
    EXPECT_NE(t.find("K1ABC"), std::string::npos) << t;
}

TEST(ClassicalDecoder, MatchedChunkSizeDoesNotChangeOutput) {
    const std::string msg = "CQ TEST K1ABC";
    const auto x = keyed_signal(msg, 25, kRate, duration_for(msg, 25), 7.0, 1.0, sigma_for_s500(10), 32);
    std::vector<std::vector<DecodedSymbol>> runs;
    for (const std::size_t chunk : {std::size_t{1}, std::size_t{7}, std::size_t{256}}) {
        ClassicalDecoder d(kRate, matched());
        runs.push_back(decode_all(d, x, chunk));
    }
    for (std::size_t r = 1; r < runs.size(); ++r) {
        ASSERT_EQ(runs[r].size(), runs[0].size());
        for (std::size_t i = 0; i < runs[0].size(); ++i) {
            EXPECT_EQ(runs[r][i].text, runs[0][i].text);
            EXPECT_EQ(runs[r][i].start_s, runs[0][i].start_s);
            EXPECT_EQ(runs[r][i].end_s, runs[0][i].end_s);
        }
    }
}

TEST(ClassicalDecoder, MatchedModeRejectsInvalidSettings) {
    auto c = matched();
    c.llr_hysteresis = -1.0;
    EXPECT_THROW(ClassicalDecoder d(kRate, c), std::invalid_argument);
    c = matched();
    c.follow_after_marks = 1;
    EXPECT_THROW(ClassicalDecoder d(kRate, c), std::invalid_argument);
}
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `cmake --build --preset windows`
Expected: compile errors (`FrontEnd`, `filter_length`, `freq_offset_hz` unknown).

- [ ] **Step 3: Extend the decode result**

In `engine/include/kz4ap/decoder.hpp`, add `#include <optional>` and a member to `DecodeUpdate`:

```cpp
    std::optional<double> freq_offset_hz;  // the station's offset from its channel's center, Hz, if the decoder tracks it
```

- [ ] **Step 4: Implement the mode**

In `engine/include/kz4ap/classical_decoder.hpp`, add `#include "kz4ap/frequency_tracker.hpp"`, `#include "kz4ap/matched_front_end.hpp"` and `#include <optional>`; before `struct ClassicalDecoderConfig` add

```cpp
enum class FrontEnd {
    Envelope,  // the baseline: |y|, smoothing, keying at 40%/60% between space and mark levels
    Matched,   // re-centering, a filter matched to the dit, keying on the posterior log-odds
};
```

add to the end of `ClassicalDecoderConfig`

```cpp
    FrontEnd front_end = FrontEnd::Envelope;
    double llr_hysteresis = 1.0;          // Matched: key down above +this, up below -this (posterior log-odds, nats)
    std::size_t follow_after_marks = 8;   // Matched: the filter follows the speed once the window holds this many marks
    MatchedFrontEndConfig matched;        // Matched only
    FrequencyTrackerConfig tracker;       // Matched only
```

change the constructor and add the accessors:

```cpp
    // initial_offset_hz: the station's offset from its channel's center as the detector
    // measured it, Hz. The Matched front end starts re-centering there.
    explicit ClassicalDecoder(double sample_rate, ClassicalDecoderConfig config = {}, double initial_offset_hz = 0.0);
```
```cpp
    double frequency_offset_hz() const;  // Matched: the tracker's estimate; Envelope: the initial offset, Hz
    int filter_length() const;           // Matched: the matched filter's length K, samples; Envelope: 0
```

and add private members (after `float confidence_ = 0;`):

```cpp
    void step_matched(Sample y, double t, DecodeUpdate& out);
    void check_gaps(double t, DecodeUpdate& out);

    double initial_offset_hz_ = 0;
    std::optional<FrequencyTracker> tracker_;     // Matched only
    std::optional<MatchedFrontEnd> front_end_;    // Matched only
```

In `engine/src/classical_decoder.cpp`:

- extend `validated_rate`'s condition with `|| !(c.llr_hysteresis >= 0) || c.follow_after_marks < 2`;
- change the constructor to

```cpp
ClassicalDecoder::ClassicalDecoder(double sample_rate, ClassicalDecoderConfig config, double initial_offset_hz)
    : rate_(validated_rate(sample_rate, config)),
      config_(config),
      attack_alpha_(alpha_for(config.attack_s, sample_rate)),
      decay_alpha_(alpha_for(config.decay_s, sample_rate)),
      initial_offset_hz_(initial_offset_hz) {
    if (config_.front_end == FrontEnd::Matched) {
        tracker_.emplace(sample_rate, initial_offset_hz, config_.tracker);
        front_end_.emplace(sample_rate, config_.matched);
    }
    reset();
}
```

- at the end of `reset()` add

```cpp
    if (tracker_) tracker_->reset(initial_offset_hz_);
    if (front_end_) front_end_->reset();
```

- replace the sample loop in `process` and the two lines after it with

```cpp
    for (const auto& sample : samples) {
        const double t = origin_s_ + static_cast<double>(count_) / rate_;
        if (front_end_) step_matched(sample, t, out);
        else step(std::abs(sample), t, out);
        ++count_;
    }
    out.wpm = static_cast<float>(wpm());
    out.confidence = confidence_;
    if (tracker_) out.freq_offset_hz = tracker_->offset_hz();
    return out;
```

- in `flush()`, before `return out;`, add `if (tracker_) out.freq_offset_hz = tracker_->offset_hz();`;
- in `step()`, replace the final `if (!key_) { … }` block with `check_gaps(t, out);`, and add:

```cpp
void ClassicalDecoder::check_gaps(double t, DecodeUpdate& out) {
    if (key_) return;
    const double gap = t - up_t_;
    if (char_open_ && gap > 2.0 * dit_s_) finish_char(out);
    if (word_open_ && !char_open_ && gap > 5.0 * dit_s_) {
        emit(out, " ", 1.0f, up_t_, t);
        word_open_ = false;
    }
}

void ClassicalDecoder::step_matched(Sample y, double t, DecodeUpdate& out) {
    const FrontEndSample f = front_end_->step(tracker_->mix(y));
    if (!f.ready) return;
    tracker_->observe(f.filtered, f.p_key_down);
    const double h = config_.llr_hysteresis;
    if (!key_ && f.signal && f.log_odds > h) {
        key_down(t);
    } else if (key_ && (!f.signal || f.log_odds < -h)) {
        key_up(t);
    }
    check_gaps(t, out);
}

double ClassicalDecoder::frequency_offset_hz() const { return tracker_ ? tracker_->offset_hz() : initial_offset_hz_; }

int ClassicalDecoder::filter_length() const { return front_end_ ? front_end_->length() : 0; }
```

- at the end of `update_speed()` add

```cpp
    // The matched filter follows the speed once the estimate rests on enough marks.
    if (front_end_ && recent_marks_.size() >= config_.follow_after_marks) front_end_->set_dit(dit_s_);
```

- [ ] **Step 5: Build and run the tests**

```powershell
cmake --build --preset windows
ctest --preset windows -R ClassicalDecoder
```
Expected: every `ClassicalDecoder.*` test passes, the milestone-1 ones unchanged. The Matched tests are the Review Focus tests for strong signals, speed changes, pauses and closely spaced stations; if one fails, debug it (superpowers:systematic-debugging) before changing any tolerance or parameter, and record any parameter you change, with the reason, in signal-processing.md §8b.

- [ ] **Step 6: Confirm the baseline is unchanged**

```powershell
ctest --preset windows
```
In Git Bash: `bash bench/smoke.sh build/windows`
Expected: all tests pass; `smoke test passed` with the same CER as before this task.

- [ ] **Step 7: Document**

In `docs/signal-processing.md`, section 8, after the paragraph **What it is.**, add:

```markdown
**Two front ends.** `ClassicalDecoderConfig::front_end` selects how samples
become key-down and key-up. `Envelope` (the default, steps 1–5 below) is
the baseline. `Matched` replaces steps 1–5 with section 7's re-centering and
section 8b's matched filter and likelihood: the key goes down when the
posterior log-odds g exceeds +1 nat and up when it falls below −1 nat
(**heuristic** hysteresis), and never goes down while a < 3. Steps 6–10
(glitches, elements, gaps, symbols, speed) are the same in both. In
`Matched` mode the filter follows the speed estimate once its window holds
8 marks (**heuristic**), and every decode result reports the tracker's
frequency estimate.
```

In section 9, add a row: `| Matched filter group delay (Matched only) | (K − 1)/2 samples: 19 ms at 25 WPM |`.

In section 10, add rows:

```markdown
| Front end | Envelope (default) or Matched | `ClassicalDecoderConfig::front_end` | — |
| LLR keying hysteresis (Matched) | g > +1 nat down, g < −1 nat up | `ClassicalDecoderConfig::llr_hysteresis` | heuristic |
| Filter follows speed after (Matched) | 8 marks in the speed window | `ClassicalDecoderConfig::follow_after_marks` | heuristic |
```

- [ ] **Step 8: Commit on the `milestone-2` branch**

```powershell
git add engine/include/kz4ap/decoder.hpp engine/include/kz4ap/classical_decoder.hpp engine/src/classical_decoder.cpp engine/tests/classical_decoder_test.cpp docs/signal-processing.md
```
```powershell
git commit -m "Add a Matched front-end mode to the classical decoder, keying on LLRs"
```

---

### Task 11: Engine — re-centering from the detector, refined frequency, drift retune, `--front-end`

Wires the Matched mode into the pipeline. The decoder of a new track starts its NCO at the detector's residual (track frequency minus channel center), so the tracker starts within a few Hz instead of ±11.7 Hz. In Matched mode the engine publishes the tracked frequency (channel center plus the decoder's estimate) in every `DecodedTextEvent`, and moves the detector's track to it (`SignalDetector::retune`), so a drifting station keeps its one track and its level is read where it now is. `kz4ap-bench --front-end matched` selects the mode.

**Files:**
- Modify: `engine/include/kz4ap/signal_detector.hpp`, `engine/src/signal_detector.cpp` (`retune`)
- Modify: `engine/src/engine.cpp`
- Modify: `engine/tests/test_signals.hpp` (carrier drift), `engine/tests/signal_detector_test.cpp`, `engine/tests/engine_test.cpp`
- Modify: `bench/src/main.cpp`, `README.md`
- Modify: `docs/signal-processing.md` (§6, §7)

**Interfaces:**
- Consumes: `EngineConfig::decoder.front_end`, `ClassicalDecoder(double, ClassicalDecoderConfig, double initial_offset_hz)`, `DecodeUpdate::freq_offset_hz` (Task 10); `Engine::Channel::bin`, `open_channel`, oracle mode (Task 6).
- Produces:
  - `void SignalDetector::retune(std::uint32_t id, double freq_hz);` — moves an active track to `freq_hz` (Hz from the span's center): its bin becomes the nearest bin and its `freq_hz` the given value; unknown ids are ignored.
  - Engine behavior: decoders are created with `initial_offset_hz = track.freq_hz − bin_to_hz(bin)`; when an update carries `freq_offset_hz`, the channel's track frequency becomes `bin_to_hz(bin) + *freq_offset_hz`, which `DecodedTextEvent::freq_hz` and the `Died` event then report, and (outside oracle mode) the detector's track is retuned to it.
  - `kz4ap::test::keyed_signal(..., double start_s = 0.5, double drift_hz_per_s = 0.0)`.
  - `kz4ap-bench --front-end baseline|matched` (default `baseline`); JSON output gains `"front_end"`.

- [ ] **Step 1: Write the failing tests**

In `engine/tests/test_signals.hpp`, give `keyed_signal` a last parameter `double drift_hz_per_s = 0.0` and replace the line computing `ph` with:

```cpp
        const double t_i = static_cast<double>(i) / rate;
        double ph = 2 * std::numbers::pi * freq_hz * static_cast<double>(i) / rate;
        if (drift_hz_per_s != 0) ph += std::numbers::pi * drift_hz_per_s * t_i * t_i;  // frequency freq_hz + drift * t
```

(The unchanged first term keeps every existing test signal bit-identical.)

Append to `engine/tests/signal_detector_test.cpp`:

```cpp
TEST(SignalDetector, RetuneMovesTrackBin) {
    SignalDetector d(config());
    int i = 0;
    for (; i < 200; ++i) d.process(frame(frame_time(i), {{100, -70.0f}}));
    ASSERT_EQ(d.tracks().size(), 1u);
    const auto id = d.tracks()[0].id;
    d.retune(id, (104 - kN / 2) * 100.0);  // the station moved 4 bins
    std::size_t born = 0;
    for (; i < 800; ++i) born += d.process(frame(frame_time(i), {{104, -70.0f}})).born.size();
    EXPECT_EQ(born, 0u);  // without the retune, a peak 4 bins away would become a second track
    ASSERT_EQ(d.tracks().size(), 1u);
    EXPECT_EQ(d.tracks()[0].id, id);
    EXPECT_DOUBLE_EQ(d.tracks()[0].freq_hz, (104 - kN / 2) * 100.0);
}

TEST(SignalDetector, RetuneIgnoresUnknownTracks) {
    SignalDetector d(config());
    for (int i = 0; i < 200; ++i) d.process(frame(frame_time(i), {{100, -70.0f}}));
    d.retune(999, 0.0);
    ASSERT_EQ(d.tracks().size(), 1u);
    EXPECT_DOUBLE_EQ(d.tracks()[0].freq_hz, (100 - kN / 2) * 100.0);
}
```

In `engine/tests/engine_test.cpp`, add a second result type that also records each track's latest published frequency, and a runner that takes a whole `EngineConfig` (keep the existing `Result` and `run` as they are):

```cpp
struct TextLog {
    std::map<std::uint32_t, std::string> text;
    std::map<std::uint32_t, double> last_freq;      // DecodedTextEvent::freq_hz of the latest event
    std::map<std::uint32_t, double> last_end_s;     // end time of the latest symbol
    std::vector<Track> born;
};

TextLog run_with(const EngineConfig& config, const std::vector<Sample>& x, std::size_t chunk = 65536) {
    EventBus bus;
    TextLog log;
    bus.subscribe([&](const Event& e) {
        if (const auto* t = std::get_if<TrackEvent>(&e); t && t->kind == TrackEvent::Kind::Born) log.born.push_back(t->track);
        if (const auto* d = std::get_if<DecodedTextEvent>(&e)) {
            for (const auto& c : d->chars) log.text[d->track_id] += c.text;
            log.last_freq[d->track_id] = d->freq_hz;
            log.last_end_s[d->track_id] = d->chars.back().end_s;
        }
    });
    Engine engine(config, bus);
    for (std::size_t i = 0; i < x.size(); i += chunk) {
        engine.process(std::span<const Sample>(x).subspan(i, std::min(chunk, x.size() - i)));
    }
    engine.finish();
    return log;
}

EngineConfig matched_config(int sample_rate = static_cast<int>(kRate)) {
    EngineConfig c;
    c.sample_rate = sample_rate;
    c.decoder.front_end = FrontEnd::Matched;
    return c;
}

constexpr double kAmplitude20dB48k = 0.0204;  // 20 dB SNR in 500 Hz against kNoiseSigma at 48 kHz
```

(put these inside the file's anonymous namespace, after `band()`), and append the tests:

```cpp
TEST(Engine, MatchedFrontEndDecodesTwoSignals) {
    const auto log = run_with(matched_config(), band(9.0));
    ASSERT_EQ(log.born.size(), 2u);
    std::string all;
    for (const auto& [id, t] : log.text) all += t + "|";
    EXPECT_NE(all.find("CQ K1ABC"), std::string::npos) << all;
    EXPECT_NE(all.find("CQ W9XYZ"), std::string::npos) << all;
}

TEST(Engine, MatchedReportsDriftingFrequency) {
    // A station at 12003 Hz drifting +1 Hz/s: the published frequency follows it, one track throughout.
    const std::string msg = "CQ TEST K1ABC K1ABC CQ TEST K1ABC K1ABC";
    const double f0 = 12003.0;
    const auto x = keyed_signal(msg, 25, 48000.0, kz4ap::test::duration_for(msg, 25), f0, kAmplitude20dB48k,
                                kNoiseSigma, 41, 0.5, 1.0);
    const auto log = run_with(matched_config(48000), x);
    ASSERT_EQ(log.born.size(), 1u);
    const auto id = log.born[0].id;
    const double truth = f0 + 1.0 * log.last_end_s.at(id);  // the carrier's frequency when the last symbol ended
    EXPECT_NEAR(log.last_freq.at(id), truth, 2.5);
    EXPECT_GT(std::abs(log.last_freq.at(id) - log.born[0].freq_hz), 10.0);  // it did move from the birth estimate
    EXPECT_NE(log.text.at(id).find("K1ABC"), std::string::npos) << log.text.at(id);
}

TEST(Engine, OracleMatchedFindsTheResidualFromTheBinCenter) {
    // The oracle opens the channel on the bin center (12000 Hz), 9 Hz below the station;
    // the tracker must find the 9 Hz itself.
    const std::string msg = "CQ TEST K1ABC K1ABC";
    const auto x = keyed_signal(msg, 25, 48000.0, kz4ap::test::duration_for(msg, 25), 12009.0, kAmplitude20dB48k,
                                kNoiseSigma, 42);
    auto config = matched_config(48000);
    config.oracle_frequencies_hz = {12009.0};
    const auto log = run_with(config, x);
    ASSERT_EQ(log.born.size(), 1u);
    EXPECT_DOUBLE_EQ(log.born[0].freq_hz, 12000.0);
    EXPECT_NEAR(log.last_freq.at(1), 12009.0, 2.0);
    EXPECT_NE(log.text.at(1).find("K1ABC"), std::string::npos) << log.text.at(1);
}

TEST(Engine, MatchedChunkingDoesNotChangeResults) {
    const auto x = band(9.0);
    const auto a = run_with(matched_config(), x, 1000);
    const auto b = run_with(matched_config(), x, 77777);
    EXPECT_EQ(a.text, b.text);
    EXPECT_EQ(a.last_freq, b.last_freq);
}
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `cmake --build --preset windows`
Expected: compile error, `retune` is not a member of `SignalDetector`.

- [ ] **Step 3: Implement `retune`**

In `engine/include/kz4ap/signal_detector.hpp`, add to the public part of `SignalDetector`:

```cpp
    // Moves an active track to freq_hz (Hz from the span's center), e.g. to a
    // decoder's refined estimate: its level is then read around the nearest bin,
    // and new peaks near that bin belong to it. Unknown ids are ignored.
    void retune(std::uint32_t id, double freq_hz);
```

In `engine/src/signal_detector.cpp`, add:

```cpp
void SignalDetector::retune(std::uint32_t id, double freq_hz) {
    for (auto& a : active_) {
        if (a.track.id != id) continue;
        const long bin = std::lround(freq_hz * config_.fft_size / config_.sample_rate) + config_.fft_size / 2;
        a.bin = static_cast<int>(std::clamp<long>(bin, 0, config_.fft_size - 1));
        a.track.freq_hz = freq_hz;
        return;
    }
}
```

- [ ] **Step 4: Wire the engine**

In `engine/src/engine.cpp`, in `open_channel`, create the decoder with the detector's residual:

```cpp
void Engine::open_channel(const Track& track, int bin) {
    channelizer_.add_channel(track.id, bin);
    const double residual_hz = track.freq_hz - channelizer_.bin_to_hz(bin);
    channels_.emplace(track.id, Channel{track, bin, std::make_unique<ClassicalDecoder>(
                                                        channelizer_.output_rate(), config_.decoder, residual_hz)});
    bus_.publish(Event{TrackEvent{TrackEvent::Kind::Born, track}});
}
```

and in the channelizer sink in `process_hop`, between `channel_samples_ += s.size();` and `publish_update(...)`, insert:

```cpp
        if (update.freq_offset_hz) {
            // Matched mode: the decoder re-centers on the carrier; report and track it there.
            const double freq_hz = channelizer_.bin_to_hz(it->second.bin) + *update.freq_offset_hz;
            it->second.track.freq_hz = freq_hz;
            if (!oracle_) detector_.retune(id, freq_hz);
        }
```

(`publish_update` already reports `channel.track.freq_hz`. In Envelope mode `freq_offset_hz` is empty, so nothing changes.)

- [ ] **Step 5: Add `--front-end` to `kz4ap-bench`**

In `bench/src/main.cpp`: add `FrontEnd front_end = FrontEnd::Envelope;` to `Args`; parse

```cpp
        else if (a == "--front-end") {
            const auto v = value().string();
            if (v == "baseline") args.front_end = FrontEnd::Envelope;
            else if (v == "matched") args.front_end = FrontEnd::Matched;
            else throw std::runtime_error("--front-end must be baseline or matched\n" + std::string(kUsage));
        }
```

extend `kUsage`'s last line to `"                   [--no-timing] [--baseline BASELINE.json] [--oracle] [--front-end baseline|matched]\n"`, set `config.decoder.front_end = args.front_end;` after `config.sample_rate`, and add `out["front_end"] = args.front_end == FrontEnd::Matched ? "matched" : "baseline";` after `out["duration_s"]`.

In `README.md`, change the suite `run` line to end with `--front-end baseline --front-end matched`, and add after the code block: "`--front-end matched` selects the dit-matched front end with frequency re-centering (see `docs/signal-processing.md`, section 8b); `baseline` is the default."

- [ ] **Step 6: Build and run all tests**

```powershell
cmake --build --preset windows
ctest --preset windows
```
Expected: all pass, including `SignalDetector.RetuneMovesTrackBin`, `SignalDetector.RetuneIgnoresUnknownTracks`, `Engine.MatchedFrontEndDecodesTwoSignals`, `Engine.MatchedReportsDriftingFrequency`, `Engine.OracleMatchedFindsTheResidualFromTheBinCenter`, `Engine.MatchedChunkingDoesNotChangeResults`, and every milestone-1 engine test unchanged.

- [ ] **Step 7: Smoke check, both front ends**

In Git Bash:
```bash
bash bench/smoke.sh build/windows
build/windows/bench/Release/kz4ap-bench.exe build/windows/smoke/band.wav --labels build/windows/smoke/band.json --front-end matched --no-timing
```
Expected: `smoke test passed` with the baseline's usual CER; the second command prints a `CER …` line for the matched front end (write it down for Task 12) and `detected 8 of 8`.

- [ ] **Step 8: Document**

In `docs/signal-processing.md`, section 6, subsection **Frequency and drift**, replace "The frequency is then **fixed** for the track's life: it is not re-measured, and the channel stays where it was put." with:

```markdown
With the default (Envelope) decoder the frequency is then **fixed** for the
track's life: it is not re-measured, and the channel stays where it was put.
With the Matched decoder (section 8b) the decoder re-centers on the carrier
(section 7) and the engine moves the track with it after every channel
block (`SignalDetector::retune`): the track's reported frequency is the
channel center plus the decoder's estimate, and its level is read around
the nearest bin to that, so a drifting station keeps one track. The channel
itself does not move; the decoder's NCO covers ±75 Hz around it.
```

In section 7, in **Frequency re-centering (Matched front end only)**, add after the NCO bullet: "The engine starts each new track's NCO at the detector's residual, track frequency minus channel center (in oracle mode, 0 Hz: the channel sits on the bin nearest the given frequency and the tracker must find the rest)."

- [ ] **Step 9: Commit on the `milestone-2` branch**

```powershell
git add engine/include/kz4ap/signal_detector.hpp engine/src/signal_detector.cpp engine/src/engine.cpp engine/tests/test_signals.hpp engine/tests/signal_detector_test.cpp engine/tests/engine_test.cpp bench/src/main.cpp README.md docs/signal-processing.md
```
```powershell
git commit -m "Re-center stations from the detector's estimate, publish and track the refined frequency"
```

---

### Task 12: Measure, document, and guard the new path in CI

Spec §5.4: "No decoder replaces the baseline unless it beats the baseline on the benchmark." This task runs the full suite on both front ends, adds the one missing measurement (how far the tracked frequency is from the truth), writes the numbers into the documents, and adds a CI smoke check for the Matched path. It does **not** change the default front end: that is the owner's decision, made on these numbers.

**Files:**
- Modify: `bench/src/scoring.hpp` (`DecodedTrack::last_freq_hz`), `bench/src/main.cpp`
- Modify: `training/kz4ap_synth/suites.py`, `training/tests/test_suites.py`
- Modify: `bench/smoke.sh`; Create: `bench/baselines/smoke-matched.json`
- Modify: `docs/signal-processing.md` (§7, §8b, §10), `docs/backlog.md`, `docs/research/decoder-survey.md`

**Interfaces:**
- Consumes: everything above; `kz4ap-bench --front-end`, `--oracle`; `kz4ap_synth.suites`.
- Produces: `DecodedTrack` gains `double last_freq_hz = 0;` (the frequency of the track's latest `DecodedTextEvent`, else its birth frequency); each `score.signals` entry of the bench JSON gains `tracked_freq_hz` (null if no track matched); suite rows gain `freq_error_hz` and each summary entry `freq_error_hz_median` (median |tracked − labeled| frequency, Hz, over matched signals without drift; None if there are none); `summary.md` gains a last column "median frequency error (Hz)".

- [ ] **Step 1: Write the failing test**

Append to `training/tests/test_suites.py`:

```python
def test_aggregate_reports_the_median_frequency_error():
    rows = [dict(_row(0.0, 10, 0), freq_error_hz=1.0), dict(_row(2.0, 10, 0), freq_error_hz=3.0),
            dict(_row(4.0, 10, 0), freq_error_hz=None)]
    assert aggregate(rows)[("baseline", "A", "25 wpm")]["freq_error_hz_median"] == pytest.approx(2.0)
    assert aggregate([_row(0.0, 10, 0)])[("baseline", "A", "25 wpm")]["freq_error_hz_median"] is None
```

Run: `.venv\Scripts\python -m pytest training/tests/test_suites.py -q`
Expected: FAIL with `KeyError: 'freq_error_hz_median'`.

- [ ] **Step 2: Implement the frequency-error measurement**

In `bench/src/scoring.hpp`, add to `DecodedTrack`: `double last_freq_hz = 0;  // frequency of the latest decoded-text event, Hz (birth frequency until then)`.

In `bench/src/main.cpp`, in the subscriber, set `tracks[t->track.id] = {t->track.id, t->track.freq_hz, "", t->track.freq_hz};` for `Born`, and in the `DecodedTextEvent` branch add `tracks[d->track_id].last_freq_hz = d->freq_hz;`. In the per-signal JSON add, after `"track_id"`:

```cpp
                                   {"tracked_freq_hz", sig.track_id ? nlohmann::json(tracks.at(*sig.track_id).last_freq_hz)
                                                                    : nlohmann::json()},
```

In `training/kz4ap_synth/suites.py`:

- in `load_results`, compute before `rows.append(...)`

```python
                tracked = sig.get("tracked_freq_hz")
                freq_error = (abs(tracked - label["freq_offset_hz"])
                              if tracked is not None and not label.get("drift_hz_per_s") else None)
```

  and add `"freq_error_hz": freq_error,` to the row;
- in `aggregate`, add `"freq_errors": []` to the new-group dictionary, append in the loop `if r.get("freq_error_hz") is not None: g["freq_errors"].append(r["freq_error_hz"])`, and add to each output entry `"freq_error_hz_median": float(np.median(g["freq_errors"])) if g["freq_errors"] else None,`;
- in `format_markdown`, add `| median frequency error (Hz) |` to the header, one more `---|` to the separator, and `| {_db(v['freq_error_hz_median'])}` before the closing ` |` of each row, formatted with one decimal (`_db` already does that).

Run: `.venv\Scripts\python -m pytest training -q` → all pass. Run: `cmake --build --preset windows` then `ctest --preset windows` → all pass.

- [ ] **Step 3: Commit the measurement tooling**

```powershell
git add bench/src/scoring.hpp bench/src/main.cpp training/kz4ap_synth/suites.py training/tests/test_suites.py
```
```powershell
git commit -m "Report the tracked frequency's error in the bench and suite summaries"
```

- [ ] **Step 4: Run the full suite on both front ends**

```powershell
cmake --build --preset windows
$env:PYTHONPATH = "training"
.venv\Scripts\python -m kz4ap_synth.suites generate --suite full --seeds 2 --out build/suite/full2
.venv\Scripts\python -m kz4ap_synth.suites run --out build/suite/full2 --bench build/windows/bench/Release/kz4ap-bench.exe --front-end baseline --front-end matched
.venv\Scripts\python -m kz4ap_synth.suites summarize --out build/suite/full2
```
Expected: about 60 recordings, each scored twice; `build\suite\full2\summary.md` has a baseline and a matched row for every condition, and a CPU table with both front ends. Close other heavy programs while it runs, since CPU time is being measured.

Also record the machine: `Get-CimInstance Win32_Processor | Select-Object -ExpandProperty Name`.

- [ ] **Step 5: Check the numbers before writing them down**

Read `summary.md` and check, before trusting it:
- Group A: the baseline's S₅₀₀ at CER 0.10 should be in the region the detector-free baseline can reach (a few dB above 0 dB S₅₀₀; the squelch alone stops it near +6 dB by estimate). A matched crossing *higher* than the baseline's means a bug, not a result: debug it (superpowers:systematic-debugging) before going on.
- Group F: the matched front end's CER should not depend on the offset (0 to 11.7 Hz) by more than the run-to-run spread between offsets of the same S₅₀₀; its median frequency error should be within the ±2 Hz target at S₅₀₀ = 5 dB.
- The smoke recording's baseline CER (run `bash bench/smoke.sh build/windows`) must equal the value written down in Task 1, Step 5: if it moved, the baseline was changed by accident — find and fix that first.

- [ ] **Step 6: Write the measured results into `docs/signal-processing.md`**

At the end of section 8b add a subsection, filling every cell from `build/suite/full2/summary.md` (2 seeds) and naming the processor from Step 4:

```markdown
### Measured: Matched against Envelope (milestone 2, part 1)

Full suite, 2 seeds, synthetic recordings (`kz4ap_synth.suites`), on
<processor>. S₅₀₀: key-down carrier power over noise power in 500 Hz, dB.
Groups A–F use oracle channels (detector bypassed, channel on the nearest
bin); the rest run the whole pipeline.

| Condition | Envelope: S₅₀₀ at CER 0.10 / 0.05 (dB) | Matched: S₅₀₀ at CER 0.10 / 0.05 (dB) |
|---|---|---|
| A, 12 WPM | … | … |
| A, 25 WPM | … | … |
| A, 40 WPM | … | … |

| Condition | Envelope: CER / character CER / space error rate / first-word CER | Matched: same |
|---|---|---|
| (one row per tag of groups B–F and of strong, pauses, tune-up, first sample, crowded, band, copied from summary.md) | … | … |

| | Envelope | Matched |
|---|---|---|
| CPU per channel-second (whole process), ms/s | … | … |
| Decoders per channel-second, ms/s | … | … |
| Median frequency error, group F offsets, Hz | … | … |
```

Then update the status of every parameter this measurement settles, in §7 ("Frequency re-centering") and §8b and the §10 rows: the tracker's accuracy becomes **measured** with the median error; leave β, τ_a, τ_n, the squelch and the hysteresis **heuristic** unless a sweep was run (none is in this plan).

- [ ] **Step 7: Guard the Matched path in CI**

Run the Matched front end on the smoke recording and read its CER:

```bash
build/windows/bench/Release/kz4ap-bench.exe build/windows/smoke/band.wav --labels build/windows/smoke/band.json --front-end matched --no-timing
```

Create `bench/baselines/smoke-matched.json` with `max_cer` = that CER plus 0.02, rounded up to two decimals (the same margin style as the baseline's file), and the baseline's detection floor:

```json
{"max_cer": <measured CER + 0.02, rounded up to 2 decimals>, "min_detection_recall": 0.875}
```

(The margin allows for floating-point differences between the Windows and Linux runners. `bench/baselines/smoke.json` stays as it is.)

Replace `bench/smoke.sh` with:

```bash
#!/usr/bin/env bash
# Benchmark smoke test: generate a synthetic band, score it against the stored
# baselines with both front ends, and check that two runs produce identical results.
# Usage: bench/smoke.sh BUILD_DIR   (run from the repository root)
set -euo pipefail

BUILD_DIR="$1"
PYTHON="${PYTHON:-python}"
BENCH=$(find "$BUILD_DIR" -type f \( -name kz4ap-bench -o -name kz4ap-bench.exe \) | head -n 1)
if [ -z "$BENCH" ]; then
    echo "kz4ap-bench not found under $BUILD_DIR" >&2
    exit 2
fi

WORK="$BUILD_DIR/smoke"
mkdir -p "$WORK"
PYTHONPATH=training "$PYTHON" -m kz4ap_synth.generate --scenario band --signals 8 \
    --duration 30 --seed 1 --out "$WORK/band.wav"

check() {  # check NAME BASELINE [bench options...]
    local name="$1" baseline="$2"
    shift 2
    "$BENCH" "$WORK/band.wav" --labels "$WORK/band.json" --json "$WORK/$name-1.json" \
        --no-timing --baseline "$baseline" "$@"
    "$BENCH" "$WORK/band.wav" --labels "$WORK/band.json" --json "$WORK/$name-2.json" --no-timing "$@"
    if ! cmp -s "$WORK/$name-1.json" "$WORK/$name-2.json"; then
        echo "FAIL: two $name runs over the same recording produced different results" >&2
        exit 1
    fi
}

check baseline bench/baselines/smoke.json
check matched bench/baselines/smoke-matched.json --front-end matched
echo "smoke test passed"
```

Run in Git Bash: `bash bench/smoke.sh build/windows`
Expected: two `CER …` summary lines, then `smoke test passed`.

- [ ] **Step 8: Update the backlog and the survey**

In `docs/backlog.md`, section 1:
- Under **Top priority**, option 1: add a line "**Built** (milestone 2, part 1): `FrontEnd::Matched`; measured against the baseline in `docs/signal-processing.md` §8b. Whether it becomes the default is the owner's decision (spec §5.4)." followed by the one-line result for group A at 25 WPM (both crossings) and the CPU cost per channel-second of each.
- **Benchmark scenarios to add first:** replace the list with "Done in milestone 2, part 1 (`training/kz4ap_synth/suites.py`)."
- **Channel filtering, two stages:** add "Stage 2 is built as the Matched front end (a boxcar of 0.8 dit); stage 1 is unchanged."
- **Track frequency drift:** add "Done within ±75 Hz of the channel's center in Matched mode (re-centering NCO, detector retune). Still open: moving the channel's center bin for larger drifts, and drift in Envelope mode."
- **Stray E's after a station stops:** add the Matched front end's result on the `pauses` and `strong` groups (CER and first-word CER from the tables) and whether `MatchedNoiseAfterStationStopsDecodesNothing` covers the case.
- Add new items at the end of section 1:

```markdown
### Tune the Matched front end by measurement

Its parameters are heuristic (signal-processing.md §8b): the filter length
β = 0.8 dit (sweep 0.6, 0.8, 1.0), the amplitude and noise time constants
(0.5 s, 2 s; the fading group is the test), the squelch a ≥ 3, the keying
hysteresis ±1 nat, the noise guard (2K samples at g > 1 nat), and when the
filter starts following the speed (8 marks). The amplitude estimate is
biased low by the filter's ramps (about 11% at high SNR, derived); measure
whether that matters.

### VE3NEA's keying and fading conventions

The generator's hand-key dah comes from VE3NEA's DeepCW generator; the other
keying-style parameters are this project's. Transcribe his per-style table,
and establish whether his f_D is the 2σ frequency spread the generator uses,
so group B anchors to his numbers exactly.
```

In `docs/research/decoder-survey.md`, at the end of the paragraph **1. Soft, pre-filtered front end on the existing baseline**, add one sentence: "Built in this project as the Matched front end (milestone 2, part 1); its measured gain over the baseline on the synthetic suite is in signal-processing.md §8b [measured]." Do not change the ranking.

- [ ] **Step 9: Commit on the `milestone-2` branch**

```powershell
git add bench/smoke.sh bench/baselines/smoke-matched.json docs/signal-processing.md docs/backlog.md docs/research/decoder-survey.md
```
```powershell
git commit -m "Record the Matched front end's measured results and guard it in the smoke check"
```

- [ ] **Step 10: Report to the owner**

Summarize for the owner, in a few lines: the group-A crossings for both front ends, the fading and fist results next to VE3NEA's anchor rows, the pause/strong/tune-up findings, the CPU cost, the tracker's measured accuracy, and the open question for the owner: whether the Matched front end should become the default. Do not push.

---

## Self-review (plan author's check against the spec)

**Spec coverage.**

| Spec requirement | Where |
|---|---|
| §3.1 item 1: decoder robustness, measured on the benchmark | Tasks 1–7 (benchmark), 12 (measurement) |
| §5.2 step 1: filter matched to the current dit, before envelope detection | Task 9 (boxcar, B = r/K ≈ 1.25/T), Task 10 (follows the speed estimate) |
| §5.2 step 1: envelope → Rician-versus-Rayleigh LLR | Task 9 (`envelope_llr`, noise and amplitude estimates) |
| §5.2 prerequisite: precise re-centering with drift tracking, target ≈ ±2 Hz | Task 8 (tracker), Task 10 (`MatchedTracksTheResidualOffset`), Task 11 (detector's initial estimate, retune, published frequency), Task 12 (measured median error) |
| §5.2: two stages, the channelizer unchanged | Tasks 8–11 run at r on the channelizer's output; `channelizer.cpp` is not touched |
| §5.2: correlated samples scaled or decimated | Task 9 (`weight = 1/K`, autocorrelation test) |
| §5.2: front end useful on its own in front of the baseline | Task 10 (LLR keying), Task 12 (measured against the baseline) |
| §5.1: the baseline stays in the code | `FrontEnd::Envelope` is the default and bit-identical (Tasks 1, 4, 5, 10, 12 check it) |
| §5.4: VE3NEA's grid, speed changes, interference, tuning error, strong signals up to S₅₀₀ = 60 dB, stations that stop and pause, tune-up carriers, crowded bands, first words | Tasks 1–4 (generator), 7 (suites), 5 (first words) |
| §5.4: CER with prosigns as one symbol and word spaces scored separately | Task 5 |
| §5.4: S₅₀₀ as generated; CPU time per channel-second | Tasks 1–4 (labels), 6 (CPU), 7 (summaries); Raspberry Pi 5: deferred |
| §5.4: real recordings with manta's oracle | Deferred (scope section); Task 6 builds the oracle mechanism |
| §4.1: determinism | Per-sample stages inside the decoder; `MatchedChunkSizeDoesNotChangeOutput`, `MatchedChunkingDoesNotChangeResults`; the smoke check compares two runs of each front end |
| Project rule: signal-processing.md in the same commit | Every task that changes signal processing or benchmark definitions has a "Document" step |

**Placeholder scan.** The only unfilled values are measurements that exist only after running the code: the smoke CER noted in Task 1, the Matched smoke CER and its baseline file in Task 12, and the results table in Task 12, each with the exact command that produces it and the file it comes from.

**Type consistency.** Checked across tasks: `SignalSpec` fields (Tasks 1–4) and the labels keys the bench parses (Task 5) and the suites read (Tasks 7, 12); `Score`/`SignalScore` fields and the bench JSON keys the suites consume; `EngineStats`, `Engine::Channel::bin`, `open_channel` (Task 6) and their use in Task 11; `FrequencyTracker` and `MatchedFrontEnd` signatures (Tasks 8–9) and their use in Task 10; `DecodeUpdate::freq_offset_hz` (Task 10) and its use in Task 11; `FrontEnd` (Task 10) in the bench (Task 11).

**Review Focus.** Each of the five items has its test in the owning task (Tasks 8–11), named in the Review Focus section.

**Known risks for the executor.** The Matched tests for speed changes and the stronger neighbor exercise behavior that was derived, not yet measured; if they fail, that is a finding about the design, to be debugged and recorded, not a tolerance to relax. Random keying may make a suite recording run long; Task 7 says how to fix that.
