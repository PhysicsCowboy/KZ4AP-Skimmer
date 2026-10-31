# Checkpoint 2: first working app (2026-10-31)

**Answer.** The app works on the merged decoder, and the decoder wins every group of the test set against the bank, Farnsworth
included: I2 reads CER 0.021 against checkpoint 1's 0.523 and the bank's 0.261. E2 interference reads 0.504 against 0.691 and 0.776
when told the frequency, and 0.675 against 0.734 and 0.814 as the app reads it. No group of the test set holds a strong station beside a strong neighbor; on such pairs, generated apart from it, the decoder before this round lost to the bank from about +20 dB S500 (0.777 against 0.191 at +40 dB S500), and a floor for a neighbor's leak now beats the bank overall there (0.135), though the bank stays better at 40 Hz. CPU on the whole test set is 6.89 ms per channel-second on
the Linux machine, about 7.4 to 7.8 ms on the desktop (derived), at the 8 ms goal. On 30 min of real 40 m it is 12.62 ms, about 18 to 21 ms in real time,
under the 40 ms required and over the goal. The call list reports 221 of the contest's 280 stations with 7 false calls, all 300 of the crowded band, and on 30 min of real 40 m 20 rows, about 17 stations; "The callsign stage" below. A person reading that text finds about 21 stations, up to 26 or 27 (review 5's judgment), so the list still misses about 4 to 10. How that compares with CW Skimmer only a replay of that recording through CW Skimmer can say. No decoder has yet been scored against truth on a real signal; that is checkpoint 3's.

**Terms.** *CER*: edits over reference symbols, spaces counted, pooled over the scored signals. *Oracle path*: the decoder is told each station's
frequency. *Detector's path*: what the app runs, the detector opening a channel at each peak. *Late*: a channel that opens more than 10 s after the stream
began; no station of the test set does (all start at 0.5 to 2 s), so "late" runs put 15 s of noise first. *Stream*: one filter width's evidence and search inside a channel. *S500*: key-down power over noise power
in 500 Hz, dB. *dB re wanted*: relative to the wanted station's key-down power. *Stand-in radio*: a DLL with the SDRplay API's names that plays a WAV through the real `SdrplaySource`; the owner's radio was not used. *Final* below is `app-8` at 8b80328; *`app-7`* is its base (decoder e913886, stage 4bf5446). *Review 5:* the independent review of `app-7`, whose tested fixes this round took.

## How to try it

`README.md`, "Try the app (Windows)": the steps as typed, from an empty folder on the desktop (configure 1.4 min, build 2.3 min; generating a recording about 40 s). One step differs from a literal reading on this desktop, by the README's own direction: `python` here is Windows' Store stub, so `$py` was set to a Python 3.12's full path, as the README says to. No Python is needed for the first try: in the repository's own folder, git-ignored, `.venv` exists, and `build\recordings\real30.wav` is the 30 min of real 40 m, so after the build

    build\windows\app\Release\kz4ap-skimmer.exe build\recordings\real30.wav

Please try 192 kHz on the radio first, then 768 kHz: the radio has not been run since the merge, and that first run is the test.

## What changed since checkpoint 1

The decoder gained Farnsworth's gaps in the fist (a story's state is now speed and gap stretch), and a second filter width: each channel runs a 12 ms and a 36 ms filter, and one stream's whole text goes out, chosen by the evidence for Morse over a timing-free key. The app gained the replay (2 s of spectrum, so a new channel reads what it missed), a judge retiring what is not keyed Morse, holds at retired places, a track budget, and recording. At the review's request: the engine's tables are computed once at 48 kHz, so it builds in 0.08 to 0.10 s at 768 kHz, not 16 to 18 s; the choice of filter runs on the channel's own age; the filter at 24 ms is gone (below). Review 5's fixes: a neighbor's leak now counts as noise (below); five rules of the callsign stage; the stage's clock in the order of the events.

**How the decoder works now.** Each channel: a matched filter per width, an envelope sample per 2.667 ms (widest), one shared level belief per channel, and a search over stories (speed, gap stretch, text) touched only at element ends, every 0.25 s the best story's text; each stream's noise is at least what the spectrum says leaks in from beside the channel. Details: `docs/signal-processing.md`, section 8e.

## Runs

The whole set is seed 1 of the development set `DEV2`, on the Linux machine, 8 jobs on 10 cores: 498 recordings on the oracle path and 210 on the detector's path. Tables are in `docs/plans/data/`, this checkpoint's in `2026-10-31-checkpoint-2/`.

| Label | Decoder and commit | Recordings | Channel-seconds | CPU, ms per channel-second | Table |
|---|---|---|---|---|---|
| Final, whole | `segmental`, 8b80328 (`app-8`; later commits change documents only) | 708 | 1 391 311 | 6.89 (decoder 6.47) | `segmental-app8-whole.csv`; CPU `cpu-app8-whole.csv` |
| `app-7`, whole | `segmental`, e913886 | 708 | 1 391 311 | 6.87 (decoder 6.46) | as committed at 9500ff2: `segmental-app6-whole.csv`, `cpu-app6-whole.csv` |
| `app-5`, whole | `segmental`, 902121c: three filters, stream's clock | 708 | 1 391 335 | 9.52 | as committed at 949b0f3: `segmental-app5-whole.csv`, `cpu-app5-whole.csv` |
| Checkpoint 1, whole | `segmental`, d78d45f | 708 | 1 395 785 | 5.61 | `2026-10-31-checkpoint-1/segmental-fast2-whole.csv` |
| Bank, whole | `bank`, task D5's replay | 498 | not recorded here | not recorded here | `2026-10-06-development-set/d5-bank.csv` |
| Final, seed 2 | `segmental`, 8b80328 | 247 | 560 413 | 5.87 | `segmental-app8-seed2.csv`; `cpu-app8-seed2.csv` |
| `app-7`, seed 2 | `segmental`, e913886 | 247 | 560 413 | 5.84 | as committed at 9500ff2: `segmental-app6-seed2.csv`, `cpu-app6-seed2.csv` |
| Three filters, seed 2 | `segmental`, `fast-3` (3f8a9ff) | 247 | 560 412 | 8.25 | as committed at 949b0f3: `segmental-fast3-seed2.csv` |
| Before `fast-3`, seed 2 | `segmental`, `fast-3`'s base | 247 | 560 412 | 5.04 | `segmental-fast2-seed2.csv` |

