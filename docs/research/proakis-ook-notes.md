# Detection theory for on-off keying: check against Proakis & Salehi

Verification notes for the detection-theory statements in
[decoder-survey.md](decoder-survey.md), the underlying
[classical_literature.md](research_notes/CW%20decoder%20algorithms%20survey/classical_literature.md)
(section 4), and the envelope-detection parts of
[signal-processing.md](../signal-processing.md).

**Source:** J. G. Proakis and M. Salehi, *Digital Communications*, 5th ed.,
McGraw-Hill, 2008. Page numbers below are the **printed** book pages (the PDF
page number is the printed page + 19). Formulas are quoted briefly and
paraphrased; derivations are summarized, not reproduced.

**Finding in one line:** Proakis does not work out noncoherent on-off keying
(OOK) in the text. It appears only as an unsolved homework problem (Problem
4.36, p. 277; coherent on-off signaling is Problem 4.4, p. 267). Everything the
survey needs follows directly from the book's general results (the noncoherent
MAP (maximum a posteriori) rule, eq. 4.5–21, and the Rayleigh, Rician and
Marcum Q definitions in section 2.3), so the OOK formulas below are **derived
here from Proakis's equations**, and their numbers were computed with SciPy.

## 1. Symbols and conventions

| Symbol | Meaning | Units |
|---|---|---|
| T | element (dit) duration; PARIS timing gives T = 1.2 s / WPM | s |
| E | energy of the received carrier during one key-down element of duration T (key-on energy, bandpass) | J |
| E_avg | average energy per element with equiprobable on/off: E_avg = E/2 | J |
| N₀ | one-sided noise power spectral density; Proakis writes the two-sided density as N₀/2 | W/Hz |
| γ = E/N₀ | key-on energy-to-noise-density ratio per element. **This is what the survey calls E_s/N₀.** Quoted in dB as 10·log₁₀(E/N₀), i.e. dB relative to E/N₀ = 1 | dimensionless |
| S | key-on SNR: carrier power during key-down divided by noise power in bandwidth B | dimensionless |
| B | noise bandwidth, ∫\|H(f)\|² df / \|H(0)\|² over all f (bandpass Hz, same convention as signal-processing.md's 252 Hz) | Hz |
| R | envelope: magnitude of the matched-filter (correlator) output at the decision instant | amplitude units |
| σ² | noise variance per real (in-phase or quadrature) component of that output | amplitude² |
| s | noise-free signal amplitude of that output during key-down | amplitude units |
| a = s/σ | normalized signal amplitude; for a matched filter a = √(2E/N₀) | dimensionless |
| b | normalized decision threshold on R/σ | dimensionless |
| P₁, P₀ = 1 − P₁ | prior probabilities of key-down and key-up | dimensionless |
| I₀(x) | modified Bessel function of the first kind, order zero | dimensionless |
| Q(x) | Gaussian tail probability, ∫ₓ^∞ (2π)^(−1/2) e^(−t²/2) dt | dimensionless |
| Q₁(a, b) | Marcum Q function, ∫_b^∞ x·exp(−(x² + a²)/2)·I₀(ax) dx (Proakis eq. 2.3–37, p. 47) | dimensionless |
| P_b | probability of a wrong on/off decision for one element ("bit error") | dimensionless |

All error rates here are **per element, per hard decision**, in additive white
Gaussian noise (AWGN), with the carrier phase unknown but constant over the
element, and no fading.

## 2. Formulas a decoder designer needs

### 2.1 Envelope densities (Proakis §2.3, pp. 48–51; §4.5–3, pp. 216–217)

With independent Gaussian in-phase and quadrature noise of variance σ² each:

- **Key-up (noise only): Rayleigh.** p₀(r) = (r/σ²)·exp(−r²/(2σ²)), r > 0
  (eq. 2.3–43, p. 48). Mean σ·√(π/2), variance (2 − π/2)σ² (eq. 2.3–44).
- **Key-down (carrier amplitude s plus noise): Rician.**
  p₁(r) = (r/σ²)·I₀(rs/σ²)·exp(−(r² + s²)/(2σ²)), r > 0 (eq. 2.3–56, p. 50).
  It reduces to Rayleigh at s = 0 and approaches a Gaussian for large s/σ.
  Mean square 2σ² + s² (eq. 2.3–58). The Rice factor is K = s²/(2σ²)
  (eq. 2.3–60, p. 51).
- **Rician CDF via Marcum Q:** P(R ≤ r) = 1 − Q₁(s/σ, r/σ) (eq. 2.3–57, p. 50).
  Useful properties (eq. 2.3–39, p. 48): Q₁(0, x) = exp(−x²/2), and
  Q₁(a, b) ≈ Q(b − a) for b ≫ 1 and b ≫ b − a.
- For the matched filter (correlator) output of a carrier element of key-on
  energy E, Proakis's normalization gives s = 2E and σ² = 2E·N₀ (§4.5–3,
  p. 217), so **a = s/σ = √(2E/N₀)**, independent of the filter's gain.

