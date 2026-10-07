# Development set redesign: Results Record

Plan: `docs/plans/2026-10-06-development-set-redesign.md` (approved by the owner, 2026-10-06). Every number here
is **measured** unless marked otherwise. CPU and wall times are from the Linux machine: Linux (Ubuntu 22.04),
10-core Intel Xeon (Ice Lake).

**Answer (D4, the new set's baseline; section 5).** DEV2 is generated at the owner's sizes: 48 signals per
(speed, S₅₀₀) cell in A2, 1.07 M oracle channel-seconds for seed 1, and seeds 1–3. The fit now has one floor per
speed cell (section 3.5).

- **The crossings.** On seed 1 the bank's crossing (S₅₀₀ at CER 0.10) is known to ±0.11 to ±0.29 dB SNR in
  500 Hz in speed cells 2–10. These are 95% percentile intervals, which cover about 90% (measured at 4 signals per
  cell, section 5.6 c); calibrated, they would be about 1.19 times wider.
  - It is 0.65–1.57 dB SNR in 500 Hz below Matched's from 22.5 to 57 WPM.
  - It is +6.44 dB SNR in 500 Hz at 71 WPM, where Matched never reaches CER 0.10 (floor 0.126).
  - It is 0.58–1.16 dB SNR in 500 Hz above Matched's at 11–14 WPM.
- **Pooled over the whole set**, bank − Matched is −0.0396 (−0.0448 to −0.0342) in CER per signal.
  - The bank is clearly better with Farnsworth timing (−0.46), and with fading and keying styles (about −0.09).
  - It is clearly worse with an interfering neighbor (+0.41). The old set's group E showed the same deficit
    (+0.30, section 5.4).
- **Below 10 WPM** the bank fails at every S₅₀₀ (floor 0.525). That is being traced separately, and those results
  are not interpreted here.
- **A bank run** on DEV2 took 1 h 48 min at 5 threads (30.41 ms CPU per channel-second).

**Answer (D3, the pilot).** Group A2 at 4 signals per (speed, S₅₀₀) cell (560 signals, 34 586.2
channel-seconds) was decoded by Matched and by the bank decoder at its current defaults. The pilot gives the
crossing (S₅₀₀ at CER 0.10) per speed cell for both decoders, with 95% bootstrap intervals of ±0.4 to
±3.7 dB SNR in 500 Hz (half-widths).

- **Sizes.** Scaling the intervals as 1/√n (an assumption, section 4.1), ±0.5 dB SNR in 500 Hz in every speed cell for both
  decoders needs **224 signals per cell**: 56 times the pilot, 4.76 M channel-seconds, about 4.3 h per bank run.
  Matched's 80 WPM cell alone sets that number. Every other (decoder, cell) pair reaches the target at
  **48 signals per cell**. The proposed size is therefore 48 signals per cell (option B, section 4.4):
  - 6 720 A2 signals;
  - 1.07 M oracle channel-seconds for the whole DEV2;
  - about 57 min per bank run.
  The owner chooses the size before the full generation.
- **The bank below 10 WPM.** The bank decoder fails on every signal from 8.03 to 9.89 WPM (most of speed
  cell 1, 8.00–10.07 WPM), at every S₅₀₀ up to +20 dB SNR in 500 Hz, with CER 0.25–0.74. Above 4 dB SNR in
  500 Hz, every signal from 10.06 WPM up decodes (CER at most 0.04).
- **The fit model and that failure.** The plan's smooth fit has one floor for all speeds, so it cannot
  represent a speed cell that never crosses. With cell 1 in the fit, the bank's crossings in cells 1–3 come
  from model misfit, and more signals would not narrow them. The bank's sizes are therefore taken from a fit
  over speed cells 2–10. How to fit a decoder that fails in a whole speed cell is a decision for the owner
  (section 4.5).

## 0. Terms used in this record

- **Decoder**: Envelope, Matched and the bank are the three decoders. Here Matched runs through the engine
  (`kz4ap-bench`) and the bank decoder in C++ through `kz4ap-bank-replay`.
- **Test case**: one recording paired with one label file. A decoder is scored on a recording.
- **Signal**: one station's transmission in a recording, with one label. A2's signals are about 100
  characters each (95–105, spaces counted).
- **Oracle channel**: one station's channel stream, opened at its labeled frequency for the whole recording.
  It is recorded once (`build/suite/dev2/channels/`, git-ignored), and every bank run replays the same stream.
- **Channel-second**: one second of one channel's stream; the unit of decoding cost.
- **S₅₀₀**: key-down carrier power over the noise power in 500 Hz, in dB SNR in 500 Hz (the labels'
  `snr_db`).
- **E/N₀ per dit**: key-down energy in one dit (E = P·T, T = 1.2 s / WPM) over the one-sided noise power
  spectral density N₀. E/N₀ = S₅₀₀ + 10·log₁₀(500 Hz × T), in dB re 1 (derived). The term 10·log₁₀(500 Hz × T)
  is 13.80 dB re 1 at 25 WPM and 16.99 dB re 1 at 12 WPM.
- **CER**: edits over reference symbols (characters, spaces counted), per signal; a ratio.
- **Crossing**: the S₅₀₀ at which the fitted CER falls to 0.10, evaluated at a speed cell's center (the
  geometric mean of its edges).
- **Interval**: a 95% percentile bootstrap interval over signals (1 000 resamples). **Half-width** = (upper −
  lower)/2, in dB SNR in 500 Hz.
- **Speed cell**: one of the plan's 10 cells, 8–80 WPM, each a factor 10^(1/10) wide (principle 2).
  **S₅₀₀ cell**: one of 14 cells 2 dB wide, −8 to +20 dB SNR in 500 Hz.
- **Signals per cell**: signals per (speed cell, S₅₀₀ cell) in A2, each drawn uniformly within its cell
  (jittered grid). It is `per_cell` in `kz4ap_synth.suites.dev2_suite`.
- **Genie bound**: the error rate of a receiver that knows timing, speed and amplitude and decides each Morse
  unit optimally (stage-1 results record, section 5.1.1; derived, with 10 units per character a heuristic).
  At CER 0.10 it lies at 11.94 dB re 1 of E/N₀ per dit (noncoherent).

## 1. Conditions

- **Code**: branch `milestone-2c-stage2` at `84413e9` (tasks D1 and D2). On the Linux machine it was built with
  CMake preset `linux` (Release, exact math). `ctest --preset linux` at that commit exited 0 (no test failed).
  The figures were redrawn at `9f751c5` (section 3.4): that commit changes presentation only.
- **Generator**: `python -m kz4ap_synth.suites generate --suite dev2 --seeds 1 --groups A2 --per-cell A2=4
  --out build/suite/dev2` (`PYTHONPATH=training`).
  - Seed 1 only: the development seed. Seeds 2 and 3 are not generated by the pilot.
  - It wrote 20 oracle recordings (`A2-awgn-c<speed cell>-<part>-s1`), each 28 signals at 1.2 kHz spacing,
    machine keying, white noise. It also wrote A2's 20 detector-path copies (`A2-detector-…`), which the
    pilot does not decode.
  - Each signal's speed is drawn uniformly in ln WPM within its speed cell, and its S₅₀₀ uniformly within its
    S₅₀₀ cell, from a generator seeded by (seed, group, cell numbers, replicate). The drawn values and their
    cells are in each label's `design`.
  - 560 signals, 34 586.2 channel-seconds. Generation took 112.2 s wall (sequential Python, both copies).
- **Matched**: `python -m kz4ap_synth.suites run --out build/suite/dev2 --bench build/linux/bench/kz4ap-bench
  --decoder matched --only '^A2-awgn-.*-s1$'`. This is `kz4ap-bench --oracle --decoder matched` on each test
  case, at the engine's defaults. It took 16.6 s wall and 17.2 s CPU (about 0.50 ms per channel-second, one
  process at a time).
- **The bank decoder**, at its current defaults (the B4g settings, `BankConfig` at `84413e9`, no `--set`):
  1. **Recording the channels**: `python -m kz4ap_proto.runner record --out build/suite/dev2 --bench … --only
     '^A2-awgn-.*-s1$'`, which is `kz4ap-bench --oracle --decoder envelope --record-channels`. This is how the
     old set's channels (`build/suite/full3/channels/`) were recorded. 8.2 s wall.
  2. **Decoding**: `kz4ap-bank-replay --out build/suite/dev2 --name pilot-bank --only '^A2-awgn-.*-s1$' --jobs
     10`. 111.6 s wall, 1 106.1 s CPU.
  3. **Scoring**: `python -m kz4ap_proto.runner score --name pilot-bank` (`kz4ap-bench --score-decoded`).

  **CPU per channel-second: 32.01 ms** (1 106.9 s of decoding CPU over 34 586.2 channel-seconds, summed from the
  decoded files). The throughput with 10 threads was **310 channel-seconds per second of wall time**. The old
  development set measured 42 ms per channel-second (Plan B results record, B4g); the CPU per
  channel-second depends on the material, and the wall times below use the A2 value.

  **Why the replay tool and not `kz4ap-bench --decoder bank`:**
  - It is the same C++ decoder.
  - Every Plan A and Plan B reference run of the bank used this path, so the results compare like for like.
  - It decodes 10 channels at once.
  - The recorded channels let every later variant decode exactly the same streams.
