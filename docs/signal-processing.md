# Signal Processing Pipeline

How KZ4AP Skimmer turns a stream of complex I/Q samples into decoded Morse
text. This document describes what the code actually does, with the numbers
it actually uses, and says for each choice whether it was **derived** (follows
from the math), **measured** (a number we ran and checked), or is a
**heuristic** (an unmeasured judgment call). Heuristics are candidates for
measurement; see `docs/backlog.md`.

**Maintenance rule:** any change to the engine's signal processing (a
parameter, an algorithm, the order of stages) updates this document in the
same commit.

Numbers below are for the default 192 kHz sample rate unless stated. Where a
value scales with the sample rate, the scaling is given.

## 0. Units and symbols

**Units.** The engine has no physical calibration: nothing relates its numbers
to volts or dBm at the antenna.

- **FS (full scale):** linear amplitude unit. The input WAV holds 16-bit
  integers; the engine divides them by 32768, so I and Q each lie in
  [−1, 32767/32768] FS: 1 FS itself is just beyond the largest positive value
  the file can hold. The WAV is the SDR software's output, not raw
  ADC counts, and the gain from the antenna to it is unknown.
- **FS²:** power unit (squared amplitude, e.g. |x|² of one complex sample).
- **dBFS:** power in dB relative to 1 FS². 0 dBFS is the power of a complex
  tone of amplitude 1 FS (|x| = 1 at every sample). All absolute dB levels in
  the engine (spectrum frames, averages, the noise floor) are dBFS.
- **dB SNR:** a power ratio, which only means something with the bandwidth
  the noise was measured in; this document always states it (section 5).
- dB values that are neither (a filter's response, a window's sidelobes) name
  their reference: "relative to the passband" or "relative to the main lobe".

**Symbols.** Used throughout; each is also defined where it first matters.

| Symbol | Meaning | Default value |
|---|---|---|
| fs | input sample rate (complex samples per second) | 192 000 Hz |
| N | FFT size: samples per transform, and number of frequency bins | 8192 |
| Δf | bin width, fs / N | 23.4 Hz |
| hop | samples between successive transforms, N/2 | 4096 |
| t_hop | hop duration, hop / fs, s | 21.3 ms |
| n | sample index within one transform, 0 … N−1 | |
| k | bin index | |
| w[n] | spectrum window (periodic Hann), ½ − ½·cos(2πn/N) | |
| Σw | sum of w[n] over the transform: N/2 (window's coherent gain × N) | 4096 |
| Σw² | sum of w[n]² over the transform: 3N/8 | 3072 |
| ENBW | equivalent noise bandwidth of one bin, N·Σw²/(Σw)² bins | 1.5 bins = 35.2 Hz |
| σ² | noise power per complex sample, FS² (σ: its RMS amplitude, FS) | synthetic recordings: σ = 0.02 FS |
| A | amplitude of a complex tone, FS | |
| τ | time constant of a first-order (exponential) filter, s | |
| α | per-update weight of such a filter, 1 − exp(−Δt/τ), where Δt is the time between updates, s | |
| T_avg | averaging time of the detector's power average, s | τ = 1 s (exponential) |
| T_el | duration of one Morse element (mark), s | dit = 48 ms at 25 WPM |
| B, B₁, B₂ | a bandwidth, Hz (noise bandwidth for SNR; filter width in 0.44/B) | |
| R | a station's averaged SNR per bin as a linear power ratio (dimensionless) | |
| H(f) | channel filter's frequency response at offset f (dimensionless, 1 at 0 Hz) | |
| y[n] | channel output sample, complex, FS | |
| e[n] | decoder's smoothed envelope, FS | |
| M, S | decoder's mark and space levels, FS | |
| channel_bins | FFT bins kept per station channel | 64 |
| D | decimation factor, N / channel_bins | 128 |
| r | channel (decoder input) sample rate, fs / D | 1500 Hz |
| dit | duration of one Morse dit, 1.2 s / WPM (PARIS timing) | 48 ms at 25 WPM |
| j | exponent in "48 kHz × 2^j" (j = 0, 1, 2, …): the rates 48, 96, 192, 384, 768 kHz | |
| K | matched-filter (boxcar) length, round(β·dit·r) | samples |
| β | matched-filter length as a fraction of the dit | 0.8 |
| v[n] | matched-filter output | FS |
| σ_v, s | noise RMS per real component of v (the complex noise power of v is 2σ_v²), and v's key-down amplitude | FS |
| x, a | normalized envelope \|v\|/σ_v and amplitude s/σ_v | dimensionless |
| Λ | log-likelihood ratio, key-down over key-up: −a²/2 + ln I₀(a·x) | nats |
| g, p | posterior log-odds Λ + ln(P₁/P₀), and the posterior probability of key-down | nats, 0…1 |
| P₁ | prior probability of key-down | 0.44 |
| f_off | a station's (carrier's) offset from its channel's center | Hz |
| f̂ | frequency tracker's estimate of f_off, and the NCO frequency | Hz |
| f_a | the tracker's anchor: where the station is, as the detector says (its frequency for the track minus the channel center) | Hz |
| τ_L | lag of the frequency discriminator, rounded to whole samples; unambiguous range ±1/(2τ_L) | τ_L = 8/r = 5.333 ms (±93.75 Hz) |
| τ_f | time constant of the frequency average, s of key-down weight 1 | 0.5 s |

## Overview

```
  I/Q samples (complex, fs = 192 kHz)
        │
        ├──────────────► Spectrum analyzer ──► Signal detector ──► tracks (frequency, SNR)
        │                (Hann window, N-point FFT,   (1 s power average,        │
        │                 power per bin, dBFS)         peak picking)             │ opens / closes
        │                                                                        ▼ channels
        └──────────────► Channelizer ─────────────────────────────────► one complex stream
                         (shared N-point FFT; per channel: shift to        per station,
                          0 Hz, low-pass, decimate ×128)                   r = 1500 samples/s
                                                                                 │
                                                                                 ▼
                                                                  Classical decoder (per station)
                                                                  |y| → smoothing → keying →
                                                                  element timing → symbols
```

Two independent FFTs run on the same input: a **windowed** one for measuring
power (detection and, later, the waterfall), and an **unwindowed** one used
only as a fast-convolution filter bank (the channelizer). Both use the same
size N and the same hop N/2, so they stay in lockstep.

The decoder box shows the Envelope front end (`--front-end envelope`, the
milestone-1 pipeline). The default, the Matched front end, replaces
|y| → smoothing → keying with frequency re-centering, a dit-matched filter
and keying on the posterior log-odds (sections 7 and 8b), and the engine
gives each channel's decoder the detector's current frequency for its track
before every block (section 6, "Channel distance").

## 1. Input

- A 16-bit PCM stereo WAV: left channel = I (real part), right = Q
  (imaginary part), divided by 32768, so values lie in [−1, 1) FS (section 0).
- Complex sampling means the stream covers −fs/2 … +fs/2 around the radio's
  center frequency: ±96 kHz at 192 kHz. Frequencies throughout the engine are
  **offsets from the center**, in Hz.
- The engine consumes input in blocks of exactly N/2 samples (the hop),
  buffering any remainder, so results are identical however the caller
  splits the input.

## 2. The FFT size N and bin width Δf

N is the number of samples per transform, which is also the number of
frequency bins. Complex sampling fixes the total span (fs wide); N sets how
finely it is divided:

- bin width Δf = fs / N
- each transform spans N / fs seconds of signal

The code (`choose_fft_size`, engine.cpp) picks N as the **largest power of two
with Δf ≥ 20 Hz**:

| fs | N | Δf | transform length N/fs | hop (N/2) |
|---|---|---|---|---|
| 44.1 kHz | 2048 | 21.5 Hz | 46.4 ms | 23.2 ms |
| 48 kHz | 2048 | 23.4 Hz | 42.7 ms | 21.3 ms |
| 96 kHz | 4096 | 23.4 Hz | 42.7 ms | 21.3 ms |
| 192 kHz | 8192 | 23.4 Hz | 42.7 ms | 21.3 ms |
| 768 kHz | 32768 | 23.4 Hz | 42.7 ms | 21.3 ms |

**Status: heuristic, and the 20 Hz is not the real parameter.** The history:
N = 8192 at 192 kHz (Δf = 23.4 Hz) was the original first guess, never
measured. When the engine was made to choose N from the sample rate, the
"≥ 20 Hz" rule was written only to reproduce that 23.4 Hz at other rates.
Because N moves in factors of two, any threshold between 11.7 Hz and 23.4 Hz
gives the same N at 48 kHz × 2^j; 20 Hz is just a number in that range. The
real parameter is "bins about 23 Hz wide". (At rates that aren't 48 kHz × 2^j
the threshold does matter: at 44.1 kHz it gives 21.5 Hz bins.)

The idea behind ~23 Hz was: narrow enough to separate stations a few tens of
Hz apart and keep noise per bin low, wide enough that a keyed CW signal's
energy (tens of Hz wide at contest speeds) falls in one to three bins. It has
not been measured against alternatives (backlog: "Choose the FFT bin width
and channel filter by measurement").

## 3. Hops and 50% overlap

Both FFTs advance by a hop of N/2 samples (21.3 ms), so each transform uses
the newest hop of samples plus the one before it: consecutive transforms
overlap by 50%. After the first, there is exactly one spectrum FFT and one
channelizer FFT per hop. (The spectrum analyzer's first transform comes when
N samples have arrived, i.e. after two hops; the channelizer starts after one
hop with N/2 samples of zeros as history.)

Why 50% (**status: standard choice**; what follows from it is derived):
- **Spectrum:** periodic Hann windows shifted by N/2 add up to a constant
  (w[n] + w[n + N/2] = 1), so across frames every input sample is weighted
  equally: nothing between the windows' tapered ends is under-counted. N/2 is
  the *largest* hop with that property; smaller hops such as N/4 (75%
  overlap) have it too, at twice the computation. Successive frames at 50% are
  nearly independent for noise: the correlation of the noise power in a bin
  between neighboring frames is (Σ w[n]·w[n+N/2] / Σw²)² = (1/6)² ≈ 3%, the
  sum running over the overlapping half, n = 0 … N/2 − 1.
- **Channelizer:** it shares the spectrum's hop so the two stay in lockstep.
  Given that hop, the filter length follows (**derived**): overlap-save fast
  convolution with an L-tap filter needs L − 1 samples of history in front of
  each block of new samples. With N-point transforms and N/2 new samples per
  hop, the history is N/2 samples, which allows at most L = N/2 + 1 taps. That
  is why the channel filter is exactly N/2 + 1 taps long (section 7).

## 4. Spectrum analyzer (for detection)

Every hop, take the last N samples x[n], multiply by the window w[n], FFT,
and compute the power in each bin:

  P[k] = |X[k]|² / (Σw)², where X[k] = Σₙ w[n]·x[n]·exp(−j2πkn/N)

and store it as 10·log₁₀(P[k]) dBFS. Bins are stored lowest frequency first:
stored bin i is at (i − N/2)·Δf.

**The window.** w[n] = ½ − ½·cos(2πn/N), n = 0 … N−1 (a *periodic* Hann
window). Its sums: Σw = N/2 and Σw² = 3N/8.

**What a tone reads.** A complex tone of amplitude A FS exactly on a bin
gives |X[k]| = A·Σw, so P[k] = A², i.e. 20·log₁₀A dBFS. Dividing by (Σw)²
corrects for the window's coherent gain. A tone of amplitude 0.0102 FS (the
synthetic 20 dB SNR (500 Hz) signal of section 5) reads −39.8 dBFS key-down.

