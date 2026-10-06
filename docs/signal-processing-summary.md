# Signal processing: summary

The short companion to `docs/signal-processing.md` ("the full document"):
how the engine turns complex I/Q samples into Morse text, stage by stage,
with the formulas, the default values and units, and each value's class:
**derived** (follows from the math), **measured**, **heuristic** (an
unmeasured judgment call), **placeholder** (kept until an experiment
settles it), **owner**, or a standard or numerical choice. Results,
evidence and history are left out; § references point into the full
document, the authority. Numbers are for the default 192 kHz input unless stated.

## 1. Units, symbols and the pipeline

**Units.** Nothing is calibrated to volts or dBm. Linear amplitude is in
**FS** (full scale: the WAV's 16-bit integers divided by 32768), power in
**FS²**, absolute levels in **dBFS** (dB relative to 1 FS²; a complex tone
of amplitude 1 FS reads 0 dBFS). An SNR always names its noise bandwidth
(dB SNR in 500 Hz, dB SNR per bin = in 35.2 Hz). Log-likelihoods are in
nats. Time constants are in s, or in dits where the text says so.

| Symbol | Meaning | Default |
|---|---|---|
| fs | input rate, complex samples/s | 192 000 samples/s |
| N | FFT size (= bins) | 8192 |
| Δf | bin width fs/N | 23.44 Hz |
| hop | N/2 samples; t_hop = hop/fs | 4096; 21.33 ms |
| r | channel (decoder) rate fs/128 | 1500 samples/s |
| dit | 1.2 s / WPM (PARIS) | 48 ms at 25 WPM |
| S₅₀₀ | key-down carrier power over noise power in 500 Hz | |

**Pipeline.**

```
I/Q (fs) ─┬─► spectrum analyzer ─► signal detector ─► tracks (frequency, SNR)
          │   (Hann, N-point FFT,   (1 s average, median     │ open / close
          │    |X|², dBFS)           floor, peak picking)    ▼ channels
          └─► channelizer ───────────────────────────► y[n] per station, r = 1500 samples/s
              (shared unwindowed N-point FFT; per channel:     │
               64 bins around the station, ±150 Hz filter,     ▼
               64-point inverse FFT)                  decoder per channel ─► text events
                                                      (Matched default; Envelope; bank)
```

The two FFTs share size and hop. Before every channel block the engine
gives the decoder the detector's current frequency for the track (the
*anchor*).

## 2. Input, FFT and spectrum (§1–§4)

- **Input.** 16-bit PCM stereo WAV, left = I, right = Q, divided by 32768,
  so values lie in [−1, 1) FS. Frequencies are offsets from the radio's
  center, Hz; the span is ±fs/2. Input is consumed in blocks of exactly
  N/2 samples, so results do not depend on how the caller splits it.
- **FFT size.** N is the largest power of two with fs/N ≥ 20 Hz: 8192 at
  192 kHz, Δf = 23.44 Hz, each transform 42.7 ms long. Heuristic; the real
  parameter is "bins about 23 Hz wide" (the 20 Hz threshold only
  reproduces it at 48 kHz × 2^j rates).
- **Hop.** N/2 (50% overlap), standard choice: periodic Hann windows
  shifted by N/2 sum to a constant (derived), and the channelizer's filter
  length N/2 + 1 follows from this hop (derived, §5 below).
- **Spectrum.** Every hop, P[k] = |X[k]|²/(Σw)², X[k] = Σₙ w[n]·x[n]·e^(−j2πkn/N),
  w[n] = ½ − ½·cos(2πn/N) (periodic Hann), Σw = N/2, Σw² = 3N/8, stored as
  10·log₁₀ P[k] dBFS. A tone of amplitude A FS on a bin reads A² FS²;
  white noise of σ² FS² per sample reads σ²·1.5/N = (σ²/fs)·ENBW, with the
  equivalent noise bandwidth ENBW = N·Σw²/(Σw)² = 1.5 bins = 35.2 Hz
  (derived). Hann is a standard choice: highest sidelobe 31 dB below the
  main lobe; a tone midway between bins reads 1.42 dB below its on-bin
  value (scalloping, derived).

## 3. What SNR means (§5)

S₅₀₀ is the key-down carrier power over the noise power in 500 Hz (the CW
Skimmer / RBN convention; synthetic recordings use σ = 0.02 FS of complex
white noise, so the noise power in 500 Hz is σ²·500 Hz/fs). The detector
works per bin, in the 35.2 Hz ENBW: SNR per bin = SNR in 500 Hz
+ 10·log₁₀(500/35.2) = **+11.5 dB** for a signal inside both bandwidths
(derived, white noise). A track's SNR is an averaged power, so it lies
below the key-down SNR per bin by about the duty cycle (−3 dB relative to
the key-down power at 50% key-down), lower still by the keying sidebands outside the peak bin and by up to
1.42 dB of scalloping.

## 4. Signal detector (§6)

