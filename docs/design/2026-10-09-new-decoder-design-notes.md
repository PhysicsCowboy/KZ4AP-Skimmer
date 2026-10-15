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
4. **Speed and the Farnsworth gap factor;** their form is topic 2. A fresh start takes its factor at its first gap
   between letters (topic 2, item 3; added 2026-10-15).
5. **The position in the Morse tree:** the letter in progress.
6. **Past text:** the current word so far, plus the kind of the previous word (and which token, if it was a fixed
   token). Closed by topic 6.
7. **The last stretch of keying,** if topic 4 finds that the expected filter output depends on keying before the
   current element.
8. **Its score and a link to its parent,** to trace its text back when printing (topic 8). The score is the
   probability of its single best story (topic 5, amended 2026-10-14).
9. **A clock for each non-Morse state:** how long the carrier or the silence has lasted.
10. **Its signal level and the level's variance,** learned from its own marks (topic 3, item 1; added 2026-10-15:
    the level differs between hypotheses because each learns it from its own decisions).

At channel level, outside the hypotheses: the hiss (noise) level (topic 3, item 3; amended 2026-10-15, the signal
level moved into the hypotheses); the fist model, fixed (topic 2, item 4; corrected 2026-10-14, this line had left it
open). If per-sender adaptation is ever built (topic 2,
item 6), each hypothesis carries its own adapted fist instead (added 2026-10-15).

### Topic 2: timing — speed, spacing and fists (owner, 2026-10-12; reopened and closed again 2026-10-15)

Guiding principle (owner): the simplest version of Bell's structure that works; a neural decoder may follow, so no
refinement without a measured need.

Reopened 2026-10-15 for review 1's D9 and O13 and for the standard-practice check's findings (step 2, rows T2.1 to
T2.4e). The conclusions below replace the earlier ones, which git keeps: a fixed fist with σ 0.3 from the generator
and a fallback of two fist classes; speed changes of one grid step at word boundaries; five Farnsworth factors
changing by one step at word boundaries.

**A real advantage (owner, 2026-10-15)** is required of every refinement this topic holds back from the baseline. It
means all three of:
- "better" than the baseline where the refinement should help, by the project's paired comparison: the bootstrap 95%
  interval of the per-signal CER difference lies below zero (`docs/signal-processing.md`, A.11; the interval rule is a
  heuristic convention);
- not "worse" by the same comparison anywhere else;
- a gain the owner judges worth its cost.

1. **Speed** is one of 32 values whose dot lengths are 10% apart, covering 5.2–100 WPM (dots of 12.0–230.3 ms; the
   bank's ladder, used only as a grid). A speed error of at most ±4.9% (the grid's worst) is small next to a hand
   sender's scatter and far inside the dot/dash (×3) and letter/word (×7/3) margins (derived). Speed only sets the
   expected lengths of elements and gaps (amended by topic 4: a hypothesis does not listen to a speed-specific
   filter).
   - **Standard but adapted (owner, 2026-10-15):** log-spaced tempo values are music's tempo-tracking practice, which
     picks their number by measured accuracy against cost (Krebs 2015: accuracy converged at 40–75 values over a 4:1
     range, 1.9–3.6% apart).
   - **Cost (derived):** a speed off by Δ in ln costs Δ²/(2σ²) nats per element, and r²/(24σ²) on average over a cell
     of width r in ln. At σ 0.3 that is 0.013 nats at worst and 0.004 on average, and a gap read from its length
     alone (letter or word, equal priors) is misread 8.0% of the time on average over the cell, against 7.9% at the
     exact speed. At σ 0.05 it is 0.45 and 0.15 nats. The baseline's σ values are broad (pooled over senders, item
     4), so 10% costs it little; narrow σ values come only with adaptation (item 6), which would also refine each
     sender's speed within a cell.
   - **Test (topic 9):** once the prototype runs, a 5% grid (61 values) is tried against it and adopted only with a
     real advantage.