### 2.2 Optimum noncoherent detector (Proakis §4.5–1, pp. 212–214)

Modeling the unknown carrier phase as uniform on [0, 2π) and averaging the
likelihood over it gives the MAP rule: choose the hypothesis m maximizing
P_m·exp(−E_m/(2N₀))·I₀(|r_l·s_ml|/(2N₀)) (eq. 4.5–21, p. 213), where r_l·s_ml
is the correlation of the complex baseband received signal with signal m.
The statistic depends on the received signal only through the **envelope of
the matched-filter output** (eqs. 4.5–23/24 and Fig. 4.5–1, p. 214). Proakis
notes that the same decisions can be implemented with an envelope or a
square-law detector (§4.5–2, p. 216).

For OOK (E₀ = 0, E₁ = E) this yields, in normalized variables x = R/σ:

- **Log-likelihood ratio** (soft output):
  Λ(x) = ln[p₁(x)/p₀(x)] = −a²/2 + ln I₀(a·x).
  Add ln(P₁/P₀) for the posterior log-odds. (Numerically, use
  ln I₀(z) = z + ln(i0e(z)) to avoid overflow.)
- **Decision:** key-down if Λ(x) > ln(P₀/P₁).

### 2.3 Optimum threshold vs SNR (derived)

Setting Λ(b) = ln(P₀/P₁): the threshold b solves
**I₀(a·b) = (P₀/P₁)·exp(a²/2)**. With equal priors this is exactly the point
where the Rayleigh and Rician densities cross. There is no closed form; a good
approximation (from I₀(z) ≈ e^z/√(2πz), eq. 2.3–33) is
**b ≈ √(2 + a²/4)**, i.e. b/a ≈ ½·√(1 + 4N₀/E). As SNR grows the threshold
falls toward half the key-down amplitude; at low SNR it sits well above it.

Exact optimum (equal priors), expressed as a fraction of the key-down
amplitude s:

| E/N₀ (dB re 1) | 5 | 7 | 10 | 12 | 14 | 16 | 20 |
|---|---|---|---|---|---|---|---|
| b/a, exact | 0.77 | 0.69 | 0.61 | 0.58 | 0.55 | 0.54 | 0.52 |
| b/a, approximation | 0.75 | 0.67 | 0.59 | 0.56 | 0.54 | 0.53 | 0.51 |

With P₁ = 0.44 (key-down less likely than key-up), the threshold rises
slightly: b/a = 0.62 at E/N₀ = 10 dB and 0.56 at 14 dB (dB re 1).

*Relation to the baseline decoder (derived, not in Proakis):* measured from
the Rayleigh mean (key-up level) toward the Rician mean (key-down level), the
optimum lies about **44–45%** of the way at E/N₀ = 10–14 dB. The baseline's
hysteresis thresholds (40% and 60%, signal-processing.md §8) straddle it. A
fixed 50% threshold costs little (P_b 0.030 instead of 0.027 at E/N₀ = 10 dB re 1);
60% costs more (0.047 at E/N₀ = 10 dB, and 4.4×10⁻³ instead of 5.1×10⁻⁴ at 14 dB, dB re 1).
This assumes the mark and space trackers sit at the true means, which the
fast-rise/slow-fall trackers only approximate.

### 2.4 Noncoherent OOK error probability (derived)

With threshold b and priors P₀, P₁:

**P_b = P₀·exp(−b²/2) + P₁·[1 − Q₁(a, b)]**, a = √(2E/N₀).

The first term is a false mark (Rayleigh tail above b); the second is a missed
mark (Rician CDF below b). High-SNR approximation with b ≈ a/2 and equal
priors (the false-mark term dominates):

**P_b ≈ ½·exp(−E/(4N₀)) = ½·exp(−E_avg/(2N₀))**.

This has the same form as noncoherent binary FSK (eq. 4.5–45, p. 218),
½·exp(−E_b/(2N₀)), when OOK is charged with its average energy.

### 2.5 Coherent OOK for comparison (Proakis §4.2–1, p. 175)

For any two equiprobable signals, P_b = Q(√(d²/(2N₀))) with d² the squared
Euclidean distance between them (eq. 4.2–37). For OOK, d² = E, so

**P_b,coh = Q(√(E/(2N₀))) = Q(√(E_avg/N₀))**,

with the threshold at half the key-down correlator output. This is 3 dB worse
than antipodal signaling at equal average energy (the comparison Problem 4.4,
p. 267, asks for).

### 2.6 Required E/N₀ (computed from 2.4 and 2.5)

