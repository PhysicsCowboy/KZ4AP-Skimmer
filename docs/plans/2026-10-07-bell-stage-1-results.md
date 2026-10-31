# Bell decoder, stage 1: Results Record

Plan: `docs/plans/2026-10-07-bell-decoder-stage-1.md` (owner's go-ahead, 2026-10-08). Branch `bell-stage-1`. Every
number here is **measured** unless marked otherwise, and is computed from the committed score tables named with it.

**Stage 1 stopped (owner, 2026-10-09).** The printed program is not a usable reference: by its measured curves it
decodes worse than the project's existing decoders (not compared head to head), and the cause of its gap to Bell's tables is unresolved. The faithful C++ port (Tasks 4-9)
is cancelled. Bell's structure (speed, element duration, letter tree and amplitude estimated jointly, with delayed
decisions) is taken up instead in the design of a new decoder fed by the bank's matched-filter observation. The
compilable program, its tooling, the golden data and Task 2's C++ were removed from the tree afterwards (they are in
the history at commit 38a7baf); the transcription, the reference sheet and this record with its score tables stay.

**Answer (V2, Task 3): FAIL.** Bell's own program, compiled with gfortran and run on his simulator at the settings of
his Tables XV and XVI as the reference sheet reads them, decodes far worse than his tables. In the final run (with the
guard of change 11, section 3.6), under reading C of decision D9 (the rule's reading), **28 of the 30 cells are
inconsistent** with his values (criterion (i) allows 5), and our error is above his in **all 30 cells** (two-sided
sign test p = 1.9·10⁻⁹; criterion (ii) needs p ≥ 0.05). Readings A and B fail as well (28 and 22 inconsistent
cells).

- **SAVEP's zero sum** (plan §9 item 6) is reached in 270 of the 1 440 runs. The first run, without a guard, stopped
  there (sections 3.1–3.4). By the controller's ruling R1 the guard of change 11 now adds P(1) to SAVEP's sum only
  when that sum is 0; it acts in exactly those 270 runs, and the other 1 170 runs are unchanged.
- **The gap.** On keyboard Morse our curve reaches his nonzero Table XV values 1.86–3.89 dB S₁₀₀ later (median of
  seven cells 2.71 dB S₁₀₀; section 3.5). The S₁₀₀ convention stays one-sided, as the thesis states it (pp. 33, 113;
  ruling R2).
- **The cause is unresolved** (section 3.7). Checked and excluded: the conversion to fixed form, the receiver chain,
  the optimizer, the trap, precision, and the choice between our 47-symbol and the 36-symbol alphabet (the latter gives the same gap,
  1.86–3.90 dB S₁₀₀, section 3.8). Not excluded: the printed program differing from the one behind the tables, the
  letter tree's structure beyond its alphabet, sheet (e)'s other choices of ours, the noise convention, his marking
  by eye, and his single 200-letter experiments.

## 1. Conditions

- **Machine.** The Linux machine: Linux (Ubuntu 22.04), 10-core Intel Xeon (Ice Lake). gfortran: GNU Fortran 16.2.0
  (conda-forge gcc 16.2.0-7), installed with micromamba 2.9.0 inside the repository's `build/` folder.
- **Program.** `reference/bell1977/bell1977.f`: the first run (sections 3.1–3.5) at commit 0ea2b05; the final run
  (section 3.6) at commit 6fb721b, which adds change 11 (SAVEP's guard, 327e6e3) and a fix to `v2.py rule`'s
  beta-binomial at a fitted correlation of 1 (6fb721b; the first run's verdicts are unchanged by it). Built by `reference/bell1977/build.sh` with
  `-std=legacy -fdec -fno-automatic -finit-local-zero -fdefault-real-8 -fdefault-double-8 -O2` and
  `-ffpe-trap=invalid,zero` (the binary `bell1977-fpe`). The trap and the optimizer change no result (section 3.7).
  The diagnostics of sections 2, 3.7 and 3.8 ran at commit 9eb99af (the Fortran of 6fb721b; `v2.py` gained `run
  --alphabet`, `compare` and `chain`). The changes to the listing, each marked in the code, are listed in
  `reference/bell1977/README.md`.
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
  p̂(1 − p̂)/200); consistent if P(X ≤ x) ≥ 0.05 and P(X ≥ x) ≥ 0.05, or P(X = 0) ≥ 0.05 for x = 0. A cell whose pooled
  rate exceeds 1 (insertions; reading C: 20 WPM fair and poor at 3 dB S₁₀₀) is modeled with p = 1, and a fitted
  correlation above 1 is clamped at 1 (all errors or none): both make the cell inconsistent. The verdict does not
  depend on it: counting those two cells as consistent would leave 26 inconsistent cells.
- **Score tables** (`docs/plans/data/2026-10-07-bell-stage-1/`), one row per run:
  - first run, without the guard (committed in 0f41a21): `v2-fortran.csv`, the 72 settings files (1 440 rows), and
    `v2-fortran-kam-grid.csv`, the S₁₀₀ grid (480 rows);
  - final run, with change 11 (commit 4c9fefd): `v2-fortran-guard.csv` and `v2-fortran-kam-grid-guard.csv`, the same
    runs and seeds;
  - diagnostics (run at commit 9eb99af, committed with section 3.8): `v2-fortran-tree36.csv` and
    `v2-fortran-kam-grid-tree36.csv` (the 36-symbol letter tree; Table XV, Table XVI reading C and the grid, same
    seeds); `v2-diag-fpe-vs-plain.csv` (per run, 1 if the two binaries wrote byte-identical TRANSL.TXT and
    EVENTS.TXT); `v2-diag-O0-vs-O2-kam-grid.csv` (the same for `-O0` against `-O2` on the grid);
    `v2-diag-single-kam-grid.csv` (the grid in single precision); `v2-diag-chain.csv` (receiver-chain statistics,
    one row per level); `v2-diag-smoke.csv` (the smoke test's score).

  Columns: table, speed (WPM), sender, reading, S₁₀₀ (dB S₁₀₀), Bell's value (%), run, seed, zero_sum (1: stopped on
  the trap), guard_steps (final run only: decoder steps at which change 11 acted), reference letters, Bell-style
  count, character edits, reference word spaces, space edits, and the two rates.

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
  marked changes (a statement-by-statement comparison). The **smoke test** (`run/smoke.settings`, score table
  `v2-diag-smoke.csv`, commit 9eb99af: keyboard Morse,
  20 WPM, no noise, 200 letters, 34 distinct letters and digits) decodes every letter right except the first: T is
  printed as M (1 error in 200). That is the listing's start state, every path in state 5, the element space after
  T's dash, so the first dash is read as the second of M (reference sheet (f), "The start state"); it costs one
  letter per session, 0.5 % of a 200-letter run.

## 3. V2: Bell's program against his Tables XV and XVI

### 3.1 First run, without the guard: the result per cell

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

### 3.2 First run: the rule

| Reading of Table XVI | Inconsistent cells (at most 5) | Bell above ours / below ours | Sign test, two-sided p (at least 0.05) | Cells with a stopped run | Verdict |
|---|---|---|---|---|---|
| C (the rule's, D9) | 28 | 0 / 30 | 1.9·10⁻⁹ | 19 | FAIL |
| A | 28 | 0 / 30 | 1.9·10⁻⁹ | 21 | FAIL |
| B | 22 | 1 / 29 | 5.8·10⁻⁸ | 15 | FAIL |

The two consistent cells under every reading are Table XV's zeros (50 WPM 12 dB S₁₀₀, P(X = 0) = 0.057; 20 WPM
9 dB S₁₀₀, P(X = 0) = 0.060). Under B, six of Table XVI's 9 dB S₁₀₀ cells are also consistent. The per-cell
statistics (p̂, s, the fitted correlation, both tail probabilities) are printed by `v2.py rule`.

### 3.3 First run: readings A, B and C

- **A** (the printed uniform law, half-widths about 1 dot) is the worst at 9 and 6 dB S₁₀₀ (50.06–78.28 %), and stops at
  SAVEP's zero sum in 167 of its 420 runs: at 20 WPM, 10–16 of 20 runs per cell.
- **B** (the printed law with σ as the half-width; no element crosses its threshold) is the closest to Bell at
  9 dB S₁₀₀ (3.90–14.57 % against his 1–12 %), and 19 of its 420 runs stop.
- **C** (the text's truncated Gaussian) lies between them: 10.78–48.45 % at 9 dB S₁₀₀, 78 of 420 runs stopped.
- These are reported, not ruled on: the rule's reading is C (D9), and the gap of section 3.5 affects all three.

### 3.4 First run: SAVEP's zero sum (plan §9 item 6)

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
  - *Route (i)*, an uncommitted diagnostic trace: a pilot run (`xv-20wpm-kam-4db` r01, seed 8001) of a build before
    change 2's warm-up was added (the decoder's code is the same), so its seed does not reproduce it at 0ea2b05; the
    route itself is derived in reference sheet (b). At step 39 742 (198.710 s) a forced decision at the trellis's 199-stage
    limit sets λ = 0 for 6 of the 7 saved paths; the survivor is in a pause (λ = 227), 805 ms into it, at 15 WPM.
    At step 39 743 its only successor with P > 0 is the stay node, which SAVEP keeps as element 6's best; the fill
    adds that node again with P = 0, and the duplicate step deletes it. One slot is left: P(1) = 1/0 = +∞.
  - From the next step on every P is NaN, and the plain binary prints garbage to the end of the session (seen in
    uncommitted pilot runs; with the trap, such runs stop and have no score).
- Without a guard, a stopped run has no score, and the cells with many stops are scored on few runs (reading C,
  20 WPM poor fist 4 dB S₁₀₀: 7 of 20).

### 3.5 The S₁₀₀ gap per cell (plan §5.2, "also reported")

Keyboard Morse at 20 and 50 WPM, 20 runs per point at 3–13 and 15 dB S₁₀₀ (`run/kam-grid`), final run with
change 11 (`v2-fortran-kam-grid-guard.csv`; the guard acts in 12 of the 480 runs). Bell-style count, %:

| S₁₀₀ (dB S₁₀₀) | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 | 11 | 12 | 13 | 15 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 20 WPM | 84.97 | 62.77 | 42.83 | 23.75 | 10.12 | 5.47 | 2.00 | 0.82 | 0.57 | 0.65 | 0.55 | 0.55 |
| 50 WPM | 79.30 | 76.80 | 77.40 | 67.38 | 47.62 | 33.00 | 19.98 | 10.03 | 6.45 | 3.15 | 1.95 | 0.97 |

In the first run (`v2-fortran-kam-grid.csv`, its 12 stopped runs left out), only the 20 WPM points with a stopped run
differ: 85.00, 63.08, 23.78, 0.53 and 0.53 % at 3, 4, 6, 11 and 15 dB S₁₀₀.

Where our curve (linear between grid points) reaches each of his nonzero values; the gap is the same, to 0.01 dB S₁₀₀, in
both runs:

| Table XV cell | Bell (%) | Ours reaches it at (dB S₁₀₀) | Gap (dB S₁₀₀) |
|---|---|---|---|
| 20 WPM, 6 dB S₁₀₀ | 3 | 8.71 | +2.71 |
| 20 WPM, 4 dB S₁₀₀ | 6 | 7.89 | +3.89 |
| 20 WPM, 3 dB S₁₀₀ | 31 | 5.62 | +2.62 |
| 50 WPM, 9 dB S₁₀₀ | 3 | 12.12 | +3.12 |
| 50 WPM, 8 dB S₁₀₀ | 5 | 11.44 | +3.44 |
| 50 WPM, 7 dB S₁₀₀ | 16 | 9.40 | +2.40 |
| 50 WPM, 6 dB S₁₀₀ | 35 | 7.86 | +1.86 |

His two zero cells, 20 WPM at 9 dB S₁₀₀ (ours 1.40 % in the final V2 run) and 50 WPM at 12 dB S₁₀₀ (ours 2.88 %),
are both consistent with his 0 under the rule.

![Keyboard Morse, Bell-style count against S₁₀₀: our curves at 20 and 50 WPM (lines, 20 runs per point) and his
Table XV values (open diamonds)](figures/2026-10-07-bell-stage-1-v2-table-xv.png)

- The gap is 1.86–3.89 dB S₁₀₀ (median 2.71). It is not uniform: it is smaller where his error is large. A shift of
  3.01 dB S₁₀₀ is what a factor of 2 in N₀, a two-sided instead of a one-sided noise density, would make. It would still leave our 50 WPM curve
  at 10.03 % where he has 16 %, and at 19.98 % where he has 35 % (derived from the grid).
- The floor at high S₁₀₀ (0.55–0.82 % at 20 WPM from 10 dB S₁₀₀ up) is mostly the first letter, T read as M
  (section 2): 0.5 % of a 200-letter run.

### 3.6 Final run, with change 11

Change 11 (commit 327e6e3; `reference/bell1977/README.md`, change list) adds P(1) to SAVEP's renormalizing sum only
when that sum is 0. It is the controller's ruling R1 under plan §9 item 6, a documented deviation that the C++ will
repeat. All 1 440 runs and the 480 grid runs were run again with it at commit 6fb721b (score tables
`v2-fortran-guard.csv` and `v2-fortran-kam-grid-guard.csv`).

- **No run stops.** The guard acts in exactly the 270 runs that stopped in the first run:

  | Table and reading | Runs where the guard acts | Decoder steps at which it acts (sum) |
  |---|---|---|
  | XV | 6 of 180 | 84 |
  | XVI, reading C | 78 of 420 | 2 421 |
  | XVI, reading A | 167 of 420 | 8 751 |
  | XVI, reading B | 19 of 420 | 462 |
  | S₁₀₀ grid | 12 of 480 | — |

- **The other 1 170 runs are unchanged**: their Bell-style counts and character edits equal the first run's.
- **The golden data are unchanged**: the three golden inputs, rerun with change 11, give byte-identical files.
- **Per cell.** Bell-style count, %, pooled over the 20 runs. For reading C the character CER is given beside it;
  "guard" counts the runs in which change 11 acted.

| Table | Speed (WPM) | Sender | S₁₀₀ (dB S₁₀₀) | Bell (%) | C: Bell-style (%) | C: char CER (%) | C: guard | A: Bell-style (%) | A: guard | B: Bell-style (%) | B: guard |
|---|---|---|---|---|---|---|---|---|---|---|---|
| XV | 50 | keyboard | 12 | 0 | 2.88 | 3.35 | 0 | | | | |
| XV | 50 | keyboard | 9 | 3 | 18.73 | 19.68 | 0 | | | | |
| XV | 50 | keyboard | 8 | 5 | 34.42 | 35.85 | 0 | | | | |
| XV | 50 | keyboard | 7 | 16 | 49.15 | 50.67 | 0 | | | | |
| XV | 50 | keyboard | 6 | 35 | 64.85 | 68.83 | 0 | | | | |
| XV | 20 | keyboard | 9 | 0 | 1.40 | 1.43 | 1 | | | | |
| XV | 20 | keyboard | 6 | 3 | 22.00 | 22.48 | 2 | | | | |
| XV | 20 | keyboard | 4 | 6 | 66.47 | 67.35 | 1 | | | | |
| XV | 20 | keyboard | 3 | 31 | 83.38 | 85.53 | 2 | | | | |
| XVI | 30 | good | 9 | 3 | 15.12 | 16.20 | 1 | 49.98 | 2 | 5.08 | 0 |
| XVI | 30 | good | 6 | 5 | 52.95 | 54.23 | 1 | 76.22 | 0 | 37.83 | 0 |
| XVI | 30 | good | 4 | 36 | 81.78 | 85.85 | 0 | 83.60 | 1 | 76.42 | 1 |
| XVI | 30 | fair | 9 | 5 | 30.55 | 32.23 | 0 | 54.10 | 1 | 7.72 | 0 |
| XVI | 30 | fair | 6 | 7 | 62.00 | 64.85 | 0 | 76.55 | 1 | 44.67 | 1 |
| XVI | 30 | fair | 4 | 42 | 86.83 | 91.17 | 2 | 85.70 | 0 | 80.90 | 0 |
| XVI | 30 | poor | 9 | 12 | 48.45 | 50.85 | 0 | 57.50 | 1 | 14.57 | 0 |
| XVI | 30 | poor | 6 | 13 | 70.58 | 75.42 | 1 | 78.28 | 0 | 50.98 | 0 |
| XVI | 30 | poor | 4 | 46 | 89.45 | 93.67 | 0 | 85.15 | 0 | 82.65 | 0 |
| XVI | 20 | good | 9 | 1 | 10.78 | 11.68 | 0 | 52.23 | 12 | 3.90 | 0 |
| XVI | 20 | good | 6 | 4 | 42.73 | 44.35 | 2 | 71.47 | 16 | 28.10 | 1 |
| XVI | 20 | good | 4 | 6 | 78.65 | 80.97 | 2 | 85.40 | 15 | 68.50 | 1 |
| XVI | 20 | good | 3 | 31 | 95.70 | 98.05 | 2 | 92.35 | 15 | 90.83 | 3 |
| XVI | 20 | fair | 9 | 4 | 27.68 | 30.02 | 8 | 54.30 | 10 | 4.40 | 0 |
| XVI | 20 | fair | 6 | 6 | 59.48 | 62.17 | 6 | 73.53 | 10 | 35.15 | 0 |
| XVI | 20 | fair | 4 | 8 | 86.78 | 90.17 | 12 | 87.75 | 16 | 72.67 | 2 |
| XVI | 20 | fair | 3 | 34 | 102.67 | 105.33 | 7 | 99.62 | 13 | 94.33 | 1 |
| XVI | 20 | poor | 9 | 11 | 48.30 | 51.95 | 5 | 60.80 | 12 | 8.60 | 1 |
| XVI | 20 | poor | 6 | 13 | 70.50 | 74.62 | 7 | 72.33 | 16 | 39.92 | 2 |
| XVI | 20 | poor | 4 | 14 | 88.62 | 92.75 | 13 | 89.85 | 13 | 78.92 | 2 |
| XVI | 20 | poor | 3 | 38 | 101.85 | 104.35 | 9 | 99.95 | 13 | 99.40 | 4 |

- **The rule** (`v2.py rule` on `v2-fortran-guard.csv`):

  | Reading of Table XVI | Inconsistent cells (at most 5) | Bell above ours / below ours | Sign test, two-sided p (at least 0.05) | Verdict |
  |---|---|---|---|---|
  | C (the rule's, D9) | 28 | 0 / 30 | 1.9·10⁻⁹ | **FAIL** |
  | A | 28 | 0 / 30 | 1.9·10⁻⁹ | FAIL |
  | B | 22 | 1 / 29 | 5.8·10⁻⁸ | FAIL |

  Under every reading, Table XV's two zeros are consistent (P(X = 0) = 0.057 at 50 WPM and 12 dB S₁₀₀, 0.060 at
  20 WPM and 9 dB S₁₀₀). Under B, the six Table XVI cells at 9 dB S₁₀₀ are consistent as well.

### 3.7 Checked, excluded, unresolved

§5.3 allows three corrections: a transcription error, a reading of his settings or definitions that the thesis
supports, or our scoring. It forbids changing his decoder's constants. No constant was changed. By ruling R2 the
S₁₀₀ convention stays one-sided (the thesis states N₀ as the one-sided density, pp. 33 and 113), and no other reading
or change was tried.

**Checked and excluded:**
- **The conversion to fixed form**: the program's statements equal the listing's, blanks aside, except at the marked
  changes (statement-by-statement comparison).
- **The receiver chain**: the noise and the statistics of z and R match sheet (c) (`v2-diag-chain.csv`, `v2.py
  chain`; one 100 s session per level, `run/diag/chain-*.settings`: keyboard Morse at 20 WPM, trace level 2; decoder
  steps after the first 10 s whose key state 14.5 ms earlier was constant for 40 ms on each side). The variance of
  s_n over key-up samples is 1.2609, 5.0196 and 10.0153 AU² against NOISE 1.2619, 5.0238 and 10.0237 AU² (0.08 % low
  in each). z and R:

  | S₁₀₀ (dB S₁₀₀) | z on marks: mean (AU), var (AU²) | z on spaces: mean (AU), var (AU²) | R mean (AU²) | Sheet (c): marks; spaces; R |
  |---|---|---|---|---|
  | 12 | 1.0812, 0.0320 | −0.0629, 0.0098 | 0.0317 | 1.116, 0.029; −0.046, 0.011; 0.031 |
  | 6 | 0.8711, 0.1068 | −0.0575, 0.0382 | 0.0618 | 0.910, 0.108; −0.035, 0.042; 0.061 |
  | 3 | 0.7379, 0.1896 | −0.0404, 0.0750 | 0.0854 | 0.773, 0.196; −0.025, 0.083; 0.085 |

  The sheet's values are for a 400 ms on/off carrier at 1500 Hz, ours for Morse at 1000 Hz, whose shorter marks lower
  the mean on marks slightly; the sheet's are single windows too. The agreement is within a few percent of the
  carrier's 1 AU, far from the 2–4 dB S₁₀₀ gap.
- **The optimizer**: `-O0` and `-O2` builds (both with the trap) write byte-identical TRANSL.TXT and EVENTS.TXT in
  all 480 grid runs (`v2-diag-O0-vs-O2-kam-grid.csv`).
- **The trap**: `bell1977-fpe` and `bell1977` write byte-identical outputs in all 1 440 V2 runs with change 11
  (`v2-diag-fpe-vs-plain.csv`).
- **Precision**: the program in single precision (built without `-fdefault-real-8 -fdefault-double-8`; the PDP-10
  itself used single precision) gives the same curve (`v2-diag-single-kam-grid.csv`; its random numbers differ, so
  the runs are not paired): the gaps of section 3.5 become +2.74, +3.94, +2.59 dB S₁₀₀ at 20 WPM and +3.13, +3.44,
  +2.37, +1.85 dB S₁₀₀ at 50 WPM, within 0.06 dB S₁₀₀ of double precision's.
- **The scoring**: the scorer is the project's tested C++ one (Bell's samples B, C and D give the reference sheet's
  counts). The smoke test decodes 199 of 200 letters right (`v2-diag-smoke.csv`).
- **Our alphabet**: the letter tree with the 36 letters and digits only gives the same gap (section 3.8).

**Candidate causes the evidence does not exclude; each is unresolved:**
- **The printed program may differ from the one behind the tables** (*unresolved*). The reference sheet already found
  that the simulator behind his printed samples was not the printed KEY: it sent fixed texts, keyed a period, and keyed
  C correctly (sheet (e)).
- **The letter tree** (*unresolved* beyond its alphabet). Bell's MORSEM is not printed; ours is rule N1 over an
  alphabet of our choice (sheet (f)). His tree held at least `%` (sample H). The 36-symbol variant does not change the
  gap (section 3.8); the "+ ; :" variant was not run, and the rule's structure (one numbering of the 13 DATA rows kept,
  the start state) was not varied.
