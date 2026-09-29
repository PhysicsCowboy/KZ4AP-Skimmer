# Milestone 2, Part 1: Benchmark Scenarios, Frequency Re-centering, and the Dit-Matched Front End — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Extend the synthetic benchmark so it exposes the decoder's known weaknesses, then build step 1 of the decoder plan (spec §5.2): per-station frequency re-centering with drift tracking, and a dit-matched pre-detection filter whose envelope becomes a Rician-versus-Rayleigh log-likelihood ratio that the existing baseline decoder can key from, selectable against the old path and measured against it.

**Architecture:** The Python generator (`training/kz4ap_synth`) gains a message-text module (CQ calls, contest exchanges, whole ragchew QSOs with prosigns in their operating positions, and filler text with VE3NEA's on-air statistics), signal options (pauses, tune-up carriers, drift, VE3NEA's keying styles, speed changes, Rayleigh fading with a Gaussian or VE3NEA's Butterworth Doppler spectrum, interferers, crowding), two-station QSOs whose alternating overs share one frequency, and named suites with a runner that calls `kz4ap-bench` and summarizes results. `kz4ap-bench` gains an oracle mode (channels at labeled frequencies, detector bypassed), separate scoring of word spaces and of each transmission's first word, and CPU time per channel-second. In the engine, two new per-station components run at the channel rate r = 1500 samples/s inside the Classical decoder when `FrontEnd::Matched` is selected: a `FrequencyTracker` (numerically controlled oscillator plus a lag-product frequency discriminator) and a `MatchedFrontEnd` (boxcar filter matched to the dit, running noise and amplitude estimates, per-sample LLR). The detector alone decides which station a channel follows (owner decision 2026-09-29, "option 1"): each detector track follows its own spectral peak within the channel distance D = 47 Hz of its current frequency, a peak farther away is a separate track, and a channel's tracker only fine-tunes within ±12 Hz of the detector's current frequency for its track (its anchor), so the channel retunes when the detector's track moves. No channel merging. `FrontEnd::Matched` becomes the default in Task 13 (owner, 2026-09-29); the baseline path (`FrontEnd::Envelope` with the milestone-1 detector rules, `--front-end envelope`) stays selectable and bit-identical, and CI keeps pinning it.

**Tech Stack:** C++20, CMake ≥ 3.25, GoogleTest 1.17.0, nlohmann/json 3.12.0, Python 3.12 with numpy and pytest, GitHub Actions. No new dependencies.

**Spec:** `docs/design/2026-09-25-kz4ap-skimmer-design.md` (binding), especially §3.1 (development order), §5.2 step 1 (front end and its prerequisite, re-centering), §5.4 (the benchmark decides). Supporting documents: `docs/signal-processing.md` (what the code does now), `docs/backlog.md` section 1, `docs/research/decoder-survey.md` (option 1 and the benchmark scenario table), `docs/research/proakis-ook-notes.md`, `docs/research/deepcw-generator-notes.md` (VE3NEA's DeepCW generator: keying styles, fading spectrum, SNR convention, text statistics; MIT).

## Scope of this plan

Implements, from the spec and backlog:

- **A. Benchmark scenarios** (spec §5.4; backlog "Benchmark scenarios to add first"; survey scenarios A–F): pauses between transmissions, stations present from the first sample, tune-up carriers, carrier offset and slow drift, keying styles with VE3NEA's timing parameters and per-operator imbalance, speed changes, Rayleigh fading with a Doppler-spread parameter (VE3NEA's spectrum shape, f_D grid, SNR points and style mix reproduced as an external anchor), realistic message text (CQ calls, contest exchanges and whole ragchew QSOs; owner decision 2026-09-27: test transmissions must include full ragchews, not only CQs and contest exchanges), two-station QSOs whose overs alternate with each operator's own speed, style and imbalance, the answering station 0–200 Hz from the caller (owner, 2026-09-27), reported in three regimes (one track, ambiguous, two tracks), interferers at a stated relative power and spacing, strong signals up to S₅₀₀ = 60 dB, a configurable minimum station spacing down to zero, and a 10–60 WPM speed range. `kz4ap-bench` scores word spaces separately from characters, scores each transmission's first word (and each transmission) separately, reports VE3NEA's no-space CER and CPU time per channel-second, and stays deterministic. Named suites: `smoke` (CI, unchanged) and `full` (local, sized for 3 seeds: at least 1000 characters per S₅₀₀ point, bootstrap 95% intervals on every rate).
- **B. Frequency re-centering and drift tracking** (spec §5.2 step 1, prerequisite; backlog "Track frequency drift").
- **C. The dit-matched front end with soft likelihoods** (spec §5.2 step 1; survey option 1; backlog "Channel filtering, two stages", stage 2), consumed by the existing baseline decoder through LLR keying, with the old path kept selectable.
- **D. Measurement** of the baseline against the new front end on the new suites, written into the documents, and a CI guard for the new path.

**Decision (2026-09-29): option 1.** The first channel-distance design (D used for attribution, a tracker that followed any station within D + 10 Hz and walked its anchor, and channel merging) failed in simulation where a station 60–100 Hz away was at the channel's station's level or stronger (Design decisions B, "The first design, rejected"). The owner chose option 1 (below): the detector alone decides which station a channel follows, and the tracker only fine-tunes around the detector's frequency. It was simulated before this revision (Design decisions B, "Option 1, simulated"); merging never fired in 1080 runs and is dropped. Tasks 10, 12 and 13 are written for option 1 and may proceed; Tasks 1–9 and 11 are unchanged.

**Owner decisions of 2026-09-29** (they replace the 2026-09-27 deferral of the default; decision 1 as amended by option 1, below):
1. **One channel distance D, in Hz** (Design decisions, B; Tasks 10, 12, 13). It replaces the tracker's ±35 Hz pull-in and the 35–70 Hz gap that pull-in left. Its use in the tracker (D + 10 Hz) and in channel merging was replaced by option 1 the same day.
2. **`FrontEnd::Matched` becomes the default in Task 13.** `--front-end envelope` stays selectable; CI keeps the milestone-1 smoke regression on Envelope (bit-identical pin) and adds a Matched smoke check (Task 14). If Task 14 finds Matched worse than Envelope in any regime, the implementer reports it to the owner and does not revert the default.
3. **Speed-estimate growth limit:** while the matched filter follows the speed, the dit estimate may grow by at most ×1.25 per mark (Task 12).
4. **Tests with a simulated pass rate below 100%** run 20 fixed seeds and assert a pass count with a wide binomial margin below the simulated rate (Tasks 12, 13). They are deterministic and still catch regressions.
5. **Postponed to the backlog, with the explanation:** the acquisition floor, the noise-rise recovery, stray noise after a silence, a co-channel station keying at the same time within a few tens of Hz, and relabeling a channel's callsign after a turnover (GUI and callsign matching).
6. **No parameter tuning** without strong evidence: the benchmark exists to find better values. Tuning stays deferred to the benchmark results; the parameter values below stay labeled heuristic.
7. **Physical units (standing rule):** parameters and calculations are in Hz, s, FS and dB with a named reference, never in units tied to an implementation choice (FFT bins, samples, decimation). Conversion to bins or samples happens only at the point of use, derived from the physical value, so changing the bin width or a sample rate keeps the physics the same. Config fields are named for their unit (`attribution_distance_hz`, not `..._bins`). Every milestone-1 bin- or sample-counted setting this plan touches is restated in Hz; the ones it does not touch are listed in one backlog item.

**Owner decisions of 2026-09-29, option 1** (after the simulation of the first design; they amend decision 1 above; simulation in Design decisions B, "Option 1, simulated"):
1. **The detector alone decides which station a channel follows.** Each detector track follows its own spectral peak: every frame it moves to the strongest peak (by the birth rule's peak test, at least 3 dB above the floor, frequency by parabolic interpolation in dB) whose frequency is within D = 47 Hz (heuristic) of the track's current frequency; with none, it holds. A peak beyond D is a separate track. The channel retunes when the detector's track frequency moves: slowly, with drift, or at once, when a turnover within D moves the peak to the answering station (Task 13).
2. **The tracker only fine-tunes**, within ±12 Hz (heuristic, `fine_tune_hz`) of its *anchor*, which is the detector's current frequency for its track (minus the channel center), set by the engine before every channel block; the anchor never follows the tracker's own estimates. If the anchor moves more than 12 Hz from the NCO, the NCO jumps to it and the frequency average restarts. The first design's follow distance (D + 10 Hz) and anchor walk are removed (Task 10).
3. **The ×1.25 per-mark growth limit also applies to the filter's first follow step after an acquisition or re-acquisition** (Task 12): the matched filter's dit grows at most ×1.25 per mark from the width it has (the 20 ms acquisition dit), until it reaches the decoder's estimate. Without it the simulation found a filter runaway (K ≥ 200 samples, a 133 ms filter, about 7 WPM or slower) in 12 of 30 runs at 50 Hz, 0 dB re A's key-down power; the traced run reached K = 288 (5 WPM).
4. **No channel merging.** It never fired in 1080 simulated runs with option 1. `merge_channels`, `SignalDetector::remove`, `SignalDetector::retune` and their tests are removed from Task 13.
5. **Only slow drift matters** (owner); fast drift (for example 3 Hz/s) is out of scope and not tested. Task 13's drift test is 1 Hz/s.
6. **Stated limits, not fixed, to the backlog** with plain explanations and the simulated numbers (`docs/backlog.md`): (a) a neighbor 60–70 Hz away at the channel's station's level or stronger leaks through its boxcar filter's first sidelobe (about −18.7 dB relative to a centered station at 60 Hz, K = 58; the K = 24 acquisition filter about −19.6 dB at 70 Hz), is keyed in fragments and corrupts the speed estimate (A's next over intact 6 of 30 at equal level and 0 of 30 at +6 dB re A, at 60 Hz); the fix belongs in the filter's design (for example a tapered filter), later, with the postponed co-channel item; (b) after a turnover to a station on a different frequency within D, the retune waits for the detector's 1 s spectrum average (median 1.4–1.8 s at 25 Hz, 2.33 s at 40 Hz and −10 dB re A); median characters of the answering station lost at the start of its over: 0 at 10 Hz; at 25/40/50 Hz, 1/3/3 at −10 dB re A, 0/1/2 at −6 dB, 0 at 0 and +6 dB; a station that pauses and resumes on its own frequency loses nothing; it joins the backlog item "Wrong or missing first characters" (replay); (c) the rejected variant with a 0.2 s spectrum during re-acquisition, and why.
7. **All earlier decisions stand:** Matched the default in Task 13; the Envelope path selectable, bit-identical and pinned in CI; tests with simulated pass rates below 100% run 20 fixed seeds with a wide binomial margin; no parameter tuning; physical units; `docs/signal-processing.md` in the same commit as any signal-processing change; the git rules.

Deliberately **not** in this plan (each gets its own plan later):

- The neural decoder (spec §5.2 step 2), the Bell-style explicit-duration HMM (step 3), hybrids (step 4), and the benchmark candidates of §5.4.
- morseformer integration as a reference decoder (spec §5.3; backlog "Integrate morseformer as a reference decoder").
- Callsign matching (spec §6), the telnet server, and the GUI and live display (spec §3.1 items 2–5).
- Real-recording scoring with manta's oracle method. This plan builds the oracle *mechanism* (channels at given frequencies, detector bypassed) for synthetic recordings; running it on real recordings needs recordings and RBN spot files and is later work.
- Moving a channel's center bin as a station drifts (the channelizer stays where the track was born; the NCO covers ±75 Hz around it), replay of the first seconds of a transmission ("Wrong or missing first characters"), the tune-up-carrier speed bug, ghost tracks beside strong signals, and separating station identity from decoding. The new scenarios *measure* all of these; fixing them is later work.
- Restating the milestone-1 settings this plan does not touch in physical units (FFT size, hop, channel bins, candidate persistence counted in frames, and the rest listed in the backlog item "Express the remaining bin- and sample-counted settings in physical units"). The detector settings this plan touches (the attribution rule, the peak neighborhood, the track-level neighborhood and the candidate step) are restated in Hz in Task 13.
- Impulsive noise (QRN), chirp, and CPU measurements on a Raspberry Pi 5 (no Pi in the loop yet).
- Tuning the Matched front end's parameters: deferred to the benchmark results (owner, 2026-09-27 and 2026-09-29). The limits the plan states rather than fixes are postponed to the backlog with their explanations (owner, 2026-09-29): the acquisition floor (S₅₀₀ = −2.5 dB derived at every speed; 50% of marks keyed near −1.8 dB at 25 WPM and −2.6 dB at 12 WPM, simulated); the noise-rise recovery (about 43 s after a sustained 6 dB rise, derived); stray noise keyed after about 1% of silences; and a second station keying at the same time within a few tens of Hz of the channel's station. With option 1 (owner, 2026-09-29) three more are stated, not fixed: a neighbor 60–70 Hz away at the channel's station's level or stronger leaking through the boxcar's sidelobes; the retune delay after a turnover to a station on another frequency within D (the detector's 1 s average; up to 3 characters lost, median, for a weak answering station 40–50 Hz away; nothing for a station that resumes on its own frequency; joins the replay item); and the rejected 0.2 s spectrum variant.
- A ragchew clip in the `smoke` suite: `smoke` must stay exactly the recording `bench/smoke.sh` makes, so its baseline (`bench/baselines/smoke.json`) and CI behavior do not change. A clip would either change the CI recording (and so its baseline) or make `smoke` differ from what CI runs, so `smoke` is left unchanged. Ragchews and two-station QSOs are in `full` only.

## Global Constraints

- C++20; CMake minimum 3.25; the engine must not depend on Qt; no new third-party dependencies.
- Namespace `kz4ap`; public headers in `engine/include/kz4ap/`, sources in `engine/src/`, tests in `engine/tests/`; bench code in `bench/src/`, `bench/tests/`; Python in `training/kz4ap_synth/`, `training/tests/`.
- Determinism (spec §4.1): the same recording and settings must produce identical output, regardless of how input is split into calls. Every new per-sample stage runs sample by sample inside the decoder, so chunking cannot change it; every random draw in the generator comes from a seeded generator.
- **The baseline stays bit-identical.** With the Envelope path (`FrontEnd::Envelope`, the milestone-1 bin attribution rule, no channel merging, no oracle; the defaults until Task 13, and `--front-end envelope` from Task 13 on), the engine's output and the `smoke` recording's bytes must not change. `bench/baselines/smoke.json` is not edited by any task.
- **Physical units (owner's standing rule, 2026-09-29).** Parameters and calculations are stated in physical units (Hz, s, FS, FS², dB with a named reference), never in units tied to an implementation choice (FFT bins, samples, decimation factors). Convert to bins or samples only at the point of use, from the physical value (for example `std::lround(distance_hz / bin_hz)`), so a different bin width or sample rate keeps the physics the same. Name config fields for their unit (`attribution_distance_hz`, `lag_s`). Counts of physical things (marks, dits, independent samples of a filter) are fine. (Every implementer of this plan must be told this rule, together with the signal-processing.md rule below.)
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
| τ_L | lag of the frequency discriminator (L = τ_L·r samples at the point of use) | 5.33 ms (8 samples at r = 1500 samples/s); unambiguous range ±1/(2τ_L) = ±93.75 Hz |
| z[n] | lag product v[n]·conj(v[n−L]) | FS² |
| Z̄ | weighted average of lag products, rotated to absolute offset | FS² |
| τ_f | time constant of that average, in seconds of weight 1 | 0.5 s |
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
| a_min | squelch: no key-down unless a ≥ a_min = 3·(T_v/T_acq)^(1/4), T_v = K/r the filter's duration and T_acq = 16 ms the acquisition filter's (0.8 × the 20 ms dit of 60 WPM); in samples, 3·(K/24)^(1/4) at r = 1500 samples/s | 3.0 at 16 ms (K = 24); 3.74 at 38.7 ms (K = 58); 5.58 at 192 ms (K = 288) |
| κ, κ_n | noise guard: σ̂² is updated from v[n−K] only if \|v[n−K]\|²/(2σ̂²) < κ and \|v\|²/(2σ̂²) < κ_n at n and n−2K | 1.75, 4 |
| m(κ) | mean of an exponential of mean 1 truncated at κ, 1 − κ·e^(−κ)/(1 − e^(−κ)); the noise update divides it out | 0.632 at κ = 1.75 |
| — | re-acquisition silence: key up for longer than max(0.5 s, 12 dits); window before returning to the narrow filter (and to the previous speed window) | s; window 2 s |
| F, c | noise floor: 10th percentile of 64 samples of \|v\|² taken K apart, over 2·(−ln(1 − 0.1/c))·2.5 = 2·(−ln 0.6)·2.5; c = 0.25 the smallest fraction of those samples one station is assumed to leave clean | FS²; c dimensionless |
| D | channel distance: a detector track follows its own spectral peak within D of its current frequency, and a new peak within D of a track's current frequency belongs to it | 47 Hz (heuristic) |
| f_a, F_t | tracker anchor (the detector's current frequency for the channel's track, minus the channel center; set by the engine before every channel block) and the fine-tuning range around it | Hz; F_t = ±12 Hz (heuristic) |
| τ_n, τ_a | time constants of the noise and amplitude estimates: seconds of noise updates (noise) and seconds of key-down weight (amplitude) | 2 s, 0.5 s |
| S₅₀₀ | key-down carrier power over noise power in 500 Hz | dB |
| E/N₀ | key-on energy per element over one-sided noise density (Proakis) | dB re 1 |
| f_D | Rayleigh-fading frequency spread: 2σ of a Gaussian Doppler spectrum, or 2σ of the Gaussian least-squares fit to VE3NEA's Butterworth spectrum (1.01·f_D for his filter, so the same f_D to about 1%) | Hz |
| f_c | −3 dB cutoff of VE3NEA's fading spectrum, S(f) ∝ 1/(1 + (f/f_c)⁴), f_c = 0.625·f_D | Hz |
| ρ | VE3NEA's SNR: fading-averaged key-down signal power over noise power in 3 kHz (0 to 3 kHz, real audio); S₅₀₀ = ρ + 10·log₁₀(3000/500) = ρ + 7.78 dB for white noise | dB |
| μ, σ_ln | mean and standard deviation of ln(duration / T) of one element or space (log-normal keying) | 1 (natural-log units) |
| δ | one operator's key-on/key-off imbalance: added to every mark, subtracted from every space; `imbalance_dits` = δ/T | s; VE3NEA: δ ~ N(0, (0.1·T)²) |
| Δf_B | carrier offset of the answering station from the calling station in a two-station QSO | Hz; 0–200 (grid 0, 10, 25, 50, 100, 200 in the suite, plus drawn values) |
| t_turn | silence between one over's last key-up and the next over's first key-down | s; uniform 0.5–2.0 |

## Design decisions

Each choice is labeled **derived** (follows from the math, with its source), **heuristic** (a judgment call), or **to be measured** (Task 14 measures it). All numbers assume r = 1500 samples/s.

### Where the new stages sit

- **Inside the Classical decoder, per sample** (heuristic, for determinism and simplicity). The decoder already owns the dit estimate the filter must follow and already processes sample by sample; putting the tracker and the front end there keeps the output independent of chunking and lets the filter follow speed changes on the very sample the estimate changes. The decoder reports its frequency estimate in each `DecodeUpdate`, so the engine can publish the refined frequency; the engine gives the decoder its anchor, the detector's current frequency for the track, before every channel block (option 1).
- **Two-stage filtering** (spec §5.2): the channelizer's ±150 Hz filter is unchanged; the narrow filter is a second stage at r.
- **Selectable:** `ClassicalDecoderConfig::front_end` is `FrontEnd::Envelope` (today's path) or `FrontEnd::Matched`; Envelope is the default until Task 13, which makes Matched the default (owner, 2026-09-29). `kz4ap-bench --front-end envelope|matched` selects it (`baseline` is accepted as another name for `envelope`, because the suites of Task 9 use it); `envelope` also selects the milestone-1 detector rules (bin attribution, track frequency fixed at birth), so it reproduces milestone 1 bit for bit.

### B. Frequency re-centering

- **Initial estimate: the detector's interpolated peak** (existing; measured +999.8 Hz for a station at +1000.0 Hz). The engine passes Δf₀ = (track frequency − channel center) to the decoder, which starts the NCO there. This removes most of the ±11.7 Hz bin-rounding offset before any signal-based estimate exists.
- **NCO:** u[n] = y[n]·e^(−jφ[n]), φ[n+1] = φ[n] + 2π·f̂/r, wrapped to [−π, π) (derived: a complex frequency shift).
- **Estimator: a lag-product (phase-increment) discriminator on the matched-filter output**, gated by the front end's key-down posterior (heuristic choice among standard estimators; the discriminator itself is derived). For a tone at Δf, v is still a tone at Δf − f̂ (any linear filter passes a tone unchanged in frequency), so arg(v[n]·conj(v[n−L])) = 2π(Δf − f̂)·L/r. Rotating each product by e^(+j2π·f̂·L/r) turns it into a measurement of the absolute Δf, independent of the NCO setting, so there is no feedback loop to stabilize (to first order: v averages samples mixed under the last K/32 NCO settings while each product is rotated by the current f̂, a small coupling while f̂ moves, harmless because τ_f·r = 750 samples ≫ K/2; review finding M3):
  - Z̄ ← Z̄ + α·p·(z·e^(j2π·f̂·L/r) − Z̄), α = 1 − e^(−1/(τ_f·r));
  - every 21.3 ms (`update_interval_s`; 32 samples at r, one channelizer block), if the average holds enough weight (below), f̂ ← arg(Z̄)/(2π·τ_L), clamped to ±75 Hz.
  - Weighting by p freezes the estimate during key-up and pauses (derived: a zero-weight update changes nothing), so it holds through gaps between transmissions.
  - The estimate moves the NCO only once the accumulated weight W (W ← W + α·p·(1 − W), from 0 toward 1) reaches 0.6 (heuristic; 0.3 in an earlier draft let a mixture of two stations' products move it), so the first few noisy products cannot throw away the detector's initial estimate.
- **Lag τ_L = 5.33 ms** (`lag_s`; L = τ_L·r = 8 samples at r; heuristic within derived bounds): the unambiguous range is ±1/(2τ_L) = ±93.75 Hz (derived), which covers the ±11.7 Hz bin rounding plus drift up to the ±75 Hz clamp. A larger L would lower the noise (the error scales as 1/L) but narrow the range.
- **±75 Hz clamp** (heuristic): the channel filter loses 0.34 dB relative to the passband at 75 Hz (measured, signal-processing.md §7). Beyond that the channel itself would have to move (out of scope).
- **Why on v, not on u:** v is only B_v = r/K wide (about 26 Hz at 25 WPM), so the estimate sees about 10 dB less noise than on the 252 Hz channel, and a neighbor 100 Hz away is attenuated by the boxcar's sinc response (for example 29.7 dB relative to the passband at 25 WPM, K = 58: |sinc(100 Hz · 58/1500 s)| = 0.033). The tracker's fine-tuning range (±12 Hz around the detector's frequency for its track) and how it fits the detector and the engine are the next bullet (owner decisions 2026-09-29, option 1; they replace the ±35 Hz pull-in of the re-review). The filter starts wide (60 WPM, main lobe ±62.5 Hz) until the speed estimate is trusted, and returns to that width after a long silence (re-acquisition, C). While K = 24 (a 16 ms filter), a neighbor 100 Hz away is only 14.5 dB down relative to a centered station (|sinc(100 Hz · 16 ms)| = 0.19, derived) and lies beyond the ±93.75 Hz unambiguous range, so it aliases to −87.5 Hz; in a simulation of `MatchedIgnoresStrongerNeighbor` (3 seeds; plan review) with the earlier tracker, f̂ swung to −9 … −11 Hz in the first second before K narrowed (review finding M4); with the anchor, follow-distance and coherence checks of the first design it swung at most 1.9 Hz and ended at 0 ± 0.05 Hz (with option 1 the tracker cannot leave ±12 Hz of the detector's frequency at all).
- **Channel distance D = 47 Hz; the detector decides the station (heuristic; owner decisions 2026-09-29, option 1).** One distance, in Hz, used by the detector; the tracker only fine-tunes around the detector's frequency:
  1. **The detector decides which station a channel follows** (Task 13). Each track follows its own spectral peak: every frame, before the track's level is read, it moves to the strongest bin that (i) is a peak by the birth rule (the maximum within ±47 Hz, `peak_radius_hz`; ties go to the lower bin), (ii) stands at least 3 dB above the floor (the 6 dB birth threshold minus the 3 dB hysteresis: the keep-alive level), and (iii) whose frequency, parabolically interpolated in dB, is within D of the track's current frequency; the track's frequency becomes that interpolated frequency. With no such peak (the station is silent, or only a stronger neighbor's skirt is there, which is not a local maximum) the track holds its frequency. A new peak within D of a track's current frequency belongs to that track; one farther away can become a track of its own. So in a turnover within D the track moves to the answering station B once B's bin is the maximum of the 1 s power average, and back to A when A resumes; beyond D, B gets its own track. The milestone-1 rules (the frequency fixed at birth; a peak less than 3 bins from the track's bin belongs to it) stay selectable (`Attribution::Bins`) for the bit-identical Envelope path.
  2. **The tracker fine-tunes** (Task 10). Its anchor f_a is the detector's current frequency for the track minus the channel center, which the engine sets before every 21.3 ms channel block; the anchor never follows the tracker's own estimates. The tracker accepts its own estimate only within ±F_t = ±12 Hz of f_a; otherwise it empties its average and returns the NCO to f_a. When f_a moves more than 12 Hz from the NCO (the detector's track moved to another peak, or drifted beyond the fine-tuning), the NCO jumps to f_a and the average restarts (weight 0). The decoder is not told to re-acquire on such a jump (heuristic, as simulated). **±12 Hz is heuristic (owner's value).** Bounds it must respect: above the detector's interpolation error (0.2 Hz measured for a clean station; the interpolation is clamped to ±0.5 bin, ±11.7 Hz) plus the difference between the detector's and the tracker's lag behind slow drift (0.3–0.6 Hz at 1 Hz/s, simulated below), and well below D, so that the tracker cannot walk toward a station the detector gives another track. The channel's published frequency is the channel center plus the tracker's f̂.
  3. **No channel merging** (owner decision 4 of option 1): with each channel held within 12 Hz of its own detector peak, two channels never followed one station in the simulation (0 merges in the 1080 runs of the grid below and in every re-check).

  **Why 47 Hz (heuristic, with a physical rationale; not measured):** it is about the half-width of the detector's Hann main lobe, 2/T_w for its window of T_w = 42.7 ms (2 × 23.4 Hz = 46.9 Hz). A peak closer than that to a station can be that station's own spread (main lobe, keying sidebands), so the detector cannot separate two stations there in any case. D is stated in Hz and does not change if the FFT changes (only its rationale would).

- **Option 1, simulated (2026-09-29; before this revision).** Method: a fresh Python/numpy port, written from `engine/src` (detector, spectrum, channelizer, classical decoder) and Tasks 10–13; it reproduced the first design's numbers (below) within a couple of runs in 30. Input: complex baseband at 6000 samples/s (256-point periodic Hann FFT, hop 128 samples: the engine's 23.4375 Hz bins and 21.3 ms hop). A at +1000.0 Hz (7.8 Hz below its bin's center), key-down power 1 FS², S₅₀₀ = 15 dB, 25 WPM, "PARIS PARIS PARIS PARIS" from 0.5 s; 1.0 s of silence; B at Δf_B above A, 18 WPM, "DE W9XYZ PARIS" (9.40 s), its level in dB re A's key-down power; 1.0 s; A again; 1.5 s of noise. Machine timing, 5 ms raised-cosine edges, random carrier phases, 30 numpy seeds per case (rates carry over to other seeds, particular results do not). Detector as in milestone 1 plus option 1's peak following (point 1 above); channelizer as in the engine (129-tap Blackman-windowed sinc, −6 dB at ±150 Hz, decimation to 1500 samples/s; the channel stays on the bin where the track was born); the Matched decoder of Tasks 10–12 with the anchor and ±12 Hz rule of point 2, the ×1.25 growth bound only *while* following (the first-step bound, owner decision 3, was decided after this simulation and is **not** in it), the decoder estimating the dit itself. Engine order per hop: spectrum frame → detector (follow, refresh, births and deaths) → one 32-sample block per channel, its anchor set first. Metrics: *B decoded* = the best channel's text during B's over has a no-space CER ≤ 0.3 (Levenshtein), split by A's channel or B's own track; *B ≥ 90%* = at least 11 of B's 12 characters matched in the alignment; *A's next over exact* = some channel's text during A's second over is exactly "PARIS PARIS PARIS PARIS" (in parentheses: its last three words intact); *runaway* = the filter reached K ≥ 200 samples (133 ms); *channel on B* = median time from B's first key-down until the published frequency of A's channel is within 5 Hz of B's carrier (only where A's channel decoded B).

| Δf_B (Hz) | B (dB re A) | B decoded: A's channel / own track (of 30) | B ≥ 90% (of 30) | A's next over exact (last 3 words) (of 30) | runaway (of 30) | channel on B, median (s) | channels, median (max) |
|---|---|---|---|---|---|---|---|
| 0 | −10 | 26 / 0 | 26 | 27 (27) | 3 | 0.01 | 1 (1) |
| 0 | −6 | 30 / 0 | 30 | 30 (30) | 0 | 0.01 | 1 (1) |
| 0 | 0 | 30 / 0 | 30 | 30 (30) | 0 | 0.01 | 1 (1) |
| 0 | +6 | 30 / 0 | 30 | 30 (30) | 0 | 0.01 | 1 (1) |
| 10 | −10 | 25 / 0 | 25 | 25 (25) | 5 | 1.40 | 1 (1) |
| 10 | −6 | 30 / 0 | 30 | 30 (30) | 0 | 1.37 | 1 (1) |
| 10 | 0 | 30 / 0 | 30 | 30 (30) | 0 | 1.35 | 1 (1) |
| 10 | +6 | 30 / 0 | 30 | 30 (30) | 0 | 1.27 | 1 (1) |
| 25 | −10 | 23 / 0 | 23 | 24 (24) | 6 | 1.45 | 1 (1) |
| 25 | −6 | 30 / 0 | 30 | 30 (30) | 0 | 1.80 | 1 (1) |
| 25 | 0 | 30 / 0 | 30 | 30 (30) | 0 | 1.44 | 1 (1) |
| 25 | +6 | 30 / 0 | 30 | 30 (30) | 0 | 1.35 | 1 (1) |
| 40 | −10 | 22 / 0 | 0 | 27 (28) | 2 | 2.33 | 1 (1) |
| 40 | −6 | 26 / 0 | 25 | 30 (30) | 0 | 2.04 | 1 (1) |
| 40 | 0 | 30 / 0 | 30 | 30 (30) | 0 | 1.46 | 1 (1) |
| 40 | +6 | 30 / 0 | 30 | 30 (30) | 0 | 0.09 | 1 (1) |
| 50 | −10 | 28 / 0 | 0 | 26 (29) | 1 | 1.48 | 1 (2) |
| 50 | −6 | 30 / 0 | 0 | 22 (30) | 0 | 1.23 | 1 (1) |
| 50 | 0 | 18 / 0 | 18 | 14 (18) | 12 | 0.12 | 1 (1) |
| 50 | +6 | 30 / 0 | 30 | 26 (28) | 2 | 0.03 | 1 (1) |
| 60 | −10 | 0 / 2 | 0 | 30 (30) | 0 | — | 2 (2) |
| 60 | −6 | 0 / 30 | 0 | 30 (30) | 0 | — | 2 (2) |
| 60 | 0 | 0 / 30 | 0 | 6 (26) | 0 | — | 2 (2) |
| 60 | +6 | 0 / 30 | 0 | 0 (30) | 0 | — | 2 (2) |
| 70 | −10 | 0 / 30 | 0 | 30 (30) | 0 | — | 2 (2) |
| 70 | −6 | 0 / 30 | 0 | 30 (30) | 0 | — | 2 (2) |
| 70 | 0 | 0 / 30 | 0 | 26 (27) | 1 | — | 2 (2) |
| 70 | +6 | 1 / 29 | 1 | 19 (22) | 6 | — | 2 (2) |
| 100 | −10 | 0 / 30 | 0 | 30 (30) | 0 | — | 2 (2) |
| 100 | −6 | 0 / 30 | 0 | 30 (30) | 0 | — | 2 (2) |
| 100 | 0 | 0 / 30 | 0 | 24 (28) | 0 | — | 2 (2) |
| 100 | +6 | 0 / 30 | 0 | 30 (30) | 0 | — | 2 (2) |
| 200 | −10, −6, 0, +6 | 0 / 30 | 0 | 30 (30) | 0 | — | 2 (2) |

  Merges: 0 in every row (merging was kept in the port, on the detector frequencies, only to count it). B's published frequency just before A resumes: median 0.01–0.14 Hz and at most 0.38 Hz from B's carrier in every row, except one run at 70 Hz, +6 dB re A (69.4 Hz: B's text there came from A's channel hearing B's leakage). A's published frequency at the end of its next over: at most 0.13 Hz from A's carrier. Duplicates (two channels each decoding at least half of one over): 3 of 30 runs at 70 Hz, +6 dB, and 18 of 30 at 100 Hz, +6 dB (B's own channel decoding a word or two of A's sidelobe after B's over; the same with the first design), else 0.

  **What option 1 fixes (simulated):** the walk-and-merge cycle is gone. B 60–200 Hz away is decoded in 30 of 30 at every level from −6 dB re A up, by its own track in all but one run (at 70 Hz, +6 dB: 29 by its own track, 1 by A's channel) (first design: 6 and 0 of 30 at 60 Hz, 0 and +6 dB; 0 of 30 at 70 Hz, +6 dB), with two channels and no merge. A weak B within D is now followed, after a delay: at 40 Hz, −10 dB re A, B decoded in 22 of 30 (first design: 3 to 4 of 30).

  **The cost: the retune waits for the detector's 1 s average (stated limit (b); backlog, with the replay item).** After a turnover within D the detector's track moves to B only when B's bin becomes the maximum within ±47 Hz of the 1 s power average, that is when A's decaying average P_A·e^(−t/τ), τ = 1 s, falls below B's rising one. Derived: ignoring B's rise, t = τ·ln(P_A/P_B) = 2.3 s after A's last mark for B at −10 dB re A; with B's rise P_B·(1 − e^(−(t − 1 s)/τ)) after the 1 s gap, about 2.6 s after A's last mark, 1.6 s into B's over. Simulated: 1.27–1.46 s into B's over at 10–25 Hz and 0 to +6 dB (1.80 s at 25 Hz, −6 dB), 2.04 and 2.33 s at 40 Hz and −6 and −10 dB (0.7 s more than derived at −10 dB: B's keying duty cycle and A's main lobe in B's bin; not separated). At 10 Hz B is within the tracker's ±12 Hz, so the tracker moves by itself. **The delay matters only when the answering station is on a different frequency from the one the channel is tuned to; a station that pauses and resumes on its own frequency loses nothing.** Characters of B lost at the start of its over (median over the runs where at least half of B was decoded; simulated): 0 at 10 Hz at every level; at 25, 40 and 50 Hz, 1, 3 and 3 at −10 dB re A, 0, 1 and 2 at −6 dB, and 0 at 0 and +6 dB. Where nothing is lost, A's channel keys B through its 16 ms acquisition filter while the detector's peak moves (|sinc(25 Hz · 16 ms)|² = −2.4 dB relative to a centered station, derived). At 40 Hz, −10 dB no run reaches 90% of B's characters. The return to A is symmetric (for B stronger than A, the track waits for B's average to decay); it did not cost A's next over at 25–40 Hz (27–30 of 30).

  **Where it fails (stated; not fixed; no parameter changed):**
  1. **A neighbor 60–70 Hz away at A's level or stronger leaks through A's matched filter (stated limit (a)).** At 60 Hz, 0 and +6 dB re A, A's next over was exact in 6 and 0 of 30, but its last three words were intact in 26 and 30 of 30 (typically "FARIS" or "EGARIS" for the first word). Traced (60 Hz, 0 dB, seed 1): A's channel stays on A (its anchor moved 1.4 Hz), but after the 2 s re-acquisition window it is back at K = 58 (38.7 ms), where B sits on the boxcar's first sidelobe, |sinc(60 Hz · 38.7 ms)| = 0.116, −18.7 dB relative to a centered station (derived); â ≈ 4.2 against the 3.74 squelch, so B's fragments are keyed, the speed window fills with them (14.5 WPM), and the next re-acquisition fires 12 of those dits (0.99 s) after B's last fragment, as A resumes; A's first two marks are classified with the 82.8 ms dit, so P (.--.) becomes F (..-.). At 70 Hz, +6 dB (A exact 19 of 30) the leak enters through the K = 24 acquisition filter (|sinc(70 Hz · 16 ms)| = 0.105, −19.6 dB relative to a centered station, derived; B then looks like S₅₀₀ ≈ +1.4 dB, above the −2.5 dB acquisition squelch), the filter follows B's fragments to 11–12 WPM (K = 123–134 samples, 82–89 ms), longer than A's 48 ms spaces, and merges A's marks (runaway in 6 of 30). The decoder hears the neighbor through its own filter; the first design had the same problem, hidden by its walk and merge. The fix belongs in the filter's design (for example a tapered filter with lower sidelobes), later, with the postponed co-channel item (owner, 2026-09-29).
  2. **A retune in the middle of a mark can start a filter runaway** (50 Hz, 0 dB re A: A exact 14 of 30, B 18 of 30, runaway in 12 of 30). At 50 Hz B's rising skirt skews A's interpolated peak a few Hz toward B, which puts B within D, and the track jumps 0.1 s into B's first dah. Traced (seed 1): the truncated first mark (109 ms; true 200 ms) sits between B's dits (67 ms) and dahs (197–200 ms); the largest ratio in the sorted window is 197/109 = 1.80, just under the milestone-1 speed logic's 1.8 split, so the dit becomes the mean of dits and dahs, 110 ms. When the filter starts following at 8 marks, that first step was not bounded (the bound applied only while already following), so K jumped from 24 to 146 samples (97 ms), longer than B's 67 ms spaces, merged marks and ran to 288 (5 WPM). **Owner decision 3:** the ×1.25 bound now applies from the filter's first follow step (Task 12). Its effect on this case was **not simulated**: it slows the filter's growth (from the 20 ms acquisition dit to 110 ms takes 8 marks instead of 1), and whether B's next marks correct the estimate before the filter outgrows B's spaces is to be measured (Task 14, group H); if the runaway persists, it is reported to the owner.
  3. **Unchanged (detector latency, not option 1):** B's own track (Δf_B ≥ 60 Hz) is born about 1.2 s into B's over (1 s average plus 0.5 s persistence), so it loses "DE" and never reaches 90% of B's characters (backlog, "Wrong or missing first characters").

  **Rejected variant (stated limit (c)):** a second, 0.2 s spectrum average used by a track while its channel is in the decoder's re-acquisition window would put the channel on B 0.01–0.04 s after B's first key-down (A's 0.2 s average is down e^(−1/0.2) = −21.7 dB in the 1 s gap, derived) and lose no characters at 25 and 40 Hz (40 Hz, −10 dB re A: B ≥ 90% in 24 of 30). It was rejected because it creates new failures (simulated, 30 seeds): at 50 Hz A's next over was exact in only 15–20 of 30 and merging came back (17–41 merges per 30 runs): in the 0.2 s average A's decaying peak and B's rising one skew the track's interpolated frequency toward B quickly enough to carry the channel across D (traced: 4.6 Hz in 0.3 s), and when A resumes it is just over D from the track, so A gets a new track born 0.66 s into its over; and in 2 of 60 runs at 200 Hz a channel in its re-acquisition window jumped 21–25 Hz to a noise peak of the 0.2 s spectrum (the 0.2 s average's per-bin relative spread is about 1/√19 = 23% against 10% for the 1 s one, derived).

  **Also re-checked (simulated, 30 seeds each, the same seeds in both designs; levels in dB re A's key-down power):** a steady neighbor 100 Hz above A, both keying from the start (A "CQ TEST K1ABC" ×4 at 25 WPM, S₅₀₀ = 15 dB; neighbor "CQ TEST W2XYZ" ×5 at 30 WPM): at −6 and 0 dB, A (some channel holding "K1ABC" at least 3 times) in 30 of 30, the neighbor in 30 of 30, A's published frequency within 0.21 Hz, 0 merges; at +10 dB, A in 16 of 30, identical in both designs (a decoder-level limit: the neighbor's leakage through A's matched filter). **Slow drift, 1 Hz/s** (A alone, S₅₀₀ = 20 dB, "PARIS" ×8 at 25 WPM from 0.5 s, 19.3 s): one channel and the last six words intact in 30 of 30, published frequency 1.49–1.63 Hz behind the carrier at the end of the last mark (the same as the first design: the tracker never needed more than its ±12 Hz), the detector's track 0.94–1.23 Hz behind (a 1 s average of a ramp lags ḟ·τ = 1 Hz at 1 Hz/s, derived). The published lag exceeds the derived ḟ·τ_f/P₁ = 1.1 Hz; it is measured at the end of the last mark, and the estimate is frozen in word spaces (not investigated further). Fast drift (3 Hz/s) is out of scope (owner, 2026-09-29).

  **Status of the choices:** derived: the sidelobe levels, the 2.3 s and 2.6 s retune delays, the 0.2 s average's −21.7 dB decay and the averages' spread, the detector's drift lag. Simulated: every count and time above. Heuristic: D = 47 Hz, ±12 Hz, following only peaks that pass the birth peak test and stand 3 dB above the floor, no re-acquisition on a retune.

- **The first design, rejected (simulated 2026-09-29; kept as the record).** It used D in three places: attribution of new peaks by distance from the track's current frequency, which followed the channel's published frequency (`SignalDetector::retune` after every channel block); a tracker that followed any station within D_f = D + 10 Hz = 57 Hz of an anchor that followed its accepted estimates with τ = 10 s; and merging two channels whose published frequencies came within D, keeping the older. Method: a Python port of Tasks 10–13 as then written, at engine level: the detector (a complex span at 6000 samples/s with a 256-point periodic Hann FFT, which gives the engine's 23.4 Hz bins and 21.3 ms hop; 1 s power average, 6 dB threshold per bin, 0.5 s persistence, 10 s timeout, distance attribution, retune after every channel block), the channelizer (mix to the bin center, Blackman-windowed sinc with −6 dB at ±150 Hz and 21.3 ms length, decimation to 1500 samples/s), channels opened at track birth with the detector's residual as the initial offset, the Matched decoder on a port of the milestone-1 decoder's own element and speed logic (the decoder estimates the dit itself) with the ×1.25 growth bound (Task 12), D_f = 57 Hz, and merging within D keeping the older channel. The port reproduces the final check's decoder-level numbers with the earlier parameters (30 seeds each: B 25 Hz away at −6 dB re A 30/30 passes, against 100/100; 100 Hz at 0 dB 23/30, against 47/60; `MatchedDecodesAtThreeDbS500` 40/50, against 81/100). Scene: A at S₅₀₀ = 15 dB, 25 WPM, "PARIS PARIS PARIS PARIS"; 1 s of silence; B at 18 WPM, "DE W9XYZ PARIS" (9.4 s), Δf_B above A, its level in dB re A's key-down power; 1 s; A again. White noise, 30 numpy seeds per case (rates carry over to other seeds, not particular results). Columns: *B decoded* counts runs where a channel's text during B's over is within CER 0.3 of B's text, split by whether that channel is A's or a track of B's own; *B's marks keyed* is the fraction of B's marks keyed at their middle and not merged with the next mark, in the best channel (B's own track loses the marks before its birth, about 0.6 s); *A's next over intact* counts runs where some channel's text during A's second over is exactly "PARIS PARIS PARIS PARIS"; *B frequency error* is the published frequency of the channel decoding B, just before A resumes, minus B's carrier. Duplicate decodes (two channels decoding the same over) occurred in **0 runs of every case**: each duplicate was merged first. A's frequency error at the end of its next over was at most 0.12 Hz in every run where it was intact.

| Δf_B (Hz) | B (dB re A) | B decoded: in A's channel / own track (of 30) | B's marks keyed, median (min) | A's next over intact (of 30) | tracks, median (max) | merges (30 runs) | B frequency error, median / max (Hz) |
|---|---|---|---|---|---|---|---|
| 0 | −10 | 27 / 0 | 1.00 (0.32) | 27 | 1 (1) | 0 | 0.08 / 0.20 |
| 0 | −6 | 30 / 0 | 1.00 (1.00) | 30 | 1 (1) | 0 | 0.04 / 0.15 |
| 0 | 0 | 30 / 0 | 1.00 (1.00) | 30 | 1 (1) | 0 | 0.02 / 0.07 |
| 0 | +6 | 30 / 0 | 1.00 (1.00) | 30 | 1 (1) | 0 | 0.01 / 0.04 |
| 10 | −10 | 25 / 0 | 1.00 (0.37) | 25 | 1 (1) | 0 | 0.09 / 0.25 |
| 10 | −6 | 30 / 0 | 1.00 (1.00) | 30 | 1 (1) | 0 | 0.06 / 0.16 |
| 10 | 0 | 30 / 0 | 1.00 (1.00) | 30 | 1 (1) | 0 | 0.03 / 0.08 |
| 10 | +6 | 30 / 0 | 1.00 (1.00) | 30 | 1 (1) | 0 | 0.02 / 0.04 |
| 25 | −10 | 26 / 0 | 0.97 (0.97) | 30 | 1 (1) | 0 | 0.06 / 0.26 |
| 25 | −6 | 30 / 0 | 1.00 (1.00) | 30 | 1 (1) | 0 | 0.06 / 0.17 |
| 25 | 0 | 30 / 0 | 1.00 (1.00) | 30 | 1 (1) | 0 | 0.03 / 0.09 |
| 25 | +6 | 30 / 0 | 1.00 (1.00) | 30 | 1 (1) | 0 | 0.01 / 0.04 |
| 40 | −10 | 3 / 0 | 0.00 (0.00) | 29 | 1 (1) | 0 | 0.13 / 0.20 |
| 40 | −6 | 22 / 0 | 0.97 (0.32) | 28 | 1 (1) | 0 | 0.05 / 0.11 |
| 40 | 0 | 30 / 0 | 1.00 (1.00) | 30 | 1 (1) | 0 | 0.03 / 0.08 |
| 40 | +6 | 30 / 0 | 1.00 (1.00) | 30 | 1 (1) | 0 | 0.02 / 0.04 |
| 50 | −10 | 0 / 3 | 0.76 (0.00) | 30 | 2 (2) | 25 | 0.14 / 0.23 |
| 50 | −6 | 1 / 26 | 0.82 (0.39) | 27 | 2 (3) | 34 | 0.05 / 0.21 |
| 50 | 0 | 24 / 0 | 1.00 (0.34) | 22 | 2 (3) | 31 | 0.03 / 0.09 |
| 50 | +6 | 30 / 0 | 1.00 (1.00) | 24 | 2 (2) | 30 | 0.02 / 0.05 |
| 60 | −10 | 0 / 2 | 0.78 (0.00) | 30 | 2 (2) | 23 | 0.06 / 0.09 |
| 60 | −6 | 0 / 30 | 0.82 (0.82) | 30 | 2 (2) | 30 | 0.05 / 0.15 |
| 60 | 0 | 0 / 6 | 0.49 (0.32) | 7 | 4 (6) | 75 | 0.02 / 0.07 |
| 60 | +6 | 0 / 0 | 0.50 (0.39) | 1 | 3 (4) | 64 | — |
| 70 | −10 | 0 / 30 | 0.89 (0.87) | 30 | 2 (2) | 29 | 0.11 / 0.38 |
| 70 | −6 | 0 / 30 | 0.89 (0.89) | 30 | 2 (2) | 29 | 0.07 / 0.24 |
| 70 | 0 | 0 / 25 | 0.89 (0.18) | 27 | 2 (6) | 41 | 0.03 / 0.08 |
| 70 | +6 | 0 / 0 | 0.37 (0.24) | 23 | 6 (7) | 123 | — |
| 80 | −10 | 0 / 30 | 0.89 (0.89) | 30 | 2 (2) | 19 | 0.07 / 0.34 |
| 80 | −6 | 0 / 30 | 0.89 (0.89) | 30 | 2 (2) | 19 | 0.05 / 0.22 |
| 80 | 0 | 0 / 27 | 0.89 (0.45) | 24 | 2 (3) | 27 | 0.03 / 0.11 |
| 80 | +6 | 0 / 30 | 0.89 (0.89) | 30 | 2 (2) | 21 | 0.01 / 0.06 |
| 100 | −10 | 0 / 30 | 0.89 (0.89) | 30 | 2 (2) | 0 | 0.08 / 0.29 |
| 100 | −6 | 0 / 30 | 0.89 (0.89) | 30 | 2 (2) | 0 | 0.05 / 0.19 |
| 100 | 0 | 0 / 30 | 0.89 (0.89) | 23 | 2 (2) | 0 | 0.02 / 0.10 |
| 100 | +6 | 0 / 30 | 0.89 (0.89) | 29 | 2 (2) | 0 | 0.01 / 0.05 |
| 200 | −10 | 0 / 30 | 0.89 (0.89) | 30 | 2 (2) | 0 | 0.09 / 0.24 |
| 200 | −6 | 0 / 30 | 0.89 (0.89) | 30 | 2 (2) | 0 | 0.06 / 0.15 |
| 200 | 0 | 0 / 30 | 0.89 (0.89) | 30 | 2 (2) | 0 | 0.03 / 0.08 |
| 200 | +6 | 0 / 30 | 0.89 (0.89) | 30 | 2 (2) | 0 | 0.01 / 0.04 |

  **Reading the table.** Merges beyond D with B weaker than A (for example 29 in 30 runs at 70 Hz, −6 dB) came after B's over: when A resumed, 6–10 dB stronger than B, B's channel walked to A and was merged into A's channel; B's text was complete by then, but B's next over would need a new track. At 50 and 60 Hz with B at −10 dB re A, B's own track was born 2.0 s into its 9.4 s over (median; 0.6 s at 100 Hz), because B's peak is not the maximum within ±47 Hz until A's decaying power average (τ = 1 s) falls below B's; so B's text missed the CER 0.3 mark (B decoded in 3 and 2 of 30), a detector latency, not the channel distance.

  **Where it works (simulated):** within D (0, 10, 25, 40 Hz) at −6 dB re A or stronger, A's channel follows B, decodes it (30 of 30 at 0–25 Hz, and at 40 Hz from 0 dB up; 22 of 30 at 40 Hz and −6 dB) and comes back for A's next over (28–30 of 30), with one track and no merge. Beyond D, with B weaker than A (−10 and −6 dB re A at 60–200 Hz), B has its own track and A's channel ignores it (A intact 30 of 30; 27 of 30 at 50 Hz −6 dB). At 50 Hz, in the overlap between D and D_f, B's own track is born and merged about 0.7 s later into A's channel, which then decodes B (30 of 30 at +6 dB; 24 of 30 at 0 dB), as intended.

  **Where it failed (no parameter was changed to make it pass; the owner then chose option 1, above):**
  1. **B 60 Hz away at A's level or stronger.** A's next over intact in 7 of 30 runs at 0 dB re A and 1 of 30 at +6 dB; B decoded in 6 of 30 and 0 of 30; 3–6 tracks per run. Mechanism (traced): after the 2 s re-acquisition window, A's channel is back at A's width (K = 58, a 38.7 ms filter at 25 WPM) and hears B through the filter's first sidelobe (|sinc(60 Hz · 38.7 ms)| = 0.116, −18.7 dB relative to a centered station, derived), which puts B near the squelch (about S₅₀₀ −3.7 dB against the −4.4 dB squelch at K = 58), so B is keyed in fragments. The tracker's average of those fragments' lag products, mixed with the noise products that the boxcar makes coherent at f̂, points between A and B (36 Hz in the traced run), inside D_f of the anchor, so it is accepted and f̂ jumps there; the published frequency then comes within D of B's own channel, and the merge rule closes B's channel. B's peak is then more than D from A's track once A's channel returns to its anchor, a new track is born for B, and the cycle repeats. A diagnostic with merging turned off (not a proposal): B decoded by its own track in 30 of 30, but A's next over intact in only 4 of 30 (0 dB) and 0 of 30 (+6 dB) (its last three words intact in 30 of 30: A's channel had walked toward B, so A's first word went to a third track). So the root cause is the tracker walking past D_f on a strong neighbor's sidelobe leakage (the final check's finding m-A, now just beyond 57 Hz instead of beyond 35 Hz), and merging on the instantaneous published frequency turns a brief walk into the loss of B's channel.
  2. **B 70–100 Hz away at 0 or +6 dB re A.** A's next over intact in 23 of 30 (70 Hz, +6 dB), 24 of 30 (80 Hz, 0 dB), 23 of 30 (100 Hz, 0 dB; the final check saw 47 of 60 with the earlier design, so unchanged there). At 70 Hz and +6 dB re A, B was decoded in 0 of 30 (up to 7 tracks and 123 merges in 30 runs): the same walk-and-merge cycle.
  3. **A weak B within D, off center.** At 40 Hz and −10 dB re A, B was decoded in 3 of 30 (marks keyed: median 0.00). At the acquisition width B is 20·log₁₀|sinc(40 Hz · 16 ms)| = −6.9 dB relative to a centered station, so about S₅₀₀ −1.9 dB, at the acquisition floor; within D it gets no track of its own. (B at 0–25 Hz and −10 dB: decoded in 25–27 of 30.)
  4. **Separate, from milestone 1 (not caused by D):** with B's over 16 s long (Task 12's text, "DE W9XYZ PARIS PARIS PARIS"), A's track died during B's over in every run at 200 Hz, and at 100 Hz at −10 and −6 dB re A: a track at S₅₀₀ = 15 dB lives about 15 s after its station stops (the power average's decay plus the 10 s timeout, signal-processing.md §6). A's next over then went to a new track that lost its first word (intact 0 of 30). Real overs are often longer than 16 s. Backlog: "Tracks outlive their stations; separate station identity from decoding".

  Candidate directions offered to the owner then (none tried or tuned): merge only after a sustained time within D, or compare anchors; bound how far a channel's anchor may move from its birth station's frequency (the direction option 1 takes, with the detector's peak as the anchor); accept a frequency estimate only from marks assigned to the channel's current station.

  **Also re-checked at engine level with the first design (30 seeds each; levels re A's key-down power):** a steady neighbor 100 Hz away keying at the same time (A "CQ TEST K1ABC …" at 25 WPM, S₅₀₀ 15 dB; neighbor at 30 WPM): at 0 dB, A's last three calls decoded in 30 of 30 and A's published frequency within 0.3 Hz throughout; at −6 dB 30 of 30 (neighbor decoded by its own track in 26); at +10 dB A's channel decoded only fragments in 30 of 30 runs, and the result is identical with the milestone-1 attribution rule and the earlier 35 Hz pull-in, so it predates D (the decoder-level `MatchedIgnoresStrongerNeighbor`, whose channel is open before either station keys, passes 88 of 100 in this port). A 3 Hz/s drift over 19 s (S₅₀₀ = 20 dB): one track in 30 of 30, published frequency 3.1–3.2 Hz behind the carrier at the last symbol (derived lag ḟ·τ_f/P₁ = 3.4 Hz), moved at least 52 Hz from its birth frequency, text intact in 30 of 30. **A neighbor keying at the same time 40–60 Hz away** (postponed to the backlog; reported only): at −6 dB re A, A decoded in 30 of 30 at 40, 50 and 60 Hz, the neighbor in 0 of 30 (within D, or its peak is not a local maximum within ±47 Hz of A's stronger one); at 0 dB, A in 0 (40 Hz; the channel settled between the two, near 14 Hz, and decoded nothing), 13 (50 Hz) and 28 (60 Hz) of 30, the neighbor in 0, 30 and 30; at +6 dB, A in 0 of 30 at each offset (the channel moves to the neighbor), the neighbor in 30 of 30.

- **Expected accuracy (derived upper bound; simulated; to be measured):** with the phase noise of each product set by the per-sample SNR in B_v, and about B_v independent products per second of key-down, the RMS error at 25 WPM is at most roughly 0.5 Hz at S₅₀₀ = 0 dB and 0.9 Hz at S₅₀₀ = −5 dB; a simulation of the whole chain (60 s of PARIS, 4 seeds; plan review) gave 0.08, 0.27 and 0.55 Hz RMS at +10, 0 and −5 dB (review finding M2). The lag behind a linear drift of rate ḟ (Hz/s) is about ḟ·τ_f/P₁ (derived for a first-order average of a phasor whose frequency ramps, updated only during key-down): 1.1 Hz at 1 Hz/s.
- **Test target (spec §5.2):** residual |f̂ − Δf| ≤ 2 Hz after 5 s of keying at S₅₀₀ = 10 dB, 20 WPM, from an initial error of 11 Hz (Task 12). Through a filter of length T that is a loss of |sinc(2 Hz · 60 ms)|² = 0.2 dB relative to a centered station (derived); through this plan's βT = 0.8T filter it is 0.13 dB.

### C. The matched front end

- **Filter: a boxcar (moving average) of K = round(β·T̂·r) samples, normalized by 1/K**, run before envelope detection. A boxcar of duration T is the matched filter for a rectangular element of duration T and has noise bandwidth exactly 1/T (derived: Proakis §4.2–2, proakis-ook-notes.md §2.7). Its cost is O(1) per sample (running sum over a ring buffer, recomputed exactly every 4096 samples and whenever K changes, to stop rounding drift).
- **β = 0.8** (heuristic; to be measured against 0.6 and 1.0): a filter slightly shorter than the dit costs 10·log₁₀(1/0.8) = 0.97 dB of output SNR relative to the matched filter (derived), and keeps the filter shorter than an element space even when the speed estimate is 25% too slow or a hand-keyed space is short. A filter longer than the gaps would merge successive dits, the speed estimate would then lock onto the merged marks, and the filter would never recover.
- **Following speed** (heuristic): the filter starts at the fastest code, 60 WPM (T = 20 ms, K = 24, B_v = 62.5 Hz), and follows the decoder's dit estimate once the decoder's speed window holds at least 8 marks; before that the estimate can be far off (it starts at 25 WPM). It then changes K every time the estimate changes, which happens only after a mark. **While the filter follows, the dit estimate may grow by at most ×1.25 per mark** (owner decision 2026-09-29; final check F-2): in 2 of 100 simulated 20 → 35 WPM changes the estimate jumped ×2 in one update while the speed window held both speeds, the filter outgrew the element spaces, merged marks and ran away. The bound costs a real slowdown ln(ratio)/ln 1.25 marks to follow: 5 marks from 35 to 12 WPM (derived); the owner accepts losing a few marks. (Re-simulated with the bound, `MatchedFollowsSpeedChange` passed 98 of 100 in this port, no run reaching the runaway's K = 105: one ended with a stray E after the last call (K = 41 samples, as expected), one garbled the last call while K was still at 61 samples, a 40.7 ms filter, on its way down.) **The bound also applies to the filter's first follow step after an acquisition or re-acquisition** (owner decision 3 of option 1, 2026-09-29): the filter's own dit, T_v/β, grows at most ×1.25 per mark from the width it has (the 20 ms acquisition dit), until it reaches the decoder's estimate; decreases are not bounded. Before, the first step went straight to the estimate, which may rest on up to 7 unbounded marks: in the option-1 simulation a retune in the middle of a mark gave a 110 ms estimate, K jumped from 24 to 146 samples and ran to 288 (12 of 30 runs at 50 Hz, 0 dB re A; Design decisions B). Reaching a station's width from the acquisition width now takes ln(T/20 ms)/ln 1.25 marks after the first 8: 4 marks at 25 WPM, 6 at 18 WPM, 8 at 12 WPM (derived); meanwhile the filter is shorter than matched, which costs sensitivity but not timing. Its effect on the runaway was not simulated (to be measured). K is clamped to [1, round(β · 1.2/5 · r)] = [1, 288] (5 WPM). When K changes, σ̂² is rescaled by K_old/K_new (derived for noise white at r: the boxcar's output noise power is then proportional to 1/K; the channel filter removes the boxcar's sidelobes beyond ±150 Hz, keeping about 0.92 of its noise power at K = 24 and 0.965 at K = 58, so the rescale, and a² below, are off by about 0.2 dB relative to the true noise power, derived; review finding M6); ŝ is unchanged (a centered tone passes a normalized boxcar at unity gain).
- **Re-acquisition after a silence** (heuristic; review finding C2, re-review I-2 to I-4, final check F-1; Task 12): the filter narrows to the current station's dit and ŝ settles at its level, so a station that answers 25 Hz away (near the narrow filter's first null, main lobe ±21–26 Hz at 20–25 WPM) or 6 dB weaker (re the first station's key-down power) never raises p, and neither the tracker nor ŝ ever moves to it. Hold-through-pause is right for one station that pauses and wrong for two taking turns. So when the key has been up for longer than max(0.5 s, 12 dits), the decoder puts K back to 24 (the 60 WPM acquisition width), sets ŝ² and its weight to 0, restarts the frequency average from the last f̂, keeps σ̂² (rescaled), **starts a new speed window** (the old one is set aside), and lets the filter follow the speed again only after 8 new marks; if nothing is keyed within 2 s it restores the set-aside speed window and returns to the width it had, so a weak station that pauses is not held at the acquisition floor (below). The new speed window is the final check's fix (F-1): with the window kept, the fragments of a station keyed only partly through the wide filter (a neighbor 50 Hz away, say) entered the same 24-mark window as the caller's marks, the filter followed the mixed estimate, and a filter longer than the caller's element spaces merged its marks; the estimate then locked onto the merged marks and K ran to 288, the runaway the β rationale above warns about. Until two new marks exist the decoder still classifies elements with the previous station's dit. 12 dits exceeds all but about 1% of word spaces (0.77% of VE3NEA's hand-key word spaces, derived from their log-normal); the 0.5 s floor matters only above 28.8 WPM. On its own, the tracker then reaches only stations within ±12 Hz of its anchor; a station answering farther away within D is reached when the detector's track moves to its peak and the engine moves the anchor there (option 1; Design decisions, B), which takes 1.3–2.3 s after its first key-down (simulated; median characters lost at the start of its over: 0 at 10 Hz, up to 3 for a station 40–50 Hz away at −10 dB re the first station, 0 at 0 dB and above; nothing for a station that resumes on the channel's own frequency). A station answering must also pass the acquisition squelch after the K = 24 boxcar's loss |sinc(Δf · 16 ms)|² (−2.4 dB relative to a centered station at 25 Hz, −6.9 dB at 40 Hz, derived), about S₅₀₀ ≥ 0 dB at 25 Hz. **Simulated with the first design (2026-09-29; decoder level, Task 12's turnover with B's 16 s over, a follow distance of 57 Hz and the ×1.25 growth bound while following, 100 numpy seeds each; levels in dB re A's key-down power; kept for the record; the cases where f̂ moved to B are now engine-level tests, Task 13):** B 25 Hz away at −6 dB: B's text decoded, f̂ 24.6–25.2 Hz before A's next over, and A's next over intact in 100 of 100; B 40 Hz away at 0 dB: the same in 100 of 100 (f̂ 39.9–40.1 Hz); B 40 Hz away at −6 dB: f̂ on B (39.8–40.2 Hz) but B's "DE W9XYZ PARIS" decoded in 26 and A's next over intact in 73 of 100; B 50 Hz away at −10 dB: too weak to be keyed at the acquisition width, f̂ within 0.2 Hz of A and A intact in 99 of 100; B 70 Hz away at −6 dB, 100 Hz away at −6 and +10 dB: f̂ within 0.2 Hz of A, A intact in 99, 99 and 100 of 100. The engine-level behavior with option 1, and where it fails, are in Design decisions, B. **The 2 s window:** re-measured, a single station at S₅₀₀ = −2 dB pausing 15 dits every 3 words kept on average 0.47 of its marks with the window and 0.39 without at 25 WPM, and 0.81 against 0.73 at 12 WPM (20 seeds, the same seeds both ways); the per-seed spread (0.00–0.92 at 25 WPM) is much larger than the difference, so the window's benefit is small. (An earlier figure of 74% against 41% came from a keyer given the true dit and is withdrawn.)
- **LLR (derived: Proakis eq. 4.5–21, OOK case; proakis-ook-notes.md §2.2):** Λ = −a²/2 + ln I₀(a·x), with x = |v|/σ̂ and a = ŝ/σ̂, in nats. ln I₀ is computed without overflow from Abramowitz & Stegun 9.8.1–9.8.2 (relative error below 2×10⁻⁷ in I₀): the power series below 3.75, and ln I₀(z) = z − ½·ln z + ln(poly(3.75/z)) above. The relation to S₅₀₀ (derived, for noise flat across B_v): a² = 2·S₅₀₀·(500 Hz)·K/r, where S₅₀₀ is a linear power ratio here. At 25 WPM (K = 58) and S₅₀₀ = 0 dB, a = 6.2. The noise-bandwidth reduction from the 252 Hz channel to B_v is 10·log₁₀(252/25.9) = 9.9 dB at 25 WPM (derived); how much CER that buys is to be measured.
- **Prior P₁ = 0.44** (derived from PARIS timing: key-down 22 of 50 dit units). The posterior log-odds is g = Λ + ln(P₁/P₀).
- **Amplitude estimate: an online EM update for the Rician component** (derived from the densities; the running form is heuristic). Per sample, with p the posterior: ŝ² ← max(0, ŝ² + p·max(α_a, 1/W_a)·(|v|² − 2σ̂² − ŝ²)), using the Rician mean square 2σ² + s² (Proakis eq. 2.3–58). W_a accumulates the weights p, so the estimate is a weighted running mean at first and an exponential average (α_a = 1 − e^(−1/(τ_a·r))) afterwards, as the detector's power average is. Samples on the boxcar's ramps (a mark entering or leaving the window) with p ≈ 1 pull ŝ low: to 0.79 of s for 25 WPM dits alone (derived for a noise-free trapezoid at K = 58: the RMS over |v| > s/2), and to 0.85–0.88 of s for PARIS at 25 WPM, S₅₀₀ 0–60 dB (simulated). Through the decision threshold (near ŝ/2, about 0.43·s, at high SNR) that lengthens each mark by about 7 ms at 25 WPM (3.5 ms per edge; simulated in the plan review; review finding M1); to be measured.
- **Noise estimate: a guard on |v|² alone, independent of the keying decision** (heuristic form; its bias correction and stability derived; review finding C1). A plain EM update for σ̂² fails on the boxcar's ramps: while a mark enters or leaves the window, |v| rises through the noise level toward s, those samples have p ≈ 0 until |v| nears s/2, and at 25 WPM and S₅₀₀ ≈ 5 dB they would inflate σ̂² about 2× (derived), at S₅₀₀ = 60 dB by orders of magnitude. An earlier draft guarded the update with the posterior (no update within 2K samples of g > +1 nat); but g is computed from σ̂, so that guard selected quiet stretches and biased σ̂ low, which raised â, which blocked more updates: in noise alone it settled at σ̂ = 0.53–0.57·σ with â ≈ 3 in 7 of 10 seeds and keyed noise (simulated, reproducing the plan review). The estimate now reads only |v|² and its own σ̂²: three taps K apart, v[n], v[n−K] and v[n−2K], share no inputs (independent in white noise), and the middle one updates σ̂² ← σ̂² + max(α_n, 1/W_n)·(|v[n−K]|²/(2·m(κ)) − σ̂²) only if |v[n−K]|²/(2σ̂²) < κ = 1.75 and the two neighbors are below κ_n = 4 (a mark or ramp next to the middle tap lifts one of them; κ_n > κ lets the estimate climb faster without letting more marks in, simulated), W_n counting the updates. In noise, y = |v|²/(2σ²) is exponential with mean 1 (Proakis eq. 2.3–43), so the accepted middle tap is y truncated at κ, with mean m(κ) = 1 − κ·e^(−κ)/(1 − e^(−κ)) = 0.632, which the update divides out (derived). With r = σ̂²/σ², the update's fixed points solve r = m(κr)/m(κ): r = 1 is one, with slope κ·m′(κ)/m(κ) = 0.651 < 1 there (stable), and the slope near r = 0 is κ/(2·m(κ)) = 1.38 > 1, so the estimate cannot settle low (derived). With a station present, any mark or ramp within K of the middle tap lifts a tap above κ except at low SNR, where some marks leak in and bias σ̂ high; a larger κ lets more leak in, and at κ = 4 the leak made a second, high fixed point (σ̂ ≈ 1.6·σ at S₅₀₀ = −5 dB, squelching the station; simulated), which is why κ is small. **Recovery from a low estimate (re-review finding I-1).** The guard's acceptance falls as (1 − e^(−κr))(1 − e^(−κ_n r))² when σ̂ is low, so its climb back is slow: from r = 0.25, a 6 dB rise in the noise, integrating dr/dt = (m(κr)/m(κ) − r)·acceptance(r)/τ_n gives 43 s to r = 0.9 (derived). Two additions bound the damage. The warm-up now runs 0.32 s at the acquisition width (about 20 independent samples) and uses the 20th percentile, so it rarely starts low; and a **floor** lifts σ̂² whenever it falls below F = Q/(2·(−ln(1 − 0.1/c))·2.5), Q the 10th percentile of the last 64 samples of |v|² taken K apart (independent). A station that leaves a fraction c of those samples clean cannot lift Q above noise's (0.1/c)-quantile, so F ≤ σ²/2.5 (derived), and the 2.5 covers the quantile's sampling spread (heuristic; with 1.3 or 1.5 instead, the floor lifted σ̂ to as much as 1.5–2·σ under continuous PARIS at low SNR, simulated). **c = 0.25 (final check F-3; the inputs derived, the rounding heuristic).** A sample taken K apart is clean when its K-sample window lies wholly inside a space; a gap of g dits then leaves g − β dits clean. Computed from the keyed envelopes (5 ms edges) of continuous text at the dit-matched K = 0.8·T: PARIS 0.33 (0.336 exactly, without edges: (9 × 0.2 + 4 × 2.2 + 6.2)/50), a CQ call 0.29, a contest exchange 0.31, a pangram 0.31; with the speed estimate 25% slow (K = T, the tolerance β = 0.8 is chosen for) 0.24–0.28. c = 0.25 rounds the lower end down. It does not cover text of solid digits ("0000 9999": 0.18 at K = 0.8·T), where F can reach about 0.64·σ² in expectation (derived), still below σ². The earlier c = 0.4 exceeded continuous PARIS's 0.34, so under strong continuous text the floor lifted routinely (in 199 of 400 seeds of `StrongSignalKeepsNoiseEstimate`) and each lift restarted ŝ (next point). **A lift restarts ŝ only if F > 4·σ̂²** (heuristic factor; final check F-3): a large lift is the stuck-low case, where a low σ̂ has let ŝ grow on noise; a small one is the quantile's spread, or a second station in the channel leaving fewer clean samples than c (not covered by c), and restarting ŝ there refit it from a few samples on a filter ramp, dropped the threshold and merged dits (simulated). In noise F = 2σ²·(−ln 0.9)/(2·(−ln 0.6)·2.5) = 0.0825·σ², so the floor acts when σ̂ < 0.29·σ (derived; with c = 0.4 it was σ̂ < 0.38·σ), and it restarts ŝ when σ̂ < 0.14·σ. It removes the stuck-low state (the case the re-review saw at K = 288) and bounds large noise rises: after a rise of 10 or 20 dB it lifted in 20 of 20 seeds and σ̂ was back within 0.9 of the new σ in 11–38 s (K = 24, simulated). It no longer acts after a 6 dB rise (0 of 80 seeds): that recovery is the guard's own, 43 s derived. Simulated (numpy seeds; final check 2026-09-28): noise alone, 40 seeds × 120 s, σ̂/σ has mean 1.00 and standard deviation 0.020 at K = 24 and 0.032 at K = 58 (0.080 at K = 288, 20 seeds, re-review), with no signal flag and no floor lift at any K; continuous PARIS (60 s, 6 seeds; mean of the last 30 s) gives 0.98–1.06·σ at S₅₀₀ 0–60 dB and 25 WPM, 0.90–1.11·σ at 12 WPM, and the leak of weak marks lifts it to 1.16–1.23·σ at −5 dB and 1.22–1.30·σ at −8 dB (25 WPM); after a 6 dB rise the estimate was back within 0.9 of the new σ in 24–41 s at K = 24 (40 seeds), 25–51 s at K = 58 and 24–56 s at K = 288 (20 seeds each; the earlier floor's lifts made some seeds faster, 1.4–39 s at K = 24, and the final check saw up to 60.5 s at K = 288), the last signal flag came at most 16 s after the rise, and σ̂ ended at 0.95–1.03 (K = 24). A faster rise would need to tell a rise in the noise from a station that occupies most of the samples, and every order statistic tried (a censored estimator, a fraction test) either leaked by 1.5–2× under weak continuous text or did not separate the two (simulated), so the 43 s stands and is stated. This is a Review Focus item.
- **Time constants τ_n = 2 s and τ_a = 0.5 s** (heuristic; to be measured on the fading suite): noise is stationary, so it can be averaged longer; the amplitude must follow fading (f_D up to 3 Hz) but still average several elements.
- **Warm-up (heuristic):** for the first 0.32 s the front end only collects |v|², always at the acquisition width (K = 24; a speed set meanwhile takes effect when the warm-up ends, and restarts ŝ, measured at the wrong width): 20 filter lengths, about 20 independent samples. It then starts σ̂² at the 20th percentile divided by 2·(−ln 0.8) (the q-quantile of an exponential with mean 2σ² is 2σ²·(−ln(1 − q)), derived) and ŝ² at max(0, 90th percentile − 2σ̂²). Starting ŝ above zero matters when σ̂ is also unknown: the mixture fit started from equal components never separates them. The warm-up then counts as weight W_n = W_a = 0.1 times its sample count, so the samples after it soon outweigh it. Nothing is keyed during warm-up. (The earlier 0.2 s at the 5th percentile held about 12 independent samples at K = 24 and 1 at K = 288, and often started σ̂ at 0.2–0.5·σ; re-review finding I-1.)
- **Squelch a_min(K) = 3·(K/24)^(1/4)** (the 3 heuristic, its K-scaling derived; re-review finding I-2): at K = 24, E/N₀ = a²/2 = 4.5 (6.5 dB re 1) for a matched filter (proakis-ook-notes.md §2.1), where a hard per-element decision already errs about 10% of the time (§2.6). In noise alone â² is a p-weighted mean over about τ_a·r/K independent samples, so its spread grows as √K; a_min ∝ K^(1/4) keeps the chance that noise alone passes the squelch the same at every K (derived, Gaussian approximation), where a flat 3 would pass noise more often at long K. From a² = 2·S₅₀₀·(500 Hz)·K/r the squelch sits at S₅₀₀ = −2.5 dB at K = 24 (any speed), −4.4 dB at K = 58 (25 WPM), −6.0 dB at K = 120 (12 WPM) and −7.9 dB at K = 288 (5 WPM) (derived), and up to 1.4 dB higher with ŝ's ramp bias (ŝ = 0.85·s: 20·log₁₀(1/0.85) = 1.4 dB, derived). **A station is first keyed at the acquisition width, so the floor for acquiring one is S₅₀₀ = −2.5 dB derived at every speed, and 50% of marks were keyed near −1.8 dB at 25 WPM and −2.6 dB at 12 WPM (simulated)**; the lower figures hold only for a station already acquired and narrowed to (simulated with the whole decoder, continuous PARIS, 10 seeds, final check 2026-09-28: 0%, 3%, 45% and 80% of marks keyed at −4, −3, −2 and −1 dB at 25 WPM; 19%, 33% and 95% at −4, −3 and −2 dB at 12 WPM). Group A's Matched curve will therefore stop between about S₅₀₀ −2.6 and −1.8 dB, not at the dit-matched figures; lowering the floor (acquiring at a longer filter, keying at K = 24 without the squelch but requiring several consistent marks, or squelching on the a the dit-matched filter would have) trades pull-in, fast-CW handling or false keying; the owner postponed it to the backlog (2026-09-29, "Acquisition floor"). Analysis of the noise-only fixed point (for small a, the p-weighted mean of |v|² − 2σ² is about P₀·a²·σ², so each time constant multiplies a² by about P₀ = 0.56) says noise alone drives a toward 0, **provided σ̂ is right**; the posterior-guarded noise estimate of an earlier draft broke that proviso (C1 above), the |v|²-only guard keeps it.
- **Correlated samples (heuristic; the autocorrelation derived):** the boxcar's output noise autocorrelation is triangular over ±(K − 1) samples and sums to exactly K (derived). So per-sample LLRs overcount the evidence by roughly a factor K; every `FrontEndSample` carries `weight = 1/K`, a factor a sequence decoder may multiply each Λ by before summing (scaling, not decimation, so 0.67 ms timing resolution is kept for the edges). Scaling a nonlinear per-sample LLR by the correlation length is an approximation: the sufficient statistic for one element is one matched-filter sample, and the HMM plan may prefer to decimate at a stride of K (review finding M5). The baseline decoder does not sum LLRs, so it ignores the weight. A unit test checks the sum of the autocorrelation.
- **How the baseline decoder consumes it (heuristic):** key down when g > +h, key up when g < −h, h = 1 nat, replacing the 40%/60% thresholds, the envelope smoother, the warm-up and the mark/space squelch; key up and no key-down while a < a_min. Everything after keying (glitch rejection, element classification, gaps, speed estimation) is unchanged. At high SNR the decision point on x is near a/2 (proakis-ook-notes.md §2.3), so both edges are delayed by about K/2 samples and mark lengths are preserved; at lower SNR the threshold rises (b/a = 0.61 at E/N₀ = 10 dB re 1) and marks shorten by about (2b/a − 1)·K samples. The decoder's existing edge-shortening correction (dah/dit ratio 3.0–3.85) absorbs that. Decoded times include the filter's group delay, (K − 1)/2 samples.

### A. Benchmark design

- **Oracle mode** (heuristic, following manta's oracle idea): for the sensitivity, fading, fist, speed, interference, tuning and ragchew suites, channels open at the labeled frequencies *rounded to the FFT bin* from the first sample, and the detector is bypassed. Reason: the detector's 6 dB-per-bin threshold stops at about S₅₀₀ ≈ 0 dB, so without an oracle the front end's gain below that would be invisible. Rounding to the bin (and starting the NCO at 0) leaves the tracker the full ±11.7 Hz to find, which is the worst case. With option 1 (Task 13) the tracker's anchor in oracle mode is the exact labeled frequency the channel was opened for (the oracle knows it), so the ±12 Hz fine-tuning range is centered on the station, not on the bin center; rows where a station drifts, or a QSO's answering station sits, more than 12 Hz from the label are not meaningful for the Matched front end in oracle mode and are marked so (Task 14). The end-to-end suites (band, crowded, strong, pauses, tune-up, first sample, two-station QSO) keep the detector.
- **Word spaces and first words** (spec §5.4): one minimum-edit alignment of decoded against reference symbols; each edit is charged to one reference symbol; an edit that involves a word space on either side is a space edit, the rest are character edits. Character CER = character edits / reference characters; space error rate = space edits / reference word spaces; first-word CER = edits charged to the first word of each transmission / that word's symbols. Total edits are unchanged (same Levenshtein optimum), so the existing CER stays comparable. The bench also reports each transmission's charged edits, and VE3NEA's metric, the no-space CER (Levenshtein distance with word spaces removed), which is what group B compares with his curves (review finding I3).
- **CPU time per channel-second:** process CPU time over the whole run divided by the total duration of channel output delivered to decoders (an upper bound, since it includes the shared FFT and detector), plus the time spent inside decoders per channel-second (steady clock, single thread).
- **Fading model:** complex Gaussian gain, E|g|² = 1, with a choice of Doppler power spectrum: Gaussian with frequency spread f_D = 2σ (the Watterson / CCIR 520 HF convention; the default), or VE3NEA's 2nd-order Butterworth, S(f) ∝ 1/(1 + (f/f_c)⁴) with f_c = 0.625·f_D (research notes `deepcw-generator-notes.md` §2). His notebook fits a Gaussian to that spectrum and gets 2σ = 1.01·f_D, so his f_D and ours are the same spread to about 1% (factor ≈ 1); the Butterworth has heavier f⁻⁴ tails (0.9% of the power beyond 2·f_D, against 6×10⁻⁵ for the Gaussian, derived), so it fades somewhat faster and rougher at the same f_D. S₅₀₀ with fading is the *mean* key-down power over the noise in 500 Hz, which is also how he defines his SNR (in 3 kHz).
- **The VE3NEA-anchored group (group B)** uses his spectrum shape, his f_D grid {0.1, 0.3, 1, 3} Hz, his ten SNR points converted with S₅₀₀ = ρ + 7.78 dB (−8.22 … 57.78 dB), his styles with a per-operator imbalance, his random-text statistics, and (in one recording) his style mix and speed range, so his published CER curves are a true external reference. Remaining differences, stated in Task 9: our oracle channel sits on the nearest FFT bin rather than his ±30 Hz pitch error, our noise is complex I/Q rather than real audio (the same S₅₀₀ either way), and we draw each character and word space once rather than as his sum of several draws (same medians, slightly less spread). The keying edges match his in group B (2 ms, centered on each element's ends); elsewhere the generator keeps milestone 1's 5 ms edges inside each mark, which shorten every mark by 5 ms at 50% amplitude, an imbalance of −0.10 dit at 24 WPM (derived; review finding I4).
- **Keying styles** (VE3NEA's values except "machine"): element and space durations are log-normal, T·exp(N(μ, σ_ln²)) with T = 1.2 s / WPM, with his μ and σ_ln per element for his styles Computer, Paddle, Vibroplex (a bug) and HandKey (`deepcw-generator-notes.md` §1.2). "machine" (exact PARIS timing) is this project's and stays the default, so milestone-1 recordings do not change. His only per-operator variation, the imbalance δ ~ N(0, (0.1·T)²), is drawn once per operator. His training mix is HandKey 0.25, Paddle 0.50, Computer 0.25 (Vibroplex is defined but never drawn). His speed range is ambiguous: 12–48 WPM in the committed `training_settings.py`, 8–50 WPM in the notebook cell that writes it; the anchored group uses 12–48 WPM and says so.
- **Message text** (heuristic; owner decision 2026-09-27): the suites send realistic text, not only CQs and contest exchanges. A ragchew QSO follows the usual order (CQ, answer, RST and name and QTH, rig and power and antenna and weather, optional chat, closing), with the usual abbreviations (FB, OM, TNX, UR, HR, ES, WX, RIG, ANT, PWR, 73, GL, HPE CUAGN), `<BT>` between thoughts, `<AR>` and `<KN>` at the end of each over, and `<SK>` at the end of each station's last over. Templates and callsigns are this project's; only the filler text for the VE3NEA-anchored group uses his character-frequency table and word-length distribution (MIT, with his copyright notice in the code).
- **Two-station QSOs** (heuristic): each over is keyed at its sender's own speed, style and imbalance, on its sender's carrier and level, after a silence t_turn. The answering station is Δf_B = 0–200 Hz from the caller (owner, 2026-09-27: zero-beating by ear leaves a few to tens of Hz, a sidetone pitch that differs from the rig's CW offset, or RIT, leaves 100–200 Hz; the suite's drawn offsets are weighted toward small ones, heuristic, with no measured distribution yet, which is a backlog item). Within 2 FFT bins (46.9 Hz) the detector hears the QSO as one track, from 3 bins (70.3 Hz) as two, and in between either (derived from its bins and its 3-bin minimum peak separation): the same-track, ambiguous and separate-track regimes, reported separately. So each QSO is labeled two ways: as one signal (the listener's single track; frequency error measured against the station that sent the last over) and as one signal per station at its own carrier (frequency error against that station). Each over is a transmission in the labels with its sender, speed, style, imbalance, offset and level, so the bench scores the first word of every over and reports each over's edits.
- **Suite sizes and intervals** (review finding I1): at `--seeds 3`, every S₅₀₀ point of groups A–C holds at least 1000 characters (about ±0.25 dB, 1σ, on a crossing near CER 0.05, derived from the binomial spread and a CER slope of 3× per 2 dB), and every group-B point at f_D = 0.1 Hz spans at least 100 fade times. Errors cluster within a signal, so every rate and crossing in the summary carries a bootstrap 95% interval over signals, and the two front ends are compared signal by signal on the same recordings.

## Review Focus

Inputs the spec implies but does not spell out, most likely to bite first. Each has a test in the owning task.

1. **Strong signals (S₅₀₀ up to 60 dB):** the noise estimate must not be inflated by the filter's ramps at key-up and key-down, and the decoder must decode exactly — Task 11 (`StrongSignalKeepsNoiseEstimate`), Task 12 (`MatchedDecodesStrongSignal`).
2. **Drifting carriers (slow drift only; owner, 2026-09-29: fast drift is out of scope):** the tracker follows a 1 Hz/s drift within 1 Hz when its anchor follows the station, and cannot leave ±12 Hz of an anchor that does not move; the detector's track follows its own peak across bins; the engine keeps one track through a 1 Hz/s drift and publishes a frequency within 3.6 Hz of the carrier (1.49–1.63 Hz behind, simulated, plus the 2 Hz target) — Task 10 (`FollowsSlowDrift`, `FineTunesOnlyNearItsAnchor`), Task 13 (`SignalDetector.TrackFollowsItsOwnPeakWithinTheDistance`, `Engine.MatchedReportsDriftingFrequency`).
3. **Speed changes mid-transmission (20 → 35 WPM):** the matched filter shortens with the speed estimate and decoding continues after the change — Task 12 (`MatchedFollowsSpeedChange`).
4. **Pauses and stations that stop:** the frequency and amplitude estimates hold through 10 s of noise, and no text is decoded from noise after a station stops (the "stray E's" of the backlog) — Task 10 (`ZeroWeightFreezesEstimate`), Task 12 (`MatchedHoldsThroughPause`, `MatchedNoiseAfterStationStopsDecodesNothing`).
5. **Closely spaced stations:** a station 10 dB stronger (re the wanted station's key-down power) 100 Hz away inside the same ±150 Hz channel must not capture the frequency tracker, and the wanted station must still decode — Task 12 (`MatchedIgnoresStrongerNeighbor`, 20 seeds). At engine level, with the channel opened while both key, the wanted station was decoded in 16 of 30 simulated runs, the same with the first design and with option 1 (Design decisions B); group E measures it. **Stated limit (a), not fixed (owner, 2026-09-29):** a neighbor 60–70 Hz away at the wanted station's level or stronger leaks through the boxcar's first sidelobe (−18.7 dB relative to a centered station at 60 Hz, K = 58; −19.6 dB at 70 Hz through the 16 ms acquisition filter, derived), is keyed in fragments and corrupts the speed estimate (the caller's next over exact in 6 and 0 of 30 at 60 Hz, 0 and +6 dB re the caller; its last three words intact in 26 and 30 of 30); only what works is asserted (`Engine.StrongerStation60HzAwayKeepsItsOwnTrack`); the fix belongs in the filter's design, later (backlog).
6. **Two stations taking turns:** within one track, every over may change speed, keying style, imbalance, level and carrier (up to 200 Hz). The generator must key and label each over with its own sender's settings — Task 6 (`test_qso_overs_alternate_with_each_senders_speed_and_a_turn_gap`, `test_each_station_keys_on_its_own_carrier_and_level`, `test_qso_labels_record_sender_speed_and_style_of_every_over`, `test_station_labels_score_each_station_at_its_own_frequency`). With option 1 (owner decisions 2026-09-29) the detector decides the station: the tracker fine-tunes only within ±12 Hz of the anchor the engine gives it, and follows a new anchor at once — Task 10 (`FineTunesOnlyNearItsAnchor`, `SetAnchorJumpsOnlyBeyondTheFineTuneRange`), Task 12 (`MatchedFollowsTheAnchorItIsGiven`); with its anchor held on the caller, the Matched decoder ignores a station 50 Hz away and 10 dB weaker, 70 Hz away and 6 dB weaker, and 100 Hz away at 6 dB weaker or 10 dB stronger, re-acquires after a silence and returns to the narrow filter when nothing answers, and its filter grows at most ×1.25 per mark from its first follow step — Task 12 (`MatchedIgnoresAWeakStation50HzAway`, `MatchedIgnoresAStation70HzAway`, `MatchedIgnoresANeighbor100HzAwayInASilence`, `MatchedReturnsToTheNarrowFilterWhenNothingAnswers`, `MatchedFilterGrowsAtMostTheBoundPerMark`); at engine level a turnover within D stays on one track and the channel follows the answering station and comes back (25 Hz at −6 dB and 40 Hz at 0 dB re the caller's key-down power), and a station beyond D gets its own track while the caller's channel ignores it (100 Hz at −6 dB; 60 Hz at +6 dB) — Task 13 (`TurnoverWithinTheChannelDistanceFollowsTheAnsweringStation`, `TurnoverAt40HzFollowsTheAnsweringStation`, `TurnoverBeyondTheChannelDistanceGetsItsOwnTrack`, `StrongerStation60HzAwayKeepsItsOwnTrack`). Stated, not asserted: the retune delay after a turnover to a station on another frequency within D (1.3–2.3 s, simulated; median characters lost 0 at 10 Hz and at 0 dB re the caller or stronger, up to 3 at 40–50 Hz and −10 dB); the 50 Hz, 0 dB runaway (the first-step bound's effect on it is to be measured). The benchmark must score each regime against the right frequency — Task 9 (`test_qsos_are_scored_per_over_per_station_and_for_track_splits`). How well the decoders cope is measured, not asserted: group H in Task 14.
7. **Noise alone, noise rises and weak signals:** the noise estimate must not depend on the keying decision, must stay unbiased in noise alone at every filter length, must recover from a 6 dB rise in the noise within 60 s and stop keying by then, and must show its known leak at S₅₀₀ = −5 dB; the squelch must scale with K — Task 11 (`NoiseAloneKeepsTheNoiseEstimateAndNeverKeys`, `NoiseEstimateRecoversFromANoiseStep`, `NoiseEstimateAtMinusFiveDbS500HasItsKnownLeak`, `SquelchScalesWithTheFilterLength`), each over 10–20 seeds with bands from the simulated spread.

## File map

| File | Responsibility |
|---|---|
| `training/kz4ap_synth/messages.py` (new) | Message text: callsigns, operators, CQ calls, contest exchanges, ragchew QSOs, VE3NEA-statistics filler (MIT tables with notice) |
| `training/kz4ap_synth/keying.py` (new) | Keying styles (exact "machine" plus VE3NEA's four): log-normal timing, imbalance, style mix, speed changes |
| `training/kz4ap_synth/fading.py` (new) | Rayleigh fading gain with a Gaussian or VE3NEA's Butterworth Doppler spectrum |
| `training/kz4ap_synth/generate.py` | Signal options, keying edges, interval planning, labels with transmissions, band-scenario settings, two-station QSOs (`Sender`, `qso_spec`, `station_labels`, `draw_answer_offset_hz`) |
| `training/kz4ap_synth/suites.py` (new) | Named suites, recording generation, bench runner, summaries with bootstrap intervals and paired differences, QSO regimes, per-over scoring, tracks per QSO |
| `training/tests/test_messages.py`, `test_keying.py`, `test_fading.py`, `test_suites.py` (new); `test_generate.py` | Tests for the above |
| `bench/src/labels.*` | Labels with transmissions and a `score` flag |
| `bench/src/scoring.*` | Alignment, character/space/first-word error rates, per-transmission counts, no-space CER, match by order |
| `bench/src/cpu_time.*` (new) | Portable process CPU time |
| `bench/src/main.cpp` | `--oracle`, `--front-end`, new outputs, CPU per channel-second |
| `bench/tests/*` | Tests for labels and scoring |
| `engine/include/kz4ap/frequency_tracker.hpp`, `engine/src/frequency_tracker.cpp` (new) | NCO and frequency discriminator |
| `engine/include/kz4ap/matched_front_end.hpp`, `engine/src/matched_front_end.cpp` (new) | ln I₀, LLR, boxcar filter, noise and amplitude estimates, re-acquisition |
| `engine/include/kz4ap/classical_decoder.hpp`, `engine/src/classical_decoder.cpp` | `FrontEnd::Matched` mode, re-acquisition after a silence, the frequency anchor, the filter's growth bound |
| `engine/include/kz4ap/decoder.hpp` | `DecodeUpdate::freq_offset_hz`, `Decoder::set_frequency_anchor_hz` |
| `engine/include/kz4ap/signal_detector.hpp`, `engine/src/signal_detector.cpp` | Tracks follow their own peak within D, distance attribution, neighborhoods in Hz |
| `engine/include/kz4ap/engine.hpp`, `engine/src/engine.cpp` | Oracle channels, statistics, initial offset, the tracker's anchor from the detector, refined frequency, channel distance, `with_envelope_path` |
| `engine/tests/*` | New and extended tests; `test_signals.hpp` gains carrier drift |
| `bench/smoke.sh`, `bench/baselines/smoke-matched.json` (new) | Smoke check pinned to the Envelope path (Task 13) and of the Matched path (Task 14) |
| `docs/signal-processing.md`, `docs/backlog.md`, `docs/research/decoder-survey.md`, `README.md` | Documentation |

## Tasks

- Task 1: Message text — CQ calls, contest exchanges, ragchew QSOs, VE3NEA-statistics filler
- Task 2: Generator — transmissions with pauses, tune-up carriers, carrier drift (smoke recording guarded)
- Task 3: Generator — keying styles (VE3NEA's), key imbalance, speed changes
- Task 4: Generator — Rayleigh fading, Gaussian or VE3NEA's Butterworth Doppler spectrum
- Task 5: Generator — band-scenario settings, interferers, tags
- Task 6: Generator — two-station QSOs: alternating overs, the answering station 0–200 Hz away
- Task 7: Bench — word spaces and first words scored separately
- Task 8: Engine and bench — oracle channels and CPU time per channel-second
- Task 9: Benchmark suites and runner
- Task 10: Frequency tracker
- Task 11: Matched front end
- Task 12: Classical decoder — the Matched mode
- Task 13: Engine — the detector decides the station, re-centering from the detector, refined frequency, the Matched default, `--front-end`
- Task 14: Measure, document, and guard the new path in CI

---

### Task 1: Message text — CQ calls, contest exchanges, ragchew QSOs, VE3NEA-statistics filler

The owner's decision of 2026-09-27: test transmissions must include whole ragchew exchanges, not only CQ calls and contest exchanges. This task writes the text side only, as a pure module with no signal code: plausible callsigns, operators (name, QTH, rig, antenna, power), CQ calls, contest exchanges, whole ragchew QSOs as a list of overs with the sending station of each, and statistical filler text. Task 6 keys a QSO's overs at each station's own speed and style; Task 9's suites use all of it.

**Content rules (heuristic, from operating practice):**
- A ragchew runs: station 0 calls CQ; station 1 answers; then they alternate overs: RST report, name and QTH; rig, power, antenna and weather; `chat_rounds` rounds of chat; and the closing. Each over after the answer starts with the other station's call, DE, its own call.
- `<BT>` separates thoughts inside an over and is never first, last, or doubled. Every over between the CQ and the closing ends by handing over: `<AR> {other} DE {me} <KN>` (the answer ends with `<AR>`). The CQ ends with K. Each station's closing over ends with `<SK>`, and `<SK>` appears nowhere else.
- The usual abbreviations appear in fixed places, so a test can find them: FB, OM, TNX, UR, HR, ES, WX, RIG, ANT, PWR, 73, GL, HPE CUAGN, plus RST, NAME, QTH, FER, R, GM/GA/GE, HW CPY?.
- Callsigns: US 1×2, 1×3, 2×1, 2×2 and 2×3 formats and 20 DX prefixes, one in 20 portable (/P). A DX operator's QTH is a city in that prefix's country; a US operator's is a US city and state.
- **Filler text** uses VE3NEA's on-air tables (41 characters with integer weights summing to 2688; word lengths 1–16 characters, mean 3.06), copied from DeepCW under the MIT license with his copyright and permission notice in the code (`deepcw-generator-notes.md` §4). His "=" is written `<BT>`. Drawing a word length from his normalized distribution is the same as his end-of-word hazard loop (derived: the hazard p_L/Σ_{j≥L} p_j reproduces p_L). Only the filler uses his tables; every template is ours.
- Every function draws only from the numpy `Generator` it is given, so the same generator state gives the same text.

**Files:**
- Create: `training/kz4ap_synth/messages.py`
- Test: `training/tests/test_messages.py` (new)
- Modify: `docs/signal-processing.md` (§11)

**Interfaces:**
- Consumes: `kz4ap_synth.morse.CODES`, `symbols` (tests only).
- Produces (Python, `kz4ap_synth.messages`):
  - `Operator(call: str, name: str, qth: str, rig: str, antenna: str, power_w: int)` (frozen dataclass).
  - `Over(sender: int, text: str)` (frozen dataclass; sender 0 called CQ, 1 answered).
  - `callsign(rng) -> str`; `prefix(call: str) -> str`; `random_operator(rng) -> Operator`.
  - `cq_call(rng, call: str) -> str`; `contest_exchange(rng, call: str, other: str) -> str`.
  - `ragchew(rng, a: Operator, b: Operator, chat_rounds: int = 0) -> list[Over]` (8 + 2·chat_rounds overs).
  - `random_text(rng, words: int) -> str`.
  - `VE3NEA_CHAR_WEIGHTS: dict[str, int]`, `VE3NEA_WORD_LENGTH_PROBS: list[float]`.

- [ ] **Step 1: Write the failing tests**

Create `training/tests/test_messages.py`:

```python
import re

import numpy as np
import pytest

from kz4ap_synth.messages import (
    VE3NEA_CHAR_WEIGHTS,
    VE3NEA_WORD_LENGTH_PROBS,
    callsign,
    contest_exchange,
    cq_call,
    ragchew,
    random_operator,
    random_text,
)
from kz4ap_synth.morse import CODES, symbols

CALL_PATTERN = re.compile(r"^[A-Z]{1,2}\d[A-Z]{1,3}(/P)?$")


def _qso(seed, chat_rounds=0):
    rng = np.random.default_rng(seed)
    return ragchew(rng, random_operator(rng), random_operator(rng), chat_rounds)


def _words(over):
    return over.text.split()


def test_callsigns_look_real():
    rng = np.random.default_rng(1)
    calls = [callsign(rng) for _ in range(500)]
    assert all(CALL_PATTERN.match(c) for c in calls)
    assert len(set(calls)) > 450
    assert any(c.endswith("/P") for c in calls)


def test_the_same_seed_gives_the_same_text():
    assert _qso(3, chat_rounds=1) == _qso(3, chat_rounds=1)
    assert _qso(3) != _qso(4)
    assert random_text(np.random.default_rng(5), 50) == random_text(np.random.default_rng(5), 50)


def test_every_symbol_can_be_keyed():
    texts = [o.text for o in _qso(6, chat_rounds=2)]
    rng = np.random.default_rng(7)
    texts += [cq_call(rng, "K1ABC"), random_text(rng, 300)]
    texts += [contest_exchange(rng, "K1ABC", "W9XYZ") for _ in range(50)]
    for text in texts:
        for word in text.split():
            assert all(s in CODES for s in symbols(word)), word


def test_overs_alternate_starting_with_the_cq():
    overs = _qso(8, chat_rounds=2)
    assert len(overs) == 8 + 2 * 2
    assert [o.sender for o in overs] == [0, 1] * 6
    assert _words(overs[0])[:2] == ["CQ", "CQ"]
    assert _words(overs[0])[-1] == "K"


def test_prosigns_sit_where_operators_send_them():
    overs = _qso(9, chat_rounds=1)
    for over in overs[1:-2]:
        assert _words(over)[-1] in ("<AR>", "<KN>")
    for over in overs[-2:]:
        assert _words(over)[-1] == "<SK>"
    assert all("<SK>" not in _words(o) for o in overs[:-2])
    for over in overs:
        words = _words(over)
        bt = [i for i, w in enumerate(words) if w == "<BT>"]
        assert all(0 < i < len(words) - 1 for i in bt)            # never first or last
        assert all(b - a > 1 for a, b in zip(bt, bt[1:]))          # never two in a row


def test_a_ragchew_carries_the_usual_content_and_abbreviations():
    rng = np.random.default_rng(10)
    a, b = random_operator(rng), random_operator(rng)
    text = " ".join(o.text for o in ragchew(rng, a, b))
    words = set(text.split())
    for abbreviation in ("FB", "OM", "TNX", "UR", "HR", "ES", "WX", "RIG", "ANT", "PWR", "73", "GL", "RST",
                         "NAME", "QTH"):
        assert abbreviation in words, abbreviation
    assert "HPE CUAGN" in text
    for fact in (a.call, b.call, a.name, b.name, a.qth, b.qth, a.rig, b.rig, a.antenna, b.antenna,
                 f"{a.power_w}W", f"{b.power_w}W"):
        assert fact in text, fact


def test_contest_exchanges_include_cq_reports_and_tu():
    rng = np.random.default_rng(11)
    texts = [contest_exchange(rng, "K1ABC", "W9XYZ") for _ in range(200)]
    assert any(t.startswith("CQ TEST K1ABC") for t in texts)
    assert any(t.startswith("W9XYZ 5NN ") for t in texts)
    assert "TU K1ABC" in texts


def test_random_text_follows_ve3nea_statistics():
    words = random_text(np.random.default_rng(12), 20000).split()
    p = np.array(VE3NEA_WORD_LENGTH_PROBS) / sum(VE3NEA_WORD_LENGTH_PROBS)
    lengths = np.array([len(symbols(w)) for w in words])
    assert np.mean(lengths) == pytest.approx(np.sum(np.arange(len(p)) * p), abs=0.05)  # 3.06 characters
    chars = [s for w in words for s in symbols(w)]
    total = sum(VE3NEA_CHAR_WEIGHTS.values())
    for c in ("E", "T", "N", "<BT>", "/"):
        assert chars.count(c) / len(chars) == pytest.approx(VE3NEA_CHAR_WEIGHTS[c] / total, abs=0.004), c
    assert min(lengths) >= 1 and max(lengths) <= 16
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `.venv\Scripts\python -m pytest training/tests/test_messages.py -q`
Expected: collection error, `ModuleNotFoundError: No module named 'kz4ap_synth.messages'`.

- [ ] **Step 3: Implement**

Create `training/kz4ap_synth/messages.py`. Before committing, open DeepCW's `LICENSE` at commit `2c8fdac01bb2bf07d80989b0e5aabfe8cb87d76b` and confirm the notice below matches it word for word (it is the standard MIT text with his copyright line); if it differs, copy his.

```python
"""Message text for synthetic CW: CQ calls, contest exchanges, ragchew QSOs,
and statistical filler text.

The templates, callsigns, names, places and the order of a QSO are this
project's own. The filler text (random_text) draws characters and word
lengths from VE3NEA's on-air tables, reproduced below under the MIT license.
Every function takes a numpy Generator and draws only from it, so the same
generator state always gives the same text.
"""

from __future__ import annotations

import re
from dataclasses import dataclass

import numpy as np

# VE3NEA_CHAR_WEIGHTS and VE3NEA_WORD_LENGTH_PROBS are copied from VE3NEA's
# DeepCW (https://github.com/VE3NEA/DeepCW, commit
# 2c8fdac01bb2bf07d80989b0e5aabfe8cb87d76b, data_generation.ipynb cell 3,
# morse_code.py), where they are said to be "collected from a large number of
# CW messages decoded with CW Skimmer on the Ham bands". His "=" (-...-) is
# written "<BT>" here. Notes: docs/research/deepcw-generator-notes.md §4.
#
# MIT License
#
# Copyright (c) 2024 Alex Shovkoplyas
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in all
# copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.
VE3NEA_CHAR_WEIGHTS = {
    "1": 13, "2": 14, "3": 33, "4": 43, "5": 41, "6": 8, "7": 14, "8": 10, "9": 14, "0": 11,
    "A": 127, "B": 62, "C": 69, "D": 84, "E": 321, "F": 55, "G": 43, "H": 68, "I": 130,
    "J": 8, "K": 117, "L": 100, "M": 76, "N": 168, "O": 126, "P": 57, "Q": 68, "R": 95,
    "S": 159, "T": 236, "U": 61, "V": 23, "W": 95, "X": 16, "Y": 40, "Z": 12,
    "/": 19, ".": 12, ",": 9, "?": 16, "<BT>": 15,
}
VE3NEA_WORD_LENGTH_PROBS = [0.0, 0.1672, 0.2569, 0.1939, 0.1745, 0.0921, 0.025, 0.008, 0.006,
                            0.004, 0.003, 0.003, 0.002, 0.002, 0.002, 0.001, 0.001]  # index = characters
# End of the material copied from DeepCW.

LETTERS = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
US_PREFIXES_1 = ("K", "N", "W")
US_PREFIXES_2 = ("AA", "AB", "AC", "AD", "AE", "AF", "AG", "AI", "AJ", "AK", "KA", "KB", "KC", "KD",
                 "KE", "KF", "KG", "KI", "KJ", "KK", "KN", "KO", "NA", "NB", "NC", "WA", "WB", "WD")
DX_QTHS = {"DL": "MUNICH", "G": "LEEDS", "F": "LYON", "JA": "OSAKA", "VE": "OTTAWA", "EA": "MADRID",
           "I": "ROME", "OH": "HELSINKI", "SM": "UPPSALA", "UA": "MOSCOW", "PY": "RIO", "VK": "PERTH",
           "ZL": "AUCKLAND", "LU": "CORDOBA", "ON": "GHENT", "PA": "UTRECHT", "OK": "BRNO",
           "SP": "KRAKOW", "HA": "BUDAPEST", "YO": "CLUJ"}
US_QTHS = ("BOSTON MA", "AUSTIN TX", "DENVER CO", "TULSA OK", "OMAHA NE", "RENO NV", "MACON GA",
           "BANGOR ME", "FRESNO CA", "DAYTON OH", "BOISE ID", "SALEM OR")
NAMES = ("JOHN", "BOB", "JIM", "TOM", "BILL", "MIKE", "DAVE", "STEVE", "ED", "AL", "DON", "JOE",
         "KEN", "RON", "PAT", "SUE", "ANN", "MARY", "HANS", "PETE")
RIGS = ("K3", "IC7300", "FT991", "TS590", "KX3", "FT710", "IC705", "HOMEBREW")
ANTENNAS = ("DIPOLE", "VERTICAL", "3 EL YAGI", "EFHW", "LOOP", "INV VEE", "LONG WIRE")
POWERS_W = (5, 10, 50, 100, 500)
WEATHER = ("SUNNY", "CLOUDY", "RAIN", "SNOW", "WINDY", "FOGGY", "CLEAR")
STATES = ("MA", "TX", "CO", "OK", "NE", "NV", "GA", "ME", "CA", "OH", "ID", "OR")
CHAT_LINES = ("BEEN LICENSED {years} YRS", "UR SIGS FB HR", "SOME QSB ON UR SIG", "BAND CONDX GUD TDY",
              "RETIRED NW ES ENJOY CW", "AGE HR {age}", "QRM HR BUT UR OK")

_CALL = re.compile(r"^([A-Z]+)\d[A-Z]{1,3}(/P)?$")


@dataclass(frozen=True)
class Operator:
    """Who sends: the facts a ragchew exchanges. Speed and keying belong to the generator."""
    call: str
    name: str
    qth: str
    rig: str
    antenna: str     # includes the height where one is sent, e.g. "DIPOLE UP 40 FT"
    power_w: int     # transmitter output power, W


@dataclass(frozen=True)
class Over:
    """One station's turn in a QSO, from its first key-down to the hand-over."""
    sender: int      # 0: the station that called CQ; 1: the station that answered
    text: str


def _pick(rng, options):
    return options[int(rng.integers(len(options)))]


def _letters(rng, count: int) -> str:
    return "".join(LETTERS[int(i)] for i in rng.integers(26, size=count))


def callsign(rng) -> str:
    """A plausible amateur callsign: a US 1x2, 1x3, 2x1, 2x2 or 2x3 call, or a DX prefix,
    digit and 1-3 letters; one call in 20 is portable (/P)."""
    kind = int(rng.integers(6))
    digit = str(int(rng.integers(10)))
    if kind == 0:
        call = _pick(rng, US_PREFIXES_1) + digit + _letters(rng, 2)
    elif kind == 1:
        call = _pick(rng, US_PREFIXES_1) + digit + _letters(rng, 3)
    elif kind == 2:
        call = _pick(rng, US_PREFIXES_2) + digit + _letters(rng, int(rng.integers(1, 4)))
    else:
        call = _pick(rng, tuple(DX_QTHS)) + digit + _letters(rng, int(rng.integers(1, 4)))
    return call + ("/P" if rng.random() < 0.05 else "")


def prefix(call: str) -> str:
    """The letters before the call's digit."""
    match = _CALL.match(call)
    if not match:
        raise ValueError(f"not a callsign: {call!r}")
    return match.group(1)


def random_operator(rng) -> Operator:
    call = callsign(rng)
    qth = DX_QTHS.get(prefix(call)) or _pick(rng, US_QTHS)
    antenna = _pick(rng, ANTENNAS)
    if rng.random() < 0.5:
        antenna += f" UP {int(rng.integers(3, 16)) * 5} FT"
    return Operator(call, _pick(rng, NAMES), qth, _pick(rng, RIGS), antenna, int(_pick(rng, POWERS_W)))


def cq_call(rng, call: str) -> str:
    """A general call: CQ two or three times, DE, the call two or three times, K."""
    cq = " ".join(["CQ"] * int(rng.integers(2, 4)))
    calls = " ".join([call] * int(rng.integers(2, 4)))
    return f"{cq} DE {calls} K"


def contest_exchange(rng, call: str, other: str) -> str:
    """One contest transmission by call, working other: a CQ, a report with a serial
    number, zone or state, or a TU."""
    kind = int(rng.integers(5))
    if kind == 0:
        return f"CQ TEST {call} {call}"
    if kind == 1:
        return f"{other} 5NN {int(rng.integers(1, 1000))}"
    if kind == 2:
        return f"{other} 5NN {int(rng.integers(1, 41)):02d}"
    if kind == 3:
        return f"{other} 5NN {_pick(rng, STATES)}"
    return f"TU {call}"


def _rst(rng) -> str:
    return f"{int(rng.integers(3, 6))}{int(rng.integers(3, 10))}9"


def _greeting(rng) -> str:
    return _pick(rng, ("GM", "GA", "GE"))


def _temperature(rng, qth: str) -> str:
    if qth in DX_QTHS.values():
        return f"TEMP {int(rng.integers(0, 36))}C"
    return f"TEMP {int(rng.integers(32, 96))}F"


def _chat(rng) -> str:
    """Two different lines of chat, separated by <BT>."""
    first, second = rng.choice(len(CHAT_LINES), size=2, replace=False)
    return " <BT> ".join(CHAT_LINES[int(i)].format(years=int(rng.integers(2, 60)), age=int(rng.integers(16, 90)))
                         for i in (first, second))


def ragchew(rng, a: Operator, b: Operator, chat_rounds: int = 0) -> list[Over]:
    """A whole ragchew QSO: a calls CQ, b answers, then they alternate overs: reports
    and names and QTHs, rigs, antennas, power and weather, chat_rounds rounds of chat,
    and the closing. <BT> separates thoughts inside an over; every over before the
    closing ends by handing over with <AR> or <KN>; each closing over ends with <SK>."""
    ga, gb = _greeting(rng), _greeting(rng)
    rst_a, rst_b = _rst(rng), _rst(rng)
    overs = [
        Over(0, cq_call(rng, a.call)),
        Over(1, f"{a.call} DE {b.call} {b.call} <AR>"),
        Over(0, f"{b.call} DE {a.call} <BT> {ga} OM ES TNX FER CALL <BT> UR RST {rst_b} {rst_b} "
                f"<BT> NAME HR {a.name} {a.name} <BT> QTH {a.qth} {a.qth} <BT> HW CPY? "
                f"<AR> {b.call} DE {a.call} <KN>"),
        Over(1, f"{a.call} DE {b.call} <BT> R R {gb} {a.name} TNX FER RPT <BT> UR RST {rst_a} {rst_a} "
                f"<BT> NAME HR {b.name} {b.name} <BT> QTH {b.qth} {b.qth} <BT> HW? "
                f"<AR> {a.call} DE {b.call} <KN>"),
        Over(0, f"{b.call} DE {a.call} <BT> R FB {b.name} TNX FER RPT ES INFO <BT> RIG HR {a.rig} "
                f"ES PWR {a.power_w}W <BT> ANT {a.antenna} <BT> WX HR {_pick(rng, WEATHER)} ES "
                f"{_temperature(rng, a.qth)} <AR> {b.call} DE {a.call} <KN>"),
        Over(1, f"{a.call} DE {b.call} <BT> FB OM {a.name} <BT> RIG HR {b.rig} ES PWR {b.power_w}W "
                f"<BT> ANT {b.antenna} <BT> WX HR {_pick(rng, WEATHER)} {_temperature(rng, b.qth)} "
                f"<AR> {a.call} DE {b.call} <KN>"),
    ]
    for _ in range(chat_rounds):
        for sender, me, other in ((0, a, b), (1, b, a)):
            overs.append(Over(sender, f"{other.call} DE {me.call} <BT> {_chat(rng)} "
                                      f"<AR> {other.call} DE {me.call} <KN>"))
    overs += [
        Over(0, f"{b.call} DE {a.call} <BT> OK {b.name} TNX FER FB QSO ES HPE CUAGN <BT> 73 ES GL "
                f"{b.call} DE {a.call} <SK>"),
        Over(1, f"{a.call} DE {b.call} <BT> TNX {a.name} FER QSO <BT> 73 GL OM {a.call} DE {b.call} <SK>"),
    ]
    return overs


def random_text(rng, words: int) -> str:
    """words words of i.i.d. characters from VE3NEA's on-air character frequencies, with
    word lengths from his word-length distribution (mean 3.06 characters)."""
    lengths_p = np.array(VE3NEA_WORD_LENGTH_PROBS) / np.sum(VE3NEA_WORD_LENGTH_PROBS)
    lengths = rng.choice(len(lengths_p), size=words, p=lengths_p)
    chars = list(VE3NEA_CHAR_WEIGHTS)
    weights = np.array([VE3NEA_CHAR_WEIGHTS[c] for c in chars], dtype=float)
    drawn = rng.choice(len(chars), size=int(np.sum(lengths)), p=weights / np.sum(weights))
    out, pos = [], 0
    for n in lengths:
        out.append("".join(chars[int(i)] for i in drawn[pos:pos + n]))
        pos += n
    return " ".join(out)
```

- [ ] **Step 4: Run the tests to verify they pass**

Run: `.venv\Scripts\python -m pytest training -q`
Expected: all pass (the existing tests plus 8 new). A full ragchew with `chat_rounds=0` is about 150 words; at 25 WPM its overs take about 300 s of keying (measured on five seeds: 285–309 s), which is why Task 9 sizes those recordings from the plan rather than using a fixed 60 s.

- [ ] **Step 5: Document**

In `docs/signal-processing.md`, section 11, add at the end:

```markdown
- **Message text** (synthetic recordings, `training/kz4ap_synth/messages.py`):
  CQ calls, contest exchanges, and whole ragchew QSOs as a list of overs,
  each with its sending station: CQ, answer, RST and name and QTH, rig and
  power and antenna and weather, optional chat, closing. `<BT>` separates
  thoughts inside an over; every over before the closing ends with `<AR>` and
  `<KN>` (the answer with `<AR>`); each station's closing over ends with `<SK>`.
  Templates and callsigns are this project's (heuristic). Filler text draws
  i.i.d. characters and word lengths from VE3NEA's on-air tables (DeepCW,
  MIT; E is 11.9% of characters, mean word length 3.06 characters).
```

- [ ] **Step 6: Commit on the `milestone-2` branch**

```powershell
git add training/kz4ap_synth/messages.py training/tests/test_messages.py docs/signal-processing.md
```
```powershell
git commit -m "Add message text: CQ calls, contest exchanges, ragchew QSOs and filler text"
```

---

### Task 2: Generator — transmissions with pauses, tune-up carriers, carrier drift

A signal can now send its text several times with pauses between (as between CQs), start with an unkeyed tune-up carrier, start at the very first sample, and drift in frequency. The labels file lists each transmission, so the bench can score each one's first word (Task 7). Random keying (Task 3) and fading (Task 4) will draw from per-signal generators, so this task also restructures interval planning around them. **The milestone-1 output must not change**: a test compares the new generator against a frozen copy of the old one.

**Files:**
- Modify: `training/kz4ap_synth/generate.py` (whole file shown below)
- Modify: `training/tests/test_generate.py`
- Modify: `docs/signal-processing.md` (§11)

**Interfaces:**
- Produces (Python, `kz4ap_synth.generate`):
  - `SignalSpec` gains `repeats: int = 1`, `pause_s: float = 0.0`, `tune_s: float = 0.0`, `drift_hz_per_s: float = 0.0`.
  - `TUNE_GAP_S = 0.5` (s of silence between a tune-up carrier and the first element).
  - `SignalPlan(intervals: list[tuple[float, float]], transmissions: list[tuple[float, float]])`, times in s from `spec.start_s`.
  - `sending_intervals(spec: SignalSpec, rng: np.random.Generator) -> list[tuple[float, float]]` (key-down intervals of one sending of `spec.text`, from 0 s; Task 3 replaces its body).
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
Expected: `smoke test passed`, with the same `CER …, detected 8 of 8 …` line as before this task. Write that CER value down and pass it to the reviewer: Tasks 5–14 compare against it.

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

### Task 3: Generator — keying styles (VE3NEA's), key imbalance, speed changes

Senders other than a keyer: element and space durations drawn from log-normal distributions per keying style (survey scenario C), a key-on/off imbalance, and speed changes inside a message, as a step or a ramp (scenario D). The four random styles are VE3NEA's DeepCW styles with his parameters (`deepcw-generator-notes.md` §1): every duration is T·exp(N(μ, σ_ln²)), T = 1.2 s / WPM, with his μ and σ_ln per element:

| Style (his name) | μ: dit, dah, element space, character space, word space | σ_ln: same order |
|---|---|---|
| computer (Computer) | 0, 1.10, 0, 1.10, 1.94 | 0.05, 0.016, 0.05, 0.016, 0.008 |
| paddle (Paddle) | 0, 1.10, 0, 1.10, 1.94 | 0.05, 0.016, 0.05, 0.2, 0.2 |
| bug (Vibroplex) | 0, 1.10, 0, 1.10, 1.94 | 0.05, 0.2, 0.05, 0.2, 0.2 |
| hand (HandKey) | 0, 1.50, 0, 1.50, 2.0 | 0.15, 0.3, 0.2, 0.3, 0.2 |

Medians exp(μ) are 1, 3.00, 1, 3.00 and 6.96 dits (the standard ratios) except for the hand key: dah and character space 4.48 dits, word space 7.39 dits. "machine" (exact PARIS timing, σ_ln = 0) is this project's own and stays the default, so milestone-1 recordings do not change. His only per-operator variation is the imbalance δ ~ N(0, (0.1·T)²), drawn once per operator (`draw_imbalance_dits`), added to every mark and subtracted from every space. His training mix is hand 0.25, paddle 0.50, computer 0.25 (`draw_style`; he never draws Vibroplex). His speed range is ambiguous: `'wpm': {'low':12, 'high':48}` in the committed `R/model/training_settings.py`, but `{'low':8, 'high':50}` in the notebook cell (MOD cell 2) that writes that file, so which range trained his published weights is unknown; `VE3NEA_WPM_RANGE` uses 12–48 WPM and its comment says so. Differences kept on purpose (heuristic): character and word spaces are drawn once each from their own distributions (he sums several independent draws: the same medians, slightly less spread here), and no duration is shorter than 0.2 dit.

**Keying edges (review finding I4).** Milestone 1's `keying_envelope` puts its 5 ms raised-cosine ramps *inside* each interval, so every mark is 5 ms shorter, and every space 5 ms longer, at 50% amplitude than its interval: a built-in imbalance of −5 ms/T (−0.10 dit at 24 WPM, −0.17 dit at 40 WPM; derived), 1–1.7 times the standard deviation of VE3NEA's per-operator δ. Changing that for every signal would break the milestone-1 bit-for-bit test, so this task adds two options instead: `edge_s` (the rise and fall time, default 5 ms) and `edges_centered` (default False). With `edges_centered`, each edge is centered on the interval's end, so the 50%-amplitude duration equals the interval, which is VE3NEA's convention (his edges are 2 ms, `deepcw-generator-notes.md` §1.3). Group B (Task 9) uses 2 ms centered edges; every other group keeps milestone 1's edges, and the difference is stated wherever results are compared with VE3NEA's.

**Files:**
- Create: `training/kz4ap_synth/keying.py`
- Modify: `training/kz4ap_synth/generate.py` (`SignalSpec`, `sending_intervals`, `keying_envelope`, `generate`)
- Test: `training/tests/test_keying.py` (new); `training/tests/test_generate.py`
- Modify: `docs/signal-processing.md` (§11)

**Interfaces:**
- Consumes: `kz4ap_synth.morse.CODES`, `symbols`, `keying_intervals`; `generate.SignalSpec`, `plan_intervals` (Task 2).
- Produces (Python):
  - `kz4ap_synth.keying.STYLES: dict[str, KeyingStyle]` with keys `"machine"`, `"computer"`, `"paddle"`, `"bug"`, `"hand"`; `Duration(median_dits: float, sigma: float)`; `KeyingStyle(dit, dah, element_gap, char_gap, word_gap)`; `MIN_DITS = 0.2`.
  - `VE3NEA_STYLE_MIX = (("hand", 0.25), ("paddle", 0.50), ("computer", 0.25))`; `VE3NEA_IMBALANCE_SIGMA_DITS = 0.1`; `VE3NEA_WPM_RANGE = (12.0, 48.0)`.
  - `draw_style(rng: np.random.Generator) -> str`; `draw_imbalance_dits(rng: np.random.Generator) -> float`.
  - `timed_intervals(text: str, wpm: float, style: str = "machine", rng: np.random.Generator | None = None, wpm_end: float | None = None, profile: str = "step", imbalance_dits: float = 0.0) -> list[tuple[float, float]]`.
  - `SignalSpec` gains `keying: str = "machine"`, `wpm_end: float | None = None`, `speed_profile: str = "step"`, `imbalance_dits: float = 0.0`, `edge_s: float = RISE_S` (0.005 s), `edges_centered: bool = False`. These appear in the labels file automatically.
  - `keying_envelope(intervals, offset_s, n, sample_rate, rise_s: float = RISE_S, centered: bool = False) -> np.ndarray` (the defaults reproduce milestone 1 exactly).

- [ ] **Step 1: Write the failing tests**

Create `training/tests/test_keying.py`:

```python
import math

import numpy as np
import pytest

from kz4ap_synth.keying import STYLES, Duration, draw_imbalance_dits, draw_style, timed_intervals
from kz4ap_synth.morse import keying_intervals


def marks(intervals):
    return [off - on for on, off in intervals]


def spaces(intervals):
    return [b[0] - a[1] for a, b in zip(intervals, intervals[1:])]


def test_machine_style_is_exact_paris_timing():
    assert timed_intervals("PARIS CQ", 20.0) == keying_intervals("PARIS CQ", 20.0)


def test_styles_are_ve3nea_table():
    assert STYLES["paddle"].dah == Duration(math.exp(1.10), 0.016)
    assert STYLES["paddle"].char_gap == Duration(math.exp(1.10), 0.2)
    assert STYLES["computer"].word_gap == Duration(math.exp(1.94), 0.008)
    assert STYLES["bug"].dah == Duration(math.exp(1.10), 0.2)
    assert STYLES["hand"].dit == Duration(1.0, 0.15)
    assert STYLES["hand"].word_gap == Duration(math.exp(2.0), 0.2)


def test_paddle_dahs_are_steadier_than_its_character_spaces():
    rng = np.random.default_rng(1)
    iv = timed_intervals(" ".join(["TT"] * 300), 20.0, "paddle", rng)
    dit = 1.2 / 20.0
    dahs = np.array(marks(iv)) / dit
    char_gaps = np.array(spaces(iv))[0::2] / dit  # T T | T T | ...: character and word spaces alternate
    assert np.median(dahs) == pytest.approx(math.exp(1.10), rel=0.01)
    assert np.std(np.log(dahs)) == pytest.approx(0.016, abs=0.004)
    assert np.std(np.log(char_gaps)) == pytest.approx(0.2, abs=0.03)


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


def test_style_mix_and_imbalance_follow_ve3nea():
    rng = np.random.default_rng(3)
    styles = [draw_style(rng) for _ in range(4000)]
    assert set(styles) == {"hand", "paddle", "computer"}
    assert styles.count("hand") / 4000 == pytest.approx(0.25, abs=0.03)
    assert styles.count("paddle") / 4000 == pytest.approx(0.50, abs=0.03)
    imbalances = [draw_imbalance_dits(rng) for _ in range(4000)]
    assert np.std(imbalances) == pytest.approx(0.1, abs=0.01)
    assert np.mean(imbalances) == pytest.approx(0.0, abs=0.01)


def test_unknown_style_or_profile_raises():
    with pytest.raises(ValueError):
        timed_intervals("E", 20.0, "straight")
    with pytest.raises(ValueError):
        timed_intervals("E E", 20.0, wpm_end=30.0, profile="sine")


def test_random_style_without_a_generator_raises():
    with pytest.raises(ValueError):
        timed_intervals("E", 20.0, "hand")


def test_every_style_is_defined():
    assert set(STYLES) == {"machine", "computer", "paddle", "bug", "hand"}
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


def test_centered_edges_keep_the_marked_length_at_half_amplitude():
    fs = 48000
    inside = keying_envelope([(0.1, 0.2)], 0.0, fs // 2, fs)
    centered = keying_envelope([(0.1, 0.2)], 0.0, fs // 2, fs, rise_s=0.002, centered=True)
    assert np.sum(inside >= 0.5) / fs == pytest.approx(0.1 - 0.005, abs=2 / fs)  # milestone 1: 5 ms short
    assert np.sum(centered >= 0.5) / fs == pytest.approx(0.1, abs=2 / fs)


def test_centered_edges_reach_the_signal():
    fs = 8000
    spec = SignalSpec("E", 500.0, 20.0, 20.0, 0.5, edge_s=0.002, edges_centered=True)
    iq = generate([spec], fs, 1.0, seed=1, add_noise=False)
    level = np.abs(iq) / amplitude_for_snr(20.0, fs)
    assert np.sum(level >= 0.5) / fs == pytest.approx(0.06, abs=2 / fs)  # one 20 WPM dit at 50% amplitude
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `.venv\Scripts\python -m pytest training -q`
Expected: `ModuleNotFoundError: No module named 'kz4ap_synth.keying'`, and `TypeError: ... unexpected keyword argument 'keying'`.

- [ ] **Step 3: Implement**

Create `training/kz4ap_synth/keying.py`:

```python
"""Keying timing: how long each element and space lasts for a given sender.

"machine" is exact PARIS timing (this project's; it keeps milestone-1
recordings unchanged). "computer", "paddle", "bug" and "hand" are VE3NEA's
DeepCW styles Computer, Paddle, Vibroplex and HandKey: every duration is
T * exp(N(mu, sigma^2)), T = 1.2 s / WPM, with his mu and sigma per element
(DeepCW data_generation.ipynb cell 9, keying_stats.py, commit 2c8fdac, MIT;
docs/research/deepcw-generator-notes.md §1). His one per-operator variation
is a key-on/key-off imbalance delta ~ N(0, (0.1 T)^2), added to every mark
and subtracted from every space (draw_imbalance_dits). His training mix is
hand 0.25, paddle 0.50, computer 0.25 (draw_style); his speed range is
12-48 WPM in the committed training_settings.py but 8-50 WPM in the notebook
cell that writes it, so which trained his published model is unknown.
"""

from __future__ import annotations

import math
from dataclasses import dataclass

import numpy as np

from .morse import CODES, keying_intervals, symbols

MIN_DITS = 0.2  # no element or space is drawn shorter than this, dits
VE3NEA_STYLE_MIX = (("hand", 0.25), ("paddle", 0.50), ("computer", 0.25))
VE3NEA_IMBALANCE_SIGMA_DITS = 0.1           # standard deviation of the per-operator imbalance, dits
VE3NEA_WPM_RANGE = (12.0, 48.0)             # committed training_settings.py; the notebook says 8-50


@dataclass(frozen=True)
class Duration:
    median_dits: float  # median duration, dits (exp(mu))
    sigma: float        # standard deviation of ln(duration); 0 = exact


@dataclass(frozen=True)
class KeyingStyle:
    dit: Duration
    dah: Duration
    element_gap: Duration  # space inside a character
    char_gap: Duration     # space between characters
    word_gap: Duration     # space between words


def _ve3nea(mu, sigma) -> KeyingStyle:
    """A style from VE3NEA's (dot, dash, intra, char, word) mu and sigma columns."""
    return KeyingStyle(*(Duration(math.exp(m), s) for m, s in zip(mu, sigma)))


STYLES = {
    # Exact PARIS timing (this project's default; not a VE3NEA style).
    "machine": KeyingStyle(Duration(1, 0), Duration(3, 0), Duration(1, 0), Duration(3, 0), Duration(7, 0)),
    # VE3NEA's Computer: "all timing is accurate", yet dots and element spaces vary by 5%.
    "computer": _ve3nea((0, 1.10, 0, 1.10, 1.94), (0.05, 0.016, 0.05, 0.016, 0.008)),
    # VE3NEA's Paddle: character and word spaces are the operator's.
    "paddle": _ve3nea((0, 1.10, 0, 1.10, 1.94), (0.05, 0.016, 0.05, 0.2, 0.2)),
    # VE3NEA's Vibroplex (a bug): dashes and spaces are the operator's.
    "bug": _ve3nea((0, 1.10, 0, 1.10, 1.94), (0.05, 0.2, 0.05, 0.2, 0.2)),
    # VE3NEA's HandKey: everything by hand, heavy dashes (median e^1.5 = 4.48 dits), wide spacing.
    "hand": _ve3nea((0, 1.50, 0, 1.50, 2.0), (0.15, 0.3, 0.2, 0.3, 0.2)),
}

SPEED_PROFILES = ("step", "ramp")


def draw_style(rng: np.random.Generator) -> str:
    """A keying style drawn with VE3NEA's training mix (hand 0.25, paddle 0.50, computer 0.25)."""
    names = [name for name, _ in VE3NEA_STYLE_MIX]
    return names[int(rng.choice(len(names), p=[p for _, p in VE3NEA_STYLE_MIX]))]


def draw_imbalance_dits(rng: np.random.Generator) -> float:
    """One operator's key-on/key-off imbalance, dits: N(0, 0.1^2), as VE3NEA draws it."""
    return float(VE3NEA_IMBALANCE_SIGMA_DITS * rng.standard_normal())


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
    transmitter that keys on and off at different speeds). Character and word
    spaces are drawn directly from their own distributions (VE3NEA assembles
    them from several draws; the medians are the same). With machine keying,
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
    edge_s: float = RISE_S           # raised-cosine rise and fall time, s
    edges_centered: bool = False     # False: edges inside each mark (milestone 1); True: centered on its ends
```

replace `keying_envelope` with:

```python
def keying_envelope(intervals, offset_s: float, n: int, sample_rate: int, rise_s: float = RISE_S,
                    centered: bool = False) -> np.ndarray:
    """0..1 envelope with raised-cosine edges rise_s long; intervals are shifted by offset_s.
    By default (milestone 1) each edge lies inside its interval, so a mark is rise_s shorter,
    and a space rise_s longer, at 50% amplitude than the interval says. centered: each edge
    is centered on the interval's end, so the 50%-amplitude duration is the interval's
    (VE3NEA's convention)."""
    env = np.zeros(n)
    ramp_len = max(1, int(round(rise_s * sample_rate)))
    ramp = 0.5 - 0.5 * np.cos(np.pi * (np.arange(ramp_len) + 0.5) / ramp_len)
    shift = rise_s / 2 if centered else 0.0
    for on, off in intervals:
        i0 = max(0, int(round((offset_s + on - shift) * sample_rate)))
        i1 = min(n, int(round((offset_s + off + shift) * sample_rate)))
        if i1 <= i0:
            continue
        env[i0:i1] = 1.0
        r = min(ramp_len, (i1 - i0) // 2)
        if r > 0:
            env[i0:i0 + r] *= ramp[:r]
            env[i1 - r:i1] *= ramp[:r][::-1]
    return env
```

(With the defaults, `shift` is 0.0 and `x - 0.0` is exactly `x`, so milestone-1 envelopes are bit-identical.) In `generate`, replace the five lines from `i0 = max(0, int(s.start_s * sample_rate))` through the `env = keying_envelope(...)` call with:

```python
        tail = s.edge_s / 2 if s.edges_centered else 0.0  # a centered edge reaches this far past an interval
        i0 = max(0, int((s.start_s - tail) * sample_rate))
        i1 = min(n, int(np.ceil((s.start_s + intervals[-1][1] + tail) * sample_rate)) + 1)
        if i1 <= i0:
            continue
        env = keying_envelope(intervals, s.start_s - i0 / sample_rate, i1 - i0, sample_rate, s.edge_s,
                              s.edges_centered)
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
Expected: all pass, including `test_default_signals_match_milestone_1_generator` (machine keying still goes through `keying_intervals`, and the default edges take the old arithmetic).

- [ ] **Step 5: Document the keying styles**

In `docs/signal-processing.md`, section 11, after the **Drift** bullet added in Task 2, insert:

```markdown
- **Keying styles** (synthetic recordings, `keying`): every element and
  space duration is T·exp(N(μ, σ_ln²)), T = 1.2 s / WPM, never below
  0.2 dit. "machine" is exact PARIS timing (σ_ln = 0; the default). The
  others are VE3NEA's DeepCW styles with his parameters (MIT; notes in
  `docs/research/deepcw-generator-notes.md` §1):

  | Style (his name) | μ: dit, dah, element space, character space, word space | σ_ln: same order |
  |---|---|---|
  | computer (Computer) | 0, 1.10, 0, 1.10, 1.94 | 0.05, 0.016, 0.05, 0.016, 0.008 |
  | paddle (Paddle) | 0, 1.10, 0, 1.10, 1.94 | 0.05, 0.016, 0.05, 0.2, 0.2 |
  | bug (Vibroplex) | 0, 1.10, 0, 1.10, 1.94 | 0.05, 0.2, 0.05, 0.2, 0.2 |
  | hand (HandKey) | 0, 1.50, 0, 1.50, 2.0 | 0.15, 0.3, 0.2, 0.3, 0.2 |

  Medians exp(μ): 1, 3.00, 1, 3.00, 6.96 dits; hand key 1, 4.48, 1, 4.48,
  7.39 dits. Character and word spaces are one draw each (VE3NEA sums
  several draws; same medians). `imbalance_dits` = δ/T lengthens every mark
  and shortens every space; VE3NEA draws δ ~ N(0, (0.1·T)²) once per
  operator, and his training mix is hand 0.25, paddle 0.50, computer 0.25.
  `wpm_end` changes the speed within a sending: `step` switches at the
  middle word, `ramp` changes linearly from word to word.
- **Keying edges** (synthetic recordings, `edge_s`, `edges_centered`):
  raised-cosine rise and fall of `edge_s` (default 5 ms). By default each
  edge lies inside its mark, so a mark is `edge_s` shorter, and a space
  `edge_s` longer, at 50% amplitude than its nominal length: an imbalance
  of −5 ms/T, −0.10 dit at 24 WPM (derived). With `edges_centered`, edges
  are centered on the mark's ends and the 50%-amplitude length is the
  nominal one (VE3NEA's convention; he uses 2 ms edges).
```

- [ ] **Step 6: Commit on the `milestone-2` branch**

```powershell
git add training/kz4ap_synth/keying.py training/kz4ap_synth/generate.py training/tests/test_keying.py training/tests/test_generate.py docs/signal-processing.md
```
```powershell
git commit -m "Add keying styles, key imbalance and speed changes to the generator"
```

---

### Task 4: Generator — Rayleigh fading, Gaussian or VE3NEA's Butterworth Doppler spectrum

Flat Rayleigh fading (QSB) with a Doppler-spread parameter f_D and a choice of Doppler spectrum, so VE3NEA's grid (f_D = 0.1–3 Hz; survey scenario B) can be reproduced as an external anchor with his own spectrum shape.

**Model (derived from the definition; the spread convention and the shapes are choices):** the carrier is multiplied by a complex Gaussian gain g(t) with E|g|² = 1. Its Doppler power spectrum S(f) is either

- **Gaussian** (default): S(f) ∝ exp(−f²/(2σ_D²)), frequency spread f_D = 2σ_D (the Watterson / CCIR 520 HF convention); or
- **Butterworth** (VE3NEA's, `deepcw-generator-notes.md` §2): S(f) ∝ 1/(1 + (f/f_c)⁴), f_c = 0.625·f_D, the power response of his 2nd-order Butterworth low-pass per quadrature. His notebook fits a Gaussian to this spectrum and gets 2σ_D = 1.01·f_D, so f_D names the same spread in both shapes to about 1% (conversion factor ≈ 1). The shapes differ in the tails: beyond 2·f_D the Butterworth holds 0.92% of the power, the Gaussian 6.3×10⁻⁵ (derived; Step 1 tests both).

The process is synthesized at 50 samples/s by shaping white complex Gaussian noise in the frequency domain (amplitude response √S(f), scaled so the expected power is exactly 1), then linearly interpolated to the recording's rate. This reproduces his spectrum, not his time-domain filter's exact sample sequence. At 50 samples/s and f_D ≤ 3 Hz the Gaussian is negligible beyond ±9 Hz (6σ_D) and the Butterworth holds 1.3×10⁻⁴ of its power beyond ±25 Hz (derived), where the synthesis cuts it off. Linear interpolation loses at most 0.04 dB (Gaussian) or 0.06 dB (Butterworth) of power relative to the unfaded mean midway between points at f_D = 3 Hz (derived: adjacent-sample correlation ≥ 0.982 and ≥ 0.972; the Butterworth's second-moment spread is f_c). S₅₀₀ of a fading signal is its **mean** key-down power over the noise in 500 Hz, as VE3NEA's SNR is a fading-averaged power. The synthesis is split into `slow_gain` (the 50 samples/s process) and `gain_at` (interpolation), so Task 6 can give each station of a QSO one continuous fading path without holding it at the recording's full sample rate.

**Files:**
- Create: `training/kz4ap_synth/fading.py`
- Modify: `training/kz4ap_synth/generate.py` (`SignalSpec`, `generate`)
- Test: `training/tests/test_fading.py` (new); `training/tests/test_generate.py`
- Modify: `docs/signal-processing.md` (§11)

**Interfaces:**
- Produces (Python, `kz4ap_synth.fading`): `FADING_RATE_HZ = 50.0`; `BUTTERWORTH_CUTOFF_PER_SPREAD = 0.625`; `SHAPES = ("gaussian", "butterworth")`; `slow_gain(duration_s: float, spread_hz: float, rng: np.random.Generator, shape: str = "gaussian") -> np.ndarray` (complex, at `FADING_RATE_HZ`, ceil(duration_s · 50) + 2 samples); `gain_at(slow: np.ndarray, t_s: np.ndarray) -> np.ndarray`; `rayleigh_gain(n: int, sample_rate: int, spread_hz: float, rng: np.random.Generator, shape: str = "gaussian") -> np.ndarray` (complex, length n). An unknown shape or spread_hz ≤ 0 raises `ValueError`.
- `SignalSpec` gains `fading_hz: float = 0.0` (f_D, Hz; 0 = no fading) and `fading_shape: str = "gaussian"`. Signal i's fading draws from `np.random.default_rng([seed, i, 1])`.

- [ ] **Step 1: Write the failing tests**

Create `training/tests/test_fading.py`:

```python
import numpy as np
import pytest

from kz4ap_synth.fading import rayleigh_gain


@pytest.mark.parametrize("shape", ["gaussian", "butterworth"])
def test_mean_power_is_one(shape):
    g = rayleigh_gain(600 * 200, 200, 3.0, np.random.default_rng(1), shape)
    assert np.mean(np.abs(g) ** 2) == pytest.approx(1.0, rel=0.1)


@pytest.mark.parametrize("shape", ["gaussian", "butterworth"])
def test_power_is_exponentially_distributed(shape):
    g = rayleigh_gain(600 * 200, 200, 3.0, np.random.default_rng(2), shape)
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


@pytest.mark.parametrize("shape, expected", [("gaussian", 6.3e-5), ("butterworth", 0.0092)])
def test_power_beyond_twice_the_spread_follows_the_shape(shape, expected):
    # Gaussian, sigma = f_D/2: P(|f| > 2 f_D) = P(|z| > 4) = 6.3e-5. Butterworth, f_c = 0.625 f_D:
    # the f^-4 tail beyond 3.2 f_c holds 0.0092 of the power (VE3NEA's spectrum is not Gaussian).
    fs = 200
    g = rayleigh_gain(1200 * fs, fs, 1.0, np.random.default_rng(5), shape)
    power = np.abs(np.fft.fft(g)) ** 2
    freqs = np.fft.fftfreq(len(g), 1 / fs)
    assert power[np.abs(freqs) > 2.0].sum() / power.sum() == pytest.approx(expected, rel=0.3)


def test_same_generator_state_gives_the_same_fading():
    a = rayleigh_gain(1000, 100, 1.0, np.random.default_rng(4))
    b = rayleigh_gain(1000, 100, 1.0, np.random.default_rng(4))
    assert np.array_equal(a, b)


def test_unknown_shape_raises():
    with pytest.raises(ValueError):
        rayleigh_gain(1000, 100, 1.0, np.random.default_rng(5), "jakes")
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


def test_fading_shape_reaches_the_generator():
    base = dict(text="E", freq_offset_hz=300.0, wpm=5.0, snr_db=20.0, start_s=0.0, tune_s=10.0, fading_hz=1.0)
    a = generate([SignalSpec(**base)], 2000, 11.0, seed=1, add_noise=False)
    b = generate([SignalSpec(**base, fading_shape="butterworth")], 2000, 11.0, seed=1, add_noise=False)
    assert not np.allclose(a, b)
    entry = labels([SignalSpec(**base, fading_shape="butterworth")], 2000, 11.0, seed=1)["signals"][0]
    assert entry["fading_shape"] == "butterworth"
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `.venv\Scripts\python -m pytest training -q`
Expected: `ModuleNotFoundError: No module named 'kz4ap_synth.fading'`, and `TypeError: ... unexpected keyword argument 'fading_hz'`.

- [ ] **Step 3: Implement**

Create `training/kz4ap_synth/fading.py`:

```python
"""Flat Rayleigh fading (QSB) for synthetic signals.

The gain g(t) is complex Gaussian with E|g|^2 = 1. Its Doppler power spectrum
S(f) has one of two shapes, both parameterized by the frequency spread
f_D = spread_hz, Hz:

- "gaussian": S(f) ~ exp(-f^2 / (2 sigma^2)) with f_D = 2 sigma (the
  Watterson / CCIR 520 HF convention).
- "butterworth": S(f) ~ 1 / (1 + (f / f_c)^4), f_c = 0.625 f_D: the spectrum of
  VE3NEA's DeepCW fading generator (a 2nd-order Butterworth low-pass per
  quadrature, cutoff 1.25 f_D / 2). A Gaussian least-squares fit to it gives
  2 sigma = 1.01 f_D, so f_D means the same spread in both shapes to about 1%;
  the Butterworth has heavier f^-4 tails. (DeepCW data_generation.ipynb
  cells 17, 21, 25, commit 2c8fdac, MIT; docs/research/deepcw-generator-notes.md §2.)

Both are synthesized at FADING_RATE_HZ by shaping white complex Gaussian
noise in the frequency domain, then linearly interpolated.
"""

from __future__ import annotations

import numpy as np

FADING_RATE_HZ = 50.0  # rate the fading process is synthesized at before interpolation, samples/s
BUTTERWORTH_CUTOFF_PER_SPREAD = 0.625  # f_c / f_D of VE3NEA's fading filter
SHAPES = ("gaussian", "butterworth")


def slow_gain(duration_s: float, spread_hz: float, rng: np.random.Generator,
              shape: str = "gaussian") -> np.ndarray:
    """The fading gain at FADING_RATE_HZ, covering duration_s (plus two samples)."""
    if spread_hz <= 0:
        raise ValueError("spread_hz must be positive")
    if shape not in SHAPES:
        raise ValueError(f"unknown fading shape {shape!r}")
    m = int(np.ceil(duration_s * FADING_RATE_HZ)) + 2
    freqs = np.fft.fftfreq(m, 1.0 / FADING_RATE_HZ)
    if shape == "gaussian":
        sigma = spread_hz / 2.0
        response = np.exp(-freqs**2 / (4.0 * sigma**2))  # amplitude response: sqrt of the power spectrum
    else:
        cutoff = BUTTERWORTH_CUTOFF_PER_SPREAD * spread_hz
        response = 1.0 / np.sqrt(1.0 + (freqs / cutoff) ** 4)
    response *= np.sqrt(m / np.sum(response**2))  # expected power of the output is exactly 1
    white = (rng.standard_normal(m) + 1j * rng.standard_normal(m)) / np.sqrt(2.0)
    return np.fft.ifft(white * response, norm="ortho")


def gain_at(slow: np.ndarray, t_s: np.ndarray) -> np.ndarray:
    """The gain at times t_s (s from the start of slow), linearly interpolated."""
    t_slow = np.arange(len(slow)) / FADING_RATE_HZ
    return np.interp(t_s, t_slow, slow.real) + 1j * np.interp(t_s, t_slow, slow.imag)


def rayleigh_gain(n: int, sample_rate: int, spread_hz: float, rng: np.random.Generator,
                  shape: str = "gaussian") -> np.ndarray:
    """n samples of complex fading gain at sample_rate, frequency spread spread_hz (Hz)."""
    return gain_at(slow_gain(n / sample_rate, spread_hz, rng, shape), np.arange(n) / sample_rate)
```

In `training/kz4ap_synth/generate.py`, add `from .fading import rayleigh_gain`, add the fields at the end of `SignalSpec`:

```python
    fading_hz: float = 0.0           # Rayleigh fading frequency spread f_D (2 sigma), Hz; 0 = none
    fading_shape: str = "gaussian"   # Doppler spectrum: "gaussian" or "butterworth" (VE3NEA's)
```

and in `generate`, change the loop header and the last line of the loop body to:

```python
    for index, (s, plan) in enumerate(zip(signals, plan_intervals(signals, seed))):
```
```python
        signal = amplitude_for_snr(s.snr_db, sample_rate) * env * np.exp(1j * angle)
        if s.fading_hz > 0:
            signal = signal * rayleigh_gain(i1 - i0, sample_rate, s.fading_hz,
                                            np.random.default_rng([seed, index, 1]), s.fading_shape)
        iq[i0:i1] += signal
```

(The unfaded expression is unchanged, so `test_default_signals_match_milestone_1_generator` still holds.)

- [ ] **Step 4: Run the tests to verify they pass**

Run: `.venv\Scripts\python -m pytest training -q`
Expected: all pass.

- [ ] **Step 5: Document the fading model**

In `docs/signal-processing.md`, section 11, change the **SNR** bullet's first sentence to "key-down carrier power A² (for a fading signal, its mean) over the noise power in a **500 Hz** bandwidth, σ²·500 Hz / fs." and, after the **Keying styles** bullet, insert:

```markdown
- **Fading** (synthetic recordings, `fading_hz`, `fading_shape`): flat
  Rayleigh fading. The carrier is multiplied by a complex Gaussian gain
  g(t) with E|g|² = 1. `fading_hz` is the frequency spread f_D, Hz. The
  Doppler power spectrum is Gaussian with f_D = 2σ (`gaussian`, the
  default; the Watterson / CCIR 520 HF convention) or VE3NEA's DeepCW
  spectrum S(f) ∝ 1/(1 + (f/f_c)⁴), f_c = 0.625·f_D (`butterworth`), whose
  Gaussian least-squares fit has 2σ = 1.01·f_D, so f_D means the same
  spread to about 1%. The Butterworth has heavier tails (0.92% of the
  power beyond 2·f_D, against 6.3×10⁻⁵). The gain is synthesized at
  50 samples/s and linearly interpolated (at most 0.04 dB, Gaussian, or
  0.06 dB, Butterworth, of power lost relative to the mean between points
  at f_D = 3 Hz, derived).
```

- [ ] **Step 6: Commit on the `milestone-2` branch**

```powershell
git add training/kz4ap_synth/fading.py training/kz4ap_synth/generate.py training/tests/test_fading.py training/tests/test_generate.py docs/signal-processing.md
```
```powershell
git commit -m "Add Rayleigh fading with a Doppler-spread setting to the generator"
```

---

### Task 5: Generator — band-scenario settings, interferers, tags

Crowded bands need a minimum station spacing that can go down to zero and a narrower span; strong signals need S₅₀₀ up to 60 dB; the speed range must reach 10–60 WPM. Interferers are labeled signals that are not scored. A `tag` names each signal's condition for the suite summaries (Task 9).

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
  (for example `df 100 Hz, +10 dB re wanted key-down power`), and suite summaries group by it.
```

- [ ] **Step 7: Commit on the `milestone-2` branch**

```powershell
git add training/kz4ap_synth/generate.py training/tests/test_generate.py docs/signal-processing.md
```
```powershell
git commit -m "Add band spacing, span, speed and SNR settings and unscored interferers to the generator"
```

---

### Task 6: Generator — two-station QSOs: alternating overs, the answering station 0–200 Hz away

A ragchew as a listener hears it: two stations near one frequency taking turns. The generator makes the QSO one `SignalSpec`, as a receiver that gives both stations one channel hears it; each over changes the speed, keying style, imbalance, level and (by Δf_B) the carrier. Real answering stations land anywhere from zero-beat to about 200 Hz away: zero-beating by ear leaves a few to tens of Hz, while a sidetone pitch that differs from the rig's CW offset, or RIT, leaves 100–200 Hz (owner, 2026-09-27; no measured distribution yet). Below about 2 FFT bins (47 Hz) the receiver hears the QSO as one track; from 3 bins (70 Hz, the detector's minimum peak separation) as two; in between it depends on where the stations fall within their bins (Task 9's `qso_regime`). So a QSO is labeled two ways: as one signal (the listener's single track), and, by `station_labels`, as one signal per station at its own carrier, so a station heard as its own track is scored against its own frequency. The label lists every over as a transmission with its sender (call and index), speed, style, imbalance, offset and level, so the bench's first-word CER (Task 7) scores the first word after every change of station, and the suite summary (Task 9) scores each over.

**Model (heuristic):** over k is keyed with `timed_intervals` at its sender's `wpm`, `keying` and `imbalance_dits`, from the signal's per-signal generator (`[seed, i, 2]`, as all keying); a silence t_turn, uniform in `turn_s` (default 0.5–2.0 s), separates the last key-up of one over from the first key-down of the next. Each sender has its own carrier f = `freq_offset_hz` + `offset_hz`, its own key-down level S₅₀₀ = `snr_db` + `relative_db` (dB, noise in 500 Hz), a carrier phase drawn once per sender from `[seed, i, 3]` (so each station's carrier is phase-continuous across its overs), and, when `fading_hz` > 0, its own fading path from `[seed, i, 1, k]` (k = sender index) with the signal's `fading_shape`, one continuous process per station across the whole QSO. Drift (`drift_hz_per_s`) and the edge settings (`edge_s`, `edges_centered`, Task 3) apply to both stations. `draw_answer_offset_hz` draws Δf_B from a distribution weighted toward small offsets (**heuristic**, no measured data yet): |Δf_B| in 0–10 Hz with probability 0.40, 10–50 Hz 0.30, 50–100 Hz 0.15, 100–200 Hz 0.15, uniform within each band, either sign. For a QSO signal, `text` is the whole QSO, `wpm` and `keying` are the first sender's (they label the signal in summaries), and `repeats`, `pause_s`, `tune_s`, `wpm_end`, `speed_profile` and `imbalance_dits` of the `SignalSpec` are not used. Single-sender signals are untouched: they take the old path, so `test_default_signals_match_milestone_1_generator` still holds.

**Files:**
- Modify: `training/kz4ap_synth/generate.py` (`Sender`, `SignalSpec`, `SignalPlan`, `plan_signal`, new `plan_overs`, `generate`, new `add_overs`, `reference_text`, `labels`, new `station_labels`, `ANSWER_OFFSET_BANDS_HZ`, `draw_answer_offset_hz`, `qso_spec`)
- Test: `training/tests/test_generate.py`
- Modify: `docs/signal-processing.md` (§11)

**Interfaces:**
- Consumes: `kz4ap_synth.messages.Over` (Task 1); `SignalPlan`, `plan_intervals`, `keying_envelope`, `amplitude_for_snr` (Task 2); `timed_intervals` (Task 3); `slow_gain`, `gain_at` (Task 4); `score`, `tag` (Task 5).
- Produces (Python, `kz4ap_synth.generate`):
  - `Sender(call: str, wpm: float, keying: str = "machine", imbalance_dits: float = 0.0, offset_hz: float = 0.0, relative_db: float = 0.0)` (dataclass).
  - `SignalSpec` gains `senders: list[Sender]` and `overs: list[Over]` (both default empty) and `turn_s: tuple[float, float] = (0.5, 2.0)`.
  - `SignalPlan` gains `senders: list[int]` (default empty): the sender index of each transmission of a QSO.
  - `plan_overs(spec, rng) -> SignalPlan` (raises `ValueError` for an over with nothing to key); `add_overs(iq, spec, plan, sample_rate, seed, index) -> None`.
  - `qso_spec(overs: list[Over], senders: list[Sender], freq_offset_hz: float, snr_db: float, start_s: float, **options) -> SignalSpec`.
  - Labels: a QSO signal's `transmissions` entries add `sender` (call), `sender_index`, `wpm`, `keying`, `imbalance_dits`, `offset_hz`, `relative_db`; its `text` is the overs joined by word spaces. The bench reads only `text`, `start_s` and `end_s` of each transmission.
  - `station_labels(signals, sample_rate, duration_s, seed) -> dict`: `labels()` with each QSO split into one entry per station (its overs only, `freq_offset_hz` + `offset_hz`, `snr_db` + `relative_db`, the station's `wpm` and `keying`, `qso_index`, `sender`; its transmissions carry `offset_hz` 0.0, since they are at the entry's own frequency).
  - `ANSWER_OFFSET_BANDS_HZ = ((0.0, 10.0, 0.40), (10.0, 50.0, 0.30), (50.0, 100.0, 0.15), (100.0, 200.0, 0.15))` ((low Hz, high Hz, probability)); `draw_answer_offset_hz(rng) -> float` (Hz, either sign, rounded to 0.1 Hz).

- [ ] **Step 1: Write the failing tests**

In `training/tests/test_generate.py`, add `Sender`, `qso_spec`, `station_labels` and `draw_answer_offset_hz` to the `kz4ap_synth.generate` import list, add `from kz4ap_synth.messages import Over` below it, and append:

```python
def _two_station_qso(**options):
    senders = [Sender("K1ABC", 20.0), Sender("W9XYZ", 40.0, offset_hz=150.0, relative_db=-6.0)]
    overs = [Over(0, "CQ DE K1ABC K"), Over(1, "K1ABC DE W9XYZ <AR>"), Over(0, "W9XYZ DE K1ABC <KN>")]
    return qso_spec(overs, senders, 500.0, 20.0, 0.5, **options)


def test_qso_overs_alternate_with_each_senders_speed_and_a_turn_gap():
    spec = _two_station_qso(turn_s=(1.0, 2.0))
    plan = plan_intervals([spec], seed=1)[0]
    assert plan.senders == [0, 1, 0]
    assert len(plan.transmissions) == 3
    for (on, off), wpm in zip(plan.transmissions, (20.0, 40.0, 20.0)):
        dits = min(b - a for a, b in plan.intervals if on <= a and b <= off)
        assert dits == pytest.approx(1.2 / wpm)
    for (_, end), (start, _) in zip(plan.transmissions, plan.transmissions[1:]):
        assert 1.0 <= start - end <= 2.0


def test_qso_labels_record_sender_speed_and_style_of_every_over():
    spec = _two_station_qso()
    entry = labels([spec], 8000, 30.0, seed=1)["signals"][0]
    assert entry["text"] == "CQ DE K1ABC K K1ABC DE W9XYZ <AR> W9XYZ DE K1ABC <KN>"
    assert " ".join(t["text"] for t in entry["transmissions"]) == entry["text"]
    assert [t["sender"] for t in entry["transmissions"]] == ["K1ABC", "W9XYZ", "K1ABC"]
    assert [t["wpm"] for t in entry["transmissions"]] == [20.0, 40.0, 20.0]
    assert [t["keying"] for t in entry["transmissions"]] == ["machine"] * 3
    assert [t["sender_index"] for t in entry["transmissions"]] == [0, 1, 0]
    assert entry["transmissions"][1]["offset_hz"] == 150.0
    assert entry["wpm"] == 20.0


def test_each_station_keys_on_its_own_carrier_and_level():
    fs = 8000
    spec = _two_station_qso()
    plan = plan_intervals([spec], seed=1)[0]
    iq = generate([spec], fs, 30.0, seed=1, add_noise=False)

    def first_mark(over):
        on, off = next((a, b) for a, b in plan.intervals if plan.transmissions[over][0] <= a)
        i = int((spec.start_s + (on + off) / 2) * fs)
        return iq[i], np.angle(iq[i + 1] * np.conj(iq[i])) * fs / (2 * np.pi)

    value, freq = first_mark(0)
    assert abs(value) == pytest.approx(amplitude_for_snr(20.0, fs), rel=1e-6)
    assert freq == pytest.approx(500.0, abs=0.1)
    value, freq = first_mark(1)
    assert abs(value) == pytest.approx(amplitude_for_snr(14.0, fs), rel=1e-6)
    assert freq == pytest.approx(650.0, abs=0.1)


def test_qso_is_reproducible_and_each_station_fades_on_its_own():
    spec = _two_station_qso(fading_hz=1.0)
    a = generate([spec], 8000, 30.0, seed=3)
    b = generate([spec], 8000, 30.0, seed=3)
    assert np.array_equal(a, b)
    assert plan_intervals([spec], seed=3)[0].intervals == plan_intervals([spec], seed=3)[0].intervals
    still = generate([_two_station_qso()], 8000, 30.0, seed=3, add_noise=False)
    faded = generate([spec], 8000, 30.0, seed=3, add_noise=False)
    assert not np.allclose(still, faded)


def test_an_over_with_nothing_to_key_is_rejected():
    spec = qso_spec([Over(0, "CQ"), Over(1, "   ")], [Sender("K1ABC", 20.0), Sender("W9XYZ", 20.0)],
                    500.0, 20.0, 0.5)
    with pytest.raises(ValueError):
        plan_intervals([spec], seed=1)


def test_station_labels_score_each_station_at_its_own_frequency():
    spec = _two_station_qso()
    whole = labels([spec], 8000, 30.0, seed=1)["signals"][0]
    a, b = station_labels([spec], 8000, 30.0, seed=1)["signals"]
    assert (a["sender"], a["freq_offset_hz"], a["snr_db"], a["wpm"]) == ("K1ABC", 500.0, 20.0, 20.0)
    assert (b["sender"], b["freq_offset_hz"], b["snr_db"], b["wpm"]) == ("W9XYZ", 650.0, 14.0, 40.0)
    assert a["text"] == "CQ DE K1ABC K W9XYZ DE K1ABC <KN>"
    assert b["text"] == "K1ABC DE W9XYZ <AR>"
    assert a["transmissions"] == [dict(t, offset_hz=0.0) for t in whole["transmissions"] if t["sender_index"] == 0]
    assert a["qso_index"] == b["qso_index"] == 0


def test_answer_offsets_favor_small_values():
    rng = np.random.default_rng(12)
    offsets = np.array([draw_answer_offset_hz(rng) for _ in range(4000)])
    assert np.all(np.abs(offsets) <= 200.0)
    assert np.mean(np.abs(offsets) < 10.0) == pytest.approx(0.40, abs=0.03)
    assert np.mean(np.abs(offsets) >= 100.0) == pytest.approx(0.15, abs=0.03)
    assert np.mean(offsets < 0) == pytest.approx(0.5, abs=0.03)
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `.venv\Scripts\python -m pytest training/tests/test_generate.py -q`
Expected: collection error, `ImportError: cannot import name 'Sender'`.

- [ ] **Step 3: Implement**

In `training/kz4ap_synth/generate.py`:

Change the imports to

```python
from dataclasses import asdict, dataclass, field
```
```python
from .fading import gain_at, rayleigh_gain, slow_gain
from .keying import timed_intervals
from .messages import Over
from .morse import keying_intervals
```

Add, above `SignalSpec`:

```python
@dataclass
class Sender:
    """One station of a multi-sender signal (a QSO heard on one frequency)."""
    call: str
    wpm: float
    keying: str = "machine"        # keying style, a key of keying.STYLES
    imbalance_dits: float = 0.0    # this operator's marks longer and spaces shorter by this, dits
    offset_hz: float = 0.0         # carrier offset from the signal's freq_offset_hz, Hz
    relative_db: float = 0.0       # key-down power relative to the signal's S500, dB
```

Add these fields at the end of `SignalSpec`:

```python
    senders: list[Sender] = field(default_factory=list)  # a QSO's stations (empty: one sender)
    overs: list[Over] = field(default_factory=list)      # a QSO's overs, in order (empty: send text)
    turn_s: tuple[float, float] = (0.5, 2.0)             # silence before each over after the first, s (uniform)
```

and this field at the end of `SignalPlan`:

```python
    senders: list[int] = field(default_factory=list)  # a QSO: the sender of each transmission
```

At the top of `plan_signal`'s body (before `intervals: list[...] = []`) insert

```python
    if spec.overs:
        return plan_overs(spec, rng)
```

and add, above `plan_intervals`:

```python
def plan_overs(spec: SignalSpec, rng: np.random.Generator) -> SignalPlan:
    """A QSO: each over at its sender's speed, keying and imbalance, each after a
    silence drawn uniformly from spec.turn_s (none before the first)."""
    plan = SignalPlan([], [], [])
    t = 0.0
    for over in spec.overs:
        who = spec.senders[over.sender]
        sent = timed_intervals(over.text, who.wpm, who.keying, rng, imbalance_dits=who.imbalance_dits)
        if not sent:
            raise ValueError(f"over {over.text!r} has nothing to key")
        if plan.transmissions:
            t = plan.transmissions[-1][1] + float(rng.uniform(*spec.turn_s))
        plan.intervals.extend((t + on, t + off) for on, off in sent)
        plan.transmissions.append((t + sent[0][0], t + sent[-1][1]))
        plan.senders.append(over.sender)
    return plan
```

In `generate`, right after `phase = rng.uniform(0, 2 * np.pi)` (the phase is still drawn for every signal, so the draws for later single-sender signals keep their order), insert

```python
        if s.overs:
            add_overs(iq, s, plan, sample_rate, seed, index)
            continue
```

and add, above `write_wav`:

```python
def add_overs(iq: np.ndarray, spec: SignalSpec, plan: SignalPlan, sample_rate: int, seed: int,
              index: int) -> None:
    """Adds a QSO to iq: each over at its sender's carrier (freq_offset_hz + offset_hz),
    level (snr_db + relative_db) and phase, faded by that sender's own fading path."""
    if not plan.intervals:
        return
    n = len(iq)
    phases = np.random.default_rng([seed, index, 3]).uniform(0, 2 * np.pi, size=len(spec.senders))
    paths = {}
    if spec.fading_hz > 0:
        for k in sorted(set(plan.senders)):
            paths[k] = slow_gain(plan.intervals[-1][1] + 1.0, spec.fading_hz,
                                 np.random.default_rng([seed, index, 1, k]), spec.fading_shape)
    for (on, off), k in zip(plan.transmissions, plan.senders):
        who = spec.senders[k]
        tail = spec.edge_s / 2 if spec.edges_centered else 0.0
        i0 = max(0, int((spec.start_s + on - tail) * sample_rate))
        i1 = min(n, int(np.ceil((spec.start_s + off + tail) * sample_rate)) + 1)
        if i1 <= i0:
            continue
        part = [iv for iv in plan.intervals if on <= iv[0] and iv[1] <= off]
        env = keying_envelope(part, spec.start_s - i0 / sample_rate, i1 - i0, sample_rate, spec.edge_s,
                              spec.edges_centered)
        t = np.arange(i0, i1) / sample_rate
        angle = 2 * np.pi * (spec.freq_offset_hz + who.offset_hz) * t + phases[k]
        if spec.drift_hz_per_s:
            angle = angle + np.pi * spec.drift_hz_per_s * (t - spec.start_s) ** 2
        signal = amplitude_for_snr(spec.snr_db + who.relative_db, sample_rate) * env * np.exp(1j * angle)
        if k in paths:
            signal = signal * gain_at(paths[k], t - spec.start_s)
        iq[i0:i1] += signal
```

(Rendering over by over keeps the temporary arrays at one over's length, at most about 60 s, even when a whole QSO lasts six minutes.)

Replace `reference_text`'s body with

```python
    """What a perfect decoder would print for the whole signal."""
    if spec.overs:
        return " ".join(o.text for o in spec.overs)
    return " ".join([spec.text] * spec.repeats)
```

Replace the loop in `labels` with

```python
    for s, plan in zip(signals, plan_intervals(signals, seed)):
        if s.overs:
            texts = [o.text for o in s.overs]
            extra = [{"sender": s.senders[k].call, "sender_index": k, "wpm": s.senders[k].wpm,
                      "keying": s.senders[k].keying, "imbalance_dits": s.senders[k].imbalance_dits,
                      "offset_hz": s.senders[k].offset_hz, "relative_db": s.senders[k].relative_db}
                     for k in plan.senders]
        else:
            texts = [s.text] * len(plan.transmissions)
            extra = [{}] * len(plan.transmissions)
        entries.append({
            **asdict(s),
            "text": reference_text(s),
            "end_s": round(signal_end_s(s, plan), 3),
            "transmissions": [
                {"text": text, "start_s": round(s.start_s + on, 3), "end_s": round(s.start_s + off, 3), **more}
                for text, (on, off), more in zip(texts, plan.transmissions, extra)
            ],
        })
```

(Single-sender entries are exactly as before.) Add, above `random_callsign`:

```python
def station_labels(signals, sample_rate: int, duration_s: float, seed: int) -> dict:
    """labels(), but with each QSO split into one entry per station: that station's overs
    only, at its own carrier (freq_offset_hz + offset_hz) and level (snr_db + relative_db),
    so a station the receiver hears as its own track is scored against its own frequency.
    Other signals are as in labels(). Each station entry names its QSO (qso_index)."""
    base = labels(signals, sample_rate, duration_s, seed)
    entries = []
    for i, (s, entry) in enumerate(zip(signals, base["signals"])):
        if not s.overs:
            entries.append(entry)
            continue
        for k, who in enumerate(s.senders):
            # offset_hz 0: these transmissions are at the entry's own frequency
            mine = [{**t, "offset_hz": 0.0} for t in entry["transmissions"] if t["sender_index"] == k]
            if not mine:
                continue
            entries.append({
                "text": " ".join(t["text"] for t in mine),
                "freq_offset_hz": round(s.freq_offset_hz + who.offset_hz, 3),
                "wpm": who.wpm, "snr_db": round(s.snr_db + who.relative_db, 3),
                "start_s": mine[0]["start_s"], "end_s": mine[-1]["end_s"],
                "keying": who.keying, "imbalance_dits": who.imbalance_dits, "drift_hz_per_s": s.drift_hz_per_s,
                "score": s.score, "tag": s.tag, "qso_index": i, "sender": who.call, "transmissions": mine,
            })
    return {**base, "signals": entries}


# Where an answering station's carrier lands relative to the caller's (heuristic: no
# measured distribution yet). Zero-beating by ear leaves a few to tens of Hz; a sidetone
# pitch that differs from the rig's CW offset, or RIT, leaves 100-200 Hz.
ANSWER_OFFSET_BANDS_HZ = ((0.0, 10.0, 0.40), (10.0, 50.0, 0.30), (50.0, 100.0, 0.15), (100.0, 200.0, 0.15))


def draw_answer_offset_hz(rng) -> float:
    """An answering station's carrier offset from the caller's, Hz: a band from
    ANSWER_OFFSET_BANDS_HZ by its probability, uniform within it, either sign."""
    band = int(rng.choice(len(ANSWER_OFFSET_BANDS_HZ), p=[p for _, _, p in ANSWER_OFFSET_BANDS_HZ]))
    low, high, _ = ANSWER_OFFSET_BANDS_HZ[band]
    return round(float(rng.uniform(low, high)) * (1.0 if rng.random() < 0.5 else -1.0), 1)
```

and

```python
def qso_spec(overs: list[Over], senders: list[Sender], freq_offset_hz: float, snr_db: float, start_s: float,
             **options) -> SignalSpec:
    """A QSO as one signal. Its text is the whole QSO; its wpm and keying are the first
    sender's (for summaries); options are further SignalSpec fields (fading_hz, tag, ...)."""
    return SignalSpec(" ".join(o.text for o in overs), freq_offset_hz, senders[0].wpm, snr_db, start_s,
                      keying=senders[0].keying, senders=list(senders), overs=list(overs), **options)
```

- [ ] **Step 4: Run the tests to verify they pass**

Run: `.venv\Scripts\python -m pytest training -q`
Expected: all pass, including `test_default_signals_match_milestone_1_generator` and `test_band_defaults_match_milestone_1` (single-sender signals take the unchanged path).

- [ ] **Step 5: Confirm the smoke check is unaffected**

In Git Bash: `bash bench/smoke.sh build/windows`
Expected: `smoke test passed` with the same CER line as in Task 2, Step 5.

- [ ] **Step 6: Document**

In `docs/signal-processing.md`, section 11, after the **Interferers and tags** bullet, insert:

```markdown
- **Two-station QSOs** (synthetic recordings, `senders`, `overs`): the two
  stations of a QSO are one labeled signal, as a receiver hears them on one
  channel when they are close; `station_labels` also labels each station
  as its own signal at its own carrier, for stations heard as two tracks.
  The answering station's offset is drawn, where a suite draws it, with
  |Δf_B| in 0–10 Hz with probability 0.40, 10–50 Hz 0.30, 50–100 Hz 0.15
  and 100–200 Hz 0.15 (**heuristic**: zero-beat by ear versus sidetone and
  RIT mismatch; no measured distribution yet). Over k is keyed at its sender's speed, keying style and
  imbalance, on its sender's carrier (`freq_offset_hz` + `offset_hz`, Hz)
  at its sender's level (S₅₀₀ + `relative_db`, dB, noise in 500 Hz), with
  its sender's own carrier phase and fading path; a silence drawn uniformly
  from `turn_s` (default 0.5–2.0 s) separates two overs. The labels list
  each over as a transmission with its sender, speed, style, imbalance,
  offset and level, so the first word of every over is scored.
```

- [ ] **Step 7: Commit on the `milestone-2` branch**

```powershell
git add training/kz4ap_synth/generate.py training/tests/test_generate.py docs/signal-processing.md
```
```powershell
git commit -m "Add two-station QSOs with alternating overs on one frequency to the generator"
```

---

### Task 7: Bench — word spaces and first words scored separately

Spec §5.4 asks for word spaces to be scored separately from characters, and the backlog for each transmission's first word, where wrong characters concentrate today. One minimum-edit alignment gives all three; the total edit count stays exactly the Levenshtein distance, so the existing CER (and the smoke baseline) keep their meaning.

**Definitions (this task writes them into signal-processing.md §11):**
- The alignment is a minimum-edit (Levenshtein) alignment of decoded against reference symbols. Ties are broken, walking back from the end, in the order: match or substitution, deletion, insertion (fixed, so results are deterministic).
- Each edit is **charged** to one reference symbol: a substitution or deletion to its own symbol; an insertion to the reference symbol it precedes, or to the last reference symbol if it comes after the end.
- An edit is a **space edit** if the reference symbol or the decoded symbol it involves is a word space; otherwise it is a **character edit**.
- Character CER = character edits / reference symbols that are not word spaces. Space error rate = space edits / reference word spaces. First-word CER = edits charged to the first word of each transmission / the symbols of those words. Each aggregate is summed over scored signals before dividing.
- **Per transmission** (review finding M7): the bench reports each transmission's reference symbols and the edits charged to them (and the same for its first word), so the suite runner scores QSO overs from the bench's own alignment instead of re-aligning in Python. The word space between two transmissions belongs to neither.
- **No-space CER** (review finding I3; VE3NEA's metric, `deepcw-generator-notes.md` §6) = the Levenshtein distance between the reference and decoded symbol sequences with every word space removed / the reference symbols that are not word spaces. It differs from character CER whenever a character and a word space trade places: a character decoded as a word space is a space edit here but one edit without spaces; a word space decoded as E is the reverse.

**Files:**
- Modify: `bench/src/labels.hpp`, `bench/src/labels.cpp`
- Modify: `bench/src/scoring.hpp`, `bench/src/scoring.cpp`
- Modify: `bench/src/main.cpp`
- Test: `bench/tests/labels_test.cpp`, `bench/tests/scoring_test.cpp`
- Modify: `docs/signal-processing.md` (§11)

**Interfaces:**
- Consumes: the labels file of Tasks 2, 5 and 6 (`transmissions`, `score`; a QSO lists each over as a transmission, with extra keys the bench ignores).
- Produces (C++, namespace `kz4ap::bench`):
  - `struct Transmission { std::string text; double start_s; double end_s; };`
  - `LabeledSignal` gains `std::vector<Transmission> transmissions;` (empty: the whole text is one transmission) and `bool score = true;`.
  - `struct EditCounts { std::size_t char_edits = 0; std::size_t space_edits = 0; std::size_t total() const; };`
  - `struct Alignment { EditCounts counts; std::vector<std::size_t> charged; };`
  - `Alignment align(const std::vector<std::string_view>& reference, const std::vector<std::string_view>& decoded);`
  - `std::vector<std::pair<std::size_t, std::size_t>> first_word_ranges(const LabeledSignal& label);` — `[first, last)` reference-symbol ranges; throws `std::runtime_error` if the transmissions do not add up to the text.
  - `std::vector<std::pair<std::size_t, std::size_t>> transmission_ranges(const LabeledSignal& label);` — `[first, last)` reference-symbol range of each whole transmission; same exception.
  - `struct TransmissionScore { std::size_t symbols = 0, edits = 0, first_word_symbols = 0, first_word_edits = 0; };`
  - `SignalScore` gains `symbols, chars, spaces, char_edits, space_edits, first_word_symbols, first_word_edits, nospace_symbols, nospace_edits` (all `std::size_t`, default 0) and `std::vector<TransmissionScore> transmissions`.
  - `Score` gains `double char_cer, space_error_rate, first_word_cer, nospace_cer;` and `std::size_t scored;` (signals with `score = true`); `detected` now counts scored signals only.
  - `kz4ap-bench --json` output: `score` gains `char_cer`, `space_error_rate`, `first_word_cer`, `nospace_cer`, `scored`; each entry of `score.signals` gains `index`, `snr_db`, `wpm`, `scored`, `symbols`, `edits`, `chars`, `char_edits`, `spaces`, `space_edits`, `first_word_symbols`, `first_word_edits`, `nospace_symbols`, `nospace_edits`, and `transmissions` (one object per transmission with `symbols`, `edits`, `first_word_symbols`, `first_word_edits`). Detection recall = detected / scored.

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

TEST(Scoring, TransmissionsAreScoredSeparately) {
    const auto label = make_label("CQ K1ABC CQ K1ABC", 1000.0, {{"CQ K1ABC", 0.5, 3.0}, {"CQ K1ABC", 9.0, 12.0}});
    EXPECT_EQ(transmission_ranges(label),
              (std::vector<std::pair<std::size_t, std::size_t>>{{0, 8}, {9, 17}}));
    const auto s = score({label}, {{1, 1000.0, "CQ K1ABC CQ K1AEC"}});
    ASSERT_EQ(s.signals[0].transmissions.size(), 2u);
    EXPECT_EQ(s.signals[0].transmissions[0].symbols, 8u);
    EXPECT_EQ(s.signals[0].transmissions[0].edits, 0u);
    EXPECT_EQ(s.signals[0].transmissions[1].edits, 1u);
    EXPECT_EQ(s.signals[0].transmissions[1].first_word_symbols, 2u);
    EXPECT_EQ(s.signals[0].transmissions[1].first_word_edits, 0u);
}

TEST(Scoring, NoSpaceCerIsVe3neasMetric) {
    // A word space inserted inside a word: a space edit, but nothing without spaces.
    const auto s = score({make_label("CQ TEST", 1000.0)}, {{1, 1000.0, "CQ T EST"}});
    EXPECT_EQ(s.signals[0].space_edits, 1u);
    EXPECT_EQ(s.signals[0].nospace_symbols, 6u);
    EXPECT_EQ(s.signals[0].nospace_edits, 0u);
    // A character decoded as a word space: no character edit, but one edit without spaces.
    const auto t = score({make_label("CQ TEST", 1000.0)}, {{1, 1000.0, "CQ TE T"}});
    EXPECT_EQ(t.signals[0].char_edits, 0u);
    EXPECT_EQ(t.signals[0].nospace_edits, 1u);
    EXPECT_DOUBLE_EQ(t.nospace_cer, 1.0 / 6.0);
}
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `cmake --build --preset windows`
Expected: compile errors (`Transmission`, `align`, `first_word_ranges`, `transmission_ranges` not declared).

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
struct TransmissionScore {
    std::size_t symbols = 0;             // the transmission's reference symbols
    std::size_t edits = 0;               // edits charged to them
    std::size_t first_word_symbols = 0;  // symbols of its first word
    std::size_t first_word_edits = 0;    // edits charged to those
};

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
    std::size_t nospace_symbols = 0;        // reference symbols without word spaces
    std::size_t nospace_edits = 0;          // Levenshtein distance with word spaces removed (VE3NEA's CER)
    std::vector<TransmissionScore> transmissions;  // one per transmission (one if the label lists none)
};

struct Score {
    std::vector<SignalScore> signals;
    double cer = 0;                // total edits / total reference symbols (scored signals)
    double char_cer = 0;           // character edits / reference characters
    double space_error_rate = 0;   // word-space edits / reference word spaces
    double first_word_cer = 0;     // edits charged to first words / their symbols
    double nospace_cer = 0;        // Levenshtein distance without word spaces / reference symbols without them
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

// [first, last) reference-symbol range of each whole transmission (one range for
// the whole text if the label lists none); the word space between two
// transmissions belongs to neither. Throws like first_word_ranges.
std::vector<std::pair<std::size_t, std::size_t>> transmission_ranges(const LabeledSignal& label);
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

std::vector<std::pair<std::size_t, std::size_t>> transmission_ranges(const LabeledSignal& label) {
    const std::size_t total = kz4ap::morse::symbols(normalize_text(label.text)).size();
    if (label.transmissions.empty()) return {{0, total}};
    std::vector<std::pair<std::size_t, std::size_t>> ranges;
    std::size_t pos = 0;
    for (const auto& t : label.transmissions) {
        const std::size_t n = kz4ap::morse::symbols(normalize_text(t.text)).size();
        ranges.emplace_back(pos, pos + n);
        pos += n + 1;  // the word space between transmissions belongs to neither
    }
    if (pos - 1 != total) throw std::runtime_error("labels: transmissions do not add up to the signal's text");
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
        const auto charged_in = [&](std::pair<std::size_t, std::size_t> r) {
            std::size_t sum = 0;
            for (std::size_t i = r.first; i < r.second && i < a.charged.size(); ++i) sum += a.charged[i];
            return sum;
        };
        const auto first_words = first_word_ranges(label);
        const auto whole = transmission_ranges(label);
        for (std::size_t k = 0; k < whole.size(); ++k) {
            const TransmissionScore ts{whole[k].second - whole[k].first, charged_in(whole[k]),
                                       first_words[k].second - first_words[k].first, charged_in(first_words[k])};
            s.first_word_symbols += ts.first_word_symbols;
            s.first_word_edits += ts.first_word_edits;
            s.transmissions.push_back(ts);
        }
        std::vector<std::string_view> reference_nospace, decoded_nospace;
        for (const auto v : reference_symbols) if (v != " ") reference_nospace.push_back(v);
        for (const auto v : decoded_symbols) if (v != " ") decoded_nospace.push_back(v);
        s.nospace_symbols = reference_nospace.size();
        s.nospace_edits = edit_distance(reference_nospace, decoded_nospace);
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
            totals.nospace_edits += s.nospace_edits;
            totals.nospace_symbols += s.nospace_symbols;
        }
        result.signals.push_back(std::move(s));
    }
    result.cer = ratio(totals.edits, totals.symbols);
    result.char_cer = ratio(totals.char_edits, totals.chars);
    result.space_error_rate = ratio(totals.space_edits, totals.spaces);
    result.first_word_cer = ratio(totals.first_word_edits, totals.first_word_symbols);
    result.nospace_cer = ratio(totals.nospace_edits, totals.nospace_symbols);
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
                nlohmann::json per_transmission = nlohmann::json::array();
                for (const auto& ts : sig.transmissions) {
                    per_transmission.push_back({{"symbols", ts.symbols}, {"edits", ts.edits},
                                                {"first_word_symbols", ts.first_word_symbols},
                                                {"first_word_edits", ts.first_word_edits}});
                }
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
                                   {"first_word_edits", sig.first_word_edits},
                                   {"nospace_symbols", sig.nospace_symbols},
                                   {"nospace_edits", sig.nospace_edits},
                                   {"transmissions", per_transmission}});
                std::printf("label %+10.1f Hz  CER %5.3f  %s%s\n", sig.label.freq_offset_hz, sig.cer,
                            sig.track_id ? "" : "(not detected)", sig.label.score ? "" : " (not scored)");
            }
            out["score"] = {{"cer", s.cer},
                            {"char_cer", s.char_cer},
                            {"space_error_rate", s.space_error_rate},
                            {"first_word_cer", s.first_word_cer},
                            {"nospace_cer", s.nospace_cer},
                            {"detected", s.detected},
                            {"labels", labels.signals.size()},
                            {"scored", s.scored},
                            {"detection_recall", recall},
                            {"false_tracks", s.false_tracks},
                            {"signals", signals}};
            std::printf("CER %.4f (characters %.4f, word spaces %.4f, first words %.4f, without spaces %.4f), "
                        "detected %zu of %zu, %zu false tracks\n",
                        s.cer, s.char_cer, s.space_error_rate, s.first_word_cer, s.nospace_cer, s.detected, s.scored,
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
  Character and space edits add up to the CER's edit count. The benchmark
  also reports these counts for each transmission (for a QSO, each over).
- **No-space CER** (VE3NEA's metric): the Levenshtein distance between
  reference and decoded symbols with every word space removed, over the
  reference symbols that are not word spaces. It differs from character
  CER when a character and a word space trade places.
```

- [ ] **Step 9: Commit on the `milestone-2` branch**

```powershell
git add bench/src/labels.hpp bench/src/labels.cpp bench/src/scoring.hpp bench/src/scoring.cpp bench/src/main.cpp bench/tests/labels_test.cpp bench/tests/scoring_test.cpp docs/signal-processing.md
```
```powershell
git commit -m "Score word spaces and each transmission's first word separately in the bench"
```

---

### Task 8: Engine and bench — oracle channels and CPU time per channel-second

Two measurement tools. **Oracle mode** opens a channel at each labeled frequency, rounded to the FFT bin, from the first sample, and bypasses the detector, so the decoder can be measured below the detector's threshold (about S₅₀₀ = 0 dB) and apart from detection errors; the rounding leaves the frequency tracker (Task 10) the whole ±11.7 Hz bin-rounding offset to find. **CPU time per channel-second** (spec §5.4) is process CPU time divided by the total duration of channel output the decoders received; a second figure counts only the time spent inside decoders.

**Files:**
- Modify: `engine/include/kz4ap/engine.hpp`, `engine/src/engine.cpp`
- Create: `bench/src/cpu_time.hpp`, `bench/src/cpu_time.cpp`
- Modify: `bench/src/scoring.hpp`, `bench/src/scoring.cpp` (match by order), `bench/src/main.cpp`, `bench/CMakeLists.txt`
- Test: `engine/tests/engine_test.cpp`, `bench/tests/scoring_test.cpp`, `bench/tests/cpu_time_test.cpp` (new)
- Modify: `docs/signal-processing.md` (§6, §11)

**Interfaces:**
- Consumes: `score()`, `LabeledSignal` (Task 7).
- Produces:
  - `EngineConfig::oracle_frequencies_hz` (`std::vector<double>`, Hz; empty = normal operation). In oracle mode the engine publishes a `Born` event for each frequency from its constructor, with track ids 1, 2, … in order and `freq_hz` = the bin center; no track ever dies; the detector is not run.
  - `struct EngineStats { std::uint64_t channel_samples = 0; double channel_seconds = 0; double decoder_seconds = 0; };` and `EngineStats Engine::stats() const`.
  - `struct Engine::Channel` gains `int bin` (the channel's center bin; Task 13 uses it).
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

### Task 9: Benchmark suites and runner

Named suites turn the generator's options into a fixed, seeded set of recordings; a runner scores each with `kz4ap-bench` for each front end; a summary reports, per scenario and condition: CER with a bootstrap 95% interval, character CER, space error rate, first-word CER, no-space CER (VE3NEA's metric), detections, the fewest symbols at any one S₅₀₀ point, the S₅₀₀ at which CER crosses 0.10 and 0.05 with bootstrap 95% intervals, the median frequency error (once Task 14 adds the tracked frequency), and CPU time per channel-second; the Matched-minus-baseline CER difference paired signal by signal; for QSOs, the CER of each over by the sending station's keying style; and for group H, how many tracks each QSO became.

**Suites.** `smoke` is exactly the recording `bench/smoke.sh` makes (band scenario, 8 signals, 30 s, seed 1, 192 kHz); CI keeps using `smoke.sh`, and no ragchew clip is added to it (a clip would change the CI recording and its baseline). `full` is for local runs and is sized for `--seeds 3` (Task 14 runs it so); `--seeds N` repeats it with N seeds. Single-station scenarios run at 48 kHz (the engine then uses N = 2048, the same 23.4 Hz bins and r = 1500 samples/s) with many stations per recording, 1.0–5 kHz apart, so one recording covers a whole S₅₀₀ sweep:

| Group | Recordings (per seed) | Bench mode | What varies |
|---|---|---|---|
| A sensitivity | 12, 25, 40 WPM, 2 recordings each; 120 s | oracle | S₅₀₀ −10 … +20 dB in 2 dB steps, 4 stations per point (2 per recording), machine keying, filler text with VE3NEA's character statistics (review finding M11) |
| B fading (VE3NEA-anchored) | paddle and hand at 24 WPM × f_D 0.1/0.3/1/3 Hz; paddle 12 and 40 WPM at f_D 0.1 Hz; one "VE3NEA mix" recording; 180 s each (240 s at 12 WPM; 11 recordings) | oracle | S₅₀₀ at all ten of VE3NEA's points, −8.22 … 57.78 dB, 3 stations each; his Butterworth spectrum, 2 ms centered edges, random-text statistics, δ drawn per station; the mix recording draws style (hand 0.25, paddle 0.50, computer 0.25), speed (12–48 WPM) and f_D (his grid) per station |
| C fists | machine, computer, paddle, bug, hand at 25 WPM; 120 s | oracle | S₅₀₀ 5, 10, 20 dB × imbalance 0, +0.1, −0.1 dit, 3 stations per combination |
| D speed | one recording, 60 s | oracle | steps 20→35 and 35→20 WPM, ramps 15→30 and 30→15 WPM, 10 WPM, 60 WPM; S₅₀₀ 15 dB |
| E interference | one recording, 60 s | oracle | wanted 25 WPM at S₅₀₀ 10 dB; unscored interferer at 30 WPM, Δf = 20/50/100/150 Hz, −10/0/+10/+20 dB relative to the wanted station's key-down power |
| F tuning | offsets 0/2.9/5.9/8.8/11.7 Hz from the bin center × 20, 25 WPM × S₅₀₀ 0, 5 dB (60 s); drift 0.2/0.5/1/2 Hz/s at 25 WPM, S₅₀₀ 5 dB (30 s) | oracle | carrier offset and drift |
| G ragchew | one recording of 12 whole ragchew QSOs, both stations at 25 WPM, paddle, on one carrier; length fitted to the QSOs (about 6 min) | oracle | S₅₀₀ 0, 4, 8, 12, 16, 20 dB, 2 QSOs each: plain-language text, prosigns, abbreviations, and the pauses between overs |
| H two-station QSO | (1) 12 whole QSOs with Δf_B = 0, 10, 25, 50, 100, 200 Hz, 2 each, through the detector; (2) the same recording with oracle channels (group "H two-station QSO, oracle"); (3) 12 QSOs with Δf_B drawn from `ANSWER_OFFSET_BANDS_HZ`, through the detector; lengths fitted (about 6 min each) | detector; oracle for (2) | each operator 20–32 WPM, style from VE3NEA's mix, own δ; the answering station −6 … +6 dB relative to the caller's S₅₀₀ = 15 dB; each recording scored twice, with one label per QSO and with one label per station; tags name the regime (below) |
| strong | S₅₀₀ 30, 40, 50, 60 dB; 30 s | detector | ghost tracks (false tracks), CER |
| pauses | a CQ sent 3 times with 2, 5, 10, 20 s pauses; S₅₀₀ 15 dB; 80 s | detector | track lifetime, first words |
| tune-up | 0.3, 0.6, 1.0, 2.0 s carriers before keying; S₅₀₀ 15 dB; 30 s | detector | the speed-estimate bug |
| first sample | 4 stations keying from 0 s; 20 s | detector | stations present at the start |
| crowded | 25 stations within ±5 kHz at minimum spacing 200, 100, 50, 0 Hz; 10–60 WPM; 40 s | detector | crowding, very different speeds side by side |
| band | 20 stations over 192 kHz, 10–60 WPM, S₅₀₀ 10–60 dB; 30 s | detector | the whole pipeline |

**Sizes (review finding I1).** The binomial relative standard deviation of a CER estimate p from n characters is √((1 − p)/(n·p)) (derived): 0.37 at p = 0.05 and n = 140, 0.14 at n = 1000. Near CER 0.05–0.1 in white noise, CER changes about 3× per 2 dB, so n = 1000 puts about ±0.25 dB (1σ) on a crossing. So, at `--seeds 3`: every S₅₀₀ point of groups A, B and C holds at least 1000 characters, spaces excluded (counted from the generated text of seeds 1–3: the fewest are 1075, in group C; the 12 WPM fading recording is 240 s long to get there); and every group-B point at f_D = 0.1 Hz spans at least 100 fade times (the gain decorrelates to 1/e in about 4.5 s at 0.1 Hz, derived; 1620 station-seconds per point = 360 fade times). Errors cluster within a signal, so the summary's intervals come from a bootstrap over signals (1000 resamples, seeded by the row's key, so they are reproducible), not from the binomial formula; a crossing's interval resamples signals within each S₅₀₀ point. The comparison of the two front ends is paired: both score the same recordings, and the summary reports the mean per-signal CER difference with its bootstrap interval. **Cost at `--seeds 3`:** 117 recordings, 4.6 h of audio, 3.2 GB of WAV files (48 kHz and 192 kHz, 16-bit stereo); generating them takes about 57 min (measured for one seed on the owner's desktop: 1139 s, 19 min, and 1.06 GB; three seeds scaled from that: about 57 min and 3.2 GB); the bench run's time is recorded in Task 14.

**Group B against VE3NEA's published numbers.** His CER is the Levenshtein distance with spaces removed (`deepcw-generator-notes.md` §6), so compare his curves with the **no-space CER** column (Task 7), which is his metric exactly. What still differs from his benchmark: the oracle channel sits on the nearest FFT bin (the tracker must find up to ±11.7 Hz) where his pitch error is ±30 Hz inside a spectrogram strip; our noise is complex I/Q, his real audio (the same S₅₀₀ for white noise); character and word spaces are one draw each (Task 3); and we score at least 1000 characters per point over 3 seeds against his 30 000. The keying edges now match his (2 ms, centered; Task 3); outside group B they are milestone 1's 5 ms inside-the-mark edges, which shorten every mark by 5 ms at 50% amplitude.

**Ragchews and QSOs (groups G, H).** A whole QSO at 20–32 WPM takes about 4–8 minutes, so these recordings are sized from the plan of their signals (`_fitted_duration`: the latest end plus 2 s, rounded up to whole seconds) instead of a fixed length. Each is about 70 MB and takes about 40 s to generate. Group G isolates the text: both stations send at one speed and style on one carrier, so its difference from group A at 25 WPM is the effect of real text and over gaps.

**Group H's three regimes (owner, 2026-09-27; review finding I5).** `qso_regime(Δf_B)` classifies a QSO by how the detector must see it, derived from its 23.4 Hz bins and 3-bin minimum peak separation (70.3 Hz): **same-track** for |Δf_B| ≤ 2 bins (46.9 Hz; both peaks always within 2 bins, one track; the turnover happens inside one channel, which exercises Task 12's re-acquisition, and the Matched channel follows the answering station within its tracker's follow distance, D + 10 Hz = 57 Hz, Task 10), **ambiguous** below 3 bins (46.9–70.3 Hz; 2 or 3 bins apart depending on where the stations fall within their bins, so one track or two), and **separate-track** from 3 bins (each station its own track). Every H recording is scored twice, and `summary.md` marks with † each row whose view does not fit its regime (re-review finding m-2). Against its **QSO labels** (one signal per QSO, frequency = the caller's), which is the right view for same-track QSOs; the bench's 50 Hz match tolerance then finds the track the caller's CQ was born on. Against its **station labels** (`station_labels`: one signal per station at its own carrier), which is the right view for separate-track QSOs: each station is matched within 50 Hz of its own frequency. Ambiguous QSOs are reported both ways, and the "Tracks per QSO" table shows directly whether each QSO stayed one track or split. The frequency error is measured against the carrier of the station that sent the last over for a QSO label (that is where the latest decoded text came from), and against the station's own carrier for a station label. The oracle copy of the grid recording opens channels at the labeled frequencies, so it measures the front end's turnover apart from detection; with QSO labels its separate-track rows are not meaningful (the answering station lies 100–200 Hz off the channel's center, at or beyond its ±150 Hz edge), which the summary's per-station rows cover. (These boundaries are the milestone-1 detector's, which the Envelope path keeps. On the Matched path, with the channel distance D = 47 Hz of Task 13, the boundaries are D = 47 Hz, where attribution changes, and D + 10 Hz = 57 Hz, where following stops; "ambiguous" there is 47–57 Hz, where a duplicate track is born and merged, and the 50 Hz grid point lies in both. Restating `qso_regime` in D waits for the owner's decision on the channel distance, Design decisions B.)

**Per-over scoring.** Each over of a QSO is a transmission in the labels (Task 6), so the bench's first-word CER already scores the first word after every change of station, and the bench reports each transmission's symbols and charged edits (Task 7). The summary pools them by (front end, group, keying style of the sender). (This replaces a Python re-alignment in an earlier draft of this plan; review finding M7.)

**Crossing S₅₀₀ (definition, written to signal-processing.md §11):** for a condition with at least three S₅₀₀ points, CER is computed per point (edits over symbols, pooled across stations); scanning down from the highest S₅₀₀, the first point whose CER exceeds the threshold and the point above it bracket the crossing, which is interpolated linearly in dB. If the top point already fails, there is no crossing; if no point fails, the lowest point is reported (an upper bound).

**Files:**
- Create: `training/kz4ap_synth/suites.py`
- Test: `training/tests/test_suites.py` (new)
- Modify: `README.md` (Benchmark section), `docs/signal-processing.md` (§11)

**Interfaces:**
- Consumes: `ragchew`, `random_operator`, `random_text` (Task 1); `SignalSpec`, `generate`, `labels`, `plan_intervals`, `signal_end_s`, `write_wav` (Task 2); `draw_style`, `draw_imbalance_dits`, `VE3NEA_WPM_RANGE`, `edge_s`, `edges_centered` (Task 3); `fading_shape` (Task 4); `scenario_band`, `with_interferer`, `fill_text`, `random_callsign`, `MESSAGES` (Task 5); `Sender`, `qso_spec`, `station_labels`, `draw_answer_offset_hz` (Task 6); `kz4ap-bench --oracle` and its JSON fields, including each signal's `nospace_symbols`, `nospace_edits`, `transmissions` and the top-level `tracks` (Tasks 7–8; `tracks` exists since milestone 1); `tracked_freq_hz` when present (Task 14); `--front-end` (Task 13; the runner passes it only for front ends other than `baseline`); `kz4ap_synth.morse.keying_intervals`.
- Produces (Python, `kz4ap_synth.suites`): `Recording(name, group, sample_rate, duration_s, noise_seed, oracle, specs, station_labels=False)`; `SUITES: dict[str, Callable[[int], list[Recording]]]` with `"smoke"` and `"full"`; `VE3NEA_RHO_DB`, `RHO_TO_S500_DB`, `VE3NEA_SNR_DB`, `VE3NEA_SPREADS_HZ`, `VE3NEA_EDGE_S`, `DETECTOR_SEPARATION_BINS`, `QSO_OFFSETS_HZ`, `BOOTSTRAP_RESAMPLES`; `qso_regime(offset_hz) -> str`; `check_recording(rec) -> None` (raises `ValueError`); `write_suite(recordings, out_dir, suite_name) -> None` (writes `<name>.stations.json` too where `station_labels`); `run_suite(out_dir, bench, front_ends) -> None`; `crossing_snr(points, threshold) -> float | None`; `bootstrap_cer(signals, rng)`, `bootstrap_crossing(by_snr, threshold, rng)`; `aggregate(rows) -> dict[tuple[str, str, str], dict]` (adds `cer_interval`, `nospace_cer`, `min_symbols_per_point`, `snr_at_cer_interval`, `freq_error_hz_median`); `paired_differences(rows) -> dict[tuple[str, str], dict]`; `load_results(out_dir)`; `over_rows(out_dir) -> list[dict]`; `aggregate_overs(rows)`; `track_splits(out_dir) -> dict[tuple[str, str], dict]`; `view_fits(group, tag) -> bool`; `write_summary(out_dir) -> None` (writes `summary.json`, with `groups`, `paired`, `overs`, `track_splits` and `cpu`, and `summary.md`). CLI: `python -m kz4ap_synth.suites generate|run|summarize`.

- [ ] **Step 1: Write the failing tests**

Create `training/tests/test_suites.py`:

```python
import json
from collections import defaultdict

import numpy as np
import pytest

from kz4ap_synth.generate import Sender, SignalSpec, qso_spec, scenario_band
from kz4ap_synth.messages import Over
from kz4ap_synth.morse import symbols
from kz4ap_synth.suites import (
    SUITES,
    VE3NEA_SNR_DB,
    Recording,
    aggregate,
    check_recording,
    crossing_snr,
    over_rows,
    paired_differences,
    qso_regime,
    view_fits,
    track_splits,
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
    assert len(names) == len(set(names)) == 39
    for r in recs:
        check_recording(r)
    assert {r.group for r in recs} == {
        "A sensitivity", "B fading", "C fists", "D speed", "E interference", "F tuning",
        "G ragchew", "H two-station QSO", "H two-station QSO, oracle", "strong", "pauses", "tune-up",
        "first sample", "crowded", "band"}


def test_full_suite_covers_the_scenarios():
    recs = SUITES["full"](1)
    specs = [s for r in recs for s in r.specs]
    assert max(s.snr_db for s in specs if s.score) == 60.0
    assert min(s.snr_db for s in specs) == -10.0
    assert set(VE3NEA_SNR_DB) <= {s.snr_db for s in specs if s.fading_hz > 0}
    assert {s.fading_hz for s in specs} >= {0.1, 0.3, 1.0, 3.0}
    assert {s.keying for s in specs} == {"machine", "computer", "paddle", "bug", "hand"}
    assert {s.fading_shape for s in specs if s.fading_hz > 0} == {"butterworth"}
    assert all(s.edges_centered and s.edge_s == 0.002 for s in specs if s.fading_hz > 0)
    assert not any(s.edges_centered for s in specs if s.fading_hz == 0)
    assert any(not s.score for s in specs)
    assert {s.pause_s for s in specs if s.repeats > 1} == {2.0, 5.0, 10.0, 20.0}
    assert {s.tune_s for s in specs if s.tune_s > 0} == {0.3, 0.6, 1.0, 2.0}
    assert {s.drift_hz_per_s for s in specs if s.drift_hz_per_s} == {0.2, 0.5, 1.0, 2.0}
    assert any(s.start_s == 0.0 for s in specs)
    assert min(s.wpm for s in specs) <= 10.0 and max(s.wpm for s in specs) >= 60.0
    assert any(s.wpm_end is not None for s in specs)
    qsos = [s for s in specs if s.overs]
    assert {s.senders[1].offset_hz for s in qsos} >= {0.0, 10.0, 25.0, 50.0, 100.0, 200.0}
    assert {qso_regime(s.senders[1].offset_hz) for s in qsos} == {"same-track", "ambiguous", "separate-track"}
    assert any(s.senders[0].wpm != s.senders[1].wpm for s in qsos)
    assert all(s.overs[-1].text.endswith("<SK>") for s in qsos)
    assert all(r.station_labels for r in recs if r.group.startswith("H "))


def test_points_hold_enough_characters_for_three_seeds():
    # Target: at least 1000 characters (spaces excluded) per S500 point with --seeds 3, so
    # seed 1 alone must hold a third of that in groups A, B and C.
    chars = defaultdict(int)
    for r in SUITES["full"](1):
        if r.group in ("A sensitivity", "B fading", "C fists"):
            for s in r.specs:
                chars[(r.group, s.tag, s.snr_db)] += len([c for c in symbols(s.text) if c != " "])
    assert min(chars.values()) >= 1000 / 3


def test_slow_fading_points_span_a_hundred_fade_times_over_three_seeds():
    # f_D = 0.1 Hz decorrelates (1/e) in about 4.5 s: 100 fades need 450 station-seconds per point.
    seconds = defaultdict(float)
    for r in SUITES["full"](1):
        for s in r.specs:
            if s.fading_hz == 0.1:
                seconds[(s.tag, s.snr_db)] += r.duration_s
    assert 3 * min(seconds.values()) >= 450.0


def test_more_seeds_make_more_recordings():
    assert len(SUITES["full"](2)) == 2 * len(SUITES["full"](1))


def test_qso_regimes_follow_the_detector_bins():
    assert [qso_regime(df) for df in (0.0, 10.0, 25.0, 46.0)] == ["same-track"] * 4
    assert [qso_regime(df) for df in (50.0, -60.0)] == ["ambiguous"] * 2
    assert [qso_regime(df) for df in (71.0, 100.0, -200.0)] == ["separate-track"] * 3


def test_views_that_do_not_fit_the_regime_are_marked():
    assert not view_fits("H two-station QSO (per station)", "same-track, offset 0 Hz")
    assert view_fits("H two-station QSO (per station)", "separate-track, offset 100 Hz")
    assert not view_fits("H two-station QSO", "separate-track, drawn offset")
    assert not view_fits("H two-station QSO, oracle", "separate-track, offset 200 Hz")
    assert view_fits("H two-station QSO", "ambiguous, offset 50 Hz")
    assert view_fits("A sensitivity", "separate-track")


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


def _row(snr, symbols_, edits, scored=True, front_end="baseline", index=0, freq_error=None):
    return {"front_end": front_end, "recording": "r", "group": "A", "tag": "25 wpm", "index": index,
            "snr_db": snr, "scored": scored, "detected": True, "symbols": symbols_, "edits": edits,
            "chars": symbols_ - 2, "char_edits": edits, "spaces": 2, "space_edits": 0, "first_word_symbols": 2,
            "first_word_edits": 0, "nospace_symbols": symbols_ - 2, "nospace_edits": edits,
            "freq_error_hz": freq_error}


def test_aggregate_pools_symbols_and_skips_unscored_signals():
    agg = aggregate([_row(0.0, 10, 5), _row(2.0, 30, 3, index=1), _row(2.0, 100, 100, scored=False, index=2)])
    v = agg[("baseline", "A", "25 wpm")]
    assert v["signals"] == 2
    assert v["cer"] == pytest.approx(8 / 40)
    assert v["nospace_cer"] == pytest.approx(8 / 36)
    assert v["space_error_rate"] == 0.0
    assert v["cer_by_snr"] == [(0.0, 0.5), (2.0, 0.1)]
    assert v["min_symbols_per_point"] == 10


def test_bootstrap_interval_brackets_the_estimate_and_is_reproducible():
    rows = [_row(float(snr), 100, e, index=i) for i, (snr, e) in
            enumerate((s, e) for s in (0, 2, 4, 6) for e in (40 - 6 * s, 30 - 5 * s, 35 - 5 * s))]
    first = aggregate(rows)[("baseline", "A", "25 wpm")]
    again = aggregate(rows)[("baseline", "A", "25 wpm")]
    low, high = first["cer_interval"]
    assert low < first["cer"] < high
    assert first["cer_interval"] == again["cer_interval"]
    low, high = first["snr_at_cer_interval"]["0.1"]
    assert low <= first["snr_at_cer"]["0.1"] <= high


def test_aggregate_reports_the_median_frequency_error():
    rows = [_row(0.0, 10, 0, freq_error=1.0), _row(2.0, 10, 0, index=1, freq_error=3.0),
            _row(4.0, 10, 0, index=2)]
    assert aggregate(rows)[("baseline", "A", "25 wpm")]["freq_error_hz_median"] == pytest.approx(2.0)
    assert aggregate([_row(0.0, 10, 0)])[("baseline", "A", "25 wpm")]["freq_error_hz_median"] is None


def test_paired_differences_compare_the_same_signals():
    rows = [_row(0.0, 100, 10, index=i) for i in range(4)]
    rows += [_row(0.0, 100, 5, front_end="matched", index=i) for i in range(4)]
    d = paired_differences(rows)[("A", "25 wpm")]
    assert d["signals"] == 4
    assert d["mean"] == pytest.approx(-0.05)
    assert d["interval"] == pytest.approx((-0.05, -0.05))


def _fake_signal(index, scored=True, **counts):
    base = {"index": index, "snr_db": 10.0, "wpm": 25.0, "scored": scored, "track_id": 1, "symbols": 2,
            "edits": 1, "chars": 2, "char_edits": 1, "spaces": 0, "space_edits": 0, "first_word_symbols": 2,
            "first_word_edits": 1, "nospace_symbols": 2, "nospace_edits": 1, "transmissions": []}
    return {**base, **counts}


def test_write_suite_and_summary_round_trip(tmp_path):
    rec = Recording("tiny", "A sensitivity", 8000, 3.0, 5, True,
                    [SignalSpec("CQ", 1000.0, 25.0, 10.0, 0.5, tag="25 wpm")])
    write_suite([rec], tmp_path, "test")
    manifest = json.loads((tmp_path / "manifest.json").read_text())
    assert manifest["recordings"][0]["oracle"] is True
    assert manifest["recordings"][0]["station_labels"] is None
    assert (tmp_path / "tiny.wav").exists()
    results = tmp_path / "results" / "baseline"
    results.mkdir(parents=True)
    (results / "tiny.json").write_text(json.dumps({
        "channel_seconds": 3.0, "timing": {"cpu_s": 0.03, "decoder_ms_per_channel_s": 1.0},
        "score": {"signals": [_fake_signal(0)]}}))
    write_summary(tmp_path)
    text = (tmp_path / "summary.md").read_text(encoding="utf-8")
    assert "## A sensitivity" in text
    assert "| 25 wpm | baseline | 1 | 1 | 0.500 (0.500–0.500) | 0.500 | 0.000 | 0.500 | 0.500 | 2 |" in text
    assert "| baseline | 3.0 | 10.000 | 1.000 |" in text


def _two_station(offset_hz):
    senders = [Sender("K1ABC", 25.0, "paddle"), Sender("W9XYZ", 30.0, "hand", offset_hz=offset_hz)]
    return qso_spec([Over(0, "CQ K1ABC"), Over(1, "K1ABC <KN>")], senders, 1000.0, 10.0, 0.5,
                    tag=f"{qso_regime(offset_hz)}, offset {offset_hz:g} Hz", turn_s=(0.5, 0.5))


def test_qsos_are_scored_per_over_per_station_and_for_track_splits(tmp_path):
    rec = Recording("qso", "H two-station QSO", 8000, 12.0, 5, False, [_two_station(100.0)], True)
    write_suite([rec], tmp_path, "test")
    entry = json.loads((tmp_path / "manifest.json").read_text())["recordings"][0]
    assert entry["station_labels"] == "qso.stations.json"
    stations = json.loads((tmp_path / "qso.stations.json").read_text())["signals"]
    assert [s["freq_offset_hz"] for s in stations] == [1000.0, 1100.0]
    results = tmp_path / "results" / "baseline"
    results.mkdir(parents=True)
    over_counts = [{"symbols": 8, "edits": 0, "first_word_symbols": 2, "first_word_edits": 0},
                   {"symbols": 7, "edits": 1, "first_word_symbols": 5, "first_word_edits": 1}]
    (results / "qso.json").write_text(json.dumps({
        "tracks": [{"id": 1, "freq_hz": 1001.0, "text": "CQ K1ABC"}, {"id": 2, "freq_hz": 1099.0, "text": "K1AEC"}],
        "score": {"signals": [_fake_signal(0, symbols=19, edits=1, transmissions=over_counts,
                                           tracked_freq_hz=1098.0)]}}))
    (results / "qso.stations.json").write_text(json.dumps({"score": {"signals": [
        _fake_signal(0, symbols=8, edits=0, tracked_freq_hz=1000.5),
        _fake_signal(1, symbols=10, edits=1, tracked_freq_hz=1099.0)]}}))
    rows = over_rows(tmp_path)
    assert [(r["sender"], r["keying"], r["symbols"], r["edits"]) for r in rows] == [
        ("K1ABC", "paddle", 8, 0), ("W9XYZ", "hand", 7, 1)]
    assert track_splits(tmp_path)[("baseline", "separate-track, offset 100 Hz")] == {"qsos": 1, "mean_tracks": 2.0}
    write_summary(tmp_path)
    summary = json.loads((tmp_path / "summary.json").read_text())
    by_group = {g["group"]: g for g in summary["groups"]}
    # The QSO label's error is measured against the last over's sender (W9XYZ at 1100 Hz);
    # each station label's against its own carrier.
    assert by_group["H two-station QSO"]["freq_error_hz_median"] == pytest.approx(2.0)
    assert by_group["H two-station QSO (per station)"]["freq_error_hz_median"] == pytest.approx(0.75)
    text = (tmp_path / "summary.md").read_text(encoding="utf-8")
    assert "| H two-station QSO | hand | baseline | 1 | 0.143 |" in text
    assert "| separate-track, offset 100 Hz | baseline | 1 | 2.00 |" in text
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `.venv\Scripts\python -m pytest training/tests/test_suites.py -q`
Expected: `ModuleNotFoundError: No module named 'kz4ap_synth.suites'`.

- [ ] **Step 3: Implement**

Create `training/kz4ap_synth/suites.py`:

```python
"""Named benchmark suites: which synthetic recordings to make, a runner that
scores each with kz4ap-bench, and a summary.

    python -m kz4ap_synth.suites generate --suite full --out build/suite/full [--seeds 3]
    python -m kz4ap_synth.suites run --out build/suite/full --bench PATH/kz4ap-bench \
        --front-end baseline --front-end matched
    python -m kz4ap_synth.suites summarize --out build/suite/full

"smoke" is the recording bench/smoke.sh makes (the CI check); "full" is for
local runs, sized for --seeds 3. S500 everywhere: key-down carrier power over
noise power in 500 Hz, dB.

Group B is anchored to VE3NEA's DeepCW benchmark: his Butterworth fading
spectrum and f_D grid, his SNR points converted to S500 (his key-on SNR in
3 kHz + 7.78 dB), his keying styles with a per-operator imbalance, his 2 ms
centered keying edges, his style mix and speed range (one recording), and
filler text with his statistics. Groups G and H send whole ragchew QSOs
(G: one speed and style for both stations on one carrier; H: two operators
0-200 Hz apart). Every rate in the summary carries a bootstrap 95% interval
over signals, so a difference smaller than the intervals is not a result.
"""

from __future__ import annotations

import argparse
import json
import math
import subprocess
import zlib
from dataclasses import dataclass
from pathlib import Path

import numpy as np

from .generate import (MESSAGES, Sender, SignalSpec, draw_answer_offset_hz, fill_text, generate, labels,
                       plan_intervals, qso_spec, random_callsign, scenario_band, signal_end_s, station_labels,
                       with_interferer, write_wav)
from .keying import VE3NEA_WPM_RANGE, draw_imbalance_dits, draw_style
from .messages import ragchew as ragchew_overs
from .messages import random_operator, random_text
from .morse import keying_intervals

BIN_HZ = 48000 / 2048  # the engine's FFT bin width at 48 kHz (and at 192 kHz), Hz
DETECTOR_SEPARATION_BINS = 3  # the detector's min_separation_bins: closer peaks become one track
SWEEP_SNR_DB = [float(x) for x in range(-10, 22, 2)]  # S500 sweep: -10 ... +20 dB
VE3NEA_RHO_DB = (-16.0, -12.0, -6.0, -3.0, 0.0, 6.0, 10.0, 20.0, 30.0, 50.0)  # his key-on SNR, noise in 3 kHz, dB
RHO_TO_S500_DB = 10 * math.log10(3000.0 / 500.0)  # 7.78 dB: the same white noise measured in 500 Hz, not 3 kHz
VE3NEA_SNR_DB = [round(r + RHO_TO_S500_DB, 2) for r in VE3NEA_RHO_DB]  # S500: -8.22 ... 57.78 dB
VE3NEA_SPREADS_HZ = (0.1, 0.3, 1.0, 3.0)  # his f_D grid, Hz
VE3NEA_EDGE_S = 0.002  # his raised-cosine keying edges, s, centered on each element's ends
CER_THRESHOLDS = (0.05, 0.10)
FRONT_ENDS = ("baseline", "matched")
COUNT_KEYS = ("symbols", "edits", "chars", "char_edits", "spaces", "space_edits",
              "first_word_symbols", "first_word_edits", "nospace_symbols", "nospace_edits")
BOOTSTRAP_RESAMPLES = 1000
PLANNED_SEEDS = 3  # the full suite's per-point sizes below assume --seeds 3

FADING_ROWS = ([(keying, 24.0, f_d) for keying in ("paddle", "hand") for f_d in VE3NEA_SPREADS_HZ]
               + [("paddle", 12.0, 0.1), ("paddle", 40.0, 0.1)])  # (keying, WPM, f_D Hz)
FIST_STYLES = ("machine", "computer", "paddle", "bug", "hand")
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
RAGCHEW_SNR_DB = (0.0, 4.0, 8.0, 12.0, 16.0, 20.0)
QSO_OFFSETS_HZ = (0.0, 10.0, 25.0, 50.0, 100.0, 200.0)  # the answering station's carrier offset from the caller's, Hz
QSO_WPM_RANGE = (20.0, 32.0)


@dataclass
class Recording:
    name: str
    group: str         # scenario group, used in summaries
    sample_rate: int
    duration_s: float
    noise_seed: int    # the seed passed to generate() and labels()
    oracle: bool       # score with kz4ap-bench --oracle
    specs: list[SignalSpec]
    station_labels: bool = False  # also score against one label per QSO station (generate.station_labels)


def qso_regime(offset_hz: float) -> str:
    """How the detector sees a QSO whose stations are offset_hz apart (derived from its
    3-bin minimum peak separation and 23.4 Hz bins): within 2 bins the two peaks are always
    closer than 3 bins (one track); from 3 bins on they never are (two tracks); between,
    it depends on where the stations fall within their bins."""
    df = abs(offset_hz)
    if df <= (DETECTOR_SEPARATION_BINS - 1) * BIN_HZ:
        return "same-track"
    if df < DETECTOR_SEPARATION_BINS * BIN_HZ:
        return "ambiguous"
    return "separate-track"


def _message(rng) -> str:
    return MESSAGES[rng.integers(len(MESSAGES))].format(c=random_callsign(rng))


def _text(rng, wpm: float, available_s: float, keying: str = "machine") -> str:
    """A repeated contest message that fits available_s; random keying runs longer, so it gets 30% slack."""
    return fill_text(_message(rng), wpm, available_s * (1.0 if keying == "machine" else 0.7))


def _filler(rng, wpm: float, available_s: float, keying: str = "hand") -> str:
    """VE3NEA-statistics random text (messages.random_text) that fits available_s at exact
    timing; random keying runs longer, so it gets 30% slack."""
    budget = available_s * (1.0 if keying == "machine" else 0.7)
    # VE3NEA's words average 3.06 characters, about 0.6 of a PARIS word: 2 per PARIS word is ample
    words = random_text(rng, int(2 * budget / 60.0 * wpm) + 20).split()
    lo, hi = 1, len(words)
    while lo < hi:  # the longest prefix that fits
        mid = (lo + hi + 1) // 2
        if keying_intervals(" ".join(words[:mid]), wpm)[-1][1] <= budget:
            lo = mid
        else:
            hi = mid - 1
    return " ".join(words[:lo])


def _fitted_duration(specs: list[SignalSpec], noise_seed: int) -> float:
    """The recording length that holds every signal, plus 2 s, rounded up to whole seconds."""
    ends = [signal_end_s(s, p) for s, p in zip(specs, plan_intervals(specs, noise_seed))]
    return float(math.ceil(max(ends) + 2.0))


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
    """Two 120 s recordings per speed, each with 2 stations per S500 point: 4 stations per
    point per seed. Filler text with VE3NEA's statistics, machine keying."""
    recs = []
    points = [snr for snr in SWEEP_SNR_DB for _ in range(2)]
    for code, wpm in enumerate((12.0, 25.0, 40.0)):
        for part in (0, 1):
            rng = np.random.default_rng([seed, 1, code, part])
            specs = []
            for f, snr in zip(_slots(len(points), 1200.0, rng, BIN_HZ / 2), points):
                start = _start(rng)
                specs.append(SignalSpec(_filler(rng, wpm, 118.0 - start, "machine"), f, wpm, snr, start,
                                        tag=f"{wpm:g} wpm"))
            recs.append(Recording(f"A-awgn-{wpm:g}wpm-{part}-s{seed}", "A sensitivity", 48000, 120.0,
                                  1000 * seed + 10 + 2 * code + part, True, specs))
    return recs


def fading(seed: int) -> list[Recording]:
    """VE3NEA-anchored: his Butterworth fading spectrum and f_D grid, his SNR points (as S500),
    his keying styles with a per-operator imbalance, his 2 ms centered edges, and his
    random-text statistics. 3 stations per point for 180 s (240 s below 20 WPM, so every
    point holds at least 1000 characters over 3 seeds)."""
    recs = []
    points = [snr for snr in VE3NEA_SNR_DB for _ in range(3)]
    for code, (keying, wpm, f_d) in enumerate(FADING_ROWS):
        rng = np.random.default_rng([seed, 2, code])
        duration = 240.0 if wpm < 20.0 else 180.0
        specs = []
        for f, snr in zip(_slots(len(points), 1200.0, rng, BIN_HZ / 2), points):
            start = _start(rng)
            specs.append(SignalSpec(_filler(rng, wpm, duration - 2.0 - start, keying), f, wpm, snr, start,
                                    keying=keying,
                                    imbalance_dits=round(draw_imbalance_dits(rng), 3), fading_hz=f_d,
                                    fading_shape="butterworth", edge_s=VE3NEA_EDGE_S, edges_centered=True,
                                    tag=f"{keying} {wpm:g} wpm fD {f_d:g} Hz"))
        recs.append(Recording(f"B-fading-{keying}-{wpm:g}wpm-{f_d:g}Hz-s{seed}", "B fading", 48000, duration,
                              1000 * seed + 20 + code, True, specs))
    rng = np.random.default_rng([seed, 2, len(FADING_ROWS)])
    specs = []
    for f, snr in zip(_slots(len(points), 1200.0, rng, BIN_HZ / 2), points):
        start = _start(rng)
        wpm = round(float(rng.uniform(*VE3NEA_WPM_RANGE)), 1)
        f_d = float(rng.choice(VE3NEA_SPREADS_HZ))
        keying = draw_style(rng)
        specs.append(SignalSpec(_filler(rng, wpm, 178.0 - start, keying), f, wpm, snr, start, keying=keying,
                                imbalance_dits=round(draw_imbalance_dits(rng), 3), fading_hz=f_d,
                                fading_shape="butterworth", edge_s=VE3NEA_EDGE_S, edges_centered=True,
                                tag="VE3NEA mix"))
    recs.append(Recording(f"B-fading-mix-s{seed}", "B fading", 48000, 180.0, 1000 * seed + 20 + len(FADING_ROWS),
                          True, specs))
    return recs


def fists(seed: int) -> list[Recording]:
    """3 stations per (S500, imbalance) combination for 120 s."""
    recs = []
    for code, keying in enumerate(FIST_STYLES):
        rng = np.random.default_rng([seed, 3, code])
        combos = [(snr, imb) for snr in (5.0, 10.0, 20.0) for imb in (0.0, 0.1, -0.1) for _ in range(3)]
        specs = []
        for f, (snr, imb) in zip(_slots(len(combos), 1200.0, rng, BIN_HZ / 2), combos):
            start = _start(rng)
            specs.append(SignalSpec(_text(rng, 25.0, 118.0 - start, keying), f, 25.0, snr, start, keying=keying,
                                    imbalance_dits=imb, tag=f"{keying} imbalance {imb:+.1f}"))
        recs.append(Recording(f"C-fists-{keying}-s{seed}", "C fists", 48000, 120.0, 1000 * seed + 30 + code,
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
        wanted = SignalSpec(_text(rng, 25.0, 58.0 - start), f, 25.0, 10.0, start,
                            tag=f"df {df:g} Hz, {rel:+g} dB re wanted key-down power")
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


def ragchew(seed: int) -> list[Recording]:
    """Whole ragchew QSOs, both sides on one frequency at one speed and style, so only the
    text (prosigns, abbreviations, overs and the pauses between them) differs from group A."""
    rng = np.random.default_rng([seed, 13])
    points = [snr for snr in RAGCHEW_SNR_DB for _ in range(2)]
    specs = []
    for f, snr in zip(_slots(len(points), 1500.0, rng, BIN_HZ / 2), points):
        a, b = random_operator(rng), random_operator(rng)
        senders = [Sender(a.call, 25.0, "paddle"), Sender(b.call, 25.0, "paddle")]
        specs.append(qso_spec(ragchew_overs(rng, a, b), senders, f, snr, _start(rng), tag="ragchew 25 wpm"))
    noise_seed = 1000 * seed + 130
    return [Recording(f"G-ragchew-s{seed}", "G ragchew", 48000, _fitted_duration(specs, noise_seed), noise_seed,
                      True, specs)]


def _qso(rng, f: float, offset_hz: float, tag: str) -> SignalSpec:
    """A whole ragchew by two operators with their own speeds (QSO_WPM_RANGE), styles
    (VE3NEA's mix) and imbalances; the answering one offset_hz away, -6 ... +6 dB re the
    caller's key-down power; the caller at S500 = 15 dB."""
    a, b = random_operator(rng), random_operator(rng)
    senders = [Sender(op.call, round(float(rng.uniform(*QSO_WPM_RANGE)), 1), draw_style(rng),
                      round(draw_imbalance_dits(rng), 3)) for op in (a, b)]
    senders[1].offset_hz = offset_hz
    senders[1].relative_db = round(float(rng.uniform(-6.0, 6.0)), 1)
    return qso_spec(ragchew_overs(rng, a, b), senders, f, 15.0, _start(rng), tag=tag)


def two_station_qso(seed: int) -> list[Recording]:
    """Whole ragchew QSOs as a listener hears them, in three regimes of the answering
    station's offset (qso_regime): on the grid QSO_OFFSETS_HZ, 2 QSOs each, through the
    detector and again with oracle channels (the front end's turnover apart from
    detection); and 12 QSOs with offsets drawn from generate.ANSWER_OFFSET_BANDS_HZ.
    Each is also scored with one label per station."""
    rng = np.random.default_rng([seed, 14])
    offsets = [df for df in QSO_OFFSETS_HZ for _ in range(2)]
    specs = [_qso(rng, f, df, f"{qso_regime(df)}, offset {df:g} Hz")
             for f, df in zip(_slots(len(offsets), 2400.0, rng, BIN_HZ / 2), offsets)]
    noise_seed = 1000 * seed + 140
    duration = _fitted_duration(specs, noise_seed)
    grid = Recording(f"H-qso-s{seed}", "H two-station QSO", 48000, duration, noise_seed, False, specs, True)
    oracle = Recording(f"H-qso-oracle-s{seed}", "H two-station QSO, oracle", 48000, duration, noise_seed, True,
                       specs, True)
    rng = np.random.default_rng([seed, 15])
    specs = []
    for f in _slots(12, 2400.0, rng, BIN_HZ / 2):
        df = draw_answer_offset_hz(rng)
        specs.append(_qso(rng, f, df, f"{qso_regime(df)}, drawn offset"))
    noise_seed = 1000 * seed + 141
    drawn = Recording(f"H-qso-drawn-s{seed}", "H two-station QSO", 48000, _fitted_duration(specs, noise_seed),
                      noise_seed, False, specs, True)
    return [grid, oracle, drawn]


def smoke_suite(seeds: int = 1) -> list[Recording]:
    """The recording bench/smoke.sh makes: band scenario, 8 signals, 30 s, seed 1 (noise seed 2)."""
    specs = scenario_band(np.random.default_rng(1), 8, 30.0, 192000)
    return [Recording("smoke-band", "smoke", 192000, 30.0, 2, False, specs)]


def full_suite(seeds: int = 1) -> list[Recording]:
    recs = []
    for seed in range(1, seeds + 1):
        for build in (sensitivity, fading, fists, speed, interference, tuning, ragchew, two_station_qso, strong,
                      pauses, tune_up, first_sample, crowded, band):
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
        if abs(spec.freq_offset_hz) + max((abs(s.offset_hz) for s in spec.senders), default=0.0) \
                > 0.45 * rec.sample_rate:
            raise ValueError(f"{rec.name}: signal at {spec.freq_offset_hz} Hz is outside the span")


def write_suite(recordings: list[Recording], out_dir: Path, suite_name: str) -> None:
    """Writes every recording (WAV plus labels) and manifest.json into out_dir."""
    out_dir.mkdir(parents=True, exist_ok=True)
    entries = []
    for rec in recordings:
        check_recording(rec)
        wav = out_dir / f"{rec.name}.wav"
        write_wav(wav, generate(rec.specs, rec.sample_rate, rec.duration_s, seed=rec.noise_seed), rec.sample_rate)
        extra = {"recording": rec.name, "group": rec.group, "oracle": rec.oracle}
        lab = labels(rec.specs, rec.sample_rate, rec.duration_s, seed=rec.noise_seed)
        wav.with_suffix(".json").write_text(json.dumps({**lab, **extra}, indent=2) + "\n")
        entry = {"name": rec.name, "group": rec.group, "oracle": rec.oracle, "wav": wav.name,
                 "labels": wav.with_suffix(".json").name, "station_labels": None}
        if rec.station_labels:
            per_station = station_labels(rec.specs, rec.sample_rate, rec.duration_s, seed=rec.noise_seed)
            path = out_dir / f"{rec.name}.stations.json"
            path.write_text(json.dumps({**per_station, **extra}, indent=2) + "\n")
            entry["station_labels"] = path.name
        entries.append(entry)
        print(f"wrote {wav.name}")
    (out_dir / "manifest.json").write_text(json.dumps({"suite": suite_name, "recordings": entries}, indent=2) + "\n")


def _scorings(rec: dict) -> list[tuple[str, str, str]]:
    """(labels file, result name, group) for each way a recording is scored."""
    out = [(rec["labels"], rec["name"], rec["group"])]
    if rec.get("station_labels"):
        out.append((rec["station_labels"], f"{rec['name']}.stations", f"{rec['group']} (per station)"))
    return out


def run_suite(out_dir: Path, bench: Path, front_ends) -> None:
    """Scores every recording of the manifest with kz4ap-bench, once per front end (and
    once more per front end against per-station labels where the manifest has them)."""
    manifest = json.loads((out_dir / "manifest.json").read_text())
    for fe in front_ends:
        if fe not in FRONT_ENDS:
            raise ValueError(f"unknown front end {fe!r}")
        results = out_dir / "results" / fe
        results.mkdir(parents=True, exist_ok=True)
        for rec in manifest["recordings"]:
            for label_file, result, _group in _scorings(rec):
                cmd = [str(bench), str(out_dir / rec["wav"]), "--labels", str(out_dir / label_file),
                       "--json", str(results / f"{result}.json")]
                if rec["oracle"]:
                    cmd.append("--oracle")
                if fe != "baseline":
                    cmd += ["--front-end", fe]
                done = subprocess.run(cmd, capture_output=True, text=True)
                if done.returncode != 0:
                    raise RuntimeError(f"kz4ap-bench failed on {result} ({fe}):\n{done.stderr}")
                print(f"{fe:9s} {result}: {done.stdout.strip().splitlines()[-1]}")


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


def _rng_for(key) -> np.random.Generator:
    """A generator seeded by the group's key, so every interval is reproducible."""
    return np.random.default_rng(zlib.crc32(repr(key).encode()))


def _interval(values) -> tuple[float, float] | None:
    """The central 95% of bootstrap values; None unless at least 95% of resamples gave one."""
    kept = [v for v in values if v is not None]
    if len(kept) < 0.95 * len(values):
        return None
    return float(np.percentile(kept, 2.5)), float(np.percentile(kept, 97.5))


def bootstrap_cer(signals, rng) -> tuple[float, float] | None:
    """95% interval of pooled CER (summed edits over summed symbols) by resampling signals
    with replacement: errors cluster within a signal, so signals, not symbols, are the units."""
    if not signals:
        return None
    edits = np.array([e for e, _ in signals], dtype=float)
    syms = np.array([s for _, s in signals], dtype=float)
    picks = rng.integers(len(signals), size=(BOOTSTRAP_RESAMPLES, len(signals)))
    return _interval([_ratio(edits[p].sum(), syms[p].sum()) for p in picks])


def bootstrap_crossing(by_snr, threshold: float, rng) -> tuple[float, float] | None:
    """95% interval of crossing_snr, resampling signals within each S500 point."""
    snrs = sorted(by_snr)
    values = []
    for _ in range(BOOTSTRAP_RESAMPLES):
        points = []
        for snr in snrs:
            sig = by_snr[snr]
            p = rng.integers(len(sig), size=len(sig))
            points.append((snr, _ratio(sum(sig[i][0] for i in p), sum(sig[i][1] for i in p))))
        values.append(crossing_snr(points, threshold))
    return _interval(values)


def aggregate(rows) -> dict:
    """Pools scored signals by (front end, group, tag); rates are summed edits over summed
    symbols, each with a bootstrap 95% interval over signals."""
    groups: dict = {}
    for r in rows:
        if not r["scored"]:
            continue
        key = (r["front_end"], r["group"], r["tag"])
        g = groups.setdefault(key, {"signals": 0, "detected": 0, "by_snr": {}, "freq_errors": [],
                                    **{k: 0 for k in COUNT_KEYS}})
        g["signals"] += 1
        g["detected"] += int(r["detected"])
        for k in COUNT_KEYS:
            g[k] += r[k]
        g["by_snr"].setdefault(r["snr_db"], []).append((r["edits"], r["symbols"]))
        if r.get("freq_error_hz") is not None:
            g["freq_errors"].append(r["freq_error_hz"])
    out = {}
    for key, g in groups.items():
        rng = _rng_for(key)
        points = [(snr, _ratio(sum(e for e, _ in sig), sum(s for _, s in sig)))
                  for snr, sig in sorted(g["by_snr"].items())]
        crossing = len(points) >= 3
        out[key] = {
            "signals": g["signals"],
            "detected": g["detected"],
            "cer": _ratio(g["edits"], g["symbols"]),
            "cer_interval": bootstrap_cer([s for sig in g["by_snr"].values() for s in sig], rng),
            "char_cer": _ratio(g["char_edits"], g["chars"]),
            "space_error_rate": _ratio(g["space_edits"], g["spaces"]),
            "first_word_cer": _ratio(g["first_word_edits"], g["first_word_symbols"]),
            "nospace_cer": _ratio(g["nospace_edits"], g["nospace_symbols"]),
            "cer_by_snr": points,
            "min_symbols_per_point": min(sum(s for _, s in sig) for sig in g["by_snr"].values()),
            "snr_at_cer": {f"{t:g}": crossing_snr(points, t) for t in CER_THRESHOLDS} if crossing else {},
            "snr_at_cer_interval": ({f"{t:g}": bootstrap_crossing(g["by_snr"], t, rng) for t in CER_THRESHOLDS}
                                    if crossing else {}),
            "freq_error_hz_median": float(np.median(g["freq_errors"])) if g["freq_errors"] else None,
        }
    return out


def paired_differences(rows) -> dict:
    """Matched minus baseline CER, signal by signal on the same recordings, pooled by
    (group, tag): mean difference and its bootstrap 95% interval over signals."""
    by_front_end: dict = {}
    for r in rows:
        if r["scored"]:
            by_front_end.setdefault(r["front_end"], {})[(r["group"], r["tag"], r["recording"], r["index"])] = r
    base, matched = by_front_end.get("baseline", {}), by_front_end.get("matched", {})
    diffs: dict = {}
    for key in sorted(set(base) & set(matched)):
        b, m = base[key], matched[key]
        diffs.setdefault(key[:2], []).append(_ratio(m["edits"], m["symbols"]) - _ratio(b["edits"], b["symbols"]))
    out = {}
    for key, d in diffs.items():
        values = np.array(d)
        picks = _rng_for(key).integers(len(values), size=(BOOTSTRAP_RESAMPLES, len(values)))
        out[key] = {"signals": len(values), "mean": float(values.mean()),
                    "interval": _interval([float(values[p].mean()) for p in picks])}
    return out


def _label_truth_hz(label: dict) -> float:
    """The frequency the latest text came from: a QSO's last over's sender's carrier."""
    transmissions = label.get("transmissions") or []
    return label["freq_offset_hz"] + (transmissions[-1].get("offset_hz", 0.0) if transmissions else 0.0)


def load_results(out_dir: Path):
    """Per-signal rows and per-recording timings from every results/<front end>/ directory."""
    manifest = json.loads((out_dir / "manifest.json").read_text())
    rows, timings = [], []
    for fe_dir in sorted(p for p in (out_dir / "results").iterdir() if p.is_dir()):
        for rec in manifest["recordings"]:
            for label_file, result_name, group in _scorings(rec):
                path = fe_dir / f"{result_name}.json"
                if not path.exists():
                    continue
                result = json.loads(path.read_text())
                label_signals = json.loads((out_dir / label_file).read_text())["signals"]
                for sig in result["score"]["signals"]:
                    label = label_signals[sig["index"]]
                    tracked = sig.get("tracked_freq_hz")
                    freq_error = (abs(tracked - _label_truth_hz(label))
                                  if tracked is not None and not label.get("drift_hz_per_s") else None)
                    rows.append({"front_end": fe_dir.name, "recording": result_name, "group": group,
                                 "tag": label.get("tag", ""), "index": sig["index"], "snr_db": sig["snr_db"],
                                 "scored": sig["scored"], "detected": sig["track_id"] is not None,
                                 "freq_error_hz": freq_error, **{k: sig[k] for k in COUNT_KEYS}})
                if result_name == rec["name"]:
                    timing = result.get("timing", {})
                    channel_s = result.get("channel_seconds", 0.0)
                    timings.append({"front_end": fe_dir.name, "channel_seconds": channel_s,
                                    "cpu_s": timing.get("cpu_s", 0.0),
                                    "decoder_s": timing.get("decoder_ms_per_channel_s", 0.0) * channel_s / 1000.0})
    return rows, timings


def over_rows(out_dir: Path) -> list[dict]:
    """One row per over of every scored QSO (labels whose transmissions name a sender):
    the over's reference symbols and the edits the bench charged to them (its
    per-transmission counts, so the bench's own alignment decides)."""
    manifest = json.loads((out_dir / "manifest.json").read_text())
    rows = []
    for fe_dir in sorted(p for p in (out_dir / "results").iterdir() if p.is_dir()):
        for rec in manifest["recordings"]:
            path = fe_dir / f"{rec['name']}.json"
            if not path.exists():
                continue
            label_signals = json.loads((out_dir / rec["labels"]).read_text())["signals"]
            for sig in json.loads(path.read_text())["score"]["signals"]:
                overs = label_signals[sig["index"]].get("transmissions", [])
                if not sig["scored"] or not overs or "sender" not in overs[0]:
                    continue
                for k, (over, counts) in enumerate(zip(overs, sig["transmissions"])):
                    rows.append({"front_end": fe_dir.name, "group": rec["group"],
                                 "tag": label_signals[sig["index"]].get("tag", ""), "over": k,
                                 "sender": over["sender"], "keying": over["keying"], "wpm": over["wpm"],
                                 "symbols": counts["symbols"], "edits": counts["edits"]})
    return rows


def aggregate_overs(rows) -> dict:
    """Per-over CER pooled by (front end, group, keying style of the over's sender)."""
    groups: dict = {}
    for r in rows:
        g = groups.setdefault((r["front_end"], r["group"], r["keying"]), {"overs": 0, "symbols": 0, "edits": 0})
        g["overs"] += 1
        g["symbols"] += r["symbols"]
        g["edits"] += r["edits"]
    return {k: {**g, "cer": _ratio(g["edits"], g["symbols"])} for k, g in groups.items()}


def track_splits(out_dir: Path) -> dict:
    """Group H through the detector: for each QSO label, the tracks that decoded text within
    25 Hz of either station's carrier (birth frequency), averaged by (front end, tag).
    1 means the QSO stayed one track; 2 means it split into one per station."""
    manifest = json.loads((out_dir / "manifest.json").read_text())
    counts: dict = {}
    for fe_dir in sorted(p for p in (out_dir / "results").iterdir() if p.is_dir()):
        for rec in manifest["recordings"]:
            path = fe_dir / f"{rec['name']}.json"
            if rec["oracle"] or not rec.get("station_labels") or not path.exists():
                continue
            tracks = [t for t in json.loads(path.read_text()).get("tracks", []) if t["text"].strip()]
            for label in json.loads((out_dir / rec["labels"]).read_text())["signals"]:
                carriers = [label["freq_offset_hz"] + s["offset_hz"] for s in label.get("senders", [])]
                near = sum(any(abs(t["freq_hz"] - c) <= 25.0 for c in carriers) for t in tracks)
                counts.setdefault((fe_dir.name, label.get("tag", "")), []).append(near)
    return {k: {"qsos": len(v), "mean_tracks": float(np.mean(v))} for k, v in counts.items()}


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


def view_fits(group: str, tag: str) -> bool:
    """Whether a group-H scoring view fits the QSO's regime: labels per QSO for a QSO heard as one
    track, labels per station for one heard as two; ambiguous QSOs fit both."""
    if not group.startswith("H two-station QSO"):
        return True
    if group.endswith("(per station)"):
        return not tag.startswith("same-track")
    return not tag.startswith("separate-track")


def _db(x) -> str:
    return "—" if x is None else f"{x:.1f}"


def _with_interval(value, interval, fmt: str) -> str:
    if value is None:
        return "—"
    if interval is None:
        return f"{value:{fmt}}"
    return f"{value:{fmt}} ({interval[0]:{fmt}}–{interval[1]:{fmt}})"


def format_markdown(agg: dict, cpu: dict, overs: dict | None = None, paired: dict | None = None,
                    splits: dict | None = None) -> str:
    lines = ["# Benchmark summary", "",
             "S₅₀₀: key-down carrier power over noise power in 500 Hz, dB. CER counts word spaces; "
             "character CER and space error rate split its edits; first-word CER scores the first word "
             "of each transmission (each over, for a QSO); no-space CER is VE3NEA's metric (Levenshtein "
             "distance with spaces removed). Parentheses: bootstrap 95% interval over signals. "
             "— : not reached, or fewer than three S₅₀₀ points. † : this group-H view does not fit the "
             "QSO's regime (labels per station for a same-track QSO, where both match one track and each "
             "is charged the other's text; labels per QSO for a separate-track QSO, where the caller's "
             "track lacks the answering station's overs); read the other view.", ""]
    for group in sorted({k[1] for k in agg}):
        lines += [f"## {group}", "",
                  "| tag | front end | signals | detected | CER | character CER | space error rate | "
                  "first-word CER | no-space CER | fewest symbols at one S₅₀₀ point | "
                  "S₅₀₀ at CER 0.10 (dB) | S₅₀₀ at CER 0.05 (dB) | median frequency error (Hz) |",
                  "|---|---|---|---|---|---|---|---|---|---|---|---|---|"]
        for (fe, g, tag), v in sorted(agg.items(), key=lambda kv: (kv[0][2], kv[0][0])):
            if g != group:
                continue
            at, at_i = v["snr_at_cer"], v["snr_at_cer_interval"]
            mark = "" if view_fits(g, tag) else " †"
            lines.append(f"| {tag}{mark} | {fe} | {v['signals']} | {v['detected']} | "
                         f"{_with_interval(v['cer'], v['cer_interval'], '.3f')} | "
                         f"{v['char_cer']:.3f} | {v['space_error_rate']:.3f} | {v['first_word_cer']:.3f} | "
                         f"{v['nospace_cer']:.3f} | {v['min_symbols_per_point']} | "
                         f"{_with_interval(at.get('0.1'), at_i.get('0.1'), '.1f')} | "
                         f"{_with_interval(at.get('0.05'), at_i.get('0.05'), '.1f')} | "
                         f"{_db(v['freq_error_hz_median'])} |")
        lines.append("")
    if paired:
        lines += ["## Matched − baseline, paired by signal", "",
                  "Mean over signals of (Matched CER − baseline CER) on the same recordings; negative favors "
                  "Matched. An interval that contains 0 is no evidence either way.", "",
                  "| group | tag | signals | mean CER difference |", "|---|---|---|---|"]
        for (group, tag), v in sorted(paired.items()):
            lines.append(f"| {group} | {tag} | {v['signals']} | {_with_interval(v['mean'], v['interval'], '+.3f')} |")
        lines.append("")
    if overs:
        lines += ["## Per over", "",
                  "CER of each over (from its first to its last symbol) pooled by the sending station's "
                  "keying style.", "",
                  "| group | keying | front end | overs | CER |", "|---|---|---|---|---|"]
        for (fe, group, keying), v in sorted(overs.items(), key=lambda kv: (kv[0][1], kv[0][2], kv[0][0])):
            lines.append(f"| {group} | {keying} | {fe} | {v['overs']} | {v['cer']:.3f} |")
        lines.append("")
    if splits:
        lines += ["## Tracks per QSO (group H, detector)", "",
                  "Tracks that decoded text within 25 Hz of either station's carrier; 1 = one track for the "
                  "QSO, 2 = one per station.", "",
                  "| tag | front end | QSOs | mean tracks |", "|---|---|---|---|"]
        for (fe, tag), v in sorted(splits.items(), key=lambda kv: (kv[0][1], kv[0][0])):
            lines.append(f"| {tag} | {fe} | {v['qsos']} | {v['mean_tracks']:.2f} |")
        lines.append("")
    lines += ["## CPU", "",
              "| front end | channel-seconds (s) | CPU per channel-second (ms/s) | decoders per channel-second (ms/s) |",
              "|---|---|---|---|"]
    for fe, c in sorted(cpu.items()):
        lines.append(f"| {fe} | {c['channel_seconds']:.1f} | {c['cpu_ms_per_channel_s']:.3f} | "
                     f"{c['decoder_ms_per_channel_s']:.3f} |")
    return "\n".join(lines) + "\n"


def _intervals_json(v: dict) -> dict:
    return {**v, "cer_by_snr": [list(p) for p in v["cer_by_snr"]]}


def write_summary(out_dir: Path) -> None:
    rows, timings = load_results(out_dir)
    agg = aggregate(rows)
    cpu = cpu_summary(timings)
    overs = aggregate_overs(over_rows(out_dir))
    paired = paired_differences(rows)
    splits = track_splits(out_dir)
    summary = {
        "groups": [{"front_end": fe, "group": g, "tag": tag, **_intervals_json(v)}
                   for (fe, g, tag), v in sorted(agg.items())],
        "paired": [{"group": g, "tag": tag, **v} for (g, tag), v in sorted(paired.items())],
        "overs": [{"front_end": fe, "group": g, "keying": k, **v} for (fe, g, k), v in sorted(overs.items())],
        "track_splits": [{"front_end": fe, "tag": tag, **v} for (fe, tag), v in sorted(splits.items())],
        "cpu": cpu,
    }
    (out_dir / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
    (out_dir / "summary.md").write_text(format_markdown(agg, cpu, overs, paired, splits), encoding="utf-8")
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
Expected: all pass (`test_suites.py` takes about 40 s: it builds the full suite's text several times). If `test_full_suite_recordings_are_valid_and_uniquely_named` reports a signal that runs past the end (random keying ran long), shorten that builder's `available_s` by 2 s and re-run; do not lengthen the recording. (Groups G and H cannot run past the end: their length is fitted to their signals.)

- [ ] **Step 5: Run the full suite once with the baseline**

```powershell
$env:PYTHONPATH = "training"
.venv\Scripts\python -m kz4ap_synth.suites generate --suite full --out build/suite/full
.venv\Scripts\python -m kz4ap_synth.suites run --out build/suite/full --bench build/windows/bench/Release/kz4ap-bench.exe --front-end baseline
.venv\Scripts\python -m kz4ap_synth.suites summarize --out build/suite/full
```
Expected: one `baseline <name>: CER …` line per scoring (39 recordings, plus a second scoring with station labels for the three group-H recordings), then `wrote build\suite\full\summary.md`. Open it: every group has a table with intervals; group A has S₅₀₀ crossings for 12, 25 and 40 WPM; group B has one row per f_D and style plus "VE3NEA mix"; the file ends with the "Per over" and "Tracks per QSO" tables (no "paired" table yet: only one front end has run). One seed needs about 1.1 GB of disk. These are the baseline's numbers before any front-end work; keep the file (it is under `build/`, not committed) for Task 14.

- [ ] **Step 6: Document**

In `docs/signal-processing.md`, section 11, add at the end:

```markdown
- **Suites** (`training/kz4ap_synth/suites.py`): `smoke` is the CI
  recording; `full` covers sensitivity (oracle, S₅₀₀ −10 … +20 dB at 12, 25
  and 40 WPM), fading anchored to VE3NEA's DeepCW benchmark (his
  Butterworth spectrum, f_D grid 0.1, 0.3, 1, 3 Hz, his ten SNR points as
  S₅₀₀ = his 3 kHz key-on SNR + 7.78 dB, his styles, imbalance, style mix
  and text statistics, and his 2 ms centered edges; compare his curves
  with the no-space CER, his metric), fists, speed changes, interference,
  tuning offsets and drift, whole ragchew QSOs (one station's speed and
  style for both sides), two-station QSOs 0–200 Hz apart with each
  operator's own speed, style, imbalance and level, strong signals, pauses, tune-up carriers, stations
  present from the first sample, crowded bands and a whole band.
  **Per-over CER** (QSOs): the edits charged to an over's reference
  symbols by the benchmark's alignment, over those symbols; the word space
  between two overs belongs to neither.
  **Intervals:** every rate and crossing in the summary carries a
  bootstrap 95% interval over signals (1000 resamples; errors cluster
  within a signal, so signals are the units), and front ends are compared
  signal by signal on the same recordings. The full suite is sized for
  3 seeds: at least 1000 characters per S₅₀₀ point in groups A–C, and at
  least 100 fade times per point at f_D = 0.1 Hz.
  **QSO regimes** (group H): same-track for an answering station within
  2 FFT bins (46.9 Hz) of the caller, ambiguous below 3 bins (70.3 Hz, the
  detector's minimum peak separation), separate-track beyond; each QSO is
  scored with one label for the QSO and with one label per station.
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
CER, character and word-space errors, first-word errors, VE3NEA's no-space
CER, the S₅₀₀ where CER crosses 0.10 and 0.05 (each with a bootstrap 95%
interval), and CPU time per channel-second. The full suite is sized for
three seeds (about 3.2 GB of recordings):

    $env:PYTHONPATH = "training"
    .venv\Scripts\python -m kz4ap_synth.suites generate --suite full --seeds 3 --out build/suite/full
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

### Task 10: Frequency tracker

The prerequisite of spec §5.2 step 1: re-center each station finely and follow its drift. `FrequencyTracker` is a numerically controlled oscillator (NCO) plus a lag-product frequency discriminator, as specified under "Design decisions, B". Under option 1 (owner decisions 2026-09-29) it does not decide which station it follows: it fine-tunes within ±12 Hz of an anchor that its owner sets (the engine sets it from the detector's frequency for the track, Task 13). This task builds and tests it on its own; Task 12 puts it in front of the matched filter.

Every implementer of this task must be told the two standing rules (Global Constraints): **physical units** (parameters in Hz, s, FS, dB with a named reference, never bins or samples; convert only at the point of use; config fields named for their unit), and **`docs/signal-processing.md` is updated in the same commit** as any signal-processing change, including its parameter table and whether each choice is derived, measured or heuristic. Git: one plain git command per call, no attribution lines, never amend.

**Files:**
- Create: `engine/include/kz4ap/frequency_tracker.hpp`, `engine/src/frequency_tracker.cpp`
- Test: `engine/tests/frequency_tracker_test.cpp` (new)
- Modify: `engine/CMakeLists.txt`, `docs/signal-processing.md` (§0, §7, §10)

**Interfaces:**
- Produces (C++, namespace `kz4ap`):
  - `struct FrequencyTrackerConfig { double lag_s = 0.00533; double tau_s = 0.5; double min_weight = 0.6; double max_offset_hz = 75.0; double update_interval_s = 0.0213; double fine_tune_hz = 12.0; double min_coherence = 0.3; };` (all in physical units, owner's rule of 2026-09-29: the lag and the update interval are converted to samples in the constructor, `std::lround(lag_s * sample_rate)` = 8 and `std::lround(update_interval_s * sample_rate)` = 32 at 1500 samples/s)
  - `class FrequencyTracker` with `FrequencyTracker(double sample_rate, double initial_offset_hz, FrequencyTrackerConfig config = {})`, `Sample mix(Sample y)`, `void observe(Sample v, float weight)`, `double offset_hz() const`, `double anchor_hz() const`, `void set_anchor(double anchor_hz)`, `void reset(double initial_offset_hz)`, `void reacquire()`.
  - Contract: `mix` returns y·e^(−jφ) and advances φ by 2π·offset_hz()/sample_rate. `observe` takes the narrow-filtered output of the mixed stream and a weight in [0, 1] (the key-down probability); a weight of 0 changes nothing. The offset is updated every `update_interval_s` (21.3 ms; 32 calls to `observe` at 1500 samples/s), only once the average holds at least `min_weight` and is coherent (|average of products| ≥ `min_coherence` × average of |products|, 0.3), and is clamped to ±`max_offset_hz`. **Anchor (owner decisions 2026-09-29, option 1):** the tracker keeps an anchor, the frequency its owner says the station is at (Hz from the channel center); it starts at the initial offset and changes only through `set_anchor` (and `reset`), never through the tracker's own estimates. An estimate is accepted only within ±`fine_tune_hz` (12 Hz, heuristic) of the anchor; otherwise the average belongs to another station (or to two at once), so it is emptied and the offset returns to the anchor. `set_anchor(f)` clamps f to ±`max_offset_hz`; if the new anchor is more than `fine_tune_hz` from the current offset, the NCO jumps to it and the average is emptied (a turnover moved the detector's peak to another station, or drift took it beyond the fine-tuning), otherwise the offset and the average are kept. `reacquire()` empties the average (weight 0) but keeps the offset, the anchor and the NCO phase: after a long silence the next station's products start a fresh average, from the last offset (Task 12 calls it; review finding C2). `reset(f)` sets the offset and the anchor to f (clamped), zeroes the phase and empties the average. So the tracker by itself follows only stations within ±12 Hz of its anchor, and slow drift only as far as its anchor follows it (the engine moves the anchor with the detector's peak, Task 13).

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
```

Why the expected values hold (derived, not simulated; the tracker's option-1 rules were simulated only as part of the engine, Design decisions B): with a pure tone and weight 1, every rotated lag product is the same phasor, so the average points exactly at the tone's frequency; in `FineTunesOnlyNearItsAnchor` the estimate for a new tone moves from the old frequency toward the new one as the average changes, is accepted while it stays within 12 Hz of the anchor, and is rejected (average emptied, NCO back to the anchor) from then on, so the offset ends exactly at the anchor. In `SetAnchorJumpsOnlyBeyondTheFineTuneRange` the tone at 31 Hz is within 12 Hz of the new anchor (30 Hz), so it is followed.

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
    double lag_s = 0.00533;             // time between the two samples of each phase difference, s (8 samples
                                        // at 1500 samples/s; unambiguous range +/- 1 / (2 lag) = +/- 93.75 Hz)
    double tau_s = 0.5;                 // time constant of the average, s at weight 1
    double min_weight = 0.6;            // the average must hold this much weight (0..1) before it moves the NCO
    double max_offset_hz = 75.0;        // the NCO frequency (and the anchor) are clamped to +/- this, Hz
    double update_interval_s = 0.0213;  // time between NCO frequency updates, s (32 samples at 1500 samples/s)
    double fine_tune_hz = 12.0;         // the tracker's own estimates are accepted only within +/- this of the
                                        // anchor, Hz (owner decision 2026-09-29, option 1; heuristic)
    double min_coherence = 0.3;         // |average of products| / average of |products| needed to move the NCO
};

// Re-centers one station's channel on its carrier. A numerically controlled
// oscillator (NCO) mixes the channel down by f, the estimated residual offset
// of the carrier from the channel's center, Hz. The phase advance of the
// narrow-filtered, re-centered stream over `lag` samples measures what is left
// of the offset, the NCO's own advance is added back to make it absolute, and
// the result is averaged with the key-down probability as weight.
//
// The tracker does not decide which station it follows (owner decision
// 2026-09-29, option 1): its owner sets an anchor (the engine: the detector's
// current frequency for the track, minus the channel center), and the tracker
// fine-tunes within +/- fine_tune_hz of it.
class FrequencyTracker {
public:
    // Throws std::invalid_argument for a non-positive sample rate, an invalid config (including a
    // lag or update interval that rounds to 0 samples), or a max_offset_hz outside the unambiguous
    // range +/- 1 / (2 lag).
    FrequencyTracker(double sample_rate, double initial_offset_hz, FrequencyTrackerConfig config = {});

    // Mixes one channel sample down by the current offset and advances the NCO.
    Sample mix(Sample y);

    // Feeds one narrow-filtered sample of the mixed stream. weight (0..1) is the
    // probability that the key is down; 0 leaves the estimate unchanged.
    void observe(Sample v, float weight);

    // Where the station is, Hz from the channel center (clamped to the NCO range). If
    // it is more than fine_tune_hz from the NCO, the NCO jumps there and the average
    // starts afresh; otherwise fine-tuning continues around it.
    void set_anchor(double anchor_hz);

    double offset_hz() const { return offset_hz_; }  // the NCO frequency, Hz
    double anchor_hz() const { return anchor_hz_; }  // Hz
    // Sets the offset and the anchor to initial_offset_hz and starts afresh.
    void reset(double initial_offset_hz);
    // Starts a fresh average (weight 0) but keeps the offset, the anchor and the NCO
    // phase: after a long silence the next station may be a different one.
    void reacquire();

private:
    void clear_average();

    double rate_;
    FrequencyTrackerConfig config_;
    int lag_;                         // lag_s in samples, converted here from the physical value
    int update_every_;                // update_interval_s in samples
    double alpha_;
    double offset_hz_ = 0;
    double anchor_hz_ = 0;            // where the station is, as the owner says, Hz
    double phase_ = 0;                // NCO phase, rad, kept in [-pi, pi)
    std::vector<Sample> history_;     // the last `lag` observed samples, circular
    std::size_t head_ = 0;
    std::size_t seen_ = 0;
    std::complex<double> average_{};  // weighted average of lag products at the absolute offset, FS^2
    double magnitude_ = 0;            // the same average of |product|, FS^2
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

// A physical duration in samples (owner's rule: convert only at the point of use).
int samples_for(double seconds, double sample_rate) { return static_cast<int>(std::lround(seconds * sample_rate)); }

// Validates the parameters and returns sample_rate. Runs as rate_'s initializer,
// before alpha_ divides by them.
double validated_rate(double sample_rate, const FrequencyTrackerConfig& c) {
    if (!(sample_rate > 0) || !(c.lag_s > 0) || samples_for(c.lag_s, sample_rate) < 1 || !(c.tau_s > 0) ||
        !(c.min_weight >= 0) || !(c.min_weight < 1) || !(c.max_offset_hz > 0) || !(c.update_interval_s > 0) ||
        samples_for(c.update_interval_s, sample_rate) < 1 || !(c.fine_tune_hz > 0) ||
        !(c.min_coherence >= 0) || !(c.min_coherence < 1))
        throw std::invalid_argument("invalid frequency tracker config");
    if (!(c.max_offset_hz < sample_rate / (2.0 * samples_for(c.lag_s, sample_rate))))
        throw std::invalid_argument("frequency tracker max_offset_hz must be below 1 / (2 lag)");
    return sample_rate;
}

}  // namespace

FrequencyTracker::FrequencyTracker(double sample_rate, double initial_offset_hz, FrequencyTrackerConfig config)
    : rate_(validated_rate(sample_rate, config)),
      config_(config),
      lag_(samples_for(config.lag_s, sample_rate)),
      update_every_(samples_for(config.update_interval_s, sample_rate)),
      alpha_(1.0 - std::exp(-1.0 / (config.tau_s * sample_rate))) {
    reset(initial_offset_hz);
}

void FrequencyTracker::clear_average() {
    average_ = {};
    magnitude_ = 0;
    weight_ = 0;
}

void FrequencyTracker::reset(double initial_offset_hz) {
    offset_hz_ = std::clamp(initial_offset_hz, -config_.max_offset_hz, config_.max_offset_hz);
    anchor_hz_ = offset_hz_;
    phase_ = 0;
    history_.assign(static_cast<std::size_t>(lag_), Sample{});
    head_ = 0;
    seen_ = 0;
    clear_average();
    until_update_ = update_every_;
}

void FrequencyTracker::reacquire() { clear_average(); }

void FrequencyTracker::set_anchor(double anchor_hz) {
    anchor_hz_ = std::clamp(anchor_hz, -config_.max_offset_hz, config_.max_offset_hz);
    if (std::abs(anchor_hz_ - offset_hz_) > config_.fine_tune_hz) {
        // The detector's track moved to another peak (a turnover within the channel distance) or
        // drifted beyond the fine-tuning: go there and start a fresh average.
        offset_hz_ = anchor_hz_;
        clear_average();
    }
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
        const std::complex<double> absolute = z * std::polar(1.0, kTwoPi * offset_hz_ * lag_ / rate_);
        const double a = alpha_ * w;
        average_ += a * (absolute - average_);
        magnitude_ += a * (std::abs(absolute) - magnitude_);
        weight_ += a * (1.0 - weight_);
    }
    if (--until_update_ == 0) {
        until_update_ = update_every_;
        // A coherent average (one station) moves the NCO; while the average changes from one
        // station to another its phasors cancel and it waits.
        if (weight_ >= config_.min_weight && std::abs(average_) > config_.min_coherence * magnitude_) {
            const double estimate = std::arg(average_) * rate_ / (kTwoPi * lag_);
            if (std::abs(estimate - anchor_hz_) <= config_.fine_tune_hz) {
                offset_hz_ = std::clamp(estimate, -config_.max_offset_hz, config_.max_offset_hz);
            } else {
                // Not this channel's station (the detector decides which station that is): forget
                // these products and return to the anchor.
                clear_average();
                offset_hz_ = anchor_hz_;
            }
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
Expected: 13 tests pass. If `EstimateIsAccurateInNoise` fails, do not widen its tolerance: print the error and compare it with the derived estimate in "Design decisions, B" (about 0.25 Hz RMS here); a large disagreement means a bug in the rotation or the averaging. If a test fails because a parameter value (not the code) is wrong, stop and report the measurement to the owner; do not change the value (parameter tuning is deferred to the benchmark results; owner, 2026-09-27 and 2026-09-29).

- [ ] **Step 5: Document**

In `docs/signal-processing.md`, add to the symbols table in section 0:

```markdown
| f̂ | frequency tracker's estimate of a station's residual offset from its channel center, and the NCO frequency | Hz |
| f_a | the tracker's anchor: where the station is, as the detector says (its frequency for the track minus the channel center) | Hz |
| τ_L | lag of the frequency discriminator | 5.33 ms (8 samples at 1500 samples/s) |
| τ_f | time constant of the frequency average, counted in samples of weight 1 | 0.5 s |
```

At the end of section 7, after "Residual frequency offset", add:

```markdown
### Frequency re-centering (Matched front end only)

Not yet wired into the decoder: nothing in the engine calls it, and the
default decoder (section 8) does not re-center. `FrequencyTracker`
(frequency_tracker.cpp) is built to run per station at r = 1500 samples/s
on a station's channel stream (section 7); this section describes the
component as implemented and tested on its own.

- **NCO (derived):** u[n] = y[n]·e^(−jφ[n]), φ advancing by 2π·f̂/r per
  sample. f̂ starts at the initial offset its owner gives it (meant to be
  the detector's residual, track frequency minus the channel's center;
  about 0.2 Hz error measured for a clean station) and is clamped to ±75 Hz, where the channel filter is 0.34 dB down relative
  to the passband (**heuristic**; beyond it the channel itself would have to
  move, which the code does not do).
- **Discriminator (derived):** on the narrow-filtered, re-centered stream
  v[n], the product z = v[n]·conj(v[n−L]) has phase 2π·(Δf − f̂)·L/r for a
  station at Δf. Rotating it by e^(j2π·f̂·L/r) makes it a measurement of Δf
  itself, so the average below does not depend on the NCO and there is no
  loop to stabilize (to first order: v averages samples mixed under the
  last K/32 NCO settings while each product is rotated by the current f̂,
  a small coupling while f̂ moves, harmless because τ_f·r = 750 samples is
  much longer than K/2). The lag τ_L = 5.33 ms (8 samples at r) gives an
  unambiguous range of ±1/(2τ_L) = ±93.75 Hz (**derived**; the value is
  **heuristic**; it is converted to samples from the physical value).
- **Average (heuristic):** Z̄ ← Z̄ + α·p·(rotated z − Z̄), α = 1 − e^(−1/(τ_f·r)),
  τ_f = 0.5 s, weighted by p, the front end's key-down probability, so
  key-up and pauses leave it unchanged. Every 32 samples (21.3 ms)
  f̂ ← arg(Z̄)·r/(2πL), once the average holds weight 0.6 or more and is
  coherent (|Z̄| ≥ 0.3 × the same average of |z|).
- **Fine-tuning around the detector's frequency (heuristic; owner
  decisions 2026-09-29, option 1):** the tracker does not decide which
  station it follows. Its anchor f_a is set by its owner (`set_anchor`;
  meant to be where the detector says the station is, minus the channel
  center, set before every channel block); the anchor never follows the
  tracker's own estimates. An estimate is accepted only within
  ±12 Hz of f_a (**heuristic**, the owner's value; it must exceed the
  detector's interpolation error, 0.2 Hz measured, clamped to ±11.7 Hz,
  and stay well below the 47 Hz channel distance); otherwise the average
  is emptied and f̂ returns to f_a. When f_a moves more than 12 Hz from
  f̂ (the detector's track moved to another station's peak in a QSO
  turnover, or drifted), f̂ jumps to f_a and the average restarts. So the
  channel's station is followed through slow drift as far as the
  detector's peak goes (a 1 s power average lags a ramp of ḟ Hz/s by
  ḟ·1 s, derived), and a station more than 12 Hz from the detector's
  frequency can never pull the tracker toward it (as long as the owner
  keeps the anchor there).
- **Expected accuracy:** about 0.5 Hz RMS at S₅₀₀ = 0 dB and 0.9 Hz at
  −5 dB, 25 WPM (derived, an upper bound); a simulation of the whole chain
  (NCO, K = 58 boxcar, posterior weights, 60 s of PARIS, 4 seeds; plan
  review, 2026-09-27) gave 0.08, 0.27 and 0.55 Hz RMS at +10, 0 and −5 dB
  (simulated), valid once a station has been acquired, which needs
  S₅₀₀ ≥ −2.5 dB (derived) at any speed, in simulation 50% of marks keyed
  near −1.8 dB at 25 WPM (section 8b, "Squelch"); Task 14 measures
  it in the benchmark; a linear drift of
  ḟ Hz/s is followed with a lag of about ḟ·τ_f/P₁ (1.1 Hz at 1 Hz/s,
  P₁ = 0.44, derived; 1.49–1.63 Hz simulated at the end of the last mark
  through the whole engine). Target (spec §5.2): within ±2 Hz, a loss of
  0.2 dB relative to a centered station at 20 WPM through a filter of
  length T.
```

In section 10, add rows:

```markdown
| Frequency discriminator lag | 5.33 ms (8 samples at 1500 samples/s; ±93.75 Hz unambiguous) | `FrequencyTrackerConfig::lag_s` | heuristic within derived range |
| Frequency average | τ_f = 0.5 s of key-down weight; moves the NCO at weight ≥ 0.6 and coherence ≥ 0.3, every 21.3 ms | `FrequencyTrackerConfig` (`tau_s`, `min_weight`, `min_coherence`, `update_interval_s`) | heuristic |
| Fine-tuning range | ±12 Hz around the anchor (the detector's frequency for the track); farther estimates are discarded; the NCO jumps to an anchor more than 12 Hz away | `FrequencyTrackerConfig::fine_tune_hz` | heuristic (owner decision 2026-09-29, option 1) |
| NCO range | ±75 Hz | `FrequencyTrackerConfig::max_offset_hz` | heuristic |
```

- [ ] **Step 6: Commit on the `milestone-2` branch**

```powershell
git add engine/include/kz4ap/frequency_tracker.hpp engine/src/frequency_tracker.cpp engine/tests/frequency_tracker_test.cpp engine/CMakeLists.txt docs/signal-processing.md
```
```powershell
git commit -m "Add a per-station frequency tracker: NCO and lag-product discriminator, fine-tuning around an anchor"
```

---

### Task 11: Matched front end

Spec §5.2 step 1 proper: a complex filter matched to the current dit estimate, before envelope detection, and a Rician-versus-Rayleigh log-likelihood ratio from running estimates of the noise and the key-down amplitude, as specified under "Design decisions, C". This task builds and tests `MatchedFrontEnd` on its own; Task 12 wires it into the decoder.

**Files:**
- Create: `engine/include/kz4ap/matched_front_end.hpp`, `engine/src/matched_front_end.cpp`
- Test: `engine/tests/matched_front_end_test.cpp` (new)
- Modify: `engine/CMakeLists.txt`, `docs/signal-processing.md` (§0, new §8b, §10)

**Interfaces:**
- Consumes: `kz4ap::test::keyed_signal`, `keying`, `duration_for` from `engine/tests/test_signals.hpp` (tests only).
- Produces (C++, namespace `kz4ap`):
  - `double log_bessel_i0(double z);` and `double envelope_llr(double x, double a);`
  - `struct MatchedFrontEndConfig { double initial_wpm = 60.0; double min_wpm = 5.0; double length_dits = 0.8; double warmup_s = 0.32; double noise_tau_s = 2.0; double amplitude_tau_s = 0.5; double noise_guard = 1.75; double neighbor_guard = 4.0; double floor_quantile = 0.1; int floor_samples = 64; double floor_min_clean = 0.25; double floor_margin = 2.5; double floor_restart_ratio = 4.0; double prior_key_down = 0.44; double squelch_a = 3.0; double squelch_exponent = 0.25; };`
  - `struct FrontEndSample { Sample filtered; float llr; float log_odds; float p_key_down; float weight; bool ready; bool signal; };`
  - `class MatchedFrontEnd` with `explicit MatchedFrontEnd(double sample_rate, MatchedFrontEndConfig config = {})`, `FrontEndSample step(Sample u)`, `void set_dit(double dit_s)`, `void reacquire()`, `void reset()`, `int length() const`, `double noise_sigma() const`, `double amplitude() const`, `double squelch() const`.
  - Contract: `filtered` is the mean of the last K inputs (FS); `llr` is Λ (nats); `log_odds` is Λ + ln(P₁/P₀); `p_key_down` is the posterior, forced to 0 while `signal` is false; `weight` is 1/K; `ready` is false during warm-up, when only `filtered` and `weight` are valid. The noise estimate never reads the posterior, the log-odds or ŝ (review finding C1): it uses only |v|² and its own σ̂² (Design decisions, C). `reacquire()` puts K back to the acquisition width (rescaling σ̂²), sets ŝ² and its weight to 0, and keeps σ̂² (review finding C2). The warm-up always runs at the acquisition width: a `set_dit` during it takes effect when it ends, and then restarts ŝ² (re-review finding I-1). `squelch()` is a_min for the current filter, 3·(T_v/16 ms)^(1/4) with T_v = K/r its duration, which is 3·(K/24)^(1/4) at 1500 samples/s (re-review finding I-2; the code takes the ratio of K to the acquisition length, so it is a ratio of durations). A floor keeps σ̂² from staying far below the noise (re-review finding I-1); it assumes a station leaves at least c = 0.25 of its samples clean, and a lift restarts ŝ only if the floor exceeds 4·σ̂² (final check F-3).

- [ ] **Step 1: Write the failing tests**

Create `engine/tests/matched_front_end_test.cpp`:

```cpp
#include "kz4ap/matched_front_end.hpp"

#include "test_signals.hpp"

#include <gtest/gtest.h>

#include <algorithm>
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
    EXPECT_EQ(fe.length(), 24);  // the warm-up (0.32 s = 480 samples) runs at the acquisition width...
    run(fe, white_noise(480, 1.0, 1));
    EXPECT_EQ(fe.length(), 58);  // ...and then takes the dit: 0.8 x 48 ms x 1500 samples/s = 57.6
    fe.set_dit(10.0);
    EXPECT_EQ(fe.length(), 288);  // clamped at 5 WPM
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
    double warmup_s = 0.32;             // collected at the acquisition width before the first estimates, s
    double noise_tau_s = 2.0;           // noise estimate's time constant, s of noise updates
    double amplitude_tau_s = 0.5;       // amplitude estimate's time constant, s of key-down weight
    double noise_guard = 1.75;          // kappa: v[n-K] updates sigma^2 only if |v|^2 / (2 sigma^2) is below this...
    double neighbor_guard = 4.0;        // ...and v[n], v[n-2K] are below this (a mark or ramp next to it)
    double floor_quantile = 0.1;        // floor: this quantile of |v|^2 taken every K samples...
    int floor_samples = 64;             // ...over this many of them...
    double floor_min_clean = 0.25;      // ...assuming a station leaves at least this fraction of them clean...
    double floor_margin = 2.5;          // ...and dividing by this for sampling spread
    double floor_restart_ratio = 4.0;   // a lift restarts s-hat only if the floor exceeds this times sigma^2
    double prior_key_down = 0.44;       // P1: PARIS keys down 22 of 50 dit units
    double squelch_a = 3.0;             // key-down evidence is ignored while a = s / sigma is below this with the
                                        // acquisition filter (16 ms: 0.8 of the 60 WPM dit)...
    double squelch_exponent = 0.25;     // ...times (filter duration / 16 ms)^this at other filter lengths
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
    // After a long silence: back to the acquisition width (initial_wpm), a fresh
    // amplitude estimate; the noise estimate is kept (rescaled to the new width).
    void reacquire();
    void reset();

    int length() const { return length_; }  // K, samples
    double noise_sigma() const;             // sigma: noise RMS per real component of v, FS
    double amplitude() const;               // s: key-down amplitude of v, FS
    double squelch() const;                 // a_min at the current K

private:
    int length_for(double dit_s) const;
    void apply_length(int k);
    void resum();
    void start_estimates();
    void update_noise();
    void update_floor(double power);

    double rate_;
    MatchedFrontEndConfig config_;
    double noise_alpha_;
    double amplitude_alpha_;
    double log_prior_odds_;
    double guard_mean_;                 // E[y | y < kappa] for y ~ Exp(1): undoes the guard's truncation
    double floor_divisor_;              // the floor's quantile of |v|^2 over this is at most sigma^2
    int max_length_;
    int acquisition_length_;            // K at initial_wpm
    int length_ = 1;
    std::vector<Sample> ring_;          // the last max_length_ inputs, circular
    std::size_t head_ = 0;              // where the next input goes
    std::complex<double> sum_{};        // sum of the last length_ inputs
    std::size_t since_resum_ = 0;
    std::vector<double> power_ring_;    // |v|^2 of the last 2 max_length_ + 1 outputs, circular
    std::uint64_t count_ = 0;           // samples stepped since reset
    std::size_t warmup_left_ = 0;
    std::vector<double> warmup_power_;
    double pending_dit_ = 0;            // a set_dit during the warm-up, applied when it ends (0 = none)
    std::vector<double> floor_ring_;    // |v|^2 every K samples, circular
    std::size_t floor_head_ = 0;
    std::size_t floor_filled_ = 0;
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
#include <cstddef>
#include <stdexcept>

namespace kz4ap {
namespace {

constexpr std::size_t kResumEvery = 4096;             // recompute the running sum this often, samples
constexpr double kMinNoiseVar = 1e-20;                // FS^2 (-200 dBFS): keeps x and a finite on noise-free input
constexpr double kWarmupNoiseQuantile = 0.2;           // the warm-up's noise estimate uses this quantile of |v|^2
constexpr double kLiftWeight = 16;                     // noise weight left after the floor lifts sigma^2

// Validates the parameters and returns sample_rate. Runs as rate_'s initializer,
// before anything divides by them.
double validated_rate(double sample_rate, const MatchedFrontEndConfig& c) {
    if (!(sample_rate > 0) || !(c.min_wpm > 0) || !(c.initial_wpm >= c.min_wpm) || !(c.length_dits > 0) ||
        !(c.warmup_s > 0) || !(c.noise_tau_s > 0) || !(c.amplitude_tau_s > 0) || !(c.noise_guard > 0) ||
        !(c.neighbor_guard >= c.noise_guard) || !(c.floor_quantile > 0) || !(c.floor_min_clean <= 1) ||
        !(c.floor_quantile < c.floor_min_clean) || c.floor_samples < 2 || !(c.floor_margin >= 1) ||
        !(c.floor_restart_ratio >= 1) || !(c.prior_key_down > 0) || !(c.prior_key_down < 1) ||
        !(c.squelch_a >= 0) || !(c.squelch_exponent >= 0))
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
      guard_mean_(1.0 - config.noise_guard * std::exp(-config.noise_guard) / (1.0 - std::exp(-config.noise_guard))),
      // Noise alone: the q-quantile of |v|^2 is 2 sigma^2 (-ln(1 - q)). With a station leaving a clean
      // fraction c, it is at most noise's (q/c)-quantile, 2 sigma^2 (-ln(1 - q/c)): dividing by that
      // (and a margin for sampling spread) keeps the floor below sigma^2.
      floor_divisor_(-2.0 * std::log(1.0 - config.floor_quantile / config.floor_min_clean) * config.floor_margin),
      max_length_(std::max(1, static_cast<int>(std::lround(config.length_dits * 1.2 / config.min_wpm * sample_rate)))),
      acquisition_length_(0) {
    acquisition_length_ = length_for(1.2 / config_.initial_wpm);
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
    power_ring_.assign(2 * static_cast<std::size_t>(max_length_) + 1, 0.0);
    count_ = 0;
    warmup_left_ = std::max<std::size_t>(1, static_cast<std::size_t>(std::lround(config_.warmup_s * rate_)));
    warmup_power_.clear();
    pending_dit_ = 0;
    floor_ring_.assign(static_cast<std::size_t>(config_.floor_samples), 0.0);
    floor_head_ = floor_filled_ = 0;
    noise_var_ = amp2_ = 0;
    noise_weight_ = amplitude_weight_ = 0;
}

double MatchedFrontEnd::noise_sigma() const { return std::sqrt(noise_var_); }

double MatchedFrontEnd::amplitude() const { return std::sqrt(amp2_); }

double MatchedFrontEnd::squelch() const {
    // In noise alone a-hat squared is a p-weighted mean over about tau_a r / K independent samples,
    // so its spread grows as sqrt(K): scaling a_min as K^(1/4) keeps the chance that noise lifts
    // a-hat past it the same at every K (derived, Gaussian approximation).
    return config_.squelch_a *
           std::pow(static_cast<double>(length_) / acquisition_length_, config_.squelch_exponent);
}

void MatchedFrontEnd::resum() {
    const std::size_t n = ring_.size();
    sum_ = {};
    for (int i = 1; i <= length_; ++i) sum_ += std::complex<double>(ring_[(head_ + n - static_cast<std::size_t>(i)) % n]);
    since_resum_ = 0;
}

void MatchedFrontEnd::set_dit(double dit_s) {
    if (warmup_left_ > 0) {  // the warm-up always runs at the acquisition width
        pending_dit_ = dit_s;
        return;
    }
    apply_length(length_for(dit_s));
}

void MatchedFrontEnd::apply_length(int k) {
    if (k == length_) return;
    // The boxcar's output noise power is proportional to 1/K for noise flat across its passband.
    const double scale = static_cast<double>(length_) / k;
    noise_var_ = std::max(kMinNoiseVar, noise_var_ * scale);
    for (auto& x : floor_ring_) x *= scale;
    length_ = k;
    resum();
}

void MatchedFrontEnd::reacquire() {
    if (warmup_left_ > 0) {
        pending_dit_ = 0;
        return;
    }
    apply_length(acquisition_length_);  // rescales sigma^2
    amp2_ = 0;
    amplitude_weight_ = 0;
}

void MatchedFrontEnd::start_estimates() {
    std::vector<double> sorted = warmup_power_;
    std::sort(sorted.begin(), sorted.end());
    const auto quantile = [&](double q) {
        return sorted[static_cast<std::size_t>(q * static_cast<double>(sorted.size() - 1))];
    };
    // In noise alone |v|^2 is exponential with mean 2 sigma^2; its q-quantile is 2 sigma^2 (-ln(1 - q)).
    // 0.32 s at K = 24 holds about 20 independent samples (section 8b).
    noise_var_ = std::max(kMinNoiseVar, quantile(kWarmupNoiseQuantile) /
                                            (2.0 * -std::log(1.0 - kWarmupNoiseQuantile)));
    amp2_ = std::max(0.0, quantile(0.9) - 2.0 * noise_var_);
    // The warm-up counts as this much weight, so the samples after it refine the estimates.
    noise_weight_ = 0.1 * static_cast<double>(sorted.size());
    amplitude_weight_ = 0.1 * static_cast<double>(sorted.size());
    warmup_power_.clear();
    if (pending_dit_ > 0) {
        apply_length(length_for(pending_dit_));
        amp2_ = 0;  // measured at the acquisition width
        amplitude_weight_ = 0;
        pending_dit_ = 0;
    }
}

void MatchedFrontEnd::update_floor(double power) {
    // Every K-th |v|^2 (such samples share no inputs). If sigma^2 lies below the floor, the noise
    // has risen (or the estimate started low): lift it and let the next updates count for more.
    // Only a large lift (the stuck-low case, where the low sigma-hat has let s-hat grow on noise)
    // restarts s-hat; a small one is the quantile's spread or a second station in the channel, and
    // restarting s-hat there refits it from a few samples, often on a filter ramp (final check F-3).
    if (count_ % static_cast<std::uint64_t>(length_) != 0) return;
    floor_ring_[floor_head_] = power;
    floor_head_ = (floor_head_ + 1) % floor_ring_.size();
    floor_filled_ = std::min(floor_filled_ + 1, floor_ring_.size());
    if (floor_filled_ < floor_ring_.size() / 2) return;
    std::vector<double> v(floor_ring_.begin(), floor_ring_.begin() + static_cast<std::ptrdiff_t>(floor_filled_));
    const auto nth = v.begin() + static_cast<std::ptrdiff_t>(config_.floor_quantile * static_cast<double>(v.size() - 1));
    std::nth_element(v.begin(), nth, v.end());
    const double floor = *nth / floor_divisor_;
    if (noise_var_ < floor) {
        const bool stuck_low = floor > config_.floor_restart_ratio * noise_var_;
        noise_var_ = floor;
        noise_weight_ = std::min(noise_weight_, kLiftWeight);
        if (stuck_low) {
            amp2_ = 0;
            amplitude_weight_ = 0;
        }
    }
}

void MatchedFrontEnd::update_noise() {
    // Three taps K apart, v[n], v[n-K], v[n-2K], share no inputs, so in white noise they are
    // independent. The middle one updates sigma^2 only if it is below kappa * 2 sigma^2 and its
    // neighbors below kappa_n * 2 sigma^2: a mark or filter ramp within K of it lifts a tap above
    // that, except at low SNR. The middle tap is then an exponential truncated at kappa, whose
    // mean is guard_mean_ times the untruncated one; dividing by it makes the estimate unbiased
    // in noise (the neighbors are independent of it). Nothing here reads the posterior, the
    // log-odds or s-hat (review finding C1).
    const auto k = static_cast<std::uint64_t>(length_);
    if (count_ <= 2 * k) return;
    const std::size_t n = power_ring_.size();
    const double limit = config_.noise_guard * 2.0 * noise_var_;
    const double neighbor_limit = config_.neighbor_guard * 2.0 * noise_var_;
    const double now = power_ring_[(count_ - 1) % n];
    const double middle = power_ring_[(count_ - 1 - k) % n];
    const double oldest = power_ring_[(count_ - 1 - 2 * k) % n];
    if (!(middle < limit && now < neighbor_limit && oldest < neighbor_limit)) return;
    noise_weight_ += 1.0;
    noise_var_ = std::max(kMinNoiseVar, noise_var_ + std::max(noise_alpha_, 1.0 / noise_weight_) *
                                                         (0.5 * middle / guard_mean_ - noise_var_));
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

    update_floor(power);
    const double sigma = std::sqrt(noise_var_);
    const double a = std::sqrt(amp2_) / sigma;
    const double llr = envelope_llr(std::sqrt(power) / sigma, a);
    const double g = llr + log_prior_odds_;
    const double p = logistic(g);
    out.ready = true;
    out.signal = a >= squelch();
    out.llr = static_cast<float>(llr);
    out.log_odds = static_cast<float>(g);
    out.p_key_down = out.signal ? static_cast<float>(p) : 0.0f;

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
Expected: 17 tests pass (several of them loop over 10–20 seeds and take a few seconds). These tests pin behavior the design derived and a Python port simulated; if one fails, find out why before touching a tolerance (superpowers:systematic-debugging). In particular, `EstimatesNoiseAndAmplitude` and `StrongSignalKeepsNoiseEstimate` fail if the three-tap guard lets filter ramps through, `NoiseAloneKeepsTheNoiseEstimateAndNeverKeys` fails if the estimate is biased or the amplitude does not decay in noise alone, `NoiseEstimateRecoversFromANoiseStep` fails if the floor or the relaxed neighbor guard is wrong, and `NoiseEstimateAtMinusFiveDbS500HasItsKnownLeak` fails if marks leak into the noise estimate more (or less) than simulated. Each multi-seed test states its per-seed band and a band for the mean, both from the simulation of this code (several standard deviations wide). If the cause is a code bug, fix it. If the cause is a parameter value (κ, a time constant, the squelch), **stop and report the measurement to the owner; do not change the value**: parameter tuning is deferred to the benchmark results (owner, 2026-09-27 and 2026-09-29).

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
  scaled by K_old/K_new (derived for white noise at r; the channel filter
  removes the boxcar's sidelobes beyond ±150 Hz, keeping about 0.92 of the
  boxcar's noise power at K = 24 and 0.965 at K = 58, so the rescale and
  the a² formula below are off by about 0.2 dB relative to the true
  noise power, derived).
- **Likelihood (derived; Proakis eq. 4.5–21):** Λ = −a²/2 + ln I₀(a·x),
  x = |v|/σ̂, a = ŝ/σ̂, in nats; ln I₀ from Abramowitz & Stegun 9.8.1–9.8.2
  without overflow. g = Λ + ln(P₁/P₀) with P₁ = 0.44 (derived from PARIS:
  key-down 22 of 50 dit units); p = 1/(1 + e^(−g)). For noise flat across
  the filter, a² = 2·S₅₀₀·(500 Hz)·K/r, S₅₀₀ as a linear ratio (derived):
  a = 6.2 at S₅₀₀ = 0 dB and 25 WPM.
- **Amplitude estimate (heuristic running form of an EM update):**
  ŝ² ← max(0, ŝ² + p·max(α_a, 1/W_a)·(|v|² − 2σ̂² − ŝ²)), the Rician mean
  square being 2σ² + s²; τ_a = 0.5 s of key-down weight. Samples on the
  filter's ramps bias ŝ low: 0.79 of s for dits alone (derived, noise-free
  trapezoid at K = 58), 0.85–0.88 of s for PARIS at 25 WPM, S₅₀₀ 0–60 dB
  (simulated). The decision point near ŝ/2 then lengthens each mark by
  about 7 ms at 25 WPM (3.5 ms per edge, simulated).
- **Noise estimate (heuristic form; derived bias correction):** three
  taps K apart, v[n], v[n−K] and v[n−2K], share no inputs, so in white
  noise they are independent. The middle one updates
  σ̂² ← σ̂² + max(α_n, 1/W_n)·(|v[n−K]|²/(2·m(κ)) − σ̂²), τ_n = 2 s of
  updates, only if it has |v|²/(2σ̂²) < κ = 1.75 and its two neighbors
  < κ_n = 4. A mark or
  a filter ramp within K of the middle tap lifts some tap above that (at
  S₅₀₀ = 60 dB by orders of magnitude). In noise, y = |v|²/(2σ²) is
  exponential with mean 1 (Proakis eq. 2.3–43), and the accepted middle
  tap is y truncated at κ, with mean m(κ) = 1 − κ·e^(−κ)/(1 − e^(−κ)) =
  0.632 at κ = 1.75 (derived), which the update divides out. The estimate
  never reads p, g or ŝ. In noise alone its only fixed point is σ̂ = σ,
  and it is stable (derived: the map r ↦ m(κr)/m(κ) has slope
  κ·m′(κ)/m(κ) = 0.651 < 1 at r = 1 and slope κ/(2m(κ)) > 1 near 0, so
  it cannot settle low). Its climb back from a low r is slow, though:
  at r it accepts a fraction (1 − e^(−κr))(1 − e^(−κ_n r))² of the
  samples, so from r = 0.25 (a 6 dB rise in the noise) the expected
  return to 0.9 takes about 43 s (derived by integrating
  dr/dt = (m(κr)/m(κ) − r)·acceptance(r)/τ_n); the relaxed neighbor
  guard κ_n = 4 cuts it from 98 s (κ_n = κ) to 43 s (derived the same
  way). This replaces a guard on the posterior, which selected quiet
  stretches, biased σ̂ low and in noise alone settled at σ̂ = 0.53–0.57·σ
  in 7 of 10 seeds, keying noise (plan review 2026-09-27; simulated).
  With the floor and warm-up below (simulated, numpy seeds, final check
  2026-09-28): noise alone (40 seeds × 120 s) σ̂/σ has mean 1.00 and
  standard deviation 0.020 at K = 24 and 0.032 at K = 58 (0.080 at
  K = 288, 20 seeds, re-review), with no signal flag; with continuous
  PARIS σ̂ is 0.98–1.06·σ at S₅₀₀ 0–60 dB and 25 WPM (0.90–1.11·σ at
  12 WPM), and the leak of weak marks lifts it to 1.16–1.23·σ at −5 dB
  and 1.22–1.30·σ at −8 dB (25 WPM); after a 6 dB rise in the noise the
  last signal flag came at most 16 s later and σ̂ returned to 0.9·σ within
  24–41 s at K = 24 (40 seeds), 25–51 s at K = 58 and 24–56 s at K = 288
  (20 seeds each), in line with the derived 43 s.
- **Floor (heuristic; its bound derived):** every K samples the 10th
  percentile Q of the last 64 samples of |v|² taken K apart gives a
  floor F = Q / (2·(−ln(1 − 0.1/c))·2.5), c = 0.25. In noise Q is
  2σ²·(−ln 0.9); with a station leaving a clean fraction of at least c of
  those samples, Q is at most noise's (0.1/c)-quantile, so F ≤ σ²/2.5
  (derived), and the factor 2.5 covers the sampling spread of a
  64-sample quantile (heuristic). **Why c = 0.25 (inputs derived,
  rounding heuristic):** a sample is clean when its K-sample window lies
  inside a space, so a gap of g dits leaves g − β dits clean; continuous
  text at K = 0.8·dit leaves 0.29–0.33 of the samples clean (PARIS 0.33,
  a CQ call 0.29, a contest exchange and a pangram 0.31; computed from
  the keyed envelopes), and 0.24–0.28 with the speed estimate 25% slow
  (K = one dit); c = 0.25 rounds the lower end down. Solid digits
  ("0000 9999", 0.18) and two stations keying at once are not covered;
  an earlier c = 0.4 was above continuous PARIS's 0.34 and made the floor
  lift routinely under strong text (final check 2026-09-28). If σ̂² falls
  below F, σ̂² ← F and the noise weight drops to 16 (so the next updates
  count for more); ŝ restarts only if F > 4·σ̂² (the factor
  **heuristic**), the stuck-low case in which the low σ̂ has let ŝ grow on
  noise. A smaller lift is the quantile's spread or a second station, and
  restarting ŝ there refit it from a few samples on a filter ramp and
  merged dits (simulated: 38 edits in 7140 characters at S₅₀₀ = 60 dB
  with c = 0.4 and every lift restarting ŝ, 0 now; a neighbor 100 Hz
  away keying at the same time, +10 dB re the wanted station's key-down
  power, garbled the wanted station in 12 of 100 seeds, 2 now). In
  noise F = 0.0825·σ², so the floor acts when σ̂ < 0.29·σ and restarts ŝ
  when σ̂ < 0.14·σ (derived). This removes the stuck-low state (a start or
  a noise rise that leaves σ̂ that low) and bounds large rises: after a
  10 or 20 dB rise it lifted in every seed and σ̂ was back within 0.9·σ
  in 11–38 s (20 seeds, K = 24). It does not act after a 6 dB rise, whose
  recovery is the guard's climb (above). During the climb noise can be
  flagged as signal for up to about 16 s (simulated).
- **Warm-up (heuristic):** the first 0.32 s only collect |v|², always at
  the acquisition width (K = 24; a `set_dit` meanwhile takes effect when
  the warm-up ends, and restarts ŝ). 0.32 s is 20 filter lengths, about 20
  independent samples. Then σ̂² = (20th percentile)/(2·(−ln 0.8)) (derived
  for noise alone) and ŝ² = max(0, 90th percentile − 2σ̂²), and the
  warm-up counts as 0.1 times its length in both noise and amplitude
  weight. (The earlier 5th percentile of 0.2 s, about 12 independent
  samples at K = 24 and 1 at K = 288, often started σ̂ at 0.2–0.5·σ.)
- **Squelch (heuristic value; its K-scaling derived):** p is forced to 0
  while a < a_min(K) = 3·(K/24)^(1/4). In noise alone â² is a p-weighted
  mean over about τ_a·r/K independent samples, so its spread grows as √K,
  and a_min ∝ K^(1/4) keeps the chance that noise alone passes the
  squelch the same at every K (derived, Gaussian approximation); a flat
  a_min = 3 would pass noise more often at long K. With a² =
  2·S₅₀₀·(500 Hz)·K/r the squelch is S₅₀₀ = −2.5 dB at K = 24 (any speed),
  −4.4 dB at K = 58 (25 WPM), −6.0 dB at K = 120 (12 WPM), −7.9 dB at
  K = 288 (5 WPM) (derived), and up to 1.4 dB higher with ŝ's ramp bias
  (ŝ = 0.85·s: 20·log₁₀(1/0.85) = 1.4 dB, derived). **A station is first
  keyed at the acquisition width**, so the sensitivity floor for
  acquiring one is S₅₀₀ = −2.5 dB derived at every speed; in simulation
  (continuous PARIS, 10 seeds) 0%, 3%, 45% and 80% of marks were keyed
  at −4, −3, −2 and −1 dB at 25 WPM, and 19%, 33% and 95% at −4, −3 and
  −2 dB at 12 WPM, so 50% is reached near −1.8 dB and −2.6 dB. The lower
  figures hold only for a station already acquired and narrowed to. In
  noise alone, with σ̂
  correct, the amplitude update shrinks a² by about P₀ = 0.56 per time
  constant (derived for small a), so a decays toward 0; this holds only
  because σ̂ does not depend on a (above).
- **Correlated samples (heuristic):** successive outputs of a K-sample
  boxcar share inputs; their noise autocorrelation is triangular and sums
  to K (derived). Each `FrontEndSample` carries weight 1/K, the factor a
  sequence decoder may apply to Λ before summing over samples, keeping the
  0.67 ms timing resolution. Scaling a nonlinear per-sample LLR by the
  correlation length is an approximation (the sufficient statistic for one
  element is one matched-filter sample); the HMM plan may decimate at a
  stride of K instead. The baseline decoder keys from g sample by sample
  and does not sum, so it ignores the weight.
```

In section 10, add rows:

```markdown
| Matched filter | boxcar, K = round(0.8·dit·r), starts at 60 WPM, max 288 samples | `MatchedFrontEndConfig` | derived shape; β and start heuristic |
| Likelihood | Λ = −a²/2 + ln I₀(a·x); prior P₁ = 0.44 | matched_front_end.cpp | derived |
| Amplitude / noise estimates | τ_a = 0.5 s (EM, p-weighted) / τ_n = 2 s (middle tap below κ = 1.75, neighbors below κ_n = 4, truncation mean 0.632 divided out) | `MatchedFrontEndConfig` | heuristic; the truncation correction derived |
| Noise floor | 10th percentile of 64 samples of \|v\|² taken K apart, over 2·(−ln(1 − 0.1/0.25))·2.5; a lift restarts ŝ only if the floor exceeds 4·σ̂² | `MatchedFrontEndConfig::floor_*` | heuristic; the occupancy bound derived, c = 0.25 from computed clean fractions of continuous text |
| Front-end warm-up | 0.32 s at K = 24; 20th / 90th percentiles, weight 0.1 × its length | `MatchedFrontEndConfig::warmup_s` | heuristic |
| Front-end squelch | a ≥ 3·(K/24)^(1/4) | `MatchedFrontEndConfig::squelch_a`, `squelch_exponent` | 3 heuristic; the K-scaling derived |
```

- [ ] **Step 6: Commit on the `milestone-2` branch**

```powershell
git add engine/include/kz4ap/matched_front_end.hpp engine/src/matched_front_end.cpp engine/tests/matched_front_end_test.cpp engine/CMakeLists.txt docs/signal-processing.md
```
```powershell
git commit -m "Add the dit-matched front end with Rician/Rayleigh log-likelihood ratios"
```

---

### Task 12: Classical decoder — the Matched mode

Every implementer of this task must be told the two standing rules (Global Constraints): **physical units** (parameters in Hz, s, FS, dB with a named reference, never bins or samples; convert only at the point of use; config fields named for their unit), and **`docs/signal-processing.md` is updated in the same commit** as any signal-processing change, including its parameter table and whether each choice is derived, measured or heuristic. Git: one plain git command per call, no attribution lines, never amend.

The minimal way for the baseline decoder to consume the front end (Design decisions, C): in `FrontEnd::Matched` each sample goes through the tracker's NCO, then the matched filter; the tracker observes the filter's output weighted by the key-down posterior; the key goes down when the posterior log-odds g exceeds +h and up when it falls below −h (h = 1 nat), and never goes down while a < a_min. The envelope smoother, the 40%/60% thresholds, the mark/space followers, the warm-up and the M ≥ 3·S squelch are not used in this mode; glitch rejection, element classification, gaps, symbols and speed estimation are shared. `FrontEnd::Envelope` stays the default and must produce exactly what it produced before (the existing tests and the smoke CER pin it).

**Re-acquisition (review finding C2, re-review I-2 to I-4, final check F-1; heuristic).** The filter narrows to the current station's dit and ŝ settles at its level, so a station that answers 25 Hz away (near the narrow filter's nulls) or 6 dB weaker (re the first station's key-down power) never raises the posterior, and neither estimate ever moves to it: in a simulation of the earlier code (A at 0 Hz, S₅₀₀ = 15 dB, 25 WPM; 1 s of silence; B, 112 marks), none of B's marks were keyed at 25, 40 or 50 Hz and −6 or 0 dB re A's key-down power. So when the key has been up for longer than max(`reacquire_min_s` = 0.5 s, `reacquire_after_dits` = 12 dits), the decoder calls `MatchedFrontEnd::reacquire()` (K back to 24 samples, the 60 WPM width; ŝ² and its weight to 0; σ̂_v² kept) and `FrequencyTracker::reacquire()` (a fresh frequency average from the last f̂), once per silence, sets its speed window aside and starts a new one, and the filter follows the speed again only after `follow_after_marks` new marks. If nothing is keyed within `reacquire_window_s` = 2 s, the station was probably just pausing (or none is there): the set-aside speed window comes back and the filter returns to the dit-matched width it had, which is more sensitive than the acquisition width (section 8b, "Squelch"). The new speed window is the final check's fix F-1: a neighbor keyed only in fragments through the wide filter put its fragments in the same window as the caller's marks, the filter followed the mixed estimate, grew longer than the caller's element spaces and merged its marks, and K ran to 288. Until two new marks exist, elements are still classified with the previous station's dit. 12 dits exceeds all but about 1% of word spaces (7 dits nominal; 0.77% of VE3NEA's hand-key word spaces, log-normal with median 7.39 dits and σ_ln = 0.2, exceed 12 dits, derived; one spurious re-acquisition per about 1300 word spaces was simulated, with no marks lost); the 0.5 s floor applies only above 28.8 WPM, where 12 dits is shorter than 0.5 s. **What it reaches (option 1, owner decisions 2026-09-29):** on its own, the tracker fine-tunes only within ±12 Hz of its anchor (Task 10); the decoder exposes `set_frequency_anchor_hz`, and in the engine (Task 13) the anchor is the detector's current frequency for the track, so a station answering within D = 47 Hz is reached when the detector's track moves to its peak (1.3–2.3 s after its first key-down, simulated; Design decisions B), and one farther away belongs to its own track. Re-acquisition still matters for that station: it widens the filter, restarts ŝ and starts a new speed window, so the answering station's marks are keyed and timed on their own. After a re-acquisition B at Δf is also attenuated by the 16 ms (K = 24) boxcar, |sinc(Δf · 16 ms)|² (−2.4 dB relative to a centered station at 25 Hz, −6.9 dB at 40 Hz, derived), and must be keyed at the acquisition width, so it needs roughly S₅₀₀ ≥ 0 dB at 25 Hz. **Simulated at decoder level with the first design (2026-09-29; a Python port of Tasks 10–12 as then written, on the milestone-1 decoder's own element and speed logic, the decoder estimating the dit itself, a tracker following within 57 Hz of a walking anchor and the ×1.25 growth bound while following; this test's turnover, white noise, 100 numpy seeds each; levels in dB re A's key-down power):** B 25 Hz away at −6 dB, and 40 Hz away at 0 dB: f̂ moved to B, and B and A's next over were decoded, in 100 of 100; under option 1 the decoder alone no longer moves to B (its anchor stays on A), so these two cases are now engine-level tests in which the detector moves the anchor (Task 13: `TurnoverWithinTheChannelDistanceFollowsTheAnsweringStation`, `TurnoverAt40HzFollowsTheAnsweringStation`, 30 of 30 simulated). B 50 Hz away at −10 dB: not keyed, f̂ within 0.2 Hz of A, A intact in 99 of 100. B 70 Hz away at −6 dB, 100 Hz away at −6 and +10 dB: f̂ within 0.2 Hz of A, A intact in 99, 99 and 100 of 100. **Which simulated rates carry over (review finding I2; derived, not re-simulated; controller's ruling 2026-09-29: do not re-simulate).** *Low risk:* at the 16 ms (K = 24) acquisition width B looks like S₅₀₀ −7.6 dB (50 Hz, −10 dB re A: 15 − 10 − 12.6 dB), −10.6 dB (70 Hz, −6 dB) and −5.5 dB (100 Hz, −6 dB), all below the −2.5 dB acquisition squelch (derived from |sinc(Δf · 16 ms)|²), so B is never keyed, the filter makes no follow step on B's account, the window-expiry return restores the set-aside width directly (not a follow step), and the tracker stays on A (it never accepted an estimate more than 0.2 Hz from A in the simulation, measured before A's next over; option 1's reject action, emptying the average and returning the NCO to the anchor, is new but has nothing to act on here). So `MatchedIgnoresAWeakStation50HzAway`, `MatchedIgnoresAStation70HzAway` and the −6 dB level of `MatchedIgnoresANeighbor100HzAwayInASilence` take the simulated path; `MatchedReturnsToTheNarrowFilterWhenNothingAnswers` too (no neighbor; its only change is that the filter reaches A's width about 4 marks later, long before the silence). *At risk, not re-simulated with the first-step bound:* the +10 dB level of `MatchedIgnoresANeighbor100HzAwayInASilence` (B keyed at about S₅₀₀ +10.5 dB through K = 24), `MatchedIgnoresStrongerNeighbor` (the neighbor at about −4.5 dB re the wanted station through K = 24 while both key; t = 13 at a simulated 0.88 leaves little headroom: at a true rate of 0.75, P(X < 13) ≈ 0.10), `MatchedDecodesAtThreeDbS500` and `MatchedFollowsSpeedChange`: the bound keeps the filter near K = 24 for about 4 more marks at 25 WPM (ln(48 ms/20 ms)/ln 1.25 = 3.9, derived), more exposure to the wide filter. **If one of these four is below its count: stop and report the measured count and the mechanism to the controller; do not change the count.** **The stated limits of option 1** (a neighbor 60–70 Hz away at A's level or stronger leaking through the boxcar; the retune delay; the 50 Hz, 0 dB runaway) are in Design decisions B; these tests do not assert them. The first marks of B are lost while the filter and ŝ settle (group H's first-word CER measures it). A single weak station pausing for 15 dits every 3 words at S₅₀₀ = −2 dB kept on average 0.47 of its marks with the 2 s window and 0.39 without (25 WPM; 0.81 and 0.73 at 12 WPM; 20 seeds, per-seed spread 0.00–0.92; final check): a small benefit.

**Speed-estimate growth bound (owner decision 2026-09-29):** while the filter follows the speed (the window holds `follow_after_marks` marks, all new since the last re-acquisition), each speed update may raise the dit estimate by at most `max_dit_growth` = ×1.25. It removes the runaway the final check found after a sudden speed change (the estimate jumped ×2 in one update, the filter outgrew the element spaces and merged marks); a real slowdown then takes ln(ratio)/ln 1.25 marks to follow, 5 marks from 35 to 12 WPM (derived). Before the filter follows, the estimate is unbounded, as in milestone 1. **The bound also applies to the filter's first follow step after an acquisition or re-acquisition (option 1, decision 3):** the decoder keeps the filter's own dit, `filter_dit_s_` (the 20 ms acquisition dit, 1.2 s / `matched.initial_wpm`, after a reset or a re-acquisition), and each speed update while following sets it to min(estimate, 1.25 × its previous value); decreases are not bounded. Before, the first step went straight to an estimate resting on up to 7 unbounded marks: in the option-1 simulation a retune in the middle of a mark made a 110 ms estimate from the mean of B's dits and dahs, K jumped from 24 to 146 samples, merged B's marks and ran to 288 (12 of 30 runs at 50 Hz, 0 dB re A). Reaching a station's width now takes ln(T/20 ms)/ln 1.25 marks after the first 8 (4 marks at 25 WPM, 6 at 18 WPM, 8 at 12 WPM; derived); meanwhile the filter is shorter than matched (less sensitive; timing unchanged). **Not simulated:** its effect on that runaway (the estimate may still be wrong when the filter reaches it; Task 14 measures group H and reports) and on the decoder-level rates below (all simulated without it); if a test fails, report it; do not change a count.

**Tests with a simulated pass rate below 100% (owner decision 2026-09-29)** run 20 fixed seeds and assert a pass count: the smallest count t with P(X < t) ≤ 0.002 for X ~ Binomial(20, simulated rate) (a wide margin, derived from the binomial). They are deterministic and still catch a regression. Tests that passed every simulated seed keep one seed.

**Files:**
- Modify: `engine/include/kz4ap/decoder.hpp` (`DecodeUpdate::freq_offset_hz`)
- Modify: `engine/include/kz4ap/classical_decoder.hpp`, `engine/src/classical_decoder.cpp`
- Test: `engine/tests/classical_decoder_test.cpp`
- Modify: `docs/signal-processing.md` (§7, §8, §8b, §9, §10)

**Interfaces:**
- Consumes: `FrequencyTracker`, `FrequencyTrackerConfig`, `FrequencyTracker::reacquire`, `FrequencyTracker::set_anchor` (Task 10); `MatchedFrontEnd`, `MatchedFrontEndConfig` (`initial_wpm`), `FrontEndSample`, `MatchedFrontEnd::reacquire`, `set_dit`, `length` (Task 11).
- Produces:
  - `DecodeUpdate` gains `std::optional<double> freq_offset_hz;` — the decoder's estimate of the station's offset from its channel center, Hz; set only by decoders that track frequency.
  - `Decoder` gains `virtual void set_frequency_anchor_hz(double offset_hz) {}` — where the detector says the station is, Hz from the channel center; the default ignores it. `ClassicalDecoder` overrides it: in Matched mode it calls `FrequencyTracker::set_anchor`; in Envelope mode it does nothing (bit-identical).
  - `enum class FrontEnd { Envelope, Matched };`
  - `ClassicalDecoderConfig` gains `FrontEnd front_end = FrontEnd::Envelope;` (Task 13 makes `Matched` the default), `double llr_hysteresis = 1.0;` (nats), `std::size_t follow_after_marks = 8;`, `double max_dit_growth = 1.25;` (factor per mark), `double reacquire_after_dits = 12.0;`, `double reacquire_min_s = 0.5;` (s), `double reacquire_window_s = 2.0;` (s), `MatchedFrontEndConfig matched;`, `FrequencyTrackerConfig tracker;`.
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

// Owner decision 2026-09-29: a test whose simulated pass rate is below 100% runs 20 fixed seeds
// and asserts a pass count t, the smallest with P(X < t) <= 0.002 for X ~ Binomial(20, simulated
// rate). Deterministic, and still catches a regression. `run` returns true on a pass and may
// describe a failure in `why`; seeds are first_seed, first_seed + 10, ... (each run may use
// seed, seed + 1, seed + 2).
constexpr unsigned kSeeds = 20;

struct Tally {
    int passed = 0;
    std::string failures;
};

template <class Run>
Tally count_passes(unsigned first_seed, Run&& run) {
    Tally tally;
    for (unsigned i = 0; i < kSeeds; ++i) {
        const unsigned seed = first_seed + 10 * i;
        std::string why;
        if (run(seed, why)) ++tally.passed;
        else tally.failures += "seed " + std::to_string(seed) + ": " + why + "\n";
    }
    return tally;
}

}  // namespace

TEST(ClassicalDecoder, EnvelopeModeTracksNoFrequency) {
    ClassicalDecoder d(kRate, {}, 3.0);
    const auto u = d.process(keyed_signal("E", 25, kRate, 1.0), 0.0);
    EXPECT_FALSE(u.freq_offset_hz.has_value());
    EXPECT_EQ(d.filter_length(), 0);
    EXPECT_EQ(d.frequency_offset_hz(), 3.0);
    d.set_frequency_anchor_hz(20.0);  // ignored on the Envelope path (bit-identical to milestone 1)
    EXPECT_EQ(d.frequency_offset_hz(), 3.0);
}

TEST(ClassicalDecoder, MatchedFollowsTheAnchorItIsGiven) {
    // Option 1 (owner decisions 2026-09-29): the detector decides where the station is; the engine
    // passes it as the anchor before every block. An anchor 30 Hz from the NCO (more than the
    // tracker's 12 Hz) moves the NCO there at once, so a station at 30 Hz decodes as if the decoder
    // had started there (the case of MatchedDecodesCleanSignal, derived; not separately simulated).
    ClassicalDecoder d(kRate, matched(), 0.0);
    d.set_frequency_anchor_hz(30.0);
    EXPECT_EQ(d.frequency_offset_hz(), 30.0);
    const std::string msg = "CQ TEST K1ABC";
    EXPECT_EQ(text(decode_all(d, keyed_signal(msg, 25, kRate, duration_for(msg, 25), 30.0, 1.0,
                                              sigma_for_s500(30), 43))), msg);
    EXPECT_NEAR(d.frequency_offset_hz(), 30.0, 2.0);
}

TEST(ClassicalDecoder, MatchedFilterGrowsAtMostTheBoundPerMark) {
    // Owner decision 3 of option 1 (2026-09-29): the x1.25 growth bound applies from the filter's
    // first follow step. A clean 12 WPM station (T = 100 ms): the decoder's estimate is near 100 ms
    // after 7 unbounded marks, but the filter must grow from the 20 ms acquisition dit (K = 24) by
    // at most x1.25 per mark: 24, 30, 38, 47, 59, 73, 92, 114, 120 samples (derived), each step at
    // most 1.25 K + 1.125 (both K rounded). Before this bound, K jumped 24 -> 120 in one step.
    // Deterministic apart from the noise at S500 = 30 dB; not simulated.
    const std::string msg = "PARIS PARIS PARIS";
    const double end = keying(msg, 12, 0.5).back().second;
    const auto x = keyed_signal(msg, 12, kRate, end + 0.3, 0, 1.0, sigma_for_s500(30), 44);
    ClassicalDecoder d(kRate, matched());
    std::vector<DecodedSymbol> chars;
    int k = d.filter_length();
    int steps = 0;
    for (std::size_t i = 0; i < x.size(); ++i) {
        auto u = d.process(std::span<const Sample>(x).subspan(i, 1), static_cast<double>(i) / kRate);
        chars.insert(chars.end(), u.chars.begin(), u.chars.end());
        const int now = d.filter_length();
        if (now > k) {
            ++steps;
            EXPECT_LE(now, 1.25 * k + 1.125) << "K " << k << " -> " << now << " at " << static_cast<double>(i) / kRate << " s";
        }
        k = now;
    }
    auto f = d.flush();
    chars.insert(chars.end(), f.chars.begin(), f.chars.end());
    EXPECT_GE(steps, 7);  // ln(100 ms / 20 ms) / ln 1.25 = 7.2 (derived)
    EXPECT_NEAR(k, std::lround(0.8 * 0.1 * kRate), 12);
    EXPECT_TRUE(ends_with(text(chars), "PARIS")) << text(chars);
}

TEST(ClassicalDecoder, MatchedDecodesCleanSignal) {
    ClassicalDecoder d(kRate, matched());
    const std::string msg = "CQ TEST K1ABC";
    EXPECT_EQ(text(decode_all(d, keyed_signal(msg, 25, kRate, duration_for(msg, 25), 0, 1.0,
                                              sigma_for_s500(30), 21))), msg);
}

TEST(ClassicalDecoder, MatchedDecodesAtThreeDbS500) {
    // Simulated (whole decoder, numpy seeds): "K1ABC DE W9XYZ" found in 81 of 100 (final check) and
    // 79 of 100 (2026-09-29, with the growth bound); the failures are errors in the first words
    // while the speed estimate and the filter settle. 20 seeds; at 0.79, t = 10. Not re-simulated
    // with the first-step growth bound: if below 10, stop and report (Task 12 intro).
    const std::string msg = "CQ TEST K1ABC DE W9XYZ";
    const auto tally = count_passes(22, [&](unsigned seed, std::string& why) {
        ClassicalDecoder d(kRate, matched());
        why = text(decode_all(d, keyed_signal(msg, 25, kRate, duration_for(msg, 25), 0, 1.0,
                                              sigma_for_s500(3), seed)));
        return why.find("K1ABC DE W9XYZ") != std::string::npos;
    });
    EXPECT_GE(tally.passed, 10) << tally.failures;
}

TEST(ClassicalDecoder, MatchedDecodesStrongSignal) {
    ClassicalDecoder d(kRate, matched());
    const std::string msg = "CQ TEST K1ABC";
    EXPECT_EQ(text(decode_all(d, keyed_signal(msg, 25, kRate, duration_for(msg, 25), 0, 1.0,
                                              sigma_for_s500(60), 23))), msg);
}

TEST(ClassicalDecoder, MatchedFollowsSpeedChange) {
    // K is read 0.3 s after the last mark, before the 0.5 s re-acquisition silence puts it back to
    // 24 (final check F-2). Without the growth bound, 2 of 100 simulated seeds ran away (the dit
    // estimate jumped from 41 to 81 ms in one update, K 105). With the x1.25 bound (owner decision
    // 2026-09-29), simulated over 100 numpy seeds: K = 41 +/- 5 samples (27.3 ms) and the text ends
    // in K1ABC in 98; one ended with a stray E, one garbled the last call while K was still 61.
    // 20 seeds; at 0.98, t = 17. Not re-simulated with the first-step growth bound: if below 17,
    // stop and report (Task 12 intro).
    const std::string first = "CQ CQ CQ";
    const std::string second = "TEST K1ABC K1ABC";
    const double start2 = keying(first, 20, 0.5).back().second + 7 * 1.2 / 20;
    const double end2 = keying(second, 35, start2).back().second;
    const double total = end2 + 1.5;
    const auto tally = count_passes(24, [&](unsigned seed, std::string& why) {
        const auto x = add(keyed_signal(first, 20, kRate, total, 0, 1.0, sigma_for_s500(20), seed),
                           keyed_signal(second, 35, kRate, total, 0, 1.0, 0.0, seed + 1, start2));
        ClassicalDecoder d(kRate, matched());
        const auto split = static_cast<std::size_t>((end2 + 0.3) * kRate);
        std::vector<DecodedSymbol> chars;
        auto a = d.process(std::span<const Sample>(x).subspan(0, split), 0.0);
        chars.insert(chars.end(), a.chars.begin(), a.chars.end());
        const int k = d.filter_length();
        auto b = d.process(std::span<const Sample>(x).subspan(split), static_cast<double>(split) / kRate);
        chars.insert(chars.end(), b.chars.begin(), b.chars.end());
        auto f = d.flush();
        chars.insert(chars.end(), f.chars.begin(), f.chars.end());
        why = text(chars) + " (K " + std::to_string(k) + ")";
        return ends_with(text(chars), "K1ABC") && std::abs(k - std::lround(0.8 * 1.2 / 35 * kRate)) <= 5;
    });
    EXPECT_GE(tally.passed, 17) << tally.failures;
}

TEST(ClassicalDecoder, MatchedTracksTheResidualOffset) {
    // Spec 5.2 target: within 2 Hz, starting 11 Hz off, as bin rounding alone can leave a station.
    // The station is 11 Hz from the anchor (0 Hz), 1 Hz inside the tracker's +/-12 Hz: a noisy early
    // estimate beyond 12 Hz is rejected (average emptied, NCO back to 0 Hz) and the average rebuilds;
    // such transient rejections are possible and harmless to the final assertion.
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
    // 10 dB stronger (re the wanted station's key-down power), at another speed, keying at the
    // same time. Simulated (100 numpy seeds): f-hat within 2 Hz in all 100; K1ABC decoded in 98
    // (final check's port) and 88 (the 2026-09-29 port, the same with the 35 Hz pull-in and without
    // the growth bound, so the difference is between the ports). 20 seeds; at 0.88, t = 13. Not
    // re-simulated with the first-step growth bound: if below 13, stop and report (Task 12 intro).
    const std::string msg = "CQ TEST K1ABC K1ABC K1ABC";
    const double total = duration_for(msg, 25);
    const auto tally = count_passes(30, [&](unsigned seed, std::string& why) {
        const auto x = add(keyed_signal(msg, 25, kRate, total, 0.0, 1.0, sigma_for_s500(15), seed),
                           keyed_signal("TU W9XYZ 5NN TU W9XYZ 5NN TU W9XYZ", 30, kRate, total, 100.0,
                                        std::sqrt(10.0), 0.0, seed + 1, 0.3));
        ClassicalDecoder d(kRate, matched(), 0.0);
        why = text(decode_all(d, x));
        why += " (f " + std::to_string(d.frequency_offset_hz()) + " Hz)";
        return std::abs(d.frequency_offset_hz()) <= 2.0 && why.find("K1ABC") != std::string::npos;
    });
    EXPECT_GE(tally.passed, 13) << tally.failures;
}

namespace {

// A (25 WPM, 0 Hz, S500 = 15 dB), 1 s of silence, B (18 WPM, offset_hz away, relative_db re A's
// key-down power), 1 s of silence, A again. Returns the text and f-hat just before A's second over.
// There is no detector here, so the tracker's anchor stays on A (0 Hz) throughout.
struct Turnover {
    std::string text;
    double f_before_second_over;
};

Turnover turnover(double offset_hz, double relative_db, unsigned seed) {
    const std::string a = "PARIS PARIS PARIS PARIS";
    const std::string b = "DE W9XYZ PARIS PARIS PARIS";
    const double b0 = keying(a, 25, 0.5).back().second + 1.0;
    const double a0 = keying(b, 18, b0).back().second + 1.0;
    const double total = keying(a, 25, a0).back().second + 1.5;
    auto x = add(keyed_signal(a, 25, kRate, total, 0.0, 1.0, sigma_for_s500(15), seed),
                 keyed_signal(b, 18, kRate, total, offset_hz, std::pow(10.0, relative_db / 20.0), 0.0, seed + 1, b0));
    x = add(x, keyed_signal(a, 25, kRate, total, 0.0, 1.0, 0.0, seed + 2, a0));
    ClassicalDecoder d(kRate, matched());
    const auto split = static_cast<std::size_t>((a0 - 0.1) * kRate);
    std::vector<DecodedSymbol> chars;
    auto first = d.process(std::span<const Sample>(x).subspan(0, split), 0.0);
    chars.insert(chars.end(), first.chars.begin(), first.chars.end());
    const double f_before = d.frequency_offset_hz();
    auto rest = d.process(std::span<const Sample>(x).subspan(split), static_cast<double>(split) / kRate);
    chars.insert(chars.end(), rest.chars.begin(), rest.chars.end());
    auto f = d.flush();
    chars.insert(chars.end(), f.chars.begin(), f.chars.end());
    return {text(chars), f_before};
}

}  // namespace

TEST(ClassicalDecoder, MatchedIgnoresAWeakStation50HzAway) {
    // B 50 Hz away, 10 dB weaker (re A's key-down power), is 12.6 dB further down at the 16 ms
    // acquisition width (|sinc(50 Hz * 16 ms)|^2, derived), so it is not keyed and leaves A alone.
    // Simulated with the first design (100 numpy seeds): f-hat within 0.2 Hz of A and A's second
    // over intact in 99; option 1's tracker (+/-12 Hz around an anchor held on A) accepts a subset of
    // what that tracker accepted, so the rate carries over (argued; Task 12 intro). Whether B's
    // station gets the channel is the detector's decision (Task 13). 20 seeds; t = 18.
    const auto tally = count_passes(36, [](unsigned seed, std::string& why) {
        const auto r = turnover(50.0, -10.0, seed);
        why = r.text + " (f " + std::to_string(r.f_before_second_over) + " Hz)";
        return std::abs(r.f_before_second_over) <= 2.0 && ends_with(r.text, "PARIS PARIS PARIS PARIS");
    });
    EXPECT_GE(tally.passed, 18) << tally.failures;
}

TEST(ClassicalDecoder, MatchedIgnoresAStation70HzAway) {
    // Beyond D = 47 Hz the answering station has its own track and this channel, its anchor held on
    // A, ignores it. B 70 Hz away, 6 dB weaker (re A's key-down power): simulated with the first
    // design over 100 numpy seeds, f-hat within 0.2 Hz of A and A's second over intact in 99; the rate
    // carries over to option 1 (argued; Task 12 intro). (At A's level or stronger, 60-70 Hz away, B
    // leaks through the boxcar's sidelobe and corrupts the speed estimate: stated limit (a), not
    // asserted.) 20 seeds; t = 18.
    const auto tally = count_passes(37, [](unsigned seed, std::string& why) {
        const auto r = turnover(70.0, -6.0, seed);
        why = r.text + " (f " + std::to_string(r.f_before_second_over) + " Hz)";
        return std::abs(r.f_before_second_over) <= 2.0 && ends_with(r.text, "PARIS PARIS PARIS PARIS");
    });
    EXPECT_GE(tally.passed, 18) << tally.failures;
}

TEST(ClassicalDecoder, MatchedIgnoresANeighbor100HzAwayInASilence) {
    // Re-review finding I-4: at equal level the earlier design let B, aliased to -87.5 Hz, pull
    // f-hat to the -75 Hz clamp during A's silence. Simulated with the first design (2026-09-29, 100
    // numpy seeds per level; the rates carry over to option 1, argued in the Task 12 intro): f-hat
    // within 0.2 Hz of A; A's second over intact in 99 of 100 at -6 dB and 100 of 100
    // at +10 dB re A's key-down power (the +10 dB level was not re-simulated with the first-step
    // growth bound: if below its count, stop and report; Task 12 intro). At A's level B sits at the acquisition squelch's edge and cost
    // A's second over in 13 of 60 seeds (final check): a stated failure, not asserted. 20 seeds per
    // level; t = 18.
    for (const double relative_db : {-6.0, 10.0}) {
        const auto tally = count_passes(39, [&](unsigned seed, std::string& why) {
            const auto r = turnover(100.0, relative_db, seed);
            why = r.text + " (f " + std::to_string(r.f_before_second_over) + " Hz)";
            return std::abs(r.f_before_second_over) <= 2.0 && ends_with(r.text, "PARIS PARIS PARIS PARIS");
        });
        EXPECT_GE(tally.passed, 18) << relative_db << " dB:\n" << tally.failures;
    }
}

TEST(ClassicalDecoder, MatchedReturnsToTheNarrowFilterWhenNothingAnswers) {
    // After a re-acquisition finds nothing within 2 s, the filter goes back to the station's width
    // (and the speed window comes back). Simulated: K back at 58 +/- 5 samples (38.7 ms) in 196 of
    // 200 (final check) and 199 of 200 (2026-09-29). In the others noise was keyed as a stray E right
    // after the re-acquisition (s-hat restarts from its first few noise samples), which counts as
    // an answer: postponed to the backlog, about 1% of silences. 20 seeds; at 0.98, t = 17.
    const std::string msg = "CQ TEST K1ABC CQ TEST K1ABC";
    const double end = keying(msg, 25, 0.5).back().second;
    const auto tally = count_passes(40, [&](unsigned seed, std::string& why) {
        ClassicalDecoder d(kRate, matched());
        decode_all(d, keyed_signal(msg, 25, kRate, end + 4.0, 0, 1.0, sigma_for_s500(20), seed));
        why = "K " + std::to_string(d.filter_length());
        return std::abs(d.filter_length() - std::lround(0.8 * 1.2 / 25 * kRate)) <= 5;
    });
    EXPECT_GE(tally.passed, 17) << tally.failures;
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
    c = matched();
    c.max_dit_growth = 0.9;  // must be at least 1
    EXPECT_THROW(ClassicalDecoder d(kRate, c), std::invalid_argument);
    c = matched();
    c.reacquire_after_dits = 0.0;
    EXPECT_THROW(ClassicalDecoder d(kRate, c), std::invalid_argument);
    c = matched();
    c.reacquire_min_s = -1.0;
    EXPECT_THROW(ClassicalDecoder d(kRate, c), std::invalid_argument);
    c = matched();
    c.reacquire_window_s = -1.0;
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

and to `class Decoder`, after `reset()`:

```cpp
    // Where the signal detector says the station is, Hz from the channel's center (option 1,
    // owner decisions 2026-09-29: the detector decides which station a channel follows). The
    // engine calls it before every block. Decoders that do not track frequency ignore it.
    virtual void set_frequency_anchor_hz(double /*offset_hz*/) {}
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
    double max_dit_growth = 1.25;         // Matched, while the filter follows: the dit estimate grows at most this factor per mark
    double reacquire_after_dits = 12.0;   // Matched: re-acquire after the key has been up this many dits...
    double reacquire_min_s = 0.5;         // ...and at least this long, s
    double reacquire_window_s = 2.0;      // Matched: if nothing is keyed this long after, back to the narrow filter, s
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
    // Matched: the tracker fine-tunes within +/- tracker.fine_tune_hz of this (Task 10); Envelope: ignored.
    void set_frequency_anchor_hz(double offset_hz) override;
```

and add private members (after `float confidence_ = 0;`):

```cpp
    void step_matched(Sample y, double t, DecodeUpdate& out);
    void check_gaps(double t, DecodeUpdate& out);

    double initial_offset_hz_ = 0;
    std::optional<FrequencyTracker> tracker_;     // Matched only
    std::optional<MatchedFrontEnd> front_end_;    // Matched only
    double last_key_t_ = 0;                       // Matched: the last time the key was down, s
    bool heard_since_reacquire_ = false;          // Matched: a key-down since the last re-acquisition
    std::size_t marks_since_reacquire_ = 0;       // Matched: marks counted for speed since then
    double reacquire_until_ = -1;                 // Matched: end of the re-acquisition window, s (-1: none)
    bool was_following_ = false;                  // Matched: the filter followed the speed before it
    std::deque<double> set_aside_marks_;          // Matched: the speed window before it, back if nothing answers
    double filter_dit_s_ = 0.02;                  // Matched: the dit the matched filter is set to, s (grows at most
                                                  // max_dit_growth per mark, from the acquisition dit)
    double set_aside_filter_dit_s_ = 0.02;        // Matched: the filter's dit before the re-acquisition, s
```

In `engine/src/classical_decoder.cpp`:

- extend `validated_rate`'s condition with `|| !(c.llr_hysteresis >= 0) || c.follow_after_marks < 2 || !(c.max_dit_growth >= 1) || !(c.reacquire_after_dits > 0) || !(c.reacquire_min_s >= 0) || !(c.reacquire_window_s >= 0)`;
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
    last_key_t_ = 0;
    heard_since_reacquire_ = false;
    marks_since_reacquire_ = 0;
    reacquire_until_ = -1;
    was_following_ = false;
    set_aside_marks_.clear();
    filter_dit_s_ = 1.2 / config_.matched.initial_wpm;  // the acquisition dit, 20 ms at 60 WPM
    set_aside_filter_dit_s_ = filter_dit_s_;
```

- in `key_up()`, after `recent_marks_.push_back(duration);` add `++marks_since_reacquire_;`

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
        heard_since_reacquire_ = true;
    } else if (key_ && (!f.signal || f.log_odds < -h)) {
        key_up(t);
    }
    if (key_) last_key_t_ = t;
    // A long silence may be a turnover to another station (at another level, or at another
    // frequency, which the detector will report through the anchor): widen the filter, restart the
    // amplitude and frequency averages, and start a new speed window, so the next station's marks
    // are not mixed with this one's (heuristic).
    if (!key_ && heard_since_reacquire_ &&
        t - last_key_t_ > std::max(config_.reacquire_min_s, config_.reacquire_after_dits * dit_s_)) {
        was_following_ = marks_since_reacquire_ >= config_.follow_after_marks;
        front_end_->reacquire();
        tracker_->reacquire();
        heard_since_reacquire_ = false;
        marks_since_reacquire_ = 0;
        reacquire_until_ = t + config_.reacquire_window_s;
        set_aside_marks_ = std::move(recent_marks_);
        recent_marks_.clear();
        set_aside_filter_dit_s_ = filter_dit_s_;
        filter_dit_s_ = 1.2 / config_.matched.initial_wpm;  // the filter is back at the acquisition width
    }
    // Nothing keyed within the window: the same station is probably pausing (or none is there).
    // Bring back its speed window, and the narrow filter it had, which is more sensitive than the
    // acquisition width.
    if (reacquire_until_ >= 0 && t > reacquire_until_) {
        reacquire_until_ = -1;
        if (!heard_since_reacquire_) {
            recent_marks_ = std::move(set_aside_marks_);
            if (was_following_) {
                filter_dit_s_ = set_aside_filter_dit_s_;  // the width it had (a return, not a follow step)
                front_end_->set_dit(filter_dit_s_);
                marks_since_reacquire_ = config_.follow_after_marks;
            }
        }
        set_aside_marks_.clear();
    }
    check_gaps(t, out);
}

double ClassicalDecoder::frequency_offset_hz() const { return tracker_ ? tracker_->offset_hz() : initial_offset_hz_; }

int ClassicalDecoder::filter_length() const { return front_end_ ? front_end_->length() : 0; }

void ClassicalDecoder::set_frequency_anchor_hz(double offset_hz) {
    if (tracker_) tracker_->set_anchor(offset_hz);  // Envelope: nothing changes (bit-identical)
}
```

- in `update_speed()`, just before `dit_s_ = std::clamp(dit, ...)`, insert

```cpp
    // Matched: while the filter follows the speed, the dit estimate may grow by at most
    // max_dit_growth per mark (owner decision 2026-09-29). A jump of x2 in one update, while the
    // window holds two speeds, made the filter outgrow the element spaces, merge marks and run away
    // (final check F-2); a real slowdown now takes ln(ratio) / ln(max_dit_growth) marks to follow.
    const bool following = front_end_ && recent_marks_.size() >= config_.follow_after_marks &&
                           marks_since_reacquire_ >= config_.follow_after_marks;
    if (following) dit = std::min(dit, config_.max_dit_growth * dit_s_);
```

  (In Envelope mode `front_end_` is empty, so the milestone-1 estimate is unchanged.) At the end of `update_speed()` add

```cpp
    // The matched filter follows the speed once the estimate rests on enough marks, and
    // after a re-acquisition only once enough of them are new. Its own dit grows at most
    // max_dit_growth per mark, from its first follow step on (owner decision 3 of option 1,
    // 2026-09-29): the estimate may rest on up to 7 unbounded marks, and a jump from the 20 ms
    // acquisition dit straight to a wrong 110 ms estimate made the filter outgrow the element
    // spaces and run away in simulation. Decreases are not bounded.
    if (front_end_ && recent_marks_.size() >= config_.follow_after_marks &&
        marks_since_reacquire_ >= config_.follow_after_marks) {
        filter_dit_s_ = std::min(dit_s_, config_.max_dit_growth * filter_dit_s_);
        front_end_->set_dit(filter_dit_s_);
    }
```

- [ ] **Step 5: Build and run the tests**

```powershell
cmake --build --preset windows
ctest --preset windows -R ClassicalDecoder
```
Expected: every `ClassicalDecoder.*` test passes, the milestone-1 ones unchanged. The Matched tests are the Review Focus tests for strong signals, speed changes, pauses, closely spaced stations and turnovers; if one fails, debug it (superpowers:systematic-debugging). If the cause is a code bug, fix it. If the cause is a parameter value, **stop and report the measurement to the owner; do not change the value, the seed count or the pass count**: parameter tuning is deferred to the benchmark results (owner, 2026-09-27 and 2026-09-29). Seven tests run 20 seeds and assert a pass count below 20, from simulated rates below 100% (owner decision 2026-09-29): `MatchedDecodesAtThreeDbS500` (79–81 of 100; at least 10 of 20), `MatchedFollowsSpeedChange` (98 of 100; 17), `MatchedIgnoresStrongerNeighbor` (88–98 of 100; 13), `MatchedIgnoresAWeakStation50HzAway` (99 of 100; 18), `MatchedIgnoresAStation70HzAway` (99 of 100; 18), `MatchedIgnoresANeighbor100HzAwayInASilence` (99 and 100 of 100; 18 per level) and `MatchedReturnsToTheNarrowFilterWhenNothingAnswers` (196–199 of 200; 17). The simulations used numpy seeds, so the rates, not particular seeds, carry over to the C++ tests' mt19937 seeds. The last four were simulated with the first design's tracker, and none of the seven with the first-step growth bound (option 1, decision 3); the Task 12 intro derives which rates carry over. **Four are at risk (`MatchedDecodesAtThreeDbS500`, `MatchedFollowsSpeedChange`, `MatchedIgnoresStrongerNeighbor`, and the +10 dB level of `MatchedIgnoresANeighbor100HzAwayInASilence`): if one is below its count, stop and report the measured count and the mechanism to the controller; do not change the count.** `MatchedFollowsTheAnchorItIsGiven` and `MatchedFilterGrowsAtMostTheBoundPerMark` are single-seed checks of mechanisms at S₅₀₀ = 30 dB whose expected values are derived, not simulated. If one of them fails, print its failures (the test does), compare them with the mechanism its comment names, and report to the owner; do not change the seeds, levels, tolerances or counts.

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
(**heuristic** hysteresis), and never goes down while a < a_min(K) (section
8b, "Squelch"). Steps 6–10
(glitches, elements, gaps, symbols, speed) are the same in both. In
`Matched` mode the filter follows the speed estimate once its window holds
8 marks (**heuristic**), and every decode result reports the tracker's
frequency estimate. While the filter follows, each speed
update may raise the dit estimate by at most ×1.25 (**heuristic**, owner
decision 2026-09-29): it stops a runaway after a sudden speed change, and a
real slowdown takes ln(ratio)/ln 1.25 marks to follow (5 marks from 35 to
12 WPM, derived). The same bound holds for the filter's own dit from its
first follow step after an acquisition or re-acquisition (owner decision
2026-09-29, option 1): it grows from the 20 ms acquisition dit by at most
×1.25 per mark until it reaches the estimate (4 marks to reach 25 WPM,
8 to reach 12 WPM, derived), because the estimate at that step may rest on
up to 7 unbounded marks (in simulation a truncated first mark gave a
110 ms estimate, the filter jumped from K = 24 to 146 samples and ran
away; the bound's effect on that case is not yet measured).
**Re-acquisition (heuristic):** once the key has been
up for max(0.5 s, 12 dits), the Matched decoder assumes the next station
may be a different one (a QSO turnover): section 8b's filter returns to
the 60 WPM width and its amplitude estimate restarts, section 7's
frequency average restarts from the last estimate, the speed window of
step 10 is set aside and a new one starts (so the next station's marks are
not mixed with this one's), and the filter follows the speed again after 8
new marks; if nothing is keyed within 2 s, the set-aside speed window comes
back and the filter returns to the width it had (a weak station that pauses
is then not held at the acquisition floor). The decoder does not decide
which station it follows: its frequency tracker fine-tunes within ±12 Hz
of the anchor its caller gives it (`Decoder::set_frequency_anchor_hz`;
section 7), so on its own it follows only a station within ±12 Hz of that
anchor; a station farther away is followed only when the caller moves the
anchor to it.
Simulated at decoder level (an earlier tracker design whose estimate
never left 0.2 Hz of the first station in these runs; levels in dB re
the first station's key-down power; 100 seeds each): a station answering
50 Hz away at −10 dB (not keyed), 70 Hz away at −6 dB, or 100 Hz away at
−6 or +10 dB left the first station's next over intact in 99–100 of 100.
**Limits:** a neighbor 60–70 Hz away at the first station's level or
stronger leaks through the filter's first sidelobe (−18.7 dB relative to a
centered station at 60 Hz and K = 58, derived) and can be keyed in
fragments that corrupt the speed estimate (simulated). After a silence in noise alone, noise was keyed as a
stray character in about 1% of cases (4 of 400), because ŝ restarts from
its first few noise samples.
```

In section 7, at the end of "Frequency re-centering (Matched front end only)", add:

```markdown
- **Re-acquisition (heuristic):** after a silence (section 8) the decoder
  calls `reacquire()`: the average starts afresh (weight 0) from the last
  f̂, so the next station, if it is within ±12 Hz of the anchor, is found
  within about 0.5 s of key-down weight (the average needs weight 0.6);
  one farther away is reached when the detector moves the anchor.
```

Section 8b and the section-7 subsection "Frequency re-centering" were written by Tasks 10 and 11 as components not yet wired into the decoder (doc-truth rule); this commit wires them in, so update them to the wired-in description:

- In section 7, "Frequency re-centering", replace the opening paragraph ("Not yet wired into the decoder: … tested on its own.") with: "Used only when the decoder's front end is `Matched` (section 8b); the Envelope pipeline does not re-center. `FrequencyTracker` (frequency_tracker.cpp) runs per station at r = 1500 samples/s inside the classical decoder, on the channel stream, ahead of section 8b's filter. The decoder passes on the anchor its caller gives it (`Decoder::set_frequency_anchor_hz`); nothing in the engine calls it yet (milestone 2, part 1, Task 13 does)."
- In section 8b, replace the opening paragraph ("Not yet wired into the decoder: … tested on its own.") with: "Used when the classical decoder's front end is `Matched` (`ClassicalDecoderConfig::front_end`; section 8, "Two front ends"). `MatchedFrontEnd` (matched_front_end.cpp) runs per station at r = 1500 samples/s inside the decoder, on the re-centered stream u[n] (section 7, "Frequency re-centering"), before any envelope is taken."
- In section 8b, **Following speed**, replace the parenthesis "(when the speed estimate is trusted enough to pass is the caller's choice; no caller exists yet)" with "; the Matched decoder passes it once its speed window holds 8 marks (8 new ones after a re-acquisition), the filter's dit growing at most ×1.25 per mark from the 20 ms acquisition dit (section 8, "Two front ends")".
- In section 8b, **Correlated samples**, replace "A decoder that keys from g sample by sample does not sum, and can ignore the weight." with "The classical decoder keys from g sample by sample and does not sum, so it ignores the weight."
- Read the whole of sections 7 ("Frequency re-centering") and 8b afterwards and correct any other sentence that still says the component is unused (doc-truth rule); keep the symbols as section 0 defines them: σ² is the input's noise power per complex sample, and v's noise is σ_v (noise RMS per real component of v; commit 40b3352), estimated as σ̂_v; write σ̂_v² for the front end's noise estimate.

In section 8b, after the **Following speed** bullet, add:

```markdown
- **Re-acquisition (heuristic):** after a silence (section 8) the decoder
  calls `reacquire()`: K returns to 24 (60 WPM, main lobe ±62.5 Hz), ŝ²
  and its weight return to 0, and σ̂_v² is kept (rescaled); if nothing is
  keyed within 2 s the width returns to what it was. Without it the
  filter stays at the last station's width (±21–26 Hz main lobe, nulls
  near 25 and 50 Hz) and ŝ at its level, and a station answering there,
  or 6 dB weaker (re the first station's key-down power), is never keyed
  (simulated). After it, a station Δf away loses |sinc(Δf·24/1500 s)|² at
  K = 24 (−2.4 dB relative to a centered station at 25 Hz, derived) and
  must pass the acquisition squelch, so it needs about S₅₀₀ ≥ 0 dB at
  25 Hz.
```

In section 9, add a row: `| Matched filter group delay (Matched only) | (K − 1)/2 samples: 19 ms at 25 WPM |`.

In section 10, add rows:

```markdown
| Front end | Envelope (default) or Matched | `ClassicalDecoderConfig::front_end` | — |
| LLR keying hysteresis (Matched) | g > +1 nat down, g < −1 nat up | `ClassicalDecoderConfig::llr_hysteresis` | heuristic |
| Filter follows speed after (Matched) | 8 marks in the speed window (8 new ones after a re-acquisition) | `ClassicalDecoderConfig::follow_after_marks` | heuristic |
| Dit-estimate growth bound (Matched) | at most ×1.25 per mark while the filter follows the speed; the filter's own dit also grows at most ×1.25 per mark from its first follow step (from the 20 ms acquisition dit) | `ClassicalDecoderConfig::max_dit_growth` | heuristic (owner decisions 2026-09-29) |
| Re-acquisition (Matched) | after max(0.5 s, 12 dits) of key-up: filter back to 60 WPM, ŝ, the frequency average and the speed window restart; the old speed window and the narrow filter come back if nothing is keyed within 2 s | `ClassicalDecoderConfig::reacquire_after_dits`, `reacquire_min_s`, `reacquire_window_s` | heuristic |
```

- [ ] **Step 8: Commit on the `milestone-2` branch**

```powershell
git add engine/include/kz4ap/decoder.hpp engine/include/kz4ap/classical_decoder.hpp engine/src/classical_decoder.cpp engine/tests/classical_decoder_test.cpp docs/signal-processing.md
```
```powershell
git commit -m "Add a Matched front-end mode to the classical decoder, keying on LLRs, with a frequency anchor"
```

---

### Task 13: Engine — the detector decides the station (option 1), re-centering from the detector, refined frequency, the Matched default, `--front-end`

Wires the Matched mode into the pipeline and makes it the default (owner decision 2026-09-29), with the channel design the owner chose the same day, **option 1** (Design decisions B): **the detector alone decides which station a channel follows.** Each detector track follows its own spectral peak within the channel distance D = 47 Hz of its current frequency (a peak beyond D is a separate track), and before every channel block the engine gives the channel's decoder the detector's current frequency for the track as the tracker's anchor (Task 10: the tracker fine-tunes within ±12 Hz of it, and jumps to it when it moves farther). So the channel retunes when the detector's track moves: slowly with drift, or at once when a turnover within D moves the peak to the answering station. The decoder of a new track starts its NCO at the detector's residual (track frequency minus channel center). In Matched mode the engine publishes the tracked frequency (channel center plus the decoder's estimate) in every `DecodedTextEvent`. **No channel merging** (owner decision 4 of option 1: it never fired in 1080 simulated runs). The detector's neighborhoods that milestone 1 counted in bins (the peak neighborhood, the track-level neighborhood, the candidate step) are restated in Hz and converted to bins at the point of use (owner's physical-units rule). The milestone-1 detector rules stay selectable (`Attribution::Bins`: the track frequency fixed at birth, the 3-bin rule), and `with_envelope_path` (bench: `--front-end envelope`) reproduces milestone 1 bit for bit, which CI keeps pinning. `kz4ap-bench --front-end` selects the path; `matched` is the default.

Every implementer of this task must be told the two standing rules (Global Constraints): **physical units** (parameters in Hz, s, FS, dB with a named reference, never bins or samples; convert only at the point of use; config fields named for their unit), and **`docs/signal-processing.md` is updated in the same commit** as any signal-processing change, including its parameter table and whether each choice is derived, measured or heuristic. Git: one plain git command per call, no attribution lines, never amend.

**Files:**
- Modify: `engine/include/kz4ap/signal_detector.hpp`, `engine/src/signal_detector.cpp` (`Attribution`, the neighborhoods in Hz, tracks following their own peak)
- Modify: `engine/include/kz4ap/engine.hpp`, `engine/src/engine.cpp` (channel distance, the anchor from the detector, `with_envelope_path`)
- Modify: `engine/include/kz4ap/classical_decoder.hpp` (default `FrontEnd::Matched`)
- Modify: `engine/tests/test_signals.hpp` (carrier drift), `engine/tests/signal_detector_test.cpp`, `engine/tests/engine_test.cpp`, `engine/tests/classical_decoder_test.cpp` (milestone-1 tests pinned to the Envelope path)
- Modify: `bench/src/main.cpp`, `bench/smoke.sh` (pinned check on `--front-end envelope`), `README.md`
- Modify: `docs/signal-processing.md` (§6, §7, §8, §8b, §10)

**Interfaces:**
- Consumes: `EngineConfig::decoder.front_end`, `ClassicalDecoder(double, ClassicalDecoderConfig, double initial_offset_hz)`, `DecodeUpdate::freq_offset_hz`, `Decoder::set_frequency_anchor_hz` (Task 12); `FrequencyTrackerConfig::fine_tune_hz` (Task 10; the engine leaves it at 12 Hz); `Engine::Channel::bin`, `open_channel`, oracle mode (Task 8).
- Produces:
  - `enum class Attribution { Bins, Distance };` and `DetectorConfig` gains `Attribution attribution = Attribution::Distance;`, `double attribution_distance_hz = 47.0;`, `double peak_radius_hz = 47.0;`, `double level_radius_hz = 23.0;`, `double candidate_step_hz = 23.0;` (Hz; `min_separation_bins` stays, used only by `Attribution::Bins`). At the engine's 23.4375 Hz bins these convert (`std::lround(hz / bin_hz)`) to 2, 1 and 1 bins, milestone 1's values, so the Envelope path stays bit-identical. With `Attribution::Distance` each track follows its own peak (below) and a new peak within `attribution_distance_hz` of a track's current frequency belongs to it; with `Attribution::Bins` the milestone-1 rules apply unchanged.
  - Detector behavior with `Attribution::Distance` (option 1): every frame, before the level refresh, each track moves to the strongest bin that is a peak by the birth rule (the maximum within ±`peak_radius_hz`, ties to the lower bin), stands at least `threshold_db − hysteresis_db` (3 dB) above the floor, and whose parabolically interpolated frequency is within `attribution_distance_hz` of the track's current frequency; its `bin` and `freq_hz` become that peak's. With no such peak it holds.
  - `EngineConfig` gains `double channel_distance_hz = 47.0;` (D). The engine sets `detector.attribution_distance_hz = D` and throws `std::invalid_argument` for D ≤ 0.
  - `EngineConfig with_envelope_path(EngineConfig config);` — the milestone-1 pipeline: `FrontEnd::Envelope`, `Attribution::Bins`.
  - `ClassicalDecoderConfig::front_end` defaults to `FrontEnd::Matched`.
  - Bit-identity of the Envelope path through the Hz neighborhoods (review finding M9): lround(47 Hz / b) = 2 and lround(23 Hz / b) = 1 for bin widths b from 18.8 to 31.3 Hz, which covers every usual rate (8, 11.025, 32, 44.1, 48, 96, 192, 768 kHz give 20–31.25 Hz bins with `choose_fft_size`); at rates whose bins are wider than 31.3 Hz (for example 33–40.9 kHz) the peak neighborhood would round to 1 bin. The engine rejects nothing here; state this in `DetectorConfig`'s comment.
  - Engine behavior: decoders are created with `initial_offset_hz = track.freq_hz − bin_to_hz(bin)`; after each detector frame (Matched, outside oracle mode) each channel records the detector's current frequency for its track; before each channel block the engine calls `decoder->set_frequency_anchor_hz(anchor − bin_to_hz(bin))`, where the anchor is the detector's frequency, or in oracle mode (detector bypassed) the exact labeled frequency the oracle channel was opened for (`oracle_frequencies_hz[i]`, not the bin center; the NCO still starts at the bin center, 0 Hz, so the tracker must find the residual itself); when an update carries `freq_offset_hz`, the channel's published frequency becomes `bin_to_hz(bin) + *freq_offset_hz`, which `DecodedTextEvent::freq_hz` and the `Died` event then report. The detector is not retuned to the published frequency (it follows its own peak), and channels are never merged.
  - `kz4ap::test::keyed_signal(..., double start_s = 0.5, double drift_hz_per_s = 0.0)`.
  - `kz4ap-bench --front-end envelope|matched` (`baseline` is accepted as another name for `envelope`, for Task 9's suites; default `matched`); `envelope` applies `with_envelope_path`. JSON output gains `"front_end"` (`"envelope"` or `"matched"`).

- [ ] **Step 1: Write the failing tests**

In `engine/tests/test_signals.hpp`, give `keyed_signal` a last parameter `double drift_hz_per_s = 0.0` and replace the line computing `ph` with:

```cpp
        const double t_i = static_cast<double>(i) / rate;
        double ph = 2 * std::numbers::pi * freq_hz * static_cast<double>(i) / rate;
        if (drift_hz_per_s != 0) ph += std::numbers::pi * drift_hz_per_s * t_i * t_i;  // frequency freq_hz + drift * t
```

(The unchanged first term keeps every existing test signal bit-identical.)

**Pin the milestone-1 tests to the milestone-1 path** (they test it, and the defaults change in this task):
- In `engine/tests/signal_detector_test.cpp`, `config()` gains `c.attribution = Attribution::Bins; c.peak_radius_hz = 200.0; c.level_radius_hz = 100.0; c.candidate_step_hz = 100.0;` — milestone 1's ±2-bin, ±1-bin and 1-bin neighborhoods at this test's 100 Hz bins, now stated in Hz.
- In `engine/tests/classical_decoder_test.cpp`, add `ClassicalDecoderConfig envelope() { ClassicalDecoderConfig c; c.front_end = FrontEnd::Envelope; return c; }` to the anonymous namespace and construct every milestone-1 decoder (`ClassicalDecoder d(kRate)`, and Task 12's `EnvelopeModeTracksNoFrequency`) with `envelope()`; `RejectsInvalidConfig` builds its configs from `envelope()`.
- In `engine/tests/engine_test.cpp`, every milestone-1 test's `EngineConfig` goes through `with_envelope_path` (in `run()` and wherever a test builds its own config).

Append to `engine/tests/signal_detector_test.cpp`:

```cpp
namespace {

const Track* find_track(const std::vector<Track>& tracks, std::uint32_t id) {
    for (const auto& t : tracks)
        if (t.id == id) return &t;
    return nullptr;
}

}  // namespace

TEST(SignalDetector, TrackFollowsItsOwnPeakWithinTheDistance) {
    // Option 1 (owner decisions 2026-09-29): each track follows its own spectral peak within the
    // attribution distance of its current frequency; a peak farther away is a track of its own.
    // Here the distance is 250 Hz, 2.5 of this test's 100 Hz bins, and the peak neighborhood
    // +/-200 Hz (2 bins). Single-bin peaks on a flat floor interpolate to the bin exactly (derived).
    auto c = config();
    c.attribution = Attribution::Distance;
    c.attribution_distance_hz = 250.0;
    SignalDetector d(c);
    int i = 0;
    for (; i < 200; ++i) d.process(frame(frame_time(i), {{100, -70.0f}}));
    ASSERT_EQ(d.tracks().size(), 1u);
    const auto id = d.tracks()[0].id;
    std::size_t born = 0;
    // The station moves 200 Hz (within the distance), then another 200 Hz, 400 Hz from where the
    // track was born: the track follows it step by step and no second track is born.
    for (; i < 400; ++i) born += d.process(frame(frame_time(i), {{102, -70.0f}})).born.size();
    ASSERT_EQ(d.tracks().size(), 1u);
    EXPECT_DOUBLE_EQ(d.tracks()[0].freq_hz, (102 - kN / 2) * 100.0);
    for (; i < 600; ++i) born += d.process(frame(frame_time(i), {{104, -70.0f}})).born.size();
    EXPECT_EQ(born, 0u);
    ASSERT_EQ(d.tracks().size(), 1u);
    EXPECT_EQ(d.tracks()[0].id, id);
    EXPECT_DOUBLE_EQ(d.tracks()[0].freq_hz, (104 - kN / 2) * 100.0);
    // A station 300 Hz from the track's current frequency is a track of its own; the old track,
    // its peak gone, holds its frequency (it dies only after death_s = 2 s).
    for (; i < 800; ++i) born += d.process(frame(frame_time(i), {{107, -70.0f}})).born.size();
    EXPECT_EQ(born, 1u);
    const auto* old_track = find_track(d.tracks(), id);
    ASSERT_NE(old_track, nullptr);
    EXPECT_DOUBLE_EQ(old_track->freq_hz, (104 - kN / 2) * 100.0);
}

TEST(SignalDetector, MilestoneOneRuleKeepsTheTrackFrequencyFixed) {
    // Attribution::Bins (the Envelope path): the frequency is fixed at birth, and a peak less than
    // 3 bins from the track's bin belongs to it (milestone 1, bit for bit).
    SignalDetector d(config());
    int i = 0;
    for (; i < 200; ++i) d.process(frame(frame_time(i), {{100, -70.0f}}));
    ASSERT_EQ(d.tracks().size(), 1u);
    std::size_t born = 0;
    for (; i < 400; ++i) born += d.process(frame(frame_time(i), {{102, -70.0f}})).born.size();
    EXPECT_EQ(born, 0u);
    ASSERT_EQ(d.tracks().size(), 1u);
    EXPECT_DOUBLE_EQ(d.tracks()[0].freq_hz, (100 - kN / 2) * 100.0);
}

TEST(SignalDetector, RejectsInvalidDistances) {
    auto c = config();
    c.attribution_distance_hz = 0.0;
    EXPECT_THROW(SignalDetector d(c), std::invalid_argument);
    c = config();
    c.peak_radius_hz = 0.0;
    EXPECT_THROW(SignalDetector d(c), std::invalid_argument);
    c = config();
    c.level_radius_hz = -1.0;
    EXPECT_THROW(SignalDetector d(c), std::invalid_argument);
    c = config();
    c.candidate_step_hz = -1.0;
    EXPECT_THROW(SignalDetector d(c), std::invalid_argument);
}
```

(`<cstdint>` for `std::uint32_t` if the file does not include it yet.) Why the first test's values hold (derived from the code, not simulated): with a 0.05 s average and 5 ms frames the old bin decays and the new one rises within a few frames; the new bin is the only local maximum within ±2 bins once it is the stronger, so the track moves to it; bin 107 is 3 bins (300 Hz) from the track's bin 104, beyond the 250 Hz distance and outside its ±2-bin peak neighborhood, so it becomes a candidate and is born after the 0.5 s persistence (100 frames); the old track's level falls below the keep-alive level within about 0.3 s, and it lives 2 s more.

In `engine/tests/engine_test.cpp`, add `using kz4ap::test::keying;`, a result type that also records each track's symbols and published frequencies and the tracks that died, and a runner that takes a whole `EngineConfig` (keep the existing `Result` and `run` as they are, apart from `with_envelope_path`):

```cpp
struct TextLog {
    std::map<std::uint32_t, std::string> text;
    std::map<std::uint32_t, std::vector<DecodedSymbol>> chars;  // every decoded symbol, in order
    std::map<std::uint32_t, double> last_freq;                  // DecodedTextEvent::freq_hz of the latest event
    std::map<std::uint32_t, double> last_end_s;                 // end time of the latest symbol
    // (end time of the event's last symbol, s; DecodedTextEvent::freq_hz) for every event
    std::map<std::uint32_t, std::vector<std::pair<double, double>>> freq_at;
    std::vector<Track> born;
    std::vector<std::uint32_t> died;  // Died events before finish()
};

TextLog run_with(const EngineConfig& config, const std::vector<Sample>& x, std::size_t chunk = 65536) {
    EventBus bus;
    TextLog log;
    bus.subscribe([&](const Event& e) {
        if (const auto* t = std::get_if<TrackEvent>(&e)) {
            if (t->kind == TrackEvent::Kind::Born) log.born.push_back(t->track);
            else log.died.push_back(t->track.id);
        }
        if (const auto* d = std::get_if<DecodedTextEvent>(&e)) {
            for (const auto& c : d->chars) log.text[d->track_id] += c.text;
            auto& all = log.chars[d->track_id];
            all.insert(all.end(), d->chars.begin(), d->chars.end());
            log.last_freq[d->track_id] = d->freq_hz;
            log.last_end_s[d->track_id] = d->chars.back().end_s;
            log.freq_at[d->track_id].emplace_back(d->chars.back().end_s, d->freq_hz);
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
    c.sample_rate = sample_rate;  // the Matched path is the default
    return c;
}

constexpr double kAmplitude20dB48k = 0.0204;  // 20 dB SNR in 500 Hz against kNoiseSigma at 48 kHz

// Key-down amplitude giving s500_db (key-down power over the noise in 500 Hz, dB) against kNoiseSigma at 48 kHz.
double amplitude_48k(double s500_db) { return kNoiseSigma * std::sqrt(500.0 / 48000.0) * std::pow(10.0, s500_db / 20.0); }

void add_to(std::vector<Sample>& a, const std::vector<Sample>& b) {
    for (std::size_t i = 0; i < a.size() && i < b.size(); ++i) a[i] += b[i];
}

bool ends_with(const std::string& s, const std::string& tail) {
    return s.size() >= tail.size() && s.compare(s.size() - tail.size(), tail.size(), tail) == 0;
}

// The text of track id's symbols that start in [from_s, to_s), without leading or trailing spaces.
std::string segment(const TextLog& log, std::uint32_t id, double from_s, double to_s = 1e9) {
    std::string out;
    if (const auto it = log.chars.find(id); it != log.chars.end()) {
        for (const auto& c : it->second)
            if (c.start_s >= from_s && c.start_s < to_s) out += c.text;
    }
    const auto first = out.find_first_not_of(' ');
    if (first == std::string::npos) return {};
    return out.substr(first, out.find_last_not_of(' ') - first + 1);
}

// Levenshtein distance between a and b with word spaces removed: VE3NEA's no-space measure, the
// one the option-1 simulation used ("B decoded" = at most 3 edits against B's 12 characters, CER <= 0.3).
std::size_t nospace_edits(std::string a, std::string b) {
    std::erase(a, ' ');
    std::erase(b, ' ');
    std::vector<std::size_t> row(b.size() + 1);
    for (std::size_t j = 0; j <= b.size(); ++j) row[j] = j;
    for (std::size_t i = 1; i <= a.size(); ++i) {
        std::size_t diagonal = row[0];
        row[0] = i;
        for (std::size_t j = 1; j <= b.size(); ++j) {
            const std::size_t above = row[j];
            row[j] = std::min({row[j] + 1, row[j - 1] + 1, diagonal + (a[i - 1] == b[j - 1] ? 0 : 1)});
            diagonal = above;
        }
    }
    return row[b.size()];
}

// The published frequency of track id's latest event whose last symbol ended before t_s (NaN if none).
double freq_before(const TextLog& log, std::uint32_t id, double t_s) {
    double f = std::numeric_limits<double>::quiet_NaN();
    if (const auto it = log.freq_at.find(id); it != log.freq_at.end()) {
        for (const auto& [end_s, freq_hz] : it->second)
            if (end_s < t_s) f = freq_hz;
    }
    return f;
}

constexpr double kTurnoverA = 12010.0;  // A's carrier, Hz from the span's center (10 Hz above its bin's center)
const std::string kTurnoverB = "DE W9XYZ PARIS";

struct EngineTurnover {
    TextLog log;
    double b0;  // B's first key-down, s
    double a0;  // the first key-down of A's second over, s
};

// A two-station turnover at 48 kHz: A (kTurnoverA, S500 = 15 dB, 25 WPM, "PARIS PARIS PARIS PARIS"),
// 1 s of silence, B (offset_hz above A, relative_db re A's key-down power, 18 WPM, "DE W9XYZ PARIS",
// 9.4 s), 1 s, A again, then 1.5 s of noise: the scene of the option-1 simulation (Design decisions B;
// there A sat 7.8 Hz below its bin's center, here 10 Hz above).
EngineTurnover engine_turnover(double offset_hz, double relative_db, unsigned seed) {
    const std::string a = "PARIS PARIS PARIS PARIS";
    const double amp = amplitude_48k(15.0);
    const double b0 = keying(a, 25, 0.5).back().second + 1.0;
    const double a0 = keying(kTurnoverB, 18, b0).back().second + 1.0;
    const double total = keying(a, 25, a0).back().second + 1.5;
    auto x = keyed_signal(a, 25, 48000.0, total, kTurnoverA, amp, kNoiseSigma, seed);
    add_to(x, keyed_signal(kTurnoverB, 18, 48000.0, total, kTurnoverA + offset_hz,
                           amp * std::pow(10.0, relative_db / 20.0), 0.0, seed + 1, b0));
    add_to(x, keyed_signal(a, 25, 48000.0, total, kTurnoverA, amp, 0.0, seed + 2, a0));
    return {run_with(matched_config(48000), x), b0, a0};
}
```

(put these inside the file's anonymous namespace, after `band()`; drop `ends_with` if the file already has one; add `#include <algorithm>`, `<cmath>`, `<limits>`, `<string>`, `<utility>` if missing), and append the tests:

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
    // Slow drift only (owner, 2026-09-29): a station at 12003 Hz (3 Hz above its bin's center)
    // drifting +1 Hz/s, about 19 Hz over the 19.3 s message. The detector's track follows its own
    // peak across the bin boundary (11.7 Hz above the center), the engine moves the tracker's anchor
    // with it, and the published frequency follows. Simulated (option 1, engine level, 30 seeds; the
    // scene of Design decisions B, "Also re-checked"): one channel and the last six words intact in
    // 30 of 30; published frequency 1.49-1.63 Hz behind the carrier at the end of the last mark.
    const std::string msg = "PARIS PARIS PARIS PARIS PARIS PARIS PARIS PARIS";
    const double f0 = 12003.0;
    const auto x = keyed_signal(msg, 25, 48000.0, kz4ap::test::duration_for(msg, 25), f0, kAmplitude20dB48k,
                                kNoiseSigma, 41, 0.5, 1.0);
    const auto log = run_with(matched_config(48000), x);
    ASSERT_EQ(log.born.size(), 1u);
    const auto id = log.born[0].id;
    const double truth = f0 + 1.0 * log.last_end_s.at(id);  // the carrier's frequency when the last symbol ended
    // The simulated lag (at most 1.63 Hz) plus the spec's 2 Hz target.
    EXPECT_NEAR(log.last_freq.at(id), truth, 3.6);
    // It moved more than the tracker's own +/-12 Hz: the anchor followed the detector's peak.
    EXPECT_GT(std::abs(log.last_freq.at(id) - log.born[0].freq_hz), 12.0);
    EXPECT_TRUE(ends_with(log.text.at(id), "PARIS PARIS PARIS PARIS PARIS PARIS")) << log.text.at(id);
}

TEST(Engine, OracleMatchedFindsTheResidualFromTheBinCenter) {
    // The oracle opens the channel on the bin center (12000 Hz), 9 Hz below the station; the NCO
    // starts there, and the tracker must find the 9 Hz itself (its anchor is the label, 12009 Hz).
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

TEST(Engine, OracleAnchorsTheTrackerAtTheLabeledFrequency) {
    // Review finding I1 (controller's ruling): in oracle mode the tracker's anchor is the labeled
    // frequency, not the bin center. The label says 12011 Hz (bin center 12000 Hz); the station is
    // at 12019 Hz, 8 Hz from the label but 19 Hz from the bin center. Anchored at the label, the
    // tracker accepts 19 Hz (within +/-12 Hz of 11 Hz) and publishes about 12019 Hz; anchored at the
    // bin center it would reject every estimate and publish 12000 Hz (derived from Task 10's rule).
    const std::string msg = "CQ TEST K1ABC K1ABC";
    const auto x = keyed_signal(msg, 25, 48000.0, kz4ap::test::duration_for(msg, 25), 12019.0, kAmplitude20dB48k,
                                kNoiseSigma, 45);
    auto config = matched_config(48000);
    config.oracle_frequencies_hz = {12011.0};
    const auto log = run_with(config, x);
    ASSERT_EQ(log.born.size(), 1u);
    EXPECT_DOUBLE_EQ(log.born[0].freq_hz, 12000.0);  // the channel itself sits on the bin center
    EXPECT_NEAR(log.last_freq.at(1), 12019.0, 2.0);
}

TEST(Engine, MatchedChunkingDoesNotChangeResults) {
    const auto x = band(9.0);
    const auto a = run_with(matched_config(), x, 1000);
    const auto b = run_with(matched_config(), x, 77777);
    EXPECT_EQ(a.text, b.text);
    EXPECT_EQ(a.last_freq, b.last_freq);
}

TEST(Engine, TurnoverWithinTheChannelDistanceFollowsTheAnsweringStation) {
    // Option 1: B 25 Hz away (within D = 47 Hz), 6 dB weaker (re A's key-down power). The detector's
    // track moves to B's peak (1.80 s into B's over, median) and back; A's channel follows. Simulated
    // (engine level, 30 numpy seeds): one channel in 30, B decoded (CER <= 0.3) by A's channel in 30
    // (at least 11 of 12 characters in 30), A's next over exact in 30, B's published frequency before
    // A resumes at most 0.15 Hz off. One seed. (Replaces the decoder-level test of the first design.)
    const auto r = engine_turnover(25.0, -6.0, 51);
    ASSERT_EQ(r.log.born.size(), 1u);
    const auto id = r.log.born[0].id;
    const auto b = segment(r.log, id, r.b0, r.a0);
    EXPECT_LE(nospace_edits(b, kTurnoverB), 3u) << b;
    EXPECT_EQ(segment(r.log, id, r.a0), "PARIS PARIS PARIS PARIS") << r.log.text.at(id);
    EXPECT_NEAR(freq_before(r.log, id, r.a0), kTurnoverA + 25.0, 1.0);
}

TEST(Engine, TurnoverAt40HzFollowsTheAnsweringStation) {
    // Option 1: B 40 Hz away at A's level (0 dB re A's key-down power). Simulated (engine level, 30
    // numpy seeds): one channel in 30, B decoded by A's channel in 30 (at least 11 of 12 characters
    // in 30; the channel on B 1.46 s into B's over, median), A's next over exact in 30, B's published
    // frequency before A resumes at most 0.07 Hz off. One seed. (At -10 dB re A: B decoded in 22 of 30,
    // 3 characters lost at the start, median: stated limit (b), not asserted.)
    const auto r = engine_turnover(40.0, 0.0, 52);
    ASSERT_EQ(r.log.born.size(), 1u);
    const auto id = r.log.born[0].id;
    const auto b = segment(r.log, id, r.b0, r.a0);
    EXPECT_LE(nospace_edits(b, kTurnoverB), 3u) << b;
    EXPECT_EQ(segment(r.log, id, r.a0), "PARIS PARIS PARIS PARIS") << r.log.text.at(id);
    EXPECT_NEAR(freq_before(r.log, id, r.a0), kTurnoverA + 40.0, 1.0);
}

TEST(Engine, TurnoverBeyondTheChannelDistanceGetsItsOwnTrack) {
    // B 100 Hz away, 6 dB weaker (re A's key-down power), has its own track, and A's channel ignores
    // it. Simulated (engine level, 30 numpy seeds): two channels in 30, B decoded by its own track in
    // 30 (its first 1.2 s lost to the detector's latency), A's next over exact in 30, no duplicate
    // (A's channel matched fewer than 6 of B's 12 characters, so at least 6 edits). One seed.
    const auto r = engine_turnover(100.0, -6.0, 53);
    ASSERT_EQ(r.log.born.size(), 2u);
    const auto a_id = r.log.born[0].id;
    const auto b_id = r.log.born[1].id;
    const auto b = segment(r.log, b_id, r.b0, r.a0);
    EXPECT_LE(nospace_edits(b, kTurnoverB), 3u) << b;
    EXPECT_GT(nospace_edits(segment(r.log, a_id, r.b0, r.a0), kTurnoverB), 3u);
    EXPECT_EQ(segment(r.log, a_id, r.a0), "PARIS PARIS PARIS PARIS") << r.log.text.at(a_id);
}

TEST(Engine, StrongerStation60HzAwayKeepsItsOwnTrack) {
    // B 60 Hz away (beyond D), 6 dB stronger than A (re A's key-down power): the first design's
    // walk-and-merge case (B decoded 0 of 30 there). Simulated with option 1 (engine level, 30 numpy
    // seeds): two channels in 30, B decoded by its own track in 30, no merge, A's next over's last
    // three words intact in 30. Not asserted: A's next over exact (0 of 30; B leaks through A's
    // filter's sidelobe and corrupts A's first word: stated limit (a)). One seed.
    const auto r = engine_turnover(60.0, 6.0, 54);
    ASSERT_EQ(r.log.born.size(), 2u);
    const auto b = segment(r.log, r.log.born[1].id, r.b0, r.a0);
    EXPECT_LE(nospace_edits(b, kTurnoverB), 3u) << b;
    bool a_tail = false;
    for (const auto& t : r.log.born) a_tail = a_tail || ends_with(segment(r.log, t.id, r.a0), "PARIS PARIS PARIS");
    EXPECT_TRUE(a_tail) << r.log.text.at(r.log.born[0].id);
}

TEST(Engine, RejectsAnInvalidChannelDistance) {
    EventBus bus;
    auto c = matched_config();
    c.channel_distance_hz = 0.0;
    EXPECT_THROW(Engine(c, bus), std::invalid_argument);
}
```

The turnover and drift tests use one seed each because every simulated seed passed (30 of 30) on what they assert, measured as the simulation measured it (Design decisions B, "Option 1, simulated": "B decoded" is at most 3 no-space edits against B's 12 characters, the text of the symbols that start during B's over; "A's next over exact" is the text of the symbols that start from A's second over on). The simulation ran at 6000 samples/s with A 7.8 Hz below its bin's center; these run through the whole engine at 48 kHz with A 10 Hz above; the simulation did not include the first-step growth bound (Task 12). So the C++ results may differ in detail; if one fails, print the tracks' texts and published frequencies, compare them with Design decisions B, and report to the owner; do not change a parameter, the scene, the seed or the tolerance.

- [ ] **Step 2: Run the tests to verify they fail**

Run: `cmake --build --preset windows`
Expected: compile errors (`Attribution`, `channel_distance_hz`, `with_envelope_path` unknown).

- [ ] **Step 3: The detector: neighborhoods in Hz, attribution, tracks following their own peak**

In `engine/include/kz4ap/signal_detector.hpp`, before `struct DetectorConfig` add

```cpp
// How tracks keep their frequency and how a new spectral peak is attributed to an existing track.
enum class Attribution {
    Bins,      // milestone 1: a track's frequency is fixed at birth, and a peak less than
               // min_separation_bins from the track's bin belongs to it
    Distance,  // owner decisions 2026-09-29, option 1: each track follows its own peak within
               // attribution_distance_hz of its current frequency, and a peak whose interpolated
               // frequency is within that distance of a track's current frequency belongs to it
};
```

and add to `DetectorConfig`, after `min_separation_bins`:

```cpp
    Attribution attribution = Attribution::Distance;
    double attribution_distance_hz = 47.0;  // D, Hz (the engine sets it from EngineConfig::channel_distance_hz)
    // Neighborhoods, Hz, converted to bins at the point of use (std::lround(hz / bin width)); at 23.4 Hz
    // bins they are milestone 1's 2, 1 and 1 bins.
    double peak_radius_hz = 47.0;           // a peak must be the maximum within +/- this
    double level_radius_hz = 23.0;          // a track's level is the maximum within +/- this of its bin
    double candidate_step_hz = 23.0;        // a candidate may move this far between frames
```

(and change `min_separation_bins`'s comment to "Attribution::Bins only: peaks closer than this to a track's bin belong to it"). Add private members `bool is_peak(const std::vector<float>& avg_db, int i) const;`, `void follow_peaks(const std::vector<float>& avg_db, float floor_db);`, `int peak_bins_ = 2; int level_bins_ = 1; int step_bins_ = 1;`.

In `engine/src/signal_detector.cpp`:
- extend `ValidatedConfig`'s condition with `|| !(config.attribution_distance_hz > 0) || !(config.peak_radius_hz > 0) || !(config.level_radius_hz >= 0) || !(config.candidate_step_hz >= 0)`;
- in the constructor body, convert the neighborhoods from Hz:

```cpp
    const double bin_hz = static_cast<double>(config_.sample_rate) / config_.fft_size;
    peak_bins_ = std::max(1, static_cast<int>(std::lround(config_.peak_radius_hz / bin_hz)));
    level_bins_ = static_cast<int>(std::lround(config_.level_radius_hz / bin_hz));
    step_bins_ = static_cast<int>(std::lround(config_.candidate_step_hz / bin_hz));
```

- add the peak test (the birth rule, unchanged: ties go to the lower bin) and the following:

```cpp
bool SignalDetector::is_peak(const std::vector<float>& avg_db, int i) const {
    const int last = static_cast<int>(avg_db.size()) - 1;
    if (i < peak_bins_ || i > last - peak_bins_) return false;
    for (int d = -peak_bins_; d <= peak_bins_; ++d) {
        if (d < 0 && avg_db[i + d] >= avg_db[i]) return false;  // ties go to the lower bin
        if (d > 0 && avg_db[i + d] > avg_db[i]) return false;
    }
    return true;
}

void SignalDetector::follow_peaks(const std::vector<float>& avg_db, float floor_db) {
    // Owner decisions 2026-09-29, option 1: the detector alone decides which station a channel
    // follows. Each track moves to the strongest peak (by the birth rule) that stands at least the
    // keep-alive level above the floor and whose interpolated frequency is within D of the track's
    // current frequency; with none (the station is silent, or only a stronger neighbor's skirt is
    // there, which is not a local maximum) it holds. A peak beyond D can become a track of its own.
    const float keep_alive_db = config_.threshold_db - config_.hysteresis_db;
    const double bin_hz = static_cast<double>(config_.sample_rate) / config_.fft_size;
    const int reach = static_cast<int>(std::ceil(config_.attribution_distance_hz / bin_hz)) + 1;
    const int last = static_cast<int>(avg_db.size()) - 1;
    for (auto& t : active_) {
        int best = -1;
        double best_freq = 0;
        for (int i = std::max(0, t.bin - reach); i <= std::min(last, t.bin + reach); ++i) {
            if (avg_db[i] - floor_db < keep_alive_db || !is_peak(avg_db, i)) continue;
            const double f = refined_freq(avg_db, i);
            if (!(std::abs(f - t.track.freq_hz) < config_.attribution_distance_hz)) continue;
            if (best < 0 || avg_db[i] > avg_db[best]) {
                best = i;
                best_freq = f;
            }
        }
        if (best >= 0) {
            t.bin = best;
            t.track.freq_hz = best_freq;
        }
    }
}
```

- in `process`, just before the comment "Refresh existing tracks; expire the ones that have been quiet too long.", add `if (config_.attribution == Attribution::Distance) follow_peaks(avg_db, floor_db);`;
- replace the three lines computing an existing track's `level` with

```cpp
        float level = avg_db[it->bin];
        for (int d = -level_bins_; d <= level_bins_; ++d) {
            const int b = it->bin + d;
            if (b >= 0 && b <= last) level = std::max(level, avg_db[b]);
        }
```

- in the new-peak loop, replace `constexpr int kRadius = 2;`, the loop bounds and the inline peak test with `for (int i = 0; i <= last; ++i)` and `if (!is_peak(avg_db, i)) continue;` after the threshold check (the same bins and the same result: `is_peak` rejects the bins within `peak_bins_` of either end, as the old bounds did);
- replace the `near_track` computation with

```cpp
        bool near_track = false;
        if (config_.attribution == Attribution::Bins) {
            near_track = std::any_of(active_.begin(), active_.end(), [&](const Active& t) {
                return std::abs(t.bin - i) < config_.min_separation_bins;
            });
        } else {
            // Within D of a track's current frequency (which follows its own peak), the peak is that
            // track's station or its spread.
            const double f = refined_freq(avg_db, i);
            near_track = std::any_of(active_.begin(), active_.end(), [&](const Active& t) {
                return std::abs(t.track.freq_hz - f) < config_.attribution_distance_hz;
            });
        }
```

- in the candidate search, replace `std::abs(k.bin - i) <= 1` with `std::abs(k.bin - i) <= step_bins_`.

- [ ] **Step 4: Wire the engine**

In `engine/include/kz4ap/engine.hpp`, add to `EngineConfig`:

```cpp
    // D, Hz (owner decisions 2026-09-29, option 1): each detector track follows its own spectral peak
    // within D of its current frequency, and a new peak within D of a track belongs to it; a channel's
    // frequency tracker fine-tunes around the detector's frequency (FrequencyTrackerConfig::fine_tune_hz).
    double channel_distance_hz = 47.0;
```

and after the struct:

```cpp
// The milestone-1 pipeline, bit for bit (kept selectable and pinned by CI; owner, 2026-09-29):
// the Envelope decoder and the milestone-1 detector rules (frequency fixed at birth, bin attribution).
inline EngineConfig with_envelope_path(EngineConfig config) {
    config.decoder.front_end = FrontEnd::Envelope;
    config.detector.attribution = Attribution::Bins;
    return config;
}
```

and to `Engine::Channel`, after `decoder`: `double detector_freq_hz = 0;  // the detector's current frequency for this track, Hz from the span's center`.

In `engine/src/engine.cpp`, at the end of `resolved()` (before `return c;`):

```cpp
    if (!(c.channel_distance_hz > 0)) throw std::invalid_argument("channel distance must be positive");
    c.detector.attribution_distance_hz = c.channel_distance_hz;
```

In `open_channel`, create the decoder with the detector's residual:

```cpp
void Engine::open_channel(const Track& track, int bin) {
    channelizer_.add_channel(track.id, bin);
    const double residual_hz = track.freq_hz - channelizer_.bin_to_hz(bin);
    channels_.emplace(track.id, Channel{track, bin,
                                        std::make_unique<ClassicalDecoder>(channelizer_.output_rate(), config_.decoder,
                                                                           residual_hz),
                                        track.freq_hz});
    bus_.publish(Event{TrackEvent{TrackEvent::Kind::Born, track}});
}
```

In `process_hop`, at the end of the frame loop's body (after the births), add:

```cpp
        if (!oracle_ && config_.decoder.front_end == FrontEnd::Matched) {
            // Option 1: the detector decides where each channel's station is (its track follows its own
            // peak); the channel's tracker is anchored there before its next block.
            for (const auto& t : detector_.tracks()) {
                if (const auto c = channels_.find(t.id); c != channels_.end()) c->second.detector_freq_hz = t.freq_hz;
            }
        }
```

and in the channelizer sink replace the lines from `const double t0 = …` to `publish_update(...)` with:

```cpp
        auto& channel = it->second;
        const double t0 = static_cast<double>(first_index) / channelizer_.output_rate();
        const double center_hz = channelizer_.bin_to_hz(channel.bin);
        channel.decoder->set_frequency_anchor_hz(channel.detector_freq_hz - center_hz);  // Envelope: ignored
        const auto started = std::chrono::steady_clock::now();
        auto update = channel.decoder->process(s, t0);
        decoder_seconds_ += std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
        channel_samples_ += s.size();
        // Matched mode: the decoder re-centers on the carrier; report the station there.
        if (update.freq_offset_hz) channel.track.freq_hz = center_hz + *update.freq_offset_hz;
        publish_update(id, std::move(update));
```

(`publish_update` already reports `channel.track.freq_hz`. In Envelope mode the anchor is ignored and `freq_offset_hz` is empty, so nothing changes.) Add `#include <cmath>` if needed.

In oracle mode the anchor is the labeled frequency (controller's ruling on review finding I1: the oracle knows it; a bin-center anchor would put a station up to 11.72 Hz off, 0.28 Hz inside the ±12 Hz edge, where noisy estimates beyond 12 Hz would keep snapping the NCO back to the bin center and cost up to |sinc(11.7 Hz · 38.7 ms)|² = −3.2 dB relative to a centered station at 25 WPM, derived). In the `Engine` constructor's oracle loop, after `open_channel(track, bin);` add:

```cpp
        // Oracle mode: the tracker's anchor is the labeled frequency itself, not the bin center the
        // channel is rounded to (the NCO still starts at the bin center, so the tracker must find the
        // residual; it fine-tunes within +/-12 Hz of the label).
        channels_.at(track.id).detector_freq_hz = config_.oracle_frequencies_hz[i];
```

(`open_channel` publishes the `Born` event with the bin-center frequency, as Task 8 specifies; only the anchor changes.)

In `engine/include/kz4ap/classical_decoder.hpp`, change `FrontEnd front_end = FrontEnd::Envelope;` to `FrontEnd front_end = FrontEnd::Matched;  // owner decision 2026-09-29; Envelope stays selectable`.

- [ ] **Step 5: Add `--front-end` to `kz4ap-bench`, and keep the smoke check on the Envelope path**

In `bench/src/main.cpp`: add `FrontEnd front_end = FrontEnd::Matched;` to `Args`; parse

```cpp
        else if (a == "--front-end") {
            const auto v = value().string();
            if (v == "envelope" || v == "baseline") args.front_end = FrontEnd::Envelope;
            else if (v == "matched") args.front_end = FrontEnd::Matched;
            else throw std::runtime_error("--front-end must be envelope or matched\n" + std::string(kUsage));
        }
```

extend `kUsage`'s last line to `"                   [--no-timing] [--baseline BASELINE.json] [--oracle] [--front-end envelope|matched]\n"`, after `config.sample_rate` is set add `if (args.front_end == FrontEnd::Envelope) config = with_envelope_path(config);`, and add `out["front_end"] = args.front_end == FrontEnd::Matched ? "matched" : "envelope";` after `out["duration_s"]`.

In `bench/smoke.sh`, add `--front-end envelope` to both `"$BENCH"` commands, and change the header comment to "score it against the stored baseline on the milestone-1 (Envelope) path". The stored `bench/baselines/smoke.json` is unchanged, and so must the CER be.

In `README.md`, change the suite `run` line to end with `--front-end baseline --front-end matched`, and add after the code block: "`--front-end matched` (the default) selects the dit-matched front end with frequency re-centering, each channel following the station the signal detector assigns it (see `docs/signal-processing.md`, sections 6 and 8b); `--front-end envelope` (also `baseline`) selects the milestone-1 pipeline."

- [ ] **Step 6: Build and run all tests**

```powershell
cmake --build --preset windows
ctest --preset windows
```
Expected: all pass, including the new `SignalDetector` and `Engine` tests above, and every milestone-1 test unchanged apart from its pin to the Envelope path. If a turnover or drift test fails, follow the note under Step 1 (report; change nothing).

- [ ] **Step 7: Smoke check, both front ends**

In Git Bash:
```bash
bash bench/smoke.sh build/windows
build/windows/bench/Release/kz4ap-bench.exe build/windows/smoke/band.wav --labels build/windows/smoke/band.json --front-end matched --no-timing
```
Expected: `smoke test passed` with the baseline's usual CER (the Envelope path is bit-identical); the second command prints a `CER …` line for the matched front end (write it down for Task 14) and `detected 8 of 8`.

- [ ] **Step 8: Document**

In `docs/signal-processing.md`, section 6:
- In **New tracks**, replace the paragraph with:

```markdown
A bin becomes a candidate when its averaged power is at least **6 dB** above
the floor (6 dB SNR per bin) *and* it is the maximum within **±47 Hz**
(±2 bins at 23.4 Hz; ties go to the lower bin). A candidate must be seen in
every frame (moving by at most **23 Hz**, 1 bin, between frames) for
**0.5 s**, and nothing is detected during the first 1 s of a recording (the
averages settle first). A peak whose interpolated frequency is within the
**channel distance D = 47 Hz** of an existing track's *current* frequency
is attributed to that track instead (below, "Channel distance"). The
milestone-1 rule, a peak less than **3 bins** (70 Hz) from a track's bin,
is still selectable (`Attribution::Bins`) and is what the Envelope path
(`--front-end envelope`) uses. The neighborhoods are stated in Hz and
converted to bins from the actual bin width (`DetectorConfig`
`peak_radius_hz`, `candidate_step_hz`). **Status: all heuristic.**
```

- In **Why neighboring bins come up at all**, replace the last paragraph ("Note that these rules are counted **in bins** …") with: "These rules are stated in Hz and converted to bins at the point of use, so a different bin width keeps their width in Hz (the 3-bin rule of `Attribution::Bins` is still counted in bins)."
- In **Existing tracks**, replace "the maximum of its bin and the two neighbors (±1 bin, ±23 Hz)" with "the maximum over its bin's neighborhood of ±23 Hz (±1 bin at 23.4 Hz; `DetectorConfig::level_radius_hz`), the bin being the track's current peak bin".
- In **Frequency and drift**, replace "The frequency is then **fixed** for the track's life: it is not re-measured, and the channel stays where it was put." with:

```markdown
With the Envelope path (`Attribution::Bins`) the frequency is then
**fixed** for the track's life: it is not re-measured, and the channel stays
where it was put. With the Matched path (the default; `Attribution::Distance`)
each track follows its own spectral peak (below, "Channel distance"), so a
drifting station keeps one track and its level is read where it now is;
the decoder re-centers within ±12 Hz of the track's frequency (section 7),
and the engine reports the channel center plus the decoder's estimate. The
channel itself does not move; the decoder's NCO covers ±75 Hz around it.
Only slow drift is in scope (owner, 2026-09-29): simulated at 1 Hz/s, one
track, the reported frequency 1.49–1.63 Hz behind the carrier and the
track's own 0.94–1.23 Hz behind (a 1 s average of a ramp lags ḟ·1 s,
derived).
```

- Add a subsection after **Frequency and drift**:

```markdown
### Channel distance

One distance, **D = 47 Hz** (`EngineConfig::channel_distance_hz`;
**heuristic**, owner decisions 2026-09-29, "option 1"), decides with the
Matched path which station each channel follows. The detector alone
decides:

1. **Following:** every frame, before its level is read, each track moves
   to the strongest bin that is a peak by the birth rule (the maximum
   within ±47 Hz), stands at least 3 dB above the floor (the keep-alive
   level), and whose interpolated frequency is within D of the track's
   current frequency. With none, it holds its frequency.
2. **Attribution:** a new peak within D of a track's current frequency is
   that track's (above); one farther away can become a track of its own.
3. **The channel's tracker fine-tunes** within ±12 Hz of the detector's
   frequency for its track (section 7); the engine passes it before every
   channel block. So in a QSO turnover within D the channel retunes to the
   answering station when the detector's peak moves there, and back.
   Channels are never merged.

Why 47 Hz: about the half-width of the detector's Hann main lobe, 2/T_w
for its 42.7 ms window (46.9 Hz); a peak closer than that to a station can
be that station's own spread. Stated in Hz, it does not change with the
FFT size.

Simulated (plan, 2026-09-29; A at S₅₀₀ = 15 dB, 25 WPM; B answering at
18 WPM, its level in dB re A's key-down power; 30 seeds each): within D
(0–40 Hz) at −6 dB or stronger, A's channel followed B, decoded it and
came back, with one channel; 60–200 Hz away B had its own channel,
decoded in 30 of 30 from −6 dB up; channels were never merged.
**Limits (stated, not fixed):**
- **Retune delay.** The detector's peak moves to an answering station
  only when its 1 s power average overtakes the first station's decaying
  one: 1.3–1.8 s into B's over at 10–25 Hz, 2.3 s at 40 Hz and −10 dB
  (simulated; 2.6 s after A's last mark derived at −10 dB). It matters
  only when the answering station is on a different frequency from the
  one the channel is tuned to (a station that pauses and resumes on its
  own frequency loses nothing). Median characters of B lost at the start
  of its over: 0 at 10 Hz; at 25, 40 and 50 Hz, 1, 3 and 3 at −10 dB, 0,
  1 and 2 at −6 dB, 0 at 0 and +6 dB.
- **A neighbor 60–70 Hz away at A's level or stronger** leaks through the
  matched filter's first sidelobe (−18.7 dB relative to a centered station
  at 60 Hz and K = 58; −19.6 dB at 70 Hz through the 16 ms acquisition
  filter; derived), is keyed in fragments and corrupts the speed estimate:
  A's next over exact in 6 and 0 of 30 at 60 Hz, 0 and +6 dB (its last
  three words intact in 26 and 30). The fix belongs in the filter's design.
- **Oracle mode** (benchmark only) has no detector, so the anchor is fixed
  at the labeled frequency the oracle channel was opened for, and the
  tracker covers ±12 Hz around it: a station that drifts more than 12 Hz
  from its label, or a QSO's answering station more than 12 Hz from the
  label, cannot be followed there; the benchmark marks such oracle rows as
  not meaningful for the Matched front end.
```

In section 7, in **Frequency re-centering (Matched front end only)**, replace "The decoder passes on the anchor its caller gives it (`Decoder::set_frequency_anchor_hz`); nothing in the engine calls it yet (milestone 2, part 1, Task 13 does)." with "The engine sets the anchor before every channel block to the detector's current frequency for the track minus the channel center (section 6, "Channel distance")", and add after the NCO bullet: "The engine starts each new track's NCO at the detector's residual, track frequency minus channel center (in oracle mode, 0 Hz: the channel sits on the bin nearest the labeled frequency and the tracker must find the rest; its anchor is the labeled frequency itself, so it fine-tunes within ±12 Hz of the label)." In section 8, **Two front ends**, replace "a station farther away is followed only when the caller moves the anchor to it." with "a station farther away is followed only when the caller moves the anchor to it: the engine sets the anchor to the detector's frequency for the track (section 6, "Channel distance"), so a station answering within 47 Hz is followed once the detector's track moves to it (with the retune delay stated there), and one farther away gets its own track.", and change "`Envelope` (the default, steps 1–5 below) is the baseline" to "`Envelope` (steps 1–5 below) is the baseline; `Matched` is the default (owner decision 2026-09-29)". In section 8b's opening, change "Used when the classical decoder's front end is `Matched`" to "Used when the classical decoder's front end is `Matched` (the default; owner decision 2026-09-29)". Change section 10's front-end row to `| Front end | Matched (default) or Envelope | `ClassicalDecoderConfig::front_end`; `--front-end` | owner decision 2026-09-29 |`.

In section 10, replace the rows **Candidate tracking**, **Min station separation**, **Peak neighborhood** and **Track level neighborhood** with:

```markdown
| Candidate tracking | may move ±23 Hz (1 bin at 23.4 Hz) between frames | `DetectorConfig::candidate_step_hz` | heuristic |
| Track following and attribution (Matched path) | each track follows its own peak within D = 47 Hz (at least 3 dB above the floor); a new peak within D of a track's current frequency belongs to it | `DetectorConfig::attribution`, `attribution_distance_hz` (set from `EngineConfig::channel_distance_hz`) | heuristic (owner decisions 2026-09-29, option 1) |
| Attribution (Envelope path) | frequency fixed at birth; peaks less than 3 bins (70 Hz) from a track's bin belong to it | `DetectorConfig::min_separation_bins` | heuristic (milestone 1) |
| Peak neighborhood | ±47 Hz (±2 bins at 23.4 Hz) | `DetectorConfig::peak_radius_hz` | heuristic |
| Track level neighborhood | ±23 Hz (±1 bin at 23.4 Hz) | `DetectorConfig::level_radius_hz` | heuristic |
| Tracker anchor (Matched path) | the detector's current frequency for the track, set before every channel block; the tracker fine-tunes within ±12 Hz of it | `Decoder::set_frequency_anchor_hz`; `FrequencyTrackerConfig::fine_tune_hz` | heuristic (owner decisions 2026-09-29, option 1) |
```

- [ ] **Step 9: Commit on the `milestone-2` branch**

```powershell
git add engine/include/kz4ap/signal_detector.hpp engine/src/signal_detector.cpp engine/include/kz4ap/engine.hpp engine/src/engine.cpp engine/include/kz4ap/classical_decoder.hpp engine/tests/test_signals.hpp engine/tests/signal_detector_test.cpp engine/tests/engine_test.cpp engine/tests/classical_decoder_test.cpp bench/src/main.cpp bench/smoke.sh README.md docs/signal-processing.md
```
```powershell
git commit -m "Let the detector decide each channel's station, re-center from it, and make Matched the default"
```

---

### Task 14: Measure, document, and guard the new path in CI

Spec §5.4: "No decoder replaces the baseline unless it beats the baseline on the benchmark." This task runs the full suite on both front ends, adds the one missing measurement (how far the tracked frequency is from the truth), writes the numbers into the documents, and adds a CI smoke check for the Matched path. `FrontEnd::Matched` is already the default (Task 13; owner decision 2026-09-29). This task does **not** tune its parameters: tuning is deferred to the benchmark results and needs strong evidence (owner, 2026-09-27 and 2026-09-29), so this task measures, records, and hands the numbers to the owner. **If Matched is worse than Envelope in any regime** (a paired Matched − Envelope difference whose 95% interval excludes 0 in Envelope's favor, or a crossing worse by more than its interval), report it to the owner with the numbers; **do not revert the default**. No step below may edit a default or a Matched parameter value.

**Files:**
- Modify: `bench/src/scoring.hpp` (`DecodedTrack::last_freq_hz`), `bench/src/main.cpp`
- Modify: `bench/smoke.sh`; Create: `bench/baselines/smoke-matched.json`
- Modify: `docs/signal-processing.md` (§7, §8b, §10, §11), `docs/backlog.md`, `docs/research/decoder-survey.md`

**Interfaces:**
- Consumes: everything above; `kz4ap-bench --front-end`, `--oracle`; `kz4ap_synth.suites` (Task 9 already reads `tracked_freq_hz` when present and reports the median frequency error).
- Produces: `DecodedTrack` gains `double last_freq_hz = 0;` (the frequency of the track's latest `DecodedTextEvent`, else its birth frequency); each `score.signals` entry of the bench JSON gains `tracked_freq_hz` (null if no track matched).

- [ ] **Step 1: Check that the bench does not report the tracked frequency yet**

In Git Bash, after `bash bench/smoke.sh build/windows` has made the smoke recording:

```bash
build/windows/bench/Release/kz4ap-bench.exe build/windows/smoke/band.wav --labels build/windows/smoke/band.json --json build/windows/smoke/freq.json --no-timing
PYTHONPATH=training .venv/Scripts/python -c "import json; s = json.load(open('build/windows/smoke/freq.json'))['score']['signals']; assert all('tracked_freq_hz' in x for x in s), 'no tracked_freq_hz'"
```
Expected: `AssertionError: no tracked_freq_hz`.

- [ ] **Step 2: Implement the frequency-error measurement**

In `bench/src/scoring.hpp`, add to `DecodedTrack`: `double last_freq_hz = 0;  // frequency of the latest decoded-text event, Hz (birth frequency until then)`.

In `bench/src/main.cpp`, in the subscriber, set `tracks[t->track.id] = {t->track.id, t->track.freq_hz, "", t->track.freq_hz};` for `Born`, and in the `DecodedTextEvent` branch add `tracks[d->track_id].last_freq_hz = d->freq_hz;`. In the per-signal JSON add, after `"track_id"`:

```cpp
                                   {"tracked_freq_hz", sig.track_id ? nlohmann::json(tracks.at(*sig.track_id).last_freq_hz)
                                                                    : nlohmann::json()},
```

In `docs/signal-processing.md`, section 11, after the **Suites** bullet, add (review finding M9: a benchmark definition goes in with its code):

```markdown
- **Frequency error:** for each matched label, |f_tracked − f_true|, Hz,
  where f_tracked is the frequency of the track's latest decoded text
  (its birth frequency until then) and f_true is the label's carrier; for a
  QSO label, the carrier of the station that sent the last over (that is
  where the latest text came from); for a station label (group H), that
  station's carrier. Signals with drift are left out. Summaries report the
  median per condition.
```

Build (`cmake --build --preset windows`), re-run the two commands of Step 1, and expect no output (every matched signal now has `tracked_freq_hz`). Then `ctest --preset windows` and `.venv\Scripts\python -m pytest training -q` → all pass.

- [ ] **Step 3: Commit the measurement tooling**

```powershell
git add bench/src/scoring.hpp bench/src/main.cpp docs/signal-processing.md
```
```powershell
git commit -m "Report the tracked frequency in the bench"
```

- [ ] **Step 4: Run the full suite on both front ends**

```powershell
cmake --build --preset windows
$env:PYTHONPATH = "training"
.venv\Scripts\python -m kz4ap_synth.suites generate --suite full --seeds 3 --out build/suite/full3
.venv\Scripts\python -m kz4ap_synth.suites run --out build/suite/full3 --bench build/windows/bench/Release/kz4ap-bench.exe --front-end baseline --front-end matched
.venv\Scripts\python -m kz4ap_synth.suites summarize --out build/suite/full3
```
Expected: 117 recordings (4.6 h of audio, about 3.2 GB; generating them takes about 57 min, scaled from a measured one-seed run of 19 min and 1.06 GB), each scored with both front ends (the nine group-H recordings also against their station labels); `build\suite\full3\summary.md` has a baseline and a matched row with 95% intervals for every condition, the paired Matched − baseline table, the per-over and tracks-per-QSO tables, and a CPU table with both front ends. Record how long `generate` and `run` took (wall-clock, min) and the machine: they go into the results. Close other heavy programs while it runs, since CPU time is being measured.

Also record the machine: `Get-CimInstance Win32_Processor | Select-Object -ExpandProperty Name`.

- [ ] **Step 5: Check the numbers before writing them down**

Read `summary.md` and check, before trusting it:
These checks look for broken runs, not for a particular winner: the benchmark decides (spec §5.4), and the owner decides what to do with it.
- Group A: the baseline's S₅₀₀ at CER 0.10 should be in the region the detector-free baseline can reach (a few dB above 0 dB S₅₀₀; the squelch alone stops it near +6 dB by estimate). The Matched crossing is expected to stop near the acquisition floor, S₅₀₀ = −2.5 dB derived at every speed and up to 1.4 dB higher with ŝ's ramp bias (Design decisions C, "Squelch"; simulated: 50% of marks keyed near −1.8 dB at 25 WPM and −2.6 dB at 12 WPM), not at the dit-matched −4.4 … −7.9 dB; CER 0.10 needs nearly every mark keyed (simulated: 80% keyed at −1 dB, 25 WPM; 95% at −2 dB, 12 WPM), so a crossing anywhere from about −2.6 to 0 dB is the design, not a bug. Whatever the Matched crossing is, read it with its interval; if it looks implausible (for example, worse than the baseline by more than the intervals and the design's expected gain would suggest), investigate whether a run or a scoring step went wrong (superpowers:systematic-debugging) and **report what you find to the owner**. Do not change a parameter or a test to move it.
- Group F: the Matched CER should not depend on the offset (0 to 11.7 Hz) by more than the intervals; its median frequency error should be within the ±2 Hz target at S₅₀₀ = 5 dB. If either is off, investigate and report; do not tune. **Expected limit of option 1 in oracle mode (report it, do not fix it):** with the detector bypassed the tracker's anchor is fixed at the labeled (starting) frequency and the tracker covers only ±12 Hz around it, so group F's drift recordings (0.2–2 Hz/s for up to about 28 s: 6–56 Hz) lose the Matched decode once the carrier is more than 12 Hz from its label (from about 0.5 Hz/s on); mark those drift rows "not meaningful (oracle anchor)" for Matched and report them separately. The engine with the detector (Task 13, `MatchedReportsDriftingFrequency`) follows slow drift. The offset rows (0–11.7 Hz from the bin center) are meaningful: the anchor is the label, so the offset is found from the NCO's start at the bin center.
- Group H: the "Tracks per QSO" table should show about 1 track for same-track QSOs and about 2 for separate-track ones; the oracle copy's same-track rows show the turnover apart from detection, and in the oracle copy the channel cannot follow an answering station more than 12 Hz from the label (no detector moves the anchor), so mark its rows at 25 and 50 Hz (and drawn offsets beyond 12 Hz) "not meaningful (oracle anchor)" for Matched. Compare the detector run's per-over first-word CER with the option-1 simulation's retune-delay numbers (Design decisions B) and the 50 Hz rows with its runaway (12 of 30 at 0 dB re A before the first-step bound), and report both.
- The smoke recording's baseline CER (run `bash bench/smoke.sh build/windows`) must equal the value written down in Task 2, Step 5: if it moved, the baseline was changed by accident — find and fix that first (that is a code bug, not tuning).

- [ ] **Step 6: Write the measured results into `docs/signal-processing.md`**

At the end of section 8b add a subsection, filling every cell from `build/suite/full3/summary.md` (3 seeds) with its 95% interval, and naming the processor and the run times from Step 4:

```markdown
### Measured: Matched against Envelope (milestone 2, part 1)

Full suite, 3 seeds, synthetic recordings (`kz4ap_synth.suites`), on
<processor>; generating took <min> min and scoring <min> min. S₅₀₀:
key-down carrier power over noise power in 500 Hz, dB. Groups A–G and the
"H, oracle" copy use oracle channels (detector bypassed, channel on the
nearest bin); the rest run the whole pipeline. Parentheses: bootstrap 95%
intervals over signals.

| Condition | Envelope: S₅₀₀ at CER 0.10 / 0.05 (dB) | Matched: S₅₀₀ at CER 0.10 / 0.05 (dB) |
|---|---|---|
| A, 12 WPM | … | … |
| A, 25 WPM | … | … |
| A, 40 WPM | … | … |

| Condition | Envelope: CER (interval) / character CER / space error rate / first-word CER | Matched: same | Matched − Envelope, paired (interval) |
|---|---|---|---|
| (one row per tag of groups B–H, the per-station view of group H, and of strong, pauses, tune-up, first sample, crowded, band, copied from summary.md) | … | … | … |

| Group H regime (tags) | Tracks per QSO, Envelope / Matched | QSO-label CER, Envelope / Matched | Station-label CER, Envelope / Matched |
|---|---|---|---|
| same-track (0, 10, 25 Hz; drawn) | … | … | … |
| ambiguous (50 Hz; drawn) | … | … | … |
| separate-track (100, 200 Hz; drawn) | … | … | … |

| Per over (groups G and H, from the "Per over" table) | Envelope: CER | Matched: CER |
|---|---|---|
| (one row per group and keying style of the sending station) | … | … |

| Group B against VE3NEA (no-space CER, his metric) | VE3NEA DeepCW | CW Skimmer (his measurement) | Envelope | Matched |
|---|---|---|---|---|
| Paddle, 24 WPM, f_D 0.1 Hz, S₅₀₀ 1.78 / 7.78 / 17.78 / 57.78 dB | 0.373 / 0.137 / 0.025 / 0.005 | 0.364 / 0.101 / 0.022 / 0.011 | … | … |
| HandKey, 24 WPM, f_D 0.1 Hz, S₅₀₀ 1.78 / 7.78 / 17.78 / 57.78 dB | 0.412 / 0.186 / 0.082 / 0.063 | 0.429 / 0.188 / 0.091 / 0.083 | … | … |

(VE3NEA's values are from `docs/research/deepcw-generator-notes.md` §6, his ρ = −6, 0, 10, 50 dB. Read the Envelope and Matched cells from `build/suite/full3/results/<front end>/B-fading-{paddle,hand}-24wpm-0.1Hz-s*.json`: pool `nospace_edits` over `nospace_symbols` of the signals whose `snr_db` is that point, over the three seeds, and give the pooled character count beside each cell (at least 1000; his points hold 30 000). Remaining differences from his benchmark, to state under the table: bin-centered oracle channels instead of his ±30 Hz pitch error, complex I/Q noise instead of real audio, and one draw per character and word space; the keying edges match his (2 ms, centered).)

| | Envelope | Matched |
|---|---|---|
| CPU per channel-second (whole process), ms/s | … | … |
| Decoders per channel-second, ms/s | … | … |
| Median frequency error, group F offsets, Hz | … | … |
```

Then update the status of every parameter this measurement settles, in §7 ("Frequency re-centering") and §8b and the §10 rows: the tracker's accuracy becomes **measured** with the median error; leave β, τ_a, τ_n, the squelch and the hysteresis **heuristic** (no sweep is in this plan, and tuning is deferred to the benchmark results; owner, 2026-09-29).

- [ ] **Step 7: Guard the Matched path in CI**

Run the Matched front end on the smoke recording and read its CER:

```bash
build/windows/bench/Release/kz4ap-bench.exe build/windows/smoke/band.wav --labels build/windows/smoke/band.json --front-end matched --no-timing
```

Create `bench/baselines/smoke-matched.json` with `max_cer` = that CER plus a margin of 3 edits, 3 / (the smoke recording's reference symbols, the sum of `symbols` over `score.signals` in the JSON), rounded up to two decimals, and the baseline's detection floor:

```json
{"max_cer": <measured CER + 3 / total reference symbols, rounded up to 2 decimals>, "min_detection_recall": 0.875}
```

(Review finding M10: the Linux runner's libm (`exp`, `log`, `cos`) differs from Windows', and one flipped keying edge can change a character or two, so the margin is stated in edits. It was calibrated on Windows only: before this branch is merged, the Linux CI run must pass it; if it fails there, report the Linux CER to the owner rather than widening the margin. `bench/baselines/smoke.json` stays as it is.)

Replace `bench/smoke.sh` with:

```bash
#!/usr/bin/env bash
# Benchmark smoke test: generate a synthetic band, score it against the stored
# baselines on the milestone-1 (Envelope) path, which must stay bit-identical, and on the
# Matched path (the default), and check that two runs of each produce identical results.
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

check baseline bench/baselines/smoke.json --front-end envelope
check matched bench/baselines/smoke-matched.json --front-end matched
echo "smoke test passed"
```

Run in Git Bash: `bash bench/smoke.sh build/windows`
Expected: two `CER …` summary lines, then `smoke test passed`.

- [ ] **Step 8: Update the backlog and the survey**

In `docs/backlog.md`, section 1:
- Under **Top priority**, option 1: add a line "**Built** (milestone 2, part 1): `FrontEnd::Matched`, the default since the owner's decision of 2026-09-29; measured against the baseline in `docs/signal-processing.md` §8b. Tuning its parameters waits for evidence from these measurements." followed by the one-line result for group A at 25 WPM (both crossings) and the CPU cost per channel-second of each.
- **Benchmark scenarios to add first:** replace the list with "Done in milestone 2, part 1 (`training/kz4ap_synth/suites.py`)."
- **Channel filtering, two stages:** add "Stage 2 is built as the Matched front end (a boxcar of 0.8 dit); stage 1 is unchanged."
- **Track frequency drift:** add "Done within ±75 Hz of the channel's center in Matched mode (each detector track follows its own peak within D = 47 Hz; the channel's NCO fine-tunes within ±12 Hz of it; slow drift only, owner 2026-09-29). Still open: moving the channel's center bin for larger drifts, drift in Envelope mode, and drift in oracle mode (no detector; the tracker covers ±12 Hz of the labeled frequency)."
- **Measure where answering stations really are** (added to the backlog with this plan's revision, 2026-09-27): add group H's tracks-per-QSO result by regime, which shows how much the real offset distribution matters.
- **Stray E's after a station stops:** add the Matched front end's result on the `pauses` and `strong` groups (CER and first-word CER from the tables) and whether `MatchedNoiseAfterStationStopsDecodesNothing` covers the case.
- **Tune the Matched front end by measurement** and the four postponed limits under it (added 2026-09-29): add the measured numbers that bear on each (group A's crossings for the acquisition floor; the pauses, strong and first-sample groups for the noise recovery and stray noise; group H's regime table and group E for the channel distance, the retune delay, the 60–70 Hz sidelobe leak and co-channel stations; group H's 50 Hz rows for the first-step growth bound).

In `docs/research/decoder-survey.md`, at the end of the paragraph **1. Soft, pre-filtered front end on the existing baseline**, add one sentence: "Built in this project as the Matched front end (milestone 2, part 1); its measured gain over the baseline on the synthetic suite is in signal-processing.md §8b [measured]." Do not change the ranking.

- [ ] **Step 9: Commit on the `milestone-2` branch**

```powershell
git add bench/smoke.sh bench/baselines/smoke-matched.json docs/signal-processing.md docs/backlog.md docs/research/decoder-survey.md
```
```powershell
git commit -m "Record the Matched front end's measured results and guard it in the smoke check"
```

- [ ] **Step 10: Report to the owner**

Summarize for the owner, in a few lines: the group-A crossings for both front ends, the fading and fist results next to VE3NEA's anchor rows, the pause/strong/tune-up findings, the CPU cost, the tracker's measured accuracy, and the ragchew and two-station-QSO results (group G against group A at 25 WPM; group H by regime: tracks per QSO, QSO-label and station-label CER, first-word and per-over CER, with the oracle copy beside the detector run), the paired Matched − Envelope differences whose intervals exclude 0, and, plainly, **every regime where Matched is worse than Envelope** (the default stays Matched; the owner decides). Present the numbers that bear on the backlog item "Tune the Matched front end by measurement" and its postponed limits (the acquisition floor, S₅₀₀ = −2.5 dB derived and about −2.6 to −1.8 dB simulated; the noise-rise recovery; stray noise after a silence; co-channel stations; option 1's stated limits: the retune delay after a turnover to another frequency within D, the 60–70 Hz sidelobe leak, and whether the first-step growth bound removed the 50 Hz runaway; the oracle-mode drift limit of group F). Recommend no tuning; present the numbers. Do not push.

---

## Self-review (plan author's check against the spec)

**Spec coverage.**

| Spec requirement | Where |
|---|---|
| §3.1 item 1: decoder robustness, measured on the benchmark | Tasks 1–9 (benchmark), 14 (measurement) |
| §5.2 step 1: filter matched to the current dit, before envelope detection | Task 11 (boxcar, B = r/K ≈ 1.25/T), Task 12 (follows the speed estimate) |
| §5.2 step 1: envelope → Rician-versus-Rayleigh LLR | Task 11 (`envelope_llr`, amplitude estimate, and a noise estimate independent of the keying decision) |
| §5.2 prerequisite: precise re-centering with drift tracking, target ≈ ±2 Hz | Task 10 (tracker), Task 12 (`MatchedTracksTheResidualOffset`), Task 13 (detector's initial estimate, tracks following their own peak, the anchor, published frequency), Task 14 (measured median error) |
| §5.2: two stages, the channelizer unchanged | Tasks 10–13 run at r on the channelizer's output; `channelizer.cpp` is not touched |
| §5.2: correlated samples scaled or decimated | Task 11 (`weight = 1/K`, autocorrelation test) |
| §5.2: front end useful on its own in front of the baseline | Task 12 (LLR keying), Task 14 (measured against the baseline) |
| §5.1: the baseline stays in the code | `FrontEnd::Envelope` stays selectable and bit-identical (`--front-end envelope`, `with_envelope_path`; `FrontEnd::Matched` is the default from Task 13); Tasks 2, 5, 6, 7, 12, 13 and 14 check it |
| §5.4: VE3NEA's grid, speed changes, interference, tuning error, strong signals up to S₅₀₀ = 60 dB, stations that stop and pause, tune-up carriers, crowded bands, first words | Tasks 2–6 (generator), 9 (suites), 7 (first words) |
| §5.4: VE3NEA's grid as a true external anchor | Task 3 (his keying styles, imbalance, style mix, speed range), Task 4 (his Butterworth spectrum), Task 1 (his text statistics), Task 3 (his 2 ms centered edges, as an option), Task 7 (his metric, no-space CER), Task 9 (group B: his f_D grid, SNR points as S₅₀₀ = ρ + 7.78 dB, ≥ 1000 characters per point over 3 seeds), Task 14 (side-by-side table) |
| §5.4: CER with prosigns as one symbol and word spaces scored separately | Task 7 |
| §5.4: S₅₀₀ as generated; CPU time per channel-second | Tasks 2–6 (labels), 8 (CPU), 9 (summaries); Raspberry Pi 5: deferred |
| Owner decision 2026-09-27: test transmissions include full ragchews | Task 1 (ragchew text, prosign positions, abbreviations), Task 6 (two stations alternating, labeled per over and per station), Task 12 (re-acquisition at a turnover), Task 9 (groups G and H, per-over CER), Task 14 (results) |
| Owner, 2026-09-27: answering stations 0–200 Hz away, three regimes, each station scored at its own frequency; backlog item to measure the real offsets | Task 6 (`station_labels`, `draw_answer_offset_hz`), Task 9 (`qso_regime`, group H grid 0/10/25/50/100/200 Hz and drawn offsets, QSO and station labels, tracks per QSO), Task 14 (regime table); `docs/backlog.md` item "Measure where answering stations really are" (added with this revision) |
| Plan review 2026-09-27 (C1, C2, I1–I5, M1–M12) | Resolutions listed in the review file; the changes are in Tasks 3, 6, 7, 9–14 and Design decisions |
| Owner decisions 2026-09-29: one channel distance D in Hz; Matched the default in Task 13 with Envelope selectable and pinned in CI; ×1.25 dit-growth bound; 20-seed pass counts; postponed limits to the backlog; physical units. Option 1 (same day): the detector decides the station (tracks follow their own peak within D), the tracker fine-tunes within ±12 Hz of the detector's frequency, the growth bound from the filter's first follow step, no merging, slow drift only, three stated limits to the backlog | Scope section (the decision, both decision lists); Design decisions B (option 1, its simulation and limits; the first design as the record) and C; Task 10 (`fine_tune_hz`, `set_anchor`, `lag_s`, `update_interval_s`); Task 12 (`set_frequency_anchor_hz`, `filter_dit_s_`, `max_dit_growth`, `count_passes`); Task 13 (`Attribution`, peak following, the neighborhoods in Hz, `channel_distance_hz`, the anchor, the default, `with_envelope_path`, `--front-end envelope`); Task 14 (Envelope pin and Matched check in the smoke test; a regime where Matched is worse is reported, the default is not reverted); `docs/backlog.md` |
| Owner decision 2026-09-27 and 2026-09-29: parameter tuning deferred to the benchmark results | Tasks 10–12 Step "Build and run" (a failing parameter is reported, not changed); Task 14 intro and Steps 5, 6, 8 and 10 |
| §5.4: real recordings with manta's oracle | Deferred (scope section); Task 8 builds the oracle mechanism |
| §4.1: determinism | Per-sample stages inside the decoder; `MatchedChunkSizeDoesNotChangeOutput`, `MatchedChunkingDoesNotChangeResults`; the smoke check compares two runs of each front end |
| Project rule: signal-processing.md in the same commit | Every task that changes signal processing or benchmark definitions has a "Document" step |

**Placeholder scan.** The only unfilled values are measurements that exist only after running the code: the smoke CER noted in Task 2, the Matched smoke CER and its baseline file in Task 14, and the results tables in Task 14 (including the Envelope and Matched cells of the VE3NEA comparison), each with the exact command or file it comes from. The Python code of Tasks 1–6 and 9, as revised on 2026-09-27, was run in a scratch copy of `training/` (applied in order): all Python tests pass, and the G and H recordings generate (about 6 min each). The C++ changes for the review's C1 and C2 and the re-review's I-1 to I-4 (Tasks 10–12: noise estimate with floor and warm-up, K-scaled squelch, re-acquisition with its window, the tracker's anchor, capture range and coherence checks; on 2026-09-29 the first design's follow distance, the growth bound and, at engine level, the detector's distance attribution and channel merging, then option 1's peak following and fine-tuning anchor) were ported to Python line for line and simulated; every multi-seed test's bands come from those simulations. The C++ itself is not compiled here, so its tests are the first check of the port. Since the final check (2026-09-28) the decoder-level tests of Task 12 are simulated on a port of the milestone-1 decoder's own element and speed logic, with the decoder estimating the dit itself, over 30–200 numpy seeds per test; the Task 11 tests over 40–400.

**Type consistency.** Checked across tasks: `Operator`, `Over`, `ragchew`, `random_operator`, `random_text` (Task 1) and their use in Tasks 6 and 9; `STYLES` keys, `draw_style`, `draw_imbalance_dits`, `VE3NEA_WPM_RANGE` (Task 3) and their use in Tasks 6 and 9; `slow_gain`, `gain_at`, `rayleigh_gain(..., shape)` and `fading_shape` (Task 4) and their use in Task 6; `edge_s`, `edges_centered` (Task 3) and their use in Tasks 6 and 9; `Sender`, `qso_spec`, `station_labels`, `draw_answer_offset_hz`, `SignalPlan.senders` and the per-over label keys (`sender`, `sender_index`, `wpm`, `keying`, `offset_hz`) (Task 6) and their use in Task 9 (`over_rows`, `track_splits`, `_label_truth_hz`); `SignalSpec` fields (Tasks 2–6) and the labels keys the bench parses (Task 7) and the suites read (Tasks 9, 14); the bench JSON's `nospace_*`, `transmissions` and `tracks` (Task 7 and milestone 1) and `tracked_freq_hz` (Task 14) in Task 9; `FrequencyTracker::reacquire`, `MatchedFrontEnd::reacquire` and `MatchedFrontEnd::squelch` (Tasks 10–11) in Task 12, and the new config fields (`fine_tune_hz`, `lag_s`, `update_interval_s`, `min_coherence`; `max_dit_growth`; `channel_distance_hz`, `Attribution`, `attribution_distance_hz`, `peak_radius_hz`, `level_radius_hz`, `candidate_step_hz`; `neighbor_guard`, `floor_*`, `squelch_exponent`; `reacquire_window_s`) with their validation tests; `view_fits` (Task 9) in `format_markdown` and its test; `Score`/`SignalScore` fields and the bench JSON keys the suites consume; `EngineStats`, `Engine::Channel::bin`, `open_channel` (Task 8) and their use in Task 13; `FrequencyTracker` and `MatchedFrontEnd` signatures (Tasks 10–11) and their use in Task 12; `DecodeUpdate::freq_offset_hz` and `Decoder::set_frequency_anchor_hz` (Task 12) and their use in Task 13; `FrequencyTracker::set_anchor` and `anchor_hz` (Task 10) in Task 12; `FrontEnd` (Task 12) in the bench (Task 13).

**Review Focus.** Each of the seven items has its tests in the owning tasks (Tasks 6, 9 and 10–13), named in the Review Focus section.

**Known risks for the executor.** Tasks 10, 12 and 13 implement option 1 (owner decisions 2026-09-29): its stated limits (the retune delay after a turnover to another frequency within D, a neighbor 60–70 Hz away at the channel's station's level or stronger leaking through the boxcar, and the oracle-mode drift limit) are not asserted, and the first-step growth bound's effect on the 50 Hz runaway was not simulated; Task 14 measures and reports them. Several design limits are postponed to the backlog, not fixed: the acquisition floor (S₅₀₀ = −2.5 dB derived, about −2.6 to −1.8 dB simulated), the noise-rise recovery (about 43 s after a sustained 6 dB rise), stray noise after about 1% of silences, and co-channel stations keying at the same time within a few tens of Hz. Seven Task 12 tests assert a 20-seed pass count below 20 (Task 12, Step 5); if one fails, report it with its printed failures, and do not change a seed, level, tolerance or count. Random keying may make a suite recording run long; Task 9 says how to fix that. The turnover tests were simulated with the decoder's own speed logic; the engine-level tests of Task 13 with a port of the detector (with option 1's peak following), channelizer and decoder at 6000 samples/s (the engine's bin width and hop), so their C++ results may differ in detail; if one fails other than as its comment states, debug it, and if the cause is a parameter, report it to the owner. The noise estimate comes down slowly from a high start (a station keying from the first sample); the "first sample" group measures it. Group B holds at least 1000 characters per point over 3 seeds, still far fewer than VE3NEA's 30 000, so compare trends and intervals against his curves, not single points. The full suite at 3 seeds is about 3.2 GB of recordings and takes about 57 min to generate (19 min and 1.06 GB measured for one seed).
