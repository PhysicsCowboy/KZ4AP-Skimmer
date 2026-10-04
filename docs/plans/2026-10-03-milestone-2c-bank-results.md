# Milestone 2c, Plan A: Results Record

Plan: `docs/plans/2026-10-03-milestone-2c-bank-port.md`. Reference behavior: the stage-1 prototype
(`training/kz4ap_proto`) with its settled `ProtoConfig()` defaults (stage-1 results record
`docs/plans/2026-09-30-milestone-2b-stage-1-results.md`, section 3.13). Every number here is
**measured** unless marked otherwise. CER is the character error rate (edits per reference symbol, a
fraction); intervals are bootstrap 95% intervals over signals.

**Answer (Task 9).** The C++ bank decoder (the port) reproduces the prototype on the whole development
set: **all 525 channels decode to the identical final text**, with identical characters (text and times
to 0.1 ms as written), corrections, selections, over starts and switches. There is therefore no text
difference to trace (owner decision D2's system-level criterion is met with nothing to explain). The only
differences anywhere in the decoded records are 317 of 875 070 per-window periodicity estimates (0.036%),
all in windows far below the confidence threshold, which change nothing downstream; three of them traced
here, and the two golden ones, are rounding-level near-ties. The port costs **128.2 ms of CPU per
channel-second** on the Linux machine, 0.70 times the prototype's 184.0 ms on the same channels and
machine, and about twice the spike's projection of 65 ms.

**Answer (Task 10).** Through the engine (`--decoder bank`), on the full suite, all three seeds, the bank
decoder scores the same as the stage-1 prototype everywhere except the drifting signals of group F: 272 of
the 280 per-regime paired comparisons (140 regimes, against Matched and against Envelope) are the same numbers
as stage 1's, and the 8 that differ are the four F drift rows. Pooled over the oracle test cases its CER is
0.369, against Matched's 0.457 and Envelope's 0.625; through the detector path 0.448, against 0.467 and 0.482
(section 4.2). The engine's text differs from the prototype's on 21 oracle signals: 15 drifting labels, because
the engine mixes at the label's frequency without its drift (expected; the frequency tracker is Plan B's), and
**6 others, because of a defect in how the engine reports corrections** (a correction's `from_index` assumes
the kept characters are a prefix of the list, which fails when characters overlap in time; section 4.4). That
defect changes no score in this suite (the 6 signals have the same edit counts either way), but the engine's
text there is not the bank's. The text as first displayed has CER 0.411 against 0.382 after corrections
(section 4.5). The bank costs 134.1 ms of CPU per channel-second through the engine, against 0.61 ms for
Matched and 0.34 ms for Envelope (section 4.6). Its detection measures equal Matched's except the strong
signals' false tracks (1 against 12; section 4.7).

## 0. Terms used in this record

- **Decoder**: Envelope, Matched and the bank are the three decoders. Here "the prototype" is the bank
  decoder in Python (`bank-proto`), "the port" the bank decoder in C++ (`bank-cpp`).
- **Test case**: one recording paired with one label file. A decoder is scored on a recording.
- **Oracle channel**: one station's channel stream, opened at its labeled frequency for the whole
  recording; both decoders read the same recorded streams (`build/suite/full3/channels/`).
- **Development set**: seed 1 of the oracle test cases without group B's per-row recordings, the regular
  expression `DEV` in `training/kz4ap_proto/experiments.py`: 21 test cases, 525 channels, 74 749.9
  channel-seconds, 509 scored signals.
- **Channel-second**: one second of one channel's stream; the unit of decoding cost.
- **Periodicity window**: the periodicity estimator (stage-1 spec 4.4) runs the comb on branch 1's
  posterior over three windows, 2 s, 5 s and 10 s, every 0.25 s (0.256 s in practice: the first whole
  21.3 ms block after it), and estimates a dit T in each; the
  shortest window whose comb score reaches the confidence threshold (0.03, dimensionless) gives T_P, the
  coarse speed. A window whose best score is below the threshold contributes nothing.
- **Near-tie** (D2): a discrete choice whose two best candidates' values differ by no more than rounding,
  on one side and the other, so that last-bit differences between numpy and C++ can flip it.

## 1. Conditions

- **Machine**: the Linux machine: Ubuntu 22.04, 10-core Intel Xeon (Ice Lake), g++ 11.4 (glibc 2.35);
  10 worker threads (`--jobs 10`), nothing else running on it during the decode but a few seconds of a
  script reading the finished files.
- **Build**: CMake preset `linux` (Release, `-O3 -DNDEBUG`), the exact-math build (no `-ffast-math`, no
  vector math library). The replay ran on the build of `fe68fc0`; the commits after it up to this record
  change only `engine/tests/bank/channel_test.cpp` (section 2.4), so the decoder code is the same.
- **Tests**: `ctest --preset linux` at `dea9065`: 306 tests passed or skipped, none failed (8 skipped, 1
  disabled, the same ones as on Windows). `ctest --preset windows` at the same commit: the same. On the
  first Linux run
  (`fe68fc0`) one test failed, `BankChannel.TheTwoPeriodicityNearTiesAreRoundingOnBothSides`: it asserted
  the Windows build's choice at a near-tie, which the Linux build makes differently (section 2.4).
- **Runs**: `kz4ap-bank-replay --out build/suite/full3 --name bank-cpp --only <DEV> --jobs 10` (16.1 min
  wall); both decoders' decoded files then scored with this branch's `kz4ap-bench --score-decoded`
  (`kz4ap_proto.runner.score` into `experiments/results/`), and compared with
  `kz4ap_proto.experiments compare --base bank-proto --variant bank-cpp`. The reference, `bank-proto`, is
  the prototype's final decoded files from stage 1 (`build/suite/full3/proto/bank-proto/`, decoded on the
  Linux machine).
