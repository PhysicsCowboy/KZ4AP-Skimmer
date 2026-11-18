# CW Skimmer 2.1 against KZ4AP Skimmer: a feature survey

Written 2026-11-18 at the owner's request: survey CW Skimmer's functionality so that none of it is left out by
accident, callsign identification included. He named two purposes for this program: to act a lot like CW Skimmer
(the *skimmer*), and to be a simple CW decoder anyone can use (the *plain decoder*), which has nothing to do with
callsigns; a master call list, if any, is an option of the skimmer only.

**Sources.** CW Skimmer 2.1's help file (Afreet Software, 2018), decompiled into `build/cwskimmer-help/`
(git-ignored); rows cite its topics by name. Setting names: the replay's settings file,
`build/cwskimmer-replay/template.ini` (git-ignored). Afreet's web pages add nothing on spotting. Secondhand facts
come through `docs/research/decoder-survey.md`. Our side: `README.md`, `docs/signal-processing.md` (cited as §),
`docs/backlog.md`, `app/`, `engine/`. An uncommitted first survey of 2026-11-15 (`build/cwskimmer-survey/survey.md`)
was checked against the help and folded in.

**Terms.** CW: Morse code. SDR (software-defined radio): a receiver that delivers a band as I/Q, two sample streams
(in-phase and quadrature) forming a complex signal. CAT (computer-aided transceiver): the serial link over which a
program reads and sets a radio's frequency. OmniRig: Afreet's radio-control engine. AGC: automatic gain control.
DSP: digital signal processing. WPM: words per minute. SNR: signal-to-noise ratio. UTC: Coordinated Universal Time.
A *spot* is a one-line report of a call heard on a frequency; a *DX cluster* is a telnet (plain-text network)
server relaying spots to logging programs. RBN: the Reverse Beacon Network, which gathers many skimmers' spots. A
*running* station calls CQ on its own frequency; one doing S&P (search and pounce) tunes around answering runners.
QSY: a move in frequency. QSO: a two-way contact. MASTER.DTA and MASTER.SCP: the Super Check Partial list of active
contest calls, binary and text forms. ITU: International Telecommunication Union, which allocates call prefixes.

**Purpose:** S skimmer, D plain decoder, B both, N neither. **Ours:** has, partly, missing.

## 1. Features and settings

### Display, band map, calls window, decoded text

| CW Skimmer feature (help topic; setting) | What it does | Purpose | Ours | Where ours stands |
|---|---|---|---|---|
| Waterfall ("Waterfall display") | Band picture, strength as brightness | B | has | `app/src/waterfall_widget.cpp` |
| Brightness, contrast, gamma ("Waterfall settings") | Picture tuning | B | partly | floor, range, auto floor; no gamma |
| Absolute or relative scale (`RelativeFreq`); pan by dragging ("Tuning") | View control | B | partly | absolute when the center is known; no toggle, no pan |
| Anti-Click; noise blanker ("DSP Processor") | Key clicks off the waterfall; impulse noise suppressed (effect on decoding not documented) | B | missing | |
| Status icons ("Status icons") | Red for CAT, input bandwidth or CPU trouble | B | partly | status text: radio silent, overload, pipeline lost, CPU |
| Always on Top, Band Scope (undescribed), no Windows animation ("Main menu", "Other settings") | Window conveniences | N | missing | |
| One decoder per signal, circle with hover text ("Band map") | Every signal decoded | S | has | track table (§10 "List") |
| Call labels at each signal's frequency ("Band map") | Calls on the band map | S | partly | separate call list; the waterfall shows track numbers |
| Bold if seen twice; filter Verified / All / Raw (`[BandMap] Filter`) | Hide doubtful labels | S | partly | call list shows reported calls only; no filter |
| CQ or DE before a label ("Band map") | Running, or the sender's own call | S | partly | "How": signed, bare, named (§11 "Role"); no running vs S&P |
| QRL? and 599 labels (`ShowRst`) ("Band map") | A newcomer; where reports go, to find where a DX station listens | S | missing | |
| Green triangle on live stations; label follows a QSY | Live marker; one label per station | S | has; partly | live markers green; a row sits at its latest signed place (A.12 "Row") |
| Delete All Labels | Restart detection | B | partly | Restart, Position slider |
| Callsign list ("Callsign list") | Verified calls, frequency, UTC; sortable | S | has | call, frequency, last heard, count, how |
| Aging: UTC refreshed every 15 min, dropped after 10 min idle; saved and restored | Current list | S | missing | never ages (backlog, callsign stage); `--calls` CSV at the end |
| Double-click a call to tune | | S | missing | |
| Decoder panel ("CW decoder") | Text at the operating frequency | B | partly | every track's text; no one-signal pane |
| Colors by kind (call, report, keyword); low-SNR characters pale; `»` for a long pause | Readability, likely errors | B | missing | backlog §2, character confidence |
| BLIND mode (`Blind`) | Waterfall only | D | missing | |

