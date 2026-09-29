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
the floor (6 dB SNR per bin) *and* it is a local maximum within **±2 bins**
(±47 Hz; ties go to the lower bin). A candidate must be seen in every frame (moving by at most
±1 bin between frames) for **0.5 s**, and nothing is detected during the
first 1 s of a recording (the averages settle first). A peak less than
**3 bins** (70 Hz) from an existing track, i.e. at most 2 bins away, is
attributed to that track instead. **Status: all heuristic.**

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

Note that these rules are counted **in bins**, so their width in Hz changes if
the bin width changes. Sweeping bin width without first restating them in Hz
would change two things at once (backlog).

### Existing tracks

A track's level is the maximum of its bin and the two neighbors (±1 bin,
±23 Hz). Its SNR is that level minus the floor. It stays active while the SNR
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

The frequency is then **fixed** for the track's life: it is not re-measured,
and the channel stays where it was put. Tolerance to drift is passive:
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

### Cap

At most **200** tracks (`DetectorConfig::max_tracks`, a config parameter not
yet exposed to users). When the cap is reached, a new candidate replaces the
weakest track if it is stronger; otherwise it is ignored. **Status:
heuristic**, a guard against CPU overload, not a measured limit. For scale:
the physical ceiling with 3-bin separation is about fs / 70 Hz ≈ 2700 tracks
at 192 kHz, and a busy contest can put more than 100 stations in 192 kHz.

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
a rectangular element) scales a tone offset by Δf (Hz) by |sinc(Δf·T_el)|
in amplitude, where sinc(x) = sin(πx)/(πx) (**derived**, from Proakis &
Salehi, *Digital Communications*, 5th ed., eq. 4.5–28;
`docs/research/proakis-ook-notes.md`, section 2.7). At ±11.7 Hz a
dit-matched filter would lose 5.1 dB at 25 WPM and 8.8 dB at 20 WPM (signal
power, relative to a centered station). So the planned narrow second-stage
filter (backlog: "Channel filtering, two stages") needs each station
re-centered to a fraction of a bin first; the current code does not do this.

A channel opens when its track is born and closes when it dies. It starts
with the current block, so the decoder never sees the signal from before the
detector noticed it: see "first characters" in the backlog.

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

## 9. Timing and latency

| Stage | Delay |
|---|---|
| Hop (processing block) | 21.3 ms |
| Detection | 1 s warm-up at recording start; then ~0.5–1.5 s for a new station (averaging + 0.5 s persistence) |
| Channel filter group delay | 10.7 ms |
| Decoder smoothing | ~¼ dit (τ_s) |
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
| Candidate tracking | may move ±1 bin (±23 Hz) between frames | signal_detector.cpp | heuristic |
| Track timeout | 10 s | `DetectorConfig::death_s` | heuristic |
| Min station separation | 3 bins (70 Hz) | `DetectorConfig::min_separation_bins` | heuristic |
| Peak neighborhood | ±2 bins (±47 Hz) | signal_detector.cpp | heuristic |
| Track level neighborhood | ±1 bin (±23 Hz) | signal_detector.cpp | heuristic |
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

## 11. Definitions used in tests and the benchmark

- **SNR** (synthetic recordings): key-down carrier power A² over the noise
  power in a **500 Hz** bandwidth, σ²·500 Hz / fs. The generator adds complex
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
- **Character error rate (CER):** the minimum number of symbol insertions,
  deletions and substitutions to turn the decoded text into the reference,
  divided by the number of reference symbols. A prosign token counts as one
  symbol, and word spaces count as symbols.
- **Message text** (synthetic recordings, `training/kz4ap_synth/messages.py`):
  CQ calls, contest exchanges, and whole ragchew QSOs as a list of overs,
  each with its sending station: CQ, answer, RST and name and QTH, rig and
  power and antenna and weather, optional chat, closing. `<BT>` separates
  thoughts inside an over; every over before the closing ends with `<AR>` and
  `<KN>` (the answer with `<AR>`); each station's closing over ends with `<SK>`.
  Templates and callsigns are this project's (heuristic). Filler text draws
  i.i.d. characters and word lengths from VE3NEA's on-air tables (DeepCW,
  MIT; E is 11.9% of characters, mean word length 3.06 characters).
