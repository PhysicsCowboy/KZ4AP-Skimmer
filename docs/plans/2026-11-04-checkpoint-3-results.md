# Checkpoint 3: real recordings against CW Skimmer (2026-11-04)

**Answer.** On real recordings our skimmer now confirms about as many callsigns as CW Skimmer 2.1, so the stop rule
does not fire. Over four public 2017 recordings, one judge for both programs, it lists 205 calls the Reverse Beacon
Network confirms against CW Skimmer's 206 with no known-call list, and 219 against 206 with the 2018 list CW Skimmer
itself ran with; a stricter judge gives the same picture (190 and 201 against 193). Not on every recording: on W3OA we
confirm 47 against 57. Without a list we also report more calls the network does not confirm, 305 against 200, so
precision is worse; by how much is not measured, because the judge's "misreading" class also holds real stations. On
the owner's 40 m recording of 2026-11-04 (367.5 s): 42 calls, 21 confirmed, 2 judged misreadings, 8 unknown; CW
Skimmer 33, 20, 3, 4. **CPU, against the bar of at most 40 ms per channel-second required and about 8 ms the goal:**
5.4 to 6.3 ms per channel-second, under the goal. Per second of stream ours is 0.28 to 0.38 CPU-seconds against CW
Skimmer's 0.35 to 0.38, but its figure includes its display and ours none, on quiet recordings with 43 to 62 channels
open on average. Scores and CPU are measured unless marked.

**Terms.** *RBN*: the Reverse Beacon Network, public spots from CW Skimmers around the world. *The judge* (a
heuristic) sorts each reported base call (/P and prefixes dropped): *confirmed*, spotted on the RBN inside the
recording's UTC window and 192 kHz span; else *RBN that day*, spotted that day elsewhere; else *misreading*, one edit (a
letter changed, added or dropped) from a call spotted in the window or that day; else *unknown*. The misreading class
is a proxy with systematic error in both directions, not a count of errors. A real station the RBN did not spot that
day lands in it about two times in three: of the 323 distinct confirmed-class calls of the four 2017 recordings in
`calls.csv`, 221 have a call one edit away among the 5 915 spotted that day. 20 of CW Skimmer's 53 are among our 96,
and KJ9X, N0YL and W3RL are read by both programs on two recordings each; KJ9X appears in at least six QSOs with
different stations in our W3OA text. 50 of CW Skimmer's 53 are in
its own list, 33 of our 96 (review 11). A garble equal to a spotted call escapes the class. *Known-call list*: a file
of calls a skimmer trusts more; *MASTER.DTA*, CW Skimmer's own (2018); *MASTER.SCP*, the contest community's Super
Check Partial file (2026-10-30). *Channel-second*: one channel open for one second of stream. *Tuning* recordings: the
two the callsign stage's new rules were chosen on; *held out*: the two not tuned on.

## Scores on real recordings

The owner started a scripted replay through CW Skimmer 2.1 on the desktop on 2026-11-04 (each recording played once
in real time, in a fresh CW Skimmer with its own settings: validation "Normal", 500 decoders, adaptive decoders off,
its own MASTER.DTA). Its copy of the owner's file was made before the resampler's stopband fix, the int-1 column's
input; it has no spots beyond +86.0 kHz nor at the negative band edge, where a station folded by the old filter
would appear (review 11).
*Ours*: `kz4ap-calls` at d4068e1 (6f89eb5 but for a comment), each recording's logged decoder events replayed through
the callsign stage with each list, so the decoder's text is the same in every column but the owner's int-1. *int-1*:
the state before the stage's list rules (`call-2`), the build of f21b5c3. Each cell: calls, confirmed, misreadings,
unknown (the rest are RBN that day).

| Recording (length) | CW Skimmer | Ours, no list | Ours, MASTER.DTA 2018 | Ours, MASTER.SCP 2026 | int-1, no list |
|---|---|---|---|---|---|
| Owner's 40 m (367.5 s) | 33, 20, 3, 4 | 42, 21, 2, 8 | 41, 22, 2, 7 | 42, 22, 2, 7 | 42, 22, 2, 7 |
| W3OA 40 m, tuning (900.6 s) | 101, 57, 15, 6 | 107, 47, 23, 11 | 94, 48, 10, 10 | 89, 47, 11, 8 | 109, 47, 25, 11 |
| KC2LRC-1 20 m, tuning (899.4 s) | 95, 45, 8, 15 | 144, 50, 22, 28 | 129, 54, 16, 21 | 119, 52, 16, 19 | 146, 50, 24, 28 |
| KC2LRC-2 20 m, held out (899.0 s) | 94, 49, 12, 11 | 135, 50, 23, 24 | 110, 52, 9, 15 | 99, 52, 9, 11 | 138, 50, 24, 25 |
| N2GZ 40 m, held out (1 863.2 s) | 116, 55, 18, 14 | 124, 58, 28, 16 | 113, 65, 16, 12 | 107, 63, 16, 9 | 124, 58, 28, 16 |
| **The four 2017** | **406, 206, 53, 46** | **510, 205, 96, 79** | **446, 219, 51, 58** | **414, 214, 52, 47** | **517, 205, 101, 80** |

