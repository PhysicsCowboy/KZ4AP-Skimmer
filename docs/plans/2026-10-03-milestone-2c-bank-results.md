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
decoder decodes exactly the prototype's text on every signal except the 15 drifting labels of group F that
differ. Those differ because the engine mixes at the label's frequency without its drift; the frequency
tracker is Plan B's. So 272 of the 280 per-regime paired comparisons (140 regimes, against Matched and against
Envelope) are the same numbers as stage 1's, and the 8 that differ are the four F drift rows. Pooled over the
oracle test cases its CER is 0.369, against Matched's 0.457 and Envelope's 0.625; through the detector path
0.448, against 0.467 and 0.482 (section 4.2). The first run found a defect in how the engine reported
corrections, which left the final text of 6 labels (4 scored signals and 2 unscored interferers) one or two
characters off the bank's own text. It is
fixed, and the run was repeated (section 4.4). The text as first displayed has CER 0.411, against 0.382
after corrections. Through the engine there are 4.10 corrections per channel-minute, reaching back a median
of 1.544 s and at most 20.000 s (section 4.5). The bank costs 133.8 ms of CPU per channel-second through the
engine; on 26 runs measured the same way, Matched costs 0.61 ms and Envelope 0.34 ms (section 4.6). Its
detection measures equal Matched's except the strong signals' false tracks (1 against 12; section 4.7).

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

*Later change (final review, owner Q4 A).* The golden test streams are now stored as complex64, the engine's
sample type (`engine/tests/data/bank/*.c64`), and every golden result was regenerated from the rounded streams,
so the C++ tests' input is still exactly the prototype's and every tolerance and exact check is unchanged. The
committed data went from 14 416 177 to 7 239 333 bytes and from 8 733 310 to 4 508 704 bytes compressed (zlib
level 6, measured on the committed files; the change's commit message quotes the working copy's sizes, which
count Windows line endings in some JSON files). Every golden channel text, correction count and switch count is unchanged; at noise #14 the
scores moved (prototype 9.501131563217924e-07, 9.501131563218533e-07 and 9.501131563310894e-07 for 97.95, 44.63
and 61.97 ms), and the prototype and the Windows port now both pick 61.97 ms there; farnsworth #64 is unchanged.
The Linux build's first run on the new streams (at `bd973b1`) failed `ChannelGolden/noise` at a third near-tie,
noise #38 (the same 2 s window and the same three candidates). It is traced with the values below and added to the
allowed near-ties (`2b9a01e`); the near-tie test checks it as it checks the other two. Scores (prototype: numpy
2.5.3 on Windows; each side's pick in bold), measured:

| stream, update | candidate T (ms) | prototype | port, Windows (MSVC) | port, Linux (g++, glibc) |
|---|---|---|---|---|
| noise #38 | 97.95 | 2.3515786746381741e-06 | 2.3515786746336205e-06 | 2.3515786746381470e-06 |
| | 44.63 | **2.3515786746568495e-06** | **2.3515786746568495e-06** | 2.3515786746383367e-06 |
| | 61.97 | 2.3515786746405865e-06 | 2.3515786746405865e-06 | **2.3515786746544642e-06** |
| noise #14 | 97.95 | 9.5011315632179236e-07 | 9.5011315631715739e-07 | 9.5011315631715739e-07 |
| | 44.63 | 9.5011315632185335e-07 | 9.5011315632185335e-07 | **9.5011315633110972e-07** |
| | 61.97 | **9.5011315633108939e-07** | **9.5011315633455884e-07** | 9.5011315633108939e-07 |

At noise #38 the leads are 1.6e-17 absolute (6.9e-12 relative) on every side; the spreads of the three scores are
1.9e-17 (prototype), 2.3e-17 (Windows) and 1.6e-17 (Linux), within the test's 1e-16 bound (a heuristic margin
about 1.5 times the largest measured spread, 6.5e-17 at farnsworth #64); the largest difference of one candidate's
score between the prototype and Linux is 1.9e-17 (7.9e-12 relative). At noise #14 Linux picks 44.63 ms by a lead
of 2.0e-20. Farnsworth #64's Linux scores are the same as on the float64 streams. (The previous commit's section
8c gave noise #14's prototype lead as 9.2e-19, 9.7e-13 relative, and its spread as 9.3e-19; they are 9.2e-18,
9.7e-12 relative, and 9.3e-18.)
`docs/signal-processing.md` section 8c, "Channel decoder", port check, has all three.

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

