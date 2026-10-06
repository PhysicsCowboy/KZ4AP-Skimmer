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

**Answer (B4a-C, one shared periodicity window per row; a variant, not the default; fix round 1).** Judging every
candidate of a row over one window N_w · T̂, with T̂ only the selected branch's eligible fitted dit of its current
over (stage 1's 2, 5 and 10 s otherwise), instead of N_w · T per candidate: against `bank-b3b` the pooled paired CER is
**+0.0045 (−0.0022 to +0.0113)** with the re-key settings in dits and **−0.0033 (−0.0071 to +0.0001)** with stage 1's
(both builds include B4b, whose own effect at this build is −0.0027, interval including 0); against per-candidate
windows at the same build it is better by 0.0206 and 0.0195 (intervals clear of 0). The comb's precision at 0.03 is
0.801 (`bank-b4a` 0.753, `bank-b3b` 0.804) and near-3T₀ errors are 0.7% of the shortest window's confident wrong
estimates (`bank-b4a` 26.3%). Worse than `bank-b3b` (interval above 0): H per station (+0.0488) and the first words
(+0.20) with the re-key settings in dits; C (+0.0018) and E (+0.0145) with stage 1's. Stretch test: paired CER
+0.1134 and +0.0931, crossing shift −0.18 and −0.12 dB (invariance +3.19 dB). The first form's runs, with a fallback
to the nominal dit that trapped T̂ at 12 ms after start, are superseded (section 6.6.1). The default is unchanged.

**Answer (B4b, the noise-spectrum guard margin).** With the spectrum mask's guard margin 0.5 · L₁ = 4.8 ms
(was 20 ms; the owner's stage-2 decision) and the mask-bias table b_mask,k re-measured for it (0.8281 at k = 1 to
0.7713 at k = 32, was 0.8370 to 0.7852), **the development-set CER does not change measurably: paired +0.0004
(−0.0021 to +0.0028)** against `bank-b3b`, 116 of 525 channels with a different final text. No group's interval
lies entirely above 0; group I's starts at exactly 0 (+0.0018, 0 to +0.0054; one regime), group G's ends at
exactly 0 (−0.0023, −0.0052 to 0), group F improves (−0.0013, −0.0026 to −0.0003). The ten-seed mask-bias table
is about 1.2% to 1.5% below a 200-seed measurement at every branch (sampling error of the brief's method; +0.05 to
+0.06 dB relative to the true noise power in the absolute level, at most +0.011 dB in the default's ratios);
whether to adopt a 200-seed table is open for the owner (7.1; adopted in B4d, section 10). **Measured with the time constants in seconds** (stage 1's re-key settings and
windows, through B4a's overrides), as the owner instructed, so that the effect is B4b's alone; the code's defaults
keep B4a's time constants in dits, still undecided. While a PARIS station sends at S₅₀₀ = 20 dB the spectrum
accepts 1.70, 1.64 and 0.65 segments per second at 12, 25 and 40 WPM, against 0.78, 0.65 and 0.28 at 20 ms
(section 7).

**Answer (B9, the stretch test and the new-over checks).** The bank decoder is not time-base invariant. Group A's
25 WPM signals stretched to 12 WPM at the same energy per dit (S₅₀₀ 3.19 dB lower) decode worse in all six
variants and in B4a-C's final form (two more runs, 6.6.1; built with B4b), every pooled paired CER interval above
0:

| variant | pooled paired CER, stretched − original | CER-0.10 crossing shift (dB of S₅₀₀; invariant +3.19) |
|---|---|---|
| stage 1's settings in seconds | +0.117 | −0.49 |
| B4a | +0.126 | +0.10 |
| re-key settings in dits only | +0.116 | −0.28 |
| windows in dits only | +0.107 | +0.33 |
| B4a-C's shared window, first form, superseded (nominal-d_k fallback; 6.6.1) | +0.093 | +0.50 |
| shared window with stage 1's re-key settings, first form, superseded (nominal-d_k fallback; 6.6.1) | +0.079 | +0.53 |
| B4a-C's shared window, final form (`stretch-b4ac2`) | +0.113 | −0.18 (−0.61 to +0.28) |
| final form with stage 1's re-key settings (`stretch-b4ac2-rekeys`) | +0.093 | −0.12 (−0.41 to +0.34) |

So at 12 WPM the decoder needs about 2.7 to 3.7 dB more energy per dit than at 25 WPM. The variants differ by a
fraction of a dB, and that difference is not established. The shift intervals are separate and nominal bootstraps,
probably too narrow (8.1, 8.2).

Stage 1's new-over rule on the same variants:

- It starts a new over in 15 or 16 of the 16 same-station pauses of 2 to 20 s.
- Group H (oracle): 0.05 to 0.10 false new overs per transmission, which are not late-found turnovers (fix round 1).
  0.23 to 0.31 of the turnovers are missed, mostly at 50 and 100 Hz offsets.
- First words after a turnover are about half wrong: 0.43 to 0.52, an upper bound.

Details in section 8.

**Answer (B4d, the owner's decisions of 2026-10-06).** Code `a360361`. The periodicity windows are back to 2, 5 and
10 s for every candidate. The re-key settings stay in dits, and their clocks now start at the over's first
provisional mark, with the stretch beginning 7 d_k before that mark; a time-out that clears nothing also moves the
stretch's start. The 200-seed mask-bias table replaces the ten-seed one. Against `bank-b3b`:

- **Pooled paired CER +0.0051 (−0.0014 to +0.0112).** The interval includes 0.
- **Paired first-word CER +0.2081 (+0.1297 to +0.2902).** First words are worse.
- **Groups worse** (paired interval entirely above 0):
  - B fading: +0.0177 (+0.0026 to +0.0375).
  - C fists: +0.0022 (+0.0004 to +0.0040).
  - H per station: +0.0663 (+0.0159 to +0.1483).

`bank-b4d-noclock` (both clock switches off, otherwise the same code) gives a pooled paired CER of
+0.0094 (+0.0019 to +0.0176) and a first-word CER of +0.1702 (+0.0629 to +0.2984). Its groups worse are H (oracle)
and H per station. The clock fixes alone are `bank-b4d` − `bank-b4d-noclock`:

- Pooled: −0.0043 (−0.0125 to +0.0034).
- Better: I Farnsworth, −0.0593 (−0.1251 to −0.0109).
- Worse: C, +0.0025 (+0.0013 to +0.0037), and H per station, +0.0366 (+0.0040 to +0.0844).
- First-word CER: +0.0379 (−0.0614 to +0.1205).

So on this development set the clock fixes do not recover the first-word loss of the re-key settings in dits. The
stretch test on `bank-b4d` gives +0.1086 (+0.0524 to +0.1750), with a crossing shift of −0.48 (−0.66 to −0.11) dB of
S₅₀₀; `bank-b3b` gave +0.117 and −0.49. CPU is 43.20 ms per channel-second (`bank-b4d-noclock` 45.33). Details in
section 10.

**Answer (B4e, the re-key wait counted in the station's marks; the owner's option d of 2026-10-06).** Code `d90441a`.
The over's start is re-keyed once the branch has 8 provisional marks, with a time-out of 7 s of channel time from the
over's first provisional mark; B4d's clocks are unchanged. Against `bank-b3b`:

- **Pooled paired CER +0.0240 (+0.0044 to +0.0496).** The interval lies entirely above 0 (flagged; committed per the
  owner's decision). H per station carries it: the mean over the other 485 signals is −0.0046 (derived from the
  group means, no interval).
- **Paired first-word CER +0.3406 (+0.1801 to +0.5222).** First words are worse than with B4d (+0.2081): the wait in
  marks does not recover them.
- **Groups worse** (paired interval entirely above 0): H per station, +0.6028 (+0.2389 to +1.0236). Better: I
  Farnsworth, −0.0643 (−0.1180 to −0.0199).

Against `bank-b4d`: pooled +0.0189 (−0.0001 to +0.0421), first-word +0.1325 (−0.0239 to +0.3157); worse H per
station (+0.5364); better B (−0.0259), D (−0.0065) and I (−0.0536). Traced (section 11.4): in every traced channel the
first provisional mark is a start-up artifact 1.7 to 3.7 ms into the stream, so the count and the stretch start at the
stream's start; the 7 s time-out never clears the pre-station noise before the station's 8 marks (B4d's 0.5 s
time-out on branch 1 did), and branch 1, selected by default, re-keys from the stream's start and publishes the noise
(8 of the 10 worst first words). In H per station, 8 short false marks (median 0.093 s of keyed time within a median
1.0 s) reach the wait between overs, where 16.7 d_k of keyed time did not, and their re-keys publish noise. The
stretch test gives +0.1023 (+0.0531 to +0.1578), shift −0.02 (−0.54 to +0.35) dB of S₅₀₀ (`bank-b4d` −0.48). CPU
53.07 ms per channel-second (`bank-b4d` 43.20). Details in section 11.

**Answer (B4f, guards on the counted marks; the owner's option e of 2026-10-06).** Code `91ffdc6`. A provisional mark
counts toward the 8-mark wait, and starts the 7 s time-out and the stretch, only if the branch's filter was full when
it began (derived) and it lasted at least L_k (heuristic). Against `bank-b3b`:

- **Pooled paired CER +0.0078 (−0.0047 to +0.0203).** The interval includes 0 (B4e: +0.0240, above 0).
- **Paired first-word CER +0.1062 (+0.0091 to +0.2004).** Still above 0, about half of B4e's +0.3406.
- **Groups worse** (paired interval entirely above 0): **H per station +0.3313 (+0.1449 to +0.5448)** (flagged;
  B4e +0.6028). Better: I Farnsworth −0.0593.

Against `bank-b4d`: pooled +0.0027 (−0.0074 to +0.0151), first-word −0.1019 (−0.2113 to +0.0053); worse H per station
+0.2650; better A, B, I. Against `bank-b4e`: −0.0161 (−0.0301 to −0.0040), first-word −0.2344. Guard 1 alone
(`bank-b4f-g1`) recovers about a third of B4e's first-word loss; guard 2 removes about half of H per station's loss.
Guard 2's premise, measured: 21.5% of white-noise provisional marks last L_k or more, 99.8% of a station's own, 45.6%
in group H's silences, 7.9% to 12.9% of a station 50 to 200 Hz away. Traced (12.4): H per station's remaining loss is
short branches (mostly 2 to 9) re-keying on the partner station's leak, whose elements are real and long enough, and
then being selected. B4e's CPU rise was the seed's quantile, a full sort every block while unknown; selecting the two
order statistics gives the same value bit for bit, and CPU is **41.72 ms per channel-second** (`bank-b4e` 53.07; B4e's
rule at the fixed code 43.58). Stretch test +0.1060 (+0.0584 to +0.1619), shift −0.16 (−0.61 to +0.20) dB of S₅₀₀.
Details in section 12.

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

Derived from the arrays the code allocates, per channel at 1500 samples/s (docs/signal-processing.md, appendix
A.8c, "Channel decoder", "Memory"):

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

In `engine/src/bank/fit.cpp` (docs/signal-processing.md, appendix A.8c, "Duration fit": "Evaluation"):

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
(docs/signal-processing.md, 8c, "Bit-for-bit check", table; since 2026-10-05 section 9.4 of this record): at most 1.3 · 10⁻¹⁵ nats (Windows) and
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

- docs/signal-processing.md, 8c, "Memory" (since 2026-10-05: section 9.6 of this record, and appendix A.8c's limitations): the 1.7 MB by which the measured per-channel memory after B1
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

In `engine/src/bank/fit.cpp` and the new `engine/src/bank/vecmath*.cpp` (docs/signal-processing.md, appendix A.8c,
"Duration fit": "SLEEF's exp and ln (Plan B, B2(b))"; commit `35817fa`):

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
  1.001 · 2^−52 · (5 |ℓ_total| + 14.3 + |ln σ_ln²|) nats (derivation in docs/signal-processing.md, appendix A.8c, "SLEEF's bound", and in
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
docs/signal-processing.md (that statement now in section 9.4 of this record), about 20 min in `fit_test.cpp`); all three now state the B2(b) measurement
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

Two rules in the bank decoder's noise estimate (`docs/signal-processing.md` appendix A.8c, "Noise" (exact zeros, the recovery of a stuck level);
"Channel decoder", "Exact zeros"):

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

**Superseded (fix round 1, 6.6.1).** The runs of this section used the first form of T̂, which fell back to the
selected branch's nominal dit d_k without an eligible fit; that made T̂ = 12 ms from branch 1's first key-up until the
selector's first switch (a start-up trap). They are kept as the record of that form; 6.6.1 replaces them.

What it is (`db38cd0`; `docs/signal-processing.md` appendix A.8c, "Periodicity", "The shared-window variant"):
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
the same conditions, 56.60 (`bank-b4a` 57.56 at `--jobs 10`). The difference between the variant and the default (one run each, on a loaded machine) is measured; attributing
it to the periodicity step alone (three FFTs per recomputation against the slid sums) is a conjecture, as the variant
also changes the decoding.

Raw outputs (git-ignored), in `build/suite/full3/experiments/linux/b4c/`: `compare-bank-b4a-shared{,-rekeys}-vs-bank-b3b.md`,
`compare-bank-b4a-shared-vs-bank-b4a.md`, `compare-bank-b4a-shared-rekeys-vs-bank-b4a-windows.md`, `c2-diff-…`
(including `c2-diff-bank-b4c-default-vs-bank-b4a.md`), `periodicity-decoded-bank-b4a-shared{,-rekeys}.md`,
`error-classes.md` (script `build/b4c/error_classes.py`), `b4c-run.log`; decoded files on the Linux machine under
`build/suite/full3/proto/bank-b4a-shared/`, `bank-b4a-shared-rekeys/`, `bank-b4c-default/`.

### 6.6.1 Fix round 1: T̂ only from an eligible fit of the current over (`bank-b4a-shared2`)

**What changed** (`ae0188c`; section 8c, "Periodicity", the "Variant" bullet). T̂ is now the selected branch's fitted
dit only when that fit is eligible and belongs to the branch's current over (its start re-keyed); otherwise there is
no T̂ and the rows are stage 1's 2, 5 and 10 s. There is no fallback to the nominal d_k. The review of B4a-C found that
the fallback made T̂ = d_1 = 12 ms from branch 1's first key-up (about 1.1 s into a stream: the selector starts on
branch 1, whose fit is never eligible below about 91 WPM) until the selector's first switch, with windows of 0.50, 1.25
and 2.50 s whose reach stops at 27.3, 68.1 and 136.3 ms (derived), a start-up trap; the runs above
(`bank-b4a-shared`, `bank-b4a-shared-rekeys`, and B9's `stretch-b4ac`, `stretch-b4ac-rekeys`) are **superseded** for
that reason. A turnover is the decoder's own new over (key up for longer than T_new = max(0.5 s, 12 T_g) on the
selected branch): from it, T̂ is withheld until the over's start is re-keyed and its fit is eligible. Measured on four
golden streams: the first selection at 1.11 to 1.15 s, the first T̂ at 2.82 to 3.67 s.

**Build.** These runs are at `ae0188c`, which includes B4b's guard margin and mask table (section 7, now the
default). To separate the two, the same build was also run with per-candidate windows (`bank-b4c2-default`, and
`bank-b4c2-windows` with stage 1's re-key settings): B4b alone, `bank-b4c2-default` − `bank-b4a`, is −0.0027
(−0.0069 to +0.0009) pooled, no group interval clear of 0. Linux machine, `--jobs 10`, one run each.

**Paired CER against `bank-b3b`** (variant − base; bootstrap 95% over signals; **bold**: interval entirely above 0):

| group | signals | bank-b4a-shared2 | bank-b4a-shared2-rekeys |
|---|---|---|---|
| all | 509 | +0.0045 (−0.0022 to +0.0113) | −0.0033 (−0.0071 to +0.0001) |
| A sensitivity | 192 | −0.0033 (−0.0100 to +0.0027) | −0.0046 (−0.0124 to +0.0019) |
| B fading | 30 | −0.0039 (−0.0228 to +0.0170) | −0.0124 (−0.0307 to +0.0024) |
| C fists | 135 | +0.0014 (−0.0008 to +0.0037) | **+0.0018 (+0.0001 to +0.0038)** |
| D speed | 12 | −0.0418 (−0.1003 to +0.0005) | −0.0401 (−0.0974 to +0.0000) |
| E interference | 16 | +0.0286 (−0.0007 to +0.0605) | **+0.0145 (+0.0039 to +0.0267)** |
| F tuning | 28 | +0.0114 (−0.0094 to +0.0371) | −0.0082 (−0.0197 to −0.0003) |
| G ragchew | 12 | −0.0020 (−0.0068 to +0.0013) | −0.0020 (−0.0057 to +0.0009) |
| H two-station QSO, oracle | 12 | +0.0075 (−0.0003 to +0.0180) | −0.0010 (−0.0025 to +0.0006) |
| H (per station) | 24 | **+0.0488 (+0.0021 to +0.1222)** | −0.0045 (−0.0442 to +0.0246) |
| I Farnsworth | 48 | +0.0274 (−0.0232 to +0.0858) | −0.0013 (−0.0059 to +0.0039) |

Paired first-word CER, pooled: `bank-b4a-shared2` **+0.2028 (+0.0821 to +0.3253)** (A +0.2745, B +0.4147,
H oracle +0.0839, intervals above 0); `bank-b4a-shared2-rekeys` +0.0093 (−0.0318 to +0.0581) (E +0.2063 above 0).
Texts changed against `bank-b3b`: 366 and 238 of 525.

**The variant's own effect**, paired against the per-candidate run of the same build and re-key settings
(variant − base; **bold**: interval clear of 0):

| group | shared2 − bank-b4c2-default | shared2-rekeys − bank-b4c2-windows |
|---|---|---|
| all | **−0.0206 (−0.0286 to −0.0128)** | **−0.0195 (−0.0260 to −0.0130)** |
| A sensitivity | **−0.0116 (−0.0237 to −0.0019)** | −0.0086 (−0.0180 to +0.0003) |
| B fading | **−0.0435 (−0.0948 to −0.0120)** | **−0.0356 (−0.0734 to −0.0104)** |
| C fists | **−0.0426 (−0.0589 to −0.0290)** | **−0.0407 (−0.0555 to −0.0272)** |
| D speed | +0.0028 (−0.0256 to +0.0271) | +0.0023 (−0.0219 to +0.0224) |
| E interference | +0.0308 (−0.0132 to +0.0786) | +0.0256 (−0.0143 to +0.0718) |
| F tuning | **−0.0505 (−0.0908 to −0.0154)** | **−0.0435 (−0.0697 to −0.0213)** |
| G ragchew | **−0.0110 (−0.0258 to −0.0018)** | **−0.0280 (−0.0657 to −0.0024)** |
| H two-station QSO, oracle | −0.0231 (−0.0676 to +0.0124) | **−0.0314 (−0.0712 to −0.0022)** |
| H (per station) | +0.0381 (−0.0232 to +0.1109) | +0.0302 (−0.0087 to +0.0688) |
| I Farnsworth | **−0.0169 (−0.0322 to −0.0020)** | **−0.0191 (−0.0319 to −0.0071)** |

Pooled first-word CER in the same pairing: −0.0855 (−0.1979 to +0.0181) and −0.0383 (−0.1228 to +0.0457). Against
the superseded form, `bank-b4a-shared2` − `bank-b4a-shared` is −0.0179 (−0.0291 to −0.0083) pooled and first-word
CER −0.2592 (−0.4092 to −0.1114): the start-up trap cost both.

**Periodicity at threshold 0.03** (6.4's method, 171 426 points):

| run | precision (rule) | coverage (rule) | shortest window alone: precision | median time to confident (s) |
|---|---|---|---|---|
| `bank-b4a-shared2` | 0.801 (0.776–0.825) | 0.993 (0.992–0.993) | 0.806 (0.783–0.831) | 0.55 |
| `bank-b4a-shared2-rekeys` | 0.797 (0.772–0.820) | 0.993 (0.992–0.993) | 0.801 (0.778–0.827) | 0.53 |

(`bank-b3b` 0.804, `bank-b4a` 0.753; calibrated thresholds for 0.95 precision 0.2589 and 0.2575.) The median time to
a confident estimate is back at `bank-b3b`'s 0.53 s: the superseded form's 0.31 s came from the short windows at
T̂ = 12 ms.

**Error classes** (the shortest window's confident wrong estimates, pooled): near 3 T₀ (2.9–3.1) **0.7%**
(`bank-b4a-shared2`) and **1.0%** (`-rekeys`), beyond 3.1 T₀ 1.8% and 1.8% (`bank-b3b` 3.7% and 1.4%; `bank-b4a`
26.3% and 15.9%); 2.1–2.9 T₀ 26.6% and 26.8%, ≈ 2 T₀ 13.5% and 13.5%, 1.05–1.45 T₀ 35.6% and 34.6%. Precision of the
shortest window by speed bin (`bank-b4a-shared2`): 5–15 WPM 0.940, 15–22 WPM 0.868, 22–30 WPM 0.769, 30–50 WPM 0.821
(`bank-b3b` 0.969, 0.902, 0.772, 0.795). Over the rule: 1.0% near 3 T₀, 3.8% beyond.

**The stretch test** (B9's method and test cases, section 8; `build/b9/b9_run.sh`'s steps with this build and 10
workers; pooled paired CER stretched − original over 64 pairs, and the CER-0.10 crossing shift, original − stretched,
in dB of S₅₀₀; time-base invariance would be +3.19 dB; the shift's interval is nominal, section 8.1):

| run | settings | paired CER | crossing shift (dB of S₅₀₀) |
|---|---|---|---|
| `stretch-b4ac2` | `--set periodicity_window_mode=shared` | +0.1134 (+0.0571 to +0.1730) | −0.18 (−0.61 to +0.28) |
| `stretch-b4ac2-rekeys` | also `rekey_after_s=0.8`, `rekey_timeout_s=2.0` | +0.0931 (+0.0419 to +0.1508) | −0.12 (−0.41 to +0.34) |
| `stretch-b4ac` (superseded) | as `stretch-b4ac2`, nominal-d_k fallback | +0.0927 (+0.0297 to +0.1691) | +0.50 (+0.30 to +0.73) |
| `stretch-b4ac-rekeys` (superseded) | | +0.0793 (+0.0399 to +0.1236) | +0.53 (−1.68 to +1.62) |

Still far from invariance; the fixed variant does not recover the superseded form's shift (separate bootstraps; the
differences are not established). These stretch runs also include B4b. New-over checks (group H): false new overs
0.075 and 0.088 per transmission, missed turnovers 0.243 and 0.243 per turnover.

**CPU** (Linux machine, `--jobs 10`, the machine otherwise idle; one run each; not a gate): `bank-b4a-shared2` 45.53,
`bank-b4a-shared2-rekeys` 43.01 ms per channel-second pooled.

**Answer.** Without the start-up trap, the shared window costs nothing pooled against `bank-b3b` (+0.0045, interval
includes 0; −0.0033 with stage 1's re-key settings), and against per-candidate windows at the same build it is
better pooled by 0.021 and 0.020 (intervals clear of 0), in A, B, C, F, G and I. It keeps the comb's precision at
`bank-b3b`'s level (0.801) with near-3T₀ errors at 0.7%. Remaining losses against `bank-b3b`: H per station
(+0.0488) and the first words (+0.20 pooled) with the re-key settings in dits; C (+0.0018) and E (+0.0145) with stage
1's. Groups made worse (interval above 0), reported to the owner: `bank-b4a-shared2` H per station;
`bank-b4a-shared2-rekeys` C and E.

Raw outputs (git-ignored), in `build/suite/full3/experiments/linux/b4c/fix1/`: compares against `bank-b3b`,
`bank-b4a`, `bank-b4a-windows`, `bank-b4a-shared`, `bank-b4c2-default` and `bank-b4c2-windows`, c2-diffs,
`periodicity-decoded-bank-b4a-shared2{,-rekeys}.md`, `error-classes-fix1.md`, `stretch-stretch-b4ac2{,-rekeys}.md`,
`new-overs-stretch-b4ac2{,-rekeys}.md`, `b4c-fix1.log`.

## 7. The noise-spectrum guard margin (B4b)

### 7.1 What changed

Stage-2 spec section 3.3 (owner, 2026-10-03); `docs/signal-processing.md` section 8c, "Noise". Code: `81a42cb`.

| Setting | Before (stage 1) | B4b | Class | Status |
|---|---|---|---|---|
| Guard margin g (`guard_margin_s`) | 20 ms (30 samples at 1500 samples/s) | 0.5 · L₁ = 4.8 ms (round(7.2) = 7 samples, 4.67 ms) | seconds tied to branch 1's filter and the transmitter's keying edges, not dits | heuristic (owner) |
| Mask bias b_mask,k (`mask_bias`) | 0.8370 (k = 1) … 0.7852 (k = 32) | 0.8281 … 0.7713 | — | measured (Plan B task B4b, white noise, seeds 101–110) |

**How B4b was measured: with the time constants in seconds.** The owner has not decided B4a (time constants in
dits; sections 6.2 to 6.6). On his instruction (2026-10-04) B4b is measured against `bank-b3b` with stage 1's re-key
wait 0.8 s, time-out 2 s and periodicity windows 2, 5 and 10 s, set by B4a's overrides
(`--set rekey_after_s=0.8 --set rekey_timeout_s=2.0 --set "periodicity_windows_s=[2,5,10]"`), so that the
comparison changes the margin and the table only. The new margin and table become the code's defaults (decided);
B4a's defaults in dits stay as they are.

**The reference reproduced.** At `8eab61e` (before B4b) with those three overrides, `bank-b4b-ref` equals
`bank-b3b` in **all 525 channels, every decoded record identical** (every field but `cpu_s`, the periodicity records
included). After B4b, the same binary as `bank-b4b` with `--set guard_margin_s=0.02 --set mask_bias=[stage 1's
table]` (`bank-b4b-oldmask`) equals `bank-b4b-ref` in all 525 channels: B4b changes nothing else.

**The table (Step 1 and 2).** Stage 1's Task 5 method on the C++ estimate, through a new tool,
`kz4ap-noise-mask` (`white`: the table on the tool's own white noise; `stream`: one complex64 stream's segments and
masked branch powers). The table comes from stage 1's own Task 5 noise (numpy's `default_rng`, seeds 101–110, 60 s
each, rounded to complex64), so it is paired with the old table:

| quantity | at 20 ms | at 4.8 ms |
|---|---|---|
| b_mask,1 … b_mask,32, stage 1's noise through the C++ estimate | 0.8370 … 0.7852 (stage 1's table to 4.9 · 10⁻⁵; identical at four decimals) | **0.8281 … 0.7713** |
| segments accepted of offered (10 × 60 s) | 2362 of 3500 | 3459 of 3500 |
| kept fraction, noise alone | 0.619 | 0.826 |
| per-seed scatter (k = 1 … 32) | 2.6% … 4.5% | 2.6% … 3.9% |
| second noise source, the same 10 seed numbers (`std::mt19937_64`, Box–Muller) | 0.8483 … 0.8020 | 0.8478 … 0.8000 |
| second noise source, 200 seeds (1001–1200) | 0.8405 … 0.7849 | 0.8381 … 0.7812 |

Cross-check against the prototype (seed 101, run with `guard_margin_s=0.0048` on the same complex64 stream): masked
branch powers equal to a relative 1.3 · 10⁻¹⁵, segments 344 of 350 in both (at 20 ms: 1.9 · 10⁻¹⁵, 237 of 350).
The second source's white-noise table is identical on the Linux machine and the Windows PC to 4.4 · 10⁻¹⁶. The
ten-seed table's standard error is about 0.8% to 1.2% (from the scatter), as large as the margin's effect on it:
−1.1% (k = 1) to −1.8% (k = 32) on stage 1's noise, −0.3% to −0.5% on the 200 seeds. The 200-seed measurement points
one way: at 4.8 ms it lies above the adopted ten-seed table at every branch (0.8381 against 0.8281 at k = 1, 0.7812
against 0.7713 at k = 32; 1.2% to 1.5% over the 32 branches) and within 0.5% of stage 1's table (−0.5% to +0.1%); its
standard error is about 0.2% (3% / √200, derived), and at 20 ms the same source agrees with stage 1's table (0.8405
against 0.8370, 0.7849 against 0.7852). So most of the adopted table's shift from stage 1's is the sampling error of
seeds 101–110, one sign at every branch because the branches see the same noise; the margin's own effect is about
−0.3% to −0.5% (measured). Effect on the estimate, if the 200-seed values are the truth: dividing by the adopted table
reads σ²_v,k 1.2% to 1.5% high, +0.05 to +0.06 dB relative to the true noise power, in the absolute level (variant
(b), `spectrum-level`, not the default); in the default (variant (a)) only b_k / b_1 enters, and branch k's σ²_v,k
relative to branch 1's reads 0% to 0.25% high, at most +0.011 dB (derived from the two tables). The table is
unchanged: whether to adopt a 200-seed table (stage 1's numpy noise, so the method stays stage 1's) and re-run the
paired comparison is open, for the owner. The options for the owner: (a) keep the brief's ten-seed
table; (b) adopt a 200-seed table on numpy noise and re-run the paired comparison (cheap: the table takes seconds,
the development-set run about 6 minutes). (Adopted in B4d, section 10: a 200-seed table, measured on this record's
second noise source, `std::mt19937_64` and Box–Muller, not on numpy noise.)

The bench test `NoiseMask.TheTableIsAWhiteNoiseMeasurementAtTheDefaultMargin` re-measures on the second source and
requires every branch within 3 standard errors of the difference (measured 2.51 at worst); at 20 ms against stage
1's table, 1.35. It checks the table's magnitude, not its identity: stage 1's table would pass it at 4.8 ms too (at
k = 32, 0.8000 − 0.7852 = 1.3 of its standard errors, against 2.5 for the adopted table). The table's exact values
are pinned by `BankConfig.PlanBDefaults`; their reproduction (the C++ estimate on stage 1's noise) is by the
git-ignored scripts listed in 7.3.

Golden tests: none moved. They run with stage 1's margin and table set explicitly (`kStage1GuardMarginS`,
`kStage1MaskBias`; `stage1_config()` in `engine/tests/bank/golden.hpp`), as B4a's run with stage 1's time constants.
One non-golden expectation changed by derivation: the |v_k|² window's full size, 32 × 34 721 → 32 × 34 675 columns
(the look-back falls by round((2 × 20 − 2 × 4.8) ms × 1500 samples/s) = 46 samples). Tests added (5): the mask's
reach (7 samples at 1500 samples/s, 10 at 2000, 30 at stage 1's 20 ms), the white-noise source, the table against a
re-measurement at the default margin and stage 1's at 20 ms, the tool's counting window. `ctest --preset windows`
354 of 354 and `ctest --preset linux` 354 of 354 passed or skipped; Python tests 269 passed, 7 expected failures;
smoke unchanged (Envelope CER 0.0353, Matched 0.0436).

### 7.2 Accepted segments while a station sends

PARIS at 12, 25 and 40 WPM, S₅₀₀ = 20 dB against white noise of 1 FS² per complex sample, 1 s of noise, then about
60 s of sending (the bench's keying, 5 ms edges), one stream per speed (one realization: about ±0.14 accepted
segments per second at 1.7 per second and ±0.10 at 0.65, binomial over about 350 offered segments; derived,
indicative, since the segments are correlated through the PARIS pattern); counted over the segments that start while
the station sends (348 to 351 offered, 5.87 per second). Measured by `kz4ap-noise-mask stream`; derived from a
noise-free model of the code's mask rule (flag level at the true σ²_v,1); the stage-2 spec's derived clean fractions
for 20 ms (a gap loses 2g + L₁) in the last column:

| WPM | accepted per s, 20 ms | accepted per s, 4.8 ms | kept fraction, 20 ms → 4.8 ms (measured) | derived clean fraction, 20 ms → 4.8 ms | derived accepted per s, 20 ms → 4.8 ms | spec, 20 ms |
|---|---|---|---|---|---|---|
| 12 | 0.78 | **1.70** | 21.6% → 38.6% | 40.8% → 49.5% | 1.91 → 2.07 | 42% |
| 25 | 0.65 | **1.64** | 16.6% → 36.1% | 26.8% → 43.3% | 1.69 → 2.53 | 28% |
| 40 | 0.28 | **0.65** | 13.3% → 30.8% | 20.5% → 35.9% | 0.59 → 0.82 | 21% |

So the spectrum's shape now updates 2.2 to 2.5 times as often while a station sends (measured). The measured rates
are below the noise-free ones because noise peaks are flagged too: in noise alone the mask keeps 82.6% of samples at
4.8 ms and 61.9% at 20 ms. In the code's rule a mark costs N₁ − 1 + R samples on each side of it (26.7 ms per gap at
4.8 ms, 57.3 ms at 20 ms, derived); the spec's "about 50 ms" counted L₁ once.

**Erratum in the stage-2 spec, for the owner (not edited; the spec needs his approval).** Section 3.3 says the
20 ms margin "adds about 50 ms to every gap (20 ms before, 20 ms plus L_1 after)". The code's rule (stage 1's,
unchanged by B4b) leaves out N₁ − 1 + R samples on each side of a mark: 2 · (13 + 30) = 86 samples, 57.3 ms per gap
at 20 ms, and 2 · (13 + 7) = 40 samples, 26.7 ms per gap at 4.8 ms (derived; checked by the review). The spec
counts L₁ once. The decision (g = 0.5 · L₁) does not depend on it, and the spec's clean fractions (42%, 28%, 21%)
are close to the noise-free model of the code's rule at 20 ms (40.8%, 26.8%, 20.5%). Proposed wording: "adds about
57 ms to every gap (20 ms plus L₁ on each side)".

### 7.3 `bank-b4b` against `bank-b3b` (Linux machine, the development set, time constants in seconds)

Texts: **116 of 525 channels decode to a different final text** (409 identical; 117 with identical character
lists; 46 with every decoded record identical). The periodicity records are identical in all 291 690 records (their
input, branch 1's posterior, does not depend on the noise spectrum's shape). Paired CER (variant − reference,
signal by signal; bootstrap 95% over signals):

| group | signals | paired CER | paired first-word CER |
|---|---|---|---|
| all | 509 | **+0.0004 (−0.0021 to +0.0028)** | +0.0116 (−0.0359 to +0.0620) |
| A sensitivity | 192 | +0.0021 (−0.0017 to +0.0065) | −0.0247 (−0.0534 to +0.0026) |
| B fading | 30 | −0.0039 (−0.0144 to +0.0047) | +0.3433 (−0.0334 to +1.3100) |
| C fists | 135 | +0.0001 (−0.0001 to +0.0003) | +0.0000 (+0.0000 to +0.0000) |
| D speed | 12 | +0.0000 (+0.0000 to +0.0000) | +0.0000 (+0.0000 to +0.0000) |
| E interference | 16 | +0.0011 (−0.0135 to +0.0142) | +0.1958 (−0.0250 to +0.6250) |
| F tuning | 28 | **−0.0013 (−0.0026 to −0.0003)** | −0.0179 (−0.0536 to +0.0000) |
| G ragchew | 12 | −0.0023 (−0.0052 to exactly 0) | −0.0183 (−0.0444 to +0.0018) |
| H two-station QSO, oracle | 12 | +0.0001 (−0.0006 to +0.0007) | −0.0028 (−0.0083 to +0.0000) |
| H two-station QSO, oracle (per station) | 24 | −0.0056 (−0.0422 to +0.0236) | −0.1262 (−0.7125 to +0.2818) |
| I Farnsworth | 48 | +0.0018 (exactly 0 to +0.0054) | +0.0208 (+0.0000 to +0.0625) |

**No group has a paired interval entirely above 0** (the plan's rule; not a gate). Group I's interval starts at
exactly 0 (full precision: 0.0 to 0.005433): one regime moves, the 25/13 WPM machine recording 0.0141 → 0.0283
(25/18 paddle 0.0426 → 0.0434); it is reported to the owner as the edge case. Group G is the mirror case: −0.0023 with an interval from
−0.0052 to exactly 0 (the 25 WPM ragchew 0.0464 → 0.0442). F improves at its offsets of 0 to
8.8 Hz (for example 5.9 Hz 25 WPM 0.0636 → 0.0593). The largest single-regime moves: E Δf 100 Hz with the
interferer at +10 dB relative to the wanted signal's key-down power 0.6055 → 0.6697, and at +20 dB relative to it
2.8557 → 2.7732 (E's pooled interval includes 0); A 40 WPM 0.3282 → 0.3333; H
separate-track 100 Hz per station 0.1042 → 0.0525.

CPU per channel-second (Linux machine, one run each, the three runs of this task under the same conditions; not a
gate): `bank-b4b-ref` 42.60, `bank-b4b` **42.27**, `bank-b4b-oldmask` 42.39 ms; per group within 0.7 ms of the
reference. The mask's cost does not depend on the margin (derived); the differences are run-to-run.

Raw outputs (git-ignored): `build/suite/full3/experiments/linux/b4b/` (`compare-bank-b4b-vs-bank-b3b.md`,
`c2-diff-bank-b4b-vs-bank-b3b.md`, `b4b-ref.log`, `b4b-run.log`, the Linux machine's white-noise tables);
`build/b4b/` (the table's runs on both noise sources, the prototype cross-check, the PARIS streams and their
counts, helper scripts); decoded files `build/suite/full3/proto/bank-b4b-ref/`, `bank-b4b/`, `bank-b4b-oldmask/`
on the Linux machine.

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
| `stretch-b4ac` | `--set periodicity_window_mode=shared` | B4a-C's one shared window per decision, re-key settings in dits; first form, superseded (nominal-d_k fallback; 6.6.1) |
| `stretch-b4ac-rekeys` | `--set periodicity_window_mode=shared --set rekey_after_s=0.8 --set rekey_timeout_s=2.0` | B4a-C's shared window, stage 1's re-key settings; first form, superseded (nominal-d_k fallback; 6.6.1) |
| `stretch-b4ac2` | `--set periodicity_window_mode=shared` | B4a-C's final form (T̂ only from an eligible fit), re-key settings in dits |
| `stretch-b4ac2-rekeys` | `--set periodicity_window_mode=shared --set rekey_after_s=0.8 --set rekey_timeout_s=2.0` | B4a-C's final form, stage 1's re-key settings |

Reproduction check: on the 76 channels these runs share with the development set (A 25 WPM and H oracle QSO labels),
the first four runs' decoded records are identical to the earlier runs with the same settings (`bank-b3b`, `bank-b4a`,
`bank-b4a-rekey`, `bank-b4a-windows`): 76 of 76 channels, every record. The two B4a-C runs (fix round 1) used a
replay tool and bench rebuilt in `build/linux-b9` from `b44da0b` (engine code of `db38cd0`, B4a-C), with `--jobs 5`
(`build/b9/b9_fix1.sh`). They have no earlier run to reproduce. They are superseded: that build's T̂ fell back to the
selected branch's nominal dit, which trapped T̂ at 12 ms after a stream's start (6.6.1). The final form's runs,
`stretch-b4ac2` and `stretch-b4ac2-rekeys`, were made in B4a-C's fix round 1 at `ae0188c` (which includes B4b's guard
margin and mask table) with B9's steps, that build's tools and `--jobs 10` (`build/b4c/b9_run_b4c.sh`, a copy of
`build/b9/b9_run.sh`).

### 8.2 The stretch test

Pooled over the 64 pairs (CER of the original signals, of the stretched copies, and paired):

| run | CER original | CER stretched | paired CER (stretched − original) | S₅₀₀ at CER 0.10, original (dB SNR in 500 Hz) | stretched | shift (dB of S₅₀₀; invariance +3.19) |
|---|---|---|---|---|---|---|
| `stretch-b3b` | 0.2968 | 0.4131 | **+0.1166 (+0.0577 to +0.1902)** | +0.16 | +0.65 | **−0.49 (−0.73 to −0.10)** |
| `stretch-b4a` | 0.3117 | 0.4369 | **+0.1260 (+0.0627 to +0.1902)** | +0.79 | +0.69 | **+0.10 (−0.08 to +0.29)** |
| `stretch-rekey` | 0.3088 | 0.4239 | **+0.1161 (+0.0517 to +0.1888)** | +0.37 | +0.64 | **−0.28 (−0.67 to +0.17)** |
| `stretch-windows` | 0.3124 | 0.4196 | **+0.1074 (+0.0550 to +0.1692)** | +1.01 | +0.68 | **+0.33 (−0.21 to +0.58)** |
| `stretch-b4ac` (superseded) | 0.3299 | 0.4210 | **+0.0927 (+0.0297 to +0.1691)** | +1.12 | +0.62 | **+0.50 (+0.30 to +0.73)** |
| `stretch-b4ac-rekeys` (superseded) | 0.3075 | 0.3867 | **+0.0793 (+0.0399 to +0.1236)** | +1.18 | +0.66 | **+0.53 (−1.68 to +1.62)** |
| `stretch-b4ac2` | 0.3044 | 0.4172 | **+0.1134 (+0.0571 to +0.1730)** | +0.41 | +0.59 | **−0.18 (−0.61 to +0.28)** |
| `stretch-b4ac2-rekeys` | 0.2963 | 0.3890 | **+0.0931 (+0.0419 to +0.1508)** | +0.10 | +0.23 | **−0.12 (−0.41 to +0.34)** |

Paired CER by the original's S₅₀₀ step (4 pairs each; intervals in the raw outputs). Steps −10 to −6 dB: +0.000 in
every run (both sides CER 1.000).

| original S₅₀₀ (dB SNR in 500 Hz) | −4 | −2 | 0 | +2 | +4 | +6 | +8 | +10 to +20 |
|---|---|---|---|---|---|---|---|---|
| `stretch-b3b` | +0.008 | +0.304 | +0.878 | +0.595 | +0.052 | +0.010 | +0.003 | −0.001 to +0.008 |
| `stretch-b4a` | +0.052 | +0.210 | +0.852 | +0.821 | +0.038 | +0.035 | +0.025 | −0.010 to +0.006 |
| `stretch-rekey` | +0.051 | +0.118 | +0.822 | +0.838 | +0.025 | +0.004 | −0.002 | −0.003 to +0.004 |
| `stretch-windows` | +0.055 | +0.194 | +0.777 | +0.617 | +0.029 | +0.032 | +0.019 | −0.007 to +0.008 |
| `stretch-b4ac` (superseded) | +0.000 | −0.049 | +0.915 | +0.536 | +0.042 | +0.014 | +0.017 | −0.003 to +0.007 |
| `stretch-b4ac-rekeys` (superseded) | +0.000 | +0.255 | +0.670 | +0.205 | +0.071 | +0.050 | +0.009 | −0.012 to +0.011 |
| `stretch-b4ac2` | +0.004 | +0.296 | +0.854 | +0.637 | +0.023 | −0.001 | −0.000 | −0.003 to +0.005 |
| `stretch-b4ac2-rekeys` | +0.037 | +0.265 | +0.898 | +0.234 | +0.031 | +0.007 | +0.003 | −0.001 to +0.006 |

CER per step, `stretch-b4a` (original / stretched): −2 dB 0.763 / 0.973; 0 dB 0.139 / 0.991; +2 dB 0.041 / 0.863;
+4 dB 0.014 / 0.052. `stretch-b3b`: 0.594 / 0.896; 0.107 / 0.988; 0.018 / 0.613; 0.003 / 0.055. `stretch-b4ac`:
0.992 / 0.943; 0.185 / 1.094; 0.033 / 0.568; 0.009 / 0.050. `stretch-b4ac-rekeys`: 0.637 / 0.893; 0.155 / 0.824;
0.062 / 0.267; 0.015 / 0.086 (both superseded). `stretch-b4ac2`: 0.684 / 0.980; 0.120 / 0.973; 0.023 / 0.660;
0.005 / 0.028. `stretch-b4ac2-rekeys`: 0.613 / 0.875; 0.104 / 1.003; 0.020 / 0.254; 0.005 / 0.036.

First-word CER, paired (stretched − original), pooled: `stretch-b3b` +0.2432 (+0.0877 to +0.4281); `stretch-b4a`
+0.9160 (+0.2456 to +1.6624); `stretch-rekey` +0.1277 (−0.2880 to +0.7020); `stretch-windows` +0.5722 (+0.2832 to
+0.9147); `stretch-b4ac` +1.4482 (+0.2736 to +2.8694); `stretch-b4ac-rekeys` +0.5772 (−0.3542 to +1.5641) (both superseded);
`stretch-b4ac2` +0.1524 (−0.4597 to +0.8608); `stretch-b4ac2-rekeys` +0.2327 (+0.0816 to +0.4225). There is no first-word crossing shift in any run. The original side's first-word CER is above 0.10 at the
highest step, +20 dB, in every run (one or two wrong first words among 4 pairs are enough), so it has no crossing.
Only `stretch-rekey`'s stretched copies cross, at +16.58 dB.

**Answer.** The bank decoder is **not time-base invariant** in any of the six variants, nor in B4a-C's final form
(`stretch-b4ac2`, `stretch-b4ac2-rekeys`).

- At equal energy per dit the stretched copies (12 WPM) decode worse than the originals (25 WPM). Pooled paired CER is
  +0.079 to +0.126, every interval above 0 (final form +0.093 and +0.113).
- The loss sits in the steep part of the curve, at original S₅₀₀ −2 to +2 dB.
- The CER-0.10 crossing shift is −0.49 to +0.53 dB of S₅₀₀ (final form −0.18 and −0.12), against the invariant
  +3.19 dB. So at 12 WPM the decoder needs about 2.7 to 3.7 dB more energy per dit than at 25 WPM.

The variants differ by a fraction of a dB, and the evidence for that is weak:

- `stretch-b4a`'s shift, +0.10 (−0.08 to +0.29), and `stretch-b3b`'s, −0.49 (−0.73 to −0.10), come from two separate
  bootstraps on the same recordings, and their intervals do not overlap. A paired bootstrap of the difference was not
  made.
- The shift intervals are nominal and probably too narrow (8.1).
- The single-part variants, the windows in dits +0.33 and the re-key settings in dits −0.28, do not add up to B4a's,
  and their intervals overlap both endpoints.
- B4a-C's first form had the largest shift, +0.50 (+0.30 to +0.73) with the re-key settings in dits and +0.53
  (−1.68 to +1.62) with stage 1's; that form is superseded (6.6.1). The final form's shifts, −0.18 (−0.61 to +0.28)
  and −0.12 (−0.41 to +0.34), lie between `stretch-b3b`'s and `stretch-b4a`'s and overlap both; the largest shift
  among current runs is `stretch-windows`' +0.33 (−0.21 to +0.58). None of these differences is established
  (separate, nominal bootstraps).
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
| `stretch-b4ac` (superseded) | 0.075 (0.013–0.138), 6 / 80 | 0 / 70 | 0.271 (0.029–0.543), 19 / 70 | 0 / 24 | 15 / 16 |
| `stretch-b4ac-rekeys` (superseded) | 0.088 (0.025–0.163), 7 / 80 | 0 / 70 | 0.229 (0.029–0.443), 16 / 70 | 0 / 24 | 16 / 16 |
| `stretch-b4ac2` | 0.075 (0.025–0.138), 6 / 80 | 0 / 70 | 0.243 (0.029–0.471), 17 / 70 | 0 / 24 | 15 / 16 |
| `stretch-b4ac2-rekeys` | 0.088 (0.025–0.150), 7 / 80 | 0 / 70 | 0.243 (0.057–0.486), 17 / 70 | 0 / 24 | 16 / 16 |

The split changes only `stretch-windows`. One of its nine false new overs was the late find of a 50 Hz turnover,
0.16 s after its first key-down. In every other run no turnover is found late. So group H's false new overs are not
late-found turnovers: the review's conjecture that they might be is refuted by these counts.

Missed turnovers by the answering station's offset (of 14 turnovers each; in the order `stretch-b3b`, `stretch-b4a`,
`stretch-rekey`, `stretch-windows`, `stretch-b4ac`, `stretch-b4ac-rekeys`; then the final form's `stretch-b4ac2`,
`stretch-b4ac2-rekeys`):

- 0 Hz: 1, 4, 1, 3, 1, 1; 1, 1.
- 10 Hz: 0 in every run.
- 25 Hz: 0, 1, 0, 1, 0, 0; 0, 0.
- 50 Hz: 5, 6, 6, 5, 6, 4; 5, 4.
- 100 Hz: 10, 11, 12, 12, 12, 11; 11, 12.

False new overs (of 16 transmissions each, same order):

- 25 Hz: 3, 3, 3, 4, 3, 3; 3, 3.
- 100 Hz: 3, 1, 3, 4, 3, 4; 2, 4.
- 50 Hz: 1, 0, 1, 0, 0, 0; 1, 0.
- 0 Hz and 10 Hz: none.

First-word CER per over:

| run | H, first over (10) | H, after a turnover (70) | pauses, first (8) | pauses, after the same station (16) |
|---|---|---|---|---|
| `stretch-b3b` | 0.150 (0.050–0.300) | 0.427 (0.234–0.627) | 0.063 | 0.000 |
| `stretch-b4a` | 0.500 (0.100–0.951) | 0.499 (0.268–0.739) | 0.000 | 0.000 |
| `stretch-rekey` | 0.050 (0.000–0.150) | 0.501 (0.272–0.740) | 0.000 | 0.000 |
| `stretch-windows` | 0.600 (0.050–1.500) | 0.464 (0.272–0.663) | 0.063 | 0.094 |
| `stretch-b4ac` (superseded) | 0.650 (0.150–1.250) | 0.516 (0.282–0.779) | 0.000 | 0.000 |
| `stretch-b4ac-rekeys` (superseded) | 0.600 (0.050–1.500) | 0.438 (0.245–0.664) | 0.063 | 0.000 |
| `stretch-b4ac2` | 0.050 (0.000–0.150) | 0.510 (0.280–0.763) | 0.000 | 0.000 |
| `stretch-b4ac2-rekeys` | 0.150 (0.050–0.300) | 0.424 (0.228–0.619) | 0.063 | 0.000 |

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

---

## 9. Moved from signal-processing.md, 2026-10-05: Plan B's measurements, checks and tests as section 8c stated them

Moved verbatim on 2026-10-05, when `docs/signal-processing.md` was restructured into a body and an appendix (owner, option B). The text is as it stood at commit 5099a29; its references to sections of signal-processing.md ("section 8", "§8b", "section 11" …) are to that version: the derivations now sit in its appendix under the same numbers (A.6, A.7, A.8, A.8b, A.8c, A.11), and "results record" means this file.

### 9.1 Noise (B3, B4b)

- **Mask bias b_mask,k.** The mask leaves out mostly low-frequency power
  (the station is near 0 Hz), so the masked spectrum reads each branch's
  noise low by a factor that differs per branch and does not cancel in
  the ratio. b_k = (σ²_v,k from the masked, smoothed spectrum) / (true
  σ²_v,k), dimensionless, the segment-weighted mean over the noise
  streams, runs from 0.8281 (k = 1) to 0.7713 (k = 32) at the 4.8 ms
  guard margin (`BankConfig::mask_bias`). Measured (Plan B, B4b: stage
  1's Task 5 method on the C++ estimate, `kz4ap-noise-mask stream`) on
  stage 1's own Task 5 noise: white, 1 FS² per complex sample, numpy's
  `default_rng` seeds 101–110, 60 s each, rounded to complex64 (the
  engine's sample type); 3459 of 3500 segments accepted (2362 at 20 ms),
  kept fraction 0.826 (0.619 at 20 ms); per-seed scatter 2.6% (k = 1) to
  3.9% (k = 32), so the ten-seed mean carries a standard error of about
  0.8% to 1.2% (derived from the scatter). Checks of the method: the same
  streams at 20 ms through the C++ estimate give stage 1's table to
  4.9 · 10⁻⁵ (largest difference; identical at four decimals), and on
  seed 101 at 4.8 ms the C++ estimate's masked branch powers equal the
  prototype's (`guard_margin_s` = 0.0048) to a relative 1.3 · 10⁻¹⁵ with
  equal segment counts (344 of 350 accepted). A second noise source (the
  bench test's: `std::mt19937_64` and Box–Muller, the same seed numbers)
  gives 0.8478 (k = 1) to 0.8000 (k = 32) with ten seeds, up to 2.5
  standard errors of the difference above the table, and 0.8381 to
  0.7812 with 200 seeds (1001–1200; 0.8405 to 0.7849 at 20 ms). So the
  margin lowers b_k by 1.1% (k = 1) to 1.8% (k = 32) on stage 1's ten
  noise streams and by 0.3% to 0.5% on the second source's 200: the
  table's own sampling error is about as large as the margin's effect on
  it (measured). The 200-seed measurement points one way: at 4.8 ms it
  lies above the adopted ten-seed table at every branch (0.8381 against
  0.8281 at k = 1, 0.7812 against 0.7713 at k = 32; 1.2% to 1.5% over
  the 32 branches) and within 0.5% of stage 1's table (−0.5% to +0.1%);
  its standard error is about 0.2% (3% / √200, derived), and at 20 ms
  the same source agrees with stage 1's table (0.8405 against 0.8370,
  0.7849 against 0.7852). So most of the adopted table's shift from
  stage 1's is the sampling error of seeds 101–110, one sign at every
  branch because the branches see the same noise; the margin's own
  effect is about −0.3% to −0.5% (measured). Effect on the estimate, if
  the 200-seed values are the truth: dividing by the adopted table reads
  σ²_v,k 1.2% to 1.5% high, +0.05 to +0.06 dB relative to the true noise
  power, in the absolute level (variant (b), `spectrum-level`, not the
  default); in the default (variant (a)) only b_k / b_1 enters, and
  branch k's σ²_v,k relative to branch 1's reads 0% to 0.25% high, at
  most +0.011 dB (derived from the two tables). The table is unchanged:
  whether to adopt a 200-seed table (stage 1's numpy noise, so the
  method stays stage 1's) and re-run the paired comparison is open, for
  the owner. Stage 1's table (20 ms margin; 0.8370 to 0.7852,
  prototype, the same streams) is kept as `kStage1MaskBias` for the
  golden tests. In channel-shaped noise stage 1's ratios were 2.9%
  (k = 1) to 5.9% (k = 32) higher (measured in the prototype at 20 ms;
  not re-measured at 4.8 ms), so the estimate is about that much low
  there. The table is valid only for the default ladder, T_seg,
  smoothing, guard margin, clean fraction, κ_n and three-tap settings at
  1500 samples/s; it has not been re-measured at other rates.
- **Accepted segments while a station sends (Plan B, B4b; measured).**
  PARIS at 12, 25 and 40 words/min, S₅₀₀ = 20 dB against white noise of
  1 FS² per complex sample, 1 s of noise first, about 60 s of sending
  (the bench's keying, 5 ms edges), counted over the segments starting
  while the station sends (348 to 351 offered, 5.87 per second), C++
  estimate (`kz4ap-noise-mask stream`): accepted 1.70, 1.64 and 0.65
  segments per second at 12, 25 and 40 words/min with the 4.8 ms margin
  (0.78, 0.65 and 0.28 with stage 1's 20 ms), mean kept fraction 38.6%,
  36.1% and 30.8% (21.6%, 16.6% and 13.3%). The noise-free model above
  gives 2.07, 2.53 and 0.82 accepted per second (1.91, 1.69 and 0.59 at
  20 ms; derived); the measured values are lower because noise peaks
  are flagged too (in noise alone the mask keeps 82.6% of samples at
  4.8 ms and 61.9% at 20 ms, measured above). One stream per speed
  (numpy seeds 1012, 1025, 1040), one realization each: about 350
  segments offered per stream, so a binomial standard error of about
  ±0.14 accepted segments per second at 1.7 per second and ±0.10 at 0.65
  (derived, indicative: the segments are correlated through the PARIS
  pattern).
- **The development set (Plan B, B4b; measured with the time constants
  in seconds).** The guard margin and the table were measured on the
  development set with stage 1's re-key wait (0.8 s), time-out (2 s) and
  periodicity windows (2, 5, 10 s), set by the overrides in seconds,
  against `bank-b3b` (the same settings, 20 ms, stage 1's table): the
  owner's instruction (2026-10-04), so that B4b's effect is separate from
  the undecided time constants in dits of B4a; the code's defaults keep
  B4a's dits. 116 of 525 channels decode to a different final text;
  paired CER +0.0004 (−0.0021 to +0.0028), no group's interval entirely
  above 0 (group I, Farnsworth: +0.0018, interval from exactly 0 to
  +0.0054, one regime, 25/13 words/min machine, 0.0141 → 0.0283), group
  F (tuning) better, −0.0013 (−0.0026 to −0.0003); CPU 42.60 → 42.27 ms
  per channel-second (Linux machine). Results record section 7.

**Exact zeros (Plan B, B3; derived).** Exact zeros are missing data: a
receiver's output carries noise, so a run of input samples that are
exactly 0 FS (both components; a zero-padded recording, a dead channel)
is padding, not a measurement of the noise. An exact-zero input sample
enters neither the three-tap warm-up nor the recovery's history or count
below, so a block whose input samples are all exactly 0 FS changes
nothing in the three-tap estimate (not the warm-up, not σ², not W, not
the recovery's count), and in a block that is only partly zero (a run of
zeros starting or ending inside it) only the non-zero samples count: the
warm-up runs on the first 0.32 s of non-zero input samples. A tap counts
only from 2N_k samples after the sample following the latest exact zero
(the stream-start rule above, with that sample as the start): its middle
and newest taps then hold no zero, and its oldest tap's boxcar may reach
back into the zeros, exactly as at a stream's start (where the boxcar
reaches into the zeros before the first sample). So a single isolated
exact zero at sample z blocks branch k's taps at z and at the 2N_k
samples after it (a tap at i counts only if i − z > 2N_k): 2N_k + 1
samples in all, 29 (19.3 ms) for branch 1 (N_1 = 14) and 553 (369 ms)
for branch 32 (N_32 = 276), derived. Before fix round 1 of
the B3 review the rule was per block: a partly zero block's exact-zero
|v_k|² entered the warm-up, whose 20% quantile was then 0, so σ² sat at
the 10⁻²⁰ FS² floor for the first few blocks of noise (4 blocks, 85 ms,
in the zero_pad stream; derived by the reviewer), and middle taps of 0
FS² inside a block were accepted. Tested bit for bit:
`BankNoise.ZerosEndingInsideABlockLeaveTheWarmUpToTheNoise` (the estimate
after 1500 zeros ending inside a block equals the noise alone's, fed in
the same blocks) and the gap's edge blocks in
`BankNoise.AGapOfExactZerosLeavesTheEstimateUnchanged`. Until the first
non-zero sample σ² is unknown (NaN), as before a stream's first block; the channel then keys, observes and publishes nothing ("Channel
decoder", "Exact zeros"). In the spectrum, a segment whose samples are
all exact zeros is not offered, and in any other segment each exact-zero
sample is left out of the mask as a flagged sample is. That per-sample
rule is stricter than leaving out only all-zero segments (accepted by the
controller): a segment straddling the edge of a run of zeros is measured
on its non-zero part (Σw² counts only those samples, so its level is not
read low), it needs 50% non-zero, unflagged samples to enter, and an
all-zero periodogram, hence the former 0/0 ratio, cannot occur.
Without exact zeros every one of these rules gives the prototype's
arithmetic unchanged (the noise goldens and all development-set records
are identical, results record section 5).

Before (Plan A, the prototype's behavior): on exact zeros the warm-up
set σ² = 10⁻²⁰ FS² and, once noise arrived, no tap passed the guard
again (acceptance (1 − e^(−κρ))(1 − e^(−κ_n ρ))² about 10⁻⁵⁴ per tap at
ρ = σ̂²/σ² = 10⁻²⁰/0.036, computed), so σ² stayed there; the spectrum
accepted the all-zero segments, its shape became identically 0 and
variant (a)'s ratio 0/0 = NaN.

**Recovery of a stuck level (Plan B, B3; heuristic).** The same lock-up
follows any rise of the true noise level by a large factor faster than
the estimate follows (at 60 dB, ρ = 10⁻⁶ and the acceptance is about
10⁻¹⁷ per tap; derived from the same formula), for example a band change
or a receiver gain step. Rule: if branch 1 has accepted no middle tap
for `noise_stuck_s` = 8 s (= 4 τ_n) of non-zero input, every branch's σ²
is set again by the warm-up rule, max(Q₀.₂ / (2·(−ln 0.8)), 10⁻²⁰ FS²)
over that branch's last 0.32 s of non-zero input (a ring of 480 |v_k|²
values per branch), W is kept and the update resumes; the firings are
counted (`recoveries()`, written per channel to the replay tool's
decoded files as `noise_recoveries`, with `noise_zero_blocks`). The time
is in seconds, not dits, because the noise process has no keying speed;
8 s is a heuristic. The rule is gated on branch 1 for every method (the
controller's ruling, option (A)): branch 1 is the shortest boxcar
(N_1 = 14 samples at 1500 samples/s). A tap at n reads v[n], v[n − N]
and v[n − 2N], whose boxcars span the 3N_k samples n − 3N_k + 1 … n, so
it lies entirely in noise when 3N_1 = 42 samples (28.0 ms) are free of
the station; a character space, 3 dits = 3.6/WPM s, is 54 samples
(36 ms) even at max_wpm = 100 words/min, so branch 1 sees noise in every
character space at every speed of the ladder (derived, assuming
rectangular keying: the margin at 100 words/min is only 12 samples,
8 ms, which the keying's rise and fall and the channel filter's tails
eat into; the measured fact relied on is that the development set's 525
channels never fire it; element spaces, 1 dit,
suffice up to 1.2 · r/42 = 42.9 words/min). A long branch does not:
branch 32 (N_32 = 276 samples) needs 3N_32 = 828 samples (0.552 s) free
of the station, longer than a word space (7 dits = 8.4/WPM s) above
about 15.2 words/min, so during continuous keying faster than that it
accepts no tap at all. A per-branch rule (the brief's first
form) therefore fired on the noise golden stream (20 s, 25 words/min
keying from 1.0 s) in the "branch" fallback after 8 s and set the long
branches to the station's power, 3 to 4 orders of magnitude above the
noise (measured), which the gate prevents
(`BankNoise.AKeyedStationDoesNotTriggerTheRecovery`). For "spectrum" and
"spectrum-level" only branch 1 has a three-tap estimate, so the gate is
their only form. Measured on the development set (525 channels): no
firing.

Consequences, tested (`engine/tests/bank/noise_test.cpp`):
- A 60 dB rise (`BankNoise.ANoiseRiseOf60dBRecoversByTheDerivedTime`):
  the recovery fires at the first block end at or after the rise
  + 8 s (measured 8.011 s), its 0.32 s window then holds only the new
  noise, and every branch restarts from a 480-sample warm-up estimate of
  the new level. From above (ρ > 1) every tap is accepted and the
  excess decays as e^(−t/τ_n), so within a factor 2 of the new σ²_v,k
  from the rise + 8 s + 0.32 s + 2 τ_n (12.32 s) on, derived given a
  warm-up estimate at most 1 + e² = 8.4 times too high; from below
  (ρ < 1) the guard truncates and the rise is slower and not
  exponential, so no time is derived for it. That condition is
  measured: the test passes in all three methods ("branch" branch 32,
  whose 0.32 s window spans only 1.7 boxcar lengths, restarts at 2.14).
  Variant (b)'s shape follows after branch 1 recovers, with β per
  segment: τ_n ln 2 = 1.39 s to half the new level (derived).
- A continuous carrier longer than 8 s also triggers it: the level is
  then set to the carrier's (an estimate cannot tell a carrier from
  noise by tap acceptance alone) and decays back with τ_n once the
  carrier stops. Derived for a 10 s carrier A² = 10⁴/N_1 FS² (40 dB above
  branch 1's noise power 1/N_1 FS² per complex sample, white noise of
  1 FS²; ρ in units of the noise's σ²_v,1): at the recovery ρ = 2·10⁴ ·
  q/(2·(−ln 0.8)), q = 0.988 (the 20% quantile of |A + n|² is about
  A²(1 − 2·0.8416·σ_v,1/A)), 46.5 dB relative to the noise; for the
  remaining 2 s of carrier every tap is accepted and ρ decays with τ_n
  toward 2·10⁴/(2 m(κ)), so at the carrier's end ρ = 2.63·10⁴
  (44.2 dB relative to the noise; measured 2.62·10⁴); then ρ decays at
  10·log₁₀(e)/τ_n = 2.17 dB (relative to the noise) per s toward
  1/m(κ) = 1.58 (conservative:
  truncation lowers the target near ρ = 1) and reaches 2 after
  τ_n ln((26 290 − 1.58)/(2 − 1.58)) = 22.1 s (measured 1.96 at 22.0 s).
  Branch k of the "branch" fallback starts higher by N_k/N_1 and takes
  τ_n ln(N_k/N_1) longer (28.0 s for k = 32; derived; measured ρ of
  branches 2 to 32 between 1.14 and 1.98 from their t_k + 0.25 s on).
  For the spectrum methods' branches 2 to 32 the factor 2 by
  max_k t_k + 0.25 s = 28.3 s is a **measured margin, not derived**: the
  shape takes the carrier's segments after branch 1 recovers and their
  excess decays with τ_n from at most A²/(2σ²_v,k) (a derived bound on
  that factor alone), but the level and the shape's ratio each within 2
  bound their product only within 4; measured ρ from 1.10 to 1.27
  ("spectrum") and 1.02 to 1.16 ("spectrum-level") from 28.3 s on
  (`BankNoise.ALongCarrierRecoversByTheDerivedTime`, Windows).
- A 5 s gap of exact zeros inside noise leaves σ² unchanged bit for bit
  (`BankNoise.AGapOfExactZerosLeavesTheEstimateUnchanged`); noise after
  5 s of zeros starts the estimate 0.32 s after its first sample
  (`BankNoise.ExactZerosThenNoiseStayFiniteAndPositive`, Plan A's
  disabled requirement, now enabled with "after the warm-up" counted from
  the first non-zero input).

### 9.2 Time constants in dits (B4a): granularity, overrides, the shared variant, tests

At 25 words/min (T = 48 ms) the values are stage 1's within the ladder's
granularity: the branch whose d_k is nearest 48 ms is k = 16 (50.13 ms),
whose W_min,16 = 837.1 ms and time-out 2.093 s are 4.6% above 0.8 s and
2 s; any dit inside the ladder's range is within a factor 1.1^(1/2) =
1.049 of the nearest d_k, and 16.7 is 0.2% above 16.67, so the bound is
a factor 1.051 (derived; tested). The windows at T = 48 ms are 2.002,
4.992 and 9.984 s. After a slow station stops, its branch's false
characters (provisional keying of noise) can stay published up to its
time-out, about 4 s at 12 words/min, inside the 20 s correction reach.
The prototype's values in seconds (0.8 s, 2 s and windows of 2, 5 and
10 s shared by every candidate) are `fixed_timing(cfg, 0.8, 2.0, {2, 5,
10})`, which the module and channel golden tests set explicitly
(`BankChannel`, `BankDecoder` and `Periodicity` take a timing); a shared
window is computed exactly as before (one FFT per window), so those
goldens pass unchanged. Before B4a the three settings were
`rekey_after_s` = 0.8 s and `rekey_timeout_s` = 2 s for every branch and
`periodicity_windows_s` = 2, 5 and 10 s for every candidate.

Overrides in seconds, for ablations (default unset): `BankConfig` keeps
`rekey_after_s`, `rekey_timeout_s` (s; 0 = unset) and
`periodicity_windows_s` (s; empty = unset). When set they take
precedence: `rekey_after_s` gives every branch W_min,k = that value (the
time-out then follows it by `rekey_timeout_ratio` unless
`rekey_timeout_s` is set too), `rekey_timeout_s` gives every branch that
time-out, and `periodicity_windows_s` gives windows of those lengths
shared by every candidate (one FFT each). The replay tool sets them with
`--set`; set to 0.8 s, 2 s and {2, 5, 10} s they reproduce stage 1's
timing bit for bit (tested on three golden streams), and each part acts
alone (tested). Note that branch 1's settings feed the periodicity
estimate: its input p is branch 1's squelched posterior, which depends on
branch 1's amplitude, seeded and re-keyed with W_min,1 (200.4 ms in dits,
0.8 s in stage 1) and its time-out (501 ms, 2 s); so the re-key settings
change the periodicity estimate's input as well as the keying.

A variant of the windows, not the default (Plan B, B4a-C):
`periodicity_window_mode` = `"shared"` keeps N_w in dits but judges every
candidate of a row over one window N_w · T̂, T̂ the selected branch's
eligible fitted dit of its current over, with stage 1's 2, 5 and 10 s
whenever there is none ("Periodicity" below). The replay tool sets it with
`--set periodicity_window_mode=shared`.

Tests at the default timing (`engine/tests/bank/`): W_min,k, the seed's
memory and the time-out in samples at 1500 samples/s for k = 1, 16 and
32 (derived values) and the 25 words/min bound above; the keyer ready
at the block that reaches each branch's own W_min,k; each candidate's
window N = max(16, round(N_w T r_P)) and its reach inside it; each
candidate's score against `comb_estimate` on its own window (above);
the channel's result independent of the split into pushes; and a
12 words/min station (S₅₀₀ = 15.2 dB SNR in 500 Hz) whose branch 23
becomes known by a re-key at 2.752 s of stream time with 1.643 s of
keyed time counted (1.622 s one block before; W_min,23 = 1.631 s),
where stage 1's timing re-keys it at 1.387 s with 0.811 s (measured,
Windows build).

### 9.3 Keying: exact zeros, before and after B3

**Exact zeros (Plan B, B3).** The keyer never sees a NaN σ²: while the
noise estimate has none (only exact zeros so far, "Noise" above) the
channel does not call it ("Channel decoder", "Exact zeros"). A run of
exact zeros after the first estimate is keyed with σ² as it stood
before the run (the noise estimate holds it): |v|² = 0 FS² gives x = 0,
which keys nothing. Before Plan B (the prototype's behavior) σ² could be
NaN here, so a_k, x, g and p were NaN, every comparison with them false,
and nothing was keyed.

### 9.4 Duration fit (B2(a), B2(b)): SLEEF's implementations, the bit-for-bit checks and the cost

- **Evaluation (Plan B task B2(b), exp and log).** The fit's exp and ln
  are SLEEF 3.9.0's vectorized functions (`_u10`: a stated error bound of
  1.0 unit in the last place of the returned value) instead of the C
  library's: every ln s_c² of a class term (on the grid and on the retained
  history), the one-pass sum's e^(x − m) and its ln, and the refinement's
  responsibilities e^(ℓ_c − ℓ_total). They are evaluated on arrays: the
  grid's points in blocks of 256 (per block every class term's s_c², then
  all their ln together, then the terms, then every e^(x − m) of the
  block, then every ln of a sum), the retained history's observations all
  at once. Per value the operations and their order are unchanged; only exp
  and ln are SLEEF's. (A term already known to be −∞, with its median not
  positive or below the bound above, gets no ln s_c²: no change.) The
  implementation is chosen once at run time (`engine/src/bank/vecmath.cpp`):
  SLEEF's "finz" functions (with fused multiply-add) on AVX-512F, 8 doubles
  per call, or on AVX2 with FMA, 4 doubles; on a processor without them its
  "cinz" functions (no FMA) on AVX-512F, AVX or SSE2 (8, 4 or 2 doubles).
  SLEEF states that each family gives the same bits with every instruction
  set, and the tests check it on each machine
  (`BankFitB2b.ExpAndLogAreWithinOneUlpOfTheCLibraryAndConsistentWithinEachFamily`);
  so the fit's values are the same on every processor with AVX2 and FMA
  (most Intel Core processors since Haswell, 2013, and AMD since Excavator,
  2015; not every Pentium, Celeron or Atom-class one), and a processor
  without them gives other last bits. Each instruction set's code is
  compiled in its own file for that set alone (on MSVC the AVX-512 file as
  SLEEF compiles its own: AVX2 code generation, AVX-512F intrinsics), and
  each runs only where the processor reports the extensions its file may
  use, so the program runs on every x86-64 processor: by construction and
  by the symbol check of results record section 4.1, not by a run on a
  processor without AVX. The environment variable `KZ4AP_FIT_MATH`
  (`cinz`, `finz` or a full name) restricts the choice, for the tests and
  for one development-set replay only (results record, section 4.9). The
  order is
  measured, not derived: the 2-double SSE2 functions (cinz) are slower than
  the C library's ln (10.6 against 4.6 ns per value on the Linux machine)
  and made the decoder slower on both machines; the cinz AVX and AVX-512
  versions made it faster on the Linux machine only, the FMA versions on
  both (results record, section 4). The
  last bits of ℓ_total change; the bound and the measured differences are
  under "Bit-for-bit check" below.

**Bit-for-bit check of the restructured evaluation (Plan B task B2(a)).**
`fit_test.cpp` keeps a frozen copy of the fit's evaluation before B2(a)
(logaddexp, the per-observation terms, the grid's log-likelihood, the
refinement and the best-fit search) and compares the bit patterns of every
result: logaddexp on 1.2 · 10⁷ pairs placed at and beside the skip
threshold at every binary exponent, and on the special values (±0, ±∞,
NaN, subnormals); the grid log-likelihood at every grid point for 10⁶
random observations per configuration (durations from 10 µs to 100 s,
beyond the 1 ms and 10 s clamps and one step beside them; σ_t² of 0,
10⁻¹⁴ to 10⁻² s², and 1, 10¹⁰, 10³⁰⁰ s² and +∞), on the default grids and on
grids with medians that are not positive (w/T up to 1.2, T_g/T down to
0.2), and on the golden observations; `best`, `refine`, the weighted
log-likelihood and `class_logliks` on the golden sequence, the fit cases
and 2000 random fits per configuration and number of refinement steps
(0, 2, 5), without and with T_P priors. With the exact steps alone every
comparison was equal (commit `42201d7`; Windows and Linux, full sizes).
Since the near-exact step the tests keep two frozen variants. (1) The
frozen code with only the near-exact step applied (its ℓ_total by the
current one-pass sum, over all five classes): against it the grid's
log-likelihood (including the bounded skip of ln s_c²), `best` with its
quality, `refine`, the weighted log-likelihood and `class_logliks` (ℓ_c,
ℓ_total, s_c²) are compared **bit for bit**, so the exact steps keep a
bit-for-bit guard. (2) The frozen code with the logaddexp chain: against it
only the near-exact difference itself is measured, each ℓ_total (the grid's
and `class_logliks`') within the derived bound 30 · 2^−52 · (|ℓ_total| +
2 nats) (a count of at most 15 roundings per evaluation, each within one
unit in the last place, on quantities of magnitude ≤ |ℓ_total| + 2 nats),
and the weighted log-likelihood at a fixed θ within the bound derived from
those totals' own differences: Σ λ^age |Δℓ_total| plus each side's
rounding of numpy's pairwise sum, (⌈n/8⌉ + ⌈log₂ n⌉ + 4) · 2^−53 ·
Σ |λ^age ℓ_total|, and of the prior's addition. The one-pass sum's
leave-out of terms below 40 nats is tested bit for bit against the same sum
without it (`LogSumExpLeaveOutChangesNoBit`, 2 · 10⁶ cases, the outlier
last and largest included). Non-finite values are compared bit for bit
everywhere. At the full sizes no comparison fails, on both platforms; the
largest differences from the logaddexp chain (Windows / Linux):

| quantity | values | largest difference from the chain | of its bound |
|---|---|---|---|
| grid log-likelihood (ℓ_total at every grid point) | 8.5 · 10⁹ | 1.3 · 10⁻¹⁵ nats (relative 7.5 · 10⁻⁸, at a value near 0 nats) | 3.5% |
| ℓ_total of `class_logliks` | 1.8 · 10⁷ | 8.9 · 10⁻¹⁶ nats | 3.3% |
| weighted log-likelihood at a fixed θ (Σ over ≤ 192 observations, with the T_P prior term) | 4.4 · 10⁵ | 1.8 · 10⁻¹² nats (relative ≤ 1.6 · 10⁻¹³) | 99.1% |

(Windows; the Linux machine's figures are in the results record, section
3.8.) The weighted log-likelihood comes within 1% of its bound (99.6% on
the Linux machine) because the bound's first term, Σ λ^age |Δℓ_total|, is
the difference itself wherever one observation's difference dominates (a
short history, or one term much larger than the others): the bound is
derived and holds by construction, it is not fitted, and it is tight
there. Through the exact
steps (variant 1) `best`, `refine` and every other value are equal bit for
bit, so no acceptance test turns.