**What noise reads.** White noise of σ² FS² per sample gives an average
|X[k]|² of σ²·Σw², so

  P_noise = σ²·Σw²/(Σw)² = σ² · 1.5/N = (σ²/fs) · ENBW,

that is, the noise power spectral density σ²/fs (FS²/Hz) times the bin's
**equivalent noise bandwidth**, ENBW = N·Σw²/(Σw)² = 1.5 bins = 35.2 Hz. With
σ = 0.02 FS at 192 kHz: σ² = −34.0 dBFS total, σ²/fs = −86.8 dBFS per Hz, and
each bin reads −71.4 dBFS.

**Why the ENBW is 1.5 bins, and why use Hann anyway.** The window tapers the
ends of each transform, which broadens every bin's frequency response: a bin
collects noise from 1.5 bins' worth of bandwidth instead of 1. A rectangular
window (no window) would give exactly 1.0 bin, i.e. 1.8 dB less noise power
per bin (a ratio of 1.5), but its sidelobes are only 13 dB below the main
lobe, so a strong station leaks into bins far away. Hann's highest sidelobe is 31 dB below the main
lobe and falls off quickly. **Status: standard choice**, not compared with
other windows here.

**Spreading.** The Hann window spreads even a pure tone over several bins: its
main lobe is 4 bins wide (±2 bins), and a tone halfway between two bins shows
up in both, each 1.42 dB below the on-bin value ("scalloping" loss, derived).
A keyed signal spreads further, by its keying sidebands.

**Why frames are stored in dB.** The frame format was chosen with a future
waterfall display in mind, which wants dB. The detector, however, needs linear
power (section 6), so every value is converted to dB here and back to linear
there. That round trip is wasted work; the backlog has an item to store
linear power and convert only where dB is needed.

## 5. What "SNR" means here

An SNR in dB is meaningless without the bandwidth the noise is measured in:
white noise power grows in proportion to bandwidth, so for the same signal

  SNR(B₁) = SNR(B₂) + 10·log₁₀(B₂/B₁)   (white noise, signal inside both bandwidths)

Two conventions are in use in this project:

- **Detector (tracks, `Track::snr_db`):** per bin, i.e. SNR in the Hann ENBW
  of **35.2 Hz**. It is the averaged power (section 6), so it is the key-down
  power times the keying duty cycle: about 3 dB below key-down for a signal
  on half the time (10·log₁₀ 0.5 = −3.0 dB). It drops by more than the duty
  cycle alone because keying spreads the signal: at 25 WPM a dit (48 ms) is
  about as long as one Hann frame (42.7 ms), so the keying sidebands put part
  of the power outside the 35 Hz ENBW of the peak bin. It is also reduced by
  up to 1.42 dB when the station sits between bins.
- **Synthetic recordings and the benchmark:** key-down carrier power over
  noise power in **500 Hz**. This is one convention, not a universal
  standard (WSJT-X reports SNR in 2500 Hz, C/N₀ uses 1 Hz), but it is the one
  CW Skimmer and the Reverse Beacon Network (RBN) use. Per its author's
  description, CW Skimmer takes the signal through a 50 Hz filter, discards
  key-up and transition samples, and estimates key-down power under a
  Rayleigh fading model; it divides that by the noise density × a nominal
  rectangular 500 Hz, with the noise density estimated from the flat part of
  the whole receiver span (`docs/research/decoder-survey.md`, "One SNR
  yardstick"). For clean, non-fading signals our synthetic SNR is the same
  quantity. How each side *estimates* signal and noise from real audio
  differs, and matching them remains a calibration item (backlog).

Conversion, 500 Hz → per bin: +10·log₁₀(500/35.2) = **+11.5 dB**. Example: the
engine tests' 20 dB SNR (500 Hz) signal is 31.5 dB SNR per bin key-down, and
its averaged track SNR is about 27 dB per bin (keying duty cycle a little
under 50% including the text's word spaces, plus the keying sidebands that
fall outside the peak bin).

## 6. Signal detector

Finds CW carriers and keeps a list of **tracks** (one per station).

### dB and linear

The spectrum arrives in dBFS. For each bin the detector converts back to
linear power (FS²), averages in linear, and converts the average to dB. That
order matters: averaging noise power in dB would bias it about 2.5 dB low
(the mean of the logarithm of exponentially distributed power is 2.51 dB
below the logarithm of its mean). Everything after averaging is done in dB,
which is correct for each use:
- thresholds are ratios to the floor, so comparing dB differences is the same
  as comparing linear ratios;
- the median (noise floor) is unchanged by any monotonic transform, so the
  median of dB values is the dB of the median;
- parabolic peak interpolation genuinely works better on log power: the
  Hann main lobe is closer to a parabola in dB than in linear power.

### Averaging

Each bin's linear power P is averaged over time with a first-order
(exponential) filter, one update per frame (every t_hop = hop/fs = 21.3 ms):

  P̄ ← P̄ + a·(P − P̄), a = max(α, 1/m), α = 1 − exp(−t_hop/τ), τ = **1 s**

where m counts the frames so far. So the first frames get a plain running
mean (a = 1/m) until 1/m falls below α = 0.0211, after about 47 frames (1 s),
and the exponential filter takes over. Averaging is **per bin**: bins are
never merged.

Why average, and how much:
- A keyed signal is on only about half the time. Averaged over many elements
  it looks like a steady carrier about 3 dB below its key-down power, so
  detection doesn't depend on catching it key-down.
- Averaging shrinks the noise fluctuation in each bin. A single frame's noise
  power in a bin is exponentially distributed: its standard deviation equals
  its mean. An exponential average has the same noise variance as a plain
  (boxcar) mean over (2 − α)/α ≈ 2τ/t_hop ≈ **94 frames** (2 s). That cuts the
  relative fluctuation to about 1/√94 ≈ 10% (≈ ±0.45 dB). Even the largest of
  8192 such bins stays well under the 6 dB detection threshold (roughly 4
  standard deviations, 1.5 dB above the mean noise power), so noise alone
  almost never creates a track.
- The cost is latency: a new station takes about 0.5–1.5 s to become a track
  (averaging plus the 0.5 s persistence), and a track outlives its station by
  several seconds while its average decays.

**How long the average takes to forget a station.** After a station stops,
its contribution to P̄ decays as exp(−t/τ). The track stays alive while its
level is ≥ 3 dB above the floor, i.e. until the leftover signal power falls to
about the noise power. Starting from an averaged SNR per bin of R (a linear
power ratio), that takes about t = τ·ln R: for 27 dB SNR per bin (R = 500),
6.2 τ = 6.2 s. (A straight dB slope of 4.34 dB per τ gives (27 − 3)/4.34 =
5.5 τ, but that ignores the noise added to the leftover signal.)
**Measured once**, while writing the engine test
`EventsFollowTrackLifecycle` (recorded in a comment there, not asserted by
any test): a 20 dB SNR (500 Hz) station's track died 7.8 s after its last
mark with a 1 s timeout, i.e. about 6.8 s of decay.

**Status of τ = 1 s: heuristic.** It is a round number that seemed a sensible
compromise. The averaging time T_avg *should* be derived from three
requirements: a false-alarm rate
(how often the largest of N noise bins crosses the threshold), an averaging
time covering several characters at the slowest speed of interest (so the
duty cycle averages out), and acceptable detection latency. The backlog has
this, and a comparison with a boxcar mean.

### Noise floor

The **median** of the averaged spectrum (in dB) across all N bins, recomputed
every frame.

- **Bias.** For averaged noise the median is essentially the mean: the
  averaged power is close to Gaussian, and a simulation of this filter on
  exponential noise puts its median 0.02 dB below its mean (less than 0.1 dB).
  For a single, unaveraged frame the median of exponentially distributed noise
  power is ln 2 = 0.69 of its mean, 1.6 dB low; that is not the case here.
  Signals occupy far fewer than half the bins, so they don't move the median.
- **It is global:** one number for the whole span. That is fine for the flat
  synthetic noise, but a real SDR's passband is not flat (it rolls off toward
  the band edges), and there is often a spike at 0 Hz (DC offset). Where the
  local noise is above the median, noise crosses the threshold more easily
  (false tracks); where it is below, weak stations are harder to detect and
  their SNR reads low. The backlog has a local floor as a candidate.

### New tracks

A bin becomes a candidate when its averaged power is at least **6 dB** above
the floor (6 dB SNR per bin) *and* it is the maximum within **±47 Hz**
(±2 bins at 23.4 Hz; ties go to the lower bin). A candidate must be seen in
every frame (moving by at most **23 Hz**, 1 bin, between frames) for
**0.5 s**, and nothing is detected during the first 1 s of a recording (the
averages settle first). A peak whose interpolated frequency is within the
**channel distance D = 47 Hz** of an existing track's *current* frequency
is attributed to that track instead (below, "Channel distance"). The
milestone-1 rule, a peak less than **3 bins** (70 Hz) from a track's bin,
is still selectable (`Attribution::Bins`) and is what the Envelope path
(`--front-end envelope`) uses. The neighborhoods are stated in Hz and
converted to bins from the actual bin width (`DetectorConfig`
`peak_radius_hz`, `candidate_step_hz`). **Status: all heuristic.**

### Why neighboring bins come up at all

Averaging is per bin and never merges bins. Neighbors enter only the decision
"is this a new station?" (the ±2-bin peak rule, the 3-bin attribution rule)
and the level of an existing track (its bin ±1, below). These rules undo
spreading: one station puts power in 2–4 adjacent bins because of the Hann
window's main lobe, its position between bins, and its keying sidebands.
Without them, one station would become several tracks.

Using larger bins instead would be worse on two counts: noise per bin grows in
proportion to bin width (a 3× wider bin gives 10·log₁₀3 = 4.8 dB lower SNR per
bin for the same signal), and two stations closer than a bin could no longer
be told apart.

These rules are stated in Hz and converted to bins at the point of use, so a
different bin width keeps their width in Hz (the 3-bin rule of
`Attribution::Bins` is still counted in bins). The conversion is
`std::lround(Hz / Δf)`: 47 Hz rounds to 2 bins and 23 Hz to 1 bin for bin
widths from 18.8 to 31.3 Hz, which covers every usual rate (8, 11.025, 32,
44.1, 48, 96, 192 and 768 kHz give 20–31.25 Hz bins with
`choose_fft_size`), so the Envelope path stays bit-identical to milestone 1
there; at rates whose bins are wider than 31.3 Hz (for example 33–40.9 kHz)
the peak neighborhood rounds to 1 bin (derived; nothing rejects such a
rate).

### Existing tracks

