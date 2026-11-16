# Checkpoint 4: one row per station, and CW Skimmer's rule for a call named once (2026-11-15)

**Answer.** The side-by-side copies of one station that the owner saw are mostly gone, and without a known-call list we
now report about as many calls as CW Skimmer 2.1, confirmed and unconfirmed alike, where at checkpoint 3 we reported half
as many unconfirmed again. Over the four public 2017 recordings, one judge for both programs: 417 calls, 200 confirmed by
the Reverse Beacon Network and 217 not, against CW Skimmer's 406, 206 and 200 (checkpoint 3: 510, 205, 305). With CW
Skimmer's own 2018 list: 479, 226, 253. The rules that did most of it were made from these same four recordings; on the
three real recordings not used to make them, `rob-1`'s review measured no gain. On the owner's 40 m recording of
2026-11-04: 41 calls, 21 confirmed, 20 not (CW Skimmer 33, 20, 13); tracks that copy a stronger neighbor hold 321
channel-seconds there, against 1 142 with the new copy test off, and 24 against 347 at the station he saw copied. The test set reads as at checkpoint 2, but better where
the detector's finer spectrum acts (A2 on the detector's path, seed 1, 0.250 to 0.239). **CPU, against the bar of at most
40 ms per channel-second required and about 8 ms the goal:** 5.4 to 6.3 ms per channel-second on the five real recordings
(the app, desktop), 3.6 ms on the whole test set (Linux machine): under the goal. Measured at ea7dcea unless marked.

