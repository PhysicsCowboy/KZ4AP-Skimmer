# Checkpoint 2: first working app (2026-10-31)

**Answer.** The app works on the merged decoder, and the decoder wins every group of the test set against the bank, Farnsworth
included: I2 reads CER 0.021 against checkpoint 1's 0.523 and the bank's 0.261. E2 interference reads 0.575 against 0.691 and 0.776
when told the frequency, and 0.688 against 0.734 and 0.814 as the app reads it. CPU on the whole test set is 6.87 ms per channel-second on
the Linux machine, about 7.3 to 7.8 ms on the desktop (derived), at the 8 ms goal. On 30 min of real 40 m it is 13.3 ms, about 17 ms in real time,
under the 40 ms required and over the goal. No decoder has yet been scored against truth on a real signal; that is checkpoint 3's.

**Terms.** *CER*: edits over reference symbols, spaces counted, pooled over the scored signals. *Oracle path*: the decoder is told each station's
frequency. *Detector's path*: what the app runs, the detector opening a channel at each peak. *Late*: a channel that opens more than 10 s after the stream
began; no station of the test set does (all start at 0.5 to 2 s), so "late" runs put 15 s of noise first. *Stream*: one filter width's evidence and search inside a channel. *S500*: key-down power over noise power
in 500 Hz, dB. *dB re wanted*: relative to the wanted station's key-down power. *Stand-in radio*: a DLL with the SDRplay API's names that plays a WAV through the real `SdrplaySource`; the owner's radio was not used.

## How to try it

`README.md`, "Try the app (Windows)": the steps as typed, tested literally from an empty folder on the desktop (configure 1.5 min, build 2.5 min, generate a recording 40 s). On this desktop, in the repository's own folder, `build\recordings\real30.wav` (git-ignored) is the 30 min of real 40 m. Please try 192 kHz on the radio first, then 768 kHz: the radio has not been run since the merge, and that first run is the test.

## What changed since checkpoint 1

The decoder gained Farnsworth's gaps in the fist (a story's state is now speed and gap stretch), and a second filter width: each channel runs a 12 ms and a 36 ms filter, and one stream's whole text goes out, chosen by the evidence for Morse over a timing-free key. The app gained the replay (2 s of spectrum, so a new channel reads what it missed), a judge retiring what is not keyed Morse, holds at retired places, a track budget, and recording. At the review's request: the engine's tables are computed once at 48 kHz, so it builds in 0.08 to 0.10 s at 768 kHz, not 16 to 18 s; the choice of filter runs on the channel's own age; the filter at 24 ms is gone (below).

**How the decoder works now.** Each channel: a matched filter per width, an envelope sample per 2.667 ms (widest), one shared level belief per channel, and a search over stories (speed, gap stretch, text) touched only at element ends, every 0.25 s the best story's text. Details: `docs/signal-processing.md`, section 8e.

## Runs

The whole set is seed 1 of the development set `DEV2`, on the Linux machine, 8 jobs on 10 cores: 498 recordings on the oracle path and 210 on the detector's path. Tables are in `docs/plans/data/`, this checkpoint's in `2026-10-31-checkpoint-2/`.

