# Backlog

Work deliberately deferred from milestone 1, so it isn't forgotten. Each item
says what's wrong, why it was deferred, and how to measure a fix. The design
spec (`docs/design/`) remains the authority; this is a to-do list.

## Top priority: research, then implement a probabilistic decoder

The classical decoder is a hard-decision baseline (see
`docs/signal-processing.md`, section 8): it keys each sample against
thresholds, then times and classifies elements. It does not yet do what the
design spec asks, find the most probable character sequence given timing
statistics and a prior over likely text. Replace it with a probabilistic
decoder: an HMM with Viterbi decoding, or a beam search, over key states and
characters, with likelihoods computed from the measured signal and noise
levels and an optional text/callsign prior, and no hard thresholds.

Research survey: `docs/research/decoder-survey.md` (notes under
`docs/research/research_notes/`). Its ranked recommendation, in order of
evidence per CPU cycle, is:
1. a pre-detection filter matched to the dit, with soft (likelihood) output,
   on the existing baseline; this is also stage 2 of "Channel filtering, two
   stages" below.
   - **Prerequisite: precise frequency re-centering.** A dit-matched filter
     is only about 1/T wide (T the dit duration, s), so a station off
     center by Δf (Hz) loses a factor |sinc(Δf·T)| in amplitude. The
     channelizer's bin rounding alone leaves up to ±11.7 Hz, which costs up
     to 5.1 dB at 25 WPM and 8.8 dB at 20 WPM (signal power, relative to a
     centered station), half or more of the filter's expected gain. Each
     channel must track drift and be re-centered on a fine frequency
     estimate (for example to ±2 Hz, a 0.2 dB loss at 20 WPM) before any
     narrow filter ("Track frequency drift" below;
     `docs/research/proakis-ook-notes.md`, item 15).
   - **Soft likelihoods are correlated.** The log-likelihood ratio
     −a²/2 + ln I₀(a·x) (x the envelope and a the key-down amplitude, both
     normalized by the noise RMS per real component) is exact for one
     matched-filter output per element. Consecutive samples of a
     narrow-filtered envelope at 1500 samples/s are strongly correlated, so
     summing per-sample values overcounts the evidence. Scale them, or
     decimate to about one sample per 1/B (B the filter's noise bandwidth,
     Hz), before a sequence decoder sums them (proakis-ook-notes.md,
     item 13).
2. a small streaming CNN+LSTM network trained with CTC, following VE3NEA's
   DeepCW;
3. a Bell-style explicit-duration HMM with beam search and Kalman amplitude
   tracking. Start from Bell 1977's verified design
   (`docs/research/bell-1977-notes.md`, sections 2.2 and 4):
   - speed states 10–60 WPM (integers), changing only at element boundaries,
     in steps of ±2/±4 WPM after marks and element spaces, ±5/±10 after word
     spaces and ±10/±20 after pauses;
   - element durations normalized by the dit length at the hypothesized
     speed, with a Laplacian (two-sided exponential) density about the
     nominal 1, 3, 7 or 14 dits, applied as a hazard (transition
     probability given the time already spent in the element);
   - pruning: keep the best path for each of the 6 element types, then add
     paths in decreasing probability until they hold 0.9 of the total;
   - at most 25 paths, each extended into at most 30 successors (6 element
     types × 5 speed steps) per sample;
   - characters released at the common ancestor of all paths, forced at a
     1 s decision delay;
   - one scalar amplitude Kalman filter per path; estimated cost about
     2–3 Mflop/s per channel at about 200 samples/s.
   Change from Bell: feed it option 1's matched-filter likelihoods instead
   of his 100 Hz filter's envelope, work in log probabilities, and add an
   interference/impulse state (his field failures came from interference);