The owner's int-1 column, decoded before the stopband fix: one confirmed call more (N8BB, since read once, not twice)
and one unknown fewer (RN5NN, a misreading of "R 5NN").

**Generated recordings, no list** (`kz4ap-calls` at d4068e1, the Linux machine; `generated-calls.csv`), true and false
calls (stations), by base call wherever it was reported: the contest 220 and 6 (280), a fresh seed of it 236 and 3
(280); strong pairs 40 to 150 Hz apart 40 and 2 (48), a fresh seed 42 and 0 (48); the crowded band 300 and 0 (300);
100 stations at 192 kHz 100 and 0 (100); G2, QSOs on one track, 40 and 0 (40); H2, QSOs on two tracks, 40 and 1 (40).
All as at int-1 (decision log, 2026-11-04).

## CPU

| Recording | Channel-seconds per s of stream, int-1 / `main` | ms per channel-second, int-1 app | CPU-s per s of stream, int-1 app | `main`, `kz4ap-calls` | CW Skimmer |
|---|---|---|---|---|---|
| Owner's 40 m | 66.1 / 62.0 | 6.17 | 0.417 | 0.380 | 0.375 |
| W3OA | 55.7 / 55.7 | 6.01 | 0.342 | 0.336 | 0.363 |
| KC2LRC-1 | 51.4 / 51.4 | 5.51 | 0.289 | | 0.358 |
| KC2LRC-2 | 52.0 / 52.0 | 5.39 | 0.286 | | 0.352 |
| N2GZ | 42.7 / 42.7 | 6.33 | 0.276 | | 0.358 |

The desktop, 8 cores. *int-1 app*: the build of f21b5c3, full speed, 4 threads, no window; its pipeline CPU (every
pipeline thread: spectrum, detector, channelizer, decoders) over channel-seconds, and its process CPU. *`main`*:
`kz4ap-calls` at 6f89eb5, 4 threads, process CPU, no display, one run each, for this record. *CW Skimmer*: the slope of
its process CPU during real-time playback, its display included. On the 2017 recordings `main` decodes as int-1, so
their int-1 figures stand for `main`. On the Linux machine, one thread, `main` reads W3OA at 6.07 ms per channel-second
(decision log, 2026-11-04).

## Findings

1. The bar's count is met on confirmed calls: 205 against 206 without a list, 219 against 206 with CW Skimmer's own.
   Requiring at least 2 RBN skimmers for a confirmation: CW Skimmer 193, ours 190 without a list, 201 with the 2018
   list, 199 with MASTER.SCP; at least 3: 180, 177, 187, 184. By recording we lose W3OA (47 or 48 against 57), tie
   KC2LRC-2 (50 against 49) and win KC2LRC-1 and N2GZ.
2. Without a list we report 510 calls against 406, and 305 the RBN does not confirm against 200: precision is worse in
   direction. The judge's misreadings, 96 against 53 (by recording 1.5 to 2.8 times), do not measure the error ratio
   (Terms); that ours are mostly unlisted strings and CW Skimmer's mostly listed calls suggests the true ratio is no
better (conjecture). With a list the classes
   come out equal (51 against 53 with MASTER.DTA, 52 with MASTER.SCP), which is not shown to be equal errors. On the
   owner's file the judge's count runs the other way, 2 against 3.
3. `call-2`'s list rules were chosen on this class (decision log, 2026-11-04), so their gain in misreadings is evidence
   of the same weak kind. On the held-out two alone, confirmed / misreadings, with MASTER.DTA 117 / 25 against CW
   Skimmer's 104 / 30, without a list 108 / 51; that check is weaker still, since every tuning run printed the held-out
   totals too and the first two rules were chosen with them in sight ("A record to correct").
4. At int-1 the total CPU per second of stream rose over `real-1`'s app: 16% (KC2LRC-2) to 73% (the owner's file, 0.241
   to 0.417), with 23% to 57% more channel-seconds, nearly all short tracks retired as not keyed Morse: the finer
   detector opens more, and a place's first retirement reopens at once (decision log, 2026-11-04). The cost per
   channel-second stayed flat on the 2017 recordings (6.37 to 6.01, 5.70 to 5.51, 5.71 to 5.39, 6.24 to 6.33 ms) and
   rose 10% on the owner's file (5.61 to 6.17 ms; not traced).
