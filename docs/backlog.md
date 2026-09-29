# Backlog

Work deliberately deferred from milestone 1, so it isn't forgotten. Each item
says what's wrong, why it was deferred, and how to measure a fix. The design
spec (`docs/design/`) remains the authority; this is a to-do list.

Sections follow the development order after milestone 1 (design spec §3.1;
owner's decisions, 2026-09-27): (1) decoder robustness; (2) GUI and live
display; (3) receiver-audio input; (4) callsign matching; (5) telnet spot
server for local logging programs. RBN upload, and whether it is KZ4AP's
own server or a contribution to manta, are decided later.

## 1. Decoder robustness (next milestone)

Start by adding benchmark scenarios that expose each problem below, then fix
against the numbers. Several of these were found by review but are invisible
to the current benchmark, which only has clean, well-separated signals of
10–30 dB SNR (500 Hz, key-down).

### Top priority: research, then implement a probabilistic decoder

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
   interference/impulse state (his field failures came from interference).
   **Lessons from manta's `hsmm` decoder**, a built and tested
   explicit-duration decoder of this kind (`docs/research/manta-notes.md`,
   sections 3–5):
   - it models element durations as **log-normal** (standard deviation of
     ln(duration) 0.22, durations admitted only within 0.6–1.5 × nominal),
     not Laplacian as Bell does; compare the two on benchmark category C
     (poor fists);
   - **one speed per hypothesis:** each token carries its own dit length,
     updated by an exponential average (weight 0.2 per element) within
     8–60 WPM and seeded at 5 speeds (about 12–50 WPM); tokens merge on
     (tree node, phase, quantized dit length);
   - **beam of 12 tokens** (Bell: at most 25 paths);
   - its likelihood is **Gaussian** on an amplitude normalized between the
     noise and mark levels, with a fixed spread (0.30 of the mark-to-noise
     difference), **not Rician**; ours should use option 1's Rician LLR,
     whose spread depends on SNR;
   - it **sums correlated samples without decimation:** per-sample LLRs at
     375 samples/s from a channel of 82.2 Hz noise bandwidth, whose noise
     correlation time is about 1/(82.2 Hz) ≈ 12 ms ≈ 4.6 samples, so its
     evidence is overweighted by a factor of about 4–5 against its duration
     and type priors (the caveat under option 1 above);
   - **results:** on manta's real-recording oracle (see "Real-recording
     scoring: manta's oracle" below), the call appeared as a whole decoded
     word in **56.1%** of 221 windows around K5TR's RBN spots, against
     26.7% for manta's threshold-keyed `legacy` decoder; it still failed its
     own acceptance gate (3 of 9 "real-conditions" synthetic vectors);
   - **CPU:** about 3.5 ms of CPU per track-second (Apple M4 Pro),
     **4.25× over its own budget**, and 3.9× the CPU of `legacy` on the
     real recording. Its anchor-based segmentation, in which cost scales
     with the number of keying edges rather than samples, is worth copying.
   manta is MIT OR Apache-2.0, so its code may be ported with its notices
   kept (design spec §3.2);
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
100–140× option 2 per channel, does not stream, was trained on 16–28 WPM only,
and scores 28.6% CER on its one held-out real operator (the 17.75%
headline includes an operator it was fine-tuned on). It becomes a
reference decoder ("Integrate morseformer as a reference decoder" below),
not a ranked option. The three repositories publish no CER results.
A fourth check, of manta (`docs/research/manta-notes.md`), left the ranking
unchanged too: its `hsmm` decoder is a worked example of option 3, not
evidence that reorders the options (survey, "Rank the candidates").

The one paper from the survey still unread, the CNKI *Radio Engineering*
(无线电工程) 3D-CNN + bidirectional ConvLSTM paper, was deliberately
skipped by the owner (owner's decision, 2026-09-27); it is not on the
to-do list.

Optional, later: once the software suite works, offer several classical
decoders as a user choice.

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

### Real-recording scoring: manta's oracle

A cheap real-signal benchmark that tests the decoder apart from the detector
(`docs/research/manta-notes.md`, sections 4 and 5, item 2). Take a real I/Q
recording made while a reference skimmer was spotting to the RBN. For each
of that skimmer's spots, open a channel at the spotted frequency and decode a
window of the recording around the spot time (manta: 40 s), without the
detector. Score each window by whether the spotted call appears:
- **as_word:** as a whole decoded word;
- **framed:** as a whole word following `CQ`, `DE` or `TEST`;
- **substring:** anywhere in the decoded text.

manta's run: 15 min of 192 kS/s I/Q from the 2025 CQ WW CW contest, 40 m
near 7.080 MHz, 221 windows around spots by the skimmer K5TR. as_word /
framed / substring: `legacy` 26.7 / 13.1 / 48.0%, `hsmm` 56.1 / 31.7 /
58.8%. By the RBN-reported SNR of each spot (< 15, 15–25, ≥ 25 dB; the
reference is presumably CW Skimmer's dB SNR in 500 Hz, key-down, though
manta does not state it), `hsmm` as_word was 24, 58 and 65%. Because K5TR
copied every one of these calls, the score is a recall relative to CW
Skimmer at a different receiver, not an absolute one.

Work items: record our own contest I/Q with the SDRplay while RBN spots are
logged (manta's recording is not redistributable); add an oracle mode to
`bench/` that takes (time, frequency, call) triples; report the three scores
by SNR bucket, with the SNR convention stated. Before any spot-level scoring
against the RBN, adopt manta's critique of it (no time matching, mismatched
counting units, a union of all RBN skimmers taken as truth). This is a
concrete method for benchmark category H (`decoder-survey.md`) and for spec
§8.3's "labeled approximately from RBN spots" (spec §9, open question 5).
Running KZ4AP's decoder on manta's own oracle is also the comparison the
manta decision needs ("Later: RBN upload and the manta decision" below).

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
1. **CPU:** about 2.1 GMAC per channel-second, roughly 100–140× option 2;
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

**Also covers the retune delay after a QSO turnover** (milestone 2, part 1,
option 1; owner, 2026-09-29). When the answering station is on a different
frequency, within 47 Hz, from the one the channel is tuned to, the channel
moves to it only when the detector's 1 s power average says the peak has
moved: 1.3–1.8 s into the answer at 10–25 Hz, 2.3 s at 40 Hz for a station
10 dB weaker than the caller (simulated; 2.6 s after the caller stops,
derived). A station that pauses and resumes on its own frequency loses
nothing. Median characters of the answering station lost at the start of
its over (simulated, 30 runs each; levels re the caller's key-down power):
0 at 10 Hz; at 25, 40 and 50 Hz, 1, 3 and 3 at −10 dB, 0, 1 and 2 at −6 dB,
0 at 0 and +6 dB. Replay would feed the retuned channel its first seconds
again. **Tried and rejected:** a faster 0.2 s spectrum average during the
decoder's re-acquisition window found the answering station within 0.04 s
and lost nothing at 25–40 Hz, but at 50 Hz its quick swings carried the
channel across the 47 Hz boundary (the caller's next over exact in only
15–20 of 30 runs, and a new track for the caller), and in 2 of 60 runs a
channel jumped 21–25 Hz to a noise peak (the short average is noisier:
about 23% spread per bin, against 10% for the 1 s one).

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

This also matters in QSOs (simulated, milestone 2 part 1 plan, 2026-09-29):
when the answering station has its own track (more than 47 Hz away) and
sends a 16 s over, the caller is silent for 18 s and its track (S₅₀₀ =
15 dB) dies before it resumes; its next over goes to a new track that loses
its first word (every run at 200 Hz, and at 100 Hz with the answering
station 6–10 dB weaker than the caller's key-down power). Real overs are
often longer.

### Ghost tracks beside very strong signals

Around 60 dB SNR (500 Hz), extra tracks appear a few hundred Hz either side of a
station and decode runs of `E` and `I`. Fix idea: reject a peak that sits
inside a much stronger track's skirt (more than X dB below that track's level within
±N Hz).

### Express the remaining bin- and sample-counted settings in physical units

Owner's standing rule (2026-09-29): parameters and calculations are in
physical units (Hz, s, FS, dB with a named reference), never in units tied
to an implementation choice (FFT bins, samples, decimation); conversion to
bins or samples happens only at the point of use, from the physical value,
so changing the bin width or a sample rate keeps the physics the same.
The milestone-2 part 1 plan restates the settings it touches: the
detector's peak neighborhood (±47 Hz), track-level neighborhood (±23 Hz)
and candidate step (23 Hz), the new channel distance (47 Hz), and the
frequency tracker's lag (5.33 ms) and update interval (21.3 ms). Still
counted in bins or samples, and not touched by that plan:
- `EngineConfig::fft_size` / the analyzer's N: state the bin width (Hz) or
  the window duration (42.7 ms) instead; `choose_fft_size`'s rule is
  already in Hz (bins of at least 20 Hz).
- The hop, N/2 samples: state it as 21.3 ms (50% overlap).
- `EngineConfig::channel_bins` = 64: state the channel output rate
  (1500 samples/s) or bandwidth instead.
- The channel filter's length, N/2 + 1 taps: state it as 21.3 ms.
- `DetectorConfig::min_separation_bins` = 3 (70 Hz): kept only for the
  milestone-1 attribution rule (`Attribution::Bins`), which stays for
  comparison; if that rule stays, state it in Hz.
- The detector's warm-up and persistence are already in seconds, but the
  averaging is updated per frame (α from the hop): check that a hop change
  keeps τ = 1 s.

Do this before sweeping the bin width ("Choose the FFT bin width and
channel filter by measurement", next item), so bin width is the only
variable.

### Choose the FFT bin width and channel filter by measurement

Both are guesses. Bins are ~23 Hz (the FFT size is now chosen from the sample
rate to keep that width), and every channel uses a ±150 Hz filter sized for
fast code. Sweep bin width (e.g. 12, 23, 47 Hz) and channel bandwidth against
the scenarios above and pick values by results. Prerequisite: the remaining
settings expressed in physical units (previous item).

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
  edges). Include manta's two estimates (`docs/research/manta-notes.md`,
  section 5, items 3–4):
  - its per-station **running minimum** (minimum statistics): the minimum
    over 1.5 s of the channel power smoothed over 40 ms, raised by a
    measured **+2.16 dB** (a power ratio, mean noise power over the
    expected minimum) to remove the minimum's low bias. That correction was
    measured for manta's channel (82.2 Hz noise bandwidth, 375 samples/s)
    and must be re-measured for any other bandwidth, smoothing or window;
  - its detector floor: the 25th percentile of each channel's power over
    10 s, capped at the median of a block of 32 channels + 3 dB (power
    ratio), because a CW duty cycle of 50–60% inflates a median.
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

### Measure where answering stations really are

The synthetic two-station QSOs (milestone 2, part 1, group H) put the
answering station 0–200 Hz from the caller, with offsets drawn toward small
values: |Δf| in 0–10 Hz with probability 0.40, 10–50 Hz 0.30, 50–100 Hz
0.15 and 100–200 Hz 0.15. That distribution is a guess (heuristic):
zero-beating by ear leaves a few to tens of Hz, while a sidetone pitch that
differs from the rig's CW offset, or RIT, leaves 100–200 Hz. Once the
owner's SDRplay recordings exist, measure the real distribution of |Δf|
between a CQ and the stations that answer it, and replace
`ANSWER_OFFSET_BANDS_HZ` in `training/kz4ap_synth/generate.py` with it. It
matters because the pipeline treats a station within the channel distance
D = 47 Hz of a channel's station as the same channel (the detector's track
moves to its peak and the channel follows), and one farther away as a
separate track (milestone 2,
part 1, Design decisions B); the milestone-1 detector heard stations within
about 47 Hz as one track, from about 70 Hz as two, and either in between.

### Tune the Matched front end by measurement

Tuning waits for evidence from the benchmark (owner, 2026-09-27 and
2026-09-29: only with strong evidence; the benchmark exists to find better
values). The Matched front end is the default from milestone 2, part 1
(owner decision 2026-09-29). Its parameters are heuristic
(`docs/signal-processing.md` §7, §8, §8b): the filter length β = 0.8 dit
(sweep 0.6, 0.8, 1.0), the amplitude and noise time constants (0.5 s, 2 s;
the fading group is the test), the squelch a_min = 3·(T_v/16 ms)^(1/4)
(T_v the filter's duration), the keying hysteresis ±1 nat, the noise guards
κ = 1.75 and κ_n = 4, the noise floor's margin (2.5), clean fraction (0.25)
and restart ratio (4), the re-acquisition silence (max(0.5 s, 12 dits)) and
window (2 s), the tracker's fine-tuning range (±12 Hz around the
detector's frequency) and minimum weight (0.6), when the filter starts following the speed (8 marks), the
dit-estimate growth bound (×1.25 per mark, also from the filter's first
follow step), and the channel distance (D = 47 Hz). The amplitude estimate is biased
low by the filter's ramps (0.85–0.88 of the true amplitude for PARIS at
25 WPM, simulated), which lengthens marks by about 7 ms at 25 WPM; measure
whether that matters.

The four limits below were postponed by the owner on 2026-09-29. Each is a
known limitation of the Matched front end as planned.

#### Acquisition floor (postponed)

A station is first heard through a short "acquisition" filter (16 ms, set
for 60 WPM), because the decoder does not know its speed yet. That filter
lets in more noise than the dit-matched filter used later, so a weak
station cannot be caught at all below about S₅₀₀ = −2.5 dB (derived, any
speed); in simulation half its marks were caught near −1.8 dB at 25 WPM and
−2.6 dB at 12 WPM. Once caught and narrowed, it could be followed down to
−4.4 dB (25 WPM) or −6.0 dB (12 WPM), but it has to be caught first.
Options: (1) acquire with a longer filter (costs fast CW and the range of
frequencies it can pull in); (2) key at the acquisition width without the
squelch, but require several consistent marks before trusting them;
(3) squelch on the level the dit-matched filter would have (raises false
keying on noise). The detector has a similar floor of its own, which
matters in the full pipeline (not in oracle mode): its 6 dB threshold per
bin, in the Hann window's 35.2 Hz noise bandwidth, for a keyed station
down 44% of the time, is reached at about S₅₀₀ = 6 − 11.5 + 3.6 ≈ −1.9 dB
(a rough estimate: 10·log₁₀(500/35.2) = 11.5 dB, 10·log₁₀(1/0.44) = 3.6 dB;
not measured, and up to 1.4 dB worse for a station between bins).

#### Noise-rise recovery (postponed; affects accuracy)

If the band noise rises and stays up (a sustained rise of 6 dB, in power),
the front end's noise estimate climbs back slowly: about 43 s (derived;
24–56 s simulated). Until it has caught up, noise can look like signal, and
stray characters can be decoded for up to about 16 s. Brief noise bursts
are not affected (the estimate does not chase them), and a fall in the
noise is followed within a few seconds. A faster climb would need to tell a
real noise rise from a station that fills most of the channel, and every
method tried either did not separate the two or let weak stations inflate
the estimate.