- **Sheet (e)'s other choices** (*unresolved*): FREQ 1000 Hz (with RCVR's image beat), the fading, AVAR, PHIVAR,
  FCHIRP and ESEP's defaults are ours, not his, and were not varied.
- **The noise convention** (*unresolved*). With a two-sided density N₀ = NOISE/8000 Hz, every S₁₀₀ here would read
  3.01 dB S₁₀₀ higher. The thesis states the one-sided convention, so it is not applied (ruling R2). That shift alone would
  not reconcile the low-S₁₀₀ cells (section 3.5).
- **His marking by eye** (*unresolved*). His printed samples show unmarked errors (sheet (i)), so his counts may be
  low by an unknown amount.
- **Single 200-letter experiments** (*unresolved*). Each of his values is one experiment of about 200 letters. The
  rule allows for that scatter in each cell. It does not explain a difference in the same direction in all 30 cells.

The golden data for the C++ tests (`engine/tests/data/bell/`: three inputs of 1 s, none reaching the zero sum), the
program, its settings and the scorer are committed.

### 3.8 Diagnostic: the 36-symbol letter tree

By the controller's ruling on the review, the letter tree was rebuilt with the reference sheet's "36 only" alphabet
(`kz4ap-bell-tables --alphabet letters-digits`: the 36 letters and digits, X under code 43, 173 states; sheet (f),
(j) item 1) and Table XV, Table XVI reading C and the keyboard grid were rerun with it, 20 runs per cell, the same
seeds, change 11 in place (commit 9eb99af; `v2-fortran-tree36.csv`, `v2-fortran-kam-grid-tree36.csv`). This is a
diagnostic: the primary result stays the 47-symbol tree.