### Call validation

| CW Skimmer feature (help topic; setting) | What it does | Purpose | Ours | Where ours stands |
|---|---|---|---|---|
| Levels Minimal, Normal, Aggressive, Paranoid (`ValdLevel`; "Callsign Validation") | Higher rejects more | S | partly | one rule set; variants in `kz4ap-calls --set` |
| Watch list ("Watch list"); MASTER.DTA (`DtaFile`) | Calls taken as valid | S | partly | `--known-calls` reads MASTER.SCP text, refuses .DTA (A.12); no separate watch list |
| Pattern: typical, unusual, suspicious | Rates a call's form | S | partly | form and refused words (A.12 "Form"); no pattern table |
| ITU block | Rejects unallocated prefixes | S | has | `allocated_pair`, A.12 |
| Colors by validation or by the logger (`ColorsFromLogger`); dupes gray | Shows status | S | missing | |

### Telnet server and spectrum output

| CW Skimmer feature (help topic; setting) | What it does | Purpose | Ours | Where ours stands |
|---|---|---|---|---|
| Read-only DX cluster, port 7300, optional password (`TelnetSrvEnabled`, `Port`, `PasswordRequired`; "Telnet server") | Spots to loggers | S | missing | design feature D, backlog §5 |
| Spot: frequency, call, SNR (key-on, 500 Hz, averaged over fading), WPM, CQ or DE, UTC; reposted when found running ("Telnet commands") | The spot | S | missing | the call list has no SNR or WPM |
| "Do not send callsigns without CQ" (`CqOnly`; "Telnet settings") | CQing stations only | S | missing | section 2 |
| `SH/DX`; `SKIMMER/START`, `STOP`, `QSY`, `LO_FREQ`, `AUDIOIF`, `SETT` (`AllowAnn`) | Old spots; remote control | S | missing | |
| `SKIMMER/STATUS` (`AnnUserOnly`); "Clicked on" notices; client count, `TelnetSrv.log` | Logger colors calls; logger gets clicks | S | missing | |
| Spectrum via UDP (`UdpEnabled`; "Spectrum via UDP Settings") | Feeds a logger's band display | S | missing | |

### Inputs, rig control, tuning, audio

| CW Skimmer feature (help topic; setting) | What it does | Purpose | Ours | Where ours stands |
|---|---|---|---|---|
| 3-kHz receiver ("3-kHz receiver") | An ordinary radio's audio by sound card; radio AGC off, no narrow filter | D | missing | design feature G, backlog §3 |
| SoftRock, SoftRock on IF, SDR-IQ, QS1R, Mercury, Perseus ("Radio") | Wideband I/Q at 48 to 192 kHz | S | partly | SDRplay at 96 to 768 kHz, I/Q files |
| MME or WDM driver, signal and audio devices (`UseWdm`; "Audio") | Sound-card choice | B | missing | |
| Swap I/Q, shift a channel (`SwapIQ`, `ShiftQ`); I/Q balance ("I/Q balancer") | Wiring fixes; image suppression | S | partly | `--mirror`; no balance |
| Frequency calibration ("Frequency Calibration") | Against a known signal | B | missing | |
| CAT through OmniRig ("CAT control") | Reads and sets the radio | B | missing | design feature C |
| Click to tune, next station, PgUp/PgDn, band change, Tune At ("Tuning") | Tuning | B | missing | a row selection marks only |
| Max decoders or adaptive (`MaxDecoders`, `AdaptiveCnt`) | CPU control | S | partly | 400 tracks, budget when behind (§10); not user-set |
| Band plan, CW segments only (`BandPlan`, `CwSegmentOnly`) | Segment marks; skip non-CW | S | missing | |
| Audio of the selected signal at a set pitch (`Pitch`), CW filter 20 to 700 Hz (`FiltBW`), AGC, volume, mute ("DSP Processor") | Listening | B | partly | none live; `kz4ap-listen` writes a clip (A.13) |