- **Analysis**: `python -m kz4ap_proto.experiments devset2 --out build/suite/dev2 --name pilot --decoder matched
  --decoder pilot-bank --reference matched`. It took 21.4 s wall, including both decoders' fits and 1 000
  bootstrap resamples each.
  - A second analysis, `--name pilot-c2to10 --only '^A2-awgn-c(0[2-9]|10)-.*-s1$'`, leaves out speed
    cell 1 (section 3.2).
  - Figures: `python -m kz4ap_proto.experiments figures --analysis
    build/suite/dev2/experiments/devset2-<name>.json`.
- **Raw outputs** (git-ignored, copied back from the Linux machine to `build/suite/dev2/experiments/`):
  `devset2-pilot.{json,md}` and `devset2-pilot-c2to10.{json,md}`. The figures are committed (section 3.4).

## 2. The analysis method

### 2.1 The fit model (task D2; `training/kz4ap_proto/devset2.py`)

CER is modeled as a logistic in the level L (S₅₀₀ in dB SNR in 500 Hz, or E/N₀ per dit in dB re 1) whose
center and width vary smoothly with speed v (WPM):

    CER(L, v) = c + (1 − c) · logistic(−(L − s₀(v)) / w(v))
    s₀(v) = a₀ + a₁x + a₂x²   (in L's unit),     ln(w(v) / 1 dB) = b₀ + b₁x + b₂x²,     x = ln(v / 25.30 WPM)

(s₀ and w are in dB SNR in 500 Hz on the S₅₀₀ axis and in dB re 1 on the E/N₀ axis.)

- **The floor** c = logistic(g) is one value for all speeds (0 < c < 1). It is the CER the decoder keeps at
  high S₅₀₀. **From task D4 (owner's decision after the pilot), the floor is one value per speed cell**,
  c_j = logistic(g_j) for cell j, so the model has 6 shape parameters and 10 floors (section 3.5). Sections 3.1–3.4
  are the pilot as first analyzed, with one floor.
- **25.30 WPM** = √(8 × 80) WPM is the speed range's geometric center. It is a centering for conditioning
  only (derived: the fitted curves do not depend on it).
- **The crossing** at threshold t is s₀ − w·logit((t − c)/(1 − c)), derived. There is none if c ≥ t.
- **The quadratic** in ln v is the plan's choice (principle 6): a heuristic smoothness assumption that pools
  neighboring cells.
- **Data**: each signal's CER, clipped at 1 (the model's ceiling), with its reference symbols n as weight.
  The fit uses A2's oracle signals.

### 2.2 Why weighted least squares (measured, task D2)

The fit minimizes Σ n·(CER − model)² by Levenberg–Marquardt. The alternative was the binomial likelihood of the
character errors. Task D2 compared both on synthetic A2-like data: 10 × 14 cells × 4 signals, 125–139
symbols each, with a known s₀ and w. The figures are the CER-0.10 crossing's per-cell values over 40 sets
(task D2 report):

| synthetic data | method | mean bias per cell (dB SNR in 500 Hz) | RMS per cell (dB SNR in 500 Hz) |
|---|---|---|---|
| independent character errors | least squares | −0.01 to 0.00 | 0.05–0.10 |
| independent character errors | binomial likelihood | 0.00 | 0.04–0.10 |
| plus 3% whole-signal failures (CER 0.5–1 at any S₅₀₀) | least squares | +0.01 to +0.06 | 0.18–0.45 |
| plus 3% whole-signal failures | binomial likelihood | +0.20 to +0.27 | 0.33–0.62 |

- **Both methods are equal on independent errors.**
- **They differ when whole signals fail.** A whole-signal failure is a signal that locks to a wrong speed and
  prints garbage at any S₅₀₀; it is the non-independence the plan names.
  - The binomial likelihood charges such a signal about n·ln(1/f), where f is the model's CER there. That is
    large where f is small, so a few failures move the floor and the slope.
  - Least squares bounds each signal's cost by n.
- **The review's check.** Its independent check (12 other seeds) kept the direction in every cell, but least
  squares' bias reached +0.29 dB SNR in 500 Hz in cell 10. The bias's magnitude at the speed extremes is
  therefore not pinned down.
- **Cost of the choice.** On clean data, least squares' RMS is equal or up to 0.01 dB SNR in 500 Hz larger.

The pilot shows whole-signal failures of both kinds: Matched at 63–80 WPM (CER 0.9 at +10 dB SNR in 500 Hz),
and the bank's speed cell 1.

### 2.3 The bootstrap

- **Resampling.** 1 000 resamples over signals: all of a decoder's fit signals are pooled and drawn with
  replacement. The seed is fixed, so the intervals are reproducible.
- **Warm start.** Each resample's fit starts from the full fit. This is heuristic: it is standard and fast,
  but it can understate the spread if the objective has several minima.
- **Failed resamples.** A resample that does not converge gives no value. An interval is given only if at
  least 95% of the resamples give one.
- **Intervals.** The crossings' intervals are percentile intervals (2.5% and 97.5%). A percentile interval
  need not be centered on the point estimate.

### 2.4 The E/N₀ view and the test of time-base invariance

- **The E/N₀ fit.** The same model is fitted against E/N₀ per dit. A time-base-invariant decoder has s₀
  constant in E/N₀, that is a₁ = a₂ = 0.
- **The test** is a Wald test, T = âᵀΣ⁻¹â, where Σ is the bootstrap covariance of (a₁, a₂). Its p-value is
  exp(−T/2) for 2 degrees of freedom (derived), and its 95% point is 5.99. It assumes the bootstrap
  distribution is about normal.
- **The spread** of the per-cell crossings in E/N₀ is descriptive only: noisy estimates spread above 0 even
  for an invariant decoder.

### 2.5 The pooled-set rule for paired comparisons (task D2, fix round 1)

A paired comparison is variant CER − reference CER per signal, on the signals both decoders scored. The pooled
set ("all") and the per-speed-cell sets hold **oracle-path signals only, without S2**:

- A2's detector-path copies are the same signals and noise as A2.
- S2 copies A2's cell-5 texts and timing, with new noise.

Counting either copy again would weight A2 twice and narrow the intervals. "A2 sensitivity, detector" and
"S2 stretch" are reported as groups of their own. In the pilot, only A2's oracle signals were decoded, so the
rule changes nothing here.

## 3. The pilot's results

### 3.1 Crossings per speed cell, all ten cells in the fit

S₅₀₀ at CER 0.10 in dB SNR in 500 Hz, with 95% intervals. There are 56 signals per speed cell (4 per S₅₀₀ cell).
The data span −8 to +20 dB SNR in 500 Hz. * marks a crossing outside the data, an extrapolation.

| speed cell | center (WPM) | Matched | half-width | bank | half-width | genie bound, noncoherent |
|---|---|---|---|---|---|---|
| 1 | 8.98 | −0.08 (−1.59 to +0.90) | 1.25 | +30.99 (+14.21 to +210.71) * | — | −6.31 |
| 2 | 11.30 | −0.29 (−1.25 to +0.24) | 0.75 | +4.84 (−0.23 to +7.32) | 3.78 | −5.31 |
| 3 | 14.23 | −0.20 (−0.90 to +0.32) | 0.61 | +0.00 (−1.58 to +2.46) | 2.02 | −4.31 |
| 4 | 17.91 | +0.19 (−0.44 to +0.84) | 0.64 | −1.35 (−1.99 to +0.42) | 1.21 | −3.31 |
| 5 | 22.55 | +0.87 (+0.20 to +1.63) | 0.72 | −1.69 (−2.07 to −0.37) | 0.85 | −2.31 |
| 6 | 28.39 | +1.85 (+1.15 to +2.67) | 0.76 | −1.48 (−1.82 to −0.38) | 0.72 | −1.31 |
| 7 | 35.73 | +3.21 (+2.47 to +4.01) | 0.77 | −0.79 (−1.13 to +0.32) | 0.73 | −0.31 |
| 8 | 44.99 | +5.02 (+4.09 to +6.04) | 0.98 | +0.42 (−0.21 to +1.76) | 0.99 | +0.69 |
| 9 | 56.64 | +7.49 (+5.70 to +9.16) | 1.73 | +2.43 (+1.08 to +4.27) | 1.60 | +1.69 |
| 10 | 71.30 | +10.93 (+7.14 to +14.61) | 3.74 | +6.74 (+3.47 to +9.52) | 3.03 | +2.69 |

**Matched's fit:**
- s₀ = −0.89 + 3.15x + 1.60x² dB SNR in 500 Hz, and ln(w / 1 dB) = −0.093 + 0.513x + 0.558x².
- The floor is 0.0204 (0.0155 to 0.0267).
- It converged in 11 iterations; 1 of 1 000 resamples failed.

**The bank's fit:**
- s₀ = −1.88 + 1.19x + 3.64x² dB SNR in 500 Hz, and ln(w / 1 dB) = −2.529 − 1.042x + 3.533x².
- The floor is 0.0504 (0.0330 to 0.0600).
- It converged in 49 iterations; 11 resamples failed.
- The floor is at 0.05, so the bank has no crossing at CER 0.05.

Findings:

1. **The bank fails below about 10.0 WPM, whatever the S₅₀₀** (measured).
   - Of cell 1's 20 signals at S₅₀₀ ≥ 10 dB SNR in 500 Hz, 19 have CER above 0.3; their pooled CER is 0.468.
   - Sorted by speed, every signal from 8.03 to 9.89 WPM with S₅₀₀ ≥ 4 dB SNR in 500 Hz has CER 0.25–0.74.
   - At the same S₅₀₀, every signal from 10.06 WPM up has CER at most 0.04; the one at 10.05 WPM has 0.14.
   - The edge is sharp: it lies between 9.89 and 10.05 WPM, just inside cell 1 (whose upper edge is 10.07 WPM).
     Its cause is not traced. It is a new finding for the bank (Plan B):
     the old set had no speed below 12 WPM in group A.
2. **That failure distorts the bank's whole smooth fit** (measured, figure 1, `fig1-pilot-bank`).
   - The quadratic s₀(v) and ln w(v) must bend up steeply at 9 WPM. Doing so moves cell 2's crossing to
     +4.84 dB SNR in 500 Hz, although cell 2's signals switch from failing to decoding near −1 dB SNR in
     500 Hz.
   - The one global floor rises to 0.050, against the bank's own CER of 0.005 at high S₅₀₀ in cells 2–5.
   - The bank's intervals in cells 2–3 (±3.8 and ±2.0 dB SNR in 500 Hz) therefore measure model misfit, not sampling noise.
3. **Matched has whole-signal failures at 63–80 WPM** (measured). Of cell 10's 20 signals at S₅₀₀ ≥ 10 dB SNR in
   500 Hz, 4 have CER above 0.1 and 2 above 0.3 (up to 0.94). Its fitted width w there is about 2.8 dB SNR in 500 Hz.
   Together these make cell 10's crossing the least precise in the table (±3.7 dB SNR in 500 Hz).
4. **Matched's fit is not affected by cell 1** (section 3.2): its crossings in cells 2–10 change by at most
   0.12 dB SNR in 500 Hz when cell 1 is left out.
5. **Paired difference, bank − Matched, over all 560 signals: +0.0384 (+0.0165 to +0.0605).** The difference is
   positive because of cell 1 (+0.509 per signal). From cell 5 up it is negative: cells 7 and 10 have
   intervals entirely below 0 (−0.0497 and −0.0841).

### 3.2 Crossings with speed cell 1 left out of the fit (sizes for the bank)

The same analysis on speed cells 2–10 (504 signals; `--only '^A2-awgn-c(0[2-9]|10)-.*-s1$'`). Cell 1's row is
an extrapolation from no signals and is not used.

| speed cell | center (WPM) | Matched | half-width | bank | half-width |
|---|---|---|---|---|---|
| 2 | 11.30 | −0.17 (−1.59 to +1.00) | 1.30 | +0.23 (−0.56 to +0.71) | 0.64 |
| 3 | 14.23 | −0.14 (−1.04 to +0.53) | 0.79 | −0.19 (−0.77 to +0.15) | 0.46 |
| 4 | 17.91 | +0.20 (−0.50 to +0.80) | 0.65 | −0.30 (−0.75 to +0.11) | 0.43 |
| 5 | 22.55 | +0.84 (+0.14 to +1.62) | 0.74 | −0.10 (−0.59 to +0.43) | 0.51 |
| 6 | 28.39 | +1.80 (+1.04 to +2.72) | 0.84 | +0.42 (−0.10 to +0.99) | 0.55 |
| 7 | 35.73 | +3.14 (+2.33 to +4.06) | 0.87 | +1.26 (+0.73 to +1.80) | 0.54 |
| 8 | 44.99 | +4.95 (+3.93 to +5.89) | 0.98 | +2.41 (+1.71 to +2.90) | 0.60 |
| 9 | 56.64 | +7.45 (+5.65 to +8.99) | 1.67 | +3.89 (+2.60 to +4.41) | 0.91 |
| 10 | 71.30 | +11.02 (+7.34 to +14.67) | 3.67 | +5.69 (+3.66 to +6.40) | 1.37 |

**The bank's fit without cell 1:**
- s₀ = −1.56 + 1.62x + 2.86x² dB SNR in 500 Hz, and ln(w / 1 dB) = −0.361 + 0.377x + 0.008x².
- The floor is 0.0190 (0.0152 to 0.0239).
- It converged in 14 iterations; no resample failed.
- The bank's width now hardly varies with speed, and its curves follow the data (figure 1 in
  `cells-2-10/`).