**Answer.** Through the engine the bank decoder gives the prototype's text on every signal of the full suite
except 15 drifting labels of group F, which are not comparable (below). Every per-regime comparison with
Matched and Envelope is therefore the same as stage 1's, except the four group F drift rows. Pooled over the
oracle test cases, its CER is 0.076 below Matched's and 0.252 below Envelope's. Through the detector path the
difference from Matched is +0.009, with an interval of −0.007 to +0.026, which contains 0; the bank is 0.063
below Envelope there. The text as first displayed is worse than the final text by 0.027 CER on average
(paired, per signal). Corrections come at 4.10 per channel-minute, with reach median 1.544 s and maximum
20.000 s. Cost: 133.8 ms of CPU per channel-second over the suite. On the 26 runs measured alongside Matched
(148.1 ms for the bank there), that is about 240 times Matched's cost.

The first run of this task found a defect in the engine's correction events; it was fixed and the whole
suite run again. Every number in this section is from the second run, except where section 4.4 describes the
first. No verdict here; the owner reads it. Every number is measured unless marked.

### 4.1 Conditions

- **Code.** The second run is at `92ba4ab`: the correction fix (`1b2f4ef`), the suite summary's drift mark
  for the bank (`ed3eb23`), and the bench's and summary's correction statistics (`92ba4ab`). It was fetched by
  bundle and built with `cmake --build --preset linux`. `ctest --preset linux -j 10` gave "100% tests passed out
  of 309": the same 8 skipped and 1 disabled as in section 1, and 3 tests new with the fix (raw:
  `c10-ctest-fix1.linux.log`). On Windows, `ctest --preset windows` passed all 309, and
  `bash bench/smoke.sh build/windows` passed (Envelope smoke CER 0.0353, Matched 0.0436). The first run was
  at `f58837d`, with the bench built at `dea9065`, whose decoder code is the same (raw: `c10-ctest.linux.log`,
  306 tests).
- **Run.** The runner's own functions are called one recording at a time in 10 worker processes by a
  git-ignored helper (`build/c10_run.py`): `kz4ap_synth.suites.run_suite(..., ["bank"])` for each of the 123
  recordings (with its station labels where it has them), and `kz4ap_proto.runner.score_engine_copies(...,
  ("bank",))` for each of the 27 oracle copies. The bench commands and result files are therefore exactly
  those of `suites run --decoder bank` and `runner engine-copies --decoder bank`. The runner itself starts one
  bench process at a time, which would have taken about 17 h of wall time (derived: 61 200 CPU-seconds,
  below).
  - **Output.** 159 result files in `build/suite/full3/results/bank/` on the Linux machine: 132 from the
    suite's test cases and 27 from the oracle copies. Of these, 120 are oracle test cases and 39 are test
    cases scored through the detector path (33 recordings, 6 of them also scored per station). Together they
    hold 3 579 labels, 3 531 of them scored, and 457 168.6 channel-seconds.
  - **Timing.** Wall time 110.2 min (00:26:20 to 02:16:29: the job's start, printed by `date`, and the log
    file's last write). 10 processes ran on the 10 cores, and nothing else ran on the machine.
  - **The first run** took 110 min the same way (21:06 to 22:56 the day before). A script reading the
    finished files ran for about one minute during it. Its results are kept on the Linux machine as
    `build/suite/full3/bank-before-fix/`.
- **Envelope and Matched** are stage 1's result files (`results/baseline/`, `results/matched/`), reused after
  a check. 54 bench runs with the build of the first run covered 27 test cases × the 2 decoders: the first
  recording of each group in seed 2 with its station labels, and every seed-2 oracle copy. They gave the same
  tracks (identifier, frequency, text), the same per-signal edits, symbols and decoded text, and the same CER
  as the stored files, in all 54 (raw: `c10-check.log`). The 27 test cases are 26 engine runs: H-qso-s2 and its
  station labels share one run, which is why section 4.6 counts 26. That their stored files' immediate text
  equals the final text is derived, not measured: they never correct. On the 54 check runs it is measured
  (section 4.5).
- **Summary.** `kz4ap_synth.suites summarize --out build/suite/full3`. It now has `bank` rows beside
  `baseline`, `matched` and `bank-proto`, and a "Corrections" section. Stage 1's summary is kept beside it as
  `stage1-summary.md` on the Linux machine. The tables below come from the git-ignored helper
  `build/c10_analyze.py`. It calls the suite's `load_results`, `aggregate`, `paired_differences`,
  `bootstrap_cer` and `correction_stats`, and stage 1's `report.comparison` and `report.detector_measures`.
- **Raw outputs** (git-ignored, copied back from the Linux machine) are in
  `build/suite/full3/experiments/linux/`:
  - Second run: `c10-fix1-summary.md` and `c10-fix1-summary.json` (the suite summary), `c10-fix1-analysis.md`
    (every table of this section, the full per-regime table, and section 9's comparison of the two runs),
    `c10-fix1-regimes.txt` (the per-regime comparison with stage 1), `c10-fix1-bank.log`,
    `c10-ctest-fix1.linux.log`.
  - First run: `c10-summary.md`, `c10-analysis.md`, `c10-regimes.txt`, `c10-bank.log`, `c10-check.log`,
    `c10-ctest.linux.log`.
  - The defect's investigation (section 4.4): `c10-rec.txt`, `c10-mix.txt`, `c10-trace.txt`, `c10-engine.txt`,
    `c10-divergences.txt`.
  - The prototype's correction counts: `c10-corrections-bank-proto.txt`.

### 4.2 Pooled CER

Pooled CER: summed edits over summed symbols. Paired: the mean over signals of the per-signal CER difference
(bank minus the reference, on the same labels). Group H's signals count in both views (labels per QSO and per
station), as in stage 1. "Without oracle-anchor rows" leaves out the rows the suite marks "not meaningful
(oracle anchor)" for the bank. That is 48 signals: all 24 F drift labels (0.2, 0.5, 1 and 2 Hz/s; for the
bank every drifting oracle label is marked, since it mixes without the drift, `ed3eb23`), and the 24 group H
oracle QSO labels whose answering station is more than 12 Hz off. The scored signals are 3 531 of the 3 579
labels: the 48 interferer labels of group E (16 per recording) carry no score.

