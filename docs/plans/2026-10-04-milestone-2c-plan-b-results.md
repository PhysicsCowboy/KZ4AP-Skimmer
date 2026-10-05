# Milestone 2c, Plan B: Results Record

Plan: `docs/plans/2026-10-04-milestone-2c-plan-b-stage-2-changes.md`. Reference behavior: the C++ bank
decoder as verified by Plan A (`docs/plans/2026-10-03-milestone-2c-bank-results.md`, code at `a3a96d9`).
Every number here is **measured** unless marked otherwise. Memory is in MB = 10⁶ B and kB = 10³ B;
`/usr/bin/time` reports kibibytes (1024 B), converted here.

**Answer (B1, memory).** Storing the |v_k|² window as 4-byte floats and sharing the duration fit's grid
constants once per configuration change no decoded text: **all 525 development-set channels decode to
the identical final text**, with every decoded record identical. The memory per channel in flight, measured
on the Linux machine, falls from **63.3 MB to 32.1 MB** (−31.1 MB; derived −18.3 MB to −46.0 MB). The CPU
per channel-second is 123.96 ms against the reference's 127.91 ms on the Linux machine (−3.1%, one run each;
not a gate).

**Answer (B2(a), the fit's arithmetic).** Restructuring the duration fit's evaluation cuts the bank
decoder's CPU on the Linux machine from **123.96 ms to 66.67 ms per channel-second** (B1 build to B2(a) after
fix round 1; −47.9% against the reference's 127.91 ms), with **all 525 development-set channels decoding to
the identical final text and every decoded record identical**, after the exact steps (no bit changes), the
near-exact step (one-pass log-sum-exp, which changes the last bits of the per-observation log-likelihood by
at most 1.3 · 10⁻¹⁵ nats) and its fix (the sum started at the largest term). There is no difference to
trace, so the near-exact step is kept. On the Windows PC (F-drift-s1) the cost falls from about 309 ms to
about 77 ms per channel-second (68 ms after the fix, in a later session).

**Answer (B2(b), SLEEF's exp and ln).** Evaluating the duration fit's exp and ln with SLEEF's vectorized
functions (the FMA family, 8 doubles per call on the Linux machine, 4 on the Windows PC, chosen at run time)
cuts the bank decoder's CPU on the Linux machine from **66.67 ms to 42.37 ms per channel-second** (−36.4%;
−66.9% against the reference's 127.91 ms), with **all 525 development-set channels decoding to the identical
final text and every decoded record identical**. The last bits of each per-observation log-likelihood change
by at most 1.8 · 10⁻¹⁵ nats, within a derived bound. On the Windows PC (F-drift-s1) the median of five
alternated runs falls from 74.5 to 66.2 ms per channel-second (−11%; the PC's run-to-run spread is larger). SLEEF's
portable SSE2 functions alone would have made the decoder slower (section 4.1).

**Answer (B3, exact zeros and a stuck noise level).** Exact zeros are now missing data and a stuck noise level
recovers: a stream that starts with 1 s of exact zeros decodes as the station alone (the same text, times shifted
by 1 s within 0.67 ms), where Plan A keyed nothing; a 60 dB rise of the noise is followed within a factor 2 by
12.32 s after it (derived given a warm-up estimate at most 8.4 times too high, a condition measured), a 10 s carrier 40 dB above the noise is forgotten within a factor 2 22 s after it
stops (derived 22.1 s, measured 22.0 s). The development set has no exact zeros and the recovery never fires on
it: **all 525 channels decode to the identical final text with every decoded record identical** (section 5).

**Answer (B4a, time constants in dits).** With the re-key wait 16.7 d_k, the re-key time-out 2.5 times that and the
periodicity windows 41.7, 104 and 208 dits of each candidate (the owner's stage-2 decisions), the bank decoder's
CER on the development set is **worse: paired +0.0277 (+0.0194 to +0.0362)**, with 428 of 525 channels decoding to
a different text; groups A, B, C, F, G, H (oracle) and I have paired intervals entirely above 0, D (speed changes,
10 WPM) improves without a significant interval. The periodicity rule's precision at the 0.03 threshold falls from
0.804 to 0.753 (non-overlapping intervals), all of it in the shortest window. CPU 42.61 → 57.56 ms per channel-second
(Linux machine), memory per channel in flight 32.16 → 33.29 MB. Every Plan A golden test passes unchanged at the
prototype's values set explicitly (section 6). The ablation (6.5) splits the loss: the windows in dits alone
cost +0.0184 (+0.0122 to +0.0251), the re-key settings in dits alone +0.0116 (+0.0042 to +0.0202); the comb's
precision fall comes from the windows alone, whose wrong estimates move to about 3 T and longer, not from a shortest
window too short at fast speeds.

**Answer (B4a-C, one shared periodicity window per row; a variant, not the default).** Judging every candidate of a
row over one window N_w · T̂ (T̂ the selected branch's dit) instead of N_w · T per candidate removes the near-3T₀
errors (0.3% of the shortest window's confident wrong estimates, against 26.3% for B4a and 3.7% for `bank-b3b`) and
restores the comb's precision at 0.03 to 0.795 (0.771 to 0.819; B4a 0.753, `bank-b3b` 0.804), and it removes most
of the windows' cost in C fists (−0.038 paired against `bank-b4a`). Pooled CER against `bank-b3b` stays worse:
**+0.0223 (+0.0119 to +0.0341)** with the re-key settings in dits and **+0.0148 (+0.0072 to +0.0239)** with stage
1's; against `bank-b4a` the pooled change, −0.0054 (−0.0151 to +0.0052), is not established, as it loses in the
first words, H per station, E and A (section 6.6). The default is unchanged (every decoded record of the 525
channels identical to `bank-b4a`).

**Answer (B9, the stretch test and the new-over checks).** The bank decoder is not time-base invariant. Group A's
25 WPM signals stretched to 12 WPM at the same energy per dit (S₅₀₀ 3.19 dB lower) decode worse in all six
variants, every pooled paired CER interval above 0:

| variant | pooled paired CER, stretched − original | CER-0.10 crossing shift (dB of S₅₀₀; invariant +3.19) |
|---|---|---|
| stage 1's settings in seconds | +0.117 | −0.49 |
| B4a | +0.126 | +0.10 |
| re-key settings in dits only | +0.116 | −0.28 |
| windows in dits only | +0.107 | +0.33 |
| B4a-C's shared window | +0.093 | +0.50 |
| shared window with stage 1's re-key settings | +0.079 | +0.53 |

So at 12 WPM the decoder needs about 2.7 to 3.7 dB more energy per dit than at 25 WPM. The variants differ by a
fraction of a dB, and that difference is not established. The shift intervals are separate and nominal bootstraps,
probably too narrow (8.1, 8.2).

Stage 1's new-over rule on the same variants:

- It starts a new over in 15 or 16 of the 16 same-station pauses of 2 to 20 s.
- Group H (oracle): 0.05 to 0.10 false new overs per transmission, which are not late-found turnovers (fix round 1).
  0.23 to 0.31 of the turnovers are missed, mostly at 50 and 100 Hz offsets.
- First words after a turnover are about half wrong: 0.43 to 0.52, an upper bound.

Details in section 8.

## 0. Terms used in this record

- **Decoder**: Envelope, Matched and the bank are the three decoders; here only the bank decoder in C++ is
  run, through `kz4ap-bank-replay` (not through the engine).
- **Test case**: one recording paired with one label file. A decoder is scored on a recording.
- **Oracle channel**: one station's channel stream, opened at its labeled frequency for the whole
  recording (`build/suite/full3/channels/`).
- **Development set**: seed 1 of the oracle test cases without group B's per-row recordings, the regular
  expression `DEV` in `training/kz4ap_proto/experiments.py`: 21 test cases, 525 channels, 74 749.9
  channel-seconds, 509 scored signals.
- **Channel-second**: one second of one channel's stream; the unit of decoding cost.
- **Reference (`bank-b0`)**: the development set replayed at the Plan A code; each Plan B task that must not
  change the text is checked against it.
- **Peak resident set size (RSS)**: the largest physical memory the process held at once ("Maximum resident
  set size" of `/usr/bin/time -v`).
- **Memory per channel in flight**: (RSS with 10 worker threads − RSS with 1) / 9, on a test case of at least
  10 channels of equal length, so that 10 channels are decoded at once in the first run and 1 in the second.
  It counts everything the replay tool holds per channel, not only the decoder.

## 1. Conditions

- **Machine**: the Linux machine: Linux (Ubuntu 22.04), 10-core Intel Xeon (Ice Lake), g++ 11.4;
  10 worker threads (`--jobs 10`) for the development set. The Windows PC: Intel Core i7-12700H, Windows 11,
  MSVC (CPU only).
- **Build**: CMake preset `linux` (Release, exact math), as Plan A.
- **Reference run `bank-b0`**: `kz4ap-bank-replay --out build/suite/full3 --name bank-b0 --only <DEV>
  --jobs 10` at `4cde638` (the Plan A code `a3a96d9` plus a CI file and plan documents; 16.1 min wall).
  `ctest --preset linux` at that commit: 314 passed or skipped, none failed. Checked against Plan A's
  `bank-cpp` (`build/suite/full3/proto/bank-cpp/`) channel by channel:

| quantity | `bank-b0` against `bank-cpp` |
|---|---|
| channels with the identical final text | **525 of 525** |
| channels with identical characters (text, start and end to 1e-4 s) | 525 of 525 |
| channels with every decoded record identical (characters, corrections, selections, periodicity updates with every window's T and score, over starts, switches; as written, 4 or 6 decimals) | 525 of 525 |
| paired CER, all 509 signals | +0.0000 (+0.0000 to +0.0000) |

  So Plan A's later commits (the engine's correction events and the tests) left the bank's output as
  `bank-cpp` recorded it; the pooled CER is therefore `bank-cpp`'s, 0.2799 (Plan A record, section 2.1).
- **B1 run `bank-b1`**: the same command with `--name bank-b1`, on a build of the B1 sources (15.6 min
  wall); `ctest --preset linux` on the same sources: 318 passed or skipped (8 skipped, 1 disabled, as on
  Windows), none failed. `ctest --preset windows`: the same. The smoke check
  (`bash bench/smoke.sh build/windows`) passes unchanged: Envelope CER 0.0353, Matched 0.0436.
- **Raw outputs** (git-ignored, copied back from the Linux machine), in
  `build/suite/full3/experiments/linux/`: `c2-diff-bank-b0-vs-bank-cpp.md`, `compare-bank-b0-vs-bank-cpp.md`,
  `c2-diff-bank-b1-vs-bank-b0.md`, `compare-bank-b1-vs-bank-b0.md` (channel by channel and paired CER;
  the `c2-diff` files also carry the CPU per group), `b1-ref-bank-b0.log`, `b1-new-bank-b1.log` (both runs,
  with the memory measurements), `b1-ctest.linux.log`. Helper scripts git-ignored under `build/`
  (`b1_run.sh`, `b1_new.sh`, `b1_ctest.sh`, `b1_tsan.sh`; `c2_diff.py` from Plan A).

## 2. Memory (B1)

### 2.1 What changed

1. The channel's |v_k|² window (FS²) holds values already rounded to float32 when computed (Plan A, the
   prototype's rounding); it now stores them as 4-byte floats instead of 8-byte doubles. Every read widens
   a value to double, which is exact, so every stage reads the same numbers as before.
2. The duration fit's grid constants (ln μ, 1/μ² and a validity flag per class and grid point, 432.7 kB with
   the defaults) are immutable and now built once per process per configuration: every fit built from a
   configuration with bit-identical values of the 13 fields they read shares them, and they are freed when no
   fit uses them. Before, every fit started afresh (not a copy) built its own. The tables and the retained
   history stay per fit. No arithmetic changed.

### 2.2 Derived and measured

Derived from the arrays the code allocates, per channel at 1500 samples/s (docs/signal-processing.md, section
8c, "Memory"):

| item | before B1 | after B1 |
|---|---|---|
| \|v_k\|² window, 32 branches × 34 721 samples | 8 B each: 8.9 MB | 4 B each: 4.4 MB |
| u window and cumulative-sum ring | 0.56 MB | 0.56 MB |
| per fit | tables 77.6 kB + history 4.6 kB + grid constants 432.7 kB = 0.515 MB | 82.2 kB (grid constants once per process: 432.7 kB) |
| fits, 1 to 3 per branch (32 to 96) | 16.5 MB to 49.4 MB | 2.6 MB to 7.9 MB |
| channel | **26 MB to 59 MB** | **7.6 MB to 12.9 MB** |

Measured on the Linux machine: `/usr/bin/time -v build/b1/<run>-kz4ap-bank-replay --out build/suite/full3
--name <run>-mem<J> --only '^G-ragchew-s1$' --jobs <J>`, J = 1 and 10, with the reference build and the B1
build. G-ragchew-s1 has 12 channels of 366.0 s each (4392.2 channel-seconds).

| build | RSS, 1 thread | RSS, 10 threads | per channel in flight | less the stream copies (17.6 MB) | derived |
|---|---|---|---|---|---|
| reference (`bank-b0`) | 96 076 KiB = 98.4 MB | 652 176 KiB = 667.8 MB | **63.3 MB** | 45.7 MB | 26 MB to 59 MB |
| B1 (`bank-b1`) | 55 992 KiB = 57.3 MB | 338 460 KiB = 346.6 MB | **32.1 MB** | 14.6 MB | 7.6 MB to 12.9 MB |
| difference | −41.0 MB | −321.2 MB | **−31.1 MB** | | −18.3 MB to −46.0 MB |

Besides the decoder, the replay tool holds per channel its recorded stream and the mixed copy, each
549 000 samples of 16 B (2 × 8.8 MB = 17.6 MB, derived from the code: `read_c64` and `baseband` return
complex doubles), its decoded record until the test case's file is written, and the allocator's overhead;
the column "less the stream copies" removes only the first. On both builds the remainder lies inside the
derived range (before) or 1.7 MB above its upper end (after: the decoded record and the allocator's
overhead are not counted in the derived figure; not traced further). The measured reduction, 31.1 MB, lies
inside the derived −18.3 MB to −46.0 MB, which spans one to three fits per branch.

### 2.3 The text check

`bank-b1` against `bank-b0`, channel by channel: **525 of 525 identical final texts**, 525 of 525 identical
character lists, 525 of 525 channels with every decoded record identical (as in section 1's table, the
periodicity windows included: 0 of 875 070 window estimates differ); paired CER +0.0000 (+0.0000 to
+0.0000) on all 509 signals and in every group. There is no difference to trace.

### 2.4 Tests added

- `BankFitShared.FitsOfOneConfigurationShareTheGridConstantsAndOthersDoNot`: two fits of one configuration
  (and a copy) share one constants object (pointer equality through `DurationFit::model()`), a fit with
  `t_grid_step` 0.02 does not, and each decodes its own grid (its T axis bit for bit against
  T_min (1 + step)^i, its table sizes, the fit of machine keying).
- `BankFitShared.EveryFieldTheConstantsReadSeparatesConfigurationsAndNoOtherDoes`: changing any one of the
  13 fields gives its own constants (all 13 kept alive at once, all distinct); changing fields they do not
  read (`correction_reach_s`, `text_window_chars`) does not.
- `BankFitShared.FitsBuiltAndUsedOnEightThreadsAtOnceGiveTheSingleThreadedResultBitForBit`: 8 threads,
  6 rounds each, build fits of the two configurations interleaved, add the same 509 observations (the
  machine-keying case) and compare every bit of the tables, weight, grid maximum and best fit (with and
  without the T_P prior) with fits built on one thread; the fits of the default configuration all share the
  main thread's constants. Also run under ThreadSanitizer on the Linux machine (`-fsanitize=thread`,
  RelWithDebInfo, address-space randomization off with `setarch -R`, these four tests): no report.
- `BankChannel.ThePowerWindowStoresFourByteFloats`: after 24 s of exact zeros at 1500 samples/s the window
  holds 32 × 34 721 values of 4 B each.

### 2.5 CPU (reported, not a gate)

On the Linux machine, from the decoded files' `cpu_s` and `channel_s` (ms of CPU per channel-second, one
run each):

| group | channels | channel-seconds | `bank-b0` | `bank-b1` |
|---|---|---|---|---|
| A sensitivity | 192 | 23 040.0 | 117.08 | 113.45 |
| B fading | 30 | 5 400.3 | 135.25 | 130.40 |
| C fists | 135 | 16 200.0 | 116.43 | 113.00 |
| D speed | 12 | 720.1 | 114.05 | 110.96 |
| E interference | 32 | 1 920.3 | 257.40 | 249.95 |
| F tuning | 28 | 1 440.3 | 138.36 | 133.88 |
| G ragchew | 12 | 4 392.2 | 187.76 | 182.07 |
| H two-station QSO, oracle | 36 | 12 996.1 | 168.79 | 163.59 |
| I Farnsworth | 48 | 8 640.5 | 52.45 | 50.74 |
| all | 525 | 74 749.9 | **127.91** | **123.96** |

Every group is 2.7% to 3.6% lower (conjectured: fewer cache misses and less allocation; not profiled). Plan
A recorded 128.2 ms for `bank-cpp`; `bank-b0` is the same code, 0.2% lower.

On the Windows PC, F-drift-s1 (8 channels, 240.1 channel-seconds), `--jobs 8`, runs alternated between the
reference build (`4cde638`, built in a separate worktree) and the B1 build: reference 302.2 and 315.0 ms,
B1 322.5, 294.4 and 298.7 ms per channel-second; the spread within a build (up to 28 ms) exceeds the
difference between the builds, so no change is measured there. The texts of all five runs are identical.
Plan A's 453.6 ms (Task 7) on the same test case and code was not reproduced in this session (302.2 and
315.0 ms now); the PC's state then is not recorded, so the cause is not known.

## 3. CPU: the fit's arithmetic (B2(a))

### 3.1 What changed

In `engine/src/bank/fit.cpp` (docs/signal-processing.md, section 8c, "Duration fit": "Evaluation"):

1. **Exact steps** (commit `42201d7`; no bit of any result changes, derived from IEEE arithmetic and tested
   bit for bit against a frozen copy of the earlier code): only the classes of an observation's kind are
   evaluated (2 for a mark, 3 for a space; the other kind's −∞ terms changed nothing); logaddexp returns
   max(x, y) without exp and log1p where |x − y| ≥ (58 − k) ln 2 nats, k the binary exponent of the larger
   term, where numpy's formula rounds to max(x, y) exactly; the best-fit search reuses the refinement's terms
   at its start and its result (3 evaluations of the retained history's terms instead of 5).
2. **Near-exact step** (commit `3f99859`, a test bound fixed in `1c6b681`; the summation order changed in
   fix round 1, section 3.8): each observation's total log-likelihood is m + ln(1 + Σ e^(x − m)) over its
   classes and the outlier in one pass (one exp per term other than the largest, one ln) instead of the
   logaddexp chain (one exp and one log1p per term). Since fix round 1 the sum starts at the largest term's
   1 and the others are added after it; terms more than 40 nats below m are left out, and in the grid a class
   whose bound (with σ_ln² for s_c²) lies more than 41 nats below the outlier's term is left out before its
   ln s_c² is computed. Both leave-outs change no bit of the one-pass sum (derived; both tested bit for bit).
   In `3f99859` the sum started at 0 in class order with the outlier last, so a left-out term could have
   shifted a partial sum below 1 and so the rounding of the sum (the review measured 667 of 200 000 random
   cases); the claim "no bit changes" was then false. The one-pass sum itself changes the last bits.

### 3.2 Tests

`engine/tests/bank/fit_test.cpp`, `BankFitB2a.*`, against a frozen copy of the code before B2(a) (at the
full sizes with `KZ4AP_FIT_FULL_SWEEP=1`: 10⁶ random observations per configuration at every grid point,
2000 random fits per configuration and number of refinement steps; edge cases: durations beyond the clamps
and one step beside them, σ_t² = 0 and up to +∞, medians not positive). After the exact steps every
comparison is bit-identical on Windows (1050 s) and on the Linux machine (188 s). Before the exact steps
were applied, the new tests ran on the earlier code (the change set aside with `git stash`) and passed: the
frozen copy reproduces the code it copies. After the near-exact step (as first committed; superseded by
section 3.8, which restored bit-for-bit tests of the exact steps and replaced the fitted bounds below by
derived ones) the differences are measured
(docs/signal-processing.md, 8c, "Bit-for-bit check", table): at most 1.3 · 10⁻¹⁵ nats (Windows) and
8.9 · 10⁻¹⁶ nats (Linux) in the grid's 8.5 · 10⁹ log-likelihood values, 1.8 · 10⁻¹² nats in the weighted
log-likelihood, at most 1.8 · 10⁻¹⁴ in `best`'s fields and 4.7 · 10⁻¹⁴ nats in its quality; no value above
its bound, no acceptance test turned. The golden tests are unchanged and pass; the port's own plain-formula
check (`FastPathsAreBitIdenticalToThePlainFormulas`) has a traced allowance (grid values 2^−50 nats, tables
64 · 2^−50 nats; measured 8.9 · 10⁻¹⁶ and 2.8 · 10⁻¹⁴ nats). `ctest` at `1c6b681`: 321 passed or
skipped on Windows and on the Linux machine (8 skipped, 1 disabled; 322 after fix round 1, section 3.8, which
added a test); at `42201d7` on Windows the same (on the
Linux machine that run's summary line was not kept by the helper's filter; its full sweep passed). Smoke check unchanged: Envelope CER 0.0353, Matched 0.0436.

### 3.3 The text check

| run (Linux machine) | against | identical final text | every decoded record identical | paired CER, 509 signals |
|---|---|---|---|---|
| `bank-b2a-exact` (`42201d7`) | `bank-b0` | **525 of 525** | 525 of 525 (0 of 875 070 periodicity windows differ) | +0.0000 (+0.0000 to +0.0000) |
| `bank-b2a` (`1c6b681`, the near-exact step) | `bank-b2a-exact` | **525 of 525** | 525 of 525 (0 of 875 070) | +0.0000 (+0.0000 to +0.0000) |

There is no difference to trace; the near-exact step passes the rule and is kept (Step 7).

### 3.4 CPU per channel-second

Linux machine, development set, from the decoded files' `cpu_s` and `channel_s` (ms of CPU per
channel-second, one run each; `bank-b0` and `bank-b1` from section 2.5):

| group | channels | channel-seconds | `bank-b0` | `bank-b1` | `bank-b2a-exact` | `bank-b2a` |
|---|---|---|---|---|---|---|
| A sensitivity | 192 | 23 040.0 | 117.08 | 113.45 | 96.27 | 69.27 |
| B fading | 30 | 5 400.3 | 135.25 | 130.40 | 113.48 | 81.35 |
| C fists | 135 | 16 200.0 | 116.43 | 113.00 | 96.29 | 70.01 |
| D speed | 12 | 720.1 | 114.05 | 110.96 | 94.21 | 68.29 |
| E interference | 32 | 1 920.3 | 257.40 | 249.95 | 189.21 | 136.86 |
| F tuning | 28 | 1 440.3 | 138.36 | 133.88 | 114.31 | 81.45 |
| G ragchew | 12 | 4 392.2 | 187.76 | 182.07 | 155.23 | 111.41 |
| H two-station QSO, oracle | 36 | 12 996.1 | 168.79 | 163.59 | 135.59 | 99.17 |
| I Farnsworth | 48 | 8 640.5 | 52.45 | 50.74 | 44.91 | 33.78 |
| all | 525 | 74 749.9 | **127.91** | **123.96** | **104.60** | **75.84** |

Wall time of the replay on 10 threads: 963 s (`bank-b0`), 787 s (`bank-b2a-exact`), 572 s (`bank-b2a`).

Windows PC, F-drift-s1 (8 channels, 240.1 channel-seconds), `--jobs 8`, the three builds alternated in one
session (reference `4cde638` and exact steps `42201d7`, each built in a separate worktree; near-exact step
`3f99859`):

| build | run 1 | run 2 |
|---|---|---|
| reference | 303.3 | 314.4 |
| B2(a) exact steps | 166.0 | 170.5 |
| B2(a) near-exact step | 76.5 | 78.4 |

The run-to-run spread is 11.1 ms (reference), 4.5 ms and 1.9 ms. The texts and every decoded record of all
six runs equal those of the reference's B1 run `b0-win-cpu` on all 8 channels. The Windows build gains more
than the Linux build (−75% against −41%), presumably because MSVC's scalar log1p and exp cost more per call
(conjectured from the larger gain of the steps that remove them; not profiled on Windows).

### 3.5 Profile

gprof, one channel (F-drift-s1, label 1, 30.0 s), Linux machine, the bank sources compiled with `-pg -O3`
and linked statically so that the math library is sampled (Plan A's method; helper `build/b2a/b2a_prof.sh`,
git-ignored). Seconds of samples (10 ms each):

| build | total | log1p | exp | ln | pow | `grid_loglik` | the search's own code | everything else |
|---|---|---|---|---|---|---|---|---|
| B1 (`41b8f5e`) | 3.70 | 1.13 | 1.19 | 0.30 | 0.05 | 0.48 | 0.29 | 0.26 |
| exact steps (`42201d7`) | 3.35 | 1.15 | 0.58 | 0.37 | 0.01 | 0.87 | 0.16 | 0.21 |
| near-exact step (`1c6b681`) | 2.30 | 0 | 0.62 | 0.62 | 0.05 | 0.64 | 0.10 | 0.27 |

("The search's own code": `terms`, `refine`, `weighted_loglik`, `best`, `retained`, `aged_sum`; a flat
profile cannot split libm's time between the grid and the search.) Before B2(a) 72% of the samples were in
libm's exp, log1p, ln and pow; after it 56%. The exact steps removed about half of the exp time (the other
kind's classes and the repeated evaluations in the search) but almost none of log1p's: the skipped
logaddexp calls appear to be the cheap ones, with a tiny log1p argument (conjectured; not measured per
call). The one-pass sum removes log1p altogether. User time of the profiled run: 3.82 s, 3.34 s, 2.31 s.

### 3.6 Carried from the B1 review

- docs/signal-processing.md, 8c, "Memory": the 1.7 MB by which the measured per-channel memory after B1
  exceeds the derived upper end is now stated there, and its cause is marked conjectured, not traced.
- The per-call copy in `BankChannel::p_row` (B1): on the profiled channel it is called 67 times in 30.0 s
  (the re-key paths only) and is attributed 0.00 s of 3.70 s (below the profile's 10 ms resolution, 0.3%).
  It costs no measurable time and is left as it is.

### 3.7 Raw outputs

Git-ignored, in `build/suite/full3/experiments/linux/`: `c2-diff-bank-b2a-exact-vs-bank-b0.md`,
`compare-bank-b2a-exact-vs-bank-b0.md`, `c2-diff-bank-b2a-vs-bank-b2a-exact.md`,
`compare-bank-b2a-vs-bank-b2a-exact.md`, and the job logs `b2a-exact.log`, `b2a-near.log` (the first
near-exact run, stopped by the quality bound before its replay) and `b2a-near2.log`. Profiles:
`build/b2a/prof-b1-flat.txt`, `prof-b2a-exact-flat.txt`, `prof-b2a-flat.txt`. Windows decoded files:
`build/suite/full3/proto/b2a-win-{ref,exact,near}-{1,2}/`; full-sweep outputs `build/b2a/near-sweep-windows*.txt`.

### 3.8 Fix round 1 (review of B2(a))

1. **The 40-nat leave-out.** As first committed (`3f99859`) the one-pass sum started at 0 and added the terms
   in class order with the outlier last; a left-out term added before the largest term's 1 could shift a
   partial sum below 1 and so the sum's rounding (the review measured 667 of 200 000 random cases), so the
   documented "no bit changes" was false. Since `327ffa7` the sum starts at the largest term's 1 and adds
   the others after it: every partial sum is ≥ 1, where the doubles are at least 2^−52 apart, and a left-out
   term is below 4.3 · 10⁻¹⁸ < 2^−53, so it cannot change any partial sum (derived; tested bit for bit by
   `BankFitB2a.LogSumExpLeaveOutChangesNoBit`, 2 · 10⁶ cases with the largest term at every position, the
   outlier included, and terms at, beside and below −40 nats).
2. **Bit-for-bit tests of the exact steps restored.** A second frozen variant, the code before B2(a) with
   its totals by the current one-pass sum, is compared bit for bit with the grid's log-likelihood, `best`
   (with Q), `refine`, the weighted log-likelihood and `class_logliks`; the comparison with the logaddexp
   chain measures only the near-exact difference (the totals, and the weighted log-likelihood at fixed θ).
3. **Derived bounds** replace the fitted 10⁻¹⁴ and 10⁻¹³: each total within 30 · 2^−52 · (|ℓ_total| +
   2 nats) (a rounding count), the weighted log-likelihood within Σ λ^age |Δℓ_total| plus numpy's pairwise
   rounding of both sums and the prior's addition. The plain-formula check
   (`FastPathsAreBitIdenticalToThePlainFormulas`, name kept) allows each grid value the same bound and each
   table entry an accumulated one.
4. Comments made accurate (NaN and +∞ in the one-pass sum; the refinement's kind-only sums are exact for a
   finite θ, which every call from `best` has). The full sweep is the ctest entry `BankFitB2a.FullSweep`
   (label `full-sweep`, excluded by the default test presets; `ctest --preset windows-full-sweep` or
   `linux-full-sweep`).

Tests: `ctest --preset windows` 322 of 322 passed or skipped, `ctest --preset linux` 322 of 322; the
full-sweep presets pass on both (Windows 1195 s, Linux 184 s); smoke check unchanged (Envelope CER 0.0353,
Matched 0.0436). Every bit-for-bit comparison is equal at the full sizes on both platforms. Largest
differences from the logaddexp chain (Windows / Linux), and the share of the derived bound used:

| quantity | values | largest difference | of its bound |
|---|---|---|---|
| grid log-likelihood | 8.5 · 10⁹ | 1.3 · 10⁻¹⁵ / 8.9 · 10⁻¹⁶ nats | 3.5% / 3.4% |
| ℓ_total of `class_logliks` | 1.8 · 10⁷ | 8.9 · 10⁻¹⁶ / 8.9 · 10⁻¹⁶ nats | 3.3% / 3.3% |
| weighted log-likelihood at fixed θ | 4.4 · 10⁵ | 1.8 · 10⁻¹² / 1.8 · 10⁻¹² nats | 99.1% / 99.6% |

The weighted log-likelihood's bound is tight where one observation's difference dominates the sum (its first
term, Σ λ^age |Δℓ_total|, is then the difference itself); it is derived, not fitted. The plain-formula check
on its 414 observations: 654 897 of 2 007 072 grid values differ (Windows), by at most 8.9 · 10⁻¹⁶ nats; 1411
of 9696 table entries, by at most 4.3 · 10⁻¹⁴ nats; the golden tables after 8, 48 and 300 observations differ
from the prototype's in 1980, 1702 and 1517 of 9696 entries (assertions unchanged).

The text check, Linux machine (`bank-b2a2`, `327ffa7`, against `bank-b2a-exact`): **525 of 525 identical
final texts**, 525 of 525 channels with every decoded record identical, 0 of 875 070 periodicity windows
differ, paired CER +0.0000 (+0.0000 to +0.0000). Nothing to trace.

CPU per channel-second, Linux machine (one run; replay wall time 502 s):

| group | `bank-b2a-exact` | `bank-b2a` (`3f99859`) | `bank-b2a2` (`327ffa7`) |
|---|---|---|---|
| A sensitivity | 96.27 | 69.27 | 60.86 |
| B fading | 113.48 | 81.35 | 71.58 |
| C fists | 96.29 | 70.01 | 61.79 |
| D speed | 94.21 | 68.29 | 60.25 |
| E interference | 189.21 | 136.86 | 118.01 |
| F tuning | 114.31 | 81.45 | 71.56 |
| G ragchew | 155.23 | 111.41 | 97.79 |
| H two-station QSO, oracle | 135.59 | 99.17 | 86.81 |
| I Farnsworth | 44.91 | 33.78 | 30.44 |
| all | 104.60 | 75.84 | **66.67** |

The fix is also 12% faster than `3f99859`: the largest term's e^0 is no longer evaluated (the outlier is the
largest term at most grid points far from the observation; conjectured as the main cause, not measured per
term). Profile (as section 3.5): total 1.98 s; log1p 0, exp 0.44 s, ln 0.73 s, pow 0.02 s, `grid_loglik`
0.45 s, the search's own code 0.13 s, everything else 0.21 s. Windows PC, F-drift-s1, two runs of the fix's
build in a later session (not alternated with the reference): 67.0 and 68.6 ms per channel-second; every
decoded record equal to `b0-win-cpu`'s on all 8 channels.

Raw outputs (git-ignored): `build/suite/full3/experiments/linux/c2-diff-bank-b2a2-vs-bank-b2a-exact.md`,
`compare-bank-b2a2-vs-bank-b2a-exact.md`, `b2a-fix1.log`; `build/b2a/prof-b2a2-flat.txt`,
`build/b2a/fix1-full-sweep-windows.txt`; Windows decoded files `build/suite/full3/proto/b2a2-win-near-{1,2}/`.

## 4. CPU: SLEEF's exp and ln in the fit (B2(b))

### 4.1 What changed

In `engine/src/bank/fit.cpp` and the new `engine/src/bank/vecmath*.cpp` (docs/signal-processing.md, section 8c,
"Duration fit": "Evaluation (Plan B task B2(b), exp and log)"; commit `35817fa`):

1. Every exp and ln of the fit (ln s_c² of each class term on the grid and on the retained history, the
   one-pass sum's e^(x − m) and its ln, the refinement's responsibilities e^(ℓ_c − ℓ_total)) is SLEEF 3.9.0's
   vectorized `_u10` function (stated error bound 1.0 ulp of the returned value) instead of the C library's.
   They are evaluated on arrays: the grid's points in blocks of 256, the retained history at once. Per value
   the operations and their order are unchanged.
2. The implementation is chosen once at run time from what the processor and the operating system support:
   SLEEF's "finz" family (with FMA) on AVX-512F (8 doubles per call) or AVX2 + FMA (4), else its "cinz" family
   (no FMA) on AVX-512F, AVX or SSE2 (8, 4, 2). SLEEF states that each family returns the same bits with every
   instruction set (tested on both machines, 4.2). Each instruction set's code is compiled in its own file for
   that set alone (no inline function in those files that the linker could share; checked with `nm` on the
   Linux build: only the wrappers' own symbols), and each runs only where the processor reports what its file
   may use, so the binaries run on every x86-64 processor: by construction and by the symbol check, not by a
   run on a processor without AVX. (Fix round 1: on MSVC the AVX-512 file is now compiled as SLEEF compiles its
   own, AVX2 code generation with AVX-512F intrinsics, and the AVX2 path also requires BMI1 and BMI2, which
   MSVC's /arch:AVX2 may emit; section 4.9.)
3. SLEEF by `FetchContent` at the 3.9.0 release (SHA-256 pinned), the static math library only (no tests,
   DFT, quad, GNU ABI or scalar libraries, no TLFloat), in both presets.

**Why a run-time choice, not the brief's "dispatcher or SSE2" alone (measured).** Per value, on arrays of
4096 (`build/b2b/microbench.cpp`, ns per value):

| function | Linux machine (g++ 11, Ice Lake) | Windows PC (MSVC, Alder Lake) |
|---|---|---|
| C library exp / ln | 4.6 / 4.6 | 3.6 / 3.6 |
| cinz SSE2 (2 doubles) exp / ln | 4.7 / 10.6 | 3.6 / 8.5 |
| SLEEF's 2-double dispatcher (`Sleef_expd2_u10`, AVX2 + FMA on 128 bits) exp / ln | 3.4 / 6.6 | 2.7 / not measured cleanly |
| cinz AVX (4) exp / ln | 2.4 / 5.8 | 1.9 / not measured cleanly |
| finz AVX2 (4) exp / ln | 1.9 / 3.5 | 1.3 / not measured cleanly |
| cinz AVX-512 (8) exp / ln | 1.3 / 2.0 | not available |
| finz AVX-512 (8) exp / ln | 1.0 / 1.3 | not available |

(On the Windows PC the benchmark's ln figures after the first AVX call were inflated by mixing SSE and AVX code
in one test program without `vzeroupper`; the fit's own build ends every AVX wrapper with it.) SLEEF's
accurate ln is 2.3 times the C library's in SSE2, and its 4-double dispatcher (`Sleef_expd4_u10`) needs AVX,
so it cannot be the fallback. On one channel (F-drift-s1, label 1, 30.0 s; Linux machine; the fit compiled
with the variant's instruction set; user seconds, 3 runs each, identical decoded output in every variant):

| variant | user s |
|---|---|
| B2(a) code, C library | 2.05 |
| B2(b) blocks, C library | 1.77 |
| B2(b), cinz SSE2 | 2.52 |
| B2(b), cinz AVX | 1.82 |
| B2(b), finz AVX2 | 1.52 |
| B2(b), cinz AVX-512 | 1.30 |
| B2(b), finz AVX-512 | 1.22 |

On the Windows PC (F-drift-s1, 8 channels; alternated with the B2(a) build in one session): cinz SSE2 89.1 ms
against 68.6 ms; cinz AVX 72.6 and 76.0 against 71.2 and 74.4 ms; finz AVX2 in 4.4. So the portable cinz SSE2
alone would have made the decoder slower on both machines, cinz AVX helps only on the Linux machine, and the
FMA functions help on both. The blocks alone, with the C library's functions, already save 14% on that channel
(no bit changes; not adopted separately, conjectured cause: fewer branches and better pipelining of
independent calls).

**What the run-time choice costs in reproducibility.** Within a family the fit's values do not depend on the
processor (SLEEF's statement, tested on both machines); the two families differ in the last bits, so a
processor without AVX2 and FMA (Intel Core before Haswell, 2013, AMD before Excavator, 2015, and some later
Pentium, Celeron and Atom-class processors) computes other last bits than the machines measured here (the cinz
family's development-set check: section 4.9). Before B2(b) the fit already depended on the platform's C library (glibc and Microsoft's differ
in the last bits); with B2(b) the Windows PC and the Linux machine use the same functions (finz) for the fit.

### 4.2 Tests

- `BankFitB2b.ExpAndLogAreWithinOneUlpOfTheCLibraryAndConsistentWithinEachFamily`: 10⁶ arguments for exp and
  10⁶ for ln (the one-pass sum's r in [−40, 0] nats and the leave-out's edge, the responsibilities down to
  subnormal results and 0, overflow, subnormal and edge arguments, ±0, ±∞, NaN), every implementation the
  processor can run: at most 1 ulp from `std::exp` / `std::log`, every element bit for bit as when evaluated
  alone (the padded last vector), every implementation of a family bit for bit the same, the fit's exp and ln
  the selected implementation's. Selected: finz AVX-512 on the Linux machine, finz AVX2 on the Windows PC.

| | exp: differ from the C library (1 ulp) | ln: differ (1 ulp) | families checked |
|---|---|---|---|
| Linux machine (glibc) | finz 37 994, cinz 60 106 | 5118 (both) | finz AVX-512 = AVX2; cinz AVX-512 = AVX = SSE2 |
| Windows PC (Microsoft) | finz 36 779, cinz 57 577 | finz 5227, cinz 5230 | cinz AVX = SSE2 (no AVX-512) |

- The B2(a) tests' frozen copy calls either the C library's exp and ln (the code at B2(a)) or the fit's. Against
  the latter every comparison is bit for bit; against the former each ℓ_total within the derived bound
  1.001 · 2^−52 · (5 |ℓ_total| + 14.3 + |ln σ_ln²|) nats (derivation in docs/signal-processing.md, 8c, and in
  `fit_test.cpp`, `sleef_bound`), the weighted log-likelihood within the bound from those totals' differences;
  against the logaddexp chain within the sum of the B2(a) and B2(b) bounds. The tests were written first: on
  the B2(a) code the three bit-for-bit comparisons with the fit's exp and ln failed (e.g. 259 216 of
  96 753 960 grid values on the default grids, 1.8 · 10⁻¹⁵ nats apart) and the comparisons with the C
  library's passed with differences 0; after the change all passed.
- Full sweeps (`ctest --preset linux-full-sweep` / `windows-full-sweep`): every bit-for-bit comparison equal;
  largest differences from the B2(a) code (Linux / Windows), share of the derived bound:

| quantity | values | largest difference from the B2(a) code | of its bound |
|---|---|---|---|
| grid log-likelihood | 8.5 · 10⁹ | 1.8 · 10⁻¹⁵ / 1.8 · 10⁻¹⁵ nats | 21.7% / 21.7% |
| ℓ_total of `class_logliks` | 1.8 · 10⁷ | 1.8 · 10⁻¹⁵ / 1.8 · 10⁻¹⁵ nats | 20.0% / 19.9% |
| weighted log-likelihood at fixed θ | 4.4 · 10⁵ | 4.6 · 10⁻¹³ / 1.1 · 10⁻¹³ nats | 31.9% / 40.5% |

  Against the logaddexp chain (both steps): grid 2.2 · 10⁻¹⁵ nats (4.8% of the summed bound), weighted
  log-likelihood up to 99.6% / 99.1% of its bound (Linux / Windows), as in section 3.8. Wall time of the full
  sweep: 336 s on the Linux machine, 2477 s on the Windows PC (the frozen copy now runs three variants, and its
  SLEEF variant calls SLEEF one value at a time).
- The golden tests are unchanged and pass. Their printouts on Windows: the tables after 8, 48 and 300
  observations differ from the prototype's in 2005, 1712 and 1523 of 9696 entries (1980, 1702 and 1517 after
  B2(a)), by at most 7.1 · 10⁻¹⁵, 2.8 · 10⁻¹⁴ and 4.3 · 10⁻¹⁴ nats. The plain-formula check
  (`FastPathsAreBitIdenticalToThePlainFormulas`) allows each grid value its B2(a) bound plus the B2(b) bound:
  657 286 of 2 007 072 values differ, by at most 1.8 · 10⁻¹⁵ nats; 1410 of 9696 table entries, by at most
  4.3 · 10⁻¹⁴ nats.
- `ctest --preset windows` and `ctest --preset linux`: 323 of 323 passed or skipped (8 skipped, 1 disabled).
  Smoke check unchanged: Envelope CER 0.0353, Matched 0.0436.

### 4.3 The text check

`bank-b2b` (`35817fa`, Linux machine, finz AVX-512) against `bank-b2a2`: **525 of 525 identical final
texts**, 525 of 525 identical character lists, 525 of 525 channels with every decoded record identical
(0 of 875 070 periodicity windows differ), paired CER +0.0000 (+0.0000 to +0.0000) on all 509 signals and in
every group. There is no difference to trace; B2(b) passes the rule and is kept (Step 7).

### 4.4 CPU per channel-second

Linux machine, development set (ms of CPU per channel-second, one run each):

| group | channels | channel-seconds | `bank-b0` | `bank-b2a2` | `bank-b2b` | change |
|---|---|---|---|---|---|---|
| A sensitivity | 192 | 23 040.0 | 117.08 | 60.86 | 37.98 | −37.6% |
| B fading | 30 | 5 400.3 | 135.25 | 71.58 | 45.34 | −36.7% |
| C fists | 135 | 16 200.0 | 116.43 | 61.79 | 38.95 | −37.0% |
| D speed | 12 | 720.1 | 114.05 | 60.25 | 38.27 | −36.5% |
| E interference | 32 | 1 920.3 | 257.40 | 118.01 | 80.55 | −31.7% |
| F tuning | 28 | 1 440.3 | 138.36 | 71.56 | 44.35 | −38.0% |
| G ragchew | 12 | 4 392.2 | 187.76 | 97.79 | 60.15 | −38.5% |
| H two-station QSO, oracle | 36 | 12 996.1 | 168.79 | 86.81 | 55.81 | −35.7% |
| I Farnsworth | 48 | 8 640.5 | 52.45 | 30.44 | 20.86 | −31.5% |
| all | 525 | 74 749.9 | **127.91** | **66.67** | **42.37** | **−36.4%** |

Replay wall time on 10 threads: 502 s (`bank-b2a2`), 319 s (`bank-b2b`). Against the reference `bank-b0` the
bank decoder now costs 33% of its CPU (−66.9%).

Windows PC, F-drift-s1 (8 channels, 240.1 channel-seconds), `--jobs 8`, the B2(a) build (`00f17f6`, a separate
worktree) and the B2(b) build alternated in one session, ms per channel-second:

| build | runs (in the order run) | median |
|---|---|---|
| B2(a) | 68.7, 72.3, 74.5, 96.6, 97.5 | 74.5 |
| B2(b) (finz AVX2) | 63.5, 88.3, 66.2, 66.1, 69.4 | 66.2 |

Two sessions of alternated pairs (2 + 3 rounds). The PC's spread within a build (up to 29 ms) is larger than
the difference between the builds, so single runs do not order the builds; the medians differ by −11%, and 4 of
the 5 pairs favor B2(b). An earlier session during development gave 58.3 and 61.8 ms (B2(b)) against 68.6 and
69.8 ms. The texts and every decoded record of the B2(b) runs equal the B2(a) runs' on all 8 channels
(`c2_diff.py`, run 1 of each; 0 of 2808 periodicity windows differ).

### 4.5 Profile

gprof, one channel (F-drift-s1, label 1, 30.0 s), Linux machine, as section 3.5 (helper `build/b2b/b2b_prof.sh`;
the vecmath files come from the library build, not compiled with `-pg`, and are sampled). Seconds of samples:

| build | total | C library ln | C library exp | pow | SLEEF ln (with its wrapper) | SLEEF exp (with its wrappers) | `grid_loglik` | the search's own code | everything else |
|---|---|---|---|---|---|---|---|---|---|
| B2(a) (`00f17f6`, at the start of B2(b)) | 2.04 | 0.75 | 0.42 | 0.03 | — | — | 0.49 | 0.07 | 0.28 |
| B2(b) (`35817fa`) | 1.27 | 0.10 | 0 | 0.01 | 0.15 | 0.08 | 0.58 | 0.10 | 0.25 |

The math library's share falls from 59% to 27% (SLEEF 18%, the C library 9%: the remaining ln outside the
fit's vectorized paths, e.g. the retained history's ln d, ln μ_c of the search points and the other stages).
`grid_loglik`'s own time rises from 0.49 to 0.58 s: it now contains the inlined one-pass sum and the staging
of the blocks; it is the largest single item (46%). User time of the profiled run: 2.07 s and 1.33 s.

### 4.6 Build cost

From scratch (new build folder, FetchContent downloads included), configure and build, seconds:

| platform | B2(a) (`00f17f6`) configure / build | B2(b) configure / build | added |
|---|---|---|---|
| Linux machine (Ninja, 10 jobs) | 2.3 / 21.6 (64 steps) | 6.9 / 24.6 (170 steps) | +4.6 / +3.0 |
| Windows PC (Visual Studio generator, Release) | 14.5 / 512.8 | 43.6 / 553.2 | +29.1 / +40.4 |

CI (GitHub Actions) was not run: it needs a push, which needs the owner's approval. The same presets build
SLEEF without manual steps on both machines here (MSVC from Visual Studio Build Tools 2026 and g++ 11.4); the
runners use other compiler versions (Windows: the runner image's Visual Studio; Ubuntu 24.04: g++ 13), not
tried.

### 4.7 Carried from the B2(a) re-review

The Windows full-sweep time was stated three ways (15 to 20 min in `engine/CMakeLists.txt`, about 25 min in
docs/signal-processing.md, about 20 min in `fit_test.cpp`); all three now state the B2(b) measurement
(about 41 min on the Windows PC) and the Linux machine's (about 6 min). Section 3.2's test count (321, correct at `1c6b681`) now notes the
322 after fix round 1.

### 4.8 Raw outputs

Git-ignored, in `build/suite/full3/experiments/linux/`: `c2-diff-bank-b2b-vs-bank-b2a2.md`,
`compare-bank-b2b-vs-bank-b2a2.md`, `b2b-run.log` (build, ctest, the implementations test, the full sweep, the
replay, the scoring, the profile), `b2b-variants.log` (the per-variant timing), `prof-b2a2-start-flat.txt`,
`prof-bank-b2b-flat.txt`. Under `build/b2b/`: the helper scripts (`b2b_run.sh`, `b2b_prof.sh`,
`b2b_variants.sh`, `b2b_buildtime.sh`, `win_cpu.sh`), `microbench.cpp`, `full-sweep-windows.txt`. Windows
decoded files: `build/suite/full3/proto/win-*/`.

### 4.9 Fix round 1 (review of B2(b))

1. **The cinz family at fit level.** The environment variable `KZ4AP_FIT_MATH` (test only, read once;
   `engine/src/bank/vecmath.cpp`) restricts the run-time choice to a family or an implementation. With
   `KZ4AP_FIT_MATH=cinz` on the Linux machine (cinz AVX-512 selected; the cinz family gives the same bits with
   SSE2 and AVX, 4.2, so this is also what a processor without FMA computes):
   - the full sweep passes (352 s): every bit-for-bit comparison equal; largest differences from the B2(a)
     code 1.8 · 10⁻¹⁵ nats in the grid (21.7% of the bound), 1.8 · 10⁻¹⁵ nats in `class_logliks`' ℓ_total
     (20.0%), 4.6 · 10⁻¹³ nats in the weighted log-likelihood (31.9%);
   - the development set, `bank-b2b-cinz` against `bank-b2a2`: **525 of 525 identical final texts**, 525 of
     525 channels with every decoded record identical, 0 of 875 070 periodicity windows differ, paired CER
     +0.0000 (+0.0000 to +0.0000). Nothing to trace.
   - CPU per channel-second (one run; replay wall time 346 s):

| group | `bank-b2a2` | `bank-b2b` (finz AVX-512) | `bank-b2b-cinz` (cinz AVX-512) |
|---|---|---|---|
| A sensitivity | 60.86 | 37.98 | 41.27 |
| B fading | 71.58 | 45.34 | 49.09 |
| C fists | 61.79 | 38.95 | 42.18 |
| D speed | 60.25 | 38.27 | 41.57 |
| E interference | 118.01 | 80.55 | 86.97 |
| F tuning | 71.56 | 44.35 | 48.52 |
| G ragchew | 97.79 | 60.15 | 65.52 |
| H two-station QSO, oracle | 86.81 | 55.81 | 60.40 |
| I Farnsworth | 30.44 | 20.86 | 22.18 |
| all | 66.67 | **42.37** | **45.90** |

   With Windows default test sizes and `KZ4AP_FIT_MATH=cinz` (cinz AVX selected) all 30 fit tests pass.
2. **Detection matches what each file may use.** MSVC's `/arch:AVX512` would allow AVX-512 CD, BW, DQ and VL
   instructions, which `detect()` did not check; the MSVC AVX-512 file is now compiled as SLEEF compiles its own
   AVX-512 code with MSVC (`/arch:AVX2` with `__AVX512F__` defined: AVX2 code generation, AVX-512F intrinsics),
   and AVX-512 is used only with AVX2 + FMA present. MSVC's `/arch:AVX2` may also emit BMI1 and BMI2
   instructions, so the MSVC path now requires them for AVX2 (leaf 7 EBX bits 3 and 8). GCC's files use exactly
   `-mavx`, `-mavx2 -mfma` and `-mavx512f`, which are checked.
3. `log_sum_exp` throws `std::invalid_argument` for more than 255 class terms (the block evaluation counts a
   point's terms in 8 bits); test `BankFitB2b.LogSumExpTakesAtMost255ClassTerms`.
4. Wording: processor generations ("most Intel Core processors since Haswell, 2013, and AMD since Excavator,
   2015; not every Pentium, Celeron or Atom-class one"); the portability claim now says "by construction and by
   the symbol check, not by a run on a processor without AVX" (Intel SDE not run); stack scratch in bytes
   (25 856 B; 4608 B per search vector); the test's shares of exps and lns that differ from the C library's are
   of its own argument mix (finz 3.7%, cinz 5.8% Windows / 6.0% Linux, lns 0.5%); the parameter table's
   classification names the assumption that the C library's exp and ln are within 1 ulp; the test file's
   section header no longer names only the SSE2 functions; README notes that SLEEF sets `CMAKE_BUILD_TYPE` to
   Release in the cache when a single-configuration generator is configured without one.

Tests: `ctest --preset windows` 324 of 324 and `ctest --preset linux` 324 of 324 passed or skipped (8
skipped, 1 disabled); smoke unchanged (Envelope CER 0.0353, Matched 0.0436). Raw outputs (git-ignored):
`build/suite/full3/experiments/linux/c2-diff-bank-b2b-cinz-vs-bank-b2a2.md`,
`compare-bank-b2b-cinz-vs-bank-b2a2.md`, `b2b-cinz.log`.

## 5. Exact zeros and a stuck noise level (B3)

### 5.1 What changed

Two rules in the bank decoder's noise estimate (`docs/signal-processing.md` section 8c, "Noise", "Exact zeros",
"Recovery of a stuck level"; "Channel decoder", "Exact zeros"):

1. **Exact zeros are missing data** (derived: a receiver's output carries noise). A block of input samples all
   exactly 0 FS updates nothing in the three-tap estimate (warm-up, level, W, the recovery's count), and after
   fix round 1 no exact-zero sample does, in a partly zero block too; the warm-up runs on the first 0.32 s of
   non-zero input samples; a tap counts from 2N_k samples after the sample following the latest exact zero. In the spectrum
   an all-zero segment is not offered and each exact-zero sample is left out of the mask as a flagged sample is
   (per sample: stricter than the plan's per-segment wording, accepted by the controller). Until the first
   non-zero block σ² is unknown, and the channel keys, observes and publishes nothing and keeps its clocks at
   its start state.
2. **Recovery of a stuck level** (heuristic): when branch 1 has accepted no tap for `noise_stuck_s` = 8 s (4 τ_n)
   of non-zero input, every branch's level is set again by the warm-up rule over its last 0.32 s of non-zero
   input. The brief's first form was per branch; it fired on the noise golden stream in the "branch" fallback
   (the long branches see no noise during continuous 25 words/min keying: a tap of branch 32 needs 0.552 s free
   of the station, a word space is 0.336 s) and set those branches to the station's power, 3 to 4 orders of
   magnitude too high, moving `BankNoise.BranchFallbackMatchesPrototype`. The controller ruled option (A): gate
   on branch 1, which sees noise in every character space up to 100 words/min (a tap spans 3N_1 = 42 samples,
   28.0 ms, a character space at 100 words/min is 54 samples, 36 ms; derived for rectangular keying, with a
   12-sample margin that keying shape and filter tails reduce; measured: no firing on the development set). With the gate that golden passes unchanged.

`noise_stuck_s` is a new `BankConfig` field (Plan B, not in the prototype); it is now in the bench's configuration
list, so decoded files record it and `--set noise_stuck_s=...` works. The replay tool writes two diagnostics per
channel: `noise_recoveries` and `noise_zero_blocks`.

### 5.2 Golden tests

All golden comparisons pass unchanged except the `zero_pad` stream's (1 s of exact zeros, then a 25 words/min
station): its golden result was the prototype's exact-zero defect (it keyed nothing). The plan's statement that
the golden streams have no exact zeros was wrong for this one. By the controller's ruling it left the
`ChannelGolden` and bench `BankJsonResult` lists; `BankChannel.LeadingExactZerosDecodeAsTheStationAlone` replaces
it (and Plan A's `ExactZerosAtTheStartPublishNoNaN`) with a stricter requirement: the text decoded from the stream
without its zeros, characters' times + 1 s within one block (measured 0.67 ms), no NaN published.

### 5.3 Tests added (`engine/tests/bank/noise_test.cpp`, `channel_test.cpp`, `config_test.cpp`)

| test | requirement | measured (Windows) |
|---|---|---|
| `ExactZerosThenNoiseStayFiniteAndPositive` (Plan A's, enabled) | 5 s zeros then noise: unknown before, finite after, > 0 from 0.32 s after the first non-zero input; within a factor 2 at the end | passes; 234 zero blocks |
| `AGapOfExactZerosLeavesTheEstimateUnchanged` | 5 s gap inside noise: σ² bit for bit constant across the gap | passes |
| `ANoiseRiseOf60dBRecoversByTheDerivedTime` | within a factor 2 from the rise + 12.32 s (derived given a warm-up estimate at most 8.4 times too high; that condition measured) | recovery at 8.011 s; "branch" branch 32 restarts at 2.14; passes |
| `ALongCarrierRecoversByTheDerivedTime` | 10 s carrier 40 dB above branch 1's noise: "branch" and branch 1 within a factor 2 by the derived t_k (22.1 s for branch 1, 28.0 s for branch 32) + 0.25 s; the spectrum methods' branches 2 to 32 by 28.3 s, a **measured margin, not derived** | 44.2 dB at the carrier's end; 1.96 at 22.0 s; spectrum methods' branches 2 to 32 at 1.02 to 1.27 from 28.3 s; passes |
| `ZerosEndingInsideABlockLeaveTheWarmUpToTheNoise` (fix round 1) | 1500 zeros ending inside a block: the three-tap estimate equals the noise alone's bit for bit, above the floor | passes |
| `AKeyedStationDoesNotTriggerTheRecovery` | no recovery on the noise golden stream, any method | passes |
| `LeadingExactZerosDecodeAsTheStationAlone` | section 5.2 | 0.67 ms |
| `BankConfig.PlanBDefaults` | noise_stuck_s = 8 s = 4 τ_n | passes |

`BankNoise.ExactZerosThenNoiseAsThePrototype`, which pinned the defect, is removed. Observation (not B3): in the
"branch" fallback, branch k = 25 (index 24) reads 0.34 to 0.47 of white noise's σ²_v,k for the whole 10 s of the
gap test's streams with or without the gap (seeds 21, 22): the three-tap estimate's slow rise from a low warm-up.

### 5.4 The text check

A probe before the ruling (`bank-b3-probe`: the per-branch rule, which for the default "spectrum" method is the
same code as the gated rule, since that method has only branch 1's three-tap estimate) against `bank-b2b`:
**525 of 525 identical final texts and decoded records**, 0 recoveries, 0 blocks of exact zeros.
`bank-b3`, from the commit: see 5.5.

### 5.5 `bank-b3` (`856fc0d`, Linux machine)

Against `bank-b2b`: **525 of 525 identical final texts**, 525 of 525 identical character lists, 525 of 525
channels with every decoded record identical (0 of 875 070 periodicity windows differ); paired CER +0.0000
(+0.0000 to +0.0000) on all 509 signals. Noise diagnostics over all 525 channels: **0 recoveries, 0 blocks of
exact zeros**; every decoded file records `noise_stuck_s` = 8.0 s. CPU 42.49 ms per channel-second pooled (one run;
`bank-b2b` 42.37; not a gate). Tests: `ctest --preset windows` 327 of 327 and `ctest --preset linux` 327 of 327
passed or skipped; smoke unchanged (Envelope CER 0.0353, Matched 0.0436). Raw outputs (git-ignored):
`build/suite/full3/experiments/c2-diff-bank-b3-vs-bank-b2b.md`, `compare-bank-b3-vs-bank-b2b.md`.

### 5.6 Fix round 1 (review of B3)

The review found that a partly zero block (a run of zeros starting or ending inside a block) fed its exact-zero
|v_k|² into the three-tap warm-up, so its 20% quantile was 0 and σ² sat at the 10⁻²⁰ FS² floor for the first
blocks of noise (4 blocks, 85 ms, in the zero_pad stream; derived by the reviewer), and that §8c said otherwise.
Fixed in the code (`9744b5e`): exact-zero samples enter neither the warm-up nor the recovery's history or count,
and a tap counts from 2N_k samples after the sample following the latest exact zero. New test
`ZerosEndingInsideABlockLeaveTheWarmUpToTheNoise` (bit for bit against the noise alone, above the floor); test (ii)
now also checks the gap's edge blocks bit for bit; test (i) checks σ² above the floor from the first non-zero
block. The spectrum methods' factor 2 in test (iv) for branches 2 to 32 is now labeled a measured margin (section
5.3); test (iii)'s time is derived given a measured condition; branch 1's gate is stated as 3N_1 = 42 samples
(42.9 words/min for element spaces) with the rectangular-keying assumption.

`bank-b3b` (`9744b5e`, Linux machine) against `bank-b2b`: **525 of 525 identical final texts and decoded records**,
paired CER +0.0000 (+0.0000 to +0.0000) on 509 signals; 0 recoveries, 0 blocks of exact zeros; CPU 42.61 ms per
channel-second (one run; not a gate). `ctest --preset windows` 328 of 328 and `ctest --preset linux` 328 of 328
passed or skipped; smoke unchanged (Envelope CER 0.0353, Matched 0.0436).

## 6. Time constants in dits (B4a)

### 6.1 What changed

Stage-2 spec section 3.1 (owner, 2026-10-03); `docs/signal-processing.md` section 8c, "Time constants in dits".
Within branch k a dit is the branch's nominal dit d_k = L_k / 0.8 (12 ms at k = 1, 50.13 ms at k = 16, 230.3 ms
at k = 32). Code: `fde71a9`.

| Setting | Before (Plan A, stage 1) | B4a | Class | Status |
|---|---|---|---|---|
| Re-key wait W_min,k | 0.8 s of keyed time, every branch | 16.7 d_k: 200.4 ms (k = 1), 837.1 ms (k = 16), 3.847 s (k = 32) | (1) dits | derived from E9b's 0.8 s, measured at 25 WPM |
| Seed memory | 4 × 0.8 s = 4800 samples | 4 W_min,k: 1202 to 23 079 samples | follows W_min,k | heuristic |
| Re-key time-out | 2 s of channel time, every branch | 2.5 W_min,k = 41.75 d_k: 501 ms, 2.093 s, 9.616 s | (1) dits | heuristic (stage 1's ratio 2 s / 0.8 s) |
| Periodicity windows | 2, 5 and 10 s, every candidate | N_w · T per candidate T, N_w = 41.7, 104, 208: 0.50 to 2.50 s at 12 ms, 10.1 to 50.4 s at 242.2 ms | (1) dits | placeholder (stage 1's at 48 ms) |
| Comb threshold | 0.03 | 0.03, kept (6.4) | — | placeholder |

At 25 WPM the nearest branch (k = 16) has W_min = 837.1 ms and time-out 2.093 s, 4.6% above stage 1's values: the
ladder's granularity (derived bound 5.1%). A window per candidate is computed by sliding per-band lagged-product
sums (no FFT per candidate); every candidate's score equals the FFT comb on its own window to 1.3 · 10⁻¹³ absolute
(measured, Windows build). The prototype's values in seconds remain reachable (`fixed_timing`), and the module and
channel golden tests set them explicitly: **every Plan A golden test passes unchanged; none moved**
(`BankKeying.KeyerMatchesPrototypeThroughRun`, `BankPeriodicity.CombMatchesPrototypeThroughRun` and the Python-port
periodicity tests, the 15 `ChannelGolden` streams, the near-tie test, `FromIndexCountsTheKeptCharacters`, the bench's
`BankJsonResult`, and `BankDecoder.TheConsumersListIsTheBanksAfterEveryUpdateWhenCharactersOverlapInTime`, whose
stream was found at those values).

Tests added (13): W_min,k, seed memory and time-out in samples at k = 1, 16, 32; the 25 WPM bound; readiness per
branch; the window N_w T and the reach per candidate; every candidate's score against the FFT comb on its own
window (with a NaN in the stream); the shortest confident window; window rows of a wrong size; split invariance at
the default timing (3 streams); a 12 WPM station (branch 23) re-keyed after 1.643 s of keyed time at 2.752 s
(W_min,23 = 1.631 s) where stage 1's timing re-keys at 1.387 s after 0.811 s; the exact-zero test at the default
timing. `ctest --preset windows` 341 of 341 and `ctest --preset linux` 341 of 341 passed or skipped; Python tests
259 passed, 7 expected failures; smoke unchanged (Envelope CER 0.0353, Matched 0.0436).

### 6.2 `bank-b4a` against `bank-b3b` (Linux machine, the development set)

Texts: **428 of 525 channels decode to a different final text** (97 identical; 47 with identical character lists).
Paired CER (variant − reference, signal by signal; bootstrap 95% over signals):

| group | signals | paired CER | paired first-word CER |
|---|---|---|---|
| all | 509 | **+0.0277 (+0.0194 to +0.0362)** | +0.2684 (+0.1356 to +0.4110) |
| A sensitivity | 192 | **+0.0153 (+0.0066 to +0.0245)** | +0.3932 (+0.1503 to +0.6901) |
| B fading | 30 | **+0.0350 (+0.0063 to +0.0704)** | +0.2886 (−0.0239 to +0.6518) |
| C fists | 135 | **+0.0438 (+0.0303 to +0.0594)** | +0.0523 (−0.0806 to +0.1858) |
| D speed | 12 | −0.0447 (−0.1138 to +0.0122) | −0.1667 (−0.4177 to +0.0833) |
| E interference | 16 | −0.0105 (−0.0723 to +0.0411) | +0.2458 (−0.9501 to +1.1834) |
| F tuning | 28 | **+0.0618 (+0.0227 to +0.1019)** | +1.5196 (+0.4107 to +2.7947) |
| G ragchew | 12 | **+0.0084 (+0.0011 to +0.0193)** | +0.0287 (−0.0259 to +0.1082) |
| H two-station QSO, oracle | 12 | **+0.0302 (+0.0022 to +0.0703)** | +0.0521 (−0.0160 to +0.1363) |
| H two-station QSO, oracle (per station) | 24 | +0.0204 (−0.0324 to +0.0785) | +0.1748 (−0.3038 to +0.7199) |
| I Farnsworth | 48 | **+0.0468 (+0.0039 to +0.0946)** | −0.0882 (−0.2351 to +0.0580) |

**Groups made worse (paired interval entirely above 0), reported to the owner (the plan's rule; not a gate):**
A, B, C, F, G, H (oracle) and I, and the pooled figure. Their regimes (CER of the test case, `bank-b3b` → `bank-b4a`):

- A: 12 WPM 0.313 → 0.344 (first-word CER 0.39 → 1.28), 25 WPM 0.297 → 0.312, 40 WPM 0.328 → 0.328.
- B: the mixed-style fading recording 0.579 → 0.607.
- C: every keying style, worst on hand (0.188–0.207 → 0.277–0.358), bug ±0.1 (0.046–0.064 → 0.097–0.123) and
  machine imbalance +0.0 (0.002 → 0.079, interval to 0.217: a few channels badly wrong).
- F: offsets at 20 and 25 WPM (for example 5.9 Hz 25 WPM 0.064 → 0.199, 8.8 Hz 25 WPM 0.057 → 0.202) and drift
  0.2 Hz/s (0.028 → 0.157).
- G: the 25 WPM ragchew 0.046 → 0.055.
- H (oracle): same-track offset 0 Hz 0.120 → 0.225; ambiguous offset 50 Hz 0.484 → 0.535.
- I: 18/5 WPM paddle 0.288 → 0.496; 25/18 and 25/13 machine 0.020 → 0.061 and 0.014 → 0.039; 18/10 paddle improved
  0.078 → 0.058.

Better: D speed 10 WPM 0.291 → 0.000 and step 35 → 20 WPM 0.060 → 0.018 (D's pooled interval includes 0).
Attribution: this comparison changes both parts at once, and the re-key part is not small at any speed, 25 WPM
included. The periodicity estimate's input is branch 1's squelched posterior p, which depends on branch 1's
amplitude, and branch 1's W_min went from 0.8 s to 200.4 ms, its time-out from 2 s to 501 ms and its seed memory from
4800 to 1202 samples, at every station speed; the branches next to a station's own also changed by up to the ladder
step. (An earlier version of this section attributed the losses at 25 WPM to the windows because "W_min and the
time-out are nearly stage 1's there"; that premise was wrong, as the review of B4a found.) The ablation in 6.5
separates the two parts; its result is there.

### 6.3 CPU and memory

CPU per channel-second (Linux machine, one run each; not a gate): pooled **42.61 → 57.56 ms** (+14.95 ms, +35%);
per group A 38.22 → 53.21, B 45.46 → 62.26, C 39.18 → 52.69, D 38.56 → 51.92, E 81.09 → 93.86, F 44.55 → 60.11,
G 60.52 → 75.95, H (oracle) 55.85 → 71.28, H (per station) 56.27 → 71.56, I 20.97 → 37.04 ms. The increase is about
15 ms in every group, as the periodicity estimate's cost does not depend on the signal: derived 3.9 M
multiply-adds per recomputation, 15 M per channel-second (section 8c, "Periodicity"). Windows PC, F-drift-s1
(8 channels, 240.1 channel-s, `--jobs 8`), alternated with the reference built in a worktree at `ff5475b`:
reference 59.5, 62.9, 62.5, 66.4; B4a 70.9, 100.8, 71.7, 111.7 ms per channel-second; the B4a runs 2 and 4 are
slower on every channel alike (PC state; texts identical in all four runs), so the typical cost is about
+10 ms (+16%).

Memory per channel in flight (B1's method, G-ragchew-s1, 12 channels × 366.0 s): `bank-b3b`'s binary 56 352 KiB
(1 thread), 339 032 KiB (10 threads): 32.16 MB; B4a 59 264 KiB, 351 852 KiB: **33.29 MB (+1.13 MB)**. Derived:
the periodicity estimate's arrays about 1.5 MB per channel, where the former windows held 60 kB plus about 0.6 MB
of FFT work arrays during a recomputation (+0.9 MB to +1.5 MB).

### 6.4 Periodicity confidence (Step 3)

The rule "the confident estimate of the shortest window", scored on each run's own decoded records (stage 1's E1
method: points at every recomputation, about every 0.256 s, inside transmissions of constant-speed stations
counted at S₅₀₀ ≥ 0 dB in groups A, B mixed style, C, G, H per-station view and I; correct within 5% of the true
dit; bootstrap 95% over channels; scores as recorded, rounded to 4 decimals): 171 426 points.
`python -m kz4ap_proto.experiments periodicity-decoded --out build/suite/full3 --name <run>` (new subcommand).

| run | windows | threshold | precision | coverage | median time to confident (s) |
|---|---|---|---|---|---|
| bank-b3b (2, 5, 10 s) | all | 0.03 | 0.804 (0.779–0.828) | 0.993 (0.992–0.994) | 0.53 |
| bank-b4a (41.7, 104, 208 T) | all | 0.03 | **0.753 (0.728–0.776)** | 0.993 (0.992–0.994) | 0.32 |
| bank-b3b | all | calibrated 0.2495 | 0.950 (0.943–0.958) | 0.582 (0.541–0.624) | 2.12 |
| bank-b4a | all | calibrated 0.2609 | 0.952 (0.943–0.961) | 0.555 (0.516–0.595) | 2.64 |
| bank-b3b | shortest alone | 0.03 | 0.809 (0.786–0.834) | 0.966 | 0.54 |
| bank-b4a | shortest alone | 0.03 | 0.753 (0.730–0.779) | 0.989 | 0.35 |
| bank-b3b | middle alone | 0.03 | 0.842 (0.813–0.868) | 0.965 | 3.54 |
| bank-b4a | middle alone | 0.03 | 0.878 (0.853–0.899) | 0.965 | 2.47 |
| bank-b3b | longest alone | 0.03 | 0.892 (0.866–0.917) | 0.922 | 8.66 |
| bank-b4a | longest alone | 0.03 | 0.914 (0.894–0.933) | 0.918 | 7.35 |

**Precision at 0.03 fell from 0.804 to 0.753 with non-overlapping intervals** (0.728–0.776 against 0.779–0.828;
two separate bootstraps, not a paired one): reported to the owner, as the brief requires. The fall is in the
shortest window (0.809 → 0.753); the two longer windows are more precise than the fixed ones and reach a confident
estimate sooner. Between these two runs p itself differs (branch 1's re-key settings changed, 6.2), so this table
alone does not attribute the fall to the windows; the ablation (6.5) does: with the re-key settings in dits and the
fixed windows the precision is 0.808, with the windows in dits and stage 1's re-key settings 0.747. The threshold at which the rule's precision reaches 0.95 is 0.2609 (0.2495 with the fixed
windows); stage 1's E1 found 0.2506. **0.03 is kept** (the brief: stage 1's end-to-end check made decoding worse at
the calibrated threshold).

Raw outputs (git-ignored): `build/suite/full3/experiments/linux/b4a/` (`compare-bank-b4a-vs-bank-b3b.md`,
`c2-diff-bank-b4a-vs-bank-b3b.md`, `periodicity-decoded-bank-b4a.md`, `periodicity-decoded-bank-b3b.md`,
`b4a-run.log`); decoded files `build/suite/full3/proto/bank-b4a/` on the Linux machine.

### 6.5 Ablation: which part costs CER

Two variants, each with one part of B4a reverted to stage 1's values through the overrides in seconds (`95c7618`;
`--set` in `kz4ap-bank-replay`; set to stage 1's values they reproduce its timing bit for bit, tested on three
golden streams). Both runs use the same build, are on the development set (Linux machine), and are compared with
`bank-b3b`:

- `bank-b4a-rekey`: the re-key settings in dits (W_min,k, time-out, seed memory, branch 1's included), with stage 1's
  periodicity windows of 2, 5 and 10 s (`--set periodicity_windows_s=[2,5,10]`). This changes p, the periodicity
  estimate's input (through branch 1's amplitude), but not the windows.
- `bank-b4a-windows`: stage 1's re-key settings (`--set rekey_after_s=0.8 --set rekey_timeout_s=2.0`), with the
  windows in dits. This keeps stage 1's p and changes only the windows.

Paired CER, variant − `bank-b3b` (bootstrap 95% over signals); **bold**: interval entirely above 0.

| group | signals | bank-b4a-rekey | bank-b4a-windows | bank-b4a (both, 6.2) |
|---|---|---|---|---|
| all | 509 | **+0.0116 (+0.0042 to +0.0202)** | **+0.0184 (+0.0122 to +0.0251)** | **+0.0277 (+0.0194 to +0.0362)** |
| A sensitivity | 192 | **+0.0118 (+0.0025 to +0.0224)** | **+0.0085 (+0.0019 to +0.0161)** | **+0.0153** |
| B fading | 30 | +0.0093 (−0.0087 to +0.0333) | **+0.0233 (+0.0017 to +0.0511)** | **+0.0350** |
| C fists | 135 | +0.0002 (−0.0016 to +0.0020) | **+0.0424 (+0.0298 to +0.0566)** | **+0.0438** |
| D speed | 12 | −0.0027 (−0.0112 to +0.0047) | −0.0425 (−0.1156 to +0.0133) | −0.0447 |
| E interference | 16 | +0.0056 (−0.0086 to +0.0250) | −0.0167 (−0.0594 to +0.0213) | −0.0105 |
| F tuning | 28 | +0.0109 (−0.0112 to +0.0352) | **+0.0358 (+0.0137 to +0.0604)** | **+0.0618** |
| G ragchew | 12 | −0.0015 (−0.0037 to +0.0002) | **+0.0253 (+0.0023 to +0.0576)** | **+0.0084** |
| H two-station QSO, oracle | 12 | +0.0045 (−0.0011 to +0.0118) | **+0.0304 (+0.0026 to +0.0686)** | **+0.0302** |
| H (per station) | 24 | **+0.0256 (+0.0080 to +0.0484)** | −0.0215 (−0.0559 to +0.0107) | +0.0204 |
| I Farnsworth | 48 | +0.0486 (−0.0140 to +0.1185) | **+0.0190 (+0.0070 to +0.0328)** | **+0.0468** |

Paired first-word CER, pooled: `bank-b4a-rekey` +0.1922 (+0.0827 to +0.3212); `bank-b4a-windows` +0.0640 (−0.0049 to
+0.1392).

Texts changed against `bank-b3b`: 333 of 525 (`bank-b4a-rekey`) and 399 of 525 (`bank-b4a-windows`). CPU: 45.17 and
55.06 ms per channel-second (one run each). The CPU cost is the windows'.

Worst regimes (test case CER, `bank-b3b` → variant):

- `bank-b4a-rekey`:
  - I 18/5 WPM paddle: 0.288 → 0.650.
  - E Δf 20 Hz, +0 dB: 0.697 → 0.808.
  - F offset 2.9 Hz 25 WPM: 0.079 → 0.183.
  - F drift 0.5 Hz/s: 0.000 → 0.080.
  - H per station, separate track 100 Hz: 0.104 → 0.169.

  These are mostly over starts. First-word CER rises in A, H and F. C fists does not move (+0.0002).
- `bank-b4a-windows`:
  - C hand imbalance −0.1, +0.1, +0.0: 0.207 → 0.361, 0.199 → 0.295, 0.188 → 0.276.
  - C machine imbalance +0.0: 0.002 → 0.077.
  - C bug −0.1: 0.064 → 0.124.
  - E Δf 50 Hz, +0 dB: 0.118 → 0.252.
  - F drift 0.2 Hz/s: 0.028 → 0.139.
  - F offsets at 20 WPM (0 Hz: 0.043 → 0.125; 5.9 Hz: 0.058 → 0.142).
  - H same-track offset 0 Hz: 0.120 → 0.229.

  Better: D 10 WPM 0.291 → 0.000; E +20 dB interferers.

**Answer.** Both parts cost CER, in different places:

- **The windows in dits** cost the larger share pooled (+0.0184). They account for C (all of it), F, G, H (oracle), B
  and part of I.
- **The re-key settings in dits** cost +0.0116 pooled. They account for A, H per station and part of I (18/5 paddle),
  mostly in the first words.
- The effects roughly add: +0.0116 + 0.0184 = +0.0300 against +0.0277 measured for both.

**The comb's precision fall is the windows'.** At threshold 0.03, over all windows:

| run | precision | coverage |
|---|---|---|
| `bank-b3b` | 0.804 (0.779–0.828) | 0.993 |
| `bank-b4a-rekey` | 0.808 (0.784–0.833) | 0.993 |
| `bank-b4a-windows` | 0.747 (0.723–0.772) | 0.993 |
| `bank-b4a` | 0.753 (0.728–0.776) | 0.993 |

So the change of p by the re-key settings does not move the comb, and the windows do.

**Is the shortest window too short at fast speeds? No.** The shortest window is 41.7 T in seconds:

- 1.25 s at 40 WPM (T = 30 ms);
- 2.00 s at 25 WPM;
- 3.34 s at 15 WPM;
- 4.17 s at 12 WPM;
- 0.50 s only at 100 WPM (T = 12 ms), the grid's fastest candidate.

Precision of the shortest window's confident estimates at 0.03, by the true dit's speed (`bank-b3b`, fixed 2 s →
`bank-b4a-windows`, 41.7 T; pooled counts):

| speed bin | 41.7 T in that bin | precision | fraction confident |
|---|---|---|---|
| 5–15 WPM | 3.3 to 10 s | 0.969 → 0.948 | 0.977 → 0.982 |
| 15–22 WPM | 2.3 to 3.3 s | 0.902 → 0.729 | 0.777 → 0.950 |
| 22–30 WPM | 1.7 to 2.3 s | 0.772 → 0.721 | 0.984 → 0.993 |
| 30–50 WPM | 1.0 to 1.7 s | 0.795 → 0.709 | 0.984 → 0.997 |

The largest fall is at 15–22 WPM, where the new window is longer than the old 2 s. So the loss does not come from
the window being too short. What changed is where the wrong estimates fall. Among the shortest window's confident
wrong estimates (all speeds), the share near 3 T (ratio 2.9–3.1) rose from 3.7% to 26.0%, and the share beyond 3.1 T
from 1.4% to 15.7%. The share at 1.05–1.45 T fell from 37.0% to 22.4%.

The wrong fraction is about the same early and late in a transmission (bank-b4a-windows: 0.255 before 5 s, 0.253
from 10 s on). So the early-availability rule (a candidate whose window is not full takes no part) is not the cause
either.

Conjecture, not measured: with each candidate judged over its own window, a long candidate (3 T and longer) is scored
over a window 3 or more times longer than the true dit's. Its normalized comb then differs in noise and taper from
the true candidate's, and the argmax across candidates whose windows differ favors the long ones. Comparing
candidates over different windows was not a question stage 1 measured.

Raw outputs (git-ignored), in `build/suite/full3/experiments/linux/b4a/`:

- `compare-bank-b4a-{rekey,windows}-vs-bank-b3b.md`
- `c2-diff-…`
- `periodicity-decoded-{bank-b3b,bank-b4a,bank-b4a-rekey,bank-b4a-windows}.md` (with the by-speed tables)
- `b4a-ablation.log`

The script for the wrong-estimate ratios is `build/b4a/wrong_ratio.py`.

### 6.6 Variant: one shared window per row (B4a-C; not the default)

What it is (`db38cd0`; `docs/signal-processing.md` section 8c, "Periodicity", the "Variant" bullet):
`periodicity_window_mode` = `"shared"` keeps N_w = 41.7, 104 and 208 but judges every candidate of a row over one
window N_w · T̂, T̂ the dit of the branch currently selected (its fitted T if its fit is eligible, else its nominal
d_k; capped at 0.2530 s), with stage 1's 2, 5 and 10 s before the channel's first selection. Each row is then stage
1's comb on that window, so the reach rule caps a row's candidates at about N_w T̂ / 18.3 (2.28 T̂ in the shortest
row). It adds a feedback loop the default does not have (the selected branch's dit sets the windows that judge T_P,
and T_P feeds the fits' prior and selection's fallback). The default (`"per_candidate"`) is unchanged: the
development set replayed at the default configuration with this build (`bank-b4c-default`) equals `bank-b4a` in every
decoded record of all 525 channels (texts, characters, corrections, selections, periodicity records, over starts,
switches); locally, 59 of 59 channels of three test cases identical to the build before.

Two variants, both on the development set (Linux machine, `--jobs 5`, one run each), compared with `bank-b3b`:

- `bank-b4a-shared`: `--set periodicity_window_mode=shared`, re-key settings in dits (as `bank-b4a`).
- `bank-b4a-shared-rekeys`: `--set periodicity_window_mode=shared --set rekey_after_s=0.8 --set rekey_timeout_s=2.0`,
  stage 1's re-key settings (as `bank-b4a-windows`, 6.5).

Paired CER, variant − `bank-b3b` (bootstrap 95% over signals); **bold**: interval entirely above 0. The B4a columns
are 6.2 and 6.5's, for comparison.

| group | signals | bank-b4a-shared | bank-b4a-shared-rekeys | bank-b4a (6.2) | bank-b4a-windows (6.5) |
|---|---|---|---|---|---|
| all | 509 | **+0.0223 (+0.0119 to +0.0341)** | **+0.0148 (+0.0072 to +0.0239)** | **+0.0277** | **+0.0184** |
| A sensitivity | 192 | **+0.0234 (+0.0047 to +0.0477)** | **+0.0177 (+0.0041 to +0.0312)** | **+0.0153** | **+0.0085** |
| B fading | 30 | +0.0106 (−0.0108 to +0.0339) | +0.0472 (−0.0100 to +0.1424) | **+0.0350** | **+0.0233** |
| C fists | 135 | **+0.0055 (+0.0019 to +0.0092)** | **+0.0064 (+0.0021 to +0.0109)** | **+0.0438** | **+0.0424** |
| D speed | 12 | −0.0336 (−0.0785 to +0.0047) | −0.0042 (−0.0512 to +0.0407) | −0.0447 | −0.0425 |
| E interference | 16 | **+0.0307 (+0.0028 to +0.0607)** | +0.0273 (−0.0055 to +0.0695) | −0.0105 | −0.0167 |
| F tuning | 28 | **+0.0600 (+0.0086 to +0.1203)** | **+0.0496 (+0.0080 to +0.1089)** | **+0.0618** | **+0.0358** |
| G ragchew | 12 | +0.0008 (−0.0009 to +0.0023) | **+0.0054 (+0.0002 to +0.0119)** | **+0.0084** | **+0.0253** |
| H two-station QSO, oracle | 12 | **+0.0264 (+0.0056 to +0.0537)** | +0.0012 (−0.0040 to +0.0067) | **+0.0302** | **+0.0304** |
| H (per station) | 24 | **+0.0967 (+0.0079 to +0.2221)** | −0.0042 (−0.0247 to +0.0197) | +0.0204 | −0.0215 |
| I Farnsworth | 48 | +0.0293 (−0.0167 to +0.0795) | +0.0028 (−0.0113 to +0.0189) | **+0.0468** | **+0.0190** |

Paired first-word CER, pooled, against `bank-b3b`: `bank-b4a-shared` +0.4620 (+0.2886 to +0.6698),
`bank-b4a-shared-rekeys` +0.3412 (+0.1782 to +0.5127).

The variant's own effect, paired CER against the per-candidate run with the same re-key settings (variant − base):

| group | shared − bank-b4a | shared-rekeys − bank-b4a-windows |
|---|---|---|
| all | −0.0054 (−0.0151 to +0.0052) | −0.0035 (−0.0129 to +0.0064) |
| A sensitivity | +0.0081 (−0.0069 to +0.0303) | +0.0091 (−0.0043 to +0.0259) |
| B fading | −0.0244 (−0.0659 to +0.0091) | +0.0239 (−0.0244 to +0.0960) |
| C fists | **−0.0383 (−0.0539 to −0.0252)** | **−0.0360 (−0.0498 to −0.0216)** |
| D speed | +0.0110 (−0.0213 to +0.0494) | +0.0383 (−0.0176 to +0.1099) |
| E interference | +0.0412 (−0.0232 to +0.1251) | +0.0440 (−0.0166 to +0.1326) |
| F tuning | −0.0017 (−0.0487 to +0.0615) | +0.0138 (−0.0316 to +0.0679) |
| G ragchew | **−0.0075 (−0.0177 to −0.0006)** | **−0.0200 (−0.0464 to −0.0016)** |
| H two-station QSO, oracle | −0.0038 (−0.0445 to +0.0418) | **−0.0293 (−0.0632 to −0.0042)** |
| H (per station) | **+0.0763 (+0.0116 to +0.1523)** | +0.0172 (−0.0116 to +0.0514) |
| I Farnsworth | **−0.0175 (−0.0339 to −0.0014)** | **−0.0163 (−0.0322 to −0.0005)** |

Bold here: interval clear of 0 (better below, worse above). Pooled first-word CER rises: +0.1935 (+0.0617 to +0.3491) and +0.2771 (+0.1423 to +0.4324).

**Groups made worse against `bank-b3b` (reported to the owner, the plan's rule; not a gate).**
`bank-b4a-shared`: pooled, A, C, E, F, H (oracle) and H per station. `bank-b4a-shared-rekeys`: pooled, A, C, F and G.
Worst regimes (test case CER, `bank-b3b` → variant):

- `bank-b4a-shared`: H per station, separate track 100 Hz 0.104 → 0.636; F offset 5.9 Hz 20 WPM 0.058 → 0.442,
  11.7 Hz 20 WPM 0.081 → 0.266, 8.8 Hz 25 WPM 0.057 → 0.215, 2.9 Hz 25 WPM 0.079 → 0.173; I 18/5 WPM paddle
  0.288 → 0.469; E Δf 20 Hz, +0 dB 0.697 → 0.859; A 12 WPM 0.313 → 0.344 (first-word CER 0.39 → 1.50), 25 WPM
  0.297 → 0.330. Better: D 10 WPM 0.291 → 0.093, D step 35 → 20 WPM 0.060 → 0.030, F drift 1 Hz/s 0.056 → 0.011.
- `bank-b4a-shared-rekeys`: F offset 11.7 Hz 20 WPM 0.081 → 0.462, 5.9 Hz 25 WPM 0.064 → 0.178, 0 Hz 20 WPM
  0.043 → 0.141; D step 20 → 35 WPM 0.029 → 0.105, ramp 15 → 30 WPM 0.000 → 0.062; C bug imbalance +0.1
  0.046 → 0.086. Better: D 10 WPM 0.291 → 0.186, D step 35 → 20 WPM 0.060 → 0.024.

Texts changed against `bank-b3b`: 390 of 525 (`bank-b4a-shared`) and 321 of 525 (`bank-b4a-shared-rekeys`).

**Periodicity at threshold 0.03** (6.4's method, 171 426 points; bootstrap 95% over channels):

| run | precision (rule) | coverage (rule) | shortest window alone: precision | median time to confident (s) |
|---|---|---|---|---|
| `bank-b3b` | 0.804 (0.779–0.828) | 0.993 | 0.809 | 0.53 |
| `bank-b4a` | 0.753 (0.728–0.776) | 0.993 | 0.753 | 0.32 |
| `bank-b4a-windows` | 0.747 (0.723–0.772) | 0.993 | 0.747 | — |
| `bank-b4a-shared` | **0.795 (0.771–0.819)** | 0.995 (0.994–0.996) | 0.800 (0.778–0.826) | 0.31 |
| `bank-b4a-shared-rekeys` | **0.789 (0.765–0.813)** | 0.995 (0.994–0.996) | 0.795 (0.772–0.820) | 0.24 |

The shared window restores the comb's precision to within `bank-b3b`'s interval (overlapping intervals, two separate
bootstraps) and keeps B4a's earlier confident estimates. Calibrated thresholds for 0.95 precision: 0.2608 and 0.2594
(B4a 0.2609, `bank-b3b` 0.2495).

**Error classes.** The shortest window's confident wrong estimates, share by ratio estimate / true dit (pooled):

| run | wrong | ≈ 3 T₀ (2.9–3.1) | > 3.1 T₀ | 2.1–2.9 T₀ | ≈ 2 T₀ | 1.05–1.45 T₀ |
|---|---|---|---|---|---|---|
| `bank-b3b` | 31 596 | 3.7% | 1.4% | 21.9% | 12.2% | 37.0% |
| `bank-b4a` | 41 903 | 26.3% | 15.9% | 15.6% | 5.1% | 23.0% |
| `bank-b4a-windows` | 42 830 | 26.0% | 15.7% | 15.8% | 5.4% | 22.4% |
| `bank-b4a-shared` | 32 958 | **0.3%** | 2.8% | 28.5% | 13.6% | 33.5% |
| `bank-b4a-shared-rekeys` | 33 839 | **0.4%** | 2.6% | 28.5% | 13.6% | 32.5% |

By speed bin (shortest window, `bank-b3b` → `bank-b4a` → `bank-b4a-shared`): precision 5–15 WPM 0.969 → 0.948 →
0.930; 15–22 WPM 0.902 → 0.734 → 0.842; 22–30 WPM 0.772 → 0.726 → 0.767; 30–50 WPM 0.795 → 0.719 → 0.814. The
share near 3 T₀ at 30–50 WPM: 20.8% → 44.4% → 0.9%. What remains wrong moves to 2.1–2.9 T₀ (5–15 WPM: 51% of
1429 wrong estimates; 30–50 WPM: 34%) and ≈ 2 T₀ (15–22 WPM: 24.5%): the shortest row's reach admits candidates up to
2.28 T̂, so a 2T or 2.2T alias stays reachable (derived from the reach; the attribution of the 2.1–2.9 class to it is a
conjecture). Over the rule (all windows) the shares are 0.6% near 3 T₀ and 4.5% beyond (`bank-b4a-shared`).
Table: `error-classes.md` below.

**Answer.** The shared window removes the near-3T₀ errors that per-candidate windows introduced (26% → 0.3% of the
shortest window's wrong estimates) and the comb's precision fall (0.753 → 0.795), and it removes most of the windows'
cost in C fists (−0.038 paired against `bank-b4a`, interval clear of 0). It does not repair the pooled CER: against
`bank-b4a` the pooled change is −0.0054 (interval includes 0), because it loses elsewhere, mostly in the first words
(pooled first-word CER +0.19 against `bank-b4a`) and in H per station (+0.076), E and A. Against `bank-b3b` both
variants remain worse pooled (+0.0223 and +0.0148). Conjecture, not measured: the losses in the first words and in
two-station views come from the feedback loop (a wrong early selection sets windows that confirm it) and from the
first selection's timing (stage 1's windows until then, then a jump to N_w T̂).

**CPU** (one run each, `--jobs 5`, alongside another job on the machine; not a gate): `bank-b4a-shared` 45.79,
`bank-b4a-shared-rekeys` 42.89 ms per channel-second pooled; `bank-b4c-default`, the default at the same build under
the same conditions, 56.60 (`bank-b4a` 57.56 at `--jobs 10`). The shared form's three FFTs per recomputation cost
about 11 ms per channel-second less than the per-candidate slid sums.

Raw outputs (git-ignored), in `build/suite/full3/experiments/linux/b4c/`: `compare-bank-b4a-shared{,-rekeys}-vs-bank-b3b.md`,
`compare-bank-b4a-shared-vs-bank-b4a.md`, `compare-bank-b4a-shared-rekeys-vs-bank-b4a-windows.md`, `c2-diff-…`
(including `c2-diff-bank-b4c-default-vs-bank-b4a.md`), `periodicity-decoded-bank-b4a-shared{,-rekeys}.md`,
`error-classes.md` (script `build/b4c/error_classes.py`), `b4c-run.log`; decoded files on the Linux machine under
`build/suite/full3/proto/bank-b4a-shared/`, `bank-b4a-shared-rekeys/`, `bank-b4c-default/`.

## 8. The stretch test and the new-over checks (B9)

Stage-2 spec sections 4.1 and 4.3. Tooling: `98ccb9d`. Every number here is measured unless marked otherwise.

### 8.1 The stretch group and the measures

**The group `S stretch`** (`suites.stretch`, recordings `S-stretch-25wpm-{0,1}-s{1,2,3}`). Each group-A 25 WPM recording
(`A-awgn-25wpm-{0,1}-sN`: 32 signals, machine keying, S₅₀₀ −10 to +20 dB SNR in 500 Hz in 2 dB steps, two signals per
step) is copied with:

- the same texts, carrier frequencies and first-mark times;
- each signal's keying timeline scaled in time by 25/12 = 2.0833 from its first mark, that is 12 WPM (derived: machine
  keying's every duration is a whole number of dits of 1.2 s / WPM; generated times are within 10⁻⁹ s of 25/12 times
  group A's, tested);
- S₅₀₀ lowered by 10·log₁₀(25/12) = 3.1876 dB, so the key-down power times the dit length, the energy per dit
  relative to the noise density, is unchanged (derived);
- the raised-cosine keying edges scaled too, 5 ms → 10.42 ms, so each envelope is exactly the original's in a time base
  25/12 slower (derived). With 5 ms edges kept, a stretched dit would carry 0.33 dB more energy than the original's
  (derived: a dit of length L with two raised-cosine edges of r inside it carries A²(L − 1.25 r): 93.75 ms against
  2.0833 × 41.75 ms);
- a recording length of 250 s (25/12 × 120 s) and its own noise seed (1000·seed + 160 + part).

Seeds 1 to 3 are generated on the Linux machine (`build/suite/full3`, manifest 123 → 129 entries, the 123 old ones
unchanged, checked); only seed 1's oracle channels are recorded and decoded; seeds 2 and 3 are held out until B12.
The development set `DEV` does not change (tested).

**Measures** (`experiments stretch`, `metrics.stretch_measures`). Each stretched signal is paired with the group-A signal
it copies (the same recording part and label index; texts and the 3.1876 dB S₅₀₀ difference checked per pair):

- the paired CER, the mean over pairs of (stretched CER − original CER), per original S₅₀₀ step (4 pairs) and pooled
  (64 pairs);
- the S₅₀₀ at which each side's CER (pooled per step) crosses 0.10, by the suite's linear interpolation, the stretched
  copies on their own S₅₀₀ axis;
- the shift, original crossing − stretched crossing, in dB of S₅₀₀. A time-base-invariant decoder gives +3.19 dB (the
  crossing at the same energy per dit);
- the same for first-word CER.

Intervals: bootstrap 95% over pairs (1000 resamples); for the shift, pairs resampled within each step, the same pairs
for both sides. The brief asked for intervals over test cases; with two test cases per side that bootstrap has no
meaning, so pairs (signals) are the units, as in the rest of this record. The shift's interval is nominal and
probably too narrow. It resamples only 4 pairs per step, and the crossing depends on the 2 or 3 steep steps. A
percentile bootstrap of n = 4 is discrete, and even for a mean it is narrow by about √(3/4).

**Runs** (Linux machine, `build/b9/b9_runs.sh`, which calls `build/b9/b9_run.sh` once per variant; replay tool and bench built in `build/linux-b9` from `98ccb9d`, whose
engine and bench code is that of `95c7618`; `--jobs 5`). Each run decodes the test cases `experiments.B9`: group A's
two 25 WPM recordings and the two stretched ones (seed 1), group H's oracle QSO labels (`H-qso-oracle-s1`) and the
pauses group's oracle copy (`pauses-s1.oracle`):

| run | settings | B4a's parts |
|---|---|---|
| `stretch-b3b` | `--set rekey_after_s=0.8 --set rekey_timeout_s=2.0 --set periodicity_windows_s=[2.0,5.0,10.0]` | none (all in seconds, stage 1) |
| `stretch-b4a` | defaults | both (in dits) |
| `stretch-rekey` | `--set periodicity_windows_s=[2.0,5.0,10.0]` | re-key settings in dits |
| `stretch-windows` | `--set rekey_after_s=0.8 --set rekey_timeout_s=2.0` | windows in dits |
| `stretch-b4ac` | `--set periodicity_window_mode=shared` | B4a-C's one shared window per decision, re-key settings in dits |
| `stretch-b4ac-rekeys` | `--set periodicity_window_mode=shared --set rekey_after_s=0.8 --set rekey_timeout_s=2.0` | B4a-C's shared window, stage 1's re-key settings |

Reproduction check: on the 76 channels these runs share with the development set (A 25 WPM and H oracle QSO labels),
the first four runs' decoded records are identical to the earlier runs with the same settings (`bank-b3b`, `bank-b4a`,
`bank-b4a-rekey`, `bank-b4a-windows`): 76 of 76 channels, every record. The two B4a-C runs (fix round 1) used a
replay tool and bench rebuilt in `build/linux-b9` from `b44da0b` (engine code of `db38cd0`, B4a-C), with `--jobs 5`
(`build/b9/b9_fix1.sh`). They have no earlier run to reproduce.

### 8.2 The stretch test

Pooled over the 64 pairs (CER of the original signals, of the stretched copies, and paired):

| run | CER original | CER stretched | paired CER (stretched − original) | S₅₀₀ at CER 0.10, original (dB SNR in 500 Hz) | stretched | shift (dB of S₅₀₀; invariance +3.19) |
|---|---|---|---|---|---|---|
| `stretch-b3b` | 0.2968 | 0.4131 | **+0.1166 (+0.0577 to +0.1902)** | +0.16 | +0.65 | **−0.49 (−0.73 to −0.10)** |
| `stretch-b4a` | 0.3117 | 0.4369 | **+0.1260 (+0.0627 to +0.1902)** | +0.79 | +0.69 | **+0.10 (−0.08 to +0.29)** |
| `stretch-rekey` | 0.3088 | 0.4239 | **+0.1161 (+0.0517 to +0.1888)** | +0.37 | +0.64 | **−0.28 (−0.67 to +0.17)** |
| `stretch-windows` | 0.3124 | 0.4196 | **+0.1074 (+0.0550 to +0.1692)** | +1.01 | +0.68 | **+0.33 (−0.21 to +0.58)** |
| `stretch-b4ac` | 0.3299 | 0.4210 | **+0.0927 (+0.0297 to +0.1691)** | +1.12 | +0.62 | **+0.50 (+0.30 to +0.73)** |
| `stretch-b4ac-rekeys` | 0.3075 | 0.3867 | **+0.0793 (+0.0399 to +0.1236)** | +1.18 | +0.66 | **+0.53 (−1.68 to +1.62)** |

Paired CER by the original's S₅₀₀ step (4 pairs each; intervals in the raw outputs). Steps −10 to −6 dB: +0.000 in
every run (both sides CER 1.000).

| original S₅₀₀ (dB SNR in 500 Hz) | −4 | −2 | 0 | +2 | +4 | +6 | +8 | +10 to +20 |
|---|---|---|---|---|---|---|---|---|
| `stretch-b3b` | +0.008 | +0.304 | +0.878 | +0.595 | +0.052 | +0.010 | +0.003 | −0.001 to +0.008 |
| `stretch-b4a` | +0.052 | +0.210 | +0.852 | +0.821 | +0.038 | +0.035 | +0.025 | −0.010 to +0.006 |
| `stretch-rekey` | +0.051 | +0.118 | +0.822 | +0.838 | +0.025 | +0.004 | −0.002 | −0.003 to +0.004 |
| `stretch-windows` | +0.055 | +0.194 | +0.777 | +0.617 | +0.029 | +0.032 | +0.019 | −0.007 to +0.008 |
| `stretch-b4ac` | +0.000 | −0.049 | +0.915 | +0.536 | +0.042 | +0.014 | +0.017 | −0.003 to +0.007 |
| `stretch-b4ac-rekeys` | +0.000 | +0.255 | +0.670 | +0.205 | +0.071 | +0.050 | +0.009 | −0.012 to +0.011 |

CER per step, `stretch-b4a` (original / stretched): −2 dB 0.763 / 0.973; 0 dB 0.139 / 0.991; +2 dB 0.041 / 0.863;
+4 dB 0.014 / 0.052. `stretch-b3b`: 0.594 / 0.896; 0.107 / 0.988; 0.018 / 0.613; 0.003 / 0.055. `stretch-b4ac`:
0.992 / 0.943; 0.185 / 1.094; 0.033 / 0.568; 0.009 / 0.050. `stretch-b4ac-rekeys`: 0.637 / 0.893; 0.155 / 0.824;
0.062 / 0.267; 0.015 / 0.086.

First-word CER, paired (stretched − original), pooled: `stretch-b3b` +0.2432 (+0.0877 to +0.4281); `stretch-b4a`
+0.9160 (+0.2456 to +1.6624); `stretch-rekey` +0.1277 (−0.2880 to +0.7020); `stretch-windows` +0.5722 (+0.2832 to
+0.9147); `stretch-b4ac` +1.4482 (+0.2736 to +2.8694); `stretch-b4ac-rekeys` +0.5772 (−0.3542 to +1.5641). There is no first-word crossing shift in any run. The original side's first-word CER is above 0.10 at the
highest step, +20 dB, in every run (one or two wrong first words among 4 pairs are enough), so it has no crossing.
Only `stretch-rekey`'s stretched copies cross, at +16.58 dB.

**Answer.** The bank decoder is **not time-base invariant** in any of the six variants.

- At equal energy per dit the stretched copies (12 WPM) decode worse than the originals (25 WPM). Pooled paired CER is
  +0.079 to +0.126, every interval above 0.
- The loss sits in the steep part of the curve, at original S₅₀₀ −2 to +2 dB.
- The CER-0.10 crossing shift is −0.49 to +0.53 dB of S₅₀₀, against the invariant +3.19 dB. So at 12 WPM the decoder
  needs about 2.7 to 3.7 dB more energy per dit than at 25 WPM.

The variants differ by a fraction of a dB, and the evidence for that is weak:

- `stretch-b4a`'s shift, +0.10 (−0.08 to +0.29), and `stretch-b3b`'s, −0.49 (−0.73 to −0.10), come from two separate
  bootstraps on the same recordings, and their intervals do not overlap. A paired bootstrap of the difference was not
  made.
- The shift intervals are nominal and probably too narrow (8.1).
- The single-part variants, the windows in dits +0.33 and the re-key settings in dits −0.28, do not add up to B4a's,
  and their intervals overlap both endpoints.
- B4a-C's shared window has the largest shift: +0.50 (+0.30 to +0.73) with the re-key settings in dits. With stage
  1's re-key settings it is +0.53, but with an interval of −1.68 to +1.62 that says nothing.
- Pooled paired CER does not separate the variants: the intervals overlap.

Above +8 dB both sides decode alike in every variant (|paired CER| ≤ 0.012).

What causes the remaining 2.7 to 3.7 dB is not measured here. Candidates are the settings still in seconds: those
stage-2 spec 3.2 keeps (for example the unknown-amplitude test's false-mark rate of 0.01 per second, per branch), and
the amplitude average τ_a, which spec 3.4 leaves to a stage-2 measurement. This is a conjecture, not a measurement.

### 8.3 The new-over checks

Measures (`experiments new-overs`, `metrics.new_over_counts`), from each channel's recorded over starts (the time on the
selected branch's time base at which the silence reached the new-over threshold) and its labels:

- **false new over**: an over start later than 0.1 s after a transmission's first key-down and before its last key-up.
  A real over start is recorded in the silence before a transmission, at most at its first key-down; the 0.1 s
  (heuristic) covers the few milliseconds between the decoder's time base and the labels' millisecond-rounded times
  (an over start 6 ms after a first key-down was seen in `bank-b3b`). An over start that found a turnover late (next
  item) is not counted here;
- **turnovers**, transmissions by the other station of a QSO, sorted by the first over start after the previous
  transmission's last key-up:
  - **found on time**: it comes no later than 0.1 s after the turnover's first key-down;
  - **found late**: it comes 0.1 to 2 s after the first key-down. Its delay from the first key-down is recorded;
  - **missed**: there is none up to 2 s after the first key-down.

  Fix round 1 (review I1): before it, a late find was also counted as a false new over;
- **over starts per same-station silence**: whether a silence between two transmissions of the same station (the
  pauses group's repeats, 2, 5, 10 and 20 s) started a new over;
- **first-word CER per over**, by the silence before the over (first over, after a turnover, after the same station),
  from the bench's per-transmission first-word counts (an upper bound, as everywhere in this record).

Group H's oracle QSO labels (the channel opened at the caller's labeled frequency) are counted only where the channel
holds the answering station: offsets 0 to 100 Hz, 10 QSOs, 80 transmissions, 70 turnovers. The two 200 Hz QSOs are left
out: they are beyond the oracle channel's −6 dB point at 150 Hz from its center (measured; `suites.view_fits`). The pauses group: 8 channels, 24 transmissions, 16
same-station silences. Intervals: bootstrap 95% over channels.

Recomputed in fix round 1 from the same decoded files:

| run | H: false new overs per transmission | H: turnovers found late | H: missed turnovers per turnover | pauses: false new overs per transmission | pauses: over starts per same-station silence |
|---|---|---|---|---|---|
| `stretch-b3b` | 0.088 (0.025–0.150), 7 / 80 | 0 / 70 | 0.229 (0.029–0.429), 16 / 70 | 0 / 24 | 16 / 16 |
| `stretch-b4a` | 0.050 (0.000–0.100), 4 / 80 | 0 / 70 | 0.314 (0.114–0.543), 22 / 70 | 0 / 24 | 16 / 16 |
| `stretch-rekey` | 0.088 (0.025–0.150), 7 / 80 | 0 / 70 | 0.271 (0.043–0.529), 19 / 70 | 0 / 24 | 15 / 16 |
| `stretch-windows` | 0.100 (0.025–0.188), 8 / 80 | 1 / 70 (delay 0.16 s) | 0.300 (0.086–0.529), 21 / 70 | 0 / 24 | 16 / 16 |
| `stretch-b4ac` | 0.075 (0.013–0.138), 6 / 80 | 0 / 70 | 0.271 (0.029–0.543), 19 / 70 | 0 / 24 | 15 / 16 |
| `stretch-b4ac-rekeys` | 0.088 (0.025–0.163), 7 / 80 | 0 / 70 | 0.229 (0.029–0.443), 16 / 70 | 0 / 24 | 16 / 16 |

The split changes only `stretch-windows`. One of its nine false new overs was the late find of a 50 Hz turnover,
0.16 s after its first key-down. In every other run no turnover is found late. So group H's false new overs are not
late-found turnovers: the review's conjecture that they might be is refuted by these counts.

Missed turnovers by the answering station's offset (of 14 turnovers each; in the order `stretch-b3b`, `stretch-b4a`,
`stretch-rekey`, `stretch-windows`, `stretch-b4ac`, `stretch-b4ac-rekeys`):

- 0 Hz: 1, 4, 1, 3, 1, 1.
- 10 Hz: 0 in every run.
- 25 Hz: 0, 1, 0, 1, 0, 0.
- 50 Hz: 5, 6, 6, 5, 6, 4.
- 100 Hz: 10, 11, 12, 12, 12, 11.

False new overs (of 16 transmissions each, same order):

- 25 Hz: 3, 3, 3, 4, 3, 3.
- 100 Hz: 3, 1, 3, 4, 3, 4.
- 50 Hz: 1, 0, 1, 0, 0, 0.
- 0 Hz and 10 Hz: none.

First-word CER per over:

| run | H, first over (10) | H, after a turnover (70) | pauses, first (8) | pauses, after the same station (16) |
|---|---|---|---|---|
| `stretch-b3b` | 0.150 (0.050–0.300) | 0.427 (0.234–0.627) | 0.063 | 0.000 |
| `stretch-b4a` | 0.500 (0.100–0.951) | 0.499 (0.268–0.739) | 0.000 | 0.000 |
| `stretch-rekey` | 0.050 (0.000–0.150) | 0.501 (0.272–0.740) | 0.000 | 0.000 |
| `stretch-windows` | 0.600 (0.050–1.500) | 0.464 (0.272–0.663) | 0.063 | 0.094 |
| `stretch-b4ac` | 0.650 (0.150–1.250) | 0.516 (0.282–0.779) | 0.000 | 0.000 |
| `stretch-b4ac-rekeys` | 0.600 (0.050–1.500) | 0.438 (0.245–0.664) | 0.063 | 0.000 |

**Answer.** Stage 1's new-over rule (silence threshold T_new = max(0.5 s, 12 T_g)), in all six variants:

- It starts a new over in almost every same-station silence of the pauses group, 2 s included: 15 or 16 of 16
  (derived: T_new = 0.576 s at 25 WPM). It starts no false new over inside those transmissions (0 of 24).
- In group H, 0.05 to 0.10 false new overs per transmission, none of them a late-found turnover (except one in
  `stretch-windows`). Turnovers are found on time or not at all: 0 or 1 of 70 are found late, and 0.23 to 0.31 are
  missed, almost all at
  50 Hz and 100 Hz offsets, where the answering station is off the channel's center (with oracle channels the bank mixes at
  the caller's labeled frequency and has no tracker). There, a missed turnover is probably a weak or absent answering
  station rather than a silence too short; this is a conjecture, not separated here.
- First words after a turnover are about half wrong in every variant: 0.43 to 0.52, an upper bound.

These are the baselines B6 (two hypotheses after 8 T_g) will be measured against. The intervals of the six variants
overlap on every measure.

### 8.4 Raw outputs

Git-ignored, copied back from the Linux machine to `build/suite/full3/experiments/linux/b9/`:

- `stretch-stretch-{b3b,b4a,rekey,windows,b4ac,b4ac-rekeys}.{md,json}`: the per-step tables with intervals.
- `new-overs-stretch-{…}.md`: per tag as well. These are the fix-round-1 versions, with late turnovers split out.
- `c2-diff-stretch-…-vs-bank-….md`: the reproduction checks.
- `b9-prep.log`, `b9-runs.log`, `b9-fix1.log`.

Decoded files are in `build/suite/full3/proto/stretch-*/` and scored results in
`build/suite/full3/experiments/results/stretch-*/`, both on the Linux machine. The helper scripts are in `build/b9/`
(`b9_prep.sh`, `b9_check.py`, `b9_run.sh`, `b9_runs.sh`, `b9_fix1.sh`).