**Check of SLEEF's exp and ln (Plan B task B2(b)).** The frozen copy calls
either the C library's exp and ln (the code at B2(a)) or the fit's
(SLEEF's, one value at a time). (1) Against the frozen one-pass code with
the fit's exp and ln, every comparison of the check above is **bit for bit**
(the blocks, the padded last vector and the order of operations change no
value). (2) Against the frozen one-pass code with the C library's, each
ℓ_total (the grid's and `class_logliks`') must lie within the derived bound
1.001 · 2^−52 · (5 |ℓ_total| + 14.3 + |ln σ_ln²|) nats (18.1 nats for the
constant with σ_ln = 0.15; 7.4 · 10⁻¹⁵ nats at ℓ_total = −3 nats), and the
weighted log-likelihood at a fixed θ within the bound above from those
totals' differences. The derivation (in `fit_test.cpp`, `sleef_bound`),
with every exp and ln within 1 ulp of the exact value (SLEEF's stated
bound; assumed, not proven, of the C libraries): the difference is each
side's evaluation error of its own terms plus the exact log-sum-exp's
change between the two sides' terms. Each side's one-pass evaluation errs
by at most 4.44 · 2^−52 + 2^−53 |ℓ_total| (the sum S ∈ [1, 4] off by at
most 3.05 · 2^−52 relative after ≤ 3 subtractions, exps and additions; its
ln by 2^−52 ln 4 more; the final addition). The log-sum-exp moves by the
responsibility-weighted mean of the terms' changes, Σ p_c |Δx_c| with
Σ p_c ≤ 1; a term x = (a − ½ ln s²) − ln √(2π) changes by at most
2^−52 (|ln s²| + 2|x| + 1), as the two ln s² differ by at most 2 ulp, and
|ln s²| ≤ 2|x| + |ln σ_ln²| (ln((1 − ε) P_c) < 0 and z²/s² ≥ 0, else
s² ≥ σ_ln²); with p |x − ℓ_total| = p |ln p| ≤ 1/e per class this gives
2^−52 (4 |ℓ_total| + 5.42 + |ln σ_ln²|). The factor 1.001 covers the
second-order terms. (3) Against the logaddexp chain each ℓ_total within the
sum of the two bounds. Each implementation the processor can run is checked
against `std::exp` and `std::log` on 10⁶ arguments each (the sum's r in
[−40, 0] nats and the leave-out's edge, the responsibilities down to
subnormal results and 0, overflow, subnormal and edge arguments, ±0, ±∞,
NaN): at most 1 ulp apart, every element bit for bit as when evaluated
alone, and bit for bit within its family (finz AVX-512 = finz AVX2 and
cinz AVX-512 = AVX = SSE2 on the Linux machine; cinz AVX = SSE2 on the
Windows PC, which has no AVX-512). Of the test's 10⁶ arguments (its own mix,
not the fit's arguments) about 3.7% of the exps (finz; 5.8% and 6.0% cinz,
Windows and Linux) and 0.5% of the lns differ from the C library's, by
1 ulp, on both machines. At the
full sizes every bit-for-bit comparison is equal on both machines, and the
largest differences from the B2(a) code are (Linux machine / Windows PC):