A track's level is the maximum over its bin's neighborhood of ±23 Hz (±1 bin
at 23.4 Hz; `DetectorConfig::level_radius_hz`), the bin being the track's
current peak bin. Its SNR is that level minus the floor. It stays active while the SNR
is at least **6 − 3 = 3 dB** per bin (hysteresis), and dies after **10 s** without
being active. **Status: heuristic.**

### Pauses and track lifetime

A station that pauses keeps its track while the pause is shorter than the
average's decay time (above) plus the 10 s timeout: roughly **16–17 s** for a
20 dB SNR (500 Hz) station, longer for stronger ones. This is **by accident,
not design**: neither number was chosen with pauses in mind. Within that
time the same track, channel and decoder continue, so the decoder keeps its
speed estimate. After a longer pause the track has died; when the station
resumes it becomes a new track with a new decoder, which starts again from
25 WPM.

### Frequency and drift

A new track's frequency is refined by parabolic interpolation over the peak
bin and its neighbors in dB (offset clamped to ±½ bin): typically accurate to
a few Hz. **Measured:** a station at +1000.0 Hz is reported at +999.8 Hz.

With the Envelope path (`Attribution::Bins`) the frequency is then
**fixed** for the track's life: it is not re-measured, and the channel stays
where it was put. With the Matched path (the default; `Attribution::Distance`)
each track follows its own spectral peak (below, "Channel distance"), so a
drifting station keeps one track and its level is read where it now is;
the decoder re-centers within ±12 Hz of the track's frequency (section 7),
and the engine reports the channel center plus the decoder's estimate. The
channel itself does not move; the decoder's NCO covers ±75 Hz around it.
Only slow drift is in scope (owner, 2026-09-29): simulated at 1 Hz/s, one
track, the reported frequency 1.49–1.63 Hz behind the carrier and the
track's own 0.94–1.23 Hz behind (a 1 s average of a ramp lags ḟ·1 s,
derived).

On the Envelope path, tolerance to drift is passive:
- the track's level is taken over ±1 bin, so a drift of up to about ±1.5 bins
  (±35 Hz) costs at most 1.42 dB of level (scalloping). Beyond that the loss
  grows fast (derived from the Hann response, for a steady tone): 6 dB at
  ±2 bins (±47 Hz), 15 dB at ±2.5 bins (±59 Hz). Whether the track survives
  depends on how far above the 3 dB keep-alive level the station was. Once
  the peak is 3 or more bins (≥ 70 Hz) from the track's bin, it can also be
  born as a second track;
- the channel filter is flat (within 0.01 dB) to ±30 Hz and only 1.2 dB down
  at ±100 Hz (measured, section 7), so the decoder would tolerate more drift
  than the track does.

The **speed**, in contrast, is tracked continuously (the last 24 marks,
section 8).

### Channel distance

One distance, **D = 47 Hz** (`EngineConfig::channel_distance_hz`;
**heuristic**, owner decisions 2026-09-29, "option 1"), decides with the
Matched path which station each channel follows. The detector alone
decides:

1. **Following:** every frame, before its level is read, each track moves
   to the strongest bin that is a peak by the birth rule (the maximum
   within ±47 Hz), stands at least 3 dB above the floor (the keep-alive
   level), and whose interpolated frequency is within D of the track's
   current frequency. With none, it holds its frequency.
2. **Attribution:** a new peak within D of a track's current frequency is
   that track's (above); one farther away can become a track of its own.
3. **The channel's tracker fine-tunes** within ±12 Hz of the detector's
   frequency for its track (section 7); the engine passes it before every
   channel block. So in a QSO turnover within D the channel retunes to the
   answering station when the detector's peak moves there, and back.
   Channels are never merged.

Why 47 Hz: about the half-width of the detector's Hann main lobe, 2/T_w
for its 42.7 ms window (46.9 Hz); a peak closer than that to a station can
be that station's own spread. Stated in Hz, it does not change with the
FFT size.

Simulated (plan, 2026-09-29; A at S₅₀₀ = 15 dB, 25 WPM; B answering at
18 WPM, its level in dB re A's key-down power; 30 seeds each): within D
(0–40 Hz) at −6 dB or stronger, A's channel followed B, decoded it and
came back, with one channel; 60–200 Hz away B had its own channel,
decoded in 30 of 30 from −6 dB up; channels were never merged.
**Limits (stated, not fixed):**
- **Retune delay.** The detector's peak moves to an answering station
  only when its 1 s power average overtakes the first station's decaying
  one: 1.3–1.8 s into B's over at 10–25 Hz, 2.3 s at 40 Hz and −10 dB
  (simulated; 2.6 s after A's last mark derived at −10 dB). It matters
  only when the answering station is on a different frequency from the
  one the channel is tuned to (a station that pauses and resumes on its
  own frequency loses nothing). Median characters of B lost at the start
  of its over: 0 at 10 Hz; at 25, 40 and 50 Hz, 1, 3 and 3 at −10 dB, 0,
  1 and 2 at −6 dB, 0 at 0 and +6 dB.
- **A neighbor 60–70 Hz away at A's level or stronger** leaks through the
  matched filter's first sidelobe (−18.7 dB relative to a centered station
  at 60 Hz and K = 58; −19.6 dB at 70 Hz through the 16 ms acquisition
  filter; derived), is keyed in fragments and corrupts the speed estimate:
  A's next over exact in 6 and 0 of 30 at 60 Hz, 0 and +6 dB (its last
  three words intact in 26 and 30). The fix belongs in the filter's design.
- **Oracle mode** (benchmark only) has no detector, so the anchor is fixed
  at the labeled frequency the oracle channel was opened for, and the
  tracker covers ±12 Hz around it: a station that drifts more than 12 Hz
  from its label, or a QSO's answering station more than 12 Hz from the
  label, cannot be followed there; the benchmark marks such oracle rows as
  not meaningful for the Matched front end.

### Cap

At most **200** tracks (`DetectorConfig::max_tracks`, a config parameter not
yet exposed to users). When the cap is reached, a new candidate replaces the
weakest track if it is stronger; otherwise it is ignored. **Status:
heuristic**, a guard against CPU overload, not a measured limit. For scale:
the physical ceiling with 3-bin separation is about fs / 70 Hz ≈ 2700 tracks
at 192 kHz, and a busy contest can put more than 100 stations in 192 kHz.

### Oracle mode (benchmark only)

`EngineConfig::oracle_frequencies_hz` (`kz4ap-bench --oracle`) bypasses the
detector: a channel is opened at each given frequency, rounded to the nearest
FFT bin, from the first sample, and stays open to the end. It measures the
decoder apart from detection, including below the detector's threshold
(roughly S₅₀₀ = 0 dB for a keyed station), and leaves the station up to
±½ bin (±11.7 Hz) off its channel's center, the worst case for frequency
re-centering. Normal operation never uses it.

## 7. Channelizer: one stream per station

For each track, the channelizer produces a narrow complex baseband stream
centered on the station, at a low sample rate. Mathematically, for a station
at center frequency f_c (the track frequency rounded to the nearest bin, so
within ±½Δf = ±11.7 Hz of it):

1. **Mix down (complex, I/Q):** multiply the input by exp(−j2π·f_c·t). This
   moves the station to 0 Hz. It is a complex frequency shift, not an audio
   demodulation: the result is still complex I/Q, now centered on the
   station.
2. **Low-pass filter** the shifted signal (the channel filter, below).
3. **Decimate** by D: keep every D-th sample.

So the channel filter is applied **after** the frequency shift and
**before** decimation, and the decoder receives the **filtered** stream.

### Decimation

D = N / channel_bins = 8192 / 64 = **128**, so the decoder's rate is
r = fs / D = **1500 complex samples/s** at every 48 kHz × 2^j rate (N scales
with fs), and 1378 samples/s at 44.1 kHz (N = 2048, D = 32).

Its purpose is **computational**: the decoder handles 128× fewer samples, and
in overlap-save producing the decimated output directly (a 64-point inverse
FFT instead of an 8192-point one) is also cheaper than producing the full
rate. It loses nothing because the filtered signal has no significant content
outside the output band: a complex stream at r samples/s represents
−r/2 … +r/2 = ±750 Hz without folding negative onto positive frequencies, and
the filter's stopband begins at about ±280 Hz (measured, below).

The bounds that constrain channel_bins (**derived**):
- r/2 must be at least the filter's stopband edge, cutoff + ½·transition
  width (150 + 129 = 279 Hz with the 258 Hz estimate). The code enforces this
  (below).
- Timing resolution 1/r = 0.67 ms must be far below the shortest element,
  a 20 ms dit at 60 WPM.
- channel_bins must be even and divide N (the code rejects anything else).

64 bins satisfies these with a wide margin; the exact value is **heuristic**.

### How it's computed

Steps 1–3 are not done literally per sample. They are done at once with
overlap-save fast convolution:
- One N-point FFT per hop, shared by all channels (unwindowed; the N/2
  earlier samples of history provide the overlap, section 3).
- For each channel, take the 64 FFT bins centered on f_c, multiply by the
  filter's frequency response, and inverse-FFT those 64 bins. Selecting bins
  around f_c is the frequency shift; the multiplication is the filter; using
  64 bins instead of N is the decimation.
- Keep the last 32 of the 64 output samples (the rest are corrupted by
  circular wrap-around), then correct a phase term so each channel's phase is
  continuous from block to block.

The cost is one large FFT per hop plus one 64-point inverse FFT per station,
instead of a mixer and filter per station per input sample. The result is
identical to steps 1–3 up to floating-point rounding and the filter response
beyond ±750 Hz, which is discarded (verified by the unit tests and an
independent hand derivation in review).

### The channel filter in frequency

- A linear-phase FIR low-pass: a Blackman-windowed sinc, N/2 + 1 taps (4097 at
  192 kHz), normalized to unity gain at 0 Hz. **Status: standard choice.**
- Cutoff (−6 dB relative to the passband) at **±150 Hz** around the station (`channel_cutoff_hz`).
  **Status: heuristic** (below).
- **Measured** response (steady tones through the Channelizer, relative to
  the 0 Hz response):

  | offset | 0–30 Hz | 50 Hz | 75 Hz | 100 Hz | 125 Hz | 150 Hz | 200 Hz | 250 Hz | 280 Hz | 290–400 Hz |
  |---|---|---|---|---|---|---|---|---|---|---|
  | response | 0.00 dB | −0.05 dB | −0.34 dB | −1.17 dB | −2.93 dB | −6.02 dB | −18.0 dB | −43.9 dB | −75.5 dB | −79 to −84 dB |

  The same values follow from the tap formula directly (computed
  independently in review).
- **Transition band (computed from the taps):** by the Blackman window's own
  criteria, the passband (response within 0.0017 of 1, i.e. 0.015 dB) runs to
  about **39 Hz**, and the stopband (at least 74 dB below the passband) starts
  at about **279 Hz**: a transition band about **240 Hz** wide. The usual
  Blackman estimate, 5.5 / (filter length in seconds) = 5.5 / 21.3 ms =
  **258 Hz**, is slightly wider, so it is a safe bound. The code uses that
  estimate as ±129 Hz around the cutoff; it puts the stopband edge at
  150 + 129 = 279 Hz, which matches the computed edge. (The computed passband
  edge sits at 150 − 111 Hz, so the band is not exactly symmetric about the
  cutoff.)
