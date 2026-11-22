# Signal Processing Pipeline

How KZ4AP Skimmer turns a radio's I/Q samples into Morse text. It covers the stages before the decoders (sections 1–7)
and, in full, the chains under development or discussion: the segmental
decoder (section 8e) with its live operation (section 10) and the callsign stage
(section 11), and the viterbi decoder with the model the two share (section 8d). Every other chain gets a short summary that points to its
appendix section. Every value carries its unit and its class: **derived**
(it follows from the mathematics), **measured** (set by an experiment),
**heuristic** (a judgment call; "chosen by measurement" where a test set
picked among candidates), **placeholder** (kept until an experiment settles
it), **owner** (the owner's decision) or **sourced** (from a named
publication).

The **appendix** (section A) is the reference: each rule's exact form, the
derivations, provenance, limitations, the parameter table (A.10) and the
tests' and benchmark's definitions (A.11). Results and evidence are in the
results records under `docs/plans/`, engineering choices in `docs/decision-log.md`.

**Maintenance rule:** this document is brought up to date at each checkpoint
and before any merge into `main`, and may lag in between. Numbers are for the
default 192 kHz input unless stated.

## 0. Units and symbols

Nothing is calibrated to volts or dBm. Amplitudes are in **FS** (full
scale: the WAV's integers scaled to [−1, 1)), powers in **FS²**, and
absolute levels in **dBFS** (dB relative to 1 FS²; a complex tone of
amplitude 1 FS reads 0 dBFS). A signal-to-noise ratio always names the
bandwidth its noise is measured in, e.g. "dB SNR in 500 Hz". Log-likelihoods
are in nats (natural logarithms); a *neper* is a natural logarithm of an amplitude ratio. Time constants are in seconds, or in
dits where the text says so.

| Symbol | Meaning | Default |
|---|---|---|
| fs | input rate | 192 000 complex samples/s |
| N | FFT size (number of bins) | 8192 |
| Δf | bin width, fs/N | 23.44 Hz |
| hop | the step between FFTs, N/2 samples | 4096 samples = 21.33 ms |
| r | the rate of one station's channel, fs/128 | 1500 samples/s |
| dit | 1.2 s / WPM (the PARIS convention) | 48 ms at 25 WPM |
| S₅₀₀ | key-down carrier power over the noise power in 500 Hz | |
| N₀ | density of the noise, FS²/Hz: σ² / fs for white noise of σ² FS² per sample | 8.33 × 10⁻⁹ FS²/Hz for σ = 0.02 FS at 48 kHz |
| Δ | the envelope sample interval of viterbi and segmental, 4/r | 2.667 ms |
| σ_y² | noise power at their front filter's output, N₀ × 74.50 Hz | FS² |

## Overview

```
I/Q (fs) ─┬─► spectrum analyzer ─┬─► signal detector ─► tracks (frequency, SNR)
          │                      └─► hiss estimator (viterbi, segmental) ─► N₀
          │                                              │ open / close
          │                                              ▼ channels
          └─► channelizer ─────────────────────► one stream per station, 1500 samples/s
                                                         │
                                                         ▼
                                                decoder per channel ─► text
                                 (segmental in the app; Matched; Envelope; bank; viterbi)
                                                         │
                                                         ▼
                                                callsign stage ─► list of calls
```

The input is the radio's whole band, 192 kHz wide, or a receiver's audio made analytic (section 1),
its DC removed on segmental's path. The **spectrum analyzer and detector** look at it every 21.33 ms
(segmental's detector: 85.3 ms) and make each station a *track* with a frequency; for each track the
**channelizer** cuts a stream about ±150 Hz wide at 1500 samples/s, and a **decoder** per channel
turns it into characters (five decoders, section 8). Matched
and the bank are given the detector's frequency (the *anchor*); viterbi's and segmental's channels
follow it (section 7) and get N₀ from a hiss estimator reading the spectrum.

## 1. Input

A stereo WAV, I left, Q right: b-bit integers (b = 16, 24, 32) over
2^(b−1), in [−1, 1) FS, or 32-bit floats. Outside the bench
other rates go exactly to the largest decoder rate below (250 to
192 kHz: × 96/125, flat to ±86 kHz); `--mirror` conjugates a mirrored receiver's samples. On
segmental's path a 10 s DC blocker comes first (section 6). Frequencies are offsets from the
center (Hz); the center (`--center-khz`, header or file
name) only labels them (A.1).
`kz4ap-listen` makes one frequency audible (A.13); `kz4ap-fading` measures a station's fading (A.14).

**Receiver audio** (a sound card's: a one-channel WAV, or `--audio`). One channel, resampled to
48 kHz (derived: the decoders' lowest rate) as above, is filtered to its analytic signal, h(t) = w(t)[sinc t + j(1 − cos πt)/(πt)]
(1240 taps, Kaiser, 82 dB re the passband, 200 Hz transitions at 0 Hz and 24 kHz; heuristics), delays taken out exactly:
a cos 2πft becomes a e^(j2πft). Frequencies are audio tones (Hz); a
full-scale sine reads 0 dBFS. The receiver's **passband**, default **300–3000 Hz** (heuristic),
bounds the detector's floor and peaks and the hiss estimator's bins: outside it lie near-silence and
no station. With dial F and pitch p (600 Hz, heuristic), RF = F + (f − p) on the upper sideband;
on the lower, the signal conjugated and the passband negated, F − (f − p). SNRs are receiver-audio values (A.1).

## 2. The FFT size N and bin width Δf

N is the largest power of two that keeps the bins at least 20 Hz wide:
8192 at 192 kHz, so Δf = 23.44 Hz and each transform spans 42.7 ms. A
heuristic: narrow enough to separate stations a few tens of Hz apart, wide
enough that most of a keyed signal's energy falls within one to three bins
up to about 40 WPM (derived).

Appendix: A.2.

## 3. Hops and 50% overlap

Input arrives in blocks of N/2 samples (the hop); each FFT covers the
newest block and the one before it, so consecutive FFTs overlap by half. With
the Hann window (section 4) the overlapping windows add up to a constant
(derived).

Appendix: A.3.

## 4. Spectrum analyzer

Every hop the analyzer takes the last N samples, multiplies them by a Hann
window and computes an FFT. The power in bin k, normalized so that a
complex tone of amplitude A on a bin reads A² FS², is

  P[k] = |X[k]|² / (Σw)²  (FS²),

with Σw = N/2 the window's sum. White noise of σ² FS² per sample reads as
the noise in one *equivalent noise bandwidth* (ENBW), 1.5 bins = 35.2 Hz
(derived).

Appendix: A.4.

## 5. What "SNR" means here

**S₅₀₀** is the key-down carrier power over the noise power in 500 Hz, the
convention of CW Skimmer and the Reverse Beacon Network. The synthetic
recordings add complex white noise of σ = 0.02 FS. Per bin the noise bandwidth is one ENBW, 35.2 Hz
(8.79 Hz on the detector's fine bins), so a carrier reads
**11.5 dB** (17.6 dB) higher as an SNR per bin than as S₅₀₀ (derived, white
noise; a keyed signal less, A.5).

Appendix: A.5.

## 6. Signal detector

The detector's form on the segmental path,
the app's, follows; the other decoders keep their own (A.6). Values are
heuristics unless marked.

**DC blocker.** First a radio's DC offset leaves the input: each sample less
a one-pole average of the samples, time constant 10 s, a notch at 0 Hz,
−3 dB re the passband at ±0.016 Hz (derived). The channels read the
blocked input too.

**Its own spectrum.** The detector reads a spectrum four times finer than
the band's (section 4): Hann-windowed FFTs of 4N points, bins of
**Δf_d = 5.86 Hz**, a window of 170.7 ms, a frame every 85.3 ms (on 23.4 Hz
bins a station's main lobe reaches ±47 Hz).

**Averaging and floor.** Each bin's power is averaged with a one-pole
average of **τ = 1 s**, in linear power. The floor is the median of all
averaged bins (receiver audio: the passband's), every second frame.

**Finding a station.** A bin is a candidate when its average stands
**7 dB** above the floor (dB SNR per fine bin), is the highest within
±12 Hz (the main lobe's half width), and passes three tests (prominence
chosen by measurement):
- *prominence:* where a higher bin lies within ±150 Hz, the lowest bin
  between them is 6 dB below the candidate;
- *keying sidebands:* a keyed carrier's spectrum is symmetric about it
  (derived): a peak whose mirror about a bin 10 dB above it, within
  ±150 Hz, stands within 3 dB as high is a sideband;
- *local floor:* 7 dB above the lower quartile of the bins within ±1 kHz,
  which a wide signal's peaks are not.

It must be a candidate in every frame, moving at most 6 Hz a frame, for
**0.5 s** before a track is born; nothing is detected in the first second.
A peak within **D = 30 Hz** of a track belongs to it. Its frequency: a
parabola through the peak bin and its neighbors (dB).

**Following a station.** Each frame a track moves to the strongest
qualifying peak within 12 Hz of where it is, or stays. It does not move to a peak within D of an older track; of two that
close the younger dies. So a station 30 Hz or more from another has its
own channel, and an answering station closer shares one. A track stays
active while its level, the highest bin within ±6 Hz, is at most 3 dB
below the threshold, and dies after **10 s** inactive. At most **400**
tracks; a stronger newcomer replaces the weakest. A variant, off: no track within
30 Hz × 10^((ΔL − 12 dB)/20) of one ΔL dB stronger (A.6).

**Copies of a neighbor.** A channel ±150 Hz wide hears a strong station
tens of Hz away; its own station silent, or none, its decoder prints
the neighbor's text: a second row of one station. Once a second each track
W is compared with each older, proven track S within 150 Hz and **15 dB**
or more above it at the detector. e is a decoder's mean envelope power at
its leading filter per 21.33 ms block (FS²), x the last **6 s** of it less
its 0.5 s moving mean: ρ = Σx_W x_S / √(Σx_W² Σx_S²), β = Σx_W x_S / Σx_S².
W is S's copy, retired with its place open, when ρ ≥ **0.5**,
β ≤ 1 and W's own power (ē_W − E)/E, E = β ē_S + σ_W² (leak and hiss), is
at most **0.25** (chosen by measurement; the forms derived, A.6).

**A neighbor's text.** A track not a copy still prints a neighbor's over
between its own (a caller's, its runner's exchange). Each symbol of W's but a word
space is compared with each track S within 150 Hz, over its span and **0.5 s**
either side, x, ρ, β, own as above: it is S's (`heard_from`) when ρ ≥ 0.5,
Σx_S² > Σx_W² (S hears it louder; of several, the loudest) and own ≤ **0.5** (chosen
by measurement; 0.5 s, ρ heuristics). A stretch of such symbols leaves W's row
only where S's row holds it whole within 25 ms (section 10).

**Oracle mode**, for the benchmark, skips the detector and opens a
channel at each labeled frequency.

Appendix: A.6.

## 7. Channelizer: one stream per station

For each track, the channelizer produces the narrow stream its decoder
reads: in effect it shifts the station to 0 Hz, low-pass filters, and keeps
one sample in 128: **r = 1500 samples/s**.

- **Center.** The channel is centered on the track's birth frequency
  rounded to the nearest bin, so the station starts within ±11.7 Hz of
  0 Hz. For Envelope, Matched and the bank the channel never moves; the
  decoder sees no signal from before the track's birth.
- **Filter.** A linear-phase low-pass of 4097 taps (21.3 ms; the longest
  the shared FFT allows, derived), cut off at **±150 Hz** (−6 dB relative to
  the passband; heuristic, sized for keying up to about 60 WPM). It is
  within 0.015 dB of the passband to about 39 Hz and at least 74 dB
  below the passband beyond about 279 Hz. Its noise bandwidth is 252 Hz;
  its delay 10.7 ms.
- **Computation.** One FFT of the band per hop serves all channels; each
  takes its 64 bins, applies the filter's response and inverts them
  (overlap-save), equal to shifting, filtering and decimating up to
  rounding (derived).

**Re-centering (Matched only):** within ±12 Hz of the detector's frequency
(A.8b).

**Following the detector (viterbi and segmental only).** The channelizer
follows the detector's frequency for the track,
f_det (oracle channels: the label's, drifting or switching at a QSO's overs),
so the decoder receives its station near 0 Hz (owner).
Two steps, before each
32-sample block: the block, cut at bin f_bin, is multiplied by a
phase-continuous oscillator at −(f_det − f_bin), f_det held over the
block (21.33 ms); then, if f_det lies more
than half a bin (11.72 Hz) from f_bin, the channel moves to the bin
nearest f_det from the next block on, the oscillator's phase adjusted so
that the station's phase does not step (derived). A 1 Hz/s drift is followed
about 1.0 Hz behind (A.7).

Appendix: A.7.

## 8. The decoders

### Which decoder runs

The bench's `--decoder envelope|matched|bank|viterbi|segmental` picks one; **Matched is its default**
(owner), segmental the app's.

### Envelope (not the default)

The milestone-1 baseline, heuristic: the smoothed magnitude against 60% and 40%
of the way from a followed space level to a mark level. Appendix A.8.

### The shared back end (Envelope and Matched)

It turns key-down and key-up times into characters: a mark over 2 dits is a dah;
a space over 2 dits ends the character, over 5 dits the word (heuristics), the
dit re-estimated from the last 24 marks. Appendix A.8.

## 8b. Matched decoder (the bench's default)

A moving average 0.8 dit long, its length following the decoder's own speed
estimate (a feedback loop), gives each sample a Rician log-likelihood ratio; the
key goes down above +1 nat of log-odds and up below −1 nat (heuristics), then the
shared back end makes characters. Appendix A.8b.

## 8c. Bank decoder (selectable; frozen as the reference)

No speed loop: 32 moving averages of 9.6 ms × 1.1^(k−1) (k = 1 … 32; 100 to
5 WPM; owner), each *branch* keying its own text with a Rician likelihood; a
selector publishes the best-fitting branch, correcting text up to 20 s back
(owner). Frozen at its defaults as the reference. Appendix A.8c.

## 8d. The story model and the viterbi decoder (selectable)

**One model, two decoders.** `viterbi` (`FrontEnd::Viterbi`) and
`segmental` (section 8e) share a channel, a
filter, a generative model of Morse and a score per sample. They differ at
the carrier's amplitude, which sets their cost. A *mark* is key-down, a *gap*
key-up, an *element* either; an *over* is one continuous transmission; a
*story* is one account of the channel since it opened: silence, or an over
at a speed, at a node of the Morse tree, with its own text.

**N₀, the hiss.** Each bin's power is averaged as the detector does (one-pole,
1 s; heuristic). Of the 171 bins within ±1 992 Hz of the channel (receiver audio: within its passband) the 43rd
smallest, the 25th percentile (placeholders), is divided by its bias on noise,
β (0.288 after one frame, 0.927 in steady state; derived), and by one bin's
ENBW, 35.16 Hz (derived). A frame of exact zeros is not counted. The engine
gives N₀ before each channel block; while N₀ = 0 samples are skipped.

**Front filter and envelope samples.** The followed channel stream (section 7)
passes a 25-tap filter, y[n] = Σ h[i−12] x[n−i], matched to a 100 WPM dot:
12 ms with raised-cosine edges of 2.5 ms 10–90% rise, 4.235 ms long (owner;
taps derived), unity gain at 0 Hz, delay 8.000 ms. Every 4th channel sample
gives e_k = |y| (FS), one per Δ = 2.667 ms (owner). On noise, σ_y² = N₀ ×
74.50 Hz, and neighboring samples correlate at ρ₁…₅ = 0.878, 0.626, 0.367,
0.138, 0.014 (derived from the taps).

**The template.** A story claims each edge at the first sample at or after it.
After a key-down claimed at sample i, sample i + j holds s_j of the amplitude:
under 8.3 × 10⁻⁴ through j = 4, then 0.051, 0.213, 0.433, 0.659, 0.866, 0.983
(j = 5 … 10), 1.0023 at j = 11, exactly 1 from j = 16 (derived from both
filters, averaged over where the edge falls within Δ). A key-up gives 1 − s_j.
v_j is the mean-square spread about s_j over that position and over transmitter
rise times uniform on 0.8–4.7 ms (ARRL Lab's range; owner; derived).

**A sample's score.** A carrier of amplitude a under template s explains e
against noise alone by the Rician log-likelihood ratio (standard):

  Λ(a; e, s) = z²(1 − 1/v) − r²/v − ln v + ln I₀(2rz/v)  (nats),
  z = e/σ_y, r = |s|a/σ_y, v = V/σ_y², V = σ_y² + a²(f²s² + v_j).

f, the *systematic floor*, is a relative amplitude error for what the model
omits (chirp, ripple, flutter), added in quadrature to the noise (heuristic).
Correlated noise is counted once by tempering the evidence with w = 1/(1 +
2Σρ_m) = 0.1987 (derived); the prior is not tempered. A sample with s = 0
scores 0 nats, whatever it holds.

**The prior.** A story's ln P adds, in nats:

- *Silence:* ln(1 − P) per sample and ln P = −8.92 at a birth, P = 0.05 s⁻¹ ×
  Δ = 1.333 × 10⁻⁴ (placeholder).
- *Speed:* dots T_s = 12 ms × 1.1^s, s = 0 … 31, 100 to 5.2 WPM (owner), each
  taken at ln(1/32) = −3.466 at an over's first mark's end. Viterbi's spacing is
  standard: no Farnsworth factor (owner); segmental adds a gap stretch (section 8e).
- *Lengths:* log-normal about 1, 3, 1, 3 and 7 dots (dot, dash, element gap,
  letter gap, word gap), σ = 0.3 in ln(length), rounded to whole samples
  (placeholders, ITU ratios). An element that continues pays ln S(τ), the log of
  surviving τ samples; one that ends pays ln P(τ).
- *Text:* a path in the tree of 62 symbols. A symbol costs ln(1/p): ln 62 =
  4.127 at its start, ln p + ln 62 at its end. p is its share of VE3NEA's on-air
  counts, floored at 0.002 before normalizing (the table sourced; the floor
  heuristic; `symbol_prior_on_air` off gives 1/62).
- *After a symbol:* letter gap 0.396, word gap 1.161, over's end 4.296, from
  1/3.06 that a letter ends its word (VE3NEA) and 1/24 that a word ends its over
  (development set; placeholders).
- *Rescue:* at a letter's start the sender may take a new speed, with
  probability ε = 10⁻³ (−6.908; placeholder); the text goes on.

The search is a maximum a posteriori search over stories, the best text
published when the channel ends.

**Viterbi's amplitude and search.** Each story holds its own Gaussian amplitude, scored by
quadrature per sample; segmental removes that cost. Appendix: A.8d.

## 8e. Segmental decoder (the app's default)

**The idea.** All stories on a channel share one belief over the level, so a
sample's evidence no longer depends on the story, and a story is touched only
to offer its ends. For a sample of a claimed mark or edge,

  score_k(story) = ln Σ_l P̂_t(k)(l) exp(wΛ(a_l; e_k, s_k(story))),

0 for key-up; P̂ is a belief per frame t, not per story. Prior and Λ are section
8d's, with f = 0.1 (chosen by measurement). `--decoder segmental`,
`FrontEnd::Segmental`; code in `engine/src/segmental/`.

**Two streams.** A channel runs stages 1 to 6 at *filter scales* m = 1, 3
(heuristic by measurement), one *stream* each; no story crosses between streams,
and stage 7 chooses whose text goes out. A front filter matched to 12 ms leaves a
neighbor within about 60 Hz in the wanted station's gaps. Scale m multiplies by m
the filter's dot, Δ and the fastest dot; the slowest stays within a step:

| m | Filter dot | Taps | σ_y²/N₀ | Δ | Speeds |
|---|---|---|---|---|---|
| 1 | 12 ms | 25 | 74.50 Hz | 2.667 ms | 32, to 100 WPM |
| 3 | 36 ms | 67 | 26.0 Hz | 8 ms | 20, to 33 WPM |

Taps and bandwidths derived; m = 2 selectable; edges at m = 3 E = T/4 = 9 ms (heuristic). Below, m = 1's values; others: A.8e.

**Stage 1: channel, filter, samples.** As section 8d. No sample is pushed while N₀ = 0.

**Stage 2: the channel's floor.** A key-up sample scores 0 at whatever noise it is told; told 4.8 dB or more below the channel's
real floor, a story earns by bridging gaps at a low level (measured), and the hiss reads quiet bins ±2 kHz around, as behind a
receiver's passband. So, per sample,

  σ² = max(N₀ B, 10^(0.3) L, Q₀.₁₀(e²) / 0.10536 × 10^(−0.3)) (FS²), B = 74.50 Hz,

Q₀.₁₀ the 10th percentile of every 4th sample's e² (10.67 ms, ρ₄ = 0.14) over the
last 5 s, retaken every 96 ms, first used 1.056 s in. On noise e² is exponential,
so 0.10536 = −ln 0.9 is derived; the −3 dB margin (re the floor as estimated),
quantile, window, stride, refresh and start are heuristics.

*A neighbor's leak* in the wanted station's gaps would score as keying, so σ² is also at least +3 dB re

  L = Σ_k (P_k / 1.5) |H_ch(kΔf)|² |H_f(kΔf)|² (FS²),

its peak over 0.2 s (9 blocks), per stream; P_k the spectrum's bins |k| = 3 to 14 (70.3 to
328.1 Hz), each live block, not in a replay. 1.5 (a tone over its Hann bins) and the responses are
derived; +3 dB re the leak and 0.2 s by measurement; 60 Hz, 320 Hz heuristics. A strong station's own keying, spread by the
analyzer, reads as such a leak (noise only, stage 3); a variant subtracts it (A.8e).

*The channel's tail* (text-fast-1). A delayed or scattered path delivers a mark's power after its fall (the owner's 80 m clips:
−10 to −15 dB re the mark for 40 ms, incoherent). Each stream learns g_m, the power m samples after a claimed fall
re the mark's: for each run above +10 dB re F with median M ≥ +20 dB re F, the mean of (e² − F)/M after its last sample ≥ M/4 (the
template's half point, h = 7; derived) until the next run, read at m − h, memory 200 runs, from 30. On if g₁₁ ≥ −15 dB re the mark;
kept to −25 dB or 48 ms (heuristics).

**Stage 3: the level filter.** A hidden Markov model over key (up, down) × level:

- *Grid:* −6 to +84 dB S₅₀₀ re the floor F = max(N₀B, channel floor), 1.5 dB steps, 61 levels
  (heuristic), a_l²/F = S₅₀₀ × 500 Hz / 74.50 Hz (derived). A leak floor above F is noise only: σ² is
  taken at F × 10^(0.15d), d whole steps (nearest, at most 40), level l scored as l − d, and below −6 dB
  S₅₀₀ re σ² as −6 (heuristic).
- *Key:* flips with 30 s⁻¹ × Δ = 0.08 per sample (chosen by measurement), with no timing.
- *Level:* constant over a frame of 8 samples, 21.33 ms (heuristic). Between
  frames ln a walks with variance 4.8² nepers²/s × 21.33 ms = (0.70 nepers)²
  (chosen by measurement), as 25 passes of (p, 1 − 2p,
  p), p = 0.3296, ends reflecting; and jumps anywhere with probability 0.05 s⁻¹ ×
  21.33 ms = 1.07 × 10⁻³ (heuristic). A variant, off, widens the walk linearly in dB from +20 to
  +40 dB S₅₀₀ re F, to a set value (A.8e).
- *Emission:* key-up 1; key-down exp(x_k(l)), x_k(l) = wΛ(a_l; e_k, 1), with f′² =
  f² + (0.1727 nepers)²/12, f′ = 0.1117: the second term is the grid's own variance
  (derived).
- *Belief:* the forward message runs every sample; the backward message, from flat,
  over the last 16 frames every 8. P̂_t(l) ∝ (forward into frame t) × (backward
  into t's end), key summed out of each: extrinsic of the frame's own 8 samples. Commit lag 170.7 to 338.7 ms.
- *Walk set by the Doppler spread* (text-fast-1): on the widest stream's complex y, within each stretch of a run at or above −10 dB
  re its median, 8 ms inside its ends, |ρ| = |Σ y_(k+τ)y*_k| / √(Σ(|y_k|² − F) Σ(|y_(k+τ)|² − F)), τ = 21.33 ms, memory 100 runs;
  f_D = 2σ_D from |ρ| = exp(−2π²σ_D²τ²) (Gaussian spectrum; derived). From 3 Hz (back under 2.5 Hz), runs at +10 dB S₅₀₀ or more and
  ln |ρ(τ)| ≥ 2.5 ln |ρ(τ/2)| (a spread's 4; a beat's less),
  every stream walks 1.4 f_D nepers/√s (f_D in Hz; 4.8 to 20) by a Gaussian matrix over 10.67 ms frames (8 ms at m = 3; 21.33 ms
  where a tail is modeled). Heuristics by measurement.

**Stage 4: evidence and running sums.** Each committed sample yields L1_k =
ln Σ_l P̂(l) exp(x_k(l)), over levels with P̂ ≥ 10⁻⁴ (heuristic), and, for the
template's values m = 5 … 10, R_k[m] and F_k[m]: the same with s_m and with
1 − s_m, and v_m, over at most 8 groups of levels (heuristic). Then C[m] =
Σ_(k<m) L1_k, rise(i) = Σ_m R_(i+m)[m], fall(j) = Σ_m F_(j+m)[m], and a mark from i
to j scores

  rise(i) + C[j+5] − C[i+11] + fall(j) (nats).

The template is taken as 0 below m = 5 and 1 from m = 11 (derived approximation:
|s₄| = 8 × 10⁻⁴, s₁₁ = 1.0023). It holds for elements of at least 6 samples (16 ms,
a dot at 75 WPM), whose windows do not overlap. With a tail, F_k[m] adds g₁₁ to v_m, and each sample yields, for a fall claimed at
k − 11 − q, T_k[q] = ln Σ P̂ exp(w(z²ρ/(1 + ρ) − ln(1 + ρ))), ρ = g_(11+q)a²/σ²: noise plus a zero-mean Gaussian of the tail's power
(derived).

**Stage 5: the search.** Boundary j is searched once evidence through j + 16
exists. An open element holds a *base*:

- A mark at speed s from node n, started at i: base = (score at i) + rise(i) −
  C[i+11]. At j it offers to end as dot and dash, into a gap at the child node:
  base + C[j+5] + fall(j) + ln P_label(s, j − i).
- A gap: base = the score at its start. It offers an element gap (a mark at the
  same node), a letter gap and a word gap (a mark at the root, paying the
  letter's end, the next one's start and the gap's cost). With a tail of n positions a gap from i to j adds Σ T[q], q < min(n,
  j − i − 6), before the next rise's window; its reach likewise.
- Offers merge by (j, speed, node, kind), the best kept: exact given the shared evidence, the next element depending on nothing else.
- Lengths end within e^(±4σ) of the median (heuristic).
- *Fist:* the `s` above is a pair (speed, gap stretch g): letter and word gaps have
  medians 3gT_s and 7gT_s; marks and element gaps stay at T_s (Farnsworth; ARRL's
  rule stretches both gaps alike). g = 1.61ⁿ, n = 0 … 6, to 17.4, while gT_s ≤
  0.7 s (step and limit heuristic; A.8e). A story takes it with its speed, to the over's end, at ln 0.998 for g = 1 or
  ln(0.002/6) = −8.0 nats for another (chosen by measurement).

*Short elements.* Below 6 samples two edges overlap. The whole element is then scored with its pair's template, s_m − s_(m−L)
(a gap: 1 minus that), variance v_m + v_(m−L) − 2c[m][m−L] (derived), only when a story can end one.

*Pruning.* Stories are compared on their reach through H = j + 16. To its base
and its survival so far, a gap adds max(0, max_d [rise(j+d) + C[H+1] − C[j+d+11]])
and a mark max(C[H+1], max_d [C[j+d+5] + fall(j+d)]), d from max(1, 6 − age) to 6:
computed once per boundary. Beam 15 nats; ceiling 1 500 elements, a stretched fist's ranked +4 nats; no element opens in a state
(kind, fist, node) where one opened within T_s/8 stands as high. All heuristics by measurement (A.8e).

**Stage 6: over's tail, births, rescue.** An over's end at j costs the letter's
end and 4.296 nats and reaches the no-signal story at j + 6 (j + n + 6 with a tail of n positions, adding all n), paying ln(1 − P)
a sample, so that a birth's rise window cannot overlap the fall's or the tail's. A birth opens a mark of
open speed; its end offers every speed at ln(1/32). A rescue opens the same from
the best story starting a letter at that boundary, paying ln ε for ln(1 − ε), text
kept. If nothing stands, the no-signal story restarts at the best score and text.

**Stage 7: text.** Every 0.25 s of stream the best story's last 64 symbols are
compared with what has gone out (heuristics): new symbols, or a correction; a letter
still open waits. At the channel's end the whole text is compared. Probability 1 per character (a
placeholder). A variant, not the default,
keeps a margin per symbol from the merges' runners-up (A.8e, "Margins").

*Speed* (the list's): the median character speed of the last 16 letters (A.9b).

*Which stream.* The stream with the largest S − F since 1.5 s of age leads (S its best story's score, F a free on-off key's, 5
switches/s); a narrower one needs half the widest's symbols or 100 nats, and 120 nats above the leader, and from 10 s is dropped if
ineligible or 30 nats behind. The widest stays; the judge reads it (heuristics; A.8e).

Scores are not probabilities: a MAP search under a real prior, the likelihood approximated
(A.8e, "What a score is").

## 9. Timing and latency

| Stage | Delay |
|---|---|
| Processing block (hop) | 21.33 ms |
| Detection | nothing in the first 1 s; then about 0.5–1.5 s for a new station |
| Channel filter | 10.7 ms |
| Viterbi | the whole text when the channel ends; front filter 8.000 ms |
| Segmental | text every 0.25 s, corrected as it firms up; the evidence lags at most 0.387 s at m = 1, 0.46 s at m = 3; the final text's error rate is reached about 0.75 s behind (measured at m = 1 alone; A.8e) |
| Track removal | about 6–7 s for the average to decay (S₅₀₀ = 20 dB), plus the 10 s timeout |

Other decoders: A.9.

## 10. Live operation

The app, `kz4ap-skimmer`, runs segmental by default. All below holds on its detector path, not in
oracle mode; other decoders keep the detector their results were measured with. Values are heuristics unless marked. Appendix: A.9b.

**Pipeline.** A source fills a ring of at least 4 s, 5.46 s at every radio rate
(power-of-two capacity, derived). On overflow the ring empties at once: one gap.
One thread runs spectrum, detector and blocks; `worker_threads` (half the machine's)
run the channels; output does not depend on their number (A.9b).

**Detector:** section 6.

**Replay.** A new channel's decoder first reads the last 2 s of the band's kept block
spectra (94 blocks), then goes live; its symbols carry their true
times. At most 100 replays start per second of stream (2.13 per block), by track id;
the rest wait undecoded. With a track budget in force, none replays.

**Judge,** once per second of stream, on the decoder's evidence over W = 12 s: best
story's score S and a free key's F (two-state chain, 5 switches/s, same evidence):
R = (ΔS − max(0, ΔF)) / W (nats/s), E = ΔS / W. Text sums, each symbol ln(q/p) against
the on-air prior p, floored at −12 nats: *tiny* (q fitted to real junk) and *garbage*
(q = 1/62). *Proven*: tiny has reached −6 nats and garbage never +6 nats. Rules in order:
from 4 s, R < 3 nats/s and tiny ≥ +8 nats or garbage ≥ +6 nats: retire; from 12 s, R < −8 nats/s
and (unproven, or E ≥ 1000 nats/s with −R > 0.1% of E): retire; unproven with R < 3 nats/s: retire if under
5 nats/s over 8 s, or at 14 s; proven: under 1 nat/s over 30 s retires, place left open.
A copy (section 6) retires first, its place left open.

**Holds.** A retired place (±D) closes 8 s, doubling to 120 s; 4 such retirements
within 750 Hz in 60 s close ±750 Hz from 30 s, likewise. A repeat is a retirement after
the hold before it ended. The first retirement at a place leaves it open: a station
read badly at first, or listening, gets a second track.

**List.** A row is listed once proven by the same test; a new track within 30 Hz of a
row ended under 60 s ago goes on with it. Rows keep about 4 000 symbols. A row leaves out
what it heard from a listed neighbor's row (section 6), a word space in its place (A.9b).

**Overload.** A pipeline more than 1 s behind keeps ¾ of its tracks, at least 8, those
with the least evidence closing first (A.9b). A radio silent 5 s is flagged.

## 11. Callsign stage

`CallSearch` reads any decoder's tracks and text; values are
heuristic unless marked (A.12).

**Words.** Each stream-second it reads text 10 s old; changes within
200 s reread, old readings kept. A word space, prosign or punctuation ends a word;
a neighbor's symbols are read too.

**Readings.** A call's form (RR 19.68; sourced): an allocated prefix of one or two
characters (three: 3DA0), one digit, one to four letters (US: one to three). Only listed:
four letters, lone prefix digit (C6AGU), CW words (N1MM). 1 point: a whole word, or glued to a keyword or
itself. 0.75: short words joined (K X 9 F), not periodic text. 0.5: a stray E, T, I
before or E, T, K after, or an X before (TU run on: XW3RJ). A Viterbi cuts unsplit words
(DEK8DXK8DXK8DXK); a call there (1 point) needs a repeat (not after a keyword it joins), a DE
before, or DE after and K; an
unplaceable last E, I or K gives both, 0.5 each.

**Role.** Before DE or 5NN: *named*. After DE, CQ or TEST, or before K: *signed*. Else *bare*.

**Evidence.** A reading overlapping one in time within 300 Hz only betters it. *Counted* points add
over the band within 30 min, a transmission once,
none where a better variant holds it. Reported at 2 points, named words
too; listed, 1 if framed (signed, named or sent twice), else 2
(variant: 1, a high-margin word). Only E T I S H A N M U 5: 2 whole words, listed or not.
Unlisted, a 1x1 (N4A) needs 3 points, 2 if a counted reading is signed; a cut number's shape (2T7A), 5 and 2.

**Variants.** A call within edit distance 1 of a better-supported call within 150 Hz, or a
prefix of one or vice versa, is held back there; distance 2 (4 characters or more, or K1T and K1BX) needs over twice the points. Ties at
distance 1: the earlier; beginning within 1 s, the later. Listed calls weigh triple,
not an X stripped. A transmission read whole and joined to letters after it (K4VR T) counts
for the longer, if it has 1.5 points besides (variants: always, or neither). A row
leaves when its counted points fail.

**List.** One row per base call, at its latest signed, else bare, else named place.
*Signed*: some reading signed, counted or not; listable alone.

# A. Appendix: derivations, provenance, limitations, parameters, definitions

Reference material, organized by the body's sections. Under each section:
**derivations** (the arguments that justify a value or a form, complete),
**provenance** (for each measured or heuristic value: what set it, under
which conditions, where its results are, and when it stays valid), and
**limitations** (known limitations and defects as stated, not fixed).
A.10 is the full parameter table, A.11 the definitions used in tests and
the benchmark. Results, measurement narratives, port checks and test
evidence are in the results records named at the top of this document
("2a record", "stage-1 record", "Plan A record", "Plan B record" below);
passages moved there from this document on 2026-10-05 are in each record's
section titled "Moved from signal-processing.md, 2026-10-05".

## A.0 Units and symbols

### Exact form

The rules of this section as the code implements them (moved verbatim from the body on 2026-10-05, when the body was rewritten for reading).

Nothing is calibrated to volts or dBm. Linear amplitude is in **FS** (full
scale: the WAV's b-bit integers divided by 2^(b−1)), power in **FS²**,
absolute levels in **dBFS** (dB relative to 1 FS²; a complex tone of
amplitude 1 FS reads 0 dBFS). An SNR always names its noise bandwidth (dB
SNR in 500 Hz; dB SNR per bin = in 35.2 Hz). A dB value that is neither
names its reference ("relative to the passband"). Log-likelihoods are in
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


**Units, in full.** The engine has no physical calibration: nothing
relates its numbers to volts or dBm at the antenna.

- **FS (full scale):** linear amplitude unit. The input WAV holds b-bit
  integers (b = 16, 24 or 32); the engine divides them by 2^(b−1), so I and Q
  each lie in [−1, 1 − 2^(1−b)] FS: 1 FS itself is just beyond the largest
  positive value the file can hold. A 32-bit float WAV is taken as stored,
  in FS, and may exceed 1. The WAV is the SDR software's output, not raw
  ADC counts, and the gain from the antenna to it is unknown.
- **FS²:** power unit (squared amplitude, e.g. |x|² of one complex sample).
- **dBFS:** power in dB relative to 1 FS². 0 dBFS is the power of a complex
  tone of amplitude 1 FS (|x| = 1 at every sample). All absolute dB levels in
  the engine (spectrum frames, averages, the noise floor) are dBFS.
- **dB SNR:** a power ratio, which only means something with the bandwidth
  the noise was measured in; this document always states it (section 5).
- dB values that are neither (a filter's response, a window's sidelobes) name
  their reference: "relative to the passband" or "relative to the main lobe".

**Symbols.** Each is also defined where it first matters.

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
| e[n] | Envelope decoder's smoothed envelope, FS | |
| M, S | Envelope decoder's mark and space levels, FS | |
| channel_bins | FFT bins kept per station channel | 64 |
| D | decimation factor, N / channel_bins | 128 |
| r | channel (decoder input) sample rate, fs / D | 1500 Hz |
| dit | duration of one Morse dit, 1.2 s / WPM (PARIS timing) | 48 ms at 25 WPM |
| j | exponent in "48 kHz × 2^j" (j = 0, 1, 2, …): the rates 48, 96, 192, 384, 768 kHz | |
| K | Matched filter (boxcar) length, round(β·dit·r), samples | 58 at 25 WPM |
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

**Dit-like quantities** (moved from the body on 2026-10-08). Several
dit-like quantities appear in the bank decoder (section 8c, A.8c); they are
different things:

| Symbol | What it is |
|---|---|
| dit | the generic dit of a speed, 1.2 s / WPM |
| d_k | branch k's nominal dit, fixed: L_k / 0.8 (12 ms on branch 1 to 230 ms on branch 32) |
| T | the dit a branch has fitted to its own marks and spaces (the duration fit) |
| τ_c | a candidate dit tried by the periodicity estimator (303 values, 12 to 242 ms) |
| T_P | the periodicity estimator's result: the best confident candidate |

## A.1 Input

### Exact form

`WavIqReader` (`engine/src/wav_reader.cpp`) reads RIFF/WAVE with 2 channels:
format tag 1 (integer PCM) of 16, 24 or 32 bits, tag 3 (IEEE float) of 32
bits, or tag 0xFFFE (WAVE_FORMAT_EXTENSIBLE) whose sub-format GUID begins
with 1 or 3. The block align must be 2·b/8 bytes. Integers are little-endian
two's complement; a 24-bit value is placed in the top three bytes of a
32-bit word and shifted back arithmetically, then every integer is scaled by
2^−(b−1). The extensible header's valid-bits field is not used, since valid
bits are left-justified in the container. The data chunk's length is cut to
what the file holds.

**The center and the time.** The center is `--center-khz` if given (the app
and `kz4ap-calls`), else the header's `auxi` chunk, else the file name; the
`auxi` start time, when present, dates the call list (`first_utc`,
`last_utc`) and the app's report (`stream_start_utc`, with `center_from`).
Nothing else in a header is used (the converter's rate, an IF, a DC
offset).

**The `auxi` chunk** (SpectraVue's and HDSDR's form, which CWSL_File
writes; at most 4 096 bytes, a longer one is skipped):
bytes 0–15 the start and 16–31 the stop, each a SYSTEMTIME (eight
little-endian 16-bit words: year, month, day of week, day, hour, minute,
second, ms), and bytes 32–35 the center frequency, an unsigned 32-bit Hz. A
time is taken only if its fields form a date (year 1980–2200, month 1–12,
day 1–31, hour ≤ 23, minute ≤ 59, second ≤ 60, ms ≤ 999), and a center
only if it is not 0. The times are taken as UTC, which is what SpectraVue,
HDSDR and CWSL_File write (they fill it from the system's UTC clock); a
writer that wrote local time would shift every UTC time by its offset, and
the file cannot show that. The stream's start is the header's start plus
the stream's first sample / fs, at the nominal fs: no clock drift is
modeled (W3OA's 2017 file: stop − start = 899.912 s of clock against
900.608 s of samples, 0.08% apart).

**Mirroring** (`RecordingOptions::mirror`; the app's and `kz4ap-calls`' `--mirror`, `kz4ap-resample`'s too): every sample
is conjugated, x → x*, so a signal the file holds at offset +f is read at −f and its true frequency is the center − f. For an
SDR on a transceiver's IF output, whose spectrum is inverted: the owner's 2026-11-04 recording (an RSP1A on the K3's 8.215 MHz
IF) holds the FT8 tones of 7 074–7 077 kHz at 6 963–6 965.4 kHz as labeled (measured by the coordinating session).

**Resampling** (`RationalResampler`, `RecordingReader`). A file whose rate is not 48 000 × 2^k samples/s (k = 0–4: the rates at
which a channel's rate, fs × 64/N (1/128 of fs at 192 kHz), holds a whole number of segmental's samples) and is above 48 kHz is resampled to the largest of
those below it (`decoder_rate_for`); below 48 kHz it is left for the decoder to refuse. The ratio is the rates' reduced
fraction L/M (96/125 for 250 to 192 kHz; L, M ≤ 4 096, else refused), so the rate is exact. The filter is a windowed sinc at
fs_up = L·fs_in whose stopband starts at half the lower rate (96 kHz at 192 kHz: the output's Nyquist, standard anti-alias
practice, so that nothing beyond it folds into the band), over a transition Δf = (10/192) × the lower rate (10 kHz at
192 kHz) below it: cut at −6 dB at 0.5 × the lower rate − Δf/2 (91 kHz). Kaiser window of β = 0.1102 (A − 8.7) = 8.078 for a
design value A = 82 dB re the passband, and length N − 1 = (A − 7.95) / (2.285 · 2π Δf / fs_up), both Kaiser's estimates (sourced: J. F. Kaiser, Proc. IEEE ISCAS 1974; Oppenheim
and Schafer round 7.95 to 8); padded to K·L taps, K = 129 per phase at 96/125. Output n takes input base ⌊nM/L⌋ and phase p = nM mod L:
y[n] = Σ_k h[p + kL] x[base − k], k = 0 … K − 1. Each phase is scaled to sum 1, so a constant goes through unchanged. Group
delay (N − 1) / (2 fs_up) = 257.979 µs (49.53 output samples) at 96/125, not removed (derived). The pass band runs to
96 − 10 = **86 kHz** (derived from the design); measured at 96/125 with 0.5 FS tones (2026-11-04, the desktop): −0.0007 to
+0.0006 dB re input for |f| ≤ 86 kHz, within 0.01 dB re input to 86.4 kHz; −0.34 dB re input at 88 kHz, −6.02 dB at 91 kHz,
−44.3 dB at 95 kHz. An input component at f, 96 ≤ |f| ≤ 125 kHz, appears at f ∓ 192 kHz, at the band's opposite edge, at most
−82.0 dB re input (25 Hz steps; a design value of 80 dB gave −79.8 dB, which Kaiser's estimate falls short by, hence 82). So
86–96 kHz each side is attenuated, by the transition, and holds nothing folded from beyond Nyquist above −82 dB re input.
That is not nothing: a station beyond ±96 kHz at about +80 dB S500 or more still makes a ghost call at the opposite edge
(measured, review 9: listed at +80 dB S500 at +96.35 kHz, not at +75 dB; the strongest key-down peaks measured on three real
files were +40 to +53 dB S500, a margin of about 27 dB). Tests:
21 tones from −86 to +86 kHz within 0.005 dB peak to peak, each with the stated delay (rms error under 10⁻³ FS at 0.5 FS);
88, −91 and 95 kHz at the levels above within 0.1 dB; every tone at 96 ≤ |f| ≤ 125 kHz in 0.5 kHz steps, +100 kHz among them,
below −80 dB re input; 129 taps per phase and the delay; output identical however the input is split; a seek gives the same
samples as reading through. 80 dB and Δf: heuristics (80 dB is 18 to 45 dB above 16-bit quantization noise, which is 98.1 dB below a full-scale tone over 250 kHz and 125.1 dB in 500 Hz; derived). Δf was chosen against CPU, measured on the desktop
(one core, 20 s of noise at 250 kHz in blocks of 4 096, best of 5, three runs): Δf = 10 kHz, 129 taps per phase, 20.1 to
23.0 ms per s of stream; 20 kHz, 65 taps, pass band to 76 kHz, 11.9 to 13.7 ms; 5 kHz, 258 taps, to 91 kHz, 38.7 to 41.9 ms;
the filter before, 63 taps, 12.0 to 14.0 ms. 10 kHz keeps the pass band where it was for about 8 ms more per s of stream, a
per-stream cost, not per channel. Input before the file's start counts as zero; the output ends with the last sample its
input completes (`outputs_for`).

**The center from the file name** (`center_hz_from_file_name`), used when
neither `--center-khz` nor the header gives one: the name's extension of 1–4
letters is dropped and the rest split into words at `_`, `-` and spaces.
First, two adjacent words `<low>` and `<high><unit>` (unit Hz, kHz or MHz,
any case) are the span's edges when high − low is between 0.8·fs and
1.001·fs; the center is their mean (`N2GZ_wideband_7000_7192kHz_…`:
7 096 kHz). Otherwise the first word `<f><unit>` whose value is not fs
itself (`KC2LRC_…_14091kHz`, `kz4ap_7030.000kHz_192kHz_…`). The 0.8 is a
heuristic: W3OA's name gives 6 995–7 177 kHz, 182 kHz, at fs = 192 kHz.
The center from a name is only as good as the name: on N2GZ's file the
call list stands +0.131 kHz above N2GZ's own RBN spots (median of 28
calls), on W3OA's (header center 7 086.101 kHz) +0.215 kHz above the North
American skimmers' median (47 calls), on KC2LRC's −0.02 kHz (50 and 49 calls);
measured on the 2017 recordings, cause of the first two not traced (a
nominal center, a receiver's clock about 30 ppm off; conjecture). No
decision depends on it: the center only labels.

### Receiver audio (`audio_input.hpp`, `RecordingReader`, `bench/src/audio_options.hpp`)

A receiver's audio output, as a sound card records it, decoded by the same chain: the plain decoder's input (owner,
2026-11-19: "paired with audiocard decoding instead of SDR"), and the skimmer's too, like CW Skimmer's "3-kHz Radio" receiver
(an ordinary radio's audio, CW mode, at least 2 kHz wide, AGC off; owner, 2026-11-19). Files only so far: no sound device.

**Exact form, in order.**
1. **Which files.** `RecordingInput::Auto` (the default) reads a one-channel WAV as receiver audio and a two-channel one as
   I/Q; `RecordingInput::Audio` (`--audio`, `--audio-channel N`) reads channel N (0 the first) of any file as audio.
   `WavAudioReader` takes the forms and scaling of `WavIqReader` (integers over 2^(b−1), floats as stored), any number of
   channels, block align = channels × b/8.
2. **Resampling.** Unless f_a = 48 000 samples/s, `RationalResampler(f_a, 48 000)` as above, on the real samples (as complex
   with zero imaginary parts; real taps keep them zero): stopband from half the lower rate, transition (10/192) f_a below it
   (417 Hz at 8 kHz: passband to 3 583 Hz; 625 Hz at 12 kHz: to 5 375 Hz; 2 297 Hz at 44.1 kHz). 100 taps per phase at 8, 12
   and 44.1 kHz (ratios 6/1, 4/1, 160/147). Its delay d_r = (n − 1)/(2M) output samples for n = taps × L taps in all (299.5 at
   8 kHz).
3. **Analytic filter** (`analytic_filter_taps`), at fs = 48 000 samples/s: the ideal response 2 for 0 < f < fs/2 and 0 for
   −fs/2 < f < 0 has the impulse response h(t) = sinc t + j(1 − cos πt)/(πt), t in samples (derived: the real part a delay,
   the imaginary part its Hilbert transform, 2/(πt) at odd t, 0 at even). Taps m = 0 … 2D + 1 at t = m − c, c = D + μ,
   windowed by Kaiser's w(t) = I₀(β√(1 − (t/(D + 1))²))/I₀(β), β = 0.1102 (A − 8.7) = 8.078 for A = 82 dB re the passband,
   D = ⌈L_K/2⌉ with Kaiser's length L_K = (A − 7.95)/(2.285 · 2π Δ/fs) = 1 237.8 for transitions Δ = **200 Hz**: D = 619,
   **1 240 taps**. μ = ⌈d_r⌉ − d_r carries the resampler's fraction of a sample (0.5 at 8 and 12 kHz, 0 at 48 kHz), so that
   the whole delay, d_r + c = ⌈d_r⌉ + D samples, is whole and is dropped: output k stands for the audio's time k / 48 000 s
   exactly (derived; measured within 4 × 10⁻⁷ rad of a tone's phase at 100 Hz to 2.9 kHz). Applied by overlap-save with
   FFTs of 4 096 points, 2 857 new samples a block (60 ms; heuristic, the smallest power of two at least three times the taps).
   Output count ⌈n_a × 48 000 / f_a⌉ for n_a audio samples; the audio after its end counts as zero.
4. **Lower sideband** (`--sideband lsb`, `RecordingOptions::mirror` on audio): the output conjugated, a tone at f read at −f.
5. **The engine** at 48 000 complex samples/s, as for I/Q: N = 2 048, Δf = 23.44 Hz, r = 1 500 samples/s (sections 2, 7).
6. **Passband** (`EngineConfig::passband_lo_hz`, `passband_hi_hz`; `--passband-hz LO,HI`, default 300,3000; on the lower
   sideband −HI to −LO). `band_bins` takes the bins whose centers lie within it; the detector's floor is the median of those
   bins only, its candidates and followed peaks lie among them, and its local floor (the lower quartile within ±1 kHz) is
   clipped to them; the hiss estimator's neighborhood (A.8d) is clipped to them, or is the passband itself for a channel
   wholly outside it. Both 0 (I/Q): the whole span, as before (bit for bit: the same median index n/2, the same loops).
7. **Frequencies.** The engine's are audio tone frequencies f, Hz (−f on the lower sideband). With the dial frequency F
   (`--dial-khz`) and the pitch p (`--pitch-hz`, default 600 Hz), the center the call list adds (`CallRow::freq_hz` + center)
   is F − p on the upper sideband and F + p on the lower, so that **RF = F + (f − p)** (upper) and **RF = F − (f − p)**
   (lower) (derived: on the upper sideband the audio rises with RF and a station at F is heard at p). Without F the list's
   kHz column is empty and its Hz are audio frequencies. `--center-khz` and `--mirror` are refused for audio (they name I/Q's).
   `kz4ap-calls --replay` takes the center from the log's `"input"` line and refuses these options; `kz4ap-bench` reports audio
   Hz only, refuses `--dial-khz` and `--pitch-hz`, and on `--sideband lsb` negates the labels' frequencies, offsets and drifts.
8. **Levels.** A real tone a cos 2πft (power a²/2 FS²) becomes a complex tone of amplitude a, a² FS² in its bin: a
   full-scale sine reads 0 dBFS re the sound card's full scale (derived). Real noise of variance σ² FS² per sample, white
   to f_a/2, has the one-sided density 2σ²/f_a and the analytic signal N₀ = 4σ²/f_a FS²/Hz in the band, so S₅₀₀ = a²/(N₀ ·
   500 Hz), the usual real-signal SNR in 500 Hz (derived). These are receiver-audio values: after the receiver's gain, filter
   and any AGC, not comparable with SDR I/Q's even in the same bandwidth. The bench tools say so (kz4ap-calls on stderr and as an
   `"input"` line in `--log`; kz4ap-bench as `"input"` in its result file).

**Provenance.** 48 kHz: derived (the decoders' lowest rate, `decoder_rate_for`; no audio rate needs more). The Hilbert
filter form: standard (the FIR Hilbert transformer, Oppenheim and Schafer; sourced); chosen over mixing the passband's center
to 0 Hz and low-passing (the same filter, shifted) because it leaves frequencies as audio tones. Δ = 200 Hz: heuristic, below
any receiver's audio passband (which starts at 200 Hz or more). A = 82 dB re the passband: as the resampler's (heuristic). Measured
(`AudioAnalytic.ARealToneBecomesItsAnalyticSignal`, 2026-11-19, at 8, 12, 44.1 and 48 kHz): gain within 0.0007 dB re the input
from 100 Hz to 2.9 kHz, the image at −f at most −84.1 dB re the tone at 100 Hz and −95 dB re the tone from 300 Hz. The passband default
300–3000 Hz: heuristic, a typical SSB receiver's audio (2.7 kHz); the median holds the in-band noise while the receiver's
filter covers more than half of it (derived for a median). The pitch default 600 Hz: heuristic, a common CW pitch.

**Limitations.**
- A filter narrower than half the stated passband (a 500 Hz CW filter within 300–3000 Hz) leaves the median and the hiss
  percentile in the near-silence: the floor reads low, the decoder's N₀ too. Set `--passband-hz` to the filter.
- The receiver's filter shape colors the noise: within the passband the floor and the hiss read the median and the 25th
  percentile of a sloped floor, low near the edges; a station near an edge is attenuated with its noise.
- AGC (gain changing within and between elements) is not modeled; nothing has been measured with it.
- A carrier's sideband whose mirror falls outside the passband passes the sideband test (section 6).
- Resampling costs about 3.5 ms per s of stream at 8, 12 and 44.1 kHz (the resampler on complex samples with zero imaginary
  parts); at 48 kHz none. The analytic filter, with the file's reading, about 1.85 ms per s of stream (measured, the desktop,
  one thread, 60 s of noise). One channel's whole cost, against the bar (at most 40 ms per channel-second required, about 8 ms
  the goal): about 10 to 13 ms per channel-second at 48 kHz and about 12 to 17 ms at 8, 12 and 44.1 kHz, of which the fixed
  per-stream part is 6.19 ms per s of stream at 48 kHz and 9.6 to 10.0 ms at the resampled rates and the track's own 2.5 to
  6.8 ms (measured, the desktop, one thread: four 60 s clips at 12 kHz and the review's runs, 2026-11-19). Within the
  requirement, above the goal: at one channel the per-stream stages (spectrum about 3.1 ms, the source, the channelizer's FFT
  about 1.05 ms) outweigh the decoder.

## A.2 The FFT size N and bin width Δf

### Exact form

The rules of this section as the code implements them (moved verbatim from the body on 2026-10-05, when the body was rewritten for reading).

N is the largest power of two with fs/N ≥ 20 Hz: 8192 at 192 kHz,
Δf = 23.44 Hz, each transform 42.7 ms long. Heuristic; the real parameter
is "bins about 23 Hz wide" (the 20 Hz threshold only reproduces it at
48 kHz × 2^j rates).

In random keying about 44% of the key-down power (the key-down fraction) is a
pure carrier line, always in one bin; the rest is keying sidebands whose
main lobe spreads over about ±1/dit: ±21 Hz at 25 WPM, ±33 Hz at 40 WPM,
±50 Hz at 60 WPM (derived). The occupied bandwidth by the ham rule of thumb
(about 4 × WPM in Hz) is wider.


**N at each rate** (`choose_fft_size`, engine.cpp):

| fs | N | Δf | transform length N/fs | hop (N/2) |
|---|---|---|---|---|
| 44.1 kHz | 2048 | 21.5 Hz | 46.4 ms | 23.2 ms |
| 48 kHz | 2048 | 23.4 Hz | 42.7 ms | 21.3 ms |
| 96 kHz | 4096 | 23.4 Hz | 42.7 ms | 21.3 ms |
| 192 kHz | 8192 | 23.4 Hz | 42.7 ms | 21.3 ms |
| 768 kHz | 32768 | 23.4 Hz | 42.7 ms | 21.3 ms |

**Provenance (heuristic; the 20 Hz is not the real parameter).** N = 8192
at 192 kHz (Δf = 23.4 Hz) was the original first guess, never measured.
When the engine was made to choose N from the sample rate, the "≥ 20 Hz"
rule was written only to reproduce that 23.4 Hz at other rates. Because N
moves in factors of two, any threshold between 11.7 Hz and 23.4 Hz gives
the same N at 48 kHz × 2^j; 20 Hz is just a number in that range. (At
rates that aren't 48 kHz × 2^j the threshold does matter: at 44.1 kHz it
gives 21.5 Hz bins.) The idea behind ~23 Hz: narrow enough to separate
stations a few tens of Hz apart and keep noise per bin low, wide enough
that a keyed CW signal's energy (tens of Hz wide at contest speeds) falls
in one to three bins. Not measured against alternatives (backlog: "Choose
the FFT bin width and channel filter by measurement").

## A.3 Hops and 50% overlap

### Exact form

The rules of this section as the code implements them (moved verbatim from the body on 2026-10-05, when the body was rewritten for reading).

The hop is N/2 (50% overlap), a standard choice: periodic Hann windows
shifted by N/2 sum to a constant (derived), and the channelizer's filter
length N/2 + 1 follows from this hop (derived, section 7).


**Derivations.** Both FFTs advance by N/2 samples (21.3 ms): each
transform uses the newest hop of samples plus the one before it. After the
first, there is exactly one spectrum FFT and one channelizer FFT per hop.
(The spectrum analyzer's first transform comes when N samples have
arrived, i.e. after two hops; the channelizer starts after one hop with
N/2 samples of zeros as history.)

- **Spectrum:** periodic Hann windows shifted by N/2 add up to a constant
  (w[n] + w[n + N/2] = 1), so across frames every input sample is weighted
  equally. N/2 is the *largest* hop with that property; smaller hops such
  as N/4 (75% overlap) have it too, at twice the computation. Successive
  frames at 50% are nearly independent for noise: the correlation of the
  noise power in a bin between neighboring frames is
  (Σ w[n]·w[n+N/2] / Σw²)² = (1/6)² ≈ 3%, the sum running over the
  overlapping half, n = 0 … N/2 − 1.
- **Channelizer:** it shares the spectrum's hop so the two stay in
  lockstep. Given that hop, the filter length follows: overlap-save fast
  convolution with an L-tap filter needs L − 1 samples of history in front
  of each block of new samples. With N-point transforms and N/2 new
  samples per hop, the history is N/2 samples, which allows at most
  L = N/2 + 1 taps. That is why the channel filter is exactly N/2 + 1 taps
  long.

**Provenance.** 50% overlap: standard choice; what follows from it is
derived.

## A.4 Spectrum analyzer

### Exact form

The rules of this section as the code implements them (moved verbatim from the body on 2026-10-05, when the body was rewritten for reading).

Every hop, P[k] = |X[k]|²/(Σw)², X[k] = Σₙ w[n]·x[n]·e^(−j2πkn/N),
w[n] = ½ − ½·cos(2πn/N) (periodic Hann), Σw = N/2, Σw² = 3N/8, stored as
10·log₁₀ P[k] dBFS. A tone of amplitude A FS on a bin reads A² FS²; white
noise of σ² FS² per sample reads σ²·1.5/N = (σ²/fs)·ENBW, with the
equivalent noise bandwidth ENBW = N·Σw²/(Σw)² = 1.5 bins = 35.2 Hz
(derived). Hann is a standard choice: highest sidelobe 31 dB below the
main lobe; a tone midway between bins reads 1.42 dB below its on-bin
value (scalloping, derived).


**Derivations.**

- Bins are stored lowest frequency first: stored bin i is at (i − N/2)·Δf.
- **A tone.** A complex tone of amplitude A FS exactly on a bin gives
  |X[k]| = A·Σw, so P[k] = A², i.e. 20·log₁₀A dBFS. Dividing by (Σw)²
  corrects for the window's coherent gain. A tone of amplitude 0.0102 FS
  (the synthetic 20 dB SNR (500 Hz) signal of section 5) reads −39.8 dBFS
  key-down.
- **Noise.** White noise of σ² FS² per sample gives an average |X[k]|² of
  σ²·Σw², so P_noise = σ²·Σw²/(Σw)² = σ²·1.5/N = (σ²/fs)·ENBW: the noise
  power spectral density σ²/fs (FS²/Hz) times the bin's equivalent noise
  bandwidth, ENBW = N·Σw²/(Σw)² = 1.5 bins = 35.2 Hz. With σ = 0.02 FS at
  192 kHz: σ² = −34.0 dBFS total, σ²/fs = −86.8 dBFS per Hz, and each bin
  reads −71.4 dBFS.
- **Why the ENBW is 1.5 bins.** The window tapers the ends of each
  transform, which broadens every bin's frequency response: a bin collects
  noise from 1.5 bins' worth of bandwidth instead of 1. A rectangular
  window would give exactly 1.0 bin, i.e. 1.8 dB less noise power per bin
  (a ratio of 1.5), but its sidelobes are only 13 dB below the main lobe,
  so a strong station leaks into bins far away. Hann's highest sidelobe is
  31 dB below the main lobe and falls off quickly.
- **Spreading.** The Hann window spreads even a pure tone over several
  bins: its main lobe is 4 bins wide (±2 bins), and a tone halfway between
  two bins shows up in both, each 1.42 dB below the on-bin value
  (scalloping). A keyed signal spreads further, by its keying sidebands.

**Provenance.** Hann: standard choice, not compared with other windows
here.

**Limitations.** None known here. Each frame carries its power both in dB re
1 FS² (`power_db`, for the display) and in FS² (`power_fs2`, since 3f99f4f),
so the detector averages linear power without undoing a logarithm; the
segmental path's detector builds its own analyzer without decibels, so its
frames carry `power_fs2` only (e766e64).

## A.5 What "SNR" means here

### Exact form

The rules of this section as the code implements them (moved verbatim from the body on 2026-10-05, when the body was rewritten for reading).

S₅₀₀ is the key-down carrier power over the noise power in 500 Hz (the CW
Skimmer / RBN convention; synthetic recordings use σ = 0.02 FS of complex
white noise, so the noise power in 500 Hz is σ²·500 Hz/fs). The detector
works per bin, in the 35.2 Hz ENBW: SNR per bin = SNR in 500 Hz
+ 10·log₁₀(500/35.2) = **+11.5 dB** for a signal inside both bandwidths
(derived, white noise). A track's SNR is an averaged power, so it lies
below the key-down SNR per bin by about the duty cycle (−3 dB relative to
the key-down power at 50% key-down), lower still by the keying sidebands
outside the peak bin and by up to 1.42 dB of scalloping.

With a key-down fraction D, the averaged power is a carrier line of D² of the
key-down power, always in the peak bin, plus keying sidebands of D − D²,
spread over about ±1/dit, so less of them fall in the bin the faster the
keying. At D = 0.44 the track reads between about 3.6 dB (slow keying: all
sidebands in the bin) and 7.1 dB (very fast: the carrier line only) below
the key-down SNR per bin (derived), plus up to 1.42 dB if the tone sits
between bins.


**Derivations.** White noise power grows in proportion to bandwidth, so
for the same signal SNR(B₁) = SNR(B₂) + 10·log₁₀(B₂/B₁) (white noise,
signal inside both bandwidths). Conversion, 500 Hz → per bin:
+10·log₁₀(500/35.2) = +11.5 dB. Example: the engine tests' 20 dB SNR
(500 Hz) signal is 31.5 dB SNR per bin key-down, and its averaged track
SNR is about 27 dB per bin (keying duty cycle a little under 50% including
the text's word spaces, plus the keying sidebands that fall outside the
peak bin).

- **Detector (tracks, `Track::snr_db`):** per bin, in the Hann ENBW of
  35.2 Hz. It is the averaged power (section 6), so it is the key-down
  power times the keying duty cycle: about 3 dB below key-down for a signal
  on half the time (10·log₁₀ 0.5 = −3.0 dB). It drops by more than the duty
  cycle alone because keying spreads the signal: at 25 WPM a dit (48 ms) is
  about as long as one Hann frame (42.7 ms), so the keying sidebands put
  part of the power outside the 35 Hz ENBW of the peak bin. It is also
  reduced by up to 1.42 dB when the station sits between bins.

**Provenance (the 500 Hz convention).** One convention, not a universal
standard (WSJT-X reports SNR in 2500 Hz, C/N₀ uses 1 Hz), but the one CW
Skimmer and the Reverse Beacon Network (RBN) use. Per its author's
description, CW Skimmer takes the signal through a 50 Hz filter, discards
key-up and transition samples, and estimates key-down power under a
Rayleigh fading model; it divides that by the noise density × a nominal
rectangular 500 Hz, with the noise density estimated from the flat part of
the whole receiver span (`docs/research/decoder-survey.md`, "One SNR
yardstick"). For clean, non-fading signals our synthetic SNR is the same
quantity.

**Limitations.** How each side *estimates* signal and noise from real
audio differs, and matching them remains a calibration item (backlog).

## A.6 Signal detector

### Exact form

The rules of this section as the code implements them (moved verbatim from the body on 2026-10-05, when the body was rewritten for reading).

All neighborhoods are stated in Hz and converted to bins at the point of
use with lround(Hz/Δf): 47 Hz → 2 bins, 23 Hz → 1 bin at 23.44 Hz.

Receiver audio (A.1, "Receiver audio", step 6): with a passband (`DetectorConfig::band_lo_hz`, `band_hi_hz`, from
`EngineConfig::passband_lo_hz`) the floor's median, the candidates, the followed peaks and the local floor read only the bins
whose centers lie within it; everything below holds within it. Without one (I/Q), the whole span.

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
- **Cap.** At most **400** tracks (`DetectorConfig::max_tracks`); a stronger new candidate replaces the
  weakest (heuristic, a CPU guard).
- **Envelope path** (with the Envelope decoder): frequency fixed at
  birth; a peak less than 3 bins (70 Hz) from a track's bin belongs to it.
- **Oracle mode** (benchmark only) bypasses the detector and opens a
  channel at each labeled frequency, rounded to the nearest bin, for the
  whole recording.


**Derivations.**

- **dB and linear.** The detector converts each bin back to linear power
  (FS²), averages in linear, and converts the average to dB. Averaging
  noise power in dB would bias it about 2.5 dB low (the mean of the
  logarithm of exponentially distributed power is 2.51 dB below the
  logarithm of its mean). Everything after averaging is done in dB, which
  is correct for each use: thresholds are ratios to the floor, so comparing
  dB differences is the same as comparing linear ratios; the median is
  unchanged by any monotonic transform, so the median of dB values is the
  dB of the median; parabolic peak interpolation works better on log
  power: the Hann main lobe is closer to a parabola in dB than in linear
  power.
- **Why average, and how much.** A keyed signal is on only about half the
  time; averaged over many elements it looks like a steady carrier about
  3 dB below its key-down power, so detection doesn't depend on catching it
  key-down. A single frame's noise power in a bin is exponentially
  distributed (standard deviation equal to its mean). An exponential
  average has the same noise variance as a plain (boxcar) mean over
  (2 − α)/α ≈ 2τ/t_hop ≈ 94 frames (2 s), cutting the relative fluctuation
  to about 1/√94 ≈ 10% (≈ ±0.45 dB). Even the largest of 8192 such bins
  stays well under the 6 dB detection threshold (roughly 4 standard
  deviations, 1.5 dB above the mean noise power), so noise alone almost
  never creates a track. The cost is latency: a new station takes about
  0.5–1.5 s to become a track (averaging plus the 0.5 s persistence), and a
  track outlives its station by several seconds while its average decays.
- **How long the average takes to forget a station.** After a station
  stops, its contribution to P̄ decays as exp(−t/τ). The track stays alive
  while its level is ≥ 3 dB above the floor, i.e. until the leftover
  signal power falls to about the noise power. Starting from an averaged
  SNR per bin of R (a linear power ratio), that takes about t = τ·ln R: for
  27 dB SNR per bin (R = 500), 6.2 τ = 6.2 s. (A straight dB slope of
  4.34 dB per τ gives (27 − 3)/4.34 = 5.5 τ, but that ignores the noise
  added to the leftover signal.)
- **The floor's bias.** For averaged noise the median is essentially the
  mean: the averaged power is close to Gaussian, and a simulation of this
  filter on exponential noise puts its median 0.02 dB below its mean (less
  than 0.1 dB). For a single, unaveraged frame the median of exponentially
  distributed noise power is ln 2 = 0.69 of its mean, 1.6 dB low; that is
  not the case here. Signals occupy far fewer than half the bins, so they
  don't move the median.
- **Why neighboring bins come up at all.** Averaging is per bin and never
  merges bins. Neighbors enter only the decision "is this a new station?"
  (the ±47 Hz peak rule on both paths; then the attribution rule, which
  differs by path: the 3-bin rule on the Envelope path,
  `Attribution::Bins`, and the channel distance D_ch = 47 Hz between
  interpolated frequencies on the default Matched path,
  `Attribution::Distance`), the level of an existing track (its bin ±1)
  and, on the Matched path only, which peak a track follows. These rules
  undo spreading: one station puts power in 2–4 adjacent bins because of
  the Hann window's main lobe, its position between bins, and its keying
  sidebands. Without them, one station would become several tracks. Larger
  bins instead would be worse on two counts: noise per bin grows in
  proportion to bin width (a 3× wider bin gives 10·log₁₀3 = 4.8 dB lower
  SNR per bin for the same signal), and two stations closer than a bin
  could no longer be told apart.
- **Hz to bins.** The rules are stated in Hz (`DetectorConfig`
  `peak_radius_hz`, `candidate_step_hz`, `level_radius_hz`) and converted
  at the point of use with `std::lround(Hz / Δf)`, so a different bin width
  keeps their width in Hz (the 3-bin rule of `Attribution::Bins` is still
  counted in bins): 47 Hz rounds to 2 bins and 23 Hz to 1 bin for bin
  widths from 18.8 to 31.3 Hz, which covers every usual rate (8, 11.025,
  32, 44.1, 48, 96, 192 and 768 kHz give 20–31.25 Hz bins with
  `choose_fft_size`), so the Envelope path stays bit-identical to milestone
  1 there; at rates whose bins are wider than 31.3 Hz (for example
  33–40.9 kHz) the peak neighborhood rounds to 1 bin (nothing rejects such
  a rate).
- **Drift on the Envelope path** (frequency fixed at birth): the track's
  level is taken over ±1 bin, so a drift of up to about ±1.5 bins (±35 Hz)
  costs at most 1.42 dB of level (scalloping). Beyond that the loss grows
  fast (from the Hann response, for a steady tone): 6 dB at ±2 bins
  (±47 Hz), 15 dB at ±2.5 bins (±59 Hz). Whether the track survives depends
  on how far above the 3 dB keep-alive level the station was. Once the peak
  is 3 or more bins (≥ 70 Hz) from the track's bin, it can also be born as
  a second track. The channel filter is flat (within 0.01 dB) to ±30 Hz and
  only 1.2 dB down at ±100 Hz (measured, A.7), so the decoder would
  tolerate more drift than the track does.
- **Drift on the Matched path.** Each track follows its own spectral peak,
  so a drifting station keeps one track and its level is read where it now
  is; the decoder re-centers within ±12 Hz of the track's frequency
  (section 7), and the engine reports the channel center plus the
  decoder's estimate. The channel itself does not move; the decoder's NCO
  covers ±75 Hz around it. A 1 s average of a ramp lags ḟ·1 s.
- **Why D_ch = 47 Hz.** About the half-width of the detector's Hann main
  lobe, 2/T_w for its 42.7 ms window (46.9 Hz); a peak closer than that to
  a station can be that station's own spread. Stated in Hz, it does not
  change with the FFT size. One distance decides, on the Matched path,
  which station each channel follows; the detector alone decides:
  (1) following, (2) attribution (one peak farther than D_ch can become a
  track of its own), (3) the channel's tracker fine-tunes within ±12 Hz of
  the detector's frequency for its track, which the engine passes before
  every channel block. So in a QSO turnover within D_ch, when the
  detector's peak moves to the answering station, the decoder's anchor and
  NCO move there, and back; the channel itself (its center, the bin
  nearest the track's birth frequency) does not move. Channels are never
  merged.
- **The cap's scale.** The physical ceiling is set by the attribution
  rule, about fs / D_ch = 192 000 Hz / 47 Hz ≈ 4090 tracks at 192 kHz on
  the default Matched path, and fs / 70.3 Hz ≈ 2730 with the Envelope
  path's 3-bin rule (3 × 23.4 Hz); a busy contest can put more than 100
  stations in 192 kHz.

**Provenance.**

- τ = 1 s: heuristic, a round number that seemed a sensible compromise. It
  *should* be derived from three requirements: a false-alarm rate (how
  often the largest of N noise bins crosses the threshold), an averaging
  time covering several characters at the slowest speed of interest (so
  the duty cycle averages out), and acceptable detection latency (backlog,
  with a comparison against a boxcar mean). The decay time was measured
  once, while writing the engine test `EventsFollowTrackLifecycle`
  (recorded in a comment there, not asserted by any test): a 20 dB SNR
  (500 Hz) station's track died 7.8 s after its last mark with a 1 s
  timeout, i.e. about 6.8 s of decay.
- The thresholds (6 dB, 3 dB), the ±47 Hz and ±23 Hz neighborhoods, the
  23 Hz candidate step, the 0.5 s persistence, the 1 s detection warm-up
  and the 10 s timeout: heuristic. The milestone-1 rule (a peak less than
  3 bins, 70 Hz, from a track's bin) is still selectable
  (`Attribution::Bins`) and is what the Envelope path (`--decoder
  envelope`) uses.
- Frequency accuracy: measured, a station at +1000.0 Hz is reported at
  +999.8 Hz (typically accurate to a few Hz).
- Only slow drift is in scope (owner, 2026-09-29): simulated at 1 Hz/s, one
  track, the reported frequency 1.49–1.63 Hz behind the carrier and the
  track's own 0.94–1.23 Hz behind (the 1 s average's lag ḟ·1 s, derived).
- D_ch = 47 Hz: heuristic (`EngineConfig::channel_distance_hz`; owner
  decisions 2026-09-29, "option 1"). Simulated (plan
  `docs/plans/2026-09-27-milestone-2a-benchmark-and-front-end.md`,
  2026-09-29; A at S₅₀₀ = 15 dB, 25 WPM; B answering at 18 WPM, its level
  in dB re A's key-down power; 30 seeds each): within D_ch (0–40 Hz) at
  −6 dB or stronger, A's channel followed B, decoded it and came back, with
  one channel; 60–200 Hz away B had its own channel, decoded in 30 of 30
  from −6 dB up; channels were never merged. The suite's view of it: 2a
  record, section 8.3 (group H).
- The cap of 400 (`DetectorConfig::max_tracks`, a config parameter not yet
  exposed to users): heuristic, A.9b.

**Limitations.**

- **The floor is global:** one number for the whole span. Fine for the
  flat synthetic noise, but a real SDR's passband is not flat (it rolls off
  toward the band edges), and there is often a spike at 0 Hz (DC offset).
  Where the local noise is above the median, noise crosses the threshold
  more easily (false tracks); where it is below, weak stations are harder
  to detect and their SNR reads low. A local floor now also guards a new track on the segmental path (A.9b). The backlog has a local floor as a
  candidate.
- **Pauses and track lifetime, by accident, not design.** A station that
  pauses keeps its track while the pause is shorter than the average's
  decay time plus the 10 s timeout: roughly 16–17 s for a 20 dB SNR
  (500 Hz) station, longer for stronger ones. Neither number was chosen
  with pauses in mind. Within that time the same track, channel and
  decoder continue, so the decoder keeps its speed estimate. After a
  longer pause the track has died; when the station resumes it becomes a
  new track with a new decoder, which starts again from 25 WPM.
- **Retune delay.** The detector's peak moves to an answering station only
  when its 1 s power average overtakes the first station's decaying one:
  1.3–1.8 s into B's over at 10–25 Hz, 2.3 s at 40 Hz and −10 dB
  (simulated; 2.6 s after A's last mark derived at −10 dB). It matters only
  when the answering station is on a different frequency from the one the
  channel is tuned to (a station that pauses and resumes on its own
  frequency loses nothing). Median characters of B lost at the start of its
  over: 0 at 10 Hz; at 25, 40 and 50 Hz, 1, 3 and 3 at −10 dB, 0, 1 and 2
  at −6 dB, 0 at 0 and +6 dB (simulated).
- **A neighbor 60–70 Hz away at A's level or stronger** leaks through the
  matched filter's first sidelobe (−18.7 dB relative to a centered station
  at 60 Hz and K = 58; −19.6 dB at 70 Hz through the 16 ms acquisition
  filter; derived), is keyed in fragments and corrupts the speed estimate:
  A's next over exact in 6 and 0 of 30 at 60 Hz, 0 and +6 dB (its last
  three words intact in 26 and 30; simulated). The fix belongs in the
  filter's design.
- **Oracle mode** (benchmark only) has no detector, so the anchor is fixed
  at the labeled frequency the oracle channel was opened for, and the
  tracker covers ±12 Hz around it: a station that drifts more than 12 Hz
  from its label, or a QSO's answering station more than 12 Hz from the
  label, cannot be followed there; the benchmark marks such oracle rows as
  not meaningful for the Matched decoder (A.11).
- **Two tracks can converge on one peak (possible; derived from the code,
  not observed).** Following (`SignalDetector::follow_peaks`) moves each
  track to the strongest qualifying peak within D_ch of its own frequency
  without checking whether another track already holds that peak, and
  channels are never merged. Two tracks born just over D_ch apart (for
  example stations at the crowded group's 50 Hz spacing) could both move
  onto one station's peak if the other station falls silent and its
  track's frequency lies within D_ch of that peak (a keyed station's
  interpolated frequency moves from frame to frame; by how much has not
  been measured); they would then follow the same station, with two
  decoders printing the same text, until one dies. No benchmark run has
  been checked for it: the crowded group's Matched runs report 0 false
  tracks (all 12 recordings, 3 seeds), but that count would not show it,
  since each converged track was matched to its own label by its birth
  frequency, the only frequency the results record. Group H's drawn QSO at
  53.9 Hz may be a case (A.11, "QSO regimes"). Backlog: "Tracks converging
  on one peak".

**Oracle mode and recorded channels (benchmark tooling).**
`EngineConfig::oracle_frequencies_hz` (`kz4ap-bench --oracle`) bypasses the
detector: a channel is opened at each given frequency, rounded to the
nearest FFT bin, from the first sample, and stays open to the end. It
measures the decoder apart from detection, including below the detector's
threshold (roughly S₅₀₀ = 0 dB for a keyed station), and leaves the station
up to ±½ bin (±11.7 Hz) off its channel's center, the worst case for
frequency re-centering. Normal operation never uses it. `kz4ap-bench
--record-channels DIR` (with `--oracle`) copies each oracle channel's
stream to a file through `Engine::set_channel_tap`, as the channelizer
delivers it and before the decoder sees it; the tap only observes (the
decoded text is the same with or without it, tested). Without `--oracle`,
`--record-channels` records every channel the detector opens, from its
first block to its track's death, with the detector's frequency for the
track at every block (`ChannelBlock::anchor_hz`, the value the engine
already hands the decoder as its anchor before each block: the labeled
frequency in oracle mode; with `--decoder matched` and no oracle, the
detector's current frequency for the track; with the Envelope decoder and
no oracle, which never updates it, the track's birth frequency). Benchmark
tooling only; it changes no signal processing. File formats: A.11,
"Recorded channel streams".

### The segmental path's detector (`apply_path_defaults`)

**Exact form.** `apply_path_defaults(EngineConfig&)` sets, for `FrontEnd::Segmental`, the values below; the engine applies it
unless `EngineConfig::as_given` (a benchmark variant: `kz4ap-bench` and `kz4ap-calls` take `--detector NAME=VALUE`, applied over
it). On every path it sets `attribution_distance_hz` = `channel_distance_hz` first.

- **DC blocker** (`EngineConfig::dc_block_s` = 10 s; `Engine::process_hop`, before the spectrum and the channelizer): d starts at
  0 FS; per sample d ← d + α(x − d), y = x − d, α = 1 − exp(−1/(τ fs)), in double precision. It runs in oracle mode too: oracle
  channels read the blocked input.
- **The detector's spectrum** (`DetectorConfig::fft_factor` = 4): a second `SpectrumAnalyzer(fs, 4N, 2N, decibels = false)`,
  fed every hop, not made in oracle mode; the detector reads its frames only (`power_fs2`), the band's frames going to the hiss,
  the leak floor and the display as before. Its frames come every 4th hop.
- **Neighborhoods:** `peak_radius_hz` 12 (2 bins), `level_radius_hz` 6 (1 bin), `candidate_step_hz` 6 (1 bin),
  `follow_radius_hz` 12 (a track's peak within 12 Hz of its frequency, a drift of up to 140 Hz/s; 0 elsewhere, meaning D), `attribution_distance_hz`
  D = 30 Hz (a new peak within D of a track belongs to it; one track per peak within D; holds of radius D).
- **Threshold** 7 dB over the band's median, the local floor's test the same 7 dB; hysteresis 3 dB. `floor_every_frames` 2.
- **Prominence** (`prominence_db` 6, `prominence_radius_hz` 150; `SignalDetector::is_peak`): for each side, walking out from
  bin i up to 150 Hz, the lowest average so far, L; at the first bin above P_i, the peak fails if P_i < 10^0.6 L. No higher bin
  within the radius: no condition from that side.
- **Sidebands** (`sideband_db` 10, `sideband_mirror_db` 3, `sideband_radius_hz` 150): for each side, c = the highest bin within
  150 Hz; if P_c ≥ 10 P_i (10 dB), m = 2c − i and M = max(P_{m−1}, P_m, P_{m+1}); the peak fails if M ≥ 10^−0.3 P_i. Both
  tests apply to new peaks and to the peaks a track follows.
- **DC guard** `dc_guard_hz` 0 (none).
- **First retirement open** (`EngineConfig::first_retirement_open` 2; `SignalDetector::retire`'s `first_open`): the first
  retirement within D of a place, among holds not forgotten (120 s open), records the place with no hold and counts toward no
  region; the next one there closes it as a first hold (8 s, doubling).

**Derivations.**

- **The blocker's response.** H(z) = (1 − α)(1 − z⁻¹) / (1 − (1 − α) z⁻¹). For f ≪ fs, |H|² = f² / (f² + f_c²), f_c =
  α fs / 2π = 1 / (2π τ) = 0.0159 Hz at τ = 10 s (α = 5.2 × 10⁻⁷ at 192 kHz); at fs/2, |H| = 2(1 − α)/(2 − α) = 1 − α/2.
- **What it does to a keyed station near 0 Hz.** A station of key-down amplitude A, keyed a fraction m of the time, at f: the
  average d holds its carrier line m A attenuated by 1/√(1 + (2π f τ)²), and y = x − d leaves that much in its key-ups and takes
  it from its marks. At τ = 1 s that is −16 dB re m A at 1 Hz and −1.4 dB at 0.1 Hz; at τ = 10 s, −36 dB at 1 Hz and −16 dB at
  0.1 Hz. So the band a blocker spoils shrinks as 1/τ, and a radio's offset takes about τ to leave (a change in it likewise).
  Started at the first hop's mean, the noise of that mean (σ/√(N/2), 6 × 10⁻⁴ FS at 48 kHz) stays at 0 Hz for about τ, 20 dB
  below a station of +10 dB S500: hence the start at 0.
- **The fine spectrum.** Bins fs/4N = 5.86 Hz at every usual rate (N scales with fs, A.2); window 4N/fs = 170.7 ms; hop 85.3 ms;
  ENBW 1.5 bins = 8.79 Hz; the main lobe's half width 2 bins = 11.7 Hz, hence the peak radius. α_d = 1 − exp(−85.3 ms / 1 s) =
  0.0818, a boxcar of (2 − α_d)/α_d = 23.4 frames, a plain running mean for the first 1/α_d = 12 frames (1 s). White noise's averaged power then has a relative standard deviation of about
  1/√23.4 = 0.21 (Hann frames overlapping by half are nearly uncorrelated in power), against 0.10 on the band's bins: the 7 dB
  threshold, 5.0 times the mean, stands about 19 standard deviations above it, so flat white noise alone does not cross it.
- **Why the band's bins hid a neighbor.** Hann's response at x bins, (sin πx / πx)² / (1 − x²)², at 60 Hz (2.56 bins) from a
  neighbor 10 dB stronger: the bin between them, 13 Hz (0.55 bins) from it, reads it −1.8 dB re its peak, 8 dB above the station's own
  peak (derived); with the peak radius of ±47 Hz the station is never a local maximum.
- **Sideband symmetry.** A keyed carrier is A(t) cos(2π f_c t + φ) with A real; its power spectrum is |Â(f − f_c)|² +
  |Â(−f − f_c)|², and |Â(−ν)|² = |Â(ν)|² for a real A: symmetric about f_c. A train of dots, period 2T, is a square wave whose
  Fourier series holds the odd harmonics k/(2T), amplitude ∝ 1/k: lines at ±(2j + 1)/(2T) (41 and 69 Hz at 33 WPM), which on the
  fine bins stand clear of the carrier's lobe. Chirp (a frequency step at key-down) breaks the symmetry.

**Provenance.** Decision log, 2026-11-04. Measured on the Linux machine, the detector's path, against `main`'s detector:
review 5's pairs (wanted +10 to +40 dB S500, neighbor 40 to 300 Hz, −10 to +10 dB re wanted), review 7's stress cells, E2 of
DEV2 seeds 1 and 2, det-1's own cells (a carrier or a keyed neighbor there first, 20 to 150 Hz, −10 to +20 dB re wanted;
jittered grid), the contest and band recordings, and real30 and live2.

- Factor 4: tried 1, 4 and 8. 8 (2.93 Hz bins, 341 ms) opened 4 times the tracks on real30 and lost true calls on the contest
  (213 against 219). Heuristic, chosen by measurement.
- 12, 6, 6 Hz: heuristics, not varied; the 12 Hz peak radius from the main lobe's 11.7 Hz half width (derived), the 6 Hz
  level radius and candidate step half of it.
- D = 30 Hz: tried 47, 30 and 20 Hz. 20 Hz read the same within 0.005 on the pairs and E2 and opened 10% more tracks on real30.
  Heuristic.
- Follow radius 12 Hz: the peak radius; one setting, not varied apart from D. Heuristic.
- Threshold 7 dB: tried 6, 7, 8 dB; the test sets read the same at all three; real30 opened 2 325, 1 711 and 1 492 tracks (no
  first-open rule). Heuristic. A keyed carrier at 22 WPM, +10 dB S500, read 15.7 to 17.3 dB per band bin and 20.9 to 23.2 dB per
  fine bin over 8 to 16 s (measured; dB re the band's median), so 7 dB on fine bins is the more sensitive threshold.
- Prominence 6 dB over 150 Hz: tried 6 and 8 dB, radii 47 and 150 Hz. Heuristic. A neighbor's sideband pair measured 3.6 to
  4.5 dB of prominence, a station 60 Hz beside a neighbor 10 dB stronger 9.8 dB.
- Sideband 10 dB, mirror 3 dB, 150 Hz: one setting. Without it, strong stations at 26 to 36 WPM opened tracks in pairs at
  ±50 to ±75 Hz: on the contest of 280 stations about 70 such tracks, 354 in all; on the band of 100, 207 tracks. The variant
  with it (and with D = 30 Hz and the follow radius) opened 312 and 100.
- First retirement open: tried none, quiet only and any. Quiet only read the slice as none did; any, against quiet only on the
  whole detector's path of seeds 1 and 2: A2 −0.0010, B2 −0.0013 (95% intervals clear of 0), no group worse, for 27% more
  channel-seconds on real30 and 48% on live2 than none.
- DC blocker 10 s, starting at 0: tried 1, 10 and 30 s, and the first hop's mean as the start. At 1 s F2 on the oracle path read
  0.1027 against `main`'s 0.0895 (its station starting at 0 Hz and drifting: 57 edits of 99 against 0), with the first hop's start
  0.1018 at 10 s and 0.1081 at 30 s; started at 0, 0.0898 at 10 s and 0.0894 at 30 s, the detector's path 0.0888 and 0.0885.
  On real30, tracks within 100 Hz of the center held 783, 950 and 1 400 channel-seconds at 1, 10 and 30 s. Heuristic, chosen by
  measurement. With no DC guard the stations the test set places near 0 Hz read as on the oracle path (E2's index 8: 0 to 285
  edits, the oracle's 0 to 293).
- `floor_every_frames` 2: heuristic, for CPU.

**Limitations.**

- **CPU:** the second analyzer and the detector on 4 times the bins, and more tracks (below). Measured on the Linux machine,
  one thread each, `main` beside `det-1`: per channel-second real30 6.04 to 6.37 ms, live2 6.60 to 6.97 ms, the contest 3.05
  to 3.12 ms; fixed cost on real30 13.3 to 20.7 ms per s of stream (spectrum 6.1 to 11.7, detector 1.9 to 2.9); all CPU per
  s of stream real30 73 to 121 ms, live2 294 to 603 ms, the contest 252 to 282 ms.
- **More tracks on real recordings.** real30's tracks 1 159 to 2 225 and channel-seconds 21 671 to 34 249, live2's 241 to 473
  and 2 758 to 5 363: the finer bins see weak signals and spurs the decoder cannot read, a retired place's second chance opens
  another, and each costs a track until the judge retires it.
- Stations closer than D = 30 Hz share a channel, as before at 47 Hz. At 20 to 45 Hz the decoder reads a station badly beside an
  equal or stronger neighbor even when told its frequency.
- Beside a keyed neighbor 20 dB or more stronger at 60 Hz (on the test set's cells; about 30 dB in review 8's single case, A.8e)
  the neighbor's skirt fills the valley and the station fails the prominence test.
- The sideband test can delay or block a real station whose mirror image about a much stronger station holds another station
  at about its level. Measured (review 8, 48 kHz): W1AW at 1 000 Hz and VE3XYZ at 1 300 Hz, both +15 dB S500, either side of
  K9ZZ at 1 150 Hz, +12 dB re them: VE3XYZ's track was born at 8.3 s, against 1.7 s with the test off or with VE3XYZ at
  1 330 Hz, and its first "CQ DE VE3XYZ" was lost; 100 Hz either side of a neighbor +15 dB re them, neither got a track in 24 s
  (both read junk there without the test). How often a band holds that geometry is not measured.
- A chirping strong station's sidebands are not symmetric and may open tracks; the generator has no chirp.
- The blocker spoils a keyed station within about 0.02 to 0.1 Hz of 0 Hz (review 8: text lost after about 10 s at 0 Hz, partly at
  0.02 Hz, whole from 0.1 Hz), and a radio's offset, or a change in it, takes about 10 s to
  leave: a strong one opens a short track at the center at the start. Without the guard, real30's raised noise within about
  ±100 Hz of its center (about +3 dB, measured) opens short tracks there that the judge retires (64 tracks, 950 channel-seconds
  in 30 min).

### Copies of a neighbor (segmental path; `Engine::is_copy`, `keying_coherence.hpp`)

**Exact form.** `apply_path_defaults` sets `EngineConfig::copy_window_s` = 6 s for `FrontEnd::Segmental`; the test runs only on the
detector's path with `judge_tracks`, never in oracle mode.

- **What each channel keeps.** After each live block (21.33 ms: 32 channel samples at 1 500 samples/s; not a replay's),
  `SegmentalDecoder::block_envelope_fs2` is the mean of e_k² over that block's envelope samples at the leading stream (8 at m = 1,
  Δ = 2.667 ms), FS², and `block_noise_fs2` that stream's hiss, N₀ × σ_y²/N₀ (74.50 Hz at m = 1, 26.0 Hz at m = 3), FS², without
  the leak floor. The engine keeps the last n = round(6 s / 21.33 ms) = 281 values (`EnvelopeHistory`, numbered by the block's
  place in the stream; a block missed starts it again) and the latest hiss σ_W².
- **When.** At each channel's once-a-second verdict, before the judge's (`judge_channels`).
- **Against what.** Each other channel S with: a lower track id (older; `copy_older_only`), `TrackJudge::proven`
  (`copy_source_proven`), |f_S − f_W| ≤ 150 Hz (`copy_radius_hz`, the detector's frequencies), L_S − L_W ≥ 15 dB
  (`copy_level_db`, the detector's levels, dB re its floor per fine bin), and both histories full and ending at this block.
- **The test.** x = e less the mean of m = round(0.5 s / 21.33 ms) = 23 blocks around it (centered, slid inward at the window's ends;
  `copy_detrend_s`); ē the plain means over the window.

  ρ = Σx_W x_S / √(Σx_W² Σx_S²),  β = Σx_W x_S / Σx_S²,  own = (ē_W − E) / E,  E = β ē_S + σ_W².

  W is S's copy when ρ ≥ 0.5 (`copy_rho`), β ≤ 1 (`copy_gain_max`) and own ≤ 0.25 (`copy_own_snr_max`; negative turns the
  condition off). An unknown hiss (σ_W² not yet positive) is never a copy.
- **Then.** W retires as `TrackEnd::Copy`, hold 0 s: `SignalDetector::retire` keeps no record, and a peak there may open a track at
  once. Its text stays in its row; a row not yet shown is dropped, as a merged one's, unless it holds text not heard from a
  neighbor (`TrackTable`; "A neighbor's text", below).

**Derivations.**

- **A copy's ρ.** W's channel hears S through g = |H_ch(Δf)|² |H_f(Δf)|², the channel and front filters at the offset Δf. Its
  envelope power per sample is |√(g P) k + n|², k ∈ {0, 1} S's key (key-down a fraction d of the time), n complex Gaussian of power
  σ²: E[e² | k] = g P k + σ², Var[e² | k] = σ⁴ + 2 g P k σ². With S = g P / σ² (the copy's key-down SNR at W's filter), S's
  envelope taken as its key and q independent noise values averaged in a block:

  ρ = S √(d (1 − d)) / √(S² d (1 − d) + (1 + 2 S d) / q),  = S / (S + 2) at d = ½, q = 1:

  0.33 at S = 0 dB, 0.5 at +3 dB, 0.83 at +10 dB (derived; the unit test checks −6 to +10 dB within 0.02). A block averages 8
  samples of a filter about 4.5 samples long at m = 1, so q is about 2 (an estimate), which raises ρ at a given S.
- **An independent station's ρ.** Mean 0, standard deviation 1/√N for N independent values (derived; the unit test, 300 and 3 000
  values, within 15%). For keyed signals N is about the number of elements in the window (an estimate): PARIS is 50 dots long and holds 28
  elements, so about 70 in 6 s at 25 WPM, 1/√N = 0.12, and ρ = 0.5 stands about 4.2 standard deviations above it.
- **The high-pass.** Callers who start together after a CQ share a step. With the plain mean, two independent keys silent for
  5 s of 8 and then keyed at d = ½ give ρ = (3/8 × ¼ − (3/16)²) / (3/8 × ½ − (3/16)²) = 0.385 (derived); less the 0.5 s moving
  mean only the blocks within about ±0.25 s of the step keep it (unit test: under 0.15).
- **β ≤ 1.** For a copy β = g, and g ≤ 1 since the two channels' filters are alike and S sits at its own channel's center, where
  they are flat (derived). A strong channel whose station is silent hears the weaker W at g′; read the other way, W on S, that is a
  gain of 1/g′ > 1.
- **own.** For a copy ē_W = g ē_S + σ_W² in the linear model, so own = 0 at any SNR (derived; exactly −β σ_S² / E, since ē_S holds S's own hiss σ_S², slightly negative and negligible for a
  strong S); a station of W's own adds its mean
  power, and own is its SNR against the leak and hiss together.

**Provenance.** Decision log, 2026-11-15. Measured on the Linux machine against `main` (a34069e), scratch tables in `build/det2`,
not committed.

- **The cause** (the owner's recording of 2026-11-04, `--mirror`, its tracks 7, 533 and 645, at 9 847, 9 897 and 9 790 Hz from the
  center): track 7 is K4TZ, a CWT runner, 39.2 dB re the floor per fine bin. Over 12 to 87 s, K4TZ alone, its averaged spectrum
  stands −19.2, −21.7, −23.1 dB re its peak at −41, −47, −53 Hz and −21.4, −22.7, −23.8 dB at +41, +47, +53 Hz (symmetric within
  1.3 dB), −24.2 and −23.2 dB at ±70 Hz: a skirt with no peak, which neither the prominence nor the sideband test lets through.
  Track 533 opened at 90.1 s on a caller, its place then 39.6 dB re the floor, 0.8 dB above K4TZ's (K4TZ silent): a real
  station, by the prominence and sideband tests rightly. Track 645 opened at 136.1 s 22.3 dB re the floor, 10.3 dB below K4TZ's
  place and 22.2 dB below the caller's 95 Hz away: the sideband test takes the highest bin within 150 Hz, the caller's, as the
  carrier, whose mirror is empty, so a sideband of K4TZ's or a weak caller passes. Neither was a second chance (no retirement
  within 200 Hz before them). Both then lived to 242 s on the judge's Morse, since their 150 Hz channels and the 12 ms filter
  (its first null at about 83 Hz) hear K4TZ and every caller 40 to 110 Hz away: the same text three times. CW Skimmer spots K5SJC at
  7035.0 kHz calling CQ; at 7029.8 kHz it was, by conjecture, one of K4TZ's callers.
- **Rules tried in order.** ρ alone (≥ 0.5, older only): it retired a real caller keying under a neighbor 8 dB stronger (the unit
  test's pile-up: ρ 0.5 to 0.6, β 0.1) and, on the slice, the pairs went 0.150 to 0.166, E2 0.533 to 0.582, the contest 220 to
  219 true calls. β ≤ 1 alone did not help (β 0.1 there: the leak is real, at about −9 dB re the caller's own channel). The own
  power: on a never-retiring traced run (ρ ≥ 1), 1 666 decisions on copies (the ghost cells below: B silent during A's over) and
  4 302 on stations keying (pairs, stress cells, E2 seeds 1–2, det-1's cells), own's quantiles at ρ ≥ 0.5 were for copies 5%
  −0.02, 50% 0.09, 95% 0.65, and for keying stations 5% 0.15, 50% 0.49: the copies' upper tail is the model's own mismatch (the two
  channels' leading filters differ, so W is not quite a scaled copy of S block by block), largest at high SNR. Of the keying
  stations' would-be retirements, nearly all stood 5 to 25 dB below a neighbor 20 to 60 Hz away; 15 dB removes them all but in
  det-1's cells (0.35% of decisions, against 10.8% at 6 dB, own 0.25) and keeps 74% of the copies' decisions. The source
  proven: det-1's carrier cells (a steady carrier 20 to 30 Hz from a station, there first) read 0.601 to 0.623 without it: a track
  on the carrier, 26 Hz from the station, hears the station's keying, and the only track reading the station, 42 Hz the other side,
  was retired as its copy. Older only: without it (an earlier form, ρ alone) a runner's track (W8CAR, the owner's file) was
  retired as the copy of a louder caller's during the caller's over, and W8CAR got no new track for 70 s, its peak failing the
  prominence test beside the caller's lingering average, while the caller's track read W8CAR's CQs.
- **Values.** own 0.5, 0.25, 0.15 and 0.1: the ghost cells' copied share of A's over 2.0%, 2.6%, 6.0% and 10.8% (the last without
  the source and level conditions); the contest's true calls 219 and 233 (two seeds) at 0.5, 221 and 236 at 0.25, 221 and 237 at
  0.15, against 220 and 236 on `main`. 0.25: heuristic, chosen by measurement. Level 6, 12, 15 dB at own 0.5: det-1's cell of a
  keyed neighbor there first (45 to 60 Hz, 10 to 20 dB re wanted) 0.681 on `main`, 0.945, 0.945, 0.687; 15 dB: heuristic, chosen
  by measurement. ρ 0.3 to 0.7 retire nearly the same decisions once own and the level apply (heuristic). 6 s, 0.5 s, 150 Hz and
  the hold of 0 s: heuristics, not varied; 150 Hz the channel's half width at −6 dB re the passband; no hold since a QSO partner
  answers within a second or two of the end of the over its track was retired in.
- **Simpler rules, measured as baselines** (the owner's question, 2026-11-15): a spacing of 47 or 60 Hz between tracks in place of
  30 Hz (`attribution_distance_hz`), and a level-scaled exclusion (`skirt_exclusion_db` 12 dB, `skirt_exclusion_hz` 30 Hz,
  below). Against the copy test: the spacing loses answering stations (ghost cells: B's call read in 46 and 36 of 60 QSOs at 47
  and 60 Hz, 60 with the test) and real calls (47 Hz: the contest's true calls 220 to 219, f11's 42 to 41, W5XX on N2GZ, all
  confirmed or true; 60 Hz: the contest's 220 to 218 and 236 to 234, w11's 40 to 38, IK6QOP on real30 and N4ZZ on the owner's
  file, confirmed); the exclusion retires the runner's own track while its caller sends (A's track in 59 of 60 QSOs) and loses
  true calls on the contest (220 to 218, 236 to 230) and K4RUM (confirmed) on KC2LRC-2. Neither is kept; both are variants, off.
- **The level-scaled exclusion's form** (`DetectorConfig::skirt_exclusion_db`, off): no track, new or standing, within
  d(ΔL) = 30 Hz × 10^((ΔL − 12 dB)/20) of a track ΔL dB stronger, at most 150 Hz. The 6 dB per doubling of the distance is a keyed
  carrier's 1/f² skirt in power (derived for hard keying), measured on K4TZ's: −18 dB re its peak at 30 Hz, −24 dB at 60, −29 dB at
  100; 12 dB is that skirt at 30 Hz less a 6 dB margin, as the prominence test's (heuristic).

**Limitations.**

- A copy prints for the window, 6 s, after it starts (and its track's replay of 2 s) before it retires: its first words stay in
  its row. A copy whose place is still a peak opens again and retires again 6.5 s later.
- Copies with own above 0.25 stay longer: in the ghost cells 2 of 60 QSOs kept over 10% of A's over, both weak: A at 5.6 and
  10.0 dB S500, the copy at +8.3 and +2.5 dB SNR at B's filter (β −2.8 and −11.5 dB re A's own channel); the first was retired
  after 14% of the over, the second never. At low SNR the noise's share of a copy's mean power makes own uncertain.
- A copy is retired only as an older track's; a runner's track still prints a louder caller's over (the owner's file: K4TZ's
  track reads its callers), though symbol by symbol it is now given to the caller's track once that is proven ("A neighbor's text").
- A station 15 dB or more below an older, proven neighbor within 150 Hz, keying under its leak with little power of its own, is
  retired as its copy (on the slice, det-1's cells only, 45 to 60 Hz, 15 to 25 dB below, where it reads CER 0.7 to 1.0
  anyway); its place stays open.
- CPU (the Linux machine, one thread, `main` and `det-2` side by side): per channel-second +0.01 to +0.16 ms (real30 6.60 to
  6.62 ms, the owner's file 6.29 to 6.45 ms, process CPU), per second of stream −4% to +3% (real30 0.126 to 0.129 CPU-s, the
  owner's file 0.390 to 0.380): the test is about 281 products per pair a second; the copies' channel-seconds it saves pay for it.
- A copy is a second reading of its source's text, through another filter: on KC2LRC-2 a copy 48 Hz from N4BP was the only track
  to read N4BP's "KI4MZC 5NN", and KI4MZC (spotted by the RBN that day, outside the recording's window) is not listed with the test.
- Until it retires, a copy's text is now mostly given to its source (below, "A neighbor's text"), so its row shows little of it.

### A neighbor's text (segmental path; `Engine::attribute`, `DecodedSymbol::heard_from`)

**Exact form.** `apply_path_defaults` sets `EngineConfig::attribute_pad_s` = 0.5 s for `FrontEnd::Segmental`; it runs only on the
detector's path with `judge_tracks`, never in oracle mode.

- **What each channel keeps.** `Channel::heard`, an `EnvelopeHistory` of round(30 s / 21.33 ms) + 1 = 1 407 values
  (`attribute_history_s` 30 s), the copy test's e (FS², one a block at the leading stream), pushed after every live block and after
  each block of the channel's replay, numbered by the block's place in the stream, so that a replay's symbols have their blocks.
- **When.** In `Engine::publish_update`, on the engine's thread after every channel's block, before the text event goes out: every
  symbol of the update's `chars` and of each correction's `chars`, a correction's re-sent symbols again.
- **Against what.** Each other open channel S with |f_S − f_W| ≤ 150 Hz (`attribute_radius_hz`, the detector's frequencies) whose
  history holds the window; proven or not (`attribute_source_proven` false; true, a variant, asks `TrackJudge::proven`).
- **The window.** The blocks b, 21.33 ms each, with ⌊(start_s − 0.5 s)/21.33 ms⌋ ≤ b ≤ ⌊(end_s + 0.5 s)/21.33 ms⌋, clipped to
  those W holds (the newest is the current block); nothing is attributed with fewer than round(0.5 s / 21.33 ms) = 23 blocks
  (`attribute_min_s`), and a word space never is.
- **The test.** `keying_coherence` over the window, as the copy test's (the high-pass's 23 blocks included): ρ, β, ē_W, ē_S, and the
  keying powers K_W and K_S, the mean of x² (FS⁴). S owns the symbol when ρ ≥ 0.5 (`attribute_rho`), K_W < K_S and own = (ē_W −
  E) / E ≤ 0.5 (`attribute_own_max`; negative turns the condition off), E = β ē_S + σ_W², σ_W² W's latest hiss; of several, the one of
  largest K_S. An unknown hiss attributes nothing.
- **Then.** `DecodedSymbol::heard_from` = S's track id; else 0. Neither the decoder nor the judge reads it.
- **The list** (`TrackTable`) keeps every symbol with its times, its `heard_from`, and whether it is left out. A *stretch* is a run of
  consecutive symbols with a `heard_from`, no word space among them; it is left out whole when every symbol of it has a twin in the
  listed row that holds the named track (`shown_by`: the same text, start and end each within `kTwinS` 0.025 s, the twin not left out
  itself), else not at all (`refresh`). It is decided again when the row's text changes, when a row is listed (`now_listed`, every row),
  and when a listed track's text comes, for the rows within 300 Hz and 30 s of it (`kRecheckHz`, `kRecheckS`). A row shows the rest, one
  word space for each stretch left out (none at the start, none beside a space), and a hidden row is listed on `text_evidence` of what
  it would show. A hidden row that ends as `Copy` waits, as a `Faded` one does, when a symbol other than a word
  space has `heard_from` 0; with none it goes at once, as before.
- **The callsign stage** reads every symbol (`CallSearchConfig::skip_neighbors_text` 0, the default; review 18). A variant, 1, skips a
  stretch by the same twin rule against the named track's text in the stage (`neighbor_twin_s` 0.025 s), and only when it is whole
  words, never part of one (A.12). The list does not ask whole words (below, provenance).

**Derivations.**

- **Louder at its own channel.** A keyed station of key-down power P heard by a channel at power gain g = |H_ch(Δf)|² |H_f(Δf)|²
  (Δf its offset from the channel's center), keyed a fraction d of the time, with complex Gaussian hiss of power σ² at the filter's
  output: the envelope power e (FS², section 6) has, per value, variance g²P² d(1 − d) + σ⁴ + 2gPσ² d (derived, as the keying-power unit test's
  comment has it). The key term and the cross term 2gPσ²d both grow with g, and σ⁴ does not depend on it, so at equal hiss and the
  same leading filter K is monotone in g and K_S > K_W ⇔ g_S > g_W (derived; the moving mean takes about the same share from both). A
  channel's g falls with the offset while it lies in the front filter's main lobe (the 12 ms filter's first null is about 83 Hz out;
  beyond it, and between channels whose hiss or leading filter differ, the equivalence can fail): K_S > K_W says the station is nearer
  S's center. Then β = ρ √(K_W / K_S) < ρ ≤ 1: the copy test's β ≤ 1 follows. Of several channels that pass, the one of largest K_S is
  the one centered nearest the station, its own (derived, as above), when that one passes the test at all (below, Limitations).
- **Antisymmetric.** Read from S's side the condition is K_S < K_W: over one window, of two channels at most one gives the stretch to
  the other (derived). Two decoders' symbols of one keying lie within a few ms of each other, so their windows nearly coincide.
- **own** as the copy test's: about 0 while W's own station is silent, at any SNR (derived); a station of W's own adds its SNR against
  leak and hiss. Over a symbol's window, not 6 s, the filters' mismatch spreads it more (A.6, copies' provenance), hence a looser bound, 0.5 set by measurement.

**Provenance.** Decision log, 2026-11-17. The measure is *text shown twice* (A.11): over pairs of proven tracks, symbols printed by
both at the same times, counted shown when the list's rule above leaves neither out. Runs of `kz4ap-calls`, one thread each, ten at
once, on the Linux machine (c18c087, the five real recordings and the generated call sets; scratch tables in its `build/copy2`, not
committed), and on the desktop (the owner's file, and the app on it).

- **The cause** (the owner's file, the copy test on): the app's row at +9 885 Hz from the center, four tracks of K4TZ's callers
  (536, 601, 643, 652, each retired as a copy within 5.6 to 12.3 s), read "E4TID KK4E KK4RE TOM 1 N3 KK4E DON 3654 KK4ETUK4TZ K8WWS
  K: TZ VE3NE VE3NE TOM 1993 N93 LALI 128 TU K4TZ CQ C": the callers' own calls and exchanges, and K4TZ's "TOM 1993" and "TU K4TZ CQ"
  38 to 50 Hz away. K4TZ's own row (track 7) read the callers' "KK4E DON 3654", "VE3NE", "LALI 128" in turn.
- **Standard practice.** Crosstalk detection in multichannel meeting recordings: a stretch on one channel is another talker's when
  the two channels' signals correlate there and the other channel holds it louder (Pfau, Ellis and Stolcke 2001, ICSI meeting
  recorder; Wrigley, Brown, Wan and Renals 2005, IEEE Trans. Speech and Audio Processing). Here the copy test's ρ and own power on a
  symbol's stretch, and K_S > K_W as "louder". CW Skimmer's own rule for this is not documented (not verified).
- **own 0.25, 0.5, 1.0** (the owner's file, sources proven, every attributed symbol counted hidden, the first form): shown twice 747
  (off) to 228, 179, 142; tracks showing it 8 times or more 49 to 15, 7, 3. At 1.0 W4RN's own call ("W 4") was hidden, no other row
  showing it. 0.5: heuristic, chosen by measurement.
- **The twin, and stretches whole.** In the first form every attributed symbol was left out and skipped. On the strong pairs' fresh
  seed that lost UA8B (a true call): its own track, 11 dB re the floor per fine bin, read "CQ CQ DE UA8B UA8B NR 339 339 K" cleanly,
  and its symbols were given to a track 38 Hz away that heard its keying more strongly (K_S > K_W) and read it as junk beside a
  stronger neighbor ("CQ CQLQA8<BK>A8/NA,63 353 K"): a second reading better than the source's. A stretch is now left out (and skipped, in the stage's
  variant) only where the source holds every symbol of it; UA8B came back. Twins' offsets (the owner's file, symbols attributed with a same-text symbol on
  the source within 1 s): 83% within 10 ms, 86% within 25 ms, 88% within 50 ms (measured); 25 ms is the duplicate rule's (heuristic).
- **Sources any, not only proven.** With the twin rule a source's text must be the same to leave a stretch out, and the list asks the
  source's row to be listed: the owner's file's shown twice 233 with proven sources only, 178 with any (both with the choice by
  highest ρ below; 179 by largest K_S). Any: heuristic, chosen by measurement.
- **The loudest owns it.** Of the first form's choice by highest ρ, a caller 57 Hz below K4TZ on the owner's file (track 655) had
  K4TZ's "TU K4TZ CQ" given to another caller's track, 95 Hz from it, which heard K4TZ too; by largest K_S it goes to K4TZ's, which
  holds it, and the row reads "3 LALI P K".
- **The default, the five real recordings, off to on** (6dd5c49; the engine's text and attributions as at c18c087): shown twice 747 to
  181 (owner), 611 to 193 (W3OA), 556 to 153 (KC2LRC-1), 331 to 102 (KC2LRC-2), 924 to 285 (N2GZ), 68% to 76% fewer; tracks showing it
  8 times or more 49 to 10, 40 to 16, 42 to 7, 28 to 5, 56 to 27. Symbols left out on proven tracks 526, 415, 403, 229, 638, none with
  no shown twin. **What the measure does not see** (A.11): symbols given to a proven neighbor and still shown, on proven tracks,
  301 of 827 (owner), 1 044 of 1 459, 333 of 736, 288 of 517, 819 of 1 457; of them 172, 754, 193, 148, 479 have no symbol of the same
  text on the neighbor within 1 s: the neighbor's keying read otherwise on the weaker row, still shown there. The headline is exact
  twins, not every trace of a neighbor the owner may see. The owner's +9 885 Hz row in the app (desktop): "E4TID KK4E N3 KK4E DON
  3654 K K: VE3NE N93 LALI 128".
- **Stretches, not whole words, in the list:** asking a left-out stretch to be whole words (as the stage's variant does) leaves more
  shown twice: 181 to 265 (owner), 193 to 218, 153 to 163, 102 to 121, 285 to 320. The list keeps stretches (chosen by measurement);
  fragments such as "K K:" stay where part of a word is the neighbor's.
- **Counts reconciled** (review 18's independent count: 181, 526 left out, 0 with no shown twin): c18c087's figures (179, 529, 2)
  came from a measure that did not ask the twin on the source's row not to be left out itself, as `shown_by` does; with that
  condition they are the review's. The review's 171 against 172 (owner) symbols with no same-text symbol within 1 s, and 748 / 191 /
  475 against 754 / 193 / 479 elsewhere, differ at that window's edges (not traced further).
- **Calls** (the RBN judge, A.12): with the stage reading every symbol (the default since review 18) every reported call and every
  field of it is the same with attribution on and off: the owner's file 41 / 21 / 20 (calls / confirmed / unconfirmed), the four 2017
  recordings 417 / 200 / 217, the generated sets 1 077 true and 3 false calls. The stage's skip, a variant, changed fields (A.12); its
  first form also changed five calls, none confirmed (W3OA N4I gone; KC2LRC-2 W6YX, spotted that day, gone and K5X come; N2GZ K4T for
  K4N).
- **0.5 s, ρ 0.5, 23 blocks, 30 s:** heuristics, not varied; ρ the copy test's; 30 s past the decoder's corrections' reach, 20 s.

**Limitations.**

- Per symbol: a word can be split, part shown and part left out (K4TZ's row on the owner's file: "4" and "VE" of the callers'
  KK4E and VE3NE).
- A stretch the source reads otherwise stays on both rows, read two ways; a stretch whose source's row is never listed stays too.
- The loudest passing neighbor is not always the station's own: in a unit scene (a runner, callers 30 and 60 Hz above, tried for
  review 18) the 60 Hz track's copy of the runner went mostly to the 30 Hz track: the runner's track did not pass the test there, or
  did not hold the window (not traced). The list then leaves it out where
  that track's row holds it, so the text shows on the runner's row and on the nearer copy's.
- A correction re-sent after the source's channel closed is not attributed again: what it re-sends shows on W's row.
- The judge (section 10), the callsign stage by default and the benchmark's scores read every symbol: the test set's scores and the
  call list do not change by construction.
- CPU (the Linux machine, one thread, ten runs at once, off and on side by side, process CPU, user and system): 1 475.84 to
  1 469.31 CPU-s over the five recordings' 240 768 channel-seconds, 6.130 to 6.103 ms per channel-second, each recording within
  ±1.3% (owner 6.45 to 6.48 ms, N2GZ 6.47 to 6.39 ms): a symbol's test is about 60 products per neighbor, 2 to 3 symbols a second.
  The app on the owner's file (desktop, 3 threads, full speed): 6.40 to 6.38 ms per channel-second.
## A.7 Channelizer

### Exact form

The rules of this section as the code implements them (moved verbatim from the body on 2026-10-05, when the body was rewritten for reading).

For each track: a channel centered on f_c, the track's birth frequency
rounded to the nearest bin (so the station is within ±11.7 Hz of 0 Hz in
the channel); in effect (1) mix down by e^(−j2πf_c t), (2) low-pass,
(3) decimate by D. The channel opens when the track is born and closes
when it dies; the decoder never sees signal from before the birth. The
channel of Envelope, Matched and the bank does not move; those of viterbi and
segmental follow the detector (below).

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
  by the detector's frequency (section 8c).

#### Frequency re-centering (Matched decoder only)

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

#### Following the detector (viterbi and segmental only)

`ChannelFollower` (`engine/include/kz4ap/channel_follower.hpp`), one per channel of `FrontEnd::Viterbi` and `FrontEnd::Segmental`, driven by the
engine (`Engine::process_hop`). The other decoders' channels are not followed (plan O2, the coordinator's ruling: the
bank stays the frozen reference, whose replay decodes fixed-bin channel streams; Matched is left as it is), and their
channel streams are bit-identical to the fixed-bin channelizer's (tested, `Engine.OtherDecodersChannelsAreUnchanged`).

- **The detector's frequency f_det** (Hz from the span's center), per channel block of 32 samples starting at
  t₀ = first index / r: on the detector path the detector's track frequency after the hop's frame (the track's own,
  section 6); in oracle mode the label's (plan O3): `EngineConfig::oracle_frequency_tracks[i]`, a list of segments
  (start_s, frequency_hz, drift_hz_per_s, drift_origin_s), f_det = frequency_hz + drift_hz_per_s·(t₀ − drift_origin_s)
  of the last segment with start_s ≤ t₀; without a track, the constant `oracle_frequencies_hz[i]`. f_det is held over
  the block (21.33 ms, the detector's own update interval). The bench gives a single station one segment from 0 s (its
  label's freq_offset_hz at its start_s and its drift from there, the generator's angle 2πft + πḟ(t − start_s)²), and a
  QSO label one segment per transmission from its start_s (the first from 0 s) at freq_offset_hz + the sender's
  offset_hz with the QSO's drift from the QSO's start_s, held through the silence after each over; a per-station label
  of a drifting QSO is refused (its start_s is not the QSO's).
- **Order, per block.** (1) The block, cut at bin c (center f_bin = c·Δf), is shifted (below) with f_det; (2) the coarse
  rule picks the bin for the next block (`Channelizer::set_center_bin`, applied when the next block starts); (3) the tap
  and the decoder receive the shifted block; the decoder's N₀ is read at c; the track's reported frequency is f_det. An
  oracle channel opens at the bin nearest f_det(0).
- **Coarse rule** (`bin_for`): keep c while |f_det − f_bin| ≤ Δf/2 = 11.71875 Hz (Δf = 23.4375 Hz at 48 and 192 kHz;
  derived: within half a bin c is the nearest bin), else the bin nearest f_det (`hz_to_bin`, ties away from zero). No
  hysteresis.
- **Fine shift** (`shift`): y′_n = y_n·e^(−j2πφ_n), φ advancing by (f_det − f_bin)/r cycles per sample (kept in [0, 1)),
  the phasor computed from φ at each sample (cos and sin, so nothing accumulates from sample to sample); one complex
  multiplication per sample.
- **Phase rule at a move.** When f_bin changes from f₁ to f₂ at a block starting at t₀, φ ← φ + (f₁ − f₂)(t₀ − D_g)
  cycles, D_g = (N/2)/2/fs = 1/(4Δf) = 10.667 ms, the channel filter's group delay. Then the total demodulating phase
  seen by the station, 2πf_bin(t − D_g) + 2πφ, is continuous, and between moves it advances by 2πf_det/r per sample.


**Derivations.**

- **Steps.** For a station at center frequency f_c (the track frequency
  rounded to the nearest bin, so within ±½Δf = ±11.7 Hz of it): (1) mix
  down (complex, I/Q) by exp(−j2π·f_c·t), a complex frequency shift, not
  an audio demodulation: the result is still complex I/Q, centered on the
  station; (2) low-pass filter (the channel filter); (3) keep every D-th
  sample. So the channel filter is applied after the shift and before
  decimation, and the decoder receives the filtered stream.
- **Decimation.** D = N / channel_bins = 8192 / 64 = 128 (N = 2048, D = 32
  at 44.1 kHz, r = 1378 samples/s). Its purpose is computational: the
  decoder handles 128× fewer samples, and in overlap-save producing the
  decimated output directly (a 64-point inverse FFT instead of an
  8192-point one) is also cheaper than producing the full rate. It loses
  nothing because the filtered signal has no significant content outside
  the output band: a complex stream at r samples/s represents
  −r/2 … +r/2 = ±750 Hz without folding negative onto positive
  frequencies, and the filter's stopband begins at about ±280 Hz. The
  bounds on channel_bins: r/2 must be at least the filter's stopband edge,
  cutoff + ½·transition width (150 + 129 = 279 Hz with the 258 Hz
  estimate; the code enforces this, below); timing resolution 1/r =
  0.67 ms must be far below the shortest element, a 20 ms dit at 60 WPM;
  channel_bins must be even and divide N (the code rejects anything else).
  64 bins satisfies these with a wide margin.
- **Overlap-save.** One N-point FFT per hop, shared by all channels
  (unwindowed; the N/2 earlier samples of history provide the overlap).
  For each channel, take the 64 FFT bins centered on f_c, multiply by the
  filter's frequency response, and inverse-FFT those 64 bins: selecting
  bins around f_c is the frequency shift; the multiplication is the
  filter; using 64 bins instead of N is the decimation. Keep the last 32
  of the 64 output samples (the rest are corrupted by circular
  wrap-around), then correct a phase term so each channel's phase is
  continuous from block to block. The cost is one large FFT per hop plus
  one 64-point inverse FFT per station, instead of a mixer and filter per
  station per input sample. The result is identical to steps 1–3 up to
  floating-point rounding and the filter response beyond ±750 Hz, which is
  discarded (verified by the unit tests and an independent hand derivation
  in review).
- **The filter's response in frequency** (measured with steady tones
  through the Channelizer, relative to the 0 Hz response; the same values
  follow from the tap formula directly, computed independently in review):

  | offset | 0–30 Hz | 50 Hz | 75 Hz | 100 Hz | 125 Hz | 150 Hz | 200 Hz | 250 Hz | 280 Hz | 290–400 Hz |
  |---|---|---|---|---|---|---|---|---|---|---|
  | response | 0.00 dB | −0.05 dB | −0.34 dB | −1.17 dB | −2.93 dB | −6.02 dB | −18.0 dB | −43.9 dB | −75.5 dB | −79 to −84 dB |

- **Transition band (computed from the taps).** By the Blackman window's
  own criteria, the passband (response within 0.0017 of 1, i.e. 0.015 dB)
  runs to about 39 Hz, and the stopband (at least 74 dB below the
  passband) starts at about 279 Hz: a transition band about 240 Hz wide.
  The usual Blackman estimate, 5.5 / (filter length in seconds) =
  5.5 / 21.3 ms = 258 Hz, is slightly wider, so it is a safe bound. The
  code uses that estimate as ±129 Hz around the cutoff; it puts the
  stopband edge at 150 + 129 = 279 Hz, which matches the computed edge.
  (The computed passband edge sits at 150 − 111 Hz, so the band is not
  exactly symmetric about the cutoff.) The noise bandwidth, ∫|H(f)|² df
  over all f, computed from the taps, is 252 Hz: the bandwidth of the
  noise the decoder sees.
- **Aliasing check.** The channel keeps only bins within r/2 = ±750 Hz of
  its center, so the filter's whole transition band must fit inside that.
  The constructor requires r/2 ≥ cutoff + ½ · 5.5 / (taps / fs)
  (5.5 / (taps / fs) is the Blackman transition width), and throws
  otherwise. With the defaults that allows cutoffs up to about 621 Hz. (It
  used to check only cutoff < r/2, but the cutoff is the −6 dB point, not
  the stopband edge: a 700 Hz cutoff passed that check with its stopband
  starting near 829 Hz.)
- **The filter in time: one dit through it** (a 25 WPM dit, a 48 ms burst
  of carrier, measured by pushing a hard-keyed carrier through the
  Channelizer and measuring |y|). The impulse response lasts
  (N/2 + 1)/fs = 21.3 ms (32 output samples), shorter than the dit. The
  whole dit comes out 10.7 ms late (group delay, (taps − 1)/2 / fs):
  measured 10.66 ms at both the leading and trailing half-amplitude points.
  Because the filter is linear-phase, this is a pure shift: every
  frequency is delayed equally, so the pulse is not smeared asymmetrically,
  and its width at half amplitude comes out exactly 48.0 ms. Decoded
  timestamps include the delay. Each edge rises from 10% to 90% of the
  amplitude in 3.1 ms (measured 3.14 ms; the rule of thumb 0.44/B with
  B = 150 Hz estimates 2.9 ms). The edges ring by about 5% of the amplitude
  on each side of the edge (measured peak 1.052 A after the edge; the
  filter's symmetry puts the same ripple before it): the price of the
  fairly sharp cutoff, small next to the Envelope decoder's thresholds
  (40–60% of the way from space to mark level). What limits timing on the
  Envelope path is its smoothing (section 8): a first-order filter with
  τ = ¼ dit (12 ms at 25 WPM), whose 10–90% rise time is τ·ln 9 ≈ 26 ms,
  eight times the channel filter's.
- **Scaling with fs.** The cutoff is set in Hz and the filter length
  N/2 + 1 scales with fs (N ∝ fs), so the filter's response in Hz is the
  same at every 48 kHz × 2^j rate. (At 44.1 kHz the filter is 23.2 ms long
  and its transition band about 237 Hz.)
- **The cost of a residual offset.** A filter matched to an element of
  duration T_el (noise bandwidth exactly 1/T_el for a rectangular element)
  scales a tone offset by f_off (Hz) by |sinc(f_off·T_el)| in amplitude,
  sinc(x) = sin(πx)/(πx) (from Proakis & Salehi, *Digital Communications*,
  5th ed., eq. 4.5–28; `docs/research/proakis-ook-notes.md`, section 2.7).
  At ±11.7 Hz a dit-matched filter would lose 5.1 dB at 25 WPM and 8.8 dB
  at 20 WPM (signal power, relative to a centered station). So a narrow
  filter needs each station re-centered to a fraction of a bin first: the
  Matched path does this with its tracker ahead of its dit-matched filter;
  the Envelope path does not re-center, which is harmless only because the
  channel filter is wide; the planned narrow second-stage filter would
  need the same re-centering in front of it.

**Derivations: frequency re-centering (Matched).**

- **NCO.** φ runs on through every change of f̂ (the tracker's updates,
  `set_anchor` jumps, `reacquire`); only `reset` sets it to 0. The engine
  starts each new track's NCO at the detector's residual, track frequency
  minus channel center (in oracle mode, 0 Hz: the channel sits on the bin
  nearest the labeled frequency and the tracker must find the rest; its
  anchor is the labeled frequency itself, so it fine-tunes within ±12 Hz
  of the label). Beyond ±75 Hz the channel itself would have to move,
  which the code does not do.
- **Discriminator.** On the narrow-filtered, re-centered stream v[n], the
  product z = v[n]·conj(v[n − τ_L·r]) has phase 2π·(f_off − f̂)·τ_L for a
  station at f_off. Rotating it by e^(j2π·f̂·τ_L) makes it a measurement of
  f_off itself, so the average does not depend on the NCO and there is no
  loop to stabilize (to first order: v averages samples mixed under the
  last K/32 NCO settings while each product is rotated by the current f̂, a
  small coupling while f̂ moves, harmless because τ_f·r = 750 samples is
  much longer than K/2). The lag is configured as 5.33 ms and rounded to
  whole samples, τ_L = 8/r = 5.333 ms, which gives an unambiguous range of
  ±1/(2τ_L) = ±93.75 Hz; it is converted to samples from the physical
  value. The NCO range (±75 Hz) and the fine-tuning range (±12 Hz) must
  both lie below it; the constructor rejects a configuration where they do
  not.
- **Average.** Weighted by p, the Matched decoder's key-down probability,
  so key-up and pauses leave it unchanged; f̂ is updated every 32 samples
  (21.3 ms) once the average holds weight 0.6 or more and is coherent
  (|Z̄| > 0.3 × the same average of |z|; strictly greater).
- **Fine-tuning around the detector's frequency.** The tracker does not
  decide which station it follows. Its anchor f_a is set by its owner
  (`set_anchor`); the anchor never follows the tracker's own estimates.
  The ±12 Hz must exceed the detector's interpolation error (0.2 Hz
  measured, clamped to ±11.7 Hz) and stay well below the 47 Hz channel
  distance. When f_a moves more than 12 Hz from f̂ (the detector's track
  moved to another station's peak in a QSO turnover, or drifted), f̂ jumps
  to f_a and the average restarts. So the channel's station is followed
  through slow drift as far as the detector's peak goes, and a station
  more than 12 Hz from the detector's frequency can never pull the tracker
  toward it (as long as the owner keeps the anchor there). How far the
  detector's peak lags a ramp of ḟ Hz/s: its 1 s power average lags by
  ḟ·1 s (exact for the power-weighted centroid of the averaged spectrum);
  the interpolated peak equals the centroid when ḟ·1 s is small compared
  with the Hann kernel's width (about one bin, 23.4 Hz), which holds at
  1 Hz/s. A non-finite anchor, or a non-finite value given to `reset`, is
  ignored (the previous state is kept); a non-finite initial offset is
  rejected by the constructor.
- **Fresh average after a jump.** After a `set_anchor` jump or a
  rejection, f̂ changes at once, but the next τ_L·r + K − 1 products still
  contain samples of v that were mixed at the old f̂ (the boxcar spans K
  samples and the product reaches back τ_L·r = 8 samples), so their
  measurement of f_off is wrong by up to the size of the jump, and they
  bias the fresh average slightly. Their share of the average when it
  first reaches the 0.6 gate weight (an upper bound: weight 1, all of
  those products counted as fully stale) is
  (1 − e^(−m/(τ_f·r)))·e^(−(n₆ − m)/(τ_f·r))/0.6 with m = τ_L·r + K − 1
  and n₆ = τ_f·r·ln 2.5 = 687 samples: 2.8% at K = 24 (60 WPM), 6.0% at
  K = 58 (25 WPM) and 32% at K = 288 (5 WPM), decaying with τ_f
  afterward. The classical decoder does not hold off the average for
  those samples; the bias is bounded by these figures and has not been
  measured.
- **Expected accuracy.** About 0.5 Hz RMS at S₅₀₀ = 0 dB and 0.9 Hz at
  S₅₀₀ = −5 dB, 25 WPM (an upper bound), valid once a station has been
  acquired, which needs S₅₀₀ ≥ −2.5 dB at any speed (A.8b, "Squelch"); a
  linear drift of ḟ Hz/s is followed with a lag of about ḟ·τ_f/P₁ (1.1 Hz
  at 1 Hz/s, P₁ = 0.44). Target (spec §5.2): within ±2 Hz, a loss of
  0.2 dB relative to a centered station at 20 WPM through a filter of
  length T.
- **Re-acquisition.** After a silence (A.8) the decoder calls
  `reacquire()`: the average starts afresh (weight 0) from the last f̂, so
  the next station, if it is within ±12 Hz of the anchor, is found within
  about 0.5 s of key-down weight (the average needs weight 0.6); one
  farther away is reached when the detector moves the anchor.

**Derivations: following the detector (viterbi and segmental).**

- **Why D_g in the phase rule.** The channelizer demodulates before it filters: channel sample j (input sample
  n = j·D) is Σ_l h[l]·x[n − l]·e^(−j2πc(n − l)/N) (from the per-block re-reference of the inverse FFT's mixer to the
  stream's start). For a station narrow against the passband, and exactly for a steady tone through the symmetric
  filter, this is |H|·x(n − D_g)·e^(−j2πc(n − D_g)/N): the station's phase in the channel is its input phase at
  n − D_g, demodulated at that instant. Keeping 2πf_bin(t − D_g) + 2πφ continuous at a move keeps the station's phase
  continuous. Without D_g the phase would step by (f₁ − f₂)·D_g = (bins moved)/4 cycles, a quarter turn per bin
  (derived; tested: removing D_g makes `ChannelFollower.PhaseIsContinuousAcrossABinMove` fail by 1.5708 rad at each
  move).
- **The phase the hold leaves** (derived). f_det changes only when the detector's frame does: every block (21.33 ms) on
  the band's spectrum, every 85.3 ms on segmental's path, its own spectrum's frame. With f_det held for a hold of
  length T_h, a drift ḟ leaves a residual rising from 0 to ḟ·T_h: at 1 Hz/s, 0.0213 Hz over 21.33 ms (a phase of
  πḟT_h² = 1.43 × 10⁻³ rad per hold) and 0.085 Hz over 85.3 ms (0.023 rad per hold), always of one sign; scatter of
  0.2 Hz in f_det steps the phase by 2π·0.2 Hz·T_h, 0.027 rad over 21.33 ms. Both are the detector's frequency doing its job, not a defect of the shift, so the tests compare
  with an independent reference (the input delayed by D_g, demodulated by the running sum of the held f_det) and
  find the output within about 10⁻⁶ rad of it at every sample, at 48 and 192 kHz, through two moves and through a station
  parked 0.00125 Hz beyond a bin boundary that moves the channel on 1 380 of 2 812 blocks (measured, the tests'
  recorded worst error, 7.3 × 10⁻⁷ rad at 48 kHz and 6.5 × 10⁻⁷ rad at 192 kHz for the parked station; limit
  0.01 rad).
- **Response** (derived from the detector's one-pole power average, τ = 1 s, section 6; design Part 1, plan D17).
  A ramp of ḟ Hz/s is followed ḟ·τ behind (exact for the power-weighted centroid of the averaged spectrum, A.6): 1.0 Hz
  at 1 Hz/s. The stage-1 simulation through the whole engine (A.6) found the track's own frequency, which the follower
  uses, 0.94 to 1.23 Hz behind, and the Matched path's reported frequency 1.49 to 1.63 Hz behind (measured). The
  design's earlier 1.1 to 1.6 Hz were Matched-path figures (its tracker's derived lag ḟ·τ_f/P₁ = 1.1 Hz, and its
  measured end-to-end lag up to 1.6 Hz), which the follower does not go through. An equally strong answering station,
  starting as the first stops, overtakes the
  first station's decaying average when 1 − e^(−t/τ) = e^(−t/τ), at t = τ·ln 2 = 0.69 s (derived), "about 0.7 s".
- **Cost.** One complex multiplication and one cos/sin pair per channel sample, 1 500 per channel-second, and a
  compare per block (derived); not measured.

**Limitations: following the detector.**

- The coarse rule has no hysteresis: a station parked on a bin boundary moves the channel on 1 380 of 2 812 blocks,
  49% (measured above; about half, as derived for f_det scattered symmetrically about the boundary). The moves are phase-continuous, and such a station lies 11.7 Hz from either bin, inside the channel
  filter's passband (flat to 0.015 dB relative to 0 Hz out to about 39 Hz), so the decoder sees nothing of them.
- On the detector path the station is off-center by the detector's error (section 6). Viterbi and segmental sum
  coherently only over their 25-tap front filter: a residual δ costs a 12 ms pulse about sinc²(δ × 12 ms) (A.7, "The
  cost of a residual offset"), 0.002 dB of signal power at the derived 1.0 Hz lag behind a 1 Hz/s drift and 0.30 dB at
  half a bin, 12 Hz (derived, the pulse taken as rectangular). The first prototype's 390 ms blocks, which felt 2.3 dB at
  that lag, are gone.
- `--record-channels` stops the run when a channel's center moves; a followed channel's recorded center is its f_det,
  so only a constant oracle frequency records.

**Provenance.**

- The Blackman-windowed sinc: standard choice. Its length: derived (A.3).
  channel_bins = 64: heuristic within the derived bounds above.
- **Cutoff ±150 Hz (heuristic).** Sized for fast code, about 60 WPM: 20 ms
  dits need several keying harmonics (tens of Hz each) to keep their
  edges. The principle that *should* set it is speed: for detecting an
  element of duration T_el, a filter matched to it has a noise bandwidth
  of 1/T_el (exact for a rectangular element; proakis-ook-notes.md section
  2.7); for timing its edges, somewhat wider. A fixed ±150 Hz (252 Hz
  noise bandwidth) is about 4× wider than the ±30 Hz that the backlog
  roughly estimates is enough at 15 WPM (an estimate, not measured), so
  slow stations get about 6 dB more noise than they need. This stage-1
  filter is meant to stay fixed and wide; narrowing per station is planned
  as a second stage at 1500 samples/s (backlog: "Channel filtering, two
  stages").
- The tracker's lag value, NCO range (±75 Hz, where the channel filter is
  0.34 dB down relative to the passband), τ_f = 0.5 s, the gates (0.6,
  0.3) and the fine-tuning range ±12 Hz (owner decisions 2026-09-29,
  option 1): heuristic.
- **Accuracy.** Initial error of the detector's residual: about 0.2 Hz
  measured for a clean station. Simulated (the whole chain: NCO, K = 58
  boxcar, posterior weights, 60 s of PARIS, 4 seeds; plan review,
  2026-09-27): 0.08, 0.27 and 0.55 Hz RMS at S₅₀₀ = +10, 0 and −5 dB; in
  simulation 50% of marks were keyed near S₅₀₀ = −1.8 dB at 25 WPM; a
  1 Hz/s drift lagged 1.49–1.63 Hz at the end of the last mark through the
  whole engine (simulated). On the benchmark (group F, oracle channels,
  stations 0 to 11.7 Hz from the bin center; A.11, "Frequency error") the
  median error was 0.11 Hz at S₅₀₀ = 5 dB and 0.13 Hz at 0 dB, within the
  ±2 Hz target at every signal, against the Envelope path's 5.9 Hz (bin
  rounding): the re-centering's accuracy is **measured**; its parameters
  stay heuristic. Results: 2a record, section 3.4, and section 8.1 (moved
  from this document).

## A.8 Classical decoder (Envelope and the shared back end)

### Exact form

The rules of this section as the code implements them (moved verbatim from the body on 2026-10-05, when the body was rewritten for reading).

#### Which decoder runs

There are five decoders. `ClassicalDecoderConfig::front_end` (a code
identifier) selects one, and `kz4ap-bench --decoder envelope|matched|bank|viterbi|segmental`
sets it from the bench (`--front-end` is the option's old name, kept as an
alias); `FrontEnd::Viterbi` is called the new decoder in older text. **Matched
is the bench's default** (owner, 2026-09-29; the app's is segmental, A.9b); Envelope
also switches the detector to the Envelope path's rules; the bank is
selectable, not the default (owner decision D1), and so are viterbi (the first
prototype's plan) and segmental (`docs/decision-log.md`, 2026-10-30). The bank, viterbi and segmental use the default
path's detector and channelizer unchanged. Envelope and Matched share the
classical decoder's back end (steps 6–10 below).

#### Envelope (not the default)

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

#### Shared back end (Envelope and Matched)

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


Input: the station's complex stream y[n] at r = 1500 samples/s. Output:
symbols (characters, `<XX>` prosign tokens, word spaces), each with a
probability and start/end times.

**What it is.** A hard-decision baseline: it decides key-down or key-up
sample by sample against thresholds, then classifies each timed element,
then looks the pattern up. Its design comes from general familiarity with
simple CW decoders; it was not derived from theory or compared with
alternatives. It does not do what the design spec asks of the classical
decoder, finding "the most probable character sequence given timing
statistics and a prior over likely text"; the bank decoder (section 8c),
developed as its probabilistic successor, is frozen at its current
defaults as the reference (design spec §5.2, amendment of 2026-10-07).

**Matched in the classical decoder.** `Matched` replaces steps 1–5 with
section 7's re-centering and A.8b's matched filter and likelihood:
the key goes down when the posterior log-odds g exceeds +1 nat and up when
it falls below −1 nat, and never goes down while a < a_min(K). Steps 6–10
are the same in both. In `Matched` mode the filter follows the speed
estimate once its window holds 8 marks, and every decode result reports
the tracker's frequency estimate.

**Derivations.**

- **Envelope detection (step 1).** Non-coherent AM detection: it needs no
  carrier recovery, and the carrier phase and, with the current wide
  channel filter, the residual frequency offset don't matter (A.7). There
  is no audio tone (BFO) anywhere in the decoding path.
- **Level followers (step 4).** M ← M + α_M·(e − M), α_M = α_fast if e > M,
  else α_slow; S ← S + α_S·(e − S), α_S = α_fast if e < S, else α_slow;
  α = 1 − exp(−1/(τ·r)): τ_fast = 4 ms gives α_fast ≈ 0.154 and
  τ_slow = 3 s gives α_slow ≈ 2.2×10⁻⁴ at r = 1500 Hz. The smoothing's τ_s
  is never less than one sample and adapts as the speed estimate changes.
  The warm-up is 72 samples.
- **The thresholds against theory (qualitative only).** For a hard on/off
  decision the optimum threshold is where the prior-weighted Rayleigh
  (key-up) and Rician (key-down) envelope densities cross. It is not at 50%
  of the way from the key-up level to the key-down level, and it moves
  with SNR (`docs/research/proakis-ook-notes.md`, section 2.3; derived
  there from Proakis & Salehi). The notes' figure of about 44–45% (equal
  priors, E/N₀ = 10–14 dB re 1, key-on energy per element over one-sided
  noise density) is for an envelope taken after a filter matched to the
  element, and it does not carry over to this decoder quantitatively: here
  the envelope is formed after the 252 Hz channel filter, where the SNR is
  far lower, and only then smoothed. The one-pole smoother with τ_s = ¼ dit
  has a noise bandwidth of 1/(4τ_s) = 1/dit on the (real, one-sided)
  envelope, but smoothing after detection does not give the envelope
  statistics that filtering before detection would. So the 40% and 60%
  thresholds are not shown to be near the optimum; that would need a
  measurement.
- **Squelch (step 5).** No keying (and any mark ends) unless M ≥ 3·S and
  M > S. The factor 3 is a 9.5 dB amplitude ratio (20·log₁₀3). After a
  station stops, the envelope drops to the noise level and M relaxes toward
  it with τ_slow = 3 s: its excess over the noise falls to 5% in
  3τ_slow ≈ 9 s. Until M falls below 3·S the squelch stays open, with the
  keying threshold sinking toward the noise.
- **Element classification (step 7).** The element's confidence is the
  probability of the chosen class. Gaps (step 8): nominal 1, 3 and 7 dits.
- **Symbols (step 9).** The 62-symbol table: 26 letters, 10 digits, 14
  punctuation marks, 12 prosigns; eight or more dits is the error prosign
  `<HH>`.
- **Speed (step 10).** If the dah/dit ratio is between 3.0 and 3.85,
  dit = (mean dah − mean dit)/2, which cancels the constant shortening
  every mark gets from the keying edges. Otherwise dit = the mean of the
  dits and one third of each dah; ratios above 3.85 are taken as the
  sender's weighting (hand-sent four-dit dahs measure 3.95–4.32). If there
  is no split (all marks alike), they are taken as all dits or, if longer
  than 2 current dits on average, all dahs. Marks longer than 0.96 s (a
  four-dit dah at 5 WPM) are excluded, as carriers rather than Morse
  elements. The result is clamped to 5–60 WPM.
- **Why 24 marks.** The window must contain both dits and dahs for the
  split to work, and more marks reduce the estimate's random error (as
  1/√(number of marks)). Plain text averages about 3 marks per character
  (PARIS: 14 marks in 5 characters), so 24 marks is about 8 characters,
  spanning about 4 s at 25 WPM. The cost is lag: the estimate reflects the
  window's middle, about 2 s back at 25 WPM, so a speed change takes a few
  seconds to follow.
- **The growth bound (Matched).** While the filter follows, each mark may
  raise the dit estimate by at most ×1.25: it stops a runaway after a
  sudden speed change, and a real slowdown takes ln(ratio)/ln 1.25 marks
  to follow (5 marks from 35 to 12 WPM). The same bound holds for the
  filter's own dit from its first follow step after an acquisition or
  re-acquisition: it grows from the 20 ms acquisition dit by at most ×1.25
  per mark, because the estimate at that step may rest on up to 7
  unbounded marks (in simulation a truncated first mark gave a 110 ms
  estimate, the filter jumped from K = 24 to 146 samples and ran away).
- **Once per physical mark.** The bound is applied in `update_speed()`, at
  every key-up counted for speed. Each key-up counted for speed saves the
  state its update starts from (the dit estimate, the filter's dit and
  length, the count of marks since the last re-acquisition, the speed
  window), and a dropout merge (step 6) that re-opens that element
  restores it, so the re-measured mark gets one update, bounded once. The
  widened filter can still re-open the mark it was widened at; each
  re-open is merged and undone, and it stops about (T_v,new − T_v,old)/2
  after the first key-up (T_v the filter's duration), at most
  0.125·T_v,old = 0.1 of the old dit, since T_v,new ≤ 1.25·T_v,old (for a
  boxcar and a strong signal: its falling edge passes half amplitude half
  the length change later; an upper bound, the ±1 nat hysteresis ends it
  slightly earlier), for example 4.8 ms at 25 WPM (T_v,old = 38.7 ms): the
  mark is timed through the new filter, up to that much longer, and grows
  the estimate at most ×1.25. The dit estimate needs ln 2/ln 1.25 = 3.1
  marks to double. The Envelope path's merge pops the mark from the window
  and keeps the updated estimate, as in milestone 1; it has no growth
  bound.
- **Marks whose start was not observed (Matched).** Keying is possible on a
  sample where the Matched decoder is ready (its 0.32 s warm-up is over)
  and the squelch is open (a ≥ a_min). A key-down counts as observed only
  if, since the last sample on which keying was impossible, the decoder saw
  a keyable sample with the key up and g < −1 nat (the key-up threshold):
  evidence that the carrier was off before the mark began. Otherwise the
  carrier may have been up before the key-down, while keying was
  impossible or while g sat between −1 and +1 nat, so the mark's duration
  may be a fragment's. Such a mark is decoded but not counted for speed: it
  does not enter the speed window or count toward the 8 marks before the
  filter follows. Every key-up by the log-odds sets the evidence, so in
  steady keying every mark counts; a dropout merge keeps the merged mark's
  flag. (Rule derived from what the decoder can observe; no new parameter:
  its threshold is the key-up hysteresis, −1 nat.) It applies after the
  warm-up (a channel opening mid-mark); to the first mark of a channel
  that opens in noise (after the warm-up, a ≈ 1.6 in noise alone, below
  a_min = 3: ŝ² = 2σ²·ln 10 − 2σ² from the warm-up's percentiles); after a
  re-acquisition (ŝ restarts at 0, so a = 0 < a_min); and wherever else the
  squelch closes and re-opens (the floor's stuck-low restart of ŝ, ŝ
  decaying or σ̂ rising at a weak station). In the last three the squelch
  re-opens on the sample after the next mark lifts ŝ past a_min (ŝ is
  updated after the squelch is decided), and that mark's keyed start is off
  in either direction: early at high S₅₀₀, where the squelch opens on the
  filter's rising ramp with â well below the true a, and late at low S₅₀₀.
  Each such channel or over loses one mark of speed evidence; the filter
  follows after 9 physical marks there.
- **Envelope's warm-up and a mark in progress.** The warm-up (one dit at
  25 WPM, 48 ms) sets both levels to the mean envelope, so a mark in
  progress when it ends keeps M < 3·S and is not keyed, unless it began
  late enough that the mean S = f·A (A the carrier's envelope, f the
  fraction of the warm-up the mark filled) is below A/3. Then the squelch
  opens when the smoothed envelope reaches 3fA, τ_s·ln((1 − f)/(1 − 3f))
  plus about 4 ms of attack after the warm-up ends (τ_s = 0.25 × 48 ms =
  12 ms): at f ≈ 0.28 about 22 ms, so a 48 ms dit is timed at about 19 ms,
  short enough to form a dit cluster of its own.
- **Re-acquisition (Matched).** Once the key has been up for
  max(0.5 s, 12 dits), the Matched decoder assumes the next station may be
  a different one (a QSO turnover): A.8b's filter returns to the
  60 WPM width and its amplitude estimate restarts, A.7's frequency
  average restarts from the last estimate, the speed window of step 10 is
  set aside and a new one starts (so the next station's marks are not
  mixed with this one's), and the filter follows the speed again after 8
  new marks; if nothing is keyed within 2 s, the set-aside speed window
  comes back and the filter returns to the width it had (a weak station
  that pauses is then not held at the acquisition floor). The decoder does
  not decide which station it follows: its frequency tracker fine-tunes
  within ±12 Hz of the anchor its caller gives it
  (`Decoder::set_frequency_anchor_hz`), so on its own it follows only a
  station within ±12 Hz of that anchor; a station farther away is followed
  only when the caller moves the anchor to it: the engine sets the anchor
  to the detector's frequency for the track, so a station answering within
  47 Hz is followed once the detector's track moves to it (with the retune
  delay of A.6), and one farther away gets its own track.

**Provenance.**

- Envelope: steps 2–5 heuristic. The warm-up: measured, without it noise at
  the start keyed as one 1.4 s mark. τ_slow = 3 s and the squelch factor 3:
  estimated, then confirmed by tests; in noise alone M/S measures about
  1.45; the squelch's ~6 dB SNR (500 Hz) sensitivity limit is an estimate,
  not measured. The 3.0–3.85 ratio band: measured (it fixed 45 WPM reading
  as 51). 24 marks, the glitch limit, the dit/dah boundary and its width:
  heuristic; the 2- and 5-dit gap thresholds: heuristic (the conventional
  midpoints between the nominal 1, 3 and 7 dits).
- Matched in the classical decoder: the ±1 nat hysteresis, the 8 marks
  before following and the re-acquisition times heuristic; the ×1.25
  growth bound heuristic (owner decisions 2026-09-29, option 1), applied
  per physical mark since milestone 2a's Task 15; the rule for marks whose
  start was not observed derived (Task 16). The history and measurements
  of both fixes: 2a record, sections 3.7–3.8 and 8.2.
- Re-acquisition: simulated at decoder level (an earlier tracker design
  whose estimate never left 0.2 Hz of the first station in these runs;
  levels in dB re the first station's key-down power; 100 seeds each): a
  station answering 50 Hz away at −10 dB (not keyed), 70 Hz away at −6 dB,
  or 100 Hz away at −6 or +10 dB left the first station's next over intact
  in 99–100 of 100.

**Limitations.**

- The Envelope squelch means weak signals are not decoded at all, roughly
  below 6 dB SNR (500 Hz) (an estimate). After a station stops, the window
  in which M has not yet fallen below 3·S is when stray `E`s are decoded
  (backlog).
- Envelope's first timed mark after a channel opens can be short by up to
  most of a dit, and a fragment can happen, rarely, in a window of opening
  phase a few ms wide (derived above, not measured; a stated limit, not
  fixed).
- **A mark whose key-up the squelch forces.** The end of a mark has no
  observed-start rule: a key-up forced by the squelch closing (for example
  the floor's stuck-low restart of ŝ in the middle of a mark) still counts
  the mark for speed, with a truncated duration (derived from the code, not
  observed; backlog, "A mark whose key-up the squelch forces").
- **Slow stations at start-up (12 WPM).** The first characters of a slow
  station often read as a string of T's (inferred from step 10, not
  instrumented): a window of whole dits alone is longer on average than
  twice the initial 48 ms dit whenever the station is slower than 12.5 WPM
  (dit > 96 ms, derived), so it is taken as all dahs and the estimate falls
  to a third of a dit until the first dah enters the window. Measured in
  the 2a record, section 3.8 (regression R2).
- **The "all alike" branch** of step 10 averages a two-cluster window
  whenever one mark sits between the clusters (2a record, section 3.8,
  regression R1; the start-up runaway chain, 2a record section 8.3,
  "Limits measured"). Merged elements at low S₅₀₀ through the 16 ms
  acquisition filter are not addressed.
- Re-acquisition: a neighbor 60–70 Hz away at the first station's level or
  stronger leaks through the filter's first sidelobe (−18.7 dB relative to
  a centered station at 60 Hz and K = 58, derived) and can be keyed in
  fragments that corrupt the speed estimate (simulated). After a silence
  in noise alone, noise was keyed as a stray character in about 1% of
  cases (4 of 400; simulated), because ŝ restarts from its first few noise
  samples.

## A.8b Matched decoder

### Exact form

The rules of this section as the code implements them (moved verbatim from the body on 2026-10-05, when the body was rewritten for reading).

Per channel at r, in order: the tracker's NCO (section 7), a dit-matched
boxcar, the envelope likelihood, keying on the posterior log-odds, then
steps 6–10.

- **Filter.** Unity-gain boxcar of K = round(β·dit·r) samples, β = 0.8
  (the boxcar shape is the matched filter of a rectangular element,
  derived; β heuristic, 0.97 dB of output SNR below the full-length matched
  filter, derived). Noise bandwidth r/K: 25.9 Hz at 25 WPM (K = 58),
  9.9 dB below the noise power the 252 Hz channel filter passes (derived).
  K starts at the 60 WPM width (16 ms, K = 24) and is clamped to 24–288
  samples (16–192 ms, 60–5 WPM). When K changes, σ̂_v² is scaled by
  K_old/K_new (derived for white noise).
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
  S₅₀₀ = −2.5 dB at every speed (derived): a lower bound on sensitivity,
  not the decoder's working sensitivity.
- **Keying.** Down when g > +1 nat, up when g < −1 nat (heuristic
  hysteresis); never down while squelched or warming up.
- **Re-acquisition** (heuristic): after the key has been up for
  max(0.5 s, 12 dits), K returns to 24, ŝ² restarts, the frequency average
  restarts, and the speed window is set aside; if nothing is keyed within
  2 s, the old window and width come back (the amplitude and frequency
  estimates stay restarted).


`MatchedFrontEnd` (matched_front_end.cpp; the class name is a code
identifier) runs per station at r = 1500 samples/s inside the decoder, on
the re-centered stream u[n], before any envelope is taken.

**Derivations.**

- **Filter.** A boxcar of duration T is the matched filter of a
  rectangular element of duration T, with noise bandwidth exactly 1/T
  (Proakis §4.2–2; proakis-ook-notes.md §2.7). β = 0.8 costs 0.97 dB of
  output SNR below the matched filter, in exchange for staying shorter
  than an element space when the speed estimate is up to 25% slow. The
  noise bandwidth is r/K: 25.9 Hz at 25 WPM (K = 58),
  10·log₁₀(252/25.9) = 9.9 dB less noise than the channel filter passes.
  K at 60 WPM is 24 (62.5 Hz). The clamp is computed from durations:
  β·1.2 s/60 = 16 ms (K = 24) and β·1.2 s/5 = 192 ms (K = 288); a dit that
  is not finite or not positive is ignored. A `set_dit` during the warm-up
  takes effect when the warm-up ends, and restarts ŝ.
- **The rescale at a change of K.** σ̂_v² ← σ̂_v²·K_old/K_new (white noise at
  r). The channel filter removes the boxcar's sidelobes beyond ±150 Hz,
  keeping about 0.92 of the boxcar's noise power at K = 24 and 0.965 at
  K = 58, so the rescale and the a² formula are off by 0.36 dB at K = 24
  and 0.15 dB at K = 58, dB relative to the true noise power. The last
  2K + 1 values of |v|² that the noise guard compares and the floor's
  samples are rescaled by the same factor.
- **Re-acquisition.** `reacquire()`: K returns to 24 (60 WPM, main lobe
  ±62.5 Hz), ŝ² and its weight return to 0, and σ̂_v² is kept (rescaled);
  if nothing is keyed within 2 s the width returns to what it was. Without
  it the filter stays at the last station's width (±21–26 Hz main lobe,
  nulls near 25 and 50 Hz) and ŝ at its level, and a station answering
  there, or 6 dB weaker (re the first station's key-down power), is never
  keyed (simulated). After it, a station f_sep away loses
  |sinc(f_sep·24/1500 s)|² at K = 24 (−2.4 dB relative to a centered
  station at 25 Hz) and must pass the acquisition squelch, so it needs
  about S₅₀₀ ≥ 0 dB at 25 Hz. The first mark after it, keyed when it lifts
  ŝ past the squelch (early on its ramp at high S₅₀₀, late at low S₅₀₀),
  is not counted for speed (A.8).
- **Likelihood** (Proakis eq. 4.5–21). The Bessel function I₀ is what
  remains of the Rician density once the carrier's unknown phase is
  averaged out. ln I₀ from Abramowitz & Stegun 9.8.1–9.8.2 without
  overflow. P₁ = 0.44: PARIS keys down 22 of 50 dit units. For noise flat
  across the filter, a² = 2·S₅₀₀·(500 Hz)·K/r, S₅₀₀ as a linear ratio:
  a = 6.2 at S₅₀₀ = 0 dB and 25 WPM.
- **Amplitude's ramp bias.** Samples on the filter's ramps bias ŝ low: 0.79
  of s for dits alone (noise-free trapezoid at K = 58). The decision point
  near ŝ/2 then lengthens each mark (about 7 ms at 25 WPM, simulated).
- **The noise guard.** Three taps K apart, v[n], v[n−K] and v[n−2K],
  share no inputs, so in white noise they are independent. A mark or a
  filter ramp within K of the middle tap lifts some tap above the guards
  (at S₅₀₀ = 60 dB by orders of magnitude). In noise, η = |v|²/(2σ_v²) is
  exponential with mean 1 (Proakis eq. 2.3–43), and the accepted middle
  tap is η truncated at κ, with mean m(κ) = 1 − κ·e^(−κ)/(1 − e^(−κ)) =
  0.632 at κ = 1.75, which the update divides out. The estimate never
  reads p, g or ŝ. With ρ = σ̂_v²/σ_v², in noise alone its only stable
  fixed point is ρ = 1: the map ρ ↦ m(κρ)/m(κ) has slope
  κ·m′(κ)/m(κ) = 0.651 < 1 at ρ = 1 and slope κ/(2m(κ)) > 1 near 0, so
  ρ = 0 is an unstable fixed point and it cannot settle low. Its climb
  back from a low ρ is slow: at ρ it accepts a fraction
  (1 − e^(−κρ))(1 − e^(−κ_n ρ))² of the samples, so from ρ = 0.25 (a rise
  in the noise of 6 dB relative to the previous noise power) the expected
  return to ρ = 0.9 takes about 43 s (by integrating
  dρ/dt = (m(κρ)/m(κ) − ρ)·acceptance(ρ)/τ_n); the relaxed neighbor guard
  κ_n = 4 cuts it from 98 s (κ_n = κ) to 43 s.
- **The floor's bound.** In noise Q is 2σ_v²·(−ln 0.9); with a station
  leaving a clean fraction of at least c of those samples, Q is at most
  noise's (0.1/c)-quantile, so F ≤ σ_v²/2.5, and the factor 2.5 covers the
  sampling spread of a 64-sample quantile (heuristic). In noise
  F = 0.0825·σ_v², so the floor acts when σ̂_v < 0.29·σ_v and restarts ŝ
  when σ̂_v < 0.14·σ_v. This removes the stuck-low state (a start or a
  noise rise that leaves σ̂_v that low) and bounds large rises.
- **Why c = 0.25 (inputs derived, rounding heuristic).** A sample is clean
  when its K-sample window lies inside a space, so a gap of g dits leaves
  g − β dits clean; continuous text at K = 0.8·dit leaves 0.29–0.33 of the
  samples clean (PARIS 0.33, a CQ call 0.29, a contest exchange and a
  pangram 0.31; computed from the keyed envelopes), and 0.24–0.28 with the
  speed estimate 25% slow (K = one dit); c = 0.25 rounds the lower end
  down.
- **The floor's lift.** If σ̂_v² falls below F, σ̂_v² ← F and the noise
  weight W_n drops to at most the number of samples in 10.67 ms (16 at
  r = 1500 samples/s), so the next updates count for more; ŝ restarts only
  if F > 4·σ̂_v², the stuck-low case in which the low σ̂_v has let ŝ grow on
  noise. A smaller lift is the quantile's spread or a second station, and
  restarting ŝ there refit it from a few samples on a filter ramp and
  merged dits.
- **Warm-up.** 0.32 s is 20 filter lengths at K = 24, about 20 independent
  samples. σ̂_v² = (20th percentile)/(2·(−ln 0.8)) is derived for noise
  alone. A mark in progress when the warm-up ends is decoded but not
  counted for speed (A.8).
- **Squelch.** T_v = K/r and 16 ms the acquisition filter's (K = 24 at
  r = 1500 samples/s, so a_min = 3·(K/24)^(1/4) there). In noise alone â²
  is a p-weighted mean over about τ_a·r/K independent samples, so its
  spread grows as √K, and a_min ∝ T_v^(1/4) keeps the chance that noise
  alone passes the squelch the same at every K (Gaussian approximation); a
  flat a_min = 3 would pass noise more often at long K. With
  a² = 2·S₅₀₀·(500 Hz)·K/r the squelch is S₅₀₀ = −2.5 dB at K = 24 (any
  speed), −4.4 dB at K = 58 (25 WPM), −6.0 dB at K = 120 (12 WPM), −7.9 dB
  at K = 288 (5 WPM), and up to 1.4 dB higher with ŝ's ramp bias
  (ŝ = 0.85·s: 20·log₁₀(1/0.85) = 1.4 dB). A station is first keyed at the
  acquisition width, so the sensitivity floor for acquiring one is
  S₅₀₀ = −2.5 dB at every speed; the lower figures hold only for a station
  already acquired and narrowed to. In noise alone, with σ̂_v correct, the
  amplitude update multiplies ŝ² by about e^(−P₁) = 0.64 per τ_a of
  key-down weight W_a (to first order in a²:
  E[p·(|v|² − 2σ_v² − ŝ²)] ≈ −P₁²·ŝ², and W_a grows by P₁ per sample); in
  wall-clock time, with p ≈ P₁, that is e^(−P₁²) = 0.82 per 0.5 s. So a
  decays toward 0; this holds only because σ̂_v does not depend on a.
- **Correlated samples.** Successive outputs of a K-sample boxcar share
  inputs; their noise autocorrelation is triangular and sums to K. Each
  `FrontEndSample` carries weight 1/K, the factor a sequence decoder may
  apply to Λ before summing over samples, keeping the 0.67 ms timing
  resolution. Scaling a nonlinear per-sample LLR by the correlation length
  is an approximation (the sufficient statistic for one element is one
  matched-filter sample) (heuristic). The classical decoder keys from g
  sample by sample and does not sum, so it ignores the weight.

**Provenance** (the design simulations are in the milestone-2a plan,
`docs/plans/2026-09-27-milestone-2a-benchmark-and-front-end.md`, and its
reviews; benchmark results in the 2a record, section 3, and section 8.3).

- β = 0.8, the 60 WPM start, the follow rule, τ_a, τ_n, κ, κ_n, the
  floor's 10th percentile, 64 samples, 2.5, the lift's 10.67 ms and 4×,
  the warm-up's 0.32 s and weight 0.1, the squelch's 3 and the ±1 nat
  hysteresis: heuristic. The shapes (boxcar, Rician/Rayleigh LLR), P₁,
  m(κ), the warm-up scale, the floor's bound and the squelch's scaling:
  derived.
- **The amplitude's ramp bias:** 0.85–0.88 of s for PARIS at 25 WPM, S₅₀₀
  0–60 dB (simulated); marks lengthened about 7 ms at 25 WPM (3.5 ms per
  edge, simulated).
- **The noise guard** replaces a guard on the posterior, which selected
  quiet stretches, biased σ̂_v low and in noise alone settled at
  σ̂_v = 0.53–0.57·σ_v in 7 of 10 seeds, keying noise (plan review
  2026-09-27; simulated). With the floor and warm-up (simulated, numpy
  seeds, final check 2026-09-28): noise alone (40 seeds × 120 s) σ̂_v/σ_v
  has mean 1.00 and standard deviation 0.020 at K = 24 and 0.032 at
  K = 58 (0.080 at K = 288, 20 seeds, re-review), with no signal flag;
  with continuous PARIS σ̂_v is 0.98–1.06·σ_v at S₅₀₀ 0–60 dB and 25 WPM
  (0.90–1.11·σ_v at 12 WPM), and the leak of weak marks lifts it to
  1.16–1.23·σ_v at S₅₀₀ = −5 dB and 1.22–1.30·σ_v at S₅₀₀ = −8 dB
  (25 WPM); after a rise in the noise of 6 dB relative to the previous
  noise power the last signal flag came at most 16 s later and σ̂_v
  returned to 0.9·σ_v within 24–41 s at K = 24 (40 seeds), 25–51 s at
  K = 58 and 24–56 s at K = 288 (20 seeds each), in line with the derived
  43 s.
- **The floor:** an earlier c = 0.4 was above continuous PARIS's 0.34 and
  made the floor lift routinely under strong text (final check
  2026-09-28); simulated: 38 edits in 7140 characters at S₅₀₀ = 60 dB
  with c = 0.4 and every lift restarting ŝ, 0 now; a neighbor 100 Hz away
  keying at the same time, +10 dB re the wanted station's key-down power,
  garbled the wanted station in 12 of 100 seeds, 2 now. After a rise of
  10 or 20 dB relative to the previous noise power it lifted in every seed
  and σ̂_v was back within 0.9·σ_v in 11–38 s (20 seeds, K = 24).
- **The warm-up:** the earlier 5th percentile of 0.2 s, about 12
  independent samples at K = 24 and 1 at K = 288, often started σ̂_v at
  0.2–0.5·σ_v.
- **The squelch:** in simulation (continuous PARIS, 10 seeds) 0%, 3%, 45%
  and 80% of marks were keyed at S₅₀₀ = −4, −3, −2 and −1 dB at 25 WPM,
  and 19%, 33% and 95% at S₅₀₀ = −4, −3 and −2 dB at 12 WPM, so 50% is
  reached near S₅₀₀ = −1.8 dB and −2.6 dB. On the benchmark's group A
  (filler text) CER 0.10 is reached at S₅₀₀ = −0.2, 1.1 and 2.9 dB at 12,
  25 and 40 WPM, above this expectation (2a record, section 3.2). The
  squelch value stays heuristic.

**Limitations.**

- **Floor coverage.** Solid digits ("0000 9999", clean fraction 0.18) and
  two stations keying at once are not covered by c = 0.25. The floor does
  not act after a rise of 6 dB relative to the previous noise power, whose
  recovery is the guard's climb (about 43 s, derived); during the climb
  noise can be flagged as signal for up to about 16 s (simulated).
- **Tune-up carriers of 1 s or more** break Matched when the channel opens
  during the carrier (likely mechanism, from the code, not instrumented: a
  warm-up that sees only carrier sets σ̂_v² from the carrier's own power,
  and a carrier longer than the floor's window, 64 samples K apart, 1.02 s
  at K = 24, lifts the floor to the carrier's level; with σ̂_v² near the
  station's power, a = ŝ/σ̂_v stays below the squelch, and the station's
  own marks pass the noise guard and hold σ̂_v up). Measurements: 2a
  record, section 8.3, "Limits measured".
- **High S₅₀₀ with random keying timing.** Matched's marks are about 7 ms
  longer at 25 WPM (the ramp bias) and a positive imbalance lengthens them
  further, shortening the element spaces the character decisions rest on;
  that this causes Matched's loss there is a hypothesis, not measured (2a
  record, section 3.7).
- **A channel that opens mid-transmission:** the first word, sent partly
  before the opening, is lost or shortened (replay, backlog "Wrong or
  missing first characters"). Measurements: 2a record, section 8.3.
- **The fresh-average bias** after a jump of the tracker (A.7) is bounded,
  not measured.

## A.8c Bank decoder

### Exact form

The rules of this section as the code implements them (moved verbatim from the body on 2026-10-05, when the body was rewritten for reading).

Instead of one boxcar following an estimated speed, 32 boxcars run at
once, one per speed; each branch keys, times, fits and spells its own
text, and a selector publishes one branch's text, with corrections. It is
the C++ port of the stage-1 Python prototype (`training/kz4ap_proto`,
removed on 2026-10-07; `engine/tests/data/bank/README.md` says where its
last version is), in `engine/src/bank/` and `engine/src/bank_decoder.cpp`.
"prototype" in this section means that removed code.

**Input: anchor mixing (no tracker).** Each channel block y (FS) is mixed
by the anchor Δ = f_det − f_c (Hz; the detector's current frequency for
the track minus the channel center; in oracle mode the label's frequency,
without its drift): u[n] = y[n]·e^(−jφ[n]), φ[n] = 2π·(Σ_{m≤n}Δ[m] − Δ[n])/r,
phase-continuous across anchor changes (derived: the prototype's
`anchored_baseband`). The residual offset stays in u; branch k attenuates
it by |H_k(f)|² below.

**The bank: ladder and filters.**

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

#### Noise (bank decoder)

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
  masked spectrum reads each branch low by a factor b_k = 0.8381 (k = 1)
  … 0.7812 (k = 32), dimensionless; **measured** in white noise (200
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
- **Locked-level recovery** (heuristic; never fired on 120 s of noise): a
  channel opened during a long mark takes the station for noise, and every
  mark then passes the guard, so the stuck-level rule never fires. When the
  last 0.32 s's 20% quantile reads over 10 dB under the level, the level is
  set again the same way.
- Variants, not the default: "spectrum-level" takes the level from the
  spectrum as well, σ²_v,k = 0.5·(W_k·S̃)/(M·b_k); "branch" runs the
  three-tap estimate on every branch.

#### Keying time constants (bank decoder)

The re-key wait is **0.8 s of keyed time** and the time-out **2 s** of
channel time from the amplitude becoming unknown, on every branch (stage
1's values, the default again since Plan B's B4g; class: seconds, stage
1's measured 0.8 s (E9b); owner, 2026-10-06; `rekey_after_s`,
`rekey_timeout_s`, both required positive). The periodicity windows are
in seconds, **5 and 10 s** for every candidate (class 2: latency after a
change; owner, 2026-10-06; values placeholder; stage 1's 2 s window
dropped by the owner, 2026-10-07, see "Periodicity" below). Plan B's
variants were removed on 2026-10-07 ("Keying time constants" below).

#### Keying (bank decoder)

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
  4·W_min of keyed time; heuristic); ŝ² = max(0, Q₀.₉ − 2σ²_v,k), the
  90% quantile because the ramps pull the mean down (heuristic). Ready to
  re-key once W ≥ W_min = 0.8 s of keyed time.

#### Duration fit (bank decoder)

Each branch fits its marks and spaces (s; densities in ln d).

- **Parameters** θ = (T, w, qT, T_g): dit, weighting (how much every mark
  is lengthened and every space shortened, e.g. by a transmitter's keying
  or by the filter), dah, gap timebase (equal to T in standard spacing;
  T_g > T for Farnsworth spacing).
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
- **Fast evaluation:** the likelihood over the grid is the decoder's main
  cost. ℓ_total is computed in one pass as
  m + ln(1 + Σe^(x−m)) (terms more than 40 nats below the largest m left
  out), with SLEEF's vectorized exp and ln (stated bound 1 ulp); this
  changes ℓ_total only in its last bits (bounds derived in A.8c).

#### Periodicity (bank decoder)

A coarse speed T_P from branch 1's keying, independent of the fits; it
feeds the fits' prior (so that they find the right speed quickly) and
selection's fallback (when no fit is yet trustworthy), and never itself.

- **Input:** branch 1's squelched posterior p, averaged in pairs to
  r_P = 750 samples/s; recomputed every 0.25 s (heuristic).
- **Candidates:** the fit's T grid (303 points τ_c, 12–242.2 ms), all
  judged over the same two windows of W = 5 and 10 s
  (`periodicity_windows_s`; 5 and 10 s since 2026-10-07, stage 1's 2, 5
  and 10 s by `--set`),
  N = max(16, round(W r_P)) = 3750 and 7500 samples of the most
  recent p; a window that is not yet full gives no estimate.
- **Autocorrelation:** x = p − mean(p), biased normalized
  ρ[τ] = Σx[m]x[m+τ]/Σx² (by FFT, as the prototype).
- **Comb on Π = 2T** (owner, E1; 2T because consecutive edges T apart have
  opposite signs, derived): tooth(c) = mean ρ over c ± 0.075Π; score =
  mean over k = 1…4 of tooth(kΠ) − ½(tooth((k−½)Π) + tooth((k+½)Π)),
  dimensionless (teeth and width placeholders, E3), with T = τ_c. A
  candidate counts only if 9.15τ_c ≤ (N − 1)/2, i.e. τ_c ≤ W/18.3 (derived):
  every candidate in the 5 and 10 s windows (273 and 546 ms; a 2 s window,
  stage 1's, reaches 109 ms, 11 WPM).
- **T_P:** per window the best candidate; T_P is the shortest window's
  estimate whose score is ≥ **0.03** (placeholder); else none.
- Removed (2026-10-07): windows in dits per candidate, N_w·τ_c (Plan B,
  B4a; Plan B record, section 6).
- Removed (2026-10-07): one window per row following the selected
  branch's fitted dit, N_w·T̂ (Plan B, B4a-C; Plan B record, section 6.6).

#### Text model and branch selection (bank decoder)

- **Text model:** unigram ln(w/2688) nats per character over VE3NEA's
  41-symbol table (derived); valid codes missing from it −5.817 nats, `*`
  −13.816 nats (heuristic). A branch's text score is the mean over its
  last **10** characters (placeholder), word spaces not counted.
- **Eligibility:** |ln(L_k/(0.8T_k))| ≤ ln 1.1 (one ladder step), with fit
  memory ≥ 8 elements (heuristic): the branch's fitted dit matches its own
  filter, so it is decoding at its own speed.
- **Best branch:** among eligible branches, those within ε_Q = 0.05 nats
  per element of the best quality Q tie (placeholder); if all tied
  branches have text, only those within 0.1 nats per character of the
  likeliest text stay tied (heuristic); the longest tied branch wins
  (better SNR). With none eligible: the likeliest text if it leads by
  ≥ 1.0 nats per character, else the branch with L_k nearest 0.8T_P, else
  branch 1.
- **Switching:** to avoid flicker, the published branch changes only after
  the same other branch has been best for **M = 4** consecutive instants,
  all eligible or all fallback picks (placeholder).

#### Channel decoder (bank decoder)

- **Marks and spaces:** a key change at sample n is at n/r − (N_k − 1)/(2r)
  s (group delay removed), with variance σ_t². Durations keyed by the
  known-amplitude test enter the branch's fit (re-maximized with the T_P
  prior); provisional ones enter none. Classification by the current fit;
  a character gap ends a character, a word gap adds a word space.
- **New over:** an *over* is one station's turn to transmit (from radio
  practice); a long enough silence ends it, because the next transmission
  may come from another station, at another speed and level. When the key
  has been up longer than T_new = max(0.5 s, 12·T_g) since the branch's
  last key-up (2.88 s without a fit; 0.576 s at 25 WPM; placeholders), the
  branch ends its character, keeps its fit as the previous over's, starts
  a fresh fit, decodes with the previous fit until the re-key, and its
  amplitude becomes unknown.
- **Clocks** (stage 1's rule): the time-out's count and the stretch start
  where the amplitude became unknown; a cleared time-out restarts only the
  count.
- Removed (2026-10-07): the clocks started at the first provisional mark,
  with the stretch's lead 7·d_k, and a cleared time-out moving the stretch
  (Plan B, B4d; Plan B record, section 10).
- **Re-key:** once the over has a mark and **0.8 s of keyed time**
  (`rekey_after_s`), the stretch (from its start, ≤ 20 s back) is keyed again
  with the full LLR at each candidate amplitude (the seed, and the previous
  over's if any); each candidate's marks and spaces go into a fresh fit and
  into the previous fit continued; the candidate whose fit explains the
  stretch best (mean log-likelihood per element) wins, its characters
  replace the stretch's (reason "rekey") and the keyer switches to the full
  LLR (candidate choice heuristic).
- **Time-out:** if the wait is not reached within **2 s** of channel time
  from the count's start (`rekey_timeout_s`), the stretch is re-keyed at
  the previous over's amplitude if that keys anything (reason "timeout");
  otherwise its provisional characters are deleted and the count
  restarts. This removes noise keyed after the last over.
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

#### The bank decoder behind the engine

Each call returns the characters appended since the last call, then the
corrections in order: `from_index`, the bank's characters from it on, the
time, the reason ("switch", "rekey", "timeout" or "resync") and the reach
(≤ 20 s, except a resync). A consumer appends, then applies each
correction by keeping the first `from_index` characters and appending its
characters; the result equals the bank's text after every call (derived;
a "resync" correction repairs any difference). The *final text* has every
correction applied, the *immediate text* none. Speed and confidence stay 0
(`DecodeUpdate::wpm` and `confidence`: `BankDecoder` never sets them, so the events carry 0 WPM, though each
branch fits a dit) and per-character probability is 1: placeholders.

**Cost.** Memory is about 8.3–13.6 MB per channel (derived from the
allocated arrays; chiefly the 21.1 s window of |v_k|² and the fits). CPU
is 28.85 ms per channel-second at the defaults (measured: run `d5-bank` on
the development set, the replay tool with 10 threads on the Linux machine;
development-set results record, section 6), against about 0.6 ms for
Matched (0.61 ms of process CPU through the engine on 26 check runs;
milestone 2c bank results record, section 4.6). The figure of about 42 ms
cited before (41.98 ms, run `bank-b4g`, Plan B results record, section 13)
was measured on the old development set, with the 2 s periodicity window
that task D5 later dropped; on the current set that configuration takes
30.35 ms (development-set results record, section 6), so the material
accounts for most of the difference.


Code: the branch filters and the envelope likelihood
(`engine/src/bank/filters.cpp`), the noise estimates (`noise.cpp`), the
keying (`keying.cpp`), the duration fit
(`fit.cpp`, `vecmath*.cpp`), the periodicity estimate (`periodicity.cpp`),
the text model and branch selection (`selection.cpp`), the channel decoder
(`channel.cpp`), and the engine's `BankDecoder` (`engine/src/bank_decoder.cpp`;
`kz4ap-bench --decoder bank`). The replay tool `kz4ap-bank-replay` (bench)
runs it on recorded channel streams. Each module is a port of the stage-1
prototype's module of the same name (`training/kz4ap_proto`, removed),
checked against the prototype's golden values (relative 10⁻⁹; discrete
outputs exactly), which stay in `engine/tests/data/bank/`; the port checks
are in the Plan A record, section 6.1. Status labels below are as the
prototype's `params.py` marked them, updated by Plan B.

### The bank in outline (moved from the body, 2026-10-31)

The reading account the body held while the bank was the chain under development. Its section 8c is now a summary.

**The idea.** Matched's filter has to know the speed to be the right
length, and the speed comes from what the filter decodes: a feedback loop
that can lock onto a wrong speed. The bank removes the loop. It runs
**32 filters at once**, one per speed from 100 down to 5 WPM; each
*branch* decodes its own text with no knowledge of the others' text, and
a selector publishes one branch's text. It is frozen at its current
defaults as the reference for new decoders (design spec §5.2).

**Input.** The channel stream is shifted by the detector's current
frequency (the anchor) so the station sits near 0 Hz; there is no separate
tracker. Whatever offset remains, each branch's filter attenuates it.

**The branches.** Branch k is a moving average of length

  L_k = 9.6 ms × 1.1^(k−1),  k = 1 … 32  (9.6 ms to 184 ms),

each 10% longer than the last (the ratio and speed range owner; the
filter as 0.8 of a dit heuristic, as in Matched). Each is realized as
N_k = round(L_k·r) samples at the channel rate r, 14 to 276 (derived), so
branch 1 is 9.33 ms long. Each branch's *nominal dit* is d_k = L_k/0.8: 12 ms
(100 WPM) on branch 1, 50 ms on branch 16 (24 WPM), 230 ms (5.2 WPM) on
branch 32.

**The likelihood.** Each branch gives every output sample v the
log-likelihood ratio of key-down against key-up: how much more likely the
observed |v| is with a carrier plus noise (Rician) than with noise alone
(Rayleigh),

  Λ = −a²/2 + ln I₀(a·x)  (nats),  x = |v|/σ_k,  a = ŝ/σ_k,

with σ_k the branch's noise per real component, ŝ its *amplitude* (its
estimate of the station's key-down carrier amplitude) and I₀ the modified
Bessel function of order 0 (the form derived). In white noise
a² = 2·S₅₀₀·(500 Hz)·L_k, S₅₀₀ as a linear ratio (derived). A longer branch
passes less noise, so a slow station gets a cleaner signal on its own
branch: the branch's own SNR, a²/2, is S₅₀₀ + 6.8 dB on branch 1, + 12.8 dB
for a 25 WPM filter (38.4 ms, between branches 15 and 16) and + 19.6 dB on
branch 32, each in dB SNR in that branch's own noise bandwidth, with the
nominal L_k. Adding the prior log-odds of key-down gives the log-odds
g = Λ + ln(0.44/0.56), in nats (PARIS keys down 22 of every 50 dit units; derived),
and the probability of key-down is p = 1/(1 + e^(−g)).

#### Noise, in outline

Every branch needs its own noise level σ²_k. The noise in the channel is
not white: the 252 Hz channel filter shapes it, and interference adds
color. So σ²_k does not simply scale as 1/L_k; each branch's noise is the
noise spectrum S(f) weighted by that branch's filter, ∫ S(f)·|H_k(f)|² df.
The bank therefore takes the **level** from branch 1 and the **ratios**
between branches from a measured noise spectrum (the method chosen by
measurement in the stage-1 prototype).

- **Level.** Branch 1 runs Matched's *three-tap guard* (without Matched's
  floor lift): of three outputs N_1 = 14 samples (9.33 ms) apart, which
  share no input samples, the middle one is taken as noise if it is small
  (|v_1|²/2σ²_1 below κ = 1.75) and both neighbors are below 4 (heuristic
  values). Accepting only small samples biases the average low by a known
  factor, m(κ) = 0.632, the mean of an exponential truncated at κ, which
  the update divides out (derived). Memory 2 s (heuristic). For the first
  0.32 s of non-zero input, a warm-up sets the level from the 20% quantile
  of the output power (the duration heuristic, the scaling derived for
  noise).
- **Spectrum.** The channel stream is cut into segments of 171 ms
  (5.86 Hz bins). Samples near any output of branch 1 stronger than
  4 × its noise power (|v_1|²/2σ²_1 ≥ 4, the guard's neighbor threshold)
  are left out, with a margin of **4.8 ms** on each side (half of branch
  1's nominal length, 9.6 ms; heuristic, owner); a segment counts only if
  at least half of it is left (heuristic). This is a level test on branch
  1's output, not its keying: no keying decision feeds the noise estimate.
  The kept segments build an averaged spectrum (2 s memory), smoothed over
  ±25 Hz (heuristic).
- **Ratios.** Branch k's noise is branch 1's level times
  ∫ S·|H_k|² df / ∫ S·|H_1|² df, S that spectrum. Leaving out samples near
  signals removes mostly low-frequency power, which the long branches
  weight most, so the masked spectrum reads each branch's noise slightly
  low; a factor per branch, 0.84 (branch 1) to 0.78 (branch 32), corrects
  it (measured in white noise, 200 seeds; valid only for these defaults).
- **Exact zeros** are missing data: a receiver's output always carries
  noise, so a run of exact zeros is padding or a dead channel, not a
  measurement (derived). Until the first non-zero sample the channel
  decodes nothing.
- **Recovery.** If branch 1 accepts nothing for **8 s** (4 × the 2 s
  memory; heuristic), as after a sudden large rise of the noise, which
  would leave the estimate stuck low, every branch's noise is
  re-initialized from its recent input; in seconds, not dits, because
  noise has no keying speed.

Two other noise methods remain selectable, not the default:
"spectrum-level" takes the level from the spectrum as well, and "branch"
runs the three-tap guard on every branch.

#### The back end, in outline

Everything advances in blocks of 32 samples (21.33 ms; heuristic). While a
branch knows the station's amplitude, it keys every sample with the
log-odds g, ±1 nat of hysteresis (heuristic) and a squelch. At the
stream's start and at each new *over* (one station's turn to transmit,
which a long enough silence ends) the amplitude is unknown: the branch
keys with a plain threshold on x until 0.8 s of keyed time (measured) lets
it seed the amplitude and key that stretch again with the likelihood. A
2 s time-out (heuristic) re-keys at the previous over's amplitude or
deletes the provisional characters, removing noise keyed after a station
stops. Each branch fits the station's timing (dit, dah/dit ratio,
weighting, gap timebase) to its own marks and spaces, helped by a coarse
speed from the rhythm of branch 1's keying. A selector publishes the text
of a branch whose fitted dit matches its own filter and whose fit explains
its intervals best, and corrects published text up to 20 s back (owner).
The bank costs 28.85 ms of CPU per channel-second at its defaults on the
development set (measured on the Linux machine: the development-set results
record, section 6, run `d5-bank`), against about 0.6 ms for Matched in
another run (milestone 2c bank results record, section 4.6). Exact form:
appendix A.8c, "Block cadence and order" and the sections "Keying time
constants" to "The bank decoder behind the engine".


### The bank: ladder, filters, likelihood

**Derivations.**

- **Ladder.** L_k = L_1 · ρ^(k−1), L_1 = β · 1.2 s / WPM_max, ρ = 1.1
  (dimensionless ratio of neighboring branches). With β = 0.8 dit and
  WPM_max = 100 words/min, L_1 = 0.8 · 1.2 s / 100 = 9.6 ms. The ladder
  stops at the first branch within one step of the optimum for
  WPM_min = 5 words/min, L_max = β · 1.2 s / WPM_min = 192 ms: the count is
  K = ⌈ln(L_max / L_1) / ln ρ − 10⁻⁹⌉ = 32, giving L_32 = 184.3 ms. One dit
  lasts 1.2 s / WPM, so each branch is the matched length β·dit of one
  speed.
- **Boxcar.** N_k = max(1, round(L_k · r)) samples (ties to even), the
  point where seconds become samples; at 2000 samples/s the same durations
  give 19 to 369 samples. It is computed as a running cumulative sum c
  (complex, summed in order from the stream's start),
  v_k[m] = (c[m+1] − c[max(m+1−N_k, 0)]) · (1/N_k): numpy divides a complex
  array by N_k + 0j by multiplying both parts by 1/N_k, which can differ
  from dividing by N_k in the last bit, so the port multiplies too. The
  first N_k − 1 outputs ramp up (zeros before the stream). |v_k| is
  computed as numpy computes it for complex128 (its vectorized loop): the
  larger of |Re|, |Im| times √(fma(ρ, ρ, 1)), ρ = smaller / larger (0 for
  0). The channel decoder stores |v_k|² rounded to float32, and every later
  stage reads those rounded values (FS²).
- **Envelope likelihood.** x = |v_k| / σ_v (σ_v in FS, the noise's
  per-component scale), a = ŝ / σ_v; Λ = −a²/2 + ln I₀(a·x) nats (Proakis &
  Salehi, eq. 4.5-21; the same expression as Matched). ln I₀ is the
  logarithm of the Abramowitz & Stegun polynomial approximations: 9.8.1 for
  z < 3.75 (I₀ itself, published error bound |ε| < 1.6 · 10⁻⁷) and 9.8.2 for
  z ≥ 3.75 (the scaled form √z · e⁻ᶻ · I₀(z), bound |ε| < 1.9 · 10⁻⁷); the
  bounds are the published ones, not re-measured here. The bank calls the
  same functions Matched uses (`kz4ap::log_bessel_i0`,
  `kz4ap::envelope_llr`, in `matched_front_end.cpp`). g is clipped to
  ±50 nats so that exp never overflows.

**Provenance.** WPM_min = 5 and WPM_max = 100 words/min and ρ = 1.1: owner;
β = 0.8 dit heuristic (the matched shape is derived, the fraction is not);
the logistic clip (±50 nats) and the 10⁻⁹ guard in the branch count:
numerical choices, not tuned.

### Noise

The estimate advances once per block of round(block_s · r) samples
(32 at r = 1500 samples/s), before that block is keyed. The three-tap
guard judges the block's samples against σ² as it stood at the block's
start; the spectrum's mask uses σ²_v,1 after that update. The inputs are u
(FS) and the branches' float32-rounded |v_k|² (FS²).

**Derivations.**

- **Three-tap level.** For each sample n of the block with n − n_s ≥ 2N_k,
  where n_s is the stream's first sample or, after an exact-zero input
  sample, the sample after the latest one, the middle tap |v_k[n−N_k]|² is
  accepted if it is below κ · 2σ² and both neighbors, |v_k[n]|² and
  |v_k[n−2N_k]|², are below κ_n · 2σ² (taps N_k apart share no inputs, so
  in white noise they are independent). W starts at 0.1 × the warm-up's
  sample count (48 at 1500 samples/s). m(κ) = 0.632 at κ = 1.75 is the mean
  of an exponential variable with mean 1 truncated at κ (in Gaussian noise
  |v|²/(2σ_v²) is exponential with mean 1; A.8b). A block in which no branch
  accepts a tap changes nothing, not even W. The floor of 10⁻²⁰ FS²
  (−200 dBFS) keeps x = |v|/σ_v finite on noise-free input.
- **Warm-up.** Up to and including the first block with which the non-zero
  input samples reach round(0.32 s · r) (480 at 1500 samples/s), σ² is
  recomputed each block holding such samples from the 20% quantile of
  |v_k|² over every non-zero input sample so far, kept by the estimator
  itself, by numpy's default ("linear", Hyndman & Fan type 7) definition,
  ported exactly (`bank::quantile_linear`). In Gaussian noise the 20%
  quantile of |v|² is 2σ_v²·(−ln 0.8), so in noise alone the estimate is
  consistent: it converges to σ_v² as the warm-up grows. Over the 480
  correlated samples of the warm-up it is not exactly unbiased (not
  computed).
- **Spectrum segments.** M = max(16, round(T_seg · r)) samples from sample
  0 (T_seg = 256/1500 s; M = 256 at 1500 samples/s, bins r/M = 5.86 Hz). A
  segment starting at sample s is examined in the first block whose end n₁
  satisfies s + M + N_1 − 1 + R ≤ n₁, R = round(g · r) samples (g = 4.8 ms:
  R = round(7.2) = 7 samples, 4.67 ms, at 1500 samples/s), because its mask
  needs |v_1|² that far ahead. Segments examined before the three-tap
  warm-up has ended are discarded. Sample u[i] feeds v_1[i … i + N_1 − 1],
  hence the mask's reach. The shape S starts at the first accepted
  periodogram. I_m is in FS² per bin; its mean over the M bins is the
  power per sample. The DFT is the engine's pocketfft in double precision
  (`detail::fft_forward` for `std::complex<double>`, `engine/src/fft.hpp`):
  unnormalized, bin m at m/M cycles per sample, m = 0 … M−1, the same
  definition and order as `numpy.fft.fft`.
- **What the mask removes.** Next to a mark the mask leaves out N_1 − 1 + R
  samples on each side where |v_1|² reaches the flag level as soon as the
  boxcar overlaps the mark (a strong station): 2 · (13 + 7) = 40 samples
  (26.7 ms) per gap at 1500 samples/s, 2 · (13 + 30) = 86 samples (57.3 ms)
  with stage 1's 20 ms margin. (The stage-2 spec's "20 ms before, 20 ms plus
  L_1 after", about 50 ms, counts L_1 once.) The clean fraction while a
  station sends PARIS at S₅₀₀ = 20 dB, from a noise-free model of this rule
  (the bench's keying with 5 ms edges, the flag level at the true σ²_v,1):
  35.9% at 40 words/min, 43.3% at 25 and 49.5% at 12 (stage 1's margin:
  20.5%, 26.8% and 40.8%; the spec's 21%, 28% and 42%), before the
  segments' 50% rule; the same model gives 2.07, 2.53 and 0.82 accepted
  segments per second (1.91, 1.69 and 0.59 at 20 ms).
- **Smoothing and branch weights.** A circular moving mean over 2h + 1
  bins, h = round(25 Hz · M / r) = 4 bins (±23.4 Hz at 1500 samples/s).
  W_k[m] (dimensionless) is the mean of |H_k(f)|² over 16 equally spaced
  frequencies across bin m (offsets ((j + ½)/16 − ½)·r/M, j = 0 … 15,
  around the bin's center as `numpy.fft.fftfreq` places it, so bins above
  M/2 are the negative frequencies).
- **Per-branch variances.** Variant (b) ("spectrum-level"): the complex
  power of v_k is (1/M)·Σ_m I_m W_k[m], half of it per real component.
  Before any segment has entered, σ²_v,k = σ²_v,1 · N_1/N_k, exact for
  white noise (Parseval).
- **Why the mask bias does not cancel.** The mask leaves out mostly
  low-frequency power (the station is near 0 Hz), so the masked spectrum
  reads each branch's noise low by a factor that differs per branch, and so
  does not cancel in the ratio. b_k = (σ²_v,k from the masked, smoothed
  spectrum) / (true σ²_v,k), the segment-weighted mean over the noise
  streams.
- **Exact zeros are missing data.** A receiver's output carries noise, so a
  run of input samples that are exactly 0 FS (both components; a
  zero-padded recording, a dead channel) is padding, not a measurement of
  the noise. An exact-zero input sample enters neither the three-tap
  warm-up nor the recovery's history or count, so a block whose input
  samples are all exactly 0 FS changes nothing in the three-tap estimate
  (not the warm-up, not σ², not W, not the recovery's count), and in a
  block that is only partly zero only the non-zero samples count: the
  warm-up runs on the first 0.32 s of non-zero input samples. A tap counts
  only from 2N_k samples after the sample following the latest exact zero:
  its middle and newest taps then hold no zero, and its oldest tap's boxcar
  may reach back into the zeros, exactly as at a stream's start. So a
  single isolated exact zero at sample z blocks branch k's taps at z and at
  the 2N_k samples after it (a tap at i counts only if i − z > 2N_k):
  2N_k + 1 samples in all, 29 (19.3 ms) for branch 1 (N_1 = 14) and 553
  (369 ms) for branch 32 (N_32 = 276). Until the first non-zero sample σ²
  is unknown (NaN), as before a stream's first block. In the spectrum, a
  segment whose samples are all exact zeros is not offered, and in any
  other segment each exact-zero sample is left out of the mask as a flagged
  sample is: a segment straddling the edge of a run of zeros is measured on
  its non-zero part (Σw² counts only those samples, so its level is not
  read low), it needs 50% non-zero, unflagged samples to enter, and an
  all-zero periodogram, hence a 0/0 ratio, cannot occur. Without exact
  zeros every one of these rules gives the prototype's arithmetic
  unchanged.
- **Why a stuck level needs a recovery.** On exact zeros the prototype's
  warm-up set σ² = 10⁻²⁰ FS² and, once noise arrived, no tap passed the
  guard again (acceptance (1 − e^(−κρ))(1 − e^(−κ_n ρ))², about 10⁻⁵⁴ per
  tap at ρ = σ̂²/σ² = 10⁻²⁰/0.036). The same lock-up follows any rise of
  the true noise level by a large factor faster than the estimate follows
  (at 60 dB, ρ = 10⁻⁶ and the acceptance is about 10⁻¹⁷ per tap), for
  example a band change or a receiver gain step. Hence the recovery: if
  branch 1 has accepted no middle tap for `noise_stuck_s` = 8 s (= 4 τ_n) of
  non-zero input, every branch's σ² is set again by the warm-up rule over
  that branch's last 0.32 s of non-zero input (a ring of 480 |v_k|² values
  per branch), W is kept and the update resumes; the firings are counted
  (`recoveries()`, written per channel to the replay tool's decoded files
  as `noise_recoveries`, with `noise_zero_blocks`).
- **Why the recovery is gated on branch 1** (for every method; the
  controller's ruling, option (A)). Branch 1 is the shortest boxcar
  (N_1 = 14 samples at 1500 samples/s). A tap at n reads v[n], v[n − N] and
  v[n − 2N], whose boxcars span the 3N_k samples n − 3N_k + 1 … n, so it
  lies entirely in noise when 3N_1 = 42 samples (28.0 ms) are free of the
  station; a character space, 3 dits = 3.6/WPM s, is 54 samples (36 ms)
  even at max_wpm = 100 words/min, so branch 1 sees noise in every
  character space at every speed of the ladder (assuming rectangular
  keying: the margin at 100 words/min is only 12 samples, 8 ms, which the
  keying's rise and fall and the channel filter's tails eat into; element
  spaces, 1 dit, suffice up to 1.2 · r/42 = 42.9 words/min). A long branch
  does not: branch 32 (N_32 = 276 samples) needs 3N_32 = 828 samples
  (0.552 s) free of the station, longer than a word space (7 dits =
  8.4/WPM s) above about 15.2 words/min, so during continuous keying faster
  than that it accepts no tap at all. For "spectrum" and "spectrum-level"
  only branch 1 has a three-tap estimate, so the gate is their only form.
- **Recovery after a noise rise.** The recovery fires at the first block
  end at or after the rise + 8 s, its 0.32 s window then holds only the new
  noise, and every branch restarts from a 480-sample warm-up estimate of
  the new level. From above (ρ > 1) every tap is accepted and the excess
  decays as e^(−t/τ_n), so within a factor 2 of the new σ²_v,k from the
  rise + 8 s + 0.32 s + 2 τ_n (12.32 s) on, given a warm-up estimate at
  most 1 + e² = 8.4 times too high; from below (ρ < 1) the guard truncates
  and the rise is slower and not exponential, so no time is derived for
  it. Variant (b)'s shape follows after branch 1 recovers, with β per
  segment: τ_n ln 2 = 1.39 s to half the new level.
- **A continuous carrier longer than 8 s** also triggers the recovery: the
  level is then set to the carrier's (an estimate cannot tell a carrier
  from noise by tap acceptance alone) and decays back with τ_n once the
  carrier stops. For a 10 s carrier A² = 10⁴/N_1 FS² (40 dB above branch
  1's noise power 1/N_1 FS² per complex sample, white noise of 1 FS²; ρ in
  units of the noise's σ²_v,1): at the recovery ρ = 2·10⁴ ·
  q/(2·(−ln 0.8)), q = 0.988 (the 20% quantile of |A + n|² is about
  A²(1 − 2·0.8416·σ_v,1/A)), 46.5 dB relative to the noise; for the
  remaining 2 s of carrier every tap is accepted and ρ decays with τ_n
  toward 2·10⁴/(2 m(κ)), so at the carrier's end ρ = 2.63·10⁴ (44.2 dB
  relative to the noise); then ρ decays at 10·log₁₀(e)/τ_n = 2.17 dB
  (relative to the noise) per s toward 1/m(κ) = 1.58 (conservative:
  truncation lowers the target near ρ = 1) and reaches 2 after
  τ_n ln((26 290 − 1.58)/(2 − 1.58)) = 22.1 s. Branch k of the "branch"
  fallback starts higher by N_k/N_1 and takes τ_n ln(N_k/N_1) longer
  (28.0 s for k = 32). For the spectrum methods' branches 2 to 32 the
  factor 2 by max_k t_k + 0.25 s = 28.3 s is a measured margin, not
  derived: the shape takes the carrier's segments after branch 1 recovers
  and their excess decays with τ_n from at most A²/(2σ²_v,k) (a bound on
  that factor alone), but the level and the shape's ratio each within 2
  bound their product only within 4. Measured checks of all three cases:
  Plan B record, section 5.

**Provenance.**

- κ = 1.75, κ_n = 4, τ_n = 2 s and the 0.32 s warm-up: Matched's
  (milestone 2; heuristic); m(κ) and the 2·(−ln 0.8) warm-up scale
  derived. Matched's floor lift is not part of the prototype and is not
  ported.
- T_seg, the ±25 Hz smoothing and the 50% clean fraction: heuristic. The 16
  points per bin and the 10⁻²⁰ FS² floor: numerical choices.
- **The guard margin g = 4.8 ms** (`guard_margin_s`; Plan B, B4b; stage-2
  spec section 3.3, owner 2026-10-03; stage 1: 20 ms): g = 0.5 · L_1, L_1
  = 0.8 dits at 100 words/min = 9.6 ms, branch 1's nominal length. The
  margin covers what the flag misses next to a mark (keying edges, the
  parts of a mark's rise and fall below the flag level): properties of
  branch 1's filter and of the transmitter, not of the station's speed, so
  its class is seconds tied to L_1, not dits. The value is heuristic. It
  was measured on the development set with stage 1's time constants in
  seconds (re-key wait 0.8 s, time-out 2 s, windows 2, 5 and 10 s), the
  owner's instruction (2026-10-04), so that its effect is separate from
  B4a's dits; accepted segments while a station sends and the development
  set: Plan B record, sections 7.2 and 7.3.
- **The noise method** "spectrum" (variant (a)): measured, prototype E10
  (stage-1 record, section 3.1).
- **The mask bias b_k** (`BankConfig::mask_bias`, 0.8381 at k = 1 …
  0.7812 at k = 32, at the 4.8 ms margin): measured (Plan B, B4b, white
  noise, 200 seeds; adopted by B4d, the owner's decision of 2026-10-06;
  Plan B record, sections 7.1 and 10) by stage 1's Task 5 method on the C++
  estimate (`kz4ap-noise-mask white`): white noise of 1 FS² per complex
  sample from `std::mt19937_64` and Box–Muller, seeds 1001–1200, 60 s
  each, rounded to complex64 (the engine's sample type), the mean over the
  seeds rounded to four decimals; per-seed scatter 1.9% (k = 1) to 3.2%
  (k = 32), so the mean carries a standard error of about 0.14% to 0.23%
  (scatter / √200, derived); 69 166 of 70 000 segments accepted, kept
  fraction 0.828. Method checks: on stage 1's own Task 5 noise (numpy's
  `default_rng`, seeds 101–110) the same estimate at 20 ms gives stage 1's
  table to 4.9 · 10⁻⁵ (largest difference), and on seed 101 at 4.8 ms its
  masked branch powers equal the prototype's to a relative 1.3 · 10⁻¹⁵ with
  equal segment counts; at 20 ms the 200-seed source gives 0.8405 to
  0.7849, within 0.5% of stage 1's table, so the two noise sources agree.
  B4b first adopted a ten-seed table on stage 1's noise (0.8281 to 0.7713),
  1.2% to 1.5% below the 200-seed values at every branch: mostly the
  sampling error of ten seeds (one sign at every branch because the
  branches see the same noise); the margin's own effect is about −0.3% to
  −0.5% (measured). In the default (variant (a)) only b_k / b_1 enters,
  and the change of table moves branch k's σ²_v,k relative to branch 1's by
  at most 0.25% (0.011 dB relative to the earlier ratio, derived).
  Stage 1's table
  (20 ms margin; 0.8370 to 0.7852) is kept as `kStage1MaskBias` for the
  golden tests. **Valid only** for the default ladder, T_seg, smoothing,
  guard margin, clean fraction, κ_n and three-tap settings at
  1500 samples/s, in white noise; not re-measured at other rates.
- Exact zeros as missing data: derived (Plan B, B3). The recovery after 8 s
  (4 τ_n) of branch 1 accepting no tap: heuristic (Plan B, B3), seconds
  because the noise process has no keying speed; the gate on branch 1
  derived for rectangular keying; the fact relied on is measured: the
  development set's 525 channels never fire it. A per-branch rule (the
  brief's first form) fired on the noise golden stream (20 s, 25 words/min
  keying from 1.0 s) in the "branch" fallback after 8 s and set the long
  branches to the station's power, 3 to 4 orders of magnitude above the
  noise (measured), which the gate prevents. History and tests: Plan B
  record, sections 5 and 9.1.

**Limitations.**

- In channel-shaped noise stage 1's mask-bias ratios were 2.9% (k = 1) to
  5.9% (k = 32) higher (measured in the prototype at 20 ms; not re-measured
  at 4.8 ms), so the estimate is about that much low there.
- A carrier longer than 8 s sets the level to the carrier's (above); the
  recovery cannot tell a carrier from noise.

### Keying time constants

Stage 1's re-key settings in seconds, the same for every branch (Plan B,
B4g; owner, 2026-10-06, option a′), and the periodicity windows in seconds
for every candidate (owner, 2026-10-06 and 2026-10-07). The keyer, the
channel and the periodicity estimator turn the seconds into samples where
they use them.

| Setting (`BankConfig` field) | Value | Class | Status |
|---|---|---|---|
| Re-key wait and time-out (`rekey_after_s`, `rekey_timeout_s`) | W_min = 0.8 s of keyed (key-down) time and the time-out 2 s of channel time from the amplitude becoming unknown, every branch; the seed's memory 4 · 0.8 s = 3.2 s of keyed time (4800 samples); both must be positive (0, which once selected the variants in dits, is refused) | seconds (stage 1's measured 0.8 s, E9b) | the wait measured (E9b, 0.8 s adopted over 0.4 s and 0.2 s at 25 words/min); the time-out heuristic (stage 1); the default again since B4g |
| Periodicity windows (`periodicity_windows_s`) | 5 and 10 s, the same for every candidate; stage 1's 2, 5 and 10 s until 2026-10-07 | (2) seconds: a latency after a change (a stream's start, a new over, a speed change), felt in seconds; the reach limit τ_c ≤ W/18.3 (4.4 and 2.2 words/min; 11 words/min for a 2 s window) is physics any rule must wait for (owner, 2026-10-06) | placeholder: stage 1's values (E1) less the 2 s window, dropped on a measured failure below 10 words/min (owner, 2026-10-07; "Periodicity" below) |

**Derivations.** Branch 1's settings feed the periodicity estimate: its
input p is branch 1's squelched posterior, which depends on branch 1's
amplitude, seeded and re-keyed after its wait (0.8 s of keyed time) and
its time-out (2 s); so the re-key settings change the periodicity
estimate's input as well as the keying.

**Removed variants** (off by default since B4g; removed on 2026-10-07,
when the bank was frozen as the reference; measured on the development set
of 21 test cases):

- The wait and time-out in each branch's nominal dit, W_min,k = 16.7·d_k
  and 2.5·W_min,k (B4a): Plan B record, section 6.
- The re-key clocks started at the first provisional mark, the stretch's
  lead 7·d_k, and a cleared time-out moving the stretch (B4d): Plan B
  record, section 10.
- The wait counted in marks, 8 provisional marks with a 7 s time-out
  (B4e): Plan B record, section 11.
- The two guards on the counted marks, filter full and length ≥ L_k (B4f):
  Plan B record, section 12.

The default's own run, `bank-b4g` against `bank-b3b`, is in section 13.

**Provenance.** The owner's decision of 2026-10-06, option a′ (Plan B,
B4g): stage 1's re-key settings are the default again; the stage-2 spec is
not edited. Plan B's variants were removed with the owner's approval of
2026-10-07 (design spec section 5.2, the amendments of 2026-10-07: the
bank frozen as the reference).

**Limitations.** After a station stops, its branch's false characters
(provisional keying of noise) can stay published up to the time-out, 2 s,
inside the 20 s correction reach. The default keeps stage 1's known
losses: the time-out in seconds counts from the amplitude becoming
unknown, and the stretch reaches back to it (B4a's re-key trace; Plan B
record, section 6.5).

### Keying

Every branch is keyed separately, all 32 once per block, in this order: the
noise estimate is updated, then the block is keyed with the amplitude as it
stood at the block's start, then the amplitude is updated from the block.
The inputs are |v_k|² (FS², float32-rounded, widened exactly to double) and
σ²_v,k (FS², per real component); the keyer rounds nothing. The lengths L_k
it uses are the realized ones, N_k / r (s).

**Derivations.**

- **Posterior.** The posterior that leaves the keyer (for the periodicity
  estimate) is p where the squelch is open and 0 where it is closed; the
  amplitude update uses p before the squelch.
- **Squelch.** a_min,k = 3 · (L_k / 16 ms)^(1/4): 2.622 at L_1 =
  14/1500 s = 9.33 ms to 5.525 at L_32 = 276/1500 s = 184 ms. In noise
  alone ŝ² is a mean over about τ_a / L_k independent samples, so its
  spread grows as √L_k and a_min as L_k^(1/4) (milestone 2's principle with
  each branch's own σ_v).
- **Full-LLR keying.** Up wins if both conditions hold (they cannot both
  hold for h > 0). The prototype's vectorized hysteresis (the last event at
  or before each sample) is ported as a per-sample loop with the same state
  at every sample (`bank::hysteresis`).
- **Unknown-amplitude test.** While branch k's amplitude is unknown (from
  the stream's start, and from each `start_over` until
  `finish_over_start`), the key is a threshold test on x alone.
  x_off = √(−2 ln 0.3) = 1.552: noise alone (Rayleigh, P(x > X) =
  e^(−X²/2)) is above it 30% of the time. Without calibrated values the
  nominal x_on,k = √(−2 ln min(0.5, R_fa · L_k)) is used (4.308 at k = 1 …
  3.549 at k = 32; heuristic: the envelope's upcrossings make it key 7–10×
  more than R_fa); the clamp at 0.5 is a numerical guard
  (x_on ≥ √(2 ln 2) = 1.177), never active at the defaults. Marks keyed
  this way are provisional: the test keys down when x rises past x_on and
  up when it falls below x_off, low on the boxcar's ramps, so noise-free a
  rectangular mark of length d ≥ L_k measures
  d + L_k · (1 − (x_on + x_off)/a), up to L_k longer at high SNR, and
  noise delays the key-up (a space little longer than L_k can close up;
  measured in the prototype).
- **Amplitude, known (online EM).** m = Σ p·|v_k|² / max(w, 10⁻³⁰⁰);
  the update runs only if w > 0; s = min(1, max(1 − (1 − α)^w,
  w / max(W, 10⁻³⁰⁰))). The Rician mean square is 2σ² + s² (milestone 2's
  estimator, as Matched's). The sums are computed in numpy's pairwise
  order (`np.sum` along a row; numpy's `pairwise_sum` ported in
  keying.cpp), the same mathematical sum.
- **Amplitude, unknown (seed).** While unknown, w = 0 (no EM step) and only
  keyed samples count: each block's keyed |v_k|² are appended to a memory
  of at most the most recent round(4 · W_min · r) samples (4800 samples,
  3.2 s, on every branch at W_min = 0.8 s, as in stage 1), W grows by the number keyed, and Q₀.₉ is
  numpy's "linear" quantile (since Plan B's B4f computed by selecting the
  two order statistics it needs, `std::nth_element`, instead of sorting:
  the same value bit for bit, tested). The 90% quantile, not the mean, because the
  mean is pulled down by the boxcar's ramps, which the test keys.
- **Re-key bookkeeping.** `ready_to_rekey`: unknown and W ≥ W_min · r
  samples of keyed time (1200 samples at 0.8 s and 1500 samples/s, not
  rounded). The keyer reads W_min (`rekey_after_s`, s) from the
  configuration. `start_over(k)` keeps an established ŝ² as the
  fallback (`prev_amp2`; NaN while there is none), then sets ŝ² = 0,
  W = 0, empties the memory and marks k unknown.
  `finish_over_start(k, ŝ², key)` installs the re-keying's winning ŝ²
  (FS²), empties the memory, marks k known and sets its key state to the
  re-keyed stretch's last; W keeps the keyed-sample count (≥ W_min when
  called once ready), so the first EM steps after the switch move ŝ² by at
  most w / W rather than replacing it. The channel calls them after each
  block's step, branch by branch: (1) `start_over(k)` when a new over is
  due (the key is up and more than T_new = max(0.5 s, 12 · T_g) has passed
  since branch k's last key-up in this over; none yet: not due; T_g the
  current fit's, or 1.2 s / 5 = 0.24 s without a fit), whether or not the
  amplitude is known; otherwise, while k is unknown: (2)
  `finish_over_start(k, …)` after the stretch is re-keyed, once the over
  has a mark and W ≥ W_min (candidates: ŝ² and, if finite, `prev_amp2`);
  or, if not, once the time-out has passed since its count started (the
  amplitude becoming unknown, the stream's start or a `start_over` of a
  known branch, or the last time-out; a new over of a still-unknown branch
  does not reset it), (3) `finish_over_start(k, …)` with `prev_amp2` if it
  is finite and keys at least one sample of the stretch, and otherwise (4)
  `start_over(k)` again, which keeps k unknown so the keyed time counts
  afresh from then on.
- **Re-keying a stretch (`rekey`).** The stored |v_k|² of a stretch are
  keyed again from key up with the full LLR at fixed σ²_v,k and ŝ²
  (a = √(max(ŝ², 0) / σ²_v,k)), the same g, h = 1 nat and squelch.
- **Edges.** `edges` lists, per branch, (sample index, key state after it)
  at every change of key state against the state before the block: the
  marks and spaces the timing stage reads.
- **Exact zeros.** The keyer never sees a NaN σ²: while the noise estimate
  has none (only exact zeros so far) the channel does not call it. A run
  of exact zeros after the first estimate is keyed with σ² as it stood
  before the run (the noise estimate holds it): |v|² = 0 FS² gives x = 0,
  which keys nothing.

**Provenance.** P₁ = 0.44 derived (PARIS: key-down 22 of 50 dit units);
τ_a = 0.5 s heuristic (a heuristic running form of an EM update); h = 1 nat
heuristic; the squelch's 3 at 16 ms heuristic, its L^(1/4) scaling derived;
R_fa = 0.01 /s a heuristic target kept by E9b; the release probability 0.3
heuristic; the wait W_min = 0.8 s of keyed time measured (E9b, at 25
words/min, adopted over 0.4 s and 0.2 s; stage-1 record, section 3.9; the
default again since Plan B's B4g); the seed's memory of 4 × W_min and the
90% quantile heuristic; the 0.5
clamp and the 10⁻³⁰⁰ guards numerical choices. **x_on,k** (4.6428 at k = 1
… 4.2036 at k = 32, `BankConfig::x_on_values`): measured, prototype
experiment E9a (stage-1 record, section 3.9): channel-shaped noise, about
20 noise key-downs per branch in 2000 s, at a target R_fa = 0.01 false
marks/s per branch; not monotone in k (sampling scatter). **Valid** for
the 129-tap channel filter's noise shape, the default ladder at
1500 samples/s, and x measured with the exact σ²_v,k.

**Limitations.** The decoder divides by its estimated σ²_v,k, whose
white-noise mask bias is 2.9–5.9% off in channel-shaped noise: a 5% low σ²
raises x by about 2.5% and the false key-down rate by about 1.6×
(estimated in the prototype, not measured).

### Duration fit

`DurationFit`, `class_priors`, `resolution_var_s2`, `class_logliks`,
`observations_loglik`, `classify_mark`, `classify_space`. Durations in s,
variances in s², log-likelihoods in nats; densities in ln(duration).

**Derivations.**

- **Dropped classes.** A class whose median is not positive (the element
  space when w ≥ T) is dropped without renormalizing the others' priors
  (the prototype's ruling: a penalty of about ln(1 − 0.647) = −1.04 nats
  per space on a reading with no element spaces).
- **Class priors** (from VE3NEA's CW character frequencies and word-length
  table, `kz4ap_synth.messages`): per character its dits and dahs, its
  elements minus one element spaces, a character gap after every character
  but a word's last ((L − 1)/L per character) and a word gap per word
  (1/L), L = 3.062 characters the mean word length.
- **Outlier class.** Density ε / ln(10 s / 1 ms) in ln d, a log-density of
  −5.216 nats; every duration is clamped to [1 ms, 10 s] before it is
  scored, so the density is proper. A duration ≤ 0 s is an error in
  `class_logliks`, `observations_loglik` and the classifiers
  (`std::invalid_argument`, the prototype's ValueError); `DurationFit::add`
  ignores it.
- **Timing resolution.** An edge through a boxcar of length L_k is a ramp
  of slope ŝ/L_k, so noise of RMS σ_v moves its crossing by L_k / a
  (a = ŝ / σ_v); a duration has two edges; sampling at r adds 1/(12 r²) per
  edge (first order, high SNR; a floored at 1). σ_t² is turned into ln
  units to first order (σ_t²/μ_c²).
- **Combining classes.** ℓ_c is −∞ for the other kind of interval (a mark
  is never a space). The reference form (the prototype's) combines the
  classes in class order with logaddexp and then with the outlier:
  ln(e^x + e^y) = max(x, y) + log1p(exp(−|x − y|)), and x + ln 2 where
  x = y (numpy's formula; both −∞ gives −∞).
- **Evaluation, exact steps (Plan B, B2(a)).** Three restructurings that
  change no bit of any result (from IEEE arithmetic; tested bit for bit
  against a frozen copy of the earlier code). (1) Only the classes of the
  observation's kind are evaluated (2 for a mark, 3 for a space): the
  other kind's −∞ terms entered the logaddexp chain only as
  logaddexp(x, −∞) = x + log1p(0) = x, and the refinement only as
  responsibilities exp(−∞ − ℓ_total) = 0, whose products add ±0 to sums
  that are never −0. (2) logaddexp returns max(x, y) without calling exp
  and log1p when |x − y| ≥ (58 − k) ln 2 nats, k the binary exponent of
  max(x, y) (2^k ≤ |max| < 2^(k+1), k ≥ −1000): then exp(−|x − y|) ≤
  2^(k−58), less than a sixteenth of half the spacing of the doubles next
  to max(x, y), so numpy's formula rounds to max(x, y) exactly (the proof,
  with the libm error allowance, is in `fit.cpp` at `lae`). With the
  outlier's −5.216 nats as the larger term (k = 2) the threshold is
  38.8 nats. (Since the near-exact step below, the fit's own evaluation no
  longer calls logaddexp; the function keeps the skip.) (3) The best-fit
  search evaluates the retained history's terms 3 times instead of 5 (with
  2 refinement steps): the grid point's terms are the refinement's
  starting terms and the refined point's are its last, so they are reused
  for the acceptance test and the quality.
- **Evaluation, near-exact step (Plan B, B2(a)).** ℓ_total =
  m + ln(1 + Σ e^(x − m)), m the largest of the terms of the observation's
  kind and the outlier's −5.216 nats (the first largest; the outlier where
  a class only ties it), the sum over the other terms (one exp per term
  that is not the largest and one ln, instead of one exp and one log1p per
  term). The sum starts at the largest term's 1 and the others are added
  after it in class order, the outlier last. Terms more than 40 nats below
  m are left out, which changes no bit: every partial sum is ≥ 1, where
  the doubles are at least 2^−52 apart, and a left-out term is below
  e^−40 (1 + 2^−52) < 4.3 · 10⁻¹⁸ < 2^−53, so adding it would round back to
  the same partial sum. The ln is skipped where the sum is exactly 1. In
  the grid's log-likelihood a class whose term cannot come within 41 nats
  of the outlier's is left out before its ln s_c² is computed: s_c² ≥
  σ_ln², so ℓ_c ≤ ln((1 − ε) P_c) − ½ z²/s_c² − ½ ln σ_ln² − ln √(2π), a
  bound with no logarithm of the observation (1 nat of the 41 covers
  rounding); the result is the same bit for bit as with the term
  evaluated. The one-pass sum itself changes the last bits of ℓ_total; the
  per-class ℓ_c, s_c² and the classifications do not change. A NaN term
  gives NaN, otherwise a +∞ term gives +∞ (as the chain); a class term is
  never +∞ while σ_ln > 0.
- **The one-pass sum's bound against the logaddexp chain.** Each ℓ_total
  (the grid's and `class_logliks`') lies within 30 · 2^−52 · (|ℓ_total| +
  2 nats) of the chain's (a count of at most 15 roundings per evaluation,
  each within one unit in the last place, on quantities of magnitude
  ≤ |ℓ_total| + 2 nats), and the weighted log-likelihood at a fixed θ
  within Σ λ^age |Δℓ_total| plus each side's rounding of numpy's pairwise
  sum, (⌈n/8⌉ + ⌈log₂ n⌉ + 4) · 2^−53 · Σ |λ^age ℓ_total|, and of the
  prior's addition. This bound comes within 1% of the measured difference
  where one observation's difference dominates (a short history, or one
  term much larger than the others), because its first term is then the
  difference itself: derived and holding by construction, not fitted, and
  tight there.
- **SLEEF's exp and ln (Plan B, B2(b)).** The fit's exp and ln are SLEEF
  3.9.0's vectorized functions (`_u10`: a stated error bound of 1.0 unit
  in the last place) instead of the C library's: every ln s_c² of a class
  term (on the grid and on the retained history), the one-pass sum's
  e^(x − m) and its ln, and the refinement's responsibilities
  e^(ℓ_c − ℓ_total). They are evaluated on arrays: the grid's points in
  blocks of 256 (per block every class term's s_c², then all their ln
  together, then the terms, then every e^(x − m) of the block, then every
  ln of a sum), the retained history's observations all at once. Per value
  the operations and their order are unchanged; only exp and ln are
  SLEEF's. (A term already known to be −∞, with its median not positive or
  below the bound above, gets no ln s_c².) The implementation is chosen
  once at run time (`engine/src/bank/vecmath.cpp`): SLEEF's "finz"
  functions (with fused multiply-add) on AVX-512F, 8 doubles per call, or
  on AVX2 with FMA, 4 doubles; on a processor without them its "cinz"
  functions (no FMA) on AVX-512F, AVX or SSE2 (8, 4 or 2 doubles). SLEEF
  states that each family gives the same bits with every instruction set;
  so the fit's values are the same on every processor with AVX2 and FMA
  (most Intel Core processors since Haswell, 2013, and AMD since
  Excavator, 2015; not every Pentium, Celeron or Atom-class one), and a
  processor without them gives other last bits. Each instruction set's
  code is compiled in its own file for that set alone (on MSVC the AVX-512
  file as SLEEF compiles its own: AVX2 code generation, AVX-512F
  intrinsics), and each runs only where the processor reports the
  extensions its file may use, so the program runs on every x86-64
  processor (by construction and by the symbol check of the Plan B record,
  section 4.1, not by a run on a processor without AVX). The environment
  variable `KZ4AP_FIT_MATH` (`cinz`, `finz` or a full name) restricts the
  choice, for the tests and for one development-set replay only.
- **SLEEF's bound.** Against the one-pass code with the C library's exp and
  ln, each ℓ_total lies within 1.001 · 2^−52 · (5 |ℓ_total| + 14.3 +
  |ln σ_ln²|) nats (18.1 nats for the constant with σ_ln = 0.15;
  7.4 · 10⁻¹⁵ nats at ℓ_total = −3 nats), and the weighted log-likelihood
  at a fixed θ within the bound above from those totals' differences.
  Derivation (in `fit_test.cpp`, `sleef_bound`), with every exp and ln
  within 1 ulp of the exact value (SLEEF's stated bound; assumed, not
  proven, of the C libraries): the difference is each side's evaluation
  error of its own terms plus the exact log-sum-exp's change between the
  two sides' terms. Each side's one-pass evaluation errs by at most
  4.44 · 2^−52 + 2^−53 |ℓ_total| (the sum S ∈ [1, 4] off by at most
  3.05 · 2^−52 relative after ≤ 3 subtractions, exps and additions; its ln
  by 2^−52 ln 4 more; the final addition). The log-sum-exp moves by the
  responsibility-weighted mean of the terms' changes, Σ p_c |Δx_c| with
  Σ p_c ≤ 1; a term x = (a − ½ ln s²) − ln √(2π) changes by at most
  2^−52 (|ln s²| + 2|x| + 1), as the two ln s² differ by at most 2 ulp, and
  |ln s²| ≤ 2|x| + |ln σ_ln²| (ln((1 − ε) P_c) < 0 and z²/s² ≥ 0, else
  s² ≥ σ_ln²); with p |x − ℓ_total| = p |ln p| ≤ 1/e per class this gives
  2^−52 (4 |ℓ_total| + 5.42 + |ln σ_ln²|). The factor 1.001 covers the
  second-order terms. Against the logaddexp chain each ℓ_total lies within
  the sum of the two bounds.
- **Grid sizes and memory.** T on a log grid from 1.2 s / 100 = 12 ms in
  steps of 1% (×1.01) up to the first point ≥ 1.2 s / 5 = 240 ms: 303
  points, 12 ms to 242.2 ms. Marks over (T, q, w), 303 × 3 × 4 = 3636
  points; spaces over (T, w, T_g), 303 × 4 × 5 = 6060 points.
  λ = e^(−1/N_mem) = 0.97938, so the tables hold the exponentially weighted
  log-likelihood exactly (untruncated memory). The memory's weight is
  W ← λW + 1 elements. The last ⌈4 N_mem⌉ = 192 observations are retained
  for the refinement, its acceptance test and the quality, with weights
  λ^age (age 0 the newest); the tail left out weighs λ^192 = e^(−4) = 1.8%
  of the total.
- **Grid maximum.** The first maximum in (T, w) order is taken, then the
  first best q and T_g there: θ_grid = (T, (w/T)·T, q·T, (T_g/T)·T).
- **Refinement.** From θ_grid, EM-style: the class responsibilities of the
  current point are held fixed per step. Residual ln d − ln μ_c, Jacobian
  DESIGN_c / μ_c (1/s), weights λ^age · responsibility / s_c²; the T_P
  prior as one more residual ln T_P − ln T with weight π / σ_P²; damping
  toward the current point with standard deviation 0.2 T per parameter
  (1/(0.2 T)² on the diagonal), which keeps unobserved classes where they
  are. The 4 × 4 system is solved by LU with partial pivoting. After each
  step T is floored at 0.1 ms and the others clipped.
- **Acceptance and quality.** The refined point is kept only if its
  weighted log-likelihood on the retained history, Σ λ^age ℓ_total, plus
  the prior term, is at least the grid point's; otherwise θ_grid is kept.
  `best` returns T, q, w, T_g, Q and W; nothing before the first
  observation. A space is the class among element, character and word with
  the largest ℓ (the first on a tie). `observations_loglik` is the mean of
  ℓ_total over a set of observations, nats per element.

**Provenance.** N_mem = 48 measured (E4: adopted over 24 and 12; stage-1
record, section 3.7); the q, w/T and T_g/T grids measured (E5, the
"coarse" variant adopted; stage-1 record, section 3.8); the T step of 1% a
placeholder kept by E5; σ_ln = 0.15 (marks) and 0.25 (spaces), ε = 0.05,
the outlier range 1 ms–10 s, σ_P = 0.1 in ln T, 2 refinement steps, the
damping 0.2 T and the clipping bounds heuristic; the class priors, σ_t² and
the 1.8% tail derived; the 0.1 ms floor on T a numerical choice; the 40-nat
leave-out and the logaddexp skip threshold derived; the order of the
SLEEF implementations measured, not derived (the 2-double SSE2 cinz
functions are slower than the C library's ln and made the decoder slower
on both machines; the cinz AVX and AVX-512 versions made it faster on the
Linux machine only, the FMA versions on both; Plan B record, section 4);
the block of 256 a heuristic (25 856 B of stack scratch per call, no
measured effect claimed). Measured differences against the bounds, the
bit-for-bit checks and the cost: Plan B record, sections 3, 4 and 9.4.

**Limitations.** The fit's last bits depend on the processor's instruction
sets (AVX2 with FMA or not). The prototype's strict expected failure,
Farnsworth T_g with the E5 grids, holds in the port: T = 66.72 ms,
T_g = 192.2 ms against 207.0 ms.

### Periodicity

`t_grid`, `_normalized_acf`, `comb_estimate`, `Periodicity`. Only the comb
on the dit-plus-space period Π = 2T is ported: the prototype's default and
the owner's choice (E1). The prototype's other two methods (a sign-weighted
comb on the edges of p, and a fit to the nulls of p's spectrum) were not
adopted and are not ported; any other `periodicity_method` is an error
(`std::invalid_argument`).

**Derivations.**

- **Input.** p is averaged down by factor = max(1, round(r / 750
  samples/s)) samples (ties to even): 2 at r = 1500 samples/s (at
  2000 samples/s factor 3, r_P = 666.7 samples/s). Each averaged sample is
  the mean of factor consecutive samples of p; a remainder shorter than
  factor waits for the next block. A buffer keeps the last N_max averaged
  samples, the longest window (7500 samples, 10 s, at 750 samples/s, by
  default).
- **Windows and updates (stage 1's rule; 2 s dropped 2026-10-07).**
  The estimate uses only recent p, because the station changes (a stream
  starts, an over begins, the speed changes): a short window follows a
  change quickly, a long one is steadier. Two windows W = 5 and
  10 s shared by every candidate: N = max(16, round(W · r_P)) = 3750 and
  7500 samples, each the most recent N samples, each computed by one FFT
  (stage 1's 2 s window, 1500 samples, by `--set
  periodicity_windows_s=[2,5,10]`).
  The estimate is recomputed once round(0.25 s · r) samples of p (375 at
  1500 samples/s) have arrived since the last recomputation; between
  recomputations the last result stands. A window the buffer does not yet
  fill gives no estimate (score 0).
- **Autocorrelation.** Over one window's N samples, x = p − mean(p) and
  ρ[τ] = Σ_{m=0}^{N−1−τ} x[m] x[m+τ] / Σ_m x[m]², τ = 0 … N − 1
  (dimensionless; 1 at τ = 0); by FFT zero-padded to the smallest power of
  two ≥ 2N (so the circular correlation equals the linear one): 8192 and
  16 384 points by default (4096 for a 2 s window). No estimate when N < 16 or Σ x² ≤ 10⁻¹² · N (p does not
  vary; for instance all zero while the squelch is closed).
- Removed (2026-10-07): windows per candidate, N_w · τ_c, computed by
  sliding sums of lagged products (Plan B, B4a; Plan B record, sections 6
  and 9.5).
- **Candidates.** The duration fit's grid; the 1.01 is written into
  `t_grid` itself, not read from `t_grid_step`.
- **Comb.** For each candidate T, Π = 2T, in samples Π · r_P. A tooth at
  lag c is the mean of ρ over the lags ⌊c − 0.075 Π⌋ … ⌈c + 0.075 Π⌉ (±15%
  of T), clipped to 0 … N − 1; teeth at 2T, 4T, 6T and 8T, negative teeth
  halfway between. **Why 2T and not T:** Morse keying is not periodic, but
  it lives on a grid of dits, and consecutive keying edges T apart have
  opposite signs, so p's structure repeats at 2T; its autocorrelation is
  low at odd and high at even multiples of T, and a comb with teeth at
  multiples of T would peak at 2T (the comb was corrected to Π = 2T on
  2026-09-30, spec 4.4). **Score values.** The score is dimensionless and
  bounded by 2 in magnitude (an unbroken string of dits scores about 1.5).
  For random machine-keyed text the true dit scores about 0.31–0.37 and its
  alias at three times it about 0.18–0.24 (derived from the keying's
  autocorrelation as measured on noise-free keying:
  `docs/research/2026-10-05-periodicity-windows-and-rekey-analysis.md`,
  §2).
- **Reach.** A candidate counts only if its outermost lag,
  (4 + ½ + 0.075) Π = 9.15 T, is at most (N − 1)/2 samples. With the
  default fixed windows this caps T at (N − 1) / (18.3 r_P) ≈ W / 18.3:
  273 ms (4.4 words/min) in 5 s and 546 ms (2.2 words/min) in 10 s, so
  every candidate of the grid (≤ 242.2 ms) is within reach of both default
  windows (stage 1's 2 s window: 109 ms, 11 words/min); a window whose
  true T is beyond its cap still returns its best candidate within reach
  (often an alias at a fraction of T, or a candidate that is not
  confident). The estimate is the candidate with the largest
  score (the first on a tie; none if a score is NaN); none if no candidate
  is within reach.
- **Taper.** The biased estimate tapers each tooth by about (1 − τ/N), so
  a tooth's contrast shrinks with its lag. With the default fixed windows
  it depends on speed: at the 8T tooth 1 − 8 T/W, 0.92 at 25 words/min
  (T = 48 ms) in the 5 s window (0.81 in a 2 s window), about 0.62 for
  5 words/min (8T = 1.92 s) in the 5 s window; it lowers long-T scores in short windows relative to
  short-T ones.
- **T_P.** At each recomputation the windows are estimated shortest first;
  T_P is the estimate of the shortest window whose score is ≥ 0.03, the
  confidence is that score, and the window (s, N / r_P) is reported with
  it. If no window is confident, T_P is none and
  the confidence is max(0, every window's score). T_P never feeds back into
  its own estimate (spec 4.4).

Removed (2026-10-07): one window per row following the selected branch's
eligible fitted dit, N_w · T̂, which fed back from selection to T_P (Plan
B, B4a-C; Plan B record, sections 6.6, 6.6.1 and 9.5).

**Provenance.** The method (the comb on Π = 2T): owner (E1; stage-1
record, section 3.2). The windows for every candidate: stage 1's 2, 5
and 10 s, a placeholder (E2; stage-1 record, section 3.3), class (2)
seconds, a latency after a change (the owner's decision of 2026-10-06,
after B4a's windows in dits raised the paired CER and the aliases near
3 T₀; stage-2 spec, decision record section 6; Plan B record, sections 6
and 10); the 2 s window dropped by the owner on 2026-10-07 (decision
record section 7), leaving 5 and 10 s.

**Why the 2 s window was dropped** (measured on the development set's
pilot, speed cell 8–10 words/min, by tracing the decoder; development-set
results record, section 6). A window W scores only T ≤ W/18.3, 109 ms at
2 s, so for a station slower than about 10 words/min the 2 s window's best
candidate within reach is an alias, most often near T₀/3, scoring 0.035 to
0.052 (median 0.041), just above the 0.03 threshold. The rule "the
shortest confident window gives T_P" then let it replace the 5 s window's
correct estimate (score 0.2 to 0.36) in about one update in five, and
while it stood the T_P prior (σ_P = 0.1 in ln T) pulled the fits to
T ≈ 0.51 T₀: ½ (ln 3 / 0.1)² = 60 nats at T = T₀ against 9 nats at
T = 0.51 T₀ (derived), more than the likelihood the fit loses (measured
outcome). An element space then read as a character gap ("S" as "EEE").
Between 10 and 11 words/min the 2 s window's top candidate within reach
(108 ms, 0.91 to 0.97 T₀) still won and did little harm. The 5 and 10 s
windows reach every candidate, so the failure cannot arise from the reach;
an alias of the 5 s window can still win when it is the shortest
confident one. The
confidence threshold 0.03: a placeholder (E1, set with the fixed windows;
re-measured with the windows in dits by Plan B's B4a and kept, Plan B
record, section 6.4). The 4 teeth
and their half-width 0.075 Π: a placeholder (E3; stage-1 record, section
3.4), not measured for the comb on 2T. The update interval 0.25 s and the
averaged rate 750 samples/s: heuristic. Π = 2T, the reach caps and the
taper's size: derived. The reach rule and the biased estimate: heuristic
(the unbiased estimate is noisier at long lags). The 1% grid step: the
duration fit's (a placeholder kept by E5). The 10⁻¹² · N variance floor and the 16-sample
minimum: numerical choices.

**Limitations.** A window scores only once full, so at a stream's start
there is no T_P for the first 5 s (2 s with stage 1's windows), and after
a speed change the 5 s window is the quickest to follow it (derived); the
fits meanwhile run without the prior or under the previous T_P. The rule
still takes the shortest confident window regardless of a longer window's
higher score. The prototype's strict expected
failure (Farnsworth 18/10 words/min, where the comb locks near the gap
timebase) holds in the port.

### Text model and branch selection

`decode_pattern`, `TextModel`, `BranchView`, `Selector` (`selection.cpp`;
the prototype's `text.py` and `select.py`). Every branch decodes its own text; selection chooses the
branch whose text is published (spec 4.6).

**Derivations.**

- **Characters.** A character's dot/dash pattern becomes its symbol by the
  code table of `kz4ap::morse` (identical to `kz4ap_synth.morse`): eight or
  more dits read "<HH>", a pattern with no code "*".
- **Text model.** VE3NEA's CW character frequencies (`kz4ap_synth.messages`,
  MIT; 41 symbols, weights summing to 2688): ln(w / 2688) for a symbol in
  their table (E: ln(321/2688) = −2.125 nats); a valid code missing from it
  (prosigns other than <BT>, rarer punctuation) gets their rarest
  character's, ln(8/2688) = −5.817 nats; "*" and anything else
  ln(10⁻⁶) = −13.816 nats. A branch's text score is the mean over its
  characters, nats per character, word spaces left out (none without
  characters). The window is applied by the channel decoder when it builds
  the views; the module scores the characters it is given, newest first,
  as the prototype sums them (the mean uses Python's compensated float
  sum, as the prototype's `sum` does).
- **Eligibility.** ln 1.1 = 0.0953 (one ladder step); also T_k > 0 s.
- **Best branch.** A branch with no text yet is not read as infinitely
  unlikely: the text step is then skipped. A fallback pick by T_P: the
  branch whose length is nearest 0.8 T_P in ln L (the first on a tie), only
  when T_P is confident.
- **Switching.** A run of one kind (eligible or fallback) does not complete
  a run of the other. One update may stand for several instants (the
  caller counts them); an update with no instant changes nothing, not even
  the eligibility times. For each branch the stream time (s) at which its
  current eligible run began is kept (none while it is not eligible); the
  channel decoder uses it to decide how far back a switch replaces
  published text.

**Provenance.** M = 4 a placeholder kept by E6 (stage-1 record, section
3.11); ε_Q = 0.05 nats per element and the text window of 10 characters
placeholders kept by E8 (section 3.12); the eligibility tolerance ln 1.1,
the minimum fit weight of 8 elements, the text tie of 0.1 nats per
character, the text separation of 1.0 nats per character and the
log-probabilities given to codes missing from the table and to "*"
heuristic; the character probabilities derived from VE3NEA's table.

### Channel decoder

`Output`, `Branch`, `ChannelDecoder.run` as the streaming `BankChannel`. It
decodes one station's baseband stream u (station at 0 Hz, FS, r samples/s).

**Derivations.**

- **Blocks.** B = max(1, round(block_s · r)) samples (43 at
  2000 samples/s, 21.5 ms). Input may arrive in pieces of any length
  (`push`): a block is processed as soon as its last sample has arrived,
  and the remainder waits; at the end (`finish`) the remainder is
  processed as one shorter block, as the prototype's last block, and the
  result does not depend on how the stream was split. As each sample
  arrives, every branch's |v_k|² is computed from the running cumulative
  sum and stored rounded to float32, as a 4-byte float.
- **Marks, spaces and characters.** A mark is key-down to key-up, a space
  key-up to key-down, each with the timing variance σ_t² of the branch at
  its amplitude a_k. A duration > 0 s enters the branch's fit unless it was
  keyed by the unknown-amplitude test (provisional: lengthened by up to
  L_k). Nothing is classified while the branch has no fit. A word gap adds
  a word space " " at the character's end time.
- **New over.** The branch keeps its fit as the previous over's if it has
  observations and none is kept yet; its amplitude becomes unknown for the
  keyer's unknown-amplitude test from the next block. T_new without a fit:
  T_g = 1.2 s / 5 = 240 ms, so T_new = 2.88 s; at 25 words/min,
  T_g = 48 ms, T_new = 0.576 s.
- **The stretch and the time-out's count.** The stretch a re-key keys
  again starts where the amplitude became unknown (≤ 20 s back), and the
  time-out counts from there (stage 1's rule). A new over started while
  the amplitude is still unknown moves neither.
- Removed (2026-10-07): the count started at the branch's first
  provisional mark, the stretch's lead 7 · d_k before it, and a cleared
  time-out moving the stretch (Plan B, B4d; Plan B record, section 10; the
  re-key trace behind it: `docs/research/
  2026-10-05-periodicity-windows-and-rekey-analysis.md` §3.2).
- **Re-key.** The stretch is keyed again at each candidate amplitude: the
  seed s² and, if there is one, the previous over's s² (FS²). For each,
  the stretch's marks and spaces go into a fresh fit and into a copy of
  the previous over's fit continued; the fresh fit is taken only if it
  wins (below), else the continued one decodes and the fresh one stays its
  rival. The candidate whose taken fit has the best mean log-likelihood
  per element (nats; the first on a tie) wins: its fit and amplitude are
  kept, the stretch's characters are decoded again with it, and the keyer
  switches to the full LLR at that amplitude. The re-keyed characters
  replace the published ones from the stretch's start if the branch is the
  selected one.
- **Wait.** The re-key comes once the over has a mark and W ≥ W_min =
  0.8 s of keyed time (1200 samples at 1500 samples/s).
- Removed (2026-10-07): the wait counted in provisional marks, 8 with a
  7 s time-out (Plan B, B4e; Plan B record, section 11), and its two guards
  on the counted marks (B4f; section 12).
- **Time-out.** 2 s for every branch (3000 samples at 1500 samples/s),
  rounded to whole samples, counted as above. When nothing in the
  stretch is keyed, its provisional characters are deleted (a correction
  if the branch is selected), the amplitude stays unknown, and the count
  restarts at once.
- **Fresh fit against the previous.** The penalty ½ · k · ln n nats, k = 4
  (the fitted T, w, q, T_g), n the number of the over's (re-keyed or later)
  marks and spaces: 4.16 nats at n = 8, 4.97 at 12 (BIC, n independent
  observations). After the re-key the rival keeps learning from every later
  mark and space and is tested again at each; the competition ends when the
  rival has seen the fit memory's retained length, ⌈4 · 48⌉ = 192 marks and
  spaces (the previous over's memory then weighs e⁻⁴ = 1.8%).
- **Over starts.** Recorded (s, on the selected branch's time base) once
  its re-key keyed at least one mark and the branch is the selected one; a
  silence followed only by noise records none.
- **Switching.** The new branch's characters replace the published ones
  from the start of its character that contains (or follows) the time at
  which its current eligible run began, on its own time base.
- **Published text and corrections.** New characters of the selected
  branch are appended when they start after the last published one. A
  replacement cuts at c = max(f, t − 20 s) and works on overlap, not on
  start times alone: a published character is kept if it starts before
  t − 20 s or ends before c, the others are replaced; a new character is
  taken only if it starts at or after both c and the end of the last kept
  character (different branches time one character a few ms apart, and
  cutting on start times published one character twice). A correction is
  recorded only if the replaced text differs: (t, the first replaced
  character's start or c, the reach t − that start ≤ 20 s, old text, new
  text, reason), the number of characters kept (`from_index`, the
  prototype's len(kept)) and the first position whose character's text
  changed (`first_changed_index`). The engine's index-based correction
  starts at the smaller of the two: the kept characters are not always a
  prefix of the list.
- **End of stream.** `finish` ends every branch's open character (no word
  space); a stream cut mid-character publishes the elements completed so
  far as a character (C, −·−·, cut during its third element, reads N, −·),
  and no correction refers past the end.
- **Exact zeros.** While the noise estimate has no first estimate (only
  exact zeros so far) a block is not keyed, nothing enters the periodicity
  or the branches, nothing is published, and every branch's clocks (over
  start, start of the unknown amplitude and of the stretch, re-key
  time-out) restart at the
  block's end: the first block with other input finds the channel as a
  stream's first block does (its noise estimate taken from that block's
  non-zero samples only). A stream that starts with 1 s of exact zeros and
  then a station therefore decodes as the station alone: the same text and
  characters, times shifted by 1 s (the block grid moves by 4 samples
  relative to the station, 1500 samples being 46.875 blocks).
- **Memory (derived from the arrays the code allocates, not measured).**
  The channel keeps a window of u and of |v_k|² (FS²) back to the earliest
  sample a later block can read: the re-key's 20 s plus the noise
  estimates' look-back (3 N_max, a segment with its mask's reach, the
  warm-up) and 2 blocks, 31 642 samples (21.1 s) at 1500 samples/s (31 688
  with stage 1's 20 ms guard margin); its storage holds up to 2 s more
  (34 675 samples) and is moved forward when full (a bound chosen for the
  port, not a tuned value). The |v_k|² values are stored as 4-byte floats,
  which is lossless (they are float32-rounded when computed); every read
  converts them to double exactly (Plan B, B1). Per channel at
  1500 samples/s: the |v_k|² window 32 × 34 675 × 4 B = 4.4 MB; the u
  window 34 675 × 16 B = 0.55 MB; the cumulative-sum ring 277 × 16 B =
  4.4 kB; per duration fit its two tables (3636 + 6060) × 8 B = 77.6 kB
  and its retained history (≤ 192 marks and spaces, 24 B each, 4.6 kB),
  82.2 kB. Each branch holds one to three fits (the decoding fit, the
  previous over's, the rival), so the fits take 32 × 82.2 kB = 2.6 MB to
  96 × 82.2 kB = 7.9 MB, and a channel 7.6 MB to 12.9 MB in all, plus small
  per-character and per-record lists. The periodicity estimate (its
  longest window, 10 s) keeps its buffer of averaged p,
  7500 × 8 B = 60 kB, and about 0.6 MB of FFT work arrays during a
  recomputation; so a channel 8.3 MB to 13.6 MB in all. The noise estimate keeps its own last 480 |v_k|² of non-zero
  input per three-tap branch for the warm-up and the recovery: 1 × 480 ×
  8 B = 3.8 kB with the default "spectrum" method (branch 1 only), 32 ×
  480 × 8 B = 123 kB with "branch"; the window's look-back still counts the
  warm-up, which the estimate no longer reads from the window (kept: a
  bound, and the stated sizes stay). The fit's grid constants (ln μ, 1/μ²,
  a validity flag per class and grid point: 432.7 kB with the defaults) are
  immutable and held once per process per configuration: every fit built
  from a configuration with the same values of the fields they read
  (`min_wpm`, `max_wpm`, `t_grid_step`, the q, w and T_g grids,
  `outlier_prior`, `outlier_range_s`, `sigma_ln_mark`, `sigma_ln_space`,
  `fit_memory`, `prior_sigma_ln`, `refine_iterations`; compared bit for
  bit) shares one copy, which is freed when no fit uses it. Sample indices
  are 64-bit throughout (the noise estimates included: tested with indices
  past 2³¹, as after 16.6 days at 1500 samples/s).

**Provenance.** block_s heuristic (the engine's channel block); T_new's
0.5 s and 12 T_g placeholders kept by E7 (stage-1 record, section 3.10);
the re-key wait 0.8 s measured (E9b) and time-out 2 s heuristic
(stage 1's; the default again since B4g, owner, 2026-10-06, option a′);
the fresh fit's 8 marks and spaces a placeholder (heuristic); the ½ k ln n
penalty's form derived (BIC, n independent observations), its use with k
counting only the fresh fit's parameters heuristic; the end of the
competition at 4 N_mem derived from the memory's weight; the 20 s
correction reach an owner decision; the overlap cut and the choice of the
re-key's candidate amplitudes heuristic; the window kept in memory a bound
of the port, not a parameter. Memory and CPU measurements (B1, B4a, the
cost of each step): Plan B record, sections 2, 3.4, 4.4, 6.3 and 9.6.

**Limitations.** The measured memory per channel after B1 lies 1.7 MB above
the derived upper end; the cause is conjectured, not traced (Plan B record,
section 9.6). With stage 1's clocks the time-out counts,
and the re-keyed stretch reaches back, from where the amplitude became
unknown, so the re-key can key the noise before a weak station (the loss
stated under "Keying time constants" above).

### The bank decoder behind the engine

`BankDecoder`, one `BankChannel` per channel, selected by `FrontEnd::Bank`
(a code identifier; `kz4ap-bench --decoder bank`).

**Derivations.**

- **Channels.** The engine opens and closes channels exactly as on the
  Matched path: the detector with distance attribution (each track follows
  its own peak within D_ch = 47 Hz), the channelizer's ±150 Hz channels at
  r = 1500 samples/s. Before every channel block (32 samples, 21.33 ms) the
  engine gives the decoder its anchor Δ = f_det − f_c, Hz (the channel's
  center is its FFT bin's center). In oracle mode f_det is the label's
  frequency, fixed for the whole recording: the label's drift is not
  followed.
- **Anchor mixing (order of operations).** Each block y (channelizer
  output, FS, single precision, widened to double) is mixed by the anchor
  in force for it; S[n] = Σ_{m ≤ n} Δ[m] (Hz, summed in order from the
  channel's first sample, in double precision), so φ[0] = 0 and the phase
  advances 2π Δ / r per sample, continuous across blocks and across anchor
  changes. This is the prototype's `streams.anchored_baseband` with the
  same order of operations (the cumulative sum, minus the sample's own Δ,
  times 2π, divided by r; cos(−φ) and sin(−φ); the complex product written
  out as numpy computes it), so the engine saw the same u as the prototype
  run on recorded detector channels, up to libm's last-bit rounding of cos
  and sin.
- **The residual through a branch.** The bank has no frequency tracker: the
  residual (the station's frequency minus f_det) stays in u, where the
  branch boxcars attenuate it by |H(f)|². For a boxcar of 58 samples
  (38.7 ms, 0.8 dit at 25 words/min; the ladder's neighbors are 36.4 and
  40.1 ms) that is −0.54 dB relative to 0 Hz at a 5 Hz residual, −2.25 dB
  at 10 Hz, −3.33 dB at 12 Hz and −11.4 dB at 20 Hz, with the first null at
  r / N = 25.9 Hz (from the formula; the decoding effect is not measured
  here). With the label's drift not followed in oracle mode, the residual
  grows by 1 or 2 Hz per second of the signal at 1 or 2 Hz/s and passes the
  25.9 Hz null of a 25 words/min branch after 25.9 s or 12.9 s.
- **Blocks.** The decoder pushes a call's samples to the bank one bank
  block at a time (the engine's 32-sample channel blocks are normally one
  bank block each; a call with more samples is cut at the bank's block
  boundaries). A bank block's last step publishes the selected branch's new
  characters, so after each block the characters it appended are the last
  ones of the bank's list (`Output::appended()` counts them).
- **Events.** Each call returns a `DecodeUpdate` (and the engine a
  `DecodedTextEvent`, published when it has characters or corrections):
  `chars`, the characters appended since the last call, in order; and
  `corrections`, one `TextCorrection` per correction the bank recorded
  since then, in order: `from_index` (the smaller of the number of the
  channel's characters the bank kept and the first position whose
  character's text the correction changed, clipped to the list's current
  length), `chars` (the bank's characters from `from_index` on, as they
  stand at the end of the call, kept ones included), `t_s` (when it was
  made, s), `reason` ("switch", "rekey" or "timeout") and `reach_s` (the
  bank's reach: `t_s` minus the start of the first replaced character, s,
  at most 20 s). A "resync" correction is the exception: its `t_s` is the
  end of the call's processed blocks, and its `reach_s` is `t_s` minus the
  start of the first character at which the consumer's list differs from
  the bank's (0 when only the list's length differs). That reach is not
  bounded by the 20 s correction reach: the character can be a kept one
  that a same-text replacement moved, and a kept character is one that
  starts more than 20 s before the replacement or ends before its cut, so
  nothing in the code bounds how far back it starts. All times are stream
  times: the bank's times (counted from the channel's first sample, the
  branch's group delay removed) plus the first block's start time, s. The
  event's frequency is the anchor (center plus Δ: the detector's
  frequency, or the label in oracle mode). The bank publishes no speed or
  confidence (both 0 in its events) and no per-character probability (1).
  Envelope's and Matched's updates always carry no corrections.
- **The consumer's rule, and its proof.** A consumer keeps one character
  list per channel: it appends an event's `chars`, then applies its
  corrections in order, each keeping the first min(`from_index`, length)
  characters and appending its `chars`. The list's text then equals the
  bank's after every call. The bank's list changes only by appends and by
  replacements, and a replacement that changes the list's text either
  records a correction or, when the replaced and the new text are equal but
  overlapping characters change places, does not. *Changes with a
  correction:* let g be the smallest `from_index` of the call's
  corrections; no character's text below g changed during the call
  (`from_index` is never past the first position whose text changed), and
  any position between the consumer's previous length and g was filled by
  an append, in order, so after the appends the consumer's first g
  characters are the bank's; the correction at g replaces everything after
  them by the bank's tail, the ones before it only touched positions at or
  after their own index (≥ g), and the ones after it (index ≥ g) put back
  the bank's tail again. *Changes without a correction:* the `BankDecoder`
  applies every update to a copy of the consumer's list exactly as the
  rule above does, and at the end of the call compares it with the bank's
  list from the lowest position that any of the call's text changes
  touched (`Output::replace_from` records every change of the list's text
  in `text_changes()`, with a correction or without one); if they differ
  there or later (or in length), it sends a "resync" correction from the
  first differing position with the bank's characters from there on, after
  which the copy, and so the consumer's list, is the bank's. Below that
  position no replacement of the call changed a text, so the argument of
  the first case holds there. The *final text* is that list's text, every
  correction applied; the *immediate text* is every appended character in
  order, corrections ignored (what a reader would have seen live). A
  replacement whose text is unchanged is not a correction (the prototype
  records none) but can re-time characters; the consumer keeps the times
  first published, so its texts are exact and its character times can
  differ from the bank's final ones by that re-timing (up to 5.0 ms on the
  speed-turnover golden stream, measured).
- **Why `from_index` is a minimum.** `Output::replace_from` keeps every
  character that starts more than 20 s before the correction or ends before
  the cut, and the prototype's `from_index` is the number kept. When two
  characters overlap in time (copies of one character timed a few ms apart
  by two branches), a kept one can follow a replaced one in the list, so
  the kept characters are not a prefix, and a consumer keeping the first
  `from_index` would keep the replaced character and drop the kept one.
  Hence the event's `from_index` = min(`from_index`,
  `first_changed_index`), its `chars` the bank's characters from there on,
  kept ones included, and the resync check. (The defect this fixed, found
  by the first full-suite run: Plan A record, sections 4.4 and 6.2.)

**Provenance.** The anchor mixing is the prototype's (`anchored_baseband`,
derived: the detector's frequency is where option 1 says the station is);
the oracle anchor without drift is Plan A's choice, not tuned. The event
format, the consumer's rule and the immediate text are this port's
interface choices (owner decision D1 for `--decoder`); probability 1,
speed 0 and confidence 0 are placeholders for values the bank does not
produce. A resync is measured never to occur on the suite (0 of 31 205
corrections; Plan A record, section 4.5). Cost through the engine and the
bank against Matched and Envelope: Plan A record, sections 3, 4 and 6.5;
the current cost after Plan B: Plan B record. Bench outputs for the bank:
A.11, "Bank corrections in the bench".

**Limitations.** No frequency tracker: in oracle mode the label's drift is
not followed, so the suite marks a bank row of an oracle recording "not
meaningful (oracle anchor)" when some label's sound gets more than 12 Hz
from its labeled frequency (for the bank a heuristic limit: a 58-sample
branch, 0.8 dit at 25 words/min, is −3.33 dB relative to 0 Hz at 12 Hz).

## A.8d The story model and the viterbi decoder

### Exact form

The viterbi decoder of section 8d as the code implements it (`engine/src/viterbi_decoder.cpp`, `engine/src/viterbi/`,
`engine/src/engine.cpp`). The headers (`engine/include/kz4ap/viterbi_decoder.hpp` and `engine/include/kz4ap/viterbi/`:
`config.hpp`, `front_filter.hpp`, `mark_template.hpp`, `observation.hpp`, `systematic.hpp`, `amplitude.hpp`,
`long_run.hpp`, `newborn.hpp`, `fist.hpp`, `morse_tree.hpp`, `symbol_prior.hpp`, `search.hpp`, `hiss.hpp`) keep the full
design records, with each measurement at its source and the test that holds it. The first prototype, which summed
2 ms pieces into coherent blocks with a common C² in its systematic term and a flat prior read from tables
(`docs/plans/2026-10-20-new-decoder-prototype-1.md`), is gone from the code; this document described it until commit
d78d45f. The older plans and design notes stay as history; where they and this section differ, this section describes
the code.

**In the engine** (`FrontEnd::Viterbi`). At construction the engine builds:
- one `HissEstimator` for the span (`EngineConfig::viterbi`'s `hiss_*` fields), which reads every spectrum frame before
  the frame is published, in oracle mode too, where the detector does not run;
- one set of shared parts (`make_viterbi_shared`, `ViterbiShared`), all from the channelizer's own taps: the front
  filter's taps, its output noise (σ_y²/N₀, ρ_m, w), the template's tables, the default Ω̄ table and the newborn's
  tables;
- per channel (`open_channel`), a `ViterbiDecoder` with `EngineConfig::viterbi`, or, for oracle channel i when
  `EngineConfig::viterbi_genie` is not empty, `with_genie(viterbi, viterbi_genie[i])` (the true fading spread and fist;
  the carrier is no genie value), and a `ChannelFollower` (A.7, "Following the detector").

Before every channel block (32 samples) the engine shifts the block to the detector's frequency and re-bins the channel
from the next block on (A.7), calls `set_noise_density_fs2_per_hz(N₀)`, N₀ being the hiss estimator's density at the bin
c the block was cut at (the frame's bin c + N/2), then `process(block, t₀)`, t₀ = (the block's first sample index) / r.
It sets no anchor, and the decoder reports no frequency (the track's is f_det).

**In the decoder.** The decoder reads its channel as received: no frequency tracker, no oscillator (owner, 2026-10-23,
plan D17). `EnvelopeSampler` keeps the filter's history across blocks. Each envelope sample k (channel sample of absolute
index 4k, t_k = 4k/r, one expression, never a running sum) goes to `Search::step(e_k, t_k, N₀)` if N₀ > 0, and is
skipped otherwise. So if N₀ falls to 0 and returns mid-stream the search sees the stream spliced (tested,
`ViterbiDecoder.ANoiseDensityOfZeroMidStreamSplicesTheStream`). The engine holds N₀ through frames of exact zeros (the
hiss estimator, below). A block must start within half a sample of where the previous one ended; the output does not
depend on how the stream is split into blocks. `process` returns nothing; `flush` returns the best story's whole text
once, each character's probability 1, speed and confidence 0 (placeholders). The decoder builds the evidence's tables and
the fist's table when it is made, so that no block stalls on them. The channel rate must make Δ a whole number of channel
samples: 4 at r = 1 500 samples/s; the shared parts refuse it otherwise, as at 44.1 and 88.2 kHz (r = 1 378.125 samples/s).

**The hiss estimator** (N the FFT size, frames every N/2 samples, Δf = fs/N):
1. reading to power: p = 10^(P/10) FS² per bin; a reading P ≤ −199.99 dBFS is an exact zero, p = 0;
2. a frame with p = 0 in every bin is not counted; the n-th counted frame moves every bin's average by a_n = max(α, 1/n),
   α = 1 − e^(−(N/2)/(fs·τ)), τ = 1 s (α = 0.021107 at the engine's hop of 21.33 ms: a plain mean through n = 47);
3. of the bins within W = ⌊2 000 Hz / Δf⌋ = 85 of the center (clipped at the span's edges), the one of rank
   ⌈0.25 × their count⌉ from the smallest: the 43rd of 171;
4. the bias: A_n = (1 − a_n)²A_(n−1) + a_n², C_n = (1 − a_n)²C_(n−1) + (1 − a_n)a_n a_(n−1), v_n = A_n + 2C_n/36,
   k = 1/v_n, β = Q(k, 0.25)/k, Q the Gamma distribution's quantile (shape k, scale 1);
5. N₀ = (the selected average / β) / (1.5 Δf); 0 before a frame is counted and where the selected average is 0.

**The front filter** (`front_filter_taps`; `front_filter_taps_any_width` is the same taps at any T, for segmental's scales, without the check that viterbi's fixed constants need: at 1 500 samples/s, 25 taps). p(t) = c(t)(1 − c(t − T)), c(x) = ½ − ½cos(π clip(x/E, 0, 1)), T = 12 ms,
E = 2.5 ms / 0.590334 = 4.2349 ms (0.590334 = (π − 2 acos 0.8)/π, a raised cosine's 10–90% time over its length);
h[j] = p(t_c + j/r) / Σp, j = −12 … 12, t_c = (T + E)/2 = 8.12 ms. y[n] = Σ_(i=0…24) h[i−12] x[n−i], read at every channel
sample whose absolute index is a multiple of 4; e_k = |y|. Zero history before the first sample.

**The output noise** (`output_noise`). R_c[d] = N₀ fs Σ_n g[n] g[n + dD], R_y[l] = Σ_a Σ_b h[a]h[b] R_c[|l + a − b|],
σ_y² = R_y[0], ρ_m = R_y[4m]/R_y[0], m = 0 … 6. On the default chain σ_y²/N₀ = 74.50 Hz, ρ₁…₆ = 0.8776, 0.6256, 0.3666,
0.1380, 0.0138, −0.0052, w_strong = 1/(1 + 2Σρ_m) = 0.1987; the correlation beyond lag 6 is taken as 0.

**The template** (`make_template_tables`). S(τ), the response of both filters to a raised-cosine key-down of 10–90% rise
2.5 ms; s_j the mean of S(jΔ + u) over the offset u from the keyed edge to the claimed sample, on the input-rate grid of
[0, Δ); j = 0 at the first sample at or after the keyed edge. s_0 … s_11 = 0.00000, 0.00001, 0.00016, −0.00064, −0.00080
(the channel filter's precursor), 0.0505, 0.2126, 0.4334, 0.6590, 0.8661, 0.9825, 1.0023; within 10⁻⁴ of 1 after, exactly
1 from j = 16. Half amplitude 20.78 ms after key-down. v_j = mean over rise times r and u of (S_r(jΔ + u) − s_j)², 79 rise
times uniform on 0.8–4.7 ms, each aligned at its half-amplitude point; c[a][b] = mean over r of (m(a, r) − s_a)(m(b, r) −
s_b), m(j, r) the mean over u of S_r(jΔ + u). A story's template is the superposition of its edges, s = (key down before
them) + Σ_e σ_e s_(age_e), σ_e = ±1, over its claimed edges of the last 16 samples (`EdgeHistory`), and v = Σ_e v_(age_e) +
2Σ_(e<e′) σ_e σ_e′ c[age_e][age_e′]. It assumes the carrier's phase continuous across a key-up, which matters only for a
gap shorter than the response, above about 67 WPM.

**The score** (`story_evidence`, `sample_evidence`). V = σ_y² + (μ′² + P′)(f²s² + v), fixed before the sample and never
divided by w, f = `systematic_floor` = 0.02. score = ln ∫₀^∞ exp(wΛ(a)) N(a; μ′, P′) da − ln Φ(μ′/√P′), the prior truncated
to a ≥ 0, Λ against the reference p₀(e | σ_y²) shared by every story: score = [the integral with Λ₀ = −b² + ln I₀(2bz′), b =
|s|a/√V, z′ = e/√V] + w[e²/σ_y² − e²/V − ln(V/σ_y²)] (derived). The integral: adaptive Gauss–Hermite of order 5 about the
integrand's mode (Newton's method from the previous sample's mode, bisection as a safeguard), nodes of the Gaussian
truncated at 0 where b̂ < 8.5σ̂, and exact tables J_w(y, κ′) where the prior is broad (y ≤ 4, κ′ ≤ 2; 5 nodes of w). s = 0
scores exactly 0. ln I₀ by Abramowitz and Stegun 9.8.1 and 9.8.2, in the log.

**The amplitude** (`predict_amplitude_at_level`, `FadingTables`). From (μ, P, t_m), lag ℓ = k − t_m samples: ρ =
exp(−π²f_D²(ℓΔ)²/2); ρ_a = (π/4)(₂F₁(−½, −½; 1; ρ²) − 1)/(1 − π/4); ā = 0.886√Ω̄; μ′ = ρ_a μ + (1 − ρ_a)ā; P′ = ρ_a²P +
½(1 − ρ²)Ω̄, the second term times m = 1 + (M − 1)x/(1 + x), x = Ω̄/(X₀σ_y²), at ℓ = 1 only. f_D = `genie_fading_spread_hz`
where set, else `fading_spread_hz` = 2 Hz. The posterior's mean and variance become (μ, P), t_m = k, and its mode Newton's
next start, only where |s| ≥ `amplitude_update_min_template` = 0.01; below it the sample is scored and the lag runs on.

**The long-run power** (`LongRun`, `OmegaBarTable`). Ω̂ = Σs_k²(e_k² − σ_y²)/Σs_k⁴ over the samples with s ≠ 0 since the
sums restarted (compensated sums). ν = (ΣW_i)²/Σ_iΣ_j W_iW_j e^(−π²f_D²(t_i−t_j)²), W_i = Σs⁴ over bin i of 16 samples
(42.67 ms) on the channel's common grid, cross terms over the 10 bins before (426.7 ms). Ω̄ = the mean of the inverse gamma
of shape ν and scale νΩ̂₊, truncated to −15 … +85 dB S₅₀₀, read from a compiled table (97 × 534 nodes, 0.42 MB, within
0.055% of the closed form on its test points; other bounds load a generated table).

**The newborn** (`NewbornIntegral`). A newborn from silence holds log beliefs F_i on 128 points even in u = ln a over its
prior, flat from −15 to +85 dB S₅₀₀ (bounds in FS fixed from N₀ at its birth), h = 0.0906 nepers; its score at k is ln Z_k −
ln Z_(k−1), Z the trapezoid sum. Each scored sample adds w l(a_i) with V(a_i) = σ_y² + a_i²(f²s² + v), by table reads
(softplus, G(ln x) = ln I₀(x) − x + ½ ln x). The live range [lo, hi] only shrinks: an end point more than 160 nats below the
largest is dropped. Every 8 scored samples (21.33 ms) a three-point diffusion step of ε = 8 × ½(1 − ρ(Δ)²)/(2h²) = 0.0684
at 2 Hz, in ⌈ε/¼⌉ substeps, reflecting at the grid's ends. It hands over to N(μ, P), μ = e^(û + σ_u²/2), P =
μ²(e^(σ_u²) − 1), when σ_u < 0.1 nepers or at the 15th sample after its first mark's claimed key-up. A rescue is no newborn
of this kind: it opens the speed only, and keeps its parent's edges, amplitude and long-run sums.

**Timing** (`Fist`). Speed s: dot T_s = 12.0 ms × 1.1^s, s = 0 … 31; standard spacing. A kind's length is D = shift + M·e^(σN)
samples, N standard normal, M = median_dots × T_s / Δ, shift 0 at the defaults; rounded to the nearest sample, at least
one: P(1) = F(3/2), P(τ) = F(τ + ½) − F(τ − ½), S(1) = 1, S(τ) = 1 − F(τ − ½), F the distribution function of D; in the
log, from a table of ln Φ_c (z from −8 to 40 in steps of 0.001, linear interpolation; direct outside it).

**Text** (`MorseTree`, `on_air_log_prior`). The tree of the 62 patterns of `kz4ap::morse` (26 letters, 10 digits, 14
punctuation marks, 12 prosigns); a mark that leaves the tree is not created (eight dots is `<HH>`; nine leave the tree). A
letter costs ln(1/62) at its start and ln p + ln 62 at its end, so ln(1/p) in all, with

  p(symbol) = max(count/2 688, 0.002) / Σ (the same over the tree's 62 symbols) = max(…) / 1.042.

The counts are VE3NEA's (`VE3NEA_CHAR_WEIGHTS`, checked against the generator's copy by `training/tests/test_messages.py`).
The 21 symbols the table lacks, `<AR>`, `<KN>`, `<SK>` among them, stand at the floor: 6.256 nats, 2.129 above flat. The
table's rarest, J and 6, count 8, cost 5.858 nats, untouched by the floor. `symbol_prior_on_air` = false pays the flat 1/62:
ln p + ln 62 is then 0. After a mark at node v: an element gap where v has children (0 nats); where v holds a symbol, a
letter gap (−ln(1 − p_w) = 0.396 nats), a word gap (−ln(p_w(1 − q)) = 1.161 nats) and the over's end (−ln(p_w q) = 4.296
nats, taken at the mark's end as a step to no signal), p_w = 1/3.06, q = 1/24, each with the letter's end. A word space
joins the text at a word gap, and before a new over's first letter when the text is not empty.

**The search,** per sample as it arrives (`search.hpp` has every rule):
1. at the channel's first sample, one story: no signal, score 0, no text;
2. every story takes its template from its edge history after this sample's claim, and, if s ≠ 0, scores the sample by its
   integral or amplitude and adds it to its long-run sums. Every phase scores so: a mark; a gap within 16 samples of its
   key-up; the no-signal story within 16 samples of an over's end (its clock, 1 … 17, keeps stories with different key-ups
   apart until their templates agree). A mark also ends as a dot and a dash where the tree continues, each a gap at the
   child node, and, where the child holds a symbol, an over's end into no signal; a gap also ends as each kind its node
   allows, each a mark at the next sample (a letter or word gap pays the letter's end, the next letter's start and ln(1 − ε)
   for not restarting); no signal pays ln(1 − P), P = 0.05 s⁻¹ × Δ = 1.333 × 10⁻⁴. Each successor claims its edge at the next
   sample;
3. births: from the continuing no-signal story, a newborn at ln P + ln(1/62) (−8.923 − 4.127 nats) with a new integral and
   empty sums; where a letter starts, one rescue from the best letter-starting successor, created or not, at ln ε (−6.908
   nats) instead of ln(1 − ε), its text and level kept, its speed open. A newborn's first mark keeps the best over the
   speeds and labels of ln(1/32) + ln S and ends into every speed and label;
4. successors created at one sample merge on the key, the better kept, of equal scores the earlier created. The key is the
   state (activity, key, τ, speed or open, node) plus the claimed edges of the last `merge_edge_history_samples` = 12
   samples, 0 merging on the state alone. A rescue and a newborn from silence each have their own key (`kRescueKeyBit`), so
   two live stories may share a state. At most one quiet no-signal story;
5. those more than 40 nats behind the best are dropped, then all but the best 1 700 (ties: the key, then creation order); a
   successor already beyond the beam is not created, except a birth (exact: the beam would drop it);
6. scores are held relative to the best.

**Variants that are not the default** (library code, no default path runs them): Bell's decay (`predict_amplitude_bell`), the
noise in Ω̄'s likelihood (`NoiseVarianceSums`, `OmegaBarVariantTable`); `--genie fist|fading` gives an oracle channel the
truth; `symbol_prior_on_air` off.

### The hiss estimator (viterbi and segmental)

**Derivations.**
- **Its average** is the detector's (section 6), with α = 1 − e^(−hop/τ) per frame.
- **The bias.** A bin of the Hann-windowed transform of white noise is a complex Gaussian, so its frame power is
  exponential. The periodic Hann window at a hop of N/2 gives successive frames' amplitudes the correlation
  Σw[m]w[m + N/2]/Σw² = (N/16)/(3N/8) = 1/6, and their powers 1/36; frames two hops apart share no samples. The average's
  relative variance is Σw_i² + 2·(1/36)·Σw_i w_(i+1) (the recursion above). Taking the average as Gamma distributed with
  that variance (an approximation, ours), the 25th percentile reads low by β: ln(4/3) = 0.2877 after one frame (an
  exponential), 0.7667 after 10, 0.8953 after 47, 0.92658 in steady state (k = 88.92). Checked by simulation on real
  analyzer frames (20 000 frames of white noise, every disjoint window of 171 bins): the 43rd smallest over the mean bin
  exceeds β by +0.96% after 1 frame, +0.23% after 5, +0.03% after 10, −0.06% after 47 and −0.07% in steady state
  (measured; `Hiss.BiasMatchesSimulation`); at one frame the order statistic of 171 bins sits +0.34% above the population
  quantile and the neighboring bins' correlation (4/9 in power) adds about +0.6%.
- **Per hertz.** One bin of the Hann window has the equivalent noise bandwidth N·Σw²/(Σw)² = 1.5 bins = 35.16 Hz at the
  engine's bins (section 4).
- **Receiver audio** (`HissEstimator::set_band`, A.1): the neighborhood is also clipped to the passband's bins, so that the
  near-silence outside a receiver's filter (below 300 Hz, above 3 kHz and every negative frequency) is not counted; a channel
  whose neighborhood lies wholly outside it reads the passband's bins. On noise within 300–3000 Hz at 12 kHz the test requires
  the estimate at 700 Hz within 8% of 4σ²/f_a with the clip and below a tenth of it without (`RecordingReaderAudio.TheHissInThePassbandIsTheReceiversNoise`).
- **Exact zeros** are missing data, as the bank treats them (A.8c): a receiver's output always carries noise, so a frame
  of exact zeros is padding or a dead channel. The −200 dBFS of an exact zero follows from the analyzer's 10·log₁₀(power +
  10⁻²⁰).

**Provenance.** τ = 1 s: heuristic (the detector's). ±2 000 Hz and the 25th percentile: placeholders, heuristic (design
notes, topic 3, item 3). The hop: the engine's. The 0.01 dB margin above −200 dBFS: heuristic (it need only exceed the
float rounding of that reading and stay far below any recording's noise per bin, about −130 dBFS for 16-bit samples at
2 048 bins). Accuracy on the generator's noise (σ = 0.02 FS; 8 realizations of 100 channels; `Hiss.MeasuresTheGeneratorsNoise`):
the mean estimate within 0.25% of σ²/fs at every second from 1 to 6 s, at 48 and 192 kHz; the rms deviation 2.0% and
2.1% at 1 s and 1.3% to 1.5% from 2 s on (measured).

**Limitations.**
- **Stations in the neighborhood lift it:** a keyed carrier at +30 dB S₅₀₀ lifts the bins out to about ±280 Hz, and
  four such stations within ±2 kHz, one per kHz, make the estimate read 7.2% high on average over 8 noise realizations
  (4.7% to 11.2%; the plan asked at most 5%; `Hiss.ToleratesNeighbors`, a strict expected failure); one station adds
  0.6% and two 2.5% (measured). It depends on the density of stations, the share of the bins they lift, not on the
  neighborhood's width (owner, 2026-10-20); the rank is the lever, left at its placeholder until real recordings set it.
- A floor that is not flat across the neighborhood reads at its lower quartile, not its mean; a burst of interference
  lifts every bin for about τ.
- It is noisy at first: one estimate's relative standard deviation is 17.7% after 1 frame, 6.7% after 5, 4.6% after 10
  and 2.0% after 47 (scratch measurement on real frames, not in a test).
- The rank r of c bins sits on average at r/(c + 1) of their distribution, the 25% that β assumes only when c leaves 3
  over a multiple of 4, as 171 does (clipped neighborhoods at the span's edges, and 185 bins at 44.1 kHz or 129 at 32 kHz,
  do not): at most 0.19% in steady state for 86 to 171 bins (scratch), not corrected.
- After a run of exact zeros, the first counted frame can straddle the data's start and read part of a window's power;
  the plain mean carries it with weight 1/n, so the estimate reads low for the first fraction of a second.

### Model and amplitude (viterbi decoder)

**Derivations.**
- **Λ** is the log-likelihood ratio of a carrier of known amplitude and unknown phase against noise alone (Proakis and
  Salehi, §4.5-1), with the carrier's systematic variance in V: the full density p(e | a, V) is scored against the reference
  p₀(e | σ_y²) every story shares, not against noise at V (D23). ln I₀ is within 4.8 × 10⁻⁷ nats.
- **w** is the composite likelihood's magnitude adjustment for a statistic linear in the noise: correlated samples at
  ρ_m count as w of a sample each. The limit for a statistic quadratic in the noise, 1/(1 + 2Σρ_m²) = 0.2754, is computed
  and not read; one constant serves every story (D23).
- **The prediction's m.** n predictions at lag 1 add nQ, Q = ½(1 − ρ(Δ)²)Ω̄, where the true variance over n lags grows as
  n²Q (Gaussian correlation near lag 0; derived; 15.4 times too little at n = 16). A fixed multiplier costs at low level on
  steady signals (measured), so m grows with Ω̄/σ_y².
- **The update's threshold.** s₀ … s₄ are at most 8.3 × 10⁻⁴ in size and the fall's tail is as small from j = 11, so such a
  sample carries no information on a; each such update reset the lag t_m.
- **The on-air prior's reach.** A flat 1/62 makes two letters cost ln 62 + 0.396 = 4.52 nats more than one merged symbol,
  so a gap reads as a letter gap only above 2.51 dots (derived), and a fifth of a paddle's letter gaps fall below that
  (measured; `symbol_prior.hpp`).
- **The merge key's edges.** A one-sample shift of an edge has 0.0% of its evidence at an age of 4 samples, 69% at 8 and
  99.8% at 10 (derived): merging on the state alone chooses by the fist's prior before the evidence between two edges exists.
- **The misfit cost:** a length k times its median costs (ln k)²/(2σ²) = (ln k)²/0.18 nats more than a perfect one: 2.7 nats
  at 2×, 6.7 at 3×, 10.7 at 4× (derived).

**Provenance.** The channel rate's Δ, the front filter's dot and edges, the rise range and f_D: owner (2026-10-22 and
2026-10-23, D16 and D17). f = 0.02: placeholder. M = 16, X₀ = 42: heuristics, chosen on one review's slices and possibly
fitted to them; above +30 dB S₅₀₀ no constant is right (`docs/decision-log.md`, 2026-10-30). |s| ≥ 0.01: heuristic. The merge
key's 12 samples: heuristic chosen by measurement (the same log). The speeds (12.0 ms × 1.1^s, 32): owner. Standard spacing
only: owner (2026-10-21, D14). The medians 1, 3, 1, 3 and 7 dots: placeholders (the ITU sending standard's ratios, ITU-R
M.1677-1, not a measurement of senders). σ = 0.3 in ln(length): placeholder. No heavy tail until measured on real keying:
owner (D5). The speed's prior 1/32: owner (flat). p_w = 1/3.06: VE3NEA's mean word length in CW Skimmer's decodes; q = 1/24:
the development set's own count; placeholders until recounted on real traffic. The symbol counts: sourced (VE3NEA). The
floor 0.002: a heuristic in the middle of a flat range (0.001 to 0.004 read callsign text alike; the same log). The over rate
0.05 s⁻¹ and ε = 10⁻³: placeholders. The level's range −15 to +85 dB S₅₀₀: placeholder. The newborn's 128 points: heuristic
with a derived bound and a measured check (D22); 160 nats: heuristic, about twice the deepest revival measured on correctly
timed stories, 67.9 nats; 21.33 ms: heuristic chosen by measurement (D24). Beam 40 nats and ceiling 1 700: placeholders.

**Limitations.**
- **Shared with segmental** (A.8e, "Limitations"): close neighbors, impulses in the hiss, the symbol
  prior's agreement with the generator. Worse here than there: Farnsworth spacing, which viterbi does not model (letter gaps at 3 dots of the story's speed; a letter gap stretched F times costs (ln F)²/0.18 nats of misfit, 24.0 nats at F = 8, derived, and reads as a word gap where that costs less; every letter is right, with a word space after each; `Search.FarnsworthTextIsRight` and `ViterbiDecoder.FarnsworthSpacingAtTheSlowestSpeed` are strict expected failures; `docs/backlog.md`, "Farnsworth spacing in the new decoder"); no neighbor rejection, one filter width only; no text until the channel ends; N₀ is the hiss alone, with no
  channel floor, so it fails at a floor raised around the channel or behind a receiver's passband, as section 8e's stage 2 explains (finding F1 of the
  independent review of commit 5aeb081, not fixed in viterbi); f = 0.02 is too small for flutter and ripple at strong levels, so CER rises with the level
  (review F5, not fixed in viterbi).
- **Amplitude above +30 dB S₅₀₀:** under fading no constant m is right (above); a rate-adaptive variance is the next step
  if viterbi is pursued.
- **A sudden large rise after a short pause** can add a letter (the update's threshold; `docs/decision-log.md`, 2026-10-30,
  not explained).
- **A late claim on the newborn's grid:** the live range lowers a story that claims its key-down 2 or more samples late at
  strong levels. Correctly timed claims stay within 8.3 × 10⁻⁴ nats per sample (`newborn.hpp`).
- **Short overs' margins.** A short over can read as one longer symbol with the same marks at a slower speed, its letter
  gaps taken for element gaps. At exact timing (derived; the sender's speed on the grid; not run through the decoder for
  every case), comparing the correct reading's extra prior cost with the merged reading's misfit, at the grid's best k:

  | Over against merged symbol | Merged reading's misfit, nats | Margin, flat 1/62 prior, nats | Margin, on-air prior, nats |
  |---|---|---|---|
  | "TEST" against "-" | [8(ln k)² + 3(ln(3/k))²]/0.18 = 14.64 at k = 1.1³ | +1.07 | +9.73 |
  | "TU" against "X" | [6(ln k)² + (ln(3/k))²]/0.18 = 5.79 at k = 1.1² | +1.27 | +4.26 |
  | "EE" against "I" | fist's own ln P at 25 WPM | −0.06 | +2.80 |
  | "EEE" against "S" | the same | −1.05 | +3.57 |
  | "EEEE" against "H" | the same | −2.07 | +5.36 |

  A positive margin favors the correct reading. The extra prior cost is the letters' −ln p less the merged symbol's −ln p,
  plus 0.396 nats per letter gap: 13.57 and 4.91 nats for "TEST", 4.52 and 1.53 for "TU", 4.52 and 1.66 for "EE", 9.05 and
  4.42 for "EEE", 13.57 and 6.13 for "EEEE" (flat, on-air). The flat margins for the repeated dots came from the fist's own
  ln P with the first prototype's 2 ms rounding and the on-air column adds the change of prior to them, not recomputed at
  2.667 ms; at 25 WPM, between two speeds of the grid, the flat margins for "TEST" and "TU" were 1.00 and 1.16 nats. The
  margins grow under the on-air prior because common letters cost less and the merged symbols more, the rare ones standing
  at or near the floor; a text unlike the table's gains less. `ViterbiDecoder.OneWordOversDecodeExactly` holds "TEST" and "TU".
  Segmental has the same prior.
- `<HH>` is exactly eight dots: nine or more leave the tree.
- **The statistics** (`ViterbiConfig::stats`) grow with the channel's time: for bench recordings.

### Viterbi's amplitude and search, in outline (moved from the body, 2026-10-31)

**Viterbi's amplitude: per story** (f = 0.02, a placeholder). Each story holds
N(μ, P) in FS. Predicted across a lag of ℓ samples, with ρ = exp(−π²f_D²(ℓΔ)²/2),
f_D = 2 Hz (owner), ρ_a the Rayleigh envelope's correlation for ρ (derived) and
Ω̄ the story's long-run power:

  μ′ = ρ_a μ + (1 − ρ_a)·0.886√Ω̄,  P′ = ρ_a²P + ½(1 − ρ²)Ω̄ · m.

m = 1 except at ℓ = 1, where m = 1 + 15x/(1 + x), x = Ω̄/(42σ_y²), halfway at
+8.0 dB S₅₀₀ (heuristic): inside a mark n predictions add nQ where the true
variance grows as n²Q (derived). V is fixed before the sample, with a² → μ′² + P′. Then

  score_k = ln ∫ exp(wΛ) N(a; μ′, P′) da

by Gauss–Hermite quadrature of order 5 at the integrand's mode, and the moment-matched
posterior becomes the amplitude, only where |s| ≥ 0.01 (heuristic: below it a
sample says nothing of a; it is still scored). This is a sequential filter on the
story's own past. A newborn's first mark integrates a level flat in ln a over
−15 to +85 dB S₅₀₀ on 128 points (placeholder range) and hands over to N(μ, P) at
a spread below 0.1 nepers, or 15 samples after its first key-up.

**Viterbi's search.** Every sample, every story scores it and offers its ends.
Successors merge on (activity, key, samples in element, speed, node) plus their
claimed edges of the last 12 samples (chosen by measurement), since an edge
shows only 5 to 10 samples later; a rescue has its own key bit, its level and a
newborn's flat prior being incomparable at birth. Beam 40 nats, ceiling 1 700
stories (placeholders). Its cost is the amplitude integral per story per sample,
which segmental removes.

### The viterbi decoder behind the engine

`ViterbiShared` holds what every decoder of a configuration on one channel filter shares, through `std::shared_ptr`.
`make_viterbi_shared` is its only maker, so every part comes from the channelizer's taps; a decoder refuses shared parts built
for another channel rate, sample interval, front filter, rise, level range or grid. An oracle channel's frequency is the
label's, given as segments (`EngineConfig::oracle_frequency_tracks`, `OracleSegment`: from start_s, frequency_hz +
drift_hz_per_s·(t − drift_origin_s), Hz from the span's center), which the channel follows (A.7): the bench gives a single
station one segment and a QSO label one per transmission at its sender's offset. An oracle channel opens at the stream's
start, so its first block (21.33 ms) comes before the first spectrum frame, N₀ = 0, and it is not searched; a detector
channel opens after frames have been counted, with N₀ set.

**Cost.** The amplitude integral per story per sample, over up to 1 700 stories; figures in `docs/decision-log.md`,
2026-10-30, and the results records. The required bar is at most 40 ms per channel-second, which viterbi does not meet.

## A.8e Segmental decoder

### Exact form

The segmental decoder of section 8e as the code implements it: `engine/src/segmental_decoder.cpp`,
`engine/src/segmental/evidence.cpp`, `engine/src/segmental/search.cpp`; headers `engine/include/kz4ap/segmental_decoder.hpp`
and `engine/include/kz4ap/segmental/` (`config.hpp`, `evidence.hpp`, `fast_math.hpp`, `search.hpp`). It reads the viterbi decoder's fist, tree,
symbol prior and over model from `SegmentalConfig::model`, a `ViterbiConfig` made by `fast_form_model()`, with
`systematic_floor` 0.1 in place of 0.02.

**In the engine** (`FrontEnd::Segmental`). The channel is as for viterbi: a `ChannelFollower`, N₀ from one `HissEstimator`
built from `SegmentalConfig::model`'s `hiss_*` fields, set before each block. `SegmentalShared` holds, per channel filter, a
`SegmentalParts` for the widest filter and one per narrower scale (`narrower`): the front filter's taps, output noise, template tables, `LevelTables` and `ModelTables`. `viterbi_genie` is refused; `--genie` and
`--stats` belong to `--decoder viterbi`; `stats()` reads the widest stream.

**Everything below runs once per stream** (`SegmentalDecoder::Stream`), at that stream's `at_filter_scale` configuration (section "Filter scales" below); the formulas are written at m = 1.

**Per envelope sample** (`SegmentalDecoder::process`). With N₀ = 0 the sample is skipped: nothing is pushed, the stream is
spliced, and `Search::note_sample_time` records the time of the next. Otherwise, in order:
F = `ChannelFloor::sigma2(e_k, N₀ × 74.50 Hz)`, σ² = max(F, the stream's leak floor); on the widest stream
`DopplerEstimator::push(y_k, F)` (a run's end may set every stream's walk); `EchoEstimator::push(e_k², F)` (a new shape to
`LevelEvidence::set_echo`); `LevelEvidence::push(e_k, σ², F)`, `Search::advance()` (text-fast-1: below, "The channel's tail").

**The channel's floor** (`ChannelFloor`). A count of samples runs from the first pushed. Every 4th sample (stride, 10.67 ms)
e² enters a ring of ⌊5 s / 10.67 ms⌋ = 468 values. At every 36th sample (refresh, 96 ms), once ⌊1 s / 10.67 ms⌋ = 93 values are
held, the value of rank ⌊0.10 × held⌋ from the smallest (`nth_element`) times 10^(−0.3) / 0.10536 = 4.757 becomes the floor; so
first at sample 396, 1.056 s. σ² = max(hiss, floor), floor 0 before that. `channel_floor_window_s` = 0 leaves it out.

**The level's tables** (`LevelTables`). levels = 1 + round((84 − (−6)) / 1.5) = 61 (at most 256), S₅₀₀ in dB at level l; r² =
10^(S₅₀₀/10) × 500 Hz / 74.50 Hz. A term at template value s and spread v_m: v = 1 + r²(f′²s² + v_m), held as floats 1/v,
−s²r²/v − ln v, 2|s|r/v, with f′² = f² + `level_grid_in_v` × step²/12, step = 1.5/20 × ln 10 = 0.17269 nepers, step²/12 =
0.002485 nepers². A value is x − wz² = w(−z²/v + (−s²r²/v − ln v) + ln I₀(2|s|r z/v)), z = e/σ. Three term sets: plain (s = 1, v_m
= 0); rise and fall at m = 5 … 10 (s_m and 1 − s_m, v_m); short elements (below). Each set holds D = ⌊60 dB / 1.5 dB⌋ = 40 table levels
below the grid (`level_noise_above_floor_max_db`), for σ² above the floor: table index i is level i − D re σ², and with
`level_noise_bottom` 1 every index below D holds the bottom's term, −6 dB S₅₀₀ re σ² (`level_on_floor` 0: D = 0).

**The level on the floor** (`LevelEvidence::push(e, σ², F)`, text-level-1). The grid's levels are re F, the channel's floor
max(hiss, channel floor). With σ² > F, s = 10 log₁₀(σ²/F) / 1.5 dB, unrounded. If s < D, d = round(s), the nearest whole step
(`lround`, a half away from 0), and σ² is taken as F × 10^(1.5 d / 10), within ±0.75 dB re σ² itself, so up to 0.75 dB below the
leak floor 10^(0.3) L when that sets σ² (Limitations, "σ² rounded to the nearest step"); s in [D − 0.5, D) gives d = D with σ² still
rounded, to F × 10⁶ (60 dB re F). If s ≥ D, d = D and σ² is taken as it is (the grid then re σ² less 60 dB). z = e/σ, and grid
level l is scored with the tables' level l − d (the plain terms in `push`, the rise, fall and short terms by the shift kept per
sample in the ring and the `SampleContext`). So a step in σ² changes the noise each level is scored at and not the levels; the
forward and backward messages run over the same grid. With `level_on_floor` 0 the floor is σ² itself, d = 0, as before.

**The forward message** (`LevelEvidence::push`), α over (key up or down) × level, flat at the start. Per sample: x_k(l) = wz² +
(float held value); top = max(0, max_l x_k(l)); new up = (0.92 up + 0.08 down)·e^(−top), new down = (0.92 down + 0.08 up)·e^(x_k(l)
− top); scaled to sum 1. If the total is not finite and positive, α starts over flat (the guard of review F6). The key's step
comes first, then the sample. At each frame's end, every 4th sample at m = 1 (text-fast-1: frames and blocks, below), `end_frame`,
at a block's end (every 8th sample while the walk is slow), steps the level on each key state (below), and stores entering(t + 1) =
α_up + α_down, by level.

**The level's step** (`step_level`). `walk_passes` = ⌈(4.8² × 21.33 ms) / (2 step²) / 0.33⌉ = 25 passes of (p, 1 − 2p, p), p =
8.2406/25 = 0.3296, the end values reflected; then x_l ← (1 − jump) x_l + jump × mean over l, jump = 1.0667 × 10⁻³. The step
is symmetric, so the backward message takes the same.

**The level-dependent walk** (text-level-1; a variant, off: `level_walk_high_nepers_per_sqrt_s` 0). With W_h =
`level_walk_high_nepers_per_sqrt_s` > 0, `LevelTables` builds an n × n kernel K[to][from], n = 61, in place of the passes
(`walk_kernel`). For the grid's level `from`, at L = −6 + 1.5 × from dB S₅₀₀ re F (re σ² with `level_on_floor` 0): t = clamp((L −
L₁) / (L₂ − L₁), 0, 1), L₁ = `level_walk_rise_from_db_s500` (+20 dB S₅₀₀), L₂ = `level_walk_rise_to_db_s500` (+40 dB S₅₀₀); the
walk W = W₀ + t (W_h − W₀) nepers per √s, W₀ = `level_walk_nepers_per_sqrt_s` (4.8); in grid steps σ_s = W √T_f / 0.17269 nepers,
T_f the stream's frame (21.33 ms at m = 1: 4.06 steps at W₀, 8.46 at 10 nepers per √s). Column `from` holds exp(−½ ((i − from) /
σ_s)²) for every whole i from −4n to 4n, each i folded into 0 … n − 1 by reflection about the ends (i < 0 → −1 − i, i ≥ n → 2n − 1
− i, repeated: the passes' reflection), normalized to sum 1. The step (`LevelEvidence::step_level`) is x′ = J K x forward and x′ =
Kᵀ J x backward, J the jump's (1 − jump) I + jump/n, symmetric, so the backward step is the forward's transpose; K's columns sum to
1 and its rows need not, so the order matters (review of text-level-1: Kᵀ came before J), where the passes' K is symmetric, its rows
summing to 1, and commutes with J. Below L₁ the kernel is a sampled Gaussian of the passes' variance, 16.48 steps², not the passes
themselves: the two differ by at most 1.4 × 10⁻³ in a column whose peak is 0.098 (measured; test
`LevelEvidence.TheLevelDependentWalksKernelIsNormalizedMatchesThePassesAndStepsBackByItsTranspose`). Cost per step n² = 3 721
multiply-adds against the passes' 25 × 3 × 61 = 4 575 (derived), not vectorized alike.

**The backward message and commit** (`LevelEvidence::commit`), written in 21.33 ms frames as while the walk is slow (in the code's
10.67 ms frames twice the counts, and each block's first frame's entering and its end's β; below). When frames ended − next
uncommitted ≥ 16: β starts flat at the last ended frame's end. For t from last down to the next uncommitted: where t is to be
committed (the first 8), P̂_t(l) ∝ entering(t)(l) × (β_up(l) + β_down(l)), normalized; then β passes back through t's samples, last first (seen up = up·e^(−top),
seen down = down·e^(x − top), new up = 0.92 seen up + 0.08 seen down, new down likewise, scaled to sum 1), then the level's step.
So the first frame of a commit reads 15 frames ahead and its last 8, plus its own within-frame position: sample latency 64 to
127 samples. The ring holds 18 frames (36 of 10.67 ms). `finish` ends a partial frame, commits all with the look-ahead there is, and pushes 17
samples of zero evidence.

**Committing a frame** (`commit_frame`). Levels with P̂ ≥ 10⁻⁴ are active, unnormalized (at most 256). They are grouped in
order into at most 8 groups of ⌈count/8⌉ neighbors, each at the rounded probability-weighted mean level and with the group's
total probability. For each sample: L1_k = wz² + top + ln Σ_active P̂(l)e^(x(l) − top), top the largest active x − wz²; 0 with
no active level. For m = 5 … 10, R_k[m − 5] and F_k[m − 5] likewise over the groups with the rise and fall terms. The sample's
z, group levels and probabilities are kept in a ring of 1 024 for short elements.

**The running sums** (`RunningSums`). C[k + 1] = C[k] + L1_k, double, never reduced. rise(i) and fall(j) accumulate from their
windows, whole once sample i + 10 is pushed. Rings of 8 192 samples, 21.8 s against the longest element's 5.35 s.

**Duration tables** (`ModelTables`). lo = max(1, ⌊M·e^(−4σ)⌋), hi = max(lo, ⌈M·e^(4σ)⌉) per kind and fist, ln P(τ) the fist's, not
renormalized over the cut. Longest: a dash at the slowest speed 2.296 s, a word gap 15.27 s (gT_s = 657 ms, the largest 12 ms × 1.1^k ≤ 0.7 s; derived), against the rings' 21.8 s. mark_survival(s, τ) = max(ln S_dot,
ln S_dash), by speed; gap_survival(fist, τ) = max(ln S_element at T_s, ln S_letter at gT_s − 0.396, ln S_word at gT_s − 1.161), by fist; a length past a table reads its last entry. Below, "s" in a gap's or mark's state is the fist.

**One boundary** (`Search::Impl::boundary`), j from 0 as soon as committed ≥ j + 17:
1. **Reach**, once: for d = 6 down to 1, gap_reach_from[d] = max(0, rise(j+d) + C[j+17] − C[j+d+11], gap_reach_from[d+1]) and
   mark_reach_from[d] = max(C[j+17], C[j+d+5] + fall(j+d), mark_reach_from[d+1]).
2. **Open elements**, each of age L = j − start:
   - gap at (s, n): partial = base + gap_survival(s, L) + gap_reach_from[max(1, 6 − L)]. ended = base, plus correction(gap, L, j) if L < 6.
     Where n has children and L is in the element gap's range: offer ended + ln P_element(s, L) to the mark table at (s, n).
     Where n holds a symbol: letter = ended + (ln p + ln 62) − ln 62 + ln(1 − ε); offer letter − 0.396 + ln P_letter(s, L) and letter −
     1.161 + ln P_word(s, L) to the mark table at (s, root).
   - mark at (s, n), base as in section 8e: partial = base + mark_reach_from[max(1, 6 − L)] + mark_survival(s, L). ended = base + C[j+5] +
     fall(j), plus correction(mark, L, j) if L < 6 and L is at least the dot's lo. For each label with a child and L in range: offer
     ended + ln P_label(s, L) to the gap table at (s, child).
   - open mark (a birth's or a rescue's): the same over every speed, with ln(1/32) added; partial uses the best speed's survival + ln(1/32).
3. **No signal:** silent += ln(1 − P), alive unless dropped.
4. **Best** of the partials, the no-signal story + gap_reach_from[1], each gap offer + gap_reach_from[6], each mark offer + rise(j) −
   C[j+11] + mark_reach_from[6]. A tail due now merges with the no-signal story, the better kept. The best over's end is taken among gap
   offers at nodes with a symbol: score = offer + (ln p + ln 62) − 4.296; it enters the best at + gap_reach_from[6]. floor = best − 15.
5. **Open** what is inside the beam: gaps with base = offer; marks with base = offer + rise(j) − C[j+11]; either is passed over if the last one opened in its state (kind, fist, node), within ⌊0.125 T_s / Δ⌋ samples, has a base at least as high, and otherwise becomes that last one; one rescue, from the best
   mark offer at the root: base = offer − ln(1 − ε) + ln ε + rise(j) − C[j+11], open speed, if with ln(1/32) it is inside the beam; one birth
   from the no-signal story, if inside the beam: base = silent + ln P − ln 62 + ln(1 − ε) + rise(j) − C[j+11]. (The ln(1 − ε), 10⁻³ nats, is a difference from
   viterbi's birth, which pays ln P − ln 62.) The over's end goes to the tail due at j + 6, score + 6 ln(1 − P), with its symbol in the text.
6. **Prune:** drop a partial below the floor and an element at its longest length (gap: gap_hi(s); mark: mark_hi(s); open: the
   slowest speed's mark_hi); keep the best 1 500 (ties by index, gaps before marks; a story of a stretched fist, g > 1, ranked 4 nats above its partial score). The no-signal story leaves below the floor. If no story, no live
   element and no waiting tail remain, it restarts at `best` with the text of the best element dropped at this boundary.

**Short elements** (`LevelTables::short_score`, `correction`). For length L = 1 … 5, position q = 0 … L + 5 at sample i + 5 + q, m = 5 + q:
shape = s(m) − s(m − L), s(j) = 0 below 0 and 1 from 16; template |shape| for a mark, |1 − shape| for a gap; v = v(m) + v(m − L) −
2c[m][m − L] where both are below 16, floored at 0. correction(mark) = whole − [rise(i) + C[j+5] − C[i+11] + fall(j)], correction(gap) =
whole − fall(i) − rise(j), i = j − L, cached per boundary by kind and length (ln(1/32) and ln P are the caller's). A mark below the dot's lo has
no offer and takes none.

**Text** (`viterbi::SharedText`). Letters and word spaces in a chain shared between stories, held by reference counts. A birth's
text takes a word space before its letter if not empty. `flush` runs `finish`, then `advance`, then `best_text`: best of the no-signal
story, waiting tails, and each live element (gap: base + gap_survival; mark: base + C[j+11] + mark_survival; open: base + C[j+11] +
ln(1/32)); a winning gap at a node holding a symbol appends that symbol. Probability 1, speed 0, confidence 0: placeholders.
`DecodeUpdate` from `process` is empty.

**Bench.** `--decoder segmental`; `--set` as A.11 ("Bench settings"). Oracle channels follow the label's frequency, as viterbi's.

### What a score is (moved from the body, 2026-11-17)

The prior is a real generative model and the search a MAP
search under it. The likelihood is a stated approximation, and scores are not
probabilities: (1) *Empirical Bayes:* P̂ is a posterior under an auxiliary model
(a key with no timing), not under the search's fist and text. (2) *Data used
twice:* a sample's neighbors shape P̂ and each is scored again, so Σ L1 is a
composite likelihood, a product of conditionals, not a chain rule; the frame
leaves out 8 samples, and noise correlated over 5 leaks at its edges. (3)
*Marginals per sample:* ln Σ_l per sample, where one amplitude over a mark would
be ln Σ_l Π_k; a broad belief understates a consistent mark. (4) *No charge for
the level:* a new over pays nothing for it, and nothing charges a drop inside a
mark.

### Filter scales and fists (segmental)

**The scale** (`at_filter_scale(config, m)`, m ≥ 1 and whole; m = 1 returns the configuration as is). Sample interval, the filter's dot `front_dot_s` and the fastest dot `dot_min_ms` × m; `speed_count` − round(ln m / ln 1.1): 25 and 20 speeds at m = 2 and 3, so the slowest dot is 236 and 220 ms against 230 ms (derived). Frame = ⌈21.33 ms / (Δm)⌉ samples: 4 samples (21.33 ms) at m = 2, 3 samples (24 ms) at m = 3. Look-ahead and commit: the nearest whole frames to 170.67 ms: 8 frames (170.67 ms), 7 frames (168 ms). The floor's stride and refresh × m (21.33 ms and 192 ms; 32 ms and 288 ms). Ceiling `filter_scale_ceiling`, 300. Everything else as configured: beam 15 nats, f, the walk in nepers per √s, the key's 30 s⁻¹ (0.16 and 0.24 per sample). A sample is committed 32 to 63 samples after it arrives at m = 2 (170.7 to 336 ms), 21 to 41 at m = 3 (168 to 328 ms); with the search's 17 samples the evidence lags at most 0.43 s and 0.46 s (derived).

**The tables' channel** (`segmental_tables_channel`). `output_noise` and `make_template_tables` are computed on the channel filter at 48 kHz, whatever the input rate, when the rate is a whole multiple k of 48 kHz and `fft_size` is a multiple of 64 k: rate / k, `fft_size` / k. The filter's length in s, its cutoff and the channel's rate do not depend on the input rate (derived: N goes with the rate), so only the positions averaged over differ. At the full rate the template's work goes as m × rate², 80 rises × 16 × 4m × decimation positions × a length proportional to the rate (derived): 16.0 s to 18.0 s for {1, 2, 3} at 768 kHz on the desktop, on the pipeline's thread, with the radio running, against a ring of 5.46 s (measured). On the stand-in 0.08 to 0.10 s at every rate (measured). Agreement with the full rate, by test: s_j within 1e-3 (at most 7.6e-4 measured), half point within 10 µs, σ_y²/N₀ and w within 1e-4 of themselves. A 48 kHz input is unchanged.

**Filter and template.** A narrower filter's pulse is a dot T with raised-cosine edges of full length E = `front_taper_ratio` × T, 0.25: 6 and 9 ms at T = 24 and 36 ms; the widest keeps the chain's 4.235 ms. Taps by `front_filter_taps_any_width`: 2⌊(T + E) × 1 500 /s / 2⌋ + 1 = 45 and 67, delay 14.67 and 22 ms (derived). The output noise and w come from `output_noise` on the same chain, with neighbors every 4m channel samples: σ_y²/N₀ = 38.87 and 26.01 Hz, w = 0.2073 and 0.2081 at m = 2 and 3 (derived). The template is that of the chain with `template_delay_s` of zeros before the channel taps: half of a step's response lies at 12.79 ms + (T + E)/2 after the edge, 7.84 samples at m = 1; a narrower filter's lies earlier in samples, so the delay, rounded to whole samples, keeps it there: 3 samples (16 ms) at m = 2, 3 samples (24 ms) at m = 3, the half point at 7.41 samples (derived; s_j = 0.096, 0.311, 0.533, 0.756, 0.952, 1.000 at j = 5 … 10, m = 3). So a story claims its edges that much early, which only the symbols' times read, and the evidence's window m = 5 … 10 stays valid (`FilterScale.ItsEdgesLieInTheEvidencesWindow`).

**Why E = T/4.** The pulse's spectrum is that of a boxcar T times that of the edge: H(f) = sinc(fT) × cos(π f E) / (1 − (2 f E)²) (derived). The first factor sets the main lobe and the nulls at k/T, 27.8 Hz apart at T = 36 ms; the second is about 1 for f ≪ 1/(2E) and falls as 1/f² past it, its first zero at 3/(2E): 354 Hz at E = 4.235 ms, 167 Hz at E = 9 ms. So a neighbor 60 to 200 Hz away meets a smaller sidelobe, and the main lobe, which passes the wanted station, barely moves. Taps alone, dB re 0 Hz, E = 4.235 ms to 9 ms (derived, computed): 40 Hz −13.5 to −14.3; 60 Hz −23.5 to −25.4; 80 Hz −28.8 to −32.3; 100 Hz −23.0 to −28.9; 120 Hz −26.3 to −35.7; 150 Hz −28.5 to −47.8; 200 Hz −38.2 to −55.1. The noise bandwidth falls 26.96 to 26.04 Hz and the 10–90% rise of the pulse's integral stays at 0.8 T = 28.8 ms (derived). A smaller leak lowers the leak floor with it, so a wanted station stands higher above σ², its stream's S − F grows faster and earns the 120 nats of the lead (mechanism traced at +15 dB S₅₀₀ with a neighbor at +12 dB re wanted at 80 Hz, review 7: each stream's floor 3.6 × 10⁻⁵ to 7 × 10⁻⁵ FS² where its noise is 2.3 × 10⁻⁷ and 6.2 × 10⁻⁷ FS²). The size of the effect, and 0.25, are measured, not derived: a heuristic the test set chose, the middle of a plateau from 3/16 to 5/16 (pairs and lists: A.8e's provenance; 3/8 costs from +30 dB S₅₀₀ and 1/2 much, the conjecture being that the longer pulse outruns the evidence's 6-sample edge window). The template delay's move from 32 to 24 ms does almost none of it (measured): with the delay crossed over, the pairs read as their E does, to 0.002 on the oracle path and 0.012 on the detector's, and `w11`'s and `f11`'s lists are the same; one stress cell, 100 Hz at +10 dB re wanted, reads 0.398 to 0.271 with the delay alone and 0.008 with the edges. m = 1's E/T is 0.353, the owner's filter (D17), left alone.

**Fists** (`ModelTables`). For speed s and n = 0 … `gap_stretches` − 1: the fist (s, g = 1.1^(5n)), while n = 0 or 12 ms × 1.1^(s + 5n) ≤ `gap_unit_max_s`, 0.7 s. So 178, 129 and 98 fists at m = 1, 2, 3 (derived; at most 254). A letter or word gap at fist (s, g) reads the tables of speed s + 5n, marks and element gaps those of s. At most 255 tree nodes. A birth's or rescue's mark, on ending, offers every speed with g = 1 at ln(1/speeds) + ln(1 − π) + ln P_label; the same ends at g > 1, at ln(π/6) in place of ln(1 − π), are offered only if inside the beam of the best so far, which leaves what opens unchanged and saves the bookkeeping on noise. π = `gap_stretch_prior`. Merging is on (j, fist, node, kind). The over's end ends the fist.

**The choice** (`SegmentalDecoder::choose_leader`, at each 0.25 s comparison and at `flush`, where no stream is dropped). `now` is the channel's age: the stream time less that of its first sample, a replay's included, so that a channel that opens late gets the same 1.5 s and 10 s as one at the stream's start (`FilterScale.TheChoiceRunsOnTheChannelsAgeNotTheStreams`). At the first call with now ≥ `filter_scale_start_s` each stream's S − F and symbol count are taken as zero points. Then, with `symbols` and S − F since then: eligible(i) = i is the widest, or symbols(i) ≥ `filter_scale_min_symbols` × symbols(widest), or S − F(i) ≥ `filter_scale_enough_nats`. A leader not eligible gives way to the widest. top = the leader's S − F; in scale order, an eligible stream above top + `filter_scale_switch_nats` becomes leader and sets top. If the leader changed, its whole text is compared. Then, from `filter_scale_drop_after_s` (a negative time never drops), a stream that is neither the widest nor the leader leaves if not eligible or below top − `filter_scale_drop_margin_nats`. `keying_evidence` reads the widest stream, so the judge reads one number across the track's life; its text sums read what was published, the leader's. A speed reported is the leader's.

**Derivations.** g's range: ARRL's standard spreads the added time over the 19 gap units of PARIS, so g = T_g / T_s, and letter and word gaps stretch alike (QEX, April 1990; the generator's). 33 WPM characters at 5 WPM overall give T_g = (60/5 − 31 × 36.4 ms)/19 = 572 ms, g = 15.7. A gap half a step off (1.61^½) is 0.24 nepers, 0.8 of the fist's σ, about 0.3 nats per gap. Taking the fist at the first mark's end, in the state, rather than a value learned per track, lets the traceback correct the first letters' spaces. S − F is the evidence for Morse's timing and text over no timing on what that filter passes; S alone favors the widest filter, which earns on the neighbor too. The Bayes-factor reading of S − F: both are scores on the same evidence stream, one under Morse's timing and text, one under a key with no timing; between filters, whose data differ, it is a heuristic, and so said.

**Provenance.** All in `docs/decision-log.md`, 2026-10-31; results in the results records.
- Scales {1, 3}: heuristic by measurement on the whole test set, seeds 1 and 2: against {1, 2, 3} E2 reads 0.575 against 0.562 and 0.694 against 0.677, the other groups within 0.001, on the detector's path E2 better, for 28% less CPU. Before the leak floor (all of this bullet's values). Scale 2 alone has a null at 1/(24 ms) = 41.7 Hz (derived): a steady carrier 40 Hz off at 0 dB re wanted reads 0.000 with it and 0.975 to 0.983 without, on the oracle path only (with the leak floor on the final code, 0.321 with it); at 35, 45 and 50 Hz neither reads, and on the detector's path neither reads at any of 25 to 60 Hz (measured; the results record). Width 6 (13.6 Hz) reads E2 better and fading and the second QSO group worse, for about 1.9 ms per channel-second more, narrow enough for a detector's frequency error to matter; 4 did not earn its place.
- π = 0.002: heuristic by measurement on seed 1 with the widest filter alone; 0.01 and 0.0004 read fists or Farnsworth worse, and it pays for CPU on near-threshold channels (values in the log).
- 1.5 s: heuristic: N₀ reading low in the first second gave one stream 100 000 nats, before the channel floor is in (1.056 s).
- The whole track rather than a window: a 30 s window read the QSO groups worse, the whole text being published and the last 30 s one station.
- 0.5 of the symbols, 100 nats (eligibility; the drop's margin is another number): heuristics. Where a narrow filter reads next to nothing S and F are both about 0, which beats a weak station's often negative S − F at the widest; the second route is for a widest filter misled into over twice the symbols by a neighbor.
- E = T/4: heuristic by measurement (`Why E = T/4` above). Before and after, measured side by side on two seeds' whole set and on the recordings, oracle and detector's path. The pairs (a wanted station at one level, a neighbor 40 to 300 Hz away at −10, 0, +10 dB re wanted), +10, +15, +20, +25, +30, +35, +40 dB S₅₀₀, oracle: 0.090, 0.128, 0.124, 0.176, 0.163, 0.157, 0.138 to 0.068, 0.103, 0.100, 0.119, 0.138, 0.136, 0.137; detector's path: 0.180, 0.209, 0.214, 0.249, 0.240, 0.259, 0.232 to 0.169, 0.182, 0.181, 0.239, 0.231, 0.238, 0.233. `w11` 39 to 40 true calls of 48, 0 to 1 false; `f11` 36 to 40, 0 false; `real30`, `live2`, `live3`, `w12`, `band100` the same rows; the contest 219 true both, 7 to 6 false. Whole set, signal by signal, change in CER with a 95% interval by bootstrap: A2 −0.0005 (−0.0008 to −0.0001), B2 +0.0006 (0.0000 to +0.0013), I2 +0.0005 (+0.0002 to +0.0009), S2 −0.0008 (−0.0017 to +0.0001), the others none shown. E2 oracle 0.4867 to 0.5029 and 0.6580 to 0.6421 on seeds 1 and 2, detector's path 0.6759 to 0.6766 and 0.7696 to 0.7731, each inside its interval. CPU none shown: 3.459 to 3.463 and 3.431 to 3.429 ms per channel-second on the whole set, 6.13 to 6.18 on `real30`.
- 120 nats: heuristic by measurement: on a clean weak station a narrower filter can stand a few tens of nats above the widest and read a little worse; the neighbors' cells stand above it.
- 10 s of age, 30 nats, the ceiling of 300: heuristics that buy CPU. 30 and 100 nats read alike to 0.001 on the test set's seeds 1 and 2, and 30 costs about 10% less there (measured by the fourth review). 5 s reads E2 worse (0.575 against 0.562, three filters). The age and not the stream's time: on the stream's clock a late channel dropped its narrower filters at a median age of 2.07 s, E2 late on the detector's path reading no better than the widest alone (0.719 both). a beam of 12 nats at the narrower filters would save more and costs a close neighbor's text, so the beam stays 15 nats.

### Channel floor (segmental)

**Derivations.** For noise alone a sample of envelope e has e² exponential of mean σ_y² (a complex Gaussian's power), so its
quantile q is −ln(1 − q)σ_y²; −ln 0.9 = 0.10536 (derived). The stride of 4 samples leaves neighbors at ρ₄ = 0.14 in amplitude,
about independent. The max with the hiss keeps the bias low on a clean channel: on white noise the floor reads about 3 dB below
the hiss by its margin, so the hiss rules and the decoder is unchanged (derived; the margin's effect measured).

**Provenance.** The margin −3 dB (re the floor as estimated), the 5 s window, the quantile 0.10, the stride, the 96 ms refresh and the 1 s
start: heuristics. −1.5 dB costs about 1 dB of sensitivity and −3 dB nothing measurable (`docs/decision-log.md`, 2026-10-31; review F1).

### Level filter (segmental)

**Derivations.**
- **a_l²/σ²:** S₅₀₀ is a² over N₀ × 500 Hz and σ² = N₀ × 74.50 Hz on noise, so a²/σ² = S₅₀₀ × 500/74.50 = 6.711 S₅₀₀ (derived).
- **The key's memory.** With a key independent from sample to sample, a low level gains ln(1/(1 − π)) nats per sample of a gap over the true level,
  and the belief leaves the true level in every long gap (measured in stage 1: CER above 1 at +20 dB S₅₀₀), so the key has a switch rate.
- **The grid's variance.** A level is off by up to half a step, uniformly: variance step²/12 in nepers². Without it, above about +30 dB S₅₀₀ a
  plain sample pays for the step while a sample in an edge's window, with its wider V, pays less, so cutting marks gains (measured: +40 dB S₅₀₀
  read all dots at a beam of 60 nats). f′² = 0.01 + 0.002485 = 0.012485, f′ = 0.1117; at f = 0.02, f′ = 0.0537.
- **Cost of an error at strong levels.** A relative amplitude error δ costs about wδ²/f′² nats per sample, and key-down on nothing about w/f′² = 15.9
  nats per sample (at f = 0.02, 68.9; derived). So f sets how large a dip is read through rather than read as a gap. The rise of CER with the level on VE3NEA's fading conditions (hand, f_D 0.1 Hz: 0.013 at +37.78 dB S₅₀₀, 0.112 at +57.78 dB S₅₀₀) was suspected to be this cost and was not: it was the leak floor reading the station's own keying (text-level-1, "Why the level is on the floor" and "A neighbor's leak"). This cost is what stays: short deep fades read as gaps at f_D 3 Hz (Limitations).
- **The float fix (review F6, F8).** wz² passes 10⁹ nats above +80 dB S₅₀₀, where a float's step is tens of nats, beyond the beam. So `Term` holds 1/v
  and the float holds x less wz², small at the level that fits, and z, rise and fall are doubles. The forward message's top is taken from the stored
  float, as the backward's is, so that exp(x − top) cannot meet a positive rounding error (the cause of F6's infinity and then NaN). F6's guard stays.
- **Why the level is on the floor (text-level-1).** The cause of the rise of CER with the level on VE3NEA's conditions. On VE3NEA's benchmark conditions (our port of his generator, a +57.78 dB
  S₅₀₀ station 1.2 kHz from the next) the leak floor read the station's own keying (below, "A neighbor's leak"), up to 40 dB re the hiss,
  in 21.33 ms blocks held 0.2 s. On a grid re σ², a steady carrier's level then stepped by up to 30 dB re its level before the step
  inside a 264 ms dash (z² 30 dB to 60 dB re σ² and back at 2.133 s and 2.181 s; traced sample by sample), more than the walk's 6.1 dB
  re the level per frame (one standard deviation) can follow, and the
  search read the steps as edges: G as C, the dash split. On the floor the level stays where it is (test
  `LevelEvidence.NoiseAboveTheFloorLeavesTheCarriersLevelWhereItIs`). A level far below σ², which the grid re σ² could not hold,
  scores next to nothing on bare noise, so a story bridges a gap for nothing wherever a neighbor's leak raises σ²; scored as the bottom
  it costs what it did (with `level_noise_bottom` 0 and the leak bound on, a keyed neighbor +12 dB re wanted 100 Hz away and a weaker caller 60 Hz from a
  stronger read worse: `BenchRun.SegmentalReadsPastAStrongNeighborBehindTheNarrowerFiltersSidelobes`, `EngineCopies.TwoCallersKeyingAtOnceKeepTheirTracks`).
- **Walk against Doppler.** The Doppler's own rate at f_D = 3.5 Hz near the rms level is 1.4 nepers per √s (Butterworth; derived). The walk is larger
  because a random walk's variance grows as lag and a smooth level's as lag². Since text-fast-1 the walk and the frame follow the
  spread measured on the channel (below, "The channel's tail and the walk set by the Doppler spread").

**Provenance.** Grid range and step, frame, jump rate, look-ahead and commit: heuristics; CER does not change from 0.085 to 1.5 s of look-ahead, since the walk
forgets within about 0.1 s (measured). Walk 4.8 nepers per √s and key rate 30 s⁻¹: heuristics chosen by measurement; the walk trades fading against steady
signals and 4.8 sits at the fading end (`docs/decision-log.md`, 2026-10-30). f = 0.1: heuristic chosen by measurement (the log, 2026-10-31; 0.02 reads flutter badly at
strong levels, 0.2 costs the low group and fast speeds). The belief threshold 10⁻⁴ and the 8 groups: heuristics that bound the edges' cost when the belief is broad.
The level-dependent walk (a variant, off): +20 and +40 dB S₅₀₀ re F are heuristics, where the strong stations of VE3NEA's conditions
stand; W_h tried at 10 and 14 nepers per √s; not the default, pending its whole-set runs (measurements: decision log, 2026-11-21).

### Running sums, search and scores (segmental)

**Derivations.**
- **The windows.** s₄ = −8 × 10⁻⁴ and s₁₁ = 1.0023, so a claimed edge shows from sample 5 and is complete at 11; taking s as 0 and 1 outside is a derived
  approximation, no error seen to +60 dB S₅₀₀ (measured).
- **The merge.** Given the shared evidence and a next element of at least 6 samples, a story's future depends on its past only through (j, speed, node, kind), so
  merging on them at j is exact. Below 6 samples the score reads only the pair of edges, and the correction carries it. That three edges overlap, above 100 WPM, is not modeled.
- **The over's tail.** 6 samples separate a fall's window from a next rise's so each sample is counted once (test `SegmentalSearch.AWindowIsNeverCountedTwiceAcrossAnOversEnd`).
  An over cannot follow its end by under 16 ms, which no sender does.
- **The horizon.** Pruning on evidence through j + 10 alone dropped every gap one sample before its true rise once the level was high (stage 1: CER above 3 at +20 dB S₅₀₀): an
  element cannot be charged for an edge it has not claimed yet whose evidence stands inside the horizon. This is the viterbi review's merge finding in another form.
- **Why no newborn.** The backward message gives a new over its level, at 170.7 ms or more of look-ahead.

**Is the approximation defensible?** The four reasons of section 8e say what it is. Whether it serves is measured: `docs/decision-log.md` and the results records. The evidence's lag, not the
text's, is at most 0.387 s at m = 1: 0.34 s for the level and 17 samples for the search (derived). A publisher reading the best story at 0.75 s behind reads the final text's CER (measured; the log, 2026-10-31); the live publisher (A.9b) sends sooner and corrects.

**Arithmetic.** The level filter and the evidence take about 180 exp and 150 ln I₀ per sample, so these are plain tables and polynomials (`segmental/fast_math.hpp`), the same formulas on every compiler; the tables are filled at start from the C library and differ in low bits between MSVC and glibc, and `real30` and `live2` read the same text on both (measured): exp_neg(y), y ≤ 0, by a table of 2^(i/32) and a cubic, relative error 6 × 10⁻¹⁰ (r⁴/24, |r| ≤ ln 2 / 64; derived), 0 below −40 nats, under the rounding of a sum that holds a term of 1 (e⁻⁴⁰ = 4 × 10⁻¹⁸ against 2⁻⁵³; derived); log(x) by a table of 256 mantissas and a cubic, absolute error 4 × 10⁻¹² nats (derived); ln I₀(x) by linear interpolation at steps of 1/64 below 32, absolute error 1.6 × 10⁻⁵ nats (h²/8 × |f″|, |f″| ≤ 0.5; derived), and the asymptotic series to x⁻³ above. A float at 100 nats, how x_k(l) is held, has a step of 7.6 × 10⁻⁶ nats. On the oracle path of the slice of the test set every signal's text came out the same as with the C library's (6 154 of 6 154).

**The search's bookkeeping.** A fist's lengths stand in one row (`ModelTables::FistRow`); gaps and marks stand in two lists, since a mark's slot's parent is always a gap and a gap's a mark; a birth's ends are tabled by length; a slot is 16 bytes. These change nothing read: 6 154 of 6 154 signals the same.

**Provenance.** Beam 15 nats: a heuristic set by measurement (the log). `duration_z_max` = 4: heuristic. Ceiling 1 500, the stretched story's +4 nats, `open_spacing_dots` 0.125: heuristics chosen by measurement on a slice of the test set's seed 1 (a fifth of A2, all of C2, E2, I2) and on real 40 m, whose list held at 20 calls; settings tried and what they cost: `docs/decision-log.md`, 2026-11-01. The over's tail: 6 samples, the
windows' length. Everything shared with viterbi: A.8d.

### Limitations (segmental)

These are the model's and, except where said, the viterbi decoder's too. The white-noise test set shows few of them. Measurements: `docs/decision-log.md` and the results records.

- **Close neighbors.** The model has one station per stream, and the belief follows whichever carrier is up. A neighbor's mark in the wanted station's gap scores as key-down, and the same story bridges the gap. The narrower filters keep out a neighbor outside their band. The leak floor below keeps a neighbor's leak from scoring as keying once it stands above the hiss. What stays (levels in dB re the wanted station's key-down power; scores in the results records): a neighbor at or below the wanted station's level inside about 60 Hz, at every level; neighbors +10 dB re wanted at about 80 Hz (0.54, from 0.74), and +12 dB re wanted at 60 to 100 Hz in part, by cell; neighbors far stronger, as +44 dB re wanted 200 to 300 Hz away, and +20 dB re wanted at 150 Hz; a steady carrier +10 dB and stronger inside the channel, where the whole text is lost at every setting tried. A very strong neighbor at 250 Hz writes false dots, believed to be its key clicks (not isolated). On the detector's path the detector sets part of the limit. Since `det-1` a station 60 Hz from a keyed neighbor at +10 or +20 dB re wanted gets a track (at +20 dB its channel then reads the neighbor's text), where on 23.4 Hz bins, a peak the maximum within ±47 Hz, it got none (CER 1.000 with any filter). It still gets none 60 Hz from a neighbor at +30 dB re wanted, 100 Hz from +30 or +40 dB, and 150 Hz from +40 dB (at +30 dB, 150 Hz, it does): the neighbor's skirt and keying sidebands make it fail the prominence or sideband test (A.6, whose own limitation, on the test set's cells, puts the 60 Hz edge at +20 dB). Measured by review 8 on 48 kHz signals, wanted +15 dB S500, one case each. A neighbor 30 dB below the wanted station, 60 Hz away, can still fill its word gaps (K9ZZ at +45 dB S500 read "K9ZZQ9ZZ" for "K9ZZ K9ZZ", the same review). Untried: a second carrier in the level filter, the neighbor's beat in the complex samples, subtracting a neighbor's track. Viterbi, one width, is worse here.
- **A neighbor after the drop.** A neighbor arriving after the narrower streams are dropped (from 10 s of age) is read at the widest filter alone, for the channel's life.
- **One stream's whole text.** Stories do not cross between streams (one sample of time mismatch is hundreds of nats at +20 dB S₅₀₀, so no joint search can prune across widths), and a QSO whose stations need different widths reads at the better over the whole track. A long track whose conditions change keeps its early leader unless another earns 120 nats more. Around 40 Hz at 0 dB re wanted a short text can fall either side.
- **A narrower filter off frequency.** The channel follows the detector's frequency, which may be up to half a bin off a station (A.7); no fine estimate corrects it, and a narrower filter pays more for it. Oracle scores do not show this.
- **Memory.** About 2.2 MB a track (measured, Linux: 658 MB at 300 tracks; 549 MB before `fast-4`; 459 MB without the spacing's tables, which a long-lived track touches nearly in full, about 0.7 MB). The margins' variant adds about 1 MB a track ("Margins" below). text-fast-1 adds 72 KB a stream (the tail's counts and wholes, 8 192 each), 152 KB more with a tail (the prefixes of 1 024 claims), 60 KB with the fast walk (its two matrices; derived).
- **Impulses inflate the hiss.** The hiss averages each bin's power over time, so a burst counts by its mean, and N₀ reads high after it. Using the channel's floor alone would not save it: the hiss would stop reading high and every
  burst would become a mark, so the inflated hiss is hiding insertions. A blanker before the channelizer, then a median over time in the hiss, is the unverified fix.
- **A tune-up writes T's** (segmental only). Each element ends at its longest length, so a carrier held past 2.3 s (a dash at the slowest speed) ends every story's mark and writes a T about every 2.3 s (`search.cpp`, the prune by
  length). The channel's floor reduces this by reading a long carrier as noise.
- **A long carrier reads as noise** (segmental only). A key-down held over 90% of the 5 s window raises the floor to the carrier, and it fades from the text.
- **The first second has no floor.** Behind a receiver's filter a few false symbols appear before the floor is first in, at 1.056 s.
- **Farnsworth spacing** is in the state (segmental only; viterbi: A.8d). A sender whose gaps change within an over pays a rescue, and a stretch costs 8.0 nats once per over, so a short Farnsworth over reads word spaces until that is earned back. Letter and word gaps stretch alike, by ARRL's rule; a sender who stretches only one pays the misfit on the other.
- **The symbol prior agrees with the generator.** The on-air table is the one the filler text is drawn from; p_w is that text's mean word length and q the development set's count. Any CER on this text is therefore an upper estimate of what the
  prior gives on the air. Real text will sit somewhere between. Both decoders read the same prior.
- **The whole text's readout is uncosted.** `best_text` walks the whole chain, at the channel's end, at a change of stream, and when more than 64 symbols came in one period. Text costs 40 bytes per symbol, 0.3 MB per hour of continuous sending, unbounded until the channel closes.
- **Short deep fades at high levels** (VE3NEA's conditions, f_D = 3 Hz). A fade to 20 to 30 dB below the mark's level (dB re its
  key-down power) lasting 10 to 20 ms inside a mark is, at +40 dB S₅₀₀ and above, cheaper as a short gap than as a mark: each fade
  sample pays the belief's distance (about 10 nats a sample when the fade stands 20 dB below the level the belief holds, dB re that
  level, the belief ±4.3 dB re its center), its frame's belief coming from its neighbors, while the gap's story pays its
  prior once and the short element's template dips as the fade does. A hand-keyed 7-dot dash reads as two dashes (T as M). After
  text-level-1 CER on the port of his generator stood at 0.073 to 0.083 for hand and 0.045 to 0.052 for paddle from +17.78 to +57.78 dB
  S₅₀₀; with the walk set by the Doppler spread (text-fast-1) at 0.023 to 0.033 and 0.019 to 0.028 (DeepCW 0.075 to 0.039 and 0.070 to
  0.020). f = 0.2 about halved it there (two recordings of each style) but costs A2, B2, C2 and F2 (decision log, 2026-11-21); the
  envelope cannot tell the two apart, the phase can (a Rayleigh null turns the phase, a key-up does not). Backlog.
- **σ² rounded to the nearest step** (text-level-1). On the floor z is taken at F × 10^(1.5 d / 10), d the nearest whole step
  (exact form, "The level on the floor"), which stands up to 0.75 dB re σ² below σ² itself, so below the leak floor 10^(0.3) L when
  that sets σ²: a neighbor's leak is then scored against up to 0.75 dB less noise than the +3 dB re the leak asks for. On the 2017
  recording KC2LRC-2 the caller K4RUM, 73 Hz from a run station, loses its text there and with it its call (window-confirmed calls
  200 to 199; traced by the independent review). Two alternatives keep K4RUM, measured by that review: σ² rounded up (d = ⌈s⌉, never
  below σ²), and z taken at the true σ² with only the tables' shift rounded; both cost E2 more than the nearest does, +0.0078 and
  +0.0096 against main where the nearest costs +0.0044 (DEV2 seed 1, oracle path; decision log, 2026-11-21). So the nearest stays,
  one confirmed call of the 2017 set's 200 against E2. E2's cost against main is not explained by the rounding: neither alternative,
  each of which keeps z's noise at or above σ², reads it better, and its cause is not known. Finer shifts (between table rows, or
  0.5 dB sub-steps): backlog.
- **The level is not bound to a story.** Nothing charges a drop inside a mark, and a new over pays nothing for its level (section 8e). Near threshold this reads better than viterbi; viterbi reads a little better from about +2 to +5 dB S₅₀₀, at +20 dB S₅₀₀
  with f_D = 3.5 Hz, and from about 60 WPM up.
- **Three overlapping edges** (above 100 WPM) are not modeled; 95 WPM reads with a small error.
- **Long life.** Scores are absolute, in doubles, and C falls or rises without bound: at +60 dB S₅₀₀ a double's step reaches 0.1 nats after about 21 days of continuous sending (derived). Boundary stamps wrap at 2³⁰ − 1 boundaries, 33 days (derived).
- **Settings accepted and ignored.** The model's fields that segmental does not read (`level_min_db`, `level_max_db`, `quadrature_order`, the newborn's and the amplitude's).

**Fixed at this commit** (findings of the independent review of commit 5aeb081, numbered F1 to F10; `docs/decision-log.md`, 2026-10-31): F1 (the channel's floor), F5 (f = 0.1), F6 (one extreme sample deafening a channel), F8 (false letters above +80 dB S₅₀₀, the float fix).

### The channel's tail and the walk set by the Doppler spread (segmental; text-fast-1)

**Exact form: the tail** (`EchoEstimator`, `LevelEvidence::set_echo`, `RunningSums::tail`). Per stream, each sample's e² and the floor
F (`ChannelFloor`, not the leak floor) go to the estimator before `LevelEvidence::push`. A run is the samples with e² > 10^(1.0) F
(`echo_run_db`), kept to 4 096 samples. At its end, if it holds at least ⌈32 ms / Δ⌉ samples (`echo_min_run_s`; 12 at m = 1) and
its median M is at least 10^(2.0) × the floor at its middle sample (`echo_min_db`), the profile starts at its last sample with e² ≥
M/4 and takes (e² − F)/M at that sample and each later one until a sample starts the next run or span = 11 + P − h samples are
taken, P = min(18, ⌊48 ms / Δ⌋) positions (`echo_max_s`; 18 at m = 1, 6 at m = 3), h the last m with 1 − s_m ≥ 0.5 (7 at m = 1, 6 at
m = 3; derived from the template). Each closed profile enters sums S[q] and counts N[q] with memory λ = e^(−1/200) per run
(`echo_memory_runs`): S ← λS + value, N ← λN + 1, for the positions it reached. From 30 runs (`echo_min_runs`), every 10th run
the shape is made again: A[q] = S[q]/N[q] where N[q] ≥ 10, else 0; on if A[11 − h] ≥ 10^(−1.5) (`echo_on_db`), the tail g_(11+q) =
A[11 + q − h] for q = 0, 1, … while ≥ 10^(−2.5) (`echo_end_db`), at most P; g at the fall window's six positions all A[11 − h]. A
new shape goes to `set_echo` at once. There, per table level i (r_i² = a_i²/σ², the grid's index with the tables' shift): the fall's
terms `LevelTables::make_term`(r_i², 1 − s_m, f′², v_m + g₁₁) in place of the tables' fall terms; and for each tail position q,
ρ = g_(11+q) r_i², floats ρ/(1 + ρ) and ln(1 + ρ). `commit_frame` then computes, per sample k and position q, T_k[q] = ln Σ_groups
p_g exp(w(z² ρ_g/(1 + ρ_g) − ln(1 + ρ_g))) over the same 8 groups of levels as the edges, and `RunningSums::push` adds it to the
prefix of the fall claimed at k − 11 − q at its q-th position (a claim whose earlier position was not pushed, after a shape changed its
length, takes no more). `tail(i, n)` reads the prefix (the last 1 024 claims) or, past the positions pushed, their whole (the last
8 192). With the tail off nothing is pushed and every tail reads 0.

**Exact form: the tail in the search** (`search.cpp`). A gap that started at i, at boundary j, length L = j − i: ended (L ≥ 6) at
base + tail(i, L − 6); partial at base + survival + reach, the reach max(tail(i, L + 6), max over d of [gap_reach_at(d) + tail(i, L
+ d − 6)]), d from max(1, 6 − L) to 6 (going on through the horizon, or ending at j + d), or gap_reach_from + tail(i, filled) once L −
5 ≥ the positions filled; a gap that starts at j: gap_reach_new + (that reach at L = 0 less gap_reach_new). An over's end at j goes to
the no-signal story at j + D, D = max(6, n + 6), n the tail positions of the last sample pushed, paying D ln(1 − P), and there adds
tail(j, 18); two ends due at one boundary keep the better (both with their tails so far). The ring of over's ends has 25 places.

**Exact form: the walk** (`DopplerEstimator`, `SegmentalDecoder::update_walk`, `LevelEvidence::set_walk`). On the widest
stream only, each envelope sample's complex y and F: a run is |y|² > 10 F (`echo_run_db`), kept to 4 096 samples. At its end, M its
median |y|²; in each stretch of consecutive samples with |y|² ≥ M/10, from 3 samples inside each end (`doppler_end_guard_s` 8 ms),
pairs (k, k + 8) (τ = 21.33 ms at m = 1) give C = Σ y_(k+8) y*_k, P₀ = Σ (|y_k|² − F), P₁ = Σ (|y_(k+8)|² − F), and the pairs (k, k + 4)
from the same k the same sums at τ/2. If any pair, the
estimator keeps C, P₀, P₁ and the runs' level 10 log₁₀(M / F at the run's middle) with memory e^(−1/100) per run
(`doppler_memory_runs`); |ρ| = |C| / √(P₀P₁), f_D = 2√(−ln |ρ| / (2π²)) / τ (0 at |ρ| ≥ 1), level = the level sum over the runs'
sum. From 20 runs (`doppler_min_runs`), at each run's end: fast if the level ≥ 18.3 dB re F (`doppler_min_db`) and f_D ≥ 3 Hz
(`doppler_fast_hz`; 2.5 Hz, `doppler_slow_hz`, while fast) and the shape ln |ρ(τ)| / ln |ρ(τ/2)| ≥ 2.5 (`doppler_min_shape`; 0 while either |ρ| reads 1);
W = clamp(1.4 f_D, 4.8, 20) nepers/√s (`doppler_walk_per_hz`,
`level_walk_nepers_per_sqrt_s`, `doppler_walk_max`). Every stream is told when the mode changes or W moves by 0.5 or more.

**Exact form: frames and blocks** (`LevelEvidence`). A frame is the largest whole share of level_frame_s's samples within
level_fast_frame_s (10.67 ms): 4 samples at m = 1, 1 sample (8 ms) at m = 3. Frames make blocks: slow_block = level_frame_s / frame (2
at m = 1, 3 at m = 3) while slow, and 1 while fast unless the stream's tail is on. The level is held over a block; at a block's end it
steps by the configured walk (kind 1: 25 three-tap passes, as before), by the fast walk over one frame (kind 2) or over level_frame_s
(kind 3), each a dense matrix K[to][from] of a Gaussian of W √(T) / 0.1727 grid steps, T the frame's or level_frame_s's length, folded
at both ends (the method of images) and normalized per column; the jump at 0.05 s⁻¹ × the block's length. A mode asked for takes
effect at the next block. Per frame the ring keeps its block's first frame and the step at its end; a frame's belief is (forward into its
block's first frame) × (backward into its block's end), its block's samples left out, as the 21.33 ms frame was. Look-ahead and commit
are the same in samples (16 and 16 frames of 4 samples). While slow the evidence is the old one to the bit (test
`LevelEvidence.ShortFramesJoinedIntoBlocksScoreAsTheLongFrame`).

**Derivations.**
- *The tail's likelihood.* A diffuse or scattered path, Rayleigh-faded and incoherent with the mark (measured on the clips: the
  coherence of y after the fall with y 2 ms before it is 0.94 at the fall, 0.24 at 10 ms), adds a zero-mean complex Gaussian of power
  g a² to the noise: the sample's density ratio against noise alone is (1/(1 + ρ)) e^(−z²/(1+ρ)) / e^(−z²), whose log is z²ρ/(1 + ρ) −
  ln(1 + ρ), tempered by w as every sample's (derived). In the fall's window the same power is noise added to the Rician term's variance.
- *Why the tail is read after the run's last −6 dB sample.* That sample is where the filtered mark's amplitude falls through half,
  the template's 1 − s_m = 0.5 between m = 7 and 8 at m = 1, so m − h counts from the claim (derived, to a sample).
- *Why an over's end waits n + 6 samples.* A birth at b has its rise window from b + 5; the tail's last sample is j + 10 + n: b ≥ j +
  n + 6 keeps every sample counted once (derived). Without it the over's end counted the next birth's first samples twice, about
  w z² ≈ 1 300 nats each at +30 dB S₅₀₀, and the clips read a 1-sample E every 7 samples (traced, a diagnostic build).
- *Why the gap's reach takes the tail along each way.* The reach adds to a gap's score the evidence through the horizon it can still
  reach; taking the tail's best and the next rise's best apart counts the next mark's samples twice (as tail and as mark), raises the
  beam's floor and cut the right stories (the clips read E and word spaces; traced). Taken together, the bound is that of one story.
- *f_D from |ρ|.* For a Gaussian Doppler spectrum of standard deviation σ_D the complex gain's correlation is exp(−2π²σ_D²τ²); f_D =
  2σ_D is Watterson's and DeepCW's width (the generator's notes). The pairs stay inside a mark: across a fall into a tail, another
  path's sample follows the mark's and takes |ρ| down in proportion to τ (on the clips kz4ap-fading's ln |ρ| is a straight line in τ
  over every pair of a run, f_D 9.1 and 9.9 Hz by the Gaussian fit; inside the marks the estimator reads about 4.6 Hz).
- *A spread or a beat.* For a smooth Doppler spectrum −ln |ρ(τ)| ≈ 2π²σ_D²τ² while |ρ| is near 1, exactly for a Gaussian one, so
  ln |ρ(τ)| / ln |ρ(τ/2)| = 4. A neighbor of relative power r and offset Δf inside the filter makes |ρ(τ)| = |1 + r e^(j2πΔfτ)| / (1 + r),
  ln |ρ| ≈ r (cos 2πΔfτ − 1) for small r: the ratio is 0.73 at 60 Hz, 0.015 at 45 Hz, 1.15 at 30 Hz, 2.45 at 20 Hz (derived).
- *The fast walk as a matrix.* The three-tap passes needed for W = 20 over 10.67 ms are ⌈W²T/(2 step²)/0.33⌉ = 217 a frame against
  61 × 61 for the matrix (derived): a Gaussian of W √(T)/step steps (12 at W = 20) folded at the ends, where the passes approach the
  same Gaussian (binomial) one.

**Provenance.** All in `docs/decision-log.md`, 2026-11-21 (`text-fast-1`).
- The tail's estimator (+10 dB re F, kz4ap-fading's run threshold; 32 ms; 200 and 30 runs), echo_on_db −15 dB, echo_end_db −25 dB,
  48 ms: heuristics. echo_min_db +20 dB re F by measurement: without it VE3NEA's clean stations at 12 to 15 dB re F read a tail of −7
  to −14 dB re the mark (a bias at the run threshold) and their CER rose (+1.78 dB S₅₀₀, f_D 3 Hz, hand: 0.705 to 0.739). On the
  matched fast-fading set (f_D 0.7 to 42 Hz) no stream learned a tail.
- The tail's state in the level filter (a third key state between key-down and key-up, the tail's power at the level, geometric length):
  tried and taken out. With it the clips read 0.659 and 0.584, against 0.352 and 0.247 without; conjecture: under fast fading the tail
  state, tolerant of amplitude, explains a mark's samples at levels 14 dB above the mark, and the belief splits.
- The shape's 2.5: heuristic by measurement, between a beat's under 1.2 (30 to 60 Hz) and a spread's 4: on the 40 m recording of
  2026-11-17 without it the fast walk lost WA2SCB and KE2BVB, confirmed calls, which came back with the walk off; the matched set and
  VE3NEA's conditions read the same with it (every cell to 0.001).
- 3 Hz, 2.5 Hz, 18.3 dB re F, 1.4 per Hz, 4.8 to 20 nepers/√s, 8 ms, 21.33 ms, 100 and 20 runs, −10 dB re the median: heuristics by
  measurement on the matched set, VE3NEA's conditions and DEV2 seed 1's slice (the log). The estimator reads the matched set's true f_D
  0.7 to 1.2, 2.2 to 2.9, 7.2 to 10.1 and 21 to 31 Hz at 0.6 to 1.6, 2.4 to 4.0, 6.6 to 11.0 and 14.2 to 19.9 Hz; a steady or 0.1 Hz
  carrier at 0 Hz below +20 dB re F and up to 2.5 Hz at +62 dB re F (amplitude left at a mark's edges, conjecture).
- The long frame where a tail is modeled: by measurement on the clips (with the short frame 0.716 and 0.662, with the fast walk and
  the long frame 0.375 and 0.221, the tail alone 0.352 and 0.247); conjecture: a short frame lets the level follow the tail into the gaps.

**Limitations.**
- The tail is one shape per stream, for every fall: a channel whose tail changes faster than about 200 runs (40 s at 35 WPM) is read
  with the shape before; the first 30 runs of a track are read without one. A station under +20 dB re F learns none, so a weak
  station's gaps still fill (the tail then under +5 dB re F).
- The tail's mean is read low by about 2 dB: a Rayleigh tail's samples above −6 dB re the mark move the last −6 dB sample into it
  (test `EchoEstimator.LearnsATailAfterTheFallAndNoneWhereThereIsNone`: 0.06 for 0.1).
- The tail is not in the level filter, and not in the next mark's rise window: where a gap is shorter than the tail, the next rise
  reads the tail's last samples as its start.
- A tail longer than 48 ms is cut there; an over's end then waits at most 24 samples (64 ms) for a birth.
- Under a tail that fills its gaps the estimator's runs are whole letters, split at −10 dB re the median; a tail's peaks above that
  join a mark to the next (a steady carrier with a tail at −10 dB re the mark reads f_D 6.2 Hz in the test channel and takes the fast
  walk, its frame long).
- The fast walk's matrix in the backward message is the current W's, though a boundary's step was taken at an earlier W (W moves by
  0.5 at most at a time).
- A neighbor's beat inside the filter reads as spread (E2's unfaded stations 5 to 27 Hz); before the shape test it took the fast walk on
  16% (40 m) and 18% (20 m) of the 2026-11-18 CWT recordings' tracks and 1% to 7% of the 2026-11-17 ones' (traced), reading E2 better
  and losing confirmed calls (decision log). A beat within about 15 Hz, whose ratio nears 4, and a spread whose spectrum is far from
  smooth still pass or fail the test wrongly. The tail is on for 0% to 0.8% of those tracks (traced).
- The fast walk leaves DEV2's other groups alone: DEV2 holds no fast-fading station above +10 dB S₅₀₀; cells of strong fast fading are in
  the backlog.

### A neighbor's leak (segmental)

**Why.** The failure with a close neighbor depends on the absolute level, not only on the level re wanted. The neighbor reaches the filter's output at L_n + 10 log₁₀(500 Hz / B) + H(f) dB re that filter's noise, L_n its level in dB S₅₀₀, B the filter's noise bandwidth, H(f) the chain's response in dB re 0 Hz (derived): at 100 Hz, −18.8 dB re 0 Hz at m = 1 and −30.1 dB re 0 Hz at m = 3. Before this floor, with a neighbor at 0 dB re wanted, 80 to 120 Hz away, CER was 0.000 at +10 dB S₅₀₀ and 0.86 to 1.54 at +30 dB S₅₀₀, and the older bank, whose decisions are relative to the track's own level, read such pairs better from about +20 dB S₅₀₀ (results record, `level-pairs.csv`). With σ² set by noise alone the leak is scored as key-down in the wanted station's gaps: section 8e's "no charge for the level" means nothing penalizes a low-level mark. The onset, where the wide filter's leak reaches about 0 to +10 dB re its noise, is a measured onset against a derived level.

**The form.** `SegmentalDecoder::set_band_spectrum`, per stream and block: L_m = Σ_k (P[c + k] / 1.5) |H_ch(kΔf)|² |H_f,m(kΔf)|² over both sides of the center bin c with k from ⌈60 Hz / Δf⌉ = 3 to ⌈320 Hz / Δf⌉ = 14 (70.3 to 328.1 Hz), the responses computed once from the taps (`SegmentalParts::leak_response`); σ² = max(N₀B_m, 10^(X/10) × max(L_m over the last 9 blocks, 0.192 s, the nearest to `leak_hold_s` 0.2 s at 21.33 ms a block), channel floor), X = +3 dB re the leak. P is the Hann-windowed spectrum's power: a tone of amplitude A reads A² in its bin and A²(1 + 2 × ¼) = 1.5 A² over its bins, so 1.5 counts it once (derived). H_ch is taken about the channel's center; the block is cut at a bin up to half a bin (11.7 Hz) from the detector's frequency, which the form ignores. c is the bin nearest the detector's frequency.

**On noise.** White noise reads P = 1.5 N₀Δf in a bin, so L_m averages N₀Δf Σ_k |H|², and 10^(0.3) L_m / (N₀B_m) is 0.068 at m = 1 and 0.030 at m = 3 (derived, the test `LeakFloor.WhiteNoiseStaysUnderTheHissOnAverage`). A peak over 0.2 s can stand above that average; on the test set it moved A2's edits by none of 151 629 (measured), so it is close to inert where no neighbor stands.

**The channel's own carrier** (text-level-1; a variant, `leak_own` 1; 0 by default). P₀ = the largest of the bins c − 1, c, c + 1 of each block, held as its
maximum over ⌊1 s / 21.33 ms⌉ = 47 blocks (`leak_own_hold_s`), and a bin k counts as max(0, P(c ± k) − ρ_k P₀). ρ_k
(`own_splatter_bound`, `SegmentalParts::leak_own_bound`) is computed at start: the periodic Hann window of the analyzer's N (2 048 at 48
kHz, 42.67 ms; the same in s at every rate), a tone at offsets δ = −0.5 … +0.5 bin from c (11), keyed on and off once with hard edges
anywhere in the window (edges every N/256 samples, every on-interval by partial sums), the most power at c ± k over the largest of the
three center bins at key-down for that δ: −10.6, −13.5, −15.6, −17.3, −18.8, −20.0, −21.1, −22.1, −22.9, −23.7, −24.4, −25.1 dB re
key-down at k = 3 … 14 (derived, computed; close to the step's 4/(πk)², −13.5 to −26.8 dB). A carrier keyed with soft edges puts less
there, so a channel's own keying never counts; a neighbor counts what stands above that bound (6 dB below the wanted station at 100 Hz
it keeps more than half; test `LeakFloor.TheChannelsOwnKeyedCarrierIsNotANeighbor`, through the analyzer). Why: on VE3NEA's conditions
the three +57.78 dB S₅₀₀ stations of one recording had σ² set by the leak 76% to 78% of the time; the leak floor 10^(0.3) L stood
at a median 19.5 to 28.8 dB re the hiss over all their samples, 21.7 to 31.3 dB re the hiss over those where it set σ², and up to
39.8 dB re the hiss (traced on the hand-keyed f_D 0.1 Hz recording); no neighbor stands within 320 Hz there, so it was their own keying, spread by the analyzer's 42.67 ms
window across the bins a block with an edge holds (a step in the window spreads as 1/k). With the level on the floor alone, such a σ²
still turned deep fades into gaps: a 352 ms dash at 3 Hz fading, its fade 33 dB re the hiss against a σ² 22.9 dB re the hiss, read
as two (T as M; traced). 1 s: heuristic, longer than a letter so that P₀ is the key-down power between edges; 0.2 s read the trace's
station 0.029 against 0.010. Why a variant: on the whole of VE3NEA's conditions, the level on the floor, the bound reads worse than
the own keying's leak left as noise (f_D 3 Hz, +57.78 dB S₅₀₀: paddle 0.064 against 0.052, hand 0.088 against 0.078; our port of
his generator; DEV2's E2, seed 1's slice, 0.5237 against 0.5072), the one traced fade notwithstanding. Measured, not understood
(conjecture: at strong levels a noise 25 to 30 dB under the carrier is a tolerance the model lacks under fast fading, below,
"Short deep fades").

**Provenance.** X by measurement on a slice of the test set, at a hold of 1 s (E2 0.504 at +3 dB, 0.573 at +6 dB, 0.690 at +10 dB, against 0.575 without; B2 rises from +6 dB; review 5, the results record). The hold, 0.2 s: a heuristic chosen by measurement, looking at E2 and the strong pairs, so not independent of them; seed 2 agrees. Against 1 s, the first try's value, never swept: E2 0.490 against 0.504, seed 2 0.651 against 0.673, a neighbor at least 6 dB re wanted at 60 to 120 Hz 0.129 against 0.283 (no floor reads 0.179); at 25 to 60 Hz 0.512 against 0.506; the contest's true calls 219 against 221. 0.1 s and 0.3 s read about the same as 0.2 s; X was not swept again at this hold, though X 0 dB gave the best pairs at 1 s (review 6). Why it helps (conjecture): in a neighbor's gaps there is no leak to excuse, so holding the floor over them only blinds the stream. 60 Hz: heuristic, past a Hann main lobe's 47 Hz. 320 Hz: heuristic; the response at 300 Hz is under −100 dB re 0 Hz (derived). Tried and refused: a floor at −15 dB re the channel's peak over 5 s or 1 s, which reads pairs better still but loses fading (B2 0.231 to 0.543); a Hann taper on the narrow filter, which undoes the gain. The value is set where E2 can see it, and E2 holds its wanted stations at 8 to 14 dB S₅₀₀; that the gain on the level pairs is the larger one is shown only by those pairs, not by any group of the test set (backlog).

**Limits.** Two systematic mismatches, derived for a steady tone (review 6; the floor over the tone's true leak at the output). Inside about 60 Hz the floor stands far under the leak, −21.6 dB re the true leak at 40 Hz and m = 1, since the bins read start at 70.3 Hz: that is why 40 Hz is not cured. Near a filter's null it stands far over the leak, +34.4 dB re the true leak at 83 Hz and m = 1 and +19.1 dB re the true leak at 55 Hz and m = 3, since the sum takes the response at each bin, not at the tone's frequency, and a Hann main lobe spreads a tone over 94 Hz: that is why a 55 Hz carrier reads worse with the floor. A replay's blocks have no spectrum of their past, so the first 2 s of a channel opened by the detector run without this floor. It also reads noise that the hiss does not see, beside the channel: with the channel floor off, a passband's edge and a raised noise read CER 0.05 to 0.41 by the leak floor alone (`BenchRunFloor`); both stay, since the channel floor covers what stands inside the channel. A neighbor +10 dB re wanted or more loses the wanted station inside 100 Hz: raising σ² removes the gaps' false marks, but the belief still follows the stronger carrier. The bins read include a wide or fast station's own far skirts; a station's own keying, spread by the analyzer's window, raised its own
floor up to 40 dB re the hiss at +57.78 dB S₅₀₀ (VE3NEA's conditions; not on the test set, which stops at +20 dB S₅₀₀); with the
level on the floor it is noise only (the variant `leak_own` 1 subtracts it, and then hides a neighbor weaker than ρ_k P₀, at 70 Hz
under 10.6 dB below the wanted station's key-down, for 1 s after the wanted station's last key-down). A neighbor's
leak that steps by more than 60 dB re the floor still moves the level by the excess.

### Margins (segmental)

**A variant, not the default** (`rival_margin_nats` R; 0 by default, review 10). At 0 no runner-up is kept, every symbol says probability
1 and no correction of reason `confidence` goes out: the decoder's events are int-1's. What follows holds at R > 0; R = 16 nats is what was
measured. R above 16.6 nats is refused: a float probability below 1 holds a margin of at most ln(2^24 − 1) = 16.64 nats, and past 17.33 nats
1 / (1 + e^−R) rounds to 1, which the callsign stage reads as unknown (derived).

**Exact form.** Each mark and gap slot keeps, beside its best offer, the best of the rest at that boundary, whatever it reads (a second
table of slots, valid at its own stamp). When an element opens from a slot whose runner-up scored within R of the winner, `note_rival`
compares the two stories' texts: the winner's as opened, the runner-up's as it would have been (its parent's text, then the letter it ended
and a word space where its end was a letter's or a word's; a birth's space first), each symbol with its start and end times. Both stand at
boundary j, so they are compared from there back by time. Two symbols *agree* if their text is the same and their starts and ends lie within
max(30 ms, half the shorter's length) of each other. Of two that do not, the one that ends later (by more than 2 ms, under a sample) is
unmatched: the winner's takes the deficit d = (winner's score) − (runner-up's) as its margin if lower; the runner-up's lowers the winner's
current symbol if that one ends no earlier than 2 ms before it begins (a word space or letter the winner has not: the symbol before it), and
the walk steps past it. After the first disagreement the walk stops at three agreeing pairs in a row; it stops at once at an entry both texts
share, and after 24 steps in all. The over's end does the same between the best over's end and the runner-up among the gap slots that can end
one. The margin is kept on the shared text's entry (`SharedText::lower_margin`), the least deficit seen, so a story inherits its past's
margins. An entry no rival reached reads R, through a float probability: 15.94 nats at R = 16 (1 / (1 + e^−16) rounds to 1 − 2^−23). A
symbol's probability is p = 1 / (1 + e^−m). Published, it goes out again as a correction (reason `confidence`, the same text) whenever
⌊2m⌋ changes (`SegmentalDecoder::confidence_level`, 0 to 127; 255 for p = 1), up or down: a rise comes when the best story becomes another
with the same text (W3OA 6 398 rises against 15 850 falls; review 10). Such a correction runs to the tail's end, so symbols never published
go out inside it rather than as new characters, and the bench's immediate text, which ignores corrections, misses them (W3OA 32 426
against 38 374 characters arriving as new). A letter still open at the channel's end reads 1.

**Derivation and provenance.** The soft-output Viterbi algorithm (Hagenauer and Hoeher, 1989) takes the reliability of a decision as the
least metric difference of a discarded path that disagrees with it. Here only one discarded story per slot is compared, the best of the
rest, and it often reads the winner's very text (the same letters with an element boundary a sample or two away), so nothing is recorded
while a lower rival that reads other letters is never compared. *Derived*: the form of the update. *Heuristics*: R = 16 nats, past the
15-nat beam; the match by time, its 30 ms and half-length tolerance, the 2 ms, the three agreeing pairs and the 24 steps; the half-nat
step of publication.

**Measured.** The text is the same with margins and without: every one of 28 228 scored signals of the development set's two seeds has the
same edits; every track's final text identical on the owner's file and the 2017 recordings; every call list on the generated recordings the
same (a row's frequency can move by up to 0.3 Hz, since events come at other moments). CPU at R = 16, one thread, 8 runs side by side on the
Linux machine: real30 6.69 to 7.02 ms per channel-second, live2 7.25 to 7.67, the contest 3.23 to 3.40, W3OA 6.07 to 6.52 (+5% to +7%).
Memory: the two runner-up tables, 16 B a slot each, about 1 MB more a track (live2, peak working set 335 to 430 MB at a mean of 87 tracks;
review 10). Corrections: W3OA 5 780 to 18 939, the event log 1.8 times as large; on clean generated signals more (the contest 8.2 times).

**Limitations.** One runner-up per slot overstates the margins: with the best four compared, 6 of W3OA's 1 347 whole or glued readings
reach 8 nats instead of 651 (review 10's experiment, at +28% CPU in its unoptimized form). A word space's published margin is mostly an
artifact of the walk: of the lowerings below 0.5 nat on the test's signal, 56% come from a rival with the same text whose space is a birth's,
of zero length, which `agree` does not match by its start, and 33% from a substitution of the next letter, after which the walk steps only
the winner and lowers the space both texts hold; 11% are a space in one text only (review 10). The genuine doubt is smaller than those
values suggest: at the sender's dot with no stretch a 7-dot gap scores about 3.2 nats more as a word gap than as a letter gap (lognormal
σ 0.3, medians 3 and 7 dots: (ln 7/3)² / (2 × 0.09) = 3.99 nats, less the costs, 1.161 − 0.396 nats; derived in review 10). An entry shared
by several stories takes the least deficit any of them saw, an underestimate for the best story. Births and rescues offer their first
mark's end at every fist, so stories at other speeds meet there; otherwise a rival at another speed meets the winner only at the over's
end. Scores are not probabilities (A.8e, "What a score is"), so p is a ranking, not calibrated. A margin is how close the decoder came to
another reading, not whether the station sent what it read: a call an operator sent wrong reads sure.

### The segmental decoder behind the engine

`SegmentalShared` is built once per engine and held by every channel's decoder. A decoder refuses shared parts built for another channel rate. `filter_scales` must start with 1 and rise; a scale below 1, a stretch count or step below 1, a prior outside [0, 1) (or 0 with stretches), and `gap_unit_max_s` not positive are refused. `ChannelFloor` is built once in `SegmentalShared`'s constructor only to check its settings, before a channel opens. The leak's settings are refused there too: a hold negative or not finite, offsets that do not rise from 0 Hz or above.
Settings the code refuses: `level_step_db` not positive, top not above bottom, more than 256 levels, `level_on_floor`, `level_noise_bottom` or `leak_own` other than 0 or 1, `level_noise_above_floor_max_db` or `leak_own_hold_s` negative or not finite, `level_noise_above_floor_max_db` past 255 grid steps,
`level_walk_high_nepers_per_sqrt_s` negative or not finite and, with it above 0, `level_walk_rise_to_db_s500` not above
`level_walk_rise_from_db_s500`, a jump probability outside [0, 1), a key probability outside (0, 0.5), non-whole frames or strides, a floor quantile outside (0, 1), start outside [0, window].

**Cost.** The evidence is once per stream; the search touches each open element per boundary, and that count rises near threshold, where a broad belief and more stories within the beam cost most. A dropped stream costs nothing after. Building `SegmentalShared`: about 0.1 s at any rate (measured; "The tables' channel"). CPU is stated in ms per channel-second in the decision log and the results records.

## A.9 Timing and latency

### Exact form

The rules of this section as the code implements them (moved verbatim from the body on 2026-10-05, when the body was rewritten for reading).

| Stage | Delay |
|---|---|
| Processing block (hop) | 21.33 ms |
| Detection | nothing in the first 1 s; then ~0.5–1.5 s for a new station (averaging + 0.5 s persistence) |
| Channel filter | 10.7 ms group delay |
| Envelope smoothing | ~¼ dit (τ_s) |
| Matched | 0.32 s warm-up per channel; boxcar delay (K − 1)/2 samples, 19 ms at 25 WPM |
| Envelope, Matched | a character is emitted once a > 2-dit gap follows it |
| Bank | block 21.33 ms; branch delay (N_k − 1)/(2r) = 4.3–91.7 ms (removed from character times); a character is published when its following space is classified (at the next key-down), at a new over or at the stream's end; corrections reach up to 20 s back (a resync is not bounded by it) |
| Viterbi | the whole text at `flush` (the channel's end); each sample searched as it arrives; a channel opened at the stream's start (oracle mode) does not search its first block (21.33 ms), N₀ being 0 before the first spectrum frame (a detector channel opens later, with N₀ already set); its channel is shifted to the detector's frequency of each block's start, held over the block (21.33 ms; A.7) |
| Segmental | every 0.25 s of stream the last 64 symbols compared with what is out (A.9b); the whole text at `flush`, with 17 samples of zero evidence pushed first; each sample's evidence committed 64 to 127 samples after it arrives (170.7 to 338.7 ms), a boundary searched once evidence 16 samples past it exists, so the evidence lags at most 0.387 s; the same first block and channel as viterbi; at m = 2 and 3 the lag is at most 0.43 s and 0.46 s (A.8e) |
| Track removal | the average's decay (~6–7 s at S₅₀₀ = 20 dB) plus the 10 s timeout |


Nothing beyond section 9: its delays are derived in the sections it names (A.6 for the average's decay, A.7 for the channel filter's group delay, A.8c for the bank's blocks and branch delays, A.8d for viterbi, A.8e for segmental).

## A.9b Live operation

### Exact form

**Engine (`resolved`, `Engine`).** `FrontEnd::Segmental` on the detector's path applies `apply_path_defaults` (A.6, "The segmental path's detector": `local_floor_radius_hz` 1000 Hz, `one_track_per_peak`, the finer spectrum and its rules, the DC blocker), and turns on `judge_tracks` (default true; `--no-judge` in the bench) and the replay. A sample rate whose channel rate is no whole number of the decoder's samples is refused ("this decoder needs a sample rate of 48, 96, 192, 384 or 768 kHz").

**Which decoder.** `kDefaultDecoderName` is `segmental` (the app; `engine_config_for_decoder`). The bench's own default stays Matched.

**Live text.** While a channel runs, `SegmentalDecoder::process` compares, every 0.25 s of stream, the best story's last 64 symbols with what it has published; it sends the new symbols, or a correction from the first that differs (reason `story`); only with the margins' variant also where a margin's half-nat step changed (reason `confidence`, A.8e). A letter still open stays back until it closes, or E would show on its way to I, S, H. `flush` compares the whole text. A change of mind more than 64 symbols back goes out only at the channel's end.

**Speed sent (`DecodeUpdate::wpm`; the track list's speed).** Each published symbol keeps, as sent, its own speed: for a letter with a code, w = 1.2 s × u / d (WPM), u = (dots) + 3 (dashes) + (elements − 1), d = `end_s` − `start_s`, the search's claimed rise of its first mark to the claimed fall of its last (`SegmentalDecoder::letter_wpm`); 0 for a word space, a symbol with no code ("*") and d ≤ 0. Every update carries the median of the last 16 nonzero values of the list as it then stands (the mean of the middle two for an even count; fewer while fewer exist; 0 before the first letter), corrections included (`text_wpm`). So a speed changes only with the text, and between transmissions it holds. u counts marks and element gaps only, so w is a character speed, as the story's fist's T_s: a Farnsworth sender reads at the character speed. `TrackTable` keeps the latest nonzero speed of a row's events. The best story's own speed at the instant (its last element's T_s) is not sent: in a silence it is whatever slow junk story leads.

**Worker pool.** `WorkerPool`: `worker_threads` threads, the caller's included. For one block every channel's job (replay if due, cut, follower, decoder) reads the block's spectrum, the kept spectra, the hiss estimator and the configuration, and writes only its own `Channel`. Results go out afterward on the pipeline's thread, by track id. The verdicts and the budget follow on that thread.

**Linear power.** `SpectrumFrame::power_fs2` is the frame's power in FS², 10^(`power_db`/10) but for rounding. The detector's average and the hiss read it; only peaks that clear the threshold are converted to dB. `power_db` itself comes from the table logarithm (A.8e), the analyzer's largest cost by the C library's. On segmental's path the detector reads its own analyzer's frames, which carry `power_fs2` only (`SpectrumAnalyzer`'s `decibels` false), and `floor_every_frames` is 2: the median, a selection over 4N bins, every 171 ms, against an average of time constant 1 s.

**Local floor.** At a new peak in bin i, `average` within ±1000 Hz: `nth_element` at a quarter of the bins (the lower quartile; 171 bins either side at 5.86 Hz). The peak must stand `threshold_db` (7 dB) above it, besides above the band's median. Only a new track is tested; a live one is not.

**One track per peak.** In `follow_peaks` a track skips a peak within `attribution_distance_hz` of an older track. After following, a younger track within that distance of an older one dies, as `TrackEnd::Merged`.

**DC.** The DC blocker (A.6) takes a radio's offset out; `dc_guard_hz` is 0 on segmental's path (a candidate peak with |f| < `dc_guard_hz` would be skipped).

**Replay.** `Channelizer::keep_past(⌈replay_s / hop time⌉ + 1)` keeps each block's whole spectrum, reusing the oldest's memory: 94 past blocks and the current one at 2 s, 2.005 s of past, 95 × N × 8 bytes (6.2 MB at N = 8192, 24.9 MB at N = 32768). Each block a turn accrues, `replay_tracks_per_s` × hop time = 2.13, saved up to that plus 1. A channel opened and without a turn is not cut or decoded, is not judged and counts no channel-seconds. Given a turn, it decodes all kept past blocks in order (its bin and follower as for a live block, at the detector's current frequency, N₀ read now) and the current block; its updates go out in order before the block's. At the stream's end a waiting channel replays the kept blocks. A channel granted its turn while a budget is in force skips the replay and goes live.

`EngineStats::channel_seconds` counts live time only; `replayed_channel_seconds` the replays'; their CPU stays in the stages, so CPU per channel-second includes it. `longest_replay_wait_s` reports the wait.

**Judge (`TrackJudge`, once per second of stream per channel, from `judge_channels`).** With the history of `KeyingEvidence` a second apart, and age the number of readings less one:

- R = (ΔS − max(0, ΔF)) / min(age, 12 s), E = ΔS / min(age, 12 s), ΔS and ΔF over 12 s; S the widest stream's best score, F that stream's two-state chain at 5 switches/s on L1, whichever stream leads (`keying_evidence`). The text sums read the published text, the leader's.
- tiny, garbage: running sums, each floored at −12 nats, with lowest tiny and highest garbage; word spaces skipped; `RunningTextEvidence` adds only new symbols and on a correction goes back to the nearest saved state (every 256 symbols). A symbol the prior lacks counts at the prior's floor, 0.002.
- proven, once true for good: lowest tiny ≤ −6 nats and highest garbage < +6 nats.
- Before the rules, on the segmental path: a copy of a neighbor (A.6, "Copies of a neighbor"): `Copy`, no hold.
- Rule 1, age ≥ 4 s, R < 3 and (tiny ≥ +8 or garbage ≥ +6): `NotMorse`, hold 8 s. Rule 2, age ≥ 12 s, R < −8, and (not proven, or E ≥ 1000 and −R > 10⁻³ E): `NotMorse`, hold 8 s. Rule 3, not proven and R < 3: age ≥ 8 s and ΔS over 8 s < 40 nats: `Quiet`, hold 8 s; else age ≥ 14 s: `NotMorse`, hold 8 s. Rule 4, proven, age ≥ 30 s and ΔS over 30 s < 30 nats: `Quiet`, no hold.

**Holds (`SignalDetector::retire`, `close_place`).** Every retirement removes the track; one with hold_s > 0 also holds its place. The first one within D (30 Hz) of a place among the holds kept (`first_retirement_open` 2) records the place with count 0 and no hold, and logs nothing; otherwise it closes radius D and logs (f, t). If at least 4 logged retirements in the last 60 s lie within 750 Hz of f, it also closes radius 750 Hz from 30 s. Closing: find a hold of that radius within that radius; if none, new, count 0. If count = 0 or now ≥ until, count + 1. Then until = max(until, now + min(hold_s × 2^(count−1), 120 s)). A hold is forgotten once open for 120 s. New peaks inside a running hold of either radius are skipped; a live track there stays.

**List (`TrackTable::set_plausible_only`).** A new row is hidden until `text_evidence(row).plausible()`, then listed for good. A new track within 30 Hz (`kSamePlaceHz`, the detector's D) of an ended row, listed or hidden, goes on with it after a word space, and its corrections start after the earlier tracks' symbols. Rows ended over 60 s ago leave. Past 4 256 symbols a row drops its oldest 256; the report and `--decoded` then hold a long row's end only. A row leaves out a stretch its tracks heard from a neighbor where the neighbor's listed row holds it, and a hidden row that ends as a copy with text of its own waits as a faded one (A.6, "A neighbor's text"); the report and `--decoded` hold what the rows show.

**Overflow.** `SdrplaySource::read`: when the driver's callback finds no room it sets a flag, and the next read empties the ring and counts the samples as lost. So one gap, not a click each read.

**Budget (`StreamRunner`, with a live source).** At each check, at least 1 s of stream after the last: grown = b > b_last + m, m = 0.05 s if b_last ≤ 0.25 s, else 0; g = grown ? g + 1 : 0. If b > 1 s, g ≥ 2 and live > 8: budget = max(8, ¾ live), and the engine closes the rest at its next block, ranked (proven, E) lowest first, `TrackEnd::Shed`, hold 8 s; a new track must then replace a weaker. Else if a budget is in force, b < 0.25 s and 5 s have passed since it changed: budget + 5; at `max_tracks` it goes away.

**Silence (`DeliveryWatch`).** No new sample from the radio for 5 s of wall clock: the status says so, a recording shows as recording nothing, the markers stop showing live. A recorder keeps its own ring, 10 s; its file is valid at any stop.

### Provenance

Decisions and their measurements: `docs/decision-log.md`, 2026-10-30 and 2026-10-31. Results: the results records under `docs/plans/`.

- Replay 2 s: heuristic. A keyed signal crosses the threshold within about 1 s of its start (τ = 1 s, A.6) and is then a candidate for 0.5 s. Valid while `average_s` and `birth_s` stand.
- Replay's rate, 100 tracks/s: heuristic, from CPU, set with one filter width: a replay cost 2 s × at most about 25 ms per channel-second (weak or wide real channels, 192 kHz, this desktop), 50 ms, so at most 5 CPU-seconds per second of stream, near 4 threads' capacity, in the worst burst. Two streams and the channel's age raise it about the same. Measured at `app-6`'s code: 400 stations at 768 kHz on 4 threads, as a file, 14.9 s of wall for 25 s of stream, longest read 0.21 s, which the ring's 5.46 s absorbs; on the stand-in radio no samples lost, no budget. The burst is short: 100 tracks/s has a maximum of 4 s at the cap of 400.
- Local floor's ±1 kHz: heuristic, wide against a station's few bins. Lower quartile: heuristic, set on 62 s of real 40 m.
- DC guard, removed (det-1): ±47 Hz had kept the radio's offset, read as `0 TOO0`, from opening tracks, and every station within 47 Hz of the center with it; the DC blocker takes the offset out instead (A.6).
- W, 5 switches/s, 4, 8, 14, 30 s, 3, 1, 5 nats/s: heuristics by measurement on one 62 s recording and generated bands.
- −8 nats/s: heuristic. Not a margin below real stations: on a 30 min recording, 9 of 57 tracks at real stations' places fell below it over a full window; `held_to_it` and the text test keep them.
- E ≥ 1000 nats/s, 10⁻³: heuristics from generated strong stations.
- Text's q: fitted to 4 968 symbols of real junk (one recording), raised for T; 0.03 to the rest. −6, +8, +6 nats: Wald's bound gives P(reach +x under p) ≤ e^(−x) and P(reach −x under q) ≤ e^(−x) (derived for independent symbols from the models). As error rates on the air they are not valid: real junk is neither independent nor from q. They are heuristics.
- Holds 8, 30, 120 s, 60 s, 750 Hz, 4: heuristics on real 40 m.
- List's 60 s, 30 Hz: heuristics, the second the detector's D. 4 000 symbols: heuristic, 27 min at 30 WPM.
- Speed's formula: derived from PARIS (a dot 1.2 s / WPM) and the code's element lengths; valid while the claimed edges are unbiased: a clean 40 WPM station at +20 dB S₅₀₀ reads 39.92 to 40.10 WPM (measured, `SegmentalSpeed` test). 16 letters: heuristic, about 4 s at 40 WPM; a median, so with up to 7 junk letters among the 16 it stays within the range of the other letters' own speeds (derived).
- First retirement open: heuristic, chosen by measurement (A.6).
- Budget's values: heuristics. 5.46 s is derived: 2^k ≥ 4 s × rate, at each radio rate 2^k / rate = 5.46 s. Cap 400: heuristic; a 300-station generated band needs over 300 and the project's goal names 500 tracks on half the desktop.
- 7031.5 kHz, the default center: heuristic. Chosen to keep the DC guard off the 7030 kHz calling frequency; kept with the guard gone, since a radio's raised noise near its center opens short tracks there (A.6). No row within 47 Hz of it in the 30 min recording.

### Limitations

- A row's speed is its last 16 letters': where those are a caller's on the runner's frequency, or a junk row's, it is theirs.
- Text sent during a hold is lost, not late. Short overs and a carrier before a CQ can lose some; a runner with callers within 30 Hz can read as not keyed and retire (backlog).
- A hold can close a place on a real station up to 120 s; ±750 Hz up to 120 s if a wide signal returns.
- Rows of data and speech read as plausible stay on the track list: it is 35% to 63% junk. The call list beside it (section 11) has none of them; a view of the rows by plausibility is not built.
- A steady carrier that came first takes a station within 30 Hz of it. Beside one 0 to 10 dB re wanted stronger, at 45 to 90 Hz, a station reads CER 0.33 to 0.64 on the detector's path (0.98 to 0.99 before det-1); 10 to 20 dB stronger, 0.94 to 0.97 (det-1's cells, A.6). A carrier alone costs a few hidden T's and up to 14 s of a channel, then holds.
- The narrower stream's CPU is spent to at least 10 s of a channel's age, even on a channel the judge then retires.
- Replay near a recent neighbor reads what the channel holds, including its neighbor. A wait over about 0.5 s loses pre-roll, at the stream's start the least.
- For viterbi and bank the budget's rank is equal for all, so it sheds the oldest first.
- A replay's first block starts with its filter and level empty, as any channel.
- The recorder does not flush to the disk; an operating-system crash can lose what the system holds.

## A.10 Parameters at a glance

| Parameter | Value | Where | Status |
|---|---|---|---|
| Input scale | b-bit integers × 2^−(b−1); 32-bit floats as stored | wav_reader.cpp | standard choice |
| Input rate | file's if 48 000 × 2^k samples/s (k = 0–4) or below 48 kHz; else the largest of those below, by L/M ≤ 4 096 | `decoder_rate_for`, `RationalResampler::kMaxFactor` | derived (the decoder's rates); the 4 096 heuristic |
| Resampler filter | Kaiser sinc, stopband from half the lower rate, transition (10/192) × the lower rate below it (10 kHz at 192 kHz: pass band to 86 kHz, −6 dB re the passband at 91 kHz), design value 82 dB re input for at least 80 dB re input (82.0 dB re input measured); 129 taps per phase at 96/125, delay 257.979 µs | `RationalResampler::kTransitionRatio`, `kAttenuationDb` | heuristic (Kaiser's formulas sourced; the transition chosen against CPU; 82 measured to meet 80) |
| Center from a file name | span edges when 0.8–1.001 fs apart, else the first `<f>Hz/kHz/MHz` word that is not fs | `center_hz_from_file_name` | heuristic |
| Listening filter (`kz4ap-listen`) | Kaiser sinc on the complex baseband, width B = 500 Hz at −6 dB re the passband, transition 0.4 B: passband ±0.3 B (150 Hz), stopband from ±0.7 B (350 Hz), at least 80 dB re the passband (design value 82 dB re the passband); 311 taps at 12 kHz, 209 at 8 kHz | `listen_filter_taps`, `kListenTransitionRatio`, `kListenAttenuationDb` | heuristic (B, the transition; Kaiser's formulas sourced; 80 dB re the passband tested) |
| Listening tone, rate, level (`kz4ap-listen`) | 600 Hz; 12 000 samples/s (or 8000); one gain per clip, its peak to −1 dBFS | `ListenConfig`; `kPeakDbfs` (listen_main.cpp) | heuristic |
| Listening filter as a receiver's (`kz4ap-listen --filter-center-hz`) | centered on that audio frequency instead of the tone, transitions 200 Hz wide (2400 Hz at 1500 Hz: −6 dB re the passband at 300 and 2700 Hz, within 0.01 dB of it from 400 to 2600 Hz) | `ListenConfig::filter_center_hz`, `kReceiverTransitionHz` | heuristic |
| Receiver audio's rate | 48 000 complex samples/s | `kAudioEngineRate` | derived (the decoders' lowest rate) |
| Analytic filter | h(t) = w(t)[sinc t + j(1 − cos πt)/(πt)], Kaiser β = 8.078, 1 240 taps, transitions 200 Hz wide at 0 Hz and 24 kHz, design value 82 dB re the passband; center D + μ, μ the resampler's fraction of a sample; overlap-save, FFT 4 096 | `analytic_filter_taps`, `kAnalyticTransitionHz`, `kAnalyticAttenuationDb`, `AudioAnalytic` | form standard (sourced); Δ, A and the FFT size heuristic; the delay's removal derived |
| Receiver's passband | 300–3000 Hz unless `--passband-hz`; bounds the detector's floor, peaks and local floor and the hiss estimator's bins | `kAudioPassbandLoHz`, `kAudioPassbandHiHz`; `EngineConfig::passband_lo_hz`, `passband_hi_hz` | heuristic |
| CW pitch, sideband | 600 Hz, upper; RF = dial + (f − pitch), lower: dial − (f − pitch) | `AudioOptions` (bench) | heuristic (the pitch); the formula derived |
| Bin width Δf | ~23 Hz (rule: largest power-of-two N with Δf ≥ 20 Hz) | `choose_fft_size` (engine.cpp) | heuristic |
| Hop / overlap | N/2, 50% (largest hop keeping Hann constant-sum) | engine.cpp | standard choice |
| Spectrum window | periodic Hann (ENBW 1.5 bins = 35.2 Hz) | spectrum.cpp | standard choice |
| Power average τ | 1 s (≈ 94-frame boxcar on the band's 21.33 ms frames; ≈ 23 frames on segmental's path's 85.3 ms) | `DetectorConfig::average_s` | heuristic |
| Detection threshold / hysteresis, band's spectrum (not segmental's path) | 6 dB / 3 dB SNR per bin (35.2 Hz) | `DetectorConfig` | heuristic |
| Noise floor | median of all bins (receiver audio: of the passband's) | signal_detector.cpp | heuristic |
| Detection warm-up | 1 s from the first frame (equal to the average's τ) | `DetectorConfig::average_s` | heuristic |
| Persistence before a track | 0.5 s | `DetectorConfig::birth_s` | heuristic |
| Candidate tracking, band's spectrum (not segmental's path) | may move ±23 Hz (1 bin at 23.4 Hz) between frames | `DetectorConfig::candidate_step_hz` | heuristic |
| Track timeout | 10 s | `DetectorConfig::death_s` | heuristic |
| Track following and attribution (Matched path) | each track follows its own peak within D_ch = 47 Hz (at least 3 dB above the floor); a new peak within D_ch of a track's current frequency belongs to it | `DetectorConfig::attribution`, `attribution_distance_hz` (set from `EngineConfig::channel_distance_hz`) | heuristic (owner decisions 2026-09-29, option 1) |
| Attribution (Envelope path) | frequency fixed at birth; peaks less than 3 bins (70 Hz) from a track's bin belong to it | `DetectorConfig::min_separation_bins` | heuristic (milestone 1) |
| Peak neighborhood, band's spectrum (not segmental's path) | ±47 Hz (±2 bins at 23.4 Hz) | `DetectorConfig::peak_radius_hz` | heuristic |
| Track level neighborhood, band's spectrum (not segmental's path) | ±23 Hz (±1 bin at 23.4 Hz) | `DetectorConfig::level_radius_hz` | heuristic |
| Tracker anchor (Matched path) | the detector's current frequency for the track, set before every channel block; the tracker fine-tunes within ±12 Hz of it | `Decoder::set_frequency_anchor_hz`; `FrequencyTrackerConfig::fine_tune_hz` | heuristic (owner decisions 2026-09-29, option 1) |
| Max tracks | 400 | `DetectorConfig::max_tracks` | heuristic |
| Channel bins / decimation | 64 / D = N ÷ 64 (r = 1500 samples/s) | `EngineConfig::channel_bins` | heuristic within derived bounds |
| Channel filter cutoff | ±150 Hz (−6 dB relative to the passband) | `EngineConfig::channel_cutoff_hz` | heuristic |
| Channel filter | Blackman-windowed sinc, N/2+1 taps (21.3 ms) | channelizer.cpp | standard choice; length derived from the hop |
| Cutoff limit | cutoff + ½·5.5·fs/taps ≤ r/2 | channelizer.cpp | derived |
| Following the detector (viterbi and segmental only) | the channel's bin moves, from the next block, to the bin nearest f_det when \|f_det − f_bin\| > Δf/2 = 11.71875 Hz; the block is shifted by −(f_det − f_bin), f_det held over the 32-sample block (21.33 ms); at a move the shift's phase gains (f₁ − f₂)(t₀ − D_g), D_g = 1/(4Δf) = 10.667 ms | `ChannelFollower`, `Channelizer::set_center_bin`; engine.cpp | half a bin derived; the hold per block the plan's (O3: the detector's update interval); D_g derived (the channel filter's group delay); for these decoders only, a ruling (plan O2) |
| Oracle frequency (viterbi, segmental) | the label's: f = frequency_hz + drift_hz_per_s·(t − drift_origin_s) of the segment in force at the block's start; one segment for a single station, one per transmission of a QSO at freq_offset_hz + the sender's offset_hz | `EngineConfig::oracle_frequency_tracks`, `OracleSegment`; `oracle_frequency_track` (bench) | the generator's own frequency law (A.11, "Oracle frequency") |
| Envelope smoothing | ¼ dit | `ClassicalDecoderConfig::smoothing_dits` | heuristic |
| Level attack / decay | 4 ms / 3 s | `ClassicalDecoderConfig` | heuristic, test-confirmed |
| Squelch | mark ≥ 3 × space | `ClassicalDecoderConfig::squelch_ratio` | heuristic, test-confirmed |
| Key thresholds | 60% down / 40% up | classical_decoder.cpp | heuristic |
| Glitch limit | 0.3 dit | `ClassicalDecoderConfig::glitch_dits` | heuristic |
| Dit/dah boundary and width | 2 dits, 0.08 (log) | classical_decoder.cpp | heuristic |
| Character / word gap | > 2 / > 5 dits | classical_decoder.cpp | heuristic (the conventional midpoints between the nominal 1, 3 and 7 dits) |
| Speed window / range | 24 marks, 5–60 WPM | classical_decoder.cpp | heuristic |
| Edge-shortening ratio band | 3.0–3.85 | classical_decoder.cpp | measured |
| Decoder | Matched (the bench's default), Envelope, the bank, viterbi or segmental | `ClassicalDecoderConfig::front_end` (`FrontEnd::Bank` makes the engine create a `BankDecoder`, `FrontEnd::Viterbi` a `ViterbiDecoder`, `FrontEnd::Segmental` a `SegmentalDecoder`); `kz4ap-bench --decoder` (`--front-end` its old alias) | Matched default and Envelope owner decisions 2026-09-29; the bank selectable (owner decision D1, 2026-10-03); viterbi selectable (the first prototype's plan, D2); segmental selectable (decision log, 2026-10-30) |
| LLR keying hysteresis (Matched) | g > +1 nat down, g < −1 nat up; the key-up threshold, −1 nat, is also the evidence that a mark's start was observed (row "Marks not counted for speed", Task 16) | `ClassicalDecoderConfig::llr_hysteresis` | heuristic |
| Filter follows speed after (Matched) | 8 marks in the speed window (8 new ones after a re-acquisition) | `ClassicalDecoderConfig::follow_after_marks` | heuristic |
| Dit-estimate growth bound (Matched) | decided: at most ×1.25 per mark while the filter follows the speed, and the filter's own dit at most ×1.25 per mark from its first follow step (from the 20 ms acquisition dit). Applied at each key-up counted for speed; a dropout merge restores the state before the merged mark's update, so each physical mark is bounded once (Task 15) | `ClassicalDecoderConfig::max_dit_growth` | heuristic value (owner decisions 2026-09-29); applied per physical mark (derived from the code, Task 15; re-measured in Task 17: 2a record, sections 3.7, 3.8 and 8.3) |
| Marks not counted for speed (Matched) | a mark whose key-down came with no keyable key-up sample (log-odds below −1 nat) seen since the last sample on which keying was impossible (warm-up, squelch closed) | `ClassicalDecoder::step_matched`, `key_up_seen_` | derived rule, no new parameter: its threshold is the key-up hysteresis, −1 nat (Task 16; measured in Task 17: 2a record, sections 3.8 and 8.3) |
| Re-acquisition (Matched) | after max(0.5 s, 12 dits) of key-up: filter back to 60 WPM, ŝ, the frequency average and the speed window restart; the old speed window and the narrow filter come back if nothing is keyed within 2 s | `ClassicalDecoderConfig::reacquire_after_dits`, `reacquire_min_s`, `reacquire_window_s` | heuristic |
| Matched filter | boxcar, K = round(0.8·dit·r), starts at 60 WPM (16 ms, K = 24), clamped to 16–192 ms (60–5 WPM; K = 24–288) | `MatchedFrontEndConfig` | derived shape; β and start heuristic |
| Likelihood | Λ = −a²/2 + ln I₀(a·x); prior P₁ = 0.44 | matched_front_end.cpp | derived |
| Amplitude / noise estimates | τ_a = 0.5 s (EM, p-weighted) / τ_n = 2 s (middle tap below κ = 1.75, neighbors below κ_n = 4, truncation mean 0.632 divided out) | `MatchedFrontEndConfig` | heuristic; the truncation correction derived |
| Noise floor | 10th percentile of 64 samples of \|v\|² taken K apart, over 2·(−ln(1 − 0.1/0.25))·2.5; a lift caps W_n at 10.67 ms of samples and restarts ŝ only if the floor exceeds 4·σ̂_v² | `MatchedFrontEndConfig::floor_*` | heuristic; the occupancy bound derived, c = 0.25 from computed clean fractions of continuous text |
| Matched warm-up | 0.32 s at K = 24; 20th / 90th percentiles, weight 0.1 × its length | `MatchedFrontEndConfig::warmup_s` | heuristic |
| Matched squelch | a ≥ 3·(T_v/16 ms)^(1/4), T_v = K/r the filter duration (3·(K/24)^(1/4) at r = 1500 samples/s) | `MatchedFrontEndConfig::squelch_a`, `squelch_exponent` | 3 heuristic; the duration scaling derived |
| Frequency discriminator lag | τ_L = 8/r = 5.333 ms (`lag_s` = 5.33 ms rounded to whole samples at r = 1500 samples/s); unambiguous range ±1/(2τ_L) = ±93.75 Hz | `FrequencyTrackerConfig::lag_s` | heuristic within derived range |
| Frequency average | τ_f = 0.5 s of key-down weight; moves the NCO at weight ≥ 0.6 and coherence > 0.3, every 21.3 ms | `FrequencyTrackerConfig` (`tau_s`, `min_weight`, `min_coherence`, `update_interval_s`) | heuristic; the resulting accuracy measured (median 0.11 Hz at S₅₀₀ = 5 dB, 0.13 Hz at S₅₀₀ = 0 dB, group F; A.7) |
| Fine-tuning range | ±12 Hz around the anchor (the detector's frequency for the track); farther estimates are discarded; the NCO jumps to an anchor more than 12 Hz away | `FrequencyTrackerConfig::fine_tune_hz` | heuristic (owner decision 2026-09-29, option 1) |
| NCO range | ±75 Hz | `FrequencyTrackerConfig::max_offset_hz` | heuristic |
| Bank ladder (decoder: bank) | L_k = 9.6 ms × 1.1^(k−1), k = 1…32 (9.6 to 184.3 ms): 0.8 dit at 100 to 5 words/min; ratio 1.1 | `BankConfig::min_wpm`, `max_wpm`, `ladder_step`, `length_dits` | WPM range and ratio owner; length 0.8 dit heuristic |
| Bank branch filter | boxcar, N_k = round(L_k · r) samples (14 to 276 at r = 1500 samples/s), zeros before the stream | `bank::branch_samples`, `bank::boxcar` | derived from the ladder |
| Bank envelope likelihood | Λ = −a²/2 + ln I₀(a·x) nats; p = logistic(g), g clipped to ±50 nats | `kz4ap::envelope_llr` (shared with Matched), `bank::logistic` | derived; clip a numerical choice |
| Bank noise method | "spectrum": branch 1's three-tap level × spectrum ratios (variant (a)); "spectrum-level" (variant (b)) and "branch" (per-branch three-tap fallback) selectable | `BankConfig::noise_method`, `bank::make_noise` | measured (prototype E10) |
| Bank three-tap noise level | κ = 1.75, κ_n = 4, τ_n = 2 s, truncation mean m(κ) = 0.632 divided out; warm-up 0.32 s of non-zero input, 20% quantile (numpy "linear") / (2·(−ln 0.8)); floor 10⁻²⁰ FS² (−200 dBFS) | `BankConfig::noise_guard`, `neighbor_guard`, `noise_tau_s`, `noise_warmup_s`; `bank::ThreeTapNoise` | heuristic (Matched's, milestone 2); m(κ) and the warm-up scale derived; floor a numerical choice |
| Bank noise: exact zeros | an exact-zero input sample enters no warm-up, recovery history or count (an all-zero block updates nothing); taps count from 2N_k samples after the sample following the latest exact zero; an all-zero spectrum segment is not offered, an exact-zero sample is left out of the mask; σ² unknown (nothing keyed or published) until the first non-zero block | `bank::ThreeTapNoise`, `bank::SpectrumNoise`, `bank::BankChannel` | derived (a receiver's output carries noise: zeros are missing data; Plan B, B3) |
| Bank noise: stuck-level recovery | when branch 1 has accepted no tap for 8 s (4 τ_n) of non-zero input, every branch's σ² is set again by the warm-up rule over its last 0.32 s of non-zero input | `BankConfig::noise_stuck_s`; `bank::ThreeTapNoise` | heuristic (8 s, seconds because the noise has no keying speed); the gate on branch 1 derived for rectangular keying (a tap needs 3N_1 = 42 samples, a character space at 100 words/min is 54) and measured (no firing on the development set); Plan B, B3 |
| Bank noise spectrum | segments T_seg = 256/1500 s = 170.7 ms (M = 256 samples, bins 5.86 Hz at 1500 samples/s), periodic Hann; exponential average τ_n = 2 s (β = 0.0818 per segment); smoothed ±25 Hz (±4 bins); W_k from 16 points per bin | `BankConfig::segment_s`, `spectrum_smoothing_hz`; `bank::SpectrumNoise` | heuristic; 16 points a numerical choice |
| Bank spectrum mask | a sample is left out if \|v_1\|² ≥ κ_n·2σ²_v,1 anywhere from g before it to (N_1 − 1)/r + g after it, guard margin g = 0.5·L_1 = 4.8 ms (7 samples at 1500 samples/s; stage 1: 20 ms); a segment enters if ≥ 50% is left in | `BankConfig::guard_margin_s`, `min_clean_fraction` | heuristic (g: owner, stage-2 spec section 3.3, Plan B task B4b; class: seconds tied to branch 1's filter) |
| Bank mask bias b_mask,k | 0.8381 (k = 1) … 0.7812 (k = 32), dimensionless; divides each branch's spectrum reading (stage 1's 0.8370 … 0.7852 at 20 ms) | `BankConfig::mask_bias` | measured (Plan B B4b, white noise, 200 seeds: 1001–1200, 60 s each; adopted by B4d; valid only for the defaults at 1500 samples/s) |
| Bank keying log-odds and hysteresis | g = Λ + ln(P₁/(1 − P₁)) nats, P₁ = 0.44 (−0.2412 nats); down at g > +1 nat, up at g < −1 nat | `BankConfig::prior_key_down`, `hysteresis_nats`; `bank::BankKeyer::step` | P₁ derived (PARIS: key-down 22 of 50 dit units); h heuristic |
| Bank squelch | a_k ≥ a_min,k = 3·(L_k/16 ms)^(1/4) (2.622 at k = 1 to 5.525 at k = 32), dimensionless | `BankConfig::squelch_a`, `squelch_ref_s`, `squelch_exponent` | 3 heuristic; L^(1/4) scaling derived |
| Bank amplitude EM | τ_a = 0.5 s of key-down weight (α = 1.332·10⁻³ per sample at 1500 samples/s), p-weighted, one step per block | `BankConfig::amplitude_tau_s`; `bank::BankKeyer` | heuristic (heuristic running form of an EM update, as Matched) |
| Bank unknown-amplitude test | key down at x > x_on,k (4.6428 at k = 1 … 4.2036 at k = 32, dimensionless), up at x < x_off = √(−2 ln 0.3) = 1.552; target R_fa = 0.01 false marks/s per branch | `BankConfig::x_on_values`, `release_probability`, `false_marks_per_s` | x_on measured (E9a); release probability heuristic; R_fa heuristic target kept by E9b |
| Bank amplitude seed and re-key | while unknown: ŝ² = 90% quantile (by selection, bit for bit the sorted form; B4f) of the last ≤ 4 × W_min of keyed \|v\|² − 2σ², FS²; ready to re-key after W_min = 0.8 s of keyed time on every branch (memory 3.2 s); `rekey_after_s` must be positive (0 s, which once selected the removed variants, is refused); class: seconds (stage 1's measured 0.8 s, E9b) | `BankConfig::rekey_after_s` (0.8), `seed_memory_rekeys`; `bank::check_rekey_times`, `bank::BankKeyer`, `bank::rekey` | 0.8 s measured (E9b, stage 1; the default again since Plan B's B4g, owner 2026-10-06); memory and 90% quantile heuristic |
| Bank re-key wait, variants (removed) | in dits, 16.7 d_k (B4a), and in marks, 8 provisional marks (B4e), with two guards (B4f); removed 2026-10-07 | — | measured: Plan B record, sections 6, 11 and 12 |
| Bank duration-fit classes | dit T + w, dah qT + w; element space T − w, character gap 3 T_g − w, word gap 7 T_g − w (s); log-normal in ln d, s_c² = σ_ln² + σ_t²/μ_c², σ_ln = 0.15 (marks), 0.25 (spaces); priors 0.5716 / 0.4284 (marks), 0.6467 / 0.2379 / 0.1154 (spaces) | `BankConfig::sigma_ln_mark`, `sigma_ln_space`; `bank::class_priors` | σ_ln heuristic; priors derived (VE3NEA tables) |
| Bank timing resolution | σ_t² = 2 (L_k / max(a, 1))² + 2 / (12 r²), s² | `bank::resolution_var_s2` | derived (first order, high SNR) |
| Bank outlier class | ε = 0.05, log-uniform on 1 ms–10 s (−5.216 nats in ln d); durations clamped to that range | `BankConfig::outlier_prior`, `outlier_range_s` | heuristic |
| Bank fit memory | N_mem = 48 marks and spaces, λ = e^(−1/48) = 0.97938; tables untruncated; refinement and quality on the last ⌈4 N_mem⌉ = 192 (tail e^(−4) = 1.8%) | `BankConfig::fit_memory`; `bank::DurationFit` | N_mem measured (E4); the 1.8% tail derived |
| Bank fit grid | T 12 ms to 242.2 ms in 1% steps (303 points); q ∈ {3, 4, 5}; w/T ∈ {−0.4, 0, 0.4, 0.8}; T_g/T ∈ {1, 1.59, 2.52, 4, 6.35} | `BankConfig::t_grid_step`, `q_grid`, `w_grid`, `tg_grid` | q, w, T_g grids measured (E5, "coarse"); T step placeholder kept by E5 |
| Bank T_P prior | −π (ln T − ln T_P)² / (2 σ_P²) nats, σ_P = 0.1 in ln T | `BankConfig::prior_sigma_ln` | heuristic |
| Bank fit refinement | 2 Gauss–Newton steps in ln d, damping 0.2 T per parameter; T ≥ 0.1 ms, w ∈ [−0.6, 1.2] T, qT ∈ [2, 6] T, T_g ∈ [0.8, 10] T; kept only if the weighted log-likelihood does not drop | `BankConfig::refine_iterations`; `bank::DurationFit::refine`, `best` | heuristic; the 0.1 ms floor a numerical choice |
| Bank fit grid constants | ln μ, 1/μ² and a validity flag per class and grid point (432.7 kB with the defaults), built once per process per configuration and shared, immutable, by every fit of a configuration with bit-identical values of the 13 fields they read; freed when no fit uses them | `bank::DurationFit::shared_model` | a memory choice with no arithmetic effect (Plan B B1: tested bit for bit across threads; development-set texts identical) |
| Bank fit evaluation | ℓ_total = m + ln(1 + Σ e^(x − m)) over the observation's kind of classes and the outlier, in one pass, the others added after the largest's 1; terms more than 40 nats below m left out, and a grid class's ln s_c² skipped when its bound (with σ_ln² for s_c²) is more than 41 nats below the outlier's −5.216 nats; logaddexp skips exp and log1p at \|x − y\| ≥ (58 − k) ln 2 nats (38.8 nats at k = 2) | `bank::DurationFit::grid_loglik`, `terms`, `log_sum_exp`, `logaddexp` | the 40 nats and the skip threshold derived (no bit changes against the one-pass sum, which starts at the largest term's 1, respectively against numpy's formula; both tested bit for bit); the one-pass sum changes ℓ_total by ≤ 1.3 · 10⁻¹⁵ nats (measured, Plan B B2(a); development-set texts and records identical) |
| Bank fit exp and ln | SLEEF 3.9.0 `_u10` (stated error bound 1.0 ulp) on arrays: grid points in blocks of 256, the retained history at once; finz (FMA) on AVX-512F (8 doubles) or AVX2 + FMA (4), else cinz on AVX-512F, AVX or SSE2 (8, 4, 2), chosen at run time; changes ℓ_total by ≤ 1.001 · 2^−52 · (5 \|ℓ_total\| + 14.3 + \|ln σ_ln²\|) nats against the C library's | `bank::vecmath` (`engine/src/bank/vecmath*.cpp`); `bank::DurationFit::grid_loglik`, `terms`, `refine` | the bound derived (1 ulp per call through the formula, assuming the C library's exp and ln within 1 ulp as SLEEF's; tested on the sweeps); the implementation order measured (Plan B B2(b)); the block of 256 a heuristic (stack scratch of 25 856 B, no measured effect claimed) |
| Bank periodicity method | the comb on Π = 2T over branch 1's posterior p (the edge comb and the spectrum fit are not ported) | `BankConfig::periodicity_method`; `bank::Periodicity` | owner (E1); Π = 2T derived |
| Bank periodicity input and updates | p averaged to r_P = r / max(1, round(r / 750 samples/s)) (750 samples/s at r = 1500 samples/s); recomputed every 0.25 s of p (375 samples at 1500 samples/s) | `BankConfig::periodicity_rate_hz`, `periodicity_update_s` | heuristic |
| Bank periodicity windows | 5 and 10 s (3750 and 7500 samples at 750 samples/s), the same for every candidate τ_c; the shortest confident window gives T_P; the comb's reach τ_c ≤ W/18.3 (273 ms, 4.4 words/min, in the 5 s window), so every candidate is in reach of both; class (2) seconds, a latency after a change; stage 1's 2, 5 and 10 s with `--set periodicity_windows_s=[2,5,10]` | `BankConfig::periodicity_windows_s` (`kStage1PeriodicityWindowsS`); `bank::Periodicity` | values placeholder (stage 1's, E2) less the 2 s window, dropped on a measured failure below 10 words/min (owner, 2026-10-07); seconds the owner's decision (2026-10-06; Plan B, B4d); reach derived |
| Bank periodicity windows, variants (removed) | per candidate, N_w · τ_c (B4a), and one per row following the selected branch's dit, N_w · T̂ (B4a-C); removed 2026-10-07 | — | measured: Plan B record, sections 6 and 6.6 |
| Bank comb teeth and width | 4 teeth at kΠ, k = 1…4, negative teeth at (k ± ½)Π, each ±0.075 Π (±15% of T) wide; score = mean contrast, dimensionless; biased autocorrelation | `BankConfig::comb_teeth`, `comb_width`; `bank::comb_estimate` | placeholder (E3), not measured for the comb on 2T; biased estimate heuristic |
| Bank comb confidence | T_P counts when its window's score ≥ 0.03 (dimensionless) | `BankConfig::comb_confidence_min` | placeholder (E1) |
| Bank text model | ln(w / 2688) nats per character (VE3NEA's table); valid codes missing from it ln(8/2688) = −5.817 nats; "*" ln(10⁻⁶) = −13.816 nats; mean over the branch's last 10 characters (word spaces not counted), applied by the channel decoder | `bank::TextModel`; `BankConfig::text_window_chars`; `bank::Branch::text_logprob` | probabilities derived (VE3NEA); the two fallbacks heuristic; window 10 characters placeholder, kept by E8 |
| Bank eligibility | \|ln(L_k / (0.8 T_k))\| ≤ ln 1.1 = 0.0953 (one ladder step), fit weight ≥ 8 elements | `BankConfig::eligibility_tolerance`, `min_fit_weight`; `bank::Selector::eligible` | heuristic |
| Bank selection ties | quality tie ε_Q = 0.05 nats per element; then text tie 0.1 nats per character; then the longest branch. None eligible: text leading by ≥ 1.0 nats per character, else nearest 0.8 T_P, else branch 1 | `BankConfig::quality_tie_nats`, `text_tie_nats`, `text_separation_nats`; `bank::Selector::best` | ε_Q placeholder, kept by E8; text tie and separation heuristic |
| Bank switch persistence | M = 4 selection instants in a row, of one kind (eligible or fallback) | `BankConfig::switch_persistence`; `bank::Selector::update` | placeholder, kept by E6 |
| Bank block cadence | every stage advances once per block of round(block_s · r) samples, block_s = 32/1500 s = 21.33 ms (32 samples at 1500 samples/s); input in pieces of any length, a partial last block at the end | `BankConfig::block_s`; `bank::BankChannel::push`, `finish` | heuristic (the engine's channel block) |
| Bank stored power | \|v_k\|² rounded to float32 as each sample arrives (as the prototype stores P), FS²; the channel's window stores it as a 4-byte float (lossless) and every read widens it to double exactly | `bank::boxcar_power_f32`; `bank::PowerMatrix`, `bank::BankChannel` | rounding a numerical choice (the prototype's, reproduced); the float storage a memory choice with no arithmetic effect (Plan B B1: development-set texts identical) |
| Bank new over | key up longer than T_new = max(0.5 s, 12 · T_g) since the branch's last key-up (2.88 s without a fit; 0.576 s at 25 words/min) | `BankConfig::new_over_min_s`, `new_over_gaps`; `bank::Branch::new_over_due` | placeholders, kept by E7 |
| Bank re-key and time-out | the over's start re-keyed with the full LLR once the over has a mark and 0.8 s of keyed time, at the seed s² and the previous over's s² (best mean log-likelihood per element wins), over the stretch (at most 20 s back); if that is not reached within 2 s of channel time from the amplitude becoming unknown (every branch), re-keyed at the previous over's s², or its provisional characters deleted; both `rekey_after_s` and `rekey_timeout_s` must be positive (0 s, which once selected the removed variants, is refused); class: seconds | `BankConfig::rekey_after_s` (0.8), `rekey_timeout_s` (2.0); `bank::check_rekey_times`, `bank::Branch::rekey_over`, `clear_over` | stage 1's (wait measured, E9b; time-out heuristic; the default again since Plan B's B4g, owner, 2026-10-06, option a′); candidate choice heuristic |
| Bank re-key clocks | stage 1's: the time-out counts and the stretch starts where the amplitude became unknown; a cleared time-out restarts only the count | `bank::BankChannel` | heuristic (stage 1) |
| Bank re-key clocks, variant (removed) | the count from the branch's first provisional mark, the stretch's lead 7 · d_k, a cleared time-out moving the stretch (B4d); removed 2026-10-07 | — | measured: Plan B record, section 10 |
| Bank fresh fit against the previous | the over's fresh fit replaces the previous over's continued fit with ≥ 8 of the over's marks and spaces and a log-likelihood gain > ½ · 4 · ln n nats on them (4.16 nats at n = 8); the competition ends after 192 (⌈4 N_mem⌉) | `BankConfig::fresh_fit_min_obs`; `bank::kFitParameters`; `bank::Branch` | 8 a placeholder (heuristic); ½ k ln n form derived (BIC), k = 4 heuristic; the end at 4 N_mem derived (e⁻⁴ = 1.8%) |
| Bank corrections | a replacement at t changes nothing that starts before t − 20 s; overlap cut at max(from, t − 20 s); recorded only if the text differs, with its kept-character count | `BankConfig::correction_reach_s`; `bank::Output::replace_from` | 20 s owner; overlap cut heuristic |
| Bank switch replacement | from the start of the new branch's character containing the time its eligible run began (its own time base); a fallback pick from the switch's time | `bank::BankChannel` | heuristic (spec 4.8; the fallback rule documented behavior) |
| Bank anchor mixing (engine) | u[n] = y[n] exp(−jφ[n]), φ[n] = 2π (Σ_{m≤n} Δ[m] − Δ[n]) / r, Δ = detector frequency (oracle: the label's, without its drift) − channel center, Hz, per channel block; no tracker | `BankDecoder::process` | derived: the prototype's `anchored_baseband`, mixing at the frequency where option 1 (the detector decides where the station is) puts the station; the oracle anchor without drift is the plan's choice, not tuned |
| Bank events (engine) | new characters, then corrections (index: the smaller of the number kept and the first position whose text changed; the bank's characters from it on; time, reason, reach), applied in order, plus a "resync" correction if the consumer's list ever differs from the bank's (its reach not bounded by the 20 s correction reach; never on the suite, measured; tested by forcing one); final text = all corrections applied, immediate text = characters as first appended | `TextCorrection`, `DecodeUpdate::corrections`, `DecodedTextEvent::corrections`; bench `TrackText` | derived: the consumer's rule rebuilds the bank's final text exactly (proof in A.8c); not signal processing, an interface choice of the port (owner decision D1 for `--decoder`); the event's probability 1, speed 0 and confidence 0 are placeholders |
| Viterbi, segmental: sample interval Δ | 4/r = 2.667 ms, a whole number of channel samples | `sample_interval_s`; `whole_count` | owner (2026-10-23, D17); r refused at 44.1 and 88.2 kHz |
| Viterbi, segmental at m = 1: front filter | 25 taps, a 12 ms dot with raised-cosine edges of 2.5 ms 10–90% rise; delay 8.000 ms; σ_y²/N₀ = 74.50 Hz; w = 0.1987 | `front_dot_s`, `edge_rise_s`; `front_filter_taps`, `output_noise` | owner (D17); taps, 74.50 Hz and w derived |
| Viterbi, segmental: template | s_j, v_j, c[a][b] from the chain; rise range 0.8–4.7 ms uniform, 79 points | `edge_rise_min_s`, `edge_rise_max_s`; `make_template_tables` | owner (range: ARRL Lab, D17); values derived |
| Viterbi, segmental: speeds | dots of 12.0 ms × 1.1^s, s = 0 … 31 (100 to 5.2 WPM) | `dot_min_ms`, `speed_ratio`, `speed_count` | owner |
| Viterbi: spacing | standard only: no Farnsworth factor (segmental: gap stretch, below) | — | owner (2026-10-21, D14) |
| Viterbi, segmental: fist | log-normal about 1, 3, 1, 3 and 7 dots, σ = 0.3 in ln(length), shifts 0 dots; rounded to whole samples, at least one | `fist_medians_dots`, `fist_sigmas`, `fist_shifts_dots`; `Fist` | placeholders, heuristic (the ITU ratios); no heavy tail: owner (D5) |
| Viterbi, segmental: speed prior | 1/32 (3.466 nats) where a newborn's or rescue's speed is taken | `Search` | owner (flat) |
| Viterbi, segmental: symbol prior | ln(1/p): ln 62 = 4.127 nats at the start, ln p + ln 62 at the end; p = max(count/2 688, 0.002) / 1.042 | `symbol_prior_on_air` (true), `symbol_prior_floor` (0.002); `on_air_log_prior` | counts sourced (VE3NEA); floor heuristic; the flat prior a selectable option |
| Viterbi, segmental: ends of words and overs | p_w = 1/3.06, q = 1/24: a letter gap 0.396 nats, a word gap 1.161, an over's end 4.296 | `word_end_p`, `over_end_q` | p_w VE3NEA's mean word length, q counted (the development set); placeholders |
| Viterbi, segmental: overs from silence | 0.05 s⁻¹: P = 1.333 × 10⁻⁴ per sample (8.92 nats at a birth) | `over_rate_per_s` | placeholder |
| Viterbi, segmental: rescue | ε = 10⁻³ per letter start (6.908 nats) | `rescue_epsilon` | placeholder |
| Viterbi, segmental: hiss | each bin's one-pole 1 s average; ±2 000 Hz (±85 bins), the 25th percentile (the 43rd of 171); ÷ β; ÷ 1.5 bins; a frame of exact zeros not counted | `hiss_average_s`, `hiss_half_width_hz`, `hiss_percentile`; `HissEstimator` (for segmental, from `SegmentalConfig::model`) | 1 s heuristic (the detector's); width and rank placeholders; β, the 1.5 bins and the zero rule derived; the 0.01 dB margin above −200 dBFS heuristic |
| Viterbi, segmental: events | the whole text at `flush` (viterbi), every 0.25 s (segmental); probability 1 (segmental's margins' variant: from the margin, A.8e), speed 0 (viterbi), confidence 0; no `freq_offset_hz` | `ViterbiDecoder`, `SegmentalDecoder`, engine.cpp | placeholders |
| Viterbi: systematic floor | f = 0.02 of the carrier's amplitude, 0 to 1 | `ViterbiConfig::systematic_floor` | placeholder (D17; too small for flutter, A.8d) |
| Viterbi: fading spread | f_D = 2 Hz (a Gaussian Doppler spectrum, twice its standard deviation) | `fading_spread_hz` | owner (2026-10-22, D16) |
| Viterbi: lag-1 added variance | × m = 1 + (M − 1)x/(1 + x), x = Ω̄/(X₀σ_y²), M = 16, X₀ = 42 (+8.0 dB S₅₀₀) | `added_variance_multiplier_max`, `added_variance_level_knee`; `predict_amplitude_at_level` | heuristic, chosen on a review's slices, possibly fitted to them |
| Viterbi: amplitude update | only where \|s\| ≥ 0.01; the sample is scored regardless | `amplitude_update_min_template` | heuristic |
| Viterbi: long-run power | ν's bins 16 samples (42.67 ms), cross terms over 10 bins (426.7 ms); Ω̄'s prior flat in dB over −15 to +85 dB S₅₀₀ | `nu_bin_s`, `nu_memory_s`, `level_min_db`, `level_max_db`; `LongRun`, `OmegaBarTable` | owner (D17); range placeholder; table grid heuristic, within 0.1% measured |
| Viterbi: the evidence | adaptive Gauss–Hermite of order 5 at the mode; tables where y ≤ 4 and κ′ ≤ 2 | `quadrature_order`; `sample_evidence` | order: plan D4; bounds and steps heuristic, their accuracy measured |
| Viterbi: newborn | flat in ln a over −15 to +85 dB S₅₀₀, 128 points; live range 160 nats; hand-over at σ_u < 0.1 nepers or j = 15 after the first key-up; diffusion every 8 scored samples (21.33 ms) | `newborn_grid_points`, `newborn_live_range_nats`, `newborn_switch_sigma_u`, `newborn_fade_interval_s` | 128 heuristic with a derived bound; 160 nats heuristic; 0.1 nepers derived (owner, D17); 21.33 ms chosen by measurement (D24) |
| Viterbi: merge key | the state plus the claimed edges of the last 12 samples; a rescue's own key bit | `merge_edge_history_samples`; `kRescueKeyBit` (search.cpp) | 12 chosen by measurement; the bit heuristic |
| Viterbi: beam, ceiling | 40 nats, 1 700 stories | `ViterbiConfig::beam_nats`, `ceiling` | placeholders |
| Viterbi: genie | per channel, the true fading spread or fist | `genie_fading_spread_hz`, `ViterbiGenie`, `EngineConfig::viterbi_genie` | genie runs only (oracle mode) |
| Segmental: systematic floor | f = 0.1 of the carrier's amplitude | `fast_form_model()`, `SegmentalConfig::model.systematic_floor`; `--set systematic_floor=` | heuristic chosen by measurement (2026-10-31) |
| Segmental: floor's window | 5 s (0 s leaves the floor out) | `channel_floor_window_s` | heuristic |
| Segmental: floor's quantile | 0.10, factor to noise 1/−ln(1 − q) = 1/0.10536 | `channel_floor_quantile` | quantile heuristic; factor derived for noise alone |
| Segmental: floor's margin | −3 dB re the floor as estimated | `channel_floor_margin_db` | heuristic |
| Segmental: a neighbor's leak | σ² at least +3 dB re L's peak over 0.2 s, 9 blocks (0 s leaves it out), bins 3 to 14, 70.3 to 328.1 Hz from the center; the responses and the factor 1.5 | `leak_floor_db`, `leak_hold_s`, `leak_min_offset_hz`, `leak_max_offset_hz`; `SegmentalParts::leak_response` | +3 dB and 0.2 s heuristics chosen by measurement; the offsets heuristic; the responses and 1.5 derived |
| Segmental: the leak without the channel's own carrier (variant) | a bin counts max(0, P − ρ_k P₀); P₀ the largest center bin (\|k\| ≤ 1) held 1 s, 47 blocks; ρ_k −10.6 to −25.1 dB re key-down at k = 3 … 14 | `leak_own` (0; 1 the variant), `leak_own_hold_s` (1 s); `SegmentalParts::leak_own_bound` | ρ_k derived (computed: hard edges anywhere in the Hann window, tones within half a bin); 1 s heuristic |
| Segmental: floor's stride | every 4th sample, 10.67 ms (ρ₄ = 0.14) | `channel_floor_stride_s` | heuristic |
| Segmental: floor's refresh | every 36 samples, 96 ms | `channel_floor_refresh_s` | heuristic |
| Segmental: floor's start | once 1 s of values is held; first in at 1.056 s | `channel_floor_start_s` | heuristic |
| Segmental: level grid | −6 to +84 dB S₅₀₀ in 1.5 dB, 61 levels, re the floor F = max(hiss, channel floor); a_l²/F = S₅₀₀ × 500/74.50 | `level_min_db_s500`, `level_max_db_s500`, `level_step_db`, `level_on_floor` (1; 0: re σ²) | range and step heuristic; the ratio derived; on the floor: the cause traced on VE3NEA's conditions (A.8e) |
| Segmental: noise above the floor | σ² taken at F × 10^(1.5 d / 10), d the nearest whole steps, at most 40 (60 dB re F; past it σ² as it is); level l scored as l − d; below −6 dB S₅₀₀ re σ² as −6 | `level_noise_above_floor_max_db` (60 dB), `level_noise_bottom` (1) | 60 dB heuristic (40 dB re the hiss the most measured); the bottom heuristic, the old grid's, chosen by the engine's neighbor tests |
| Segmental: the grid in V | step²/12 = 0.002485 nepers² added to f²; f′ = 0.1117 | `level_grid_in_v` (1) | derived |
| Segmental: key's switch rate | 30 s⁻¹, 0.08 per sample | `key_switch_per_s` | heuristic chosen by measurement |
| Segmental: level's walk | 4.8 nepers/√s; per frame (0.70 nepers)²; 25 passes, p = 0.3296 | `level_walk_nepers_per_sqrt_s`; `LevelTables` | heuristic chosen by measurement; passes derived from it, the 0.33 per pass a numerical limit |
| Segmental: level-dependent walk (variant) | off (0); with W_h > 0 the walk from a level L rises linearly in dB from 4.8 nepers/√s at L₁ = +20 dB S₅₀₀ re F to W_h at L₂ = +40 dB S₅₀₀ re F, a dense kernel, its columns Gaussians folded at the ends | `level_walk_high_nepers_per_sqrt_s` (0), `level_walk_rise_from_db_s500` (+20 dB S₅₀₀), `level_walk_rise_to_db_s500` (+40 dB S₅₀₀); `LevelTables::walk_kernel` | heuristics; tried at 10 and 14 nepers/√s, not adopted |
| Segmental: level's jump | 0.05 s⁻¹, 1.07 × 10⁻³ per frame, to flat | `level_jump_per_s` | heuristic |
| Segmental: frame | 8 samples, 21.33 ms | `level_frame_s` | heuristic |
| Segmental: the fast frame (text-fast-1) | 10.67 ms, 4 samples at m = 1; 8 ms, 1 sample at m = 3; joined into level_frame_s while the walk is slow or where a tail is modeled | `level_fast_frame_s` | heuristic by measurement |
| Segmental: the walk set by the Doppler spread (text-fast-1) | fast from f_D 3 Hz, back under 2.5 Hz, with the runs at +18.3 dB re F (+10 dB S₅₀₀) or more and ln \|ρ(21.33 ms)\| / ln \|ρ(10.67 ms)\| ≥ 2.5 (`doppler_min_shape`); W = 1.4 nepers/√s per Hz of f_D, 4.8 to 20, a Gaussian matrix | `doppler_fast_hz`, `doppler_slow_hz`, `doppler_min_db`, `doppler_walk_per_hz`, `doppler_walk_max` | heuristics by measurement |
| Segmental: the Doppler estimator (text-fast-1) | the widest stream's runs above +10 dB re F; pairs 21.33 ms apart within each stretch at or above −10 dB re the run's median, 8 ms inside its ends; memory 100 runs, used from 20; f_D = 2σ_D, Gaussian spectrum | `doppler_end_guard_s`, `doppler_memory_runs`, `doppler_min_runs` | form derived; values heuristic |
| Segmental: the channel's tail (text-fast-1) | per stream; runs above +10 dB re F of at least 32 ms, median at least +20 dB re F; memory 200 runs, used from 30, remade every 10; on at g₁₁ ≥ −15 dB re the mark, kept to −25 dB re the mark or 48 ms (18 positions at m = 1, 6 at m = 3); 0 s leaves it out | `echo_max_s`, `echo_run_db`, `echo_min_db`, `echo_min_run_s`, `echo_memory_runs`, `echo_min_runs`, `echo_on_db`, `echo_end_db` | likelihood derived; values heuristic, `echo_min_db` by measurement |
| Segmental: look-ahead, commit | 8 frames each, 170.7 ms; a sample committed 64 to 127 samples after it arrives | `level_lookahead_s`, `level_commit_s` | heuristic; no measured effect from 0.085 to 1.5 s |
| Segmental: belief threshold, edge groups | P̂ ≥ 10⁻⁴; at most 8 groups of levels for the edges | evidence.cpp, `kEdgeLevels` | heuristic |
| Segmental: template's window | m = 5 … 10 (0 below, 1 from 11); elements below 6 samples by the pair's template | `kEdgeFirst`, `kEdgeCount`; `short_score` | derived approximation |
| Segmental: horizon | boundary j searched with evidence through j + 16 | `kHorizon` | derived (a claim's window ends at j + 10, plus up to 6 for a next edge) |
| Segmental: over's tail | 6 samples; with a channel's tail of n positions n + 6 | `kEdgeCount` in search.cpp, `RunningSums::tail_positions` | derived (the windows' length) |
| Segmental: duration cut | an element ends within 4 σ of its median, a factor of 3.32 | `duration_z_max` | heuristic |
| Segmental: beam, ceiling | 15 nats, 1 500 elements | `SegmentalConfig::beam_nats`, `ceiling` | heuristics set by measurement |
| Segmental: a stretched story at the ceiling | ranked 4 nats above its partial score | `ceiling_stretch_bonus_nats` | heuristic chosen by measurement |
| Segmental: spacing of opened elements | none within T_s / 8 of one opened in the same state with a base as high | `open_spacing_dots` 0.125, `ModelTables::open_spacing` | heuristic chosen by measurement |
| Segmental: gap survival's table | every length to 256 samples, then 1 in 16, each entry the bin's first length's | `ModelTables::survival_index` | heuristic; the bound stays optimistic (derived) |
| Segmental: exp, ln, ln I₀ | 32 + 256 + 2 049 table entries, errors below | `segmental/fast_math.hpp` | derived |
| Detector: band's median, segmental's path | every 2nd frame of its own spectrum, 171 ms | `DetectorConfig::floor_every_frames` | heuristic |
| App: display | 12.5 pictures/s; track list 4.2 times/s, changed cells only; call list and status bar about once a second | `kTickMs`, `kTextEveryTicks`, `kStatusEveryTicks` | heuristics |
| Segmental: rings | C, rise, fall 8 192 samples (21.8 s at m = 1); context 1 024 samples; level 18 frames | `RunningSums`, `LevelEvidence` | sized before the gap stretch; holds the longest element, a word gap of 15.27 s at m = 1 (derived) |
| Segmental: filter scales | m = 1, 3 | `filter_scales`; `--set filter_scales=[1,2,3]` or one number | heuristic chosen by measurement |
| Segmental: tables' channel | the channel filter at 48 kHz for any whole multiple of 48 kHz | `segmental_tables_channel` | derived |
| Segmental: scale m, m = 2 selectable | dot 12 ms × m, Δ 2.667 ms × m, fastest dot 12 ms × m; 25, 45, 67 taps (`front_filter_taps_any_width`); σ_y²/N₀ 74.50, 38.87, 26.01 Hz; w 0.1987, 0.2073, 0.2081; 32, 25, 20 speeds | `at_filter_scale` | derived from m |
| Segmental: narrower filter's edges | E = 0.25 T at m > 1: 6 ms, 9 ms; 10–90% rise E × 0.590334; 0: 4.235 ms; refused outside 0 to 1; m = 1 keeps 4.235 ms | `front_taper_ratio`; `front_filter_rise_s`, set by `at_filter_scale` | heuristic by measurement (plateau 3/16 to 5/16); the response derived |
| Segmental: template delay | 0, 16, 24 ms at m = 1, 2, 3 (half a step's response at 7.84 samples, to a sample) | `template_delay_s`, set by `at_filter_scale` | derived |
| Segmental: narrower ceiling | 300 elements at m > 1 | `filter_scale_ceiling` | heuristic for CPU |
| Segmental: choice's start | 1.5 s of the channel's age | `filter_scale_start_s` | heuristic |
| Segmental: eligibility | at least 0.5 of the widest's symbols, or S − F ≥ 100 nats | `filter_scale_min_symbols`, `filter_scale_enough_nats` | heuristics by measurement |
| Segmental: taking the lead | 120 nats above the leader | `filter_scale_switch_nats` | heuristic by measurement |
| Segmental: drop | from 10 s of the channel's age, a narrower stream not leading, ineligible or over 30 nats behind | `filter_scale_drop_after_s` (negative: never), `filter_scale_drop_margin_nats` | heuristics for CPU |
| Segmental: gap stretches | g = 1.1^(5n) = 1.61ⁿ, n = 0 … 6, 1 to 17.4 | `gap_stretches` (7), `gap_stretch_speed_steps` (5) | reach derived (g = 15.7 at 33/5 WPM); step heuristic |
| Segmental: longest gap unit | gT_s ≤ 0.7 s, about 4.5 WPM overall; 178, 129, 98 fists at m = 1, 2, 3 | `gap_unit_max_s` | heuristic; fist counts derived |
| Segmental: stretch's prior | π = 0.002: ln(1 − π) = −0.002 nats for g = 1, ln(π/6) = −8.0 nats for another, once per over | `gap_stretch_prior` | heuristic chosen by measurement |
| Segmental: publishing | every 0.25 s; last 64 symbols compared | `kPublishEveryS`, `kPublishTailSymbols` | heuristic |
| Segmental: speed sent | median of 1.2 s × u / d over the last 16 published letters (A.9b, "Speed sent") | `kSpeedLetters`, `letter_wpm`, `text_wpm` | formula derived (PARIS); 16 heuristic |
| App: default decoder | segmental | `kDefaultDecoderName` | coordinator's choice |
| App: default center | 7031.5 kHz, until a saved one (README, "Settings kept between runs") | `UserChoices::center_khz`, `SdrplaySettings::center_hz`, `--center-khz` | heuristic |
| Local floor, segmental's path (off elsewhere) | ±1 kHz, lower quartile, 7 dB above it | `DetectorConfig::local_floor_radius_hz`; signal_detector.cpp | heuristic |
| One track per peak, segmental's path (off elsewhere) | D = 30 Hz | `DetectorConfig::one_track_per_peak` | heuristic |
| DC guard | none (0 Hz) | `DetectorConfig::dc_guard_hz` | heuristic |
| DC blocker, segmental's path | one-pole average of the input from 0 FS, τ = 10 s; notch at 0 Hz, −3 dB re the passband at ±0.0159 Hz (0.032 Hz wide) | `EngineConfig::dc_block_s`; `Engine::process_hop` | heuristic, chosen by measurement (τ); derived (notch) |
| Detector's spectrum, segmental's path | 4N-point Hann FFT, hop 2N: 5.86 Hz bins, 170.7 ms window, 85.3 ms frames, ENBW 8.79 Hz | `DetectorConfig::fft_factor` | heuristic, chosen by measurement (factor); derived (the rest) |
| Detector's neighborhoods, segmental's path | peak ±12 Hz; level ±6 Hz; candidate step 6 Hz a frame; follow 12 Hz a frame; D = 30 Hz | `peak_radius_hz`, `level_radius_hz`, `candidate_step_hz`, `follow_radius_hz`, `attribution_distance_hz` | heuristic (D chosen by measurement) |
| Detector's threshold, segmental's path | 7 dB SNR per fine bin (8.79 Hz); hysteresis 3 dB | `DetectorConfig::threshold_db` | heuristic, chosen by measurement |
| Prominence, segmental's path (off elsewhere) | 6 dB re the lowest bin toward a higher one, within ±150 Hz | `prominence_db`, `prominence_radius_hz` | heuristic, chosen by measurement |
| Keying sidebands, segmental's path (off elsewhere) | a bin at +10 dB re the peak within ±150 Hz whose mirror stands within 3 dB of the peak | `sideband_db`, `sideband_mirror_db`, `sideband_radius_hz` | heuristic (values); derived (the symmetry) |
| First retirement at a place, segmental's path (off elsewhere: 0) | leaves it open; the next closes it as a first hold | `EngineConfig::first_retirement_open` = 2 | heuristic, chosen by measurement |
| Copies of a neighbor, segmental's path (off elsewhere: 0 s) | window 6 s (281 blocks of 21.33 ms), high-pass 0.5 s; ρ ≥ 0.5, β ≤ 1, own ≤ 0.25 (linear, power); source older, proven, within 150 Hz, 15 dB or more above at the detector; no hold | `EngineConfig::copy_window_s`, `copy_detrend_s`, `copy_rho`, `copy_gain_max`, `copy_own_snr_max`, `copy_older_only`, `copy_source_proven`, `copy_radius_hz`, `copy_level_db` | forms derived (ρ, β ≤ 1, own); values heuristic, chosen by measurement (ρ, own, level, older, proven) or not varied (6 s, 0.5 s, 150 Hz, no hold) |
| A neighbor's text, segmental's path (off elsewhere: 0 s) | a symbol's span ± 0.5 s, at least 0.5 s (23 blocks of 21.33 ms), high-pass 0.5 s; ρ ≥ 0.5, K_W < K_S, own ≤ 0.5 (linear, power); any source within 150 Hz, the loudest (largest K_S) of several; envelopes kept 30 s | `EngineConfig::attribute_pad_s`, `attribute_min_s`, `attribute_rho`, `attribute_own_max`, `attribute_source_proven`, `attribute_radius_hz`, `attribute_history_s` | forms derived (ρ, own, K_W < K_S, the loudest); own and any source chosen by measurement; the rest heuristic, not varied |
| Level-scaled exclusion (a variant, off) | no track within 30 Hz × 10^((ΔL − 12 dB)/20) of one ΔL dB stronger, at most 150 Hz | `DetectorConfig::skirt_exclusion_db` (0 = off), `skirt_exclusion_hz`, `skirt_exclusion_radius_hz` | 6 dB per doubling derived (1/f²); 12 dB, 30 Hz heuristic; dropped by measurement (A.6) |
| Replay | 2 s, 94 past blocks | `EngineConfig::replay_s` | heuristic |
| Replay's rate | 100 tracks/s, 2.13 per block | `EngineConfig::replay_tracks_per_s` | heuristic |
| Judge: window, first verdict | 12 s, 4 s | `TrackJudge::kWindowS`, `kFirstVerdictS` | heuristic |
| Judge: rates | keyed 3, not keyed −8 nats/s; strong 1000 nats/s, share 10⁻³ | `kKeyedRate`, `kNotKeyedRate`, `kStrongRate`, `kShortfallShare` | heuristic |
| Judge: quiet | new 5 nats/s over 8 s; at most 14 s unproven; proven 1 nat/s over 30 s | `kQuietNew*`, `kUnprovenS`, `kQuietOld*` | heuristic |
| Judge: free key | two states, 5 switches/s | `kFreeKeySwitchPerS` | heuristic |
| Text test | floor −12, plausible −6, tiny junk +8, garbage junk +6 nats; q fitted | `kTextFloorNats`, `kPlausibleNats`, `kTinyJunkNats`, `kGarbageJunkNats`, `kTinyShares` | heuristic (the bound is derived, for the models only) |
| Holds | place ±D (30 Hz on segmental's path), 8 s, doubling to 120 s; a repeat only after the hold's end; forgotten after 120 s open | `TrackJudge::kHoldS`, `SignalDetector::kLongestHoldS` | heuristic |
| Region | 4 retirements within 750 Hz in 60 s close ±750 Hz, 30 s up | `kRegionRetirements`, `kRegionRadiusHz`, `kRegionMemoryS`, `kRegionHoldS` | heuristic |
| List | ended rows 60 s; same place 30 Hz (the segmental path's D); about 4 000 symbols a row; a stretch heard from a neighbor left out whole where the neighbor's listed row holds every symbol of it within 25 ms; rechecked within 300 Hz and 30 s | `TrackTable::kRetainEndedS`, `kSamePlaceHz`, `kMaxRowSymbols`, `kTwinS`, `kRecheckHz`, `kRecheckS` | heuristic; 25 ms the duplicate rule's, twins' offsets measured (A.6) |
| Ring | at least 4 s; 5.46 s at each radio rate | `SdrplaySource::kRingS`, `SpscRing` | 4 s heuristic; 5.46 s derived |
| Budget | shed above 1 s, grown over 2 checks 1 s apart (margin 0.05 s below 0.25 s), over 8 tracks, to ¾; back by 5 per 5 s below 0.25 s | stream_runner.cpp | heuristic |
| Silence | 5 s | `DeliveryWatch::kStalledAfterS` | heuristic |
| Bank: locked-level recovery | last 0.32 s's 20% quantile over 10 dB under the level | `kLockedLevelRatio` (bank/noise.cpp) | heuristic |
| Callsign stage: read text settled for | 10 s, on each whole second of stream | `CallSearchConfig::settle_s` | heuristic, chosen by measurement (A.12) |
| Callsign stage: text a correction changes | read again; kept 200 s back | `reread_corrected`, `history_s` | heuristic, chosen by measurement (A.12) |
| Callsign stage: same place | 300 Hz | `CallSearchConfig::same_place_hz` | heuristic, chosen by measurement (A.12); manta's bucket |
| Callsign stage: evidence window | 1 800 s, at one place and, since `rob-1`, over the band (pooled points) | `CallSearchConfig::window_s` | heuristic, chosen by measurement at one place; not re-chosen for the band-wide use (A.12) |
| Callsign stage: points to report | framed 2, bare 2; listed framed 1, listed bare 2 | `points_framed`, `points_bare`, `points_known`, `points_known_bare` | heuristic, chosen by measurement (A.12); from CW Skimmer's table |
| Callsign stage: points of a reading | whole or glued 1, joined 0.75, stripped 0.5 | `joined_points`, `stripped_points` | joined heuristic, chosen by measurement (A.12); stripped heuristic |
| Callsign stage: a word named | no exception: 2 points, as any word (1, `call-1` to `rob-1`: a whole or glued word named by another passed alone) | `points_named` | standard practice (CW Skimmer's Normal level, secondhand through the RBN's 2021 post), checked by measurement (A.12) |
| Callsign stage: places pooled | a call's readings add over the band, one per transmission (overlap in time), leaving out places where a variant holds it; the same support decides report and withdrawal | `pool_places` 1 | heuristic motivated by the evidence rule (readings at several places are not independent when a misreading is systematic); scope (the band, `window_s`) a choice; on by measurement (A.12) |
| Callsign stage: glued pieces | each piece of a glued word has its own characters' times (K1ABCK1ABC: two readings) | `glued_pieces_timed` 1 | derived from the evidence rule (A.12) |
| Callsign stage: a neighbor's text | every symbol read (default); a variant skips a stretch with `heard_from` that is whole words and whose neighbor's track holds every symbol of it within 25 ms | `skip_neighbors_text` 0, `neighbor_twin_s` 0.025 s | chosen by measurement: the variant changed 12 calls' fields, no call (A.12) |
| Callsign stage: joined words segmented | on (1): a word of 6 characters or more that rule 2 cannot split (2, a variant: every such word; 0 off); a Viterbi cuts it into keywords, calls and strays (gains in nats against noise text); a call is believed at 2.5 nats of evidence (repeats; DE before 2.77, DE after 1.0, an unambiguous K after 1.5) on a word at least as likely the protocol's text as noise; at most 64 characters | `segment_joined` 1, `segment_min_chars` 6, `segment_alternatives` 1 (both readings, stripped, of a call whose last E, I or K the segmentation cannot place; 0 the segmentation's alone), `CallSearchConfig::segmentation` (`SegmentationCosts`) | on by measurement and a license check: +8 window-confirmed calls for +1 RBN misreading on the 2026-11-17 recordings, none the RBN can see held out (it spots CQ only), the calls added checked in the FCC's license file, about 12 likely real for 2 false (A.12); keyword shares, call lengths and `call_nats` measured, the form's gain derived for 3 to 9 characters, the rest heuristic |
| Callsign stage: tiny calls | whole words only, 2 of them, listed or not; 0 leaves them unread | `tiny_points` | heuristic, chosen by measurement (A.12) |
| Callsign stage: a call that starts another | held as a variant at distance 1 (0: by edit distance alone) | `prefix_held` | heuristic, chosen by measurement (A.12) |
| Callsign stage: withdrawal | pooled (the default): when its pooled support no longer passes; with `pool_places` 0: by a rival better at its best, or a local defeat with no pass elsewhere beyond 150 Hz (0: any local defeat) | `withdraw_own_place` | heuristic, by a hand test and measurement (A.12) |
| Callsign stage: variants | edit distance 1, or 2 at over 2 times the points (2 only between calls of 4 characters or more, or a short pair); within 150 Hz, 1 800 s; a tie goes to the earlier call, or within 1 s to the later (a heuristic constant in the code) | `far_variant_ratio`, `variant_hz`, `variant_s` | heuristic, chosen by measurement (A.12) |
| Callsign stage: a list against a rival | factor 3 | `known_rival_factor` | heuristic |
| Callsign stage: a call sent twice in a row | framed | `doubled_is_framed` 1 | heuristic, chosen by measurement (A.12) |
| Callsign stage: a word named, with a list | passes alone only listed (read only with `points_named` below the other thresholds; a listed named word passes as framed in any case) | `named_listed_only` 1 | heuristic, chosen by measurement (A.12) |
| Callsign stage: a sure listed word | off (0); a variant: a listed call passes on one bare whole or glued word of margin at least this | `known_margin_nats` | heuristic; off by measurement on the held-out recordings (A.12) |
| Callsign stage: known-call list | text, one call a line; NUL bytes or no line with a call's form refused | `KnownCalls::read` | interface choice |
| Callsign stage: short pairs | one letter after the digit, same prefix and digit, distance 2: held at over 2 times the points | `short_variant_ratio` | heuristic, chosen by measurement (A.12) |
| Callsign stage: locators | six characters, AA00AA with letters A–R, A–X: no reading | `is_locator` | heuristic |
| Callsign stage: call forms | three characters before the digit only for 3D? (a half series) and 3XY, then three letters at most; US series (K, N, W, AA–AL): one to three letters, two after AA–AL and NA–NZ (AF#EMA, NF#EMA excepted), no 1x1 ending in X; a letter-digit prefix (C6, E7) and 1 to 4 letters, its digit the call's; around a slash both sides tried as the base, and when both can: the listed one, else the one that is not a location form (VP2V), else the left | `parse_form` (`three_character_prefix`, `us_form`, `location_form`), `CallSearch::add` | derived: RR 19.68 and 19.68.1, Appendix 42; 47 CFR 97.3(a)(11) and the FCC's HD.dat of 2026-11-15; 3XY sourced (Guinea's DXpeditions); the prefix-digit form measured (MASTER.SCP, the RBN); the slash's choice heuristic (A.12) |
| Callsign stage: unusual forms | four letters after one digit: from a list only (0: as any call) | `unusual_listed_only` 1 | heuristic, CW Skimmer's practice in kind (an unusual pattern needs more), chosen by measurement (A.12) |
| Callsign stage: prefix-digit forms | no digit but the prefix's (C6AGU): from a list only (0: as any call) | `prefix_digit_listed_only` 1 | heuristic, chosen by measurement (A.12) |
| Callsign stage: words of CW | rigs, antennas, software (21, exact) and one or two letters before 5NN: from a list only, always | `is_cw_word`, `ParsedCall::cw_word` | heuristic (manta's exact matches); list only, not refused, since `call-form-2` (A.12) |
| Callsign stage: joined periodic text | a joined reading whose text, run on through the next words, repeats with a period shorter than the call over max(n, 2p) characters is refused (0: read) | `joined_periodic` 1 | heuristic, chosen by measurement (A.12) |
| Callsign stage: a repeat after a joinable keyword | no evidence in the segmentation (1: a repeat as any) | `SegmentationCosts::ambiguous_repeat` 0 | heuristic, the ambiguous K's rule turned around; chosen on one case (W7Q) and measured (A.12) |
| Callsign stage: an X in front | also read as TU run onto the call, a stripped reading (XW3RJ: W3RJ at 0.5 point; 0: not) | `x_as_tu` 1 | X = T + U with the letter space lost, derived; the half point heuristic (`stripped_points`); on by measurement (A.12) |
| Callsign stage: a fragment a joined reading covers | in the variants' comparison, a transmission read as a whole-word call and as a joined reading starting with it (K4VR T) counts for the longer only, when the longer has `covers_own_points` of its own at the place; else for both; no cover when the words added are only K and R (3; variants 1: for the longer always, 2: for neither, 0: for both) | `joined_covers` 3 | heuristic, chosen by measurement (A.12) |
| Callsign stage: the longer call's own points for a cover | 1.5 points outside the covered transmissions (more than one reading, two joined at least) | `covers_own_points` | heuristic; 1.0 let one whole word take a real call's transmissions (review of `call-arb-1`, A.12) |
| Callsign stage: flat stripped readings | a stripped reading with an X taken off the front counts once, not 3 times, in a listed call's points against an unlisted one (2: also E, T, I; 0: never) | `flat_stripped` 1 | heuristic, the review's ruling (A.12) |
| Callsign stage: a 1x1's bar | an unlisted call of three characters (N4A, W6S, G2M) needs at least this many points, whatever else it passes on; signed means a counted occurrence (in the pooled window, not held by a variant) signed, not the List's *Signed* | `short_points` 3, `short_points_signed` 2 | heuristic, chosen by measurement; 3 characters are 0.63% to 0.73% of the calls the RBN spots, measured (A.12) |
| Callsign stage: a cut number's shape | a digit 2 to 9, A, N or T, then the digit, outside 9A, 7N, 8N (2T7A, 3N4D): unlisted, it needs at least this many points, fewer when a counted occurrence signed, as a 1x1 (2; 1: from a list only; 0: as any call) | `cut_number_form` 2, `cut_number_points` 5, `cut_number_points_signed` 2 | heuristic, the bar chosen by measurement; 4 of 11 370 spotted calls have the shape, measured; not 1 or 0 first, derived: a number's 1 and 0 are sent cut themselves (A.12) |
| Callsign stage: signed | some reading had the role signed, at any place and time; never undone | `is_signed` (`CallRow::framed`) | heuristic (A.12) |
| App: decoder-only mode | off by default; on, no stage runs | `UserChoices::decoder_only`, `--decoder-only` | owner (2026-11-18) |
| App: call list | every call by default; signed calls only an option | `UserChoices::signed_only`, `--signed-only` | owner (2026-11-18) |
| App: known-call download | on the button only; file 16 MiB at most, listing 1 MiB; fails after 30 s without data | `kMaxListBytes`, `kMaxListingBytes`, `kTransferTimeoutMs` | owner (the button); limits heuristic |
| App: settings kept | per user, an INI file; none read or written with `--no-settings`, without an operator or offscreen (unless `--settings`); a command-line option not saved unless changed in the run | `SettingsStore`, `start_choices`, `choices_to_save` | interface choices (README, "Settings kept between runs") |
| App: pane's pick on the waterfall | the nearest marker within 8 px | `WaterfallWidget::kPickPx` | heuristic (the marker is 8 px wide) |
| App: pane's pick from a call; its follow distance | the nearest listed row within 30 Hz, a live one first; a successor within 30 Hz | `row_near`, `TrackTable::kSamePlaceHz` | the list's own same place (the segmental path's D), reused; heuristic for this use |
| App: pane's successor | a live row, first track born after the followed signal's newest, started no earlier than 2 s before it was last heard (its latest symbol's end) | `SignalFollow::kSuccessorOverlapS` | heuristic (about the engine's 2 s replay) |
| App: pane's earlier text | 16 384 bytes, the oldest cut at a word space | `SignalFollow::kMaxEarlierBytes` | heuristic (about 20 min at 30 WPM) |
| App: calls on the waterfall | heard at their place (`CallRow::heard_s`) within 60 s of stream; white within 16 s; 3 lanes, 6 px between labels | `kCallLabelAgeS`, `kCallLabelRecentS`, `WaterfallWidget::kLabelLanes`, `kLabelGapPx` | heuristics (16 s: the picture's height at its first size, 380 rows at 23.4 rows/s, derived) |
| Segmental: margins | off (0); a variant: a merge's runner-up within R (measured at 16 nats; at most 16.6); symbols matched by time (30 ms or half the shorter's length; 2 ms; 3 agreeing; 24 steps); published at half-nat steps | `rival_margin_nats`; `note_rival`, `confidence_level` | the update derived (soft-output Viterbi); the constants heuristic (A.8e) |

## A.11 Definitions used in tests and the benchmark

- **Duplicate tracks** (det-2's count on real recordings, results analysis, not the engine's test): a symbol (not a word space) of
  track W is a copy when a track within 150 Hz of W (at its birth or its end), stronger by the detector's level (the larger of its
  levels at birth and end), alive at the same time, has a symbol of the same text whose start and end lie within 25 ms of W's; W is
  a duplicate when it has at least 8 symbols and at least half of them are copies (a track of fewer than 8 is not tested: its
  copied symbols are counted as 0). Two decoders behind like filters read one station's
  characters at the same times; an independent station's do not line up. Heuristics: 25 ms is about half a dot at 25 WPM. Short
  junk tracks on wide signals, retired as not Morse, count as duplicates of one another too.
- **Text shown twice** (copy-2's measure on real recordings, results analysis, not the engine's test): two tracks within 150 Hz
  of each other (at birth or at the end), alive at the same time, both proven when they end (`TrackEvent::proven`, the judge's: the
  tracks a list shows); a symbol (not a word space) of one and a symbol of the other are *printed twice* when their texts are equal
  and their starts and their ends each lie within 25 ms (the duplicate rule's match, without its level); *shown twice* when the
  list's rule (A.6, "A neighbor's text") leaves neither out, the source's listing read as its being proven. It sees exact twins only:
  a neighbor's keying read otherwise on the weaker row is not counted; A.6 gives that residual beside it. Counted once per pair of symbols. Heuristic, as the duplicate rule. It counts junk rows' junk too,
  where they are proven.
- **Ghost cells** (det-2's scratch generator over `kz4ap_synth`, 48 kHz, one seed): QSOs of three signals, A's CQ at the slot, B's
  answer 30 to 130 Hz away (12 to 18 dB S500), then A's long over (110 characters); A at 5 to 36 dB S500; jittered cells of offset
  (30, 45, 60, 80, 100, 130 Hz) and A's level (5, 12, 20, 28, 36 dB S500), three QSOs a cell; machine keying, 18 to 32 WPM. A
  copy: a symbol of a track within 15 Hz of B during A's over matching A's track's by text and by start and end within 25 ms.

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
  others are VE3NEA's DeepCW styles with their parameters (MIT; notes in
  `docs/research/deepcw-generator-notes.md` §1):

  | Style (their name) | μ: dit, dah, element space, character space, word space | σ_ln: same order |
  |---|---|---|
  | computer (Computer) | 0, 1.10, 0, 1.10, 1.94 | 0.05, 0.016, 0.05, 0.016, 0.008 |
  | paddle (Paddle) | 0, 1.10, 0, 1.10, 1.94 | 0.05, 0.016, 0.05, 0.2, 0.2 |
  | bug (Vibroplex) | 0, 1.10, 0, 1.10, 1.94 | 0.05, 0.2, 0.05, 0.2, 0.2 |
  | hand (HandKey) | 0, 1.50, 0, 1.50, 2.0 | 0.15, 0.3, 0.2, 0.3, 0.2 |

  Medians exp(μ): 1, 3.00, 1, 3.00, 6.96 dits; hand key 1, 4.48, 1, 4.48,
  7.39 dits. Character and word spaces are one draw each (VE3NEA sums
  several draws; same medians). `imbalance_dits` = δ/T lengthens every mark
  and shortens every space; VE3NEA draws δ ~ N(0, (0.1·T)²) once per
  operator, and their training mix is hand 0.25, paddle 0.50, computer 0.25.
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
  VE3NEA's parameter, taken from their code, not derived or measured here),
  whose Gaussian least-squares fit has 2σ = 1.01·f_D (measured by VE3NEA:
  their notebook fits a periodogram of one hour of simulated gain at
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
- **Keying edges** (synthetic recordings, `edge_s`, `edge_placement`):
  every mark rises and falls as a raised cosine, c(x) = ½ − ½ cos(π x) for
  x = (t − t₀)/`edge_s` from 0 to 1 (0 before, 1 after), where `edge_s` is
  the cosine's **full length** (s), not its 10–90% time (10–90% = 0.5903 ×
  `edge_s`, derived: (π − 2 acos 0.8)/π). `edge_placement` says where the
  edges lie, one of three values:
  - **"start"** (D17; owner, 2026-10-23): the rise starts at key-down and the
    fall at key-up, so env(t) = c(t − on) · (1 − c(t − off)) for each mark
    (summed over marks), 2.5 ms 10–90% (owner), full length
    `edge_s` = 2.5 ms / 0.5903 = 4.235 ms (derived). Each mark keeps its
    keyed length at half amplitude at every speed (its envelope crosses ½ at
    on + `edge_s`/2 and off + `edge_s`/2) and reaches `edge_s` past its
    key-up; a 12 ms dot is 7.765 ms flat (65%). A mark shorter than
    `edge_s` follows the product and never reaches 1; a gap shorter than
    `edge_s` sums the fall and the next rise, which stays at or below 1
    because c does not decrease (derived). These are the edges of DEV2
    (DEV2e) and of every new suite, **set explicitly by their builders**
    (`D17_EDGES` in `training/kz4ap_synth/generate.py`): they are not the
    code's default.
  - **"centered"**: each edge is centered on the mark's end, so the
    50%-amplitude length is the nominal one: VE3NEA's convention with their
    2 ms edges (groups B and B2).
  - **"inside"**: milestone 1's: each edge lies inside its mark (5 ms, the
    default of `SignalSpec` and of `keying_envelope`), so a mark is `edge_s`
    shorter, and a space `edge_s` longer, at 50% amplitude than its nominal
    length: an imbalance of −5 ms/T, −0.10 dit at 24 WPM (derived). **The
    milestone-1 scenarios and suites (single, band, `smoke`, `full`,
    including the stage-1 benchmark's groups) keep these edges, because they
    are frozen evidence: the smoke check's figures below rest on them**
    (new decoder plan 1b, Ruling 70). Group S scales its 5 ms with its time
    base (× 25/12).

  S2 (the stretch test of the development set) scales D17's edges with its
  time base, 2 × 4.235 ms = 8.470 ms, placement "start" (plan 1b,
  Ruling 62). The labels carry `edge_s` and `edge_placement` (an older
  field, `edges_centered`, is gone: "centered" is its True).
- **Oracle frequency** (the oracle channels of viterbi and segmental, `kz4ap-bench
  --oracle --decoder viterbi|segmental`; design Part 1, plan O3): the detector
  frequency an oracle channel follows is its label's true carrier, as the
  generator makes it (angle 2πft + πḟ(t − start_s)²): f(t) =
  `freq_offset_hz` + `drift_hz_per_s`·(t − `start_s`), Hz from the span's
  center, for a signal of one station; for a QSO label, the over in
  progress, `freq_offset_hz` + its sender's `offset_hz` (+ the same drift
  term, from the QSO's `start_s`), switching at each transmission's
  `start_s` (the first from 0 s) and held through the silence after it.
  It is evaluated at each 32-sample channel block's start (21.33 ms) and
  held over the block. A per-station label of a drifting QSO is refused
  (its `start_s` is the station's first over, not the QSO's). The other
  decoders' oracle channels keep the label's frequency at `start_s` as
  their anchor (Matched) or mixing frequency (the bank), unchanged. The
  frequency genie (`--genie frequency`), which gave the decoder the
  carrier, is removed: the channel now carries it.
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
- **Letters** (viterbi's and segmental's rates): the reference text's characters
  that are not word spaces, a prosign counting one (1 086 215 in the
  development set DEV2, counted by the plan's analysis).
- **Search error** (viterbi and segmental): every edit, word-space edits
  included, in the bench scorer's alignment (above) between a run's text and
  the text of the *wide-beam reference*, the same decoder on the same
  recording with a much wider beam and higher ceiling (80 nats and 17 000
  stories, the first prototype's placeholders); counted per 1 000 letters.
- **Live stories** (viterbi): the stories alive after a sample's
  pruning. Its statistics (`SearchStats`, kept when `ViterbiConfig::stats`
  is set) count the samples by live stories in bins of 10, the mean and the
  largest in each 0.1 s, the samples at which the ceiling dropped a story,
  and what became of each newborn line (born from silence or as a rescue;
  when it first led; its largest deficit behind the best until then; the
  step, merge, beam or ceiling, that removed its last story).
- **Bench settings** (`kz4ap-bench --set NAME=VALUE`, repeatable): read as given and applied after all options (`bench/src/main.cpp`), since which decoder a setting belongs to is known only then, and before the recording is read, so a refused setting costs nothing and the order of `--set` and `--decoder` does not matter. With `--decoder viterbi` it sets a `ViterbiConfig` field by its name. With `--decoder segmental` it sets a `SegmentalConfig` field where the name is one (`beam_nats`, `ceiling`, the gap stretch's and the filter scales' included, `filter_scales` as a JSON list or one number), else the field of `SegmentalConfig::model`. An unknown name is refused; so is `--set` with the other decoders.
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
  Measured values: 2a record, sections 3.3 and 8.4 (Envelope and
  Matched; they vary by about a fifth between sessions on one machine);
  Plan A record, section 4.6, and the Plan B record (the bank).
- **Smoke check** (`bench/smoke.sh`; CI runs it on Windows and Linux):
  generates the `smoke` recording (band scenario, 8 stations, 30 s,
  192 kHz, seed 1) and scores it twice on each path. The Envelope path
  (`--decoder envelope`) must meet `bench/baselines/smoke.json` (CER
  ≤ 0.09, detection recall ≥ 0.875) and the Matched path
  `bench/baselines/smoke-matched.json` (CER ≤ 0.07, recall ≥ 0.875); the
  two runs of each path must write byte-identical results (run-to-run
  determinism on one platform). **CI therefore bounds the Envelope CER; it
  does not pin bit-identity with milestone 1.** What establishes that the
  Envelope path is unchanged: the generator's frozen-copy tests
  (`test_default_signals_match_milestone_1_generator`,
  `test_band_defaults_match_milestone_1` in `training/tests/test_generate.py`:
  the band scenario and the generator's default output match frozen copies
  of milestone 1's code), reading the engine's Envelope path (final branch
  review, 2026-09-30), and its smoke CER, 34 edits in 964 symbols (0.0353,
  measured), the same as at milestone 2a's Task 2, before any engine
  change. These figures stay valid: the `smoke` recording keeps milestone 1's
  "inside" edges (Keying edges, above), so D17's edges change none of them.
  **Provenance of the Matched limit 0.07 (heuristic):** the
  measured 0.0622 = 60/964 (Windows, before milestone 2a's Tasks 15 and
  16) plus a margin of 3/482 = 6/964 (0.0685), rounded up to two decimals:
  the check fails from 68 edits (Envelope's 0.09 fails from 87 edits, a
  margin of 52 over its 34). The limit is never widened, and is tightened
  only when the Matched CER is below 0.0622 on both CI platforms, Windows
  and Linux, to the higher of the two plus 6/964, rounded up to two
  decimals. The measured history (now 0.0436 = 42/964 on Windows; the Linux
  value not yet measured, so the limit stays 0.07): 2a record, sections
  3.5 and 8.4.
- **Suites** (`training/kz4ap_synth/suites.py`): `smoke` is the CI
  recording; `full` covers sensitivity (oracle, S₅₀₀ −10 … +20 dB at 12, 25
  and 40 WPM), fading anchored to VE3NEA's DeepCW benchmark (their
  Butterworth spectrum, f_D grid 0.1, 0.3, 1, 3 Hz, their ten SNR points as
  S₅₀₀ = their 3 kHz key-on SNR + 7.78 dB, their styles, imbalance, style mix
  and text statistics, and their 2 ms centered edges; compare their curves
  with the no-space CER, their metric), fists, speed changes, interference,
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
    could go farther, which the code allows and no run has shown). How the
    full suite's QSOs fell into tracks on the Matched path (measured): 2a
    record, section 8.5.
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
  −6.02 dB at 150 Hz and −18.0 dB at 200 Hz (measured, A.7): labels
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
  sample count. The decoder that reads them (`kz4ap-bank-replay`, as the
  stage-1 prototype did) mixes the stream down by f_off (and a labeled
  drift) itself. With `--decoder viterbi` the stream is the followed one
  (A.7), its center the label's frequency, and a channel whose center
  moves (a drifting or QSO label) stops the run with an error when it
  first moves.
  **Detector form** (`--record-channels DIR` without `--oracle`, used with
  `--decoder matched`): `channel-<track id>.c64` holds the channelizer
  output of every channel the detector opens, from its first block to its
  track's death (to the end if it never dies). `channels.json` gives each
  one's track id, `birth_freq_hz` (the track's frequency at birth, Hz from
  the span's center), center, first sample index, `open_s` (first sample
  index / r, s), `close_s` (end of its last block, s; null unless the track
  died), sample count, and `anchors`: [first sample index, Hz] at every
  block where the detector's frequency for the track changed
  (`ChannelBlock::anchor_hz`). `label_index` and `label_freq_hz` are null
  (and `birth_freq_hz` is null for oracle channels, whose entries carry
  `open_s`, `close_s` and `anchors` too). The stage-1 prototype
  (`streams.anchored_baseband`; removed on 2026-10-07) decoded these: it
  mixed a detector channel down by
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
- **Runs on recorded channels** (`training/kz4ap_proto/runner.py` records
  the channels and scores the decoded files, `metrics.py`, `report.py`;
  built in milestone 2b, stage 1, for the Python prototype, removed on
  2026-10-07; `kz4ap-bank-replay` now writes the decoded files, oracle
  test cases only). **Oracle copies:** the recordings of the groups normally decoded
  through the detector path (pauses, strong, tune-up, first sample, band,
  crowded; `suites.ORACLE_COPY_GROUPS`) are decoded once more on oracle
  channels, as result `<recording>.oracle` in
  group `<group>, oracle`, by Envelope and Matched (`kz4ap-bench --oracle`)
  and by the bank (`kz4ap-bank-replay`), so every regime has a like-for-like decoder
  comparison; group H already has its own oracle copy. **Channels:** the
  oracle test cases' channels are recorded with `--oracle --decoder envelope
  --record-channels` and mixed at the label; every non-oracle recording is
  recorded once more through the Matched path's detector (`--decoder
  matched`, no `--oracle`, D_ch = 47 Hz) as `<recording>.detector`, each
  channel decoded from its opening and mixed by the detector's frequency
  block by block (by the prototype; the replay tool decodes oracle test
  cases only), and scored in the tracks form under the engine's own
  result names, so on the detector path such a run pairs with Matched
  per track (the same tracks) and with Envelope per label (Envelope's
  detector opens its own tracks). **Comparison** (no acceptance gate;
  owner, 2026-09-30): for each (group, tag), the mean over signals of the
  per-signal CER difference (the run minus the reference, on the same
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
  favoring the run) and group H oracle QSO labels with a nonzero
  answering offset (the answering station is off the mix, handicapping
  it). **Detection measures** on the detector path, per decoder and
  group: labels scored, labels detected, detection recall (detected /
  scored) and false tracks (tracks that decoded text and matched no
  label), summed over each recording's main test case, and tracks per QSO
  (group H, as in the suite summary); as the bench counts them they depend
  on the decoder. **The bank's own statistics** (oracle channels,
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
  outside the padded transmissions; *decoding CPU*, the decoded files'
  CPU time per channel-second (s/s; the replay tool's per-channel thread
  time). All
  thresholds here (×1.5, 3 s, 5 s, 1 s, 0.5 s, 6 dB) are heuristic choices
  of the report, not measured.
- **Bank corrections in the bench** (`kz4ap-bench`, any decoder): the
  bench assembles each track's final and immediate text from the events
  (`TrackText`, A.8c "The consumer's rule"), writes both per track (`text`,
  the final text, and `text_immediate`), and scores both: `cer` (and every
  other rate, and the baseline check) on the final text, and
  `cer_immediate` (with `decoded_immediate`, `edits_immediate` and
  `cer_immediate` per signal) on the immediate text. The JSON keeps the
  key `front_end` (stage 1's tooling reads it) and adds `decoder`, with the
  same value (`envelope`, `matched` or `bank`). For Envelope and Matched
  the two texts are equal. For the bank it also writes, per track,
  `corrections`: every correction the events carried, with `t_s` (s,
  stream time), `reach_s` (s), `reason`, and `removed` and `inserted`
  (characters): the final text's characters from the correction's index on
  against its new ones, less what the two share at their start (an index
  before the first change) and at their end (characters re-sent
  unchanged), so the smallest contiguous block that differs; an upper
  bound of the correction's edit distance (derived), and removed plus
  inserted, summed over a track, is at least the net Levenshtein distance
  from the immediate to the final text (derived: an append adds the same
  character to both texts, and each correction changes the final text by
  at most its removed plus inserted). `kz4ap_synth.suites summarize`
  counts them per group (section "Corrections" of the summary: corrections
  per channel-minute of the group's engine runs, by reason, the reach's
  median, 99th percentile and maximum, numpy's linear percentile, and the
  characters removed and inserted per channel-minute, shown as "—" for
  results written before these counts; each engine run once, a
  detector-path recording's station-label result being the same run).

## A.12 Callsign stage

### Exact form

`engine/include/kz4ap/callsign.hpp` and `engine/src/callsign.cpp`; the tool `kz4ap-calls` (`bench/src/calls_main.cpp`) runs it over a
recording or over a log of a decoder's events, and the app holds it in `app/src/call_list_model.hpp`, on the GUI's thread.

**Form.** `parse_form`: letters, digits and at most one slash. A base call is a prefix of length p, then d digits, then l letters, with
l from 1 to 4 and d from 1 to 4, d > 1 being a *special* form, and d = 1 with l = 4 an *unusual* one (`ParsedCall::unusual`). p is 1 only
for the series that are a whole letter's (B, F, G, I, K, M, N, R, W); else 2; 3 only for a digit, a letter and a letter that open a half
series or Guinea's 3XY (`three_character_prefix`: 3D?, 3XY; 3DA0RU, 3XY1D), and then l is at most 3. A prefix of the United States'
series (`us_prefix`: K, N or W alone, or followed by a letter, or AA to AL) with d = 1 takes the FCC's forms (`us_form`): l at most 3; l at
most 2 after AA to AL and NA to NZ, except AF#EMA and NF#EMA; and a 1x1 (K, N or W, the digit, one letter) never X. The special forms
of those series (W100AW) are as any other's. `allocated_pair` is the ITU's table: no Q; no pair of two digits; no 0
first, 1 only in 1A; a digit and a letter except 4N; two letters except YZ; a letter and a digit by the table (A2-A9, C2-C9, D2-D9, E2-E7,
H2-H4, H6-H9, J2-J8, L2-L9, P2-P9, S2, S3, S5-S9, T2-T8, V2-V8, Y2-Y9, Z2, Z3, Z8), with S0 and Z6, which amateurs use outside it. DE
as a prefix is refused: it is the word DE glued on. A *prefix-digit* form (`ParsedCall::prefix_digit`, since `call-form-2`): an
allocated pair of a letter and a digit outside the whole-letter series (C6, E7, J6, P3, D4, T8, V3, Z3, A6, S0), then 1 to 4 letters and
nothing else, the prefix's digit standing for the call's (C6AGU, E7DX, J6M; 3 to 6 characters); with 4 letters it is unusual too. A
digit-letter pair (3B, 9H) has no such form: its calls have a digit after it (3B8CF, 9H1A). Around a slash: `base/P`, /M, /MM, /AM, /QRP,
/QRPP, /A, a digit, or a prefix-like word (`is_prefix_like`: a whole-letter series' letter, or at most 4 characters opening with an
allocated pair); or `prefix/base`, the left side prefix-like. A prefix can itself be a base call (VP2V, VK9X, 3D2R). When both sides
can be the base (a heuristic, the review's ruling in `call-form-2`), in order: (a) in the stage (`CallSearch::add`), the side the
known-call list holds, when it holds exactly one (`ParsedCall::other_base` carries the other side); (b) the side that is not a
*location form* (`location_form`: exactly four characters, an allocated pair, a digit and one letter, outside the United States'
series: VP2V, VP2E, VK9X, 3D2R, CE0Y, PJ2T), when exactly one is; (c) the left. So VP2V/K1ABC and K1ABC/VP2V give K1ABC (one side only
can be the base), W1AW/VP2V, VP2V/W1AW and VK9X/W1AW W1AW, K1A/W1AW K1A and N4A/K4SV N4A (a 1x1 or a US call of four characters, KH6A,
is no location), and with K4SV alone listed, K4SV. Before `call-form-2` the left side was the base whenever it parsed, and VP2V/K1ABC
was refused. `training/kz4ap_proto/calls_score.py`'s `base_call` takes rules (b) and (c), with no list.

**Words of CW.** `is_cw_word`: exact, G5RV, K3S, K4D, N1MM, K1EL, K9AY, TH7DX, TH6DX, TH5DX, TH3JR, TH2MK, HF5B, HF6V, HF9V, HF2V,
FT8DX, FT5DX, FT3DX, W2DU, W8JK, W3DZZ; and any base of one or two letters before 5NN (R5NN, TU5NN, FN5NN, DJ5NN, W5NN), which
takes in the exact list it replaces, {R, K, E, T, N, TU, UR, ES, EE, TT, ET, TE}5NN. Since `call-form-2` such a word is not refused:
`parse_call` marks it (`ParsedCall::cw_word`), every reading rule treats it as a call, and the stage drops its readings before they
reach a candidate unless the list holds it. A joined reading (rule 4 below) neither starts at one nor runs through one. `only_tiny_pieces`: every character in E T I S H A N M U 5:
read only as a whole word. `is_locator`: a word of six characters, two letters A to R, two digits, two letters A to X (a Maidenhead
locator, FN31PR), gives no reading at all, whole, glued, stripped or joined.

**Margin.** A reading's `margin_nats` is the least of `margin_of(p)` = max(0, ln(p / (1 − p))) over the symbols it came from, the word spaces
between joined words included; −1 (unknown) if any has p = 1, as every symbol of a decoder that keeps no margins (the segmental decoder's are
below 1 with their margins' variant: A.8e, "Margins"). An occurrence keeps the best margin of the whole or glued readings it holds. The
margin is read once, when the word is read (`settle_s` after it), and a correction that changes only margins reads nothing again, so a
margin that falls later is not seen (W3OA: 484 of 12 639 falls from 8 nats or more to below 8 nats come more than 11 s after the symbol;
review 10). Only the variant `known_margin_nats` reads it.

**A neighbor's text.** By default (`skip_neighbors_text` 0) every symbol is read, `heard_from` or not. With the variant 1, in `read_track`, a *stretch* (consecutive symbols with a
`heard_from`, no word space among them; A.6, "A neighbor's text") is skipped when every symbol of it has a twin in the named
neighbor's `TrackText`: the same text, start and end each within `neighbor_twin_s` 0.025 s, and when it is whole words: a word's end
(word space, prosign, punctuation, the text's end) on either side. A word only partly heard from a neighbor is read whole (review 18:
on KC2LRC-2 W7QDM's own "TEST W7QDM" had its "TES" given to a track 27 Hz away, and the reading lost its role of sender). `words_of(symbols,
skip)` then ends a word at a skipped symbol and puts it in no word. A stretch with a symbol untwinned, or whose neighbor's track has ended (its text
gone from the stage), is read as the track's own. The track's own words stay whole; the neighbor's text is read on the neighbor's
track. A rereading after a correction is triggered by a change of text only, not of `heard_from`.

**Readings of the word at i,** which are alternatives; the evidence decides.

1. *Whole:* the word parses, unless it ends in CQ with a call before that, or in DE with a call before that and another, different call
   as the next word.
2. *Glued,* when it does not: the call twice, with at most one character behind; a keyword in front (DE, CQ, TU, TEST, QRZ); DE inside,
   recursively on the right; a keyword behind (DE, 5NN, ENN, TEST, TU, CQ, QRZ, KN, BK). The pieces are the word's characters in order
   (at most one left behind), and each piece's reading takes the start of its first character and the end of its last
   (`glued_pieces_timed` 1; at 0 every piece has the word's times), so the call glued to itself is two occurrences, as with a space.
3. *Stripped:* one or two characters of E, T, I off the front, while what is left parses, and with `x_as_tu` 1 (the default since
   `call-arb-1`) an X there too: TU run onto the call, T (-) and U (..-) with the letter space between them lost being X (-..-), as a
   runner ends each QSO, TU and his call (XW3RJ gives W3RJ, EXW3RJ both XW3RJ and W3RJ); one of E, T, K off the back.
4. *Joined:* from a word of at most 4 characters, up to five following words of at most 4 characters, not a prosign, keyword (K and R
   excepted) or refused word, up to 9 characters: each concatenation that parses. K and R may start a join, not a whole reading.
   When the first word is itself a whole reading, every concatenation made from it *covers* it (`CallReading::covers`: K4VR T gives
   K4VRT covering K4VR), unless the words added are only K and R (sign-offs and rogers: W4PM K, N4A K K, W4PM R R). A shorter
   joined reading (K 4VR T) is covered by none. An occurrence keeps the first cover it is given, and a re-read text (`reread_corrected`)
   never clears it: a correction that took the joined reading back leaves the link, which only ever lowers a fragment's side of the
   comparison where the longer call also stands on points of its own (conservative; not seen to matter on the replays).
   With `joined_periodic` 1 (the default since `call-form-1`), a concatenation C of n characters is refused when the text T of its
   words and those after it (up to 3n characters, stopping at a prosign) has a period p < n over a run from its start of at least
   max(n, 2p) characters (T[k] = T[k − p] for p ≤ k < that length): periodic text, whose call forms are phases of the period (L 9 6LL
   9 6LL: L96LL, T periodic in 96LL; N 5 JJ N 5 JJ: N5JJN). A call sent twice has its own length as period and stays (N5JJ).
5. *Segmented* (`segment_joined`, 1 by default since `joined-words-1`; `segment_word`): with 1, a word of at least
   `segment_min_chars` (6) characters that is no call itself and that rule 2 cannot split; with 2 (a variant), every such word,
   before rule 2, which then reads it only if no call on it is believed; with 0, none. The word w (at most 64 characters) is cut into tokens by a Viterbi over its split points that maximizes
   the sum of the tokens' gains G, log probability ratios in nats against noise text, whose characters cost ν(c) = e(c) ln 2 + ln 4.09375
   each, e(c) the character's count of dots and dashes (p(c) ∝ 2^−e over the 36 letters and digits and the slash, whose sum is
   4.09375: E and T 2.10 nats, I A N M 2.80, a digit 4.87):
   - a keyword κ (`is_keyword`'s 39 words): G = Σ ν(c) − λ_κ, λ_κ = −ln of its share of decoded words (the table in `callsign.cpp`:
     CQ 3.9, TU 4.5, K 4.2, R 4.4, DE 5.1, DX 8.7, AR 7.8 nats), so CQ gains 4.5 nats, DE 0.5, K −0.7, DX −1.0;
   - a stray character: G = −`stray_nats` (2.30);
   - a new call C of m characters (`parse_call`, not tiny, not a shorter string twice or more, 3 to 12 characters):
     G = Σ ν(c) − m ln 36 + φ(m) + ln π(m) − `call_nats` (2.4), φ(m) = −ln of the share of random strings of m letters and digits that
     parse (`call_form_nats`: 2.29, 1.45, 1.54, 1.85, 2.40, 3.66 and 5.05 nats for m = 3 to 9, derived, remade for the grammar of
   `call-form-2`, which takes the prefix-digit forms and the words of CW; 10 to 12 reuse 9's, a heuristic; φ counts the forms reported
   from a list only too, a modeling choice that costs every regular call 0.72, 0.18, 0.14 and 0.13 nats at 3 to 6 characters against
   the table of `call-form-1`, with no effect on the three replay sets), π(m) the share of calls of m
     characters (`call_length_nats`: 0.63%, 20.0%, 41.1%, 37.3%, 0.65% for 3 to 7, 0.07% each for 8 to 12): K8DX gains −0.86 nats,
     NA9M −3.63;
   - the call in the cache again: G = Σ ν(c) − `repeat_nats` (1.61): K8DX 14.4 nats;
   - after a DE, a call gains `de_before_nats` (2.77) more; after a call, a DE gains `de_after_nats` (1.0), a K or KN `sign_off_nats`
     (1.5).

   The cache (a cache model, Kuhn and De Mori 1990, of one entry) holds the last call; a keyword keeps it, a stray empties it, so that
   copies apart in garbled text are no repeat. Only a string found twice without overlap has a cache state of its own: the states are
   (place, cache, last token DE, call or other). A call on the best path is *believed* when its evidence E, summed over its copies,
   reaches `believe_nats` (2.5): each copy after the first its gain, each DE before a copy 2.77, each DE after one 1.0, each K or KN
   after one 1.5 unless the call with it is a call too (AK4J K and AK4JK: an ambiguous K is no evidence; `ambiguous_sign_off` 0), a
   copy right after a keyword other than DE with which it is a call nothing (TTW7QKW7Q: W7Q K W7Q, or W7Q KW7Q; `ambiguous_repeat` 0,
   since `call-form-1`), and
   the path's whole gain reaches `word_min_nats` (0: the word at least as likely the protocol's text as noise). A first copy's gain is
   no evidence, since noise text holds many call forms. So a repeat is always enough, a DE before is, a DE after only with an
   unambiguous K. Each copy of a believed call is a *glued* reading (1 point) with its own characters' times, as in rule 2, and
   the role (below) of the nearest path tokens outside it and its other copies, stray E, T, I skipped, and the words around at the
   word's ends. Examples (`JoinedWords` tests): DEK8DXK8DXK8DXK is DE K8DX K8DX K8DX K, E = 2.77 + 2 × 14.4 = 31.6 nats (K8DXK is
   a call); CQCQPOTADENA9M is CQ CQ POTA DE NA9M, E = 2.77; N5JJDEWA0QZK is N5JJ DE WA0QZ K, N5JJ 1.0 (not believed), WA0QZ 2.77;
   CQCQDXCQDXCQDXDEK8DXK8DK reads K8DX once (the last copy, K8DK, is a new call of no evidence).
   With `segment_alternatives` 1 (the default since `joined-words-1`; 0 reads the segmentation's choice alone, glued), a believed copy whose last E, I or K the
   segmentation cannot place gives both readings, each *stripped* (0.5 point), for the stage's repeats and arbitration to choose
   between: a call ending in E, I or K that is a call without it (73DEWN4ATI: WN4AT and WN4ATI), and a call followed by a stray
   beginning with E or I, or by the keyword K, that is a call with it (K5NUDEAK4JK73: AK4JK and AK4J). The segmentation's own choice
   is emitted last, so that on a tie the stage's rule for near calls that begin within 1 s (the later made wins) leans to it. Each
   reading takes its own characters' times.

**Role,** from the nearest words outside the call and its repeats, skipping E, T, I, EE, TT, IE, EI, ET, TE: next word DE, *named*; previous word DE, *signed*; next word
in {5NN, ENN, 599, UR, NR, RST, GM, GA, GE, GN, FB, TNX, TKS, R, SRI, AGN}, *named*; previous in {CQ, TEST, QRZ, CQCQ, TESTTEST, POTA, SOTA,
DX, UP} or next in {K, KN, AR, SK, TEST, QRZ, CQ, UP, <KN>, <AR>, <SK>, <KA>}, *signed*; else unknown (*bare*).

**Reading.** On each whole second of stream (a whole number of seconds, so that how often the clock is given does not change what is read), the words ended
by that second less `settle_s`; all of a track's words when it ends. A correction that changes a symbol behind what has been read (`reread_corrected`; an unchanged tail of a correction does not count) sends the reader back to the start of
the word it falls in, so the new text is read when settled. Nothing already read is taken back: a stream that takes over can be worse there than the one it replaces (at settle_s 1e6 s, reading only final text, real 40 m loses E51MWA). Text is kept back to `history_s`, and eight words at least.

**Evidence.** A reading overlapping an occurrence of the same call in time within `same_place_hz` betters it (points, role) and is not
counted again, so text read twice adds nothing. *Local* points at f: the sum over occurrences within `same_place_hz` of f and within `window_s` of now; a whole or glued reading 1, a joined one `joined_points`, a stripped one `stripped_points`. Framed: any of them had a
role, or the same word was next to it (`doubled_is_framed`). *Pooled* points (`pool_places` 1, the default): over every occurrence o within
`window_s`, leaving out o when the call is held at o's frequency (the arbitration below, with supports taken within `variant_hz` of o), and
counting each chain of occurrences connected by overlaps in time once, at the largest of their points, wherever they lie (a sweep in order of start, so the result does not depend on the order of the tracks); framed, named and sure as
any counted o. A candidate's first time is its earliest reading, which a correction can move back.

**Report test** (a named word no exception: CW Skimmer's practice, provenance below; a special form, two or more digits as in
W100AW, and a word of CW (N1MM, DJ5NN), from a list only; with `unusual_listed_only` 1, the default since `call-form-1`, an unusual one, four letters after one digit as in VK3FABC; and with `prefix_digit_listed_only` 1, the default since `call-form-2`, a prefix-digit one (C6AGU): a reading of any of these forms off the list is dropped before it reaches a candidate), on the pooled points when `pool_places` is 1 (the default: the same test then reports and withdraws, so a row does not come and go; review 15), else on the local points; in order. First, since `call-short-1`, an unlisted call of three characters (`is_short_call`: a letter, a digit, a letter, the 1x1 form of any series: N4A, W6S, G2M) needs at least `short_points` (3) on the points tested, or `short_points_signed` (2) when an occurrence counted holds a reading with the role *signed* (`Occurrence::sender`; `Support::sender`), whatever it would pass on below (a tiny call, a named word); 0 leaves the bar to the rules below. And with `cut_number_form` 2 (the default) an unlisted call of a cut number's shape (`is_cut_number_form`: a digit 2 to 9, then A, N or T, then the call's digit, the pair not 9A, 7N or 8N: 2T7A, 3N4D, 5A3BOB, 4A7A) needs in the same way at least `cut_number_points` (5), or `cut_number_points_signed` (2) with such a signed occurrence; with 1 its readings off the list are dropped before they reach a candidate, as a special form's are; with 0 it is any call. The first digit is 2 to 9 by derivation: a member's number sent in cut digits has its 1 and 0 sent as A and T too, so it never starts with either digit. *Counted*: the pooled test counts an occurrence, and its role, only inside the window (`window_s`) and where no variant holds the call (`held_at`), so a signed reading at a place where a variant holds the call does not lower the bar; the List's *Signed* (`CallRow::framed`) counts every reading, held or not, and is another thing. A tiny call: `tiny_points`. Any occurrence that is a whole or glued word with the role *named*: `points_named` (2 by default, so no exception; 1 a variant), with a list for a listed call only (`named_listed_only`). On a list: `points_known` if framed, or, with `known_margin_nats` above 0 (a variant; 0 by default), if an occurrence of 1 point has a margin of at least that (*sure*); else `points_known_bare`. Off it: `points_framed` if framed, else `points_bare`.

**Arbitration.** Against every other candidate with a reading within `variant_hz` of this one and whose occurrences span to within `variant_s` of the judgment time on either side (now, or a loser's last reading, below), by base-call edit distance d,
the limit being 2 for a shorter call of 4 characters or more, else 1, and 2 also for a *short pair* (`short_variant_ratio` not 0): one of the two has one letter after its last digit and both have the same characters up to their last digit (K1T and K1BX); a call that is the start of the other, or starts with it, counts as d = 1 at any distance (`prefix_held`). A rival beats a call if both are on or off the list and its points
exceed its need (1 at d = 1; at d = 2 `short_variant_ratio` for a short pair, else `far_variant_ratio`) times the call's, or at d = 1 tie: with an earlier first reading, unless the two first readings are within 1 s (one word, heuristic), when the one seen later wins, being what a correction made of the word; a listed rival beats
an unlisted call at a third of its points; an unlisted rival a listed call at three times. A *flat* occurrence (`flat_stripped`
1, the default since review of `call-arb-1`: one whose points are a stripped reading's with an X taken off the front, `x_as_tu`;
2, a variant, also E, T, I) counts once, not three times, in a listed call's points against an unlisted one: listed weight
3 (p − f) + f for p points of which f flat, so a listed twin read only off an XF4ABC does not take the real call's row. *Covers* (`joined_covers`), for a pair one
of which starts the other: an occurrence of the shorter (the fragment F) and an occurrence of the longer L that covers F, overlapping
in time within `same_place_hz` and both within `variant_hz` and `window_s`, are one transmission read two ways. With 3, the default
since `call-arb-1`, F's points in such transmissions are left out of F's side of the comparison when L's points there less its
covering occurrences' points reach `covers_own_points` (1.5: more than one reading, two joined at least; heuristic), so the
transmission counts for L alone; else nothing changes. With 1 they are left out
always; with 2 both sides lose theirs, unless both are then at 0, when neither does; with 0 both count, F at 1 and L at
`joined_points`. The report test is unchanged, but the pooled points change with the comparison: `pooled_support` leaves out the
places where `held_at` holds the call, so a fragment held there under the covers loses those occurrences from its pooled points. A held call is not reported. Pooled (the default): a reported call held here, or beaten here by a call being reported, leaves when its pooled points no longer pass, judged at the earlier of now and its own last reading: its occurrences within `window_s` before it, against rivals' occurrences within `window_s` of it on either side (so a rival's later readings count up to `window_s` after it). At the stream's present the two-sided window is the one-sided one: occurrences ahead of a reading's end lie within `history_s` (200 s, re-read corrections) or a second (other tracks), inside `window_s`. Unreported candidates are pruned `window_s` + `variant_s` (3 600 s) after their last reading, the longest a rival can count for a judgment (derived). Heuristic, motivated by the evidence rule (a reported call stood on readings that have since aged out of the window, and a rival heard over `window_s` after them is not evidence against them); chosen by measurement. Judged at the present, the 2026-11-17 80 m recording's WK0B (five signed readings to 1 045 s) was taken off at 2 647 s by W5FB's three at its place (stage-aging, 2026-11-18: on the seven 2026-11-17 recordings one confirmed call more, no other change). With `pool_places` 0: if it was reported, it leaves when the rival's best at one place is above its own best, or when its readings more than `variant_hz` from this place, taken at any one place of their own, do not pass the test above, a word named not counting there (`withdraw_own_place`); else it stays. A reported call withdraws the reported calls it beats by the same test, its best at one place against theirs.

**Order and clock.** Tracks are read in the order of their ids, so the result does not depend on a library's hash. The app's stage gets the clock among the events: `DisplayFeed` puts a `StreamClock` in the updates at the first spectrum frame of each whole second, and `MainWindow` applies them in order, so the stage reads at the same place in the events as `kz4ap-calls` whatever the display's pace.

**Row.** The place is that of the reading of the highest rank, latest on a tie: rank = 2 x (signed 2, bare 1, named 0) + (a whole or glued
reading 1). `call` is the base, or the most common form with a slash seen at least twice and more often than any other. A candidate not reported
and older than the longest window is dropped, each 30 s of stream.

**The list and the tools.** `kz4ap-skimmer --known-calls PATH` takes MASTER.SCP's format: one call a line, lines starting with #, ; or !! (a directive to logging programs, MASTER.SCP's `!!Order,1,1`) skipped, case ignored,
whatever follows white space ignored. A file with a NUL byte (a binary one: CW Skimmer's MASTER.DTA) or with no line whose first word has
a call's form is refused with a message naming the file, since it would match nothing and, with `named_listed_only`, refuse every call
named once (W3OA: 107 calls with no list, 74 with MASTER.DTA given as it is, before the check; review 10); the file is read in binary
mode. The app and `kz4ap-calls` print to standard error how many calls they read. `--calls PATH` writes the list as `calls_csv` at the stream's end, with the absolute kHz when the
center is known and `first_utc`, `last_utc` when the recording's header dates it (A.1). `kz4ap-calls --known PATH --out PATH
[--center-khz KHZ] [--mirror] [--log PATH | --replay PATH] [--candidates PATH] [--readings PATH] [--set NAME=VALUE]`; the log keeps each
symbol's probability when below 1 (a fourth element) and each update's speed (`"w"`, WPM; A.9b), the readings their margin (nats; −1: unknown). The log holds a clock line every 0.5 s and at each whole second's first frame, where the stage reads, so that a replay reads the same text at the same point of the events as the run (since `joined-words-1`); a replay of an older log reads up to 0.5 s late: on the seven 2026-11-17 recordings it changed 8 of 408 rows (one call added, one read as another, point totals, counts, a framing, frequencies within 0.4 Hz), the counts of window-confirmed, misread and unknown calls unchanged. `training/kz4ap_proto/calls_score.py compare` reads a run and
another program's list and reports calls in both, in each only, counts, and pairs one letter apart, and keeps of a Reverse Beacon Network
day file only the spots of given skimmers, a continent, a UTC window, a span and a mode (`--spotter`, `--continent`, `--from`, `--to`,
`--khz`, `--tx-mode`); `truth` reads a run and a recording's labels and
reports precision and recall, and how many calls found are within 100 Hz of their station. Its reader takes ADIF, telnet spot lines, CSV with a
header (`dx` ahead of `callsign`, as in the Reverse Beacon Network's files) and, failing those, the first word of a call's form on each line,
and prints which it used: CW Skimmer's export format is not known here. Calls compare by base call. A telnet spot's word is kept as it stands where the reader's grammar has no form for it (D4C, LZ1SPACE; the Python reader has no prefix-digit form): a spot is the spotter's test, and dropping it would flatter this decoder.

**Signed (`is_signed`).** A row is *signed* when `CallRow::framed` is true: some reading of the call, at any place and time since its
first, had the role *signed* (`Role::Sender`, the rule above), whether or not a variant held the call where it was read; once true it
stays. It is not the report test's *framed* of an occurrence: a call sent twice, or one named by another, is framed there but not
signed. A heuristic, a choice made here (2026-11-18): it reuses the Role rule unchanged, since its word lists are what the stage
already calls signed (the list's How column showed it before); every reading counts, not only those the pooled points count, so that a
row never leaves the signed list again. The cost (a conjecture, not measured): a reading that signs at a place where a better-supported
variant holds the call still marks the call signed, so when that reading is a misreading of the variant's own signing (DE K1ABD read for
DE K1ABC), the misread call shows as signed wherever it is reported.

**Heard at its place (`CallRow::heard_s`).** The end, s of stream, of the latest occurrence within `window_s` that holds a reading
of the call not in the role Named, within `same_place_hz` (300 Hz) of the row's `freq_hz`; −1 for none. `last_s` counts every reading,
named ones too, while `freq_hz` stays where the best reading put it (signed, then bare, then named), so a call named elsewhere moves
`last_s` and not its place; the app's calls on the waterfall go by `heard_s`. Bookkeeping only: nothing the stage decides reads it.

**The app's modes and list.** `Startup::start` (`app/src/main_window.hpp`), the controls of the toolbar's third row; they are kept
between runs with the app's other settings (README, "Settings kept between runs"; a saved list is read from disk only).
- *Mode:* Skimmer, the default, runs the stage. Decoder only (`--decoder-only`) builds none (`CallListModel::stop`): no `CallSearch`
  exists, no event reaches one, and the call list, its controls and its status are hidden; the waterfall, the track list and its text are
  as in skimmer mode, since the stage changes nothing they read. Back in skimmer mode a new stage starts, reading the tracks born from then
  on: a track's earlier text is not given to it, since a correction's `from_index` counts from the track's first symbol and a stage begun
  mid-track could not place it. In decoder-only mode the known-call list's actions are refused, from the controls (hidden) and from
  the automation steps `known=` and `download` (the status says so). The command line sets the start: `--decoder-only` refuses
  `--known-calls` and `--signed-only`, which configure a list it does not have (the steps `mode=skimmer`, then `known=`, `list=signed`
  set them later); `--calls` is an output and is taken, written at the end as the list then stands, the header alone in decoder-only mode.
- *List:* all calls, the default (every reported row, signed, bare or named, as CW Skimmer lists every call; the owner's ruling,
  2026-11-18), or signed calls only (`--signed-only`): the rows with `is_signed`. The filter is the list's, not the stage's: the stage
  reports the same rows either way, and `--calls` writes the rows shown. The status bar reads "Calls N signed of M".
- *Known calls:* none by default, and none ships with the program. Open list... and `--known-calls PATH` read the user's file in
  MASTER.SCP's format the same way (`read_known_calls`: the file through memory into `KnownCalls::read`, so any path the system takes
  is read). Download MASTER.SCP (`KnownCallDownloader`, `app/src/known_call_downloader.hpp`) asks supercheckpartial.com, as its guide
  for developers says (read 2026-11-18):
  1. GET `https://supercheckpartial.com/api/v1/files`, a JSON array of name, size, etag, modified; an answer over 1 MiB is stopped.
  2. GET `https://supercheckpartial.com/downloads/<name>` for the entry named MASTER.SCP (case ignored; a name other than letters,
     digits, `.`, `-`, `_` is refused, so a listing names no path or query), with `If-None-Match` set to the ETag kept from the last
     download when the copy is still there. An answer over 16 MiB is stopped.
  3. HTTP 304: the kept copy is read; if it no longer reads, its ETag is deleted and step 2 runs once more without `If-None-Match`, so
     a damaged copy cannot be held in place by its 304s. HTTP 200: the body's size must equal its Content-Length, or with none the
     listing's size, else it was cut short (a closed connection ends an answer with no error) and nothing is kept; then it is read as a
     list (a page of HTML or a file of no calls is refused, and the list in use stays), and only then saved atomically (`QSaveFile`) to
     `QStandardPaths::AppLocalDataLocation`/MASTER.SCP (Windows: `%LOCALAPPDATA%\kz4ap-skimmer\MASTER.SCP`), its ETag beside it in
     `MASTER.SCP.etag`.
  Every request carries exactly these headers, checked by test against a local server: `Host`, `Connection` (Qt's), `User-Agent:
  KZ4AP-Skimmer/<version>` (the program's version, 0.1.0), `Accept-Language: *` (set, so that Qt does not send the system's languages),
  `Accept-Encoding: identity` (so that Content-Length is the file's size), and on a conditional request `If-None-Match`. No cookie is sent
  or kept. A redirect is followed only to the same scheme, host and port (`SameOriginRedirectPolicy`); any other is refused. Limits,
  heuristic: 16 MiB for the file (MASTER.SCP was 359 821 bytes on 2026-11-18), 1 MiB for the listing (2 166 bytes), and a request that
  moves no data for 30 s fails as such; each answer is checked against its limit while it comes and again when it ends. The program
  reaches the network only on that button (or the automation step `download`), and makes its network manager on the first press. The
  status beside the buttons shows the list's size and origin, or why the last try failed. A new list, or none, takes effect at once (`CallSearch::set_known`): every call
  held is marked listed or not, its row too, and is judged so at its next reading; reported rows stay until a reading takes them back,
  and a special form (W100AW) refused before is not read again.
- *CPU:* the stage runs on the GUI's thread. The report's `call_stage_cpu_s` is that thread's CPU (`thread_cpu_precise_seconds`) inside
  the stage's `apply`, `advance` and `finish` and the list model's refresh, painting not included; `call_stage_ms_per_channel_s` divides it
  by the stream's channel-seconds. `process_cpu_ms_per_channel_s` is the whole process's.

### Provenance

**What came from where** (all *secondhand* through `docs/research/decoder-survey.md`, the RBN's 2021 article and manta's
`wiki/pages/spot-validation.md`, read 2026-10-31; no source's code was used).

- *CW Skimmer and the RBN.* A call must pass a pattern check and repeat: at its Normal level 1 for a watch list, 2, 3 or 5 by how common the
  pattern is, and it is not re-spotted near a frequency within 10 min. Taken: the idea, list 1, framed 2, bare 3, the 10 min as a first
  window. Not taken: `patt3ch.lst`, which is unpublished in form and comes with their program; the requirement of CQ or TEST, RBN's alone.
- *manta.* Refusal of exact non-call words only (`DJ5NN` stays), unallocated prefixes refused, a list that does not exempt from context, a 300 Hz bucket,
  arbitration by withholding a better-supported rival's confusable. Taken as these. Not taken: repetition counted only across messages 3 words or 60 s apart
  (CW Skimmer counts repeats in its buffer; sent twice in a row counts here as context); manta's beacon exemption, a cautionary 29 false spots in a night.
- *ITU Radio Regulations.* 19.68 and Appendix 42 as read on Wikipedia's ITU prefix table on 2026-10-31, the three additions from amateur use (*sourced*).
- *The design spec, section 6:* never match against the Reverse Beacon Network (owner, 2026-10-14). Nothing here reads from it.

**Chosen by measurement,** replaying the logged events of the generated contest (`w7_contest`, 280 stations), `w12_cap` (300), `w11_pairs` and `band192k_100`, and of 30 min
and 62 s of real 40 m; the numbers are in the decision log, 2026-10-31.

- `settle_s` 10 s: at 2 s the 30 min of real 40 m gave 7 calls against 10 at 6 s (it read text the decoder had not yet corrected). On the final decoder 10 and 15 s read alike; 20, 25 and 30 s lost WA0QZK and gained one true call of 280 on the contest. Waiting does not reach the longest corrections in any case: see `reread_corrected`.
- `reread_corrected` on and `history_s` 200 s: on real 40 m 2 025 corrections changed text, reach median 2.6 s, 99% 36.4 s, maximum 199.2 s; on the contest maximum 106.7 s (measured on the final decoder). 53 reach past 20 s on real 40 m. One of them turned OL1TM OL1TM into ZL1TM ZL1TM, 50 s after it had been read. The maximum seen before the leak floor, 164.9 s, plus a margin set `history_s`, a heuristic; the longest now reaches just under 200 s. It cost one false call on H2, seed 2 (N5PX and DE, glued, in corrected text; `prefix_held` now takes it away), and gained one true on the contest.
- The tie at 1 s: chosen for that word, tested on all the generated recordings, where it changes nothing. A heuristic on one case of real 40 m.
- `same_place_hz` 300 Hz: 100 Hz loses a partner named at another's place 210 Hz away; 300 and 500 Hz gave the same lists.
- `window_s` 1 800 s: 600 s missed an ID repeating every 255 s on real 40 m; no misreading came with 1 800 s on 30 min, with points added at one place (300 Hz). Valid at one place for real bands as busy as those recordings. Since `rob-1` the same window bounds the band-wide pooled points, a use it was not chosen for: there real30 (the same 30 min) gains the misreadings K0HBN, K6QO, K6T and IK0I, read the same way at several places within it. A shorter band-wide window was not tried; the window's validity for pooling is not established.
- `points_bare` 2 (3 at first): on the final decoder 3 refused EA1DR, read exactly twice 31 s apart, and the two spellings under which real 40 m has IK6QOP; 2 added no false call on any generated recording and 3 true on the contest with the re-read. On real 40 m it also adds one misreading (IK0S). Before the filter change 2 cost one false call on `w12_cap`; it no longer does. With `points_framed` at the same value, framing matters only with a list. The bar is about as many calls as CW Skimmer on a real recording, whose Normal level asks 2 repeats of a common pattern.
- `points_framed` 2: 3 lost 28 true calls on the contest to take away one false call; 1 gains 20 true for 22 false on the contest, 1.5 gains 2 true on the contest for 5 false on the two QSO groups. One reading is for a list, as in CW Skimmer.
- `points_known_bare` 2: 1 tripled false calls with a list.
- `variant_hz` 150 Hz: 500 Hz held back a true station 237 Hz from a call one letter away; real recordings read alike. 300 Hz on the final decoder: 3 false calls fewer on H2, 1 true call fewer on the contest; not adopted, a trade that favors precision.
- `far_variant_ratio` 2: at 3, a call read twice next to six readings of the true call stayed; 1.4 changed nothing on any recording.
- The five rules of review 5, adopted together, each chosen looking at the contest and the 30 min of real 40 m, the same overfitting as the settings above; the crowded band, the 100 stations, G2 and the short real recordings are the checks. Alone and together: `calls-rules.csv`, results record. `points_named` 1 is the largest gain on the contest, 12 more true and 6 more false, and the source of most of the false calls gained. `joined_points` 0.75 brings KF0YUX on real 40 m; 1 lists false calls there. `tiny_points` 2 reads SM5NU on the contest and EA5MEE on the 100 stations. `prefix_held` takes H2's false calls down (K5N beside K5NXY). `withdraw_own_place`: a call doubled and signed at 1 kHz stayed off the list for a misreading at 2 kHz, 60 s later; a simpler form, keeping any call with support elsewhere, kept K0HBN, K0HBT and K6QO on real 40 m's list.

**Chosen in `call-2` against CW Skimmer 2.1** on the four 2017 recordings, replaying the logged events (the text the same in all): one
judge for both programs, a call *confirmed* if the Reverse Beacon Network spotted it inside the recording's UTC window and 192 kHz span, a
*misreading* if one edit from a call it spotted that day. Tuned on W3OA and KC2LRC-1; KC2LRC-2 and N2GZ held out, though every tuning run
scored and printed all four (review 10). The numbers below are confirmed / misreadings, the tuned two together and then the held-out two
(CW Skimmer 102 / 23 and 104 / 30); `docs/decision-log.md`, 2026-11-04, has each recording. The list is CW Skimmer's own `MASTER.DTA` of
2018, its 40 035 calls taken out of the binary file as text for this measurement, the list it ran with.

- *Standard practice.* CW Skimmer standalone validates against a watch list, MASTER.DTA and pattern tables, with repeats needed by validation
  level (RBN 2021; CW Skimmer's manual); the RBN's own busted-call filters (Skimbusted in AR-Cluster 6, after CT1BOH) mark a spot busted
  when a call one character inserted, deleted or substituted from it is already valid within 0.1 kHz, and valid after two spots within
  0.2 kHz (secondhand, through the RBN's pages). The arbitration within `variant_hz` is that rule's form; the list is CW Skimmer's.
- `named_listed_only` 1, with the list: 105 / 42 to 102 / 29 tuned, 119 / 46 to 117 / 26 held out. Without a list the named word stays: 3
  confirmed calls of W3OA and 9 of KC2LRC-1 rest on it, as do 14 true calls of the contest. With the generated lists on the contest, 70% of
  truth: 225 / 12 to 222 / 8; 100%: 228 / 11 to 228 / 7; none of truth: 216 / 15 to 206 / 11 (a list that lacks the callers loses their named
  words).
- `short_variant_ratio` 2: K1T and N1T (W3OA), K5K and K7D (KC2LRC-1) go; with the list 102 / 29 to 102 / 26 tuned, 117 / 26 to 117 / 25
  held out; without one 97 / 49 to 97 / 45 tuned, the held-out two unchanged; the generated recordings unchanged. 3 keeps one of KC2LRC-1's.
  Distance 2 for every call of three characters was not tried.
- `is_locator`: L96UD of EL96UD (KC2LRC-1, SEQP's exchange) and two calls of KC2LRC-2, one a misreading, and with the list DM04JG and DM95CB, which MASTER.DTA
  holds; without a list 108 / 52 to 108 / 51 held out.
- `known_margin_nats`, off. At 8 nats, with the margins' variant at 16 nats: +6 / +5 tuned but +6 / +9 held out (to 108 / 31 and 123 / 34),
  on the line of `points_known_bare` 1 for every listed call (+11 / +13 tuned, +10 / +13 held out), so it does not separate on data it was
  not tuned on (review 10). Its sweep (1 nat 108 / 33, 4 nats 108 / 32, "16 nats as 8") was measured before a symbol with no rival read the
  ceiling through a float; since then no margin exceeds 15.94 nats, so 16 nats is the rule off, and 8 and 15.9 nats read W3OA alike.

**Chosen in `rob-1`** against CW Skimmer 2.1 on the four 2017 recordings, with the judge above, after a diagnosis of every call CW
Skimmer lists that the RBN confirms and we did not, and of every call we list that it does not confirm (decision log, 2026-11-15). The
diagnosis read all four recordings; W3OA and KC2LRC-1 stay the tuned two, KC2LRC-2 and N2GZ the held-out two by name only, since their
cases (N5ZC, GM3YOR, W7QC) are among those the rules were made from. Numbers: calls / confirmed / unconfirmed, the four together, `main`
(d44c7d6) then `rob-1`, CW Skimmer 406 / 206 / 200; logged events replayed, so the text is the same in all.

- *Standard practice.* CW Skimmer at its Normal level asks 2 to 5 repeats by pattern and 1 for a listed call, and spots no call from one
  word otherwise (RBN 2021); the evidence rule here (one transmission, one occurrence; independent readings add) is the same idea, a count
  of independent decodes.
- `points_named` 2 (no exception for a named word): 506 / 205 / 301 to 322 / 176 / 146 alone. Of the 128 calls that rested on one named
  word, 18 were confirmed, 34 spotted by the RBN elsewhere that day, 44 judged misreadings, 32 unknown. The contest 221 / 5 to 210 / 1,
  its fresh seed 236 / 3 to 228 / 0, the strong pairs 40 / 2 to 39 / 1 and 42 / 0 to 40 / 0, H2 seed 2 40 / 1 to 40 / 0. 1.5 points (a
  named word and half a point more), with pooling: 421 / 200 / 221 against 2's 417 / 200 / 217, no better. Chosen as CW Skimmer's documented practice, not tuned.
- `pool_places` 1, a heuristic motivated by the evidence rule (independent readings add), not derived from it: a misreading the decoder makes the same way at several places is not independent (real30's K0HBN), and the scope, the whole band over `window_s`, is a choice. With the above, 322 / 176 / 146 to 417 / 200 / 217; the calls it adds are 24 confirmed, 41 spotted by the RBN that day
  (callers such as N4NO, N4QM and VE3OU, read in 10, 9 and 6 bins of 300 Hz), 14 misreadings, 17 unknown. Alone, without the named rule's change,
  506 / 205 / 301 to 550 / 217 / 333. The generated recordings read alike with and without it (their callers do not move). The
  held-place exclusion and the withdrawal by pooled points came from KC2LRC-2's N5ZC (confirmed by 21 skimmers): named once on N5ZO's
  track one edit away and read three times 5 kHz away, it was withdrawn when N5ZO's best passed its own.
- `glued_pieces_timed` 1: K9BGL (W3OA, confirmed, CQCQTESTDEK9BGLK9BGLK) and KC9AWL (KC2LRC, spotted that day); 506 / 205 / 301 to
  509 / 206 / 303 alone; the generated recordings unchanged.
- Together, by pair: tuned 250 / 97 / 153 to 215 / 92 / 123 (CW Skimmer 196 / 102 / 94), held out 256 / 108 / 148 to 202 / 108 / 94
  (210 / 104 / 106). With CW Skimmer's MASTER.DTA of 2018: 444 / 219 / 225 to 479 / 226 / 253 (pooling adds unlisted calls read at two
  places; of the 15 misreadings and unknowns it adds in the replay, CW Skimmer lists 6). The owner's file 42 / 21 / 21 to 41 / 21 / 20.
- *Tried and dropped:* a fragment beside a whole call (5LN K5LN, three characters or more with the digit) read as a repeat at 1 point
  (+2 confirmed, +7 misreadings, +11 unconfirmed; at 0.5 point nothing changes); `joined_points` 1 (+4 confirmed, +16 unconfirmed; the
  contest +1 true); a transmission read as two near calls counted for the better reading only (`shadow_alternatives`: 200 to 199
  confirmed, misreadings 54 to 57, OH2J false on H2; it did take K0HBN off real30).

**Heuristics not tuned:** `stripped_points`, the keyword sets, the words of CW (listed only) and the refused characters.

**Not taken:** `near_factor` (a call's near variants adding to its points: 217 true and 3 to 11 false on the contest, but 24 to 76 rows on real 40 m); a call before UP at 1 point (does not bring E51MWA back when the word is lost); CW Skimmer's 1 repetition for a listed call on real MASTER.SCP (1 true for 3 false on real 40 m; review 5). In `call-2`, on the two tuned 2017 recordings without a list (int-1's rules 97 / 49, `call-2`'s 97 / 45): a word named once to pass only at a margin of 0.05, 0.1, 0.2, 0.5 or 1 nat (0.5 nat: 95 / 36, and on the contest 216 / 4 against 220 / 6; 0.05 nat: the contest 219 / 5, but W4BOH, confirmed, lost on W3OA); a whole word of margin under 0.2, 0.5, 1 or 2 nats scoring 0.5 or 0.75 point (0.5 nat at 0.5 point, without the short pairs: 92 / 29, the contest 206 / 3); no word named alone at all (85 / 22, the contest 210 / 2; taken in `rob-1` with pooled points, above); a call named once held while a reported call one edit from it stands anywhere in the band (96 / 42: one confirmed call fewer, three misreadings fewer). Each takes confirmed calls with the misreadings: the margin of a correct named word is often under 0.5 nat too (W4BOH 0.00, N1XI 0.22), and wrong ones can be sure (K8VC, W6UM).

- `skip_neighbors_text` 0 (copy-2, review 18): with attribution on, every call and every field of it (frequency, points, count, heard,
  framed, times) as with it off, on the five real recordings (by construction: the stage reads no `heard_from`). The variant 1 with the
  twin and whole-word rules gained and lost no call there but changed 12 calls' fields (9 frequencies, among them K7D 11.6 kHz and W1UJ
  15.6 kHz; 5 framings, K1LT confirmed on N2GZ among them; 2 point totals): a skipped word's reading counts on the neighbor's track
  only where that track's words around it match, and otherwise the call's best place and frame move. Before the whole-word rule it
  also took W7QDM's role of sender (KC2LRC-2, "TEST W7QDM" with "TES" given to a track 27 Hz away). Off: chosen by measurement; the
  stage already counts readings that overlap in time within 300 Hz once.

- `segment_joined` 1 and `segment_alternatives` 1 (`joined-words-1`, 2026-11-19): on the seven 2026-11-17 recordings, 42 calls the RBN spotted in the
  window and the defaults did not report appear inside a decoded word of 7 characters or more within 0.5 kHz of a spot (about 35
  stations; the gate before building). On their decoded text (the defaults' decodes again, with the clock lines: calls equal to the
  earlier decodes' and the replays equal to the decodes, byte for byte), mode 1 gave 168 / 83 / 103 (window-confirmed / misread /
  unknown) against 160 / 82 / 102: KD2ITZ, KG5VNQ, NA9M, W3GTR, N8CKU, CO3ET, KE2EH and K8DX in the window, WA4BAM and WA9TOP that day,
  one misreading (KD5Z, from S73DEKD5ZCQ), one unknown (WA7WKY, from DEWA7WKYK), none lost; `believe_nats` 3: 166 / 82 / 102 (NA9M and
  KG5VNQ, a DE before alone, go). Mode 2: 168 / 85 / 102 (W4PM and WA5DT for K9IHK and WA5DTK, which the RBN also spotted). Around the
  chosen costs (replays of the earlier logs): `believe_nats` 2.0 the same, `stray_nats` 1.5 and 3.0 the same; `word_min_nats` −5
  167 / 86 / 103, +3 168 / 83 / 102. Held out, replaying logged events: the three 2026-11-18 recordings (mostly the CWT contest; RBN day
  file of 2026-11-18) 576 / 309 / 212 at the defaults, 576 / 313 / 215 with mode 1, 576 / 312 / 212 at `believe_nats` 3 (AK4JK lost as
  AK4J K from K5NUDEAK4JK73; misreadings WN4ATI from 73DEWN4ATI, W7Q and B3FW beside KW7Q and KB3FW; unknown W3QRE from ES73DEW3QRE,
  KC4HCH, AC0BU); the four 2017 recordings (review 18's logs) 200 / 56 / 54 to 200 / 56 / 55 (L99E from a grid square, EL99, sent
  over and over). By the RBN it is no win held out. With `segment_alternatives` 1 as well (one iteration, no value retuned):
  2026-11-17 168 / 83 / 103 (exact; WA9TOP, spotted that day, lost: GMDEWA9TOPK's readings split between WA9TOP and WA9TOPK);
  2026-11-18 576 / 310 / 214 (AK4JK kept; WN4ATI, B3FW, AK4J and W3QRE gone; W7Q, KC4HCH and AC0BU stay); 2017 200 / 56 / 55. The
  rule first set for a default (window-confirmed calls up on 2026-11-17, and on no held-out set more than one new misreading per
  window-confirmed call gained) failed on 2026-11-18 (none gained, one new misreading, W7Q). It was set aside on better evidence, and
  both options are on by default since `joined-words-1`: (1) the RBN cannot see this variant's gains, since it spots stations that
  call CQ: 99.9% of its non-beacon spots on 2026-11-17 are type CQ (423 137 against 305 DX), and "DE call" joined words come from
  contacts, where a station signs without calling CQ; (2) every call the variant adds or changes was looked up in the FCC's amateur
  license file (l_amat.zip of 2026-11-15, HD.dat: 822 120 active calls, 1 607 238 ever issued): active licenses are the 8
  window-confirmed calls gained, KD5Z (judged a misreading by the RBN, from S73DEKD5ZCQ), WA7WKY, KC4HCH, AC0BU, AK4JK, WA9TOP,
  WN4AT, AK4J and KB3FW; never issued are W7Q, W3QRE and WN4ATI (the last two gone with the alternatives), and L99E is a grid square (gone since the periodic-run test, below).
  Net against the old defaults, about 12 likely real calls for 2 false ones (W7Q, L99E; 1, W7Q, after the review's fixes). "Active" is weak evidence alone: a one-edit
  garble of a confirmed US call is an active license 38.1% of the time (measured over every one-edit variant of US form of the 284
  confirmed US calls of 2026-11-17); "never issued" is strong evidence that a US call is false as of the file's date (2026-11-15),
  not proof, since a license granted since or purged long ago reads the same. At the new defaults the seven 2026-11-17 recordings read
  168 / 83 / 103, exact replays equal to the mode 1 and alternatives column call for call. After the review's fixes (an alternative
  only where emit takes it; the periodic-run test), call for call the same on 2026-11-17 (168 / 83 / 103) and 2026-11-18
  (576 / 310 / 214), and 2017 200 / 56 / 54, L99E gone, the old defaults' figures. The clean held-out set (the 2026-11-19
  recordings) waits for the RBN's day file, not posted at 14:03Z on 2026-11-19. The ambiguous-K rule (`ambiguous_sign_off` 0) came from AK4J and changes
  nothing at 2.5 nats on any of these sets. An earlier form (keywords equally likely, a call's first copy counted as evidence,
  repeats across strays, `believe_nats` 3) gave 168 / 90 / 106 on 2026-11-17: N0TAR lost as N0T AR, OO0O and W1BR from garbled
  repeats; and until strings repeated within themselves were refused, 3J3J from periodic garbage. On 2 000 words of noise text (12 to
  40 characters drawn with p(c) ∝ 2^−e) 0 words hold a believed call (the test allows 2: 3 or more is 0.15%, the 95% upper bound
  that 0 in 2 000 gives) (the first form, its noise characters uniform at ln 36, DE after a call
  at 2.77 nats and no test of the word: 52). The stage's CPU: 0.00423 ms per channel-second at the defaults, 0.00424 with mode 1
  (exact replays over 819 447 channel-seconds, the keywords matched once a place); on the dense CWT recordings of 2026-11-18 the stage
  costs 1.2 to 2.7 ms per channel-second whether mode 1 is on or off (566 219 and 607 817 channel-seconds a recording; the wall clock
  there varied by more than the variant's effect between runs of the same text). Classes: ν(c) a model, heuristic; λ_κ measured, the
  mean of −ln shares over the 2026-11-17 recordings' 127 036 decoded words (general text) and the 2026-11-18 recordings' 366 101
  (mostly the CWT contest), SOTA unseen at 11 (heuristic); φ(m) derived from the grammar for 3 to 9 characters, 10 to 12 reusing 9's (heuristic) (2 000 000 random strings a length; a test
  checks it within 0.1 nats); π(m) measured, the 4 314 base calls three or more RBN skimmers spotted on 2026-11-17 (8 to 12
  characters share their 0.35% evenly: heuristic); `call_nats` measured, the mean of 3.32 and 1.52 nats on those two days;
  `stray_nats`, `repeat_nats`, the bigram gains, `believe_nats`, `word_min_nats`, `segment_min_chars` and the 64-character limit
  heuristic, the bigram gains of the size measured on 2026-11-17's words (DE before a call 2.72 nats, DE after 1.22, K after 1.54; the
  CWT text of 2026-11-18 gives 1.06, −1.34, −0.03). The keyword shares and the call lengths were measured on the days the variant was
  judged on, the 2026-11-18 recordings among them: a held-out check of the costs' form, not of those values.

- Call forms (`call-form-1`, 2026-11-19). *Standard practice:* CW Skimmer validates a call by the ITU's blocks, a watch list or
  MASTER.DTA, and its pattern, typical, unusual or suspicious (its help, "Callsign Validation"), an unusual pattern asking more
  repeats (2, 3 or 5 at its Normal level; RBN 2021, secondhand); its pattern table is unpublished. *Sources* (read 2026-11-19):
  RR Article 19 (ITU's own copy of section III, fxm-art19-sec3.pdf): 19.68, one character (B, F, G, I, K, M, N, R, W) or two, a
  single digit, then a group of not more than four characters, the last a letter; 19.68.1 (WRC-07), in a half series "the call sign
  shall consist of three characters followed by a single digit and a group of not more than three characters"; 19.68A, more than
  four on special occasions. Appendix 42's half series (Wikipedia's ITU prefix table): 3DA–3DM Eswatini, 3DN–3DZ Fiji, SSA–SSM
  Egypt, SSN–SSZ Sudan. 47 CFR 97.3(a)(11) and the FCC's pages on its call sign systems: one or two letters, a digit, one to
  three letters; Group A (Extra) 1x2, 2x1 and AA–AL 2x2, Group B 2x2 of K, N, W, Group C 1x3, Group D 2x3 of K or W; the special
  event 1x1, K, N or W, a digit, a letter A to W, Y or Z. *Derived and checked:* the FCC's HD.dat of 2026-11-15, 1 607 237 calls
  ever issued: all but WB4MZ0 have one or two letters, a digit and one to three letters; of A or N with two prefix letters only
  AF0EMA–AF9EMA and NF0EMA–NF9EMA (FEMA) have three; no 1x1 (the coordinators keep them). Guinea's 3XY is sourced practice
  outside 19.68.1 (3XY7C, 3XY1D, 3XY1T; GDXF's DXpedition records; 3XY3D, ARRL bulletin 2017); MASTER.SCP of 2026-11-18 holds
  no other digit-letter-letter-digit call than 3DA0GY and 3DA0PA, and the base calls three or more RBN skimmers spotted on
  2026-11-17, 2026-11-18 and 2017-08-21 none but the bust 8RN8RN. *Unusual forms, measured:* of those base calls 11 of 4 366
  (0.25%), 9 of 4 835 and 3 of 3 468 have four letters after one digit, most of them busts by inspection (SD6MCWT, IK4EATX,
  W0RAUW, KE4COHK) beside a few specials (8N3ACAF, 8N5JAZZ); MASTER.SCP holds 65 of 50 034 (0.13%: YOTA and IARU stations,
  VK5FBIC). The rule's form (list only, not more points) is a choice; 5 points did not refuse EE4EEEE (5.25). *Measured* (exact
  replays of the defaults' logged events; RBN judge with MASTER.SCP and HD.dat; tables in `build/cf-1/judge/`, git-ignored),
  window-confirmed / misread / unknown, never-issued US calls in brackets: 2026-11-17 168 / 83 / 103 (14) to 170 / 85 / 90 (13);
  2026-11-18 576 / 310 / 214 (99) to 578 / 305 / 188 (99); 2017 200 / 56 / 54 (31) to 200 / 56 / 53 (31). No call the RBN spotted
  in the window was lost, and every call removed was judged junk by its text. Removed, by rule: three characters before the digit
  (5NN5NN on four recordings, 5NN5NNK, 5NN5NNCT, 5NN5NNNJ, 5RE5RE; 3TN6JIM, 3TN6R, 3TT5TU, 2NN3R, 3OA6E, 3TA3R, 3TN4TU, 3TT3R,
  3TT3TU, 4OO3R from the CWT's exchanges); the US forms (K9STPL twice, AB9CAEE, N5X, AA5YOM, K4TMYL, KF0UVLK, N7WYDA, WX2ZAQT,
  WB4DCWR, KZ5ZQNI; N5X and AA5YOM never issued); unusual forms (`unusual_listed_only`; GA5NNON, TE7TTTD, EE4EEEE; on
  2026-11-18 4T5DAVE, AT7BILL, 4T5MARK, A78DAVE, AT7RICH, a call and a CWT name run together, and F5PLTN, FI8IEEE, FN5NNDK,
  FN5NNNY, GW0TAUW, JD3NTTT, TU5NNNY; 2017 SS7SSDH, T34BIEI); periodic joined text (`joined_periodic`; L96LL; 9P9P and W9NW);
  the ambiguous repeat (`ambiguous_repeat`; W7Q, never issued, KW7Q's misreading); by arbitration, a junk rival gone, N7ORE (N7OR
  E). Gained: window-confirmed AB9CA (11 signed readings, held before by AB9CAEE), N5XE (held by N5X), KF0UVL (DE KF 0UVL K,
  three times), KZ5ZQN (DE KZ5ZQNI, never issued as of 2026-11-15, the RBN's own spot perhaps a bust); spotted that day WA9TOP, N7ATU, K0XF
  (listed) and W9W; K4MF (listed, active; FB K4MF TU); and junk the removals stopped holding back: EN5X (a phase of N5 X E N5 X
  E), K4SP, NN5NN (twice), FN5NN, FK0X (a phase of K 0 X F), JD3NTT and WX2ZAQ (never issued). The stage's CPU (`kz4ap-calls`,
  the stage's time over channel-seconds): 0.001 to 0.013 ms per channel-second before and after on the seven 2026-11-17 and the
  four 2017 recordings; on the densest, the 2026-11-18 CWT on 20 m (566 219 channel-seconds), the two builds side by side 1.66 ms
  before and 1.28 ms after (fewer candidates), against the bar's 40 ms and the goal's 8 ms.
  The decoders are untouched: on live2.wav every decoded symbol's event is the same byte for byte with and without the change.

- Call forms (`call-form-2`, 2026-11-19), the real calls `call-form-1`'s review found refused. *Sources* (read 2026-11-19): RR 19.68
  (ITU's copy, fxm-art19-sec3.pdf) asks two characters and a single digit, then the group: a call whose only digit is the second
  character of its prefix (C6AGU) is outside its letter, so the form is *measured*, not derived: MASTER.SCP of 2026-11-18 holds 92
  (C6 11, E7 12, D4 9, P3 9, C4 8, E2 7; none with four letters), and of the base calls three or more RBN skimmers spotted, 37 of 4 423
  (0.84%) on 2026-11-17, 55 of 4 901 on 2026-11-18 and 8 of 3 513 on 2017-08-21 have it, real by their spots (C8K by 136 skimmers in
  2 282 spots on 2026-11-18, E6DX by 41, T2DE by 19, T7LW by 11, A6BB by 7) though MASTER.SCP holds only 1, 4 and 0 of them. The
  slash rule is a heuristic (the review's ruling): the RBN's three days, MASTER.SCP and the FCC's file change no base against `main`
  (a first rule, the longer side, made N4A/K4SV K4SV, which the RBN spotted as N4A/K4SV 10 times on 2017-08-21). *Grammar check* (every call of
  each list through `main`'s `parse_call` and this one's; refused before and after): the FCC's calls ever issued (1 607 237) 9 to 1
  (WB4MZ0), the 8 gained the words of CW K1EL, K5NN, K9AY, N1MM, N5NN, W2DU, W3DZZ, W8JK, now listed only; active (822 120) 7 to 0;
  MASTER.SCP (50 028 lines) 107 to 13, 92 prefix-digit calls and N1MM, K9AY gained; the RBN's spotted calls 259 to 65 (8 976,
  2026-11-17), 335 to 55 (10 179, 2026-11-18), 98 to 40 (6 600, 2017-08-21); no call refused that was not before. *Measured*
  (exact replays of the logged events, no list, RBN judge with MASTER.SCP and HD.dat; tables in `build/cf-2/judge/`, git-ignored),
  window-confirmed / spotted that day / misread / unknown, never-issued US calls in brackets: 2026-11-17 170 / 66 / 85 / 90 (13) to
  170 / 66 / 84 / 90 (13); 2026-11-18 578 / 180 / 305 / 188 (99) to 578 / 180 / 304 / 186 (99); 2017 200 / 107 / 56 / 53 (31),
  unchanged. Removed, all one or two letters before 5NN and junk by their text: NN5NN on two recordings (5 NN5NN NE; NALD 5 NN5NN AR),
  FN5NN (GAAGN FN5NN NY), BK5NN (MOBK BK5NN 73). Gained: none. The prefix-digit form as any call (`prefix_digit_listed_only` 0,
  measured with the form gains of `call-form-1`) gained no confirmed call and 35 calls on 2026-11-17 (misread 85 to 115, unknown 90 to
  95: E5E, T5E, E4E, A9E, D4Y; D2OWJ for KD2OWJ) and 12 on 2017 (misread 56 to 61, unknown 53 to 60: A8DT, Y4EL, S5N), so it is
  listed only. With MASTER.SCP as the list, against `main` with it: the same removals, and gained D4A (spotted that day; D4A ALEX 388N,
  a CWT exchange), E2E (spotted that day; one whole word, E2E JIM 2216, and joined junk), E2M (spotted that day, but only joined
  pieces, E 2M ETT) and E7A (a fragment of KE7AEO, K E 7AE O); of the listed words of CW only NR5NN was read, once (1 point, not
  reported), and K5NN and N5NN, read 6 and 13 times, are on no list. The stage's CPU (`kz4ap-calls`, the stage's wall time over channel-seconds), the two builds side by side and
  alone on the densest recording, the 2026-11-18 CWT on 20 m (566 219 channel-seconds): 1.31 ms per channel-second before, 1.29 after;
  0.001 to 0.010 ms on the seven 2026-11-17 and four 2017 recordings either way; against the bar's 40 ms and the goal's 8 ms.

- Arbitration (`call-arb-1`, 2026-11-19), from `gap-1`'s comparison with CW Skimmer on the spare radio's recordings. *W3RJ* (the
  2026-11-18 CWT on 20 m, 116 RBN skimmers in the window) was reported at 17.7 s and withdrawn at 2 150.4 s by the arbitration
  working as designed (a trace of `evaluate` in a scratch build): XW3RJ, one edit away, had 52.5 points within `variant_hz` and
  `window_s` of the place against W3RJ's 46.0, since each QSO of the run ends TU W3RJ and the decoder loses the space between T
  and U, which then read X (derived from the Morse code). On 40 m W3RJ was listed again only from 3 455 s. With `x_as_tu` each
  XW3RJ gives W3RJ half a point: W3RJ is listed from 17.7 s to the end on 20 m (94.0 points) and from 92.1 s on 40 m, and XW3RJ
  is held. A half point, not a whole one, keeps a real X call (XE2ABC) ahead of the stripped reading it also gives (E2ABC).
  *Fragments:* on the 2026-11-17 40 m recording K4VRT sends DE K4VR T with a 7.8-dit space before the T against 3.4 to 4.3 dits
  inside the call (`gap-1`, measured), a word space by the standard 7, so the gap cannot tell; each such transmission gave K4VR a
  whole word's point and K4VRT 0.75, and K4VR (11.2 points) beat K4VRT (10.0). The proposal (`gap-1`): count such a transmission
  once, for the longer call or for neither. Tried: for the longer always (`joined_covers` 1), for neither (2), for the longer when
  it has points of its own (3), and 1 and 3 with a shorter joined reading covered too (K 4VR T). *Measured* (exact replays of the
  logged events, no list; RBN judge with MASTER.SCP of 2026-11-18 and HD.dat; tables in `build/arb-1/judge/`, git-ignored),
  window-confirmed / spotted that day / misread / unknown, never-issued US calls in brackets; `main` 2026-11-17 170 / 66 / 84 / 90
  (13), 2026-11-18 578 / 180 / 304 / 186 (99), 2017 200 / 107 / 56 / 53 (31):
  - `x_as_tu` alone: 2026-11-18 the same counts, W3RJ for XW3RJ, which the RBN spotted too (2 skimmers: its own reading of TU W3RJ);
    the other sets and the generated ones unchanged.
  - 1: 2026-11-17 170 / 67 / 74 / 89 (10), 2017 200 / 104 / 49 / 54 (24), but window-confirmed HC5A (HC5A M) and W6SX (W6SX E,
    KC2LRC-2) lost to the longer call read only so, and a false call on H2 seed 2 (N3YPB). Not taken.
  - 1 and 3 covering a shorter joined reading: 2026-11-17 167 / 64 / 73 / 89 (8) and 170 / 67 / 81 / 90 (11); lost HB9T, KZ8R,
    K8BZ, HC5A, W1B, W6SX with 1, K8BZ and W1B with 3. Not taken.
  - 2: 2026-11-17 170 / 67 / 81 / 90 (11), 2017 201 / 106 / 54 / 53 (29); K4VR stays (its glued DEK4VR T readings outnumber K4VRT's
    own). Not measured on 2026-11-18.
  - 3, with `x_as_tu`, as first taken (before the review's fixes below): 2026-11-17 171 / 67 / 79 / 90 (10), 2026-11-18 580 / 180 / 297 / 185 (95), 2017 201 / 105 /
    55 / 53 (29). The generated sets (checkpoint 4's logged events, replayed): every list the same, true / false the contest 207 / 1,
    fresh seed 225 / 0, strong pairs 39 / 1 and 39 / 0, crowded 297 / 1, 100 stations 98 / 0, G2 40 / 0 and 38 / 2, H2 39 / 2 and
    39 / 0 (seeds 1 and 2). No window-confirmed call lost but XW3RJ. Gained, window-confirmed: K4VRT (DE K4VR T K, DE K4VRT K;
    1 skimmer), KS4OT (KS4O T KS4O T; 1), WB4FDT (DE WB4F D T, DE WB4FDT QNI K; 13), W4BOH (N2GZ; W4B OH K, W4BOH 569; 1), W3RJ;
    spotted that day KA2SJG (KA2S JG DE K); K4VRT on N2GZ (K4VRT TU EL 88; judged a misreading, the RBN having K4VR that day, probably wrongly: in the FCC's HD.dat K4VR was cancelled 2015-12-29 and granted again 2018-01-17, and K4VRT has been active since 2004-10-08, so the RBN's K4VR spots of 2017-08-21 were most likely its own reading of this operator's K4VR T); junk
    OO0QOT (OO0Q OT). Removed, each a fragment of a longer call by its text: K4VR, KS4O, WB4F, W4B, VE3F (VE3F I T: VE3FI), K2H
    (K2H T), W1F (W1F J), N0B (N0B PM), KA2S, OO0Q, W1N (W1N S), W9G (W 9 G TA), JA1G (JA1G MO D), K4P (beside K4PI), K7S (beside
    K7SV), I4K (I4K ZS), and on KC2LRC-1 K2W (K2WK K; spotted that day, never issued); XW3RJ (TU W3RJ). Not reached: K1L, N2E,
    W1B and WA5D (read joined themselves, K 1L GQ), VE3GF and EA1E stay. The stage's CPU (`kz4ap-calls`, the stage's wall time
    over channel-seconds), the two builds side by side and alone on the densest recording, the 2026-11-18 CWT on 20 m (566 219
    channel-seconds): 1.38 ms per channel-second before, 1.46 after (the comparison's extra loop over a pair's occurrences); 0.001 to 0.010 ms
    before and 0.001 to 0.013 ms after on the seven 2026-11-17 and four 2017 recordings; against the bar's 40 ms and the goal's 8 ms
    (measured before the review's fixes, which add no loop).
  - The review's fixes (the defaults now): `covers_own_points` 1.5 (above 0 before; the reviewer's probe, a real W1AW T K three
    times beside one QRZ W1AWT 50 Hz away, listed W1AWT; at the ruled 1.0 one whole word still did, so 1.5, more than one reading,
    is taken, a heuristic), no cover after a run of K and R (N4A K K), and `flat_stripped` 1 (a stripped reading with an X taken off
    the front counts flat, not three times, in a listed call's points: with F4ABC listed, four CQ XF4ABC XF4ABC K listed F4ABC and
    dropped XF4ABC). Measured as above. No list: 2026-11-17 171 / 66 / 82 / 90 (12), 2026-11-18 580 / 180 / 302 / 185 (99), 2017
    200 / 107 / 55 / 53 (31); against `main`, gained W3RJ, K4VRT, KS4OT (it keeps its covers: KS4O T read four times joined, 3
    points of its own) and WB4FDT, all window-confirmed, removed K4VR, N0B, XW3RJ, KS4O, WB4F, I4K and VE3F, no window-confirmed
    call lost but XW3RJ. Against the first version the fragments with under 1.5 points of their own longer call come back: W1F,
    K2H, KA2S (KA2SJG, spotted that day, gone), OO0Q (OO0QOT gone), W1N, W9G, JA1G, K4P, K7S, and on 2017 K2W, K4VR on N2GZ (K4VRT
    gone) and W4B (window-confirmed W4BOH gone: one whole word of its own at that place, the others 870 Hz away). With MASTER.SCP of
    2026-11-18 as the list, `main` / the fixes: 2026-11-17 170 / 71 / 84 / 94 (10) / 171 / 71 / 82 / 94 (9) (K4VRT for K4VR, N0B
    gone); 2026-11-18 571 / 173 / 227 / 183 (38) / 572 / 173 / 228 / 182 (38) (KS4OT gained, K0HT gained, spotted that day, K0 H T
    and K0HT BUD 242, the judge's misreading of K0HTF; I4K gone); 2017 219 / 120 / 60 / 55 (20) / 219 / 120 / 59 / 55 (20) (VE3F
    gone). W3RJ, listed, is reported from 17.7 s on 20 m and from 92.1 s on 40 m with the list, before and after. `flat_stripped` 2
    (stray E, T, I flat too), with the list: 2026-11-17 the same as 1; 2017 M7N (listed, junk: GRID EM7N QC) for EM7N; 2026-11-18
    572 / 173 / 226 / 182 (38), but listed M2E lost (spotted that day: M 2E ND, EM2E), and F5DM and M7W (listed, judged
    misreadings) gone, NB7O (spotted that day) gained: a loss of an RBN-spotted listed call, so 2 stays a variant. The generated
    sets: every list byte for byte the same as before the fixes.

- Short calls and cut numbers (`call-short-1`, 2026-11-20), from the held-out check of the two 2026-11-19 recordings, where 26 of the
  29 calls the RBN judge marked never-issued US calls were 1x1s (N5N, W6S, K0E) and CW Skimmer reported none of that form. That mark
  is no evidence for a 1x1 (review): the FCC's HD.dat of 2026-11-15 holds no US 1x1 at all (0 of its 1 607 236 distinct US-form calls
  match K, N or W, a digit, a letter; the coordinators keep them), so every US 1x1, real or not, reads never-issued. The case rests on
  the window-confirmed and same-day counts and on the traces below; since the review the judge marks a US 1x1 `-`, not judged
  (`rbn_judge.us_license`), and the never-issued counts here are of other shapes only. *Why they pass* (a trace
  of `evaluate` in a scratch build, 2026-11-18 CWT on 20 m): K7U, a fragment of K7UT, is held wherever K7UT is read, but the pooled
  points sum three scattered readings no K7UT is within 150 Hz of (0.75 joined at +6 003 Hz, a tie with K7UTE; 1 whole word at
  −23 883 Hz; 0.5 stripped at −23 955 Hz): 2.25 points, none signed. The 1x1s the stage reported on the tuning sets had mostly two to
  five bare whole words, or were such fragments (K7U, N6N, N3J, W1Q of K7UT, N6NT, N3JT, W1QK, each read 59 to 192 times
  stripped); the real ones, the 2017-08-21 eclipse stations (W0D, N7S, W7E, W0E, K7Y, W6S; 6 to 18 RBN skimmers), had 6 to 74
  transmissions, signed in 1 to 64. *Measured shares* (base calls three or more RBN skimmers spotted on 2026-11-17, 2026-11-18,
  2017-08-21): three characters 27 of 4 311 (0.63%), 33 of 4 772 (0.69%), 24 of 3 287 (0.73%); MASTER.SCP of 2026-11-18 holds 268
  calls of three characters, none of the US series. A cut number's shape: 16, 21 and 23 calls, all 9A, 7N or 8N but 5A1AL, 5N80Y, 5T5T
  and 9N1HA (4 of 11 370, 0.035%); MASTER.SCP holds 389, 349 of them 9A, 7N or 8N (the rest 7A 19, 4A 11, 8A 3, 4T 2, 5N 2, 4N, 7T,
  9N). The CWT's exchange is a name and a member's number, sent in cut digits by many (A 1, N 9, T 0): DREW N7N, SCOTT 4T2A, JOHN 5A3
  BOB, read as calls. *Tried* (exact replays of the logged events, no list; RBN judge with MASTER.SCP and HD.dat; tables in
  `build/cs-1/judge/`, git-ignored), window-confirmed / spotted that day / misread / unknown, never-issued US calls of other shapes in
  brackets (1x1s left out; re-judged in the review, `build/csf-1/judge/`, git-ignored); `main` 2026-11-17 171 / 66 / 82 / 90 (3),
  2026-11-18 580 / 180 / 302 / 185 (16), 2017 200 / 107 / 55 / 53 (3). As (unsigned,
  signed) bars: (3, 3) 171 / 64 / 73 / 90 (3), 577 / 173 / 233 / 183 (16), 199 / 104 / 45 / 52 (3), losing window-confirmed K2W
  (N2GZ, 1 skimmer); (4, 4) and (5, 5) on 2026-11-17 and 2017 170 / 64 / 72 / 90 (3) and 170 / 64 / 71 / 90 (3), 198 / 104 / 41
  / 52 (3), losing W1B (6 skimmers), W6K and K2W; signed only (2 signed, else never) 171 / 65 / 70 / 90 (3), 199 / 104 / 50 / 53 (3), losing W6K
  (KC2LRC-1, 1 skimmer, named only: W6K5NN); (4, 2) 171 / 65 / 72 / 90 (3), 578 / 174 / 234 / 183 (16), 199 / 105 / 50 / 53 (3),
  losing W6K; (5, 2) 171 / 65 / 71 / 90 (3), 199 / 105 / 50 / 53 (3), losing W6K; (5, 3) 171 / 64 / 71 / 90 (3), 198 / 104 / 43
  / 52 (3), losing W6K and K2W (on 2026-11-17 and 2017 only); (3, 2), taken: 171 / 65 / 73 / 90 (3), 578 / 176 /
  245 / 183 (16), 200 / 105 / 52 / 53 (3); no bar changes a never-issued count of another shape. With the cut-number rule (`cut_number_form`) on 2026-11-18: 2 as first taken (signed only) 578 /
  176 / 237 / 167 (16), 1 (list only) 578 / 176 / 237 / 165 (16); a points bar since the review (next item); on the other sets neither changes anything. The defaults lose
  window-confirmed W8K and N0T, both the RBN's own misreadings: W8K's one spot (EA8BFK, 14 040.4 kHz, 19:07Z) is on W8CAR's
  frequency (486 spots by 79 skimmers; our CQ CWT W8K TR is CQ CWT W8CAR), as are W8T's two, and N0T's one (JA1CCA, 7 032.1 kHz,
  13:19Z) is on N0TA's (509 spots by 99 skimmers). Every other call removed is a fragment of a longer call or junk by its text (lists
  in `build/cs-1/judge/changed-*.txt`): K7U, N6N, W1Q, N1L, K0H, K4Y, W2H, W0O, K4P, K8A (VK8A K8A), K9V (K 9 V P L), W7G (DE W7G F
  T, W7GKF), W4Z (4Z YT W4Z YT), B4V, I5E, I8O, I7X and the like; of the removed calls spotted that day, all had one RBN spot that day
  (N4R, K1N, K9T, K9V, W7G, W0M) but W1T (5 skimmers, 17:31Z to 18:49Z, hours after the recording; TU NE5A W1T K); two removed calls
  are in MASTER.SCP, M5A (W5LA M5A A T B) and M3E (E M3E). The cut-number rule removes 25 numbers (DANI 2N7N, DON 2N8N, SCOTT
  4T2A, STAN 3N7N, CHIP 4T5 JIM) and gains 2T2M (JIM 2T2M, no longer held by 2T2T); 2T7A and 3T7T, one signed reading each, stay (signed only). With
  MASTER.SCP (`main` / the defaults): 2026-11-17 171 / 71 / 82 / 94 (3) / 171 / 71 / 75 / 94 (3), 2026-11-18 572 / 173 / 228 / 182
  (12) / 571 / 172 / 202 / 163 (12), 2017 219 / 120 / 59 / 55 (2) / 219 / 120 / 57 / 55 (2) (signed only for cut numbers), the same calls removed and N0T, W9W
  (one spot that day, 15 m, 08:49Z; DTT S DTT S) gone; none gained. The generated sets (checkpoint 4's logged events): false calls
  unchanged on every set (the contest 1, fresh seed 0, strong pairs 1 and 0, crowded 1, 100 stations 0, G2 0 and 2, H2 2 and 0);
  true calls the same but on the two contests, 207 to 204 (G6I, I1J, I8Z) and 225 to 221 (F8L, I0M, K2Z, W4T), callers of three
  characters read twice bare: the generator gives 8.6% to 12.5% of its stations that form. A heuristic: the bar of 3 points and
  the signed reading are CW Skimmer's practice in kind (a rarer pattern needs more repeats), their values the test set's choice.
  The stage's CPU (`kz4ap-calls`, the stage's wall time over channel-seconds), the two builds side by side and alone on the densest
  recording, the 2026-11-18 CWT on 20 m (566 219 channel-seconds): 1.67 ms per channel-second before, 1.66 after; 0.001 to 0.010 ms
  on the seven 2026-11-17 and four 2017 recordings; against the bar's 40 ms and the goal's 8 ms. *Held out, once* (the two
  2026-11-19 recordings, exact replays judged against the RBN's 2026-11-19 file; `build/cs-1/heldout/`, git-ignored), calls /
  window-confirmed / spotted that day / misread / unknown, never-issued US calls of other shapes and US 1x1s (not judged) in
  brackets (re-judged in the review, `build/csf-1/heldout/`, git-ignored): `main` 534 / 260 / 66 / 131 / 77 (3; 26) to 499 / 260 /
  65 / 102 / 72 (3; 8); with MASTER.SCP 514 / 259 / 69 / 108 / 78 (3; 12) to 491 / 259 / 67 / 91 / 74 (3; 4); CW Skimmer 420 / 224 /
  49 / 93 / 54 (2; 0) (cut numbers signed only). No window-confirmed call lost, none gained at all; removed without a
  list 35, among them the cut numbers 3N3T, 5T7R, 3T2T, 2N7N (signed only; the bar since the review gives back 3N3T, 3T2T and
  2N7N, next item), the 1x1s N5N, W6S, K1K, N4M, N8P, W8O, N4W (2 RBN spots that day,
  15 m, 16:43Z) and G1T (in MASTER.SCP, never spotted); with the list M0T too (1 spot that day). Left without a list, US 1x1s the judge classes as misreadings: K0E, N0N,
  N5O, N8W, W0M, W0T, W4K, W7M.

- The cut number's bar (review of `call-short-1`, 2026-11-21): a points bar as the 1x1's, `cut_number_points` 5 bare,
  `cut_number_points_signed` 2 signed, in place of a signed reading required, by the owner's ruling on calls the list does not
  hold ("I think several repeats should be enough, I don't think signed should be required for these special calls. It's more a
  question of how many times it gets repeated. Obviously fewer would be required if signed."), and since a DX station running the
  CWT need not sign by the Role rule (CQ CWT 8T2BH TU 8T2BH: CWT is no keyword; a CQ or K after the call would sign it). *Measured*
  (exact replays, no list; `build/csf-1/judge/`, git-ignored), window-confirmed / spotted that day / misread / unknown, never-issued
  US calls of other shapes in brackets, then the cut-shaped calls reported outside 9A, 7N and 8N, on the 2026-11-18 CWT: no rule
  578 / 176 / 245 / 183 (16), 27; signed only 578 / 176 / 237 / 167 (16), 3 (25 removed, 2T2M gained); bar 3 578 / 176 / 242 / 179
  (16), 20 (7 removed: 2T1T, 2T6I, 4T5JIM, 5A3T, 2N8T, 5T6R, 5T7TU); bar 4 578 / 176 / 242 / 176 (16), 17 (also 5A3BOB, 2A8O,
  5A3JIM); bar 5, taken, 578 / 176 / 239 / 177 (16), 15 (also 2T2T, 3T7T on 20 m, 5T7R; 2T2M gained, as with signed only), the lowest
  bar that removes most (13) of the 25. No call but cut-shaped ones changes at any bar, so no window-confirmed call is lost; on
  2026-11-17, 2017 and the generated sets every list is byte for byte the signed-only rule's at every bar (the shape is not reported
  there either way), so the generated false calls are the same. Left at 5: 2T7A, 3N7N, 2N7N, 3T2T, 4T2A (20 m and 40 m), 2N8N (both),
  3N4D, 2A2A, 4T5R, 5A3R, 3T7T (40 m) and 2T2M, each a runner's own number read in 7 to 71 transmissions: no bar of several repeats
  can tell them from a DX station's call. With MASTER.SCP, bar 5: 2026-11-18 571 / 172 / 203 / 171 (12), 12 (signed only 571 / 172 / 202 / 163 (12), 3; `main` 572 / 173 / 228 / 182 (12),
  24), the 9 calls added all numbers of the 12 above; 2026-11-17 and 2017 byte for byte the signed-only rule's. *Held out* (the 2026-11-19 recordings, looked at again only
  to report, not to choose): calls / window-confirmed / spotted that day / misread / unknown (never-issued US calls of other shapes; US 1x1s), signed only to
  bar 5, without a list 499 / 260 / 65 / 102 / 72 (3; 8) to 502 / 260 / 65 / 102 / 75 (3; 8), with MASTER.SCP 491 / 259 / 67 / 91 /
  74 (3; 4) to 494 / 259 / 67 / 91 / 77 (3; 4): three numbers back (3N3T, 3T2T, 2N7N), nothing else changed (`build/csf-1/heldout/`). A heuristic, the bar the test set's choice.
- The series a cut number's shape covers, checked against the ITU (review of `call-short-1`, 2026-11-21; *sourced*). Allocation:
  the ITU's Table of International Call Sign Series (Appendix 42 to the RR), GLAD (itu.int/gladapp/Allocation/CallSigns, read
  2026-11-21): 2A, 2N, 2T the United Kingdom; 3A Monaco; 3N, 3T China; 4A Mexico; 4T Peru; 5A Libya; 5N Nigeria; 5T Mauritania; 6A
  Egypt; 6N the Republic of Korea; 6T Sudan; 7A, 8A Indonesia; 7N, 8N Japan; 7T Algeria; 8T India; 9A Croatia; 9N Nepal; 9T the
  Democratic Republic of the Congo; 4N has no row (returned; `allocated_pair` refuses it). Amateur use: the ITU's list of the forms
  of call signs administrations assign their amateur stations (Annex to Operational Bulletin 1350, position on 15 October 2026)
  names 3A (3A2AA–3A2ZZ, 3A9A–3A9Z), 5N (5N, a digit, three letters), 5T (5T5 and 5T2, two letters), 6N (6N2AA–6N2ZZ experimental;
  6N with a digit, special occasions), 7N (7K–7N, digits 1 to 4, three letters), 8N (8J and 8N), 9A and 9N (9N1AA–9N1ZZZ); its forms
  for the United Kingdom (2E, G, M), Mexico (XE, XF), Peru (OA), Egypt (SU), Indonesia (YB–YH) and India (VU) name none of these
  series, and China, Libya, Sudan, Algeria and the DRC give no forms. Special-event use is wider than the list: MASTER.SCP holds 4A
  11 (4A2DX), 7A 19 (7A0A), 8A 3, 4T 2 (4T4T), 5N 2, 7T 1, 9N 1 and a 4N1A; the RBN, at three skimmers or more, spotted 5A1AL,
  5N80Y, 5T5T and 9N1HA on 2017-08-21 and none of the shape outside 9A, 7N and 8N on 2026-11-17, 2026-11-18 or 2026-11-19. So every
  series the rule covers is allocated, a few are used, and 9A, 7N and 8N are left out as the ones most real calls of the shape use
  (349 of MASTER.SCP's 389). Downloaded copies in `build/csf-1/itu/`, git-ignored.
- A call read only once, anywhere in the band, is not reported without a list: that is the rule (since `rob-1` named or not; before it, named words passed alone). On W3OA, of the 13 calls CW Skimmer lists that the RBN confirms and `main` (d44c7d6) did not, 5 were read as one transmission (K5LN after a fragment 5LN, N4KGL and NZ9Z signing, VE3KP bare, KQ2M on two tracks at once), W0OPW twice as joined letters (1.5 points), K9BGL glued to itself (two readings since `rob-1`), and 5 were never read whole (weak or garbled text: N0AIS as N0AI and 0AIS); CW Skimmer read both repeats where our decoder read one. Reading a fragment beside a whole call (5LN K5LN) as a repeat was tried and dropped (A.12, `rob-1`). On the generated contest it is not the largest loss of callers: of the 78 lost before the rules below (review 5), 45 were never decoded, every one overlapped by a keying station within 150 Hz over at least 30% of its bursts, and 33 were read: 28 once, 1 at 1.5 points, 4 refused before the evidence (SM5NU; N9AZ, EA7UPZ, KB8AKK glued inside a word). The generator's callers key at random over the runner and each other, where a real caller waits his turn, so this overlap is likely overstated (conjecture).
- Since `rob-1` a word named once does not pass without a list: a caller whose own signal is never read loses his row (the contest 221 to 210 true calls; on the four 2017 recordings 18 confirmed calls and 36 the RBN spotted elsewhere that day, nearly all real callers). With `points_named` 1, as before, a misread call before 5NN was listed: most of the false calls the rules gained on the contest (6 of 7), and about half the misreadings on the 2017 recordings without a list. A row reached by naming shows the namer's frequency. With a list an unlisted caller named once is lost, as is one not on the list at all (the contest with a list of none of its callers: 206 true calls against 216).
- Withdrawal judged at a loser's last reading (stage-aging): a misreading reported early stays when its true call comes more than `window_s` after its last reading, and a reported call that survives a defeat once cannot leave later, since a rival's support at that time only falls as its old occurrences age out.
- Pooled points let a misreading the decoder makes the same way at several places through, where only one place's points would not: on real30 (30 min of real 40 m) K0HBN and K6QO, misreadings of IK0HBN and IK6QOP that drop the I, come back to the list (`withdraw_own_place` had taken them off), as do IK0I and K6T. Without a list the arbitration cannot tell which of two near calls is the station when both are read often (backlog: spellings of one station).
- With the variant `known_margin_nats`, a listed misreading the decoder was sure of passes on one bare word (on the generated contest EA5V, JA3E, ZL9XMP, the list's decoys).
- The segmentation (`segment_joined`, on by default): W7Q (never issued), KW7Q with its K read as a keyword (W7Q K W7Q), is gone
  since `call-form-1` (`ambiguous_repeat`), at a cost: a call sent twice with a K between whose K joins it to a call (K8DXKK8DX:
  KK8DX) gains nothing from its repeat; the alternatives do not touch a K before a call. The RBN cannot judge the variant's
  gains (it spots stations calling CQ), and an active license is weak evidence (38.1% of one-edit garbles of confirmed US calls are
  active). A call ending in K beside a sign-off is a choice the lengths' shares and the K's bigram
  make by fractions of a nat (K8DX K over K8DXK by 0.2 nats), and the RBN's skimmers split the same way (K9IHK and K9IH, WA5DTK and
  WA5DT; AK4JK read as AK4J K); a stray E or I behind a call after DE is taken into it (WN4ATI, W3QRE), since a fifth character costs
  less than a stray; a call before DE with nothing after it is not believed (N5JJ DE ...); a repeat with a garbled copy (K3BFQK3BF)
  does not count, nor a single copy with two strays behind (AB9CAEE, read 19 times; refused by its form since `call-form-1`); a single copy after DE is believed on 2.77 nats
  alone, so the stage's two points are what keeps a call's form after a chance DE in noise off the list; the keyword shares come from
  two days' text, one mostly a contest, and the noise model is not measured. A run of copies side by side that is a phase of
  periodic text (a grid square sent over and over: EL98EL98EL98 cuts as E L98E L98E L98) is refused only when its word has a
  character on both sides of it; a shifted phase that starts or ends its word (L98EL98EL98, the last copy cut short) is still read
  as a call repeated, since a real call sent again is the same there (GEHKE2EHKE2EH, KE2EH confirmed, the H before a garbled
  copy's tail). A garbled middle copy (K1ABCK1ABK1ABC) gives the shorter call twice (K1AB), not the call: no repeat within one edit.
- Call forms (`call-form-1`): only the United States' national rules are taken, since only the FCC's file proves a rule against
  every call issued; other administrations' suffix rules (the UK's, Spain's) are not, so a string of three letters after its digit that
  breaks another country's rules (GA5NNO) passes. A run of dots read as a whole word (SH5SHI, three times on 20 m, 2026-11-17) has a real call's form, and real calls of
  dots alone exist (EE5E and SE5S, listed and spotted), so no form rule refuses it. A joined phase at the call's own period (EN5X
  in N5 X E N5 X E, FK0X in K 0 X F K 0 X) is not refused. A call that repeats within itself (9A9A, 9K9K, 4K4K, 3Z3Z; 11 in
  MASTER.SCP) is refused when read joined (9 A9A) by `joined_periodic`, though not as a whole word. Without a list,
  a real call of four letters after its digit (Australia's foundation calls, VK3FABC; Japan's specials, 8N3ACAF) is not reported.
- Call forms (`call-form-2`), reported from a list only, so lost without one: a call whose only digit is its prefix's (C6AGU, E7DX; the
  RBN spots many that MASTER.SCP lacks, C8K, E6DX, T2DE, A6BB, T7LW), the active US licenses that are words of CW (K1EL, K5NN, K9AY,
  N1MM, N5NN, W2DU, W8JK; MASTER.SCP holds K9AY and N1MM), and every call of one or two letters before 5NN (the FCC's file holds 18
  active: AA5NN, KE5NN, NN5NN, W5NN and others; MASTER.SCP 5: DJ5NN, DM5NN, NA5NN, NR5NN, W5NN). With a list, a fragment of a longer
  call that is a listed prefix-digit call is reported (E7A, from K E 7AE O, KE7AEO, on 40 m, 2026-11-17). On a slash with a call on
  both sides and no list to tell, the left is taken unless it alone is a location form, so a call of a non-US series with one letter
  after its digit (DL1A, 9A1A) beside another call is taken for a location (DL1A/W1AW gives W1AW), and a US call of four characters
  or a 1x1 written first (KH6A/W1AW, K1A/W1AW) is taken as the base.
  The derived rules hold as of their sources' dates: a new US call system or a new half series needs the code changed.
- Arbitration (`call-arb-1`): only a fragment that is a whole word starting a joined reading is covered, so a fragment read joined
  itself or glued (K 1L GQ, N 2E DD, W 1B HC, DEK4VR T) still beats its call (K1L, N2E, W1B, WA5D on the 2026-11 recordings). A real
  call followed by a stray letter (W1AW T K, three times) keeps its transmissions while the longer call has under
  `covers_own_points` (1.5) of its own at the place: one whole word of W1AWT elsewhere (QRZ W1AWT, 50 Hz away) does not take them
  (the reviewer's probe, a test); two such words, or a whole one and a joined one, do, and the longer call is then listed. An X before a call that is the call's own (XE2ABC) gives a stripped twin (E2ABC) at half its points, flat against the
  list (`flat_stripped`), though readings of the twin as itself elsewhere at the place weigh triple if it is listed; other keywords
  run together (DE before a call reads B: BW3RJ) are not read. A listed call read only stripped of a stray E, T or I (EM2E: M2E)
  still weighs triple against the word it came from: `flat_stripped` 2 would make it flat, but lost M2E, listed and spotted that
  day, on 2026-11-18.
- Short calls and cut numbers (`call-short-1`): a real 1x1 heard only bare (a contest's caller who never signs, the generated contest's I1J, G6I, K2Z) needs three readings where another call needs two; the generator draws 8.6% to 12.5% of its stations in that form against 0.63% to 0.73% of spotted calls, so its loss there (7 of 59 such callers on the two contests) overstates a real band's. A signed reading is the Role rule's: CWT is not among its keywords, so CQ CWT W8K is bare. A real call of a cut number's shape outside 9A, 7N and 8N (4A7A, 7A1A, 5N7Q; 40 in MASTER.SCP), off the list and heard only bare, needs 5 points in 30 min (a running station reaches that, a caller seldom does), and a CWT runner's own number, sent in every exchange, passes the bar as a call would (12 on the 2026-11-18 CWT: 2T7A, 3N7N, 4T2A); telling them apart would take the exchange's context (a name before the number), which the stage does not read. Fragments of a longer call that are not of three characters (MO0M, OQ0MM) and a CWT number run onto a name in another shape (A78JIM: Qatar's A7, then 8) are not touched.
- Who sent a call is known only from the words around it: a DX naming a caller twice with no 5NN read next to it counts as *bare*, at the DX's frequency. *Named*
  is only as good as the decoder's reading of those words.
- A misreading read twice, with its true call read less, is reported: on real 40 m IK0S beside IK0HBN (too many edits apart for the arbitration); IK6QOP, read exactly once, shows as K6QOP and IK6QO, the stray-letter reading of a word giving half a point to the shorter call. The arbitration cannot know which of two near calls is the true one without a list, and with one a misreading that lies on the list beats the true call.
- A tiny call needs 2 whole words with a list or without (a list does not lower that), and glued, stripped or joined tiny readings do not count.
- The text reads a station's latest place: a station that moved shows there only.
- A correction that reaches what has been read adds its readings and does not take back the old ones, so a misreading it replaces can stay beside it, unless the arbitration rules on them. Beyond `history_s` it is not read.
- A row can hang on one transient reading: K6QOP's second point on real 40 m is a word the decoder corrects away 10.30 s after its end, and `settle_s` from 10.5 to 14 s loses it. So what is listed near such a word depends on `settle_s` to the second (measured, review 5), though no longer on the app's pace.
- Points are heuristic weights, not probabilities. Segmental's margins (A.8e, a variant) are a ranking, not a calibrated posterior, and no default rule reads them; a posterior on the call would need calibrated ones. The bank's and viterbi's symbols say 1, so their readings' margins are unknown; Envelope's and Matched's carry the product of their elements' confidences, which the stage reads as a margin too, untested as one.

- A known-call list's size counts every first word of a line that is not skipped (#, ;, !!, blank), a word with no call's form
  included, so a list with stray lines reads a little larger than its calls (MASTER.SCP of 2026-11-18: 50 021, its header skipped).
- Leaving decoder-only mode mid-stream finds no call in a track already running until a new track starts there.

## A.13 Listening clips (`kz4ap-listen`)

A tool beside the decoders, in no decoder's chain: one frequency of a recording as mono audio, so that a decoding can be checked
by ear (the owner, 2026-11-15). `engine/include/kz4ap/listen.hpp`, `bench/src/listen_main.cpp`. It writes a file and plays
nothing.

### Exact form

- **Input.** `RecordingReader` as section 1: resampled to the decoder rate fs, mirrored with `--mirror`. The station's offset
  f_o = f − center, Hz, in the true spectrum (`--khz` less the center from `--center-khz`, the header or the file name, as the
  app's; or `--offset-hz`). Refused unless |f_o| + W (W under Tone, below) lies within the band the input holds flat: fs/2,
  or, for a resampled recording, the resampler's passband (±86 kHz from the center at 192 kHz, A.1), beyond which a station
  is attenuated.
- **Listening path (the default), in order.**
  1. Mix: z₁[n] = x[n] e^(−j2π f_o n / fs), the oscillator's phase kept in cycles in [0, 1) as a double.
  2. Resample fs → f_a (`--rate`, 8000 or 12000 samples/s, default 12000) with `RationalResampler` (A.1): stopband from f_a/2,
     transition (10/192) f_a below it (625 Hz at 12 kHz, flat to 5375 Hz), at least 80 dB re the passband. Only input at
     |f| ≥ f_a − 0.7B (11.65 kHz at 12 kHz and B = 500 Hz) can fold into the listening band, at least 80 dB below the passband (derived).
  3. Listening filter (`listen_filter_taps`): a Kaiser-windowed sinc, linear phase, real taps applied to the complex baseband,
     so it passes the station's own band and rejects the other side of the tone (the image a real mixer would fold onto it) as
     much as any other frequency. Width B (`--bandwidth-hz`, default **500 Hz**, at −6 dB re the passband, both sides of the
     carrier together; heuristic, a common width for a CW filter). Cutoff ±B/2 at −6 dB re the passband; transition 0.4 B
     centered there (heuristic): **passband |f| ≤ 0.3 B (±150 Hz), stopband |f| ≥ 0.7 B (±350 Hz), at least 80 dB below the
     passband**. Kaiser's estimates for a design value A = 82 dB re the passband (as the resampler's, to meet 80 dB): β = 0.1102 (A − 8.7) = 8.078,
     L = ⌈(A − 7.95) / (2.285 · 2π · 0.4B / f_a)⌉ + 1, made odd: **311 taps at 12 kHz** (25.9 ms, delay 12.92 ms), 209 at 8 kHz.
     Taps normalized to a sum of 1 (unity gain at 0 Hz). B from 50 Hz (3097 taps at 12 kHz; heuristic) to f_a/2.
  4. *A receiver's filter* (`--filter-center-hz C`): the same path for the frequency that comes out at C, mixed by
     f_o + (C − f_t) and heard at C, so that the station comes out at f_t (derived) with its neighbors from C − B/2 to C + B/2
     in the audio; transitions 200 Hz wide (`kReceiverTransitionHz`, heuristic: a transceiver's steep DSP or crystal filter):
     `--bandwidth-hz 2400 --filter-center-hz 1500 --tone-hz 600` gives −6 dB re the passband at 300 and 2700 Hz, within
     0.01 dB of it from 400 to 2600 Hz, the receiver-audio clips' filter (A.1, "Receiver audio"). No AGC: one fixed gain.
- **Track path (`--track-filter`).** The channelizer (section 7, A.7) with one channel at bin c = round(f_o / Δf), its stream at
  r mixed by e^(−j2π (f_o − c Δf) j / r) (`ChannelFollower`'s shift for a detector frequency held at f_o), then
  `RationalResampler` r → f_a (1500 → 12000 samples/s: transition 78 Hz, flat to 672 Hz). Its filter is the channel's: −6 dB re
  the passband at ±150 Hz, within 0.015 dB of the passband to about 39 Hz, at least 74 dB below it from about 279 Hz. This is the stream
  segmental's and viterbi's channels hand their decoders for a station held at f_o; the decoders' own front filters (A.8d,
  A.8e) are not applied, nor the DC blocker (A.6), which acts within 0.03 Hz of the center only. Refused where r is not a whole
  number of samples/s (44.1 kHz input).
- **Tone.** a[k] = Re(z[k] e^(j2π f_t k / f_a)), z the path's output with the station at 0 Hz, f_t = `--tone-hz` (default
  600 Hz; heuristic, settable). A complex tone of amplitude A FS at f_o gives a real tone of amplitude A FS at f_t, power A²/2
  FS² (derived). Refused unless f_t ≥ W and f_t + W ≤ f_a/2 − (10/192) f_a, with W = 0.7 B (about 279 Hz with the track's
  filter). The derived limits are 0 Hz and f_a/2, so that nothing folds when the real part is taken; the margin of (10/192) f_a
  below f_a/2 (625 Hz at 12 kHz) is a heuristic, keeping the band off the resampler's transition to f_a.
- **Time.** a[k] stands for the recording's time t₀ + k / f_a (`--start` t₀, s from its first sample), the delays taken out:
  the path's output k from reader sample s stands for reader sample s + (k / f_a − d_p) fs, and reader sample m for the file's
  time m / fs − d_r, so reading starts at s = round((t₀ + d_r + d_p − K / f_a) fs) and the first K = max(⌈2 d_p f_a⌉ + 1, 16) outputs
  are dropped (d_r the reader's resampler delay, d_p the sum of the path's linear-phase delays; K / f_a ≥ 2 d_p, so the first
  output kept rests on input from s on only). Rounding s leaves every output e = s − s_exact input samples late (|e| ≤ ½,
  up to 2.6 µs at 192 kHz; ½ exactly whenever d_p holds a half sample, as from 192 kHz to 12 or 8 kHz, whose decimators have even lengths, 1586 and
  2378 taps). A fractional
  delay at f_a takes it out: a Kaiser-windowed sinc of 33 taps (heuristic) centered at 16 + μ samples, μ = e f_a / fs
  (|μ| ≤ f_a/(2 fs): 0.031 at 192 kHz in and 12 kHz out, 0.125 at 48 kHz in and 12 kHz out), β = 8.078, taps summing to 1; the identity at μ = 0. A burst's edges then land within
  about 0.05 µs of their times at 48, 96, 192 and 250 kHz with either path (measured; the test allows 1 µs, and fails a bookkeeping
  error of one input sample, 5.2 µs). A station off its channel's bin center sees an asymmetric channel response, so its
  edges' half points spread apart (1.2 µs each at 7.8 Hz off, measured): the burst's shape, not a shift. Input before the
  recording's start or past its end counts as zero; a clip past the end is shortened to it; one shorter than an audio sample
  (round(duration × f_a) = 0) is refused.
- **Level.** One fixed gain for the whole clip: the clip's peak to **−1 dBFS** (20 log₁₀(|a|max / 1 FS); 1 dB of room for a
  player's own resampling, heuristic), or `--gain-db`, dB re unity (1 FS in, 1 FS out). Not an AGC: a fixed gain keeps the
  station's level against the noise, and its fades, as the decoder had them, where an AGC would raise the noise between overs
  and flatten fades over its time constant. The clip is held in memory (60 s at 12 kHz: 0.72 M samples), so its peak is known
  before the file is written. The tool reports the power in the band, mean |z|², dBFS re 1 FS² on the recording's scale, and
  the clip's peak and rms, dBFS re 1 FS (a full-scale sine's rms reads −3.01 dBFS by section 0's convention).
- **Output.** Mono 16-bit PCM WAV, 1 FS = 32 768 counts, rounded and clipped (none clip at the default gain).

### Limitations

- A click or static crash in the band sets the peak and leaves the station quieter; `--gain-db` sets the gain by hand.
- The band holds whatever else lies within it: stations 45 Hz apart are heard together at 500 Hz (their tones 45 Hz apart);
  `--bandwidth-hz 100` narrows it (passband ±30 Hz, stopband ±70 Hz).
- The track path follows no drift: the channel stays at f_o, where the decoders' channels follow the detector (section 7).

## A.14 Fading measurement (`kz4ap-fading`)

A tool beside the decoders, in no decoder's chain (text-level-1; the owner's "auroral" clips): how fast one station's carrier fades
on a span of an I/Q recording. `bench/src/fading_main.cpp`. It prints its measurements, writes a table of |ρ(τ)| with `--rho` and
of the fall after a mark with `--fall` (text-fast-1), and nothing else.

### Exact form

- **Input.** `kz4ap-fading FILE.wav --offset-hz F --start S --seconds T [--rho RHO.csv] [--fall FALL.csv] [--end-guard-ms G]`: `WavIqReader` at the file's rate f_s, no
  resampling or mirroring; the span [S, S + T) s from the file's first sample; F the station's offset from the center, Hz. Refused,
  exit status 1 and one line on stderr: an option unknown or without its value, a value that is not a finite number, T not above
  0 s, S below 0 s, a span the file does not hold (no sample read, or fewer than 16 samples after the low-pass), and a span with no
  key-down run (test `FadingTool.*`).
- **Channel.** Mixed by e^(−j2πFn/f_s); low-passed by a Blackman-windowed sinc, cutoff 200 Hz (heuristic: wide enough for keying
  sidebands at 35 WPM, narrow enough to leave out most neighbors), 2 × 8D + 1 taps, D = round(f_s / 1 500 Hz), taps summing to 1;
  decimated by D, to r = f_s / D, about 1 500 samples/s.
- **Noise and key-down.** The noise N: the mean of the lowest 20% of the means of |y|² over 16-sample blocks (10.67 ms; heuristic,
  for a station whose gaps fill more than a fifth of the blocks). Where every block is noise it reads N at 0.677 of the noise's
  mean, −1.69 dB re it (computed: the mean of the lowest fifth of means of 16 exponential values), so the threshold below stands
  +8.3 dB re the noise's mean. Key-down: |y|² > 10 N, above +10 dB re the noise
  (heuristic); a run of key-down samples counts if at least round(8 ms × r) + 2 round(3 ms × r) samples long, and round(3 ms × r)
  are trimmed from each end (heuristic: the low-pass's edges).
- **Doppler spread.** The mean frequency, arg Σ y[k+1] y*[k] over the runs, is removed. |ρ(τ)| = |Σ y[k+τ] y*[k]| / Σ (|y[k]|² −
  N) over pairs inside one run, τ = 1 … 40 samples (to 26.7 ms), the noise's power taken out of the denominator (derived). A Gaussian
  Doppler spectrum gives |ρ(τ)| = exp(−2π²σ_D²τ²) (derived); σ_D by least squares of ln |ρ| on τ² through the origin, over the lags
  with 0.2 ≤ |ρ| ≤ 0.97 (heuristic: above the estimate's noise and past the lags where the noise's own decorrelation dominates),
  and f_D = 2σ_D (Watterson's and DeepCW's two-sigma width; `docs/research/deepcw-generator-notes.md`); NaN when no lag qualifies.
- **Slow fading.** Per whole second, the median |y|² of its key-down samples (more than 20), dB re N; their mean, rms spread and
  range (largest less smallest), dB.
- **Marks and gaps.** Key-down with gaps under 6 ms closed (heuristic); the 10th, 50th and 90th percentile marks and the median gap
  under 300 ms, ms.
- **The fall after a mark.** For each run of at least 12 samples: its median power P_m, its last sample at or above P_m / 4 (−6 dB re
  the mark's median) up to 3 trims past its end, and the mean of |y|² / P_m 0 … 60 samples after that sample, until the next run
  starts, in dB re the mark's median: an echo stands as a shelf for its delay; a clean carrier falls in a few ms through the low-pass.
  With `--fall` (text-fast-1) the same per sample, ms, dB re the mark, runs counted, and the coherence |Σ y[last + j] y*[last − 3]| /
  √(Σ |y[last + j]|² Σ |y[last − 3]|²) over the runs (amplitudes re √P_m): near 1 for a ringing of the same path, near 1/√runs for
  another path's power.
- **Off the runs' ends** (text-fast-1). |ρ(τ)| again with every pair inside [b, e − G) of its run (G = `--end-guard-ms`, 30 ms by
  default), and least squares of ln |ρ| = −aτ − bτ² over the lags with 0.2 ≤ |ρ| ≤ 0.999, for every pair and off the ends, f_D from
  b alone: pairs across a mark's end into another path's tail take |ρ| down in proportion to τ (a), a Doppler spread as τ² (b). The
  first sample of a pair may stand on the run's rise, where its power is lower than its partner's, so |ρ| off the ends can pass 1 (the
  clips: 1.011 at 2.67 ms); the decoder's own estimator (A.8e) normalizes by both ends and stays inside each mark.

### Provenance

- **Calibration.** On our port of VE3NEA's generator the tool reads f_D 3, 10 and 30 Hz as 3.6, 9.6 and 20.4 Hz (measured; |ρ| at
  26.7 ms 0.956, 0.730, 0.237; decision log and backlog, 2026-11-21): close at 10 Hz, high at 3 Hz and low at 30 Hz, where |ρ| falls
  below 0.2 within a few lags and few lags enter the fit. A clean carrier falls to the noise within 2.7 ms after a mark (measured).
  The cutoff, the noise's fifth, the +10 dB re the noise, the trims and the fit's range are heuristics, so a reading is the
  instrument's, to be read against that calibration.

### Limitations

- One station: a neighbor within about ±200 Hz, or its leak, enters the runs and the correlation.
- A station under about +10 dB re the noise key-down has few or no runs; a station whose gaps fill under a fifth of the span reads
  N high.
- Drift within the span reads as fading: only the mean frequency is removed.
- The Gaussian fit over every pair of a run reads a tail of another path after each mark as Doppler spread (the clips: 9.1 and 9.9
  Hz, where the decoder's estimator inside the marks reads about 4.6 Hz; text-fast-1).
