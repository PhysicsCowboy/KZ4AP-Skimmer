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
