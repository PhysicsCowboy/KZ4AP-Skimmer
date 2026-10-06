# Development set redesign

> **Status: approved by the owner, 2026-10-06** (with the figures of principle 7 added at his request). Owner's decision E (2026-10-06): redesign the development set now, before Plan B's B5, as a
> **jittered grid**. The old set (`kz4ap_proto.experiments.DEV`, seed 1) stays available, unchanged, for
> comparisons with earlier results.

## Why

The old set's speeds are concentrated near 25 WPM (groups C, E, G and a third of A at 25 WPM; B, H and I
mostly 18–31 WPM), and S₅₀₀ (dB SNR in 500 Hz) covers −10 to +20 dB on a regular grid only in group A
(backlog, 2026-10-04). Plan B's remaining changes (B5 τ_a, B6 new overs, B7 corrections, B8 tracker) act
mostly away from 25 WPM: the stretch test showed a 12 WPM deficit of about 3 dB of energy per dit that the
old set can hardly measure. The backlog also asks for tests at 12, 25, 40 and 80 WPM.

## Principles

1. **Jittered grid** (owner): each condition's range is divided into cells, and each recording's value is
   drawn at random within its cell, with a recorded seed. Every region gets equal coverage, no two signals
   share a speed or an S₅₀₀ exactly, and nothing sits only on grid points.
2. **Speed: 10 cells from 8 to 80 WPM** (owner), evenly spaced in ln WPM, each a factor 10^(1/10) = 1.259
   wide; a signal's speed is drawn uniformly in ln WPM within its cell:

   | Cell | From (WPM) | To (WPM) | Center (WPM) | Dit (ms) |
   |---|---|---|---|---|
   | 1 | 8.00 | 10.07 | 8.98 | 119.1–150.0 |
   | 2 | 10.07 | 12.68 | 11.30 | 94.6–119.1 |
   | 3 | 12.68 | 15.96 | 14.23 | 75.2–94.6 |
   | 4 | 15.96 | 20.10 | 17.91 | 59.7–75.2 |
   | 5 | 20.10 | 25.30 | 22.55 | 47.4–59.7 |
   | 6 | 25.30 | 31.85 | 28.39 | 37.7–47.4 |
   | 7 | 31.85 | 40.09 | 35.73 | 29.9–37.7 |
   | 8 | 40.09 | 50.48 | 44.99 | 23.8–29.9 |
   | 9 | 50.48 | 63.55 | 56.64 | 18.9–23.8 |
   | 10 | 63.55 | 80.00 | 71.30 | 15.0–18.9 |

3. **S₅₀₀: contiguous cells 2 dB wide** (owner), 14 cells from −8 to +20 dB (dB SNR in 500 Hz); a signal's
   S₅₀₀ drawn uniformly within its cell.
4. **Text length: fixed per signal, about 100 characters.** Reason (derived, discussed with the owner): the
   quantity estimated is mainly where CER crosses 0.10, and there p = 0.10 at every speed, so for
   independent characters equal characters give equal precision at the crossing
   (standard error √(p(1 − p)/n)). Errors are not independent, though (a signal whose speed locks wrong
   fails as a whole), so precision is bought mainly by more signals per cell, not longer signals. A slow
   signal is a longer recording.
5. **Sizes from a target precision, set by a pilot** (owner: no arbitrary run-time limit). Target: the
   crossing's 95% interval about ±0.5 dB of S₅₀₀ per speed cell (heuristic target). A pilot of group A2
   with 4 signals per (speed, S₅₀₀) cell measures the bootstrap interval per speed cell; the number of
   signals per cell is then scaled to the target (the interval narrows about as 1/√signals). The other
   groups are sized in proportion to what they must resolve, stated per group in the full plan. Cost
   (derived from the measured CPU, 42 ms per channel-second): the Linux machine's 10 cores decode about
   240 channel-seconds per second of wall time, so even 4 × the old set's size is about 20 minutes per run.
6. **Analysis.**
   - **Against energy per dit as well as S₅₀₀.** CER is plotted against E/N₀ per dit,
     E/N₀ = S₅₀₀ + 10·log₁₀(500 Hz × dit) (dB): a time-base-invariant decoder has one curve for every speed,
     so the spread between speed cells measures the non-invariance directly.
   - **A smooth fit over speed and S₅₀₀ together:** CER modeled as a logistic in S₅₀₀ whose crossing (and
     slope) vary smoothly with ln WPM (e.g. quadratic in ln WPM), rather than ten separate fits; this pools
     information across neighboring cells. Crossings and their intervals from the fit, bootstrapped over
     signals.
   - Comparisons between decoders or variants stay paired: every variant decodes the same recordings.
