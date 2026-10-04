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

The decoder box shows the Envelope decoder (`--decoder envelope`, the
milestone-1 pipeline). The default, the Matched decoder, replaces
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
  not meaningful for the Matched decoder.
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

For the filter-bank prototype (milestone 2b, stage 1), `kz4ap-bench
--record-channels DIR` (with `--oracle`) copies each oracle channel's
stream to a file through `Engine::set_channel_tap`, as the channelizer
delivers it and before the decoder sees it. The tap only observes: the
decoded text is the same with or without it (tested). Without `--oracle`,
`--record-channels` records every channel the detector opens, from its
first block to its track's death, with the detector's frequency for the
track at every block (`ChannelBlock::anchor_hz`, the Matched path's
tracker anchor); the tap still only observes. (`anchor_hz` is the value
the engine already hands the decoder as its anchor before each block: the
labeled frequency in oracle mode; with `--front-end matched` and no
oracle, the detector's current frequency for the track; with the Envelope
decoder and no oracle, which never updates it, the track's birth
frequency. Benchmark tooling only; it changes no signal processing.)

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
its channel, well inside the flat passband. The two decoders treat that
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

### Frequency re-centering (Matched decoder only)

Used only by the Matched decoder (section 8b); the
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
  α = 1 − e^(−1/(τ_f·r)), τ_f = 0.5 s, weighted by p, the Matched decoder's
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
  afterward. The classical decoder (section 8, "Two decoders"), which
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

