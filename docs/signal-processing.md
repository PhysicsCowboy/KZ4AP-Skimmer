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
| K | matched-filter (boxcar) length, round(β·dit·r), samples | 58 at 25 WPM |
| β | matched-filter length as a fraction of the dit | 0.8 |
| v[n] | matched-filter output, complex, FS | |
| σ_v, s | noise RMS per real component of v (the complex noise power of v is 2σ_v²), and v's key-down amplitude, FS | |
| x, a | normalized envelope \|v\|/σ_v and amplitude s/σ_v (dimensionless) | |
| Λ | log-likelihood ratio, key-down over key-up: −a²/2 + ln I₀(a·x), nats | |
| g, p | posterior log-odds Λ + ln(P₁/P₀), nats, and the posterior probability of key-down, 0…1 | |
| P₁ | prior probability of key-down | 0.44 |
| f_off | a station's (carrier's) offset from its channel's center, Hz | |
| f̂ | frequency tracker's estimate of f_off, and the NCO frequency, Hz | |
| f_a | the tracker's anchor: where the station is, as the detector says (its frequency for the track minus the channel center), Hz | |
| τ_L | lag of the frequency discriminator, s, rounded to whole samples; unambiguous range ±1/(2τ_L) | τ_L = 8/r = 5.333 ms (±93.75 Hz) |
| τ_f | time constant of the frequency average, s of key-down weight 1 | 0.5 s |
| D_ch | channel distance: a detector track follows its own peak within D_ch of its current frequency, and a new peak within D_ch of a track belongs to it (Matched path, `Attribution::Distance`), Hz | 47 Hz |
| f_sep | separation of two stations' carriers (for example an answering station's offset from the caller), Hz | |

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
**channel distance D_ch = 47 Hz** of an existing track's *current* frequency
is attributed to that track instead (below, "Channel distance"). The
milestone-1 rule, a peak less than **3 bins** (70 Hz) from a track's bin,
is still selectable (`Attribution::Bins`) and is what the Envelope path
(`--front-end envelope`) uses. The neighborhoods are stated in Hz and
converted to bins from the actual bin width (`DetectorConfig`
`peak_radius_hz`, `candidate_step_hz`). **Status: all heuristic.**

### Why neighboring bins come up at all

Averaging is per bin and never merges bins. Neighbors enter only the decision
"is this a new station?" (the ±47 Hz peak rule, ±2 bins at 23.4 Hz, on both
paths; then the attribution rule, which differs by path: the 3-bin rule on the
Envelope path, `Attribution::Bins`, and the channel distance D_ch = 47 Hz
between interpolated frequencies on the default Matched path,
`Attribution::Distance`), the level of an existing track (its bin ±1, below)
and, on the Matched path only, which peak a track follows (below, "Channel
distance"). These rules undo spreading: one station puts power in 2–4
adjacent bins because of the Hann window's main lobe, its position between
bins, and its keying sidebands. Without them, one station would become
several tracks.

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

One distance, **D_ch = 47 Hz** (`EngineConfig::channel_distance_hz`;
**heuristic**, owner decisions 2026-09-29, "option 1"), decides with the
Matched path which station each channel follows. The detector alone
decides:

1. **Following:** every frame, before its level is read, each track moves
   to the strongest bin that is a peak by the birth rule (the maximum
   within ±47 Hz), stands at least 3 dB above the floor (the keep-alive
   level), and whose interpolated frequency is within D_ch of the track's
   current frequency. With none, it holds its frequency.
2. **Attribution:** a new peak within D_ch of a track's current frequency is
   that track's (above); one farther away can become a track of its own.
3. **The channel's tracker fine-tunes** within ±12 Hz of the detector's
   frequency for its track (section 7); the engine passes it before every
   channel block. So in a QSO turnover within D_ch, when the detector's
   peak moves to the answering station, the decoder's anchor and NCO move
   there, and back; the channel itself (its center, the bin nearest the
   track's birth frequency) does not move. Channels are never merged.

Why 47 Hz: about the half-width of the detector's Hann main lobe, 2/T_w
for its 42.7 ms window (46.9 Hz); a peak closer than that to a station can
be that station's own spread. Stated in Hz, it does not change with the
FFT size.

Simulated (plan, 2026-09-29; A at S₅₀₀ = 15 dB, 25 WPM; B answering at
18 WPM, its level in dB re A's key-down power; 30 seeds each): within D_ch
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
- **Two tracks can converge on one peak (possible; derived from the code,
  not observed).** Following (item 1, `SignalDetector::follow_peaks`)
  moves each track to the strongest qualifying peak within D_ch of its
  own frequency without checking whether another track already holds
  that peak, and channels are never merged. Two tracks born just over
  D_ch apart (for example stations at the crowded group's 50 Hz spacing)
  could both move onto one station's peak if the other station falls
  silent and its track's frequency lies within D_ch of that peak (a keyed
  station's interpolated frequency moves from frame to frame; by how much
  has not been measured); they
  would then follow the same station, with two decoders printing the same
  text, until one dies. No benchmark run has been checked for it: the
  crowded group's Matched runs report 0 false tracks (all 12 recordings,
  3 seeds), but that count would not show it, since each converged track
  was matched to its own label by its birth frequency, the only frequency
  the results record. Group H's drawn QSO at 53.9 Hz may be a case
  (section 11, "QSO regimes"). Backlog: "Tracks converging on one peak".

### Cap

At most **200** tracks (`DetectorConfig::max_tracks`, a config parameter not
yet exposed to users). When the cap is reached, a new candidate replaces the
weakest track if it is stronger; otherwise it is ignored. **Status:
heuristic**, a guard against CPU overload, not a measured limit. For scale
(derived): the physical ceiling is set by the attribution rule, about
fs / D_ch = 192 000 Hz / 47 Hz ≈ 4090 tracks at 192 kHz on the default
Matched path, and fs / 70.3 Hz ≈ 2730 with the Envelope path's 3-bin rule
(3 × 23.4 Hz); a busy contest can put more than 100 stations in 192 kHz.

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
its channel, well inside the flat passband. The two front ends treat that
offset differently:

- **Envelope path** (`--front-end envelope`): the offset appears as slow
  phase rotation, which this decoder ignores (it uses only the magnitude).
- **Matched path** (the default): the decoder's frequency tracker
  re-centers the station before the dit-matched filter (next subsection,
  "Frequency re-centering"; median residual 0.11 Hz measured in group F,
  S₅₀₀ = 5 dB).

Ignoring it on the Envelope path is harmless **only because the channel
filter is wide**. A filter
matched to an element of duration T_el (noise bandwidth exactly 1/T_el for
a rectangular element) scales a tone offset by f_off (Hz) by
|sinc(f_off·T_el)| in amplitude, where sinc(x) = sin(πx)/(πx) (**derived**, from Proakis &
Salehi, *Digital Communications*, 5th ed., eq. 4.5–28;
`docs/research/proakis-ook-notes.md`, section 2.7). At ±11.7 Hz a
dit-matched filter would lose 5.1 dB at 25 WPM and 8.8 dB at 20 WPM (signal
power, relative to a centered station). So a narrow filter needs each
station re-centered to a fraction of a bin first: the Matched path does
this with its tracker ahead of its dit-matched filter (next subsection);
the Envelope path does not re-center, and the planned narrow second-stage
filter (backlog: "Channel filtering, two stages") would need the same
re-centering in front of it.

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
  average for those samples; the bias is bounded by the figures above
  (derived) and has not been measured.
- **Expected accuracy:** about 0.5 Hz RMS at S₅₀₀ = 0 dB and 0.9 Hz at
  S₅₀₀ = −5 dB, 25 WPM (derived, an upper bound); a simulation of the
  whole chain (NCO, K = 58 boxcar, posterior weights, 60 s of PARIS,
  4 seeds; plan review, 2026-09-27) gave 0.08, 0.27 and 0.55 Hz RMS at
  S₅₀₀ = +10, 0 and −5 dB (simulated), valid once a station has been
  acquired, which needs S₅₀₀ ≥ −2.5 dB (derived) at any speed, in
  simulation 50% of marks keyed near S₅₀₀ = −1.8 dB at 25 WPM
  (section 8b, "Squelch"); a linear
  drift of ḟ Hz/s is followed with a lag of about ḟ·τ_f/P₁ (1.1 Hz at
  1 Hz/s, P₁ = 0.44, derived; 1.49–1.63 Hz simulated at the end of the
  last mark through the whole engine). Target (spec §5.2): within ±2 Hz,
  a loss of 0.2 dB relative to a centered station at 20 WPM through a
  filter of length T.
- **Measured accuracy (benchmark, milestone 2, part 1):** in group F
  (oracle channels on the nearest bin, stations 0, 2.9, 5.9, 8.8 and
  11.7 Hz from the bin center, 20 and 25 WPM, 3 seeds, 30 signals per
  S₅₀₀ point; section 11, "Frequency error"), |f_tracked − f_true| had a
  median of 0.11 Hz (90th percentile 0.21 Hz, largest 0.51 Hz) at
  S₅₀₀ = 5 dB and 0.13 Hz (0.28 Hz, 0.57 Hz) at S₅₀₀ = 0 dB, within the
  ±2 Hz target at every signal; the Envelope path, which does not
  re-center, is off by the bin rounding (median 5.9 Hz, largest 11.7 Hz).
  Through the detector (band, crowded, strong, pauses, tune-up, first
  sample) the median per condition was 0.0–0.2 Hz. The re-centering's accuracy is
  therefore **measured**; its parameters (τ_f, the gates, the lag) stay
  heuristic.
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
2026-09-29, option 1): the decision is that it grows from the 20 ms
acquisition dit by at most ×1.25 per mark, because the estimate at that
step may rest on up to 7 unbounded marks (in simulation a truncated first
mark gave a 110 ms estimate, the filter jumped from K = 24 to 146 samples
and ran away).
**Defect, reported to the owner; not fixed in this milestone (measured,
milestone 2, part 1, Task 14): the code applies both bounds per speed
update, not per mark, against the owner's decision.** The bound is applied
in `update_speed()`, which runs at every key-up counted for speed. When the
filter widens at a key-up, the wider boxcar's window still covers the mark
that just ended, so its output rises again and the key goes down within
0.3 dit. The dropout merge in `key_down()` (step 6) then pops the last mark
from the speed window but does not restore `dit_s_` or `filter_dit_s_`, so
the next key-up re-measures the longer mark and applies ×1.25 again. In
instrumented runs (debug prints, not committed) this happened 7 times on
one mark: the filter's dit went 20 → 25 → 31.2 → 39.1 → 48.8 → 61.0 →
76.3 → 95.4 ms (20 ms × 1.25⁷ = 95.4 ms) within 35 ms. The speed window
then holds the stretched mark, not the true one. The merge also does not
decrement `marks_since_reacquire_`, so one physical mark can count twice
toward the 8 marks before the filter follows. Where the estimate is right
the effect is small; where it is wrong the filter jumps to the wrong width
at once. Section 8b, "Measured: Matched against Envelope", gives the cases
and their numbers; the fix is a backlog item ("Growth bound per mark").
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
  a re-acquisition), the filter's dit meant to grow at most ×1.25 per mark
  from the 20 ms acquisition dit (owner decision); as coded the bound is
  applied per speed update, and a widening that re-opens the mark just
  ended adds updates within that mark (a defect, measured and reported,
  section 8, "Two front ends"); K is clamped between the acquisition width
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
  (simulated). After it, a station f_sep away loses |sinc(f_sep·24/1500 s)|² at
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
  figures hold only for a station already acquired and narrowed to.
  (Measured on the benchmark's group A, filler text: CER 0.10 is reached
  at S₅₀₀ = 0.2, 2.7 and 3.1 dB at 12, 25 and 40 WPM, above this
  expectation; "Measured: Matched against Envelope" below. The squelch
  value stays heuristic.) In
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

