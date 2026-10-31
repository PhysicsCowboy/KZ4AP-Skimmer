# Step 1: standard practice for a model-based Morse decoder (blind review)

> **Committed 2026-10-14** (owner's approval) from `build/standard-practice-check/`, where it was written. The text
> below is the reviewer's, unchanged; only this note is added. Its derivation script `numbers.py` is committed
> beside it, with its "Run:" line updated to that path; its printed output is not committed, because rerunning the
> script (numpy only) reproduces it exactly (checked 2026-10-14). The other files named under "Files" below (the
> downloaded source texts and the reference-lookup helpers) stay in `build/` and are not committed: the texts are
> other authors' publications, and every one can be fetched again from section 6.

Reviewer: an independent Claude agent, working blind. I read only the step-1 brief; no project file, design note,
repository page or session record. Written 2026-10-14. Status: **complete** (written incrementally).

Sources were found by web search, by following the reference lists of the Morse sources I found (Bell 1977, Mills
1977), and by checking bibliographic records against Crossref; I did not search for the project by name.

## 0. How to read this file

**Classes.** Every statement about practice carries one of the brief's four classes:

- **(S) standard**, with a source: a textbook, a widely cited tutorial or survey, or the documentation of a widely
  used system.
- **(C) common practice:** used in many published systems but not codified (examples given).
- **(A) adapted:** no standard exists for this problem, so the nearest standard from another field is used, with the
  changes stated.
- **(J) my judgment.**

How I assign them: a decision is **(S)** when a textbook or survey treats this exact sub-problem class and its answer
applies to Morse as written (for example, detecting on-off keying with unknown phase is a textbook communications
problem). It is **(A)** when the sub-problem exists only because of Morse (Farnsworth spacing, a fist, a change of
station between overs) and the answer is borrowed from another field with changes. It is **(C)** when many published
systems do it but no textbook says so, and **(J)** when it is my own reasoning. A derived number is labeled with
the class of the formula it comes from, and the derivation is in `numbers.py` (section N1 to N12 of its output).
A sentence that only reports what one or two cited sources did or said (for example, "Bell used 5 ms frames") is a
reported fact, not a claim about practice: it carries its source and the source's status ("read", "abstract",
"record only") instead of a class, and one or two such instances are never counted as (C).

**Sources.** Keys in square brackets, for example [Bell1977], point to section 6. For each source, section 6 says
whether I read its text (all or the cited part), only its abstract, or only its bibliographic record. When I state
what a source says without having read it, the claim is my recollection of a well-known result and is marked
"(record only)" in section 6; step 2 should treat such attributions with that caution.

**Units.** Every dB value names its reference. S₅₀₀ is the brief's SNR: key-down carrier power over the noise power
in 500 Hz, in dB. E/N₀ is the energy of one key-down element (key-down power times its duration) over the noise
power spectral density N₀ (noise power per hertz), in dB; with these definitions
E/N₀ = S₅₀₀ + 10·log₁₀(500 Hz × duration). Filter responses are in dB relative to the passband. Neighbor levels
are in dB relative to the wanted station's key-down power. T is the dot length, T = 1.2/w seconds at w WPM.

**Assumptions** (the brief is silent on these; I state them and go on):

1. "Doppler spread" in the test set is taken as the Watterson / ITU-R F.1487 "frequency spread", which is twice the
   standard deviation of a Gaussian Doppler spectrum [ITU-R-F1487]. If the test set instead uses the Clarke/Jakes
   maximum Doppler frequency, the coherence times quoted below shrink by a factor 0.65 (N5, N5b).
2. The decoder may decimate or filter the 1500 samples/s channel further; nothing outside the channel is available.
3. Where I need the channel filter's shape between the stated points, I model it as flat to ±39 Hz with a
   raised-cosine roll-off to zero at ±279 Hz. This model gives −5.1 dB relative to the passband at ±150 Hz and a
   noise bandwidth of 258 Hz, close to the brief's −6 dB and 252 Hz; it is used only for the noise-correlation
   numbers in N6 (J).
4. Farnsworth timing follows the ARRL standard [Bloom1990]; the test set's exact formula is not stated.