| Label | Decoder and commit | Recordings | Channel-seconds | CPU, ms per channel-second | Table |
|---|---|---|---|---|---|
| Final, whole | `segmental`, e913886 (`app-6`; later commits on the branch change tests and documents only) | 708 | 1 391 311 | 6.87 (decoder 6.46) | `segmental-app6-whole.csv`; CPU `cpu-app6-whole.csv` |
| `app-5`, whole | `segmental`, 902121c: three filters, stream's clock | 708 | 1 391 335 | 9.52 | as committed at 949b0f3: `segmental-app5-whole.csv`, `cpu-app5-whole.csv` |
| Checkpoint 1, whole | `segmental`, d78d45f | 708 | 1 395 785 | 5.61 | `2026-10-31-checkpoint-1/segmental-fast2-whole.csv` |
| Bank, whole | `bank`, task D5's replay | 498 | not recorded here | not recorded here | `2026-10-06-development-set/d5-bank.csv` |
| Final, seed 2 | `segmental`, e913886 | 247 | 560 413 | 5.84 | `segmental-app6-seed2.csv`; `cpu-app6-seed2.csv` |
| Three filters, seed 2 | `segmental`, `fast-3` (3f8a9ff) | 247 | 560 412 | 8.25 | as committed at 949b0f3: `segmental-fast3-seed2.csv` |
| Before `fast-3`, seed 2 | `segmental`, `fast-3`'s base | 247 | 560 412 | 5.04 | `segmental-fast2-seed2.csv` |

The CPU of the runs that have no CPU table here (checkpoint 1, and `fast-3` and its base on seed 2) comes from their result files, git-ignored. `.../conditions-b2-c2.csv` of checkpoint 1 gives each B2 and C2 signal's Doppler spread and keying. Every CER below is computed from these rounded tables. Tests at 3842b07 (the code as at e913886 and one more test; later commits change this record only): `ctest`, Windows with Qt 758 tests, 739 passed, 19 skipped, 0 failed; Windows without Qt the same 758; Linux 745, 726 passed, 19 skipped, 0 failed. The skipped are as designed: expected failures and measurements.

## Scores by group

| Group | Final | `app-5` | Checkpoint 1 | Bank |
|---|---|---|---|---|
| A2 sensitivity | 0.226 | 0.226 | 0.241 | 0.287 |
| A2, detector's path | 0.250 | 0.250 | 0.274 | |
| B2 fading | 0.242 | 0.241 | 0.251 | 0.557 |
| C2 fists | 0.063 | 0.063 | 0.061 | 0.157 |
| D2 speed changes | 0.000 | 0.000 | 0.000 | 0.027 |
| E2 interference | 0.575 | 0.562 | 0.691 | 0.776 |
| F2 tuning | 0.085 | 0.085 | 0.087 | 0.133 |
| G2 QSO, same track | 0.055 | 0.055 | 0.055 | 0.206 |
| H2 QSO, separate tracks | 0.056 | 0.057 | 0.058 | 0.198 |
| I2 Farnsworth | 0.021 | 0.021 | 0.523 | 0.261 |
| S2 stretch | 0.237 | 0.237 | 0.264 | 0.337 |

E2 counts only the wanted stations (90 signals); G2 has 20, H2 40. By signal, final against checkpoint 1: 7 595 better, 12 218 the same, 1 021 worse; against `app-5`: 77 better, 20 623 the same, 134 worse. On the detector's path 5 607 of 6 720 signals are detected, in both decoders; first-word CER over all 6 720 (20 627 first-word symbols) 0.284 against checkpoint 1's 0.681, and over the 5 607 detected (17 173 symbols) 0.140 against 0.617.