### Measured: Matched against Envelope (milestone 2, part 1)

Full suite, 3 seeds, synthetic recordings (`kz4ap_synth.suites`, 117
recordings, 4.55 h of audio, 3.20 GB), on a 12th Gen Intel Core i7-12700H
(32 GB, Windows 11); generating took 60 min (another job shared the
machine for part of it) and scoring both front ends 5.0 min (nothing
else running). S₅₀₀: key-down carrier power over noise power in 500 Hz,
dB; every dB value in this subsection is an S₅₀₀ (or a difference of
S₅₀₀ values) unless it names another reference. Groups A–G and the "H, oracle" copy use oracle channels (detector
bypassed, channel on the nearest bin); the rest run the whole pipeline.
Parentheses: bootstrap 95% intervals over signals (1000 resamples); none
for a row of one signal. "Envelope" is the milestone-1 path
(`--front-end envelope`, unchanged from milestone 1 by the evidence in
section 11, "Smoke check"; CI bounds its smoke CER, it does not pin bit
identity); "Matched" is the
default. No parameter was tuned for these runs (owner, 2026-09-29).
Source: `build/suite/full3/summary.md` (not in the repository; rerun
with the commands in section 11, "Suites").

| Condition | Envelope: S₅₀₀ at CER 0.10 / 0.05 (dB) | Matched: S₅₀₀ at CER 0.10 / 0.05 (dB) |
|---|---|---|
| A, 12 WPM | 7.2 (6.8 to 7.4) / 7.7 (7.5 to 7.8) | 0.2 (−0.5 to 2.2) / 2.3 (−0.0 to 3.4) |
| A, 25 WPM | 5.1 (4.8 to 5.2) / 5.6 (5.5 to 5.7) | 2.7 (−0.0 to 10.4) / 3.4 (1.7 to 11.2) |
| A, 40 WPM | 6.0 (5.9 to 15.2) / 14.8 (7.0 to 15.7) | 3.1 (2.0 to 3.6) / 3.7 (3.3 to 12.4) |

Each row below is one tag of the summary; for group H the tag names the
regime and the answering station's offset. † marks the group-H view that
does not fit (section 11). A row pools every S₅₀₀ point of its condition
(groups B, C and G sweep S₅₀₀), so a row can favor Matched while its
high-S₅₀₀ points favor Envelope ("Where Matched is worse" below).