**Two decoders.** `ClassicalDecoderConfig::front_end` selects how samples
become key-down and key-up. `Envelope` (steps 1–5 below) is the baseline;
`Matched` is the default (owner decision 2026-09-29). `Matched` replaces
steps 1–5 with section 7's re-centering and section 8b's matched filter
and likelihood: the key goes down when the posterior log-odds g exceeds
+1 nat and up when it falls below −1 nat (**heuristic** hysteresis), and
never goes down while a < a_min(K) (section 8b, "Squelch"). Steps 6–10
(glitches, elements, gaps, symbols, speed) are the same in both. In
`Matched` mode the filter follows the speed estimate once its window holds
8 marks (**heuristic**), and every decode result reports the tracker's
frequency estimate. While the filter follows, each mark
may raise the dit estimate by at most ×1.25 (**heuristic**, owner
decision 2026-09-29): it stops a runaway after a sudden speed change, and a
real slowdown takes ln(ratio)/ln 1.25 marks to follow (5 marks from 35 to
12 WPM, derived). The same bound holds for the filter's own dit from its
first follow step after an acquisition or re-acquisition (owner decision
2026-09-29, option 1): the decision is that it grows from the 20 ms
acquisition dit by at most ×1.25 per mark, because the estimate at that
step may rest on up to 7 unbounded marks (in simulation a truncated first
mark gave a 110 ms estimate, the filter jumped from K = 24 to 146 samples
and ran away).
**Once per physical mark (Task 15, milestone 2, part 1).** The bound is
applied in `update_speed()`, at every key-up counted for speed. Until
Task 15 it acted per speed update, against the owner's decision: when the
filter widened at a key-up, the wider boxcar still covered the mark just
ended, the key went down again within 0.3 dit, the dropout merge (step 6)
popped the mark but kept the updated `dit_s_` and `filter_dit_s_`, and
the next key-up applied ×1.25 again (measured, Task 14: 7 times on one
mark, the filter's dit 20 → 95.4 ms within 35 ms); the merge also left
the mark counted twice toward the 8 before the filter follows. Now each
key-up counted for speed saves the state its update starts from (the dit
estimate, the filter's dit and length, the count of marks since the last
re-acquisition, the speed window), and a merge that re-opens that element
restores it, so the re-measured mark gets one update, bounded once. The
widened filter can still re-open the mark it was widened at; each re-open
is merged and undone, and it stops about (T_v,new − T_v,old)/2 after
the first key-up (T_v the filter's duration), at most 0.125·T_v,old =
0.1 of the old dit, since T_v,new ≤ 1.25·T_v,old (derived for a boxcar
and a strong signal: its falling edge passes half amplitude half the
length change later; an upper bound, the ±1 nat hysteresis ends it
slightly earlier), for example 4.8 ms at 25 WPM (T_v,old = 38.7 ms): the
mark is timed through the new filter, up to that much longer, and grows
the estimate at most ×1.25. The Envelope path's merge
is unchanged (it pops the mark from the window and keeps the updated
estimate, as in milestone 1; it has no growth bound).
Measured in section 8b (Task 17; full suite, 3 seeds; Task 15 alone
scored against Task 14's code): the two start-up runaways there went
from Matched CER 0.464 and 0.981 to 0.000 and 0.074 (0.015 with Task 16
added); the speed step from
20 to 35 WPM (group D) went from 0.113 (0.058 to 0.163) to 0.394 (0.222
to 0.499), the text stopping after the step in 5 of 6 signals (not
diagnosed).
**Marks whose start was not observed (Matched; Task 16).** Keying is
possible on a sample where the Matched decoder is ready (its 0.32 s warm-up is
over) and the squelch is open (a ≥ a_min). A key-down counts as observed
only if, since the last sample on which keying was impossible, the
decoder saw a keyable sample with the key up and g < −1 nat (the key-up
threshold): evidence that the carrier was off before the mark began.
Otherwise the carrier may have been up before the key-down, while keying
was impossible or while g sat between −1 and +1 nat, so the mark's
duration may be a fragment's (Task 14: a 15.3 ms fragment of a 195 ms
dah on the smoke recording pinned the dit at 20 ms). Such a mark is
decoded but not counted for speed: it does not enter the speed window or
count toward the 8 marks before the filter follows. Every key-up by the
log-odds sets the evidence, so in steady keying every mark counts; a
dropout merge keeps the merged mark's flag. (Rule derived from what the
decoder can observe; no new parameter: the rule's threshold is the
key-up hysteresis, −1 nat.)
It applies after the warm-up (a channel opening mid-mark); to the first
mark of a channel that opens in noise (after the warm-up, a ≈ 1.6 in
noise alone, below a_min = 3, derived: ŝ² = 2σ²·ln 10 − 2σ² from the
warm-up's percentiles); after a re-acquisition (ŝ restarts at 0, so
a = 0 < a_min); and wherever else the squelch closes and re-opens (the
floor's stuck-low restart of ŝ, ŝ decaying or σ̂ rising at a weak
station). In the last three the squelch re-opens on the sample after the
next mark lifts ŝ past a_min (ŝ is updated after the squelch is
decided), and that mark's keyed start is off in either direction:
early at high S₅₀₀, where the squelch opens on the filter's rising ramp
with â well below the true a, and late at low S₅₀₀ (Task 14: 100.7 ms of
a 144 ms dah at S₅₀₀ 10 dB) (derived from the update). Each such channel
or over loses one mark of speed evidence; the filter follows after 9
physical marks there.
The end of a mark has no such rule: a key-up forced by the squelch
closing (for example the floor's stuck-low restart of ŝ in the middle of
a mark) still counts the mark for speed, with a truncated duration
(derived from the code, not observed; backlog, "A mark whose key-up the
squelch forces").
Measured in section 8b (Task 17): the smoke recording's Matched CER went
from 0.0622 to 0.0436 and its +7617.6 Hz station from 0.234 to 0.043; in
the focused late-opening run no 18.5 WPM opening is worse than Envelope
by 5 edits or more any more (6 and 3 of 80 with Task 14's code, S₅₀₀ 25
and 10 dB). One new loss, found by scoring the full suite (3 seeds) with
Task 15 alone and with Task 16 added: at 12 WPM (dit 100 ms) the first
characters of a station often read as a string of T's (group A, 12 WPM,
first-word CER over S₅₀₀ 6–20 dB 0.591 → 0.985; group D, 10 WPM, CER
0.077 → 0.112). Inferred from step 10, not instrumented: a window of
whole dits alone is longer on average than twice the initial 48 ms dit
whenever the station is slower than 12.5 WPM (dit > 96 ms, derived), so
it is taken as all dahs and the estimate falls to a third of a dit until
the first dah enters the window; why counting the first mark avoided
this was not traced.
The Envelope path is unchanged: its warm-up (one dit at 25 WPM, 48 ms)
sets both levels to the mean envelope, so a mark in progress when it
ends keeps M < 3·S and is not keyed, unless it began late enough that
the mean S = f·A (A the carrier's envelope, f the fraction of the
warm-up the mark filled) is below A/3. Then the squelch opens when the
smoothed envelope reaches 3fA, τ_s·ln((1 − f)/(1 − 3f)) plus about
4 ms of attack after the warm-up ends (τ_s = 0.25 × 48 ms = 12 ms): at
f ≈ 0.28 about 22 ms, so a 48 ms dit is timed at about 19 ms, short
enough to form a dit cluster of its own. So its first timed mark can be
short by up to most of a dit, and a fragment can happen, rarely, in a
window of opening phase a few ms wide (derived, not measured; a stated
limit, not fixed).
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

## 8b. Matched decoder (the default, per station)

Used when the classical decoder's `front_end` setting is `Matched` (the default;
owner decision 2026-09-29) (`ClassicalDecoderConfig::front_end`; section 8, "Two decoders").
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
  a re-acquisition), the filter's dit growing at most ×1.25 per
  physical mark from the 20 ms acquisition dit (owner decision; a merge
  undoes the merged mark's update, section 8, "Two decoders"); K is
  clamped between the acquisition width (β·1.2 s/60 = 16 ms, K = 24) and
  the 5 WPM width (β·1.2 s/5 = 192 ms, K = 288), both computed from
  durations; a dit that is not finite or
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
  25 Hz. The first mark after it, keyed when it lifts ŝ past the squelch
  (early on its ramp at high S₅₀₀, late at low S₅₀₀), is not counted for
  speed (section 8, same paragraph).
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
  A mark in progress when the warm-up ends is decoded but not counted for
  speed (section 8, "Marks whose start was not observed").
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
machine for part of it) and scoring both decoders 5.0 min (nothing
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
**Re-measured after Tasks 15 and 16 (Task 17, 2026-09-30).** The Matched
cells below are from the code after Task 16 (76e2c27), scored on the same
117 recordings (Task 14's, not regenerated); scoring both decoders took
6.0 min on the same machine. The Envelope results were re-scored too and
are identical to Task 14's, score and decoded tracks, in all 126 result
files (the Envelope path is untouched by both fixes). Figures quoted
elsewhere as "before" are Task 14's, from the code of commits bb48b01
and c9a2aa4.

| Condition | Envelope: S₅₀₀ at CER 0.10 / 0.05 (dB) | Matched: S₅₀₀ at CER 0.10 / 0.05 (dB) |
|---|---|---|
| A, 12 WPM | 7.2 (6.8 to 7.4) / 7.7 (7.5 to 7.8) | −0.2 (−0.5 to 0.7) / 1.2 (−0.1 to 18.1) |
| A, 25 WPM | 5.1 (4.8 to 5.2) / 5.6 (5.5 to 5.7) | 1.1 (0.4 to 1.4) / 1.7 (1.4 to 1.8) |
| A, 40 WPM | 6.0 (5.9 to 15.2) / 14.8 (7.0 to 15.7) | 2.9 (2.2 to 3.3) / 3.6 (3.3 to 4.3) |

Task 14 (before Tasks 15 and 16), Matched: 0.2 (−0.5 to 2.2) / 2.3 (−0.0
to 3.4) dB at 12 WPM, 2.7 (−0.0 to 10.4) / 3.4 (1.7 to 11.2) dB at
25 WPM, 3.1 (2.0 to 3.6) / 3.7 (3.3 to 12.4) dB at 40 WPM.

Each row below is one tag of the summary; for group H the tag names the
regime and the answering station's offset. † marks the group-H view that
does not fit (section 11). A row pools every S₅₀₀ point of its condition
(groups B, C and G sweep S₅₀₀), so a row can favor Matched while its
high-S₅₀₀ points favor Envelope ("Where Matched is worse" below).

| Condition | Envelope: CER (interval) / character CER / space error rate / first-word CER | Matched: same | Matched − Envelope, paired (interval) |
|---|---|---|---|
| B fading: VE3NEA mix | 0.927 (0.811 to 1.054) / 0.967 / 0.802 / 1.632 | 0.733 (0.703 to 0.766) / 0.762 / 0.645 / 0.704 | −0.208 (−0.317 to −0.105) |
| B fading: hand 24 wpm fD 0.1 Hz | 0.697 (0.595 to 0.830) / 0.683 / 0.741 / 1.305 | 0.610 (0.573 to 0.652) / 0.586 / 0.682 / 0.548 | −0.090 (−0.214 to −0.003) |
| B fading: hand 24 wpm fD 0.3 Hz | 0.758 (0.657 to 0.884) / 0.770 / 0.718 / 1.866 | 0.654 (0.624 to 0.685) / 0.656 / 0.646 / 0.587 | −0.104 (−0.211 to −0.015) |
| B fading: hand 24 wpm fD 1 Hz | 0.937 (0.796 to 1.093) / 0.948 / 0.903 / 1.788 | 0.737 (0.717 to 0.758) / 0.765 / 0.652 / 0.686 | −0.202 (−0.351 to −0.080) |
| B fading: hand 24 wpm fD 3 Hz | 1.147 (0.974 to 1.354) / 1.136 / 1.183 / 2.468 | 0.788 (0.768 to 0.811) / 0.819 / 0.689 / 0.855 | −0.363 (−0.539 to −0.202) |
| B fading: paddle 12 wpm fD 0.1 Hz | 0.852 (0.558 to 1.220) / 0.881 / 0.759 / 1.694 | 0.513 (0.466 to 0.563) / 0.527 / 0.469 / 1.020 | −0.341 (−0.714 to −0.059) |
| B fading: paddle 24 wpm fD 0.1 Hz | 0.615 (0.478 to 0.765) / 0.650 / 0.507 / 0.948 | 0.508 (0.448 to 0.568) / 0.519 / 0.475 / 0.408 | −0.107 (−0.220 to −0.004) |
| B fading: paddle 24 wpm fD 0.3 Hz | 0.782 (0.647 to 0.940) / 0.832 / 0.629 / 1.538 | 0.594 (0.551 to 0.637) / 0.613 / 0.537 / 0.542 | −0.186 (−0.323 to −0.072) |
| B fading: paddle 24 wpm fD 1 Hz | 1.013 (0.860 to 1.181) / 1.054 / 0.885 / 3.024 | 0.752 (0.728 to 0.780) / 0.783 / 0.655 / 0.837 | −0.263 (−0.437 to −0.118) |
| B fading: paddle 24 wpm fD 3 Hz | 1.126 (0.966 to 1.312) / 1.123 / 1.134 / 2.225 | 0.812 (0.788 to 0.839) / 0.847 / 0.704 / 0.794 | −0.314 (−0.467 to −0.169) |
| B fading: paddle 40 wpm fD 0.1 Hz | 0.766 (0.696 to 0.842) / 0.804 / 0.645 / 0.866 | 0.591 (0.531 to 0.647) / 0.599 / 0.565 / 0.707 | −0.175 (−0.245 to −0.108) |
| C fists: bug imbalance +0.0 | 0.385 (0.286 to 0.513) / 0.402 / 0.328 / 0.174 | 0.622 (0.514 to 0.717) / 0.624 / 0.616 / 0.783 | +0.239 (+0.062 to +0.389) |
| C fists: bug imbalance +0.1 | 0.396 (0.301 to 0.516) / 0.403 / 0.372 / 0.214 | 0.753 (0.664 to 0.835) / 0.760 / 0.729 / 0.914 | +0.349 (+0.192 to +0.467) |
| C fists: bug imbalance -0.1 | 0.372 (0.269 to 0.487) / 0.375 / 0.358 / 0.453 | 0.431 (0.334 to 0.543) / 0.438 / 0.406 / 0.562 | +0.068 (−0.063 to +0.188) |
| C fists: computer imbalance +0.0 | 0.106 (0.023 to 0.222) / 0.100 / 0.124 / 0.044 | 0.002 (0.001 to 0.003) / 0.002 / 0.000 / 0.118 | −0.098 (−0.203 to −0.023) |
| C fists: computer imbalance +0.1 | 0.139 (0.030 to 0.255) / 0.135 / 0.152 / 0.017 | 0.002 (0.000 to 0.003) / 0.002 / 0.001 / 0.119 | −0.134 (−0.270 to −0.029) |
| C fists: computer imbalance -0.1 | 0.288 (0.113 to 0.507) / 0.283 / 0.304 / 0.063 | 0.005 (0.001 to 0.013) / 0.006 / 0.003 / 0.111 | −0.286 (−0.525 to −0.106) |
| C fists: hand imbalance +0.0 | 0.302 (0.261 to 0.351) / 0.184 / 0.729 / 0.250 | 0.300 (0.245 to 0.361) / 0.210 / 0.623 / 0.312 | −0.001 (−0.060 to +0.076) |
| C fists: hand imbalance +0.1 | 0.351 (0.301 to 0.406) / 0.245 / 0.727 / 0.544 | 0.510 (0.420 to 0.611) / 0.459 / 0.689 / 0.691 | +0.157 (+0.070 to +0.242) |
| C fists: hand imbalance -0.1 | 0.324 (0.282 to 0.371) / 0.154 / 0.899 / 0.351 | 0.288 (0.247 to 0.337) / 0.177 / 0.667 / 0.284 | −0.037 (−0.085 to +0.019) |
| C fists: machine imbalance +0.0 | 0.017 (0.008 to 0.028) / 0.018 / 0.017 / 0.033 | 0.001 (0.001 to 0.002) / 0.002 / 0.000 / 0.133 | −0.017 (−0.028 to −0.009) |
| C fists: machine imbalance +0.1 | 0.022 (0.008 to 0.041) / 0.021 / 0.027 / 0.000 | 0.002 (0.000 to 0.003) / 0.002 / 0.001 / 0.190 | −0.022 (−0.042 to −0.008) |
| C fists: machine imbalance -0.1 | 0.046 (0.019 to 0.081) / 0.050 / 0.033 / 0.000 | 0.001 (0.001 to 0.002) / 0.002 / 0.000 / 0.118 | −0.045 (−0.077 to −0.019) |
| C fists: paddle imbalance +0.0 | 0.106 (0.043 to 0.220) / 0.088 / 0.170 / 0.061 | 0.044 (0.039 to 0.050) / 0.036 / 0.074 / 0.106 | −0.061 (−0.163 to +0.001) |
| C fists: paddle imbalance +0.1 | 0.106 (0.048 to 0.214) / 0.090 / 0.162 / 0.034 | 0.116 (0.077 to 0.191) / 0.112 / 0.131 / 0.207 | +0.014 (−0.117 to +0.127) |
| C fists: paddle imbalance -0.1 | 0.089 (0.056 to 0.128) / 0.060 / 0.186 / 0.062 | 0.032 (0.026 to 0.038) / 0.021 / 0.068 / 0.234 | −0.060 (−0.100 to −0.025) |
| D speed: 10 wpm | 0.052 (0.045 to 0.063) / 0.061 / 0.019 / 1.000 | 0.112 (0.074 to 0.177) / 0.110 / 0.115 / 2.083 | +0.061 (+0.027 to +0.121) |
| D speed: 60 wpm | 0.133 (0.005 to 0.367) / 0.130 / 0.142 / 0.833 | 0.005 (0.003 to 0.007) / 0.006 / 0.002 / 0.667 | −0.138 (−0.379 to +0.001) |
| D speed: ramp 15->30 | 0.202 (0.057 to 0.433) / 0.172 / 0.314 / 0.000 | 0.000 (0.000 to 0.000) / 0.000 / 0.000 / 0.000 | −0.192 (−0.411 to −0.059) |
| D speed: ramp 30->15 | 0.000 (0.000 to 0.000) / 0.000 / 0.000 / 0.000 | 0.000 (0.000 to 0.000) / 0.000 / 0.000 / 0.000 | +0.000 (+0.000 to +0.000) |
| D speed: step 20->35 | 0.525 (0.257 to 0.845) / 0.541 / 0.474 / 0.000 | 0.394 (0.222 to 0.499) / 0.409 / 0.342 / 0.917 | −0.123 (−0.434 to +0.165) |
| D speed: step 35->20 | 0.114 (0.061 to 0.160) / 0.096 / 0.179 / 0.250 | 0.129 (0.097 to 0.162) / 0.125 / 0.143 / 0.417 | +0.016 (−0.049 to +0.066) |
| E interference: df 100 Hz, +0 dB re wanted key-down power | 0.820 (0.808 to 0.837) / 0.901 / 0.547 / 1.000 | 0.006 (0.000 to 0.019) / 0.004 / 0.013 / 0.333 | −0.814 (−0.817 to −0.808) |
| E interference: df 100 Hz, +10 dB re wanted key-down power | 0.943 (0.775 to 1.113) / 1.062 / 0.566 / 0.818 | 0.388 (0.031 to 0.972) / 0.398 / 0.355 / 0.727 | −0.574 (−1.082 to +0.009) |
| E interference: df 100 Hz, +20 dB re wanted key-down power | 1.041 (0.940 to 1.155) / 1.171 / 0.559 / 1.900 | 0.863 (0.763 to 0.974) / 0.929 / 0.618 / 1.000 | −0.191 (−0.392 to +0.034) |
| E interference: df 100 Hz, -10 dB re wanted key-down power | 0.036 (0.000 to 0.056) / 0.023 / 0.090 / 0.000 | 0.000 (0.000 to 0.000) / 0.000 / 0.000 / 0.000 | −0.037 (−0.056 to +0.000) |
| E interference: df 150 Hz, +0 dB re wanted key-down power | 0.145 (0.027 to 0.331) / 0.152 / 0.120 / 0.000 | 0.000 (0.000 to 0.000) / 0.000 / 0.000 / 0.000 | −0.137 (−0.331 to −0.027) |
| E interference: df 150 Hz, +10 dB re wanted key-down power | 0.824 (0.818 to 0.829) / 0.921 / 0.521 / 1.000 | 0.000 (0.000 to 0.000) / 0.000 / 0.000 / 0.000 | −0.824 (−0.829 to −0.818) |
| E interference: df 150 Hz, +20 dB re wanted key-down power | 0.779 (0.491 to 1.008) / 0.860 / 0.521 / 2.333 | 0.076 (0.040 to 0.129) / 0.080 / 0.064 / 1.111 | −0.689 (−0.937 to −0.362) |
| E interference: df 150 Hz, -10 dB re wanted key-down power | 0.011 (0.000 to 0.029) / 0.007 / 0.023 / 0.000 | 0.000 (0.000 to 0.000) / 0.000 / 0.000 / 0.000 | −0.010 (−0.029 to +0.000) |
| E interference: df 20 Hz, +0 dB re wanted key-down power | 0.793 (0.654 to 1.101) / 0.846 / 0.600 / 0.667 | 0.786 (0.455 to 0.978) / 0.779 / 0.812 / 1.500 | −0.077 (−0.646 to +0.245) |
| E interference: df 20 Hz, +10 dB re wanted key-down power | 0.544 (0.480 to 0.640) / 0.656 / 0.123 / 6.333 | 0.765 (0.727 to 0.803) / 0.851 / 0.438 / 1.000 | +0.221 (+0.088 to +0.352) |
| E interference: df 20 Hz, +20 dB re wanted key-down power | 0.922 (0.822 to 1.131) / 0.979 / 0.707 / 2.667 | 0.813 (0.752 to 0.899) / 0.877 / 0.573 / 0.667 | −0.128 (−0.232 to +0.013) |
| E interference: df 20 Hz, -10 dB re wanted key-down power | 0.044 (0.000 to 0.081) / 0.033 / 0.080 / 0.000 | 0.003 (0.000 to 0.009) / 0.004 / 0.000 / 0.167 | −0.043 (−0.081 to +0.009) |
| E interference: df 50 Hz, +0 dB re wanted key-down power | 0.857 (0.807 to 0.930) / 0.912 / 0.651 / 1.833 | 0.090 (0.023 to 0.235) / 0.088 / 0.096 / 1.000 | −0.757 (−0.902 to −0.571) |
| E interference: df 50 Hz, +10 dB re wanted key-down power | 0.787 (0.500 to 1.009) / 0.878 / 0.467 / 1.000 | 0.817 (0.728 to 0.872) / 0.833 / 0.760 / 1.000 | +0.041 (−0.138 to +0.365) |
| E interference: df 50 Hz, +20 dB re wanted key-down power | 0.855 (0.640 to 1.040) / 0.867 / 0.813 / 1.000 | 1.031 (0.811 to 1.404) / 1.084 / 0.853 / 0.667 | +0.160 (−0.192 to +0.500) |
| E interference: df 50 Hz, -10 dB re wanted key-down power | 0.005 (0.000 to 0.014) / 0.003 / 0.011 / 0.000 | 0.000 (0.000 to 0.000) / 0.000 / 0.000 / 0.000 | −0.005 (−0.014 to +0.000) |
| F tuning: drift 0.2 Hz/s | 0.105 (0.038 to 0.168) / 0.106 / 0.100 / 0.000 | 0.065 (0.006 to 0.166) / 0.071 / 0.043 / 0.500 | −0.041 (−0.111 to +0.021) |
| F tuning: drift 0.5 Hz/s (not meaningful (oracle anchor) for Matched) | 0.073 (0.006 to 0.173) / 0.056 / 0.130 / 0.133 | 0.022 (0.009 to 0.036) / 0.028 / 0.000 / 0.400 | −0.069 (−0.170 to +0.021) |
| F tuning: drift 1 Hz/s (not meaningful (oracle anchor) for Matched) | 0.080 (0.014 to 0.171) / 0.078 / 0.086 / 0.267 | 0.232 (0.190 to 0.276) / 0.239 / 0.207 / 0.400 | +0.138 (+0.022 to +0.241) |
| F tuning: drift 2 Hz/s (not meaningful (oracle anchor) for Matched) | 0.113 (0.046 to 0.186) / 0.086 / 0.210 / 0.000 | 0.532 (0.439 to 0.618) / 0.555 / 0.452 / 0.417 | +0.410 (+0.268 to +0.515) |
| F tuning: offset 0 Hz 20 wpm | 0.416 (0.136 to 0.716) / 0.471 / 0.213 / 0.625 | 0.049 (0.002 to 0.142) / 0.049 / 0.049 / 0.250 | −0.376 (−0.680 to −0.097) |
| F tuning: offset 0 Hz 25 wpm | 0.412 (0.189 to 0.623) / 0.475 / 0.218 / 0.500 | 0.194 (0.012 to 0.425) / 0.211 / 0.141 / 0.583 | −0.228 (−0.466 to −0.026) |
| F tuning: offset 11.7 Hz 20 wpm | 0.404 (0.141 to 0.647) / 0.479 / 0.113 / 0.333 | 0.043 (0.012 to 0.079) / 0.042 / 0.047 / 0.933 | −0.338 (−0.600 to −0.086) |
| F tuning: offset 11.7 Hz 25 wpm | 0.420 (0.161 to 0.670) / 0.469 / 0.219 / 0.300 | 0.113 (0.023 to 0.233) / 0.114 / 0.110 / 0.450 | −0.299 (−0.519 to −0.088) |
| F tuning: offset 2.9 Hz 20 wpm | 0.486 (0.143 to 0.929) / 0.488 / 0.475 / 0.579 | 0.047 (0.003 to 0.129) / 0.048 / 0.041 / 0.789 | −0.472 (−0.909 to −0.135) |
| F tuning: offset 2.9 Hz 25 wpm | 0.394 (0.131 to 0.692) / 0.429 / 0.276 / 0.533 | 0.208 (0.039 to 0.451) / 0.218 / 0.171 / 0.467 | −0.187 (−0.358 to −0.030) |
| F tuning: offset 5.9 Hz 20 wpm | 0.393 (0.128 to 0.639) / 0.433 / 0.252 / 0.476 | 0.028 (0.011 to 0.045) / 0.036 / 0.000 / 0.333 | −0.354 (−0.599 to −0.117) |
| F tuning: offset 5.9 Hz 25 wpm | 0.386 (0.148 to 0.598) / 0.439 / 0.206 / 0.312 | 0.091 (0.017 to 0.169) / 0.098 / 0.065 / 1.188 | −0.276 (−0.472 to −0.093) |
| F tuning: offset 8.8 Hz 20 wpm | 0.492 (0.204 to 0.789) / 0.515 / 0.407 / 0.647 | 0.010 (0.004 to 0.016) / 0.010 / 0.009 / 0.176 | −0.487 (−0.757 to −0.186) |
| F tuning: offset 8.8 Hz 25 wpm | 0.405 (0.148 to 0.631) / 0.471 / 0.138 / 0.400 | 0.178 (0.016 to 0.347) / 0.190 / 0.131 / 0.867 | −0.221 (−0.434 to −0.054) |
| G ragchew: ragchew 25 wpm | 0.196 (0.111 to 0.284) / 0.203 / 0.174 / 0.240 | 0.081 (0.051 to 0.116) / 0.076 / 0.096 / 0.169 | −0.117 (−0.183 to −0.056) |
| H two-station QSO: ambiguous, drawn offset | 0.130 (0.087 to 0.164) / 0.092 / 0.237 / 0.486 | 0.573 (0.456 to 0.643) / 0.607 / 0.477 / 0.820 | +0.442 (+0.368 to +0.507) |
| H two-station QSO: ambiguous, offset 50 Hz | 0.062 (0.026 to 0.101) / 0.038 / 0.128 / 0.106 | 0.215 (0.070 to 0.386) / 0.216 / 0.212 / 0.387 | +0.151 (−0.008 to +0.341) |
| H two-station QSO: same-track, drawn offset | 0.116 (0.087 to 0.146) / 0.075 / 0.231 / 0.247 | 0.220 (0.150 to 0.296) / 0.194 / 0.291 / 0.283 | +0.104 (+0.053 to +0.172) |
| H two-station QSO: same-track, offset 0 Hz | 0.150 (0.074 to 0.216) / 0.099 / 0.298 / 0.261 | 0.271 (0.092 to 0.469) / 0.251 / 0.329 / 0.332 | +0.124 (−0.027 to +0.273) |
| H two-station QSO: same-track, offset 10 Hz | 0.103 (0.036 to 0.176) / 0.067 / 0.208 / 0.246 | 0.146 (0.045 to 0.253) / 0.125 / 0.208 / 0.217 | +0.043 (+0.006 to +0.081) |
| H two-station QSO: same-track, offset 25 Hz | 0.107 (0.035 to 0.216) / 0.067 / 0.221 / 0.377 | 0.183 (0.043 to 0.363) / 0.168 / 0.225 / 0.274 | +0.076 (−0.070 to +0.255) |
| H two-station QSO: separate-track, drawn offset † | 0.723 (0.702 to 0.746) / 0.722 / 0.723 / 0.738 | 0.790 (0.782 to 0.797) / 0.790 / 0.789 / 0.814 | +0.067 (+0.052 to +0.082) |
| H two-station QSO: separate-track, offset 100 Hz † | 0.428 (0.169 to 0.679) / 0.415 / 0.467 / 0.542 | 0.718 (0.548 to 0.848) / 0.726 / 0.697 / 0.778 | +0.294 (+0.089 to +0.555) |
| H two-station QSO: separate-track, offset 200 Hz † | 0.786 (0.770 to 0.799) / 0.789 / 0.778 / 0.886 | 0.806 (0.794 to 0.816) / 0.806 / 0.808 / 0.905 | +0.021 (+0.010 to +0.033) |
| H two-station QSO (per station): ambiguous, drawn offset | 0.992 (0.900 to 1.089) / 0.953 / 1.103 / 1.009 | 1.073 (0.753 to 1.395) / 1.052 / 1.131 / 2.802 | +0.087 (−0.195 to +0.400) |
| H two-station QSO (per station): ambiguous, offset 50 Hz | 0.947 (0.878 to 1.007) / 0.931 / 0.994 / 1.576 | 0.977 (0.847 to 1.115) / 0.975 / 0.983 / 1.871 | +0.028 (−0.109 to +0.183) |
| H two-station QSO (per station): same-track, drawn offset † | 0.973 (0.951 to 0.995) / 0.946 / 1.049 / 1.089 | 0.903 (0.869 to 0.936) / 0.909 / 0.889 / 1.061 | −0.065 (−0.101 to −0.035) |
| H two-station QSO (per station): same-track, offset 0 Hz † | 0.971 (0.902 to 1.036) / 0.940 / 1.062 / 0.765 | 0.852 (0.758 to 0.938) / 0.859 / 0.830 / 0.769 | −0.113 (−0.214 to −0.033) |
| H two-station QSO (per station): same-track, offset 10 Hz † | 0.960 (0.927 to 0.993) / 0.934 / 1.038 / 1.725 | 0.907 (0.828 to 0.973) / 0.903 / 0.919 / 1.639 | −0.050 (−0.109 to −0.003) |
| H two-station QSO (per station): same-track, offset 25 Hz † | 0.979 (0.912 to 1.071) / 0.949 / 1.063 / 1.400 | 0.878 (0.780 to 0.961) / 0.885 / 0.857 / 1.428 | −0.095 (−0.191 to −0.001) |
| H two-station QSO (per station): separate-track, drawn offset | 0.564 (0.548 to 0.582) / 0.575 / 0.533 / 0.715 | 0.618 (0.606 to 0.629) / 0.618 / 0.618 / 0.805 | +0.055 (+0.038 to +0.074) |
| H two-station QSO (per station): separate-track, offset 100 Hz | 0.673 (0.593 to 0.778) / 0.679 / 0.653 / 1.368 | 0.635 (0.581 to 0.693) / 0.643 / 0.610 / 0.755 | −0.033 (−0.165 to +0.061) |
| H two-station QSO (per station): separate-track, offset 200 Hz | 0.613 (0.603 to 0.623) / 0.619 / 0.597 / 0.725 | 0.646 (0.627 to 0.663) / 0.646 / 0.644 / 0.754 | +0.034 (+0.015 to +0.051) |
| H two-station QSO, oracle: ambiguous, offset 50 Hz (not meaningful (oracle anchor) for Matched) | 0.357 (0.029 to 0.973) / 0.351 / 0.373 / 0.078 | 0.479 (0.413 to 0.562) / 0.502 / 0.414 / 0.668 | +0.120 (−0.483 to +0.460) |
| H two-station QSO, oracle: same-track, offset 0 Hz | 0.322 (0.087 to 0.668) / 0.276 / 0.455 / 0.223 | 0.262 (0.088 to 0.459) / 0.239 / 0.328 / 0.282 | −0.056 (−0.521 to +0.256) |
| H two-station QSO, oracle: same-track, offset 10 Hz | 0.101 (0.036 to 0.176) / 0.066 / 0.205 / 0.217 | 0.142 (0.046 to 0.238) / 0.121 / 0.205 / 0.172 | +0.040 (+0.007 to +0.078) |
| H two-station QSO, oracle: same-track, offset 25 Hz (not meaningful (oracle anchor) for Matched) | 0.106 (0.033 to 0.211) / 0.064 / 0.222 / 0.335 | 0.490 (0.418 to 0.557) / 0.513 / 0.426 / 0.507 | +0.384 (+0.280 to +0.481) |
| H two-station QSO, oracle: separate-track, offset 100 Hz (not meaningful (oracle anchor) for Matched) | 0.336 (0.072 to 0.712) / 0.314 / 0.397 / 0.311 | 0.657 (0.539 to 0.799) / 0.662 / 0.641 / 0.698 | +0.321 (+0.033 to +0.506) |
| H two-station QSO, oracle: separate-track, offset 200 Hz † (not meaningful (oracle anchor) for Matched) | 0.468 (0.441 to 0.491) / 0.485 / 0.420 / 0.716 | 0.503 (0.479 to 0.532) / 0.497 / 0.518 / 0.588 | +0.035 (−0.006 to +0.067) |
| H two-station QSO, oracle (per station): ambiguous, offset 50 Hz † | 1.490 (1.011 to 2.189) / 1.472 / 1.541 / 1.894 | 1.001 (0.746 to 1.219) / 0.902 / 1.280 / 2.797 | −0.481 (−1.132 to +0.029) |
| H two-station QSO, oracle (per station): same-track, offset 0 Hz † | 1.461 (1.028 to 1.996) / 1.403 / 1.628 / 1.462 | 0.860 (0.747 to 0.981) / 0.885 / 0.788 / 1.374 | −0.608 (−1.132 to −0.220) |
| H two-station QSO, oracle (per station): same-track, offset 10 Hz † | 1.055 (0.971 to 1.151) / 0.999 / 1.221 / 2.561 | 0.965 (0.851 to 1.063) / 0.964 / 0.966 / 1.730 | −0.090 (−0.150 to −0.038) |
| H two-station QSO, oracle (per station): same-track, offset 25 Hz † | 1.115 (0.985 to 1.258) / 1.047 / 1.309 / 2.293 | 1.029 (0.839 to 1.206) / 1.039 / 1.001 / 2.233 | −0.084 (−0.266 to +0.068) |
| H two-station QSO, oracle (per station): separate-track, offset 100 Hz † | 1.449 (1.023 to 2.046) / 1.418 / 1.534 / 2.057 | 0.634 (0.455 to 0.822) / 0.563 / 0.833 / 1.387 | −0.819 (−1.407 to −0.366) |
| H two-station QSO, oracle (per station): separate-track, offset 200 Hz | 0.277 (0.178 to 0.384) / 0.232 / 0.402 / 1.223 | 0.081 (0.042 to 0.125) / 0.065 / 0.125 / 0.152 | −0.197 (−0.312 to −0.092) |
| strong: S500 30 dB | 0.034 (0.023 to 0.048) / 0.037 / 0.019 / 0.571 | 0.053 (0.045 to 0.060) / 0.056 / 0.038 / 0.857 | +0.019 (+0.011 to +0.024) |
| strong: S500 40 dB | 0.052 (0.047 to 0.056) / 0.050 / 0.056 / 0.867 | 0.055 (0.051 to 0.059) / 0.050 / 0.069 / 0.867 | +0.004 (+0.000 to +0.011) |
| strong: S500 50 dB | 0.028 (0.020 to 0.038) / 0.032 / 0.015 / 0.583 | 0.045 (0.041 to 0.049) / 0.054 / 0.015 / 1.000 | +0.018 (+0.011 to +0.023) |
| strong: S500 60 dB | 0.028 (0.023 to 0.037) / 0.036 / 0.000 / 0.583 | 0.048 (0.045 to 0.050) / 0.062 / 0.000 / 1.000 | +0.020 (+0.012 to +0.025) |
| pauses: pause 10 s | 0.144 (0.096 to 0.211) / 0.115 / 0.273 / 0.222 | 0.034 (0.032 to 0.036) / 0.042 / 0.000 / 0.333 | −0.114 (−0.179 to −0.062) |
| pauses: pause 2 s | 0.050 (0.023 to 0.092) / 0.048 / 0.061 / 0.167 | 0.038 (0.035 to 0.041) / 0.048 / 0.000 / 0.333 | −0.015 (−0.056 to +0.013) |
| pauses: pause 20 s | 0.644 (0.632 to 0.655) / 0.656 / 0.591 / 0.833 | 0.707 (0.699 to 0.714) / 0.702 / 0.727 / 1.000 | +0.064 (+0.046 to +0.080) |
| pauses: pause 5 s | 0.069 (0.035 to 0.112) / 0.067 / 0.076 / 0.222 | 0.039 (0.036 to 0.042) / 0.050 / 0.000 / 0.333 | −0.032 (−0.075 to +0.003) |
| tune-up: tune-up 0.3 s | 0.007 (0.000 to 0.014) / 0.009 / 0.000 / 0.125 | 0.010 (0.000 to 0.023) / 0.013 / 0.000 / 0.188 | +0.003 (+0.000 to +0.009) |
| tune-up: tune-up 0.6 s | 0.000 (0.000 to 0.000) / 0.000 / 0.000 / 0.000 | 0.003 (0.000 to 0.009) / 0.004 / 0.000 / 0.062 | +0.003 (+0.000 to +0.008) |
| tune-up: tune-up 1 s | 0.000 (0.000 to 0.000) / 0.000 / 0.000 / 0.000 | 0.542 (0.258 to 0.835) / 0.536 / 0.559 / 0.833 | +0.562 (+0.280 to +0.839) |
| tune-up: tune-up 2 s | 0.029 (0.006 to 0.067) / 0.018 / 0.068 / 0.000 | 0.909 (0.758 to 1.000) / 0.912 / 0.898 / 1.000 | +0.899 (+0.775 to +0.981) |
| first sample: from the first sample | 0.119 (0.097 to 0.147) / 0.111 / 0.145 / 0.833 | 0.138 (0.122 to 0.157) / 0.143 / 0.120 / 0.900 | +0.018 (−0.001 to +0.033) |
| crowded: spacing 0 Hz | 0.518 (0.409 to 0.620) / 0.531 / 0.469 / 1.370 | 0.253 (0.167 to 0.347) / 0.259 / 0.230 / 1.028 | −0.247 (−0.335 to −0.152) |
| crowded: spacing 100 Hz | 0.326 (0.223 to 0.430) / 0.342 / 0.271 / 0.876 | 0.048 (0.037 to 0.068) / 0.051 / 0.038 / 0.861 | −0.251 (−0.341 to −0.167) |
| crowded: spacing 200 Hz | 0.037 (0.030 to 0.047) / 0.039 / 0.030 / 0.749 | 0.041 (0.037 to 0.046) / 0.043 / 0.035 / 0.806 | +0.007 (−0.005 to +0.020) |
| crowded: spacing 50 Hz | 0.394 (0.286 to 0.503) / 0.414 / 0.327 / 1.141 | 0.126 (0.065 to 0.201) / 0.128 / 0.118 / 0.895 | −0.280 (−0.393 to −0.175) |
| band: band | 0.102 (0.041 to 0.177) / 0.107 / 0.084 / 0.808 | 0.051 (0.045 to 0.058) / 0.053 / 0.042 / 0.842 | −0.030 (−0.082 to +0.010) |

| Per over (groups G and H, from the "Per over" table) | Envelope: CER | Matched: CER |
|---|---|---|
| G ragchew, paddle (288 overs) | 0.197 | 0.082 |
| H two-station QSO, computer (120 overs) | 0.246 | 0.303 |
| H two-station QSO, hand (144 overs) | 0.306 | 0.534 |
| H two-station QSO, paddle (312 overs) | 0.218 | 0.316 |
| H two-station QSO, oracle, computer (68 overs) (not meaningful (oracle anchor) for Matched) | 0.305 | 0.370 |
| H two-station QSO, oracle, hand (68 overs) (not meaningful (oracle anchor) for Matched) | 0.296 | 0.648 |
| H two-station QSO, oracle, paddle (152 overs) (not meaningful (oracle anchor) for Matched) | 0.266 | 0.346 |

**Group H by regime** (detector run, grid and drawn offsets pooled by
`qso_regime`; the oracle copy in the second block; QSO-label CER scores
one label per QSO, station-label CER one label per station; the view that
does not fit the regime is marked †, as in section 11):

| Group H regime (tags) | Tracks per QSO, Envelope / Matched | QSO-label CER, Envelope / Matched | Station-label CER, Envelope / Matched |
|---|---|---|---|
| same-track (0, 10, 25 Hz; drawn), 45 QSOs | 1.00 / 1.00 | 0.118 (0.091 to 0.145) / 0.212 (0.156 to 0.271) | † 0.972 (0.951 to 0.992) / 0.893 (0.863 to 0.919) |
| ambiguous (50 Hz; drawn), 9 QSOs | 1.56 / 1.67 | 0.085 (0.051 to 0.119) / 0.334 (0.189 to 0.491) | 0.962 (0.908 to 1.015) / 1.009 (0.877 to 1.157) |
| separate-track (100, 200 Hz; drawn), 18 QSOs | 6.78 / 6.78 | † 0.646 (0.531 to 0.747) / 0.772 (0.710 to 0.815) | 0.616 (0.585 to 0.656) / 0.633 (0.613 to 0.655) |
| oracle copy, same-track (0, 10, 25 Hz), 18 QSOs | — | 0.176 (0.080 to 0.316) / 0.297 (0.194 to 0.392); Matched not meaningful at 25 Hz (oracle anchor) | † 1.210 (1.057 to 1.433) / 0.951 (0.869 to 1.035) |
| oracle copy, ambiguous (50 Hz), 6 QSOs | — | 0.357 (0.028 to 0.973) / 0.479 (0.412 to 0.562), Matched not meaningful (oracle anchor) | † 1.490 (0.991 to 2.182) / 1.001 (0.776 to 1.224) |
| oracle copy, separate-track (100, 200 Hz), 12 QSOs | — | 0.402 (0.242 to 0.583) / 0.579 (0.508 to 0.669), Matched not meaningful (oracle anchor); † at 200 Hz | 0.861 (0.561 to 1.226) / 0.357 (0.220 to 0.494); † at 100 Hz |

Task 14's Matched cells, in the same order: QSO label 0.197 (0.149 to
0.251), 0.297 (0.162 to 0.439), 0.745 (0.683 to 0.799), 0.283, 0.597,
0.505; station label 0.904 (0.875 to 0.928), 1.010 (0.872 to 1.180),
0.620 (0.597 to 0.636), 0.960, 1.073, 0.338; tracks per QSO unchanged.
Every Matched QSO-label cell through the detector rose (0.015–0.037) and
every interval overlaps its Task 14 interval: no change beyond the
intervals.

A separate-track QSO shows 6.78 tracks on both paths because each
station's track dies during the other's over and is re-born (section 11,
"Tracks per QSO"; backlog, "Tracks outlive their stations").

**First word of each over** (group H through the detector, grid offsets;
first-word CER is an upper bound, section 11; the answering station's
level is drawn from −6 to +6 dB re the caller's key-down power):

| Answering station's offset | Envelope: answer / caller first-word CER | Matched: answer / caller first-word CER | Matched: answer / caller over CER |
|---|---|---|---|
| 0 Hz (24 overs each) | 0.199 / 0.343 | 0.287 / 0.392 | 0.306 / 0.243 |
| 10 Hz | 0.213 / 0.287 | 0.110 / 0.352 | 0.207 / 0.094 |
| 25 Hz | 0.422 / 0.323 | 0.190 / 0.374 | 0.216 / 0.156 |
| 50 Hz | 0.032 / 0.204 | 0.379 / 0.398 | 0.143 / 0.279 |

Envelope's over CER for the same rows: answer 0.208, 0.124, 0.140, 0.044;
caller 0.104, 0.085, 0.076, 0.079. Task 14's Matched cells (answer /
caller first word; answer / caller over): 0 Hz 0.235 / 0.353, 0.279 /
0.231; 10 Hz 0.147 / 0.287, 0.172 / 0.099; 25 Hz 0.181 / 0.444, 0.245 /
0.196; 50 Hz 0.444 / 0.323, 0.345 / 0.086. These are ratios of pooled
counts over 24 overs from 6 QSOs, with no interval, so changes of this
size (−0.07 to +0.07 in first-word CER) are not shown to be beyond
chance. At 50 Hz the over CER moved from the answering station (0.345 →
0.143) to the caller (0.086 → 0.279), mostly in one split QSO
(`H-qso-s1`, QSO 6: answering station's overs 1.095 → 0.021, caller's
0.048 → 1.030 on the QSO label); not diagnosed.

| Group B against VE3NEA (no-space CER, his metric) | VE3NEA DeepCW | CW Skimmer (his measurement) | Envelope | Matched |
|---|---|---|---|---|
| Paddle, 24 WPM, f_D 0.1 Hz, S₅₀₀ 1.78 / 7.78 / 17.78 / 57.78 dB | 0.373 / 0.137 / 0.025 / 0.005 | 0.364 / 0.101 / 0.022 / 0.011 | 0.730 / 0.677 / 0.292 / 0.179 | 0.696 / 0.577 / 0.276 / 0.260 |
| HandKey, 24 WPM, f_D 0.1 Hz, S₅₀₀ 1.78 / 7.78 / 17.78 / 57.78 dB | 0.412 / 0.186 / 0.082 / 0.063 | 0.429 / 0.188 / 0.091 / 0.083 | 0.805 / 0.541 / 0.369 / 0.295 | 0.663 / 0.460 / 0.437 / 0.504 |

Pooled over 3 seeds, 9 stations per point: 1951, 1960, 1954 and 1932
reference characters per point (paddle) and 1927, 1964, 1952 and 1922
(hand key), the same for both decoders; VE3NEA's points hold 30 000.
Task 14's Matched cells: paddle 0.741 / 0.604 / 0.558 / 0.348, hand key
0.658 / 0.530 / 0.485 / 0.519 (no interval; pooled ratios).
Remaining differences from his benchmark: bin-centered oracle channels
instead of his ±30 Hz pitch error, complex I/Q noise instead of real
audio, and one draw per character and word space (he sums several); the
keying edges match his (2 ms, centered). Both of our decoders are far
from his at every point, and the gap does not close at high S₅₀₀: at
57.78 dB, where a fade to 30 dB below the mean power (probability 10⁻³
for Rayleigh fading, derived) still leaves S₅₀₀ = 28 dB, Envelope reads
0.179 and 0.295 and Matched 0.260 and 0.504. So the residual is not
noise; that it is the hard-decision timing (speed estimate and fixed
gap thresholds, section 8) under changing level is an inference, not
measured.

| | Envelope | Matched |
|---|---|---|
| CPU per channel-second (process, over the bench's timed window; section 11), ms/s | 0.202 (Task 14) | 0.381 (Task 14) |
| Decoders per channel-second, ms/s | 0.014 (Task 14) | 0.177 (Task 14) |
| Median frequency error, group F offsets, Hz | 5.9 (bin rounding; no re-centering) | 0.11 at S₅₀₀ = 5 dB, 0.13 at 0 dB (30 signals each) |

CPU is over 384 495 channel-seconds (Envelope) and 384 661 (Matched), on
the machine above; the Matched decoder adds 0.163 ms of decoder time per
channel-second, 11.6 times the Envelope decoder's time (the Matched decoders'
total, 0.177 ms, is 12.6 times it), and nearly doubles the
whole process: outside the decoders (WAV reading, the shared FFTs, the
detector in the detector runs only, the bench's event subscriber;
section 11, "CPU time per channel-second") Envelope's process spends
0.202 − 0.014 = 0.188 ms/s. Most channel-seconds (340 465) come from
oracle runs, which skip the detector (section 11 splits the figures).
**Task 17 (after Tasks 15 and 16), two runs on the same machine, same
session:** process 0.235 and 0.244 ms/s (Envelope), 0.444 and 0.472
ms/s (Matched); decoders 0.016 and 0.017 ms/s (Envelope), 0.201 and
0.218 ms/s (Matched); `run` took 6.0 and 6.2 min (the second overlapped
a few seconds of short diagnostic runs). Envelope's figures are 16–21%
above Task 14's although its code path and its results are
identical, so the machine's state differed between the sessions
(inferred; not controlled), a spread larger than any effect of the
fixes. Within each session the ratios hold: Matched's process 1.89,
1.89 and 1.93 times Envelope's, its decoders 12.6, 12.4 and 13.0 times
(Task 14, Task 17 runs 1 and 2), so the fixes add no decoder cost this
measurement can resolve.

**Reading the results.**

- **Group A (sensitivity, oracle).** Matched crosses CER 0.10 at S₅₀₀ =
  −0.2, 1.1 and 2.9 dB (12, 25, 40 WPM) against Envelope's 7.2, 5.1 and
  6.0 dB: 3.1–7.4 dB better by the point estimates, with the two
  intervals disjoint at every speed, and the paired differences favor Matched
  at all three speeds (−0.575, −0.238, −0.087, intervals excluding 0).
  (Task 14: 0.2, 2.7 and 3.1 dB; at 25 WPM the Matched interval was
  −0.0 to 10.4 dB and overlapped Envelope's.) Envelope's crossings sit
  near its squelch's estimated +6 dB (section 8, step 5), as expected.
  Matched's are **above** the design's expectation of about −2.6 to 0 dB
  (acquisition floor −2.5 dB derived; "Squelch" above) at 25 and 40 WPM
  (1.1 dB, 0.4 to 1.4 dB; 2.9 dB, 2.2 to 3.3 dB); at 12 WPM (−0.2 dB,
  −0.5 to 0.7 dB) the interval overlaps that range. Per point, the
  Matched CER at 25 WPM is 0.670 at −2 dB, 0.185 at 0 dB, 0.024 at 2 dB
  and 0.003 at 4 dB (Task 14: 0.711, 0.122, 0.151, 0.006), so the
  crossing now lies between 0 and 2 dB; 8 of 12 stations exceed CER 0.10
  at 0 dB and none at 2 dB (Task 14: 6 and 2; 40 WPM: 12 and 7 of 12,
  unchanged). The single stations that failed at high S₅₀₀ in Task 14
  (25 WPM, 10 dB, CER 0.464; 40 WPM, 6, 10 and 12 dB, 0.155, 0.265,
  0.236), the start-up runaways of "Limits measured" below, now read
  0.000–0.020. The wide CER-0.05 interval at 12 WPM (−0.1 to 18.1 dB)
  comes from the new 12 WPM start-up loss (section 8, "Marks whose start was
  not observed"): the first characters read as T's, CER 0.021–0.039 per
  point at 6–20 dB against 0.011–0.021 in Task 14, and 1 of 12 stations
  above CER 0.10 at 4 and at 12 dB. No run or scoring fault was found:
  every result file names the decoder that made it, and the rebuilt
  bench reproduces the stored results exactly (Task 14's Matched results
  re-scored at 74182aa, whose engine differs from c9a2aa4 only in
  comments, are identical in all 126 files; Task 17).
- **Group F (tuning, oracle).** Matched's CER does not depend on the
  offset from the bin center beyond the intervals (20 WPM: 0.049, 0.047,
  0.028, 0.010, 0.043 at 0, 2.9, 5.9, 8.8, 11.7 Hz, every interval
  overlapping; 25 WPM: 0.194, 0.208, 0.091, 0.178, 0.113, likewise; Task
  14: 0.124, 0.041, 0.030, 0.184, 0.049 and 0.145, 0.089, 0.203, 0.167,
  0.309);
  Envelope's is 0.39–0.49 at every offset. The frequency error is within
  the ±2 Hz target for every signal (section 7). The drift rows show the
  **oracle-mode limit of option 1**: with no detector the anchor stays on
  the labeled starting frequency and the tracker covers only ±12 Hz, so a
  drift of 1 or 2 Hz/s over the 17.5–27 s signals (19–51 Hz) loses the
  decode (Matched CER 0.232 and 0.532 against Envelope's 0.080 and 0.113;
  Task 14: 0.486 and 0.777; "not meaningful (oracle anchor)"); at
  0.5 Hz/s (up to 13.5 Hz) Matched still read 0.022. Through the
  detector the anchor follows the drift (Task 13's
  `MatchedReportsDriftingFrequency`); no suite group measures that yet.
- **Groups G and H (QSOs).** Ragchew (G, 25 WPM, oracle) crosses
  CER 0.10 at 6.9 dB (6.6 to 7.1 dB, Envelope) and 3.1 dB (2.8 to 3.2 dB,
  Matched; Task 14: 3.8 dB, 3.4 to 5.3 dB), 1.8 and 2.0 dB above group A
  at 25 WPM: the QSO text (prosigns, abbreviations, pauses between overs)
  costs about 2 dB on both. At CER 0.05 Envelope crosses at 7.6 dB (7.5
  to 7.7 dB) and Matched at 3.8 dB (no interval: fewer than 95% of the
  resamples reached 0.05; Task 14: 9.2 dB, where the order was
  reversed); Matched still keeps a residual CER of 0.012–0.018 more than
  Envelope at 8–20 dB (below; Task 14: 0.013–0.033). Two-station QSOs
  through the detector: same-track QSOs stay one track on both paths
  (1.00), and Matched is **worse** on the QSO label (0.212 against
  0.118; paired +0.104, +0.053 to +0.172, drawn offsets; Task 14: 0.197,
  +0.078). The answering station's first word, the retune delay's
  measure, is better with Matched at 10 and 25 Hz (0.110 and 0.190
  against 0.213 and 0.422) and worse at 0 Hz (0.287 against 0.199); the
  option-1 simulation (Design decisions B) predicted a median of 0–1
  character lost at 10–25 Hz for answering stations at −6 to +6 dB re
  the caller, consistent with these rates for 3–5 character first words
  (a first-word CER of 0.15–0.25 is one character in 4–7; an upper
  bound). At 50 Hz (ambiguous on both paths; on the Matched path it is
  3 Hz beyond D_ch = 47 Hz, inside the band where interpolated
  frequencies can read closer than D_ch; section 11, "QSO regimes")
  Matched reads the QSO label much worse (answering station's first word
  0.379, over 0.143, caller's over 0.279, against Envelope's 0.032, 0.044
  and 0.079): per QSO label the Matched CER was 0.557, 0.234, 0.046,
  0.374, 0.025 and 0.043 (Task 14: 0.533, 0.154, 0.046, 0.415, 0.032,
  0.040; Envelope 0.023–0.140). The two worst are exactly the QSOs that
  **split into two tracks** on the Matched path, the same two as in
  Task 14 (the other 4 stayed one track, the caller's track following
  the answering station; tracks within 25 Hz of either carrier in
  `build/suite/full3/results/matched/H-qso-s*.json`). In Task 14 the
  caller's track stayed on the caller in both (last frequency 0.9 and
  1.0 Hz from it) and the answering station's overs read CER 0.936 on
  it (first word 0.636), because the Matched channel is re-centered on
  the caller behind the dit-matched filter, whose main lobe is ±r/K =
  ±21–33 Hz at 20–32 WPM (derived); now, in one of them (`H-qso-s1`,
  QSO 6), the answering station's overs read 0.021 and the caller's
  1.030 (not diagnosed), in the other (`H-qso-s2`, QSO 7) 0.673 and
  0.114. The Envelope caller channel (±150 Hz, no re-centering) decoded
  the answering station in its own 3 split QSOs (QSO-label CER 0.023,
  0.140, 0.028). So the QSO-label loss at 50 Hz is the split, a QSO read
  through the view that does not fit it; the per-station view, which
  fits a split QSO, reads poorly at 50 Hz on both paths (0.947 Envelope,
  0.977 Matched, all 12 station labels), so these runs do not show how
  well Matched decodes the answering station on its own track. With
  the growth bound now applied per mark (Task 15) the two split QSOs
  still read worst, so a dit-estimate runaway, which the design
  simulation's 12 of 30 failures before the first-step bound came from,
  is not what fails them here (inferred). Separate-track QSOs: Matched
  is worse per station (paired +0.055, +0.038 to +0.074, drawn; +0.034,
  +0.015 to +0.051, 200 Hz). In the oracle copy, which shows the
  turnover apart from detection, the rows within ±12 Hz of the label
  show no difference beyond the interval at 0 Hz (paired −0.056) and a
  small one at 10 Hz (+0.040, +0.007 to +0.078; Task 14 +0.026, −0.004
  to +0.078), and per station Matched is better wherever the view fits
  (200 Hz: 0.081 against 0.277).
- **Strong signals, pauses, tune-up, first sample (detector).** Strong
  (S₅₀₀ 30–60 dB): Matched is slightly worse at every level (paired
  +0.004 to +0.020, intervals excluding 0 at 30, 50 and 60 dB; CER
  0.045–0.055 against 0.028–0.052), with first-word CER 0.857–1.000
  against 0.571–0.867. Pauses: Matched is better after 10 s (0.034
  against 0.144) and equal within the intervals at 2 and 5 s; after 20 s
  both lose the station's track (0.707 and 0.644; paired +0.064, +0.046
  to +0.080). First sample: no difference beyond the interval (+0.018,
  −0.001 to +0.033). Strong, pauses and first sample are unchanged from
  Task 14 to three decimals. **Tune-up carriers of 1 s and 2 s break
  Matched** (CER 0.542 and 0.909 against 0.000 and 0.029, unchanged;
  0.3 and 0.6 s carriers are harmless, 0.010 and 0.003, Task 14 0.014
  and 0.032); see "Limits measured".
- **Crowded and band (detector).** Matched is better at 0, 50 and 100 Hz
  spacing (paired −0.247, −0.280, −0.251) and not different beyond the
  interval at 200 Hz (+0.007, −0.005 to +0.020; Task 14 +0.029) or on
  the band (−0.030).

**Where Matched is worse than Envelope** (paired Matched − Envelope CER
whose 95% interval excludes 0, or a crossing worse beyond its interval;
the default stays Matched, owner decision 2026-09-29; no parameter was
changed; Task 14's value after "was"):

- Per condition (summary.md), **still worse**: C fists, bug imbalance
  +0.1 (+0.349, +0.192 to +0.467; was +0.317); E, 20 Hz interferer at
  +10 dB re the wanted station's key-down power (+0.221, +0.088 to
  +0.352; was +0.349); H through the detector: same-track drawn (+0.104,
  +0.053 to +0.172; was +0.078), ambiguous drawn (+0.442, +0.368 to
  +0.507; was +0.352), separate-track per station (drawn +0.055, 200 Hz
  +0.034; was +0.057 and +0.029); strong at 30, 50, 60 dB (+0.018 to
  +0.020, unchanged); pauses 20 s (+0.064, unchanged); tune-up 1 s
  (+0.562) and 2 s (+0.899), unchanged. C fists, paddle imbalance +0.1,
  still crosses CER 0.10 for Envelope at 8.4 dB (5.0 to 9.5 dB) and not
  at all for Matched. **Newly worse**: C fists, bug imbalance +0.0
  (+0.239, +0.062 to +0.389; was +0.117, −0.043 to +0.269) and hand
  imbalance +0.1 (+0.157, +0.070 to +0.242; was +0.069, −0.017 to
  +0.156) (Matched CER 0.505 → 0.570 with Task 15 alone → 0.622 with
  Task 16 added, and 0.424 → 0.550 → 0.510: mostly Task 15); D 10 WPM
  (+0.061, +0.027 to +0.121; was +0.021, lower bound +0.000), from
  Task 16 (the 12 WPM start-up loss, section 8); H same-track at 10 Hz
  (+0.043, +0.006 to +0.081; was +0.028, lower bound −0.000) and its
  oracle copy (+0.040, +0.007 to +0.078; was
  +0.026). **No longer worse**: G ragchew at CER 0.05 (Matched 3.8 dB,
  no interval, against Envelope's 7.6 dB, 7.5 to 7.7 dB; was 9.2 dB) and
  C fists, machine imbalance +0.1 at CER 0.10 (Matched ≤ 5.0 dB, as
  Envelope; was 8.4 dB, 5.0 to 9.2 dB). Borderline (interval's lower
  bound rounds to +0.000): strong 40 dB (+0.004), tune-up 0.3 s (+0.003)
  and 0.6 s (+0.003). (F drift at 1 and 2 Hz/s and the oracle H rows
  beyond ±12 Hz are worse too, but not meaningful: oracle anchor.)
- **A Matched row worse than in Task 14 beyond its interval**: D speed
  step 20 → 35 WPM, CER 0.113 (0.058 to 0.163) → 0.394 (0.222 to 0.499),
  still not worse than Envelope (paired −0.123, −0.434 to +0.165; was
  −0.412). It comes from Task 15 alone (0.394 with Task 15 and without
  Task 16): in 5 of 6 signals the text stops within a few characters
  after the step; not diagnosed. The only row better beyond its interval
  is F drift 2 Hz/s (0.777 → 0.532, not meaningful: oracle anchor).
- Per S₅₀₀ point (paired over the stations of a point: 12 in group A,
  9 in groups B and C, 6 in group G): 30 of 209 points in groups A, B,
  C and G favor Envelope and 76 favor Matched (was 31 and 73); at 95%
  about 10 of 209 would exclude 0 by chance, so single points are weak
  evidence, a run of them is not. **Group B at high S₅₀₀ with slow
  fading and random timing**, fewer than before (9 points, was 18):
  paddle 12 WPM f_D 0.1 Hz at 27.78, 37.78 and 57.78 dB (+0.222, +0.144,
  +0.175), paddle 24 WPM f_D 0.1 Hz at 57.78 dB (+0.080), hand 24 WPM
  f_D 0.1 Hz at 13.78 and 57.78 dB (+0.073, +0.171), the VE3NEA mix at
  17.78 dB (+0.098), and two low points, hand f_D 1 Hz at 1.78 dB
  (+0.049) and f_D 3 Hz at 4.78 dB (+0.058); no longer paddle 24 WPM
  f_D 0.1 Hz at 1.78, 17.78 and 37.78 dB, paddle 12 WPM at 17.78 dB,
  paddle or hand f_D 0.3 Hz at any point, or hand f_D 0.1 Hz at
  17.78 dB. **Group C at 5–20 dB with bug or hand keying or positive
  imbalance**, more than before (11 points, was 8): bug +0.0 at 5 and
  20 dB (+0.219, +0.313), bug +0.1 at 5, 10 and 20 dB (+0.284, +0.355,
  +0.408), bug −0.1 at 20 dB (+0.141), hand +0.0 at 20 dB (+0.134),
  hand +0.1 at 10 and 20 dB (+0.168, +0.268), paddle +0.1 at 10 and
  20 dB (+0.032, +0.051). **Group A at 12 WPM, 8–18 dB** (5 points, new,
  +0.004 to +0.022: the 12 WPM start-up loss) and 40 WPM at −2 dB
  (+0.115, new). **Group G at 8–20 dB**, small (+0.012 to +0.018; was
  +0.013 to +0.033). The pattern is unchanged: Matched's advantage is
  at low S₅₀₀; at high S₅₀₀ with random keying timing it loses a few to
  41 CER points. Its marks are about 7 ms longer at 25 WPM (ŝ's ramp
  bias, "Amplitude estimate" above) and a positive imbalance lengthens
  them further, shortening the element spaces the character decisions
  rest on; that this causes the loss is a hypothesis, not measured. The
  fading loss at high S₅₀₀ is not diagnosed.

**Limits measured** (diagnosed by focused runs and debug prints that are
not in the repository; nothing was changed):

- **A channel that opens mid-transmission.** The speed-estimate part is
  **fixed by Task 16** (section 8, "Marks whose start was not observed"):
  a fragment timed from a mark in progress when the warm-up ends is
  decoded (as a dit) but no longer enters the speed estimate. Every cut
  of the test `MatchedChannelOpeningInsideADahCountsNoFragment` (warm-up
  ending −10 to +40 ms before the end of U's dah, 1 ms steps; 7 of them
  time a 14.7–20.7 ms fragment) decodes every word after the first
  exactly (measured, Task 16).
  **Task 14's diagnosis** (the record; code before Tasks 15 and 16): on
  the smoke recording (`bench/smoke.sh`) Matched read CER 0.0622 against
  Envelope's 0.0353 and 0.0145 with oracle channels (Matched). One
  station, +7617.6 Hz, 18.5 WPM (dit 64.9 ms), S₅₀₀ 25.1 dB, keying from
  1.076 s, decoded at CER 0.234 through the detector: its channel opens
  at about 1.6 s, and its text started "E ETT TT 7 L T U U A 7L" for
  "UA7L TU UA7L" and then recovered. Cutting the recording at 1.600 s
  and decoding that station with an oracle channel (which opens at the
  cut) reproduced the same text exactly (11 edits in 47 symbols), so the
  detector is not involved; cut at 1.620 s the Matched decode lost only
  the first word, sent partly before the cut (3 edits). Mechanism (from
  the code, confirmed with debug prints): the Matched decoder's warm-up
  (0.32 s, "Warm-up" above) keys nothing; cut at 1.600 s it ended 15 ms
  before the end of the dah of U (1.725–1.919 s, plus about 18 ms of
  filter delay), so the first mark the decoder timed was a 15.3 ms
  fragment of it, longer than the 14.4 ms glitch limit (0.3 × the 48 ms
  initial dit at 25 WPM); cut at 1.620 s the warm-up ended just after
  that dah and no fragment was timed. With marks {15.3, 60.7 ms} the
  speed estimate (section 8, step 10) splits at the ratio 3.97, the
  fragment is the whole "dit" cluster, the dah/dit ratio exceeds 3.85,
  and the averaged estimate, 17.8 ms, is clamped to 20 ms (60 WPM). With
  a 20 ms dit, the station's 65 ms dits read as dahs, its element spaces
  end characters and its 195 ms character spaces read as word spaces.
  The fragment stayed the only member of the dit cluster for the next 24
  marks: the estimate rose through 29.8, 41.5 and 35–37 ms and the text
  recovered once the fragment left the window.
  **After Tasks 15 and 16 (Task 17, measured):** the smoke recording
  reads Matched CER 0.0436 (42 edits in 964 symbols; was 0.0622, 60
  edits; Envelope unchanged at 0.0353, 34 edits), the +7617.6 Hz station
  through the detector 0.043 (was 0.234); cut at 1.600 s the station
  reads "E UA7L TU UA7L …", 2 edits in 47 (was 11), and cut at 1.620 s
  3 edits (unchanged). How general it is (focused run: 4 stations per
  speed, machine keying, 30 s at 48 000 samples/s, noise seeds 700–703,
  each recording cut at 20 random times in 2–6 s, so 80 oracle channels
  per speed open at random phases of the keying; openings whose Matched
  decode had at least 5 more edits than Envelope's, and at least 5
  fewer; "before" is Task 14's code, 74182aa, which reproduced Task 14's
  counts exactly; one run each, so no interval):

  | Speed | S₅₀₀ 25 dB, Matched worse / better: before → after | S₅₀₀ 10 dB, Matched worse / better: before → after |
  |---|---|---|
  | 12 WPM | 5 / 0 → 6 / 0 of 80 | 12 / 0 → 15 / 0 of 80 |
  | 18.5 WPM | 6 / 0 → 0 / 0 of 80 | 3 / 1 → 0 / 1 of 80 |
  | 25 WPM | 0 / 0 → 0 / 0 of 80 | 2 / 0 → 1 / 0 of 80 |
  | 40 WPM | 0 / 16 → 0 / 16 of 80 | 1 / 78 → 0 / 78 of 80 |

  Matched edits over the 80 openings, before → after (Envelope; symbols):
  at 25 dB 889 → 947 (762; 2800) at 12 WPM, 644 → 549 (522; 2800) at
  18.5 WPM, 664 → 663 (692; 2800) at 25 WPM, 1273 → 1277 (1421; 5680) at
  40 WPM; at 10 dB 862 → 803 (633), 573 → 524 (495), 826 → 805 (862),
  1235 → 1196 (3016). **Every opening that lost the rest of its text
  before** (23–70 edits: the dit stuck low or a runaway, the "DE K****V"
  case among them) **now decodes after its first word**, with one
  exception that is unchanged (25 WPM, S₅₀₀ 10 dB, one channel with no
  text at all, 35 edits in 35, Envelope 15; not diagnosed). **What
  remains, and its shape:** (a) the first word, sent partly before the
  opening, is lost or shortened on both paths (replay, backlog); at
  12 WPM Matched loses up to two words more than Envelope in some
  openings ("TSC K1ABC UR 5NN" for "CQ TEST DE K1ABC K1ABC UR 5NN",
  16–18 edits against
  Envelope's 6–7; unchanged by the fixes); (b) new, at 12 WPM: the first
  characters after the opening read as a string of T's ("T T T TTT T DE
  K1ABC" for "CQ TEST DE K1ABC", 11–15 edits against Envelope's 6–10),
  the 12 WPM start-up loss of section 8 (cause inferred, not
  instrumented); it accounts for the rise in the 12 WPM worse counts
  above, each such opening 3–7 edits worse than before, and it is over
  within the first two words. In the suite's detector-path groups (band,
  crowded, strong, pauses, first sample: every channel opens
  mid-transmission) Matched is worse than Envelope by more than 0.1 CER
  for 1 of 36 stations at 10–15 WPM, 3 of 35 at 15–20, 2 of 144 at
  20–30, 1 of 114 at 30–45 and 0 of 91 at 45–60 WPM (was 3, 2, 4, 1,
  3), and better by more than 0.1 for 8, 3, 25, 20 and 30 (was 8, 4, 25,
  20, 30). Envelope did not fail on these openings: its warm-up (one
  dit, 48 ms) keeps its squelch closed through a mark in progress unless
  that mark filled less than a third of the warm-up (f < 1/3, derived in
  section 8, "Marks whose start was not observed"). Replay ("Wrong or
  missing first characters", backlog) would remove the late opening
  itself.
- **Start-up runaway.** Three runs in Task 14 showed the same chain:
  (1) the first mark or two were misleading (a fragment, a first mark
  keyed late while ŝ rises from the warm-up, or merged elements at low
  S₅₀₀ through the 16 ms acquisition filter); (2) at the 8th mark the
  filter started following, and the widening cascade (section 8, "Two
  decoders") stretched the mark just ended; (3) with one mark of
  intermediate length in the window, no neighbor ratio reached 1.8, the
  estimator took the mean of dits and dahs as the dit (section 8, step
  10), about twice the true dit, and the bound, then applied ×1.25 per
  update (the defect), let it grow there within one mark. Cases: A,
  25 WPM (dit 48 ms), S₅₀₀ 10 dB, +6606.5 Hz in `A-awgn-25wpm-1-s1`:
  first mark 100.7 ms (a 144 ms dah keyed late), estimate correct
  (45.7 ms) for 8 marks, then the cascade stretched a dit to 62 ms and
  the estimate went 58.3 → 72.9 → 91.1 → 93.8 ms within 23 ms; CER
  0.464, recovering later. A, 25 WPM, S₅₀₀ 2 dB, −2991.9 Hz in
  `A-awgn-25wpm-0-s2`: merged elements gave an estimate of 105–120 ms
  before following began, the cascade widened the filter from 20 to
  119 ms within one mark, the 48 ms element spaces filled in, marks grew
  to 287–503 ms, the estimate to 210 ms, and decoding stopped (CER 0.981;
  the bound slowed the runaway, it did not stop it). The 18.5 WPM
  late-opening case above that read "DE K****V" followed the same chain
  from a 113.3 ms fragment. **After Tasks 15 and 16 (Task 17,
  measured):** Matched CER 0.464 → 0.000 (`A-awgn-25wpm-1-s1`) and 0.981
  → 0.015 (`A-awgn-25wpm-0-s2`), and "DE K****V" now reads "DE K1ABC
  K1ABC UR 5NN 14 TU" (8 edits, as Envelope). Which steps remain:
  Task 15 removes step (2)'s cascade (a mark can still be re-opened by
  the widened filter, and timed up to 0.1 of the old dit longer, derived
  in section 8) and step (3)'s growth within one mark (the dit estimate
  now needs ln 2/ln 1.25 = 3.1 marks to double, derived); step (3)'s
  estimator branch, the mean of both clusters taken as the dit, is not
  addressed. Of step (1)'s causes, Task 16 keeps a fragment and a first
  mark keyed late while ŝ rises (after the warm-up, an opening in noise
  or a re-acquisition) out of the speed estimate; merged elements at low
  S₅₀₀ are not addressed. The −2991.9 Hz case began with merged elements
  and now decodes (0.015), so bounding the growth per mark was enough
  there; whether merged elements still start runaways elsewhere is not
  shown by these runs. No group A station at S₅₀₀ ≥ 6 dB reads above CER
  0.110 any more (Task 14: 0.155–0.464 for the four runaway stations).
- **Tune-up carriers of 1 s or more** (unchanged by Tasks 15 and 16,
  re-measured in Task 17). Cutting `tune-up-s1` so that an oracle
  channel opens during a 2 s carrier (at 1.2 or 2.0 s) or a 1 s carrier
  (at 1.6 s) gives no Matched text at all (CER 1.000, before and after);
  opening in the 0.5 s silence after the carrier gives CER 0.000 (2 s
  carrier, cut at 2.9 s) and 0.021 (1 s carrier, cut at 2.4 s);
  Envelope decodes every cut (CER 0.000–0.103; 0.051 and 0.000 at these
  cuts). With the channel open from the start of the recording, the 1 s
  carrier does little harm (0.064) but the 2 s carrier still does
  (0.974), both as in Task 14; the suite rows are identical to Task 14's
  (CER 0.542 and 0.909). Through the detector the channel opens at least
  0.5 s after the station appears (section 6, persistence), so it likely
  opens during a 1–2 s carrier. Likely mechanism (from the code, not
  instrumented; neither fix touches the noise estimate, so no change was
  expected, and the mechanism is still not instrumented): a warm-up
  (0.32 s) that sees only carrier sets σ̂_v² from the carrier's own
  power, and a carrier longer than the noise floor's window (64 samples
  K apart, 1.02 s at K = 24) lifts the floor to the carrier's level;
  with σ̂_v² near the station's power, a = ŝ/σ̂_v stays below the
  squelch, and the station's own marks (|v|²/(2σ̂_v²) below κ = 1.75)
  pass the noise guard and hold σ̂_v up. Envelope's speed estimate
  excludes marks longer than 0.96 s (section 8, step 10) and its levels
  are set by one dit.

## 8c. Bank decoder (milestone 2c; selectable, not the default)

The bank decoder is the C++ port of the stage-1 Python prototype
(`training/kz4ap_proto`); it is a *decoder* alongside Envelope and Matched.
This section covers the
branch filters and the envelope likelihood (`engine/src/bank/filters.cpp`),
the noise estimates (`engine/src/bank/noise.cpp`, "Noise" below), the
keying (`engine/src/bank/keying.cpp`, "Keying" below), the duration
fit (`engine/src/bank/fit.cpp`, "Duration fit" below), the periodicity
estimate (`engine/src/bank/periodicity.cpp`, "Periodicity" below),
the text model and branch selection (`engine/src/bank/selection.cpp`,
"Text model and branch selection" below) and the channel decoder that
drives them, with new overs, re-keying and the published text with its
corrections (`engine/src/bank/channel.cpp`, "Channel decoder" below),
each checked against golden values from the prototype (relative 1e-9;
discrete outputs exactly). The engine runs it behind its `Decoder`
interface (`BankDecoder`, `kz4ap-bench --decoder bank`; "The bank decoder
behind the engine" below), and the replay tool `kz4ap-bank-replay`
(bench) runs it on recorded channel streams.

**The bank.** Instead of one matched filter whose length follows an
estimated speed (section 8b), the bank runs K = 32 boxcar filters at once,
one per speed on a geometric ladder, and lets the later stages choose
among them.

- **Ladder.** Branch k = 1 … K has duration
  L_k = L_1 · ρ^(k−1), with L_1 = β · 1.2 s / WPM_max, ρ = 1.1
  (dimensionless ratio of neighboring branches). With β = 0.8 dit and
  WPM_max = 100 words/min this is L_1 = 0.8 · 1.2 s / 100 = 9.6 ms, so
  L_k = 9.6 ms × 1.1^(k−1). The ladder stops at the first branch within
  one step of the optimum for WPM_min = 5 words/min,
  L_max = β · 1.2 s / WPM_min = 192 ms: the count is
  K = ⌈ln(L_max / L_1) / ln ρ − 10⁻⁹⌉ = 32, giving L_32 = 184.3 ms
  (9.6 ms to 184.3 ms). One dit lasts 1.2 s / WPM, so each branch is
  the matched length β·dit of one speed.
- **Boxcar.** Branch k is the causal, unity-gain boxcar over
  N_k = max(1, round(L_k · r)) samples of the channel stream u at
  r samples/s (ties to even), the point where seconds become samples:
  v_k[m] = (1/N_k) · Σ u[m−N_k+1 … m], with zeros before the start of the
  stream (so the first N_k − 1 outputs ramp up). It is computed as a
  running cumulative sum c (complex, summed in order from the stream's
  start), v_k[m] = (c[m+1] − c[max(m+1−N_k, 0)]) · (1/N_k): numpy divides
  a complex array by N_k + 0j by multiplying both parts by 1/N_k, which can
  differ from dividing by N_k in the last bit, so the port multiplies too.
  The channel decoder stores the power |v_k|² rounded to single precision
  (float32), as the prototype does, and every later stage reads those
  rounded values (FS²). |v_k| is computed as numpy computes it for
  complex128 on this build (its vectorized loop): the larger of |Re|, |Im|
  times √(fma(ρ, ρ, 1)), ρ = smaller / larger (0 for 0); measured equal to
  `np.abs` on 200 000 boxcar outputs (numpy 2.5.3, x86-64), where `hypot`
  and `std::abs` differ from it in the last bit for about 4% of values.
  At r = 1500 samples/s N_k runs from 14 to 276; at 2000 samples/s the same
  durations give 19 to 369 samples. The realized duration is N_k / r (s).
  Its power response is
  |H(f)|² = (sin(π f N_k / r) / (N_k sin(π f / r)))², which is 1
  (dimensionless, relative to 0 Hz) at 0 Hz.
- **Envelope likelihood.** For a branch output with normalized envelope
  x = |v_k| / σ_v (σ_v in FS, the noise's per-component scale) and
  normalized key-down amplitude a = ŝ / σ_v, the log-likelihood ratio of
  key-down (Rician envelope) over key-up (Rayleigh) is
  Λ = −a²/2 + ln I₀(a·x), in nats (Proakis & Salehi, eq. 4.5-21; the same
  expression as Matched, section 8b). ln I₀ is the logarithm of the
  Abramowitz & Stegun polynomial approximations: 9.8.1 for z < 3.75
  (I₀ itself, published error bound |ε| < 1.6 · 10⁻⁷) and 9.8.2 for
  z ≥ 3.75 (the scaled form √z · e⁻ᶻ · I₀(z), bound |ε| < 1.9 · 10⁻⁷).
  The bounds are the published ones for the approximations; they were
  not re-measured here. The bank calls the same functions Matched uses
  (`kz4ap::log_bessel_i0` and `kz4ap::envelope_llr`, in
  `matched_front_end.cpp`), whose formula equals
  the prototype's term for term. The probability of key-down is the logistic
  p = 1 / (1 + exp(−g)) of the log-odds g in nats (Λ plus the prior
  log-odds), with g clipped to ±50 nats so that exp never overflows.

Status of each value (as `training/kz4ap_proto/params.py` marks it):
WPM_min = 5 words/min and WPM_max = 100 words/min are owner decisions;
the step ρ = 1.1 is an owner decision; β = 0.8 dit is heuristic (the
shape matched filter is derived, the fraction is not); the logistic clip
(±50 nats) and the 10⁻⁹ guard in the branch count are numerical choices,
not tuned.

### Noise (bank decoder)

`engine/src/bank/noise.cpp`, a port of `training/kz4ap_proto/noise.py`,
checked against the prototype's golden values (below). The keying needs
σ²_v,k, the noise variance per real component of each branch output v_k,
in FS². It comes from two sources: the **level** of branch 1 by the
three-tap guard of Matched (section 8b), and the **ratios** between
branches from one shared noise spectrum of the channel stream u. The
estimate advances once per block of round(block_s · r) samples
(block_s = 32/1500 s = 21.33 ms; 32 samples at r = 1500 samples/s),
before that block is keyed. The three-tap guard judges the block's
samples against σ² as it stood at the block's start; the spectrum's mask
uses σ²_v,1 after that update (below). The inputs are u (FS) and the
branches' |v_k|² (FS²), which the prototype stores in single precision
(float32); the noise estimate reads those rounded values.

- **Three-tap level.** For each sample n of the block with n ≥ 2N_k, the
  middle tap |v_k[n−N_k]|² is accepted if it is below κ · 2σ² and both
  neighbors, |v_k[n]|² and |v_k[n−2N_k]|², are below κ_n · 2σ², with
  κ = 1.75 and κ_n = 4 (dimensionless; taps N_k apart share no inputs,
  so in white noise they are independent). With c accepted taps in the
  block (their mean |v|², FS², written μ), the update is
  σ² ← max(σ² + s·(μ / (2·m(κ)) − σ²), 10⁻²⁰ FS²), where
  s = max(1 − (1 − α)^c, c / W), α = 1 − exp(−1 / (τ_n · r)) per sample,
  τ_n = 2 s, and W is the number of taps accepted so far (a count),
  starting at 0.1 × the warm-up's sample count (48 at 1500 samples/s).
  m(κ) = 1 − κ·e^(−κ)/(1 − e^(−κ)) = 0.632 at κ = 1.75 is the mean of an
  exponential variable with mean 1 truncated at κ (derived: in Gaussian
  noise |v|²/(2σ_v²) is exponential with mean 1; section 8b). A block in
  which no branch accepts a tap changes nothing, not even W. The floor of
  10⁻²⁰ FS² (−200 dBFS) is a numerical choice that keeps x = |v|/σ_v
  finite on noise-free input.
- **Warm-up.** Up to and including the first block that ends at or after
  round(0.32 s · r) samples (480 at 1500 samples/s), σ² is recomputed each
  block as max(Q₀.₂ / (2·(−ln 0.8)), 10⁻²⁰ FS²), where Q₀.₂ is the 20%
  quantile of |v_k|² over every sample from the stream's start, by
  numpy's default ("linear", Hyndman & Fan type 7) definition, ported
  exactly (`bank::quantile_linear`). In Gaussian noise the 20% quantile of
  |v|² is 2σ_v²·(−ln 0.8) (derived), so in noise alone the estimate is
  consistent: it converges to σ_v² as the warm-up grows. Over the 480
  correlated samples of the warm-up it is not exactly unbiased (not
  computed).
  Matched's floor lift (section 8b, "Floor") is not part of the
  prototype and is not ported.
- **Spectrum shape.** u is cut into consecutive, non-overlapping segments
  of M = max(16, round(T_seg · r)) samples from sample 0
  (T_seg = 256/1500 s = 170.7 ms; M = 256 at 1500 samples/s, bins
  r/M = 5.86 Hz wide). A segment starting at sample s is examined in the
  first block whose end n₁ satisfies s + M + N_1 − 1 + R ≤ n₁, with
  R = round(0.02 s · r) = 30 samples (the guard margin), because its mask
  needs |v_1|² that far ahead. Segments examined before the three-tap
  warm-up has ended are discarded. **Mask:** sample u[i] feeds
  v_1[i … i + N_1 − 1]; it is left out if any |v_1[j]|², j from i − R to
  i + N_1 − 1 + R, is at or above κ_n · 2σ²_v,1 (κ_n = 4), where σ²_v,1
  is the three-tap level after the current block's update (the update
  runs first, then the segments due in this block are masked). A segment
  enters only if at least 50% of its samples are left in. With w the
  periodic Hann window 0.5 − 0.5·cos(2π i/M) times the mask (1 kept,
  0 left out), its periodogram is I_m = |DFT(u·w)_m|² / Σ w² (FS² per
  bin; its mean over the M bins is the power per sample). The shape S
  starts at the first accepted periodogram and then follows
  S ← S + max(β, 1/n_seg)·(I − S), β = 1 − exp(−T_seg/τ_n) = 0.0818
  (n_seg the number of accepted segments, including this one). The DFT
  is the engine's pocketfft in double precision (`detail::fft_forward`
  for `std::complex<double>`, `engine/src/fft.hpp`): unnormalized, bin m
  at m/M cycles per sample, m = 0 … M−1, the same definition and order
  as `numpy.fft.fft`, so no rescaling or reordering is needed.
- **Smoothing and branch weights.** S is smoothed by a circular moving
  mean over 2h + 1 bins, h = round(25 Hz · M / r) = 4 bins (±23.4 Hz at
  1500 samples/s). W_k[m] (dimensionless) is the mean of the boxcar's
  power response |H_k(f)|² over 16 equally spaced frequencies across
  bin m (offsets ((j + ½)/16 − ½)·r/M, j = 0 … 15, around the bin's
  center as `numpy.fft.fftfreq` places it, so bins above M/2 are the
  negative frequencies).
- **Per-branch variances.** With S̃ the smoothed shape and b_k the mask
  bias of branch k, the default ("spectrum", variant (a)) is
  σ²_v,k = σ²_v,1 · [(W_k·S̃)/b_k] / [(W_1·S̃)/b_1]: the level from the
  three-tap guard, the ratios from the spectrum. Variant (b)
  ("spectrum-level") takes the level from the spectrum too:
  σ²_v,k = 0.5·(W_k·S̃)/(M·b_k) (the complex power of v_k is
  (1/M)·Σ_m I_m W_k[m], half of it per real component; derived). Before
  any segment has entered, both use σ²_v,k = σ²_v,1 · N_1/N_k, exact for
  white noise (Parseval; derived). The fallback ("branch") runs the
  three-tap estimate on every branch separately.
- **Mask bias b_mask,k.** The mask leaves out mostly low-frequency power
  (the station is near 0 Hz), so the masked spectrum reads each branch's
  noise low by a factor that differs per branch and does not cancel in
  the ratio. b_k = (σ²_v,k from the masked, smoothed spectrum) / (true
  σ²_v,k), dimensionless, runs from 0.8370 (k = 1) to 0.7852 (k = 32);
  measured in the prototype (white noise of 1 FS² per complex sample,
  seeds 101–110, 60 s each, 2362 of 3500 segments accepted; per-seed
  scatter 2.6% at k = 1 to 4.5% at k = 32). In channel-shaped noise the
  same ratios are 2.9% (k = 1) to 5.9% (k = 32) higher (measured in the
  prototype), so the estimate is that much low there. The table is valid
  only for the default ladder, T_seg, smoothing, guard margin, clean
  fraction, κ_n and three-tap settings at 1500 samples/s; it has not
  been re-measured at other rates.

**Port check.** Golden values (`engine/tests/data/bank/noise.json`, from
`kz4ap_proto.golden`): 20 s of a 1 FS carrier keyed at 25 WPM from 1.0 s
at S₅₀₀ = 15 dB (SNR in 500 Hz), passed through the channel
filter's shape so the noise is channel-shaped, rounded to complex64 (the
engine's sample type; stored as `noise_stream.c64`, and the prototype ran
on the rounded stream), run block by block as the
prototype's channel runs it, σ²_v,k of all 32 branches compared after
every 10th block (93 instants). Measured largest relative difference
(Windows build): 2.4 · 10⁻¹⁵ ("spectrum"), 2.0 · 10⁻¹⁵ ("spectrum-level"), 0 ("branch");
accepted and offered segment counts equal at every instant.