- With that fit, the bank's crossing is at or below Matched's in every cell from 3 to 10. From 35 WPM up, it is
  1.9 to 5.3 dB SNR in 500 Hz better.

### 3.3 The E/N₀ view (time-base invariance)

The crossing at CER 0.10 in E/N₀ per dit (dB re 1), at each cell center:

| speed cell (center WPM) | 1 (8.98) | 2 (11.30) | 3 (14.23) | 4 (17.91) | 5 (22.55) | 6 (28.39) | 7 (35.73) | 8 (44.99) | 9 (56.64) | 10 (71.30) |
|---|---|---|---|---|---|---|---|---|---|---|
| Matched (all cells) | 18.17 | 16.96 | 16.05 | 15.44 | 15.12 | 15.10 | 15.46 | 16.27 | 17.74 | 20.18 |
| bank (cells 2–10) | — | 17.49 | 16.06 | 14.95 | 14.15 | 13.67 | 13.51 | 13.66 | 14.14 | 14.94 |

The genie bound is 11.94 dB re 1 (noncoherent) and 10.34 dB re 1 (coherent).

1. **Neither decoder is time-base invariant** (measured; Wald test of a₁ = a₂ = 0 in the E/N₀ fit):
   - Matched: T = 41.3, p = 1.1·10⁻⁹. Without cell 1: T = 20.7, p = 3.2·10⁻⁵.
   - The bank, cells 2–10: T = 536, p = 4·10⁻¹¹⁷. With cell 1: T = 153.
2. **At the slow end, both decoders waste the energy a slower dit carries** (measured).
   - From 22.5 to 11.3 WPM a dit carries 3.0 dB re 1 more energy (10·log₁₀(22.55 / 11.30)). A time-base-invariant decoder's S₅₀₀
     crossing would fall by the same 3.0 dB SNR in 500 Hz.
   - Matched's crossing falls by 1.2 dB SNR in 500 Hz, so its E/N₀ crossing rises by 1.8 dB.
   - The bank's crossing rises by 0.3 dB SNR in 500 Hz, so its E/N₀ crossing rises by 3.3 dB.
   - The bank's deficit agrees with the stretch test's deficit of about 3 dB of E/N₀ per dit at 12 WPM
     (Plan B, B9).
3. **The bank's best is 1.6 dB of E/N₀ per dit from the genie bound** (noncoherent), at 35.7 WPM (13.51 against
   11.94 dB re 1). Matched's best is 3.2 dB from it, at 28.4 WPM.
4. **The width's speed terms** (b₁, b₂) are significant for Matched (Wald p = 3.6·10⁻⁴) and not for the
   bank without cell 1 (p = 0.13).

### 3.4 Figures

The figures are committed in `docs/plans/figures/2026-10-06-pilot/`, as PNG at 150 dpi and SVG. `captions.md`
lists the runs and the commits. The subfolder `cells-2-10/` holds the same figures from the fit over cells 2–10.

- `fig1-matched`, `fig1-pilot-bank`: CER against S₅₀₀, one panel per speed cell. Each panel shows the signals,
  the fitted curve at the cell center, and the crossing with its interval. A crossing outside the data is
  marked at the edge with its value.