| Condition | Envelope: CER (interval) / character CER / space error rate / first-word CER | Matched: same | Matched − Envelope, paired (interval) |
|---|---|---|---|
| B fading: VE3NEA mix | 0.927 (0.811 to 1.054) / 0.967 / 0.802 / 1.632 | 0.754 (0.725 to 0.783) / 0.786 / 0.653 / 0.708 | −0.191 (−0.297 to −0.085) |
| B fading: hand 24 wpm fD 0.1 Hz | 0.697 (0.595 to 0.830) / 0.683 / 0.741 / 1.305 | 0.616 (0.579 to 0.658) / 0.597 / 0.674 / 0.582 | −0.084 (−0.206 to +0.006) |
| B fading: hand 24 wpm fD 0.3 Hz | 0.758 (0.657 to 0.884) / 0.770 / 0.718 / 1.866 | 0.669 (0.642 to 0.698) / 0.680 / 0.635 / 0.636 | −0.088 (−0.197 to +0.002) |
| B fading: hand 24 wpm fD 1 Hz | 0.937 (0.796 to 1.093) / 0.948 / 0.903 / 1.788 | 0.747 (0.728 to 0.768) / 0.775 / 0.663 / 0.698 | −0.192 (−0.345 to −0.070) |
| B fading: hand 24 wpm fD 3 Hz | 1.147 (0.974 to 1.354) / 1.136 / 1.183 / 2.468 | 0.786 (0.768 to 0.806) / 0.821 / 0.678 / 0.826 | −0.365 (−0.533 to −0.205) |
| B fading: paddle 12 wpm fD 0.1 Hz | 0.852 (0.558 to 1.220) / 0.881 / 0.759 / 1.694 | 0.543 (0.496 to 0.592) / 0.559 / 0.493 / 0.810 | −0.311 (−0.689 to −0.029) |
| B fading: paddle 24 wpm fD 0.1 Hz | 0.615 (0.478 to 0.765) / 0.650 / 0.507 / 0.948 | 0.589 (0.538 to 0.640) / 0.598 / 0.563 / 0.491 | −0.025 (−0.139 to +0.084) |
| B fading: paddle 24 wpm fD 0.3 Hz | 0.782 (0.647 to 0.940) / 0.832 / 0.629 / 1.538 | 0.653 (0.611 to 0.693) / 0.683 / 0.562 / 0.608 | −0.126 (−0.263 to −0.008) |
| B fading: paddle 24 wpm fD 1 Hz | 1.013 (0.860 to 1.181) / 1.054 / 0.885 / 3.024 | 0.752 (0.731 to 0.776) / 0.797 / 0.611 / 0.659 | −0.263 (−0.436 to −0.118) |
| B fading: paddle 24 wpm fD 3 Hz | 1.126 (0.966 to 1.312) / 1.123 / 1.134 / 2.225 | 0.816 (0.794 to 0.840) / 0.849 / 0.713 / 0.850 | −0.310 (−0.462 to −0.166) |
| B fading: paddle 40 wpm fD 0.1 Hz | 0.766 (0.696 to 0.842) / 0.804 / 0.645 / 0.866 | 0.602 (0.545 to 0.655) / 0.612 / 0.570 / 0.681 | −0.164 (−0.233 to −0.099) |
| C fists: bug imbalance +0.0 | 0.385 (0.286 to 0.513) / 0.402 / 0.328 / 0.174 | 0.505 (0.402 to 0.608) / 0.509 / 0.490 / 0.594 | +0.117 (−0.043 to +0.269) |
| C fists: bug imbalance +0.1 | 0.396 (0.301 to 0.516) / 0.403 / 0.372 / 0.214 | 0.724 (0.627 to 0.810) / 0.732 / 0.700 / 0.900 | +0.317 (+0.158 to +0.450) |
| C fists: bug imbalance -0.1 | 0.372 (0.269 to 0.487) / 0.375 / 0.358 / 0.453 | 0.304 (0.219 to 0.398) / 0.311 / 0.280 / 0.500 | −0.067 (−0.212 to +0.049) |
| C fists: computer imbalance +0.0 | 0.106 (0.023 to 0.222) / 0.100 / 0.124 / 0.044 | 0.004 (0.001 to 0.009) / 0.003 / 0.005 / 0.221 | −0.096 (−0.202 to −0.021) |
| C fists: computer imbalance +0.1 | 0.139 (0.030 to 0.255) / 0.135 / 0.152 / 0.017 | 0.037 (0.001 to 0.107) / 0.033 / 0.049 / 0.169 | −0.096 (−0.242 to +0.033) |
| C fists: computer imbalance -0.1 | 0.288 (0.113 to 0.507) / 0.283 / 0.304 / 0.063 | 0.006 (0.001 to 0.014) / 0.007 / 0.004 / 0.111 | −0.285 (−0.524 to −0.106) |
| C fists: hand imbalance +0.0 | 0.302 (0.261 to 0.351) / 0.184 / 0.729 / 0.250 | 0.314 (0.268 to 0.368) / 0.247 / 0.554 / 0.344 | +0.011 (−0.035 to +0.063) |
| C fists: hand imbalance +0.1 | 0.351 (0.301 to 0.406) / 0.245 / 0.727 / 0.544 | 0.424 (0.359 to 0.496) / 0.348 / 0.694 / 0.574 | +0.069 (−0.017 to +0.156) |
| C fists: hand imbalance -0.1 | 0.324 (0.282 to 0.371) / 0.154 / 0.899 / 0.351 | 0.269 (0.239 to 0.302) / 0.155 / 0.659 / 0.257 | −0.056 (−0.104 to −0.009) |
| C fists: machine imbalance +0.0 | 0.017 (0.008 to 0.028) / 0.018 / 0.017 / 0.033 | 0.006 (0.001 to 0.016) / 0.006 / 0.005 / 0.167 | −0.012 (−0.021 to −0.005) |
| C fists: machine imbalance +0.1 | 0.022 (0.008 to 0.041) / 0.021 / 0.027 / 0.000 | 0.103 (0.001 to 0.216) / 0.100 / 0.114 / 0.259 | +0.088 (−0.015 to +0.206) |
| C fists: machine imbalance -0.1 | 0.046 (0.019 to 0.081) / 0.050 / 0.033 / 0.000 | 0.002 (0.001 to 0.004) / 0.002 / 0.001 / 0.132 | −0.044 (−0.076 to −0.019) |
| C fists: paddle imbalance +0.0 | 0.106 (0.043 to 0.220) / 0.088 / 0.170 / 0.061 | 0.047 (0.042 to 0.053) / 0.039 / 0.077 / 0.136 | −0.058 (−0.160 to +0.004) |
| C fists: paddle imbalance +0.1 | 0.106 (0.048 to 0.214) / 0.090 / 0.162 / 0.034 | 0.160 (0.087 to 0.248) / 0.156 / 0.172 / 0.241 | +0.060 (−0.080 to +0.179) |
| C fists: paddle imbalance -0.1 | 0.089 (0.056 to 0.128) / 0.060 / 0.186 / 0.062 | 0.037 (0.026 to 0.053) / 0.026 / 0.075 / 0.547 | −0.055 (−0.097 to −0.016) |
| D speed: 10 wpm | 0.052 (0.045 to 0.063) / 0.061 / 0.019 / 1.000 | 0.073 (0.046 to 0.121) / 0.072 / 0.077 / 1.333 | +0.021 (+0.000 to +0.064) |
| D speed: 60 wpm | 0.133 (0.005 to 0.367) / 0.130 / 0.142 / 0.833 | 0.073 (0.004 to 0.217) / 0.077 / 0.056 / 0.750 | −0.071 (−0.311 to +0.147) |
| D speed: ramp 15->30 | 0.202 (0.057 to 0.433) / 0.172 / 0.314 / 0.000 | 0.000 (0.000 to 0.000) / 0.000 / 0.000 / 0.000 | −0.192 (−0.411 to −0.059) |
| D speed: ramp 30->15 | 0.000 (0.000 to 0.000) / 0.000 / 0.000 / 0.000 | 0.000 (0.000 to 0.000) / 0.000 / 0.000 / 0.000 | +0.000 (+0.000 to +0.000) |
| D speed: step 20->35 | 0.525 (0.257 to 0.845) / 0.541 / 0.474 / 0.000 | 0.113 (0.058 to 0.163) / 0.121 / 0.088 / 0.000 | −0.412 (−0.694 to −0.140) |
| D speed: step 35->20 | 0.114 (0.061 to 0.160) / 0.096 / 0.179 / 0.250 | 0.133 (0.108 to 0.162) / 0.120 / 0.179 / 0.417 | +0.020 (−0.023 to +0.055) |
| E interference: df 100 Hz, +0 dB re wanted key-down power | 0.820 (0.808 to 0.837) / 0.901 / 0.547 / 1.000 | 0.006 (0.000 to 0.019) / 0.004 / 0.013 / 0.333 | −0.814 (−0.817 to −0.808) |
| E interference: df 100 Hz, +10 dB re wanted key-down power | 0.943 (0.775 to 1.113) / 1.062 / 0.566 / 0.818 | 0.391 (0.062 to 0.982) / 0.394 / 0.382 / 1.000 | −0.570 (−1.052 to +0.018) |
| E interference: df 100 Hz, +20 dB re wanted key-down power | 1.041 (0.940 to 1.155) / 1.171 / 0.559 / 1.900 | 0.794 (0.767 to 0.814) / 0.857 / 0.559 / 1.000 | −0.252 (−0.340 to −0.172) |
| E interference: df 100 Hz, -10 dB re wanted key-down power | 0.036 (0.000 to 0.056) / 0.023 / 0.090 / 0.000 | 0.000 (0.000 to 0.000) / 0.000 / 0.000 / 0.000 | −0.037 (−0.056 to +0.000) |
| E interference: df 150 Hz, +0 dB re wanted key-down power | 0.145 (0.027 to 0.331) / 0.152 / 0.120 / 0.000 | 0.000 (0.000 to 0.000) / 0.000 / 0.000 / 0.000 | −0.137 (−0.331 to −0.027) |
| E interference: df 150 Hz, +10 dB re wanted key-down power | 0.824 (0.818 to 0.829) / 0.921 / 0.521 / 1.000 | 0.000 (0.000 to 0.000) / 0.000 / 0.000 / 0.000 | −0.824 (−0.829 to −0.818) |
| E interference: df 150 Hz, +20 dB re wanted key-down power | 0.779 (0.491 to 1.008) / 0.860 / 0.521 / 2.333 | 0.548 (0.126 to 0.934) / 0.553 / 0.532 / 1.000 | −0.246 (−0.882 to +0.126) |
| E interference: df 150 Hz, -10 dB re wanted key-down power | 0.011 (0.000 to 0.029) / 0.007 / 0.023 / 0.000 | 0.000 (0.000 to 0.000) / 0.000 / 0.000 / 0.000 | −0.010 (−0.029 to +0.000) |
| E interference: df 20 Hz, +0 dB re wanted key-down power | 0.793 (0.654 to 1.101) / 0.846 / 0.600 / 0.667 | 0.872 (0.838 to 0.921) / 0.888 / 0.812 / 1.000 | +0.040 (−0.263 to +0.195) |
| E interference: df 20 Hz, +10 dB re wanted key-down power | 0.544 (0.480 to 0.640) / 0.656 / 0.123 / 6.333 | 0.885 (0.752 to 0.960) / 0.928 / 0.726 / 1.000 | +0.349 (+0.112 to +0.480) |
| E interference: df 20 Hz, +20 dB re wanted key-down power | 0.922 (0.822 to 1.131) / 0.979 / 0.707 / 2.667 | 0.788 (0.725 to 0.848) / 0.842 / 0.587 / 0.667 | −0.156 (−0.283 to +0.007) |
| E interference: df 20 Hz, -10 dB re wanted key-down power | 0.044 (0.000 to 0.081) / 0.033 / 0.080 / 0.000 | 0.003 (0.000 to 0.009) / 0.004 / 0.000 / 0.167 | −0.043 (−0.081 to +0.009) |
| E interference: df 50 Hz, +0 dB re wanted key-down power | 0.857 (0.807 to 0.930) / 0.912 / 0.651 / 1.833 | 0.064 (0.039 to 0.098) / 0.062 / 0.072 / 2.167 | −0.790 (−0.832 to −0.756) |
| E interference: df 50 Hz, +10 dB re wanted key-down power | 0.787 (0.500 to 1.009) / 0.878 / 0.467 / 1.000 | 0.814 (0.744 to 0.881) / 0.840 / 0.720 / 1.167 | +0.037 (−0.128 to +0.327) |
| E interference: df 50 Hz, +20 dB re wanted key-down power | 0.855 (0.640 to 1.040) / 0.867 / 0.813 / 1.000 | 1.123 (0.895 to 1.414) / 1.072 / 1.293 / 1.833 | +0.262 (−0.009 to +0.775) |
| E interference: df 50 Hz, -10 dB re wanted key-down power | 0.005 (0.000 to 0.014) / 0.003 / 0.011 / 0.000 | 0.000 (0.000 to 0.000) / 0.000 / 0.000 / 0.000 | −0.005 (−0.014 to +0.000) |
| F tuning: drift 0.2 Hz/s | 0.105 (0.038 to 0.168) / 0.106 / 0.100 / 0.000 | 0.071 (0.006 to 0.184) / 0.078 / 0.043 / 0.500 | −0.035 (−0.111 to +0.035) |
| F tuning: drift 0.5 Hz/s (not meaningful (oracle anchor) for Matched) | 0.073 (0.006 to 0.173) / 0.056 / 0.130 / 0.133 | 0.028 (0.021 to 0.035) / 0.024 / 0.043 / 0.333 | −0.065 (−0.163 to +0.023) |
| F tuning: drift 1 Hz/s (not meaningful (oracle anchor) for Matched) | 0.080 (0.014 to 0.171) / 0.078 / 0.086 / 0.267 | 0.486 (0.233 to 0.743) / 0.495 / 0.448 / 0.533 | +0.369 (+0.075 to +0.659) |
| F tuning: drift 2 Hz/s (not meaningful (oracle anchor) for Matched) | 0.113 (0.046 to 0.186) / 0.086 / 0.210 / 0.000 | 0.777 (0.669 to 0.860) / 0.805 / 0.677 / 0.583 | +0.663 (+0.503 to +0.805) |
| F tuning: offset 0 Hz 20 wpm | 0.416 (0.136 to 0.716) / 0.471 / 0.213 / 0.625 | 0.124 (0.023 to 0.250) / 0.136 / 0.082 / 0.812 | −0.304 (−0.605 to −0.005) |
| F tuning: offset 0 Hz 25 wpm | 0.412 (0.189 to 0.623) / 0.475 / 0.218 / 0.500 | 0.145 (0.025 to 0.264) / 0.160 / 0.100 / 0.583 | −0.266 (−0.465 to −0.094) |
| F tuning: offset 11.7 Hz 20 wpm | 0.404 (0.141 to 0.647) / 0.479 / 0.113 / 0.333 | 0.049 (0.017 to 0.095) / 0.037 / 0.094 / 0.667 | −0.333 (−0.593 to −0.095) |
| F tuning: offset 11.7 Hz 25 wpm | 0.420 (0.161 to 0.670) / 0.469 / 0.219 / 0.300 | 0.309 (0.077 to 0.593) / 0.316 / 0.281 / 2.050 | −0.094 (−0.268 to +0.050) |
| F tuning: offset 2.9 Hz 20 wpm | 0.486 (0.143 to 0.929) / 0.488 / 0.475 / 0.579 | 0.041 (0.003 to 0.112) / 0.044 / 0.033 / 0.789 | −0.479 (−0.922 to −0.137) |
| F tuning: offset 2.9 Hz 25 wpm | 0.394 (0.131 to 0.692) / 0.429 / 0.276 / 0.533 | 0.089 (0.012 to 0.169) / 0.094 / 0.072 / 0.467 | −0.320 (−0.585 to −0.092) |
| F tuning: offset 5.9 Hz 20 wpm | 0.393 (0.128 to 0.639) / 0.433 / 0.252 / 0.476 | 0.030 (0.006 to 0.069) / 0.039 / 0.000 / 0.286 | −0.354 (−0.612 to −0.108) |
| F tuning: offset 5.9 Hz 25 wpm | 0.386 (0.148 to 0.598) / 0.439 / 0.206 / 0.312 | 0.203 (0.017 to 0.454) / 0.216 / 0.161 / 1.500 | −0.168 (−0.395 to +0.024) |
| F tuning: offset 8.8 Hz 20 wpm | 0.492 (0.204 to 0.789) / 0.515 / 0.407 / 0.647 | 0.184 (0.008 to 0.417) / 0.187 / 0.176 / 0.294 | −0.333 (−0.690 to +0.132) |
| F tuning: offset 8.8 Hz 25 wpm | 0.405 (0.148 to 0.631) / 0.471 / 0.138 / 0.400 | 0.167 (0.055 to 0.286) / 0.171 / 0.154 / 1.000 | −0.226 (−0.471 to +0.020) |
| G ragchew: ragchew 25 wpm | 0.196 (0.111 to 0.284) / 0.203 / 0.174 / 0.240 | 0.103 (0.065 to 0.149) / 0.098 / 0.118 / 0.249 | −0.094 (−0.153 to −0.042) |
| H two-station QSO: ambiguous, drawn offset | 0.130 (0.087 to 0.164) / 0.092 / 0.237 / 0.486 | 0.484 (0.405 to 0.619) / 0.526 / 0.364 / 0.811 | +0.352 (+0.270 to +0.455) |
| H two-station QSO: ambiguous, offset 50 Hz | 0.062 (0.026 to 0.101) / 0.038 / 0.128 / 0.106 | 0.204 (0.058 to 0.370) / 0.195 / 0.232 / 0.392 | +0.142 (−0.020 to +0.325) |
| H two-station QSO: same-track, drawn offset | 0.116 (0.087 to 0.146) / 0.075 / 0.231 / 0.247 | 0.194 (0.136 to 0.251) / 0.167 / 0.269 / 0.272 | +0.078 (+0.038 to +0.123) |
| H two-station QSO: same-track, offset 0 Hz | 0.150 (0.074 to 0.216) / 0.099 / 0.298 / 0.261 | 0.252 (0.080 to 0.450) / 0.226 / 0.328 / 0.286 | +0.106 (−0.033 to +0.245) |
| H two-station QSO: same-track, offset 10 Hz | 0.103 (0.036 to 0.176) / 0.067 / 0.208 / 0.246 | 0.132 (0.039 to 0.233) / 0.106 / 0.208 / 0.209 | +0.028 (−0.000 to +0.079) |
| H two-station QSO: same-track, offset 25 Hz | 0.107 (0.035 to 0.216) / 0.067 / 0.221 / 0.377 | 0.218 (0.063 to 0.402) / 0.204 / 0.256 / 0.302 | +0.111 (−0.031 to +0.280) |
| H two-station QSO: separate-track, drawn offset † | 0.723 (0.702 to 0.746) / 0.722 / 0.723 / 0.738 | 0.789 (0.782 to 0.797) / 0.790 / 0.787 / 0.801 | +0.067 (+0.052 to +0.081) |
| H two-station QSO: separate-track, offset 100 Hz † | 0.428 (0.169 to 0.679) / 0.415 / 0.467 / 0.542 | 0.639 (0.492 to 0.769) / 0.656 / 0.591 / 0.750 | +0.213 (+0.086 to +0.345) |
| H two-station QSO: separate-track, offset 200 Hz † | 0.786 (0.770 to 0.799) / 0.789 / 0.778 / 0.886 | 0.806 (0.794 to 0.816) / 0.806 / 0.808 / 0.905 | +0.021 (+0.010 to +0.033) |
| H two-station QSO (per station): ambiguous, drawn offset | 0.992 (0.900 to 1.089) / 0.953 / 1.103 / 1.009 | 1.124 (0.738 to 1.569) / 1.121 / 1.134 / 3.342 | +0.131 (−0.220 to +0.532) |
| H two-station QSO (per station): ambiguous, offset 50 Hz | 0.947 (0.878 to 1.007) / 0.931 / 0.994 / 1.576 | 0.953 (0.859 to 1.048) / 0.904 / 1.092 / 2.631 | +0.005 (−0.102 to +0.121) |
| H two-station QSO (per station): same-track, drawn offset † | 0.973 (0.951 to 0.995) / 0.946 / 1.049 / 1.089 | 0.917 (0.882 to 0.951) / 0.920 / 0.906 / 1.044 | −0.053 (−0.086 to −0.024) |
| H two-station QSO (per station): same-track, offset 0 Hz † | 0.971 (0.902 to 1.036) / 0.940 / 1.062 / 0.765 | 0.864 (0.779 to 0.942) / 0.876 / 0.829 / 0.836 | −0.102 (−0.191 to −0.030) |
| H two-station QSO (per station): same-track, offset 10 Hz † | 0.960 (0.927 to 0.993) / 0.934 / 1.038 / 1.725 | 0.927 (0.872 to 0.978) / 0.921 / 0.945 / 1.160 | −0.031 (−0.072 to −0.000) |
| H two-station QSO (per station): same-track, offset 25 Hz † | 0.979 (0.912 to 1.071) / 0.949 / 1.063 / 1.400 | 0.868 (0.770 to 0.954) / 0.877 / 0.842 / 0.912 | −0.104 (−0.209 to −0.001) |
| H two-station QSO (per station): separate-track, drawn offset | 0.564 (0.548 to 0.582) / 0.575 / 0.533 / 0.715 | 0.620 (0.609 to 0.630) / 0.619 / 0.622 / 0.801 | +0.057 (+0.039 to +0.075) |
| H two-station QSO (per station): separate-track, offset 100 Hz | 0.673 (0.593 to 0.778) / 0.679 / 0.653 / 1.368 | 0.598 (0.542 to 0.648) / 0.594 / 0.609 / 1.033 | −0.068 (−0.199 to +0.034) |
| H two-station QSO (per station): separate-track, offset 200 Hz | 0.613 (0.603 to 0.623) / 0.619 / 0.597 / 0.725 | 0.641 (0.625 to 0.655) / 0.637 / 0.652 / 0.768 | +0.029 (+0.011 to +0.047) |
| H two-station QSO, oracle: ambiguous, offset 50 Hz (not meaningful (oracle anchor) for Matched) | 0.357 (0.029 to 0.973) / 0.351 / 0.373 / 0.078 | 0.597 (0.505 to 0.708) / 0.617 / 0.541 / 0.885 | +0.240 (−0.446 to +0.657) |
| H two-station QSO, oracle: same-track, offset 0 Hz | 0.322 (0.087 to 0.668) / 0.276 / 0.455 / 0.223 | 0.248 (0.076 to 0.449) / 0.219 / 0.331 / 0.223 | −0.069 (−0.538 to +0.239) |
| H two-station QSO, oracle: same-track, offset 10 Hz | 0.101 (0.036 to 0.176) / 0.066 / 0.205 / 0.217 | 0.128 (0.039 to 0.216) / 0.101 / 0.207 / 0.164 | +0.026 (−0.004 to +0.078) |
| H two-station QSO, oracle: same-track, offset 25 Hz (not meaningful (oracle anchor) for Matched) | 0.106 (0.033 to 0.211) / 0.064 / 0.222 / 0.335 | 0.476 (0.387 to 0.553) / 0.516 / 0.364 / 0.498 | +0.370 (+0.307 to +0.434) |
| H two-station QSO, oracle: separate-track, offset 100 Hz (not meaningful (oracle anchor) for Matched) | 0.336 (0.072 to 0.712) / 0.314 / 0.397 / 0.311 | 0.507 (0.405 to 0.626) / 0.528 / 0.449 / 0.575 | +0.169 (−0.277 to +0.420) |
| H two-station QSO, oracle: separate-track, offset 200 Hz † (not meaningful (oracle anchor) for Matched) | 0.468 (0.441 to 0.491) / 0.485 / 0.420 / 0.716 | 0.502 (0.479 to 0.532) / 0.497 / 0.518 / 0.588 | +0.035 (−0.006 to +0.066) |
| H two-station QSO, oracle (per station): ambiguous, offset 50 Hz † | 1.490 (1.011 to 2.189) / 1.472 / 1.541 / 1.894 | 1.073 (0.912 to 1.226) / 0.974 / 1.352 / 3.378 | −0.415 (−1.023 to +0.033) |
| H two-station QSO, oracle (per station): same-track, offset 0 Hz † | 1.461 (1.028 to 1.996) / 1.403 / 1.628 / 1.462 | 0.887 (0.782 to 0.997) / 0.906 / 0.834 / 1.660 | −0.581 (−1.104 to −0.189) |
| H two-station QSO, oracle (per station): same-track, offset 10 Hz † | 1.055 (0.971 to 1.151) / 0.999 / 1.221 / 2.561 | 0.987 (0.891 to 1.086) / 0.975 / 1.024 / 1.488 | −0.067 (−0.131 to −0.017) |
| H two-station QSO, oracle (per station): same-track, offset 25 Hz † | 1.115 (0.985 to 1.258) / 1.047 / 1.309 / 2.293 | 1.004 (0.835 to 1.192) / 0.979 / 1.075 / 2.098 | −0.109 (−0.325 to +0.080) |
| H two-station QSO, oracle (per station): separate-track, offset 100 Hz † | 1.449 (1.023 to 2.046) / 1.418 / 1.534 / 2.057 | 0.603 (0.437 to 0.758) / 0.531 / 0.803 / 1.486 | −0.852 (−1.452 to −0.374) |
| H two-station QSO, oracle (per station): separate-track, offset 200 Hz | 0.277 (0.178 to 0.384) / 0.232 / 0.402 / 1.223 | 0.076 (0.041 to 0.110) / 0.059 / 0.122 / 0.156 | −0.203 (−0.321 to −0.098) |
| strong: S500 30 dB | 0.034 (0.023 to 0.048) / 0.037 / 0.019 / 0.571 | 0.053 (0.045 to 0.060) / 0.056 / 0.038 / 0.857 | +0.019 (+0.011 to +0.024) |
| strong: S500 40 dB | 0.052 (0.047 to 0.056) / 0.050 / 0.056 / 0.867 | 0.055 (0.051 to 0.059) / 0.050 / 0.069 / 0.867 | +0.004 (+0.000 to +0.011) |
| strong: S500 50 dB | 0.028 (0.020 to 0.038) / 0.032 / 0.015 / 0.583 | 0.045 (0.041 to 0.049) / 0.054 / 0.015 / 1.000 | +0.018 (+0.011 to +0.023) |
| strong: S500 60 dB | 0.028 (0.023 to 0.037) / 0.036 / 0.000 / 0.583 | 0.048 (0.045 to 0.050) / 0.062 / 0.000 / 1.000 | +0.020 (+0.012 to +0.025) |
| pauses: pause 10 s | 0.144 (0.096 to 0.211) / 0.115 / 0.273 / 0.222 | 0.034 (0.032 to 0.036) / 0.042 / 0.000 / 0.333 | −0.114 (−0.179 to −0.062) |
| pauses: pause 2 s | 0.050 (0.023 to 0.092) / 0.048 / 0.061 / 0.167 | 0.038 (0.035 to 0.041) / 0.048 / 0.000 / 0.333 | −0.015 (−0.056 to +0.013) |
| pauses: pause 20 s | 0.644 (0.632 to 0.655) / 0.656 / 0.591 / 0.833 | 0.707 (0.699 to 0.714) / 0.702 / 0.727 / 1.000 | +0.064 (+0.046 to +0.080) |
| pauses: pause 5 s | 0.069 (0.035 to 0.112) / 0.067 / 0.076 / 0.222 | 0.039 (0.036 to 0.042) / 0.050 / 0.000 / 0.333 | −0.032 (−0.075 to +0.003) |
| tune-up: tune-up 0.3 s | 0.007 (0.000 to 0.014) / 0.009 / 0.000 / 0.125 | 0.014 (0.000 to 0.027) / 0.013 / 0.017 / 0.250 | +0.006 (+0.000 to +0.013) |
| tune-up: tune-up 0.6 s | 0.000 (0.000 to 0.000) / 0.000 / 0.000 / 0.000 | 0.032 (0.000 to 0.093) / 0.038 / 0.014 / 0.125 | +0.028 (+0.000 to +0.085) |
| tune-up: tune-up 1 s | 0.000 (0.000 to 0.000) / 0.000 / 0.000 / 0.000 | 0.542 (0.258 to 0.835) / 0.536 / 0.559 / 0.833 | +0.562 (+0.280 to +0.839) |
| tune-up: tune-up 2 s | 0.029 (0.006 to 0.067) / 0.018 / 0.068 / 0.000 | 0.909 (0.758 to 1.000) / 0.912 / 0.898 / 1.000 | +0.899 (+0.775 to +0.981) |
| first sample: from the first sample | 0.119 (0.097 to 0.147) / 0.111 / 0.145 / 0.833 | 0.138 (0.122 to 0.157) / 0.143 / 0.120 / 0.900 | +0.018 (−0.001 to +0.033) |
| crowded: spacing 0 Hz | 0.518 (0.409 to 0.620) / 0.531 / 0.469 / 1.370 | 0.249 (0.167 to 0.344) / 0.256 / 0.225 / 0.994 | −0.258 (−0.341 to −0.175) |
| crowded: spacing 100 Hz | 0.326 (0.223 to 0.430) / 0.342 / 0.271 / 0.876 | 0.052 (0.038 to 0.073) / 0.054 / 0.046 / 0.847 | −0.244 (−0.334 to −0.159) |
| crowded: spacing 200 Hz | 0.037 (0.030 to 0.047) / 0.039 / 0.030 / 0.749 | 0.082 (0.037 to 0.144) / 0.084 / 0.073 / 0.785 | +0.029 (−0.002 to +0.072) |
| crowded: spacing 50 Hz | 0.394 (0.286 to 0.503) / 0.414 / 0.327 / 1.141 | 0.136 (0.072 to 0.220) / 0.138 / 0.130 / 0.885 | −0.276 (−0.403 to −0.168) |
| band: band | 0.102 (0.041 to 0.177) / 0.107 / 0.084 / 0.808 | 0.054 (0.047 to 0.065) / 0.057 / 0.045 / 0.829 | −0.025 (−0.079 to +0.017) |

