# KZ4AP Skimmer — Design

- **Author:** Kenton Randolph Brown, KZ4AP
- **Date:** 2026-09-25
- **Status:** Draft for review

## 1. Purpose

KZ4AP Skimmer is an open-source alternative to Afreet Software's CW Skimmer
and CW Skimmer Server. Those programs decode many Morse (CW) signals at
once from a software-defined radio (SDR), pull the callsigns out of what they
hear, and report them as "spots" to the operator, to logging programs, and to
the Reverse Beacon Network (RBN).

The project serves four goals, all of which matter:

1. **Own it.** A skimmer that can be run, read, and fixed by anyone, on more
   than one operating system.
2. **Feed the Reverse Beacon Network.** Eventually run unattended as a
   headless skimmer server that uploads spots.
3. **Push decoding forward.** Use modern machine-learning methods alongside
   classical signal processing, and measure which works better.
4. **Community.** Something other hams will adopt, contribute to, and learn
   from.

## 2. Decisions

| Topic | Decision | Why |
|---|---|---|
| First release | A native desktop app with a user interface | The operator-facing skimmer is what the author wants to use first. |
| Platforms | Windows and Linux (macOS likely follows cheaply) | Cross-platform from day one is far cheaper than porting later. |
| Language and GUI | C++20 with Qt 6 | The most proven cross-platform path for radio desktop apps (e.g. gqrx). |
| Engine / GUI split | The engine is a C++ library with **no Qt dependency** | Makes the later headless server a small step instead of a rewrite. |
| License | GPL-3.0, plus an additional permission (linking exception) for closed-source SDR driver libraries | Keeps the project and its forks open, matches ham-SDR norms, and explicitly permits use of the proprietary SDRplay API. |
| Radio input, first release | SDRplay via SDRplay API 3.x, and playback of I/Q recording files | The author's hardware; recording playback makes decoding testable and lets people try the app without an SDRplay. |
| Decoder | Three user-selectable modes: Classical, Neural, Hybrid (see §5) | Lets users trade CPU for accuracy, and lets the benchmark decide what works. |
| Names | Display name "KZ4AP Skimmer"; GitHub repository `KZ4AP-Skimmer`; program file name `kz4ap-skimmer` | Identifiers cannot contain spaces; lowercase-hyphenated is the Linux convention for program names. |
| Hosting | GitHub, repository owned by the PhysicsCowboy account | Free automated builds on Windows and Linux (§8); personal ownership. |
| Research on CW Skimmer | Only where needed to interoperate: the telnet spot line format and the standard data files (`MASTER.SCP`, `cty.dat`) | We are not cloning CW Skimmer screen-for-screen; a full functional spec of it would cost weeks and describe behavior we may not copy. |

Defaults recorded without debate (open to change): CMake as the build
system; ONNX Runtime to run the neural model inside the engine; PyTorch for
training; GoogleTest for C++ tests; pytest for Python tests.

## 3. Scope

### First release

- **A. Core skimming.** Waterfall over the full SDR span (default 192 kHz,
  configurable); many decoders running at once; decoded text for the
  selected signal.
- **B. Callsign extraction.** Callsigns pulled from decoded text and
  validated (rules still open, see §6); spot list and band map.
- **D. Telnet spot server.** Spots served in DX-cluster format so logging and
  contest programs can use them.

### Next release

- **C. Listening and tuning.** Audio of the selected signal through the local
  sound card, and click-to-tune via radio control (OmniRig on Windows,
  Hamlib on Linux).

### Later, optional

- **E.** Recording I/Q to file from inside the app (playback is already in
  the first release).
- **F.** Uploading spots to the Reverse Beacon Network.
- A headless server build of the engine (enabled by the engine/GUI split).
- More radio sources (SoapySDR, sound-card I/Q, others) behind the same
  source interface.

### Not planned

- A browser-based user interface. It was considered and rejected in favor of
  a native app; the engine/GUI split keeps it possible later.

## 4. Architecture

```
 SDRplay / I/Q file ──► Source ──► Channelizer ──┬──► Spectrum ──────────────► Waterfall (Qt)
                                                 │
                                                 └──► Signal detector
                                                         │ one channel per CW signal
                                                         ▼
                                                     Decoders
                                         (Classical | Neural | Hybrid)
                                                         │
                                                         ▼
                                                 Callsign search  ◄── callsign data (§6)
                                                         │
                                                         ▼
                                                    Event bus ──► Spot list / band map (Qt)
                                                              ──► Telnet server
                                                              ──► Benchmark scorer
```