| part | signals | bank | Matched | Envelope | bank − Matched (paired) | bank − Envelope (paired) | bank − prototype (paired) |
|---|---|---|---|---|---|---|---|
| oracle test cases, every signal | 2 871 | 0.369 (0.352–0.385) | 0.457 (0.442–0.473) | 0.625 (0.599–0.655) | −0.076 (−0.086 to −0.066) | −0.252 (−0.272 to −0.232) | +0.0018 (+0.0009 to +0.0029) |
| oracle test cases, without oracle-anchor rows | 2 823 | 0.366 (0.349–0.382) | 0.456 (0.440–0.471) | 0.634 (0.604–0.663) | −0.078 (−0.088 to −0.068) | −0.259 (−0.281 to −0.238) | +0.0000 (+0.0000 to +0.0000) |
| through the detector path | 660 | 0.448 (0.409–0.490) | 0.467 (0.426–0.505) | 0.482 (0.442–0.525) | +0.009 (−0.007 to +0.026) | −0.063 (−0.088 to −0.039) | +0.0000 (+0.0000 to +0.0000) |
| all | 3 531 | 0.382 (0.367–0.397) | 0.459 (0.446–0.473) | 0.601 (0.576–0.626) | −0.061 (−0.069 to −0.051) | −0.217 (−0.235 to −0.197) | +0.0015 (+0.0007 to +0.0023) |

The prototype (`bank-proto`, stage 1's decoded files) pools 0.368 on the oracle test cases and 0.448 through
the detector path. Without the oracle-anchor rows, the bank and the prototype pool the same CER, with a
paired difference of exactly 0: every remaining signal has the prototype's text (section 4.4). Every value in
this table equals the first run's, except the "without oracle-anchor rows" row. That row changed because the
F drift row of 0.2 Hz/s is now marked; it had 2 832 signals, and its paired difference against the prototype
was +0.0000 (+0.0000 to +0.0001).

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
| F tuning, without oracle-anchor rows (the offset rows) | 60 | 0.067 (0.053–0.084) | 0.103 (0.059–0.156) | 0.419 (0.325–0.516) | −0.026 (−0.069 to +0.010) | −0.350 (−0.439 to −0.265) |
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
detector opens its own, so against Envelope these rows compare per label, not per track. Every row equals the
first run's except "F tuning, without oracle-anchor rows". That row changed because the 0.2 Hz/s drift row is
now marked; in the first run it had 69 signals.

**Every regime** (group and tag, 140 rows; the suite's paired tables, `paired_differences`): a row is counted
"better" or "worse" when its paired 95% interval excludes 0. This is stage 1's convention and a heuristic: if
all 132 rows counted were truly unchanged, about 6.6 would read better or worse by chance. The counts leave out
the 8 rows the suite marks "not meaningful (oracle anchor)" for the bank: the four F drift rows and four
group H oracle QSO-label rows.

| | better | worse | unchanged |
|---|---|---|---|
| bank against Matched | 50 | 26 | 56 |
| bank against Envelope | 84 | 16 | 32 |

(The first run left out 7 rows, because the 0.2 Hz/s drift row was not yet marked: 50 / 26 / 57 against
Matched, 85 / 16 / 32 against Envelope.) Every one of the 140 rows is in `c10-fix1-analysis.md` (section 3). Except the four F drift rows, each row's paired
mean, interval and call is the same as the prototype's in the stage-1 results record, section 4.3.1: 272 of the
280 (row, reference) pairs are identical, and the 8 that differ are the F drift rows (0.2, 0.5, 1 and 2 Hz/s;
`c10-fix1-regimes.txt`, the same in both runs). This follows from section 4.4: outside the drift rows every
signal's text is the prototype's, and the bootstrap's generator is seeded by the row, not the decoder. The group A crossings (S₅₀₀ at
CER 0.10 and 0.05) have the prototype's values too (12 / 25 / 40 WPM at CER 0.10: −0.1, −0.0 and 1.8 dB of S₅₀₀;
at 0.05: 1.1, 1.3 and 3.2 dB of S₅₀₀). Their intervals differ in the last digit, because that bootstrap is
seeded with the decoder's name.