- Its noise bandwidth, ∫|H(f)|² df over all f, computed from the taps, is
  **252 Hz**. That is the bandwidth of the noise the decoder sees.

**Aliasing check (derived).** The channel keeps only bins within r/2 = ±750 Hz
of its center, so the filter's whole transition band must fit inside that.
The constructor requires

  r/2 ≥ cutoff + ½ · 5.5 / (taps / fs)

(5.5 / (taps / fs) is the Blackman transition width), and throws otherwise.
With the defaults that allows cutoffs up to about 621 Hz. (It used to check
only cutoff < r/2, but the cutoff is the −6 dB point, not the stopband edge: a
700 Hz cutoff passed that check with its stopband starting near 829 Hz.)

### The channel filter in time: follow one dit through it

A 25 WPM dit is a 48 ms burst of carrier. **Measured** by pushing a hard-keyed
carrier through the Channelizer and measuring |y|:

- The filter's impulse response lasts (N/2 + 1)/fs = **21.3 ms** (32 output
  samples), shorter than the dit.
- The whole dit comes out **10.7 ms late** (group delay, (taps − 1)/2 / fs):
  measured 10.66 ms at both the leading and trailing half-amplitude points.
  Because the filter is linear-phase, this is a pure shift: every frequency
  is delayed equally, so the pulse is not smeared asymmetrically, and its
  width at half amplitude comes out exactly 48.0 ms. Decoded timestamps
  include the delay.
- Each edge rises from 10% to 90% of the amplitude in **3.1 ms** (measured
  3.14 ms; the rule of thumb 0.44/B with B = 150 Hz estimates 2.9 ms). The
  edges are also rounded with a ringing of about **5%** of the amplitude on
  each side of the edge (measured peak 1.052 A after the edge; the filter's
  symmetry puts the same ripple before it). This is the price of the
  filter's fairly sharp cutoff; it is small next to the decoder's thresholds
  (40–60% of the way from space to mark level).

What actually limits timing is the decoder's envelope smoothing (section 8):
a first-order filter with τ = ¼ dit (12 ms at 25 WPM), whose 10–90% rise time
is τ·ln 9 ≈ 26 ms, eight times the channel filter's.

### Why ±150 Hz

**Status: heuristic.** It was sized for fast code, about 60 WPM: 20 ms dits
need several keying harmonics (tens of Hz each) to keep their edges. The
principle that *should* set it is speed: for detecting an element of
duration T_el, a filter matched to it has a noise bandwidth of 1/T_el
(exact for a rectangular element, derived; proakis-ook-notes.md section 2.7); for
timing its edges, somewhat wider. A fixed ±150 Hz (252 Hz noise bandwidth)
is about 4× wider than the ±30 Hz that the backlog roughly estimates is enough at 15 WPM (an
estimate, not measured), so slow stations get about 6 dB more noise than they
need.

The cutoff is set in Hz and the filter length N/2 + 1 scales with fs (N ∝ fs),
so the filter's response in Hz is the same at every 48 kHz × 2^j rate. (At
44.1 kHz the filter is 23.2 ms long and its transition band about 237 Hz.)

This stage-1 filter is meant to stay fixed and wide; narrowing per station is
planned as a second stage at 1500 samples/s (backlog: "Channel filtering, two
stages").

### Residual frequency offset

Because f_c is rounded to a bin, a station can sit up to ±11.7 Hz from 0 Hz in
its channel, well inside the flat passband. The offset appears as slow phase
rotation, which the decoder ignores (it uses only the magnitude).

That is harmless **only because the channel filter is wide**. A filter
matched to an element of duration T_el (noise bandwidth exactly 1/T_el for
a rectangular element) scales a tone offset by f_off (Hz) by
|sinc(f_off·T_el)| in amplitude, where sinc(x) = sin(πx)/(πx) (**derived**, from Proakis &
Salehi, *Digital Communications*, 5th ed., eq. 4.5–28;
`docs/research/proakis-ook-notes.md`, section 2.7). At ±11.7 Hz a
dit-matched filter would lose 5.1 dB at 25 WPM and 8.8 dB at 20 WPM (signal
power, relative to a centered station). So the planned narrow second-stage
filter (backlog: "Channel filtering, two stages") needs each station
re-centered to a fraction of a bin first; the current code does not do this.

A channel opens when its track is born and closes when it dies. It starts
with the current block, so the decoder never sees the signal from before the
detector noticed it: see "first characters" in the backlog.

### Frequency re-centering (Matched front end only)

Used only when the decoder's front end is `Matched` (section 8b); the
Envelope pipeline does not re-center. `FrequencyTracker`
(frequency_tracker.cpp) runs per station at r = 1500 samples/s inside the
classical decoder, on the channel stream, ahead of section 8b's filter.
The engine sets the anchor before every channel block to the detector's
current frequency for the track minus the channel center (section 6,
"Channel distance").

- **NCO (derived):** u[n] = y[n]·e^(−jφ[n]), φ advancing by 2π·f̂/r per
  sample. f̂ starts at the initial offset its owner gives it (the
  detector's residual, track frequency minus the channel's center;
  about 0.2 Hz error measured for a clean station) and is clamped to
  ±75 Hz, where the channel filter is 0.34 dB down relative to the
  passband (**heuristic**; beyond it the channel itself would have to
  move, which the code does not do). φ runs on through every change of f̂
  (the tracker's updates, `set_anchor` jumps, `reacquire`); only `reset`
  sets it to 0.
- The engine starts each new track's NCO at the detector's residual, track
  frequency minus channel center (in oracle mode, 0 Hz: the channel sits
  on the bin nearest the labeled frequency and the tracker must find the
  rest; its anchor is the labeled frequency itself, so it fine-tunes
  within ±12 Hz of the label).
- **Discriminator (derived):** on the narrow-filtered, re-centered stream
  v[n], the product z = v[n]·conj(v[n − τ_L·r]) has phase
  2π·(f_off − f̂)·τ_L for a station at f_off. Rotating it by
  e^(j2π·f̂·τ_L) makes it a measurement of f_off itself, so the average below does not
  depend on the NCO and there is no loop to stabilize (to first order: v
  averages samples mixed under the last K/32 NCO settings while each
  product is rotated by the current f̂, a small coupling while f̂ moves,
  harmless because τ_f·r = 750 samples is much longer than K/2). The lag
  is configured as 5.33 ms and rounded to whole samples,
  τ_L = 8/r = 5.333 ms, which gives an unambiguous range of
  ±1/(2τ_L) = ±93.75 Hz (**derived**; the value is **heuristic**; it is
  converted to samples from the physical value). The NCO range (±75 Hz)
  and the fine-tuning range (±12 Hz) must both lie below it; the
  constructor rejects a configuration where they do not.
- **Average (heuristic):** Z̄ ← Z̄ + α·p·(rotated z − Z̄),
  α = 1 − e^(−1/(τ_f·r)), τ_f = 0.5 s, weighted by p, the front end's
  key-down probability, so key-up and pauses leave it unchanged. Every
  32 samples (21.3 ms) f̂ ← arg(Z̄)/(2π·τ_L), once the average holds
  weight 0.6 or more and is coherent (|Z̄| > 0.3 × the same average of
  |z|; strictly greater).
- **Fine-tuning around the detector's frequency (heuristic; owner
  decisions 2026-09-29, option 1):** the tracker does not decide which
  station it follows. Its anchor f_a is set by its owner (`set_anchor`;
  the engine sets it to where the detector says the station is, minus the
  channel center, before every channel block); the anchor never follows the
  tracker's own estimates. An estimate is accepted only within
  ±12 Hz of f_a (**heuristic**, the owner's value; it must exceed the
  detector's interpolation error, 0.2 Hz measured, clamped to ±11.7 Hz,
  and stay well below the 47 Hz channel distance); otherwise the average
  is emptied and f̂ returns to f_a. When f_a moves more than 12 Hz from
  f̂ (the detector's track moved to another station's peak in a QSO
  turnover, or drifted), f̂ jumps to f_a and the average restarts. So the
  channel's station is followed through slow drift as far as the
  detector's peak goes, and a station more than 12 Hz from the
  detector's frequency can never pull the tracker toward it (as long as
  the owner keeps the anchor there). How far the detector's peak lags a
  ramp of ḟ Hz/s: its 1 s power average lags by ḟ·1 s (**derived**,
  exact for the power-weighted centroid of the averaged spectrum); the
  interpolated peak equals the centroid when ḟ·1 s is small compared with
  the Hann kernel's width (about one bin, 23.4 Hz), which holds at
  1 Hz/s. A non-finite anchor, or a non-finite value given to `reset`, is
  ignored (the previous state is kept); a non-finite initial offset is
  rejected by the constructor.
- **Fresh average after a jump (derived):**
  after a `set_anchor` jump or a rejection, f̂ changes at once, but the
  next τ_L·r + K − 1 products still contain samples of v that were mixed
  at the old f̂ (the boxcar spans K samples and the product reaches back
  τ_L·r = 8 samples), so their measurement of f_off is wrong by up to the
  size of the jump, and they bias the fresh average slightly. Their share
  of the average when it first reaches the 0.6 gate weight (**derived**,
  an upper bound: weight 1, all of those products counted as fully
  stale) is
  (1 − e^(−m/(τ_f·r)))·e^(−(n₆ − m)/(τ_f·r))/0.6 with m = τ_L·r + K − 1
  and n₆ = τ_f·r·ln 2.5 = 687 samples: 2.8% at K = 24 (60 WPM), 6.0% at
  K = 58 (25 WPM) and 32% at K = 288 (5 WPM), decaying with τ_f
  afterward. The classical decoder (section 8, "Two front ends"), which
  puts the tracker in front of the matched filter, does not hold off the
  average for those samples; the bias is left at this bound (not
  measured).
- **Expected accuracy:** about 0.5 Hz RMS at S₅₀₀ = 0 dB and 0.9 Hz at
  S₅₀₀ = −5 dB, 25 WPM (derived, an upper bound); a simulation of the
  whole chain (NCO, K = 58 boxcar, posterior weights, 60 s of PARIS,
  4 seeds; plan review, 2026-09-27) gave 0.08, 0.27 and 0.55 Hz RMS at
  S₅₀₀ = +10, 0 and −5 dB (simulated), valid once a station has been
  acquired, which needs S₅₀₀ ≥ −2.5 dB (derived) at any speed, in
  simulation 50% of marks keyed near S₅₀₀ = −1.8 dB at 25 WPM
  (section 8b, "Squelch"); Task 14 measures it in the benchmark; a linear
  drift of ḟ Hz/s is followed with a lag of about ḟ·τ_f/P₁ (1.1 Hz at
  1 Hz/s, P₁ = 0.44, derived; 1.49–1.63 Hz simulated at the end of the
  last mark through the whole engine). Target (spec §5.2): within ±2 Hz,
  a loss of 0.2 dB relative to a centered station at 20 WPM through a
  filter of length T.