| quantity | values | largest difference from the B2(a) code | of its bound |
|---|---|---|---|
| grid log-likelihood | 8.5 · 10⁹ | 1.8 · 10⁻¹⁵ / 1.8 · 10⁻¹⁵ nats | 21.7% / 21.7% |
| ℓ_total of `class_logliks` | 1.8 · 10⁷ | 1.8 · 10⁻¹⁵ / 1.8 · 10⁻¹⁵ nats | 20.0% / 19.9% |
| weighted log-likelihood at a fixed θ | 4.4 · 10⁵ | 4.6 · 10⁻¹³ / 1.1 · 10⁻¹³ nats | 31.9% / 40.5% |

Against the logaddexp chain (both steps together) the grid's ℓ_total
differs by at most 2.2 · 10⁻¹⁵ nats (4.8% of the summed bound). The
development-set texts and every decoded record are unchanged (results
record, section 4).

(The default test run uses 20 000 observations and 100 fits; the full sizes
run as the ctest entry `BankFitB2a.FullSweep`, label `full-sweep`, which the
default test presets exclude: `ctest --preset windows-full-sweep` or
`ctest --preset linux-full-sweep`, about 41 min on the Windows PC and 6 min
on the Linux machine since B2(b), whose frozen copy runs three variants.) The port's check against the plain formula
(`FastPathsAreBitIdenticalToThePlainFormulas`) now allows each grid value
the same derived bound 30 · 2^−52 · (|v| + 2 nats) (6.7 · 10⁻¹⁵ · (|v| + 2)
nats, against the 1.3 · 10⁻¹⁵ nats measured in the full sweep) plus, since
B2(b), SLEEF's bound above, and each table entry an allowance accumulated as
the table is: λ × its allowance + the value's + 2^−51 |entry|; on its 414
observations 657 286 of 2 007 072 grid values differ (Windows), by at most
1.8 · 10⁻¹⁵ nats, and 1410 of the 9696 table entries, by at most
4.3 · 10⁻¹⁴ nats, each within its allowance (after B2(a) alone 654 897,
8.9 · 10⁻¹⁶ nats, and 1411).