All neighborhoods are stated in Hz and converted to bins at the point of
use with lround(Hz/Δf): 47 Hz → 2 bins, 23 Hz → 1 bin at 23.44 Hz.

- **Averaging.** Per bin, in linear power (FS²), one update per frame:
  P̄ ← P̄ + a·(P − P̄), a = max(α, 1/m), α = 1 − exp(−t_hop/τ) = 0.0211,
  τ = **1 s** (heuristic), m the frame count (a plain running mean for the
  first ~47 frames). Same noise variance as a ~94-frame boxcar (derived).
  Bins are never merged. Then everything is in dB.
- **Noise floor.** The median of the averaged spectrum over all N bins,
  every frame, one number for the whole span (heuristic).
- **New tracks.** A bin is a candidate if its average is ≥ **6 dB** above
  the floor (dB SNR per bin) and it is the maximum within **±47 Hz** (ties
  to the lower bin). It must be seen in every frame, moving ≤ 23 Hz per
  frame, for **0.5 s**; nothing is detected in the first 1 s. A peak whose
  interpolated frequency is within the **channel distance D_ch = 47 Hz** of
  an existing track's current frequency belongs to that track. All
  heuristic.
- **Frequency.** Parabolic interpolation in dB over the peak bin and its
  neighbors, offset clamped to ±½ bin.
- **Following (default path).** Every frame, before its level is read,
  each track moves to the strongest bin that is a peak by the birth rule,
  ≥ 3 dB above the floor, with interpolated frequency within D_ch of the
  track's current frequency; with none, it holds. So one track follows a
  drifting station, or moves to an answering station within 47 Hz. D_ch
  ≈ the Hann main lobe's half-width 2/(42.7 ms) = 46.9 Hz; heuristic (owner,
  2026-09-29).
- **Existing tracks.** Level = maximum over ±23 Hz of the track's current
  peak bin; active while ≥ **3 dB** above the floor (dB SNR per bin,
  6 dB minus 3 dB of hysteresis); dies after **10 s** inactive. Heuristic.
- **Cap.** At most **200** tracks; a stronger new candidate replaces the
  weakest (heuristic, a CPU guard).
- **Envelope path** (with the Envelope decoder): frequency fixed at
  birth; a peak less than 3 bins (70 Hz) from a track's bin belongs to it.
- **Oracle mode** (benchmark only) bypasses the detector and opens a
  channel at each labeled frequency, rounded to the nearest bin, for the
  whole recording.

## 5. Channelizer (§7)

For each track: a channel centered on f_c, the track's birth frequency
rounded to the nearest bin (so the station is within ±11.7 Hz of 0 Hz in
the channel); in effect (1) mix down by e^(−j2πf_c t), (2) low-pass,
(3) decimate by D. The channel opens when the track is born and closes
when it dies; the decoder never sees signal from before the birth. The
channel does not move.

- **Decimation.** D = N/64 = **128**, r = fs/D = **1500 samples/s** at
  every 48 kHz × 2^j rate (1378 samples/s at 44.1 kHz). channel_bins = 64
  is heuristic within derived bounds (r/2 above the filter's stopband edge;
  1/r = 0.67 ms ≪ a 20 ms dit at 60 WPM).
- **Computation** (overlap-save): one unwindowed N-point FFT per hop shared
  by all channels; per channel, the 64 bins around f_c times the filter's
  response, a 64-point inverse FFT, the last 32 samples kept (32 samples =
  21.33 ms per hop per channel), and a phase correction for continuity.
  Equal to steps (1)–(3) up to rounding (derived).
- **Channel filter.** Blackman-windowed sinc, linear phase, N/2 + 1 = 4097
  taps (21.3 ms; the most overlap-save allows with an N/2 history, derived),
  unity gain at 0 Hz; standard choice. Cutoff **±150 Hz** at −6 dB relative
  to the passband (heuristic, sized for ~60 WPM keying). Within 0.015 dB
  of the passband to ~39 Hz, ≥ 74 dB below the passband from ~279 Hz; noise
  bandwidth ∫|H|²df = **252 Hz**; group delay 10.7 ms. The constructor
  requires r/2 ≥ cutoff + ½·5.5·fs/taps (derived).
- **Residual offset.** Up to ±11.7 Hz, inside the flat passband. Envelope
  ignores it (magnitude only); Matched re-centers it (below); the bank mixes
  by the detector's frequency (§6.4).

### Frequency re-centering (Matched only; §7)

`FrequencyTracker`, per channel at r, ahead of the matched filter.

- **NCO:** u[n] = y[n]·e^(−jφ[n]), φ advancing 2π·f̂/r per sample; f̂
  starts at the detector's residual (track frequency − f_c) and is clamped
  to ±75 Hz (heuristic; the channel filter is 0.34 dB down relative to the
  passband there).
- **Discriminator:** on the matched filter's output v,
  z = v[n]·conj(v[n − τ_L r]) rotated by e^(j2πf̂τ_L), so its phase measures
  the station's offset directly (no loop to stabilize, derived);
  τ_L = 8/r = 5.333 ms, unambiguous range ±1/(2τ_L) = ±93.75 Hz (derived;
  the lag heuristic).
