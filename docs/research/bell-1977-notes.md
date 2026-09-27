# Bell (1977): implementation notes for a Bell-style Morse decoder

Source: Edison Lee Bell, *Optimal Bayesian Estimation of the State of a Probabilistically Mapped Memory-Conditional Markov Process with Application to Manual Morse Decoding*. Doctor of Engineering thesis, Naval Postgraduate School, Monterey, September 1977. Advisor: S. Jauregui. Public domain. Local copy: `C:\KZ4APSkimmer-papers\Bell1977-optimal-bayesian-estimation-morse.pdf` (Internet Archive scan, 404 PDF pages).

**Page citations.** "p. N" is the page number printed on the thesis page. In this scan, printed page N is PDF page 2N + 3: every other PDF page is a blank verso. So p. 128 is PDF page 259.

**Labels.** [fact, p. N] means Bell states it on that page, checked against the page image where math, tables or figures are involved. [fact, code p. N] means it comes from his Fortran listing (pp. 159–191). [inference] means reasoning by this note, not by Bell. [uncertain] means the scan, the OCR or a hand-drawn graph could not be read precisely.

## 0. Bottom line

- Bell's decoder is a **multiple-hypothesis tracker**. The discrete state is (letter/trie state, Morse element type, key state, time spent in the current element, speed). Each hypothesis carries its own **scalar Kalman filter on the received signal amplitude**, and the Kalman innovation gives that hypothesis's measurement likelihood. Paths are extended every 5 ms sample and pruned M-path style. At most 25 paths are kept, and each extends into at most 30 successors [fact, pp. 119–123; code pp. 167–181].
- The measurement is **not I/Q**. It is the output of a real-valued envelope detector behind a 100 Hz band-pass filter, sampled at 200 samples/s. The mean of the noise floor is subtracted, and the result is then treated as if it were zero-mean white Gaussian noise [fact, pp. 110–114, 123–124].
- The only text model actually coded and tested is **independent, equally likely letters** [fact, p. 106]. Bell says the biggest remaining gain lies in better text (language) models [fact, pp. 106–108, 146].
- Headline results. Simulated hand-keyed Morse with a "fair" fist (10% of letters mis-sent) at 20 wpm gives 4% letter error at SNR 9 dB in 100 Hz. The error rises to 8% at 4 dB and 34% at 3 dB (same bandwidth) [fact, Table XVI, p. 134]. On real off-air recordings at 16–22 dB SNR in 100 Hz, letter error was 1–10% (keyboard senders) and 3–15% (hand senders) [fact, Tables XXI–XXII, pp. 141–142]. None of Bell's results, and none of his lower bounds, gets anywhere near 10⁻⁵ letter error.

## 1. Symbols and units