**E2 as the app reads it** (`e2-detector.csv`; detector's path; 90 signals):

| Variant | All | Neighbor 0 to 25 Hz | 25 to 60 Hz | 60 to 120 Hz |
|---|---|---|---|---|
| Final, stations at 0.5 to 2 s | 0.688 | 0.851 | 0.961 | 0.250 |
| Final, late | 0.669 | 0.851 | 0.910 | 0.244 |
| `app-5`, stations at 0.5 to 2 s | 0.705 | 0.852 | 1.011 | 0.251 |
| `app-5`, late | 0.719 | 0.876 | 1.010 | 0.272 |
| `app-5`, late, widest filter alone | 0.719 | 0.876 | 0.969 | 0.313 |
| Checkpoint 1 | 0.734 | 0.967 | 0.987 | 0.249 |
| Bank | 0.814 | 1.094 | 1.060 | 0.287 |

So the oracle headline (0.691 to 0.575) overstates what the app gains from the filters: the rest of the gap is the detector. The other groups do hold on the detector's path: the fourth review's slice of seed 1, an app measurement at 902121c, oracle against detector: B2 0.231 and 0.233, C2 0.051 and 0.052, D2 0.000 and 0.000, G2 0.055 and 0.055, H2 0.057 and 0.061 with a place's tracks joined, I2 0.031 and 0.036; E2 is the exception.

## Whole set by condition

| I2, Farnsworth factor (character speed over overall speed) | Final | Checkpoint 1 | Bank |
|---|---|---|---|
| 1 to 2 | 0.054 | 0.349 | 0.039 |
| 2 to 4 | 0.028 | 0.504 | 0.053 |
| 4 to 8 | 0.015 | 0.525 | 0.228 |
| 8 and above | 0.017 | 0.583 | 0.675 |

| A2, S500 (dB SNR in 500 Hz) | Final | Checkpoint 1 | Bank |
|---|---|---|---|
| -10 to -5 | 0.970 | 0.971 | 0.998 |
| -5 to 0 | 0.519 | 0.601 | 0.739 |
| 0 to +5 | 0.157 | 0.156 | 0.201 |
| +5 to +10 | 0.007 | 0.007 | 0.030 |
| +10 to +20 | 0.000 | 0.000 | 0.018 |

| B2, S500 at least +10 dB, Doppler spread | Final | Checkpoint 1 | Bank |
|---|---|---|---|
| 0.05 to 0.2 Hz | 0.089 | 0.089 | 0.232 |
| 0.2 to 0.6 Hz | 0.103 | 0.103 | 0.389 |
| 0.6 to 2 Hz | 0.103 | 0.102 | 0.591 |
| 2 to 5 Hz | 0.128 | 0.128 | 0.753 |

| C2, S500 at least +10 dB, keying | Final | Checkpoint 1 | Bank |
|---|---|---|---|
| machine, computer | 0.000 | 0.000 | 0.019 |
| paddle | 0.022 | 0.022 | 0.048 |
| bug | 0.031 | 0.031 | 0.118 |
| hand | 0.156 | 0.148 | 0.436 |

## Seed 2, not tuned on

Seven groups, 247 recordings, oracle path.

| Group | Before `fast-3` | Three filters | Final |
|---|---|---|---|
| B2 fading | 0.251 | 0.242 | 0.242 |
| C2 fists | 0.061 | 0.062 | 0.062 |
| D2 speed changes | 0.000 | 0.000 | 0.000 |
| E2 interference | 0.784 | 0.677 | 0.694 |
| G2 QSO, same track | 0.040 | 0.040 | 0.040 |
| H2 QSO, separate tracks | 0.045 | 0.047 | 0.047 |
| I2 Farnsworth | 0.523 | 0.020 | 0.020 |

## Stress: a neighbor or a carrier inside the channel

The first independent review's recordings: a wanted station at +15 dB S500, 20 to 30 WPM, oracle path, about 120 symbols a cell. Offsets from the wanted station. `stress.csv`.

| Cell | 5aeb081, before the channel floor | `app-4` (b4323ab) | `app-5`, three filters | Final | Bank |
|---|---|---|---|---|---|
| Neighbor 30 Hz, -10 dB re wanted | 0.935 | 0.870 | 0.000 | 0.000 | 0.000 |
| Neighbor 60 Hz, 0 dB re wanted | 0.900 | 0.908 | 0.083 | 0.083 | 0.317 |
| Neighbor 30 Hz, 0 dB re wanted | 1.290 | 0.976 | 0.976 | 0.976 | 0.363 |
| Neighbor 100 Hz, 0 dB re wanted | 0.463 | 0.057 | 0.057 | 0.057 | 0.041 |
| Neighbor 100 Hz, +10 dB re wanted | 0.831 | 0.847 | 0.686 | 0.686 | 1.110 |
| Neighbor 100 Hz, +20 dB re wanted | 0.935 | 0.935 | 0.919 | 0.935 | 3.537 |
| Neighbor 150 Hz, +10 dB re wanted | 0.860 | 0.413 | 0.413 | 0.413 | 0.364 |
| Carrier 40 Hz, -10 dB re wanted | 0.829 | 0.932 | 0.000 | 0.000 | 0.051 |
| **Carrier 40 Hz, 0 dB re wanted** | 0.727 | 0.983 | **0.000** | **0.983** | 1.000 |
| Carrier 40 Hz, +10 dB re wanted | 0.727 | 0.983 | 0.983 | 0.983 | 1.000 |
| Carrier 40 Hz, +30 dB re wanted | 0.758 | 0.984 | 0.984 | 0.984 | 1.000 |
| Carrier 100 Hz, 0 dB re wanted | 0.729 | 0.042 | 0.042 | 0.042 | 0.280 |
| Carrier 100 Hz, +10 dB re wanted | 0.719 | 0.975 | 0.975 | 0.975 | 2.041 |

The one cell lost by the saving is the bold one, and it is narrow. A carrier at 0 dB re wanted, swept in offset (`carrier-sweep.csv`, 6 stations an offset):

| Offset, Hz | 25 | 30 | 35 | 40 | 45 | 50 | 55 | 60 | 70 | 80 |
|---|---|---|---|---|---|---|---|---|---|---|
| Oracle, final | 0.983 | 0.975 | 0.975 | **0.975** | 0.983 | 0.975 | 0.000 | 0.803 | 0.273 | 0.000 |
| Oracle, filters {1, 2, 3} | 0.983 | 0.975 | 0.975 | **0.000** | 0.983 | 0.975 | 0.000 | 0.803 | 0.236 | 0.000 |
| Detector's path, final | 0.975 | 0.970 | 0.975 | 0.975 | 0.983 | 0.979 | 1.000 | 0.987 | 0.620 | 0.004 |
| Detector's path, {1, 2, 3} | 0.975 | 0.970 | 0.975 | 0.975 | 0.983 | 0.979 | 1.000 | 0.987 | 0.599 | 0.004 |

A rectangular 24 ms filter has its null at 1/(24 ms) = 41.7 Hz, and a 36 ms one at 27.8 and 55.6 Hz (derived); the cell is where the deleted filter's null happened to fall (conjectured as the cause, the reading itself measured), and the app's path reads neither.

## The app

Measured with the engine and the app on the desktop, 3 worker threads for files and 4 for the app, each an app measurement at the commit named. `app-3` is 2749b04, `app-4` b4323ab, `app-5` 902121c, final e913886. All in `app-measures.csv`. The recordings:

- *Contest band:* 300 s generated, 110 runners and 170 callers, 280 calls. *Crowded band:* 60 s generated, 300 stations.
- *`W1AAA`:* a generated station at +15 dB S500 added to a slice of real 40 m beside a wide data signal, which used to close its place.
- *Clean band:* 30 generated stations at 192 kHz.
- *30 min real* and *62 s real:* real 40 m at 192 kHz, no truth; a call counts here when its shape fits and it is read at least twice or after DE (a heuristic, not a score).
- *3 s stall:* a stand-in radio delivers a 100-station band and stops for 3 s, 20 s in.
- *400 stations:* 25 s generated at 768 kHz.

| Measure | `app-3` | `app-4` | `app-5` | Final |
|---|---|---|---|---|
| Contest band: callers' calls, of 170 | 96 | 126 | 127 | 125 |
| Contest band: all calls, of 280 | 206 | 236 | 237 | 235 |
| Crowded band: calls, of 300 | 215 | 300 | 300 | 300 |
| Crowded band: CER | 0.351 | 0.015 | 0.015 | 0.015 |
| `W1AAA`: CER | 0.99 | 0.27 | 0.30 | 0.30 |
| Clean band: CER | 0.0166 | 0.0011 | 0.0013 | 0.0018 |
| Clean band: first words' CER | 0.65 | 0.00 | 0.00 | 0.00 |
| 30 min real: rows listed, hidden | 166, 743 | 202, 715 | 204, 712 | 204, 712 |
| 30 min real: channel-seconds | 19 458 | 23 062 | 23 115 | 23 115 |
| 30 min real: calls | 17 | 22 | 31 | 27 |
| 62 s real: rows listed, hidden | 34, 153 | 41, 163 | 43, 164 | 43, 164 |
| 62 s real: channel-seconds | 2 632 | 2 959 | 3 060 | 3 060 |
| 62 s real: calls | 4 | 6 | 6 | 6 |
| 3 s stall: tracks opened, budget left | 143, 100 | 100, none | 100, none | 100, none |
| Engine's build at 768 kHz, s | | 2.68 | 16.0 | 0.08 to 0.10 |
| 400 stations, file, 4 threads: s of wall for 25 s of stream | | 14.4 | 30.4 | 14.9 |
| 400 stations, stand-in radio, 60 s, 4 threads: samples lost | | | 12 372 864 | 0 |
| The same: budget at the end | | | 350 | none, all 400 rows listed |

The real radio was not used for this record. Measured on `app-4`, before the merge: 62 s at 192 kHz, 17.3 ms per channel-second, at most 21 tracks live; 66 s at 768 kHz, 14.0 ms per channel-second, at most 84 tracks live; no samples lost and no budget in either. Since the merge it has been run only as the stand-in.

## CPU against the bar

At most 40 ms per channel-second required, about 8 ms the goal, stated on the desktop. The whole process, on the desktop unless named.

| Run | `app-4` or checkpoint 1 | `app-5` | Final |
|---|---|---|---|
| Whole test set, the Linux machine | 5.61 | 9.52 | 6.87 (groups 4.23 for H2 to 9.23 for F2) |
| Whole test set, the desktop (derived) | | about 10.8 | about 7.3 to 7.8 |
| Contest band | 6.05 | 6.94 | 6.61 |
| Crowded band | 4.44 | 6.57 | 5.10 |
| 30 min real | 10.88 | 13.44 | 13.26 |
| 62 s real | 12.50 | 17.45 | 14.97 |
| First 300 s of the real 30 min, as fast as it goes, 4 threads | | 14.73 | 13.82 |
| The same on the stand-in radio, real time | | 21.34 | 16.91 |

The desktop's figure is derived: the Linux machine's 6.87 times the ratio on the same recordings on one thread, 1.06 to 1.16 (mean 1.13) on the review's three with `app-5`, 1.02 to 1.10 (mean 1.07) on three of the test set's with the final code. Real time costs about 1.22 times the batch figure (1.45 at `app-5`); a pipeline sleeping between blocks and waking to cold caches is the conjecture. So on the air read about 17 ms, inside the 40 ms required, over the goal.

The app's own split, CPU = per track + fixed, in the pipeline only (the threads' clock):

