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
   the next marks decide. After a letter, the gap is one between letters with probability 1 − p, one between words
   with p(1 − q), or the over's end with p·q, where p is the probability that a letter ends its word and q that a
   word ends its over (topic 5, item 3; added 2026-10-19).
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
   filter). A newborn takes each of the 32 with equal probability (a flat prior: topic 5, item 3; owner, 2026-10-19).
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
   gradual, is picked up by a fresh start (topic 5, item 3, amended 2026-10-18: wherever a hypothesis starts a mark,
   a newborn starts with it, behind it by a factor ε, and takes its speed when its first mark ends). Speed is thus
   constant between change points, as in the change-point model of Adams and MacKay (2007): standard but adapted, as
   is the ε-restart itself (topic 5, item 3; relabeled 2026-10-18).
   This replaces one grid step at word boundaries, with a probability p that was never set, and its fallback of two
   steps; step 2 had classed that rule as ours where a standard exists (T2.2: music lets the tempo change at every
   beat, Krebs 2015; Bell, at every key transition).
   - **Why fresh starts suffice** (the owner's simplification): keyers and computers hold a fixed speed and change it
     suddenly, which is what fresh starts are for; the senders who drift are hand senders, whose loose timing makes a
     speed error of about 5% nearly harmless (item 1); and it drops p and the three-way speed branching at every word
     boundary.
   - **The cost:** a newborn must make up its penalty, so slow drift is followed late. A newborn at f times the old
     speed overtakes once the old speed's extra misfit, (ln f)²/(2σ²) nats per element, adds up to ln(1/ε) plus its
     speed and factor priors: at σ 0.3, 0.38 nats per element for ×1.3 and 2.7 for ×2 (derived), so about 35 and 5
     elements at the first prototype's 13.5 nats (topic 5, item 3; amended 2026-10-19). Since ε now carries every
     speed change, setting it (topic 5) matters more.
   - **Test (topic 9):** small steps are added back only with a real advantage, judged on the development set's slow
     speed changes (D2's ramps) and on how much real senders drift (item 5's measurement).
   - The start of an over is not a speed change: hypotheses are born at every grid speed (topics 5 and 7; in
     practice one newborn, which takes its speed when its first mark ends: topic 5, item 3, 2026-10-18).
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
     gap, each with probability 1/23 (a flat prior: topic 5, item 3; owner, 2026-10-19). The factor stretches only
     gaps between letters and words, so newborns that differ only in factor have exactly the same score until then
     (derived): every surviving hypothesis gets the score it would have had if all 23 factors had been started at
     birth, without multiplying births by 23 or filling the beam with identical copies. A computing shortcut (topic
     5, item 1). The same shortcut serves speed: a newborn takes its speed when its first mark ends (topic 5, item 3;
     owner, 2026-10-18).
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
     weight (by likelihood on held-out senders), a check of the log-normal form, and, with topic 7, the rates of
     overs and restarts and the probabilities that a letter ends its word and a word its over (topic 5, item 3;
     added 2026-10-19). Amounts (derived): a two-minute
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
2. **How fast the level may change: moved to topic 4 (owner, 2026-10-15), and settled there: one fixed spread,
   chosen by measurement, sets both the block length and the level's rate (topic 4, item 9).** At slow speeds in fast
   fading the level changes within a mark: a 5 WPM dash lasts 720 ms, while the fading's correlation halves after
   0.375/spread seconds (375 ms at 1 Hz, 75 ms at 5 Hz; step 1, N5). Marks must then be split into blocks shorter
   than the fading's coherence time (the standard: step 1, P1.5), so the level's rate of change and the block length
   are one assumption, set together in topic 4. Derived for that discussion, with one level measurement per mark:
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
   width is topic 5's (K8, O7). In a long pause the detector's level falls toward the hiss, and the prior's center
   with it; what newborns should look for then is settled with topic 5's priors at birth (topic 5, item 3; added
   2026-10-18).

Errors: a level too high makes weak marks look like gaps (lost elements), too low makes noise look like marks (false
dots); a hiss level too low makes noise look like marks and the decoder overconfident, too high makes weak marks look
like noise.

**For topic 9:** item 3's measurement of the hiss estimator on HamSCI's recordings.

### Topic 4: the observation (owner, 2026-10-12; reopened 2026-10-15, closed again 2026-10-16)