- **Raw outputs** (git-ignored, copied back from the Linux machine), in
  `build/suite/full3/experiments/linux/`: `compare-bank-cpp-vs-bank-proto.md` (the compare),
  `c2-diff-bank-cpp-vs-bank-proto.md` (channel by channel: texts, every other record, CPU per group),
  `c2-pooled-cer.txt`, `c2-ties-bank-cpp-vs-bank-proto.txt` (section 2.3), `c2-gprof-F-drift-s1-1.txt`
  and `c2-cpu-F-drift-s1.linux.txt` (section 3), `c2-replay-dev.log`, `c2-ctest.linux.log`. The helper
  scripts are git-ignored under `build/` (`c2_diff.py`, `c2_tie_rec.py`, `c2_tie.cpp`, `c2_prof.sh`,
  `c2_trace.*`).

## 2. Reference check (Task 9)

### 2.1 Text and scores

| quantity | prototype (`bank-proto`) | port (`bank-cpp`) |
|---|---|---|
| channels decoded | 525 | 525 |
| channels with the identical final text | — | **525 of 525** |
| channels with identical characters (text, start and end to 1e-4 s) | — | 525 of 525 |
| channels with every decoded record identical (characters, corrections, selections, periodicity updates, over starts, switches, as written to 4 or 6 decimals) | — | 395 of 525 |
| pooled CER, 509 signals | 0.2799 (35 457 edits / 126 663 symbols) | 0.2799 (35 457 / 126 663) |
| pooled first-word CER | 0.5191 (1385 / 2668) | 0.5191 (1385 / 2668) |
| paired CER, port minus prototype, all 509 signals | | +0.0000 (+0.0000 to +0.0000) |

The paired CER is +0.0000 with interval +0.0000 to +0.0000 in every group (A, B, C, D, E, F, G, H in
both views, I) and so is the paired first-word CER. The per-group CER intervals in the compare file
differ in the last digit between the two decoders although the values are equal: the bootstrap's random
generator is seeded by the decoder's name, so the resamples differ (not a difference in the decoders).
The prototype's 0.2799 is stage 1's `exp-E9-rekey-0.8`, as the stage-1 record states.

### 2.2 D2: the channels whose text differs

None: 0 of 525. Owner decision D2's system-level criterion ("every development-set channel whose final
text differs from the prototype's is traced to a near-tie") holds with nothing to trace.

### 2.3 The other differences: per-window periodicity estimates

In the 130 channels whose records are not all identical, the only record that differs is the periodicity
record, and within it only the T of one window at some updates:

| over all 525 channels | count |
|---|---|
| periodicity updates, the same number on both sides in every channel (each with T_P, confidence, chosen window, and per window T and score) | 291 690 |
| updates whose T_P or chosen window differs | **0** |
| updates whose confidence differs (as written, 4 decimals) | 0 |
| window estimates (3 per update) | 875 070 |
| window estimates whose T differs | 317 (0.036%) |
| window estimates whose score differs (as written, 4 decimals) | 0 |
| largest score where T differs | 0.0028 (threshold 0.03) |

So each differing window is one whose comb score is below the confidence threshold by a factor of 10 or
more: it does not give T_P, and T_P is the only output of the periodicity estimator that reaches the
decoder (the fit's prior and the selection's fallback). Most are windows of a posterior that is zero but
for a short squelch opening (score 0.0000 as written), where several candidates score almost exactly the
same.

Three traced, the prototype's and the port's scores of the best candidates recomputed from each side's
own buffer at that update (file `c2-ties-bank-cpp-vs-bank-proto.txt`; its "port: pick" lines print a
meaningless value, about 5e-310, read through a reference into a temporary in the helper; the port's
picks below follow from its scores and agree with the decoded files):

| channel, update, window | candidate T (s) | prototype score | port score | lead of each side's pick |
|---|---|---|---|---|
| A-awgn-12wpm-0-s1, label 7, update 132, 2 s | 0.0979470 | **2.393254340787908e-06** | 2.3932543407786109e-06 | prototype: 2.7e-20 absolute (1.1e-14 relative) |
| | 0.0446275 | 2.393254340787881e-06 | **2.3932543408063936e-06** | port: 2.5e-17 absolute (1.1e-11 relative) |
| | 0.0619738 | 2.393254340780915e-06 | 2.3932543407809148e-06 | |
| A-awgn-25wpm-0-s1, label 7, update 403, 5 s | 0.2374616 | **1.2159623781734025e-04** | 1.2159623781734225e-04 | prototype: 4e-20 absolute (3e-16 relative, one or two units in the last place) |
| | 0.2398362 | 1.2159623781734021e-04 | **1.2159623781734232e-04** | port: 7e-20 absolute (6e-16 relative) |
| A-awgn-12wpm-1-s1, label 24, update 462, 2 s (the largest score among the 317) | 0.0220184 | **2.8383525696524456e-03** | 2.8383525696514533e-03 | prototype: 6.6e-16 absolute (2.3e-13 relative) |
| | 0.0127382 | 2.8383525696517864e-03 | **2.8383525696515366e-03** | port: 8.3e-17 absolute (2.9e-14 relative) |