**Files** (all in `C:\KZ4APSkimmer\build\standard-practice-check\`, git-ignored): `numbers.py` and its output
`numbers_out.txt` (every derived number); `crossref_check.py` with `refs_batch*.txt` and outputs (bibliographic
checks against Crossref); `s2_abstracts.py` (abstract lookups); `downloads\` (texts I read: the Bell 1977 and Mills
1977 reports, ITU-R F.1487 and M.1677-1, Bloom 1990, Gales and Young 2008, Murphy 2002, Särkkä 2013, Johnson and
Willsky 2010, Krebs et al. 2015). They can be deleted; every one can be fetched again from the URL in section 6.

## 1. Summary table

80 decisions: **45 (S)**, **3 (C)**, **24 (A)**, **8 (J)**. The class is that of the standard answer as stated in
the part's section; where a part's text adds a Morse-specific adaptation to a standard answer, the row keeps (S)
and the adaptation is described in the part. P0 is the overall architecture (section 2).

| ID | Part | Decision | Standard answer (short) | Class | Sources |
|---|---|---|---|---|---|
| P0.1 | Architecture | Formulation | Generative source-channel model, decoded by maximizing the posterior | S | [Jelinek1976], [Bahl1983], [GalesYoung2008] |
| P0.2 | Architecture | Integration | One joint search over all knowledge sources with soft observations, not a cascade of hard decisions | S | [GalesYoung2008], [Mohri2002], [Forney1973] |
| P0.3 | Architecture | Model family | Switching state-space model (discrete states plus conditionally Gaussian continuous states) | S | [Murphy2002], [CemgilKappen2003]; Morse: [Bell1977] |
| P0.4 | Architecture | Implementation of the network | Compiled finite-state network; hand-built is an equal option for a small vocabulary | S | [Mohri2002] |
| P1.1 | P1 Detection | Coherent or noncoherent | Noncoherent (envelope/energy); costs 1.0 dB of E/N₀ at a 1 % element error (N4) | S | [Proakis2008], [VanTrees1968] |
| P1.2 | P1 Detection | Pre-detection filter | Matched filter in complex baseband (noise bandwidth 1/T), following the speed hypothesis | S | [Turin1960], [Proakis2008] |
| P1.3 | P1 Detection | Unknown frequency | Estimate and track: periodogram acquisition; HMM, Kalman or PLL tracking (offset loss 8.8 dB at 20 WPM, N3) | S | [RifeBoorstyn1974], [StreitBarrett1990], [Meyr1998] |
| P1.4 | P1 Detection | Observation model | Exact Rayleigh/Rician (chi-square) densities; estimator-correlator under fading | S | [VanTrees1968], [Proakis2008], [Kailath1969] |
| P1.5 | P1 Detection | Fading | Gauss-Markov gain from the Doppler spread, Kalman-tracked; noncoherent combining when fading is fast (N5) | S | [Watterson1970], [ITU-R-F1487], [Baddour2005], [Proakis2008] |
| P1.6 | P1 Detection | Correlated noise | Whiten or decimate (generalized matched filter); naive overcount 5.95 (N6) | S | [Kay1998] |
| P1.7 | P1 Detection | Soft or hard decisions | Soft (likelihoods) | S | [Forney1973], [Proakis2008] |
| P1.8 | P1 Detection | Frame step | At most about T/4: ≤ 3 ms at 100 WPM (speech uses 10 ms) | A | [GalesYoung2008], [Bell1977], [Mills1977] |
| P2.1 | P2 Timing | Duration model | Explicit durations (hidden semi-Markov), not geometric | S | [Murphy2002], [Yu2010], [JohnsonWillsky2010] |
| P2.2 | P2 Timing | Duration implementation | Duration counter with hazard-rate transitions, O(T·D·K²) | S | [Murphy2002]; Morse: [Bell1977] |
| P2.3 | P2 Timing | Duration distributions | Time in units of T; scatter proportional to duration (constant CV), parameters measured | A | [Gibbon1977], [WingKristofferson1973], [Mills1977], [Bell1977] |
| P2.4 | P2 Timing | Speed representation | Latent log-speed with random-walk prior; log grid (33 to 63 states, N11) or continuous | A | [CemgilKappen2003], [Krebs2015] |
| P2.5 | P2 Timing | When speed may change | Only at element boundaries | A | [Krebs2015], [Bell1977] |
| P2.6 | P2 Timing | Abrupt speed changes | Switching or jump prior (IMM, change-points, heavy-tailed transitions) | A | [BlomBarShalom1988], [AdamsMacKay2007], [Krebs2015] |
| P2.7 | P2 Timing | Style parameters | Additive weighting δ, dash/dot ratio, Farnsworth gap stretch g (up to about 16, N10), MAP-adapted | A | [GauvainLee1994], [Bloom1990], [Mills1977] |
| P2.8 | P2 Timing | Context dependence | Durations conditioned on the preceding element and keying device | A | [GalesYoung2008], [Mills1977], [Bell1977] |
| P3.1 | P3 Nuisances | Joint or separate | Non-data-aided acquisition; decision-directed or per-survivor tracking at low SNR | S | [Meyr1998], [Mengali1997], [Raheli1995] |
| P3.2 | P3 Nuisances | Noise level | Robust estimate: minimum statistics with bias correction, ordered-statistic CFAR; key-up frames | S | [Martin2001], [Rohling1983] |
| P3.3 | P3 Nuisances | Amplitude | Kalman filter on a Gauss-Markov gain, per hypothesis, updated on key-down | S | [BarShalom2001], [Baddour2005] |
| P3.4 | P3 Nuisances | Frequency and drift | Periodogram acquisition; Kalman/PLL or HMM tracking; Cramér-Rao 1.2 Hz per dot at 20 WPM, S₅₀₀ = 0 dB (N8) | S | [RifeBoorstyn1974], [Meyr1998], [StreitBarrett1990] |
| P3.5 | P3 Nuisances | Speed estimation | Jointly in the state | A | [Krebs2015] |
| P3.6 | P3 Nuisances | Cold start | Broad priors from the detector; re-decode the opening after convergence | A | [GalesYoung2008], [Sarkka2013] |
| P3.7 | P3 Nuisances | Process noise | Derived from physics; checked by NEES/NIS consistency tests | S | [BarShalom2001] |
| P3.8 | P3 Nuisances | Plug-in or marginalize | Marginalize, or carry estimate variance, for calibrated output (both methods standard) | J | [Kay1998] |
| P4.1 | P4 Inference | Model class | Switching state-space model | S | [Murphy2002], [ChenLiu2000] |
| P4.2 | P4 Inference | State contents | Every variable the factors condition on (Markov in the augmented state) | S | [Murphy2002] |
| P4.3 | P4 Inference | Continuous-state approximation | Keep best per survivor, merge (GPB, IMM), or sample (Rao-Blackwellized PF) | S | [Raheli1995], [Murphy2002], [BlomBarShalom1988], [ChenLiu2000], [Doucet2000] |
| P4.4 | P4 Inference | Search algorithm | Time-synchronous Viterbi with recombination (token passing) | S | [GalesYoung2008], [Young1989], [NeyOrtmanns2000] |
| P4.5 | P4 Inference | Pruning | Beam plus histogram (active-hypothesis cap), tuned on development data | S | [Steinbiss1994], [NeyOrtmanns2000], [Kaldi] |
| P4.6 | P4 Inference | Objective | Viterbi for a transcript; minimum-Bayes-risk / consensus for error rate | S | [GoelByrne2000], [Mangu2000] |
| P4.7 | P4 Inference | Numerics | Log domain or scaling | S | [Rabiner1989] |
| P4.8 | P4 Inference | Streaming decisions | Partial traceback with a forced decision at a maximum delay; fixed-lag smoothing | S | [Brown1982], [Sarkka2013] |
| P4.9 | P4 Inference | Determinism | Seeded randomness, deterministic tie-breaks, fixed reduction order, fixed build | J | [IEEE754] |
| P4.10 | P4 Inference | Passes | One integrated pass with bounded lag; optional re-decoding in the correction window | A | [GalesYoung2008] |
| P5.1 | P5 Segmentation | No-signal hypothesis | Explicit background model in the search, with hang-over | S | [Sohn1999], [GalesYoung2008] |
| P5.2 | P5 Segmentation | Non-Morse keying | Filler models: a carrier state and an unconstrained keying loop with a tuned penalty | A | [RosePaul1990] |
| P5.3 | P5 Segmentation | Start and end of transmissions | Background-Morse transitions with minimum durations; end decided by posterior | A | [Sohn1999], [Bell1977] |
| P5.4 | P5 Segmentation | Impulsive noise | Heavy-tailed likelihood and/or blanking (plus an impulse event in the model) | S | [Middleton1977], [Huber1964], [Kassam1988], [Zhidkov2008] |
| P5.5 | P5 Segmentation | Other signals | Selectivity; joint (factorial) model; cancellation, chosen by leakage (N7) | S | [Harris1978], [GhahramaniJordan1997], [Verdu1998] |
| P5.6 | P5 Segmentation | Gain control | No AGC in the decoder path; model the level | J | evidence: [Bell1977], [Mills1977] |
| P6.1 | P6 Text | Unit | Characters, with optional token classes | A | [PlotzFink2009] |
| P6.2 | P6 Text | Smoothing and order | Modified Kneser-Ney (or Witten-Bell); order by held-out perplexity | S | [ChenGoodman1999], [KneserNey1995], [WittenBell1991] |
| P6.3 | P6 Text | Mixed content | Mixture of component models, EM-weighted on held-out text; weights adapted online | S | [JelinekMercer1980], [IyerOstendorf1999], [Bellegarda2004] |
| P6.4 | P6 Text | Callsigns without lists | Class-based model with a callsign-format sub-model | A | [Brown1992], [Mohri2002], [ITU-RR19] |
| P6.5 | P6 Text | Repetition in a contact | Cache component | A | [KuhnDeMori1990] |
| P6.6 | P6 Text | Weight against the signal | Scale factor and insertion penalty tuned on development data (speech: 8 to 20) | S | [GalesYoung2008] |
| P6.7 | P6 Text | Estimation and evaluation | Smoothed counts; held-out perplexity; end-to-end error versus scale | S | [ChenGoodman1999], [Jelinek1997], [GalesYoung2008] |
| P6.8 | P6 Text | Symbol inventory | Word space and each prosign as one symbol, matching the metric | J | brief |
| P7.1 | P7 Station change | Detect or model | Change detection, Bayesian online change-points, or a latent station variable | A | [Page1954], [Basseville1993], [AdamsMacKay2007], [TranterReynolds2006] |
| P7.2 | P7 Station change | Where changes occur | In gaps; prior raised by end-of-over procedure signals | A | [Bell1977], [ITU-R-M1677] |
| P7.3 | P7 Station change | What resets | Sender parameters reset; channel noise kept | A | [TranterReynolds2006] |
| P7.4 | P7 Station change | Returning stations | Per-station parameter sets; re-identification | A | [TranterReynolds2006], [ChenGopalakrishnan1998] |
| P7.5 | P7 Station change | First word after a change | Re-decode the opening after convergence | A | [GalesYoung2008], [Sarkka2013] |
| P8.1 | P8 Outputs | Per-character probabilities | Posteriors in a confusion network, including "no symbol" | S | [Mangu2000], [EvermannWoodland2000], [Wessel2001] |
| P8.2 | P8 Outputs | Calibration | Post-hoc map on held-out data; reliability diagram, ECE, Brier, NCE | S | [Platt1999], [Guo2017], [NiculescuMizil2005], [Naeini2015], [Brier1950], [Siu1997] |
| P8.3 | P8 Outputs | Overall confidence, hand-off | Chow's reject rule on calibrated posteriors; aggregate as expected errors Σ(1 − pᵢ) | A | [Chow1970], [Jiang2005] |
| P8.4 | P8 Outputs | Streaming and corrections | Interim results with stability, finalized by traceback or fixed lag; instability measured | S | [GoogleSTT], [Brown1982], [Sarkka2013], [Shangguan2020] |
| P8.5 | P8 Outputs | Element timing | Best-path alignment | S | [GalesYoung2008] |
| P8.6 | P8 Outputs | Speed | Smoothed posterior mean with its spread | J | precedent [Bell1977] |
| P9.1 | P9 CPU | Cost knob | Pruning caps (active hypotheses, beam) | S | [Kaldi], [Steinbiss1994] |
| P9.2 | P9 CPU | Overload policy | Mandatory minimal decode, optional effort by priority, shedding last | A | [Liu1991], [Zilberstein1996], [Tatbul2003] |
| P9.3 | P9 CPU | Priorities | New channels first, then uncertain ones | J | |
| P9.4 | P9 CPU | Cost measurement | Measured real-time factor; accuracy-cost curves | S | [NeyOrtmanns2000], [Kaldi] |
| P9.5 | P9 CPU | Benchmark under load | Fixed, load-independent settings | J | |
| P9.6 | P9 CPU | Implementation efficiency | Block processing, vectorization, lowest adequate frame rate | C | [Kaldi] |
| P10.1 | P10 Testing | Derive or tune | Derive physical parameters; tune the rest on development data; test once | S | [Hastie2009], [GalesYoung2008] |
| P10.2 | P10 Testing | Data partition | Disjoint by independent units; guard against development-set over-fitting | S | [Hastie2009], [CawleyTalbot2010] |
| P10.3 | P10 Testing | Sampling conditions | Stratified random (jittered-grid, Latin-hypercube) sampling | S | [McKay1979], [BergstraBengio2012] |
| P10.4 | P10 Testing | Approximation error | With/without runs on the same signals; forced-alignment search-error counts; ablation | C | [NeyOrtmanns2000] |
| P10.5 | P10 Testing | Comparing variants | Paired tests with signals as units (MAPSSWE, sign, Wilcoxon); bootstrap; multiple-comparison control | S | [GillickCox1989], [NIST-SCTK], [BisaniNey2004], [Holm1979], [BenjaminiHochberg1995] |
| P10.6 | P10 Testing | Error rate versus SNR | Threshold S₅₀₀ by sigmoid fit with bootstrap intervals | A | [WichmannHill2001], [ITU-R-F1487] |
| P10.7 | P10 Testing | Test length under fading | ITU rule 3000/spread for modems; many independent signals per slow-fading cell for CER | A | [ITU-R-F1487] |
| P10.8 | P10 Testing | Calibration evaluation | On held-out data, per condition cell | S | [Guo2017], [Naeini2015] |
| P10.9 | P10 Testing | Reference transcripts | Blind transcription; double-transcribed sample to measure reference error | J | |
| P10.10 | P10 Testing | Baselines and bounds | Genie-aided decoder and simple baseline beside the full decoder | C | [Bell1977], [Mills1977] |
| P10.11 | P10 Testing | Error metric | Levenshtein CER, as the brief defines it | S | [Levenshtein1966], [WagnerFischer1974] |

## 2. The standard overall architecture

**Short answer.** Yes for the class of problem, no for Morse in particular. For recognizing text from a noisy signal
produced by a known code with variable timing, the standard architecture is the *source-channel model with
search*, worked out for speech recognition between 1975 and 2010 and used in the same form in handwriting
recognition, music transcription and digital receivers (S). For Morse it has one detailed published precedent
(Bell's 1977 dissertation), one partial one (Mills 1977), and vendor statements; no textbook or survey codifies a
standard Morse decoder. So the architecture-level decisions (P0 below) are **(S)**, while fitting the architecture
to Morse needs adaptations, which are the **(A)** decisions in the parts.

**What the standard architecture is.**

1. *A generative model of everything, factored by stage.* The probability of a text W and of the received signal Y
   is written as P(W)·p(Y | W): a **language (text) model** P(W); a **lexicon** that spells each character as
   elements (here, the Morse code tree); a **duration or timing model** (how long each element and gap lasts);
   and an **observation model** p(y | state) for each short frame of signal. Decoding finds the W that maximizes
   P(W)·p(Y | W) (S: [Jelinek1976], [Bahl1983], [Jelinek1997]). Gales and Young's survey gives exactly this
   structure: feature extraction, then a decoder that searches with acoustic models, a pronunciation dictionary
   and an n-gram language model, "using pruning to remove unlikely hypotheses", optionally producing lattices
   (S: [GalesYoung2008], §2 and Fig. 2.1, read).
2. *A hidden Markov or semi-Markov network.* All knowledge sources are compiled into one network of discrete states
   (a hidden Markov model, HMM: a chain of hidden states, each emitting observations, with fixed transition
   probabilities; or a hidden semi-Markov model, HSMM, in which each state also has an explicit duration
   distribution) (S: [Rabiner1989], [Murphy2002], [Yu2010]). Compiling by composition of weighted finite-state
   transducers is the modern standard implementation (S: [Mohri2002]).
3. *Time-synchronous dynamic-programming search.* The Viterbi recursion δ_t(j) = max_i δ_{t−1}(i)·a_ij·b_j(y_t)
   finds the best state sequence; in practice it runs as a beam search that keeps only hypotheses scoring within a
   margin of the best (S: [GalesYoung2008] eq. 2.21, read; [NeyOrtmanns2000]; [Forney1973]).
4. *Posteriors from the same network.* Forward-backward (the BCJR algorithm in coding) or lattices (compact graphs
   of the competing hypotheses kept by the search) give per-symbol posterior probabilities, used for confidence and for minimum-error decoding (S: [BCJR1974],
   [Mangu2000], [Wessel2001]).
5. *Nuisance parameters* (level, frequency, timing) handled either by estimation outside the search (adaptation,
   synchronization) or inside it by augmenting the state; the second makes the model a *switching state-space
   model* (discrete states selecting the dynamics of continuous ones), solved approximately by keeping a Kalman
   filter (the recursive estimator of a Gaussian state from noisy linear observations) per surviving hypothesis or
   by particle filtering (a set of weighted random samples of the state) (S: [Raheli1995], [Murphy2002] §4.3, read;
   [ChenLiu2000]; [BlomBarShalom1988]).

**Evidence that this is the standard.**

- Speech: the textbooks and surveys above (S).
- Handwriting: HMMs with n-gram language models are the standard offline handwriting recognizers of the pre-neural
  era (S: [PlotzFink2009], [MartiBunke2001]).
- Music rhythm, the closest timing problem: tempo tracking and rhythm quantization are posed as a switching
  state-space model "where the switch variables correspond to discrete note locations as in a musical score, and
  the continuous hidden variables denote the tempo" (S within that field: [CemgilKappen2003], abstract read;
  [Raphael2002]; [Krebs2015], read).
- Digital receivers: maximum-likelihood sequence estimation by the Viterbi algorithm, with per-survivor processing
  for unknown channel parameters (S: [Forney1973], [Raheli1995]).
- Morse itself: Bell derived the optimal Bayesian decoder for hand-keyed Morse as "a denumerable but exponentially
  expanding set of linear Kalman filters operating on a dynamically evolving trellis" of key state, element, letter
  and speed, and built a pruned version; he likened the problem to speech recognition and pointed to the
  speech-recognition "linguistic statistical decoder" of Jelinek, Bahl and Mercer for text modeling ([Bell1977],
  read). Mills built a real-time decoder with a matched filter, a Markov model of the Morse code with a-priori
  and transition probabilities, and Viterbi decoding ([Mills1977], read). The best-known skimmer describes its
  decoder only as "based on the methods of Bayesian statistics" ([CWSkimmer], read). This is one dissertation, one
  technical report and a vendor sentence: a precedent, not a codified standard (J).

**Main alternative frameworks, and when each is preferred.**

| Framework | What it is | Preferred when | Class |
|---|---|---|---|
| Source-channel model with HMM/HSMM search (above) | joint search over text, code, timing and signal models | the default for model-based decoding; uses all constraints; gives posteriors | S (A for Morse) |
| Cascade: threshold detector → mark/space durations → threshold or clustering classifier → table lookup | hard decisions at each stage | high SNR, minimal CPU; cannot use the code or text to repair a wrong key decision | C: [Gold1959] via [Mills1977]; [fldigi]; [Wang2017] |
| Duration-domain probabilistic decoding | hard key decisions, then probabilistic (Bayes or Viterbi) decoding of the mark/space durations | moderate SNR, low CPU; loses the soft information of the signal | C: [Mills1977]; [AG1LE2013a] |
| Sequential Monte Carlo (particle filter), possibly Rao-Blackwellized (a Kalman filter per particle for the linear-Gaussian part) | sampled hypotheses over hybrid discrete-continuous states | continuous nuisances (frequency, speed) that discretize badly or are strongly non-Gaussian | S: [Arulampalam2002], [Doucet2000], [ChenLiu2000]; used for tempo tracking [CemgilKappen2003] |
| Hybrid neural observation model + HMM search | a trained network supplies frame likelihoods to the same search | when labeled training data are plentiful; the 2010-2015 speech standard | S: [Hinton2012] |
| End-to-end neural (CNN+LSTM trained with CTC) | one network maps signal to text | large training sets; the brief's separate mode | S for the method: [Graves2006]; Morse: [Dey2018] |

**Why the source-channel architecture is the usual first choice for a model-based decoder** (J, drawing on the
sources above): it is the only one of these that keeps the code and text constraints and soft signal information
in one search, which is what lets it correct a wrong demodulation decision; Bell made exactly this argument, that
"as a result of a single incorrect demodulation decision, an entire letter (two letters if the symbol was a
character space) is transcribed incorrectly" unless demodulation is integrated with knowledge of the code and
language ([Bell1977], read). Mills measured about 5 dB (in SNR) of gain from Viterbi decoding over a fixed-interval
classifier at a 10 % error rate, while a second comparison in the same report was "not enough to be statistically
significant" ([Mills1977], read).

**Decisions at this level** (numbered P0 so that step 2 can refer to them):

- **P0.1 Formulation.** A generative source-channel model decoded by maximizing the posterior. Standard answer:
  yes (S: [Jelinek1976], [Bahl1983], [GalesYoung2008]).
- **P0.2 Integration.** One joint search over all knowledge sources with soft observations, rather than a cascade of
  hard decisions. Standard answer: integrated search (S: [GalesYoung2008] §2.4; [Mohri2002]; soft decisions:
  [Forney1973]). Morse decoders in the field mostly cascade (C: [fldigi], [Gold1959] via [Mills1977]).
- **P0.3 Model family.** A dynamic Bayesian network with discrete states (text, code position, element, key state,
  duration) and continuous nuisance states (amplitude, frequency, possibly speed): a switching state-space model.
  Standard answer: yes (S: [Murphy2002]; analog in rhythm: [CemgilKappen2003]; Morse precedent: [Bell1977]).
- **P0.4 Implementation of the network.** Compile text model, code tree and timing topology into one finite-state
  network (S: [Mohri2002]); for a vocabulary as small as Morse's (about 60 symbols) a hand-built network is an
  equally standard option (J).

## 3. The parts

### P1. Detecting a keyed carrier: unknown amplitude and phase, small frequency offset and drift, correlated noise, fading

**(a) Fields and why they apply.** Statistical detection and estimation theory ([VanTrees1968], [Kay1998]);
digital communications, specifically noncoherent on-off keying and fading channels ([Proakis2008],
[SimonAlouini2004]); matched filtering ([Turin1960]); HF channel modeling ([Watterson1970], [ITU-R-F1487]). They
apply directly: a Morse transmission *is* on-off keying (OOK: the carrier is either on or off), received through the
ionosphere as a sinusoid of unknown amplitude and phase, multiplied by a random complex gain (Rayleigh fading), in
additive Gaussian noise. That is the textbook problem of noncoherent OOK detection over a fading channel (S).

**(b) Decisions and the standard answer to each.**

- **P1.1 Coherent or noncoherent detection.** Standard answer: *noncoherent* (the decision uses the envelope or
  energy of the filtered signal, so the unknown carrier phase drops out) when the phase is unknown and cannot be
  tracked reliably (S: [Proakis2008]; [VanTrees1968]). Its cost, derived for one element decision with perfect
  timing and level and the optimum threshold: 2.0 dB of E/N₀ at a 10 % error probability, 1.0 dB at 1 %, 0.7 dB at
  0.1 %, relative to coherent detection (N4, derived from the standard Rician and Gaussian error formulas) (S).
- **P1.2 Pre-detection filtering.** Standard answer: filter in complex baseband with a filter *matched* to the
  key-down pulse (a matched filter correlates the input with the expected pulse shape, which maximizes the output
  SNR in white noise) before taking the magnitude. For a rectangular element of length T the matched filter is an
  integrate-and-dump over T, whose (two-sided) noise bandwidth is 1/T: 4.17 Hz at 5 WPM, 16.7 Hz at 20 WPM,
  83.3 Hz at 100 WPM (N2) (S: [Turin1960]; [Proakis2008]). Integrating over the whole 252 Hz channel instead costs
  10·log₁₀(252 Hz × T) of SNR for a narrowband signal: 11.8 dB at 20 WPM, 4.8 dB at 100 WPM (N2, derived). Because
  T is unknown and varies 60:1 (a 12 ms dot at 100 WPM to a 720 ms dash at 5 WPM), the filter must follow the speed
  hypothesis (A, see (e)).
- **P1.3 Unknown frequency.** Standard answer: *estimate and track* the carrier frequency rather than widen the
  filter to cover the uncertainty. Acquisition: the maximum of the periodogram (the squared magnitude of the
  Fourier transform, computed by FFT) is the maximum-likelihood estimate of a single tone in white noise (S:
  [RifeBoorstyn1974]); equivalently, a bank of matched filters across
  frequency (S: [Kay1998]), whose worst-case loss midway between filters is |sinc(0.25)|² = 0.9 dB for a spacing of
  0.5/T and |sinc(0.5)|² = 3.9 dB for a spacing of 1/T (derived from the same sinc formula). Tracking at low SNR
  with drift: a hidden Markov model whose states are frequency cells, decoded by Viterbi ("frequency line
  tracking") (S: [StreitBarrett1990]), or a Kalman filter /
  phase-locked loop on frequency and drift rate at higher SNR (S: [Meyr1998]). Why it is needed, derived (N3): a
  matched filter centered 11.7 Hz away from the carrier loses |sinc(Δf·T)|², which is 8.8 dB at 20 WPM and 23.8 dB at
  5 WPM but only 0.28 dB at 100 WPM; a 1 dB loss is reached at Δf = 0.26/T, that is 1.1 Hz at 5 WPM, 4.4 Hz at
  20 WPM and 21.8 Hz at 100 WPM (S formula; derived values).
- **P1.4 The observation model (the likelihood the decoder uses).** Standard answer: the exact density of what is
  observed under each key state. For a complex sample or matched-filter output z with unknown uniform phase:
  Rayleigh density of |z| with key up (the magnitude of complex Gaussian noise), Rician with key down (the magnitude
  of a constant plus that noise); equivalently, central and noncentral chi-square densities for |z|² (S:
  [VanTrees1968]; [Proakis2008]). With Rayleigh fading the key-down signal is itself a complex Gaussian
  process, and the optimum likelihood is the *estimator-correlator*: a causal (Kalman) predictor of the complex gain
  whose prediction errors give the likelihood (S: [Kailath1969]).
- **P1.5 Fading.** Standard answer: model the complex gain as a Gaussian process with the Doppler spectrum of the
  HF channel model (Gaussian spectrum of "frequency spread" 2σ in the Watterson model) (S: [Watterson1970];
  [ITU-R-F1487], read), represent it for tracking by an autoregressive (Gauss-Markov) process (S: [Baddour2005] on
  autoregressive models of fading, record only; Bell used a first-order one, [Bell1977], read), and track it with a
  Kalman filter conditioned on the key state; when the gain changes within one
  element, integrate coherently only over pieces shorter than the coherence time and combine the pieces
  noncoherently (S: [Proakis2008]; [SimonAlouini2004]). Derived coherence times (N5, Gaussian spectrum, correlation
  ρ(τ) = exp(−2π²σ²τ²), σ = spread/2): ρ falls to 0.5 after 0.375/spread, which is 7.5 s at 0.05 Hz, 375 ms at 1 Hz
  and 75 ms at 5 Hz. At 5 Hz the gain correlation across one dot is 0.001 at 5 WPM, 0.64 at 20 WPM and 0.98 at
  100 WPM: slow Morse in flutter is in the fast-fading regime, where a dot cannot be integrated coherently (S
  formula; derived values).
- **P1.6 Correlated noise.** Standard answer: account for the correlation, either by whitening before matched
  filtering (the "generalized matched filter") or by filtering to the signal band and decimating so that the
  samples used are nearly independent (S: [Kay1998]). Treating the 1500 samples/s as independent when the noise
  bandwidth is 252 Hz overstates the log-likelihood ratio of a narrowband signal by f_s/B_N = 1500/252 = 5.95 (N6,
  derived: for a narrowband signal the true information is E/N₀ while the independence assumption gives
  E·f_s/(N₀·B_N)). In the modeled filter the noise correlation is 0.53 at 2 ms and about 0 at 4 ms (N6). Speech
  recognition lives with a similar overconfidence, which Gales and Young attribute to the HMM's conditional
  independence assumptions, and compensates with an empirically tuned scale on the observation log-likelihoods
  (S for speech: [GalesYoung2008] §4.2, read; as a remedy here it is A).
- **P1.7 Soft or hard decisions to the decoder.** Standard answer: pass soft information (likelihoods), not sliced
  key-up/key-down decisions (S: [Forney1973]; [Proakis2008], where soft-decision decoding gains about 2 dB over hard
  decisions on the Gaussian channel (record only)).
- **P1.8 Frame rate (time step) of the observation sequence.** Standard answer in speech: feature vectors
  "typically computed every 10 ms using an overlapping analysis window of around 25 ms" (S: [GalesYoung2008],
  read), which is short compared with the shortest phone (J). Morse decoders used 5 ms (Bell, for at most 50 WPM)
  and 2 ms (Mills) ([Bell1977], [Mills1977], read). Adapted answer: a frame step of at most about a quarter of
  the shortest element, that is at most 3 ms for a 12 ms dot at 100 WPM; a 5 ms frame gives only 2.4 frames per
  100 WPM dot (N12) (A).

**(c) Standard methods, with sources.** Matched filter ([Turin1960]); envelope or square-law (energy) detector
for unknown phase ([VanTrees1968], [Proakis2008], [Urkowitz1967]); generalized likelihood ratio test (GLRT: plug in
maximum-likelihood estimates of unknown amplitude and frequency) or Bayesian marginalization ([Kay1998]);
maximum-likelihood single-tone frequency estimation and its Cramér-Rao bound, the smallest variance any unbiased
estimator can reach ([RifeBoorstyn1974]); HMM frequency
line tracking ([StreitBarrett1990]); estimator-correlator likelihood for a Gaussian random signal ([Kailath1969]);
Watterson Gaussian-scatter HF fading model with Rayleigh tap gains and a Gaussian Doppler spectrum, and the ITU-R
test conditions built on it (frequency spread 0.1 Hz mid-latitude quiet, 0.5 Hz moderate, 1 Hz disturbed; 10 Hz
low-latitude disturbed; 10 to 30 Hz high-latitude) ([ITU-R-F1487], read) (all S).

**(d) Main alternatives, and when each is preferred.**

- *Fixed wide filter, envelope detector and adaptive threshold.* Simplest. Its loss is about 10·log₁₀(B·T) for a
  filter of noise bandwidth B wider than 1/T; Bell computed for his own fixed 100 Hz filter a 6 dB loss against the
  optimum at 50 WPM and 10 dB at 20 WPM ([Bell1977], read). Preferred only at high SNR or with very little CPU (C:
  [fldigi]; [Bell1977]; [Mills1977]).
- *Coherent detection with a phase-locked loop.* About 1 dB better per decision at a 1 % error rate (N4), but needs
  phase continuity across gaps and slow fading. Mills found a synchronous phase-locked filter-detector "superior"
  but "very hard to tune (the bandwidth was less than 20 Hz)" and too costly for his computer ([Mills1977], read).
  Preferred at high SNR with slow fading (J).
- *Frame energy detection* (square-law statistic per frame, chi-square likelihoods). Robust to phase and frequency
  jitter; loses the coherent-integration gain within an element. Preferred under fast fading (flutter) and for
  cheap implementations (S: [Urkowitz1967]; J for the preference).
- *Filter bank across frequency feeding a frequency-state HMM.* Preferred at low SNR with drift, when a phase-locked
  loop would lose lock (S: [StreitBarrett1990]).

**(e) What is different about Morse, and how to adapt.**

1. *Unknown, variable pulse length.* No single matched filter fits 5 to 100 WPM. Adaptation (A, J): let the duration
   model do the matched integration. In an HSMM, summing per-frame log-likelihood ratios over a hypothesized
   key-down segment is the matched-filter (integrate-and-dump) statistic for that segment when amplitude and phase
   are known (coherent) or tracked; so a frame-level observation model inside a duration-explicit search performs
   speed-matched filtering automatically, provided frames are short (P1.8) and the frame likelihoods are correct
   (P1.4, P1.6) (J, derived from the definition of the matched filter).
2. *Intermittent observability.* Amplitude, phase and frequency are observable only during key-down; key-up gives
   noise-only samples, which is useful for noise estimation (P3) (J).
3. *Phase continuity across gaps* depends on the transmitter (an oscillator that keeps running versus one that is
   keyed) and on propagation (Doppler); it cannot be assumed (J). This is why the noncoherent answer to P1.1 is the
   safe default, with coherence exploited only within a mark.
4. *Frequency offset.* With the carrier up to 11.7 Hz from the channel center and drifting, frequency must be
   estimated at speeds up to about 40 WPM; at 100 WPM the offset alone costs under 0.3 dB (N3, derived).
5. *Keying shape.* Real transmitters shape the key-down edges (rise and fall over a few milliseconds), so at
   100 WPM a 12 ms dot is far from rectangular; the matched template should be the shaped pulse (J).

**(f) What published Morse decoders did** (details in section 4). Bell: fixed 100 Hz band-pass filter (two digital
resonators), envelope detector, 100 Hz low-pass, 5 ms samples chosen so that noise samples are independent; he
noted the detected noise is "neither zero-mean, gaussian, nor white" yet used Gaussian Kalman filters, and noted
that narrower filters would need a correlated-noise model ([Bell1977], read). Mills: receiver plus an 85 Hz filter
and envelope detector, 500 samples/s, a moving-sum matched filter whose length follows the nominal speed set by the
operator ("performs well over at least an octave in code speeds"), an automatic threshold corrector with a 2 s time
constant and a slicer with hysteresis ([Mills1977], read). RSCW (machine-sent satellite telemetry): mixing to
baseband, a moving average over exactly one bit on I and Q, and FFT-based carrier tracking on 1 s segments twice a
second ([RSCW], read). fldigi: "basically an amplitude demodulator with an automatic threshold adjustment", with an
FFT filter that can be set "matched" to the transmit speed ([fldigi], read). CW Skimmer: "a noise blanker, AGC, and
a sharp, variable-bandwidth CW filter"; no algorithm published ([CWSkimmer], read).

**(g) Where no standard exists.** No source treats the joint problem of a carrier whose amplitude, phase and
frequency are observable only during key-down, with unknown pulse lengths varying 60:1. Nearest standard practice:
per-survivor processing (each surviving hypothesis carries its own channel estimate) from digital receivers
([Raheli1995]) and switching Kalman filters from tracking ([Murphy2002]); adaptation: run the amplitude, phase and
frequency trackers per hypothesis and update them only on hypothesized key-down frames, letting their uncertainty
grow during key-up (A).

**(h) Pitfalls and the standard checks.**

- *Wrong observation density.* A Gaussian model of envelope-detected noise misstates likelihood ratios at low SNR,
  where the Rayleigh/Rician shapes differ most from Gaussian (S: [Proakis2008]); Bell acknowledged the violation
  ([Bell1977], read). Check: histograms or quantile plots of noise-only and signal statistics against the assumed
  densities on synthetic data with known truth (S practice of model checking; J for this application).
- *Overcounting correlated samples* (P1.6) makes posteriors overconfident and forces a compensating text-model
  weight (P6.6). Check: the autocorrelation of noise-only frames, and the calibration of the frame log-likelihood
  ratios against known key states (J).
- *Frequency-offset and drift loss* (N3). Check: CER versus carrier offset and drift rate, with all else fixed (J).
- *Deep fades inside a dash* look like gaps and split elements (J). Check: element-type confusion matrices versus
  fading rate (J).
- *Automatic gain control (AGC) artifacts.* In Bell's field data a weak interfering station was decoded during the
  wanted station's word spaces because the receiver AGC raised the gain ([Bell1977], read); Mills saw the AGC act on
  an interferer outside his narrow filter ([Mills1977], read). Check: tests with neighbors and level steps (P5).
- *Implementation loss unmeasured.* Standard check: compare with a "genie-aided" detector that knows timing,
  frequency and level (the N4 numbers are such a bound for single elements) (C: comparisons against known-channel
  or coherent bounds are routine in communications texts and studies, e.g. [Proakis2008]; record only).

### P2. Unknown and changing timing: speed, Farnsworth spacing, keyer weighting, hand-keying scatter

**(a) Fields and why they apply.** Duration modeling in speech recognition: explicit-duration (semi-Markov) HMMs and
segment models ([Levinson1986], [RussellMoore1985], [Ostendorf1996], [Yu2010], [Murphy2002]). Tempo tracking and
rhythm quantization in music information retrieval ([Cemgil2000], [Raphael2002], [CemgilKappen2003], [Krebs2015],
[Gouyon2006]). Human motor timing, where variability grows in proportion to the interval timed
([WingKristofferson1973], [Gibbon1977]). Tracking of maneuvering targets, for abrupt changes ([BlomBarShalom1988],
[BarShalom2001]). Speaker adaptation, for per-sender style ([GauvainLee1994]). And the Morse literature itself
([Gold1959], [Freimer1959], [Guenther1973], [Bell1977], [Mills1977]). They apply because Morse timing is a
*quantized rhythm*: durations of 1, 3 and 7 units (S: [ITU-R-M1677], read), performed at a slowly varying tempo (the
speed) with motor noise. That is the generative structure of an expressive music performance of a score, and of
phone durations in speech (A).

**(b) Decisions and the standard answer to each.**

- **P2.1 Duration model.** Standard answer: *explicit durations* (a hidden semi-Markov model), not the geometric
  durations implied by a plain HMM's self-loops, p(d) = (1−p)·p^(d−1) (S: [Murphy2002] §2.3.12, read; [Yu2010];
  [Rabiner1989]). Morse-specific evidence: on spectrograms of Morse audio, a nonparametric HMM could not separate the
  "short tone" and "long tone" states; the semi-Markov version could, "only by incorporating duration information"
  ([JohnsonWillsky2010], read) (S).
- **P2.2 How durations are implemented.** Standard answer: a duration counter in the state ("how long we have been
  in that state"), with the leave-probability at each frame given by the hazard h(d) = P(D = d | D ≥ d); cost
  O(T·D·K²) instead of the O(T·D²·K²) of the original explicit-duration algorithm, for T frames, K states and
  maximum duration D (S: [Murphy2002], read). Alternative: chains of sub-states sharing one output density
  (negative-binomial durations) (S: [Murphy2002]). Bell's decoder used the counter-and-hazard form: his key-state
  transition probability is "the probability of extending duration D for one more sample interval", computed as
  P(ρ ≥ D + ε)/P(ρ ≥ D) from the duration density ([Bell1977], read) (S).
- **P2.3 Duration distributions and their scaling with speed.** No standard for Morse. Adapted answer: measure time
  in units of the current dot length T, so the duration model is speed-free (Bell did this, noting that otherwise
  transition probabilities depend on the sample rate; [Bell1977], read), and let the scatter grow in proportion to
  the duration (constant coefficient of variation, for example log-normal or gamma), as in human interval timing (A:
  [Gibbon1977], [WingKristofferson1973]). Morse measurements that support or qualify it: Mills found the standard
  deviation of mark/space durations proportional to their magnitude, σ = 0.15·|x| with |x| the length of the
  (mark, space) duration vector ([Mills1977], read); the simulated hand-keyed files of Howe, which Bell also used,
  had a standard-deviation-to-mean ratio of 0.15 to 0.2 per element ([Bell1977], read); a 1976 study of 55
  hand-keyed samples found speed-normalized densities averaged over operators near Gaussian, but individual
  operators' densities "far from gaussian" (Technology Services Corporation report, as reported in [Bell1977],
  read). Bell chose a Laplacian (two-sided exponential) density per element type, with its spread set so that a dot
  exceeds 2 units with probability 1.35 %, the average error of optimum thresholds in that study ([Bell1977], read).
  The family is a modeling choice (A); its parameters should be measured (P10).
- **P2.4 Representation of speed.** No standard for Morse. Adapted answer, from tempo tracking: speed is a latent
  state with a random-walk prior in log-speed, either continuous (Kalman or particle filter: [Cemgil2000],
  [CemgilKappen2003]) or on a grid of discrete values spaced logarithmically ([Krebs2015], read: tempo states
  distributed logarithmically, "trying to mimic the JNDs of the human auditory system") (A). Bell used a linear grid
  in 2 WPM steps from 10 to 60 WPM ([Bell1977], read). Derived sizes for 5 to 100 WPM (N11): a ratio of 1.05
  between neighbors needs 63 states with a worst-case quantization of ±2.5 % in speed; a ratio of 1.10 needs 33
  states with ±4.9 %. Both are small against a hand-keying scatter of 15 % (J).
- **P2.5 When speed may change.** Adapted answer: only at element boundaries, not at every frame (A). Precedents:
  Krebs et al. "allow transitions to each tempo, but only at beat times" and report more stable tempo tracks
  ([Krebs2015], read); Bell: "the speed is not allowed to change except when the keystate changes" ([Bell1977],
  read).
- **P2.6 Abrupt speed changes.** Standard answer in tracking: a prior that mixes small steps with rare large jumps,
  implemented as switching between motion models (interacting multiple models, IMM) or as explicit changepoints
  (S: [BlomBarShalom1988], [BarShalom2001], [AdamsMacKay2007]); for Morse speed this is (A). Krebs et al. use an
  exponential law in the relative tempo change, f ∝ exp(−λ·|τ_k/τ_(k−1) − 1|), heavier-tailed than a Gaussian
  ([Krebs2015], read). Why a single Gaussian random walk is not enough, derived (N11): with a per-element standard
  deviation of 0.05 in ln(speed), drifting by a factor 2 takes about (ln 2/0.05)² ≈ 190 elements, while the test
  set steps by factors 1.3 to 2.0.
- **P2.7 Style parameters beyond speed.** No standard for Morse. Adapted answer: a few per-station parameters,
  estimated online with priors (maximum a posteriori adaptation, which re-estimates parameters from a speaker's own
  data while pulling them toward a prior, as in speaker adaptation: [GauvainLee1994]) (A):
  - *weighting*, the mark/space balance: a keyer's weighting lengthens marks and shortens spaces by the same
    amount δ at a constant element period, so it is additive (mark = m·T + δ, space = m·T − δ), not a ratio (J);
    the test set has |δ| ≤ 0.1 T;
  - *dash/dot ratio* (Mills's "weight") and the receiver-dependent *mark/space bias* (Mills's "bias", which he notes
    depends on the detector threshold and on multipath) ([Mills1977], read);
  - *gap stretch* for Farnsworth spacing: the ARRL standard sends characters at speed c and adds a total delay
    t_a = (60c − 37.2s)/(s·c) seconds per 50-unit word to reach the overall speed s, split as letter gaps
    t_c = 3t_a/19 and word gaps t_w = 7t_a/19, keeping the 3:7 ratio (S: [Bloom1990], read). Derived (N10): letter
    and word gaps are both stretched by the same factor, t_c/(3u) with u = 1.2/c seconds the unit at the character
    speed, which is 15.7 at c = 33 WPM and s = 5 WPM, matching the brief's "up to about 16". So one extra parameter, a common gap stretch g ≥ 1, describes Farnsworth
    spacing (A, J).
  - Precedent: Mills estimated six separate means (dot, dash; element, letter and word spaces, the spaces
    separately after dots and after dashes) ([Mills1977], read).
- **P2.8 Context dependence of hand-keyed durations.** Adapted answer: condition duration distributions on the
  preceding element and on the keying device, as speech recognition conditions phone models on their neighbors
  (context-dependent units; S in speech: [GalesYoung2008]) (A). Morse evidence: the 1959 MAUDE decoder added a
  fraction of the preceding mark to the following space because operators shorten spaces after dashes ([Mills1977]
  describing [Gold1959], read); Mills estimated spaces after dots and after dashes separately; on mechanical and
  electronic "bugs" the dots and the spaces between them are machine-made and nearly constant while dashes and the
  spaces around them are manual and variable ([Bell1977], [Mills1977], read).

**(c) Standard methods, with sources.** Explicit-duration HMMs and segment models ([Yu2010], [Ostendorf1996],
[Murphy2002]); tempo tracking by Kalman filtering ([Cemgil2000]), by switching state-space models with Monte Carlo
inference ([CemgilKappen2003]) and by discretized "bar-pointer" state spaces decoded with Viterbi ([Krebs2015]);
rhythm parsing with a hybrid discrete-continuous model ([Raphael2002]); multiple-model filtering for abrupt changes
([BlomBarShalom1988]); maximum a posteriori adaptation ([GauvainLee1994]) (S within their fields; A for Morse).

**(d) Main alternatives, and when each is preferred.**

- *Adaptive thresholds on running averages* of mark and space durations (the MAUDE lineage; fldigi's receive-speed
  tracking within a set range around the transmit speed). Cheap; slow after abrupt changes and confused by
  Farnsworth gaps (C: [Gold1959] via [Mills1977]; [fldigi], read).
- *Clustering of measured durations* (histograms, k-means). Works at high SNR with a stable sender (C: [AG1LE2013a],
  read; [Wang2017], abstract).
- *Two-dimensional classification of mark-space pairs*, which captures the dependence of a space on the preceding
  mark (C: [Guenther1973], abstract; [Howe1976], record only; [Mills1977], read).
- *Particle filtering of a continuous speed*, when the posterior over speed is multimodal (for example during a
  change) (S within music: [CemgilKappen2003]).

**(e) What is different about Morse, and how to adapt.**

1. *Gaps carry the segmentation.* Reading a 3T letter gap as a 1T element gap merges two letters into one code;
   reading a 7T word gap as 3T drops a word space. Joint decoding with the code tree and the text model resolves
   many of these (the standard joint-decoding argument of section 2) (A).
2. *Farnsworth spacing* breaks the single-speed assumption by a factor up to about 16 (N10): a decoder with one
   speed would track the slow gaps and misread the fast elements; hence the gap stretch g of P2.7 (J).
3. *Factor-of-3 ambiguity.* A run of dashes at w WPM, with its gaps, looks like a run of dots at w/3 WPM. This is the
   Morse analog of the half- or double-tempo "octave errors" that music tempo evaluations score separately (A:
   [Gouyon2006] (record only); [Krebs2015] counts estimates "at double or half of the ground-truth tempo", read).
   Mills gives a concrete case: after "DE" (both letters end with a dot) some parameters cannot be estimated until
   more text arrives ([Mills1977], read).
4. *Two keying regimes.* Machine keying is nearly exact; hand keying scatters by 15 to 20 % (above). Carrying both a
   low-variance and a high-variance duration model as competing hypotheses, or a per-station scatter parameter, is
   the adaptation (A, J).

**(f) What published Morse decoders did** (details in section 4). Gold's MAUDE (1959): running weighted averages
of marks and spaces, structural checks (for example, more than six marks in a row cannot be one character) and the
space correction above (via [Mills1977], read; [Gold1959] record only). Guenther (1973): pattern classification of
mark-space pairs; "a time duration averaging algorithm" with under 1 % recognition error on noise-free code
([Guenther1973], abstract). Bell (1977): speed as a state on a 2 WPM grid from 10 to 60 WPM; changes only at key
transitions, by 0, ±2 or ±4 WPM after elements and letter gaps and by larger steps after word spaces and pauses, with
probabilities "selected on the basis of speed differences reported by TSC (and on intuitive appeal)"; Laplacian
duration densities ([Bell1977], read). Mills (1977): "ratio-weighted estimation", a shift-register estimator of the
means of pairs of duration classes whose ratios should be near 3:1 and 7:3, with constants "determined empirically
by adjusting them in real time while observing (on the CRT monitor)" ([Mills1977], read). fldigi: receive-speed
tracking within a set range ([fldigi], read). AG1LE (2013): histograms of measured dot, dash and gap durations,
Bayes' rule with equal priors, then a code-book match ([AG1LE2013a], read).

**(g) Where no standard exists.** Farnsworth gap stretch; additive weighting; abrupt speed steps in Morse; per-sender
fist models; the factor-3 ambiguity. Nearest standard practice: tempo tracking with switching models (music),
speaker adaptation (speech), multiple-model filtering (tracking). Adaptation: as in P2.3 to P2.8 (A).

**(h) Pitfalls and the standard checks.**

- *Geometric durations* (a plain HMM) cannot separate short and long tones ([JohnsonWillsky2010], read). Check:
  compare histograms of decoded durations with the model's densities (a posterior predictive check) (J).
- *Factor-3 errors.* Check: speed error measured as a log ratio, with errors near ×3 and ×1/3 counted separately, as
  tempo evaluations count octave errors (A from [Gouyon2006], [Krebs2015]).
- *Tracker inertia versus jitter.* Check: the step response, meaning elements until re-lock and extra character
  errors after a ×1.3 and a ×2 step (J); Bell measured the extra errors caused by an abrupt speed change after
  every tenth letter ([Bell1977], read).
- *Circularity with the synthetic generator.* If the test set's timing generator and the decoder's duration model
  share assumptions (Gaussian scatter, fixed ratios), synthetic scores overstate real-world performance. Check: real
  hand-keyed recordings (J).
- *Time quantization at high speed.* Frame step and speed-grid spacing add timing error that matters most at 80 to
  100 WPM. Check: CER versus frame step at the highest speeds (J).

### P3. Estimating the nuisance parameters: amplitude, noise level, frequency offset and drift, speed

**(a) Fields and why they apply.** Synchronization and channel estimation in digital receivers ([Mengali1997],
[Meyr1998]); estimation theory ([Kay1993], [VanTrees1968]); target tracking ([BarShalom2001]); noise estimation in
speech enhancement ([Martin2001]); radar detection with constant false-alarm rate (CFAR) ([Rohling1983]); adaptation
in speech recognition ([GauvainLee1994]); the expectation-maximization (EM) algorithm, an iterative
maximum-likelihood method for models with hidden variables ([Dempster1977]). They apply
because amplitude, carrier frequency, symbol timing (here, speed) and noise level are exactly the "synchronization
parameters" of a digital receiver, and that literature's taxonomy fits: *data-aided* (using known symbols),
*decision-directed* (using the receiver's own tentative decisions) or *non-data-aided* (using neither);
*feedforward* (estimate from a block of signal, then use) or *feedback* (a tracking loop) (S: [Mengali1997],
[Meyr1998], record only).

**(b) Decisions and the standard answer to each.**

- **P3.1 Joint or separate estimation.** Standard answer: non-data-aided feedforward estimates for *acquisition*,
  then decision-directed or joint estimates for *tracking*; when tentative decisions are unreliable (low SNR) and
  the parameters change within the decision delay, estimate jointly with the sequence, either by per-survivor
  processing (each surviving hypothesis carries and updates its own parameter estimates) or by putting the
  parameters into the state (S: [Meyr1998]; [Mengali1997]; [Raheli1995]). Batch alternative: EM over a buffer
  (S: [Dempster1977]).
- **P3.2 Noise level.** Standard answer: estimate the noise power inside the decoder from signal-free data, robustly:
  minimum statistics (track the minimum of the smoothed power over a window, then correct its known downward bias)
  (S: [Martin2001]) or order statistics across neighboring frequency cells (ordered-statistic CFAR, robust to other
  signals in the reference cells) (S: [Rohling1983]). For Morse, key-up intervals are signal-free by definition, so a
  decision-directed noise estimate from hypothesized key-up frames is available (A). Derived precision (N9): the
  relative standard deviation of a power estimate averaged over bandwidth B and time τ is 1/√(B·τ): 7.1 % (0.30 dB
  relative to the true noise power) for B = 200 Hz and τ = 1 s, but 22 % (0.88 dB relative to the true noise power)
  for B = 20 Hz and τ = 1 s (S formula; derived values). So a
  noise estimate taken across most of the channel's width reaches a given precision about 10 times sooner than one
  taken in a 20 Hz matched bandwidth (equivalently, it is about 3 times more precise for the same averaging time),
  provided the carrier and any neighbors are excluded from it (J).
- **P3.3 Amplitude (signal level).** Standard answer: a Kalman filter on the (complex or real) gain with a
  Gauss-Markov model whose correlation time comes from the Doppler spread (P1.5), updated only on hypothesized
  key-down frames and run per hypothesis (S: [BarShalom2001]; [Baddour2005]; Morse precedent [Bell1977], read). The
  detector's 1 s average power P̄ includes key-up time; for PARIS text the key is down 22 of 50 units (duty 0.44, N10),
  so the key-down power prior is P_kd ≈ (P̄ − P_noise)/0.44, with a wide spread because the duty over a single
  second at 5 WPM (about 4 units) varies greatly (derived; J for the spread).
- **P3.4 Frequency and drift.** Standard answer: acquire with the periodogram peak (S: [RifeBoorstyn1974]); track
  frequency and drift rate with a Kalman filter or phase-locked loop at moderate SNR (S: [Meyr1998],
  [BarShalom2001]) or with an HMM over frequency cells at low SNR (S: [StreitBarrett1990]); in Morse, update only on
  key-down (A). How much one mark tells, derived from the Cramér-Rao bound (the smallest variance any unbiased
  estimator can reach) for a tone in white noise, std(f̂) ≥ √6 / (2π·T_m·√(E/N₀)) for a mark of duration T_m (S: [RifeBoorstyn1974]; values N8): at 20 WPM and
  S₅₀₀ = 0 dB a dot gives at best 1.2 Hz and a dash 0.23 Hz; at S₅₀₀ = −10 dB a dot gives 3.8 Hz, and at that
  E/N₀ (4.8 dB) the maximum-likelihood estimator is near its threshold, where outliers appear (S:
  [RifeBoorstyn1974]). So near the sensitivity limit frequency must be integrated over many marks, which is what a
  tracker with a drift model does (J).
- **P3.5 Speed.** For a model-based decoder the standard answer is joint estimation inside the state (P2.4), as in
  joint tempo and meter tracking (A: [Krebs2015]); separate estimation from duration histograms or clusters is the
  common cheap alternative (C: [AG1LE2013a]; [Wang2017]).
- **P3.6 Starting cold when the channel opens.** Adapted answer: broad priors informed by the detector (level,
  frequency), then re-decode the opening once the estimates have converged, which the brief's 20 s correction
  window allows. Nearest standards: multi-pass decoding with adaptation between passes in speech recognition
  ([GalesYoung2008] §7.1, read: the first-pass output "is used to perform a first round of adaptation", and the
  adapted models then generate lattices for further passes) and fixed-lag smoothing,
  "optimal filtering, where a constant delay is tolerated" ([Sarkka2013], read) (A).
- **P3.7 How fast each parameter may change (process noise).** Standard answer: derive the process-noise level of
  each tracker from the physics (Doppler spread for the gain, drift rate for frequency, observed speed changes for
  speed), then check filter consistency with the normalized estimation-error and innovation tests (NEES, NIS: the
  squared errors normalized by the filter's own covariance should follow chi-square laws) (S: [BarShalom2001],
  record only).
- **P3.8 Plug-in or marginalize.** Both are standard (S: [Kay1998]: the generalized likelihood ratio test plugs in
  maximum-likelihood estimates; the Bayesian approach integrates the unknowns out). For a decoder whose probabilities
  must be calibrated, marginalizing (or at least carrying the estimate's variance into the likelihood, as a Kalman
  predictive density does) is preferable, because plug-in estimates ignore parameter uncertainty and make posteriors
  overconfident (J).

**(c) Standard methods, with sources.** Synchronizer design ([Mengali1997], [Meyr1998]); per-survivor processing
([Raheli1995]); Kalman filtering and its consistency tests ([BarShalom2001]); minimum statistics ([Martin2001]);
ordered-statistic CFAR ([Rohling1983]); periodogram frequency estimation and its Cramér-Rao bound
([RifeBoorstyn1974]); HMM frequency tracking ([StreitBarrett1990]); EM ([Dempster1977]); fixed-lag smoothing
([Sarkka2013]) (all S).

**(d) Main alternatives, and when each is preferred.** Decision-directed estimation: cheap and accurate at high SNR,
but errors feed back into the estimates ("error propagation"); preferred above the SNR where tentative decisions are
reliable (S: [Meyr1998]). Per-survivor or state-augmented (joint) estimation: robust at low SNR, costs one estimator
per hypothesis; preferred near the sensitivity limit (S: [Raheli1995]). Batch EM over a buffer: best estimates but
adds delay; preferred for re-decoding a buffered opening (J). Separate pre-estimation of speed from durations:
cheapest; fails when the durations themselves are uncertain (low SNR) or bimodal (Farnsworth) (J).

**(e) What is different about Morse, and how to adapt.**

1. *Intermittent observability.* Amplitude and frequency are observed only during key-down. During gaps the
   trackers must predict without updates, and their variances grow; after a long gap (at 5 WPM a word space is
   1.68 s; with Farnsworth spacing up to 4.0 s, N12) the trackers are nearly re-acquiring (A, J).
2. *Speed is a timing parameter*, observable only through several element durations, not from any single frame (J).
3. *Parameters jump at a change of station* (P7) (J).
4. *The first word matters* (often a callsign), and it arrives while the estimates are converging; hence P3.6 (J).

**(f) What published Morse decoders did.** Bell: amplitude by a Kalman filter per surviving path (joint); noise by
the minimum of the envelope over 1.2 s, averaged and multiplied by a scale "selected empirically" (an uncompensated
minimum-statistics estimate); he reported the decoder "relatively insensitive" to the noise estimate within "a
rather large range"; speed joint (in the state); frequency not estimated (fixed 100 Hz filter) ([Bell1977], read).
Mills: separate duration-mean estimation (ratio-weighted estimation) and an automatic threshold with a 2 s time
constant ([Mills1977], read). RSCW: separate FFT-based frequency tracking ([RSCW], read). fldigi: threshold and
speed tracking ([fldigi], read).

**(g) Where no standard exists.** Synchronization with intermittent observability (parameters seen only during
key-down, by an amount depending on the hypothesized key state). Nearest standard: per-survivor processing and
switching Kalman filters; adaptation: update each hypothesis's trackers only on its own key-down frames and inflate
their covariance by the process noise over each gap (A).

**(h) Pitfalls and the standard checks.**

- *Error propagation and false lock* in decision-directed loops (S: [Meyr1998], record only). Check: acquisition and
  re-acquisition tests at low SNR, counting losses of lock (S practice).
- *Biased noise estimates.* The minimum of N independent exponentially distributed power samples has mean 1/N of the
  true mean, so an uncorrected minimum is badly biased low; minimum statistics corrects this bias analytically
  (S: [Martin2001], record only; the 1/N result is elementary). Bell's empirically scaled minimum is the uncorrected
  form with a fitted constant ([Bell1977], read). Check: estimated versus true noise level on synthetic data across
  speeds and duty cycles (J).
- *Inconsistent filters* (covariances too small) give overconfident likelihoods. Check: NEES and NIS
  (S: [BarShalom2001]).
- *Unidentifiable parameters early on*: amplitude versus noise level before any key-up has been seen; speed versus
  its factor-3 alias (P2). Check: behavior on the first word of each transmission (J).

### P4. The inference framework: model class, algorithm, what to track, approximations, streaming decisions

**(a) Fields and why they apply.** Speech-recognition decoding: Viterbi beam search, token passing, weighted
finite-state transducers, lattices ([GalesYoung2008], [NeyOrtmanns2000], [Steinbiss1994], [Young1989],
[Mohri2002], [Kaldi]). Digital communications: Viterbi and BCJR decoding, the M-algorithm, per-survivor processing
([Forney1973], [BCJR1974], [AndersonMohan1984], [Raheli1995]). Bayesian filtering of hybrid discrete-continuous
states: switching Kalman filters and their approximations, particle filters ([Murphy2002], [BlomBarShalom1988],
[ChenLiu2000], [Doucet2000], [Arulampalam2002], [Sarkka2013]). Music beat tracking with discretized state spaces
([Krebs2015]). They apply because the decoder must find the most probable path through a large hybrid state space,
in real time, with bounded delay: the core problem of all three fields (S).

**(b) Decisions and the standard answer to each.**

- **P4.1 Model class.** Standard answer: a switching state-space model, that is, a dynamic Bayesian network whose
  discrete states select the dynamics and observation laws of conditionally linear-Gaussian continuous states
  (S: [Murphy2002]; [ChenLiu2000]). Bell's decoder is exactly this, with a Kalman filter per path ([Bell1977], read).
- **P4.2 What the state must keep track of.** Standard answer: every variable that the model's factors condition
  on, so that the process is Markov in the augmented state (S: [Murphy2002]). For this model (J): the text-model
  history (the last n−1 characters), the position in the Morse code tree, the current element or gap type
  (including "no signal"), the time already spent in it (duration counter), the speed (and any discrete style
  parameters), and per hypothesis the continuous estimates of gain and frequency with their covariances. Bell's
  joint state was key state, element, letter and speed, plus "memory functions" holding the samples since the last
  key transition and the previous symbols, plus a Kalman filter per path ([Bell1977], read).
- **P4.3 Exact or approximate inference over the continuous part.** Exact inference is intractable: the belief
  state of a switching Kalman model is a Gaussian mixture whose size grows as O(Kᵗ) (S: [Murphy2002] §4.3, read; Bell's
  "exponentially expanding set of linear Kalman filters", read). Standard approximations: *keep the best* (one filter
  per surviving discrete hypothesis: per-survivor processing) (S: [Raheli1995]); *merge* (collapse the mixture to
  one Gaussian per discrete state by moment matching: the generalized pseudo-Bayesian algorithms GPB(n), and the
  cheaper interacting multiple model algorithm, IMM) (S: [Murphy2002], read; [BlomBarShalom1988]); or *sample*
  (Rao-Blackwellized particle filtering, the "mixture Kalman filter") (S: [ChenLiu2000]; [Doucet2000]).
- **P4.4 Search algorithm.** Standard answer: time-synchronous Viterbi search with recombination of hypotheses that
  reach the same discrete state (only the better one survives), implemented as token passing, in which each
  hypothesis is a token carrying its score and history from state to state (S: [GalesYoung2008] eq. 2.21, read;
  [Young1989]; [NeyOrtmanns2000]).
- **P4.5 Pruning.** Standard answer: *beam pruning* (drop hypotheses whose log score is more than a fixed margin
  below the current best) plus *histogram pruning* (keep at most a fixed number of the best) (S: [Steinbiss1994];
  [NeyOrtmanns2000]). Kaldi's documentation: "max-active ... controls the maximum number of states that can be
  active at one time", and "the effective beam width may differ from 'beam' if the 'max_active' constraint is
  limiting" ([Kaldi], read) (S). The coding-theory counterpart is the M-algorithm, which keeps the M best paths
  (S: [AndersonMohan1984]). Bell used *probability-mass pruning*: keep paths in decreasing posterior order until
  their total reaches P_opt (0.9 adopted; other values up to 0.98 tested, the table being partly illegible in the
  scanned copy), capped at M_max paths, keeping at least one path per element state; "the 90% optimal estimator with
  a decode delay of 200 (1 second) is very nearly as good [as] the 98% optimal decoder" ([Bell1977], read; one
  published precedent, J for whether it generalizes). Settings are tuned on development data against accuracy and
  CPU (S: [NeyOrtmanns2000]; C: [Kaldi]).
- **P4.6 Decoding objective.** Standard answers: the most probable state path (Viterbi) for a transcript (S:
  [GalesYoung2008]); minimum expected error (minimum Bayes risk) decoding when the error metric is what matters,
  implemented with posteriors on a lattice or confusion network (S: [GoelByrne2000]; [Mangu2000], whose consensus
  decoding minimizes word error). For a character-error metric, the adapted answer is consensus over characters (A).
- **P4.7 Numerics.** Standard answer: scores in the log domain, with log-sum-exp for sums, or scaled forward
  variables (S: [Rabiner1989], record only).
- **P4.8 Streaming decisions.** Standard answer: *partial traceback*, emitting the part of the best path that all
  surviving hypotheses share (S: [Brown1982], record only), with a maximum delay at which the best path's decision is
  forced; equivalently, *fixed-lag smoothing*, "optimal filtering, where a constant delay is tolerated in the
  estimates", with digital demodulation as a named application (S: [Sarkka2013] §10.4, read). Morse precedents:
  Bell decoded "by finding the one node, at sufficient delay, from which all successor paths originate", and if none
  existed within 200 samples (1 s) took the node on the best path at that delay and deleted the rest ([Bell1977],
  read); RSCW outputs once "all remaining messages have the same initial set of dibits" ([RSCW], read).
- **P4.9 Determinism.** No textbook rule for decoders. Judgment, from general software practice: no unseeded
  randomness (a fixed seed if a particle filter is used), deterministic tie-breaking when ranking equal scores, a
  fixed order of floating-point reductions (no reassociation by compiler options or by thread scheduling), and the
  same build for benchmark runs (J). The floating-point standard defines the conditions under which results are
  reproducible (S: [IEEE754], clause 11, record only).
- **P4.10 Single pass or several.** Standard in speech: a fast first pass, then rescoring of lattices with richer
  models or adapted parameters (S: [GalesYoung2008] §2.4 and §7.1, read). Adapted answer for a streaming decoder: a
  single integrated pass with bounded lag, plus optional re-decoding inside the correction window (A).

**(c) Standard methods, with sources.** Viterbi and token passing ([Forney1973], [Young1989]); beam and histogram
pruning ([Steinbiss1994], [NeyOrtmanns2000], [Kaldi]); GPB and IMM ([Murphy2002], [BlomBarShalom1988]); per-survivor
processing ([Raheli1995]); mixture Kalman and Rao-Blackwellized particle filters ([ChenLiu2000], [Doucet2000]);
partial traceback ([Brown1982]); fixed-lag smoothing ([Sarkka2013]); minimum-Bayes-risk and consensus decoding
([GoelByrne2000], [Mangu2000]) (all S).

**(d) Main alternatives, and when each is preferred.**

- *Forward (sum) beam search instead of Viterbi (max):* gives posteriors directly; preferred when calibrated
  probabilities are the product (P8) (S: [BCJR1974]; J for the preference).
- *Stack (best-first) decoding:* efficient but "their run-time search characteristics can be difficult to control"
  ([GalesYoung2008], read); not preferred for a fixed real-time budget (S).
- *Particle filtering:* preferred when continuous states are strongly nonlinear or multimodal (frequency acquisition,
  speed during a change); needs a fixed seed for determinism and many particles at low SNR (S: [Arulampalam2002]; J
  for the Morse use).

**(e) What is different about Morse, and how to adapt.**

1. *Very long durations.* A Farnsworth word gap can last 4.0 s, which is 800 frames at 5 ms or 4000 at 1 ms (N12).
   A naive explicit-duration recursion costs O(D) per state per frame; the counter-and-hazard form keeps it O(1) per
   hypothesis per frame, as in Bell's decoder (S: [Murphy2002]; derived numbers).
2. *Uneven search effort.* Bell observed that between key transitions most paths carry negligible probability, but
   "at the instant of a keystate transition, however, the probabilities become more uniform and it is necessary to
   save all the M_max paths" ([Bell1977], read). The adaptation is a beam that widens automatically at boundaries,
   which beam pruning already does (S) and probability-mass pruning also does (J).
3. *Continuous state per hypothesis.* When two hypotheses reach the same discrete state, Viterbi recombination
   must keep one filter (per-survivor processing) or merge the two (GPB); both are standard (S).

**(f) What published Morse decoders did.** Bell: trellis of (key state, element, letter, speed) with a Kalman filter
per path, probability-mass pruning, decisions at the paths' common ancestor or forced at 1 s, a 5 ms frame; on a
PDP-10 "one minute of data requires approximately 20 minutes of processing time" ([Bell1977], read). Mills: Viterbi
over a Morse code tree whose input symbols are measured mark-space pairs, with a-priori transition probabilities,
"continuous and self-synchronizing", in real time on an LSI-11 ([Mills1977], read). RSCW: a tree search over dibits
for machine-sent code ([RSCW], read). AG1LE: a C implementation of Bell's algorithms integrated into fldigi
([AG1LE2013b], read). CW Skimmer: unpublished ([CWSkimmer], read).

**(g) Where no standard exists.** No source combines all of these for Morse; every piece is standard in its own
field. The combination is the adaptation (A).

**(h) Pitfalls and the standard checks.**

- *Search errors versus model errors.* Standard diagnostic: score the reference transcription's best alignment
  under the same model; if it scores higher than the decoder's output, the error is a search (pruning) error,
  otherwise a model error; and sweep the beam to plot CER against CPU time (C: speech-recognition practice,
  [NeyOrtmanns2000], record only).
- *Max is not sum.* Viterbi path scores and probability-mass-pruned posteriors leave out the probability of pruned
  paths and overstate confidence (J; consequences in P8).
- *Numerical underflow* without log-domain arithmetic or scaling (S: [Rabiner1989]).
- *Nondeterminism* from thread scheduling, unstable sorting or compiler reassociation. Check: bitwise-identical
  output across repeated runs, thread counts and machines (J).

### P5. Where transmissions start and stop; silence; non-Morse signals; impulsive noise; other signals in the channel

**(a) Fields and why they apply.** Voice activity detection and endpointing ([Sohn1999]); silence and noise models
in speech recognizers ([GalesYoung2008]); keyword spotting with "filler" (garbage) models that absorb everything that
is not a keyword ([RosePaul1990]); impulsive-noise statistics and robust detection ([Middleton1977], [Huber1964],
[Kassam1988], [Zhidkov2008]); multiuser detection and the separation of overlapping sources ([Verdu1998],
[GhahramaniJordan1997]); window design for selectivity ([Harris1978]). They apply because the decoder must decide,
at every moment, whether its input is Morse from the wanted station, nothing, something else, or another station:
the same "what is this segment?" question that voice activity detection and keyword spotting answer with competing
models in one search (A).

**(b) Decisions and the standard answer to each.**

- **P5.1 "No signal" as a hypothesis.** Standard answer: an explicit background (silence/noise) model competing in
  the same search, with its own duration behavior; decide signal presence by a likelihood ratio smoothed by an HMM
  "hang-over" (a hold time) so that brief dips do not end a segment (S: [Sohn1999], record only; silence models:
  [GalesYoung2008], record only). Emit nothing while the background hypothesis dominates (S).
- **P5.2 Non-Morse keying** (tuning carriers, random or idle keying). Standard answer in keyword spotting: explicit
  filler models with a tuned entry penalty that trades misses against false alarms (S: [RosePaul1990], record only).
  Adapted answer: a "carrier" state for key-down far longer than any dash, and an unconstrained keying loop that
  explains valid timing which spells nothing (A).
- **P5.3 Start and end of a transmission.** Adapted answer: transitions between the background model and the Morse
  model with minimum durations; the end of a transmission is a gap much longer than a word space, decided by the
  posterior of the background model rather than by a fixed timeout (A, from [Sohn1999]). Bell's model had a
  separate "pause" symbol of 14 units ([Bell1977], read).
- **P5.4 Impulsive noise (static crashes).** Standard answers: a heavy-tailed noise model in the likelihood
  (Middleton's Class A model, a Poisson-weighted mixture of Gaussians of increasing variance; or a two-component
  Gaussian mixture, the "ε-contaminated" model of robust statistics)
  so that one large sample cannot dominate the decision (S: [Middleton1977], [Huber1964], [Kassam1988]), and/or a
  blanking or clipping nonlinearity before detection (S: [Zhidkov2008]). Adapted answer for Morse: also give the
  duration model a short "impulse" event, so that a crash is explained as an impulse rather than as a faded dot (A,
  J).
- **P5.5 Other signals in the channel.** Three standard answers, in increasing cost: (i) *selectivity*: a narrow,
  tapered (windowed) matched filter on the tracked frequency, trading a little SNR for low sidelobes (S:
  [Harris1978]: a Hann window's highest sidelobe is −32 dB relative to the main lobe, at an equivalent noise
  bandwidth 1.5 times that of the rectangular window, i.e. 1.76 dB of SNR, record only); (ii) a *joint model* of the
  two keyed carriers (a factorial HMM, whose hidden state is the pair of the two signals' states) (S:
  [GhahramaniJordan1997]); (iii) *cancellation* of the interferer using its own decoded signal (successive
  interference cancellation) (S: [Verdu1998], record only). Which is needed depends on the leakage (derived below).
  Adapted answer for a skimmer: the neighbor has its own channel and decoder, so its key-state posterior and level
  can be shared with the wanted channel's decoder to explain or cancel its leakage (A, J).
- **P5.6 Automatic gain control.** Judgment: keep any AGC out of the decoder's input path and model the level
  explicitly (P3.3) (J). Evidence: both 1977 decoders were hurt by receiver AGC, in Bell's case making a weak
  interferer decodable during the wanted station's word spaces ([Bell1977], [Mills1977], read: evidence from two
  published decoders).

**Derived leakage of a neighbor through a matched filter** (N7; S formula, derived values). An integrate-and-dump
over T passes a carrier Δf away with gain |sinc(Δf·T)|², whose sidelobes follow the envelope 1/(π·Δf·T)². The
envelope, in dB relative to the passband:

| Speed | Δf = 30 Hz | Δf = 60 Hz | Δf = 120 Hz |
|---|---|---|---|
| 5 WPM (T = 240 ms) | −27.1 | −33.1 | −39.1 |
| 20 WPM (T = 60 ms) | −15.0 | −21.1 | −27.1 |
| 60 WPM (T = 20 ms) | −5.5 | −11.5 | −17.5 |
| 100 WPM (T = 12 ms) | −1.1 | −7.1 | −13.1 |

A neighbor 12 dB stronger than the wanted station therefore leaks at about −15 dB relative to the wanted
station's key-down power at 20 WPM and 120 Hz spacing, but at about −1 dB at 100 WPM and 120 Hz, and at about
+5 dB at 100 WPM and 60 Hz. At high speeds or close spacing, filtering alone cannot separate the stations, and the
joint model or cancellation of P5.5 is needed (J, from the derived values). A tapered filter lowers far sidelobes,
not the main-lobe overlap that dominates at 100 WPM (S: [Harris1978]).

**(c) Standard methods, with sources.** Likelihood-ratio voice activity detection with HMM hang-over ([Sohn1999]);
filler models ([RosePaul1990]); Class A and ε-contaminated noise models and robust detectors ([Middleton1977],
[Huber1964], [Kassam1988]); blanking ([Zhidkov2008]); factorial HMMs ([GhahramaniJordan1997]); multiuser detection
and interference cancellation ([Verdu1998]); windows ([Harris1978]) (all S).

**(d) Main alternatives, and when each is preferred.** A squelch or energy threshold in front of the decoder:
cheap, but it throws away weak signals that the joint model could decode; preferred only when CPU is very short
(C: a squelch is a usual feature of amateur decoders, e.g. [fldigi], whose detector has attack and decay "squelch
parameters", read). Blanking before detection versus a heavy-tailed
likelihood: blanking is cheaper and standard in receivers; the likelihood approach keeps the information in samples
near a crash and is preferable when impulses are frequent (J). Filtering versus joint modeling of a neighbor: per
the table above (J).

**(e) What is different about Morse, and how to adapt.**

1. *A tuning carrier is a key-down of seconds*, which the duration model sees as an impossibly long dash; without a
   carrier state the decoder must output something (typically T) (J).
2. *Random keying has valid Morse timing* but spells nothing; only the code tree and the text model can reject it,
   and a strong text model may instead "hear" words in it (J; see P6).
3. *A static crash* lasts a few milliseconds, shorter than a dot at most speeds (60 ms at 20 WPM, 12 ms at 100 WPM),
   so the duration model alone rejects many crashes. Bell: "the algorithm successfully rejects many of the shorter
   spurious marks because they are inconsistent with the speed of transmission", but "enough are accepted as valid
   marks to cause the error rate to be high" ([Bell1977], read; one published observation).
4. *The interferer is usually another Morse signal* with the same statistics, separable only by frequency, level and
   fist; in the wanted station's gaps the decoder may follow the neighbor (J; Bell's field sample 5, read).

**(f) What published Morse decoders did.** Bell: a "pause" symbol; static bursts accepted as faded marks; a weak
interferer decoded during word spaces through AGC action ([Bell1977], read). Mills: hysteresis in the slicer "to
attenuate short noise bursts, such as produced during long key-up intervals when the receiver automatic gain
control recovers", truncation of "excessively long intervals" (continuous key-down or key-up), and AGC acting on an
interferer outside the 85 Hz filter ([Mills1977], read). fldigi: impulse noise "can cause the filter to ring";
attack and decay squelch settings ([fldigi], read). CW Skimmer: a noise blanker ([CWSkimmer], read). The code
itself contains an "error" signal of eight dots, which a decoder must accept as a symbol ([ITU-R-M1677], read).

**(g) Where no standard exists.** Rejection of non-Morse keying, separation of co-channel Morse stations, and
impulse handling inside a Morse duration model. Nearest standard practice: filler models in keyword spotting,
factorial HMMs and multiuser detection, heavy-tailed likelihoods; adaptations as in P5.2, P5.4 and P5.5 (A).

**(h) Pitfalls and the standard checks.**

- *Filler models mis-set:* too permissive and real Morse is absorbed (deletions); too strict and noise becomes text
  (insertions, typically runs of E, T and I) (J). Standard check from keyword spotting: false alarms per hour against
  detection rate as the filler penalty varies (S: [RosePaul1990], record only); here, characters emitted per hour on
  noise-only, carrier-only, random-keying and neighbor-only inputs (A).
- *Blanking that removes signal* at high SNR or high impulse rates. Check: CER against impulse rate and amplitude,
  with and without the blanker (J).
- *Following the neighbor.* Check: count characters that belong to the neighbor's text in the wanted channel's
  output, across the test set's offset and level grid (J).

### P6. The text model: probabilities of letters and words for mixed content, its weight against the signal, its estimation

**(a) Fields and why they apply.** Statistical language modeling: n-gram models (the probability of each symbol
given the n−1 before it), smoothing, interpolation, class-based and cache models, adaptation, evaluation by
perplexity (the inverse of the geometric-mean probability that the model gives each symbol of held-out text)
([Jelinek1997], [ChenGoodman1999], [KneserNey1995], [WittenBell1991], [JelinekMercer1980], [Brown1992],
[KuhnDeMori1990], [IyerOstendorf1999], [Bellegarda2004]). Handwriting and character recognition, where open
vocabularies are handled by character-level models integrated into HMM decoding ([PlotzFink2009], [MartiBunke2001]).
Text compression, whose adaptive context models face the same "zero-frequency" problem of unseen symbols
([ClearyWitten1984], [WittenBell1991]). They apply because the decoder's text prior is a language model, and Morse
traffic is an open vocabulary: callsigns, serial numbers and abbreviations are not words of any dictionary (A).

**(b) Decisions and the standard answer to each.**

- **P6.1 Modeling unit.** Adapted answer: characters as the base unit (an open-vocabulary character n-gram), with
  optional classes for whole tokens (callsigns, numbers, common abbreviations) (A, from character-level models in
  recognition of open-vocabulary text: [PlotzFink2009], record only).
- **P6.2 Smoothing and order.** Standard answer: interpolated, modified Kneser-Ney smoothing (smoothing spreads
  probability onto sequences never seen in training; Kneser-Ney does so in proportion to the number of different
  contexts a symbol has been seen in); Chen and Goodman's
  comparison introduced "a novel variation of Kneser-Ney smoothing that consistently outperforms all other
  algorithms evaluated" (S: [ChenGoodman1999], abstract; [KneserNey1995]); Witten-Bell (escape) estimation is the
  standard alternative for adaptive character models (S: [WittenBell1991]); the order is chosen by perplexity on
  held-out text (S: [Jelinek1997], [ChenGoodman1999]).
- **P6.3 Mixed content.** Standard answer: a mixture of component models (for example ragchew English with
  abbreviations and Q-codes; contest exchanges; calls and sign-offs), linearly interpolated with weights estimated by
  EM on held-out text (S: [JelinekMercer1980], record only; [IyerOstendorf1999]); weights may be re-estimated online
  from the recent decoded text, which is standard LM adaptation (S: [Bellegarda2004], record only). Applying it to
  amateur traffic, with its abrupt switches between formats within one contact, is (A).
- **P6.4 Callsigns without lists.** Adapted answer: a *class-based* model in which a "callsign" token in the text
  model expands into a sub-model of callsign *format*, not of particular calls (S for class-based n-grams:
  [Brown1992]; for expanding classes into sub-grammars in a compiled network: [Mohri2002]; A for this use). The
  format is set by the ITU Radio Regulations, Article 19 (No. 19.68): amateur call signs are one or two characters
  and a single digit, followed by a group of not more than four characters, the last of which is a letter
  (S: [ITU-RR19], via a secondary source; special and modern forms extend this).
- **P6.5 Repetition within a contact.** Adapted answer: a *cache* component that raises the probability of tokens seen
  recently in the same channel, since callsigns, names and reports repeat within a contact (S for cache models:
  [KuhnDeMori1990]; [IyerOstendorf1999]; A for this use).
- **P6.6 Weight of the text model against the signal.** Standard answer: a language-model scale factor and an
  insertion penalty, tuned on development data. Gales and Young: "the language model is often scaled by an
  empirically determined constant and a word insertion penalty is added", the scale "typically in the range 8-20"
  and the penalty "typically in the range 0 to −20" (S: [GalesYoung2008] §2, read). The same survey gives the
  reason when it computes posteriors for training: "as a consequence of the conditional independence assumptions,
  the posterior probabilities computed using the HMM likelihoods tend to have a very large dynamic range and
  typically one of the hypotheses dominates", so the acoustic likelihoods are raised to a power set "to the inverse
  of the language model scale-factor" ([GalesYoung2008] §4.2, read). In a correctly specified generative model the
  scale would be 1 (S, by Bayes' rule). Derived (N6): if the observation model treated the
  1500 samples/s as independent, it would overstate log-likelihoods by about 6, and a compensating scale near 6
  would be expected; correcting the observation model first (P1.6) and tuning the residual scale on development
  data is the cleaner order (A, J).
- **P6.7 Estimation and evaluation.** Standard answer: maximum-likelihood counts from in-domain text, smoothed, with
  held-out perplexity (or bits per character) as the intrinsic measure (S: [ChenGoodman1999], [Jelinek1997]), plus
  the end-to-end error rate as a function of the scale factor (S: [GalesYoung2008]). When in-domain text is scarce:
  interpolate with general-domain text and with synthetic text generated from templates of the known formats (A, J).
- **P6.8 Symbol inventory.** Judgment: the word space as a symbol of the model and each prosign as one symbol,
  consistent with the brief's error metric (J).

**(c) Standard methods, with sources.** N-gram estimation with modified Kneser-Ney or Witten-Bell smoothing
([ChenGoodman1999], [KneserNey1995], [WittenBell1991]); deleted interpolation and EM-estimated mixtures
([JelinekMercer1980], [IyerOstendorf1999]); class-based models ([Brown1992]); cache models ([KuhnDeMori1990]);
adaptation ([Bellegarda2004]); scale factor and insertion penalty ([GalesYoung2008]) (all S).

**(d) Main alternatives, and when each is preferred.** Word-level models with a fixed vocabulary: best for running
English, poor for callsigns and numbers (S: open-vocabulary problem; J for this domain). Neural language models
(recurrent or transformer, over characters): lower perplexity, much higher CPU per hypothesis per frame; preferred
in a rescoring pass rather than in the first-pass search (J). Dictionary post-correction of the decoded text:
cheap; cannot repair what the search already discarded (J; proposed for Morse by Mills, below). A uniform text
model (independent, equally likely letters): the baseline Bell actually coded, and the right model for measuring
the signal-only floor on code-group tests ([Bell1977], [Mills1977], read).

**(e) What is different about Morse, and how to adapt.**

1. *Little in-domain text* exists for training (J); hence P6.7's interpolation and synthetic text.
2. *Abrupt content switches* within a contact (a call, an exchange, conversation) make a dynamic mixture more
   appropriate than one static model (A).
3. *Callsigns are high-entropy, structured strings*: the text model should know their format and be nearly
   uninformative inside them (J).
4. *Segmentation errors dominate.* Mills: the common errors are those "where Morse characters get concatenated or
   amputated due to mangled element/word spaces" ([Mills1977], read). A character model that includes the word
   space as a symbol, integrated in the search, scores these alternatives directly (A).
5. *The no-list rule has a subtle leak* (J): a high-order character n-gram trained on many real callsigns memorizes
   the frequent ones, which is a callsign list in disguise. Mitigations: train the callsign class on strings
   generated from the format rules and prefix statistics, or cap its order (J).

**(f) What published Morse decoders did.** Gold (1959), as quoted by Bell: "for the automatic reception of a
language encoded by even a simple process like Morse code, a machine must have some knowledge of the language if it
is to approximate the performance of a man" ([Bell1977], read). McElwain and Evens (1962): "the degarbler", a
program for correcting machine-read Morse code ([McElwain1962], record only). Bell (1977): first- and second-order
Markov models of the *element* sequence were tested, and the text model actually coded was independent, equally
likely letters; he wrote that "the largest payoff in terms of increase performance is to be found in more
sophisticated models", that text types vary ("call-up, response, chatter, formatted messages, plain language
messages, code groups"), and pointed to speech recognition's "linguistic statistical decoder", to syntactic
decoding of formats and to dictionary look-up ([Bell1977], read). Mills (1977): a-priori letter probabilities from
English text, noting that Morse traffic differs because of "service abbreviations, such as the 'Q' signals" and
patois such as "TU", or alternatively probabilities inversely proportional to the code length; he proposed digram
probabilities and post-decoding dictionary correction with the Levenshtein metric ([Mills1977], read;
[Okuda1976]).

**(g) Where no standard exists.** A text model for amateur-radio traffic; a callsign class with no list; the weight
of the text model for Morse. Nearest standard practice: speech-recognition language modeling with class-based,
cache and mixture components and an empirically tuned scale (A).

**(h) Pitfalls and the standard checks.**

- *An overweighted text model* produces fluent but wrong text, and can pull a callsign toward a more probable
  pattern, creating a busted call (J). Check: CER and the busted-callsign rate as functions of the scale factor on
  development data (S: [GalesYoung2008] for tuning; A for the callsign metric), and decoding of random five-letter
  groups, where the text model cannot help (C: random code groups are the usual test text for Morse reception,
  used in the operator studies Bell cites and in both 1977 decoders; [Bell1977], [Mills1977], read).
- *Zero probabilities for unseen strings* without smoothing (S: [WittenBell1991]).
- *Train-test overlap* between language-model text and test transcripts inflates scores (S: [Hastie2009]).
- *Perplexity is not error rate*: a lower held-out perplexity need not lower CER; check end to end (J).

### P7. A change of station between overs

**(a) Fields and why they apply.** Speaker change detection and speaker diarization ("who spoke when")
([ChenGopalakrishnan1998], [TranterReynolds2006]); statistical change-point detection ([Page1954],
[Basseville1993], [AdamsMacKay2007]); multiple-model tracking ([BlomBarShalom1988]); speaker adaptation
([GauvainLee1994]). They apply because an over is a "speaker turn": the source's parameters (speed, fist, level,
frequency offset) change abruptly at an unknown time, and a station may return later (A).

**(b) Decisions and the standard answer to each.**

- **P7.1 Detect changes explicitly, or model them.** Standard answers: either explicit change detection on summary
  statistics (cumulative-sum (CUSUM) and generalized likelihood-ratio tests, which raise an alarm when the
  accumulated log-likelihood ratio for "a change has occurred" crosses a threshold; the Bayesian information
  criterion, BIC, for speaker changes) (S: [Page1954]; [Basseville1993], record only; [ChenGopalakrishnan1998], record only), or online
  Bayesian change-point inference that maintains a posterior over the time since the last change (S:
  [AdamsMacKay2007], abstract read), or a latent "station identity" variable inside the model, as HMM-based
  diarization does (S: [TranterReynolds2006], record only). For Morse, choosing among them is (A); inside a
  model-based decoder the latent-variable or run-length form fits most naturally (J).
- **P7.2 Where a change may occur.** Adapted answer: only in gaps, mostly in long gaps, with a higher prior after
  end-of-over procedure signals (K, KN, BK, AR, SK) and in the "DE" pattern (A, J). Precedents: Bell's amplitude model
  lets the level jump at a mark that follows a pause and assumes "changes do not occur during an element-space or a
  mark" ([Bell1977], read); the ITU operating procedure ends a transmission with the cross signal followed by K
  ([ITU-R-M1677], read).
- **P7.3 What is reset.** Adapted answer: re-acquire the parameters that belong to the sender (frequency offset,
  level, speed, fist and spacing) from broad priors, and keep those that belong to the channel and receiver (noise
  level) (A, J).
- **P7.4 Returning stations.** Adapted answer: keep a small set of per-station parameter sets and re-identify a
  returning station, as diarization clusters segments by speaker (A: [TranterReynolds2006]; [ChenGopalakrishnan1998]).
- **P7.5 The first word after a change.** Adapted answer: once the new station's parameters have converged,
  re-decode its opening inside the correction window (A, as P3.6).

**(c) Standard methods, with sources.** CUSUM ([Page1954]); GLR tests and their delay/false-alarm theory
([Basseville1993]); Bayesian online change-point detection ([AdamsMacKay2007]); BIC segmentation and clustering
([ChenGopalakrishnan1998]); diarization systems ([TranterReynolds2006]); IMM ([BlomBarShalom1988]); MAP adaptation
([GauvainLee1994]) (all S).

**(d) Main alternatives, and when each is preferred.** No explicit model, only fast adaptation: the choice of Mills,
whose estimator was designed for "extremely fast adaptation to the time-varying Morse code parameters typical of
multi-operator circuits" ([Mills1977], read); adequate when overs are long and stations similar (J). Explicit
detection plus reset: simple, but each false alarm costs a re-acquisition (J). A latent station variable: most
accurate, costs extra hypotheses (J).

**(e) What is different about Morse, and how to adapt.** The cues are strong and several: speed, fist, level,
frequency offset (0 to 10 Hz between stations, per the brief) and spacing style, plus text cues from the operating
procedure (J). Overs can be very short (in a contest, a callsign and an exchange), leaving few elements to
re-estimate from, which favors priors that a station's parameters are close to typical values (J). The change
happens in a gap, so it can be modeled as a transition available only from gap states (A).

**(f) What published Morse decoders did.** Bell: a jump in level allowed at the mark after a long space, motivated
by nets where "the received power of each of the transmitters on the net will usually be different" ([Bell1977],
read). Mills: fast adaptation only; he notes that after "DE" some parameters cannot be estimated until the next
station sends more text ([Mills1977], read).

**(g) Where no standard exists.** Station changes in Morse traffic. Nearest standard practice: speaker change
detection and diarization, change-point detection; adaptation: P7.1 to P7.5 (A).

**(h) Pitfalls and the standard checks.**

- *Fading mistaken for a change* (a fade looks like a level step), causing needless resets; and *missed changes*,
  causing a lagging tracker to misread the new station's first word (J). Standard check for change detectors: mean
  detection delay against mean time between false alarms (S: [Basseville1993], record only); here also the CER of
  the first word of each over (A).
- *Wrong station attribution*. Check: a diarization-style error rate (fraction of time attributed to the wrong
  station) when stations alternate (A from [TranterReynolds2006]).

### P8. The outputs: streaming text with bounded delay and corrections, per-character probabilities, calibrated confidence, speed

**(a) Fields and why they apply.** Confidence estimation in speech recognition ([Jiang2005], [Wessel2001],
[EvermannWoodland2000], [Mangu2000], [Siu1997]); calibration of probabilities and forecast verification
([Brier1950], [MurphyWinkler1987], [Gneiting2007], [Platt1999], [ZadroznyElkan2002], [NiculescuMizil2005],
[Guo2017], [Naeini2015]); classification with a reject option ([Chow1970]); streaming recognition with revisable
partial results ([GoogleSTT], [Shangguan2020], [Brown1982], [Sarkka2013]). They apply directly: the decoder emits a
symbol sequence with probabilities, revises it while streaming, and hands low-confidence output to a second system,
which is the reject-option setting (S).

**(b) Decisions and the standard answer to each.**

- **P8.1 Per-character probabilities.** Standard answer: posterior probabilities obtained by summing over competing
  hypotheses (forward-backward over the lattice or beam), arranged as a *confusion network*: a sequence of slots,
  each holding the competing symbols, including "no symbol", with posteriors that sum to one (S: [Mangu2000];
  [EvermannWoodland2000]; [GalesYoung2008], read: "each arc in the confusion network carries the posterior
  probability"). Not the best path's score, which is not a probability (S: [Wessel2001], record only).
- **P8.2 Calibration.** Standard answer: treat raw posteriors as scores that are usually miscalibrated, fit a
  monotone calibration map on held-out data (Platt's logistic map, its one-parameter form "temperature scaling", or
  isotonic regression, a monotone step-function map; in speech, a small classifier on confidence features such as posterior, duration and
  language-model score), and evaluate on separate data with reliability diagrams (observed accuracy against stated
  probability), expected calibration error (ECE, the average gap between stated probability and observed accuracy
  over probability bins), the Brier score (mean squared error of the probabilities, a proper scoring rule) and
  normalized cross-entropy (NCE, the speech-recognition measure of how much information the confidence scores add
  over always quoting the average accuracy) (S: [Platt1999],
  [ZadroznyElkan2002], [NiculescuMizil2005]; [Guo2017], abstract read: temperature scaling "is surprisingly effective";
  [Jiang2005]; [Naeini2015]; [Brier1950]; [Gneiting2007]; [Siu1997], record only).
- **P8.3 Overall confidence and the hand-off to the neural decoder.** Standard answer for when to reject: Chow's rule,
  reject an item when its largest posterior is below a threshold set by the relative costs of an error and of a
  rejection, with the operating point read from the error-reject curve (S: [Chow1970], abstract read: "an optimum
  rejection rule ... the error rate can be directly evaluated from the reject function"). It requires calibrated
  posteriors (S). No standard defines an "overall" confidence for a stretch of Morse text; adapted answer: the
  expected number of character errors, Σ(1 − pᵢ), and the probability that the stretch is entirely right, ∏pᵢ,
  from calibrated per-character posteriors (A, J).
- **P8.4 Streaming with bounded delay and corrections.** Standard answer: emit provisional results at once, mark a
  result final when no surviving hypothesis can change it (partial traceback) or when the fixed lag expires, and
  revise provisional text only inside the correction window (S: [Brown1982], record only; [Sarkka2013], read).
  A widely used recognizer's interface makes this explicit: interim results flagged "is_final=false" that "may
  change", and a "stability" field, "an estimate of the likelihood that the recognizer will not change its guess
  about this interim result" (S: [GoogleSTT], read). The quality of streaming output is measured by instability
  metrics at word and segment level (S: [Shangguan2020], abstract read).
- **P8.5 Element timing.** Standard answer: the time alignment of the best path ("if every maximisation decision is
  recorded, a traceback will yield the required best matching state/word sequence") (S: [GalesYoung2008], read).
- **P8.6 Speed.** Judgment: the posterior mean (or most probable value) of the speed state, smoothed over a few
  elements, reported with its posterior spread (J). Precedent: Bell computed "the conditional mean estimates of the
  data rate" at every sample ([Bell1977], read).

**(c) Standard methods, with sources.** Lattice forward-backward and confusion networks ([Mangu2000],
[EvermannWoodland2000]); posterior-based confidence ([Wessel2001], [Jiang2005]); Platt, temperature and isotonic
calibration ([Platt1999], [Guo2017], [ZadroznyElkan2002], [NiculescuMizil2005]); reliability diagrams, ECE, Brier
score, NCE and proper scoring rules ([Naeini2015], [Brier1950], [MurphyWinkler1987], [Gneiting2007], [Siu1997]);
Chow's reject rule ([Chow1970]); interim/final streaming results ([GoogleSTT]) (all S).

**(d) Main alternatives, and when each is preferred.** N-best lists instead of full lattices: simpler, adequate when
the posterior is concentrated on few hypotheses (S: [GalesYoung2008]). A trained confidence classifier on many
features instead of a calibrated posterior: preferred when posteriors are poorly modeled, at the price of needing
labeled held-out data (S: [Jiang2005], record only). Committing output only at the fixed lag (no interim text):
simplest, but every character arrives late (J).

**(e) What is different about Morse, and how to adapt.**

1. *Insertions and deletions are common* (spurious E and T from noise, merged or split letters), so a
   per-character probability must also say whether a character is there at all; the "no symbol" entries of a
   confusion network do this (A).
2. *Word spaces are scored separately* by the brief's metric, so they need their own posteriors (J).
3. *The correction window is long*: at 20 WPM, 20 s holds about 6.7 PARIS words, roughly 40 characters with spaces
   (derived from the PARIS definition); a display policy must limit visible revisions (J).
4. *Calibration must hold per condition* (SNR, speed, fading, keying), because the hand-off threshold acts on each
   signal; a map fitted on the pooled test set can be right on average and wrong in every cell (J).

**(f) What published Morse decoders did.** Bell: the most probable key state (the "demodulated signal"), the most
probable element state with zero delay, the conditional-mean speed, and delayed decisions at the paths' common
ancestor or at 1 s ([Bell1977], read). Mills: print a character when the survivor at the final node scores better
than every successor ([Mills1977], read). RSCW: output when all remaining hypotheses agree ([RSCW], read). No
published Morse decoder that I found reports calibrated per-character probabilities (J, from the sources in
section 4). Neural decoders trained with CTC produce per-frame label posteriors (S for the method: [Graves2006]).

**(g) Where no standard exists.** An overall confidence for a stretch of decoded Morse used to route it to another
decoder. Nearest standard practice: Chow's reject option on calibrated posteriors, and utterance-level confidence in
speech recognition; adaptation in P8.3 (A).

**(h) Pitfalls and the standard checks.**

- *Pruned beams leave out probability*, so posteriors computed inside the beam are overconfident (J). Check:
  reliability diagrams at several beam widths (J).
- *The observation-likelihood scale changes posterior sharpness*: Kaldi's posterior tool notes that "it will normally
  be necessary to supply an acoustic scale to be used in the forward-backward algorithm" ([Kaldi], read; S). So the
  calibration map must be refitted whenever that scale or the model changes (J).
- *Calibrating and evaluating on the same data* overstates calibration (S: [Hastie2009]).
- *ECE depends on its binning*; report it with the Brier score, NCE and the reliability diagram itself (S:
  [Naeini2015], [Gneiting2007]).
- *Streaming flicker.* Check: revisions per character and the delay to finality, the instability measures of
  [Shangguan2020] (S).

### P9. Running many channels within a CPU budget, and degrading gracefully under overload

**(a) Fields and why they apply.** Efficiency of speech-recognition search: pruning, active-hypothesis limits,
real-time factor (CPU seconds per second of audio) ([NeyOrtmanns2000], [Steinbiss1994], [Kaldi]); real-time
scheduling of "imprecise computations", which split each task into a mandatory part and an optional part that
improves the result ([Liu1991]); anytime algorithms, which trade computation time for result quality and can be
stopped at any time ([Zilberstein1996]); load shedding in data-stream systems, which drops work under overload to
keep latency bounded ([Tatbul2003]). They apply because the skimmer runs an unknown, varying number of decoders
against a fixed CPU, each with a real-time deadline (A).

**(b) Decisions and the standard answer to each.**

- **P9.1 The knob that trades cost for accuracy.** Standard answer in decoders: the pruning thresholds, chiefly the
  cap on active hypotheses (Kaldi's max-active) and the beam (S: [Kaldi], read; [Steinbiss1994]). Bell noted that
  probability-mass pruning makes "the decoding computational burden ... adaptive to the signal-to-noise ratio and
  the data rate" ([Bell1977], read; a published precedent).
- **P9.2 Overload policy.** No standard for decoders. Adapted answer: treat a minimal decode of every channel as the
  mandatory part and wider beams or rescoring as optional parts (imprecise computation), allocate the optional
  budget by priority, and shed whole channels only as a last resort (A: [Liu1991]; [Zilberstein1996];
  [Tatbul2003]).
- **P9.3 Priorities.** No standard. Judgment: newly opened channels first (time to spot, and the first word often
  holds the callsign), then channels whose text is still uncertain, then stable, already-spotted channels (J).
- **P9.4 Measure the cost.** Standard answer: measure CPU time per second of signal (the real-time factor) on the
  target machines, and plot accuracy against cost as the pruning settings vary (S: [NeyOrtmanns2000], record only;
  C: [Kaldi]). A floating-point-operation estimate is a planning aid only: beam search is dominated by memory access,
  sorting and branching, which operation counts do not capture (J).
- **P9.5 Benchmark mode under load.** Judgment: in benchmark mode, use fixed settings that do not depend on load, so
  output is reproducible (P4.9) (J).
- **P9.6 Implementation efficiency.** Common practice: process each channel in blocks of frames, vectorize across
  hypotheses, and compute observations at the lowest frame rate that keeps the timing resolution of P1.8 (C: speech
  decoders' frame-synchronous batching, [Kaldi]; J for the Morse specifics).

**(c) Standard methods, with sources.** Beam and histogram pruning with active-hypothesis caps ([Steinbiss1994],
[NeyOrtmanns2000], [Kaldi]); imprecise-computation scheduling ([Liu1991]); anytime algorithms ([Zilberstein1996]);
load shedding ([Tatbul2003]) (S in their fields).

**(d) Main alternatives, and when each is preferred.** Provisioning for the worst case (fixed per-channel cost):
simple and deterministic, wasteful on quiet bands (J). Dropping the weakest channels first: cheapest overload rule,
but weak signals are often the rare ones a skimmer exists to find (J). Lowering the frame rate under load: large
savings, but it costs timing resolution at high speeds first (J).

**(e) What is different about Morse, and how to adapt.** Per-channel cost varies with SNR, with speed and over time
(it peaks at key transitions, P4(e); observed by [Bell1977], read). A busy contest band can hold more than 100
signals (brief). At the brief's planning estimate of 2 to 3 million floating-point operations per channel-second, 100 channels need
0.2 to 0.3 billion per second, a small fraction of a desktop CPU's arithmetic capacity; whether the real cost is
near that estimate can only be measured (J, derived from the brief's numbers).

**(f) What published Morse decoders did.** CW Skimmer: "up to 700 signals can be decoded in parallel on a 3-GHz P4"
(vendor claim, [CWSkimmer], read). Mills: a re-entrant design in which serving several Morse circuits on one
computer "is essentially trivial" ([Mills1977], read). Bell: about 20 minutes of PDP-10 time per minute of signal,
with a path count that adapts to SNR and speed ([Bell1977], read).

**(g) Where no standard exists.** Overload governance for many model-based decoders sharing a CPU. Nearest standard
practice: imprecise computation, anytime algorithms and load shedding; adaptation in P9.2 (A).

**(h) Pitfalls and the standard checks.**

- *An adaptive beam that oscillates* when the controller reacts to its own effect (J). Check: logged beam and CPU
  against time under a step in channel count (J).
- *Starvation* of low-priority channels (S: a classic scheduling failure, [Liu1991], record only). Check: the
  worst-case decode delay per channel under overload (J).
- *Accuracy loss under load that is never measured.* Check: CER and time-to-spot against the number of channels,
  on the target machines (J).

### P10. How numbers are set and the design is tested

**(a) Fields and why they apply.** Model selection and assessment in statistical learning ([Hastie2009],
[CawleyTalbot2010], [BergstraBengio2012]); evaluation of speech recognizers ([GillickCox1989], [BisaniNey2004],
[NIST-SCTK]); design of computer experiments ([McKay1979]); resampling and multiple comparisons
([EfronTibshirani1993], [Holm1979], [BenjaminiHochberg1995]); estimation of thresholds from performance curves in
psychophysics ([WichmannHill2001]); testing of HF modems on simulated fading channels ([ITU-R-F1487]); edit distance
([Levenshtein1966], [WagnerFischer1974]). They apply as written: setting free parameters, testing and comparing
variants of a recognizer is the generic model-selection and evaluation problem (S).

**(b) Decisions and the standard answer to each.**

- **P10.1 Derive or tune.** Standard answer: parameters with a physical meaning are derived from physics or
  measured from data (for example, a filter bandwidth from the element duration, a tracker's process noise from the
  Doppler spread); the remaining free parameters (scale factors, penalties, beams) are tuned on development data; the
  test data are used once, at the end (S: [Hastie2009] §7.2, record only; [GalesYoung2008], read: the language-model
  scale is "an empirically determined constant").
- **P10.2 Partition of the data.** Standard answer: development and test sets disjoint at the level of independent
  units (here: signals and seeds; for real recordings, sessions and stations) (S: [Hastie2009]); beware of
  over-fitting the development set itself, and of the resulting optimistic bias, when many settings are tried on it
  (S: [CawleyTalbot2010], abstract partly read).
- **P10.3 Sampling the conditions.** Standard answer: stratified random sampling of the condition space (one random
  draw within each cell of a grid; Latin hypercube sampling is the best-known form) covers every region while
  avoiding the aliasing of a regular grid (S: [McKay1979]); for tuning, random trials are more efficient than a
  regular grid when few parameters matter (S: [BergstraBengio2012], abstract read). The brief's jittered grid is stratified
  random sampling (S).
- **P10.4 Measuring the error an approximation introduces.** Common practice: run the same signals with and without
  the approximation (exact inference on short signals, a much wider beam, more particles), and count search errors
  by forced alignment of the reference (P4(h)); remove one component at a time ("ablation") (C: speech-recognition
  practice, [NeyOrtmanns2000], record only).
- **P10.5 Comparing variants statistically.** Standard answer: paired comparisons on identical test material, with
  signals or segments (not characters) as the independent units: the matched-pairs segment test (MAPSSWE, which
  splits the output into segments bounded by words both systems got right and compares error counts per segment),
  the sign and Wilcoxon signed-rank tests across signals, McNemar's test on segment correctness (S:
  [GillickCox1989], record only; [NIST-SCTK]: sc_stats "statistically compares the performance of two or more ASR
  systems which have been run on identical test data", read; MAPSSWE description from [ISIP-SCTK], read);
  bootstrap confidence intervals, which resample whole test signals with replacement to estimate the spread of a
  score (S: [BisaniNey2004], record only;
  [EfronTibshirani1993]); the paired bootstrap (S: [Koehn2004]); and control of multiple comparisons when many
  variants are compared (S: [Holm1979], [BenjaminiHochberg1995]).
- **P10.6 Error rate against SNR.** Adapted answer: report CER against S₅₀₀ and E/N₀ for each speed and condition, and
  estimate a threshold SNR (for example S₅₀₀ at CER = 10 %) by fitting a sigmoid with bootstrap confidence intervals
  (A: from psychometric-function fitting, [WichmannHill2001], record only; and from modem testing, where performance
  is specified "as a function of S/N", [ITU-R-F1487], read).
- **P10.7 Test length on fading channels.** Standard answer for modems: "tests should last for the longer of either
  3 000 times the reciprocal of the Doppler spread or 100 times the reciprocal of the product of the BER and the data
  rate" (S: [ITU-R-F1487], read). Derived (N5): 60 000 s (16.7 h) of signal at 0.05 Hz spread, 600 s at 5 Hz.
  For CER, the adapted reading is that slow-fading conditions need many independent signals, since a signal of
  duration L sees only about L × spread independent fading states (about 3 for a 60 s signal at 0.05 Hz spread)
  (A, J).
- **P10.8 Calibration measured on held-out data, per condition cell** (S, see P8.2).
- **P10.9 Reference transcriptions of real recordings.** Judgment: transcribe blind to the decoder's output; have a
  sample transcribed twice, independently, to measure the reference's own error rate, which bounds the measurable
  CER at low SNR (J).
- **P10.10 Baselines and bounds.** Common practice: report a genie-aided decoder (known speed, frequency and level) and
  a simple baseline beside the full decoder, to locate where performance is lost (C: known-channel and coherent
  bounds are routine comparisons in communications, e.g. [Proakis2008], record only; in Morse, [Bell1977] compared
  his decoder with derived lower bounds and with simpler decoders, and [Mills1977] with a fixed-interval classifier,
  read).
- **P10.11 The error metric.** Standard answer: character error rate as Levenshtein (edit) distance divided by the
  reference length (S: [Levenshtein1966], record only; [WagnerFischer1974]); the brief's definition is this
  standard, with prosigns as single symbols and word spaces scored separately (S).

**(c) Standard methods, with sources.** Train/validation/test discipline ([Hastie2009]); stratified and Latin
hypercube sampling ([McKay1979]); random search ([BergstraBengio2012]); MAPSSWE, sign, Wilcoxon and McNemar tests
([GillickCox1989], [NIST-SCTK]); bootstrap ([EfronTibshirani1993], [BisaniNey2004], [Koehn2004]); Holm and
Benjamini-Hochberg corrections ([Holm1979], [BenjaminiHochberg1995]); psychometric fitting ([WichmannHill2001]);
fading-channel test length ([ITU-R-F1487]) (all S).

**(d) Main alternatives, and when each is preferred.** k-fold cross-validation instead of a single development set:
preferred when data are scarce, as real recordings will be (S: [Hastie2009]). Gaussian-approximation confidence
intervals for error proportions: acceptable for large counts, misleading for small or zero counts, where exact
binomial or bootstrap intervals are preferred (S: [EfronTibshirani1993]). Model-based tuning (Bayesian optimization)
instead of random trials: preferred when each evaluation is expensive (J).

**(e) What is different about Morse, and how to adapt.**

1. *Circularity.* The synthetic generator encodes assumptions (scatter shapes, timing ratios, fading spectra) that
   the decoder's model may share; good synthetic scores then partly measure agreement between two models. Real
   recordings, and synthetic sets built from measured real timing, are the check (J).
2. *Correlated errors within a signal.* All characters of one signal share its speed, fist, SNR and fading, so
   characters are not independent trials; signals are (S: the reason for segment-level tests, [GillickCox1989]).
3. *Slow fading needs long tests* (P10.7) (S).
4. *The reference can be wrong* at low SNR, even for an expert listener (J); hence P10.9.

**(f) What published Morse decoders did.** Bell: about 200 letters per test condition, 90 % confidence intervals
under a Gaussian approximation, comparisons with derived lower bounds on error rate and with Gold's MAUDE and Howe's
quasi-Bayes decoder on Howe's recorded mark-space files, and a small field test (cuts of about 50 s) presented as "a
proof of feasibility" ([Bell1977], read). Mills: 108 random five-letter groups at 20 WPM with noise band-limited to
500 Hz; the Viterbi decoder needed about 5 dB less SNR than a fixed-interval classifier for a 10 % error rate, but in
a related test the advantage was "not enough to be statistically significant, considering the test equipment and
methodology used" ([Mills1977], read).

**(g) Where no standard exists.** Little: the evaluation methods are standard. The adaptations are the threshold-SNR
estimate for CER curves (P10.6) and the fading test length for CER (P10.7) (A).

**(h) Pitfalls and the standard checks.**

- *Tuning on the test set*, directly or through repeated looks (S: [Hastie2009]; [CawleyTalbot2010]). Check: a
  held-out set used once, with its seeds recorded before the variants are run (J for the record-keeping).
- *Treating characters as independent* overstates significance (S: [GillickCox1989]).
- *Gaussian intervals on small error counts* (S: [EfronTibshirani1993]).
- *Too few independent fades* (S: [ITU-R-F1487]).
- *Many variants, one test*: without correction, some "significant" wins are chance (S: [Holm1979],
  [BenjaminiHochberg1995]).

## 4. Prior Morse decoders, part by part

### 4.1 The decoders and studies found

I searched for published Morse decoders myself; neural decoders are listed only as context. "Read" means I read the
text (all, or the parts cited); otherwise the status is given.

| Key | Who, when, where | What it is | Status |
|---|---|---|---|
| [Gold1959], [Freimer1959] | B. Gold, MIT Lincoln Laboratory, 1959; M. Freimer, B. Gold, A. Tritter, same issue | MAUDE ("Morse Automatic Decoder"), a special-purpose machine decoding measured mark and space durations with adaptive averages, structural checks and a space correction; "The Morse distribution", statistics of the code | record only; MAUDE described in [Mills1977] and quoted in [Bell1977], read |
| [McElwain1962] | C. K. McElwain, M. B. Evens, 1962 | "The degarbler", a program for correcting machine-read Morse | record only |
| [Petit1971] | R. C. Petit, QST, 1971 | "The Morse-A-Verter", a hobbyist decoder | record only (cited in [Mills1977]) |
| [Guenther1973] | J. A. Guenther, Air Force Institute of Technology, M.S. thesis, 1973 | analysis of hand-sent code; recognition algorithms on a PDP-12; mark-space pair classification | abstract read |
| [Althoff1973] | W. E. Althoff, Naval Postgraduate School, M.S. thesis, 1973 | hardware translator for manual Morse, tested at minimum SNR and for speed tracking | abstract read |
| [Bell1975] | E. L. Bell, Naval Postgraduate School, Engineer's thesis, 1975 | random-telegraph-wave model, Kalman filter and smoother, Viterbi decoder on a simple Markov model of the code; "acceptable recovery" down to −14 dB SNR in 3 kHz (−6.2 dB S₅₀₀, derived) | abstract read |
| [Hickey1976] | W. A. Hickey, Naval Postgraduate School, M.S. thesis, 1976 | modifications to a commercial decoder; a delay algorithm "based upon the attributes of a human operator" | abstract read |
| [Howe1976] | D. M. Howe, Catholic University of America, Ph.D. dissertation, 1976 | decision-directed quasi-Bayes estimation and tracking of the time-varying Morse parameters; a Markov model of English letters | record only (described in [Bell1977] and [Mills1977], read) |
| [Bell1977] | E. L. Bell, Naval Postgraduate School, Ph.D. dissertation, 1977 | optimal Bayesian joint demodulation, decoding and translation; pruned "tree decoder" with a Kalman filter per path | read |
| [Mills1977] | D. L. Mills, University of Maryland, Technical Report TR-554, 1977 | real-time decoder on an LSI-11: matched filter, threshold corrector, ratio-weighted estimation of durations, Viterbi over the code tree | read |
| [Tong1990] | J. B. Tong, University of New Brunswick, senior report, 1990 | computer-based multi-channel Morse decoding | record only (access restricted) |
| [fldigi] | W1HKJ and contributors, fldigi manual | amplitude demodulator with automatic threshold, matched FFT filter option, speed tracking, "SOM decoding" (a fuzzy best-fit character match) | documentation read |
| [CWSkimmer] | A. Shovkoplyas (VE3NEA), Afreet Software | multi-signal skimmer; decoder "based on the methods of Bayesian statistics"; algorithm unpublished | vendor page read |
| [RSCW] | P.-T. de Boer (PA3FWM) | decoder for machine-sent satellite Morse: bit-matched filtering, FFT frequency tracking, tree search | documentation read |
| [AG1LE2013a], [AG1LE2013b] | M. Niininen (AG1LE), blog, 2013 | a histogram-and-Bayes classifier; then a C implementation of Bell's 1977 algorithms integrated into fldigi, reported to improve accuracy "with real life noisy CW signals", with "approximately 3% base error rate" and no controlled measurements | blog read |
| [JohnsonWillsky2010] | M. J. Johnson, A. S. Willsky, UAI 2010 | unsupervised semi-Markov model that recovers the short-tone/long-tone structure of Morse audio | read |
| [Wang2017] | Wang, Sun, Jia, Journal of Signal Processing (China), 2017 | time-frequency analysis, clustering of element durations, binary-tree lookup | abstract read |
| [WangX2018], [Dey2018] | 2018 conference papers | neural decoders and data sets (context only) | abstract read / record only |

### 4.2 What they did, part by part

| Part | Bell 1977 | Mills 1977 | MAUDE 1959 | fldigi | RSCW | CW Skimmer | AG1LE 2013 |
|---|---|---|---|---|---|---|---|
| P1 detection | fixed 100 Hz filter, envelope detector, 5 ms samples | 85 Hz filter, envelope, moving-sum matched filter set by nominal speed, 2 s threshold corrector, slicer with hysteresis | input was measured durations | amplitude demodulator, automatic threshold, FFT filter optionally matched to speed | I/Q bit-matched moving average | blanker, AGC, variable filter | as fldigi / as Bell |
| P2 timing | speed state on a 2 WPM grid, 10 to 60 WPM, changes only at key transitions; Laplacian durations normalized by speed | six duration means by ratio-weighted estimation; Gaussian likelihood whose σ is 0.15 times the length of the (mark, space) duration vector | running averages, structural checks, space correction | speed tracking within a set range | fixed speed (machine code) | unpublished | histograms; then Bell's model |
| P3 nuisances | amplitude by Kalman filter per path; noise by scaled minimum of the envelope over 1.2 s; frequency not tracked | separate estimation of durations; threshold by RC-like averaging | adaptive averages | threshold adaptation | FFT frequency tracking, bit-clock recovery | unpublished | Bell's |
| P4 inference | joint trellis, probability-mass pruning (P_opt = 0.9), decisions at common ancestor or forced at 1 s | Viterbi over the code tree on mark-space pairs | rule-based | "SOM" best-fit match | tree search over dibits | "Bayesian statistics" | Bell's |
| P5 segmentation, interference | "pause" symbol; static and interferer failures reported | hysteresis, truncation of long intervals; AGC problem reported | not documented in sources read | squelch; ringing on impulses noted | none for hand-sent or interference | noise blanker | not documented |
| P6 text | element-level Markov chains tested; letters independent and equally likely; richer models recommended | English letter priors or priors by code length; digrams and dictionary correction proposed | "knowledge of the language" judged necessary (Gold) | none documented | none | unpublished | none documented |
| P7 station change | level jump allowed after pauses | fast adaptation for multi-operator circuits | not documented | not documented | not applicable | unpublished | not documented |
| P8 outputs | MAP key state, zero-delay MAP element, conditional-mean speed, delayed text | print rule at the final node | printed text | text, speed indicator | text when hypotheses agree | text, spots | text |
| P9 CPU | 20 min of PDP-10 per minute of signal; adaptive path count | real time on an LSI-11; multi-circuit by design | special-purpose machine | one signal per instance | one signal | "up to 700 signals ... on a 3-GHz P4" | one signal |
| P10 testing | ≈ 200 letters per condition, Gaussian 90 % intervals, lower bounds, comparison with MAUDE and Howe, small field set | 108 five-letter groups at 20 WPM; ~5 dB gain at 10 % error; significance doubtful in a second test | not documented in sources read | none published | none published | none published | qualitative examples |

(Sources: as in 4.1; every cell for Bell, Mills, fldigi, RSCW, CW Skimmer and AG1LE is from text I read; the MAUDE
column is from Mills's and Bell's descriptions of it.)

### 4.3 What the prior work shows (J)

1. The model-based approach to Morse was worked out in the 1970s, in US defense-funded theses and reports, and Bell's
   1977 dissertation is its fullest statement; it already contains the switching state-space structure, explicit
   duration modeling, joint speed tracking, Kalman tracking of amplitude, pruned search and delayed decisions.
2. What that work did not do: a text model beyond independent letters, frequency tracking, explicit handling of
   impulses and co-channel Morse, calibrated probabilities, and large-scale testing on real signals. These are
   exactly the parts where this review finds the nearest standards in speech recognition, communications and
   tracking rather than in the Morse literature.
3. Public modern decoders are mostly cascades of threshold detection and duration classification; the one widely
   used skimmer that claims a Bayesian decoder has not published it.
4. No published Morse decoder that I found reports per-character probabilities with a measured calibration.

## 5. Where no standard exists

Each row is a sub-problem for which I found no standard specific to Morse (or to keyed carriers), the nearest
standard practice, and how it would be adapted. The adaptations are (A) or (J) as marked in the parts.

| # | Sub-problem without a standard | Nearest standard practice (field, source) | Adaptation | Decisions |
|---|---|---|---|---|
| 1 | The overall decoder for Morse | Source-channel model with HMM search (speech: [GalesYoung2008]); one Morse precedent, [Bell1977] | Compile text model, code tree, explicit durations and a keyed-carrier observation model into one search | P0.1 to P0.4 |
| 2 | Detecting a carrier observable only while keyed, with unknown pulse lengths (12 ms to 720 ms) | Per-survivor processing ([Raheli1995]); switching Kalman filters ([Murphy2002]); HMM frequency tracking ([StreitBarrett1990]) | Trackers per hypothesis, updated only on its key-down frames, with covariance growing through gaps; matched filtering done implicitly by the duration model on short frames | P1.2, P1.3, P1.5, P3.3, P3.4 |
| 3 | Correlated channel noise in the frame likelihoods | Generalized matched filter ([Kay1998]); acoustic scaling in speech ([GalesYoung2008]) | Whiten or decimate first so that the likelihood counts independent information (the naive overcount is 5.95, N6); then tune any residual scale | P1.6, P6.6 |
| 4 | Duration distributions of hand keying | Scalar (Weber) timing in motor psychology ([Gibbon1977], [WingKristofferson1973]) | Durations in units of T with constant coefficient of variation; parameters measured on real keying | P2.3 |
| 5 | Speed representation and abrupt speed changes | Tempo tracking ([CemgilKappen2003], [Krebs2015]); multiple-model tracking ([BlomBarShalom1988]) | Log-spaced speed grid (33 to 63 states for 5 to 100 WPM, N11); changes only at element boundaries; heavy-tailed or jump transitions | P2.4, P2.5, P2.6 |
| 6 | Farnsworth spacing | None outside Morse; the ARRL generative formula ([Bloom1990]) | One gap-stretch parameter g common to letter and word gaps (up to about 16, N10) | P2.7 |
| 7 | Weighting, dash/dot ratio, fist | Speaker adaptation ([GauvainLee1994]); context-dependent units in speech ([GalesYoung2008]) | Additive mark/space bias δ, dash/dot ratio, per-station parameters with priors; durations conditioned on the preceding element and keying device | P2.7, P2.8 |
| 8 | The factor-of-3 speed ambiguity | Tempo "octave errors" in music ([Gouyon2006], [Krebs2015]) | Speed priors and joint decoding; evaluation that counts ×3 errors separately | P2(e), P2(h) |
| 9 | Parameter estimation with intermittent observability and long unobserved gaps | Synchronization ([Meyr1998]); per-survivor processing ([Raheli1995]) | Gate updates by the hypothesized key state; re-acquire after long gaps | P3.1 to P3.4 |
| 10 | Cold start and the first word of a transmission or over | Multi-pass adaptation in speech ([GalesYoung2008] §7.1); fixed-lag smoothing ([Sarkka2013]) | Re-decode the opening once estimates converge, inside the 20 s correction window | P3.6, P7.5 |
| 11 | Rejecting non-Morse keying (tuning carriers, random keying) | Filler models in keyword spotting ([RosePaul1990]) | A carrier state and an unconstrained keying loop with a tuned penalty; false-character rate as the check | P5.2 |
| 12 | Separating co-channel Morse stations | Factorial HMMs ([GhahramaniJordan1997]); multiuser detection and cancellation ([Verdu1998]) | Use the neighbor channel's own decoder output to explain or cancel its leakage; a joint model where leakage is high (N7) | P5.5 |
| 13 | Impulses inside a Morse duration model | Heavy-tailed likelihoods ([Middleton1977], [Huber1964]); blanking ([Zhidkov2008]) | A short "impulse" event in the model, plus a robust likelihood | P5.4 |
| 14 | A text model for amateur-radio traffic | Mixture, class and cache language models in speech ([IyerOstendorf1999], [Brown1992], [KuhnDeMori1990]) | A dynamic mixture of ragchew, contest, call and procedure components | P6.1, P6.3, P6.5 |
| 15 | Callsigns with no list of calls | Class-based models with sub-grammars ([Brown1992], [Mohri2002]); callsign format ([ITU-RR19]) | A format-only callsign class, guarded against memorizing real calls | P6.4 |
| 16 | The weight of the text model for Morse | Language-model scale and insertion penalty ([GalesYoung2008]) | Tune after fixing the observation model; monitor the busted-callsign rate | P6.6 |
| 17 | A change of station between overs | Speaker change detection and diarization ([ChenGopalakrishnan1998], [TranterReynolds2006]); change-point detection ([AdamsMacKay2007]) | Changes only in gaps, priors raised by procedure signals; reset sender parameters, keep noise; recall returning stations | P7.1 to P7.5 |
| 18 | An overall confidence for routing text to the neural decoder | Chow's reject rule ([Chow1970]); utterance confidence in speech ([Jiang2005]) | Expected number of errors Σ(1 − pᵢ) from calibrated per-character posteriors; calibration checked per condition | P8.3 |
| 19 | Overload governance for many model-based decoders | Imprecise computation ([Liu1991]); anytime algorithms ([Zilberstein1996]); load shedding ([Tatbul2003]) | A minimal decode of every channel as the mandatory part; optional effort by priority | P9.2, P9.3 |
| 20 | Threshold SNR and fading test length for CER | Psychometric fitting ([WichmannHill2001]); modem test length ([ITU-R-F1487]) | Sigmoid fits with bootstrap intervals; many independent signals per slow-fading cell | P10.6, P10.7 |

The owner's expectation, recorded in the brief, that speech and language recognition will need adapting is borne
out: rows 1, 3, 10, 14 to 16 and 18 adapt speech practice; rows 4, 5 and 8 adapt music practice; rows 2, 9 and 12
adapt communications practice (J).

## 6. References

"Checked" says what I verified in this review: **text** (I read the work, or the cited part of it), **abstract**
(only the abstract or the publisher's summary), or **record** (only the bibliographic record: author, year, title,
venue, identifier, verified against Crossref or the publisher unless marked "from memory"). For "record" entries
the content attributed in this file is my recollection of a well-known result.

### 6.1 Morse code, Morse decoders and Morse timing

- **[AG1LE2013a]** Niininen, M. (AG1LE) (2013). "Towards Bayesian Morse Decoder", blog post, 5 January 2013.
  https://ag1le.blogspot.com/2013/01/ . Checked: text.
- **[AG1LE2013b]** Niininen, M. (AG1LE) (2013). "New Morse Decoder - Part 1" (27 September 2013) and "Part 2"
  (24 December 2013), blog posts. https://ag1le.blogspot.com/2013/ and
  https://ag1le.blogspot.com/2013/12/new-morse-decoder-part-2.html . Checked: text.
- **[Althoff1973]** Althoff, W. E. (1973). *An automatic radiotelegraph translator and transcriber for manually sent
  Morse.* M.S. thesis, Naval Postgraduate School, Monterey, CA. https://hdl.handle.net/10945/16499 . Checked:
  abstract.
- **[Bell1975]** Bell, E. L. (1975). *Processing of the manual Morse signal using optimal linear filtering,
  smoothing and decoding.* Engineer's thesis, Naval Postgraduate School, Monterey, CA.
  https://hdl.handle.net/10945/21000 . Checked: abstract.
- **[Bell1977]** Bell, E. L. (1977). *Optimal Bayesian estimation of the state of a probabilistically mapped
  memory-conditional Markov process with application to manual Morse decoding.* Ph.D. dissertation, Naval
  Postgraduate School, Monterey, CA. https://hdl.handle.net/10945/26668 ; full text (OCR):
  https://archive.org/details/optimalbayesiest1094526668 ; DTIC ADA046503. Checked: text (sections I, II, VI to X
  and the reference list; some tables are illegible in the OCR).
- **[Bloom1990]** Bloom, J. (KE3Z) (1990). "A Standard for Morse Timing Using the Farnsworth Technique." *QEX*,
  April 1990, pp. 8-9. ARRL. http://www.arrl.org/files/file/Technology/x9004008.pdf . Checked: text.
- **[CWSkimmer]** Shovkoplyas, A. (VE3NEA). *CW Skimmer* (version 2.1), product page, Afreet Software.
  https://www.dxatlas.com/CwSkimmer/ . Checked: text.
- **[Dey2018]** Dey, S., Chugg, K. M., Beerel, P. A. (2018). "Morse Code Datasets for Machine Learning." *9th
  International Conference on Computing, Communication and Networking Technologies (ICCCNT)*, pp. 1-7.
  doi:10.1109/ICCCNT.2018.8494011 . Checked: record.
- **[fldigi]** Freese, D. (W1HKJ) and contributors. *Fldigi Users Manual* (v4.2), pages "CW" and "CW
  Configuration". https://www.w1hkj.org/FldigiHelp/cw_page.html and
  https://www.w1hkj.org/FldigiHelp/cw_configuration_page.html . Checked: text.
- **[Freimer1959]** Freimer, M., Gold, B., Tritter, A. L. (1959). "The Morse distribution." *IRE Transactions on
  Information Theory* 5(1):25-31. doi:10.1109/TIT.1959.1057479 . Checked: record.
- **[Gold1959]** Gold, B. (1959). "Machine recognition of hand-sent Morse code." *IRE Transactions on Information
  Theory* 5(1):17-24. doi:10.1109/TIT.1959.1057478 . Checked: record (content via [Mills1977] and [Bell1977]).
- **[Guenther1973]** Guenther, J. A. (1973). *Machine Recognition of Hand-Sent Morse Code Using the PDP-12
  Computer.* M.S. thesis, Air Force Institute of Technology. DTIC AD0786492;
  https://archive.org/details/DTIC_AD0786492 . Checked: abstract.
- **[Hickey1976]** Hickey, W. A. (1976). *Quasi-real time translation of Morse-coded signals using digital delay
  processing.* M.S. thesis, Naval Postgraduate School. https://hdl.handle.net/10945/17918 . Checked: abstract.
- **[Howe1976]** Howe, D. M. (1976). *Decision-directed modified quasi-Bayes estimation and tracking of the
  nonstationary stochastic parameters associated with a communication signal.* Ph.D. dissertation, Catholic
  University of America, Washington, DC. Checked: record (as cited in [Bell1977] and [Mills1977]; not located
  online).
- **[ITU-R-M1677]** ITU-R (2009). *Recommendation ITU-R M.1677-1: International Morse code* (10/2009).
  https://www.itu.int/rec/R-REC-M.1677 . Checked: text.
- **[ITU-RR19]** ITU (current edition). *Radio Regulations*, Article 19 "Identification of stations", No. 19.68
  (formation of amateur call signs). https://www.itu.int/pub/R-REG-RR . Checked: record (the provision's wording
  via secondary sources found by search, not read in the Regulations).
- **[JohnsonWillsky2010]** Johnson, M. J., Willsky, A. S. (2010). "The Hierarchical Dirichlet Process Hidden
  Semi-Markov Model." *Proceedings of the 26th Conference on Uncertainty in Artificial Intelligence (UAI 2010).*
  arXiv:1203.3485 . Checked: text (section 5.2, "Learning Morse Code"). The journal version, "Bayesian
  Nonparametric Hidden Semi-Markov Models", *JMLR* 14:673-701 (2013), does not contain the Morse example (checked).
- **[McElwain1962]** McElwain, C. K., Evens, M. B. (1962). "The degarbler—A program for correcting machine-read
  Morse code." *Information and Control* 5(4):368-384. doi:10.1016/S0019-9958(62)90657-5 . Checked: record.
- **[Mills1977]** Mills, D. L. (1977). *Real-Time Recognition of Manual Morse Telegraphy Using Nonlinear Estimation
  and Viterbi Decoding.* Technical Report TR-554, Department of Computer Science, University of Maryland, College
  Park. DTIC ADA051001; https://archive.org/details/DTIC_ADA051001 . Checked: text (whole report).
- **[Okuda1976]** Okuda, T., Tanaka, E., Kasai, T. (1976). "A Method for the Correction of Garbled Words Based on
  the Levenshtein Metric." *IEEE Transactions on Computers* C-25(2):172-178. doi:10.1109/TC.1976.5009232 . Checked:
  record.
- **[Petit1971]** Petit, R. C. (1971). "The Morse-A-Verter." *QST* 55(1):30-39. Checked: record (as cited in
  [Mills1977]).
- **[RSCW]** de Boer, P.-T. (PA3FWM). *RSCW's algorithm*, documentation page.
  https://www.pa3fwm.nl/software/rscw/algorithm.html . Checked: text.
- **[Tong1990]** Tong, J. B. (1990). *Computer-based multi-channel Morse code decoding.* Engineering senior report
  EE-182, University of New Brunswick. https://unbscholar.lib.unb.ca/handle/1882/31003 . Checked: record (access
  restricted).
- **[Wang2017]** Wang, Y., Sun, Z., Jia, K. (2017). "Research on Automatic Decoding Algorithm of Morse Telegraph."
  *Journal of Signal Processing* (China) 33(11):1451-1456. doi:10.16798/j.issn.1003-0530.2017.11.006 . Checked:
  abstract.
- **[WangX2018]** Wang, X., Zhao, Q., Ma, C., Xiong, J. (2018). "Automatic Morse Code Recognition under Low SNR."
  *Proceedings of MECAE 2018*, Advances in Engineering Research. doi:10.2991/mecae-18.2018.46 . Checked: abstract.

### 6.2 Standards and system documentation

- **[GoogleSTT]** Google Cloud. *Speech-to-Text v1 RPC reference*: StreamingRecognitionResult (is_final,
  stability) and StreamingRecognitionConfig (interim_results).
  https://docs.cloud.google.com/speech-to-text/docs/reference/rpc/google.cloud.speech.v1 . Checked: text.
- **[IEEE754]** IEEE (2019). *IEEE Standard for Floating-Point Arithmetic*, IEEE Std 754-2019.
  doi:10.1109/IEEESTD.2019.8766229 . Checked: record.
- **[ISIP-SCTK]** Institute for Signal and Information Processing (Picone et al.). "Scoring: Significance
  Testing", speech fundamentals tutorial, section 4.3.6.
  https://isip.piconepress.com/projects/speech/software/tutorials/production/fundamentals/v1.0/section_04/s04_03_p06.html .
  Checked: text.
- **[ITU-R-F1487]** ITU-R (2000). *Recommendation ITU-R F.1487: Testing of HF modems with bandwidths of up to about
  12 kHz using ionospheric channel simulators* (05/2000). https://www.itu.int/rec/R-REC-F.1487 . Checked: text
  (Annex 1 model; Annex 3 parameters and test length).
- **[Kaldi]** Povey, D., et al. *Kaldi documentation*: "Decoders used in the Kaldi toolkit" and "Lattices in
  Kaldi". https://kaldi-asr.org/doc/decoders.html and https://kaldi-asr.org/doc/lattices.html . Checked: text.
- **[NIST-SCTK]** NIST. *SCTK scoring toolkit*, sc_stats manual page.
  https://people.csail.mit.edu/joe/sctk-1.2/doc/sc_stats.htm . Checked: text of the description (the list of
  tests, McNemar, MAPSSWE, sign and Wilcoxon, from the manual as summarized by search; the page copy I read lacks
  the list).

### 6.3 Speech recognition and language modeling

- **[Bahl1983]** Bahl, L. R., Jelinek, F., Mercer, R. L. (1983). "A Maximum Likelihood Approach to Continuous
  Speech Recognition." *IEEE Transactions on Pattern Analysis and Machine Intelligence* PAMI-5(2):179-190.
  doi:10.1109/TPAMI.1983.4767370 . Checked: record.
- **[Bellegarda2004]** Bellegarda, J. R. (2004). "Statistical language model adaptation: review and perspectives."
  *Speech Communication* 42(1):93-108. doi:10.1016/j.specom.2003.08.002 . Checked: record.
- **[Brown1982]** Brown, P. F., Spohrer, J. C., Hochschild, P. H., Baker, J. K. (1982). "Partial traceback and
  dynamic programming." *Proc. ICASSP 1982*, pp. 1629-1632. doi:10.1109/ICASSP.1982.1171441 . Checked: record.
- **[Brown1992]** Brown, P. F., Della Pietra, V. J., deSouza, P. V., Lai, J. C., Mercer, R. L. (1992). "Class-Based
  n-gram Models of Natural Language." *Computational Linguistics* 18(4):467-480. https://aclanthology.org/J92-4003/ .
  Checked: record.
- **[ChenGoodman1999]** Chen, S. F., Goodman, J. (1999). "An empirical study of smoothing techniques for language
  modeling." *Computer Speech & Language* 13(4):359-393. doi:10.1006/csla.1999.0128 ; technical report TR-10-98,
  Harvard University. Checked: abstract.
- **[ChenGopalakrishnan1998]** Chen, S. S., Gopalakrishnan, P. S. (1998). "Clustering via the Bayesian information
  criterion with applications in speech recognition." *Proc. ICASSP 1998*, vol. 2, pp. 645-648.
  doi:10.1109/ICASSP.1998.675347 . Checked: record.
- **[ClearyWitten1984]** Cleary, J., Witten, I. (1984). "Data Compression Using Adaptive Coding and Partial String
  Matching." *IEEE Transactions on Communications* 32(4):396-402. doi:10.1109/TCOM.1984.1096090 . Checked: record.
- **[EvermannWoodland2000]** Evermann, G., Woodland, P. C. (2000). "Large vocabulary decoding and confidence
  estimation using word posterior probabilities." *Proc. ICASSP 2000*, vol. 3, pp. 1655-1658.
  doi:10.1109/ICASSP.2000.862067 . Checked: record.
- **[GalesYoung2008]** Gales, M., Young, S. (2008). "The Application of Hidden Markov Models in Speech
  Recognition." *Foundations and Trends in Signal Processing* 1(3):195-304. doi:10.1561/2000000004 ;
  https://mi.eng.cam.ac.uk/~mjfg/mjfg_NOW.pdf . Checked: text (sections 2, 4.2 and 7.1).
- **[GauvainLee1994]** Gauvain, J.-L., Lee, C.-H. (1994). "Maximum a posteriori estimation for multivariate Gaussian
  mixture observations of Markov chains." *IEEE Transactions on Speech and Audio Processing* 2(2):291-298.
  doi:10.1109/89.279278 . Checked: record.
- **[GoelByrne2000]** Goel, V., Byrne, W. J. (2000). "Minimum Bayes-risk automatic speech recognition." *Computer
  Speech & Language* 14(2):115-135. doi:10.1006/csla.2000.0138 . Checked: record.
- **[Graves2006]** Graves, A., Fernández, S., Gomez, F., Schmidhuber, J. (2006). "Connectionist temporal
  classification: labelling unsegmented sequence data with recurrent neural networks." *Proc. ICML 2006*,
  pp. 369-376. doi:10.1145/1143844.1143891 . Checked: record.
- **[Hinton2012]** Hinton, G., Deng, L., Yu, D., Dahl, G., Mohamed, A., Jaitly, N., Senior, A., Vanhoucke, V.,
  Nguyen, P., Sainath, T., Kingsbury, B. (2012). "Deep Neural Networks for Acoustic Modeling in Speech Recognition:
  The Shared Views of Four Research Groups." *IEEE Signal Processing Magazine* 29(6):82-97.
  doi:10.1109/MSP.2012.2205597 . Checked: record.
- **[IyerOstendorf1999]** Iyer, R. M., Ostendorf, M. (1999). "Modeling long distance dependence in language: topic
  mixtures versus dynamic cache models." *IEEE Transactions on Speech and Audio Processing* 7(1):30-39.
  doi:10.1109/89.736328 . Checked: record.
- **[Jelinek1976]** Jelinek, F. (1976). "Continuous speech recognition by statistical methods." *Proceedings of the
  IEEE* 64(4):532-556. doi:10.1109/PROC.1976.10159 . Checked: record.
- **[Jelinek1997]** Jelinek, F. (1997). *Statistical Methods for Speech Recognition.* MIT Press, Cambridge, MA.
  Checked: record (from a published review of the book found on Crossref).
- **[JelinekMercer1980]** Jelinek, F., Mercer, R. L. (1980). "Interpolated estimation of Markov source parameters
  from sparse data." In Gelsema, E. S., Kanal, L. N. (eds.), *Pattern Recognition in Practice*, North-Holland,
  pp. 381-397. Checked: record (from memory).
- **[Jiang2005]** Jiang, H. (2005). "Confidence measures for speech recognition: A survey." *Speech Communication*
  45(4):455-470. doi:10.1016/j.specom.2004.12.004 . Checked: record.
- **[KneserNey1995]** Kneser, R., Ney, H. (1995). "Improved backing-off for M-gram language modeling." *Proc.
  ICASSP 1995*, vol. 1, pp. 181-184. doi:10.1109/ICASSP.1995.479394 . Checked: record.
- **[KuhnDeMori1990]** Kuhn, R., De Mori, R. (1990). "A cache-based natural language model for speech recognition."
  *IEEE Transactions on Pattern Analysis and Machine Intelligence* 12(6):570-583. doi:10.1109/34.56193 . Checked:
  record.
- **[Levinson1986]** Levinson, S. E. (1986). "Continuously variable duration hidden Markov models for automatic
  speech recognition." *Computer Speech & Language* 1(1):29-45. doi:10.1016/S0885-2308(86)80009-2 . Checked: record.
- **[Mangu2000]** Mangu, L., Brill, E., Stolcke, A. (2000). "Finding consensus in speech recognition: word error
  minimization and other applications of confusion networks." *Computer Speech & Language* 14(4):373-400.
  doi:10.1006/csla.2000.0152 ; arXiv:cs/0010012 . Checked: abstract.
- **[MartiBunke2001]** Marti, U.-V., Bunke, H. (2001). "Using a statistical language model to improve the
  performance of an HMM-based cursive handwriting recognition system." *International Journal of Pattern
  Recognition and Artificial Intelligence* 15(1):65-90. doi:10.1142/S0218001401000848 . Checked: record.
- **[Mohri2002]** Mohri, M., Pereira, F., Riley, M. (2002). "Weighted finite-state transducers in speech
  recognition." *Computer Speech & Language* 16(1):69-88. doi:10.1006/csla.2001.0184 . Checked: record.
- **[NeyOrtmanns2000]** Ney, H., Ortmanns, S. (2000). "Progress in dynamic programming search for LVCSR."
  *Proceedings of the IEEE* 88(8):1224-1240. doi:10.1109/5.880081 . Checked: record.
- **[Ostendorf1996]** Ostendorf, M., Digalakis, V. V., Kimball, O. A. (1996). "From HMM's to segment models: a
  unified view of stochastic modeling for speech recognition." *IEEE Transactions on Speech and Audio Processing*
  4(5):360-378. doi:10.1109/89.536930 . Checked: record.
- **[PlotzFink2009]** Plötz, T., Fink, G. A. (2009). "Markov models for offline handwriting recognition: a survey."
  *International Journal on Document Analysis and Recognition* 12(4):269-298. doi:10.1007/s10032-009-0098-4 .
  Checked: record.
- **[Rabiner1989]** Rabiner, L. R. (1989). "A tutorial on hidden Markov models and selected applications in speech
  recognition." *Proceedings of the IEEE* 77(2):257-286. doi:10.1109/5.18626 . Checked: record (the copies found
  online are scanned images without a text layer; the content cited is standard and also in [Murphy2002], read).
- **[RosePaul1990]** Rose, R. C., Paul, D. B. (1990). "A hidden Markov model based keyword recognition system."
  *Proc. ICASSP 1990*, pp. 129-132. doi:10.1109/ICASSP.1990.115555 . Checked: record.
- **[RussellMoore1985]** Russell, M., Moore, R. (1985). "Explicit modelling of state occupancy in hidden Markov
  models for automatic speech recognition." *Proc. ICASSP 1985*, pp. 5-8. doi:10.1109/ICASSP.1985.1168477 .
  Checked: record.
- **[Shangguan2020]** Shangguan, Y., Knister, K., He, Y., McGraw, I., Beaufays, F. (2020). "Analyzing the Quality
  and Stability of a Streaming End-to-End On-Device Speech Recognizer." *Proc. Interspeech 2020*, pp. 591-595.
  doi:10.21437/Interspeech.2020-1194 ; arXiv:2006.01416 . Checked: abstract.
- **[Siu1997]** Siu, M., Gish, H., Richardson, F. (1997). "Improved estimation, evaluation and applications of
  confidence measures for speech recognition." *Proc. Eurospeech 1997*, pp. 831-834.
  doi:10.21437/Eurospeech.1997-282 . Checked: record.
- **[Sohn1999]** Sohn, J., Kim, N. S., Sung, W. (1999). "A statistical model-based voice activity detection." *IEEE
  Signal Processing Letters* 6(1):1-3. doi:10.1109/97.736233 . Checked: record.
- **[Steinbiss1994]** Steinbiss, V., Tran, B.-H., Ney, H. (1994). "Improvements in beam search." *Proc. ICSLP 1994*,
  pp. 2143-2146. doi:10.21437/ICSLP.1994-538 . Checked: record.
- **[TranterReynolds2006]** Tranter, S. E., Reynolds, D. A. (2006). "An overview of automatic speaker diarization
  systems." *IEEE Transactions on Audio, Speech, and Language Processing* 14(5):1557-1565.
  doi:10.1109/TASL.2006.878256 . Checked: record.
- **[Wessel2001]** Wessel, F., Schlüter, R., Macherey, K., Ney, H. (2001). "Confidence measures for large vocabulary
  continuous speech recognition." *IEEE Transactions on Speech and Audio Processing* 9(3):288-298.
  doi:10.1109/89.906002 . Checked: record.
- **[WittenBell1991]** Witten, I. H., Bell, T. C. (1991). "The zero-frequency problem: estimating the probabilities
  of novel events in adaptive text compression." *IEEE Transactions on Information Theory* 37(4):1085-1094.
  doi:10.1109/18.87000 . Checked: record.
- **[Young1989]** Young, S. J., Russell, N. H., Thornton, J. H. S. (1989). *Token passing: a simple conceptual model
  for connected speech recognition systems.* Technical report CUED/F-INFENG/TR.38, Cambridge University Engineering
  Department. Checked: record (from memory).
- **[Yu2010]** Yu, S.-Z. (2010). "Hidden semi-Markov models." *Artificial Intelligence* 174(2):215-243.
  doi:10.1016/j.artint.2009.11.011 . Checked: record.

### 6.4 Detection, estimation and communications

- **[AndersonMohan1984]** Anderson, J., Mohan, S. (1984). "Sequential Coding Algorithms: A Survey and Cost
  Analysis." *IEEE Transactions on Communications* 32(2):169-176. doi:10.1109/TCOM.1984.1096023 . Checked: record.
- **[Baddour2005]** Baddour, K. E., Beaulieu, N. C. (2005). "Autoregressive modeling for fading channel simulation."
  *IEEE Transactions on Wireless Communications* 4(4):1650-1662. doi:10.1109/TWC.2005.850327 . Checked: record.
- **[BCJR1974]** Bahl, L., Cocke, J., Jelinek, F., Raviv, J. (1974). "Optimal decoding of linear codes for minimizing
  symbol error rate." *IEEE Transactions on Information Theory* 20(2):284-287. doi:10.1109/TIT.1974.1055186 .
  Checked: record.
- **[Dempster1977]** Dempster, A. P., Laird, N. M., Rubin, D. B. (1977). "Maximum Likelihood from Incomplete Data via
  the EM Algorithm." *Journal of the Royal Statistical Society, Series B* 39(1):1-22.
  doi:10.1111/j.2517-6161.1977.tb01600.x . Checked: record.
- **[Forney1973]** Forney, G. D. (1973). "The Viterbi algorithm." *Proceedings of the IEEE* 61(3):268-278.
  doi:10.1109/PROC.1973.9030 . Checked: record.
- **[GhahramaniJordan1997]** Ghahramani, Z., Jordan, M. I. (1997). "Factorial Hidden Markov Models." *Machine
  Learning* 29(2-3):245-273. doi:10.1023/A:1007425814087 . Checked: record.
- **[Harris1978]** Harris, F. J. (1978). "On the use of windows for harmonic analysis with the discrete Fourier
  transform." *Proceedings of the IEEE* 66(1):51-83. doi:10.1109/PROC.1978.10837 . Checked: record.
- **[Huber1964]** Huber, P. J. (1964). "Robust Estimation of a Location Parameter." *The Annals of Mathematical
  Statistics* 35(1):73-101. doi:10.1214/aoms/1177703732 . Checked: record.
- **[Kailath1969]** Kailath, T. (1969). "A general likelihood-ratio formula for random signals in Gaussian noise."
  *IEEE Transactions on Information Theory* 15(3):350-361. doi:10.1109/TIT.1969.1054307 . Checked: record.
- **[Kassam1988]** Kassam, S. A. (1988). *Signal Detection in Non-Gaussian Noise.* Springer.
  doi:10.1007/978-1-4612-3834-8 . Checked: record.
- **[Kay1993]** Kay, S. M. (1993). *Fundamentals of Statistical Signal Processing, Volume I: Estimation Theory.*
  Prentice Hall. Checked: record (from memory).
- **[Kay1998]** Kay, S. M. (1998). *Fundamentals of Statistical Signal Processing, Volume II: Detection Theory.*
  Prentice Hall. Checked: record (from memory).
- **[Martin2001]** Martin, R. (2001). "Noise power spectral density estimation based on optimal smoothing and minimum
  statistics." *IEEE Transactions on Speech and Audio Processing* 9(5):504-512. doi:10.1109/89.928915 . Checked:
  record.
- **[Mengali1997]** Mengali, U., D'Andrea, A. N. (1997). *Synchronization Techniques for Digital Receivers.*
  Springer. doi:10.1007/978-1-4899-1807-9 . Checked: record.
- **[Meyr1998]** Meyr, H., Moeneclaey, M., Fechtel, S. A. (1998). *Digital Communication Receivers:
  Synchronization, Channel Estimation, and Signal Processing.* Wiley. doi:10.1002/0471200573 . Checked: record.
- **[Middleton1977]** Middleton, D. (1977). "Statistical-Physical Models of Electromagnetic Interference." *IEEE
  Transactions on Electromagnetic Compatibility* EMC-19(3):106-127. doi:10.1109/TEMC.1977.303527 . Checked: record.
- **[Proakis2008]** Proakis, J. G., Salehi, M. (2008). *Digital Communications*, 5th ed. McGraw-Hill. Checked:
  record (from memory).
- **[Raheli1995]** Raheli, R., Polydoros, A., Tzou, C.-K. (1995). "Per-Survivor Processing: a general approach to
  MLSE in uncertain environments." *IEEE Transactions on Communications* 43(2/3/4):354-364. doi:10.1109/26.380054 .
  Checked: record.
- **[RifeBoorstyn1974]** Rife, D., Boorstyn, R. (1974). "Single tone parameter estimation from discrete-time
  observations." *IEEE Transactions on Information Theory* 20(5):591-598. doi:10.1109/TIT.1974.1055282 . Checked:
  record.
- **[Rohling1983]** Rohling, H. (1983). "Radar CFAR Thresholding in Clutter and Multiple Target Situations." *IEEE
  Transactions on Aerospace and Electronic Systems* AES-19(4):608-621. doi:10.1109/TAES.1983.309350 . Checked:
  record.
- **[SimonAlouini2004]** Simon, M. K., Alouini, M.-S. (2004). *Digital Communication over Fading Channels*, 2nd ed.
  Wiley. doi:10.1002/0471715220 . Checked: record.
- **[StreitBarrett1990]** Streit, R. L., Barrett, R. F. (1990). "Frequency line tracking using hidden Markov models."
  *IEEE Transactions on Acoustics, Speech, and Signal Processing* 38(4):586-598. doi:10.1109/29.52700 . Checked:
  record.
- **[Turin1960]** Turin, G. (1960). "An introduction to matched filters." *IRE Transactions on Information Theory*
  6(3):311-329. doi:10.1109/TIT.1960.1057571 . Checked: record.
- **[Urkowitz1967]** Urkowitz, H. (1967). "Energy detection of unknown deterministic signals." *Proceedings of the
  IEEE* 55(4):523-531. doi:10.1109/PROC.1967.5573 . Checked: record.
- **[VanTrees1968]** Van Trees, H. L. (1968). *Detection, Estimation, and Modulation Theory, Part I.* Wiley
  (reprinted 2001, doi:10.1002/0471221082). Checked: record.
- **[Verdu1998]** Verdú, S. (1998). *Multiuser Detection.* Cambridge University Press. Checked: record (from
  memory).
- **[Watterson1970]** Watterson, C., Juroshek, J., Bensema, W. (1970). "Experimental Confirmation of an HF Channel
  Model." *IEEE Transactions on Communication Technology* 18(6):792-803. doi:10.1109/TCOM.1970.1090438 . Checked:
  record (the model as specified in [ITU-R-F1487], read).
- **[Zhidkov2008]** Zhidkov, S. V. (2008). "Analysis and comparison of several simple impulsive noise mitigation
  schemes for OFDM receivers." *IEEE Transactions on Communications* 56(1):5-9. doi:10.1109/TCOMM.2008.050391 .
  Checked: record.

### 6.5 Filtering, tracking and change detection

- **[AdamsMacKay2007]** Adams, R. P., MacKay, D. J. C. (2007). "Bayesian Online Changepoint Detection."
  arXiv:0710.3742 . Checked: abstract.
- **[Arulampalam2002]** Arulampalam, M. S., Maskell, S., Gordon, N., Clapp, T. (2002). "A tutorial on particle
  filters for online nonlinear/non-Gaussian Bayesian tracking." *IEEE Transactions on Signal Processing*
  50(2):174-188. doi:10.1109/78.978374 . Checked: record.
- **[BarShalom2001]** Bar-Shalom, Y., Li, X.-R., Kirubarajan, T. (2001). *Estimation with Applications to Tracking
  and Navigation.* Wiley. doi:10.1002/0471221279 . Checked: record.
- **[Basseville1993]** Basseville, M., Nikiforov, I. V. (1993). *Detection of Abrupt Changes: Theory and
  Application.* Prentice Hall. Checked: record (from a published review found on Crossref).
- **[BlomBarShalom1988]** Blom, H. A. P., Bar-Shalom, Y. (1988). "The interacting multiple model algorithm for
  systems with Markovian switching coefficients." *IEEE Transactions on Automatic Control* 33(8):780-783.
  doi:10.1109/9.1299 . Checked: record (IMM as described in [Murphy2002], read).
- **[ChenLiu2000]** Chen, R., Liu, J. S. (2000). "Mixture Kalman Filters." *Journal of the Royal Statistical
  Society, Series B* 62(3):493-508. doi:10.1111/1467-9868.00246 . Checked: record.
- **[Doucet2000]** Doucet, A., Godsill, S., Andrieu, C. (2000). "On sequential Monte Carlo sampling methods for
  Bayesian filtering." *Statistics and Computing* 10(3):197-208. doi:10.1023/A:1008935410038 . Checked: record.
- **[Murphy2002]** Murphy, K. P. (2002). *Dynamic Bayesian Networks: Representation, Inference and Learning.* Ph.D.
  dissertation, University of California, Berkeley. https://www.cs.ubc.ca/~murphyk/Thesis/thesis.pdf . Checked: text
  (sections 2.3.12, 2.3.14, 2.4.5, 4.3).
- **[Page1954]** Page, E. S. (1954). "Continuous Inspection Schemes." *Biometrika* 41(1-2):100-115.
  doi:10.1093/biomet/41.1-2.100 . Checked: record.
- **[Sarkka2013]** Särkkä, S. (2013). *Bayesian Filtering and Smoothing.* Cambridge University Press.
  doi:10.1017/CBO9781139344203 ; https://users.aalto.fi/~ssarkka/pub/cup_book_online_20131111.pdf . Checked: text
  (section 10.4).

### 6.6 Music rhythm and human timing

- **[Cemgil2000]** Cemgil, A. T., Kappen, B., Desain, P., Honing, H. (2000). "On tempo tracking: Tempogram
  Representation and Kalman filtering." *Journal of New Music Research* 29(4):259-273.
  doi:10.1080/09298210008565462 . Checked: record.
- **[CemgilKappen2003]** Cemgil, A. T., Kappen, B. (2003). "Monte Carlo Methods for Tempo Tracking and Rhythm
  Quantization." *Journal of Artificial Intelligence Research* 18:45-81. doi:10.1613/jair.1121 ; arXiv:1106.4863 .
  Checked: abstract.
- **[Gibbon1977]** Gibbon, J. (1977). "Scalar expectancy theory and Weber's law in animal timing." *Psychological
  Review* 84(3):279-325. doi:10.1037/0033-295X.84.3.279 . Checked: record.
- **[Gouyon2006]** Gouyon, F., Klapuri, A., Dixon, S., Alonso, M., Tzanetakis, G., Uhle, C., Cano, P. (2006). "An
  experimental comparison of audio tempo induction algorithms." *IEEE Transactions on Audio, Speech, and Language
  Processing* 14(5):1832-1844. doi:10.1109/TSA.2005.858509 . Checked: record.
- **[Krebs2015]** Krebs, F., Böck, S., Widmer, G. (2015). "An Efficient State-Space Model for Joint Tempo and Meter
  Tracking." *Proceedings of the 16th ISMIR Conference*, Málaga, pp. 72-78.
  https://www.ismir2015.uma.es/articles/239_Paper.pdf . Checked: text (sections 2 and 3).
- **[Raphael2002]** Raphael, C. (2002). "A hybrid graphical model for rhythmic parsing." *Artificial Intelligence*
  137(1-2):217-238. doi:10.1016/S0004-3702(02)00192-3 . Checked: record.
- **[WingKristofferson1973]** Wing, A. M., Kristofferson, A. B. (1973). "Response delays and the timing of discrete
  motor responses." *Perception & Psychophysics* 14(1):5-12. doi:10.3758/BF03198607 . Checked: record.

### 6.7 Confidence, calibration and decision with rejection

- **[Brier1950]** Brier, G. W. (1950). "Verification of forecasts expressed in terms of probability." *Monthly
  Weather Review* 78(1):1-3. doi:10.1175/1520-0493(1950)078<0001:VOFEIT>2.0.CO;2 . Checked: record.
- **[Chow1970]** Chow, C. K. (1970). "On optimum recognition error and reject tradeoff." *IEEE Transactions on
  Information Theory* 16(1):41-46. doi:10.1109/TIT.1970.1054406 . Checked: abstract.
- **[Gneiting2007]** Gneiting, T., Raftery, A. E. (2007). "Strictly Proper Scoring Rules, Prediction, and
  Estimation." *Journal of the American Statistical Association* 102(477):359-378.
  doi:10.1198/016214506000001437 . Checked: record.
- **[Guo2017]** Guo, C., Pleiss, G., Sun, Y., Weinberger, K. Q. (2017). "On Calibration of Modern Neural Networks."
  *Proceedings of the 34th International Conference on Machine Learning*, PMLR 70:1321-1330.
  https://proceedings.mlr.press/v70/guo17a.html . Checked: abstract.
- **[MurphyWinkler1987]** Murphy, A. H., Winkler, R. L. (1987). "A General Framework for Forecast Verification."
  *Monthly Weather Review* 115(7):1330-1338. doi:10.1175/1520-0493(1987)115<1330:AGFFFV>2.0.CO;2 . Checked: record.
- **[Naeini2015]** Pakdaman Naeini, M., Cooper, G., Hauskrecht, M. (2015). "Obtaining Well Calibrated Probabilities
  Using Bayesian Binning." *Proceedings of the AAAI Conference on Artificial Intelligence* 29(1).
  doi:10.1609/aaai.v29i1.9602 . Checked: record.
- **[NiculescuMizil2005]** Niculescu-Mizil, A., Caruana, R. (2005). "Predicting good probabilities with supervised
  learning." *Proceedings of the 22nd International Conference on Machine Learning*, pp. 625-632.
  doi:10.1145/1102351.1102430 . Checked: record.
- **[Platt1999]** Platt, J. C. (1999). "Probabilistic outputs for support vector machines and comparisons to
  regularized likelihood methods." In Smola, A., Bartlett, P., Schölkopf, B., Schuurmans, D. (eds.), *Advances in
  Large Margin Classifiers*, MIT Press (2000), pp. 61-74. Checked: record (from memory).
- **[ZadroznyElkan2002]** Zadrozny, B., Elkan, C. (2002). "Transforming classifier scores into accurate multiclass
  probability estimates." *Proceedings of the 8th ACM SIGKDD Conference*, pp. 694-699. doi:10.1145/775047.775151 .
  Checked: record.

### 6.8 Evaluation, statistics and experimental design

- **[BenjaminiHochberg1995]** Benjamini, Y., Hochberg, Y. (1995). "Controlling the False Discovery Rate: A
  Practical and Powerful Approach to Multiple Testing." *Journal of the Royal Statistical Society, Series B*
  57(1):289-300. doi:10.1111/j.2517-6161.1995.tb02031.x . Checked: record.
- **[BergstraBengio2012]** Bergstra, J., Bengio, Y. (2012). "Random Search for Hyper-Parameter Optimization."
  *Journal of Machine Learning Research* 13(10):281-305. https://www.jmlr.org/papers/v13/bergstra12a.html . Checked:
  abstract.
- **[BisaniNey2004]** Bisani, M., Ney, H. (2004). "Bootstrap estimates for confidence intervals in ASR performance
  evaluation." *Proc. ICASSP 2004*, vol. 1, pp. I-409-412. doi:10.1109/ICASSP.2004.1326009 . Checked: record.
- **[CawleyTalbot2010]** Cawley, G. C., Talbot, N. L. C. (2010). "On Over-fitting in Model Selection and Subsequent
  Selection Bias in Performance Evaluation." *Journal of Machine Learning Research* 11:2079-2107.
  https://www.jmlr.org/papers/v11/cawley10a.html . Checked: abstract (first sentences).
- **[EfronTibshirani1993]** Efron, B., Tibshirani, R. J. (1993). *An Introduction to the Bootstrap.* Chapman &
  Hall. doi:10.1007/978-1-4899-4541-9 . Checked: record.
- **[GillickCox1989]** Gillick, L., Cox, S. J. (1989). "Some statistical issues in the comparison of speech
  recognition algorithms." *Proc. ICASSP 1989*, pp. 532-535. doi:10.1109/ICASSP.1989.266481 . Checked: record.
- **[Hastie2009]** Hastie, T., Tibshirani, R., Friedman, J. (2009). *The Elements of Statistical Learning*, 2nd ed.
  Springer. doi:10.1007/978-0-387-84858-7 . Checked: record.
- **[Holm1979]** Holm, S. (1979). "A simple sequentially rejective multiple test procedure." *Scandinavian Journal
  of Statistics* 6(2):65-70. Checked: record (from memory).
- **[Koehn2004]** Koehn, P. (2004). "Statistical Significance Tests for Machine Translation Evaluation." *Proceedings
  of EMNLP 2004*, pp. 388-395. https://aclanthology.org/W04-3250/ . Checked: record.
- **[Levenshtein1966]** Levenshtein, V. I. (1966). "Binary codes capable of correcting deletions, insertions, and
  reversals." *Soviet Physics Doklady* 10(8):707-710. Checked: record (from memory).
- **[McKay1979]** McKay, M. D., Beckman, R. J., Conover, W. J. (1979). "Comparison of Three Methods for Selecting
  Values of Input Variables in the Analysis of Output from a Computer Code." *Technometrics* 21(2):239-245.
  doi:10.1080/00401706.1979.10489755 . Checked: record.
- **[WagnerFischer1974]** Wagner, R. A., Fischer, M. J. (1974). "The String-to-String Correction Problem." *Journal
  of the ACM* 21(1):168-173. doi:10.1145/321796.321811 . Checked: record.
- **[WichmannHill2001]** Wichmann, F. A., Hill, N. J. (2001). "The psychometric function: I. Fitting, sampling, and
  goodness of fit." *Perception & Psychophysics* 63(8):1293-1313. doi:10.3758/BF03194544 . Checked: record.

### 6.9 Real-time systems

- **[Liu1991]** Liu, J. W. S., Lin, K.-J., Shih, W.-K., Yu, A. C., Chung, J.-Y., Zhao, W. (1991). "Algorithms for
  scheduling imprecise computations." *Computer* 24(5):58-68. doi:10.1109/2.76287 . Checked: record.
- **[Tatbul2003]** Tatbul, N., Çetintemel, U., Zdonik, S., Cherniack, M., Stonebraker, M. (2003). "Load Shedding in
  a Data Stream Manager." *Proceedings of VLDB 2003*, pp. 309-320. doi:10.1016/B978-012722442-8/50035-5 . Checked:
  record.
- **[Zilberstein1996]** Zilberstein, S. (1996). "Using Anytime Algorithms in Intelligent Systems." *AI Magazine*
  17(3):73-83. doi:10.1609/aimag.v17i3.1232 . Checked: record.
