# VE3NEA DeepCW: synthetic-data generator parameters

Extracted 2026-09-27 from [VE3NEA/DeepCW](https://github.com/VE3NEA/DeepCW) at commit
[`2c8fdac`](https://github.com/VE3NEA/DeepCW/tree/2c8fdac01bb2bf07d80989b0e5aabfe8cb87d76b)
(last commit 2024-10-27). License: MIT, "Copyright (c) 2024 Alex Shovkoplyas" [fact: `LICENSE`].
Links below use the prefix `R/` for
`https://github.com/VE3NEA/DeepCW/blob/2c8fdac01bb2bf07d80989b0e5aabfe8cb87d76b/`.

**References.** "DG cell n" is cell index n (0-based, counting markdown cells) of
`R/data_generation.ipynb`; likewise "VAL" = `R/validation.ipynb`, "MOD" = `R/model.ipynb`,
"AC" = `R/accuracy_charts.ipynb`, "ER" = `R/error_rate.ipynb`. Most generator code lives in
notebook cells that begin `%%writefile data_generation/<module>.py`; those `.py` files are
*not* committed (`data_generation/readme.txt`: "files in this folder are auto-generated"), so
the notebook cells are the source of truth. The one committed module is
`R/model/training_settings.py`.

**Labels.** **[fact: where]** read in the code or its stored outputs; **[inference]** my
reasoning or arithmetic; **[checked]** a small pure-Python calculation done here (no
packages installed).

## Symbols

| Symbol | Meaning | Unit |
|---|---|---|
| f_s | generator audio sample rate, 6000 | samples/s |
| W | keying speed | WPM (PARIS) |
| T | dot duration, T = 1.2 s / W | s |
| μ, σ_ln | mean and standard deviation of ln(element length / T) | 1 (natural-log units) |
| δ | per-transmitter key-on/key-off imbalance | s |
| n_e | samples per raised-cosine edge, round(f_s · edge_ms · 10⁻³) | samples |
| A | key-down signal amplitude, 10^((L_nf + ρ)/20) | FS |
| σ_n | noise amplitude per quadrature, 10^(L_nf/20) | FS |
| L_nf | "noise_floor", an overall level | dBFS |
| ρ | DeepCW's "snr" (definition in §3) | dB, key-on, noise in 3 kHz |
| g(t) | complex fading gain | 1 |
| f_D | DeepCW's "doppler_spread" (definition in §2) | Hz |
| σ_D | standard deviation of a Gaussian fitted to the Doppler power spectrum | Hz |
| f_c | −3 dB cutoff of the fading low-pass filter, per quadrature | Hz |
| S₅₀₀ | KZ4AP's SNR: key-down carrier power over noise power in 500 Hz | dB |
| CER | character error rate (definition in §6) | 1 |

## Summary

- Keying styles: `HandKey`, `Vibroplex`, `Paddle`, `Computer` (Vibroplex is defined but not
  used in training or the benchmark). Every element length is log-normal:
  T · exp(N(μ, σ_ln²)) ± δ, with δ ~ N(0, (0.1 T)²) drawn once per transmitter. Speed is
  uniform in WPM and constant for each ≈ 27 s transmission; no drift.
- Fading: flat (single-path) Rayleigh, complex Gaussian noise low-passed by a 2nd-order
  Butterworth filter with cutoff f_c = 0.625 f_D. The notebook fits a Gaussian to the
  resulting power spectrum and gets **2σ_D = 1.01 f_D**, so DeepCW's f_D is, to about 1–2%,
  the 2σ convention KZ4AP uses. The spectrum is not actually Gaussian (tails fall as f⁻⁴).
- SNR ρ: fading-averaged key-down signal power over **white noise in the 3 kHz band 0–f_s/2**.
  **S₅₀₀ = ρ + 7.78 dB.**
- Text: i.i.d. characters drawn from a 41-symbol frequency table plus a word-length
  distribution, both said to come from CW Skimmer decodes on the air. Both tables are in the
  repo (MIT). No templates, callsigns, prosigns, or bigram structure.
- No QRM, chirp, clicks, AGC, drift, or multipath delay.
- Benchmark: {HandKey, Paddle} × W ∈ {12, 18, 24, 32, 40} × f_D ∈ {0.1, 0.3, 1, 3} Hz ×
  ρ ∈ {−16, −12, −6, −3, 0, 6, 10, 20, 30, 50} dB, with at least 30 000 characters per
  point. CER is the Levenshtein distance with spaces removed.
- Trained weights ship: `R/model/weights.h5` (1 525 568 bytes, Keras HDF5).

## 1. Keying styles and timing

### 1.1 Styles and how one is drawn

```python
class KeyingStyle(IntEnum):
    HandKey   = 0 # all elements are variable
    Vibroplex = 1 # dashes and spaces are variable
    Paddle    = 2 # char and word spaces are variable
    Computer  = 3 # all timing is accurate
```
[fact: DG cell 9, `keying_stats.py`]

Training draws a style with `random.choices` from
`[[HandKey, Paddle, Computer], [0.25, 0.5, 0.25]]`, so the probabilities are 0.25 / 0.50 / 0.25.
Vibroplex is never drawn [fact: `R/model/training_settings.py` line 11; the draw is in
`_get_random_settings`, DG cell 39]. The generator's default, used when no style is given, is
`Computer` [fact: DG cell 39, `get_default_settings`].

### 1.2 Timing model

- Dot duration: `seconds_per_dot = 1.2 / wpm`, so T = 1.2 s / W, the PARIS standard [fact: DG cell 9].
- Every element length is log-normal:
  `length = seconds_per_dot * exp(np.random.normal(loc=means[el], scale=devs[el]))`,
  then `+ on_off_imbalance` for dots and dashes and `− on_off_imbalance` for every space
  [fact: DG cell 9, `get_length`]. Each element is an independent draw. There is no
  correlation between successive elements and no dependence on the character.
- Imbalance: `on_off_imbalance = np.random.normal(scale = 0.1) * seconds_per_dot`, so
  δ ~ N(0, (0.1 T)²), drawn once for each `KeyingStats` object. The comment reads "many keying
  circuits turn on faster or slower than they turn off" [fact: DG cell 9].
  One `KeyingStats` object is created per transmitter [fact: DG cell 39,
  `_make_spectrogram_source`], so δ is a per-operator (per-transmitter) constant. **It is the
  only per-operator variation**: μ and σ_ln are fixed for each style.

Parameters, with columns in `MorseElement` order (Dot, Dash, IntraSpace, CharSpace, WordSpace)
[fact: DG cell 9, `means` and `devs`]:

| Style | μ (dot, dash, intra, char, word) | σ_ln (dot, dash, intra, char, word) |
|---|---|---|
| HandKey | 0, 1.50, 0, 1.50, 2 | 0.15, 0.3, 0.2, 0.3, 0.2 |
| Vibroplex | 0, 1.10, 0, 1.10, 1.94 | 0.05, 0.2, 0.05, 0.2, 0.2 |
| Paddle | 0, 1.10, 0, 1.10, 1.94 | 0.05, 0.016, 0.05, 0.2, 0.2 |
| Computer | 0, 1.10, 0, 1.10, 1.94 | 0.05, 0.016, 0.05, 0.016, 0.008 |

Median lengths exp(μ) in units of T [inference]:

- Paddle, Vibroplex and Computer: dot 1, dash e^1.10 = 3.00, intra-character gap 1,
  character gap 3.00, word gap e^1.94 = 6.96. These are the standard 1:3:1:3:7 ratios.
- HandKey: dot 1, dash e^1.5 = **4.48**, intra-character gap 1, character gap **4.48**, word
  gap e^2 = **7.39**. This is a heavy-dash, wide-spacing hand style.

The log-normal means exp(μ + σ_ln²/2) are 0.1–5% above the medians. For example, the HandKey
dash averages 4.70 T [inference].

Observations on the table [inference]:

- σ_ln is roughly the coefficient of variation. Dots vary by 5% for Computer, Paddle and
  Vibroplex, and by 15% for HandKey.
- The comments say Computer timing "is accurate", but its dots and intra-character gaps still
  have σ_ln = 0.05.
- Paddle dashes (σ_ln = 0.016) are three times *less* variable than Paddle dots (0.05). An
  iambic keyer times both from the same clock, so this looks like an arbitrary choice rather
  than a measurement.

### 1.3 How gaps are assembled

This comes from `get_char_waveform`, DG cell 13, `keying_waveform.py` [fact]:

- Each mark is `front_edge` (n_e samples) + `ones(L − n_e)` + `rear_edge` (n_e samples). Each
  intra-character gap is `zeros(I − n_e)`. The edges are the two halves of a Hann window
  (raised cosine). Consequently the 50%-amplitude mark duration is L and the 50%-amplitude gap
  is I [inference].
- After the last element's intra-character gap I₁, the code appends
  `max(1, C − I₂)` samples of silence, where C and I₂ are *fresh independent* draws.
  The total character gap is therefore I₁ + C − I₂ − δ, with mean ≈ C [inference].
  The spread is larger than the CharSpace σ_ln alone, because two extra intra-space draws enter.
- A space character adds `max(1, W_s − C')` samples of silence, where W_s and C' are fresh
  draws. The total word gap is (character gap) + W_s − C', with mean ≈ W_s [inference].
- Edge length: `edge_ms` defaults to 2 ms [fact: DG cell 39, `get_default_settings`], and
  `training_settings` does not override it. So n_e = 12 samples at 6000 samples/s.
  This is a 2 ms, 0–100% raised-cosine rise and fall.
- The validation cells use other edge values, 10 ms, 5 ms and 1 ms (DG cells 15, 29, 35).
  Those are illustrations and play no part in training.

### 1.4 Speed

- Range: `'wpm': {'low':12, 'high':48}` in the committed `R/model/training_settings.py`
  (line 9). But MOD cell 2, the cell that writes that file, now reads
  `'wpm': {'low':8, 'high':50}` [fact]. It is unknown which range trained the shipped
  weights [inference].
- Draw: `np.random.uniform(low, high)`, a continuous value uniform in WPM (not in log WPM)
  [fact: DG cell 39, `_get_random_settings`].
- Hold time: all random settings, including speed, are redrawn every
  `randomize_every_batches = 5` batches. Each batch is 512 frames × 64 samples / 6000
  samples/s = 5.46 s, so each transmission lasts 27.3 s at a constant speed [inference from
  `training_settings.py` and DG cell 39].
- There is no speed drift within a transmission [fact: absent from the code].

## 2. Fading

- **Model: Rayleigh**, flat (one path, no delay spread, no specular component)
  [fact: DG cell 21, `fading.py`; there is no Rician or multipath code anywhere].
  ```python
  class RayleighFading:
      def __init__(self, doppler_spread, sampling_rate):
          self.gain = ns.ComplexBandlimitedGaussianNoiseSource(1.25 * doppler_spread, 2, sampling_rate)
      def get(self, count):
          return 0.946 * self.gain.get(count)
  ```
- **Filter.** `BandlimitedGaussianNoiseSource(bandwidth, order, fs)` computes
  `cutoff = bandwidth / fs` and then calls `signal.butter(order, cutoff, output='sos')`. It
  applies that filter to white N(0, 1/cutoff) samples, and does so independently for I and Q
  [fact: DG cell 17, `noise.py`].
  - SciPy normalizes `Wn` to the Nyquist frequency, so the −3 dB cutoff is f_c = (bandwidth/f_s)·(f_s/2) = bandwidth/2 [inference].
  - For fading this gives **f_c = 0.625 f_D** per quadrature. The complex gain's power
    spectrum is therefore approximately ∝ 1/(1 + (f/f_c)⁴) for |f| ≪ f_s, a two-sided
    2nd-order Butterworth.
  - Its −3 dB full width is 1.25 f_D, and it falls off as f⁻⁴ [inference].
- **How f_D is defined.** DG cell 24 cites Watterson: "the spectrum should have Gaussian
  shape". DG cell 25 generates one hour of g(t) at f_D = 1 Hz and takes the periodogram. It
  then fits `a*exp(-x*x/(2*s*s))` and prints `"Two-sigma bandwidth = {2*s}"`. The stored
  output is **`Two-sigma bandwidth = 1.0116072476270135 Hz`** [fact: DG cell 25 output].
  - A least-squares Gaussian fit to 1/(1 + (f/0.625)⁴) over ±20 Hz gives 2σ_D = 1.016 Hz
    [checked], which agrees.
  - So **f_D ≈ 2σ_D**, where σ_D is the standard deviation of a Gaussian fitted to the Doppler
    *power* spectrum.
  - The factor 1.25 was evidently chosen so that the fit returns 2σ_D ≈ f_D [inference].
  - For comparison, a true Gaussian with 2σ = f_D has a −3 dB full width of 1.18 f_D
    [inference], against 1.25 f_D here.
  - The Butterworth spectrum's second-moment standard deviation is f_c = 0.625 f_D (0.616 f_D
    over ±20 Hz [checked]), so "2σ" by second moment would be 1.25 f_D. The two conventions
    differ because the tails are not Gaussian.
- **Normalization.** Before scaling, each quadrature has variance ≈ π/(2√2) = 1.11, the
  equivalent-noise-bandwidth factor of a 2nd-order Butterworth. The factor 0.946 then gives
  E|g|² ≈ 0.946² × 2 × 1.11 = 1.99 ≈ 2 [inference]. After `np.real(...)` in the audio source (§3), the fading-averaged key-down
  mean square is A². DG cell 31 checks this: a continuous faded tone at ρ = 100 dB with
  L_nf = −100 dBFS gives `amplitude: 0.9928771573188232`, whose target is 1.0 [fact: DG
  cell 31 output].
- **Depth.** Full Rayleigh: the envelope is Rayleigh-distributed with no floor or clipping
  [fact: DG cell 21].
- **Values.**
  - Training: `'doppler_spread': {'low':0.1, 'high':3}`, uniform in Hz [fact:
    `training_settings.py` line 10]. Because the lower bound is > 0, every training
    transmission is faded.
  - Generator default: 1 Hz [fact: DG cell 39]. With `doppler_spread` = 0, no fading object
    is created [fact: DG cell 39].
  - Benchmark: f_D ∈ {0.1, 0.3, 1, 3} Hz [fact: AC cell 2 `dopplers`; VAL cell 4 comment].
- **Multipath delay:** none [fact: absent].

## 3. Noise, SNR, sample rate, carrier

- **Sample rate:** 6000 samples/s [fact: DG cell 39 default `'sampling_rate': 6000`; not
  overridden in training].
- **Audio synthesis** [fact: DG cell 27, `audio_source.py`]:
  ```python
  self.amplitude = amplitude_from_db(noise_floor + snr)   # 10**(x/20)
  self.noise_rms = amplitude_from_db(noise_floor)
  audio = keying * self.amplitude
  if self.fading != None: audio = self.fading.get(count) * audio
  audio = ns.complex_gaussian(self.noise_rms, count) + audio   # I and Q each N(0, noise_rms²), white
  audio *= np.exp(1j*phase)                                    # mix up to pitch
  audio = np.real(audio)
  ```
- **Noise.** The additive noise is `complex_gaussian`, which is **white over the full band**
  (not the 500 Hz band-limited source). The markdown in DG cell 16 describes "complex
  gaussian noise with a 500-Hz bandwidth", but that `BandlimitedGaussianNoiseSource` is used
  only inside the fading generator [fact: DG cells 17, 21, 27].
  - After mixing and taking the real part, the noise is real and white over 0–3000 Hz with
    mean square σ_n² [inference]. DG cell 31 checks this: noise-only `noise_rms:
    0.9991295791495024`, target 1.0 [fact].
- **SNR definition.** ρ = 10 log₁₀( fading-averaged key-down signal mean square A² / noise
  mean square σ_n² ), **with the noise measured in the full real band 0 to f_s/2 = 3 kHz**.
  - This is how the code sets it up (DG cells 27, 31 [fact]).
  - The benchmark charts label the axis "Key-on SNR in 3 kHz at 24 WPM, dB" [fact: ER cell 9,
    `axes.py`].
  - The theory curves use `NOISE_BW = 3000` and
    `EB_N0_TO_3KHZ_KEYON_SNR = 10*log10(info_bit_rate / NOISE_BW / duty_cycle)` [fact: ER cell 7].
- **Quirk: no fading.** When `doppler_spread` = 0, the real key-down signal is A cos φ, with
  mean square A²/2, so the true SNR is **3 dB below** the nominal ρ [inference from DG cell
  27]. This does not affect training or the benchmark, where f_D ≥ 0.1 Hz. It does affect
  the `spectra.bin` export in DG cell 47.
- **Ranges.**
  - ρ: training `'snr': {'low': -16, 'high': 50}`, uniform in dB [fact:
    `training_settings.py` line 8]. Generator default: 50 dB.
  - L_nf: `'noise_floor': {'low':-20, 'high':20}` dBFS, uniform [fact: line 13]. This is only
    an overall level, since the spectrogram is renormalized (below).
- **Carrier.**
  - Pitch: `pitch` is not in `training_settings`, so the default `'pitch': 1500` Hz = f_s/4
    applies [fact: DG cell 39].
  - Offset: `'pitch_error': {'low':-30, 'high':30}` Hz, uniform, constant per transmission
    (ω = 2π(pitch + pitch_error)/f_s) [fact: `training_settings.py` line 12; DG cell 27].
  - There is no frequency drift and no phase noise beyond what the fading introduces [fact:
    absent].
- **Front end** (these define what the network sees, and are not part of the channel) [fact:
  `training_settings.py`; DG cell 33]:
  - STFT with `fft_length` 512 (11.72 Hz bins).
  - Kaiser window, β = 6, 300 samples (50 ms).
  - `frame_step` 64 samples. That is 10.67 ms, or 93.75 frames/s; the code comment says
    "93.75 ms", which is wrong [inference].
  - A 22-bin strip centered on bin round(1500·512/6000) = 128, which spans about 1371–1617 Hz
    [inference].
  - Magnitudes are taken, not power.
- **AGC / normalization.** There is no AGC. Each 512-frame spectrogram is divided by 4× its
  own standard deviation: `spectra /= np.std(spectra) * 4` [fact: DG cell 39, `_format_batch`].

## 4. Message text

- **Generator** [fact: DG cell 5, `text_generator.py`]:
  ```python
  word_end_probs = [p[i] / np.sum(p[i:]) for i in range(len(p))]   # p = word_length_probs
  if np.random.uniform() < word_end_probs[cnt]: yield ' '; cnt = 0
  else: yield random.choices(mc.morse_chars, mc.morse_char_frequencies)[0]; cnt += 1
  ```
  Characters are i.i.d. unigrams. Word length follows a hazard (end-of-word probability)
  derived from the length distribution. `word_end_probs[0] = 0`, so double spaces never
  occur. The maximum word length is 16, where the hazard is 1.0.
  There are **no QSO, CQ or contest templates, no callsign generator, no bigram or word
  model** [fact: absent from all notebooks].
- **Statistics tables are in the repo** as literals in DG cell 3 (`morse_code.py`). DG cell 2
  says: "Character frequencies and word length distribution have been collected from a large
  number of CW messages decoded with CW Skimmer on the Ham bands" [fact].
  - The underlying decode corpus, its size and its date are **not** in the repo [fact: absent].
  - The tables are covered by the repo's MIT license [fact: `LICENSE`], which requires keeping
    the copyright and permission notice in copies.
- **Character table:** 41 symbols with integer relative weights summing to 2688 [checked]:
  - Digits: 1:13 2:14 3:33 4:43 5:41 6:8 7:14 8:10 9:14 0:11
  - A:127 B:62 C:69 D:84 E:321 F:55 G:43 H:68 I:130 J:8 K:117 L:100 M:76
  - N:168 O:126 P:57 Q:68 R:95 S:159 T:236 U:61 V:23 W:95 X:16 Y:40 Z:12
  - Punctuation: `/`:19 `.`:12 `,`:9 `?`:16 `=`(BT, `-...-`):15

  So E is 11.9%, T 8.8%, N 6.3% [inference].
- **Word-length distribution** `word_length_probs`, index = length in characters, 0…16
  [fact: DG cell 3]:
  `0.0, 0.1672, 0.2569, 0.1939, 0.1745, 0.0921, 0.025, 0.008, 0.006, 0.004, 0.003, 0.003, 0.002, 0.002, 0.002, 0.001, 0.001`.
  The entries sum to 0.9416, but the hazard form normalizes implicitly. The mean word length
  is 3.06 characters [checked].
- **Prosigns:** not generated. DG cell 3 has a commented-out TODO block:
  `<`=`-.-.--.-` (CQ), `>`=SK, `#`=AR, `$`=AS, `%`=KN, `@`=BK, `+`=DX [fact].
- **Output vocabulary:** 41 characters + space + mask/blank = 43 classes [fact: DG cell 5;
  model output shape `(1, 1, 43)`, MOD cell 24 output].
- **Word spaces** are generated as described in §1.3. Each transmission starts with one
  character-gap of silence, labeled `' '` [fact: DG cell 13, `reset`].

## 5. Other impairments

None are modeled [fact: absent from all notebooks]:

- no interfering signals (QRM) or adjacent stations
- no chirp and no key clicks, beyond the fixed 2 ms raised-cosine edges
- no impulsive noise (QRN)
- no frequency drift, AGC or receiver filter shaping
- no multipath delay

The only transmitter-side impairments are:

- the key-on/key-off imbalance δ (§1.2)
- the static pitch offset of ±30 Hz (§3)

## 6. Accuracy benchmark

- **Grid** [fact: AC cell 2]:
  ```python
  wpms = [12,18,24,32,40]
  dopplers = [0.1, 0.3, 1, 3]
  snrs = [-16, -12, -6, -3, 0, 6, 10, 20, 30, 50]   # rho, dB key-on in 3 kHz
  ```
  - Four series of 20 curves (5 × 4) with 10 points each: `Skim_HandKey`, `Skim_Paddle`
    (CW Skimmer) and `Deep_HandKey`, `Deep_Paddle` (DeepCW).
  - Only HandKey and Paddle are benchmarked. There is no Computer or Vibroplex series.
  - VAL cell 4 carries the full intended loop as a comment:
    `keyings = [HandKey, Paddle]`, `wpms = [8,12,18,24,32,40,48]`, and the same `dopplers`
    and `snrs`. The recorded charts use only the five speeds above.
- **Other conditions** [fact: VAL cell 4 uses `{**ts.training_settings, **var_settings}`]:
  - Everything not in the grid is drawn as in training: pitch error uniform ±30 Hz, L_nf
    uniform ±20 dBFS, `continuous = True`, 22-bin spectrograms of 512 frames.
  - δ is random per transmitter.
- **Characters per point:** `compute_accuracy_cont(..., chars_to_use=30000)` [fact: VAL cell 4].
  - The loop runs until `total_count`, the true characters *including spaces*, reaches 30 000.
  - Each pass decodes 5 batches × 4 parallel streams, each 27.3 s long, with fresh random
    settings [fact: MOD cell 12, `accuracy.py`].
- **Decoding:** greedy CTC. The `GreedyDecoder` takes the argmax per frame and collapses
  repeats and blanks; it skips the first 2 frames [fact: MOD cell 10].
- **CER** [fact: MOD cell 12]:
  - Per stream, the predicted and true strings are stripped.
  - `char_err_rate` = Σ Levenshtein(pred without spaces, true without spaces) / Σ len(true
    without spaces).
  - A separate `space_err_rate` = (Σ Levenshtein with spaces − Σ Levenshtein without spaces) /
    number of true spaces.
  - The charts plot `err[0]`, the CER with spaces removed.
- **CW Skimmer comparison.** AC cell 1 says the Skimmer rates were "measured on the same
  data". How the audio was fed to Skimmer is not in the repo [fact: absent].
- **Snapshot at 24 WPM, f_D = 0.1 Hz** [fact: AC cell 2], as CER at
  ρ = −16 / −6 / 0 / 10 / 50 dB (S₅₀₀ = −8.2 / 1.8 / 7.8 / 17.8 / 57.8 dB):
  - Deep_Paddle: 0.901 / 0.373 / 0.137 / 0.025 / 0.005
  - Skim_Paddle: 0.896 / 0.364 / 0.101 / 0.022 / 0.011
  - Deep_HandKey: 0.914 / 0.412 / 0.186 / 0.082 / 0.063
  - Skim_HandKey: 0.895 / 0.429 / 0.188 / 0.091 / 0.083

  At f_D = 3 Hz DeepCW holds up while Skimmer does not. For Paddle at ρ = 50 dB the CER is
  0.009 for DeepCW against 0.405 for Skimmer.
- **Caveat.** VAL cell 4's only stored run, Paddle, 24 WPM, 0.1 Hz, dated
  2024-10-27, gives CER 0.951 … 0.019. The `Deep_Paddle_wpm_24_doppl_0.1` row in AC cell 2
  reads 0.901 … 0.005. So the chart data came from a different run or weights than the
  notebook's last recorded output [inference].
- **Theory curves** [fact: ER cells 3–9]:
  - They assume an ideal OOK detector with an optimal threshold on Rice versus Rayleigh
    amplitude distributions, marginalized over Rayleigh fading.
  - Bit error rate is converted to CER using the code lengths and character frequencies.
  - The Eb/N0 ↔ ρ conversion uses `NOISE_BW = 3000` and the computed duty cycle.

## 7. Trained model weights

Yes: `R/model/weights.h5`, 1 525 568 bytes, a Keras HDF5 weights file (not a full saved model)
[fact: repo tree; MOD cell 22 `model.save_weights`].

- The architecture must be rebuilt with `model_builder.build_model` (MOD cell 8):
  Conv2D 32 → MaxPool → Conv2D 64 → MaxPool → Dense 64 → LSTM 256 (stateful) →
  Dense 43 softmax, trained with CTC loss.
- The training input shape is (4, 518, 22, 1) [fact: MOD cell 18 output].
- Training ran for 50 epochs of 329 batches [fact: MOD cell 20]. That is about 2 h of audio
  per epoch across the 4 streams [inference].
- The weights fall under the repo's MIT license.
- Which `wpm` range they were trained with is ambiguous (§1.4).

## 8. Mapping to KZ4AP generator conventions

**SNR.** Both definitions use key-down power over noise power.

- DeepCW's ρ puts the noise in 3 kHz; S₅₀₀ puts it in 500 Hz. For white noise:
  **S₅₀₀ = ρ + 10 log₁₀(3000/500) = ρ + 7.78 dB** [inference].
- DeepCW's key-down power is averaged over the fading (E|g|² normalized so the faded mean
  square equals A²). This matches S₅₀₀ provided KZ4AP's fading gain is also normalized to
  unit mean power and S₅₀₀ refers to the mean (not instantaneous) carrier power.