#### Stray noise after a silence (postponed)

After each silence of max(0.5 s, 12 dits) the front end restarts its
amplitude estimate, in case the next over is from another station. In
about 1% of silences in noise alone (4 of 400 simulated), the restarted
estimate latched onto noise and one stray character was decoded. One
option: give the restart a prior weight, as the warm-up has.

#### A strong neighbor 60–70 Hz away leaks through the matched filter (stated limit)

The matched filter is a boxcar (a plain moving average). Its frequency
response has sidelobes: a station 60 Hz from the channel's station comes
through the 25 WPM filter only 18.7 dB down (relative to a centered
station), and one 70 Hz away through the short acquisition filter 19.6 dB
down (derived). If that neighbor is as strong as the channel's station or
stronger, its marks are keyed in fragments in the wrong channel, and those
fragments corrupt the speed estimate. Simulated (milestone 2, part 1,
option 1; 30 runs each; levels re the channel's station's key-down power):
with the neighbor answering 60 Hz away, the first station's next over came
through exactly in 6 of 30 at equal level and 0 of 30 at +6 dB (its last
three words were intact in 26 and 30; typically "FARIS" for "PARIS"); at
70 Hz and +6 dB, 19 of 30, with the filter running away to a slow speed in
6. The neighbor itself was decoded in every run, on its own channel in all but one (at 70 Hz and +6 dB, once by the first station's channel). The fix
belongs in the filter's design, for example a tapered filter with lower
sidelobes; take it up with the co-channel item below (owner, 2026-09-29).

#### Two stations keying at the same time within a few tens of Hz (postponed)

A second station keying at the same time as the channel's station, a few
tens of Hz away, cannot be separated by the Matched front end: both pass
its filter, and the frequency tracker settles on the stronger or between
the two. Simulated (engine level, 30 runs each; levels in dB re the
channel's station's key-down power): 6 dB weaker at 40–60 Hz, the
channel's station decoded in 30 of 30 and the other in 0 (no track of its
own); at equal level, the channel's station decoded in 0 (40 Hz: the
tracker settled between them), 13 (50 Hz) and 28 (60 Hz) of 30; 6 dB
stronger, the channel moved to the other station in 30 of 30. Candidate
mitigations to explore: hold the tracker when its average stops being
coherent (a sign of two tones); choose the filter length so the other
station sits on one of its nulls (at multiples of 1/T_v); estimate each
mark's own frequency and assign marks to stations; run a second tracker
and decoder in the channel for the second tone; a finer detector
spectrum (a longer FFT) to see both peaks; or a probabilistic decoder that
models two stations (top-priority item).