The 26 rows where the bank is worse than Matched: A 12 WPM; B hand 24 WPM at f_D = 3 Hz; C computer imbalance
+0.0 and +0.1, machine +0.0 and −0.1, paddle −0.1 (each +0.002 to +0.014); D 10 WPM and the 30 → 15 WPM ramp;
E six rows, a neighbor at +10 or +20 dB relative to the wanted station's key-down power at 20, 50, 100 or 150 Hz
(up to +1.912 (+1.159 to +2.759) CER at 150 Hz and +20 dB relative to the wanted key-down power); H same-track QSOs scored per station (detector path at
the drawn offset, 0 Hz and 10 Hz; oracle at 0 Hz and 10 Hz); crowded at 50 and 100 Hz spacing (detector path),
and at 0, 50 and 100 Hz (oracle); tune-up 0.6 s through the detector path. These are the rows stage 1 found
worse for the prototype (derived: the rows' paired values are stage 1's, above).

**F drift rows: not comparable.** Through the engine the oracle channel is anchored at the label's frequency
without the label's drift; stage 1 mixed by the label's drifting phase law. So the bank minus the prototype
in these rows (+0.011, +0.015, +0.288 and +0.540 CER at 0.2, 0.5, 1 and 2 Hz/s) measures the missing drift, not
the port (controller's ruling; the frequency tracker that would follow the drift is Plan B's). Against Matched
at 1 and 2 Hz/s: +0.083 (+0.011 to +0.158) and +0.054 (−0.008 to +0.131). The suite marks the bank's rows at
all four drift rates "not meaningful (oracle anchor)", and Matched's at 0.5, 1 and 2 Hz/s.

### 4.4 Bank (engine) against the prototype, signal by signal

Each label's decoded text (the bench's, matched to the label) in `results/bank/` against
`results/bank-proto/`, second run. The oracle test cases hold 2 919 labels: the 2 871 scored signals of
section 4.2, and the 48 interferer labels of group E, which are decoded (each has its own oracle channel) but
not scored.

| test cases | labels | identical text | different |
|---|---|---|---|
| oracle, drifting labels (group F drift; all scored) | 24 | 9 | 15 |
| oracle, other labels, scored | 2 847 | 2 847 | 0 |
| oracle, other labels, not scored (group E interferers) | 48 | 48 | 0 |
| through the detector path (all scored) | 660 | 660 | 0 |

**The 15 drifting labels.** The prototype was run again on the same recorded channels, mixed as the engine
mixes: the anchor at the label's frequency, held fixed, phase from a running sum (`streams.anchored_baseband`).
It gives exactly the engine's text on all 15 (`c10-mix.txt`, against the first run; the second run's text on
these 15 is the same). So these differences are the missing drift and nothing else. They are not comparable
(section 4.3), and the suite now marks all four drift rows for the bank.

**A defect in the engine's correction events, found by the first run and fixed.** In the first run, 6 other
oracle labels also differed from the prototype by one or two characters. Four of them are scored signals:
B-fading-hand-24wpm-3Hz-s1 label 11, B-fading-paddle-24wpm-0.3Hz-s3 label 8, E-qrm-s1 label 22 and E-qrm-s3
label 22. Two are unscored interferers: E-qrm-s2 label 9 and E-qrm-s3 label 9. None was through the detector
path. Each of the 4 scored signals had the same edit count as the prototype, so no CER changed; the 2
interferers enter no CER at all. The 6 count labels whose final text differed at the end. Corrections that
made the consumer's list wrong and were later repaired by another correction were not counted.