2. **Speed changes: fresh starts only (owner, 2026-10-15).** A hypothesis keeps one speed. Every change, sudden or
   gradual, is picked up by a fresh start (topic 5, item 3: wherever a mark plausibly begins, new hypotheses start at
   every speed, behind the best by a factor ε). Speed is thus constant between change points, as in the change-point
   model of Adams and MacKay (2007): standard but adapted (the label of the ε-restart itself is topic 5's, reopened).
   This replaces one grid step at word boundaries, with a probability p that was never set, and its fallback of two
   steps; step 2 had classed that rule as ours where a standard exists (T2.2: music lets the tempo change at every
   beat, Krebs 2015; Bell, at every key transition).
   - **Why fresh starts suffice** (the owner's simplification): keyers and computers hold a fixed speed and change it
     suddenly, which is what fresh starts are for; the senders who drift are hand senders, whose loose timing makes a
     speed error of about 5% nearly harmless (item 1); and it drops p and the three-way speed branching at every word
     boundary.
   - **The cost:** a newborn must make up its penalty, so slow drift is followed late. A newborn at f times the old
     speed overtakes once the old speed's extra misfit, (ln f)²/(2σ²) nats per element, adds up to ln(1/ε): at
     σ 0.3, 0.38 nats per element for ×1.3 and 2.7 for ×2 (derived). Since ε now carries every speed change, setting
     it (topic 5) matters more.
   - **Test (topic 9):** small steps are added back only with a real advantage, judged on the development set's slow
     speed changes (D2's ramps) and on how much real senders drift (item 5's measurement).
   - The start of an over is not a speed change: hypotheses are born at every grid speed (topics 5 and 7).
3. **Farnsworth gap factor (owner, 2026-10-15).** One factor F stretches letter and word gaps alike, keeping their
   3:7 ratio: the ARRL rule (Bloom 1990), F = (60c/s − 37.2)/22.8 for characters at c WPM sent at s WPM overall. The
   form is standard; carrying F as a hidden variable of each hypothesis is the adaptation.
   - **Grid:** 23 factors 10% apart, 1.1^k for k = 0 to 22 (1 to 8.14), like the speeds. The range, 1 to 8, is the
     owner's decision: it covers the use known on the air (the ARRL's 18 WPM characters at 5 WPM overall give 7.84;
     W1AW's code practice has used 15 WPM characters since 2001, at most 6.26, from a weak source). Covering the whole
     development group (to 15.7, 30 values) would cost almost no CPU, since factors that do not fit are dropped at a
     hypothesis's first gap between letters (derived in outline); it was declined because factors above 8 are not
     needed. The earlier five factors (1, 1.68, 2.83, 4.76, 8.00) were too far apart: a sender midway between two is
     off by +29.7% or −22.9%, so at σ 0.3 with equal priors 29.3% of one kind of gap is misread (letter gaps as word
     gaps, or the reverse), against 7.9% at the exact factor and at most 10.5% on a 10% grid (derived).
   - **Change rule: as for speed (item 2).** A hypothesis keeps one factor; a change is picked up by a fresh start. A
     sender sets the factor for a whole transmission by choosing the two speeds. No steps at word boundaries
     (replacing one step per word boundary, which had neither a derivation nor a plan: step 2, T2.3c).
   - **A fresh start takes its factor at its first gap between letters,** branching into the factors that fit that
     gap. The factor stretches only gaps between letters and words, so newborns that differ only in factor have
     exactly the same score until then (derived): every surviving hypothesis gets the score it would have had if all
     23 factors had been started at birth, without multiplying births by 23 or filling the beam with identical copies.
     A computing shortcut; topic 5 settles its implementation (it may apply to speed as well).
   - **Test signals above a factor of 8 are out of range (owner, 2026-10-15).** About a fifth of the development
     set's Farnsworth group (I2, which reaches 15.7: review 1, D9) lies above 8 (estimated from its cells). In every
     comparison on I2, the frozen bank decoder's reference scores included, those signals are left out on both sides
     and listed separately; about four fifths of I2 remain. The committed score tables do not record a signal's
     overall speed: it is recovered from the seed by rerunning the generator's label step, and the new decoder's
     score tables will record it. The design spec names I2 in the comparison with the bank decoder, so this cut joins
     the spec amendment ("After review 1", item 6). The older Farnsworth group (I) reaches 7.84 and is unaffected.
4. **Fist model, baseline: fixed, at channel level, never learned while running.** It is the rule that scores every
   hypothesis: how probable an element or gap of a given length is at that hypothesis's speed (and, for gaps between
   letters, its Farnsworth factor), when a mark or gap ends, and its classification (dot or dash; element, letter or
   word gap).
   - **Form:** length = median × a random factor whose natural logarithm is Gaussian with standard deviation σ (the
     generator's form, from VE3NEA's DeepCW generator, consistent with Mills's (1977) scatter proportional to length;
     no source shows that it was measured on real keying), with a heavy tail so that one wild element does not kill
     the right hypothesis: a two-component (contaminated) likelihood, the standard device of robust statistics (Huber
     1964). The tail's form and weight, and the log-normal form itself, come from item 5's measurement.
   - **One median and one σ for each of the five kinds (owner, 2026-10-15):** dot, dash, element gap, letter gap and
     word gap, a table of 10 numbers in place of fixed 1:3:1:3:7 ratios and two σ's. Item 5's measurement gives them
     at no extra cost.
   - **Placeholders until then (heuristic):** medians 1, 3, 1, 3, 7 dots (the ITU sending standard, ITU-R M.1677-1,
     not a measurement of senders) and σ 0.3 for every kind (as forgiving as the generator's hand style). The
     generator's hand style itself differs (medians 4.48 dots for dashes and letter gaps, 7.39 for word gaps): its
     letter gaps sit almost on the placeholders' letter/word boundary (√21 = 4.58 dots at equal priors), so 47% of
     them look more like word gaps, and still 30% if letter gaps are a priori 3.7 times as likely (derived).
   - **Published evidence on σ is thin.** TSC's 55 hand-keyed samples (1976, reported by Bell 1977) were still
     misread at 1.35% with each sample's best threshold, which under this form means σ ≈ 0.25 if every operator had
     the same scatter (σ = ln √3 / 2.21; if operators differ and each has σ below 0.39, their average is below 0.25,
     the error rate being convex in σ there; derived). Mills's k ≈ 0.15 (Mills 1977) is a different quantity: his
     standard deviation is 0.15 times the length of the (mark, space) pair, shared by both members, and includes
     noise, fading and his receiver's slicer. Per element it gives coefficients of variation from about 0.15 for the
     longer member of a pair up to 0.47 for a dot before a letter gap and 1.06 before a word gap (derived), so step
     2's direct comparison of 0.15 with our σ (§3.3 and row T2.4d) overstates the match (corrected 2026-10-15).
   - **Sender style is adaptation's job.** In the baseline, one sender's weighting (marks longer and gaps shorter by
     the same amount), dash/dot and spacing ratios, and bug style (machine dots, hand dashes) stay scatter around the
     pooled values; learning them per sender is item 6's. Which mix of senders the pooled values represent (ragchew,
     contest) is part of item 5's plan. With item 6's genie test, this answers review 1's O13.
   - **Gaps conditioned on the mark before** (after a dot or after a dash: 6 gap kinds instead of 3) are standard but
     held back: MAUDE (1959) lengthened each space by a fraction of the mark before it, Mills kept spaces after dots
     and after dashes apart, and speech conditions units on their neighbors (Gales and Young 2008). They need no new
     state (a hypothesis in a gap knows which mark ended, from its place in the Morse tree), and the measurement fits
     the 6 kinds too. Our generator draws every length independently (`training/kz4ap_synth/keying.py`), so only real
     keying can show whether they matter. **Test (topic 9):** adopted only with a real advantage when decoding real
     fists.
5. **Where the fist's numbers come from: measured on real keying (owner, 2026-10-15).** This is the standard
   practice: speech trains its speaker-independent models, durations included, on many speakers' recordings and uses
   them as the prior for adaptation, fitting the prior's spread to the differences between speakers (parametric
   empirical Bayes; Morris 1983); TSC and Mills measured Morse keying.
   - **Data:** strong stretches of IZ4KBW's archive (thousands of QSOs from 2017 to 2026, as MP3s named by date,
     calls, key used, speed and frequency; no transcripts) and of HamSCI's 2017 eclipse recordings (CC BY 4.0)
     (`docs/research/research_notes/CW conversation text sources/sources.md`).
   - **Labeled by machine, with no transcription** ("lightly supervised training": untranscribed audio labeled
     automatically, keeping only what reads reliably; Lamel, Gauvain and Adda 2002). Marks and gaps are measured at
     half amplitude: 0.3 ms rms at S₅₀₀ = 20 dB and 0.1 ms at 30 dB (simulated, with 5 ms raised-cosine edges and a
     ±150 Hz filter; MP3 coding and receiver AGC are not included, and are checked by passing synthetic signals of
     known timing through the same chain). Dots and dashes, and element gaps and gaps between letters, are told apart
     by clustering each sender's own lengths (each pair is about 3 times apart or more). Word boundaries come from
     the text (words, abbreviations and callsign patterns from topic 6's sources), never from gap length, so letter
     and word gaps, the pair that overlaps for sloppy senders, are not cut at a timing threshold. Stretches that do
     not read as valid text are dropped.
   - **What it gives:** the baseline's pooled values (item 4), adaptation's prior (item 6), the tail's form and
     weight (by likelihood on held-out senders), and a check of the log-normal form. Amounts (derived): a two-minute
     over at 25 WPM has about 500 dots, 200 dashes, 450 element gaps, 200 letter gaps and 50 word gaps (counted on
     PARIS), which pins that sender's σ to between ±3% (dots) and ±10% (word gaps) (relative standard error
     1/√(2n)); a σ off by 10% costs about 0.01 nats per element (the Kullback–Leibler divergence, about δ² for a
     fractional error δ), small next to the 4–7 nats that separate element kinds at σ 0.3. The spread between
     senders needs about 50 senders for ±10% (1/√(2(m − 1))); a tail weight near 1% needs about 10 000 elements for
     ±10%, and fifty two-minute overs give about 70 000.
   - **Its weakness:** the dropped stretches may be the sloppiest, so σ and especially the tail could come out too
     small. Running the same labeler on synthetic signals of known timing (the generator's styles, tails included)
     measures that bias, which is then corrected or stated with the result. It cannot catch a real fist unlike any of
     the generator's styles.
   - **Timing:** it runs in parallel with the prototype, which starts on item 4's placeholders. It must be finished
     before adaptation is judged (item 6) and before the baseline's numbers become final. It needs nothing from the
     owner. The senders measured here are kept out of the real check (topic 6, item 8), as speech keeps test speakers
     out of training (TIMIT, for example). The full plan goes in topic 9.
   - **Also for topic 9:** the same data can check the generator's keying styles, and give a test group of real
     fists on synthetic channels at controlled S₅₀₀, made by keying the real timing onto them (as Aurora 2 added
     noise at set levels to clean recorded speech; Hirsch and Pearce 2000).
6. **Per-sender adaptation: only with a real advantage (owner, 2026-10-15).** The first prototype uses the fixed fist
   (item 4) as the baseline. Adaptation is the standard practice: MAP adaptation (Gauvain and Lee 1994); Mills's
   online estimates (1977); and Bell (1977), on TSC's finding that individual operators are "far from gaussian":
   "estimation of these highly individualistic densities must be accomplished on-line". Each hypothesis would adapt
   each kind's median and σ to its sender, pulled toward the prior measured in item 5, and reset at a change of
   station (topic 7). By topic 1's rule it lives in each hypothesis, since hypotheses that decoded different elements
   learn different fists; merging then becomes approximate (two hypotheses that differ only in what they have
   learned are merged, keeping the better one's estimate: the standard approximation of per-survivor processing).
   - **Why it may matter (derived):** a broad fixed fist throws away timing evidence on precise senders. For a
     perfectly timed element, the evidence from its length alone is (ln 3)²/(2σ²) for dot against dash, 6.7 nats at
     σ 0.3 and 241 at σ 0.05, and (ln 7/3)²/(2σ²) for letter against word gap, 4.0 and 144 nats.
   - **How fast it corrects (derived; the standard MAP update, with κ in the role of Gauvain and Lee's τ):** after n
     elements of one kind the prior keeps a share κ/(κ + n), with κ = (scatter within one sender ÷ spread between
     senders)², so a prior off by Δ leaves an error κΔ/(κ + n). Short overs are decoded mostly on the prior: a
     contest exchange such as "K1ABC 5NN 14" has 2 word gaps and 7 letter gaps, a two-minute ragchew over at 25 WPM
     about 50 and 200. Adaptation corrects medians and σ, but not the prior's strength, the tail or the form, so item
     5's measurement is needed with or without it.
   - **The gate.** A genie test first bounds the gain: the development set's keying-style group (C2: 5 styles,
     imbalance up to ±0.1 dot) is decoded again with each signal's true timing from the generator (speed, medians, σ,
     imbalance); the gap per style, in dB of S₅₀₀ at equal CER, is the most a better timing model can gain. If
     adaptation is then built, it is kept only with a real advantage. This replaces the fallback of two fist classes
     "only if the development set shows precise signals losing noticeably" (step 2, T2.4e: "noticeably" undefined).
   - **For its design, if built:** weighting is additive, so it matters most for short elements (±0.1 dot is ±10% of
     a dot, ±1.4% of a word gap). One weighting term shared by all kinds would learn it from every element, where five
     separate medians each learn only from their own kind, and word gaps are scarce; sharing one adjustment across
     many parameters is standard in speech (MLLR; Leggetter and Woodland 1995). Topic 7 may let a station's fist
     carry over between its overs, giving adaptation more to learn from.
7. **Telling a speed change, fist scatter and Farnsworth spacing apart:** no special logic; the hypothesis
   probabilities do it (a speed change shifts all later lengths together and is picked up by a fresh start; scatter
   is independent per element; Farnsworth stretches only letter and word gaps).

**For topic 9,** the measurements and tests this topic needs: item 5's measurement on real keying, with its
labeler-bias check, the check of the generator's styles and the real-fist test group; item 6's genie test on C2, and
adaptation's own test if it is built; item 4's gaps conditioned on the mark before, on real fists; item 1's 5% speed
grid; item 2's small speed steps (D2's ramps, real drift); item 3's cut of I2 above a factor of 8.