- The benchmark ρ grid in S₅₀₀ is
  **−8.2, −4.2, 1.8, 4.8, 7.8, 13.8, 17.8, 27.8, 37.8, 57.8 dB** (S₅₀₀, key-down, 500 Hz).
- DeepCW's training range, ρ from −16 to 50 dB, corresponds to S₅₀₀ from −8.2 to 57.8 dB.
- If KZ4AP ever reproduces DeepCW's unfaded case: with f_D = 0, DeepCW's true SNR is 3 dB
  below the nominal ρ (§3).

**Fading spread.** DeepCW's `doppler_spread` f_D is the 2σ_D of a Gaussian least-squares-fitted
to the Doppler power spectrum (measured 2σ_D = 1.012 f_D). This **matches KZ4AP's
"f_D = 2σ" with a conversion factor of 1** (within about 1–2%).

- The VE3NEA grid can therefore be reproduced with f_D ∈ {0.1, 0.3, 1, 3} Hz unchanged.
- The spectral *shape* differs: DeepCW's is a 2nd-order Butterworth, |H|² = 1/(1 + (f/f_c)⁴)
  per quadrature with f_c = 0.625 f_D; KZ4AP's is Gaussian.
  - The −3 dB widths are close: 1.25 f_D against 1.18 f_D.
  - DeepCW has heavier f⁻⁴ tails, so its fading is somewhat faster and rougher at the same
    f_D.
  - An exact replica would use DeepCW's filter; for an "external anchor" comparison the
    Gaussian with 2σ = f_D is a fair match [inference].