**Cost (Plan B task B2(a), measured).** CPU per channel-second of the bank
decoder (`kz4ap-bank-replay`, development set, 525 oracle channels,
74 749.9 channel-seconds, Linux machine, 10 threads): 127.91 ms before
Plan B, 123.96 ms after B1, 104.60 ms after B2(a)'s exact steps,
75.84 ms after its near-exact step as first committed, and **66.67 ms**
with the sum started at the largest term's 1 (fix round 1, which also
saves the exp(0) of the largest term; −47.9% against 127.91 ms). On the
Windows PC (F-drift-s1, 8 channels, 240.1 channel-seconds, 8 threads, two
runs each, alternated in one session): 303.3 and 314.4 ms before, 166.0
and 170.5 ms after the exact steps, 76.5 and 78.4 ms after the near-exact
step as first committed; 67.0 and 68.6 ms with fix round 1 (a later
session, not alternated with the reference). A gprof profile of one channel
(F-drift-s1, label 1, 30 s; Linux, statically linked so that libm is
sampled) falls from 3.70 s to 1.98 s; the scalar libm functions from
2.67 s (log1p 1.13 s, exp 1.19 s, ln 0.30 s, pow 0.05 s) to 1.19 s
(log1p 0, exp 0.44 s, ln 0.73 s, pow 0.02 s). The decoded text and every decoded record are unchanged on all
525 channels (results record, section 3).

