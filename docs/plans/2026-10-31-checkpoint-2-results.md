# Checkpoint 2: first working app (2026-10-31)

**Answer.** The app works on the merged decoder, and the merged decoder wins every group of the test set against the bank,
Farnsworth included: I2 reads CER 0.021 against checkpoint 1's 0.523 and the bank's 0.261. E2 interference reads 0.562
against 0.691 and 0.776. The price is CPU: 9.5 ms per channel-second on the whole test set against 5.6 ms at checkpoint 1,
and 13.4 ms against 10.9 ms on 30 min of real 40 m: under the 40 ms required, over the 8 ms goal. CER is edits over
reference symbols, spaces counted, pooled over the scored signals. No decoder has yet been scored against truth on a real
signal; that is checkpoint 3's.

## Runs

The whole set is seed-1 of the development set `DEV2`, on the Linux machine, 8 jobs on 10 cores: 498 recordings on the oracle
path (the decoder is told the frequency and nothing else) and 210 on the detector's path.

| Label | Decoder and commit | Recordings | Channel-seconds | CPU, ms per channel-second | Table in `docs/plans/data/` |
|---|---|---|---|---|---|
| merged, whole | `segmental`, 902121c (`app-5`) | 708 | 1 391 335 | 9.5 (decoder 9.1) | `2026-10-31-checkpoint-2/segmental-app5-whole.csv`; CPU per recording, `.../cpu-app5-whole.csv` |
| segmental, whole | `segmental`, d78d45f (checkpoint 1) | 708 | 1 395 785 | 5.6 | `2026-10-31-checkpoint-1/segmental-fast2-whole.csv` |
| bank, whole | `bank`, task D5's replay | 498 | not recorded here | not recorded here | `2026-10-06-development-set/d5-bank.csv` |
| fast-3, seed 2 | `segmental`, `fast-3` (built at 3f8a9ff; 582f5d7 adds only the log) | 247 | 560 412 | 8.3 | `2026-10-31-checkpoint-2/segmental-fast3-seed2.csv` |
| before fast-3, seed 2 | `segmental`, `fast-3`'s base | 247 | 560 412 | 5.0 | `2026-10-31-checkpoint-2/segmental-fast2-seed2.csv` |
| stress | see "Stress" below | 2 | | | `2026-10-31-checkpoint-2/stress.csv` |

`.../conditions-b2-c2.csv` of checkpoint 1 gives each B2 and C2 signal's Doppler spread and keying. Every CER below is computed
from these rounded tables. Tests at 902121c (later commits on `app-5` change documents and data only): `ctest`, Windows with Qt 753 tests, 734 passed, 19 skipped as
designed, 0 failed; Windows without Qt the same 753; Linux 741, 722 passed, 19 skipped, 0 failed.

## Scores by group

| Group | Merged, whole | Segmental, checkpoint 1 | Bank, whole |
|---|---|---|---|
| A2 sensitivity | 0.226 | 0.241 | 0.287 |
| A2, detector's path | 0.250 | 0.274 | |
| B2 fading | 0.241 | 0.251 | 0.557 |
| C2 fists | 0.063 | 0.061 | 0.157 |
| D2 speed changes | 0.000 | 0.000 | 0.027 |
| E2 interference | 0.562 | 0.691 | 0.776 |
| F2 tuning | 0.085 | 0.087 | 0.133 |
| G2 QSO, same track | 0.055 | 0.055 | 0.206 |
| H2 QSO, separate tracks | 0.057 | 0.058 | 0.198 |
| I2 Farnsworth | 0.021 | 0.523 | 0.261 |
| S2 stretch | 0.237 | 0.264 | 0.337 |

E2 counts only the wanted stations (90 signals); G2 has 20, H2 40. By signal, merged against checkpoint 1: 7 655 better, 12 154
the same, 1 025 worse. On the detector's path: 5 607 of 6 720 signals detected in both; first-word CER 0.284 against 0.681.

## Whole set by condition

| I2, Farnsworth factor (character speed over overall speed) | Merged | Checkpoint 1 | Bank |
|---|---|---|---|
| 1 to 2 | 0.054 | 0.349 | 0.039 |
| 2 to 4 | 0.028 | 0.504 | 0.053 |
| 4 to 8 | 0.016 | 0.525 | 0.228 |
| 8 and above | 0.017 | 0.583 | 0.675 |

