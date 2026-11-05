# Checkpoint 3: real recordings against CW Skimmer (2026-11-04)

**Answer.** On real recordings the app now finds about as many callsigns as CW Skimmer 2.1, so the stop rule does not
fire; without a known-call list it still misreads more. Over four public 2017 recordings, one judge for both programs,
it lists 205 confirmed calls against CW Skimmer's 206 with no list, and 219 against 206 with the 2018 list CW Skimmer
itself ran with. Misreadings: 96 against 53 without a list (1.5 to 2.8 times CW Skimmer's, by recording), 51 against
53 with it. On the owner's 40 m recording of 2026-11-04 (367.5 s): 42 calls, 21 confirmed, 2 misread, 8 unknown; CW
Skimmer 33, 20, 3, 4. **CPU, against the bar of at most 40 ms per channel-second required and about 8 ms the goal:**
5.4 to 6.3 ms per channel-second, under the goal. Per second of stream the total is 0.28 to 0.42 CPU-seconds against CW
Skimmer's 0.35 to 0.38. All numbers are measured unless marked.

**Terms.** *RBN*: the Reverse Beacon Network, public spots from CW Skimmers around the world. *The judge* (a
heuristic) sorts each reported base call (/P and prefixes dropped): *confirmed*, spotted on the RBN inside the
recording's UTC window and 192 kHz span; else *RBN that day*, spotted that day elsewhere; else *misreading*, one edit (a
letter changed, added or dropped) from a call spotted in the window or that day; else *unknown*. *Known-call list*: a
file of calls a skimmer trusts more; *MASTER.DTA*, CW Skimmer's own (2018); *MASTER.SCP*, the contest community's Super
Check Partial file (2026-10-30). *Channel-second*: one channel open for one second of stream. *Tuning* recordings: the
two the callsign stage's new rules were chosen on; *held out*: the two kept as the check.

## Scores on real recordings

CW Skimmer 2.1 replayed each recording on the desktop on 2026-11-04: validation "Normal", 500 decoders, adaptive
decoders off, its own MASTER.DTA. Ours is `main` at 6f89eb5, each recording's logged decoder events replayed through
the callsign stage (`kz4ap-calls --replay`), so the decoder's text is the same in every column but the owner's int-1.
*int-1* is the state before the stage's list rules (`call-2`). Each cell: calls, confirmed, misreadings, unknown (the
rest are RBN that day).

| Recording (length) | CW Skimmer | Ours, no list | Ours, MASTER.DTA 2018 | Ours, MASTER.SCP 2026 | int-1, no list |
|---|---|---|---|---|---|
| Owner's 40 m (367.5 s) | 33, 20, 3, 4 | 42, 21, 2, 8 | 41, 22, 2, 7 | 42, 22, 2, 7 | 42, 22, 2, 7 |
| W3OA 40 m, tuning (900.6 s) | 101, 57, 15, 6 | 107, 47, 23, 11 | 94, 48, 10, 10 | 89, 47, 11, 8 | 109, 47, 25, 11 |
| KC2LRC-1 20 m, tuning (899.4 s) | 95, 45, 8, 15 | 144, 50, 22, 28 | 129, 54, 16, 21 | 119, 52, 16, 19 | 146, 50, 24, 28 |
| KC2LRC-2 20 m, held out (899.0 s) | 94, 49, 12, 11 | 135, 50, 23, 24 | 110, 52, 9, 15 | 99, 52, 9, 11 | 138, 50, 24, 25 |
| N2GZ 40 m, held out (1 863.2 s) | 116, 55, 18, 14 | 124, 58, 28, 16 | 113, 65, 16, 12 | 107, 63, 16, 9 | 124, 58, 28, 16 |
| **The four 2017** | **406, 206, 53, 46** | **510, 205, 96, 79** | **446, 219, 51, 58** | **414, 214, 52, 47** | **517, 205, 101, 80** |

The owner's int-1 column was decoded before the resampler's stopband moved to 96 kHz (below): one confirmed call more
(N8BB, since read once, not twice) and one unknown fewer (RN5NN, a misreading of "R 5NN").

