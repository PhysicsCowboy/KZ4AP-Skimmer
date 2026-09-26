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
   stages" below;
2. a small streaming CNN+LSTM network trained with CTC, following VE3NEA's
   DeepCW;
3. a Bell-style explicit-duration HMM with beam search and Kalman amplitude
   tracking;
4. hybrids of 2 and 3.
See the survey's "Rank the candidates by evidence per CPU cycle" for the
evidence and costs.

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
   is re-decoded through the narrower filter.

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