4. hybrids of 2 and 3.
See the survey's "Rank the candidates by evidence per CPU cycle" for the
evidence and costs. Verification of the survey's sources against Bell 1977
and Proakis & Salehi, and then against a second batch of six documents
(Gold 1959, the IEEE 2023 LSTM-CTC paper, AG1LE's 2012 eHam article, Wang
et al. 2018, YFDM 2023 and G4ILO's 2012 blog post), left the ranking
unchanged: none of the six is a decoder benchmark comparable to the ones
the ranking rests on. Notes: `docs/research/gold-1959-notes.md`,
`neural-papers-notes.md`, `wang-yfdm-notes.md`, `g4ilo-2012-notes.md`.
A third check, of morseformer (`docs/research/morseformer-notes.md`) and
three GitHub repositories (`docs/research/github-repos-notes.md`), also
left the ranking unchanged. morseformer has ready weights but costs about
100× option 2 per channel, does not stream, was trained on 16–28 WPM only,
and scores 28.6% CER on its one held-out real operator (the 17.75%
headline includes an operator it was fine-tuned on). It becomes a
reference decoder ("Integrate morseformer as a reference decoder" below),
not a ranked option. The three repositories publish no CER results.

The one paper from the survey still unread, the CNKI *Radio Engineering*
(无线电工程) 3D-CNN + bidirectional ConvLSTM paper, was deliberately
skipped by the owner (owner's decision, 2026-09-27); it is not on the
to-do list.

Optional, later: once the software suite works, offer several classical
decoders as a user choice.

## Next milestone: decoder robustness

Start by adding benchmark scenarios that expose each problem below, then fix
against the numbers. Several of these were found by review but are invisible
to the current benchmark, which only has clean, well-separated signals of
10–30 dB SNR (500 Hz, key-down).

### Benchmark scenarios to add first

- **Strong signals:** SNR up to 60 dB (500 Hz) (the generator stops at
  30 dB).
- **Stations that stop and pause:** a transmission, then several seconds of
  silence, then another — as between CQs.
- **Stations present from the first sample** of a recording.
- **Tune-up carriers:** an unkeyed carrier of 0.3–2 s before keying starts.
- **Crowded bands:** make the generator's minimum station spacing a setting
  (today it is fixed at 1 kHz) that can go down to zero, so stations 50–200 Hz
  apart and overlapping stations can be tested.
- **Speed range:** 10–60 WPM, including very different speeds side by side.
- **Scoring of the first word** of each transmission, since that is where
  wrong characters currently concentrate.

### Integrate morseformer as a reference decoder

Plan and evidence: `docs/research/morseformer-notes.md` (sections 8–9).
morseformer (sderhy, v0.6.4) is a 4.13M-parameter bidirectional Conformer
with an RNN-T head, Apache-2.0 for code and weights. It is the only neural
Morse decoder with ready weights and some real-audio evidence, so it gives
the benchmark a neural baseline before option 2 is trained. **It does not
replace option 2 and is not scheduled ahead of it.** It comes after the
benchmark scenarios above, because its value is its scores on them.

Two stages:
1. **Now: reference decoder in the evaluation harness.** Score it on the
   survey's benchmark categories A–I (`decoder-survey.md`, last table),
   especially H (real recordings).
2. **Later, only if the benchmark shows it beats option 2 on the stations
   where it would run:** an opt-in, channel-capped "second-opinion"
   decoder for low-confidence stations (survey option 4, arbitration),
   for example at most about 10 stations on a desktop and none on a Pi.

Work items:
- **Runtime:** export the encoder plus CTC head once to ONNX (15.9 MB,
  standard operators, already verified against PyTorch) and run it with
  ONNX Runtime, in a Python benchmark harness for stage 1 or in the C++
  engine for stage 2. Ship the converted `.onnx`, never the PyTorch pickle.
  Stagger each channel's window phase to spread the load over the 2 s hop.
- **Feature front end from the complex channel:** per 6 s window of 9000
  complex samples at 1500 samples/s: a zero-phase 4th-order Butterworth
  low-pass at 100 Hz (run forward and backward over the window), magnitude,
  ln(|y| + 10⁻⁶), a 3-sample box mean to 500 frames/s (3000 frames), then
  per-window zero mean and unit variance. This matched the author's 8 kHz
  audio path with correlation 0.994–0.999 and identical decodes on one
  synthetic message; re-check on the benchmark. No 600 Hz tone needs to be
  synthesized.
- **Decoding loop:** RNN-T greedy decoding (the path the author shipped and
  benchmarked): a hand-coded LSTM(128) prediction step and joint network
  (about 0.22M parameters), up to 5 emissions per encoder frame, with the
  0.6 confidence gate (0.9 for digits). Beam search is optional later. CTC
  greedy decoding is simpler and could take the callsign prior later, but
  needs its own scoring. Sliding window: 6 s, re-decoded every 2 s,
  committing only tokens in each window's central 2 s; `flush` decodes the
  last partial window.
- **Symbol mapping to `DecodedSymbol`:** A–Z, 0–9 and `. , ? / - ! '` pass
  through; the explicit space token becomes a word space; `=` → `<BT>`,
  `+` → `<AR>`. **Prosign limitation:** SK, KN and BK arrive as ordinary
  letter pairs, indistinguishable from the same letters sent with a
  character gap; fusing them needs an unvalidated timing rule. `É` and `À`
  map through their Morse patterns. Times: each emission's encoder frame
  (8 ms resolution) marks roughly the start of a character. Probability:
  the joint softmax value of the emitted token, calibrated on the
  benchmark. There is no speed output, so `wpm` must be estimated from
  token spacing or left at 0.
- **Apache-2.0 obligations:** keep the copyright and attribution (cite the
  GitHub handle `sderhy`; the author's given name differs between the
  README and the model card), include the Apache-2.0 license text with the
  weights and any ported code, and mark modified files as changed. There is
  no NOTICE file to carry. Apache-2.0 code and weights may be combined into
  the GPL-3.0 engine.
- **Effort:** 1–2 developer-weeks for the export script, front end, RNN-T
  greedy loop, windowing, symbol mapping and unit tests against the Python
  reference outputs; another 1–2 weeks to score it on categories A–I.

Risks, most serious first:
1. **CPU:** about 2.1 GMAC per channel-second, roughly 100× option 2;
   measured 7–20 channels per performance core (ONNX Runtime, fp32, one
   thread, i7-12700H), and an estimated 8–20 channels per Raspberry Pi 5.
2. **Speed range:** trained on 16–28 WPM only; contest speeds above about
   30 WPM are out of distribution.
3. **Latency:** up to 4 s, fixed 6 s window; the model is not robust to
   other window lengths.
4. **Weak real-audio evidence:** 28.6% CER on one held-out operator, an
   unpublished corpus, no SNR curve for the shipped checkpoint, and an SNR
   definition (white noise over 0–4000 Hz, duty-dependent signal estimate)
   that converts to S₅₀₀ (key-on, 500 Hz) only within a band about 10 dB
   wide.
5. **False characters on noise:** about 1 per 6 s of pure noise in the
   author's gate test, multiplied across channels.
6. **Prosign ambiguity** (SK/KN/BK as letter pairs).
7. **Single maintainer, dormant since June 2026;** the shipped file is a
   mid-training checkpoint.

**Optional baseline: pd0wm/nn-morse** (MIT; `docs/research/github-repos-notes.md`).
A Dense×4 → LSTM(256) → CTC network with no convolution, about 740,000
parameters and about 37 million MAC/s per channel. Retrained on the
project generator (down to S₅₀₀ ≤ 0 dB, key-on, 500 Hz, and with fading),
it would be a cheap no-convolution ablation of option 2. Its shipped weights
never saw anything below S₅₀₀ = +1 dB and are useful only as a smoke test.
Low priority.

### Wrong or missing first characters

A station's channel opens only after the detector has seen it persist (about
0.5 s, plus a 1 s warm-up at the start of a recording), so the decoder starts
mid-transmission, often mid-character, and emits a confident wrong symbol
(e.g. `M6Z` for `W6Z`, `RQ` for `CQ`). These produce busted callsigns.

Fix, in two parts:
1. **Replay:** keep the last ~2 s of signal; when a track is born, feed its
   channel from that buffer first so the decoder hears the transmission from
   its start. Decoding runs ~39× real time, so catching up is instant.
2. **Second pass:** once a station's speed and timing estimates have settled,
   re-decode its first characters. For real-time display, show text as it
   arrives and correct it when the better decode lands.

Must land before callsign matching.

### Speed estimate derailed by a short tune-up carrier

An unkeyed carrier of about 0.5–0.95 s pins the classical decoder's speed
estimate at 5 WPM and garbles what follows (carriers of 1 s or more are
already excluded). Milestone 1's original estimator fails the same way, so it
is a limitation of the cluster-based design, not a regression. Candidate
fixes: estimate speed from the mark-plus-space period, or wait for a minimum
number of marks before trusting an update. Related to the second pass above.

### Stray E's after a station stops

After a station stops, a few `E`s can be decoded from noise before its track
is dropped (6 of 26 cases in review). The decoder's noise test uses wideband
noise, but the engine delivers noise narrowed by the channel filter, whose
envelope fluctuates more. Tune the squelch against channel-filtered noise and
add an engine-level test: signal, then 10 s of noise, then no text after the
last real character. Tune together with the next item.

### Tracks outlive their stations; separate station identity from decoding

A track is dropped about 8 s after its station stops even with a 1 s timeout,
because the detector's one-second power average takes that long to decay.
The longer a track lingers, the more noise it can decode. At the same time,
pauses longer than about 16 s (average decay plus the 10 s default timeout)
end the track, so a station that resumes gets a new track and a decoder that
has forgotten its speed. Both lifetimes are accidents of the current
parameters.

Separate the two: a station's identity (track) should survive long pauses,
a minute or more, and re-attach when it resumes on the same frequency, while
decoding gates off within a second or two of silence.

### Ghost tracks beside very strong signals

Around 60 dB SNR (500 Hz), extra tracks appear a few hundred Hz either side of a
station and decode runs of `E` and `I`. Fix idea: reject a peak that sits
inside a much stronger track's skirt (more than X dB below that track's level within
±N Hz).

