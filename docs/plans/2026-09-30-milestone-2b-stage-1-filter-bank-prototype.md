# Milestone 2b, Stage 1: Filter-Bank Prototype on Recorded Oracle Channel Streams — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Prototype, in Python on the engine's own recorded oracle channel streams, every estimator of the filter-bank design (32 fixed boxcars, shared noise spectrum, per-branch keying and duration fits, periodicity estimate, branch selection, new-over handling, corrections), score its text with the bench's own scoring, settle the design's placeholders by measurement, and lay out a full comparison with the current Matched and Envelope paths on the same oracle signals, every regime included, for the owner to decide whether the redesign is better and whether stage 2 is worth doing (owner, 2026-09-30: no acceptance gate).

**Architecture:** `kz4ap-bench` gains two tooling options: `--record-channels DIR` writes each oracle channel's complex stream (the channelizer's output, 1500 samples/s, before the frequency tracker) plus a manifest, through a new observation-only `Engine::set_channel_tap`; `--score-decoded FILE` scores externally decoded text per label with exactly the bench's scoring and JSON. The generator gains Farnsworth spacing and a full-suite group I. A new package `training/kz4ap_proto/` (numpy only) mixes each recorded stream to 0 Hz at its labeled frequency, runs the bank channel by channel, writes decoded text, has the bench score it into `build/suite/full3/results/bank-proto/`, and summarizes it against the Matched and Envelope results already there. The detector-only groups (pauses, strong, tune-up, first sample, band, crowded) are also scored with oracle channels — the existing recordings, not regenerated — by Envelope, the current Matched path and the prototype, so every regime is compared like for like. And every non-oracle recording is also decoded through the detector (Task 11b; owner, 2026-09-30): the bench records the channels the Matched path's detector opens, the prototype decodes them from their opening, and `--score-decoded` scores its tracks by frequency, as the engine's detector path is scored. Experiments on seed 1 (development) set the placeholders by stated decision rules; the final configuration is evaluated on all three seeds.

**Tech Stack:** C++20, CMake ≥ 3.25, GoogleTest 1.17.0, nlohmann/json 3.12.0 (engine and bench); Python 3.12 venv with numpy and pytest only (prototype, generator, runner). No new dependencies.

**Spec:** `docs/design/2026-09-30-filter-bank-speed-estimator-design.md` (binding; stage 1 is its section 7). Also: `docs/design/2026-09-25-kz4ap-skimmer-design.md` §5.2 with the 2026-09-30 amendment; `docs/signal-processing.md` §0 (units and symbols), §7 (channelizer, residual offset, re-centering), §8b (the Matched front end whose noise guard, amplitude estimate, LLR and hysteresis are reused), §11 (benchmark definitions); `docs/plans/2026-09-27-milestone-2a-results.md` (baseline numbers, regressions R1 and R2, the start-up runaways).

## Scope of this plan

Implements spec §7, stage 1:

1. **Recording** each oracle channel's stream from `kz4ap-bench` (Task 2).
2. **Scoring** externally decoded text with the bench's scoring, not a copy of it (Task 3).
3. **Farnsworth spacing** in the generator, with a full-suite group (Task 1).
4. **The Python prototype** of every component of spec §4, each with unit tests on synthetic signals whose expected values are derived (Tasks 4–11).
5. **A runner** over the 3-seed full suite that scores through the bench and summarizes against Matched and Envelope (Task 12), with oracle copies of the detector-only groups so every regime is compared, and **the detector path** (Task 11b: record the detector's channels, score tracks by frequency; Task 12 runs it) for group H through the detector, band, crowded and the late-opening case (owner, 2026-09-30).
6. **Experiments** that settle the §6 placeholders by measurement (Tasks 13–14), and **a final evaluation** that writes the measured values into the spec and gives the owner the full comparison — group A crossings, R1, R2, the start-up runaways, and every regime better, worse or unchanged beyond its interval, by how much — with no pass/fail gate (Task 15). The experiments' pre-registered rules choose parameters; they are not the verdict.

Deliberately **not** in this plan: the C++ bank (`--front-end bank`, stage 2); corrections in the engine's text events and in the bench's scoring (stage 2: here the prototype applies its corrections itself and the bench scores the final text); any change to the engine's signal processing; tuning the Envelope or current Matched paths; confirming the detection measures (detection recall, false tracks, tracks per QSO) behind the live detector with the frequency tracker: stage 1 reports them on the recorded channels of the Matched path's detector (as the bench counts them they depend on the decoder), and stage 2 confirms them in C++ (spec §7; backlog "Stage-2 evaluation of the filter bank through the detector"). Which tracks the detector opens is unchanged by the redesign.

**Stage 1 changes no engine signal processing.** The engine gains one observation hook (`Engine::set_channel_tap`) that copies nothing into and changes nothing in the decoding path; the bench additions and the prototype are tooling. `docs/signal-processing.md` still gets its benchmark definitions (§11: recorded channel streams, externally decoded text, Farnsworth spacing) in the same commits, because §11 describes the benchmark.

## Global Constraints

- C++20; CMake minimum 3.25; GoogleTest; the engine must not depend on Qt; no new third-party dependencies (C++ or Python). Python: the existing 3.12 `.venv` with numpy and pytest only; anything else needs the owner's approval.
- Namespace `kz4ap`; public headers in `engine/include/kz4ap/`, sources in `engine/src/`, tests in `engine/tests/`; bench code in `bench/src/`, `bench/tests/`; generator in `training/kz4ap_synth/`; prototype in `training/kz4ap_proto/`; Python tests in `training/tests/`.
- Determinism (spec §4.1): the same recording and settings produce identical output. Every random draw in the generator and in tests comes from a seeded numpy `Generator`; the prototype has no randomness.
- **The Envelope and current Matched paths stay bit-identical.** Their engine output, the smoke recording's bytes and the smoke CERs (Envelope 0.0353, Matched 0.0436, `docs/plans/2026-09-27-milestone-2a-results.md` §3.5) must not change. `bench/baselines/*.json` are not edited by any task. The `smoke` suite must not change.
- **Physical units (owner's standing rule).** Parameters and calculations are in physical units (Hz, s, FS, FS², dB with a named reference), never in bins or samples; convert at the point of use (for example `int(round(cfg.segment_s * rate_hz))`). Counts of physical things (marks, spaces, characters, branches, selection instants) are fine. Config fields are named for their unit.
- **`docs/signal-processing.md` describes the engine's signal processing as it actually is.** Any change to signal processing — a parameter value, an algorithm, the order of stages, a new stage — updates that document in the same commit, including its parameter table and whether each choice is derived, measured or heuristic. Treat a stale description as a bug. Stage 1 changes no engine signal processing; the benchmark definitions it adds go into §11 in the same commit as their code. (Every implementer of this plan must be told this rule, together with the physical-units rule.)
- **Units** (project `CLAUDE.md`): every displayed, logged or documented quantity carries an explicit unit. Every dB value names its reference (dBFS, dB SNR in a stated bandwidth, dB relative to the passband, …); a bare "dB" is a bug. Linear signal values are in FS (amplitude) and FS² (power). S₅₀₀ always means key-down carrier power over noise power in 500 Hz, dB.
- **No tuning toward the benchmark without recording it.** Every placeholder value changed by this plan is changed by an experiment task whose procedure, metric and decision rule are written before it runs, and whose result (seeds, intervals, the rule's outcome) is recorded in the results document. If no setting meets a rule's criterion, report to the owner; do not invent a new rule.
- American spelling in code, comments and docs.
- **Git:** all work on branch `milestone-2b-filter-bank`; commit at the end of each task without waiting for approval. Run each git command as its own call: `git add …` then `git commit -m "…"` — no heredocs, no `cd … &&`, no chaining, no `git -C`. Never amend, rebase, reset, push or merge. **No `Co-Authored-By` or other attribution lines** in commit messages (a hook rejects them).
- I/Q convention: left channel = I (real), right channel = Q (imaginary). Recorded streams are complex64, little-endian, I and Q interleaved (numpy dtype `<c8`).

## Environment (Windows, run once per PowerShell session)

CMake and Ninja ship with Visual Studio Build Tools 2026 but are not on `PATH`:

```powershell
$env:Path = "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;$env:Path"
$env:PYTHONPATH = "training"
```

Common commands (from the repository root):

- Configure: `cmake --preset windows`
- Build: `cmake --build --preset windows`
- Run all C++ tests: `ctest --preset windows`
- Run matching C++ tests: `ctest --preset windows -R <regex>`
- Python tests: `.venv\Scripts\python -m pytest training -q`
- One Python test file: `.venv\Scripts\python -m pytest training/tests/test_proto_fit.py -q`
- Bench executable: `build\windows\bench\Release\kz4ap-bench.exe`
- Smoke check (Git Bash): `bash bench/smoke.sh build/windows`
- Suite tools: `.venv\Scripts\python -m kz4ap_synth.suites …`; prototype tools: `.venv\Scripts\python -m kz4ap_proto.runner …` and `… -m kz4ap_proto.experiments …` (with `PYTHONPATH=training`).
- The 3-seed full suite is in `build/suite/full3` (117 recordings, results for `baseline` and `matched` in `results/`); Task 1 adds group I to it.

## Symbols

Where a symbol exists in `docs/signal-processing.md` §0 it means the same here: **Δf is the FFT bin width (23.4 Hz), D the decimation factor, D_ch the channel distance, σ² the noise power per complex sample, σ_v the noise RMS per real component of a filter output, f_off a station's offset from its channel's center, τ_L the tracker's lag.** The spec calls the dah/dit ratio r; this plan writes it **q**, because r is the channel sample rate.

| Symbol | Meaning | Unit / default |
|---|---|---|
| r | channel sample rate (the channelizer's output) | 1500 samples/s |
| y[n] | recorded channel sample (channelizer output, before the tracker) | FS (complex) |
| u[n] | y mixed down so the labeled carrier sits at 0 Hz (oracle re-centering) | FS (complex) |
| k | branch index, 1 … K | K = 32 |
| L_k | branch length, 9.6 ms × 1.1^(k−1); realized as N_k = round(L_k·r) samples, and every later use of L_k is N_k/r | s |
| v_k[n], P_k[n] | branch output, (1/N_k)·Σ of the last N_k values of u; its power \|v_k\|² | FS; FS² |
| H_k(f) | branch response, sin(πfN_k/r)/(N_k·sin(πf/r)) | dimensionless, 1 at 0 Hz |
| S_n(f) | noise power spectrum of u (two-sided, complex) | FS²/Hz |
| σ_v,k² | noise variance per real component of v_k; complex noise power of v_k is 2σ_v,k² = ∫S_n(f)\|H_k(f)\|² df | FS² |
| s_k, a_k | key-down amplitude of v_k; a_k = s_k/σ_v,k | FS; dimensionless |
| x | \|v_k\|/σ_v,k | dimensionless |
| Λ, g, p | LLR −a²/2 + ln I₀(a·x); g = Λ + ln(P₁/P₀); p = 1/(1 + e^(−g)) | nats; nats; 0…1 |
| P₁ | prior probability of key-down | 0.44 |
| h | keying hysteresis on g | 1 nat |
| a_min,k | squelch: key-down only if a_k ≥ 3·(L_k/16 ms)^(1/4) | dimensionless |
| x_on,k, x_off | unknown-amplitude test (start of an over): key down when x > x_on,k, up when x < x_off = √(−2 ln 0.3) = 1.55; x_on,k nominally √(−2 ln(R_fa·L_k)), calibrated by measurement in E9 | dimensionless |
| R_fa | target rate of false key-downs that noise alone causes in the unknown-amplitude test, per branch | 0.01 /s (heuristic target; the nominal formula gives 0.07–0.10 /s, simulated in review) |
| W_min | keyed time (samples the unknown-amplitude test keys down, / r) in an over before its start is re-keyed with the full LLR | 0.4 s (placeholder) |
| κ, κ_n | three-tap noise guard (milestone 2) | 1.75, 4 |
| T_seg | noise-spectrum segment | 256/1500 s = 170.7 ms (bin 5.86 Hz) |
| τ_n, τ_a | noise and amplitude time constants | 2 s, 0.5 s |
| T | dit (WPM = 1.2 s / T) | s |
| q | dah/dit ratio (the spec's r) | 3 normally, 3.5–5 for bug keying |
| w | key weighting: marks longer by w, spaces shorter by w | s |
| T_g | gap timebase: character gap ≈ 3T_g − w, word gap ≈ 7T_g − w | s (= T with standard spacing) |
| σ_ln | log-normal scatter of durations | 0.15 marks, 0.25 spaces (heuristic) |
| σ_t,k² | a branch's timing-resolution variance, 2(L_k/a_k)² + 2/(12 r²) | s² |
| ε | outlier-class prior; the outlier class is log-uniform over 1 ms … 10 s | 0.05 |
| N_mem, λ | fit memory (marks and spaces) and its per-observation decay, λ = e^(−1/N_mem) | 24; 0.959 |
| Q_k | fit quality: weighted mean log-likelihood per element | nats per element |
| T_P, c_P | periodicity estimate and its confidence | s; comb: dimensionless, spectrum: nats |
| Π | the comb's period: a dit and its element space, 2T | s |
| e[n] | signed edge signal of p, p[n] − p[n−1] (the edge comb, third variant of E1) | 1 per sample |
| M | switch persistence: selection instants in a row | 4 (placeholder) |
| ε_Q | a quality tie: Q within ε_Q of the best | 0.05 nats per element (placeholder) |
| T_new | new-over silence threshold, max(0.5 s, 12·T_g) | s |
| T_c | the ARRL Farnsworth added time per PARIS, 60/s − 37.2/c; T_g = T_c/19 | s |

## Design decisions

Each choice is labeled **derived**, **measured**, **heuristic**, **placeholder** (a heuristic an experiment of this plan sets), or **owner**.

### Where the streams are recorded, and why before the tracker

The bench records the **channelizer's output y[n]**, before the frequency tracker, and Python mixes it down at the labeled frequency: u[n] = y[n]·exp(−j(2π·f_off·t + π·ḟ·(t − t₀)²)), f_off = labeled frequency − channel center, ḟ and t₀ the label's drift and start (the generator's own phase law, `docs/signal-processing.md` §11 "Drift"). Reasons:

1. **The tracker's input comes from the front end being replaced.** It observes the matched filter's output v and weights it by the Matched front end's key-down probability (§7, "Frequency re-centering"); a post-tracker recording would carry the old speed-following filter, and its runaways, into the new estimators' input. Recording before the tracker keeps the old loop out of stage 1 entirely.
2. **The tracker itself is unchanged by the redesign** (spec §2), and where it locks it is accurate: median error 0.11 Hz in group F at S₅₀₀ = 5 dB (measured, results §3.4). Through the longest branch (184 ms) that costs |sinc(0.11 Hz × 0.184 s)|² → 0.005 dB relative to a centered station (derived). So the labeled mix is a faithful stand-in for a locked tracker.
3. **It is deterministic and exact** for the synthetic suite: the generator places each carrier exactly at its label.

**Consequences, stated not fixed:** (a) group F's drift rows are favorable to the prototype (its mix follows the labeled drift exactly; Matched's oracle anchor cannot follow beyond ±12 Hz) — not comparable; (b) a QSO label's channel is mixed at the caller's frequency, so an answering station 10–200 Hz away is off-center for the prototype (Matched fine-tunes within ±12 Hz) — group H oracle QSO-label rows with a nonzero offset are handicapped, and the per-station labels are the comparable view; (c) **stage 2 must decide which branch's v and p feed the tracker** (an open design point for the owner; the prototype does not need it).

### Scoring with the bench's own code

`kz4ap-bench --labels L --score-decoded D.json` reads one text per label (in the labels file's order, as oracle channels are numbered), builds tracks 1…n at the labels' frequencies, and runs the same `score()` and the same JSON writer (moved from `main.cpp` into `bench/src/report.cpp`, used by both paths). `tracked_freq_hz` is null for decoded text (the prototype estimates no frequency). Task 3 checks equivalence end to end: the bench's own oracle run and `--score-decoded` of its own texts give identical `score` objects apart from `tracked_freq_hz`.

### Farnsworth spacing

Elements and element spaces at the character speed c (T = 1.2 s/c); character and word gaps on a timebase T_g > T, keeping 3 : 7. T_g follows the ARRL standard for an overall speed s: the added time per PARIS, T_c = 60/s − 37.2/c seconds, is spread over PARIS's 19 gap units (four character gaps of 3, one word gap of 7), so **T_g = (60/s − 37.2/c)/19** (J. Bloom, KE3Z, "A Standard for Morse Timing Using the Farnsworth Technique," QEX, April 1990; formula quoted from memory of that standard, not checked against the paper here: the executor verifies it before relying on it and records the check). It is consistent by construction (derived): PARIS is 31 units at T plus 19 at T_g, and 31·1.2/c + (60/s − 37.2/c) = 60/s, one PARIS word per 60/s seconds; at s = c, T_g = 1.2/c = T. Group I: (c, s) = (18, 5), (18, 10), (25, 13), (25, 18) WPM, so T_g/T = 7.84, 3.11, 3.43, 2.02 (derived).

### The filter bank

L_k = 9.6 ms × 1.1^(k−1), k = 1…32 (owner). At r = 1500 samples/s, N_k = 14, 16, 17, 19, 21, 23, 26, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107, 117, 129, 142, 156, 172, 189, 208, 228, 251, 276 samples (derived; all distinct because consecutive lengths differ by at least 0.1 × 14.4 = 1.44 samples). The realized length N_k/r (9.33 ms … 184.0 ms) is used everywhere after the point of use (eligibility, timing resolution, group delay). Each branch is the engine's boxcar (causal running mean, unity gain); its group delay (N_k − 1)/(2r) is subtracted from every time the branch reports, so times of different branches line up (derived: a symmetric FIR has linear phase).

### Noise: the level is decided by measurement (owner, 2026-09-30)

The channel's noise is **not white**: the channel filter passes about ±150 Hz of the 1500 Hz stream (§7), so the in-band density is about 5× the white-noise density σ²/r, and the white-noise formula σ²/(2N_k) underestimates **every** branch's noise, by 4.79× (N = 14) to 5.19× (N = 276) in Task 5's channel-shaped noise (review check). The spectrum's *shape* correction across the ladder is only about 8% there; it matters mainly when a neighbor leaks into the short branches. So each branch needs σ_v,k² = ½∫S_n(f)|H_k(f)|² df, and what matters most is its absolute level.

Common parts:
- **Shape:** Ŝ, an exponential average (τ_n) of Hann-windowed periodograms of u over segments of T_seg = 170.7 ms, each masked: a sample of u is left out if any |v_1|² sample that contains it (the next N_1 − 1 samples) or lies within 20 ms of it exceeds κ_n·2σ_v,1². A segment enters only if at least half of it is left in (heuristic). W_k[m] is the mean of |H_k(f)|² over bin m (16 points per bin); for white noise (W_k·Ŝ)/(W_1·Ŝ) = N_1/N_k exactly (Parseval, derived). Ŝ is smoothed over ±25 Hz before use, because the longest branches see only 2–3 bins near 0 Hz (derived: the degrees of freedom are B·τ either way; smoothing assumes the noise is smooth on a 25 Hz scale — heuristic). Until branch 1's three-tap warm-up is over, no segment enters and the shape is white. **The mask is not neutral:** in white noise it keeps only 63% of noise-only samples, and the kept samples' power reads 2.1% low (review, 3·10⁶ samples); Task 5 measures both numbers and the segment acceptance rate on the prototype's own code.
- **The mask's reference:** branch 1's three-tap estimate (below), in every variant.

The variants of **E10** (owner, 2026-09-30: measure both levels and decide by a pre-registered rule; spec §4.2 and §5 now say so):
- **(a) `spectrum` — three-tap level, spectrum ratios:** σ_v,1² from the milestone-2 three-tap guard, unchanged (κ = 1.75, κ_n = 4, the truncation correction m(κ) = 0.632, τ_n = 2 s, warm-up 0.32 s at the 20% quantile; §8b; branch 1 has about 107 independent samples per second, so about 7% scatter over τ_n, derived, rough), and σ_v,k² = σ_v,1²·(W_k·Ŝ)/(W_1·Ŝ). The mask's bias cancels in the ratio. *[Corrected 2026-10-02, final review: the mask's bias does **not** cancel in the ratio. The mask removes mostly low-frequency power, so its bias differs per branch: uncorrected, branch 32's level relative to branch 1's reads 6.2% low in white noise and 3.5% low in channel-shaped noise (Task 5 review, measured). By the Task 5 ruling the code multiplies branch k's ratio by b_mask,1 / b_mask,k from the per-branch table `mask_bias`.]*
- **(b) `spectrum-level` — spectrum level, bias-corrected:** σ_v,k² = ½·(W_k·Ŝ)/(M·b_mask), with M the segment length in samples and b_mask the ratio of the masked periodogram's mean power to the true noise power in white noise (0.979 in review; Task 5 measures the prototype's own value, stored as `mask_bias`, status measured). Before the first segment enters, variant (a)'s value is used. *[Corrected 2026-10-02, final review: b_mask is not one number but a per-branch table b_mask,k, 32 values from 0.8370 (k = 1) to 0.7852 (k = 32), measured in white noise (Task 5 ruling), and σ_v,k² = ½·(W_k·Ŝ)/(M·b_mask,k); the whole-band value (0.9745 on the prototype's code, 0.979 in review) is not used.]*
- **(c) `branch` — the recorded fallback** (spec §4.2): every branch runs its own three-tap estimate.

### Keying per branch

Kept from milestone 2, per branch: the p-weighted online-EM amplitude (τ_a = 0.5 s of key-down weight), Λ = −a²/2 + ln I₀(a·x), g = Λ + ln(P₁/P₀), keying with ±1 nat hysteresis, and the squelch a_min,k = 3·(L_k/16 ms)^(1/4) (the milestone-2 principle — a constant chance that noise lifts â past it — with each branch's own σ_v: in noise alone â² is a mean over about τ_a/L_k independent samples, so its spread grows as √L_k and a_min as L_k^(1/4); derived scaling, heuristic constant 3). The estimates advance once per 21.3 ms block with the block's starting σ² and s² (τ_n, τ_a ≫ one block; heuristic, for speed). Keying is then exact per sample within the block (vectorized hysteresis). **At high SNR a keyed rectangular mark keeps its length through a matched branch**: the ±1 nat crossings sit at x ≈ a/2 + O(ln a / a), half the amplitude, where a boxcar's rising and falling ramps are L apart, so the measured duration equals the true one (derived; Task 6 checks it).

### The first marks of an over: the unknown-amplitude test is a threshold on x

Spec §4.7 offers a generalized likelihood ratio maximized over the amplitude, or a threshold on |v|/σ_v. **Per sample they are one test family:** max over a ≥ 0 of (−a²/2 + ln I₀(a·x)) is 0 for x ≤ √2 (the derivative at a = 0 is a(x²/2 − 1)) and strictly increasing in x above (derived; checked numerically in review), so a threshold on the per-sample GLRT is a threshold on x = |v|/σ_v. This holds per sample only: a GLRT that fits one amplitude over a whole mark or block (summing over its samples) is not a per-sample threshold on x. The prototype uses the per-sample reading; the only choice is the threshold.

Nominally x_on,k = √(−2 ln(R_fa·L_k)): noise alone gives about 1/L_k independent envelope samples per second, each above x with probability e^(−x²/2) (Rayleigh). **That formula is heuristic, not derived:** it ignores the envelope's upcrossings between independent samples (Rice), and the review simulated 0.070 /s at 9.3 ms, 0.099 /s at 40 ms and 0.097 /s at 184 ms against the 0.01 /s target, 7–10× too many. So **E9 first calibrates x_on,k by measurement** (pre-registered: per branch, the threshold at which channel-shaped noise alone keys down R_fa times per second; `experiments calibrate-x-on`), stores it as `x_on_values` (status measured), and only then compares R_fa targets. Release at x < x_off = 1.55 (noise exceeds it 30% of the time; heuristic).

The over's first marks are keyed with this test at once. **While the amplitude is unknown, only keyed samples count** (review I5: a reset amplitude makes p = P₁ = 0.44 on every sample, so a p-weight would fill about 0.9 s after the first mark whatever the marks): the fresh amplitude is seeded as the mean of |v|² − 2σ_v² over the samples the unknown test keys down *[Corrected 2026-10-02, final review: the code seeds it as the 0.9 quantile of |v|² over those samples, minus 2σ_v² (the plan's fix round; a mean would be pulled down by the boxcar's ramps, which the test keys)]*, and W_min = 0.4 s (placeholder) is counted in keyed time. When it is reached, the stretch from the over's start (at most 20 s back) is re-keyed with the full LLR; each candidate amplitude (the seeded one, and the previous over's) and each candidate fit (fresh, and the previous over's continued) is tried, and the combination whose fit explains the re-keyed marks and spaces best (mean log-likelihood per element) wins (spec §4.7: "a comparison of two fits, not a feedback loop"). The stretch is decoded again with the winning fit and the difference is issued as a correction. From then on the amplitude follows the milestone-2 p-weighted EM.

A new over starts when the key has been up for longer than T_new = max(0.5 s, 12·T_g) since a key-up (spec §4.7, placeholder). A branch without a fit uses T_g = 1.2 s / 5 WPM, the slowest standard dit (T_new = 2.88 s, derived from the spec's formula).

### Duration fit

Model (spec §4.5): marks: dit T + w, dah qT + w; spaces: element T − w, character 3T_g − w, word 7T_g − w; each log-normal in duration with σ_ln (0.15 for marks, 0.25 for spaces; heuristic, between VE3NEA's Computer and HandKey styles) plus the branch's timing-resolution variance σ_t,k² = 2(L_k/a_k)² + 2/(12r²) (derived, first order at high SNR: an edge through a boxcar is a ramp of slope s/L, so noise of RMS σ_v moves the crossing by L/a; two edges per duration; sampling adds 1/(12r²) per edge), and an outlier class (ε = 0.05, log-uniform over 1 ms … 10 s; heuristic).

**Class priors are derived** from VE3NEA's tables (`messages.VE3NEA_CHAR_WEIGHTS`, `VE3NEA_WORD_LENGTH_PROBS`, MIT): per character 1.618 dits and 1.212 dahs, so marks are dits with probability 0.5716; per character 1.830 element spaces, 0.673 character gaps and 0.327 word gaps (mean word length 3.062 characters), so spaces are element spaces 0.6467, character gaps 0.2379, word gaps 0.1154 (computed while planning).

**Memory by recursion:** every observation multiplies both likelihood tables by λ = e^(−1/N_mem) and adds its log-likelihood to one of them, so the tables hold the exponentially weighted log-likelihood exactly at every grid point (derived). Marks are tabulated over (T, q, w) and spaces over (T, w, T_g); the joint maximum is max over (T, w) of [max_q marks + max_(T_g) spaces] + the T_P prior. Grid: T log-spaced at 1% over 12–240 ms (302 points), q ∈ {3, 3.5, 4, 4.5, 5}, w/T ∈ {−0.4 … 1.0} in steps of 0.2, T_g/T ∈ {1, 1.26, 1.59, 2, 2.52, 3.17, 4, 5.04, 6.35, 8} (placeholders; w reaches +1.0·T because a branch longer than the ideal lengthens marks by up to its own length). **Local refinement:** two iterations of weighted least squares in the durations themselves *[Corrected 2026-10-02, final review: the code uses two Gauss–Newton steps in ln d instead (Task 7 ruling: least squares on raw durations estimates means, while the model's parameters are log-normal medians, biasing T by +2.3%), kept only if the weighted log-likelihood does not fall]*, (T, w, qT, T_g) linear in every class mean, with the class responsibilities of the current point and a weak prior (standard deviation 0.2·T per parameter) toward it, over the last 4·N_mem observations weighted λ^age. The T_P prior adds −(ln T − ln T_P)²/(2·0.1²) to the grid's log-likelihood (heuristic width) whenever T_P is confident, and nothing otherwise: "weighted by its confidence" (spec §4.5) is **reinterpreted** as a gate at the confidence threshold, because the three methods' confidences are on different scales (heuristic; listed for the owner in the results' open items).

**Checked while planning** (a scratch numpy port of the grid step, not committed): the grid maximum gives T within 0.4% for machine keying at 25 WPM; T = 99.9 ms for "HI" at 12 WPM (the R2 case: all marks dits, the element spaces decide), where the dahs reading T = 33 ms loses because element spaces (prior 0.647) beat character gaps (0.238) four times and dits beat dahs six times; T_g/T = 3.17 (grid) for Farnsworth 18/10 (true 3.11) and 8.0 (grid edge) for 18/5 (true 7.84); q = 4.5 for HandKey; w/T = 0.2 for a 0.2-dit imbalance; after a 20 → 35 WPM step, T = 34.5 ms (true 34.3) after 72 new observations and 37.3 ms after 36.

### Periodicity estimator: three variants, the comb on 2T the default (owner, 2026-09-30)

Input: branch 1's squelched posterior p (0 while squelched), averaged down to 750 samples/s (Nyquist 375 Hz, above the third null of the fastest dit, 3/12 ms = 250 Hz; heuristic). Windows 2, 5 and 10 s run in parallel (placeholders); the confident estimate with the shortest window is used; T_P is recomputed every 0.25 s (heuristic).

- **Why the literal comb lands on 2T.** The comb as first written in spec §4.4 (positive teeth at T, 2T, 3T, 4T, negative teeth halfway) peaks at 2T on every keying style tried (planning check: comb score at T/2, T, 2T, 3T = −0.011, −0.025, 0.315, −0.035 at 25 WPM machine; review: 0 of 3 seeds right in every case, median ratio 2.00). Consecutive keying edges T apart have opposite signs (a dit's key-down and key-up), so p's structure repeats at 2T, not T: the autocorrelation of mean-removed p is low at odd and high at even multiples of T (derived: between lattice points it is piecewise linear, so a tooth's contrast at kT is −¼ of the lattice autocorrelation's second difference). The spec's §4.4 now says so (owner, 2026-09-30).
- **Comb (default, `comb`; owner).** On mean-removed p, the comb runs on the period Π = 2T of a dit and its element space: 4 teeth at 2T, 4T, 6T, 8T, negative teeth halfway (at T, 3T, 5T, 7T), each the mean of the normalized autocorrelation within **±15% of T (±0.075 Π)** of its lag, and T_P = Π/2. Its last tooth is at 8T and it reads lags up to (4 + 0.5 + 0.075)·2T ≈ 9.2T. Farnsworth still does not disturb it: its stretched gaps are 3·T_g ≥ 3T long and fall at no fixed multiple of 2T, so they add no tooth-locked structure, while the elements inside characters keep their 2T lattice. Measured (review, 3 seeds, clean and at S₅₀₀ = 10 and 3 dB): within 5% of T for machine, paddle and computer keying at 12–100 WPM in every window, for Farnsworth 18/10 and 25/13 (18/5: 2 of 3 at 2 and 5 s), and at 5 WPM with windows ≥ 5 s even at S₅₀₀ = 3 dB; it misses HandKey (1.13–1.22 T) and bug keying with 2 s windows. The ±0.075 Π width gave the same hit rates as ±0.15 Π in review.
- **Sign-weighted edge comb (third variant, `edge`).** On the signed edges e[n] = p[n] − p[n−1]: teeth at kT, k = 1…4, weighted −1 at odd and +1 at even k (key-down and key-up alternate), each within ±15% of T of its lag, on the normalized autocorrelation of e. Review: best on HandKey and bug keying at S₅₀₀ ≥ 10 dB, but at 3 dB it fails 5 WPM in every window and 12 WPM at 2 s (differencing whitens the noise; slow keying has little edge energy).
- **Spectrum-shape fit (`spectrum`).** p is piecewise constant on a T lattice, so every mark's spectrum carries sinc(f·d) with d a multiple of T, and the power spectrum has nulls at f = k/T whatever the marks' positions (derived). Score(T) = mean over k = 1…3 of ln(power in [(k − ½)/T, (k + ½)/T] / power within ±0.075/T of k/T), from a Hann-windowed periodogram zero-padded 4×; T_P is the maximum. Checked while planning: within 1% for machine keying at 25–100 WPM, 1–6% at 12 WPM, within 4% for paddle and Farnsworth; wrong for HandKey. (A rule preferring the longest T near the maximum picked 3T for paddle keying and was rejected.) Its null width is its own parameter (`spectrum_null_width` = 0.15 T), separate from the comb's tooth width, so E3 varies them independently.
- **Confidence:** each method's score (comb and edge comb dimensionless, spectrum in nats); the thresholds are placeholders, calibrated in E1 to 95% precision.

### Branch selection, corrections

As spec §4.6: eligibility |ln(L_k/(0.8·T_k))| ≤ ln 1.1, and only once the fit's memory holds at least 8 elements of weight (heuristic); among eligible branches the highest Q_k; ties (within ε_Q) by the mean text log-probability of the last 10 characters (ties there within 0.1 nats per character; heuristic), then the longer branch; none eligible: the text log-probability if the best exceeds the second by at least 1 nat per character (heuristic "clearly separates"), else the branch nearest 0.8·T_P if T_P is confident, else branch 1. Selection instants are branch 1's key-ups (it resolves every mark); a switch needs the same other branch to be the best **eligible** branch for M = 4 instants in a row (placeholder; spec §4.6). When no branch is eligible, the fallback pick is counted separately (M instants in a row of the same fallback pick), so fallback instants never complete an eligible run or the other way round. After a switch the new branch's text replaces the old from the start of the character that contains the time the new branch's current eligible run began, at most 20 s back (owner). The prototype publishes each selected-branch character when it is decided, applies its corrections, logs each correction with its reach, and hands the final text to the bench.

### Compute budget, and runs in the background

The review timed the plan's fit at 1.49 ms per observation (add + best) on the default grid and 22.9 ms on E5's finest; at 25 WPM each branch sees about 10.6 observations per second, so **a keyed channel costs about 0.3–0.5 s of CPU per channel-second** (Task 11 Step 5 times a keyed stream to confirm it on this machine). The development set holds about 80 000 channel-seconds, so one development run takes about 25–30 min on 14 workers; Tasks 13–14 need about 28 runs, so **the experiments take about 12–14 h of wall time on 14 workers, or more** (E5's finest grids cost about 15× a run and use a smaller subset, `experiments.DEV_E5`, about 20% of the development set), plus about 2 h for the final three-seed pass (Task 15). The oracle copies of the detector-only groups add 27 recordings (9 per seed): recording their streams takes a few minutes and about 0.2 GB, the extra Envelope and Matched bench runs (54, with `--oracle`) about 2 min, and the prototype about 18 000 channel-seconds more, about 5% of the final pass. The detector path (Task 11b) adds 33 recordings: recording the Matched path's detector channels takes a few minutes and about 0.5 GB, and the prototype decodes about 44 000 channel-seconds more (the detector runs' channel-seconds, milestone-2a results §3.3), about 12% of the final pass, about 15 min on 14 workers. Every long run is started as a **background process** (the harness's `run_in_background`), writing its log to `build/suite/full3/experiments/logs/<name>.log` and a short summary to `build/suite/full3/experiments/summary-<name>.md` when it ends; the executing agent starts it, does not poll, is notified when it finishes, and reads only the summary file (and the compare file it names).

### Development set, held-out seeds and decision rules

Experiments run on **seed 1** of the oracle recordings of groups A, C, D, E, F, G, H (oracle copy, both views), I, and the B mixed-style recording (the development set; group B's per-row recordings are left out of development because they are 60% of the channel-seconds). The final configuration runs on all three seeds; seeds 2 and 3 are reported separately as held out. Each experiment changes one parameter group from the current reference, and a change is adopted only if its decision rule says so (written in Tasks 13–14 before they run); otherwise the placeholder stays and is recorded as "kept, not rejected by measurement".

## Parameters

| Parameter | Value | Status | Set by |
|---|---|---|---|
| Speed range | 5–100 WPM | owner | — |
| Ladder | 9.6 ms × 1.1^(k−1), 32 branches | owner; loss derived | — |
| Branch length relative to the dit | 0.8 | heuristic | — |
| Estimate update block | 21.3 ms | heuristic | — |
| Noise method | (a) three-tap level × spectrum ratios; (b) spectrum level ÷ mask bias; (c) per-branch three-tap | open; owner: decided by measurement | E10 |
| Mask bias b_mask,k | per branch, 0.8370 (k = 1) to 0.7852 (k = 32), white noise (corrected 2026-10-02: a table, not one number; the review's whole-band 0.979 is not used) | measured | Task 5 |
| T_seg; smoothing; guard reach; clean fraction | 170.7 ms; ±25 Hz; 20 ms; 0.5 | heuristic | — |
| Three-tap guard κ, κ_n; τ_n; warm-up | 1.75, 4; 2 s; 0.32 s | milestone 2 (heuristic) | — |
| τ_a; P₁; h | 0.5 s; 0.44; 1 nat | milestone 2 (heuristic) | — |
| Squelch | 3·(L/16 ms)^(1/4) | scaling derived, constant heuristic | — |
| R_fa (target); x_on per branch; x_off | 0.01 /s; calibrated to the target; 1.55 | heuristic target; measured; heuristic | E9 |
| W_min (keyed time) | 0.4 s | placeholder | E9 |
| Fit memory N_mem | 24 | placeholder | E4 |
| Grid: T step; q; w/T; T_g/T | 1%; 5 values; 8 values; 10 values | placeholder | E5 |
| σ_ln (marks, spaces); ε; outlier range | 0.15, 0.25; 0.05; 1 ms–10 s | heuristic | — |
| T_P prior width | 0.1 in ln T | heuristic | — |
| Minimum fit weight | 8 elements | heuristic | — |
| Periodicity method | comb on Π = 2T (default, owner); edge comb; spectrum | open (default set by owner) | E1 |
| Windows | 2, 5, 10 s | placeholder | E2 |
| Comb teeth / tooth width / lag reach | 4 teeth on Π (last at 8T) / ±15% of T (±0.075 Π) / about 9.2T | placeholder / placeholder / follows from the teeth | E3 |
| Edge-comb teeth; spectrum nulls / null width | 4 at kT, weights (−1)^k; 3 / ±0.075/T | placeholder | E3 |
| Confidence thresholds | comb 0.03; edge comb 0.03; spectrum 1.5 nats | placeholder | E1 |
| Eligibility tolerance | ln 1.1 | heuristic (one ladder step) | — |
| M | 4 | placeholder | E6 |
| ε_Q; text window; text tie; separation | 0.05 nats/element; 10 characters; 0.1; 1 nat/character | placeholder; placeholder; heuristic; heuristic | E8 |
| T_new | max(0.5 s, 12·T_g) | placeholder | E7 |
| Correction reach | 20 s | owner | — |
| Fresh fit's minimum observations; its penalty | 8 marks and spaces; ½·k·ln n nats (k = 4, n = observations; BIC-style) | placeholder; heuristic (controller ruling, Task 11 review; added 2026-10-02) | — |
| Re-key time-out | 2.0 s of channel time after the amplitude became unknown | placeholder, heuristic (controller ruling, Task 11 review; added 2026-10-02) | — |
| Seed memory while the amplitude is unknown | 4 × W_min of keyed time | heuristic (Task 6; added 2026-10-02) | — |
| Local refinement | 2 Gauss–Newton steps in ln d | heuristic (Task 7 ruling; added 2026-10-02) | — |
| Periodicity update interval; rate of p | 0.25 s; 750 samples/s | heuristic (Task 9; added 2026-10-02) | — |
| Spectrum fit's front-end floor | −20 dB relative to the DC power gain of branch 1's |H(f)|² | heuristic (Task 9 ruling; added 2026-10-02) | — |

*Note 2026-10-02:* the values above are those planned. The values stage 1 adopted are in `docs/plans/2026-09-30-milestone-2b-stage-1-results.md`, section 3.13, and in `ProtoConfig` (`training/kz4ap_proto/params.py`).

## Review Focus

Inputs the spec implies but does not spell out, most likely to bite first. Each has a test in the owning task.

1. **A station keying from the first sample** (no noise-only start): the noise level must come down from the marks-inflated warm-up and the shape must not take the marks in; the first over is still decoded — Task 5 (`test_spectrum_keeps_marks_out_when_a_station_keys_from_the_first_sample`), Task 11 (`test_decodes_a_station_from_the_first_sample`).
2. **Noise alone and long silences:** no text survives in the final output, and the unknown-amplitude test's false key-downs are counted (the nominal threshold is known to key 7–10× more than R_fa; E9 calibrates it) — Task 6 (`test_unknown_amplitude_test_keys_noise_rarely`, `test_an_unknown_amplitude_counts_only_keyed_samples`), Task 11 (`test_noise_alone_leaves_no_text`).
3. **Farnsworth word gaps (7·T_g, up to 3.7 s at 18/5 WPM) must not start a new over** — Task 11 (`test_farnsworth_word_gaps_do_not_start_a_new_over`).
4. **A long carrier (tune-up) or a stuck key** must go to the outlier class and leave the speed intact — Task 7 (`test_a_tune_up_carrier_is_an_outlier`), Task 11 (`test_a_tune_up_carrier_does_not_derail_decoding`).
5. **Speed changes and slow starts** (regressions R1 and R2): a slow station's first dits are not read as dahs, and a 15 → 30 WPM step is followed within about 10 marks — Task 7 (`test_slow_first_dits_are_not_read_as_dahs`, `test_the_fit_follows_a_speed_step_within_its_memory`), Task 11 (`test_follows_a_speed_step_within_ten_marks`).

Also pinned: every correction reaches back at most 20 s (Task 11, `test_corrections_never_reach_back_more_than_20_s`).

**On the suite, too:** items 1, 3 and 4 are also measured on the oracle copies of the first-sample, tune-up and pauses groups and on their detector-path runs (Tasks 11b, 12), against Envelope and Matched; the detector path adds the late-opening case (a channel that opens partway through a transmission). Stage 1 also reports detection recall, false tracks and tracks per QSO there; stage 2 confirms them behind the live detector with the tracker (spec §7).

## File map

| File | Responsibility |
|---|---|
| `training/kz4ap_synth/keying.py` | `farnsworth_gap_s`, Farnsworth gaps in `timed_intervals` |
| `training/kz4ap_synth/generate.py` | `SignalSpec.farnsworth_wpm`; labels carry it only when set |
| `training/kz4ap_synth/suites.py` | Group I; `generate --only`, `run --only`; `paired_differences(rows, a, b)` |
| `engine/include/kz4ap/engine.hpp`, `engine/src/engine.cpp` | `ChannelBlock`, `ChannelTap`, `Engine::set_channel_tap`, `Engine::channel_rate` (observation only) |
| `bench/src/channel_recorder.{hpp,cpp}` (new) | Channel files and `channels.json` |
| `bench/src/report.{hpp,cpp}` (new) | The bench's score JSON and printout (moved from `main.cpp`); decoded-text files |
| `bench/src/main.cpp` | `--record-channels` (oracle and, Task 11b, detector channels), `--score-decoded` (texts; Task 11b: tracks by frequency) |
| `training/kz4ap_proto/` (new) | `params`, `streams`, `bank`, `detect`, `noise`, `keying`, `fit`, `text`, `periodicity`, `select`, `channel`, `runner`, `report`, `experiments` |
| `training/tests/test_proto_*.py` (new); `test_keying.py`, `test_generate.py`, `test_suites.py` | Tests |
| `engine/tests/engine_test.cpp`; `bench/tests/channel_recorder_test.cpp`, `report_test.cpp` (new) | Tests |
| `docs/signal-processing.md` §6, §11 | Benchmark definitions (no DSP change) |
| `docs/plans/2026-09-30-milestone-2b-stage-1-results.md` (new, Task 13) | Experiment records and the final results |
| `docs/design/2026-09-30-filter-bank-speed-estimator-design.md` §6, new §10 | Measured values and stage-1 results (Task 15) |

## Tasks

- Task 1: Generator — Farnsworth spacing, suite group I, `--only`
- Task 2: Engine and bench — channel tap and `--record-channels`
- Task 3: Bench — `--score-decoded` with the bench's own scoring
- Task 4: Prototype foundations — parameters, streams and oracle mix, the bank, the LLR
- Task 5: Noise — the three-tap level and the shared spectrum
- Task 6: Keying — amplitude, LLR keying, squelch, unknown-amplitude test, re-keying
- Task 7: Duration fit
- Task 8: Characters and the text model
- Task 9: Periodicity estimator
- Task 10: Branch selection
- Task 11: Channel decoder — branches, new overs, re-keying, corrections
- Task 11b: Detector-opened channels — recording, scoring by frequency, anchored mixing (amendment, 2026-09-30)
- Task 12: Runner — record, decode, score, report
- Task 13: Experiment harness; noise and periodicity experiments (E10, E1, E2, E3)
- Task 14: Fit, over-start and selection experiments (E4, E5, E9, E7, E6, E8)
- Task 15: Final evaluation, the full comparison for the owner, write-back to the spec

---
### Task 1: Generator — Farnsworth spacing, suite group I, `--only`

Farnsworth-spaced text (spec §7: "not in the suite today") so the periodicity estimator and the fit are tested on stretched gaps: elements and element spaces at the character speed, character and word gaps on T_g (Design decisions, "Farnsworth spacing"). A full-suite group I uses it; `smoke` does not change. `generate --only` and `run --only` add group I to the existing `build/suite/full3` without regenerating the other 117 recordings (60 min) and score it with Envelope and Matched, so Task 12 has references for it. **Every existing recording's WAV and labels bytes must stay the same**: `farnsworth_wpm` enters a label only when set.

**Files:**
- Modify: `training/kz4ap_synth/keying.py` (new `farnsworth_gap_s`; `timed_intervals`)
- Modify: `training/kz4ap_synth/generate.py` (`SignalSpec.farnsworth_wpm`, `sending_intervals`, `labels`)
- Modify: `training/kz4ap_synth/suites.py` (group I, `write_suite(..., only)`, `run_suite(..., only)`, CLI)
- Test: `training/tests/test_keying.py`, `training/tests/test_generate.py`, `training/tests/test_suites.py`
- Modify: `docs/signal-processing.md` (§11)

**Interfaces:**
- Consumes: `kz4ap_synth.keying.timed_intervals`, `STYLES`, `word_wpm`; `generate.SignalSpec`, `labels`, `plan_signal`; `suites.Recording`, `_slots`, `_start`, `_filler`, `check_recording`.
- Produces:
  - `keying.farnsworth_gap_s(char_wpm: float, overall_wpm: float) -> float` (T_g, s; `ValueError` unless 0 < overall ≤ char).
  - `keying.timed_intervals(text, wpm, style="machine", rng=None, wpm_end=None, profile="step", imbalance_dits=0.0, farnsworth_wpm: float | None = None) -> list[tuple[float, float]]`.
  - `generate.SignalSpec.farnsworth_wpm: float | None = None` (last field); labels carry `"farnsworth_wpm"` only when it is not None.
  - `suites.farnsworth(seed: int) -> list[Recording]` (group `"I Farnsworth"`, names `I-farnsworth-{machine,paddle}-s{seed}`, tags `"farnsworth {c:g}/{s:g} wpm {keying}"`), `FARNSWORTH_ROWS`, `FARNSWORTH_SNR_DB`, `FARNSWORTH_KEYING`.
  - `suites.write_suite(recordings, out_dir, suite_name, only: str | None = None)`, `suites.run_suite(out_dir, bench, front_ends, only: str | None = None)`; CLI `generate --only REGEX`, `run --only REGEX`.

- [ ] **Step 1: Write the failing tests**

Append to `training/tests/test_keying.py` (add the imports at the top if missing):

```python
import numpy as np
import pytest

from kz4ap_synth.keying import farnsworth_gap_s, timed_intervals
from kz4ap_synth.morse import keying_intervals


def test_farnsworth_gap_timebase_follows_the_arrl_formula():
    # T_g = (60/s - 37.2/c) / 19: the added time per PARIS spread over its 19 gap units.
    assert farnsworth_gap_s(18.0, 5.0) == pytest.approx((60.0 / 5.0 - 37.2 / 18.0) / 19.0)
    assert farnsworth_gap_s(18.0, 5.0) == pytest.approx(0.52281, abs=1e-5)
    assert farnsworth_gap_s(25.0, 25.0) == pytest.approx(1.2 / 25.0)  # no stretch: the dit
    with pytest.raises(ValueError):
        farnsworth_gap_s(18.0, 20.0)
    with pytest.raises(ValueError):
        farnsworth_gap_s(18.0, 0.0)


def test_farnsworth_stretches_only_character_and_word_gaps():
    c, s = 18.0, 10.0
    dit, tg = 1.2 / c, farnsworth_gap_s(c, s)
    iv = timed_intervals("AN IT", c, farnsworth_wpm=s)
    marks = [b - a for a, b in iv]
    spaces = [iv[i + 1][0] - iv[i][1] for i in range(len(iv) - 1)]
    assert marks == pytest.approx([dit, 3 * dit, 3 * dit, dit, dit, dit, 3 * dit])
    # .- | -. || .. | -  : element, character, element, word, element, character
    assert spaces == pytest.approx([dit, 3 * tg, dit, 7 * tg, dit, 3 * tg])


def test_one_paris_takes_sixty_seconds_over_the_overall_speed():
    # PARIS: 31 units at T and 19 at T_g; 31 * 1.2/c + (60/s - 37.2/c) = 60/s (derived).
    iv = timed_intervals("PARIS PARIS", 25.0, farnsworth_wpm=13.0)
    assert iv[14][0] == pytest.approx(60.0 / 13.0)  # PARIS has 14 elements


def test_farnsworth_at_the_character_speed_is_standard_spacing():
    assert timed_intervals("CQ TEST", 20.0, farnsworth_wpm=20.0) == pytest.approx(keying_intervals("CQ TEST", 20.0))


def test_farnsworth_with_random_keying_scales_the_gap_medians():
    rng = np.random.default_rng(3)
    c, s = 20.0, 8.0
    tg = farnsworth_gap_s(c, s)
    iv = timed_intervals(" ".join(["TEST"] * 200), c, "paddle", rng, farnsworth_wpm=s)
    spaces = np.array([iv[i + 1][0] - iv[i][1] for i in range(len(iv) - 1)])
    words = spaces[spaces > 5 * tg]
    assert np.median(words) == pytest.approx(np.exp(1.94) * tg, rel=0.05)  # paddle word gap median e^1.94 units
```

Append to `training/tests/test_generate.py`:

```python
def test_farnsworth_signal_is_keyed_and_labeled_with_its_overall_speed():
    from kz4ap_synth.generate import SignalSpec, labels, plan_signal
    spec = SignalSpec("PARIS PARIS", 1000.0, 25.0, 20.0, 0.5, farnsworth_wpm=13.0)
    plan = plan_signal(spec, np.random.default_rng(0))
    assert plan.intervals[14][0] - plan.intervals[0][0] == pytest.approx(60.0 / 13.0)
    assert labels([spec], 48000, 10.0, seed=1)["signals"][0]["farnsworth_wpm"] == 13.0
    plain = labels([SignalSpec("CQ", 1000.0, 25.0, 20.0, 0.5)], 48000, 10.0, seed=1)["signals"][0]
    assert "farnsworth_wpm" not in plain  # existing labels files stay byte-identical
```

In `training/tests/test_suites.py`, change `test_full_suite_recordings_are_valid_and_uniquely_named` to expect 41 recordings and the new group:

```python
def test_full_suite_recordings_are_valid_and_uniquely_named():
    recs = SUITES["full"](1)
    names = [r.name for r in recs]
    assert len(names) == len(set(names)) == 41
    for r in recs:
        check_recording(r)
    assert {r.group for r in recs} == {
        "A sensitivity", "B fading", "C fists", "D speed", "E interference", "F tuning",
        "G ragchew", "H two-station QSO", "H two-station QSO, oracle", "I Farnsworth", "strong", "pauses",
        "tune-up", "first sample", "crowded", "band"}
```

and append:

```python
def test_farnsworth_group_uses_its_overall_speeds():
    recs = [r for r in SUITES["full"](1) if r.group == "I Farnsworth"]
    assert [r.name for r in recs] == ["I-farnsworth-machine-s1", "I-farnsworth-paddle-s1"]
    specs = [s for r in recs for s in r.specs]
    assert {(s.wpm, s.farnsworth_wpm) for s in specs} == {(18.0, 5.0), (18.0, 10.0), (25.0, 13.0), (25.0, 18.0)}
    assert {s.snr_db for s in specs} == {5.0, 10.0, 20.0}
    assert all(r.oracle for r in recs)
    assert {s.keying for s in recs[0].specs} == {"machine"} and {s.keying for s in recs[1].specs} == {"paddle"}


def test_generate_only_rewrites_matching_recordings_and_lists_every_one(tmp_path):
    a = Recording("keep", "A sensitivity", 8000, 3.0, 5, True, [SignalSpec("CQ", 1000.0, 25.0, 10.0, 0.5)])
    b = Recording("redo", "A sensitivity", 8000, 3.0, 6, True, [SignalSpec("TU", 1000.0, 25.0, 10.0, 0.5)])
    with pytest.raises(FileNotFoundError, match="keep.wav"):
        write_suite([a, b], tmp_path, "test", only="^redo$")
    write_suite([a, b], tmp_path, "test")
    kept = (tmp_path / "keep.wav").stat().st_mtime_ns
    (tmp_path / "redo.wav").unlink()
    write_suite([a, b], tmp_path, "test", only="^redo$")
    assert (tmp_path / "redo.wav").exists()
    assert (tmp_path / "keep.wav").stat().st_mtime_ns == kept
    manifest = json.loads((tmp_path / "manifest.json").read_text())
    assert [r["name"] for r in manifest["recordings"]] == ["keep", "redo"]


def test_run_only_scores_matching_recordings(tmp_path, monkeypatch):
    (tmp_path / "manifest.json").write_text(json.dumps({"suite": "t", "recordings": [
        {"name": n, "group": "g", "oracle": False, "wav": f"{n}.wav", "labels": f"{n}.json", "station_labels": None}
        for n in ("x", "y")]}))
    calls = []

    def fake_run(cmd, *a, **k):
        calls.append(cmd)
        return subprocess.CompletedProcess(cmd, 0, "CER 0.0\n", "")

    monkeypatch.setattr(subprocess, "run", fake_run)
    run_suite(tmp_path, tmp_path / "kz4ap-bench", ["baseline"], only="^y$")
    assert [c[1] for c in calls] == [str(tmp_path / "y.wav")]
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `.venv\Scripts\python -m pytest training/tests/test_keying.py training/tests/test_generate.py training/tests/test_suites.py -q`
Expected: failures: `ImportError: cannot import name 'farnsworth_gap_s'`, `TypeError: ... unexpected keyword argument 'farnsworth_wpm'`, the 39-recording assertion, and `TypeError: write_suite() got an unexpected keyword argument 'only'`.

- [ ] **Step 3: Implement Farnsworth spacing in `keying.py`**

Add after `word_wpm`:

```python
def farnsworth_gap_s(char_wpm: float, overall_wpm: float) -> float:
    """Gap timebase T_g, s, for Farnsworth spacing: characters at char_wpm, overall speed
    overall_wpm (PARIS). The ARRL standard (J. Bloom, KE3Z, "A Standard for Morse Timing Using
    the Farnsworth Technique", QEX, April 1990) spreads the added time 60/s - 37.2/c seconds per
    PARIS over its 19 gap units (four character gaps of 3, one word gap of 7): T_g = that / 19.
    At s = c it is the dit, 1.2 s / c."""
    if not 0 < overall_wpm <= char_wpm:
        raise ValueError(f"Farnsworth overall speed {overall_wpm} WPM must be positive and at most "
                         f"the character speed {char_wpm} WPM")
    return (60.0 / overall_wpm - 37.2 / char_wpm) / 19.0
```

Replace `timed_intervals` with:

```python
def timed_intervals(text: str, wpm: float, style: str = "machine", rng: np.random.Generator | None = None,
                    wpm_end: float | None = None, profile: str = "step",
                    imbalance_dits: float = 0.0, farnsworth_wpm: float | None = None) -> list[tuple[float, float]]:
    """Key-down intervals (start_s, end_s) for text, from 0 s.

    wpm_end: the speed at the end (None = constant); profile "step" switches at
    the middle word, "ramp" changes linearly from word to word. imbalance_dits:
    every mark is longer, and every space shorter, by this many dits (a
    transmitter that keys on and off at different speeds). farnsworth_wpm: the
    overall speed with Farnsworth spacing: character and word gaps are drawn on
    the timebase farnsworth_gap_s(character speed, farnsworth_wpm) instead of the
    dit (elements and element spaces stay at the character speed); None =
    standard spacing. Character and word spaces are drawn directly from their
    own distributions (VE3NEA assembles them from several draws; the medians are
    the same). With machine keying, constant speed, no imbalance and standard
    spacing the result is exactly keying_intervals().
    """
    if style not in STYLES:
        raise ValueError(f"unknown keying style {style!r}")
    if profile not in SPEED_PROFILES:
        raise ValueError(f"unknown speed profile {profile!r}")
    if farnsworth_wpm is not None:
        farnsworth_gap_s(min(wpm, wpm_end if wpm_end is not None else wpm), farnsworth_wpm)  # validates
    if style == "machine" and wpm_end is None and imbalance_dits == 0.0 and farnsworth_wpm is None:
        return keying_intervals(text, wpm)
    k = STYLES[style]
    if rng is None and any(d.sigma > 0 for d in (k.dit, k.dah, k.element_gap, k.char_gap, k.word_gap)):
        raise ValueError(f"keying style {style!r} needs a random generator")

    def length(d: Duration, unit_s: float, extra_units: float) -> float:
        units = d.median_dits if d.sigma == 0 else d.median_dits * math.exp(d.sigma * rng.standard_normal())
        return max(units + extra_units, MIN_DITS) * unit_s

    words = [[CODES[s] for s in symbols(w) if s in CODES] for w in text.upper().split()]
    words = [w for w in words if w]
    out: list[tuple[float, float]] = []
    t = 0.0
    for wi, patterns in enumerate(words):
        char_wpm = word_wpm(wi, len(words), wpm, wpm_end, profile)
        dit_s = 1.2 / char_wpm
        gap_s = dit_s if farnsworth_wpm is None else farnsworth_gap_s(char_wpm, farnsworth_wpm)
        # the same imbalance, s, in units of the gap timebase (exactly imbalance_dits with standard spacing,
        # so existing recordings draw bit-identical durations)
        gap_imbalance = imbalance_dits if farnsworth_wpm is None else imbalance_dits * dit_s / gap_s
        for ci, pattern in enumerate(patterns):
            for ei, element in enumerate(pattern):
                mark = length(k.dit if element == "." else k.dah, dit_s, imbalance_dits)
                out.append((t, t + mark))
                t += mark
                if ei < len(pattern) - 1:
                    t += length(k.element_gap, dit_s, -imbalance_dits)
            if ci < len(patterns) - 1:
                t += length(k.char_gap, gap_s, -gap_imbalance)
        if wi < len(words) - 1:
            t += length(k.word_gap, gap_s, -gap_imbalance)
    return out
```

(With standard spacing gap_s is dit_s and gap_imbalance is imbalance_dits itself, not recomputed, so every existing call draws the same numbers in the same order and computes the same durations bit for bit.)

- [ ] **Step 4: Implement it in `generate.py`**

Add as the **last** field of `SignalSpec` (after `turn_s`):

```python
    farnsworth_wpm: float | None = None  # overall speed with Farnsworth spacing, WPM (keying.farnsworth_gap_s); None = standard
```

In `sending_intervals`, pass it:

```python
def sending_intervals(spec: SignalSpec, rng: np.random.Generator) -> list[tuple[float, float]]:
    """Key-down intervals of one sending of spec.text, from 0 s."""
    return timed_intervals(spec.text, spec.wpm, spec.keying, rng, spec.wpm_end, spec.speed_profile,
                           spec.imbalance_dits, farnsworth_wpm=spec.farnsworth_wpm)
```

In `labels`, replace the loop head and the `entries.append({**asdict(s), ...` line so that the key is left out when unset:

```python
    for s, plan in zip(signals, plan_intervals(signals, seed)):
        fields = asdict(s)
        if fields["farnsworth_wpm"] is None:
            del fields["farnsworth_wpm"]  # standard spacing: the labels stay exactly as before
        if s.overs:
```

and in the `entries.append({...})` call use `**fields,` in place of `**asdict(s),`.

- [ ] **Step 5: Implement group I and `--only` in `suites.py`**

Add after `QSO_WPM_RANGE`:

```python
FARNSWORTH_ROWS = ((18.0, 5.0), (18.0, 10.0), (25.0, 13.0), (25.0, 18.0))  # (character, overall) speed, WPM
FARNSWORTH_SNR_DB = (5.0, 10.0, 20.0)
FARNSWORTH_KEYING = ("machine", "paddle")
```

Add after `two_station_qso`:

```python
def farnsworth(seed: int) -> list[Recording]:
    """Farnsworth spacing (keying.farnsworth_gap_s): character and word gaps stretched to the
    overall speed, elements at the character speed. Two stations per (speeds, S500) point, one
    recording per keying style, filler text, 180 s. T_g/T = 7.84, 3.11, 3.43, 2.02 for the rows
    (derived)."""
    recs = []
    for code, keying in enumerate(FARNSWORTH_KEYING):
        rng = np.random.default_rng([seed, 16, code])
        combos = [(c, s, snr) for c, s in FARNSWORTH_ROWS for snr in FARNSWORTH_SNR_DB for _ in range(2)]
        specs = []
        for f, (c, s, snr) in zip(_slots(len(combos), 1200.0, rng, BIN_HZ / 2), combos):
            start = _start(rng)
            # Sized at the overall speed: VE3NEA text has about PARIS's ratio of gap to element units
            # (4.31 to 7.08 per character against 19 to 31), so it runs about 60/s s per PARIS word;
            # the paddle slack (0.7) covers the rest.
            specs.append(SignalSpec(_filler(rng, s, 178.0 - start, "paddle"), f, c, snr, start, keying=keying,
                                    farnsworth_wpm=s, tag=f"farnsworth {c:g}/{s:g} wpm {keying}"))
        recs.append(Recording(f"I-farnsworth-{keying}-s{seed}", "I Farnsworth", 48000, 180.0,
                              1000 * seed + 150 + code, True, specs))
    return recs
```

In `full_suite`, add `farnsworth` after `two_station_qso` in the tuple of builders.

Replace `write_suite` with:

```python
def write_suite(recordings: list[Recording], out_dir: Path, suite_name: str, only: str | None = None) -> None:
    """Writes every recording (WAV plus labels) and manifest.json into out_dir. only: a regular
    expression; a recording whose name does not match it is not rewritten (its files must already
    be in out_dir from an earlier run of the same suite), but the manifest lists every recording."""
    out_dir.mkdir(parents=True, exist_ok=True)
    entries = []
    for rec in recordings:
        check_recording(rec)
        wav = out_dir / f"{rec.name}.wav"
        stations = out_dir / f"{rec.name}.stations.json"
        entries.append({"name": rec.name, "group": rec.group, "oracle": rec.oracle, "wav": wav.name,
                        "labels": wav.with_suffix(".json").name,
                        "station_labels": stations.name if rec.station_labels else None})
        if only is not None and not re.search(only, rec.name):
            needed = [wav, wav.with_suffix(".json")] + ([stations] if rec.station_labels else [])
            missing = [p.name for p in needed if not p.exists()]
            if missing:
                raise FileNotFoundError(f"{', '.join(missing)} missing from {out_dir}: generate without --only first")
            continue
        write_wav(wav, generate(rec.specs, rec.sample_rate, rec.duration_s, seed=rec.noise_seed), rec.sample_rate)
        extra = {"recording": rec.name, "group": rec.group, "oracle": rec.oracle}
        lab = labels(rec.specs, rec.sample_rate, rec.duration_s, seed=rec.noise_seed)
        wav.with_suffix(".json").write_text(json.dumps({**lab, **extra}, indent=2) + "\n")
        if rec.station_labels:
            per_station = station_labels(rec.specs, rec.sample_rate, rec.duration_s, seed=rec.noise_seed)
            stations.write_text(json.dumps({**per_station, **extra}, indent=2) + "\n")
        print(f"wrote {wav.name}")
    (out_dir / "manifest.json").write_text(json.dumps({"suite": suite_name, "recordings": entries}, indent=2) + "\n")
```

In `run_suite`, add the parameter `only: str | None = None` and, as the first statement inside `for rec in manifest["recordings"]:`,

```python
            if only is not None and not re.search(only, rec["name"]):
                continue
```

and update its docstring's first sentence to "Scores every recording of the manifest (or those whose names match `only`, a regular expression) with kz4ap-bench, …".

In `main`, add `g.add_argument("--only", default=None, help="regular expression: write only matching recordings (the others must exist)")` and `r.add_argument("--only", default=None, help="regular expression: score only matching recordings")`, and pass `only=args.only` in both calls.

- [ ] **Step 6: Run the tests to verify they pass**

Run: `.venv\Scripts\python -m pytest training -q`
Expected: all pass. If `test_full_suite_recordings_are_valid_and_uniquely_named` fails in `check_recording` because a group-I signal runs past 180 s, lower that recording's text budget (the `178.0 - start` argument) until it fits and note it in the commit message; do not change the speeds.

- [ ] **Step 7: Check that existing recordings are unchanged**

```powershell
.venv\Scripts\python -c "import filecmp, pathlib; from kz4ap_synth.suites import SUITES, write_suite; rec = next(r for r in SUITES['full'](1) if r.name == 'C-fists-paddle-s1'); out = pathlib.Path('build/suite/check'); write_suite([rec], out, 'check'); print(filecmp.cmp(out / 'C-fists-paddle-s1.wav', 'build/suite/full3/C-fists-paddle-s1.wav', shallow=False), filecmp.cmp(out / 'C-fists-paddle-s1.json', 'build/suite/full3/C-fists-paddle-s1.json', shallow=False))"
```
Expected: `True True` (a random-keying recording, so `timed_intervals`'s changed loop is exercised). Then, in Git Bash, `bash bench/smoke.sh build/windows` → `smoke test passed` (the smoke recording and both baselines unchanged).

- [ ] **Step 8: Add group I to the 3-seed suite and score it with both engine front ends**

```powershell
.venv\Scripts\python -m kz4ap_synth.suites generate --suite full --seeds 3 --out build/suite/full3 --only "^I-"
.venv\Scripts\python -m kz4ap_synth.suites run --out build/suite/full3 --bench build/windows/bench/Release/kz4ap-bench.exe --front-end baseline --front-end matched --only "^I-"
```
Expected: six `wrote I-farnsworth-…` lines, then twelve `CER …` lines; `build/suite/full3/manifest.json` lists 123 recordings. Record the wall-clock time of each command for the results document (Task 13).

- [ ] **Step 9: Document**

In `docs/signal-processing.md` §11, after the **Keying styles** bullet, add:

```markdown
- **Farnsworth spacing** (synthetic recordings, `farnsworth_wpm`): elements
  and element spaces at the character speed c (T = 1.2 s / c); character
  and word gaps drawn on the gap timebase T_g = (60/s − 37.2/c)/19 s instead
  of T, for an overall speed s (the ARRL standard, Bloom 1990: the added
  time per PARIS over its 19 gap units), so one PARIS takes 60/s s
  (derived). Random keying styles draw the gaps with their own σ_ln in
  units of T_g; an imbalance keeps its length in seconds. Suite group I:
  (c, s) = (18, 5), (18, 10), (25, 13), (25, 18) WPM (T_g/T = 7.84, 3.11,
  3.43, 2.02), machine and paddle keying, S₅₀₀ 5, 10, 20 dB. Labels carry
  `farnsworth_wpm` only when it is set.
```

- [ ] **Step 10: Commit**

```powershell
git add training/kz4ap_synth/keying.py training/kz4ap_synth/generate.py training/kz4ap_synth/suites.py training/tests/test_keying.py training/tests/test_generate.py training/tests/test_suites.py docs/signal-processing.md
```
```powershell
git commit -m "Add Farnsworth spacing to the generator and suite group I"
```

---

### Task 2: Engine and bench — channel tap and `--record-channels`

`kz4ap-bench --oracle --record-channels DIR` writes each oracle channel's stream as the channelizer delivered it (1500 samples/s, complex, FS), before the decoder and so before the frequency tracker (Design decisions, "Where the streams are recorded"), plus `channels.json` with each channel's label index, labeled frequency and center. The engine gains an observation-only hook; **no decoding path changes**, which the engine test checks by comparing the decoded text with and without the tap.

**Files:**
- Modify: `engine/include/kz4ap/engine.hpp`, `engine/src/engine.cpp`
- Create: `bench/src/channel_recorder.hpp`, `bench/src/channel_recorder.cpp`, `bench/tests/channel_recorder_test.cpp`
- Modify: `bench/CMakeLists.txt`, `bench/src/main.cpp`
- Test: `engine/tests/engine_test.cpp`
- Modify: `docs/signal-processing.md` (§6 "Oracle mode", §11)

**Interfaces:**
- Consumes: `Channelizer::push`'s sink (`id`, `first_index`, samples), `Channelizer::bin_to_hz`, `Channelizer::output_rate`.
- Produces (C++):
  - `struct kz4ap::ChannelBlock { std::uint32_t track_id; std::uint64_t first_index; double center_hz; std::span<const Sample> samples; };`
  - `using kz4ap::ChannelTap = std::function<void(const ChannelBlock&)>;`
  - `void Engine::set_channel_tap(ChannelTap tap);` `double Engine::channel_rate() const;` (samples/s)
  - `class kz4ap::bench::ChannelRecorder { ChannelRecorder(std::filesystem::path dir, double sample_rate_hz); void add_channel(std::uint32_t track_id, std::size_t label_index, double label_freq_hz); void write(const ChannelBlock& block); void finish(const std::string& recording, const std::string& labels); };`
  - Files: `DIR/channel-<track id>.c64` (complex64 little-endian, I and Q interleaved, numpy `<c8`, FS) and `DIR/channels.json`: `{"recording", "labels", "sample_rate_hz", "format", "channels": [{"track_id", "label_index", "label_freq_hz", "center_hz", "first_sample_index", "samples", "file"}]}`.
  - `kz4ap-bench RECORDING --labels L --oracle --record-channels DIR` (`--record-channels` needs `--oracle`).

- [ ] **Step 1: Write the failing tests**

Append to `engine/tests/engine_test.cpp` (inside the file's anonymous-namespace-free test section, after `OracleAnchorsTheTrackerAtTheLabeledFrequency`; add `#include <complex>` and `#include <numbers>` at the top if missing):

```cpp
TEST(Engine, ChannelTapSeesEachOracleChannelWithoutChangingTheDecode) {
    // Benchmark tooling (kz4ap-bench --record-channels): the tap sees every block of the channel as the
    // channelizer delivers it, before the decoder, and the decoded text is the same with or without it.
    const std::string msg = "CQ TEST K1ABC";
    const auto x = keyed_signal(msg, 25, 48000.0, kz4ap::test::duration_for(msg, 25), 12009.0, 1.0, 0.0, 42);
    auto config = matched_config(48000);
    config.oracle_frequencies_hz = {12009.0};
    const auto plain = run_with(config, x);

    EventBus bus;
    std::map<std::uint32_t, std::string> text;
    bus.subscribe([&](const Event& e) {
        if (const auto* d = std::get_if<DecodedTextEvent>(&e)) {
            for (const auto& c : d->chars) text[d->track_id] += c.text;
        }
    });
    Engine engine(config, bus);
    std::vector<Sample> stream;
    double center_hz = 0;
    std::uint64_t next_index = 0;
    bool contiguous = true;
    engine.set_channel_tap([&](const ChannelBlock& b) {
        EXPECT_EQ(b.track_id, 1u);
        contiguous = contiguous && b.first_index == next_index;
        next_index = b.first_index + b.samples.size();
        center_hz = b.center_hz;
        stream.insert(stream.end(), b.samples.begin(), b.samples.end());
    });
    for (std::size_t i = 0; i < x.size(); i += 65536) {
        engine.process(std::span<const Sample>(x).subspan(i, std::min<std::size_t>(65536, x.size() - i)));
    }
    engine.finish();

    EXPECT_EQ(text, plain.text);
    EXPECT_TRUE(contiguous);
    EXPECT_DOUBLE_EQ(engine.channel_rate(), 1500.0);
    EXPECT_DOUBLE_EQ(center_hz, 12000.0);
    EXPECT_EQ(stream.size(), engine.stats().channel_samples);
    // The station sits at label - center = 9 Hz in the stream (the channelizer is a complex shift by the
    // bin center, derived): mixed down by 9 Hz, consecutive key-down samples no longer rotate.
    double peak = 0;
    for (const auto& s : stream) peak = std::max(peak, static_cast<double>(std::abs(s)));
    std::complex<double> sum{};
    for (std::size_t n = 1; n < stream.size(); ++n) {
        if (std::abs(stream[n]) < 0.5 * peak || std::abs(stream[n - 1]) < 0.5 * peak) continue;
        const auto mix = [](std::size_t i) { return std::polar(1.0, -2.0 * std::numbers::pi * 9.0 * static_cast<double>(i) / 1500.0); };
        sum += std::complex<double>(stream[n]) * mix(n) * std::conj(std::complex<double>(stream[n - 1]) * mix(n - 1));
    }
    const double residual_hz = std::arg(sum) * 1500.0 / (2.0 * std::numbers::pi);
    EXPECT_LT(std::abs(residual_hz), 0.05);
}
```

Create `bench/tests/channel_recorder_test.cpp`:

```cpp
#include "channel_recorder.hpp"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <vector>

using namespace kz4ap;
using namespace kz4ap::bench;

namespace {

std::filesystem::path fresh_dir(const char* name) {
    const auto dir = std::filesystem::temp_directory_path() / name;
    std::filesystem::remove_all(dir);
    return dir;
}

}  // namespace

TEST(ChannelRecorder, WritesEachChannelsSamplesAndAManifest) {
    const auto dir = fresh_dir("kz4ap_recorder_writes");
    {
        ChannelRecorder rec(dir, 1500.0);
        rec.add_channel(1, 0, 1009.0);
        const std::vector<Sample> a{{1.0f, -2.0f}, {0.5f, 0.25f}};
        const std::vector<Sample> b{{3.0f, 4.0f}};
        rec.write({1, 0, 1000.0, a});
        rec.write({1, 2, 1000.0, b});
        rec.finish("x.wav", "x.json");
    }
    std::ifstream f(dir / "channel-1.c64", std::ios::binary);
    std::vector<float> v(7, -9.0f);
    f.read(reinterpret_cast<char*>(v.data()), 7 * sizeof(float));
    EXPECT_EQ(f.gcount(), static_cast<std::streamsize>(6 * sizeof(float)));  // exactly three samples
    v.resize(6);
    EXPECT_EQ(v, (std::vector<float>{1.0f, -2.0f, 0.5f, 0.25f, 3.0f, 4.0f}));
    std::ifstream m(dir / "channels.json");
    const auto j = nlohmann::json::parse(m);
    EXPECT_EQ(j.at("recording"), "x.wav");
    EXPECT_EQ(j.at("labels"), "x.json");
    EXPECT_DOUBLE_EQ(j.at("sample_rate_hz").get<double>(), 1500.0);
    const auto& c = j.at("channels").at(0);
    EXPECT_EQ(c.at("track_id"), 1);
    EXPECT_EQ(c.at("label_index"), 0);
    EXPECT_DOUBLE_EQ(c.at("label_freq_hz").get<double>(), 1009.0);
    EXPECT_DOUBLE_EQ(c.at("center_hz").get<double>(), 1000.0);
    EXPECT_EQ(c.at("first_sample_index"), 0);
    EXPECT_EQ(c.at("samples"), 3);
    EXPECT_EQ(c.at("file"), "channel-1.c64");
    std::filesystem::remove_all(dir);
}

TEST(ChannelRecorder, RejectsGapsUnknownChannelsAndMovedCenters) {
    const auto dir = fresh_dir("kz4ap_recorder_rejects");
    ChannelRecorder rec(dir, 1500.0);
    rec.add_channel(1, 0, 0.0);
    const std::vector<Sample> a(4);
    rec.write({1, 0, 0.0, a});
    EXPECT_THROW(rec.write({1, 5, 0.0, a}), std::runtime_error);   // samples 4 and 5 missing
    EXPECT_THROW(rec.write({2, 0, 0.0, a}), std::runtime_error);   // no such channel
    EXPECT_THROW(rec.write({1, 4, 23.4, a}), std::runtime_error);  // the channel's center moved
    rec.finish("x.wav", "x.json");  // closes the file, so the directory can go
    std::filesystem::remove_all(dir);
}
```

- [ ] **Step 2: Build and run the tests to verify they fail**

In `bench/CMakeLists.txt` add `src/channel_recorder.cpp` to `kz4ap_bench_lib` and `tests/channel_recorder_test.cpp` to `kz4ap_bench_tests` first (so the build reaches the tests), then run `cmake --build --preset windows`.
Expected: compile errors: `ChannelBlock` and `set_channel_tap` undeclared; `channel_recorder.hpp` not found.

- [ ] **Step 3: Implement the tap**

In `engine/include/kz4ap/engine.hpp`, add `#include <functional>` and, before `struct EngineConfig`:

```cpp
// Benchmark tooling (kz4ap-bench --record-channels): one channel block as the channelizer delivered it,
// before the channel's decoder (and so before its frequency tracker) sees it. Observation only.
struct ChannelBlock {
    std::uint32_t track_id;
    std::uint64_t first_index;        // index of samples[0] in the channelizer's output stream, from its start
    double center_hz;                 // the channel's center (its FFT bin), Hz from the span's center
    std::span<const Sample> samples;  // channelizer output at Engine::channel_rate(), FS
};
using ChannelTap = std::function<void(const ChannelBlock&)>;
```

In `class Engine`, public section, after `stats()`:

```cpp
    // Benchmark tooling: called with every channel block before the channel's decoder sees it. It
    // observes only; decoding is the same with or without it. Set it before process().
    void set_channel_tap(ChannelTap tap) { tap_ = std::move(tap); }
    double channel_rate() const;  // the channelizer's output rate, samples/s
```

and in the private members, after `double decoder_seconds_ = 0;`: `ChannelTap tap_;`. (Add `#include <utility>` if missing.)

In `engine/src/engine.cpp`, add after `Engine::stats`:

```cpp
double Engine::channel_rate() const { return channelizer_.output_rate(); }
```

and in `process_hop`'s sink lambda, right after `const double center_hz = channelizer_.bin_to_hz(channel.bin);`:

```cpp
        if (tap_) tap_(ChannelBlock{id, first_index, center_hz, s});
```

- [ ] **Step 4: Implement the recorder**

Create `bench/src/channel_recorder.hpp`:

```cpp
#pragma once

#include "kz4ap/engine.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <string>

namespace kz4ap::bench {

// Writes each oracle channel's stream (kz4ap-bench --record-channels): DIR/channel-<track id>.c64, the
// channelizer's output as complex64 (float32 I then Q, little-endian; numpy dtype "<c8"), FS, and
// DIR/channels.json with each channel's label, labeled frequency, center and sample count.
class ChannelRecorder {
public:
    // Throws std::runtime_error if dir cannot be created.
    ChannelRecorder(std::filesystem::path dir, double sample_rate_hz);

    // track_id: the oracle channel (label_index + 1); label_freq_hz: its labeled frequency, Hz from the
    // span's center. Throws std::runtime_error if the file cannot be opened.
    void add_channel(std::uint32_t track_id, std::size_t label_index, double label_freq_hz);

    // Appends a block. Throws std::runtime_error for an unknown channel, a block that does not follow
    // the last one, a center that moved, or a write error.
    void write(const ChannelBlock& block);

    // Closes the files and writes channels.json (recording and labels: file names, for the record).
    void finish(const std::string& recording, const std::string& labels);

private:
    struct Channel {
        std::size_t label_index = 0;
        double label_freq_hz = 0;
        std::optional<double> center_hz;
        std::optional<std::uint64_t> first_index;
        std::uint64_t samples = 0;
        std::string file;
        std::ofstream out;
    };

    std::filesystem::path dir_;
    double rate_;
    std::map<std::uint32_t, Channel> channels_;
};

}  // namespace kz4ap::bench
```

Create `bench/src/channel_recorder.cpp`:

```cpp
#include "channel_recorder.hpp"

#include <nlohmann/json.hpp>

#include <bit>
#include <stdexcept>
#include <system_error>
#include <utility>

namespace kz4ap::bench {

static_assert(std::endian::native == std::endian::little, "channel files are written little-endian");
static_assert(sizeof(Sample) == 2 * sizeof(float), "a sample is two float32 values, I then Q");

ChannelRecorder::ChannelRecorder(std::filesystem::path dir, double sample_rate_hz)
    : dir_(std::move(dir)), rate_(sample_rate_hz) {
    std::error_code ec;
    std::filesystem::create_directories(dir_, ec);
    if (ec) throw std::runtime_error("cannot create " + dir_.string() + ": " + ec.message());
}

void ChannelRecorder::add_channel(std::uint32_t track_id, std::size_t label_index, double label_freq_hz) {
    Channel c;
    c.label_index = label_index;
    c.label_freq_hz = label_freq_hz;
    c.file = "channel-" + std::to_string(track_id) + ".c64";
    c.out.open(dir_ / c.file, std::ios::binary | std::ios::trunc);
    if (!c.out) throw std::runtime_error("cannot open " + (dir_ / c.file).string());
    channels_.emplace(track_id, std::move(c));
}

void ChannelRecorder::write(const ChannelBlock& block) {
    const auto it = channels_.find(block.track_id);
    if (it == channels_.end()) throw std::runtime_error("block for unknown channel " + std::to_string(block.track_id));
    Channel& c = it->second;
    if (c.first_index && block.first_index != *c.first_index + c.samples) {
        throw std::runtime_error("channel " + std::to_string(block.track_id) + ": block at sample " +
                                 std::to_string(block.first_index) + " does not follow sample " +
                                 std::to_string(*c.first_index + c.samples - 1));
    }
    if (c.center_hz && *c.center_hz != block.center_hz) {
        throw std::runtime_error("channel " + std::to_string(block.track_id) + " moved its center");
    }
    if (!c.first_index) c.first_index = block.first_index;
    c.center_hz = block.center_hz;
    c.out.write(reinterpret_cast<const char*>(block.samples.data()),
                static_cast<std::streamsize>(block.samples.size() * sizeof(Sample)));
    if (!c.out) throw std::runtime_error("cannot write " + (dir_ / c.file).string());
    c.samples += block.samples.size();
}

void ChannelRecorder::finish(const std::string& recording, const std::string& labels) {
    nlohmann::json j;
    j["recording"] = recording;
    j["labels"] = labels;
    j["sample_rate_hz"] = rate_;
    j["format"] = "complex64: float32 I then Q, little-endian (numpy dtype <c8), FS";
    j["channels"] = nlohmann::json::array();
    for (auto& [id, c] : channels_) {
        c.out.close();
        if (!c.out) throw std::runtime_error("cannot close " + (dir_ / c.file).string());
        j["channels"].push_back({{"track_id", id},
                                 {"label_index", c.label_index},
                                 {"label_freq_hz", c.label_freq_hz},
                                 {"center_hz", c.center_hz ? nlohmann::json(*c.center_hz) : nlohmann::json()},
                                 {"first_sample_index", c.first_index ? *c.first_index : 0},
                                 {"samples", c.samples},
                                 {"file", c.file}});
    }
    std::ofstream f(dir_ / "channels.json");
    f << j.dump(2) << '\n';
    f.close();
    if (!f) throw std::runtime_error("cannot write " + (dir_ / "channels.json").string());
}

}  // namespace kz4ap::bench
```

In `bench/src/main.cpp`: add `#include "channel_recorder.hpp"`; add `std::optional<std::filesystem::path> record_channels;` to `Args`; add `"                   [--record-channels DIR]\n"` to `kUsage` before its last line ends (so the usage lists it); in `parse_args` add `else if (a == "--record-channels") args.record_channels = value();` and, before `return args;`, `if (args.record_channels && !args.oracle) throw std::runtime_error("--record-channels needs --oracle");`. After `Engine engine(config, bus);` add:

```cpp
        std::optional<ChannelRecorder> recorder;
        if (args.record_channels) {
            recorder.emplace(*args.record_channels, engine.channel_rate());
            for (std::size_t i = 0; i < labels->signals.size(); ++i) {
                recorder->add_channel(static_cast<std::uint32_t>(i + 1), i, labels->signals[i].freq_offset_hz);
            }
            engine.set_channel_tap([&](const ChannelBlock& b) { recorder->write(b); });
        }
```

and after `engine.finish();`:

```cpp
        if (recorder) recorder->finish(args.recording.filename().string(), args.labels->filename().string());
```

(Task 3 rewrites `main.cpp` as a whole and keeps these lines.)

- [ ] **Step 5: Build and run the tests**

Run: `cmake --build --preset windows` then `ctest --preset windows`
Expected: all pass, including `Engine.ChannelTapSeesEachOracleChannelWithoutChangingTheDecode` and both `ChannelRecorder` tests. If the residual check fails, the stream is not at f_off = label − center, which would invalidate the prototype's mixing: stop and report it (do not loosen 0.05 Hz).

- [ ] **Step 6: Record one real recording**

```powershell
build\windows\bench\Release\kz4ap-bench.exe build\suite\full3\D-speed-s1.wav --labels build\suite\full3\D-speed-s1.json --oracle --front-end envelope --no-timing --record-channels build\suite\check\D-speed-s1
.venv\Scripts\python -c "import json, numpy as np; m = json.load(open('build/suite/check/D-speed-s1/channels.json')); c = m['channels'][0]; y = np.fromfile('build/suite/check/D-speed-s1/' + c['file'], dtype='<c8'); print(len(m['channels']), m['sample_rate_hz'], len(y) == c['samples'], round(len(y) / m['sample_rate_hz'], 2))"
```
Expected: the bench's usual output lines, then `12 1500.0 True 60.0` (12 labels, 1500 samples/s, 60 s; the last value may read 60.01 because `finish()` pads the last hop).

- [ ] **Step 7: Check the Envelope path is untouched**

In Git Bash: `bash bench/smoke.sh build/windows` → `smoke test passed` (Envelope CER 0.0353 and Matched 0.0436 as before).

- [ ] **Step 8: Document**

In `docs/signal-processing.md` §6, at the end of "Oracle mode (benchmark only)", add:

```markdown
For the filter-bank prototype (milestone 2b, stage 1), `kz4ap-bench
--record-channels DIR` (with `--oracle`) copies each oracle channel's
stream to a file through `Engine::set_channel_tap`, as the channelizer
delivers it and before the decoder sees it. The tap only observes: the
decoded text is the same with or without it (tested).
```

In §11, after the **Frequency error** bullet, add:

```markdown
- **Recorded channel streams** (`kz4ap-bench --oracle --record-channels
  DIR`): for each label i, `channel-(i+1).c64` holds oracle channel i + 1's
  channelizer output (r = 1500 samples/s at 48 and 192 kHz; complex64, I
  then Q, little-endian; FS), before the frequency tracker, so the station
  sits at f_off = labeled frequency − channel center (up to ±½Δf =
  ±11.7 Hz); `channels.json` gives each channel's label index, labeled
  frequency, center (Hz from the span's center), first sample index and
  sample count. The prototype mixes the stream down by f_off (and a
  labeled drift) itself.
```

- [ ] **Step 9: Commit**

```powershell
git add engine/include/kz4ap/engine.hpp engine/src/engine.cpp engine/tests/engine_test.cpp bench/CMakeLists.txt bench/src/channel_recorder.hpp bench/src/channel_recorder.cpp bench/tests/channel_recorder_test.cpp bench/src/main.cpp docs/signal-processing.md
```
```powershell
git commit -m "Record each oracle channel's stream from the bench through an engine tap"
```

---

### Task 3: Bench — `--score-decoded` with the bench's own scoring

The prototype's text must be scored by exactly the bench's code. This task moves the score JSON and printout out of `main.cpp` into `bench/src/report.cpp`, used by both the engine path and a new `--score-decoded` path, which reads one text per label and scores it as oracle tracks. No scoring rule changes; an end-to-end check shows identical `score` objects.

**Files:**
- Create: `bench/src/report.hpp`, `bench/src/report.cpp`, `bench/tests/report_test.cpp`
- Modify: `bench/CMakeLists.txt`, `bench/src/main.cpp` (whole file below)
- Modify: `docs/signal-processing.md` (§11)

**Interfaces:**
- Consumes: `bench::score`, `Score`, `SignalScore`, `DecodedTrack`, `normalize_text`, `Labels`, `load_labels`; `ChannelRecorder` (Task 2).
- Produces (C++, `kz4ap::bench`):
  - `double detection_recall(const Score& s);`
  - `nlohmann::json score_json(const Labels& labels, const Score& s, const std::map<std::uint32_t, double>& tracked_freq_hz);` (`tracked_freq_hz` null for tracks missing from the map)
  - `void print_score(const Score& s);`
  - `struct DecodedTexts { std::string front_end; std::string recording; std::vector<std::string> texts; };` `DecodedTexts parse_decoded_texts(const std::string& json_text);` `DecodedTexts load_decoded_texts(const std::filesystem::path& path);` (throw `std::runtime_error` on bad input; other keys ignored)
  - `std::vector<DecodedTrack> tracks_for(const Labels& labels, const DecodedTexts& decoded);` (track i + 1 at label i's frequency)
- Produces (command line): `kz4ap-bench --labels L.json --score-decoded D.json [--json OUT.json] [--baseline B.json]`. Its JSON has the engine path's keys (`recording`, `duration_s`, `front_end`, `tracks`, `channel_seconds` = 0, `score`), no `timing`.

- [ ] **Step 1: Write the failing tests**

Create `bench/tests/report_test.cpp`:

```cpp
#include "report.hpp"

#include <gtest/gtest.h>

#include <stdexcept>
#include <vector>

using namespace kz4ap::bench;

namespace {

Labels two_labels() {
    Labels labels;
    labels.sample_rate = 8000;
    labels.duration_s = 3.0;
    labels.signals = {{"CQ K1ABC", 1000.0, 25, 10, 0.5, 2.5}, {"TU", 2000.0, 20, 5, 0.5, 1.5}};
    return labels;
}

}  // namespace

TEST(Report, ScoreJsonCarriesTotalsAndOneEntryPerLabel) {
    const Labels labels = two_labels();
    const std::vector<DecodedTrack> tracks{{1, 1000.0, "CQ K1ABD"}, {2, 2000.0, "TU"}};
    const Score s = score(labels.signals, tracks, 50.0, true);
    const auto j = score_json(labels, s, {{1u, 1001.5}});
    EXPECT_EQ(j.at("labels"), 2);
    EXPECT_EQ(j.at("scored"), 2);
    EXPECT_EQ(j.at("detected"), 2);
    EXPECT_DOUBLE_EQ(j.at("cer").get<double>(), s.cer);
    EXPECT_DOUBLE_EQ(j.at("detection_recall").get<double>(), 1.0);
    const auto& sig = j.at("signals");
    ASSERT_EQ(sig.size(), 2u);
    EXPECT_EQ(sig[0].at("decoded"), "CQ K1ABD");
    EXPECT_EQ(sig[0].at("edits"), 1);
    EXPECT_EQ(sig[0].at("reference"), "CQ K1ABC");
    EXPECT_DOUBLE_EQ(sig[0].at("tracked_freq_hz").get<double>(), 1001.5);
    EXPECT_TRUE(sig[1].at("tracked_freq_hz").is_null());
    EXPECT_EQ(sig[1].at("track_id"), 2);
}

TEST(Report, ParsesDecodedTextsAndBuildsOracleTracks) {
    const auto d = parse_decoded_texts(
        R"({"front_end": "bank-proto", "recording": "x.wav", "texts": ["CQ", "TU"], "channels": []})");
    EXPECT_EQ(d.front_end, "bank-proto");
    EXPECT_EQ(d.recording, "x.wav");
    ASSERT_EQ(d.texts.size(), 2u);
    Labels labels;
    labels.signals = {{"CQ", 1000.0, 25, 10, 0, 1}, {"TU", -500.0, 25, 10, 0, 1}};
    const auto t = tracks_for(labels, d);
    ASSERT_EQ(t.size(), 2u);
    EXPECT_EQ(t[1].id, 2u);
    EXPECT_DOUBLE_EQ(t[1].freq_hz, -500.0);
    EXPECT_EQ(t[1].text, "TU");
}

TEST(Report, RejectsMalformedOrMismatchedDecodedText) {
    EXPECT_THROW(parse_decoded_texts("{"), std::runtime_error);
    EXPECT_THROW(parse_decoded_texts(R"({"front_end": "x"})"), std::runtime_error);
    Labels labels;
    labels.signals = {{"CQ", 0.0, 25, 10, 0, 1}};
    EXPECT_THROW(tracks_for(labels, DecodedTexts{"x", "", {}}), std::runtime_error);
}
```

Add `src/report.cpp` to `kz4ap_bench_lib` and `tests/report_test.cpp` to `kz4ap_bench_tests` in `bench/CMakeLists.txt`.

- [ ] **Step 2: Build to verify it fails**

Run: `cmake --build --preset windows`
Expected: `report.hpp: No such file or directory`.

- [ ] **Step 3: Implement `report`**

Create `bench/src/report.hpp`:

```cpp
#pragma once

#include "labels.hpp"
#include "scoring.hpp"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace kz4ap::bench {

// Scored signals matched to a track over scored signals (1 if none is scored).
double detection_recall(const Score& s);

// kz4ap-bench's "score" object: totals, and one entry per labeled signal. tracked_freq_hz: the frequency
// of each track's latest decoded text, Hz, by track id; a signal whose track is not in it gets null
// (text decoded outside the engine carries no frequency).
nlohmann::json score_json(const Labels& labels, const Score& s, const std::map<std::uint32_t, double>& tracked_freq_hz);

// The per-label lines and the summary line kz4ap-bench prints.
void print_score(const Score& s);

// Text decoded outside the engine (kz4ap-bench --score-decoded): one string per label, in the labels
// file's order: {"front_end": NAME, "recording": NAME, "texts": [...]}. Other keys are ignored.
struct DecodedTexts {
    std::string front_end;
    std::string recording;
    std::vector<std::string> texts;
};

// Throw std::runtime_error on bad input.
DecodedTexts parse_decoded_texts(const std::string& json_text);
DecodedTexts load_decoded_texts(const std::filesystem::path& path);

// Track i + 1 at label i's frequency with text i, numbered as oracle channels are. Throws
// std::runtime_error unless there is exactly one text per label.
std::vector<DecodedTrack> tracks_for(const Labels& labels, const DecodedTexts& decoded);

}  // namespace kz4ap::bench
```

Create `bench/src/report.cpp` (the JSON and printout are `main.cpp`'s, moved unchanged apart from the tracked frequency's source):

```cpp
#include "report.hpp"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace kz4ap::bench {

double detection_recall(const Score& s) {
    return s.scored == 0 ? 1.0 : static_cast<double>(s.detected) / static_cast<double>(s.scored);
}

nlohmann::json score_json(const Labels& labels, const Score& s, const std::map<std::uint32_t, double>& tracked_freq_hz) {
    nlohmann::json signals = nlohmann::json::array();
    for (std::size_t i = 0; i < s.signals.size(); ++i) {
        const auto& sig = s.signals[i];
        nlohmann::json per_transmission = nlohmann::json::array();
        for (const auto& ts : sig.transmissions) {
            per_transmission.push_back({{"symbols", ts.symbols}, {"edits", ts.edits},
                                        {"first_word_symbols", ts.first_word_symbols},
                                        {"first_word_edits", ts.first_word_edits}});
        }
        nlohmann::json tracked;  // null
        if (sig.track_id) {
            if (const auto it = tracked_freq_hz.find(*sig.track_id); it != tracked_freq_hz.end()) tracked = it->second;
        }
        signals.push_back({{"index", i},
                           {"freq_offset_hz", sig.label.freq_offset_hz},
                           {"snr_db", sig.label.snr_db},
                           {"wpm", sig.label.wpm},
                           {"scored", sig.label.score},
                           {"reference", normalize_text(sig.label.text)},
                           {"decoded", sig.decoded},
                           {"track_id", sig.track_id ? nlohmann::json(*sig.track_id) : nlohmann::json()},
                           {"tracked_freq_hz", tracked},
                           {"cer", sig.cer},
                           {"symbols", sig.symbols},
                           {"edits", sig.edits},
                           {"chars", sig.chars},
                           {"char_edits", sig.char_edits},
                           {"spaces", sig.spaces},
                           {"space_edits", sig.space_edits},
                           {"first_word_symbols", sig.first_word_symbols},
                           {"first_word_edits", sig.first_word_edits},
                           {"nospace_symbols", sig.nospace_symbols},
                           {"nospace_edits", sig.nospace_edits},
                           {"transmissions", per_transmission}});
    }
    return {{"cer", s.cer},
            {"char_cer", s.char_cer},
            {"space_error_rate", s.space_error_rate},
            {"first_word_cer", s.first_word_cer},
            {"nospace_cer", s.nospace_cer},
            {"detected", s.detected},
            {"labels", labels.signals.size()},
            {"scored", s.scored},
            {"detection_recall", detection_recall(s)},
            {"false_tracks", s.false_tracks},
            {"signals", signals}};
}

void print_score(const Score& s) {
    for (const auto& sig : s.signals) {
        std::printf("label %+10.1f Hz  CER %5.3f  %s%s\n", sig.label.freq_offset_hz, sig.cer,
                    sig.track_id ? "" : "(not detected)", sig.label.score ? "" : " (not scored)");
    }
    std::printf("CER %.4f (characters %.4f, word spaces %.4f, first words %.4f, without spaces %.4f), "
                "detected %zu of %zu, %zu false tracks\n",
                s.cer, s.char_cer, s.space_error_rate, s.first_word_cer, s.nospace_cer, s.detected, s.scored,
                s.false_tracks);
}

DecodedTexts parse_decoded_texts(const std::string& json_text) {
    try {
        const auto j = nlohmann::json::parse(json_text);
        DecodedTexts d;
        d.front_end = j.at("front_end").get<std::string>();
        d.recording = j.value("recording", std::string());
        d.texts = j.at("texts").get<std::vector<std::string>>();
        return d;
    } catch (const nlohmann::json::exception& e) {
        throw std::runtime_error(std::string("bad decoded-text file: ") + e.what());
    }
}

DecodedTexts load_decoded_texts(const std::filesystem::path& path) {
    std::ifstream f(path);
    if (!f) throw std::runtime_error("cannot open " + path.string());
    std::ostringstream text;
    text << f.rdbuf();
    return parse_decoded_texts(text.str());
}

std::vector<DecodedTrack> tracks_for(const Labels& labels, const DecodedTexts& decoded) {
    if (decoded.texts.size() != labels.signals.size()) {
        throw std::runtime_error("decoded text for " + std::to_string(decoded.texts.size()) + " signals, labels for " +
                                 std::to_string(labels.signals.size()));
    }
    std::vector<DecodedTrack> tracks;
    for (std::size_t i = 0; i < labels.signals.size(); ++i) {
        const double f = labels.signals[i].freq_offset_hz;
        tracks.push_back({static_cast<std::uint32_t>(i + 1), f, decoded.texts[i], f});
    }
    return tracks;
}

}  // namespace kz4ap::bench
```

Replace `bench/src/main.cpp` with:

```cpp
// kz4ap-bench: runs the engine over an I/Q recording and scores the result, or scores text decoded
// elsewhere (--score-decoded) against a labels file with the same scoring.

#include "channel_recorder.hpp"
#include "cpu_time.hpp"
#include "labels.hpp"
#include "report.hpp"
#include "scoring.hpp"

#include "kz4ap/engine.hpp"
#include "kz4ap/wav_reader.hpp"

#include <nlohmann/json.hpp>

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

using namespace kz4ap;
using namespace kz4ap::bench;

namespace {

struct Args {
    std::optional<std::filesystem::path> recording;
    std::optional<std::filesystem::path> labels;
    std::optional<std::filesystem::path> json;
    std::optional<std::filesystem::path> baseline;
    std::optional<std::filesystem::path> record_channels;
    std::optional<std::filesystem::path> score_decoded;
    bool timing = true;
    bool oracle = false;
    FrontEnd front_end = FrontEnd::Matched;
};

constexpr const char* kUsage =
    "usage: kz4ap-bench RECORDING.wav [--labels LABELS.json] [--json OUT.json]\n"
    "                   [--no-timing] [--baseline BASELINE.json] [--oracle] [--front-end envelope|matched]\n"
    "                   [--record-channels DIR]\n"
    "       kz4ap-bench --labels LABELS.json --score-decoded DECODED.json [--json OUT.json] [--baseline BASELINE.json]\n";

Args parse_args(int argc, char** argv) {
    Args args;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto value = [&]() -> std::filesystem::path {
            if (i + 1 >= argc) throw std::runtime_error(a + " needs a value\n" + kUsage);
            return argv[++i];
        };
        if (a == "--labels") args.labels = value();
        else if (a == "--json") args.json = value();
        else if (a == "--baseline") args.baseline = value();
        else if (a == "--no-timing") args.timing = false;
        else if (a == "--oracle") args.oracle = true;
        else if (a == "--record-channels") args.record_channels = value();
        else if (a == "--score-decoded") args.score_decoded = value();
        else if (a == "--front-end") {
            const auto v = value().string();
            if (v == "envelope" || v == "baseline") args.front_end = FrontEnd::Envelope;
            else if (v == "matched") args.front_end = FrontEnd::Matched;
            else throw std::runtime_error("--front-end must be envelope or matched\n" + std::string(kUsage));
        }
        else if (!a.empty() && a[0] == '-') throw std::runtime_error("unknown option " + a + "\n" + kUsage);
        else if (!args.recording) args.recording = a;
        else throw std::runtime_error(std::string("more than one recording given\n") + kUsage);
    }
    if (args.baseline && !args.labels) throw std::runtime_error("--baseline needs --labels");
    if (args.score_decoded) {
        if (args.recording) throw std::runtime_error("--score-decoded takes no recording\n" + std::string(kUsage));
        if (!args.labels) throw std::runtime_error("--score-decoded needs --labels");
        if (args.oracle || args.record_channels) {
            throw std::runtime_error("--score-decoded cannot be combined with --oracle or --record-channels");
        }
        return args;
    }
    if (!args.recording) throw std::runtime_error(kUsage);
    if (args.oracle && !args.labels) throw std::runtime_error("--oracle needs --labels");
    if (args.record_channels && !args.oracle) throw std::runtime_error("--record-channels needs --oracle");
    return args;
}

// Scores, prints, fills out["score"] and checks the baseline; returns the exit code.
int score_and_report(const Labels& labels, const std::vector<DecodedTrack>& tracks,
                     const std::map<std::uint32_t, double>& tracked_freq_hz, bool match_by_order,
                     const std::optional<std::filesystem::path>& baseline, nlohmann::json& out) {
    const Score s = score(labels.signals, tracks, 50.0, match_by_order);
    out["score"] = score_json(labels, s, tracked_freq_hz);
    print_score(s);
    if (!baseline) return 0;
    std::ifstream f(*baseline);
    if (!f) throw std::runtime_error("cannot open " + baseline->string());
    const auto b = nlohmann::json::parse(f);
    const double max_cer = b.at("max_cer").get<double>();
    const double min_recall = b.at("min_detection_recall").get<double>();
    const double recall = detection_recall(s);
    if (s.cer > max_cer || recall < min_recall) {
        std::fprintf(stderr, "FAIL: CER %.4f (max %.4f), detection recall %.3f (min %.3f)\n", s.cer, max_cer, recall,
                     min_recall);
        return 1;
    }
    return 0;
}

void write_json(const std::optional<std::filesystem::path>& path, const nlohmann::json& out) {
    if (!path) return;
    std::ofstream f(*path);
    f << out.dump(2) << '\n';
    f.close();
    if (!f) throw std::runtime_error("cannot write " + path->string());
}

void add_tracks(nlohmann::json& out, const std::vector<DecodedTrack>& tracks) {
    out["tracks"] = nlohmann::json::array();
    for (const auto& t : tracks) {
        out["tracks"].push_back({{"id", t.id}, {"freq_hz", t.freq_hz}, {"text", normalize_text(t.text)}});
        std::printf("track %4u  %+10.1f Hz  %s\n", t.id, t.freq_hz, normalize_text(t.text).c_str());
    }
}

int run_engine(const Args& args) {
    WavIqReader reader(*args.recording);
    std::optional<Labels> labels;
    if (args.labels) {
        labels = load_labels(*args.labels);
        if (labels->sample_rate != reader.sample_rate())
            throw std::runtime_error("labels sample rate does not match the recording");
    }

    EventBus bus;
    std::map<std::uint32_t, DecodedTrack> tracks;
    bus.subscribe([&](const Event& e) {
        if (const auto* t = std::get_if<TrackEvent>(&e); t && t->kind == TrackEvent::Kind::Born) {
            tracks[t->track.id] = {t->track.id, t->track.freq_hz, "", t->track.freq_hz};
        } else if (const auto* d = std::get_if<DecodedTextEvent>(&e)) {
            for (const auto& c : d->chars) tracks[d->track_id].text += c.text;
            tracks[d->track_id].last_freq_hz = d->freq_hz;
        }
    });

    EngineConfig config;
    config.sample_rate = reader.sample_rate();
    if (args.front_end == FrontEnd::Envelope) config = with_envelope_path(config);
    if (args.oracle) {
        for (const auto& s : labels->signals) config.oracle_frequencies_hz.push_back(s.freq_offset_hz);
    }
    Engine engine(config, bus);
    std::optional<ChannelRecorder> recorder;
    if (args.record_channels) {
        recorder.emplace(*args.record_channels, engine.channel_rate());
        for (std::size_t i = 0; i < labels->signals.size(); ++i) {
            recorder->add_channel(static_cast<std::uint32_t>(i + 1), i, labels->signals[i].freq_offset_hz);
        }
        engine.set_channel_tap([&](const ChannelBlock& b) { recorder->write(b); });
    }
    std::vector<Sample> block(65536);
    const double cpu_started = process_cpu_seconds();
    const auto started = std::chrono::steady_clock::now();
    while (const auto n = reader.read(block)) engine.process(std::span<const Sample>(block).first(n));
    engine.finish();
    if (recorder) recorder->finish(args.recording->filename().string(), args.labels->filename().string());
    const double wall_s = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    const double cpu_s = process_cpu_seconds() - cpu_started;
    const EngineStats stats = engine.stats();
    const double duration_s = static_cast<double>(reader.total_samples()) / reader.sample_rate();

    nlohmann::json out;
    out["recording"] = args.recording->filename().string();
    out["duration_s"] = duration_s;
    out["front_end"] = args.front_end == FrontEnd::Matched ? "matched" : "envelope";
    std::vector<DecodedTrack> track_list;
    std::map<std::uint32_t, double> tracked_freq_hz;
    for (const auto& [id, t] : tracks) {
        track_list.push_back(t);
        tracked_freq_hz[id] = t.last_freq_hz;
    }
    add_tracks(out, track_list);
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
                    duration_s, wall_s, duration_s / wall_s, per_channel_ms(cpu_s), per_channel_ms(stats.decoder_seconds));
    }
    int exit_code = 0;
    if (labels) exit_code = score_and_report(*labels, track_list, tracked_freq_hz, args.oracle, args.baseline, out);
    write_json(args.json, out);
    return exit_code;
}

int score_decoded(const Args& args) {
    const Labels labels = load_labels(*args.labels);
    const DecodedTexts decoded = load_decoded_texts(*args.score_decoded);
    const auto tracks = tracks_for(labels, decoded);
    nlohmann::json out;
    out["recording"] = decoded.recording;
    out["duration_s"] = labels.duration_s;
    out["front_end"] = decoded.front_end;
    add_tracks(out, tracks);
    out["channel_seconds"] = 0.0;  // no engine ran
    const int exit_code = score_and_report(labels, tracks, {}, true, args.baseline, out);
    write_json(args.json, out);
    return exit_code;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const Args args = parse_args(argc, argv);
        return args.score_decoded ? score_decoded(args) : run_engine(args);
    } catch (const std::exception& e) {
        std::cerr << "kz4ap-bench: " << e.what() << '\n';
        return 2;
    }
}
```

(The engine path's printout changes order in one place: track lines are printed before the timing line, as before; label lines are printed by `print_score`, as before. The JSON is the same object: nlohmann orders keys alphabetically.)

- [ ] **Step 4: Build and run the tests**

Run: `cmake --build --preset windows` then `ctest --preset windows`
Expected: all pass, including the three `Report` tests.

- [ ] **Step 5: Check the two paths score identically**

In Git Bash (after `bash bench/smoke.sh build/windows`, which must still print `smoke test passed`):

```bash
build/windows/bench/Release/kz4ap-bench.exe build/windows/smoke/band.wav --labels build/windows/smoke/band.json --oracle --front-end matched --no-timing --json build/windows/smoke/oracle.json
PYTHONPATH=training .venv/Scripts/python -c "import json; j = json.load(open('build/windows/smoke/oracle.json')); json.dump({'front_end': 'matched-copy', 'recording': j['recording'], 'texts': [s['decoded'] for s in j['score']['signals']]}, open('build/windows/smoke/decoded.json', 'w'))"
build/windows/bench/Release/kz4ap-bench.exe --labels build/windows/smoke/band.json --score-decoded build/windows/smoke/decoded.json --json build/windows/smoke/rescored.json
PYTHONPATH=training .venv/Scripts/python -c "import json; a = json.load(open('build/windows/smoke/oracle.json'))['score']; b = json.load(open('build/windows/smoke/rescored.json'))['score']; [s.pop('tracked_freq_hz') for s in a['signals'] + b['signals']]; assert a == b, 'scores differ'; print('identical')"
```
Expected: `identical`. (The decoded field is the normalized text; scoring normalizes again, which changes nothing.)

- [ ] **Step 6: Document**

In `docs/signal-processing.md` §11, after the **Recorded channel streams** bullet, add:

```markdown
- **Externally decoded text** (`kz4ap-bench --labels L --score-decoded
  D.json`): one text per label, in the labels file's order, scored as
  oracle tracks 1…n at the labels' frequencies by the same scoring and JSON
  as an engine run (`bench/src/report.cpp`); `tracked_freq_hz` is null and
  `channel_seconds` 0. Checked identical to an engine oracle run's own
  texts on the smoke recording (apart from `tracked_freq_hz`).
```

- [ ] **Step 7: Commit**

```powershell
git add bench/CMakeLists.txt bench/src/report.hpp bench/src/report.cpp bench/tests/report_test.cpp bench/src/main.cpp docs/signal-processing.md
```
```powershell
git commit -m "Score externally decoded text with the bench's own scoring"
```

---
### Task 4: Prototype foundations — parameters, streams and oracle mix, the bank, the LLR

The package `training/kz4ap_proto/` starts here: every parameter of the prototype in one frozen dataclass (physical units, with its status), reading a recorded channel and mixing it to 0 Hz at its labeled frequency, the ladder of branch lengths, the causal boxcar and its power response, and ln I₀ and the LLR ported from `engine/src/matched_front_end.cpp`.

**Files:**
- Create: `training/kz4ap_proto/__init__.py`, `params.py`, `streams.py`, `bank.py`, `detect.py`
- Test: `training/tests/test_proto_bank.py`, `test_proto_streams.py`, `test_proto_detect.py`

**Interfaces:**
- Consumes: the recorder's files (Task 2).
- Produces (`kz4ap_proto`):
  - `params.ProtoConfig` (frozen dataclass; fields below) and `ProtoConfig.with_values(**changes) -> ProtoConfig` (lists become tuples; unknown names raise `ValueError`).
  - `streams.ChannelStream` (`label_index: int`, `rate_hz: float`, `y: np.ndarray`, `first_sample_index: int`, `f_off_hz: float`, `drift_hz_per_s: float`, `start_s: float`, `label: dict`; method `baseband() -> np.ndarray`); `streams.baseband(y, rate_hz, f_off_hz, drift_hz_per_s=0.0, start_s=0.0, first_sample_index=0) -> np.ndarray`; `streams.read_manifest(record_dir) -> dict`; `streams.load_channel(record_dir, labels_path, position: int) -> ChannelStream`.
  - `bank.branch_lengths_s(cfg) -> np.ndarray` (s), `bank.branch_samples(lengths_s, rate_hz) -> np.ndarray` (int), `bank.realized_lengths_s(cfg, rate_hz) -> np.ndarray` (N_k/r, s), `bank.boxcar(u, n: int) -> np.ndarray`, `bank.power_response(f_hz, n: int, rate_hz) -> np.ndarray`.
  - `detect.log_bessel_i0(z)`, `detect.envelope_llr(x, a)`, `detect.logistic(g)` (numpy, broadcasting).

- [ ] **Step 1: Write the failing tests**

Create `training/tests/test_proto_bank.py`:

```python
import numpy as np
import pytest

from kz4ap_proto.bank import boxcar, branch_lengths_s, branch_samples, power_response, realized_lengths_s
from kz4ap_proto.params import ProtoConfig


def test_ladder_spans_100_to_5_wpm_in_32_steps_of_ten_percent():
    lengths = branch_lengths_s(ProtoConfig())
    assert len(lengths) == 32
    assert lengths[0] == pytest.approx(0.0096)                   # 0.8 x the 100 WPM dit
    assert lengths[-1] == pytest.approx(0.0096 * 1.1 ** 31)      # 184.3 ms
    assert np.allclose(lengths[1:] / lengths[:-1], 1.1)
    # the 5 WPM optimum, 0.8 x 240 ms, is within one step: 0.18 dB of signal power relative to the ideal length (spec 4.1, derived)
    assert 10 * np.log10(0.8 * 0.24 / lengths[-1]) == pytest.approx(0.18, abs=0.005)


def test_branch_lengths_in_samples_are_distinct_at_the_channel_rate():
    n = branch_samples(branch_lengths_s(ProtoConfig()), 1500.0)
    assert n.tolist() == [14, 16, 17, 19, 21, 23, 26, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107,
                          117, 129, 142, 156, 172, 189, 208, 228, 251, 276]
    assert realized_lengths_s(ProtoConfig(), 1500.0)[0] == pytest.approx(14 / 1500)


def test_boxcar_is_the_causal_running_mean():
    assert boxcar(np.arange(1, 7, dtype=complex), 3).real.tolist() == pytest.approx([1 / 3, 1, 2, 3, 4, 5])


def test_boxcar_passes_a_carrier_at_unity_gain_and_white_noise_at_one_over_n():
    rng = np.random.default_rng(1)
    u = (rng.standard_normal(200000) + 1j * rng.standard_normal(200000)) / np.sqrt(2)  # 1 FS² per sample
    v = boxcar(u, 60)[60:]
    assert np.mean(np.abs(v) ** 2) == pytest.approx(1 / 60, rel=0.05)
    assert boxcar(np.ones(100, complex), 60)[-1].real == pytest.approx(1.0)


def test_power_response_integrates_to_one_over_n():
    f = np.linspace(-750.0, 750.0, 150001)[:-1]
    assert np.mean(power_response(f, 14, 1500.0)) == pytest.approx(1 / 14, rel=1e-3)  # Parseval
    assert power_response(np.array([0.0]), 14, 1500.0)[0] == 1.0
    assert power_response(np.array([1500.0 / 14]), 14, 1500.0)[0] == pytest.approx(0.0, abs=1e-20)  # first null
```

Create `training/tests/test_proto_streams.py`:

```python
import json

import numpy as np
import pytest

from kz4ap_proto.streams import baseband, load_channel


def test_baseband_puts_the_labeled_carrier_at_zero_hz_including_drift():
    rate, f_off, drift, t0 = 1500.0, 9.0, 0.5, 2.0
    t = np.arange(30000) / rate
    y = np.exp(1j * (2 * np.pi * f_off * t + np.pi * drift * np.maximum(t - t0, 0.0) ** 2 + 0.3))
    assert np.allclose(baseband(y, rate, f_off, drift, t0), np.exp(0.3j))


def test_load_channel_reads_the_stream_its_offset_and_its_label(tmp_path):
    y = (np.arange(10) + 1j).astype("<c8")
    y.tofile(tmp_path / "channel-1.c64")
    (tmp_path / "channels.json").write_text(json.dumps({
        "recording": "x.wav", "labels": "x.json", "sample_rate_hz": 1500.0, "format": "complex64",
        "channels": [{"track_id": 1, "label_index": 0, "label_freq_hz": 1009.0, "center_hz": 1000.0,
                      "first_sample_index": 0, "samples": 10, "file": "channel-1.c64"}]}))
    (tmp_path / "x.json").write_text(json.dumps({"signals": [{"text": "E", "start_s": 0.5, "drift_hz_per_s": 0.0}]}))
    ch = load_channel(tmp_path, tmp_path / "x.json", 0)
    assert (ch.label_index, ch.rate_hz, ch.f_off_hz, ch.start_s) == (0, 1500.0, 9.0, 0.5)
    assert np.allclose(ch.y, y)
```

Create `training/tests/test_proto_detect.py`:

```python
import numpy as np
import pytest

from kz4ap_proto.detect import envelope_llr, log_bessel_i0, logistic


def test_log_bessel_i0_matches_numpy():
    z = np.linspace(0.0, 50.0, 501)
    assert np.max(np.abs(log_bessel_i0(z) - np.log(np.i0(z)))) < 1e-6  # A&S: relative error in I0 below 5e-7


def test_llr_is_zero_without_amplitude_and_grows_with_the_envelope():
    assert envelope_llr(np.array([0.5, 3.0]), 0.0).tolist() == pytest.approx([0.0, 0.0])
    llr = envelope_llr(np.array([[0.5, 3.0, 6.0]]), np.array([[4.0]]))
    assert llr[0, 0] < 0 < llr[0, 2] and llr[0, 1] < llr[0, 2]
    assert logistic(np.array([0.0, 100.0]))[1] == pytest.approx(1.0) and logistic(np.array([0.0]))[0] == 0.5
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `.venv\Scripts\python -m pytest training/tests/test_proto_bank.py training/tests/test_proto_streams.py training/tests/test_proto_detect.py -q`
Expected: collection errors, `ModuleNotFoundError: No module named 'kz4ap_proto'`.

- [ ] **Step 3: Implement**

Create `training/kz4ap_proto/__init__.py`:

```python
"""Filter-bank prototype (milestone 2b, stage 1): the estimators of
docs/design/2026-09-30-filter-bank-speed-estimator-design.md in numpy, run on recorded oracle
channel streams (kz4ap-bench --record-channels) and scored by kz4ap-bench --score-decoded."""
```

Create `training/kz4ap_proto/params.py`:

```python
"""The prototype's parameters, in physical units (Hz, s, FS; counts of marks, spaces, characters or
selection instants). Samples and bins appear only at the point of use. Status of each value: the plan's
"Parameters" table (docs/plans/2026-09-30-milestone-2b-stage-1-filter-bank-prototype.md) and spec §6."""

from __future__ import annotations

import math
from dataclasses import dataclass, fields, replace


@dataclass(frozen=True)
class ProtoConfig:
    # Filter bank (spec 4.1)
    min_wpm: float = 5.0                   # owner
    max_wpm: float = 100.0                 # owner
    ladder_step: float = 1.1               # owner
    length_dits: float = 0.8               # heuristic
    block_s: float = 32 / 1500             # estimates advance once per block (the engine's channel block), s; heuristic
    # Noise (spec 4.2)
    noise_method: str = "spectrum"         # "spectrum": three-tap level x spectrum ratios; "spectrum-level": spectrum
                                           # level / mask_bias; "branch": the fallback; open, decided by E10 (owner)
    mask_bias: float = 0.979               # masked periodogram power / true noise power, white noise; measured
                                           # (review; Task 5 Step 5 replaces it with this code's own value)
                                           # [corrected 2026-10-02: Task 5 made it a per-branch table,
                                           # tuple of 32 values, b_mask,k; see training/kz4ap_proto/params.py]
    noise_tau_s: float = 2.0               # tau_n, s of noise updates (milestone 2)
    noise_warmup_s: float = 0.32           # first estimate: 20% quantile of |v|^2 over this, s (milestone 2)
    noise_guard: float = 1.75              # kappa (milestone 2)
    neighbor_guard: float = 4.0            # kappa_n (milestone 2); also the spectrum's mark flag
    segment_s: float = 256 / 1500          # T_seg, s (bins 5.86 Hz wide); heuristic
    spectrum_smoothing_hz: float = 25.0    # the shape is averaged over +/- this, Hz; heuristic
    guard_margin_s: float = 0.02           # the spectrum's mark flag reaches this far, s; heuristic
    min_clean_fraction: float = 0.5        # a segment enters the spectrum only if this much of it is unflagged; heuristic
    # Keying (spec 4.3, 4.7)
    amplitude_tau_s: float = 0.5           # tau_a, s of key-down weight (milestone 2)
    prior_key_down: float = 0.44           # P1 (PARIS)
    hysteresis_nats: float = 1.0           # h (kept; heuristic)
    squelch_a: float = 3.0                 # a_min at squelch_ref_s (milestone 2; heuristic)
    squelch_ref_s: float = 0.016           # s
    squelch_exponent: float = 0.25         # a_min proportional to L^(1/4) (derived scaling)
    false_marks_per_s: float = 0.01        # R_fa target, per branch, noise alone; heuristic (E9)
    x_on_values: tuple[float, ...] = ()    # x_on per branch, calibrated to R_fa by measurement (E9); () = the
                                           # nominal sqrt(-2 ln(R_fa L_k)), heuristic (keys 7-10x more than R_fa)
    release_probability: float = 0.3       # key up where noise alone exceeds x this often (x_off = 1.55); heuristic
    rekey_after_s: float = 0.4             # W_min, s of keyed time while the amplitude is unknown; placeholder (E9)
    # Duration fit (spec 4.5)
    fit_memory: float = 24.0               # N_mem, marks and spaces; placeholder (E4)
    t_grid_step: float = 0.01              # relative step of the T grid; placeholder (E5)
    q_grid: tuple[float, ...] = (3.0, 3.5, 4.0, 4.5, 5.0)                                  # placeholder (E5)
    w_grid: tuple[float, ...] = (-0.4, -0.2, 0.0, 0.2, 0.4, 0.6, 0.8, 1.0)                 # w/T; placeholder (E5)
    tg_grid: tuple[float, ...] = (1.0, 1.26, 1.59, 2.0, 2.52, 3.17, 4.0, 5.04, 6.35, 8.0)  # T_g/T; placeholder (E5)
    sigma_ln_mark: float = 0.15            # heuristic
    sigma_ln_space: float = 0.25           # heuristic
    outlier_prior: float = 0.05            # epsilon; heuristic
    outlier_range_s: tuple[float, float] = (0.001, 10.0)   # log-uniform outlier class, s; heuristic
    prior_sigma_ln: float = 0.1            # width of the T_P prior in ln T; heuristic
    refine_iterations: int = 2             # weighted-least-squares steps after the grid; heuristic
    min_fit_weight: float = 8.0            # elements of memory weight before a fit counts for eligibility; heuristic
    # Periodicity (spec 4.4)
    periodicity_method: str = "comb"       # "comb" (on Pi = 2T; default, owner), "edge" or "spectrum"; E1
    periodicity_windows_s: tuple[float, ...] = (2.0, 5.0, 10.0)   # placeholder (E2)
    periodicity_update_s: float = 0.25     # heuristic
    periodicity_rate_hz: float = 750.0     # p is averaged down to this rate, samples/s; heuristic
    comb_teeth: int = 4                    # teeth at k Pi (comb) or k T (edge comb); placeholder (E3)
    comb_width: float = 0.075              # comb tooth half-width, fraction of Pi (0.075 Pi = 15% of T; the edge comb
                                           # uses 2 x this, fraction of T); placeholder (E3)
    spectrum_nulls: int = 3                # placeholder (E3)
    spectrum_null_width: float = 0.15      # null band width x T (+/- half of it around k/T); placeholder (E3)
    comb_confidence_min: float = 0.03      # placeholder (E1)
    edge_confidence_min: float = 0.03      # placeholder (E1)
    spectrum_confidence_min: float = 1.5   # nats; placeholder (E1)
    # Selection (spec 4.6)
    eligibility_tolerance: float = math.log(1.1)   # heuristic (one ladder step)
    switch_persistence: int = 4            # M, selection instants in a row; placeholder (E6)
    quality_tie_nats: float = 0.05         # epsilon_Q, nats per element; placeholder (E8)
    text_tie_nats: float = 0.1             # nats per character; heuristic
    text_window_chars: int = 10            # placeholder (E8)
    text_separation_nats: float = 1.0      # "clearly separates" with none eligible, nats per character; heuristic
    # Silences and output (spec 4.7, 4.8)
    new_over_min_s: float = 0.5            # placeholder (E7)
    new_over_gaps: float = 12.0            # x T_g; placeholder (E7)
    correction_reach_s: float = 20.0       # owner

    def with_values(self, **changes) -> "ProtoConfig":
        """A copy with some values changed; lists become tuples (JSON has no tuples)."""
        names = {f.name for f in fields(self)}
        unknown = sorted(set(changes) - names)
        if unknown:
            raise ValueError(f"unknown parameters {unknown}")
        return replace(self, **{k: tuple(v) if isinstance(v, list) else v for k, v in changes.items()})
```

Create `training/kz4ap_proto/streams.py`:

```python
"""Recorded oracle channel streams (kz4ap-bench --record-channels) and the oracle re-centering."""

from __future__ import annotations

import json
from dataclasses import dataclass
from pathlib import Path

import numpy as np


def baseband(y, rate_hz: float, f_off_hz: float, drift_hz_per_s: float = 0.0, start_s: float = 0.0,
             first_sample_index: int = 0) -> np.ndarray:
    """u[n]: y mixed down so the labeled carrier sits at 0 Hz, y * exp(-j(2 pi f_off t + pi fdot (t - t0)^2))
    with the drift from t0 on (the generator's phase law, docs/signal-processing.md section 11 "Drift"),
    t = (first index + n) / r."""
    t = (first_sample_index + np.arange(len(y))) / rate_hz
    phase = 2.0 * np.pi * f_off_hz * t
    if drift_hz_per_s:
        phase = phase + np.pi * drift_hz_per_s * np.maximum(t - start_s, 0.0) ** 2
    return np.asarray(y, np.complex128) * np.exp(-1j * phase)


@dataclass
class ChannelStream:
    label_index: int
    rate_hz: float            # r, samples/s
    y: np.ndarray             # channelizer output, FS (complex128)
    first_sample_index: int
    f_off_hz: float           # labeled carrier minus the channel's center, Hz
    drift_hz_per_s: float     # the label's drift, Hz/s
    start_s: float            # the label's start, the drift's reference, s
    label: dict

    def baseband(self) -> np.ndarray:
        return baseband(self.y, self.rate_hz, self.f_off_hz, self.drift_hz_per_s, self.start_s,
                        self.first_sample_index)


def read_manifest(record_dir) -> dict:
    return json.loads((Path(record_dir) / "channels.json").read_text())


def load_channel(record_dir, labels_path, position: int) -> ChannelStream:
    """The position-th channel of a recording directory, with its label from labels_path."""
    record_dir = Path(record_dir)
    manifest = read_manifest(record_dir)
    ch = manifest["channels"][position]
    label = json.loads(Path(labels_path).read_text())["signals"][ch["label_index"]]
    y = np.fromfile(record_dir / ch["file"], dtype="<c8").astype(np.complex128)
    if len(y) != ch["samples"]:
        raise ValueError(f"{ch['file']}: {len(y)} samples, the manifest says {ch['samples']}")
    return ChannelStream(int(ch["label_index"]), float(manifest["sample_rate_hz"]), y, int(ch["first_sample_index"]),
                         float(ch["label_freq_hz"]) - float(ch["center_hz"]),
                         float(label.get("drift_hz_per_s") or 0.0), float(label["start_s"]), label)
```

Create `training/kz4ap_proto/bank.py`:

```python
"""The filter bank (spec 4.1): fixed boxcars on a geometric ladder of lengths."""

from __future__ import annotations

import numpy as np


def branch_lengths_s(cfg) -> np.ndarray:
    """L_k = length_dits x 1.2 s / max_wpm x step^(k-1), up to the first within one step of the
    min_wpm optimum: 9.6 ms ... 184.3 ms, 32 branches with the defaults (owner)."""
    l_min = cfg.length_dits * 1.2 / cfg.max_wpm
    l_max = cfg.length_dits * 1.2 / cfg.min_wpm
    count = int(np.ceil(np.log(l_max / l_min) / np.log(cfg.ladder_step) - 1e-9))
    return l_min * cfg.ladder_step ** np.arange(count)


def branch_samples(lengths_s, rate_hz: float) -> np.ndarray:
    """N_k = round(L_k r), at least 1: the point of use."""
    return np.maximum(1, np.rint(np.asarray(lengths_s) * rate_hz)).astype(int)


def realized_lengths_s(cfg, rate_hz: float) -> np.ndarray:
    """N_k / r, s: the lengths the branches really have."""
    return branch_samples(branch_lengths_s(cfg), rate_hz) / rate_hz


def boxcar(u, n: int) -> np.ndarray:
    """v[m] = (1/n) sum of u[m-n+1 .. m], zeros before the stream: the engine's causal, unity-gain boxcar."""
    c = np.concatenate(([0], np.cumsum(np.asarray(u, np.complex128))))
    m = np.arange(1, len(u) + 1)
    return (c[m] - c[np.maximum(m - n, 0)]) / n


def power_response(f_hz, n: int, rate_hz: float) -> np.ndarray:
    """|H(f)|^2 of an n-sample boxcar at rate r: (sin(pi f n / r) / (n sin(pi f / r)))^2, 1 at 0 Hz."""
    x = np.pi * np.asarray(f_hz, float) / rate_hz
    den = n * np.sin(x)
    out = np.ones_like(x)
    nz = np.abs(den) > 1e-12
    out[nz] = (np.sin(n * x[nz]) / den[nz]) ** 2
    return out
```

Create `training/kz4ap_proto/detect.py`:

```python
"""ln I0 and the envelope log-likelihood ratio, as engine/src/matched_front_end.cpp computes them."""

from __future__ import annotations

import numpy as np


def log_bessel_i0(z) -> np.ndarray:
    """ln I0(z) for z >= 0 without overflow (Abramowitz & Stegun 9.8.1 for z < 3.75, 9.8.2 above)."""
    z = np.abs(np.asarray(z, dtype=float))
    out = np.empty_like(z)
    small = z < 3.75
    t = (z[small] / 3.75) ** 2
    out[small] = np.log(1.0 + t * (3.5156229 + t * (3.0899424 + t * (1.2067492 + t * (0.2659732 + t * (
        0.0360768 + t * 0.0045813))))))
    zl = z[~small]
    s = 3.75 / zl
    poly = 0.39894228 + s * (0.01328592 + s * (0.00225319 + s * (-0.00157565 + s * (0.00916281 + s * (
        -0.02057706 + s * (0.02635537 + s * (-0.01647633 + s * 0.00392377)))))))
    out[~small] = zl - 0.5 * np.log(zl) + np.log(poly)
    return out


def envelope_llr(x, a) -> np.ndarray:
    """Lambda = -a^2/2 + ln I0(a x), nats: key-down (Rician) over key-up (Rayleigh) for x = |v|/sigma_v
    and a = s/sigma_v (Proakis & Salehi eq. 4.5-21)."""
    a = np.asarray(a, float)
    return -0.5 * a * a + log_bessel_i0(a * np.asarray(x, float))


def logistic(g) -> np.ndarray:
    return 1.0 / (1.0 + np.exp(-np.clip(np.asarray(g, float), -50.0, 50.0)))
```

- [ ] **Step 4: Run the tests to verify they pass**

Run: `.venv\Scripts\python -m pytest training -q`
Expected: all pass.

- [ ] **Step 5: Commit**

```powershell
git add training/kz4ap_proto/__init__.py training/kz4ap_proto/params.py training/kz4ap_proto/streams.py training/kz4ap_proto/bank.py training/kz4ap_proto/detect.py training/tests/test_proto_bank.py training/tests/test_proto_streams.py training/tests/test_proto_detect.py
```
```powershell
git commit -m "Start the filter-bank prototype: parameters, recorded streams, the bank and the LLR"
```

---

### Task 5: Noise — the three-tap level, the shared spectrum, and both level variants

Each branch's σ_v,k² (Design decisions, "Noise"), in the three variants E10 compares (owner: the absolute level is decided by measurement): (a) `spectrum`, branch 1's level by the milestone-2 three-tap guard and every branch's by the ratio of the masked, smoothed noise spectrum weighted by the branches' power responses; (b) `spectrum-level`, the spectrum's own level divided by the mask's measured bias; (c) `branch`, the recorded fallback, a three-tap estimate per branch. Tests use white noise, channel-shaped noise (where the white formula is off by 4.8–5.2× for every branch) and a strong station keying from the first sample; a measurement step records the mask's kept fraction, its bias and the segment acceptance rate.

**Files:**
- Create: `training/kz4ap_proto/noise.py`
- Test: `training/tests/test_proto_noise.py`

**Interfaces:**
- Consumes: `bank.power_response`, `bank.boxcar`, `bank.branch_samples`, `bank.branch_lengths_s`, `ProtoConfig`.
- Produces:
  - `noise.guard_mean(kappa: float) -> float`
  - `noise.ThreeTapNoise(cfg, rate_hz, branch_n)`: `.update(P, n0, n1)`, `.var` (K,) FS² per real component, `.started: bool`
  - `noise.SpectrumNoise(cfg, rate_hz, branch_n, level="three-tap")` (`level="spectrum"` for variant (b)) and `noise.BranchNoise(cfg, rate_hz, branch_n)`: `.update(u, P, n0, n1)`, `.sigma2() -> np.ndarray` (K,) FS² per real component; `SpectrumNoise` also counts `segments_offered`, `segments` (accepted), `kept_fraction_sum` (Σ over offered segments of the fraction left in) and `masked_power_sum` (Σ over accepted segments of the masked periodogram's mean power)
  - `noise.make_noise(cfg, rate_hz, branch_n)` (by `cfg.noise_method`: `spectrum`, `spectrum-level`, `branch`)
  - Convention: `P` is the (K, N) array of |v_k|² for the whole stream (float32 is fine); `update` reads only samples before `n1`; call `update` before `sigma2` in each block.

- [ ] **Step 1: Write the failing tests**

Create `training/tests/test_proto_noise.py`:

```python
import numpy as np
import pytest

from kz4ap_proto.bank import boxcar, branch_lengths_s, branch_samples
from kz4ap_proto.noise import BranchNoise, SpectrumNoise, ThreeTapNoise, guard_mean
from kz4ap_proto.params import ProtoConfig
from kz4ap_synth.generate import keying_envelope
from kz4ap_synth.messages import random_text
from kz4ap_synth.morse import keying_intervals

RATE = 1500.0
CFG = ProtoConfig()
N = branch_samples(branch_lengths_s(CFG), RATE)


def white(n, seed):
    rng = np.random.default_rng(seed)
    return (rng.standard_normal(n) + 1j * rng.standard_normal(n)) / np.sqrt(2)  # 1 FS^2 per sample


def lowpass():
    """A 129-tap Blackman-windowed sinc, -6 dB relative to the passband at +/-150 Hz, unity gain at 0 Hz: the channel filter's shape."""
    k = np.arange(129) - 64
    h = np.sinc(2 * 150.0 / RATE * k) * np.blackman(129)
    return h / h.sum()


def powers(u, ns):
    return np.stack([np.abs(boxcar(u, int(n))) ** 2 for n in ns]).astype(np.float32)


def averaged(est, u, P, from_s):
    """Run est block by block; the mean of sigma2() over the blocks after from_s."""
    block = int(round(CFG.block_s * RATE))
    kept = []
    for n0 in range(0, len(u), block):
        n1 = min(n0 + block, len(u))
        est.update(u, P, n0, n1)
        if n1 / RATE > from_s:
            kept.append(est.sigma2())
    return np.mean(kept, axis=0)


def true_sigma2(h, ns):
    """sigma_v,k^2 per real component for unit-power white noise through h, then the n-sample boxcar (derived)."""
    return np.array([0.5 * np.sum(np.convolve(h, np.ones(n) / n) ** 2) for n in ns])


def test_guard_mean_is_the_truncated_exponential_mean():
    assert guard_mean(1.75) == pytest.approx(0.632, abs=1e-3)


def test_three_tap_estimate_is_unbiased_in_white_noise():
    # 60 s averaged over the last 50 s: branch 32 (184 ms) has about 270 independent |v|^2 samples there, so its
    # scatter is about 6% (derived, rough); 15% leaves room for it without hiding a bias of the guard's size.
    ns = N[[0, 15, 31]]
    u = white(int(60 * RATE), 1)
    est = BranchNoise(CFG, RATE, ns)
    assert averaged(est, u, powers(u, ns), 10.0) == pytest.approx(0.5 / ns, rel=0.15)


def test_spectrum_gives_one_over_n_in_white_noise():
    u = white(int(20 * RATE), 2)
    est = SpectrumNoise(CFG, RATE, N)
    assert averaged(est, u, powers(u, N), 10.0) == pytest.approx(0.5 / N, rel=0.1)


def test_spectrum_level_arm_corrects_the_mask_bias_in_white_noise():
    u = white(int(20 * RATE), 7)
    est = SpectrumNoise(CFG, RATE, N, level="spectrum")
    assert averaged(est, u, powers(u, N), 10.0) == pytest.approx(0.5 / N, rel=0.1)


def test_the_mask_keeps_most_noise_and_reads_it_slightly_low():
    # Measured in review: the mask keeps 63% of noise-only samples, whose power reads 2.1% low (white noise).
    u = white(int(60 * RATE), 8)
    est = SpectrumNoise(CFG, RATE, N)
    averaged(est, u, powers(u, N), 0.0)
    kept = est.kept_fraction_sum / est.segments_offered
    bias = est.masked_power_sum / est.segments  # true power per sample is 1 FS^2
    print(f"kept fraction {kept:.3f}, accepted segments {est.segments}/{est.segments_offered}, mask bias {bias:.4f}")
    assert 0.5 < kept < 0.8 and 0.95 < bias < 1.0


def test_spectrum_follows_channel_shaped_noise():
    h = lowpass()
    u = np.convolve(white(int(30 * RATE), 3), h, mode="same")
    truth = true_sigma2(h, N)
    assert truth[0] > 2 * 0.5 * np.sum(h ** 2) / N[0]  # the white-noise formula would be off by more than 2x
    est = SpectrumNoise(CFG, RATE, N)
    assert averaged(est, u, powers(u, N), 10.0) == pytest.approx(truth, rel=0.15)


def test_spectrum_keeps_marks_out_when_a_station_keys_from_the_first_sample():
    # A 25 WPM station at 10 FS from the first sample (S500 about 25 dB) over channel-shaped noise.
    h = lowpass()
    n = int(12 * RATE)
    iv = keying_intervals(random_text(np.random.default_rng(4), 60), 25.0)
    carrier = 10.0 * keying_envelope(iv, 0.0, n, int(RATE))
    u = carrier + np.convolve(white(n, 5), h, mode="same")
    est = SpectrumNoise(CFG, RATE, N)
    assert averaged(est, u, powers(u, N), 7.0) == pytest.approx(true_sigma2(h, N), rel=0.3)


def test_the_fallback_estimates_each_branch_on_its_own():
    h = lowpass()
    u = np.convolve(white(int(40 * RATE), 6), h, mode="same")
    ns = N[[0, 15, 31]]
    assert averaged(BranchNoise(CFG, RATE, ns), u, powers(u, ns), 10.0) == pytest.approx(true_sigma2(h, ns), rel=0.15)
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `.venv\Scripts\python -m pytest training/tests/test_proto_noise.py -q`
Expected: `ModuleNotFoundError: No module named 'kz4ap_proto.noise'`.

- [ ] **Step 3: Implement**

Create `training/kz4ap_proto/noise.py`:

```python
"""Noise estimates for the branches (spec 4.2): branch 1's level by the milestone-2 three-tap guard and
every branch's by the shape of the shared noise spectrum; or, the recorded fallback, a three-tap estimate
per branch. Variances are per real component of v_k, FS^2."""

from __future__ import annotations

import math

import numpy as np

from .bank import power_response

MIN_VAR = 1e-20  # FS^2 (-200 dBFS): keeps x and a finite on noise-free input


def guard_mean(kappa: float) -> float:
    """E[y | y < kappa] for y ~ Exp(1): what the guard's truncation leaves of the mean (0.632 at 1.75)."""
    return 1.0 - kappa * math.exp(-kappa) / (1.0 - math.exp(-kappa))


class ThreeTapNoise:
    """sigma_v,k^2 of each boxcar in branch_n by the milestone-2 guard (docs/signal-processing.md 8b): the tap
    v[n-N] updates sigma^2 only if |v[n-N]|^2/(2 sigma^2) < kappa and |v[n]|^2, |v[n-2N]|^2 are below
    kappa_n 2 sigma^2 (three taps N apart share no inputs); the mean of the accepted taps is divided by
    m(kappa). The first estimate is the 20% quantile of |v|^2 over the warm-up (a provisional one before).
    Updated once per block with the block's starting sigma^2 (tau_n >> one block)."""

    def __init__(self, cfg, rate_hz: float, branch_n):
        self.n = np.asarray(branch_n, dtype=int)
        self.kappa, self.kappa_n = cfg.noise_guard, cfg.neighbor_guard
        self.m = guard_mean(cfg.noise_guard)
        self.alpha = 1.0 - math.exp(-1.0 / (cfg.noise_tau_s * rate_hz))
        self.warmup = max(1, int(round(cfg.noise_warmup_s * rate_hz)))
        self.var = np.full(len(self.n), np.nan)
        self.weight = np.zeros(len(self.n))
        self.started = False

    def update(self, P, n0: int, n1: int) -> None:
        if n1 <= n0:
            return
        if not self.started:
            q = np.quantile(np.asarray(P[:, :n1], float), 0.2, axis=1) / (2.0 * -math.log(0.8))
            self.var = np.maximum(q, MIN_VAR)
            if n1 >= self.warmup:
                self.started = True
                self.weight[:] = 0.1 * self.warmup  # milestone 2: the warm-up counts as this much weight
            return
        k = len(self.n)
        idx = np.broadcast_to(np.arange(n0, n1), (k, n1 - n0))
        lag = self.n[:, None]
        valid = idx >= 2 * lag
        now = np.take_along_axis(P, idx, axis=1).astype(float)
        mid = np.take_along_axis(P, np.maximum(idx - lag, 0), axis=1).astype(float)
        old = np.take_along_axis(P, np.maximum(idx - 2 * lag, 0), axis=1).astype(float)
        two_var = 2.0 * self.var[:, None]
        ok = valid & (mid < self.kappa * two_var) & (now < self.kappa_n * two_var) & (old < self.kappa_n * two_var)
        count = ok.sum(axis=1)
        has = count > 0
        if not has.any():
            return
        mean_mid = (mid * ok).sum(axis=1) / np.maximum(count, 1)
        self.weight += count
        step = np.maximum(1.0 - (1.0 - self.alpha) ** count, count / np.maximum(self.weight, 1.0))
        target = mean_mid / (2.0 * self.m)
        self.var = np.where(has, np.maximum(self.var + step * (target - self.var), MIN_VAR), self.var)


class BranchNoise:
    """The recorded fallback (spec 4.2): each branch's own three-tap estimate."""

    def __init__(self, cfg, rate_hz: float, branch_n):
        self.est = ThreeTapNoise(cfg, rate_hz, branch_n)

    def update(self, u, P, n0: int, n1: int) -> None:
        self.est.update(P, n0, n1)

    def sigma2(self) -> np.ndarray:
        return self.est.var.copy()


class SpectrumNoise:
    """The shared noise spectrum (spec 4.2). sigma_v,k^2 = sigma_v,1^2 (W_k . S) / (W_1 . S): branch 1's level
    from the three-tap guard, the ratio from S, an exponential average (tau_n) of Hann-windowed periodograms of
    u over segments of T_seg, smoothed over +/- spectrum_smoothing_hz; W_k[m] is the mean of |H_k(f)|^2 over
    bin m. A sample of u is left out of its segment if any |v_1|^2 that contains it, or lies within
    guard_margin_s of it, reaches kappa_n 2 sigma_v,1^2; a segment enters only if at least min_clean_fraction
    of it is left in. Until the three-tap warm-up is over no segment enters and the shape is white
    (ratio N_1/N_k, exact for white noise by Parseval)."""

    SUBSAMPLES = 16  # points per bin for W_k

    def __init__(self, cfg, rate_hz: float, branch_n, level: str = "three-tap"):
        if level not in ("three-tap", "spectrum"):
            raise ValueError(f"unknown noise level source {level!r}")
        self.level = level
        self.mask_bias = cfg.mask_bias
        self.n = np.asarray(branch_n, dtype=int)
        self.ref = ThreeTapNoise(cfg, rate_hz, self.n[:1])
        self.m = max(16, int(round(cfg.segment_s * rate_hz)))
        self.window = 0.5 - 0.5 * np.cos(2.0 * np.pi * np.arange(self.m) / self.m)
        centers = np.fft.fftfreq(self.m, d=1.0 / rate_hz)
        offsets = ((np.arange(self.SUBSAMPLES) + 0.5) / self.SUBSAMPLES - 0.5) * rate_hz / self.m
        f = centers[:, None] + offsets[None, :]
        self.bin_weights = np.stack([power_response(f, int(n), rate_hz).mean(axis=1) for n in self.n])  # (K, M)
        self.half = max(0, int(round(cfg.spectrum_smoothing_hz * self.m / rate_hz)))
        self.kappa_n = cfg.neighbor_guard
        self.reach = int(round(cfg.guard_margin_s * rate_hz))
        self.min_clean = cfg.min_clean_fraction
        self.beta = 1.0 - math.exp(-cfg.segment_s / cfg.noise_tau_s)
        self.shape = None       # periodogram average, FS^2 per bin
        self.ratio = None       # (W_k . S) / (W_1 . S), cached
        self.segments = 0              # accepted
        self.segments_offered = 0      # after the warm-up
        self.kept_fraction_sum = 0.0   # fraction of each offered segment left in by the mask
        self.masked_power_sum = 0.0    # mean power per sample of each accepted masked periodogram, FS^2
        self.next_start = 0

    def update(self, u, P, n0: int, n1: int) -> None:
        self.ref.update(P[:1], n0, n1)
        look = int(self.n[0]) - 1 + self.reach  # the flag needs |v_1|^2 this far past a segment
        while self.next_start + self.m + look <= n1:
            s = self.next_start
            self.next_start += self.m
            if not self.ref.started:
                continue
            clean = self._clean(P[0], s)
            self.segments_offered += 1
            self.kept_fraction_sum += float(clean.mean())
            if clean.mean() >= self.min_clean:
                self._accept(np.asarray(u[s:s + self.m]), clean)

    def _clean(self, p1, s: int) -> np.ndarray:
        n1 = int(self.n[0])
        lo = max(0, s - self.reach)
        hi = min(len(p1), s + self.m + n1 - 1 + self.reach)
        flagged = np.asarray(p1[lo:hi], float) >= self.kappa_n * 2.0 * self.ref.var[0]
        c = np.concatenate(([0], np.cumsum(flagged)))
        i = np.arange(s, s + self.m)
        a = np.clip(i - self.reach - lo, 0, len(flagged))
        b = np.clip(i + n1 - 1 + self.reach - lo + 1, 0, len(flagged))
        return (c[b] - c[a]) == 0  # u[i] feeds v_1[i .. i + N_1 - 1]; none of them (+/- the reach) is flagged

    def _accept(self, seg, clean) -> None:
        w = self.window * clean
        norm = float(np.sum(w * w))
        if norm <= 0.0:
            return
        periodogram = np.abs(np.fft.fft(seg * w)) ** 2 / norm  # mean over bins = power per sample, FS^2
        self.segments += 1
        self.masked_power_sum += float(periodogram.mean())
        if self.shape is None:
            self.shape = periodogram
        else:
            self.shape = self.shape + max(self.beta, 1.0 / self.segments) * (periodogram - self.shape)
        self.ratio = None

    def _smoothed(self) -> np.ndarray:
        if self.half == 0:
            return self.shape
        ext = np.concatenate((self.shape[-self.half:], self.shape, self.shape[:self.half]))
        return np.convolve(ext, np.ones(2 * self.half + 1) / (2 * self.half + 1), mode="valid")

    def sigma2(self) -> np.ndarray:
        level = self.ref.var[0]
        if self.shape is None:
            return level * self.n[0] / self.n
        if self.ratio is None:
            k = self.bin_weights @ self._smoothed()
            self.ratio = k / k[0]
            # variant (b): complex power of v_k is (1/M) sum_m I_m W_k[m]; per real component half of it, and the
            # masked periodogram reads mask_bias of the true power (measured)
            self.absolute = 0.5 * k / self.m / self.mask_bias
            # [corrected 2026-10-02: the code divides k by the per-branch mask_bias before the ratio, so both
            # variants' relative levels are corrected; see training/kz4ap_proto/noise.py]
        return self.absolute.copy() if self.level == "spectrum" else level * self.ratio


def make_noise(cfg, rate_hz: float, branch_n):
    if cfg.noise_method == "spectrum":
        return SpectrumNoise(cfg, rate_hz, branch_n)
    if cfg.noise_method == "spectrum-level":
        return SpectrumNoise(cfg, rate_hz, branch_n, level="spectrum")
    if cfg.noise_method == "branch":
        return BranchNoise(cfg, rate_hz, branch_n)
    raise ValueError(f"unknown noise method {cfg.noise_method!r}")
```

- [ ] **Step 4: Run the tests to verify they pass**

Run: `.venv\Scripts\python -m pytest training/tests/test_proto_noise.py -q`
Expected: all pass (run once with `-s` to see the mask's printed numbers). The tolerances come from the estimators' scatter (7% for branch 1 over τ_n, derived; the shape averaged over 10 s) and a small low bias of the three-tap guard in correlated (channel-shaped) noise, where taps N_1 apart are not quite independent. If a test fails by a few percent beyond its tolerance, report the measured ratio; do not widen the tolerance. If `test_spectrum_keeps_marks_out_…` fails by more, the mask lets marks in: debug `_clean` first.

- [ ] **Step 5: Measure the mask on this code and store its bias**

Run `.venv\Scripts\python -m pytest training/tests/test_proto_noise.py -q -s -k mask` and read the printed kept fraction, accepted segments and mask bias (review: 63% kept, 2.1% low). Set `ProtoConfig.mask_bias` to the printed bias (four decimals) with the comment "measured (Task 5, white noise, 60 s)", and note all three numbers for the results document (Task 13, section 1). If the kept fraction or the acceptance rate is far below the review's (for example fewer than half the offered segments accepted), report it: variant (a)'s shape and variant (b)'s level would rest on few segments.

- [ ] **Step 6: Commit**

```powershell
git add training/kz4ap_proto/noise.py training/tests/test_proto_noise.py training/kz4ap_proto/params.py
```
```powershell
git commit -m "Add the prototype's noise estimates: three-tap level and shared spectrum shape"
```

---

### Task 6: Keying — amplitude, LLR keying, squelch, unknown-amplitude test, re-keying

All branches keyed at once, one block at a time (Design decisions, "Keying per branch" and "The first marks of an over"): the p-weighted amplitude, the LLR with ±1 nat hysteresis and each branch's squelch; the unknown-amplitude test (a threshold on x, the nominal or the calibrated one) for an over's first marks, during which the amplitude is seeded from, and the re-key weight counted in, keyed samples only; and `rekey`, the full-LLR keying of a stored stretch. The key test checks the derived claim that a keyed rectangular mark keeps its length through a matched branch at high SNR.

**Files:**
- Create: `training/kz4ap_proto/keying.py`
- Test: `training/tests/test_proto_keying.py`

**Interfaces:**
- Consumes: `detect.envelope_llr`, `detect.logistic`, `ProtoConfig`.
- Produces:
  - `keying.hysteresis(down, up, initial) -> np.ndarray` ((K, n) bool; up wins)
  - `keying.edges(key, before, n0) -> list[list[tuple[int, bool]]]` (per branch: sample index and the key state after it)
  - `keying.BankKeyer(cfg, rate_hz, lengths_s)`: attributes `amp2`, `weight` (samples: keyed samples while `unknown`, p-weight after), `prev_amp2` (NaN if none), `unknown`, `key` (all (K,)), `a_min`, `x_on` ((K,); `cfg.x_on_values` when given), `x_off`, `rekey_weight` (samples); method `ready_to_rekey() -> np.ndarray` (bool (K,)); methods `step(P, sigma2) -> (key (K, n), p (K, n; 0 where squelched), before (K,), a (K,))`, `start_over(k)`, `finish_over_start(k, amp2, key_now)`.
  - `keying.rekey(P, sigma2, amp2, cfg, a_min) -> np.ndarray` (bool key state over the stretch, from key up).

- [ ] **Step 1: Write the failing tests**

Create `training/tests/test_proto_keying.py`:

```python
import math

import numpy as np
import pytest

from kz4ap_proto.bank import boxcar
from kz4ap_proto.keying import BankKeyer, edges, hysteresis, rekey
from kz4ap_proto.params import ProtoConfig
from kz4ap_synth.morse import keying_intervals

RATE = 1500.0
CFG = ProtoConfig()


def rectangles(intervals, n, start_s):
    env = np.zeros(n)
    for a, b in intervals:
        env[int(round((a + start_s) * RATE)):int(round((b + start_s) * RATE))] = 1.0
    return env


def noise(n, power, seed):
    rng = np.random.default_rng(seed)
    return (rng.standard_normal(n) + 1j * rng.standard_normal(n)) * math.sqrt(power / 2)


def marks_of(key, before=False):
    out, down = [], None
    for i, d in edges(key[None, :], np.array([before]), 0)[0]:
        if d:
            down = i
        elif down is not None:
            out.append((down, i))
            down = None
    return out


def test_hysteresis_holds_between_the_thresholds():
    down = np.array([[0, 1, 0, 0, 0, 0]], bool)
    up = np.array([[0, 0, 0, 1, 0, 0]], bool)
    assert hysteresis(down, up, np.array([False])).tolist() == [[False, True, True, False, False, False]]
    assert hysteresis(down & False, up & False, np.array([True])).tolist() == [[True] * 6]


def test_edges_list_every_change_of_state():
    key = np.array([[False, True, True, False]])
    assert edges(key, np.array([True]), 100) == [[(100, False), (101, True), (103, False)]]


def test_unknown_amplitude_thresholds_follow_the_rayleigh_tail():
    keyer = BankKeyer(CFG, RATE, np.array([14 / RATE, 276 / RATE]))
    assert keyer.x_on[0] == pytest.approx(math.sqrt(-2 * math.log(0.01 * 14 / RATE)))  # 4.31
    assert keyer.x_on[1] == pytest.approx(math.sqrt(-2 * math.log(0.01 * 276 / RATE)))  # 3.54
    assert keyer.x_off == pytest.approx(1.5518, abs=1e-4)
    assert keyer.a_min[0] == pytest.approx(3 * (14 / RATE / 0.016) ** 0.25)


def test_marks_through_a_matched_branch_keep_their_length():
    # At high SNR the +/-1 nat crossings sit at x = a/2 + O(ln a / a): half the amplitude, where the boxcar's
    # ramps are L apart, so a rectangular mark d >= L keeps its length (derived).
    n_box = 60  # 40 ms, 0.83 of the 25 WPM dit
    iv = keying_intervals("PARIS PARIS", 25.0)
    n = int(round((iv[-1][1] + 1.0) * RATE))
    u = rectangles(iv, n, 0.5) + noise(n, 1e-4, 1)
    P = np.abs(boxcar(u, n_box)) ** 2
    sigma2 = 0.5 * 1e-4 / n_box
    keyer = BankKeyer(CFG, RATE, np.array([n_box / RATE]))
    keyer.unknown[:] = False
    keyer.amp2[:] = 1.0
    key, _, _, _ = keyer.step(P[None, :], np.array([sigma2]))
    measured = [(b - a) / RATE for a, b in marks_of(key[0])]
    truth = [b - a for a, b in iv]
    assert len(measured) == len(truth) == 28
    assert measured == pytest.approx(truth, abs=2.5 / RATE)


def test_the_squelch_keeps_noise_out_without_an_amplitude():
    n = int(20 * RATE)
    P = (np.abs(boxcar(noise(n, 1.0, 2), 14)) ** 2)[None, :]
    keyer = BankKeyer(CFG, RATE, np.array([14 / RATE]))
    keyer.unknown[:] = False
    key, p, _, _ = keyer.step(P, np.array([0.5 / 14]))
    assert not key.any() and not p.any()


def test_calibrated_thresholds_replace_the_nominal_ones():
    keyer = BankKeyer(CFG.with_values(x_on_values=[5.0, 4.0]), RATE, np.array([14 / RATE, 276 / RATE]))
    assert keyer.x_on.tolist() == [5.0, 4.0]
    with pytest.raises(ValueError):
        BankKeyer(CFG.with_values(x_on_values=[5.0]), RATE, np.array([14 / RATE, 276 / RATE]))


def test_an_unknown_amplitude_counts_only_keyed_samples():
    # Review I5: with a reset amplitude p = 0.44 on every sample, so a p-weight fills whatever the key does.
    # Noise alone: (almost) nothing is keyed, so the re-key weight stays near 0 after 10 s.
    n = int(10 * RATE)
    P = (np.abs(boxcar(noise(n, 1.0, 12), 60)) ** 2)[None, :]
    keyer = BankKeyer(CFG, RATE, np.array([60 / RATE]))
    keyer.step(P, np.array([0.5 / 60]))
    assert keyer.weight[0] < 0.05 * RATE and not keyer.ready_to_rekey()[0]
    # A keyed 1 FS carrier: the weight is the keyed time and the amplitude is seeded from the keyed samples
    # (T and S's dah and dits give flat tops on more than 10% of the keyed samples through a 40 ms boxcar).
    iv = keying_intervals("TEST TEST", 25.0)
    n = int(4 * RATE)
    u = rectangles(iv, n, 0.5) + noise(n, 1e-3, 13)
    P = (np.abs(boxcar(u, 60)) ** 2)[None, :]
    keyer = BankKeyer(CFG, RATE, np.array([60 / RATE]))
    key, _, _, _ = keyer.step(P, np.array([0.5 * 1e-3 / 60]))
    assert keyer.weight[0] == pytest.approx(key.sum())
    assert keyer.amp2[0] == pytest.approx(1.0, rel=0.1)  # the 0.9 quantile sits on the marks' flat tops
    assert keyer.ready_to_rekey()[0] == (key.sum() >= 0.4 * RATE)


def test_unknown_amplitude_test_keys_noise_rarely():
    # Noise alone with the nominal threshold: the independent-sample argument says R_fa = 0.01 /s (2 in 200 s), but
    # the envelope's upcrossings make it 7-10x more (review: 0.070 /s at 9.3 ms, 9-18 here over seeds). E9
    # calibrates x_on by measurement; this test only bounds the nominal rate and records it.
    n = int(200 * RATE)
    P = (np.abs(boxcar(noise(n, 1.0, 3), 14)) ** 2)[None, :]
    keyer = BankKeyer(CFG, RATE, np.array([14 / RATE]))
    key, _, _, _ = keyer.step(P, np.array([0.5 / 14]))
    count = len(marks_of(key[0]))
    print(f"false key-downs in 200 s of noise: {count}")  # recorded in the Task 13 results as the measured rate
    assert count <= 20


def test_unknown_amplitude_test_keys_a_strong_station_at_once():
    iv = keying_intervals("TEST", 25.0)
    n = int(3 * RATE)
    u = rectangles(iv, n, 0.5) + noise(n, 1e-3, 4)
    P = (np.abs(boxcar(u, 60)) ** 2)[None, :]
    keyer = BankKeyer(CFG, RATE, np.array([60 / RATE]))
    key, _, _, _ = keyer.step(P, np.array([0.5 * 1e-3 / 60]))
    first = marks_of(key[0])[0]
    assert first[0] / RATE == pytest.approx(0.5, abs=62 / RATE)  # within the boxcar's length of the true start


def test_rekey_matches_the_full_llr_keying():
    iv = keying_intervals("TEST", 25.0)
    n = int(3 * RATE)
    u = rectangles(iv, n, 0.5) + noise(n, 1e-3, 5)
    P = np.abs(boxcar(u, 60)) ** 2
    sigma2 = 0.5 * 1e-3 / 60
    keyer = BankKeyer(CFG, RATE, np.array([60 / RATE]))
    keyer.unknown[:] = False
    keyer.amp2[:] = 1.0
    key, _, _, _ = keyer.step(P[None, :], np.array([sigma2]))
    assert np.array_equal(rekey(P, sigma2, 1.0, CFG, keyer.a_min[0]), key[0])
    assert not rekey(P, sigma2, 0.0, CFG, keyer.a_min[0]).any()  # no amplitude: squelched


def test_start_over_keeps_the_established_amplitude_as_fallback():
    keyer = BankKeyer(CFG, RATE, np.array([14 / RATE, 60 / RATE]))
    keyer.unknown[:] = False
    keyer.amp2[:] = [2.0, 3.0]
    keyer.weight[:] = 100.0
    keyer.start_over(1)
    assert keyer.unknown.tolist() == [False, True]
    assert keyer.amp2[1] == 0.0 and keyer.weight[1] == 0.0 and keyer.prev_amp2[1] == 3.0
    assert math.isnan(keyer.prev_amp2[0])
    keyer.finish_over_start(1, 2.5, True)
    assert not keyer.unknown[1] and keyer.amp2[1] == 2.5 and keyer.key[1]
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `.venv\Scripts\python -m pytest training/tests/test_proto_keying.py -q`
Expected: `ModuleNotFoundError: No module named 'kz4ap_proto.keying'`.

- [ ] **Step 3: Implement**

Create `training/kz4ap_proto/keying.py`:

```python
"""Per-branch amplitude, log-likelihood ratio, squelch and keying (spec 4.3), and the unknown-amplitude
test for the first marks of an over (spec 4.7), for all branches at once, one block at a time."""

from __future__ import annotations

import math

import numpy as np

from .detect import envelope_llr, logistic


def hysteresis(down, up, initial) -> np.ndarray:
    """Key state (K, n): down where `down`, up where `up` (up wins), otherwise the state before;
    `initial` (K,) is the state before the first sample."""
    down = np.asarray(down, bool)
    up = np.asarray(up, bool)
    n = down.shape[1]
    event = np.where(up, -1, np.where(down, 1, 0))
    index = np.where(event != 0, np.arange(n)[None, :], -1)
    last = np.maximum.accumulate(index, axis=1)
    value = np.take_along_axis(event, np.maximum(last, 0), axis=1)
    return np.where(last >= 0, value > 0, np.asarray(initial, bool)[:, None])


def edges(key, before, n0: int) -> list[list[tuple[int, bool]]]:
    """Per branch, (sample index, key state after it) at every change of key state."""
    key = np.asarray(key, bool)
    prev = np.concatenate((np.asarray(before, bool)[:, None], key[:, :-1]), axis=1)
    rows, cols = np.nonzero(key != prev)
    out: list[list[tuple[int, bool]]] = [[] for _ in range(key.shape[0])]
    for r, c in zip(rows.tolist(), cols.tolist()):
        out[r].append((n0 + c, bool(key[r, c])))
    return out


def _log_prior_odds(cfg) -> float:
    return math.log(cfg.prior_key_down / (1.0 - cfg.prior_key_down))


class BankKeyer:
    """Amplitude, LLR keying with hysteresis and squelch per branch; the unknown-amplitude test while a
    branch is at the start of an over (`unknown`)."""

    def __init__(self, cfg, rate_hz: float, lengths_s):
        lengths_s = np.asarray(lengths_s, float)
        self.alpha = 1.0 - math.exp(-1.0 / (cfg.amplitude_tau_s * rate_hz))
        self.log_prior = _log_prior_odds(cfg)
        self.h = cfg.hysteresis_nats
        # Squelch: in noise alone a-hat^2 is a mean over about tau_a / L_k independent samples, so its spread
        # grows as sqrt(L_k) and a_min as L_k^(1/4) (milestone 2's principle with each branch's own sigma_v).
        self.a_min = cfg.squelch_a * (lengths_s / cfg.squelch_ref_s) ** cfg.squelch_exponent
        # Unknown amplitude: about 1/L_k independent envelope samples per second in noise alone, each above x
        # with probability exp(-x^2/2) (Rayleigh): x_on,k = sqrt(-2 ln(R_fa L_k)), nominally.
        # (heuristic: the envelope's upcrossings make it 7-10x more; E9 calibrates x_on_values by measurement)
        if cfg.x_on_values:
            if len(cfg.x_on_values) != len(lengths_s):
                raise ValueError("x_on_values needs one threshold per branch")
            self.x_on = np.asarray(cfg.x_on_values, float)
        else:
            self.x_on = np.sqrt(-2.0 * np.log(np.minimum(0.5, cfg.false_marks_per_s * lengths_s)))
        self.x_off = math.sqrt(-2.0 * math.log(cfg.release_probability))
        self.rekey_weight = cfg.rekey_after_s * rate_hz  # samples of keyed time (W_min)
        k = len(lengths_s)
        self.amp2 = np.zeros(k)          # s_k^2, FS^2
        self.weight = np.zeros(k)        # behind it, samples: keyed samples while unknown, p-weight after
        self.prev_amp2 = np.full(k, np.nan)
        self.unknown = np.ones(k, bool)  # the stream's start is an over's start
        self.keyed: list[list[np.ndarray]] = [[] for _ in range(k)]  # |v|^2 of keyed samples while unknown
        self.keyed_cap = max(1, int(round(4 * self.rekey_weight)))    # at most this many kept, samples
        self.key = np.zeros(k, bool)

    def step(self, P, sigma2):
        """P: (K, n) |v_k|^2, FS^2; sigma2: (K,) sigma_v,k^2, FS^2. Returns the key state (K, n), the posterior
        p (K, n; 0 where squelched), the key state before the block (K,) and a_k (K,) at the block's start."""
        P = np.asarray(P, float)
        sigma2 = np.asarray(sigma2, float)
        a = np.sqrt(self.amp2 / sigma2)
        x = np.sqrt(P / sigma2[:, None])
        g = envelope_llr(x, a[:, None]) + self.log_prior
        p = logistic(g)
        signal = (a >= self.a_min)[:, None]
        unknown = self.unknown[:, None]
        down = np.where(unknown, x > self.x_on[:, None], signal & (g > self.h))
        up = np.where(unknown, x < self.x_off, (~signal) | (g < -self.h))
        before = self.key.copy()
        key = hysteresis(down, up, before)
        self.key = key[:, -1].copy() if key.shape[1] else before
        self._update_amplitude(P, p, sigma2, key)
        return key, p * signal, before, a

    def _update_amplitude(self, P, p, sigma2, key) -> None:
        """Online EM for the Rician component (milestone 2): mean square 2 sigma^2 + s^2, p-weighted, one step
        per block: the block's weighted mean pulls s^2 by 1 - (1 - alpha)^(sum p), or by sum p / W early on.
        While a branch's amplitude is unknown (review I5) only its keyed samples count: W is their number, so
        W_min is keyed time, and s^2 is seeded as their 0.9 quantile of |v|^2 minus 2 sigma^2 (milestone 2 seeds
        from the 0.9 quantile too; a mean would be pulled down by the boxcar's ramps, which the test keys)."""
        known = ~self.unknown
        wp = np.where(known, p.sum(axis=1), 0.0)
        has = wp > 0
        mean_p = (p * P).sum(axis=1) / np.maximum(wp, 1e-300)
        self.weight += wp
        step = np.minimum(1.0, np.maximum(1.0 - (1.0 - self.alpha) ** wp, wp / np.maximum(self.weight, 1e-300)))
        target = mean_p - 2.0 * sigma2
        self.amp2 = np.where(has, np.maximum(0.0, self.amp2 + step * (target - self.amp2)), self.amp2)
        for k in np.nonzero(self.unknown & key.any(axis=1))[0]:
            self.keyed[k].append(P[k][key[k]])
            samples = np.concatenate(self.keyed[k])[-self.keyed_cap:]
            self.keyed[k] = [samples]
            self.weight[k] += float(key[k].sum())
            self.amp2[k] = max(0.0, float(np.quantile(samples, 0.9)) - 2.0 * sigma2[k])

    def ready_to_rekey(self) -> np.ndarray:
        return self.unknown & (self.weight >= self.rekey_weight)

    def start_over(self, k: int) -> None:
        """A possible new over (spec 4.7): a fresh amplitude and the unknown-amplitude test; an established
        amplitude is kept as the fallback."""
        if not self.unknown[k]:
            self.prev_amp2[k] = self.amp2[k]
        self.amp2[k] = 0.0
        self.weight[k] = 0.0
        self.keyed[k] = []
        self.unknown[k] = True

    def finish_over_start(self, k: int, amp2: float, key_now: bool) -> None:
        """After the re-keying: the winning amplitude, the full LLR (and the p-weighted EM) from now on."""
        self.amp2[k] = amp2
        self.keyed[k] = []
        self.unknown[k] = False
        self.key[k] = key_now


def rekey(P, sigma2: float, amp2: float, cfg, a_min: float) -> np.ndarray:
    """Key state over a stored stretch with the full LLR at fixed sigma^2 and s^2, from key up."""
    a = math.sqrt(max(amp2, 0.0) / sigma2)
    x = np.sqrt(np.asarray(P, float) / sigma2)[None, :]
    g = envelope_llr(x, a) + _log_prior_odds(cfg)
    signal = a >= a_min
    down = np.logical_and(signal, g > cfg.hysteresis_nats)
    up = np.logical_or(not signal, g < -cfg.hysteresis_nats)
    return hysteresis(down, up, np.zeros(1, bool))[0]
```

- [ ] **Step 4: Run the tests to verify they pass**

Run: `.venv\Scripts\python -m pytest training/tests/test_proto_keying.py -q`
Expected: all pass (run once with `-s` and note the printed false key-down count for the results document). `test_marks_through_a_matched_branch_keep_their_length` checks a derived property: if it fails, print the measured durations and report them (the design's timing model relies on it); do not change the tolerance.

- [ ] **Step 5: Commit**

```powershell
git add training/kz4ap_proto/keying.py training/tests/test_proto_keying.py
```
```powershell
git commit -m "Add per-branch keying: amplitude, LLR, squelch, unknown-amplitude test, re-keying"
```

---
### Task 7: Duration fit

The per-branch speed estimate (spec §4.5; Design decisions, "Duration fit"): a mixture over the five element classes with derived priors, log-normal scatter plus the branch's timing resolution, and an outlier class; exponential memory held exactly by recursive likelihood tables over the grid; the global grid maximum (with the T_P prior), then two weighted-least-squares refinement steps; the quality Q_k. The tests pin the regressions R1 and R2 at fit level.

**Files:**
- Create: `training/kz4ap_proto/fit.py`
- Test: `training/tests/test_proto_fit.py`

**Interfaces:**
- Consumes: `kz4ap_synth.messages.VE3NEA_CHAR_WEIGHTS`, `VE3NEA_WORD_LENGTH_PROBS`; `kz4ap_synth.morse.CODES`; `ProtoConfig`.
- Produces:
  - `fit.class_priors() -> tuple[np.ndarray, np.ndarray]` ((dit, dah), (element, character, word))
  - `fit.Fit(t_s, q, w_s, tg_s, quality, weight)` (frozen; `theta() -> np.ndarray` = (T, w, qT, T_g))
  - `fit.resolution_var_s2(length_s, a, rate_hz) -> float` (σ_t², s²)
  - `fit.class_logliks(theta, is_mark, d, var_t, cfg) -> (ll (n, 5), total (n,), var_lin (n, 5))`
  - `fit.observations_loglik(fit: Fit | None, obs, cfg) -> float` (obs: sequences whose first three items are is_mark, duration s, σ_t² s²; −inf if empty or no fit)
  - `fit.classify_mark(fit, d, var_t, cfg) -> bool` (True: dah); `fit.classify_space(fit, d, var_t, cfg) -> str` ("element", "character", "word")
  - `fit.DurationFit(cfg)`: `add(is_mark, duration_s, var_t)`, `best(prior_t_s=None, prior_weight=0.0) -> Fit | None`, `copy() -> DurationFit`, `weight: float`, `history` (newest first)

- [ ] **Step 1: Write the failing tests**

Create `training/tests/test_proto_fit.py`:

```python
import numpy as np
import pytest

from kz4ap_proto.fit import (DurationFit, Fit, class_priors, classify_mark, classify_space, observations_loglik,
                             resolution_var_s2)
from kz4ap_proto.params import ProtoConfig
from kz4ap_synth.keying import farnsworth_gap_s, timed_intervals
from kz4ap_synth.messages import random_text
from kz4ap_synth.morse import keying_intervals

CFG = ProtoConfig()


def durations(intervals):
    """(is_mark, duration s) for every mark and the space before it, in order."""
    out = []
    for i, (a, b) in enumerate(intervals):
        if i:
            out.append((False, a - intervals[i - 1][1]))
        out.append((True, b - a))
    return out


def fitted(obs, prior=(None, 0.0), var_t=1e-8):
    fit = DurationFit(CFG)
    for is_mark, d in obs:
        fit.add(is_mark, d, var_t)
    return fit.best(*prior)


def text(seed=1, words=30):
    return random_text(np.random.default_rng(seed), words)


def test_class_priors_follow_ve3nea_statistics():
    marks, spaces = class_priors()
    assert marks.tolist() == pytest.approx([0.5716, 0.4284], abs=1e-4)
    assert spaces.tolist() == pytest.approx([0.6467, 0.2379, 0.1154], abs=1e-4)


def test_resolution_variance_is_two_edges_of_l_over_a_plus_sampling():
    assert resolution_var_s2(0.04, 10.0, 1500.0) == pytest.approx(2 * 0.004 ** 2 + 2 / (12 * 1500.0 ** 2))
    assert resolution_var_s2(0.04, 0.1, 1500.0) == resolution_var_s2(0.04, 1.0, 1500.0)  # a floored at 1


def test_fits_machine_keying():
    f = fitted(durations(keying_intervals(text(), 25.0)))
    assert f.t_s == pytest.approx(0.048, rel=0.02)
    assert f.q == pytest.approx(3.0, abs=0.1)
    assert abs(f.w_s) < 0.05 * 0.048
    assert f.tg_s == pytest.approx(0.048, rel=0.05)


def test_fits_farnsworth_spacing():
    f = fitted(durations(timed_intervals(text(), 18.0, farnsworth_wpm=10.0)))
    assert f.t_s == pytest.approx(1.2 / 18.0, rel=0.02)
    assert f.tg_s == pytest.approx(farnsworth_gap_s(18.0, 10.0), rel=0.05)  # 207 ms, 3.11 T


def test_fits_key_weighting():
    f = fitted(durations(timed_intervals(text(), 25.0, "machine", None, imbalance_dits=0.2)))
    assert f.w_s == pytest.approx(0.2 * 0.048, abs=0.002)
    assert f.t_s == pytest.approx(0.048, rel=0.03)


def test_fits_heavy_dahs():
    # HandKey: dah median e^1.5 = 4.48 dits with sigma_ln 0.3 (its mean is 4.69 dits).
    rng = np.random.default_rng(2)
    f = fitted(durations(timed_intervals(text(words=60), 24.0, "hand", rng)))
    assert 4.0 <= f.q <= 5.2
    assert f.t_s == pytest.approx(0.05, rel=0.06)


def test_slow_first_dits_are_not_read_as_dahs():
    # Regression R2 (milestone-2a results 3.8): a 12 WPM station's first marks are dits. The element spaces
    # between them (T - w) and the derived class priors put the global maximum at T = 100 ms, not at the
    # dahs reading T = 33 ms (checked while planning: 99.9 ms).
    assert fitted(durations(keying_intervals("HI", 12.0))).t_s == pytest.approx(0.1, rel=0.1)


def test_the_fit_follows_a_speed_step_within_its_memory():
    # Regression R1's trigger was a mark between the old clusters; the fit has no "all alike" branch.
    before = durations(keying_intervals(text(3), 20.0))
    after = durations(keying_intervals(text(4, 40), 35.0))
    assert fitted(before + after[:72]).t_s == pytest.approx(1.2 / 35.0, rel=0.05)


def test_a_tune_up_carrier_is_an_outlier():
    obs = durations(keying_intervals(text(), 25.0))
    obs = obs[:40] + [(False, 0.5), (True, 2.0), (False, 0.5)] + obs[40:]
    assert fitted(obs).t_s == pytest.approx(0.048, rel=0.02)


def test_the_periodicity_prior_decides_an_ambiguous_start():
    two_marks = [(True, 0.1), (True, 0.1)]  # dits of 12 WPM, or dahs of 36 WPM
    assert fitted(two_marks, prior=(0.1, 1.0)).t_s == pytest.approx(0.1, rel=0.1)
    assert fitted(two_marks, prior=(1.2 / 36, 1.0)).t_s == pytest.approx(1.2 / 36, rel=0.1)


def test_quality_is_higher_for_morse_than_for_random_durations():
    morse = durations(keying_intervals(text(), 25.0))
    rng = np.random.default_rng(5)
    random = [(m, float(d)) for (m, _), d in zip(morse, np.exp(rng.uniform(np.log(0.01), np.log(1.0), len(morse))))]
    assert fitted(morse).quality > fitted(random).quality + 1.0


def test_classification_follows_the_fit():
    f = fitted(durations(keying_intervals(text(), 25.0)))
    assert not classify_mark(f, 0.048, 1e-8, CFG) and classify_mark(f, 0.144, 1e-8, CFG)
    assert [classify_space(f, d, 1e-8, CFG) for d in (0.048, 0.144, 0.336)] == ["element", "character", "word"]
    obs = [(True, 0.048, 1e-8), (False, 0.144, 1e-8)]
    assert observations_loglik(f, obs, CFG) > observations_loglik(Fit(0.1, 3.0, 0.0, 0.1, 0.0, 1.0), obs, CFG)


def test_copy_is_independent_and_best_needs_an_observation():
    fit = DurationFit(CFG)
    assert fit.best() is None
    fit.add(True, 0.048, 1e-8)
    other = fit.copy()
    other.add(False, 0.048, 1e-8)
    assert len(fit.history) == 1 and len(other.history) == 2
    assert fit.weight == pytest.approx(1.0) and other.weight == pytest.approx(1.0 + np.exp(-1 / 24))
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `.venv\Scripts\python -m pytest training/tests/test_proto_fit.py -q`
Expected: `ModuleNotFoundError: No module named 'kz4ap_proto.fit'`.

- [ ] **Step 3: Implement**

Create `training/kz4ap_proto/fit.py`:

```python
"""The per-branch duration fit (spec 4.5): a mixture over the element classes with log-normal scatter plus
the branch's timing resolution and an outlier class; exponential memory held exactly by recursive
likelihood tables over a grid; the global grid maximum (with the T_P prior), then a local
weighted-least-squares refinement; the quality Q (weighted mean log-likelihood per element, nats).
Densities are in ln(duration)."""

from __future__ import annotations

import copy as _copy
import functools
import math
from collections import deque
from dataclasses import dataclass

import numpy as np

from kz4ap_synth.messages import VE3NEA_CHAR_WEIGHTS, VE3NEA_WORD_LENGTH_PROBS
from kz4ap_synth.morse import CODES

LOG_SQRT_2PI = 0.5 * math.log(2.0 * math.pi)
# Class means are linear in theta = (T, w, Q = qT, G = T_g): dit T + w, dah Q + w, element space T - w,
# character gap 3G - w, word gap 7G - w.
DESIGN = np.array([[1.0, 1.0, 0.0, 0.0],
                   [0.0, 1.0, 1.0, 0.0],
                   [1.0, -1.0, 0.0, 0.0],
                   [0.0, -1.0, 0.0, 3.0],
                   [0.0, -1.0, 0.0, 7.0]])
IS_MARK_CLASS = np.array([True, True, False, False, False])
SPACE_KINDS = ("element", "character", "word")


@functools.lru_cache(maxsize=1)
def class_priors() -> tuple[np.ndarray, np.ndarray]:
    """P(dit), P(dah) among marks and P(element), P(character), P(word) among spaces, derived from VE3NEA's
    tables: per character its dits and dahs, its elements minus one element spaces, a character gap after
    every character but a word's last ((L - 1)/L per character) and a word gap per word (1/L), L the mean
    word length (3.062 characters)."""
    total = sum(VE3NEA_CHAR_WEIGHTS.values())
    dits = sum(w * CODES[c].count(".") for c, w in VE3NEA_CHAR_WEIGHTS.items()) / total
    dahs = sum(w * CODES[c].count("-") for c, w in VE3NEA_CHAR_WEIGHTS.items()) / total
    p = np.array(VE3NEA_WORD_LENGTH_PROBS) / sum(VE3NEA_WORD_LENGTH_PROBS)
    mean_len = float(np.sum(np.arange(len(p)) * p))
    spaces = np.array([dits + dahs - 1.0, (mean_len - 1.0) / mean_len, 1.0 / mean_len])
    return np.array([dits, dahs]) / (dits + dahs), spaces / spaces.sum()


@dataclass(frozen=True)
class Fit:
    t_s: float      # T, the dit, s
    q: float        # dah/dit ratio (the spec's r)
    w_s: float      # key weighting, s
    tg_s: float     # gap timebase T_g, s
    quality: float  # Q: weighted mean log-likelihood per element, nats
    weight: float   # the memory's weight, elements

    def theta(self) -> np.ndarray:
        return np.array([self.t_s, self.w_s, self.q * self.t_s, self.tg_s])


def resolution_var_s2(length_s: float, a: float, rate_hz: float) -> float:
    """sigma_t^2, s^2: an edge through a boxcar of length L is a ramp of slope s/L, so noise of RMS sigma_v
    moves its crossing by L/a; a duration has two edges; sampling adds 1/(12 r^2) per edge (derived, first
    order, high SNR). a is floored at 1."""
    return 2.0 * (length_s / max(a, 1.0)) ** 2 + 2.0 / (12.0 * rate_hz ** 2)


def _log_priors(cfg) -> np.ndarray:
    marks, spaces = class_priors()
    return np.log((1.0 - cfg.outlier_prior) * np.concatenate((marks, spaces)))


def _log_outlier(cfg) -> float:
    lo, hi = cfg.outlier_range_s
    return math.log(cfg.outlier_prior) - math.log(math.log(hi / lo))


def class_logliks(theta, is_mark, d, var_t, cfg):
    """ll (n, 5): ln(prior x density in ln d) of each observation under each class (-inf for the other kind,
    or a class whose mean is not positive); total (n,): the log-likelihood with the outlier class; var_lin
    (n, 5): each class's variance in duration, s^2."""
    is_mark = np.asarray(is_mark, bool)
    d = np.asarray(d, float)
    var_t = np.asarray(var_t, float)
    mu = DESIGN @ np.asarray(theta, float)
    valid = mu > 0
    safe = np.where(valid, mu, 1.0)
    sigma = np.where(IS_MARK_CLASS, cfg.sigma_ln_mark, cfg.sigma_ln_space)
    s2 = sigma[None, :] ** 2 + var_t[:, None] / safe[None, :] ** 2
    z = np.log(d)[:, None] - np.log(safe)[None, :]
    ll = _log_priors(cfg)[None, :] - 0.5 * z * z / s2 - 0.5 * np.log(s2) - LOG_SQRT_2PI
    ll = np.where((IS_MARK_CLASS[None, :] == is_mark[:, None]) & valid[None, :], ll, -np.inf)
    total = np.logaddexp(np.logaddexp.reduce(ll, axis=1), _log_outlier(cfg))
    return ll, total, s2 * safe[None, :] ** 2


def observations_loglik(fit, obs, cfg) -> float:
    """Mean log-likelihood per element of observations (is_mark, duration s, sigma_t^2 s^2, ...), nats."""
    if fit is None or not obs:
        return -math.inf
    _, total, _ = class_logliks(fit.theta(), [o[0] for o in obs], [o[1] for o in obs], [o[2] for o in obs], cfg)
    return float(np.mean(total))


def classify_mark(fit: Fit, d: float, var_t: float, cfg) -> bool:
    """True if the mark is likelier a dah than a dit under the fit."""
    ll, _, _ = class_logliks(fit.theta(), [True], [d], [var_t], cfg)
    return bool(ll[0, 1] > ll[0, 0])


def classify_space(fit: Fit, d: float, var_t: float, cfg) -> str:
    """The likeliest space class under the fit: "element", "character" or "word"."""
    ll, _, _ = class_logliks(fit.theta(), [False], [d], [var_t], cfg)
    return SPACE_KINDS[int(np.argmax(ll[0, 2:]))]


@functools.lru_cache(maxsize=8)
def _grid(cfg):
    """T (log-spaced), q, w/T, T_g/T and, per class, (ln mean, 1/mean^2, mean > 0) over the grid: marks over
    (T, q, w) (the dit's broadcast over q), spaces over (T, w, T_g) (the element space's over T_g)."""
    t_min, t_max = 1.2 / cfg.max_wpm, 1.2 / cfg.min_wpm
    count = int(math.ceil(math.log(t_max / t_min) / math.log1p(cfg.t_grid_step))) + 1
    t = t_min * (1.0 + cfg.t_grid_step) ** np.arange(count)
    q = np.asarray(cfg.q_grid, float)
    w = np.asarray(cfg.w_grid, float)
    g = np.asarray(cfg.tg_grid, float)
    T = t[:, None, None]
    marks = (T * (1.0 + w[None, None, :]),
             T * (q[None, :, None] + w[None, None, :]))
    spaces = (T * (1.0 - w[None, :, None]),
              T * (3.0 * g[None, None, :] - w[None, :, None]),
              T * (7.0 * g[None, None, :] - w[None, :, None]))

    def prep(mu):
        safe = np.where(mu > 0, mu, 1.0)
        return np.log(safe), 1.0 / safe ** 2, mu > 0

    return t, q, w, g, tuple(prep(m) for m in marks), tuple(prep(m) for m in spaces)


class DurationFit:
    """One branch's fit. Every observation multiplies both tables by lambda = exp(-1/N_mem) and adds its
    log-likelihood to one of them, so the tables hold the exponentially weighted log-likelihood exactly at
    every grid point. The last 4 N_mem observations are kept for the refinement and the quality."""

    def __init__(self, cfg):
        self.cfg = cfg
        self.t, self.q, self.w, self.g, self._marks, self._spaces = _grid(cfg)
        priors = _log_priors(cfg)
        self._mark_priors, self._space_priors = priors[:2], priors[2:]
        self._log_outlier = _log_outlier(cfg)
        self.lam = math.exp(-1.0 / cfg.fit_memory)
        self.mark_table = np.zeros((len(self.t), len(self.q), len(self.w)))
        self.space_table = np.zeros((len(self.t), len(self.w), len(self.g)))
        self.weight = 0.0
        self.history: deque = deque(maxlen=int(math.ceil(4 * cfg.fit_memory)))  # newest first

    def copy(self) -> "DurationFit":
        other = _copy.copy(self)  # shares the read-only grid
        other.mark_table = self.mark_table.copy()
        other.space_table = self.space_table.copy()
        other.history = deque(self.history, maxlen=self.history.maxlen)
        return other

    def add(self, is_mark: bool, duration_s: float, var_t: float) -> None:
        """One more mark or space: its duration, s, and its timing-resolution variance sigma_t^2, s^2."""
        if not duration_s > 0:
            return
        log_d = math.log(duration_s)
        if is_mark:
            classes, priors, sigma = self._marks, self._mark_priors, self.cfg.sigma_ln_mark
        else:
            classes, priors, sigma = self._spaces, self._space_priors, self.cfg.sigma_ln_space
        total = None
        for (log_mu, inv_mu2, valid), lp in zip(classes, priors):
            s2 = sigma * sigma + var_t * inv_mu2
            z = log_d - log_mu
            ll = np.where(valid, lp - 0.5 * z * z / s2 - 0.5 * np.log(s2) - LOG_SQRT_2PI, -np.inf)
            total = ll if total is None else np.logaddexp(total, ll)
        total = np.logaddexp(total, self._log_outlier)
        self.mark_table *= self.lam
        self.space_table *= self.lam
        if is_mark:
            self.mark_table += total
        else:
            self.space_table += total
        self.weight = self.lam * self.weight + 1.0
        self.history.appendleft((bool(is_mark), float(duration_s), float(var_t)))

    def _arrays(self):
        h = self.history
        is_mark = np.fromiter((o[0] for o in h), bool, len(h))
        d = np.fromiter((o[1] for o in h), float, len(h))
        var_t = np.fromiter((o[2] for o in h), float, len(h))
        return is_mark, d, var_t, self.lam ** np.arange(len(h))

    def best(self, prior_t_s: float | None = None, prior_weight: float = 0.0) -> Fit | None:
        """The global maximum over the grid, with -prior_weight (ln T - ln T_P)^2 / (2 sigma_P^2) added when
        T_P is given, refined locally. None before any observation."""
        if not self.history:
            return None
        score = self.mark_table.max(axis=1) + self.space_table.max(axis=2)  # (T, w): best q, best T_g
        if prior_t_s is not None and prior_weight > 0:
            z = (np.log(self.t) - math.log(prior_t_s)) / self.cfg.prior_sigma_ln
            score = score - (prior_weight * 0.5 * z * z)[:, None]
        i, j = np.unravel_index(int(np.argmax(score)), score.shape)
        qi = int(np.argmax(self.mark_table[i, :, j]))
        gi = int(np.argmax(self.space_table[i, j, :]))
        t = float(self.t[i])
        theta = self._refine(np.array([t, self.w[j] * t, self.q[qi] * t, self.g[gi] * t]))
        is_mark, d, var_t, age = self._arrays()
        _, total, _ = class_logliks(theta, is_mark, d, var_t, self.cfg)
        quality = float(np.sum(age * total) / np.sum(age))
        return Fit(float(theta[0]), float(theta[2] / theta[0]), float(theta[1]), float(theta[3]), quality, self.weight)

    def _refine(self, theta: np.ndarray) -> np.ndarray:
        """Weighted least squares in the durations: every class mean is linear in theta, weights are age x
        responsibility / variance, and a weak prior (sd 0.2 T per parameter) toward the current point keeps
        unobserved classes where they are."""
        is_mark, d, var_t, age = self._arrays()
        for _ in range(self.cfg.refine_iterations):
            ll, total, var_lin = class_logliks(theta, is_mark, d, var_t, self.cfg)
            weights = age[:, None] * np.exp(ll - total[:, None]) / var_lin
            m = np.einsum("nc,ci,cj->ij", weights, DESIGN, DESIGN)
            b = np.einsum("nc,ci,n->i", weights, DESIGN, d)
            prior = 1.0 / (0.2 * theta[0]) ** 2
            new = np.linalg.solve(m + prior * np.eye(4), b + prior * theta)
            t = max(float(new[0]), 1e-4)
            theta = np.array([t, np.clip(new[1], -0.6 * t, 1.2 * t), np.clip(new[2], 2.0 * t, 6.0 * t),
                              np.clip(new[3], 0.8 * t, 10.0 * t)])
        return theta
```

- [ ] **Step 4: Run the tests to verify they pass**

Run: `.venv\Scripts\python -m pytest training/tests/test_proto_fit.py -q`
Expected: all pass (the grid parts were checked while planning; see Design decisions). If the R2 or R1 test fails, it is a design finding, not a tolerance to widen: report the fitted values to the owner.

- [ ] **Step 5: Commit**

```powershell
git add training/kz4ap_proto/fit.py training/tests/test_proto_fit.py
```
```powershell
git commit -m "Add the prototype's duration fit: recursive grid likelihood, refinement, quality"
```

---

### Task 8: Characters and the text model

Patterns to characters exactly as the engine reads them (`"*"` for no code, eight or more dits `<HH>`), and the unigram text log-probability that breaks selection ties (spec §4.6: VE3NEA's frequencies, invalid codes very unlikely).

**Files:**
- Create: `training/kz4ap_proto/text.py`
- Test: `training/tests/test_proto_text.py`

**Interfaces:**
- Consumes: `kz4ap_synth.morse.CODES`, `kz4ap_synth.messages.VE3NEA_CHAR_WEIGHTS`.
- Produces: `text.decode_pattern(pattern: str) -> str`; `text.INVALID_LOGPROB` (= ln 10⁻⁶); `text.TextModel()` with `char_logprob(symbol: str) -> float` and `mean_logprob(symbols) -> float | None` (word spaces ignored; None if no character).

- [ ] **Step 1: Write the failing tests**

Create `training/tests/test_proto_text.py`:

```python
import math

import pytest

from kz4ap_proto.text import INVALID_LOGPROB, TextModel, decode_pattern
from kz4ap_synth.messages import VE3NEA_CHAR_WEIGHTS

TOTAL = sum(VE3NEA_CHAR_WEIGHTS.values())  # 2688


def test_patterns_decode_as_the_engine_reads_them():
    assert [decode_pattern(p) for p in (".-", "-...-", "...-.-", "..--..")] == ["A", "<BT>", "<SK>", "?"]
    assert decode_pattern("........") == "<HH>" and decode_pattern(".........") == "<HH>"
    assert decode_pattern("--.--.") == "*"


def test_text_log_probability_uses_ve3nea_frequencies():
    m = TextModel()
    assert m.char_logprob("E") == pytest.approx(math.log(321 / TOTAL))
    assert m.char_logprob("<KN>") == pytest.approx(math.log(8 / TOTAL))   # valid but not in his table: his rarest
    assert m.char_logprob("*") == INVALID_LOGPROB == pytest.approx(math.log(1e-6))
    assert m.mean_logprob(["E", " ", "T"]) == pytest.approx((math.log(321 / TOTAL) + math.log(236 / TOTAL)) / 2)
    assert m.mean_logprob([" "]) is None and m.mean_logprob([]) is None
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `.venv\Scripts\python -m pytest training/tests/test_proto_text.py -q`
Expected: `ModuleNotFoundError: No module named 'kz4ap_proto.text'`.

- [ ] **Step 3: Implement**

Create `training/kz4ap_proto/text.py`:

```python
"""Characters from classified elements, and the text log-probability that breaks selection ties (spec 4.6)."""

from __future__ import annotations

import math

from kz4ap_synth.messages import VE3NEA_CHAR_WEIGHTS
from kz4ap_synth.morse import CODES

PATTERNS = {pattern: symbol for symbol, pattern in CODES.items()}
INVALID_LOGPROB = math.log(1e-6)  # an element sequence that is no Morse character (heuristic: "very unlikely")


def decode_pattern(pattern: str) -> str:
    """The symbol for a dot/dash pattern: eight or more dits read "<HH>", a pattern with no code "*"
    (as engine/src/morse.cpp and the classical decoder)."""
    if len(pattern) >= 8 and set(pattern) == {"."}:
        return "<HH>"
    return PATTERNS.get(pattern, "*")


class TextModel:
    """Unigram log-probability under VE3NEA's CW character frequencies (messages.VE3NEA_CHAR_WEIGHTS, MIT).
    A valid code missing from his table (prosigns other than <BT>, rarer punctuation) gets his rarest
    character's probability (heuristic); "*" gets 1e-6."""

    def __init__(self):
        total = sum(VE3NEA_CHAR_WEIGHTS.values())
        self.logp = {c: math.log(w / total) for c, w in VE3NEA_CHAR_WEIGHTS.items()}
        self.floor = math.log(min(VE3NEA_CHAR_WEIGHTS.values()) / total)

    def char_logprob(self, symbol: str) -> float:
        if symbol in self.logp:
            return self.logp[symbol]
        return self.floor if symbol in CODES else INVALID_LOGPROB

    def mean_logprob(self, symbols) -> float | None:
        """Mean log-probability per character, nats (word spaces ignored); None without characters."""
        values = [self.char_logprob(s) for s in symbols if s != " "]
        return sum(values) / len(values) if values else None
```

- [ ] **Step 4: Run the tests to verify they pass**

Run: `.venv\Scripts\python -m pytest training/tests/test_proto_text.py -q`
Expected: all pass. (If `decode_pattern(".........")` disagrees with `engine/src/morse.cpp` for nine dits, follow the engine and fix the test's comment.)

- [ ] **Step 5: Commit**

```powershell
git add training/kz4ap_proto/text.py training/tests/test_proto_text.py
```
```powershell
git commit -m "Add Morse character decoding and the text log-probability model"
```

---

### Task 9: Periodicity estimator

T_P from branch 1's keying probability (spec §4.4) with **three** variants and parallel windows (Design decisions, "Periodicity estimator"): the comb on mean-removed p applied to the dit-plus-space period Π = 2T, then halved (the default; owner, 2026-09-30), its teeth ±15% of T (±0.075 Π) wide; the sign-weighted edge comb on e[n] = p[n] − p[n−1]; and the spectrum-shape fit.

**Files:**
- Create: `training/kz4ap_proto/periodicity.py`
- Test: `training/tests/test_proto_periodicity.py`

**Interfaces:**
- Consumes: `ProtoConfig`; tests use `kz4ap_synth.keying.timed_intervals`, `messages.random_text`.
- Produces:
  - `periodicity.t_grid(cfg) -> np.ndarray` (1% steps, 12–240 ms)
  - `periodicity.comb_estimate(p, rate_hz, t_grid, teeth, width) -> (T s | None, score)`
  - `periodicity.edge_comb_estimate(p, rate_hz, t_grid, teeth, width) -> (T s | None, score)` (width: fraction of T)
  - `periodicity.spectrum_estimate(p, rate_hz, t_grid, nulls, width) -> (T s | None, score nats)` (width: null band × T)
  - `periodicity.Periodicity(cfg, rate_hz)`: `push(p)` (p at the channel rate r), `update(force=False) -> (T_P s | None, confidence, window s | None, updated: bool)`; attribute `per_window: list[tuple[float | None, float]]` (the last update's (T, score) per window, shortest first; None where the window was not yet full).

- [ ] **Step 1: Write the failing tests**

Create `training/tests/test_proto_periodicity.py`:

```python
import numpy as np
import pytest

from kz4ap_proto.params import ProtoConfig
from kz4ap_proto.periodicity import Periodicity
from kz4ap_synth.keying import timed_intervals
from kz4ap_synth.messages import random_text

RATE = 1500.0
OPEN = dict(comb_confidence_min=0.0, edge_confidence_min=0.0, spectrum_confidence_min=0.0)  # all confident


def keyed_p(intervals, duration_s):
    """1 while keyed, 0 otherwise, through branch 1's boxcar (14 samples, 9.3 ms), at r."""
    n = int(duration_s * RATE)
    p = np.zeros(n)
    for a, b in intervals:
        p[int(round(a * RATE)):min(n, int(round(b * RATE)))] = 1.0
    return np.convolve(p, np.ones(14) / 14)[:n]


def ten_seconds(wpm, style="machine", farnsworth=None):
    rng = np.random.default_rng(1)
    text = random_text(rng, 300)
    iv = timed_intervals(text, wpm, style, rng, farnsworth_wpm=farnsworth)
    return keyed_p(iv, iv[-1][1] + 1.0)[int(RATE):int(11 * RATE)]


@pytest.mark.parametrize("method", ["comb", "edge", "spectrum"])
@pytest.mark.parametrize("wpm,style,farnsworth", [(5.0, "machine", None), (12.0, "machine", None),
                                                  (25.0, "machine", None), (40.0, "machine", None),
                                                  (100.0, "machine", None), (25.0, "paddle", None),
                                                  (18.0, "machine", 10.0)])
def test_periodicity_finds_the_dit(method, wpm, style, farnsworth):
    if method == "edge" and (wpm == 5.0 or farnsworth):
        pytest.skip("the edge comb is checked only where the review found it working (E1 measures the rest)")
    # Checked while planning (10 s windows): comb within 0.3% (paddle 0.7%, Farnsworth 0.7%), spectrum
    # within 1.1% (Farnsworth 1.7%); the review found the comb within 5% with teeth +/-0.075 Pi wide as well.
    # Expected value T = 1.2 s / WPM (derived).
    per = Periodicity(ProtoConfig(periodicity_method=method, periodicity_windows_s=(10.0,), **OPEN), RATE)
    per.push(ten_seconds(wpm, style, farnsworth))
    t, _, window, updated = per.update(force=True)
    assert updated and window == pytest.approx(10.0)
    assert t == pytest.approx(1.2 / wpm, rel=0.05)


@pytest.mark.parametrize("method", ["comb", "spectrum"])
def test_noise_scores_below_keying(method):
    rng = np.random.default_rng(5)
    noise = np.convolve(rng.random(int(10 * RATE)) * 0.2, np.ones(14) / 14)[:int(10 * RATE)]
    scores = []
    for p in (ten_seconds(25.0), noise):
        per = Periodicity(ProtoConfig(periodicity_method=method, periodicity_windows_s=(10.0,), **OPEN), RATE)
        per.push(p)
        scores.append(per.update(force=True)[1])
    assert scores[0] > scores[1]


def test_the_shortest_full_window_is_used_and_updates_follow_the_interval():
    per = Periodicity(ProtoConfig(periodicity_windows_s=(2.0, 5.0, 10.0), **OPEN), RATE)
    p = ten_seconds(25.0)
    per.push(p[:int(1.0 * RATE)])
    assert per.update(force=True)[0] is None  # no window is full yet
    per.push(p[int(1.0 * RATE):int(1.1 * RATE)])
    assert per.update()[3] is False           # less than 0.25 s since the last update
    per.push(p[int(1.1 * RATE):])
    t, _, window, updated = per.update()
    assert updated and window == pytest.approx(2.0) and t == pytest.approx(0.048, rel=0.05)
    assert [w[0] is not None for w in per.per_window] == [True, True, True]


def test_an_unconfident_estimate_is_not_used():
    per = Periodicity(ProtoConfig(periodicity_windows_s=(10.0,), comb_confidence_min=10.0), RATE)
    per.push(ten_seconds(25.0))
    t, confidence, window, _ = per.update(force=True)
    assert t is None and window is None and confidence > 0
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `.venv\Scripts\python -m pytest training/tests/test_proto_periodicity.py -q`
Expected: `ModuleNotFoundError: No module named 'kz4ap_proto.periodicity'`.

- [ ] **Step 3: Implement**

Create `training/kz4ap_proto/periodicity.py`:

```python
"""The periodicity estimator (spec 4.4): the coarse speed T_P from branch 1's keying probability, by an
autocorrelation comb or by the spectrum's nulls, over several windows in parallel."""

from __future__ import annotations

import math

import numpy as np


def t_grid(cfg) -> np.ndarray:
    """Candidate dits, 1% apart, from the fastest to the slowest speed, s."""
    t_min, t_max = 1.2 / cfg.max_wpm, 1.2 / cfg.min_wpm
    return t_min * 1.01 ** np.arange(int(math.ceil(math.log(t_max / t_min) / math.log(1.01))) + 1)


def _normalized_acf(x):
    n = len(x)
    size = 1 << int(math.ceil(math.log2(2 * n)))
    spec = np.fft.rfft(x, size)
    return np.fft.irfft(spec * np.conj(spec), size)[:n] / float(x @ x)


def comb_estimate(p, rate_hz: float, grid, teeth: int, width: float):
    """(T, score). The comb of spec 4.4 (as corrected 2026-09-30) on mean-removed p, applied to the period Pi of a
    dit and its element space: teeth at k Pi (k = 1..teeth: 2T ... 8T), negative teeth halfway between, each the
    mean of the normalized autocorrelation within +/- width Pi of its lag (0.075 Pi = 15% of T); score = mean over
    teeth of tooth - (left + right)/2. Consecutive keying edges T apart have opposite signs, so p's structure
    repeats at 2T: its autocorrelation is low at odd and high at even multiples of T, and a comb with teeth at kT
    peaks at 2T (derived; checked while planning and in review); so the comb runs on Pi = 2T and T = Pi / 2.
    Lags reach (teeth + 1/2 + width) Pi, about 9.2 T; candidates whose reach passes half the window are skipped
    (heuristic). (None, 0) when p does not vary."""
    x = np.asarray(p, float) - float(np.mean(p))
    n = len(x)
    c0 = float(x @ x)
    if n < 16 or c0 <= 1e-12 * n:
        return None, 0.0
    acf = _normalized_acf(x)
    cs = np.concatenate(([0.0], np.cumsum(acf)))
    period = 2.0 * np.asarray(grid) * rate_hz  # Pi in samples
    half = width * period[:, None]
    k = np.arange(1, teeth + 1)[None, :]

    def band(center):
        lo = np.clip(np.floor(center - half), 0, n - 1).astype(int)
        hi = np.clip(np.ceil(center + half), 0, n - 1).astype(int)
        return (cs[hi + 1] - cs[lo]) / (hi - lo + 1)

    contrast = band(k * period[:, None]) - 0.5 * (band((k - 0.5) * period[:, None]) + band((k + 0.5) * period[:, None]))
    score = np.where((teeth + 0.5 + width) * period <= (n - 1) / 2, contrast.mean(axis=1), -np.inf)
    i = int(np.argmax(score))
    return (float(grid[i]), float(score[i])) if np.isfinite(score[i]) else (None, 0.0)


def edge_comb_estimate(p, rate_hz: float, grid, teeth: int, width: float):
    """(T, score). The sign-weighted edge comb (E1's third variant): on the signed edges e[n] = p[n] - p[n-1], teeth at
    k T (k = 1..teeth) weighted (-1)^k, because key-down and key-up edges alternate, so edges an odd number of dits
    apart tend to have opposite signs and an even number the same sign; each tooth is the mean of e's normalized
    autocorrelation within +/- width T of its lag; score = mean of the weighted teeth. Review: best on hand and
    bug keying at S500 >= 10 dB, but it fails slow keying at 3 dB (differencing whitens the noise). (None, 0)
    when e does not vary."""
    x = np.diff(np.asarray(p, float), prepend=float(p[0]) if len(p) else 0.0)
    n = len(x)
    if n < 16 or float(x @ x) <= 1e-12 * n:
        return None, 0.0
    acf = _normalized_acf(x)
    cs = np.concatenate(([0.0], np.cumsum(acf)))
    t = np.asarray(grid) * rate_hz  # T in samples
    half = width * t[:, None]
    k = np.arange(1, teeth + 1)[None, :]
    center = k * t[:, None]
    lo = np.clip(np.floor(center - half), 0, n - 1).astype(int)
    hi = np.clip(np.ceil(center + half), 0, n - 1).astype(int)
    teeth_mean = (cs[hi + 1] - cs[lo]) / (hi - lo + 1)
    score = np.where((teeth + width) * t <= (n - 1) / 2, np.mean(np.where(k % 2 == 0, 1.0, -1.0) * teeth_mean, axis=1),
                     -np.inf)
    i = int(np.argmax(score))
    return (float(grid[i]), float(score[i])) if np.isfinite(score[i]) else (None, 0.0)


def spectrum_estimate(p, rate_hz: float, grid, nulls: int, width: float):
    """(T, score in nats). Keying on a T lattice has nulls at f = k/T in its power spectrum whatever the marks'
    positions (every mark's spectrum carries sinc(f d), d a multiple of T; derived). score = mean over
    k = 1..nulls of ln(mean power in [(k - 1/2)/T, (k + 1/2)/T] / mean power within +/- width/(2T) of k/T), from
    a Hann-windowed periodogram zero-padded 4x; candidates whose last band passes Nyquist are skipped. The
    maximum is the estimate (a rule preferring the longest T near it picked 3T for paddle keying while
    planning). (None, 0) when p does not vary."""
    x = (np.asarray(p, float) - float(np.mean(p))) * np.hanning(len(p))
    if len(x) < 16 or float(x @ x) <= 1e-12 * len(x):
        return None, 0.0
    size = 4 * (1 << int(math.ceil(math.log2(len(x)))))
    power = np.abs(np.fft.rfft(x, size)) ** 2
    df = rate_hz / size
    cs = np.concatenate(([0.0], np.cumsum(power)))
    m = len(power)

    def band(f_lo, f_hi):
        lo = np.clip(np.floor(f_lo / df), 0, m - 1).astype(int)
        hi = np.clip(np.ceil(f_hi / df), 0, m - 1).astype(int)
        return (cs[hi + 1] - cs[lo]) / (hi - lo + 1)

    t = np.asarray(grid)[:, None]
    f = np.arange(1, nulls + 1)[None, :] / t
    null = band(f - 0.5 * width / t, f + 0.5 * width / t)
    around = band(f - 0.5 / t, f + 0.5 / t)
    score = np.where((nulls + 0.5) / t[:, 0] <= rate_hz / 2,
                     np.mean(np.log(around / np.maximum(null, 1e-300)), axis=1), -np.inf)
    i = int(np.argmax(score))
    return (float(grid[i]), float(score[i])) if np.isfinite(score[i]) else (None, 0.0)


class Periodicity:
    """Branch 1's posterior p, averaged down to periodicity_rate_hz, in a buffer as long as the longest
    window; every periodicity_update_s each window (shortest first) is estimated, and the confident estimate
    with the shortest window is T_P. T_P never feeds back onto its own window (spec 4.4)."""

    def __init__(self, cfg, rate_hz: float):
        self.cfg = cfg
        self.factor = max(1, int(round(rate_hz / cfg.periodicity_rate_hz)))
        self.rate = rate_hz / self.factor
        self.windows = sorted(max(16, int(round(w * self.rate))) for w in cfg.periodicity_windows_s)
        self.update_every = max(1, int(round(cfg.periodicity_update_s * rate_hz)))
        self.grid = t_grid(cfg)
        if cfg.periodicity_method == "comb":
            self.threshold = cfg.comb_confidence_min
            self._estimate = lambda x: comb_estimate(x, self.rate, self.grid, cfg.comb_teeth, cfg.comb_width)
        elif cfg.periodicity_method == "edge":
            self.threshold = cfg.edge_confidence_min
            # the edge comb's teeth are the comb's width in T: 2 x comb_width (a fraction of Pi = 2T)
            self._estimate = lambda x: edge_comb_estimate(x, self.rate, self.grid, cfg.comb_teeth, 2.0 * cfg.comb_width)
        elif cfg.periodicity_method == "spectrum":
            self.threshold = cfg.spectrum_confidence_min
            self._estimate = lambda x: spectrum_estimate(x, self.rate, self.grid, cfg.spectrum_nulls,
                                                         cfg.spectrum_null_width)
        else:
            raise ValueError(f"unknown periodicity method {cfg.periodicity_method!r}")
        self.buffer = np.zeros(0)
        self.carry = np.zeros(0)
        self.pending = 0
        self.last: tuple = (None, 0.0, None)
        self.per_window: list[tuple[float | None, float]] = [(None, 0.0)] * len(self.windows)

    def push(self, p) -> None:
        x = np.concatenate((self.carry, np.asarray(p, float)))
        whole = len(x) // self.factor * self.factor
        if whole:
            averaged = x[:whole].reshape(-1, self.factor).mean(axis=1)
            self.buffer = np.concatenate((self.buffer, averaged))[-self.windows[-1]:]
        self.carry = x[whole:]
        self.pending += len(p)

    def update(self, force: bool = False):
        """(T_P s or None, confidence, window s or None, whether it was recomputed)."""
        if not force and self.pending < self.update_every:
            return (*self.last, False)
        self.pending = 0
        found, best = None, 0.0
        self.per_window = []
        for w in self.windows:
            if len(self.buffer) < w:
                self.per_window.append((None, 0.0))
                continue
            t, score = self._estimate(self.buffer[-w:])
            self.per_window.append((t, score))
            best = max(best, score)
            if found is None and t is not None and score >= self.threshold:
                found = (t, score, w / self.rate)
        self.last = found if found is not None else (None, best, None)
        return (*self.last, True)
```

- [ ] **Step 4: Run the tests to verify they pass**

Run: `.venv\Scripts\python -m pytest training/tests/test_proto_periodicity.py -q`
Expected: all pass. The planning check used a centered 7-sample smoother at 750 samples/s; this module averages a causal 14-sample boxcar down to 750 samples/s, so the numbers may differ slightly. The edge comb was never run on these exact cases: if an edge case fails, mark it `xfail(strict=True)` with the measured estimate (its limits are E1's subject). If a comb or spectrum case fails, report the estimate and score; do not widen 5%.

- [ ] **Step 5: Commit**

```powershell
git add training/kz4ap_proto/periodicity.py training/tests/test_proto_periodicity.py
```
```powershell
git commit -m "Add the periodicity estimator: comb on the dit-plus-space period, edge comb, spectrum nulls"
```

---
### Task 10: Branch selection

Spec §4.6 as a small state machine (Design decisions, "Branch selection, corrections"): eligibility by the fitted dit against the branch's own length; the best Q among eligible branches; ties by text log-probability, then the longer branch; fallbacks when none is eligible; a switch only after the same branch has been the best **eligible** one for M selection instants in a row (fallback picks are counted separately and never complete an eligible run); and the time each branch's current eligible run began (for the correction's start).

**Files:**
- Create: `training/kz4ap_proto/select.py`
- Test: `training/tests/test_proto_select.py`

**Interfaces:**
- Consumes: `fit.Fit`, `bank.realized_lengths_s`, `ProtoConfig`.
- Produces:
  - `select.BranchView(index: int, length_s: float, fit: Fit | None, text_logprob: float | None)` (frozen; index 0-based, ladder order)
  - `select.Selector(cfg, lengths_s)`: `eligible(view) -> bool`, `best(views, prior_t_s: float | None) -> tuple[int, bool]` (the branch, and whether it was chosen among eligible branches), `update(views, instants: int, t_now: float, prior_t_s: float | None) -> int`; attributes `current: int`, `eligible_since: list[float | None]`.

- [ ] **Step 1: Write the failing tests**

Create `training/tests/test_proto_select.py`:

```python
import math

import numpy as np
import pytest

from kz4ap_proto.bank import realized_lengths_s
from kz4ap_proto.fit import Fit
from kz4ap_proto.params import ProtoConfig
from kz4ap_proto.select import BranchView, Selector

CFG = ProtoConfig()
L = realized_lengths_s(CFG, 1500.0)


def fit_for(k, quality, weight=24.0, scale=1.0):
    t = L[k] / 0.8 * scale  # the dit this branch matches, times scale
    return Fit(t, 3.0, 0.0, t, quality, weight)


def views(fits=None, texts=None):
    fits, texts = fits or {}, texts or {}
    return [BranchView(k, float(L[k]), fits.get(k), texts.get(k)) for k in range(len(L))]


def test_a_branch_is_eligible_when_its_fitted_dit_matches_its_length():
    sel = Selector(CFG, L)
    assert sel.eligible(BranchView(15, L[15], fit_for(15, -1.0), None))
    assert sel.eligible(BranchView(15, L[15], fit_for(15, -1.0, scale=1.09), None))
    assert not sel.eligible(BranchView(15, L[15], fit_for(15, -1.0, scale=1.12), None))  # beyond one ladder step
    assert not sel.eligible(BranchView(15, L[15], fit_for(15, -1.0, weight=3.0), None))   # too little memory
    assert not sel.eligible(BranchView(15, L[15], None, None))


def test_the_best_eligible_quality_wins():
    v = views({14: fit_for(14, -1.0), 15: fit_for(15, -0.5), 20: fit_for(20, 0.0, scale=1.5)})
    assert Selector(CFG, L).best(v, None) == (15, True)  # branch 20 fits better but is not eligible


def test_quality_ties_go_to_the_likelier_text_then_to_the_longer_branch():
    sel = Selector(CFG, L)
    fits = {14: fit_for(14, -0.50), 15: fit_for(15, -0.52)}  # within 0.05 nats per element: a tie
    assert sel.best(views(fits, {14: -2.0, 15: -3.5}), None) == (14, True)
    assert sel.best(views(fits, {14: -2.0, 15: -2.05}), None) == (15, True)  # texts tie too: the longer branch
    assert sel.best(views(fits), None) == (15, True)                          # no text: the longer branch


def test_without_an_eligible_branch_text_then_periodicity_then_the_shortest():
    sel = Selector(CFG, L)
    assert sel.best(views(texts={3: -2.0, 20: -4.0}), None) == (3, False)        # the text clearly separates
    nearest = int(np.argmin(np.abs(np.log(L / (0.8 * 0.048)))))
    assert sel.best(views(texts={3: -2.0, 20: -2.5}), 0.048) == (nearest, False)  # no clear text: nearest 0.8 T_P
    assert sel.best(views(), None) == (0, False)


def test_a_switch_needs_m_instants_in_a_row():
    sel = Selector(CFG, L)  # M = 4
    v15 = views({15: fit_for(15, -0.5)})
    v16 = views({16: fit_for(16, -0.5)})
    assert [sel.update(v15, 1, t, None) for t in (1.0, 2.0, 3.0)] == [0, 0, 0]
    assert sel.update(v16, 1, 4.0, None) == 0         # the run is broken
    assert sel.update(v16, 3, 5.0, None) == 16        # three more instants in one update: four in a row
    assert sel.eligible_since[16] == 4.0 and sel.eligible_since[15] is None


def test_fallback_picks_do_not_complete_an_eligible_run():
    # Spec 4.6: the best *eligible* branch for M instants in a row. Branch 16 as a fallback pick (text only)
    # twice, then eligible twice: no switch yet with M = 4.
    sel = Selector(CFG, L)
    fallback16 = views(texts={16: -2.0, 20: -4.0})
    eligible16 = views({16: fit_for(16, -0.5)})
    assert [sel.update(fallback16, 1, t, None) for t in (1.0, 2.0)] == [0, 0]
    assert [sel.update(eligible16, 1, t, None) for t in (3.0, 4.0)] == [0, 0]
    assert sel.update(eligible16, 2, 5.0, None) == 16
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `.venv\Scripts\python -m pytest training/tests/test_proto_select.py -q`
Expected: `ModuleNotFoundError: No module named 'kz4ap_proto.select'`.

- [ ] **Step 3: Implement**

Create `training/kz4ap_proto/select.py`:

```python
"""Branch selection (spec 4.6)."""

from __future__ import annotations

import math
from dataclasses import dataclass

import numpy as np

from .fit import Fit


@dataclass(frozen=True)
class BranchView:
    index: int                  # 0-based ladder position (branch k = index + 1); longer branches have larger indices
    length_s: float             # realized length N_k / r, s
    fit: Fit | None
    text_logprob: float | None  # mean over the branch's recent characters, nats per character


class Selector:
    def __init__(self, cfg, lengths_s):
        self.cfg = cfg
        self.lengths = np.asarray(lengths_s, float)
        self.current = 0
        self.candidate: tuple[int, bool] | None = None  # (branch, chosen among eligible branches)
        self.count = 0
        self.eligible_since: list[float | None] = [None] * len(self.lengths)

    def eligible(self, view: BranchView) -> bool:
        """The branch's own fitted dit agrees with its length: |ln(L_k / (0.8 T_k))| <= ln 1.1, once the fit's
        memory holds min_fit_weight elements."""
        f = view.fit
        return (f is not None and f.weight >= self.cfg.min_fit_weight and f.t_s > 0
                and abs(math.log(view.length_s / (self.cfg.length_dits * f.t_s))) <= self.cfg.eligibility_tolerance)

    def best(self, views, prior_t_s: float | None) -> tuple[int, bool]:
        """(branch, chosen among eligible branches)."""
        cfg = self.cfg
        eligible = [v for v in views if self.eligible(v)]
        if eligible:
            q_best = max(v.fit.quality for v in eligible)
            tied = [v for v in eligible if v.fit.quality >= q_best - cfg.quality_tie_nats]
            texts = [v.text_logprob if v.text_logprob is not None else -math.inf for v in tied]
            if len(tied) > 1 and math.isfinite(max(texts)):
                tied = [v for v, t in zip(tied, texts) if t >= max(texts) - cfg.text_tie_nats]
            return max(v.index for v in tied), True  # the longer branch (better SNR)
        texts = sorted((v.text_logprob, v.index) for v in views if v.text_logprob is not None)
        if len(texts) >= 2 and texts[-1][0] - texts[-2][0] >= cfg.text_separation_nats:
            return texts[-1][1], False
        if prior_t_s is not None:
            return int(np.argmin(np.abs(np.log(self.lengths / (cfg.length_dits * prior_t_s))))), False
        return 0, False

    def update(self, views, instants: int, t_now: float, prior_t_s: float | None) -> int:
        """One more selection (instants: how many selection instants it stands for). A switch needs the same
        other branch best for switch_persistence instants in a row, all as the best eligible branch or all as
        the fallback pick (spec 4.6: "the best eligible one for M marks in a row")."""
        for v in views:
            if self.eligible(v):
                if self.eligible_since[v.index] is None:
                    self.eligible_since[v.index] = t_now
            else:
                self.eligible_since[v.index] = None
        b, from_eligible = self.best(views, prior_t_s)
        if b == self.current:
            self.candidate, self.count = None, 0
        elif self.candidate == (b, from_eligible):
            self.count += instants
        else:
            self.candidate, self.count = (b, from_eligible), instants
        if self.candidate is not None and self.count >= self.cfg.switch_persistence:
            self.current, self.candidate, self.count = self.candidate[0], None, 0
        return self.current
```

- [ ] **Step 4: Run the tests to verify they pass**

Run: `.venv\Scripts\python -m pytest training/tests/test_proto_select.py -q`
Expected: all pass.

- [ ] **Step 5: Commit**

```powershell
git add training/kz4ap_proto/select.py training/tests/test_proto_select.py
```
```powershell
git commit -m "Add branch selection: eligibility, quality, text tie-break, fallbacks, persistence"
```

---

### Task 11: Channel decoder — branches, new overs, re-keying, corrections

Everything per channel (spec §3–4): the bank's outputs, the noise estimate, the keyer, the periodicity estimate, 32 branches each timing its marks and spaces (group delay removed), feeding its fit and decoding its own text; new overs (silence > T_new) with fresh fits and amplitudes and the previous ones as fallback; re-keying of an over's start once W_min of keyed time is in (only keyed samples count while the amplitude is unknown); selection at branch 1's key-ups; and the channel's output with corrections, at most 20 s back. The tests are the Review Focus cases on synthetic baseband streams.

**Files:**
- Create: `training/kz4ap_proto/channel.py`
- Test: `training/tests/test_proto_channel.py`

**Interfaces:**
- Consumes: Tasks 4–10: `bank.boxcar`, `branch_lengths_s`, `branch_samples`; `noise.make_noise`; `keying.BankKeyer`, `edges`, `rekey`; `fit.DurationFit`, `classify_mark`, `classify_space`, `observations_loglik`, `resolution_var_s2`; `periodicity.Periodicity`; `select.BranchView`, `Selector`; `text.TextModel`, `decode_pattern`.
- Produces:
  - `channel.Char(text, start_s, end_s)`, `channel.Correction(t_s, from_s, reach_s, old, new, reason)`
  - `channel.Output(reach_s)`: `append_new(chars)`, `replace_from(from_s, chars, t_s, reason)`, `text() -> str`, `chars`, `corrections`
  - `channel.ChannelResult` (`text: str`, `chars: list[Char]`, `corrections: list[Correction]`, `selections: list[tuple[float, int, float]]` (t s, branch index, its T s or NaN), `periodicity: list[tuple]` (t s, T_P s or NaN, confidence, window s or NaN, per-window [(T or None, score)]), `over_starts: list[float]` (s, the selected branch's), `switches: int`; `to_json() -> dict`)
  - `channel.ChannelDecoder(cfg, rate_hz)`: `run(u: np.ndarray, keep_p1: bool = False) -> ChannelResult` (`ChannelResult.p1`: branch 1's squelched posterior at r, float32, when `keep_p1`)

- [ ] **Step 1: Write the failing tests**

Create `training/tests/test_proto_channel.py`:

```python
import math

import numpy as np
import pytest

from kz4ap_proto.bank import realized_lengths_s
from kz4ap_proto.channel import Char, ChannelDecoder, Output
from kz4ap_proto.params import ProtoConfig
from kz4ap_synth.generate import keying_envelope
from kz4ap_synth.keying import timed_intervals
from kz4ap_synth.morse import keying_intervals

RATE = 1500.0
CFG = ProtoConfig()


def stream(intervals, start_s, duration_s, s500_db, seed):
    """A 1 FS carrier keyed by intervals (5 ms raised-cosine edges) from start_s, in white noise giving S500
    = s500_db: noise power per complex sample 1 / (10^(S500/10) x 500 Hz / r) = 3 x 10^(-S500/10) FS^2."""
    n = int(round(duration_s * RATE))
    rng = np.random.default_rng(seed)
    env = keying_envelope(intervals, start_s, n, int(RATE)) if intervals else np.zeros(n)
    power = 3.0 * 10 ** (-s500_db / 10)
    return env * np.exp(1j * rng.uniform(0, 2 * np.pi)) + (
        rng.standard_normal(n) + 1j * rng.standard_normal(n)) * math.sqrt(power / 2)


def norm(s):
    return " ".join(s.split())


def cer(reference, decoded):
    a, b = reference, decoded
    row = list(range(len(b) + 1))
    for i in range(1, len(a) + 1):
        diag, row[0] = row[0], i
        for j in range(1, len(b) + 1):
            above = row[j]
            row[j] = min(row[j] + 1, row[j - 1] + 1, diag + (a[i - 1] != b[j - 1]))
            diag = above
    return row[len(b)] / len(a)


def run(u):
    return ChannelDecoder(CFG, RATE).run(u)


def test_output_appends_and_corrects_within_the_reach():
    out = Output(20.0)
    out.append_new([Char("A", 1.0, 1.1), Char("B", 2.0, 2.1)])
    out.append_new([Char("A", 1.0, 1.1), Char("B", 2.0, 2.1), Char("C", 3.0, 3.1)])
    assert out.text() == "ABC"
    out.replace_from(2.0, [Char("X", 2.0, 2.1), Char("Y", 3.0, 3.1)], 4.0, "switch")
    assert out.text() == "AXY"
    c = out.corrections[-1]
    assert (c.from_s, c.reach_s, c.old, c.new, c.reason) == (2.0, 2.0, "BC", "XY", "switch")
    out.replace_from(0.0, [Char("Z", 1.0, 1.1)], 30.0, "rekey")  # reaches only 20 s back: from 10 s
    assert out.text() == "AXY" and len(out.corrections) == 1


def test_decodes_a_clean_station():
    iv = keying_intervals("CQ TEST K1ABC K1ABC", 25.0)
    r = run(stream(iv, 1.0, iv[-1][1] + 3.0, 20.0, 1))
    assert norm(r.text) == "CQ TEST K1ABC K1ABC"


def test_decodes_a_station_from_the_first_sample():
    iv = keying_intervals("CQ TEST K1ABC K1ABC", 25.0)
    r = run(stream(iv, 0.0, iv[-1][1] + 3.0, 20.0, 2))
    assert cer("CQ TEST K1ABC K1ABC", norm(r.text)) <= 0.1


def test_slow_first_word_is_right_after_corrections():
    # Regression R2: a 12 WPM station's first dits must not stay read as dahs.
    iv = keying_intervals("HI HI TEST", 12.0)
    r = run(stream(iv, 1.0, iv[-1][1] + 4.0, 20.0, 3))
    assert norm(r.text).startswith("HI HI")


def test_follows_a_speed_step_within_ten_marks():
    # Spec 4.6: a real speed jump (15 -> 30 WPM) must be followed within about 10 marks.
    text = "CQ CQ CQ DE K1ABC K1ABC K1ABC K1ABC"
    iv = timed_intervals(text, 15.0, wpm_end=30.0, profile="step")
    start = 1.0
    step_s = start + iv[28][0]  # words 0-3 (CQ CQ CQ DE) have 28 marks; word 4 is the first at 30 WPM
    r = run(stream(iv, start, iv[-1][1] + start + 3.0, 20.0, 4))
    lengths = realized_lengths_s(CFG, RATE)
    followed = next((t for t, k, _ in r.selections
                     if t > step_s and abs(math.log(lengths[k] / (0.8 * 1.2 / 30.0))) <= math.log(1.1)), None)
    assert followed is not None, "no branch matched to 30 WPM was ever selected after the step"
    marks = sum(1 for a, _ in iv if step_s <= start + a <= followed)
    print(f"followed {followed - step_s:.2f} s and {marks} marks after the step")
    assert marks <= 10


def test_farnsworth_word_gaps_do_not_start_a_new_over():
    iv = timed_intervals("CQ TEST K1ABC", 18.0, farnsworth_wpm=10.0)  # word gaps 1.45 s, T_new 2.5 s
    start = 1.0
    r = run(stream(iv, start, iv[-1][1] + start + 3.0, 20.0, 5))
    assert [t for t in r.over_starts if t < start + iv[-1][1]] == []
    assert norm(r.text) == "CQ TEST K1ABC"


def test_noise_alone_leaves_no_text():
    r = run(stream([], 0.0, 30.0, 20.0, 6))
    assert len(norm(r.text).replace(" ", "")) <= 2
    assert all(c.reach_s <= 20.0 + 1e-9 for c in r.corrections)


def test_two_overs_at_different_speeds():
    a = keying_intervals("CQ DE K1ABC K", 25.0)
    b = keying_intervals("K1ABC DE W9XYZ K", 15.0)
    gap = a[-1][1] + 2.0
    iv = a + [(s + gap, e + gap) for s, e in b]
    r = run(stream(iv, 1.0, iv[-1][1] + 4.0, 20.0, 7))
    assert cer("CQ DE K1ABC K K1ABC DE W9XYZ K", norm(r.text)) <= 0.1


def test_a_tune_up_carrier_does_not_derail_decoding():
    body = keying_intervals("CQ TEST K1ABC K1ABC", 25.0)
    iv = [(0.0, 2.0)] + [(s + 2.5, e + 2.5) for s, e in body]
    r = run(stream(iv, 1.0, iv[-1][1] + 4.0, 20.0, 8))
    assert "TEST K1ABC K1ABC" in norm(r.text)


def test_corrections_never_reach_back_more_than_20_s():
    iv = keying_intervals(" ".join(["CQ TEST K1ABC"] * 8), 20.0)
    r = run(stream(iv, 1.0, iv[-1][1] + 3.0, 8.0, 9))
    assert all(c.reach_s <= 20.0 + 1e-9 for c in r.corrections)
    assert r.to_json()["corrections"] == [c.__dict__ for c in r.corrections]


def test_short_and_empty_streams():
    assert run(np.zeros(0, complex)).text == ""
    assert isinstance(run(stream([], 0.0, 0.3, 20.0, 10)).text, str)


def test_keeps_branch_ones_posterior_for_the_offline_experiments():
    u = stream(keying_intervals("TEST", 25.0), 0.5, 2.0, 20.0, 11)
    r = ChannelDecoder(CFG, RATE).run(u, keep_p1=True)
    assert r.p1.shape == (len(u),) and r.p1.dtype == np.float32 and 0.0 <= r.p1.min() <= r.p1.max() <= 1.0
    assert ChannelDecoder(CFG, RATE).run(u).p1 is None
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `.venv\Scripts\python -m pytest training/tests/test_proto_channel.py -q`
Expected: `ModuleNotFoundError: No module named 'kz4ap_proto.channel'`.

- [ ] **Step 3: Implement**

Create `training/kz4ap_proto/channel.py`:

```python
"""One channel through the filter bank (spec sections 3 and 4): 32 branches, each with its own amplitude,
keying, timing, duration fit and text; the periodicity estimate; branch selection; new overs with
re-keying (spec 4.7); and the channel's text with corrections (spec 4.8)."""

from __future__ import annotations

import math
from dataclasses import asdict, dataclass, field

import numpy as np

from .bank import boxcar, branch_lengths_s, branch_samples
from .fit import DurationFit, classify_mark, classify_space, observations_loglik, resolution_var_s2
from .keying import BankKeyer, edges, rekey
from .noise import make_noise
from .periodicity import Periodicity
from .select import BranchView, Selector
from .text import TextModel, decode_pattern


@dataclass
class Char:
    text: str       # a symbol, "*" for a pattern with no code, or " " for a word space
    start_s: float  # stream time, the branch's group delay removed, s
    end_s: float


@dataclass
class Correction:
    t_s: float      # when it was issued, s
    from_s: float   # the replaced text started here, s
    reach_s: float  # t_s - from_s, s (at most the correction reach)
    old: str
    new: str
    reason: str     # "switch" (branch selection) or "rekey" (an over's first marks re-keyed)


class Output:
    """The channel's text (spec 4.8): the selected branch's characters appended as they are decided, and
    corrections that replace everything from a time on, reaching back at most reach_s."""

    def __init__(self, reach_s: float):
        self.reach_s = reach_s
        self.chars: list[Char] = []
        self.corrections: list[Correction] = []

    def append_new(self, chars) -> None:
        """Appends the characters (in time order) that start after the last one published."""
        last = self.chars[-1].start_s if self.chars else -math.inf
        fresh = []
        for c in reversed(chars):
            if c.start_s <= last:
                break
            fresh.append(c)
        self.chars.extend(reversed(fresh))

    def replace_from(self, from_s: float, chars, t_s: float, reason: str) -> None:
        cut = max(from_s, t_s - self.reach_s)
        old = [c for c in self.chars if c.start_s >= cut]
        new = [c for c in chars if c.start_s >= cut]
        self.chars = [c for c in self.chars if c.start_s < cut] + new
        old_text, new_text = "".join(c.text for c in old), "".join(c.text for c in new)
        if old_text != new_text:
            self.corrections.append(Correction(t_s, cut, t_s - cut, old_text, new_text, reason))

    def text(self) -> str:
        return "".join(c.text for c in self.chars)


class Branch:
    """One branch's marks and spaces, duration fit and text (spec 4.3, 4.5, 4.7)."""

    def __init__(self, index: int, length_s: float, n: int, rate_hz: float, cfg, text_model: TextModel):
        self.index, self.length_s, self.rate, self.cfg = index, length_s, rate_hz, cfg
        self.delay_s = (n - 1) / (2.0 * rate_hz)  # a boxcar's group delay (linear phase), s
        self.text_model = text_model
        self.fit = DurationFit(cfg)
        self.prev_fit: DurationFit | None = None  # the previous over's, until this over's start is decided
        self.current = None                       # the Fit that decodes
        self.chars: list[Char] = []
        self.elements = ""
        self.char_start = 0.0
        self.char_end = 0.0
        self.word_open = False
        self.down_at: float | None = None
        self.up_at: float | None = None           # this over's last key-up (None: none yet)
        self.over_start_n = 0
        self.over_obs: list[tuple[bool, float, float]] = []
        self.marks_in_over = 0

    def time(self, n: int) -> float:
        return n / self.rate - self.delay_s

    def on_edges(self, changes, a: float, prior) -> None:
        """Key changes (sample index, key down after it) with the branch's amplitude over noise a."""
        var_t = resolution_var_s2(self.length_s, a, self.rate)
        for n, down in changes:
            t = self.time(n)
            if down:
                if self.up_at is not None:
                    self._observe(False, t - self.up_at, var_t, prior)
                    self._end_space(t - self.up_at, var_t)
                self.down_at = t
            elif self.down_at is not None:
                self._observe(True, t - self.down_at, var_t, prior)
                self.marks_in_over += 1
                self._add_element(self.down_at, t, var_t)
                self.up_at, self.down_at = t, None

    def _observe(self, is_mark: bool, d: float, var_t: float, prior) -> None:
        self.fit.add(is_mark, d, var_t)
        if self.prev_fit is not None:
            self.prev_fit.add(is_mark, d, var_t)
        self.over_obs.append((is_mark, d, var_t))
        fresh = self.fit.best(*prior)
        if self.prev_fit is None:
            self.current = fresh
            return
        old = self.prev_fit.best(*prior)
        # Until the over's start is decided, the fit that explains this over better decodes (spec 4.7).
        better_old = observations_loglik(old, self.over_obs, self.cfg) > observations_loglik(fresh, self.over_obs, self.cfg)
        self.current = old if better_old else fresh

    def _add_element(self, start: float, end: float, var_t: float, fit=None) -> None:
        if not self.elements:
            self.char_start = start
        self.elements += "-" if classify_mark(fit or self.current, end - start, var_t, self.cfg) else "."
        self.char_end = end

    def _end_space(self, d: float, var_t: float, fit=None) -> None:
        kind = classify_space(fit or self.current, d, var_t, self.cfg)
        if kind != "element":
            self._finish_char()
        if kind == "word":
            self._word_space()

    def _finish_char(self) -> None:
        if self.elements:
            self.chars.append(Char(decode_pattern(self.elements), self.char_start, self.char_end))
            self.elements = ""
            self.word_open = True

    def _word_space(self) -> None:
        if self.word_open:
            self.chars.append(Char(" ", self.char_end, self.char_end))
            self.word_open = False

    def flush(self) -> None:
        self._finish_char()

    def silence_limit_s(self) -> float:
        """T_new = max(new_over_min_s, new_over_gaps x T_g); T_g of the slowest standard speed without a fit."""
        tg = self.current.tg_s if self.current is not None else 1.2 / self.cfg.min_wpm
        return max(self.cfg.new_over_min_s, self.cfg.new_over_gaps * tg)

    def new_over_due(self, t_now: float, key_down: bool) -> bool:
        return (not key_down) and self.up_at is not None and t_now - self.up_at > self.silence_limit_s()

    def start_over(self, n_now: int) -> None:
        """A possible new over (spec 4.7): a fresh fit, the previous over's kept as the fallback."""
        self._finish_char()
        self._word_space()
        if self.prev_fit is None and self.fit.history:
            self.prev_fit = self.fit
        self.fit = DurationFit(self.cfg)
        self.current = self.prev_fit.best() if self.prev_fit is not None else None
        self.over_start_n = n_now
        self.over_obs = []
        self.marks_in_over = 0
        self.down_at = self.up_at = None

    def _observations(self, key, n0: int, var_t: float):
        obs, down_at, up_at = [], None, None
        for n, down in edges(np.asarray(key, bool)[None, :], np.zeros(1, bool), n0)[0]:
            t = self.time(n)
            if down:
                if up_at is not None:
                    obs.append((False, t - up_at, var_t, up_at, t))
                down_at = t
            elif down_at is not None:
                obs.append((True, t - down_at, var_t, down_at, t))
                up_at, down_at = t, None
        return obs, down_at, up_at

    def rekey_over(self, P_stretch, n0: int, sigma2: float, amp_candidates, a_min: float, prior):
        """Re-keys the stretch from sample n0 with the full LLR (spec 4.7): each candidate amplitude s^2 with
        each candidate fit (fresh, and the previous over's continued); the combination whose fit explains the
        re-keyed marks and spaces best (mean log-likelihood per element) wins, and the stretch's characters are
        decoded again with it. Returns (s^2, key state at the end, the stretch's start time s)."""
        from_s = self.time(n0)
        best = None
        for amp2 in amp_candidates:
            key = rekey(P_stretch, sigma2, amp2, self.cfg, a_min)
            var_t = resolution_var_s2(self.length_s, math.sqrt(max(amp2, 0.0) / sigma2), self.rate)
            obs, down_at, up_at = self._observations(key, n0, var_t)
            for base in [None] + ([self.prev_fit] if self.prev_fit is not None else []):
                fit = DurationFit(self.cfg) if base is None else base.copy()
                for is_mark, d, v, _, _ in obs:
                    fit.add(is_mark, d, v)
                result = fit.best(*prior)
                score = observations_loglik(result, obs, self.cfg)
                if best is None or score > best[0]:
                    best = (score, amp2, key, fit, result, obs, down_at, up_at)
        _, amp2, key, fit, result, obs, down_at, up_at = best
        self.fit, self.prev_fit = fit, None
        if result is not None:
            self.current = result
        self.chars = [c for c in self.chars if c.start_s < from_s]
        self.word_open = bool(self.chars) and self.chars[-1].text != " "
        self.elements = ""
        self.over_obs = [o[:3] for o in obs]
        self.marks_in_over = sum(1 for o in obs if o[0])
        if result is not None:
            for is_mark, d, v, start, end in obs:
                if is_mark:
                    self._add_element(start, end, v, result)
                else:
                    self._end_space(d, v, result)
        self.down_at, self.up_at = down_at, up_at
        return amp2, bool(key[-1]) if len(key) else False, from_s

    def text_logprob(self, window: int):
        recent = []
        for c in reversed(self.chars):
            if len(recent) >= window:
                break
            if c.text != " ":
                recent.append(c.text)
        return self.text_model.mean_logprob(recent)


@dataclass
class ChannelResult:
    text: str
    chars: list = field(default_factory=list)
    corrections: list = field(default_factory=list)
    selections: list = field(default_factory=list)   # (t s, branch index, its T s or NaN)
    periodicity: list = field(default_factory=list)  # (t s, T_P s or NaN, confidence, window s or NaN, per window)
    over_starts: list = field(default_factory=list)  # s, the selected branch's
    switches: int = 0
    p1: np.ndarray | None = None                     # branch 1's squelched posterior at r (run(keep_p1=True) only)

    def to_json(self) -> dict:
        nan_to_none = lambda x: None if x is None or (isinstance(x, float) and math.isnan(x)) else round(x, 6)
        return {"text": self.text,
                "chars": [[c.text, round(c.start_s, 4), round(c.end_s, 4)] for c in self.chars],
                "corrections": [asdict(c) for c in self.corrections],
                "selections": [[round(t, 4), k, nan_to_none(T)] for t, k, T in self.selections],
                "periodicity": [[round(t, 4), nan_to_none(T), round(c, 4), nan_to_none(w),
                                 [[nan_to_none(pt), round(ps, 4)] for pt, ps in per]]
                                for t, T, c, w, per in self.periodicity],
                "over_starts": [round(t, 4) for t in self.over_starts],
                "switches": self.switches}


def _char_start_at(chars, t: float) -> float:
    """The start of the character that contains t (or the first after it); t if none."""
    for c in chars:
        if c.start_s <= t <= c.end_s or c.start_s > t:
            return c.start_s
    return t


class ChannelDecoder:
    def __init__(self, cfg, rate_hz: float):
        self.cfg = cfg
        self.rate = rate_hz
        self.n = branch_samples(branch_lengths_s(cfg), rate_hz)
        self.lengths = self.n / rate_hz  # realized lengths, s
        self.text_model = TextModel()

    def run(self, u, keep_p1: bool = False) -> ChannelResult:
        """Decodes one channel's baseband stream u (station at 0 Hz, r samples/s). keep_p1: also return branch
        1's squelched posterior, the periodicity estimator's input, for the offline experiments of Task 13."""
        cfg, rate = self.cfg, self.rate
        p1_blocks = [] if keep_p1 else None
        u = np.asarray(u, np.complex128)
        total = len(u)
        P = np.stack([np.abs(boxcar(u, int(k))) ** 2 for k in self.n]).astype(np.float32)
        noise = make_noise(cfg, rate, self.n)
        keyer = BankKeyer(cfg, rate, self.lengths)
        periodicity = Periodicity(cfg, rate)
        selector = Selector(cfg, self.lengths)
        branches = [Branch(k, float(self.lengths[k]), int(self.n[k]), rate, cfg, self.text_model)
                    for k in range(len(self.n))]
        out = Output(cfg.correction_reach_s)
        result = ChannelResult("")
        block = max(1, int(round(cfg.block_s * rate)))
        reach = int(round(cfg.correction_reach_s * rate))
        prior = (None, 0.0)
        for n0 in range(0, total, block):
            n1 = min(n0 + block, total)
            t_now = n1 / rate
            noise.update(u, P, n0, n1)
            sigma2 = noise.sigma2()
            key, p, before, a = keyer.step(P[:, n0:n1], sigma2)
            periodicity.push(p[0])
            if p1_blocks is not None:
                p1_blocks.append(p[0].astype(np.float32))
            t_p, confidence, window, updated = periodicity.update()
            prior = (t_p, 1.0) if t_p is not None else (None, 0.0)  # the prior counts once T_P is confident
            if updated:
                result.periodicity.append((t_now, t_p if t_p is not None else math.nan, confidence,
                                           window if window is not None else math.nan, list(periodicity.per_window)))
            changes = edges(key, before, n0)
            for k, br in enumerate(branches):
                if changes[k]:
                    br.on_edges(changes[k], float(a[k]), prior)
                if br.new_over_due(t_now, bool(keyer.key[k])):
                    keyer.start_over(k)
                    br.start_over(n1)
                    if k == selector.current:
                        result.over_starts.append(t_now)
                elif keyer.unknown[k] and br.marks_in_over and keyer.weight[k] >= keyer.rekey_weight:
                    # W_min of keyed time since the over started (keyer: only keyed samples count while unknown)
                    s = max(br.over_start_n, n1 - reach)
                    candidates = [float(keyer.amp2[k])]
                    if math.isfinite(keyer.prev_amp2[k]):
                        candidates.append(float(keyer.prev_amp2[k]))
                    amp2, key_now, from_s = br.rekey_over(P[k, s:n1], s, float(sigma2[k]), candidates,
                                                          float(keyer.a_min[k]), prior)
                    keyer.finish_over_start(k, amp2, key_now)
                    if k == selector.current:
                        out.replace_from(from_s, br.chars, t_now, "rekey")
            instants = sum(1 for _, down in changes[0] if not down)
            if instants:
                views = [BranchView(k, br.length_s, br.current, br.text_logprob(cfg.text_window_chars))
                         for k, br in enumerate(branches)]
                old = selector.current
                new = selector.update(views, instants, t_now, prior[0])
                if new != old:
                    result.switches += 1
                    since = selector.eligible_since[new]
                    start = _char_start_at(branches[new].chars, since if since is not None else t_now)
                    out.replace_from(start, branches[new].chars, t_now, "switch")
                current = branches[new].current
                result.selections.append((t_now, new, current.t_s if current is not None else math.nan))
            out.append_new(branches[selector.current].chars)
        for br in branches:
            br.flush()
        if branches:
            out.append_new(branches[selector.current].chars)
        result.text = out.text()
        result.chars = out.chars
        result.corrections = out.corrections
        if p1_blocks is not None:
            result.p1 = np.concatenate(p1_blocks) if p1_blocks else np.zeros(0, np.float32)
        return result
```

- [ ] **Step 4: Run the tests to verify they pass**

Run: `.venv\Scripts\python -m pytest training/tests/test_proto_channel.py -q -s`
Expected: all pass; note the printed "followed … s and … marks after the step" for the results document. These are the first tests of the whole chain, written before any of it ran: when one fails, debug it (superpowers:systematic-debugging) and fix code defects. If the cause is the design or a parameter (for example the step is followed only after 14 marks, or a Farnsworth word gap starts an over because the fit's T_g is too small after the first word), **report the measured numbers to the owner and leave the test failing with a `pytest.mark.xfail(reason=..., strict=True)` naming the finding**; do not change a threshold, a tolerance or a placeholder to make it pass (placeholders are set only by the experiments of Tasks 13–14).

- [ ] **Step 5: Time a keyed channel**

Noise alone is not representative (the branches are squelched and the fit is almost never called), so time a keyed stream: 60 s of 25 WPM filler text at S₅₀₀ = 10 dB.

```powershell
.venv\Scripts\python -c "import time; import numpy as np; from kz4ap_proto.channel import ChannelDecoder; from kz4ap_proto.experiments import stream; from kz4ap_proto.params import ProtoConfig; from kz4ap_synth.messages import random_text; from kz4ap_synth.morse import keying_intervals; iv = [x for x in keying_intervals(random_text(np.random.default_rng(1), 80), 25.0) if x[1] < 59.0]; u = stream(iv, 0.5, 60.0, 10.0, 1); t = time.process_time(); ChannelDecoder(ProtoConfig(), 1500.0).run(u); print(f'{(time.process_time() - t) / 60.0:.3f} s of CPU per channel-second')"
```
(`experiments.stream` is written in Task 13; until then paste its body from Task 13 Step 3 into a scratch file in `build/` and import it from there.) Record the number; the review's estimate from timing the fit alone (1.49 ms per observation, about 10.6 observations per branch per second at 25 WPM) is 0.3–0.5 s of CPU per channel-second. A full pass over the suite's oracle recordings is about 370 000 channel-seconds (340 465 measured for the oracle runs in milestone 2a, results §3.3, plus group I's 26 000) and the development set about 80 000, so a pass takes that number × the channel-seconds / (worker processes). Update the plan's budget (Design decisions, "Compute budget": about 12–14 h of experiments on 14 workers, plus about 2 h for the final pass) with the measured number in the results document; if a development run would exceed about 1 h on 14 workers, report it with a profile of the slowest functions (`.venv\Scripts\python -m cProfile -s cumtime …`) before Task 12. Speed-ups that keep every result bit-identical (vectorizing, caching) are fine; changing a parameter for speed is not.

- [ ] **Step 6: Commit**

```powershell
git add training/kz4ap_proto/channel.py training/tests/test_proto_channel.py
```
```powershell
git commit -m "Add the channel decoder: branches, new overs, re-keying, selection, corrections"
```

---
### Task 11b: Detector-opened channels — recording, scoring by frequency, anchored mixing

*Amendment (owner, 2026-09-30).* Stage 1 extends to the channels the detector opens, so the comparison also covers group H through the detector, the band and crowded groups as the detector sees them, and the late-opening case (a channel that opens partway through a transmission). Only the detector's own measures stay for stage 2. This task adds the tooling; Task 12 runs it over the suite.

- **Recording (C++).** `kz4ap-bench --record-channels DIR` also works without `--oracle`: every channel the detector opens is recorded from its first block, with its track id, center, the detector's frequency at birth, the time (s) and first sample index at which it opened, and its close time if its track died. The engine's `ChannelBlock` gains one field, `anchor_hz`: the detector's current frequency for the track (in oracle mode the labeled frequency), which the engine already sets as the tracker's anchor before each block. It is observation only; the decoding path and the Envelope and Matched outputs stay bit-identical, and `bench/baselines` stay untouched.
- **Scoring (C++).** `--score-decoded` accepts a second file form, `"tracks"`: one entry per track with its id, birth frequency, text and (optional) latest frequency. Such a file is scored exactly as the engine's detector path is: the same `score(..., match_by_order = false)` with the 50 Hz frequency rule, false tracks counted as today, and the same `score_json`/`print_score` in `report.cpp`. No second scoring path. An identity check re-scores the current Matched path's own detector-path text.
- **Mixing (Python), decided here:** a detector channel is mixed by the **detector's frequency for the track, block by block** (the recorded anchors minus the channel's center), with a continuous phase. Why: (1) with no label there is no oracle frequency; (2) the channel's center alone leaves the station up to ±½Δf = ±11.7 Hz off, which through a 40 ms branch costs 10·log₁₀ |sinc(11.7 Hz × 40 ms)|² ≈ −3.4 dB of signal power relative to a centered station *[Corrected 2026-10-02, final review: −3.4 dB, not −2.7 dB as first written: 10·log₁₀ |sin(π·0.468)/(π·0.468)|² = −3.39 dB]*, and more for longer branches (derived) — a handicap the Matched path does not have; (3) the anchor is exactly what the Matched path's NCO starts from and follows (option 1), including the detector following its own peak through a drift or a turnover. The prototype lacks only the tracker's fine-tuning within ±12 Hz of the anchor; the detector's interpolation error is about 0.2 Hz for a clean station (milestone-2a plan, Design decisions B), so the difference is small where the peak is clean and is stated where it is not. The tracker is out of scope for stage 1 (it is unchanged by the redesign; which branch feeds it is stage 2's question).
- **Which detector:** the Matched path's (`--front-end matched`: option 1, distance attribution), because the bank replaces the Matched front end and will sit behind that detector in stage 2. The prototype's detector-path rows are therefore paired with Matched's on **the same tracks**; Envelope's detector path uses bin attribution and opens its own tracks, so the comparison with Envelope there is per label, not per track (stated in the report).

**Files:**
- Modify: `engine/include/kz4ap/engine.hpp`, `engine/src/engine.cpp` (`ChannelBlock::anchor_hz`)
- Modify: `bench/src/channel_recorder.hpp`, `bench/src/channel_recorder.cpp`, `bench/src/main.cpp`, `bench/src/report.hpp`, `bench/src/report.cpp`
- Modify: `training/kz4ap_proto/streams.py`
- Test: `engine/tests/engine_test.cpp`, `bench/tests/channel_recorder_test.cpp`, `bench/tests/report_test.cpp`, `training/tests/test_proto_streams.py`
- Modify: `docs/signal-processing.md` (§6 "Oracle mode", §11: recorded streams and decoded text)

**Interfaces:**
- Consumes: Task 2's `ChannelBlock`, `ChannelTap`, `ChannelRecorder`; Task 3's `DecodedTexts`, `parse_decoded_texts`, `tracks_for`, `score_json`, `print_score`; the bus's `TrackEvent::Kind::Born`/`Died`; Task 4's `streams.ChannelStream`, `read_manifest`.
- Produces:
  - C++: `ChannelBlock::anchor_hz` (double, Hz from the span's center; default 0); `ChannelRecorder::open_track(std::uint32_t track_id, double birth_freq_hz)`, `ChannelRecorder::close_track(std::uint32_t track_id)`; `channels.json` entries gain `birth_freq_hz` (null for oracle channels), `open_s`, `close_s` (null unless the track died), `anchors` ([[first sample index, Hz], …] at every change), and `label_index`/`label_freq_hz` are null for detector channels; `DecodedTexts::detector` (bool) and `DecodedTexts::tracks` (`std::vector<DecodedTrack>`), parsed from `{"front_end", "recording", "tracks": [{"id", "freq_hz", "text", "last_freq_hz"?}]}`.
  - Command line: `kz4ap-bench RECORDING [--labels L] --record-channels DIR` (no `--oracle`: the detector's channels); `kz4ap-bench --labels L --score-decoded D.json` with a `"tracks"` file (scored by frequency).
  - Python: `streams.anchored_baseband(y, rate_hz, center_hz, anchors, first_sample_index=0) -> np.ndarray`; `streams.load_detector_channel(record_dir, position) -> ChannelStream` (`label_index` and `label` None; new fields `track_id`, `center_hz`, `birth_freq_hz`, `open_s`, `close_s`, `anchors`, all defaulted, and `baseband()` uses `anchored_baseband` when `label` is None).

- [ ] **Step 1: Write the failing tests**

Append to `engine/tests/engine_test.cpp`:

```cpp
TEST(Engine, ChannelTapReportsTheAnchorOfEachBlock) {
    // Oracle mode: the anchor is the labeled frequency itself. Detector mode: the detector's current frequency
    // for the track, which the Matched path gives the tracker; a clean station's is within 1 Hz of its carrier.
    const std::string msg = "CQ TEST K1ABC";
    const auto x = keyed_signal(msg, 25, 48000.0, kz4ap::test::duration_for(msg, 25), 12009.0, kAmplitude20dB48k,
                                kNoiseSigma, 42);
    for (const bool oracle : {true, false}) {
        auto config = matched_config(48000);
        if (oracle) config.oracle_frequencies_hz = {12009.0};
        EventBus bus;
        Engine engine(config, bus);
        std::vector<double> anchors;
        engine.set_channel_tap([&](const ChannelBlock& b) { anchors.push_back(b.anchor_hz); });
        engine.process(x);
        engine.finish();
        ASSERT_FALSE(anchors.empty());
        if (oracle) {
            for (const double a : anchors) EXPECT_DOUBLE_EQ(a, 12009.0);
        } else {
            EXPECT_NEAR(anchors.back(), 12009.0, 1.0);
        }
    }
}
```

Append to `bench/tests/channel_recorder_test.cpp`:

```cpp
TEST(ChannelRecorder, RecordsDetectorTracksWithOpeningClosingAndAnchors) {
    const auto dir = fresh_dir("kz4ap_recorder_detector");
    {
        ChannelRecorder rec(dir, 1500.0);
        rec.open_track(7, 1011.0);
        const std::vector<Sample> a(32);
        ChannelBlock b{7, 3000, 1000.0, a};
        b.anchor_hz = 1011.0;
        rec.write(b);
        b.first_index = 3032;
        b.anchor_hz = 1012.5;
        rec.write(b);
        rec.close_track(7);
        EXPECT_THROW(rec.write(b), std::runtime_error);  // a closed track takes no more blocks
        rec.open_track(9, -500.0);                         // opened, never fed: 0 samples
        rec.finish("x.wav", "");
    }
    std::ifstream m(dir / "channels.json");
    const auto j = nlohmann::json::parse(m);
    const auto& c = j.at("channels").at(0);
    EXPECT_EQ(c.at("track_id"), 7);
    EXPECT_TRUE(c.at("label_index").is_null());
    EXPECT_DOUBLE_EQ(c.at("birth_freq_hz").get<double>(), 1011.0);
    EXPECT_EQ(c.at("first_sample_index"), 3000);
    EXPECT_DOUBLE_EQ(c.at("open_s").get<double>(), 2.0);
    EXPECT_DOUBLE_EQ(c.at("close_s").get<double>(), 3064.0 / 1500.0);
    EXPECT_EQ(c.at("samples"), 64);
    EXPECT_EQ(c.at("anchors"), nlohmann::json::parse("[[3000, 1011.0], [3032, 1012.5]]"));
    EXPECT_TRUE(j.at("channels").at(1).at("close_s").is_null());
    std::filesystem::remove_all(dir);
}
```

Append to `bench/tests/report_test.cpp`:

```cpp
TEST(Report, DetectorTracksAreScoredByFrequencyWithFalseTracks) {
    const auto d = parse_decoded_texts(R"({"front_end": "bank-proto", "recording": "x.wav", "tracks": [
        {"id": 3, "freq_hz": 1010.0, "text": "CQ K1ABC", "last_freq_hz": 1001.0},
        {"id": 4, "freq_hz": 2600.0, "text": "EE"},
        {"id": 5, "freq_hz": 2000.0, "text": "TU"}]})");
    ASSERT_TRUE(d.detector);
    ASSERT_EQ(d.tracks.size(), 3u);
    EXPECT_DOUBLE_EQ(d.tracks[0].last_freq_hz, 1001.0);
    EXPECT_DOUBLE_EQ(d.tracks[1].last_freq_hz, 2600.0);  // defaults to the birth frequency
    Labels labels;
    labels.signals = {{"CQ K1ABC", 1000.0, 25, 10, 0, 1}, {"TU", 2000.0, 25, 10, 0, 1}};
    const Score s = score(labels.signals, d.tracks, 50.0, false);  // the engine's detector-path rule
    EXPECT_EQ(s.signals[0].track_id, 3u);
    EXPECT_EQ(s.signals[1].track_id, 5u);
    EXPECT_EQ(s.false_tracks, 1u);                                 // track 4 decoded text and matched nothing
    EXPECT_THROW(parse_decoded_texts(R"({"front_end": "x", "texts": [], "tracks": []})"), std::runtime_error);
    EXPECT_TRUE(parse_decoded_texts(R"({"front_end": "x", "tracks": []})").detector);  // no track opened
}
```

Append to `training/tests/test_proto_streams.py`:

```python
def test_anchored_baseband_follows_the_detector_frequency_with_a_continuous_phase():
    from kz4ap_proto.streams import anchored_baseband
    rate, center = 1500.0, 1000.0
    n = np.arange(3000)
    f = np.where(n < 1500, 9.0, 11.0)                      # the station moves from +9 Hz to +11 Hz off center
    y = np.exp(1j * (2 * np.pi * np.cumsum(f) / rate + 0.4))
    u = anchored_baseband(y, rate, center, [(4000, center + 9.0), (5500, center + 11.0)], first_sample_index=4000)
    assert np.allclose(u, u[0])                            # no rotation left, no phase jump at the change


def test_load_detector_channel_reads_track_opening_and_anchors(tmp_path):
    from kz4ap_proto.streams import load_detector_channel
    y = (np.ones(64) + 0j).astype("<c8")
    y.tofile(tmp_path / "channel-7.c64")
    (tmp_path / "channels.json").write_text(json.dumps({
        "recording": "x.wav", "labels": "", "sample_rate_hz": 1500.0, "format": "complex64",
        "channels": [{"track_id": 7, "label_index": None, "label_freq_hz": None, "birth_freq_hz": 1011.0,
                      "center_hz": 1000.0, "first_sample_index": 3000, "open_s": 2.0, "close_s": None,
                      "samples": 64, "file": "channel-7.c64", "anchors": [[3000, 1011.0]]}]}))
    ch = load_detector_channel(tmp_path, 0)
    assert (ch.track_id, ch.label_index, ch.label, ch.birth_freq_hz, ch.open_s, ch.close_s) == (7, None, None, 1011.0, 2.0, None)
    assert ch.first_sample_index == 3000 and ch.anchors == [(3000, 1011.0)]
    # a DC input mixed down by anchor - center = 11 Hz, phase 0 at the first recorded sample
    assert np.allclose(ch.baseband(), np.exp(-2j * np.pi * 11.0 * np.arange(64) / 1500.0))
```

- [ ] **Step 2: Build and run the tests to verify they fail**

Run: `cmake --build --preset windows` → compile errors (`anchor_hz`, `open_track`, `close_track`, `DecodedTexts::detector` undeclared). `.venv\Scripts\python -m pytest training/tests/test_proto_streams.py -q` → `ImportError: cannot import name 'anchored_baseband'`.

- [ ] **Step 3: Implement the engine field**

In `engine/include/kz4ap/engine.hpp`, add as the last member of `ChannelBlock`:

```cpp
    double anchor_hz = 0;             // the detector's current frequency for the track (oracle: the label), Hz from
                                      // the span's center: the anchor the Matched path gives the tracker
```

In `engine/src/engine.cpp`, change the tap call to `if (tap_) tap_(ChannelBlock{id, first_index, center_hz, s, channel.detector_freq_hz});`. (Aggregate initialization with four values still compiles: the new member has a default.)

- [ ] **Step 4: Implement the recorder's detector mode**

In `bench/src/channel_recorder.hpp`, update the class comment's first sentence to "Writes each recorded channel's stream (kz4ap-bench --record-channels): oracle channels (add_channel) or the channels the detector opens (open_track, close_track)", add after `add_channel`:

```cpp
    // A channel the detector opened (--record-channels without --oracle): no label; birth_freq_hz is the track's
    // frequency at birth, Hz from the span's center. Throws std::runtime_error if the file cannot be opened.
    void open_track(std::uint32_t track_id, double birth_freq_hz);

    // The track died: its file is closed and its close time recorded; later blocks for it throw.
    void close_track(std::uint32_t track_id);
```

and change `struct Channel`'s first two members and add three:

```cpp
        std::optional<std::size_t> label_index;   // oracle channels only
        std::optional<double> label_freq_hz;      // oracle channels only
        std::optional<double> birth_freq_hz;      // detector channels only
        bool closed = false;
        std::vector<std::pair<std::uint64_t, double>> anchors;  // (first sample index, Hz) at every change
```

(add `#include <utility>` and `#include <vector>`). In `bench/src/channel_recorder.cpp`, add:

```cpp
void ChannelRecorder::open_track(std::uint32_t track_id, double birth_freq_hz) {
    Channel c;
    c.birth_freq_hz = birth_freq_hz;
    c.file = "channel-" + std::to_string(track_id) + ".c64";
    c.out.open(dir_ / c.file, std::ios::binary | std::ios::trunc);
    if (!c.out) throw std::runtime_error("cannot open " + (dir_ / c.file).string());
    channels_.emplace(track_id, std::move(c));
}

void ChannelRecorder::close_track(std::uint32_t track_id) {
    const auto it = channels_.find(track_id);
    if (it == channels_.end()) return;  // a track the recorder never saw open (none in practice)
    it->second.closed = true;
    it->second.out.close();
}
```

In `write`, after the unknown-channel check add `if (c.closed) throw std::runtime_error("block for closed channel " + std::to_string(block.track_id));`, and before `c.out.write(...)` add:

```cpp
    if (c.anchors.empty() || c.anchors.back().second != block.anchor_hz) c.anchors.emplace_back(block.first_index, block.anchor_hz);
```

In `finish`, close only open files (`if (c.out.is_open()) { c.out.close(); if (!c.out) throw …; }`) and replace the pushed object with:

```cpp
        const auto first = c.first_index ? *c.first_index : 0;
        nlohmann::json anchors = nlohmann::json::array();
        for (const auto& [index, hz] : c.anchors) anchors.push_back({index, hz});
        j["channels"].push_back({{"track_id", id},
                                 {"label_index", c.label_index ? nlohmann::json(*c.label_index) : nlohmann::json()},
                                 {"label_freq_hz", c.label_freq_hz ? nlohmann::json(*c.label_freq_hz) : nlohmann::json()},
                                 {"birth_freq_hz", c.birth_freq_hz ? nlohmann::json(*c.birth_freq_hz) : nlohmann::json()},
                                 {"center_hz", c.center_hz ? nlohmann::json(*c.center_hz) : nlohmann::json()},
                                 {"first_sample_index", first},
                                 {"open_s", static_cast<double>(first) / rate_},
                                 {"close_s", c.closed ? nlohmann::json(static_cast<double>(first + c.samples) / rate_)
                                                      : nlohmann::json()},
                                 {"samples", c.samples},
                                 {"anchors", anchors},
                                 {"file", c.file}});
```

(Task 2's recorder tests still pass: `label_index` and `label_freq_hz` keep their values for oracle channels.)

In `bench/src/main.cpp`: delete the check `if (args.record_channels && !args.oracle) throw …`; move `std::optional<ChannelRecorder> recorder;` above `bus.subscribe(...)`; in the subscriber, extend the `Born` branch and add a `Died` branch:

```cpp
        if (const auto* t = std::get_if<TrackEvent>(&e); t && t->kind == TrackEvent::Kind::Born) {
            tracks[t->track.id] = {t->track.id, t->track.freq_hz, "", t->track.freq_hz};
            if (recorder && !args.oracle) recorder->open_track(t->track.id, t->track.freq_hz);
        } else if (t && t->kind == TrackEvent::Kind::Died) {
            if (recorder) recorder->close_track(t->track.id);
        } else if (const auto* d = std::get_if<DecodedTextEvent>(&e)) {
```

(restructure so `t` is declared before the `if`: `const auto* t = std::get_if<TrackEvent>(&e);` then the three branches). Replace the recorder block after `Engine engine(config, bus);` with:

```cpp
    if (args.record_channels) {
        recorder.emplace(*args.record_channels, engine.channel_rate());
        if (args.oracle) {  // oracle channels were born in the constructor, before the recorder existed
            for (std::size_t i = 0; i < labels->signals.size(); ++i) {
                recorder->add_channel(static_cast<std::uint32_t>(i + 1), i, labels->signals[i].freq_offset_hz);
            }
        }
        engine.set_channel_tap([&](const ChannelBlock& b) { recorder->write(b); });
    }
```

(Detector tracks are born during `process()`, when the recorder exists; the Born event is published before the channel's first block, `Engine::open_channel`.) And `recorder->finish(args.recording->filename().string(), args.labels ? args.labels->filename().string() : std::string());`. Add `"--record-channels DIR (with --oracle: the oracle channels; without: the detector's)"` to the usage text.

- [ ] **Step 5: Implement the decoded-tracks form**

In `bench/src/report.hpp`, extend `DecodedTexts` and its comment:

```cpp
// Text decoded outside the engine (kz4ap-bench --score-decoded), in one of two forms: {"front_end", "recording",
// "texts": [one per label, in the labels file's order]} (oracle channels, matched by order), or {"front_end",
// "recording", "tracks": [{"id", "freq_hz" (birth), "text", "last_freq_hz" (optional)}]} (detector channels,
// matched by frequency as the engine's detector path is). Other keys are ignored.
struct DecodedTexts {
    std::string front_end;
    std::string recording;
    std::vector<std::string> texts;
    bool detector = false;
    std::vector<DecodedTrack> tracks;
};
```

In `report.cpp`, replace the `d.texts = …` line of `parse_decoded_texts` with:

```cpp
        const bool has_texts = j.contains("texts"), has_tracks = j.contains("tracks");
        if (has_texts == has_tracks) throw std::runtime_error("bad decoded-text file: give either texts or tracks");
        if (has_texts) {
            d.texts = j.at("texts").get<std::vector<std::string>>();
        } else {
            d.detector = true;
            for (const auto& t : j.at("tracks")) {
                const double f = t.at("freq_hz").get<double>();
                d.tracks.push_back({t.at("id").get<std::uint32_t>(), f, t.at("text").get<std::string>(),
                                    t.value("last_freq_hz", f)});
            }
        }
```

In `bench/src/main.cpp`, `score_decoded` becomes:

```cpp
int score_decoded(const Args& args) {
    const Labels labels = load_labels(*args.labels);
    const DecodedTexts decoded = load_decoded_texts(*args.score_decoded);
    // Oracle texts are matched by order; detector tracks by frequency (50 Hz), false tracks counted: the
    // engine's own two rules, through the same score() and score_json().
    const auto tracks = decoded.detector ? decoded.tracks : tracks_for(labels, decoded);
    std::map<std::uint32_t, double> tracked_freq_hz;
    if (decoded.detector) {
        for (const auto& t : tracks) tracked_freq_hz[t.id] = t.last_freq_hz;
    }
    nlohmann::json out;
    out["recording"] = decoded.recording;
    out["duration_s"] = labels.duration_s;
    out["front_end"] = decoded.front_end;
    add_tracks(out, tracks);
    out["channel_seconds"] = 0.0;  // no engine ran
    const int exit_code = score_and_report(labels, tracks, tracked_freq_hz, !decoded.detector, args.baseline, out);
    write_json(args.json, out);
    return exit_code;
}
```

- [ ] **Step 6: Implement anchored mixing and detector channels in Python**

In `training/kz4ap_proto/streams.py`, add after `baseband`:

```python
def anchored_baseband(y, rate_hz: float, center_hz: float, anchors, first_sample_index: int = 0) -> np.ndarray:
    """u[n]: y mixed down by the detector's frequency for the track minus the channel's center, as the engine gave
    it block by block (anchors: [(first sample index, Hz from the span's center)], in order), with a continuous
    phase across changes. This is the anchor the Matched path's NCO starts from and follows (option 1), without
    the tracker's fine-tuning within +/-12 Hz of it (the tracker is out of scope for stage 1)."""
    index = first_sample_index + np.arange(len(y))
    starts = np.array([a[0] for a in anchors], dtype=np.int64)
    offsets = np.array([a[1] for a in anchors], float) - center_hz
    offset = offsets[np.maximum(np.searchsorted(starts, index, side="right") - 1, 0)]
    phase = 2.0 * np.pi * (np.cumsum(offset) - offset) / rate_hz  # the phase before each sample's own advance
    return np.asarray(y, np.complex128) * np.exp(-1j * phase)
```

Add to `ChannelStream` (after `label`, all with defaults so oracle construction is unchanged):

```python
    track_id: int | None = None           # detector channels: the track
    center_hz: float | None = None        # the channel's center, Hz from the span's center
    birth_freq_hz: float | None = None    # the detector's frequency at the track's birth, Hz
    open_s: float | None = None           # when the channel opened, s
    close_s: float | None = None          # when its track died, s (None: open to the end)
    anchors: list | None = None           # [(first sample index, Hz)]: the detector's frequency by block
```

and make `baseband` choose:

```python
    def baseband(self) -> np.ndarray:
        if self.label is None:  # a detector channel: no label, mixed by the detector's frequency
            return anchored_baseband(self.y, self.rate_hz, self.center_hz, self.anchors, self.first_sample_index)
        return baseband(self.y, self.rate_hz, self.f_off_hz, self.drift_hz_per_s, self.start_s,
                        self.first_sample_index)
```

(`label_index: int | None`, `label: dict | None` in the annotations.) Add:

```python
def load_detector_channel(record_dir, position: int) -> ChannelStream:
    """The position-th channel of a detector-path recording directory (kz4ap-bench --record-channels without
    --oracle): no label; it starts where the channel opened."""
    record_dir = Path(record_dir)
    manifest = read_manifest(record_dir)
    ch = manifest["channels"][position]
    y = np.fromfile(record_dir / ch["file"], dtype="<c8").astype(np.complex128)
    if len(y) != ch["samples"]:
        raise ValueError(f"{ch['file']}: {len(y)} samples, the manifest says {ch['samples']}")
    anchors = [(int(i), float(f)) for i, f in ch["anchors"]] or [(int(ch["first_sample_index"]), float(ch["birth_freq_hz"]))]
    return ChannelStream(None, float(manifest["sample_rate_hz"]), y, int(ch["first_sample_index"]), 0.0, 0.0, 0.0,
                         None, track_id=int(ch["track_id"]), center_hz=float(ch["center_hz"] or 0.0),
                         birth_freq_hz=float(ch["birth_freq_hz"]), open_s=float(ch["open_s"]),
                         close_s=None if ch["close_s"] is None else float(ch["close_s"]), anchors=anchors)
```

(If the committed `streams.py` differs in names from Task 4's text, add these beside what is there; the existing oracle functions and tests must not change behavior.)

- [ ] **Step 7: Build and run the tests**

Run: `cmake --build --preset windows`, `ctest --preset windows`, `.venv\Scripts\python -m pytest training -q` → all pass.

- [ ] **Step 8: Check bit-identity and the scoring identity on the detector path**

In Git Bash:

```bash
bash bench/smoke.sh build/windows
build/windows/bench/Release/kz4ap-bench.exe build/windows/smoke/band.wav --labels build/windows/smoke/band.json --front-end matched --no-timing --json build/windows/smoke/detector.json
build/windows/bench/Release/kz4ap-bench.exe build/windows/smoke/band.wav --labels build/windows/smoke/band.json --front-end matched --no-timing --json build/windows/smoke/detector-recorded.json --record-channels build/windows/smoke/detector-channels
PYTHONPATH=training .venv/Scripts/python -c "import json; a = json.load(open('build/windows/smoke/detector.json')); b = json.load(open('build/windows/smoke/detector-recorded.json')); assert a == b, 'recording changed the output'; m = json.load(open('build/windows/smoke/detector-channels/channels.json')); assert len(m['channels']) == len(a['tracks']); json.dump({'front_end': 'matched-copy', 'recording': a['recording'], 'tracks': [{'id': t['id'], 'freq_hz': t['freq_hz'], 'text': t['text']} for t in a['tracks']]}, open('build/windows/smoke/detector-decoded.json', 'w')); print(len(a['tracks']), 'tracks recorded')"
build/windows/bench/Release/kz4ap-bench.exe --labels build/windows/smoke/band.json --score-decoded build/windows/smoke/detector-decoded.json --json build/windows/smoke/detector-rescored.json
PYTHONPATH=training .venv/Scripts/python -c "import json; a = json.load(open('build/windows/smoke/detector.json'))['score']; b = json.load(open('build/windows/smoke/detector-rescored.json'))['score']; [s.pop('tracked_freq_hz') for s in a['signals'] + b['signals']]; assert a == b, 'scores differ'; print('identical')"
```
Expected: `smoke test passed` (Envelope 0.0353 and Matched 0.0436 unchanged, `bench/baselines` untouched); the recorded run's JSON equal to the plain run's (recording changes no output); one recorded channel per track; `identical` (the detector-path text re-scored through `--score-decoded` reproduces the engine's score object; `tracked_freq_hz` differs only because the engine's JSON does not list each track's latest frequency). If `identical` fails, the two paths do not share one rule: fix that, do not paper over it.

- [ ] **Step 9: Document**

In `docs/signal-processing.md` §6, extend the "Oracle mode" paragraph added in Task 2: "Without `--oracle`, `--record-channels` records every channel the detector opens, from its first block to its track's death, with the detector's frequency for the track at every block (`ChannelBlock::anchor_hz`, the Matched path's tracker anchor); the tap still only observes." In §11, extend **Recorded channel streams** with the detector form (track id, birth frequency, open and close times, anchors; `label_index` null) and **Externally decoded text** with the `"tracks"` form: "scored as the engine's detector path is — tracks at their birth frequencies matched to labels within 50 Hz, false tracks counted — by the same code; checked identical on the smoke recording's Matched detector path (apart from `tracked_freq_hz`)."

- [ ] **Step 10: Commit**

```powershell
git add engine/include/kz4ap/engine.hpp engine/src/engine.cpp engine/tests/engine_test.cpp bench/src/channel_recorder.hpp bench/src/channel_recorder.cpp bench/src/main.cpp bench/src/report.hpp bench/src/report.cpp bench/tests/channel_recorder_test.cpp bench/tests/report_test.cpp training/kz4ap_proto/streams.py training/tests/test_proto_streams.py docs/signal-processing.md
```
```powershell
git commit -m "Record and score the channels the detector opens, with the detector's frequency by block"
```

---
### Task 12: Runner — record, decode, score, report

Stage 1's pipeline over `build/suite/full3` (spec §7 item 5): record every oracle scoring's channel streams with the bench (Task 2), decode each channel with the prototype in parallel worker processes, score the text with `kz4ap-bench --score-decoded` (Task 3) into `results/bank-proto/`, and report against the Matched and Envelope results already in `results/` — reusing `suites.load_results`, `aggregate`, `paired_differences` and the bootstrap. The report is the full comparison the owner decides on (owner, 2026-09-30: no acceptance gate): group A crossings at each speed, R1, R2 and the start-up runaway cases, and every regime better, worse or unchanged beyond its interval against Matched and against Envelope, with the prototype's own speed, switching, over-start and false-character statistics. **Every regime is covered:** the detector-only groups (pauses, strong, tune-up, first sample, band, crowded) are scored again with oracle channels from the existing recordings — by Envelope and the current Matched path (`kz4ap-bench --oracle`) and by the prototype — as `<recording>.oracle` in groups `<group>, oracle`. **And through the detector** (Task 11b; owner, 2026-09-30): every non-oracle recording (group H through the detector, band, crowded, pauses, strong, tune-up, first sample) is recorded once more with the Matched path's detector (`--front-end matched`, no `--oracle`); the prototype decodes each channel the detector opened, from its opening, mixed by the detector's frequency block by block; its tracks are scored with `--score-decoded` by frequency under the very result names the engine's detector-path runs have (`H-qso-s1`, `H-qso-s1.stations`, `band-s1`, …), so its rows pair signal by signal with Matched's and Envelope's existing detector-path results. That covers the late-opening case too. **How the detector path compares** (controller ruling, 2026-09-30): the channels are the Matched path's detector's (option 1, D_ch = 47 Hz), the detector the redesign will sit behind, so the prototype is compared with Matched **per track** (the same tracks) and with Envelope **per label** (Envelope's bin-attribution detector opens its own tracks). The report also gives, for the prototype, Matched and Envelope side by side, the bench's **detection recall, false tracks and tracks per QSO** on the detector path: as the bench counts them (a track counts only if it decoded text) they depend on the decoder, so stage 1 measures them; stage 2 confirms them in C++ behind the live detector with the frequency tracker. Which tracks the detector opens is unchanged by the redesign. Experiment runs (Tasks 13–14) go to `experiments/results/`, so the suite's own summary is not cluttered.

**Files:**
- Modify: `training/kz4ap_synth/suites.py` (`ORACLE_COPY_GROUPS`, `oracle_copies(rec)`, `load_results(out_dir, results_dirs=None, front_ends=None)` reading the copies, `paired_differences(rows, a="baseline", b="matched")`)
- Create: `training/kz4ap_proto/runner.py`, `metrics.py`, `report.py`
- Test: `training/tests/test_suites.py`, `training/tests/test_proto_runner.py`, `training/tests/test_proto_metrics.py`

**Interfaces:**
- Consumes: Task 11b's `--record-channels` without `--oracle`, the `"tracks"` form of `--score-decoded`, and `streams.load_detector_channel`; `suites._scorings`, `load_results`, `aggregate`, `paired_differences`, `bootstrap_cer`, `_interval`, `_rng_for`, `_tag_offset_hz`, `BOOTSTRAP_RESAMPLES`; `streams.load_channel`, `read_manifest`; `channel.ChannelDecoder`; `periodicity.Periodicity`; `ProtoConfig`; the bench's `--record-channels` and `--score-decoded`.
- Produces:
  - `suites.ORACLE_COPY_GROUPS = ("pauses", "strong", "tune-up", "first sample", "band", "crowded")`; `suites.oracle_copies(rec) -> list[tuple[str, str, str]]` ((labels, `<name>.oracle`, `<group>, oracle`) for a detector-only recording of those groups, else []); `suites.load_results(out_dir, results_dirs: list[Path] | None = None, front_ends: list[str] | None = None)` (also reads the copies' results, as oracle rows); `suites.paired_differences(rows, a="baseline", b="matched")` (b − a; defaults unchanged)
  - `runner.DEFAULT_NAME = "bank-proto"`; `runner.oracle_scorings(out_dir, only=None) -> list[dict]` (keys `result`, `group`, `wav`, `labels`; the oracle recordings' scorings and the oracle copies); `runner.detector_jobs(out_dir, only=None) -> list[dict]` (keys `result` = `<name>.detector`, `wav`, `labels`, `scorings`: the recording's (labels, result, group) scorings; one per non-oracle recording, filtered by `only` on the scorings' result names); `record`, `decode` and `score` also handle the detector jobs (files `channels/<name>.detector/`, `proto/<name>/<name>.detector.decoded.json` in the `"tracks"` form, `results/<name>/<result>.json` for each scoring); `runner.score_engine_copies(out_dir, bench, front_ends=("baseline", "matched"), only=None)` (CLI `engine-copies`); `runner.record(out_dir, bench, only=None)`; `runner.decode(out_dir, name=DEFAULT_NAME, values=None, only=None, jobs=None, keep_p1=False)`; `runner.score(out_dir, bench, name=DEFAULT_NAME, only=None, results_root=None)`; CLI `record | decode | score | report`.
  - Files: `channels/<result>/` (Task 2's format), `proto/<name>/<result>.decoded.json` (`front_end`, `recording`, `labels`, `config`, `texts`, `channels`: each channel's `ChannelResult.to_json()` plus `label_index`, `rate_hz`, `cpu_s`, `channel_s`), `proto/<name>/p1/<result>/<label index>.npy` (with `keep_p1`), `results/<name>/<result>.json` (the bench's JSON).
  - `metrics.iter_channels(out_dir, name, only=None)` (yields `(job, label, channel, config)`), `metrics.true_dit_s(label)`, `metrics.transmissions(label, pad_s=0.0)`, `metrics.bootstrap_ratio(units, key)`, `metrics.speed_errors(...)`, `metrics.switch_stats(...)`, `metrics.spurious_over_starts(...)`, `metrics.false_characters(...)`, `metrics.cpu_per_channel_second(...)`, `metrics.periodicity_points(out_dir, name, cfg, only=None, groups=..., min_snr_db=0.0, jobs=None)`, `metrics.evaluate_rule(points, windows_s, subset, threshold)`, `metrics.calibrate(points, windows_s, subset, target=0.95)`.
  - `report.RUNAWAY_CASES`, `report.DETECTOR_PATH_GROUPS`, `report.detector_measures(out_dir, name, results_dirs=None, only=None) -> dict` (per front end and detector-path group: `scored`, `detected`, `detection_recall`, `false_tracks`, `recordings`; and `tracks_per_qso` from `suites.track_splits`), `report.comparable_rows(out_dir, name, results_dirs=None, only=None)`, `report.not_comparable(group, tag) -> str | None`, `report.verdict(interval) -> str`, `report.comparison(rows, name, out_dir, results_dirs=None) -> dict`, `report.DETECTOR_GAP` (the stated gap), `report.write_report(out_dir, name, results_dirs=None, only=None, suffix="") -> Path`.

- [ ] **Step 1: Write the failing tests**

Append to `training/tests/test_suites.py`:

```python
def test_paired_differences_compare_any_two_front_ends():
    rows = [_row(0.0, 100, 10, front_end="matched", index=i) for i in range(4)]
    rows += [_row(0.0, 100, 20, front_end="bank-proto", index=i) for i in range(4)]
    d = paired_differences(rows, "matched", "bank-proto")[("A", "25 wpm")]
    assert d["mean"] == pytest.approx(0.10)


def test_detector_only_groups_get_oracle_copies_that_load_as_oracle_rows(tmp_path):
    from kz4ap_synth.suites import ORACLE_COPY_GROUPS, load_results, oracle_copies
    rec = {"name": "pauses-s1", "group": "pauses", "oracle": False, "labels": "pauses-s1.json"}
    assert oracle_copies(rec) == [("pauses-s1.json", "pauses-s1.oracle", "pauses, oracle")]
    assert oracle_copies({**rec, "group": "A sensitivity", "oracle": True}) == []
    assert oracle_copies({**rec, "group": "H two-station QSO"}) == []  # group H has its own oracle copy
    assert set(ORACLE_COPY_GROUPS) == {"pauses", "strong", "tune-up", "first sample", "band", "crowded"}
    spec = Recording("pauses-s1", "pauses", 8000, 3.0, 5, False, [SignalSpec("CQ", 1000.0, 25.0, 10.0, 0.5)])
    write_suite([spec], tmp_path, "test")
    (tmp_path / "results" / "matched").mkdir(parents=True)
    for result in ("pauses-s1", "pauses-s1.oracle"):
        (tmp_path / "results" / "matched" / f"{result}.json").write_text(
            json.dumps({"score": {"signals": [_fake_signal(0)]}}))
    rows, _ = load_results(tmp_path)
    assert sorted((r["recording"], r["group"]) for r in rows) == [("pauses-s1", "pauses"),
                                                                  ("pauses-s1.oracle", "pauses, oracle")]


def test_load_results_reads_the_given_directories_and_front_ends(tmp_path):
    rec = Recording("tiny", "A sensitivity", 8000, 3.0, 5, True, [SignalSpec("CQ", 1000.0, 25.0, 10.0, 0.5)])
    write_suite([rec], tmp_path, "test")
    for root, fe in ((tmp_path / "results", "baseline"), (tmp_path / "experiments" / "results", "exp-a")):
        (root / fe).mkdir(parents=True)
        (root / fe / "tiny.json").write_text(json.dumps({"score": {"signals": [_fake_signal(0)]}}))
    from kz4ap_synth.suites import load_results
    assert {r["front_end"] for r in load_results(tmp_path)[0]} == {"baseline"}
    rows, _ = load_results(tmp_path, [tmp_path / "results", tmp_path / "experiments" / "results"], ["exp-a"])
    assert {r["front_end"] for r in rows} == {"exp-a"}
```

Create `training/tests/test_proto_runner.py`:

```python
import json
import subprocess

import numpy as np

from kz4ap_proto import runner


def manifest(tmp_path, recordings):
    (tmp_path / "manifest.json").write_text(json.dumps({"suite": "t", "recordings": recordings}))


def rec(name, oracle, stations=None):
    return {"name": name, "group": "A sensitivity", "oracle": oracle, "wav": f"{name}.wav", "labels": f"{name}.json",
            "station_labels": stations}


def test_record_runs_the_bench_on_every_oracle_scoring(tmp_path, monkeypatch):
    manifest(tmp_path, [rec("a", True, "a.stations.json"), rec("b", False)])
    calls = []
    monkeypatch.setattr(subprocess, "run", lambda cmd, **k: calls.append(cmd) or subprocess.CompletedProcess(cmd, 0, "", ""))
    runner.record(tmp_path, tmp_path / "kz4ap-bench")
    oracle = [c for c in calls if "--oracle" in c]
    assert [c[c.index("--record-channels") + 1] for c in oracle] == [
        str(tmp_path / "channels" / "a"), str(tmp_path / "channels" / "a.stations")]
    assert all(c[c.index("--front-end") + 1] == "envelope" for c in oracle)
    # b is not an oracle recording: its detector channels are recorded once (Task 11b)
    assert [c[c.index("--record-channels") + 1] for c in calls if "--oracle" not in c] == [
        str(tmp_path / "channels" / "b.detector")]


def test_decode_writes_one_text_per_label_in_label_order(tmp_path):
    manifest(tmp_path, [rec("a", True)])
    (tmp_path / "a.json").write_text(json.dumps({"signals": [
        {"text": "E", "start_s": 0.5, "end_s": 1.0, "wpm": 25.0, "snr_db": 10.0},
        {"text": "T", "start_s": 0.5, "end_s": 1.0, "wpm": 25.0, "snr_db": 10.0}]}))
    record_dir = tmp_path / "channels" / "a"
    record_dir.mkdir(parents=True)
    rng = np.random.default_rng(1)
    channels = []
    for i in (1, 2):
        y = ((rng.standard_normal(3000) + 1j * rng.standard_normal(3000)) * 0.1).astype("<c8")
        y.tofile(record_dir / f"channel-{i}.c64")
        channels.append({"track_id": i, "label_index": i - 1, "label_freq_hz": 0.0, "center_hz": 0.0,
                         "first_sample_index": 0, "samples": 3000, "file": f"channel-{i}.c64"})
    (record_dir / "channels.json").write_text(json.dumps({"sample_rate_hz": 1500.0, "channels": channels}))
    runner.decode(tmp_path, "t-proto", jobs=1, keep_p1=True)
    decoded = json.loads((tmp_path / "proto" / "t-proto" / "a.decoded.json").read_text())
    assert decoded["front_end"] == "t-proto" and len(decoded["texts"]) == 2
    assert [c["label_index"] for c in decoded["channels"]] == [0, 1]
    assert decoded["channels"][0]["channel_s"] == 2.0 and decoded["channels"][0]["rate_hz"] == 1500.0
    assert np.load(tmp_path / "proto" / "t-proto" / "p1" / "a" / "1.npy").shape == (3000,)


def test_oracle_copies_are_recorded_and_scored_by_the_engine_front_ends(tmp_path, monkeypatch):
    manifest(tmp_path, [rec("a", True), {**rec("p", False), "group": "pauses"}, {**rec("h", False), "group": "H two-station QSO"}])
    assert [j["result"] for j in runner.oracle_scorings(tmp_path)] == ["a", "p.oracle"]
    calls = []
    monkeypatch.setattr(subprocess, "run", lambda cmd, **k: calls.append(cmd) or subprocess.CompletedProcess(cmd, 0, "CER 0\n", ""))
    runner.score_engine_copies(tmp_path, tmp_path / "kz4ap-bench")
    assert [(c[c.index("--front-end") + 1], c[c.index("--json") + 1]) for c in calls] == [
        ("envelope", str(tmp_path / "results" / "baseline" / "p.oracle.json")),
        ("matched", str(tmp_path / "results" / "matched" / "p.oracle.json"))]
    assert all("--oracle" in c and c[1] == str(tmp_path / "p.wav") for c in calls)


def test_detector_path_is_recorded_decoded_as_tracks_and_scored_under_the_engine_names(tmp_path, monkeypatch):
    manifest(tmp_path, [rec("a", True), {**rec("h", False, "h.stations.json"), "group": "H two-station QSO"}])
    jobs = runner.detector_jobs(tmp_path)
    assert [(j["result"], [s[1] for s in j["scorings"]]) for j in jobs] == [("h.detector", ["h", "h.stations"])]
    calls = []
    monkeypatch.setattr(subprocess, "run", lambda cmd, **k: calls.append(cmd) or subprocess.CompletedProcess(cmd, 0, "CER 0\n", ""))
    runner.record(tmp_path, tmp_path / "kz4ap-bench", only="^h")
    (cmd,) = calls
    assert "--oracle" not in cmd and cmd[cmd.index("--front-end") + 1] == "matched"
    assert cmd[cmd.index("--record-channels") + 1] == str(tmp_path / "channels" / "h.detector")
    # a detector recording with one track, decoded as a "tracks" file
    record_dir = tmp_path / "channels" / "h.detector"
    record_dir.mkdir(parents=True)
    y = ((np.random.default_rng(2).standard_normal(3000) + 0j) * 0.1).astype("<c8")
    y.tofile(record_dir / "channel-4.c64")
    (record_dir / "channels.json").write_text(json.dumps({"sample_rate_hz": 1500.0, "channels": [
        {"track_id": 4, "label_index": None, "label_freq_hz": None, "birth_freq_hz": 1011.0, "center_hz": 1000.0,
         "first_sample_index": 1500, "open_s": 1.0, "close_s": 3.0, "samples": 3000, "file": "channel-4.c64",
         "anchors": [[1500, 1011.0]]}]}))
    runner.decode(tmp_path, "t-proto", only="^h", jobs=1)
    decoded = json.loads((tmp_path / "proto" / "t-proto" / "h.detector.decoded.json").read_text())
    assert "texts" not in decoded and [(t["id"], t["freq_hz"], t["last_freq_hz"]) for t in decoded["tracks"]] == [
        (4, 1011.0, 1011.0)]
    calls.clear()
    runner.score(tmp_path, tmp_path / "kz4ap-bench", "t-proto", only="^h")
    assert [(c[c.index("--labels") + 1], c[c.index("--json") + 1]) for c in calls] == [
        (str(tmp_path / "h.json"), str(tmp_path / "results" / "t-proto" / "h.json")),
        (str(tmp_path / "h.stations.json"), str(tmp_path / "results" / "t-proto" / "h.stations.json"))]


def test_detector_measures_sum_the_bench_counts_per_front_end(tmp_path):
    from kz4ap_proto.report import detector_measures
    manifest(tmp_path, [{**rec("band-s1", False), "group": "band"}, rec("a", True)])
    for fe, (scored, detected, false) in (("bank-proto", (20, 19, 2)), ("matched", (20, 18, 3)), ("baseline", (20, 17, 5))):
        (tmp_path / "results" / fe).mkdir(parents=True)
        (tmp_path / "results" / fe / "band-s1.json").write_text(json.dumps(
            {"tracks": [], "score": {"scored": scored, "detected": detected, "false_tracks": false, "signals": []}}))
    m = detector_measures(tmp_path, "bank-proto")["by_front_end"]
    assert m["bank-proto"]["band"] == {"recordings": 1, "scored": 20, "detected": 19, "false_tracks": 2,
                                       "detection_recall": 0.95}
    assert m["baseline"]["band"]["false_tracks"] == 5 and "A sensitivity" not in m["matched"]


def test_score_hands_each_decoded_file_to_the_bench(tmp_path, monkeypatch):
    manifest(tmp_path, [rec("a", True)])
    (tmp_path / "proto" / "t-proto").mkdir(parents=True)
    (tmp_path / "proto" / "t-proto" / "a.decoded.json").write_text("{}")
    calls = []
    monkeypatch.setattr(subprocess, "run",
                        lambda cmd, **k: calls.append(cmd) or subprocess.CompletedProcess(cmd, 0, "CER 0.1\n", ""))
    runner.score(tmp_path, tmp_path / "kz4ap-bench", "t-proto")
    (cmd,) = calls
    assert cmd[cmd.index("--score-decoded") + 1] == str(tmp_path / "proto" / "t-proto" / "a.decoded.json")
    assert cmd[cmd.index("--json") + 1] == str(tmp_path / "results" / "t-proto" / "a.json")
```

Create `training/tests/test_proto_metrics.py`:

```python
import json
import math

import pytest

from kz4ap_proto import metrics


def suite(tmp_path, label, channel, windows=(2.0, 5.0, 10.0)):
    (tmp_path / "manifest.json").write_text(json.dumps({"suite": "t", "recordings": [
        {"name": "a", "group": "A sensitivity", "oracle": True, "wav": "a.wav", "labels": "a.json",
         "station_labels": None}]}))
    (tmp_path / "a.json").write_text(json.dumps({"signals": [label]}))
    (tmp_path / "proto" / "p").mkdir(parents=True)
    (tmp_path / "proto" / "p" / "a.decoded.json").write_text(json.dumps({
        "front_end": "p", "config": {"periodicity_windows_s": list(windows)}, "texts": [channel["text"]],
        "channels": [{"label_index": 0, "rate_hz": 1500.0, "cpu_s": 1.0, "channel_s": 60.0, **channel}]}))


LABEL = {"text": "CQ", "wpm": 25.0, "snr_db": 10.0, "start_s": 0.0, "end_s": 30.0, "score": True,
         "transmissions": [{"text": "CQ", "start_s": 0.0, "end_s": 30.0}]}


def test_speed_errors_and_lock_ins(tmp_path):
    right, wrong = 0.048, 0.096
    selections = [[t, 15, right] for t in range(4, 10)] + [[t, 25, wrong] for t in range(10, 15)]
    suite(tmp_path, LABEL, {"text": "CQ", "selections": selections, "over_starts": [], "chars": [], "periodicity": []})
    s = metrics.speed_errors(tmp_path, "p")["A sensitivity"]
    assert s["instants"] == 11 and s["error_fraction"] == pytest.approx(5 / 11) and s["lock_ins"] == 1


def test_switches_alternations_over_starts_and_false_characters(tmp_path):
    selections = [[1.0, 0, None], [2.0, 5, 0.03], [3.0, 0, None], [20.0, 7, 0.05]]
    chars = [["E", 31.0, 31.05], ["T", 10.0, 10.1], [" ", 31.1, 31.1]]
    suite(tmp_path, LABEL, {"text": "E T", "selections": selections, "over_starts": [0.5, 12.0], "chars": chars,
                            "periodicity": []})
    sw = metrics.switch_stats(tmp_path, "p")["A sensitivity"]
    assert sw["switches"] == 3 and sw["alternations"] == 1
    assert sw["switches_per_min"] == pytest.approx(3 / 0.5)
    assert metrics.spurious_over_starts(tmp_path, "p")["A sensitivity"]["per_transmission"] == pytest.approx(1.0)
    fc = metrics.false_characters(tmp_path, "p")["A sensitivity"]
    assert fc["characters"] == 1 and fc["per_min"] == pytest.approx(1 / ((60.0 - 30.5) / 60.0))
    assert metrics.cpu_per_channel_second(tmp_path, "p") == pytest.approx(1 / 60)


def test_the_shortest_confident_window_rule_is_scored_against_the_true_dit():
    truth = 0.048
    points = [{"unit": ("a", 0), "tx": 0, "truth": truth, "since_start": float(t),
               "per": [(None, 0.0) if t < 2 else (0.1, 0.02), (0.049, 0.3), (0.048, 0.4)]} for t in range(10)]
    r = metrics.evaluate_rule(points, (2.0, 5.0, 10.0), (2.0, 5.0, 10.0), 0.05)
    assert r["confident"] == 10 and r["precision"] == pytest.approx(1.0) and r["coverage"] == pytest.approx(1.0)
    r = metrics.evaluate_rule(points, (2.0, 5.0, 10.0), (2.0, 5.0, 10.0), 0.01)
    assert r["precision"] == pytest.approx(2 / 10)  # the 2 s window's wrong estimates now count as confident
    threshold, best = metrics.calibrate(points, (2.0, 5.0, 10.0), (2.0, 5.0, 10.0), 0.95)
    assert threshold > 0.02 and best["precision"] >= 0.95
    assert metrics.true_dit_s({**LABEL, "wpm_end": 35.0}) is None
    assert metrics.true_dit_s({**LABEL, "senders": [{"wpm": 20.0}, {"wpm": 25.0}]}) is None
    assert metrics.true_dit_s(LABEL) == pytest.approx(0.048)
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `.venv\Scripts\python -m pytest training/tests/test_suites.py training/tests/test_proto_runner.py training/tests/test_proto_metrics.py -q`
Expected: `TypeError` for the extra arguments of `paired_differences` and `load_results`; `ImportError: cannot import name 'runner'` / `'metrics'`.

- [ ] **Step 3: Generalize `suites.load_results` and `suites.paired_differences`**

In `training/kz4ap_synth/suites.py`, change `paired_differences`'s head and the two lookups:

```python
def paired_differences(rows, a: str = "baseline", b: str = "matched") -> dict:
    """b minus a CER, signal by signal on the same recordings, pooled by (group, tag): mean difference and
    its bootstrap 95% interval over signals (defaults: Matched minus baseline)."""
    by_front_end: dict = {}
    for r in rows:
        if r["scored"]:
            by_front_end.setdefault(r["front_end"], {})[(r["group"], r["tag"], r["recording"], r["index"])] = r
    base, matched = by_front_end.get(a, {}), by_front_end.get(b, {})
```

(the rest of the function is unchanged; `matched` now names front end b). Add, after `_scorings`:

```python
# The detector-only groups, scored again with oracle channels for the filter-bank prototype (milestone 2b,
# stage 1; owner, 2026-09-30), so every regime is compared like for like. Group H has its own oracle copy.
ORACLE_COPY_GROUPS = ("pauses", "strong", "tune-up", "first sample", "band", "crowded")


def oracle_copies(rec: dict) -> list[tuple[str, str, str]]:
    """(labels file, result name, group) of a detector-only recording's oracle copy, or []."""
    if rec["oracle"] or rec["group"] not in ORACLE_COPY_GROUPS:
        return []
    return [(rec["labels"], f"{rec['name']}.oracle", f"{rec['group']}, oracle")]
```

Change `load_results`'s head and its directory loop:

```python
def load_results(out_dir: Path, results_dirs=None, front_ends=None):
    """Per-signal rows and per-recording timings from every front-end directory in results_dirs (default:
    out_dir/results), or only those named in front_ends."""
    manifest = json.loads((out_dir / "manifest.json").read_text())
    rows, timings = [], []
    roots = results_dirs if results_dirs is not None else [out_dir / "results"]
    fe_dirs = sorted((p for root in roots if root.exists() for p in root.iterdir() if p.is_dir()), key=lambda p: p.name)
    for fe_dir in fe_dirs:
        if front_ends is not None and fe_dir.name not in front_ends:
            continue
```

and in the loop body replace `for label_file, result_name, group in _scorings(rec):` with

```python
            scorings = [(s, rec["oracle"]) for s in _scorings(rec)] + [(s, True) for s in oracle_copies(rec)]
            for (label_file, result_name, group), oracle in scorings:
```

and use `oracle` in place of `rec["oracle"]` in the two calls inside it (`_row_regime` and `beyond_oracle_anchor`); the rest is unchanged. (Nothing is written for the copies until a results file exists, so the suite's own summaries are unchanged until Task 12 Step 8 runs.)

- [ ] **Step 4: Implement the runner**

Create `training/kz4ap_proto/runner.py`:

```python
"""Stage 1 runner (plan Task 12): record the suite's oracle channel streams with kz4ap-bench, decode them with
the prototype, score the decoded text with kz4ap-bench --score-decoded, and report against the engine's
results in the suite's results/ directory.

    python -m kz4ap_proto.runner record --out build/suite/full3 --bench PATH/kz4ap-bench [--only REGEX]
    python -m kz4ap_proto.runner engine-copies --out build/suite/full3 --bench PATH/kz4ap-bench [--only REGEX]
    python -m kz4ap_proto.runner decode --out build/suite/full3 [--name bank-proto] [--set KEY=VALUE ...] [--only REGEX] [--jobs N]
    python -m kz4ap_proto.runner score --out build/suite/full3 --bench PATH/kz4ap-bench [--name bank-proto] [--only REGEX]
    python -m kz4ap_proto.runner report --out build/suite/full3 [--name bank-proto] [--only REGEX] [--suffix TEXT]
"""

from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import time
from concurrent.futures import ProcessPoolExecutor
from dataclasses import asdict
from pathlib import Path

import numpy as np

from kz4ap_synth.suites import BENCH_FRONT_END, _scorings, oracle_copies

from .channel import ChannelDecoder
from .params import ProtoConfig
from .streams import load_channel, load_detector_channel, read_manifest

DEFAULT_NAME = "bank-proto"


def oracle_scorings(out_dir: Path, only: str | None = None) -> list[dict]:
    """Every (recording, labels file) scored with oracle channels: the oracle recordings' scorings, and the oracle
    copies of the detector-only groups (suites.oracle_copies)."""
    manifest = json.loads((Path(out_dir) / "manifest.json").read_text())
    jobs = []
    for rec in manifest["recordings"]:
        for label_file, result, group in (_scorings(rec) if rec["oracle"] else oracle_copies(rec)):
            if only is None or re.search(only, result):
                jobs.append({"result": result, "group": group, "wav": rec["wav"], "labels": label_file})
    return jobs


def detector_jobs(out_dir: Path, only: str | None = None) -> list[dict]:
    """One job per non-oracle recording: its channels are the ones the Matched path's detector opens
    (kz4ap-bench --record-channels without --oracle; Task 11b), recorded once and scored under each of the
    recording's scorings (labels, and station labels where given), whose result names match the engine's own
    detector-path results."""
    manifest = json.loads((Path(out_dir) / "manifest.json").read_text())
    jobs = []
    for rec in manifest["recordings"]:
        if rec["oracle"]:
            continue
        scorings = [s for s in _scorings(rec) if only is None or re.search(only, s[1])]
        if scorings:
            jobs.append({"result": f"{rec['name']}.detector", "wav": rec["wav"], "labels": rec["labels"],
                         "scorings": scorings})
    return jobs


def record(out_dir: Path, bench: Path, only: str | None = None) -> None:
    """Records the oracle scorings' channels (--oracle) and the detector's channels of every non-oracle recording
    (the Matched path's detector: --front-end matched, no --oracle)."""
    out_dir = Path(out_dir)
    for job in detector_jobs(out_dir, only):
        target = out_dir / "channels" / job["result"]
        if (target / "channels.json").exists():
            continue
        cmd = [str(bench), str(out_dir / job["wav"]), "--labels", str(out_dir / job["labels"]),
               "--front-end", "matched", "--no-timing", "--record-channels", str(target),
               "--json", str(target / "bench.json")]
        done = subprocess.run(cmd, capture_output=True, text=True)
        if done.returncode != 0:
            raise RuntimeError(f"kz4ap-bench failed recording {job['result']}:\n{done.stderr}")
        print(f"recorded {job['result']}")
    for job in oracle_scorings(out_dir, only):
        target = out_dir / "channels" / job["result"]
        if (target / "channels.json").exists():
            continue
        cmd = [str(bench), str(out_dir / job["wav"]), "--labels", str(out_dir / job["labels"]), "--oracle",
               "--front-end", "envelope", "--no-timing", "--record-channels", str(target),
               "--json", str(target / "bench.json")]
        done = subprocess.run(cmd, capture_output=True, text=True)
        if done.returncode != 0:
            raise RuntimeError(f"kz4ap-bench failed recording {job['result']}:\n{done.stderr}")
        print(f"recorded {job['result']}")


def score_engine_copies(out_dir: Path, bench: Path, front_ends=("baseline", "matched"), only: str | None = None) -> None:
    """Scores the oracle copies of the detector-only groups with the engine's own front ends
    (kz4ap-bench --oracle --front-end envelope|matched) into results/<front end>/<name>.oracle.json, so the
    prototype's rows there compare like for like. Existing results are kept."""
    out_dir = Path(out_dir)
    manifest = json.loads((out_dir / "manifest.json").read_text())
    for rec in manifest["recordings"]:
        for label_file, result, _ in oracle_copies(rec):
            if only is not None and not re.search(only, result):
                continue
            for fe in front_ends:
                target = out_dir / "results" / fe / f"{result}.json"
                if target.exists():
                    continue
                target.parent.mkdir(parents=True, exist_ok=True)
                cmd = [str(bench), str(out_dir / rec["wav"]), "--labels", str(out_dir / label_file), "--oracle",
                       "--front-end", BENCH_FRONT_END[fe], "--json", str(target)]
                done = subprocess.run(cmd, capture_output=True, text=True)
                if done.returncode != 0:
                    raise RuntimeError(f"kz4ap-bench failed on {result} ({fe}):\n{done.stderr}")
                lines = done.stdout.strip().splitlines()
                print(f"{fe:9s} {result}: {lines[-1] if lines else ''}")


def _decode_one(work) -> dict:
    record_dir, labels_path, position, values, p1_path = work
    cfg = ProtoConfig().with_values(**values)
    # labels_path None: a detector channel, decoded from its opening, mixed by the detector's frequency (Task 11b)
    ch = load_channel(record_dir, labels_path, position) if labels_path else load_detector_channel(record_dir, position)
    started = time.process_time()
    result = ChannelDecoder(cfg, ch.rate_hz).run(ch.baseband(), keep_p1=p1_path is not None)
    out = result.to_json()
    out.update(label_index=ch.label_index, rate_hz=ch.rate_hz, cpu_s=time.process_time() - started,
               channel_s=len(ch.y) / ch.rate_hz)
    if labels_path is None:
        out.update(track_id=ch.track_id, birth_freq_hz=ch.birth_freq_hz, last_freq_hz=ch.anchors[-1][1],
                   open_s=ch.open_s, close_s=ch.close_s)
    if p1_path is not None:
        Path(p1_path).parent.mkdir(parents=True, exist_ok=True)
        np.save(p1_path, result.p1)
    return out


def decode(out_dir: Path, name: str = DEFAULT_NAME, values: dict | None = None, only: str | None = None,
           jobs: int | None = None, keep_p1: bool = False) -> None:
    """Decodes every recorded channel of the matching oracle scorings with ProtoConfig().with_values(**values)."""
    out_dir = Path(out_dir)
    values = values or {}
    cfg = ProtoConfig().with_values(**values)  # validates the names before any work starts
    target = out_dir / "proto" / name
    work = []
    for job in oracle_scorings(out_dir, only):
        record_dir = out_dir / "channels" / job["result"]
        for i in range(len(read_manifest(record_dir)["channels"])):
            p1 = str(target / "p1" / job["result"] / f"{i}.npy") if keep_p1 else None
            work.append((job, (str(record_dir), str(out_dir / job["labels"]), i, values, p1)))
    for job in detector_jobs(out_dir, only):
        record_dir = out_dir / "channels" / job["result"]
        for i in range(len(read_manifest(record_dir)["channels"])):
            work.append((job, (str(record_dir), None, i, values, None)))
    results: dict = {}
    workers = jobs or max(1, (os.cpu_count() or 2) - 2)
    with ProcessPoolExecutor(max_workers=workers) as pool:
        for (job, _), channel in zip(work, pool.map(_decode_one, [w for _, w in work], chunksize=1)):
            results.setdefault(job["result"], (job, []))[1].append(channel)
    target.mkdir(parents=True, exist_ok=True)
    for result, (job, channels) in results.items():
        common = {"front_end": name, "recording": job["wav"], "labels": job["labels"], "config": asdict(cfg)}
        if "scorings" in job:  # detector channels: the "tracks" form, scored by frequency (Task 11b)
            channels.sort(key=lambda c: c["track_id"])
            tracks = [{"id": c["track_id"], "freq_hz": c["birth_freq_hz"], "last_freq_hz": c["last_freq_hz"],
                       "text": c["text"]} for c in channels]
            body = {**common, "tracks": tracks, "channels": channels}
        else:
            channels.sort(key=lambda c: c["label_index"])
            body = {**common, "texts": [c["text"] for c in channels], "channels": channels}
        (target / f"{result}.decoded.json").write_text(json.dumps(body) + "\n")
        print(f"decoded {result}")


def score(out_dir: Path, bench: Path, name: str = DEFAULT_NAME, only: str | None = None,
          results_root: Path | None = None) -> None:
    """Scores every decoded file with kz4ap-bench --score-decoded into results_root/name (default
    out_dir/results/name)."""
    out_dir = Path(out_dir)
    results = Path(results_root or out_dir / "results") / name
    results.mkdir(parents=True, exist_ok=True)
    scorings = [(job["result"], job["labels"], job["result"]) for job in oracle_scorings(out_dir, only)]
    scorings += [(job["result"], label_file, result) for job in detector_jobs(out_dir, only)
                 for label_file, result, _ in job["scorings"]]
    for decoded_name, label_file, result in scorings:
        decoded = out_dir / "proto" / name / f"{decoded_name}.decoded.json"
        if not decoded.exists():
            continue
        cmd = [str(bench), "--labels", str(out_dir / label_file), "--score-decoded", str(decoded),
               "--json", str(results / f"{result}.json")]
        done = subprocess.run(cmd, capture_output=True, text=True)
        if done.returncode != 0:
            raise RuntimeError(f"kz4ap-bench failed scoring {result} ({name}):\n{done.stderr}")
        lines = done.stdout.strip().splitlines()
        print(f"{name} {result}: {lines[-1] if lines else ''}")


def parse_values(pairs) -> dict:
    """--set KEY=VALUE pairs; VALUE is JSON if it parses (numbers, lists), else a string."""
    values = {}
    for pair in pairs or []:
        key, _, text = pair.partition("=")
        try:
            values[key] = json.loads(text)
        except json.JSONDecodeError:
            values[key] = text
    return values


def main(argv=None) -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)
    for command in ("record", "engine-copies", "decode", "score", "report"):
        p = sub.add_parser(command)
        p.add_argument("--out", type=Path, required=True)
        p.add_argument("--only", default=None, help="regular expression on result names")
        if command in ("record", "engine-copies", "score"):
            p.add_argument("--bench", type=Path, required=True)
        if command not in ("record", "engine-copies"):
            p.add_argument("--name", default=DEFAULT_NAME)
        if command == "decode":
            p.add_argument("--set", dest="values", action="append", help="KEY=VALUE: a ProtoConfig value")
            p.add_argument("--jobs", type=int, default=None)
            p.add_argument("--keep-p1", action="store_true")
        if command == "report":
            p.add_argument("--suffix", default="")
    args = parser.parse_args(argv)
    if args.command == "record":
        record(args.out, args.bench, args.only)
    elif args.command == "engine-copies":
        score_engine_copies(args.out, args.bench, only=args.only)
    elif args.command == "decode":
        decode(args.out, args.name, parse_values(args.values), args.only, args.jobs, args.keep_p1)
    elif args.command == "score":
        score(args.out, args.bench, args.name, args.only)
    else:
        from .report import write_report
        print(f"wrote {write_report(args.out, args.name, only=args.only, suffix=args.suffix)}")


if __name__ == "__main__":
    main()
```

- [ ] **Step 5: Implement the metrics**

Create `training/kz4ap_proto/metrics.py`:

```python
"""Measurements on the prototype's decoded files (proto/<name>/<result>.decoded.json) against their labels,
for the report (Task 12) and the experiments (Tasks 13-14). Intervals: bootstrap 95% over channels."""

from __future__ import annotations

import json
import math
from concurrent.futures import ProcessPoolExecutor
from pathlib import Path

import numpy as np

from kz4ap_synth.suites import BOOTSTRAP_RESAMPLES, _interval, _rng_for

from .periodicity import Periodicity
from .runner import oracle_scorings

PERIODICITY_GROUPS = ("A sensitivity", "B fading", "C fists", "G ragchew", "H two-station QSO, oracle (per station)",
                      "I Farnsworth")


def iter_channels(out_dir, name: str, only: str | None = None):
    """(job, label, decoded channel, decoded config) for every decoded channel."""
    out_dir = Path(out_dir)
    for job in oracle_scorings(out_dir, only):
        path = out_dir / "proto" / name / f"{job['result']}.decoded.json"
        if not path.exists():
            continue
        decoded = json.loads(path.read_text())
        labels = json.loads((out_dir / job["labels"]).read_text())["signals"]
        for ch in decoded["channels"]:
            yield job, labels[ch["label_index"]], ch, decoded["config"]


def true_dit_s(label: dict) -> float | None:
    """The label's dit, 1.2 s / WPM, if its speed is constant (no wpm_end; a QSO only if all senders share it)."""
    if label.get("wpm_end") is not None:
        return None
    senders = label.get("senders") or []
    if senders and len({s["wpm"] for s in senders}) > 1:
        return None
    return 1.2 / label["wpm"]


def transmissions(label: dict, pad_s: float = 0.0) -> list[tuple[float, float]]:
    txs = label.get("transmissions") or [{"start_s": label["start_s"], "end_s": label["end_s"]}]
    return [(t["start_s"] - pad_s, t["end_s"] + pad_s) for t in txs]


def bootstrap_ratio(units, key):
    """95% interval of sum(numerators) / sum(denominators), resampling units (channels)."""
    if len(units) < 2:
        return None
    num = np.array([u[0] for u in units], float)
    den = np.array([u[1] for u in units], float)
    picks = _rng_for(key).integers(len(units), size=(BOOTSTRAP_RESAMPLES, len(units)))
    return _interval([num[p].sum() / den[p].sum() if den[p].sum() > 0 else None for p in picks])


def speed_errors(out_dir, name, only=None, min_snr_db=6.0, factor=1.5, settle_s=3.0, run_s=3.0) -> dict:
    """Per group, scored constant-speed labels at S500 >= min_snr_db: the fraction of selection instants inside a
    transmission (from settle_s after its start) whose selected dit is off the label's by more than `factor`
    (no fit counts as off), and lock-ins: transmissions with such instants for run_s or longer in a row."""
    groups: dict = {}
    for job, label, ch, _ in iter_channels(out_dir, name, only):
        truth = true_dit_s(label)
        if truth is None or not label.get("score", True) or label["snr_db"] < min_snr_db:
            continue
        g = groups.setdefault(job["group"], {"units": [], "lock_ins": 0, "transmissions": 0})
        bad = total = 0
        for a, b in transmissions(label):
            g["transmissions"] += 1
            run_start, locked = None, False
            for t, _, T in ch["selections"]:
                if not a + settle_s <= t <= b:
                    continue
                total += 1
                off = T is None or abs(math.log(T / truth)) > math.log(factor)
                bad += off
                if off:
                    run_start = t if run_start is None else run_start
                    locked = locked or t - run_start >= run_s
                else:
                    run_start = None
            g["lock_ins"] += locked
        g["units"].append((bad, total))
    out = {}
    for grp, g in groups.items():
        bad, total = sum(u[0] for u in g["units"]), sum(u[1] for u in g["units"])
        out[grp] = {"instants": total, "error_fraction": bad / total if total else None,
                    "interval": bootstrap_ratio(g["units"], ("speed", grp)), "lock_ins": g["lock_ins"],
                    "transmissions": g["transmissions"]}
    return out


def switch_stats(out_dir, name, only=None, back_within_s=5.0) -> dict:
    """Per group, scored labels: branch switches and alternations (a switch straight back within back_within_s),
    per minute of transmission time."""
    groups: dict = {}
    for job, label, ch, _ in iter_channels(out_dir, name, only):
        if not label.get("score", True):
            continue
        g = groups.setdefault(job["group"], {"switches": 0, "alternations": 0, "minutes": 0.0})
        g["minutes"] += sum(b - a for a, b in transmissions(label)) / 60.0
        last = None  # (t, from, to)
        sel = ch["selections"]
        for (_, k0, _), (t1, k1, _) in zip(sel, sel[1:]):
            if k1 == k0:
                continue
            g["switches"] += 1
            if last is not None and last[1] == k1 and last[2] == k0 and t1 - last[0] <= back_within_s:
                g["alternations"] += 1
            last = (t1, k0, k1)
    return {grp: {**g, "switches_per_min": g["switches"] / g["minutes"] if g["minutes"] else None,
                  "alternations_per_min": g["alternations"] / g["minutes"] if g["minutes"] else None}
            for grp, g in groups.items()}


def spurious_over_starts(out_dir, name, only=None, settle_s=1.0) -> dict:
    """Per group, scored labels: over starts of the selected branch inside a transmission (from settle_s after
    its start; a real over starts in the silence before it), per transmission."""
    groups: dict = {}
    for job, label, ch, _ in iter_channels(out_dir, name, only):
        if not label.get("score", True):
            continue
        g = groups.setdefault(job["group"], {"over_starts": 0, "transmissions": 0})
        txs = transmissions(label)
        g["transmissions"] += len(txs)
        g["over_starts"] += sum(1 for t in ch["over_starts"] if any(a + settle_s <= t <= b for a, b in txs))
    return {grp: {**g, "per_transmission": g["over_starts"] / g["transmissions"] if g["transmissions"] else None}
            for grp, g in groups.items()}


def false_characters(out_dir, name, only=None, pad_s=0.5) -> dict:
    """Per group, scored labels: final characters (not word spaces) that start outside every transmission
    (padded by pad_s), per minute outside transmissions."""
    groups: dict = {}
    for job, label, ch, _ in iter_channels(out_dir, name, only):
        if not label.get("score", True):
            continue
        g = groups.setdefault(job["group"], {"characters": 0, "minutes": 0.0})
        txs = [(max(0.0, a), min(ch["channel_s"], b)) for a, b in transmissions(label, pad_s)]
        g["minutes"] += (ch["channel_s"] - sum(max(0.0, b - a) for a, b in txs)) / 60.0
        g["characters"] += sum(1 for text, start, _ in ch["chars"]
                               if text != " " and not any(a <= start <= b for a, b in txs))
    return {grp: {**g, "per_min": g["characters"] / g["minutes"] if g["minutes"] else None}
            for grp, g in groups.items()}


def cpu_per_channel_second(out_dir, name, only=None) -> float:
    """Decoding CPU time per channel-second, s/s."""
    cpu = seconds = 0.0
    for _, _, ch, _ in iter_channels(out_dir, name, only):
        cpu += ch["cpu_s"]
        seconds += ch["channel_s"]
    return cpu / seconds if seconds else 0.0


def _points_of(work) -> list[dict]:
    p1_path, cfg, rate, label, unit, group = work
    truth = true_dit_s(label)
    p1 = np.load(p1_path)
    per = Periodicity(cfg, rate)
    txs = transmissions(label)
    points = []
    for i in range(0, len(p1), per.update_every):
        per.push(p1[i:i + per.update_every])
        t = min(i + per.update_every, len(p1)) / rate
        inside = [k for k, (a, b) in enumerate(txs) if a <= t <= b]
        if not inside:
            continue
        per.update(force=True)
        points.append({"group": group, "unit": unit, "tx": inside[0], "truth": truth,
                       "since_start": t - txs[inside[0]][0], "per": list(per.per_window)})
    return points


def periodicity_points(out_dir, name, cfg, only=None, groups=PERIODICITY_GROUPS, min_snr_db=0.0, jobs=None) -> list:
    """Runs cfg's periodicity estimator offline on the stored branch-1 posteriors (decode --keep-p1), every
    window logged: one point per update inside a transmission of a scored, constant-speed label at
    S500 >= min_snr_db in the given groups. Exact: T_P never feeds back into branch 1's posterior."""
    out_dir = Path(out_dir)
    work = []
    for job, label, ch, _ in iter_channels(out_dir, name, only):
        if job["group"] not in groups or true_dit_s(label) is None or not label.get("score", True):
            continue
        if label["snr_db"] < min_snr_db:
            continue
        p1 = out_dir / "proto" / name / "p1" / job["result"] / f"{ch['label_index']}.npy"
        work.append((str(p1), cfg, ch["rate_hz"], label, (job["result"], ch["label_index"]), job["group"]))
    with ProcessPoolExecutor(max_workers=jobs) as pool:
        return [p for points in pool.map(_points_of, work, chunksize=1) for p in points]


def evaluate_rule(points, windows_s, subset, threshold: float, tolerance: float = math.log(1.05)) -> dict:
    """The rule "the confident estimate (score >= threshold) with the shortest window in subset": precision
    (confident estimates within 5% of the true dit), coverage (points with a confident estimate), and the median
    time from a transmission's start to its first confident estimate, s."""
    order = [list(windows_s).index(w) for w in sorted(subset)]
    units: dict = {}
    firsts: dict = {}
    for p in points:
        u = units.setdefault(p["unit"], [0, 0, 0])  # points, confident, correct
        u[0] += 1
        est = next((p["per"][i] for i in order if p["per"][i][0] is not None and p["per"][i][1] >= threshold), None)
        if est is None:
            continue
        u[1] += 1
        u[2] += abs(math.log(est[0] / p["truth"])) <= tolerance
        firsts.setdefault((p["unit"], p["tx"]), p["since_start"])
    total = sum(u[0] for u in units.values())
    confident = sum(u[1] for u in units.values())
    correct = sum(u[2] for u in units.values())
    key = (tuple(sorted(subset)), round(threshold, 6))
    return {"points": total, "confident": confident,
            "precision": correct / confident if confident else None,
            "precision_interval": bootstrap_ratio([(u[2], u[1]) for u in units.values()], ("precision",) + key),
            "coverage": confident / total if total else None,
            "coverage_interval": bootstrap_ratio([(u[1], u[0]) for u in units.values()], ("coverage",) + key),
            "median_time_to_confident_s": float(np.median(list(firsts.values()))) if firsts else None}


def calibrate(points, windows_s, subset, target: float = 0.95):
    """(threshold, evaluate_rule result): the lowest of 100 quantiles of the scores seen at which the rule's
    precision reaches target; (None, None) if none does."""
    scores = [s for p in points for t, s in p["per"] if t is not None]
    if not scores:
        return None, None
    for threshold in np.unique(np.quantile(scores, np.linspace(0.0, 0.99, 100))):
        r = evaluate_rule(points, windows_s, subset, float(threshold))
        if r["precision"] is not None and r["precision"] >= target:
            return float(threshold), r
    return None, None
```

- [ ] **Step 6: Implement the report**

Create `training/kz4ap_proto/report.py`:

```python
"""The prototype against the engine's front ends on the same oracle signals, every regime included: the full
comparison the owner decides on (spec section 7; owner, 2026-09-30: no acceptance gate), from the suite's
results (plan Task 12)."""

from __future__ import annotations

import json
import re
from pathlib import Path

from kz4ap_synth import suites

from . import metrics

REFERENCES = ("matched", "baseline")
# The start-up runaways of milestone 2 (docs/signal-processing.md 8b, "Start-up runaway"): recording, labeled Hz.
RUNAWAY_CASES = (("A-awgn-25wpm-1-s1", 6606.5), ("A-awgn-25wpm-0-s2", -2991.9))


def comparable_rows(out_dir, name, results_dirs=None, only=None) -> list[dict]:
    """Rows of the prototype and the references, on the signals the prototype decoded."""
    rows, _ = suites.load_results(Path(out_dir), results_dirs, [name, *REFERENCES])
    if only:
        rows = [r for r in rows if re.search(only, r["recording"])]
    ours = {(r["recording"], r["index"]) for r in rows if r["front_end"] == name}
    return [r for r in rows if (r["recording"], r["index"]) in ours]


def not_comparable(group: str, tag: str) -> str | None:
    """Why a row's comparison with the engine is unfair (plan: Design decisions, "Where the streams are recorded")."""
    if group == "F tuning" and tag.startswith("drift"):
        return "oracle mix follows the labeled drift (favors the prototype)"
    if group == "H two-station QSO, oracle" and suites._tag_offset_hz(tag) != 0.0:
        return "QSO label: the answering station is off the oracle mix (handicaps the prototype)"
    return None


def _first_word(rows, fe, group, tag, lo_db, hi_db):
    sig = [(r["first_word_edits"], r["first_word_symbols"]) for r in rows
           if r["front_end"] == fe and r["group"] == group and r["tag"] == tag and r["scored"]
           and lo_db <= r["snr_db"] <= hi_db and r["first_word_symbols"] > 0]
    e, s = sum(x for x, _ in sig), sum(y for _, y in sig)
    return {"cer": e / s if s else None, "interval": suites.bootstrap_cer(sig, suites._rng_for(("fw", fe, tag))),
            "signals": len(sig)}


def _signal_cer(out_dir, results_dirs, fe, recording, freq_hz):
    for root in results_dirs or [Path(out_dir) / "results"]:
        path = Path(root) / fe / f"{recording}.json"
        if path.exists():
            signals = json.loads(path.read_text())["score"]["signals"]
            return min(signals, key=lambda s: abs(s["freq_offset_hz"] - freq_hz))["cer"]
    return None


# What stage 1 leaves to stage 2 (owner and controller, 2026-09-30; spec section 7; backlog).
DETECTOR_GAP = ("Which tracks the detector opens is unchanged by the redesign. Detection recall, false tracks and tracks "
                "per QSO as the bench counts them (tracks that decoded text) depend on the decoder, and stage 1 reports "
                "them for the prototype on the recorded channels of the Matched path's detector, beside Matched and "
                "Envelope. Stage 2 confirms them in C++ behind the live detector, with the frequency tracker in the "
                "loop (stage 1 mixes by the detector's frequency without the tracker's fine-tuning).")
# Detector-path groups: the prototype decodes the Matched path's detector channels; Envelope's detector (bin
# attribution) opens its own tracks, so against Envelope these rows compare per label, not per track.
DETECTOR_PATH_GROUPS = ("H two-station QSO", "H two-station QSO (per station)", "strong", "pauses", "tune-up",
                        "first sample", "crowded", "band")


def detector_measures(out_dir, name, results_dirs=None, only=None) -> dict:
    """The bench's detection measures on the detector path, per front end (the prototype, Matched, Envelope) and
    group: scored labels, detected labels, detection recall, and false tracks (tracks that decoded text and matched
    no label), summed over the non-oracle recordings' main scorings; plus tracks per QSO (group H, suites.track_splits).
    As the bench counts them they depend on the decoder (a track counts only if it decoded text)."""
    out_dir = Path(out_dir)
    manifest = json.loads((out_dir / "manifest.json").read_text())
    roots = results_dirs or [out_dir / "results"]
    out: dict = {}
    for fe in (name, *REFERENCES):
        for rec in manifest["recordings"]:
            if rec["oracle"] or (only and not re.search(only, rec["name"])):
                continue
            path = next((Path(r) / fe / f"{rec['name']}.json" for r in roots
                         if (Path(r) / fe / f"{rec['name']}.json").exists()), None)
            if path is None:
                continue
            s = json.loads(path.read_text())["score"]
            g = out.setdefault(fe, {}).setdefault(rec["group"], {"recordings": 0, "scored": 0, "detected": 0,
                                                                  "false_tracks": 0})
            g["recordings"] += 1
            for k in ("scored", "detected", "false_tracks"):
                g[k] += s[k]
    for groups in out.values():
        for g in groups.values():
            g["detection_recall"] = g["detected"] / g["scored"] if g["scored"] else None
    splits = suites.track_splits(out_dir)
    return {"by_front_end": out,
            "tracks_per_qso": {f"{fe} | {tag}": v for (fe, tag), v in sorted(splits.items())
                               if fe in (name, *REFERENCES)}}


def verdict(interval) -> str:
    """"better" or "worse" when the paired 95% interval (prototype minus reference, CER) excludes 0, else
    "unchanged"; "no interval" for fewer than 2 signals."""
    if interval is None:
        return "no interval"
    if interval[1] < 0:
        return "better"
    if interval[0] > 0:
        return "worse"
    return "unchanged"


def comparison(rows, name, out_dir, results_dirs=None) -> dict:
    """The full comparison, no gate (owner): (a) group A S500 at CER 0.10 and 0.05 at every speed for the prototype,
    Matched and Envelope, with intervals and the differences; (b) R1 (group D step 20->35 WPM), R2 (group A 12 WPM
    first-word CER at S500 6-20 dB) and the start-up runaway cases; (c) every (group, tag): the prototype against
    Matched and against Envelope, paired, called better / worse / unchanged beyond its interval, by how much."""
    agg = suites.aggregate(rows)
    crossings = []
    for tag in ("12 wpm", "25 wpm", "40 wpm"):
        for threshold in ("0.1", "0.05"):
            cell = {fe: (agg[(fe, "A sensitivity", tag)]["snr_at_cer"].get(threshold),
                         agg[(fe, "A sensitivity", tag)]["snr_at_cer_interval"].get(threshold))
                    if (fe, "A sensitivity", tag) in agg else (None, None) for fe in (name, *REFERENCES)}
            ours = cell[name][0]
            crossings.append({"tag": tag, "cer": threshold, **{fe: list(v) for fe, v in cell.items()},
                              **{f"minus_{fe}_db": ours - cell[fe][0] if ours is not None and cell[fe][0] is not None
                                 else None for fe in REFERENCES}})
    r1 = {fe: {k: agg.get((fe, "D speed", "step 20->35"), {}).get(k) for k in ("cer", "cer_interval")}
          for fe in (name, *REFERENCES)}
    r2 = {fe: _first_word(rows, fe, "A sensitivity", "12 wpm", 6.0, 20.0) for fe in (name, *REFERENCES)}
    runaways = [{"recording": rec, "freq_hz": f,
                 **{fe: _signal_cer(out_dir, results_dirs, fe, rec, f) for fe in (name, *REFERENCES)}}
                for rec, f in RUNAWAY_CASES]
    pairs = {ref: suites.paired_differences(rows, ref, name) for ref in REFERENCES}
    regimes = []
    for group, tag in sorted(set(pairs["matched"]) | set(pairs["baseline"])):
        entry = {"group": group, "tag": tag, "not_comparable": not_comparable(group, tag),
                 "note": not_comparable(group, tag) or ("detector path: Matched's tracks; Envelope's detector opens "
                                                        "its own" if group in DETECTOR_PATH_GROUPS else None)}
        for ref in REFERENCES:
            v = pairs[ref].get((group, tag))
            entry[ref] = None if v is None else {"signals": v["signals"], "mean": v["mean"], "interval": v["interval"],
                                                 "verdict": verdict(v["interval"])}
        regimes.append(entry)
    counts = {ref: {k: sum(1 for e in regimes if e[ref] and e[ref]["verdict"] == k and not e["not_comparable"])
                    for k in ("better", "worse", "unchanged", "no interval")} for ref in REFERENCES}
    return {"crossings": crossings, "r1": r1, "r2": r2, "runaways": runaways, "regimes": regimes, "counts": counts,
            "detector_gap": DETECTOR_GAP}


def _fmt(v, interval=None, fmt=".3f"):
    return suites._with_interval(v, interval, fmt)


def write_report(out_dir, name, results_dirs=None, only=None, suffix="") -> Path:
    out_dir = Path(out_dir)
    rows = comparable_rows(out_dir, name, results_dirs, only)
    agg = suites.aggregate(rows)
    pairs = {ref: suites.paired_differences(rows, ref, name) for ref in REFERENCES}
    comp = comparison(rows, name, out_dir, results_dirs)
    lines = [f"# {name} against Matched and Envelope" + (f" ({suffix})" if suffix else ""), "",
             "Same oracle signals for all three. CER with bootstrap 95% intervals over signals; paired columns: "
             f"{name} minus the reference, signal by signal (negative favors {name}). S₅₀₀: key-down carrier power "
             "over noise power in 500 Hz, dB. Rows marked * are not comparable (reason given).", ""]
    for group in sorted({g for _, g, _ in agg}):
        lines += [f"## {group}", "", f"| tag | signals | {name} CER | Matched CER | Envelope CER | {name} − Matched | "
                  f"{name} − Envelope | first-word CER {name} / Matched / Envelope |", "|---|---|---|---|---|---|---|---|"]
        for tag in sorted({t for fe, g, t in agg if g == group}):
            v = {fe: agg.get((fe, group, tag)) for fe in (name, *REFERENCES)}
            if v[name] is None:
                continue
            note = not_comparable(group, tag)
            p = [pairs[ref].get((group, tag), {}) for ref in REFERENCES]
            cells = [_fmt(v[fe]["cer"], v[fe]["cer_interval"]) if v[fe] else "—" for fe in (name, *REFERENCES)]
            fw = " / ".join(f"{v[fe]['first_word_cer']:.3f}" if v[fe] else "—" for fe in (name, *REFERENCES))
            lines.append(f"| {tag}{' *' + note if note else ''} | {v[name]['signals']} | {' | '.join(cells)} | "
                         f"{_fmt(p[0].get('mean'), p[0].get('interval'), '+.3f')} | "
                         f"{_fmt(p[1].get('mean'), p[1].get('interval'), '+.3f')} | {fw} |")
        lines.append("")
    names = {name: name, "matched": "Matched", "baseline": "Envelope"}
    lines += ["## Comparison for the owner (no acceptance gate)", "",
              "Group A, S₅₀₀ at CER 0.10 and 0.05 (dB), with intervals; differences in dB of S₅₀₀ (negative: the "
              f"{name} needs less signal):", "",
              f"| tag | CER | {name} | Matched | Envelope | {name} − Matched | {name} − Envelope |", "|---|---|---|---|---|---|---|"]
    for c in comp["crossings"]:
        lines.append(f"| {c['tag']} | {c['cer']} | " + " | ".join(_fmt(c[fe][0], c[fe][1], '.1f') for fe in (name, *REFERENCES))
                     + f" | {_fmt(c['minus_matched_db'], None, '+.1f')} | {_fmt(c['minus_baseline_db'], None, '+.1f')} |")
    r1, r2 = comp["r1"], comp["r2"]
    lines += ["", "Regression R1 (group D, step 20→35 WPM, CER; Matched 0.394 after milestone-2a Task 17, 0.113 at Task 14): " +
              ", ".join(f"{names[fe]} {_fmt(r1[fe]['cer'], r1[fe]['cer_interval'])}" for fe in r1) + ".",
              "Regression R2 (group A 12 WPM, first-word CER at S₅₀₀ 6–20 dB; Matched 0.985 after Task 17): " +
              ", ".join(f"{names[fe]} {_fmt(r2[fe]['cer'], r2[fe]['interval'])} ({r2[fe]['signals']} signals)" for fe in r2) + ".",
              "Start-up runaway cases (CER): " + "; ".join(
                  f"{c['recording']} {c['freq_hz']:+.1f} Hz: " + ", ".join(
                      f"{names[fe]} {_fmt(c[fe])}" for fe in (name, *REFERENCES)) for c in comp["runaways"]) + ".", "",
              f"Every regime: {name} minus the reference, paired CER with its 95% interval; better / worse beyond the "
              "interval, else unchanged. Comparable regimes: " + "; ".join(
                  f"against {names[ref]}: " + ", ".join(f"{v} {k}" for k, v in comp["counts"][ref].items())
                  for ref in REFERENCES) + ".", "",
              "| group | tag | against Matched | against Envelope | note |", "|---|---|---|---|---|"]
    for e in comp["regimes"]:
        cells = [f"{_fmt(e[ref]['mean'], e[ref]['interval'], '+.3f')} {e[ref]['verdict']}" if e[ref] else "—"
                 for ref in REFERENCES]
        lines.append(f"| {e['group']} | {e['tag']} | {cells[0]} | {cells[1]} | {e['note'] or ''} |")
    measures = detector_measures(out_dir, name, results_dirs, only)
    lines += ["", "Detection measures on the detector path (the Matched path's detector; as the bench counts them, a "
              "track counts only if it decoded text):", "",
              "| group | front end | recordings | labels scored | detected | detection recall | false tracks |",
              "|---|---|---|---|---|---|---|"]
    for fe, groups in measures["by_front_end"].items():
        for g, v in sorted(groups.items()):
            lines.append(f"| {g} | {names[fe]} | {v['recordings']} | {v['scored']} | {v['detected']} | "
                         f"{_fmt(v['detection_recall'], None, '.3f')} | {v['false_tracks']} |")
    lines += ["", "| front end and group-H tag | QSOs | tracks per QSO |", "|---|---|---|"]
    lines += [f"| {k} | {v['qsos']} | {v['mean_tracks']:.2f} |" for k, v in measures["tracks_per_qso"].items()]
    lines += ["", f"**Stated gap.** {comp['detector_gap']}", ""]
    lines += ["", f"## {name}'s own statistics", ""]
    speed = metrics.speed_errors(out_dir, name, only)
    lines += ["Selected speed off the label's by more than ×1.5 (fraction of selection instants, from 3 s into each "
              "transmission, S₅₀₀ ≥ 6 dB, constant-speed labels) and lock-ins (3 s or longer):", "",
              "| group | instants | fraction | lock-ins / transmissions |", "|---|---|---|---|"]
    lines += [f"| {g} | {v['instants']} | {_fmt(v['error_fraction'], v['interval'])} | {v['lock_ins']} / {v['transmissions']} |"
              for g, v in sorted(speed.items())]
    switches = metrics.switch_stats(out_dir, name, only)
    overs = metrics.spurious_over_starts(out_dir, name, only)
    false = metrics.false_characters(out_dir, name, only)
    lines += ["", "| group | switches per min | alternations per min | over starts inside a transmission, per "
              "transmission | false characters per min outside transmissions |", "|---|---|---|---|---|"]
    for g in sorted(switches):
        lines.append(f"| {g} | {_fmt(switches[g]['switches_per_min'], None, '.2f')} | "
                     f"{_fmt(switches[g]['alternations_per_min'], None, '.2f')} | "
                     f"{_fmt(overs.get(g, {}).get('per_transmission'), None, '.3f')} | "
                     f"{_fmt(false.get(g, {}).get('per_min'), None, '.3f')} |")
    lines += ["", f"Decoding CPU: {1000 * metrics.cpu_per_channel_second(out_dir, name, only):.1f} ms per "
              "channel-second (Python prototype; not comparable with the engine's C++)."]
    path = out_dir / f"report-{name}{('-' + suffix) if suffix else ''}.md"
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")
    (path.with_suffix(".json")).write_text(json.dumps({"comparison": comp, "detector_measures": measures, "speed": speed, "switches": switches,
                                                       "over_starts": overs, "false_characters": false},
                                                      indent=2, default=str) + "\n")
    return path
```

- [ ] **Step 7: Run the tests**

Run: `.venv\Scripts\python -m pytest training -q`
Expected: all pass.

- [ ] **Step 8: Score the oracle copies with Envelope and Matched, record the suite's oracle streams, and run the pipeline end to end on one recording**

```powershell
.venv\Scripts\python -m kz4ap_proto.runner engine-copies --out build/suite/full3 --bench build/windows/bench/Release/kz4ap-bench.exe
.venv\Scripts\python -m kz4ap_proto.runner record --out build/suite/full3 --bench build/windows/bench/Release/kz4ap-bench.exe
.venv\Scripts\python -m kz4ap_proto.runner decode --out build/suite/full3 --name smoke-proto --only "^D-speed-s1$"
.venv\Scripts\python -m kz4ap_proto.runner score --out build/suite/full3 --bench build/windows/bench/Release/kz4ap-bench.exe --name smoke-proto --only "^D-speed-s1$"
```
Expected: first 54 `envelope`/`matched … CER …` lines for the oracle copies (27 detector-only recordings: per seed pauses, strong, tune-up, first sample, band and four crowded; about 2 min), then `recorded …` for the 33 detector-path recordings (`….detector`: the 27 above plus group H's grid and drawn recordings, per seed) and for each of the 120 oracle scorings (90 oracle recordings, 30 per seed with group I's two; the three H-oracle recordings once more against their station labels; and the 27 oracle copies); about 5.1 GB in `build/suite/full3/channels/` (about 430 000 channel-seconds × 12 kB/s, the detector path's about 44 000 included; the three `.stations` scorings record the same WAVs again, at the station frequencies); record the time taken. Then `decoded D-speed-s1` and one `smoke-proto D-speed-s1: CER …` line. Delete `build/suite/full3/results/smoke-proto` and `build/suite/full3/proto/smoke-proto` afterwards (they are not a result).

- [ ] **Step 9: Commit**

```powershell
git add training/kz4ap_synth/suites.py training/kz4ap_proto/runner.py training/kz4ap_proto/metrics.py training/kz4ap_proto/report.py training/tests/test_suites.py training/tests/test_proto_runner.py training/tests/test_proto_metrics.py
```
```powershell
git commit -m "Add the prototype runner, its metrics and the report against Matched and Envelope"
```

---
### Task 13: Experiment harness; noise and periodicity experiments (E10, E1, E2, E3)

The experiment tooling, the results document, the development reference run, and the first four experiments (Design decisions, "Development set, held-out seeds and decision rules"). E1–E3 are evaluated **offline** on branch 1's stored posterior (`--keep-p1`): the periodicity estimate never feeds back into it, so every method, window set and comb setting is evaluated exactly from one decode. **Each experiment's procedure, metric and decision rule below are fixed before it runs; the outcome is whatever the rule gives, recorded with its numbers.** If no setting meets a rule's criterion, keep the placeholder, record "no setting met the criterion", and report to the owner.

**How runs are executed (compute budget: Design decisions).** Every `experiments run`, `experiments batch` and `experiments calibrate-x-on` below is started as a **background process** (`run_in_background`), with its output redirected to `build/suite/full3/experiments/logs/<name>.log`. The executing agent starts it, **does not poll**, is notified when it ends, and then reads only the summary file the run writes (`build/suite/full3/experiments/summary-<name>.md`) and the compare files it names. Several variants of one experiment go into one `batch` (a JSON file under `build/suite/full3/experiments/`), which runs them one after another, compares each with the base, and writes one summary.

**Files:**
- Create: `training/kz4ap_proto/experiments.py`, `training/tests/test_proto_experiments.py`
- Create: `docs/plans/2026-09-30-milestone-2b-stage-1-results.md`
- Modify: `training/kz4ap_proto/params.py` (only defaults a decision rule adopts, with their status comment changed to "measured (E#)")

**Interfaces:**
- Consumes: `runner.decode`, `runner.score`, `runner.parse_values`; `metrics.*`; `suites.load_results`, `aggregate`, `_interval`, `_rng_for`, `BOOTSTRAP_RESAMPLES`; `channel.ChannelDecoder`; `bank.realized_lengths_s`; `generate.keying_envelope`; `keying.timed_intervals`.
- Produces: `experiments.DEV` (regular expression: the development set), `experiments.DEV_E5` (E5's subset), `experiments.EXPERIMENT_RESULTS` (`experiments/results` under the suite), `experiments.run(out_dir, bench, name, values, keep_p1=False, jobs=None, only=DEV) -> Path` (writes `experiments/summary-<name>.md`), `experiments.batch(out_dir, bench, spec_path, jobs=None) -> Path`, `experiments.lowpass(rate_hz=1500.0)`, `experiments.calibrate_x_on(cfg, rate_hz=1500.0, events=20, seed=1, chunk_s=2000.0) -> list[float]`, `experiments.pooled_paired(rows, a, b, edits="edits", symbols="symbols") -> dict` (by group and `"all"`: `signals`, `mean`, `interval`), `experiments.compare(out_dir, base, variant) -> Path`, `experiments.periodicity_table(out_dir, name, values, subsets, target=0.95, jobs=None) -> Path`, `experiments.stream(intervals, start_s, duration_s, s500_db, seed, rate_hz=1500.0)`, `experiments.follow_marks(cfg, seeds) -> list[int | None]`; CLI `run | batch | compare | periodicity | follow | calibrate-x-on`.

- [ ] **Step 1: Write the failing tests**

Create `training/tests/test_proto_experiments.py`:

```python
import re

import pytest

from kz4ap_proto import experiments
from kz4ap_proto.params import ProtoConfig


def row(fe, index, edits, symbols=100, group="A sensitivity"):
    return {"front_end": fe, "group": group, "tag": "t", "recording": "r", "index": index, "scored": True,
            "edits": edits, "symbols": symbols, "first_word_edits": 0, "first_word_symbols": 0}


def test_the_development_set_is_seed_one_without_the_per_row_fading_recordings():
    names = ["A-awgn-12wpm-0-s1", "B-fading-mix-s1", "B-fading-paddle-24wpm-0.1Hz-s1", "C-fists-bug-s1",
             "D-speed-s1", "H-qso-oracle-s1.stations", "I-farnsworth-machine-s1", "A-awgn-12wpm-0-s2", "strong-s1"]
    assert [n for n in names if re.search(experiments.DEV, n)] == [
        "A-awgn-12wpm-0-s1", "B-fading-mix-s1", "C-fists-bug-s1", "D-speed-s1", "H-qso-oracle-s1.stations",
        "I-farnsworth-machine-s1"]


def test_pooled_paired_differences_by_group_and_overall():
    rows = [row("ref", i, 10) for i in range(4)] + [row("var", i, 5) for i in range(4)]
    rows += [row("ref", 9, 10, group="D speed"), row("var", 9, 30, group="D speed")]
    d = experiments.pooled_paired(rows, "ref", "var")
    assert d["A sensitivity"]["mean"] == pytest.approx(-0.05) and d["A sensitivity"]["signals"] == 4
    assert d["all"]["mean"] == pytest.approx((4 * -0.05 + 0.2) / 5)
    assert d["D speed"]["interval"] is None  # one signal: no interval


def test_calibrated_thresholds_make_noise_key_at_the_target_rate():
    # Calibrated at 0.5 key-downs per second (fast to measure); fresh channel-shaped noise must then key branch 1
    # near that rate: 100 expected in 200 s (Poisson spread about 10).
    import numpy as np
    from kz4ap_proto.bank import boxcar
    from kz4ap_proto.keying import BankKeyer, edges
    cfg = ProtoConfig(false_marks_per_s=0.5)
    x_on = experiments.calibrate_x_on(cfg, events=200)
    assert len(x_on) == 32 and all(1.55 < x < 7.0 for x in x_on)
    rng = np.random.default_rng(99)
    n = int(200 * 1500)
    h = experiments.lowpass()
    u = np.convolve((rng.standard_normal(n) + 1j * rng.standard_normal(n)) / np.sqrt(2), h, mode="same")
    sigma2 = 0.5 * np.sum(np.convolve(h, np.ones(14) / 14) ** 2)
    keyer = BankKeyer(cfg.with_values(x_on_values=x_on), 1500.0, np.array([14 / 1500] + [0.1] * 31))
    P = np.abs(boxcar(u, 14)) ** 2
    key, _, _, _ = keyer.step(np.vstack([P] * 32)[:, :], np.full(32, sigma2))
    downs = sum(1 for _, down in edges(key[:1], np.zeros(1, bool), 0)[0] if down)
    assert 60 <= downs <= 150


def test_follow_marks_counts_marks_until_a_matched_branch_is_selected():
    counts = experiments.follow_marks(ProtoConfig(), [1])
    assert len(counts) == 1 and (counts[0] is None or 0 <= counts[0] <= 40)
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `.venv\Scripts\python -m pytest training/tests/test_proto_experiments.py -q`
Expected: `ImportError: cannot import name 'experiments'`.

- [ ] **Step 3: Implement**

Create `training/kz4ap_proto/experiments.py`:

```python
"""Stage 1 experiments (plan Tasks 13-14): the prototype on the development set with one parameter group
changed, scored like the suite and compared signal by signal with a reference run; the periodicity estimator
evaluated offline on stored branch-1 posteriors; and the synthetic speed-step follow test.

    python -m kz4ap_proto.experiments run --out build/suite/full3 --bench PATH --name exp-ref [--set KEY=VALUE ...] [--keep-p1] [--jobs N]
    python -m kz4ap_proto.experiments compare --out build/suite/full3 --base exp-ref --variant exp-E10-branch
    python -m kz4ap_proto.experiments periodicity --out build/suite/full3 --name exp-ref [--set KEY=VALUE ...] --subsets "2,5,10;1,2,5,10"
    python -m kz4ap_proto.experiments follow [--set KEY=VALUE ...] [--seeds 10]
    python -m kz4ap_proto.experiments batch --out build/suite/full3 --bench PATH --spec build/suite/full3/experiments/E4.json
    python -m kz4ap_proto.experiments calibrate-x-on [--set KEY=VALUE ...] [--events 20]
Start run, batch and calibrate-x-on in the background; each writes experiments/summary-<name>.md when done.
"""

from __future__ import annotations

import argparse
import json
import math
import time
from pathlib import Path

import numpy as np

from kz4ap_synth.generate import keying_envelope
from kz4ap_synth.keying import timed_intervals
from kz4ap_synth.suites import BOOTSTRAP_RESAMPLES, _interval, _rng_for, _with_interval, aggregate, load_results

from . import metrics, runner
from .bank import boxcar, branch_lengths_s, branch_samples, realized_lengths_s
from .channel import ChannelDecoder
from .params import ProtoConfig

# Seed 1 of the oracle recordings, without group B's per-row recordings (60% of the channel-seconds); B's
# mixed-style recording stays in.
DEV = (r"^(A-awgn-.*|B-fading-mix|C-fists-.*|D-speed|E-qrm|F-offset|F-drift|G-ragchew|H-qso-oracle|"
       r"I-farnsworth-.*)-s1(\.stations)?$")
# E5's finest grids cost about 15x a run: they use this subset (about 20% of DEV), base included.
DEV_E5 = r"^(A-awgn-25wpm-.*|C-fists-.*|D-speed|I-farnsworth-.*)-s1$"
SUBSETS = {"dev": DEV, "e5": DEV_E5}
EXPERIMENT_RESULTS = Path("experiments") / "results"


def run(out_dir, bench, name: str, values: dict, keep_p1: bool = False, jobs=None, only: str = DEV) -> Path:
    """Decodes and scores the subset, then writes experiments/summary-<name>.md: the settings, the signals
    scored, the pooled CER, the wall time and the decoding CPU (the only file the agent reads afterwards)."""
    out_dir = Path(out_dir)
    started = time.monotonic()
    runner.decode(out_dir, name, values, only, jobs, keep_p1)
    runner.score(out_dir, bench, name, only, results_root=out_dir / EXPERIMENT_RESULTS)
    rows, _ = load_results(out_dir, [out_dir / EXPERIMENT_RESULTS], [name])
    scored = [r for r in rows if r["scored"]]
    edits, symbols = sum(r["edits"] for r in scored), sum(r["symbols"] for r in scored)
    path = out_dir / "experiments" / f"summary-{name}.md"
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(f"# {name}\n\nSettings: {json.dumps(values)}; subset: `{only}`.\n\n"
                    f"Signals scored: {len(scored)}; pooled CER {edits / symbols if symbols else float('nan'):.4f}.\n"
                    f"Wall time {(time.monotonic() - started) / 60.0:.1f} min; decoding CPU "
                    f"{1000 * metrics.cpu_per_channel_second(out_dir, name, only):.1f} ms per channel-second.\n",
                    encoding="utf-8")
    return path


def batch(out_dir, bench, spec_path, jobs=None) -> Path:
    """Runs the variants a JSON file lists, one after another, and compares each with the base:
    {"name": NAME, "base": BASE, "subset": "dev" | "e5", "runs": [{"name": ..., "set": {...}}, ...],
    "follow": [{"set": {...}}, ...]} ("follow": optional speed-step follow tests, 10 seeds each). Writes
    experiments/summary-<NAME>.md listing each run's pooled CER, its paired CER against the base and the files."""
    out_dir = Path(out_dir)
    spec = json.loads(Path(spec_path).read_text())
    only = SUBSETS[spec.get("subset", "dev")]
    lines = [f"# Batch {spec['name']} (base {spec['base']}, subset {spec.get('subset', 'dev')})", ""]
    for item in spec["runs"]:
        summary = run(out_dir, bench, item["name"], item.get("set", {}), jobs=jobs, only=only)
        report = compare(out_dir, spec["base"], item["name"]) if item["name"] != spec["base"] else None
        rows, _ = load_results(out_dir, [out_dir / EXPERIMENT_RESULTS], [spec["base"], item["name"]])
        pooled = pooled_paired(rows, spec["base"], item["name"]).get("all", {})
        lines.append(f"- {item['name']} {json.dumps(item.get('set', {}))}: paired CER against the base "
                     f"{_with_interval(pooled.get('mean'), pooled.get('interval'), '+.4f')}; {summary.name}"
                     + (f"; {report.name}" if report else ""))
    for item in spec.get("follow", []):
        counts = follow_marks(ProtoConfig().with_values(**item.get("set", {})), range(1, 11))
        found = [c for c in counts if c is not None]
        lines.append(f"- follow {json.dumps(item.get('set', {}))}: marks per seed {counts}; median "
                     f"{np.median(found) if found else None}; never followed in {counts.count(None)} of 10")
    path = out_dir / "experiments" / f"summary-{spec['name']}.md"
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")
    return path


def lowpass(rate_hz: float = 1500.0) -> np.ndarray:
    """A 129-tap Blackman-windowed sinc, -6 dB relative to the passband at +/-150 Hz, unity gain at 0 Hz: the
    channel filter's shape (as Task 5's tests)."""
    k = np.arange(129) - 64
    h = np.sinc(2 * 150.0 / rate_hz * k) * np.blackman(129)
    return h / h.sum()


def calibrate_x_on(cfg, rate_hz: float = 1500.0, events: int = 20, seed: int = 1, chunk_s: float = 2000.0) -> list:
    """Per branch, the x_on at which channel-shaped noise alone keys the unknown-amplitude test down
    cfg.false_marks_per_s times per second (review I4: the nominal formula keys 7-10x more). Each excursion of x
    above x_off gives exactly one key-down if its maximum exceeds x_on (release needs x < x_off, which ends the
    excursion), so x_on is the (R_fa x duration)-th largest excursion maximum: exact for the simulated noise. The
    noise lasts events / R_fa seconds (about `events` key-downs); sigma_v,k^2 is exact (derived from the filters)."""
    lengths = branch_samples(branch_lengths_s(cfg), rate_hz)
    h = lowpass(rate_hz)
    x_off = math.sqrt(-2.0 * math.log(cfg.release_probability))
    total_s = events / cfg.false_marks_per_s
    skip = len(h) + int(lengths[-1])
    rng = np.random.default_rng(seed)
    maxima: list = [[] for _ in lengths]
    used_s = 0.0
    while used_s < total_s:
        n = int(min(chunk_s, total_s - used_s + 1.0) * rate_hz) + skip
        u = np.convolve((rng.standard_normal(n) + 1j * rng.standard_normal(n)) / math.sqrt(2.0), h, mode="same")
        for k, nk in enumerate(lengths):
            sigma2 = 0.5 * float(np.sum(np.convolve(h, np.ones(int(nk)) / nk) ** 2))
            x = np.abs(boxcar(u, int(nk)))[skip:] / math.sqrt(sigma2)
            ids = np.cumsum(x < x_off)
            m = np.full(int(ids[-1]) + 1, -np.inf)
            np.maximum.at(m, ids, x)
            maxima[k].append(m)
        used_s += (n - skip) / rate_hz
    out = []
    for m in maxima:
        m = np.sort(np.concatenate(m))[::-1]
        rank = max(1, int(round(cfg.false_marks_per_s * used_s)))
        out.append(float(m[min(rank, len(m) - 1)]))
    return out


def pooled_paired(rows, a: str, b: str, edits: str = "edits", symbols: str = "symbols") -> dict:
    """b minus a, signal by signal (edits / symbols of each signal), pooled by group and over all ("all"):
    mean and bootstrap 95% interval over signals."""
    by: dict = {}
    for r in rows:
        if r["scored"] and r[symbols] > 0:
            by.setdefault(r["front_end"], {})[(r["group"], r["recording"], r["index"])] = r[edits] / r[symbols]
    base, var = by.get(a, {}), by.get(b, {})
    diffs: dict = {}
    for key in sorted(set(base) & set(var)):
        d = var[key] - base[key]
        diffs.setdefault(key[0], []).append(d)
        diffs.setdefault("all", []).append(d)
    out = {}
    for group, d in diffs.items():
        v = np.array(d)
        picks = _rng_for((group, a, b, edits)).integers(len(v), size=(BOOTSTRAP_RESAMPLES, len(v)))
        out[group] = {"signals": len(v), "mean": float(v.mean()),
                      "interval": _interval([float(v[p].mean()) for p in picks]) if len(v) >= 2 else None}
    return out


def _table(title, stats, columns):
    lines = [f"### {title}", "", "| group | " + " | ".join(c for c, _ in columns) + " |",
             "|---|" + "---|" * len(columns)]
    for group, v in sorted(stats.items()):
        lines.append(f"| {group} | " + " | ".join(fmt(v) for _, fmt in columns) + " |")
    return lines + [""]


def compare(out_dir, base: str, variant: str) -> Path:
    """Writes experiments/compare-<variant>-vs-<base>.md: pooled and paired CER and first-word CER, and each
    run's speed errors, switches, over starts, false characters and CPU."""
    out_dir = Path(out_dir)
    rows, _ = load_results(out_dir, [out_dir / EXPERIMENT_RESULTS], [base, variant])
    agg = aggregate(rows)
    cer = pooled_paired(rows, base, variant)
    fw = pooled_paired(rows, base, variant, "first_word_edits", "first_word_symbols")
    lines = [f"# {variant} against {base} (development set)", "",
             "Paired: variant minus base, signal by signal; bootstrap 95% intervals over signals.", "",
             "| group | signals | paired CER | paired first-word CER |", "|---|---|---|---|"]
    for group in sorted(cer, key=lambda g: (g != "all", g)):
        f = fw.get(group, {})
        lines.append(f"| {group} | {cer[group]['signals']} | {_with_interval(cer[group]['mean'], cer[group]['interval'], '+.4f')} | "
                     f"{_with_interval(f.get('mean'), f.get('interval'), '+.4f')} |")
    lines += ["", "| front end | group | tag | CER | first-word CER |", "|---|---|---|---|---|"]
    for (fe, group, tag), v in sorted(agg.items(), key=lambda kv: (kv[0][1], kv[0][2], kv[0][0])):
        lines.append(f"| {fe} | {group} | {tag} | {_with_interval(v['cer'], v['cer_interval'], '.4f')} | "
                     f"{v['first_word_cer']:.4f} |")
    lines.append("")
    for name in (base, variant):
        lines += [f"## {name}", ""]
        lines += _table("Selected speed off by more than x1.5 (S500 >= 6 dB)", metrics.speed_errors(out_dir, name, DEV),
                        [("fraction", lambda v: _with_interval(v["error_fraction"], v["interval"], ".4f")),
                         ("lock-ins / transmissions", lambda v: f"{v['lock_ins']} / {v['transmissions']}")])
        lines += _table("Switching", metrics.switch_stats(out_dir, name, DEV),
                        [("switches per min", lambda v: _with_interval(v["switches_per_min"], None, ".3f")),
                         ("alternations per min", lambda v: _with_interval(v["alternations_per_min"], None, ".3f"))])
        lines += _table("Over starts inside transmissions", metrics.spurious_over_starts(out_dir, name, DEV),
                        [("per transmission", lambda v: _with_interval(v["per_transmission"], None, ".4f"))])
        lines += _table("False characters outside transmissions", metrics.false_characters(out_dir, name, DEV),
                        [("per min", lambda v: _with_interval(v["per_min"], None, ".4f"))])
        lines += [f"Decoding CPU: {1000 * metrics.cpu_per_channel_second(out_dir, name, DEV):.2f} ms per channel-second.", ""]
    path = out_dir / "experiments" / f"compare-{variant}-vs-{base}.md"
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")
    return path


def periodicity_table(out_dir, name: str, values: dict, subsets, target: float = 0.95, jobs=None) -> Path:
    """Offline evaluation of values' periodicity estimator on name's stored posteriors: for each window subset,
    the calibrated threshold (lowest reaching `target` precision) with precision, coverage and median time to
    the first confident estimate, and the same at the configured threshold."""
    out_dir = Path(out_dir)
    cfg = ProtoConfig().with_values(**values)
    windows = sorted(cfg.periodicity_windows_s)
    points = metrics.periodicity_points(out_dir, name, cfg, only=DEV, jobs=jobs)
    configured = {"comb": cfg.comb_confidence_min, "edge": cfg.edge_confidence_min,
                  "spectrum": cfg.spectrum_confidence_min}[cfg.periodicity_method]
    tag = "-".join(f"{k}={v}" for k, v in sorted(values.items())).replace(" ", "")
    lines = [f"# Periodicity, offline, on {name}'s posteriors ({tag})", "",
             f"{len(points)} update points inside transmissions (groups {', '.join(metrics.PERIODICITY_GROUPS)}; "
             "S500 >= 0 dB; constant-speed labels). Correct: within 5% of 1.2 s / WPM. Intervals: bootstrap 95% over "
             "channels.", "",
             "| windows (s) | threshold | precision | coverage | median time to confident (s) |", "|---|---|---|---|---|"]
    for subset in subsets:
        threshold, r = metrics.calibrate(points, windows, subset, target)
        for label, th, res in (("calibrated", threshold, r),
                               ("configured", configured, metrics.evaluate_rule(points, windows, subset, configured))):
            if res is None:
                lines.append(f"| {subset} | {label}: none reaches {target} | — | — | — |")
                continue
            lines.append(f"| {subset} | {label} {th:.4g} | {_with_interval(res['precision'], res['precision_interval'], '.3f')} | "
                         f"{_with_interval(res['coverage'], res['coverage_interval'], '.3f')} | "
                         f"{_with_interval(res['median_time_to_confident_s'], None, '.2f')} |")
    path = out_dir / "experiments" / f"periodicity-{name}-{tag}.md"
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")
    return path


def stream(intervals, start_s, duration_s, s500_db, seed, rate_hz=1500.0):
    """A 1 FS carrier keyed by intervals (5 ms raised-cosine edges) from start_s, in white noise giving S500 =
    s500_db: noise power per complex sample rate_hz / (500 Hz x 10^(S500/10)) FS^2."""
    n = int(round(duration_s * rate_hz))
    rng = np.random.default_rng(seed)
    env = keying_envelope(intervals, start_s, n, int(rate_hz)) if intervals else np.zeros(n)
    power = rate_hz / 500.0 * 10 ** (-s500_db / 10)
    return env * np.exp(1j * rng.uniform(0, 2 * np.pi)) + (
        rng.standard_normal(n) + 1j * rng.standard_normal(n)) * math.sqrt(power / 2)


def follow_marks(cfg, seeds) -> list:
    """Spec 4.6's speed jump: 15 -> 30 WPM at the fifth word, S500 = 20 dB. For each seed, the marks sent from
    the step until a branch matched to 30 WPM (within the eligibility tolerance of 0.8 x 40 ms) is selected;
    None if never."""
    text = "CQ CQ CQ DE K1ABC K1ABC K1ABC K1ABC"
    iv = timed_intervals(text, 15.0, wpm_end=30.0, profile="step")
    start = 1.0
    step_s = start + iv[28][0]  # CQ CQ CQ DE has 28 marks
    lengths = realized_lengths_s(cfg, 1500.0)
    counts = []
    for seed in seeds:
        r = ChannelDecoder(cfg, 1500.0).run(stream(iv, start, iv[-1][1] + start + 3.0, 20.0, seed))
        followed = next((t for t, k, _ in r.selections if t > step_s and abs(
            math.log(lengths[k] / (cfg.length_dits * 1.2 / 30.0))) <= cfg.eligibility_tolerance), None)
        counts.append(None if followed is None else sum(1 for a, _ in iv if step_s <= start + a <= followed))
    return counts


def main(argv=None) -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)
    r = sub.add_parser("run")
    c = sub.add_parser("compare")
    p = sub.add_parser("periodicity")
    f = sub.add_parser("follow")
    b = sub.add_parser("batch")
    x = sub.add_parser("calibrate-x-on")
    for s in (r, c, p, b):
        s.add_argument("--out", type=Path, required=True)
    for s in (r, p, f, x):
        s.add_argument("--set", dest="values", action="append", help="KEY=VALUE: a ProtoConfig value")
    for s in (r, p, b):
        s.add_argument("--jobs", type=int, default=None)
    for s in (r, b):
        s.add_argument("--bench", type=Path, required=True)
    r.add_argument("--name", required=True)
    r.add_argument("--keep-p1", action="store_true")
    r.add_argument("--subset", choices=sorted(SUBSETS), default="dev")
    b.add_argument("--spec", type=Path, required=True)
    x.add_argument("--events", type=int, default=20)
    c.add_argument("--base", required=True)
    c.add_argument("--variant", required=True)
    p.add_argument("--name", required=True)
    p.add_argument("--subsets", required=True, help='window subsets, s: "2,5,10;1,2,5,10"')
    p.add_argument("--target", type=float, default=0.95)
    f.add_argument("--seeds", type=int, default=10)
    args = parser.parse_args(argv)
    if args.command == "run":
        print(f"wrote {run(args.out, args.bench, args.name, runner.parse_values(args.values), args.keep_p1, args.jobs, SUBSETS[args.subset])}")
    elif args.command == "batch":
        print(f"wrote {batch(args.out, args.bench, args.spec, args.jobs)}")
    elif args.command == "calibrate-x-on":
        cfg = ProtoConfig().with_values(**runner.parse_values(args.values))
        print(json.dumps([round(v, 4) for v in calibrate_x_on(cfg, events=args.events)]))
    elif args.command == "compare":
        print(f"wrote {compare(args.out, args.base, args.variant)}")
    elif args.command == "periodicity":
        subsets = [tuple(float(x) for x in s.split(",")) for s in args.subsets.split(";")]
        print(f"wrote {periodicity_table(args.out, args.name, runner.parse_values(args.values), subsets, args.target, args.jobs)}")
    else:
        counts = follow_marks(ProtoConfig().with_values(**runner.parse_values(args.values)), range(1, args.seeds + 1))
        found = [c for c in counts if c is not None]
        print(f"marks to follow per seed: {counts}; median {np.median(found) if found else None}, "
              f"max {max(found) if found else None}, never followed in {counts.count(None)} of {len(counts)}")


if __name__ == "__main__":
    main()
```

- [ ] **Step 4: Run the tests to verify they pass**

Run: `.venv\Scripts\python -m pytest training -q`
Expected: all pass.

- [ ] **Step 5: Commit the harness**

```powershell
git add training/kz4ap_proto/experiments.py training/tests/test_proto_experiments.py
```
```powershell
git commit -m "Add the stage-1 experiment harness"
```

- [ ] **Step 6: Start the results document**

Create `docs/plans/2026-09-30-milestone-2b-stage-1-results.md` with this skeleton, filling section 1 now (processor: `Get-CimInstance Win32_Processor | Select-Object -ExpandProperty Name`; the commit: `git rev-parse --short HEAD`; the timings recorded in Tasks 1, 6, 11 and 12):

```markdown
# Milestone 2b, Stage 1: Results Record

Plan: `docs/plans/2026-09-30-milestone-2b-stage-1-filter-bank-prototype.md`. Spec:
`docs/design/2026-09-30-filter-bank-speed-estimator-design.md`. Every number here is measured unless
marked otherwise; every interval is a bootstrap 95% interval (over signals for CER, over channels for
the periodicity and speed statistics). S₅₀₀: key-down carrier power over noise power in 500 Hz, dB.

## 1. Conditions

- Machine: <processor>, Windows 11. Code: <commit>.
- Suite: `build/suite/full3` (3 seeds; 123 recordings with group I). Prototype input: the oracle
  channels' recorded streams (120 test cases, the 27 oracle copies of the recordings decoded through the detector path included), mixed to 0 Hz at the labeled frequency and drift.
- Development set (experiments): `experiments.DEV` — seed 1 of groups A, C, D, E, F, G, H (oracle, both
  views), I, and B's mixed-style recording. Held out: seeds 2 and 3.
- Run times: group I generation <min> and scoring <min> (Task 1); recording the streams <min>
  (Task 12); decoding CPU <ms> per channel-second (Task 11).
- Unknown-amplitude test, noise alone, nominal threshold (Task 6): <count> false key-downs in 200 s at
  branch 1 (R_fa = 0.01 /s targeted; review: 0.07–0.10 /s).
- Noise mask (Task 5, white noise): kept fraction <…>, accepted segments <…> of <…>, mask bias <…>
  (review: 63%, whole-band bias 0.979; the prototype stores a per-branch table).
- Decoding CPU on a keyed channel (Task 11 Step 5): <…> s per channel-second; experiment budget updated
  to <…> h on 14 workers.
- Speed step 15 → 30 WPM (Task 11 test, one seed): followed after <marks> marks.

## 2. Owner's decisions (2026-09-30, after the plan review)

- Noise level: both measured (E10 variants a, b, c); spec §4.2 and §5 say it is decided by E10.
- Periodicity: the comb on Π = 2T with teeth ±15% of T is the default; the edge comb is E1's third variant;
  spec §4.4 and §6 corrected.
- First-mark threshold calibrated by measurement (E9); keyed samples only while the amplitude is unknown.
- The whole experiment list kept; runs in the background.

## 3. Experiments

(One subsection per experiment, in the order run: question; setting(s) and command(s); development
set size (signals, channel-seconds); the table from `experiments/…md`; the decision rule as written in
the plan; its outcome; the value adopted and its status.)

## 4. Final evaluation (Task 15)

## 5. The comparison for the owner (no acceptance gate), and what stage 1 cannot measure

## 6. Open items for the owner
```

- [ ] **Step 7: The development reference**

```powershell
.venv\Scripts\python -m kz4ap_proto.experiments run --out build/suite/full3 --bench build/windows/bench/Release/kz4ap-bench.exe --name exp-ref --keep-p1
```
Start it in the background with its output redirected to `build/suite/full3/experiments/logs/exp-ref.log`; when notified, read `summary-exp-ref.md`. Record the run's wall-clock time and the development set's size in section 1. `exp-ref` is the **current reference**; whenever a rule adopts a variant, that variant's run becomes the current reference for the experiments after it.

- [ ] **Step 8: E10 — the noise level: three-tap level with spectrum ratios, spectrum level, or per-branch**

Question (owner, 2026-09-30: measure both levels; spec §4.2, §5 and §8). Variants: (a) `spectrum` (the reference: three-tap level × spectrum ratios); (b) `spectrum-level` (the masked spectrum's own level ÷ the measured mask bias); (c) `branch` (per-branch three-tap, the spec's recorded fallback). Write `build/suite/full3/experiments/E10.json`:

```json
{"name": "E10", "base": "exp-ref", "subset": "dev",
 "runs": [{"name": "exp-E10-level", "set": {"noise_method": "spectrum-level"}},
          {"name": "exp-E10-branch", "set": {"noise_method": "branch"}}]}
```
and start in the background: `.venv\Scripts\python -m kz4ap_proto.experiments batch --out build/suite/full3 --bench build/windows/bench/Release/kz4ap-bench.exe --spec build/suite/full3/experiments/E10.json > build/suite/full3/experiments/logs/E10.log 2>&1`. When notified, read `summary-E10.md` and the two compare files.
Metric: paired CER (variant − reference) pooled over the development set and per group; group E (a neighbor leaking into short branches) read separately.
**Rule (pre-registered):** a variant qualifies if its pooled paired CER interval lies entirely below 0 and no group's paired interval lies entirely above 0; adopt the qualifying variant with the lower pooled mean; if none qualifies, keep (a). Record the table, the mask numbers of Task 5 and the outcome in section 3; if a variant is adopted, set `noise_method` in `ProtoConfig` with the comment "measured (E10)" and make its run the current reference. Write the outcome into spec §4.2's sentence "decided by measurement (E10)" in Task 15.

- [ ] **Step 9: E1 — periodicity method: comb on 2T (default), edge comb, or spectrum-shape fit**

Question (spec §4.4 as corrected, §8). Offline, on `exp-ref`'s posteriors, the three variants with every logged window, each variant's threshold calibrated to 95% precision with the three default windows (each command in the background; each writes `experiments/periodicity-exp-ref-….md`):

```powershell
.venv\Scripts\python -m kz4ap_proto.experiments periodicity --out build/suite/full3 --name exp-ref --set periodicity_method=comb --set "periodicity_windows_s=[1,2,3,5,10]" --subsets "2,5,10"
.venv\Scripts\python -m kz4ap_proto.experiments periodicity --out build/suite/full3 --name exp-ref --set periodicity_method=edge --set "periodicity_windows_s=[1,2,3,5,10]" --subsets "2,5,10"
.venv\Scripts\python -m kz4ap_proto.experiments periodicity --out build/suite/full3 --name exp-ref --set periodicity_method=spectrum --set "periodicity_windows_s=[1,2,3,5,10]" --subsets "2,5,10"
```
Metric: at each variant's calibrated threshold, coverage (fraction of update points inside transmissions with a confident estimate) and median time from a transmission's start to its first confident estimate; precision is ≥ 0.95 by calibration. **Rule (pre-registered):** the comb stays unless another variant's coverage exceeds the comb's with non-overlapping intervals; among variants that do, the higher coverage; ties on coverage (overlapping intervals) go to the shorter median time to confident, then to the comb (owner's default). If no variant reaches 0.95 precision at any threshold, keep the comb, record "no setting met the criterion", and report to the owner. Also record each variant's precision at its placeholder threshold (the "configured" rows). The pooled points include group C's hand and bug stations, where the review found the comb wrong (hand: 1.13–1.22 T) and the edge comb best at S₅₀₀ ≥ 10 dB; record group C's rows separately in the results if the tables show them apart, otherwise say that the pooled table mixes them.

- [ ] **Step 10: E2 — periodicity windows**

With E1's method, offline:

```powershell
.venv\Scripts\python -m kz4ap_proto.experiments periodicity --out build/suite/full3 --name exp-ref --set periodicity_method=<E1's method> --set "periodicity_windows_s=[1,2,3,5,10]" --subsets "2,5,10;1,2,5,10;2,5;5,10;2,3,5,10;1,2,3,5,10"
```
(in the background, like every command of this task).
Metric: each subset at its own calibrated threshold (precision ≥ 0.95): coverage and median time to confident. **Rule:** among subsets whose coverage is within 0.02 of the best coverage, choose the one with the shortest median time to confident; ties within 0.25 s go to the fewest windows, then to (2, 5, 10). Adopt it only if it differs from (2, 5, 10) under this rule.

- [ ] **Step 11: E3 — teeth and widths of the chosen method**

With E1's method and E2's windows, offline, one command per setting (in the background): comb or edge comb: `comb_teeth` ∈ {3, 4, 5} × `comb_width` ∈ {0.05, 0.075, 0.10} (fractions of Π = 2T: ±10%, ±15%, ±20% of T; the edge comb uses the same widths in T); spectrum: `spectrum_nulls` ∈ {2, 3, 4} × `spectrum_null_width` ∈ {0.10, 0.15, 0.20} (× T). For example:

```powershell
.venv\Scripts\python -m kz4ap_proto.experiments periodicity --out build/suite/full3 --name exp-ref --set periodicity_method=comb --set "periodicity_windows_s=[1,2,3,5,10]" --set comb_teeth=3 --set comb_width=0.05 --subsets "<E2's windows>"
```
Metric: coverage at the calibrated threshold. **Rule:** adopt a setting other than the default (4 teeth, 0.075; spectrum: 3 nulls, 0.15) only if its coverage exceeds the default's by more than 0.02 with non-overlapping intervals; among several, the highest coverage. Record the lag reach of the adopted comb, (teeth + 0.5 + width)·2T, for spec §6.

- [ ] **Step 12: Adopt E1–E3 and confirm end to end**

Set in `ProtoConfig` the method, windows, teeth/nulls and width the rules chose, and the chosen method's confidence threshold to its calibrated value (status "measured (E1–E3)"). Then:

a batch `E1-3.json` with base = the current reference and one run `exp-E1-3` with no `set` (the new defaults), started in the background; read `summary-E1-3.md`.
**Rule:** exp-E1-3 becomes the current reference unless its pooled paired CER interval lies entirely above 0; in that case revert the periodicity defaults, keep the current reference, record it, and report to the owner (a better T_P that makes decoding worse is a design finding). Run `.venv\Scripts\python -m pytest training -q` (tests use explicit settings, so they must still pass).

- [ ] **Step 13: Record and commit**

Write E10 and E1–E3 into section 3 of the results document (each: the command lines, development set size, the tables from `build/suite/full3/experiments/*.md`, the rule, the outcome, the adopted value and its status).

```powershell
git add training/kz4ap_proto/params.py docs/plans/2026-09-30-milestone-2b-stage-1-results.md
```
```powershell
git commit -m "Record the noise and periodicity experiments of stage 1"
```

---

### Task 14: Fit, over-start and selection experiments (E4, E5, E9, E7, E6, E8)

Six more placeholders, one group at a time, each against the current reference, in this order (each adopted value is in the reference for the next). Every run uses the development set; every rule was written before the runs. For each: write a batch file `build/suite/full3/experiments/E#.json` (base = the current reference; one run per variant, named `exp-E#-<value>`, with its `set`), start `experiments batch` in the background with its log redirected, and when notified read only `summary-E#.md` and the compare files it names; apply the rule, record everything in section 3 of the results document, and, if a value is adopted, set it in `ProtoConfig` with the status "measured (E#)" and make that run the current reference. Budget: about 28 development runs in Tasks 13–14, about 25–30 min each on 14 workers (Design decisions, "Compute budget"; Task 11 Step 5's measured cost replaces the estimate).

**Files:**
- Modify: `training/kz4ap_proto/params.py` (adopted defaults only), `docs/plans/2026-09-30-milestone-2b-stage-1-results.md`

**Interfaces:**
- Consumes: `experiments run | compare | follow` (Task 13).
- Produces: settled defaults in `ProtoConfig` for `fit_memory`, the grid, `x_on_values`, `false_marks_per_s`, `rekey_after_s`, `new_over_gaps`, `new_over_min_s`, `switch_persistence`, `quality_tie_nats`, `text_window_chars`.

- [ ] **Step 1: E4 — fit memory N_mem**

Variants: `fit_memory` = 12 and 48 (the reference has 24).
Metrics: pooled and per-group paired CER; group D's rows; the selected-speed error fraction (compare's "Selected speed" table).
**Rule:** adopt a value only if its pooled paired CER interval lies entirely below 0 and neither group D nor group I has a paired interval entirely above 0; if both 12 and 48 qualify, the one with the lower pooled mean. Otherwise keep 24.

- [ ] **Step 2: E5 — grid steps**

Variants: `t_grid_step` = 0.005 and 0.02; "fine": `q_grid=[3,3.25,3.5,3.75,4,4.25,4.5,4.75,5]`, `w_grid=[-0.4,-0.3,-0.2,-0.1,0,0.1,0.2,0.3,0.4,0.5,0.6,0.7,0.8,0.9,1.0]`, `tg_grid=[1,1.12,1.26,1.41,1.59,1.78,2,2.24,2.52,2.83,3.17,3.56,4,4.49,5.04,5.66,6.35,7.13,8]`; "coarse": `q_grid=[3,4,5]`, `w_grid=[-0.4,0,0.4,0.8]`, `tg_grid=[1,1.59,2.52,4,6.35]`. (The finest run is `t_grid_step=0.005` with the fine coarse-grids: `exp-E5-finest`.) The finest grids cost about 15× a run (review: 22.9 ms per fit update against 1.49 ms), so **E5 runs on `DEV_E5`** (`"subset": "e5"` in its batch: seed 1 of group A at 25 WPM, C, D and I, about 20% of the development set), with the reference re-run on the same subset as `exp-E5-ref`; the batch's base is `exp-E5-finest`, listed first.
Metrics: pooled paired CER of every variant against `exp-E5-finest`; decoding CPU per channel-second (compare's last line).
**Rule:** among the variants (including the reference) whose pooled paired CER against the finest has an interval that does not lie entirely above 0, adopt the one with the lowest CPU per channel-second. Record the CPU of each.

- [ ] **Step 3: E9 — calibrate the unknown-amplitude threshold, then R_fa and when to re-key**

(a) **Calibration (pre-registered, adopted as a measurement):** in the background, `.venv\Scripts\python -m kz4ap_proto.experiments calibrate-x-on --set false_marks_per_s=0.01 > build/suite/full3/experiments/logs/E9-calibrate.log`; it prints 32 thresholds (one per branch) at which channel-shaped noise alone keys down 0.01 times per second. Set them as `ProtoConfig.x_on_values` with the comment "measured (E9a, channel-shaped noise, R_fa 0.01 /s)" and record them beside the nominal ones (4.31 … 3.55) in section 3. Run the reference with them (`exp-E9-calibrated`, batch against the current reference) and record the comparison; it becomes the current reference unless its pooled paired CER interval lies entirely above 0, in which case keep it anyway (a measured threshold replaces a wrong formula) but report the loss to the owner.

(b) Variants of the target, each with its own calibration (run `calibrate-x-on --set false_marks_per_s=<R>` first and put the printed list in the variant's `set` as `x_on_values`): `false_marks_per_s` = 0.001 and 0.1 (/s); then, with the outcome, `rekey_after_s` = 0.2 and 0.8 (s of keyed time).
Metrics: paired first-word CER pooled over groups A, G, H (oracle, both views) and I; false characters per minute outside transmissions; pooled paired CER.
**Rule:** adopt a value only if the pooled paired first-word CER interval lies entirely below 0, the false characters per minute do not exceed the reference's by more than 50% in any group, and the pooled paired CER interval does not lie entirely above 0. Otherwise keep the reference's value. (Per sample, the spec's two candidate tests, GLRT and threshold, are one family — Design decisions — so this experiment sets the threshold; record that.)

- [ ] **Step 4: E7 — the new-over silence threshold**

Variants, one at a time: `new_over_gaps` = 8 and 16; then `new_over_min_s` = 0.3 and 1.0 (s).
Metrics: over starts inside transmissions per transmission (all groups, group I separately); paired first-word CER over groups G and H (oracle, both views: each over's first word); pooled paired CER.
**Rule:** first check the reference: if group I shows more than 0.05 over starts inside a transmission per transmission, report it to the owner with the numbers (Farnsworth word gaps are starting overs). Adopt a variant only if its paired first-word CER over G and H lies entirely below 0 and group I's over starts per transmission stay at most 0.05. Otherwise keep max(0.5 s, 12·T_g).

- [ ] **Step 5: E6 — switch persistence M**

Variants: `switch_persistence` = 1, 2, 6, 8 (the reference has 4). The batch file also lists `"follow": [{"set": {"switch_persistence": M}}, …]` for M = 1, 2, 4, 6, 8, so the speed-step follow test (10 seeds) runs in the same background process.
Metrics: marks to follow the 15 → 30 WPM step (median and maximum over 10 seeds; spec §4.6: "within about 10 marks"); switches and alternations per minute; pooled paired CER.
**Rule:** consider only values whose median follow is at most 10 marks and that were never "not followed"; among them adopt the one with the fewest alternations per minute unless its pooled paired CER interval lies entirely above 0; ties keep 4. If no value meets the follow criterion, keep 4 and report to the owner with the follow counts.

- [ ] **Step 6: E8 — the text log-probability's weight and window**

Variants, one at a time: `quality_tie_nats` = 0, 0.02, 0.1, 0.2 (nats per element; 0 disables the tie-break); then `text_window_chars` = 5 and 20.
Metrics: pooled paired CER; paired CER over groups G and H (ragchew text) separately from the filler-text groups (the spec warns that filler text drawn from the model's own frequencies flatters it).
**Rule:** adopt a value only if the pooled paired CER interval lies entirely below 0 and the mean paired CER over groups G and H is not above 0. Otherwise keep the reference's.

- [ ] **Step 7: Re-run the tests, record and commit**

Run: `.venv\Scripts\python -m pytest training -q` → all pass (a test that asserted a now-changed default by value is updated only if it tested the default itself; the channel tests use the defaults and must still pass — if one fails after an adopted change, record it as a finding and report to the owner rather than reverting silently).

```powershell
git add training/kz4ap_proto/params.py docs/plans/2026-09-30-milestone-2b-stage-1-results.md
```
```powershell
git commit -m "Record the fit, over-start and selection experiments of stage 1"
```

---

### Task 15: Final evaluation, the full comparison for the owner, write-back to the spec

The settled configuration on all three seeds, reported against Matched and Envelope on the same oracle signals, every regime included (the oracle copies of the detector-only groups too), the held-out seeds separately, laid out as a full comparison for the owner's decision — there is no acceptance gate (owner, 2026-09-30) — with the stated gap of what only stage 2 can measure, and the measured values written into the spec's §6 and a new stage-1 results section (the write-back was asked for with this plan; the owner reviews it on the branch before merge).

**Files:**
- Modify: `docs/plans/2026-09-30-milestone-2b-stage-1-results.md` (sections 4–6)
- Modify: `docs/design/2026-09-30-filter-bank-speed-estimator-design.md` (§6 table; new §10)
- Modify: `docs/backlog.md` (stage-2 items found here)

**Interfaces:**
- Consumes: `runner decode | score | report`; the settled `ProtoConfig` defaults.
- Produces: `build/suite/full3/results/bank-proto/`, `build/suite/full3/report-bank-proto.md` and `report-bank-proto-held-out.md`, the documents above.

- [ ] **Step 1: Decode and score every oracle recording**

Start the decode (about 2 h on 14 workers) in the background with its log redirected, and the rest when notified:

```powershell
.venv\Scripts\python -m kz4ap_proto.runner decode --out build/suite/full3 --name bank-proto
.venv\Scripts\python -m kz4ap_proto.runner score --out build/suite/full3 --bench build/windows/bench/Release/kz4ap-bench.exe --name bank-proto
.venv\Scripts\python -m kz4ap_proto.runner report --out build/suite/full3 --name bank-proto
.venv\Scripts\python -m kz4ap_proto.runner report --out build/suite/full3 --name bank-proto --only "-s[23](\.stations)?$" --suffix held-out
.venv\Scripts\python -m kz4ap_synth.suites summarize --out build/suite/full3
```
Expected: 153 decoded files (120 oracle scorings, the 27 oracle copies included, and 33 detector-path recordings), 159 scored (the six group-H detector recordings twice: labels per QSO and per station), the two reports, and the suite's `summary.md` now with `bank-proto` rows beside `baseline` and `matched`. Record the wall-clock times.

- [ ] **Step 2: Check the references are untouched**

In Git Bash: `bash bench/smoke.sh build/windows` → `smoke test passed`; and `git diff --stat main -- bench/baselines` → no output (Envelope and the current Matched path unchanged: Envelope smoke CER 0.0353, Matched 0.0436).

- [ ] **Step 3: Read the numbers before writing them down**

Check, before trusting them (these look for broken runs, not for a winner): every group, including the six `…, oracle` copies and the detector-path groups (H, band, crowded, pauses, strong, tune-up, first sample), has `bank-proto` rows with the same signal counts as `matched` and `baseline`; group A's prototype CER falls with S₅₀₀; the rows marked not comparable are the F drift rows and the H oracle QSO-label rows with an offset; the held-out report's group-A crossings are within their intervals of the development seed's (a large difference means the experiments tuned toward seed 1: report it). If something looks broken, debug (superpowers:systematic-debugging) before writing.

- [ ] **Step 4: Write sections 4–6 of the results document**

Section 4: from `report-bank-proto.md` and the held-out report: every group's table (prototype, Matched and Envelope CER with intervals, the paired columns), the group-A crossings, the prototype's own statistics (speed errors and lock-ins, switches and alternations, over starts, false characters, CPU), and the corrections (count and reach distribution from the decoded files: `max`, median; all must be ≤ 20 s).

Section 5: the comparison, plainly and without a verdict of our own (the owner decides whether the prototype is better and whether stage 2 is worth doing):
- group A crossings (S₅₀₀ at CER 0.10 and 0.05) at every speed for the prototype, Matched and Envelope, with intervals, and the differences in dB of S₅₀₀;
- R1 (group D step 20 → 35 WPM CER), R2 (group A 12 WPM first-word CER at S₅₀₀ 6–20 dB) and the two start-up runaway cases, for all three, with numbers; and the prototype's lock-ins at S₅₀₀ ≥ 6 dB from the speed table (a selected speed off by more than ×1.5 for 3 s or longer);
- every regime (group and tag, the oracle copies and the detector-path groups included) against Matched and against Envelope: better, worse or unchanged beyond its interval, by how much, with the counts of each; the not-comparable rows marked (F drift, H oracle QSO labels with an offset); on the detector path the prototype decodes Matched's tracks, so against Envelope (its own tracks) those rows compare per label; the late-opening case (channels opened partway through a transmission) is read from the detector-path first-word CER;
- **detection measures on the detector path**, the prototype beside Matched and Envelope: detection recall, false tracks and tracks per QSO per group, as the bench counts them (a track counts only if it decoded text, so they depend on the decoder); the prototype is compared with Matched per track (the same detector channels) and with Envelope per label;
- **the stated gap:** which tracks the detector opens is unchanged by the redesign; stage 2 confirms the detection measures in C++ behind the live detector with the frequency tracker in the loop (spec §7; backlog "Stage-2 evaluation of the filter bank through the detector"); on the detector path stage 1's prototype mixes by the detector's frequency without the tracker's fine-tuning (±12 Hz), which Matched has.
State that this compares the Python prototype on oracle streams, not the C++ bank.

Section 6: open items for the owner: the comb-on-2T change (if not already answered), the tracker's input in stage 2 (which branch's v and p), every "no setting met the criterion" and every finding reported during Tasks 11–15, the placeholders kept unmeasured, the T_P prior's reinterpretation (a gate at the confidence threshold instead of a weight by the confidence, because the variants' confidences are on different scales); that on the detector path the prototype lacks the tracker's fine-tuning (the tracker is stage 2's); and the spec §8 questions this stage did not measure: 5 WPM keying against fading (the suite has no slow fading recording below 12 WPM), the outlier class's shape, the noise spectrum's FFT size, averaging and guard (only compared with the fallback, E10), and the choices this plan made heuristically (listed in its Parameters table).

- [ ] **Step 5: Write the values back into the spec**

In `docs/design/2026-09-30-filter-bank-speed-estimator-design.md`:
- §4.2: replace "decided by measurement (E10)" with the variant E10 adopted and its numbers.
- §6: for every row an experiment settled, replace the value with the adopted one and the status "placeholder" with "measured (stage 1, E#; results §3)"; for rows kept, write "kept (stage 1, E#: no setting met the rule)" where that is what happened. Add rows for the prototype's settled parameters the table lacks: noise method (E10) and mask bias (Task 5), the unknown-amplitude thresholds, R_fa and W_min (E9), confidence thresholds (E1). Leave owner and heuristic rows as they are.
- Add a section after §9:

```markdown
## 10. Stage 1 results (measured, <date of the final run>)

Prototype on the recorded oracle channel streams of the 3-seed full suite
(`docs/plans/2026-09-30-milestone-2b-stage-1-results.md`). <One paragraph each: group A crossings, R1, R2
and the runaways, and the counts of regimes better / worse / unchanged against Matched and against Envelope,
with the largest differences and their intervals; no verdict (the owner decides).> <One paragraph: detection recall, false tracks and tracks per QSO on the detector path for the prototype,
Matched and Envelope.> <One sentence: what stage 2 confirms — these measures in C++ behind the live detector,
with the tracker.> <One paragraph: the settled parameters and the changes to
this design found in stage 1: which periodicity variant and noise variant won; the unknown-amplitude test is a per-sample threshold on x, calibrated
by measurement.> <One sentence: what stage 2 must decide that stage 1 did not (the tracker's input).>
```

- [ ] **Step 6: Update the backlog**

In `docs/backlog.md`, under the milestone-2b heading (create it after the milestone-2 items if missing), add one line per stage-2 item found: the tracker's input from the bank; every regime where the prototype was worse than Matched or Envelope, with its numbers; any placeholder kept unmeasured. (The item "Stage-2 evaluation of the filter bank through the detector" is already there.)

- [ ] **Step 7: Commit**

```powershell
git add docs/plans/2026-09-30-milestone-2b-stage-1-results.md docs/design/2026-09-30-filter-bank-speed-estimator-design.md docs/backlog.md
```
```powershell
git commit -m "Record stage 1 of the filter-bank redesign: final evaluation and measured parameters"
```

- [ ] **Step 8: Report to the owner**

In a few lines, without a verdict: the group-A crossings against Matched and Envelope, R1, R2, the runaways, and how many regimes are better, worse and unchanged against each (with the largest differences), the detection measures (recall, false tracks, tracks per QSO) for all three, the stated gap (stage 2 confirms them behind the live detector with the tracker), the settled parameter values and which stayed placeholders, the findings reported along the way (comb on 2T; tracker input; any rule no setting met), the prototype's CPU (Python, for scale only), and that the spec's §6 and §10 were changed on the branch for the owner's review. Do not push or merge.

---
## Self-review (plan author's check against the spec)

**Spec coverage.**

| Spec requirement | Where |
|---|---|
| §7 stage 1: record each oracle channel's stream from `kz4ap-bench` | Task 2 (tap, recorder, `--record-channels`); recorded before the tracker, justified in Design decisions |
| §7 stage 1: process the streams of `build/suite/full3` (3 seeds) in Python | Tasks 4–12 (`kz4ap_proto`), Task 12 Step 8 (recording the suite), Task 15 (all seeds) |
| Scoring with exactly the bench's scoring | Task 3 (`report.cpp` shared by both paths; end-to-end identity check) |
| §7: Farnsworth-spaced text in the generator, tested, in a full-suite group; smoke unchanged | Task 1 (`farnsworth_gap_s`, group I, labels unchanged when unset, smoke check) |
| §4.1 bank: 32 fixed boxcars, 9.6 ms × 1.1^(k−1) | Task 4 (ladder, samples, boxcar, response; tests) |
| §4.2 shared noise spectrum, σ_v,k² = ∫S_n\|H_k\|² df, mark guard; per-branch fallback, selectable; the level decided by measurement (owner) | Task 5 (three variants; mask bias measured), E10 |
| §4.3 amplitude, LLR, ±1 nat hysteresis, per-branch squelch, timing | Task 6 (keyer, squelch scaling, length-preservation test), Task 11 (timing with group delay removed) |
| §4.4 periodicity (as corrected 2026-09-30): comb on 2T (default), edge comb and spectrum-shape fit, parallel windows, shortest confident; only a prior and a fallback | Task 9 (three variants), Task 7 (prior), Task 10 (fallback), E1–E3 |
| §4.5 fit: classes, log-normal plus resolution term, outliers, ~24-element memory, global grid then local refinement, T_P prior, outputs and thresholds | Task 7 (derived priors and resolution term; recursive tables; WLS refinement; `classify_*`), E4, E5 |
| §4.6 selection: eligibility, Q, text tie-break (VE3NEA weights, invalid codes very unlikely), longer branch, fallbacks, stickiness M; follow a jump within ~10 marks | Task 8 (text model), Task 10, Task 11 (`test_follows_a_speed_step_within_ten_marks`), E6, E8 |
| §4.7 silences: nothing else resets; new over at max(0.5 s, 12·T_g); fresh fit and amplitude with the previous as fallback; first marks with an unknown-amplitude test, then re-keyed with the full LLR as a correction | Task 6 (`start_over`, x-threshold test, `rekey`), Task 11 (`start_over`, `rekey_over`), E7, E9 |
| §4.8 corrections, reach 20 s, after a switch from the character where the new branch became eligible | Task 11 (`Output`, `_char_start_at`, `eligible_since`; reach test) |
| §6 placeholders settled by measurement | Tasks 13–14 (E1–E10, rules written before running), Task 15 (write-back) |
| §7 (owner, 2026-09-30): no acceptance gate; a full comparison for the owner, every regime included, through the detector too; only the detector's own measures stated as stage 2's | Task 11b (detector channels, tracks scored by frequency, anchored mixing), Task 12 (`report.comparison`, oracle copies, `engine-copies`, detector jobs), Task 15 Step 4 section 5, backlog item |
| §8 open questions | E1–E3 (method, windows, confidence), E5 (grid), E6 (M, alternations), E9 (unknown-amplitude test, re-key timing), E10 (noise estimator); the rest listed as open in Task 15 |
| Runner reuses `suites.py`'s summary machinery | Task 12 (`load_results`, `aggregate`, `paired_differences`, bootstrap) |
| Project rules: physical units; signal-processing.md; Envelope/Matched bit-identical; baselines untouched; git | Global Constraints; §11 updates in Tasks 1–3; smoke checks in Tasks 1, 2, 3, 15 |

**Placeholder scan.** The only unfilled values are measurements that exist only after running: the results document's fill-ins (`<processor>`, run times, counts) and the spec's §10 text in Task 15, each naming the file or command it comes from; experiment outcomes are left to their decision rules by design. No step says "add tests" without the tests or "similar to Task N".

**Type consistency.** Checked across tasks: `ProtoConfig` fields and `with_values` (Task 4) in every module and in `runner.parse_values`; `branch_samples`/`realized_lengths_s` (Task 4) in Tasks 5, 10, 11, 13; `ThreeTapNoise.update(P, n0, n1)`, `SpectrumNoise/BranchNoise.update(u, P, n0, n1)` and `sigma2()` (Task 5) in Task 11; `BankKeyer.step -> (key, p, before, a)`, `start_over`, `finish_over_start`, `rekey_weight`, `weight`, `prev_amp2`, `a_min`, `rekey(P, sigma2, amp2, cfg, a_min)`, `edges(key, before, n0)` (Task 6) in Task 11; `DurationFit.add/best(prior_t_s, prior_weight)/copy`, `Fit.theta`, `observations_loglik`, `classify_mark`, `classify_space`, `resolution_var_s2` (Task 7) in Task 11; `Periodicity.push/update -> (T, confidence, window, updated)`, `per_window`, `update_every` (Task 9) in Tasks 11 and 12; `BranchView`, `Selector.update(views, instants, t_now, prior_t_s)`, `eligible_since` (Task 10) in Task 11; `ChannelResult.to_json` keys (`chars`, `selections`, `periodicity`, `over_starts`, `corrections`) and `p1` (Task 11) in Task 12's runner and metrics; `runner.decode/score` signatures (Task 12) in Task 13; the bench's `channels.json` keys (Task 2) in `streams.load_channel` (Task 4); the decoded-text file (`front_end`, `recording`, `texts`) between Task 3 and Task 12; `suites.load_results(out_dir, results_dirs, front_ends)` and `paired_differences(rows, a, b)` (Task 12) in Tasks 12–13.

**Review Focus.** Five items, each with its tests in the owning tasks (Tasks 5, 6, 7, 11), named in the Review Focus section; the 20 s correction reach is pinned too.

**Known risks for the executor.** None of the Python or C++ here has been run; the numbers the tests expect are derived, and the periodicity estimator and the grid part of the fit were checked in a scratch numpy port while planning (Design decisions). Tasks 11's channel tests are the first run of the whole chain: debug code defects; report design findings with numbers and mark the test `xfail(strict=True)` rather than loosening it. Full passes are long (about 370 000 channel-seconds of Python); the development set is about 80 000; Task 11 Step 5 measures the cost before any pass. The recorded streams take about 4.4 GB in `build/`. The owner decided the open points on 2026-09-30 (comb on 2T as the default with the edge comb as a third E1 variant and ±15%-of-T teeth; both noise levels measured in E10; the first-mark threshold calibrated in E9; keyed-sample weighting while the amplitude is unknown), and the spec's §4.2, §4.4, §5 and §6 were corrected to match; no owner step remains in the tasks.