- **Re-acquisition (heuristic):** after a silence (section 8) the decoder
  calls `reacquire()`: the average starts afresh (weight 0) from the last
  f̂, so the next station, if it is within ±12 Hz of the anchor, is found
  within about 0.5 s of key-down weight (the average needs weight 0.6);
  one farther away is reached when the detector moves the anchor.

## 8. Classical decoder (per station)

Input: the station's complex stream y[n] at r = 1500 samples/s. Output:
symbols (characters, `<XX>` prosign tokens, word spaces), each with a
probability and start/end times.

**What it is.** A **hard-decision baseline**: it decides key-down or key-up
sample by sample against thresholds, then classifies each timed element, then
looks the pattern up. Its design comes from general familiarity with simple
CW decoders; it was not derived from theory or compared with alternatives.
It does **not** yet do what the design spec asks of the classical decoder,
finding "the most probable character sequence given timing statistics and a
prior over likely text". A probabilistic decoder is the key next step; decoder
research is under way (`docs/research/`).

**Two front ends.** `ClassicalDecoderConfig::front_end` selects how samples
become key-down and key-up. `Envelope` (steps 1–5 below) is the baseline;
`Matched` is the default (owner decision 2026-09-29). `Matched` replaces
steps 1–5 with section 7's re-centering and section 8b's matched filter and likelihood: the key goes down when the
posterior log-odds g exceeds +1 nat and up when it falls below −1 nat
(**heuristic** hysteresis), and never goes down while a < a_min(K) (section
8b, "Squelch"). Steps 6–10
(glitches, elements, gaps, symbols, speed) are the same in both. In
`Matched` mode the filter follows the speed estimate once its window holds
8 marks (**heuristic**), and every decode result reports the tracker's
frequency estimate. While the filter follows, each speed
update may raise the dit estimate by at most ×1.25 (**heuristic**, owner
decision 2026-09-29): it stops a runaway after a sudden speed change, and a
real slowdown takes ln(ratio)/ln 1.25 marks to follow (5 marks from 35 to
12 WPM, derived). The same bound holds for the filter's own dit from its
first follow step after an acquisition or re-acquisition (owner decision
2026-09-29, option 1): it grows from the 20 ms acquisition dit by at most
×1.25 per mark until it reaches the estimate (4 marks to reach 25 WPM,
8 to reach 12 WPM, derived), because the estimate at that step may rest on
up to 7 unbounded marks (in simulation a truncated first mark gave a
110 ms estimate, the filter jumped from K = 24 to 146 samples and ran
away; the bound's effect on that case is not yet measured).
**Re-acquisition (heuristic):** once the key has been
up for max(0.5 s, 12 dits), the Matched decoder assumes the next station
may be a different one (a QSO turnover): section 8b's filter returns to
the 60 WPM width and its amplitude estimate restarts, section 7's
frequency average restarts from the last estimate, the speed window of
step 10 is set aside and a new one starts (so the next station's marks are
not mixed with this one's), and the filter follows the speed again after 8
new marks; if nothing is keyed within 2 s, the set-aside speed window comes
back and the filter returns to the width it had (a weak station that pauses
is then not held at the acquisition floor). The decoder does not decide
which station it follows: its frequency tracker fine-tunes within ±12 Hz
of the anchor its caller gives it (`Decoder::set_frequency_anchor_hz`;
section 7), so on its own it follows only a station within ±12 Hz of that
anchor; a station farther away is followed only when the caller moves the
anchor to it: the engine sets the anchor to the detector's frequency for the
track (section 6, "Channel distance"), so a station answering within 47 Hz
is followed once the detector's track moves to it (with the retune delay
stated there), and one farther away gets its own track.
Simulated at decoder level (an earlier tracker design whose estimate
never left 0.2 Hz of the first station in these runs; levels in dB re
the first station's key-down power; 100 seeds each): a station answering
50 Hz away at −10 dB (not keyed), 70 Hz away at −6 dB, or 100 Hz away at
−6 or +10 dB left the first station's next over intact in 99–100 of 100.
**Limits:** a neighbor 60–70 Hz away at the first station's level or
stronger leaks through the filter's first sidelobe (−18.7 dB relative to a
centered station at 60 Hz and K = 58, derived) and can be keyed in
fragments that corrupt the speed estimate (simulated). After a silence in noise alone, noise was keyed as a
stray character in about 1% of cases (4 of 400), because ŝ restarts from
its first few noise samples.

1. **Envelope detection.** Take the magnitude |y[n]| (FS). This is
   non-coherent AM detection: it needs no carrier recovery, and the carrier
   phase and, with the current wide channel filter, the residual frequency
   offset don't matter (section 7, "Residual frequency offset"). There is no audio tone (BFO)
   anywhere in the decoding path.
2. **Smoothing.** A first-order low-pass on the magnitude:
   e[n] = e[n−1] + α_s·(|y[n]| − e[n−1]), α_s = 1 − exp(−1/(τ_s·r)), with
   τ_s = ¼ dit at the current speed estimate (12 ms at 25 WPM; never less than
   one sample). It adapts as the speed estimate changes. (**Heuristic.**)
3. **Warm-up.** For the first dit at the initial speed of 25 WPM (48 ms,
   72 samples), e[n] is the running mean of |y|, both level trackers are set
   to it, and nothing is keyed, so the trackers start from the input's actual
   level. (**Measured:** without it, noise at the start keyed as one 1.4 s
   mark.)
4. **Mark and space levels.** Two asymmetric first-order followers track the
   envelope. The mark level M follows rises fast and falls slowly; the space
   level S does the opposite:

   M ← M + α_M·(e − M), α_M = α_fast if e > M, else α_slow
   S ← S + α_S·(e − S), α_S = α_fast if e < S, else α_slow

   with α = 1 − exp(−1/(τ·r)): τ_fast = 4 ms gives α_fast ≈ 0.154 and
   τ_slow = 3 s gives α_slow ≈ 2.2×10⁻⁴ at r = 1500 Hz. (**Heuristic;** 3 s
   was estimated, then confirmed by tests.)
5. **Keying decision** with hysteresis: key down when e rises above
   S + 0.6·(M − S) (60% of the way from space to mark); key up when it falls
   below S + 0.4·(M − S) (40%).
   **Compared with theory (qualitative only):** for a hard on/off decision
   the optimum threshold is where the prior-weighted Rayleigh (key-up) and
   Rician (key-down) envelope densities cross. It is not at 50% of the way
   from the key-up level to the key-down level, and it moves with SNR
   (`docs/research/proakis-ook-notes.md`, section 2.3; derived there from
   Proakis & Salehi). The notes' figure of about 44–45% (equal priors,
   E/N₀ = 10–14 dB re 1, key-on energy per element over one-sided noise
   density) is for an envelope taken after a filter matched to the element,
   and it does **not** carry over to this decoder quantitatively: here the
   envelope is formed after the 252 Hz channel filter, where the SNR is far
   lower, and only then smoothed. The one-pole smoother with τ_s = ¼ dit
   has a noise bandwidth of 1/(4τ_s) = 1/dit on the (real, one-sided)
   envelope, but smoothing after detection does not give the envelope
   statistics that filtering before detection would. So the 40% and 60%
   thresholds are not shown to be near the optimum for this decoder; that
   would need a measurement.
   **Squelch:** no keying (and any mark ends) unless M ≥ 3·S and M > S. The
   factor 3 is a 9.5 dB amplitude ratio (20·log₁₀3); in noise alone M/S
   measures about 1.45. It also means weak signals are not decoded at all:
   roughly below 6 dB SNR (500 Hz), an estimate, not measured.
   (**Heuristic;** 3× was estimated, then confirmed by tests.)
   **After a station stops,** the envelope drops to the noise level and M
   relaxes toward it with τ_slow = 3 s: its excess over the noise falls to 5%
   in 3τ_slow ≈ 9 s. Until M falls below 3·S the squelch stays open, with the
   keying threshold sinking toward the noise; this is the window in which stray
   `E`s are decoded after a station stops (backlog).
6. **Glitch rejection.** Marks shorter than 0.3 dit are ignored; key-up
   dropouts shorter than 0.3 dit are merged back into the mark.
7. **Element classification.** Each mark's duration d is compared to the dit
   estimate on a log scale: P(dah) = logistic((ln(d/dit) − ln 2) / 0.08),
   i.e. the dit/dah boundary is at 2 dits. The element's confidence is the
   probability of the chosen class.
8. **Gaps.** A space longer than 2 dits ends the character; longer than
   5 dits also emits a word space. (Nominal: 1, 3 and 7 dits.)
9. **Symbols.** The dot/dash pattern is looked up in the 62-symbol table
   (26 letters, 10 digits, 14 punctuation marks, 12 prosigns; eight or more
   dits is the error prosign `<HH>`). An unknown pattern emits `*`. A
   symbol's probability is the product of its elements' confidences.
10. **Speed.** Re-estimated after every mark from the last **24 marks**,
    sorted by duration and split into dits and dahs at the largest ratio
    between neighbors (if that ratio is at least 1.8).
    - If the dah/dit ratio is between 3.0 and 3.85, dit =
      (mean dah − mean dit) / 2. That cancels the constant shortening every
      mark gets from the keying edges. (**Measured:** it fixed 45 WPM reading
      as 51.)
    - Otherwise, dit = the mean of the dits and one third of each dah.
      Ratios above 3.85 are taken as the sender's weighting (hand-sent
      four-dit dahs measure 3.95–4.32).
    - If there is no split (all marks alike), they are taken as all dits or,
      if longer than 2 current dits on average, all dahs.
    - Marks longer than 0.96 s (a four-dit dah at 5 WPM) are excluded, as
      carriers rather than Morse elements.
    - The result is clamped to 5–60 WPM.

    **Why 24 marks (heuristic).** The window must contain both dits and dahs
    for the split to work, and more marks reduce the estimate's random error
    (as 1/√(number of marks)). Plain text averages about 3 marks per character
    (PARIS: 14 marks in 5 characters), so 24 marks is about 8 characters,
    spanning about 4 s at 25 WPM. The cost is lag: the estimate reflects the
    window's middle, about 2 s back at 25 WPM, so a speed change takes a few
    seconds to follow. A window measured in time rather than marks is an
    alternative (backlog).

## 8b. Matched front end (the default, per station)

Used when the classical decoder's front end is `Matched` (the default;
owner decision 2026-09-29) (`ClassicalDecoderConfig::front_end`; section 8, "Two front ends").
`MatchedFrontEnd` (matched_front_end.cpp) runs per station at
r = 1500 samples/s inside the decoder, on the re-centered stream u[n]
(section 7, "Frequency re-centering"), before any envelope is taken.

- **Filter (derived):** a boxcar (moving average) of K samples, normalized
  to unity gain. A boxcar of duration T is the matched filter of a
  rectangular element of duration T, with noise bandwidth exactly 1/T
  (Proakis §4.2–2; proakis-ook-notes.md §2.7). K = round(β·dit·r), β = 0.8
  (**heuristic**: 0.97 dB of output SNR below the matched filter, derived,
  in exchange for staying shorter than an element space when the speed
  estimate is up to 25% slow). The noise bandwidth is r/K: 25.9 Hz at
  25 WPM (K = 58), 10·log₁₀(252/25.9) = 9.9 dB less noise than the channel
  filter passes (derived).
