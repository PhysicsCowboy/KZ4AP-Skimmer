# Checkpoint 4: one row per station, and CW Skimmer's rule for a call named once (2026-11-15)

**Answer.** The side-by-side copies of one station that the owner saw are mostly gone, and without a known-call list we
now report about as many unconfirmed calls as CW Skimmer 2.1, where at checkpoint 3 we reported half as many again. Over
the four public 2017 recordings, one judge for both programs: 417 calls, 200 confirmed by the Reverse Beacon Network and
217 not, against CW Skimmer's 406, 206 and 200 (checkpoint 3: 510, 205, 305). The cost: confirmed calls 205 to 200 (by at
least two skimmers 190 to 188) against its 206, and on W3OA 47 to 45 against its 57. With CW Skimmer's 2018 list: 479,
226, 253. The rules that did most of it were made from these four; on three other real recordings (two older 40 m ones
from the first receiver, and the owner's, which the diagnosis read but no rule came from) `rob-1`'s review measured no
gain. On the owner's recording of 2026-11-04: 41 calls, 21 confirmed, 20 not (21 at checkpoint 3; CW Skimmer 33, 20, 13);
copies of a stronger neighbor hold 321 channel-seconds against 1 142 with the new copy test off, 24 against 347 at the
station he saw copied; the test changes no reported call on the five recordings (`calls.csv`). The test set reads as at checkpoint 2,
A2 on the detector's path better (seed 1, 0.250 to 0.239). **CPU, against the bar of at most 40 ms per channel-second
required and about 8 ms the goal:** 5.4 to 6.3 ms on the real recordings (the app, desktop), 3.6 ms on the test set
(Linux machine). Measured at ea7dcea unless marked.

**Terms.** *RBN*: the Reverse Beacon Network, public spots from CW Skimmers. *The RBN judge* (heuristic; checkpoint 3's,
now `training/kz4ap_proto/rbn_judge.py`) sorts each reported base call: *confirmed*, spotted on the RBN in the
recording's UTC window and 192 kHz span; else *RBN that day*; else *misreading*, one edit from a call spotted that day;
else *unknown*. *Unconfirmed*: the last three. Misreading is a proxy with systematic error both ways, not an error count.
*MASTER.DTA*: CW Skimmer's known-call list (2018). *Channel-second*: one channel open for one second. *Duplicate track*:
one of at least 8 symbols, at least half of them matched within 25 ms at both ends by a stronger track within 150 Hz
(A.11; analysis only). *Copy test*: `det-2`'s rule retiring a track whose keying is a stronger neighbor's (section 6).
*Transmission*, in the callsign stage: readings overlapping in time, not an over. *Oracle path*: the decoder is told each
station's frequency; *detector's path*: the app's. *CER*: edits over reference symbols, pooled over a group. Branches:
*int-1*, checkpoint 3's detector and input work; *call-2*, its list rules; *det-2*, the copy test; *rob-1*, the new
callsign rules.

## Scores on real recordings

CW Skimmer: the owner's replay of 2026-11-04 (validation "Normal", 500 decoders, its own MASTER.DTA), not run again.
*Ours*: `kz4ap-calls`, one thread (the desktop's app lists the same calls); *checkpoint 3*: its committed column
(d4068e1). Each cell: calls, confirmed, unconfirmed (of them misreadings). *Tuning*: the two recordings `call-2`'s list
rules were chosen on; `rob-1` used all four.

| Recording (length) | CW Skimmer | Checkpoint 3, no list | Ours, no list | Ours, MASTER.DTA 2018 |
|---|---|---|---|---|
| Owner's 40 m (367.5 s) | 33, 20, 13 (3) | 42, 21, 21 (2) | 41, 21, 20 (2) | 42, 22, 20 (2) |
| W3OA 40 m, tuning (900.6 s) | 101, 57, 44 (15) | 107, 47, 60 (23) | 91, 45, 46 (15) | 99, 48, 51 (11) |
| KC2LRC-1 20 m, tuning (899.4 s) | 95, 45, 50 (8) | 144, 50, 94 (22) | 124, 47, 77 (14) | 139, 54, 85 (17) |
| KC2LRC-2 20 m, held out (899.0 s) | 94, 49, 45 (12) | 135, 50, 85 (23) | 92, 49, 43 (9) | 117, 55, 62 (7) |
| N2GZ 40 m, held out (1 863.2 s) | 116, 55, 61 (18) | 124, 58, 66 (28) | 110, 59, 51 (18) | 124, 69, 55 (18) |
| **The four 2017** | **406, 206, 200 (53)** | **510, 205, 305 (96)** | **417, 200, 217 (56)** | **479, 226, 253 (53)** |

Confirmed by at least 2 RBN skimmers, the four 2017: CW Skimmer 193, checkpoint 3 190, ours 188, with the list 207.

**Duplicate tracks, the owner's file** (`duplicates-owner.csv`, a row per track). *Copy test off*: ea7dcea with
`--detector copy_window_s=0`: bf92612's tracks (`main` before `det-2`; the same channel-seconds on all five recordings),
whose own log lacks the levels the count needs. Channel-seconds here sum the tracks' lives on the log's
0.5 s marks, cut at 367.15 s, so they fall short of the engine's totals in `cpu.csv` (22 785 and 21 614).

| Owner's file | Tracks | Duplicates | Their channel-seconds, of all | At K4TZ's run, 9 700 to 10 000 Hz from the center |
|---|---|---|---|---|
| Copy test off | 1 354 | 26 | 1 142 of 22 768 (5.0%) | 2 tracks, 347 channel-seconds |
| Copy test on | 1 367 | 27 | 321 of 21 595 (1.5%) | 3 tracks, 24 channel-seconds |

**Generated recordings, no list** (`generated-calls.csv`), true / false calls (checkpoint 3's): contest 210 / 1 (220 / 6)
of 280 stations, fresh seed 228 / 0 (236 / 3); strong pairs 39 / 1 (40 / 2) of 48, fresh 40 / 0 (42 / 0); crowded band
300 / 0 of 300; 100 stations 100 / 0; G2 40 / 0 each seed; H2 40 / 1 seed 1, 40 / 0 seed 2 (40 / 1 before `rob-1`).

## The test set

DEV2, seeds 1 and 2, every recording (`segmental-ckpt4-s1.csv`, `-s2.csv`: oracle path and A2's detector recordings;
`segmental-ckpt4-detector.csv`: the rest through the detector); checkpoint 2's `segmental-app9-whole.csv` and `-seed2.csv`. Intervals: 95%,
paired bootstrap over signals.

| Group (signals a seed) | Oracle, seed 1 | Oracle, seed 2 | Detector's path, seed 1 | Detector's path, seed 2 | Checkpoint 2, seed 1 (oracle; detector) |
|---|---|---|---|---|---|
| A2 sensitivity (6 720) | 0.2254 | 0.2258 | 0.2385 | 0.2385 | 0.2261; 0.2496 |
| B2 fading (1 920) | 0.2421 | 0.2432 | 0.2434 | 0.2443 | 0.2422 |
| C2 fists (3 600) | 0.0627 | 0.0623 | 0.0633 | 0.0626 | 0.0628 |
| D2 speed changes (108) | 0.0001 | 0.0002 | 0.0001 | 0.0002 | 0.0001 |
| E2 interference (90) | 0.5028 | 0.6420 | 0.5327 | 0.6882 | 0.4904 |
| F2 tuning (80) | 0.0862 | 0.0935 | 0.0858 | 0.0919 | 0.0851 |
| G2 QSO, one track (20) | 0.0552 | 0.0403 | 0.0544 | 0.0410 | 0.0552 |
| H2 QSO, two tracks (40) | 0.0564 | 0.0472 | 0.6253 | 0.6152 | 0.0559 |
| I2 Farnsworth (864) | 0.0171 | 0.0179 | 0.0191 | 0.0192 | 0.0211 |
| S2 stretch (672) | 0.2359 | 0.2418 | 0.2536 | 0.2572 | 0.2368 |

On the detector's path A2's signals get a track 6 505 and 6 492 times of 6 720 (checkpoint 2, seed 1: 5 607), S2's 636 and
628 of 672, every other group's all.

## CPU

| Recording | Channel-seconds per s of stream | ms per channel-second, app's pipeline | CPU-s per s of stream, app | `kz4ap-calls`, Linux, 1 thread | CW Skimmer |
|---|---|---|---|---|---|
| Owner's 40 m | 58.8 | 6.29 | 0.378 | 0.376 | 0.375 |
| W3OA | 55.2 | 5.97 | 0.336 | 0.339 | 0.363 |
| KC2LRC-1 | 50.2 | 5.50 | 0.281 | 0.286 | 0.358 |
| KC2LRC-2 | 50.5 | 5.43 | 0.280 | 0.283 | 0.352 |
| N2GZ | 42.3 | 6.20 | 0.268 | 0.271 | 0.358 |

`cpu.csv`. *App*: the owner's build, desktop (8 cores), offscreen, full speed, 4 threads. *CW Skimmer*: its process
CPU's slope in real-time playback, display included (checkpoint 3's rows). The whole test set, Linux machine, 8 jobs on
10 cores: 3.61 ms per channel-second each seed (checkpoint 2: 6.88), from `cpu-whole-s1.csv` and `-s2.csv`.

## Findings

1. **Measured.** Without a list, against checkpoint 3, 137 calls left the 2017 lists (18 confirmed, 37 spotted that
   day, 47 misreadings, 35 unknown) and 44 came (13, 14, 7, 10).
2. **Measured, weak evidence.** `rob-1`'s rules (no call from one named word, CW Skimmer's documented practice; readings
   pooled over the band, a heuristic) were made from these four recordings, so their gain here is not independent. The
   check is `rob-1`'s review (decision log, 2026-11-15, its own judge) on real30 and live2, 30 min and 62 s of 40 m from
   the first receiver on the K3's IF port, probably leakage, so a weak check: real30 9 to 8 confirmed and 6 to 9
   misreadings, live2 7 to 6 calls; the owner's file 21 confirmed both. The contest lost 10 true calls and 5 false: a
   caller whose own signal is never read now waits for a second reading.
3. **Measured.** With MASTER.DTA: 446 to 479 calls, confirmed 219 to 226, unconfirmed 227 to 253 (CW Skimmer 406, 206,
   200). That pooling adds them is the rules' logic, not measured.
4. **Measured.** The copy test takes 72% of the owner's duplicate channel-seconds (1 142 to 321) and 93% at K4TZ's run
   (347 to 24); a duplicate now lives 11.9 s on average against 43.9 s, so their number stays (26 to 27). The 2017
   recordings' figures are `det-2`'s (decision log), reproduced exactly.
5. **Measured.** The test set is where checkpoint 2 left it but three gains: A2 on the detector's path (seed 1 0.2496 to
   0.2385, [−0.0125, −0.0098]), from `det-1`'s detector; I2 (0.0211 to 0.0171, [−0.0071, −0.0011]), concentrated (39
   signals better, 69 worse), as `fast-5` measured for its ceiling bonus (I2 0.0211 to 0.0168, decision log 2026-11-01);
   A2 told the frequency (0.2261 to 0.2254, [−0.0012, −0.0001]), not traced. Every other interval holds 0, E2's widest
   (0.490 to 0.503 on seed 1, 0.651 to 0.642 on seed 2). `det-2` and `rob-1` change no scored signal. H2's 0.62 on the
   detector's path is the bench scoring a station against one track while its overs open several (seen on one
   recording); its calls are the check.