### VE3NEA's pitch error in group B

Add VE3NEA's ±30 Hz pitch-error option to the benchmark's group B only if
we compare with his published curve directly or test his published model
(owner, 2026-09-29).

## 2. GUI and live display

The Qt app with live input: SDRplay source, ring buffer, worker threads and
status events, a waterfall over one band, and decoded text for the selected
signal (design spec §3, feature A; §4.1). This is the owner's first goal: a
live, single-band, waterfall-style operator view like CW Skimmer's. It gets
its own plan. Related item above: "Spectrum frames in linear power".

- **A channel that follows a QSO turnover** (owner, 2026-09-29): with the
  channel distance (milestone 2, part 1), one channel follows both stations
  of a QSO within 47 Hz of each other. When it decodes "DE <call>", its
  displayed callsign should change to that call, and change back when the
  other station's call is decoded. The same rule belongs in callsign
  matching (section 4).

## 3. Receiver-audio input (directly after the live display)

Design spec §3.3. Decode whatever the operator is listening to on his own
receiver, from its audio output through a sound card. It comes directly after
the live display, before callsign matching (owner's decision, 2026-09-27),
because it reuses the live-input plumbing (a sound-card source feeding the
same ring buffer).

- **Analytic signal.** The input is real, sampled at f_a (typically
  48 kHz), with content only in the receiver's audio passband (typically
  about 300–3000 Hz). Form the complex signal by a Hilbert transform or by
  mixing the passband center to 0 Hz and low-pass filtering, then run the
  existing engine on it. At a complex rate of 48 kHz,
  `choose_fft_size` gives N = 2048 and 23.4 Hz bins, and the channel rate
  stays r = 1500 samples/s (`docs/signal-processing.md` §2, §7).
