# New decoder: design notes

Started 2026-10-09, by discussion between the owner and Claude. These notes record the agenda and, as each topic
closes, its conclusions. Nothing here is decided until its topic has been discussed and its conclusions are written
below.

## Background

Stage 1 of the Bell plan (`docs/plans/2026-10-07-bell-decoder-stage-1.md`) compiled E. L. Bell's 1977 Fortran
decoder and ran it under the conditions of his published tables. The printed program does not reproduce them and,
by its measured curves, decodes worse than the project's existing decoders, so it was dropped as a benchmark (owner,
2026-10-09; `docs/plans/2026-10-07-bell-stage-1-results.md`).

What carries over is Bell's overall structure: the decoder keeps many competing hypotheses about what has been sent
(element, speed, letter in progress, ...), updates their probabilities as the signal arrives, and commits to text
only after enough evidence. His model is documented in `docs/research/bell-1977-reference.md`. The new decoder takes
that structure as its starting point; every part of it is open for discussion below.

## How the discussion runs

- One topic at a time, in the agenda's order. A topic is not opened before the previous one is settled.
- Nothing is pre-decided: proposals, ranges and numbers are discussed within their own topic, not earlier.
- When a topic closes, its conclusions are recorded here, each with whether it is derived, measured, heuristic or
  the owner's decision.
- **Standard practice (owner, 2026-10-14):** every choice is standard practice, with its source, or our own choice,
  labeled as such. Where no standard exists for Morse, the nearest practice (for example of speech and language
  recognition) is adapted, and the adaptation is labeled as ours.
- **No darts (owner, 2026-10-14):** every heuristic number ends with a measurement plan or a real, checked
  derivation. "If it's just a dart thrown at a board, we need a better way."

## Agenda (owner, 2026-10-09)

1. **What a hypothesis tracks.** The elements; the non-Morse states (no signal, steady carrier, nonsense keying);
   whatever else each hypothesis must carry. Open item: how much past text a hypothesis carries depends on the text
   model (topic 6); it is closed when topic 6 is decided.
2. **Timing: speed, spacing and fists.** How speed and the Farnsworth gaps are represented, how they may change and
   over what range, and how element lengths vary for real senders.
3. **Amplitude.** Where the signal level is used, and how it is estimated.
4. **The observation.** What the decoder reads from the bank and its filters; the delays; the noise statistics; the
   time step.
5. **Hypotheses.** Births, branching, merging, pruning; the cost per channel.
6. **The text model.** The probabilities of letter sequences (letters, callsigns, words): what they are and how
   strong. Then back to topic 1's open item.
7. **Station changes between overs.**
8. **Output.** When to print, and corrections.
9. **How we test each decision.**

## Conclusions

### Topic 1: what a hypothesis tracks (owner, 2026-10-09)

**Rule:** a hypothesis carries everything the probability of its next step or of the next observation depends on,
and nothing that is the same for every hypothesis on the channel; that goes to channel level.

Each hypothesis carries:
1. **The kind of activity:** Morse being sent, no signal, steady carrier, or nonsense keying.
2. **Key down or up, and the time spent in it so far.** A mark is held as "a mark lasting so far" and becomes a dot
   or a dash when it ends, instead of Bell's choice at its start: fewer hypotheses for the same information (owner's
   decision; Bell's way is not revisited).
3. **For key-up time, the kind of gap:** between elements, letters or words, possibly resolved when the gap ends.
   No separate "pause" element (Bell had one): anything longer than a word space is either a long word space (the
   over continues: same sender, same speed) or no signal (the over has ended; topic 7). Both hypotheses coexist until
   the next marks decide.
4. **Speed and the Farnsworth gap factor;** their form is topic 2.
5. **The position in the Morse tree:** the letter in progress.
6. **Past text:** the current word so far, plus the kind of the previous word (and which token, if it was a fixed
   token). Closed by topic 6.
7. **The last stretch of keying,** if topic 4 finds that the expected filter output depends on keying before the
   current element.
8. **Its score and a link to its parent,** to trace its text back when printing (topic 8). The score is the
   probability of its single best story (topic 5, amended 2026-10-14).