| label | prototype → engine (first run) | at (prototype's character time) | scored |
|---|---|---|---|
| B-fading-hand-24wpm-3Hz-s1, label 11 | `RBSV TTOA` → `RBSV TQOA` | 122.07 s | yes |
| B-fading-paddle-24wpm-0.3Hz-s3, label 8 | `SMT KT` → `SOT KT` | 60.83 s | yes |
| E-qrm-s1, label 22 | `<HH>* 4` → `<HH>**4`; `R<AS>S5` → `R*S5` | 25.59 s; 37.73 s | yes |
| E-qrm-s2, label 9 | `E***<HH>I**<AS>` → `E****<HH>I*<AS>` (one `*` moved) | 42.33 s | no |
| E-qrm-s3, label 9 | `D<HH>EEE` → `D*EEE` | 6.46 s | no |
| E-qrm-s3, label 22 | `<HH>*<HH>*` → `<HH>***` | 52.19 s | yes |

How this was found (`c10-rec.txt`, `c10-mix.txt`, `c10-trace.txt`, `c10-engine.txt`, `c10-divergences.txt`):

1. **Not the input.** E-qrm-s1's 32 oracle channels, recorded again with the bench and `--decoder bank`, are
   byte-identical to the stage-1 recording the prototype decoded (`c10-rec.txt`, with the build of the second
   run).
2. **Not the mix, not the port.** On each of the 6 recorded channels the prototype under stage 1's mix and
   under the engine's mix, and the C++ bank (Task 9's block-by-block tracer) under both mixes, all give the
   prototype's stored text. The two mixes differ by at most 5.2 · 10⁻⁸ rad of phase; numpy's cos and sin and
   the C library's differ in the last bit on 43 to 44% of the samples. None of it changes a character.
3. **The events.** Driving the engine's `BankDecoder` itself on the recorded channel (32-sample calls with
   the anchor set before each, then `flush`, as the engine does) and assembling the text with the bench's rule
   (`TrackText::apply`) gives the engine's text, while the bank's own list (`BankChannel::output`) and its
   result give the prototype's. They first diverge at a single correction, with 32-sample calls and with
   1000-sample calls alike.
   Every divergence, not only the first, was traced with 32-sample calls (`c10-divergences.txt`): after each
   call with corrections, the bank's list was compared with the consumer's, and the call where each new
   difference appears was listed. In five of the labels a single correction makes the one difference.
   E-qrm-s2 label 9's appears as a deletion and an insertion, both from the correction at 44.971 s. E-qrm-s1
   label 22 has two defective corrections, matching its two differences:
   - a "switch" at 26.005 s with `from_index` 121, after which the consumer keeps `*` where the bank has `␣`
     (prototype `<HH>* 4`, engine `<HH>**4`);
   - a "switch" at 39.552 s with `from_index` 207, after which the consumer has lost `<AS>` and keeps a `*`
     after it. A further "switch" at 39.680 s, again `from_index` 207, leaves the consumer with `*` where the
     bank has `<AS>` (prototype `R<AS>S5`, engine `R*S5`).
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
   not kept.) The others fail the same way, at the corrections listed in step 3.

The defect affected only the final text built from the events; the immediate text ignores corrections, and
the bank's own result, which the replay tool writes, was right (derived).

**The fix** (`1b2f4ef`, described in `docs/signal-processing.md` section 8c, "The consumer's rule") has three
parts:

- `Output::replace_from` also records `first_changed_index`, the first position whose character's text
  differs between the list before and after the replacement. The event's `from_index` is
  min(`from_index`, `first_changed_index`), and its characters are the bank's from there on, kept ones
  included. The prototype's `from_index` and every golden value are unchanged.
- The `BankDecoder` keeps the consumer's list as its updates build it, and checks it against the bank's list
  after every change of the list's text.
- If the two lists ever differ, it sends a correction with reason "resync".

A test on the first 10 s of E-qrm-s3 label 9's recorded channel failed before the fix and passes after it. On
the second run:

- **Measured.** No "resync" correction was sent anywhere in the suite (0 of 31 205 corrections), so the
  corrected index alone kept the consumer's list equal to the bank's after every update. That the
  event-assembled final text equals the bank's own on every channel follows from this (derived: the check runs
  after every change).
- **Measured.** The 6 labels now have the prototype's text, with the same edit counts as before (251, 113,
  277, 158, 149 and 218).
- **Measured.** All the other 3 573 labels have the same final text, edit count and immediate text as in the
  first run (`c10-fix1-analysis.md`, section 9).

### 4.5 Displayed text: immediate and final