**Exact zeros (prototype behavior, ported as is; fix deferred to Plan B).**
On a stream that
starts with exact zeros (a zero-padded recording, a dead channel) every
|v_k|² is 0 FS², so the warm-up sets σ² = 10⁻²⁰ FS² and the three-tap
update keeps it there. When noise arrives, every tap exceeds κ·2σ², none
is accepted, and σ² stays at 10⁻²⁰ FS² indefinitely: the "cannot settle
low" argument of section 8b assumes ρ = σ̂²/σ² is not tiny, but its
acceptance (1 − e^(−κρ))(1 − e^(−κ_n ρ))² is about 10⁻⁵⁴ at
ρ = 10⁻²⁰/0.036 (computed). Meanwhile the spectrum accepts the all-zero
segments; from the first (the block ending at 576 samples, 0.384 s, at
1500 samples/s) S is identically 0, so variant (a)'s ratio is 0/0 = NaN
and variant (b)'s level 0 FS² from then on. Measured with the prototype
and pinned by `BankNoise.ExactZerosThenNoiseAsThePrototype` (5 s of zeros,
then 5 s of white noise of 1 FS² per complex sample); the plan's
requirement that no NaN reach a published value (Review Focus 3) is
`BankNoise.DISABLED_ExactZerosThenNoiseStayFiniteAndPositive`, disabled.
The controller's ruling (2026-10-03): Plan A, a faithful port, keeps the
prototype's behavior; the fix (the three-tap recovery from an all-zero
start, the spectrum's 0/0 ratio) is a Plan B item. Until then the bank
decoder may key nothing on digital silence, and the channel (Task 7)
must still keep NaN out of published text and corrections.

