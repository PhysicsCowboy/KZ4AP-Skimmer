# CW Skimmer 2.1 against KZ4AP Skimmer: a feature survey

Written 2026-11-18 at the owner's request, so that none of CW Skimmer's functionality is left out by accident. He
named two purposes: to act a lot like CW Skimmer (the *skimmer*), and to be a simple CW decoder anyone can use (the
*plain decoder*), which has nothing to do with callsigns; a master call list is an option of the skimmer only.

**Sources.** CW Skimmer 2.1's help file (Afreet Software, 2018), decompiled into `build/cwskimmer-help/`
(git-ignored); rows cite its topics. Setting names: `build/cwskimmer-replay/template.ini` (git-ignored). Afreet's
web pages add nothing on spotting. Secondhand facts: `docs/research/decoder-survey.md`, and the RBN's post "How
Skimmer Server decides what to spot" (reversebeacon.blogspot.com/2021/09/how-skimmer-server-decides-what-to-spot.html,
read 2026-11-18; "RBN 2021 post" below). Our side: `README.md`, `docs/signal-processing.md` (cited as §),
`docs/backlog.md`, `app/`, `engine/`; an uncommitted survey of 2026-11-15 in `build/cwskimmer-survey/` was folded in.

**Terms.** CW: Morse code. SDR (software-defined radio): a receiver delivering a band as I/Q, two sample streams
(in-phase, quadrature) forming a complex signal. CAT (computer-aided transceiver): the serial link by which a
program reads and sets a radio's frequency; OmniRig is Afreet's engine for it. AGC: automatic gain control. DSP:
digital signal processing. WPM: words per minute. SNR: signal-to-noise ratio. UTC: Coordinated Universal Time. CPU:
processor. A *spot* reports a call heard on a frequency; a *DX cluster* is a telnet (plain-text network) server
relaying spots to loggers. RBN: the Reverse Beacon Network, which gathers skimmers' spots. A *running* station calls
CQ on its own frequency; S&P (search and pounce): tuning around answering runners. QSY: a move in frequency. QSO: a
contact. MASTER.DTA, MASTER.SCP: the Super Check Partial list of contest calls, binary and text. ITU: International
Telecommunication Union, which allocates prefixes. DX: a distant, often rare, station. RST: the signal report, 599
at best, sent 5NN in contests. QRL? "frequency in use?"; QRZ "who is calling?"; TEST: a contest CQ; UP "listening
higher"; GM "good morning"; TNX "thanks"; UR "your"; K "go ahead"; KN "go ahead, only you"; AR "end of message"; SK
"end of contact". POTA, SOTA: Parks, Summits on the Air; FD, SS, NA: Field Day, Sweepstakes, North American QSO
Party. IF: intermediate frequency; LO: local oscillator. MME, WDM: Windows sound-driver interfaces. UDP (User
Datagram Protocol): one-way network messages.

**Purpose:** S skimmer, D plain decoder, B both, N neither. **Ours:** has, partly, missing.

## 1. Features and settings

### Display, band map, calls window, decoded text