The repository has four parts:

| Directory | What it is | Depends on |
|---|---|---|
| `engine/` | C++ library: source, channelizer, signal detector, decoders, callsign search, event bus, telnet server | ONNX Runtime; SDRplay API loaded at runtime only |
| `app/` | Qt desktop app; displays and configures what the engine does | `engine/`, Qt 6 |
| `bench/` | Command-line tool that runs the engine over I/Q recordings and scores the spots against known answers | `engine/` |
| `training/` | Python: synthetic Morse generator, neural-decoder training, export to ONNX | PyTorch; never part of the app build |

The app ships only the exported model file, not the training code.

### 4.1 Data flow inside the engine

1. **Source.** The SDRplay API delivers I/Q sample blocks on its own thread.
   The source copies them into a lock-free ring buffer and returns at once,
   so the driver is never blocked. The file source feeds the same buffer,
   either at real-time pace or as fast as possible (benchmark mode).
2. **Channelizer** (one DSP thread). A sliding FFT over the sample stream
   produces (a) spectrum frames for the waterfall and (b) a narrow baseband
   stream for each tracked signal, by fast convolution.
3. **Signal detector.** Watches the spectrum for CW carriers and maintains a
   list of **tracks** (frequency, SNR, start time). A track is born when a
   signal persists and dies when it goes quiet, with hysteresis. The number
   of tracks is capped (configurable).
4. **Decoders** (worker thread pool). Each track's stream goes to the decoder
   for the selected mode. Every decoder returns the same **decode result**:
   text with a probability for each character, element timing, speed, and an
   overall confidence. (No pitch: the track already knows the signal's
   frequency.)
5. **Callsign search.** Keeps a running text buffer per track, extracts
   callsign candidates, scores them against callsign data and message
   patterns, and emits a spot only when the evidence clears a threshold. The
   same call on the same frequency is not re-spotted within a set time.
   Its matching rules are an open design topic (§6).