- `fig2-matched`, `fig2-pilot-bank`: CER against E/N₀ per dit, every cell's fitted curve on one plot. A
  time-base-invariant decoder's curves would coincide.
- `fig3`: the crossing against speed for both decoders, with pointwise intervals, beside the genie bound.

Figures 1 and 3 were first drawn with axes reaching +211 dB and +5 000 dB SNR in 500 Hz, because the bank's
cell-1 extrapolation set the scale. From `9f751c5`, the S₅₀₀ axes span the data (plus the genie bound in
figure 3); the change is presentation only, with a test.

### 3.5 The pilot re-analyzed with one floor per speed cell (task D4)

The owner's decision after the pilot: separate floors per speed cell, so that a cell that never reaches CER 0.10
is reported as "no crossing" and cannot distort its neighbors. Code at `aaffd90` (`devset2.py`), figures at
`e882c8a`.

**What changed in the fit** (`training/kz4ap_proto/devset2.py`):

- **The model**: CER(L, v) = c_j(v) + (1 − c_j(v))·logistic(−(L − s₀(v))/w(v)), with c_j = logistic(g_j) the
  floor of speed cell j. That is 16 parameters: a₀…a₂, b₀…b₂ as before, and g₁…g₁₀. A cell with no fit signals
  keeps its floor unfitted, and the floor is reported as none.
- **Bounds on the floors** (heuristic): each floor is held within [10⁻⁵, 1 − 10⁻⁵].
  - Without the bound, a cell with no errors at high S₅₀₀ drives its floor toward 0. The objective flattens there,
    and the fit stopped on its iteration limit; the bank's pilot fit did, after 200 iterations.
  - The bound is enforced by a projected Levenberg–Marquardt step: a floor at its bound whose descent direction
    points outward is held for that iteration. With it, the bank's pilot fit converged in 18 iterations to the
    same objective (326.598287, against 326.598289 after 325 unbounded iterations).
- **Two starting points** (heuristic): every floor at 0.002, and each cell's floor from the mean CER of the
  quarter of its signals at the highest S₅₀₀. The fit keeps the lower objective.
- **"No crossing"**, at a cell's center, for one of two reasons:
  - the cell's floor is at or above the threshold, so the curve never reaches it;
  - the crossing lies above the highest S₅₀₀ among the cell's fit signals, so the curve does not reach the
    threshold within the data.

  The same rule is applied to every bootstrap resample. The summary gives the share of resamples that cross,
  and gives an interval only when at least 95% of them do. A crossing below the data (an extrapolation
  downward) is still reported, marked *.
- **The E/N₀ spread** is taken over the cells that cross.

**Measured properties on synthetic data** (`test_devset2.py`; git-ignored scripts `build/d4-tmp/`). The data are
A2-like: 10 × 14 cells × 4 signals, with the model of task D2's tests.

1. **A failing speed cell whose data have the model's form** (cell 1's floor 0.5, the same transition):
   - Over 10 sets, cells 2–10's crossings differ from the fit without cell 1 by at most 0.09 dB SNR in 500 Hz
     (mean 0.005 dB).
   - With task D2's single floor, they moved by up to 7.3 dB.
2. **A failing cell whose data do not have the model's form.** This imitates the bank: the transition is 1 dB
   higher, and the floor falls from 0.70 at 0 dB to 0.45 at +20 dB SNR in 500 Hz.
   - Cell 2 moves by +0.40 dB on average (at most 0.49 dB), and cell 3 by +0.19 dB.
   - With the single floor, cell 2 moved by up to 8.2 dB.
   - The per-cell floor removes the floor's coupling. The shared s₀(v) and w(v) still couple the cells.
3. **Whole-signal failures** (3% of signals, CER 0.5–1 at any S₅₀₀; section 2.2):
   - Least squares' mean bias of the CER-0.10 crossing is +0.05 to +0.15 dB SNR in 500 Hz per cell, with RMS
     0.38–0.75 dB (200 sets, 4 signals per cell). The binomial likelihood gives +0.18 to +0.29 dB.
   - At 48 signals per cell (20 sets): least squares +0.00 to +0.11 dB (RMS 0.08–0.17 dB), binomial +0.14 to
     +0.29 dB. Least squares stays the method.
   - The single floor gave least squares a bias of 0.01–0.06 dB with RMS 0.18–0.45 dB. Each cell's floor now
     absorbs its own few failures, at the cost of more spread at 4 signals per cell.
4. **The bootstrap's coverage** of the true crossing is 0.907 with per-cell floors and 0.895 with the single floor
   (40 sets × 10 cells, 200 resamples, the same sets). So the percentile intervals cover about 90%, not 95%,
   whichever model is used. This is a property of the interval, measured, and is not corrected here.

**The pilot's crossings at CER 0.10** (S₅₀₀ in dB SNR in 500 Hz; 95% intervals; 56 signals per speed cell),
with each cell's fitted floor (a CER). Analyses `devset2-pilot-d4` and `devset2-pilot-d4-c2to10`, run on
the pilot's files, which are now in `build/suite/dev2/pilot/` on the Linux machine.

| speed cell | center (WPM) | Matched floor | Matched | bank floor | bank, all cells | bank, cells 2–10 |
|---|---|---|---|---|---|---|
| 1 | 8.98 | 0.045 | −0.01 (−1.68 to +0.96) | 0.518 (0.460 to 0.574) | no crossing (floor) | — |
| 2 | 11.30 | 0.029 | −0.21 (−1.18 to +0.34) | 0.000 | +1.21 (−0.12 to +1.82) | +0.24 (−0.60 to +0.71) |
| 3 | 14.23 | 0.010 | −0.16 (−0.91 to +0.32) | 0.008 | +0.31 (−0.51 to +0.67) | −0.18 (−0.78 to +0.15) |
| 4 | 17.91 | 0.010 | +0.30 (−0.36 to +0.95) | 0.002 | −0.24 (−0.75 to +0.12) | −0.38 (−0.81 to +0.04) |
| 5 | 22.55 | 0.001 | +0.91 (+0.27 to +1.66) | 0.019 | −0.20 (−0.75 to +0.36) | −0.10 (−0.61 to +0.49) |
| 6 | 28.39 | 0.013 | +1.96 (+1.21 to +3.08) | 0.020 | +0.14 (−0.41 to +0.77) | +0.39 (−0.15 to +0.99) |
| 7 | 35.73 | 0.019 | +3.26 (+2.47 to +4.04) | 0.032 | +1.00 (+0.44 to +1.65) | +1.28 (+0.76 to +1.80) |
| 8 | 44.99 | 0.004 | +4.57 (+3.72 to +5.62) | 0.039 | +2.29 (+1.66 to +3.07) | +2.54 (+1.78 to +3.13) |
| 9 | 56.64 | 0.001 | +6.41 (+5.11 to +7.85) | 0.028 | +3.85 (+2.82 to +4.42) | +3.91 (+2.62 to +4.42) |
| 10 | 71.30 | 0.121 (0.048 to 0.215) | no crossing (floor) | 0.020 | +5.96 (+3.81 to +6.75) | +5.68 (+3.67 to +6.44) |

Findings:

1. **The bank's cell 1 reports no crossing** (measured). Its floor is 0.518 (0.460 to 0.574), and no resample
   crosses. With the single floor, the fit put cell 1's crossing at +31 dB SNR in 500 Hz and raised the one
   floor to 0.050.
2. **Matched's cell 10 now has no crossing at CER 0.10 either** (measured). Its floor is 0.121 (0.048 to 0.215), and
   29% of the resamples cross.
   - Matched's whole-signal failures at 63–80 WPM (finding 3 of section 3.1) set this cell's own floor.
   - With the single floor, they were shared with the other cells, and cell 10 crossed at +10.93 dB SNR in 500 Hz.
   - Whether Matched crosses CER 0.10 in cell 10 at all is not settled by 56 signals; the interval of the floor
     straddles 0.10.
3. **The bank's cell 1 still moves its neighbors through the shared s₀ and w** (measured; figure 1,
   `per-cell-floor/fig1-pilot-bank`).
   - Cell 1 rises from CER 1 to about 0.5 near 0 dB SNR in 500 Hz and then falls slowly to about 0.45 at
     +20 dB. The quadratic s₀(v) and ln w(v) bend to follow that.
   - Cell 2's crossing is +1.21 dB SNR in 500 Hz with cell 1 in the fit, against +0.24 dB without it: +0.97 dB.
     Cell 3 moves by +0.49 dB, and cells 4–10 by −0.28 to +0.28 dB.
   - With the single floor, cell 2 moved by +4.6 dB (+4.84 against +0.23, section 3.2).
   - The synthetic misfit of the same kind (above) moved cell 2 by +0.40 dB. The bank's real cell 1 departs further
     from the model's form.
   - The owner's decision ("cannot distort its neighbors") is therefore met for the floor, but not fully for the
     shape: a decision for the owner (section 5.6).