| Feature (topic; setting) | Does | Purpose | Ours | Where |
|---|---|---|---|---|
| Waterfall ("Waterfall display") | Band picture | B | has | `app/src/waterfall_widget.cpp` |
| Start/Stop processing (toolbar, F9) | Run or halt | B | has | Pause/Play, Start/Stop radio |
| Claims ("Overview"): 700 signals at once on a 3 GHz Pentium 4; waterfall read by eye to 45 WPM | Capacity | S; B | partly | at most 400 tracks (§10); readability not measured |
| Brightness, contrast, gamma ("Waterfall settings") | Picture tuning | B | partly | floor, range, auto floor; no gamma |
| Absolute or relative scale (`RelativeFreq`); pan by dragging ("Tuning") | View control | B | partly | axis always kHz from the center, named in its title; no toggle or pan |
| Anti-Click; noise blanker ("DSP Processor") | Key clicks off the waterfall; impulse noise cut (effect on decoding undocumented) | B | missing | |
| Status icons ("Status icons") | Red for CAT, input bandwidth or CPU trouble | B | partly | status text: silence, overload, loss, CPU |
| Always on Top, Band Scope (undescribed), no Windows animation ("Main menu", "Other settings") | Window conveniences | N | missing | |
| One decoder per signal, circle with hover text ("Band map") | Every signal decoded | S | has | track table (§10 "List") |
| Call labels at each signal ("Band map") | Calls on the band map | S | partly | separate call list; waterfall shows track numbers |
| Bold if seen twice; filter (`[BandMap] Filter`): Verified Calls (seen twice), All Calls (3 a label), Raw Text | Hide doubtful labels, or show raw text | S | partly | call list shows reported calls only; no filter |
| CQ or DE before a label ("Band map") | Running, or the sender's own call | S | partly | "How": signed, bare, named (§11 "Role"); no running vs S&P |
| QRL? and 599 labels (`ShowRst`) ("Band map") | A newcomer; where reports go (where a DX listens) | S | missing | |
| Green triangle on live stations; label follows a QSY | Live marker; one label a station | S | has; partly | green markers; a row sits at its latest signed place (A.12) |
| Delete All Labels | Restart detection | B | partly | Restart, Position slider |
| Callsign list ("Callsign list") | Verified calls, frequency, UTC; sortable | S | has | call, frequency, last heard, count, how |
| Aging: UTC refreshed every 15 min, dropped after 10 min idle; saved and restored | Current list | S | missing | never ages (backlog); `--calls` CSV at the end |
| Double-click a call to tune | | S | missing | |
| Decoder panel ("CW decoder") | Text at the operating frequency | B | partly | every track's text; no one-signal pane |
| Colors by kind (call, report, keyword); low-SNR characters pale; `»` for a long pause | Readability, likely errors | B | missing | backlog §2, character confidence |
| BLIND mode (`Blind`) | Waterfall only | D | missing | |

### Call validation

| Feature (topic; setting) | Does | Purpose | Ours | Where |
|---|---|---|---|---|
| Levels Minimal, Normal, Aggressive, Paranoid (`ValdLevel`; "Callsign Validation") | Higher rejects more | S | partly | one rule set; variants in `kz4ap-calls --set` |
| Watch list ("Watch list"); MASTER.DTA (`DtaFile`) | Calls taken as valid | S | partly | `--known-calls` reads MASTER.SCP, refuses .DTA (A.12); no watch list |
| Pattern: typical, unusual, suspicious | Rates a call's form | S | partly | form and refused words (A.12 "Form"); no pattern table |
| ITU block | Rejects unallocated prefixes | S | has | `allocated_pair`, A.12 |
| Colors by validation or by the logger (`ColorsFromLogger`); dupes gray | Shows status | S | missing | |

### Telnet server and spectrum output

| Feature (topic; setting) | Does | Purpose | Ours | Where |
|---|---|---|---|---|
| Read-only DX cluster, port 7300, optional password (`TelnetSrvEnabled`, `Port`, `PasswordRequired`; "Telnet server") | Spots to loggers | S | missing | design feature D, backlog §5 |
| Spot: frequency, call, SNR (key-on, 500 Hz, over fading), WPM, CQ or DE, UTC; reposted on finding a run ("Telnet commands") | The spot | S | missing | the call list has no SNR or WPM |
| "Do not send callsigns without CQ" (`CqOnly`; "Telnet settings") | CQing stations only | S | missing | section 2 |
| `SH/DX`; `SKIMMER/START`, `STOP`, `QSY`, `LO_FREQ`, `AUDIOIF`, `SETT` (`AllowAnn`); `BYE` | Old spots; remote control; log out | S | missing | |
| `SKIMMER/STATUS` (`AnnUserOnly`); "Clicked on" notices; client count, `TelnetSrv.log` | Logger status; clicks to logger | S | missing | |
| Spectrum via UDP (`UdpEnabled`; "Spectrum via UDP Settings") | Feeds a logger's band display | S | missing | |

### Inputs, rig control, tuning, audio

