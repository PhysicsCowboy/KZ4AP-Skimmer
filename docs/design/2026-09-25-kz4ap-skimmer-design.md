# KZ4AP Skimmer — Design

- **Author:** Kenton Randolph Brown, KZ4AP
- **Date:** 2026-09-25
- **Status:** Draft for review
- **Revised 2026-09-27:** owner's decisions on the development order after
  milestone 1 (§3.1), a live single-band operator view first with
  multi-band RBN spotting not a current goal (§2, §3), a receiver-audio
  input (§3.3), and the relationship to manta (§3.2). Also revised
  2026-09-27: §5 rewritten to record the research-backed decoder plan
  (`docs/research/decoder-survey.md`): the development order inside the
  decoder-robustness milestone, morseformer and nn-morse as benchmark-only
  reference decoders, and the rule that the benchmark decides.

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
   headless skimmer server that uploads spots. (Not a current goal; see
   §3 and §3.2.)
3. **Push decoding forward.** Use modern machine-learning methods alongside
   classical signal processing, and measure which works better.
4. **Community.** Something other hams will adopt, contribute to, and learn
   from.

## 2. Decisions

| Topic | Decision | Why |
|---|---|---|
| First release | A native desktop app with a live, single-band, waterfall-style operator view (like CW Skimmer), delivered as a sequence of milestones (§3.1). Multi-band RBN spotting is not a current goal. | The operator-facing skimmer is what the author wants to use first. |
| Development order after milestone 1 | (1) decoder robustness; (2) GUI and live display; (3) receiver-audio input; (4) callsign matching; (5) telnet spot server for local logging programs. RBN upload: maybe later (§3.1) | The owner wants decoded text displayed well before busted-callsign handling. |
| Platforms | Windows and Linux (macOS likely follows cheaply) | Cross-platform from day one is far cheaper than porting later. |
| Language and GUI | C++20 with Qt 6 | The most proven cross-platform path for radio desktop apps (e.g. gqrx). |
| Engine / GUI split | The engine is a C++ library with **no Qt dependency** | Makes the later headless server a small step instead of a rewrite. |
| License | GPL-3.0, plus an additional permission (linking exception) for closed-source SDR driver libraries | Keeps the project and its forks open, matches ham-SDR norms, and explicitly permits use of the proprietary SDRplay API. |
| Radio input, first release | SDRplay via SDRplay API 3.x, and playback of I/Q recording files | The author's hardware; recording playback makes decoding testable and lets people try the app without an SDRplay. |
| Receiver-audio input | Directly after the live display (owner's decision, 2026-09-27): a receiver's audio output into a sound card, decoded through the same detector, channelizer and decoders (§3.3) | Lets the decoder copy whatever the operator is listening to on his own receiver; it needs the live-input plumbing the live display brings. |
| Relationship to manta | Evaluate manta (HagaleTechnologies/manta) and consider integrating parts of it into the decoder and any later RBN server; later decide between building KZ4AP's own RBN server and contributing to manta (§3.2) | manta may serve the same purpose in a more collaborative spirit, and its MIT-or-Apache-2.0 license lets KZ4AP reuse its code (§3.2). |
| Decoder | Three user-selectable modes: Classical, Neural, Hybrid (see §5) | Lets users trade CPU for accuracy, and lets the benchmark decide what works. |
| Names | Display name "KZ4AP Skimmer"; GitHub repository `KZ4AP-Skimmer`; program file name `kz4ap-skimmer` | Identifiers cannot contain spaces; lowercase-hyphenated is the Linux convention for program names. |
| Hosting | GitHub, repository owned by the PhysicsCowboy account | Free automated builds on Windows and Linux (§8); personal ownership. |
| Research on CW Skimmer | Only where needed to interoperate: the telnet spot line format and the standard data files (`MASTER.SCP`, `cty.dat`) | We are not cloning CW Skimmer screen-for-screen; a full functional spec of it would cost weeks and describe behavior we may not copy. |

Defaults recorded without debate (open to change): CMake as the build
system; ONNX Runtime to run the neural model inside the engine; PyTorch for
training; GoogleTest for C++ tests; pytest for Python tests.

## 3. Scope

### First release

The emphasis is a live, single-band, waterfall-style operator view, like CW
Skimmer's; multi-band spotting for the Reverse Beacon Network (RBN) is not a
current goal. The first release is reached through the milestones of §3.1,
in that order; the letters below name features, not their order.

- **A. Core skimming.** Waterfall over the full SDR span (default 192 kHz,
  configurable); many decoders running at once; decoded text for the
  selected signal.
- **G. Receiver-audio input** (§3.3). Comes directly after the live display,
  which provides the live-input plumbing it needs, and before callsign
  matching.
- **B. Callsign extraction.** Callsigns pulled from decoded text and
  validated (rules still open, see §6); spot list and band map.
- **D. Telnet spot server.** Spots served in DX-cluster format so local
  logging and contest programs can use them (not an RBN feed). Under the
  order of §3.1 it now comes after callsign matching, as the last milestone
  of the first release.

### Next release

- **C. Listening and tuning.** Audio of the selected signal through the local
  sound card, and click-to-tune via radio control (OmniRig on Windows,
  Hamlib on Linux).

### Later, optional

- **E.** Recording I/Q to file from inside the app (playback is already in
  the first release).
- **F.** Uploading spots to the Reverse Beacon Network: maybe, later. It may
  instead come through manta (§3.2).
- A headless server build of the engine (enabled by the engine/GUI split).
  Whether KZ4AP builds its own RBN server or contributes to manta is decided
  later (§3.2).
- More radio sources (SoapySDR, sound-card I/Q, others) behind the same
  source interface.

### Not planned

- A browser-based user interface. It was considered and rejected in favor of
  a native app; the engine/GUI split keeps it possible later.

### 3.1 Development order

Milestone 1 (the decoding pipeline and benchmark,
`docs/plans/2026-09-25-milestone-1-decoding-pipeline.md`) is built. After it,
in this order (owner's decision, 2026-09-27):

1. **Decoder robustness**, measured on the benchmark (`docs/backlog.md`):
   the decoder plan and its internal order are in §5.
2. **GUI and live display** (A): live input, waterfall, and decoded text for
   the selected signal.
3. **Receiver-audio input** (G, §3.3), directly after the live display.
4. **Callsign matching** (B, §6). Decoded text is to be displayed well
   before busted-callsign handling is designed.
5. **Telnet spot server** (D), for local logging programs, not the RBN.

RBN upload (F) may follow later; whether it is KZ4AP's own server or a
contribution to manta is decided later (§3.2).

### 3.2 Relationship to manta

manta (github.com/HagaleTechnologies/manta; `docs/research/manta-notes.md`)
is a headless Rust CW skimmer daemon with callsign validation, a DX-cluster
telnet server and an RBN uplink client, but no waterfall or operator view.
The plan (owner's decision, 2026-09-27):

- **Evaluate** manta, and consider integrating parts of it into KZ4AP's
  decoder and into any later RBN server.
- **Later, decide** between building KZ4AP's own RBN server and contributing
  to manta instead, which might serve the same purpose in a more
  collaborative spirit.
- **Owner's idea:** if KZ4AP's decoder outperforms manta's on manta's own
  tests (its real-recording oracle and golden vectors), a merge or a fork
  could produce the RBN tool.

**License facts** (not legal advice). manta is licensed MIT OR Apache-2.0,
at the recipient's option. So:

- KZ4AP (GPL-3.0) may copy or port manta code, keeping its copyright and
  license notices.
- Contributing KZ4AP code upstream to manta would require the owner, as
  copyright holder of his own code, to license that code under MIT or
  Apache-2.0.
- A GPL-3.0 fork of manta is allowed.

### 3.3 Receiver-audio input

A source that takes a ham receiver's audio output into a sound card, so the
decoder can copy whatever the operator is listening to on his own receiver.

- **Signal.** A real-valued signal x(t), sampled at the sound card's rate
  f_a (typically 48 kHz), spanning the receiver's audio passband (typically
  about 300–3000 Hz, depending on the receiver's filter). The engine forms
  the analytic (complex) signal internally, by a Hilbert transform or by
  complex mixing to 0 Hz with low-pass filtering, and treats it as a narrow
  span, so the same detector, channelizer and decoders apply. At a complex
  rate of 48 kHz the FFT-size rule of `docs/signal-processing.md` §2 gives
  N = 2048 and 23.4 Hz bins, and the channel rate stays 1500 samples/s.
- **The receiver shapes what the decoder sees.** Its automatic gain control
  (AGC) changes the gain between and during elements, so key-up and key-down
  levels are not those at the antenna; its filters narrow and color the
  noise, so the noise is not white across the span, and the detector's
  noise floor must be estimated inside the passband, not across the
  otherwise empty bins; and its beat-frequency oscillator (BFO) sets the
  audio tone, so a station's audio frequency is its offset from the dial
  frequency plus the BFO/tone offset (sign set by the sideband). Converting
  to RF frequency needs the dial frequency and sideband.
- **SNR is not comparable.** An SNR measured on receiver audio, after the
  receiver's AGC and filters, is not directly comparable with one measured
  from an SDR I/Q stream, even when both are stated in the same bandwidth
  (e.g. dB SNR in 500 Hz). Reported values must say which input they came
  from.
- **Units.** dBFS on this input is relative to the sound card's full scale,
  which bears no fixed relation to the SDR input's full scale.

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
described in §4.1. The user chooses the mode in the app. The evidence behind
this section is in `docs/research/decoder-survey.md` (section "Rank the
candidates by evidence per CPU cycle"); the work items are in
`docs/backlog.md`, section 1.

Symbols and conventions used below:

- T: dit duration, s; T = 1.2/w for a speed of w WPM (PARIS timing), so
  T = 48 ms at 25 WPM and 60 ms at 20 WPM.
- Δf: offset of a station's carrier from its channel's center, Hz.
- B: noise bandwidth of a filter, Hz.
- S₅₀₀: the project's SNR convention, key-on (key-down) carrier power over
  noise power in a 500 Hz bandwidth, dB. Every SNR in this section and in
  benchmark results uses it unless another reference is named.
- CER: character error rate, the Levenshtein edit distance between decoded
  and reference text divided by the reference length (dimensionless).
- Losses of signal power in dB are relative to the same station exactly
  centered in its channel.

### 5.1 Modes (decided)

| Mode | How it works | CPU cost |
|---|---|---|
| **Classical** | Statistical decoding: soft likelihoods from the front end (§5.2, step 1) feed an explicit-duration hidden Markov model (HMM) that finds the most probable character sequence given timing statistics and a prior over likely text. Same family as CW Skimmer's Bayesian decoder and Bell 1977. | Lowest (estimated 2–3 million floating-point operations per channel-second; not yet measured) |
| **Neural** | A small streaming neural network reads a narrow spectrogram strip for each track and outputs characters directly. It is trained with CTC (connectionist temporal classification), the standard speech-recognition technique for learning to output text from audio without hand-aligned labels. | Highest (estimated 15–20 million multiply-accumulates (MAC) per channel-second; not yet measured) |
| **Hybrid** | A cascade: Classical decodes every track; only results whose confidence falls below a threshold are re-decoded by the neural network. | Between the two, depending on how many tracks are handed over |

**Hybrid depends on trustworthy confidence.** If the Classical decoder is
confidently wrong, the network never sees the signal. The benchmark measures
how well Classical confidence predicts correctness.

The current hard-decision decoder (threshold keying, then element timing;
`docs/signal-processing.md`, section 8) stays in the code as the
**baseline** that every new decoder is measured against.

### 5.2 Development order (decided)

Inside the decoder-robustness milestone (§3.1, item 1), in this order,
following the survey's ranking:

1. **Front end: dit-matched filter and soft likelihoods.** A complex filter
   matched to the current dit estimate (B ≈ 1/T, about 21 Hz at 25 WPM)
   runs *before* envelope detection, and the envelope is turned into a
   log-likelihood ratio from Rician (key down) versus Rayleigh (key up)
   densities.
   - **Prerequisite: precise frequency re-centering** of each channel, with
     drift tracking. Correlating a tone offset by Δf against a template of
     duration T scales its amplitude by |sinc(Δf·T)|. Today's FFT-bin
     rounding leaves Δf up to ±11.7 Hz, which through a filter with
     B ≈ 1/T costs up to 5.1 dB of signal power at 25 WPM and 8.8 dB at
     20 WPM. The target is a small fraction of 1/T (for example ±2 Hz, a
     0.2 dB loss at 20 WPM).
   - **Two stages:** the shared FFT channelizer stays fixed and wide enough
     for the fastest code; the narrow filter is a per-station second stage
     at 1500 samples/s (backlog, "Channel filtering, two stages").
   - **Correlated samples:** successive samples of the narrow-filtered
     envelope are strongly correlated, so per-sample likelihoods must be
     scaled down, or decimated to about one sample per 1/B, before a
     sequence decoder sums them.

   The front end is useful on its own, in front of the baseline, and it is
   the input to the Classical HMM (step 3) and the hybrids (step 4).

   **Amendment (2026-09-30, owner).** The single dit-matched filter whose
   length follows the decoder's own speed estimate (built in milestone 2
   part 1) is replaced by a bank of fixed-length matched filters with a
   loop-free speed estimate (a periodicity estimate plus per-branch duration
   fits) and branch selection by self-consistency, with the decoded text's
   log-probability as a tie-breaker; the engine's text output gains
   corrections reaching back up to 20 s. Design:
   `docs/design/2026-09-30-filter-bank-speed-estimator-design.md`.
2. **Neural: a small streaming CNN+LSTM+CTC network** (a convolutional
   front end, then a long short-term memory recurrent layer, trained with
   CTC) following VE3NEA's DeepCW recipe. DeepCW is MIT-licensed, so the
   project may start from his code, with attribution. The network is
   trained in Python (PyTorch) on the project's own signal generator,
   exported to ONNX, and run in the engine with ONNX Runtime; the app ships
   only the exported model file.
3. **Classical: an explicit-duration (semi-Markov) HMM with beam search,**
   in the style of Bell 1977: Kalman tracking of the key-down amplitude and
   discrete speed states. It starts from Bell 1977's parameters and the
   lessons of manta's `hsmm` decoder (backlog, "Top priority", option 3).
4. **Hybrids,** once both 2 and 3 exist.

**Training data** for the neural decoder is generated synthetically in
unlimited quantity: Morse with varied speeds, sloppy "fist" timing, fading
(QSB), noise, and interfering signals.

### 5.3 Reference decoders (decided: benchmark only)

- **morseformer** (sderhy; Apache-2.0 for code and weights) runs in the
  benchmark as a ready-trained neural reference. It is not a shipped mode
  unless that is decided later on benchmark evidence: it costs about
  2.1 GMAC (10⁹ multiply-accumulates) per channel-second, roughly 100–140×
  the Neural mode's estimate, does not stream, and lags the audio by up to 4 s.
- **nn-morse** (pd0wm; MIT) is an optional no-convolution baseline, useful
  only if retrained on the project's generator.

### 5.4 The benchmark decides

**No decoder replaces the baseline unless it beats the baseline on the
benchmark.** What is decided above is the order of work and the three
user-facing modes; how well each performs, and whether any candidate below
ships at all, is left to the benchmark.

- **Synthetic scenarios** (survey, last table; backlog, "Benchmark
  scenarios to add first"): VE3NEA's fading and keying grid, reproduced as
  an external anchor; speed changes; interference; tuning error; strong
  signals (up to S₅₀₀ = 60 dB); stations that stop and pause; tune-up
  carriers; crowded bands; separate scoring of each transmission's first
  word.
- **Real recordings,** scored with manta's oracle method: for each RBN spot
  by a reference skimmer, decode a window of our own I/Q recording at the
  spotted frequency and time, bypassing the detector, and score whether the
  call appears (backlog, "Real-recording scoring: manta's oracle").
- **Metrics:** CER, with each prosign counted as one symbol and word spaces
  scored separately from characters; S₅₀₀ as generated; and CPU time per
  channel-second on a desktop and on a Raspberry Pi 5.

**Benchmark candidates, not shipped.** Each would need a change to this
spec to enter the app:

- *Neural emissions into the HMM* (the survey's alternative hybrid): the
  network's per-frame key-on probabilities replace the front end's
  likelihoods as the Classical HMM's emission probabilities.
- *Neural rescorer:* Classical proposes its top readings and the network
  picks or corrects one. Costs nearly as much CPU as Neural, and cannot
  recover a reading Classical never proposed.
- *Conditioned neural decoder:* the network also receives the Classical
  decoder's speed estimate, timings, and candidate text. Costs more than
  Neural alone; its one plausible advantage is long-range timing context
  across a whole transmission.

The rescorer and the conditioned decoder can be proposed only if the
benchmark shows a real gain over Neural alone.

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
7. Whether KZ4AP builds its own RBN server or contributes to manta, and
   whether a merge or fork follows (§3.2).
8. *Resolved 2026-09-27:* the receiver-audio input (§3.3) comes directly
   after the live display, before callsign matching (§3.1).