- **Following speed (heuristic):** K starts at 60 WPM (K = 24, 62.5 Hz),
  the widest filter, and follows the dit passed to `set_dit`; the Matched
  decoder passes it once its speed window holds 8 marks (8 new ones after
  a re-acquisition), the filter's dit growing at most ×1.25 per mark from
  the 20 ms acquisition dit (section 8, "Two front ends"); K is clamped between the acquisition width
  (β·1.2 s/60 = 16 ms, K = 24) and the 5 WPM width (β·1.2 s/5 = 192 ms,
  K = 288), both computed from durations; a dit that is not finite or
  not positive is ignored. When K changes, σ̂_v² is
  scaled by K_old/K_new (derived for white noise at r; the channel filter
  removes the boxcar's sidelobes beyond ±150 Hz, keeping about 0.92 of the
  boxcar's noise power at K = 24 and 0.965 at K = 58, so the rescale and
  the a² formula below are off by 0.36 dB at K = 24 and 0.15 dB at
  K = 58, dB relative to the true noise power, derived). The last 2K + 1
  values of |v|² that the noise guard compares (below) and the floor's
  samples are rescaled by the same factor.
- **Re-acquisition (heuristic):** after a silence (section 8) the decoder
  calls `reacquire()`: K returns to 24 (60 WPM, main lobe ±62.5 Hz), ŝ²
  and its weight return to 0, and σ̂_v² is kept (rescaled); if nothing is
  keyed within 2 s the width returns to what it was. Without it the
  filter stays at the last station's width (±21–26 Hz main lobe, nulls
  near 25 and 50 Hz) and ŝ at its level, and a station answering there,
  or 6 dB weaker (re the first station's key-down power), is never keyed
  (simulated). After it, a station Δf away loses |sinc(Δf·24/1500 s)|² at
  K = 24 (−2.4 dB relative to a centered station at 25 Hz, derived) and
  must pass the acquisition squelch, so it needs about S₅₀₀ ≥ 0 dB at
  25 Hz.
- **Likelihood (derived; Proakis eq. 4.5–21):** Λ = −a²/2 + ln I₀(a·x),
  x = |v|/σ̂_v, a = ŝ/σ̂_v, in nats; ln I₀ from Abramowitz & Stegun 9.8.1–9.8.2
  without overflow. g = Λ + ln(P₁/P₀) with P₁ = 0.44 (derived from PARIS:
  key-down 22 of 50 dit units); p = 1/(1 + e^(−g)). For noise flat across
  the filter, a² = 2·S₅₀₀·(500 Hz)·K/r, S₅₀₀ as a linear ratio (derived):
  a = 6.2 at S₅₀₀ = 0 dB and 25 WPM.
- **Amplitude estimate (heuristic running form of an EM update):**
  ŝ² ← max(0, ŝ² + p·max(α_a, 1/W_a)·(|v|² − 2σ̂_v² − ŝ²)), the Rician mean
  square being 2σ_v² + s²; τ_a = 0.5 s of key-down weight. Samples on the
  filter's ramps bias ŝ low: 0.79 of s for dits alone (derived, noise-free
  trapezoid at K = 58), 0.85–0.88 of s for PARIS at 25 WPM, S₅₀₀ 0–60 dB
  (simulated). The decision point near ŝ/2 then lengthens each mark by
  about 7 ms at 25 WPM (3.5 ms per edge, simulated).
- **Noise estimate (heuristic form; derived bias correction):** three
  taps K apart, v[n], v[n−K] and v[n−2K], share no inputs, so in white
  noise they are independent. The middle one updates
  σ̂_v² ← σ̂_v² + max(α_n, 1/W_n)·(|v[n−K]|²/(2·m(κ)) − σ̂_v²), τ_n = 2 s of
  updates, only if it has |v|²/(2σ̂_v²) < κ = 1.75 and its two neighbors
  < κ_n = 4. A mark or
  a filter ramp within K of the middle tap lifts some tap above that (at
  S₅₀₀ = 60 dB by orders of magnitude). In noise, η = |v|²/(2σ_v²) is
  exponential with mean 1 (Proakis eq. 2.3–43), and the accepted middle
  tap is η truncated at κ, with mean m(κ) = 1 − κ·e^(−κ)/(1 − e^(−κ)) =
  0.632 at κ = 1.75 (derived), which the update divides out. The estimate
  never reads p, g or ŝ. With ρ = σ̂_v²/σ_v², in noise alone its only
  stable fixed point is ρ = 1 (derived: the map ρ ↦ m(κρ)/m(κ) has slope
  κ·m′(κ)/m(κ) = 0.651 < 1 at ρ = 1 and slope κ/(2m(κ)) > 1 near 0, so
  ρ = 0 is an unstable fixed point and it cannot settle low). Its climb
  back from a low ρ is slow, though: at ρ it accepts a fraction
  (1 − e^(−κρ))(1 − e^(−κ_n ρ))² of the samples, so from ρ = 0.25 (a rise
  in the noise of 6 dB relative to the previous noise power) the expected
  return to ρ = 0.9 takes about 43 s (derived by integrating
  dρ/dt = (m(κρ)/m(κ) − ρ)·acceptance(ρ)/τ_n); the relaxed neighbor
  guard κ_n = 4 cuts it from 98 s (κ_n = κ) to 43 s (derived the same
  way). This replaces a guard on the posterior, which selected quiet
  stretches, biased σ̂_v low and in noise alone settled at σ̂_v = 0.53–0.57·σ_v
  in 7 of 10 seeds, keying noise (plan review 2026-09-27; simulated).
  With the floor and warm-up below (simulated, numpy seeds, final check
  2026-09-28): noise alone (40 seeds × 120 s) σ̂_v/σ_v has mean 1.00 and
  standard deviation 0.020 at K = 24 and 0.032 at K = 58 (0.080 at
  K = 288, 20 seeds, re-review), with no signal flag; with continuous
  PARIS σ̂_v is 0.98–1.06·σ_v at S₅₀₀ 0–60 dB and 25 WPM (0.90–1.11·σ_v at
  12 WPM), and the leak of weak marks lifts it to 1.16–1.23·σ_v at
  S₅₀₀ = −5 dB and 1.22–1.30·σ_v at S₅₀₀ = −8 dB (25 WPM); after a rise in
  the noise of 6 dB relative to the previous noise power the
  last signal flag came at most 16 s later and σ̂_v returned to 0.9·σ_v within
  24–41 s at K = 24 (40 seeds), 25–51 s at K = 58 and 24–56 s at K = 288
  (20 seeds each), in line with the derived 43 s.
- **Floor (heuristic; its bound derived):** every K samples the 10th
  percentile Q of the last 64 samples of |v|² taken K apart gives a
  floor F = Q / (2·(−ln(1 − 0.1/c))·2.5), c = 0.25. In noise Q is
  2σ_v²·(−ln 0.9); with a station leaving a clean fraction of at least c of
  those samples, Q is at most noise's (0.1/c)-quantile, so F ≤ σ_v²/2.5
  (derived), and the factor 2.5 covers the sampling spread of a
  64-sample quantile (heuristic). **Why c = 0.25 (inputs derived,
  rounding heuristic):** a sample is clean when its K-sample window lies
  inside a space, so a gap of g dits leaves g − β dits clean; continuous
  text at K = 0.8·dit leaves 0.29–0.33 of the samples clean (PARIS 0.33,
  a CQ call 0.29, a contest exchange and a pangram 0.31; computed from
  the keyed envelopes), and 0.24–0.28 with the speed estimate 25% slow
  (K = one dit); c = 0.25 rounds the lower end down. Solid digits
  ("0000 9999", 0.18) and two stations keying at once are not covered;
  an earlier c = 0.4 was above continuous PARIS's 0.34 and made the floor
  lift routinely under strong text (final check 2026-09-28). If σ̂_v² falls
  below F, σ̂_v² ← F and the noise weight W_n drops to at most the number
  of samples in 10.67 ms (16 at r = 1500 samples/s; heuristic), so the
  next updates count for more; ŝ restarts only if F > 4·σ̂_v² (the factor
  **heuristic**), the stuck-low case in which the low σ̂_v has let ŝ grow on
  noise. A smaller lift is the quantile's spread or a second station, and
  restarting ŝ there refit it from a few samples on a filter ramp and
  merged dits (simulated: 38 edits in 7140 characters at S₅₀₀ = 60 dB
  with c = 0.4 and every lift restarting ŝ, 0 now; a neighbor 100 Hz
  away keying at the same time, +10 dB re the wanted station's key-down
  power, garbled the wanted station in 12 of 100 seeds, 2 now). In
  noise F = 0.0825·σ_v², so the floor acts when σ̂_v < 0.29·σ_v and restarts ŝ
  when σ̂_v < 0.14·σ_v (derived). This removes the stuck-low state (a start or
  a noise rise that leaves σ̂_v that low) and bounds large rises: after a
  rise of 10 or 20 dB relative to the previous noise power it lifted in
  every seed and σ̂_v was back within 0.9·σ_v in 11–38 s (20 seeds,
  K = 24). It does not act after a rise of 6 dB relative to the previous
  noise power, whose
  recovery is the guard's climb (above). During the climb noise can be
  flagged as signal for up to about 16 s (simulated).
- **Warm-up (heuristic):** the first 0.32 s only collect |v|², always at
  the acquisition width (K = 24; a `set_dit` meanwhile takes effect when
  the warm-up ends, and restarts ŝ). 0.32 s is 20 filter lengths, about 20
  independent samples. Then σ̂_v² = (20th percentile)/(2·(−ln 0.8)) (derived
  for noise alone) and ŝ² = max(0, 90th percentile − 2σ̂_v²), and the
  warm-up counts as 0.1 times its length in both noise and amplitude
  weight. (The earlier 5th percentile of 0.2 s, about 12 independent
  samples at K = 24 and 1 at K = 288, often started σ̂_v at 0.2–0.5·σ_v.)