Caution: until item 5's measurement replaces the placeholders, the fist comes from the generator that also makes the
development set, so setting it there and testing on it is partly circular; recordings of real operators are the real
check.

### Topic 3: amplitude (owner, 2026-10-12; reopened and closed again 2026-10-15)

Reopened 2026-10-15 for review 1's K3 and for the standard-practice check's findings (step 2, rows T3.0 to T3.6). The
conclusions below replace the earlier ones, which git keeps: one amplitude per channel from a 10 ms tap ("owner's tap
idea"), soft key-down weights over the last 0.5 s of key-down time, and the noise measured on the gaps the hypotheses
agree on.

**Decision feedback (owner, 2026-10-15; answers review 1's K3).** The decoder's own decisions feed an estimate only
inside the hypothesis that made them; whatever is kept at channel level uses no decisions.

1. **The signal level: each hypothesis tracks its own (owner, 2026-10-15).** Standard: per-survivor processing
   (Raheli, Polydoros and Tzou 1995), as in Bell (1977), who ran a Kalman filter on the amplitude for each path. This
   replaces one level per channel (step 2, T3.0, class (iii)).
   - Each hypothesis carries its level estimate and the estimate's variance. Each measurement of the level (the sum
     over one of its marks, or over one block of a mark if topic 4 splits marks) moves the estimate toward the
     measurement by the Kalman gain, variance / (variance + the measurement's own noise variance). Between
     measurements the estimate is kept and its variance grows at the fading model's rate (item 2). Older marks
     therefore count less and less: the level is tracked over time, never fitted as one constant.
   - Each mark is scored against the predicted level allowing for its variance (the Kalman predictive density), not
     against a level taken as exact. Why (derived): taken as exact, a mark more than 6 dB below the stored level reads
     as noise on a strong signal (the crossover is at half the amplitude), so the level stops learning. In a
     simulation of the earlier channel estimate, a drop of 8 dB or more at 20 dB S₅₀₀ was never followed in 60 s, and
     the weaker station was not decoded at all; drops of 6 dB were followed within about 5 s. With the variance in the
     score, a 20 WPM dot at 10 dB S₅₀₀ arriving 10 dB below the level scores 1 107 nats better as noise when the level
     is taken as exact, but 271 nats better as a mark when the level is uncertain by ±10%, and that one dot moves the
     estimate 98% of the way to its own level.
   - A fresh start begins with its level uncertain and learns it from its first marks, so a fresh start picks up a
     new station's level: hypotheses stuck at the old level read the new station's marks as noise, which costs them
     roughly 30 to 300 nats per 20 WPM dot between 0 and 10 dB S₅₀₀ (derived). A fresh start's level prior is set
     with topic 5's priors at birth (K8, O7).
   - Cost: two numbers per hypothesis and an update per measurement. Merging becomes approximate: two hypotheses that
     differ only in their levels are merged, keeping the better one's level (the standard per-survivor approximation).
   - Dropped with the channel estimate: the 10 ms tap, its soft key-down weights (with their unstated key-down prior,
     0.44 by step 2, C8) and the 0.5 s window, and with them review 1's D4 to D6, which concerned them.
2. **How fast the level may change: moved to topic 4 (owner, 2026-10-15).** At slow speeds in fast fading the level
   changes within a mark: a 5 WPM dash lasts 720 ms, while the fading's correlation halves after 0.375/spread seconds
   (375 ms at 1 Hz, 75 ms at 5 Hz; step 1, N5). Marks must then be split into blocks shorter than the fading's
   coherence time (the standard: step 1, P1.5), so the level's rate of change and the block length are one assumption,
   set together in topic 4. Derived for that discussion, with one level measurement per mark:
   - a filter that assumes faster fading than the signal has costs sensitivity on steady signals. Per mark, at 1%
     errors, +0.4 dB of E/N₀ at ±20% level uncertainty and +2.7 dB at ±50%; at 0.1% errors, +2.1 and +12 dB. A 1 Hz
     assumption leaves ±49% at 5 WPM, ±19% at 20 WPM and ±10% at 40 WPM;
   - one that assumes slower fading than the signal has loses marks in deep fades whatever the SNR: a Rayleigh-faded
     signal spends 22% of its time more than 6 dB below its average;
   - the standard ways to cover an unknown spread are one fixed value (robust design for the fastest fading expected,
     Li, Cimini and Sollenberger 1998, whose small mismatch loss relies on frequent channel measurements; Bell 1977
     used one fixed model), an estimated spread (Tepedelenlioğlu, Abdi, Giannakis and Kaveh 2001), or several fading
     models inside each hypothesis (Blom and Bar-Shalom 1988). Our spreads range 100 to 1 (0.05–5 Hz in group B2).
3. **The hiss (noise) level: one per channel, measured without decisions (owner, 2026-10-15).** It is read from the
   signal detector's averaged spectrum around the station: its 23.4 Hz bins (Hann window), each averaged over about
   1 s, are inherited from the detector (`docs/signal-processing.md`, section 6). A low-ranked value of the bins
   within a range around the station gives the hiss per bin; its bias on hiss alone is computed exactly from the
   detector's averaging and divided out, and the result is converted to a level per hertz for topic 4's sums (topic 4,
   item 7).
   Standard but adapted: ordered-statistic estimation of the noise level (ordered-statistic CFAR, Rohling 1983;
   trimmed means, Gandhi and Kassam 1988) at a lower rank than radar's usual one, since an HF band can be more
   crowded than radar's quarter of occupied cells. This replaces the noise measured on the gaps the hypotheses agree
   on (step 2, T3.4; "agree" was never defined).
   - Not inside the channel: a strong signal's keying sidebands rise above the hiss right across the ±150 Hz channel,
     at any speed. The fourth-quietest of 13 slices of 23.4 Hz reads 1.9 dB high at 20 WPM and 4.1 dB high at
     100 WPM at 20 dB S₅₀₀, and 8.2 and 12.2 dB high at 30 dB (derived, 5 ms edges). A 100 WPM signal holds 99% of
     its power within 212 Hz with 5 ms edges and 308 Hz with 2 ms edges (derived; the ITU's necessary bandwidth for
     Morse, ITU-R SM.1138, gives 240–400 Hz), though the channel still passes 99% of it.
   - Not in time (minimum statistics, Martin 2001): a steady carrier longer than the window, such as a tune-up,
     reads as hiss.
   - Not from the decoder's gaps: a shared estimate fed by decisions feeds on its own mistakes (the rule above).
   - Estimators compared (derived; ±2 kHz, 170 values; bins taken as independent, and the detector's overlapping bins
     raise the scatter by about 40%; signals at 20 times the hiss, notches at a tenth). Scatter on hiss alone, bias
     with a quarter and with half of the values holding signals, bias with 3 values in a notch: the weakest value
     ±4.6%, +1.2%, +2.8%, −88%; the 10th percentile ±1.4%, +1.8%, +4.8%, −1.1%; the 25th percentile ±1.1%, +2.6%,
     +7.3%, −0.5%; the median ±1.0%, +4.4%, +28%, −0.2%; radar's 75th percentile ±1.1%, +17%, fails, −0.2%; the
     mean of the bottom quarter ±1.2%, +1.9%, +5.2%, −6.7%; the mean of all values ±0.8%, +470%, fails, −1.6%. The
     scatter of the 25th percentile is ±2.2%, ±1.6%, ±1.1% and ±0.7% over ±0.5, ±1, ±2 and ±5 kHz.
   - **Set by measurement (topic 9):** on HamSCI's whole-band contest recordings, the share of values holding signals
     within ±0.5 to ±5 kHz of CW stations, how flat the hiss is over those distances, and how often notches occur;
     then the range and the estimator with the smallest total error at the most crowded measured conditions.
     Placeholders until then: ±2 kHz and the 25th percentile (heuristic). Nothing is needed from the owner.
   - An interferer inside the station's own channel is not hiss and is rightly ignored here; how the decoder copes
     with it belongs to the new topic on the channel's other contents.
4. **Use:** topic 4's scoring compares a hypothesis's summed mark (or block) with a carrier of the hypothesis's
   predicted level plus the hiss.
5. **Start of an over:** the detector's level converts to a key-down level (derived, not yet written out; review 1,
   U8; step 2, T3.6): the track's 1 s average includes key-up time, so the key-down power is about (average − hiss)
   divided by the key-down fraction (0.44 for PARIS text), corrected for the share of the keying sidebands that falls
   in the bin (the track reads 3.6–7.1 dB below the key-down SNR per bin, depending on speed:
   `docs/signal-processing.md`, section 5). It centers the level prior of a hypothesis born from silence; the prior's
   width is topic 5's (K8, O7).

Errors: a level too high makes weak marks look like gaps (lost elements), too low makes noise look like marks (false
dots); a hiss level too low makes noise look like marks and the decoder overconfident, too high makes weak marks look
like noise.

**For topic 9:** item 3's measurement of the hiss estimator on HamSCI's recordings.

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
5. **The bank is not used by the new decoder** (owner): each hypothesis's signal level comes from its own sums of
   the pieces, and the hiss level from the signal detector's spectrum around the station (topic 3, items 1 and 3,
   amended 2026-10-15). The existing bank decoder stays frozen as the yardstick until the new decoder beats it.