E/N₀ in dB re 1, with E the **key-on** energy per element. Subtract 3.0 dB to
get average energy per element, E_avg/N₀.

| P_b per element | Coherent | Noncoherent, optimum threshold | Noncoherent, b = a/2 | ½·exp(−E/4N₀) approx. | Noncoherent penalty |
|---|---|---|---|---|---|
| 10⁻¹ | 5.2 | 7.2 | 8.4 | 8.1 | 2.0 dB |
| 3×10⁻² | 8.5 | 9.8 | 10.7 | 10.5 | 1.3 dB |
| 10⁻² | 10.3 | 11.4 | 12.0 | 11.9 | 1.0 dB |
| 10⁻³ | 12.8 | 13.5 | 14.0 | 14.0 | 0.7 dB |
| 10⁻⁴ | 14.4 | 15.0 | 15.4 | 15.3 | 0.6 dB |
| 10⁻⁵ | 15.6 | 16.1 | 16.4 | 16.4 | 0.5 dB |

The shrinking penalty at high SNR matches Proakis's general observation for
binary FSK and DPSK that noncoherent detection costs less than 0.8 dB below
about 10⁻⁴ (pp. 218, 224). At low SNR, where a CW decoder actually works, the
penalty grows to 1–2 dB, and the threshold choice matters by a further
0.5–1 dB.

### 2.7 Matched-filter bandwidth (Proakis §4.2–2, pp. 180–181)

The matched filter's magnitude response equals the signal's spectrum,
|H(f)| = |S(f)| (eq. 4.2–50), and it maximizes the output SNR at the sampling
instant, reaching 2E/N₀ (eq. 4.2–55; peak signal squared over noise variance,
for a real filter). For a rectangular element of duration T, |S(f)| ∝
|sinc(fT)|, so the noise bandwidth is **exactly 1/T** (null-to-null width
2/T). Proakis does not state "1/T" explicitly; it follows by integration, and
is consistent with his statement that noncoherently orthogonal tones need
spacing 1/T (p. 219).

**Frequency offset (derived from eq. 4.5–28, p. 215):** correlating a tone
offset by Δf (Hz) against a template of duration T reduces the amplitude by
|sinc(Δf·T)|, where sinc(x) = sin(πx)/(πx). The noncoherent detector is
insensitive to a constant phase, not to a frequency offset comparable to 1/T.

## 3. Claim-by-claim verification

"Survey" = decoder-survey.md; "Notes" = classical_literature.md §4;
"SP" = signal-processing.md.