| A2, S500 (dB SNR in 500 Hz) | Merged | Checkpoint 1 | Bank |
|---|---|---|---|
| -10 to -5 | 0.970 | 0.971 | 0.998 |
| -5 to 0 | 0.519 | 0.601 | 0.739 |
| 0 to +5 | 0.156 | 0.156 | 0.201 |
| +5 to +10 | 0.007 | 0.007 | 0.030 |
| +10 to +20 | 0.000 | 0.000 | 0.018 to 0.019 |

| B2, S500 at least +10 dB, Doppler spread | Merged | Checkpoint 1 | Bank |
|---|---|---|---|
| 0.05 to 0.2 Hz | 0.088 | 0.089 | 0.232 |
| 0.2 to 0.6 Hz | 0.103 | 0.103 | 0.389 |
| 0.6 to 2 Hz | 0.103 | 0.102 | 0.591 |
| 2 to 5 Hz | 0.125 | 0.128 | 0.753 |

| C2, S500 at least +10 dB, keying | Merged | Checkpoint 1 | Bank |
|---|---|---|---|
| machine, computer | 0.000 | 0.000 | 0.019 |
| paddle | 0.022 | 0.022 | 0.048 |
| bug | 0.031 | 0.031 | 0.118 |
| hand | 0.156 | 0.148 | 0.436 |

## Seed 2, not tuned on

Run on `fast-3`, not on the merged head; on seed 1 the two read alike on these oracle groups, the merge adding the replay only to the detector's path. Seven groups, 247 recordings.

| Group | Before `fast-3` | `fast-3` |
|---|---|---|
| B2 fading | 0.251 | 0.242 |
| C2 fists | 0.061 | 0.062 |
| D2 speed changes | 0.000 | 0.000 |
| E2 interference | 0.784 | 0.677 |
| G2 QSO, same track | 0.040 | 0.040 |
| H2 QSO, separate tracks | 0.045 | 0.047 |
| I2 Farnsworth | 0.523 | 0.020 |