6. **Standard parts:** the noncoherent matched filter (textbook communications theory; Proakis and Salehi, §4.5-1),
   segment scoring in hidden semi-Markov models (Rabiner 1989, §IV.D; Ostendorf, Digalakis and Kimball, 1996). Each
   hypothesis keeps its own running sum, the segment model's statistic, and since 2026-10-15 its own signal level
   (per-survivor processing, Raheli, Polydoros and Tzou 1995: topic 3, item 1; the correction of 2026-10-14, which
   removed that citation because nothing was then estimated per hypothesis, no longer applies). The combination for
   Morse is ours; no published Morse decoder doing exactly this is known to us (not searched specifically).

7. **Noise per hertz (owner, 2026-10-14; amended 2026-10-15 by topic 3, item 3: the level per hertz now comes from
   the signal detector's spectrum around the station, measured without decisions, no longer from the gaps the
   hypotheses agree on).** The noise of any sum over a time T is computed from the noise level per hertz (its variance
   proportional to T, for T well above about 3 ms), as a density, not per piece. Treating the pieces as independent
   would understate a mark's noise variance by a factor of about 1.7–1.8 (2.2–2.6 dB) for marks of 8–720 ms on an
   ideal 300 Hz channel, and 1.9–2.1 (2.8–3.3 dB) on the real channel filter, whose noise bandwidth is 252 Hz
   (derived; review 1, D2), making the decoder overconfident. Bell's receiver chain made that assumption, though Bell himself warned that his sampling interval
   "may render the independent noise assumption invalid" (Bell 1977, as read in the standard-practice check, step 2,
   §6.1; wording corrected 2026-10-14 from "the mistake of Bell's receiver chain"). The variances come from the real filter's noise autocovariance
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
piece: a numerical convenience; beam pruning compares each score with the best, Ney and Ortmanns 2000) or as
logarithms (Rabiner 1989, eq. 105); either changes no decision. Probabilities (per character, per speed) are **not** sums of the kept Viterbi scores: merging has already
discarded the merged stories' probability, so such a sum is only an approximation. The standard routes are the
forward (sum) recursion over the same states, or forward–backward over a lattice of the kept hypotheses (Rabiner
1989, §III.A; Wessel et al. 2001); one option is for each hypothesis to carry both its best score and a summed
total. The choice belongs to topic 8. (Corrected 2026-10-14: this paragraph had attributed the rescaling to Rabiner's
"scaling", which divides the forward variables by their sum, and had called the sum of kept scores true
probabilities.) This replaces the earlier wording, which called the scores probabilities.