**Cost (Plan B task B2(b), measured).** With SLEEF's exp and ln (finz
AVX-512 on the Linux machine) the bank decoder's CPU per channel-second on
the development set (as above) falls from 66.67 ms to **42.37 ms** (−36.4%;
−66.9% against 127.91 ms before Plan B; replay wall time 502 s to 319 s).
On the Windows PC (F-drift-s1, finz AVX2, five runs of each build alternated
in two sessions) the medians are 74.5 ms (B2(a)) and 66.2 ms (B2(b)),
−11%, with a run-to-run spread of up to 29 ms. The gprof profile of one
channel (as above) falls from 2.04 s (at the start of B2(b)) to 1.27 s: the
C library's ln and exp from 1.17 s to 0.10 s (ln outside the fit's arrays:
ln d of the retained history, ln μ_c, other stages), SLEEF's ln 0.15 s and
exp 0.08 s; `grid_loglik`'s own code, which now contains the one-pass sum
and the staging of the blocks, rises from 0.49 s to 0.58 s and is the
largest item (46%). Memory: the blocks use 25 856 B of stack scratch per
call and the search two vectors of up to 3 × 192 doubles (4608 B each) per
call, freed on return; nothing persistent is added. Build: SLEEF adds
4.6 s to a fresh configure and 3.0 s to a fresh build on the Linux machine
(Ninja, 10 jobs), 29.1 s and 40.4 s on the Windows PC (results record,
section 4.6). With the cinz family forced (`KZ4AP_FIT_MATH=cinz`, cinz
AVX-512, what a processor without FMA computes) the development set costs
45.90 ms per channel-second and decodes to the same texts and records
(results record, section 4.9).

### 9.5 Periodicity (B4a, B4a-C): the sliding sums and their measured agreement and cost; the shared-window variant with its history, tests and measurements

- **Autocorrelation.** Over one window's N samples,
  x = p − mean(p) and the biased, normalized autocorrelation
  ρ[τ] = Σ_{m=0}^{N−1−τ} x[m] x[m+τ] / Σ_m x[m]², τ = 0 … N − 1
  (dimensionless; 1 at τ = 0), computed by FFT zero-padded to the
  smallest power of two ≥ 2N (so the circular correlation equals the
  linear one). No estimate when N < 16 or Σ x² ≤ 10⁻¹² · N (p does not
  vary; for instance all zero while the squelch is closed). With a
  window per candidate the same quantity is computed per candidate
  without an FFT: for a band of lags [lo, hi] (a tooth, below) over the
  window [s, e) of N samples, with S the running sum of p and m the
  window's mean,
  Σ_{τ=lo}^{hi} Σ_m x[m] x[m+τ] = F + tail − m (A + B) + m² (L N − (lo + hi) L / 2),
  L = hi − lo + 1, where F = Σ_{i=s}^{e−1−hi} p_i (S(i+hi+1) − S(i+lo))
  holds the samples whose every lag of the band is inside the window,
  tail = Σ_{i=e−hi}^{e−1−lo} p_i (S(e) − S(i+lo)) those whose lags reach
  its end, A = Σ_τ (S(e−τ) − S(s)) and B = Σ_τ (S(e) − S(s+τ)), and
  Σ x² = Σ p² − N m² (derived: the expansion of x = p − m). F is kept per
  window, candidate and band, and slid from one recomputation to the
  next: the products entering at the window's end are added (computed
  once per candidate, shared by the three windows), those leaving at its
  start subtracted; it is recomputed directly the first time, once the
  window has slid by its own length N, and whenever it is not finite.
  S, Σ p² and a count of non-finite samples are recomputed over the
  buffer at each recomputation, from the buffer's first sample (so their
  size is that of the buffer's sums, not of the stream's); a NaN or
  infinity is held there as 0 and counted, and a candidate whose window
  holds one has no score (as the FFT form, whose mean is then NaN). The
  slid sums differ from the FFT form by rounding only: on 60 s of keyed
  posteriors at five speeds with noise, pushed block by block, every
  candidate's score equals `comb_estimate` on its own window to a largest
  absolute difference of 1.3 · 10⁻¹³ (measured, Windows build; the test's
  bound is 10⁻¹², scores are of order 10⁻² to 1). Cost, derived from the
  operation counts at 750 samples/s, 192 samples per recomputation and
  303 candidates × 9 bands: entering 303 · 9 · 192 = 0.52 M
  products, leaving 3 × 0.52 M, the direct recomputation at most 9 N per
  N samples of sliding (3 × 0.52 M per recomputation on average), and
  the bands' tails and S-sums 5 L per candidate and band (0.26 M): about
  3.9 M multiply-adds per recomputation, 15 M per channel-second at
  3.9 recomputations per second (the FFT form: three FFTs of 4096 to
  16 384 points per recomputation). Measured with the rest of Plan B's
  B4a on the development set (Linux machine): the bank decoder's CPU
  42.61 → 57.56 ms per channel-second, about +15 ms in every group
  (results record, section 6.3).
- **Variant: one window per row shared by every candidate (Plan B,
  B4a-C; an experiment, not the default).** `periodicity_window_mode` =
  `"shared"` (default `"per_candidate"`, the form above; the override
  `periodicity_windows_s`, when set, wins over either mode). Each of the
  three rows has one window, the same for every candidate:
  N = max(16, round(N_w · T̂ · r_P)) averaged samples, N_w ∈ {41.7, 104,
  208}, where T̂ (s) is the fitted dit T of the branch currently selected
  (by the last selection instant, an earlier block), and only when (a) a
  selection exists, (b) that branch's fit belongs to its current over
  and (c) the fit is eligible (selection's test: memory ≥ 8 elements and
  |ln(L_k / (0.8 T))| ≤ ln 1.1). Otherwise there is no T̂ and the rows
  are stage 1's windows in seconds, `periodicity_unselected_windows_s` =
  2, 5 and 10 s (1500, 3750 and 7500 samples at 750 samples/s). Condition
  (b) is the decoder's own turnover: a new over starts on a branch when
  its key has been up for longer than T_new = max(0.5 s, 12 T_g) since
  its last key-up (`Branch::new_over_due`, `start_over`); from then the
  branch decodes with the previous over's fit (`prev_fit`) until the
  over's first marks are re-keyed (W_min,k of keyed time, or a time-out
  that keys marks with the previous amplitude), when the fit becomes the
  over's own (fresh, or the previous one continued with this over's
  marks). The decoder does not tell a new station from the same station
  resuming: every pause longer than T_new withholds T̂ until the re-key.
  So T̂ is withheld at the stream's start until the selected branch has
  an eligible fit, and from every over start on the selected branch until
  that over's re-keyed fit is eligible; a switch to another branch gives
  that branch's T̂ under the same conditions. (Fix round 1, controller's
  ruling 2026-10-05: the first form fell back to the selected branch's
  nominal dit d_k without an eligible fit; as the selector starts on
  branch 1, that made T̂ = d_1 = 12 ms from branch 1's first key-up,
  about 1.1 s into a stream, until the selector's first switch, with
  windows of 0.50, 1.25 and 2.50 s whose reach stops at 27.3, 68.1 and
  136.3 ms, i.e. stations slower than 44 words/min out of the shortest
  row's reach: a start-up trap. Its runs are superseded; results record
  section 6.6.) On the golden streams (25 words/min clean, turnover and
  two speeds; 12 words/min slow) the first selection comes at 1.11 to
  1.15 s and the first T̂ at 2.82 to 3.67 s (measured). T̂ is capped at
  the longest fitted dit an eligible fit can have, L_32 / 0.8 · 1.1 =
  0.2530 s at 1500 samples/s (L_32 = 276 samples / 1500 samples/s,
  realized; derived), and the buffer keeps 208 times that, 39 468
  samples (52.6 s, 316 kB of doubles) at 750 samples/s, instead of
  37 789. Each row is then exactly the shared form, `comb_estimate` over
  its most recent N samples (one FFT per row, zero-padded to the power of
  two ≥ 2N: 4096, 8192 and 16 384 points at T̂ = 48 ms; 16 384, 65 536
  and 131 072 at the cap), and the reach rule applies as in stage 1: a
  candidate counts in a row only if 9.15 T ≤ (N − 1) / 2, so T ≤ about
  N_w T̂ / 18.3: 2.28 T̂ in the shortest row, 5.68 T̂ and 11.4 T̂ in the
  others (derived). An alias at 3 T̂ therefore cannot be the shortest
  row's estimate; the other two rows still score it, over the same window
  as T̂ itself. T_P, the confidence and the reported window follow the
  rule above (the shortest confident row; its window N / r_P). Why: with
  a window per candidate, an alias at 3 T₀ is scored over three times the
  samples of the true T₀, and the per-window cap T ≤ W / 18.3 is gone;
  the shortest window's confident wrong estimates near 3 T₀ rose from
  3.7% (`bank-b3b`) to 26.3% (`bank-b4a`; 26.0% with the windows alone,
  `bank-b4a-windows`; results record sections 6.5 and 6.6). Class (1)
  dits, through T̂. Status: heuristic, an experiment (owner, 2026-10-04:
  measured as a variant, no default changed). **It introduces a feedback
  the default does not have:** the selected branch's speed sets the
  windows that judge T_P, and T_P feeds every fit's prior (and so the
  fitted T that becomes T̂) and selection's fallback (which chooses the
  branch whose dit becomes T̂). A wrong selection with an eligible fit
  can therefore shorten or lengthen the windows that would correct it;
  when the selected dit is too long, the shortest row's reach admits
  candidates up to 2.28 times that dit. The default form has no such loop
  (its windows depend on the candidate alone). Tests
  (`engine/tests/bank/periodicity_test.cpp`, `channel_test.cpp`): in this
  mode each row's estimate equals `comb_estimate` on its most recent N
  samples (by construction; the test's content is the window length,
  computed independently as T̂ changes, and the reach: the shortest row's
  estimate < 2.3 T̂); without T̂ the estimates equal stage 1's windows'
  bit for bit; T̂ at the cap; on four golden channel streams, at every
  block, T̂ equals the rule above evaluated on the state before the block
  (both transitions occur: withheld after the first selection until the
  first eligible fit, and withheld from an over start on the selected
  branch until its re-key), every filled row's window is N_w T̂ or stage
  1's, and on the clean 25 words/min stream every T̂ is between 40 and
  58 ms (never d_1).
  Measured on the development set (Linux machine, results record
  section 6.6.1; build with B4b): CPU 45.53 ms per channel-second
  pooled, comb precision at 0.03 0.801 (default 0.753), near-3T₀ share
  of the shortest window's confident wrong estimates 0.7% (default
  26.3%), paired CER against `bank-b3b` +0.0045 (−0.0022 to +0.0113),
  and −0.0206 (−0.0286 to −0.0128) against the per-candidate windows at
  the same build. The first form's runs (section 6.6) are superseded.

### 9.6 Channel decoder: memory (B1, B4a, B4b) and exact zeros (B3)