### Recorder, settings, other

| CW Skimmer feature (help topic; setting) | What it does | Purpose | Ours | Where ours stands |
|---|---|---|---|---|
| I/Q recorder ("IQ recorder") | WAV named by UTC, no prompt; 24-bit; new file at 2 GB; tags (operator, start, center) | B | partly | asks for a file, 16-bit, stops at 4 GiB; center and time in the name |
| Playback: pause, seek, UTC clock, loop, file info | Replay | B | partly | Pause, Restart, Position, 2x to Max; no clock, loop or info |
| Settings kept (`CwSkimmer.ini`); copies with own files (`ini=`) | Remembered setup | B | missing; partly | no settings store; separate processes |
| Autostart (`autostart`, F9) | Unattended start | B | has | file argument or `--radio` |
| Operator details; third-party tools ("Third party software") | Greeting, tags; RigSync and others | S | missing | |
| Registration, drivers, prerequisites, troubleshooting | | N | n/a | |

Ours has, and CW Skimmer lacks: faster-than-real-time playback, every track's text at once, the named class,
automation (`--report`, `--calls`), Linux builds.

## 2. Callsign identification

### What CW Skimmer documents

1. **Extraction.** An analyzer reads each decoder's text, pulls out calls, and uses keywords such as CQ, DE and
   TEST to decide whether a call is the transmitting station's or its correspondent's, and whether the station is
   running or doing S&P ("Band map").
2. **Markers.** A label gets CQ if the station runs, DE if the call is the transmitting station's. A telnet spot
   ends in CQ if the station runs, DE if it is not running but sends its own call, nothing otherwise, and is posted
   again when the station is later found running ("Telnet commands"). No marker says "named by its correspondent".
3. **What is reported.** A call reaches the telnet clients and the Callsigns window, and is bold, only if it passes
   validation ("Callsign Validation"); the window lists *verified* calls, those appearing twice or more in a
   decoder's text ("Callsign list", "Band map"). Neither asks for a keyword. The help's sample telnet output has 11
   of 19 spots unmarked.
4. **Validation.** Tests: watch list or MASTER.DTA; typical, unusual or suspicious pattern; prefix outside the ITU's
   blocks. Higher levels reject more; the help gives no numbers. Secondhand (RBN, 2021, for CW Skimmer Server): at
   Normal a call needs 1 repeat on the watch list, else 2, 3 or 5 by how common its pattern is, within one
   decoder's 256-character buffer.
5. **`CqOnly`** sends spots only for CQing stations. Its documented scope is the telnet server. That it drops DE spots
   too is my reading of "only for the CQ'ing stations"; that the band map and window keep all calls is inference.
6. **The keyword rule is the RBN's.** The RBN asks each transmission to hold CQ or TEST, and has CW Skimmer Server
   send its aggregator all spots, "not just CQ spots" (RBN, 2021): the filter sits downstream (secondhand).

### Does CW Skimmer count a call without CQ, DE or TEST as heard?

**Yes.** Documented: reporting needs validation and repeats, not a keyword (points 3 and 4); only the optional
`CqOnly` withholds unmarked spots. Measured on CW Skimmer 2.1's replays, validation Normal, `CqOnly` off (every
run's `SKIMMER/SETT` reads `vlNormal`, no CQ filter): telnet logs `build/cwskimmer-replay/out*/*/spots.txt`
(git-ignored), classed by the marker before the UTC field; a call is the spot's word as sent.

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