| Symbol | Meaning | Units |
|---|---|---|
| k | sample index | — |
| τ | sample interval; Bell uses 5 ms | s |
| x_k | key state: 1 = key down (mark), 0 = key up | — |
| a_k | Morse element (symbol) type: dot, dash, element space (e-sp), character space (c-sp), word space (w-sp), pause | — |
| ℓ_k | text letter being sent (26 letters + 10 digits in the analysis) | — |
| u_k | "control vector". In the coded model, only the instantaneous speed r_k | wpm |
| r_k | instantaneous speed | words per minute (wpm) |
| Δ | instantaneous element (baud) duration, Δ = 1.2/r (PARIS: 50 elements per word) | s |
| m | nominal length of an element in bauds: 1 (dot, e-sp), 3 (dash, c-sp), 7 (w-sp), 14 (pause) | bauds |
| φ_k | number of samples since the last key-state change | samples |
| D | normalized time spent in the current element, D = φ_k·τ/Δ | bauds |
| ε | normalized sample interval, ε = τ/Δ | bauds |
| β_k, α_k, λ_k | memory states of the key, of the encoder (element) and of the source (letter) | — |
| s_k | discrete "output" state, s_k = [x_k, a_k, ℓ_k] | — |
| σ_k | total memory state (path label), σ_k = f_σ(s_k, σ_{k−1}) | — |
| y_k | "apparent transmitted amplitude", the Kalman state (scalar) | normalized amplitude (AGC-leveled, about 1) |
| z_k | measurement: envelope-detector output with the noise-floor mean removed | same units as y_k |
| n_k | measurement noise, variance R_k | (amplitude)² |
| Q_w | amplitude process-noise variance per step | (amplitude)² |
| S₁₀₀ | Bell's SNR: key-down carrier power over noise power in a 100 Hz bandwidth | dB |
| E/N₀ ("E_b/N₀" in Bell's figures) | energy of **one key-down element (dot)** divided by the one-sided noise spectral density. This is **not** energy per information bit | dB |
| S₅₀₀ | project convention: key-down power over noise in 500 Hz | dB |
| P_opt | pruning threshold: keep paths until their summed posterior probability reaches P_opt | — |

**Converting Bell's SNRs.** Table IV (p. 35) lists S₁₀₀ next to E/N₀. At 20, 30 and 50 wpm the E/N₀ entries equal S₁₀₀ + 7.8, +6.0 and +3.8 dB. These offsets are exactly 10·log₁₀(100 Hz × Δ) with Δ = 60, 40 and 24 ms. So Bell's S₁₀₀ is a **key-down** SNR, and his "E_b" is **energy per dot** [fact for the table values, p. 35; inference for the interpretation]. For white noise, S₅₀₀ = S₁₀₀ − 7.0 dB, and the SNR in 2500 Hz is S₁₀₀ − 14.0 dB [inference].

## 2. Signal model: the "probabilistically mapped memory-conditional Markov process"

### 2.1 General structure (Chapter IV, pp. 45–65)

- Bell models the sender as a **sliding block coder with unbounded memory** followed by a probabilistic mapping. Letters are encoded into Morse elements, and the key turns elements into 0/1 levels whose durations are random [fact, pp. 45–47].
- **State variables** [fact, pp. 47–49]:
  - key state x_k ∈ {0, 1};
  - element a_k ∈ {6 types};
  - letter ℓ_k ∈ {L_1, …, L_N};
  - three finite-state memory functions: β_k = f_β(x_k, β_{k−1}) (key memory), α_k = f_α(a_k, α_{k−1}) (encoder memory), λ_k = f_λ(ℓ_k, λ_{k−1}) (source memory);
  - a random control vector u_k, which selects the code sequence, the instantaneous rate and the average speed.

  Bell's example of this memory: f_β counts samples since the last key transition, f_α counts elements since the last letter change, and f_λ records the previous letter [fact, pp. 48–49].
- **Transition factorization, eq. (9a)** [fact, p. 53]:
  p(s_k, u_k | u_{k−1}, σ_{k−1}) = p(x_k | a_k, u_k, β_{k−1}) · p(a_k | ℓ_k, u_k, α_{k−1}, β_{k−1}) · p(ℓ_k | λ_{k−1}, α_{k−1}) · p(u_k | u_{k−1}, α_{k−1}, β_{k−1}, λ_{k−1}).
  The memory updates are deterministic, eq. (9b): each is 1 if the new memory equals f(·) and 0 otherwise [fact, pp. 52–53].
- **Theorem and corollary** (pp. 56–59). u_k is a *conditional Markov* process, meaning Markov given the memory σ_{k−1}. The output state s_k is a *probabilistic mapping* of u_k, conditioned on the whole past through σ. That is where the title comes from. If f_σ is invertible, the output is a sliding-block encoding of {s_k}, so the optimal estimator has trellis structure [fact, pp. 56–60].
- Operator mistakes (insertions, deletions, substitutions of elements, especially extra dots and w-sp/c-sp confusion) are meant to live in f_α and u_k [fact, pp. 46–47, 54]. **None of these was implemented.** u_k was reduced to speed only [fact, p. 101].

### 2.2 The coded (practical) model (Chapter VI, pp. 86–108)

**Key-state transitions depend on elapsed duration and speed** [fact, pp. 87–100]:

- A plain two-state Markov chain is rejected. Its probabilities depend on the sample rate (Tables VIII–IX, pp. 88–89), so time is normalized to bauds instead [fact, pp. 89–91].
- The duration counter is φ_k = φ_{k−1}(1 − x_k − x_{k−1} + 2x_k·x_{k−1}) + 1. It counts samples in the current state and resets on a 0↔1 change. The normalized time is T′_k = 5·φ_k·u_k·τ/6 bauds (u_k in wpm, τ in s) [fact, p. 90].
- Dependence on the *previous* element, for example the lower variance of an e-sp after a dot on a bug, is acknowledged and deliberately ignored as a second-order effect [fact, pp. 91–92].
- **Duration density.** Given the speed r, the normalized duration of an element of nominal length m has a Laplacian (two-sided exponential) density with mode at m [fact, pp. 96–97]:
  p(mφ_Δ | r) = c·exp(−α·|mφ_Δ − m|), with c = α/2.
  Here α is chosen so that the crossover probability at the midpoint threshold is 1.35%: Pr[dot ≥ 2 bauds] = Pr[dash ≤ 2 bauds] = ½·e^{−α} = 0.0135. The value 0.0135 is the mean error with optimal thresholds over 55 hand-keyed samples in a study by Technology Services Corporation (TSC) [fact, pp. 97–98].
  - Dot, dash, e-sp, c-sp: α = 3.61 per baud, c = 1.81.
  - W-sp: α = 1.81, c = 0.90.
  - Pause: α = 0.90, c = 0.45 [fact, p. 98].
  - These values imply decision thresholds 2, 2 and 4 bauds from the mode: 2 (dot/dash), 5 (c-sp/w-sp) and 10 (w-sp/pause) bauds [inference].
- **The code uses different constants.** XTRANS uses α = m × {3.0, 1.5, 1.0} applied to duration normalized by m·Δ. That is 3.0 per baud for m = 1 and 3 elements, 1.5 per baud for w-sp, and 1.0 per baud for pause, not 3.61/1.81/0.90 [fact, code p. 170].
- **Transition probability** (hazard form) [fact, p. 100]. With D₀ the elapsed normalized duration:
  Pr[stay] = Pr[φ ≥ D₀ + ε | φ ≥ D₀] =
  - e^{−αε} if D₀ ≥ m;
  - (1 − ½e^{α(D₀+ε−m)}) / (½e^{−α(D₀−m)}) if D₀ ≤ m ≤ D₀ + ε;
  - (1 − ½e^{α(D₀+ε−m)}) / (1 − ½e^{α(D₀−m)}) if D₀ + ε ≤ m.

  Pr[change] = 1 − Pr[stay] [fact, pp. 100–101]. The code implements exactly this, with durations kept in ms and incremented by 5 ms per sample [fact, code pp. 170, 174].

  **Note.** This is an explicit-duration (semi-Markov) model. The survival probability past the mode decays with a constant hazard α·ε, so a long element never becomes "impossible" [inference].

**Speed model (Section VI.B, pp. 101–104)**:

- Speed is the only component of u_k that is used [fact, p. 101].
- Speeds are discrete integers, **10 ≤ r ≤ 60 wpm** [fact, p. 102]. The code zeroes any transition that would leave 10–60 [fact, code p. 172].
- **Speed changes only at element boundaries.** In the text this is "only when the key state changes" [fact, pp. 102–103]. In the code, no speed change happens unless the element type changes [fact, code p. 172].
- The increment depends on the element just completed, from Table X (p. 104) [fact]:

  | Element just completed | Speed increments (wpm) | Probabilities | Mean \|Δr\| (wpm) |
  |---|---|---|---|
  | dot, dash, e-sp | −4, −2, 0, +2, +4 | .1, .2, .4, .2, .1 | 1.6 |
  | c-sp | −4, −2, 0, +2, +4 | .15, .2, .3, .2, .15 | 2.0 |
  | w-sp | −10, −5, 0, +5, +10 | .1, .2, .4, .2, .1 | 4.0 |
  | pause | −20, −10, 0, +10, +20 | .15, .2, .3, .2, .15 | 10.0 |

  The "0" column is lost in the OCR of Table X. It is restored here from the averages and from the ±2i rule on p. 103 [inference; consistent with the code's 5 rate branches (I−3)·IDEL, code p. 174].
- The justification is TSC's data: hand-sent speed differs by about 2.5 wpm on average between 10-mark/space-pair segments, and 8–10 wpm differences are not rare [fact, pp. 101–102].
- Initialization: the 25 starting paths are 5 each at 10, 20, 30, 40 and 50 wpm [fact, code p. 167].
- Output speed estimate: the conditional mean over saved paths [fact, p. 121; code p. 180].

**Element and letter models (pp. 104–108)**:

- Tables XI and XII (p. 105) give first- and second-order Markov element transition matrices. The first-order matrix has rows dot → (e-sp .58, c-sp .33, w-sp .07, pause .02), dash → (.54, .37, .07, .02), and any space → (dot .5–.55, dash .45–.5) [fact, p. 105]. Several printed rows of Table XII do not sum to 1, for example "−^ : .55, .5" [fact, p. 105; probably typos].
- In the "independent letters" model, the element sequence is fixed by the letter (probability 0 or 1). A letter transition is allowed only when α_{k−1} shows a letter-ending space. Letters are then uniform [fact, pp. 104–106].
- The code carries a letter state LAMBDA with tables of up to 400 entries (IELMST, MEMFCN) and a 16×6 element-transition table ELEMTR. From these dimensions, LAMBDA appears to be a node in the Morse code tree [uncertain: code pp. 169–171 only partly read].
- Bell notes that a third-order letter model would need 36⁴ = 1,679,616 words of storage and recommends language/grammar/dictionary models instead [fact, pp. 107–108].

## 3. Channel, observation model and Kalman filters

### 3.1 Model (Sections IV.B, VII; pp. 60–65, 109–118)

- Physical chain: the key switches the transmitter on and off (on-off keying, OOK). HF adds additive noise n and multiplicative fading b, and the receiver applies AGC and a low-pass filter. Sampled baseband: z_k = x_k·c_k·b_k + n_k [fact, p. 60].
- The apparent amplitude y_k = c_k·b_k is modeled as a **conditional Gauss–Markov process whose parameters depend on the discrete state** [fact, pp. 63–64]:
  - y_k = γ·F(s_k, σ_{k−1})·y_{k−1} + Γ(s_k, σ_{k−1})·w_k, with w_k ~ N(0, 1);
  - z_k = x_k·y_k + n_k, so **the amplitude is observed only during marks**, with H(s) = x_k [fact, pp. 64, 109, 115];
  - n_k is white, zero-mean Gaussian with known variance R_k [fact, p. 109];
  - vector generalization: y_k = Φ(s_k, σ_{k−1})·y_{k−1} + Γ·w_k and z_k = H(s_k)·y_k + n_k [fact, pp. 64–65].
- **Parameter values in the text** (Section VII.C–D, pp. 115–118):
  - Fading at a 1 Hz rate with τ = 5 ms: γ = exp(−2π·1 Hz·0.005 s) = 0.97, and Q_w = Var(v) = (2/200)² = 10⁻⁴. The second figure assumes about 3 dB of amplitude change in 1 s after AGC [fact, pp. 115–116].
  - Per-state (Φ, Q_w) pairs [fact, pp. 116–117]:
    - (a) previous element a mark: Φ = 0.97, Q_w = 10⁻⁴;
    - (b) previous element an e-sp, still key-up: Φ = 1, Q_w = 0;
    - (c) e-sp → mark: Φ = 1, Q_w = 0.01. The rationale: 0.97⁴ ≈ 0.89 at 50 wpm, and (1 − 0.89)² ≈ 0.01;
    - (d) other space, still key-up: Φ = 0.98, Q_w = 0 (the amplitude estimate slowly decays);
    - (e) other space → mark: Φ = 1, **Q_w = 0.25**. This lets a new transmitter power appear after a c-sp, w-sp or pause, as when a new net station signs on.
- **Values in the code** (MODEL, code p. 178) differ:
  - Marks: Φ = 1 (not 0.97), Q_A = 10⁻⁴.
  - Entering a mark from a space: Φ = 1 and Q_A = 0.15·exp(0.6(B − 14)) + 0.01·B·exp(0.2(1 − B)), where B is the space length in bauds, capped at 14. That is a smooth version of rules (c) and (e).
  - Key-up: Φ = 10^(−2/(22.4·Δ_ms)), with Δ_ms = 1200/r, and Q_A = 0.

  [fact, code p. 178]
- Kalman initialization: ŷ = 0.5 and P = 0.1 for every path. ŷ is floored at 0.01 after each update [fact, code pp. 176–177].

### 3.2 What each filter estimates and how the likelihood is formed (Section V, pp. 66–85)

- **One scalar Kalman filter per path extension**, estimating the **amplitude y only**. There is no phase or frequency state, because the input is already envelope-detected [fact, pp. 76, 109–118; code pp. 176–177].
- The Bayes recursion, eq. (26) [fact, p. 73]:
  p(s_k^i, u_k^j, σ_k^ℓ | z^k) ∝ Σ_{n,m,q} p(s_k^i, u_k^j, σ_k^ℓ | s_{k−1}^n, u_{k−1}^m, σ_{k−1}^q) · p(s_{k−1}^n, u_{k−1}^m, σ_{k−1}^q | z^{k−1}) · L_k^{iq},
  normalized over all (i, j, ℓ). The transition term factors as p(s|u, u_{k−1}, σ_{k−1})·p(u|u_{k−1}, σ_{k−1}) [fact, p. 73].
- **Likelihood, eqs. (25) and (29)** [fact, pp. 73, 76–77]:
  L_k^{iq} = ∫ p(z_k | y_k, s^i) · p(y_k | s^i, σ^q; z^{k−1}) dy = c·|V_z|^{−1/2}·exp(−½ z̃ᵀ V_z⁻¹ z̃).
  Here z̃ = z_k − H(S_i)·ŷ_{k|k−1}(Λ_ℓ) is the innovation and V_z = H·V_{k|k−1}·Hᵀ + R_k is its variance. Predict: ŷ_{k|k−1} = Φ(S_i, Λ_q)·ŷ_{k−1|k−1}(Λ_q) and V_{k|k−1} = Φ·V·Φᵀ + Q(S_i, Λ_q). Update with the standard gain G = V_{k|k−1}Hᵀ[HV_{k|k−1}Hᵀ + R]⁻¹ [fact, p. 76].
- In the code: LKHD = PZ^{−1/2}·exp(−ZR²/(2·PZ)), set to 0 if the exponent exceeds 1000. Extensions whose transition probability is ≤ 10⁻⁴ are skipped, with likelihood set to 0 [fact, code pp. 176–177].
- Because the Kalman state is conditioned on the path label σ, each path's amplitude density stays a single Gaussian. The exact filter is a tree of Kalman filters whose number of nodes grows as N^k [fact, pp. 75–78]. Bell places this within prior work: switching-parameter (Markov) models [10, 11] and Lainiotis's partitioned filter for a fixed unknown parameter [9] are both special cases [fact, pp. 82–85].
- **Baseband versus pre-detection.** Bell's observation is **post-detection**, a real envelope. His front end [fact, pp. 111–114, 123–124]:
  1. 8 kHz sampling; the carrier is translated to 1000 Hz;
  2. a 500 Hz single-pole band-pass filter;
  3. a **100 Hz band-pass filter**: two cascaded single-tuned resonators, with noise bandwidth 1.22× the 3 dB bandwidth;
  4. an envelope detector: |x| followed by a 3-pole Chebyshev low-pass filter at 100 Hz (code pp. 189–190);
  5. sampling at **200 samples/s** (τ = 5 ms).

  At τ ≥ 1/(2·B_BPF) the noise samples are independent [fact, pp. 110–112]. Bell notes that the optimal filter for 50 wpm would be 25 Hz wide (0.613/0.024 s, 0.56 dB worse than matched), and 10 Hz for 20 wpm. So the fixed 100 Hz filter costs **6 dB at 50 wpm and 10 dB at 20 wpm**, relative to a matched filter. He chose 100 Hz to tolerate chirp of about 50 Hz [fact, pp. 111–112].
- **Noise handling** [fact, pp. 110–114, 124]:
  - The envelope noise is Rayleigh (Rician with signal present), not Gaussian, not zero-mean, and correlated with the signal. Bell keeps only the first two moments.
  - Mean and variance: μ_n = E[Z] and R = 2μ_n² (Bell's statement from Davenport).
  - NOISE subroutine: at each sample, take the minimum of the envelope over 240 samples (1.2 s), average it and scale it by an empirical factor to estimate μ_n. Subtract μ_n and pass R = 2μ_n² to the filters [fact, p. 124]. The listing uses buffers of 200 and 50 samples, so the implemented windows may differ [uncertain, code p. 190].
  - Sensitivity to a wrong R is small (Table XIII, p. 130): at 20 wpm keyboard Morse with true S₁₀₀ = 3 dB, using an assumed S₁₀₀ of 9, 6, 3 or 1 dB gave 9, 6, 5 and 5% letter error [fact, p. 130].

## 4. Trellis/tree decoder (Chapters V.B–C and VIII)

- **Structure.** A tree whose nodes are joint states (key state, element state, letter state, speed) and whose path labels come from f_σ. With finite memory the tree becomes a trellis suitable for Viterbi decoding. Bell's implementation is "most like" Haccoun's M-path algorithm, with path metric = likelihood × transition probability × previous path probability [fact, pp. 78, 119–120]. Saving only one path reduces it to the decision-directed linear filter of reference [2] [fact, p. 120].
- **Per-sample loop**, subroutine PROCES, called every 5 ms [fact, pp. 120–123; code pp. 167–168]:
  1. TRPROB: for each saved path, the transition probability to each of **30 successor states = 6 element types × 5 speed increments**. P(n) = (1 − P_stay)·P_element·P_rate for an element change, or P_stay if the element is unchanged (rate branch "0" only). Normalized over the 30 successors [code pp. 169, 171].
  2. PATH: label each new node with its duration, letter state and rate. The duration is reset to 5 ms on an element change and incremented by 5 ms otherwise [code p. 174].
  3. LIKHD/KALFIL: a Kalman step and likelihood for each extension [code pp. 174–177].
  4. PROBP: posterior of each new path, eq. (26), normalized [p. 121; code p. 178].
  5. SPROB: marginal posteriors of the key state and of the 6 element states, and the conditional-mean speed. The key-state MAP estimate (P_x > 0.5) is the "demodulated signal" and the element MAP estimate is a zero-delay decode [p. 121; code pp. 168, 180].
  6. SAVEP (pruning) [p. 122; code pp. 180–181]:
     - first keep the single best node for each of the 6 element states, if its probability is ≥ about 10⁻⁶ [uncertain: constant read as 0.000001];
     - then add nodes in decreasing probability until the saved total reaches **P_opt = 0.90**, with at most **25 paths** in all;
     - the code comment says the minimum is 7 paths;
     - paths are re-sorted and a back-pointer array is kept.
  7. TRELIS: **decoding delay.** Find a node, at sufficient delay, that all surviving paths share. If none exists within **200 samples (1 s)**, output the node at 200-sample delay on the current best path, and delete every path that does not descend from it [fact, p. 123]. Translation is a table lookup from elements to letters [fact, p. 123].
- **Printed algorithm gap.** Section V.C lists Steps 1–4 and then jumps to Step 8. Steps 5–7 are missing from the printed thesis. The printed pages 81 and 82 are consecutive, so this is not a scan gap [fact, pp. 80–82]. The Fortran fills the gap.
- **No merging.** Paths reaching the same (element, speed, letter, duration) state are *not* merged. The decoder is a pruned tree, not a Viterbi trellis [inference from pp. 119–123 and the SAVEP code].
- **Choosing P_opt and the delay** (Table XIV, p. 131; 50 wpm keyboard Morse, first-order element model):

  | P_opt | Avg paths saved | Letter error at delay 0 / 40 / 200 samples, S₁₀₀ = 9 dB | Same, S₁₀₀ = 6 dB |
  |---|---|---|---|
  | 0.98 | 20 | 9 / 5 / 5 % | 68 / 45 / 45 % |
  | 0.95 | 17 | 9 / 5 / 5 % | 68 / 45 / 45 % |
  | 0.90 | 15 | 12 / 8 / 5 % | 56 / 52 / 46 % |
  | 0.85 | 12 | 32 / 32 / 29 % | 58 / 56 / 53 % |
  | 0.80 | 8 | 38 / 39 / 36 % | 68 / 67 / 63 % |

  At S₁₀₀ = 12 dB all settings gave 0–3%. Bell chose P_opt = 0.9 with a 200-sample delay for all later tests [fact, pp. 131–132]. The average number of saved paths was 8–16 in all later tables [fact, Tables XV–XVI].
- **Computational cost.** On a PDP-10, 1 minute of signal took about 20 minutes to process [fact, p. 140]. The decoder ran about 90 hours (about 21,000 characters) without divergence or instability [fact, p. 146]. Bell calls the burden "severe" and suggests pipelined hardware [fact, p. 146].
- **Per-sample operation count** [inference]: at most 25 × 30 = 750 extensions per sample. Many are skipped, because an unchanged element allows only the "0" speed branch and extensions with P ≤ 10⁻⁴ are dropped. With about 15 paths saved on average, roughly 15 × (1 + about 3 allowed element changes × 5 speeds) ≈ 250 scalar Kalman steps per sample. Each step costs about 20 floating-point operations plus one exp, one sqrt and one to three exp calls for transitions. That is about 10⁴ flops per sample, or about 2 Mflop/s at 200 samples/s. The worst case is about 8 Mflop/s. A sort of up to 750 candidates per sample comes on top.

## 5. Parameters used

| Item | Value | Source |
|---|---|---|
| Simulation sample rate | 8 kHz; carrier translated to 1000 Hz | [fact, pp. 123, 125] |
| Pre-filter | 500 Hz single-pole BPF, then 100 Hz BPF (two resonators) | [fact, pp. 123–124] |
| Detector and post-filter | envelope detector; 100 Hz 3-pole Chebyshev LPF | [fact, p. 124] |
| Decoder rate | 200 samples/s (τ = 5 ms) | [fact, pp. 112, 120] |
| Speed range | 10–60 wpm, integer | [fact, p. 102; code p. 172] |
| Duration density | Laplacian: α = 3.61/1.81/0.90 per baud in the text; 3.0/1.5/1.0 in the code | [fact, p. 98; code p. 170] |
| Amplitude model | see §3.1; Q_w = 10⁻⁴ on marks, up to 0.25 after long spaces | [fact, pp. 115–118] |
| Kalman initial state | ŷ = 0.5, P = 0.1 | [fact, code p. 176] |
| Pruning | P_opt = 0.9; 25 paths max; about 7 min; best node per element kept | [fact, pp. 122, 131–132; code p. 181] |
| Decision delay | 200 samples = 1 s | [fact, pp. 123, 132] |
| Simulated fists | truncated Gaussian durations, 16–360 ms. Variance set so element crossover probability P_es = .00143 (good), .0149 (fair), .0403 (poor) | [fact, pp. 33, 125, 134] |
| Simulated channel | fading model of §VII.C plus white Gaussian noise | [fact, p. 125] |
| Test speeds | 20, 30 and 50 wpm; 15 wpm and 10–20 wpm in Howe's files; 16–35 wpm field | [fact, pp. 131–142] |
| Test size | about 200 letters per experiment | [fact, p. 139] |
| Field recordings | 4 kHz IF; fast-attack AGC with about 200 ms decay; digitized at 8 kHz; about 50 s per cut | [fact, p. 141] |

## 6. Results, with SNR definitions

All letter error rates are percentages of about 200 letters of random text unless noted. The 90% confidence interval is roughly 7–14% at a measured 10% and 20–31% at 25% [fact, Table XX, p. 139]. A reported "0%" means fewer than about 1 error in 200.

### 6.1 Entropy (Section III.A, pp. 24–30)

- Independent Morse elements (mark and space types): 1.927 bits/element, 1.09 bits per channel bit [fact, p. 26].
- First-order Markov elements: 0.938 bits/element. Second-order: 0.858 [fact, pp. 27–28].
- **Equally likely independent characters from a 36-symbol alphabet** (26 letters + 10 digits): H = −log₂(1/36) = **5.17 bits/letter**, which is 0.711 bits per element at 7.27 elements per letter [fact, p. 28].
- English, citing Shannon (ref. [5]) [fact, p. 29]:
  - equiprobable letters: 4.76 bits/letter;
  - with single-letter frequencies: 4.03;
  - first-order: 3.32;
  - second-order: 3.1;
  - **a model producing equally likely words: 2.14 bits/letter**, or 0.294 bits per element.
- Table III (p. 30) collects these values per element and per channel bit.

### 6.2 Lower bounds (Section III.B–C, pp. 31–44)

- **Idealized channel.** Element-synchronous matched filter, discrete crossover probabilities, and a binary symmetric channel with ε_eq = ε + δ − 2εδ. Here ε is the demodulation error and δ the keying error [fact, pp. 31–34].
- Keying quality is defined by E_s, the fraction of letters with at least one wrong element: good 1%, fair 10%, poor 25%. These correspond to element crossover P_es = .00143/.0149/.0403 and channel-bit δ = .000837/.00874/.0237 [fact, p. 33].
- **Table IV** (p. 35), envelope detection, element detection error 1 − P_d. This is **not** a letter error rate:

  | wpm | S₁₀₀ (dB) | E/N₀ (dB) | 1 − P_d | Capacity (bits per channel bit) |
  |---|---|---|---|---|
  | 50 | 12 / 9 / 6 / 3 / 0 | 15.8 / 12.8 / 9.8 / 6.8 / 3.8 | 2×10⁻⁵ / 2.5×10⁻³ / 2.7×10⁻² / 0.11 / 0.23 | ≈1.0 / .975 / .821 / .500 / .222 |
  | 30 | 12 / 9 / 6 / 3 / 0 | 18 / 15 / 12 / 9 / 6 | <10⁻⁵ / 1.3×10⁻⁴ / 6×10⁻³ / 4.5×10⁻² / 0.13 | ≈1.0 / .998 / .947 / .735 / .443 |
  | 20 | 12 / 9 / 6 / 3 / 0 | 19.8 / 16.8 / 13.8 / 10.8 / 7.7 | <10⁻⁵ / <10⁻⁵ / 7×10⁻⁴ / 1.6×10⁻² / 8×10⁻² | ≈1.0 / ≈1.0 / .992 / .882 / .598 |

- **Straight-line (sphere-packing-type) bound** on the equivalent block codes of Table VII (p. 41):
  - symbol pairs (13, 4);
  - triplets (33, 6);
  - single letters (395, 12) exact, or (1344, 14) as a bound;
  - letter pairs (139,264, 28);
  - 3-letter words (22,020,096, 42).

  The block codes are made non-synchronous by allowing all shifts [fact, pp. 34–41].
- Bound curves, all read from graphs with an E/N₀ axis of 7–14 dB and letter error 0.1–30% [uncertain: read by eye]:
  - Fig. 5, coherent, keyboard Morse (all values E/N₀ per dot): triplets ≈ 1% at 13.1 dB; independent letters ≈ 0.5% at 12.4 dB and ≈ 0.3% at 13 dB.
  - Fig. 6, envelope, keyboard Morse: independent letters ≈ 1.3% at 12.2 dB and ≈ 0.25% at 13.7 dB.
  - Fig. 7, envelope, hand-keyed, random letters: at 13.7 dB, good ≈ 0.5%, fair ≈ 3% and poor ≈ 8%. The fair and poor curves flatten at those floors.
  - [fact for the curves, pp. 42–44]

### 6.3 Idealized decoder (Section IX.A, pp. 126–130)

- **Idealized synchronous keyboard Morse** (Fig. 14, p. 128). Branching is allowed only at true element boundaries. The input is true baseband with white Gaussian noise and no fading. At E/N₀ = 12 dB (S₁₀₀ = 4 dB at 20 wpm, 8 dB at 50 wpm), approximate letter errors are:
  - linear matched filter with threshold decoding: ≈ 5%;
  - second-order Markov tree decoder: ≈ 3.5%;
  - independent-letter tree decoder: ≈ 2%;
  - lower bound: ≈ 0.7%.

  At E/N₀ = 9 dB the four curves read ≈ 25%, 22%, 12% and 7% [fact for the curves; values uncertain, read by eye]. Bell's summary is that the bound is "very nearly obtainable" and that the tree decoder beats the matched filter [fact, p. 127].
- **Idealized non-synchronous** (Fig. 15, p. 129; 20 wpm; E/N₀ 10–14 dB, S₁₀₀ 2–6 dB):
  - at E/N₀ = 12 dB: first-order ≈ 10%, second-order ≈ 7%, independent letters ≈ 5%, bound ≈ 0.9%;
  - at E/N₀ = 14 dB: ≈ 3.5%, ≈ 2.5% and ≈ 1.0%.

  [uncertain: read by eye]

### 6.4 Realistic decoder, envelope detection with fading model (Section IX.B, pp. 130–138)

- **Table XV** (p. 132), keyboard Morse, P_opt = 0.9, 1 s delay. Letter error for the first-order / second-order / independent-letter models:
  - 50 wpm, S₁₀₀ = 12 / 9 / 8 / 7 / 6 dB: 0/0/0, 5/4/3, 14/11/5, 36/30/16 and 46/41/35%;
  - 20 wpm, S₁₀₀ = 9 / 6 / 4 / 3 dB: 0/0/0, 10/6/3, 12/9/6 and 43/38/31%.

  [fact, p. 132]
- Fig. 16 (p. 133), 20 wpm keyboard Morse, independent letters, the realistic (envelope) tree decoder:
  - realistic: ≈ 30% at S₁₀₀ = 3 dB, 6% at 4 dB, 3% at 6 dB;
  - idealized curve (matches Fig. 15's non-synchronous case, [inference]): ≈ 4.5% at 4 dB, 1% at 6 dB;
  - envelope lower bound: ≈ 2% at 4 dB, 0.2% at 6 dB.

  Bell concludes that the loss from non-Gaussian envelope noise is moderate above S₁₀₀ ≈ 4 dB [fact, pp. 133, 145].
- **Table XVI** (p. 134), simulated hand-keyed Morse, P_opt = 0.9, 1 s delay, independent letters. Values are letter error % (average paths saved) at S₁₀₀ = 9 / 6 / 4 / 3 dB:

  | Fist | 30 wpm | 20 wpm |
  |---|---|---|
  | Good (E_s = 1%) | 3 (8), 5 (8), 36 (9), – (9) | 1 (9), 4 (10), 6 (10), 31 (11) |
  | Fair (E_s = 10%) | 5 (9), 7 (10), 42 (10), – (11) | 4 (10), 6 (10), 8 (11), 34 (11) |
  | Poor (E_s = 25%) | 12 (11), 13 (11), 46 (12), – (12) | 11 (12), 13 (13), 14 (13), 38 (14) |

  In S₅₀₀ terms, the 20 wpm fair fist gives 4% at +2 dB, 6% at −1 dB, 8% at −3 dB and 34% at −4 dB [inference: S₅₀₀ = S₁₀₀ − 7 dB].
- Fig. 17 (p. 135), 20 wpm. The hand-keyed fair curve lies about on top of the human-operator points for independent letters at S₁₀₀ ≈ 4–6 dB (≈ 10% and 6%). The keyboard curve is lower, and the envelope bound is far lower [fact for the figure; values uncertain, read by eye]. Bell's summary: under lab conditions the tree decoder is "no worse than" a human for random letters when 10% error or less is acceptable [fact, p. 145].
- **Speed agility** (Table XVII, p. 136). An abrupt speed change after every 10th letter; keyboard Morse at 50 wpm, fair fist at 30 and 20 wpm. The *extra* letter error relative to constant speed was:
  - 0–2% at S₁₀₀ = 9 dB;
  - 1–4% at 8 dB;
  - 3–6% at 6 dB.

  [fact, p. 136]
- **Howe's mark–space files** (Table XVIII, p. 138):
  - S1: 15 wpm, element standard deviation/mean 0.2, E_s ≈ 10%;
  - S2: abrupt changes among 10/15/20 wpm, standard deviation/mean 0.15, E_s ≈ 3%;
  - S3: gradual 10–20 wpm drift over 30 s.

  The signal is a constant-amplitude carrier plus white Gaussian noise. At S₁₀₀ = 12 / 9 / 6 dB, letter error was S1: 11/11/24%, S2: 4/6/11% and S3: 5/6/13% [fact, pp. 137–138].
- **Table XIX** (p. 138), high SNR, tree / MAUDE / Howe quasi-Bayes: S1 11/20/8%, S2 4/12/5%, S3 5/14/6%. The MAUDE and quasi-Bayes numbers are quoted from Howe [14, p. 74] [fact].

### 6.5 Field data (Chapter X, pp. 141–143)

Off-air tape recordings, 4 kHz IF, about 50 s per cut, context-free text, high SNR [fact, p. 141].

- **Keyboard senders** (Table XXI) at 35/30/28/32/30 wpm, S₁₀₀ = 20/16/16/18/20 dB: letter error 1/2/1/10/8% [fact, p. 141].
- **Hand senders** (Table XXII) at 18/16/22/20 wpm, S₁₀₀ = 20/16/18/20 dB: 4/3/15/8% [fact, p. 142].
- Failures were caused by burst/static noise, which was accepted as faded marks, and by weak interferers (another Morse signal, one leg of an FSK teletype signal). These interferers dominate during the wanted signal's word spaces and pauses, when the receiver AGC lets the gain rise [fact, pp. 142–143].
- All hand senders were rated good-to-fair [fact, p. 143].

### 6.6 Human operators (Section II.C, pp. 17–23)

- Fig. 1 (p. 19): field operators on an LF communications link (Watt et al. [1]), 5-letter code groups, 12/16/20 wpm, SNR in 100 Hz. About 2–4% error at 8–10 dB and about 30% near 0 dB (SNR in 100 Hz) for 20 wpm [fact; values uncertain, read by eye].
- Fig. 2 (p. 20): laboratory results [2], 35 and 25 wpm, with 20 wpm extrapolated using Lane's speed-adjustment Table II (p. 21). For example, 25 wpm reaches about 2% at 5–8 dB SNR in 100 Hz [fact; values uncertain, read by eye].
- Table II (p. 21), Lane's adjustment in dB added to the abscissa: 10 wpm −5.0, 12 wpm −3.6, 14 wpm −2.3, 15 wpm −1.8, 16 wpm −1.4, 18 wpm −0.6, 20 wpm 0, 25 wpm +1.6, 30 wpm +2.6 [fact, p. 21; the 20 wpm entry is blank in the scan, 0 dB by inference].

### 6.7 Verdicts on the four secondhand claims

- **(a) "Idealized synchronized decoder reaches about 10⁻⁵ letter error at about 12 dB E_b/N₀": FALSE.**
  - Bell's idealized synchronous decoder gives about 2% letter error at E/N₀ = 12 dB for independent letters (Fig. 14, p. 128). The lower bound at E/N₀ = 12 dB is about 0.7%.
  - Nothing in the thesis goes below about 0.1% letter error; the plots stop at 0.1%.
  - Bell's "E_b" is energy per dot, not per bit.
  - The 10⁻⁵ figure probably comes from Table IV (p. 35): 1 − P_d = 2×10⁻⁵ at 50 wpm, S₁₀₀ = 12 dB (E/N₀ = 15.8 dB), and "<10⁻⁵" at 20–30 wpm. That is an **element (bit) detection error for envelope detection**, not a decoder letter error [inference about the origin].
- **(b) "Realistic decoder gives 1–10% letter error at moderate SNR": ROUGHLY TRUE, but needs specifics.**
  - Simulated hand-keyed Morse at 20 wpm: 1–14% letter error at S₁₀₀ = 4–9 dB, i.e. S₅₀₀ ≈ −3 to +2 dB, depending on fist (Table XVI, p. 134). There is a cliff to 31–38% at S₁₀₀ = 3 dB.
  - At 30 wpm the cliff is already at S₁₀₀ = 4 dB (36–46%).
  - Real signals gave 1–15% even at S₁₀₀ = 16–22 dB, mostly because of interference and AGC effects (pp. 141–143).
  - All results are for random-letter text and about 200 letters per experiment.
- **(c) "Lab operators about 2–3 dB better than field operators": CONFIRMED as Bell's own statement.** He says the lab tests show "a difference of about 2–3 dB for equal error rates" [fact, p. 18]. Caveats:
  - the two data sets use different speeds (field 12–20 wpm, lab 25–35 wpm), and the 20 wpm lab curve is extrapolated with Lane's table;
  - the SNR is in 100 Hz;
  - the text is 5-letter random code groups.
  - Bell draws the design conclusion that a machine should be designed against the *lab* curves [fact, p. 18].
- **(d) "Letter entropy about 5.17 bits independent vs about 2.14 bits for English with context": CONFIRMED with a precision caveat.**
  - 5.17 bits/letter is log₂ 36 for **equally likely** independent letters and digits [fact, p. 28].
  - 2.14 bits/letter is the entropy Bell quotes from Shannon [5] for **"a model which produces equally likely words of text"** [fact, p. 29]. It is not a general "English with context" figure.
  - Intermediate figures: 4.76 (equiprobable letters), 4.03 (letter frequencies), 3.32 (first-order), 3.1 (second-order) [fact, p. 29].

## 7. Unfinished work and weaknesses

**Stated by Bell:**

- Only the case with **known** model probabilities was treated. Joint estimation of the transition and mapping probabilities, i.e. adapting to the individual fist, is left open [fact, p. 66].
- Individual operators' duration densities are "far from Gaussian" and have no known parametric form. They must be estimated online by combining parametric and nonparametric methods [fact, p. 93]. The same is conjectured for sending peculiarities in u_k: extra dots, slurs and split characters [fact, p. 101].
- Only independent, equally likely letters were coded. Letter-context, format, grammar and dictionary models (f_λ) are "clearly the only realistic" way to approach human performance on real traffic [fact, pp. 106–108, 145–146]. The abstract repeats that non-random texts need further research [fact, p. 11].
- An adaptive pre-detection filter is needed to handle chirp and drift without the 6–10 dB loss of the 100 Hz filter [fact, pp. 111–112]. Narrower filters or faster sampling make the noise correlated, which then needs a correlated-noise model [fact, pp. 110–113].
- The Gaussian treatment of envelope noise is not correct: Rayleigh/Rician statistics, signal-dependent [fact, p. 114].
- The computational burden is severe and needs dedicated hardware [fact, p. 146]. Field testing was small (about 50 s cuts, 9 cuts) [fact, pp. 140–141].

**Observed in this reading [inference unless marked]:**

- The decoder has no model of interference or impulse noise: every energy burst must be either signal or Gaussian noise. That caused both field failures, where static was taken as faded marks and a weak interferer was decoded during spaces [fact for the failures, pp. 142–143].
- The code and the text disagree on several constants: α, the mark Φ, the transition-variance rule, and the noise-estimator windows [fact, code pp. 170, 178, 190 vs text pp. 98, 116–117, 124]. An implementation should follow one of them consistently.
- The simulator drew durations from truncated Gaussians while the decoder assumed Laplacians [fact, pp. 96–98, 125]. This mismatch is realistic but uncontrolled.
- The printed algorithm omits Steps 5–7, and Table XII has rows that do not sum to 1 [fact, pp. 81–82, 105].
- Probabilities are carried in linear form with renormalization, not in the log domain [fact, code pp. 176–178].
- There is no path merging, so equivalent hypotheses consume several of the 25 slots.
- Statistical power is low: about 200 letters per cell, so a "5%" result has a 90% confidence interval of roughly 3–8% [fact for the table, p. 139].

## 8. Assessment for a modern implementation (all [inference])

Target: a 1500 samples/s complex-baseband channel, band-limited to about ±150 Hz, one channel per detected signal.

**Adopt as is:**

- The joint state (letter-tree node, element, key state, elapsed duration, speed) and the hazard-form, speed-normalized duration model. This is the core of Bell's gain over threshold decoders and costs little.
- One amplitude Kalman filter per hypothesis, whose innovation likelihood scores the hypothesis. The per-state Q that allows a jump in amplitude after long spaces, but not within a character, is a good, cheap way to handle QSB and a new station taking over the frequency.
- Probability-mass pruning (P_opt ≈ 0.9 with a hard cap) plus "keep the best path for each element state". The cap is what makes the processing load adapt to SNR.
- Common-ancestor decoding with a bounded delay of about 1 s.
- Speed changes only at element boundaries, with larger steps allowed after word spaces and pauses.

**Change:**

1. **Front end and rate.** Decimate 1500 → 187.5 samples/s (÷8, τ = 5.33 ms) or 250 samples/s (÷6, τ = 4 ms). The key-state resolution matches Bell's 5 ms. Do not copy his 100 Hz pre-detection filter. Use a filter matched to the element rate, or a small bank covering the speed range: this recovers the 6–10 dB Bell gave up (pp. 111–112). The project's 252 Hz noise-bandwidth channel filter is far wider still.
2. **Use the complex samples.** Bell's Gaussian-on-envelope likelihood is a two-moment approximation. Two better options:
   - (a) keep envelope/power samples but use the exact Rician (key down) and Rayleigh (key up) likelihoods, with the amplitude state as the Rician parameter. This loses the Kalman closed form but can be done with a grid or a linearization;
   - (b) run a hypothesis-conditioned Kalman filter on the **complex** amplitude, with a phase/frequency random walk to absorb chirp and drift. This gives a Gaussian likelihood that is actually correct, at about 4× the cost of a scalar filter.

   Option (b) is the natural "pre-detection" extension of Bell's structure and fits a ±150 Hz I/Q stream.
3. **Log domain and merging.** Carry log-probabilities. Merge hypotheses that share (letter node, element, speed, duration bin) by moment-matching their amplitude Gaussians, as in generalized pseudo-Bayes (GPB)/interacting multiple model (IMM) filters. This frees slots Bell spent on duplicates.
4. **Noise-floor estimate.** Replace the ad hoc minimum tracker with the project's own noise estimator, and pass a realistic R to the filters. Bell's Table XIII shows robustness to a mismatch of a few dB.
5. **Interference and impulse hypotheses.** Add at least a "non-Morse energy" state and a heavy-tailed measurement model (for example a Gaussian mixture or Student-t innovation). Bell's two field failures show this is the dominant real-world error source.
6. **Language model.** Replace uniform letters with a letter n-gram model plus a ham-traffic grammar (CQ/DE/callsign/RST/5NN/TU). Bell's own entropy analysis and conclusions say this is where the large remaining gain is. A callsign-structure model is the skimmer-relevant equivalent of his "format" model.
7. **Speed grid.** Bell's 10–60 wpm integer grid with ±2/±4, ±5/±10 and ±10/±20 steps is adequate. Consider log-spaced speed steps (proportional to speed), since fist jitter scales with speed. Initialize from a coarse speed estimate instead of five fixed speeds.
8. **Fist adaptation.** Estimate α (the duration spread) and, ideally, the dash/dot and space ratios online per channel. Bell names this as open, and the project's hand-keyed traffic will need it.

**Per-channel cost** at about 190–250 samples/s, 25-path cap, 30 successors per path, scalar amplitude Kalman filter:

- Measured from Bell's pruning statistics, about 8–16 paths are saved on average, and only about 1 + (allowed element changes × 5 speeds) successors per path survive the transition-probability cut. That is roughly 150–400 Kalman steps plus likelihoods per sample, around 20–40 flops plus two or three transcendental functions each: about 10⁴ flops per sample, or **about 2–3 Mflop/s per channel**. The worst case (750 extensions per sample) is about 8–10 Mflop/s.
- The front end (complex FIR decimation 1500 → about 200 samples/s plus detection) adds well under 1 Mflop/s.
- A complex-amplitude Kalman filter (option 2b) multiplies the Kalman part by about 3–4.
- A 100-channel skimmer therefore needs roughly 0.3–1 Gflop/s for the scalar version, and a few Gflop/s with complex filters and a larger path cap. That is feasible on a modern multicore CPU with vectorization across hypotheses or channels. The main cost drivers are the path cap and the number of speed branches.
