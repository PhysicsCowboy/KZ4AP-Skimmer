# Signal Processing Pipeline

How KZ4AP Skimmer turns a radio's I/Q samples into Morse text. This body is
written to be read: for each stage, what it is for, the idea behind it, the
formula or two that carry the idea, and the values that matter. Every value
carries its unit and its class: **derived** (it follows from the
mathematics), **measured** (set by an experiment), **heuristic** (a judgment
call, not measured), **placeholder** (kept until an experiment settles it)
or **owner** (the owner's decision).

The **appendix** (section A) is the reference. For each section it gives the
exact form of every rule as the code implements it (update orders,
clipping, edge cases), the derivations, where each measured or heuristic
value came from and when it is valid, the known limitations, the full
parameter table (A.10) and the definitions used by the tests and the
benchmark (A.11). Results and evidence are not here: they are in the
results records under `docs/plans/` (milestone 2a, stage 1, Plan A,
Plan B).

**Maintenance rule:** any change to the engine's signal processing updates
this body and, where a derivation, provenance or limitation changes, the
appendix, in the same commit. Numbers are for the default 192 kHz input
unless stated.

## 0. Units and symbols

Nothing is calibrated to volts or dBm. Amplitudes are in **FS** (full
scale: the WAV's 16-bit integers divided by 32768), powers in **FS²**, and
absolute levels in **dBFS** (dB relative to 1 FS²; a complex tone of
amplitude 1 FS reads 0 dBFS). A signal-to-noise ratio always names the
bandwidth its noise is measured in, e.g. "dB SNR in 500 Hz". Log-likelihoods
are in nats (natural logarithms). Time constants are in seconds, or in
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

Several dit-like quantities appear in the bank decoder (section 8c); they
are different things:

| Symbol | What it is |
|---|---|
| dit | the generic dit of a speed, 1.2 s / WPM |
| d_k | branch k's nominal dit, fixed: L_k / 0.8 (12 ms on branch 1 to 230 ms on branch 32) |
| T | the dit a branch has fitted to its own marks and spaces (the duration fit) |
| τ_c | a candidate dit tried by the periodicity estimator (303 values, 12 to 242 ms) |
| T_P | the periodicity estimator's result: the best confident candidate |

## Overview

```
I/Q (fs) ─┬─► spectrum analyzer ─► signal detector ─► tracks (frequency, SNR)
          │                                              │ open / close
          │                                              ▼ channels
          └─► channelizer ─────────────────────► one stream per station, 1500 samples/s
                                                         │
                                                         ▼
                                                decoder per channel ─► text
                                                (Matched default; Envelope; bank)
```

The input is the whole band the radio delivers, 192 kHz wide. Two things
happen to it in parallel. The **spectrum analyzer and detector** look at
the band every 21.33 ms and decide where stations are: each station becomes
a *track* with a frequency. For every track the **channelizer** cuts a
narrow stream out of the band, about ±150 Hz around the station, at 1500
samples/s. A **decoder** per channel turns that stream into characters.
There are three decoders: **Matched** (the default), **Envelope** (the
original baseline) and the **bank** (the current development focus). Before
each block of samples the engine tells the decoder where the detector now
thinks the station is (the *anchor*).

## 1. Input

A 16-bit stereo WAV, left channel I and right channel Q, divided by 32768,
so samples lie in [−1, 1) FS. Frequencies are offsets from the radio's
center frequency, in Hz, spanning ±fs/2. The engine collects incoming
samples and processes them in fixed blocks of exactly N/2 samples (the
hop), always at the same sample positions, so the output does not depend on
how a caller splits the input (one sample at a time or a whole file at
once).

## 2. The FFT size N and bin width Δf

N is the largest power of two that keeps the bins at least 20 Hz wide:
8192 at 192 kHz, so Δf = 23.44 Hz and each transform spans 42.7 ms. This is
a heuristic. The real intent is "bins about 23 Hz wide": narrow enough to
separate stations a few tens of Hz apart, wide enough that most of a keyed
CW signal's energy falls within one to three bins up to about 40 WPM. In
random keying about 44% of the key-down power (the key-down fraction) is a
pure carrier line, always in one bin; the rest is keying sidebands whose
main lobe spreads over about ±1/dit: ±21 Hz at 25 WPM, ±33 Hz at 40 WPM,
±50 Hz at 60 WPM (derived). The occupied bandwidth by the ham rule of thumb
(about 4 × WPM in Hz) is wider. The detector does not need to follow the
keying: it averages each bin over 1 s (section 6).

Appendix: A.2.

## 3. Hops and 50% overlap

New input arrives in blocks of N/2 samples (the hop), but each FFT covers
N samples: the newest block and the one before it. So consecutive FFTs
overlap by half, and every sample appears in two consecutive FFTs. With
the Hann window (section 4) the overlapping windows add up to a constant,
so no part of the signal is weighted less than another (derived). The hop
also fixes the channel filter's length (section 7).

Appendix: A.3.

## 4. Spectrum analyzer (for detection)

Every hop, the analyzer takes the last N samples, multiplies them by a Hann
window and computes an FFT. The power in bin k, normalized so that a tone
exactly on a bin reads its true power, A² FS², is

  P[k] = |X[k]|² / (Σw)²  (FS²),

with X the FFT of the windowed samples and Σw = N/2 the window's sum. A
complex tone of amplitude A on a bin reads A² FS². White noise of σ² FS²
per sample reads as the noise in one *equivalent noise bandwidth* (ENBW):
1.5 bins = 35.2 Hz for the Hann window (derived). The values are stored in
dBFS. The Hann window is a standard choice: its sidelobes are 31 dB below
the main lobe (relative to the main lobe's peak), so a strong station does
not hide a weak one a few bins away; a tone halfway between bins reads
1.42 dB below its on-bin value (derived).

Appendix: A.4.

## 5. What "SNR" means here

**S₅₀₀** is the key-down carrier power over the noise power in 500 Hz, the
convention of CW Skimmer and the Reverse Beacon Network. The synthetic test
recordings always add the same noise, complex white noise of σ = 0.02 FS;
a station's S₅₀₀ is set by its carrier amplitude. The detector sees a
narrower noise bandwidth, one bin's ENBW of 35.2 Hz, so the same signal
reads 10·log₁₀(500/35.2) = **11.5 dB higher** as an SNR per bin than as an
SNR in 500 Hz (derived, white noise). A track's reported SNR is lower than
its key-down SNR per bin, and by how much depends on the speed. With a
key-down fraction D, the averaged power is a carrier line of D² of the
key-down power, always in the peak bin, plus keying sidebands of D − D²,
spread over about ±1/dit, so less of them fall in the bin the faster the
keying. At D = 0.44 the track reads between about 3.6 dB (slow keying: all
sidebands in the bin) and 7.1 dB (very fast: the carrier line only) below
the key-down SNR per bin (derived), plus up to 1.42 dB if the tone sits
between bins. Example at 25 WPM: a station at 20 dB SNR in 500 Hz is
31.5 dB SNR per bin key-down and reads about 27 dB SNR per bin as a track.

Appendix: A.5.

## 6. Signal detector

The detector's job is to find stations and follow them. It works on the
spectrum, once per hop.

**Averaging.** Each bin's power is averaged over time with a one-pole
average of time constant **τ = 1 s** (heuristic), in linear power; during
the first second it is a plain running mean. Keying comes and goes, so
the average shows a station as a steady peak above the noise.

**Noise floor.** The median of all 8192 averaged bins, every hop: one
number for the whole span (heuristic). Stations occupy few bins, so the
median is very close to the noise.

**Finding a station.** A bin becomes a candidate when its average is at
least **6 dB** above the floor (dB SNR per bin, since the floor is the
noise per bin) and is the highest within **±47 Hz**. A peak within the
channel distance D_ch = 47 Hz of an existing track belongs to that track,
so no second track is born there: two stations closer than 47 Hz share one
channel, and a station farther away gets its own.
It must stay a candidate in every hop for **0.5 s** before a
track is born; nothing is detected in the first second (all heuristic).
The track's frequency is refined between bins by fitting a parabola to the
peak bin and its two neighbors (in dB).

**Following a station.** Every hop, each track moves to the strongest
qualifying peak within the **channel distance D_ch = 47 Hz** of where it
is, or stays put if there is none. So one track follows a drifting
station, or moves to an answering station within 47 Hz. 47 Hz is about the
half-width of the Hann window's main lobe, 2/(42.7 ms) = 46.9 Hz
(heuristic; owner). A track stays active while its level is at least
**3 dB** above the floor (dB SNR per bin; 3 dB of hysteresis below the
birth threshold), and dies after **10 s** inactive. At most **200** tracks
exist; a stronger newcomer replaces the weakest (heuristic, to bound the
CPU).

With the Envelope decoder the detector uses older rules (frequency fixed at
birth). **Oracle mode**, for the benchmark only, skips the detector and
opens a channel at each labeled station's frequency.

Appendix: A.6.

## 7. Channelizer: one stream per station

For each track, the channelizer produces the narrow stream its decoder
reads: in effect it shifts the station to 0 Hz, low-pass filters, and keeps
one sample in 128.

- **Center.** The channel is centered on the track's birth frequency
  rounded to the nearest bin, so the station starts within ±11.7 Hz of
  0 Hz. The channel never moves; the decoder sees no signal from before the
  track's birth.
- **Rate.** Decimating by 128 gives **r = 1500 samples/s** (0.67 ms per
  sample, far shorter than a 20 ms dit at 60 WPM).
- **Filter.** A linear-phase low-pass of 4097 taps (21.3 ms; the longest
  the shared FFT allows, derived), cut off at **±150 Hz** (−6 dB relative to
  the passband; heuristic, sized for keying up to about 60 WPM). It is flat
  to within 0.015 dB of the passband out to about 39 Hz and at least 74 dB
  below the passband beyond about 279 Hz. Its noise bandwidth is 252 Hz;
  its delay 10.7 ms.
- **Computation.** One FFT of the whole band per hop is shared by all
  channels; each channel takes its 64 bins, applies the filter's response
  and inverts them (overlap-save). The result equals shifting, filtering
  and decimating directly, up to rounding (derived).

**Frequency re-centering (Matched only).** The station may sit up to
11.7 Hz off 0 Hz and may drift. The Matched decoder's tracker measures the
offset and shifts it out, sample by sample. It compares the matched
filter's output with itself 5.33 ms earlier: the phase change over that
lag is 2π × offset × 5.33 ms, so the offset is read directly (no feedback
loop; derived). Only key-down samples count (weighted by Matched's
probability of key-down), averaged over **0.5 s** of key-down time
(heuristic). The detector decides which station; the tracker only
fine-tunes: its estimate is accepted only within **±12 Hz** of the
detector's frequency (heuristic; owner), and it can shift by at most
±75 Hz within the channel (heuristic; the channel filter is 0.34 dB below
its passband there). The channel itself never moves.

Appendix: A.7.

## 8. The decoders

### Which decoder runs

Three decoders exist; the bench's `--decoder envelope|matched|bank` picks
one (`--front-end` is the old name of that option, still accepted).
**Matched is the default** (owner). The bank is selectable, not the default
(owner). Envelope and Matched share a common back end that turns marks
into characters; the bank has its own.

### Envelope (not the default)

The milestone-1 baseline, heuristic throughout. It takes the magnitude of
the channel stream, smooths it over about a quarter dit, and follows two
levels: a *mark level* that jumps up quickly (4 ms) and decays slowly (3 s),
and a *space level* that does the opposite. The key is down when the
smoothed magnitude is above 60% of the way from the space level to the mark
level, and up below 40% (hysteresis). It keys nothing unless the mark level
is at least 3 times the space level (a squelch).

### The shared back end (Envelope and Matched)

From key-down and key-up times to characters:

- Marks shorter than 0.3 dit are ignored, and key-up gaps shorter than
  0.3 dit are bridged.
- A mark longer than 2 dits is a dah. A space longer than 2 dits ends the
  character; longer than 5 dits also ends the word.
- Characters come from a 62-symbol table; eight or more dits read `<HH>`
  (the error signal), and a pattern not in the table reads `*`.
- **Speed.** After every mark, the dit is re-estimated from the last
  **24 marks**: they are sorted and split into dits and dahs at the largest
  ratio between neighbors; when the dah/dit ratio is plausible (3.0 to
  3.85), dit = (mean dah − mean dit)/2, which cancels a constant lengthening
  or shortening of every mark (measured band). The speed is kept within 5 to
  60 WPM and starts at 25 WPM. Heuristic apart from the measured band.

Appendix: A.8.

## 8b. Matched decoder (the default)

The Matched decoder replaces Envelope's thresholds with a matched filter and
a likelihood ratio. Per channel, in order: frequency re-centering
(section 7), a filter 0.8 dit long, a probability of key-down for every
sample, keying on that probability, then the shared back end.

**The filter.** A dit is a rectangular burst of carrier; the filter that
maximizes its SNR is a moving average (a *boxcar*) as long as the burst
(derived). The decoder uses a boxcar of **0.8 dit** (K samples; 58 at
25 WPM), a heuristic that costs 0.97 dB of output SNR relative to the full
dit (derived) but keeps short marks resolvable. Its noise bandwidth at
25 WPM is 25.9 Hz, so it passes 9.9 dB less noise power than the 252 Hz
channel. The filter starts at the 60 WPM width (16 ms). Once the speed
estimate rests on 8 marks, the length follows the decoder's own speed
estimate, growing at most ×1.25 per mark (heuristic; owner). A mark whose
start was not seen (it began while keying was impossible) is decoded but
not counted for speed.

**The probability of key-down.** With v the filter output, σ the noise per
real component and ŝ the station's amplitude, each filter output gets the
log-likelihood ratio of key-down against key-up: how much more likely the
observed |v| is with a carrier plus noise (Rician) than with noise alone
(Rayleigh):

  Λ = −a²/2 + ln I₀(a·x)  (nats),  x = |v|/σ,  a = ŝ/σ.

(The Bessel function I₀ is what remains of the Rician density once the
carrier's unknown phase is averaged out.) The amplitude-to-noise ratio a is
tied to S₅₀₀ by a² = 2·S₅₀₀·(500 Hz)·K/r, S₅₀₀ as a linear ratio
(derived, for noise flat across the filter): a = 6.2 at S₅₀₀ = 0 dB and
25 WPM. Adding the prior log-odds of key-down, ln(0.44/0.56) (PARIS text keys down
22 of every 50 dit units; derived), gives g; the probability is
p = 1/(1 + e^(−g)). The key goes down when g > +1 nat and up when
g < −1 nat (heuristic hysteresis). Each sample is judged on its own: neighboring outputs of the
boxcar share most of their input, so they are strongly correlated, and the decoder does not add
their likelihood ratios up as independent evidence.

**What it must estimate.** The rule needs ŝ and σ, both learned on the air:

- **Station amplitude ŝ²:** a running average of |v|² − 2σ² over key-down
  samples (weighted by p), with a memory of **0.5 s** of key-down time
  (heuristic). The mean of |v|² while keyed is ŝ² + 2σ², hence the
  subtraction.
- **Noise σ²:** the *three-tap guard*. It looks at three filter outputs
  K samples apart, which share no input samples. If the middle one is
  small (|v|²/2σ² below κ = 1.75) and both neighbors are small too (below
  4), the middle sample is taken as noise. Accepting only small samples
  biases the average low by a known factor, the mean of an exponential
  truncated at κ, m(κ) = 0.632, which the update divides out (derived). Its
  memory is **2 s** (heuristic). In noise alone, the only stable value is
  the true σ² (derived).
- A floor (the 10th percentile of recent |v|², scaled) keeps σ² from
  sinking below what noise can produce, and a 0.32 s warm-up sets both
  estimates at the start (both heuristic forms with derived scaling).

**Squelch.** p is forced to 0 while the station's amplitude is below
a_min = 3 × (filter length / 16 ms)^(1/4) in units of σ: the scaling keeps
noise's false-pass rate the same at every filter length (derived), the 3 is
heuristic. Since a station is acquired at the 16 ms width, this sets the
squelch threshold at S₅₀₀ = −2.5 dB at every speed (derived, with the true
amplitude; up to 1.4 dB higher with the estimate's bias). That is a lower
bound on sensitivity, not the decoder's working sensitivity.

**Re-acquisition.** After the key has been up for the longer of 0.5 s and
12 dits, the decoder assumes a new transmission may start at a new speed:
the filter returns to its acquisition width, the amplitude and frequency
estimates restart, and the speed window is set aside so the next station's
marks are not mixed with the last one's. If nothing is keyed within 2 s,
the old speed window and filter width come back (the amplitude and
frequency estimates stay restarted). Heuristic.

Appendix: A.8b.

## 8c. Bank decoder (selectable; the current development focus)

**The idea.** Matched's filter has to know the speed to be the right
length, and the speed comes from what the filter decodes: a feedback loop
that can lock onto a wrong speed. The bank removes the loop. It runs
**32 filters at once**, one per speed, from 100 WPM down to 5 WPM; each
*branch* keys, times and spells its own text with no knowledge of the
others; a selector then decides which branch's text to publish, and
corrects published text when it changes its mind.

Two words recur below. An **over** is one station's turn to transmit (from
radio practice); the decoder treats a long enough silence as the end of an
over, because the next transmission may come from another station, at
another speed and level. A branch's **amplitude** is its estimate of the
station's key-down carrier amplitude, which the likelihood ratio needs.

**Input.** The channel stream is shifted by the detector's current
frequency (the anchor) so the station sits near 0 Hz; there is no separate
tracker. Whatever offset remains, each branch's filter attenuates it.

**The branches.** Branch k is a moving average of length

  L_k = 9.6 ms × 1.1^(k−1),  k = 1 … 32  (9.6 ms to 184 ms),

each 10% longer than the last (the ratio and speed range owner; the
filter as 0.8 of a dit heuristic, as in Matched). Its *nominal dit* is
d_k = L_k/0.8: 12 ms (100 WPM) on branch 1, 50 ms on branch 16 (24 WPM),
230 ms (5.2 WPM) on branch 32. A longer branch passes less noise, so a slow
station gets a cleaner signal on its own branch. In Matched's terms the
branch sees a² = 2·S₅₀₀·(500 Hz)·L_k (derived, white noise): its own SNR,
a²/2, is S₅₀₀ + 6.8 dB on branch 1, + 12.8 dB for a 25 WPM filter (38.4 ms,
between branches 15 and 16) and + 19.6 dB on branch 32 (each in dB SNR in
that branch's own noise bandwidth). Each branch computes Matched's
likelihood ratio Λ and probability p from its own output.

Everything advances in blocks of **32 samples (21.33 ms)**: noise, keying,
the speed estimate, the branches' fits and decisions, selection, and
publishing, in that order.

### Noise

Every branch needs its own noise level σ²_k. The noise in the channel is
not white: the 252 Hz channel filter shapes it, and interference adds
color. So σ²_k does not simply scale as 1/L_k; each branch's noise is the
noise spectrum S(f) weighted by that branch's filter, ∫ S(f)·|H_k(f)|² df.
The bank therefore takes the **level** from branch 1 and the **ratios**
between branches from a measured noise spectrum (the method chosen by
measurement in the stage-1 prototype).

- **Level.** Branch 1 runs Matched's three-tap guard (memory 2 s). For the
  first 0.32 s of non-zero input, a warm-up sets it from the 20% quantile of the
  output power (scaling derived for noise). Unlike Matched, the bank has no
  floor that lifts σ².
- **Spectrum.** The channel stream is cut into segments of 171 ms
  (5.86 Hz bins). Samples near any output of branch 1 stronger than
  4 × its noise power (|v_1|²/2σ² ≥ 4, the guard's neighbor threshold) are
  left out, with a margin of **4.8 ms** on each side (half of branch 1's
  filter; heuristic, owner); a segment counts only if at least half of it
  is left (heuristic). This is a level test on branch 1's output, not its
  keying: no keying decision feeds the noise estimate. The kept segments
  build an averaged spectrum (2 s memory), smoothed over ±25 Hz
  (heuristic).
- **Ratios.** Each branch's noise is that spectrum weighted by its filter
  response, divided by branch 1's, times branch 1's level. Leaving out
  samples near signals removes mostly low-frequency power, which the long
  branches weight most, so the masked spectrum reads each branch's noise
  slightly low; a factor per branch, 0.84 (branch 1) to 0.78 (branch 32),
  corrects it (measured in white noise, 200 seeds; valid only for these
  defaults).
- **Exact zeros** are treated as missing data: a receiver's output always
  carries noise, so a run of exact zeros is padding or a dead channel, not
  a measurement (derived). Until the first non-zero sample the channel
  decodes nothing.
- **Recovery.** If the noise rises suddenly by a large factor, the guard can
  accept no sample again and the estimate would stay stuck low. If branch 1
  accepts nothing for **8 s** (4 × the 2 s memory; heuristic), every
  branch's noise is re-initialized from its recent input. In seconds, not
  dits, because noise has no keying speed.

### Keying time constants

The keying time constants are in seconds, the same for every branch, each
with its class (owner):

| Setting | Value | Stage 1 | Class |
|---|---|---|---|
| Re-key wait | 0.8 s of keyed time, every branch | the same | seconds (stage 1's, measured by E9b) |
| Re-key time-out | 2 s from the amplitude becoming unknown | the same | seconds; heuristic |
| Periodicity windows | 5, 10 s for every candidate | 2, 5, 10 s | seconds, class 2: latency (owner); values placeholder; 2 s dropped (owner, 2026-10-07) |

Plan B measured variants in the branch's dits and in marks; they were
removed when the bank was frozen as the reference (2026-10-07; Plan B
record, sections 6 and 10–13).

### Keying

Each branch decides key-down or key-up for every sample, in one of two
modes.

**When the amplitude is known**, it uses Matched's rule: key down when the
log-odds g exceeds +1 nat, up below −1 nat, with a per-branch squelch,
a_min,k = 3 × (L_k/16 ms)^(1/4) (2.6 on branch 1 to 5.5 on branch 32, in
units of σ_k). The amplitude keeps tracking the station: in every block,
the key-down samples (weighted by p) give a fresh estimate of the carrier
power, their mean |v|² less the noise's 2σ², and the running estimate ŝ²
moves part of the way toward it, with a memory of **0.5 s** of key-down
time (heuristic). It starts from the seed below, counted as the keyed time
the seed rests on.

**When the amplitude is unknown** (at the stream's start and at every new
over), the likelihood ratio cannot be computed. The branch then keys with a
plain threshold on its output in units of the noise: key down when
x = |v|/σ exceeds x_on,k (4.64 on branch 1 to 4.20 on branch 32,
**measured** so that noise alone produces 0.01 false marks per second per
branch; the target heuristic), key up below 1.55 (noise alone exceeds it
30% of the time; heuristic). Marks keyed this way are *provisional*: the
key releases only when the filter's falling ramp drops below the low
release threshold, near the end of the ramp, so each mark comes out too
long by up to L_k (the high on-threshold shortens its start somewhat;
derived). They are not used to fit the timing.

**The re-key cycle:**

1. A new over begins (or the stream starts): the amplitude becomes unknown,
   and the branch keys provisionally.
2. The time-out counts from that moment, and the stretch to be re-keyed
   starts there.
3. After **0.8 s of keyed time** (stage 1's value, measured at 25 WPM), it
   seeds the amplitude from the keyed samples: their 90% quantile less the
   noise (heuristic).
4. It **re-keys** the stretch (at most 20 s back) with the full rule, at
   two candidate amplitudes: the seed, and the previous over's amplitude if
   any. The candidate whose fit explains the stretch better wins
   (heuristic); its characters replace the provisional ones.
5. **Time-out:** without 0.8 s keyed within **2 s**, the stretch is
   re-keyed at the previous over's amplitude if that keys anything;
   otherwise its provisional characters are deleted and the count starts
   afresh. This removes noise keyed after a station stops.

These are stage 1's settings (owner).

### Duration fit

Each branch learns the station's timing from the durations of its marks
and spaces. Five kinds of interval are modeled, each around a median:

| Interval | Median |
|---|---|
| dit | T + w |
| dah | qT + w |
| space inside a character | T − w |
| space between characters | 3T_g − w |
| space between words | 7T_g − w |

T is the dit, q the dah/dit ratio, w the *weighting* (how much every mark is
lengthened and every space shortened, e.g. by a transmitter's keying or by
the filter), and T_g the gap timebase (equal to T in standard spacing,
longer in Farnsworth spacing). Around each median, ln(duration) scatters as
a normal distribution, of width 0.15 for marks and 0.25 for spaces
(heuristic), widened by the branch's timing resolution (each edge can be
moved by noise by about L_k/a, derived). The five kinds' prior
probabilities come from VE3NEA's character and word statistics (derived);
5% of intervals are allowed to be anything between 1 ms and 10 s (outliers;
heuristic).

**Finding the best fit.** For every point of a grid over (T, q, w, T_g),
the branch keeps the log-likelihood of all intervals seen so far, with older
intervals fading out: memory of **48 intervals** (measured). The grid: T in
1% steps from 12 to 242 ms (step a placeholder); q ∈ {3, 4, 5}; w/T ∈
{−0.4, 0, 0.4, 0.8}; T_g/T ∈ {1, 1.59, 2.52, 4, 6.35} (grids measured).
The best grid point, nudged toward the speed estimate T_P when one exists
(next section), is refined by two Gauss–Newton steps on the last 192
intervals, and kept only if it does not lower the likelihood (heuristic).
The fit's **quality** is its mean log-likelihood per interval, in nats. A
mark is then a dah if the dah explains it better than the dit; a space is
whichever of the three kinds explains it best.

(The likelihood over the grid is the decoder's main cost; it is evaluated
in a fast, vectorized form that agrees with the plain formula to the last
bits. A.8c.)

### Periodicity: a coarse speed from the rhythm

A second, independent speed estimate T_P helps the fits find the right
speed quickly and helps selection when no fit is yet trustworthy. It looks
for the rhythm of the keying rather than for individual marks.

The input is branch 1's probability of key-down p over a recent window.
Morse keying is not periodic, but it lives on a grid of dits, and
consecutive edges one dit apart have opposite signs. So p's autocorrelation
is low at odd multiples of the dit and high at even ones, and the natural
period to test is a dit plus a space (the argument derived; the comb on it
chosen by the owner, E1). For each candidate dit τ_c the estimator computes
a *comb* score: the normalized autocorrelation of p (1 at lag 0) at 2τ_c,
4τ_c, 6τ_c and 8τ_c, each minus the mean of its values halfway on either
side, averaged over the four. The score is dimensionless
and bounded by 2 in magnitude (an unbroken string of dits scores about
1.5). For random machine-keyed text the true dit scores about 0.31–0.37 and
its alias at three times it about 0.18–0.24 (derived from the keying's autocorrelation
as measured on noise-free keying:
`docs/research/2026-10-05-periodicity-windows-and-rekey-analysis.md`, §2).
Recomputed every 0.25 s.

**Windows.** The estimate uses only recent p, because the station
changes (a stream starts, an over begins, the speed changes): a short window
follows a change quickly, a long one is steadier. Two run at once,
**5 and 10 s** (placeholder values); in each the best-scoring candidate
is *confident* at a score of at least **0.03** (placeholder), and T_P is
the confident estimate of the shortest window. The comb's outermost lag,
9.15·τ_c, must fit in half the window, so a window W scores only
τ_c ≤ W/18.3 (derived): 5 s reaches down to 4.4 WPM, 10 s to 2.2 WPM. The
windows are fixed in seconds for every candidate (owner, 2026-10-06): they
exist for latency after a change, felt in seconds; the reach limit is
physics any rule must wait for. Stage 1's 2 s window (reach 11 WPM) was
dropped (owner, 2026-10-07): below about 10 WPM it returned a false match
near a third of the dit, scoring just above 0.03, which as the shortest
confident window set T_P and pulled the fits to half the dit (measured).
The cost: a window scores only once full, so the first T_P comes after 5 s
of a stream, not 2 s. T_P never feeds back into its own estimate.

### Choosing which branch to publish

A branch is **eligible** when its fitted dit matches its own filter
(L_k within one ladder step of 0.8 × its fitted T) and its fit has seen at
least 8 intervals (heuristic): it is decoding at its own speed. Among
eligible branches the best fit quality wins, refined as follows:

- Branches within 0.05 nats per interval of the best quality are a tie
  (placeholder).
- If every tied branch has text, only those whose text is within 0.1 nats
  per character of the likeliest text stay tied (heuristic). The text score
  is a per-character language score over the last 10 characters
  (placeholder window), from VE3NEA's character frequencies (derived);
  codes missing from his table and `*` are penalized (heuristic values).
- Of what remains tied, the **longest branch** wins: it has the best SNR.

If no branch is eligible, selection falls back to clearly better text (a
lead of at least 1 nat per character; heuristic), else to the branch
nearest the speed estimate T_P, else to branch 1. To avoid flicker, the
published branch changes only after the same other branch has been best at
**4** consecutive selection instants (placeholder). Selection runs whenever
branch 1 keys up.

### Channel decoder: overs and corrections

- **Marks and spaces** are timed with each branch's filter delay removed
  and fed to that branch's fit; provisional ones are not fitted.
- **A new over.** When a branch's key has been up longer than the larger of
  0.5 s and 12 gap units T_g (0.58 s at 25 WPM; before any fit, T_g is
  taken as 240 ms, so 2.88 s; placeholders), the branch ends its character,
  keeps its fit as "the previous over's", starts a fresh fit, and its
  amplitude becomes unknown (the re-key cycle above). Until the re-key it
  keeps decoding with the previous over's fit.
- **Fresh fit or previous fit.** After a new over, both fits keep running;
  the fresh one takes over only when it has 8 of the over's intervals
  (placeholder) and explains them clearly better (a penalized likelihood
  test in the BIC form; the form derived, its constant heuristic).
- **Corrections.** When selection switches branch, or a re-key or time-out
  changes a branch's text, the published text is corrected from the point
  where it changed, but never more than **20 s** back (owner). The engine
  passes each correction to the display as "keep the first n characters,
  then append these"; the *final* text has every correction applied, the
  *immediate* text none. The bank publishes text only: the speed,
  confidence and per-character probability fields of its output are
  placeholders.
- **End of stream:** every open character is ended and published.

**Cost.** About 8 to 14 MB of memory per channel (derived from the arrays
allocated), and about 42 ms of CPU per second of channel on the Linux test
machine at the current defaults (measured, Plan B), against about 0.6 ms for
Matched (measured).

Appendix: A.8c.

## 9. Timing and latency

| Stage | Delay |
|---|---|
| Processing block (hop) | 21.33 ms |
| Detection | nothing in the first 1 s; then about 0.5–1.5 s for a new station |
| Channel filter | 10.7 ms |
| Envelope | about ¼ dit of smoothing |
| Matched | 0.32 s warm-up per channel; filter delay 19 ms at 25 WPM |
| Envelope, Matched | a character appears once a gap of more than 2 dits follows it |
| Bank | a character appears when the space after it is classified (at the next key-down), at a new over, or at the stream's end; corrections reach up to 20 s back |
| Track removal | about 6–7 s for the average to decay (S₅₀₀ = 20 dB), plus the 10 s timeout |

Appendix: A.6, A.9 and A.10.

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
scale: the WAV's 16-bit integers divided by 32768), power in **FS²**,
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

## A.2 The FFT size N and bin width Δf

### Exact form

The rules of this section as the code implements them (moved verbatim from the body on 2026-10-05, when the body was rewritten for reading).

N is the largest power of two with fs/N ≥ 20 Hz: 8192 at 192 kHz,
Δf = 23.44 Hz, each transform 42.7 ms long. Heuristic; the real parameter
is "bins about 23 Hz wide" (the 20 Hz threshold only reproduces it at
48 kHz × 2^j rates).


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

**Limitations.** Frames are stored in dB with a future waterfall display
in mind; the detector needs linear power (section 6), so every value is
converted to dB here and back to linear there. That round trip is wasted
work; the backlog has an item to store linear power and convert only where
dB is needed.

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
- The cap of 200 (`DetectorConfig::max_tracks`, a config parameter not yet
  exposed to users): heuristic, a guard against CPU overload, not a
  measured limit.

**Limitations.**

- **The floor is global:** one number for the whole span. Fine for the
  flat synthetic noise, but a real SDR's passband is not flat (it rolls off
  toward the band edges), and there is often a spike at 0 Hz (DC offset).
  Where the local noise is above the median, noise crosses the threshold
  more easily (false tracks); where it is below, weak stations are harder
  to detect and their SNR reads low. The backlog has a local floor as a
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

## A.7 Channelizer

### Exact form

The rules of this section as the code implements them (moved verbatim from the body on 2026-10-05, when the body was rewritten for reading).

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

There are three decoders. `ClassicalDecoderConfig::front_end` (a code
identifier) selects one, and `kz4ap-bench --decoder envelope|matched|bank`
sets it (`--front-end` is the option's old name, kept as an alias). **Matched
is the default** (the code's and the bench's; owner, 2026-09-29); Envelope
also switches the detector to the Envelope path's rules; the bank is
selectable, not the default (owner decision D1). The bank uses the default
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
statistics and a prior over likely text"; the bank decoder (section 8c) is
the probabilistic successor under development.

**Matched in the classical decoder.** `Matched` replaces steps 1–5 with
section 7's re-centering and section 8b's matched filter and likelihood:
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
  a different one (a QSO turnover): section 8b's filter returns to the
  60 WPM width and its amplitude estimate restarts, section 7's frequency
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
  heuristic; the 2- and 5-dit gap thresholds: standard midpoints.
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
  S₅₀₀ = −2.5 dB at every speed (derived).
- **Keying.** Down when g > +1 nat, up when g < −1 nat (heuristic
  hysteresis); never down while squelched or warming up.
- **Re-acquisition** (heuristic): after the key has been up for
  max(0.5 s, 12 dits), K returns to 24, ŝ² restarts, the frequency average
  restarts, and the speed window is set aside; if nothing is keyed within
  2 s, the old window and width come back.


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
- **Likelihood** (Proakis eq. 4.5–21). ln I₀ from Abramowitz & Stegun
  9.8.1–9.8.2 without overflow. P₁ = 0.44: PARIS keys down 22 of 50 dit
  units. For noise flat across the filter, a² = 2·S₅₀₀·(500 Hz)·K/r, S₅₀₀
  as a linear ratio: a = 6.2 at S₅₀₀ = 0 dB and 25 WPM.
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
  changes ℓ_total only in its last bits (bounds derived in A.8c).

#### Periodicity (bank decoder)

A coarse speed T_P from branch 1's keying, independent of the fits; it
feeds the fits' prior and selection's fallback, and never itself.

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

#### Channel decoder (bank decoder)

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
correction applied, the *immediate text* none. Speed, confidence (0) and
per-character probability (1) are placeholders.

**Cost.** Memory is about 8.3–13.6 MB per channel (derived from the
allocated arrays; chiefly the 21.1 s window of |v_k|² and the fits). CPU
is about 42 ms per channel-second at the defaults on the Linux test
machine, against about 0.6 ms for Matched.


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
  Two windows W = 5 and
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
  halfway between. **Why 2T and not T:** consecutive keying edges T apart
  have opposite signs, so p's structure repeats at 2T; its autocorrelation
  is low at odd and high at even multiples of T, and a comb with teeth at
  multiples of T would peak at 2T (the comb was corrected to Π = 2T on
  2026-09-30, spec 4.4).
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
  his table (E: ln(321/2688) = −2.125 nats); a valid code missing from it
  (prosigns other than <BT>, rarer punctuation) gets his rarest
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
| Track removal | the average's decay (~6–7 s at S₅₀₀ = 20 dB) plus the 10 s timeout |


Nothing beyond section 9: its delays are derived in the sections it names (A.6 for the average's decay, A.7 for the channel filter's group delay, A.8c for the bank's blocks and branch delays).

## A.10 Parameters at a glance

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
| Frequency average | τ_f = 0.5 s of key-down weight; moves the NCO at weight ≥ 0.6 and coherence > 0.3, every 21.3 ms | `FrequencyTrackerConfig` (`tau_s`, `min_weight`, `min_coherence`, `update_interval_s`) | heuristic; the resulting accuracy measured (median 0.11 Hz at S₅₀₀ = 5 dB, 0.13 Hz at 0 dB, group F; A.7) |
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
| Bank amplitude seed and re-key | while unknown: ŝ² = 90% quantile (by selection, bit for bit the sorted form; B4f) of the last ≤ 4 × W_min of keyed \|v\|² − 2σ², FS²; ready to re-key after W_min = 0.8 s of keyed time on every branch (memory 3.2 s); `rekey_after_s` must be positive (a 0, which once selected the removed variants, is refused); class: seconds (stage 1's measured 0.8 s, E9b) | `BankConfig::rekey_after_s` (0.8), `seed_memory_rekeys`; `bank::check_rekey_times`, `bank::BankKeyer`, `bank::rekey` | 0.8 s measured (E9b, stage 1; the default again since Plan B's B4g, owner 2026-10-06); memory and 90% quantile heuristic |
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
| Bank re-key and time-out | the over's start re-keyed with the full LLR once the over has a mark and 0.8 s of keyed time, at the seed s² and the previous over's s² (best mean log-likelihood per element wins), over the stretch (at most 20 s back); if that is not reached within 2 s of channel time from the amplitude becoming unknown (every branch), re-keyed at the previous over's s², or its provisional characters deleted; both `rekey_after_s` and `rekey_timeout_s` must be positive (a 0, which once selected the removed variants, is refused); class: seconds | `BankConfig::rekey_after_s` (0.8), `rekey_timeout_s` (2.0); `bank::check_rekey_times`, `bank::Branch::rekey_over`, `clear_over` | stage 1's (wait measured, E9b; time-out heuristic; the default again since Plan B's B4g, owner, 2026-10-06, option a′); candidate choice heuristic |
| Bank re-key clocks | stage 1's: the time-out counts and the stretch starts where the amplitude became unknown; a cleared time-out restarts only the count | `bank::BankChannel` | heuristic (stage 1) |
| Bank re-key clocks, variant (removed) | the count from the branch's first provisional mark, the stretch's lead 7 · d_k, a cleared time-out moving the stretch (B4d); removed 2026-10-07 | — | measured: Plan B record, section 10 |
| Bank fresh fit against the previous | the over's fresh fit replaces the previous over's continued fit with ≥ 8 of the over's marks and spaces and a log-likelihood gain > ½ · 4 · ln n nats on them (4.16 nats at n = 8); the competition ends after 192 (⌈4 N_mem⌉) | `BankConfig::fresh_fit_min_obs`; `bank::kFitParameters`; `bank::Branch` | 8 a placeholder (heuristic); ½ k ln n form derived (BIC), k = 4 heuristic; the end at 4 N_mem derived (e⁻⁴ = 1.8%) |
| Bank corrections | a replacement at t changes nothing that starts before t − 20 s; overlap cut at max(from, t − 20 s); recorded only if the text differs, with its kept-character count | `BankConfig::correction_reach_s`; `bank::Output::replace_from` | 20 s owner; overlap cut heuristic |
| Bank switch replacement | from the start of the new branch's character containing the time its eligible run began (its own time base); a fallback pick from the switch's time | `bank::BankChannel` | heuristic (spec 4.8; the fallback rule documented behavior) |
| Bank anchor mixing (engine) | u[n] = y[n] exp(−jφ[n]), φ[n] = 2π (Σ_{m≤n} Δ[m] − Δ[n]) / r, Δ = detector frequency (oracle: the label's, without its drift) − channel center, Hz, per channel block; no tracker | `BankDecoder::process` | derived: the prototype's `anchored_baseband`, mixing at the frequency where option 1 (the detector decides where the station is) puts the station; the oracle anchor without drift is the plan's choice, not tuned |
| Bank events (engine) | new characters, then corrections (index: the smaller of the number kept and the first position whose text changed; the bank's characters from it on; time, reason, reach), applied in order, plus a "resync" correction if the consumer's list ever differs from the bank's (its reach not bounded by the 20 s correction reach; never on the suite, measured; tested by forcing one); final text = all corrections applied, immediate text = characters as first appended | `TextCorrection`, `DecodeUpdate::corrections`, `DecodedTextEvent::corrections`; bench `TrackText` | derived: the consumer's rule rebuilds the bank's final text exactly (proof in A.8c); not signal processing, an interface choice of the port (owner decision D1 for `--decoder`); the event's probability 1, speed 0 and confidence 0 are placeholders |

## A.11 Definitions used in tests and the benchmark

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
  Measured values: 2a record, sections 3.3 and 8.4 (Envelope and
  Matched; they vary by about a fifth between sessions on one machine);
  Plan A record, section 4.6, and the Plan B record (the bank). A
  Raspberry Pi 5 is not yet measured.
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
  change. **Provenance of the Matched limit 0.07 (heuristic):** the
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
  drift) itself.
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