Status (as `params.py` marks it): κ = 1.75, κ_n = 4, τ_n = 2 s and the
0.32 s warm-up are Matched's (milestone 2; heuristic), with m(κ) and
the 2·(−ln 0.8) warm-up scale derived; T_seg, the ±25 Hz smoothing, the
20 ms guard margin and the 50% clean fraction are heuristic; b_mask,k is
measured; the choice of variant (a) is measured (prototype experiment
E10); the 16 points per bin and the 10⁻²⁰ FS² floor are numerical choices.

### Keying (bank decoder)

`engine/src/bank/keying.cpp`, a port of `training/kz4ap_proto/keying.py`
(`BankKeyer`, `hysteresis`, `edges`, `rekey`), checked against the
prototype's golden values (below). Every branch is keyed separately, all
32 once per block (round(block_s · r) = 32 samples at 1500 samples/s), in
this order: the noise estimate is updated (above), then the block is
keyed with the amplitude as it stood at the block's start, then the
amplitude is updated from the block. The inputs are |v_k|² (FS², the
float32-rounded values the prototype stores, widened exactly to double)
and σ²_v,k (FS², per real component). The keyer itself rounds nothing.
The lengths L_k it uses are the realized ones, N_k / r (s).

- **Normalization.** a_k = √(ŝ_k² / σ²_v,k) (dimensionless, from the
  amplitude estimate ŝ_k², FS², at the block's start) and, per sample,
  x = √(|v_k|² / σ²_v,k) (dimensionless).
- **Log-odds and posterior.** g = Λ(x, a_k) + ln(P₁ / (1 − P₁)) nats, with
  Λ the envelope log-likelihood ratio above and P₁ = 0.44 (prior log-odds
  −0.2412 nats); p = logistic(g). The posterior that leaves the keyer
  (for the periodicity estimate) is p where the squelch is open and 0
  where it is closed; the amplitude update uses p before the squelch.
- **Squelch.** Open while a_k ≥ a_min,k = 3 · (L_k / 16 ms)^(1/4)
  (dimensionless): 2.622 at L_1 = 14/1500 s = 9.33 ms to 5.525 at
  L_32 = 276/1500 s = 184 ms. In noise alone ŝ² is a mean over about
  τ_a / L_k independent samples, so its spread grows as √L_k and a_min as
  L_k^(1/4) (the scaling derived; the 3 at 16 ms heuristic, milestone 2's
  principle with each branch's own σ_v).
- **Full-LLR keying (amplitude known).** Key down where the squelch is open
  and g > h; key up where the squelch is closed or g < −h; otherwise the
  key state stays as it was; h = 1 nat. Up wins if both hold (they cannot
  both hold for h > 0). The prototype's vectorized hysteresis (the last
  event at or before each sample) is ported as a per-sample loop with the
  same state at every sample (`bank::hysteresis`).
- **Unknown-amplitude test (at an over's start).** While branch k's
  amplitude is unknown (`unknown`: from the stream's start, and from each
  `start_over` until `finish_over_start`), the key is a threshold test on
  x alone: key down where x > x_on,k, up where x < x_off, otherwise as it
  was. x_off = √(−2 ln 0.3) = 1.552: noise alone (Rayleigh, P(x > X) =
  e^(−X²/2)) is above it 30% of the time (derived from the heuristic
  release probability 0.3). x_on,k are measured per branch (prototype
  experiment E9a: channel-shaped noise, about 20 noise key-downs per
  branch in 2000 s, at a target R_fa = 0.01 false marks/s per branch):
  4.6428 (k = 1) … 4.2036 (k = 32), not monotone in k (sampling scatter;
  `BankConfig::x_on_values`). They are valid for the 129-tap channel
  filter's noise shape, the default ladder at 1500 samples/s, and x
  measured with the exact σ²_v,k; the decoder divides by its estimated
  σ²_v,k, whose white-noise mask bias is 2.9–5.9% off in channel-shaped
  noise (a 5% low σ² raises x by about 2.5% and the false key-down rate
  by about 1.6×, estimated in the prototype, not measured). Without
  calibrated values the nominal x_on,k = √(−2 ln min(0.5, R_fa · L_k))
  is used (4.308 at k = 1 … 3.549 at k = 32; heuristic: the envelope's
  upcrossings make it key 7–10× more than R_fa), the clamp at 0.5 a
  numerical guard (x_on ≥ √(2 ln 2) = 1.177), never active at the
  defaults. Marks keyed this way are provisional: the test keys down when
  x rises past x_on and up when it falls below x_off, low on the boxcar's
  ramps, so noise-free a rectangular mark of length d ≥ L_k measures
  d + L_k · (1 − (x_on + x_off)/a) (derived), up to L_k longer at high
  SNR, and noise delays the key-up (a space little longer than L_k can
  close up; measured in the prototype).
- **Amplitude, known (online EM).** One step per block for each branch
  whose amplitude is known: with w = Σ p over the block (samples of
  key-down weight) and the p-weighted mean m = Σ p·|v_k|² / max(w, 10⁻³⁰⁰)
  (FS²), W ← W + w and, if w > 0,
  ŝ² ← max(0, ŝ² + s·(m − 2σ²_v,k − ŝ²)),
  s = min(1, max(1 − (1 − α)^w, w / max(W, 10⁻³⁰⁰))),
  α = 1 − exp(−1 / (τ_a · r)) = 1.332 · 10⁻³ per sample at 1500 samples/s,
  τ_a = 0.5 s of key-down weight (the Rician mean square is 2σ² + s²;
  milestone 2's estimator, as Matched's, section 8b). The sums are
  computed in numpy's pairwise order (`np.sum` along a row; numpy's
  `pairwise_sum` ported in keying.cpp), the same mathematical sum.
- **Amplitude, unknown (seed).** While unknown, w = 0 (no EM step) and
  only keyed samples count: each block's keyed |v_k|² are appended to a
  memory of at most the most recent round(4 · W_min · r) = 4800 samples
  (3.2 s of keyed time; heuristic, a bound several W_min long), W grows
  by the number keyed, and ŝ² = max(0, Q₀.₉ − 2σ²_v,k) with Q₀.₉ the 90%
  quantile (numpy "linear", `bank::quantile_linear`) of that memory,
  FS². The 90% quantile, not the mean, because the mean is pulled down by
  the boxcar's ramps, which the test keys (milestone 2 seeds from the 90%
  quantile too; heuristic).