(Bold: each side's pick. Scores are dimensionless.) In each, each side's lead is of the order of the
difference between the two sides' scores of one candidate (largest such difference: 1.9e-17, 2.1e-18 and
9.9e-16 absolute in the three rows; the largest lead is 1.3 times it, the port's 2.5e-17 in the first),
and every lead is at most 1.1e-11 relative, the scale of rounding for comb scores of order 1e-6 (derived
in section 2.4). So each is a near-tie in D2's sense (measured values).
The 314 untraced ones are not individually traced; that they are the same kind is conjectured from their
scores (all below 0.0028, equal on both sides to 4 decimals) and from T_P being identical at every update.

### 2.4 The golden near-tie test on Linux (a defect in the test, fixed)

The golden tests of Task 7 had found two such near-ties on Windows (stream "noise", update 14, and
"farnsworth", update 64, both the 2 s window), and `TheTwoPeriodicityNearTiesAreRoundingOnBothSides`
required the port to pick 44.63 ms at both. On the Linux machine it picked 61.97 ms at farnsworth #64: the
tie is three-way. The test asserted one platform's rounding, which D2 does not require; it now scores all
three candidates on the port and requires the port's pick to be its own largest score, each port score
within the golden tolerance (1e-9 relative) of the prototype's, and the three scores within 1e-16 absolute
of each other on each side (a heuristic margin, not derived: about 1.5 times the largest measured spread,
6.5e-17, and below one unit in the last place of 1.0; relative to the scores it is 2.1e-11 at farnsworth
and 1.05e-10 at noise) (commits `fe74c75`, `c09f458`, `dea9065`, `3e628a6`; the first of these
referred to a temporary, which the second fixed). The values (prototype: numpy on Windows):

| stream, update | candidate T (ms) | prototype | port, Windows (MSVC) | port, Linux (g++, glibc) |
|---|---|---|---|---|
| farnsworth #64 | 97.95 | **4.7329276538316477e-06** | 4.7329276538037837e-06 | 4.7329276537942970e-06 |
| | 44.63 | 4.7329276538129181e-06 | **4.7329276538129181e-06** | 4.7329276537759468e-06 |
| | 61.97 | 4.7329276537666498e-06 | 4.7329276537944054e-06 | **4.7329276538082832e-06** |
| noise #14 | 97.95 | **9.5011319738165349e-07** | 9.5011319737701853e-07 | the same as Windows |
| | 44.63 | 9.5011319738161283e-07 | **9.5011319738161283e-07** | |
| | 61.97 | 9.5011319737464683e-07 | 9.5011319737464683e-07 | |

The spread of the three scores is at most 6.5e-17 absolute on any side; one candidate's score differs
between sides by up to 4.2e-17. Each score is a difference of comb-tooth means of the normalized
autocorrelation (values up to 1, summed in a cumulative sum), so last-bit differences of order 1e-17 in
the means survive the cancellation down to scores of order 1e-6 as relative differences of order 1e-11
(derived, as an order of magnitude; the test's 1e-16 bound is not derived from it). The prototype itself orders the two runners-up differently under numpy on Linux (61.97 ms
4.732927653808283e-06 above 44.63 ms 4.732927653775947e-06; its pick, 97.95 ms, is the same).

## 3. Cost (Task 9)

CPU time per channel-second, from the decoded files' `cpu_s` (each channel's thread CPU time, mixing to
baseband included, as in the prototype) and `channel_s`, on the Linux machine, exact-math build:

| group | channels | channel-seconds (s) | prototype (ms per channel-second) | port (ms per channel-second) | port / prototype |
|---|---|---|---|---|---|
| A sensitivity | 192 | 23 040.0 | 171.85 | 117.50 | 0.68 |
| B fading | 30 | 5 400.3 | 170.81 | 135.44 | 0.79 |
| C fists | 135 | 16 200.0 | 173.41 | 116.57 | 0.67 |
| D speed | 12 | 720.1 | 172.46 | 114.55 | 0.66 |
| E interference | 32 | 1 920.3 | 380.32 | 257.87 | 0.68 |
| F tuning | 28 | 1 440.3 | 202.27 | 138.76 | 0.69 |
| G ragchew | 12 | 4 392.2 | 259.93 | 188.57 | 0.73 |
| H two-station QSO, oracle (both views: 12 + 24 channels) | 36 | 12 996.1 | 234.40 | 169.07 | 0.72 |
| I Farnsworth | 48 | 8 640.5 | 84.04 | 52.47 | 0.62 |
| **all (pooled)** | **525** | **74 749.9** | **183.96** | **128.21** | **0.70** |

(The pooled values are those `experiments compare` prints; the per-group values are from
`c2-diff-bank-cpp-vs-bank-proto.md`.) Total: 9 584 CPU-seconds (160 CPU-minutes), 16.1 min wall on 10
threads, against the plan's budget of about 81 CPU-minutes and 8 minutes (that budget was the 65 ms
projection times 74 748 channel-seconds).

**Against the spike's projection (about 65 ms, derived, not measured).** The port costs 2.0 times the
projection. One channel profiled (F-drift-s1, label 1, 30 s; gprof on a `-pg` build linked statically so
that the math library is sampled; file `c2-gprof-F-drift-s1-1.txt`) shows where:

- **75% of the CPU time is in glibc's scalar math functions**: `log1p` 31%, `exp` 34%, `log` 10%. They
  are called from the duration fit's grid update (`DurationFit::add`: the per-class log-likelihoods and
  their log-sum-exp, `max + log1p(exp(-|d|))`), and from the refinement. The fit code itself
  (`grid_loglik` 10%, `refine` 3%, `terms` 2%) and everything else (boxcars, keying, noise, periodicity
  and its FFT, selection) share the rest.