6. **Measured.** CPU per channel-second is flat against int-1's 5.39 to 6.33 ms; CW Skimmer's per-second figure holds
   its display, ours none. These recordings hold 42 to 59 channels on average; the busy band is still the test.

## What changed since checkpoint 3

`det-2`: a track whose keying follows an older, proven neighbor 15 dB or more above it at the detector, within 150 Hz, is
retired as its copy (last 6 s of both envelopes, each less its 0.5 s moving mean: correlation ρ ≥ 0.5, gain β ≤ 1, own
power at most 0.25 of what leak and hiss explain; forms derived, values heuristics chosen by measurement). `rob-1`:
without a list a call named once no longer passes alone, readings add over the band one per transmission, a glued
word's pieces keep their own times. `sdr-1`: only the SDRplay radio with the serial asked for opens. `listen-1`:
`kz4ap-listen` (A.13). `license-1`: `LICENSE-EXCEPTION.md`. `tests-1`, `ci-1`, `ckpt-4`: per-process test folders,
GitHub's builds green, the RBN judge committed.

**How the decoder works now** (heuristic unless marked; `docs/signal-processing.md` gives the provenance). The WAV is
read, resampled to 192 kHz if need be, its DC removed (10 s blocker). The detector reads its own 5.86 Hz bins every
85.3 ms (derived from the bins) and opens a track at a peak 7 dB re the floor per fine bin held 0.5 s, tracks at least
30 Hz apart. Each track's channel, ±150 Hz at 1 500 samples/s, feeds the segmental decoder: matched filters of 12 and
36 ms, a level belief, a search over stories of speed, gap stretch and text. Once a second the track judge retires a
track that is not keyed Morse, or only copies a stronger, older neighbor. Without a list the callsign stage reports a
call at 2 points from two transmissions anywhere in the band, holding back variants of a better-supported call within
150 Hz. `docs/signal-processing.md` matches ea7dcea after review 16's fixes (the pooling rule, the duplicate rule, the
DC blocker, section 6's off variant; body 5 999 words).