- **Squelch (heuristic value; its scaling with the filter duration
  derived):** p is forced to 0 while a < a_min = 3·(T_v/16 ms)^(1/4),
  T_v = K/r the filter's duration and 16 ms the acquisition filter's
  (K = 24 at r = 1500 samples/s, so a_min = 3·(K/24)^(1/4) there). In
  noise alone â² is a p-weighted mean over about τ_a·r/K independent
  samples, so its spread grows as √K, and a_min ∝ T_v^(1/4) keeps the chance that noise alone passes the
  squelch the same at every K (derived, Gaussian approximation); a flat
  a_min = 3 would pass noise more often at long K. With a² =
  2·S₅₀₀·(500 Hz)·K/r the squelch is S₅₀₀ = −2.5 dB at K = 24 (any speed),
  −4.4 dB at K = 58 (25 WPM), −6.0 dB at K = 120 (12 WPM), −7.9 dB at
  K = 288 (5 WPM) (derived), and up to 1.4 dB higher with ŝ's ramp bias
  (ŝ = 0.85·s: 20·log₁₀(1/0.85) = 1.4 dB, derived). **A station is first
  keyed at the acquisition width**, so the sensitivity floor for
  acquiring one is S₅₀₀ = −2.5 dB derived at every speed; in simulation
  (continuous PARIS, 10 seeds) 0%, 3%, 45% and 80% of marks were keyed
  at S₅₀₀ = −4, −3, −2 and −1 dB at 25 WPM, and 19%, 33% and 95% at
  S₅₀₀ = −4, −3 and −2 dB at 12 WPM, so 50% is reached near S₅₀₀ = −1.8 dB
  and −2.6 dB. The lower
  figures hold only for a station already acquired and narrowed to. In
  noise alone, with σ̂_v correct, the amplitude update multiplies ŝ² by
  about e^(−P₁) = 0.64 per τ_a of key-down weight W_a (derived to first
  order in a²: E[p·(|v|² − 2σ_v² − ŝ²)] ≈ −P₁²·ŝ², and W_a grows by P₁ per
  sample); in wall-clock time, with p ≈ P₁, that is e^(−P₁²) = 0.82 per
  0.5 s. So a decays toward 0; this holds only because σ̂_v does not
  depend on a (above).
- **Correlated samples (heuristic):** successive outputs of a K-sample
  boxcar share inputs; their noise autocorrelation is triangular and sums
  to K (derived). Each `FrontEndSample` carries weight 1/K, the factor a
  sequence decoder may apply to Λ before summing over samples, keeping the
  0.67 ms timing resolution. Scaling a nonlinear per-sample LLR by the
  correlation length is an approximation (the sufficient statistic for one
  element is one matched-filter sample); the HMM plan may decimate at a
  stride of K instead. The classical decoder keys from g sample by sample
  and does not sum, so it ignores the weight.

## 9. Timing and latency

| Stage | Delay |
|---|---|
| Hop (processing block) | 21.3 ms |
| Detection | 1 s warm-up at recording start; then ~0.5–1.5 s for a new station (averaging + 0.5 s persistence) |
| Channel filter group delay | 10.7 ms |
| Decoder smoothing | ~¼ dit (τ_s) |
| Matched filter group delay (Matched only) | (K − 1)/2 samples: 19 ms at 25 WPM |
| Character emitted | after a 2-dit gap follows it |
| Track removed | average decay (~6–7 s for a 20 dB SNR (500 Hz) station) plus the 10 s timeout |

## 10. Parameters at a glance

| Parameter | Value | Where | Status |
|---|---|---|---|
| Bin width Δf | ~23 Hz (rule: largest power-of-two N with Δf ≥ 20 Hz) | `choose_fft_size` (engine.cpp) | heuristic |
| Hop / overlap | N/2, 50% (largest hop keeping Hann constant-sum) | engine.cpp | standard choice |
| Spectrum window | periodic Hann (ENBW 1.5 bins = 35.2 Hz) | spectrum.cpp | standard choice |
| Power average τ | 1 s (≈ 94-frame boxcar) | `DetectorConfig::average_s` | heuristic |
| Detection threshold / hysteresis | 6 dB / 3 dB SNR per bin (35.2 Hz) | `DetectorConfig` | heuristic |
| Noise floor | median of all bins | signal_detector.cpp | heuristic |
| Detection warm-up | 1 s from the first frame (equal to the average's τ) | `DetectorConfig::average_s` | heuristic |
| Persistence before a track | 0.5 s | `DetectorConfig::birth_s` | heuristic |
| Candidate tracking | may move ±23 Hz (1 bin at 23.4 Hz) between frames | `DetectorConfig::candidate_step_hz` | heuristic |
| Track timeout | 10 s | `DetectorConfig::death_s` | heuristic |
| Track following and attribution (Matched path) | each track follows its own peak within D = 47 Hz (at least 3 dB above the floor); a new peak within D of a track's current frequency belongs to it | `DetectorConfig::attribution`, `attribution_distance_hz` (set from `EngineConfig::channel_distance_hz`) | heuristic (owner decisions 2026-09-29, option 1) |
| Attribution (Envelope path) | frequency fixed at birth; peaks less than 3 bins (70 Hz) from a track's bin belong to it | `DetectorConfig::min_separation_bins` | heuristic (milestone 1) |
| Peak neighborhood | ±47 Hz (±2 bins at 23.4 Hz) | `DetectorConfig::peak_radius_hz` | heuristic |
| Track level neighborhood | ±23 Hz (±1 bin at 23.4 Hz) | `DetectorConfig::level_radius_hz` | heuristic |
| Tracker anchor (Matched path) | the detector's current frequency for the track, set before every channel block; the tracker fine-tunes within ±12 Hz of it | `Decoder::set_frequency_anchor_hz`; `FrequencyTrackerConfig::fine_tune_hz` | heuristic (owner decisions 2026-09-29, option 1) |
| Max tracks | 200 | `DetectorConfig::max_tracks` | heuristic |
| Channel bins / decimation | 64 / D = N ÷ 64 (r = 1500 Hz) | `EngineConfig::channel_bins` | heuristic within derived bounds |
| Channel filter cutoff | ±150 Hz (−6 dB relative to the passband) | `EngineConfig::channel_cutoff_hz` | heuristic |
| Channel filter | Blackman-windowed sinc, N/2+1 taps (21.3 ms) | channelizer.cpp | standard choice; length derived from the hop |
| Cutoff limit | cutoff + ½·5.5·fs/taps ≤ r/2 | channelizer.cpp | derived |
| Envelope smoothing | ¼ dit | `ClassicalDecoderConfig::smoothing_dits` | heuristic |
| Level attack / decay | 4 ms / 3 s | `ClassicalDecoderConfig` | heuristic, test-confirmed |
| Squelch | mark ≥ 3 × space | `ClassicalDecoderConfig::squelch_ratio` | heuristic, test-confirmed |
| Key thresholds | 60% down / 40% up | classical_decoder.cpp | heuristic |
| Glitch limit | 0.3 dit | `ClassicalDecoderConfig::glitch_dits` | heuristic |
| Dit/dah boundary and width | 2 dits, 0.08 (log) | classical_decoder.cpp | heuristic |
| Character / word gap | > 2 / > 5 dits | classical_decoder.cpp | standard midpoints |
| Speed window / range | 24 marks, 5–60 WPM | classical_decoder.cpp | heuristic |
| Edge-shortening ratio band | 3.0–3.85 | classical_decoder.cpp | measured |
| Front end | Matched (default) or Envelope | `ClassicalDecoderConfig::front_end`; `--front-end` | owner decision 2026-09-29 |
| LLR keying hysteresis (Matched) | g > +1 nat down, g < −1 nat up | `ClassicalDecoderConfig::llr_hysteresis` | heuristic |
| Filter follows speed after (Matched) | 8 marks in the speed window (8 new ones after a re-acquisition) | `ClassicalDecoderConfig::follow_after_marks` | heuristic |
| Dit-estimate growth bound (Matched) | at most ×1.25 per mark while the filter follows the speed; the filter's own dit also grows at most ×1.25 per mark from its first follow step (from the 20 ms acquisition dit) | `ClassicalDecoderConfig::max_dit_growth` | heuristic (owner decisions 2026-09-29) |
| Re-acquisition (Matched) | after max(0.5 s, 12 dits) of key-up: filter back to 60 WPM, ŝ, the frequency average and the speed window restart; the old speed window and the narrow filter come back if nothing is keyed within 2 s | `ClassicalDecoderConfig::reacquire_after_dits`, `reacquire_min_s`, `reacquire_window_s` | heuristic |
| Matched filter | boxcar, K = round(0.8·dit·r), starts at 60 WPM (16 ms, K = 24), clamped to 16–192 ms (60–5 WPM; K = 24–288) | `MatchedFrontEndConfig` | derived shape; β and start heuristic |
| Likelihood | Λ = −a²/2 + ln I₀(a·x); prior P₁ = 0.44 | matched_front_end.cpp | derived |
| Amplitude / noise estimates | τ_a = 0.5 s (EM, p-weighted) / τ_n = 2 s (middle tap below κ = 1.75, neighbors below κ_n = 4, truncation mean 0.632 divided out) | `MatchedFrontEndConfig` | heuristic; the truncation correction derived |
| Noise floor | 10th percentile of 64 samples of \|v\|² taken K apart, over 2·(−ln(1 − 0.1/0.25))·2.5; a lift caps W_n at 10.67 ms of samples and restarts ŝ only if the floor exceeds 4·σ̂_v² | `MatchedFrontEndConfig::floor_*` | heuristic; the occupancy bound derived, c = 0.25 from computed clean fractions of continuous text |
| Front-end warm-up | 0.32 s at K = 24; 20th / 90th percentiles, weight 0.1 × its length | `MatchedFrontEndConfig::warmup_s` | heuristic |
| Front-end squelch | a ≥ 3·(T_v/16 ms)^(1/4), T_v = K/r the filter duration (3·(K/24)^(1/4) at r = 1500 samples/s) | `MatchedFrontEndConfig::squelch_a`, `squelch_exponent` | 3 heuristic; the duration scaling derived |
| Frequency discriminator lag | τ_L = 8/r = 5.333 ms (`lag_s` = 5.33 ms rounded to whole samples at r = 1500 samples/s); unambiguous range ±1/(2τ_L) = ±93.75 Hz | `FrequencyTrackerConfig::lag_s` | heuristic within derived range |
| Frequency average | τ_f = 0.5 s of key-down weight; moves the NCO at weight ≥ 0.6 and coherence > 0.3, every 21.3 ms | `FrequencyTrackerConfig` (`tau_s`, `min_weight`, `min_coherence`, `update_interval_s`) | heuristic |
| Fine-tuning range | ±12 Hz around the anchor (the detector's frequency for the track); farther estimates are discarded; the NCO jumps to an anchor more than 12 Hz away | `FrequencyTrackerConfig::fine_tune_hz` | heuristic (owner decision 2026-09-29, option 1) |
| NCO range | ±75 Hz | `FrequencyTrackerConfig::max_offset_hz` | heuristic |

## 11. Definitions used in tests and the benchmark

- **SNR** (synthetic recordings): key-down carrier power A² (for a fading
  signal, its mean) over the noise power in a **500 Hz** bandwidth,
  σ²·500 Hz / fs. The generator adds complex
  white noise across the whole sampled span with σ = 0.02 FS. See section 5
  for converting to the detector's per-bin SNR.
- **Transmissions** (synthetic recordings): a signal may send its text
  several times (`repeats`), with `pause_s` seconds of silence between the
  last key-up of one sending and the first key-down of the next. The labels
  file lists each sending as a transmission with its start and end time, s;
  the signal's reference text is the sendings joined by word spaces. A
  signal may start at 0 s, the first sample of the recording.
- **Tune-up carrier:** an unkeyed carrier of `tune_s` seconds before the
  first sending, followed by 0.5 s of silence. It is not part of the
  reference text.
- **Drift:** the carrier's frequency changes linearly at `drift_hz_per_s`
  (Hz/s) from `freq_offset_hz` at the signal's start time t₀; its phase is
  2π·(f·t + ½·ḟ·(t − t₀)²).
- **Keying styles** (synthetic recordings, `keying`): every element and
  space duration is T·exp(N(μ, σ_ln²)), T = 1.2 s / WPM, never below
  0.2 dit. "machine" is exact PARIS timing (σ_ln = 0; the default). The
  others are VE3NEA's DeepCW styles with his parameters (MIT; notes in
  `docs/research/deepcw-generator-notes.md` §1):

  | Style (his name) | μ: dit, dah, element space, character space, word space | σ_ln: same order |
  |---|---|---|
  | computer (Computer) | 0, 1.10, 0, 1.10, 1.94 | 0.05, 0.016, 0.05, 0.016, 0.008 |
  | paddle (Paddle) | 0, 1.10, 0, 1.10, 1.94 | 0.05, 0.016, 0.05, 0.2, 0.2 |
  | bug (Vibroplex) | 0, 1.10, 0, 1.10, 1.94 | 0.05, 0.2, 0.05, 0.2, 0.2 |
  | hand (HandKey) | 0, 1.50, 0, 1.50, 2.0 | 0.15, 0.3, 0.2, 0.3, 0.2 |

  Medians exp(μ): 1, 3.00, 1, 3.00, 6.96 dits; hand key 1, 4.48, 1, 4.48,
  7.39 dits. Character and word spaces are one draw each (VE3NEA sums
  several draws; same medians). `imbalance_dits` = δ/T lengthens every mark
  and shortens every space; VE3NEA draws δ ~ N(0, (0.1·T)²) once per
  operator, and his training mix is hand 0.25, paddle 0.50, computer 0.25.
  `wpm_end` changes the speed within a sending: `step` switches at the
  middle word, `ramp` changes linearly from word to word.
- **Fading** (synthetic recordings, `fading_hz`, `fading_shape`): flat
  Rayleigh fading. The carrier is multiplied by a complex Gaussian gain
  g(t) with E|g|² = 1. `fading_hz` is the frequency spread f_D, Hz. The
  Doppler power spectrum is Gaussian with f_D = 2σ (`gaussian`, the
  default; the Watterson / CCIR 520 HF convention) or VE3NEA's DeepCW
  spectrum S(f) ∝ 1/(1 + (f/f_c)⁴), f_c = 0.625·f_D (`butterworth`), whose
  Gaussian least-squares fit has 2σ = 1.01·f_D, so f_D means the same
  spread to about 1%. The Butterworth has heavier tails (0.92% of the
  power beyond 2·f_D, against 6.3×10⁻⁵). The gain is synthesized at
  50 samples/s and linearly interpolated (at most 0.04 dB, Gaussian, or
  0.06 dB, Butterworth, of power lost relative to the mean between points
  at f_D = 3 Hz, derived).
- **Interferers and tags:** a labeled signal with `score: false` is an
  interferer. The benchmark matches it to its track (so that track is not
  counted as a false track) but leaves it out of every error rate and of
  detection recall. `tag` names the condition a signal represents
  (for example `df 100 Hz, +10 dB re wanted key-down power`), and suite summaries group by it.
- **Two-station QSOs** (synthetic recordings, `senders`, `overs`): the two
  stations of a QSO are one labeled signal, as a receiver hears them on one
  channel when they are close; `station_labels` also labels each station
  as its own signal at its own carrier, for stations heard as two tracks.
  The answering station's offset is drawn, where a suite draws it, with
  |Δf_B| in 0–10 Hz with probability 0.40, 10–50 Hz 0.30, 50–100 Hz 0.15
  and 100–200 Hz 0.15 (**heuristic**: zero-beat by ear versus sidetone and
  RIT mismatch; no measured distribution yet). Over k is keyed at its sender's speed, keying style and
  imbalance, on its sender's carrier (`freq_offset_hz` + `offset_hz`, Hz)
  at its sender's level (S₅₀₀ + `relative_db`, dB, noise in 500 Hz), with
  its sender's own carrier phase and fading path; a silence drawn uniformly
  from `turn_s` (default 0.5–2.0 s) separates two overs. The labels list
  each over as a transmission with its sender, speed, style, imbalance,
  offset and level, so the first word of every over is scored.
- **Keying edges** (synthetic recordings, `edge_s`, `edges_centered`):
  raised-cosine rise and fall of `edge_s` (default 5 ms). By default each
  edge lies inside its mark, so a mark is `edge_s` shorter, and a space
  `edge_s` longer, at 50% amplitude than its nominal length: an imbalance
  of −5 ms/T, −0.10 dit at 24 WPM (derived). With `edges_centered`, edges
  are centered on the mark's ends and the 50%-amplitude length is the
  nominal one (VE3NEA's convention; he uses 2 ms edges).
- **Character error rate (CER):** the minimum number of symbol insertions,
  deletions and substitutions to turn the decoded text into the reference,
  divided by the number of reference symbols. A prosign token counts as one
  symbol, and word spaces count as symbols.
- **Where the edits are** (spec §5.4): the benchmark takes one minimum-edit
  alignment (ties broken, from the end, as match or substitution, then
  deletion, then insertion) and charges each edit to one reference symbol:
  a substitution or deletion to its own symbol, an insertion to the
  reference symbol it precedes (the last one after the end). An edit that
  involves a word space on either side is a **space edit**; the rest are
  **character edits**. Reported alongside CER, each summed over scored
  signals before dividing:
  - **character CER** = character edits / reference symbols that are not
    word spaces;
  - **space error rate** = space edits / reference word spaces;
  - **first-word CER** = edits charged to the first word of each
    transmission / the symbols of those words.
  Character and space edits add up to the CER's edit count. The benchmark
  also reports, for each transmission (for a QSO, each over): its reference
  symbols and the edits charged to them, and the same pair for its first
  word.
  **Where an edit lands when the alignment is ambiguous is a heuristic, and
  it biases first-word and per-transmission rates upward.** Walking the
  trace back from the end and preferring a match or substitution over a
  deletion or insertion (the tie-break above) means that among several
  minimum-edit alignments, matches are pushed as late as possible and
  deletions/insertions as early as possible — so an ambiguous edit is always
  charged to the *earliest* reference symbol it could belong to, never a
  later one. Two consequences: (1) when the reference text repeats (for
  example a CQ sent twice) and a later repetition is the one actually lost,
  the missing symbols are still charged to the first occurrence, so
  first-word CER for that transmission can read 100% even though the first
  word was copied correctly; first-word and per-transmission CER are
  therefore an **upper bound**, not an exact attribution, whenever the text
  repeats or whole words are dropped. (2) An insertion decoded before a
  transmission's own start (for example noise the decoder read as characters
  before the true key-down) is charged to that transmission's first
  reference symbol, i.e. counted against its first word, because insertions
  are charged to the reference symbol they precede. Because insertions can
  outnumber a short first word's own symbols, first-word CER (and the
  per-transmission and space rates, by the same mechanism) can exceed 1.