The CPU of the runs that have no CPU table here (checkpoint 1, and `fast-3` and its base on seed 2) comes from their result files, git-ignored. `.../conditions-b2-c2.csv` of checkpoint 1 gives each B2 and C2 signal's Doppler spread and keying. Every CER below is computed from these rounded tables. Tests at 81d9419 (later commits change documents only): `ctest`, Windows with Qt 815 tests, 796 passed, 19 skipped, 0 failed; Windows without Qt the same 815; Linux 802, 783 passed, 19 skipped, 0 failed. `pytest` on `training`, 325 passed, on both. The skipped are as designed: expected failures and measurements.

## Scores by group

| Group | Final | `app-7` | `app-5` | Checkpoint 1 | Bank |
|---|---|---|---|---|---|
| A2 sensitivity | 0.226 | 0.226 | 0.226 | 0.241 | 0.287 |
| A2, detector's path | 0.250 | 0.250 | 0.250 | 0.274 | |
| B2 fading | 0.242 | 0.242 | 0.241 | 0.251 | 0.557 |
| C2 fists | 0.063 | 0.063 | 0.063 | 0.061 | 0.157 |
| D2 speed changes | 0.000 | 0.000 | 0.000 | 0.000 | 0.027 |
| E2 interference | 0.504 | 0.575 | 0.562 | 0.691 | 0.776 |
| F2 tuning | 0.085 | 0.085 | 0.085 | 0.087 | 0.133 |
| G2 QSO, same track | 0.055 | 0.055 | 0.055 | 0.055 | 0.206 |
| H2 QSO, separate tracks | 0.056 | 0.056 | 0.057 | 0.058 | 0.198 |
| I2 Farnsworth | 0.021 | 0.021 | 0.021 | 0.523 | 0.261 |
| S2 stretch | 0.237 | 0.237 | 0.237 | 0.264 | 0.337 |

E2 counts only the wanted stations (90 signals); G2 has 20, H2 40. By signal, final against checkpoint 1: 7 640 better, 12 124 the same, 1 070 worse; against `app-7`: 54 better, 20 729 the same, 51 worse (E2 34 signals differ, B2 66, C2 3, G2 2). What the leak floor moves in E2: neighbor 0 to 25 Hz 0.927 to 0.930, 25 to 60 Hz 0.745 to 0.506, 60 to 120 Hz 0.052 to 0.076. On the detector's path 5 607 of 6 720 signals are detected, in both decoders; first-word CER over all 6 720 (20 627 first-word symbols) 0.284 against checkpoint 1's 0.681, and over the 5 607 detected (17 173 symbols) 0.140 against 0.617.