| Per over (groups G and H, from the "Per over" table) | Envelope: CER | Matched: CER |
|---|---|---|
| G ragchew, paddle (288 overs) | 0.197 | 0.104 |
| H two-station QSO, computer (120 overs) | 0.246 | 0.289 |
| H two-station QSO, hand (144 overs) | 0.306 | 0.495 |
| H two-station QSO, paddle (312 overs) | 0.218 | 0.301 |
| H two-station QSO, oracle, computer (68 overs) (not meaningful (oracle anchor) for Matched) | 0.305 | 0.331 |
| H two-station QSO, oracle, hand (68 overs) (not meaningful (oracle anchor) for Matched) | 0.296 | 0.590 |
| H two-station QSO, oracle, paddle (152 overs) (not meaningful (oracle anchor) for Matched) | 0.266 | 0.366 |

**Group H by regime** (detector run, grid and drawn offsets pooled by
`qso_regime`; the oracle copy in the second block; QSO-label CER scores
one label per QSO, station-label CER one label per station; the view that
does not fit the regime is marked †, as in section 11):

| Group H regime (tags) | Tracks per QSO, Envelope / Matched | QSO-label CER, Envelope / Matched | Station-label CER, Envelope / Matched |
|---|---|---|---|
| same-track (0, 10, 25 Hz; drawn), 45 QSOs | 1.00 / 1.00 | 0.118 (0.091 to 0.145) / 0.197 (0.149 to 0.251) | † 0.972 (0.951 to 0.992) / 0.904 (0.875 to 0.928) |
| ambiguous (50 Hz; drawn), 9 QSOs | 1.56 / 1.67 | 0.085 (0.051 to 0.119) / 0.297 (0.162 to 0.439) | 0.962 (0.908 to 1.015) / 1.010 (0.872 to 1.180) |
| separate-track (100, 200 Hz; drawn), 18 QSOs | 6.78 / 6.78 | † 0.646 (0.531 to 0.747) / 0.745 (0.683 to 0.799) | 0.616 (0.585 to 0.656) / 0.620 (0.597 to 0.636) |
| oracle copy, same-track (0, 10, 25 Hz), 18 QSOs | — | 0.176 (0.080 to 0.316) / 0.283 (0.181 to 0.380); Matched not meaningful at 25 Hz (oracle anchor) | † 1.210 (1.057 to 1.433) / 0.960 (0.883 to 1.038) |
| oracle copy, ambiguous (50 Hz), 6 QSOs | — | 0.357 (0.028 to 0.973) / 0.597 (0.501 to 0.701), Matched not meaningful (oracle anchor) | † 1.490 (0.991 to 2.182) / 1.073 (0.901 to 1.237) |
| oracle copy, separate-track (100, 200 Hz), 12 QSOs | — | 0.402 (0.242 to 0.583) / 0.505 (0.448 to 0.557), Matched not meaningful (oracle anchor); † at 200 Hz | 0.861 (0.561 to 1.226) / 0.338 (0.211 to 0.473); † at 100 Hz |