4. **Matched is not affected by its cell 1** (measured). Its crossings in cells 2–9 change by at most 0.02 dB
   SNR in 500 Hz when cell 1 is left out.
5. **Per-cell floors move Matched's fast cells down** (measured). Against the single floor, cells 8 and 9 are
   0.45 and 1.08 dB SNR in 500 Hz lower. Their own floors (0.004 and 0.001) are below the old common floor of
   0.020, which cell 10's failures had raised.
6. **The E/N₀ view** (dB re 1 of E/N₀ per dit):
   - Matched's Wald statistic is T = 39.3, p = 3·10⁻⁹.
   - For the bank with cell 1, T = 357, p = 4·10⁻⁷⁸.
   - Neither decoder is time-base invariant, as before.
7. **The paired difference** does not depend on the fit and is unchanged: bank − Matched +0.0384 (+0.0165 to
   +0.0605).

The redrawn figures are in `docs/plans/figures/2026-10-06-pilot/per-cell-floor/` (all cells) and
`per-cell-floor/cells-2-10/`. Figures 1–4 are PNG at 150 dpi and SVG, with `captions.md`. The figures of section
3.4 stay as drawn with one floor.

## 4. The proposed sizes

### 4.1 The scaling rule and its assumptions

The signals per cell needed for a half-width h_target = 0.5 dB SNR in 500 Hz, from the pilot's half-width
h₄ at 4 signals per cell:

    n = 4 · (h₄ / h_target)²     (rounded up)

- **The 1/√n assumption.** A crossing's bootstrap interval narrows as 1/√N with the total number of signals N.
  This is derived for a correctly specified model with independent signals, as the standard error of an
  M-estimator. The fit is shared across cells and all cells get the same signals per cell, so N ∝ n and
  every cell's interval scales as 1/√n.
- **It does not hold where the model misfits.** There, more signals converge on a biased answer: this is the
  bank's cells 1–3 with cell 1 in the fit. The bank's sizes therefore come from the cells 2–10 fit
  (section 3.2), and Matched's from the all-cells fit (section 3.1).
- **The pilot's half-widths are themselves estimates.** They come from one realization of 560 signals. n goes
  as h², so a 20% error in h is a 44% error in n. That uncertainty is not quantified here.
- **Percentile intervals are asymmetric.** The half-width is their mean distance, not a ± around the point.

### 4.2 Signals per cell needed per speed cell (±0.5 dB SNR in 500 Hz)

| speed cell | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 |
|---|---|---|---|---|---|---|---|---|---|---|
| Matched (h₄ from section 3.1) | 25 | 9 | 6 | 7 | 9 | 10 | 10 | 16 | 48 | **224** |
| bank (h₄ from section 3.2) | no crossing | 7 | 4 | 3 | 5 | 5 | 5 | 6 | 14 | 31 |
| the larger | 25 | 9 | 6 | 7 | 9 | 10 | 10 | 16 | 48 | 224 |

A2 draws one per-cell number for every cell, so A2's size is the largest entry of the cells it must cover.

### 4.3 The other groups

The plan sizes the other groups "in proportion to what they must resolve" (principle 5) but states no
per-group target. The pilot measured no interval for them. The proposal is:

- **B2 fading, C2 fists, I2 Farnsworth have a crossing.** Their S₅₀₀ cells (0–10 and 10–20; 2–8, 8–14 and 14–20 dB
  SNR in 500 Hz) span the region where CER crosses 0.10. Each is scaled by the same factor as A2 (n / 4 times
  its default per cell: B2 2, C2 2, I2 2). This is heuristic: it keeps the plan's relative sizes.
  - Their S₅₀₀ cells are 3 to 5 times wider than A2's. At the same factor, a crossing per (condition, speed
    cell) has 3 to 5 times fewer signals per dB SNR in 500 Hz of S₅₀₀ than A2's. If the crossing's precision depends on the
    signals per dB near the crossing, its interval would be about √3 to √5 times wider than A2's.
  - The strict alternative gives every crossing group A2's signals per dB per (condition, speed cell):
    B2 5n, C2 3n, I2 3n. Its cost is in section 4.4 (option B′).
- **D2 speed changes, E2 interference, F2 tuning, G2 and H2 QSOs have no crossing.** They measure CER at S₅₀₀
  well above it (10–20, 8–14, 0–10, 10–20 dB SNR in 500 Hz) for paired comparisons. With no stated target,
  they stay at their defaults (D2 3, E2 3, F2 2, G2 2, H2 2 per cell). The owner may set a target for them
  (for example a paired difference's interval).
- **S2 follows A2**: it is A2's cell-5 signals stretched by 2.

### 4.4 Options, sizes and run times

Channel-seconds are computed from the generator at seed 1, as the oracle runs' cost: labels per recording ×
recording duration, summed. A2's detector-path copies are listed apart, because they are decoded once, for
detection recall, and not in DEV2.

The wall time per bank run is DEV2's channel-seconds over the measured 310 channel-seconds per second (32.01 ms
CPU per channel-second, 10 threads). At the old set's 42 ms per channel-second, the times are 1.31 times longer.

| option | A2 per cell | B2, C2, I2 per cell | A2 signals | DEV2 oracle channel-seconds | bank run (wall) | meets ±0.5 dB SNR in 500 Hz |
|---|---|---|---|---|---|---|
| pilot (D1 defaults) | 4 | 2 | 560 | 138 712 | 7.5 min | — |
| C | 16 | 8 | 2 240 | 391 030 | 21 min | bank cells 2–9; Matched cells 2–8 |
| **B (proposed)** | **48** | **24** | **6 720** | **1 065 610** | **57 min** | every cell of both decoders except Matched's cell 10 (predicted ±1.08 dB SNR in 500 Hz) |
| B′ (strict density for B2, C2, I2) | 48 | 240, 144, 144 | 6 720 | 4 212 372 | 3.8 h | as B, with B2, C2 and I2 at A2's density |
| A (every cell, both decoders) | 224 | 112 | 31 360 | 4 758 755 | 4.3 h | every cell with a crossing |

Groups at option B (seed 1):

| group | per cell | recordings | scored signals (channels) | channel-seconds |
|---|---|---|---|---|
| A2 | 48 | 210 | 6 720 | 419 264 |
| B2 | 24 | 60 | 1 920 | 149 056 |
| C2 | 24 | 120 | 3 600 | 243 420 |
| D2 | 3 | 10 | 108 | 7 200 |
| E2 | 3 | 10 | 90 (+90 neighbors) | 11 034 |
| F2 | 2 | 20 | 80 | 4 640 |
| G2 | 2 | 10 | 20 QSOs | 10 612 |
| H2 | 2 | 10 | 40 station labels | 20 608 |
| I2 | 24 | 27 | 864 | 123 232 |
| S2 | follows A2 | 21 | 672 | 76 544 |
| **DEV2 (oracle)** | | **498** | | **1 065 610** |
| A2 detector-path copies | 48 | 210 | 6 720 | 419 264 |

Other costs at option B:

- **Generation.** The pilot generated 69 172 channel-seconds (both copies) in 112.2 s wall (sequential
  Python). Scaled by channel-seconds (a heuristic), seed 1 with the detector copies takes about 40 min, and all
  three seeds about 2 h.
- **Disk.** The pilot measured 6.6 kB of WAV per channel-second generated and 11.5 kB of recorded channel
  stream per oracle channel-second. That gives about 10 GB of WAV per seed and 12 GB of channels for seed 1.
  The Linux machine has 853 GB free.
- **Matched.** At 0.50 ms per channel-second, one test case at a time: about 9 min per run.

**At option B, the predicted half-widths** are h₄·√(4/48) = 0.289·h₄, in dB SNR in 500 Hz:

- Matched: 0.36, 0.22, 0.18, 0.18, 0.21, 0.22, 0.22, 0.28, 0.50 and 1.08 in cells 1–10.
- The bank: 0.18, 0.13, 0.12, 0.15, 0.16, 0.15, 0.17, 0.26 and 0.40 in cells 2–10.

The bank is the decoder Plan B tunes, and it reaches ±0.5 dB SNR in 500 Hz from 14 per cell. Matched's cells 8–10 set
option B's size.

### 4.5 Decisions for the owner

a. **The size:**
   - Option B, 48 per cell: proposed; 57 min per bank run. It meets the target everywhere except Matched at
     63–80 WPM.
   - Option A, 224 per cell: the brief's rule taken literally, 4.3 h per run.
   - Option C, 16 per cell: 21 min. It meets the target for the bank in cells 2–9 and for Matched in cells 2–8.

   **Recommendation: B.** It meets the target for every (decoder, cell) pair but one. Matched's 80 WPM cell
   costs 4.7 times more for one reference decoder's least important cell.