### Express the detector's bin-counted settings in Hz

The detector's neighborhoods are counted in bins: minimum station separation
3 bins (70 Hz), peak neighborhood ±2 bins (±47 Hz), track level ±1 bin
(±23 Hz). Changing the bin width silently changes them too. Restate them in
Hz (and convert to bins from the actual bin width) *before* sweeping bin
width, so bin width is the only variable.

### Choose the FFT bin width and channel filter by measurement

Both are guesses. Bins are ~23 Hz (the FFT size is now chosen from the sample
rate to keep that width), and every channel uses a ±150 Hz filter sized for
fast code. Sweep bin width (e.g. 12, 23, 47 Hz) and channel bandwidth against
the scenarios above and pick values by results. Prerequisite: the detector's
settings expressed in Hz (previous item).

### Channel filtering, two stages

The best decoding bandwidth depends on speed: roughly ±30 Hz is enough at
15 WPM (an estimate), while 50 WPM needs ±100–150 Hz; the fixed ±150 Hz
gives slow stations about 6 dB more noise than they need. Split the
filtering in two:
1. The shared FFT channelizer stays fixed and wide enough for the fastest
   code (±150 Hz, or a user setting), with a fixed decimation sized for that
   widest filter.
2. An optional per-station narrow filter at 1500 samples/s, *before*
   magnitude detection, chosen from the station's estimated speed (possibly
   a filter matched to the dit). Combine with the replay so buffered signal
   is re-decoded through the narrower filter. **Prerequisite:** precise
   frequency re-centering of each channel ("Track frequency drift" below).
   A filter about 1/T wide loses up to 5.1 dB at 25 WPM and 8.8 dB at
   20 WPM (signal power, relative to a centered station) to today's
   ±11.7 Hz bin-rounding offset.