| Feature (topic; setting) | Does | Purpose | Ours | Where |
|---|---|---|---|---|
| 3-kHz receiver ("3-kHz receiver") | An ordinary radio's audio by sound card; radio AGC off, no narrow filter; CAT required | D | missing | design feature G, backlog §3 |
| SoftRock, SoftRock on IF, SDR-IQ, QS1R, Mercury, Perseus ("Radio"); SDR-1000, Flex-5000 via SoftRock on IF ("Other radios") | Wideband I/Q at 48 to 192 kHz | S | partly | SDRplay at 96 to 768 kHz, I/Q files |
| MME or WDM driver, signal and audio devices (`UseWdm`; "Audio") | Sound-card choice | B | missing | |
| Swap I/Q, shift a channel (`SwapIQ`, `ShiftQ`); automatic I/Q balance from strong signals ("I/Q balancer") | Wiring fixes; image suppression | S | partly | `--mirror`; no balance |
| Frequency calibration ("Frequency Calibration") | Against a known signal | B | missing | |
| CAT through OmniRig ("CAT control") | Reads and sets the radio | B | missing | design feature C |
| Click to tune, next station, PgUp/PgDn, band change, Tune At ("Tuning") | Tuning | B | missing | a row selection marks only |
| Max decoders or adaptive (`MaxDecoders`, `AdaptiveCnt`) | CPU control | S | partly | 400 tracks, budget when behind (§10); not user-set |
| Band plan, CW segments only (`BandPlan`, `CwSegmentOnly`) | Segment marks; skip non-CW | S | missing | |
| Selected signal's audio at a pitch (`Pitch`), CW filter 20 to 700 Hz (`FiltBW`), AGC, volume, mute ("DSP Processor") | Listening | B | partly | none live; `kz4ap-listen` writes a clip (A.13) |

### Recorder, settings, other

| Feature (topic; setting) | Does | Purpose | Ours | Where |
|---|---|---|---|---|
| I/Q recorder ("IQ recorder") | WAV named by UTC, no prompt, Move/Rename Last Recording; 24-bit; new file at 2 GB; tags (operator, start, center) | B | partly | asks; 16-bit; stops at 4 GiB; suggested name: center and local time, not UTC (`QDateTime::currentDateTime()`) |
| Playback: pause, seek, UTC clock, loop, file info, Play Last Played or Last Recorded | Replay | B | partly | Pause, Restart, Position, 2x to Max; no clock, loop, info or play last |
| Settings kept (`CwSkimmer.ini`); copies with own files (`ini=`) | Remembered setup | B | missing; partly | no settings store; separate processes |
| Autostart (`autostart`, F9) | Unattended start | B | has | file argument or `--radio` |
| Operator details; third-party tools ("Third party software") | Greeting, tags; RigSync, others | S | missing | |
| Registration, drivers, prerequisites, troubleshooting | | N | n/a | |

## 2. Callsign identification

### What CW Skimmer documents

1. **Extraction.** An analyzer pulls calls out of each decoder's text and uses keywords such as CQ, DE and TEST to
   tell the transmitting station's call from its correspondent's, and running from S&P ("Band map").
2. **Markers.** A label gets CQ if the station runs, DE if the call is the sender's. A telnet spot ends in CQ if the
   station runs, DE if it is not running but sends its own call, nothing otherwise, and is posted again when the
   station is found running ("Telnet commands"). No marker says "named by its correspondent".
3. **What is reported.** A call reaches telnet and the Callsigns window, and is bold, only if it passes validation
   ("Callsign Validation"); the window lists *verified* calls, appearing twice or more in a decoder's text
   ("Callsign list", "Band map"). Neither asks for a keyword.