b. **The other groups:** the factor rule (B) or A2's density (B′, 3.8 h per run), and whether D2, E2, F2 and
   G2/H2 get a precision target.
c. **How to fit a decoder that does not cross in a whole speed cell** (the bank below 10 WPM). The plan's
   model has one floor for all speeds. The options are:
   1. Leave such cells out of the fit and report them as "no crossing, CER x at +20 dB SNR in 500 Hz". This
      is what section 3.2 did by hand; it would need a rule in `devset2.py`.
   2. A speed-dependent floor (a model change).
   3. Keep the model and accept its bias in the neighboring cells.

   **Recommendation: 1.** It is the smallest change, and it keeps the crossings of the cells that do cross
   unbiased. Option 2 cannot represent the sharp edge near 10.0 WPM either.
d. **The bank's failure below about 10.0 WPM** is a new item for Plan B (cause not traced).

**Owner's decisions (2026-10-06):** a. option B, 48 signals per cell; b. B2, C2 and I2 scaled by the same factor,
D2–H2 at their defaults; c. separate floors per speed cell (section 3.5); d. trace the bank's failure below
10 WPM before the full generation's bank runs are interpreted.

## 5. The new set's baseline (task D4)

**Answer.** DEV2 was generated at option B (seeds 1–3), and on seed 1 the three decoders were run at their
current defaults: Envelope and Matched through `kz4ap-bench` with oracle channels, and the bank through
`kz4ap-bank-replay`. Matched also ran once on A2's detector-path copies, for detection recall.

- **Precision.** With 672 signals per speed cell, the bank's crossing (S₅₀₀ at CER 0.10) is known to ±0.11 to
  ±0.29 dB SNR in 500 Hz in speed cells 2–10. These 95% percentile intervals cover about 90% (measured at 4
  signals per cell; section 5.6 c), so calibrated half-widths would be about 1.19 times larger.
- **The bank against Matched.**
  - From 22.5 to 57 WPM the bank's crossing lies 0.65–1.57 dB SNR in 500 Hz below Matched's. At 17.9 WPM their
    intervals overlap, so these unpaired intervals do not resolve the difference.
  - At 63.5–80 WPM the bank crosses at +6.44 dB SNR in 500 Hz, and Matched does not reach CER 0.10 at all.
  - At 11.3 and 14.2 WPM Matched is better by 1.16 and 0.58 dB SNR in 500 Hz.
- **Pooled over every oracle-path signal of the set**, the bank's CER is lower than Matched's: paired difference
  −0.0396 (−0.0448 to −0.0342).
- **In speed cell 1 (8.0–10.1 WPM) the bank fails at every S₅₀₀**, as in the pilot; its floor is 0.525. **That
  failure is being traced separately, and the bank's results below 10 WPM are not interpreted here.**

### 5.1 Conditions and run times

All times are from the Linux machine. Another investigation shared it, using up to 5 of its 10 cores, so every
step here ran with 5 workers.

- **Code.** Branch `milestone-2c-stage2`. The engine was unchanged since `84413e9` (built there with preset `linux`,
  task D3). The analysis code is at `aaffd90`, and the figures at `af8a9b3`.
- **Generation**: `kz4ap_synth.suites.dev2_suite(3, {"A2": 48, "B2": 24, "C2": 24, "I2": 24})`, written by
  the committed `write_suite`.
  - The recordings were written 5 at a time by a git-ignored driver (`build/d4-gen.py`). Each recording was
    written by `write_suite` alone; the manifest was then written for the whole list.
  - This gives the same files as `python -m kz4ap_synth.suites generate --suite dev2 --seeds 3 --per-cell A2=48
    --per-cell B2=24 --per-cell C2=24 --per-cell I2=24 --out build/suite/dev2`, except for the order of writing.
  - It wrote 2 124 recordings: per seed, 498 DEV2 oracle recordings and A2's 210 detector-path copies. That is
    11 GB of WAV per seed, 32 GB in all.
  - It took 1 700.3 s wall and 8 417 s of worker CPU.
  - Seeds 2 and 3 are generated but not decoded (plan, principle 8).
  - The pilot's files were moved to `build/suite/dev2/pilot/`, where they remain a complete suite folder
    (section 3.5).
- **Seed 1's DEV2 holds 14 204 oracle channels and 1 065 742.1 channel-seconds** (the bench's own count).
  Section 4.4 estimated 1 065 610 from labels × durations.
- **The runs.** Each step ran 5 test cases at a time through the committed functions, one test case per call
  (git-ignored driver `build/d4-run.py`; job script `build/d4-refs.sh`).

| step | command (per test case) | wall | CPU | CPU per channel-second |
|---|---|---|---|---|
| Matched, DEV2 | `suites.run_suite`: `kz4ap-bench --oracle --decoder matched` | 106.5 s | 521.1 s decoding | 0.489 ms |
| Matched, A2's detector-path copies (330 042.5 channel-seconds) | `kz4ap-bench --decoder matched` (no `--oracle`) | 44.7 s | 218.3 s decoding | 0.661 ms |
| Envelope, DEV2 | `kz4ap-bench --oracle --decoder envelope` (results folder `baseline`) | 52.0 s | 248.1 s decoding | 0.233 ms |
| recording the oracle channels | `runner.record`: `kz4ap-bench --oracle --decoder envelope --record-channels` | 55.2 s | 275 s (processes) | — |
| the bank, DEV2 | `kz4ap-bank-replay --name d4-bank --jobs 5`, at its defaults (B4g, no `--set`) | 6 490.1 s (1 h 48 min) | 32 410.8 s decoding | **30.41 ms** |
| scoring the bank | `runner.score`: `kz4ap-bench --score-decoded` | 5.8 s | 29 s | — |
| analysis, 3 decoders | `experiments devset2 --name d4 --decoder baseline --decoder matched --decoder d4-bank --reference matched` | 164.7 s | 165.0 s | — |

- **The bank's throughput** was 164 channel-seconds per second of wall time at 5 threads, about 33 per thread.
  - That is consistent with the pilot's 310 at 10 threads (31 per thread).
  - At 10 threads a DEV2 bank run would take about 57 min, as section 4.4 predicted.
- **The CPU per channel-second** (30.41 ms) is 5% below the pilot's 32.01 ms on A2 alone. DEV2 holds other material.
- **A second analysis**, `--name d4-c2to10 --only '^A2-awgn-c(0[2-9]|10)-.*-s1$'` (108.7 s wall), fits speed
  cells 2–10 only. It measures how much cell 1 moves the others (section 5.2, finding 3).
- **Raw outputs** (git-ignored) are in `build/suite/dev2/` on the Linux machine: WAVs, labels, `channels/` (12 GB),
  `proto/d4-bank/`, `results/{baseline,matched,d4-bank}/` and `experiments/`.
  - The analyses `devset2-d4.{json,md}` and `devset2-d4-c2to10.{json,md}` were copied to the local
    `build/suite/dev2/experiments/`.
  - Their summaries are committed as `summary.md` beside the figures (section 5.7).

### 5.2 Crossings per speed cell

The table gives S₅₀₀ at CER 0.10 in dB SNR in 500 Hz, with 95% bootstrap intervals over signals (1 000 resamples).

- There are 672 signals per speed cell (48 per S₅₀₀ cell), and each cell's floor is fitted.
- "No crossing" follows section 3.5's rule.
- "Envelope" is results folder `baseline`.