- Both are flat Rayleigh with no delay spread.

**Keying-style parameters KZ4AP should adopt** [inference]:

1. The log-normal element model, length = T · exp(N(μ, σ_ln²)) with T = 1.2 s/W, using the
   μ/σ_ln table in §1.2 for HandKey and Paddle. Those are the two styles DeepCW benchmarks,
   so matching them is what makes the grid reproducible. Also include Computer, as the
   near-perfect-timing reference.
2. The per-transmitter key-on/key-off imbalance δ ~ N(0, (0.1 T)²), added to marks and
   subtracted from spaces.
3. The gap assembly, if exact reproduction matters: character gap = I₁ + C − I₂ and word
   gap = character gap + W_s − C', with independent draws (§1.3). Otherwise draw character
   and word gaps directly from their log-normals. The means are the same, and the variance
   is slightly lower.
4. Speed uniform in WPM, constant per transmission. For the VE3NEA anchor the range is
   12–48 WPM (or 8–50 WPM; the repo is inconsistent); KZ4AP's planned range is 10–60 WPM.
5. The style mix of 0.25 HandKey, 0.50 Paddle, 0.25 Computer.
6. Raised-cosine edges of 2 ms (0–100%).

What *not* to copy as-is, or what to extend [inference]:

- DeepCW has no per-operator variation of μ, so every HandKey operator shares one
  4.48:1 dash/dot ratio. KZ4AP's hand-keying scenario should draw μ per operator around
  these values.
- DeepCW has no speed drift within a transmission. KZ4AP's speed-change scenario needs
  its own model.
- DeepCW's Paddle dash σ_ln = 0.016 < dot σ_ln = 0.05 asymmetry looks arbitrary.
- DeepCW's text is i.i.d. unigrams only. The character and word-length tables (MIT, with
  attribution) are a reasonable fallback "random text" source. Callsigns, exchanges and
  prosigns must come from KZ4AP's own templates.