## What comes next

The owner's decisions, one at a time: (1) try the app, `build\try-the-app\GUIDE.txt` (git-ignored; in chat), 10 minutes,
now with a double-click to hear one station; (2) the known-call list, open since checkpoint 3 (with the 2018 list 226
confirmed and 253 unconfirmed, without 200 and 217; its licensing is yours); (3) the push, after (1): nothing pushed
since a34069e.

Next work, in the owner's order: the spare receiver for real data, once he says it is connected (the busy-band hour
first); the CW Skimmer survey, then GUI polish; receiver-audio input; then the telnet spot server.

**Memory notes** (this project's folder, since checkpoint 3): `feedback-let-him-try-it.md`, new (a double-click trial at
each checkpoint, no push request before it); `feedback-one-decision-at-a-time.md`, updated (questions on past work before
the push question); `kz4apskimmer-project-state.md`, updated; `MEMORY.md`, the index.

## Data and reproducing

In `docs/plans/data/2026-11-15-checkpoint-4/`: `calls.csv` (a row per base call per recording, 0/1 per run, the copy test off
among them; unreported rows are North American skimmers' window calls), `windows.csv`, `cpu.csv`, `cpu-whole-s1.csv`, `-s2.csv`, `generated-calls.csv`,
`duplicates-owner.csv`, the three score tables; every number above comes from them, but those cited to the decision log
and the bootstrap intervals. To rerun: MASTER.DTA's calls as text (its NUL-terminated strings of 3 to 12 letters, digits
or / with a letter and a digit: 39 509, one a line); `kz4ap-calls` at ea7dcea (`--mirror`, `--known`, `--detector
copy_window_s=0` as above); `python -m kz4ap_proto.rbn_judge` with `--start`, `--seconds`, `--center-khz` from
`windows.csv` and the RBN's public day file.