- **Re-key bookkeeping.** `ready_to_rekey`: unknown and W ≥ W_min =
  0.8 s · r = 1200 samples of keyed time (measured, prototype E9b:
  0.8 s adopted over 0.4 s and 0.2 s). `start_over(k)` keeps an
  established ŝ² as the fallback (`prev_amp2`; NaN while there is none),
  then sets ŝ² = 0, W = 0, empties the memory and marks k unknown.
  `finish_over_start(k, ŝ², key)` installs the re-keying's winning ŝ²
  (FS²), empties the memory, marks k known and sets its key state to the
  re-keyed stretch's last; W keeps the keyed-sample count (≥ W_min when
  called once ready), so the first EM steps after the switch move ŝ² by
  at most w / W rather than replacing it. The prototype's channel
  (`ChannelDecoder.run` in `channel.py`; its port is plan Task 7) calls
  them after each block's step, branch by branch: (1) `start_over(k)` when
  a new over is due, that is the key is up and more than
  T_new = max(0.5 s, 12 · T_g) has passed since branch k's last key-up in
  this over (none yet: not due; T_g the current fit's T_g, or
  1.2 s / 5 = 0.24 s without a fit), whether or not the amplitude is
  known; otherwise, while k is unknown: (2) `finish_over_start(k, …)`
  after the stretch is re-keyed, once the over has a mark and
  W ≥ W_min (candidates: ŝ² and, if finite, `prev_amp2`); or, if not,
  once 2 s (`rekey_timeout_s`) of channel time have passed since the
  amplitude became unknown (the stream's start, or a `start_over` of a
  known branch; a new over of a still-unknown branch does not reset it)
  or since the last time-out,
  (3) `finish_over_start(k, …)`
  with `prev_amp2` if it is finite and keys at least one sample of the
  stretch, and otherwise (4) `start_over(k)` again, which keeps k unknown
  so W_min of keyed time counts afresh from then on.
- **Re-keying a stretch (`rekey`).** The stored |v_k|² of a stretch are
  keyed again from key up with the full LLR at fixed σ²_v,k and ŝ²
  (a = √(max(ŝ², 0) / σ²_v,k)), the same g, h = 1 nat and squelch as
  above. The channel decoder chooses among candidate amplitudes ("Channel
  decoder" below, "Re-key").
- **Edges.** `edges` lists, per branch, (sample index, key state after it)
  at every change of key state against the state before the block: the
  marks and spaces the timing stage reads.

**Exact zeros.** The keyer adds nothing for exact-zero input beyond the
prototype: with σ² = 10⁻²⁰ FS² from the noise (above) and |v|² = 0 FS²,
x = 0 is finite. Where the noise's σ²_v,k is NaN (above), a_k, x, g and p
are NaN and every comparison with them is false: an unknown branch's key
state holds; a known branch's squelch reads closed (a_k ≥ a_min,k is
false), so its key goes up and its posterior out is 0 · NaN = NaN; its EM
skips the update (w = NaN is not > 0) but W becomes NaN; an unknown
branch's seed max(0, Q₀.₉ − 2·NaN) is 0 FS² (Python's max(0, NaN) and
std::max(0.0, NaN) both give 0). Faithful to the prototype in Plan A;
the fix is Plan B's.

**Port check.** Golden values (`engine/tests/data/bank/keying.json`, from
`kz4ap_proto.golden`): the prototype's `ChannelDecoder.run` itself, on the
noise check's 20 s stream (above), with its keyer replaced by a recording
subclass. Over 938 blocks the C++ keyer, fed the same float32 |v_k|² and
the C++ noise estimate, replays run's 56 calls (24 `start_over`,
32 `finish_over_start`, with their re-keyed amplitudes) after the blocks
they followed. Measured: all 5113 edges of the 32 branches and the
unknown flags of every block equal; a_k, Σp, W at 115 sampled blocks and
W, ŝ², prev_amp2 and `ready_to_rekey` before every call agree to a
largest relative difference of 4.3 · 10⁻¹³ (Windows build). `rekey` on run's first
re-key stretch of branch 14 (samples 0–3007, three amplitudes, one of
them below the squelch): edges equal.

Status (as §8b classifies them for Matched): P₁ = 0.44 derived (from PARIS:
key-down 22 of 50 dit units); τ_a = 0.5 s heuristic (the amplitude
estimate is a heuristic running form of an EM update); h = 1 nat
heuristic; the squelch's 3 at 16 ms heuristic, its L^(1/4) scaling derived; R_fa = 0.01 /s a heuristic
target kept by E9b; x_on,k measured (E9a); the release probability 0.3
(x_off = 1.552) heuristic; W_min = 0.8 s measured (E9b); the seed's
memory of 4 × W_min heuristic; the 0.5 clamp and the 10⁻³⁰⁰ guards
numerical choices.

### Duration fit (bank decoder)

`engine/src/bank/fit.cpp`, a port of `training/kz4ap_proto/fit.py`
(`DurationFit`, `class_priors`, `resolution_var_s2`, `class_logliks`,
`observations_loglik`, `classify_mark`, `classify_space`), checked against
the prototype's golden values (below). Each branch fits the timing of the
marks and spaces its keying produces (the edges above). Durations are in
s, variances in s², log-likelihoods in nats; densities are in ln(duration).

- **Parameters.** θ = (T, w, qT, T_g), all s: T the dit, w the key
  weighting, q the dah/dit ratio, T_g the gap timebase (T_g = T for
  standard spacing, larger for Farnsworth spacing).
- **Classes.** Marks: dit, median T + w; dah, qT + w. Spaces: element
  space T − w; character gap 3 T_g − w; word gap 7 T_g − w. Each class c
  is log-normal around its median μ_c: ln d ~ N(ln μ_c, s_c²) with
  s_c² = σ_ln² + σ_t² / μ_c² (σ_ln = 0.15 for marks, 0.25 for spaces,
  dimensionless; σ_t² the branch's timing-resolution variance, s², turned
  into ln units to first order). A class whose median is not positive
  (the element space when w ≥ T) is dropped without renormalizing the
  others' priors (the prototype's ruling: a penalty of about
  ln(1 − 0.647) = −1.04 nats per space on a reading with no element
  spaces).
- **Class priors** (derived from VE3NEA's CW character frequencies and
  word-length table, `kz4ap_synth.messages`): per character its dits and
  dahs, its elements minus one element spaces, a character gap after every
  character but a word's last ((L − 1)/L per character) and a word gap per
  word (1/L), L = 3.062 characters the mean word length. Among marks
  P(dit) = 0.5716, P(dah) = 0.4284; among spaces P(element) = 0.6467,
  P(character) = 0.2379, P(word) = 0.1154. Each is multiplied by 1 − ε.
- **Outlier class.** Probability ε = 0.05, log-uniform on [1 ms, 10 s]:
  density ε / ln(10 s / 1 ms) in ln d, a log-density of −5.216 nats.
  Every duration is clamped to [1 ms, 10 s] before it is scored, so the
  density is proper. A duration ≤ 0 s is an error in `class_logliks`,
  `observations_loglik` and the classifiers (`std::invalid_argument`,
  the prototype's ValueError); `DurationFit::add` ignores it.
- **Timing resolution.** σ_t² = 2 (L_k / max(a, 1))² + 2 / (12 r²), s²:
  an edge through a boxcar of length L_k is a ramp of slope ŝ/L_k, so noise
  of RMS σ_v moves its crossing by L_k / a (a = ŝ / σ_v); a duration has
  two edges; sampling at r adds 1/(12 r²) per edge (derived, first order,
  high SNR; a floored at 1).
- **Per-observation log-likelihood.** For class c,
  ℓ_c = ln((1 − ε) P_c) − ½ z² / s_c² − ½ ln s_c² − ln √(2π), z = ln d − ln μ_c,
  −∞ for the other kind of interval (a mark is never a space); the
  classes are combined in class order with logaddexp and then with the
  outlier: ln(e^x + e^y) = max(x, y) + log1p(exp(−|x − y|)), and x + ln 2
  where x = y (numpy's formula; both −∞ gives −∞).
- **Evaluation (Plan B task B2(a), exact steps).** Three restructurings
  that change no bit of any result (derived from IEEE arithmetic, and
  tested bit for bit against a frozen copy of the earlier code: below).
  (1) Only the classes of the observation's kind are evaluated (2 for a
  mark, 3 for a space): the other kind's −∞ terms entered the logaddexp
  chain only as logaddexp(x, −∞) = x + log1p(0) = x, and the refinement
  only as responsibilities exp(−∞ − ℓ_total) = 0, whose products add ±0 to
  sums that are never −0. (2) logaddexp returns max(x, y) without calling
  exp and log1p when |x − y| ≥ (58 − k) ln 2 nats, k the binary exponent of
  max(x, y) (2^k ≤ |max| < 2^(k+1), k ≥ −1000): then exp(−|x − y|) ≤
  2^(k−58), less than a sixteenth of half the spacing of the doubles next
  to max(x, y), so numpy's formula rounds to max(x, y) exactly (the proof,
  with the libm error allowance, is in `fit.cpp` at `lae`). With the
  outlier's −5.216 nats as the larger term (k = 2) the threshold is
  38.8 nats. (Since the near-exact step below, the fit's own evaluation no
  longer calls logaddexp; the function keeps the skip.) (3) The best-fit search evaluates the retained history's
  terms 3 times instead of 5 (with 2 refinement steps): the grid point's
  terms are the refinement's starting terms and the refined point's are
  its last, so they are reused for the acceptance test and the quality.
- **Evaluation (Plan B task B2(a), near-exact step).** ℓ_total is computed
  in one pass instead of the logaddexp chain:
  ℓ_total = m + ln(1 + Σ e^(x − m)), m the largest of the terms of the
  observation's kind and the outlier's −5.216 nats (the first largest; the
  outlier where a class only ties it), the sum over the other terms (one exp
  per term that is not the largest and one ln, instead of one exp and one
  log1p per term). The sum starts at the largest term's 1 and the others are
  added after it in class order, the outlier last. Terms more than 40 nats
  below m are left out, which changes no bit (derived): every partial sum is
  ≥ 1, where the doubles are at least 2^−52 apart, and a left-out term is
  below e^−40 (1 + 2^−52) < 4.3 · 10⁻¹⁸ < 2^−53, so adding it would round
  back to the same partial sum (tested bit for bit:
  `BankFitB2a.LogSumExpLeaveOutChangesNoBit`). The ln is skipped where the
  sum is exactly 1. In the grid's log-likelihood a class whose term cannot
  come within 41 nats of the outlier's is left out before its ln s_c² is
  computed: s_c² ≥ σ_ln², so
  ℓ_c ≤ ln((1 − ε) P_c) − ½ z²/s_c² − ½ ln σ_ln² − ln √(2π), a bound with no
  logarithm of the observation (1 nat of the 41 covers rounding); the result
  is the same bit for bit as with the term evaluated. The one-pass sum
  itself changes the last bits of ℓ_total; the measured differences from the
  logaddexp chain are stated under "Bit-for-bit check" below. The per-class
  ℓ_c, s_c² and the classifications do not change. A NaN term gives NaN,
  otherwise a +∞ term gives +∞ (as the chain); a class term is never +∞
  while σ_ln > 0.
- **Grid and memory.** T on a log grid from 1.2 s / 100 = 12 ms in steps
  of 1% (×1.01) up to the first point ≥ 1.2 s / 5 = 240 ms: 303 points,
  12 ms to 242.2 ms. q ∈ {3, 4, 5}; w/T ∈ {−0.4, 0, 0.4, 0.8};
  T_g/T ∈ {1, 1.59, 2.52, 4, 6.35}. Two tables hold the log-likelihood of
  every observation at every grid point: marks over (T, q, w), 303 × 3 × 4
  = 3636 points; spaces over (T, w, T_g), 303 × 4 × 5 = 6060 points. Each
  observation multiplies both tables by λ = e^(−1/N_mem) = 0.97938
  (N_mem = 48 marks and spaces) and adds its log-likelihood to its table,
  so the tables hold the exponentially weighted log-likelihood exactly
  (untruncated memory). The memory's weight is W ← λW + 1 elements. The
  last ⌈4 N_mem⌉ = 192 observations are retained for the refinement, its
  acceptance test and the quality, with weights λ^age (age 0 the newest);
  the tail left out weighs λ^192 = e^(−4) = 1.8% of the total (derived).
- **Grid maximum.** The score over (T, w) is the best q of the mark table
  plus the best T_g of the space table; with a periodicity prior T_P
  (weight π) it is lowered by π (ln T − ln T_P)² / (2 σ_P²), σ_P = 0.1 in
  ln T. The first maximum in (T, w) order is taken, then the first best q
  and T_g there: θ_grid = (T, (w/T)·T, q·T, (T_g/T)·T).
- **Refinement.** 2 Gauss–Newton steps in ln d from θ_grid, EM-style: the
  class responsibilities of the current point are held fixed per step.
  Residual ln d − ln μ_c, Jacobian DESIGN_c / μ_c (1/s), weights
  λ^age · responsibility / s_c²; the T_P prior as one more residual
  ln T_P − ln T with weight π / σ_P²; damping toward the current point
  with standard deviation 0.2 T per parameter (1/(0.2 T)² on the
  diagonal), which keeps unobserved classes where they are. The 4 × 4
  system is solved by LU with partial pivoting. After each step T is
  floored at 0.1 ms and the others clipped: w to [−0.6, 1.2] T, qT to
  [2, 6] T, T_g to [0.8, 10] T.
- **Acceptance and quality.** The refined point is kept only if its
  weighted log-likelihood on the retained history, Σ λ^age ℓ_total, plus
  the prior term, is at least the grid point's; otherwise θ_grid is kept.
  The quality Q = Σ λ^age ℓ_total / Σ λ^age, nats per element, at the
  kept point. `best` returns T, q, w, T_g, Q and W; nothing before the
  first observation.
- **Classification.** A mark is a dah if ℓ_dah > ℓ_dit under the fit; a
  space is the class among element, character and word with the largest
  ℓ (the first on a tie). `observations_loglik` is the mean of ℓ_total
  over a set of observations, nats per element.

**Port check.** Golden values (`engine/tests/data/bank/fit.json`, from
`kz4ap_proto.golden`): 300 observations (25 WPM text, a tune-up carrier of
2 s, a step to 15 WPM, then 30 WPM, each duration jittered by
exp(0.08 N(0, 1)), with their σ_t²) added one by one. After observations
1, 2, 8, 48, 100 and 300, without a prior and with T_P = 0.05 s at
weight 3: the grid indices and the classifications of 20 probe durations
(10 ms to 1 s) equal; θ_grid, `best`, the weighted log-likelihoods and
`observations_loglik` agree to a largest relative difference of
2.0 · 10⁻¹⁶; the full mark and space tables after 8, 48 and 300
observations were bit-identical until Plan B task B2(a). Since its
near-exact step (one-pass log-sum-exp, above) they agree to the test's
relative 10⁻⁹ as the other values: after 8, 48 and 300 observations 1980,
1702 and 1517 of the 9696 entries differ from the prototype's, by at most
7.1 · 10⁻¹⁵, 2.8 · 10⁻¹⁴ and 4.3 · 10⁻¹⁴ nats (relative 1.2 · 10⁻¹⁵,
4.6 · 10⁻¹⁴ and 1.2 · 10⁻¹⁵; measured on Windows), and the grid indices and
classifications are still equal. Both outcomes of the acceptance test occur
in the sequence. The prototype's fit tests are ported one for one
(inputs in `fit_cases.json`); its strict expected failure (Farnsworth
T_g with the E5 grids) is skipped with the same reason, and the port
reproduces its finding: T = 66.72 ms, T_g = 192.2 ms against 207.0 ms.
Where numpy's order of operations is not reproduced (the einsum sums of
the refinement's normal equations, LAPACK's solve, the `DESIGN @ θ`
matrix product, where BLAS may fuse 3·T_g − w into one rounding), values
may differ in the last bit. The prototype's squares `x ** 2` (in
`resolution_var_s2`: (L/a)² and r²; in the refinement: the prior's σ_ln²,
T² and (0.2 T)²) call libm's `pow`, which is not guaranteed correctly
rounded (glibc states a bound of about 0.52 units in the last place); the
port computes them as products x · x, so a rare value can differ in the
last bit.

**Bit-for-bit check of the restructured evaluation (Plan B task B2(a)).**
`fit_test.cpp` keeps a frozen copy of the fit's evaluation before B2(a)
(logaddexp, the per-observation terms, the grid's log-likelihood, the
refinement and the best-fit search) and compares the bit patterns of every
result: logaddexp on 1.2 · 10⁷ pairs placed at and beside the skip
threshold at every binary exponent, and on the special values (±0, ±∞,
NaN, subnormals); the grid log-likelihood at every grid point for 10⁶
random observations per configuration (durations from 10 µs to 100 s,
beyond the 1 ms and 10 s clamps and one step beside them; σ_t² of 0,
10⁻¹⁴ to 10⁻² s², and 1, 10¹⁰, 10³⁰⁰ s² and +∞), on the default grids and on
grids with medians that are not positive (w/T up to 1.2, T_g/T down to
0.2), and on the golden observations; `best`, `refine`, the weighted
log-likelihood and `class_logliks` on the golden sequence, the fit cases
and 2000 random fits per configuration and number of refinement steps
(0, 2, 5), without and with T_P priors. With the exact steps alone every
comparison was equal (commit `42201d7`; Windows and Linux, full sizes).
Since the near-exact step the tests keep two frozen variants. (1) The
frozen code with only the near-exact step applied (its ℓ_total by the
current one-pass sum, over all five classes): against it the grid's
log-likelihood (including the bounded skip of ln s_c²), `best` with its
quality, `refine`, the weighted log-likelihood and `class_logliks` (ℓ_c,
ℓ_total, s_c²) are compared **bit for bit**, so the exact steps keep a
bit-for-bit guard. (2) The frozen code with the logaddexp chain: against it
only the near-exact difference itself is measured, each ℓ_total (the grid's
and `class_logliks`') within the derived bound 30 · 2^−52 · (|ℓ_total| +
2 nats) (a count of at most 15 roundings per evaluation, each within one
unit in the last place, on quantities of magnitude ≤ |ℓ_total| + 2 nats),
and the weighted log-likelihood at a fixed θ within the bound derived from
those totals' own differences: Σ λ^age |Δℓ_total| plus each side's
rounding of numpy's pairwise sum, (⌈n/8⌉ + ⌈log₂ n⌉ + 4) · 2^−53 ·
Σ |λ^age ℓ_total|, and of the prior's addition. The one-pass sum's
leave-out of terms below 40 nats is tested bit for bit against the same sum
without it (`LogSumExpLeaveOutChangesNoBit`, 2 · 10⁶ cases, the outlier
last and largest included). Non-finite values are compared bit for bit
everywhere. At the full sizes no comparison fails, on both platforms; the
largest differences from the logaddexp chain (Windows / Linux):

| quantity | values | largest difference from the chain | of its bound |
|---|---|---|---|
| grid log-likelihood (ℓ_total at every grid point) | 8.5 · 10⁹ | 1.3 · 10⁻¹⁵ nats (relative 7.5 · 10⁻⁸, at a value near 0 nats) | 3.5% |
| ℓ_total of `class_logliks` | 1.8 · 10⁷ | 8.9 · 10⁻¹⁶ nats | 3.3% |
| weighted log-likelihood at a fixed θ (Σ over ≤ 192 observations, with the T_P prior term) | 4.4 · 10⁵ | 1.8 · 10⁻¹² nats (relative ≤ 1.6 · 10⁻¹³) | 99.1% |

(Windows; the Linux machine's figures are in the results record, section
3.8.) The weighted log-likelihood comes within 1% of its bound (99.6% on
the Linux machine) because the bound's first term, Σ λ^age |Δℓ_total|, is
the difference itself wherever one observation's difference dominates (a
short history, or one term much larger than the others): the bound is
derived and holds by construction, it is not fitted, and it is tight
there. Through the exact
steps (variant 1) `best`, `refine` and every other value are equal bit for
bit, so no acceptance test turns.

(The default test run uses 20 000 observations and 100 fits; the full sizes
run as the ctest entry `BankFitB2a.FullSweep`, label `full-sweep`, which the
default test presets exclude: `ctest --preset windows-full-sweep` or
`ctest --preset linux-full-sweep`, about 25 min on the Windows PC and 3 min
on the Linux machine.) The port's check against the plain formula
(`FastPathsAreBitIdenticalToThePlainFormulas`) now allows each grid value
the same derived bound 30 · 2^−52 · (|v| + 2 nats) (6.7 · 10⁻¹⁵ · (|v| + 2)
nats, against the 1.3 · 10⁻¹⁵ nats measured in the full sweep), and each
table entry an allowance accumulated as the table is: λ × its allowance +
the value's + 2^−51 |entry|; on its 414 observations 654 897 of 2 007 072 grid values differ (Windows), by at most 8.9 · 10⁻¹⁶ nats, and 1411 of the 9696 table entries, by at most 4.3 · 10⁻¹⁴ nats, each within its allowance.

**Cost (Plan B task B2(a), measured).** CPU per channel-second of the bank
decoder (`kz4ap-bank-replay`, development set, 525 oracle channels,
74 749.9 channel-seconds, Linux machine, 10 threads): 127.91 ms before
Plan B, 123.96 ms after B1, 104.60 ms after B2(a)'s exact steps,
75.84 ms after its near-exact step as first committed, and **66.67 ms**
with the sum started at the largest term's 1 (fix round 1, which also
saves the exp(0) of the largest term; −47.9% against 127.91 ms). On the
Windows PC (F-drift-s1, 8 channels, 240.1 channel-seconds, 8 threads, two
runs each, alternated in one session): 303.3 and 314.4 ms before, 166.0
and 170.5 ms after the exact steps, 76.5 and 78.4 ms after the near-exact
step as first committed; 67.0 and 68.6 ms with fix round 1 (a later
session, not alternated with the reference). A gprof profile of one channel
(F-drift-s1, label 1, 30 s; Linux, statically linked so that libm is
sampled) falls from 3.70 s to 1.98 s; the scalar libm functions from
2.67 s (log1p 1.13 s, exp 1.19 s, ln 0.30 s, pow 0.05 s) to 1.19 s
(log1p 0, exp 0.44 s, ln 0.73 s, pow 0.02 s). The decoded text and every decoded record are unchanged on all
525 channels (results record, section 3).