- **The rule** under reading C: 29 of 30 cells inconsistent (one more than with 47 symbols: 50 WPM at 12 dB S₁₀₀,
  2.80 % with P(X = 0) = 0.049), Bell below ours in all 30 cells, sign test p = 1.9·10⁻⁹: FAIL.
- **Per cell**, the Bell-style count changes by −1.27 to +2.92 points (mean +0.52) against the 47-symbol tree.
- **The gap per cell** (grid, linear interpolation as in section 3.5):

  | Table XV cell | Bell (%) | Gap, 47-symbol tree (dB S₁₀₀) | Gap, 36-symbol tree (dB S₁₀₀) |
  |---|---|---|---|
  | 20 WPM, 6 dB S₁₀₀ | 3 | +2.71 | +2.69 |
  | 20 WPM, 4 dB S₁₀₀ | 6 | +3.89 | +3.90 |
  | 20 WPM, 3 dB S₁₀₀ | 31 | +2.62 | +2.61 |
  | 50 WPM, 9 dB S₁₀₀ | 3 | +3.12 | +3.08 |
  | 50 WPM, 8 dB S₁₀₀ | 5 | +3.44 | +3.48 |
  | 50 WPM, 7 dB S₁₀₀ | 16 | +2.40 | +2.38 |
  | 50 WPM, 6 dB S₁₀₀ | 35 | +1.86 | +1.86 |

- **The guard** acts in 5 of 180 Table XV runs, 89 of 420 reading-C runs and 14 of 480 grid runs.
- **Reading:** the alphabet does not explain the gap. The extra hypotheses of the 47-symbol tree (period and
  prosigns, which the simulator never sends) cost at most a few points per cell at low S₁₀₀, nothing at the
  thresholds where the gap is measured.