9. **A clock for each non-Morse state:** how long the carrier or the silence has lasted.

At channel level, outside the hypotheses: the amplitude and the noise level (topic 3); the fist model, fixed
(topic 2, item 4; corrected 2026-10-14, this line had left it open).

### Topic 2: timing — speed, spacing and fists (owner, 2026-10-12)

Guiding principle (owner): the simplest version of Bell's structure that works; a neural decoder may follow, so no
refinement without a measured need.

1. **Speed** is one of 32 values whose dot lengths are 10% apart, covering 5.2–100 WPM (dots of 12.0–230.3 ms; the
   bank's ladder, used only as a grid), nothing finer. A speed error of at most ±4.9% (the grid's worst) is small next
   to a hand sender's scatter and far inside the dot/dash (×3) and letter/word (×7/3) margins (derived). Speed only
   sets the expected lengths of elements and gaps (amended by topic 4: a hypothesis does not listen to a
   speed-specific filter).
2. **Speed changes only at word boundaries, by one ladder step (±10%),** with a small probability (heuristic, to be
   measured). Drift within a word is absorbed by the fist scatter (heuristic). The start of an over is not a speed
   change: hypotheses are born at every ladder speed (topics 5 and 7). Check: the development set's speed-change
   group; if one step per word follows too slowly, allow two steps at a smaller probability.
3. **Farnsworth gap factors:** exactly geometric (owner), 1, 1.68, 2.83, 4.76, 8.00 (ratio 8^(1/4)); the count, 5, is
   heuristic. The top value matches letters at 18 WPM sent at 5 WPM overall (gaps 7.84 times nominal, derived by the
   ARRL timing rule). No source shows that this is the slowest Farnsworth spacing in use, and the development set's
   Farnsworth group reaches 15.7 (review 1, D9; corrected 2026-10-14): the range is reopened (see "After review 1").
   The factor changes only at word boundaries, by one step.