Status (as `training/kz4ap_proto/params.py` marks them): N_mem = 48
measured (E4: adopted over 24 and 12); the q, w/T and T_g/T grids measured
(E5, the "coarse" variant adopted); the T step of 1% a placeholder kept
by E5; σ_ln = 0.15 (marks) and 0.25 (spaces), ε = 0.05, the outlier range
1 ms–10 s, σ_P = 0.1 in ln T, 2 refinement steps, the damping 0.2 T and the
clipping bounds heuristic; the class priors, σ_t² and the 1.8% tail
derived; the 0.1 ms floor on T a numerical choice.

### Periodicity (bank decoder)

`engine/src/bank/periodicity.cpp`, a port of
`training/kz4ap_proto/periodicity.py` (`t_grid`, `_normalized_acf`,
`comb_estimate`, `Periodicity`), checked against the prototype's golden
values (below). It estimates the coarse speed T_P (the dit, s) from
branch 1's keying, independently of every branch's duration fit, for the
fit's prior and for selection's fallback. Only the comb on the
dit-plus-space period Π = 2T is ported: it is the prototype's default
and the owner's choice (E1). The prototype's other two methods (a
sign-weighted comb on the edges of p, and a fit to the nulls of p's
spectrum) were not adopted and are not ported; any other
`periodicity_method` is an error (`std::invalid_argument`).

- **Input.** Branch 1's squelched posterior p (probability of key-down,
  dimensionless; 0 where the squelch is closed), at the channel rate
  r samples/s, as the keying produces it block by block. It is averaged
  down by factor = max(1, round(r / 750 samples/s)) samples (ties to
  even): 2 at r = 1500 samples/s, so the averaged rate is
  r_P = r / factor = 750 samples/s (at 2000 samples/s factor 3,
  r_P = 666.7 samples/s). Each averaged sample is the mean of factor
  consecutive samples of p; a remainder shorter than factor waits for
  the next block. A buffer keeps the last N_max averaged samples, the
  longest window.
- **Windows and updates.** Windows W ∈ {2, 5, 10} s, of
  N_W = max(16, round(W · r_P)) averaged samples (1500, 3750 and 7500 at
  750 samples/s). The estimate is recomputed once round(0.25 s · r)
  samples of p (375 at 1500 samples/s, 0.25 s) have arrived since the
  last recomputation; between recomputations the last result stands. A
  window the buffer does not fill yet gives no estimate (score 0).
- **Candidates.** T on a log grid from 1.2 s / 100 = 12 ms in steps of
  1% (×1.01) up to the first point ≥ 1.2 s / 5 = 240 ms: 303 points,
  12 ms to 242.2 ms (the duration fit's grid; the 1.01 is written into
  `t_grid` itself, not read from `t_grid_step`).
- **Autocorrelation.** Over one window's N samples,
  x = p − mean(p) and the biased, normalized autocorrelation
  ρ[τ] = Σ_{m=0}^{N−1−τ} x[m] x[m+τ] / Σ_m x[m]², τ = 0 … N − 1
  (dimensionless; 1 at τ = 0), computed by FFT zero-padded to the
  smallest power of two ≥ 2N (so the circular correlation equals the
  linear one). No estimate when N < 16 or Σ x² ≤ 10⁻¹² · N (p does not
  vary; for instance all zero while the squelch is closed).
- **Comb.** For each candidate T, Π = 2T, in samples Π · r_P. A tooth at
  lag c is the mean of ρ over the lags ⌊c − 0.075 Π⌋ … ⌈c + 0.075 Π⌉
  (± 15% of T), clipped to 0 … N − 1. The contrast of tooth k is
  tooth(kΠ) − ½ (tooth((k − ½)Π) + tooth((k + ½)Π)), k = 1 … 4 (teeth at
  2T, 4T, 6T and 8T, negative teeth halfway between); the score is the
  mean of the 4 contrasts (dimensionless). Why 2T and not T: consecutive
  keying edges T apart have opposite signs, so p's structure repeats at
  2T; its autocorrelation is low at odd and high at even multiples of T,
  and a comb with teeth at multiples of T would peak at 2T (derived; the
  comb was corrected to Π = 2T on 2026-09-30, spec 4.4).
- **Reach.** A candidate counts only if its outermost lag,
  (4 + ½ + 0.075) Π = 9.15 T, is at most (N − 1)/2 samples, which caps
  T at (N − 1) / (18.3 r_P) ≈ W / 18.3: 109 ms (11 words/min) in the 2 s
  window, 273 ms (the whole grid) in 5 s and 546 ms in 10 s (derived from
  the rule; the rule itself is heuristic). A window whose true T is beyond
  its cap still returns its best candidate within reach; only the
  confidence threshold keeps that estimate out. The estimate is the
  candidate with the largest score (the first on a tie); none if no
  candidate is within reach.
- **Taper.** The biased estimate tapers each tooth by about (1 − τ/N), so
  a tooth's contrast shrinks with its lag: about 0.62 at the 8T tooth for
  5 words/min (τ = 1.92 s) in a 5 s window (derived). Keeping the taper is
  heuristic (the unbiased estimate is noisier at long lags); it lowers
  long-T scores in short windows relative to short-T ones.
- **T_P.** At each recomputation the windows are estimated shortest
  first; T_P is the estimate of the shortest window whose score is
  ≥ 0.03, the confidence is that score, and the window (s) is reported
  with it. If no window is confident, T_P is none and the confidence is
  max(0, every window's score): the largest score, or 0 if none is
  positive (a window without an estimate scores 0). T_P never
  feeds back into its own estimate (spec 4.4).

**Port check.** Golden values (`engine/tests/data/bank/periodicity.json`,
from `kz4ap_proto.golden`): the prototype's `ChannelDecoder.run` on 12 s
streams keyed at 12, 25 and 40 words/min (S₅₀₀ = 15 dB SNR in 500 Hz, through the
channel filter's shape), with its `Periodicity` recorded; the port is
given the same blocks of p and asked for an update after each, as `run`
does. At all 46 recomputations per stream the port recomputes on the
same blocks; T_P, the window and every window's T are equal (the 303
grid points are bit-identical); the confidences and the per-window
scores agree to a largest relative difference of 1.0 · 10⁻¹⁴ (692 of the
1005 compared values bit-identical). Not reproduced in numpy's order of
operations: Σ x² (numpy's `x @ x` is a BLAS dot product, whose order
depends on the BLAS build; the port sums the squares pairwise, as
`np.sum` does), numpy's FFT build and its complex product; the
contrasts subtract nearly equal tooth means, which turns last-bit
differences of ρ into larger relative differences of the score. The
prototype's comb tests are ported one for one (inputs in
`periodicity_cases.json`); its strict expected failure (Farnsworth
18/10 words/min, where the comb locks near the gap timebase) is skipped
with the same reason.

Status (as `training/kz4ap_proto/params.py` marks them): the method (the
comb on Π = 2T) owner (E1); the windows 2, 5 and 10 s a placeholder
(E2); the confidence threshold 0.03 a placeholder (E1); the 4 teeth and
their half-width 0.075 Π a placeholder (E3), not measured for the comb
on 2T; the update interval 0.25 s and the averaged rate 750 samples/s
heuristic; Π = 2T, the reach caps and the taper's size derived; the
reach rule and the biased estimate heuristic; the 1% grid step the
duration fit's (a placeholder kept by E5); the 10⁻¹² · N variance floor
and the 16-sample minimum numerical choices.

### Text model and branch selection (bank decoder)

`engine/src/bank/selection.cpp`, a port of `training/kz4ap_proto/text.py`
(`decode_pattern`, `TextModel`) and `select.py` (`BranchView`,
`Selector`), checked against the prototype's golden values (below).
Every branch decodes its own text; selection chooses the branch whose
text is published (spec 4.6).

- **Characters.** A character's dot/dash pattern becomes its symbol by
  the code table of `kz4ap::morse` (identical to `kz4ap_synth.morse`):
  eight or more dits read "<HH>", a pattern with no code "*".
- **Text model.** Unigram log-probability per symbol, nats, under
  VE3NEA's CW character frequencies (`kz4ap_synth.messages`, MIT; 41
  symbols, weights summing to 2688): ln(w / 2688) for a symbol in his
  table (E: ln(321/2688) = −2.125 nats); a valid code missing from it
  (prosigns other than <BT>, rarer punctuation) gets his rarest
  character's, ln(8/2688) = −5.817 nats (heuristic); "*" and anything
  else ln(10⁻⁶) = −13.816 nats (heuristic: "very unlikely"). A branch's
  text score is the mean over its characters, nats per character, word
  spaces left out (none without characters). The window,
  `text_window_chars` = 10 characters (the branch's most recent ones,
  word spaces not counted), is applied by the channel decoder when it
  builds the views ("Channel decoder" below); this module scores the
  characters it is given, newest first, as the prototype sums them.
- **Eligibility.** A branch is eligible when its own fitted dit T_k
  agrees with its length L_k: |ln(L_k / (0.8 T_k))| ≤ ln 1.1 = 0.0953
  (one ladder step), once its fit's memory weight is ≥ 8 elements and
  T_k > 0 s.
- **Best branch.** Among eligible branches: the best quality Q (nats per
  element) and every branch within ε_Q = 0.05 nats per element of it are
  tied; if more than one is tied and every tied branch has text, only
  those within 0.1 nats per character of the likeliest text stay tied
  (a branch with no text yet is not read as infinitely unlikely: the
  text step is then skipped); of the tied, the longest branch wins
  (better SNR). With no eligible branch (a fallback pick): the branch with
  the likeliest text if it leads the next likeliest by ≥ 1.0 nats per
  character; else, when T_P is confident, the branch whose length is
  nearest 0.8 T_P in ln L (the first on a tie); else branch 1, the
  shortest.
- **Switching.** The published branch changes only when the same other
  branch has been best for M = 4 selection instants in a row, all as the
  best eligible branch or all as fallback picks (a run of one kind does
  not complete a run of the other). One update may stand for several
  instants (the caller counts them); an update with no instant changes
  nothing, not even the eligibility times. For each branch the stream
  time (s) at which its current eligible run began is kept (none while it
  is not eligible); the channel decoder uses it to decide how far back a
  switch replaces published text.

**Port check.** Golden values (`engine/tests/data/bank/selection.json`):
the selector on a scripted sequence of 40 updates over the view sets of
the prototype's selection tests (eligible and ineligible fits, quality
and text ties, fallback picks by text and by T_P, 0 to 4 instants per
update): eligibility, the best branch, the returned branch, the pending
switch and its count equal at every update, the eligibility times
bit-identical; the text model's 12 patterns equal and its
log-probabilities of 30 symbols and 5 symbol lists bit-identical (the
mean uses Python's compensated float sum, as the prototype's `sum`
does). The prototype's text and selection tests are ported one for one.

Status (as `training/kz4ap_proto/params.py` marks them): M = 4 a
placeholder kept by E6; ε_Q = 0.05 nats per element and the text window
of 10 characters placeholders kept by E8; the eligibility tolerance
ln 1.1 (one ladder step), the minimum fit weight of 8 elements, the
text tie of 0.1 nats per character, the text separation of 1.0 nats per
character and the log-probabilities given to codes missing from the
table and to "*" heuristic; the character probabilities derived from
VE3NEA's table.

### Channel decoder (bank decoder)

`engine/src/bank/channel.cpp`, a port of `training/kz4ap_proto/channel.py`
(`Output`, `Branch`, `ChannelDecoder.run` as the streaming `BankChannel`),
checked against the prototype's full decoded output (below). It decodes
one station's baseband stream u (station at 0 Hz, FS, r samples/s): the
32 branches each key, time, fit and spell their own text, and selection
publishes one branch's characters, with corrections.

- **Blocks.** Everything advances once per block of
  B = max(1, round(block_s · r)) samples, block_s = 32/1500 s = 21.33 ms
  (B = 32 at 1500 samples/s; 43 at 2000 samples/s, 21.5 ms). Input may
  arrive in pieces of any length (`push`): a block is processed as soon
  as its last sample has arrived, and the remainder waits; at the end
  (`finish`) the remainder is processed as one shorter block, as the
  prototype's last block, and the result does not depend on how the
  stream was split (tested with pieces of 1, 47 and 1000 samples). As
  each sample arrives, every branch's power |v_k|² is computed from the
  running cumulative sum and stored rounded to float32, as a 4-byte float
  (section 8c, "The bank"; "Memory" below).
- **Order within a block** [n0, n1), t = n1 / r (s): (1) the noise
  estimate is updated and σ²_v,k read; (2) the keyer keys the block and
  updates the amplitudes ("Keying"); (3) branch 1's posterior p goes to
  the periodicity estimate, which may recompute; T_P, when confident,
  becomes the fit's prior (weight 1, else none); (4) per branch, in
  ladder order: its key changes become marks and spaces, a new over may
  start, or, while its amplitude is unknown, the over's start may be
  re-keyed or time out; (5) if branch 1 keyed up in the block, selection
  runs (each key-up of branch 1 is one selection instant); (6) the selected
  branch's new characters are published.