- **Average:** Z̄ ← Z̄ + α·p·(z_rot − Z̄), α = 1 − e^(−1/(τ_f r)),
  τ_f = **0.5 s** of key-down weight (p is Matched's posterior, so key-up
  leaves it unchanged). Every 32 samples f̂ ← arg Z̄/(2πτ_L) if the
  average's weight is ≥ 0.6 and |Z̄| > 0.3 × the same average of |z|.
  Heuristic.
- **Fine-tuning around the anchor:** the anchor f_a = detector frequency −
  f_c is set by the engine before every block; an estimate is accepted
  only within **±12 Hz** of f_a, otherwise the average is emptied and f̂
  returns to f_a; if f_a moves more than 12 Hz from f̂, f̂ jumps to it
  (heuristic, owner). The detector decides which station; the tracker only
  fine-tunes.

## 6. The decoders (§8, §8b, §8c)

### 6.1 Which decoder runs

`ClassicalDecoderConfig::front_end` selects the decoder: **Matched is the
default** (code default and `kz4ap-bench --decoder`, whose default is
`matched`; owner, 2026-09-29), Envelope (`--decoder envelope`, which also
switches the detector to the Envelope path's rules) or the bank
(`--decoder bank`, selectable, not the default; owner decision D1). The
bank uses the default path's detector and channelizer unchanged. Envelope
and Matched share the classical decoder's back end (steps 6–10 below).

### 6.2 Envelope (§8; not the default)

The milestone-1 hard-decision baseline (heuristic throughout, not derived
from theory).

1. |y[n]| (FS), non-coherent; no BFO anywhere.
2. Smoothing: e[n] = e[n−1] + α_s(|y[n]| − e[n−1]), α_s = 1 − e^(−1/(τ_s r)),
   τ_s = ¼ dit at the current speed estimate (12 ms at 25 WPM).
3. Warm-up: for one dit at 25 WPM (48 ms) e is the running mean of |y|,
   both levels are set to it, nothing is keyed.
4. Mark level M and space level S: asymmetric one-pole followers,
   τ_fast = 4 ms toward a new extreme, τ_slow = 3 s back.
5. Key down when e > S + 0.6(M − S), up when e < S + 0.4(M − S); squelch:
   no keying unless M ≥ 3·S and M > S.

**Shared back end (Envelope and Matched).**

6. Marks shorter than 0.3 dit are ignored; key-up dropouts shorter than
   0.3 dit are merged into the mark.
7. Element: P(dah) = logistic((ln(d/dit) − ln 2)/0.08), boundary at 2 dits.
8. A space > 2 dits ends the character; > 5 dits also emits a word space.
9. Symbols from the 62-symbol table; ≥ 8 dits read `<HH>`, an unknown
   pattern `*`; probability = product of element confidences.
10. Speed, re-estimated after every mark from the last **24 marks**: sorted
    and split at the largest neighbor ratio (if ≥ 1.8); if dah/dit is in
    3.0–3.85, dit = (mean dah − mean dit)/2 (cancels edge shortening;
    measured), else the mean of the dits and a third of each dah; marks
    > 0.96 s excluded; clamped to 5–60 WPM; starts at 25 WPM. Heuristic
    apart from the measured ratio band.

### 6.3 Matched (§8b; the default)

Per channel at r, in order: the tracker's NCO (§5), a dit-matched boxcar,
the envelope likelihood, keying on the posterior log-odds, then steps 6–10.

- **Filter.** Unity-gain boxcar of K = round(β·dit·r) samples, β = 0.8
  (the boxcar shape is the matched filter of a rectangular element,
  derived; β heuristic, 0.97 dB of output SNR below the full-length matched
  filter, derived). Noise bandwidth r/K: 25.9 Hz at 25 WPM (K = 58),
  9.9 dB below the noise power the 252 Hz channel filter passes (derived). K starts at
  the 60 WPM width (16 ms, K = 24) and is clamped to 24–288 samples
  (16–192 ms, 60–5 WPM). When K changes, σ̂_v² is scaled by K_old/K_new
  (derived for white noise).
- **Following speed.** The filter follows the decoder's dit once the speed
  window holds 8 marks; the dit it uses grows at most ×1.25 per physical
  mark, from the 20 ms acquisition dit (heuristic, owner). A mark whose
  start was not observed (no keyable key-up sample with g < −1 nat since
  keying last became impossible) is decoded but not counted for speed
  (derived rule, no new parameter).
- **Likelihood.** Λ = −a²/2 + ln I₀(a·x) nats, x = |v|/σ̂_v,
  a = ŝ/σ̂_v (Rician over Rayleigh, derived; σ_v the noise RMS per real
  component, FS); g = Λ + ln(P₁/P₀), P₁ = **0.44** (PARIS keys down 22 of
  50 dit units; derived); p = 1/(1 + e^(−g)). For noise flat across the
  filter a² = 2·S₅₀₀·(500 Hz)·K/r (S₅₀₀ linear; derived): a = 6.2 at
  S₅₀₀ = 0 dB and 25 WPM.
- **Amplitude** (heuristic running EM): ŝ² ← max(0, ŝ² + p·max(α_a, 1/W_a)·(|v|² − 2σ̂_v² − ŝ²)),
  τ_a = **0.5 s** of key-down weight (the Rician mean square is 2σ_v² + s²).
- **Noise** (three-tap guard; heuristic form, derived correction): taps
  v[n], v[n−K], v[n−2K] share no inputs. If |v[n−K]|²/(2σ̂_v²) < κ = 1.75
  and both neighbors < κ_n = 4, σ̂_v² ← σ̂_v² + max(α_n, 1/W_n)·(|v[n−K]|²/(2m(κ)) − σ̂_v²),
  τ_n = **2 s** of updates; m(κ) = 1 − κe^(−κ)/(1 − e^(−κ)) = 0.632 is the
  mean of a unit exponential truncated at κ (derived). It never reads p,
  g or ŝ; in noise alone ρ = σ̂_v²/σ_v² = 1 is its only stable point
  (derived).
- **Floor** (heuristic; its bound derived): every K samples, the 10th
  percentile Q of the last 64 values of |v|² taken K apart gives
  F = Q/(2·(−ln(1 − 0.1/c))·2.5), c = 0.25; if σ̂_v² < F, σ̂_v² ← F and
  W_n is capped at 16 samples (10.67 ms); ŝ restarts only if F > 4·σ̂_v².
- **Warm-up** (heuristic): the first 0.32 s collect |v|² at K = 24; then
  σ̂_v² = Q₀.₂/(2·(−ln 0.8)) (derived for noise), ŝ² = max(0, Q₀.₉ − 2σ̂_v²),
  each counted as 0.1 × the warm-up's sample count.
- **Squelch.** p = 0 while a < a_min = 3·(T_v/16 ms)^(1/4), T_v = K/r: the
  scaling keeps noise's pass rate the same at every K (derived), the 3 is
  heuristic. A station is acquired at K = 24, so the acquisition floor is
  S₅₀₀ = −2.5 dB at every speed (derived).
- **Keying.** Down when g > +1 nat, up when g < −1 nat (heuristic
  hysteresis); never down while squelched or warming up.
- **Re-acquisition** (heuristic): after the key has been up for
  max(0.5 s, 12 dits), K returns to 24, ŝ² restarts, the frequency average
  restarts, and the speed window is set aside; if nothing is keyed within
  2 s, the old window and width come back.

### 6.4 The bank decoder (§8c; selectable, the current development focus)

Instead of one boxcar following an estimated speed, 32 boxcars run at
once, one per speed; each branch keys, times, fits and spells its own
text, and a selector publishes one branch's text, with corrections.

**Input: anchor mixing (no tracker).** Each channel block y (FS) is mixed
by the anchor Δ = f_det − f_c (Hz; the detector's current frequency for
the track minus the channel center; in oracle mode the label's frequency,
without its drift): u[n] = y[n]·e^(−jφ[n]), φ[n] = 2π·(Σ_{m≤n}Δ[m] − Δ[n])/r,
phase-continuous across anchor changes (derived: the prototype's
`anchored_baseband`). The residual offset stays in u; branch k attenuates
it by |H_k(f)|² below.

**Ladder and filters** (§8c, "The bank").

- L_k = L_1·ρ^(k−1), L_1 = β·1.2 s/100 = 9.6 ms, ρ = 1.1, β = 0.8 dit;
  K = ⌈ln(L_max/L_1)/ln ρ − 10⁻⁹⌉ = 32 branches with L_max = 192 ms (5 WPM),
  so 9.6 ms to 184.3 ms (WPM range and ρ owner; β heuristic).
- Branch k: causal unity-gain boxcar of N_k = round(L_k·r) samples (14 to
  276 at 1500 samples/s), v_k[m] = (1/N_k)·Σu[m−N_k+1 … m], zeros before
  the stream; power response |H_k(f)|² = (sin(πfN_k/r)/(N_k sin(πf/r)))²
  (1 at 0 Hz). |v_k|² (FS²) is stored rounded to float32, as the prototype
  stores it. The keyer uses the realized length N_k/r.
- Envelope likelihood as Matched's: Λ = −a²/2 + ln I₀(a·x) nats,
  p = logistic(g), g clipped to ±50 nats (numerical choice).

**Block cadence and order.** Everything advances once per block of
B = round(block_s·r) = **32 samples** (block_s = 32/1500 s = 21.33 ms,
heuristic: the engine's channel block). Within a block [n0, n1),
t = n1/r: (1) noise update, σ²_v,k read; (2) the keyer keys the block
(with the amplitude at the block's start), then updates the amplitudes;
(3) branch 1's posterior goes to the periodicity estimate; T_P, when
confident, becomes the fits' prior (weight 1); (4) per branch, in ladder
order: key changes become marks and spaces, a new over may start, or an
unknown-amplitude over start may be re-keyed or time out; (5) if branch 1
keyed up in the block, selection runs (one instant per branch-1 key-up);
(6) the selected branch's new characters are published.

#### Noise (§8c, "Noise")

The keyer needs σ²_v,k (FS², per real component) for every branch. The
default method ("spectrum"; its choice measured, prototype E10) takes the
**level** from branch 1's three-tap guard and the **ratios** between
branches from one noise spectrum of u.

- **Three-tap level (branch 1).** Matched's guard (κ = 1.75, κ_n = 4,
  m(κ) = 0.632), per block: with c accepted middle taps of mean μ (FS²),
  σ² ← max(σ² + s·(μ/(2m(κ)) − σ²), 10⁻²⁰ FS²), s = max(1 − (1−α)^c, c/W),
  α = 1 − e^(−1/(τ_n r)), τ_n = 2 s, W the count of accepted taps (starting
  at 48). Taps count only from 2N_k samples after the stream's start.
  Heuristic (Matched's values); m(κ) derived; the 10⁻²⁰ FS² (−200 dBFS)
  floor numerical.
- **Warm-up.** Until 0.32 s (480 samples) of non-zero input,
  σ² = max(Q₀.₂/(2·(−ln 0.8)), 10⁻²⁰ FS²), Q₀.₂ the 20% quantile (numpy
  "linear") of |v_k|² so far (scale derived). Matched's floor lift is not
  part of the bank.
- **Spectrum shape.** u is cut into non-overlapping segments of M = 256
  samples (T_seg = 170.7 ms, bins 5.86 Hz). **Mask:** sample u[i] is left
  out if any |v_1[j]|², j from i − R to i + N_1 − 1 + R, is
  ≥ κ_n·2σ²_v,1, with the guard margin g = 0.5·L_1 = **4.8 ms**
  (R = 7 samples; heuristic, its class seconds tied to branch 1's filter,
  owner); a segment enters only if ≥ 50% of it is kept (heuristic).
  Periodogram I_m = |DFT(u·w)_m|²/Σw², w = periodic Hann × mask;
  S ← S + max(β, 1/n_seg)·(I − S), β = 1 − e^(−T_seg/τ_n) = 0.0818.
- **Branch variances.** S̃ = S smoothed over ±25 Hz (±4 bins; heuristic);
  W_k[m] = the mean of |H_k|² over 16 points across bin m;
  σ²_v,k = σ²_v,1 · [(W_k·S̃)/b_k] / [(W_1·S̃)/b_1]. Before any segment has
  entered, σ²_v,k = σ²_v,1·N_1/N_k (exact for white noise, derived).
- **Mask bias b_k.** The mask removes mostly low-frequency power, so the
  masked spectrum reads each branch low by a factor b_k = 0.8281 (k = 1)
  … 0.7713 (k = 32), dimensionless; **measured** in white noise (ten
  seeds), valid only for the defaults at 1500 samples/s.
- **Exact zeros are missing data** (derived): an input sample exactly 0 FS
  enters no warm-up, count or history; taps count from 2N_k samples after
  the last zero; a zero sample is left out of the spectrum's mask. Until
  the first non-zero sample σ² is unknown and the channel keys, observes
  and publishes nothing.
- **Stuck-level recovery** (heuristic): if branch 1 has accepted no tap
  for 8 s (= 4τ_n) of non-zero input, every branch's σ² is set again by
  the warm-up rule over its last 0.32 s of non-zero input. Seconds, not
  dits, because noise has no keying speed.
- Variants, not the default: "spectrum-level" takes the level from the
  spectrum as well, σ²_v,k = 0.5·(W_k·S̃)/(M·b_k); "branch" runs the
  three-tap estimate on every branch.

#### Time constants in dits (§8c, "Time constants in dits")

Three time constants are stated in each branch's nominal dit
d_k = L_k/0.8 (12 ms at k = 1, 50.13 ms at k = 16, 230.3 ms at k = 32) or
each periodicity candidate's T: the re-key wait W_min,k = **16.7·d_k** of
keyed time (derived from a 0.8 s measured at 25 WPM: 0.8 s/48 ms), the
re-key time-out **2.5·W_min,k** = 41.75·d_k of channel time (heuristic),
and the periodicity windows **N_w·T**, N_w ∈ {41.7, 104, 208}
(placeholder). The owner's decision on these dits is pending; this
summary describes what the code does now, where they are the defaults
(overrides in seconds exist for ablations).

#### Keying (§8c, "Keying")

All 32 branches are keyed per block, from |v_k|² and σ²_v,k.

- a_k = √(ŝ_k²/σ²_v,k), x = √(|v_k|²/σ²_v,k);
  g = Λ(x, a_k) + ln(P₁/(1 − P₁)), P₁ = 0.44 (−0.2412 nats; derived).
- **Squelch:** open while a_k ≥ a_min,k = 3·(L_k/16 ms)^(1/4): 2.622 at
  k = 1 to 5.525 at k = 32 (scaling derived, 3 heuristic). The posterior
  sent to periodicity is p where open, 0 where closed.
- **Full-LLR keying (amplitude known):** down where open and g > h, up
  where closed or g < −h, else unchanged; h = **1 nat** (heuristic).
- **Unknown-amplitude test (at an over's start):** down at x > x_on,k, up
  at x < x_off = √(−2 ln 0.3) = 1.552 (noise alone exceeds it 30% of the
  time; heuristic release probability). x_on,k = 4.6428 (k = 1) … 4.2036
  (k = 32), **measured** for 0.01 false marks/s per branch in
  channel-shaped noise (the target heuristic). Marks keyed this way are
  provisional (up to L_k too long, derived).
- **Amplitude, known** (online EM, one step per block): w = Σp,
  m = Σp|v_k|²/w, W ← W + w, ŝ² ← max(0, ŝ² + s·(m − 2σ²_v,k − ŝ²)),
  s = min(1, max(1 − (1−α)^w, w/W)), α = 1 − e^(−1/(τ_a r)) = 1.332·10⁻³
  per sample, τ_a = 0.5 s of key-down weight (heuristic).
- **Amplitude, unknown (seed):** keyed samples are kept (at most the last
  4·W_min,k of keyed time; heuristic); ŝ² = max(0, Q₀.₉ − 2σ²_v,k), the
  90% quantile because the ramps pull the mean down (heuristic). Ready to
  re-key once W ≥ W_min,k of keyed time.

#### Duration fit (§8c, "Duration fit")

Each branch fits its marks and spaces (s; densities in ln d).

- **Parameters** θ = (T, w, qT, T_g): dit, weighting, dah, gap timebase
  (T_g > T for Farnsworth spacing).
- **Classes**, log-normal around medians: dit T + w, dah qT + w; element
  space T − w, character gap 3T_g − w, word gap 7T_g − w;
  ln d ~ N(ln μ_c, s_c²), s_c² = σ_ln² + σ_t²/μ_c², σ_ln = 0.15 (marks),
  0.25 (spaces) (heuristic). A class with μ_c ≤ 0 is dropped.
- **Priors** (derived from VE3NEA's character and word-length tables):
  marks dit 0.5716, dah 0.4284; spaces element 0.6467, character 0.2379,
  word 0.1154; each × (1 − ε).
- **Outlier:** ε = 0.05, log-uniform on 1 ms–10 s (−5.216 nats in ln d);
  durations clamped to that range (heuristic).
- **Timing resolution:** σ_t² = 2(L_k/max(a, 1))² + 2/(12r²) s² (two edges,
  each a boxcar ramp moved by noise, plus sampling; derived, first order).
- **Per observation:** ℓ_c = ln((1−ε)P_c) − ½z²/s_c² − ½ln s_c² − ln√(2π),
  z = ln d − ln μ_c; ℓ_total = log-sum-exp over the observation's classes
  and the outlier.
- **Grid and memory:** T from 12 ms in 1% steps to 242.2 ms (303 points;
  step placeholder); q ∈ {3, 4, 5}; w/T ∈ {−0.4, 0, 0.4, 0.8};
  T_g/T ∈ {1, 1.59, 2.52, 4, 6.35} (grids measured, E5). Two tables hold
  every grid point's log-likelihood (marks over (T, q, w), spaces over
  (T, w, T_g)); each observation multiplies them by λ = e^(−1/48)
  (N_mem = 48 marks and spaces, measured) and adds its own. The last 192
  observations are kept for refinement (the rest weighs e^(−4) = 1.8%,
  derived).
- **Maximum:** the score over (T, w) is the best q of the mark table plus
  the best T_g of the space table, minus π(ln T − ln T_P)²/(2σ_P²),
  σ_P = 0.1 (heuristic), when a confident T_P exists (weight π = 1).
- **Refinement:** 2 Gauss–Newton steps in ln d with responsibilities held
  per step, damping of 0.2T per parameter, clipping (w ∈ [−0.6, 1.2]T,
  qT ∈ [2, 6]T, T_g ∈ [0.8, 10]T); kept only if the weighted
  log-likelihood on the retained history does not drop (heuristic).
- **Quality** Q = Σλ^age ℓ_total / Σλ^age, nats per element. A mark is a
  dah if ℓ_dah > ℓ_dit; a space takes its likeliest class.
- **Fast evaluation:** ℓ_total is computed in one pass as
  m + ln(1 + Σe^(x−m)) (terms more than 40 nats below the largest m left
  out), with SLEEF's vectorized exp and ln (stated bound 1 ulp); this
  changes ℓ_total only in its last bits (bounds derived in §8c).

#### Periodicity (§8c, "Periodicity")

A coarse speed T_P from branch 1's keying, independent of the fits; it
feeds the fits' prior and selection's fallback, and never itself.

- **Input:** branch 1's squelched posterior p, averaged in pairs to
  r_P = 750 samples/s; recomputed every 0.25 s (heuristic).
- **Candidates:** the fit's T grid (303 points, 12–242.2 ms). Each is
  judged over its own windows of N_w·T, N_w ∈ {41.7, 104, 208} (three
  rows), N = max(16, round(N_w T r_P)) samples; a candidate whose window
  is not yet full takes no part.
- **Autocorrelation:** x = p − mean(p), biased normalized
  ρ[τ] = Σx[m]x[m+τ]/Σx² (computed per candidate with sliding sums; equal
  to the FFT form up to rounding).
- **Comb on Π = 2T** (owner, E1; 2T because consecutive edges T apart have
  opposite signs, derived): tooth(c) = mean ρ over c ± 0.075Π; score =
  mean over k = 1…4 of tooth(kΠ) − ½(tooth((k−½)Π) + tooth((k+½)Π)),
  dimensionless (teeth and width placeholders, E3). A candidate counts only
  if 9.15T ≤ (N − 1)/2 (always true with these windows, derived).
- **T_P:** per row the best candidate; T_P is the shortest row's estimate
  whose score is ≥ **0.03** (placeholder); else none.
- Variant, not the default: `periodicity_window_mode = "shared"` judges
  every candidate of a row over one window N_w·T̂, T̂ the selected branch's
  eligible fitted dit (2, 5 and 10 s without one), which adds a feedback
  from selection to T_P.

#### Text model and branch selection (§8c, "Text model and branch selection")

- **Text model:** unigram ln(w/2688) nats per character over VE3NEA's
  41-symbol table (derived); valid codes missing from it −5.817 nats, `*`
  −13.816 nats (heuristic). A branch's text score is the mean over its
  last **10** characters (placeholder), word spaces not counted.
- **Eligibility:** |ln(L_k/(0.8T_k))| ≤ ln 1.1 (one ladder step), with fit
  memory ≥ 8 elements (heuristic).
- **Best branch:** among eligible branches, those within ε_Q = 0.05 nats
  per element of the best quality Q tie (placeholder); if all tied
  branches have text, only those within 0.1 nats per character of the
  likeliest text stay tied (heuristic); the longest tied branch wins
  (better SNR). With none eligible: the likeliest text if it leads by
  ≥ 1.0 nats per character, else the branch with L_k nearest 0.8T_P, else
  branch 1.
- **Switching:** the published branch changes only after the same other
  branch has been best for **M = 4** consecutive instants, all eligible or
  all fallback picks (placeholder).

#### Channel decoder (§8c, "Channel decoder")

- **Marks and spaces:** a key change at sample n is at n/r − (N_k − 1)/(2r)
  s (group delay removed), with variance σ_t². Durations keyed by the
  known-amplitude test enter the branch's fit (re-maximized with the T_P
  prior); provisional ones enter none. Classification by the current fit;
  a character gap ends a character, a word gap adds a word space.
- **New over:** when the key has been up longer than
  T_new = max(0.5 s, 12·T_g) since the branch's last key-up (2.88 s
  without a fit; 0.576 s at 25 WPM; placeholders), the branch ends its
  character, keeps its fit as the previous over's, starts a fresh fit,
  decodes with the previous fit until the re-key, and its amplitude
  becomes unknown.
- **Re-key:** once the over has marks and W_min,k of keyed time, the
  stretch since the amplitude became unknown (≤ 20 s back) is keyed again
  with the full LLR at each candidate amplitude (the seed, and the previous
  over's if any); each candidate's marks and spaces go into a fresh fit and
  into the previous fit continued; the candidate whose fit explains the
  stretch best (mean log-likelihood per element) wins, its characters
  replace the stretch's (reason "rekey") and the keyer switches to the full
  LLR (candidate choice heuristic).
- **Time-out:** if W_min,k is not reached within 2.5·W_min,k of channel
  time, the stretch is re-keyed at the previous over's amplitude if that
  keys anything (reason "timeout"); otherwise its provisional characters
  are deleted and the count restarts. This removes noise keyed after the
  last over.
- **Fresh fit against the previous:** the fresh fit replaces the continued
  one only with ≥ 8 of the over's marks and spaces (placeholder) and a
  log-likelihood gain > ½·4·ln n nats on them (BIC form, derived; k = 4
  heuristic); the competition ends after 192 observations.
- **Switch replacement:** on a switch, the new branch's characters replace
  the published ones from the start of its character containing the time
  its eligible run began (a fallback pick: from the switch's time).
- **Corrections:** a replacement from time f at time t cuts at
  c = max(f, t − 20 s) by overlap: a published character is kept if it
  starts before t − 20 s or ends before c. It is recorded only if the text
  changes. Nothing older than **20 s** (owner) is ever changed.
- **End of stream:** the last partial block is processed, every open
  character ended and published.

#### Behind the engine (§8c, "The bank decoder behind the engine")

Each call returns the characters appended since the last call, then the
corrections in order: `from_index`, the bank's characters from it on, the
time, the reason ("switch", "rekey", "timeout" or "resync") and the reach
(≤ 20 s, except a resync). A consumer appends, then applies each
correction by keeping the first `from_index` characters and appending its
characters; the result equals the bank's text after every call (derived;
a "resync" correction repairs any difference). The *final text* has every
correction applied, the *immediate text* none. Speed, confidence (0) and
per-character probability (1) are placeholders.

**Cost.** Memory is about 9.1–14.4 MB per channel (derived from the
allocated arrays; chiefly the 21.1 s window of |v_k|² and the fits). CPU
is about 58 ms per channel-second at the defaults on the Linux test
machine, against about 0.6 ms for Matched.

## 7. Timing and latency (§9)

| Stage | Delay |
|---|---|
| Processing block (hop) | 21.33 ms |
| Detection | nothing in the first 1 s; then ~0.5–1.5 s for a new station (averaging + 0.5 s persistence) |
| Channel filter | 10.7 ms group delay |
| Matched | 0.32 s warm-up per channel; boxcar delay (K − 1)/2 samples, 19 ms at 25 WPM |
| Envelope, Matched | a character is emitted once a > 2-dit gap follows it |
| Bank | block 21.33 ms; branch delay (N_k − 1)/(2r) = 4.3–91.7 ms (removed from character times); a character is published when its following space is classified (at the next key-down), at a new over or at the stream's end; corrections reach up to 20 s back |
| Track removal | the average's decay (~6–7 s at S₅₀₀ = 20 dB) plus the 10 s timeout |

## 8. Parameters

The parameters that shape behavior; §10 of the full document has the
complete table.

| Parameter | Value | Class |
|---|---|---|
| FFT bin width | ~23 Hz (N = 8192 at 192 kHz) | heuristic |
| Detector average τ | 1 s | heuristic |
| Birth / keep-alive | 6 dB / 3 dB above the floor (dB SNR per bin) | heuristic |
| Persistence, timeout | 0.5 s, 10 s | heuristic |
| Channel distance D_ch | 47 Hz | heuristic (owner) |
| Max tracks | 200 | heuristic |
| Channel bins / r | 64 / 1500 samples/s | heuristic within derived bounds |
| Channel filter | Blackman sinc, 4097 taps, ±150 Hz at −6 dB re passband | length derived; cutoff heuristic |
| Decoder | Matched default; Envelope; bank | owner |
| Tracker τ_f, gates | 0.5 s key-down weight; weight ≥ 0.6, coherence > 0.3 | heuristic |
| Fine-tune / NCO range | ±12 Hz / ±75 Hz | heuristic (owner) |
| Matched β, start | 0.8 dit; 60 WPM (K = 24) | heuristic |
| Matched follows after | 8 marks; growth ≤ ×1.25 per mark | heuristic (owner) |
| Prior P₁ | 0.44 | derived (PARIS) |
| LLR hysteresis | ±1 nat | heuristic |
| τ_a / τ_n | 0.5 s key-down weight / 2 s | heuristic |
| κ / κ_n | 1.75 / 4; m(κ) = 0.632 | heuristic; m(κ) derived |
| Squelch a_min | 3·(T/16 ms)^(1/4) | 3 heuristic; scaling derived |
| Envelope τ_s, τ_fast, τ_slow | ¼ dit, 4 ms, 3 s | heuristic |
| Speed window | 24 marks, 5–60 WPM | heuristic |
| Bank ladder | 32 branches, 9.6–184.3 ms, ratio 1.1, 0.8 dit | range, ratio owner; 0.8 heuristic |
| Bank block | 32 samples (21.33 ms) | heuristic |
| Bank noise method | "spectrum" | measured (E10) |
| Spectrum segment, smoothing | 170.7 ms, ±25 Hz | heuristic |
| Guard margin | 4.8 ms (0.5·L_1) | heuristic (owner) |
| Mask bias b_k | 0.8281 … 0.7713 | measured |
| Stuck-level recovery | 8 s of no accepted tap | heuristic |
| x_on,k / x_off | 4.6428 … 4.2036 / 1.552 | measured / heuristic |
| W_min,k | 16.7 d_k keyed time | derived from a measurement |
| Re-key time-out | 2.5·W_min,k channel time | heuristic |
| σ_ln marks / spaces | 0.15 / 0.25 | heuristic |
| Fit memory N_mem | 48 | measured (E4) |
| Fit grids | q, w/T, T_g/T as above; T step 1% | grids measured (E5); step placeholder |
| Periodicity windows | 41.7, 104, 208 × T | placeholder |
| Comb teeth, width, confidence | 4, ±0.075Π, 0.03 | placeholder |
| Eligibility | ln 1.1; ≥ 8 elements | heuristic |
| ε_Q, text window | 0.05 nats/element, 10 characters | placeholder |
| Switch persistence M | 4 instants | placeholder |
| New over T_new | max(0.5 s, 12·T_g) | placeholder |
| Fresh fit | ≥ 8 obs, gain > 2·ln n nats | 8 placeholder; form derived |
| Correction reach | 20 s | owner |