- On this channel the fits took **252 grid updates per channel-second** (7560 calls of
  `DurationFit::add` in 30.0 s, all branches, the rival and re-keying fits included) and **232 best-fit
  searches** (6972 calls of `refine`); the projection assumed about 160 and 145. With
  the spike's per-call costs (329 µs per observation, 66 µs per search) these rates give 98 ms per
  channel-second (derived), against 130 ms measured for this channel under the profiler and 120.7 ms
  for its test case without it. So on this channel the event rate accounts for 98 − 65 = 33 ms of the
  120.7 − 65 = 56 ms gap to the projection, 59% (derived from the measured rates; that the development
  set's rate is similar is conjectured, not counted).

**Against the prototype (stage 1's "157 ms").** The like-for-like reference is the prototype's own
decoded files on the same 525 channels and machine: 184.0 ms per channel-second, within 0.2% of the stage-1
record's 184.3 ms for `exp-E9-rekey-0.8` (the settled configuration). The 157.3 ms in the stage-1 record
is `exp-E5-coarse` on E5's subset (about 20% of the development set) under the configuration before E6 to
E9, so it is not comparable. The port is 0.70 times the prototype: faster, but by much less than a
compiled port of numpy code might suggest, because both spend most of their time in the same exp and log
evaluations, and numpy evaluates them vectorized (SIMD) while the exact-math C++ build calls glibc's
scalar functions one value at a time (conjectured from the profile above and Task 7's measurement below;
the prototype was not profiled here).

**The Windows PC (Task 7) against the Linux machine.** On the two test cases Task 7 replayed on the
Windows PC (Intel Core i7-12700H, Windows 11, MSVC; files `build/suite/full3/proto/t7-cpp/` and
`t7-proto/`, git-ignored), with the Linux figures for the one of them in the development set:

| test case | port, Windows (ms of CPU per channel-second) | port, Linux (ms of CPU per channel-second) | prototype, Windows (ms of CPU per channel-second) | prototype, Linux (ms of CPU per channel-second) |
|---|---|---|---|---|
| F-drift-s1 (8 channels, 240.1 channel-seconds) | 453.6 | 120.7 | 273.1 | 175.5 |
| first-sample-s1.oracle (4 channels, 80.0 channel-seconds) | 543.3 | not decoded (not in the development set) | 246.4 | — |

On F-drift-s1 the same Python code costs 1.56 times as much on the Windows PC as on the Linux machine,
the port 3.76 times as much; so the port's extra factor on Windows, 3.76 / 1.56 = 2.4, comes with the
build, not the hardware (derived from measured ratios, assuming the prototype's ratio measures the
hardware alone, which is approximate: numpy's own builds also differ between the platforms; the owner's
reading, that the cost is the code's construction, is consistent with them). Which part of the build: the exact-math port spends 75% of its time in the
math library on Linux (above), and Task 7 measured `DurationFit::add` at 458 µs per call with MSVC's
scalar exp and log against 221 µs for numpy's vectorized ones on the same PC; that MSVC's exp, log and
log1p are the main cause of the remaining factor is conjectured (the Windows build was not profiled by
function).

## 4. Full-suite run (Task 10)

**Answer.** Through the engine the bank decoder gives the prototype's scores on the full suite: every
per-regime comparison with Matched and Envelope is the same as stage 1's except the four group F drift rows,
which are not comparable (below). Pooled, it is 0.076 below Matched and 0.252 below Envelope in CER on the
oracle test cases, and within its interval of Matched (+0.009) and 0.063 below Envelope through the detector
path. Six oracle signals (not drifting) end with a text different from the bank's own, from a defect in the
engine's correction events; no score changes. The text as first displayed is worse than the final text by
0.027 CER on average (paired, per signal). Cost: 134 ms of CPU per channel-second, about 240 times Matched's on the same test cases.
No verdict here; the owner reads it. Every number is measured unless marked.

### 4.1 Conditions

- **Code.** `f58837d` on the Linux machine (fetched by bundle), `cmake --build --preset linux` (only the
  bank test binary relinked: the commits after `dea9065` change a test and documents, so the bench is the
  build of `dea9065`, whose decoder code is `f58837d`'s), `ctest --preset linux -j 10`: "100% tests passed
  out of 306" (the same 8 skipped and 1 disabled as in section 1). Raw: `c10-ctest.linux.log`.
- **Run.** The suite runner's own functions, `kz4ap_synth.suites.run_suite(..., ["bank"])` for each of the 123
  recordings (with its station labels where it has them) and `kz4ap_proto.runner.score_engine_copies(...,
  ("bank",))` for each of the 27 oracle copies, called one recording at a time in 10 worker processes by a
  git-ignored helper (`build/c10_run.py`), so the bench commands and result files are exactly those of
  `suites run --decoder bank` and `runner engine-copies --decoder bank`. The runner itself starts one bench
  process at a time, which would have taken about 17 h of wall time (derived: 61 300 CPU-seconds, below). Result
  files: 159 in `build/suite/full3/results/bank/` on the Linux machine (132 scorings of the suite and 27 oracle
  copies): 120 oracle test cases and 39 through the detector path, 3 531 scored signals, 457 168.6
  channel-seconds. Wall time 110 min (21:06 to 22:56), 10 processes on the 10 cores; nothing else ran
  except about one minute of a script reading the finished files at about 21:50.
- **Envelope and Matched** are stage 1's result files (`results/baseline/`, `results/matched/`), reused after
  a check: 54 bench runs with this build (27 test cases × the 2 decoders: the first recording of each group in
  seed 2 with its station labels, and every seed-2 oracle copy) gave the same tracks (identifier, frequency,
  text), the same per-signal edits, symbols and decoded text, and the same CER as the stored files, in all 54
  (raw: `c10-check.log`). Their stored files predate the immediate text; for them it equals the final text
  (section 4.5).
- **Summary.** `kz4ap_synth.suites summarize --out build/suite/full3` (it now has `bank` rows beside
  `baseline`, `matched` and `bank-proto`; stage 1's summary is kept beside it as `stage1-summary.md` on the
  Linux machine). The tables below come from the git-ignored helper `build/c10_analyze.py`, which calls the
  suite's `load_results`, `aggregate`, `paired_differences` and `bootstrap_cer`, and stage 1's
  `report.comparison` and `report.detector_measures`.
- **Raw outputs** (git-ignored, copied back from the Linux machine) in `build/suite/full3/experiments/linux/`:
  `c10-summary.md` and `c10-summary.json` (the suite summary), `c10-analysis.md` (every table of this section
  and the full per-regime table), `c10-regimes.txt` (the per-regime comparison with stage 1), `c10-mix.txt`,
  `c10-trace.txt` and `c10-engine.txt` (section 4.4), `c10-corrections-bank-proto.txt` (section 4.5),
  `c10-bank.log`, `c10-check.log`, `c10-ctest.linux.log`.

### 4.2 Pooled CER

Pooled CER: summed edits over summed symbols. Paired: the mean over signals of the per-signal CER difference
(bank minus the reference, on the same labels). Group H's signals count in both views (labels per QSO and per
station), as in stage 1. "Without oracle-anchor rows" leaves out the rows the suite marks "not meaningful
(oracle anchor)": the F drift rows of 0.5, 1 and 2 Hz/s and the group H oracle QSO-label rows whose answering
station is more than 12 Hz off (39 signals).

| part | signals | bank | Matched | Envelope | bank − Matched (paired) | bank − Envelope (paired) | bank − prototype (paired) |
|---|---|---|---|---|---|---|---|
| oracle test cases, every signal | 2 871 | 0.369 (0.352–0.385) | 0.457 (0.442–0.473) | 0.625 (0.599–0.655) | −0.076 (−0.086 to −0.066) | −0.252 (−0.272 to −0.232) | +0.0018 (+0.0009 to +0.0029) |
| oracle test cases, without oracle-anchor rows | 2 832 | 0.366 (0.350–0.382) | 0.456 (0.440–0.472) | 0.634 (0.601–0.660) | −0.077 (−0.088 to −0.068) | −0.258 (−0.281 to −0.238) | +0.0000 (+0.0000 to +0.0001) |
| through the detector path | 660 | 0.448 (0.409–0.490) | 0.467 (0.426–0.505) | 0.482 (0.442–0.525) | +0.009 (−0.007 to +0.026) | −0.063 (−0.088 to −0.039) | +0.0000 (+0.0000 to +0.0000) |
| all | 3 531 | 0.382 (0.367–0.397) | 0.459 (0.446–0.473) | 0.601 (0.576–0.626) | −0.061 (−0.069 to −0.051) | −0.217 (−0.235 to −0.197) | +0.0015 (+0.0007 to +0.0023) |

The prototype (`bank-proto`, stage 1's decoded files) pools 0.368 on the oracle test cases and 0.448 through
the detector path. Its residual difference from the bank without the anchor rows comes from the F drift row
of 0.2 Hz/s, which the suite does not mark (section 4.4).

### 4.3 Per group

Tags pooled within each group; CER with its interval; paired differences, bank minus the reference.

| group | signals | bank | Matched | Envelope | bank − Matched | bank − Envelope |
|---|---|---|---|---|---|---|
| A sensitivity | 576 | 0.316 (0.280–0.355) | 0.331 (0.290–0.370) | 0.541 (0.484–0.599) | −0.004 (−0.014 to +0.007) | −0.304 (−0.362 to −0.249) |
| B fading | 990 | 0.532 (0.513–0.551) | 0.665 (0.650–0.678) | 0.870 (0.823–0.916) | −0.124 (−0.135 to −0.113) | −0.338 (−0.385 to −0.290) |
| C fists | 405 | 0.056 (0.048–0.064) | 0.190 (0.165–0.217) | 0.189 (0.163–0.215) | −0.148 (−0.175 to −0.123) | −0.143 (−0.166 to −0.118) |
| D speed | 36 | 0.031 (0.018–0.049) | 0.077 (0.037–0.138) | 0.169 (0.079–0.290) | −0.051 (−0.110 to +0.008) | −0.114 (−0.215 to −0.021) |
| E interference | 48 | 0.637 (0.432–0.865) | 0.350 (0.229–0.469) | 0.585 (0.478–0.691) | +0.299 (+0.142 to +0.481) | +0.057 (−0.111 to +0.242) |
| F tuning, every row | 84 | 0.094 (0.072–0.119) | 0.119 (0.078–0.167) | 0.365 (0.289–0.448) | −0.009 (−0.041 to +0.022) | −0.209 (−0.290 to −0.127) |
| F tuning, without oracle-anchor rows | 69 | 0.065 (0.050–0.081) | 0.100 (0.057–0.150) | 0.398 (0.313–0.488) | −0.025 (−0.064 to +0.008) | −0.317 (−0.392 to −0.245) |
| G ragchew | 36 | 0.048 (0.033–0.063) | 0.081 (0.052–0.116) | 0.196 (0.106–0.287) | −0.034 (−0.055 to −0.017) | −0.151 (−0.234 to −0.075) |
| H QSO, detector path | 72 | 0.279 (0.212–0.355) | 0.367 (0.298–0.443) | 0.245 (0.180–0.312) | −0.088 (−0.127 to −0.050) | +0.034 (+0.001 to +0.073) |
| H QSO, detector path (per station) | 144 | 0.856 (0.817–0.894) | 0.843 (0.812–0.873) | 0.882 (0.851–0.912) | +0.011 (−0.027 to +0.048) | −0.025 (−0.063 to +0.011) |
| H QSO, oracle, every row | 36 | 0.347 (0.276–0.419) | 0.420 (0.349–0.493) | 0.281 (0.168–0.417) | −0.073 (−0.124 to −0.020) | +0.068 (−0.070 to +0.194) |
| H QSO, oracle, without oracle-anchor rows | 12 | 0.074 (0.041–0.109) | 0.201 (0.089–0.324) | 0.211 (0.087–0.400) | −0.129 (−0.214 to −0.056) | −0.137 (−0.331 to −0.027) |
| H QSO, oracle (per station) | 72 | 0.735 (0.603–0.865) | 0.764 (0.657–0.867) | 1.141 (0.969–1.323) | −0.030 (−0.137 to +0.068) | −0.410 (−0.639 to −0.214) |
| I Farnsworth | 144 | 0.066 (0.051–0.082) | 0.543 (0.522–0.570) | 0.574 (0.530–0.628) | −0.470 (−0.510 to −0.430) | −0.456 (−0.503 to −0.411) |
| band, detector path | 60 | 0.045 (0.037–0.053) | 0.051 (0.045–0.059) | 0.102 (0.041–0.174) | −0.005 (−0.014 to +0.003) | −0.036 (−0.088 to +0.004) |
| band, oracle | 60 | 0.006 (0.003–0.009) | 0.010 (0.007–0.014) | 0.010 (0.006–0.017) | −0.007 (−0.017 to +0.002) | −0.011 (−0.025 to +0.000) |
| crowded, detector path | 300 | 0.159 (0.120–0.201) | 0.119 (0.089–0.154) | 0.324 (0.272–0.373) | +0.041 (+0.014 to +0.075) | −0.151 (−0.197 to −0.106) |
| crowded, oracle | 300 | 0.160 (0.115–0.213) | 0.074 (0.050–0.104) | 0.298 (0.250–0.348) | +0.109 (+0.062 to +0.165) | −0.113 (−0.163 to −0.061) |
| first sample, detector path | 12 | 0.105 (0.090–0.122) | 0.138 (0.122–0.158) | 0.119 (0.098–0.146) | −0.034 (−0.042 to −0.026) | −0.017 (−0.037 to +0.000) |
| first sample, oracle | 12 | 0.003 (0.000–0.009) | 0.032 (0.029–0.038) | 0.043 (0.030–0.064) | −0.030 (−0.037 to −0.023) | −0.045 (−0.070 to −0.025) |
| pauses, detector path | 24 | 0.198 (0.084–0.315) | 0.213 (0.099–0.336) | 0.235 (0.135–0.343) | −0.015 (−0.020 to −0.009) | −0.039 (−0.071 to −0.011) |
| pauses, oracle | 24 | 0.002 (0.000–0.005) | 0.000 (0.000–0.000) | 0.087 (0.064–0.113) | +0.002 (+0.000 to +0.005) | −0.085 (−0.113 to −0.061) |
| strong, detector path | 24 | 0.031 (0.026–0.038) | 0.050 (0.047–0.053) | 0.036 (0.030–0.042) | −0.019 (−0.024 to −0.013) | −0.004 (−0.012 to +0.004) |
| strong, oracle | 24 | 0.007 (0.001–0.015) | 0.000 (0.000–0.000) | 0.000 (0.000–0.000) | +0.007 (+0.002 to +0.016) | +0.007 (+0.002 to +0.016) |
| tune-up, detector path | 24 | 0.336 (0.183–0.522) | 0.353 (0.187–0.527) | 0.009 (0.002–0.018) | −0.016 (−0.061 to +0.016) | +0.351 (+0.194 to +0.519) |
| tune-up, oracle | 24 | 0.049 (0.042–0.056) | 0.241 (0.103–0.407) | 0.133 (0.074–0.204) | −0.210 (−0.377 to −0.070) | −0.084 (−0.161 to −0.026) |

Through the detector path the bank and Matched decode the same tracks (the Matched path's detector); Envelope's
detector opens its own, so against Envelope these rows compare per label, not per track.

**Every regime** (group and tag, 140 rows; the suite's paired tables, `paired_differences`): rows counted
"better" or "worse" when the paired 95% interval excludes 0 (stage 1's convention, heuristic; of 133 rows truly
unchanged about 6.7 would read better or worse by chance), leaving out the 7 rows the suite marks
"not meaningful (oracle anchor)":

| | better | worse | unchanged |
|---|---|---|---|
| bank against Matched | 50 | 26 | 57 |
| bank against Envelope | 85 | 16 | 32 |

Every one of the 140 rows is in `c10-analysis.md` (section 3). Except the four F drift rows, each row's paired
mean, interval and call is the same as the prototype's in the stage-1 results record, section 4.3.1: 272 of the
280 (row, reference) pairs are identical, and the 8 that differ are the F drift rows (0.2, 0.5, 1 and 2 Hz/s;
`c10-regimes.txt`). This follows from section 4.4: outside the drift rows every signal's edit count is the
prototype's, and the bootstrap's generator is seeded by the row, not the decoder. The group A crossings (S₅₀₀ at
CER 0.10 and 0.05) have the prototype's values too (12 / 25 / 40 WPM at CER 0.10: −0.1, −0.0 and 1.8 dB of S₅₀₀;
at 0.05: 1.1, 1.3 and 3.2 dB of S₅₀₀). Their intervals differ in the last digit, because that bootstrap is
seeded with the decoder's name.

The 26 rows where the bank is worse than Matched: A 12 WPM; B hand 24 WPM at f_D = 3 Hz; C computer imbalance
+0.0 and +0.1, machine +0.0 and −0.1, paddle −0.1 (each +0.002 to +0.014); D 10 WPM and the 30 → 15 WPM ramp;
E six rows, a neighbor at +10 or +20 dB relative to the wanted station's key-down power at 20, 50, 100 or 150 Hz
(up to +1.912 (+1.159 to +2.759) CER at 150 Hz and +20 dB relative to the wanted key-down power); H same-track QSOs scored per station (detector path at
the drawn offset, 0 Hz and 10 Hz; oracle at 0 Hz and 10 Hz); crowded at 50 and 100 Hz spacing (detector path),
and at 0, 50 and 100 Hz (oracle); tune-up 0.6 s through the detector path. These are the rows stage 1 found
worse for the prototype.

**F drift rows: not comparable.** Through the engine the oracle channel is anchored at the label's frequency
without the label's drift; stage 1 mixed by the label's drifting phase law. So the bank minus the prototype
in these rows (+0.011, +0.015, +0.288 and +0.540 CER at 0.2, 0.5, 1 and 2 Hz/s) measures the missing drift, not
the port (controller's ruling; the frequency tracker that would follow the drift is Plan B's). Against Matched
at 1 and 2 Hz/s: +0.083 (+0.011 to +0.158) and +0.054 (−0.008 to +0.131); the suite marks Matched's and the
bank's rows at 0.5, 1 and 2 Hz/s "not meaningful (oracle anchor)".

### 4.4 Bank (engine) against the prototype, signal by signal

Each oracle signal's decoded text (the bench's, matched to the label) in `results/bank/` against
`results/bank-proto/`:

| test cases | signals | identical text | different |
|---|---|---|---|
| oracle, drifting labels (group F drift) | 24 | 9 | 15 |
| oracle, all other labels | 2 895 | 2 889 | **6** |
| through the detector path | 660 | 660 | 0 |

**The 15 drifting labels.** The prototype, run again on the same recorded channels but mixed as the engine
mixes (the anchor at the label's frequency, held fixed, phase from a running sum: `streams.anchored_baseband`),
gives exactly the engine's text on all 15 (`c10-mix.txt`). So these differences are the missing drift and
nothing else.

**The 6 others: a defect in the engine's correction events.** Each differs by one or two characters, and each
has the same edit count on both sides, so no CER changes:

| signal | prototype → engine | at (prototype's character time) |
|---|---|---|
| B-fading-hand-24wpm-3Hz-s1, label 11 | `RBSV TTOA` → `RBSV TQOA` | 122.07 s |
| B-fading-paddle-24wpm-0.3Hz-s3, label 8 | `SMT KT` → `SOT KT` | 60.83 s |
| E-qrm-s1, label 22 | `<HH>* 4` → `<HH>**4`; `R<AS>S5` → `R*S5` | 25.59 s; 37.73 s |
| E-qrm-s2, label 9 | `E***<HH>I**<AS>` → `E****<HH>I*<AS>` (one `*` moved) | 42.33 s |
| E-qrm-s3, label 9 | `D<HH>EEE` → `D*EEE` | 6.46 s |
| E-qrm-s3, label 22 | `<HH>*<HH>*` → `<HH>***` | 52.19 s |

How this was found (`c10-mix.txt`, `c10-trace.txt`, `c10-engine.txt`):

1. **Not the input.** E-qrm-s1's 32 oracle channels, recorded again with this bench and `--decoder bank`, are
   byte-identical to the stage-1 recording the prototype decoded.
2. **Not the mix, not the port.** On each of the 6 recorded channels the prototype under stage 1's mix and
   under the engine's mix, and the C++ bank (Task 9's block-by-block tracer) under both mixes, all give the
   prototype's stored text. The two mixes differ by at most 5.2 · 10⁻⁸ rad of phase; numpy's cos and sin and
   the C library's differ in the last bit on 43 to 44% of the samples. None of it changes a character.
3. **The events.** Driving the engine's `BankDecoder` itself on the recorded channel (32-sample calls with
   the anchor set before each, then `flush`, as the engine does) and assembling the text with the bench's rule
   (`TrackText::apply`) gives the engine's text, while the bank's own list (`BankChannel::output`) and its
   result give the prototype's. They first diverge at a single correction, with 32-sample calls and with
   1000-sample calls alike.
4. **The mechanism**, in B-fading-hand-24wpm-3Hz-s1 label 11: before the call the bank's list ends
   `… [T][Q][T][O][A][␣]`, with the first T at 122.071–122.179 s and the second at 122.074–122.180 s (two
   characters overlapping in time). A "switch" correction at 124.16 s removes Q; the bank's list becomes
   `… [T][T][O][A][␣]`, and the event says `from_index` 290, characters `O A ␣`. The consumer keeps its first
   290 characters, which include Q, and appends `O A ␣`, so it ends `… [T][Q][O][A][␣]`: it keeps Q and loses
   the second T. `Output::replace_from` keeps every character that starts more than 20 s back or ends before
   the cut, and sets `from_index` to the number kept. Here the second T ends before the cut and is kept, while
   Q, listed before it, is not, so the kept characters are not a prefix of the list. The event format, and
   the proof in `docs/signal-processing.md` section 8c ("The consumer's rule"), assume they are. (Derived from
   the code and the printed lists. Q's end time was not printed; it must be at or after the cut, since Q was
   not kept.) The other five fail the same way, at one correction each.

The defect affects only the final text built from the events. The immediate text ignores corrections. The
bank's own result, which the replay tool writes, is unaffected (derived). In this suite it touched 6 of the
2 919 oracle signals and none through the detector path, and changed no edit count. Fixing it is not part of
this task.

### 4.5 Displayed text: immediate and final

`cer_immediate` scores the text as each character was first published (corrections ignored), `cer` the final
text. For Matched and Envelope they are equal: they never correct, and with this bench their immediate text
equalled the final text on all 1 064 tracks of the 54 check runs (section 4.1), with `cer_immediate` = `cer`
in every file. Bank, per group:

| group | signals | CER final | CER immediate | immediate − final (paired) | signals the corrections improved / worsened | characters changed per channel-minute (engine) | corrections per channel-minute (prototype) |
|---|---|---|---|---|---|---|---|
| A sensitivity | 576 | 0.316 | 0.323 | +0.009 (+0.005 to +0.013) | 353 / 40 | 5.82 | 1.61 |
| B fading | 990 | 0.532 | 0.537 | +0.005 (+0.002 to +0.007) | 554 / 292 | 13.49 | 6.44 |
| C fists | 405 | 0.056 | 0.068 | +0.013 (+0.012 to +0.014) | 348 / 28 | 1.89 | 0.96 |
| D speed | 36 | 0.031 | 0.060 | +0.024 (+0.003 to +0.038) | 31 / 1 | 3.55 | 1.33 |
| E interference | 48 | 0.637 | 0.676 | +0.043 (+0.014 to +0.081) | 33 / 11 | 20.69 | 8.39 |
| F tuning | 84 | 0.094 | 0.118 | +0.027 (+0.012 to +0.041) | 63 / 15 | 8.18 | 2.50 |
| G ragchew | 36 | 0.048 | 0.056 | +0.008 (+0.006 to +0.010) | 31 / 3 | 1.86 | 1.60 |
| H QSO, detector path | 72 | 0.279 | 0.314 | +0.036 (+0.010 to +0.071) | 58 / 7 | 17.63 | 3.82 |
| H QSO, detector path (per station) | 144 | 0.856 | 0.940 | +0.082 (+0.027 to +0.139) | 43 / 36 | (the same runs) | |
| H QSO, oracle | 36 | 0.347 | 0.421 | +0.076 (+0.027 to +0.132) | 26 / 9 | 26.53 | 4.98 (both oracle views) |
| H QSO, oracle (per station) | 72 | 0.735 | 1.112 | +0.380 (+0.236 to +0.536) | 51 / 19 | 25.93 | |
| I Farnsworth | 144 | 0.066 | 0.099 | +0.042 (+0.034 to +0.050) | 121 / 5 | 1.92 | 0.58 |
| band, detector path / oracle | 60 / 60 | 0.045 / 0.006 | 0.077 / 0.045 | +0.031 / +0.039 | 44 / 0; 52 / 0 | 6.49 / 5.86 | |
| crowded, detector path / oracle | 300 / 300 | 0.159 / 0.160 | 0.187 / 0.176 | +0.029 / +0.016 | 227 / 17; 225 / 42 | 10.53 / 13.14 | |
| first sample, detector path / oracle | 12 / 12 | 0.105 / 0.003 | 0.195 / 0.084 | +0.091 / +0.082 | 11 / 0; 10 / 1 | 9.20 / 8.00 | |
| pauses, detector path / oracle | 24 / 24 | 0.198 / 0.002 | 0.271 / 0.067 | +0.074 / +0.066 | 24 / 0; 24 / 0 | 8.74 / 2.75 | |
| strong, detector path / oracle | 24 / 24 | 0.031 / 0.007 | 0.088 / 0.057 | +0.058 / +0.049 | 22 / 0; 24 / 0 | 40.57 / 4.91 | |
| tune-up, detector path / oracle | 24 / 24 | 0.336 / 0.049 | 0.354 / 0.048 | +0.016 / −0.004 | 13 / 2; 9 / 10 | 2.76 / 4.66 | |
| **all** | 3 531 | **0.382** | **0.411** | **+0.027 (+0.023 to +0.032)** | | **11.41** | **4.09** |

(Intervals of the paired column for the two-part rows are in `c10-analysis.md`, section 5; every one excludes
0 except tune-up oracle's, −0.016 to +0.008.)

The correction columns:

- **Characters changed (engine)** is the Levenshtein distance, in characters, from each track's immediate text
  to its final text, summed per group, over the channel-minutes decoded. Each engine run counts once: the
  detector path's per-station scorings share their recording's run. In all: 86 913 characters on 3 177 of the
  3 556 tracks, in 7 619.5 channel-minutes. That is 11.4 per channel-minute, or 11.2 per 100 characters first
  displayed. It is a lower bound on the characters a reader saw replaced: a correction that rewrites text and a
  later one that restores it cancel.
- **The count of corrections and their reach** are not in the bench's output: it writes the two texts, not
  the events. The right-hand column is the prototype's count from its decoded files (stage 1's corrections
  script per group, `c10-corrections-bank-proto.txt`). The engine's final texts equal the prototype's on every
  signal but the 21 above, and Task 9 found the port's corrections identical to the prototype's on the
  development set, so the engine's counts are conjectured to be the same. All channels: 31 158 corrections in
  457 169 channel-seconds, 4.09 per channel-minute (switch 13 352, re-key 9 522, time-out 8 284). Reach median
  1.544 s, 90th percentile 5.437 s, 99th percentile 19.861 s, maximum 20.000 s; none beyond the 20 s limit.
  Detector path alone: 3.45 per channel-minute; oracle copies 3.55.

### 4.6 CPU per channel-second through the engine

The bench's process CPU time (spectrum, detector, channelizer and decoders) and the decoders' share, from the
result files. Exact-math build; 10 bench processes at once on the 10 cores.

| decoder | test cases | channel-seconds (s) | CPU (ms per channel-second) | decoders (ms per channel-second) |
|---|---|---|---|---|
| bank, oracle | 120 | 412 972.5 | 130.69 | 130.61 |
| bank, detector path (each recording once) | 33 | 44 196.1 | 166.06 | 165.24 |
| bank, all | 153 | 457 168.6 | **134.11** | 133.96 |
| bank, on the 26 check runs | 26 | 52 579.7 | 148.61 | 148.37 |
| Matched, on the same 26 check runs | 26 | 52 579.7 | **0.610** | 0.282 |
| Envelope, on the same 26 check runs | 26 | 52 577.5 | **0.344** | 0.019 |

- Per group (bank, ms of CPU per channel-second): A 119.9, B 129.8, C 118.1, D 118.1, E 260.8, F 144.2,
  G 201.2, H detector path 166.9, H oracle 156.8 (per station 157.3), I 54.4; band 152.2 / 146.9 (detector
  path / oracle), crowded 182.8 / 197.0, first sample 98.8 / 99.8, pauses 100.9 / 82.9, strong 137.0 / 108.6,
  tune-up 87.5 / 109.1.
- **Against the replay (Task 9).** On the development set's 21 test cases (74 749.9 channel-seconds) the
  engine costs 130.73 ms of CPU per channel-second against the replay tool's 128.21 ms: 2.0% more. The engine
  adds the spectrum, the channelizer and its mixing loop, and the two runs did not share the machine the same
  way (Task 9: 10 replay threads in one process; here 10 bench processes).
- **Totals (derived).** 457 168.6 channel-seconds × 134.11 ms = 61 300 CPU-seconds, about 1 020 CPU-minutes,
  in 110 min of wall time on 10 cores. At 134 ms of CPU per channel-second one core decodes about 7.5 channels
  in real time (1 / 0.134). The bank costs about 240 times Matched and 430 times Envelope per channel-second
  (148.6 / 0.610 = 244 and 148.6 / 0.344 = 432, on the check runs).
- The suite summary's CPU table (`c10-summary.md`) counts each recording's main scoring once and leaves out
  the oracle copies: bank 130.92 ms, Matched 0.532 ms and Envelope 0.272 ms per channel-second. The Matched and
  Envelope figures there come from their stage-1 result files, not from this run.

### 4.7 Detection measures through the detector path

As the bench counts them (a track counts only if it decoded text), behind the Matched path's live detector.
The bank has no frequency tracker; it mixes at the detector's frequency. The bank and Matched decode the same
tracks; Envelope's detector opens its own. All three seeds:

| group | recordings | labels | detection recall: bank / Matched / Envelope | false tracks: bank / Matched / Envelope |
|---|---|---|---|---|
| H two-station QSO | 6 | 72 | 1.000 / 1.000 / 1.000 | 110 / 110 / 109 |
| band | 3 | 60 | 1.000 / 1.000 / 1.000 | 0 / 0 / 0 |
| crowded | 12 | 300 | 0.957 / 0.957 / 0.957 | 0 / 0 / 0 |
| first sample | 3 | 12 | 1.000 / 1.000 / 1.000 | 0 / 0 / 0 |
| pauses | 3 | 24 | 1.000 / 1.000 / 1.000 | 12 / 12 / 12 |
| strong | 3 | 24 | 1.000 / 1.000 / 1.000 | **1** / 12 / 12 |
| tune-up | 3 | 24 | 1.000 / 1.000 / 1.000 | 0 / 0 / 0 |

Tracks per QSO (group H): the bank equals Matched in all 9 tags (1.00 for every same-track tag; 7.17, 5.67 and
7.50 for the separate-track tags, which the detector reopens about once per over; 2.33 and 1.33 for the
ambiguous ones). Envelope differs only in the ambiguous tags (1.67 and 1.50). Every count above, the bank's
tracks per QSO included, equals the prototype's from stage 1, which decoded the recorded channels of the same
detector (stage-1 record, section 5.4).
