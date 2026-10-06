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
  spaces counted (`docs/signal-processing.md` appendix A.11). **First-word CER**
  scores the first word of each transmission and is an upper bound: it
  can exceed 1 (signal-processing.md A.11; ruling in §4 below, Task 7).
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
  (ms/s), over the bench's timed window ([SP] A.11, "CPU time per
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
**[SP]** = `docs/signal-processing.md` as it stood when this record was written (restructured 2026-10-05: the passages cited below as [SP] §7 "Measured accuracy", §8 "Once per physical mark" and "Marks whose start was not observed", §8b "Measured: Matched against Envelope" and its subsections, and §11's measured figures are now in §8 of this record; [SP]'s derivations and definitions are in its appendix, A.7 to A.11); **[plan]** = the milestone plan;
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
| `docs/signal-processing.md` | What the code does now (body and appendix since 2026-10-05). §7 and A.7 (tracker, "Frequency re-centering"), §8 and A.8 ("Once per physical mark", "Marks whose start was not observed"), A.10 parameters, A.11 definitions, "Smoke check". Its former §8b "Measured: Matched against Envelope", "Where Matched is worse than Envelope" and "Limits measured" are now §8.3 of this record |
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

---

## 8. Moved from signal-processing.md, 2026-10-05: Matched against Envelope (milestone 2a)

Moved verbatim on 2026-10-05, when `docs/signal-processing.md` was restructured into a body and an appendix (owner, option B). The text is as it stood at commit 5099a29; its references to sections of signal-processing.md ("section 8", "§8b", "section 11" …) are to that version: the derivations now sit in its appendix under the same numbers (A.6, A.7, A.8, A.8b, A.8c, A.11), and "results record" means this file.

### 8.1 From section 7, "Frequency re-centering": measured accuracy on the benchmark

- **Measured accuracy (benchmark, milestone 2, part 1):** in group F
  (oracle channels on the nearest bin, stations 0, 2.9, 5.9, 8.8 and
  11.7 Hz from the bin center, 20 and 25 WPM, 3 seeds, 30 signals per
  S₅₀₀ point; section 11, "Frequency error"), |f_tracked − f_true| had a
  median of 0.11 Hz (90th percentile 0.21 Hz, largest 0.51 Hz) at
  S₅₀₀ = 5 dB and 0.13 Hz (0.28 Hz, 0.57 Hz) at S₅₀₀ = 0 dB, within the
  ±2 Hz target at every signal; the Envelope path, which does not
  re-center, is off by the bin rounding (median 5.9 Hz, largest 11.7 Hz).
  Through the detector (band, crowded, strong, pauses, tune-up, first
  sample) the median per condition was 0.0–0.2 Hz. The re-centering's accuracy is
  therefore **measured**; its parameters (τ_f, the gates, the lag) stay
  heuristic.

### 8.2 From section 8, "Two decoders": the growth bound per physical mark (Task 15) and marks whose start was not observed (Task 16), with their measurements