(The decision log's line says 0.524 for I2 before; the table reads 45 114 edits over 86 226 symbols, 0.5232.)

## Stress: a neighbor or a carrier inside the channel

The first independent review's recordings: a wanted station at +15 dB S500, 20 to 30 WPM, oracle path, about 120 symbols a cell.
Offsets from the wanted station; levels in dB relative to its key-down power ("dB re wanted"). The four decoder columns are:
the review's own run on commit 5aeb081, before the channel floor; `app-4` (b4323ab), the decoder just before `fast-3`; `fast-3`
at 582f5d7; the bank. The merged head, run again here, reads the same as `fast-3` in every cell.

| Cell | 5aeb081 | `app-4` | `fast-3` | Bank |
|---|---|---|---|---|
| Neighbor 30 Hz, -10 dB re wanted | 0.935 | 0.870 | 0.000 | 0.000 |
| Neighbor 60 Hz, 0 dB re wanted | 0.900 | 0.908 | 0.083 | 0.317 |
| Neighbor 30 Hz, 0 dB re wanted | 1.290 | 0.976 | 0.976 | 0.363 |
| Neighbor 100 Hz, 0 dB re wanted | 0.463 | 0.057 | 0.057 | 0.041 |
| Neighbor 100 Hz, +10 dB re wanted | 0.831 | 0.847 | 0.686 | 1.110 |
| Neighbor 150 Hz, +10 dB re wanted | 0.860 | 0.413 | 0.413 | 0.364 |
| Carrier 40 Hz, -10 dB re wanted | 0.829 | 0.932 | 0.000 | 0.051 |
| Carrier 40 Hz, 0 dB re wanted | 0.727 | 0.983 | 0.000 | 1.000 |
| Carrier 40 Hz, +10 dB re wanted | 0.727 | 0.983 | 0.983 | 1.000 |
| Carrier 40 Hz, +30 dB re wanted | 0.758 | 0.984 | 0.984 | 1.000 |
| Carrier 100 Hz, 0 dB re wanted | 0.729 | 0.042 | 0.042 | 0.280 |
| Carrier 100 Hz, +10 dB re wanted | 0.719 | 0.975 | 0.975 | 2.041 |

## The app

Measured with the engine and the app on the desktop, 3 worker threads for files and 4 for the app. `app-3` is 2749b04, `app-4` b4323ab. The recordings:

- *Contest band:* 300 s generated, 110 runners and 170 callers, 280 calls. *Crowded band:* 60 s generated, 300 stations.
- *`W1AAA`:* a generated station at +15 dB S500 added to a slice of real 40 m beside a wide data signal, which used to close its place.
- *Clean band:* 30 generated stations at 192 kHz.
- *30 min real* and *62 s real:* real 40 m at 192 kHz, no truth; a call counts here when its shape fits and it is read at least twice
  or after DE (a heuristic, not a score).
- *3 s stall:* a stand-in radio delivers a 100-station band and stops for 3 s, 20 s in.

| Measure | `app-3` | `app-4` | Merged |
|---|---|---|---|
| Contest band: callers' calls, of 170 | 96 | 126 | 127 |
| Contest band: all calls, of 280 | 206 | 236 | 237 |
| Crowded band: calls, of 300 | 215 | 300 | 300 |
| Crowded band: CER | 0.351 | 0.015 | 0.015 |
| `W1AAA`: CER | 0.99 | 0.27 | 0.30 |
| Clean band: CER | 0.0166 | 0.0011 | 0.0013 |
| Clean band: first words' CER | 0.65 | 0.00 | 0.00 |
| 30 min real: rows listed, hidden | 166, 743 | 202, 715 | 204, 712 |
| 30 min real: channel-seconds | 19 458 | 23 062 | 23 115 |
| 30 min real: calls | 17 | 22 | 31 |
| 62 s real: rows listed, hidden | 34, 153 | 41, 163 | 43, 164 |
| 62 s real: channel-seconds | 2 632 | 2 959 | 3 060 |
| 62 s real: calls | 4 | 6 | 6 |
| 3 s stall: tracks opened, budget left | 143, 100 | 100, none | 100, none |

These figures are in `2026-10-31-checkpoint-2/app-measures.csv`. The real radio was not used for this record. Its figures stand as
measured on `app-4`, before the merge: 62 s at 192 kHz, 17.3 ms per channel-second, at most 21 tracks live; 66 s at 768 kHz, 14.0 ms per
channel-second, at most 84 tracks live; no samples lost and no budget in either.

## CPU against the bar

At most 40 ms per channel-second required, about 8 ms the goal. The whole process, on the desktop unless named.

| Run | `app-4` or checkpoint 1 | Merged |
|---|---|---|
| Whole test set, the Linux machine | 5.6 | 9.5 (groups 5.5 for H2 to 12.4 for F2) |
| Contest band | 6.05 | 6.94 |
| Crowded band | 4.44 | 6.57 |
| 30 min real | 10.88 | 13.44 |
| 62 s real | 12.50 | 17.45 |

The app's own split, CPU = per track + fixed, in the pipeline only (the threads' clock):

| Run | `app-4` | Merged |
|---|---|---|
| 30 min real, first 8 to 9 min at up to 10 times real time | 9.77 ms per channel-second + 21.0 ms per s of stream | 12.98 + 21.8 |
| 100-station band, real time | 4.78 + 25.3 | 7.03 + 26.3 |

## Findings

1. **Measured.** The new fist fixes Farnsworth: letters and word spaces now read right to factors above 8, where the bank reads 0.675.
   Seed 2 agrees (0.020).