- **Marks, spaces and characters (per branch).** A key change at sample n
  is at time n / r − (N_k − 1)/(2r) s (the boxcar's group delay removed).
  A mark is key-down to key-up, a space key-up to key-down, each with the
  timing variance σ_t² of the branch at its amplitude a_k ("Duration fit").
  A duration > 0 s enters the branch's fit, which is then re-maximized
  (`best`, with the T_P prior), unless it was keyed by the unknown-amplitude
  test (provisional: lengthened by up to L_k, "Keying"); provisional
  durations enter no fit. Each mark is classified dit or dah, each space
  element space, character gap or word gap, by the branch's current fit
  (nothing is classified while the branch has no fit); a character gap
  ends the character (its symbol from its dot/dash pattern), a word gap
  also adds a word space " " at the character's end time.
- **New over.** After the key has been up longer than
  T_new = max(0.5 s, 12 · T_g) since the branch's last key-up (T_g of its
  current fit; without a fit T_g = 1.2 s / 5 = 240 ms, so T_new = 2.88 s;
  at 25 words/min, T_g = 48 ms, T_new = 0.576 s), the branch ends its
  character and word, keeps its fit as the previous over's (if it has
  observations and none is kept yet), starts a fresh fit, decodes with the
  previous fit's best until the re-key, and its amplitude becomes unknown
  (the keyer's unknown-amplitude test from the next block).
- **Re-key.** While a branch's amplitude is unknown, once it has keyed
  marks in this over and W_min = 0.8 s of keyed time, the stretch from
  where the amplitude became unknown (at most 20 s back) is keyed again
  with the full log-likelihood ratio at each candidate amplitude: the seed
  s² and, if there is one, the previous over's s² (FS²). For each, the
  stretch's marks and spaces go into a fresh fit and into a copy of the
  previous over's fit continued; the fresh fit is taken only if it wins
  (below), else the continued one decodes and the fresh one stays its
  rival. The candidate whose taken fit explains the stretch best (mean
  log-likelihood per element, nats; the first on a tie) wins: its fit and
  amplitude are kept, the stretch's characters are decoded again with it,
  and the keyer switches to the full LLR at that amplitude. The
  re-keyed characters replace the published ones from the stretch's start
  if the branch is the selected one (reason "rekey").
- **Re-key time-out.** If W_min is not reached within 2 s of channel time
  (counted from where the amplitude became unknown, or from the last
  time-out that keyed nothing), the stretch
  is re-keyed at the previous over's amplitude if there is one and it keys
  any sample (reason "timeout"); otherwise nothing in the stretch is
  keyed: its provisional characters are deleted (a correction if the
  branch is selected), the amplitude stays unknown and the 2 s count
  restarts. This is what removes false key-downs in noise after the last
  over.
- **Fresh fit against the previous.** The fresh fit replaces the
  previous over's continued fit only with at least 8 of this over's
  (re-keyed or later) marks and spaces, and only if its log-likelihood on
  them beats the continued fit's by more than ½ · k · ln n nats, k = 4
  (the fitted T, w, q, T_g), n the number of those marks and spaces
  (4.16 nats at n = 8, 4.97 at 12). After the re-key the rival fresh fit
  keeps learning from every later mark and space and is tested again at
  each; the competition ends when the rival has seen the fit memory's
  retained length, ⌈4 · 48⌉ = 192 marks and spaces (the previous over's
  memory then weighs e⁻⁴ = 1.8%).
- **Over starts.** An over start is recorded (s, on the selected branch's
  time base) once its re-key keyed at least one mark and the branch is
  the selected one; a silence followed only by noise records none.
- **Switching.** When selection switches to another branch, the new
  branch's characters replace the published ones from the start of its
  character that contains (or follows) the time at which its current
  eligible run began, on its own time base; a fallback pick (never
  eligible) replaces from the switch's own time.
- **Published text and corrections.** New characters of the selected
  branch are appended when they start after the last published one. A
  replacement from time f at time t cuts at c = max(f, t − 20 s) and works
  on overlap, not on start times alone: a published character is kept if
  it starts before t − 20 s or ends before c, the others are replaced; a
  new character is taken only if it starts at or after both c and the end
  of the last kept character (different branches time one character a
  few ms apart, and cutting on start times published one character
  twice). A correction is recorded only if the replaced text differs:
  (t, the first replaced character's start or c, the reach t − that
  start ≤ 20 s, old text, new text, reason), the number of characters
  kept (`from_index`, the prototype's len(kept)) and the first position
  whose character's text changed (`first_changed_index`). The engine's
  index-based correction starts at the smaller of the two ("The bank
  decoder behind the engine", "Events"): the kept characters are not
  always a prefix of the list. Nothing that starts more than 20 s before
  t is ever changed.
- **End of stream.** `finish` processes the last partial block, ends
  every branch's open character (no word space) and publishes the
  selected branch's new characters; a stream cut mid-character publishes
  the elements completed so far as a character (tested: C, −·−·, cut
  during its third element reads N, −·), and no correction refers past
  the end.
- **Memory.** The channel keeps a window of u and of |v_k|² (FS²) back to
  the earliest sample a later block can read: the re-key's 20 s plus the
  noise estimates' look-back (3 N_max, a segment with its mask's reach,
  the warm-up) and 2 blocks, 31 688 samples (21.1 s) at 1500 samples/s;
  its storage holds up to 2 s more (34 721 samples) and is moved forward
  when full (a bound chosen for the port, not a tuned value). The |v_k|²
  values are rounded to float32 when computed (the prototype's rounding,
  "Port check" below) and stored as 4-byte floats, which is lossless; every
  read converts them to double exactly (Plan B task B1). Memory per
  channel at 1500 samples/s, counted from the arrays the code allocates
  (derived, not measured): the |v_k|² window 32 × 34 721 × 4 B = 4.4 MB;
  the u window 34 721 × 16 B = 0.56 MB; the cumulative-sum ring
  277 × 16 B = 4.4 kB; per duration fit its two tables
  (3636 + 6060) × 8 B = 77.6 kB and its retained history (≤ 192 marks and
  spaces, 24 B each, 4.6 kB), 82.2 kB. Each branch holds one to three fits
  (the decoding fit, the previous over's, the rival), so the fits take
  32 × 82.2 kB = 2.6 MB to 96 × 82.2 kB = 7.9 MB, and a channel 7.6 MB to
  12.9 MB in all, plus small per-character and per-record lists. The fit's
  grid constants (ln μ, 1/μ², a validity flag per class and grid point:
  432.7 kB with the defaults) are immutable and held once per process per
  configuration: every fit built from a configuration with the same values
  of the fields they read (`min_wpm`, `max_wpm`, `t_grid_step`, the q, w
  and T_g grids, `outlier_prior`, `outlier_range_s`, `sigma_ln_mark`,
  `sigma_ln_space`, `fit_memory`, `prior_sigma_ln`, `refine_iterations`;
  compared bit for bit) shares one copy, which is freed when no fit uses
  it. Before Plan B task B1 the window took 8.9 MB (8-byte doubles) and
  every fit started afresh allocated its own grid constants (0.515 MB per
  fit), 26 MB to about 59 MB per channel (derived). **Measured** (Linux
  machine, `kz4ap-bank-replay` on G-ragchew-s1: 12 channels of 366.0 s,
  peak resident set size from `/usr/bin/time -v`, per channel in flight
  (RSS at 10 threads − RSS at 1 thread) / 9): 63.3 MB before B1, 32.1 MB
  after (−31.1 MB, against −18.3 MB to −46.0 MB derived). The measured
  figure includes what the replay tool holds per channel besides the
  decoder, chiefly the channel's recorded stream and its mixed copy
  (2 × 549 000 samples × 16 B = 17.6 MB, derived); less the 17.6 MB it is
  45.7 MB before (derived 26 MB to 59 MB) and 14.6 MB after (derived 7.6 MB
  to 12.9 MB), so after B1 the measurement lies **1.7 MB above** the
  derived upper end. The cause of that excess is **conjectured, not
  traced**: the channel's decoded record held until its test case is
  written and the allocator's overhead are not in the derived count;
  per-thread allocator arenas, or more than three fits per branch at some
  moment, are untested alternatives. The check is weak in both directions:
  the derived range before B1 is wide enough to contain its measurement,
  and the 17.6 MB subtracted is itself derived, not measured.
  Sample indices are 64-bit throughout (the noise estimates included:
  tested with indices past 2³¹, as after 16.6 days at 1500 samples/s).
- **Exact zeros.** On exact-zero input the noise estimate goes NaN (the
  prototype's behavior, kept until Plan B; "Noise"), so nothing is keyed
  and nothing is published: on a stream that starts with 1 s of exact
  zeros the prototype and the port both publish no character and no
  correction, and no NaN reaches a published time.

**Port check.** Golden values (`engine/tests/data/bank/channel.json`,
streams in `channel_stream_*.c64`, complex64 like the engine's own
samples; the prototype ran on the same float32-rounded streams, so the
comparison is exact in its input): the prototype's
`ChannelDecoder(ProtoConfig(), r).run(u)` on 15 streams (about 297 s of
channel time: a clean 25 words/min CQ, a same-speed turnover, noise after
the last over, a 15 → 30 words/min step, Farnsworth 18/10, a zero-padded
start, noise alone, a tune-up carrier, a 66 s stream at S₅₀₀ = 8 dB SNR in
500 Hz, a speed change across a turnover and the inputs of the
prototype's other channel tests; the clean stream also at 2000 samples/s
and cut mid-character) and its full result. The port, fed each stream in
one push, publishes the same text and characters (times to relative
10⁻⁹), the same corrections (old and new text, reason, times), over
starts, switches and selections (branch exactly, fitted T to relative
10⁻⁹), and the same periodicity records (T_P and windows exactly,
confidences and scores to relative 10⁻⁹) except per-window estimates
that are traced near-ties (D2) and allowed to differ: three
recomputations, all in the 2 s window over a buffer of p that is zero but
for one short squelch opening, far below the 0.03 confidence threshold;
T_P and everything downstream are unaffected. Three candidates tie in
each, T = 97.95 ms, 44.63 ms and 61.97 ms (the fourth best scores about
half as much). Their scores (dimensionless) on the prototype (numpy 2.5.3
on Windows, the golden values), the port's Windows build (MSVC) and the
port's Linux build (g++ 11.4, glibc 2.35), on the complex64 streams,
each side's pick in bold:

| Recomputation | T | Prototype | Port, Windows | Port, Linux |
|---|---|---|---|---|
| noise #14 | 97.95 ms | 9.5011315632179236 · 10⁻⁷ | 9.5011315631715739 · 10⁻⁷ | 9.5011315631715739 · 10⁻⁷ |
| noise #14 | 44.63 ms | 9.5011315632185335 · 10⁻⁷ | 9.5011315632185335 · 10⁻⁷ | **9.5011315633110972 · 10⁻⁷** |
| noise #14 | 61.97 ms | **9.5011315633108939 · 10⁻⁷** | **9.5011315633455884 · 10⁻⁷** | 9.5011315633108939 · 10⁻⁷ |
| noise #38 | 97.95 ms | 2.3515786746381741 · 10⁻⁶ | 2.3515786746336205 · 10⁻⁶ | 2.3515786746381470 · 10⁻⁶ |
| noise #38 | 44.63 ms | **2.3515786746568495 · 10⁻⁶** | **2.3515786746568495 · 10⁻⁶** | 2.3515786746383367 · 10⁻⁶ |
| noise #38 | 61.97 ms | 2.3515786746405865 · 10⁻⁶ | 2.3515786746405865 · 10⁻⁶ | **2.3515786746544642 · 10⁻⁶** |
| Farnsworth #64 | 97.95 ms | **4.7329276538316477 · 10⁻⁶** | 4.7329276538037837 · 10⁻⁶ | 4.7329276537942970 · 10⁻⁶ |
| Farnsworth #64 | 44.63 ms | 4.7329276538129181 · 10⁻⁶ | **4.7329276538129181 · 10⁻⁶** | 4.7329276537759468 · 10⁻⁶ |
| Farnsworth #64 | 61.97 ms | 4.7329276537666498 · 10⁻⁶ | 4.7329276537944054 · 10⁻⁶ | **4.7329276538082832 · 10⁻⁶** |

So the port's pick differs from the prototype's at Farnsworth #64 on both
builds, and at noise #14 and noise #38 on Linux only. The leads of the
picks over the runner-up, absolute (relative): prototype 9.2 · 10⁻¹⁸
(9.7 · 10⁻¹²), 1.6 · 10⁻¹⁷ (6.9 · 10⁻¹²) and 1.9 · 10⁻¹⁷ (4.0 · 10⁻¹²) at
noise #14, noise #38 and Farnsworth #64; port, Windows, 1.3 · 10⁻¹⁷,
1.6 · 10⁻¹⁷ and 9.1 · 10⁻¹⁸; port, Linux, 2.0 · 10⁻²⁰, 1.6 · 10⁻¹⁷ and
1.4 · 10⁻¹⁷. The spread of the three scores is at most 6.5 · 10⁻¹⁷
absolute on any side (prototype, Farnsworth #64; at noise #38 1.9, 2.3
and 1.6 · 10⁻¹⁷). One candidate's score differs between the prototype
and the port by up to 1.9 · 10⁻¹⁷ (noise #38, Linux, 7.9 · 10⁻¹²
relative) and 4.2 · 10⁻¹⁷ (Farnsworth, Linux, 8.8 · 10⁻¹² relative)
absolute. (Noise #38 was found by the Linux build's first run on the
complex64 streams; on the earlier float64 streams the Linux build had
matched the prototype there.) A score is a difference of comb-tooth means of
the normalized autocorrelation (values up to 1, taken from a cumulative
sum), so last-bit differences of the means, of order 10⁻¹⁷, survive the
cancellation down to a score of order 10⁻⁶ as relative differences of
order 10⁻¹² to 10⁻¹¹ (derived, order of magnitude). That is rounding, and
it is at least as large as every lead, so the pick depends on the build (a
test recomputes the port's three scores at these recomputations and
requires the port's pick to be its own largest). The
prototype's channel tests are ported one for one; its strict expected
failures are skipped with their reasons (and fail if they pass), and its
`keep_p1` posterior for the offline experiments is not ported.

Status (as `training/kz4ap_proto/params.py` marks them): block_s
heuristic (the engine's channel block); T_new's 0.5 s and 12 T_g
placeholders kept by E7; W_min measured (E9b); the 2 s re-key time-out
and the fresh fit's 8 marks and spaces placeholders (heuristic); the
½ k ln n penalty's form derived (BIC, n independent observations), its
use with k counting only the fresh fit's parameters heuristic; the end
of the competition at 4 N_mem derived from the memory's weight; the 20 s
correction reach an owner decision; the overlap cut and the choice of
the re-key's candidate amplitudes heuristic; the window kept in memory a
bound of the port, not a parameter.

### The bank decoder behind the engine

`engine/src/bank_decoder.cpp` (`BankDecoder`), one `BankChannel` per
channel, selected by `FrontEnd::Bank` (`kz4ap-bench --decoder bank`;
`--front-end` is the option's old name, kept as an alias). The bank is a
decoder like Envelope and Matched: the detector and the channelizer in
front of it are the Matched path's, unchanged.

- **Channels.** The engine opens and closes channels exactly as on the
  Matched path: the detector with distance attribution (each track follows
  its own peak within D_ch = 47 Hz, section 6), the channelizer's ±150 Hz
  channels at r = 1500 samples/s (section 7). Before every channel block
  (32 samples, 21.33 ms at 1500 samples/s) the engine gives the decoder
  its anchor Δ = f_det − f_c, Hz: the detector's current frequency for the
  track minus the channel's center (its FFT bin's center). In oracle mode
  f_det is the label's frequency, fixed for the whole recording: the
  label's drift is not followed.
- **Anchor mixing (order of operations).** Each block y (channelizer
  output, FS, single precision, widened to double) is mixed down by the
  anchor in force for it: u[n] = y[n] · exp(−jφ[n]),
  φ[n] = 2π · (S[n] − Δ[n]) / r rad, S[n] = Σ_{m ≤ n} Δ[m] (Hz, summed in
  order from the channel's first sample, in double precision), so φ[0] = 0
  and the phase advances 2π Δ / r per sample, continuous across blocks and
  across anchor changes. This is the prototype's
  `streams.anchored_baseband` with the same order of operations (the
  cumulative sum, minus the sample's own Δ, times 2π, divided by r;
  cos(−φ) and sin(−φ); the complex product written out as numpy computes
  it), so the engine and the prototype run on recorded detector channels
  see the same u up to libm's last-bit rounding of cos and sin. The bank
  has no frequency tracker: the residual (the station's frequency minus
  f_det) stays in u, where the branch boxcars attenuate it by
  |H(f)|² = (sin(π f N_k / r) / (N_k sin(π f / r)))². For a boxcar of
  58 samples (38.7 ms, 0.8 dit at 25 words/min; the ladder's neighbors are
  36.4 and 40.1 ms) that is −0.54 dB relative to 0 Hz at a 5 Hz residual,
  −2.25 dB at 10 Hz, −3.33 dB at 12 Hz and −11.4 dB at 20 Hz, with the first
  null at r / N = 25.9 Hz (derived from the formula; the decoding effect
  is not measured here).
- **Blocks.** The decoder pushes a call's samples to the bank one bank
  block at a time (B = round(block_s · r) = 32 samples at 1500 samples/s,
  so the engine's 32-sample channel blocks are normally one bank block
  each; a call with more samples is cut at the bank's block boundaries).
  A bank block's last step publishes the selected branch's new
  characters, so after each block the characters it appended are the
  last ones of the bank's list (`Output::appended()` counts them).
- **Events.** Each call returns a `DecodeUpdate` (and the engine a
  `DecodedTextEvent`, published when it has characters or corrections):
  `chars`, the characters appended since the last call, in order; and
  `corrections`, one `TextCorrection` per correction the bank recorded
  since then, in order: `from_index` (the smaller of the number of the
  channel's characters the bank kept and the first position whose
  character's text the correction changed, clipped to the list's current
  length; see "The consumer's rule"), `chars` (the bank's characters from
  `from_index` on, as they stand at the end of the call, kept ones
  included), `t_s` (when it was made, s), `reason` ("switch", "rekey" or
  "timeout") and `reach_s` (the bank's reach: `t_s` minus the start of the
  first replaced character, s, at most 20 s). A "resync" correction (see
  "Overlapping characters" below) is the exception: its `t_s` is the end
  of the call's processed blocks, and its `reach_s` is `t_s` minus the
  start of the first character at which the consumer's list differs from
  the bank's (0 when only the list's length differs). That reach is not
  bounded by the 20 s correction reach: the character can be a kept one
  that a same-text replacement moved, and a kept character is one that
  starts more than 20 s before the replacement or ends before its cut, so
  nothing in the code bounds how far back it starts. All times are stream times: the bank's times (counted
  from the channel's first sample, the branch's group delay removed) plus
  the first block's start time, s. The event's frequency is the anchor
  (center plus Δ: the detector's frequency, or the label in oracle mode).
  The bank publishes no speed or confidence (both 0 in its events) and no
  per-character probability (1). Envelope's and Matched's updates always
  carry no corrections, and their events are otherwise unchanged.
- **The consumer's rule.** A consumer keeps one character list per
  channel: it appends an event's `chars`, then applies its corrections in
  order, each keeping the first min(`from_index`, length) characters and
  appending its `chars`. The list's text then equals the bank's after
  every call (derived; tested with calls of 1, 32, 47 and 1000 samples on
  golden streams with corrections, after every call on a recorded channel
  whose characters overlap in time, and with a forced resync). The bank's
  list changes only by appends and by replacements, and a replacement that
  changes the list's text either records a correction or, when the
  replaced and the new text are equal but overlapping characters change
  places, does not. *Changes with a correction:* let g be the smallest
  `from_index` of the call's corrections; no character's text below g
  changed during the call (`from_index` is never past the first position
  whose text changed), and any position between the consumer's previous
  length and g was filled by an append, in order, so after the appends the
  consumer's first g characters are the bank's; the correction at g
  replaces everything after them by the bank's tail, the ones before it
  only touched positions at or after their own index (≥ g), and the ones
  after it (index ≥ g) put back the bank's tail again. *Changes without a
  correction:* the `BankDecoder` applies every update to a copy of the
  consumer's list exactly as the rule above does, and at the end of the
  call compares it with the bank's list from the lowest position that any
  of the call's text changes touched; if they differ there or later (or
  in length), it sends a "resync" correction from the first differing
  position with the bank's characters from there on, after which the copy,
  and so the consumer's list, is the bank's. Below that position no
  replacement of the call changed a text, so the argument of the first
  case holds there. The *final text* is that list's text, every
  correction applied; the *immediate text* is every appended character in
  order, corrections ignored (what a reader would have seen live). A
  replacement whose text is unchanged is not a correction (the prototype
  records none) but can re-time characters; the consumer keeps the times
  first published, so its texts are exact and its character times can
  differ from the bank's final ones by that re-timing (up to 5.0 ms on the
  speed-turnover golden stream, measured).
  **Overlapping characters (a defect, fixed in milestone 2c Task 10).**
  `Output::replace_from` keeps every character that starts more than 20 s
  before the correction or ends before the cut, and the prototype's
  `from_index` is the number kept. When two characters overlap in time
  (copies of one character timed a few ms apart by two branches), a kept
  one can follow a replaced one in the list, so the kept characters are
  not a prefix, and a consumer keeping the first `from_index` would keep
  the replaced character and drop the kept one. Found by the first
  full-suite run, where it left the final text of 6 oracle labels (4 scored
  signals and 2 unscored interferers) one or two characters off the bank's,
  with no edit count changed. It was traced by driving the `BankDecoder` on
  the recorded channels (results record
  `docs/plans/2026-10-03-milestone-2c-bank-results.md`, section 4.4). The fix:
  `replace_from` also records `first_changed_index`, the first position
  whose character's text differs between the list before and after, and
  the event's `from_index` is min(`from_index`, `first_changed_index`),
  its `chars` the bank's characters from there on, kept ones included.
  `replace_from` also records every change of the list's text in
  `text_changes()`, with a correction or without one (a same-text
  replacement that reorders overlapping characters). The `BankDecoder`
  keeps the consumer's list as its updates build it, checks it against
  the bank's from the lowest new change, and if they differ sends a
  correction with reason "resync" (counted by the bench; its reach is not
  bounded by the 20 s correction reach, "Events" above). Measured: never
  on the suite (0 of 31 205 corrections on the second full-suite run).
  The resync path is tested by forcing one: a test-only seam
  (`BankDecoderTestAccess`, a friend of `BankDecoder` and `BankChannel`
  that adds no code to the engine) makes a same-text replacement in the
  bank's list ("E" from 2.0 to 5.0 s and "T" from 3.0 to 3.5 s become "T",
  "E" with no correction), and the test checks that exactly one resync
  follows and leaves the consumer's list equal to the bank's. The
  prototype's `from_index` and every other recorded value are unchanged.
  On the second full-suite run the 6 labels have the bank's, and the
  prototype's, text.
- **The bench.** `kz4ap-bench` assembles each track's final and immediate
  text from the events (`TrackText`), writes both per track (`text`, the
  final text as before, and `text_immediate`), and scores both: `cer`
  (and every other rate, and the baseline check) on the final text, and
  `cer_immediate` (with `decoded_immediate`, `edits_immediate` and
  `cer_immediate` per signal) on the immediate text. The JSON keeps the
  key `front_end` (stage 1's tooling reads it) and adds `decoder`, with the
  same value (`envelope`, `matched` or `bank`). For Envelope and Matched
  the two texts are equal and the rest of the output unchanged (checked
  against the previous build on the smoke recording, first-sample-s1 and
  F-drift-s1, through the detector path and with oracle channels: identical
  JSON apart from the new keys). For the bank it also writes, per track,
  `corrections`: every correction the events carried, with `t_s` (s,
  stream time), `reach_s` (s), `reason`, and `removed` and `inserted`
  (characters): the final text's characters from the correction's index
  on against its new ones, less what the two share at their start (an
  index before the first change) and at their end (characters re-sent
  unchanged), so the smallest contiguous block that differs; an upper
  bound of the correction's edit distance (derived), and removed plus
  inserted, summed over a track, is at least the net Levenshtein distance
  from the immediate to the final text (derived: an append adds the same
  character to both texts, and each correction changes the final text by
  at most its removed plus inserted). `kz4ap_synth.suites
  summarize` counts them per group (section "Corrections" of the summary:
  corrections per channel-minute of the group's engine runs, by reason,
  the reach's median, 99th percentile and maximum, numpy's linear
  percentile, and the characters removed and inserted per channel-minute,
  shown as "—" for results written before these counts; each engine run
  once, a detector-path recording's station-label result being the same
  run). The per-correction counts are not yet measured on the suite: the
  first full run after this change measures them.
- **End to end (measured, development set seed 1).** first-sample-s1 with
  oracle channels: the engine's final texts equal the replay tool's on all
  4 channels (CER 0.0000 on the final text, 0.0743 on the immediate text:
  corrections restore the first characters of three channels). Through the
  detector path, its 4 tracks' final texts equal the prototype's
  (`kz4ap_proto.runner decode`) on the channels recorded through the
  Matched path's detector (CER 0.1014, immediate 0.2027). F-drift-s1 with
  oracle channels: equal for the 4 labels drifting 0.2 and 0.5 Hz/s,
  different for the 4 drifting 1 and 2 Hz/s, because the engine anchors
  the bank at the label's starting frequency while the replay tool (and
  stage 1) mixes by the label's drifting phase law (the label's frequency
  is the carrier's at the label's start); the residual grows by 1 or
  2 Hz per second of the signal and passes the 25.9 Hz null of a
  25 words/min branch after 25.9 s or 12.9 s (derived). The suite
  summary therefore marks a bank row of an oracle recording "not
  meaningful (oracle anchor)" as it does a Matched one, when some label's
  sound gets more than 12 Hz from its labeled frequency (for the bank a
  heuristic limit: a 58-sample branch, 0.8 dit at 25 words/min, is
  −3.33 dB relative to 0 Hz at 12 Hz).
  Cost: 0.40 s of CPU per channel-second on first-sample-s1 (4 channels,
  0.6× real time; the exact-math build, the development desktop).

Status: the anchor mixing is the prototype's (`anchored_baseband`,
derived: the detector's frequency is where option 1 says the station is);
the event format, the consumer's rule and the immediate text are this
port's interface choices (owner decision D1 for `--decoder`); probability
1, speed 0 and confidence 0 are placeholders for values the bank does not
produce.

### Measured: the bank against Matched and Envelope (milestone 2c, Plan A)

Full suite, 3 seeds (`kz4ap_synth.suites`: 120 oracle test cases, the 27
oracle copies included, and 39 test cases scored through the detector
path; 3 579 labels, of which 3 531 scored signals, the other 48 being
group E's interferers; 457 168.6 channel-seconds), `kz4ap-bench --decoder
bank` on the Linux machine (Ubuntu 22.04, 10-core Intel Xeon (Ice Lake),
g++ 11.4, exact-math build), 10 bench processes at once, 110 min of wall
time. This is the second run, with the correction fix (`1b2f4ef`). Envelope
and Matched are stage 1's result files, checked identical on 54 runs with
the bench. Source: results record
`docs/plans/2026-10-03-milestone-2c-bank-results.md`, section 4 (raw
summaries git-ignored under `build/suite/full3/experiments/linux/`). CER is
a fraction (edits per reference symbol); parentheses are bootstrap 95%
intervals over signals; "paired" is the mean per-signal difference. No
parameter was changed for this run.

| part | bank CER | Matched CER | Envelope CER | bank − Matched, paired | bank − Envelope, paired |
|---|---|---|---|---|---|
| oracle test cases (2 871 signals) | 0.369 (0.352–0.385) | 0.457 (0.442–0.473) | 0.625 (0.599–0.655) | −0.076 (−0.086 to −0.066) | −0.252 (−0.272 to −0.232) |
| through the detector path (660 signals) | 0.448 (0.409–0.490) | 0.467 (0.426–0.505) | 0.482 (0.442–0.525) | +0.009 (−0.007 to +0.026) | −0.063 (−0.088 to −0.039) |

- **Against the prototype.** Every per-regime paired comparison (140
  regimes against Matched and against Envelope) has the prototype's values
  from the stage-1 results record, section 4.3.1, except the four group F
  drift rows (272 of 280 pairs identical). Signal by signal, the decoded
  text equals the prototype's on all 2 895 non-drifting oracle labels
  (2 847 scored, 48 unscored) and all 660 through the detector path. The 15
  drifting labels that differ are the anchor without the drift: the
  prototype mixed as the engine mixes reproduces all 15. They are not
  comparable until a frequency tracker is in the loop, and the suite marks
  every drifting oracle row of the bank "not meaningful (oracle anchor)".
  The first run, before the fix, also differed on 6 other labels (4 scored,
  2 unscored, no edit count changed); that was the correction-event defect
  under "The consumer's rule" above.
- **Every regime** (132 rows; the 8 rows the suite marks "not meaningful
  (oracle anchor)" for the bank are left out): against Matched 50 better,
  26 worse, 56 unchanged; against Envelope 84 better, 16 worse, 32
  unchanged ("better" or "worse": the paired interval excludes 0, a
  heuristic convention). Worse than
  Matched: strong neighbors at +10 and +20 dB relative to the wanted
  station's key-down power (group E), crowded channels 0 to 100 Hz apart,
  10 WPM and the 30 → 15 WPM ramp, same-track QSOs scored per station,
  12 WPM in white noise, fast-fading hand keying, tune-up 0.6 s through the
  detector path, and clean machine, computer and paddle keying (+0.002 to
  +0.014 CER). Group A, S₅₀₀ (key-down carrier power over noise power in
  500 Hz) at CER 0.10: −0.1, −0.0 and 1.8 dB of S₅₀₀ at 12, 25 and 40 WPM
  (Matched −0.2, 1.1, 2.9 dB of S₅₀₀; Envelope 7.2, 5.1, 6.0 dB of S₅₀₀).
- **Displayed text.** CER of the text as first published (corrections
  ignored) 0.411, of the final text 0.382, over all 3 531 signals; paired
  immediate minus final +0.027 (+0.023 to +0.032), positive in every group
  except tune-up with oracle channels (−0.004, interval containing 0). For
  Matched and Envelope the two texts are equal.
- **Corrections** (from the engine's text events, counted by the bench and
  `suites summarize`; each engine run once). In all, 4.095 per
  channel-minute (31 205 in 7 619.5 channel-minutes: switch 13 371, re-key
  9 521, time-out 8 313, resync 0). Reach, from the correction's time back to
  the start of the first character it replaced: median 1.544 s, 99th
  percentile 19.861 s, maximum 20.000 s. Per group from 0.579 per
  channel-minute (Farnsworth) to 8.394 (interference). The counts equal the
  prototype's in every group but F tuning (227 against 180, the drifting
  labels). The corrections change a net 11.4 characters per channel-minute:
  the Levenshtein distance from immediate to final text, a lower bound on
  the characters replaced, 86 906 characters on 3 177 of 3 556 tracks.
- **Cost.** 133.8 ms of CPU per channel-second through the engine
  (130.4 ms on oracle test cases, 165.6 ms through the detector path). Nearly
  all of it is in the decoder: the decoders' share, a steady-clock time
  that is not strictly nested in the process CPU time, is within 0.9 ms of
  it. Per group it ranges from 54.2 ms (Farnsworth) to 259.4 ms
  (interference). On the development set it is 130.4 ms, against the replay
  tool's 128.2 ms (Task 9). On the same 26 engine runs (148.1 ms for the
  bank) Matched costs 0.610 ms and Envelope 0.344 ms, so the bank costs
  about 240 and 430 times as much. One core decodes about 7.5 bank channels
  in real time (derived).
- **Detection** (behind the Matched path's live detector, as the bench
  counts it): recall equal for all three decoders in every group; false
  tracks equal to Matched's except the strong group (bank 1, Matched and
  Envelope 12 each); tracks per QSO equal to Matched's in every group-H tag;
  every count equal to the prototype's in stage 1.
- **Status.** Measured. The F drift rows are not comparable with the
  prototype (anchor without drift). The intervals treat signals as
  independent, although signals of one recording share its noise; they are
  likely too narrow where a regime has few recordings (stage-1 results
  record, section 4.3).

## 9. Timing and latency

| Stage | Delay |
|---|---|
| Hop (processing block) | 21.3 ms |
| Detection | 1 s warm-up at recording start; then ~0.5–1.5 s for a new station (averaging + 0.5 s persistence) |
| Channel filter group delay | 10.7 ms |
| Decoder smoothing | ~¼ dit (τ_s) |
| Decoder warm-up (Matched only) | the first 0.32 s of each channel key nothing (section 8b, "Warm-up"); a mark in progress when it ends is decoded but not counted for speed (section 8, "Marks whose start was not observed") |
| Matched filter group delay (Matched only) | (K − 1)/2 samples: 19 ms at 25 WPM |
| Character emitted | after a 2-dit gap follows it (Envelope and Matched) |
| Bank block (bank only) | 21.3 ms: B = round(block_s · r) = 32 samples at 1500 samples/s; characters and corrections are published at the end of the block that makes them (section 8c, "Blocks") |
| Bank branch group delay (bank only) | (N_k − 1)/(2r): 4.3 ms (k = 1, N_k = 14) to 91.7 ms (k = 32, N_k = 276) at 1500 samples/s (derived); removed from the published character times, but branch k sees a key change that much later |
| Bank character published (bank only) | when the selected branch classifies the space after it as a character or word gap, at the next key-down; at a new over (key up for T_new = max(0.5 s, 12 · T_g)) or at the end of the stream (section 8c, "Marks, spaces and characters") |
| Bank corrections (bank only) | replace published text up to 20 s back (the correction reach); a "resync" is not bounded by it (section 8c, "Events") |
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
| Decoder | Matched (default), Envelope or the bank | `ClassicalDecoderConfig::front_end` (`FrontEnd::Bank` makes the engine create a `BankDecoder`); `kz4ap-bench --decoder` (`--front-end` its old alias) | Matched default and Envelope owner decisions 2026-09-29; the bank selectable (owner decision D1, 2026-10-03) |
| LLR keying hysteresis (Matched) | g > +1 nat down, g < −1 nat up; the key-up threshold, −1 nat, is also the evidence that a mark's start was observed (row "Marks not counted for speed", Task 16) | `ClassicalDecoderConfig::llr_hysteresis` | heuristic |
| Filter follows speed after (Matched) | 8 marks in the speed window (8 new ones after a re-acquisition) | `ClassicalDecoderConfig::follow_after_marks` | heuristic |
| Dit-estimate growth bound (Matched) | decided: at most ×1.25 per mark while the filter follows the speed, and the filter's own dit at most ×1.25 per mark from its first follow step (from the 20 ms acquisition dit). Applied at each key-up counted for speed; a dropout merge restores the state before the merged mark's update, so each physical mark is bounded once (Task 15) | `ClassicalDecoderConfig::max_dit_growth` | heuristic value (owner decisions 2026-09-29); applied per physical mark (derived from the code, Task 15; re-measured in section 8b, "Measured: Matched against Envelope" and "Start-up runaway", Task 17) |
| Marks not counted for speed (Matched) | a mark whose key-down came with no keyable key-up sample (log-odds below −1 nat) seen since the last sample on which keying was impossible (warm-up, squelch closed) | `ClassicalDecoder::step_matched`, `key_up_seen_` | derived rule, no new parameter: its threshold is the key-up hysteresis, −1 nat (Task 16; measured in section 8b, Task 17) |
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
| Bank ladder (decoder: bank) | L_k = 9.6 ms × 1.1^(k−1), k = 1…32 (9.6 to 184.3 ms): 0.8 dit at 100 to 5 words/min; ratio 1.1 | `BankConfig::min_wpm`, `max_wpm`, `ladder_step`, `length_dits` | WPM range and ratio owner; length 0.8 dit heuristic |
| Bank branch filter | boxcar, N_k = round(L_k · r) samples (14 to 276 at r = 1500 samples/s), zeros before the stream | `bank::branch_samples`, `bank::boxcar` | derived from the ladder |
| Bank envelope likelihood | Λ = −a²/2 + ln I₀(a·x) nats; p = logistic(g), g clipped to ±50 nats | `kz4ap::envelope_llr` (shared with Matched), `bank::logistic` | derived; clip a numerical choice; the logistic checked against the prototype on nine log-odds |
| Bank noise method | "spectrum": branch 1's three-tap level × spectrum ratios (variant (a)); "spectrum-level" (variant (b)) and "branch" (per-branch three-tap fallback) selectable | `BankConfig::noise_method`, `bank::make_noise` | measured (prototype E10) |
| Bank three-tap noise level | κ = 1.75, κ_n = 4, τ_n = 2 s, truncation mean m(κ) = 0.632 divided out; warm-up 0.32 s, 20% quantile (numpy "linear") / (2·(−ln 0.8)); floor 10⁻²⁰ FS² (−200 dBFS) | `BankConfig::noise_guard`, `neighbor_guard`, `noise_tau_s`, `noise_warmup_s`; `bank::ThreeTapNoise` | heuristic (Matched's, milestone 2); m(κ) and the warm-up scale derived; floor a numerical choice |
| Bank noise spectrum | segments T_seg = 256/1500 s = 170.7 ms (M = 256 samples, bins 5.86 Hz at 1500 samples/s), periodic Hann; exponential average τ_n = 2 s (β = 0.0818 per segment); smoothed ±25 Hz (±4 bins); W_k from 16 points per bin | `BankConfig::segment_s`, `spectrum_smoothing_hz`; `bank::SpectrumNoise` | heuristic; 16 points a numerical choice |
| Bank spectrum mask | a sample is left out if \|v_1\|² ≥ κ_n·2σ²_v,1 anywhere from 20 ms before it to (N_1 − 1)/r + 20 ms after it; a segment enters if ≥ 50% is left in | `BankConfig::guard_margin_s`, `min_clean_fraction` | heuristic |
| Bank mask bias b_mask,k | 0.8370 (k = 1) … 0.7852 (k = 32), dimensionless; divides each branch's spectrum reading | `BankConfig::mask_bias` | measured (white noise, seeds 101–110; valid only for the defaults at 1500 samples/s) |
| Bank keying log-odds and hysteresis | g = Λ + ln(P₁/(1 − P₁)) nats, P₁ = 0.44 (−0.2412 nats); down at g > +1 nat, up at g < −1 nat | `BankConfig::prior_key_down`, `hysteresis_nats`; `bank::BankKeyer::step` | P₁ derived (PARIS: key-down 22 of 50 dit units); h heuristic |
| Bank squelch | a_k ≥ a_min,k = 3·(L_k/16 ms)^(1/4) (2.622 at k = 1 to 5.525 at k = 32), dimensionless | `BankConfig::squelch_a`, `squelch_ref_s`, `squelch_exponent` | 3 heuristic; L^(1/4) scaling derived |
| Bank amplitude EM | τ_a = 0.5 s of key-down weight (α = 1.332·10⁻³ per sample at 1500 samples/s), p-weighted, one step per block | `BankConfig::amplitude_tau_s`; `bank::BankKeyer` | heuristic (heuristic running form of an EM update, as Matched) |
| Bank unknown-amplitude test | key down at x > x_on,k (4.6428 at k = 1 … 4.2036 at k = 32, dimensionless), up at x < x_off = √(−2 ln 0.3) = 1.552; target R_fa = 0.01 false marks/s per branch | `BankConfig::x_on_values`, `release_probability`, `false_marks_per_s` | x_on measured (E9a); release probability heuristic; R_fa heuristic target kept by E9b |
| Bank amplitude seed and re-key | while unknown: ŝ² = 90% quantile of the last ≤ 3.2 s (4 × W_min) of keyed \|v\|² − 2σ², FS²; ready to re-key after W_min = 0.8 s of keyed time | `BankConfig::rekey_after_s`, `seed_memory_rekeys`; `bank::BankKeyer`, `bank::rekey` | W_min measured (E9b); memory and 90% quantile heuristic |
| Bank duration-fit classes | dit T + w, dah qT + w; element space T − w, character gap 3 T_g − w, word gap 7 T_g − w (s); log-normal in ln d, s_c² = σ_ln² + σ_t²/μ_c², σ_ln = 0.15 (marks), 0.25 (spaces); priors 0.5716 / 0.4284 (marks), 0.6467 / 0.2379 / 0.1154 (spaces) | `BankConfig::sigma_ln_mark`, `sigma_ln_space`; `bank::class_priors` | σ_ln heuristic; priors derived (VE3NEA tables) |
| Bank timing resolution | σ_t² = 2 (L_k / max(a, 1))² + 2 / (12 r²), s² | `bank::resolution_var_s2` | derived (first order, high SNR) |
| Bank outlier class | ε = 0.05, log-uniform on 1 ms–10 s (−5.216 nats in ln d); durations clamped to that range | `BankConfig::outlier_prior`, `outlier_range_s` | heuristic |
| Bank fit memory | N_mem = 48 marks and spaces, λ = e^(−1/48) = 0.97938; tables untruncated; refinement and quality on the last ⌈4 N_mem⌉ = 192 (tail e^(−4) = 1.8%) | `BankConfig::fit_memory`; `bank::DurationFit` | N_mem measured (E4); the 1.8% tail derived |
| Bank fit grid | T 12 ms to 242.2 ms in 1% steps (303 points); q ∈ {3, 4, 5}; w/T ∈ {−0.4, 0, 0.4, 0.8}; T_g/T ∈ {1, 1.59, 2.52, 4, 6.35} | `BankConfig::t_grid_step`, `q_grid`, `w_grid`, `tg_grid` | q, w, T_g grids measured (E5, "coarse"); T step placeholder kept by E5 |
| Bank T_P prior | −π (ln T − ln T_P)² / (2 σ_P²) nats, σ_P = 0.1 in ln T | `BankConfig::prior_sigma_ln` | heuristic |
| Bank fit refinement | 2 Gauss–Newton steps in ln d, damping 0.2 T per parameter; T ≥ 0.1 ms, w ∈ [−0.6, 1.2] T, qT ∈ [2, 6] T, T_g ∈ [0.8, 10] T; kept only if the weighted log-likelihood does not drop | `BankConfig::refine_iterations`; `bank::DurationFit::refine`, `best` | heuristic; the 0.1 ms floor a numerical choice |
| Bank fit grid constants | ln μ, 1/μ² and a validity flag per class and grid point (432.7 kB with the defaults), built once per process per configuration and shared, immutable, by every fit of a configuration with bit-identical values of the 13 fields they read; freed when no fit uses them | `bank::DurationFit::shared_model` | a memory choice with no arithmetic effect (Plan B B1: tested bit for bit across threads; development-set texts identical) |
| Bank fit evaluation | ℓ_total = m + ln(1 + Σ e^(x − m)) over the observation's kind of classes and the outlier, in one pass, the others added after the largest's 1; terms more than 40 nats below m left out, and a grid class's ln s_c² skipped when its bound (with σ_ln² for s_c²) is more than 41 nats below the outlier's −5.216 nats; logaddexp skips exp and log1p at \|x − y\| ≥ (58 − k) ln 2 nats (38.8 nats at k = 2) | `bank::DurationFit::grid_loglik`, `terms`, `log_sum_exp`, `logaddexp` | the 40 nats and the skip threshold derived (no bit changes against the one-pass sum, which starts at the largest term's 1, respectively against numpy's formula; both tested bit for bit); the one-pass sum changes ℓ_total by ≤ 1.3 · 10⁻¹⁵ nats (measured, Plan B B2(a); development-set texts and records identical) |
| Bank periodicity method | the comb on Π = 2T over branch 1's posterior p (the edge comb and the spectrum fit are not ported) | `BankConfig::periodicity_method`; `bank::Periodicity` | owner (E1); Π = 2T derived |
| Bank periodicity input and updates | p averaged to r_P = r / max(1, round(r / 750 samples/s)) (750 samples/s at r = 1500 samples/s); recomputed every 0.25 s of p (375 samples at 1500 samples/s) | `BankConfig::periodicity_rate_hz`, `periodicity_update_s` | heuristic |
| Bank periodicity windows | 2, 5 and 10 s (1500, 3750, 7500 samples at 750 samples/s); the shortest confident window gives T_P; reach caps T at ≈ W / 18.3 (109, 273, 546 ms) | `BankConfig::periodicity_windows_s` | windows placeholder (E2); reach caps derived from a heuristic rule |
| Bank comb teeth and width | 4 teeth at kΠ, k = 1…4, negative teeth at (k ± ½)Π, each ±0.075 Π (±15% of T) wide; score = mean contrast, dimensionless; biased autocorrelation | `BankConfig::comb_teeth`, `comb_width`; `bank::comb_estimate` | placeholder (E3), not measured for the comb on 2T; biased estimate heuristic |
| Bank comb confidence | T_P counts when its window's score ≥ 0.03 (dimensionless) | `BankConfig::comb_confidence_min` | placeholder (E1) |
| Bank text model | ln(w / 2688) nats per character (VE3NEA's table); valid codes missing from it ln(8/2688) = −5.817 nats; "*" ln(10⁻⁶) = −13.816 nats; mean over the branch's last 10 characters (word spaces not counted), applied by the channel decoder | `bank::TextModel`; `BankConfig::text_window_chars`; `bank::Branch::text_logprob` | probabilities derived (VE3NEA); the two fallbacks heuristic; window 10 characters placeholder, kept by E8 |
| Bank eligibility | \|ln(L_k / (0.8 T_k))\| ≤ ln 1.1 = 0.0953 (one ladder step), fit weight ≥ 8 elements | `BankConfig::eligibility_tolerance`, `min_fit_weight`; `bank::Selector::eligible` | heuristic |
| Bank selection ties | quality tie ε_Q = 0.05 nats per element; then text tie 0.1 nats per character; then the longest branch. None eligible: text leading by ≥ 1.0 nats per character, else nearest 0.8 T_P, else branch 1 | `BankConfig::quality_tie_nats`, `text_tie_nats`, `text_separation_nats`; `bank::Selector::best` | ε_Q placeholder, kept by E8; text tie and separation heuristic |
| Bank switch persistence | M = 4 selection instants in a row, of one kind (eligible or fallback) | `BankConfig::switch_persistence`; `bank::Selector::update` | placeholder, kept by E6 |
| Bank block cadence | every stage advances once per block of round(block_s · r) samples, block_s = 32/1500 s = 21.33 ms (32 samples at 1500 samples/s); input in pieces of any length, a partial last block at the end | `BankConfig::block_s`; `bank::BankChannel::push`, `finish` | heuristic (the engine's channel block) |
| Bank stored power | \|v_k\|² rounded to float32 as each sample arrives (as the prototype stores P), FS²; the channel's window stores it as a 4-byte float (lossless) and every read widens it to double exactly | `bank::boxcar_power_f32`; `bank::PowerMatrix`, `bank::BankChannel` | rounding a numerical choice (the prototype's, reproduced); the float storage a memory choice with no arithmetic effect (Plan B B1: development-set texts identical) |
| Bank new over | key up longer than T_new = max(0.5 s, 12 · T_g) since the branch's last key-up (2.88 s without a fit; 0.576 s at 25 words/min) | `BankConfig::new_over_min_s`, `new_over_gaps`; `bank::Branch::new_over_due` | placeholders, kept by E7 |
| Bank re-key and time-out | the over's start re-keyed with the full LLR after W_min = 0.8 s of keyed time, at the seed s² and the previous over's s² (best mean log-likelihood per element wins), over at most 20 s back; if W_min is not reached within 2 s, re-keyed at the previous over's s², or its provisional characters deleted | `BankConfig::rekey_after_s`, `rekey_timeout_s`; `bank::Branch::rekey_over`, `clear_over` | W_min measured (E9b); the 2 s time-out a placeholder (heuristic); candidate choice heuristic |
| Bank fresh fit against the previous | the over's fresh fit replaces the previous over's continued fit with ≥ 8 of the over's marks and spaces and a log-likelihood gain > ½ · 4 · ln n nats on them (4.16 nats at n = 8); the competition ends after 192 (⌈4 N_mem⌉) | `BankConfig::fresh_fit_min_obs`; `bank::kFitParameters`; `bank::Branch` | 8 a placeholder (heuristic); ½ k ln n form derived (BIC), k = 4 heuristic; the end at 4 N_mem derived (e⁻⁴ = 1.8%) |
| Bank corrections | a replacement at t changes nothing that starts before t − 20 s; overlap cut at max(from, t − 20 s); recorded only if the text differs, with its kept-character count | `BankConfig::correction_reach_s`; `bank::Output::replace_from` | 20 s owner; overlap cut heuristic |
| Bank switch replacement | from the start of the new branch's character containing the time its eligible run began (its own time base); a fallback pick from the switch's time | `bank::BankChannel` | heuristic (spec 4.8; the fallback rule documented behavior) |
| Bank anchor mixing (engine) | u[n] = y[n] exp(−jφ[n]), φ[n] = 2π (Σ_{m≤n} Δ[m] − Δ[n]) / r, Δ = detector frequency (oracle: the label's, without its drift) − channel center, Hz, per channel block; no tracker | `BankDecoder::process` | derived: the prototype's `anchored_baseband`, mixing at the frequency where option 1 (the detector decides where the station is) puts the station; the oracle anchor without drift is the plan's choice, not tuned |
| Bank events (engine) | new characters, then corrections (index: the smaller of the number kept and the first position whose text changed; the bank's characters from it on; time, reason, reach), applied in order, plus a "resync" correction if the consumer's list ever differs from the bank's (its reach not bounded by the 20 s correction reach; never on the suite, measured; tested by forcing one); final text = all corrections applied, immediate text = characters as first appended | `TextCorrection`, `DecodeUpdate::corrections`, `DecodedTextEvent::corrections`; bench `TrackText` | derived: the consumer's rule rebuilds the bank's final text exactly (proof in section 8c); not signal processing, an interface choice of the port (owner decision D1 for `--decoder`); the event's probability 1, speed 0 and confidence 0 are placeholders |

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
- **Farnsworth spacing** (synthetic recordings, `farnsworth_wpm`): elements
  and element spaces at the character speed c (T = 1.2 s / c); character
  and word gaps drawn on the gap timebase T_g = (60/s − 37.2/c)/19 s instead
  of T, for an overall speed s (the ARRL standard, Bloom 1990: the added
  time per PARIS over its 19 gap units), so one PARIS takes 60/s s
  (derived). Random keying styles draw the gaps with their own σ_ln in
  units of T_g; an imbalance keeps its length in seconds. Suite group I:
  (c, s) = (18, 5), (18, 10), (25, 13), (25, 18) WPM (T_g/T = 7.84, 3.11,
  3.43, 2.02), machine and paddle keying, S₅₀₀ 5, 10, 20 dB. Labels carry
  `farnsworth_wpm` only when it is set.
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
  0.174 ms/s (oracle), 0.015 and 0.196 ms/s (detector) (Task 14). Task
  17's two runs after Tasks 15 and 16 read 16–24% higher on both paths,
  Envelope's code unchanged, so these figures vary by about a fifth
  between sessions on this machine (section 8b). A Raspberry Pi 5 is not
  yet measured.
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
  edits, a margin of 52 over its 34). The Matched figure was dominated
  by one station's start-up (CER 0.234; section 8b, "A channel that
  opens mid-transmission"), which a platform difference that moves the
  channel's opening by one hop could change by tens of edits. **After
  Tasks 15 and 16** the Matched smoke CER is 0.0436 = 42/964 on Windows
  (measured, Task 17; that station 0.043). The limit is never widened,
  and is tightened only when the Matched CER is below 0.0622 on both CI
  platforms, Windows and Linux, to the higher of the two plus 6/964,
  rounded up to two decimals (on Windows alone that would be
  48/964 = 0.0498, rounded up to 0.05). The Linux value has not been
  measured yet (the branch has not run on CI), so the limit stays 0.07
  and `bench/baselines/smoke-matched.json` is unchanged.
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
  within a signal, so signals are the units), and decoders are compared
  signal by signal on the same recordings. The full suite is sized for
  3 seeds: at least 1000 characters per S₅₀₀ point in groups A–C, and at
  least 100 fade times per point at f_D = 0.1 Hz.
  **QSO regimes** (group H): how the detector sorts a QSO's two stations
  into tracks depends on the decoder's attribution rule (section 6), so
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
- **Recorded channel streams** (`kz4ap-bench --oracle --record-channels
  DIR`): for each label i, `channel-(i+1).c64` holds oracle channel i + 1's
  channelizer output (r = 1500 samples/s at 48 and 192 kHz; complex64, I
  then Q, little-endian; FS), before the frequency tracker, so the station
  sits at f_off = labeled frequency − channel center (up to ±½Δf =
  ±11.7 Hz); `channels.json` gives each channel's label index, labeled
  frequency, center (Hz from the span's center), first sample index and
  sample count. The prototype mixes the stream down by f_off (and a
  labeled drift) itself.
  **Detector form** (`--record-channels DIR` without `--oracle`, used with
  `--front-end matched`): `channel-<track id>.c64` holds the channelizer
  output of every channel the detector opens, from its first block to its
  track's death (to the end if it never dies). `channels.json` gives each
  one's track id, `birth_freq_hz` (the track's frequency at birth, Hz from
  the span's center), center, first sample index, `open_s` (first sample
  index / r, s), `close_s` (end of its last block, s; null unless the track
  died), sample count, and `anchors`: [first sample index, Hz] at every
  block where the detector's frequency for the track changed
  (`ChannelBlock::anchor_hz`). `label_index` and `label_freq_hz` are null
  (and `birth_freq_hz` is null for oracle channels, whose entries carry
  `open_s`, `close_s` and `anchors` too). The prototype
  (`streams.anchored_baseband`) mixes a detector channel down by
  anchor − center, block by block, with a continuous phase: the anchor the
  Matched path's NCO starts from and follows (option 1), without the
  tracker's fine-tuning within ±12 Hz of it. Mixing at the channel's
  center alone would leave the station up to ±½Δf = ±11.7 Hz off, which
  through a 40 ms branch costs 10·log₁₀|sinc(11.7 Hz × 40 ms)|² ≈ −3.4 dB of
  signal power relative to a centered station (derived), a handicap the
  Matched path does not have.
- **Externally decoded text** (`kz4ap-bench --labels L --score-decoded
  D.json`): one text per label, in the labels file's order, scored as
  oracle tracks 1…n at the labels' frequencies by the same scoring and JSON
  as an engine run (`bench/src/report.cpp`); `tracked_freq_hz` is null and
  `channel_seconds` 0. Checked identical to an engine oracle run's own
  texts on the smoke recording (apart from `tracked_freq_hz`).
  **Tracks form**: `{"front_end", "recording", "tracks": [{"id",
  "freq_hz" (birth, Hz from the span's center), "text", "last_freq_hz"
  (optional; defaults to "freq_hz")}]}`, one entry per detector track,
  scored as the engine's detector path is — tracks at their birth
  frequencies matched to labels within 50 Hz, false tracks counted — by the
  same code (`score(..., match_by_order = false)`, `score_json`,
  `print_score`); `tracked_freq_hz` is each track's `last_freq_hz`. A file
  must give either `texts` or `tracks`, not both. Checked identical on the
  smoke recording's Matched detector path (apart from `tracked_freq_hz`).
- **Stage-1 prototype runs** (`training/kz4ap_proto/runner.py`,
  `metrics.py`, `report.py`; milestone 2b, stage 1; Python, not the
  engine). **Oracle copies:** the recordings of the groups normally decoded
  through the detector path (pauses, strong, tune-up, first sample, band,
  crowded; `suites.ORACLE_COPY_GROUPS`) are decoded once more on oracle
  channels, as result `<recording>.oracle` in
  group `<group>, oracle`, by Envelope and Matched (`kz4ap-bench --oracle`)
  and by the prototype, so every regime has a like-for-like decoder
  comparison; group H already has its own oracle copy. **Channels:** the
  oracle test cases' channels are recorded with `--oracle --front-end envelope
  --record-channels` and mixed at the label; every non-oracle recording is
  recorded once more through the Matched path's detector (`--front-end
  matched`, no `--oracle`, D_ch = 47 Hz) as `<recording>.detector`, each
  channel decoded from its opening and mixed by the detector's frequency
  block by block, and scored in the tracks form under the engine's own
  result names, so on the detector path the prototype pairs with Matched
  per track (the same tracks) and with Envelope per label (Envelope's
  detector opens its own tracks). **Comparison** (no acceptance gate;
  owner, 2026-09-30): for each (group, tag), the mean over signals of the
  per-signal CER difference (prototype minus reference, on the same
  labels), with a paired bootstrap 95% interval; "better" if the
  interval's upper end is below 0, "worse" if its lower end is above 0,
  else "unchanged"; "no interval" below 2 signals. The CER columns beside
  it are pooled (summed edits over summed symbols), so a paired mean and
  the difference of two pooled CERs can differ, even in sign. **What the
  statistics do and do not model:** the "interval excludes 0" verdict is
  a convention (heuristic), not a derived decision rule. The bootstrap
  unit is the signal (1000 resamples of signals within each group and
  tag, the milestone-2 convention); signals of one recording share its
  noise and keying draws, and that within-recording correlation is not
  modeled, so intervals are likely too narrow where a regime has few
  recordings (the report shows the number of recordings per regime).
  There is no multiplicity correction: with R regimes compared, about
  0.05·R that are truly unchanged are expected to read better or worse by
  chance; the report prints that number beside its counts. Rows not comparable by construction are marked and not
  counted: group F drift (the oracle mix follows the labeled drift,
  favoring the prototype) and group H oracle QSO labels with a nonzero
  answering offset (the answering station is off the mix, handicapping
  it). **Detection measures** on the detector path, per decoder and
  group: labels scored, labels detected, detection recall (detected /
  scored) and false tracks (tracks that decoded text and matched no
  label), summed over each recording's main test case, and tracks per QSO
  (group H, as in the suite summary); as the bench counts them they depend
  on the decoder. **The prototype's own statistics** (oracle channels,
  scored labels): *speed error* — at each selection instant from 3 s after
  a transmission's start to its end, the selected branch's fitted dit T is
  "off" if |ln(T/T_true)| > ln 1.5 or there is no fit, T_true = 1.2 s/WPM,
  constant-speed labels only (no `wpm_end`; QSOs only if both senders share
  a speed) at S₅₀₀ ≥ 6 dB; reported as the fraction of instants off, with a
  bootstrap 95% interval over channels, and *lock-ins*, transmissions with
  off instants for 3 s or longer in a row; *switches* of the selected
  branch per minute of transmission time, and *alternations*, a switch
  straight back to the previous branch within 5 s; *spurious over starts*,
  over starts from 1 s after a transmission's start to its end, per
  transmission; *false characters*, final characters other than word
  spaces starting outside every transmission padded by 0.5 s, per minute
  outside the padded transmissions; *decoding CPU*, process CPU time per
  channel-second (s/s), Python, not comparable with the engine's C++. All
  thresholds here (×1.5, 3 s, 5 s, 1 s, 0.5 s, 6 dB) are heuristic choices
  of the report, not measured.