**Once per physical mark (Task 15, milestone 2, part 1).** The bound is
applied in `update_speed()`, at every key-up counted for speed. Until
Task 15 it acted per speed update, against the owner's decision: when the
filter widened at a key-up, the wider boxcar still covered the mark just
ended, the key went down again within 0.3 dit, the dropout merge (step 6)
popped the mark but kept the updated `dit_s_` and `filter_dit_s_`, and
the next key-up applied ×1.25 again (measured, Task 14: 7 times on one
mark, the filter's dit 20 → 95.4 ms within 35 ms); the merge also left
the mark counted twice toward the 8 before the filter follows. Now each
key-up counted for speed saves the state its update starts from (the dit
estimate, the filter's dit and length, the count of marks since the last
re-acquisition, the speed window), and a merge that re-opens that element
restores it, so the re-measured mark gets one update, bounded once. The
widened filter can still re-open the mark it was widened at; each re-open
is merged and undone, and it stops about (T_v,new − T_v,old)/2 after
the first key-up (T_v the filter's duration), at most 0.125·T_v,old =
0.1 of the old dit, since T_v,new ≤ 1.25·T_v,old (derived for a boxcar
and a strong signal: its falling edge passes half amplitude half the
length change later; an upper bound, the ±1 nat hysteresis ends it
slightly earlier), for example 4.8 ms at 25 WPM (T_v,old = 38.7 ms): the
mark is timed through the new filter, up to that much longer, and grows
the estimate at most ×1.25. The Envelope path's merge
is unchanged (it pops the mark from the window and keeps the updated
estimate, as in milestone 1; it has no growth bound).
Measured in section 8b (Task 17; full suite, 3 seeds; Task 15 alone
scored against Task 14's code): the two start-up runaways there went
from Matched CER 0.464 and 0.981 to 0.000 and 0.074 (0.015 with Task 16
added); the speed step from
20 to 35 WPM (group D) went from 0.113 (0.058 to 0.163) to 0.394 (0.222
to 0.499), the text stopping after the step in 5 of 6 signals (not
diagnosed).
**Marks whose start was not observed (Matched; Task 16).** Keying is
possible on a sample where the Matched decoder is ready (its 0.32 s warm-up is
over) and the squelch is open (a ≥ a_min). A key-down counts as observed
only if, since the last sample on which keying was impossible, the
decoder saw a keyable sample with the key up and g < −1 nat (the key-up
threshold): evidence that the carrier was off before the mark began.
Otherwise the carrier may have been up before the key-down, while keying
was impossible or while g sat between −1 and +1 nat, so the mark's
duration may be a fragment's (Task 14: a 15.3 ms fragment of a 195 ms
dah on the smoke recording pinned the dit at 20 ms). Such a mark is
decoded but not counted for speed: it does not enter the speed window or
count toward the 8 marks before the filter follows. Every key-up by the
log-odds sets the evidence, so in steady keying every mark counts; a
dropout merge keeps the merged mark's flag. (Rule derived from what the
decoder can observe; no new parameter: the rule's threshold is the
key-up hysteresis, −1 nat.)
It applies after the warm-up (a channel opening mid-mark); to the first
mark of a channel that opens in noise (after the warm-up, a ≈ 1.6 in
noise alone, below a_min = 3, derived: ŝ² = 2σ²·ln 10 − 2σ² from the
warm-up's percentiles); after a re-acquisition (ŝ restarts at 0, so
a = 0 < a_min); and wherever else the squelch closes and re-opens (the
floor's stuck-low restart of ŝ, ŝ decaying or σ̂ rising at a weak
station). In the last three the squelch re-opens on the sample after the
next mark lifts ŝ past a_min (ŝ is updated after the squelch is
decided), and that mark's keyed start is off in either direction:
early at high S₅₀₀, where the squelch opens on the filter's rising ramp
with â well below the true a, and late at low S₅₀₀ (Task 14: 100.7 ms of
a 144 ms dah at S₅₀₀ 10 dB) (derived from the update). Each such channel
or over loses one mark of speed evidence; the filter follows after 9
physical marks there.
The end of a mark has no such rule: a key-up forced by the squelch
closing (for example the floor's stuck-low restart of ŝ in the middle of
a mark) still counts the mark for speed, with a truncated duration
(derived from the code, not observed; backlog, "A mark whose key-up the
squelch forces").
Measured in section 8b (Task 17): the smoke recording's Matched CER went
from 0.0622 to 0.0436 and its +7617.6 Hz station from 0.234 to 0.043; in
the focused late-opening run no 18.5 WPM opening is worse than Envelope
by 5 edits or more any more (6 and 3 of 80 with Task 14's code, S₅₀₀ 25
and 10 dB). One new loss, found by scoring the full suite (3 seeds) with
Task 15 alone and with Task 16 added: at 12 WPM (dit 100 ms) the first
characters of a station often read as a string of T's (group A, 12 WPM,
first-word CER over S₅₀₀ 6–20 dB 0.591 → 0.985; group D, 10 WPM, CER
0.077 → 0.112). Inferred from step 10, not instrumented: a window of
whole dits alone is longer on average than twice the initial 48 ms dit
whenever the station is slower than 12.5 WPM (dit > 96 ms, derived), so
it is taken as all dahs and the estimate falls to a third of a dit until
the first dah enters the window; why counting the first mark avoided
this was not traced.

### 8.3 From section 8b: "Measured: Matched against Envelope (milestone 2, part 1)"

Full suite, 3 seeds, synthetic recordings (`kz4ap_synth.suites`, 117
recordings, 4.55 h of audio, 3.20 GB), on a 12th Gen Intel Core i7-12700H
(32 GB, Windows 11); generating took 60 min (another job shared the
machine for part of it) and scoring both decoders 5.0 min (nothing
else running). S₅₀₀: key-down carrier power over noise power in 500 Hz,
dB; every dB value in this subsection is an S₅₀₀ (or a difference of
S₅₀₀ values) unless it names another reference. Groups A–G and the "H, oracle" copy use oracle channels (detector
bypassed, channel on the nearest bin); the rest run the whole pipeline.
Parentheses: bootstrap 95% intervals over signals (1000 resamples); none
for a row of one signal. "Envelope" is the milestone-1 path
(`--front-end envelope`, unchanged from milestone 1 by the evidence in
section 11, "Smoke check"; CI bounds its smoke CER, it does not pin bit
identity); "Matched" is the
default. No parameter was tuned for these runs (owner, 2026-09-29).
Source: `build/suite/full3/summary.md` (not in the repository; rerun
with the commands in section 11, "Suites").
**Re-measured after Tasks 15 and 16 (Task 17, 2026-09-30).** The Matched
cells below are from the code after Task 16 (76e2c27), scored on the same
117 recordings (Task 14's, not regenerated); scoring both decoders took
6.0 min on the same machine. The Envelope results were re-scored too and
are identical to Task 14's, score and decoded tracks, in all 126 result
files (the Envelope path is untouched by both fixes). Figures quoted
elsewhere as "before" are Task 14's, from the code of commits bb48b01
and c9a2aa4.

| Condition | Envelope: S₅₀₀ at CER 0.10 / 0.05 (dB) | Matched: S₅₀₀ at CER 0.10 / 0.05 (dB) |
|---|---|---|
| A, 12 WPM | 7.2 (6.8 to 7.4) / 7.7 (7.5 to 7.8) | −0.2 (−0.5 to 0.7) / 1.2 (−0.1 to 18.1) |
| A, 25 WPM | 5.1 (4.8 to 5.2) / 5.6 (5.5 to 5.7) | 1.1 (0.4 to 1.4) / 1.7 (1.4 to 1.8) |
| A, 40 WPM | 6.0 (5.9 to 15.2) / 14.8 (7.0 to 15.7) | 2.9 (2.2 to 3.3) / 3.6 (3.3 to 4.3) |

Task 14 (before Tasks 15 and 16), Matched: 0.2 (−0.5 to 2.2) / 2.3 (−0.0
to 3.4) dB at 12 WPM, 2.7 (−0.0 to 10.4) / 3.4 (1.7 to 11.2) dB at
25 WPM, 3.1 (2.0 to 3.6) / 3.7 (3.3 to 12.4) dB at 40 WPM.

Each row below is one tag of the summary; for group H the tag names the
regime and the answering station's offset. † marks the group-H view that
does not fit (section 11). A row pools every S₅₀₀ point of its condition
(groups B, C and G sweep S₅₀₀), so a row can favor Matched while its
high-S₅₀₀ points favor Envelope ("Where Matched is worse" below).

| Condition | Envelope: CER (interval) / character CER / space error rate / first-word CER | Matched: same | Matched − Envelope, paired (interval) |
|---|---|---|---|
| B fading: VE3NEA mix | 0.927 (0.811 to 1.054) / 0.967 / 0.802 / 1.632 | 0.733 (0.703 to 0.766) / 0.762 / 0.645 / 0.704 | −0.208 (−0.317 to −0.105) |
| B fading: hand 24 wpm fD 0.1 Hz | 0.697 (0.595 to 0.830) / 0.683 / 0.741 / 1.305 | 0.610 (0.573 to 0.652) / 0.586 / 0.682 / 0.548 | −0.090 (−0.214 to −0.003) |
| B fading: hand 24 wpm fD 0.3 Hz | 0.758 (0.657 to 0.884) / 0.770 / 0.718 / 1.866 | 0.654 (0.624 to 0.685) / 0.656 / 0.646 / 0.587 | −0.104 (−0.211 to −0.015) |
| B fading: hand 24 wpm fD 1 Hz | 0.937 (0.796 to 1.093) / 0.948 / 0.903 / 1.788 | 0.737 (0.717 to 0.758) / 0.765 / 0.652 / 0.686 | −0.202 (−0.351 to −0.080) |
| B fading: hand 24 wpm fD 3 Hz | 1.147 (0.974 to 1.354) / 1.136 / 1.183 / 2.468 | 0.788 (0.768 to 0.811) / 0.819 / 0.689 / 0.855 | −0.363 (−0.539 to −0.202) |
| B fading: paddle 12 wpm fD 0.1 Hz | 0.852 (0.558 to 1.220) / 0.881 / 0.759 / 1.694 | 0.513 (0.466 to 0.563) / 0.527 / 0.469 / 1.020 | −0.341 (−0.714 to −0.059) |
| B fading: paddle 24 wpm fD 0.1 Hz | 0.615 (0.478 to 0.765) / 0.650 / 0.507 / 0.948 | 0.508 (0.448 to 0.568) / 0.519 / 0.475 / 0.408 | −0.107 (−0.220 to −0.004) |
| B fading: paddle 24 wpm fD 0.3 Hz | 0.782 (0.647 to 0.940) / 0.832 / 0.629 / 1.538 | 0.594 (0.551 to 0.637) / 0.613 / 0.537 / 0.542 | −0.186 (−0.323 to −0.072) |
| B fading: paddle 24 wpm fD 1 Hz | 1.013 (0.860 to 1.181) / 1.054 / 0.885 / 3.024 | 0.752 (0.728 to 0.780) / 0.783 / 0.655 / 0.837 | −0.263 (−0.437 to −0.118) |
| B fading: paddle 24 wpm fD 3 Hz | 1.126 (0.966 to 1.312) / 1.123 / 1.134 / 2.225 | 0.812 (0.788 to 0.839) / 0.847 / 0.704 / 0.794 | −0.314 (−0.467 to −0.169) |
| B fading: paddle 40 wpm fD 0.1 Hz | 0.766 (0.696 to 0.842) / 0.804 / 0.645 / 0.866 | 0.591 (0.531 to 0.647) / 0.599 / 0.565 / 0.707 | −0.175 (−0.245 to −0.108) |
| C fists: bug imbalance +0.0 | 0.385 (0.286 to 0.513) / 0.402 / 0.328 / 0.174 | 0.622 (0.514 to 0.717) / 0.624 / 0.616 / 0.783 | +0.239 (+0.062 to +0.389) |
| C fists: bug imbalance +0.1 | 0.396 (0.301 to 0.516) / 0.403 / 0.372 / 0.214 | 0.753 (0.664 to 0.835) / 0.760 / 0.729 / 0.914 | +0.349 (+0.192 to +0.467) |
| C fists: bug imbalance -0.1 | 0.372 (0.269 to 0.487) / 0.375 / 0.358 / 0.453 | 0.431 (0.334 to 0.543) / 0.438 / 0.406 / 0.562 | +0.068 (−0.063 to +0.188) |
| C fists: computer imbalance +0.0 | 0.106 (0.023 to 0.222) / 0.100 / 0.124 / 0.044 | 0.002 (0.001 to 0.003) / 0.002 / 0.000 / 0.118 | −0.098 (−0.203 to −0.023) |
| C fists: computer imbalance +0.1 | 0.139 (0.030 to 0.255) / 0.135 / 0.152 / 0.017 | 0.002 (0.000 to 0.003) / 0.002 / 0.001 / 0.119 | −0.134 (−0.270 to −0.029) |
| C fists: computer imbalance -0.1 | 0.288 (0.113 to 0.507) / 0.283 / 0.304 / 0.063 | 0.005 (0.001 to 0.013) / 0.006 / 0.003 / 0.111 | −0.286 (−0.525 to −0.106) |
| C fists: hand imbalance +0.0 | 0.302 (0.261 to 0.351) / 0.184 / 0.729 / 0.250 | 0.300 (0.245 to 0.361) / 0.210 / 0.623 / 0.312 | −0.001 (−0.060 to +0.076) |
| C fists: hand imbalance +0.1 | 0.351 (0.301 to 0.406) / 0.245 / 0.727 / 0.544 | 0.510 (0.420 to 0.611) / 0.459 / 0.689 / 0.691 | +0.157 (+0.070 to +0.242) |
| C fists: hand imbalance -0.1 | 0.324 (0.282 to 0.371) / 0.154 / 0.899 / 0.351 | 0.288 (0.247 to 0.337) / 0.177 / 0.667 / 0.284 | −0.037 (−0.085 to +0.019) |
| C fists: machine imbalance +0.0 | 0.017 (0.008 to 0.028) / 0.018 / 0.017 / 0.033 | 0.001 (0.001 to 0.002) / 0.002 / 0.000 / 0.133 | −0.017 (−0.028 to −0.009) |
| C fists: machine imbalance +0.1 | 0.022 (0.008 to 0.041) / 0.021 / 0.027 / 0.000 | 0.002 (0.000 to 0.003) / 0.002 / 0.001 / 0.190 | −0.022 (−0.042 to −0.008) |
| C fists: machine imbalance -0.1 | 0.046 (0.019 to 0.081) / 0.050 / 0.033 / 0.000 | 0.001 (0.001 to 0.002) / 0.002 / 0.000 / 0.118 | −0.045 (−0.077 to −0.019) |
| C fists: paddle imbalance +0.0 | 0.106 (0.043 to 0.220) / 0.088 / 0.170 / 0.061 | 0.044 (0.039 to 0.050) / 0.036 / 0.074 / 0.106 | −0.061 (−0.163 to +0.001) |
| C fists: paddle imbalance +0.1 | 0.106 (0.048 to 0.214) / 0.090 / 0.162 / 0.034 | 0.116 (0.077 to 0.191) / 0.112 / 0.131 / 0.207 | +0.014 (−0.117 to +0.127) |
| C fists: paddle imbalance -0.1 | 0.089 (0.056 to 0.128) / 0.060 / 0.186 / 0.062 | 0.032 (0.026 to 0.038) / 0.021 / 0.068 / 0.234 | −0.060 (−0.100 to −0.025) |
| D speed: 10 wpm | 0.052 (0.045 to 0.063) / 0.061 / 0.019 / 1.000 | 0.112 (0.074 to 0.177) / 0.110 / 0.115 / 2.083 | +0.061 (+0.027 to +0.121) |
| D speed: 60 wpm | 0.133 (0.005 to 0.367) / 0.130 / 0.142 / 0.833 | 0.005 (0.003 to 0.007) / 0.006 / 0.002 / 0.667 | −0.138 (−0.379 to +0.001) |
| D speed: ramp 15->30 | 0.202 (0.057 to 0.433) / 0.172 / 0.314 / 0.000 | 0.000 (0.000 to 0.000) / 0.000 / 0.000 / 0.000 | −0.192 (−0.411 to −0.059) |
| D speed: ramp 30->15 | 0.000 (0.000 to 0.000) / 0.000 / 0.000 / 0.000 | 0.000 (0.000 to 0.000) / 0.000 / 0.000 / 0.000 | +0.000 (+0.000 to +0.000) |
| D speed: step 20->35 | 0.525 (0.257 to 0.845) / 0.541 / 0.474 / 0.000 | 0.394 (0.222 to 0.499) / 0.409 / 0.342 / 0.917 | −0.123 (−0.434 to +0.165) |
| D speed: step 35->20 | 0.114 (0.061 to 0.160) / 0.096 / 0.179 / 0.250 | 0.129 (0.097 to 0.162) / 0.125 / 0.143 / 0.417 | +0.016 (−0.049 to +0.066) |
| E interference: df 100 Hz, +0 dB re wanted key-down power | 0.820 (0.808 to 0.837) / 0.901 / 0.547 / 1.000 | 0.006 (0.000 to 0.019) / 0.004 / 0.013 / 0.333 | −0.814 (−0.817 to −0.808) |
| E interference: df 100 Hz, +10 dB re wanted key-down power | 0.943 (0.775 to 1.113) / 1.062 / 0.566 / 0.818 | 0.388 (0.031 to 0.972) / 0.398 / 0.355 / 0.727 | −0.574 (−1.082 to +0.009) |
| E interference: df 100 Hz, +20 dB re wanted key-down power | 1.041 (0.940 to 1.155) / 1.171 / 0.559 / 1.900 | 0.863 (0.763 to 0.974) / 0.929 / 0.618 / 1.000 | −0.191 (−0.392 to +0.034) |
| E interference: df 100 Hz, -10 dB re wanted key-down power | 0.036 (0.000 to 0.056) / 0.023 / 0.090 / 0.000 | 0.000 (0.000 to 0.000) / 0.000 / 0.000 / 0.000 | −0.037 (−0.056 to +0.000) |
| E interference: df 150 Hz, +0 dB re wanted key-down power | 0.145 (0.027 to 0.331) / 0.152 / 0.120 / 0.000 | 0.000 (0.000 to 0.000) / 0.000 / 0.000 / 0.000 | −0.137 (−0.331 to −0.027) |
| E interference: df 150 Hz, +10 dB re wanted key-down power | 0.824 (0.818 to 0.829) / 0.921 / 0.521 / 1.000 | 0.000 (0.000 to 0.000) / 0.000 / 0.000 / 0.000 | −0.824 (−0.829 to −0.818) |
| E interference: df 150 Hz, +20 dB re wanted key-down power | 0.779 (0.491 to 1.008) / 0.860 / 0.521 / 2.333 | 0.076 (0.040 to 0.129) / 0.080 / 0.064 / 1.111 | −0.689 (−0.937 to −0.362) |
| E interference: df 150 Hz, -10 dB re wanted key-down power | 0.011 (0.000 to 0.029) / 0.007 / 0.023 / 0.000 | 0.000 (0.000 to 0.000) / 0.000 / 0.000 / 0.000 | −0.010 (−0.029 to +0.000) |
| E interference: df 20 Hz, +0 dB re wanted key-down power | 0.793 (0.654 to 1.101) / 0.846 / 0.600 / 0.667 | 0.786 (0.455 to 0.978) / 0.779 / 0.812 / 1.500 | −0.077 (−0.646 to +0.245) |
| E interference: df 20 Hz, +10 dB re wanted key-down power | 0.544 (0.480 to 0.640) / 0.656 / 0.123 / 6.333 | 0.765 (0.727 to 0.803) / 0.851 / 0.438 / 1.000 | +0.221 (+0.088 to +0.352) |
| E interference: df 20 Hz, +20 dB re wanted key-down power | 0.922 (0.822 to 1.131) / 0.979 / 0.707 / 2.667 | 0.813 (0.752 to 0.899) / 0.877 / 0.573 / 0.667 | −0.128 (−0.232 to +0.013) |
| E interference: df 20 Hz, -10 dB re wanted key-down power | 0.044 (0.000 to 0.081) / 0.033 / 0.080 / 0.000 | 0.003 (0.000 to 0.009) / 0.004 / 0.000 / 0.167 | −0.043 (−0.081 to +0.009) |
| E interference: df 50 Hz, +0 dB re wanted key-down power | 0.857 (0.807 to 0.930) / 0.912 / 0.651 / 1.833 | 0.090 (0.023 to 0.235) / 0.088 / 0.096 / 1.000 | −0.757 (−0.902 to −0.571) |
| E interference: df 50 Hz, +10 dB re wanted key-down power | 0.787 (0.500 to 1.009) / 0.878 / 0.467 / 1.000 | 0.817 (0.728 to 0.872) / 0.833 / 0.760 / 1.000 | +0.041 (−0.138 to +0.365) |
| E interference: df 50 Hz, +20 dB re wanted key-down power | 0.855 (0.640 to 1.040) / 0.867 / 0.813 / 1.000 | 1.031 (0.811 to 1.404) / 1.084 / 0.853 / 0.667 | +0.160 (−0.192 to +0.500) |
| E interference: df 50 Hz, -10 dB re wanted key-down power | 0.005 (0.000 to 0.014) / 0.003 / 0.011 / 0.000 | 0.000 (0.000 to 0.000) / 0.000 / 0.000 / 0.000 | −0.005 (−0.014 to +0.000) |
| F tuning: drift 0.2 Hz/s | 0.105 (0.038 to 0.168) / 0.106 / 0.100 / 0.000 | 0.065 (0.006 to 0.166) / 0.071 / 0.043 / 0.500 | −0.041 (−0.111 to +0.021) |
| F tuning: drift 0.5 Hz/s (not meaningful (oracle anchor) for Matched) | 0.073 (0.006 to 0.173) / 0.056 / 0.130 / 0.133 | 0.022 (0.009 to 0.036) / 0.028 / 0.000 / 0.400 | −0.069 (−0.170 to +0.021) |
| F tuning: drift 1 Hz/s (not meaningful (oracle anchor) for Matched) | 0.080 (0.014 to 0.171) / 0.078 / 0.086 / 0.267 | 0.232 (0.190 to 0.276) / 0.239 / 0.207 / 0.400 | +0.138 (+0.022 to +0.241) |
| F tuning: drift 2 Hz/s (not meaningful (oracle anchor) for Matched) | 0.113 (0.046 to 0.186) / 0.086 / 0.210 / 0.000 | 0.532 (0.439 to 0.618) / 0.555 / 0.452 / 0.417 | +0.410 (+0.268 to +0.515) |
| F tuning: offset 0 Hz 20 wpm | 0.416 (0.136 to 0.716) / 0.471 / 0.213 / 0.625 | 0.049 (0.002 to 0.142) / 0.049 / 0.049 / 0.250 | −0.376 (−0.680 to −0.097) |
| F tuning: offset 0 Hz 25 wpm | 0.412 (0.189 to 0.623) / 0.475 / 0.218 / 0.500 | 0.194 (0.012 to 0.425) / 0.211 / 0.141 / 0.583 | −0.228 (−0.466 to −0.026) |
| F tuning: offset 11.7 Hz 20 wpm | 0.404 (0.141 to 0.647) / 0.479 / 0.113 / 0.333 | 0.043 (0.012 to 0.079) / 0.042 / 0.047 / 0.933 | −0.338 (−0.600 to −0.086) |
| F tuning: offset 11.7 Hz 25 wpm | 0.420 (0.161 to 0.670) / 0.469 / 0.219 / 0.300 | 0.113 (0.023 to 0.233) / 0.114 / 0.110 / 0.450 | −0.299 (−0.519 to −0.088) |
| F tuning: offset 2.9 Hz 20 wpm | 0.486 (0.143 to 0.929) / 0.488 / 0.475 / 0.579 | 0.047 (0.003 to 0.129) / 0.048 / 0.041 / 0.789 | −0.472 (−0.909 to −0.135) |
| F tuning: offset 2.9 Hz 25 wpm | 0.394 (0.131 to 0.692) / 0.429 / 0.276 / 0.533 | 0.208 (0.039 to 0.451) / 0.218 / 0.171 / 0.467 | −0.187 (−0.358 to −0.030) |
| F tuning: offset 5.9 Hz 20 wpm | 0.393 (0.128 to 0.639) / 0.433 / 0.252 / 0.476 | 0.028 (0.011 to 0.045) / 0.036 / 0.000 / 0.333 | −0.354 (−0.599 to −0.117) |
| F tuning: offset 5.9 Hz 25 wpm | 0.386 (0.148 to 0.598) / 0.439 / 0.206 / 0.312 | 0.091 (0.017 to 0.169) / 0.098 / 0.065 / 1.188 | −0.276 (−0.472 to −0.093) |
| F tuning: offset 8.8 Hz 20 wpm | 0.492 (0.204 to 0.789) / 0.515 / 0.407 / 0.647 | 0.010 (0.004 to 0.016) / 0.010 / 0.009 / 0.176 | −0.487 (−0.757 to −0.186) |
| F tuning: offset 8.8 Hz 25 wpm | 0.405 (0.148 to 0.631) / 0.471 / 0.138 / 0.400 | 0.178 (0.016 to 0.347) / 0.190 / 0.131 / 0.867 | −0.221 (−0.434 to −0.054) |
| G ragchew: ragchew 25 wpm | 0.196 (0.111 to 0.284) / 0.203 / 0.174 / 0.240 | 0.081 (0.051 to 0.116) / 0.076 / 0.096 / 0.169 | −0.117 (−0.183 to −0.056) |
| H two-station QSO: ambiguous, drawn offset | 0.130 (0.087 to 0.164) / 0.092 / 0.237 / 0.486 | 0.573 (0.456 to 0.643) / 0.607 / 0.477 / 0.820 | +0.442 (+0.368 to +0.507) |
| H two-station QSO: ambiguous, offset 50 Hz | 0.062 (0.026 to 0.101) / 0.038 / 0.128 / 0.106 | 0.215 (0.070 to 0.386) / 0.216 / 0.212 / 0.387 | +0.151 (−0.008 to +0.341) |
| H two-station QSO: same-track, drawn offset | 0.116 (0.087 to 0.146) / 0.075 / 0.231 / 0.247 | 0.220 (0.150 to 0.296) / 0.194 / 0.291 / 0.283 | +0.104 (+0.053 to +0.172) |
| H two-station QSO: same-track, offset 0 Hz | 0.150 (0.074 to 0.216) / 0.099 / 0.298 / 0.261 | 0.271 (0.092 to 0.469) / 0.251 / 0.329 / 0.332 | +0.124 (−0.027 to +0.273) |
| H two-station QSO: same-track, offset 10 Hz | 0.103 (0.036 to 0.176) / 0.067 / 0.208 / 0.246 | 0.146 (0.045 to 0.253) / 0.125 / 0.208 / 0.217 | +0.043 (+0.006 to +0.081) |
| H two-station QSO: same-track, offset 25 Hz | 0.107 (0.035 to 0.216) / 0.067 / 0.221 / 0.377 | 0.183 (0.043 to 0.363) / 0.168 / 0.225 / 0.274 | +0.076 (−0.070 to +0.255) |
| H two-station QSO: separate-track, drawn offset † | 0.723 (0.702 to 0.746) / 0.722 / 0.723 / 0.738 | 0.790 (0.782 to 0.797) / 0.790 / 0.789 / 0.814 | +0.067 (+0.052 to +0.082) |
| H two-station QSO: separate-track, offset 100 Hz † | 0.428 (0.169 to 0.679) / 0.415 / 0.467 / 0.542 | 0.718 (0.548 to 0.848) / 0.726 / 0.697 / 0.778 | +0.294 (+0.089 to +0.555) |
| H two-station QSO: separate-track, offset 200 Hz † | 0.786 (0.770 to 0.799) / 0.789 / 0.778 / 0.886 | 0.806 (0.794 to 0.816) / 0.806 / 0.808 / 0.905 | +0.021 (+0.010 to +0.033) |
| H two-station QSO (per station): ambiguous, drawn offset | 0.992 (0.900 to 1.089) / 0.953 / 1.103 / 1.009 | 1.073 (0.753 to 1.395) / 1.052 / 1.131 / 2.802 | +0.087 (−0.195 to +0.400) |
| H two-station QSO (per station): ambiguous, offset 50 Hz | 0.947 (0.878 to 1.007) / 0.931 / 0.994 / 1.576 | 0.977 (0.847 to 1.115) / 0.975 / 0.983 / 1.871 | +0.028 (−0.109 to +0.183) |
| H two-station QSO (per station): same-track, drawn offset † | 0.973 (0.951 to 0.995) / 0.946 / 1.049 / 1.089 | 0.903 (0.869 to 0.936) / 0.909 / 0.889 / 1.061 | −0.065 (−0.101 to −0.035) |
| H two-station QSO (per station): same-track, offset 0 Hz † | 0.971 (0.902 to 1.036) / 0.940 / 1.062 / 0.765 | 0.852 (0.758 to 0.938) / 0.859 / 0.830 / 0.769 | −0.113 (−0.214 to −0.033) |
| H two-station QSO (per station): same-track, offset 10 Hz † | 0.960 (0.927 to 0.993) / 0.934 / 1.038 / 1.725 | 0.907 (0.828 to 0.973) / 0.903 / 0.919 / 1.639 | −0.050 (−0.109 to −0.003) |
| H two-station QSO (per station): same-track, offset 25 Hz † | 0.979 (0.912 to 1.071) / 0.949 / 1.063 / 1.400 | 0.878 (0.780 to 0.961) / 0.885 / 0.857 / 1.428 | −0.095 (−0.191 to −0.001) |
| H two-station QSO (per station): separate-track, drawn offset | 0.564 (0.548 to 0.582) / 0.575 / 0.533 / 0.715 | 0.618 (0.606 to 0.629) / 0.618 / 0.618 / 0.805 | +0.055 (+0.038 to +0.074) |
| H two-station QSO (per station): separate-track, offset 100 Hz | 0.673 (0.593 to 0.778) / 0.679 / 0.653 / 1.368 | 0.635 (0.581 to 0.693) / 0.643 / 0.610 / 0.755 | −0.033 (−0.165 to +0.061) |
| H two-station QSO (per station): separate-track, offset 200 Hz | 0.613 (0.603 to 0.623) / 0.619 / 0.597 / 0.725 | 0.646 (0.627 to 0.663) / 0.646 / 0.644 / 0.754 | +0.034 (+0.015 to +0.051) |
| H two-station QSO, oracle: ambiguous, offset 50 Hz (not meaningful (oracle anchor) for Matched) | 0.357 (0.029 to 0.973) / 0.351 / 0.373 / 0.078 | 0.479 (0.413 to 0.562) / 0.502 / 0.414 / 0.668 | +0.120 (−0.483 to +0.460) |
| H two-station QSO, oracle: same-track, offset 0 Hz | 0.322 (0.087 to 0.668) / 0.276 / 0.455 / 0.223 | 0.262 (0.088 to 0.459) / 0.239 / 0.328 / 0.282 | −0.056 (−0.521 to +0.256) |
| H two-station QSO, oracle: same-track, offset 10 Hz | 0.101 (0.036 to 0.176) / 0.066 / 0.205 / 0.217 | 0.142 (0.046 to 0.238) / 0.121 / 0.205 / 0.172 | +0.040 (+0.007 to +0.078) |
| H two-station QSO, oracle: same-track, offset 25 Hz (not meaningful (oracle anchor) for Matched) | 0.106 (0.033 to 0.211) / 0.064 / 0.222 / 0.335 | 0.490 (0.418 to 0.557) / 0.513 / 0.426 / 0.507 | +0.384 (+0.280 to +0.481) |
| H two-station QSO, oracle: separate-track, offset 100 Hz (not meaningful (oracle anchor) for Matched) | 0.336 (0.072 to 0.712) / 0.314 / 0.397 / 0.311 | 0.657 (0.539 to 0.799) / 0.662 / 0.641 / 0.698 | +0.321 (+0.033 to +0.506) |
| H two-station QSO, oracle: separate-track, offset 200 Hz † (not meaningful (oracle anchor) for Matched) | 0.468 (0.441 to 0.491) / 0.485 / 0.420 / 0.716 | 0.503 (0.479 to 0.532) / 0.497 / 0.518 / 0.588 | +0.035 (−0.006 to +0.067) |
| H two-station QSO, oracle (per station): ambiguous, offset 50 Hz † | 1.490 (1.011 to 2.189) / 1.472 / 1.541 / 1.894 | 1.001 (0.746 to 1.219) / 0.902 / 1.280 / 2.797 | −0.481 (−1.132 to +0.029) |
| H two-station QSO, oracle (per station): same-track, offset 0 Hz † | 1.461 (1.028 to 1.996) / 1.403 / 1.628 / 1.462 | 0.860 (0.747 to 0.981) / 0.885 / 0.788 / 1.374 | −0.608 (−1.132 to −0.220) |
| H two-station QSO, oracle (per station): same-track, offset 10 Hz † | 1.055 (0.971 to 1.151) / 0.999 / 1.221 / 2.561 | 0.965 (0.851 to 1.063) / 0.964 / 0.966 / 1.730 | −0.090 (−0.150 to −0.038) |
| H two-station QSO, oracle (per station): same-track, offset 25 Hz † | 1.115 (0.985 to 1.258) / 1.047 / 1.309 / 2.293 | 1.029 (0.839 to 1.206) / 1.039 / 1.001 / 2.233 | −0.084 (−0.266 to +0.068) |
| H two-station QSO, oracle (per station): separate-track, offset 100 Hz † | 1.449 (1.023 to 2.046) / 1.418 / 1.534 / 2.057 | 0.634 (0.455 to 0.822) / 0.563 / 0.833 / 1.387 | −0.819 (−1.407 to −0.366) |
| H two-station QSO, oracle (per station): separate-track, offset 200 Hz | 0.277 (0.178 to 0.384) / 0.232 / 0.402 / 1.223 | 0.081 (0.042 to 0.125) / 0.065 / 0.125 / 0.152 | −0.197 (−0.312 to −0.092) |
| strong: S500 30 dB | 0.034 (0.023 to 0.048) / 0.037 / 0.019 / 0.571 | 0.053 (0.045 to 0.060) / 0.056 / 0.038 / 0.857 | +0.019 (+0.011 to +0.024) |
| strong: S500 40 dB | 0.052 (0.047 to 0.056) / 0.050 / 0.056 / 0.867 | 0.055 (0.051 to 0.059) / 0.050 / 0.069 / 0.867 | +0.004 (+0.000 to +0.011) |
| strong: S500 50 dB | 0.028 (0.020 to 0.038) / 0.032 / 0.015 / 0.583 | 0.045 (0.041 to 0.049) / 0.054 / 0.015 / 1.000 | +0.018 (+0.011 to +0.023) |
| strong: S500 60 dB | 0.028 (0.023 to 0.037) / 0.036 / 0.000 / 0.583 | 0.048 (0.045 to 0.050) / 0.062 / 0.000 / 1.000 | +0.020 (+0.012 to +0.025) |
| pauses: pause 10 s | 0.144 (0.096 to 0.211) / 0.115 / 0.273 / 0.222 | 0.034 (0.032 to 0.036) / 0.042 / 0.000 / 0.333 | −0.114 (−0.179 to −0.062) |
| pauses: pause 2 s | 0.050 (0.023 to 0.092) / 0.048 / 0.061 / 0.167 | 0.038 (0.035 to 0.041) / 0.048 / 0.000 / 0.333 | −0.015 (−0.056 to +0.013) |
| pauses: pause 20 s | 0.644 (0.632 to 0.655) / 0.656 / 0.591 / 0.833 | 0.707 (0.699 to 0.714) / 0.702 / 0.727 / 1.000 | +0.064 (+0.046 to +0.080) |
| pauses: pause 5 s | 0.069 (0.035 to 0.112) / 0.067 / 0.076 / 0.222 | 0.039 (0.036 to 0.042) / 0.050 / 0.000 / 0.333 | −0.032 (−0.075 to +0.003) |
| tune-up: tune-up 0.3 s | 0.007 (0.000 to 0.014) / 0.009 / 0.000 / 0.125 | 0.010 (0.000 to 0.023) / 0.013 / 0.000 / 0.188 | +0.003 (+0.000 to +0.009) |
| tune-up: tune-up 0.6 s | 0.000 (0.000 to 0.000) / 0.000 / 0.000 / 0.000 | 0.003 (0.000 to 0.009) / 0.004 / 0.000 / 0.062 | +0.003 (+0.000 to +0.008) |
| tune-up: tune-up 1 s | 0.000 (0.000 to 0.000) / 0.000 / 0.000 / 0.000 | 0.542 (0.258 to 0.835) / 0.536 / 0.559 / 0.833 | +0.562 (+0.280 to +0.839) |
| tune-up: tune-up 2 s | 0.029 (0.006 to 0.067) / 0.018 / 0.068 / 0.000 | 0.909 (0.758 to 1.000) / 0.912 / 0.898 / 1.000 | +0.899 (+0.775 to +0.981) |
| first sample: from the first sample | 0.119 (0.097 to 0.147) / 0.111 / 0.145 / 0.833 | 0.138 (0.122 to 0.157) / 0.143 / 0.120 / 0.900 | +0.018 (−0.001 to +0.033) |
| crowded: spacing 0 Hz | 0.518 (0.409 to 0.620) / 0.531 / 0.469 / 1.370 | 0.253 (0.167 to 0.347) / 0.259 / 0.230 / 1.028 | −0.247 (−0.335 to −0.152) |
| crowded: spacing 100 Hz | 0.326 (0.223 to 0.430) / 0.342 / 0.271 / 0.876 | 0.048 (0.037 to 0.068) / 0.051 / 0.038 / 0.861 | −0.251 (−0.341 to −0.167) |
| crowded: spacing 200 Hz | 0.037 (0.030 to 0.047) / 0.039 / 0.030 / 0.749 | 0.041 (0.037 to 0.046) / 0.043 / 0.035 / 0.806 | +0.007 (−0.005 to +0.020) |
| crowded: spacing 50 Hz | 0.394 (0.286 to 0.503) / 0.414 / 0.327 / 1.141 | 0.126 (0.065 to 0.201) / 0.128 / 0.118 / 0.895 | −0.280 (−0.393 to −0.175) |
| band: band | 0.102 (0.041 to 0.177) / 0.107 / 0.084 / 0.808 | 0.051 (0.045 to 0.058) / 0.053 / 0.042 / 0.842 | −0.030 (−0.082 to +0.010) |

| Per over (groups G and H, from the "Per over" table) | Envelope: CER | Matched: CER |
|---|---|---|
| G ragchew, paddle (288 overs) | 0.197 | 0.082 |
| H two-station QSO, computer (120 overs) | 0.246 | 0.303 |
| H two-station QSO, hand (144 overs) | 0.306 | 0.534 |
| H two-station QSO, paddle (312 overs) | 0.218 | 0.316 |
| H two-station QSO, oracle, computer (68 overs) (not meaningful (oracle anchor) for Matched) | 0.305 | 0.370 |
| H two-station QSO, oracle, hand (68 overs) (not meaningful (oracle anchor) for Matched) | 0.296 | 0.648 |
| H two-station QSO, oracle, paddle (152 overs) (not meaningful (oracle anchor) for Matched) | 0.266 | 0.346 |

**Group H by regime** (detector run, grid and drawn offsets pooled by
`qso_regime`; the oracle copy in the second block; QSO-label CER scores
one label per QSO, station-label CER one label per station; the view that
does not fit the regime is marked †, as in section 11):

| Group H regime (tags) | Tracks per QSO, Envelope / Matched | QSO-label CER, Envelope / Matched | Station-label CER, Envelope / Matched |
|---|---|---|---|
| same-track (0, 10, 25 Hz; drawn), 45 QSOs | 1.00 / 1.00 | 0.118 (0.091 to 0.145) / 0.212 (0.156 to 0.271) | † 0.972 (0.951 to 0.992) / 0.893 (0.863 to 0.919) |
| ambiguous (50 Hz; drawn), 9 QSOs | 1.56 / 1.67 | 0.085 (0.051 to 0.119) / 0.334 (0.189 to 0.491) | 0.962 (0.908 to 1.015) / 1.009 (0.877 to 1.157) |
| separate-track (100, 200 Hz; drawn), 18 QSOs | 6.78 / 6.78 | † 0.646 (0.531 to 0.747) / 0.772 (0.710 to 0.815) | 0.616 (0.585 to 0.656) / 0.633 (0.613 to 0.655) |
| oracle copy, same-track (0, 10, 25 Hz), 18 QSOs | — | 0.176 (0.080 to 0.316) / 0.297 (0.194 to 0.392); Matched not meaningful at 25 Hz (oracle anchor) | † 1.210 (1.057 to 1.433) / 0.951 (0.869 to 1.035) |
| oracle copy, ambiguous (50 Hz), 6 QSOs | — | 0.357 (0.028 to 0.973) / 0.479 (0.412 to 0.562), Matched not meaningful (oracle anchor) | † 1.490 (0.991 to 2.182) / 1.001 (0.776 to 1.224) |
| oracle copy, separate-track (100, 200 Hz), 12 QSOs | — | 0.402 (0.242 to 0.583) / 0.579 (0.508 to 0.669), Matched not meaningful (oracle anchor); † at 200 Hz | 0.861 (0.561 to 1.226) / 0.357 (0.220 to 0.494); † at 100 Hz |

Task 14's Matched cells, in the same order: QSO label 0.197 (0.149 to
0.251), 0.297 (0.162 to 0.439), 0.745 (0.683 to 0.799), 0.283, 0.597,
0.505; station label 0.904 (0.875 to 0.928), 1.010 (0.872 to 1.180),
0.620 (0.597 to 0.636), 0.960, 1.073, 0.338; tracks per QSO unchanged.
Every Matched QSO-label cell through the detector rose (0.015–0.037) and
every interval overlaps its Task 14 interval: no change beyond the
intervals.

A separate-track QSO shows 6.78 tracks on both paths because each
station's track dies during the other's over and is re-born (section 11,
"Tracks per QSO"; backlog, "Tracks outlive their stations").

**First word of each over** (group H through the detector, grid offsets;
first-word CER is an upper bound, section 11; the answering station's
level is drawn from −6 to +6 dB re the caller's key-down power):

| Answering station's offset | Envelope: answer / caller first-word CER | Matched: answer / caller first-word CER | Matched: answer / caller over CER |
|---|---|---|---|
| 0 Hz (24 overs each) | 0.199 / 0.343 | 0.287 / 0.392 | 0.306 / 0.243 |
| 10 Hz | 0.213 / 0.287 | 0.110 / 0.352 | 0.207 / 0.094 |
| 25 Hz | 0.422 / 0.323 | 0.190 / 0.374 | 0.216 / 0.156 |
| 50 Hz | 0.032 / 0.204 | 0.379 / 0.398 | 0.143 / 0.279 |

Envelope's over CER for the same rows: answer 0.208, 0.124, 0.140, 0.044;
caller 0.104, 0.085, 0.076, 0.079. Task 14's Matched cells (answer /
caller first word; answer / caller over): 0 Hz 0.235 / 0.353, 0.279 /
0.231; 10 Hz 0.147 / 0.287, 0.172 / 0.099; 25 Hz 0.181 / 0.444, 0.245 /
0.196; 50 Hz 0.444 / 0.323, 0.345 / 0.086. These are ratios of pooled
counts over 24 overs from 6 QSOs, with no interval, so changes of this
size (−0.07 to +0.07 in first-word CER) are not shown to be beyond
chance. At 50 Hz the over CER moved from the answering station (0.345 →
0.143) to the caller (0.086 → 0.279), mostly in one split QSO
(`H-qso-s1`, QSO 6: answering station's overs 1.095 → 0.021, caller's
0.048 → 1.030 on the QSO label); not diagnosed.

| Group B against VE3NEA (no-space CER, his metric) | VE3NEA DeepCW | CW Skimmer (his measurement) | Envelope | Matched |
|---|---|---|---|---|
| Paddle, 24 WPM, f_D 0.1 Hz, S₅₀₀ 1.78 / 7.78 / 17.78 / 57.78 dB | 0.373 / 0.137 / 0.025 / 0.005 | 0.364 / 0.101 / 0.022 / 0.011 | 0.730 / 0.677 / 0.292 / 0.179 | 0.696 / 0.577 / 0.276 / 0.260 |
| HandKey, 24 WPM, f_D 0.1 Hz, S₅₀₀ 1.78 / 7.78 / 17.78 / 57.78 dB | 0.412 / 0.186 / 0.082 / 0.063 | 0.429 / 0.188 / 0.091 / 0.083 | 0.805 / 0.541 / 0.369 / 0.295 | 0.663 / 0.460 / 0.437 / 0.504 |

Pooled over 3 seeds, 9 stations per point: 1951, 1960, 1954 and 1932
reference characters per point (paddle) and 1927, 1964, 1952 and 1922
(hand key), the same for both decoders; VE3NEA's points hold 30 000.
Task 14's Matched cells: paddle 0.741 / 0.604 / 0.558 / 0.348, hand key
0.658 / 0.530 / 0.485 / 0.519 (no interval; pooled ratios).
Remaining differences from his benchmark: bin-centered oracle channels
instead of his ±30 Hz pitch error, complex I/Q noise instead of real
audio, and one draw per character and word space (he sums several); the
keying edges match his (2 ms, centered). Both of our decoders are far
from his at every point, and the gap does not close at high S₅₀₀: at
57.78 dB, where a fade to 30 dB below the mean power (probability 10⁻³
for Rayleigh fading, derived) still leaves S₅₀₀ = 28 dB, Envelope reads
0.179 and 0.295 and Matched 0.260 and 0.504. So the residual is not
noise; that it is the hard-decision timing (speed estimate and fixed
gap thresholds, section 8) under changing level is an inference, not
measured.

| | Envelope | Matched |
|---|---|---|
| CPU per channel-second (process, over the bench's timed window; section 11), ms/s | 0.202 (Task 14) | 0.381 (Task 14) |
| Decoders per channel-second, ms/s | 0.014 (Task 14) | 0.177 (Task 14) |
| Median frequency error, group F offsets, Hz | 5.9 (bin rounding; no re-centering) | 0.11 at S₅₀₀ = 5 dB, 0.13 at 0 dB (30 signals each) |

CPU is over 384 495 channel-seconds (Envelope) and 384 661 (Matched), on
the machine above; the Matched decoder adds 0.163 ms of decoder time per
channel-second, 11.6 times the Envelope decoder's time (the Matched decoders'
total, 0.177 ms, is 12.6 times it), and nearly doubles the
whole process: outside the decoders (WAV reading, the shared FFTs, the
detector in the detector runs only, the bench's event subscriber;
section 11, "CPU time per channel-second") Envelope's process spends
0.202 − 0.014 = 0.188 ms/s. Most channel-seconds (340 465) come from
oracle runs, which skip the detector (section 11 splits the figures).
**Task 17 (after Tasks 15 and 16), two runs on the same machine, same
session:** process 0.235 and 0.244 ms/s (Envelope), 0.444 and 0.472
ms/s (Matched); decoders 0.016 and 0.017 ms/s (Envelope), 0.201 and
0.218 ms/s (Matched); `run` took 6.0 and 6.2 min (the second overlapped
a few seconds of short diagnostic runs). Envelope's figures are 16–21%
above Task 14's although its code path and its results are
identical, so the machine's state differed between the sessions
(inferred; not controlled), a spread larger than any effect of the
fixes. Within each session the ratios hold: Matched's process 1.89,
1.89 and 1.93 times Envelope's, its decoders 12.6, 12.4 and 13.0 times
(Task 14, Task 17 runs 1 and 2), so the fixes add no decoder cost this
measurement can resolve.

**Reading the results.**

- **Group A (sensitivity, oracle).** Matched crosses CER 0.10 at S₅₀₀ =
  −0.2, 1.1 and 2.9 dB (12, 25, 40 WPM) against Envelope's 7.2, 5.1 and
  6.0 dB: 3.1–7.4 dB better by the point estimates, with the two
  intervals disjoint at every speed, and the paired differences favor Matched
  at all three speeds (−0.575, −0.238, −0.087, intervals excluding 0).
  (Task 14: 0.2, 2.7 and 3.1 dB; at 25 WPM the Matched interval was
  −0.0 to 10.4 dB and overlapped Envelope's.) Envelope's crossings sit
  near its squelch's estimated +6 dB (section 8, step 5), as expected.
  Matched's are **above** the design's expectation of about −2.6 to 0 dB
  (acquisition floor −2.5 dB derived; "Squelch" above) at 25 and 40 WPM
  (1.1 dB, 0.4 to 1.4 dB; 2.9 dB, 2.2 to 3.3 dB); at 12 WPM (−0.2 dB,
  −0.5 to 0.7 dB) the interval overlaps that range. Per point, the
  Matched CER at 25 WPM is 0.670 at −2 dB, 0.185 at 0 dB, 0.024 at 2 dB
  and 0.003 at 4 dB (Task 14: 0.711, 0.122, 0.151, 0.006), so the
  crossing now lies between 0 and 2 dB; 8 of 12 stations exceed CER 0.10
  at 0 dB and none at 2 dB (Task 14: 6 and 2; 40 WPM: 12 and 7 of 12,
  unchanged). The single stations that failed at high S₅₀₀ in Task 14
  (25 WPM, 10 dB, CER 0.464; 40 WPM, 6, 10 and 12 dB, 0.155, 0.265,
  0.236), the start-up runaways of "Limits measured" below, now read
  0.000–0.020. The wide CER-0.05 interval at 12 WPM (−0.1 to 18.1 dB)
  comes from the new 12 WPM start-up loss (section 8, "Marks whose start was
  not observed"): the first characters read as T's, CER 0.021–0.039 per
  point at 6–20 dB against 0.011–0.021 in Task 14, and 1 of 12 stations
  above CER 0.10 at 4 and at 12 dB. No run or scoring fault was found:
  every result file names the decoder that made it, and the rebuilt
  bench reproduces the stored results exactly (Task 14's Matched results
  re-scored at 74182aa, whose engine differs from c9a2aa4 only in
  comments, are identical in all 126 files; Task 17).
- **Group F (tuning, oracle).** Matched's CER does not depend on the
  offset from the bin center beyond the intervals (20 WPM: 0.049, 0.047,
  0.028, 0.010, 0.043 at 0, 2.9, 5.9, 8.8, 11.7 Hz, every interval
  overlapping; 25 WPM: 0.194, 0.208, 0.091, 0.178, 0.113, likewise; Task
  14: 0.124, 0.041, 0.030, 0.184, 0.049 and 0.145, 0.089, 0.203, 0.167,
  0.309);
  Envelope's is 0.39–0.49 at every offset. The frequency error is within
  the ±2 Hz target for every signal (section 7). The drift rows show the
  **oracle-mode limit of option 1**: with no detector the anchor stays on
  the labeled starting frequency and the tracker covers only ±12 Hz, so a
  drift of 1 or 2 Hz/s over the 17.5–27 s signals (19–51 Hz) loses the
  decode (Matched CER 0.232 and 0.532 against Envelope's 0.080 and 0.113;
  Task 14: 0.486 and 0.777; "not meaningful (oracle anchor)"); at
  0.5 Hz/s (up to 13.5 Hz) Matched still read 0.022. Through the
  detector the anchor follows the drift (Task 13's
  `MatchedReportsDriftingFrequency`); no suite group measures that yet.
- **Groups G and H (QSOs).** Ragchew (G, 25 WPM, oracle) crosses
  CER 0.10 at 6.9 dB (6.6 to 7.1 dB, Envelope) and 3.1 dB (2.8 to 3.2 dB,
  Matched; Task 14: 3.8 dB, 3.4 to 5.3 dB), 1.8 and 2.0 dB above group A
  at 25 WPM: the QSO text (prosigns, abbreviations, pauses between overs)
  costs about 2 dB on both. At CER 0.05 Envelope crosses at 7.6 dB (7.5
  to 7.7 dB) and Matched at 3.8 dB (no interval: fewer than 95% of the
  resamples reached 0.05; Task 14: 9.2 dB, where the order was
  reversed); Matched still keeps a residual CER of 0.012–0.018 more than
  Envelope at 8–20 dB (below; Task 14: 0.013–0.033). Two-station QSOs
  through the detector: same-track QSOs stay one track on both paths
  (1.00), and Matched is **worse** on the QSO label (0.212 against
  0.118; paired +0.104, +0.053 to +0.172, drawn offsets; Task 14: 0.197,
  +0.078). The answering station's first word, the retune delay's
  measure, is better with Matched at 10 and 25 Hz (0.110 and 0.190
  against 0.213 and 0.422) and worse at 0 Hz (0.287 against 0.199); the
  option-1 simulation (Design decisions B) predicted a median of 0–1
  character lost at 10–25 Hz for answering stations at −6 to +6 dB re
  the caller, consistent with these rates for 3–5 character first words
  (a first-word CER of 0.15–0.25 is one character in 4–7; an upper
  bound). At 50 Hz (ambiguous on both paths; on the Matched path it is
  3 Hz beyond D_ch = 47 Hz, inside the band where interpolated
  frequencies can read closer than D_ch; section 11, "QSO regimes")
  Matched reads the QSO label much worse (answering station's first word
  0.379, over 0.143, caller's over 0.279, against Envelope's 0.032, 0.044
  and 0.079): per QSO label the Matched CER was 0.557, 0.234, 0.046,
  0.374, 0.025 and 0.043 (Task 14: 0.533, 0.154, 0.046, 0.415, 0.032,
  0.040; Envelope 0.023–0.140). The two worst are exactly the QSOs that
  **split into two tracks** on the Matched path, the same two as in
  Task 14 (the other 4 stayed one track, the caller's track following
  the answering station; tracks within 25 Hz of either carrier in
  `build/suite/full3/results/matched/H-qso-s*.json`). In Task 14 the
  caller's track stayed on the caller in both (last frequency 0.9 and
  1.0 Hz from it) and the answering station's overs read CER 0.936 on
  it (first word 0.636), because the Matched channel is re-centered on
  the caller behind the dit-matched filter, whose main lobe is ±r/K =
  ±21–33 Hz at 20–32 WPM (derived); now, in one of them (`H-qso-s1`,
  QSO 6), the answering station's overs read 0.021 and the caller's
  1.030 (not diagnosed), in the other (`H-qso-s2`, QSO 7) 0.673 and
  0.114. The Envelope caller channel (±150 Hz, no re-centering) decoded
  the answering station in its own 3 split QSOs (QSO-label CER 0.023,
  0.140, 0.028). So the QSO-label loss at 50 Hz is the split, a QSO read
  through the view that does not fit it; the per-station view, which
  fits a split QSO, reads poorly at 50 Hz on both paths (0.947 Envelope,
  0.977 Matched, all 12 station labels), so these runs do not show how
  well Matched decodes the answering station on its own track. With
  the growth bound now applied per mark (Task 15) the two split QSOs
  still read worst, so a dit-estimate runaway, which the design
  simulation's 12 of 30 failures before the first-step bound came from,
  is not what fails them here (inferred). Separate-track QSOs: Matched
  is worse per station (paired +0.055, +0.038 to +0.074, drawn; +0.034,
  +0.015 to +0.051, 200 Hz). In the oracle copy, which shows the
  turnover apart from detection, the rows within ±12 Hz of the label
  show no difference beyond the interval at 0 Hz (paired −0.056) and a
  small one at 10 Hz (+0.040, +0.007 to +0.078; Task 14 +0.026, −0.004
  to +0.078), and per station Matched is better wherever the view fits
  (200 Hz: 0.081 against 0.277).
- **Strong signals, pauses, tune-up, first sample (detector).** Strong
  (S₅₀₀ 30–60 dB): Matched is slightly worse at every level (paired
  +0.004 to +0.020, intervals excluding 0 at 30, 50 and 60 dB; CER
  0.045–0.055 against 0.028–0.052), with first-word CER 0.857–1.000
  against 0.571–0.867. Pauses: Matched is better after 10 s (0.034
  against 0.144) and equal within the intervals at 2 and 5 s; after 20 s
  both lose the station's track (0.707 and 0.644; paired +0.064, +0.046
  to +0.080). First sample: no difference beyond the interval (+0.018,
  −0.001 to +0.033). Strong, pauses and first sample are unchanged from
  Task 14 to three decimals. **Tune-up carriers of 1 s and 2 s break
  Matched** (CER 0.542 and 0.909 against 0.000 and 0.029, unchanged;
  0.3 and 0.6 s carriers are harmless, 0.010 and 0.003, Task 14 0.014
  and 0.032); see "Limits measured".
- **Crowded and band (detector).** Matched is better at 0, 50 and 100 Hz
  spacing (paired −0.247, −0.280, −0.251) and not different beyond the
  interval at 200 Hz (+0.007, −0.005 to +0.020; Task 14 +0.029) or on
  the band (−0.030).

**Where Matched is worse than Envelope** (paired Matched − Envelope CER
whose 95% interval excludes 0, or a crossing worse beyond its interval;
the default stays Matched, owner decision 2026-09-29; no parameter was
changed; Task 14's value after "was"):

- Per condition (summary.md), **still worse**: C fists, bug imbalance
  +0.1 (+0.349, +0.192 to +0.467; was +0.317); E, 20 Hz interferer at
  +10 dB re the wanted station's key-down power (+0.221, +0.088 to
  +0.352; was +0.349); H through the detector: same-track drawn (+0.104,
  +0.053 to +0.172; was +0.078), ambiguous drawn (+0.442, +0.368 to
  +0.507; was +0.352), separate-track per station (drawn +0.055, 200 Hz
  +0.034; was +0.057 and +0.029); strong at 30, 50, 60 dB (+0.018 to
  +0.020, unchanged); pauses 20 s (+0.064, unchanged); tune-up 1 s
  (+0.562) and 2 s (+0.899), unchanged. C fists, paddle imbalance +0.1,
  still crosses CER 0.10 for Envelope at 8.4 dB (5.0 to 9.5 dB) and not
  at all for Matched. **Newly worse**: C fists, bug imbalance +0.0
  (+0.239, +0.062 to +0.389; was +0.117, −0.043 to +0.269) and hand
  imbalance +0.1 (+0.157, +0.070 to +0.242; was +0.069, −0.017 to
  +0.156) (Matched CER 0.505 → 0.570 with Task 15 alone → 0.622 with
  Task 16 added, and 0.424 → 0.550 → 0.510: mostly Task 15); D 10 WPM
  (+0.061, +0.027 to +0.121; was +0.021, lower bound +0.000), from
  Task 16 (the 12 WPM start-up loss, section 8); H same-track at 10 Hz
  (+0.043, +0.006 to +0.081; was +0.028, lower bound −0.000) and its
  oracle copy (+0.040, +0.007 to +0.078; was
  +0.026). **No longer worse**: G ragchew at CER 0.05 (Matched 3.8 dB,
  no interval, against Envelope's 7.6 dB, 7.5 to 7.7 dB; was 9.2 dB) and
  C fists, machine imbalance +0.1 at CER 0.10 (Matched ≤ 5.0 dB, as
  Envelope; was 8.4 dB, 5.0 to 9.2 dB). Borderline (interval's lower
  bound rounds to +0.000): strong 40 dB (+0.004), tune-up 0.3 s (+0.003)
  and 0.6 s (+0.003). (F drift at 1 and 2 Hz/s and the oracle H rows
  beyond ±12 Hz are worse too, but not meaningful: oracle anchor.)
- **A Matched row worse than in Task 14 beyond its interval**: D speed
  step 20 → 35 WPM, CER 0.113 (0.058 to 0.163) → 0.394 (0.222 to 0.499),
  still not worse than Envelope (paired −0.123, −0.434 to +0.165; was
  −0.412). It comes from Task 15 alone (0.394 with Task 15 and without
  Task 16): in 5 of 6 signals the text stops within a few characters
  after the step; not diagnosed. The only row better beyond its interval
  is F drift 2 Hz/s (0.777 → 0.532, not meaningful: oracle anchor).
- Per S₅₀₀ point (paired over the stations of a point: 12 in group A,
  9 in groups B and C, 6 in group G): 30 of 209 points in groups A, B,
  C and G favor Envelope and 76 favor Matched (was 31 and 73); at 95%
  about 10 of 209 would exclude 0 by chance, so single points are weak
  evidence, a run of them is not. **Group B at high S₅₀₀ with slow
  fading and random timing**, fewer than before (9 points, was 18):
  paddle 12 WPM f_D 0.1 Hz at 27.78, 37.78 and 57.78 dB (+0.222, +0.144,
  +0.175), paddle 24 WPM f_D 0.1 Hz at 57.78 dB (+0.080), hand 24 WPM
  f_D 0.1 Hz at 13.78 and 57.78 dB (+0.073, +0.171), the VE3NEA mix at
  17.78 dB (+0.098), and two low points, hand f_D 1 Hz at 1.78 dB
  (+0.049) and f_D 3 Hz at 4.78 dB (+0.058); no longer paddle 24 WPM
  f_D 0.1 Hz at 1.78, 17.78 and 37.78 dB, paddle 12 WPM at 17.78 dB,
  paddle or hand f_D 0.3 Hz at any point, or hand f_D 0.1 Hz at
  17.78 dB. **Group C at 5–20 dB with bug or hand keying or positive
  imbalance**, more than before (11 points, was 8): bug +0.0 at 5 and
  20 dB (+0.219, +0.313), bug +0.1 at 5, 10 and 20 dB (+0.284, +0.355,
  +0.408), bug −0.1 at 20 dB (+0.141), hand +0.0 at 20 dB (+0.134),
  hand +0.1 at 10 and 20 dB (+0.168, +0.268), paddle +0.1 at 10 and
  20 dB (+0.032, +0.051). **Group A at 12 WPM, 8–18 dB** (5 points, new,
  +0.004 to +0.022: the 12 WPM start-up loss) and 40 WPM at −2 dB
  (+0.115, new). **Group G at 8–20 dB**, small (+0.012 to +0.018; was
  +0.013 to +0.033). The pattern is unchanged: Matched's advantage is
  at low S₅₀₀; at high S₅₀₀ with random keying timing it loses a few to
  41 CER points. Its marks are about 7 ms longer at 25 WPM (ŝ's ramp
  bias, "Amplitude estimate" above) and a positive imbalance lengthens
  them further, shortening the element spaces the character decisions
  rest on; that this causes the loss is a hypothesis, not measured. The
  fading loss at high S₅₀₀ is not diagnosed.

**Limits measured** (diagnosed by focused runs and debug prints that are
not in the repository; nothing was changed):

- **A channel that opens mid-transmission.** The speed-estimate part is
  **fixed by Task 16** (section 8, "Marks whose start was not observed"):
  a fragment timed from a mark in progress when the warm-up ends is
  decoded (as a dit) but no longer enters the speed estimate. Every cut
  of the test `MatchedChannelOpeningInsideADahCountsNoFragment` (warm-up
  ending −10 to +40 ms before the end of U's dah, 1 ms steps; 7 of them
  time a 14.7–20.7 ms fragment) decodes every word after the first
  exactly (measured, Task 16).
  **Task 14's diagnosis** (the record; code before Tasks 15 and 16): on
  the smoke recording (`bench/smoke.sh`) Matched read CER 0.0622 against
  Envelope's 0.0353 and 0.0145 with oracle channels (Matched). One
  station, +7617.6 Hz, 18.5 WPM (dit 64.9 ms), S₅₀₀ 25.1 dB, keying from
  1.076 s, decoded at CER 0.234 through the detector: its channel opens
  at about 1.6 s, and its text started "E ETT TT 7 L T U U A 7L" for
  "UA7L TU UA7L" and then recovered. Cutting the recording at 1.600 s
  and decoding that station with an oracle channel (which opens at the
  cut) reproduced the same text exactly (11 edits in 47 symbols), so the
  detector is not involved; cut at 1.620 s the Matched decode lost only
  the first word, sent partly before the cut (3 edits). Mechanism (from
  the code, confirmed with debug prints): the Matched decoder's warm-up
  (0.32 s, "Warm-up" above) keys nothing; cut at 1.600 s it ended 15 ms
  before the end of the dah of U (1.725–1.919 s, plus about 18 ms of
  filter delay), so the first mark the decoder timed was a 15.3 ms
  fragment of it, longer than the 14.4 ms glitch limit (0.3 × the 48 ms
  initial dit at 25 WPM); cut at 1.620 s the warm-up ended just after
  that dah and no fragment was timed. With marks {15.3, 60.7 ms} the
  speed estimate (section 8, step 10) splits at the ratio 3.97, the
  fragment is the whole "dit" cluster, the dah/dit ratio exceeds 3.85,
  and the averaged estimate, 17.8 ms, is clamped to 20 ms (60 WPM). With
  a 20 ms dit, the station's 65 ms dits read as dahs, its element spaces
  end characters and its 195 ms character spaces read as word spaces.
  The fragment stayed the only member of the dit cluster for the next 24
  marks: the estimate rose through 29.8, 41.5 and 35–37 ms and the text
  recovered once the fragment left the window.
  **After Tasks 15 and 16 (Task 17, measured):** the smoke recording
  reads Matched CER 0.0436 (42 edits in 964 symbols; was 0.0622, 60
  edits; Envelope unchanged at 0.0353, 34 edits), the +7617.6 Hz station
  through the detector 0.043 (was 0.234); cut at 1.600 s the station
  reads "E UA7L TU UA7L …", 2 edits in 47 (was 11), and cut at 1.620 s
  3 edits (unchanged). How general it is (focused run: 4 stations per
  speed, machine keying, 30 s at 48 000 samples/s, noise seeds 700–703,
  each recording cut at 20 random times in 2–6 s, so 80 oracle channels
  per speed open at random phases of the keying; openings whose Matched
  decode had at least 5 more edits than Envelope's, and at least 5
  fewer; "before" is Task 14's code, 74182aa, which reproduced Task 14's
  counts exactly; one run each, so no interval):

  | Speed | S₅₀₀ 25 dB, Matched worse / better: before → after | S₅₀₀ 10 dB, Matched worse / better: before → after |
  |---|---|---|
  | 12 WPM | 5 / 0 → 6 / 0 of 80 | 12 / 0 → 15 / 0 of 80 |
  | 18.5 WPM | 6 / 0 → 0 / 0 of 80 | 3 / 1 → 0 / 1 of 80 |
  | 25 WPM | 0 / 0 → 0 / 0 of 80 | 2 / 0 → 1 / 0 of 80 |
  | 40 WPM | 0 / 16 → 0 / 16 of 80 | 1 / 78 → 0 / 78 of 80 |

  Matched edits over the 80 openings, before → after (Envelope; symbols):
  at 25 dB 889 → 947 (762; 2800) at 12 WPM, 644 → 549 (522; 2800) at
  18.5 WPM, 664 → 663 (692; 2800) at 25 WPM, 1273 → 1277 (1421; 5680) at
  40 WPM; at 10 dB 862 → 803 (633), 573 → 524 (495), 826 → 805 (862),
  1235 → 1196 (3016). **Every opening that lost the rest of its text
  before** (23–70 edits: the dit stuck low or a runaway, the "DE K****V"
  case among them) **now decodes after its first word**, with one
  exception that is unchanged (25 WPM, S₅₀₀ 10 dB, one channel with no
  text at all, 35 edits in 35, Envelope 15; not diagnosed). **What
  remains, and its shape:** (a) the first word, sent partly before the
  opening, is lost or shortened on both paths (replay, backlog); at
  12 WPM Matched loses up to two words more than Envelope in some
  openings ("TSC K1ABC UR 5NN" for "CQ TEST DE K1ABC K1ABC UR 5NN",
  16–18 edits against
  Envelope's 6–7; unchanged by the fixes); (b) new, at 12 WPM: the first
  characters after the opening read as a string of T's ("T T T TTT T DE
  K1ABC" for "CQ TEST DE K1ABC", 11–15 edits against Envelope's 6–10),
  the 12 WPM start-up loss of section 8 (cause inferred, not
  instrumented); it accounts for the rise in the 12 WPM worse counts
  above, each such opening 3–7 edits worse than before, and it is over
  within the first two words. In the suite's detector-path groups (band,
  crowded, strong, pauses, first sample: every channel opens
  mid-transmission) Matched is worse than Envelope by more than 0.1 CER
  for 1 of 36 stations at 10–15 WPM, 3 of 35 at 15–20, 2 of 144 at
  20–30, 1 of 114 at 30–45 and 0 of 91 at 45–60 WPM (was 3, 2, 4, 1,
  3), and better by more than 0.1 for 8, 3, 25, 20 and 30 (was 8, 4, 25,
  20, 30). Envelope did not fail on these openings: its warm-up (one
  dit, 48 ms) keeps its squelch closed through a mark in progress unless
  that mark filled less than a third of the warm-up (f < 1/3, derived in
  section 8, "Marks whose start was not observed"). Replay ("Wrong or
  missing first characters", backlog) would remove the late opening
  itself.
- **Start-up runaway.** Three runs in Task 14 showed the same chain:
  (1) the first mark or two were misleading (a fragment, a first mark
  keyed late while ŝ rises from the warm-up, or merged elements at low
  S₅₀₀ through the 16 ms acquisition filter); (2) at the 8th mark the
  filter started following, and the widening cascade (section 8, "Two
  decoders") stretched the mark just ended; (3) with one mark of
  intermediate length in the window, no neighbor ratio reached 1.8, the
  estimator took the mean of dits and dahs as the dit (section 8, step
  10), about twice the true dit, and the bound, then applied ×1.25 per
  update (the defect), let it grow there within one mark. Cases: A,
  25 WPM (dit 48 ms), S₅₀₀ 10 dB, +6606.5 Hz in `A-awgn-25wpm-1-s1`:
  first mark 100.7 ms (a 144 ms dah keyed late), estimate correct
  (45.7 ms) for 8 marks, then the cascade stretched a dit to 62 ms and
  the estimate went 58.3 → 72.9 → 91.1 → 93.8 ms within 23 ms; CER
  0.464, recovering later. A, 25 WPM, S₅₀₀ 2 dB, −2991.9 Hz in
  `A-awgn-25wpm-0-s2`: merged elements gave an estimate of 105–120 ms
  before following began, the cascade widened the filter from 20 to
  119 ms within one mark, the 48 ms element spaces filled in, marks grew
  to 287–503 ms, the estimate to 210 ms, and decoding stopped (CER 0.981;
  the bound slowed the runaway, it did not stop it). The 18.5 WPM
  late-opening case above that read "DE K****V" followed the same chain
  from a 113.3 ms fragment. **After Tasks 15 and 16 (Task 17,
  measured):** Matched CER 0.464 → 0.000 (`A-awgn-25wpm-1-s1`) and 0.981
  → 0.015 (`A-awgn-25wpm-0-s2`), and "DE K****V" now reads "DE K1ABC
  K1ABC UR 5NN 14 TU" (8 edits, as Envelope). Which steps remain:
  Task 15 removes step (2)'s cascade (a mark can still be re-opened by
  the widened filter, and timed up to 0.1 of the old dit longer, derived
  in section 8) and step (3)'s growth within one mark (the dit estimate
  now needs ln 2/ln 1.25 = 3.1 marks to double, derived); step (3)'s
  estimator branch, the mean of both clusters taken as the dit, is not
  addressed. Of step (1)'s causes, Task 16 keeps a fragment and a first
  mark keyed late while ŝ rises (after the warm-up, an opening in noise
  or a re-acquisition) out of the speed estimate; merged elements at low
  S₅₀₀ are not addressed. The −2991.9 Hz case began with merged elements
  and now decodes (0.015), so bounding the growth per mark was enough
  there; whether merged elements still start runaways elsewhere is not
  shown by these runs. No group A station at S₅₀₀ ≥ 6 dB reads above CER
  0.110 any more (Task 14: 0.155–0.464 for the four runaway stations).
- **Tune-up carriers of 1 s or more** (unchanged by Tasks 15 and 16,
  re-measured in Task 17). Cutting `tune-up-s1` so that an oracle
  channel opens during a 2 s carrier (at 1.2 or 2.0 s) or a 1 s carrier
  (at 1.6 s) gives no Matched text at all (CER 1.000, before and after);
  opening in the 0.5 s silence after the carrier gives CER 0.000 (2 s
  carrier, cut at 2.9 s) and 0.021 (1 s carrier, cut at 2.4 s);
  Envelope decodes every cut (CER 0.000–0.103; 0.051 and 0.000 at these
  cuts). With the channel open from the start of the recording, the 1 s
  carrier does little harm (0.064) but the 2 s carrier still does
  (0.974), both as in Task 14; the suite rows are identical to Task 14's
  (CER 0.542 and 0.909). Through the detector the channel opens at least
  0.5 s after the station appears (section 6, persistence), so it likely
  opens during a 1–2 s carrier. Likely mechanism (from the code, not
  instrumented; neither fix touches the noise estimate, so no change was
  expected, and the mechanism is still not instrumented): a warm-up
  (0.32 s) that sees only carrier sets σ̂_v² from the carrier's own
  power, and a carrier longer than the noise floor's window (64 samples
  K apart, 1.02 s at K = 24) lifts the floor to the carrier's level;
  with σ̂_v² near the station's power, a = ŝ/σ̂_v stays below the
  squelch, and the station's own marks (|v|²/(2σ̂_v²) below κ = 1.75)
  pass the noise guard and hold σ̂_v up. Envelope's speed estimate
  excludes marks longer than 0.96 s (section 8, step 10) and its levels
  are set by one dit.

### 8.4 From section 11: CPU time per channel-second (measured) and the smoke check's history

CPU time per channel-second, measured:

  only steady-clock time inside decoders (the engine runs on one thread).
  Measured (full suite, 3 seeds, `build/suite/full3`, each recording's
  results file; section 8b's machine): over all recordings 0.202 ms/s
  (Envelope) and 0.381 ms/s (Matched); in the oracle runs (340 465
  channel-seconds) 0.163 and 0.324 ms/s, in the detector runs (44 031 and
  44 196 channel-seconds) 0.501 and 0.816 ms/s; decoders alone 0.014 and
  0.174 ms/s (oracle), 0.015 and 0.196 ms/s (detector) (Task 14). Task
  17's two runs after Tasks 15 and 16 read 16–24% higher on both paths,
  Envelope's code unchanged, so these figures vary by about a fifth
  between sessions on this machine (section 8b). A Raspberry Pi 5 is not
  yet measured.

Smoke check, as it stood:

- **Smoke check** (`bench/smoke.sh`; CI runs it on Windows and Linux):
  generates the `smoke` recording (band scenario, 8 stations, 30 s,
  192 kHz, seed 1) and scores it twice on each path. The Envelope path
  (`--front-end envelope`) must meet `bench/baselines/smoke.json` (CER
  ≤ 0.09, detection recall ≥ 0.875) and the Matched path
  `bench/baselines/smoke-matched.json` (CER ≤ 0.07, recall ≥ 0.875); the
  two runs of each path must write byte-identical results (run-to-run
  determinism on one platform). **CI therefore bounds the Envelope CER; it
  does not pin bit-identity with milestone 1.** What establishes that the
  Envelope path is unchanged: the generator's frozen-copy tests
  (`test_default_signals_match_milestone_1_generator`,
  `test_band_defaults_match_milestone_1` in `training/tests/test_generate.py`:
  the band scenario and the generator's default output match frozen copies
  of milestone 1's code), reading the
  engine's Envelope path (final branch review, 2026-09-30), and its smoke
  CER, 34 edits in 964 symbols (0.0353, measured), the same as at Task 2,
  before any engine change. The Matched limit 0.07 (**heuristic**) is the
  measured 0.0622 = 60/964 (Windows) plus a margin of 3/482 = 6/964
  (0.0685), rounded up to two decimals: the check fails from 68 edits, a
  margin of 7 edits over the measured 60 (Envelope's 0.09 fails from 87
  edits, a margin of 52 over its 34). The Matched figure was dominated
  by one station's start-up (CER 0.234; section 8b, "A channel that
  opens mid-transmission"), which a platform difference that moves the
  channel's opening by one hop could change by tens of edits. **After
  Tasks 15 and 16** the Matched smoke CER is 0.0436 = 42/964 on Windows
  (measured, Task 17; that station 0.043). The limit is never widened,
  and is tightened only when the Matched CER is below 0.0622 on both CI
  platforms, Windows and Linux, to the higher of the two plus 6/964,
  rounded up to two decimals (on Windows alone that would be
  48/964 = 0.0498, rounded up to 0.05). The Linux value has not been
  measured yet (the branch has not run on CI), so the limit stays 0.07
  and `bench/baselines/smoke-matched.json` is unchanged.

### 8.5 From section 11, "QSO regimes": how the full suite's QSOs fell into tracks on the Matched path

    could go farther, which the code allows and no run has shown). Measured (full
    suite, 3 seeds; `build/suite/full3/results/matched/H-qso-s*.json` and
    `H-qso-drawn-s*.json`, tracks within 25 Hz of either carrier): at
    50 Hz, 4 of 6 grid QSOs stayed one track, the caller's track ending on
    the answering station (its last frequency 49.8–50.0 Hz from the
    caller), and 2 split into two tracks; of the drawn QSOs at 53.9, 57.9
    and 70.2 Hz, the one at 53.9 Hz ended with the caller's track on the
    answering station (that station also had a track born at its own
    carrier; whether both were alive at once is not recorded) and the
    other two stayed apart. Every QSO below 47 Hz (up to 42.8 Hz)
    stayed one track, and every one from 94.6 Hz up split into at least
    two.