7. **Figures** (owner, 2026-10-06), drawn as image files and committed beside the results record:
   (1) CER against S₅₀₀, one panel per speed cell: the signals as points, the fitted curve, the crossing
   with its interval; (2) CER against E/N₀ per dit, all speed cells on one plot (a time-base-invariant
   decoder's curves coincide); (3) the crossing (S₅₀₀ at CER 0.10) against speed, per decoder or variant,
   with intervals, beside the ideal decoder's bound; (4) paired differences by group, a variant minus the
   reference, with intervals; (5) detection recall against S₅₀₀ per speed cell, beside the decoder's CER.
8. **Seeds:** development seed 1; held-out seeds 2 and 3 generated but not decoded until a final
   evaluation (as now).

## Groups (seed 1; signals per cell to be scaled by the pilot)

| Group | What it tests | Conditions (cells; values jittered within each) |
|---|---|---|
| A2 sensitivity | white noise, machine keying | 10 speed cells × 14 S₅₀₀ cells; pilot 4 signals per cell (560) |
| B2 fading | Rayleigh fading (VE3NEA's spectrum) | 10 speed cells × f_D cells (0.05–0.2, 0.2–0.6, 0.6–2, 2–5 Hz) × S₅₀₀ cells (0–10, 10–20 dB); VE3NEA's keying mix |
| C2 fists | keying styles | 5 styles × 10 speed cells × S₅₀₀ cells (2–8, 8–14, 14–20 dB); imbalance −0.1 to +0.1 dit |
| D2 speed changes | steps and ramps | from each speed cell, a factor 1.3–2.0 up or down (within 8–80 WPM), step or ramp, S₅₀₀ 10–20 dB |
| E2 interference | a neighbor station | 10 speed cells × offset cells (0–25, 25–60, 60–120 Hz); neighbor −6 to +12 dB relative to the wanted key-down power, its speed from the cells; wanted S₅₀₀ 8–14 dB |
| F2 tuning | offsets and drift | 10 speed cells × offset 0–12 Hz, and × drift 0–2 Hz/s; S₅₀₀ 0–10 dB |
| G2/H2 QSOs | overs, turnovers | whole QSOs (as now), each side's speed from the 10 cells, VE3NEA's style mix; same-track and separate-track views |
| I2 Farnsworth | stretched gaps | character speed 18–25, 25–33 WPM × overall speed 5–8, 8–12, 12–16 WPM; S₅₀₀ 2–8, 8–14, 14–20 dB |
| S2 stretch | time-base invariance, paired | every A2 signal of speed cell 5 (20.1–25.3 WPM), stretched by exactly 2 (to 10.1–12.7 WPM, cell 2) with S₅₀₀ lowered by 3.01 dB, as B9; kept as a paired check beside the E/N₀ fit |

Through the detector path (not only oracle): A2 decoded both ways, for detection recall against S₅₀₀ per
speed cell (backlog, 2026-10-06). The backlog's 80 WPM point lies in cell 10.

## Tasks (outline; full text before dispatch)

1. Suite tooling: a jittered-grid helper (cells, seeded draws, the drawn values recorded in the labels);
   the groups; tests (every draw inside its cell; text length per signal; reproducible by seed).
2. Pilot: A2 at 4 signals per cell, decoded by Matched and the current bank; the per-cell crossing
   intervals; the sizes scaled to the target; the owner sees the sizes before the full generation.
3. Generation on the Linux machine (seeds 1–3; only seed 1 decoded).
4. The analysis tooling: the E/N₀ view; the smooth fit over speed and S₅₀₀; crossings with bootstrap
   intervals; paired comparisons by cell and pooled; detection recall per speed cell; the five figures
   (principle 7).
5. Reference runs: Envelope, Matched, `bank-b3b` settings and the current bank (after B4d); a new results
   record section, the new set's baseline.
6. Plan B continues (B5 onward) on the new set; the old set is run once more at the end, for continuity.

## Full task texts

Common to every task: the repository CLAUDE.md and its "Working rules for agents" (one git command per call,
never chained; no attribution lines; units with references, S₅₀₀ is dB SNR in 500 Hz); work on branch
`milestone-2c-stage2`; the Linux machine by `server-workflow.md` (Plan B workspace); the old development set
and its regex `DEV` stay unchanged. New suite names start with the group letter and a `2`
(`A2-…`, `B2-…`, …) and a new regex `DEV2` selects seed 1 of them.

### Task D1 (full text): the jittered grid and the new groups

**Files:** `training/kz4ap_synth/jitter.py` (new), `training/kz4ap_synth/suites.py`, `training/kz4ap_proto/experiments.py`
(the `DEV2` regex only), `training/tests/test_synth_jitter.py` (new), `training/tests/test_synth_suites2.py` (new).

- [ ] **Step 1: The helper.** `Cells` (edges, scale "log" or "linear") and `draw(cells, index, rng)` returning a
  value uniform within cell `index` (uniform in ln for "log"); `SPEED_CELLS` = 11 edges 8·10^(i/10) WPM,
  i = 0…10 (the plan's table); `SNR_CELLS` = edges −8, −6, …, +20 dB. Tests: every draw lies inside its cell;
  the cells tile the range (each upper edge is the next lower edge); reproducible by seed; the log draw is
  uniform in ln WPM (a χ² test over 10 000 draws, stated threshold).
- [ ] **Step 2: Text of about 100 characters.** A text generator that returns VE3NEA-statistics random text of
  100 ± 5 characters (spaces counted); each signal's recording time follows from its speed (12/WPM s per
  character on average, PARIS) plus its start offset. Test: lengths within the bound for 1 000 draws.
- [ ] **Step 3: The groups**, per the plan's table (A2, B2, C2, D2, E2, F2, G2/H2, I2, S2), each a function of
  the seed and of `per_cell` (signals per cell, default from the plan; A2's pilot value 4). Every drawn value
  (speed, S₅₀₀, offsets, levels, f_D, imbalance) is recorded in the label file with its cell index. Signals are
  placed in recordings as now (several stations per recording, slots ≥ 1.2 kHz apart). Tests: counts per cell;
  every label value inside its cell; S2 is A2's cell-5 signals stretched by exactly 2.0 with S₅₀₀ lowered by
  10·log₁₀ 2 = 3.01 dB, edges scaled (as B9).
- [ ] **Step 4:** pytest; commit.

### Task D2 (full text): the analysis and the figures

**Files:** `training/kz4ap_proto/devset2.py` (new: the fits and comparisons), `training/kz4ap_proto/figures.py`
(new), `training/kz4ap_proto/experiments.py` (subcommands `devset2` and `figures`), tests, and
`training/requirements-analysis.txt` (new: `matplotlib`, pinned; kept out of CI's requirements). Install it into
the local `.venv` and on the Linux machine's `.venv` (`PIP_CACHE_DIR=build/pip-cache`); if installation is
refused, stop and report.

- [ ] **Step 1: The fit.** Per signal: CER and its character count. Model: CER(S₅₀₀, v) = c_floor +
  (1 − c_floor)·logistic(−(S₅₀₀ − s₀(v))/w(v)), with s₀ and ln w quadratic in ln v (v the speed, WPM) and
  c_floor ≥ 0 a small floor; fitted by weighted least squares on the signals' CER (weights: character counts),
  or by maximum likelihood of the character errors as binomial, whichever is stable (state which and why).
  Outputs: the crossings (S₅₀₀ at CER 0.10 and 0.05) as functions of speed, evaluated at each cell's center;
  their 95% intervals by bootstrap over signals (1 000 resamples, seeded). Tests on synthetic CER data with known
  s₀(v), w(v): the fit recovers them within stated tolerances.
- [ ] **Step 2: E/N₀ per dit:** E/N₀ = S₅₀₀ + 10·log₁₀(500 Hz × dit), dit = 1.2 s / WPM (dB). The same fit
  against E/N₀ with s₀ constant in v is the time-base-invariant hypothesis; report the spread of the per-cell
  crossings in E/N₀ and a test of invariance (the quadratic terms' significance by bootstrap).
- [ ] **Step 3: Paired comparisons:** variant − reference per signal; pooled and per group with bootstrap
  intervals over test cases (as `experiments compare`), and per speed cell.
- [ ] **Step 4: Detection recall** per speed cell against S₅₀₀ (from the bench's through-the-detector scoring).
- [ ] **Step 5: The five figures** of the plan's principle 7, as PNG at 150 dpi and SVG, with axis labels
  carrying units (dB SNR in 500 Hz; dB of E/N₀ per dit; WPM), legends, and a caption file listing the runs and
  commit each figure was drawn from. Test: each figure is produced from a small fixture without error.
- [ ] **Step 6:** pytest; commit.

### Task D3 (full text): the pilot

- [ ] **Step 1:** generate A2 seed 1 at 4 signals per cell (560 signals) on the Linux machine.
- [ ] **Step 2:** decode with Matched (`kz4ap-bench`, oracle) and the bank at its current defaults
  (`kz4ap-bank-replay`), score.
- [ ] **Step 3:** the fit; the crossing's 95% interval per speed cell for each decoder; the signals per cell
  needed to reach about ±0.5 dB (the interval scales about as 1/√n; state the assumption); the resulting sizes of
  every group and the run time at the measured CPU.
- [ ] **Step 4:** a short pilot section in a new results record `docs/plans/2026-10-06-development-set-results.md`,
  with figures 1–3 for the pilot; commit. **Stop: the owner sees the sizes before the full generation.**