**Generated recordings, no list** (`kz4ap-calls` at `call-2`'s defaults, the Linux machine; decision log, 2026-11-04),
true and false calls (stations): the contest 220 and 6 (280), a fresh seed of it 236 and 3 (280); strong pairs 40 to
150 Hz apart 40 and 2 (48), a fresh seed 42 and 0 (48); the crowded band 300 and 0 (300); 100 stations at 192 kHz 100
and 0 (100); G2, QSOs on one track, 40 and 0 (40); H2, QSOs on two tracks, 40 and 1 (40). All as at int-1.

## CPU

| Recording | Channel-seconds per s of stream | ms per channel-second, int-1 app | CPU-s per s of stream, int-1 app | `main`, `kz4ap-calls` | CW Skimmer |
|---|---|---|---|---|---|
| Owner's 40 m | 62.0 | 6.17 | 0.417 | 0.380 | 0.375 |
| W3OA | 55.7 | 6.01 | 0.342 | 0.336 | 0.363 |
| KC2LRC-1 | 51.4 | 5.51 | 0.289 | | 0.358 |
| KC2LRC-2 | 52.0 | 5.39 | 0.286 | | 0.352 |
| N2GZ | 42.7 | 6.33 | 0.276 | | 0.358 |

The desktop, 8 cores. *int-1 app*: full speed, 4 threads, no window, its report's pipeline CPU per channel-second and
its process CPU. *`main`*: `kz4ap-calls` at 6f89eb5, 4 threads, process CPU (detector, channels, decoders, callsign
stage; no display), one run each, for this record. *CW Skimmer*: the slope of its process CPU during real-time
playback, its display included. Channel-seconds are `main`'s (on the 2017 recordings the same as int-1's). On the Linux
machine, one thread, `main`'s default reads W3OA at 6.07 ms per channel-second (decision log, 2026-11-04).

## Findings

1. The bar's count is met on confirmed calls: 205 against 206 without a list, 219 against 206 with CW Skimmer's own.
   By recording we lose W3OA (47 or 48 against 57) and win the other three.
2. Precision is the gap. Without a list we report 510 calls against 406; the excess is misreadings (96 against 53:
   W3OA 1.5, KC2LRC-1 2.8, KC2LRC-2 1.9, N2GZ 1.6 times) and unknowns (79 against 46). A list closes it: 51 against 53
   with MASTER.DTA, 52 with MASTER.SCP.
3. The held-out two alone, confirmed / misreadings: with MASTER.DTA 117 / 25 against CW Skimmer's 104 / 30; without a
   list 108 / 51. That check is weaker than it looks: every tuning run printed the held-out totals too, and the first
   two list rules were chosen with them in sight (decision log, 2026-11-04, "A record to correct").
4. Plainly: at int-1 the total CPU per second of stream rose 16% to 73% over `real-1` (the owner's file 0.241 to 0.417)
   while the cost per channel-second did not (5.70 to 6.37 ms before, 5.39 to 6.33 after). The rise is more
   channel-seconds, 23% to 45% more on the 2017 recordings, nearly all short tracks retired as not keyed Morse: the
   finer detector opens more, and a place's first retirement reopens at once (decision log, 2026-11-04). The total is
   now about CW Skimmer's, at 43 to 62 channels open on average on these quiet recordings.
5. **Derived.** 500 channels at 6 ms per channel-second is 3.0 CPU-seconds per second of stream, under half the
   desktop's 8 cores, plus a fixed cost of 25 to 30 ms per second of stream (int-1's reports).

## What changed since checkpoint 2

`fast-5`: the decoder's search and evidence made cheaper (CPU about halved on real recordings), a lower-sidelobe
narrow filter for strong neighbors. `real-1`: recordings read as recorded (16-, 24-, 32-bit integer and 32-bit float
WAV, the `auxi` chunk, `--mirror`), exact resampling (250 to 192 kHz by 96/125) with the anti-alias stopband starting
at 96 kHz (review 8's fix, in `int-1`), and `kz4ap-resample` to give CW Skimmer the same samples. `det-1`: the
detector's own spectrum, 4 times finer (5.86 Hz bins), peaks by prominence and a keying-sideband test, tracks 30 Hz
apart, a 10 s DC blocker in place of the guard at 0 Hz, a first retirement left open. `call-2`: with a list, a call
named once passes only if listed; a one-letter suffix is held by a call two edits away at twice its points;
six-character locators are never calls; a list with NUL bytes or no call-shaped line is refused. Per-symbol margins
in the decoder are a variant, off by default (`rival_margin_nats` 0): no text changes, 5% to 7% more CPU per
channel-second.

**How the decoder works now.** The WAV is read, resampled to 192 kHz if need be, and its DC removed. The detector reads
its own fine spectrum every 85.3 ms and opens a track at a peak 7 dB re the floor (per 5.86 Hz bin) held 0.5 s. Each
track's channel, ±150 Hz at 1 500 samples/s, feeds the segmental decoder: two matched filters (12 and 36 ms), a level
belief, a search over stories of speed, gap stretch and text. The callsign stage scores call-shaped words by role and
repetition and holds back variants of better-supported calls. `docs/signal-processing.md` (sections 1, 6, 8e, 11 and
the parameter table) was checked against the code at 6f89eb5 for this record: no change needed.

## What comes next: the owner's decisions

1. Whether to ship or fetch a known-call list (MASTER.SCP; its licensing is yours to decide): without one our
   misreadings are 1.5 to 2.8 times CW Skimmer's.
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
real-recording count above comes from it. `cpu.csv` beside it holds the CPU table. The generated recordings' counts
are the decision log's; their per-call tables were not rerun. Not committed: recordings, lists, CW Skimmer's spots,
RBN day files. Our calls rerun in minutes with `kz4ap-calls` at 6f89eb5 (`--known` for a list, `--mirror` for the
owner's file).