- **Noise floor.** Most of the span lies outside the receiver's filter and
  holds almost no noise, so today's median of all bins would sit far below
  the in-band noise. Restrict the floor to the passband, or mix and
  decimate the span down to it (see "Detector: averaging, noise floor and
  detection theory" above).
- **Receiver effects to test:** AGC (gain changing between and during
  elements, which moves the decoder's mark and space levels), the
  receiver's filter shape (colored noise; stations near the filter edges
  attenuated), and the BFO/tone offset (audio frequency = RF offset from
  the dial frequency plus the tone offset, sign set by the sideband; RF
  frequency needs the dial frequency from radio control).
- **SNR and units.** Label SNRs from this input as receiver-audio values:
  they are not comparable with SNRs from SDR I/Q even in the same bandwidth
  (e.g. dB SNR in 500 Hz). dBFS here is relative to the sound card's full
  scale.
- **Test:** record receiver audio and SDR I/Q of the same stations at the
  same time and compare decodes and SNRs.

## 4. Callsign matching

Needs its design session first (spec §6). Items from milestone 1's review
that must land before it are in section 1 ("Wrong or missing first
characters"). A channel that follows a QSO turnover carries two stations'
calls in turn; relabel it when it decodes "DE <call>" (section 2, owner
2026-09-29).

### Input to the design session: manta's callsign acceptance rules

manta's `manta-spot` crate (`docs/research/manta-notes.md`, section 3, last
rows, and section 5, item 6) is a worked, tested set of rules to compare
against spec §6's candidates:
- parse the context: `CQ`, `DE`, `TEST` and beacon patterns;
- a callsign-grammar prefilter;
- `cty.dat` allocation check: a call whose prefix is not allocated is
  **rejected**;
- `MASTER.SCP` only **raises confidence**, never gates (one answer to
  spec §6's "soft prior, hard filter, or not at all");
- the call must **repeat as a distinct message within 90 s** before its
  first spot (beacons and an operator allowlist are exempt);
- **variant arbitration:** a weaker rival call confusable with a stronger
  one is withheld;
- **dedupe** on (call, frequency bucket); re-spot after 10 min unless the
  SNR improves or the spot type changes.
Cautionary example: the beacon exemption produced 29 garbage spots in one
overnight run. manta bundles `cty.dat`, `MASTER.SCP` and a DXCC table; check
their own terms (its `SOURCES.md`) before reusing them.

## 5. Telnet spot server

Spots in DX-cluster format for local logging and contest programs (spec §3,
feature D). Not an RBN feed. Needs spots, so it follows section 3. manta's
DX-cluster telnet server (port 7300, RBN `DX de` line format) is a reference
for the line format and client handling.

## Later: RBN upload and the manta decision

- **RBN upload** (spec §3, feature F): maybe, later. Not a current goal.
- **Decide: KZ4AP's own RBN server, or contribute to manta** (spec §3.2,
  §9 open question 7). Evaluate manta first and consider integrating parts
  of it into the decoder and any later RBN server. The owner's idea: if
  KZ4AP's decoder outperforms manta's on manta's own tests (its oracle and
  golden vectors), a merge or a fork could produce the RBN tool. License
  facts: manta is MIT OR Apache-2.0, so KZ4AP (GPL-3.0) may copy or port
  from it, keeping its notices; contributing KZ4AP code upstream would
  require the owner to license that code MIT or Apache-2.0; a GPL-3.0 fork
  of manta is allowed. Obstacles noted in manta-notes.md section 6: Rust
  versus C++20, and a different channel format (375 samples/s magnitude
  from 93.75 Hz-spaced channels versus 1500 samples/s complex from a
  ±150 Hz channel).