| speed cell | center (WPM) | Envelope | Matched | bank | bank, fit over cells 2–10 | genie bound, noncoherent |
|---|---|---|---|---|---|---|
| 1 | 8.98 | +11.19 (+10.41 to +11.84) | −0.80 (−1.19 to −0.47) | no crossing (floor 0.525) | — | −6.31 |
| 2 | 11.30 | +9.16 (+8.76 to +9.48) | −0.52 (−0.81 to −0.29) | +0.64 (+0.43 to +0.86) | +0.46 (+0.30 to +0.66) | −5.31 |
| 3 | 14.23 | +7.51 (+7.23 to +7.75) | −0.42 (−0.60 to −0.27) | +0.16 (+0.01 to +0.31) | +0.07 (−0.06 to +0.20) | −4.31 |
| 4 | 17.91 | +6.80 (+6.58 to +7.02) | +0.18 (−0.01 to +0.33) | −0.02 (−0.13 to +0.08) | −0.04 (−0.15 to +0.06) | −3.31 |
| 5 | 22.55 | +6.79 (+6.55 to +7.02) | +0.82 (+0.64 to +0.99) | +0.17 (+0.06 to +0.28) | +0.21 (+0.10 to +0.31) | −2.31 |
| 6 | 28.39 | +7.63 (+7.33 to +7.99) | +1.73 (+1.52 to +1.93) | +0.74 (+0.60 to +0.87) | +0.81 (+0.68 to +0.93) | −1.31 |
| 7 | 35.73 | +13.60 (+11.97 to +17.96) | +2.68 (+2.48 to +2.88) | +1.73 (+1.57 to +1.88) | +1.80 (+1.65 to +1.94) | −0.31 |
| 8 | 44.99 | +14.68 (+13.67 to +16.47) | +3.87 (+3.67 to +4.08) | +2.98 (+2.81 to +3.12) | +3.03 (+2.88 to +3.19) | +0.69 |
| 9 | 56.64 | no crossing (not within the data: fitted CER 0.118 at +20 dB; floor 0.085) | +6.03 (+5.54 to +6.64) | +4.46 (+4.28 to +4.64) | +4.46 (+4.28 to +4.66) | +1.69 |
| 10 | 71.30 | no crossing (floor 0.264) | no crossing (floor 0.126, 0.102 to 0.152) | +6.44 (+6.14 to +6.73) | +6.38 (+6.08 to +6.69) | +2.69 |

The floors (CER) in speed cells 1–10:

- Envelope: below 10⁻⁴ in cells 1, 3, 4 and 5; 0.010, 0.005, 0.084, 0.062, 0.085 and 0.264 in cells 2 and 6–10.
- Matched: 0.053, 0.046, 0.010, 0.010, 0.003, 0.007, 0.003, 0.005, 0.046 and 0.126.
- The bank: 0.525, 0.006, 0.013, 0.010, 0.012, 0.015, 0.024, 0.027, 0.022 and 0.030.

The crossings at CER 0.05 are in `summary.md`.

Findings (all measured):

1. **The bank needs the least S₅₀₀ from 22.5 to 80 WPM; Matched needs the least at 11–14 WPM.**
   - Bank minus Matched in cells 2–9 is +1.16, +0.58, −0.20, −0.65, −0.99, −0.95, −0.89 and −1.57 dB SNR in
     500 Hz. In cell 4 the two intervals overlap (−0.13 to +0.08 against −0.01 to +0.33); these unpaired
     intervals do not resolve the difference. The paired CER in speed cell 4 favors the bank (section 5.4).
   - In cell 10 (63.5–80 WPM) the bank crosses at +6.44 dB SNR in 500 Hz, and Matched does not cross.
2. **Matched does not reach CER 0.10 at 63.5–80 WPM at any S₅₀₀**: cell 10's floor is 0.126 (0.102 to 0.152).
   - The pilot's single-floor fit gave +10.93 dB SNR in 500 Hz here.
   - The pilot's per-cell refit (section 3.5) found the floor's interval straddling 0.10. With 12 times the
     signals, it lies above 0.10.
3. **The bank's cell 1 moves its neighbors little at this size.**
   - Cell 2's crossing is +0.64 dB SNR in 500 Hz with cell 1 in the fit and +0.46 dB without it: +0.18 dB SNR in
     500 Hz, about one interval half-width (±0.21 dB SNR in 500 Hz).
   - Cell 3 moves by +0.09 dB SNR in 500 Hz, and cells 4–10 by at most 0.07 dB SNR in 500 Hz.
   - In the pilot, the same comparison gave +0.97 dB SNR in 500 Hz (section 3.5).
4. **The shared quadratic shape couples the cells in general, not only through a failing cell.**
   - For Envelope, which has no failing cell, leaving out cell 1 moves cell 2 by +0.64 dB and cell 3 by +0.30 dB
     SNR in 500 Hz.
     Envelope's s₀(v) bends strongly (a₂ = +4.38 dB SNR in 500 Hz).
   - Crossings near the ends of a fitted range therefore depend on which cells are fitted, here by up to about
     half a dB SNR in 500 Hz.
   - This is a property of the quadratic-in-ln v model, and the bootstrap intervals do not contain it.
5. **The precision reached**, as half-widths of the 95% intervals at CER 0.10 in dB SNR in 500 Hz, with section
   4.4's prediction in brackets:
   - The bank, cells 2–10: 0.21, 0.15, 0.11, 0.11, 0.13, 0.15, 0.15, 0.18, 0.29 [0.18, 0.13, 0.12, 0.15, 0.16,
     0.15, 0.17, 0.26, 0.40].
   - Matched, cells 1–9: 0.36, 0.26, 0.17, 0.17, 0.17, 0.21, 0.20, 0.20, 0.55 [0.36, 0.22, 0.18, 0.18, 0.21,
     0.22, 0.22, 0.28, 0.50].
   - The ±0.5 dB SNR in 500 Hz target is met everywhere a crossing exists, except Matched's cell 9 (0.55).
   - **Caveat:** these are 95% percentile intervals, whose coverage measured about 0.90 (40 synthetic sets at 4
     signals per cell, section 3.5; not measured at 48). Calibrated to 95% under a normal approximation, the
     half-widths would be about 1.96/1.645 = 1.19 times larger: the bank 0.13–0.35, Matched's cell 9 about 0.65
     dB SNR in 500 Hz (derived). The conclusions of this section do not change.
   - The 1/√n scaling of section 4.1 held to within about 0.1 dB SNR in 500 Hz.
   - Envelope's intervals in cells 7 and 8 (±3.0 and ±1.4 dB SNR in 500 Hz) are wide, because its floors there (0.084 and
     0.062) lie close to 0.10.
6. **Envelope** needs 6.8–9.2 dB SNR in 500 Hz from 11 to 32 WPM, 5.9–9.7 dB SNR in 500 Hz more than Matched.
   - Above 32 WPM its floor rises to 0.06–0.26.
   - From 50.5 WPM up it does not reach CER 0.10.

### 5.3 The E/N₀ view (time-base invariance)

The table gives the crossing at CER 0.10 in E/N₀ per dit, dB re 1, at each cell's center. The genie bound is
11.94 dB re 1 (noncoherent) and 10.34 dB re 1 (coherent).

| speed cell (center WPM) | 1 (8.98) | 2 (11.30) | 3 (14.23) | 4 (17.91) | 5 (22.55) | 6 (28.39) | 7 (35.73) | 8 (44.99) | 9 (56.64) | 10 (71.30) |
|---|---|---|---|---|---|---|---|---|---|---|
| Envelope | 29.44 | 26.41 | 23.76 | 22.05 | 21.04 | 20.88 | 25.85 | 25.93 | — | — |
| Matched | 17.45 | 16.73 | 15.83 | 15.43 | 15.08 | 14.98 | 14.93 | 15.12 | 16.28 | — |
| bank | — | 17.89 | 16.41 | 15.23 | 14.43 | 13.99 | 13.98 | 14.23 | 14.71 | 15.69 |

1. **No decoder is time-base invariant.** The Wald test of s₀'s speed terms in the E/N₀ fit gives:
   - Matched T = 1 052, p = 4·10⁻²²⁹;
   - the bank T = 8 210;
   - Envelope T = 2 491.
2. **The spread of the crossings** (descriptive), in dB of E/N₀ per dit:
   - Matched 2.52 (2.04 to 2.97) over cells 1–9;
   - the bank 3.91 (3.65 to 4.23) over cells 2–10;
   - Envelope 8.56 (7.73 to 9.50) over cells 1–8.
3. **The bank's deficit at the slow end is confirmed with narrow intervals.**
   - From 22.5 to 11.3 WPM, the bank's E/N₀ crossing rises 3.46 dB re 1 (14.43 to 17.89).
   - Matched's rises 1.65 dB re 1 (15.08 to 16.73).
   - A time-base-invariant decoder's would not change.
   - The rise agrees with the pilot (3.3 dB) and with the stretch test B9 (about 3 dB at 12 WPM).
4. **The bank's best is 13.98 dB re 1 at 35.7 WPM**, 2.04 dB from the noncoherent genie bound. Matched's best is
   14.93 dB re 1, also at 35.7 WPM, 2.99 dB from the bound.

### 5.4 Paired comparisons

The table gives the CER difference per signal, variant minus Matched, with 95% bootstrap intervals over signals
and, in square brackets, over test cases. The pooled set and the per-speed-cell sets hold oracle-path signals only,
without S2 (section 2.5).