1. **Branching:** every piece (2 ms), a hypothesis continues as it is, or ends its current mark or gap and branches
   into the possible successors; a hypothesis with no Farnsworth factor yet also branches, at its first gap between
   letters, into the factors that fit it (amended 2026-10-15 by topic 2, items 2 and 3: the speed and Farnsworth
   steps at word gaps, up to 3 × 3 variants, are gone). A branch is created only where the fist model gives it a
   non-negligible probability (a pruning threshold; its value is not yet set).
2. **Merging:** two hypotheses in the same state (everything that affects the future: activity, key up or down and
   when the current mark or gap began, speed, Farnsworth factor, position in the Morse tree, the past text the text
   model needs, the clock of a non-Morse state, and the gap kind if it is resolved before the gap ends; the last two
   added 2026-10-14 for consistency with topic 1, review 1, D14) can never be told apart by future evidence, so the
   ratio of their probabilities is fixed forever (derived). The lower-scoring is dropped; the survivor keeps its own
   score and its own text: the Viterbi algorithm's merging step (standard). No correction is lost by this: every
   correction future evidence could make is between hypotheses in different states, which are all kept. How far back
   word context can correct is set by how much past text a hypothesis carries (topic 1's open item, topic 6): more
   text, more correction, fewer merges, more CPU. Amended 2026-10-15 by topic 3, item 1: each hypothesis also carries
   a continuous signal level, which also affects the future; hypotheses that differ only in it are merged anyway,
   keeping the better one's level (the standard per-survivor approximation), so merging is no longer exact.
3. **Births:** (a) from silence: while the no-signal hypothesis is alive, Morse at any of the 32 speeds, a steady
   carrier or nonsense keying may begin at any piece; (b) **always-on rescue births**: wherever a mark plausibly
   begins, fresh hypotheses "a new letter starts here" are seeded at all speeds (the trigger, "plausibly begins", is
   not yet defined; each newborn takes its Farnsworth factor at its first gap between letters, topic 2, item 3). A
   birth is an ordinary transition, with a small disruption probability ε, from every hypothesis; under Viterbi the
   newborn takes the best of these, so its score is the best current score times ε. The Viterbi arithmetic is
   standard; the ε-restart model is our choice (corrected 2026-10-14: it was labeled standard). Derived constraint
   (review 1, D15): a newborn survives the beam (item 4) only if ε is larger than 1/B, where B is the beam factor;
   otherwise narrowing the beam silently disables births. Births let the decoder recover when every surviving
   hypothesis is wrong in a way its own branching cannot repair (any change of speed or Farnsworth factor, which since
   2026-10-15 only births carry, topic 2, items 2 and 3; a noise burst that misplaces a letter boundary; a new station
   without silence between overs). Without them the decoder can stay lost, as Bell's did.
4. **Pruning: a beam plus a maximum count** (standard in speech recognition: beam search, Lowerre 1976; the maximum
   count, "histogram pruning", Steinbiss, Tran and Ney 1994; citation completed 2026-10-14). At each
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
   ceiling rather than the beam decides is counted (the maximum count is "histogram pruning", standard: Steinbiss,
   Tran and Ney 1994). Rough
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
   callsign-search step after the decoder (design spec §4.1, item 5; §6; reference corrected 2026-10-14). Callsigns and other traffic are **never
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
   errors from the contests' public log-checking reports (public only for top-scoring stations, as CQ WW publishes
   them; added 2026-10-14). Plain language (proposed): built from human-written practice
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

   Done 2026-10-14: `docs/research/2026-10-14-standard-practice-check/` (step 1, blind:
   `step-1-standard-practice.md`; step 2, the comparison of these notes at b51eefe: `step-2-comparison.md`). Its
   findings are taken up topic by topic as the topics reopen.