## Smaller items worth keeping

- **Engine events:** the "station gone" event carries the track as it was when
  found, so its SNR and last-active time are stale; carry the final values.
  Tracks still alive at the end of a recording get no "gone" event.
- **Timestamps:** document the time bases — detection times lag the true start
  by up to ~43 ms, and decoded symbol times include ~10.7 ms of filter delay.
- **Benchmark matching** of labeled signals to tracks is greedy in file order;
  fine at 1 kHz spacing, but needs a proper assignment once crowded scenarios
  exist.
- **Neutral attribution of ambiguous edits in first-word scoring:** the
  bench's alignment traceback charges an ambiguous edit to the earliest
  reference symbol it could belong to (`docs/signal-processing.md` §11),
  which biases first-word and per-transmission CER upward when text repeats
  or a whole word is dropped. Among the alignment's optimal paths, split an
  ambiguous edit's charge (for example half to each candidate symbol), or
  also report the forward-traceback figure as a bracket around the current
  one. Until then, treat first-word CER as an upper bound, not an exact
  attribution.
- **Determinism check** in CI runs the same block size twice; add a bench
  option to vary the block size and compare.
- **Test gaps** noted in review: detector hysteresis band and equal-power peak
  ties; channelizer band-edge and adjacent-channel tests use exact-bin tones;
  a direct test that a track's final text precedes its "gone" event (needs a
  way to inject a test decoder).
- **Input checks:** reject infinite values in decoder settings; generator
  arguments (zero or negative counts and durations).