- **Memory.** The channel keeps a window of u and of |v_k|² (FS²) back to
  the earliest sample a later block can read: the re-key's 20 s plus the
  noise estimates' look-back (3 N_max, a segment with its mask's reach,
  the warm-up) and 2 blocks, 31 642 samples (21.1 s) at 1500 samples/s
  (31 688 with stage 1's 20 ms guard margin; Plan B, B4b);
  its storage holds up to 2 s more (34 675 samples) and is moved forward
  when full (a bound chosen for the port, not a tuned value). The |v_k|²
  values are rounded to float32 when computed (the prototype's rounding,
  "Port check" below) and stored as 4-byte floats, which is lossless; every
  read converts them to double exactly (Plan B task B1). Memory per
  channel at 1500 samples/s, counted from the arrays the code allocates
  (derived, not measured): the |v_k|² window 32 × 34 675 × 4 B = 4.4 MB;
  the u window 34 675 × 16 B = 0.55 MB; the cumulative-sum ring
  277 × 16 B = 4.4 kB; per duration fit its two tables
  (3636 + 6060) × 8 B = 77.6 kB and its retained history (≤ 192 marks and
  spaces, 24 B each, 4.6 kB), 82.2 kB. Each branch holds one to three fits
  (the decoding fit, the previous over's, the rival), so the fits take
  32 × 82.2 kB = 2.6 MB to 96 × 82.2 kB = 7.9 MB, and a channel 7.6 MB to
  12.9 MB in all, plus small per-character and per-record lists. The
  periodicity estimate (Plan B, B4a: a window per candidate, the longest
  208 · 242.2 ms = 50.4 s) keeps its buffer of averaged p, at most
  37 789 + 192 samples × 8 B = 304 kB (60 kB with the former 10 s
  window), and per recomputation, kept allocated, the buffer with
  non-finite samples held as 0, S and Σ p² over it (3 × 304 kB) and the
  non-finite count (4 B per sample, 152 kB), plus the slid sums and the
  per-candidate terms ((3 + 3) × 303 × 9 × 8 B = 131 kB) and the bands
  (22 kB): about 1.5 MB per channel (derived), where the former 10 s
  window held 60 kB and about 0.6 MB of FFT work arrays during a
  recomputation; so a channel 9.1 MB to 14.4 MB in all. Measured by the
  method below: 32.16 MB per channel in flight before B4a, 33.29 MB after
  (+1.13 MB; derived +0.9 MB to +1.5 MB). The
  noise estimate keeps its own last 480 |v_k|² of non-zero input per
  three-tap branch for the warm-up and the recovery (Plan B, B3): 1 × 480
  × 8 B = 3.8 kB with the default "spectrum" method (branch 1 only),
  32 × 480 × 8 B = 123 kB with "branch" (derived); the window's look-back
  still counts the warm-up, which the estimate no longer reads from the
  window (kept: a bound, and the stated sizes stay). The fit's
  grid constants (ln μ, 1/μ², a validity flag per class and grid point:
  432.7 kB with the defaults) are immutable and held once per process per
  configuration: every fit built from a configuration with the same values
  of the fields they read (`min_wpm`, `max_wpm`, `t_grid_step`, the q, w
  and T_g grids, `outlier_prior`, `outlier_range_s`, `sigma_ln_mark`,
  `sigma_ln_space`, `fit_memory`, `prior_sigma_ln`, `refine_iterations`;
  compared bit for bit) shares one copy, which is freed when no fit uses
  it. Before Plan B task B1 the window took 8.9 MB (8-byte doubles) and
  every fit started afresh allocated its own grid constants (0.515 MB per
  fit), 26 MB to about 59 MB per channel (derived). **Measured** (Linux
  machine, `kz4ap-bank-replay` on G-ragchew-s1: 12 channels of 366.0 s,
  peak resident set size from `/usr/bin/time -v`, per channel in flight
  (RSS at 10 threads − RSS at 1 thread) / 9): 63.3 MB before B1, 32.1 MB
  after (−31.1 MB, against −18.3 MB to −46.0 MB derived). The measured
  figure includes what the replay tool holds per channel besides the
  decoder, chiefly the channel's recorded stream and its mixed copy
  (2 × 549 000 samples × 16 B = 17.6 MB, derived); less the 17.6 MB it is
  45.7 MB before (derived 26 MB to 59 MB) and 14.6 MB after (derived 7.6 MB
  to 12.9 MB), so after B1 the measurement lies **1.7 MB above** the
  derived upper end. The cause of that excess is **conjectured, not
  traced**: the channel's decoded record held until its test case is
  written and the allocator's overhead are not in the derived count;
  per-thread allocator arenas, or more than three fits per branch at some
  moment, are untested alternatives. The check is weak in both directions:
  the derived range before B1 is wide enough to contain its measurement,
  and the 17.6 MB subtracted is itself derived, not measured.
  Sample indices are 64-bit throughout (the noise estimates included:
  tested with indices past 2³¹, as after 16.6 days at 1500 samples/s).
- **Exact zeros (Plan B, B3).** Exact zeros are missing data ("Noise").
  While the noise estimate has no first estimate (only exact zeros so
  far) a block is not keyed, nothing enters the periodicity or the
  branches, nothing is published, and every branch's clocks (over start,
  start of the unknown amplitude, re-key time-out) restart at the
  block's end: the first block with other input finds the channel as a
  stream's first block does (its noise estimate taken from that block's
  non-zero samples only, "Noise"). A stream that starts with 1 s of exact zeros
  and then a station therefore decodes as the station alone: the same
  text and characters, times shifted by 1 s (measured within 0.67 ms;
  the block grid moves by 4 samples relative to the station, 1500
  samples being 46.875 blocks), no NaN in any published value
  (`BankChannel.LeadingExactZerosDecodeAsTheStationAlone`, at the
  prototype's time constants and at the default ones in dits). Before Plan B
  (the prototype's behavior) the noise estimate went NaN there and
  nothing was keyed or published.

## 10. The owner's decisions of 2026-10-06 (B4d)

The owner decided three things on 2026-10-06 (stage-2 spec, decision record section 6, amended in 66c8ffb and
cb6dc15). The analysis behind them is `docs/research/2026-10-05-periodicity-windows-and-rekey-analysis.md`. Code:
`a360361`. `docs/signal-processing.md` covers the change in body section 8c and in appendix A.8c and A.10. Every
number here is measured unless marked otherwise.

### 10.1 What changed

| Part | Before (B4a/B4b defaults) | B4d default | Class | Status |
|---|---|---|---|---|
| A. Periodicity windows | N_w · τ_c per candidate, N_w = 41.7, 104, 208 | 2, 5 and 10 s for every candidate (`periodicity_window_mode` = "seconds", `periodicity_windows_s`) | (2) seconds: a latency after a change (owner) | values placeholder (stage 1's) |
| B(ii). Time-out count | from the amplitude becoming unknown | from the branch's first provisional mark (`rekey_timeout_from_first_mark`) | — | heuristic (owner's rule) |
| B(lead). Stretch start | where the amplitude became unknown | at the first provisional mark, 7 · d_k before it, if later (`rekey_lead_dits` = 7) | (1) dits | heuristic (controller's choice for the owner) |
| B(i). Cleared time-out | restarts only the count | also moves the stretch's start to its instant (`rekey_clear_moves_stretch`) | — | heuristic (owner's rule) |
| C. Mask bias b_mask,k | 0.8281 … 0.7713 (ten seeds, stage 1's noise) | 0.8381 … 0.7812 (200 seeds, 1001–1200, `std::mt19937_64` and Box–Muller, B4b's measurement, not numpy noise as 7.1's option (b) proposed; section 7.1) | — | measured (Plan B B4b, white noise, 200 seeds) |

The per-candidate windows (B4a) and the shared window (B4a-C) remain as variants, selected with
`periodicity_window_mode`. `periodicity_windows_s` is no longer an override. It is the default mode's field again, the
prototype's field and values. Both clock switches off give stage 1's rule exactly. `stage1_config()` sets them off, so
the golden tests are unmoved. Under (ii) with the lead, switch (i) is nearly inert: after a time-out that clears
nothing, (ii) stops the count, and the next provisional mark moves the stretch's start to max(start, mark − 7·d_k)
whatever (i) says, so (i) changes the outcome only when that mark comes within 7·d_k of the clear (derived from the
code; B4d review). The two are switches of one rule, not independent fixes; only both off was measured (10.3).

**Why the lead.** As first briefed, fixes (i) and (ii) worked against each other at a stream's start. Under (ii) no
time-out fires before the first provisional mark. In the noise before a station nothing is keyed, so (i) never moved
the stretch's start, and the re-key still keyed the pre-station noise at the seed a ≈ 5. Test: 1 s of noise at
S₅₀₀ = +2 dB (dB SNR in 500 Hz) before a 25 WPM station, seeds 1 to 8. Characters published starting more than half a
dit (24 ms) before the station's first mark, at any time:

| setting | characters (8 seeds) |
|---|---|
| (i) and (ii), no lead | 25 (6 seeds) |
| (i) only | 0 |
| (ii) only | 26 |
| neither (stage 1's clocks) | 26 (7 seeds) |
| (i), (ii) and a phantom time-out before the first mark (an experiment, not adopted) | 4 (2 seeds) |
| **(i), (ii) and the lead 7 · d_k (adopted)** | **1 (seed 8)** |
| (i), (ii) and a lead of 6, 5, 4, 3, 2, 1 or 0 d_k | 1 each (seed 8) |

No lead from 0 to 7 d_k gives 0. The one remaining character (seed 8) is an "E" at 0.971 s, 29 ms before the
station's first mark at 1.000 s, and branches 1 to 3 all decode it. It is a mark that the unknown-amplitude test
keyed in the noise just before the station. That mark is itself the over's first provisional mark, so it starts the
clocks, and no lead can exclude it. With lead 0 the character starts at 0.972 s, the mark's own key-down. The
adopted test therefore asserts a bound that holds: no character starts more than 2 dits (96 ms) before the station's
first mark (test bound, heuristic). The strict count is printed, and the case is stated as a known limitation in A.8c.

**The later-over test** (the trace's second mechanism). A 30 WPM station on branch 14 (W_min,14 = 0.692 s,
time-out 1.730 s) sends a second over at amplitude 3 FS (the first was at 1 FS). The second over starts 1.2 s after the
branch's amplitude became unknown.

- With B4d's clocks the branch re-keys at its own seed: it is known at 12.501 s, with 0.707 s keyed (0.686 s one block
  before) and s² = 9.50 FS².
- With stage 1's clocks a time-out fires first: the branch is known at 12.181 s, with 0.434 s keyed, and re-keyed at the
  first over's amplitude, s² = 0.85 FS².

**Tests** (`engine/tests/bank`):

- `BankChannelClocks.NoiseBeforeAStationIsNeverPublished`, seeds 1 to 4. Stage 1's clocks fail it on seed 1.
- `BankChannelClocks.ALaterOverReKeysAtItsOwnSeed`.
- `BankChannelClocks.TheStretchStartsSevenDitsBeforeTheFirstProvisionalMark`.
- `BankPeriodicitySeconds.TheDefaultWindowsAreStageOnesTwoFiveAndTenSeconds`: the default mode is bit for bit the
  explicit {2, 5, 10} s estimator over 60 s.
- The split test runs both the default and the per-candidate variant.
- The per-candidate tests select that variant explicitly.
- `bank_json`: bool fields and the three new fields.

The two clock tests went from about 120 s to about 30 s on the Windows PC, by trimming seeds and settings.

Results:

- `ctest --preset windows`: 359 of 359 passed or skipped. `ctest --preset linux`: 359 of 359.
- Python tests: 269 passed, 7 expected failures.
- Smoke unchanged: Envelope CER 0.0353, Matched 0.0436.

### 10.2 `bank-b4d` against `bank-b3b` (Linux machine, the development set)

Texts: **408 of 525 channels decode to a different final text** (117 identical). The periodicity records differ in
28 802 of 291 690 (T_P or its window). The windows are stage 1's again, but their input, branch 1's posterior, now
depends on branch 1's re-key settings in dits.

| group | signals | paired CER | paired first-word CER |
|---|---|---|---|
| all | 509 | **+0.0051 (−0.0014 to +0.0112)** | **+0.2081 (+0.1297 to +0.2902)** |
| A sensitivity | 192 | +0.0018 (−0.0044 to +0.0083) | +0.2153 (+0.0876 to +0.3768) |
| B fading | 30 | **+0.0177 (+0.0026 to +0.0375)** | +0.3489 (+0.0856 to +0.6809) |
| C fists | 135 | **+0.0022 (+0.0004 to +0.0040)** | +0.1172 (−0.0077 to +0.2274) |
| D speed | 12 | −0.0002 (−0.0121 to +0.0113) | −0.0500 (−0.3750 to +0.2500) |
| E interference | 16 | +0.0143 (−0.0092 to +0.0378) | +0.2583 (−0.8068 to +1.2500) |
| F tuning | 28 | +0.0035 (−0.0102 to +0.0168) | +0.5625 (+0.1321 to +1.0398) |
| G ragchew | 12 | −0.0039 (−0.0099 to +0.0007) | −0.0195 (−0.0594 to +0.0136) |
| H two-station QSO, oracle | 12 | +0.0040 (−0.0046 to +0.0169) | +0.0578 (−0.0129 to +0.1431) |
| H two-station QSO, oracle (per station) | 24 | **+0.0663 (+0.0159 to +0.1483)** | +0.1853 (−0.0970 to +0.5309) |
| I Farnsworth | 48 | −0.0107 (−0.0544 to +0.0316) | +0.2938 (+0.1364 to +0.4629) |

**Groups made worse (paired interval entirely above 0), reported to the owner** (the plan's rule; not a gate).
Regimes, CER of the test case, `bank-b3b` → `bank-b4d`:

- **B:** the mixed-style fading recording, 0.579 → 0.594 (first-word CER 0.72 → 1.01).
- **C:** small, spread over keying styles. Bug imbalance +0.1: 0.046 → 0.055. Computer +0.1: 0.006 → 0.013.
  Paddle +0.0: 0.028 → 0.035. Hand +0.1: 0.199 → 0.208. Computer ±0.0 and −0.1 and bug +0.0 improved.
- **H per station:**
  - Separate-track 100 Hz: 0.104 → 0.392.
  - Ambiguous 50 Hz: 0.585 → 0.670.
  - Same-track 25 Hz: 1.336 → 1.375.
  - The separate-track 100 Hz regime is volatile. B4b's table change alone moved it 0.104 → 0.053 (7.3).

The pooled first-word loss is about what the re-key settings in dits cost in B4a's ablation: `bank-b4a-rekey`,
+0.1922. Group A's first words are worse at every speed: 12 WPM 0.39 → 0.46, 25 WPM 0.39 → 0.57, 40 WPM 0.64 → 0.72.

### 10.3 `bank-b4d-noclock`: the clock fixes separated

`bank-b4d-noclock` is the same binary with `--set rekey_clear_moves_stretch=false --set
rekey_timeout_from_first_mark=false`. The lead acts only under (ii), so it is unused here. This run differs from
`bank-b3b` by the re-key settings in dits and the 200-seed table, with windows in seconds as in `bank-b3b`.

| group | `bank-b4d-noclock` − `bank-b3b` | first-word | `bank-b4d` − `bank-b4d-noclock` (the clock fixes) | first-word |
|---|---|---|---|---|
| all | **+0.0094 (+0.0019 to +0.0176)** | **+0.1702 (+0.0629 to +0.2984)** | −0.0043 (−0.0125 to +0.0034) | +0.0379 (−0.0614 to +0.1205) |
| A sensitivity | +0.0051 (−0.0046 to +0.0188) | +0.2699 (+0.0622 to +0.5641) | −0.0033 (−0.0177 to +0.0072) | −0.0547 (−0.2423 to +0.0966) |
| B fading | +0.0054 (−0.0087 to +0.0223) | +0.2647 (−0.0008 to +0.5975) | +0.0123 (−0.0036 to +0.0336) | +0.0842 (−0.2367 to +0.4422) |
| C fists | −0.0003 (−0.0021 to +0.0016) | +0.0047 (−0.1180 to +0.1203) | **+0.0025 (+0.0013 to +0.0037)** | **+0.1125 (+0.0296 to +0.1883)** |
| D speed | −0.0027 (−0.0113 to +0.0046) | −0.0833 (−0.3750 to +0.1667) | +0.0025 (−0.0077 to +0.0121) | +0.0333 (−0.1833 to +0.2333) |
| E interference | +0.0187 (−0.0100 to +0.0489) | +0.0875 (−0.9562 to +1.0000) | −0.0044 (−0.0265 to +0.0087) | +0.1708 (−0.0544 to +0.4250) |
| F tuning | +0.0103 (−0.0107 to +0.0349) | +0.5982 (−0.0806 to +1.5449) | −0.0068 (−0.0317 to +0.0145) | −0.0357 (−1.0376 to +0.7161) |
| G ragchew | −0.0023 (−0.0056 to +0.0001) | −0.0184 (−0.0460 to +0.0050) | −0.0016 (−0.0049 to +0.0008) | −0.0010 (−0.0138 to +0.0103) |
| H, oracle | **+0.0077 (+0.0018 to +0.0149)** | **+0.0742 (+0.0291 to +0.1252)** | −0.0037 (−0.0137 to +0.0051) | −0.0164 (−0.0889 to +0.0484) |
| H, oracle (per station) | **+0.0298 (+0.0017 to +0.0684)** | +0.2084 (−0.0425 to +0.5345) | **+0.0366 (+0.0040 to +0.0844)** | −0.0231 (−0.3814 to +0.4079) |
| I Farnsworth | +0.0486 (−0.0152 to +0.1206) | +0.0712 (−0.0899 to +0.2608) | **−0.0593 (−0.1251 to −0.0109)** | **+0.2226 (+0.0607 to +0.3590)** |

Texts: `bank-b4d-noclock` differs from `bank-b3b` in 339 of 525 channels. Findings:

1. **The clock fixes do not recover the first-word loss** on this development set (measured). The pooled first-word
   change from the fixes is +0.0379, and its interval includes 0. In group A it is −0.0547 (−0.2423 to +0.0966). The
   trace attributed group A's B4a first-word loss to pre-station noise re-keyed by branch 1 (mechanism M1). The
   section 10.1 test shows that the fixes remove that noise in a synthetic stream. The development-set first-word loss
   that remains must therefore have another cause. Conjectured: branch 1, still selected, re-keys early from a few
   marks with a wrong fitted dit (the trace's channel #8 kind), which the clock rules do not touch. Not traced here.
   A second conjecture (B4d review): fix (i) may itself cost first words. At a stream's start there is no previous
   amplitude, so every time-out clears; if one clears part of a weak station's opening, (i) and the lead keep that
   opening out of every later re-key, where stage 1's stretch would have re-keyed it. Not traced. (Section 11.4
   traces B4e's first words.)
2. The fixes make **H per station worse (+0.0366)**, although mechanism M2 is about later overs, as in H. Not traced.
3. The fixes improve **group I Farnsworth CER (−0.0593)** but worsen its first words (+0.2226). On the paddle test
   case `I-farnsworth-paddle-s1`, `bank-b4d-noclock` is at 0.1194 and `bank-b4d` at 0.0780.
4. Without the fixes, A and C sit within 0.006 of `bank-b3b` (intervals include 0). Two things are worse there: H
   (oracle) and H per station, and first words.

### 10.4 Periodicity

`periodicity-decoded`, `bank-b4d`, threshold 0.03, S₅₀₀ ≥ 0 dB, constant-speed labels:

- Precision 0.809 (0.785 to 0.834).
- Coverage 0.993.
- Median time to confident 0.55 s.

For comparison: `bank-b3b` 0.804, `bank-b4a` 0.753 (6.4). The windows are stage 1's again, so the precision is back
at stage 1's level.

### 10.5 The stretch test and the new-over checks (`stretch-b4d`, B9's script, B4d's defaults)

The `stretch-b4d` decodes of group A's 25 WPM recordings and group H equal `bank-b4d`'s in all 76 shared channels,
every decoded record identical. The run reproduces the development-set run.

| variant | pooled paired CER, stretched − original | CER-0.10 crossing shift, dB of S₅₀₀ (invariant +3.19) |
|---|---|---|
| `bank-b3b`'s settings (8.2) | +0.117 (+0.058 to +0.190) | −0.49 (−0.73 to −0.10) |
| **`bank-b4d`** | **+0.1086 (+0.0524 to +0.1750)** | **−0.48 (−0.66 to −0.11)** |

The losses sit at S₅₀₀ −2 to +2 dB of the original: +0.27, +0.77, +0.67. At 12 WPM the decoder still needs about
3.7 dB more energy per dit than at 25 WPM, as in every earlier variant (section 8).

New overs, from the same run:

- 1.00 over start per same-station silence (16 of 16).
- Group H (oracle): 0.125 false new overs per transmission (10 of 80) and 0.257 missed turnovers (18 of 70; 12 of 14
  at separate-track 100 Hz).
- First-word CER after a turnover: 0.47 (an upper bound).

These are within the earlier variants' ranges (8.3).

### 10.6 CPU and memory

CPU per channel-second, Linux machine, `--jobs 10`, one run each:

- `bank-b4d`: **43.20 ms**. By group: A 39.91, B 46.90, C 38.29, D 38.41, E 83.18, F 49.04, G 63.13, H 55.42, H per
  station 55.64, I 20.62.
- `bank-b4d-noclock`: 45.33 ms.
- For comparison: `bank-b4b` 42.27, `bank-b4a` 57.56.

The windows in seconds remove the per-candidate sliding sums (6.3). Memory per channel (derived, A.8c) is 8.3 to
13.6 MB. In the per-candidate variant it is 9.1 to 14.4 MB.

### 10.7 Raw outputs

All git-ignored.

- `build/suite/full3/experiments/linux/b4d/`: the compare and c2-diff files for both runs and for the fixes; the
  periodicity, stretch and new-over files; the job log `b4d.log`.
- `build/b4d/`: the run scripts `b4d_run.sh`, and `b9_run.sh` copied from B9.
- On the Linux machine: the decoded files `build/suite/full3/proto/bank-b4d/`, `bank-b4d-noclock/` and
  `stretch-b4d/`.

## 11. The re-key wait counted in the station's marks (B4e)

The owner's option d of 2026-10-06, after B4d's first-word loss (section 10.3): the re-key wait counts the station's
marks instead of the branch's dits. Code: `d90441a`. `docs/signal-processing.md` covers the change in body section 8c
(the time-constant table and the re-key cycle) and in appendix A.8c and A.10. Every number here is measured unless
marked otherwise.

### 11.1 What changed

| Setting | B4d default | B4e default | Class | Status |
|---|---|---|---|---|
| Re-key wait | W_min,k = 16.7 · d_k of keyed time (200.4 ms at k = 1 to 3.847 s at k = 32) | 8 provisional marks on the branch (`rekey_marks`) | counted in marks, like the fit's memory | heuristic: stage 1's 0.8 s of key-down at 25 WPM holds about 9 marks (a mark averages 1.86 dits, 89 ms; derived), and a fresh fit already needs 8 marks and spaces |
| Re-key time-out | 2.5 · W_min,k (501 ms at k = 1 to 9.616 s at k = 32) | 7 s of channel time, every branch (`rekey_marks_timeout_s`) | (2) seconds: a latency | derived: 8 PARIS marks (one every 3.57 dits) take 6.9 s at 5 WPM; noise alone (0.01 false marks per second per branch) needs about 800 s |
| Switch | — | `rekey_wait_in_marks` = true; false gives B4d's wait and time-out in dits | — | — |

A provisional mark counts when it ends (key-down, then key-up, both keyed by the unknown-amplitude test) within the
branch's over: since the over started or since the last time-out that keyed nothing (`Branch::marks_in_over`). The
seed is still the 90% quantile of the keyed samples, within the memory of 4 · W_min,k of keyed time, which is
unchanged; on a branch much faster than the station that memory holds only the most recent of the 8 marks' samples
(derived). B4d's clocks (the count from the first provisional mark, the lead 7 · d_k, a cleared time-out moving the
stretch's start) are unchanged. The goldens are unmoved: `stage1_config()` sets the wait in keyed time.

**Tests** (`engine/tests/bank`):

- `BankChannelMarks.A12WpmStationReKeysAtItsEighthProvisionalMark`: the 12 WPM station of B4a's test re-keys on
  branch 23 at 3.285 s with 7 provisional marks one block before, and on branch 1 at 3.243 s with 7 one block before;
  the station's 8th mark ends at 3.200 s. In B4d branch 1 re-keyed after 200.4 ms of keyed time.
- Test (a), renamed `BankChannelClocks.NoiseBeforeAStationIsNotPublishedBeyondTwoDits` (B4d review; seeds 1 to 4 in
  the test). Seeds 1 to 8 on the Windows PC: **0** characters starting more than 2 dits before the station's first
  mark, **1** starting more than half a dit before (seed 8, the same "E" as in B4d, 10.1); B4d's wait gives the same
  counts.
- Test (b), `BankChannelClocks.ALaterOverReKeysAtItsOwnSeed`, now run with three settings. B4e: the branch is known
  at 12.843 s, 1.211 s after the second over's first mark, with 7 provisional marks one block before and
  s² = 9.418 FS² (its own seed). B4d's wait: 12.501 s, 0.707 s keyed (0.686 s before), s² = 9.503 FS². Stage 1's
  clocks: 12.181 s, 0.434 s keyed, s² = 0.849 FS² (a time-out at the first over's amplitude).
- The keyed-time tests (`BankChannelDits.A12WpmStationReKeysAfterItsBranchsWait`,
  `BankKeyingDits.RekeyWaitAndTimeOutPerBranch`, `At25WpmTheValuesAreStageOnes`) run the variant; the first also checks
  the default's 7 s = 10 500 samples on every branch. The split test adds the variant. `bank_json`: the three fields.

Results: `ctest --preset windows` 360 of 360 passed or skipped; `ctest --preset linux` 360 of 360; Python tests 269
passed, 7 expected failures; smoke unchanged (Envelope CER 0.0353, Matched 0.0436).

### 11.2 `bank-b4e` against `bank-b3b` and `bank-b4d` (Linux machine, the development set)

Texts: against `bank-b3b` **345 of 525 channels decode to a different final text** (180 identical); against
`bank-b4d`, 403 (122 identical).

| group | signals | − `bank-b3b` | first-word | − `bank-b4d` | first-word |
|---|---|---|---|---|---|
| all | 509 | **+0.0240 (+0.0044 to +0.0496)** | **+0.3406 (+0.1801 to +0.5222)** | +0.0189 (−0.0001 to +0.0421) | +0.1325 (−0.0239 to +0.3157) |
| A sensitivity | 192 | −0.0026 (−0.0108 to +0.0058) | **+0.3409 (+0.1405 to +0.5527)** | −0.0044 (−0.0136 to +0.0053) | +0.1256 (−0.0411 to +0.3020) |
| B fading | 30 | −0.0082 (−0.0197 to +0.0026) | **+0.4000 (+0.0417 to +0.9945)** | **−0.0259 (−0.0473 to −0.0083)** | +0.0511 (−0.4190 to +0.6473) |
| C fists | 135 | +0.0017 (−0.0003 to +0.0038) | +0.0936 (−0.0545 to +0.2484) | −0.0005 (−0.0028 to +0.0016) | −0.0236 (−0.1846 to +0.1410) |
| D speed | 12 | −0.0067 (−0.0159 to +0.0024) | −0.2500 (−0.5000 to −0.0417) | **−0.0065 (−0.0119 to −0.0018)** | −0.2000 (−0.3333 to −0.0750) |
| E interference | 16 | +0.0205 (−0.0047 to +0.0501) | −0.1375 (−1.3063 to +0.8753) | +0.0062 (−0.0090 to +0.0224) | −0.3958 (−0.9125 to +0.0417) |
| F tuning | 28 | +0.0037 (−0.0139 to +0.0209) | **+0.8286 (+0.1964 to +1.5609)** | +0.0002 (−0.0189 to +0.0201) | +0.2661 (−0.5038 to +1.0456) |
| G ragchew | 12 | −0.0028 (−0.0073 to +0.0008) | −0.0135 (−0.0449 to +0.0233) | +0.0011 (−0.0005 to +0.0031) | +0.0060 (−0.0125 to +0.0295) |
| H two-station QSO, oracle | 12 | +0.0843 (−0.0058 to +0.2223) | **+0.2265 (+0.0501 to +0.4359)** | +0.0803 (−0.0057 to +0.2178) | +0.1687 (−0.0524 to +0.3885) |
| H two-station QSO, oracle (per station) | 24 | **+0.6028 (+0.2389 to +1.0236)** | **+2.5099 (+0.3474 to +5.6622)** | **+0.5364 (+0.2076 to +0.9518)** | **+2.3246 (+0.2167 to +5.2583)** |
| I Farnsworth | 48 | **−0.0643 (−0.1180 to −0.0199)** | +0.0517 (−0.1751 to +0.3622) | **−0.0536 (−0.0856 to −0.0242)** | −0.2420 (−0.4889 to +0.1007) |

**The pooled interval against `bank-b3b` lies entirely above 0, and so does H per station's: reported to the owner**
(the plan's rule; not a gate; committed per the owner's decision). H per station's regimes, CER of the test case,
`bank-b3b` → `bank-b4e`:

- Separate-track 100 Hz: 0.104 → 2.350 (first-word CER 1.67 → 15.34).
- Ambiguous 50 Hz: 0.585 → 1.807.
- Same-track 25 Hz: 1.336 → 1.380; 0 Hz and 10 Hz and separate-track 200 Hz unchanged within 0.004.

H per station alone moves the pooled mean: the other 485 signals average −0.0046 (derived from the group means, no
interval). Group A's first words are worse at every speed than with `bank-b3b` and with `bank-b4d`: 12 WPM 0.39 →
0.51 (B4d 0.46), 25 WPM 0.39 → 0.78 (0.57), 40 WPM 0.64 → 0.84 (0.72). Summed first-word edits over the 509 signals:
`bank-b3b` 1385, `bank-b4d` 1675, `bank-b4e` 2780; H per station's later transmissions alone go 121 → 192 → 1027.

### 11.3 Periodicity, the stretch test and the new-over checks

Periodicity (`periodicity-decoded`, threshold 0.03, S₅₀₀ ≥ 0 dB, constant-speed labels): precision 0.809 (0.785 to
0.834), coverage 0.993, median time to confident 0.54 s, as `bank-b4d` (0.809).

The `stretch-b4e` decodes of group A's 25 WPM recordings and group H equal `bank-b4e`'s in all 76 shared channels,
every decoded record identical.

| variant | pooled paired CER, stretched − original | CER-0.10 crossing shift, dB of S₅₀₀ (invariant +3.19) |
|---|---|---|
| `bank-b3b`'s settings (8.2) | +0.117 (+0.058 to +0.190) | −0.49 (−0.73 to −0.10) |
| `bank-b4d` (10.5) | +0.1086 (+0.0524 to +0.1750) | −0.48 (−0.66 to −0.11) |
| **`bank-b4e`** | **+0.1023 (+0.0531 to +0.1578)** | **−0.02 (−0.54 to +0.35)** |

The shift moves 0.46 dB toward invariance, but its interval overlaps `bank-b4d`'s; the 12 WPM copy still needs about
3.2 dB more energy per dit than the 25 WPM original (measured shift minus the invariant +3.19 dB). The losses sit at
S₅₀₀ −2 to +2 dB of the original: +0.29, +0.78, +0.46.

New overs (same run): 16 of 16 same-station silences start an over; group H (oracle): 0.300 false new overs per
transmission (24 of 80; `bank-b4d` 0.125) and 0.186 missed turnovers (13 of 70; `bank-b4d` 0.257); first-word CER
after a turnover 0.71 (an upper bound; `bank-b4d` 0.47). The false new overs sit at ambiguous 50 Hz (13 of 16) and
separate-track 100 Hz (11 of 16), the regimes of H per station's loss.

### 11.4 Where the first words go: the worst 10 traced

How traced: the B4a re-key trace's instrumentation (git-ignored scripts; that trace's findings are
`docs/research/2026-10-05-periodicity-windows-and-rekey-analysis.md`) applied to `d90441a` in a worktree on the Linux machine, with the provisional-mark count added to
each re-key's line. Its decodes of the 10 test cases traced equal `bank-b4e`'s in text, characters, corrections and
selections in all 285 channels (measured). The 10 signals with the largest first-word increase against `bank-b3b`
carry +0.2063 of the pooled +0.3406.

| signal | first-word edits b3b → b4e (b4d) | what published the text before the first word |
|---|---|---|
| H per station #19, 24.2 WPM, 11.7 dB | 0 → 450 (14) | noise between overs, re-keyed (below) |
| H per station #17, 28.7 WPM, 15.3 dB | 116 → 517 (88) | the same |
| A 12 WPM-0 #11, 0 dB, station at 1.25 s | 1 → 12 (12) | k1 at 3.157 s: 8 provisional marks, 0.784 s keyed, count from 0.0023 s, stretch from −0.0043 s, seed a = 4.58, 36 marks re-keyed, fit T 9.5 ms: "I E EEEE***E E" |
| A 25 WPM-0 #13, 2 dB, at 1.39 s | 0 → 15 (5) | k1 at 2.027 s: 8 marks, 0.440 s keyed, count from 0.0023 s, a = 5.94, 15 marks, T 11.2 ms: "E E E E E ITAM" |
| B fading #14, 36.9 WPM, 7.8 dB, at 0.75 s | 1 → 15 (2) | k1 at 1.835 s: 8 marks, 0.290 s keyed, count from 0.0017 s, a = 5.28, 20 marks, T 11.0 ms: "E E EEI ES E EES N" |
| A 25 WPM-0 #12, 2 dB, at 1.75 s | 0 → 13 (1) | k1 at 2.688 s: 8 marks, 0.550 s keyed, count from 0.0030 s, a = 5.78, 22 marks, T 32.2 ms: "E E EE HIIHE" |
| A 40 WPM-0 #11, 0 dB, at 0.99 s | 0 → 13 (3) | k1 at 2.027 s: 8 marks, 0.430 s keyed, count from 0.0030 s, a = 5.40, 25 marks, T 10.2 ms: "EEEIE E TIZ TN EPN" |
| C machine #11, 25 WPM, 10 dB, at 0.69 s | 0 → 13 (0) | k1 at 0.512 s, before the station: 8 marks, 0.105 s keyed, count from 0.0023 s, a = 4.44, 17 marks, T 9.0 ms: "E S55" |
| F offset #8, 20 WPM, 0 dB, at 1.52 s | 2 → 15 (2) | k1 at 3.157 s: 8 marks, 0.599 s keyed, count from 0.0017 s, a = 4.49, 36 marks, T 54.6 ms: "I I EE E IESSI **I" |
| A 12 WPM-1 #11, 0 dB, at 1.22 s | 0 → 16 (0) | k1 at 1.429 s: 8 marks, 0.229 s keyed, count from 0.0023 s, a = 4.63, 15 marks, T 9.9 ms: "EE EE E EE EE" |

In every case the re-key's stretch starts at −0.0043 s (the stream's start less branch 1's group delay) and branch 1
was the selected branch (the selector's start state).

Findings:

1. **The first provisional mark is a start-up artifact** (measured). In all 285 traced channels, on every branch
   (9120 of 9120), the count starts 1.7 to 3.7 ms into the stream. So under B4d's clocks the count and the stretch
   start at the stream's start (the lead cannot reach before it). Conjectured cause: the development set's channel
   streams begin with about 10 near-zero samples (the channel filter's ramp: |u|² below 0.01 of the noise's median
   at 1 to 2 s, measured on three channels), and during the 0.32 s warm-up σ²_v,k is the 20% quantile of the samples
   so far, too low in the first block, so the unknown-amplitude test keys at once. B4d's test (a) stream has full
   noise from its first sample, which would be why it passes (conjectured). Not traced into the noise estimate.
2. **With the 7 s time-out nothing clears that start** (derived from the rule, consistent with the traces). In B4d,
   branch 1's 0.501 s time-out fired in the pre-station noise, cleared it, and moved the stretch's start (fix (i)).
   Now the first time-out would come at 7 s; the station's 8 marks come first (0.5 to 3.2 s), and branch 1, still
   selected, re-keys the whole stretch from the stream's start at its seed a = 4.4 to 5.9. Noise crosses the
   full-LLR threshold there on 0.3% to 0.9% of samples (B4d's trace, derived), and the garbage stands in front of the
   first word. This is mechanism M1 of the re-key trace, reinstated; 8 of the 10 worst signals. In C machine #11
   8 provisional marks come even before the station (0.512 s against 0.69 s), consistent with finding 1's low σ²
   during the warm-up.
3. **H per station: noise between overs is re-keyed** (measured). After 5 s, the selected branch re-keys 73 times at
   a seed a < 8 in the 24 per-station channels. Their 8 provisional marks come within a median 1.00 s of the first and
   hold a median 0.093 s of keyed time in all (about 12 ms each: short noise peaks), and the re-keys publish 862
   non-space characters. Under B4d's wait these marks would not have re-keyed: 0.093 s is below W_min,k on every
   branch (200.4 ms at k = 1; derived), so the time-out cleared them. 8 marks within about 1 s is a false-mark rate
   near 8 per second, far above the calibrated 0.01 per second per branch; conjectured causes: the other station 50
   to 100 Hz away, or a low σ² estimate in these channels. Not traced. Counting marks ignores their duration, which
   is what kept noise peaks out of the keyed-time wait.
4. Not tried here (outside B4e's brief). Options for the owner (not measured): (a) a minimum keyed time
   per counted mark (for example, a mark counts only if it lasts at least about one d_k), which would exclude the
   12 ms noise peaks of finding 3; (b) start the count only after the noise warm-up (0.32 s) or ignore provisional
   marks keyed while the noise estimate is warming up, which would remove finding 1's start-up mark; (c) keep the
   7 s time-out for genuine stations but clear unknown overs whose marks are all shorter than the branch's dit.
   Recommendation: (b) and (a) together, each behind its own switch and measured as a variant, because findings 1
   and 3 are separate mechanisms.

### 11.5 CPU and memory

CPU per channel-second, Linux machine, `--jobs 10`, one run: **`bank-b4e` 53.07 ms** (`bank-b4d` 43.20, +23%). By
group: A 50.34, B 58.42, C 47.29, D 51.59, E 98.29, F 56.50, G 71.75, H 68.78, H per station 70.81, I 22.22.
Conjectured cause, not measured: more branches re-key (8 marks of a station reach every branch's wait, where the
keyed time of a slow branch often timed out first), and a known branch fits every duration while an unknown one fits
none. Memory per channel (derived, A.8c) is unchanged, 8.3 to 13.6 MB; the seed's memory per branch is unchanged
(4 · W_min,k).

### 11.6 Raw outputs

All git-ignored.

- `build/suite/full3/experiments/linux/b4e/`: the compare and c2-diff files against `bank-b3b` and `bank-b4d`; the
  periodicity, stretch and new-over files; the job log `b4e.log`.
- `build/b4e/`: the run scripts `b4e_run.sh` and `b9_run.sh` (from B4d), the trace scripts (`trace_run.sh`,
  `patch_b4e.py`, `fixnl.py`, `scripts/fw.py`, `scripts/worst10.py`), the traces (`traces/`), and the scored results
  of the three runs (`res/`).
- On the Linux machine: the decoded files `build/suite/full3/proto/bank-b4e/` and `stretch-b4e/`; the trace worktree
  `build/b4e/wt` (detached at `d90441a`, instrumented) and its decodes `build/b4e/suite/proto/trace-b4e/`.

## 12. Guards on the counted marks (B4f) and B4e's CPU

The owner's option e of 2026-10-06, after B4e's trace (11.4): the wait stays 8 marks with the 7 s time-out, and two
guards decide which provisional marks count. Code: `91ffdc6`. `docs/signal-processing.md` covers the change in body
section 8c (the re-key cycle) and in appendix A.8c and A.10. Every number here is measured unless marked otherwise.

### 12.1 What changed

| Setting | B4e | B4f default | Status |
|---|---|---|---|
| Guard 1, filter full (`rekey_guard_filter_full`) | every ended provisional mark counts | a mark counts only if N_k samples of non-zero input (inclusive) precede its key-down, since the stream's start or the last exact zero (N_1 = 14 samples, 9.3 ms; N_32 = 276) | derived: before that the boxcar's output is a partial sum divided by N_k |
| Guard 2, minimum length (`rekey_guard_min_length`) | — | a mark counts only if it lasts at least N_k samples (L_k) | heuristic: a real element keyed on its own branch lasts about L_k or longer; a noise excursion is expected to be shorter than the filter's correlation time (12.2) |
| The clocks | the first ended provisional mark starts the count at its key-down | the first counted mark does, from its key-down sample, when it ends | — |
| The seed's quantile | a full sort of the kept keyed samples every block | the two order statistics by selection (`std::nth_element`) | exact: the same value bit for bit (12.5) |

Marks that do not count are keyed provisionally and decoded as before. Each guard has its own switch; both off is B4e's
rule, and `bank-b4f-noguard` (both off, B4f's binary) reproduces `bank-b4e` in all 525 channels, every decoded record
identical, which also checks the selection quantile on the whole development set.

**Tests** (`engine/tests/bank`; Windows):

- `BankChannelGuards.AStartUpMarkDoesNotStartTheCount` (extends test (a)): test (a)'s stream with a channel filter's
  start-up ramp (10 samples near zero, then a linear rise to sample 20, as the development set's streams begin: their
  power averaged over the 32 channels of `A-awgn-25wpm-0-s1` is below 0.01 of the noise's median up to sample 10 and
  near it from sample 20). Without the guards all 32 branches start their count before their filter is full (samples
  10 to 11); with them none does. Characters published more than 2 dits before the station, seeds 1 to 4: **7
  without the guards, 1 with**. The remaining one comes from a mark keyed in the noise warm-up after the filter is
  full and longer than L_1: branch 1's count starts at sample 51 (34 ms), branch 5's at 53. The test asserts the
  count rule and "fewer", not "none".
- `BankChannelGuards.ABurstOfShortMarksBetweenOversDoesNotReKey` (extends test (b)): a 25 WPM station's over, then 1 s
  later a station 100 Hz away at +9.5 dB relative keying for 6 s. The first station's branch (16) re-keys once on the
  leak without the guards, never with them.
- Test (a) as before (seeds 1 to 4): 0 characters more than 2 dits or half a dit before the station. Test (b) as before:
  the second over re-keys at its own seed, known at 12.843 s with 7 counted marks one block before, s² = 9.418 FS².
- `BankNoise.QuantileBySelectionIsTheSortedFormBitForBit`: 7 224 comparisons with a frozen sorting copy (sizes 1 to 300
  and 23 079, ties, exact zeros, quantiles 0, 0.2, 0.5, 0.9, 1 and random), every one equal bit for bit.

Results: `ctest --preset windows` 363 of 363; `ctest --preset linux` 363 of 363; Python tests 269 passed, 7 expected
failures; smoke unchanged (Envelope CER 0.0353, Matched 0.0436).

### 12.2 How long are noise marks? (guard 2's premise)

Provisional marks were logged (an instrumented copy of B4e's code, which keys exactly as B4f) with their length in
units of the branch's L_k, while the branch's amplitude was unknown. White noise alone: 600 s at 1500 samples/s, the
re-key disabled so every branch keys provisionally throughout. Group H per station (24 channels): classified by
whether the channel's own station transmits (±0.3 s), its partner transmits, or neither.

| marks | count | median length / L_k | 10% | 90% | lasting ≥ L_k |
|---|---|---|---|---|---|
| white noise alone (0.0133 per second per branch) | 256 | 0.76 | 0.50 | 1.18 | **21.5%** |
| group H, the channel's own station transmitting | 23 043 | 4.88 | 1.93 | 11.56 | **99.8%** |
| group H, neither station (silences) | 1 468 | 0.91 | 0.26 | 2.84 | **45.6%** |
| group H, partner transmitting, separate-track (100 and 200 Hz) | 27 848 | 0.30 | 0.10 | 0.92 | 7.9% |
| group H, partner transmitting, ambiguous (50 Hz) | 15 574 | 0.22 | 0.10 | 1.31 | 12.9% |
| group H, partner transmitting, same-track (0 to 25 Hz) | 15 106 | 2.81 | 0.18 | 10.26 | 68.6% |

By branch, the share lasting ≥ L_k falls with k for noise and leaks: white noise 45.9% on branches 1 to 8, 3.4% on
17 to 24; group H's silences 70.8% on 1 to 8, 11.0% on 25 to 32; separate-track partner marks 15.1% on 1 to 8.

So the premise holds for white noise (78.5% of its marks are shorter than L_k) and for a station 50 to 200 Hz away
(87% to 92% shorter), and real marks of the channel's own station pass (99.8%). It holds only partly in group H's
silences (54.4% shorter) and not when the partner shares the frequency (31.4% shorter: those are real marks). Guard 2
lowers the rate of counted false marks by a factor of about 2 to 13; it does not remove them.

### 12.3 `bank-b4f` against `bank-b3b`, `bank-b4d` and `bank-b4e` (Linux machine, the development set)

Texts differ from `bank-b3b` in 346 of 525 channels, from `bank-b4d` in 406, from `bank-b4e` in 293.

| group | signals | − `bank-b3b` | first-word | − `bank-b4d` | first-word | − `bank-b4e` | first-word |
|---|---|---|---|---|---|---|---|
| all | 509 | +0.0078 (−0.0047 to +0.0203) | **+0.1062 (+0.0091 to +0.2004)** | +0.0027 (−0.0074 to +0.0151) | −0.1019 (−0.2113 to +0.0053) | **−0.0161 (−0.0301 to −0.0040)** | **−0.2344 (−0.3870 to −0.0894)** |
| A sensitivity | 192 | −0.0075 (−0.0161 to −0.0000) | +0.1601 (−0.0042 to +0.3223) | **−0.0093 (−0.0177 to −0.0021)** | −0.0552 (−0.2448 to +0.1466) | −0.0049 (−0.0112 to +0.0012) | **−0.1809 (−0.3884 to −0.0106)** |
| B fading | 30 | −0.0019 (−0.0130 to +0.0080) | **+0.6011 (+0.0222 to +1.2984)** | **−0.0196 (−0.0373 to −0.0052)** | +0.2522 (−0.3778 to +1.0167) | +0.0063 (−0.0014 to +0.0154) | +0.2011 (−0.1612 to +0.7157) |
| C fists | 135 | +0.0018 (−0.0003 to +0.0038) | −0.0594 (−0.1604 to +0.0501) | −0.0005 (−0.0025 to +0.0014) | **−0.1765 (−0.2952 to −0.0532)** | +0.0001 (−0.0023 to +0.0023) | −0.1530 (−0.3290 to +0.0072) |
| D speed | 12 | −0.0061 (−0.0174 to +0.0067) | −0.2500 (−0.4583 to −0.0417) | −0.0059 (−0.0150 to +0.0028) | −0.2000 (−0.3333 to −0.0750) | +0.0007 (−0.0050 to +0.0067) | +0.0000 |
| E interference | 16 | +0.0105 (−0.0120 to +0.0370) | −0.3229 (−1.6773 to +0.7815) | −0.0037 (−0.0272 to +0.0186) | −0.5813 (−1.1776 to +0.0440) | −0.0100 (−0.0277 to +0.0058) | −0.1854 (−0.5813 to +0.1668) |
| F tuning | 28 | −0.0053 (−0.0196 to +0.0087) | +0.4643 (−0.0179 to +0.9826) | −0.0088 (−0.0248 to +0.0074) | −0.0982 (−0.6736 to +0.5107) | −0.0090 (−0.0285 to +0.0132) | −0.3643 (−1.0001 to +0.2966) |
| G ragchew | 12 | −0.0039 (−0.0103 to +0.0007) | −0.0234 (−0.0792 to +0.0227) | +0.0000 (−0.0028 to +0.0030) | −0.0039 (−0.0473 to +0.0456) | −0.0011 (−0.0051 to +0.0022) | −0.0099 (−0.0612 to +0.0404) |
| H, oracle | 12 | +0.0199 (−0.0273 to +0.0816) | +0.0906 (−0.0250 to +0.2694) | +0.0158 (−0.0260 to +0.0685) | +0.0327 (−0.1263 to +0.2013) | **−0.0644 (−0.1668 to −0.0007)** | **−0.1359 (−0.2792 to −0.0181)** |
| H, oracle (per station) | 24 | **+0.3313 (+0.1449 to +0.5448)** | **+0.4265 (+0.0891 to +0.8594)** | **+0.2650 (+0.1158 to +0.4509)** | +0.2412 (−0.2269 to +0.7538) | **−0.2715 (−0.5570 to −0.0299)** | **−2.0834 (−4.8166 to −0.1164)** |
| I Farnsworth | 48 | **−0.0593 (−0.1121 to −0.0122)** | −0.0535 (−0.2165 to +0.1111) | **−0.0486 (−0.0825 to −0.0214)** | **−0.3472 (−0.5080 to −0.1923)** | +0.0051 (−0.0014 to +0.0113) | −0.1052 (−0.3831 to +0.0719) |

**Against `bank-b3b`, H per station's interval still lies entirely above 0: reported to the owner** (the plan's rule;
not a gate). Its regimes, `bank-b3b` → `bank-b4f` (`bank-b4e`): separate-track 100 Hz 0.104 → 1.050 (2.350);
ambiguous 50 Hz 0.585 → 1.544 (1.807); same-track 25 Hz 1.336 → 1.378; the others within 0.015. The pooled interval
now includes 0, and the first-word loss is about half of B4e's but still above 0. Group A's first words, `bank-b3b` →
`bank-b4f` (`bank-b4e`): 12 WPM 0.39 → 0.28 (0.51), 25 WPM 0.39 → 0.73 (0.78), 40 WPM 0.64 → 0.71 (0.84).

**The guards separated** (`bank-b4f-g1`, guard 1 only):

| | pooled paired CER | first-word | H per station |
|---|---|---|---|
| `bank-b4f-g1` − `bank-b3b` | **+0.0221 (+0.0012 to +0.0481)** | **+0.2191 (+0.0671 to +0.4078)** | **+0.6048 (+0.2317 to +1.0475)** |
| `bank-b4f-g1` − `bank-b4e` (guard 1) | −0.0019 (−0.0043 to +0.0005) | **−0.1215 (−0.2189 to −0.0375)** | +0.0020 (+0.0001 to +0.0043) |
| `bank-b4f` − `bank-b4f-g1` (guard 2) | **−0.0142 (−0.0299 to −0.0026)** | −0.1129 (−0.2559 to +0.0088) | **−0.2735 (−0.5907 to −0.0270)** |

Guard 1 recovers about a third of B4e's first-word loss (A −0.1731 with its interval below 0) and changes the CER little;
guard 2 removes about half of H per station's loss.

### 12.4 H per station: the worst 5 traced

The B4e trace's instrumentation, applied to `91ffdc6` in a worktree on the Linux machine, reproduces `bank-b4f` in 24
of 24 per-station channels (text, characters, corrections, selections). The 5 signals with the largest CER increase
against `bank-b3b`: #15 (ambiguous 50 Hz, +1.743), #16 (separate-track 100 Hz, +1.273), #13 (ambiguous, +1.058),
#19 (separate-track, +0.981), #18 (separate-track, +0.817).

Findings:

1. **The text comes while the partner station keys** (measured). In #16, #19 and #18 the published text changes 1 200
   times while the partner keys, against 840 while the channel's own station keys and 17 in silences. B4e's
   mechanism of 11.4 (the selected branch re-keying noise between overs) is nearly gone: re-keys of the selected
   branch outside its own transmissions publish 150 non-space characters in all, in #15 and #13 only.
2. **Short branches re-key on the partner's leak and are selected** (measured). 118 switches in the 5 channels go,
   while the partner keys, to a branch that re-keyed during that transmission: mostly branches 2 to 9 (5: 30, 6: 23,
   9: 17, 7: 13; 22: 9). Those re-keys came a median 1.47 s after their count started, with 0.479 s keyed, at a seed
   a = 5.71 and a fitted T of 21.1 ms. Example (#16, 81.0 s, the partner's over starts): branches 4 to 7 count 8 marks
   from 81.006 s and re-key at 81.86 to 82.47 s at a = 5.0 to 5.9 (their previous amplitude a = 20 to 23 loses), and
   selection switches to them.
3. **Why guard 2 does not stop it** (measured, 12.2; derived). The partner's elements are real marks, attenuated by
   the short boxcars' sidelobes (a boxcar of L = 13 ms has its first null near 77 Hz): 15.1% of the leak's marks on
   branches 1 to 8 last at least L_k, and the partner keys several marks per second, so 8 counted marks come within
   about 1.5 s. A short branch re-keyed at the leak's own amplitude then keys it cleanly, its fit explains it, and
   selection takes it. Stage 1's wait (0.8 s of keyed time, time-out 2 s) reached the same branches less often
   (`bank-b3b` 0.104 at separate-track 100 Hz); not traced.

### 12.5 B4e's CPU rise, explained and removed

Profiling with `perf` is not allowed on the Linux machine (`perf_event_paranoid` = 4), so the instrumented B4e
worktree timed each stage of `process_block` per channel (steady clock), decoding `A-awgn-25wpm-0-s1`,
`C-fists-machine-s1` and `H-qso-oracle-s1.stations` with B4e's wait (marks) and with B4d's wait (keyed time), the same
code otherwise, 4 threads. ms per channel-second:

| test case | wait | total | keyer | edges and fits | re-keys | periodicity | known branch-blocks |
|---|---|---|---|---|---|---|---|
| A 25 WPM-0 | marks | 48.77 | **12.10** | 31.85 | 1.66 | 1.81 | 3 929 622 |
| A 25 WPM-0 | keyed time | 38.85 | 3.93 | 31.07 | 0.94 | 1.73 | 3 977 825 |
| C machine | marks | 60.55 | **17.52** | 39.25 | 0.61 | 2.18 | 4 174 335 |
| C machine | keyed time | 46.35 | 3.01 | 39.80 | 0.44 | 2.11 | 4 515 593 |
| H per station | marks | 70.51 | **8.34** | 49.96 | 8.88 | 1.95 | 9 112 823 |
| H per station | keyed time | 53.89 | 4.46 | 42.00 | 4.15 | 1.95 | 8 676 407 |

The rise is in the keyer (A +8.2, C +14.5 ms per channel-second), not in the fits: the number of known branch-blocks is
about the same. While a branch's amplitude is unknown, the keyer recomputes the seed (the 90% quantile of the kept
keyed samples, up to 4 · W_min,k of keyed time: 1202 samples at k = 1, 23 079 at k = 32) every block, by copying and
fully sorting them. With the wait in marks the kept samples grow larger before the re-key (a slow branch re-keys after
8 marks, a fast one keeps keying provisionally up to 7 s), so the per-block sort costs more (conjectured from the code;
the buffer sizes were not logged). In H, the re-keys also cost more (8.88 against 4.15), as there are more of them
(7 930 against 5 094). It is an inefficiency: the seed needs only two order statistics. Selecting them
(`std::nth_element`, then the minimum above) gives the same value bit for bit (12.1). Measured on the development set
(one run each): `bank-b4f-noguard` (B4e's rule) **43.58 ms per channel-second, against `bank-b4e`'s 53.07** with
identical decodes; `bank-b4f` **41.72**; `bank-b4f-g1` 43.73; `bank-b4d` 43.20. By group, `bank-b4f`: A 38.88, B
45.57, C 37.21, D 37.43, E 79.78, F 46.85, G 61.83, H 53.19, H per station 53.66, I 18.40.

### 12.6 Periodicity, the stretch test and the new-over checks

Periodicity (threshold 0.03, S₅₀₀ ≥ 0 dB): precision 0.809 (0.785 to 0.834), coverage 0.993, as `bank-b4e`.

`stretch-b4f` equals `bank-b4f` in all 76 shared channels, every decoded record identical.

| variant | pooled paired CER, stretched − original | CER-0.10 crossing shift, dB of S₅₀₀ (invariant +3.19) |
|---|---|---|
| `bank-b3b`'s settings (8.2) | +0.117 (+0.058 to +0.190) | −0.49 (−0.73 to −0.10) |
| `bank-b4d` (10.5) | +0.1086 (+0.0524 to +0.1750) | −0.48 (−0.66 to −0.11) |
| `bank-b4e` (11.3) | +0.1023 (+0.0531 to +0.1578) | −0.02 (−0.54 to +0.35) |
| **`bank-b4f`** | **+0.1060 (+0.0584 to +0.1619)** | **−0.16 (−0.61 to +0.20)** |

New overs: 16 of 16 same-station silences start an over; group H (oracle): 0.075 false new overs per transmission (6
of 80; `bank-b4e` 0.300, `bank-b4d` 0.125), 0.271 missed turnovers (19 of 70), first-word CER after a turnover 0.54
(an upper bound; `bank-b4e` 0.71, `bank-b4d` 0.47).

### 12.7 Raw outputs

All git-ignored.

- `build/suite/full3/experiments/linux/b4f/`: the compare and c2-diff files, periodicity, stretch and new-over files,
  the job log `b4f.log`.
- `build/b4f/`: `b4f_run.sh`, `b9_run.sh`, the trace scripts (`trace_run.sh`, `patch_b4f.py`, `patch_pm.py`,
  `fixnl.py`) and the traces (`traces/`).
- `build/b4e/`: the mark-length measurement (`pm_run.sh`, `patch_pm.py`, `pm/`, `scripts/pm_an.py`), the CPU timing
  (`cpu_run.sh`, `patch_cpu.py`), `scripts/h5.py`, `scripts/h5b.py`, and the scored results (`res/`).
- On the Linux machine: `build/suite/full3/proto/bank-b4f/`, `bank-b4f-g1/`, `bank-b4f-noguard/`, `stretch-b4f/`;
  the instrumented worktrees `build/b4e/wt` (B4e, with the timers and PM lines) and `build/b4f/wt` (detached at
  `91ffdc6`).