| set | signals | bank − Matched | Envelope − Matched |
|---|---|---|---|
| **all (oracle path)** | 13 442 | **−0.0396 (−0.0448 to −0.0342)** [−0.0605 to −0.0186] | +0.2515 (+0.2393 to +0.2637) [+0.2065 to +0.3005] |
| A2 sensitivity | 6 720 | +0.0486 (+0.0425 to +0.0543) | +0.3604 (+0.3386 to +0.3805) |
| B2 fading | 1 920 | −0.0867 (−0.0990 to −0.0747) | +0.1668 (+0.1451 to +0.1910) |
| C2 fists | 3 600 | −0.0895 (−0.0972 to −0.0812) | +0.1058 (+0.0959 to +0.1160) |
| D2 speed changes | 108 | −0.0045 (−0.0316 to +0.0205) | +0.3285 (+0.2127 to +0.4443) |
| E2 interference | 90 | **+0.4076 (+0.2371 to +0.5970)** | +0.7240 (+0.5454 to +0.9110) |
| F2 tuning | 80 | −0.0482 (−0.0953 to −0.0027) | +0.2792 (+0.2029 to +0.3539) |
| G2 QSO, same track | 20 QSOs | −0.1115 (−0.2239 to +0.0188) | +0.1975 (−0.0350 to +0.4915) |
| H2 QSO, separate tracks | 40 station labels | −0.0511 (−0.1296 to +0.0189) | +1.7925 (+1.0607 to +2.5642) |
| I2 Farnsworth | 864 | **−0.4612 (−0.4874 to −0.4356)** | +0.0682 (+0.0284 to +0.1089) |
| S2 stretch (its own group) | 672 | +0.0496 (+0.0306 to +0.0701) | +0.9104 (+0.7897 to +1.0433) |
| speed cell 1, all groups | 1 255 | +0.3280 (+0.3028 to +0.3539) | +0.5494 (+0.4767 to +0.6274) |
| speed cells 2–10, all groups | 1 252–1 618 each | −0.0144 to −0.1512; every interval below 0 except cell 2's (−0.0300 to +0.0028) | +0.14 to +0.40 |

Findings (measured):

1. **Over the whole set the bank is better than Matched, despite its cell-1 failure.**
   - Pooled, its CER is 0.040 lower.
   - In A2 alone it is 0.049 higher, because of cell 1: over all groups, the bank is 0.33 worse there.
2. **The bank is clearly better with Farnsworth timing** (I2, −0.46), and on fading and keying styles (B2 and C2,
   about −0.09). The old set's group I gave −0.470 (−0.510 to −0.430) for the bank at milestone 2c
   (`docs/plans/2026-10-03-milestone-2c-bank-results.md`, section 4.3).
3. **The bank is clearly worse with a neighboring station** (E2, +0.41).
   - In E2 the wanted signal is at 8–14 dB SNR in 500 Hz. The neighbor is 0–120 Hz above it, at −6 to +12 dB
     relative to the wanted signal's key-down power.
   - **This deficit is not new.** On the old set, group E gave bank − Matched +0.299 (+0.142 to +0.481) for the
     bank at milestone 2c (`docs/plans/2026-10-03-milestone-2c-bank-results.md`, section 4.3); group E was at
     25 WPM, with the neighbor 20–150 Hz away. E2 confirms the deficit with the bank at its B4g defaults, across
     10 speed cells and offsets of 0–120 Hz, at a similar size.
4. **F2 has 40 signals beyond the oracle anchor's ±12 Hz** (a drift excursion above 12 Hz), as task D1 expected.
   They are counted, not excluded.

### 5.5 Detection recall (Matched, through the detector path)

The table gives the A2 detector-path labels detected, out of 48, per speed cell and S₅₀₀ cell. The Wilson 95%
intervals are in `summary.md` and figure 5.

| S₅₀₀ cell (dB SNR in 500 Hz) | cell 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 |
|---|---|---|---|---|---|---|---|---|---|---|
| −8 to −6 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| −6 to −4 | 21 | 5 | 8 | 5 | 5 | 0 | 0 | 0 | 0 | 0 |
| −4 to −2 | 48 | 46 | 45 | 43 | 40 | 34 | 28 | 21 | 3 | 2 |
| −2 to 0 | 48 | 48 | 48 | 48 | 48 | 48 | 48 | 47 | 37 | 33 |
| each 2 dB cell from 0 to +20 | 48 | 48 | 48 | 48 | 48 | 48 | 48 | 48 | 48 | 48 |

1. **The detector finds every signal from 0 dB SNR in 500 Hz up, at every speed**, and from −2 dB SNR in 500 Hz up to
   40 WPM (one miss at 40–50 WPM).
2. **The detector's edge moves up by about 2 dB SNR in 500 Hz from 8 to 80 WPM.** At −4 to −2 dB SNR in 500 Hz
   it finds all 48 signals at
   9 WPM, but 2 at 71 WPM.
3. **Detection does not limit the oracle crossings measured here.** In every speed cell, recall reaches 48 of 48
   at least 1 dB SNR in 500 Hz below Matched's crossing at CER 0.10.
   - Cells 1–5: recall is complete from −4 or −2 dB SNR in 500 Hz, against crossings at −0.8 to +0.8 dB SNR in
     500 Hz.
   - Cells 9 and 10: recall is complete from 0 dB SNR in 500 Hz, against Matched's +6.0 and the bank's +4.5 and
     +6.4 dB SNR in 500 Hz.
   - The margin grows with speed.

### 5.6 Decisions and open items for the owner

a. **The bank below 10 WPM** (cell 1: floor 0.525, no crossing; paired +0.33 over every group's cell-1
   signals). The trace runs separately, and the bank's reference run here is at its current defaults. After the
   trace, these should be read again:
   - the bank's cell-1 numbers;
   - everything pooled over cell 1: every group's paired mean (each group spans the 10 speed cells; A2's most,
     the others by roughly a tenth of the bank's cell-1 excess, an estimate), and the pooled mean.
b. **How strictly "a failing cell cannot distort its neighbors" should hold.**
   - Per-cell floors removed the coupling through the floor.
   - The shared quadratic s₀(v) and w(v) still move the bank's cell 2: by +0.18 dB SNR in 500 Hz at 48 signals per
     cell (about one half-width), and by +0.97 dB SNR in 500 Hz in the pilot.
   - The options:
     1. Accept it as is.
     2. Fit without the cells whose floor is at or above 0.10: a two-pass rule, D3's option 1.
     3. Make s₀ and ln w more flexible in ln v, for example cubic or a spline. This would also reduce the coupling
        seen for Envelope (section 5.2, finding 4).
   - **Recommendation: 1 for now, revisited after the trace.** If the trace fixes the bank's cell 1, the question
     disappears for the bank.
c. **The percentile bootstrap covers about 90%, not 95%**, whichever floor model is used (measured, section 3.5).
   The intervals in this record are therefore somewhat too narrow. A calibrated alternative, for example the
   bootstrap over test cases or a studentized interval, is a separate decision.
d. **E2 (interference).** The bank's +0.41 against Matched is the largest group effect against the bank. It is a
   candidate for Plan B's next steps, alongside the deficit at slow speeds (section 5.3, finding 3).

### 5.7 Figures

The figures are in `docs/plans/figures/2026-10-06-dev2-baseline/`, as PNG at 150 dpi and SVG. Beside them are
`captions.md`, which lists the runs and the commits, and `summary.md`, the analysis summary. The figures in
the folder's top level were redrawn in task D4's fix round 1. In that redraw:
- figure 3 breaks every curve and band at each cell edge;
- the point clouds of figures 1 and 2 are raster images inside the SVGs;
- the legends name the decoders (Envelope, Matched, bank) instead of their results folders.

The subfolder `cells-2-10/` holds the fit over cells 2–10. Its `summary.md` is the record's source for that
comparison. Its figures are from the first drawing, which joined the curves across cell edges, and are no longer
redrawn: duplicate figures are not produced by default from now on. They stay committed until the owner decides.
The same holds for `docs/plans/figures/2026-10-06-pilot/per-cell-floor/cells-2-10/`.

- `fig1-baseline`, `fig1-matched`, `fig1-d4-bank`: CER against S₅₀₀ per speed cell. Each panel shows the signals,
  the fit at the cell's center, and either the crossing with its interval or "no crossing" with the floor.
- `fig2-…`: CER against E/N₀ per dit, every speed cell's fitted curve.
- `fig3`: the crossing against speed for the three decoders, beside the genie bound.
  - An x near the top marks a cell without a crossing, one row of marks per decoder.
  - Each decoder's curve and band are drawn one speed cell at a time and break at every cell edge. The floor is one
    value per speed cell, so the crossing can jump at an edge (Envelope at 31.85 and 40.09 WPM). A jump between
    cells is a property of the model's cells, not a measured slope.
- `fig4`: paired differences by group, bank − Matched and Envelope − Matched.
- `fig5-matched`: detection recall through the detector path against S₅₀₀ per speed cell, beside Matched's oracle
  CER.