4. **Validation.** Tests: watch list or MASTER.DTA; typical, unusual or suspicious pattern; prefix outside the ITU's
   blocks. Higher levels reject more; no numbers, and no repeat count in the tests. Secondhand (RBN 2021 post, for
   CW Skimmer Server's spots): at Normal 1 repeat on the watch list, else 2, 3 or 5 by how common the pattern is,
   within one decoder's 256-character buffer.
5. **`CqOnly`** sends spots only for CQing stations, on telnet. That it drops DE spots, and leaves the band map and
   window alone, is inference from "only for the CQ'ing stations".
6. **The RBN's rule** (secondhand, RBN 2021 post): a non-beacon station should send TEST or CQ (FD, SS, NA, UP in
   their settings) at least once a transmission; DE is not among them. So that beacons, which send no keyword, can
   be spotted, node operators have CW Skimmer Server send the aggregator all spots, "not just CQ spots", and the
   aggregator sorts beacons out: the keyword filter sits downstream of the skimmer (inference from that post).

### Does CW Skimmer count a call without CQ, DE or TEST as heard?

**Yes.** Documented: the help asks repeats (twice or more) only for bold labels and the Callsigns window, validation
only for telnet, and no keyword anywhere; only the optional `CqOnly` withholds unmarked spots. The documentary
evidence that unmarked calls are spotted is the help's sample output: 11 of 19 spots unmarked. Repeats for telnet
are secondhand (point 4). Measured on CW Skimmer 2.1's replays at Normal, `CqOnly` off (each `SKIMMER/SETT` reads
`vlNormal`, no CQ filter): telnet logs `build/cwskimmer-replay/out*/*/spots.txt` (git-ignored), by the marker
before the UTC field.

| Recording | Spots | CQ | DE | Unmarked | Calls | Never marked |
|---|---|---|---|---|---|---|
| 15 m, 2026-11-17 1700Z | 24 | 4 | 6 | 14 (58%) | 17 | 10 |
| 20 m, 2026-11-17 1400Z | 99 | 16 | 24 | 59 (60%) | 60 | 39 |
| 20 m, 2026-11-17 1900Z | 235 | 30 | 53 | 152 (65%) | 145 | 97 |
| 30 m, 2026-11-17 1529Z | 17 | 1 | 4 | 12 (71%) | 13 | 10 |
| 40 m, 2026-11-17 1230Z | 226 | 41 | 77 | 108 (48%) | 132 | 77 |
| 40 m, 2026-11-17 2229Z | 102 | 18 | 26 | 58 (57%) | 75 | 45 |
| 80 m, 2026-11-17 1013Z | 55 | 14 | 27 | 14 (25%) | 31 | 9 |
| W3OA, 2017 | 178 | 41 | 26 | 111 (62%) | 101 | 52 |
| KC2LRC-1, 2017 | 157 | 35 | 20 | 102 (65%) | 95 | 58 |
| KC2LRC-2, 2017 | 158 | 42 | 15 | 101 (64%) | 95 | 61 |
| N2GZ, 2017 | 252 | 44 | 45 | 163 (65%) | 117 | 65 |
| Owner's, 2026-11-04 | 52 | 15 | 6 | 31 (60%) | 33 | 14 |
| **All twelve** | **1 555** | | | **925 (59%)** | **914** | **537 (59%)** |

The 80 m replay of 2026-11-18 0059Z was still running and is left out.

**What an unmarked spot is.** Only that the analyzer did not find the station running or signing: a caller sending
their call alone, the norm in pileups and contests, or a call read in its correspondent's text; the spot cannot
tell which. Measured hints, not proof:

- On the seven 2026-11-17 recordings our app's text (`build/recordings/spare/*-report.txt`, git-ignored: rows kept
  at the end, up to about 4 000 symbols each), read with A.12's role words by a scratch script, holds 5 of CW
  Skimmer's 287 never-marked calls only named within 0.15 kHz of the spot (K4UX on 15 m: "K4UX DE K1JD"), 87 bare
  and 16 signed there (108 in all); of the other 179, 170 are not in our text and 9 only farther away. Both sides
  of a QSO on one frequency share a decoder, so even K4UX is not proof.
- On N2GZ, of the 71 unmarked spots of calls that also have a CQ or DE spot, 40 lie more than 0.3 kHz from all of
  that call's CQ and DE spots: read by a correspondent's decoder, or after a move.

Inference: CW Skimmer does not withhold a call read only from its correspondent; it counts repeats in a decoder's
text, whoever sent them.