**Terms.** *RBN*: the Reverse Beacon Network, public spots from CW Skimmers around the world. *The judge* (a heuristic,
checkpoint 3's, now committed as `training/kz4ap_proto/rbn_judge.py`) sorts each reported base call (/P and prefixes
dropped): *confirmed*, spotted on the RBN inside the recording's UTC window and 192 kHz span; else *RBN that day*,
spotted that day elsewhere; else *misreading*, one edit (a letter changed, added or dropped) from a call spotted in the
window or that day; else *unknown*. *Unconfirmed*: the last three. The misreading class is a proxy with systematic error
both ways, not an error count (checkpoint 3: a real station the RBN missed lands in it about two times in three).
*MASTER.DTA*: CW Skimmer's known-call list (2018). *Channel-second*: one channel open for one second of stream. *Duplicate
track*: one with at least half its symbols, and 8 or more, matched within 25 ms at both ends by a stronger track within
150 Hz (A.11; `det-2`'s heuristic, analysis only). *Copy test*: `det-2`'s rule that retires such a track (section 6).
*Oracle path*: the decoder is told each station's frequency; *detector's path*: what the app runs. *CER*: edits over
reference symbols, spaces counted, pooled over a group's scored signals.

## Scores on real recordings

CW Skimmer: the owner's replay of 2026-11-04 (validation "Normal", 500 decoders, its own MASTER.DTA), not run again.
*Ours*: `kz4ap-calls`, one thread, Linux machine (the desktop's app and `kz4ap-calls` list the same calls); *checkpoint 3*:
its committed column (d4068e1). Each cell: calls, confirmed, unconfirmed (of them misreadings). *Tuning*: the two
recordings `call-2`'s list rules were chosen on; `rob-1` used all four.

| Recording (length) | CW Skimmer | Checkpoint 3, no list | Ours, no list | Ours, MASTER.DTA 2018 |
|---|---|---|---|---|
| Owner's 40 m (367.5 s) | 33, 20, 13 (3) | 42, 21, 21 (2) | 41, 21, 20 (2) | 42, 22, 20 (2) |
| W3OA 40 m, tuning (900.6 s) | 101, 57, 44 (15) | 107, 47, 60 (23) | 91, 45, 46 (15) | 99, 48, 51 (11) |
| KC2LRC-1 20 m, tuning (899.4 s) | 95, 45, 50 (8) | 144, 50, 94 (22) | 124, 47, 77 (14) | 139, 54, 85 (17) |
| KC2LRC-2 20 m, held out (899.0 s) | 94, 49, 45 (12) | 135, 50, 85 (23) | 92, 49, 43 (9) | 117, 55, 62 (7) |
| N2GZ 40 m, held out (1 863.2 s) | 116, 55, 61 (18) | 124, 58, 66 (28) | 110, 59, 51 (18) | 124, 69, 55 (18) |
| **The four 2017** | **406, 206, 200 (53)** | **510, 205, 305 (96)** | **417, 200, 217 (56)** | **479, 226, 253 (53)** |

Confirmed by at least 2 RBN skimmers, the four 2017: CW Skimmer 193, checkpoint 3 190, ours 188, with the list 207; by
at least 3: 180, 177, 176, 193.

**Duplicate tracks, the owner's file** (`duplicates-owner.csv`, one row per track; channel-seconds summed over the
tracks' lives). *Copy test off*: ea7dcea with `--detector copy_window_s=0`, the same tracks as `main` before `det-2`
(bf92612: the same channel-seconds on all five recordings); its own log has no levels, which the count needs.

| Owner's file | Tracks | Duplicates | Their channel-seconds, of all | Their decoder CPU, of all | At K4TZ's run, 9 700 to 10 000 Hz from the center |
|---|---|---|---|---|---|
| Copy test off | 1 354 | 26 | 1 142 of 22 768 (5.0%) | 4.6 of 122.4 s (3.7%) | 2 tracks, 347 channel-seconds |
| Copy test on (ea7dcea) | 1 367 | 27 | 321 of 21 595 (1.5%) | 1.8 of 118.3 s (1.6%) | 3 tracks, 24 channel-seconds |

**Generated recordings, no list** (`generated-calls.csv`), true and false calls (stations), checkpoint 3's in brackets: the
contest 210 and 1 (220 and 6) of 280, its fresh seed 228 and 0 (236 and 3); strong pairs 40 to 150 Hz apart 39 and 1 (40
and 2) of 48, fresh 40 and 0 (42 and 0); the crowded band 300 and 0 of 300, 100 stations at 192 kHz 100 and 0, G2 40 and
0 on each seed, H2 40 and 1 on seed 1, 40 and 0 on seed 2 (all as before).

## The test set

DEV2, seeds 1 and 2, every recording; scored signals and CER (`segmental-ckpt4-s1.csv`, `-s2.csv`: oracle path and A2's
detector recordings; `segmental-ckpt4-detector.csv`: every other group through the detector). Checkpoint 2: its committed
`segmental-app9-whole.csv` and `-seed2.csv`; change on seed 1 with its 95% interval by bootstrap over signals.

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

On the detector's path, of A2's 6 720 signals a seed 6 505 and 6 492 get a track (checkpoint 2, seed 1: 5 607), S2's 636
and 628 of 672; every other group's all.

## CPU

| Recording | Channel-seconds per s of stream | ms per channel-second, app's pipeline | CPU-s per s of stream, app | `kz4ap-calls`, Linux, 1 thread | CW Skimmer |
|---|---|---|---|---|---|
| Owner's 40 m | 58.8 | 6.29 | 0.378 | 0.376 | 0.375 |
| W3OA | 55.2 | 5.97 | 0.336 | 0.339 | 0.363 |
| KC2LRC-1 | 50.2 | 5.50 | 0.281 | 0.286 | 0.358 |
| KC2LRC-2 | 50.5 | 5.43 | 0.280 | 0.283 | 0.352 |
| N2GZ | 42.3 | 6.20 | 0.268 | 0.271 | 0.358 |

`cpu.csv`. *App*: the owner's build of ea7dcea on the desktop (8 cores), offscreen, full speed, 4 threads; its pipeline
CPU over channel-seconds, and its process CPU. *CW Skimmer*: the slope of its process CPU in real-time playback, its
display included (checkpoint 3's rows). The whole test set, Linux machine, 8 jobs on 10 cores: 3.61 ms per channel-second
on each seed (checkpoint 2, seed 1: 6.88), weighted by channel-seconds from `cpu-whole-s1.csv` and `-s2.csv`; on the
desktop about 3.8 to 4.2 ms (derived, by checkpoint 2's measured ratio of the two machines, 1.05 to 1.16). `det-2` and
`rob-1` together, bf92612 against ea7dcea, one thread side by side: CPU-s per s of stream 0.392 to 0.376 on the owner's
file, with the copies' channel-seconds (22 785 to 21 614); 0.270 to 0.343 before and 0.271 to 0.339 after on the 2017
recordings.

## Findings

1. **Measured.** Without a list the unconfirmed calls fell from 305 to 217 over the four 2017 recordings (CW Skimmer
   200), and the confirmed from 205 to 200 (206; by 2 skimmers 190 to 188, CW Skimmer 193). Against checkpoint 3, 137
   calls left the list (18 confirmed, 37 spotted that day, 47 misreadings, 35 unknown) and 44 came (13, 14, 7, 10).
   W3OA is still the shortfall: 45 confirmed against 57.
2. **Measured, weak evidence.** `rob-1`'s rules (no call from one named word, CW Skimmer's documented practice; readings
   pooled over the band, a heuristic) were made from these four recordings, so their gain here is not independent. On
   the three real recordings not used (real30, live2, the owner's file) `rob-1`'s review measured none (real30 9 to 8
   confirmed, misreadings 6 to 9; live2 7 to 6 calls; the owner's 21 confirmed both). The contest lost 10 true calls
   (220 to 210) with 5 false fewer: callers whose own signal is never read now wait for a second reading.
3. **Measured.** With MASTER.DTA, pooling adds listed calls: 446 to 479 calls, confirmed 219 to 226, unconfirmed 227
   to 253 (CW Skimmer 406, 206, 200).
4. **Measured.** The copy test takes 72% of the owner's duplicate channel-seconds (1 142 to 321) and 93% at K4TZ's
   run (347 to 24); a duplicate now lives 11.9 s on average against 43.9 s, so their number stays (26 to 27). On the
   2017 recordings 837 to 535, 1 140 to 366, 1 560 to 335 and 1 447 to 971 channel-seconds (W3OA, KC2LRC-1, KC2LRC-2,
   N2GZ; `det-2`'s figures, reproduced exactly here, not in a committed table). A caller's row stays beside K4TZ and
   prints his exchange too, since its ±150 Hz channel hears him (the app's track at +9 885 Hz reads KK4E, K8WWS,
   VE3NE and "TU K4TZ CQ"): one QSO's text on two rows, not a copy.
5. **Measured.** The test set is where checkpoint 2 left it, but A2 on the detector's path (seed 1 0.2496 to 0.2385,
   [-0.0125, -0.0098]), I2 (0.0211 to 0.0171, [-0.0071, -0.0011]) and A2 told the frequency (0.2261 to 0.2254,
   [-0.0012, -0.0001]); every other group's interval holds 0, E2's widest: 0.490 to 0.503 on seed 1, 0.651 to 0.642 on
   seed 2 (90 signals). `det-2` and `rob-1` change no scored
   signal (`det-2`'s own run: every signal the same; the callsign stage is not in the bench). H2 reads 0.62 on the
   detector's path because the bench scores each station against one track while its overs, minutes apart, open
   several (seen on H2's first recording, seed 1: the scored track starts at a later over); checkpoint 2's app joined
   a place's tracks for 0.061. Its calls are the check: 40 and 1, 40 and 0.
6. **Measured.** CPU stays under the goal: 5.43 to 6.29 ms per channel-second on the real recordings, against int-1's
   5.39 to 6.33 at checkpoint 3; per second of stream our 0.27 to 0.38 CPU-seconds against CW Skimmer's 0.35 to 0.38,
   its display in its figure and none in ours. These recordings hold 42 to 59 channels open on average; the busy band,
   where the 400-track cap would matter, is still the test.

## What changed since checkpoint 3

`det-2`: a track whose keying follows an older, proven neighbor 15 dB or more stronger within 150 Hz is retired as its
copy (over the last 6 s of both decoders' envelopes, each less its 0.5 s moving mean: correlation ρ ≥ 0.5, gain β ≤ 1,
and the track's own power at most 0.25 of what the leak and its hiss explain; the forms derived, the values heuristics
chosen by measurement). `rob-1`: without a list a call named once by another station no longer passes alone
(`points_named` 2), a call's readings add over the whole band, one per transmission (`pool_places`), and a glued word's
pieces keep their own characters' times. `sdr-1`: the SDRplay source opens only the radio with the exact serial asked for
(`--sdrplay-serial`, `--list-sdrplay`, a "Radio serial" box). `listen-1`: `kz4ap-listen` writes one station of an I/Q
recording as a mono WAV (A.13). `tests-1`: each C++ test process has its own temporary folder. `ci-1`: GitHub's builds
pass again. `ckpt-4`: the judge committed, with a test.

**How the decoder works now** (settings are heuristics unless marked derived; `docs/signal-processing.md` gives each
one's provenance). The WAV is read, resampled to 192 kHz if need be (250 kHz by 96/125, flat to ±86 kHz), and its DC
removed (10 s blocker). The detector reads its own spectrum of 5.86 Hz bins every 85.3 ms (derived from the bins) and opens
a track at a peak 7 dB re the floor (per fine bin) held 0.5 s, tracks at least 30 Hz apart. Each track's channel, ±150 Hz
at 1 500 samples/s, feeds the segmental decoder: two matched filters (12 and 36 ms), a level belief, a search over stories
of speed, gap stretch and text. Once a second the judge retires a track that is not keyed Morse, and now one that only
copies a stronger, older neighbor. The callsign stage scores call-shaped words by role; without a list a call needs 2
points from at least two transmissions anywhere in the band, and a variant of a better-supported call within 150 Hz is
held back. `docs/signal-processing.md` was brought up to date by each merge since checkpoint 3; its copy-test and
callsign defaults were checked against the code at ea7dcea for this record and needed no change (body 6 000 words).

## What comes next

The owner's decisions, one at a time: (1) try the app: `build\try-the-app\GUIDE.txt` (git-ignored; in chat), about 10
minutes on your own file, now with a double-click to hear one station; (2) the known-call list, still open from checkpoint
3: with CW Skimmer's 2018 list we confirm 226 against its 206, at 253 unconfirmed against 200; without one 200 and 217;
its licensing is yours; (3) the push, after (1): nothing pushed since a34069e.

Next work, in the owner's order: the spare receiver for real data, once he says it is connected (the busy-band hour
first); the CW Skimmer survey, then GUI polish; receiver-audio input; then the telnet spot server.

**Memory notes** (the coordinating session's, this project's folder only, since checkpoint 3):
`feedback-let-him-try-it.md`, new: at each checkpoint a double-click way to try the app on a known recording, given in
chat, and no push request before you have tried it; `feedback-one-decision-at-a-time.md`, updated: questions about past
work come before the push question, each in its own message; `kz4apskimmer-project-state.md`, the standing state,
updated; `MEMORY.md`, the index.

## Data and reproducing

In `docs/plans/data/2026-11-15-checkpoint-4/`. `calls.csv`: one row per base call per recording, the judge's class, RBN
skimmers in the window, the calls a misreading is one edit from (the first three), and 0/1 for CW Skimmer, checkpoint
3's two runs (copied from its table) and this checkpoint's two; rows no one reported are North American skimmers' calls in
the window. `windows.csv`: each recording's window and center, checkpoint 3's (with them the committed judge reproduces
checkpoint 3's table, all 1 020 rows). `cpu.csv`, `cpu-whole-s1.csv`, `cpu-whole-s2.csv`, `generated-calls.csv`,
`duplicates-owner.csv` and the three score tables as named above. Every number above comes from these tables, but those
cited to `det-2`'s and `rob-1`'s decision-log lines and the 95% intervals (bootstraps over the tables, not kept). Not
committed: recordings, lists, CW Skimmer's spots, RBN day files (public). The calls rerun in minutes with `kz4ap-calls`
at ea7dcea (`--mirror` for the owner's file, `--known` for a list, `--detector copy_window_s=0` for the copy test off),
then `python -m kz4ap_proto.rbn_judge`.