`cer_immediate` scores the text as each character was first published (corrections ignored), `cer` the final
text. For Matched and Envelope they are equal: they never correct, and with this bench their immediate text
equaled the final text on all 1 064 tracks of the 54 check runs (section 4.1), with `cer_immediate` = `cer`
in every file. Bank, per group, second run:

| group | signals | CER final | CER immediate | immediate − final (paired) | signals the corrections improved / worsened |
|---|---|---|---|---|---|
| A sensitivity | 576 | 0.316 | 0.323 | +0.009 (+0.005 to +0.013) | 353 / 40 |
| B fading | 990 | 0.532 | 0.537 | +0.005 (+0.002 to +0.007) | 554 / 292 |
| C fists | 405 | 0.056 | 0.068 | +0.013 (+0.012 to +0.014) | 348 / 28 |
| D speed | 36 | 0.031 | 0.060 | +0.024 (+0.003 to +0.038) | 31 / 1 |
| E interference | 48 | 0.637 | 0.676 | +0.043 (+0.014 to +0.081) | 33 / 11 |
| F tuning | 84 | 0.094 | 0.118 | +0.027 (+0.012 to +0.041) | 63 / 15 |
| G ragchew | 36 | 0.048 | 0.056 | +0.008 (+0.006 to +0.010) | 31 / 3 |
| H QSO, detector path | 72 | 0.279 | 0.314 | +0.036 (+0.010 to +0.071) | 58 / 7 |
| H QSO, detector path (per station) | 144 | 0.856 | 0.940 | +0.082 (+0.027 to +0.139) | 43 / 36 |
| H QSO, oracle | 36 | 0.347 | 0.421 | +0.076 (+0.027 to +0.132) | 26 / 9 |
| H QSO, oracle (per station) | 72 | 0.735 | 1.112 | +0.380 (+0.236 to +0.536) | 51 / 19 |
| I Farnsworth | 144 | 0.066 | 0.099 | +0.042 (+0.034 to +0.050) | 121 / 5 |
| band, detector path / oracle | 60 / 60 | 0.045 / 0.006 | 0.077 / 0.045 | +0.031 / +0.039 | 44 / 0; 52 / 0 |
| crowded, detector path / oracle | 300 / 300 | 0.159 / 0.160 | 0.187 / 0.176 | +0.029 / +0.016 | 227 / 17; 225 / 42 |
| first sample, detector path / oracle | 12 / 12 | 0.105 / 0.003 | 0.195 / 0.084 | +0.091 / +0.082 | 11 / 0; 10 / 1 |
| pauses, detector path / oracle | 24 / 24 | 0.198 / 0.002 | 0.271 / 0.067 | +0.074 / +0.066 | 24 / 0; 24 / 0 |
| strong, detector path / oracle | 24 / 24 | 0.031 / 0.007 | 0.088 / 0.057 | +0.058 / +0.049 | 22 / 0; 24 / 0 |
| tune-up, detector path / oracle | 24 / 24 | 0.336 / 0.049 | 0.354 / 0.048 | +0.016 / −0.004 | 13 / 2; 9 / 10 |
| **all** | 3 531 | **0.382** | **0.411** | **+0.027 (+0.023 to +0.032)** | |

The intervals of the paired column for the two-part rows are in `c10-fix1-analysis.md`, section 5. Every one
excludes 0 except tune-up oracle's (−0.016 to +0.008). The table is the same as the first run's: the fix
changed no edit count.

**Corrections through the engine** (stage-2 spec §4.2), from the corrections the bench received in the
engine's text events. The bench writes them per track since `92ba4ab`, and `suites summarize` counts them in
its "Corrections" section. Each engine run counts once: a detector-path recording's station labels share
its run. Reach is the correction's time minus the start of the first character it replaced, in s.
Characters changed is the Levenshtein distance, in characters, from each track's immediate text to its final
text, per channel-minute.

