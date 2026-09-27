# Classical (non-neural) machine decoding of Morse code: literature notes

Scope: probabilistic and signal-processing approaches to automatic Morse (CW) decoding, judged for a multi-channel skimmer whose per-station input is 1500 sample/s complex baseband limited to about ±150 Hz.

Labels: **[fact]** = documented fact from a primary source (or the primary source's own abstract); **[secondhand]** = reported by a secondary source, or a summary of a long scanned document that I could not check line by line; **[inference]** = my own reasoning from the cited material.

Method note: I read the full OCR text of Bell's 1975 thesis and the abstract and OCR of his 1977 dissertation through an automated summarizer, not page by page. The numbers taken from the 1975 thesis are internally consistent and plausible. The numbers from the 1977 dissertation are less certain and are flagged below.

---

## 1. Early machine recognition of hand-sent Morse (1950s–1970s)

### Takeaway
Gold's MAUDE (MIT Lincoln Lab, built 1959) and Guenther's PDP-12 program (AFIT, 1973) were rule-based duration classifiers. They kept adaptive running averages of mark and space lengths and added a little code and language knowledge on top. They tackled timing variability in hand-sent code, not weak-signal detection. MAUDE is reported to have decoded 90–95% of 184 operators. [Corrected 2026-09-27: 184 *messages* from 53 operators (about 45,000 characters), scored as the share of messages under 6% character error: 89.1% with per-operator fixed thresholds, 96.2% with thresholds updated every 100 marks or spaces; no noise condition stated. MAUDE also applied fixed order-statistics rules before any running-average threshold — see ../../gold-1959-notes.md §2.2, §2.4]

### Cited Findings
- **[fact]** Bernard Gold, "Machine recognition of hand-sent Morse code," *IRE Transactions on Information Theory*, 1959. The paper is paywalled at IEEE; this is the abstract-level record. [Resolved 2026-09-27: full text now read — see ../../gold-1959-notes.md] — [IEEE Xplore](https://ieeexplore.ieee.org/abstract/document/1057478/); [Semantic Scholar](https://www.semanticscholar.org/paper/Machine-recognition-of-hand-sent-Morse-code-Gold/3b8be4ea9582f9fa6f84666d08b2cf397d9200f3)
- **[secondhand]** MAUDE (Morse AUtomatic DEcoder) was a transistorized special-purpose digital computer designed and built by March 1959. It decoded hand-sent Morse and printed the text on a teletypewriter. It made decisions at several levels, using knowledge of the relative lengths of dots and dashes, of the Morse code itself, and of "certain elementary properties of language." It reportedly decoded between 90% and 95% of 184 operators. (This comes from a search summary of the abstract; I could not open the full text.) [Corrected 2026-09-27: the abstract does say "184 operators," but the paper's body defines the test as 184 messages from 53 operators; 89.1% / 96.2% of messages were under a 6% character-error cutoff for two threshold schemes (Table IV, p. 23) — see ../../gold-1959-notes.md §2.4] — [IEEE Xplore abstract](https://ieeexplore.ieee.org/abstract/document/1057478/)
- **[secondhand]** Gold started the work at Lincoln Laboratory in 1955, and the ETHW biography calls it one of the first practical applications of what later came to be called artificial intelligence. — [ETHW: Bernard Gold](https://ethw.org/Bernard_Gold); [NAE Memorial Tributes](https://www.nationalacademies.org/read/11912/chapter/25)
- **[fact]** J. A. Guenther, "Machine Recognition of Hand-Sent Morse Code Using the PDP-12 Computer," AFIT thesis, Dec 1973 (DTIC AD0786492). Its aim was to find an optimum decision algorithm. It includes an extensive analysis of hand-sent timing data and compares several recognition algorithms. — [DTIC](https://apps.dtic.mil/sti/citations/AD0786492); [transcription](http://alanpich.github.io/Morse-Code-Recognition/)
- **[fact]** How Guenther's program works:
  - A pulse longer than the running average pulse length is a dash; otherwise it is a dot.
  - Spaces go through two thresholds in turn: symbol vs. character, then character vs. word.
  - The averages are initialized from the first 49 pulses and then updated continuously to follow speed changes.
  - A pulse shorter than half the dot average is thrown out as noise.
  - When characters run together, the largest internal space is reclassified as a character break and the pieces are decoded again.
  - A language heuristic applies a stricter word-space threshold after I, J, Q, U, V, and Z, because those letters rarely end English words.

  No quantitative results appear in that section. — [Code Translation Section](http://alanpich.github.io/Morse-Code-Recognition/computer-recognition-program/program-description/code-translation-section.html)
- **[fact]** In Guenther's design, the signal-processing front end turns the analog signal into a list of mark and space durations. That is, it makes hard decisions before any code-level reasoning happens. — [Signal Processing Section](http://alanpich.github.io/Morse-Code-Recognition/computer-recognition-program/program-description/signal-processing-section.html)
- **[secondhand]** TF3LJ/VE2AO reproduced Guenther's method on an Arduino Uno. — [TF3LJ page](https://sites.google.com/site/lofturj/14-machine-recognition-of-hand-sent-morse-code)

### Inferences
- **[inference]** These systems threshold the envelope first and then classify durations, so they lose information at low SNR (signal-to-noise ratio). One noise spike that splits a dash, or fills a gap, breaks the element sequence. Bell's later work was designed to remove exactly this weakness.
- **[inference]** Running averages of marks and spaces are cheap per channel, a few operations per element. They make a reasonable fallback or initialization stage for speed estimation, but they are not a weak-signal detector.

### Gaps
- I could not get Gold's full paper: the exact MAUDE decision rules, how "90–95% of operators" was scored, and the SNR conditions (probably clean signals) are unknown. [Resolved 2026-09-27: rules, scoring and conditions are now documented; no SNR or receiver is stated anywhere in the paper, consistent with clean keyed input — see ../../gold-1959-notes.md §2.2, §2.4]
- I found no numbers from Guenther on accuracy or on the statistics of hand-sent timing.
- I did not find other 1960s–80s machine-Morse papers in the time available. Candidates to look for include work by Blair and by Freudberg, and Mills's statistics on Morse elements. Mills is cited secondhand below.

---

## 2. HMM / Viterbi / trellis approaches (Bell 1975, 1977) and how the keying process is modeled

### Takeaway
E. L. Bell, at the Naval Postgraduate School, is the key classical reference.
- His 1975 Engineer's thesis uses Kalman filtering and smoothing of the keying waveform, followed by a Viterbi decoder over a first-order Markov model of element types. Measured on its own, this Viterbi stage improved bit error rate by only about 1.2–2.5×.
- His 1977 PhD dissertation poses the problem as optimal Bayesian estimation. The state combines keystate, encoder and source memory, and data rate (speed). One Kalman filter runs for each hypothesis path on a growing, pruned trellis, and it tracks the channel amplitude. This is the model that AG1LE later implemented in fldigi.

### Cited Findings — Bell 1975 thesis ("Processing of the manual Morse signal using optimal linear filtering, smoothing and decoding," NPS, Engineer's degree, advisor S. Jauregui)
- **[fact]** Baseband keying model: x(k+1) = x(k) + w(k), with x ∈ {0 = space, 1 = mark} and w a random forcing term that models keying transitions. The observation is z(k) = x(k) + v(k). The variance of the forcing term is conditioned on the current state and on mark/space transition probabilities, and those probabilities depend on how long the element has lasted so far. The noise variance R is estimated online. — [Bell 1975 full text](https://archive.org/stream/processingofmanu00bellpdf/processingofmanu00bell_djvu.txt)
- **[fact]** Duration model:
  - Dot and element space: uniform around a mean T, with tolerance ε.
  - Dash and letter space: uniform around a mean T_d, with tolerance δ.
  - Word space: exponential beyond about 5T.

  Dots and dashes are assumed equally likely, and T and T_d are estimated sequentially from the signal. — [Bell 1975](https://archive.org/stream/processingofmanu00bellpdf/processingofmanu00bell_djvu.txt)
- **[fact]** Pre-detection (IF) Kalman filter: the state is the in-phase and quadrature pair (a sin ωt + b cos ωt). The observation is weighted by the estimated probability that the signal is present, and that probability is held at 0.5 during spaces so the filter can pick up the next onset. — [Bell 1975](https://archive.org/stream/processingofmanu00bellpdf/processingofmanu00bell_djvu.txt)
- **[fact]** A fixed-interval forward–backward smoother was used, at 500 samples/s baseband with a smoothing window of N = 250 samples. — [Bell 1975](https://archive.org/stream/processingofmanu00bellpdf/processingofmanu00bell_djvu.txt)
- **[fact]** Viterbi stage:
  - States: dot, dash, element space, letter space.
  - Transition probabilities: first-order Markov, taken from telegraph code statistics. Bell notes that a third-order model had better structure but was not the one implemented.
  - Branch likelihoods: built from soft "figures of merit," scaled 0–1, for "mark lasted a dot length" and "mark lasted a dash length."
  - Cost: −Σ[ln P(x_i|x_{i−1}) + ln P(z_i|x_i)].
  - Long marks can optionally be split into three segments so that noise-split dashes can be recovered.

  — [Bell 1975](https://archive.org/stream/processingofmanu00bellpdf/processingofmanu00bell_djvu.txt)
- **[fact]** Post-detection results, 35 WPM, SNR measured in 2 kHz:

  | SNR (2 kHz) | Bit error | Letter error |
  |---|---|---|
  | 6 dB | 0.83% | 9% |
  | 5 dB | 3.0% | 27% |
  | 4 dB | 6.7% | 49% |
  | 3 dB | 35% | 96% |

  At 25 WPM, 6 dB gave 0.77% bit error and 10% letter error. Adding a 100 Hz bandpass pre-filter reached about 10% letter error at about −7 dB (2 kHz), and the Q estimation ran away at about −9 to −12 dB. — [Bell 1975](https://archive.org/stream/processingofmanu00bellpdf/processingofmanu00bell_djvu.txt)
- **[fact]** Viterbi gain over the smoothed output at 35 WPM: 0.83% → 0.35% bit error at 6 dB, 3.0% → 2.5% at 5 dB, and no gain at 4 dB (6.7% → 6.8%). Bell observes that the Viterbi stage stops helping once bit error exceeds about 3%, and that most remaining errors are isolated dots inserted into word spaces. — [Bell 1975](https://archive.org/stream/processingofmanu00bellpdf/processingofmanu00bell_djvu.txt)
- **[fact]** Pre-detection Kalman filter with a 100 Hz bandpass ahead of it, 25 WPM, 4000 Hz sampling, 1000 Hz carrier: 0% bit error from −11 to −13 dB (2 kHz), and about 1% bit error (about 10% projected letter error) at −14 and −15 dB. Without the 100 Hz filter, 2% bit error appeared at −2 dB. — [Bell 1975](https://archive.org/stream/processingofmanu00bellpdf/processingofmanu00bell_djvu.txt)
- **[fact]** Compute cost on an XDS-9300: about 4 s of processing per 1 s of signal at baseband, and about 30 s per 1 s pre-detection. Bell judged real-time operation unrealistic without parallel hardware. — [Bell 1975](https://archive.org/stream/processingofmanu00bellpdf/processingofmanu00bell_djvu.txt)
- **[fact]** Human baseline in the same thesis: at 0 dB SNR in the signal bandwidth, "good" operators had 10–15% error; at 3 dB, 5–10%; at 6 dB, nearly perfect copy. — [Bell 1975](https://archive.org/stream/processingofmanu00bellpdf/processingofmanu00bell_djvu.txt)

### Cited Findings — Bell 1977 PhD dissertation ("Optimal Bayesian estimation of the state of a probabilistically mapped memory-conditional Markov process with application to manual Morse decoding," NPS)
- **[fact]** From the abstract: the hand-keyed Morse signal on a noisy channel is modeled as a system whose state evolves as a memory-conditioned probabilistic mapping of a conditional Markov process. Decoding is finding the optimal estimate of that state from the measurements. For the parameter-conditional linear-Gaussian channel, the optimal decoder is "a denumerable but exponentially expanding set of linear Kalman filters operating on a dynamically evolving trellis." It was evaluated by simulation on random-letter text, and Bell says linguistic and format models for non-random text need further research. — [Internet Archive record](https://archive.org/details/optimalbayesiane00bell)
- **[secondhand]** The model in more detail, from an automated summary of the dissertation's OCR:
  - State s_k = [x_k (keystate 0/1), a_k (encoder memory: which element within the letter), ξ_k (source memory: previous letters)].
  - A control vector u_k models the data rate or speed, sending anomalies, and encoding errors.
  - Speed follows a conditional Markov chain P(u_k | u_{k−1}, a_{k−1}, ξ_{k−1}), with a table of symbol-conditional speed transition probabilities.
  - Measurement: z_k = H(s_k) y_k + n_k, where y_k is a Gauss–Markov amplitude state for fading and power variation.
  - Each path has its own Kalman filter, and the path likelihood is computed from that filter's innovation and innovation covariance.
  - Element durations are conditioned on symbol type, on the instantaneous data rate, and on time already spent in the current state.

  — [Bell 1977 OCR text](https://archive.org/stream/optimalbayesiane00bell/optimalbayesiane00bell_djvu.txt)
- **[secondhand, low confidence]** The same summary reports: an idealized, synchronized decoder at about 12 dB Eb/N0 giving letter error around 10⁻⁵; a realistic decoder giving 1–10% letter error "at moderate SNR"; and lab operators doing about 2–3 dB better than field operators. The summary also puts letter entropy at about 5.17 bits for independent letters versus about 2.14 bits for English with context. I could not check these values against the scanned figures, so they should be confirmed before anyone quotes them. [Corrected 2026-09-27: the 10⁻⁵ letter-error claim is FALSE — the idealized decoder gives about 2% letter error at E/N₀ = 12 dB per dot (key-on energy per dot over one-sided noise density), lower bound about 0.7%; the 10⁻⁵ is probably Table IV's single-element envelope-detection error at S₁₀₀ = 12 dB (key-on, 100 Hz). The other three values are roughly true or confirmed with caveats — see ../../bell-1977-notes.md §6.7] — [Bell 1977 OCR text](https://archive.org/stream/optimalbayesiane00bell/optimalbayesiane00bell_djvu.txt)

### Cited Findings — AG1LE implementation of Bell 1977 (fldigi "Bayesian" / bmorse)
- **[fact]** AG1LE (Mauri Niininen) ported Bell's Fortran algorithms to C, about 3,335 lines, with Bell's permission. He calls the method a "correlator-estimator": every possible sequence of keystate transitions is hypothesized and correlated with the input, and the most likely sequence is output. — [AG1LE Part 1](http://ag1le.blogspot.com/2013/09/new-morse-decoder-part-1.html)
- **[fact]** The element states tracked are P(dit), P(dah), P(element space), P(character space), P(word space), and P(pause). — [AG1LE Part 1](http://ag1le.blogspot.com/2013/09/new-morse-decoder-part-1.html)
- **[fact]** The code is organized into modules: a noise estimator, which AG1LE says does not track the real noise level well; Kalman filters that give the likelihood of each keystate; Bayesian posterior updates for each new path, which had intermittent overflows; and a speed tracker limited to 8–80 WPM. — [AG1LE Part 2](http://ag1le.blogspot.com/2013/12/new-morse-decoder-part-2.html)
- **[fact]** In testing, CER (character error rate, measured by Levenshtein distance) was computed on 200 words of 5-character random groups over −10 to +20 dB SNR in 2 kHz. CER fell sharply above about 6 dB, but a residual CER of about 3% remained at high SNR and its cause was unresolved. Timing variance and fading had not yet been tested. — [AG1LE Part 1](http://ag1le.blogspot.com/2013/09/new-morse-decoder-part-1.html); [AG1LE Part 2](http://ag1le.blogspot.com/2013/12/new-morse-decoder-part-2.html)
- **[fact]** A 2014 multi-signal demo decoded 9 signals between 400 and 1200 Hz at 20–35 WPM, but no accuracy figures were published. — [AG1LE Part 5](http://ag1le.blogspot.com/2014/07/new-morse-decoder-part-5.html)

### Cited Findings — later HMM-labeled work
- **[secondhand]** Wang, Zhao et al., "Automatic Morse Code Recognition under Low SNR" (MECAE 2018, Atlantis Press). Search snippets say it combines signal processing with deep learning, and mention HMM and GMM (Gaussian mixture model) as common audio-recognition baselines and training at 8 dB. The PDF did not extract, so I could not verify the method or the results. [Corrected 2026-09-27: every recognizer in the paper is an HMM; monophone GMM-HMM, triphone GMM-HMM and DNN-HMM are the paper's own compared systems, not background baselines. Best result 1.1% word error rate (DNN-HMM, whole-word units) at 8 dB SNR and 18% at −3 dB; SNR bandwidth and key-on/average definition not stated; 2020 synthetic clips — see ../../wang-yfdm-notes.md §a] — [Atlantis Press](https://www.atlantis-press.com/proceedings/mecae-18/25893679)

### Inferences
- **[inference]** A "Bell-style" state for implementation is: keystate × element-type memory (about 6 classes) × discrete speed state × (optionally) letter context. The duration model sits inside the transition probabilities, which depend on the time already spent in the current element. Measuring that time in dit units at the current speed hypothesis makes the model an explicit-duration (semi-Markov) HMM.
- **[inference]** In Bell 1975 the gain comes mostly from better detection (the narrow pre-filter plus the pre-detection Kalman filter), not from the Viterbi code model. The Viterbi stage helps only when bit error is already under about 3%. So a skimmer should spend its design effort on the soft likelihood front end, and let the sequence decoder consume soft per-sample likelihoods rather than hard marks.
- **[inference]** Bell's 4000 Hz / 100 Hz pre-filter setup is structurally close to the skimmer's input, which is 1500 S/s complex limited to about ±150 Hz. The pre-detection Kalman filter over the I/Q pair maps directly onto complex baseband samples.
- **[inference]** Tracking one Kalman amplitude filter per surviving path gives natural QSB (fading) tracking. Per-channel cost is roughly (paths kept) × (successors per path) × (a scalar or 2×2 Kalman update) per sample. With aggressive pruning and decimation to around 100–200 S/s, which is still several samples per dit at 40 WPM, this should be feasible for many channels on modern CPUs, unlike Bell's 1975 hardware.

### Gaps
- The exact trellis size, pruning rule, number of speed states, and speed-change probabilities in Bell 1977 and in AG1LE's code were not verified here. They would need to come from the dissertation's Section VIII or from the bmorse/fldigi source.
- I found no independent, peer-reviewed benchmark of Bell-style decoders against human operators or against CW Skimmer.

---

## 3. Bayesian / particle-filter / Kalman tracking of speed, amplitude, and keystate

### Takeaway
The verified classical Bayesian lineage is Bell (1975, 1977), followed by AG1LE's port and by VE3NEA's CW Skimmer. The prompt asked for a Bell "A Bayesian approach..." paper; that exact title does not appear to exist. The actual title is the 1977 NPS dissertation named in section 2, and it comes from the Naval Postgraduate School, not MIT or Bell Labs. I found no published Morse-specific particle-filter paper.

### Cited Findings
- **[fact]** Bell's 1977 dissertation (Naval Postgraduate School, PhD, Sept 1977) is the thesis AG1LE builds on. — [Internet Archive](https://archive.org/details/optimalbayesiane00bell); [AG1LE Part 1](http://ag1le.blogspot.com/2013/09/new-morse-decoder-part-1.html)
- **[fact]** CW Skimmer (VE3NEA, Afreet Software) uses a CW decoding algorithm "based on the methods of Bayesian statistics" and decodes all signals in the passband at once. It is reported to handle up to about 700 signals on a 3 GHz Pentium 4. — [Wikipedia: CW Skimmer](https://en.wikipedia.org/wiki/CW_Skimmer); [CW Skimmer manual](http://dxatlas.com/CWSkimmer/Files/CwSkimmer.pdf)
- **[secondhand]** AG1LE quotes VE3NEA's advice: put all prior knowledge in the form of probabilities, and use observed data to update them. For example, compute the probability that a signal is present instead of making a hard decision at each sample, and keep combining probabilities through Bayes' rule up to word recognition. AG1LE describes the pipeline as:
  1. Signal-presence probability.
  2. Dit/dah probabilities from duration histograms.
  3. Character matching against the code book using probability vectors.
  4. Word or callsign matching against a dictionary.

  VE3NEA reportedly spent about 8 years and tried hundreds of algorithms. CW Skimmer's internal algorithm is not published. — [AG1LE: Towards Bayesian Morse Decoder](http://ag1le.blogspot.com/2013/01/towards-bayesian-morse-decoder.html)
- **[secondhand]** AG1LE also cites D. L. Mills's empirical prior probabilities for Morse elements: dots and dashes about equally likely, and a word space after a mark about 0.2 times as frequent as a character space. — [AG1LE: Towards Bayesian Morse Decoder](http://ag1le.blogspot.com/2013/01/towards-bayesian-morse-decoder.html)

### Inferences
- **[inference]** A particle filter over (keystate, time in state, dit length, amplitude) is a natural modern relaxation of Bell's pruned trellis. Speed becomes continuous instead of a set of discrete states, and amplitude can be integrated out per particle with a Kalman filter (Rao–Blackwellization). I found no literature that validates this for Morse, so it is untested design territory.

### Gaps
- I found no published Morse-specific particle filter or Kalman speed-tracker paper outside Bell.
- CW Skimmer's algorithm is proprietary, and no measured sensitivity figures from VE3NEA were found.

---

## 4. Optimal detection theory for on-off keyed (OOK) carriers in noise

### Takeaway
Textbook theory says to filter before envelope detection with a filter matched to the element length, and to use noncoherent detection unless the carrier phase can be tracked. Bell's experiments support this empirically: filtering before detection bought him roughly 7 dB or more over filtering after it.

### Cited Findings
- **[secondhand]** For noncoherent (envelope) detection of OOK/ASK, the envelope follows a Rayleigh distribution when no signal is sent and a Rician distribution when the signal is present. The optimum threshold is where the two densities cross, and detection probability is expressed with the Marcum Q-function. — [Academia.edu: Noncoherent envelope detection of ASK](https://www.academia.edu/12903043/Analysis_of_the_Probability_of_Error_in_Noncoherent_Envelope_Detection_of_an_ASK_Waveform); [Univ. of Michigan EECS 555, Ch. 5 Noncoherent Receivers](http://www.eecs.umich.edu/courses/eecs555/chap5.pdf) (identified by search, not read in full)
- **[fact]** Bell's own comparison of filtering before and after detection:
  - Post-detection filtering with smoothing, 2 kHz input: about 10% letter error at around +6 dB.
  - The same with a 100 Hz pre-filter: about 10% letter error at around −7 dB.
  - Pre-detection I/Q Kalman filter with a 100 Hz pre-filter: about 1% bit error at −14 dB.

  All SNRs are measured in 2 kHz. — [Bell 1975](https://archive.org/stream/processingofmanu00bellpdf/processingofmanu00bell_djvu.txt)

### Inferences
- **[inference]** Why filtering before detection matters: an envelope or square-law detector at low input SNR suffers a "small-signal suppression" loss, because the noise × noise term dominates the output. Narrowing the bandwidth before detection raises SNR at the detector input and reduces this loss. Bell's results fit that picture.
- **[inference]** Matched-filter bandwidth by speed, using the PARIS dit length T = 1.2/WPM seconds:
  - 20 WPM: T = 60 ms, noise bandwidth about 1/T ≈ 17 Hz.
  - 40 WPM: T = 30 ms, about 33 Hz.

  Relative to a 2.5 kHz reference bandwidth, a matched dit filter at 20 WPM gains about 10·log10(2500/17) ≈ 21.7 dB. So "−15 dB in 2.5 kHz" is about +7 dB per dit (Es/N0 in the dit bandwidth). A coherent OOK detector needs roughly 11–13 dB Es/N0 for about 1e-3 symbol error, and a noncoherent one needs slightly more. A human-quality decode of about 1–5% CER at −15 dB/2.5 kHz and 20 WPM therefore relies on soft sequence decoding and priors, not on per-dit hard decisions. (These are standard textbook numbers from memory; check them against Proakis or Van Trees before quoting.)
- **[inference]** Coherent detection needs carrier phase tracking. Hand-keyed signals with chirp, drift, and ionospheric phase variation make that fragile. Bell's I/Q Kalman filter is a partly coherent tracker: its a, b states follow the slowly varying in-phase and quadrature components. On 1500 S/s complex input, a practical front end could be:
  1. A complex filter matched to the dit, a few tens of Hz wide, possibly a bank of filters for different speeds.
  2. |·|² or |·| detection.
  3. Soft likelihood ratios per sample from Rician-versus-Rayleigh densities, using an estimated amplitude and noise floor.
  4. An HMM/trellis over key states.

### Gaps
- I did not retrieve the specific Van Trees or Proakis pages, so the textbook error-rate formulas above are from memory and labeled inference. A closed-form "SNR limit for CW at N WPM" was not found in the Morse literature itself.

---

## 5. Language and text priors (dictionaries, callsign structure, n-grams)

### Takeaway
Language knowledge has been part of machine Morse decoding since the beginning:
- Gold used "elementary properties of language."
- Guenther used letter-ending heuristics.
- Bell 1975 used a first-order Markov model of element types.
- Bell 1977 built in slots for letter-level Markov source models but tested only random text.
- VE3NEA reportedly carries probabilities up through word and callsign matching.

No published quantitative measurement of how much an n-gram or callsign prior improves CW decoding was found.

### Cited Findings
- **[secondhand]** MAUDE used knowledge of the Morse code and of "certain elementary properties of language." [Confirmed 2026-09-27: the concrete language rule is S6 (five or more successive E/T characters are rare in English); error rates on English and cipher text were about equal (Table III), so it made no measurable difference — see ../../gold-1959-notes.md §2.2] — [IEEE Xplore abstract](https://ieeexplore.ieee.org/abstract/document/1057478/)
- **[fact]** Guenther applied a stricter word-space threshold after I, J, Q, U, V, and Z. — [Code Translation Section](http://alanpich.github.io/Morse-Code-Recognition/computer-recognition-program/program-description/code-translation-section.html)
- **[fact]** Bell 1975's Viterbi stage used Markov transition probabilities over element types taken from telegraph statistics. — [Bell 1975](https://archive.org/stream/processingofmanu00bellpdf/processingofmanu00bell_djvu.txt)
- **[fact]** Bell 1977 states that linguistic and format-dependent models for non-random text need further research. — [Internet Archive abstract](https://archive.org/details/optimalbayesiane00bell)
- **[secondhand]** CW Skimmer's final stage reportedly matches character probability sequences against a dictionary or callsign corpus. — [AG1LE: Towards Bayesian Morse Decoder](http://ag1le.blogspot.com/2013/01/towards-bayesian-morse-decoder.html)

### Inferences
- **[inference]** For a skimmer, the most useful prior is the structure of callsigns and contest or QSO exchanges: prefix tables, digit positions, CQ/TEST/DE/5NN/TU templates. It fits naturally as a character-level weighted automaton inside beam search. The downside is hallucinated calls on weak signals, so priors should raise confidence without inventing text. (No measured evidence was found in the classical literature.)

### Gaps
- No classical paper was found that measures the CER gain from n-gram or dictionary priors in Morse decoding.

---

## 6. Robustness, computational cost, and performance summary per approach

### Takeaway
| Approach | Weak signals | Speed change / poor fist | QSB | Cost per channel | Evidence |
|---|---|---|---|---|---|
| Adaptive-threshold duration classifiers (Gold, Guenther) | Poor: hard decisions | Good, via running averages and heuristics | Weak | Negligible | MAUDE 90–95% of operators (clean) [Corrected 2026-09-27: 89–96% of 184 messages from 53 operators under a 6% character-error cutoff; "clean" is inferred, not stated — see ../../gold-1959-notes.md] |
| Kalman smoothing + element Viterbi (Bell 1975) | About −7 dB/2 kHz post-detection; −14 dB/2 kHz pre-detection (25 WPM, simulated perfect code) | Online estimates of T, T_d | Partial | 4–30× real time on 1970s hardware | Simulation only |
| Bell 1977 Bayesian trellis + per-path Kalman (AG1LE port) | CER falls sharply above about 6 dB/2 kHz in AG1LE's test, with about 3% floor | Speed states on the trellis (8–80 WPM) | Amplitude state per path (designed for it, not measured) | Paths × successors × Kalman update per sample | Simulation and informal tests |
| CW Skimmer (proprietary Bayesian) | Widely regarded as sensitive; no published figures | Unknown | Unknown | About 700 signals on a 3 GHz P4 | Vendor and Wikipedia claims |

Sources: [IEEE Xplore (Gold)](https://ieeexplore.ieee.org/abstract/document/1057478/); [Bell 1975](https://archive.org/stream/processingofmanu00bellpdf/processingofmanu00bell_djvu.txt); [Bell 1977](https://archive.org/details/optimalbayesiane00bell); [AG1LE Part 1](http://ag1le.blogspot.com/2013/09/new-morse-decoder-part-1.html); [AG1LE Part 2](http://ag1le.blogspot.com/2013/12/new-morse-decoder-part-2.html); [Wikipedia: CW Skimmer](https://en.wikipedia.org/wiki/CW_Skimmer)

### Cited Findings
- (See the per-section citations above. The table only condenses them.)

### Inferences
- **[inference]** For a 1500 S/s, ±150 Hz per-station stream, the most implementable classical design with evidence behind it is a Bell-1977-style explicit-duration HMM:
  - State: keystate/element × time-in-state (in dit units) × discrete speed.
  - Observation: soft likelihoods from a dit-matched complex filter plus envelope, with a per-path or per-channel Kalman amplitude and noise tracker.
  - Search: beam-pruned Viterbi or forward search.
  - Priors: a character/callsign model layered on top.

  Bell's own results suggest most of the sensitivity comes from the pre-detection filtering and soft likelihoods, and less from the code-level sequence model.

### Gaps
- There are no head-to-head, independently measured sensitivity numbers (CER vs. SNR in a defined bandwidth, with real fists, QSB, and QRM) for any classical decoder, including CW Skimmer. Every Bell result is from simulation with either "perfect code" or random letters.
