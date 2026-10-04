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
own buffer at that update (file `c2-ties-bank-cpp-vs-bank-proto.txt`):

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
difference between the two sides' scores of one candidate (largest such difference: 1.9e-17, 2.0e-18 and
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
of each other on each side (commits `fe74c75`, `c09f458`, `dea9065`, `3e628a6`; the first of these
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
(derived). The prototype itself orders the two runners-up differently under numpy on Linux (61.97 ms
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
  for its test case without it. So most of the gap to the projection is the event rate (measured on this
  one channel; that the development set's rate is similar is conjectured, not counted).

**Against the prototype (stage 1's "157 ms").** The like-for-like reference is the prototype's own
decoded files on the same 525 channels and machine: 184.0 ms per channel-second, the same as the stage-1
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

| test case | port, Windows | port, Linux | prototype, Windows | prototype, Linux |
|---|---|---|---|---|
| F-drift-s1 (8 channels, 240.1 channel-seconds) | 453.6 ms | 120.7 ms | 273.1 ms | 175.5 ms |
| first-sample-s1.oracle (4 channels, 80.0 channel-seconds) | 543.3 ms | not decoded (not in the development set) | 246.4 ms | — |

On F-drift-s1 the same Python code costs 1.56 times as much on the Windows PC as on the Linux machine,
the port 3.76 times as much; so the port's extra factor on Windows, 3.76 / 1.56 = 2.4, comes with the
build, not the hardware (measured ratios; the owner's reading, that the cost is the code's construction,
is consistent with them). Which part of the build: the exact-math port spends 75% of its time in the
math library on Linux (above), and Task 7 measured `DurationFit::add` at 458 µs per call with MSVC's
scalar exp and log against 221 µs for numpy's vectorized ones on the same PC; that MSVC's exp, log and
log1p are the main cause of the remaining factor is conjectured (the Windows build was not profiled by
function).

## 4. Full-suite run (Task 10)

To be filled by Task 10.