Reopened 2026-10-15 for review 1's D7 and D8 (with O1, O2 and K13), for the standard-practice check's findings
(step 2, rows T4.1 to T4.7, of which T4.2b, whole-mark sums with no frequency reference, was "ours where a standard
exists"), and for how fast the signal level may change (moved here from topic 3, item 2). Items 1 to 7 stand,
amended where marked. Items 8 (the frequency reference) and 9 (blocks, and how fast the level may change) are new;
they answer the risk this topic used to end with, which git keeps.

1. **Every hypothesis is scored on the same evidence:** the channel signal, re-centered by item 8's fine frequency
   shift (added 2026-10-16), cut into short non-overlapping pieces, each the sum of the complex samples over about
   2 ms (3 samples at 1500 samples/s). Its cost is derived (review 1, D11; corrected 2026-10-14 from "heuristic"):
   placing edges only at piece boundaries costs 0.37 dB of SNR (power ratio) on average and 0.72 dB at worst on a
   12 ms dot (100 WPM), and under 0.1 dB on average at 25 WPM; 1.33 ms pieces would save about 0.1 dB at 100 WPM for
   1.5 times as many pieces. (Checked by the standard-practice check, step 2, T4.3, added 2026-10-16: in a
   white-noise model with unfiltered marks, 0.37 dB on average and 0.67 dB at worst; on the real channel filter with
   its correlated noise, 0.30 dB on average and 0.41 dB at worst, against the ideal E/N₀. The figures above are
   conservative.) The pieces' noise is **not** independent: the channel passes about 300 Hz, so its noise stays
   correlated for about 3.3 ms (derived; about 4 ms on the real filter); see item 7. Reading speed-specific filters
   instead would score hypotheses at different speeds on different data, and overlapping samples would count
   evidence several times.
2. **Each hypothesis does its own matched filtering:** it adds up the pieces it claims fall inside its current mark
   (one complex running sum). A Morse carrier keeps its phase through a mark, so the sum is what a filter matched to
   that mark would give; its noise follows from the noise level per hertz (item 7). The score is the standard
   carrier-of-unknown-phase-plus-noise against noise-alone statistics, with A from topic 3; gaps are scored as noise.
   Edges fall where the hypothesis puts them (to one piece); there are no filter delays; speeds between grid values
   do not matter for the observation. Scoring a mark's sum and the neighboring gap pieces separately ignores the
   noise correlation across the mark's edges (about 4 ms): an approximation (review 1, U10). Amended 2026-10-16 by
   item 9: the running sum covers the current block of the mark, at most item 9's block length; each finished block
   is scored this way, against the hypothesis's predicted level (topic 3, item 1), and the scores of a mark's blocks
   add (noncoherent combining). A mark no longer than one block is summed whole, as before. The current block's
   score so far counts at every piece, not only when the block finishes (topic 5, item 9; added 2026-10-16).
3. **The time step** is the piece length, about 2 ms.
4. **Keying history (topic 1, item 7):** only the running sum of the current mark (amended 2026-10-16 by item 9:
   of the current block. The block's score so far is part of the hypothesis's score at every piece, and a finished
   block's sum updates the hypothesis's level. Corrected the same day: this amendment first said a finished block's
   score "goes straight into the hypothesis's score", which booked the evidence late and made the beam's comparisons
   unfair; topic 5, item 9).
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

8. **The frequency reference (owner, 2026-10-15; answers review 1, D7, O1 and K13, and step 2, T4.2b).** One
   tracker per channel follows the carrier's frequency and its drift rate. It uses none of the decoder's decisions
   (topic 3's rule), starts from the signal detector's frequency, and steers the channel's fine frequency shift, so
   every hypothesis reads the same re-centered pieces (item 1).
   - **Why (derived; review 1, D7).** A carrier Δf off 0 Hz turns its phase through a full cycle every 1/Δf seconds,
     and a coherent sum over a time T keeps |sinc(Δf·T)|² of its signal power. The channelizer centers each channel
     once on the nearest bin of its shared FFT (192 kHz / 8 192, so 23.4 Hz apart) and never moves it
     (`docs/signal-processing.md`, A.7), leaving the carrier up to ±11.7 Hz off 0 Hz. A whole 20 WPM dash (180 ms)
     then loses 26 dB of signal power, and for a 5 WPM dash (720 ms) to lose under 0.3 dB the carrier must be within
     0.2 Hz. Shifting by whole bins is the usual coarse step of FFT channelizers (free, and shared by every channel);
     the fine shift runs on the channel's 1500 samples/s at one complex multiplication per sample.
   - **Standard (step 1, P3.4; step 2, §3.6):** acquire the frequency at the spectrum's peak, the maximum-likelihood
     estimate (Rife and Boorstyn 1974); then track it with its drift rate, by a Kalman filter or phase-locked loop at
     moderate SNR, or by a hidden Markov model over frequency cells at low SNR (Streit and Barrett 1990). Review 1
     suggested this layout: one tracker per channel, ahead of the pieces, the same for every hypothesis (O1).
   - **Without decisions (derived).** On/off keying puts a share of the signal's average power equal to the key-down
     fraction (0.44 for PARIS text) in a steady line at the carrier frequency, which a tracker can follow without
     knowing when the key is down. Over 1 s at 0 dB S₅₀₀ that line alone pins the frequency to about 0.04 Hz at best
     (the Cramér–Rao bound), while one mark says little: a 20 WPM dot pins it to about 1.2 Hz at best, a dash to
     0.23 Hz.
   - **Not chosen.** Each hypothesis tracking its own frequency, as it does its level (per-survivor processing),
     would help only at a QSO turnover, when the second station transmits a few to a few tens of hertz from the
     first. The channel's tracker follows the carrier line, which moves as soon as that station transmits, so it
     should relock quickly on any signal strong enough to decode (expected; measured below). Matched's tracker is
     standard, but it is steered by Matched's own key-down decisions at channel level, which topic 3's rule forbids,
     and it lags a 1 Hz/s drift by 1.1–1.6 Hz (review 1, D7).
   - **Accuracy needed:** item 9 caps every coherent sum at its block length T, and a residual error Δf loses under
     1 dB of signal power while Δf·T ≤ 0.26; over a 100 ms block, 1 Hz costs 0.14 dB and 2 Hz 0.6 dB (derived). This
     replaces the spec's ±2 Hz re-centering target in the spec amendment (K13).
   - **Built and measured with the prototype:** the tracker's model and gains are set when it is built, from the
     standard forms above. Its frequency error is measured on weak keyed stations, on the drift group (F2, excursions
     up to 40 Hz) and at QSO turnovers, on the development set's detector path (topic 9). Per-hypothesis frequencies
     are reconsidered only if the turnovers show a real loss.
9. **Blocks, and how fast the level may change (owner, 2026-10-15; answers review 1, D8 and O2, and step 2, T4.2b;
   settles topic 3, item 2).** A hypothesis sums its mark coherently over blocks no longer than a fixed block length,
   combines the blocks of a mark noncoherently (item 2), and measures its level once per block (topic 3, item 1). One
   fixed Doppler spread, the same for every signal, sets both the block length and how fast the level may change
   (the rate of the level's Gauss–Markov model); its value is set by measurement.
   - **Why (derived; review 1, D8).** Fading turns the carrier's phase and changes its level within a mark. For a
     Rayleigh-fading carrier whose Doppler spectrum is Gaussian with standard deviation f_D/2 (f_D, the spread), a
     coherent sum over T keeps (2/T²)∫₀^T (T − τ)·ρ(τ) dτ of its energy, ρ being the fading's correlation: a 5 WPM
     dash (720 ms) loses 1.4 dB at a 1 Hz spread and 6.9 dB at 5 Hz, a 20 WPM dash 2.0 dB at 5 Hz. The faded sum also
     stops looking like "a carrier of level A", so its score is wrong too. The development set's fading group B2
     spans 0.05–5 Hz.
   - **Standard (step 1, P1.5; step 2, §3.6; review 1, O2):** sum coherently only over blocks shorter than the
     fading's coherence time and short against 1/Δf (item 8), and combine the blocks noncoherently or differentially
     (Dovis and Ta 2012); track the level block by block with a Gauss–Markov model whose rate comes from the spread
     (Bell 1977 ran a Kalman filter on the amplitude). A refinement is the estimator-correlator (Kailath 1969), in
     effect a Kalman predictor of the carrier that picks its own block length from the fading model.
   - **The trade-off (derived, one mark judged on its own).** Short blocks cost a steady signal: the extra E/N₀ for
     1% errors per mark (missed marks and noise read as marks, averaged), against one sum over the whole mark, is
     0.5, 1.0, 1.5, 2.4 and 3.4 dB for 2, 3, 5, 10 and 20 blocks. Long blocks cost a fading signal: the longest block
     that loses at most 0.5 dB of energy is 78 ms at a 5 Hz spread, 195 ms at 2 Hz, 390 ms at 1 Hz and 1.3 s at
     0.3 Hz. Example, a 5 WPM dash: blocks short enough for 5 Hz fading cut it into about 9, which costs a steady
     signal about 2.4 dB, while left whole it loses 6.9 dB at 5 Hz. So any one value gives up about 2 dB on steady
     slow signals or up to about 7 dB on fast-fading slow ones. It matters mostly below about 20 WPM: marks of 78 ms
     or less are never split (dots from 16 WPM up, dashes from 46 WPM up). The block length is in milliseconds, not
     dits, because the fading's coherence time does not scale with the sending speed. What a level rate that is too
     fast or too slow costs is in topic 3, item 2.
   - **The value, by measurement (topic 9):** the spread that does best on B2 without making A2 (steady signals)
     worse, by the paired comparison (topic 2, "a real advantage"). The block length is the longest that loses at
     most 0.5 dB of energy at that spread (heuristic; the same sweep tries 0.25 and 1 dB). A genie test on B2,
     decoding with the generator's true fading, measures what one fixed value loses.
   - **Only with a real advantage (topic 2):** a spread estimated per signal (Tepedelenlioğlu, Abdi, Giannakis and
     Kaveh 2001), or two or three spreads side by side in each hypothesis, weighted by how well each fits (multiple
     models, Blom and Bar-Shalom 1988; cheaper than it sounds, since long blocks are sums of short ones).
     Differential combining and the estimator-correlator are refinements, tested the same way.
   - **The block cap also** bounds the loss from any frequency error item 8's tracker leaves (review 1, O1).
     Transmitter chirp, a frequency change at key-down (O2), is not modeled, and the development set has none.

**For topic 9:** item 8's measurement of the tracker, and item 9's sweep of the spread with its genie test.

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
   carrier or nonsense keying may begin at any piece; (b) **always-on rescue births, where a mark starts (owner,
   2026-10-18; answers review 1, U7, and step 2, T5.3b)**: at every piece where at least one surviving hypothesis
   ends a gap and starts a mark, one newborn "a new letter starts here" begins with that mark. Births let the decoder
   recover when every surviving hypothesis is wrong in a way its own branching cannot repair (any change of speed or
   Farnsworth factor, which since 2026-10-15 only births carry, topic 2, items 2 and 3; a noise burst that misplaces
   a letter boundary; a new station without silence between overs). Without them the decoder can stay lost, as
   Bell's did.
   - **The model:** at each mark that follows a gap between letters or words, the sender begins a new story with
     probability ε (a new speed and Farnsworth factor; the text goes on) and goes on with the old one with
     probability 1 − ε, so ε is charged once per letter start (wording corrected 2026-10-19 from "per mark start":
     marks inside a letter are not charged). A newborn's possible parents are the hypotheses that start a
     mark at that piece, each with the gap before it read as a gap between letters or words: the letter before it
     is complete, as at any letter's end, and the gap is timed by the parent's own speed and factor (derived: the
     change comes with the mark). Under Viterbi the newborn takes the best parent: its score is that parent's score
     times ε, and it keeps that parent's text. It starts behind the best by ln(1/ε), by the priors it pays later,
     and by its parent's own deficit: none where the best hypothesis starts the mark, about g nats per piece where
     the mark's start is misplaced (g, the signal's energy-to-noise ratio per piece: 1 at S₅₀₀ 0 dB).
   - **Standard but adapted** (relabeled 2026-10-18 after step 2, section 7, item 2; labeled our choice on
     2026-10-14, standard before that). A restart with a constant probability is the change-point model of Adams
     and MacKay (2007): its restart, with the sum replaced by the maximum as Viterbi does, is this one (so is the
     "fixed share" of Herbster and Warmuth 1998). Allowing it only at element boundaries is the standard for changes
     of speed or tempo: Bell (1977), "the speed is not allowed to change except when the keystate changes"; Krebs,
     Böck and Widmer (2015), "allow transitions to each tempo, but only at beat times", which gave "higher stability"
     and "far fewer states and transitions" (step 1, P2.5).
   - **Review 1's suggestion** (U7 and correction 23) was to treat the trigger as a pruning threshold, checked by a
     wide-beam reference that seeds newborns everywhere. Here the rule is part of the model, so the reference (item
     4) uses it too. What it can miss is a mark start that no surviving hypothesis places right; the newborn then
     starts a piece or more off, which item 4's counts and margin check show.
   - **Rejected:** a newborn at every piece (the change-point model's own form): 500 a second; inside a mark it
     claims that the mark was cut in two with no gap between, which Morse cannot do, and lives on the mark's evidence
     until the mark ends; and ε charged per piece starts every newborn 5.0 to 6.2 nats further behind (the logarithm
     of the number of pieces per letter start, at 40 to 12 WPM; corrected 2026-10-19 from 4.0 to 5.2, which counted
     pieces per mark start). A newborn where an onset detector on the signal fires (tracking's measurement-driven
     births, Ristic, Clark, Vo and Vo 2012): it needs a threshold that trades missed dots against false starts, at
     low S₅₀₀ places a dot's start only to within several pieces, and repeats, less well, what the hypotheses do with
     the whole timing model.
   - **One newborn, which takes its speed when its first mark ends (owner, 2026-10-18).** While its first mark
     lasts, a newborn's score depends on its speed only through the probability that a mark has lasted this long:
     block lengths are in milliseconds and the level's rate is per second (topic 4, item 9), and the letter is
     charged at its start (item 9). So one newborn carries, at each piece, the best over the 32 speeds and over dot
     and dash of the speed's prior times that probability; when the mark ends it branches (item 1) into the speeds
     and labels that fit, each charged its own prior and its own probability of ending there. Every surviving
     hypothesis gets exactly the score it would have had from 32 newborns, and the beam keeps at least as many,
     since the newborn falls outside it only when every speed would (derived). It is the device of the merged mark
     (topic 1, item 2) and of the Farnsworth factor (topic 2, item 3; a newborn still takes its factor at its first
     gap between letters), and of speech recognition's prefix tree with its look-ahead (Ney and Ortmanns 1999, §4.1
     and §5.2). Births from silence use it too, and need it: they start at every piece. Simulated in noise (topic
     4's observation, level known, so lower bounds; 9 nats of room, which is item 4's 10-nat margin plus the 3.1
     nats of Farnsworth prior not yet paid, less the 4.1 nats of letter charged at birth, when births from silence
     have the largest starting cost), the newborns alive at once in their first mark are 2, 14, 44 to 47 and 140 to
     200 at S₅₀₀ +10, 0, −5 and −10 dB with it, against 64, 460, 1 300 to 1 400 and 3 300 to 4 100 without (ranges:
     blocks of 78 ms to one block), when the ceiling is about 1 700 (item 5).
   - **What a newborn from silence leaves behind in noise: the beam and the ceiling alone, the standard (owner,
     2026-10-18; the problem was found the same day).** A newborn that reads a few pieces of noise as a short mark
     branches, as that mark ends, into the speeds that fit it. Each branch is then in a gap, which is scored as noise
     exactly as the no-signal hypothesis scores it, so it costs nothing more until the gap grows too long: with the
     Farnsworth factor still open, up to a word gap at a factor of 8. Simulated as above (only these first branches;
     the Farnsworth prior paid once the gap reads as one between letters; P(a letter ends a word) taken as 0.2, a
     heuristic since replaced by 1/3.06, below, which moves a gap's cost by 0.45 nats at most; not rerun), they are
     0, about 3 400, 18 000 to 22 000 and 55 000 to 77 000 alive at once at S₅₀₀ +10, 0, −5 and −10 dB, the same
     with or without the shortcut. Followed through their whole first letter (up to four marks), the beam alone
     would keep 1 to 2, about 4 000, 24 000 to 27 000 and 55 000 to 91 000 stories (ranges over block lengths and
     runs). S₅₀₀ is here the level of the station the newborns look for, taken as known, so these are lower bounds.
     - **The standard: decoders do not search long silences.** Speech recognizers cut the audio at them: "A common
       practice for processing long-form utterances is to first segment the audio upstream with a separate voice
       activity detector (VAD)"; "At each segment boundary, the beam search finalizes the top hypothesis by
       discarding all other hypotheses" (Huang et al. 2022, E2E Segmenter). Their detector cuts after 0.2 s of
       silence and then discards frames until speech returns, "a practical measure to save computation", which
       raised their word error rate by about 2% absolute. Kaldi's endpointing (online-endpoint.h) "times out after 5
       seconds of silence, even if we decoded nothing", and after 0.5 to 2 s once something has been decoded. Inside
       a segment, silence is decoded like everything else, under the same beam and maximum count (items 4 and 5).
       Tracking deletes tentative tracks by their score: "The score increases with each hit and decreases with each
       miss" (MathWorks' track logic, after Blackman and Popoli 1999). That does not carry over to stories inside a
       channel: silence in a gap is what a Morse story predicts, so its score does not fall.
     - **Ours:** the cut is already in the design, for whole channels: the decoder closes the channel once no Morse
       hypothesis has led for a hold time, and the detector reopens it with a replay (opening and closing channels,
       decisions 2 and 3). A cut that kept only the no-signal hypothesis without closing the channel would not help,
       since births rebuild the population within a second or so. What remains is noise during the hold time and in
       shorter pauses, where the standard is the beam and the ceiling alone. The first prototype, on oracle channels
       open for the whole recording, makes no cut.
     - **How the ceiling reconciles the counts (simulated; level known).** The decoder never holds more than the
       ceiling (item 5): at every piece it keeps the best 1 700 and drops the rest. That costs a station nothing only
       if the stories that matter rank inside the ceiling. The leader (the no-signal hypothesis) does; a station's
       newborn is the question. A newborn born at the first piece of a real mark, at the same S₅₀₀, against the noise
       stories above (ranges: blocks of 78 ms to one block; "ahead" means a better score):

       | S₅₀₀ the newborns look for | Noise stories ahead of a fresh newborn | The 1 700th story trails a fresh newborn by | A station's newborn at birth: stories ahead | Its worst place in its first 100 ms (unluckiest tenth) |
       |---|---|---|---|---|
       | 0 dB | 10–12 | 7.3 nats | 9–10 | 27–29 |
       | −5 dB | 21 | 4.5–4.8 nats | 22 | 57–58 |
       | −10 dB | 15–21 | 4.0–4.4 nats | 16–23 | 67–80 |

       Why so few rank ahead (derived): every noise story has paid a newborn's starting cost; once its mark has
       ended, it has also paid the probability of that mark ending in that piece (1.5 nats for a 100 WPM dot;
       2.4–5.6 nats at 12–40 WPM, item 9), and for a gap read as one between letters the Farnsworth prior (3.1
       nats). To get ahead of a fresh newborn it must have gained more than that from the noise. About 90% of the
       noise stories lie 5 to 9 nats behind a fresh newborn, at the edge of the beam's room. A station's newborn
       gains from its first piece: within 100 ms the median one leads every noise story at all three levels, and
       after its first mark a story that follows a decodable station keeps gaining on average (derived).
     - **What the ceiling costs in a quiet channel:** the full CPU budget at S₅₀₀ 0 dB and below, for as long as the
       quiet lasts; and an effectively narrower beam, which keeps stories within 4.0–7.3 nats of a fresh newborn
       instead of 9, so that a waiting story (late in a long pause, the station's own "long word space" story) is
       dropped 2 to 5 nats sooner than by the beam alone: a search error of the beam's kind, counted as one (item 4).
     - **Limits:** with each story learning its level from its own marks, noise stories fit the noise better and
       newborns pay to learn their level, so the ranks could shift. Only the noise stories' first letter is
       followed, so the count ahead of a newborn is a lower bound in that respect; the population is the beam's
       alone, an upper bound in the other (with the ceiling on, some of those ahead would have been cut earlier).
       Checked on the prototype by item 4's count of newborns removed by the ceiling, also on quiet stretches
       (item 8).
     - **Considered, not adopted: a dominance rule** adapted from change-point detection's exact pruning (PELT;
       Killick, Fearnhead and Eckley 2012, Theorem 3.1). It drops a story waiting in a gap between letters or words
       once it trails the no-signal hypothesis by more than B + K: B is a newborn's starting cost apart from its
       level (ln(1/P) plus the speed and factor priors), K the most the story's learned level can be worth over a
       newborn's level prior, ln(σ0/σ) + (μ − μ0)²/(2(σ0² − σ²)), for a level estimate μ with spread σ and a prior
       centered on μ0 with spread σ0. Simulated with K = 0, it leaves about 450, 2 700 and 6 800 to 9 500 stories
       at S₅₀₀ 0, −5 and −10 dB. It is exact only in an idealized decoder, not in ours (derived): the newborn that
       would replace a dropped story is born when the story would resume, by when its prior's center has followed
       the detector's level (topic 3, item 5); a waiting story's level spread grows at the level model's rate
       (topic 3, item 1) and can outgrow the prior's, after which no finite K exists; and merging is approximate
       (item 2), so dropping a story can change which story survives a later merge. A sum over hypotheses (topic 8)
       would also lose the dropped stories' share, under 2 × 10⁻⁴ of the no-signal hypothesis's at S₅₀₀ 0 dB, and a
       text model with memory (topic 6) would need a text part in K. Taken up only if the idle channel's CPU shows a
       need, and then as a pruning threshold judged by search errors (item 4). Extended to gaps between elements it
       is heuristic: 17 symbols (the digits 0, 1, 3 and 9; `,` `?` `!` `'` `)` `@` `_` `$`; SK, BK, CL, HH and SOS)
       have a prefix after which their remaining elements spell no symbol.
     - **Open, for the priors at birth (below): the level newborns look for in a long pause.** Their
       prior is centered on the detector's level (topic 3, item 5), which falls toward the hiss within ln R seconds
       of the station's last mark (2.3 s at R = 10 dB, 4.6 s at 20 dB, per bin; opening and closing channels). The
       newborns then look for ever weaker stations, down the table above, and the next over's newborns start from a
       prior centered near the hiss. Candidates: hold the last station's level through the pause, widened at the
       level model's rate; a floor at the weakest level worth decoding.
   - **The priors at birth (review 1, K8, O5 and O7, and its correction 17): what a newborn starts with before the
     signal has chosen anything, decided one at a time from 2026-10-19.** The standard is to give each value its
     probability in real traffic, either flat over a set range or measured from the population; rates of events are
     measured too, or tuned on development data where they absorb the model's errors (amended 2026-10-19).
     - **Speed and Farnsworth factor: flat (owner, 2026-10-19).** A newborn's speed is each of the 32 with
       probability 1/32 (3.47 nats), and its factor, taken at its first gap between letters, each of the 23 with
       probability 1/23 (3.14 nats). This is the simpler of the two standard forms, used by the tempo tracker that
       our speed grid follows (Krebs, Böck and Widmer 2015): madmom, its reference implementation (55 to 215 BPM),
       gives no initial distribution, and "if 'None' a uniform distribution is assumed". That is flat over its
       states, which weights each tempo by the frames in its beat (slow tempi up to 3.9 times fast ones). The other
       form is Ellis's (2007): a Gaussian in log tempo centered on 120 BPM, whose width (1.4 octaves) gave the best
       agreement with listeners on development data; he needed it because tempi a factor of 2 or 3 apart fit the
       same beats almost equally well, as speeds a factor of 3 apart can fit the same Morse.
     - **Why flat:** with a flat share, a measured prior moves a newborn's starting cost by a nat or two (derived).
       Half the everyday distribution of CQ calls on the Reverse Beacon Network (25 WPM, standard deviation
       5.34 WPM; amateurradio.com, "The need for speed in CW") and half flat would charge 2.2 to 2.3 nats near
       25 WPM and 4.2 at the ends of the grid, against 3.47; a single 30 WPM dot at S₅₀₀ 0 dB carries about 18 nats
       of evidence (simulated mean, level known). So it decides only ties, and the signal soon breaks them: "EEE" at
       10 WPM and "TTT" at 30 WPM with a factor of 3 are timed alike (120 ms marks, 360 ms gaps between letters)
       until any other letter is sent (derived). Without a flat share a measured prior is only as good as its data:
       the network counts stations calling CQ, mostly in contests, as read by skimmers. Fitted to a contest (about
       31 WPM, standard deviation about 3.7 WPM: N6TV's count of CQ WW CW 2013), it would charge a 12 WPM newborn
       30 nats as a Gaussian in log speed (14.5 nats as a Gaussian in WPM); since the largest starting cost sets the
       beam's minimum width (item 4), slow newborns would either widen the beam (by about 27 nats to reach 12 WPM,
       more for slower speeds) or be dropped at birth.
     - **Not adopted: a measured prior,** mixed with a flat share so that no value becomes improbable: the
       network's CQ speeds, and for ragchews the speeds in the names of IZ4KBW's recordings; for factors, topic 2's
       measurement on real keying (item 5). **Test (topic 9):** tried only with a real advantage, judged on short
       overs (contest exchanges), where step 1 expects priors near typical values to help (P7(e)), and on ties of
       the ×3 kind, each counted separately.
     - **Rates of overs and restarts: the real rates, counted, not tuned (owner, 2026-10-19).** P, the probability
       that an over starts in a given 2 ms piece of silence, and ε, that a sender begins a new story at a letter
       start, are rates of real events. Step 1 (P10.1): "parameters with a physical meaning are derived from physics
       or measured from data"; "the remaining free parameters (scale factors, penalties, beams) are tuned on
       development data". Change-point detection uses a constant rate chosen for the data (Adams and MacKay 2007
       set it by hand: one change per 250 or per 1 000 samples), and results can "depend strongly on the choice of
       h" (Wilson, Nassar and Gold 2010). When a training set's mix differs from real use, the priors are reset to
       real use's rates (Saerens, Latinne and Decaestecker 2002).
     - **Placeholders until counted: the development set's own rates** (counted from the generator's plans: seed 1
       at the default sizes, 1 486 oracle channels). P = 1/10 000 per piece (9.2 nats): 1 746 overs in 10.0 hours
       of silence, 1/10 300 (by group, 1/4 000 to 1/13 600 with one over per signal, 1/6 400 for QSOs on one track,
       1/37 000 on two), mostly silence after each channel's last over, since a recording runs until 2 s after its
       longest signal ends (17 s per channel on average). ε = 1/1 000 per letter start (6.9 nats): the
       speed-change group's one change per signal (54 steps, 54 ramps) in 126 600 letter starts, 1/1 170 (1/2 340
       counting only the steps, 1/400 counting each ramp as the 10% speed steps it crosses). If the decoder is
       right, tuning on the development set would aim at these: decisions that use the true probabilities make the
       fewest errors (Bayes's decision rule; exactly for errors in the whole story, approximately for CER).
     - **What they do (derived; σ 0.3, level left out).** A newborn from silence starts 16.8 nats behind (9.2 for
       P, 3.47 for its speed, 4.1 for its first letter) and pays 3.14 for its factor at its first gap between
       letters; a rescue newborn starts 13.5 nats behind (6.9, 3.47 and 3.14). A late lead costs delay, not letters:
       the newborn brings every letter since its birth when it leads. At the best possible decoder's limit (S₅₀₀
       −1.9 dB at 25 WPM) a letter carries about 59 nats against no signal (simulated, PARIS mix, level known), so
       a newborn from silence leads within its first letter, and a tenfold change of P moves its lead by 2.3 nats,
       about 4% of a letter. After a speed change by f the old story misfits by (ln f)²/(2σ²) per element (0.38 nats
       at ×1.3, 2.7 at ×2: topic 2, item 2), so the newborn leads after about 35 and 5.1 elements; a tenfold change
       of ε moves these by 6 and 0.9 elements. Bounds if the model is right (Ville's inequality): at most 500·P
       false overs per second of noise (180 per hour) and at most ε false restarts per letter start. On white noise
       false overs are far rarer (simulated, level known: in 50 s at each of S₅₀₀ 0 and −10 dB no noise story came
       within 7 nats of leading; extrapolated, at most about 0.2 per hour). A false restart keeps the text, and does
       harm only if it reads the following letters differently.
     - **Checks on the prototype (topic 9), each rate also at ten times and a tenth of its value:** false characters
       per hour on noise-only input (step 1, P5(h)); the first word of each over in the QSO groups (G2, H2) and the
       delay against false alarms (P7(h)); the elements taken to follow the speed-change group's steps (D2) against
       false restarts in the constant-speed group (A2). The sweep is a check, not a tuning: CER barely sees these
       rates (a late lead costs delay, and the first prototype prints at the end of each recording), and a
       better-scoring value far from the counted rate points to an error elsewhere (the fist, the level, the
       search), to be found, not adopted. If something must be tuned, it is the insertion penalty (review 1, S20
       and O11; not yet discussed).
     - **The real rates (topics 7 and 2):** counted from topic 2, item 5's machine labels of IZ4KBW's and HamSCI's
       recordings, which need no transcripts. P from the pauses between overs, in channels kept open as topic 7's
       hold time will keep them (P is per piece of silence in an open channel, so it depends on the hold time); ε
       from speed changes within overs and from new stations that start without a pause (how a turnover splits
       between the two rates is settled in topic 7). They replace the placeholders when counted.
     - **Not adopted for the rates:** P from an accepted rate of false overs (Neyman–Pearson, as for the detector's
       threshold: opening and closing channels, decision 1): one per channel-hour by the bound needs P =
       1/1 800 000, 5.2 nats more, which widens the beam and on white noise buys almost nothing. Both rates tuned on
       the development set, as speech tunes its insertion penalty: tuning can at best return the generator's rates,
       which its code gives; the development set's CER barely depends on them; its rates reflect how it was built
       (how recordings are packed, how many speed-change signals it holds); and its white Gaussian noise cannot
       show what a large P costs on the air, where static crashes and other signals make false overs. Speaker
       diarization shows the risk: one of BUT's systems for DIHARD III tuned VBx's P_loop, the extra probability of
       keeping the same speaker from one 0.25 s step to the next, to 0 on the development set (Landini et al.
       2021), when real speakers talk for seconds. Learning the rates per channel as data arrive (Wilson, Nassar
       and Gold 2010): a possible refinement in topic 7.
     - **Where words and overs end, without a text model: constants, counted (owner, 2026-10-19).** Not priors at
       birth, but decided with them. After each letter the gap is one between letters with probability 1 − p, one
       between words with p(1 − q), or the over's end with p·q, where p is the probability that a letter ends its
       word and q that a word ends its over. Review 1 lists p among what the first prototype needs (O7: "all
       symbols equally likely" does not fix "P(word gap) after a letter"); q, which the over's end needs (topic 1,
       item 3), had not been set anywhere. The standard: a text model's probabilities are maximum-likelihood counts
       from in-domain text (step 1, P6.7), here words ÷ letters for p (1 ÷ the mean word length) and overs ÷ words
       for q; the end of a sentence is a symbol of its own, counted like any other ("We need the end-symbol to make
       the bigram grammar a true probability distribution"; Jurafsky and Martin, Speech and Language Processing,
       chapter 3). Step 1 counts the word space as a symbol of the model by its own judgment, consistent with the
       error metric (P6.8). The 0.2 taken until now came from Bell's simulator (after a letter, a word space with
       probability 0.1 and a pause with 0.1: `docs/research/bell-1977-reference.md`), which nothing measured.
     - **The values, until recounted on real traffic.** p = 1/3.06, measured by VE3NEA: the mean word length in CW
       Skimmer's decodes of on-air traffic, the table our generator draws from (`training/kz4ap_synth/messages.py`);
       "measured, but of all traffic (not conversation), at the unigram level, and with decoding errors" (step 2).
       The development set gives 1/3.01 (42 661 words in 128 345 letters; seed 1, oracle channels; 1/2.80 in the
       QSO groups, G2 and H2), within 0.02 nats. q = 1/24, the development set's own (1 746 overs of 42 661 words,
       1/24.4, set by its 100-character signals). Real overs run from 3 words (a contest exchange, "K1ABC 5NN 14")
       to about 50 (a two-minute ragchew over at 25 WPM), so q lies between 1/3 and 1/50 (ending an over costs 1.1
       to 3.9 nats). Both are recounted from topic 2, item 5's machine labels, whose word boundaries come from the
       text, where VE3NEA's come from CW Skimmer's own spacing decisions; q in topic 7, as words per over. A text
       model (topic 6) will give its own word ends in place of p.
     - **What they do (derived; σ 0.3).** A gap between letters costs 0.40 nats, one between words 1.16 and the
       over's end 4.30 (ln(1/(1 − p)), ln(1/(p(1 − q))) and ln(1/(p·q))). The 0.77 nats between the first two move
       the line between those readings from 4.58 dots (√21, at equal priors) to 4.97, times the Farnsworth factor;
       timing alone separates a perfectly timed letter gap from a word gap by 4.0 nats (topic 2, item 6). After an
       over's last letter, "the over has ended" overtakes "a long word space" once the gap's survival (item 9) has
       cost ln((1 − q)/q) = 3.1 nats: after 1.67 times the median word gap, 0.56 s at 25 WPM with a factor of 1.
       From then until the next over no Morse story leads, which starts the channel's hold time (opening and
       closing channels, decision 2); the over's text is kept either way. Letters are hardly touched: word spaces
       count in the CER, and the benchmark reports a space error rate of its own (`docs/signal-processing.md`,
       A.11). Births' starting costs and item 4's minimum beam do not change.
     - **Check on the prototype (topic 9):** the space error rate per group, beside the CER.
     - **Not adopted:** a word end that depends on the letter's place in the word, from VE3NEA's whole table (0.18
       after the first letter, rising to 0.62 after the fifth): each hypothesis would carry its place (1 to 16), so
       fewer merges and more CPU, for about 0.12 nats per word (the Kullback–Leibler divergence of VE3NEA's lengths
       from a constant end probability), and a text model that carries the current word (topic 6) gives it anyway.
       Tuning p and q on the development set, for the reasons given for the rates.
     - **Still open:** the level (the prior's width, its center in a long pause, and whether a rescue newborn keeps
       its parent's level).
   - **Derived constraint** (review 1, D15): a newborn starts behind the best by the cost of its birth, ε and the
     priors of what it starts with, and survives the beam (item 4) only while that cost is inside it (with the
     priors left out, only if ε is larger than 1/B, where B is the beam factor); otherwise narrowing the beam
     silently disables births. The beam therefore has a minimum width (item 4; owner, 2026-10-17).
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

   **Minimum width, so that births survive (owner, 2026-10-17; answers review 1, D15 and O5).** A newborn (item 3)
   starts behind the best by the cost of its birth, and the beam drops it at once if that cost is larger than the
   beam. A beam narrowed by the measurement above would then disable births, with no sign but unexplained search
   errors.
   - **The standard:** one beam, relative to the best, set by experiment as "a compromise between speed and avoiding
     search errors" (the HTK Book for HTK 3.4, Young et al. 2009, §13.1). In speech recognition a word start pays
     its language-model probability and survives only inside that beam; language-model look-ahead brings that cost
     in early (Ney and Ortmanns 1999, §5.1–5.2). Speech recognizers' separate threshold for word starts, cited by
     review 1, is an additional, narrower cut to save CPU, not a protection: "an additional pruning step" (Ney and
     Ortmanns 1999, §5.1); "since the range of language model probabilities is relatively small, a narrower beam
     can be applied to word-end nodes without incurring additional search errors" (HTK Book, §13.1).
   - **The rule (ours, on a derived bound):** the beam the decoder runs with is never narrower, in nats, than the
     largest starting cost of any newborn plus a margin of 10 nats. A newborn's starting cost is ln(1/ε) (born from
     silence, ln(1/P), with P the probability that an over starts in a given piece), plus the prior cost of its
     speed, plus that of its Farnsworth factor, paid at its first gap between letters (topic 2, item 3), plus the
     cost of learning its level from its first marks (topic 3, item 1). The speed and factor priors are flat, 3.47
     and 3.14 nats, and P and ε are the real rates, until counted the development set's own, 1/10 000 per piece and
     1/1 000 per letter start, 9.2 and 6.9 nats (item 3; owner, 2026-10-19); the level's prior is set with the rest
     of the priors at birth (item 3; review 1, K8 and O7). Leaving out the level, births from silence then have the
     largest starting cost, 15.8 nats against 13.5 for rescue births, and the minimum is 25.8 nats. (Corrected
     2026-10-19: the earlier illustration charged ε at every mark start, 700 in 2 minutes at 25 WPM, instead of at
     each letter start, 250; per letter start, one change per 10 s to one per hour gives rescue births starting
     costs of 9.6 to 15.5 nats, against 15 to 21 had ε been charged per piece.)
   - **Why the starting cost and not the whole catch-up (derived):** at the right speed a newborn gains on the old
     story by the old story's misfit, on average at every element, so its deficit shrinks from its starting value
     apart from swings (within an element, and from the sender's jitter and the noise), which the margin and its
     check cover; and a newborn the beam drops is replaced at the next birth by one with the same starting cost.
     Examples (σ 0.3, PARIS mix): after a change from 20 to 30 WPM the old story misfits by 0.91 nats per element,
     and a newborn 15 nats behind leads after about 16 elements (1.2 s); after 25 to 30 WPM, 0.19 nats per element
     and about 81 elements (5.8 s), the late following of small changes that topic 2, item 2 accepts.
   - **The margin, 10 nats (derived from item 9; checked by measurement):** the 5.6 nats by which a story that has
     just ended an element can trail one still in it (the largest at median lengths, 12 to 40 WPM), plus the
     4.1 nats of a letter charged when it starts (the first prototype's 1/62), which can coincide: 9.7 nats,
     rounded up. Check: the largest deficit any newborn reaches before it leads, per group, in the wide-beam
     reference run; if it exceeds the margin, the margin is raised to it.
   - **In the measurement above:** births are on in the wide-beam reference and in every narrowed run. Search
     errors are reported per group of the development set as well as pooled (a pooled 0.1% can hide errors
     concentrated in a few groups: step 2, T5.4b). Two counts are kept per group: newborns the beam drops within
     their first dot, dash or gap, and newborns removed by the ceiling (item 5), which removes the lowest
     hypotheses first, newborns among them (in HTK, reaching the limit narrows the beam, to the same effect).
     Births get protection from the ceiling only if that count shows a need.
   - **Rejected:** newborns exempt from the beam for a while and compared only with each other (review 1's
     alternative): the sources do the opposite, and it adds a duration and a width with nothing to derive them
     from. The measurement alone, with no minimum: standard and simplest, but a pooled target could accept a beam
     that switches births off where they are needed (speed changes; a new station without silence between overs).
5. **Ceiling on the number of hypotheses, set by CPU, not accuracy:** a fixed CPU budget per channel; how often the
   ceiling rather than the beam decides is counted (the maximum count is "histogram pruning", standard: Steinbiss,
   Tran and Ney 1994). Rough
   estimate: about 50 ns per hypothesis per piece, so a budget equal to the bank decoder's 42 ms per channel-second
   allows about 1 700 hypotheses; normally a few hundred at most are needed (estimates, to be measured). In a
   channel holding only noise, at S₅₀₀ 0 dB and below, the ceiling rather than the beam decides at every piece until
   the channel closes; what it drops there is noise, and a station's newborn ranks far inside it (item 3; added
   2026-10-18).
6. **CPU as a requirement:** the target (owner's decision) is the spec's estimate for the Classical mode, 2–3 million
   floating-point operations per channel-second for typical signals (design spec §5.1, where it is an estimate, not
   yet measured); at the ceiling the estimate is 10–25 million, so the ceiling should serve only hard moments.
   Quiet stretches at S₅₀₀ 0 dB and below also run at it until the channel closes (item 3; added 2026-10-18),
   measured with the CPU of a channel holding no station (item 8). CPU per channel-second is measured on the first
   prototype, together with accuracy, before any refinement.
7. **Overload governor (owner, 2026-10-14): goal, as many stations as the CPU allows.** When the machine cannot keep
   up: (a) first decode every station with fewer hypotheses (all ceilings lowered); (b) only when the ceilings reach
   a floor below which decoding falls apart, drop the weakest stations.
8. **The ceiling, the floor and the cost are measured, not guessed (owner):**
   - **Cost per hypothesis per piece:** timed on the prototype on the Linux machine (replacing the 50 ns estimate).
   - **How many hypotheses a station needs:** the distribution of live hypotheses per piece over the development
     set, per group (clean, weak, fists, speed changes, Farnsworth).
   - **Quiet stretches (added 2026-10-18, item 3):** live hypotheses per piece split into overs and the pauses
     between them, and on noise alone at the levels the priors at birth allow; how often the ceiling decides there;
     and item 4's count of newborns removed by the ceiling. With the CPU of a channel holding no station (opening and
     closing channels), they show whether quiet stretches need more than the beam and the ceiling (item 3).
   - **The ceiling and the floor:** the decoder is run on the development set at a range of ceilings; the CER
     against the ceiling gives the knee (the default ceiling: little accuracy gained above it) and the floor (the
     CER rises steeply below it).
   - **Cost per station-second** follows from the three, and is reported for typical and hard signals.
9. **Complete scores at every piece, so that the beam compares like with like (Claude's ruling at the owner's
   request, 2026-10-16; answers review 1, O4, and its caveat to D16; step 2, section 5.1, row P2.2/P4.5, and section
   5.4).** In plain terms: each hypothesis keeps a running tally of how well its story explains the signal, and at
   every piece the beam throws out any story too far behind the leader (item 4). That is fair only if every tally is
   up to date. As written before, three kinds of points were booked late: the credit for the signal in a mark still
   under way, the penalty for a mark or gap that has run long, and the cost of each letter. So a story halfway
   through a dash, its credit not yet booked, was compared with a story that had ended a dot a moment earlier and
   been credited for it. Every point is now booked as it is earned. No complete story's score changes; only what
   the beam keeps along the way. A nat, below, is a factor of e in a score.
   - **The standard:** time-synchronous Viterbi search, in which every hypothesis has been scored on the same input
     at each step (Ney and Ortmanns 1999; step 1, P4.4).
   - **Evidence:** the score of the current block so far is computed from its running sum at every piece, exactly as
     a finished block's score is (topic 4, items 2 and 4), so it counts as soon as the pieces arrive and a finished
     block scores what it did before (derived). What late booking cost (derived): at 20 WPM, with a dash 30 ms under
     way, the rival story "a dot that ended at 30 ms" had already been credited for those 30 ms (12.9, 147 and
     1 496 nats at S₅₀₀ 0, 10 and 20 dB) less 5.3 nats for so short a dot, so it led the true story by 7.6, 141 and
     1 490 nats, and any narrower beam dropped the true story. Booked on time, the true story leads by 5.3 nats.
   - **Durations, in the hazard form** (Murphy 2002; step 1, P2.2; Bell 1977 did the same: "the probability of
     extending duration D for one more sample interval"). At each piece, a mark or gap that continues multiplies the
     score by S(τ + 1)/S(τ), the probability of lasting one more piece, and one that ends by the probability of
     ending in that piece; S(τ) is the probability, under the fist model (topic 2), of lasting at least τ. An
     unfinished element has then paid S(τ). The single "mark lasting so far" hypothesis (topic 1, item 2) takes the
     better of the dot's and the dash's survival, each times its label's probability, and a gap the best of its
     kinds'; it then scores exactly what the better of separate hypotheses would (derived), which settles review 1's
     caveat to D16. What late booking cost (derived, 25 WPM): an unfinished mark has used 3.4 nats at 0.25 s,
     11.0 at 0.5 s and 23.7 at 1 s, an unfinished gap 2.4 nats at 0.5 s, 8.9 at 1 s and 20.4 at 2 s. Booked at the
     end, a tune-up carrier was a free Morse mark at every speed for as long as it lasted, and a long silence kept
     "a long word space" level with "no signal" until the next mark.
   - **Text, by look-ahead** (Ney and Ortmanns 1999, §5.2, "used in many systems for large-vocabulary speech
     recognition"; Ney and Ortmanns 2000): at each node of the Morse tree, the score carries the best probability of
     any letter still reachable from it, corrected when the letter completes, so each letter's probability is paid in
     full as before, only earlier (topic 6, amended). In the first prototype, with all 62 symbols equally likely,
     this means charging each letter's 1/62 (4.1 nats) when it starts. With look-ahead, a score is the probability of
     the story so far times the best text probability still reachable from its node.
   - **What remains:** the probability of ending in one particular piece, 2.4 to 5.6 nats at the median lengths of
     dots, dashes and word gaps from 12 to 40 WPM (derived). A story that has just ended an element trails one still
     in it by about that much until the second ends too. It comes from the standard hazard form, whose survival
     counts every end time still possible while an ended element has paid for one. A look-ahead for durations,
     charging the best end time still possible (review 1's second remedy), would remove it, but we know of no
     standard source for it; the beam measured in item 4 absorbs it.
   - **Checks (topic 9):** item 4's search-error measurement, where unfair comparisons would show up as errors at
     beams that should be safe; and strong clean signals (S₅₀₀ 20 dB and above, every speed of group A) decoded with
     a beam of 20 nats, or item 4's minimum width if that is larger (added 2026-10-17), must give the wide-beam
     reference's text. The 20 nats are heuristic: several times the residual above plus one letter's 4.1 nats, and
     far below the 141 to 1 490 nats a late credit costs.
   - **Cost:** the block's score is computed at every piece instead of once per block; the survival and look-ahead
     factors are table lookups. Measured with the rest (item 8).
   - **Rejected:** keeping the factors late with a beam wide enough to cover them. At good SNR that needs hundreds to
     thousands of nats, which leaves the beam almost nothing to prune.

### Topic 6: the text model (owner, 2026-10-14)

The text model gives the probability of each completed letter given the text before it; it multiplies a hypothesis's
score each time the hypothesis completes a letter (amended 2026-10-16 by topic 5, item 9: the same probability is
paid earlier, by look-ahead; at each node of the Morse tree the score carries the best probability of a letter still
reachable from it, corrected when the letter completes). The decoder serves both uses the owner wants: skimming
(callsigns, CQ, contest and DX exchanges) and transliterating conversations (ragchews).

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
   words, the catch-all includes the probability of the word ending (without a text model, p: topic 5, item 3;
   added 2026-10-19), and the text-model weight is 1 (at weight λ the
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
6. **Start simple (owner).** The first prototype has no text model (the Morse tree only, all symbols equally likely,
   and two counted constants for where words and overs end: topic 5, item 3, added 2026-10-19), as the baseline.
   Kinds are added one at a time, each measured: callsign, token and number first, plain language second.
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
     (D8), with how fast the signal level may change (moved from topic 3, item 2, 2026-10-15): done 2026-10-16
     (topic 4 above, items 8 and 9);
   - topic 2: the Farnsworth range (D9), with step 2's findings on timing: done 2026-10-15 (topic 2 above);
   - topic 3: whether the decoder's own decisions may feed the amplitude and noise estimates (K3), with step 2's
     findings on amplitude: done 2026-10-15 (topic 3 above);
   - topic 5: fair comparison under the beam between hypotheses in mid-element and finished ones (O4; done
     2026-10-16, topic 5, item 9, Claude's ruling at the owner's request), births against the beam (D15 and O5;
     done 2026-10-17, topic 5, items 3 and 4), where rescue births start and when a newborn takes its speed (U7;
     done 2026-10-18, topic 5, item 3), what newborns from silence leave behind in noise (found and done
     2026-10-18: the beam and the ceiling alone, topic 5, item 3), and the priors at birth and for the first
     prototype (K8, O7; topic 5, item 3): the speed and Farnsworth priors (flat), the rates of overs and restarts
     (the real rates, the development set's own until counted), and where words and overs end without a text model
     (constants, counted) done 2026-10-19; the level, with the level newborns look for in a long pause, still open.
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
   re-centering target (§5.2, step 1) is too loose for whole-mark sums (D7, K13); the amendment replaces it with the
   accuracy topic 4's blocks need (topic 4, item 8).
7. **Building starts after topics 3, 4 and 5 (owner, 2026-10-15).** Then come a short spec amendment and the first
   prototype's plan, each for the owner's approval. Topic 6's reopened items, the new topics on the channel's other
   contents and on opening and closing channels (below; added 2026-10-16), and topics 7, 8 and 9 follow while the
   prototype is built and measured, each settled before its part is added. The reason: the first prototype needs
   only simple versions of those parts (it has no text model, topic 6, item 6; it can print its best story at the
   end of each recording; fresh starts already follow a change of station), while it cannot do without amplitude,
   the observation and the search. The risk accepted is rework if a later topic changes something already built.

### Opening and closing channels (owner, 2026-10-16; a new topic, settled during building)

Raised by the owner during topic 4: whether the signal detector ends a station's track during long pauses, whether
that matters, and how it decides that a station is there and that it is gone. There are two levels. The **signal
detector** opens a channel when it finds a station and closes it when it judges the station gone
(`docs/signal-processing.md`, section 6). Inside the channel, the **decoder's** no-signal hypothesis decides whether
Morse is being sent (topic 1, items 1 and 3; the standard way, step 1, P5.1 and P5.3). This topic is the first
level. Neither review examined it. Our records flag it: every detector number is heuristic, the noise floor is one
value for the whole span, and track lifetimes are set "by accident, not design" (`docs/signal-processing.md`, A.6);
the backlog items it takes up are listed at the end. Nothing here affects the first prototype, which is scored on
oracle channels open for the whole recording.

**The standard.**
- **Opening, in two stages** (target tracking). A detector proposes: its threshold is set from a chosen false-alarm
  rate, against noise measured around the cell under test (Neyman–Pearson detection, Kay 1998; constant false-alarm
  rate (CFAR) detection, Rohling 1983, whose ordered statistic topic 3 uses for the hiss). A better test confirms:
  M detections in N looks, or the track's score, the log-likelihood ratio of "target" against "nothing", crossing a
  threshold (Blackman and Popoli 1999). For weak targets, track-before-detect sets no threshold on single looks: it
  accumulates evidence under the target model and decides on that (Barniv 1985; Davey, Rutten and Cheung 2008).
- **Closing:** after a run of misses, when the score falls a set amount below its peak, or when the probability that
  the target exists falls below a threshold (Mušicki, Evans and Stanković 1994).
- **Speech:** the recognizer's own silence model decides where speech starts and ends, with a hold time (step 1,
  P5.1 and P5.3). Clipping the start of speech is a standard error measure of voice-activity detectors ("front-end
  clipping"; Beritelli, Casale, Ruggeri and Serrano 2002). The usual fix, replaying a buffer from before the
  detection, is documented only in product documentation and patents.

**What the detector does now, and what follows (derived).** Its rules are heuristic (`docs/signal-processing.md`,
section 6). The figures below come from a simulation of one detector bin with the detector's window, hop, 1 s
average and rules; white noise; PARIS text with 5 ms raised-cosine edges; the tone at a bin's center or halfway
between two bins; 300 stations per point. S₅₀₀ is the key-down SNR in 500 Hz.
- **Opening:** a bin's average must be at least 6 dB above the floor (the median of all 8 192 bins) and the highest
  within ±47 Hz, in every frame for 0.5 s. The 6 dB compares signal plus noise with noise, so the station's averaged
  power must be 4.72 dB above the noise per bin. The bin reads 3.8 dB (12 WPM) to 5.3 dB (80 WPM) less than the
  key-down power on a bin's center (the key-up time, and keying sidebands outside the bin), and 1.0–1.3 dB less
  again halfway between bins.

  | Speed | Opens every time within 20 s from (S₅₀₀; bin center / between bins) | Median delay there | Best possible decoder at CER 0.10, no text model | Measured CER-0.10 crossing, bank / Matched |
  |---|---|---|---|---|
  | 12 WPM | −3 / −2 dB | 2.9 / 3.1 s | −5.0 dB | −0.1 / −0.2 dB |
  | 25 WPM | −2 / −1 dB | 1.8 / 3.2 s | −1.9 dB | 0.0 / +1.1 dB |
  | 40 WPM | −2 / −1 dB | 3.6 / 3.8 s | +0.2 dB | +1.8 / +2.9 dB |

  The two right-hand columns are the stage-1 results record's genie-aided bound (derived, §5.1.1) and measured
  crossings (group A, oracle channels, §5.1). Today's decoders need more signal than the detector does, so it costs
  them nothing. A new decoder at the bound would lose 2–3 dB at 12 WPM, up to about 1 dB at 25 WPM (between bins)
  and nothing at 40 WPM. Near the threshold, channels open late: at 12 WPM and −3 dB, after a median 2.9 s and 5.9 s
  in 1 case in 10, about three letters. Strong stations open in 0.7–0.8 s.
- **On white noise, the 6 dB is set by nothing.** False channels per hour over 8 192 bins (simulated for one hour,
  without the peak rule, so slightly pessimistic): 1 569, 89, 5 and 0 at 1.5, 1.75, 2.0 and 2.25 dB above the floor,
  and 0 at 2.5, 3 and 6 dB. The largest averaged noise reading in the hour was 2.70 dB above the floor. At 2.25 dB,
  every station opened within 20 s from −9 dB S₅₀₀ at 12, 25 and 40 WPM (median delays 2.0, 3.5 and 4.4 s), and
  stations at the bound above opened after a median of at most 0.8 s. What the 6 dB does guard against has not been
  counted on real recordings: peaks beside very strong stations (on the stage-1 strong group, Matched and Envelope
  printed text on 12 extra channels, the bank prototype on 1; stage-1 results §5.4), signals that are not Morse, and
  a floor that is wrong locally.
- **Closing:** a channel stays open while its average is at least 3 dB above the floor (the station's averaged power
  at least equal to the noise per bin: down to about −7.8, −7.5 and −7.1 dB S₅₀₀ at 12, 25 and 40 WPM, far below
  anything copyable), and closes 10 s after its last frame above that. After a station's last mark its average takes
  ln R seconds to decay to the noise, R being the averaged signal-to-noise power ratio per bin, so channels close
  11–21 s after their station stops, and any longer pause closes them. A closing is not a judgment about the
  station. What it costs: the next over opens late and loses its start, as every opening does (first-word CER on the
  band group, bank prototype / Matched / Envelope: 0.753 / 0.842 / 0.808 through the detector against 0.068 / 0.308
  / 0.226 on oracle channels; stage-1 results §5.3, item 11); the decoder's state is lost, which costs the new decoder
  little (a resumed over is born from silence at every speed, topic 5, item 3, with its level from the detector,
  topic 3, item 5, and its frequency from the detector's peak, topic 4, item 8); and the text splits between
  channels.
- **Correction:** the backlog item "Does the detector miss stations the chosen decoder could copy?" put the opening
  threshold at −1.0 dB S₅₀₀ at 25 WPM, reading the 6 dB as signal over noise. Without the noise's fluctuations it is
  −3.0, −2.7, −2.3 and −1.5 dB S₅₀₀ at 12, 25, 40 and 80 WPM on a bin's center, 1.0–1.3 dB higher between bins; the
  simulated figures above include the fluctuations.

**Decisions (owner, 2026-10-16), for when the new decoder runs behind the live detector:**
1. **Opening: a threshold set from a false-channel rate, against a local floor, with the decoder confirming** (the
   standard two stages). The floor is topic 3's hiss estimator (the 25th percentile of the bins within ±2 kHz,
   corrected for its bias; topic 3, item 3), so the detector and the decoder use the same noise. The local floor is
   what makes a low threshold safe: with today's global floor, any stretch of band whose noise sits 2–3 dB above the
   band's median would open channels on noise alone (derived from the false-channel counts above). The rate is chosen
   from what a false channel costs (its CPU until the decoder closes it), and the threshold follows from the rate by
   measurement on real recordings; on white noise it would be about 2.25–2.5 dB above the floor (derived above). The
   decoder confirms: its score of Morse against no signal is in effect the standard's track score; it gates what is
   printed (topic 8) and closes the channel (decision 2). **Directly** (owner, 2026-10-16), not after a measurement
   shows a gap: the 6 dB is a heuristic where a standard exists, so the standard is the default; the measurements
   below set its numbers and size its gain.
2. **Closing: the decoder decides.** The channel closes once no Morse hypothesis has led for a hold time: the
   standard close, on the score or on the probability that the station exists, from what the decoder already
   computes. The hold time is set in topic 7, from measured pause lengths and the CPU of an idle channel.
3. **Replay at every opening:** the last few seconds of the channelizer's shared spectrum are kept, and a new channel
   starts from the oldest of them, so its decoder hears the transmission from its start (the backlog's fix; memory:
   8 192 complex bins × 46.9 frames/s × 8 bytes, 6.1 MB per 2 s at 192 kHz, derived). The length is set from the
   measured opening delays at the decoder's limit; the backlog's 2 s is a placeholder.
4. **Keeping a station through pauses** (a longer life for idle channels, or joining a new channel to one that closed
   recently at the same frequency and passing on the station's speed and level): topic 7 (step 1, P7.3 and P7.4; in
   tracking, track segment association, Yeom, Kirubarajan and Bar-Shalom 2004).

The present decoders print text on channels that hold no station (above), so the lower threshold goes live with the
new decoder, not before.

**Open, settled during building:**
- **Channels that hold something other than Morse.** A lower threshold opens more channels on carriers, other modes
  and peaks beside very strong stations. If the decoder closes such a channel (decision 2), the detector reopens it
  0.5 s later. Either such channels stay open without printing, or the detector remembers the frequencies the decoder
  has rejected; the nearest standard is radar's clutter map, which raises the threshold in cells that keep returning
  echoes (Nitzberg 1986). Settled with the new topic on the channel's other contents.
- **The 1 s average and the 0.5 s persistence** (heuristic): with the threshold, they trade the opening delay against
  the false-channel rate; they are derived with it, by the simulation above, for the chosen rate.
- **The cap of 200 channels** (a CPU guard) binds sooner with more channels open; the overload governor (topic 5,
  item 7) decides which stations keep theirs.

**Measurements (topic 9):**
- false channels per hour on HamSCI's recordings at thresholds from 2 to 6 dB above the local floor, with their
  causes (noise, peaks beside strong stations, other signals), and what the new decoder prints on them per hour
  (step 1, P5(h));
- the CPU of a channel holding no station, and of one holding a carrier, on the prototype;
- how often and how soon channels open against S₅₀₀ by speed through the detector, beside the new decoder's oracle
  CER (the backlog's measure);
- pause lengths in real traffic (topic 7), which also give the rate of overs P (topic 5, item 3).

**Backlog items taken up here:** "Does the detector miss stations the chosen decoder could copy?", "Wrong or missing
first characters", "Stray E's after a station stops", "Tracks outlive their stations; separate station identity from
decoding", "Ghost tracks beside very strong signals", and "Detector: averaging, noise floor and detection theory".