| Run | `app-4` | `app-5` | Final |
|---|---|---|---|
| 30 min real, first 8 min at up to 10 times real time | 9.77 ms per channel-second + 21.0 ms per s of stream | 12.98 + 21.8 | 12.16 + 21.5 |
| 30 min real, first 300 s, stand-in radio at real time | | 14.47 + 27.13 | 12.41 + 25.07 |
| 100-station band, real time | 4.78 + 25.3 | 7.03 + 26.3 | 5.42 + 25.9 |

Capacity, derived from these: (4 000 ms per s of stream - fixed) / per track on 4 threads at 192 kHz, about 320 real tracks and about 730 clean ones. So the goal's 500 tracks on half the desktop's cores holds on clean signals and not on real 40 m.

## Findings

1. **Measured.** The new fist fixes Farnsworth: to factors above 8, where the bank reads 0.675. Seed 2 agrees (0.020). What the gain is, at ARRL's spacing: checkpoint 1 already read I2's letters at 0.033 without spaces, and 42 959 of its 45 049 edits were word spaces; now 0.0027 without spaces and 1 648 space edits of 1 823 (`i2-kinds.csv`). The fourth review found, on its own recordings at 902121c, that another spacing keeps the letters and loses only word spaces, 0.05 to 0.21 (an app measurement).
2. **Measured.** Two filter widths take E2 from 0.691 to 0.575 and seed 2's from 0.784 to 0.694 on the oracle path, and on the detector's path from 0.734 to 0.688, late 0.669: the table above. Not fixed: a neighbor 30 Hz off at 0 dB re wanted (0.976; the bank's 0.363), and neighbors +10 dB re wanted and stronger within 150 Hz.
3. **Measured.** What it cost in text is small: C2 0.063 against 0.061 (seed 2: 0.062 against 0.061), hand keying 0.156 against 0.148, H2 on seed 2 0.047 against 0.045; in the app `W1AAA` 0.30 against 0.27 and the clean band 0.0018 against 0.0011.
4. **Measured.** The replay carries over: on the detector's path first-word CER is 0.284 against 0.681, A2 0.250 against 0.274. Oracle signals do not have it.
5. **Measured. Correcting the record's earlier conclusion.** The record said that 400 clean stations at 768 kHz run slower than real time and that a live radio's budget would shed there. They do not. The 30.4 s of wall was about 16 s of building the engine and about 15 s for the 25 s of stream: the tables scaled as filter scale times rate squared, on the pipeline's thread, with the radio already running, and at that rate a radio start lost its first 16 s and a center change froze the window as long. With the tables on 48 kHz: 14.9 s of wall for 25 s of stream, 1.7 times real time on 4 threads, longest read 0.21 s, no samples lost, no budget, all 400 rows, and two center changes in the first 5 s answer at once.
6. **Measured. The carrier "regression" is not one.** `fast-3` reported that a carrier +10 dB re wanted or stronger now reads 0.98 where it read 0.72. The 0.72 is the first review's run on 5aeb081, before the channel floor; `app-4`, just before `fast-3`, already reads 0.975 to 0.984 in those cells, and the 0.72 was a row of false T's, some matching by chance. In the app, with text on real 40 m (steady carriers on for 180 s, 3 alone from +25 to +55 dB S500, 8 beside a station at +25 dB S500), the 26 rows at every carrier and every station are identical before and after `fast-3` (at b4323ab and 902121c): a carrier alone writes 5 T's in hidden rows and lists nothing.
7. **Measured, on the real recordings, at b4323ab and 902121c.** A carrier here is a bin steady over 1 s, at least 10 dB above the local median for at least 5 s running (a heuristic for this question). The 30 min recording has 374 such runs at 89 places. Within 150 Hz of them, before and after `fast-3`: 102 rows and 1 082 symbols, 859 listed, 99 of the 102 rows identical; no call read twice in either. In the 62 s recording: 11 rows and 34 symbols, 10 identical. So on real recordings it does not matter.
8. **Measured, and old.** What does matter is on the detector's path: a station within 47 Hz of a carrier that came first is not read with the carrier at its own level or above, nor at 100 Hz from one +10 dB re wanted or stronger: at 40 Hz, one station each, CER 0.95 at `app-5` and 0.19 now at -10 dB re wanted, 1.00 from 0 dB up; at 100 Hz 0.07 at -10 dB, 0.02 at 0 dB, 1.00 at +10 and +30 dB (an app measurement at e913886). Conjectured: the track sits on the carrier, as the row's frequency shows, and the judge retires it.
9. **Conjectured; no truth.** Calls on the 30 min recording went 22, 31, then 27 with the channel's clock. Of the 12 gained at the merge, about 9 to 10 are not new stations (`IK0HBT`, `K0HBT`, `EK0HBN`, `IK0HT`, `IK6M`, `K6QO`, `N5JJK`, `X9F`, `S5ME`, read beside `IK0HBN`, `IK6QOP`, `N5JJ`, `KX9F`, `S58W`), and 2 to 3 are: `S58W` is real and exact, `KX9FK` is `KX9F` and K glued, `D5NH` is probably a real station with its first letter unread. Three were lost, one real: `IK6QOP`, read exactly once at `app-5`. With the channel's clock six go (`D5NH`, `IK6M`, `K6QO`, `KX9FK`, `TK0HBN`, `X9F`) and two come (`B5NHS`, `T6Q`): mostly misreadings and fragments, with `KX9F` still there with spaces removed. The fourth review counts about 12 to 14 stations behind the names. With spaces removed the real calls were already there at `app-4`, so the callsign stage should match with spaces removed. The comparison with CW Skimmer is what can say.
10. **Measured. These scores flatter the text prior near threshold, callsigns most of all.** The same stations, levels and noise with different text, oracle path, 96 stations a text, 15 to 35 WPM (`text-kinds.csv`):

| Text | S500 -3 to +1 dB | S500 +1 to +5 dB |
|---|---|---|
| The test set's (on-air statistics) | 0.216 | 0.007 |
| English | 0.238 | 0.010 |
| Callsigns | 0.334 | 0.018 |
| 5-letter code groups | 0.360 | 0.019 |

Callsigns are the bar, so near threshold expect 1.5 to 2.6 times the CER the test set shows.

## What was adopted, and what it lost

Filters {1, 3} and a drop margin of 30 nats (the narrower stream is dropped when over 30 nats behind, not 100), with both fixes: whole set 9.52 to 6.87 ms, seed 2 8.25 to 5.84 ms, real 62 s 17.45 to 14.97 ms. Lost: oracle E2 0.562 to 0.575 and seed 2's 0.677 to 0.694 (on the app's path E2 improves); contest callers 127 to 125 of 170; the clean band 0.0013 to 0.0018; the one carrier cell above; the 100 Hz, +20 dB re wanted cell 0.919 to 0.935. On the review's 30 pairs (wanted +15 dB S500, neighbor 40 to 150 Hz; single realizations, an app measurement at e913886) 0.353 early and late, against the review's 0.312 late with three filters and the clock's fix, and 0.376 late as merged.