| # | Claim (where) | Verdict | Correct statement and source |
|---|---|---|---|
| 1 | Envelope is Rayleigh with no signal, Rician with signal (Survey ¶ on Bell; Notes line 151) | **CONFIRMED** | §2.3, pp. 48–50 (eqs. 2.3–43, 2.3–56); §4.5–3, p. 217 (eqs. 4.5–35/36). See 2.1 above. |
| 2 | Optimum threshold is where the two densities cross (Notes 151) | **CONFIRMED, with qualifier** | True for equal priors (MAP and ML (maximum likelihood) rules, §4.1–1, pp. 162–163; noncoherent MAP rule eq. 4.5–21, p. 213). With unequal priors the prior-weighted densities cross instead. OOK threshold values are not in Proakis; derived in 2.3. |
| 3 | Detection probability is expressed with the Marcum Q function (Notes 151) | **CONFIRMED** | Rician CDF = 1 − Q₁(s/σ, r/σ), eq. 2.3–57, p. 50; Marcum Q defined eqs. 2.3–37/38, p. 47. |
| 4 | Filter *before* the envelope detector, with a filter matched to the element (Survey intro and Bell ¶; Notes 148) | **CONFIRMED** | The optimum noncoherent receiver correlates with (matched-filters) each signal and then takes the envelope: eqs. 4.5–23/24, Fig. 4.5–1, p. 214. |
| 5 | Use noncoherent detection unless the carrier phase can be tracked (Notes 148, 166) | **CONFIRMED** | Noncoherent detection is defined by a uniformly distributed unknown phase, §4.5–1, p. 212; coherent detection requires the receiver to know φ, p. 213. |
| 6 | Filtering before detection matters because a square-law/envelope detector at low input SNR suffers small-signal suppression from the noise × noise term (Survey Bell ¶; Notes 160) | **NOT IN PROAKIS** | The book does not analyze envelope-detector small-signal suppression. The nearest passage is the squaring loss of a squaring carrier-recovery loop: signal × noise and noise × noise terms add noise, with a loss of 3 dB when the loop SNR equals the bandpass-to-loop bandwidth ratio (§5.2–5, pp. 311–312). Proakis's own argument for pre-detection filtering is item 4: the matched filter must come before the envelope. Keep the mechanism labeled inference or cite a noncoherent-receiver text. |
| 7 | Matched-filter noise bandwidth ≈ 1/T (Notes 161–164; Survey Bell ¶; SP "Why ±150 Hz") | **CONFIRMED (by derivation)** | Exactly 1/T for a rectangular element, from eq. 4.2–50, p. 180. See 2.7. |
| 8 | E_s/N₀ = S·B·T; S₅₀₀ = 0 dB gives 13.8 / 11.8 / 17.0 dB at 25 / 40 / 12 WPM; +5 dB spot ≈ 19 dB (Survey "second yardstick" ¶) | **CONFIRMED** | Definitional, with N₀ one-sided (Proakis's two-sided N₀/2, eq. 4.2–52, p. 180) and S the key-on SNR in noise bandwidth B. Arithmetic checks: 13.8, 11.8, 17.0, 18.8 dB. |
| 9 | Coherent OOK needs roughly 11–13 dB E_s/N₀ for about 10⁻³ symbol error (Notes 165) | **CONFIRMED** | 12.8 dB key-on E/N₀ (9.8 dB average), from eq. 4.2–37 with d² = E, p. 175. |
| 10 | Noncoherent OOK needs slightly more than coherent (Notes 165) | **CONFIRMED** | +0.7 dB at 10⁻³ with the optimum threshold; +1 to +2 dB at 10⁻² to 10⁻¹. See table 2.6. |
| 11 | Noncoherent OOK needs something like 12–14 dB E_s/N₀ for about 10⁻³ (Survey "second yardstick" ¶) | **CONFIRMED, refine** | 13.5 dB key-on E/N₀ with the optimum threshold, 14.0 dB with a threshold at half the key-down amplitude. It is 3 dB lower (10.5 dB) if E_s is read as average energy, so the survey should say "key-on". |
| 12 | Matched dit filter at 20 WPM gains 10·log₁₀(2500/17) ≈ 21.7 dB, so −15 dB in 2.5 kHz ≈ +7 dB E_s/N₀ per dit (Notes 165) | **CONFIRMED** | Arithmetic: 1/T = 16.7 Hz, gain 21.8 dB, E/N₀ = 6.8 dB. At that level a per-dit hard decision errs about 11% of the time noncoherently (6% coherently), which supports the notes' inference that soft sequence decoding is needed. |
| 13 | Soft per-sample likelihood ratios from Rician-versus-Rayleigh densities (Survey option 1; Notes 169) | **CONFIRMED (form)** | The LLR −a²/2 + ln I₀(a·x) is exactly the OOK case of eq. 4.5–21, p. 213. Caveat (not in Proakis): the result applies to one matched-filter output per element. Samples of a narrow-filtered envelope at 1500 samples/s are strongly correlated, so summing per-sample LLRs overcounts the evidence; scale or decimate. |
| 14 | Square-law or envelope detection (\|·\|² or \|·\|) are interchangeable (Notes 168) | **CONFIRMED** | §4.5–2, p. 216: implemented with an envelope or a square-law detector. For a single threshold the two give identical decisions (the map is monotonic); the LLR must be written in the matching variable. |
| 15 | Envelope detection \|y[n]\| needs no carrier recovery, and residual frequency offset and phase don't matter (SP §7 "Residual frequency offset", §8 step 1) | **CONFIRMED for SP's current 252 Hz filter; CORRECTED for the proposed dit-matched pre-filter** | Phase: confirmed, p. 212. Frequency offset: a matched filter of width ~1/T loses |sinc(Δf·T)| in amplitude (eq. 4.5–28, p. 215). With the stated ±11.7 Hz bin-rounding offset this is up to 5.1 dB at 25 WPM (T = 48 ms) and 8.8 dB at 20 WPM, which erases half or more of the survey's predicted 10.8 dB gain from a 21 Hz pre-filter (Survey Bell ¶ and option 1). The survey should say that the station must be re-centered (fine frequency estimate) to within a small fraction of 1/T before the narrow filter. |

**Counts:** 13 confirmed (items 1–5, 7–14; items 2 and 11 with qualifiers),
1 corrected (item 15, frequency offset with the narrow pre-filter), 1 not in
Proakis (item 6, small-signal suppression mechanism).

## 4. Items outside Proakis that the survey should keep as inference

- The small-signal suppression mechanism (item 6).
- Bell's measured numbers and the "13 dB from a factor-of-20 bandwidth cut"
  arithmetic (10·log₁₀ 20 = 13.0 dB, correct as arithmetic).
- Any statement about fading: all numbers above are for a constant-amplitude
  carrier in AWGN. Proakis treats Rayleigh and Rician *fading* separately
  (Chapter 13 and Appendix C); fading changes the required SNR by tens of dB
  at low error rates and is not covered by these notes.
