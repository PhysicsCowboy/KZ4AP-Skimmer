# Step 2: the new decoder's design set beside standard practice

> **Committed 2026-10-14** (owner's approval) from `build/standard-practice-check/`, where it was written. The text
> below is the reviewer's, unchanged; only this note is added. The paths it gives for step 1's output and its own
> scripts are those of the working copy in `build/`; the committed copies sit in this folder. Its check script
> `step2_checks.py` is committed here, with its "Run:" line updated; its printed output is not committed, because
> rerunning the script (numpy only) reproduces it exactly (checked 2026-10-14). Its lookup helper and downloaded
> source texts stay in `build/`. The design notes it compares are those at commit b51eefe.

Reviewer: an independent Claude agent (step 2 of the standard-practice check). Written 2026-10-14.
Status: **complete** (written incrementally).

What I compared:

- the design: `docs/design/2026-10-09-new-decoder-design-notes.md`, branch `bell-stage-1`, commit b51eefe (I
  confirmed that HEAD is b51eefe and the working tree is clean, so the file I read is the committed one);
- step 1's output: `build/standard-practice-check/step-1-standard-practice.md` (decisions P0.1 to P10.11), with its
  derivations `numbers.py` / `numbers_out.txt`;
- for context only: `docs/signal-processing.md` (sections 0 to 9, A.7 and the bank's ladder in A.8c) and the
  project spec `docs/design/2026-09-25-kz4ap-skimmer-design.md`.

I did not read `docs/research/` (so not review 1, which the notes cite), `docs/plans/`, `build/design-review/`,
step 1's `downloads\`, `refs_*`, `abstracts_*`, `s2_abstracts.py` or `crossref_check.py`. Claims the notes attribute
to review 1 or to `docs/plans/` are treated as claims: where they are derivations I re-derived them myself
(`build/standard-practice-check/step2_checks.py`, numpy only, its printout in `step2_checks_out.txt`); where they
are measurements in files I may not read, I say "not checkable here".

Process note: one of my read-only git commands (`git log` on the notes) was chained with a `cd` in the same call,
against the one-command rule; it changed nothing. All other git commands were single and read-only.

## 0. How to read this file

**Classes** (the brief's four):

- **(i) standard:** the choice is what a textbook, a widely cited survey or tutorial, or the documentation of a
  widely used system does for this sub-problem, used as the literature uses it.
- **(ii) standard but adapted:** a standard method, from this field or a neighboring one (speech and language
  recognition, music tempo tracking, digital receivers, target tracking), is used with changes for Morse. I say
  what is changed and whether the notes label the adaptation as theirs.
- **(iii) ours where a standard exists:** the design uses its own method or value in place of an identifiable
  standard method for the same sub-problem. I name the standard, say what the literature says each gains or loses,
  and whether the notes give a reason.
- **(iv) ours where no standard exists:** I say whether the notes give a checked derivation or a measurement plan
  with a stated method (the owner's "no darts" rule), or neither.

Two conventions: a **number** that sets a standard method's parameter is classed by how the number was obtained (a
heuristic value where the standard practice is to estimate or tune it on data is (iii); a heuristic value with no
standard way of setting it is (iv)). A **deferred or open** item that the notes only name is not classed; it is
listed in section 5 (standard practices not used or not addressed).

**IDs.** T1.3 is topic 1, item 3, in the notes' own numbering where the notes number items; letters (T3.0, T5.S)
mark unnumbered statements. T0.x are the notes' process rules and agenda; B.x the "Background" section; R1.x the
owner's decisions in "After review 1".

**Sources.** Keys in square brackets point to section 8, which says for each source whether I read its text (all,
or the cited part), only its abstract, or only its bibliographic record. "Step 1" means step 1's file; I name its
decisions P0.1 to P10.11 but never rely on its sources: every source here I found and checked myself.

**Units.** Every dB names its reference. S₅₀₀ is key-down carrier power over the noise power in 500 Hz, in dB. E/N₀
is the energy of one key-down element over the noise power per hertz. Losses (from a frequency offset, fading, a
short mark or piece edges) are in dB of signal power or SNR relative to the ideal case named with them: by default
the same mark, exactly centered, unfaded and with exact edges, as in the spec (§5). Ratios of variances are
10·log₁₀ of the ratio. Filter responses are in dB relative to the passband. Log-likelihoods are in nats. T is the
dot length, T = 1.2/w s at w WPM.

## 1. Summary

**Counts.** 73 choices (topics 1–6, the process rules, the background and the owner's decisions after review 1):

| Class | Count |
|---|---|
| (i) standard | 33 |
| (ii) standard but adapted | 26 |
| (iii) ours where a standard exists | 9 |
| (iv) ours where no standard exists | 5 |

The architecture is standard: a generative model searched by time-synchronous Viterbi beam search with token
passing, explicit durations, beam and histogram pruning set by measured search errors, posteriors deferred to the
forward recursion (T5). Most adaptations come from speech recognition, music tempo tracking and receivers, as the
owner expected; the notes label some of them and not others (column "Note" in section 2).

**Class (iii), most consequential first** (details in section 3):

1. **T4.2b: coherent sums over whole marks with no frequency reference.** Standard: estimate and track the carrier
   frequency, and integrate coherently only over blocks short against 1/Δf and the coherence time, combining blocks
   noncoherently. Derived: at the channel's worst offset (±11.7 Hz) a 5 WPM dash loses 28.7 dB; at ±2 Hz a 5 WPM
   dash loses 13.3 dB and an 8 WPM dash 19.2 dB; 5 Hz fading costs 6.9 dB on a 5 WPM dash. The notes record the risk
   and have reopened it; they give no reason for the choice.
2. **T3.0: one amplitude per channel, independent of the hypotheses.** Standard: per-survivor estimation (each
   hypothesis tracks its own level, as Bell's Kalman filter per path), or decision-directed tracking. Near the
   decoding threshold the channel estimate lets noise in (noise-only taps weighted 0.12 at S₅₀₀ = 0 dB, 0.25 at
   −3 dB), and at a change of station every hypothesis shares one slowly refilling level. Reason given: simplicity;
   reopened (K3).
3. **T3.3: the amplitude follows over 0.5 s of key-down time.** Standard: a gain tracker whose bandwidth follows from
   the Doppler spread. The window spans 15 half-correlation times of the gain at a 5 Hz spread, so it cannot follow
   flutter. No reason; "to be tested on fading".
4. **T2.4: a fixed fist model, never adapted to the sender.** Standard: per-sender adaptation (MAP adaptation in
   speech; Mills's online duration estimates). Reason given (simplicity), fallback of two classes.
5. **T2.4d: σ ≈ 0.3, taken from the test generator.** Standard: measure the scatter on real keying (Mills: 0.15).
   At σ = 0.3 a perfectly timed dot carries 6.7 nats of dot-versus-dash evidence, against 26.8 nats at 0.15.
6. **T2.2: speed changes only at word boundaries, by one 10 % step.** Standard (from tempo tracking and Bell): at
   every element, any step with a heavy-tailed law. The test set's steps of ×1.3 to ×2 must then come from births.
7. **T6.5: past text fixed by reasoning** (current word plus previous kind), not by measured perplexity and CPU.
8. **T6.4b: catch-all weight w = 5 %**, where the standard estimates it on held-out text (the notes say so).
9. **T6.1: no callsign lists in the decoder**, where speech recognizers put the known vocabulary in the search;
   the owner gives a reason (lists belong to callsign search; self-confirmation through RBN).

**Class (iv) without a derivation or a measurement plan:** 2 of the 5. The Farnsworth grid (T2.3b: only its top
value, 7.84, is derived; its count of 5 and ratio 1.68 are not, and a stretch midway between grid values costs
0.375 nats per gap at σ = 0.3) and the rule that the factor changes only at word boundaries by one step (T2.3c).
Inside the other classes, 9 more heuristic values or settings have neither yet (section 4.2: σ, the heavy tail, the
weights' prior, the meaning of "agree", the detector-level conversion, the branch threshold, the birth trigger, the
blend of text sources, w); R1.5 schedules their plans for topic 9.

**Standard practices not used** (section 5), most important first:

1. Carrier frequency estimation and tracking (step 1, P1.3, P3.4): reopened, not yet chosen.
2. Fading-aware observation and tracking: bounded coherent integration, a gain model from the Doppler spread,
   consistency checks (P1.4, P1.5, P3.7): reopened in part.
3. Per-character posteriors, calibration on held-out data and Chow's reject rule for the hand-off to the neural
   decoder (P8.1 to P8.3, P10.8): deferred to topics 8 and 9.
4. Language-model look-ahead (not in step 1) and the insertion penalty (P6.6): look-ahead not addressed (it is the
   standard answer to the reopened O4), the penalty raised (O11).
5. Models for the channel's other contents: filler models, impulsive noise, neighbor stations (P5.2, P5.4, P5.5):
   a new topic.
6. Change-of-station modeling (P7): topic 7. Determinism under pruning ties and the load-dependent overload governor
   (P4.9, P9.5): not addressed.

**Other findings.**

- Two choices the notes call their own are standard: noise measured on the decoder's own gap decisions (T3.4; speech
  enhancement's detector-gated or probability-weighted noise estimation) and the ε-restart births (T5.3b; the
  constant-hazard restart of Bayesian online change-point detection, in its Viterbi form).
- The owner's decision to label a mark dot or dash at its end (T1.2b) is exact, not an approximation (C12).
- Every review-1 derivation I could redo holds (D2, D4, D6, D7, D8, D9, D11, D12, D14, D15, and the tap's 91 Hz and
  4.4 dB); D4's lower end comes out 0.66 dB instead of 0.8 dB, D6 holds with an unstated key-down prior of 0.44,
  D8 assumes the ITU definition of Doppler spread, and D11 is conservative on the real filter. D5 is a simulation I
  did not repeat.
- Citations: one wrong reference (the spec's "§5, step 5" is §4.1, item 5); Lowerre supports the beam but not the
  maximum count (that is Steinbiss, Tran and Ney's histogram pruning); Ney and Ortmanns support pruning relative to
  the best, not "divided by the best" as such; public log-checking reports exist only for top-scoring stations; and
  Bell himself flagged the independence assumption that the notes call "the mistake of Bell's receiver chain".
- Farnsworth on the air: since 2001 W1AW's slow code practice uses 15 WPM characters, a stretch of at most 6.26
  (at 5 WPM), against the design's 8.00 and the development set's 15.7 (section 4.1).

## 2. The full table

One row per choice. "Step 1" gives step 1's decision numbers (P2(e)4 means part P2, section (e), item 4). "C1" to
"C12" are my derivations in `step2_checks.py` (output in `step2_checks_out.txt`). Topic 1's items 6, 7 and 10 are
classified where their content is decided (T6.5, T4.4, T3.0 and T2.4), so they have no row of their own. The
owner's open items from "After review 1" (R1.2 to R1.4) are not classified; they are in section 5.

| ID | Choice (the notes' words, short) | Class | Step 1 | Standard method or alternative | Note | Sources |
|---|---|---|---|---|---|---|
| T0.1 | Each conclusion recorded "with whether it is derived, measured, heuristic or the owner's decision" | (i) | P10.1 | Keep derived, measured and tuned parameters apart | Matches step 1's own labels | — |
| T0.2 | Standard-practice rule (owner): "every choice is standard practice, with its source, or our own choice, labeled as such" | (i) | none (a rule for the notes) | Cite the source of every method | This check is its implementation | — |
| T0.3 | No darts (owner): "every heuristic number ends with a measurement plan or a real, checked derivation" | (i) | P10.1 | Derive physical parameters, set the rest on development data, test once | Section 4 lists the numbers that have neither yet | [ChenGoodman1999], [Steinbiss1994] |
| T0.4 | Agenda (owner): nine topics, "one topic at a time, in the agenda's order" | (i) | P0 to P10 | The parts of a source-channel recognizer | Frequency, fading and interference had no topic until R1.2 and R1.3 | [GalesYoung2008] |
| B.1 | Bell's printed program "dropped as a benchmark": it "decodes worse than the project's existing decoders" | (i) | P10.10 | Keep a reproducible baseline beside every new decoder | Measurement in `docs/plans/`, not checkable here | — |
| B.2 | "What carries over is Bell's overall structure": competing hypotheses, updated probabilities, text committed "only after enough evidence" | (i) | P0.1 to P0.3, P4.4, P4.8 | Generative model, time-synchronous search, delayed decisions | | [Bell1977], [GalesYoung2008] |
| T1.R | "a hypothesis carries everything the probability of its next step or of the next observation depends on, and nothing that is the same for every hypothesis" | (i) | P4.2 | A state that makes the model Markov | Its second half is why amplitude and noise sit at channel level (T3.0) | [Murphy2002] §2.3.12 |
| T1.1 | "The kind of activity: Morse being sent, no signal, steady carrier, or nonsense keying" | (ii) | P5.1, P5.2 | Silence (background) model inside the search; "filler" (garbage) models of keyword spotting | Adapted: carrier and nonsense keying become filler states. Not labeled or cited. Their models are deferred (R1.3) | [RosePaul1990], [Sohn1999] |
| T1.2 | "Key down or up, and the time spent in it so far" | (i) | P2.1, P2.2 | Explicit-duration (semi-Markov) model with a duration counter | | [Murphy2002] §2.3.12, [Rabiner1989] §IV.D, [Bell1977] |
| T1.2b | A mark is "a mark lasting so far" and "becomes a dot or a dash when it ends, instead of Bell's choice at its start" (owner) | (ii) | P2.2 (no direct counterpart) | Label chosen when the segment starts (Bell; usual HSMM) | Exact, not an approximation (C12): dot and dash share one observation model, so one segment with the prior-weighted mixture of their duration laws, labeled at its end, gives every complete story exactly Bell's probability. Same idea as the prefix tree of a speech lexicon. The notes state the gain, without derivation or source | [Bell1977], [NeyOrtmanns1999] §4, C12 |
| T1.3 | Gap kind "possibly resolved when the gap ends"; "No separate 'pause' element"; a long gap is "a long word space" or "no signal", and "Both hypotheses coexist until the next marks decide" | (ii) | P5.3, P2.1 | End of a transmission decided by the posterior of a background model, not by a timeout | Adapted from silence and voice-activity modeling; replaces Bell's 14-dot pause; not labeled | [Sohn1999], [Bell1977] Table I |
| T1.4 | "Speed and the Farnsworth gap factor" carried by each hypothesis | (ii) | P2.4, P2.7, P3.5 | Tempo as a discrete hidden state (music); Bell's speed state | Forms in T2.1 and T2.3 | [Krebs2015], [Bell1977] |
| T1.5 | "The position in the Morse tree: the letter in progress" | (i) | P0.4, P4.2 | Lexicon network (pronunciation tree) | | [GalesYoung2008] §2.4, [Bell1977] |
| T1.8 | "Its score and a link to its parent"; the score is "the probability of its single best story" | (i) | P4.4, P8.5 | Token passing with traceback records | | [Young1989] |
| T1.9 | "A clock for each non-Morse state" | (ii) | P5.1, P5.2 | Duration ("hang-over") modeling of background states | Adapted, not labeled | [Sohn1999] |
| T2.0 | "the simplest version of Bell's structure that works; ... no refinement without a measured need" | (i) | P10.4, P10.10 | Baseline first; add parts one at a time and measure each | | — |
| T2.1 | Speed "is one of 32 values whose dot lengths are 10% apart, covering 5.2–100 WPM ... nothing finer"; error "at most ±4.9% ... (derived)" | (ii) | P2.4 | Log-spaced tempo states; music spaces them at the 2–5 % just-noticeable difference and picks their number by measured accuracy against cost | Derivation checked (C9). Coarser than the music practice: harmless at σ = 0.3 (0.013 nats per element midway between grid speeds) but 0.45 nats per element at σ = 0.05, if a "precise" fist class is added. Not labeled as adapted | [Krebs2015], C9 |
| T2.2 | "Speed changes only at word boundaries, by one ladder step (±10%), with a small probability (heuristic, to be measured)" | (iii) | P2.5, P2.6 | Changes allowed at every rhythmic unit (each beat in music, each key transition in Bell) to any step, with a heavy-tailed law | Section 3.1 | [Krebs2015], [Bell1977] |
| T2.2b | "The start of an over is not a speed change: hypotheses are born at every ladder speed" | (ii) | P3.6, P7.3 | Restart from a broad prior after a change point | Adapted; priors at birth are reopened (K8, O7) | [Adams2007] |
| T2.3a | Farnsworth as one gap factor that "stretches only letter and word gaps", carried by each hypothesis | (ii) | P2.7 | The ARRL rule: one added delay split 3:7 between letter and word gaps, so one common stretch | The factor's form is the ARRL standard (checked); carrying it as a discrete hidden variable is the adaptation | [Bloom1990] |
| T2.3b | Gap factors "exactly geometric (owner), 1, 1.68, 2.83, 4.76, 8.00"; "the count, 5, is heuristic"; top value from 7.84, "derived by the ARRL timing rule"; range reopened | (iv) | P2.7 (no counterpart for a grid) | none | Top value checked (7.84 at 18 WPM characters, 5 WPM overall). Count and spacing: no derivation, no plan. A stretch midway between grid values is off by +29.7 % or −22.9 %, 0.375 nats per gap at σ = 0.3 (C9). On the air, W1AW has used 15 WPM characters since 2001 (stretch 6.26 at 5 WPM) | [Bloom1990], [ARRL2001], C9 |
| T2.3c | "The factor changes only at word boundaries, by one step" | (iv) | P2.7 | none | Neither derivation nor plan | — |
| T2.4 | "Fist model: fixed, at channel level, never learned while running" | (iii) | P2.7, P2.3 | Per-sender adaptation of the duration parameters (maximum a posteriori adaptation in speech; Mills's online estimation of duration means) | Section 3.2 | [GauvainLee1994], [Mills1977] |
| T2.4a | "length = ideal × a random factor whose natural logarithm is Gaussian (the generator's form, from VE3NEA's DeepCW generator ...)" | (ii) | P2.3 | Scatter proportional to duration (constant coefficient of variation) | Log-normal is one constant-coefficient family; Mills found scatter proportional to length on off-air signals. The label is honest: checked in DeepCW's code, which gives no measurement for its timing | [Mills1977], [DeepCW] |
| T2.4b | "a slightly heavy tail so that one wild element does not kill the right hypothesis (its form and weight are heuristic, not yet chosen)" | (ii) | P5.4, P2.3 | Contaminated (two-component) likelihood of robust statistics | Standard device; its weight is open | [Huber1964] |
| T2.4c | "Ideal lengths 1, 3, 7 dots" | (i) | P2.3 | ITU Morse timing | | [ITU-R-M1677] §2 |
| T2.4d | "Spread σ ≈ 0.3 ... for marks and for gaps (two separate settings, both starting at 0.3: as forgiving as the generator's hand style; heuristic)" | (iii) | P2.3 | Scatter measured on real keying (Mills: proportionality constant 0.15) | Section 3.3 | [Mills1977], [DeepCW] |
| T2.4e | "two fist classes ('precise', 'hand') ... only if the development set shows precise signals losing noticeably" | (ii) | P2(e)4, P2.7 | A few model sets for classes of sender, chosen by likelihood (as speech uses gender-dependent models) | Adapted; "noticeably" undefined | — |
| T2.5 | Telling speed change, scatter and Farnsworth apart: "no special logic; the hypothesis probabilities do it" | (i) | P0.2 | One joint search over all knowledge sources | | [GalesYoung2008] |
| T2.6 | "the development set comes from the same generator ... recordings of real operators are the real check" | (i) | P10(e)1, P2(h) | The circularity pitfall, checked on real data | | — |
| T3.0 | Amplitude: "One estimate per channel, independent of every hypothesis and speed (owner's tap idea)" | (iii) | P3.1, P3.3 | Per-survivor estimation (each hypothesis tracks its own amplitude, as Bell's Kalman filter per path) or decision-directed tracking after a non-data-aided start | Section 3.4 | [Raheli1995], [Bell1977] |
| T3.1 | "The tap is a fixed sum matched to the fastest speed: about 10 ms ... (5 ... pieces)"; noise bandwidth "about 91 Hz"; SNR "about 4.4 dB" above one sample | (ii) | P1.2 | Matched filter (integrate-and-dump) as long as the element | Adapted: one filter sized to the shortest dot serves every speed. Checked (C3, C4): 91.4 Hz and 4.40 dB; a 10 ms mark peaks 0.66–0.94 dB low (notes 0.8–1.0 dB); nominal 12 ms marks reach the full level within 0.24 dB; 27.2 % of 100 WPM dots are shorter than 10 ms at σ = 0.3 | C3, C4 |
| T3.2 | "A² = the weighted average of the tap's power minus the noise power, each sample weighted by its probability of being key-down ... an EM-like soft-decision estimate" | (ii) | P3.3, P3.1 | Online expectation-maximization (EM) for a two-component (Rician/Rayleigh) mixture | Adapted: a moment update instead of the maximum-likelihood one, inside a 0.5 s window. Labeled heuristic; EM not cited. The notes' noise-only weights (0.12 at S₅₀₀ = 0 dB, 0.25 at −3 dB) reproduce exactly with a key-down prior of 0.44 (C8), the PARIS duty cycle, so that is the unstated prior behind them. The bias (−0.9 to +0.7 dB) is a simulation I did not repeat | [CappeMoulines2009], [Dempster1977], C8 |
| T3.3 | "an average over about the last 0.5 s of key-down time ...; heuristic, to be tested on fading (QSB)" | (iii) | P1.5, P3.3, P3.7 | Gain tracker (Kalman filter on a Gauss-Markov gain) whose bandwidth follows from the Doppler spread and the SNR | Section 3.5 | [Bell1977], [Baddour2005], [ITU-R-F1487], C11 |
| T3.4 | Noise "measured on the pieces that the hypotheses agree are gaps, as a level per hertz"; "choosing them by the decoder's own decisions is our method (decision-directed)" | (ii) | P3.2 | Speech enhancement updates the noise estimate on frames a voice-activity detector judges signal-free, or weights each frame by its probability of holding signal, and corrects the known bias; minimum statistics needs no decisions | The notes call this "our method"; it is standard (section 7, item 1). "Agree" undefined; the expected selection bias is right | [Martin2001], [Cohen2003] |
| T3.5 | "the scoring of topic 4 compares a hypothesis's summed mark with a carrier of amplitude A plus noise" | (i) | P1.4 | Rician (carrier plus noise) against Rayleigh (noise alone) likelihood | | [Proakis2008] §4.5-1 (contents only) |
| T3.6 | "A starts from the level the signal detector measured ... then refines from the first marks"; the conversion "is not yet stated" | (ii) | P3.6, P3.3 | Start from a coarse acquisition estimate, then track | The conversion can be derived (key-down power ≈ (1 s average − noise) / duty cycle, plus the detector's bin and sideband factors in `signal-processing.md` §5); not yet written | `signal-processing.md` §5 |
| T4.1 | "Every hypothesis is scored on the same evidence ... short non-overlapping pieces"; speed-specific filters "would score hypotheses at different speeds on different data" | (i) | P1.8, P1.6, P0.2 | One shared observation sequence for all hypotheses | Ney and Ortmanns: scores can be compared because all hypotheses "cover the same portion of the input" | [NeyOrtmanns1999] |
| T4.2 | "Each hypothesis does its own matched filtering: it adds up the pieces it claims fall inside its current mark (one complex running sum)"; standard noncoherent scoring | (ii) | P1.1, P1.2, P1.4 | Noncoherent matched filter; segment models with one likelihood for the whole segment | Adapted, and labeled ("The combination for Morse is ours"). It is exactly step 1's adaptation P1(e)1 | [Proakis2008], [Rabiner1989] §IV.D, [Ostendorf1996], [Murphy2002] §2.3.14 |
| T4.2b | Coherent integration over the whole mark ("A Morse carrier keeps its phase through a mark") with no frequency reference ("the channelizer has no frequency hold") | (iii) | P1.3, P1.5, P3.4 | Estimate and track the carrier frequency; integrate coherently only over blocks short against 1/Δf and the coherence time; combine blocks noncoherently or differentially | Section 3.6 | [DovisTa2012], [RifeBoorstyn1974], [StreitBarrett1990], C6, C7 |
| T4.3 | "The time step is the piece length, about 2 ms" | (ii) | P1.8 | Speech's frame step (10 ms) scaled to the shortest element (at most a quarter of a dot) | Cost derived. The notes' 0.37 dB mean reproduces in a white-noise model with unfiltered marks (C5b); on the real channel filter the cost is 0.30 dB mean and 0.41 dB worst against the ideal (C5) | C5 |
| T4.4 | "Keying history ...: only the running sum of the current mark" (closes topic 1, item 7) | (i) | P4.2 | The segment's statistic carried in the state | | [Murphy2002] §2.3.14 |
| T4.5 | "The bank is not used by the new decoder (owner)"; it "stays frozen as the yardstick" | (i) | P10.10 | Frozen baseline | | — |
| T4.7 | "The noise of any sum over a time T is computed from the noise level per hertz"; "a sum of n samples has variance Σ (n − \|k\|)·R[k] (derived, exact)" | (i) | P1.6 | Exact variance of a sum of correlated samples (step 1 lists whitening or decimation; this is a third standard option) | All the topic's numbers checked (C1, C2) | C1, C2 |
| T5.0 | "A hypothesis is one continuous story of what has been sent, for the whole over" | (i) | P4.4 | Token with traceback | | [Young1989] |
| T5.S | "Viterbi beam search, the standard decoder of speech recognition"; scores "are not probabilities"; held "relative to the best ... or as logarithms" | (i) | P4.4, P4.7 | The same | Checked: Rabiner's eq. (105) (logarithms), token passing, pruning relative to the best | [Rabiner1989], [Young1989], [NeyOrtmanns1999] |
| T5.P | Probabilities "are not sums of the kept Viterbi scores"; standard routes: "the forward (sum) recursion ... or forward–backward over a lattice"; choice in topic 8 | (i) | P8.1, P4.6 | Forward–backward on lattices or confusion networks; full-sum decoding | | [Wessel2001], [GalesYoung2008] §2.4, [ZhouSchluterNey2020] |
| T5.1 | "every piece (2 ms), a hypothesis continues as it is, or ends its current mark or gap and branches"; a branch only "where the fist model gives it a non-negligible probability (a pruning threshold; its value is not yet set)" | (ii) | P2.2, P4.5 | Time-synchronous expansion; extra pruning when hypotheses are created (speech's word-end pruning) | Threshold open; unless it is relative to the best score it acts like a second beam (cf. D15 for births) | [NeyOrtmanns1999] §5.1 |
| T5.2 | Merging: two hypotheses in the same state "can never be told apart by future evidence ... The lower-scoring is dropped" | (i) | P4.4 | Viterbi recombination | The derivation holds for the best story; the notes say the lost probability matters for posteriors (T5.P) | [Young1989], [GalesYoung2008] |
| T5.3a | Births from silence: "Morse at any of the 32 speeds, a steady carrier or nonsense keying may begin at any piece" | (ii) | P5.1, P5.3 | Transitions from the background model into speech | Adapted | [Sohn1999] |
| T5.3b | "always-on rescue births ... with a small disruption probability ε, from every hypothesis; ... its score is the best current score times ε"; "the ε-restart model is our choice" | (ii) | P2.6, P7.1, P3.6 | Constant-hazard restart of Bayesian online change-point detection; the "fixed share" of tracking the best expert | The notes call it ours; it is the maximum (Viterbi) form of the standard restart (section 7, item 2). The constraint ε > 1/B checks by algebra. Trigger undefined | [Adams2007], [Herbster1998] |
| T5.4a | "Pruning: a beam plus a maximum count"; the factor "set by measurement" against a decode with "a very wide beam" | (i) | P4.5, P10.4 | Beam pruning (Lowerre) plus histogram pruning (Steinbiss et al.), set on development data by counting search errors | | [Lowerre1976], [NeyOrtmanns1999], [Steinbiss1994] |
| T5.4b | Target: "pruning may change the decoded result at most once per 1 000 letters (0.1%)" | (iv) | P4.5 | none (a requirement) | Reasoned against measured error floors (in `docs/plans/`, not checkable here); the method is stated. A pooled 0.1 % can hide search errors concentrated in the weakest cells | — |
| T5.5 | "Ceiling on the number of hypotheses, set by CPU, not accuracy"; "about 50 ns per hypothesis per piece ... about 1 700 hypotheses" | (i) | P4.5, P9.1 | Histogram pruning cap | Arithmetic checked (C10: 1 680); estimates flagged and replaced by measurement (T5.8) | [Steinbiss1994], C10 |
| T5.6 | Target: "the spec's estimate for the Classical mode, 2–3 million floating-point operations per channel-second" | (iv) | P9.4 | none (a requirement) | Measurement plan stated. The spec measures CPU time per channel-second on a desktop and a Raspberry Pi 5; T5.8 names only the Linux machine | spec §5.1, §5.4 |
| T5.7 | Overload: "(a) first decode every station with fewer hypotheses ...; (b) ... drop the weakest stations" | (ii) | P9.2, P9.3, P9.5 | Imprecise computation: a mandatory minimal part for every task, optional effort by priority, shedding last | Adapted, not labeled. No priorities (new or uncertain channels first); sheds the weakest; determinism in benchmark mode not stated | [Liu1991] |
| T5.8 | "The ceiling, the floor and the cost are measured, not guessed" (cost timed, live hypotheses counted, CER against the ceiling) | (i) | P9.4, P10.4 | Accuracy-against-cost curves; measured real-time factor | Method stated | [Steinbiss1994] |
| T6.0 | The text model "multiplies a hypothesis's score each time the hypothesis completes a letter" | (i) | P6, P0.1 | Language model applied when a unit completes; decoders also anticipate it inside the tree ("look-ahead") | Look-ahead not used (section 5) | [NeyOrtmanns1999] §5.2 |
| T6.1 | "No callsign matching in the decoder (owner)"; "never matched against the Reverse Beacon Network" | (iii) | P6.4, P0.2 | The known vocabulary inside the search (a lexicon), plus an open-vocabulary model for unknown words | Section 3.7; reason given | [GalesYoung2008], [Bazzi2002], [Naptali2012] |
| T6.2 | "One model, not two modes"; five kinds (callsign by "written pattern rules", fixed token, number, plain language, catch-all); "class-based language models ... here the classes are defined by hand", "interpolation with a uniform distribution", "a finite-state grammar" | (ii) | P6.1, P6.3, P6.4 | Class model Pr(word \| class)·Pr(class \| history) with a sub-model per class; interpolation with a uniform model | Adaptation labeled | [Brown1992], [ChenGoodman1999] |
| T6.3a | "one fixed mix for all channels, counted from independent text (the blend of sources that fixes it is not yet chosen)" | (ii) | P6.3, P6.7 | Class or mixture weights estimated on held-out text of the target domain | Adapted to the lack of such text; the blend is open | [ChenGoodman1999], [Iyer1996] |
| T6.3b | Per-channel mix "by counting (each kind's share of the channel's last N words); N is measured"; "our variant of a cache language model" | (ii) | P6.3, P6.5 | Dynamic adaptation of mixture weights from the recent history | The label is fair, but the closer standard is dynamic mixture weights (Kneser and Steinbiss), which Iyer and Ostendorf found gave no significant gain in their task, consistent with "only if tests show" | [KneserSteinbiss1993], [Iyer1996], [Kuhn1988], [KuhnDeMori1990] |
| T6.4a | The catch-all bounds a word "to no less than w times its probability without a text model" (three conditions; w^λ at weight λ) | (i) | P6.3 | A mixture is at least each component times its weight | Checked by algebra | — |
| T6.4b | "The value w = 5% (1/20) is heuristic; standard practice (Jelinek and Mercer, 1980) sets it on held-out text" | (iii) | P6.3, P6.7 | Interpolation weights estimated on held-out text | Section 3.8 | [ChenGoodman1999] |
| T6.4c | The text model's weight "is set by measurement: errors on ambiguous signals must fall, and random callsigns decoded as something else must not rise" | (ii) | P6.6 | Language-model scale tuned on development data | Adapted criterion (busted callsigns); the procedure is not yet written; the insertion penalty is missing (section 5) | [GalesYoung2008] |
| T6.5 | Past text: "the current word so far, plus the kind of the previous word"; "more text, more correction, fewer merges, more CPU" | (iii) | P4.2, P6.2 | Context length chosen by held-out perplexity and by error against CPU | Section 3.9 | [ChenGoodman1999] |
| T6.6 | "The first prototype has no text model ... Kinds are added one at a time, each measured" | (i) | P6(d), P10.4 | Uniform-text baseline; one component at a time | | [Bell1977] |
| T6.7 | "Alphabet: the engine's existing Morse table (letters, digits, 14 punctuation marks, 12 prosigns)" | (i) | P6.8 | ITU code, prosigns as single symbols | | [ITU-R-M1677] |
| T6.8 | Sources: contest logs and log-checking reports; practice text for plain language; real recordings transcribed "by correcting a decoder's draft"; blindness "decided in topic 9" | (ii) | P6.7, P10.9, P10.2 | Train on in-domain text, test on held-out text; transcribe references blind, and a sample twice | Adapted to scarce in-domain text. Correcting a draft leaves the draft's imprint (the post-editing literature); only top-scoring stations' log-checking reports are public | [CQWW-LCR], [Toral2019], [IZ4KBW] |
| T6.9 | "Contest logs are used ... but never committed. IZ4KBW's recordings, if ever included, credit him." | (iv) | none | n/a (a data policy) | Needs neither derivation nor measurement | — |
| R1.1 | "A blind check of standard practice comes first (owner)" | (i) | none | Independent review | This document | — |
| R1.5 | "Measurement plans for every heuristic number ... are written in topic 9", and "a test of how well confidence predicts correctness" | (i) | P10.1, P8.2, P10.8 | Calibration measured on held-out data | Plans not yet written (section 4) | — |
| R1.6 | Spec to be amended; "Its ±2 Hz re-centering target ... is too loose for whole-mark sums" | (i) | P1.3 | Frequency error small against 1/(mark length) | Checked: ±2 Hz costs 13.3 dB on a 5 WPM dash (C6) | C6 |

## 3. Class (iii) in detail

Nine choices use the project's own method or value where a standard one exists. For each: what the design does,
the standard method, what the literature says each gains or loses, and whether the notes give a reason. Section 1
ranks them by consequence.

### 3.1 T2.2: speed changes only at word boundaries, by one 10 % step

**The design.** A hypothesis's speed may change only at a word boundary, by one ladder step (a factor 1.1), with a
small probability not yet set. Larger or faster changes are left to the rescue births (T5.3b), which restart a
letter at every speed.

**The standard.** There is no Morse standard; the adapted standard comes from tempo tracking in music, the closest
problem (a rhythm of fixed ratios performed at a varying tempo). Krebs, Böck and Widmer "allow transitions to each
tempo, but only at beat times", with a probability that falls exponentially in the relative tempo change,
f ∝ exp(−λ·|τ_k/τ_(k−1) − 1|), and λ tuned between 1 and 300 [Krebs2015] (text). The Morse analog of a beat is an
element. In Bell's decoder "the speed is not allowed to change except when the keystate changes", by increments of
0, ±2 or ±4 WPM conditioned on "the type of Morse symbol just completed", with larger increments after word spaces
and pauses [Bell1977] (text).

**Gains and losses.** Krebs et al. report that restricting changes to beat times gave "higher stability" of the
tempo track and "far fewer states and transitions" [Krebs2015]; allowing any step size keeps the tracker able to
follow abrupt changes. The design restricts both when (word boundaries, coarser than elements) and how much (one
step). The consequence follows from the numbers: a step by a factor 2 needs 7 word boundaries at one step each
(1.1⁷ = 1.95), and a factor 1.3 needs 3 (1.1³ = 1.33); the test set's steps of 1.3 to 2.0 therefore rely on births,
not on continuing hypotheses. In exchange, word-boundary changes cost fewer branches (CPU) and rule out spurious
speed changes inside a word.

**Reason given.** Yes, partly: "Drift within a word is absorbed by the fist scatter (heuristic)", a named check
(the development set's speed-change group) and a fallback ("allow two steps at a smaller probability"). The
probability itself has no stated method.

### 3.2 T2.4: a fixed fist model, never learned while running

**The design.** One duration model for every sender on the channel, fixed in advance.

**The standard.** Adapt the duration parameters to each sender. In speech, maximum a posteriori (MAP) adaptation
re-estimates model parameters from a speaker's own data while pulling them toward a prior; Gauvain and Lee present
it as "a unified approach" for "parameter smoothing and model adaptation" [GauvainLee1994] (abstract). In Morse,
Mills's ratio-weighted estimation "provides extremely fast adaptation to the time-varying Morse code parameters
typical of multi-operator circuits" [Mills1977] (text).

**Gains and losses.** A sender's own scatter is usually much smaller than the scatter of all senders together, and
the timing evidence grows as 1/σ². For a perfectly timed dot, the log-likelihood ratio between "dot" and "dash"
under log-normal laws with medians 1 and 3 dots is (ln 3)²/(2σ²): 6.7 nats at σ = 0.3, 26.8 nats at σ = 0.15
(Mills's off-air value), 241 nats at σ = 0.05 (machine keying) (derived). Between a letter gap and a word gap the
same formula gives (ln 7/3)²/(2σ²) = 4.0 nats at σ = 0.3 and 16.0 nats at σ = 0.15. So a broad fixed model leaves
more of the decision to the signal and the text model. Adaptation's costs: it learns from decoded (possibly wrong)
text, the error propagation of decision-directed estimators (step 1, P3(d)); it needs data per sender; and it must
reset at a change of station (topic 7).

**Reason given.** Yes: topic 2's principle ("the simplest version ... no refinement without a measured need") and
a fallback of two fist classes (T2.4e). No measurement plan says when "noticeably" is reached.

### 3.3 T2.4d: σ ≈ 0.3 for marks and gaps, set from the generator

**The design.** The spread of ln(length/ideal) is 0.3 for marks and for gaps, "as forgiving as the generator's
hand style".

**The standard.** Measure the scatter on real keying. Mills, from off-air signals: the standard deviation is
proportional to the length of the (mark, space) vector, σ = k·|x|, and "a value of k ≈ 0.15 is a good compromise
and agrees well with a number of observations under widely varying receiving conditions and operator
characteristics"; he also called for "a study of the statistical parameters of the 'typical' human operator"
[Mills1977] (text). The generator the value comes from (DeepCW) draws ln-lengths with σ = 0.15 for dots, 0.3 for
dashes and letter gaps and 0.2 for element and word gaps in its hand-key style, and 0.016 to 0.05 for most
elements in its computer style; it gives no measurement behind these values [DeepCW] (code read).

**Gains and losses.** As in 3.2: a broad σ weakens timing evidence (by 4 times against σ = 0.15) but rarely kills
the right hypothesis on a sloppy fist; a narrow σ does the opposite (hence the heavy tail, T2.4b). Setting σ from
the generator that also makes the development set is circular, which the notes say (T2.6).

**Reason given.** Yes ("as forgiving as the generator's hand style"), but no measurement plan yet. IZ4KBW's
archive, which lists the key used for each recording [IZ4KBW], could give measured scatter per keying device.

### 3.4 T3.0: one amplitude per channel, independent of the hypotheses

**The design.** The key-down amplitude A is estimated once per channel from a fixed 10 ms tap, with soft weights
judged on the tap itself (T3.1, T3.2). No hypothesis carries its own amplitude.

**The standard.** For sequence detection with unknown, changing channel parameters, embed the estimation in the
search: per-survivor processing, in which "data-aided estimation of unknown parameters may be embedded into the
structure of the Viterbi algorithm itself" [Raheli1995] (abstract). Bell's Morse decoder does exactly this: a
Kalman filter per path, the amplitude "observed only when" the key is down, constant during a mark and allowed to
change during a space by an amount depending "on the type and duration of the space" [Bell1977] (text). The
conventional alternative is non-data-aided acquisition followed by decision-directed tracking.

**Gains and losses.** Raheli et al.: per-survivor algorithms "are clearly more robust than conventional techniques
both in tracking a time-varying channel and acquiring its characteristics without training" [Raheli1995]
(abstract); the cost is one estimator per surviving hypothesis. The design's estimator costs one per channel and
cannot be dragged by wrong hypotheses, but it cannot use what the hypotheses know about where the marks are. Near
the decoding threshold it lets noise in: noise-only taps get a mean key-down weight of 0.12 at S₅₀₀ = 0 dB and
0.25 at −3 dB (C8, prior 0.44), and the notes report a fixed-point bias of −0.9 to +0.7 dB in A². At a change of
station all hypotheses share one level that refills over 0.5 s of key-down time, where Bell's paths could each
jump after a pause.

**Reason given.** Yes: "owner's tap idea" and topic 2's simplicity principle; the alternative (feeding back the
decoder's key-down probability) is listed as "a later option", and the question is reopened (K3).

### 3.5 T3.3: the amplitude follows over 0.5 s of key-down time

**The design.** A is an average over about the last 0.5 s of key-down time; "heuristic, to be tested on fading".

**The standard.** Model the complex gain as a Gauss-Markov (autoregressive) process whose correlation follows the
fading's Doppler spectrum, and track it with a Kalman filter whose process noise is set by that spectrum; then
check the filter's consistency (step 1, P3.7). Bell did this inside his Kalman filter: during a mark the fading
amplitude "obeys a linear gauss Markov process" with parameters "selected to represent the fading observed"
[Bell1977] (text). Autoregressive models of Rayleigh fading are standard [Baddour2005] (abstract).

**Gains and losses.** A tracker matched to the spread trades noise against lag optimally for that spread; a fixed
window is right for one spread only. Derived (C11), with the ITU convention that the spread is twice the standard
deviation of a Gaussian Doppler spectrum [ITU-R-F1487] (text): the gain's correlation falls to 0.5 after 7.5 s at
0.05 Hz, 0.75 s at 0.5 Hz and 75 ms at 5 Hz. The 0.5 s of key-down time spans about 1.14 s of signal at a duty
cycle of 0.44, which is 0.15 half-correlation times at 0.05 Hz (follows well), 1.5 at 0.5 Hz (lags) and 15 at 5 Hz.
At 5 Hz the estimate is the mean level, while a Rayleigh-faded carrier spends 9.5 % of the time more than 10 dB
below its mean power (1 − e^(−0.1)); those marks are scored against an A 10 dB too high, which the notes' own error
list says turns weak marks into gaps.

**Reason given.** No; heuristic, with a test named ("to be tested on fading") but no method.

### 3.6 T4.2b: coherent integration over the whole mark, with no frequency reference

**The design.** Each hypothesis sums the complex pieces over its whole mark, on the premise that "a Morse carrier
keeps its phase through a mark". The channel is centered once on the nearest 23.4 Hz bin, so the carrier sits up
to ±11.7 Hz off 0 Hz, and nothing in the new decoder estimates that offset.

**The standard.** Two standard practices, usually together. (a) Estimate and track the carrier frequency: the
maximum-likelihood single-tone estimate via the discrete Fourier transform, with its Cramér-Rao bound and
threshold effect [RifeBoorstyn1974] (abstract); at low SNR, tracking with a hidden Markov model over frequency cells
[StreitBarrett1990] (abstract); at higher SNR a Kalman filter or phase-locked loop. (b) Integrate coherently only
over blocks short against 1/Δf and the coherence time, then combine the blocks noncoherently (sum of squared
magnitudes) or differentially (sum of products of each block with the previous one's conjugate). The weak-signal
acquisition literature describes exactly this choice: coherent combination "is equivalent to increase" the
integration time; noncoherent combination suffers a "squaring loss ... less effective than the coherent one";
differential combination is "a trade-off solution" [DovisTa2012] (text). The loss of a coherent block of length T
at offset Δf is sinc²(Δf·T).

**Gains and losses.** With a known, steady phase, the whole-mark sum is the matched filter and has no equal. Without
it, derived (C6, C7): a 5 WPM dash (720 ms) loses 1.9 dB at 0.5 Hz offset, 13.3 dB at 2 Hz and 28.7 dB at
11.7 Hz; an 8 WPM dash loses 19.2 dB at 2 Hz; a 20 WPM dash 1.9 dB at 2 Hz and 26.1 dB at 11.7 Hz. Rayleigh fading
with a 5 Hz spread costs 6.9 dB on a 5 WPM dash and 2.0 dB on a 20 WPM dash. To keep the loss under 1 dB,
Δf·T ≤ 0.26, so at the channel's worst offset coherent blocks must stop at about 22 ms; with the offset estimated to
±0.5 Hz they may grow to about 0.5 s.

**Reason given.** None for the choice; the notes record the risk ("Risk: it relies on the carrier's phase staying
steady through a mark"), quantify it (D7, D8, which I reproduce), and reopen the frequency reference and the
changes within a mark (R1.2), with the spec's ±2 Hz target judged too loose (R1.6).

### 3.7 T6.1: no callsign lists in the decoder

**The design.** The decoder knows callsign formats, not which calls exist; lists (`cty.dat`, `MASTER.SCP`) are used
only after the decoder, and nothing is ever matched against the Reverse Beacon Network.

**The standard.** A speech recognizer searches over its known vocabulary: the decoder uses "a pronunciation
dictionary" with the acoustic and language models [GalesYoung2008] (text). Words outside the vocabulary are handled
by an explicit model of out-of-vocabulary words, built from sub-word units, that "augment[s] the closed-vocabulary
search space" [Bazzi2002] (abstract). For callsigns, a list such as `MASTER.SCP` would be the vocabulary and the
format grammar the out-of-vocabulary model.

**Gains and losses.** A list helps listed calls. Its known cost falls on words outside it: "Not only are they
miss-recognized as in-vocabulary (IV) words with similar phonetics, but the error also causes further errors in
nearby words" [Naptali2012] (abstract). For a skimmer, an unlisted call pulled to a listed one is a busted spot. A
list fed by the system's own earlier output confirms its own mistakes, which is the owner's argument against the
Reverse Beacon Network. Keeping lists out of the decoder is a deliberate two-stage design, against step 1's general preference for
one joint search (P0.2).

**Reason given.** Yes, explicitly (the callsign-search step owns the databases; self-confirmation through RBN).
Step 1's caution applies to how the callsign kind is built: a model that learns letter statistics from many real
callsigns in contest logs can memorize frequent calls, "a callsign list in disguise" (step 1, P6(e)5).

### 3.8 T6.4b: the catch-all weight w = 5 %

**The design.** The catch-all kind gets weight w = 1/20, a heuristic; the notes say standard practice sets it on
held-out text.

**The standard.** Interpolation weights are estimated on held-out data: Chen and Goodman describe the
Jelinek-Mercer weights as found "using the Baum-Welch algorithm" on held-out data, and in their experiments the
weights "were trained using the Baum-Welch algorithm on the second of the three held-out sets" [ChenGoodman1999]
(text).

**Gains and losses.** A held-out estimate matches the real rate of strings the other kinds cannot explain; a fixed w
guarantees the derived bound (no word below w times its probability without a text model) whatever the data. The
notes have the bound right (T6.4a) and are frank that the value is not set by the standard method.

**Reason given.** The heuristic is labeled and the standard named; no plan yet. Held-out text of real traffic is
scarce (T6.8), so the plan needs one: the owner's planned transcriptions could provide it.

### 3.9 T6.5: past text = the current word plus the kind of the previous word

**The design.** The text model conditions on the word in progress and on the kind (and, for fixed tokens, the
identity) of the previous word; hypotheses that differ inside the current word do not merge until it ends.

**The standard.** Choose the context length (the n-gram order) by measurement: by the perplexity (or cross-entropy)
of text not used for training and, end to end, by error rate against CPU. Chen and Goodman measure how "n-gram
order (bigram vs. trigram)" changes performance "through the cross-entropy of test data" [ChenGoodman1999] (text).

**Gains and losses.** A longer context predicts better but keeps more hypotheses apart (fewer merges, more CPU);
the notes state this trade-off in words ("more text, more correction, fewer merges, more CPU"). Keeping the whole
current word as context, without a bound, makes the number of unmerged hypotheses grow with word length; a
character model of fixed order would bound it.

**Reason given.** Yes, qualitatively; no measurement plan for the context length. The first prototype has no text
model (T6.6), so the comparison can be measured when the kinds are added.

## 4. Class (iv) in detail

### 4.1 The five choices with no standard

| ID | Choice | Checked derivation? | Measurement plan with a stated method? | Verdict |
|---|---|---|---|---|
| T2.3b | Farnsworth gap factors 1, 1.68, 2.83, 4.76, 8.00 | Only the top value: 7.84 at 18 WPM characters and 5 WPM overall, by the ARRL rule; I reproduce it from Bloom's formula (C9). None for the count (5) or the spacing (ratio 8^(1/4) = 1.68) | No; the range is reopened (D9) | **Neither, for the count and spacing** |
| T2.3c | The factor changes only at word boundaries, by one step | No | No | **Neither** |
| T5.4b | Pruning may change the result at most once per 1 000 letters | A reasoned target against measured error floors (0.5–1 % on clean signals; in `docs/plans/`, which I may not read) | Yes: decode the development set with a very wide beam, narrow the beam until results differ at most once per 1 000 letters | Plan with method |
| T5.6 | CPU target 2–3 million floating-point operations per channel-second | An unmeasured estimate from the spec (§5.1), labeled as such | Yes: measured on the first prototype, with accuracy (T5.8) | Plan with method |
| T6.9 | Contest logs never committed; IZ4KBW credited | Not needed (a data policy) | Not needed | n/a |

What a derivation for T2.3b and T2.3c could rest on (my input, not the notes'):

- **Spacing.** A true stretch midway between two grid values is off by +29.7 % or −22.9 % (ln error 0.26), which
  costs 0.375 nats per letter or word gap at σ = 0.3 and 13.5 nats per gap at σ = 0.05 (C9). The speed ladder's
  ratio of 1.1 costs 0.013 nats per element at σ = 0.3. Deriving the ratio from σ, as the speed ladder's ±4.9 % was
  argued against the fist scatter, would make the grid follow from the timing model.
- **Range.** The ARRL standard of 1990 sends characters at 18 WPM below 18 WPM overall: stretch 7.84 at 5 WPM
  [Bloom1990] (text). Since July 2001 the ARRL VEC and W1AW's code practice use 15 WPM characters: stretch 6.26 at
  5 WPM (my arithmetic from Bloom's formula; the change from [ARRL2001], read only as a search-engine extract
  because the bulletin page returned "not found" to my direct fetch). Larger stretches need faster characters
  (15.74 at 33 WPM characters and 5 WPM overall, the development set's top), which come from training programs, not
  from a published standard I found.
- **Changes within an over.** Bloom specifies Farnsworth spacing per transmission ("Speeds are specified as s/c"),
  so the stretch should be constant during an over and change only with the station. That suggests choosing it at
  birth and holding it, which would also cut the 9 speed-and-factor variants at each word gap to 3. This is my
  reading, not a measured fact.

### 4.2 Heuristic values inside the other rows ("no darts" status)

The owner's rule asks for a checked derivation or a measurement plan for every heuristic number. Classes (ii) and
(iii) also contain numbers and settings; this is their status at commit b51eefe. R1.5 promises the plans in
topic 9, so "none" means "not yet written", not "refused".

| Value or setting | Row | Status |
|---|---|---|
| Speed-change probability per word boundary | T2.2 | Check named (speed-change group); no method for the value |
| "Drift within a word is absorbed by the fist scatter" | T2.2 | Check named (same group) |
| Heavy tail: form and weight | T2.4b | None (not yet chosen) |
| σ = 0.3 for marks, and for gaps | T2.4d | None (taken from the generator; circularity acknowledged) |
| "Noticeably" (trigger for two fist classes) | T2.4e | Check named; threshold undefined |
| Tap length 10 ms | T3.1 | Derived (checked, C3, C4) |
| Prior probability of key-down used by the weights | T3.2 | None (not stated; review 1's numbers imply 0.44, C8) |
| 0.5 s of key-down time | T3.3 | Test named ("to be tested on fading"); no method |
| What "the hypotheses agree are gaps" means | T3.4 | None (undefined) |
| Conversion of the detector's level to A | T3.6 | None yet (derivable, see T3.6) |
| Piece length 2 ms | T4.3 | Derived (checked; conservative, C5) |
| Branch-creation threshold | T5.1 | None (not set) |
| ε of the rescue births | T5.3b | Derived bound only (ε > 1/B, checked); value not set |
| Trigger "wherever a mark plausibly begins" | T5.3b | None (undefined) |
| Beam factor | T5.4a | Plan with method |
| Ceiling and floor on hypotheses | T5.5, T5.8 | Plan with method |
| 50 ns per hypothesis per piece; 1 700 hypotheses; "a few hundred" | T5.5 | Estimates, to be replaced by the planned timing |
| Blend of sources for the fixed mix of kinds | T6.3a | None (not chosen) |
| N, the words counted for a per-channel mix | T6.3b | "N is measured"; no method |
| w = 5 % | T6.4b | None (standard method named, not adopted) |
| Text-model weight | T6.4c | Criteria named; no procedure |

In all, of these 21: 2 derived, 1 with only a derived bound, 3 with a measurement plan and a stated method, 6 with
a check named but no method, and 9 with neither. Together with T2.3b and T2.3c, 11 numbers or settings have neither
a derivation nor a measurement plan yet.

## 5. Standard practices not used or not addressed

Every step-1 decision that no choice in the notes answers, grouped by where the notes stand on it, with the topic
that would take it. Decisions the notes answer are in the table of section 2; P1.7 (soft decisions), P5.6 (no
automatic gain control in the decoder's path) and P10.11 (the error metric, fixed by the spec) are met without a
row of their own.

### 5.1 Reopened by the owner after review 1 (R1.2)

| Step 1 | Standard practice | Topic | Comment |
|---|---|---|---|
| P1.3, P3.4 | Estimate and track the carrier frequency (periodogram acquisition; Kalman filter or phase-locked loop; hidden Markov model over frequency cells at low SNR) | 4 | The most consequential gap (section 3.6). The engine already has a frequency tracker in the Matched decoder (`signal-processing.md` §7, A.7) that could serve as a front end |
| P1.4, P1.5 | Under fading: a gain model from the Doppler spread; coherent integration only within the coherence time, blocks combined noncoherently; the "estimator-correlator" likelihood for a randomly faded mark | 4 (and 3 for the tracker, section 3.5) | Reopened as "changes of phase and amplitude within a mark" (D8) |
| P2.7 | Farnsworth range from the sending standard | 2 | Section 4.1 adds W1AW's 15 WPM characters since 2001 |
| P3.1, P3.3 | Per-survivor or decision-directed amplitude and noise estimation | 3 | Reopened as K3 (section 3.4) |
| P2.2, P4.5 | Fair comparison of hypotheses in mid-element with finished ones: the explicit-duration recursion scores an unfinished segment with its survival probability, so all hypotheses at one time step are comparable | 5 | Reopened as O4. The language-model part of the same problem has its own standard, look-ahead (5.4) |
| P2.6, P3.6 | Priors at birth: re-acquisition from broad priors informed by the detector | 5 | Reopened as K8, O7 |

### 5.2 Raised by review 1, not yet discussed (R1.3, R1.4)

| Step 1 | Standard practice | Topic | Comment |
|---|---|---|---|
| P5.2 | Filler models for non-Morse keying, with a tuned entry penalty; check by characters emitted per hour on noise-only, carrier-only and random-keying input | New topic ("the channel's other contents") | The states exist (T1.1); their models do not |
| P5.4 | Impulsive noise: a heavy-tailed likelihood, blanking, or a short "impulse" event in the model | New topic | |
| P5.5 | Other signals: selectivity, a joint model of both stations, or cancellation; in a skimmer, the neighbor's own decoder output can be shared | New topic | Also the spec's B10 item |
| P2(e), P5.2 | Element sequences that spell no letter, and the ITU eight-dot "error" signal | New topic | |
| P6.6 | Word (here: letter or word) insertion penalty tuned with the language-model scale | 6 | Raised as O11; [GalesYoung2008] gives scale 8–20 and penalty 0 to −20 for speech |
| P6.3, P4.6 | Interpolated mixtures sum over their components; committing a hypothesis to one word kind is the Viterbi approximation of that sum | 6 | Raised as K9, O12 |
| P2.7 | Keyer weighting (an additive mark/space bias) and the dash/dot ratio as sender parameters | 2 | Raised as O13; the test set has up to ±0.1 dot of imbalance, and DeepCW's generator draws one with a standard deviation of 0.1 dot [DeepCW] |

### 5.3 Deferred to topics 7 to 9

| Step 1 | Standard practice | Topic |
|---|---|---|
| P7.1 to P7.5 | Change of station: change-point or latent-station models; changes only in gaps; reset sender parameters, keep noise; recognize returning stations; re-decode the first word | 7 |
| P4.8, P8.4 | Partial traceback (emit what all surviving hypotheses share) [Brown1982], a forced decision at a maximum delay, interim results with a stability measure | 8 |
| P4.10, P3.6, P7.5 | Re-decode the opening of a transmission once the estimates have converged, inside the 20 s correction window | 8 |
| P8.1, P4.6 | Per-character posteriors in a confusion network (with "no character" entries); minimum-Bayes-risk (consensus) decoding for a character-error metric | 8 |
| P8.2, P10.8 | Calibration map fitted on held-out data; reliability diagrams, expected calibration error, Brier score, normalized cross-entropy, per condition cell | 9 (promised as K14) |
| P8.3 | Chow's reject rule on calibrated posteriors for the hand-off to the neural decoder [Chow1970] | 8 or 9 |
| P8.5, P8.6 | Element timing from the best path; speed reported with its spread | 8 |
| P10.2, P10.5 to P10.7, P10.9, P10.10 | Disjoint data, paired tests with signals as units, bootstrap intervals, threshold-SNR fits, fading test length, blind and double transcription, genie-aided bounds | 9 (blindness of the transcription is promised there, T6.8) |

### 5.4 Not addressed anywhere in the notes

| Step 1 | Standard practice | Topic that would take it |
|---|---|---|
| none in step 1 | **Language-model look-ahead:** anticipate the text model's probability at each node of the code tree (the best letter reachable from it), so that pruning compares hypotheses fairly before a letter completes. Ney and Ortmanns describe it as used "in many systems for large-vocabulary speech recognition" [NeyOrtmanns1999] (text), and their 2000 paper, which the notes cite, lists it among its main methods [NeyOrtmanns2000] (abstract) | 5 or 6 (it is part of O4) |
| P1.6 | The noise correlation across a mark's edges (about 4 ms), which the notes call an approximation (U10), with no plan to measure or remove it | 4 |
| P2.8 | Durations conditioned on the previous element and on the keying device (bugs make dots and their spaces mechanical) | 2 |
| P2(h) | Speed errors near ×3 and ×1/3 (a run of dashes looks like a run of dots at a third of the speed) counted separately | 9 |
| P3.2 | Correction of the noise estimate's known bias (minimum statistics corrects it analytically; probability-weighted estimators apply a factor) | 3 |
| P3.7 | Process noise of each tracker derived from physics, and filter consistency checks (normalized error tests) | 3, 9 |
| P3.8 | Plugging in an estimate of A versus carrying its uncertainty into the likelihood, which matters for calibrated output | 3, 8 |
| P4.9, P9.5 | Determinism in benchmark mode: deterministic tie-breaks when merging and pruning equal scores, a fixed order of floating-point sums, load-independent settings (the overload governor of T5.7 changes ceilings with load) | 5, 9 |
| P9.3 | Overload priorities: new channels first (time to spot), then uncertain ones | 5 |
| P9.4 | CPU time measured on a desktop and on a Raspberry Pi 5, as the spec's metric requires (T5.8 names one Linux machine) | 5 |
| P9.6 | Block processing and vectorization across hypotheses | implementation plan |
| P6.2 | Smoothing (modified Kneser-Ney or Witten-Bell) and order for the "plain language" letter statistics | 6 |
| P6.5 | A cache of tokens repeated within a contact (callsigns, names, reports), as distinct from the notes' adaptation of the mix of kinds; it must not let a misread call confirm itself | 6 |
| P6.7 | Held-out perplexity (bits per character) as the intrinsic test of each text kind | 6, 9 |
| P0.4 | Compiling the network as weighted finite-state transducers; a hand-built network is an equally standard option for an alphabet this small, so no action is needed | none |

## 6. Citation checks

Verdicts: **confirmed** (the source says what the notes claim), **partly** (it says part of it, or says it with a
qualification that matters), **wrong**, or **not checkable here** (outside the files and sources I may read).

### 6.1 Published sources

| Source as cited | Where | What the notes claim | What I found | Verdict |
|---|---|---|---|---|
| Bell (1977), via "Bell had one" pause element | T1.3 | Bell had a separate pause | Table I lists six symbols, the last "Pause 14" (elements) [Bell1977] (text) | Confirmed |
| Bell (1977), "Bell's choice at its start" | T1.2b | Bell decides dot or dash when the mark begins | Bell makes the key-state transition probability depend on "the type of Morse symbol being keyed" (dot, dash, ...), each symbol with its own duration density, extended "for one more sample interval" at a time [Bell1977] (text) | Confirmed |
| Bell (1977), "the mistake of Bell's receiver chain" | T4.7 | Bell's chain treats correlated samples as independent, making the decoder overconfident | Bell states the condition for independent samples (interval at least 1/(2B), B the band-pass width) and that his chosen interval "may render the independent noise assumption invalid", its "effect ... minimized" by making the interval as large as other constraints allow [Bell1977] (text). He knew; whether the result was overconfident is the project's finding | Partly (the overconfidence is not checkable here) |
| Bell (1977), "Without them the decoder can stay lost, as Bell's did" | T5.3 | Bell's decoder did not recover | Not in the parts of Bell I read; it refers to the project's port (`docs/plans/`) | Not checkable here |
| VE3NEA's DeepCW generator | T2.4 | Log-normal length factor; "no source shows that it was measured on real keying"; hand-style medians 4.48 dots (dashes, letter gaps) and 7.39 dots (word gaps) | Code: "element lengths have log-normal distrubution, so we take an exp() of a normal r.v."; hand-key means of ln-length 1.50 and 2 (e^1.5 = 4.48, e^2 = 7.39 dots); no source for the timing values. The notebook says its data have "statistical properties of the real-world messages", but documents a measurement only for character frequencies and word lengths, "collected from ... CW messages decoded with CW Skimmer" [DeepCW] (code) | Confirmed |
| The ARRL timing rule | T2.3 | Gaps 7.84 times nominal for 18 WPM letters at 5 WPM overall | Bloom's formulas t_a = (60c − 37.2s)/(sc), t_c = 3t_a/19, t_w = 7t_a/19 give 7.84 (C9) [Bloom1990] (text) | Confirmed |
| Proakis and Salehi, §4.5-1 | T4.6 | The noncoherent matched filter is textbook theory | §4.5-1 is "Noncoherent Detection of Carrier Modulated Signals", in §4.5 "Optimal Detection in Presence of Uncertainty: Noncoherent Detection" [Proakis2008] (table of contents only) | Confirmed as to the section; content not read |
| Rabiner 1989, §IV.D | T4.6 | Segment scoring in hidden semi-Markov models | §IV.D "Inclusion of Explicit State Duration Density in HMMs": the observations of a state's whole duration come from "the joint observation density", and "Generally we assume independence" of them [Rabiner1989] (text). The joint form is segment scoring; Rabiner's worked equations use the product form | Confirmed |
| Ostendorf, Digalakis and Kimball 1996 | T4.6 | Segment scoring | Segment models represent "a variable-length sequence of observation vectors" [Ostendorf1996] (abstract); Murphy's account gives the segment likelihood P(y_t0:t1 \| q, l) [Murphy2002] (text) | Confirmed |
| (no source: a search claim) | T4.6 | "no published Morse decoder doing exactly this is known to us (not searched specifically)" | Bell sums nothing per hypothesis (envelope samples into a Kalman filter per path) [Bell1977]; Mills uses one moving-sum matched filter whose length follows the nominal speed [Mills1977]; RSCW, for machine-sent satellite Morse, takes the noncoherent magnitude of a one-bit moving average and scores dibits, with no coherent sum over a mark [RSCW] (Bell and Mills: text; RSCW: its algorithm page, through a summarizing fetch tool). I found no decoder that sums each hypothesis's mark coherently | Confirmed as far as I searched |
| Raheli, Polydoros and Tzou 1995 | T4.6 | Per-survivor processing was listed by mistake; "this design does not use it" | Per-survivor processing embeds "data-aided estimation of unknown parameters" in the Viterbi algorithm [Raheli1995] (abstract). The design carries no parameter estimate per hypothesis; the running sum is the segment's statistic, not an estimate carried forward | Confirmed (the correction is right) |
| Rabiner 1989 | T5 | Viterbi beam search is the standard decoder of speech recognition | Rabiner names "various forms of Viterbi beam searches" (citing Lowerre and Reddy) as the alternatives to exhaustive search for large networks, details "beyond the scope" [Rabiner1989] (text). Stronger support: [GalesYoung2008], [NeyOrtmanns1999] (text) | Confirmed (weakly by this source) |
| Young, Russell and Thornton 1989 | T5 | The "token passing" form | Tokens passed around a network, "keep best token", Viterbi recursion (their eq. 4), word link records for traceback; no pruning in this report [Young1989] (text) | Confirmed |
| Ney and Ortmanns 2000 | T5 | Scores "held relative to the best (divided by the best score at every piece, as beam pruning does)" | Abstract only for the 2000 paper. The companion tutorial: prune a hypothesis if Q < f_AC·Q_best, "the score of the best hypothesis is determined, then all hypotheses whose scores fall short of this optimal score by more than a fixed factor are pruned" [NeyOrtmanns1999] (text). Pruning works relative to the best; dividing the stored scores by the best is a numerical convenience the text I read does not describe, and, as the notes say, it changes no decision | Partly |
| Rabiner 1989, eq. (105) | T5 | Scores may be kept as logarithms | "no scaling is required if we use logarithms in the following way", eqs. (105a–c) [Rabiner1989] (text) | Confirmed |
| Rabiner's "scaling" (correction note) | T5 | Rabiner's scaling divides the forward variables by their sum | Scaling coefficient c_t = 1/Σᵢ α_t(i), eqs. (91)–(92) [Rabiner1989] (text) | Confirmed |
| Rabiner 1989, §III.A; Wessel et al. 2001 | T5 | Posteriors from the forward recursion or forward-backward over a lattice | §III.A is "Solution to Problem 1", the forward-backward procedure [Rabiner1989] (text); word posteriors "computed on word graphs using a forward-backward algorithm" [Wessel2001] (abstract) | Confirmed |
| Lowerre 1976 | T5.4 | "a beam plus a maximum count (standard in speech recognition: beam search, Lowerre 1976)" | HARPY "searches only a few 'best' paths in parallel. 'Best' is defined by a heuristic threshold on the probabilities of the states being searched" [Lowerre1976] (text of the thesis summary). The maximum count is histogram pruning, from Steinbiss, Tran and Ney [Steinbiss1994] (text), which the notes call standard (item 5) without a source | Partly (the beam, not the cap) |
| Brown et al. 1992 | T6.2 | Class-based models whose classes are learned by clustering | Class model Pr(w_k \| c_k)·Pr(c_k \| history); "algorithms for assigning words to classes based on the frequency of their co-occurrence" [Brown1992] (text) | Confirmed |
| Jelinek and Mercer 1980 | T6.2, T6.4 | Interpolation with a uniform distribution; the weight set on held-out text | The original is not online (record only). Chen and Goodman describe the Jelinek-Mercer class of interpolated models, ending the recursion either with the first-order model or "the uniform distribution", with weights trained by Baum-Welch on held-out data [ChenGoodman1999] (text) | Confirmed through a secondary source |
| Kuhn and De Mori 1990 | T6.3 | A cache model "raises recently used words"; the notes' version adapts the prior over kinds instead | The cache "reflects short-term patterns of word use" [KuhnDeMori1990] (abstract); in Kuhn's earlier paper the cache keeps "recent frequencies of words within each POS" (part of speech) and leaves the class model unchanged [Kuhn1988] (text) | Confirmed; the label "our variant" is fair, and the closer standard is dynamic mixture weights (T6.3b) |

### 6.2 Claims the notes attribute to review 1, re-derived

| Item | Claim | My result | Verdict |
|---|---|---|---|
| D2 (T4.7) | Pieces treated as independent understate a mark's noise variance by 1.9–2.1 (2.8–3.3 dB) on the real filter, 1.7–1.8 (2.2–2.6 dB) on an ideal 300 Hz channel, for marks of 8–720 ms | 1.91–2.14 (2.81–3.30 dB) and 1.65–1.82 (2.18–2.59 dB) (C2) | Confirmed |
| T4.1, T4.7 | Noise correlated for about 3.3 ms (ideal), about 4 ms on the real filter | First zero of the noise correlation at 3.33 ms (ideal) and 3.98 ms (real filter) (C1) | Confirmed |
| T4.7 | "Proportional to T" overstates the variance by 0.3–0.5 dB for 8–12 ms sums, under 0.1 dB from 48 ms | 0.49 dB (8 ms), 0.32 dB (12 ms), 0.08 dB (48 ms) (C2) | Confirmed |
| T3.1 | Tap noise bandwidth about 91 Hz; SNR about 4.4 dB above one sample | 91.4 Hz; 4.40 dB (C3) | Confirmed |
| D4 (T3.1) | A 10 ms mark peaks 0.8–1.0 dB below A²; 27 % of 100 WPM dots are shorter than 10 ms at σ = 0.3 | 0.66–0.94 dB over start offsets within a piece; 27.2 % (C4) | Confirmed in substance (my lower end is 0.66 dB) |
| D5 (T3.2) | Bias of the amplitude estimate at its fixed point −0.9 to +0.7 dB | A simulation whose settings the notes do not give | Not re-derived |
| D6 (T3.2) | Noise-only taps weighted 0.12 at S₅₀₀ = 0 dB and 0.25 at −3 dB; 2–4 s of silence refill the 0.5 s window | 0.120 and 0.251 with a key-down prior of 0.44; 0.5 s / 0.25 = 2 s and 0.5 s / 0.12 = 4.2 s (C8) | Confirmed (under the unstated prior 0.44) |
| D7 (risk) | At ±2 Hz, 13 dB lost on a 5 WPM dash and 19 dB on an 8 WPM dash | 13.3 dB and 19.2 dB (C6) | Confirmed |
| Risk paragraph | 0.5 Hz over a 720 ms dash is about a third of a cycle | 0.36 cycles | Confirmed |
| D8 (risk) | Fading within a mark costs up to about 7 dB on a 5 WPM dash at a 5 Hz Doppler spread | 6.86 dB if the spread is twice the standard deviation of the Doppler spectrum (the ITU convention); 9.7 dB if it were one standard deviation (C7) | Confirmed (ITU convention) |
| D9 (T2.3) | The development set's Farnsworth group reaches 15.7 | 15.74 at 33 WPM characters and 5 WPM overall by the ARRL formula (C9); the set itself not checkable here | Confirmed as arithmetic |
| D11 (T4.1) | Piece edges cost 0.37 dB on average and 0.72 dB at worst on a 12 ms dot, under 0.1 dB at 25 WPM; 1.33 ms pieces save about 0.1 dB | In a white-noise model with unfiltered marks: 0.37 dB mean, 0.67 dB worst, 0.09 dB at 48 ms, 1.33 ms pieces 0.24 dB (saving 0.13 dB) (C5b). On the real channel filter with its correlated noise: 0.30 dB mean, 0.41 dB worst against the ideal E/N₀ (C5) | Confirmed, and conservative |
| D12 (T6.4) | The catch-all bound and its three conditions; w^λ at weight λ | For a mixture over whole words, P(word) ≥ w·P_catch-all(word); raising both sides to the power λ gives w^λ | Confirmed |
| D14 (T5.2) | The merging state must include the gap kind and the non-Morse clock | Both affect future probabilities, so two hypotheses differing in them can be told apart later | Confirmed |
| D15 (T5.3) | A newborn survives the beam only if ε > 1/B | Newborn score = best × ε; the beam keeps scores ≥ best/B | Confirmed |
| S15 (T3.4) | The bank's level mask reads the noise low by 0.78–0.84 | `signal-processing.md` §8c: correction factors 0.84 (branch 1) to 0.78 (branch 32) | Confirmed |
| S20 (T6.4) | Speech recognizers tune the weight together with an insertion penalty | "a word insertion penalty is added", scale typically 8–20, penalty 0 to −20 [GalesYoung2008] (text) | Confirmed |
| U8, U10 | The detector-level conversion is not stated; the edge correlation is ignored | Both true of the notes | Confirmed |

### 6.3 Project files

| File as cited | Claim | Verdict |
|---|---|---|
| `docs/plans/2026-10-07-bell-decoder-stage-1.md`, `docs/plans/2026-10-07-bell-stage-1-results.md`, `docs/plans/2026-10-06-development-set-results.md` | Bell's program does not reproduce his tables and decodes worse; the bank's CER of 0.005 at high S₅₀₀ in speed cells 2–5; the Bell port's 0.55–0.82 % at 20 WPM | Not checkable here (outside the files the brief lets me read) |
| `docs/research/bell-1977-reference.md`; `docs/research/research_notes/CW conversation text sources/sources.md`; review 1 | Bell's model; the text sources; the review's findings | Not read, as the brief requires; review 1's derivations re-derived in 6.2 |
| `docs/signal-processing.md` §5, §8c, A.7 | The detector's level is 3.6–7.1 dB below the key-down SNR per bin; the bank's 0.78–0.84 factors; the channel centered once on a 23.4 Hz bin, never moving | Confirmed |
| Design spec §5.1 | The 2–3 million floating-point operations per channel-second are an estimate, not measured | Confirmed |
| Design spec §5.2, step 1 | A ±2 Hz re-centering target | Confirmed ("for example ±2 Hz, a 0.2 dB loss at 20 WPM", stated there for a one-dot filter) |
| Design spec §5.1 and §5.2, step 3 | The spec still describes a Kalman amplitude per hypothesis, filters matched to each hypothesis's speed, speed changes at element boundaries | Confirmed (step 3 and its 2026-10-07 amendments) |
| Design spec "§5, step 5; §6" | The callsign-search step owns the databases; RBN is never matched | §6 confirmed (the RBN rule is there). **"§5, step 5" is wrong**: §5.2 has steps 1–4; callsign search is item 5 of §4.1, "Data flow inside the engine" |

### 6.4 Data sources named in topic 6

| Source | Claim | What I found | Verdict |
|---|---|---|---|
| `cty.dat`, `MASTER.SCP` | Callsign databases for the callsign-search step | supercheckpartial.com answered; country-files.com refused my automated request (HTTP 403); both are named in the spec (§6) | Exist (record) |
| Reverse Beacon Network | Not to be used for matching | reversebeacon.net answered | Exists |
| Public contest logs (CQ WW, CQ WPX, ARRL) | Text for the callsign, token and number kinds | CQ WW and CQ WPX public-log pages answered; the ARRL page offers "ARRL Public Logs" for its contests [ARRL-PL] | Confirmed |
| "the contests' public log-checking reports" | Real copy errors | CQ WW makes public "the Log Checking Reports of the World high scoring stations in various categories" (2012–2024), listing bad calls, not-in-log calls and exchange errors [CQWW-LCR]; other entrants' reports are private | Partly: public, but only for top-scoring stations |
| "the standard abbreviation and Q-code lists" | Token kind | Not specific enough to check | Not checked |
| IZ4KBW's archive | Real recordings for the real check | "thousands of records" of real QSOs, organized by date, calls, frequencies, key used and WPM, in a shared folder [IZ4KBW] | Confirmed |
| HamSCI's recordings | Real recordings | HamSCI accepts "raw I/Q data recordings or large audio files" on Zenodo [HamSCI]; which of them hold CW traffic I did not check | Partly |
| "No public transcripts of real ragchews exist, and no measured statistics of real CW conversation" | Negative claim | My searches found no transcripts either. DeepCW carries character frequencies and a word-length distribution measured on CW Skimmer's decodes of on-air traffic [DeepCW]: measured, but of all traffic (not conversation), at the unigram level, and with decoding errors | Confirmed, with that qualification |

## 7. Where I disagree with step 1

Step 1 is careful and, on the points I checked, accurate. These are the places where I find it wrong or incomplete
in a way that changes a classification, followed by smaller additions.

**Changes a classification**

1. **Noise estimated on the decoder's own gap decisions (P3.2; design T3.4).** Step 1 calls the decision-directed
   estimate from key-up frames an adaptation with no standard (A), and the notes call it "our method". In speech
   enhancement it was the usual method: Martin introduces minimum statistics by contrast, "In contrast to other
   methods, our approach does not use a voice activity detector" [Martin2001] (abstract), and Cohen's estimator
   averages "past spectral power values, using a time-varying frequency-dependent smoothing parameter that is
   adjusted by the signal presence probability" [Cohen2003] (abstract). Bias is the known weakness these methods
   work around; Martin's derives "an unbiased noise estimator" from the statistics of spectral minima. So T3.4 is
   (ii), standard but adapted (the "detector" is the decoder's own hypotheses), not the project's own.
2. **Rescue births (design T5.3b).** Step 1 lists Bayesian online change-point detection under abrupt speed changes
   and station changes (P2.6, P7.1) but does not connect it to restarting hypotheses. The connection is exact: in
   Adams and MacKay's recursion the restart's mass is P(r_t = 0, x_1:t) = Σ P(r_(t−1), x_1:(t−1))·π_t·H, with a
   constant hazard H for a memoryless change-point process [Adams2007] (text). Replacing the sum by a maximum, as
   Viterbi does, gives the notes' "best current score times ε". Herbster and Warmuth's fixed share, in which "each
   expert 'shares' a portion of its weight with the other experts", is the same device in on-line learning
   [Herbster1998] (text). So T5.3b is (ii), not the notes' "our choice".
3. **No callsign lists (design T6.1).** Step 1's brief fixed this as the owner's decision, so step 1 gave only the
   format-model answer (P6.4). The standard alternative, a vocabulary in the search plus an out-of-vocabulary model
   [GalesYoung2008], [Bazzi2002], exists, so T6.1 is (iii) with a stated reason, not a choice where nothing
   standard exists.

**Incomplete, without changing a class**

4. **Language-model look-ahead** is missing from step 1 (P4.5, P6.6). It is the standard remedy for comparing, under
   one beam, hypotheses that have and have not yet received a language-model factor [NeyOrtmanns1999] (text), which
   is the notes' reopened question O4 (section 5.4).
5. **Correlated noise (P1.6).** Step 1 names whitening or decimation as the standard answers. Computing the exact
   variance of the sum statistic from the noise autocovariance, as the design does (T4.7), is a third, equally
   standard answer; T4.7 stays (i).
6. **Model class (P0.3, P4.1).** Step 1's table gives the switching state-space model (continuous nuisance states
   inside the hypotheses) as the standard answer, but its own section 2 (item 5) says nuisance parameters are
   handled "either by estimation outside the search ... or inside it by augmenting the state". Both routes are
   standard, so the design's discrete model with channel-level estimates is not a deviation by itself; the deviation
   is the particular amplitude estimator (T3.0).
7. **Bell's sampling (P1(f)).** Step 1 says Bell's 5 ms samples were "chosen so that noise samples are independent".
   Bell's own text is more cautious: independence holds if the interval is at least 1/(2B) and the post-detection
   low-pass is wider than 2B, and the chosen value "may render the independent noise assumption invalid"
   [Bell1977] (text). This bears on the notes' phrase "the mistake of Bell's receiver chain" (section 6.1).
8. **Farnsworth on the air (P2.7).** Step 1 gives the 1990 ARRL rule (18 WPM characters). Since July 2001 the ARRL
   VEC and W1AW's code practice use 15 WPM characters [ARRL2001] (search extract), so the largest stretch in that
   on-air use is 6.26 at 5 WPM, below the design's 8.00 and far below the development set's 15.7. This matters for
   the reopened Farnsworth range (D9).
9. **Speed grid (P2.4).** Step 1 judges a 10 % grid small against a 15 % hand-keying scatter (J), which supports the
   notes. The music evidence step 1 cites is less permissive: Krebs et al. spaced tempo states to mimic a 2–5 %
   just-noticeable difference and found accuracy converging only at about 40 to 75 states over a tempo range of
   about 4:1 [Krebs2015] (text). For hand keying the notes' argument holds; for machine keying with a narrow fist
   model it would not (T2.1).

## 8. References

I found every source myself. "Checked" says what I verified: **text** (I read the work or the cited part),
**abstract**, **record** (only the bibliographic record), **contents** (the table of contents), **code**, or **page**
(a web page, some read through a summarizing fetch tool, as marked). Records and abstracts not otherwise marked come
from OpenAlex (api.openalex.org) through my helper `step2_lookup.py`. Texts I downloaded are in
`build/standard-practice-check/step2-downloads/` (git-ignored; each can be fetched again from its URL and the folder
deleted).

### 8.1 Published sources

- **[Adams2007]** Adams, R. P., MacKay, D. J. C. (2007). "Bayesian Online Changepoint Detection." arXiv:0710.3742.
  https://arxiv.org/abs/0710.3742 . Checked: text (§2, Algorithm 1).
- **[Baddour2005]** Baddour, K. E., Beaulieu, N. C. (2005). "Autoregressive modeling for fading channel
  simulation." *IEEE Transactions on Wireless Communications* 4(4):1650–1662. doi:10.1109/TWC.2005.850327 . Checked:
  abstract.
- **[Bazzi2002]** Bazzi, I. (2002). *Modelling out-of-vocabulary words for robust speech recognition.* Ph.D.
  thesis, Massachusetts Institute of Technology. http://hdl.handle.net/1721.1/29241 . Checked: abstract.
- **[Bell1977]** Bell, E. L. (1977). *Optimal Bayesian estimation of the state of a probabilistically mapped
  memory-conditional Markov process with application to manual Morse decoding.* Ph.D. dissertation, Naval
  Postgraduate School, Monterey, CA. https://hdl.handle.net/10945/26668 ; OCR text:
  https://archive.org/details/optimalbayesiest1094526668 . Checked: text (Table I; the key-state, speed and
  amplitude models; the observed-noise section).
- **[Bloom1990]** Bloom, J. (KE3Z) (1990). "A Standard for Morse Timing Using the Farnsworth Technique." *QEX*,
  April 1990, pp. 8–9. ARRL. http://www.arrl.org/files/file/Technology/x9004008.pdf . Checked: text (scanned pages
  read as images, including Appendix A).
- **[Brown1982]** Brown, P. F., Spohrer, J. C., Hochschild, P. H., Baker, J. K. (1982). "Partial traceback and
  dynamic programming." *Proc. ICASSP 1982*, pp. 1629–1632. doi:10.1109/ICASSP.1982.1171441 . Checked: abstract.
- **[Brown1992]** Brown, P. F., Della Pietra, V. J., deSouza, P. V., Lai, J. C., Mercer, R. L. (1992). "Class-Based
  n-gram Models of Natural Language." *Computational Linguistics* 18(4):467–479. https://aclanthology.org/J92-4003/
  . Checked: text.
- **[CappeMoulines2009]** Cappé, O., Moulines, É. (2009). "On-line expectation–maximization algorithm for latent
  data models." *Journal of the Royal Statistical Society, Series B* 71(3):593–613.
  doi:10.1111/j.1467-9868.2009.00698.x . Checked: abstract.
- **[ChenGoodman1999]** Chen, S. F., Goodman, J. (1999). "An empirical study of smoothing techniques for language
  modeling." *Computer Speech & Language* 13(4):359–394. doi:10.1006/csla.1999.0128 ; copy read:
  https://u.cs.biu.ac.il/~yogo/courses/mt2016/papers/chen-goodman-99.pdf . Checked: text (abstract, §2 on
  Jelinek-Mercer smoothing, the experimental method).
- **[Chow1970]** Chow, C. K. (1970). "On optimum recognition error and reject tradeoff." *IEEE Transactions on
  Information Theory* 16(1):41–46. doi:10.1109/TIT.1970.1054406 . Checked: abstract.
- **[Cohen2003]** Cohen, I. (2003). "Noise spectrum estimation in adverse environments: improved minima controlled
  recursive averaging." *IEEE Transactions on Speech and Audio Processing* 11(5):466–475.
  doi:10.1109/TSA.2003.811544 . Checked: abstract.
- **[Dempster1977]** Dempster, A. P., Laird, N. M., Rubin, D. B. (1977). "Maximum Likelihood from Incomplete Data
  via the EM Algorithm." *Journal of the Royal Statistical Society, Series B* 39(1):1–22.
  doi:10.1111/j.2517-6161.1977.tb01600.x . Checked: record.
- **[DovisTa2012]** Dovis, F., Ta, T. H. (2012). "High Sensitivity Techniques for GNSS Signal Acquisition." In
  Jin, S. (ed.), *Global Navigation Satellite Systems: Signal, Theory and Applications*, InTech, Rijeka, pp. 3–32
  (ISBN 978-953-307-843-4).
  https://iris.polito.it/retrieve/handle/11583/2487019/55828/InTech-High_sensitivity_techniques_for_gnss_signal_acquisition.pdf
  . Checked: text (§3.2, coherent, non-coherent and differential combination).
- **[GalesYoung2008]** Gales, M., Young, S. (2008). "The Application of Hidden Markov Models in Speech Recognition."
  *Foundations and Trends in Signal Processing* 1(3):195–304. doi:10.1561/2000000004 ;
  https://mi.eng.cam.ac.uk/~mjfg/mjfg_NOW.pdf . Checked: text (§1 note 2 on the language-model scale and insertion
  penalty; §2 and §2.4 on decoding, lattices and confusion networks; §4.2 on the dynamic range of posteriors).
- **[GauvainLee1994]** Gauvain, J.-L., Lee, C.-H. (1994). "Maximum a posteriori estimation for multivariate
  Gaussian mixture observations of Markov chains." *IEEE Transactions on Speech and Audio Processing* 2(2):291–298.
  doi:10.1109/89.279278 . Checked: abstract.
- **[Herbster1998]** Herbster, M., Warmuth, M. K. (1998). "Tracking the Best Expert." *Machine Learning*
  32(2):151–178. doi:10.1023/A:1007424614876 . Checked: text (§1).
- **[Huber1964]** Huber, P. J. (1964). "Robust Estimation of a Location Parameter." *The Annals of Mathematical
  Statistics* 35(1):73–101. doi:10.1214/aoms/1177703732 . Checked: abstract (the paper's opening, as given by
  OpenAlex).
- **[Iyer1996]** Iyer, R., Ostendorf, M. (1996). "Modeling long distance dependence in language: topic mixtures vs.
  dynamic cache models." *Proc. ICSLP 1996.* https://www.isca-archive.org/icslp_1996/iyer96_icslp.pdf . Checked:
  text (§3.2 and the reference list).
- **[KneserSteinbiss1993]** Kneser, R., Steinbiss, V. (1993). "On the dynamic adaptation of stochastic language
  models." *Proc. ICASSP 1993*, vol. 2, pp. 586–589. doi:10.1109/ICASSP.1993.319375 . Checked: record (and as cited
  by [Iyer1996]).
- **[Krebs2015]** Krebs, F., Böck, S., Widmer, G. (2015). "An Efficient State-Space Model for Joint Tempo and Meter
  Tracking." *Proc. 16th ISMIR Conference*, Málaga, pp. 72–78.
  https://www.ismir2015.uma.es/articles/239_Paper.pdf . Checked: text (§2, §3).
- **[Kuhn1988]** Kuhn, R. (1988). "Speech Recognition and the Frequency of Recently Used Words: A Modified Markov
  Model for Natural Language." *Proc. COLING 1988*, pp. 348–350. https://aclanthology.org/C88-1071/ . Checked:
  text.
- **[KuhnDeMori1990]** Kuhn, R., De Mori, R. (1990). "A cache-based natural language model for speech
  recognition." *IEEE Transactions on Pattern Analysis and Machine Intelligence* 12(6):570–583.
  doi:10.1109/34.56193 . Checked: abstract.
- **[Liu1991]** Liu, J. W. S., Lin, K.-J., Shih, W.-K., Yu, A. C., Chung, J.-Y., Zhao, W. (1991). "Algorithms for
  scheduling imprecise computations." *Computer* 24(5):58–68. doi:10.1109/2.76287 . Checked: abstract.
- **[Lowerre1976]** Lowerre, B. T. (1976). *The HARPY Speech Recognition System: Thesis Summary.* Department of
  Computer Science, Carnegie-Mellon University, April 1976 (summary of the Ph.D. thesis).
  https://stacks.stanford.edu/file/druid:rq916rn6924/rq916rn6924.pdf . Checked: text of the 19-page summary, not
  the full thesis.
- **[Martin2001]** Martin, R. (2001). "Noise power spectral density estimation based on optimal smoothing and
  minimum statistics." *IEEE Transactions on Speech and Audio Processing* 9(5):504–512. doi:10.1109/89.928915 .
  Checked: abstract.
- **[Mills1977]** Mills, D. L. (1977). *Real-Time Recognition of Manual Morse Telegraphy Using Nonlinear Estimation
  and Viterbi Decoding.* Technical Report TR-554, Department of Computer Science, University of Maryland, College
  Park (DTIC ADA051001). https://archive.org/details/DTIC_ADA051001 . Checked: text (OCR: abstract, the noise model
  with k ≈ 0.15, ratio-weighted estimation, conclusions).
- **[Murphy2002]** Murphy, K. P. (2002). *Dynamic Bayesian Networks: Representation, Inference and Learning.* Ph.D.
  dissertation, University of California, Berkeley. https://www.cs.ubc.ca/~murphyk/Thesis/thesis.pdf . Checked:
  text (§2.3.12, §2.3.14).
- **[Naptali2012]** Naptali, W., Tsuchiya, M., Nakagawa, S. (2012). "Class-Based N-Gram Language Model for New
  Words Using Out-of-Vocabulary to In-Vocabulary Similarity." *IEICE Transactions on Information and Systems*
  E95-D(9):2308–2317. doi:10.1587/transinf.E95.D.2308 . Checked: abstract.
- **[NeyOrtmanns1999]** Ney, H., Ortmanns, S. (1999). "Dynamic programming search for continuous speech
  recognition." *IEEE Signal Processing Magazine* 16(5):64–83. doi:10.1109/79.790984 ; copy read: the authors'
  submitted version of 20 June 1999,
  https://www.isip.piconepress.com/publications/journals_refereed/1999/ieee_spmag/search/_ARCHIVE/dp_search/paper_v0.pdf
  . Checked: text of that version (§3 beam search, §4 tree lexicon, §5.1 pruning, §5.2 look-ahead).
- **[NeyOrtmanns2000]** Ney, H., Ortmanns, S. (2000). "Progress in dynamic programming search for LVCSR."
  *Proceedings of the IEEE* 88(8):1224–1240. doi:10.1109/5.880081 . Checked: abstract.
- **[Ostendorf1996]** Ostendorf, M., Digalakis, V. V., Kimball, O. A. (1996). "From HMM's to segment models: a
  unified view of stochastic modeling for speech recognition." *IEEE Transactions on Speech and Audio Processing*
  4(5):360–378. doi:10.1109/89.536930 . Checked: abstract.
- **[Proakis2008]** Proakis, J. G., Salehi, M. (2008). *Digital Communications*, 5th ed. McGraw-Hill.
  https://highered.mheducation.com/sites/0072957166/information_center_view0/table_of_contents.html . Checked:
  contents (the §4.5 subsection titles, read in a posted copy of the book's front matter; the publisher's page lists
  only the chapters).
- **[Rabiner1989]** Rabiner, L. R. (1989). "A tutorial on hidden Markov models and selected applications in speech
  recognition." *Proceedings of the IEEE* 77(2):257–286. doi:10.1109/5.18626 ; copy read:
  https://people.engr.tamu.edu/rgutier/web_courses/cpsc689_s07/rabiner1989tutorialHMM.pdf . Checked: text (§III.A,
  §IV.D, §V.A with eqs. (91)–(105), §VI).
- **[Raheli1995]** Raheli, R., Polydoros, A., Tzou, C.-K. (1995). "Per-Survivor Processing: a general approach to
  MLSE in uncertain environments." *IEEE Transactions on Communications* 43(2/3/4):354–364. doi:10.1109/26.380054 .
  Checked: abstract.
- **[RifeBoorstyn1974]** Rife, D. C., Boorstyn, R. R. (1974). "Single tone parameter estimation from discrete-time
  observations." *IEEE Transactions on Information Theory* 20(5):591–598. doi:10.1109/TIT.1974.1055282 . Checked:
  abstract.
- **[RosePaul1990]** Rose, R. C., Paul, D. B. (1990). "A hidden Markov model based keyword recognition system."
  *Proc. ICASSP 1990*, pp. 129–132. doi:10.1109/ICASSP.1990.115555 . Checked: abstract.
- **[Sohn1999]** Sohn, J., Kim, N. S., Sung, W. (1999). "A statistical model-based voice activity detection." *IEEE
  Signal Processing Letters* 6(1):1–3. doi:10.1109/97.736233 . Checked: abstract.
- **[Steinbiss1994]** Steinbiss, V., Tran, B.-H., Ney, H. (1994). "Improvements in beam search." *Proc. ICSLP
  1994*, pp. 2143–2146. doi:10.21437/ICSLP.1994-538 . Checked: text (§3, histogram pruning, read as a page image).
- **[StreitBarrett1990]** Streit, R. L., Barrett, R. F. (1990). "Frequency line tracking using hidden Markov
  models." *IEEE Transactions on Acoustics, Speech, and Signal Processing* 38(4):586–598. doi:10.1109/29.52700 .
  Checked: abstract.
- **[Toral2019]** Toral, A. (2019). "Post-editese: an Exacerbated Translationese." *Proc. Machine Translation Summit
  XVII, Research Track*, Dublin. https://aclanthology.org/W19-6627/ ; arXiv:1907.00900 . Checked: abstract.
- **[Wessel2001]** Wessel, F., Schlüter, R., Macherey, K., Ney, H. (2001). "Confidence measures for large vocabulary
  continuous speech recognition." *IEEE Transactions on Speech and Audio Processing* 9(3):288–298.
  doi:10.1109/89.906002 . Checked: abstract.
- **[Young1989]** Young, S. J., Russell, N. H., Thornton, J. H. S. (1989). *Token Passing: a Simple Conceptual Model
  for Connected Speech Recognition Systems.* Technical report CUED/F-INFENG/TR.38, Cambridge University Engineering
  Department, 31 July 1989. https://mi.eng.cam.ac.uk/reports/abstracts/young_tr38.html . Checked: text (the
  PostScript version, §2).
- **[ZhouSchluterNey2020]** Zhou, W., Schlüter, R., Ney, H. (2020). "Full-Sum Decoding for Hybrid HMM based Speech
  Recognition using LSTM Language Model." *Proc. ICASSP 2020.* arXiv:2004.00967 . Checked: abstract.

### 8.2 Standards, software and web pages

- **[ARRL2001]** ARRL (2001). ARRL General Bulletin ARLB024, 19 June 2001: new Morse examination standards from
  1 July 2001, Farnsworth character speed of 15 WPM for the ARRL VEC, W1AW code practice below 18 WPM moved from 18
  to 15 WPM characters from 2 July 2001. https://www.arrl.org/w1aw-bulletins-archive/ARLB024/2001 . Checked: only a
  search engine's extract of the bulletin; the page returned "not found" to my direct fetch.
- **[ARRL-PL]** ARRL. "ARRL Public Logs." https://contests.arrl.org/publiclogs.php . Checked: page (fetch tool).
- **[CQWW-LCR]** CQ World Wide DX Contest. "Public Log Checking Reports." https://cqww.com/publiclcr/ . Checked:
  page (fetch tool).
- **[DeepCW]** Shovkoplyas, A. (VE3NEA). *DeepCW: Morse Code decoder based on Deep Learning*, notebook
  `data_generation.ipynb` (repository created 21 October 2024; MIT license). https://github.com/VE3NEA/DeepCW .
  Checked: code (the "Keying Stats" and text-generator cells and the notebook's descriptions).
- **[HamSCI]** HamSCI. "Submitting eclipse observations to HamSCI."
  https://hamsci.org/article/submitting-eclipse-observations-hamsci . Checked: page (fetch tool).
- **[ITU-R-F1487]** ITU-R (2000). *Recommendation ITU-R F.1487: Testing of HF modems with bandwidths of up to about
  12 kHz using ionospheric channel simulators.* https://www.itu.int/rec/R-REC-F.1487 . Checked: text (Annex 1,
  eq. (2): "the frequency spread on each component is usually determined by 2σ").
- **[ITU-R-M1677]** ITU-R (2009). *Recommendation ITU-R M.1677-1: International Morse code.*
  https://www.itu.int/rec/R-REC-M.1677 . Checked: text (§2, the 1, 3 and 7 dot rules; the eight-dot error signal).
- **[IZ4KBW]** SWLing Post (2021). "Large archive of CW off-air recordings from Italy", 30 November 2021, contributed
  by L. Ciampoli (IZ4KBW). https://swling.com/blog/tag/cw-practice/ ; the archive itself:
  https://drive.google.com/drive/folders/1XaF3Mvwa6WvgOCzZjp0NwzgnYdNHsW2Q (not opened). Checked: page (fetch
  tool).
- **[RSCW]** de Boer, P.-T. (PA3FWM). "RSCW's algorithm." https://www.pa3fwm.nl/software/rscw/algorithm.html .
  Checked: page (through a summarizing fetch tool).

### 8.3 Project files read

- `docs/design/2026-10-09-new-decoder-design-notes.md` at b51eefe (the design).
- `build/standard-practice-check/step-1-brief.md`, `step-1-standard-practice.md`, `numbers.py`, `numbers_out.txt`.
- `docs/signal-processing.md` (§0 to §9, A.7, the bank's ladder in A.8c).
- `docs/design/2026-09-25-kz4ap-skimmer-design.md` (the spec).

### 8.4 My scripts and outputs (in `build/standard-practice-check/`, git-ignored)

- `step2_checks.py` and its output `step2_checks_out.txt`: sections C1 to C12, the re-derivations cited above.
  The channel filter is rebuilt from `signal-processing.md` A.7 (a Blackman-windowed sinc of 4097 taps at
  192 000 samples/s, cut off at 150 Hz, decimated by 128); it reproduces A.7's figures (−0.015 dB at 39 Hz,
  −6.02 dB at 150 Hz and −74.5 dB at 279 Hz, all relative to the passband; noise bandwidth 251.9 Hz), which checks
  the model.
- `step2_lookup.py`: bibliographic records and abstracts from OpenAlex.
- `step2-downloads/`: the texts I read (can be deleted; every one has its URL above).