2. **Topics reopened after that check, one at a time:**
   - topic 4: the frequency reference for whole-mark sums (D7), and changes of phase and amplitude within a mark
     (D8), with how fast the signal level may change (moved from topic 3, item 2, 2026-10-15);
   - topic 2: the Farnsworth range (D9), with step 2's findings on timing: done 2026-10-15 (topic 2 above);
   - topic 3: whether the decoder's own decisions may feed the amplitude and noise estimates (K3), with step 2's
     findings on amplitude: done 2026-10-15 (topic 3 above);
   - topic 5: fair comparison under the beam between hypotheses in mid-element and finished ones (O4), births
     against the beam (D15), and the priors at birth and for the first prototype (K8, O7).
3. **A new topic before topic 7, the channel's other contents:** other signals, impulsive noise (static crashes), the
   models of the non-Morse states, and element sequences that fit no letter (O3, O6, O8). Its number is settled with
   the agenda after the check.
4. **Also raised by review 1, not yet discussed:** whether a hypothesis commits to one word kind or carries the
   mixture (K9, O12); an insertion penalty (O11); keyer weighting and imbalance (O13; answered 2026-10-15 by
   topic 2, items 4 and 6).
5. **Measurement plans** for every heuristic number (the "no darts" rule above) are written in topic 9, which also
   gets a test of how well confidence predicts correctness, for the Hybrid mode (K14). Topic 2's are listed at the
   end of topic 2, topic 3's at the end of topic 3.
6. **The design spec** (§5.1, and §5.2 step 3) still describes a Bell-style decoder: a Kalman amplitude per
   hypothesis (which topic 3 has chosen again, 2026-10-15), each hypothesis reading a filter matched to its speed,
   speed changes at element boundaries. It is
   amended, with the owner's approval, once this design settles (K12), together with topic 2's cut of the Farnsworth
   test signals above a factor of 8 from the comparison with the bank decoder (topic 2, item 3). Its ±2 Hz
   re-centering target (§5.2, step 1) is too loose for whole-mark sums (D7, K13).
7. **Building starts after topics 3, 4 and 5 (owner, 2026-10-15).** Then come a short spec amendment and the first
   prototype's plan, each for the owner's approval. Topic 6's reopened items, the new topic on the channel's other
   contents, and topics 7, 8 and 9 follow while the prototype is built and measured, each settled before its part is
   added. The reason: the first prototype needs only simple versions of those parts (it has no text model, topic 6,
   item 6; it can print its best story at the end of each recording; fresh starts already follow a change of
   station), while it cannot do without amplitude, the observation and the search. The risk accepted is rework if a
   later topic changes something already built.
