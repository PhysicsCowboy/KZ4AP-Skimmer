# Signal Processing Pipeline

How KZ4AP Skimmer turns a stream of complex I/Q samples into decoded Morse
text. This document describes what the code actually does, with the numbers
it actually uses, and says for each choice whether it was **derived**,
**measured**, or is a **heuristic** (an unmeasured judgment call). Heuristics
are candidates for measurement; see `docs/backlog.md`.

**Maintenance rule:** any change to the engine's signal processing (a
parameter, an algorithm, the order of stages) updates this document in the
same commit.

Numbers below are for the default 192 kHz sample rate unless stated. Where a
value scales with the sample rate, the scaling is given.

## Overview

```
  I/Q samples (complex, fs = 192 kHz)
        │
        ├──────────────► Spectrum analyzer ──► Signal detector ──► tracks (frequency, SNR)
        │                (Hann window, N-point FFT,   (1 s power average,        │
        │                 power per bin)               peak picking)             │ opens / closes
        │                                                                        ▼ channels
        └──────────────► Channelizer ─────────────────────────────────► one complex stream
                         (shared N-point FFT; per channel: shift to        per station,
                          0 Hz, low-pass, decimate ×128)                   1500 samples/s
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
  (imaginary part), scaled to [−1, 1).
- Complex sampling means the stream covers −fs/2 … +fs/2 around the radio's
  center frequency: ±96 kHz at 192 kHz. Frequencies throughout the engine are
  **offsets from the center**, in Hz.
- The engine consumes input in blocks of exactly N/2 samples (the hop),
  buffering any remainder, so results are identical however the caller
  splits the input.

## 2. The FFT size N and bin width

N is the number of samples per transform, which is also the number of
frequency bins. Nyquist fixes the total span (fs wide, complex); N sets how
finely it is divided:

- bin width Δf = fs / N
- each transform spans N / fs seconds of signal

The engine chooses N as the **largest power of two with Δf ≥ 20 Hz**:

| fs | N | Δf | transform length | hop (N/2) |
|---|---|---|---|---|
| 48 kHz | 2048 | 23.4 Hz | 42.7 ms | 21.3 ms |
| 96 kHz | 4096 | 23.4 Hz | 42.7 ms | 21.3 ms |
| 192 kHz | 8192 | 23.4 Hz | 42.7 ms | 21.3 ms |

**Status: heuristic.** ~23 Hz was chosen as narrow enough to separate
stations a few tens of Hz apart and to keep noise per bin low, while wide
enough that a keyed CW signal's energy (tens of Hz wide at contest speeds)
falls in one to three bins. It has not been measured against alternatives.

## 3. Spectrum analyzer (for detection)

- Every hop (21.3 ms), take the last N samples, multiply by a periodic Hann
  window, FFT, and compute power per bin: |X[k]|² / (Σw)².
- That normalization makes a pure tone of amplitude A exactly on a bin read
  20·log₁₀A dB in that bin (coherent-gain correction). Noise, by contrast,
  reads its power in the window's equivalent noise bandwidth, 1.5 bins
  ≈ 35 Hz.
- The Hann window spreads even a pure tone over several bins: its main lobe
  is 4 bins wide, and a tone between two bins shows up in both (up to 1.4 dB
  "scalloping" loss at the midpoint). A keyed signal spreads further, by its
  keying sidebands.
- Bins are stored lowest frequency first: bin i is at (i − N/2)·Δf.

## 4. Signal detector

Finds CW carriers and keeps a list of **tracks** (one per station).

**Averaging.** Each bin's *linear* power is averaged over time with a
first-order (exponential) filter, time constant **1 s**; for the first frames
it is a plain running mean. Averaging is **per bin**: bins are never merged.

Why average at all, and why 1 s (**status: heuristic, reasoned but not
measured**):
- A keyed signal is on only about half the time. Averaged over many elements
  it looks like a steady carrier about 3 dB below its key-down power, so
  detection doesn't depend on catching it key-down.
- Averaging shrinks the noise fluctuation in each bin. A single frame's noise
  power per bin is exponentially distributed (it fluctuates by its own mean).
  An exponential average with τ = 1 s over 21.3 ms frames behaves like a mean
  over about 2τ/hop ≈ 94 frames, cutting the relative fluctuation to about
  1/√94 ≈ 10% (≈ ±0.45 dB). The maximum of 8192 such bins stays well under
  the 6 dB threshold, so noise alone almost never creates a track.
- The cost is latency: a new station takes up to about 0.5–1.5 s to be
  detected, and a track outlives its station by several seconds while the
  average decays (measured: ~7.8 s with a 1 s timeout). Both matter; see the
  backlog.

**Noise floor.** The median of the averaged spectrum, in dB, across all bins.
One number for the whole span, recomputed every frame. (A real SDR's passband
is not flat, so a local floor may be needed later.)

**New tracks.** A bin becomes a candidate when its averaged power is at least
**6 dB** above the floor *and* it is a local maximum within ±2 bins (ties go
to the lower bin). A candidate must persist (within ±1 bin) for **0.5 s**,
and nothing is detected during the first 1 s of a recording (the averages
settle first). Candidates within **3 bins** (70 Hz) of an existing track are
attributed to that track instead.

**Why "neighboring bins" come up at all.** The ±2-bin local-maximum rule and
the 3-bin attribution rule are not a coarser resolution; they undo the
spreading described in section 3. One station puts power in 2–4 adjacent bins
because of the Hann window's main lobe, its position between bins, and its
keying sidebands. Without these rules one station would become several
tracks. Using larger bins instead would be worse on two counts: noise per
bin grows in proportion to bin width (a 3× wider bin has ~4.8 dB worse SNR
for the same signal), and two stations closer than a bin could no longer be
told apart. The bins themselves keep their full resolution; only the
decision "is this a new station?" looks at a neighborhood.

**Existing tracks.** A track's level is the maximum of its bin and the two
neighbors (a station can sit between bins or drift slightly). It stays alive
while that level is at least **6 − 3 = 3 dB** above the floor (hysteresis),
and dies after **10 s** below it.

**Frequency.** A new track's frequency is refined by parabolic interpolation
over the peak bin and its neighbors in dB: typically accurate to a few Hz.
Measured: a station at +1000.0 Hz is reported at +999.8 Hz.

**Cap.** At most 200 tracks; a stronger new station replaces the weakest.

## 5. Channelizer: one stream per station

For each track, the channelizer produces a narrow complex baseband stream
centered on the station, at a low sample rate. Mathematically, for a station
at center frequency f_c (rounded to the nearest bin, so within ±11.7 Hz of
the track frequency):

1. **Mix down (complex, I/Q):** multiply the input by exp(−j2π·f_c·t). This
   moves the station to 0 Hz. It is a complex frequency shift, not an audio
   demodulation: the result is still complex I/Q, now centered on the
   station.
2. **Low-pass filter** the shifted signal.
3. **Decimate** by D = N / 64 = 128, giving 192000 / 128 = **1500 complex
   samples per second** (at every fs in the table above, since N scales with
   fs).

So the channel filter is applied **after** the frequency shift and
**before** decimation, and the decoder receives the **filtered** stream.

**How it's computed.** Steps 1–3 are not done literally per sample. They are
done at once with overlap-save fast convolution:
- One N-point FFT per hop, shared by all channels (unwindowed; the history of
  N/2 earlier samples provides the overlap).
- For each channel, take the 64 FFT bins centered on f_c, multiply by the
  filter's frequency response, and inverse-FFT those 64 bins. Selecting bins
  around f_c is the frequency shift; the multiplication is the filter; using
  64 bins instead of N is the decimation.
- Keep the last 32 of the 64 output samples (the rest are corrupted by
  circular wrap-around), then correct a phase term so each channel's phase is
  continuous from block to block.

The cost is one large FFT per hop plus one 64-point inverse FFT per station,
instead of a mixer and filter per station per input sample. The result is
identical to steps 1–3 up to floating-point rounding (verified by the unit
tests and an independent hand derivation in review).

**The channel filter** (**status: heuristic**):
- A linear-phase FIR low-pass: a Blackman-windowed sinc, N/2 + 1 taps
  (4097 at 192 kHz; always 21.3 ms long), normalized to unity gain at 0 Hz.
- Cutoff (−6 dB) at **±150 Hz** around the station. The Blackman transition
  band is roughly ±130 Hz around the cutoff: flat (to a small fraction of a
  dB) within about ±20 Hz, below −70 dB beyond about ±280 Hz.
- Group delay: half the filter length, 10.7 ms. Decoded timestamps include
  it.
- The 1500 Hz output rate puts the output Nyquist frequency at ±750 Hz, far
  outside the filter's stopband, so decimation doesn't alias.
- The ±150 Hz width was sized for fast code. It is wider than slow code needs,
  so slow weak stations get more noise than necessary. Adapting it per
  station to the measured speed is in the backlog.

**Residual frequency offset.** Because f_c is rounded to a bin, a station can
sit up to ±11.7 Hz from 0 Hz in its channel, well inside the flat passband.
The offset appears as slow phase rotation, which the decoder ignores (it uses
only the magnitude).

A channel opens when its track is born and closes when it dies. It starts
with the current block, so the decoder never sees the signal from before the
detector noticed it: see "first characters" in the backlog.

## 6. Classical decoder (per station)

Input: the station's 1500 Hz complex stream. Output: symbols (characters,
`<XX>` prosign tokens, word spaces), each with a probability and start/end
times.

1. **Envelope detection.** Take the magnitude |y| of each complex sample. This
   is non-coherent AM detection: it needs no carrier recovery, and the
   residual frequency offset and phase don't matter. There is no audio tone
   (BFO) anywhere in the decoding path.
2. **Smoothing.** A first-order low-pass on the magnitude, time constant
   **¼ dit** at the current speed estimate (12 ms at 25 WPM). It adapts as the
   speed estimate changes. (**Heuristic.**)
3. **Warm-up.** For the first dit (48 ms at the initial 25 WPM), the envelope
   is a running mean and nothing is keyed, so the level trackers start from
   the input's actual level. (**Measured:** without it, noise at the start
   keyed as one 1.4 s mark.)
4. **Mark and space levels.** Two trackers follow the envelope: the *mark*
   level rises quickly (4 ms) toward peaks and relaxes slowly (3 s); the
   *space* level falls quickly toward troughs and relaxes slowly.
5. **Keying decision** with hysteresis: key down when the envelope rises
   above 60% of the way from space to mark; key up below 40%.
   **Squelch:** no keying unless mark > 3 × space. That's about a 9.5 dB
   envelope ratio; in noise alone the ratio measures about 1.45. It also
   means signals below roughly 6 dB SNR (in 500 Hz) are not decoded at all.
   (**Heuristic;** 3 s and 3× were estimated, then confirmed by tests.)
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
10. **Speed.** From the last 24 marks, sorted by duration, split into dits and
    dahs at the largest ratio between neighbors (if it exceeds 1.8).
    - If the dah/dit ratio is between 3.0 and 3.85, dit =
      (mean dah − mean dit) / 2. That cancels the constant shortening every
      mark gets from the keying edges. (**Measured:** it fixed 45 WPM reading
      as 51.)
    - Otherwise, dit = the mean of the dits and one third of each dah.
      Ratios above 3.85 are taken as the sender's weighting (hand-sent
      four-dit dahs measure 3.95–4.32).
    - Marks longer than 0.96 s (a four-dit dah at 5 WPM) are excluded, as
      carriers rather than Morse elements.
    - The result is clamped to 5–60 WPM.

## 7. Timing and latency

| Stage | Delay |
|---|---|
| Hop (processing block) | 21.3 ms |
| Detection | 1 s warm-up at recording start; then ~0.5–1.5 s for a new station (averaging + 0.5 s persistence) |
| Channel filter group delay | 10.7 ms |
| Decoder smoothing | ~¼ dit |
| Character emitted | after a 2-dit gap follows it |
| Track removed | ~8 s after the station stops (10 s default timeout plus average decay) |

## 8. Parameters at a glance

| Parameter | Value | Where | Status |
|---|---|---|---|
| Bin width | ≥ 20 Hz (23.4 Hz) | `choose_fft_size` (engine.cpp) | heuristic |
| Spectrum window | periodic Hann | spectrum.cpp | standard choice |
| Power average τ | 1 s | `DetectorConfig::average_s` | heuristic |
| Detection threshold / hysteresis | 6 dB / 3 dB | `DetectorConfig` | heuristic |
| Persistence before a track | 0.5 s | `DetectorConfig::birth_s` | heuristic |
| Track timeout | 10 s | `DetectorConfig::death_s` | heuristic |
| Min station separation | 3 bins (70 Hz) | `DetectorConfig::min_separation_bins` | heuristic |
| Peak neighborhood | ±2 bins | signal_detector.cpp | heuristic |
| Channel bins / decimation | 64 / N÷64 (1500 Hz out) | `EngineConfig::channel_bins` | heuristic |
| Channel filter cutoff | ±150 Hz (−6 dB) | `EngineConfig::channel_cutoff_hz` | heuristic |
| Channel filter | Blackman-windowed sinc, N/2+1 taps | channelizer.cpp | standard choice |
| Envelope smoothing | ¼ dit | `ClassicalDecoderConfig::smoothing_dits` | heuristic |
| Level attack / decay | 4 ms / 3 s | `ClassicalDecoderConfig` | heuristic, test-confirmed |
| Squelch | mark > 3 × space | `ClassicalDecoderConfig::squelch_ratio` | heuristic, test-confirmed |
| Key thresholds | 60% down / 40% up | classical_decoder.cpp | heuristic |
| Glitch limit | 0.3 dit | `ClassicalDecoderConfig::glitch_dits` | heuristic |
| Dit/dah boundary and width | 2 dits, 0.08 (log) | classical_decoder.cpp | heuristic |
| Character / word gap | > 2 / > 5 dits | classical_decoder.cpp | standard midpoints |
| Speed window / range | 24 marks, 5–60 WPM | classical_decoder.cpp | heuristic |
| Edge-shortening ratio band | 3.0–3.85 | classical_decoder.cpp | measured |

## 9. Definitions used in tests and the benchmark

- **SNR** (synthetic recordings): carrier power over noise power in a
  **500 Hz** bandwidth. The generator adds complex white noise across the
  whole sampled span, σ = 0.02.
- **Character error rate (CER):** the minimum number of symbol insertions,
  deletions and substitutions to turn the decoded text into the reference,
  divided by the number of reference symbols. A prosign token counts as one
  symbol, and word spaces count as symbols.