**What an unmarked spot is.** Only that the analyzer did not find the station running or signing. That covers a
caller sending his call alone, the norm in pileups and contests, and a call read in its correspondent's text; the
spot cannot tell which. Measured hints, not proof:

- On the seven 2026-11-17 recordings our app's text of the same band (`build/recordings/spare/*-report.txt`,
  git-ignored: rows kept at the end, up to about 4 000 symbols each), read with A.12's role words by a scratch
  script, holds 5 of CW Skimmer's 287 never-marked calls only named within 0.15 kHz of the spot (K4UX on 15 m, our
  "K4UX DE K1JD"), 87 bare there and 16 signed. Both sides of a QSO on one frequency share a decoder, so even K4UX
  is not proof.
- On N2GZ, 40 of 163 unmarked spots lie more than 0.3 kHz from every CQ or DE spot of that call: read away from
  where the station signs, by a correspondent's decoder or after a move.

My inference: CW Skimmer does not withhold a call read only from its correspondent; it counts repeats in a decoder's
text, whoever sent them.

### What KZ4AP Skimmer does now

`CallSearch` (§11, A.12) gives each reading a role from the words around it: *signed* (after DE, CQ, TEST, QRZ,
POTA, SOTA, DX or UP, or before K, KN, AR, SK, TEST, QRZ, CQ or UP), *named* (before DE, 5NN, 599, UR, GM, TNX and
the like: another station sending it), else *bare*. A call is reported at 2 points (a whole word is 1) from readings
anywhere in the band within 30 min, whatever the role; a listed call at 1 point if framed (signed, named or sent
twice). "How" is signed if any reading signed, else bare if the station itself was read, else named (README).

So ours, like CW Skimmer, lists calls with no keyword. "How" plays the markers' part, finer in one way (named apart
from bare), coarser in another (CQ and DE lumped; no running or S&P). Nothing shows signed calls only. The owner's
proposal is such an option: `CqOnly` keeping DE and TEST, the RBN's rule. By the table it drops the 59% of calls CW
Skimmer never marks, in a contest mostly S&P callers, who send their call bare (inference).

On the seven 2026-11-17 recordings our live call lists (`build/recordings/spare/*-calls.csv`, git-ignored) class
CW Skimmer's 287 never-marked calls as 31 signed, 54 bare, 3 named, and lack 199 (measured; CW Skimmer's
misreadings among them).

For the plain decoder none of this applies. The known-call list is optional (`--known-calls`), but the call stage
always runs and its list is always shown.

## 3. Gaps, as candidates (the coordinating session decides)

**Skimmer**, most important first:

1. Telnet spot server: spots with SNR, WPM and a CQ or DE marker, `SH/DX`, a password, and a role filter as an
   option (CQ only as `CqOnly`, or signed only, the owner's proposal).
2. Calls drawn on the waterfall at their frequencies (CW Skimmer's band map), not track numbers.
3. Tuning: CAT through OmniRig (Hamlib on Linux), click a signal or call to tune, next or previous station.
4. Call aging (10 min) and spot updates on a change of role; running told from S&P.
5. Validation choices: levels, a watch list apart from MASTER.SCP, MASTER.DTA read, a pattern table.
6. QRL? and 599 labels, band plan and CW-segment-only decoding, SNR in the call list, spectrum over UDP.

**Plain decoder**, most important first:

1. Receiver-audio input by sound card (CW Skimmer's 3-kHz mode; feature G, backlog §3): without it the plain
   decoder needs an SDR.
2. A decoder-only mode: no call stage, call list or master list (the owner's requirement).
3. A one-signal text pane, picked by clicking the waterfall, with doubtful characters shaded.
4. Audio of the selected signal with a CW filter and AGC (feature C), for an SDR input.
5. Settings kept between runs; a choice of sound devices.