**E2 as the app reads it** (`e2-detector.csv`; detector's path; 90 signals):

| Variant | All | Neighbor 0 to 25 Hz | 25 to 60 Hz | 60 to 120 Hz |
|---|---|---|---|---|
| Final, stations at 0.5 to 2 s | 0.675 | 0.851 | 0.961 | 0.212 |
| Final, late | 0.658 | 0.855 | 0.908 | 0.210 |
| `app-7`, stations at 0.5 to 2 s | 0.688 | 0.851 | 0.961 | 0.250 |
| `app-7`, late | 0.669 | 0.851 | 0.910 | 0.244 |
| `app-5`, stations at 0.5 to 2 s | 0.705 | 0.852 | 1.011 | 0.251 |
| `app-5`, late | 0.719 | 0.876 | 1.010 | 0.272 |
| `app-5`, late, widest filter alone | 0.719 | 0.876 | 0.969 | 0.313 |
| Checkpoint 1 | 0.734 | 0.967 | 0.987 | 0.249 |
| Bank | 0.814 | 1.094 | 1.060 | 0.287 |

So the oracle headline (0.691 to 0.504) overstates what the app gains from the filters: the rest of the gap is the detector. The other groups do hold on the detector's path: the fourth review's slice of seed 1, an app measurement at 902121c, oracle against detector: B2 0.231 and 0.233, C2 0.051 and 0.052, D2 0.000 and 0.000, G2 0.055 and 0.055, H2 0.057 and 0.061 with a place's tracks joined, I2 0.031 and 0.036; E2 is the exception.

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
| 0.6 to 2 Hz | 0.104 | 0.102 | 0.591 |
| 2 to 5 Hz | 0.127 | 0.128 | 0.753 |

| C2, S500 at least +10 dB, keying | Final | Checkpoint 1 | Bank |
|---|---|---|---|
| machine, computer | 0.000 | 0.000 | 0.019 |
| paddle | 0.022 | 0.022 | 0.048 |
| bug | 0.031 | 0.031 | 0.118 |
| hand | 0.156 | 0.148 | 0.436 |

## Seed 2, not tuned on

Seven groups, 247 recordings, oracle path.

| Group | Before `fast-3` | Three filters | `app-7` | Final |
|---|---|---|---|---|
| B2 fading | 0.251 | 0.242 | 0.242 | 0.243 |
| C2 fists | 0.061 | 0.062 | 0.062 | 0.062 |
| D2 speed changes | 0.000 | 0.000 | 0.000 | 0.000 |
| E2 interference | 0.784 | 0.677 | 0.694 | 0.673 |
| G2 QSO, same track | 0.040 | 0.040 | 0.040 | 0.040 |
| H2 QSO, separate tracks | 0.045 | 0.047 | 0.047 | 0.047 |
| I2 Farnsworth | 0.523 | 0.020 | 0.020 | 0.020 |

By signal against `app-7`: 36 better, 6 536 the same, 70 worse. The leak floor's gain in E2 is smaller on this seed (0.694 to 0.673, 3%) than on seed 1 (0.575 to 0.504, 12%), the seed its setting was chosen on.

## Stress: a neighbor or a carrier inside the channel

The first independent review's recordings: a wanted station at +15 dB S500, 20 to 30 WPM, oracle path, about 120 symbols a cell. Offsets from the wanted station. `stress.csv`.

| Cell | 5aeb081, before the channel floor | `app-4` (b4323ab) | `app-5`, three filters | `app-7` | Final | Bank |
|---|---|---|---|---|---|---|
| Neighbor 30 Hz, -10 dB re wanted | 0.935 | 0.870 | 0.000 | 0.000 | 0.000 | 0.000 |
| Neighbor 60 Hz, 0 dB re wanted | 0.900 | 0.908 | 0.083 | 0.083 | 0.000 | 0.317 |
| Neighbor 30 Hz, 0 dB re wanted | 1.290 | 0.976 | 0.976 | 0.976 | 0.976 | 0.363 |
| Neighbor 100 Hz, 0 dB re wanted | 0.463 | 0.057 | 0.057 | 0.057 | 0.000 | 0.041 |
| Neighbor 100 Hz, +10 dB re wanted | 0.831 | 0.847 | 0.686 | 0.686 | 0.280 | 1.110 |
| Neighbor 100 Hz, +20 dB re wanted | 0.935 | 0.935 | 0.919 | 0.935 | 1.000 | 3.537 |
| Neighbor 150 Hz, +10 dB re wanted | 0.860 | 0.413 | 0.413 | 0.413 | 0.008 | 0.364 |
| Carrier 40 Hz, -10 dB re wanted | 0.829 | 0.932 | 0.000 | 0.000 | 0.000 | 0.051 |
| **Carrier 40 Hz, 0 dB re wanted** | 0.727 | 0.983 | **0.000** | **0.983** | **0.983** | 1.000 |
| Carrier 40 Hz, +10 dB re wanted | 0.727 | 0.983 | 0.983 | 0.983 | 0.983 | 1.000 |
| Carrier 40 Hz, +30 dB re wanted | 0.758 | 0.984 | 0.984 | 0.984 | 0.984 | 1.000 |
| Carrier 100 Hz, 0 dB re wanted | 0.729 | 0.042 | 0.042 | 0.042 | 0.000 | 0.280 |
| Carrier 100 Hz, +10 dB re wanted | 0.719 | 0.975 | 0.975 | 0.975 | 1.000 | 2.041 |

The one cell lost by the two-filter saving is the bold one, and it is narrow. A carrier at 0 dB re wanted, swept in offset (`carrier-sweep.csv`, 6 stations an offset):

| Offset, Hz | 25 | 30 | 35 | 40 | 45 | 50 | 55 | 60 | 70 | 80 |
|---|---|---|---|---|---|---|---|---|---|---|
| Oracle, final | 0.983 | 0.975 | 0.975 | 0.975 | 0.983 | 0.975 | **0.979** | 0.815 | **0.000** | 0.000 |
| Oracle, `app-7` | 0.983 | 0.975 | 0.975 | 0.975 | 0.983 | 0.975 | 0.000 | 0.803 | 0.273 | 0.000 |
| Oracle, final with filters {1, 2, 3} | 0.983 | 0.975 | 0.975 | **0.646** | 0.983 | 0.975 | 0.979 | 0.815 | 0.000 | 0.000 |
| Detector's path, final | 0.975 | 0.970 | 0.975 | 0.975 | 0.983 | 0.979 | 1.000 | 0.987 | 0.620 | 0.004 |

A rectangular 24 ms filter has its null at 1/(24 ms) = 41.7 Hz, and a 36 ms one at 27.8 and 55.6 Hz (derived); the cell is where the deleted filter's null happened to fall (review 5 confirmed the cause in 2 Hz steps: a null at k / T, T = m x 12 ms), and the app's path reads neither. The same at 55 Hz, the 36 ms filter's null: `app-7` read it, and the leak floor, which sees the carrier's skirt at 70 Hz, no longer does.

## A strong station beside a strong neighbor (review 5)

No group of the test set holds this: E2's wanted stations stand at 8 to 14 dB S500, and the stress table's at +15 dB S500. Pairs generated for it (`level-pairs.csv`): 48 kHz, 2 seeds, machine keying, 20 to 30 WPM, the neighbor no longer than the wanted station, oracle path. CER of the wanted station, all offsets 40 to 300 Hz and neighbors -10, 0 and +10 dB re wanted pooled:

| Wanted S500 | Without the leak floor (as `app-7`) | Final | Bank | Final, detector's path |
|---|---|---|---|---|
| +10 dB | 0.129 | 0.093 | 0.226 | 0.212 |
| +15 dB | 0.160 | 0.140 | 0.264 | 0.222 |
| +20 dB | 0.324 | 0.126 | 0.239 | 0.224 |
| +25 dB | 0.430 | 0.181 | 0.184 | 0.254 |
| +30 dB | 0.573 | 0.176 | 0.211 | 0.238 |
| +35 dB | 0.663 | 0.159 | 0.223 | 0.263 |
| +40 dB | 0.777 | 0.135 | 0.191 | 0.215 |

Neighbor at 0 dB re wanted, by offset, without the leak floor and final:

| Wanted S500 | 40 Hz | 60 Hz | 80 Hz | 100 Hz | 120 Hz | 150 Hz | 200 Hz | 250 Hz up |
|---|---|---|---|---|---|---|---|---|
| +10 dB, without | 0.186 | 0.254 | 0.000 | 0.000 | 0.000 | 0.000 | 0.000 | 0.000 |
| +10 dB, final | 0.186 | 0.070 | 0.000 | 0.000 | 0.000 | 0.000 | 0.000 | 0.000 |
| +20 dB, without | 0.810 | 0.786 | 0.397 | 0.431 | 0.433 | 0.000 | 0.000 | 0.000 |
| +20 dB, final | 0.802 | 0.308 | 0.000 | 0.000 | 0.000 | 0.000 | 0.000 | 0.000 |
| +30 dB, without | 0.720 | 0.902 | 1.541 | 0.858 | 1.100 | 0.466 | 0.000 | 0.000 |
| +30 dB, final | 0.746 | 0.049 | 0.000 | 0.000 | 0.000 | 0.000 | 0.000 | 0.000 |
| +40 dB, without | 0.787 | 0.800 | 1.083 | 1.033 | 1.119 | 1.169 | 0.808 | 0.000 |
| +40 dB, final | 0.811 | 0.000 | 0.000 | 0.000 | 0.000 | 0.000 | 0.000 | 0.000 |

Not cured: 40 Hz, where the bank reads 0.148 to 0.388 at 0 dB re wanted and 0.000 to 0.025 at -10 dB re wanted against 0.068 to 0.839 here; a neighbor +10 dB re wanted at 80 Hz, 0.906 to 0.983 at every level, and inside that; much stronger neighbors far off. So the bank stays the better at 40 Hz with a neighbor at or below the wanted station's level. The bank is no answer overall: through the same stage on the contest it lists 147 true and 8 false (review 5, not rerun).

Whether it matters on the air is untested: 30 min of real 40 m is a quiet band, and the list there hardly moves with this (below). Review 5 found 2 station-minutes with a neighbor's leak at +3 dB re the wide filter's noise below 7072 kHz, 63 above, in the digital segment. A contest weekend should show it (conjecture); that is checkpoint 3's busy-band hour.

## The app

Measured with the engine and the app on the desktop, 3 worker threads for files and 4 for the app, each an app measurement at the commit named. `app-3` is 2749b04, `app-4` b4323ab, `app-5` 902121c, `app-7` e913886, final 8b80328. All in `app-measures.csv`. The recordings:

- *Contest band:* 300 s generated, 110 runners and 170 callers, 280 calls. *Crowded band:* 60 s generated, 300 stations.
- *`W1AAA`:* a generated station at +15 dB S500 added to a slice of real 40 m beside a wide data signal, which used to close its place.
- *Clean band:* 30 generated stations at 192 kHz.
- *30 min real* and *62 s real:* real 40 m at 192 kHz, no truth; a call counts here when its shape fits and it is read at least twice or after DE (a heuristic, not a score).
- *3 s stall:* a stand-in radio delivers a 100-station band and stops for 3 s, 20 s in.
- *400 stations:* 25 s generated at 768 kHz.

| Measure | `app-3` | `app-4` | `app-5` | `app-7` | Final |
|---|---|---|---|---|---|
| Contest band: callers' calls, of 170 | 96 | 126 | 127 | 125 | 132 |
| Contest band: all calls, of 280 | 206 | 236 | 237 | 235 | 242 |
| Crowded band: calls, of 300 | 215 | 300 | 300 | 300 | 300 |
| Crowded band: CER | 0.351 | 0.015 | 0.015 | 0.015 | 0.015 |
| `W1AAA`: CER | 0.99 | 0.27 | 0.30 | 0.30 | 0.30 |
| Clean band: CER | 0.0166 | 0.0011 | 0.0013 | 0.0018 | 0.0018 |
| Clean band: first words' CER | 0.65 | 0.00 | 0.00 | 0.00 | 0.00 |
| 30 min real: rows listed, hidden | 166, 743 | 202, 715 | 204, 712 | 204, 712 | 174, 729 |
| 30 min real: channel-seconds | 19 458 | 23 062 | 23 115 | 23 115 | 21 897 |
| 30 min real: calls | 17 | 22 | 31 | 27 | 24 |
| 62 s real: rows listed, hidden | 34, 153 | 41, 163 | 43, 164 | 43, 164 | 37, 163 |
| 62 s real: channel-seconds | 2 632 | 2 959 | 3 060 | 3 060 | 2 748 |
| 62 s real: calls | 4 | 6 | 6 | 6 | 6 |
| 3 s stall: tracks opened, budget left | 143, 100 | 100, none | 100, none | 100, none | 100, none |
| Engine's build at 768 kHz, s | | 2.68 | 16.0 | 0.08 to 0.10 | 0.08 |
| 400 stations, file, 4 threads: s of wall for 25 s of stream | | 14.4 | 30.4 | 14.9 | 15.0 |
| 400 stations, stand-in radio, 60 s, 4 threads: samples lost | | | 12 372 864 | 0 | 0 |
| The same: budget at the end | | | 350 | none, all 400 rows listed | none, all 400 rows listed |

The rows and channel-seconds on real 40 m fall with the leak floor (conjecture: a track reads less where a neighbor raises its noise, and is retired sooner): what the list loses is measured below, where the real stations' text falls 4.7% and their exact call words rise.

The real radio was not used for this record. Measured on `app-4`, before the merge: 62 s at 192 kHz, 17.3 ms per channel-second, at most 21 tracks live; 66 s at 768 kHz, 14.0 ms per channel-second, at most 84 tracks live; no samples lost and no budget in either. Since the merge it has been run only as the stand-in.

## The callsign stage

**What it does.** `CallSearch` reads the settled text and lists one row per call: `docs/signal-processing.md`, section 11. It is scored here by base call: *true* is a reported call that is a station of the recording, *false* one that is not, *precision* true over reported, *recall* true over stations. Measured over whole decodes with `kz4ap-calls`, on the desktop for the band recordings and the Linux machine for G2 and H2. The tables are named below.

*As merged:* the stage at a277ed0, on `app-6`'s filters. *`app-7`:* the stage at 4bf5446, tuned, on `app-6`'s decoder. *Final:* 8b80328, with the leak floor and the five rules below. *Older count:* the rule the record's "calls" used so far: on the listed rows, a word of a call's shape read at least twice, or once after DE or CQ.

| Recording (stations) | Older count, true, false | As merged: true, false | `app-7`: true, false | Final: true, false, precision, recall |
|---|---|---|---|---|
| Contest band (280) | 203, 22 | 199, 0 | 202, 0 | 221, 7, 0.969, 0.789 |
| Crowded band, `w12` (300) | 300, 4 | 300, 0 | 300, 0 | 300, 0, 1.000, 1.000 |
| 100 stations, 192 kHz (100) | 100, 1 | 99, 0 | 99, 0 | 100, 0, 1.000, 1.000 |
| Pairs, the review's, 40 to 150 Hz apart (48) | 32, 2 | 32, 0 | 32, 0 | 39, 0, 1.000, 0.813 |
| G2, QSOs on one track, seed 1 (40) | | 40, 0 | 40, 0 | 40, 0, 1.000, 1.000 |
| G2, seed 2 (40) | | 40, 0 | 40, 0 | 40, 0, 1.000, 1.000 |
| H2, QSOs on two tracks, seed 1 (40) | | 38, 2 | 38, 2 | 40, 1, 0.976, 1.000 |
| H2, seed 2 (40) | | 40, 3 | 40, 4 | 40, 0, 1.000, 1.000 |

`calls-generated.csv` (per recording, with the false and the missed calls), `calls-older-count.csv` (on `app-7`'s decoder, not rerun: it is the old rule). Of the true calls, within 100 Hz of their station: contest 220 of 221, H2 seed 1 40 of 40, seed 2 40 of 40. The contest's 7 false: 4D1O, DL0SZ, DW5AE, IL9ZR, JA3XI, K4LFON, S91KD. H2's 1: PA8PG. With a known-call list of 50 000 calls, generated, holding truth's calls at 0%, 70% or 100% coverage (`calls-known.csv`): contest true and false 217 and 17, 225 and 14, 228 and 13; `w12` 295 and 5, 299 and 1, 300 and 0; on the real recordings the lists change nothing. A real list, MASTER.SCP of 2026-10-30 (`calls-rules.csv`, `known_list`): contest 222 and 8, G2 79 and 1, H2 80 and 3; on real 40 m the same 20 rows; on 62 s of real band the same 7; on 20 s, K3AV more. Against the five rules' gain, a list is close to nothing here. That is on this decoder's text; its value against CW Skimmer's table of repeats is untested.

**Real recordings, no truth.** By hand, nothing looked up, then checked against review 5's reading of all the text and against MASTER.SCP (`calls-real.csv`, each call's judgment and reason).

| Recording | Older count | As merged | `app-7` | Final | Final, by judgment |
|---|---|---|---|---|---|
| 30 min real | 27 | 12 | 16 | 20 | 15 likely real amateurs, 1 of them misspelled in the row (PY2UDTE for PY2UD); 1 misspelling of a likely real one (K6QOP for IK6QOP); 1 transmitter outside the amateur band (WX2ZAQ, 6992.0 kHz); 3 misreadings or false (IK0S and K0S, the start of IK0HBN; N4SFL) |
| 62 s real | 6 | 6 | 6 | 7 | 1 real, 6 likely real |
| 20 s real | | 2 | 2 | 2 | not judged |

So **17 stations in 20 rows** on the 30 min, 14 in 16 rows before (review 5's count; the record then said about 13, the same without WX2ZAQ). Against `app-7`: + KF0YUX, NQ6B, PY2UDTE (true), K0S, N4SFL (false); - IK6QO (a misspelling; K6QOP stays). A person reading the same text finds about 21 stations, up to 26 or 27 (review 5's judgment of 1 147 tracks' text): still missing are IV3TMV, IT9YBL, K5UR, HK3QQ, damaged in the text, which is the decoder's or the tracking's to fix and not the stage's. Real stations' text against `app-7`: 4 749 symbols against 4 984 at the 19 real stations' places (-4.7%), 62 exact call words against 59 (K0TF +3, WA0QZK +1, S58W -1); by review 5's guess CW Skimmer would spot 15 to 18 here (conjecture, from its manual). Its replay is what can say.

The app's list is `calls-app-lists.csv`: **20 at every pace** (`--speed max` twice, 191 and 193 s of wall; `--speed 5`; `--speed 1`, 1 800 s), the same rows as `kz4ap-calls`. The 15 against 16 in the previous version of this record was a race: a word that stands 10.30 s and a clock that came after the batch. The clock now comes among the events. A row can still hang on one transient reading (K6QOP; A.12).

**What it dropped.** (At the merge, on `app-7`'s decoder.) The stage as merged reports 7 of the older count's 27 calls and drops 20. For each of them, the frequency, times, readings, text around each reading and rule are in the scratch file named under Reproducing, not committed, since it holds decoded text; `calls-real30-dropped.csv` has all of that except text.

| Judgment, as merged | Calls | Rule that refused them |
|---|---|---|
| Likely real, 2 | EA1DR | points: 2 bare, 3 needed |
| | ZL1TM | never read: the word came with a correction at 176 s, 50 s after the stage had read OL1TM there |
| Misspellings of a likely real station, 2 | IK6QO, K6QOP | points: 2 and 2.5 bare |
| Uncertain, 1 | B5NHS | never read, likewise; with the re-read, 1 point after DE |
| Likely misreadings, 15 | Beside IK0HBN: EK0HBN, IK0HBT, IK0HN, IK0HT, IK0S, K0HBN, K0HBT | points, or variants held back by a better-supported call, or both |
| | Beside E51MWA: E51M, EK1MWA. Beside S58W: S5ME | points 1; S5ME has no digit after the prefix |
| | K glued on: KA3LOCK, N5JJK | points or variants |
| | Fragments: E5E, H1MO, T6Q | not a call's form |

So the older count's 27 mostly were misreadings, 17 of its 20 lost, of which 2 do point to a real station; but 3 real stations were lost, and one of them had been replaced by a misreading. By rule: 4 grammar, 2 never read, 6 points alone, 5 points and variants, 3 variants alone.

**What changed at `app-7`.** Three rules. Each is a heuristic, each changes what real 40 m reads, and EA1DR and ZL1TM prompted them, so the gain on that recording is not independent; the generated recordings measure only the cost, and no list changed on 62 s or 20 s of real band.

1. *A correction of text already read has it read again* (`reread_corrected`), on text kept 200 s back. Corrections on real 40 m reach up to 164.9 s, 99% within 39.0 s, on 2 393 that change text; reading only final text, at a setting of 1e6 s, gains ZL1TM and loses E51MWA, so old readings stay: neither stream's text is always the better.
2. *A tie at edit distance 1 between calls that begin within 1 s goes to the later,* what the correction made of the word. Without it OL1TM stays and holds ZL1TM back.
3. *A bare call needs 2 points, not 3* (`points_bare`). Two exact readings at one place are enough, as at CW Skimmer's Normal level for a common pattern.

Against as merged. Contest: 3 true more, no false more. H2 seed 2: 1 false more (N5PX and DE, glued, read in corrected text). Every other generated recording: the same. 30 min real: + EA1DR, ZL1TM, K6QOP, IK6QO, IK0S; - OL1TM. The stage also now reads on whole seconds of stream, so that replays and whole decodes agree: 0 of 88 lists differ between them, where one did.

**Settings tried at `app-7`: 15, besides as merged,** all on all these recordings, all in `calls-settings.csv` (on `app-7`'s decoder). Not adopted: `settle_s` 15, 20, 25, 30 s (no gain; from 20 s WA0QZK goes); `settle_s` 1e6 s; `points_bare` 2.5; `points_framed` 1 and 1.5; `far_variant_ratio` 1.4 (nothing changes); `variant_hz` 300 Hz (a trade toward precision, kept for the comparison with CW Skimmer).

**The five rules of review 5, adopted on the final decoder** (`calls-rules.csv`, each alone and together, replayed on the final decoder's events; "as `app-7`" is the five off, so the leak floor's own effect is that row against `app-7`'s). Each is a heuristic, chosen looking at the contest and real 40 m, the same overfitting as above; `w12`, the 100 stations, G2 and the short real recordings are the checks, and they moved only up. Nothing further was tuned to real 40 m.

| Rule | What it does | Contest, of 280 | H2, of 80 | 100 stations | 30 min real: rows |
|---|---|---|---|---|---|
| As `app-7`, five off | | 208, 1 | 78, 6 | 99, 0 | 16 |
| `points_named` 1 | A whole or glued word named before DE, 5NN, UR and the like passes at 1 point | 222, 7 | 78, 6 | 99, 0 | 18: + NQ6B, PY2UDTE |
| `joined_points` 0.75 | Letters joined across spaces, 0.75 points, not 0.5 | 208, 1 | 78, 7 | 99, 0 | 17: + KF0YUX |
| `tiny_points` 2 | A call of tiny pieces is read, as whole words, and needs 2 | 209, 1 | 79, 6 | 100, 0 | 16 |
| `prefix_held` 1 | A call that starts another, or starts with it, is held as a near variant | 208, 1 | 78, 1 | 99, 0 | 16 |
| `withdraw_own_place` 1 | A call that stands elsewhere is not withdrawn by a local defeat | 208, 1 | 79, 6 | 99, 0 | 17: + K0S |
| **The five together** | | **221, 7** | **80, 1** | **100, 0** | **20** |
| Rejected: `joined_points` 1 | | 222, 9 | 80, 4 | 100, 0 | 26, with IT6E, K3Q, OJ4X, SE8WST and others |
| Rejected: `points_framed` 1 | Any framed word passes at 1 | 226, 27 | 80, 2 | 100, 0 | 26, with EH1MO, EK1MWA, W1MWA and others |

Review 5 measured the rules on `app-7`'s decoder: contest 216 and 3, H2 80 and 1, real 40 m 19 rows; with its leak variant, contest 221 and 7, real 40 m 20 rows. This run reads the same to the call. Precision on the contest fell from 1.000 to 0.969 for recall from 0.721 to 0.789; on real 40 m 2 false rows came for 3 true stations. By the bar, callsigns against CW Skimmer, recall on real recordings counts at least as much, so these stay.

**The contest's losses.** Of the 78 stations lost at `app-7`, all callers (review 5, `w7`'s truth against the text): 45 were never decoded, every one overlapped by a keying station, 32 within 47 Hz over at least 30% of their bursts and 13 at 47 to 150 Hz; 33 were read: 28 once, 1 at 1.5 points, 4 refused before the evidence. So overlap is the largest loss, not the rule of two readings. The generator's callers key at random times over the runner and each other, where a real caller waits his turn, so this overlap is likely overstated (conjecture). Now 59 are lost.

**CPU.** The stage, over whole decodes on the desktop: contest 0.0052, `w12` 0.0040, 30 min real 0.0034, 62 s real 0.0022 ms per channel-second, against 8 ms the goal and 40 ms required: nothing to see. In the app, with the list, 4 threads, real display: 30 min real at `--speed max` 12.96 ms per channel-second in the pipeline, 14.09 in the whole process; at `--speed 1` 13.35 and 21.42 (`calls-cpu.csv`).

## CPU against the bar

At most 40 ms per channel-second required, about 8 ms the goal, stated on the desktop. The whole process, on the desktop unless named.

| Run | `app-4` or checkpoint 1 | `app-5` | `app-7` | Final |
|---|---|---|---|---|
| Whole test set, the Linux machine | 5.61 | 9.52 | 6.87 | 6.89 (groups 4.24 for H2 to 9.18 for F2; E2 6.93 to 7.43) |
| Whole test set, the desktop (derived) | | about 10.8 | about 7.3 to 7.8 | about 7.4 to 7.8 |
| Contest band | 6.05 | 6.94 | 6.61 | 5.86 |
| Crowded band | 4.44 | 6.57 | 5.10 | 5.12 |
| 30 min real | 10.88 | 13.44 | 13.26 | 12.62 |
| 62 s real | 12.50 | 17.45 | 14.97 | 14.21 |
| First 300 s of the real 30 min, as fast as it goes, 4 threads | | 14.73 | 13.82 | 13.40 |
| The same on the stand-in radio, real time | | 21.34 | 16.91 | 17.62 |
| 30 min real, real time, the real display | | | 19.52 (review 5, offscreen) | 21.42 |

The desktop's figure is derived: the Linux machine's 6.89 times the ratio on the same recordings on one thread, 1.06 to 1.16 (mean 1.13) on the review's three with `app-5`, 1.04 to 1.08 (mean 1.07) on three of the test set's with the final code. The leak floor's own cost: +0.3% on the whole set (6.87 to 6.89 ms, with nothing else on the machine), +1.3% on review 5's slice; E2, where it acts, +7%.

**What real time costs.** The previous version of this record said real time costs about 1.22 times batch, and conjectured that a pipeline sleeping between blocks wakes to cold caches. Review 5 refuted that, by measurement: the pipeline's own CPU does not change with pace (first 300 s, 14.38 ms as fast as it goes, 14.31 ms in real time), and the excess is 50 to 90 ms of CPU per second of wall outside the pipeline, the display and the radio's thread, whatever the pace; 98 ms on the real display at real time here (final: 13.35 ms in the pipeline, 21.42 ms in the whole process, 30 min, `calls-cpu.csv`). So on the air the cost is about 12.1 ms per channel-second + (25 + 53 to 98) ms per s of stream / the number of channels (derived): about 18 to 22 ms at the 12 channels this quiet band has, about 13 ms at 100. Inside the 40 ms required, over the goal; the per-track 12.1 ms is what limits capacity, and "about 20 ms" says as much about a quiet band as about the decoder.

The app's own split, CPU = per track + fixed, in the pipeline only (the threads' clock):

| Run | `app-4` | `app-5` | `app-7` | Final |
|---|---|---|---|---|
| 30 min real, first 8 min at up to 10 times real time | 9.77 ms per channel-second + 21.0 ms per s of stream | 12.98 + 21.8 | 12.16 + 21.5 | 11.53 + 21.3 |
| 30 min real, first 300 s, stand-in radio at real time | | 14.47 + 27.13 | 12.41 + 25.07 | 12.12 + 25.32 |
| 100-station band, real time | 4.78 + 25.3 | 7.03 + 26.3 | 5.42 + 25.9 | 5.36 + 25.8 |

Capacity, derived from these: (4 000 ms per s of stream - fixed) / per track on 4 threads at 192 kHz, about 330 real tracks and about 740 clean ones. So the goal's 500 tracks on half the desktop's cores holds on clean signals and not on real 40 m.

**Where the real 40 m figure goes** (review 5, measured at `app-7`, the bench's per-track CPU on the 30 min, each track judged from its text): 1 034 of 1 178 tracks are junk, 63.8% of decoder CPU; at real stations' places, 36.2%, 11.56 ms per channel-second alone. So the goal cannot be reached by removing junk: real signals by themselves stand over it. A faster judge (`kUnprovenS` 8 s, `kQuietNewS` 5 s) saves 21.5% of decoder CPU and 29.6% of channel-seconds, but loses E51MWA, a real DX station, and raises I2 by half on the detector's path. Recall comes first: not taken (`docs/decision-log.md`), first on the backlog's CPU items. Removing junk also raises "ms per channel-second" (12.31 to 14.20 ms in that run), so such a gain must be stated per second of stream.

## Findings

1. **Measured.** The new fist fixes Farnsworth: to factors above 8, where the bank reads 0.675. Seed 2 agrees (0.020). What the gain is, at ARRL's spacing: checkpoint 1 already read I2's letters at 0.033 without spaces, and 42 959 of its 45 049 edits were word spaces; now 0.0027 without spaces and 1 648 space edits of 1 823 (`i2-kinds.csv`). The fourth review found, on its own recordings at 902121c, that another spacing keeps the letters and loses only word spaces, 0.05 to 0.21 (an app measurement).
2. **Measured.** Two filter widths and the leak floor take E2 from 0.691 to 0.504 and seed 2's from 0.784 to 0.673 on the oracle path, and on the detector's path from 0.734 to 0.675, late 0.658: the table above; the filters alone gave 0.575, 0.694 and 0.688. Not fixed: a neighbor 30 Hz off at 0 dB re wanted (0.976; the bank's 0.363), a neighbor +10 dB re wanted at 80 Hz and inside, and +20 dB re wanted at 100 and 150 Hz (1.000; the bank's 3.537 and worse).
3. **Measured.** What it cost in text is small: C2 0.063 against 0.061 (seed 2: 0.062 against 0.061), hand keying 0.156 against 0.148, H2 on seed 2 0.047 against 0.045; in the app `W1AAA` 0.30 against 0.27 and the clean band 0.0018 against 0.0011.
4. **Measured.** The replay carries over: on the detector's path first-word CER is 0.284 against 0.681, A2 0.250 against 0.274. Oracle signals do not have it.
5. **Measured. Correcting the record's earlier conclusion.** The record said that 400 clean stations at 768 kHz run slower than real time and that a live radio's budget would shed there. They do not. The 30.4 s of wall was about 16 s of building the engine and about 15 s for the 25 s of stream: the tables scaled as filter scale times rate squared, on the pipeline's thread, with the radio already running, and at that rate a radio start lost its first 16 s and a center change froze the window as long. With the tables on 48 kHz: 14.9 s of wall for 25 s of stream, 1.7 times real time on 4 threads, longest read 0.21 s, no samples lost, no budget, all 400 rows, and two center changes in the first 5 s answer at once.
6. **Measured. The carrier "regression" is not one.** `fast-3` reported that a carrier +10 dB re wanted or stronger now reads 0.98 where it read 0.72. The 0.72 is the first review's run on 5aeb081, before the channel floor; `app-4`, just before `fast-3`, already reads 0.975 to 0.984 in those cells, and the 0.72 was a row of false T's, some matching by chance. In the app, with text on real 40 m (steady carriers on for 180 s, 3 alone from +25 to +55 dB S500, 8 beside a station at +25 dB S500), the 26 rows at every carrier and every station are identical before and after `fast-3` (at b4323ab and 902121c): a carrier alone writes 5 T's in hidden rows and lists nothing.
7. **Measured, on the real recordings, at b4323ab and 902121c.** A carrier here is a bin steady over 1 s, at least 10 dB above the local median for at least 5 s running (a heuristic for this question). The 30 min recording has 374 such runs at 89 places. Within 150 Hz of them, before and after `fast-3`: 102 rows and 1 082 symbols, 859 listed, 99 of the 102 rows identical; no call read twice in either. In the 62 s recording: 11 rows and 34 symbols, 10 identical. So on real recordings it does not matter.
8. **Measured, and old.** What does matter is on the detector's path: a station within 47 Hz of a carrier that came first is not read with the carrier at its own level or above, nor at 100 Hz from one +10 dB re wanted or stronger: at 40 Hz, one station each, CER 0.95 at `app-5` and 0.19 at `app-7` at -10 dB re wanted, 1.00 from 0 dB re wanted up; at 100 Hz 0.07 at -10 dB re wanted, 0.02 at 0 dB re wanted, 1.00 at +10 and +30 dB re wanted (an app measurement at e913886). Review 5's sweep in 2 Hz steps at 0 dB re wanted puts the limit at about 66 Hz, not 47 Hz. Conjectured: the track sits on the carrier, as the row's frequency shows, and the judge retires it.
9. **Conjectured; no truth.** (The older count; for the callsign stage's list, see its section.) Calls on the 30 min recording went 22, 31, then 27 with the channel's clock. Of the 12 gained at the merge, about 9 to 10 are not new stations (`IK0HBT`, `K0HBT`, `EK0HBN`, `IK0HT`, `IK6M`, `K6QO`, `N5JJK`, `X9F`, `S5ME`, read beside `IK0HBN`, `IK6QOP`, `N5JJ`, `KX9F`, `S58W`), and 2 to 3 are: `S58W` is real and exact, `KX9FK` is `KX9F` and K glued, `D5NH` is probably a real station with its first letter unread. Three were lost, one real: `IK6QOP`, read exactly once at `app-5`. With the channel's clock six go (`D5NH`, `IK6M`, `K6QO`, `KX9FK`, `TK0HBN`, `X9F`) and two come (`B5NHS`, `T6Q`): mostly misreadings and fragments, with `KX9F` still there with spaces removed. The fourth review counts about 12 to 14 stations behind the names. With spaces removed the real calls were already there at `app-4`, so the callsign stage should match with spaces removed. The comparison with CW Skimmer is what can say.
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

**The leak floor** (+3 dB re the leak, heuristic chosen by measurement; 60 Hz, 320 Hz and 1 s heuristic; the responses and 1.5 derived; `docs/signal-processing.md`, 8e and A.8e). Gained: E2 0.575 to 0.504, seed 2 0.694 to 0.673, detector's path 0.688 to 0.675; the strong pairs above; the 30 pairs 0.353 to 0.287 early, 0.283 late; contest true 202 to 208 by itself. Lost: at a 55 Hz carrier, in oracle, 0.000 to 0.979; a neighbor +20 dB re wanted at 100 Hz, 0.935 to 1.000, and a carrier +10 dB re wanted at 100 Hz, 0.975 to 1.000; B2 0.2423 to 0.2424 and seed 2 0.2425 to 0.2426; by signal on seed 2, 36 better and 70 worse, mostly by an edit or two; real stations' symbols on real 40 m -4.7%. E2 at 60 to 120 Hz with a neighbor 6 dB re wanted or stronger, 0.179 to 0.283.

**Not taken.** The faster judge: "CPU against the bar" above. The real list as a default: there is no file to ship with the program; `--known-calls` takes one.

## Limits

The test set's noise is white; a narrower filter on the detector's path pays for a track up to half a bin off, which the oracle rows do not show. S - F, the choice of stream, is a heuristic between filters whose data differ, and a long track whose conditions change keeps its early leader. Memory about 3 MB a track. The track list is still about 35% to 63% junk on real 40 m; the call list beside it is what to read. **A neighbor's level matters absolutely, not only re wanted:** the table above; 40 Hz at or below the wanted station's level is where the bank stays better, and the test set has no group for it (backlog). A call with no digit after a letter-digit prefix (D4C, E7DX, C6AGU, P3F, about 90 calls of MASTER.SCP) is never read, with a list or without. The list never ages out, where CW Skimmer's deletes a call after 10 min of inactivity. On the air the largest single CPU item is the widest stream's search on weak and wide channels (55% of the decoder's time on real 40 m, the fourth review's counters at 902121c). The app's measurements are single runs on the desktop, the tools and generators for them are scratch, not committed, and the recordings are not committed; the app's numbers are totals in `app-measures.csv`, not per signal, and that part waits for checkpoint 3. See `docs/signal-processing.md`, appendices A.8e, A.9b and A.12.

## What comes next

Merge to `main`, this checkpoint to the owner, his first run on the radio. His replay of `real30.wav` through CW Skimmer, compared with `python -m kz4ap_proto.calls_score compare`, whose reader now keeps every spot: 30 min of his time, and every estimate of the bar above is a stand-in for it. Then the busy-band hour for checkpoint 3, the bar's other half. Then the CPU: junk's 64% on real 40 m and the widest stream's search on weak channels.

## Reproducing

Recordings: `python -m kz4ap_synth.suites` from the seeds. Run: `--decoder segmental` at 8b80328, recordings as named in the table's `test_case` column. Tables: `kz4ap_proto.devset2.load_rows(out, [name], only=None)` and `write_scores`. Each group above is the sum of `edits` over the sum of `symbols`. `level-pairs.csv`'s recordings come from a scratch generator of review 5, not committed, built on `kz4ap_synth`: without the leak floor is `--set leak_hold_s=0`.

The callsign stage: `kz4ap-calls RECORDING --out CALLS.csv` at 8b80328, and at 4bf5446 and a277ed0 for the earlier columns, `--replay` with `--set NAME=VALUE` for the settings (`calls-rules.csv`: the five off are `points_named=1e9`, `joined_points=0.5`, `tiny_points=0`, `prefix_held=0`, `withdraw_own_place=0`), `python -m kz4ap_proto.calls_score truth CALLS.csv LABELS.json`. G2 and H2 are `G2-*` and `H2-*` of `DEV2`, seeds 1 and 2; the band recordings are the first review's generators', not committed, and so are the real ones. The full table of what was dropped, with the text around each reading, is the session's scratch `app7\real30_dropped.csv`, regenerated from a run of the review's harness and a `kz4ap-calls --log`.