5. **Derived, on an unmeasured assumption.** If the cost per channel-second, fixed cost included, held at about 6.3 ms
   with many more channels open, the detector's cap of 400 tracks would cost about 2.5 CPU-seconds per second of stream,
   under half the desktop's 8 cores. These recordings hold 43 to 62 channels; the busy band is the test.

## What changed since checkpoint 2

`fast-5`: the decoder's search and evidence made cheaper (CPU about halved on real recordings), a lower-sidelobe
narrow filter for strong neighbors. `real-1`: recordings read as recorded (16-, 24-, 32-bit integer and 32-bit float
WAV, the `auxi` chunk, `--mirror`), exact resampling (250 to 192 kHz by 96/125) with the anti-alias stopband starting
at 96 kHz (review 8's fix, in `int-1`), and `kz4ap-resample` to give CW Skimmer the same samples. `det-1`: the
detector's own spectrum, 4 times finer (5.86 Hz bins), peaks by prominence and a keying-sideband test, tracks 30 Hz
apart, a 10 s DC blocker in place of the guard at 0 Hz, a first retirement left open. `call-2`: with a list, a call
named once passes only if listed; a one-letter suffix is held by a call two edits away at twice its points;
six-character locators are never calls; a list with NUL bytes or no call-shaped line is refused. Per-symbol margins
in the decoder are a variant, off by default (`rival_margin_nats` 0); when on, no text changes and they cost 5% to 7%
more CPU per channel-second.

**How the decoder works now** (settings are heuristics unless marked derived; `docs/signal-processing.md` gives each
one's provenance). The WAV is read, resampled to 192 kHz if need be, and its DC removed (10 s blocker). The detector
reads its own fine spectrum every 85.3 ms (derived from 5.86 Hz bins) and opens a track at a peak 7 dB re the floor
(per 5.86 Hz bin) held 0.5 s. Each track's channel, ±150 Hz at 1 500 samples/s (derived), feeds the segmental decoder:
two matched filters (12 and 36 ms), a level belief, a search over stories of speed, gap stretch and text. The callsign
stage scores call-shaped words by role and repetition and holds back variants of better-supported calls.
`docs/signal-processing.md` matches the code at 6f89eb5 after this checkpoint's fixes (review 11: every dB named, stale
appendix passages on the dB round trip and the hold's residual, path labels in the parameter table).

## What comes next: the owner's decisions

1. Whether to ship or fetch a known-call list (MASTER.SCP; its licensing is yours to decide): with one our counts
   match CW Skimmer's by the judge; without one we report about 50% more unconfirmed calls (305 against 200), though on
   your own file the judge's misreadings were 2 against its 3.
2. The busy-band hour on the receive-only receiver, when you say it is ready.
3. Approval to push: nothing has been pushed since 686999b.

**Memory notes** (the coordinating session's, this project's folder only, since 2026-10-31):
`feedback-attribute-questions-correctly.md`, my concerns never presented as yours, nothing asserted about your station;
`feedback-cw-skimmer-reaches-k3.md`, starting CW Skimmer here launches the radio's control program, so not without your
say; `feedback-no-radio-use.md`, nothing touches radios, radio software, devices or programs I did not start without
asking; `kz4apskimmer-project-state.md`, the standing state, updated; `MEMORY.md`, the index.

## Data and reproducing

`docs/plans/data/2026-11-04-checkpoint-3/calls.csv`: one row per base call per recording, with the judge's class, the
number of RBN skimmers spotting it in the window, the RBN calls a misreading is one edit from, and whether CW Skimmer
and each of our four runs reported it (rows no one reported are North American skimmers' calls in the window). Every
real-recording count above comes from it. The judge is a scratch script, not in the repository (backlog: commit it
with a test); `calls.csv` carries its output. `cpu.csv`: process CPU, channel-seconds and pipeline ms per
channel-second of `real-1`'s app (a build of 2026-11-04 before `det-1`'s merge, no commit recorded), int-1's app and
`main`, and CW Skimmer's slope. `generated-calls.csv`: one row per station or reported call of each generated recording
(G2 and H2 by recording, seed 1), whether it is a station and whether it was reported. Not committed: recordings,
lists, CW Skimmer's spots, RBN day files. Our calls rerun in minutes with `kz4ap-calls` at 6f89eb5 (`--known` for a
list, `--mirror` for the owner's file).