### What KZ4AP Skimmer does now

`CallSearch` (§11, A.12) gives each reading a role from the words around it: *signed* (after DE, CQ, TEST, QRZ,
POTA, SOTA, DX or UP, or before K, KN, AR, SK, TEST, QRZ, CQ or UP), *named* (before DE, 5NN, 599, UR, GM, TNX and
the like), else *bare*. A call is reported at 2 points (a whole word 1) over the band within 30 min, whatever the
role; a listed call at 1 if framed (signed, named or sent twice). "How" is signed if any reading signed, else bare
if the station itself was read, else named (README).

So ours, like CW Skimmer, lists calls with no keyword. "How" plays the markers' part, finer (named apart from bare)
and coarser (CQ and DE lumped; no running or S&P). Nothing shows signed calls only. On the seven
2026-11-17 recordings our live call lists (`build/recordings/spare/*-calls.csv`, git-ignored) class CW Skimmer's 287
never-marked calls as 31 signed, 54 bare, 3 named, and lack 199, CW Skimmer's misreadings among them (measured).

The owner first proposed counting a call as heard only when CQ, DE or TEST precedes it. That rule is neither
`CqOnly` (CQ only, on telnet) nor the RBN's rule (CQ or TEST anywhere in the transmission, no DE), and narrower than
our signed role (which also takes a call before K, or after QRZ or POTA). CW Skimmer's DE marker means "sends its own
call", not "DE before it", so the table bounds the effect roughly: about the calls CW Skimmer never marks (59%) would
drop, less those our stage signs (31 of the 287); in a contest mostly S&P callers, who send their call bare
(inference). Given this survey, the owner ruled (2026-11-18) that the list keeps every call by default, as CW Skimmer
does, so that the program can one day feed the RBN, with an option to show signed calls only.

For the plain decoder none of this applies. The known-call list is optional (`--known-calls`), but the call stage
always runs and its list is always shown.

## 3. Gaps, as candidates (the coordinating session decides)

**Skimmer**, most important first:

1. Telnet spot server: SNR, WPM, CQ or DE marker, `SH/DX`, password, and an optional role filter (`CqOnly`, or
   signed only).
2. Calls drawn on the waterfall at their frequencies (CW Skimmer's band map), not track numbers.
3. Tuning: CAT through OmniRig (Hamlib on Linux), click to tune, next or previous station.
4. Call aging (10 min), spot updates on a change of role, running told from S&P.
5. Validation choices: levels, a watch list, MASTER.DTA read, a pattern table; a button that downloads MASTER.SCP
   for the user (the owner's request, 2026-11-18: the program never ships a list).
6. QRL? and 599 labels, band plan, CW segments only, SNR in the call list, spectrum over UDP.

**Plain decoder**, most important first:

1. Receiver-audio input by sound card (CW Skimmer's 3-kHz mode; feature G, backlog §3), else it needs an SDR. CW
   Skimmer's mode also requires CAT, for the dial frequency; ours could show audio offsets without it (inference).
2. A decoder-only mode: no call stage, call list or master list (the owner's requirement).
3. A one-signal text pane, picked on the waterfall, with doubtful characters shaded.
4. Audio of the selected signal with a CW filter and AGC (feature C).
5. Settings kept between runs; a choice of sound devices.

## 4. Since the survey (status as of 2026-11-19; the tables above are left as written)

- **Done on `main`:** skimmer gap 2 (calls drawn on the waterfall), the download button of gap 5, plain-decoder gaps 2
  (a decoder-only mode in the skimmer window), 3 (the one-signal pane, without shaded characters) and 5 (settings
  kept, without a choice of sound devices); also the signed-only option (section 2).
- **The owner's ruling of 2026-11-19:** the plain decoder is a separate, spartan window on receiver audio (his K3, for
  example) for one or a few signals, so plain-decoder gap 1 comes first for it and the skimmer window's decoder-only
  mode is not that product (`docs/backlog.md` section 4). The engine side of gap 1 (receiver audio from a file) is
  being built; GUI work follows the decoder's quality in the priorities.
