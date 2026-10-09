# Bell decoder, stage 1: Results Record

Plan: `docs/plans/2026-10-07-bell-decoder-stage-1.md` (owner's go-ahead, 2026-10-08). Branch `bell-stage-1`. Every
number here is **measured** unless marked otherwise, and is computed from the committed score tables named with it.

**Answer (V2, Task 3): FAIL, and the task is stopped for two rulings (plan §5.3).** Bell's own program, compiled with
gfortran and run on his simulator at the settings of his Tables XV and XVI as the reference sheet reads them, decodes
far worse than his tables. Under reading C of decision D9 (the rule's reading), **28 of the 30 cells are
inconsistent** with his values (criterion (i) allows 5), and our error is above his in **all 30 cells** (two-sided
sign test p = 1.9·10⁻⁹; criterion (ii) needs p ≥ 0.05). Readings A and B fail as well (28 and 22 inconsistent
cells). Two findings need a ruling before V2 can be completed (section 3.6):

1. **SAVEP's zero sum is reached** (plan §9 item 6): 270 of the 1 440 runs stop on it, in 19 of the 30 cells under
   reading C. The traces show both routes the reference sheet predicted: one live path is left (by a forced
   decision, or by the likelihoods), and every successor of it other than staying has a transition probability of at
   most 10⁻⁴.
2. **Our error curve sits 1.9–3.9 dB S₁₀₀ to the right of his** (keyboard Morse, Table XV; median of the seven
   cells with a nonzero value: 2.7 dB S₁₀₀). The reference sheet ((d), (j) item 21) names the noise density's
   convention, one-sided or two-sided (3.01 dB), as the first suspect. Nothing here applies it.

## 1. Conditions

- **Machine.** The Linux machine: Linux (Ubuntu 22.04), 10-core Intel Xeon (Ice Lake). gfortran: GNU Fortran 16.2.0
  (conda-forge gcc 16.2.0-7), installed with micromamba 2.9.0 inside the repository's `build/` folder.
- **Program.** `reference/bell1977/bell1977.f` at commit 0ea2b05, built by `reference/bell1977/build.sh` with
  `-std=legacy -fdec -fno-automatic -finit-local-zero -fdefault-real-8 -fdefault-double-8 -O2` and
  `-ffpe-trap=invalid,zero` (the binary `bell1977-fpe`). The trap changes no result: on the 293 runs of a 5-run pilot
  of every cell that did not stop, `bell1977` and `bell1977-fpe` wrote byte-identical TRANSL.TXT and EVENTS.TXT; and
  builds at `-O0` and `-O2` gave identical scores on a 120-run pilot (both pilots built before change 2's warm-up
  was added; not committed). The changes to the listing, each marked in the
  code, are listed in `reference/bell1977/README.md`.
- **Letter tree.** `MORSEM` written by `kz4ap-bell-tables` (rule N1, the 47-symbol alphabet; Task 2, commit ae3f28f).
- **Settings.** `reference/bell1977/run/*.settings`, the values of reference sheet (e): FREQ 1000 Hz; NOISE =
  20·10^(−S₁₀₀/10) AU² per 8 kHz sample (S₁₀₀ with the one-sided noise density N₀ = NOISE/4000 Hz; derived, sheet
  (d)); the fading GAMMA 0.9992388097 per sample, BVAR 2.574960·10⁻⁶ AU² per sample (σ 0.0411 AU, our choice);
  AVAR 0, PHIVAR 0, FCHIRP 0 Hz, TCHIRP 10 ms; ESEP 1, 3, 1, 3, 7, 14 dots; keyboard Morse with EDEV 0 and KEY's
  printed law; the fists with the spreads of readings A, B (printed uniform law) and C (truncated Gaussian, change 9).
- **Runs.** Each of the 30 cells of §5.1 (and each reading for Table XVI): 20 runs, one session each, seed 1000·k + r
  for cell k = 1 … 30 and run r = 1 … 20 (the three readings of a cell share their seeds). A session lasts
  (1.25 × 3490.6/RATE + 5) s, 224 s at 20 WPM, 151 s at 30 WPM, 93 s at 50 WPM, so that at least 202 letters are sent.
- **Scoring.** The reference is the first 200 letters KEY sent, each labeled by the code it keyed (KEY's column C
  keys B and counts as B). The decoded text is the letters TRANSL printed whose deciding trellis stage lies before
  T_cut = (t₂₀₁ + t₂₀₂)/2 + 14.5 ms, where t_k is the start of letter k's first mark (heuristic, `v2.py`). Both are
  scored by the project's C++ scorer (`kz4ap-bell-score`, `bench/src/scoring.cpp`): the **Bell-style count** of
  decision D10 (`docs/signal-processing.md` A.11) for the rule, and the character CER beside it, each over the 200
  reference letters. A run that stopped on the trap has no score; the rule leaves it out of its cell and counts it.
- **Rule.** Plan §5.2 as written, implemented in `reference/bell1977/v2.py rule`: per cell the pooled rate p̂ and the
  unbiased variance s² of the 20 (or fewer) per-run rates; Bell's x = 2·b errors of 200 letters for his b %; a
  beta-binomial over 200 letters with mean p̂ and its overdispersion from s² by moments (binomial when s² ≤
  p̂(1 − p̂)/200); consistent if P(X ≤ x) ≥ 0.05 and P(X ≥ x) ≥ 0.05, or P(X = 0) ≥ 0.05 for x = 0.
- **Score tables** (committed with this record): `docs/plans/data/2026-10-07-bell-stage-1/v2-fortran.csv`, one row
  per run of the 72 settings files (1 440 rows); `.../v2-fortran-kam-grid.csv`, the S₁₀₀ grid of section 3.5
  (480 rows). Columns: table, speed (WPM), sender, reading, S₁₀₀ (dB S₁₀₀), Bell's value (%), run, seed, zero_sum
  (1: stopped on the trap), reference letters, Bell-style count, character edits, reference word spaces, space edits,
  and the two rates.

## 2. V1: the transcription and the letter tree

- **Transcription** (Task 1a; commits 1709ffe, 69a1c1d, bec54ed): two independent readings of pp. 159–191, 1 715 of
  1 814 lines agreeing; every difference settled on the page image; a review checked five pages character by
  character and every numeric constant, and found two wrong constants (p. 163 02300, p. 181 30800), both corrected.
  No difference is left unresolved (`reference/bell1977/transcription-log.md`).
- **Reference sheet** (Task 1b; commits 080196d, 532c9a6, a0cbe71): `docs/research/bell-1977-reference.md`, reviewed
  by two reviewers and re-reviewed.
- **Letter tree** (Task 2; commits ae3f28f, 69906c1): `MORSEM` by rule N1 passes the sheet's seven tests in the C++
  tests, among them "every symbol decodes" for all 47 symbols from its noiseless element sequence.
- **The compiled program** (Task 3, commit 0ea2b05): its statements equal the listing's, blanks aside, except at the
  marked changes (a statement-by-statement comparison). The **smoke test** (`run/smoke.settings`: keyboard Morse,
  20 WPM, no noise, 200 letters, 34 distinct letters and digits) decodes every letter right except the first: T is
  printed as M (1 error in 200). That is the listing's start state, every path in state 5, the element space after
  T's dash, so the first dash is read as the second of M (reference sheet (f), "The start state"); it costs one
  letter per session, 0.5 % of a 200-letter run.

## 3. V2: Bell's program against his Tables XV and XVI

### 3.1 The result per cell

Bell-style count (%, pooled over the runs that finished) for Table XV and for Table XVI under reading C, the
character CER beside it, and readings A and B; "stops" counts the runs of 20 that stopped at SAVEP's zero sum.

| Table | Speed (WPM) | Sender | S₁₀₀ (dB S₁₀₀) | Bell (%) | C: Bell-style (%) | C: char CER (%) | C: stops | A: Bell-style (%) | A: stops | B: Bell-style (%) | B: stops |
|---|---|---|---|---|---|---|---|---|---|---|---|
| XV | 50 | keyboard | 12 | 0 | 2.88 | 3.35 | 0 | | | | |
| XV | 50 | keyboard | 9 | 3 | 18.73 | 19.68 | 0 | | | | |
| XV | 50 | keyboard | 8 | 5 | 34.42 | 35.85 | 0 | | | | |
| XV | 50 | keyboard | 7 | 16 | 49.15 | 50.67 | 0 | | | | |
| XV | 50 | keyboard | 6 | 35 | 64.85 | 68.83 | 0 | | | | |
| XV | 20 | keyboard | 9 | 0 | 1.39 | 1.42 | 1 | | | | |
| XV | 20 | keyboard | 6 | 3 | 22.39 | 22.81 | 2 | | | | |
| XV | 20 | keyboard | 4 | 6 | 66.63 | 67.53 | 1 | | | | |
| XV | 20 | keyboard | 3 | 31 | 84.11 | 86.03 | 2 | | | | |
| XVI | 30 | good | 9 | 3 | 14.84 | 15.89 | 1 | 50.06 | 2 | 5.08 | 0 |
| XVI | 30 | good | 6 | 5 | 52.84 | 54.08 | 1 | 76.22 | 0 | 37.83 | 0 |
| XVI | 30 | good | 4 | 36 | 81.78 | 85.85 | 0 | 83.87 | 1 | 76.00 | 1 |
| XVI | 30 | fair | 9 | 5 | 30.55 | 32.23 | 0 | 54.42 | 1 | 7.72 | 0 |
| XVI | 30 | fair | 6 | 7 | 62.00 | 64.85 | 0 | 76.63 | 1 | 44.24 | 1 |
| XVI | 30 | fair | 4 | 42 | 87.19 | 91.22 | 2 | 85.70 | 0 | 80.90 | 0 |
| XVI | 30 | poor | 9 | 12 | 48.45 | 50.85 | 0 | 57.08 | 1 | 14.57 | 0 |
| XVI | 30 | poor | 6 | 13 | 70.76 | 75.53 | 1 | 78.28 | 0 | 50.98 | 0 |
| XVI | 30 | poor | 4 | 46 | 89.45 | 93.67 | 0 | 85.15 | 0 | 82.65 | 0 |
| XVI | 20 | good | 9 | 1 | 10.78 | 11.68 | 0 | 50.81 | 12 | 3.90 | 0 |
| XVI | 20 | good | 6 | 4 | 42.53 | 44.17 | 2 | 73.38 | 16 | 28.03 | 1 |
| XVI | 20 | good | 4 | 6 | 79.33 | 81.50 | 2 | 83.80 | 15 | 68.37 | 1 |
| XVI | 20 | good | 3 | 31 | 96.33 | 99.00 | 2 | 97.90 | 15 | 91.65 | 3 |
| XVI | 20 | fair | 9 | 4 | 26.50 | 28.58 | 8 | 53.55 | 10 | 4.40 | 0 |
| XVI | 20 | fair | 6 | 6 | 58.57 | 60.89 | 6 | 72.65 | 10 | 35.15 | 0 |
| XVI | 20 | fair | 4 | 8 | 84.75 | 86.44 | 12 | 88.88 | 16 | 73.11 | 2 |
| XVI | 20 | fair | 3 | 34 | 104.35 | 107.42 | 7 | 104.50 | 13 | 93.47 | 1 |
| XVI | 20 | poor | 9 | 11 | 47.77 | 51.53 | 5 | 58.75 | 12 | 8.58 | 1 |
| XVI | 20 | poor | 6 | 13 | 69.35 | 73.62 | 7 | 71.75 | 16 | 40.06 | 2 |
| XVI | 20 | poor | 4 | 14 | 87.79 | 91.36 | 13 | 92.50 | 13 | 79.28 | 2 |
| XVI | 20 | poor | 3 | 38 | 104.18 | 106.27 | 9 | 101.14 | 13 | 100.00 | 4 |

Rates above 100 % come from insertions: at 3 dB S₁₀₀ the decoder prints more letters than were sent.

### 3.2 The rule

| Reading of Table XVI | Inconsistent cells (at most 5) | Bell above ours / below ours | Sign test, two-sided p (at least 0.05) | Cells with a stopped run | Verdict |
|---|---|---|---|---|---|
| C (the rule's, D9) | 28 | 0 / 30 | 1.9·10⁻⁹ | 19 | FAIL |
| A | 28 | 0 / 30 | 1.9·10⁻⁹ | 21 | FAIL |
| B | 22 | 1 / 29 | 5.8·10⁻⁸ | 15 | FAIL |

The two consistent cells under every reading are Table XV's zeros (50 WPM 12 dB S₁₀₀, P(X = 0) = 0.057; 20 WPM
9 dB S₁₀₀, P(X = 0) = 0.060). Under B, six of Table XVI's 9 dB S₁₀₀ cells are also consistent. The per-cell
statistics (p̂, s, the fitted correlation, both tail probabilities) are printed by `v2.py rule`.

### 3.3 Readings A, B and C

- **A** (the printed uniform law, half-widths about 1 dot) is the worst at 9 and 6 dB S₁₀₀ (50.06–78.28 %), and stops at
  SAVEP's zero sum in 167 of its 420 runs: at 20 WPM, 10–16 of 20 runs per cell.
- **B** (the printed law with σ as the half-width; no element crosses its threshold) is the closest to Bell at
  9 dB S₁₀₀ (3.90–14.57 % against his 1–12 %), and 19 of its 420 runs stop.
- **C** (the text's truncated Gaussian) lies between them: 10.78–48.45 % at 9 dB S₁₀₀, 78 of 420 runs stopped.
- These are reported, not ruled on: the rule's reading is C (D9), and the gap of section 3.5 affects all three.

### 3.4 SAVEP's zero sum (plan §9 item 6)

- **How often.** 270 of 1 440 runs stop on the trap: Table XV 6 of 180; Table XVI reading C 78 of 420, A 167 of
  420, B 19 of 420; in the S₁₀₀ grid, 12 of 480.
- **Where** (`bell1977-fpe` rebuilt with `-g`; run `xv-20wpm-kam-4db` r19, seed 8019): SAVEP line 42700,
  `P(I)=P(I)/PSUM`, with PSUM = 0.
- **Two routes, both those of reference sheet (b)**, traced with `bell1977` (no trap) at trace level 2:
  - *Route (ii)*, run `xv-20wpm-kam-4db` r19 of the committed V2. At step 35 116 (175.580 s) a negative z
    (−0.106 AU) inside a mark leaves 3 saved paths; at step 35 117 two: a live path 20 ms into a mark at 21 WPM, and
    a dead node (λ = 0) that SAVEP's fill carried over. Every successor of the live path except staying has a
    transition probability of at most 10⁻⁴, so KALFIL gives it likelihood 0; at step 35 118 the live path's stay
    node is the only node with P > 0, followed by a zero-probability fill node: P(1) = +∞, P(2) = NaN.
  - *Route (i)*, a run of the 5-run pilot (`xv-20wpm-kam-4db` r01, seed 8001, built before change 2's warm-up was
    added; the decoder's code is the same). At step 39 742 (198.710 s) a forced decision at the trellis's 199-stage
    limit sets λ = 0 for 6 of the 7 saved paths; the survivor is in a pause (λ = 227), 805 ms into it, at 15 WPM.
    At step 39 743 its only successor with P > 0 is the stay node, which SAVEP keeps as element 6's best; the fill
    adds that node again with P = 0, and the duplicate step deletes it. One slot is left: P(1) = 1/0 = +∞.
  - From the next step on every P is NaN, and the program prints garbage to the end of the session (1 000 % and
    more of the letters sent, in pilot runs of the plain binary).
- Without a guard, a stopped run has no score, and the cells with many stops are scored on few runs (reading C,
  20 WPM poor fist 4 dB S₁₀₀: 7 of 20).

### 3.5 The S₁₀₀ shift (plan §5.2, "also reported")

Keyboard Morse at 20 and 50 WPM, 20 runs per point at S₁₀₀ 3–13 and 15 dB S₁₀₀ (`run/kam-grid`; score table
`v2-fortran-kam-grid.csv`). Bell-style count, %:

| S₁₀₀ (dB S₁₀₀) | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 | 11 | 12 | 13 | 15 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 20 WPM | 85.00 | 63.08 | 42.83 | 23.78 | 10.12 | 5.47 | 2.00 | 0.82 | 0.53 | 0.65 | 0.55 | 0.53 |
| 50 WPM | 79.30 | 76.80 | 77.40 | 67.38 | 47.62 | 33.00 | 19.98 | 10.03 | 6.45 | 3.15 | 1.95 | 0.97 |

Where our curve (linear between grid points) reaches each of his nonzero values:

| Table XV cell | Bell (%) | Ours reaches it at (dB S₁₀₀) | Shift (dB S₁₀₀) |
|---|---|---|---|
| 20 WPM, 6 dB S₁₀₀ | 3 | 8.71 | +2.71 |
| 20 WPM, 4 dB S₁₀₀ | 6 | 7.89 | +3.89 |
| 20 WPM, 3 dB S₁₀₀ | 31 | 5.62 | +2.62 |
| 50 WPM, 9 dB S₁₀₀ | 3 | 12.12 | +3.12 |
| 50 WPM, 8 dB S₁₀₀ | 5 | 11.44 | +3.44 |
| 50 WPM, 7 dB S₁₀₀ | 16 | 9.40 | +2.40 |
| 50 WPM, 6 dB S₁₀₀ | 35 | 7.86 | +1.86 |

- The shift is 1.86–3.89 dB S₁₀₀ (median 2.71). It is near the 3.01 dB that the two-sided reading of N₀ would give
  (reference sheet (d), (j) item 21), but not uniform: it is smaller where his error is large. A 3.01 dB shift alone
  would leave our 50 WPM curve at 10.03 % where he has 16 % and at 19.98 % where he has 35 % (derived from the grid).
- The floor at high S₁₀₀ (0.5–1.0 % at 20 WPM) is mostly the first letter, T read as M (section 2): 0.5 % of a
  200-letter run.
- **The receiver chain is as the sheet derived** (one 100 s run per level at trace level 2, keyboard Morse at
  20 WPM, built before change 2's warm-up was added, which does not touch the chain; samples more
  than 40 ms from a key change, after the first 10 s): the noise variance of s_n on key-up samples is within 0.1 % of
  NOISE, and z and R match sheet (c)'s table: at 12 dB S₁₀₀, z on marks 1.082 AU (variance 0.034 AU²), on spaces
  −0.055 AU (0.011 AU²), R 0.0319 AU² (sheet: 1.116, 0.029; −0.046, 0.011; 0.031); at 6 dB S₁₀₀ 0.870 (0.112),
  −0.044 (0.042), 0.0623 (sheet 0.910, 0.108; −0.035, 0.042; 0.061); at 3 dB S₁₀₀ 0.737 (0.200), −0.023 (0.083),
  0.0862 (sheet 0.773, 0.196; −0.025, 0.083; 0.085). The sheet's values are for a 400 ms on/off carrier at 1500 Hz;
  ours for Morse at 1000 Hz, whose shorter marks lower the mean on marks slightly. These runs are diagnostic and
  their scores are not committed.
- **Precision does not explain it**: the program in single precision (built without `-fdefault-real-8`; the
  PDP-10's own was single) gives the same curve within the runs' scatter (5-run pilot of the grid, built before
  change 2's warm-up was added; not committed).

### 3.6 What §5.3 allows, and the open questions

§5.3 allows correcting a transcription error, a reading of his settings or definitions that the thesis supports, or
our scoring, and forbids changing his decoder's constants. None was changed. The task stops here for two rulings:

1. **SAVEP's zero sum.** V2 reaches it (section 3.4). Plan §9 item 6's ruling foresees a guard acting only when the
   sum is 0, marked and applied identically in the Fortran and the C++. Its form is open: for example, include P(1)
   in the sum when the others sum to 0 (the single survivor gets P = 1), or skip the division when PSUM = 0 (PROBP
   renormalizes at the next step anyway). Without it, V2 and V5 cannot score the stopped runs.
2. **The S₁₀₀ gap.** Our curves lie about 2–4 dB S₁₀₀ to the right of his tables (section 3.5). The reference
   sheet's first suspect is the noise density's convention, two-sided N₀ = NOISE/8000 Hz instead of the one-sided
   NOISE/4000 Hz of the thesis's pp. 33 and 113, which would label every one of our S₁₀₀ values 3.01 dB higher.
   Whether the thesis supports that reading is not decided here. A 3.01 dB correction alone would still leave the
   low-S₁₀₀ cells of Table XV inconsistent, with Bell's values then above ours (section 3.5).

The rest of Task 3 does not depend on these rulings. The golden data for the C++ tests are committed
(`engine/tests/data/bell/`, three inputs of 1 s, none reaching the zero sum), and so are the program, its settings
and the scorer.