| group | corrections per channel-minute | reach median / 99th percentile / maximum (s) | characters changed per channel-minute |
|---|---|---|---|
| A sensitivity | 1.613 | 1.854 / 19.923 / 20.000 | 5.82 |
| B fading | 6.439 | 1.434 / 19.618 / 20.000 | 13.49 |
| C fists | 0.957 | 3.175 / 19.931 / 19.995 | 1.89 |
| D speed | 1.333 | 3.080 / 4.497 / 4.853 | 3.55 |
| E interference | 8.394 | 0.926 / 19.929 / 19.994 | 20.62 |
| F tuning | 3.152 | 3.218 / 19.894 / 19.982 | 8.18 |
| G ragchew | 1.595 | 1.946 / 19.973 / 19.987 | 1.86 |
| H QSO, detector path (both views: one run) | 3.819 | 1.737 / 19.936 / 20.000 | 17.63 |
| H QSO, oracle | 5.037 | 1.816 / 19.822 / 20.000 | 26.53 |
| H QSO, oracle (per station) | 4.955 | 1.761 / 19.849 / 20.000 | 25.93 |
| I Farnsworth | 0.579 | 3.951 / 20.000 / 20.000 | 1.92 |
| band, detector path / oracle | 1.879 / 1.866 | 1.498 / 1.966 / 1.988; 3.006 / 3.722 / 4.015 | 6.49 / 5.86 |
| crowded, detector path / oracle | 3.140 / 4.380 | 1.519 / 19.884 / 19.966; 1.383 / 19.918 / 19.987 | 10.53 / 13.14 |
| first sample, detector path / oracle | 2.977 / 2.998 | 1.690 / 1.882 / 1.882; 1.690 / 1.939 / 1.967 | 9.20 / 8.00 |
| pauses, detector path / oracle | 2.104 / 1.219 | 1.818 / 13.831 / 14.053; 3.069 / 13.866 / 14.053 | 8.74 / 2.75 |
| strong, detector path / oracle | 2.490 / 2.166 | 1.754 / 18.249 / 19.739; 3.098 / 3.428 / 3.439 | 40.57 / 4.91 |
| tune-up, detector path / oracle | 1.334 / 1.749 | 1.519 / 1.934 / 1.946; 3.439 / 5.801 / 5.914 | 2.76 / 4.66 |
| **all** | **4.095** | **1.544 / 19.861 / 20.000** | **11.41** |

- **In all:** 31 205 corrections in 7 619.5 channel-minutes: switch 13 371, re-key 9 521, time-out 8 313,
  resync 0. Reach 90th percentile 5.445 s; none beyond the 20 s limit. Oracle test cases 4.164 per
  channel-minute; through the detector path 3.454.
- **Characters changed:** 86 906 characters on 3 177 of the 3 556 tracks; 11.2 per 100 characters first
  displayed. It is a lower bound on the characters a reader saw replaced: a correction that rewrites text and
  a later one that restores it cancel. The number of characters each correction replaced was not written by
  the bench in these runs, so the spec's "characters changed" is given only as this net measure. (First run:
  86 913; E interference 1 987. The difference is the fixed defect.) *Later change (final review):* the bench now
  writes, per correction, the characters removed and inserted (the smallest block that differs between the
  replaced and the new characters), and the suite summary sums them per group, per channel-minute
  (`docs/signal-processing.md` section 8c, "The bench"). The statistic is implemented and is measured from the
  next full run; the suite was not re-run for it, so in Plan A the net Levenshtein measure above remains the
  reported figure.
- **Against the prototype** (stage 1's corrections script on its decoded files,
  `c10-corrections-bank-proto.txt`): the counts per group are equal in every group except F tuning. There the
  engine has 227 against the prototype's 180, from the drifting labels. All: 31 205 against 31 158; reach
  median 1.544 s on both sides, 99th percentile 19.861 s on both.

### 4.6 CPU per channel-second through the engine

The bench's process CPU time (spectrum, detector, channelizer and decoders) and the decoders' share, from the
result files. Exact-math build; 10 bench processes at once on the 10 cores.

| decoder | test cases | channel-seconds (s) | CPU (ms per channel-second) | decoders (ms per channel-second) |
|---|---|---|---|---|
| bank, oracle | 120 | 412 972.5 | 130.41 | 130.20 |
| bank, detector path (each recording once) | 33 | 44 196.1 | 165.56 | 164.67 |
| bank, all | 153 | 457 168.6 | **133.81** | 133.54 |
| bank, on the 26 check runs | 26 | 52 579.7 | 148.07 | 147.69 |
| Matched, on the same 26 check runs | 26 | 52 579.7 | **0.610** | 0.282 |
| Envelope, on the same 26 check runs | 26 | 52 577.5 | **0.344** | 0.019 |

The bank rows are from the second run. The first run gave 130.69, 166.06, 134.11 and 148.61 ms; the
difference, −0.3 to −0.5 ms, is not attributed. The Matched and Envelope rows are the check runs of
section 4.1: 27 test cases, 26 engine runs.

- **Per group** (bank, ms of CPU per channel-second): A 119.6, B 129.6, C 117.7, D 116.9, E 259.4, F 143.5,
  G 200.6, H detector path 166.3, H oracle 157.0 (per station 156.8), I 54.2. Detector path / oracle: band
  153.3 / 146.1, crowded 182.1 / 196.8, first sample 98.2 / 99.6, pauses 100.5 / 82.1, strong 136.8 / 108.5,
  tune-up 86.6 / 109.0.