4. **Fist model: fixed, at channel level, never learned while running.** It is the rule that scores every
   hypothesis: how probable an element or gap of a given length is at that hypothesis's speed, when a mark or gap
   ends, and its classification (dot or dash; element, letter or word gap). Form: length = ideal × a random factor
   whose natural logarithm is Gaussian (the generator's form, from VE3NEA's DeepCW generator; no source shows that it
   was measured on real keying, corrected 2026-10-14), with a slightly heavy tail so that one wild element does not
   kill the right hypothesis (its form and weight are heuristic, not yet chosen). Ideal lengths 1, 3, 7 dots (the
   generator's hand style differs: its medians are 4.48 dots for dashes and letter gaps and 7.39 dots for word gaps).
   Spread σ ≈ 0.3, the standard deviation of the natural logarithm, for marks and for gaps (two separate settings,
   both starting at 0.3: as forgiving as the generator's hand style; heuristic). One forgiving model to start; two
   fist classes ("precise", "hand") in the hypothesis only if the development set shows precise signals losing
   noticeably.
5. **Telling a speed change, fist scatter and Farnsworth spacing apart:** no special logic; the hypothesis
   probabilities do it (a speed change shifts all later lengths together; scatter is independent per element;
   Farnsworth stretches only letter and word gaps).

Caution: the development set comes from the same generator, so setting the fist from it and testing on it is partly
circular; recordings of real operators are the real check.

### Topic 3: amplitude (owner, 2026-10-12)

The amplitude A is the expected key-down level the hypotheses score against. One estimate per channel, independent of
every hypothesis and speed (owner's tap idea).

1. **The tap** is a fixed sum matched to the fastest speed: about 10 ms of the signal (5 of topic 4's 2 ms pieces),
   the same as the bank's shortest branch (9.6 ms). With nominal timing every element at every
   speed up to 100 WPM lasts at least 12 ms, so the tap reaches the full A on every such mark (derived). Marks
   shortened by fist scatter or key imbalance may not: a 10 ms mark peaks 0.8–1.0 dB below A² (signal power), and at
   topic 2's σ = 0.3, 27% of dots at 100 WPM are shorter than 10 ms (review 1, D4; corrected 2026-10-14). On the real
   channel filter the tap's noise bandwidth is about 91 Hz against the channel's 252 Hz, so its SNR is about 4.4 dB
   (power ratio) above that of one channel sample (derived; corrected 2026-10-14 from 4.6 dB). (Amended by topic 4:
   built from the pieces, not from the bank.)
2. **Estimate:** A² = the weighted average of the tap's power minus the noise power, each sample weighted by its
   probability of being key-down, judged on the tap itself (carrier plus noise against noise alone, at the current
   estimates). Heuristic: an EM-like soft-decision estimate. The prior probability of key-down that the weights use
   is not yet stated. Simulated bias at its fixed point: −0.9 to +0.7 dB in A² over 12–100 WPM and S₅₀₀ of 0–30 dB
   (review 1, D5). On strong signals gaps get weights near zero, so the estimate barely moves during gaps and
   silence. Near the decoding threshold it does not freeze: a noise-only tap gets a mean weight of 0.12 at
   S₅₀₀ = 0 dB and 0.25 at −3 dB, so 2–4 s of silence refill the 0.5 s window with noise (review 1, D6; corrected
   2026-10-14: this item had said the estimate freezes). Feeding back the decoder's own key-down probability is a
   later option (a loop).
3. **Speed of following:** an average over about the last 0.5 s of key-down time (gaps do not count); heuristic, to
   be tested on fading (QSB).
4. **Noise level:** measured on the pieces that the hypotheses agree are gaps, as a level per hertz (amended by
   topic 4, items 5 and 7; first proposed as the bank's per-branch noise estimate). Measuring noise on signal-free
   stretches is standard; choosing them by the decoder's own decisions is our method (decision-directed), and what
   "agree" means is not yet defined. The bank decoder keeps keying decisions out of its noise estimate, yet its
   level mask still reads the noise low by a factor 0.78–0.84 (`docs/signal-processing.md`, section 8c), so a
   selection bias is to be expected here too (review 1, S15; corrected 2026-10-14).
5. **Use:** the scoring of topic 4 compares a hypothesis's summed mark with a carrier of amplitude A plus noise.
6. **Start of an over:** A starts from the level the signal detector measured when it found the station, then refines
   from the first marks. The detector's level is an average over 1 s including key-up time, 3.6–7.1 dB below the
   key-down SNR per bin depending on speed (`docs/signal-processing.md`, section 5); the conversion is not yet
   stated (review 1, U8).

Errors: A too high makes weak marks look like gaps (lost elements); too low makes noise look like marks (false dots).

### Topic 4: the observation (owner, 2026-10-12)

1. **Every hypothesis is scored on the same evidence:** the channel signal cut into short non-overlapping pieces,
   each the sum of the complex samples over about 2 ms (3 samples at 1500 samples/s). Its cost is derived (review 1,
   D11; corrected 2026-10-14 from "heuristic"): placing edges only at piece boundaries costs 0.37 dB of SNR (power
   ratio) on average and 0.72 dB at worst on a 12 ms dot (100 WPM), and under 0.1 dB on average at 25 WPM; 1.33 ms
   pieces would save about 0.1 dB at 100 WPM for 1.5 times as many pieces. The pieces' noise is **not** independent:
   the channel passes about 300 Hz, so its noise stays correlated for about 3.3 ms (derived; about 4 ms on the real
   filter); see item 7. Reading speed-specific filters instead would score hypotheses at different speeds on
   different data, and overlapping samples would count evidence several times.
2. **Each hypothesis does its own matched filtering:** it adds up the pieces it claims fall inside its current mark
   (one complex running sum). A Morse carrier keeps its phase through a mark, so the sum is what a filter matched to
   that mark would give; its noise follows from the noise level per hertz (item 7). The score is the standard
   carrier-of-unknown-phase-plus-noise against noise-alone statistics, with A from topic 3; gaps are scored as noise.
   Edges fall where the hypothesis puts them (to one piece); there are no filter delays; speeds between grid values
   do not matter for the observation. Scoring a mark's sum and the neighboring gap pieces separately ignores the
   noise correlation across the mark's edges (about 4 ms): an approximation (review 1, U10).
3. **The time step** is the piece length, about 2 ms.
4. **Keying history (topic 1, item 7):** only the running sum of the current mark.
5. **The bank is not used by the new decoder** (owner): signal level and noise level come from the pieces too
   (topic 3, amended). The existing bank decoder stays frozen as the yardstick until the new decoder beats it.
6. **Standard parts:** the noncoherent matched filter (textbook communications theory; Proakis and Salehi, §4.5-1),
   segment scoring in hidden semi-Markov models (Rabiner 1989, §IV.D; Ostendorf, Digalakis and Kimball, 1996). Each
   hypothesis keeps its own running sum, the segment model's statistic; nothing is estimated per hypothesis
   (corrected 2026-10-14: per-survivor processing, Raheli, Polydoros and Tzou 1995, was listed here, but this design
   does not use it). The combination for Morse is ours; no published Morse decoder doing exactly this is known to us
   (not searched specifically).

7. **Noise per hertz, measured on gaps (owner, 2026-10-14; standard in principle, while choosing the gaps by the
   decoder's own decisions is our method: topic 3, item 4).** The noise of any sum over a time T is
   computed from the noise level per hertz (its variance proportional to T, for T well above about 3 ms), estimated on
   gaps as a density, not per piece. Treating the pieces as independent would understate a mark's noise variance by a
   factor of about 1.7–1.8 (2.2–2.6 dB) for marks of 8–720 ms on an ideal 300 Hz channel, and 1.9–2.1 (2.8–3.3 dB) on
   the real channel filter, whose noise bandwidth is 252 Hz (derived; review 1, D2), making the decoder
   overconfident, the mistake of Bell's receiver chain. The variances come from the real filter's noise autocovariance
   R[k]: a sum of n samples has variance Σ_{|k|<n} (n − |k|)·R[k] (derived, exact; "proportional to T" overstates the
   variance by 0.3–0.5 dB for sums of 8–12 ms, and by under 0.1 dB from 48 ms up). (Amended 2026-10-14: item 1 had
   claimed nearly independent noise, and item 2 a gain of 10·log₁₀ n dB over one piece; both were wrong by that
   factor.)

Risk: it relies on the carrier's phase staying steady through a mark. A residual frequency error after mixing to
0 Hz rotates the phase (0.5 Hz over a 720 ms dash at 5 WPM: about a third of a cycle, derived), losing part of the
gain at slow speeds. Corrected 2026-10-14: the channelizer has no frequency hold. It centers each channel once on the
nearest 23.4 Hz bin, leaving the carrier up to ±11.7 Hz off 0 Hz, and never moves (`docs/signal-processing.md`,
A.7). A whole-mark sum then loses most of its signal at slow speeds: even at ±2 Hz, 13 dB of signal power on a 5 WPM
dash and 19 dB on an 8 WPM dash, relative to a centered station (review 1, D7). Fading within a mark costs up to
about 7 dB of signal power on a 5 WPM dash at a 5 Hz Doppler spread (review 1, D8). The frequency reference, and
changes of phase and amplitude within a mark, are open (see "After review 1").

### Topic 5: hypotheses (owner, 2026-10-13)

A hypothesis is one continuous story of what has been sent, for the whole over, with its text kept through a link to
its parent. It does not end at letters or words.

**Search algorithm (owner, 2026-10-14, on the condition that it is standard practice): Viterbi beam search,** the
standard decoder of speech recognition (Rabiner 1989; in the "token passing" form of Young, Russell and Thornton,
1989). Each hypothesis carries a **score**: the probability of its single best story together with the signal so far.
Scores are not probabilities of the hypotheses: they do not sum to 1 and need not, since the decoder only asks which
story is best. To keep them in numerical range they are held relative to the best (divided by the best score at every
piece, as beam pruning does; Ney and Ortmanns 2000) or as logarithms (Rabiner 1989, eq. 105); either changes no
decision. Probabilities (per character, per speed) are **not** sums of the kept Viterbi scores: merging has already
discarded the merged stories' probability, so such a sum is only an approximation. The standard routes are the
forward (sum) recursion over the same states, or forward–backward over a lattice of the kept hypotheses (Rabiner
1989, §III.A; Wessel et al. 2001); one option is for each hypothesis to carry both its best score and a summed
total. The choice belongs to topic 8. (Corrected 2026-10-14: this paragraph had attributed the rescaling to Rabiner's
"scaling", which divides the forward variables by their sum, and had called the sum of kept scores true
probabilities.) This replaces the earlier wording, which called the scores probabilities.

1. **Branching:** every piece (2 ms), a hypothesis continues as it is, or ends its current mark or gap and branches
   into the possible successors (at a word gap also the speed and Farnsworth steps, up to 3 × 3 variants). A branch
   is created only where the fist model gives it a non-negligible probability (a pruning threshold; its value is not
   yet set).
2. **Merging:** two hypotheses in the same state (everything that affects the future: activity, key up or down and
   when the current mark or gap began, speed, Farnsworth factor, position in the Morse tree, the past text the text
   model needs, the clock of a non-Morse state, and the gap kind if it is resolved before the gap ends; the last two
   added 2026-10-14 for consistency with topic 1, review 1, D14) can never be told apart by future evidence, so the
   ratio of their probabilities is fixed forever (derived). The lower-scoring is dropped; the survivor keeps its own
   score and its own text: the Viterbi algorithm's merging step (standard). No correction is lost by this: every
   correction future evidence could make is between hypotheses in different states, which are all kept. How far back
   word context can correct is set by how much past text a hypothesis carries (topic 1's open item, topic 6): more
   text, more correction, fewer merges, more CPU.
3. **Births:** (a) from silence: while the no-signal hypothesis is alive, Morse at any of the 32 speeds, a steady
   carrier or nonsense keying may begin at any piece; (b) **always-on rescue births**: wherever a mark plausibly
   begins, fresh hypotheses "a new letter starts here" are seeded at all speeds (the trigger, "plausibly begins", is
   not yet defined). A birth is an ordinary transition, with a small disruption probability ε, from every
   hypothesis; under Viterbi the newborn takes the best of these, so its score is the best current score times ε.
   The Viterbi arithmetic is standard; the ε-restart model is our choice (corrected 2026-10-14: it was labeled
   standard). Derived constraint (review 1, D15): a newborn survives the beam (item 4) only if ε is larger than 1/B,
   where B is the beam factor; otherwise narrowing the beam silently disables births. Births let the decoder recover
   when every surviving hypothesis is wrong in a way its own branching cannot repair (a speed jump beyond one step per
   word, a noise burst that misplaces a letter boundary, a new station without silence between overs). Without them
   the decoder can stay lost, as Bell's did.
4. **Pruning: a beam plus a maximum count** (standard in speech recognition: beam search, Lowerre 1976). At each
   piece, every hypothesis whose score falls below the best by more than a set factor is dropped. **The factor is set
   by measurement against the owner's target:** pruning may change the decoded result at most once per 1 000 letters
   (0.1%; negligible at the CER 0.10 crossing, and a tenth to a fifth of the 0.5–1% floor measured on clean signals:
   the bank decoder's CER of 0.005 at high S₅₀₀ in speed cells 2–5,
   `docs/plans/2026-10-06-development-set-results.md`, and the Bell port's 0.55–0.82% at 20 WPM from 10 dB SNR in
   100 Hz up, `docs/plans/2026-10-07-bell-stage-1-results.md`). Method (standard: keeping "search errors"
   negligible): decode the development set once with a very wide beam as the reference, then narrow the beam until
   the result differs from the reference at most once per 1 000 letters. Amended 2026-10-14: the
   earlier rule (drop hypotheses while their combined probability stays under a budget per piece) treated the scores
   as probabilities summing to 1, which they are not.
5. **Ceiling on the number of hypotheses, set by CPU, not accuracy:** a fixed CPU budget per channel; how often the
   ceiling rather than the beam decides is counted (the maximum count is "histogram pruning", standard). Rough
   estimate: about 50 ns per hypothesis per piece, so a budget equal to the bank decoder's 42 ms per channel-second
   allows about 1 700 hypotheses; normally a few hundred at most are needed (estimates, to be measured).
6. **CPU as a requirement:** the target (owner's decision) is the spec's estimate for the Classical mode, 2–3 million
   floating-point operations per channel-second for typical signals (design spec §5.1, where it is an estimate, not
   yet measured); at the ceiling the estimate is 10–25 million, so the ceiling only serves hard moments. CPU per
   channel-second is measured on the first prototype, together with accuracy, before any refinement.
7. **Overload governor (owner, 2026-10-14): goal, as many stations as the CPU allows.** When the machine cannot keep
   up: (a) first decode every station with fewer hypotheses (all ceilings lowered); (b) only when the ceilings reach
   a floor below which decoding falls apart, drop the weakest stations.
8. **The ceiling, the floor and the cost are measured, not guessed (owner):**
   - **Cost per hypothesis per piece:** timed on the prototype on the Linux machine (replacing the 50 ns estimate).
   - **How many hypotheses a station needs:** the distribution of live hypotheses per piece over the development
     set, per group (clean, weak, fists, speed changes, Farnsworth).
   - **The ceiling and the floor:** the decoder is run on the development set at a range of ceilings; the CER
     against the ceiling gives the knee (the default ceiling: little accuracy gained above it) and the floor (the
     CER rises steeply below it).
   - **Cost per station-second** follows from the three, and is reported for typical and hard signals.

### Topic 6: the text model (owner, 2026-10-14)

The text model gives the probability of each completed letter given the text before it; it multiplies a hypothesis's
score each time the hypothesis completes a letter. The decoder serves both uses the owner wants: skimming (callsigns,
CQ, contest and DX exchanges) and transliterating conversations (ragchews).

1. **No callsign matching in the decoder (owner).** Callsign databases (`cty.dat`, `MASTER.SCP`) belong to the
   callsign-search step after the decoder (design spec §5, step 5; §6). Callsigns and other traffic are **never
   matched against the Reverse Beacon Network** (owner; spec §6 amended): once this skimmer feeds RBN, our own misread
   calls would confirm themselves.
2. **One model, not two modes (owner).** Each word is one of five kinds: **callsign** (written pattern rules: prefix,
   digit, suffix, optional `/P` and the like; no list of calls), **fixed token** (CQ, DE, TEST, K, KN, TU, 5NN, 73,
   the common abbreviations and Q-codes), **number** (serials, reports, zones), **plain language** (letter-sequence
   statistics of conversation English, abbreviations included), and a **catch-all** (any string of symbols, with a
   small probability, so that no reading is ever impossible). Standard components: class-based language models
   (Brown et al., 1992, who learn the classes by clustering; here the classes are defined by hand), interpolation
   with a uniform distribution (Jelinek and Mercer, 1980), a finite-state grammar for callsign patterns.
3. **The mix of kinds, without hand-written rules.** First version: one fixed mix for all channels, counted from
   independent text (the blend of sources that fixes it is not yet chosen). Only if tests show the fixed mix hurting
   one kind of traffic: a per-channel mix by counting (each kind's share of the channel's last N words); N is
   measured. This is our variant of a cache language model (Kuhn and De Mori, 1990, whose cache raises recently used
   words; here it adapts the prior over kinds; label corrected 2026-10-14).
4. **Strength.** The catch-all bounds how much an unusual word can be lowered: to no less than w times its probability
   without a text model. The bound is derived, under three conditions (review 1, D12): the mixture is over whole
   words, the catch-all includes the probability of the word ending, and the text-model weight is 1 (at weight λ the
   bound is w^λ). The value w = 5% (1/20) is heuristic; standard practice (Jelinek and Mercer, 1980) sets it on
   held-out text (corrected 2026-10-14: it was labeled derived). It does not bound how much a common word is raised.
   The overall weight of the text model (the standard "language model weight" of speech recognition) is set by
   measurement: errors on ambiguous signals must fall, and random callsigns decoded as something else must not rise.
   Speech recognizers tune that weight together with an insertion penalty (review 1, S20), which these notes do not
   yet include.
5. **Past text (closes topic 1's open item):** the current word so far, plus the kind of the previous word (and which
   token, for a fixed token). Merging (topic 5): hypotheses that differ within the current word stay apart until the
   word ends (except inside a callsign, where only the position in the pattern matters); corrections reach back
   through the current word and, through the kind, one word further.
6. **Start simple (owner).** The first prototype has no text model (the Morse tree only, all symbols equally likely),
   as the baseline. Kinds are added one at a time, each measured: callsign, token and number first, plain language
   second.
7. **Alphabet:** the engine's existing Morse table (letters, digits, 14 punctuation marks, 12 prosigns; corrected
   2026-10-14 from 15), shared with the generator and the scorer.
8. **Sources** (`docs/research/research_notes/CW conversation text sources/sources.md`). No public transcripts of real
   ragchews exist, and no measured statistics of real CW conversation. Callsign, token and number kinds: built and
   tested from public contest logs (CQ WW, CQ WPX, ARRL) and the standard abbreviation and Q-code lists; real copy
   errors from the contests' public log-checking reports. Plain language (proposed): built from human-written practice
   text, which is independent of our generator but not real traffic; the real check is a set of real recordings
   (IZ4KBW's archive, HamSCI's recordings) transcribed later by the owner, who copies to 50 WPM, by correcting a
   decoder's draft, with a way to mark uncopyable or garbled stretches. Whether the draft comes from another decoder,
   or part is copied blind, so that the test does not favor the decoder whose draft was corrected, is decided in
   topic 9. The development set's text comes from our generator, so text models are built and tested on independent
   text.
9. **Data and the repository (owner).** Contest logs are used for building and testing but never committed. IZ4KBW's
   recordings, if ever included, credit him. What else, if anything, from these sources is committed (raw data,
   derived tables) is decided when the need arises.

### After review 1 (2026-10-14)

An independent review of topics 1–6 ("review 1": `docs/research/2026-10-14-new-decoder-review-1.md`, with its
derivation scripts beside it; cited above by its item numbers, such as D7) checked every claim labeled standard, derived or sourced, classified every
number, and listed omissions. Its corrections of labels and facts are made in place above, each marked "corrected
2026-10-14". The owner's decisions on the rest (2026-10-14):

1. **A blind check of standard practice comes first (owner).** Review 1 checked whether our labels are true, not
   whether our choices are the standard ones. Before any topic reopens:
   - a reviewer who has not seen these notes, and who finds its own references, sets out the standard practice for
     each part of the problem;
   - a second reviewer sorts every choice here as standard, standard but adapted, ours where a standard exists, or
     ours where none exists, and lists the standard practices the design does not use.
2. **Topics reopened after that check, one at a time:**
   - topic 4: the frequency reference for whole-mark sums (D7), and changes of phase and amplitude within a mark
     (D8);
   - topic 2: the Farnsworth range (D9);
   - topic 3: whether the decoder's own decisions may feed the amplitude and noise estimates (K3);
   - topic 5: fair comparison under the beam between hypotheses in mid-element and finished ones (O4), births
     against the beam (D15), and the priors at birth and for the first prototype (K8, O7).
3. **A new topic before topic 7, the channel's other contents:** other signals, impulsive noise (static crashes), the
   models of the non-Morse states, and element sequences that fit no letter (O3, O6, O8). Its number is settled with
   the agenda after the check.
4. **Also raised by review 1, not yet discussed:** whether a hypothesis commits to one word kind or carries the
   mixture (K9, O12); an insertion penalty (O11); keyer weighting and imbalance (O13).
5. **Measurement plans** for every heuristic number (the "no darts" rule above) are written in topic 9, which also
   gets a test of how well confidence predicts correctness, for the Hybrid mode (K14).
6. **The design spec** (§5.1, and §5.2 step 3) still describes a Bell-style decoder: a Kalman amplitude per
   hypothesis, each hypothesis reading a filter matched to its speed, speed changes at element boundaries. It is
   amended, with the owner's approval, once this design settles (K12). Its ±2 Hz re-centering target (§5.2, step 1)
   is too loose for whole-mark sums (D7, K13).