6. **Event bus.** Publishes three kinds of event: **spectrum frames**,
   **decoded text**, and **spots**, plus **status events** (§7).
   Subscribers are the Qt app (via Qt's cross-thread signals), the telnet
   server, and the benchmark scorer. The engine does not know who is
   listening.

**Determinism requirement.** In benchmark mode, the same recording with the
same settings must always produce the same spots; otherwise score
comparisons are meaningless. Benchmark mode runs threads in a fixed order and
seeds any randomness.

## 5. Decoders

All decoders sit behind one decoder interface and return the decode result
described in §4.1. The user chooses the mode in the app.

| Mode | How it works | CPU cost |
|---|---|---|
| **Classical** | Statistical decoding: detect the signal's on/off envelope, estimate speed, and find the most probable character sequence given timing statistics and a prior over likely text. Same family as CW Skimmer's Bayesian decoder. | Lowest |
| **Neural** | A small neural network reads a narrow spectrogram strip for each track and outputs characters directly. It is trained with CTC (connectionist temporal classification), the standard speech-recognition technique for learning to output text from audio without hand-aligned labels. | Highest |
| **Hybrid** | A cascade: Classical decodes every track; only results whose confidence falls below a threshold are re-decoded by the neural network. | Between the two, depending on how many tracks are handed over |

**Training data** for the neural decoder is generated synthetically in
unlimited quantity: Morse with varied speeds, sloppy "fist" timing, fading
(QSB), noise, and interfering signals. The model is trained in Python and
exported to ONNX; the engine runs it with ONNX Runtime.

**Hybrid depends on trustworthy confidence.** If the Classical decoder is
confidently wrong, the network never sees the signal. The benchmark measures
how well Classical confidence predicts correctness.

**Considered and not shipped (benchmark experiments only):**
- *Neural rescorer:* Classical proposes its top readings and the network
  picks or corrects one. Costs nearly as much CPU as Neural, and cannot
  recover a reading Classical never proposed.
- *Conditioned neural decoder:* the network also receives the Classical
  decoder's speed estimate, timings, and candidate text. Costs more than
  Neural alone; its one plausible advantage is long-range timing context
  across a whole transmission.

Either can enter the app only if the benchmark shows a real gain over
Neural alone.

## 6. Callsign matching — open design topic

This step turns decoded text (e.g. `CQ CQ DE K1ABC K1ABC K`) into spots and
is the main defense against busted callsigns. It will be designed together
in a later session. The candidates on the table:

**Callsign references**
- Callsign structure rules (prefix, digit, suffix; `/P`, `/M`, `/QRP`,
  `DL/K1ABC`). No data file needed.
- `cty.dat` from country-files.com: prefix → DXCC entity, zones, continent.
- `MASTER.SCP` from supercheckpartial.com: tens of thousands of calls active
  in contests.
- Calls recently spotted by this skimmer or by the Reverse Beacon Network.
- National licensing databases (e.g. the FCC's US list); probably later, if
  ever.

**Message patterns**
- CQ indicators: `CQ`, `CQ TEST`, `TEST`, `QRZ`, and in contests a bare
  callsign repeated between contacts.
- Structure: `DE`, end-of-message signals (`K`, `KN`, `BK`, `SK`), `5NN`,
  `TU`.
- Beacons: the NCDXF beacon network, `VVV DE …`, repeated IDs.

**Undecided:** how strictly `MASTER.SCP` is applied (soft prior, hard
filter, or not at all); which other references are used; how the station
calling CQ is told apart from stations answering it; spot types (CQ, DX,
BEACON); and the evidence thresholds for spotting. The rest of the design
depends only on callsign search existing as a step, not on these answers.

## 7. Error handling

The engine never crashes the app. Failures inside the engine become
**status events** on the event bus; the app shows them as a banner and a
status-bar indicator, and the telnet server logs them.

- **SDRplay problems** (service not running, device unplugged, ADC overload):
  overload is reported and acknowledged back to the API as it requires; on
  device loss the engine retries with increasing delay, keeping existing
  spots.
- **Falling behind real time:** on ring-buffer overflow, drop samples and
  **mark the gap** so decoders reset cleanly instead of producing garbage.
  If the backlog persists, shed load: first raise the Hybrid hand-over
  threshold, then drop the weakest tracks. Report overload and CPU use.
  Benchmark mode never drops samples; the file source waits instead.
- **Data files:** a missing or malformed callsign data file produces a
  warning and the engine runs without it, spotting less confidently. A
  missing or incompatible neural model disables Neural and Hybrid; Classical
  still works.
- **Telnet:** each client has a bounded send queue; a client that falls too
  far behind is disconnected rather than stalling the others. Failure to
  bind the port is a status event, not a fatal error.

New failure cases found during the build follow the same pattern.

## 8. Testing

1. **Unit tests** (GoogleTest) for each engine module in isolation, e.g.
   a pure tone lands in the right channelizer bin; a keyed signal creates and
   ends a track; Morse timing rules; telnet spot-line formatting.
2. **Benchmark as the acceptance test.** `bench/` runs the whole engine over
   labeled recordings and reports:
   - **Callsign recall:** share of callsigns present that were spotted.
   - **Busted-spot rate:** share of spots with the wrong callsign (the
     number users care about most).
   - **Character error rate** of decoded text.
   - **Time to spot** after a station starts sending.
   - **Real-time factor and CPU per signal**, for each decoder mode.
   - **Classical confidence calibration** (§5), which the Hybrid mode
     depends on.
3. **Benchmark recordings**, in two tiers:
   - **Synthetic**, graded by difficulty (SNR, speed, fist, fading, crowding,
     interference), each generated with known answers.
   - **Real** recordings from the author's SDRplay, labeled by hand or
     approximately from Reverse Beacon Network spots.
   192 kHz I/Q is about 2.7 GB per hour, so only short clips go in git.
4. **Automated build on GitHub (continuous integration).** GitHub Actions
   starts fresh Windows and Linux machines on every push, builds the
   project, and runs the unit tests and a small benchmark subset. A change
   fails if the busted-spot rate or recall is worse than the stored
   **baseline** (the scores of the last known-good version), or if two runs
   over the same recording produce different spots.
5. **Hardware and interface testing by hand.** The automated build machines
   have no SDRplay, so SDRplay support is loaded at runtime and everything
   else builds and tests without the SDRplay API installed. Live-radio and
   user-interface testing follow a written checklist before each release.
6. **Training pipeline checks** (pytest): the synthetic generator's labels
   match its audio, and an exported ONNX model produces the same outputs in
   C++ as in PyTorch.

## 9. Open questions

1. Callsign-matching rules (§6).
2. GPL-3.0-only or GPL-3.0-or-later, and the exact wording of the SDR-driver
   linking exception.
3. The SDRplay API's license terms: whether it may be redistributed with the
   app or installed on the automated build machines (§8.5 assumes not).
4. Where the full recording corpus is stored, since it is too large for git.
5. How real recordings get their known answers (hand labeling, RBN spots, or
   both).
6. Whether the Neural decoder can keep up on the CPU with hundreds of tracks
   at once; the benchmark will answer this.