A separate-track QSO shows 6.78 tracks on both paths because each
station's track dies during the other's over and is re-born (section 11,
"Tracks per QSO"; backlog, "Tracks outlive their stations").

**First word of each over** (group H through the detector, grid offsets;
first-word CER is an upper bound, section 11; the answering station's
level is drawn from −6 to +6 dB re the caller's key-down power):

| Answering station's offset | Envelope: answer / caller first-word CER | Matched: answer / caller first-word CER | Matched: answer / caller over CER |
|---|---|---|---|
| 0 Hz (24 overs each) | 0.199 / 0.343 | 0.235 / 0.353 | 0.279 / 0.231 |
| 10 Hz | 0.213 / 0.287 | 0.147 / 0.287 | 0.172 / 0.099 |
| 25 Hz | 0.422 / 0.323 | 0.181 / 0.444 | 0.245 / 0.196 |
| 50 Hz | 0.032 / 0.204 | 0.444 / 0.323 | 0.345 / 0.086 |

Envelope's over CER for the same rows: answer 0.208, 0.124, 0.140, 0.044;
caller 0.104, 0.085, 0.076, 0.079.

| Group B against VE3NEA (no-space CER, his metric) | VE3NEA DeepCW | CW Skimmer (his measurement) | Envelope | Matched |
|---|---|---|---|---|
| Paddle, 24 WPM, f_D 0.1 Hz, S₅₀₀ 1.78 / 7.78 / 17.78 / 57.78 dB | 0.373 / 0.137 / 0.025 / 0.005 | 0.364 / 0.101 / 0.022 / 0.011 | 0.730 / 0.677 / 0.292 / 0.179 | 0.741 / 0.604 / 0.558 / 0.348 |
| HandKey, 24 WPM, f_D 0.1 Hz, S₅₀₀ 1.78 / 7.78 / 17.78 / 57.78 dB | 0.412 / 0.186 / 0.082 / 0.063 | 0.429 / 0.188 / 0.091 / 0.083 | 0.805 / 0.541 / 0.369 / 0.295 | 0.658 / 0.530 / 0.485 / 0.519 |

Pooled over 3 seeds, 9 stations per point: 1951, 1960, 1954 and 1932
reference characters per point (paddle) and 1927, 1964, 1952 and 1922
(hand key), the same for both front ends; VE3NEA's points hold 30 000.
Remaining differences from his benchmark: bin-centered oracle channels
instead of his ±30 Hz pitch error, complex I/Q noise instead of real
audio, and one draw per character and word space (he sums several); the
keying edges match his (2 ms, centered). Both of our decoders are far
from his at every point, and the gap does not close at high S₅₀₀: at
57.78 dB, where a fade to 30 dB below the mean power (probability 10⁻³
for Rayleigh fading, derived) still leaves S₅₀₀ = 28 dB, Envelope reads
0.179 and 0.295 and Matched 0.348 and 0.519. So the residual is not
noise; that it is the hard-decision timing (speed estimate and fixed
gap thresholds, section 8) under changing level is an inference, not
measured.

| | Envelope | Matched |
|---|---|---|
| CPU per channel-second (process, over the bench's timed window; section 11), ms/s | 0.202 | 0.381 |
| Decoders per channel-second, ms/s | 0.014 | 0.177 |
| Median frequency error, group F offsets, Hz | 5.9 (bin rounding; no re-centering) | 0.11 at S₅₀₀ = 5 dB, 0.13 at 0 dB (30 signals each) |

CPU is over 384 495 channel-seconds (Envelope) and 384 661 (Matched), on
the machine above; the Matched front end adds 0.163 ms of decoder time per
channel-second, 11.6 times the Envelope decoder's time (the Matched decoders'
total, 0.177 ms, is 12.6 times it), and nearly doubles the
whole process: outside the decoders (WAV reading, the shared FFTs, the
detector in the detector runs only, the bench's event subscriber;
section 11, "CPU time per channel-second") Envelope's process spends
0.202 − 0.014 = 0.188 ms/s. Most channel-seconds (340 465) come from
oracle runs, which skip the detector (section 11 splits the figures).

**Reading the results.**

- **Group A (sensitivity, oracle).** Matched crosses CER 0.10 at S₅₀₀ =
  0.2, 2.7 and 3.1 dB (12, 25, 40 WPM) against Envelope's 7.2, 5.1 and
  6.0 dB: 2.4–7.0 dB better by the point estimates, and the paired
  differences favor Matched at all three speeds (−0.588, −0.228, −0.086,
  intervals excluding 0). At 25 WPM the crossing intervals overlap
  (Envelope 4.8 to 5.2 dB, Matched −0.0 to 10.4 dB), so there the claim
  rests on the paired differences, not on the crossings.
  Envelope's crossings sit near its squelch's estimated +6 dB (section 8,
  step 5), as expected. Matched's are **above** the design's expectation
  of about −2.6 to 0 dB (acquisition floor −2.5 dB derived; "Squelch"
  above) at 25 and 40 WPM (2.7 and 3.1 dB); at 12 WPM (0.2 dB, interval
  −0.5 to 2.2 dB) the interval overlaps that range. Per point, the Matched CER at 25 WPM is 0.711 at
  −2 dB, 0.122 at 0 dB, 0.151 at 2 dB and 0.006 at 4 dB, so the crossing
  lies between 2 and 4 dB; 6 of 12 stations exceed CER 0.10 at 0 dB and
  2 at 2 dB (40 WPM: 12 and 7 of 12). The wide interval at 25 WPM (−0.0
  to 10.4 dB) comes from single stations that fail at high S₅₀₀ (1 of 12
  at 10 dB, CER 0.464; also 1 of 12 at 6, 10 and 12 dB at 40 WPM): these
  are the start-up failures described under "Limits measured" below. No
  run or scoring fault was found: every result file names the front end
  that made it, and the rebuilt bench reproduces the stored results
  exactly (checked on `A-awgn-25wpm-1-s1`).
- **Group F (tuning, oracle).** Matched's CER does not depend on the
  offset from the bin center beyond the intervals (20 WPM: 0.124, 0.041,
  0.030, 0.184, 0.049 at 0, 2.9, 5.9, 8.8, 11.7 Hz, every interval
  overlapping; 25 WPM: 0.145, 0.089, 0.203, 0.167, 0.309, likewise);
  Envelope's is 0.39–0.49 at every offset. The frequency error is within
  the ±2 Hz target for every signal (section 7). The drift rows show the
  **oracle-mode limit of option 1**: with no detector the anchor stays on
  the labeled starting frequency and the tracker covers only ±12 Hz, so a
  drift of 1 or 2 Hz/s over the 17.5–27 s signals (19–51 Hz) loses the
  decode (Matched CER 0.486 and 0.777 against Envelope's 0.080 and 0.113;
  "not meaningful (oracle anchor)"); at 0.5 Hz/s (up to 13.5 Hz) Matched
  still read 0.028. Through the detector the anchor follows the drift
  (Task 13's `MatchedReportsDriftingFrequency`); no suite group measures
  that yet.
- **Groups G and H (QSOs).** Ragchew (G, 25 WPM, oracle) crosses
  CER 0.10 at 6.9 dB (6.6 to 7.1 dB, Envelope) and 3.8 dB (3.4 to 5.3 dB,
  Matched), 1.8 and 1.1 dB above group A at 25 WPM: the QSO text
  (prosigns, abbreviations, pauses between overs) costs about 1–2 dB on
  both. At CER 0.05 the order reverses: Envelope 7.6 dB (7.5 to 7.7 dB),
  Matched 9.2 dB (no interval: fewer than 95% of the resamples reached
  0.05), because Matched keeps a residual CER of 0.013–0.033 more than
  Envelope at 8–20 dB (below). Two-station QSOs through the
  detector: same-track QSOs stay one track on both paths (1.00), and
  Matched is **worse** on the QSO label (0.197 against 0.118; paired
  +0.078, +0.038 to +0.123, drawn offsets). The answering station's first
  word, the retune delay's measure, is better with Matched at 10 and
  25 Hz (0.147 and 0.181 against 0.213 and 0.422) and worse at 0 Hz (0.235
  against 0.199); the option-1 simulation (Design decisions B) predicted a
  median of 0–1 character lost at 10–25 Hz for answering stations at −6 to
  +6 dB re the caller, consistent with these rates for 3–5 character
  first words (a first-word CER of 0.15–0.25 is one character in 4–7; an
  upper bound). At 50 Hz (ambiguous on both paths; on the Matched path it
  is 3 Hz beyond D_ch = 47 Hz, inside the band where interpolated
  frequencies can read closer than D_ch; section 11, "QSO regimes")
  Matched loses the answering station much more often on the QSO label
  (first word 0.444, over 0.345, against 0.032 and 0.044): per QSO label
  the Matched CER was 0.533, 0.154, 0.046, 0.415, 0.032 and 0.040
  (Envelope 0.023–0.140), so **2 of 6 grid QSOs at 50 Hz failed**
  (CER > 0.4). Those two are exactly the QSOs that **split into two
  tracks** on the Matched path (the other 4 stayed one track, the
  caller's track following the answering station to 49.8–50.0 Hz; tracks
  within 25 Hz of either carrier in
  `build/suite/full3/results/matched/H-qso-s*.json`): in them the caller's
  track stayed on the caller (last frequency 0.9 and 1.0 Hz from it) and
  the answering station's overs read CER 0.936 on it (first word 0.636),
  because the Matched channel is re-centered on the caller behind the
  dit-matched filter, whose main lobe is ±r/K = ±21–33 Hz at 20–32 WPM
  (derived); the Envelope caller channel (±150 Hz, no re-centering)
  decoded the answering station in its own 3 split QSOs (QSO-label CER
  0.023, 0.140, 0.028). In the 4 one-track QSOs Matched read 0.032–0.154
  on the QSO label (answering station's overs 0.044, first word 0.338, the
  retune delay; Envelope's 3 one-track QSOs 0.073 and 0.067). So the
  QSO-label loss at 50 Hz is the split, a QSO read through the view that
  does not fit it; the per-station view, which fits a split QSO, reads
  poorly at 50 Hz on both paths (0.947 Envelope, 0.953 Matched, all 12
  station labels), so these runs do not show how well Matched decodes the
  answering station on its own track, nor whether the dit-estimate
  runaway (the growth bound acts per update as coded, a defect, section
  8, "Two front ends"), which the design simulation's 12 of 30 failures
  before the first-step bound came from, still contributes. Separate-track
  QSOs: Matched is worse per station (paired +0.057, +0.039 to +0.075,
  drawn; +0.029, +0.011 to +0.047, 200 Hz). In the oracle copy, which
  shows the turnover apart from detection, the rows within ±12 Hz of the
  label (0 and 10 Hz) show no difference beyond the intervals (paired
  −0.069 and +0.026), and per station Matched is better wherever the view
  fits (200 Hz: 0.076 against 0.277).
- **Strong signals, pauses, tune-up, first sample (detector).** Strong
  (S₅₀₀ 30–60 dB): Matched is slightly worse at every level (paired
  +0.004 to +0.020, intervals excluding 0 at 30, 50 and 60 dB; CER
  0.045–0.055 against 0.028–0.052), with first-word CER 0.857–1.000
  against 0.571–0.867. Pauses: Matched is better after 10 s (0.034
  against 0.144) and equal within the intervals at 2 and 5 s; after 20 s
  both lose the station's track (0.707 and 0.644; paired +0.064, +0.046
  to +0.080). First sample: no difference beyond the interval (+0.018,
  −0.001 to +0.033). **Tune-up carriers of 1 s and 2 s break Matched**
  (CER 0.542 and 0.909 against 0.000 and 0.029; 0.3 and 0.6 s carriers
  are harmless); see "Limits measured".
- **Crowded and band (detector).** Matched is better at 0, 50 and 100 Hz
  spacing (paired −0.258, −0.276, −0.244) and not different beyond the
  interval at 200 Hz (+0.029, −0.002 to +0.072) or on the band (−0.025).

**Where Matched is worse than Envelope** (paired Matched − Envelope CER
whose 95% interval excludes 0, or a crossing worse beyond its interval;
the default stays Matched, owner decision 2026-09-29; no parameter was
changed):

- Per condition (summary.md): C fists, bug imbalance +0.1 (+0.317,
  +0.158 to +0.450); E, 20 Hz interferer at +10 dB re the wanted
  station's key-down power (+0.349, +0.112 to +0.480); H through the
  detector: same-track drawn (+0.078), ambiguous drawn (+0.352, +0.270
  to +0.455), separate-track per station (drawn +0.057, 200 Hz +0.029);
  strong at 30, 50, 60 dB (+0.018 to +0.020); pauses 20 s (+0.064);
  tune-up 1 s (+0.562) and 2 s (+0.899). Borderline (interval's lower
  bound rounds to +0.000): D 10 WPM (+0.021), strong 40 dB (+0.004),
  tune-up 0.3 s (+0.006) and 0.6 s (+0.028). G ragchew crosses CER 0.05
  at 7.6 dB (7.5 to 7.7 dB) for Envelope and 9.2 dB for Matched (no
  interval). C fists, paddle imbalance +0.1,
  crosses CER 0.10 for Envelope at 8.4 dB (5.0 to 9.5 dB) and not at all
  for Matched; machine imbalance +0.1 at ≤ 5.0 dB (Envelope) against
  8.4 dB (5.0 to 9.2 dB, Matched). (F drift at 1 and 2 Hz/s and the
  oracle H rows beyond ±12 Hz are worse too, but not meaningful: oracle
  anchor.)
- Per S₅₀₀ point (paired over the stations of a point: 12 in group A,
  9 in groups B and C, 6 in group G;
  31 of 209 points in groups A, B, C and G favor Envelope and 73 favor
  Matched; at 95% about 10 of 209 would exclude 0 by chance, so single
  points are weak evidence, a run of them is not): **group B at high
  S₅₀₀ with slow fading and random timing** — paddle 24 WPM f_D 0.1 Hz at
  17.78, 37.78 and 57.78 dB (+0.266, +0.137, +0.177), paddle 24 WPM
  f_D 0.3 Hz at 27.78, 37.78 and 57.78 dB (+0.142, +0.228, +0.141), paddle
  12 WPM f_D 0.1 Hz at 17.78–57.78 dB (+0.107 to +0.344), hand 24 WPM
  f_D 0.1 Hz at 17.78 and 57.78 dB (+0.086, +0.184), f_D 0.3 Hz at 27.78
  and 37.78 dB (+0.094, +0.108), the VE3NEA mix at 17.78 dB (+0.122); not
  at f_D = 1 or 3 Hz or at 40 WPM. **Group C at 10–20 dB with positive
  imbalance or hand keying** — bug +0.1 (+0.330 at 5 dB, +0.298 at
  20 dB), hand +0.0 and +0.1 at 20 dB (+0.143, +0.169), paddle +0.1 at 10
  and 20 dB (+0.033, +0.053). **Group G at 8–20 dB**, small (+0.013 to
  +0.033). The pattern: Matched's advantage is at low S₅₀₀; at high S₅₀₀
  with random keying timing it loses a few to 34 CER points. Its marks
  are about 7 ms longer at 25 WPM (ŝ's ramp bias, "Amplitude estimate"
  above) and a positive imbalance lengthens them further, shortening the
  element spaces the character decisions rest on; that this causes the
  loss is a hypothesis, not measured. The fading loss at high S₅₀₀ is not
  diagnosed.

**Limits measured** (diagnosed by focused runs and debug prints that are
not in the repository; nothing was changed):

- **A channel that opens mid-transmission.** On the smoke recording
  (`bench/smoke.sh`) Matched reads CER 0.0622 against Envelope's 0.0353
  and 0.0145 with oracle channels (Matched). One station, +7617.6 Hz,
  18.5 WPM (dit 64.9 ms), S₅₀₀ 25.1 dB, keying from 1.076 s, decodes at
  CER 0.234 through the detector: its channel opens at about 1.6 s, and
  its text starts "E ETT TT 7 L T U U A 7L" for "UA7L TU UA7L" and then
  recovers. Cutting the recording at 1.600 s and decoding that station
  with an oracle channel (which opens at the cut) reproduces the same
  text exactly, so the detector is not involved; cut at 1.620 s the
  Matched decode loses only the first word, sent partly before the cut
  (3 edits). Mechanism (from the code, confirmed with debug prints): the
  front end's warm-up (0.32 s, "Warm-up" above) keys nothing; cut at
  1.600 s it ended 15 ms before the end of the dah of U (1.725–1.919 s,
  plus about 18 ms of filter delay), so the first mark the decoder timed
  was a 15.3 ms fragment of it, longer than the 14.4 ms glitch limit
  (0.3 × the 48 ms initial dit at 25 WPM); cut at 1.620 s the warm-up
  ended just after that dah and no fragment was timed.
  With marks {15.3, 60.7 ms} the speed estimate (section 8, step 10)
  splits at the ratio 3.97, the fragment is the whole "dit" cluster, the
  dah/dit ratio exceeds 3.85, and the averaged estimate, 17.8 ms, is
  clamped to 20 ms (60 WPM). With a 20 ms dit, the station's 65 ms dits
  read as dahs, its element spaces end characters and its 195 ms
  character spaces read as word spaces. The fragment stays the only
  member of the dit cluster for the next 24 marks: the estimate rose
  through 29.8, 41.5 and 35–37 ms and the text recovered once the
  fragment left the window. How general it is (focused run: 4 stations
  per speed, machine keying, 30 s at 48 000 samples/s, noise seeds
  700–703, each recording cut at 20 random times in 2–6 s, so 80 oracle
  channels per speed open at random phases of the keying; stations whose
  Matched decode had at least 5 more edits than Envelope's, and at least
  5 fewer):

  | Speed | S₅₀₀ 25 dB: Matched worse / better | S₅₀₀ 10 dB: Matched worse / better |
  |---|---|---|
  | 12 WPM | 5 / 0 of 80 | 12 / 0 of 80 |
  | 18.5 WPM | 6 / 0 of 80 | 3 / 1 of 80 |
  | 25 WPM | 0 / 0 of 80 | 2 / 0 of 80 |
  | 40 WPM | 0 / 16 of 80 | 1 / 78 of 80 |

  So a late-opening channel hurts Matched at 12–18.5 WPM, in 4–15% of
  openings, and not at 25–40 WPM. Two failure shapes occur: the dit
  estimate stuck low (above), and a runaway to a slow speed (next item).
  In the suite's detector-path groups (band, crowded, strong, pauses,
  first sample: every channel opens mid-transmission) Matched was worse
  than Envelope by more than 0.1 CER for 3 of 36 stations at 10–15 WPM,
  2 of 35 at 15–20, 4 of 144 at 20–30, 1 of 114 at 30–45 and 3 of 91 at
  45–60 WPM, and better by more than 0.1 for 8, 4, 25, 20 and 30: a limit,
  not the dominant effect. Envelope did not fail on these openings (its
  warm-up lasts one dit, 48 ms; why its estimate survives was not
  traced). Replay ("Wrong or missing first
  characters", backlog) would remove the late opening itself.
- **Start-up runaway.** Three runs showed the same chain: (1) the first
  mark or two are misleading (a fragment, a first mark keyed late while
  ŝ rises from the warm-up, or merged elements at low S₅₀₀ through the
  16 ms acquisition filter); (2) at the 8th mark the filter starts
  following, and the widening cascade (section 8, "Two front ends")
  stretches the mark just ended; (3) with one mark of intermediate length
  in the window, no neighbor ratio reaches 1.8, the estimator takes the
  mean of dits and dahs as the dit (section 8, step 10), about twice the
  true dit, and the bound, applied ×1.25 per update (the defect), lets it grow there within
  one mark. Cases: A, 25 WPM (dit 48 ms), S₅₀₀ 10 dB, +6606.5 Hz in
  `A-awgn-25wpm-1-s1`: first mark 100.7 ms (a 144 ms dah keyed late),
  estimate correct (45.7 ms) for 8 marks, then the cascade stretched a
  dit to 62 ms and the estimate went 58.3 → 72.9 → 91.1 → 93.8 ms within
  23 ms; CER 0.464, recovering later. A, 25 WPM, S₅₀₀ 2 dB, −2991.9 Hz in
  `A-awgn-25wpm-0-s2`: merged elements gave an estimate of 105–120 ms
  before following began, the cascade widened the filter from 20 to
  119 ms within one mark, the 48 ms element spaces filled in, marks grew
  to 287–503 ms, the estimate to 210 ms, and decoding stopped (CER 0.981;
  the bound slowed the runaway, it did not stop it). The 18.5 WPM
  late-opening case above that read "DE K****V" followed the same chain
  from a 113.3 ms fragment.
- **Tune-up carriers of 1 s or more.** Cutting `tune-up-s1` so that an
  oracle channel opens during a 2 s carrier (at 1.2 or 2.0 s) or a 1 s
  carrier (at 1.6 s) gave no Matched text at all (CER 1.000); opening in
  the 0.5 s silence after the carrier gave CER 0.000; Envelope decoded
  every cut (CER 0.000–0.103). With the channel open from the start of
  the recording, the 1 s carrier did no harm (0.064) but the 2 s carrier
  still did (0.974). Through the detector the channel opens at least
  0.5 s after the station appears (section 6, persistence), so it likely
  opens during a 1–2 s carrier. Likely mechanism
  (from the code, not instrumented): a warm-up (0.32 s) that sees only
  carrier sets σ̂_v² from the carrier's own power, and a carrier longer
  than the noise floor's window (64 samples K apart, 1.02 s at K = 24)
  lifts the floor to the carrier's level; with σ̂_v² near the station's
  power, a = ŝ/σ̂_v stays below the squelch, and the station's own marks
  (|v|²/(2σ̂_v²) below κ = 1.75) pass the noise guard and hold σ̂_v up.
  Envelope's speed estimate excludes marks longer than 0.96 s (section 8,
  step 10) and its levels are set by one dit.

## 9. Timing and latency

| Stage | Delay |
|---|---|
| Hop (processing block) | 21.3 ms |
| Detection | 1 s warm-up at recording start; then ~0.5–1.5 s for a new station (averaging + 0.5 s persistence) |
| Channel filter group delay | 10.7 ms |
| Decoder smoothing | ~¼ dit (τ_s) |
| Front-end warm-up (Matched only) | the first 0.32 s of each channel key nothing (section 8b, "Warm-up"); a mark that ends just after it can be timed as a fragment (section 8b, "A channel that opens mid-transmission") |
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
| Track following and attribution (Matched path) | each track follows its own peak within D_ch = 47 Hz (at least 3 dB above the floor); a new peak within D_ch of a track's current frequency belongs to it | `DetectorConfig::attribution`, `attribution_distance_hz` (set from `EngineConfig::channel_distance_hz`) | heuristic (owner decisions 2026-09-29, option 1) |
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
| Dit-estimate growth bound (Matched) | decided: at most ×1.25 per mark while the filter follows the speed, and the filter's own dit at most ×1.25 per mark from its first follow step (from the 20 ms acquisition dit). As coded: ×1.25 per speed update in `update_speed()`; a dropout merge in `key_down()` pops the last mark without restoring `dit_s_`/`filter_dit_s_` or decrementing `marks_since_reacquire_`, so a mark re-opened by the widening filter is bounded again at each re-measure (7 updates on one mark measured, 20 → 95.4 ms) | `ClassicalDecoderConfig::max_dit_growth` | heuristic value (owner decisions 2026-09-29); **defect** in its application, measured, reported to the owner, not fixed in this milestone (section 8, "Two front ends") |
| Re-acquisition (Matched) | after max(0.5 s, 12 dits) of key-up: filter back to 60 WPM, ŝ, the frequency average and the speed window restart; the old speed window and the narrow filter come back if nothing is keyed within 2 s | `ClassicalDecoderConfig::reacquire_after_dits`, `reacquire_min_s`, `reacquire_window_s` | heuristic |
| Matched filter | boxcar, K = round(0.8·dit·r), starts at 60 WPM (16 ms, K = 24), clamped to 16–192 ms (60–5 WPM; K = 24–288) | `MatchedFrontEndConfig` | derived shape; β and start heuristic |
| Likelihood | Λ = −a²/2 + ln I₀(a·x); prior P₁ = 0.44 | matched_front_end.cpp | derived |
| Amplitude / noise estimates | τ_a = 0.5 s (EM, p-weighted) / τ_n = 2 s (middle tap below κ = 1.75, neighbors below κ_n = 4, truncation mean 0.632 divided out) | `MatchedFrontEndConfig` | heuristic; the truncation correction derived |
| Noise floor | 10th percentile of 64 samples of \|v\|² taken K apart, over 2·(−ln(1 − 0.1/0.25))·2.5; a lift caps W_n at 10.67 ms of samples and restarts ŝ only if the floor exceeds 4·σ̂_v² | `MatchedFrontEndConfig::floor_*` | heuristic; the occupancy bound derived, c = 0.25 from computed clean fractions of continuous text |
| Front-end warm-up | 0.32 s at K = 24; 20th / 90th percentiles, weight 0.1 × its length | `MatchedFrontEndConfig::warmup_s` | heuristic |
| Front-end squelch | a ≥ 3·(T_v/16 ms)^(1/4), T_v = K/r the filter duration (3·(K/24)^(1/4) at r = 1500 samples/s) | `MatchedFrontEndConfig::squelch_a`, `squelch_exponent` | 3 heuristic; the duration scaling derived |
| Frequency discriminator lag | τ_L = 8/r = 5.333 ms (`lag_s` = 5.33 ms rounded to whole samples at r = 1500 samples/s); unambiguous range ±1/(2τ_L) = ±93.75 Hz | `FrequencyTrackerConfig::lag_s` | heuristic within derived range |
| Frequency average | τ_f = 0.5 s of key-down weight; moves the NCO at weight ≥ 0.6 and coherence > 0.3, every 21.3 ms | `FrequencyTrackerConfig` (`tau_s`, `min_weight`, `min_coherence`, `update_interval_s`) | heuristic; the resulting accuracy measured (median 0.11 Hz at S₅₀₀ = 5 dB, 0.13 Hz at 0 dB, group F, section 7) |
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
  spectrum S(f) ∝ 1/(1 + (f/f_c)⁴), f_c = 0.625·f_D (`butterworth`;
  VE3NEA's parameter, taken from his code, not derived or measured here),
  whose Gaussian least-squares fit has 2σ = 1.01·f_D (measured by VE3NEA:
  his notebook fits a periodogram of one hour of simulated gain at
  f_D = 1 Hz and prints 2σ = 1.012 Hz; a fit to the analytic spectrum over
  ±20 Hz gives 1.016 Hz, computed; `docs/research/deepcw-generator-notes.md`),
  so f_D means the same spread to about 1–2%. The Butterworth has heavier
  tails: 0.91% of the power beyond 2·f_D, against 6.3×10⁻⁵ for the Gaussian
  (both derived, by integrating the two spectra; 2·f_D is 3.2·f_c and 4σ;
  the f⁻⁴ tail approximation gives 0.92%, the value `test_fading.py` checks
  within ±30%).
  The gain is synthesized at
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
  |f_sep| in 0–10 Hz with probability 0.40, 10–50 Hz 0.30, 50–100 Hz 0.15
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
  kernel) over the bench's timed window divided by the total duration of
  channel output delivered to decoders, summed over channels, in ms per
  channel-second. The timed window (`bench/src/main.cpp`, `cpu_started` to
  `cpu_s`) starts after the labels are read and the engine is built, and
  covers reading the WAV file block by block, the engine (the shared FFTs,
  the channels, the decoders and, except in oracle mode, the detector) and
  the bench's own event subscriber, which collects the decoded text; it
  ends after `finish()`. In oracle mode (`--oracle`; groups A–G and
  "H, oracle") the detector is skipped, though the windowed spectrum FFT
  still runs. So it is an upper bound on the per-channel cost, and oracle
  and detector runs are not comparable; `decoder_ms_per_channel_s` counts
  only steady-clock time inside decoders (the engine runs on one thread).
  Measured (full suite, 3 seeds, `build/suite/full3`, each recording's
  results file; section 8b's machine): over all recordings 0.202 ms/s
  (Envelope) and 0.381 ms/s (Matched); in the oracle runs (340 465
  channel-seconds) 0.163 and 0.324 ms/s, in the detector runs (44 031 and
  44 196 channel-seconds) 0.501 and 0.816 ms/s; decoders alone 0.014 and
  0.174 ms/s (oracle), 0.015 and 0.196 ms/s (detector). A Raspberry Pi 5 is
  not yet measured.
- **Smoke check** (`bench/smoke.sh`; CI runs it on Windows and Linux):
  generates the `smoke` recording (band scenario, 8 stations, 30 s,
  192 kHz, seed 1) and scores it twice on each path. The Envelope path
  (`--front-end envelope`) must meet `bench/baselines/smoke.json` (CER
  ≤ 0.09, detection recall ≥ 0.875) and the Matched path
  `bench/baselines/smoke-matched.json` (CER ≤ 0.07, recall ≥ 0.875); the
  two runs of each path must write byte-identical results (run-to-run
  determinism on one platform). **CI therefore bounds the Envelope CER; it
  does not pin bit-identity with milestone 1.** What establishes that the
  Envelope path is unchanged: the generator's frozen-copy tests
  (`test_default_signals_match_milestone_1_generator`,
  `test_band_defaults_match_milestone_1` in `training/tests/test_generate.py`:
  the band scenario and the generator's default output match frozen copies
  of milestone 1's code), reading the
  engine's Envelope path (final branch review, 2026-09-30), and its smoke
  CER, 34 edits in 964 symbols (0.0353, measured), the same as at Task 2,
  before any engine change. The Matched limit 0.07 (**heuristic**) is the
  measured 0.0622 = 60/964 (Windows) plus a margin of 3/482 = 6/964
  (0.0685), rounded up to two decimals: the check fails from 68 edits, a
  margin of 7 edits over the measured 60 (Envelope's 0.09 fails from 87
  edits, a margin of 52 over its 34). The Matched figure is dominated by one station's
  start-up (CER 0.234; section 8b, "A channel that opens
  mid-transmission"), which a platform difference that moves the channel's
  opening by one hop could change by tens of edits; the Linux value has
  not been measured yet (the branch has not run on CI).
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
  **QSO regimes** (group H): how the detector sorts a QSO's two stations
  into tracks depends on the front end's attribution rule (section 6), so
  the regime is judged per path (`suites.qso_regime`):
  - **Envelope path** (`Attribution::Bins`, the milestone-1 rule; derived
    from the 3-bin minimum peak separation at 23.4 Hz bins): same-track
    for an answering station within 2 bins (46.9 Hz) of the caller,
    ambiguous below 3 bins (70.3 Hz; it depends on where the stations fall
    within their bins), separate-track from 70.3 Hz. This is the regime
    word in every group-H tag (written when the recording is generated).
  - **Matched path** (`Attribution::Distance`, the default): same-track
    below D_ch = 47 Hz; ambiguous from D_ch up to D_ch + 2 bins = 93.9 Hz;
    separate-track from 93.9 Hz. The code has no band of its own: it
    tests whether a peak's interpolated frequency is strictly less than
    D_ch from the track's current frequency (itself an interpolated peak
    frequency). The band is the derived bound on how far those can read
    from the carriers: each interpolated frequency is clamped to ±½ bin
    around its peak bin, and a clean tone's peak bin lies within ½ bin of
    its carrier, so each can be up to one bin (23.4 Hz) off, and carriers
    up to D_ch + 2 bins apart can read closer than D_ch (a bound for one
    step; a track that followed intermediate peaks over several frames
    could go farther, which the code allows and no run has shown). Measured (full
    suite, 3 seeds; `build/suite/full3/results/matched/H-qso-s*.json` and
    `H-qso-drawn-s*.json`, tracks within 25 Hz of either carrier): at
    50 Hz, 4 of 6 grid QSOs stayed one track, the caller's track ending on
    the answering station (its last frequency 49.8–50.0 Hz from the
    caller), and 2 split into two tracks; of the drawn QSOs at 53.9, 57.9
    and 70.2 Hz, the one at 53.9 Hz ended with the caller's track on the
    answering station (that station also had a track born at its own
    carrier; whether both were alive at once is not recorded) and the
    other two stayed apart. Every QSO below 47 Hz (up to 42.8 Hz)
    stayed one track, and every one from 94.6 Hz up split into at least
    two.
  In the full suite's QSOs (grid 0, 10, 25, 50, 100, 200 Hz; drawn offsets
  up to 42.8 Hz, 53.9–70.2 Hz and from 94.6 Hz) the two rules put every
  QSO in the same regime. Each QSO is scored with one label for the QSO
  and with one label per station. The summary marks with † the view that
  does not fit: through the detector, by the QSO's regime on the row's own
  path (labels per station do not fit a row whose QSOs are all
  same-track, labels per QSO one whose QSOs are all separate-track;
  ambiguous rows and rows that mix regimes fit both; a row whose QSOs fall
  in another regime on its path than its tag's word says so, "on this
  path: …"); with oracle channels, by the channel's passband, whose
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
  **Not meaningful (oracle anchor):** with oracle channels there is no
  detector, so the Matched tracker's anchor is the labeled frequency and it
  fine-tunes only within ±12 Hz of it (section 7, "Frequency
  re-centering"). A Matched row (and its paired difference, and its
  per-over row) of an oracle recording is marked "not meaningful (oracle
  anchor)" when some label in it gets more than 12 Hz from its labeled
  frequency: a drift of ḟ Hz/s over the signal's length t_end − t_start,
  |ḟ|·(t_end − t_start) > 12 Hz, or a QSO label whose answering station is
  more than 12 Hz from the caller. A per-station label's channel sits on
  that station, so the per-station view is not marked.
  **S₅₀₀ at a CER threshold:** for a condition with at least three S₅₀₀
  points, CER per point is pooled over its stations; scanning down from the
  highest S₅₀₀, the first point above the threshold and the one above it
  bracket the crossing, interpolated linearly in dB. No crossing is
  reported if the highest point already fails; if none fails, the lowest
  point is reported (an upper bound).
- **Frequency error:** for each matched label, |f_tracked − f_true|, Hz,
  where f_tracked is the frequency of the track's latest decoded text
  (its birth frequency until then) and f_true is the label's carrier; for a
  QSO label, the carrier of the station that sent the last over (that is
  where the latest text came from); for a station label (group H), that
  station's carrier. Signals with drift are left out. Summaries report the
  median per condition.