## Limits

The test set's noise is white; a narrower filter on the detector's path pays for a track up to half a bin off, which the oracle rows do not show. S - F, the choice of stream, is a heuristic between filters whose data differ, and a long track whose conditions change keeps its early leader. Memory about 3 MB a track. The list is still 35% to 63% junk on real 40 m until the callsign stage. On the air the largest single CPU item is the widest stream's search on weak and wide channels (55% of the decoder's time on real 40 m, the fourth review's counters at 902121c). The app's measurements are single runs on the desktop, the tools and generators for them are scratch, not committed, and the recordings are not committed; the app's numbers are totals in `app-measures.csv`, not per signal, and that part waits for checkpoint 3. See `docs/signal-processing.md`, appendices A.8e and A.9b.

## What comes next

Merge to `main`, this checkpoint to the owner, his first run on the radio. The callsign stage (`call-1`), matching with spaces removed. The busy-band hour and CW Skimmer's output for checkpoint 3, the bar's other half. Then the CPU of the widest stream's search on weak channels, the real 40 m figure.

## Reproducing

Recordings: `python -m kz4ap_synth.suites` from the seeds. Run: `--decoder segmental` at e913886, recordings as named in the table's `test_case` column. Tables: `kz4ap_proto.devset2.load_rows(out, [name], only=None)` and `write_scores`. Each group above is the sum of `edits` over the sum of `symbols`.