Should help weak, slow signals and crowded bands most.

### Detector: averaging, noise floor and detection theory

All of the detector's parameters are heuristics (see
`docs/signal-processing.md`, section 6).
- **Averaging:** compare a boxcar mean over T = 1–2 s with the current
  exponential average (τ = 1 s): false tracks, detection delay, time to drop
  a track. Derive T from the false-alarm rate across N bins, an averaging time
  covering several characters at the slowest speed of interest, and latency.
- **Noise floor:** compare a local floor (median or a low percentile over
  ±2–5 kHz) and minimum statistics (Martin's method) against today's global
  median, using real SDRplay recordings (non-flat passband, DC spike, band
  edges).
- **Detection-theory analysis** of the parameters: the threshold from the
  false-alarm and detection probabilities; neighborhoods from the window's
  main lobe plus the keying bandwidth at the fastest speed; persistence set
  jointly with T.

### Track frequency drift

A track's frequency is fixed at birth; drift is tolerated only passively
(about ±35 Hz before the track's level drops noticeably). Re-center tracks on
their peak each frame, move the channel with them, and update the reported
frequency.

This is a prerequisite of the pre-detection matched filter (top-priority
item 1 and "Channel filtering, two stages"), and for that it must be finer
than whole bins. The channel must be shifted by the fractional offset too,
so the station sits within a small fraction of 1/T of 0 Hz (for example
±2 Hz), with drift tracked continuously, before any narrow filter. The
wide ±150 Hz channel filter does not need this; a dit-matched one does
(`docs/research/proakis-ook-notes.md`, section 2.7 and item 15).

### Speed window

The decoder estimates speed from the last 24 marks (a heuristic; about 4 s at
25 WPM). Evaluate other window sizes and a window measured in time.

### max_tracks

`DetectorConfig::max_tracks` = 200 is a guess at a CPU guard, not exposed to
users. Measure throughput against the number of tracks (on this PC and on a
Raspberry Pi), set the default with margin, and expose it in the app.

### Spectrum frames in linear power

Spectrum frames are stored in dBFS and the detector converts every bin back
to linear power to average it. Store linear power (FS²) and convert to dB
only where it is needed: locally for peak interpolation, and in the display.

### SNR calibration against CW Skimmer / the RBN

The detector reports SNR per bin (35 Hz), the generator in 500 Hz. The
definition is now known (`docs/research/decoder-survey.md`, "One SNR
yardstick"): CW Skimmer reports key-down signal power, measured through a
50 Hz filter with key-up and transition samples discarded and a Rayleigh
fading model, divided by the noise density × a nominal rectangular 500 Hz,
with the density estimated from the flat part of the whole receiver span.
For clean, non-fading signals that is the same quantity as our synthetic
SNR. What remains is calibration: how each side *estimates* signal and noise
from real audio (CW Skimmer's band-wide floor reads high in crowded
segments; our median floor and per-bin averages differ again). Report an SNR
in that convention from the engine, then check the offset against
simultaneous recordings and RBN spots.

## Smaller items worth keeping

- **Engine events:** the "station gone" event carries the track as it was when
  found, so its SNR and last-active time are stale; carry the final values.
  Tracks still alive at the end of a recording get no "gone" event.
- **Timestamps:** document the time bases — detection times lag the true start
  by up to ~43 ms, and decoded symbol times include ~10.7 ms of filter delay.
- **Benchmark matching** of labeled signals to tracks is greedy in file order;
  fine at 1 kHz spacing, but needs a proper assignment once crowded scenarios
  exist.
- **Determinism check** in CI runs the same block size twice; add a bench
  option to vary the block size and compare.
- **Test gaps** noted in review: detector hysteresis band and equal-power peak
  ties; channelizer band-edge and adjacent-channel tests use exact-bin tones;
  a direct test that a track's final text precedes its "gone" event (needs a
  way to inject a test decoder).
- **Input checks:** reject infinite values in decoder settings; generator
  arguments (zero or negative counts and durations).