- **The two clocks are not nested.** The "decoders" column is steady-clock (wall) time spent inside the
  decoders; the CPU column is the process's CPU time. So the decoders' share can exceed the CPU, as in group E:
  259.61 against 259.43 ms. That the CPU is almost all in the decoder (all but 0.2 to 0.9 ms per
  channel-second in the table's rows) is therefore approximate.
- **Against the replay (Task 9).** On the development set's 21 test cases (74 749.9 channel-seconds) the
  engine costs 130.39 ms of CPU per channel-second, against the replay tool's 128.21 ms: 1.7% more. The engine
  adds the spectrum, the channelizer and its mixing loop. The two runs also did not share the machine the same
  way: Task 9 ran 10 replay threads in one process, here 10 bench processes.
- **Totals (derived).** 457 168.6 channel-seconds × 133.81 ms = 61 200 CPU-seconds, about 1 020 CPU-minutes,
  in 110 min of wall time on 10 cores. At 134 ms of CPU per channel-second one core decodes about 7.5 channels
  in real time (1 / 0.134). On the check runs the bank costs about 240 times Matched (148.07 / 0.610 = 243)
  and 430 times Envelope (148.07 / 0.344 = 430) per channel-second.
- **The suite summary's CPU table** (`c10-fix1-summary.md`) counts each recording's main result once and
  leaves out the oracle copies: bank 130.63 ms, Matched 0.532 ms and Envelope 0.272 ms per channel-second.
  The Matched and Envelope figures there come from their stage-1 result files, not from this run.

### 4.7 Detection measures through the detector path

As the bench counts them (a track counts only if it decoded text), behind the Matched path's live detector.
The bank has no frequency tracker; it mixes at the detector's frequency. The bank and Matched decode the same
tracks; Envelope's detector opens its own. All three seeds; the two runs give the same counts:

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

## 5. Open items (final review)

The final whole-branch review's findings that this plan leaves open, each with why and where it goes. Everything
else the review found is fixed on the branch (the fix report is in the plan's working folder).

- **Exact zeros then noise: the noise estimate goes NaN, so nothing is published** (Review Focus 3). Kept
  faithful to the prototype by ruling; a decoder behavior change. Plan B, first.
- **The bank's events carry speed 0 WPM and confidence 0** (stage-1 spec 4.6 names the selected branch's T as
  the reported speed). A change of what the decoder reports. Plan B.
- **Per-tag counts behind "227 against the prototype's 180" corrections in group F** (section 4.5). The
  per-result files of that run are on the Linux machine only, and the counts need no new run. Plan B's first full
  run reports them per tag.
- **Characters changed per correction, measured** (stage-2 spec 4.2). Implemented in the bench and the suite
  summary; measuring it needs a full run, which this plan does not repeat. Plan B's first full run.
- **No test pins the keyer's own exact-zero NaN path.** The Plan B zero-input fix changes that path, so the test
  belongs with it. Plan B.
- **Two unreachable `comb_estimate` edge cases differ from the prototype without a trace.** Making them agree
  changes the decoder's behavior (on inputs it never receives). Backlog.
- **No drift guard on the committed golden files** (comparing a regeneration with them). It needs a decision on
  which fields are stable across numpy builds and CPUs (last bits, near-ties). Backlog.
- **Test cost** (about 400 s of ctest on the development PC, most of it the bank's golden-stream tests, paid
  twice in CI). A CI choice (`ctest -j`, or a `slow` label run separately). Backlog.
- **The bank's logistic duplicates Matched's.** Sharing it touches Matched's source, and `classical_decoder.cpp`
  has a different function of the same name in the same namespace. Backlog.
- **Golden coverage still missing:** `masked_branch_power`, `kept_fraction_sum`, `masked_power_sum` (covered end
  to end through σ²); a fit golden that selects q ≠ 3 or T_g ≠ T (mitigated: the Farnsworth channels replay
  identically); the replay's `baseband` and the engine's anchored mixing against a prototype golden of
  `anchored_baseband` (mitigated by the replay and the full suite). Each needs new golden inputs. Backlog.
- **The fit tables are stored as decimal text** (about 1.3 MB on disk; base64 would be about 0.7 MB, little gain
  compressed). Optional. Backlog.
- **Not defects, recorded:** the replay tool's decoded files are JSON-equivalent to `runner.decode`'s, not
  byte-identical (Task 9's tooling accepts them); the C++ fast-path fit test is close to tautological but kept as
  the brief requires; the temporary outputs `build/t8` and `build/suite/full3/proto/t8-proto-det` are
  git-ignored local folders, left for the owner to delete.
