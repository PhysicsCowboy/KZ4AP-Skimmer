# Milestone 2, Part 1: Results Record

Branch `milestone-2`, written 2026-09-30 after Task 17 (346b9d5). This is
the baseline for redesigning the speed estimator and the matched filter.
It records what was built, what was measured, every decision taken on the
owner's behalf, the fixes applied and proposed, and what is still open.
Nothing here is new measurement: every figure is copied from a source
named beside it.

**Conventions.**

- **S₅₀₀**: key-down carrier power over noise power in 500 Hz, in dB.
  Every dB in this file is an S₅₀₀ (or a difference of S₅₀₀ values)
  unless it names another reference ("dB re A" = relative to the calling
  station's key-down power).
- **CER**: character error rate, edits over reference symbols, word
  spaces counted (`docs/signal-processing.md` §11). **First-word CER**
  scores the first word of each transmission and is an upper bound: it
  can exceed 1 (§11; ruling in §4 below, Task 7).
- **WPM**: words per minute (PARIS standard, dit = 1.2 s / WPM).
- **Envelope**: the milestone-1 front end (`--front-end envelope`).
  **Matched**: the new default, a boxcar filter matched to 0.8 dit, fed
  by a per-station frequency tracker, keying on a log-likelihood ratio
  (LLR, in nats).
- **K**: the matched filter's length in samples at the channel rate
  r = 1500 samples/s (so K samples = K/1.5 ms). **D_ch** = 47 Hz: the
  detector's channel distance (heuristic).
- **Oracle**: channels opened at the labeled frequencies, detector
  bypassed (benchmark only).
- **CPU figure**: ms of process CPU time per second of channel audio
  (ms/s), over the bench's timed window (§11, "CPU time per
  channel-second").
- **Intervals**: bootstrap 95% intervals over signals, 1000 resamples,
  written "(low to high)". "No interval" means the source gives none.
- **Suite seeds**: the full suite uses generator seeds 1, 2 and 3
  (recordings `*-s1`, `*-s2`, `*-s3`).
- **Labels**: *measured* (run on the code), *simulated* (a Python port,
  not the C++), *derived* (from a formula), *heuristic* (a chosen value),
  *inferred* (reasoned from evidence, not instrumented).

Source abbreviations used below (full paths and git status in §2):
**[ledger]** = the execution ledger `progress.md`, cited by line (L);
**[SP]** = `docs/signal-processing.md`; **[plan]** = the milestone plan;
**[T*n*]** = `task-n-report.md`; **[FR]** = `final-review.md`;
**[FF]** = `final-fix-report.md`; **[RD]** = `regressions-diagnosis.md`;
**[sum]** = `build/suite/full3/summary.md`.

---

## 1. What was built

Execution order: Tasks 1–9 and 11 (owner scope 2026-09-29, [ledger] L3),
then the history rewrite ([ledger] L71), then Tasks 10, 12, 13, 14, the
final review fix wave, then the addendum Tasks 15–17 ([plan] "Addendum
(2026-09-30)"). Commit IDs are current (after the rewrite); the ledger's
IDs before its line L71 are pre-rewrite (mapping at the end of §2).

| Task | What it built | Commits (current IDs) |
|---|---|---|
| 1 | Message text: CQ calls, contest exchanges, whole ragchew QSOs, filler text with VE3NEA's statistics (Python generator) | 147d713 |
| 2 | Transmissions with pauses, tune-up carriers and carrier drift | d451dfc |
| 3 | VE3NEA's keying styles, key imbalance, speed changes | 0bc722b |
| 4 | Rayleigh fading with a Doppler-spread setting (Gaussian or VE3NEA's Butterworth spectrum) | 642cbbd |
| 5 | Band spacing, span, speed and S₅₀₀ settings; unscored interferers | 9cb70d3 |
| 6 | Two-station QSOs, alternating overs on one frequency, answering station 0–200 Hz away | 9f9bd35 |
| 7 | Bench scores word spaces and each transmission's first word separately | b40f0d9; fix round 61598d9 |
| 8 | Oracle channels and CPU time per channel-second in engine and bench | 25571b0 |
| 9 | Named benchmark suites, runner and summary | 50a173d; fix round a19e851 |
| 10 | Per-station frequency tracker: numerically controlled oscillator (NCO) and lag-product discriminator, fine-tuning within ±12 Hz of an anchor | 7f3b9e8; fix rounds c99f5e2, 3a061eb |
| 11 | Dit-matched front end with Rician/Rayleigh LLRs (not wired in) | 2208caa; fix round 590da5a |
| 12 | Matched mode in the classical decoder: LLR keying, filter follows the speed, frequency anchor | 2aefdee |
| 13 | Engine: the detector decides each channel's station (option 1), re-centering from the detector, Matched made the default, `--front-end` | 842ce58 |
| 14 | Tracked frequency in the bench; runner passes the front end explicitly; measured results documented; Matched smoke check in CI | 3fad14c, 6f8ad09, bb48b01; fix round c9a2aa4 |
| Final review | Doc-truth fix wave; group-H regimes judged per front end | d02b0ee |
| Linux CI | Test bug: dangling pointer into a temporary track list | 3c1b8ce |
| 15 | Dit growth bound applied once per physical mark (snapshot and restore at a dropout merge) | 59cd450 |
| 16 | A mark whose key-down was not observed is decoded but not counted for speed | 76e2c27 |
| 17 | Re-measurement of Matched after Tasks 15 and 16 (docs only) | 346b9d5 |

Plan commits: d3b1c25, 12d8b34, 23d8963 (after review), e63b459 (after
re-review), 5606d3b (after final check), b3e5729 (owner decisions
2026-09-29), 0adc1b7 (option 1), 082f56d and 74182aa (addendum and its
review fixes).

Test totals at the end: C++ `ctest` 166/166 passed ([T17] step 1);
Python `pytest` 102 passed at d02b0ee ([FF] "Tests"; not re-counted
since, and Tasks 15–17 changed no Python).

---

## 2. Where things are recorded

### Committed (on `milestone-2`)

| File | What it holds |
|---|---|
| `docs/plans/2026-09-27-milestone-2a-benchmark-and-front-end.md` | The plan: owner decisions of 2026-09-27 and 2026-09-29, option 1, design decisions with the simulations, Tasks 1–14, Addendum (2026-09-30) Tasks 15–17 |
| `docs/signal-processing.md` | What the code does now. §7 (tracker, "Frequency re-centering"), §8 ("Two front ends", "Once per physical mark", "Marks whose start was not observed"), §8b "Measured: Matched against Envelope", "Where Matched is worse than Envelope", "Limits measured", §10 parameters, §11 definitions, "Smoke check" |
| `docs/backlog.md` | Deferred work, including the Matched limits and proposed fixes |
| `README.md` | Smoke figures, suite size and timing, Known limitations |
| `bench/baselines/smoke.json` | Envelope smoke limit: max CER 0.09 (not edited by any task) |
| `bench/baselines/smoke-matched.json` | Matched smoke limit: max CER 0.07, min detection recall 0.875 |
| `docs/plans/2026-09-27-milestone-2a-results.md` | This file |

### Git-ignored (on this machine only; not in the repository)

`.superpowers/sdd/` is ignored by `.superpowers/sdd/.gitignore` (`*`);
`build/` by the root `.gitignore` (`/build/`). Both checked with
`git check-ignore -v`.

| Path | What it holds |
|---|---|
| `.superpowers/sdd/2026-09-27-milestone-2a-benchmark-and-front-end/progress.md` | The execution ledger: every dispatch, review outcome, ruling, deferred minor and owner decision |
| same folder, `task-1-report.md` … `task-17-report.md`, `task-N-brief.md` | Each implementer's report (commands, test output, measured figures) and brief |
| same folder, `final-review.md`, `final-fix-report.md` | Whole-branch review (238e8b7..c9a2aa4) and the fix wave d02b0ee |
| same folder, `regressions-diagnosis.md` | Diagnosis of the two regressions R1 and R2, proposals f1, f2, g1, g2 |
| same folder, `review-*.diff`, `preflight-scan.md`, `global-constraints.md`, `*-rules.md` | Review inputs |
| `build/suite/full3/` | The 117 recordings (3 seeds), `results/{baseline,matched}/*.json`, and `summary.md`/`summary.json` from Task 17's run (code 76e2c27) |
| `build/suite/full3-task14-results/`, `full3-task14-summary.md`, `full3-task14-summary.json` | Task 14's results (code c9a2aa4), kept by Task 17 |
| `build/suite/full3-t17-run1-summary.*`, `full3-*.log`, `t17-attrib-t14/`, `t17-attrib-t15/` | Task 17's first run, logs, and the attribution scorings at 74182aa and 59cd450 |
| `build/t14diag/` | Diagnosis scripts: `cut.py`, `cut_any.py`, `late_open.py`, `t17_attrib.py` |

### Temporary (may be deleted at any time)

| Path | What it holds |
|---|---|
| `%TEMP%\kz4ap-m2a-plan-review.md` | Plan review 2026-09-27 (C1, C2, I1–I5, M1–M12) and resolutions (plan 23d8963) |
| `%TEMP%\kz4ap-m2a-plan-rereview.md` | Re-review 2026-09-27 (I-1 to I-4) and resolutions (plan e63b459) |
| `%TEMP%\kz4ap-m2a-plan-final-check.md` | Final check 2026-09-27/28 (F-1 to F-3) and resolutions (plan 5606d3b) |
| `%TEMP%\kz4ap-option1-sim.md` | Option-1 simulation, 2026-09-29 (Python port, numpy seeds 0–29, 30 per case) |
| `%TEMP%\kz4ap-m2a-plan-summary.md` | Owner-facing summary of the option-1 plan revision |
| `%TEMP%\kz4ap-option1-plan-review.md` | Review of the option-1 plan revision (I1–I3, M1–M12) |
| `%TEMP%\kz4ap-addendum-review.md` | Review of the addendum 082f56d (I1, I2, M1–M5) |
| `C:\Users\kbrow\.claude-personal\jobs\163ae915\tmp\diag\` | Scratch builds used by [RD] (per [RD] "Scope") |
| `C:\Users\kbrow\.claude-personal\projects\C--KZ4APSkimmer\memory\project-matched-filter-rethink.md` | The owner's 2026-09-30 direction and the revisit list (assistant memory, outside the repository; not temporary, but not in git) |

`%TEMP%` = `C:\Users\kbrow\AppData\Local\Temp`. GitHub Actions runs
36722406454 and 36725430221 are on GitHub (external; retention set by
GitHub).

### Pre-rewrite commit IDs in the ledger

The ledger's IDs before line L71 are pre-rewrite. Mapped by subject
(`git log --format='%h %s' 238e8b7..HEAD`; the old objects still resolve
locally):

| Old | Current | Old | Current |
|---|---|---|---|
| fad1962 | b3e5729 | 749d4ea | 25571b0 |
| 50f36ea | 147d713 | 8802b30 | 50a173d |
| 9d45b4c | d451dfc | 9051342 | a19e851 |
| 73544d7 | 0bc722b | a912fd4 | 2208caa |
| ca0de6b | 642cbbd | 40b3352 | 590da5a |
| 39900b4 | 9cb70d3 | 080c956 | 12d8b34 |
| 37a26fc | 9f9bd35 | ec043ef | 23d8963 |
| 4f8d505 | b40f0d9 | f7d539a | e63b459 |
| b89e952 | 61598d9 | | |

---

## 3. Measured results

### 3.1 Conditions

Full suite, 3 seeds, 117 synthetic recordings, 4.55 h of audio, 3.20 GB;
12th Gen Intel Core i7-12700H, 32 GB, Windows 11. Generation 60.0 min
(another job shared the machine part of the time); scoring both front
ends 5.0 min (Task 14) and 6.0 and 6.2 min (Task 17, two runs). Task 17
re-scored Task 14's recordings (not regenerated); Envelope results were
identical in all 126 result files. Groups A–G and "H, oracle" use oracle
channels; the rest run the whole pipeline. No parameter was tuned.
Sources: [SP] §8b "Measured: Matched against Envelope" (opening
paragraphs); [T14] "Run"; [T17] steps 3–4. Measured.

Three code states are compared:

- **Envelope**: unchanged throughout (same results at Task 14 and 17).
- **Matched, Task 14**: code of bb48b01/c9a2aa4 (growth bound applied per
  speed update, the defect; fragments counted for speed).
- **Matched, Task 17**: code of 76e2c27 (Tasks 15 and 16 applied).

### 3.2 Group A (sensitivity, additive white Gaussian noise, oracle)

S₅₀₀ at which CER falls to 0.10 / 0.05, in dB (192 signals per speed, 12
stations per S₅₀₀ point). Measured. Sources: [SP] §8b first table and
the paragraph after it; [sum] "A sensitivity" (Task 17); [T14] "Key
numbers"; [T17] step 6.

| Speed | Envelope | Matched, Task 14 | Matched, Task 17 |
|---|---|---|---|
| 12 WPM | 7.2 (6.8 to 7.4) / 7.7 (7.5 to 7.8) | 0.2 (−0.5 to 2.2) / 2.3 (−0.0 to 3.4) | −0.2 (−0.5 to 0.7) / 1.2 (−0.1 to 18.1) |
| 25 WPM | 5.1 (4.8 to 5.2) / 5.6 (5.5 to 5.7) | 2.7 (−0.0 to 10.4) / 3.4 (1.7 to 11.2) | 1.1 (0.4 to 1.4) / 1.7 (1.4 to 1.8) |
| 40 WPM | 6.0 (5.9 to 15.2) / 14.8 (7.0 to 15.7) | 3.1 (2.0 to 3.6) / 3.7 (3.3 to 12.4) | 2.9 (2.2 to 3.3) / 3.6 (3.3 to 4.3) |

Pooled group A CER over all S₅₀₀ points, Task 17 ([sum] "A
sensitivity"): Envelope 0.862 (0.674–1.037), 0.543 (0.446–0.650), 0.445
(0.381–0.512); Matched 0.290 (0.233–0.353), 0.306 (0.245–0.368), 0.358
(0.297–0.422) at 12/25/40 WPM.

Reading ([SP] §8b "Reading the results", Group A):

- Matched is 3.1–7.4 dB better by the point estimates at CER 0.10, with
  disjoint intervals at every speed after Task 17 (at 25 WPM in Task 14
  the intervals overlapped).
- Matched is **above** the design expectation of about −2.6 to 0 dB at
  25 and 40 WPM. The expectation rests on the acquisition floor
  S₅₀₀ = −2.5 dB (derived) and −2.6 to −1.8 dB for 50% of marks keyed
  (simulated) ([plan] Scope, "Deliberately not in this plan").
- The wide CER-0.05 interval at 12 WPM after Task 17 (−0.1 to 18.1 dB)
  comes from regression R2 (§3.8).
- Ragchew (group G, 25 WPM, oracle), CER 0.10: Envelope 6.9 (6.6 to
  7.1) dB; Matched 3.8 (3.4 to 5.3) dB in Task 14, 3.1 (2.8 to 3.2) dB
  in Task 17. CER 0.05: Envelope 7.6 (7.5 to 7.7) dB; Matched 9.2 dB
  (Task 14) and 3.8 dB (Task 17), no interval for either ([SP] §8b,
  "Groups G and H").

### 3.3 CPU

Measured, ms/s. Sources: [SP] §8b CPU table and the paragraph after it;
[FF] "I5" (split by run type); [T17] step 6.

| | Envelope | Matched | Matched / Envelope |
|---|---|---|---|
| Process, Task 14 | 0.202 | 0.381 | 1.89 |
| Process, Task 17 runs 1 and 2 | 0.235, 0.244 | 0.444, 0.472 | 1.89, 1.93 |
| Decoders, Task 14 | 0.014 | 0.177 | 12.6 |
| Decoders, Task 17 runs 1 and 2 | 0.016, 0.017 | 0.201, 0.218 | 12.4, 13.0 |
| Task 14, oracle runs only (340 465 channel-seconds) | 0.163 | 0.324 | |
| Task 14, detector runs only (44 031 / 44 196 channel-seconds) | 0.501 | 0.816 | |

Task 14 totals: 384 495 (Envelope) and 384 661 (Matched)
channel-seconds. Envelope rose 16–21% between Task 14 and Task 17 with
identical code and results, so cross-session CPU comparisons on this
machine are good to about a fifth (the cause, the machine's state, is
inferred; [T17] concern 4). Within a session the ratios hold, so Tasks
15 and 16 add no resolvable decoder cost.

### 3.4 Tracker accuracy

Group F (tuning, oracle), Task 14 code: median frequency error 0.11 Hz
at S₅₀₀ = 5 dB (maximum 0.51 Hz) and 0.13 Hz at 0 dB, 30 signals each;
Envelope 5.9 Hz (bin rounding, no re-centering). Target ±2 Hz (spec
§5.2). Smoke recording: 0.00–0.08 Hz. Measured. Sources: [T14] "Key
numbers" and "Tests"; [SP] §8b CPU/frequency table.

- Matched CER does not depend on the offset from the bin center (0 to
  11.7 Hz) beyond the intervals ([SP] §8b "Group F").
- Drift of 1 and 2 Hz/s in oracle mode is **not meaningful** for
  Matched: with no detector the anchor stays on the labeled start
  frequency and the tracker covers only ±12 Hz (Matched CER 0.232 and
  0.532 in Task 17; 0.486 and 0.777 in Task 14). Through the detector
  the anchor follows drift (Task 13's `MatchedReportsDriftingFrequency`,
  1 Hz/s: published lag 1.47 Hz, [ledger] L85); no suite group measures
  drift through the detector yet ([SP] §8b "Group F").

### 3.5 Smoke check (CI recording `bench/smoke.sh`, 964 symbols)

Measured. Envelope 0.0353 = 34 edits; it has not changed since Task 2.

| Code | Where | Envelope CER | Matched CER | Source |
|---|---|---|---|---|
| d451dfc (Task 2) | Windows, local | 0.0353 | — | [ledger] L14 |
| 842ce58..d02b0ee (Tasks 13–14, final wave) | Windows, local | 0.0353 | 0.0622 (60 edits) | [FR] work log 4; [FF] "Tests" |
| 3c1b8ce | GitHub Actions run 36725430221, **Windows and Linux** | 0.0353 | 0.0622 | [ledger] L110 |
| 59cd450 (Task 15) | Windows, local | 0.0353 | 0.0622 | [T15] "Full verification" |
| 76e2c27 and 346b9d5 (Tasks 16–17) | Windows, local | 0.0353 | 0.0436 (42 edits) | [T16] "Smoke"; [T17] step 1 |
| 76e2c27 and later | Linux | — | **not measured** | [T17] step 7 |

- Oracle channels, Matched, Task 13 code: 0.0145 (the +7617.6 Hz station
  0.000) ([ledger] L89).
- The Matched smoke CER was dominated by one station (+7617.6 Hz,
  18.5 WPM, S₅₀₀ 25.1 dB): CER 0.234 before Task 16, 0.043 after ([SP]
  §8b "A channel that opens mid-transmission").
- Limits: Envelope max 0.09 (fails from 87 edits); Matched max 0.07
  (fails from 68 edits), heuristic: 0.0622 + 6/964 = 0.0685, rounded up
  ([FF] "I1"; [SP] §11 "Smoke check"). ([T14] states the same CER as
  30/482 = 0.0622.)
- CI run 36722406454 (before 3c1b8ce): Windows passed; Linux failed 1 of
  163 tests, `SignalDetector.TrackFollowsItsOwnPeakWithinTheDistance`, a
  test bug (pointer into a temporary vector), fixed in 3c1b8ce
  ([ledger] L105).

### 3.6 Group H (two-station QSOs) by regime, through the detector

QSO-label CER, Envelope / Matched Task 14 / Matched Task 17. Measured.
Source: [SP] §8b "Group H by regime" and the paragraph after it.

| Regime (offsets) | QSOs | Tracks per QSO, Envelope / Matched | Envelope | Matched, Task 14 | Matched, Task 17 |
|---|---|---|---|---|---|
| same-track (0, 10, 25 Hz; drawn) | 45 | 1.00 / 1.00 | 0.118 (0.091 to 0.145) | 0.197 (0.149 to 0.251) | 0.212 (0.156 to 0.271) |
| ambiguous (50 Hz; drawn) | 9 | 1.56 / 1.67 | 0.085 (0.051 to 0.119) | 0.297 (0.162 to 0.439) | 0.334 (0.189 to 0.491) |
| separate-track (100, 200 Hz; drawn), station label | 18 | 6.78 / 6.78 | 0.616 (0.585 to 0.656) | 0.620 (0.597 to 0.636) | 0.633 (0.613 to 0.655) |

At 50 Hz the two worst Matched QSOs are exactly the two that split into
two tracks (Task 17: 0.557 and 0.374; Task 14: 0.533 and 0.415); the
other four stayed one track ([FF] "I4"; [SP] §8b "Groups G and H").
6.78 tracks per separate-track QSO is drop-and-rebirth per over ([SP]
§11 "Tracks per QSO").

### 3.7 Where Matched is worse than Envelope (Task 17)

Criterion: paired Matched − Envelope CER whose 95% interval excludes 0,
or a crossing worse beyond its interval. Measured, 3 seeds. Source:
[SP] §8b "Where Matched is worse than Envelope" (Task 14 values after
"was").

**Still worse** (per condition):

| Condition | Paired difference, Task 17 | Task 14 |
|---|---|---|
| C, bug keying, imbalance +0.1 | +0.349 (+0.192 to +0.467) | +0.317 |
| E, interferer 20 Hz away at +10 dB re wanted key-down power | +0.221 (+0.088 to +0.352) | +0.349 |
| H detector, same-track, drawn offsets | +0.104 (+0.053 to +0.172) | +0.078 |
| H detector, ambiguous, drawn offsets | +0.442 (+0.368 to +0.507) | +0.352 |
| H detector, separate-track, per station: drawn / 200 Hz | +0.055 / +0.034 | +0.057 / +0.029 |
| Strong signals, S₅₀₀ 30, 50, 60 dB | +0.018 to +0.020 | same |
| Pause of 20 s | +0.064 | same |
| Tune-up carrier 1 s / 2 s | +0.562 / +0.899 | same |
| C, paddle, imbalance +0.1: CER-0.10 crossing | Envelope 8.4 dB (5.0 to 9.5); Matched never | same |

**Newly worse after Tasks 15–16**: C bug +0.0 (+0.239, +0.062 to +0.389;
was +0.117, interval included 0); C hand +0.1 (+0.157, +0.070 to +0.242;
was +0.069); D 10 WPM (+0.061, +0.027 to +0.121; was +0.021); H
same-track 10 Hz (+0.043, +0.006 to +0.081) and its oracle copy (+0.040,
+0.007 to +0.078).

**No longer worse**: G ragchew at CER 0.05 (9.2 → 3.8 dB); C machine
+0.1 at CER 0.10 (8.4 dB → ≤ 5.0 dB).

**Per S₅₀₀ point** (paired over 12 stations in A, 9 in B and C, 6 in
G): 30 of 209 points favor Envelope and 76 favor Matched (Task 14: 31
and 73); about 10 of 209 would exclude 0 by chance. Envelope wins at
high S₅₀₀ with slow fading and random timing (group B, 9 points, was
18), in group C at 5–20 dB with bug, hand or positive-imbalance keying
(11 points, was 8), group A 12 WPM at 8–18 dB (5 points, new: R2), and
group G at 8–20 dB (+0.012 to +0.018). The hypothesis for the high-S₅₀₀
loss (Matched marks about 7 ms longer at 25 WPM from the amplitude
estimate's ramp bias) is not measured; the fading loss is not diagnosed.

**Group B against VE3NEA** (no-space CER, paddle / hand key, 24 WPM,
f_D 0.1 Hz, S₅₀₀ 1.78 / 7.78 / 17.78 / 57.78 dB, about 1950 reference
characters per point): DeepCW 0.373 / 0.137 / 0.025 / 0.005 and 0.412 /
0.186 / 0.082 / 0.063; Envelope 0.730 / 0.677 / 0.292 / 0.179 and 0.805 /
0.541 / 0.369 / 0.295; Matched (Task 17) 0.696 / 0.577 / 0.276 / 0.260
and 0.663 / 0.460 / 0.437 / 0.504. Both are far from VE3NEA and the gap
does not close at high S₅₀₀; that the cause is hard-decision timing is
inferred ([SP] §8b VE3NEA table).

### 3.8 The two regressions of Tasks 15 and 16

Found by Task 17, attributed by scoring the whole suite with builds at
74182aa (before Task 15) and 59cd450 (Task 15 alone) ([T17]
"Attribution runs"); diagnosed with instrumented scratch builds whose
scoring reproduced Task 17's pooled figures exactly ([RD] "Scope").
Pooled Matched CER, 3 seeds.

| | Condition | Task 14 → Task 15 → Tasks 15+16 | Caused by |
|---|---|---|---|
| **R1** | D, speed step 20 → 35 WPM | 0.113 (0.058 to 0.163) → 0.394 → 0.394 (0.222 to 0.499) | Task 15 |
| **R2** | A, 12 WPM, first-word CER at S₅₀₀ 6–20 dB | 0.585 → 0.591 → 0.985 (no interval) | Task 16 |
| same mechanism as R1 | C bug +0.0; bug −0.1; hand +0.1 | 0.505 → 0.570 → 0.622; 0.304 → 0.429 → 0.431; 0.424 → 0.550 → 0.510 | mostly Task 15 |
| same mechanism as R2 | D 10 WPM | 0.073 → 0.077 → 0.112 | Task 16 |

**R1 mechanism** ([RD] "Regression 1", measured on `D-speed-s1` signal
0; step at 27.55 s):

1. At 20 WPM the dit is 60 ms and K = 73 (48.7 ms boxcar); at 35 WPM
   the dit is 34.3 ms and the dah 103 ms.
2. The first 35 WPM dah is timed 101.3 ms, between the old clusters
   (dits 57–67 ms, dahs 178–184 ms). No neighbor ratio in the sorted
   24-mark window reaches 1.8 (101/67 = 1.51, 178/101 = 1.76), so the
   estimator takes the "all alike" branch ([SP] §8, step 10) and uses
   the window mean, 102.3 ms, as the dit. The bound limits it to
   1.25 × 60.6 = 75.8 ms, K = 91. This trigger is present in Task 14's
   code too.
3. Before Task 15, the widened filter re-opened the mark and each
   re-opening grew the filter again, stretching the mark to 171 ms,
   inside the dah cluster; the split returned (ratio 2.57) and the dit
   fell back to 60.5 ms. The defect produced an accidental recovery.
4. After Task 15, each re-opening restores the pre-mark state (K = 73),
   the shorter filter keys up one sample later, the update gives K = 91
   again: a one-sample limit cycle, 12 re-openings (27.6553–27.6620 s).
   The mark is timed 108.0 ms, still between the clusters. Then the
   filter grows ×1.25 per mark, the 34.3 ms element spaces fill in
   under a 60.7 ms and longer boxcar, marks merge (182.7, 533.3 ms), the
   window mean settles near 110 ms, and the text stops.
5. Three more signals show the same trigger (s2-0, s3-0, s3-6:
   intermediate mark 109.3–110.7 ms, best ratio 1.67–1.76); s2-6
   escaped (its "all alike" mean was 42 ms). Single-signal oracle CER,
   Task 14 code / Task 15 / Tasks 15+16, for signals s1-0, s1-6, s2-0,
   s2-6, s3-0, s3-6 (seed-signal): 0.208 / 0.481 / 0.481, 0.126 / 0.463
   / 0.484, 0.078 / 0.468 / 0.442, 0.011 / 0.011 / 0.011, 0.177 / 0.529
   / 0.529, 0.108 / 0.494 / 0.494.

Verdict ([RD]): not a coding defect in Task 15; it applies the owner's
per-mark rule as written. The runaway is step (3) of the documented
start-up runaway chain ([SP] §8b "Start-up runaway"): the "all alike"
branch averages a two-cluster window whenever one mark sits between the
clusters.

**R2 mechanism** ([RD] "Regression 2", measured on
`A-awgn-12wpm-0-s3` signal 22, S₅₀₀ 12 dB):

1. At 12 WPM the dit is 100 ms. The decoder starts at the 25 WPM guess,
   48 ms, so its dit/dah boundary (2 dits) is 96 ms: a true 12 WPM dit
   lies above it.
2. The squelch opens during the first mark (76.0 ms, a fragment). Task
   16 correctly excludes it from the speed estimate.
3. The next two counted marks are true dits (101.3, 100.7 ms). The
   window is "all alike" and its mean exceeds 2 × 48 ms, so the branch
   takes them as dahs: dit = 101/3 = 33.7 ms. Every 100 ms dit now reads
   as a dah ("T").
4. 33 ms is a fixed point of that branch until the first dah (300 ms)
   forces a split; the climb from 33 to 99 ms under the ×1.25 bound then
   takes ln 3 / ln 1.25 = 4.9 marks (derived).
5. Before Task 16 the 76 ms fragment made the first window [76, 101],
   mean 88.7 ms < 96 ms, so the marks were read as dits: the fragment
   broke the tie by accident.
6. Survey of 96 group A 12 WPM signals at S₅₀₀ 6–20 dB: 80 exclude only
   the first mark, 1 none, 15 exclude one or two more (the squelch
   closed briefly). The estimate fell below 50 ms within the first 12
   updates in 37 of 96 after Task 16 against 24 of 96 before: the
   mechanism predates Task 16; Task 16 made it common.

Verdict ([RD]): Task 16's rule works as stated; the loss is a design
consequence of the "all alike" branch deciding dits against dahs from
the current estimate, which at start-up is the 25 WPM guess. Any station
slower than 12.5 WPM whose first two counted marks are dits is read at
three times its speed.

Both regressions share one cause: the speed estimator's "all alike"
branch, combined with a filter whose length follows that estimator. The
owner's reading on 2026-09-30 was that the fixes patch symptoms of a
fragile speed-estimator/filter feedback loop (memory file, §2).

---

## 4. Decisions made on the owner's behalf

Every "Ruling" line in [ledger], grouped by task. "If wrong" is the
ledger's own statement; where the ledger gives none, the cost is my
reading and is marked *(assessed)*.

### Before and during Tasks 1–9, 11

| Where | Ruling | Why | If wrong |
|---|---|---|---|
| Task 9, preflight ([ledger] L6) | Task 9 proceeds with the guarded references to the then on-hold Tasks 13/14 (`tracked_freq_hz`, `--front-end`) | The plan guards them explicitly | Task 9's runner needs a small edit when 13/14 land. (It did: Task 14 found the runner passed no `--front-end` for the baseline, which after Task 13 would have run Matched silently; fixed in 6f8ad09, [T14] "What was built".) |
| Task 1 ([ledger] L9) | DeepCW MIT license text accepted without a byte-level check (the fetch tool paraphrased) | Standard MIT text with the correct copyright line, transcribed from the plan and the research notes | A one-line comment fix |
| Task 5 ([ledger] L25) | Accept a §11 bullet describing bench handling of unscored interferers before Task 7 implemented it | Task 7 implements it (test `UnscoredSignalIsMatchedButNotCounted`); §11 is the definitions section | The doc is stale between two commits on an unmerged branch |
| Task 7/8 ([ledger] L35) | From Task 8 on, smoke runs use the Release bench; Tasks 2–6 smoke runs against a stale Debug bench accepted | Tasks 2–6 changed only Python; the stale bench had the identical milestone-1 engine | Tasks 2–6 smoke evidence is weaker than reported; Task 7's Release run covers the same recording |
| Task 7 ([ledger] L37) | Document the first-word scoring bias (ambiguous edits charged to the earliest position) as a heuristic with its direction; pin it with a test; backlog a neutral attribution; do not change the attribution | The owner needs honest labels; changing the attribution is his design choice | First-word figures stay biased upward, but labeled (smoke first-word CER 0.7647 against CER 0.0353) |
| Task 7 ([ledger] L38) | `bench/smoke.sh` picks the Release bench deterministically (fails if ambiguous) | Otherwise Tasks 8 and 11 could test a stale binary | A small conflict when Task 13 revises the file |
| Task 9 ([ledger] L52) | The summary never shows false precision: crossings only when every point holds ≥ 2 scored signals; no interval for rows with < 2 signals; a lowest-point crossing prints "≤ x" | A zero-width "95% interval" is a false statement | Some rows lose numbers the owner wanted (restorable) |
| Task 9 ([ledger] L53) | For oracle recordings the † (wrong-view) rule keys on the measured channel passband: QSO labels fit when the answering station is < 150 Hz away (the channel's −6.02 dB point relative to the passband) | The detector-bin rule is wrong for oracle channels (100 Hz passes at −1.17 dB relative to the passband) | The † moves for 100–150 Hz oracle rows only |
| Task 11 ([ledger] L60) | Rename the front end's noise RMS to σ_v in §0/§8b/§10; take the doc minors and the code minors (rescale `power_ring_` with `floor_ring_` on a K change; clamp K to the acquisition..5 WPM range; reject non-finite/non-positive dit; `kLiftWeight` from a duration, same value at 1500 samples/s) | The owner checks formulas against the doc; robustness fixes that change no parameter value | Small behavior change during the 2K samples after a filter-length change |

### Option-1 plan revision (2026-09-29)

| Where | Ruling | Why | If wrong |
|---|---|---|---|
| [ledger] L62 | Oracle rows for drift, and for QSOs beyond ±12 Hz, are marked "not meaningful under option 1" instead of giving oracle mode a truth-following anchor | The detector rows measure drift and turnovers; a label-driven anchor adds engine API and bench logic nobody asked for | No oracle view of the tracker on drift |
| [ledger] L63 | Tests whose counts were not simulated for option 1 run as written; a shortfall stops the implementer, who reports the measured count and mechanism; no silent threshold changes | No tuning without evidence | A stop mid-task |
| [ledger] L68 (review I1) | In oracle mode the tracker's anchor is the exact labeled frequency, not the bin center | Removes an up-to-11.72 Hz offset against the ±12 Hz range at no design cost | Oracle rows differ slightly from a detector-driven anchor |
| [ledger] L69 (review I2) | No re-simulation; the plan states the derivation for the low-risk tests and marks four at-risk tests (100 Hz at +10 dB re A, `MatchedIgnoresStrongerNeighbor`, `MatchedDecodesAtThreeDbS500`, `MatchedFollowsSpeedChange`) report-on-fail | Per L63 | A stop mid-Task 12. (None stopped: Task 12 measured 20-seed passes 14/10, 20/17, 20/13, 20/18, 20/18, 20/18, 19/18, 20/17 against their thresholds, [ledger] L80.) |
| [ledger] L70 (review I3 and minors) | Fix in the plan text | Doc consistency (σ_v) | *(assessed)* none beyond plan wording |

### Tasks 10, 12, 13, 14 and the final review

| Where | Ruling | Why | If wrong |
|---|---|---|---|
| Task 10 ([ledger] L74) | Station offset gets its own symbol (first f_s, then f_off because f_s reads like the sample rate); formulas written with τ_L = 8/r = 5.333 ms; doc minors; reject non-finite offsets and anchors; check `fine_tune_hz` below the unambiguous range; tests for the coherence gate and NCO phase continuity | Doc precision; small robustness; no parameter change | *(assessed)* none measurable; the fix round replaced the controller's 10% bias figure with the implementer's derivation 2.8/6.0/32% at K = 24/58/288 ([ledger] L76) |
| Task 13 ([ledger] L86) | `Engine.MatchedReportsDriftingFrequency` compares text with the same trimming the turnover tests use, `ends_with(segment(...), "PARIS ×6")` | The brief's raw `ends_with` contradicts the decoder's designed trailing word space | A trailing-space regression would go unnoticed by this one test |
| Task 14 ([ledger] L96) | Fix round on docs only (and a `summary.json` flag): state plainly that the code departs from the owner's ×1.25-per-mark decision, cite the mechanism, mark it as a defect for the owner; do not fix the code in Task 14 | The code defect is the owner's call | *(assessed)* the defect stays in code until the owner decides (he did: Tasks 15–17) |
| Final review ([ledger] L101) | One fix wave for I2–I5, M1–M10 and six deferred minors; I4 fixed by making group-H regimes follow the front end and regenerating the summary from existing results; M3 (tracks converging) documented plus backlog, no code; M2: the test comment states it checks per update and passes despite the defect | Doc truth before merge; no engine behavior change | *(assessed)* the implementer deviated on I4 (an ambiguous band 47–93.9 Hz instead of "separate at or beyond D"), on measured evidence (4 of 6 50 Hz QSOs stayed one track); every suite QSO kept its regime, so no figure changed ([FF] "I4") |

### Addendum, Tasks 15–17 (2026-09-30)

| Where | Ruling | Why | If wrong |
|---|---|---|---|
| [ledger] L107 (owner may override) | Task 16's rule applies at warm-up **and** at re-acquisition | The first key-down after keying becomes possible is not reliably observed in either case | One mark of speed evidence lost per over and per channel opening; one line at re-acquisition removes it. **Materialized in part**: removing the first mark exposed R2 (§3.8) |
| [ledger] L108 (owner may override) | Task 15 restores the filter length at a merge, as designed (re-open bounded by 0.125·T_v,old = 0.1 of the old dit, 4.8 ms at 25 WPM, derived) | The owner's approved fix restores it | Drop the length restore. **Materialized in part**: the restore removed the accidental recovery and created the one-sample limit cycle of R1 (§3.8; [RD] proposal 3) |
| [ledger] L110 (addendum review I1) | Adopt the reviewer's stronger "observed" rule: a key-down counts only if, since the last non-keyable sample, a keyable sample had the key up and g < −1 nat | Fewer holes than "first keyable sample" (dead band; squelch closing and re-opening) | *(assessed)* more marks excluded: 15 of 96 group A 12 WPM signals lost one or two more ([RD] R2 survey) |
| [ledger] L110 (review I2) | Reword the Envelope claim ("never a short fragment" was wrong; the smoother dominates) | Doc truth | *(assessed)* none |
| [ledger] L110 (review M4) and L119 | Tighten `smoke-matched.json` only if the Matched smoke CER is lower on **both** CI platforms; Task 17 leaves it unchanged | One hop of channel opening can move tens of edits; Linux unmeasured | *(assessed)* the CI check stays loose: it fails from 68 edits against 42 measured on Windows |
| [ledger] L121 | Diagnose both regressions (read-only, scratch builds) before review or merge; a fix design goes to the owner if it is a design choice | The regressions came from fixes the owner approved | *(assessed)* time only; the result is [RD] |

### Deviations accepted without a ruling line

- Task 11: §8b text for the not-yet-wired component describes it as it
  was (doc-truth rule); approved by the reviewer ([ledger] L58–L59).
- Task 14: kept `bench/smoke.sh`'s deterministic binary selection
  instead of the brief's `find | head -n 1` ([T14] "What was built").
- Task 15: two extra present-tense defect sentences in §8b reworded
  ([T15] "Concerns").

### The owner's own decisions

| Date | Decision | Source |
|---|---|---|
| 2026-09-27 | Test transmissions include whole ragchew QSOs, not only CQs and contest exchanges | [plan] Scope A; Task 1 |
| 2026-09-27 | Answering stations 0–200 Hz from the caller, reported in three regimes; backlog item to measure real offsets | [plan] Scope A; Self-review table |
| 2026-09-27 and 2026-09-29 | No parameter tuning without strong evidence; tuning deferred to benchmark results | [plan] 2026-09-29 decision 6; Self-review table |
| 2026-09-27 | Choice of default front end deferred (replaced on 2026-09-29) | [plan] "Owner decisions of 2026-09-29" heading |
| 2026-09-29 | Scope: Tasks 1–9 and 11 only until the channel design is decided | [ledger] L3 |
| 2026-09-29 | Hard deny in `git_commit_gate.py` for attribution lines | [ledger] L16 |
| 2026-09-29 | Plan decisions 1–7: one channel distance D in Hz; Matched the default in Task 13, Envelope selectable and pinned in CI, a worse regime reported not reverted; ×1.25 dit growth per mark; 20-seed tests with wide binomial margins; limits postponed to backlog; no tuning; physical units | [plan] "Owner decisions of 2026-09-29" |
| 2026-09-29 | Option 1: detector alone decides a channel's station (tracks follow their own peak within D = 47 Hz); tracker fine-tunes within ±12 Hz of the detector's frequency; growth bound also on the first follow step; no channel merging; slow drift only; three stated limits to the backlog | [plan] "Owner decisions of 2026-09-29, option 1"; [ledger] L49 |
| 2026-09-29 | Rewrite history to drop the attribution trailer from 0f8bc4f (now 50c94b1) on main, milestone-2, research-verification; force-push main (done: main 5c352af → 238e8b7) | [ledger] L49, L71 |
| 2026-09-29 | Delete backup tags `pre-rewrite/*` and `refs/original/*` | [ledger] L75 |
| 2026-09-29 | Task 13, Matched worse on smoke (0.0622 against 0.0353): report, do not revert | [ledger] L87 |
| 2026-09-30 | Write this results file; push `milestone-2` (done, CI run 36722406454); fix the growth-bound and mid-transmission defects before merge (Tasks 15–17) | [ledger] L104; [plan] Addendum |
| 2026-09-30 | Asked for the list of fixes applied and proposed (§5) | this task's brief |
| 2026-09-30 | The matched-filter and speed-estimation process will be replaced by a new design; coding on it paused; revisit every fix in §5 then | [ledger] L124; memory file (§2) |

---

## 5. Fixes applied and fixes proposed

Per the owner's 2026-09-30 decision, **every entry in tables A and B is
to be revisited when the new process exists** (keep, change or drop).

### A. Decoder and signal-processing code fixes (applied)

| ID | Fix | Commit | Evidence | Status |
|---|---|---|---|---|
| A1 | Dit growth bound (×1.25) applied once per physical mark: at each key-up counted for speed, save {dit estimate, smoothing constant, filter's dit and length, marks since re-acquisition, speed window}; a dropout merge restores it | 59cd450 (Task 15) | RED: K 24 → 125 within one mark on a clean 12 WPM signal; a dah counted 9 times ([T15]). Runaways 0.464 → 0.000 and 0.981 → 0.074 (0.015 with A2) ([SP] §8). **Caused R1** (§3.8) | Revisit |
| A2 | A mark whose key-down was not observed (no keyable key-up sample with g < −1 nat since keying became possible) is decoded but not counted for speed | 76e2c27 (Task 16) | Smoke Matched 0.0622 → 0.0436; the +7617.6 Hz station 0.234 → 0.043 ([T16]). Late openings that lost the rest of their text now decode after the first word ([SP] §8b late-opening table). **Caused R2** (§3.8) | Revisit |
| A3 | Matched front-end robustness: σ_v naming; rescale `power_ring_` with `floor_ring_` on a K change; reject invalid dits and clamp K to the acquisition..5 WPM range; lift weight from a duration | 590da5a (Task 11 fix round) | No parameter value changed ([ledger] L60) | Revisit |
| A4 | Tracker robustness: reject non-finite offsets and anchors; check `fine_tune_hz` below the unambiguous range; tests for coherence gate and NCO phase continuity | c99f5e2 (Task 10 fix round) | [ledger] L74, L76 | Revisit |

### B. Plan-level design fixes (built into the design before code)

| ID | Fix | Origin | Plan commit | Evidence (simulated unless stated) |
|---|---|---|---|---|
| B1 | Three-tap noise guard: σ̂² updated from v[n−K] only if v[n], v[n−K], v[n−2K] all have \|v\|²/(2σ̂²) < κ = 1.75, truncation divided out with m(κ) = 0.632 (derived); replaces a self-biasing posterior guard | Review C1 | 23d8963 | Noise alone σ̂/σ 0.96–1.04 over 20 seeds × 120 s (old guard: bistable, 7/10 seeds at 0.53–0.57) (`kz4ap-m2a-plan-review.md`, resolutions) |
| B2 | Warm-up 0.32 s at the acquisition width (20th percentile); neighbor taps κ_n = 4; noise floor from a low quantile; derived recovery about 43 s after a sustained 6 dB noise rise | Re-review I-1 | e63b459 | `kz4ap-m2a-plan-rereview.md`, resolutions |
| B3 | Squelch scaled with filter length, a_min = 3·(K/24)^(1/4) (in [SP]: 3·(T_v/16 ms)^(1/4)); acquisition floor S₅₀₀ ≈ −2.5 dB at every speed (derived) | Re-review I-2 | e63b459 | Same file |
| B4 | Re-acquisition after a silence of max(0.5 s, 12 dits): K back to 24 samples (16 ms), amplitude estimate restarted; after 2 s with nothing keyed the old narrow filter returns; a new speed window at each re-acquisition, the old one restored if nothing is keyed | Review C2; re-review I-2; final check F-1 | 23d8963, e63b459, 5606d3b | Turnover at 25–50 Hz: 0/112 → 110–112/112 (review resolutions); 25 Hz at −6 dB re A: 100/100 (final-check resolutions) |
| B5 | Noise-floor clean fraction 0.4 → 0.25; floor restart ratio 4 (heuristic) | Final check F-3 | 5606d3b | Per-seed failures 3/400 and 6/400 → 0/400 (`kz4ap-m2a-plan-final-check.md`, resolutions) |
| B6 | ×1.25 dit-growth bound per mark while following; extended to the filter's first follow step | Owner 2026-09-29 decision 3 and option-1 decision 3 | b3e5729, 0adc1b7 | Without it, runaway (K ≥ 200 samples) in 12 of 30 runs at 50 Hz, 0 dB re A (`kz4ap-option1-sim.md` §8.4 item 2). Applied per update until A1 |
| B7 | Option-1 channel design: detector decides the station; tracker ±12 Hz around the detector's frequency; no merging | Owner 2026-09-29 | 0adc1b7; code 7f3b9e8, 2aefdee, 842ce58 | Merges 0 in 1080 runs; B 60–200 Hz away decoded on its own track from −6 dB re A up (`kz4ap-option1-sim.md` §8.2) |
| B8 | Tracker update gates: weight ≥ 0.6 and coherence ≥ 0.3; an estimate outside the allowed range empties the average | Re-review I-4 | e63b459 (range later replaced by option 1's ±12 Hz) | 100 Hz neighbor at −6/0/+10 dB re A: f̂ 0.0 ± 0.1 Hz (re-review resolutions) |
| B9 | Oracle anchor at the labeled frequency, not the bin center | Option-1 review I1 ([ledger] L68) | 0adc1b7 | Derived: removes an offset up to 11.72 Hz |

### C. Benchmark and test fixes (applied)

| ID | Fix | Commit | Source |
|---|---|---|---|
| C1 | `bench/smoke.sh` picks the Release bench deterministically (was: a stale Debug bench first) | 61598d9 | [ledger] L34, L38 |
| C2 | First-word scoring bias documented, pinned by a test, backlogged | 61598d9 | [ledger] L37 |
| C3 | Summary without false precision (crossings, intervals, "≤ x") | a19e851 | [ledger] L52 |
| C4 | Oracle † rule by the channel passband | a19e851 | [ledger] L53 |
| C5 | Runner passes `--front-end envelope` or `--front-end matched` explicitly (the baseline would otherwise have run Matched after Task 13) | 6f8ad09 | [T14] "What was built" |
| C6 | "not meaningful (oracle anchor)" marking; `summary.json` flags it on Matched rows only | 6f8ad09, c9a2aa4 | [T14]; fix round item 10 |
| C7 | Group-H regimes per front end (Matched: same-track < 47 Hz, ambiguous 47–93.9 Hz, separate ≥ 93.9 Hz; derived from the ±½-bin interpolation clamp) | d02b0ee | [FF] "I4" |
| C8 | Drift test compares trimmed text | 842ce58 | [ledger] L86 |
| C9 | Linux: detector test keeps the track list alive (dangling pointer) | 3c1b8ce | [ledger] L105 |
| C10 | Growth-bound test checks K per physical mark (it passed with the per-update defect); new dropout-merge test | 59cd450 | [T15] |
| C11 | Suites sized for ≥ 1000 characters per S₅₀₀ point over 3 seeds; VE3NEA's no-space metric; his 2 ms centered keying edges for group B | 23d8963 | `kz4ap-m2a-plan-review.md`, resolutions I1, I3, I4 |

### D. Proposed, not implemented

Numbers are pooled Matched CER, 3 seeds, no intervals, from scratch
builds ([RD] tables and "Full-suite check").

| ID | Proposal | Effect measured in a scratch build | Cost | Source |
|---|---|---|---|---|
| f1 | In `update_speed`, Matched only: when no neighbor ratio reaches 1.8 but d_max/d_min ≥ 2.5 (heuristic), split at the largest ratio anyway | R1: D step 20→35 0.394 → 0.059; C bug +0.0 0.622 → 0.111; C hand +0.1 0.510 → 0.342; whole suite 0.4405 → 0.3962 | Worse where marks fragment: B f_D 3 Hz hand 0.788 → 0.922, paddle 24 WPM 0.812 → 1.083; E 50 Hz +20 dB re wanted 1.031 → 1.318, 100 Hz 0.863 → 0.928; E 20 Hz +0/+10 dB 0.786 → 0.892, 0.765 → 0.880; F drift 2 Hz/s 0.532 → 0.787 (cause inferred) | [RD] proposal 1 |
| f2 | Alternative to f1: Otsu split on ln(duration), two clusters if their geometric means differ by ≥ 1.8 | D step 20→35 0.046; nearly the same as f1 | Not run on the whole suite | [RD] R1 table |
| g1 | In the "all alike" branch, Matched only: take the marks as dahs only if the shortest of the last 24 spaces is below half the mark mean (0.5 heuristic; rationale derived: element space = 1 dit) | R2: A 12 WPM first-word 0.985 → 0.745 (Task 15 alone: 0.591); CER 0.030 → 0.021 | One more element read at the 48 ms initial dit than before Task 16 | [RD] proposal 2 |
| g2 | Update the speed from the first counted mark (Matched) | With g1: first-word 0.430, CER 0.012 | D 10 WPM signal 4 worse (15 edits against 10); with f1+g1+g2 E 150 Hz +20 dB re wanted 0.074 → 0.386 (not traced) | [RD] proposal 2, "Full-suite check" |
| — | Break the speed-estimate/filter feedback loop | Not designed or measured | — | Owner's memory file only (§2); not in the repository |
| — | Limit cycle after a merge (key toggles each sample until the longer filter lets go; squelch threshold toggles with K) | Noted, not proposed as a fix | — | [RD] proposal 3 |
| — | Hold off the frequency average for L + K samples after an anchor jump | Not built; bias ≤ 2.8/6.0/32% at K = 24/58/288 (derived upper bound, not measured) | — | [ledger] L82; [SP] §7 "Fresh average after a jump" |

Backlog items that propose fixes for measured or derived Matched limits
(`docs/backlog.md`, section headings as named):

- "Wrong or missing first characters": replay the last ~2 s when a
  track is born or a channel retunes (removes late openings; retune
  delay after a turnover within D).
- "Speed estimate derailed by a short tune-up carrier" and "Tune the
  Matched front end by measurement": tune-up carriers ≥ 1 s silence
  Matched (CER 0.542 and 0.909; mechanism inferred: noise estimate
  absorbs the carrier).
- "Tune the Matched front end by measurement", subsections: acquisition
  floor (postponed); noise-rise recovery (postponed); stray noise after
  a silence (postponed); a strong neighbor 60–70 Hz away leaks through
  the matched filter (stated limit; fix in the filter's design, for
  example a tapered filter); two stations keying at once within a few
  tens of Hz (postponed).
- "Growth bound per mark": fixed (A1), kept as the record.
- "A mark whose key-up the squelch forces": possible limit, derived
  from the code, not observed.
- "Tracks converging on one peak": possible limit, derived, not
  observed.
- "Measure where answering stations really are".
- "Smaller items worth keeping": neutral attribution of ambiguous edits
  in first-word scoring.

---

## 6. Open items and the owner's current direction

**Direction (owner, 2026-09-30).** The Matched front end's process (a
boxcar matched to the dit, whose length follows the decoder's own speed
estimate, with the ×1.25 bound, re-acquisition and the patches around
it) will be replaced by a new, refined design worked out with the owner.
Coding on the matched filter is paused. When the new design exists,
revisit every entry in §5 A and B, and the proposals f1, g1, g2 and the
feedback-loop idea, and decide keep, change or drop for each ([ledger]
L124; memory file, §2). f1, g1 and g2 are not to be implemented unless
asked.

**Branch state.** `milestone-2` is not merged. It was pushed on
2026-09-30 ([ledger] L104). The local remote-tracking ref
`origin/milestone-2` is at 3c1b8ce, so by the local record Tasks 15–17
(74182aa..346b9d5) and this file are not on the remote (I did not
contact the remote to confirm). Merging waits for the redesign.

**Open measurement and diagnosis items.**

1. Linux smoke CER for Matched after Task 16: not measured.
   `smoke-matched.json` stays at 0.07 until both CI platforms are below
   0.0622; the rule then is max(Windows, Linux) + 6/964, rounded up
   (0.05 on Windows' 0.0436 alone) ([T17] step 7; [ledger] L110).
2. R1 and R2 are diagnosed but unfixed (§3.8).
3. Tune-up carriers ≥ 1 s break Matched; mechanism inferred, not
   instrumented ([SP] §8b "Tune-up carriers of 1 s or more").
4. The fading loss at high S₅₀₀ (group B) and the gap to VE3NEA are not
   diagnosed (§3.7).
5. `H-qso-s1` QSO 6: the loss moved from the answering station (1.095 →
   0.021) to the caller (0.048 → 1.030) between Task 14 and Task 17; not
   diagnosed ([SP] §8b first-word paragraph).
6. One 25 WPM, S₅₀₀ 10 dB late-opening channel decodes no text (35
   edits in 35; Envelope 15); not diagnosed ([SP] §8b late-opening
   item).
7. Whether merged elements at low S₅₀₀ still start runaways is not
   shown ([SP] §8b "Start-up runaway").
8. Drift through the detector is tested (Task 13, 1 Hz/s) but no suite
   group measures it ([SP] §8b "Group F").
9. Owner question from Task 12, not answered in the ledger: hold off
   the frequency average after an anchor jump? ([ledger] L82; §5 D).
10. CPU: cross-session CPU comparisons on this machine are
    reliable only to about a fifth (§3.3).
11. Doc caveat owed to [SP] §8b tune-up item: Task 14 did not record
    its after-carrier cut times, so Task 17's 0.021 (1 s carrier, cut at
    2.4 s) against Task 14's 0.000 is not a before/after comparison
    ([ledger] L123; [T17] addendum). Not yet written into [SP].
12. Task 17's debug worktrees `build/wt-t14` and `build/wt-t15` are no
    longer registered (`git worktree list` shows only the main tree on
    2026-09-30); their build logs remain in `build/`. The ledger does not
    record the removal.

---

## 7. Deferred minor findings still open

From the ledger's "minor (deferred)" lines, minus those fixed by the
final fix wave d02b0ee ([FF]: Task 4 fading labels, Task 8 CPU text,
Task 13 §6 "Why neighboring bins" and "Cap", Task 13 `engine.hpp`
comment, Task 12 §7 "bounded by", Task 12 class comment), by 59cd450
(Task 12 `marks_since_reacquire_` not undone on merge), by 61598d9
(smoke.sh Debug pick), and by 346b9d5 (Tasks 15 and 16 doc minors,
[T17] "Doc changes"). The final review triaged all of these as able to
wait ([FR] "Deferred-minor triage").

| From | Finding | Ledger |
|---|---|---|
| Task 2 | `plan_intervals` recomputed 3× per `main()` run (deterministic, cost only) | L12 |
| Task 2 | `plan_signal` with repeats = 0 or empty text untested (no caller produces it) | L13 |
| Task 3 | `timed_intervals` drops all-invalid words (`keying.py:320`), giving one word gap instead of two; untested; no text source hits it | L17 |
| Task 4 | RED for the `test_generate.py` additions not separately observed (process) | L21 |
| Task 6 | `station_labels` `qso_index` indexes the full signal list (gaps); nothing reads it | L29 |
| Task 6 | `add_overs` fades each sender over the whole QSO span (extra work only) | L30 |
| Task 7 | Per-transmission range logic duplicated (`first_word_ranges`/`transmission_ranges`); `SignalScore` reused as a totals accumulator; alignment memory O(nm); test gaps (`detection_recall`/`--json` scored, `transmission_ranges` throw, greedy label-order matching with close interferers, backlogged) | L39 |
| Task 8 | Decoder timing near timer resolution; `--oracle` with empty labels silently runs the detector; JSON does not record oracle mode (the manifest does); `EXPECT_GE(decoder_seconds, 0.0)` cannot fail; `CpuTime.BusyLoopUsesCpuTime` load-sensitive; `process_cpu_seconds` returns 0.0 on failure; local `exit` shadows `::exit`; oracle Born events published in the constructor (works, since the bench subscribes first; undocumented) | L45 |
| Task 12 | `follow_after_marks` > 24 accepted (not user-exposed); chunk test lacks the re-acquisition and anchor branches; no test for `freq_offset_hz` presence; stray-noise count "4/400 vs 5/400" (plan and docs both say 4 of 400) | L81 |
| Task 13 | `ClassicalDecoder.ChunkSizeDoesNotChangeOutput` not pinned to `envelope()` (add a Matched twin; engine-level chunk test does cover Envelope); `bin_hz` computed twice; one long doc line; §8b double parenthetical; drift-test truth uses `last_end_s` (+0.25 Hz bias, within margin) | L88 |
| Task 15 | The dropout test could also assert the decoded text "PAR MMM" | L113 |
| Task 16 | Re-report the fragment durations after the fix (not found in [T17]; status unverified) | L117 |
| Task 17 | Tune-up after-carrier cut caveat (§6 item 11) | L123 |