- **No-space CER** (VE3NEA's metric): the Levenshtein distance between
  reference and decoded symbols with every word space removed, over the
  reference symbols that are not word spaces. It differs from character
  CER when a character and a word space trade places.
- **Message text** (synthetic recordings, `training/kz4ap_synth/messages.py`):
  CQ calls, contest exchanges, and whole ragchew QSOs as a list of overs,
  each with its sending station: CQ, answer, RST and name and QTH, rig and
  power and antenna and weather, optional chat, closing. `<BT>` separates
  thoughts inside an over; every over before the closing ends with `<AR>` and
  `<KN>` (the answer with `<AR>`); each station's closing over ends with `<SK>`.
  Templates and callsigns are this project's (heuristic). Filler text draws
  i.i.d. characters and word lengths from VE3NEA's on-air tables (DeepCW,
  MIT; E is 11.9% of characters, mean word length 3.06 characters).
- **CPU time per channel-second:** the process's CPU time (user plus
  kernel) for the whole run divided by the total duration of channel output
  delivered to decoders, summed over channels, in ms per channel-second. It
  includes the shared FFTs and the detector, so it is an upper bound on the
  per-channel cost; `decoder_ms_per_channel_s` counts only steady-clock time
  inside decoders (the engine runs on one thread). Measured on a desktop; a
  Raspberry Pi 5 is not yet measured.
- **Suites** (`training/kz4ap_synth/suites.py`): `smoke` is the CI
  recording; `full` covers sensitivity (oracle, S₅₀₀ −10 … +20 dB at 12, 25
  and 40 WPM), fading anchored to VE3NEA's DeepCW benchmark (his
  Butterworth spectrum, f_D grid 0.1, 0.3, 1, 3 Hz, his ten SNR points as
  S₅₀₀ = his 3 kHz key-on SNR + 7.78 dB, his styles, imbalance, style mix
  and text statistics, and his 2 ms centered edges; compare his curves
  with the no-space CER, his metric), fists, speed changes, interference,
  tuning offsets and drift, whole ragchew QSOs (one station's speed and
  style for both sides), two-station QSOs 0–200 Hz apart with each
  operator's own speed, style, imbalance and level, strong signals, pauses, tune-up carriers, stations
  present from the first sample, crowded bands and a whole band.
  **Per-over CER** (QSOs): the edits charged to an over's reference
  symbols by the benchmark's alignment, over those symbols; the word space
  between two overs belongs to neither.
  **Intervals:** every rate and crossing in the summary carries a
  bootstrap 95% interval over signals (1000 resamples; errors cluster
  within a signal, so signals are the units), and front ends are compared
  signal by signal on the same recordings. The full suite is sized for
  3 seeds: at least 1000 characters per S₅₀₀ point in groups A–C, and at
  least 100 fade times per point at f_D = 0.1 Hz.
  **QSO regimes** (group H): same-track for an answering station within
  2 FFT bins (46.9 Hz) of the caller, ambiguous below 3 bins (70.3 Hz, the
  detector's minimum peak separation), separate-track beyond; each QSO is
  scored with one label for the QSO and with one label per station. The
  summary marks with † the view that does not fit: through the detector,
  by the regime (labels per station for same-track, labels per QSO for
  separate-track); with oracle channels, by the channel's passband, whose
  response relative to the passband is −1.17 dB at 100 Hz from its center,
  −6.02 dB at 150 Hz and −18.0 dB at 200 Hz (measured, section 7): labels
  per QSO fit when the answering station is less than 150 Hz from the
  caller, labels per station otherwise.
  **Summary precision:** no bootstrap interval is printed for a row with
  fewer than 2 signals, and an S₅₀₀ crossing is computed only when every
  S₅₀₀ point holds at least 2 signals (groups that draw S₅₀₀ per signal,
  band and crowded, have one per point and get none). A crossing at the
  lowest point, where no point fails, is printed "≤ x dB". First-word and
  per-over CER are upper bounds (see "Where the edits are" above).
  **S₅₀₀ at a CER threshold:** for a condition with at least three S₅₀₀
  points, CER per point is pooled over its stations; scanning down from the
  highest S₅₀₀, the first point above the threshold and the one above it
  bracket the crossing, interpolated linearly in dB. No crossing is
  reported if the highest point already fails; if none fails, the lowest
  point is reported (an upper bound).
