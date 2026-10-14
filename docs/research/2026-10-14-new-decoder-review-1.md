# Independent review of the new decoder's design notes (review 1)

> **Committed 2026-10-14** (owner's approval) from `build/design-review/`, where it was written. The text below is
> the reviewer's, unchanged; only this note is added. The two scripts it names are committed beside it, in
> `docs/research/2026-10-14-new-decoder-review-1/`, with their "Run:" lines updated to that path. Their printed
> outputs are not committed: rerunning each script (numpy only, about a minute) reproduces them exactly (checked
> 2026-10-14). Line numbers ("L123") refer to the design notes at commit 99ae649. The review's corrections of labels
> and facts were applied in d85a9e7; its other recommendations are listed as open in the notes' section "After
> review 1".

- **Reviewed:** `docs/design/2026-10-09-new-decoder-design-notes.md` (266 lines, 21 980 bytes; topics 1–6
  concluded), branch `bell-stage-1`, commit 99ae649. "L123" below means line 123 of that file.
- **Reviewer:** an independent Claude agent, 2026-10-14. Read-only on the repository.
- **Supporting files** (all in `build/design-review/`, git-ignored, not committed):
  - `derivations.py` with its output `derivations-output.txt` (numpy only; about a minute to run);
  - `sim_amplitude.py` with its output `sim_amplitude-output.txt` (the amplitude estimator of topic 3, simulated).
- **Context read:** `docs/signal-processing.md` (sections 0–9, A.7, the bank's noise), the project spec
  `docs/design/2026-09-25-kz4ap-skimmer-design.md` (§5, §6), `docs/research/bell-1977-reference.md` (structure and
  receiver chain), `docs/research/deepcw-generator-notes.md`, `docs/research/proakis-ook-notes.md`,
  `training/kz4ap_synth/keying.py`, `engine/src/morse.cpp`, `docs/plans/2026-10-06-development-set-redesign.md`, and
  the crossing table of `docs/plans/2026-10-06-development-set-results.md`.

## Summary verdict

**The design's skeleton is sound and mostly standard, but several labels overstate their support, a few numbers do
not match the project's own channel or test set, and a builder would stall on some missing pieces.**

1. **Standard where it says so (verified):**
   - Viterbi beam search and its token-passing form;
   - merging hypotheses in the same state;
   - beam plus maximum-count ("histogram") pruning;
   - tuning the beam by search errors;
   - noncoherent (unknown-phase) detection;
   - segment scoring in explicit-duration models;
   - class-based models, interpolation and cache language models;
   - the language-model weight.

   Every citation checked is real and correctly dated (Rabiner 1989; Young, Russell and Thornton 1989; Lowerre
   1976; Raheli, Polydoros and Tzou 1995; Brown et al. 1992; Jelinek and Mercer 1980; Kuhn and De Mori 1990).
2. **The derived claims the owner asked about hold:**
   - deciding dot or dash when a mark ends is an exact rearrangement of the standard explicit-duration model;
   - Viterbi merging loses no correction of the best story;
   - the noise-correlation factor 1.7–1.8 is right for an ideal 300 Hz channel;
   - the Farnsworth 7.8 is right;
   - the catch-all bound is right, under conditions the notes do not state.
3. **Ten claims are mislabeled** (presented as standard, derived or sourced when they are not, or not as stated).
   The ones that matter:
   - "true probabilities by summing over the alternatives kept" is not true under Viterbi merging (L170–171);
   - the catch-all weight 5% is labeled derived but is a choice (L241–242);
   - "Rabiner's scaling" is misattributed (L169–170);
   - per-survivor processing is listed as a standard part, but the design does not use it (L146);
   - the fist's provenance "from VE3NEA's analysis of real keying" is unsupported (L91).

   The full list is in "Mislabeled or unlabeled items". There are also two small factual slips: 4.6 dB, and 15
   punctuation marks (there are 14).
4. **Three findings change the design's numbers or scope** (details under Derivations and Consistency):
   - **Coherent whole-mark sums need a frequency reference the design does not name.** The channel leaves the
     station up to ±11.7 Hz off 0 Hz. Even the spec's ±2 Hz target loses 13 dB of signal power on a 5 WPM dash
     and 19 dB on an 8 WPM dash, relative to a centered station. The notes treat this as a risk to measure in
     "the channelizer's frequency hold", but the channelizer has no frequency hold.
   - **The Farnsworth grid stops at 8, but the development set's Farnsworth group I2 reaches 15.7.** In I2's cells
     at 5–8 WPM overall, about 29% of draws exceed 8 for letters at 18–25 WPM and about 90% for letters at
     25–33 WPM: about a fifth of the group.
   - **On the real channel filter the noise factor is 1.9–2.1, not 1.7–1.8.** The filter's noise bandwidth is
     252 Hz, not an ideal 300 Hz. The design's rule (compute from the noise per hertz) handles this, but only if
     the builder uses the real filter's correlation.
5. **Fourteen (d) items have no derivation and no measurement plan** (listed under Numbers and settings). Four more
   say "to be measured" but give no method.
6. **The largest omissions:**
   - frequency re-centering and drift;
   - fading and loss of phase coherence within slow marks;
   - other signals and impulsive noise in the channel;
   - unfair beam comparisons between hypotheses mid-mark or mid-letter and finished ones;
   - the coupling between the birth probability ε and the beam (a birth survives only if ε exceeds 1/beam);
   - the models of the non-Morse states;
   - the priors the first prototype needs even without a text model.

## Terms used in this review

- **Hypothesis / story:** one candidate account of what was sent (elements, speed, letters). **Score:** under the
  Viterbi algorithm, the probability of that hypothesis's single best story together with the signal so far.
- **Viterbi algorithm:** finds the single most probable story by keeping, for each possible state, only the best
  story that reaches it. **Forward algorithm:** the same recursion with sums in place of maxima. It gives the total
  probability of all stories reaching a state, which is what posterior probabilities need. **Posterior:** the
  probability of something (a character, a speed) given the signal.
- **Beam pruning:** dropping hypotheses whose score is below the best by more than a set factor (the beam).
  **Histogram pruning:** keeping at most a set number of hypotheses. **Search error:** a result that pruning changed,
  compared with a search that pruned nothing.
- **Token passing:** a way of organizing Viterbi beam search in which each hypothesis is a "token" carrying its
  score and a link back to its history.
- **Explicit-duration (semi-Markov) model:** a hidden Markov model in which each state lasts a duration drawn from
  its own distribution, instead of the memoryless geometric duration of an ordinary hidden Markov model.
  **Segment model:** the same, with the observations of a whole segment scored jointly, not sample by sample.
- **Noncoherent detection:** deciding whether a carrier of unknown phase is present. The optimum score depends on
  the magnitude of the matched filter's output through the Bessel function I₀ (Rician against Rayleigh densities).
- **Per-survivor processing (PSP):** in a Viterbi decoder, each surviving path carries its own estimate of an
  unknown channel parameter (gain, phase, frequency), updated from that path's own decisions.
- **Coherence time / Doppler spread (f_D):** fading changes a carrier's phase and amplitude. Over a time much
  shorter than about 1/f_D they stay nearly constant (coherent); over longer times they do not.
- **Class-based language model:** words are grouped into classes. The model gives the probability of the next
  class, then of the word within its class. **Interpolation:** mixing two models with fixed weights.
  **Cache model:** raising the probability of recently seen words. **Language-model weight:** an exponent on the
  text model's probability. **Insertion penalty:** a fixed factor per word (or letter) that compensates for that
  weight.
- **Classes of a number** (as the owner set them): (a) derived, (b) measured or to be measured with a stated method,
  (c) the owner's decision, (d) a guess with no derivation and no measurement plan.

## Standard-practice claims

Every claim in the notes that something is standard, textbook or established, plus the attributed provenances. The
verdicts: **Correct**, **Correct, used loosely**, **Partly** (true only under conditions the notes do not state) or
**Wrong / unsupported**. Where a source's wording was checked, the check is stated.

| # | Claim (line) | Verdict | Source |
|---|---|---|---|
| S1 | Viterbi beam search is "the standard decoder of speech recognition (Rabiner 1989)" (L165–166) | **Correct** for hidden-Markov-model recognizers (1980s to the WFST decoders still in use). Rabiner's tutorial names "various forms of Viterbi beam searches" as the way to search large networks (§VI, citing Lowerre and Reddy). | L. R. Rabiner, *Proc. IEEE* 77(2):257–286, 1989; text checked in the copy at https://people.engr.tamu.edu/rgutier/web_courses/cpsc689_s07/rabiner1989tutorialHMM.pdf. H. Ney, S. Ortmanns, *Proc. IEEE* 88(8):1224–1240, 2000, doi:10.1109/5.880081 |
| S2 | Token passing, Young, Russell and Thornton 1989 (L166–167) | **Correct.** Cambridge University Engineering Dept. report CUED/F-INFENG/TR38, 31 July 1989. | https://mi.eng.cam.ac.uk/reports/abstracts/young_tr38.html |
| S3 | Scores are best-story probabilities. They need not sum to 1, since only the best story is asked for (L167–169). | **Correct.** This is Rabiner's δ_t(i), the best path's joint probability with the observations (eqs. 30–33). | Rabiner 1989, §III.B |
| S4 | Scores rescaled every piece by dividing by the best: "Rabiner's 'scaling'" (L169–170) | **Operation correct, attribution wrong.** Dividing all scores by one number changes no comparison (derived). Rabiner's scaling divides the *forward* variables by their *sum* (eq. 92b), for forward–backward. For Viterbi he writes "no scaling is required if we use logarithms" (eq. 105). Normalizing to the best is ordinary beam-search practice. | Rabiner 1989 §V.A (both passages checked verbatim in the copy above). Ney and Ortmanns: prune if Q < f·Q_best |
| S5 | "True probabilities (per character, speed) are computed separately, by summing over the alternatives kept" (L170–171) | **Wrong as stated.** Viterbi merging discards the merged stories' probability. The sum of kept Viterbi scores is a sum of maxima, not the total probability. Standard routes: the forward recursion (sum instead of max at each merge, over the same pruned states), or forward–backward over a lattice. Topic 8 is where this is designed, but the notes already call the result "true". | Rabiner 1989 §III.A (forward procedure), eqs. 18–21. F. Wessel et al., "Confidence measures for large vocabulary continuous speech recognition", *IEEE Trans. Speech Audio Process.* 9(3):288–298, 2001. Word posteriors computed on word graphs outperform other confidence measures. https://www-i6.informatik.rwth-aachen.de/publications/singlebibtex.php?id=162 |
| S6 | Merging = "the Viterbi algorithm's merging step (standard)" (L180–181) | **Correct.** Bellman's principle of optimality; in token passing each state keeps its best token. It holds only if "state" includes everything the future depends on (see Derivations, D14). | Rabiner 1989 eq. 33; Young et al. 1989 |
| S7 | "No correction is lost" by merging (L181–182) | **Correct for the best story.** It stays correct under pruning, because a merged-away hypothesis would never outrank its survivor. Not correct for probabilities (S5). | Derived, D14 |
| S8 | A birth is a transition with probability ε from every hypothesis. "Under Viterbi the newborn takes the best of these, so its score is the best current score times ε (standard)" (L188–189) | **Partly.** The algebra is right, and it mirrors token passing's word-end to word-start step, which propagates the best token. But an ε-restart from every state is our model choice, not a standard component, and the notes miss that it only works if ε exceeds 1/beam (D15). | Young et al. 1989 (word-link propagation) |
| S9 | Beam search, Lowerre 1976 (L193) | **Correct.** B. T. Lowerre, *The HARPY Speech Recognition System*, PhD thesis, Carnegie Mellon University, 1976. | https://en.wikipedia.org/wiki/Beam_search ; https://purl.stanford.edu/rq916rn6924 |
| S10 | Set the beam by measuring search errors: a very wide beam as the reference, narrowed until results differ at most once per 1 000 letters (L197–199) | **Correct as practice.** Recognizers choose the beam where "the number of recognition errors is virtually not increased" (Ney and Ortmanns). The formal search-error definition compares the decoded result's score with the reference transcript's. The notes' wide-beam comparison is the usual practical proxy. The reference run must also lift the ceiling and the "non-negligible branch" threshold. | Ney and Ortmanns 1999 preprint, text checked: https://www.isip.piconepress.com/publications/journals_refereed/1999/ieee_spmag/search/_ARCHIVE/dp_search/paper_v0.pdf |
| S11 | The maximum count is "histogram pruning", standard (L202) | **Correct.** Ney and Ortmanns: "This pruning method is called histogram pruning". | V. Steinbiss, B.-H. Tran, H. Ney, "Improvements in beam search", ICSLP 1994, https://www.isca-archive.org/icslp_1994/steinbiss94_icslp.html; Ney and Ortmanns (text checked) |
| S12 | The noncoherent matched filter is textbook (L145). The score is the standard carrier-of-unknown-phase-plus-noise against noise-alone statistic (L138–139). | **Correct.** The optimum noncoherent detector: matched filter, then magnitude, then I₀. Conditions: the phase is constant over the sum, and the noise variance of the sum is right. The first fails without re-centering (D7) and in fading (D8). | J. G. Proakis, M. Salehi, *Digital Communications*, 5th ed., §4.5-1, eq. 4.5-21; project check in `docs/research/proakis-ook-notes.md` §2.2 |
| S13 | Segment scoring in hidden semi-Markov models (L146) | **Correct.** Rabiner's explicit-duration model allows a joint observation density over a state's whole duration (§IV.D: "observations O₁ … O_d are chosen according to the joint observation density"). Segment models generalize it. | Rabiner 1989 §IV.D (checked); M. Ostendorf, V. Digalakis, O. Kimball, *IEEE Trans. Speech Audio Process.* 4(5):360–378, 1996, doi:10.1109/89.536930; S.-Z. Yu, *Artificial Intelligence* 174(2):215–243, 2010 |
| S14 | Per-survivor estimates (Raheli, Polydoros and Tzou, 1995) listed as a standard part (L146) | **Citation right, applicability wrong.** PSP is "a general approach to MLSE [maximum-likelihood sequence estimation] in uncertain environments": each survivor estimates unknown channel parameters from its own decisions. This design has no per-survivor parameter estimate: amplitude and noise are one per channel (topic 3, L105–106), and the carrier phase is integrated out by I₀, not estimated. Each hypothesis's running sum is simply the segment model's statistic (S13). | R. Raheli, A. Polydoros, C.-K. Tzou, *IEEE Trans. Commun.* 43(2/3/4):354–364, 1995 (bibliographic details confirmed through citing works found by search) |
| S15 | Noise per hertz, measured on gaps: "standard" (L149) | **Principle correct; method ours.** Estimating noise density on signal-free data is standard. Choosing the data by "the pieces that the hypotheses agree are gaps" (L119) is decision-directed and our own. It has a selection bias this project has already measured: the bank's mask reads the noise low by a factor 0.78–0.84. The bank deliberately kept keying decisions out of its noise estimate. | `docs/signal-processing.md` §8c "Noise" |
| S16 | Class-based language models (Brown et al., 1992) (L235–236) | **Correct, used loosely.** *Computational Linguistics* 18(4):467–479, 1992. Brown et al. learn word classes by clustering. Here the five kinds are hand-defined classes with their own letter models. Same idea; say "hand-defined classes". | https://aclanthology.org/J92-4003/ |
| S17 | Interpolation with a uniform distribution (Jelinek and Mercer, 1980) (L236) | **Correct, used loosely.** In *Pattern Recognition in Practice* (Gelsema and Kanal, eds.), North-Holland, 1980, pp. 381–397. Jelinek–Mercer weights are *estimated on held-out data* (by the forward–backward algorithm). The notes fix the catch-all weight at 5% by hand and call it derived (M4). | Confirmed by search; e.g. the citing survey https://arxiv.org/pdf/cmp-lg/9406027 |
| S18 | A finite-state grammar for callsign patterns (L237) | **Correct** (callsign structure is a regular language). | — |
| S19 | A cache language model (Kuhn and De Mori, 1990) for a per-channel mix of kinds (L240) | **Correct citation, used as a variant.** *IEEE Trans. PAMI* 12(6):570–583, June 1990. Their cache raises recently used *words*. Caching each kind's share is an adaptive class prior: a variant, to be labeled so. | https://en.wikipedia.org/wiki/Cache_language_model |
| S20 | The "standard 'language model weight' of speech recognition" (L242–243) | **Correct but incomplete.** HTK's "grammar scale factor" is tuned *together with* a word insertion penalty on development data. The notes omit the penalty (a letter or word penalty here). The catch-all bound becomes w^λ for weight λ (D12). | HTK Book, "Recognising the test data": https://www.seas.ucla.edu/spapl/weichu/htkbook/node44_ct.html |
| S21 | Fist form "as the generator's styles, from VE3NEA's analysis of real keying" (L90–91) | **Unsupported provenance.** The project's VE3NEA notes say only his *character and word statistics* come from on-air CW Skimmer decodes. His keying parameters have no stated source, and one of them "looks like an arbitrary choice rather than a measurement". Log-normal timing is a reasonable choice (manta and the bank also use it), not an established result. | `docs/research/deepcw-generator-notes.md` §1.2; `docs/research/research_notes/CW decoder algorithms survey/neural_decoders.md` (DeepCW entry) |
| S22 | "The top value is about the slowest Farnsworth practice in use (letters at 18 WPM, overall 5 WPM)" (L85–86) | **Arithmetic correct; "slowest in use" unsupported.** 7.84 follows from the ARRL timing standard (Bloom, KE3Z, QEX April 1990, as in the generator). W1AW's schedule gives the speeds (5, 7½, 10, 13, 15 WPM) but no character speed. The project's own Farnsworth test group uses letters up to 33 WPM at 5–8 WPM overall (factors up to 15.7). | `training/kz4ap_synth/keying.py` `farnsworth_gap_s`; https://ARRL.ORG/w1aw-operating-schedule ; dev-set plan, group I2 |
| S23 | Treating correlated samples as independent was "the mistake of Bell's receiver chain" (L153) | **Correct.** Bell's 5 ms samples are correlated 0.70 at lag 1, and his decoder treats them as independent. | `docs/research/bell-1977-reference.md` (c), "Noise statistics" |

**Items on the requested checklist that the notes do not call standard.**

- **Filler or garbage models** appear only in substance: the "nonsense keying" state (L51) and the catch-all
  word (L234). These are the standard keyword-spotting device (R. C. Rose, D. B. Paul, "A hidden Markov model based
  keyword recognition system", ICASSP 1990, pp. 129–132; https://dblp.org/pid/92/968), and citing it would help,
  but no model is specified for either (Omissions, O6).
- **Decision-directed or EM amplitude estimation** is what topic 3's estimate is (L113–115), but the notes call it
  "the owner's tap idea" and claim nothing standard for it. EM (expectation-maximization) re-estimates parameters
  from probability-weighted data. Its fixed point is consistent when the model is right (D5). Its behavior near
  threshold contradicts L114–115 (D6).

## Mislabeled or unlabeled items

### Mislabeled: presented as standard, derived or sourced, but not (or not as stated) — 10 items

| # | Line | What the notes say | What is true |
|---|---|---|---|
| M1 | L169–170 | Rescaling by the best score is "Rabiner's 'scaling'" | Rabiner's scaling divides forward variables by their sum, for forward–backward. For Viterbi he prescribes logarithms (S4). The operation itself is fine. |
| M2 | L170–171 | "True probabilities … by summing over the alternatives kept" | An approximation, not true probabilities. Viterbi merging has already discarded the merged stories' probability (S5, D14). This matters for the spec's Hybrid mode, which "depends on trustworthy confidence" (spec §5.1). |
| M3 | L146 | "Per-survivor estimates (Raheli, Polydoros and Tzou, 1995)" as a standard part used here | The design estimates nothing per hypothesis: amplitude and noise are per channel (L105–106), and the phase is integrated out. The citation describes something the design does not do (S14). |
| M4 | L241–242 | "(w = 5%: 1/20; derived)" | Only the *bound* is derived (an unusual word is lowered at most to w times its no-text-model probability). The *value* 5% is a choice. Standard practice (Jelinek–Mercer) estimates w on held-out text (S17). Class (d) unless the owner decides it. |
| M5 | L90–91 | Log-normal fist "as the generator's styles, from VE3NEA's analysis of real keying" | No source says VE3NEA's keying parameters come from real keying (S21). The form matches the generator but the medians do not: the generator's hand style has dashes and letter gaps of 4.48 dits and word gaps of 7.39 dits, not 3, 3 and 7 (`training/kz4ap_synth/keying.py`). |
| M6 | L85–86 | The top Farnsworth factor 8 is "about the slowest Farnsworth practice in use" | No source found. The project's own Farnsworth group I2 reaches factor 15.7 (S22, D9). |
| M7 | L109–110 | "Every element … lasts at least 12 ms, so the tap reaches the full A on every mark (derived)" | Derived for nominal timing only. Under the notes' own fist (σ = 0.3, L93) 27% of 100 WPM dots and 9% of 80 WPM dots are shorter than the 10 ms tap. The generator's key imbalance (±0.1 dit) also shortens marks. Through the real channel the tap's peak is within 0.3 dB of A² (signal power) on marks of 12 ms or more, but 0.8–1.0 dB below it on a 10 ms mark (D4). |
| M8 | L114–115 | "Gaps get weights near zero, so the estimate freezes during gaps and silence" | True above about S₅₀₀ = +5 dB SNR in 500 Hz. False near the decoding threshold, where it matters: a noise-only tap gets a mean weight of 0.12 at 0 dB and 0.25 at −3 dB SNR in 500 Hz, so 2–4 s of silence refill a 0.5 s window with noise (D6). |
| M9 | L158 | "The channelizer's frequency hold is to be measured before relying on it" | The channelizer holds nothing. It centers each channel on the nearest bin, leaving up to ±11.7 Hz, and never moves (signal-processing.md A.7). What needs measuring is the re-centering the new decoder will use, and the notes do not choose one (O1). |
| M10 | L188–189 | ε-births: the newborn's score "is the best current score times ε (standard)" | The Viterbi arithmetic is standard. The ε-restart model is ours, and it fails silently unless ε exceeds 1/beam (S8, D15). |

**Small numeric or factual slips**

- **m1 (L110–111):** "about 4.6 dB less noise than the raw channel (derived)". The real channel gives 4.40 dB and an
  ideal 300 Hz channel gives 5.07 dB (SNR gain of the 15-sample tap over one channel sample, as a power ratio; D3).
  4.6 dB is 10·log₁₀(300 Hz / 104 Hz), the bank's 9.6 ms branch, not the 10 ms tap. Descriptive only; no decision
  rests on it.
- **m2 (L252–253):** "15 punctuation marks". `engine/src/morse.cpp` and `training/kz4ap_synth/morse.py` have 14
  (. , ; : ? ! ' " ) / - $ @ _) and 12 prosigns.
- **m3 (L78):** "5–100 WPM". The ladder's dits run from 12.00 ms to 230.3 ms, i.e. 100 to 5.21 WPM.
- **m4 (L196):** "a tenth of a 0.5–1% floor". 0.1% is a tenth to a fifth of it. The floor itself matches measured
  records (bank about 0.5% at high S₅₀₀; Bell port 0.55–0.82%), but the notes cite none.
- **m5 (L205–206):** the spec's 2–3 million operations per channel-second is an *estimate* ("not yet measured",
  spec §5.1). The notes make it a target. That is fine as an owner's decision, but say so.

### Unlabeled items

The notes' own rule (L23–24) gives every conclusion a class. These have none.

- **U1 (L113–115):** the amplitude estimator's form. It is a soft-decision moment estimator, EM-like and heuristic.
  It needs a prior P(key-down), which is not stated (D5).
- **U2 (L188):** the ε-restart transition from every hypothesis. A model choice.
- **U3 (L85):** the count (5) and spacing (×1.68) of the Farnsworth factors.
- **U4 (L91–92):** the "slightly heavy tail": its form and weight.
- **U5 (L119–120):** what "the hypotheses agree are gaps" means. All live hypotheses? Weighted by score? The best
  only? With what margin around marks?
- **U6 (L175–176):** "A branch is created only where the fist model gives it a non-negligible probability". A pruning
  threshold with no value, and not part of the search-error measurement.
- **U7 (L186–187):** "wherever a mark plausibly begins". The trigger for rescue births.
- **U8 (L122–123):** the start-of-over amplitude taken from the detector. The detector's level is a 1 s average
  including key-up time, which reads 3.6–7.1 dB below the key-down SNR per bin (signal-processing.md §5). A
  conversion is needed and is not stated.
- **U9 (L82):** "Drift within a word is absorbed by the fist scatter". Heuristic.
- **U10 (L138–140 with L149–151):** an unlabeled approximation. Scoring each mark's sum separately ignores the noise
  correlation (about 4 ms) across a mark's edges with the neighboring gap pieces. A small effect, but an
  approximation.
- **U11 (L238–239):** the fixed mix of kinds is "counted from independent text". The blend of contest and
  conversation text that fixes the mix is an unstated choice.

## Numbers and settings

The classes are as defined in "Terms used in this review". A value is (c) only where the notes or the record name
the owner for it. A topic closed "(owner, date)" with a value marked heuristic and no plan stays (d): the owner asked
for these to be listed so they can get a plan.

| Value or setting (line) | Class | Note |
|---|---|---|
| 32 speeds, dots 10% apart, 5–100 WPM (L77–78) | (c) + (a) | The bank's ladder ("ratio and speed range owner", signal-processing.md §8c). Adequacy derived: the largest grid error is ±4.88% (0.048 in ln), far inside the half-margins 0.55 (×3) and 0.42 (×7/3) in ln (D10). The range is really 5.21–100 WPM (m3). |
| "A 10% speed error is small … far inside the margins" (L78–79) | (a) | Correct, and conservative: the worst grid error is 4.9%, not 10%. |
| Speed changes only at word boundaries (L81) | (c) | Matches the generator, which changes speed only between words. Real operators: unknown. |
| One ladder step (±10%) per word (L81) | (b) | Stated check: the development set's speed-change group D2 (steps of ×1.3 to ×2.0, i.e. 2.8 to 7.3 ladder steps at once); allow two steps if too slow. |
| Probability of a speed change per word boundary ("small", L81–82) | **(d)** | "Heuristic, to be measured", but no value and no method for the value. The D2 check tests the step size, not this probability. |
| Farnsworth factors 1, 1.68, 2.83, 4.76, 8.00: five values, ratio 8^(1/4) (L85) | **(d)** | Count and spacing are not derived. At ×1.68 per step, a factor midway between grid values is off by 0.26 in ln on every gap (0.87 σ at σ = 0.3). |
| Top Farnsworth factor 8 (L85–87) | (a) for 7.84; **(d)** as a range | 7.84 is right (D9). Whether 8 is enough is unsupported, and the development set's I2 group reaches 15.7 (D9). |
| Farnsworth factor changes only at word boundaries, by one step (L87) | (c) structure; **(d)** probability | The probability of a step is not stated. |
| Fist form: log-normal, ideal × random factor (L90–91) | (c) | A reasonable modeling choice. Its provenance is mislabeled (M5). |
| "Slightly heavy tail" (L91–92) | **(d)** | No form (Student-t? mixture with a wide component?) and no weight. |
| Ideal lengths 1, 3, 7 dots (L92) | (a) | Standard Morse timing (ITU-R M.1677-1). The generator's hand style differs (1, 4.48, 1, 4.48, 7.39). |
| σ ≈ 0.3 for marks and for gaps, natural log (L92–94) | **(d)** | "Heuristic", with no plan for its value. The two-class trigger (L94–95) concerns adding classes, not σ. Also, the unit (natural log) is not stated. |
| Two fist classes "if the development set shows precise signals losing noticeably" (L94–95) | (b), weak | Trigger stated; "noticeably" undefined. |
| Tap = 5 pieces = 10 ms (L108–109) | (a) | Matched to the 12 ms dot at 100 WPM. Through the real channel filter its peak is within 0.3 dB of A² on marks of 12 ms or more (D4). The owner's idea (c). |
| "Every element ≥ 12 ms … the tap reaches the full A on every mark" (L109–110) | (a), nominal timing only | See M7. |
| Tap noise "about 4.6 dB less" than the raw channel (L110–111) | (a), inexact | 4.40 dB on the real channel, 5.07 dB on an ideal 300 Hz channel (m1, D3). |
| Amplitude estimate's form (L113–115) | heuristic, unlabeled | Simulated here (D5): its bias at its fixed point is −0.9 dB to +0.7 dB in A² over 12–100 WPM and 0–30 dB SNR in 500 Hz. |
| Prior P(key-down) used in the weights (not stated, L113–115) | **(d)** | Needed for the weights. The choice matters at low SNR: +0.2 dB with 0.44 against +0.7 dB with 0.2 at 0 dB SNR in 500 Hz and 12 WPM (D5). |
| 0.5 s of key-down time (L117–118) | (b), weak | "To be tested on fading (QSB)": method named, no decision rule. |
| Noise from the pieces "the hypotheses agree are gaps", per hertz (L119–120, L149–151) | (c) principle; **(d)** details | The agreement rule, margins around marks, memory, start-up and recovery: none given (U5, O10). |
| A at the start of an over from the detector's level (L122–123) | (c), conversion missing | U8. |
| Piece = 3 samples = 2 ms (L130–131) | **(d)** as labeled; derivable | "Heuristic", with no plan. A derivation exists (D11): placing edges to whole 2 ms pieces costs 0.37 dB mean and 0.72 dB worst on a 12 ms mark, 0.09/0.18 dB on 48 ms (SNR loss, power ratio). The owner can turn this into (a). |
| Noise correlated "about 3.3 ms" (L132) | (a) | Exact for an ideal 300 Hz channel. The real filter's correlation first crosses zero at 3.98 ms (D1). |
| Factor 1.7–1.8 (2.2–2.6 dB) for 8–720 ms marks, ideal 300 Hz channel (L151–152) | (a) | Confirmed: 1.65–1.82 (2.18–2.59 dB as a variance ratio). On the real filter: 1.91–2.14 (2.81–3.30 dB) (D2). |
| "Variance proportional to T, for T well above about 3 ms" (L150–151) | (a) | Within 0.32 dB (variance ratio) at 12 ms and 0.49 dB at 8 ms on the real filter. The exact sum formula is just as cheap (D2). |
| 0.5 Hz over a 720 ms dash ≈ a third of a cycle (L157) | (a) | 0.36 cycles, a loss of 1.9 dB of signal power relative to a centered station (D7). The 0.5 Hz is an example, not a measured residual. |
| "The channelizer's frequency hold is to be measured" (L158) | (b), misdirected | M9, O1. |
| Births at every ladder speed (L83, L185–187) | (c) | The speed prior at birth and the Farnsworth factor at birth are not stated: **(d)**. |
| Birth probability from silence per piece (L185–186) | **(d)** | Not stated. |
| ε, the disruption probability (L188) | **(d)** | "Small". No value, no plan, and coupled to the beam (D15). |
| "Non-negligible probability" threshold for branching (L175–176) | **(d)** | No value. Should be set by the same search-error measurement as the beam. |
| "Wherever a mark plausibly begins": rescue-birth trigger (L186–187) | **(d)** | No criterion. |
| Beam factor (L193–200) | (b) | Method and target stated. |
| At most 1 decoded change per 1 000 letters from pruning (L195–196) | (c) | Owner's target. |
| "0.5–1% floor on clean signals" (L196) | (b), uncited | Consistent with the measured records (m4). Cite them. |
| 50 ns per hypothesis per piece (L202) | (b) | An estimate, replaced by a timing on the prototype (L213). |
| About 1 700 hypotheses at 42 ms per channel-second (L203) | (a) | 1 680 (D13). |
| "A few hundred at most" hypotheses (L203–204) | (b) | To be measured (L214–215). |
| 2–3 million operations per channel-second (L205–206) | (c) | The spec's estimate, turned into a target (m5). |
| 10–25 million at the ceiling (L206–207) | (b) | Implies 12–30 operations per hypothesis per piece (D13). |
| Ceiling, floor, cost per station-second (L212–219) | (b) | Methods stated. |
| Five word kinds (L231–234) | (c) | Owner. |
| Catch-all w = 5% (L241–242) | **(d)** | Labeled derived, but only the bound is (M4, D12). |
| Text-model weight (L242–244) | (b) | Two criteria stated. Add the insertion penalty (S20). |
| Fixed mix of kinds counted from independent text (L238–239) | (b), blend unstated | U11. |
| Cache size N (L240) | (b), weak | "N is measured", no method. |
| Alphabet: letters, digits, 15 punctuation marks, 12 prosigns (L252–253) | fact, wrong | 14 punctuation marks (m2). |
| Overload governor: fewer hypotheses first, then drop stations (L209–211) | (c) | Owner. |

**The (d) items, for the owner's plans (14):**

1. Probability of a speed change per word boundary (L81–82).
2. Count and spacing of the Farnsworth factors, 5 at ×1.68 (L85).
3. Top Farnsworth factor 8 as the range limit (L85–87); the development set needs about 16.
4. Probability of a Farnsworth step per word boundary (L87).
5. Fist σ = 0.3 for marks and gaps (L92–94).
6. Form and weight of the heavy tail (L91–92).
7. Prior P(key-down) in the amplitude weights (L113–115, not stated).
8. The noise estimate's selection rule, margins and memory (L119–120).
9. Piece length 2 ms (L130–131); derivable, see D11.
10. ε, the birth (disruption) probability (L188).
11. Birth probability from silence, and the speed and Farnsworth priors at birth (L185–187).
12. The "non-negligible" branching threshold (L175–176).
13. The "plausibly begins" rescue-birth trigger (L186–187).
14. Catch-all weight w = 5% (L241–242).

**"To be measured" without a stated method or decision rule (4):** the 0.5 s amplitude window (L117–118); cache size
N (L240); "noticeably" for fist classes (L94–95); the blend of independent text that sets the fixed mix
(L238–239).

## Derivations checked

Scripts: `build/design-review/derivations.py` (D1–D4, D7–D11, D13) and `build/design-review/sim_amplitude.py` (D5).
D6 is a short computation with the same tap model. The real channel filter was rebuilt from A.7: a
Blackman-windowed sinc, 4097 taps at 192 kHz, cutoff 150 Hz. It reproduces every documented response value to
0.01 dB (−6.02 dB at 150 Hz, −18.0 dB at 200 Hz, both relative to 0 Hz) and the noise bandwidth (251.9 Hz against
252 Hz).

**D1. Correlation time (L132).**
- On an ideal ±150 Hz channel the noise correlation is sinc(300 Hz·τ), first zero at 1/(300 Hz) = 3.33 ms.
  Confirmed.
- On the real filter the first zero is at 3.98 ms. The correlation at one piece (2 ms) is 0.50 on the ideal
  channel and 0.57 on the real one.

**D2. Pieces treated as independent (L149–154).**

The factor is var(sum of a mark's 3n samples) / (n · var(one piece)), as a variance ratio:

| Mark | Ideal 300 Hz channel | Real filter (252 Hz) |
|---|---|---|
| 8 ms (4 pieces) | 1.652 (2.18 dB) | 1.911 (2.81 dB) |
| 12 ms | 1.727 (2.37 dB) | 1.984 (2.98 dB) |
| 48 ms | 1.792 (2.53 dB) | 2.100 (3.22 dB) |
| 144 ms | 1.809 (2.57 dB) | 2.125 (3.27 dB) |
| 720 ms | 1.815 (2.59 dB) | 2.136 (3.30 dB) |

- The notes' 1.7–1.8 (2.2–2.6 dB) is **right for the ideal channel they name**. The real channel's factor is about
  0.6–0.7 dB larger (as a variance ratio), because its smoother skirt keeps more correlation at 2 ms.
- The design's rule (variance from the noise per hertz) gives the right answer only if the noise per hertz is
  converted to a piece's variance through the real filter.
- **"Variance proportional to T"** overstates the true variance of a short sum by 0.32 dB at 12 ms, 0.49 dB at
  8 ms, and 0.08 dB at 48 ms (real filter, variance ratio). The exact formula,
  var(n samples) = Σ_{|k|<n} (n − |k|)·R[k] with R the measured or computed autocovariance, costs nothing extra.
  Prefer it.

**D3. The tap's noise (L110–111).**
- The SNR gain of the 15-sample (10 ms) sum over one channel sample, as a power ratio: **4.40 dB on the real
  channel, 5.07 dB on an ideal 300 Hz channel.**
- The rule-of-thumb ratios are 10·log₁₀(252/100) = 4.01 dB and 10·log₁₀(300/100) = 4.77 dB. The notes' 4.6 dB
  equals 10·log₁₀(300/104), the bank's 9.6 ms branch.

**D4. Does the tap reach the full A?**

A carrier burst was put through the real channel filter, decimated at every sample phase, cut into pieces at
every piece phase, and the largest 5-piece sum taken. Results (signal power relative to A²):

| Mark | Hard edges | 2 ms raised-cosine edges (the generator's VE3NEA option) |
|---|---|---|
| 10 ms | −0.94 to −0.76 dB | −0.99 to −0.81 dB |
| 12 ms | −0.24 to −0.10 dB | −0.28 to −0.14 dB |
| 15 ms or more | +0.06 to +0.14 dB | +0.06 to +0.13 dB |

The positive values are the filter's 5% edge ringing. So the claim holds for nominal timing at up to 100 WPM. It
fails by about 1 dB for marks shortened to 10 ms, which the notes' own σ = 0.3 makes common at 100 WPM (27% of
dots).

**D5. Bias of the amplitude estimate at its fixed point** (`sim_amplitude-output.txt`).

Setup:
- Topic 3's estimator exactly as written: the posterior weight from the tap alone, Rician against Rayleigh at the
  current A, and A² = Σw(|tap|² − noise) / Σw.
- Iterated to its fixed point on random 5-letter words with exact PARIS timing, through the real filter.
- Noise level known; prior P(key-down) = 0.44.

A² bias, dB relative to the true A²:

| Speed | 0 dB | 5 dB | 10 dB | 20 dB | 30 dB (SNR in 500 Hz) |
|---|---|---|---|---|---|
| 100 WPM | −0.59 | −0.82 | −0.93 | −0.94 | −0.86 |
| 50 WPM | −0.12 | −0.35 | −0.41 | −0.41 | −0.41 |
| 25 WPM | +0.01 | −0.13 | −0.18 | −0.20 | −0.20 |
| 12 WPM | +0.19 | −0.02 | −0.06 | −0.08 | −0.09 |

- The negative bias comes from tap positions half over a mark's edge: at high SNR every partial tap above half the
  amplitude is weighted as key-down. It grows with speed.
- With prior 0.2 the 0 dB column becomes +0.20, +0.56, +0.57 and +0.72 dB: the unstated prior matters at low SNR.
- Verdict: the estimator is **sound to about ±1 dB** in A². It is consistent when the two-state model holds,
  because E[w·(|y|² − σ²)] = P(1)·A² when w is the true posterior. Its errors are edge bias and prior
  sensitivity, both small but worth stating.

**D6. Does the estimate "freeze during gaps and silence" (L114–115)?**

The mean weight given to a noise-only tap, with A set to the station's level and prior 0.44 (the tap's SNR is
S₅₀₀ + 7.4 dB, since on the real channel the tap's noise bandwidth is 91 Hz):

| S₅₀₀ (dB SNR in 500 Hz) | Mean weight per noise tap | Silence that adds 0.5 s of key-down weight |
|---|---|---|
| −3 | 0.25 | 2.0 s |
| 0 | 0.12 | 4.2 s |
| +3 | 0.028 | 18 s |
| +5 | 0.005 | 97 s |
| +10 | about 0 | never |

The existing decoders cross CER 0.10 between about −0.8 and +6.4 dB SNR in 500 Hz (Matched and the bank,
development-set record §5.2), and the noncoherent genie bound lies at −6.3 to +2.7 dB. So where a better decoder
must work, a few seconds between overs replace the amplitude estimate with a noise-driven value. The claim holds
only on strong signals.

**D7. Phase rotation and re-centering (L156–158).** "0.5 Hz over a 720 ms dash ≈ a third of a cycle" is right
(0.36 cycles). Loss of a coherent whole-mark sum, |sinc(Δf·T)|², in dB of signal power relative to a station at
0 Hz:

| Mark | 0.1 Hz | 0.2 Hz | 0.5 Hz | 1 Hz | 2 Hz | 5.9 Hz | 11.7 Hz |
|---|---|---|---|---|---|---|---|
| dash, 100 WPM (36 ms) | −0.0 | −0.0 | −0.0 | −0.0 | −0.1 | −0.7 | −2.7 |
| dash, 25 WPM (144 ms) | −0.0 | −0.0 | −0.1 | −0.3 | −1.2 | −15.4 | −16.0 |
| dash, 20 WPM (180 ms) | −0.0 | −0.0 | −0.1 | −0.5 | −1.9 | −24.7 | −26.1 |
| dash, 13 WPM (277 ms) | −0.0 | −0.0 | −0.3 | −1.1 | −4.9 | −15.0 | −23.4 |
| dash, 8 WPM (450 ms) | −0.0 | −0.1 | −0.7 | −3.1 | −19.2 | −19.5 | −27.0 |
| dash, 5 WPM (720 ms) | −0.1 | −0.3 | −1.9 | −9.4 | −13.3 | −25.6 | −28.7 |

(Non-monotone columns are sinc sidelobes.)
- Without re-centering, the bin rounding's worst offset (11.7 Hz) costs a dash 2.7 dB at 100 WPM, about 9 dB at
  60 WPM and 16–29 dB at 25 WPM and below. A typical offset (5.9 Hz) costs 15–26 dB at 25 WPM and below. Whole-mark
  sums are unusable without re-centering.
- The spec's ±2 Hz target (§5.2, sized for a dit-long filter at 20 WPM) is not enough for dash-long sums below
  about 20 WPM.
- Matched's tracker achieves 0.08–0.55 Hz RMS (simulated) and 0.11–0.13 Hz median (measured on group F). That is
  enough at good SNR. But a 1 Hz/s drift lags 1.1–1.6 Hz, which costs 4–9 dB on an 8 WPM dash and 12–18 dB on a
  5 WPM dash (signal power relative to a centered station).
- The design must name its frequency reference, and either track to a few tenths of a hertz or cap the coherent
  length (O1).

**D8. Fading inside a mark** (development-set group B2, f_D 0.05–5 Hz). The expected energy of a coherent
whole-mark sum relative to no fading, (2/T²)∫₀^T (T − τ)·ρ(τ) dτ, for a Gaussian Doppler spectrum of standard
deviation f_D/2 (approximate: VE3NEA's spectrum is Butterworth). In dB:

| Mark | f_D 0.1 Hz | 0.3 Hz | 1 Hz | 2 Hz | 5 Hz |
|---|---|---|---|---|---|
| dash, 25 WPM | −0.0 | −0.0 | −0.1 | −0.3 | −1.4 |
| dash, 13 WPM | −0.0 | −0.0 | −0.3 | −0.9 | −3.3 |
| dash, 8 WPM | −0.0 | −0.1 | −0.6 | −2.0 | −5.0 |
| dash, 5 WPM | −0.0 | −0.2 | −1.4 | −3.4 | −6.9 |

Beyond the energy loss, the sum is no longer "a carrier of amplitude A": its magnitude is Rayleigh-like, so the
Rician score is mismatched. Harmless for slow fading (f_D ≤ 0.3 Hz), significant in B2's upper cells.
The standard remedy is to sum coherently over sub-blocks shorter than the coherence time and combine the
sub-blocks noncoherently (O2).

**D9. Farnsworth.**
- The factor is F = T_g / dit = (60c/s − 37.2) / 22.8 for letters at c WPM and an overall s WPM (ARRL/Bloom, as
  in the generator).
- Letters 18 WPM at 5 WPM overall: **F = 7.84**. Confirmed.
- Letters 25 and 33 WPM at 5 WPM overall: F = 11.5 and 15.7.
- Grid: 8^(1/4) = 1.682; 1, 1.68, 2.83, 4.76, 8.00. Confirmed.
- The development set's I2 group, assuming uniform draws within cells:

| Letters \ overall | 5–8 WPM | 8–12 WPM | 12–16 WPM |
|---|---|---|---|
| 18–25 WPM | F 4.3–11.5; 29% above 8 | F 2.3–6.6; 0% | F 1.3–3.9; 0% |
| 25–33 WPM | F 6.6–15.7; 90% above 8 | F 3.9–9.2; 6% above 8 | F 2.5–5.6; 0% |

About a fifth of I2's signals lie outside the decoder's grid, if the cells are equally populated. At F = 15.7 a
letter gap (47 dits) sits closer to the model's word gap at F = 8 (56 dits; 0.17 in ln) than to its letter gap
(24 dits; 0.68 in ln). By timing, every letter gap would read as a word gap.

**D10. Speed ladder and fist margins.**
- Dits 12.00–230.3 ms (100–5.21 WPM). The worst grid error is ±4.88%.
- Timing-only confusion at σ = 0.3, for a sender as scattered as the model allows:
  - 3.4% per dot/dash or element/letter-gap decision (half-margin ln 3 / 2 = 0.549);
  - 7.9% per letter/word-gap decision (ln(7/3) / 2 = 0.424).

  This is what the text model and the Morse tree must absorb on poor fists.

**D11. Piece length.** The SNR loss from placing edges at whole-piece boundaries, for a rectangular mark with edges
at a random position within a piece, with real-filter noise, the best piece-aligned sum against the exact sum
(mean / worst, dB of SNR as a power ratio):

| Piece | 12 ms mark | 24 ms mark | 48 ms mark |
|---|---|---|---|
| 2 samples (1.33 ms) | 0.25 / 0.47 | 0.12 / 0.24 | 0.06 / 0.12 |
| **3 samples (2 ms)** | **0.37 / 0.72** | **0.18 / 0.36** | **0.09 / 0.18** |
| 4 samples (2.67 ms) | 0.48 / 0.49 | 0.25 / 0.47 | 0.12 / 0.24 |

The 2 ms piece costs at most 0.4 dB on average at 100 WPM and under 0.1 dB at 25 WPM. 1.33 ms pieces would save
about 0.1 dB at 100 WPM for 1.5× the pieces. This is the derivation the "heuristic" label (L130) lacks.

**D12. The catch-all bound (L241–242).**
- Let P₀(word) be the word's probability with no text model (the Morse tree with every symbol equally likely,
  *including the probability of the word ending*).
- If the text model is a word-level mixture, P(word) = Σ_kinds P(kind)·P(word | kind), with the catch-all kind
  equal to P₀ and given weight at least w, then P(word) ≥ w·P₀(word). **Correct**, with three conditions the notes
  do not state:
  1. **The mixture must be at the word level.** Interpolating per letter would give w^L for a word of L letters.
  2. **The catch-all must include the word-end probability.**
  3. **The bound holds only at text-model weight λ = 1.** With a weight λ on the text model it becomes w^λ, e.g.
     w² = 1/400 at λ = 2.
- Computing letter by letter is fine. P(letter | prefix) = Σ_kinds P(kind | prefix)·P(letter | prefix, kind)
  multiplies out exactly to the word-level mixture.
- If instead each hypothesis commits to one kind (as L247's merging rule implies), the best story's score is the
  maximum over kinds. That is still at least w·P₀.

**D13. CPU.**
- 42 ms / (500 pieces/s × 50 ns) = **1 680 hypotheses**. Confirmed.
- 10–25 million operations at the ceiling means 12–30 operations per hypothesis per piece. A few hundred
  hypotheses then give 1.8–4.5 million per channel-second, consistent with the 2–3 million target.
- 50 ns for 12–30 operations is 1.7–4.2 ns per operation, slow for a modern core. Either memory traffic dominates
  or one of the two estimates is off. Both are replaced by measurement (L213), so this is not a defect.

**D14. Viterbi merging loses no correction (L177–184).**

The proof:
- Two partial stories π₁ and π₂ end at time t in the same state s, with Viterbi scores V₁ ≥ V₂.
- Every continuation c multiplies both by the same factor F(c), because every future factor (durations, the
  running sum of the current mark, text-model probabilities, births, A and the noise level) depends only on s,
  on channel-level quantities and on future data.
- So V₁·F(c) ≥ V₂·F(c) for every c, and no best story at any later time passes through π₂. Dropping π₂ never
  changes the best story, at any time: **correct** (Bellman's principle).
- It also holds with pruning: π₂ always ranks below π₁, so it would be pruned no later.

Conditions:
- s must contain everything the future factors read. The list at L178–180 does: the start time of the current mark
  fixes its running sum and its duration factor. But it omits:
  - the clocks of the non-Morse states (topic 1 item 9, L67);
  - the gap kind, if it is resolved before the gap ends (L55);
  - and, if they are ever added, a per-hypothesis amplitude or cache.
- What merging *does* lose is the merged story's probability, which is why S5/M2 fail.
- "More text, more correction, fewer merges" (L183–184) is right for corrections driven by the text model.
  Corrections driven by timing reach as far back as states differ.

**D15. Births against the beam.**
- A newborn's score is the best score × ε (L188–189). The beam drops any hypothesis below the best ÷ B, for beam
  factor B.
- So a newborn survives its first piece only if **ε > 1/B**, and it must then gain on the best before the beam
  catches it.
- If the beam is narrowed by the search-error rule (L197–199) below 1/ε, rescue births are silently disabled.
  The wide-beam reference run would still have them, so the difference would surface only as unexplained
  "search errors".
- Births from silence carry the same coupling with their birth probability.
- Speech recognizers give word-start hypotheses their own pruning threshold for this reason ("language model
  pruning", Ney and Ortmanns).

**D16. Deciding dot or dash when the mark ends (L52–54).**

The standard explicit-duration model picks the label ℓ ∈ {dot, dash} at the mark's start, with probability P(ℓ)
given the tree position, then a duration d with probability p_ℓ(d) at the hypothesis's speed. The observations
during the mark are scored by the running sum, which does not depend on ℓ. A story with label ℓ and duration d
therefore scores P(ℓ)·p_ℓ(d)·E(d), where E(d) is the observation factor.

In the notes' form, one hypothesis "mark lasting so far" carries E(d). When it ends at d it branches into "dot" with
factor P(dot)·p_dot(d) and "dash" with factor P(dash)·p_dash(d). Every (ℓ, d) gets exactly the same score: an
**exact rearrangement**, under both maximum (Viterbi) and sum (forward). Half as many hypotheses live during each
mark, and the same number exist after it.

Two caveats:
- It is exact only while nothing scored during the mark depends on ℓ. That holds here: A is per channel, and there
  is no label-specific emission.
- During the mark the merged hypothesis has not yet paid any duration factor. Under a beam it is compared with
  hypotheses that already have. The fairness rule for pruning must be chosen (O4).

## Consistency

**Within the notes**

- **K1 (L69–70 against L88).** Topic 1 still says the fist habits are "per channel or per hypothesis, open (topics
  2 and 7)". Topic 2 item 4 closed this: fixed, at channel level, never learned while running.
- **K2 (L105–106 against L146).** Topic 3 says one amplitude per channel, "independent of every hypothesis".
  Topic 4 lists "per-survivor estimates" as a standard part used. They contradict each other; topic 3 is the
  decision (S14, M3).
- **K3 (L115–116 against L119–120, L149–151): decision feedback.**
  - The amplitude keeps the decoder's decisions out ("feeding back the decoder's own key-down probability is a
    later option (a loop)").
  - The noise level takes them in from the start ("the pieces that the hypotheses agree are gaps").
  - The bank deliberately avoided this ("no keying decision feeds the noise estimate", signal-processing.md §8c),
    and measured that even its level-based mask reads the noise 16–22% low.

  One policy should cover both.
- **K4 (L91–92 against the generator).** "As the generator's styles" holds for the form, not the medians: the hand
  style used in group C2 has dashes and letter gaps of 4.48 dits and word gaps of 7.39 dits. C2 therefore tests a
  model mismatch, which is a fair robustness test. The caution at L100–101 then concerns only σ.
- **K5 (L85–87 against group I2).** The grid tops out at 8; I2 reaches 15.7 (D9).
- **K6 (L167–169 against L170–171).** Scores are declared best-story values, then summed as if they were total
  probabilities (S5).
- **K7 (L178–180 against L51–67).** The merge-state list omits:
  - the non-Morse clocks (L67);
  - the gap kind if resolved early (L55);
  - a per-hypothesis cache, if one is added (L240).

  If the cache is computed per channel from published text, it is decision feedback like K3 and should be called
  so.
- **K8 (L185–187 against L87).** Births happen at every speed, but the notes do not say at which Farnsworth
  factor. Since the factor moves one step per word, a station at F = 8 born at F = 1 needs four words to be
  modeled. Births should cover all five factors (160 newborns per birth event), or the factor must be allowed to
  jump at an over's start.
- **K9 (L231–234, L241 against L247).**
  - Item 2 and item 4 read as a word-level *mixture* over kinds.
  - Item 5's merging rule ("inside a callsign … only the position in the pattern matters") implies each
    hypothesis *commits* to a kind.

  Both work (D12), but they differ in merging, CPU and the probabilities reported. Pick one.
- **K10 (L131, L152 against signal-processing.md A.7).** The derivations assume "an ideal 300 Hz channel". The
  real channel has a 252 Hz noise bandwidth and a smooth skirt, which changes the factor from 1.8 to 2.1 (D2). The
  rule at L150 covers this only if the real filter's correlation is used.
- **K11 (L108–111, trivial).** The tap is 15 samples (10.0 ms), against the bank's branch 1 at 14 samples
  (9.33 ms). The tap's noise bandwidth is 91 Hz on the real channel, not "about 100 Hz".

**Against the project spec** (the authority, per CLAUDE.md)

- **K12. Spec §5.2 step 3**, as amended 2026-10-07, still describes the Classical decoder as Bell-style: a Kalman
  amplitude per candidate, "each candidate reading the branch matched to its speed", and speed changes at element
  boundaries "up to ±20 WPM after a pause". The notes replace all three (L105–106, L133, L143–144, L81). The spec
  needs an owner-approved amendment, or the notes should state that they supersede it once the owner approves.
- **K13. Spec §5.2 step 1** makes "precise frequency re-centering" a prerequisite for matched filtering, with a
  ±2 Hz target. The notes' coherent whole-mark sums need more than that at slow speeds (D7), and no re-centering is
  chosen (O1).
- **K14. Spec §5.1:** Hybrid "depends on trustworthy confidence". With S5 unresolved, the Classical decoder's
  confidence would be an uncalibrated approximation. Topic 9 should include a calibration test (does a reported
  90% come true 90% of the time?).

## Omissions

Design questions a decoder of this kind must answer that topics 1–6 neither answer nor defer to topics 7–9. Topic 7
covers station changes between overs, topic 8 output and corrections, topic 9 testing.

- **O1. Frequency reference and drift (critical).**
  - **The gap:** coherent whole-mark sums (L135–137) need the carrier at 0 Hz to within a few tenths of a hertz at
    slow speeds (D7). The channel leaves up to ±11.7 Hz. The development set's F2 group adds drift excursions up to
    40 Hz. The notes name no frequency reference: the detector's anchor, Matched's tracker, or a per-hypothesis
    frequency.
  - **A decision consistent with topic 1's rule:** one tracker per channel, ahead of the pieces (the same for every
    hypothesis), such as Matched's (A.7; measured 0.11–0.13 Hz median on group F). Plus a cap on the coherent
    length, beyond which a mark's sub-sums are combined noncoherently, to bound the loss from residual drift.
- **O2. Phase and amplitude changes within a mark.**
  - Fading (group B2, f_D up to 5 Hz) costs 1–7 dB of coherent energy on slow dashes and makes the Rician score
    wrong (D8).
  - Transmitter chirp (a frequency shift at key-down) breaks phase coherence too.
  - Topic 3's 0.5 s window addresses only the slow change of A. Sub-block noncoherent combining, or a segment score
    with a random amplitude, is the standard answer. Not on the agenda.
- **O3. Other signals and impulsive noise in the channel.**
  - Group E2 puts a neighbor 0–25 Hz away at up to +12 dB relative to the wanted station's key-down power. Two stations within
    47 Hz share one channel (signal-processing.md §6). Callers in a pileup overlap in time (G2: 0–10 Hz).
  - Lightning crashes (QRN) are far from Gaussian.
  - The observation model (one carrier plus Gaussian noise) scores a crash as a very strong mark, and the
    gap-based noise estimate absorbs a neighbor's keying.
  - Topic 7 concerns stations *in turn*, not *at once*. No topic covers this.
- **O4. Fair comparison under the beam.**
  - Duration factors are applied when a mark or gap ends; text factors when a letter completes (L223–224).
    Hypotheses part-way through therefore look better than finished ones.
  - The standard remedies:
    - the duration factor applied incrementally (S(d)/S(d−1) per piece, the "hazard" form);
    - a look-ahead for durations;
    - "language-model look-ahead" for text: multiply in the best text probability still reachable from the
      current Morse-tree position (Ney and Ortmanns; Steinbiss et al. 1994).
  - Not chosen; it affects the beam measured in topic 5 item 4.
- **O5. Births against the beam** (D15). ε, the birth probability from silence, and the beam must be set together,
  or births need their own pruning threshold. The speed and Farnsworth priors at birth are also unstated (K8).
- **O6. The non-Morse states' models.** Topic 1 lists them (L51, L67), but nothing defines:
  - the steady carrier: its amplitude (A, or its own?), its frequency (a tune-up carrier can sit anywhere in the
    channel), its duration;
  - nonsense keying: a filler model (timing without the Morse tree? what symbol probabilities?);
  - the hazards of going from one activity to another: an over starting or ending.

  Topic 7 may cover over boundaries, but not these observation models.
- **O7. The priors the first prototype needs without a text model** (L249–250). "All symbols equally likely" fixes
  the dot/dash and letter-end probabilities at each tree node (derivable). It does not fix:
  - P(word gap) after a letter: a word-length distribution (VE3NEA's on-air table is in the repository);
  - P(an over starts) per piece;
  - the speed prior.
- **O8. Element sequences that leave the Morse tree** (run-together letters, unknown prosigns). Does the hypothesis
  die, become an "unknown symbol", or fall to the catch-all? This decides whether one sloppy letter kills the right
  story when the text model is off.
- **O9. The amplitude when a channel opens or an over starts** (L122–123). The detector's level must be converted:
  the track reads 3.6–7.1 dB below the key-down SNR per bin depending on speed (signal-processing.md §5). Topic 7
  may handle the change of station; the conversion belongs to topic 3.
- **O10. The noise estimate's working details.** Each of these was needed by the bank (signal-processing.md §8c):
  - what "agree" means;
  - the margin around marks (the channel filter and keying edges spread a mark over about ±4 ms; the bank uses
    4.8 ms);
  - its memory;
  - start-up before any gap is agreed;
  - recovery after a step in the noise (the bank needed an 8 s rule);
  - a correction for selection bias (the bank measured 0.78–0.84);
  - colored noise: long sums depend on the noise density near 0 Hz, short sums on the whole band.
- **O11. An insertion penalty** to go with the text-model weight (S20).
- **O12. Kinds as hypothesis variables or as a mixture** (K9).
- **O13. Keyer weighting, key imbalance and bug fists.**
  - Marks systematically longer than gaps, and dash/dot ratios other than 3 (the generator's ±0.1 dit imbalance;
    its hand style's 4.48; bugs).
  - The fixed fist (L88–95) has no parameter for them. The bank decoder fits the dash/dot ratio and weighting for
    this reason.
  - Waiting for a measured need is the owner's principle (L74–75). Group C2 should be named as the check that
    triggers it.
- **O14 (minor). Missing data.** Runs of exact zeros, at a channel's start or from a dead receiver, are padding,
  not noise. The bank skips them; the new decoder's noise and amplitude estimates must too.

## Recommended corrections

Ordered by importance. Each names the line it changes or the place where the missing item belongs. Where a fix
changes a decision, it is offered for the owner's choice, not made.

**Labels and citations**

1. **L170–171:** replace "True probabilities … summing over the alternatives kept" with: "Posterior probabilities
   need the forward (sum) recursion over the same states, or forward–backward over a lattice (Rabiner 1989;
   Wessel et al. 2001). Summing kept Viterbi scores is only an approximation (topic 8)." A cheap option: each
   hypothesis carries both a best score (maximum at merges) and a total (sum at merges).
2. **L241–242:** "(w = 5%: 1/20; derived)" becomes: "The bound P ≥ w·P₀ is derived (word-level mixture,
   word end included, text-model weight 1; w^λ at weight λ). The value of w is heuristic: to be set on held-out
   text (Jelinek–Mercer) or by the criteria of L243–244."
3. **L169–170:** replace "(Rabiner's 'scaling')" with "scores are kept in logarithms (Rabiner 1989, eq. 105),
   relative to the best (as beam pruning does; Ney and Ortmanns 2000)".
4. **L146:** delete "per-survivor estimates (Raheli, Polydoros and Tzou, 1995)", or replace it with "each hypothesis
   keeps its own running sum, the segment model's statistic; nothing is estimated per hypothesis". This also
   removes contradiction K2.
5. **L90–91:** replace "(as the generator's styles, from VE3NEA's analysis of real keying)" with "(the generator's
   form, VE3NEA's DeepCW parametrization; no source gives it as measured on real keying; the generator's hand style
   has dash and letter-gap medians of 4.48 dits and word gaps of 7.39 dits)". Also state "σ in natural-log
   units" (L93).
6. **L188–189:** label the ε-restart "our model choice; its Viterbi computation is standard", and add the
   constraint ε > 1/B, or a separate birth threshold (D15).
7. **L114–115:** replace "so the estimate freezes during gaps and silence" with "on strong signals. Near the
   decoding threshold noise-only taps carry weights of 0.1–0.25 (D6), so the estimate is frozen explicitly while
   the no-signal hypothesis dominates (or takes its weights from the decoder: the loop option)". Also state the
   prior P(key-down) used.
8. **L109–110:** add "for nominal timing; a mark shortened to 10 ms (fist scatter, key imbalance) peaks about 1 dB
   below A² (signal power)".
9. **L158:** replace "the channelizer's frequency hold is to be measured" with the decision of correction 11.
10. **Small fixes:**
    - L110–111: "about 4.4 dB less noise power (real channel: noise bandwidth 252 Hz; tap 91 Hz)".
    - L252–253: "14 punctuation marks".
    - L78: "5.2–100 WPM".
    - L196: "a tenth to a fifth", citing the measured floors.
    - L205–206: "the owner's target, taken from the spec's estimate".

**Decisions the builder needs (owner's choice)**

11. **After L156 (topic 4):** choose the frequency reference. The suggestion: one tracker per channel ahead of the
    pieces, as in Matched; target residual 0.2 Hz (0.3 dB of signal power on a 5 WPM dash, D7); and a coherent-length cap with
    noncoherent combining of sub-sums beyond it. Measured on group F2. Also amend spec §5.2's ±2 Hz target for
    this decoder (K13).
12. **L85–87:** extend the Farnsworth grid to cover the development set (about 16), or declare I2's upper cells out
    of scope. A measured way to set count and spacing exists: the bank's E5 rule, "the cheapest grid not
    measurably worse than the finest", which gave ratio 1.59 for the bank's fit. Ratio 2^(2/3) = 1.587 reaches 16
    in six steps: 1, 1.59, 2.52, 4, 6.35, 10.1, 16. Note that the bank refined its fit between grid points and
    this decoder does not, so E5's result must be re-measured here, not copied.
13. **L131–132, L151–152:** add "on the real channel filter the factor is 1.9–2.1 (2.8–3.3 dB as a variance ratio); piece and sum
    variances are computed from the real filter's autocovariance, var(n samples) = Σ (n − |k|)·R[k] (D2)".
14. **L130–131:** replace "heuristic" with D11's derivation: whole 2 ms pieces cost at most 0.4 dB of SNR (power
    ratio) on average at 100 WPM, and under 0.1 dB at 25 WPM.
15. **L119–120 and L149–151:** state the decision-feedback policy for both A and the noise (K3). Either define
    "agree", margins, memory, start-up, recovery and the bias correction (O10), or start the prototype with the
    bank's decision-free level mask and its measured correction.
16. **L178–180:** add the non-Morse clocks and (if resolved early) the gap kind to the merge state (K7).
17. **L185–187:** state the birth priors: speed (uniform over the 32?), Farnsworth factor (all five?), and the
    birth probability from silence per piece (K8, O5).
18. **L231–247:** decide whether a hypothesis commits to a kind or carries the mixture (K9, O12). Add the insertion
    penalty beside the text-model weight (L242–244; S20).
19. **L69–70:** update topic 1's fist line: "fixed, channel level (topic 2, item 4)" (K1).

**Plans for the (d) items** (owner's choice of method; each can reuse an existing group)

20. **L92–94 (σ):** sweep σ (e.g. 0.15–0.4) on group C2 with a stated rule (the smallest CER at the crossing), then
    check on the real recordings of topic 6 item 8.
21. **L81–82 and L87 (step probabilities):** sweep on groups D2 and I2, with a rule.
22. **L91–92 (heavy tail):** name a form (for example a mixture with a broad component of weight 1–5%) and sweep
    its weight on C2.
23. **L175–176, L186–188 (the branch threshold, the rescue trigger, ε):** fold them into the search-error
    measurement of L197–199. The very-wide reference also lifts these thresholds and keeps ε > 1/B.

**Missing topics** (owner's choice where they go)

24. Add to topic 4, or as a new agenda item before topic 7:
    - phase and amplitude changes within a mark (O2);
    - other signals and impulsive noise (O3);
    - the non-Morse states' models (O6);
    - invalid element sequences (O8).
25. Add to topic 5:
    - the pruning-fairness rule (O4);
    - the priors the first prototype needs (O7).
26. **Spec §5.2 step 3 and §5.1:** the owner decides whether and when to amend the spec to match topics 2–5 (K12).
    Topic 9 should include a confidence-calibration test for the Hybrid mode (K14).