2. **Measured.** The three filter widths take E2 from 0.691 to 0.562 and seed 2's from 0.784 to 0.677, and the review's neighbor cells at
   30 Hz, -10 dB re wanted and at 60 Hz, 0 dB re wanted from 0.870 and 0.908 to 0.000 and 0.083. Not fixed: 30 Hz at 0 dB re wanted (0.976; the
   bank's 0.363), and neighbors +10 dB re wanted and stronger within 150 Hz.
3. **Measured.** What it cost in text is small: C2 0.063 against 0.061 (seed 2: 0.062 against 0.061), hand keying 0.156 against 0.148, H2 on
   seed 2 0.047 against 0.045; in the app `W1AAA` 0.30 against 0.27 and the clean band 0.0013 against 0.0011.
4. **Measured.** The replay carries over into the merged decoder: on the detector's path first-word CER is 0.284 against 0.681, A2 0.250 against
   0.274. Oracle signals do not have it.
5. **Measured.** CPU rose by 1.1 to 1.7 times, on the whole set 5.6 to 9.5 ms and on real recordings 10.9 to 13.4 and 12.5 to 17.5 ms. It stays under
   the 40 ms required, and meets the 8 ms goal only on the contest and crowded bands. What it costs in capacity: 400 clean stations at 768 kHz
   on 4 threads now take 30.4 s of wall for 25 s of stream, against 14.4 s, using 1.9 of the 4 threads; the longest read 0.31 s against 0.10 s. So that
   band runs slower than real time, and a live radio's track budget would shed there (conjectured; a file has no budget). Real 40 m at 768 kHz had at most 84 live (on `app-4`).
6. **Measured. The carrier "regression" is not one.** `fast-3` reported that a carrier +10 dB re wanted or stronger now reads 0.98 where it read 0.72. The 0.72 is the
   first review's run on 5aeb081, before the channel floor; `app-4`, just before `fast-3`, already reads 0.975 to 0.984 in those cells. The 0.72 was not better
   reading (its text, in the review's run): it is a row of false T's, about 21 symbols with their spaces, some matching by chance, where the decoder now writes one T. `fast-3` changes the cells below that level,
   from 0.932 and 0.983 to 0.000 at 40 Hz. Several cells the decision log credits to `fast-3` were the channel floor's, reached by `app-4` already (100 Hz at 0 dB re wanted; 150 Hz at +10 dB re wanted). In the app, with text on real 40 m (steady carriers on for 180 s, 3 alone from +25 to +55 dB S500, 8 beside a
   station at +25 dB S500), the 26 rows at every carrier and every station are identical before and after `fast-3`: a carrier alone writes 5 T's in hidden rows and lists nothing.
7. **Measured, on the real recordings.** A carrier here is a bin steady over 1 s, at least 10 dB above the local median for at least 5 s running (a heuristic for this question). The 30 min
   recording has 374 such runs at 89 places. Within 150 Hz of them, before and after `fast-3`: 102 rows and 1 082 symbols, 859 listed, 99 of the 102 rows identical; no call read twice in either, so no neighbor lost that was found before. In the 62 s recording: 11 rows and 34 symbols, 10 identical. So on real recordings it does not matter.
8. **Measured, and old.** What does matter is on the detector's path, unchanged by `fast-3`: a station within 47 Hz of a carrier that came first is not read even with the carrier 10 dB below it, or at 100 Hz from one +10 dB re wanted or stronger (at 40 Hz CER 0.95 at -10 dB re wanted, 1.00 from 0 dB up; at 100 Hz 0.07 at -10 dB, 0.03 at 0 dB, 1.00 at +10 and +30 dB). Conjectured: the track sits on the carrier, as the row's frequency shows, and the judge retires it.
9. **Conjectured.** The jump from 22 to 31 calls on the 30 min recording is not 9 more stations. Of the 12 gained, 6 look like misreadings of one or two stations between +3.2 and +3.6 kHz (`IK0HBT`, `K0HBT`, `EK0HBN`, `IK0HT`, `IK6M`, `K6QO`), and there is no truth on this recording. Checkpoint 3's comparison with CW Skimmer is what can say.
10. **Conjectured.** These scores flatter the text prior, as at checkpoint 1: the on-air table is the one the test text is drawn from.

## Limits

The test set's noise is white; a narrower filter on the detector's path pays for a track up to half a bin off, which the oracle rows do not show. S - F, the choice of stream, is a heuristic between filters whose data differ, and a long track whose conditions change keeps its early leader. Memory about 3 MB a track. The list is still 35% to 63% junk on real 40 m until the callsign stage. The app's measurements are single runs on the desktop, the tools were scratch tools, and the recordings are not committed. See `docs/signal-processing.md`, appendices A.8e and A.9b.

## Reproducing

Recordings: `python -m kz4ap_synth.suites` from the seeds. Run: `--decoder segmental` at 902121c, recordings as named in the table's `test_case` column. Tables: `kz4ap_proto.devset2.load_rows(out, [name], only=None)` and `write_scores`. Each group above is the sum of `edits` over the sum of `symbols`.
