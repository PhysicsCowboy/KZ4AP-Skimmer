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
   between letters (topic 2, item 3; added 2026-10-15). (The factor is removed from the first prototype, which assumes
   standard spacing: topic 2, item 3, amended 2026-10-21.)
5. **The position in the Morse tree:** the letter in progress.
6. **Past text:** the current word so far, plus the kind of the previous word (and which token, if it was a fixed
   token). Closed by topic 6.
7. **The last stretch of keying (amended 2026-10-25, owner; plan D17).** The expected front-filter output does depend
   on keying before the current element. An edge moves the expected output by 0.01 of full or more through the 11th
   sample after it (j = 0 to 10, j counting samples from the first at or after the edge; 29.3 ms, of which the first 5
   are the delay; derived), the plan's "about 10 samples (26.67 ms)" (amendment of 2026-10-25, part 5: the last section
   of these conclusions). The template itself is not truncated there: it is nonzero through j = 15 after an edge and
   exactly 0 from j = 16 (42.7 ms; 1 − s_j, the template's distance from full, is 9 × 10⁻⁷ at j = 14 and 4 × 10⁻¹¹ at
   j = 15; derived; plan D17, 4896e97: "The template stays nonzero for 16 samples (42.7 ms) after each key-up"). Each
   story computes its template (part 5) from its own key-down and key-up times within those 16 samples; merging does not
   compare them (topic 5, item 2). (Until 2026-10-25 this item read: "The last stretch of keying, if topic 4 finds that
   the expected filter output depends on keying before the current element.")
8. **Its score and a link to its parent,** to trace its text back when printing (topic 8). The score is the
   probability of its single best story (topic 5, amended 2026-10-14).
9. **A clock for each non-Morse state:** how long the carrier or the silence has lasted.
10. **Its amplitude and the amplitude's variance, and its long-run power's statistics (amended 2026-10-25, owner;
    plan D16, D17;** replacing "its signal level and the level's variance, learned from its own marks", added
    2026-10-15: the level differs between hypotheses because each learns it from its own decisions). A story carries:
    - a Gaussian amplitude N(μ, P) and the time of its last update;
    - the two running sums of its RMS since its over began (amendment of 2026-10-25, part 7; the variant with the
      noise in Ω̄, the expected long-run power, adds two for the RMS's noise variance);
    - for ν, the number of independent fades behind its RMS, the running sums and its last 10 bins of key-down weight
      (part 7).

    All are learned from its own samples (topic 3, item 1).

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
     elements at the first prototype's 13.5 nats (topic 5, item 3; amended 2026-10-19), and 43 to 47 and 6 to 7 once
     the 2.9 to 4.5 nats of learning the level again are added (topic 5, item 3; added 2026-10-19). Since ε now
     carries every speed change, setting it (topic 5) matters more.
   - **Test (topic 9):** small steps are added back only with a real advantage, judged on the development set's slow
     speed changes (D2's ramps) and on how much real senders drift (item 5's measurement).
   - The start of an over is not a speed change: hypotheses are born at every grid speed (topics 5 and 7; in
     practice one newborn, which takes its speed when its first mark ends: topic 5, item 3, 2026-10-18).
3. **Farnsworth gap factor (owner, 2026-10-15).** One factor F stretches letter and word gaps alike, keeping their
   3:7 ratio: the ARRL rule (Bloom 1990), F = (60c/s − 37.2)/22.8 for characters at c WPM sent at s WPM overall. The
   form is standard; carrying F as a hidden variable of each hypothesis is the adaptation.
   - **Amended 2026-10-21 (owner): removed from the first prototype.** The decoder assumes standard spacing (F = 1),
     and Farnsworth-spaced signals decode poorly until a later fix.
     - Measured in the prototype: the flat prior over 23 factors cost every reading of standard code ln 23 = 3.1 nats
       at its first letter gap, which a reading that merges letters avoids, so "TEST" sent alone decoded as "-".
     - The factor's branching was also the search's largest CPU cost.
     - The owner: "The vast majority of on air code is not Farnsworth."
     - What was found, and the thoughts for a fix (a concentrated prior, Farnsworth readings started on demand, or
       spacing learned per sender): `docs/backlog.md`, "Farnsworth spacing in the new decoder".
     - The factor and the grid below are removed from the code; the branch's history keeps them for the later fix.
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
   letters, its Farnsworth factor, removed from the first prototype by item 3's amendment of 2026-10-21), when a mark
   or gap ends, and its classification (dot or dash; element, letter or word gap).
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
   - **The fist's lengths are counted in 2.667 ms samples (amended 2026-10-25; owner, 2026-10-23: "Agreed"; plan D17,
     "every 4th channel sample", replacing D16's 3 ms and, before it, the 2 ms pieces of topic 5, item 1 and the
     plan's §2.5, item 3).**
     - The forms are unchanged: log-normal lengths, medians 1, 3, 1, 3 and 7 dots, σ = 0.3 in ln(length)
       (placeholders, above), and the hazard form (topic 5, item 9).
     - The rounding is the plan's §2.5, item 3's, with "piece" read as "sample": a kind's median is M = (its median in
       dots × the speed's dot) / 2.667 ms. A 100 WPM dot has a median of 4.50 samples; the longest median, a word gap
       at 5.2 WPM with standard spacing, is 605 samples (derived; amendment of 2026-10-25, part 11).
     - The 32 speeds are unchanged. Item 3 (Farnsworth) stands as amended on 2026-10-21 (the plan's D14).
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

Amended 2026-10-25 (owner, 2026-10-21 to 2026-10-25; plan D16, D17): items 1, 2 and 4 are restated for an amplitude
per story updated at every 2.667 ms envelope sample, with no blocks; the design in full, with its formulas and values,
is the amendment of 2026-10-25, the last section of these conclusions ("Detection, amplitude, frequency and the
mark's shape").

**Decision feedback (owner, 2026-10-15; answers review 1's K3).** The decoder's own decisions feed an estimate only
inside the hypothesis that made them; whatever is kept at channel level uses no decisions.

1. **The amplitude: each story tracks its own, every 2.667 ms (owner, 2026-10-15; amended 2026-10-25, owner; plan
   D16, D17).** Standard: per-survivor processing (Raheli, Polydoros and Tzou 1995), with Bell's (1977) form, one
   scalar Gaussian amplitude per path. This replaces one level per channel (step 2, T3.0, class (iii)), and, since
   2026-10-25, the level measured once per mark or block and kept between measurements (2026-10-15).
   - **Prediction** (owner, 2026-10-22, the rule; owner, 2026-10-23: "agreed", the prediction in full; plan D16, D17):
     a story's amplitude a at the front filter's output is predicted at each envelope sample (amendment of 2026-10-25,
     part 6).
     - The mean drifts from the last estimate toward the Rayleigh mean amplitude 0.886·√Ω̄ = (√π/2)·√Ω̄, at the
       amplitude's own correlation ρ_a over the time Δt since the story's last estimate.
     - The variance is ρ_a²·P + ½(1 − ρ²)·Ω̄. Its second term is the owner's rule: the spread √(½(1 − ρ²)Ω̄), the
       long-run amplitude times √((1 − ρ²)/2).
     - ρ is the fading's correlation at a fixed 2 Hz (item 2).
   - **Ω̄** is the expected long-run power given the story's RMS clipped at zero, the count of fades ν and the owner's
     flat prior (part 7; owner, 2026-10-25, the default; the noise in its likelihood a measured variant).
   - **Update:** each sample where the story's template is nonzero (s_k ≠ 0, the one sample set of part 7) updates
     N(μ, P) by the tempered Rician likelihood (part 4), moment-matched to a Gaussian.
   - **The prediction's spread** is set by the long-run amplitude, never by the story's current amplitude: the cause of
     the fading failure of 2026-10-21 (plan D16).
   - **A fresh start** scores its first mark against the owner's flat prior (part 8). Why it must learn its own level
     (derived, 2026-10-15): stories stuck at the old level read a new station's marks as noise, which costs them
     roughly 30 to 300 nats per 20 WPM dot between 0 and 10 dB S₅₀₀.
   - **The cost:** per story, the Gaussian's two numbers, the time of the last update, the RMS's two sums (two more for
     its noise variance in the noise-in-Ω̄ variant), about 12 numbers for ν (part 7) and the over's start time.
   - **Merging** is per-survivor, as before: of two stories in the same state, the better one's amplitude and sums are
     kept (topic 5, item 2).
   - **The derivation kept (2026-10-15):** the reasons for scoring against the predictive density rather than a level
     taken as exact stand. Why (derived): taken as exact, a mark more than 6 dB below the stored level reads as noise
     on a strong signal (the crossover is at half the amplitude), so the level stops learning. In a simulation of the
     earlier channel estimate, a drop of 8 dB or more at 20 dB S₅₀₀ was never followed in 60 s, and
     the weaker station was not decoded at all; drops of 6 dB were followed within about 5 s. With the variance in the
     score, a 20 WPM dot at 10 dB S₅₀₀ arriving 10 dB below the level scores 1 107 nats better as noise when the level
     is taken as exact, but 271 nats better as a mark when the level is uncertain by ±10%, and that one dot moves the
     estimate 98% of the way to its own level.
   - Dropped with the channel estimate: the 10 ms tap, its soft key-down weights (with their unstated key-down prior,
     0.44 by step 2, C8) and the 0.5 s window, and with them review 1's D4 to D6, which concerned them.
2. **How fast the amplitude may change: one fixed Doppler spread, f_D = 2 Hz (owner, 2026-10-22: "A, with B in
   backlog"; plan D16; amended 2026-10-25).** This replaces one fixed spread chosen by measurement that set both the
   block length and the level's rate (topic 4, item 9, 2026-10-15).
   - There are no blocks, so the spread sets only the amplitude's prediction (amendment of 2026-10-25, part 6) and ν's
     count (part 7).
   - Bell used 1 Hz. 2 Hz covers the development set's 5 Hz fading better.
   - Several rates per story are in the backlog.
   - The genie test (decoding with the generator's true spread) measures what the one fixed value loses (part 15).
   - **Background, kept (2026-10-15):** the derivations of what a too fast or too slow rate costs stand. At slow
     speeds in fast fading the level changes within a mark: a 5 WPM dash lasts 720 ms, while the fading's correlation
     halves after 0.375/spread seconds (375 ms at 1 Hz, 75 ms at 5 Hz; step 1, N5). Derived then, with one level
     measurement per mark:
     - a filter that assumes faster fading than the signal has costs sensitivity on steady signals. Per mark, at 1%
       errors, +0.4 dB of E/N₀ at ±20% level uncertainty and +2.7 dB at ±50%; at 0.1% errors, +2.1 and +12 dB. A
       1 Hz assumption leaves ±49% at 5 WPM, ±19% at 20 WPM and ±10% at 40 WPM;
     - one that assumes slower fading than the signal has loses marks in deep fades whatever the SNR: a
       Rayleigh-faded signal spends 22% of its time more than 6 dB below its average;
     - the standard ways to cover an unknown spread are one fixed value (robust design for the fastest fading
       expected, Li, Cimini and Sollenberger 1998, whose small mismatch loss relies on frequent channel measurements;
       Bell 1977 used one fixed model), an estimated spread (Tepedelenlioğlu, Abdi, Giannakis and Kaveh 2001), or
       several fading models inside each hypothesis (Blom and Bar-Shalom 1988). Our spreads range 100 to 1
       (0.05–5 Hz in group B2).
3. **The hiss (noise) level: one per channel, measured without decisions (owner, 2026-10-15).** It is read from the
   signal detector's averaged spectrum around the station: its 23.4 Hz bins (Hann window), each averaged over about
   1 s, are inherited from the detector (`docs/signal-processing.md`, section 6). A low-ranked value of the bins
   within a range around the station gives the hiss per bin; its bias on hiss alone is computed exactly from the
   detector's averaging and divided out, and the result is converted to a level per hertz, N₀, for topic 4's noise
   (topic 4, item 7; "sums" until 2026-10-25).
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
4. **Use (amended 2026-10-25; plan D17):** each envelope sample is scored against a carrier of the story's predicted
   amplitude, shaped by its template, plus the hiss and the systematic term (amendment of 2026-10-25, parts 4 and 9).
   This replaces comparing a hypothesis's summed mark (or block) with a carrier of its predicted level plus the hiss.
5. **Start of an over: no detector level (owner, 2026-10-19).** A hypothesis born from silence starts, like every
   newborn, from a level prior flat in dB, and takes its level from its own first mark (topic 5, item 3). This
   replaces the detector's level as the prior's center (2026-10-15), which would have needed a conversion (derived,
   not yet written out; review 1, U8; step 2, T3.6): the track's 1 s average includes key-up time, so the key-down
   power is about (average − hiss) divided by the key-down fraction (0.44 for PARIS text), corrected for the share of
   the keying sidebands that falls in the bin (the track reads 3.6–7.1 dB below the key-down SNR per bin, depending
   on speed: `docs/signal-processing.md`, section 5). In a long pause the detector's level falls toward the hiss, so
   that center would also have needed the detector's level over the second after each birth. A detector-centered
   prior is tried only with a real advantage (topic 5, item 3).

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

Amended 2026-10-25 (owner, 2026-10-21 to 2026-10-25; plan D16, D17): every item below is restated for one front
filter per channel, envelope samples every 2.667 ms, each story's predicted output, the channelizer following the
detector and no blocks. The design in full, with its formulas and values, is the amendment of 2026-10-25, the last
section of these conclusions.

1. **Every story is scored on the same evidence: envelope samples of one front filter, every 2.667 ms (owner,
   2026-10-22 and 2026-10-23; plan D16, D17; amended 2026-10-25).**
   - The channelizer's stream, already following the detector's frequency (item 8), passes through one filter per
     channel, matched to a 100 WPM dot: 12 ms at half amplitude, 2.5 ms (10–90%) raised-cosine edges, noise bandwidth
     B_N = 76 Hz (amendment of 2026-10-25, part 2).
   - Its envelope is sampled at every 4th channel sample, 2.667 ms (part 3).
   - Consecutive samples are correlated, 0.878 at one step. Each sample's log-likelihood is counted at a weight w_k
     that follows the sample's expected SNR, from 0.275 for weak samples to 0.1986 for strong ones, both from that
     correlation (owner, 2026-10-25; part 4).
   - The pieces, their sums and their noise c(m) are gone. (They were the channel signal, re-centered by item 8's
     fine frequency shift, cut into non-overlapping pieces, each the sum of the complex samples over about 2 ms, with
     the cost of placing edges at piece boundaries derived in review 1, D11, and step 2, T4.3.) Reading
     speed-specific filters would still score stories at different speeds on different data, which one filter per
     channel avoids.
2. **The matched filtering is done once per channel, and each story predicts its output: the smeared prediction
   (owner, 2026-10-23: "I want option B"; "OK agreed"; plan D17; amended 2026-10-25).** This replaces each hypothesis's
   own matched filtering (a complex running sum of the pieces it claimed inside its current mark, 2026-10-12, or block,
   2026-10-16) and its amendment of 2026-10-20 (a mark's rise in its matched filter).
   - Each story scores each envelope sample with today's noncoherent statistic, tempered by w and integrated over its
     predicted amplitude (amendment of 2026-10-25, part 4).
   - The carrier's expected envelope is the story's template s_k (part 5). That is the front filter's output for the
     story's claimed keying, rise and fall, including the channelizer's filter, averaged over the edge's position
     within a sample.
   - The fall is now modeled: a story keeps scoring its carrier after a key-up wherever its template is nonzero, through
     j = 15 (16 samples, 42.7 ms); it is at or above 0.01 of full through j = 10.
   - Standard: sequence detection over a smearing channel (MLSE with intersymbol interference, Forney 1972).
   - The rise template of 2026-10-20 (5 ms placeholder, piece fractions g_j) and the noise rate 4 500 Hz × N₀ per piece
     are replaced. What that amendment found stands as history: without a rise, a story that starts its mark on a
     part-amplitude first piece explained it only as a fade, and a speed step from 20 to 30 WPM failed at +20 to
     +24 dB S₅₀₀ (measured in the first prototype's search; plan D10).
3. **The time step is the envelope sample, 2.667 ms, every 4th channel sample (owner, 2026-10-23: "Agreed"; plan D17,
   replacing D16's 3 ms; amended 2026-10-25;** it was the piece length, about 2 ms). It keeps the rule of at most a
   quarter of the 100 WPM dot (12 ms / 4 = 3 ms), the standard frame step, and every instant is a channel sample.
4. **Keying history (topic 1, item 7; amended 2026-10-25):** each story's own key-down and key-up times within the
   template's span, 16 samples, 42.7 ms. No running sums (until 2026-10-25: the running sum of the current mark, then
   of the current block).
5. **The bank is not used by the new decoder** (owner): each story's amplitude comes from its own envelope samples
   where its template is nonzero (amended 2026-10-25; until then "its own sums of the pieces"), and the hiss level
   from the signal detector's spectrum around the station (topic 3, items 1 and 3, amended 2026-10-15). The existing
   bank decoder stays frozen as the yardstick until the new decoder beats it.
6. **Standard parts:** one matched filter per channel and envelope detection (CW Skimmer, fldigi, Bell 1977), the
   smeared prediction (Forney 1972) (amended 2026-10-25, in place of the noncoherent matched filter of textbook
   communications theory, Proakis and Salehi, §4.5-1, which the front filter still is); segment scoring in hidden
   semi-Markov models (Rabiner 1989, §IV.D; Ostendorf, Digalakis and Kimball, 1996). Each story scores its own
   segments from its own template (since 2026-10-25; until then its own running sum, the segment model's statistic),
   and since 2026-10-15 keeps its own signal level (per-survivor processing, Raheli, Polydoros and Tzou 1995: topic
   3, item 1; the correction of 2026-10-14, which removed that citation because nothing was then estimated per
   hypothesis, no longer applies). The combination for Morse is ours; no published Morse decoder doing exactly this is
   known to us (not searched specifically).

7. **Noise (owner, 2026-10-14; amended 2026-10-15 by topic 3, item 3; amended 2026-10-25, plan D17, the noise
   figures, 2026-10-23).**
   - The hiss density N₀ comes from the signal detector's spectrum around the station, as before; it is the only
     measured noise quantity.
   - The front filter's output noise is σ_y² = N₀ × 74.48 Hz on the channel's colored noise, a fixed constant computed
     from a 48 kHz model of the channel filter and the continuous pulse (derived; the decided 25 taps give 74.50 Hz,
     0.03% apart; amendment of 2026-10-25, part 2).
   - Its correlation between samples is counted by w (part 4).
   - **The derivation kept (2026-10-14; review 1, D2), the reason for w:** treating correlated samples as
     independent overstates the evidence. On the pieces it would have understated a mark's noise variance by a factor
     of about 1.7–1.8 (2.2–2.6 dB) for marks of 8–720 ms on an ideal 300 Hz channel, and 1.9–2.1 (2.8–3.3 dB) on the
     real channel filter, whose noise bandwidth is 252 Hz (derived), making the decoder overconfident. Bell's receiver
     chain made that assumption, though Bell himself warned that his sampling interval "may render the independent
     noise assumption invalid" (Bell 1977, as read in the standard-practice check, step 2, §6.1).
   - Replaced on 2026-10-25: the noise of a sum over a time T computed from the noise per hertz (a sum of n samples
     has variance Σ_{|k|<n} (n − |k|)·R[k]), and the long-run rate 4 500 Hz × N₀ per piece of 2026-10-20.

8. **The frequency reference: the channelizer follows the detector, and the new decoder has no frequency tracker
   (owner, 2026-10-23: "B", then "A"; plan D17; amended 2026-10-25).** This replaces one tracker per channel following
   the carrier's frequency and drift rate and steering the channel's fine frequency shift (owner, 2026-10-15; review
   1, D7, O1 and K13; step 2, T4.2b), whose purpose was to keep whole-mark coherent sums within 0.2 Hz.
   - Each channel block is shifted by the detector's current frequency for the track: a phase-continuous rotation on
     the 1 500 samples/s stream, one complex multiplication per sample. The channel's bins move when the track moves by
     more than half a bin (11.7 Hz). This is the usual coarse-plus-fine layout of FFT channelizers.
   - It is decision-free: the detector decides where the station is (the owner's rule of 2026-09-29).
   - Every decoder receives the station near 0 Hz. Its error is the detector's: about 0.2 Hz steady,
     about 1.0 Hz behind a 1 Hz/s drift (derived: the detector's 1 s average lags a ramp by ḟ·τ; stage 1 measured 0.94–1.23 Hz on the detector track; corrected 2026-10-27 from 1.1 to 1.6 Hz, which were the Matched path's tracker figures), up to 47 Hz for about 0.7 s at a QSO turnover. The front filter loses 0.01 dB at 2 Hz,
     0.50 dB at 15 Hz and 2.1 dB at 30 Hz (dB relative to 0 Hz; derived).
   - The tracker, its trust tolerance, its untrusted search and its use of the detector's frequency are no longer
     decisions (amendment of 2026-10-25, part 10).
   - The bank's and Matched's own shifts by the detector's frequency become redundant; how they adapt is for the
     implementation plan.
9. **No blocks (owner, 2026-10-21: "I didn't know we were doing anything with blocks …"; 2026-10-22: the
   standard-practice direction; plan D16; amended 2026-10-25).** This replaces blocks (owner, 2026-10-15): a
   hypothesis summing its mark coherently over blocks no longer than a fixed block length, one fixed Doppler spread
   setting both the block length and how fast the level may change, its value set by measurement.
   - The coherent integration is the front filter's alone, an equivalent window of 1/B_N = 13.2 ms. That loses
     0.01 dB of energy at 5 Hz fading (derived).
   - Marks are combined noncoherently, sample by sample, at weight w_k (amendment of 2026-10-25, part 4).
   - How fast the amplitude may change is topic 3, item 2: one fixed 2 Hz.
   - The owner accepts the cost of the 76 Hz filter: slow signals pay up to 1.8 dB of noise power against a 50 Hz
     filter (10·log₁₀(76/50); derived; the plan's D16 "about 2 dB"), and a narrower filter for slow signals is in the
     backlog.
   - **Background, kept (2026-10-15): what any coherent window costs** (derived, one mark judged on its own). Short
     blocks cost a steady signal: the extra E/N₀ for 1% errors per mark (missed marks and noise read as marks,
     averaged), against one sum over the whole mark, is 0.5, 1.0, 1.5, 2.4 and 3.4 dB for 2, 3, 5, 10 and 20 blocks.
     Long blocks cost a fading signal: the longest block that loses at most 0.5 dB of energy is 78 ms at a 5 Hz
     spread, 195 ms at 2 Hz, 390 ms at 1 Hz and 1.3 s at 0.3 Hz. Example, a 5 WPM dash: blocks short enough for 5 Hz
     fading cut it into about 9, which costs a steady signal about 2.4 dB, while left whole it loses 6.9 dB at 5 Hz.
     So any one value gives up about 2 dB on steady slow signals or up to about 7 dB on fast-fading slow ones.

**For topic 9 (amended 2026-10-25):** item 8's measurement of the tracker and item 9's sweep of the spread are replaced
by the prototype's measurements (amendment of 2026-10-25, part 15), including the genie spread. Of the plan's runs
(§6.1): R6 (the fading spread with block losses: the spread is a fixed 2 Hz and there are no blocks) and R9 (the
tracker: there is none) are void under the decided design; R10 (genie fading) is part 15, item 8; R2 (the beam), R3
(the ceiling), R5 (the over and rescue rates P and ε) and R11 (the hiss estimator) still set placeholders.

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

**Amended 2026-10-25 (owner, 2026-10-23; plan D17): the decoder's step is the 2.667 ms envelope sample** (topic 4,
item 3), no longer the 2 ms piece. Where the items below still say "piece" for a step of the search, read "sample".
Every number in items 3 to 9 counted per piece or per second of pieces (for example g nats per piece, 500 newborns a
second, the 5.0 to 6.2 nats of ε charged per piece, 50 ns per hypothesis per piece), and every figure simulated with
the block observation of 2026-10-16, is for the 2 ms pieces of 2026-10-17 to 2026-10-19 and is kept as history, not
recomputed here. Items 1, 2 and 9 are restated (amendment of 2026-10-25, part 11). The Farnsworth factor is removed
from the first prototype (topic 2, item 3, amended 2026-10-21); where items 3 to 9 mention it, or count its prior, it
applies only to a later prototype.

1. **Branching: every 2.667 ms sample (amended 2026-10-25;** until then every 2 ms piece).
   - A story continues as it is, or ends its current mark or gap and branches into the possible successors (amended
     2026-10-15 by topic 2, items 2 and 3: the speed and Farnsworth steps at word gaps, up to 3 × 3 variants, are
     gone; the branching into Farnsworth factors at a hypothesis's first gap between letters is removed from the first
     prototype with the factor, topic 2, item 3, amended 2026-10-21).
   - A branch is created only where the fist model gives it a non-negligible probability (a pruning threshold; its
     value is not yet set).
   - Each sample's evidence is booked at that sample, so every story's score is complete at every sample by
     construction (each sample is booked 26.67 ms late, after the systematic term's look-ahead; amendment of
     2026-10-25, part 9).
   - The hazard form and the look-ahead for text stand, counted in samples (item 9).
   - **The over-start probability per sample:** the placeholder rate of overs, 0.05/s, gives 0.05/s × 2.667 ms =
     1.333 × 10⁻⁴ per sample of silence (8.92 nats; derived from the rate). It replaces 10⁻⁴ per 2 ms piece (item 3).
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
   keeping the better one's level (the standard per-survivor approximation), so merging is no longer exact. (The
   Farnsworth factor left the state with the factor itself, topic 2, item 3, amended 2026-10-21.)
   - **Amended 2026-10-25: merging on the state as it is, per-survivor (owner, 2026-10-23: "I agree", option A; plan
     D17).** No recent edges are added to the merge state.
     - The merge state is the plan's (§2.7, item 4, without the Farnsworth factor D14 removed), counted in samples:
       activity, key up or down, samples in the current element, speed or "open", tree node (D17: "element kind, time
       in it, speed, text node").
     - Each story computes its expected output from its own history, so every score is exact. Of two stories that
       differ only in an older edge, the better so far survives.
     - Standard: reduced-state sequence estimation with per-survivor processing (Duel-Hallen and Heegard 1989;
       Eyuboglu and Qureshi 1988), as already done for the amplitude. No stories are added.
     - Where it matters: an edge shapes the expected output for about 10 samples (26.67 ms), so above about 45 WPM two
       stories that agree on the state can still expect different outputs, because a previous element was shorter
       than that.
     - **A measured variant** (option B): the previous element's length in the state when shorter than 10 samples
       (full maximum-likelihood sequence estimation, Forney 1972), measured on the high-speed signals; its CPU and any
       decoding gain go to the owner before anything changes (amendment of 2026-10-25, part 15, item 13), under his
       CPU rule (plan D16, 2026-10-22: "all the other choices which add to the number of potential stories needs to
       be considered very carefully").
   - Stories that differ only in their amplitude, RMS sums (and the noise-in-Ω̄ variant's noise-variance sums) and ν
     sums are merged keeping the better one's (the per-survivor approximation, as before).
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
     block lengths are in milliseconds and the level's rate is per second (topic 4, item 9; since 2026-10-25 the
     template is in milliseconds and the amplitude's rate per second, topic 3, item 2), and the letter is charged at
     its start (item 9). So one newborn carries, at each piece, the best over the 32 speeds and over dot
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
       the detector's level (topic 3, item 5, as it then stood; newborns' level prior is flat since 2026-10-19, so K
       is then the flat prior's cost, below); a waiting story's level spread grows at the level model's rate
       (topic 3, item 1) and can outgrow the prior's, after which no finite K exists; and merging is approximate
       (item 2), so dropping a story can change which story survives a later merge. A sum over hypotheses (topic 8)
       would also lose the dropped stories' share, under 2 × 10⁻⁴ of the no-signal hypothesis's at S₅₀₀ 0 dB, and a
       text model with memory (topic 6) would need a text part in K. Taken up only if the idle channel's CPU shows a
       need, and then as a pruning threshold judged by search errors (item 4). Extended to gaps between elements it
       is heuristic: 17 symbols (the digits 0, 1, 3 and 9; `,` `?` `!` `'` `)` `@` `_` `$`; SK, BK, CL, HH and SOS)
       have a prefix after which their remaining elements spell no symbol.
     - **Settled with the priors at birth (below; owner, 2026-10-19): the level newborns look for in a long pause.**
       Their prior was to be centered on the detector's level (topic 3, item 5), which falls toward the hiss within
       ln R seconds of the station's last mark (2.3 s at R = 10 dB, 4.6 s at 20 dB, per bin; opening and closing
       channels), so newborns would have looked for ever weaker stations, down the table above. With a prior flat in
       dB over the whole range, newborns look for every level, so nothing falls with the pause.
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
       at the default sizes, 1 486 oracle channels; per 2.667 ms sample since 2026-10-25, P = 1.333 × 10⁻⁴, 8.92 nats,
       the same rate of 0.05/s: item 1). P = 1/10 000 per piece (9.2 nats): 1 746 overs in 10.0 hours
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
     - **The level: a prior flat in dB for every newborn (owner, 2026-10-19).** A newborn's level is equally likely
       anywhere in a set range of S₅₀₀, in dB, the form of the speed prior (flat over log-spaced values). This
       settles the three questions left open: the prior's width (the whole range), its center in a long pause (it
       has none, so nothing goes stale), and rescue newborns (they start their level again, like their speed and
       factor). It replaces topic 3, item 5's detector-centered prior.
       - **Amended 2026-10-25: integrated while broad, then Gaussian** (plan D16, "A new story's first mark is scored
         against the owner's flat prior (−15 to +85 dB S₅₀₀) and seeds the RMS"; owner, 2026-10-24: "Agree", the
         first mark kept as designed; plan D17). A newborn's first mark is scored sample by sample against the flat
         prior, by an integral over the amplitude (amendment of 2026-10-25, part 8). When the posterior is narrow
         enough (σ_u < 0.1 in ln a), or at the latest at the last sample of its first mark's template with s_k ≠ 0,
         the story continues with the Gaussian amplitude of topic 3, item 1. Its samples seed its RMS. A rescue
         newborn restarts its RMS, as it restarts its speed. The integral's approximations are measured in the
         prototype (part 15, item 2).
       - This replaces (until 2026-10-25): a newborn took its level when its first mark (or first block) ended, the
         mark scored until then against the flat prior by the predictive density of the mark's running sum, a table
         lookup per piece, and its Kalman filter started from that posterior's mean and variance.
       - **What stands:** the range (placeholder), the standard and the cost figures below.
     - **The standard.** Priors at birth are flat over a set range or measured (above). Tracking starts a newborn
       from the measurement it is born with, here its own first mark ("use the current measurement set Z_k to place
       the newborn particles in the region of the state space where" they fit: Ristic, Clark, Vo and Vo 2012), and
       draws what that measurement does not fix from a broad prior over its physical range (a velocity spread
       "usually chosen between half and the full maximum speed of possible targets": Berger, Daun and Koch 2008). At a
       change point the parameters start again ("the model parameters before and after the changepoint are
       independent": Adams and MacKay 2007; at a change of station, "re-acquire the parameters that belong to the
       sender (frequency offset, level, speed, fist and spacing) from broad priors": step 1, P7.3). Bell (1977)
       started every path at half the nominal level with a spread of ±0.32 of it, which works only with normalized
       levels.
     - **The range, S₅₀₀ −15 to +85 dB (placeholder; topic 9 measures the strongest stations on HamSCI's
       recordings).** The lower end lies below the best possible decoder's limit at 5 WPM (about −9 dB: −1.9 dB at
       25 WPM, scaled by the energy per dot; derived). The upper end covers a station at S9+40 dB over band noise at
       S3 in 2.4 kHz (about +83 dB, from the S-meter's S9 = −73 dBm on HF and 6 dB per unit; derived). The cost
       grows only as the logarithm of the range's width: 20 dB more costs ln(120/100) = 0.2 nats here (derived;
       simulated from 60 to 80 dB, 0.29 nats, which is ln(80/60)).
     - **What learning the level costs (simulated: ideal Bayesian learning on a grid, steady level, whole marks,
       25 WPM, the first letter of the PARIS mix).** Over a 60-dB range: 2.9, 3.1, 4.3 and 5.4 nats at S₅₀₀ −2, 0,
       +10 and +20 dB, against 0.9, 1.1, 2.2 and 3.4 for a prior 3 dB wide centered on the true level, about the
       same as flat when that prior is 6 dB off, and 5.8 to 8.9 nats when it is 10 dB off. The evidence with the
       level known is 60, 99, 1 045 and 10 545 nats. The 100-dB range adds 0.5 nats.
     - **What it does.** A newborn from silence learns its level from marks that bring more evidence than the level
       costs, from the first dot on (at S₅₀₀ −2 dB, near the best possible decoder's limit at 25 WPM, a dot brings
       13.1 nats and its level costs 2.7), so its starting cost stays 15.8 nats. A rescue newborn pays its level
       against the old story, which keeps what it has learned: 2.9 to 4.5 nats over the first letter (S₅₀₀ −2 to
       +20 dB, the old story's level known to 5–20%, simulated), so its starting cost becomes about 18 nats, now the
       largest (item 4). Following a ×1.3 speed change then takes 43 to 47 elements instead of 35, and ×2 takes 6 to
       7 instead of 5 (topic 2, item 2). Ville's bound on false overs holds for any proper prior (derived). The
       counts of noise stories above (what a newborn from silence leaves behind) had the level known; with it
       learned they change, which item 8's counts of quiet stretches show.
     - **Checks on the prototype (topic 9):** the elements taken to follow the speed-change group's steps (D2) and
       the first word after each turnover in the QSO groups (G2, H2), which also judge the variants not adopted
       below; the upper end of the range, from HamSCI's recordings.
     - **Not adopted for the level, each tried only with a real advantage (topic 2):** the detector's level as the
       center, with its measured error as the width and the decoder reading the detector's level over the second
       after each birth (about 2 nats less per newborn while the center is within 3 dB, and at best it is within
       ±1.75 dB from the keying sidebands alone, since the prior may not depend on the speed; it needs the
       conversion written out, its error measured, a 1 s lag and the detector's level emulated on oracle channels,
       uses the newborn's marks twice, and loses to the flat prior beyond about 6 dB of error). Rescue newborns
       that keep their parent's level (2.9 to 4.5 nats less per rescue, but a station that changes without a pause
       then costs 4.0 to 15.2 nats if it is 6 to 20 dB weaker and 6.9 to 887 nats if 6 to 20 dB stronger,
       simulated with the parent's level known to 20%, against 2.9 to 5.4 for starting again; and the change-point
       standard starts again). The detector's level from the past second (10 to 20 dB low at an over's start after
       a pause).
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
     1/1 000 per letter start, 9.2 and 6.9 nats (item 3; owner, 2026-10-19); the level's prior is flat in dB (item
     3; owner, 2026-10-19). A newborn from silence learns its level from marks that bring more evidence than the level
     costs, so its starting cost stays 15.8 nats; a rescue newborn pays its level against the old story, up to 4.5
     nats (simulated), so rescue births have the largest starting cost, about 18 nats, and the minimum is about 28
     nats (amended 2026-10-19 from 25.8, which left out the level; since 2026-10-25 P is per 2.667 ms sample,
     8.92 nats, item 1, and the Farnsworth factor's 3.14 nats is not paid in the first prototype, topic 2, item 3,
     amended 2026-10-21; the minimum is not recomputed here). (Corrected
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
   - **Evidence (amended 2026-10-25):** each envelope sample's evidence is booked at that sample, so every story's
     score is complete at every sample by construction; each sample is booked 26.67 ms late, after the systematic
     term's look-ahead (topic 4, item 2; amendment of 2026-10-25, parts 4 and 9). This replaces the current block's
     score so far, computed from its running sum at every piece (2026-10-16). What late booking cost (derived, with
     the block observation): at 20 WPM, with a dash 30 ms under way, the rival story "a dot that ended at 30 ms" had
     already been credited for those 30 ms (12.9, 147 and 1 496 nats at S₅₀₀ 0, 10 and 20 dB) less 5.3 nats for so
     short a dot, so it led the true story by 7.6, 141 and 1 490 nats, and any narrower beam dropped the true story.
     Booked on time, the true story leads by 5.3 nats.
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
   - **Cost:** each sample's score is computed once, at that sample (amended 2026-10-25; until then the block's score
     at every piece instead of once per block); the survival and look-ahead factors are table lookups. Measured with
     the rest (item 8).
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
     (constants, counted), and the level (a prior flat in dB for every newborn, which also settles the level
     newborns look for in a long pause) done 2026-10-19. Topic 5 closed 2026-10-19.
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
   accuracy topic 4's blocks need (topic 4, item 8). Done 2026-10-19 (owner): spec §5.1 (the Classical mode), §5.2
   steps 1 and 3, and §4.1 (a pointer to the decisions on opening and closing channels, below). (Superseded in the
   notes since 2026-10-25: there are no blocks and no decoder tracker, and the channelizer follows the detector;
   topic 4, items 8 and 9, and the amendment of 2026-10-25. The spec's text is not changed by these notes.)
7. **Building starts after topics 3, 4 and 5 (owner, 2026-10-15; all three closed by 2026-10-19).** Then come a
   short spec amendment and the first prototype's plan, each for the owner's approval. Topic 6's reopened items, the
   new topics on the channel's other contents and on opening and closing channels (below; added 2026-10-16), and
   topics 7, 8 and 9 follow while the prototype is built and measured, each settled before its part is added. The
   reason: the first prototype needs only simple versions of those parts (it has no text model, topic 6, item 6; it
   can print its best story at the end of each recording; fresh starts already follow a change of station), while
   it cannot do without amplitude, the observation and the search. The risk accepted is rework if a later topic
   changes something already built.
   - **The first prototype is written in C++, in the engine behind the bench (owner, 2026-10-20).**
   - **What a piece costs, timed before the plan (measured 2026-10-19):** a timing slice (scratch code, not in the
     repository) did one 2 ms piece's work for every hypothesis (continuing; ending into successors, with the
     level's update; one rescue newborn and one birth from silence; merging; pruning by the beam and the ceiling),
     on synthetic tables and noise alone, so that the ceiling binds. On a laptop (Intel i7-12700H, Windows, MSVC
     /O2, one thread) it took 61 ns per hypothesis per piece, against topic 5, item 5's estimate of 50 ns: 51.6 ms
     of CPU per channel-second at 1 700 hypotheses, 9.0 ms at 300. Python with numpy took 12 to 16 times as long. The
     slice leaves out the Farnsworth branching, the over's end and the newborns' bookkeeping, so the prototype will
     cost more; its own timing replaces these numbers (topic 5, item 8). At the slice's rate, one run over the
     development set's seed 1 (1 065 742 channel-seconds; `docs/plans/2026-10-06-development-set-results.md`,
     section 5.1) costs 2.7 to 15.3 CPU-hours; the bank decoder's run there took 9.0 CPU-hours on the Linux
     machine. (History since 2026-10-25: the piece is replaced by the 2.667 ms sample, topic 4, item 3, and the new
     decoder's costs are stated against its projected total, amendment of 2026-10-25, part 15.)

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
  little (a resumed over is born from silence at every speed, topic 5, item 3, takes its level from its own first
  mark, topic 5, item 3, as amended 2026-10-19 (the detector's level before), and its frequency from the detector's
  peak, topic 4, item 8); and the text splits between
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

### Detection, amplitude, frequency and the mark's shape (owner, 2026-10-21 to 2026-10-25; plan D15–D17)

This section is the amendment of 2026-10-25. It consolidates the owner's decisions D15 to D17 of the first
prototype's plan (`docs/plans/2026-10-20-new-decoder-prototype-1.md`, "the plan"), taken one at a time from
2026-10-21 to 2026-10-25, the last of them recorded in plan D17 by commits 484b16a to 3e8e7ea: one front filter per
channel and envelope samples every 2.667 ms, each story's predicted output (the smeared prediction), its amplitude
and long-run power, the newborn's first mark, the systematic term per sample, the channelizer following the detector,
the test generator's edges, and what the prototype must measure. The plan's D15 to D17 record each decision with the
owner's words and are the authority where this section and the plan differ. Topics 1 to 5 above are amended in place
where this section changes them and cite it as "the amendment of 2026-10-25, part n".

Parts 1 to 11 give the decided design in the order the signal passes; part 12, what is removed from today's decoder;
part 13, the known limitations and backlog items; part 14, the test generator; part 15, what the prototype must
measure; part 16, standard practice for each stage; part 17, the values.

**How values are marked:**
- *derived*: by algebra or exact computation;
- *measured*: by simulation or computation in scratch programs, which are not in the repository (the plan's D16 and
  D17 record the main results);
- *heuristic*: a choice with a reason but no derivation;
- *placeholder*: kept until a measurement replaces it;
- *owner, date*: the owner's decision, with the plan's record.

**Symbols** (each is defined again where it first appears in a formula):

| Symbol | Meaning | Unit |
|---|---|---|
| r | the channel's sample rate, 1 500 | samples/s |
| x[n] | the channelizer's complex output for the station's channel | FS |
| h[j] | the front filter's taps | 1 |
| y(t) | the front filter's complex output; e = \|y\| its envelope | FS |
| Δ | the decoder's sample interval, 4/r = 2.667 ms | s |
| t_k, e_k | the k-th sample time and envelope sample | s, FS |
| N₀ | the hiss density, from the hiss estimator | FS²/Hz |
| σ_y² | the front filter's output noise power | FS² |
| B_N | the front filter's noise bandwidth | Hz |
| w, w_k | the weight of one envelope sample's log-likelihood; w_k at sample k, from the sample's expected SNR | 1 |
| w₂ | the effective count per sample of a sum of e², 1/(1 + 2Σ\|ρ_m\|²) | 1 |
| ρ_m | the output noise's correlation between samples m apart (ρ_0 = 1; part 4); ρ alone is the fading's (part 6) | 1 |
| q | a carrier's power over the output noise, a²/σ_y² | 1 |
| s_k, v_k | a story's mark template at sample k, and its edge term (variance over the edge's position and the transmitter's rise) | 1, 1 |
| u | the offset from a true edge to the first sample at or after it | s |
| t_r | a transmitter's 10–90% rise (and fall) time | s |
| a | a story's key-down amplitude at the front filter's output | FS |
| μ, P | the mean and variance of a story's Gaussian amplitude (the plan's r̂₀ and P_m) | FS, FS² |
| Ω̂ | a story's RMS power: the ordinary-least-squares fit of its samples' e_k² − σ_y² to s_k²·Ω | FS² |
| Ω, G | a story's long-run power, and the fading's power gain over its samples (mean 1) | FS², 1 |
| ν | the number of independent fades behind Ω̂ | 1 |
| W_i, b | a story's key-down weight in bin i (Σ s_k⁴), and the bin length, 42.67 ms | 1, s |
| A, B | (variant) Ω̂'s noise variance: A from noise with noise, B·ΩG from signal with noise | FS⁴, FS² |
| SE₀, ζ, κ, λ | (variant) √A, Ω̂'s standard error on noise alone; ζ = SE₀/σ_y²; κ = B·σ_y²/A; λ = κζ = B/SE₀ | FS², 1, 1, 1 |
| χ, υ, P_r | (variant) the Ω̄ table's inputs Ω̂/SE₀ and 1/ν; the realized signal power ΩG in Ω̄'s integral | 1, 1, FS² |
| ξ | the w table's coordinate, q_k/(1 + q_k) | 1 |
| Ω̄ | the expected long-run power given Ω̂ clipped at zero, ν and the owner's flat prior (the variant adds Ω̂'s noise) | FS² |
| f_D | the fading's Doppler spread, two standard deviations of a Gaussian Doppler spectrum | Hz |
| ρ, ρ_a | the complex gain's and the envelope's correlation over a time Δt | 1 |
| C_k², f | the systematic term's carrier power and its floor | FS², 1 |
| S₅₀₀ | dB SNR: key-down carrier power over the noise power in 500 Hz | dB S₅₀₀ |

#### Part 1. The channelizer follows the detector

- **Input:** the channelizer's stream for the track, complex, in FS, at r = 1 500 samples/s, after the channel
  filter: a Blackman-windowed sinc, 21.3 ms, −6 dB at ±150 Hz (dB relative to 0 Hz), noise bandwidth 252 Hz, group
  delay 10.67 ms (`docs/signal-processing.md` §7).
- **Coarse step:** the channel is cut from the shared FFT at the bin nearest the detector's current frequency for the
  track, f_det(t). It moves when the track moves more than half a bin, 11.7 Hz (192 kHz / 8 192 = 23.4 Hz bins;
  derived).
- **Fine step:** the stream is multiplied by a phase-continuous oscillator at −(f_det(t) − f_bin), one complex
  multiplication per sample. Every story reads the same samples.
- **Response** (derived from the detector's 1 s average; plan D17):
  - a drift of 1 Hz/s is followed about 1.0 Hz behind (derived: the detector's 1 s average lags a ramp by ḟ·τ; stage 1 measured 0.94–1.23 Hz on the detector track; corrected 2026-10-27 from 1.1 to 1.6 Hz, which were the Matched path's tracker figures);
  - an equally strong answering station moves the detector's peak after about 0.7 s, during which a station 30 Hz
    off loses about 2.1 dB (dB relative to 0 Hz) in the front filter.
- **Oracle channels** (measurement only): the detector's frequency is the label's. Drifting test stations need the
  label's true, drifting frequency supplied (a test-harness change; plan D17).
- **Practice:** fldigi's decoder has no AFC; Bell's frequency was fixed; CW Skimmer states nothing
  (`docs/research/2026-10-22-cw-detection-standard-practice.md`). Coarse-plus-fine shifting is the usual FFT-channelizer layout
  (topic 4, item 8). The detector's following itself predates this decision; its open questions are in the backlog.

#### Part 2. The front filter: 76 Hz, matched to a real 100 WPM dot

- **The pulse it is matched to** (owner, 2026-10-23: "Agreed"; plan D17): a dot keyed for T = 12 ms. The rise starts at
  key-down and the fall at key-up, each a raised cosine of full length E = 2.5 ms / 0.5903 = 4.235 ms.
  - 0.5903 = (π − 2·arccos 0.8)/π is a raised cosine's 10–90% time as a fraction of its full length (derived).
  - The dot is 12 ms at half amplitude, as the generator now keys (part 14):
    p(t) = c(t)·(1 − c(t − T)), c(x) = ½ − ½cos(π·clip(x/E, 0, 1)).
- **The taps** (derived): h[j] = p(t_c + j/r)/Σ p, j = −12 … 12, with t_c = (T + E)/2 =
  8.12 ms the pulse's center. That is 25 taps at 1 500 samples/s, symmetric, unity gain at 0 Hz, so a steady carrier of
  amplitude a gives |y| = a:

  0.00011, 0.00457, 0.01459, 0.02778, 0.04096, 0.05099, 0.05545, 0.05556 (× 11), 0.05545, 0.05099, 0.04096, 0.02778,
  0.01459, 0.00457, 0.00011.

  - The matched filter of a symmetric pulse is the pulse itself.
  - Run causally, its delay is 12 samples, 8.000 ms (derived); the continuous pulse's center, t_c = 8.12 ms, is
    0.117 ms (0.044 of a sample) later.
  - One tap set serves every sample: every 2.667 ms instant is a channel sample (part 3).
- **Its noise bandwidth and output noise** (plan D17, the noise figures, 2026-10-23; the owner: "I honestly don't think
  this decision makes any difference at all", so it was handled as obvious):
  - B_N = r·Σh[j]² = 76.0 Hz on white noise (75.98 Hz continuous): the filter's own figure, the plan's "76 Hz".
  - **σ_y² = N₀ × 74.48 Hz,** the output noise power on the channel's colored noise, enters the evidence (derived,
    from a 48 kHz model of the channel filter and the continuous pulse). The decided 25 taps
    after the 4 097-tap channel filter at 192 kHz give 74.50 Hz and w = 0.1987, 0.03% apart, which is
    immaterial; the plan's figures stand. The decoder's input is the channelizer's output, whose noise is not white,
    and σ_y² is what the envelope samples actually carry. It is a fixed constant; N₀ alone is measured.
  - **w and its two limits come from the output noise's actual correlation** on the same colored noise (part 4).
- **Its power response,** dB relative to 0 Hz (derived): −0.01 at 2 Hz, −0.22 at 10 Hz, −0.50 at 15 Hz, −0.90 at 20 Hz,
  −1.42 at 25 Hz, −2.07 at 30 Hz, −3.82 at 40 Hz, −6.31 at 50 Hz, −15.5 at 70 Hz, −15.4 at 120 Hz, −24.5 at 200 Hz.
- **Against a plain 12 ms running sum (83 Hz)** (plan D17): it gains 0.17 dB of SNR on the dot (relative to the
  running sum) and suppresses neighbors more:
  −15.5 against −14.8 dB at 70 Hz, −15.4 against −13.3 dB at 120 Hz, −24.5 against −18.0 dB at 200 Hz (dB relative to
  0 Hz).
- **Its width against the ITU's necessary bandwidth** (250 to 417 Hz at 100 WPM): that is a transmitter's occupied
  bandwidth. A detector's best bandwidth is the matched filter's, about 1/T (Turin 1960), and a 250 to 417 Hz filter
  would admit 5.3 to 7.5 dB more noise power (plan D17).
- **Practice:**
  - CW Skimmer extracts each signal "through a 50 Hz filter";
  - fldigi's "matched filter" mode is 5/T_dit wide (83 Hz at 20 WPM);
  - Bell 1977 used 64 to 100 Hz (`docs/research/2026-10-22-cw-detection-standard-practice.md`).
- **[departure, owner's decision] in width from CW Skimmer's 50 Hz,** to reach 100 WPM. Slower signals pay up to
  10·log₁₀(76/50) = 1.8 dB of noise power against a 50 Hz filter, part of which post-detection summing recovers
  (derived). A narrower filter for slower signals is in the backlog.

#### Part 3. Envelope samples every 2.667 ms

- **The sample:** e_k = |y(t_k)|, t_k = t₀ + kΔ, Δ = 4/r = 2.667 ms, every 4th channel sample, 375 samples/s (owner,
  2026-10-23: "Agreed"; plan D17, replacing D16's 3 ms).
  - The owner asked "Why would we not just change our own sampling rate to be a multiple of the channel sample rate":
    3 ms is 4.5 channel samples, so every other instant fell between samples.
  - 2.667 ms keeps the rule of at most a quarter of a 100 WPM dot (12 ms / 4 = 3 ms); 3.333 ms (5 channel samples)
    would break it.
- **Exact instants:** every instant is a channel sample, so the filter is evaluated there directly with the one tap set
  of part 2.
- **The cost:** 25 complex multiply-adds per instant, 375 instants per second per channel; about 12% more computation
  than 3 ms in everything done per sample (3 / 2.667 = 1.125; plan D17).
- **Practice:** fldigi decimates its envelope to 500 samples/s (2 ms); Bell sampled every 5 ms
  (`docs/research/2026-10-22-cw-detection-standard-practice.md`).

#### Part 4. The evidence per sample

- **The statistic.** For a story whose template at sample k is s_k (part 5) and whose amplitude is a, the
  log-likelihood ratio of e_k against noise alone (derived; the Rician envelope of a carrier of unknown phase in
  complex Gaussian noise; Proakis and Salehi §4.5-1) is:

    Λ_k(a) = −b_k² + ln I₀(2 b_k z_k),   b_k = s_k a / √V′_k,   z_k = e_k / √V′_k   (nats),

  with V′_k = σ_y² + C_k²(f²s_k² + v_k) the noise plus the systematic term (part 9).
  - ln I₀ is computed as today (Abramowitz and Stegun 9.8.1 and 9.8.2).
  - Where s_k = 0, Λ_k = 0: such samples add nothing, as gap pieces did.
- **The score** (derived):

    score_k = ln ∫ exp(w_k·Λ_k(a)) N(a; μ_k|k−1, P_k|k−1) da,

  with the posterior ∝ exp(w_k·Λ_k(a)) N(a; μ_k|k−1, P_k|k−1). N(μ_k|k−1, P_k|k−1) is the story's prediction for this
  sample (part 6), and w_k the sample's weight (below).
  - The integral and the posterior's mean and variance come from today's Gauss–Hermite quadrature of order K = 5 (plan D4),
    as built (`docs/signal-processing.md`): adaptive, its nodes centered at the integrand's mode (Newton's method,
    started from the previous mode) and scaled by its curvature (Liu and Pierce 1994; the build's Ruling 11, since nodes
    spread over the prior miss a strong signal's narrow peak), and mapped onto that Gaussian truncated at a = 0 where it
    has mass below 0 (Ruling 15: dropping the nodes with a < 0 and renormalizing, the plan's first wording, missed the
    dense integral by 0.355 nats against D4's 0.01, the mapping by 0.0011 nats; measured in the prototype's Task 2),
    applied to the tempered integrand.
  - A newborn's first mark uses part 8 instead.
- **The weight w_k follows each sample's expected SNR** (owner, 2026-10-25: "Well, yes", option D; plan D17), with the
  fixed 0.1986 a measured variant (part 15, item 18).
  - **The correlation it counts:** the output noise's correlation on the channel's colored noise at 2.667, 5.333,
    8.000, 10.67, 13.33 and 16.00 ms is 0.878, 0.626, 0.367, 0.138, 0.014 and −0.005, then 0 (derived;
    the same to 0.001 with the decided 25 taps). These ρ_m (ρ_0 = 1)
    also enter Ω̂'s noise variance in the noise-in-Ω̄ variant (part 7).
  - **Its two limits** (derived): 1/(1 + 2Σ_m ρ_m) is the effective count per sample for a statistic
    linear in the noise, and Λ_k is linear in e_k at high SNR (ln I₀(x) ≈ x): w = 0.1986. At low SNR Λ_k is quadratic
    (ln I₀(x) ≈ x²/4, so Λ_k ∝ e_k² − V′_k), and the e² of complex Gaussian noise correlate as |ρ_m|²: w₂ =
    1/(1 + 2Σ_m|ρ_m|²) = 0.2754.
    - Check of the strong limit: 0.1986 equals Δ·σ_y²/N₀ = 2.667 ms × 74.48 Hz. By Poisson summation, the sum of the
      autocovariance at multiples of Δ is N₀/Δ, because the channel filter leaves no noise at multiples of 1/Δ =
      375 Hz. The white-noise figure Δ·B_N = 0.2026 differs by 2%. It replaces 0.2235, the figure for
      3 ms samples (plan D17).
  - **Between the limits** (measured in scratch; a steady carrier at full template in noise with the decided chain's
    correlation, 2²² samples, two seeds): the weight that makes w·ΣΛ carry the samples' true information about a,
    w_eff = Var(ψ)/(Var(Σψ)/n) with ψ_k = ∂Λ_k/∂a, is

    | q = a²/σ_y² per sample | −20 dB | −10 dB | −5 dB | 0 dB | +3 dB | +6 dB | +10 dB | +15 dB | +20 dB | +30 dB |
    |---|---|---|---|---|---|---|---|---|---|---|
    | S₅₀₀ at s = 1 (q − 8.27 dB) | −28.3 | −18.3 | −13.3 | −8.3 | −5.3 | −2.3 | +1.7 | +6.7 | +11.7 | +21.7 |
    | w_eff | 0.273 | 0.259 | 0.240 | 0.217 | 0.208 | 0.203 | 0.200 | 0.199 | 0.1987 | 0.1986 |

    (q in dB SNR in the filter's 74.48 Hz; seed-to-seed spread about ±0.0006.) The ratio for Λ_k's own scatter is the
    same to 0.0007. Extended from q = −40 to +40 dB (two new seeds, all 11 lags; seed-to-seed difference up to 0.0016),
    w is within 1% of the weak limit below q = −18.5 dB and within 1% of the strong limit above q = +8.75 dB (q in dB
    SNR in the filter's 74.48 Hz throughout). The fixed 0.1986 undercounts evidence by 9% at −8 dB S₅₀₀ (roughly 0.2 to
    0.4 dB of signal, a rough estimate; plan D17).
  - **The rule** (plan D17): each story looks w_k up per sample with s_k ≠ 0 (where Λ_k ≠ 0) from that curve, at the
    sample's expected SNR

    q_k = (μ_k|k−1)²·s_k²/σ_y²,

    μ_k|k−1 the story's predicted amplitude (part 6); space samples have Λ_k = 0 and need no weight. The newborn
    reads the same curve at each amplitude a of its integral, q = a²s_k²/σ_y² (owner, 2026-10-26: "a"; plan D21;
    part 8).
    - **Standard:** the composite-likelihood magnitude adjustment evaluated at the parameter (Chandler and Bate 2007;
      Varin, Reid and Firth 2011).
    - **Approximate on edges,** where neighboring samples differ in strength: the curve is for neighbors of equal
      strength (plan D17).
    - **Rejected** (plan D17): 0.275 fixed (overcounts strong signals by 38%); 0.217 fixed at "the threshold" (a
      made-up operating point, 9% overcount); the exact joint likelihood (a redesign).
    - **The fixed 0.1986, a measured variant:** the owner doubts the curve changes decoding ("it's not clear to me
      that it will have any impact in real life at all except on the extra CPU cost"); the prototype scores the fixed
      0.1986 beside it on the weak-signal groups, and if the curve shows no measurable gain the decoder goes back to
      the fixed value (part 15, item 18).
  - **The table** (its node values computed ahead of time and compiled in: the curve is a Monte Carlo result, 2²²
    samples and two seeds per point, too slow to rebuild at startup):
    - **Input and range:** q_k from −40 to +40 dB SNR in the filter's 74.48 Hz, clamped outside to the limits, 0.2754
      below and 0.1986 above (the curve is within 0.0002 of them at the ends).
    - **Grid** (heuristic, its adequacy measured against the curve, whose two seeds share their noise across q, so its
      shape is smooth below the Monte Carlo scatter): 129 nodes uniform in ξ = q_k/(1 + q_k) on [0, 1], the end nodes
      at the derived limits, linear interpolation: |error| at most 0.0002 (0.1% of w), set by the end node against the
      curve's Monte Carlo value at −40 dB; 65 nodes do as well. A grid every 1 dB of q (81 nodes) errs by at most
      0.00007 but needs a logarithm per lookup.
    - **Cost per lookup** (rough estimate, not timed): q_k from μ² (2 multiplications), ξ = q_k/(1 + q_k) (an
      addition and a division), an index and one interpolation: about 5 to 10 ns; at the 1 700-story ceiling (637 500
      story-samples per channel-second), on the samples with s_k ≠ 0, about 2 to 6 ms per channel-second (plan D17,
      4896e97; rough).
  - **Which numbers derived from w change** (derived; values below and in parts 8 and 9):
    - **The caps** become w_k·s_k²/(f²s_k² + v_k) (part 9): where a cap can bind, the story predicts a strong
      sample, and w_k is at most 0.2057 at j = 5, 0.1998 at j = 6 and the strong limit within 0.2% from j = 7 on and
      on the plateau, so the stated caps hold within 4% at j = 5 and 0.6% elsewhere; the guaranteed bound for a story
      predicting a weak signal is at most 0.2754/0.1986 = 1.387 times them.
    - **The newborn's switch timing** (part 8): unchanged at 0 to +30 dB S₅₀₀.
    - **The flat-part cap w/f²** (part 9): 497 nats where it binds (the sample's SNR above 1/f² = +34.0 dB SNR in
      the filter's 74.48 Hz, where w_k is the strong limit), unchanged.
    - **Ω̂'s noise variance** (the noise-in-Ω̄ variant, part 7) does not use w_k: it is computed from the
      correlation ρ_m directly.
  - w_k tempers the likelihood inside the integral. V′ is not divided by w: that would cut the low-SNR evidence by a
    further factor w (measured in scratch, with 3 ms samples and w = 0.2235: a 25 WPM dot at −5 dB S₅₀₀ scored −2.6
    against +3.1 nats).
- **Practice:** decimating to about the filter's bandwidth, or weighting each log-likelihood accordingly, is the
  standard treatment of correlated samples (Kay; acoustic scaling in speech recognition, Gales and Young).
  - Bell 1977 treated his correlated 5 ms samples as independent, which over-counts. Not copying that is a correction,
    not a departure.
- **The evidence of a mark** is the sum of its samples' scores (standard post-detection integration). Every sample's
  score is booked at that sample, so scores are complete at every sample (topic 5, item 9).

#### Part 5. The mark template: the smeared prediction

- **The decision** (owner, 2026-10-23: "I want option B, but we need to discuss the exact shape"; then "OK agreed";
  plan D17): each story predicts the front filter's actual output for its claimed keying, not a square on/off shape.
  A square shape would misread every mark's timing by several milliseconds.
- **The shape:** a carrier keyed with 2.5 ms (10–90%) raised-cosine edges, the rise starting at the claimed key-down
  and the fall at the claimed key-up, passed through:
  - the channelizer's filter. It is included because it is known exactly and costs nothing in a precomputed table,
    though it changes the shape by at most 0.6% of full at 25 WPM and 1.2% at 100 WPM (plan D17);
  - then the front filter.
- **The step response S(τ),** τ the time after key-down, through the 4 097-tap channel filter and the 25 decided taps,
  both causal (derived):
  - half amplitude at 20.8 ms after key-down: 2.12 ms (the edge's half point), plus 10.67 ms (the channelizer), plus
    8.00 ms (the front filter's 12 taps of delay); 20.79 ms computed;
  - 10–90% rise 10.03 ms through the channelizer and the front filter, 9.9 ms through the front filter alone (plan
    D17, corrected 2026-10-24 and 2026-10-25);
  - 1–99% rise 14.73 ms (plan D17, corrected 2026-10-25); 0.1–99.9% rise 16.1 ms on the signed response. The plan's
    19.04 ms for the 0.1–99.9% rise (the look-ahead bullet) is measured on |y| from its first crossing of 0.001 of full,
    which falls on the channel filter's negative precursor ahead of the main rise (|y| first reaches 0.001 of full 9.8
    ms after key-down; derived); it is for reference only.
- **The template of a story** with key-down at t_d and key-up at t_u, and of any earlier marks within the span:
  s_k = Σ_marks [S(t_k − t_d) − S(t_k − t_u)].
  - Its superposition of a fall and the next rise assumes the carrier's phase continuous across the key-up. That
    matters only when an element gap is shorter than the response, above about 67 WPM (derived).
- **Averaged over the edge's position** (derived; owner's shape, "averaged over where an edge falls within a sample"):
  a story claims its edges on the 2.667 ms grid. The true edge lies anywhere within the sample interval.
  - **The convention** (owner, 2026-10-25: "Approved"; rejected the nearest sample, u on [−Δ/2, +Δ/2)): sample j = 0 is
    the first sample at or after the key-down, so the offset u from the key-down to that sample is uniform on [0, Δ).
    s_j is S(jΔ + u) averaged over u. Reason: it makes the claimed edge sample a definite function of the true edge, so
    the template and the timing reference (part 11) agree. Which sample is labeled j = 0 changes the memory's and
    look-ahead's derived counts by one sample (part 9).
  - The values below are for the decided chain, the 4 097-tap channel filter and the 25 taps, both causal, averaged
    over all 512 offsets u of the 192 kHz grid within [0, Δ) (derived). Values computed
    with the continuous pulse's 8.12 ms delay instead lie 0.117 ms late (up to 0.0095 of full lower, at j = 7).
  - With j counted that way:

| j | 5 | 6 | 7 | 8 | 9 | 10 | 11 | 12 | 13 | 14 on |
|---|---|---|---|---|---|---|---|---|---|---|
| s_j | 0.0505 | 0.2126 | 0.4334 | 0.6590 | 0.8661 | 0.9825 | 1.0023 | 0.9999 | 0.9999 | 1.0000 |
| v_j (edge term: over u and t_r) | 1.0 × 10⁻³ | 3.6 × 10⁻³ | 4.2 × 10⁻³ | 4.2 × 10⁻³ | 2.6 × 10⁻³ | 3.4 × 10⁻⁴ | 2.7 × 10⁻⁶ | 1.4 × 10⁻⁷ | 2.9 × 10⁻⁹ | < 3 × 10⁻¹¹ |
| variance over u alone (for reference) | 9.5 × 10⁻⁴ | 3.6 × 10⁻³ | 4.2 × 10⁻³ | 4.2 × 10⁻³ | 2.6 × 10⁻³ | 2.7 × 10⁻⁴ | 6.4 × 10⁻⁷ | 6.5 × 10⁻⁸ | 3.2 × 10⁻⁹ | < 3 × 10⁻¹² |

  - Samples j = 0 to 4 have |s| < 0.001 (the delay): s = 0.00000, 0.00001, 0.00016, −0.00064 and −0.00080, the
    channel filter's precursor; v_j there is 8 × 10⁻¹⁴, 3.1 × 10⁻¹⁰, 7.8 × 10⁻⁹, 5.8 × 10⁻⁷ and 2.8 × 10⁻⁵.
  - **The values far below the table's precision matter at the prior's top:** at +85 dB S₅₀₀ the carrier is
    q = a²/σ_y² = 2.1 × 10⁹ times the noise, so the leeway C²v_j reaches the noise wherever v_j ≥ 5 × 10⁻¹⁰. The
    memory's and look-ahead's derived counts there (part 9) rest on the template's ringing at about 10⁻⁴ of full
    in amplitude (j = 1 or 2 on the rise, j = 12 or 13 after key-up). The owner's 10 samples are not affected.
  - The fall is the mirror: after a key-up the template stays at or above 0.01 of full through j = 10, 11 samples, and
    nonzero through j = 15 (16 samples); exactly 0 from j = 16.
  - **Dot peaks** (derived): a 100 WPM dot peaks at 0.900 of a steady carrier, an 80
    WPM dot at 0.984, and slower dots at 1.00. The template carries this.
- **Other transmitters' edges go into the edge term v_k** (owner, 2026-10-23: "Agree", option B; plan D17).
  - **The definition:** v_k is the average over u and t_r of (s_k(u, t_r) − s̄_k)², with u uniform within the
    2.667 ms sample and t_r, the transmitter's 10–90% rise time, uniform over 0.8 to 4.7 ms (the ARRL Lab's measured
    range, `docs/research/2026-10-23-keying-envelopes.md`; uniform is the maximum-entropy choice given only the range). Each rise's
    edge is aligned to the template at its half-amplitude point, the story absorbing the delay in its timing. s̄_k
    stays the 2.5 ms template (the table's s_j).
  - Zero on the flat part and in gaps; one precomputed number per template sample; no CPU per story, no stories added.
  - **Its values** (derived, 79 rise times over the range): the table's v_j row. The rise-time spread
    adds little to the edge-position spread: after alignment the rises differ from the template by about 1% of full
    or less on most samples.
  - **Misfit after alignment,** the largest |difference| from the 2.5 ms template through the channelizer and the
    front filter (derived, 48 ms mark and 12 ms dot, each rise aligned at its half-amplitude point on the decided
    chain): 0.78–1.56% of full at 0.8 ms, 0.55–1.10% at 1.5 ms, 0.77–1.54% at 3.5 ms, 1.92–3.83% at 4.7 ms. The plan
    records the 0.8 ms figure as "corrected 2026-10-25 from 0.9–1.6%, which took the best shift on a coarse 41.7 µs grid
    instead of aligning at half amplitude" (plan D17). VE3NEA's 2 ms full edge (1.18 ms 10–90%): 0.67–1.35%.
    The plan records 1.92–3.83% at 4.7 ms, "corrected 2026-10-24 by the amendment's revision: the first figure,
    3.8–4.6%, searched shifts of only ±1.5 ms, while half-amplitude alignment at 4.7 ms needs 1.86 ms" (plan D17).
    Both curves pass through the same filters, so the 25 taps' delay does not change these figures.
  - Unshaped keying (about 5.3% of full) stays outside v_k, with key clicks a known limitation (part 13).
  - Chosen over the floor alone because f is now a placeholder that may go to zero (part 9). An earlier figure
    of 6–12.5% for "8 ms rises" used a 13.5 ms full edge outside the measured range, without the alignment, and is
    withdrawn (plan D17).
- **The timing convention** (owner, 2026-10-23): edges start at key-down and key-up, so a mark keeps its keyed length at
  half amplitude. Every keying shaper whose design could be read does this (QRP Labs AN005, KE3HO, Morse Runner, DeepCW;
  `docs/research/2026-10-23-keying-envelopes.md`). Real transmit paths add a mark bias of −7 to +4.7 ms; the fist absorbs it, and
  the prototype tests it (part 15).
- **Practice:**
  - **[departure] from Bell's** measurement, which is 0 or 1 (H = x_k);
  - **standard** in sequence detection over a smearing channel (MLSE with intersymbol interference, Forney 1972).
  - **The reason:** scoring the filter's tail as noise would end every mark about half the response late, a timing
    bias of several ms on every mark, and would leave the tail for an invented over to claim.

#### Part 6. The amplitude per story: Bell's form, the owner's rule for its change

- **The form:** each story carries one Gaussian amplitude N(μ, P), in FS at the front filter's output, and the time t_m
  of its last update [practice: Bell 1977, one scalar Kalman filter per path; per-survivor processing, Raheli et al.
  1995].
- **The prediction** to sample k (owner, 2026-10-22: "It should be set by the long run amplitude but it depends heavily
  on how long since the last estimate of the story's amplitude was made"; plan D16; the prediction in full, owner,
  2026-10-23: "agreed"; plan D17), with Δt = t_k − t_m:

    ρ = exp(−π² f_D² Δt²/2),   f_D = 2 Hz (owner, 2026-10-22),
    ρ_a = (π/4)·(₂F₁(−½, −½; 1; ρ²) − 1)/(1 − π/4),
    ā = (√π/2)·√Ω̄ = 0.886·√Ω̄,
    μ_k|k−1 = ρ_a·μ + (1 − ρ_a)·ā,
    P_k|k−1 = ρ_a²·P + ½(1 − ρ²)·Ω̄.

  - **ρ** is the correlation of the complex gain of Rayleigh fading with a Gaussian Doppler spectrum of spread f_D
    (derived).
  - **ρ_a** is the amplitude's own correlation for the same ρ (derived; a table over Δt, since f_D is fixed).
  - **ā** is the Rayleigh mean amplitude for the long-run power Ω̄ (part 7).
  - **The owner's rule** is the variance's second term, unchanged. The whole is form (iv) of the derivation of
    2026-10-22 (plan D16, D17), within 0.2 nats of the exact predictive (measured in scratch; plan D17).
  - **The values at 2 Hz** (derived):

| Δt | ρ | ρ_a | spread factor √((1 − ρ²)/2) |
|---|---|---|---|
| 2.667 ms (1 sample) | 0.99986 | 0.99967 | 0.012 |
| 48 ms (18 samples) | 0.9555 | 0.902 | 0.208 |
| 144 ms (54 samples) | 0.664 | 0.416 | 0.529 |
| 336 ms (126 samples) | 0.108 | 0.011 | 0.703 |

  - **Inside a mark** every sample is an estimate, so Δt = 2.667 ms. Across a gap, Δt runs from the mark's last
    updated sample.
  - **Its properties** (derived):
    - ½(1 − ρ²)Ω̄ is the Rician conditional variance near the story's level, exact for short gaps;
    - as ρ → 0 it tends to Ω̄/2, 2.33 times the Rayleigh envelope's variance (1 − π/4)Ω̄: under-confident, the safe
      side, at long gaps;
    - the rough formula's third term, (1 − ρ)²(π/16)Ω̄/ν, is dropped as negligible (within 0.2 nats, measured; plan
      D16).
- **The update** at each sample with s_k ≠ 0: the posterior of part 4, moment-matched to N(μ, P); t_m = t_k.
  Samples with s_k = 0 do not update. Λ_k is even in s_k, so the slightly negative template values at j = 3 and 4
  are well defined.
- **The order at each sample** (plan D16): predict from the estimates so far (μ, P, Ω̄), score, then update the
  amplitude and, if s_k ≠ 0, the RMS and ν sums (and the variant's noise-variance sums; part 7).
- **Practice and departures:**
  - Bell's scalar amplitude, per path, with a larger spread after a long space: practice.
  - **[departure, owner's decision] in scale:** the spread is set by the long-run amplitude, not by the story's current
    amplitude or a receiver AGC's units. Today's decoder scaled it by the current amplitude, the cause of the fading
    failure (plan D16).
  - **[departure in value, owner's decision]:** 2 Hz against Bell's 1 Hz.
  - **A measured variant:** Bell's decay toward zero across spaces, against the decided drift toward 0.886·√Ω̄ (the
    owner's question of 2026-10-22; recorded in the plan 2026-10-24, D17, 6a13f77; part 15, item 16).
- **Known limitation** (owner, 2026-10-22: "I think we record that as a known limitation and backlog it"; plan D16):
  inside marks under fast fading the one-number model is over-confident, even with the true level and rate. A
  Gaussian-spectrum gain is smooth. Over n unobserved steps, a chain of n first-order steps understates its drift
  variance by the factor n (derived). Inside a mark every step is observed, so what applies
  there is the measured effect below (both measured with 3 ms samples; not remeasured at 2.667 ms).
  - About three times too many 1% surprises at 2 Hz, 25 WPM, E[z²] ≈ 1.6 in marks (measured).
  - 5 Hz fading predicted at the fixed 2 Hz loses 1.15 to 1.69 nats per mark sample, +20 and +30 dB S₅₀₀ (measured in
    scratch).

#### Part 7. The long-run amplitude: each story's RMS, and Ω̄

- **The decision** (owner, 2026-10-22: "adopt it", the backup since dropped; plan D16): each story's RMS of its own
  samples since its over began, as CW Skimmer measures strength from key-on samples (VE3NEA, quoted by N4ZR on the
  RBN blog, 2014-03-05: "key-off and transition samples are discarded"; `docs/research/2026-10-22-cw-detection-standard-practice.md`).
- **The RMS power: ordinary least squares over every sample** (owner, 2026-10-23: "B", every sample where the story
  expects signal, each counting in proportion to the square of its template value; the formula corrected by the
  owner, 2026-10-25: "OK Option 3"; plan D17):

    Ω̂ = Σ_k s_k²(e_k² − σ_y²) / Σ_k s_k⁴,

  over every sample since the story's over began, the fall's tail included. A sample with s_k = 0 adds nothing to
  either sum, so no threshold is needed.
  - **One sample set** (owner, 2026-10-25: "Yes"; rejected s_k > 0 and a threshold on |s_k|): every per-story sum and
    update runs over the samples with s_k ≠ 0: Ω̂, ν's bins, the variant's noise-variance sums, Ω̄'s lookups, w_k, the
    amplitude update and C²'s own part. "s_k > 0" and "every sample of the story's marks" differ from it only at j = 3
    and 4, where s = −0.00064 and −0.00080.
  - **It is the least-squares fit** of e_k² − σ_y² = s_k²·Ω, each sample's e_k² counted in proportion to s_k², the
    owner's 2026-10-23 wording (derived). It is unbiased: E[e_k²] = s_k²a² + σ_y² on a steady carrier, so E[Ω̂] = a²,
    and under fading E[Ω̂] = Ω (derived).
  - **The correction** (plan D17, 2026-10-25): the formula recorded with the decision of 2026-10-23,
    Σ(e_k² − σ_y²)/Σs_k², weights each sample's own estimate (e_k² − σ_y²)/s_k² by s_k², so every sample's noise
    counts in full however small its template; the plan mislabeled it "the least-squares
    estimate". The question it raised, which samples count (every s_k > 0, or a cut at |s_k| ≥ 0.01), is gone: a tiny
    template value weighs almost nothing (s_k² in the numerator, s_k⁴ in the denominator).
  - **Why ordinary least squares** (plan D17): at low SNR every e_k² has the same noise variance, σ_y⁴, so ordinary
    least squares is optimal there for uncorrelated samples (derived), and low SNR is the only place Ω̂'s precision
    affects decoding. With the filter's correlated noise (steady carrier, one mark; variance
    relative to the best of four estimators; measured): on noise alone, the 2026-10-23 formula 3.60, 1.61 and 1.16 (a
    100 WPM dot, a 25 WPM dot, a 25 WPM dash), the 0.01 cut 2.04, 1.34 and 1.08, ordinary least squares 1.00, 1.00 and
    1.00; at −8 dB S₅₀₀, 2.10, 1.14 and 1.04 against 0.95, 1.01 and 1.00.
  - **Rejected** (plan D17): the 0.01 cut (a made-up threshold, still twice the variance at 100 WPM); weighting by each
    sample's variance σ_y⁴ + 2s_k²Ωσ_y² (at most 2% better, at +10 to +20 dB S₅₀₀, where the noise is under 0.1% of
    Ω̂'s variance beside the fading's; 8% worse on a 100 WPM dot, since that variance leaves out the neighbors'
    correlation and the edge position's scatter; it needs Ω in its own weights and a division per sample per story).
  - **Its cost:** two running sums per story, as before.
  - **Its physical floor** (derived): e_k² ≥ 0, so Ω̂ ≥ −σ_y²·Σs_k²/Σs_k⁴: −2.09 σ_y² after a 4-sample mark, −1.30 σ_y²
    after "PARIS" at 100 WPM and −1.05 σ_y² after "PARIS" at 25 WPM.
  - **[departure, owner's decision, for a derived reason] from CW Skimmer's practice:** on a mark's flat part (s = 1)
    it is Skimmer's average of key-on samples; the edge samples count less instead of being discarded. At 100 WPM a
    dot's output peaks at 0.90 of full and has no flat part, so discarding edges would leave dots at high speed, and an
    all-dot word, with no estimate (plan D17; kept on 2026-10-25).
- **Ω̂'s noise variance, for the noise-in-Ω̄ variant** (derived; the plan's
  follow-on of 2026-10-25, "the noise's standard error with weights s_k²"). The default (clipping) does not compute
  it. Write each sample as x_k = s_k α_k + n_k, with α_k the story's complex
  carrier at the front filter's output (E|α_k|² = Ω under fading) and n_k the complex Gaussian noise (E|n_k|² = σ_y²,
  E[n_j n_l*] = σ_y² ρ_{j−l}, ρ_m the noise correlation of part 4, ρ_0 = 1). Then

    e_k² = s_k²|α_k|² + 2 s_k Re(α_k* n_k) + |n_k|²,   so   Ω̂ = Ω·G + N,

  with Ω·G = Σ s_k⁴|α_k|²/Σ s_k⁴ the fading's part (G the fading's power gain over the story's samples, mean 1) and
  N = [Σ 2 s_k³ Re(α_k* n_k) + Σ s_k²(|n_k|² − σ_y²)]/Σ s_k⁴ the noise's part.
  - **Noise with noise:** variance σ_y⁴·a₂/(Σs_k⁴)², a₂ = Σ_j Σ_l s_j² s_l² ρ_{j−l}², since Cov(|n_j|², |n_l|²) =
    |E n_j n_l*|² for circular complex Gaussian noise (Isserlis' theorem; derived). On contiguous samples with s = 1
    this is σ_y⁴/(w₂·n), n the count, as first stated for the 2026-10-23 formula (plan D17).
  - **Signal with noise:** given the fading, variance 2σ_y²·Σ_j Σ_l s_j³ s_l³ ρ_{j−l} Re(α_j* α_l)/(Σs_k⁴)² (derived).
    The two parts are uncorrelated (an odd moment of the noise).
  - **How fading enters** (derived): the noise's correlation spans 6 samples (16.0 ms; ρ_7 and beyond are below 3 ×
    10⁻⁴), over which the fading's own correlation is at least 0.9950 at 2 Hz (0.969 at 5 Hz; at 16.0 ms; plan D17,
    4896e97), so Re(α_j* α_l) is the local power |α|². With the local power replaced by the story's realized power ΩG,

    Var(N | Ω, G) = A + B·ΩG,   A = σ_y⁴·a₂/(Σs_k⁴)²,   B = 2σ_y²·b₂/(Σs_k⁴)²,   b₂ = Σ_j Σ_l s_j³ s_l³ ρ_{j−l}.

    The signal-with-noise part follows the realized power ΩG, not the long-run Ω: a story in a fade carries less of
    it. This replaces the plan's approximation, [σ_y⁴·n/0.275 + 2σ_y²·Ω·Σs_k²/0.1986]/(Σs_k²)², which was for the
    2026-10-23 formula, used Ω, and was marked "to be checked before the amendment states it".
  - **Checked by simulation** (the decided chain's correlation, 60 000 trials per case; a 100 WPM dot claimed as 4
    samples, a 25 WPM dot and dash, and "PARIS" at 100, 25 and 12 WPM; steady and with 2 Hz and 5 Hz Rayleigh fading;
    noise alone and −10 to +10 dB S₅₀₀; measured): E[N²]/(A + B·Ω) lies between 0.984 and 1.010, and
    E[N²/(A + B·ΩG)] between 0.981 and 1.010. The exact conditional variance of the signal-with-noise part against
    B·ΩG: mean ratio 0.983 to 0.999; 5–95% range within 3% on single marks at 2 Hz and on "PARIS" at 25 and 12 WPM,
    0.92 to 1.01 on a 25 WPM dot at 5 Hz, and 0.84 to 1.07 on "PARIS" at 100 WPM, where the fading changes within
    the word.
  - **N is not Gaussian on the first marks** (the owner's model treats it as Gaussian, "an approximation over a story's
    first few samples", plan D17): its skewness on noise alone is +1.8 after one 4-sample mark, +1.1 after a 25 WPM
    dot and +0.2 after "PARIS" at 25 WPM, and it falls with the SNR (+0.3 at +10 dB S₅₀₀ on the 4-sample mark)
    (measured in scratch).
  - **How the decoder computes it: two exact running sums per story** over the 6 lags (truncating at 6 lags changes
    a₂ and b₂ by under 0.01% on the cases above; measured): at each sample with s_k ≠ 0,

    a₂ += s_k²·(s_k² + 2Σ_{m=1…6} ρ_m² s_{k−m}²),   b₂ += s_k³·(s_k³ + 2Σ_{m=1…6} ρ_m s_{k−m}³),

    with s_{k−m} the story's own template at its previous 6 samples (final once booked, since both filters are
    causal; 0 before the over began). A and B then use the Σs_k⁴ that Ω̂ already sums.
    - **Cost** (rough count, not timed): about 16 multiply-adds, some 3 to 5 ns, per story per sample with s_k ≠ 0;
      on the share of samples with s_k ≠ 0 (given with Ω̄'s table below) roughly 1 to 3 ms per channel-second at
      the 1 700-story ceiling, and two numbers per story, in the variant only.
    - **The one-sum approximation** a₂ ≈ Σs_k⁴/w₂, b₂ ≈ Σs_k⁶/w (w = 0.1986) errs by +24% and +58% on a 4-sample mark,
      +8% and +15% on "PARIS" at 100 WPM and +2% and +3% at 25 WPM (measured in scratch): too far on the first marks
      of fast signals, where the noise matters most. The exact sums are the owner's decision (2026-10-25: "A"; plan
      D17); reason: their cost is small and the approximation's error is largest where the variant differs from
      the default.
- **ν, the number of independent fades behind Ω̂: binned, exact on the bins** (owner, 2026-10-23: "Agreed"; his idea:
  "What if we just did a very long-term average and decimated..."; plan D17).
  - Each story sums its key-down weight in bins of b = 42.67 ms (16 samples, two engine blocks): W_i = Σ s_k⁴ over
    the samples of bin i (owner, 2026-10-25, with the RMS by ordinary least squares; plan D17: "the bin weights become
    w_i = Σ s_k⁴ over the bin's samples"). Then (derived: Satterthwaite's 1946 moment match):

    ν = (Σ_i W_i)² / Σ_i Σ_j W_i W_j ρ²(t_i − t_j),   ρ² = exp(−π² f_D² τ²) at the rule's 2 Hz,

    with ρ here the fading's correlation (part 6) and t_i each bin's center, the time the binning-error formula
    assumes (stated 2026-10-24 as obvious; plan D17).
  - **The weights, re-derived for ordinary least squares:** Ω̂'s fading part is ΩG with G = Σ s_k⁴|g_k|²/Σ s_k⁴ (g_k
    the complex fading gain, E|g_k|² = 1). For a complex Gaussian gain Cov(|g_j|², |g_l|²) = ρ²(t_j − t_l), so
    Var(G) = ΣΣ s_j⁴s_l⁴ρ²/(Σ s⁴)²; matching it to the variance of a Gamma of shape ν and mean 1, 1/ν, gives
    ν = (Σ s_k⁴)²/ΣΣ s_j⁴s_l⁴ρ²(t_j − t_l) on the samples, and the binned form above (derived). With every s = 1 and no
    binning it is n²/ΣΣρ², the plan's count on key-down sample times (D16). The 2026-10-23 weights s_k² matched the
    formula of that date, now corrected.
  - **Binning:** the estimate (π f_D b)²/12 = 0.6% at 2 Hz (derived for weight spread evenly within each bin) gives the
    scale, not a bound. Measured against the exact sample sum with weights s_k⁴ ("PARIS" once, twice and four times):
    +0.30 to +0.32% at 8 WPM, +0.03 to +0.11% at 25 WPM, and −0.15 to +2.30% at 100 WPM, the largest on a single "PARIS"
    (0.54 s, ν = 1.93). With the old weights s_k², +0.05 to +1.71% on the same texts. Both stay within the Satterthwaite
    form's own ~3%. Keeping only the last 10 bins changes ν by under 0.01% more.
  - **Each story keeps its last 10 bins:** ρ² < 0.001 beyond 0.42 s, so older pairs are dropped. The double sum is
    kept as a running total: each closed bin adds its own term and its products with the last 10.
  - **Its cost** (rough counts; plan D17): about 1.6 operations per story per sample (1 addition each sample, 10
    multiply-adds per closed bin) and about 12 numbers per story. Option C, an exponential-sum fit, would take about 10
    operations per sample, 4 numbers, and an unchecked fit error.
  - Measured against the generator's fading, ν matches the empirical number of looks within 3% at 0.1 to 5 Hz true
    rates, when counted at the true rate. Counted on elapsed time it over-counts: 10% at 25 WPM and up to 46% at 8 WPM
    (plan D16, "Findings of that derivation").
  - At 25 WPM, at 2 Hz: ν = 2.10 after 0.5 s and 6.72 after 2 s (derived, on sample times with every s = 1, in
    scratch computations of 2026-10-22 not in the repository). With the decided template and weights s_k⁴, "PARIS" at
    25 WPM (1.94 s) gives ν = 6.55 (6.63 with weights s_k²; measured).
  - At other true rates the fixed 2 Hz count is off: 1.07 true looks against 6.72 at 0.1 Hz after 2 s, and 13.9 at
    5 Hz (measured in scratch computations of 2026-10-22 not in the repository).
- **Ω̄, the expected long-run power: by default from the RMS clipped at zero, through the fading-only closed form**
  (owner, 2026-10-22: "Isn't omega bar already a well defined number?", plan D16; the default, owner, 2026-10-25: "We
  clip as the default, and we test option 7's improvement in decoding and in speed against that default", plan D17,
  f507812, reversing the same day's default of the noise in Ω̄'s likelihood after the corrected costs and that table's
  growth to 12.8 MB; that model is the measured variant below).
  - **Why clipping:** on a buried signal Ω̂ scatters about zero and goes negative often, and the fading-only model of
    D16, νΩ̂/Ω ~ Gamma(ν), gives no answer for Ω̂ ≤ 0 (plan D17). The default replaces Ω̂ by Ω̂₊ = max(Ω̂, 0), the
    standard point estimate of a variance component (Searle, Casella and McCulloch 1992). It keeps no noise-variance
    sums.
  - **The model:** νΩ̂₊/Ω ~ Gamma(ν), with the owner's flat-in-dB prior p(Ω) ∝ 1/Ω on [Ω_lo, Ω_hi], −15 to +85 dB S₅₀₀
    (Ω = 10^(L/10)·N₀·500 Hz at L dB S₅₀₀, so 0.212 σ_y² to 2.12 × 10⁹ σ_y²). The posterior is an inverse gamma of
    shape ν and scale β = νΩ̂₊, truncated to that range (derived; plan D16):

    Ω̄ = β·[Γ(ν − 1, β/Ω_hi) − Γ(ν − 1, β/Ω_lo)] / [Γ(ν, β/Ω_hi) − Γ(ν, β/Ω_lo)],

    with Γ(a, x) the upper incomplete gamma function.
  - **Why Ω̄** (derived, the law of total variance): the rule's variance term is linear in Ω, so averaging it over Ω's
    uncertainty replaces Ω by its expectation.
  - **Its values** (D16, unchanged): about Ω̂·ν/(ν − 1) once ν exceeds about 1.5 (3.00, 2.00
    and 1.25 times Ω̂ at ν = 1.5, 2 and 5). On the first fade or two (ν ≈ 1) it depends on the prior's top, +85 dB
    S₅₀₀, a placeholder: at ν = 1, Ω̄/Ω̂ is 14.4 at +20 dB S₅₀₀ and 12.1 at +30 dB S₅₀₀; with a +60 dB S₅₀₀ top, 8.6 and
    6.3.
  - **At Ω̂ ≤ 0** (derived): β = 0, and Ω̄ = Ω_lo·ν/(ν − 1) for ν > 1 (the prior's top negligible) and
    Ω_lo·ln(Ω_hi/Ω_lo) = 23.0·Ω_lo (−1.4 dB S₅₀₀) at ν = 1: a buried story's Ω̄ rests on the prior's −15 dB S₅₀₀ edge,
    a placeholder. On simulated samples, noise alone gives −1.4 to −1.9 dB S₅₀₀ after a first mark (ν ≈ 1), −11.6 to
    −11.8 at ν ≈ 1.9 and −14.3 after "PARIS" at 25 WPM (ν = 6.55) (medians, measured; the table in the variant
    below).
  - **Its table:** D16's, over ν and ln(β/Ω_hi), with the β = 0 limit above; its grid and error are for the
    implementation plan.
  - **Looked up only after a sample with s_k ≠ 0** (plan D17: "looked up only after a sample where the story expects
    carrier, Ω̂ standing still otherwise"): elsewhere Ω̂ does not change, and neither does ν (a closed bin of zero
    weight adds nothing), so holding Ω̄ is exact.
    - **The share of samples looked up** (measured; plan D17, 4896e97; each over from its first key-down to the end of
      its last tail): because the template stays nonzero for 16 samples after a key-up, the samples with s_k ≠ 0 are 63
      to 71% of an over of "PARIS" at 5.2 to 12 WPM, 86% at 25 WPM and 95 to 100% from 40 WPM ("TEST" 61 to 100%; "E E E
      E" 29 to 93%), against a key-down share of about 55%. Holding Ω̄ between those samples is exact but saves far less
      than the halving first estimated (plan D17, 4896e97: "The lookup saving is not a halving").
  - **Its cost** (rough, not timed; plan D17, 4896e97): about 6.5 to 13 ms per channel-second at the 1 700-story
    ceiling (637 500 story-samples per channel-second), its own line in the CPU breakdown. The implementation plan
    carries it against the new decoder's projected total, by part (owner, 2026-10-25; plan D17, f507812).
  - **Rejected** (plan D17): holding Ω̂ at one standard error (a made-up constant); |Ω̂| (it reports the noise's size
    as signal, mean 0.80 SE₀ on noise, and leaves the noise out of Ω̄); Σ|e_k² − σ_y²| (settles at 0.74 σ_y² on noise);
    counting the noise as extra fades (it cannot explain a negative Ω̂); fitting Ω to the raw samples (the cost grows
    with every sample).
  - **The 95% confidence bound** offered beside it is not part of the design (plan D16).
- **The measured variant: the noise in Ω̄'s likelihood** (owner, 2026-10-25: "Acceptable", plan D17, 63d9bfb, first as
  the default; made the variant by f507812; part 15, item 17). It keeps the information a buried story's samples
  carry instead of clipping it, at the cost of two more running sums per story (Ω̂'s noise variance, derived above)
  and a four-input table of 12.8 MB. Its derivations follow.
  - **The model** (plan D17, 63d9bfb): Ω̂ = Ω·G + N, with G the fading's power gain (Gamma, shape ν, mean 1) and N the
    noise's part (Gaussian, mean 0, variance A + B·ΩG as derived above; the plan's "standard deviation SE"):

    p(Ω̂ | Ω) = ∫ Gamma(G; ν)·Normal(Ω̂ − ΩG; 0, A + B·ΩG) dG,

    with the default's flat-in-dB prior p(Ω), and Ω̄ = ∫ Ω p(Ω̂ | Ω) p(Ω) dΩ / ∫ p(Ω̂ | Ω) p(Ω) dΩ.
  - **Computed in scratch** (derived): with P_r = ΩG, the realized signal power, the
    integral over Ω at fixed P_r is a partial moment of the Gamma, so Ω̄ = ∫ φ(P_r) E[G⁻¹; G ∈ (P_r/Ω_hi, P_r/Ω_lo)]
    dP_r / ∫ φ(P_r) Pr[G ∈ (P_r/Ω_hi, P_r/Ω_lo)]/P_r dP_r, with φ(P_r) = Normal(Ω̂ − P_r; 0, A + B·P_r). Checked in the
    fading-only limit against an independent quadrature of the default's closed form: within 0.0003%;
    a second, brute-force quadrature reproduces its values to 0.01 dB.
  - **Limits** (derived; checked at ν = 1, 2, 6.7 and 50 and ζ = SE₀/σ_y² = 0.02, 0.3 and
    1.6, with κ = 2.6):
    - **Ω̂ ≫ SE₀:** N's share vanishes and Ω̄ reduces to the default's closed form (above). The ratio to it is 0.9986 to
      0.9999 at Ω̂ = 100 SE₀, 0.9998 to 1.0000 at 1 000 SE₀ and 1.0000 at 10⁴ SE₀. The mean approaches it as about 1 −
      1/χ² (χ = Ω̂/SE₀): a constant variance A lowers the mean by about that factor, while B's term barely moves it,
      since with the prior flat in ln Ω and a noise variance proportional to the power the posterior is inverse
      Gaussian, whose mean is exactly Ω̂ (derived). The checks follow it (0.9898 at χ = 10,
      0.9999 at χ = 100 for ν = 2 to 50), even where B's share of the variance exceeds the fading's; only ν ≈ 1 with
      large ζ converges more slowly (0.9986 at χ = 100, ζ = 1.6).
    - **Ω̂ ≤ 0:** Ω̄ stays positive and finite, since the prior confines Ω to [Ω_lo, Ω_hi]; for example 6.5, 1.1 and
      −1.2 dB S₅₀₀ at Ω̂ = 0, ν = 1 and ζ = 1.6, 0.3 and 0.02, and −4.9, −11.0 and −14.2 dB S₅₀₀ at ν = 6.7.
    - **Not monotonic far below zero: an artifact of treating N as Gaussian, not physics** (plan D17,
      4896e97). With N Gaussian of variance A + B·ΩG, Ω̄ reaches a minimum at Ω̂ between −0.5 and −3.7 SE₀ (lower as
      the story's sums grow) and rises below it (derived). Ω̂'s physical floor bounds the rise:
      at the floor Ω̄ is 0.36, 0.71, 0.95, 1.83 and 1.54 dB above its minimum (dB relative to the minimum's Ω̄) after a
      4-sample mark, a 25 WPM dot, "E E" at 25 WPM, "PARIS" at 100 WPM and "PARIS" at 25 WPM. On noise alone Ω̂ falls
      below the minimum's position 37%, 22%, 11%, 1.6% and 0% of the time on those cases (measured).
      - **The true likelihood does the opposite** (derived; plan D17, 4896e97): every e_k² ≥ 0, so a reading near the
        floor, −σ_y²·Σs_k²/Σs_k⁴, needs the noise to cancel the carrier on every sample, and that grows less likely as
        the carrier grows (the lower tail of a noncentral χ², whose density at 0 falls as exp(−noncentrality)). The true
        family of likelihoods has a monotone likelihood ratio in Ω̂, so the exact Ω̄ does not rise as Ω̂ falls. The
        Gaussian's variance grows with ΩG without bound while the true lower tail stays bounded at the floor; that is
        what breaks it. The earlier explanation, "a very negative reading is better explained by a larger power with its
        larger noise variance", describes the model only and is withdrawn (plan D17, 4896e97).
      - **Measured** (steady carrier, so ΩG is fixed; the decided
        chain's noise; 2 × 10⁶ trials per power): the density of Ω̂ in a low bin over its density near the minimum
        falls with the power in the true distribution (25 WPM dot, χ from −1.90 to −1.50 against −0.90 to −0.70:
        0.106, 0.079, 0.061, 0.040 and 0.016 at ΩG = 0, 0.25, 0.5, 1 and 2 σ_y²) and rises in the Gaussian model (0.33,
        0.38, 0.41, 0.44 and 0.46). Near the floor the Gaussian's tail at ΩG = 0 is 70 to 900 times too heavy.
      - **Does it matter:** a buried story then expects a stronger signal than it should; on noise that likely only
        scores it lower (the safe side for false characters), but for a weak real station with an unlucky low first
        reading it overstates Ω̄. Probably a small effect on decoding (a judgment, not measured).
      - **Its remedy is decided only if the variant earns its place** (owner, 2026-10-25; plan D17, f507812). The
        candidates:
        - (A) keep it and count it (part 15, item 7);
        - (B) build the table monotone: below the minimum's position, hold Ω̄ at its minimum. Computed offline when
          the table is built; no cost at run time, no new input;
        - (C) a better model of N on the first marks, for example a distribution with N's mean (0) and variance
          (A + B·ΩG) and a lower bound that puts Ω̂ at or above its floor (N ≥ floor − ΩG), such as a shifted gamma. It
          would need one more running sum per story (Σs_k², for the floor), one more table input (the floor in units
          of SE₀: a five-input table, roughly 10 to 20 times the size at the same resolution, about 130 to 260 MB from
          this grid's 12.8 MB unless reparametrized), the direct integral
          redone with that density, and a check against the measured density above that it removes the rise. Matching
          N's third moment as well would need a third running sum over pairs of lags (N's third cumulant, some 20 to 30
          lag pairs within the 6-sample correlation) and possibly a sixth input.
  - **On simulated samples** (measured; e_k from the decided chain's correlated noise and 2 Hz
    Rayleigh fading, so N has its true, skewed distribution; each case's own ν, A and B; 600 trials per cell;
    medians):

| Story's samples (ν) | noise alone | −10 dB S₅₀₀ | −5 dB S₅₀₀ | 0 dB S₅₀₀ | +10 dB S₅₀₀ |
|---|---|---|---|---|---|
| one 4-sample mark (1.00) | +5.5 / −1.4 | +5.6 / −0.4 | +6.1 / +4.8 | +10.1 / +10.9 | +20.6 / +20.6 |
| one 25 WPM dot (1.01) | +1.9 / −1.9 | +2.5 / +1.2 | +5.6 / +6.0 | +11.1 / +11.1 | +21.2 / +21.2 |
| "E E", 25 WPM (1.82) | −8.3 / −11.6 | −6.9 / −7.6 | −2.6 / −2.5 | +2.4 / +2.4 | +12.6 / +12.6 |
| "PARIS", 100 WPM (1.93) | −9.6 / −11.8 | −7.6 / −7.7 | −2.6 / −2.5 | +2.5 / +2.5 | +12.2 / +12.2 |
| "PARIS", 25 WPM (6.55) | −13.2 / −14.3 | −9.8 / −9.7 | −4.5 / −4.5 | +0.5 / +0.5 | +10.3 / +10.3 |

    (Ω̄ in dB S₅₀₀, noise-in-Ω̄ variant / clipping default.) From −10 dB S₅₀₀ up, once ν ≥ 1.8, the two agree within 0.7
    dB in median (dB relative to each other); on noise alone the default reads 1.1 to 6.9 dB lower; on a first mark (ν =
    1) both read far above the truth, as the plan records. The plan's Ω̄ bullet quotes other figures (within about 1 dB
    from −8 dB S₅₀₀ once ν ≥ 2; on noise alone clipping −12 to −14 dB S₅₀₀ and this model 1.5 to 6 dB more): they came
    from a simulation whose noise was drawn from the Gaussian model itself, and this simulation on true, skewed
    samples supersedes them (plan D17, 4896e97).
  - **Its table** (on its build cost see the grid below):
    - **Inputs** (derived): υ = 1/ν in [0, 1] (ν ≥ 1 by the Satterthwaite form); χ = Ω̂/SE₀; ζ = SE₀/σ_y²; and the
      signal-with-noise term's size, κ = B·σ_y²/A, tabulated as λ = κζ = B/SE₀ (with powers counted in units of SE₀,
      the likelihood's variance is SE₀²·(1 + λ·ΩG/SE₀), so λ is the combination that enters; derived). The prior's
      bounds are fixed multiples of σ_y² (Ω_lo = 0.212 σ_y², Ω_hi = 2.12 × 10⁹ σ_y²), so they add no input; if the
      owner moves them, the table is rebuilt.
    - **The fourth input** (derived and measured): κ = 2b₂/a₂ runs
      from 0.10, 0.38, 0.78 and 1.20 after a single first mark of 1, 2, 3 and 4 samples (ζ = 19.2, 5.1, 2.5 and 1.6)
      to 2.59 after a 25 WPM dot and toward 2w₂/w = 2.77 on long runs; partway through a long mark's rise it is 0.005,
      0.085, 0.32 and 0.71 through j = 5 to 8. Where ζ ≥ 0.2 (the first marks), Ω̄ at Ω̂ from 0 to 2 SE₀ moves by up to
      23% between κ = 1.2 and 2.6, and by up to 42% at Ω̂ = −3 SE₀; at ζ = 0.05 by 2% or less. Fixing κ at 2.6 in a
      three-input table errs by up to 306% (99th percentile 65%; κ from 0.4 to 2.8), at Ω̂ below
      zero and small κ, where Ω̄'s rise toward the floor depends on κ; above Ω̂ = 30 SE₀ it does as well as the
      four-input table (99th percentile 0.69%). The four-input table is the owner's decision (2026-10-25: "a"; plan
      D17); reason: the three-input table's error falls on the first marks of fast signals, where the noise term
      matters, and the fourth input costs only memory and 8 more table reads per lookup.
    - **Why λ, on a logarithmic axis** (measured): with κ's axis extended to 0 at 8 uniform nodes, as
      the plan states, the interpolation error reached 66% (99th percentile 34% where ζ > 10), because at large ζ Ω̄
      changes strongly between κ = 0 and 0.05 (λ from 0 to 5 at ζ = 100); 15 κ nodes dense near 0 still erred by up to
      23%. On ln λ the error along that axis is 0.5% at most (one axis at a time, 29 nodes). λ below 10⁻³ is read at
      10⁻³ (κ = 0 included); the clamp moves Ω̄ by 0.076% at most (measured). The axis covers κ = 0 to 2.8
      at every ζ in the table. The choice of axis is part of the same decision.
    - **Ranges and grid** (the grid heuristic, chosen from each axis's own interpolation error, measured;
      its adequacy measured below): υ at 31 nodes, υ = 1 − (1 − p)², p uniform
      on [0, 1] (denser toward ν = 1); χ from −8 to 10⁴ at 85 nodes uniform in asinh(χ) (steps of 0.15); ln ζ from
      ln 0.003 to ln 400 at 42 nodes (steps of 0.29; plan D17, 4896e97: ζ "up to about 400"); ln λ from ln 10⁻³ to
      ln 1 120 (κ = 2.8 at ζ = 400) at 29 nodes (steps of 0.50). Output ln(Ω̄/σ_y²), multilinear interpolation (16
      corners). 3 209 430 nodes, 12.8 MB in single precision (the first grid, with κ from 0.4 and ζ to 10, was 2.1 MB;
      today's broad-prior J tables: 0.97 MB). Built from the direct integral at about 2 to 4 ms per node in the
      scratch program (24 minutes on 7 cores), so "built at startup" needs a much faster integrator or the table
      computed ahead of time and shipped (for the implementation plan). Most of the λ axis above κ = 2.8 at small ζ
      is never read; a table ragged in (ζ, λ) would be smaller.
    - **Beyond the ranges:** above χ = 10⁴ the fading-only closed form (D16's table), within 0.01% there (above).
      Below χ = −8, the value at −8, which only ζ below about 0.26 can reach: Ω̂'s floor is −1.0 to −2.1 σ_y² on long
      runs and about −1.0 to −1.3 SE₀ after a single short first mark or a partial rise; there Ω̄ is flat in χ (−1.35
      against −1.32 dB S₅₀₀ at χ = −10 and −3, ν = 1, ζ = 0.02). ζ stays within [0.003, 400]: with the newborn's
      latest switch at the last sample of its first mark's template with s_k ≠ 0 (part 8), its first lookup comes
      after at least one sample with s ≥ 0.01, so ζ ≤ 393 (after j = 5 alone; 23 after j = 6, 19.2 after a whole
      1-sample mark); ζ falls to 0.003 only after about 4 × 10⁵ key-down samples in one over (18 minutes of key-down,
      about 36 minutes of keying). λ stays within the axis for κ ≤ 2.8, which κ ≤ 2w₂/w = 2.77 on long runs keeps
      (derived for contiguous s = 1; measured below it on every case computed).
    - **Interpolation error against the direct integral** (measured; 4 000
      points in two physically motivated halves: ζ from 0.003 to 4 with κ from 0.4 to 2.8 and ν log-uniform on 1 to
      1 000; and first marks, ζ from 4 to 400 with κ from 0 to 1.2 (κ = 0 on 200 points) and ν from 1 to 3; χ uniform in
      asinh(χ) from Ω̂'s floor, χ ≥ −max(2.1/ζ, 1.3) and ≥ −8, to 30 or to 10⁴): |relative error| median 0.090%, 99th
      percentile 0.87%, largest 2.5% (at Ω̂ = −6.4 SE₀, in the rise toward the floor). By region (99th percentile,
      largest): ζ < 4, 1.1% and 2.5%; first marks, 0.58% and 0.70%; χ ≤ 30, 0.91% and 2.5%; χ > 30, 0.38% and 1.3%;
      κ = 0, 0.58% and 0.70%. 1% of Ω̄ is 0.04 dB.
    - **Cost per lookup** (rough estimate, not timed): a square root, a logarithm, an asinh, two divisions and the 16
      corners' 15 interpolations, about 40 to 60 ns. The main risk to that figure is the cache: the table is larger
      than a typical per-core second-level cache, and each lookup reads its 16 corners in about 8 cache lines, at
      random across the stories; the CPU breakdown's Ω̄ line is to be read against it.
    - **Per channel-second** at the 1 700-story ceiling (637 500 story-samples per channel-second; rough, plan D17,
      4896e97): the variant's lookup about 13 to 38 ms, in place of the default's 6.5 to 13 ms, plus its
      noise-variance sums, about 1 to 3 ms. Both are lines of the CPU breakdown (part 15, item 17).
- **What it fixes** (measured with 3 ms samples). The rule with Ω̂ taken as exact lost up to about 12 nats per gap in
  an over's first half-second, and up to about 100 nats per message in marks (2 Hz, +30 dB S₅₀₀; plan D16, "Findings of
  that derivation"). With Ω̄, about 1 nat per gap at ν = 1 and 8.8 to 20 nats per message against the exact predictive
  (+20 to +30 dB S₅₀₀; measured in scratch computations of 2026-10-22 not in the repository).
- **Starts and restarts:**
  - a new story's first mark seeds the sums (part 8);
  - a rescue newborn restarts them, as it starts its level again (topic 5, item 3, the level prior);
  - an over's end restarts them, so every over starts with no level.
- **Its CPU** (estimated, not timed; rough ms per channel-second at the 1 700-story ceiling; the prototype measures
  it in the breakdown of part 15, and the implementation plan carries it against the new decoder's projected total,
  by part; owner, 2026-10-25, plan D17, f507812):
  - Ω̂: two running sums per story;
  - ν: about 1.6 operations per story per sample;
  - Ω̄ by default: D16's two-input table, after samples with s_k ≠ 0 only, about 6.5 to 13 ms;
  - the noise-in-Ω̄ variant instead: its four-input table, about 13 to 38 ms, and its noise-variance sums, about 1 to
    3 ms (part 15, item 17).
- **The units:** μ, P, Ω̂ and Ω̄ are absolute (FS, FS²), so nothing is converted.
- **The backup hang AGC is dropped from the design for now** (owner, 2026-10-23: "A"; plan D17). The Ω̄ fix covers the
  starts it was meant to back up, and a channel-level AGC knows a new station's level only from an earlier
  transmission within its hang. It is in the backlog as a fallback, its role and settings to be worked out only if
  the prototype shows the per-story RMS failing.

#### Part 8. The newborn: the owner's flat prior, integrated while broad, then Gaussian

- **The decision** (owner, 2026-10-19, topic 5, item 3; plan D16): a new story's first mark is scored against a prior
  flat in dB over −15 to +85 dB S₅₀₀ (placeholder), and it seeds the RMS. Topic 5, item 3's standard (Ristic et al.
  2012; Berger, Daun and Koch 2008; Adams and MacKay 2007) and cost figures stand.
- **Kept as designed** (owner, 2026-10-24: "Agree"; plan D17): until its belief in log-amplitude narrows to a spread
  of σ_u = 0.1, a newborn scores against the exact belief p₀(a)·exp(Σ_j w_j·Λ_j(a)) under the flat prior p₀, with the
  amplitude held constant, then switches to N(μ, P).
- **How it is integrated (amended 2026-10-26, owner: "Okay with A"; plan D22):** on a fixed grid in log-amplitude
  u = ln a, the point-mass (grid) method of Bayesian filtering (Bucy and Senne 1971), its support thresholded as in the
  advanced point-mass method (Šimandl, Královec and Söderström 2006). The integral and the switch compute the owner's
  flat prior's score; they add no assumption to the model beyond the amplitude held constant (approximation (b)
  below).
  - **The grid:** N = 128 points u_i evenly spaced over the prior's 11.51 nepers (−15 to +85 dB S₅₀₀), spacing
    h = 11.51/127 = 0.0906 nepers (0.79 dB); each point carries ln of the unnormalized belief, Σ_j w_j(a_i)·Λ_j(a_i),
    with the exact ln I₀ and the weight of D21. The integral Z_k is the trapezoid sum h·Σ_i (ends halved)
    exp(Σ_j w_j Λ_j(a_i)), computed as a log-sum-exp; the sample's score is ln Z_k − ln Z_(k−1). No mode, curvature or
    stored samples: on noise, where the belief is highest at the prior's bottom edge, the edge is simply the grid's
    end.
  - **Why 128 points** (derived bound, measured check): the trapezoid rule's relative error on a belief of width σ
    (in u) is about 2·exp(−2π²σ²/h²) (Poisson summation). The belief is narrowest at the sample where the newborn
    hands over (σ_u falls below 0.1; on a strong rise one sample takes it from about 0.13 to 0.08 at +40 dB S₅₀₀,
    derived from the high-SNR curvature below). At h = 0.0906 the bound is 7 × 10⁻¹¹ at σ = 0.1, 4 × 10⁻⁷ at 0.08,
    4 × 10⁻⁴ at 0.06 and 5 × 10⁻³ at 0.05; 64 points (h = 0.18) give 0.045 at 0.08. Measured on 36 000 first marks
    (noise and −10 to +40 dB S₅₀₀, 25 and 50 WPM): at most 0.003 nats per sample against a dense integral; 96 points,
    0.044; 64 points, 0.28. **Not yet measured above +40 dB S₅₀₀**, and two rough derivations disagree there: the
    floor f alone limits one sample's precision to about f/√(2w) ≈ 0.03 nepers (f = 0.02), where 128 points would err
    about 0.17 nats; but the caps (part 9) limit each sample's precision more, to about twice its cap, which puts the
    width at handover near 0.074–0.078 nepers above about +30 dB S₅₀₀ (the amended build's re-review, 2026-10-26),
    where 128 points suffice. The prototype records the narrowest σ_u at handover (part 15, item 2); a value below 0.05
    goes to the owner.
  - **The live range (heuristic, with a measured margin):** to save CPU, only grid points within 80 nats of the
    largest are evaluated; a point that falls more than 80 nats below it (its weight in Z is under e⁻⁸⁰ ≈ 10⁻³⁵ of
    the largest's) is dropped for good. The risk is a point that is negligible now but becomes the best later (a
    rise or a misaligned story favoring small amplitudes before the flat part favors large ones). Measured over the
    36 000 first marks: the deepest a point fell and later came back to within 10 nats of the largest was 41.5 nats
    (one case, a misaligned story at +40 dB S₅₀₀; every other case under 17.3), so 80 is about twice the deepest
    seen. Keeping all 128 points would remove the constant at about 1 300 ns per newborn-sample instead of 490
    (estimated from the measured 9.5 ns per point), about +40% of the projected total instead of +3.6%. The
    prototype records the deepest revival (part 15, item 2); one deeper than 40 nats goes to the owner.
  - **The tables** (derived; their error measured): on a grid even in u, ln x = ln κ + u_i and ln q = 2u_i + ln s_k²
    for every point, so ln I₀ and w at every point are two fixed tables shifted by one offset per sample: no
    logarithm, polynomial or division per point. ln I₀(x) = x − ½ln x + G(ln x), G tabulated at steps of 0.023
    nepers of ln x over [−20, +165] with 4-point Lagrange interpolation (ln I₀ = x²/4 where every point has
    x < 10⁻³); w tabulated against ln q at steps of 0.18 with linear interpolation. Against direct evaluation: at
    most 3.1 × 10⁻⁴ nats per sample and 0.0037 nats per first mark.
  - **The weight (amended 2026-10-26, owner: "a"; plan D21):** w follows part 4's curve at each amplitude of the
    integral, w(q) with q = a²s_k²/σ_y², the same rule as an ordinary story's, evaluated at the parameter. It replaces
    the two fixed weights first decided (0.275 with the weak-signal form of ln I₀, 0.1986 with the strong-signal form;
    owner, 2026-10-25), which existed only for the running sums this grid replaces.
  - **What it replaces** (measured 2026-10-26; the amended build's review and its newborn study, scratch): the running
    sums of two forms of ln I₀ with the form frozen per sample let the integrand run to the prior's top on 82–100% of
    first marks (per-sample scores near 10⁷ nats); Newton's method plus Gauss–Hermite of order 5 scaled by the
    curvature has no mode to find where the belief is highest at the prior's bottom edge (66–81% of noise samples)
    and misses by 0.3–0.9 nats per sample at the 95th percentile (13 nats at worst) at every level; adaptive
    Gauss–Kronrod is exact but about 10⁴ times too slow (estimated).
  - **Its CPU** (timed in C++, desktop, 500 newborns): 490 ns per newborn-sample on noise, 595 on a mix of levels
    (43.5 and 53.8 live points); net of the ordinary scoring its samples replace, +14.7 and +34.3 ms per
    channel-second, +3.6% and +8.3% of the projected 411 ms. Its state is about 1 KB per newborn; copying it when a
    newborn's story branches costs about 60 ns (estimated, not timed).
- **Its approximations** (plan D17), each measured in the prototype (part 15); anything over a fraction of a nat goes
  to the owner:
  - **(a) the grid:** at most 0.003 nats per sample on the measured range (above); above +40 dB S₅₀₀ as stated above.
  - **(b) fading ignored before the switch** (the amplitude held constant): 0.4 to 0.9 nats at 0 dB S₅₀₀ and 2 Hz, and
    about 3 nats at 5 Hz (derived, rough). Measured under fading.
  - **(c) the switch,** about 0.008 nats (derived, below).
- **The switch to N(μ, P)** (derived):
  - **When:** when the posterior's standard deviation in u falls below σ_u = 0.1, or at the latest at the last sample
    of its first mark's template with s_k ≠ 0, j = 15 after the claimed key-up (owner, 2026-10-25: "A"; rejected
    the claimed key-up and no latest moment). Read as the claimed key-up instead, a short mark's newborn would switch
    before any sample with s ≥ 0.01, and in the noise-in-Ω̄ variant Ω̄ would be looked up at ζ in the hundreds and κ
    near 0.
  - **How:** moment matching, μ = e^(û + σ_u²/2), P = μ²(e^(σ_u²) − 1).
  - **Why 0.1:** below it the log-normal's skew costs under 0.01 nats against the matched Gaussian: (3σ_u)²/12 =
    0.0075, about 0.008 nats (derived, an Edgeworth estimate).
  - **When it happens** (derived; high-SNR curvature): σ_u ≈ 1/√(2Σ_k w_k s_k²a²/V′_k), with
    V′_k = σ_y²·(1 + q(f²s_k² + v_k)), q = a²/σ_y² = 10^(S₅₀₀/10) × 500 Hz / 74.48 Hz, and C_k² ≈ a² (the flat part
    inside the look-ahead). Counting rise samples from j = 5, the first with s ≥ 0.01 (f = 0.02, the template and v_k
    above; w = 0.1986 on every sample):

| S₅₀₀ | with the systematic term (the design) | with V′ = σ_y² (for reference) |
|---|---|---|
| +30 dB | 5th rise sample (j = 9, about 25 ms after key-down) | 2nd (j = 6, about 17 ms) |
| +20 dB | 5th (j = 9, about 25 ms) | 4th (j = 8, about 23 ms) |
| +10 dB | 8th (j = 12, about 33 ms); the 7th gives σ_u = 0.111 | 8th (j = 12); the 7th gives 0.105 |
| 0 dB | 42nd (j = 46, about 124 ms); the 41st gives σ_u = 0.1003 | 42nd (j = 46); the 41st gives 0.1002 |

    - **With w per regime (2026-10-25) the timings are unchanged** (derived): the weak form, and so 0.275, applies only
      to the rise's first samples, where 2b_k² is below the crossover (with the crossover at 1, 2 or 3: none at +30 dB
      S₅₀₀, at most 1 at +20 and +10 dB S₅₀₀, 2 or 3 at 0 dB S₅₀₀), and those samples carry too little information to
      move the switch: the same sample at every level, with the previous sample's σ_u within 0.0002 of the values above.
    - The systematic term caps each sample's information at strong levels, so the switch comes later there; at
      +10 dB S₅₀₀ and below it changes nothing.
    - The rule itself (σ_u < 0.1 on the actual posterior) is computed at run time; only these stated timings depend
      on the approximation.
    - In ms the V′ = σ_y² timings are within one sample of the 3 ms figures (about 20, 23, 35 and 124 ms), since σ_u
      depends on w·Σs_k² ∝ Δ·Σs_k² (at the strong limit w ∝ Δ), nearly independent of Δ (derived).
- **Its CPU** (timed 2026-10-26 for the grid of plan D22; the earlier estimate of 100 to 200 ns per newborn-sample
  was for the running sums it replaces): 490 ns per newborn-sample on noise, 595 on a mix of levels; with about 500
  newborns inside the integral at once on noise (an estimate to be counted), +14.7 to +34.3 ms per channel-second net
  of the ordinary scoring their samples replace. It multiplies no stories. Its CPU is its own line in the breakdown (part 15).
- **Practice:** Bell had no mid-stream newborns: every path started at ŷ = 0.5, P = 0.1 (AGC units), and a new station
  entered through the larger process noise after a space. The flat prior over a physical range is the tracking
  literature's standard for births (topic 5, item 3).

#### Part 9. The systematic term, per sample

- **D15's meaning, unchanged** (owner, 2026-10-21; D16: "carries on as decided in D15 … with both parts of C²"): the
  model's systematic uncertainty is added in quadrature to the noise in the error budget, proportional to the
  carrier's amplitude, with a relative size from the template's spread (now the edge term v_k) plus a floor. The term is
  a systematic effect, never noise. **Per sample:** its memory, look-ahead, floor and edge term were each decided on
  the per-sample form (owner, 2026-10-23; plan D17, below).
- **Per sample:** V′_k = σ_y² + C_k²·(f²s_k² + v_k), on both sides of Λ_k's ratio (b_k and z_k), inside the score of
  part 4. V′ is not divided by w.
- **C_k²** is the larger of two parts, both from the data, never from a story's level (D15; plan D17):
  - the channel's largest e² over a symmetric window of 21 samples: 10 back (the memory), the current one, 10 ahead
    (the look-ahead);
  - the sample's own e_k²/s_k², on every sample with s_k ≠ 0 (owner, 2026-10-25: "B"; plan D17), replacing a
    threshold at s_k ≥ 0.01, which goes with its sweep.
    - **Why no threshold** (plan D17, derived): the own part reads a² plus the noise divided by s_k². The noise
      dominates exactly where the sample's own SNR a²s_k²/σ_y² is below about 0 dB (dB SNR in the filter's 74.48 Hz),
      where the sample carries at most 0.06 to 0.09 nats of expected evidence (w·E[Λ]: 0.001, 0.008, 0.06–0.09, 0.5–0.7
      and 1.6–2.2 nats at −10, −5, 0, +6 and +10 dB; measured). A noisy own part can only raise V′,
      so it only shrinks that evidence, never adds to it.
    - Where a sample carries evidence, the own part is accurate even far below s = 0.01 (a +60 dB S₅₀₀ station at
      s = 0.005 has +22 dB SNR of its own), and the window's peak covers those samples anyway (derived).
    - Spaces (s_k = 0) have no own part and no leeway (v_k = 0 there).
    - **Rejected** (plan D17): keeping 0.01 (a made-up constant; its second reason, the same cut in Ω̂, went with
      ordinary least squares); dropping the own part (C² 19% low on 100 WPM dots and their evidence bound 23% higher on
      strong stations, the failure D15 guards against); a strength-dependent threshold (another made-up factor).
    - The prototype still counts how often the own part sets C² (part 15, item 10).
- **The memory: 10 samples, 26.67 ms** (owner, 2026-10-23: "I agree"; plan D17), replacing 390 ms. The owner: "The
  390 ms is arbitrary at this point and hence stupid": it was the old block length, and the blocks are gone.
  - **Its role:** it supplies C² on a mark's fall, where the template still tails off after the signal has dropped.
    Elsewhere it adds nothing: on the flat part the peak equals the current power, on the rise the look-ahead does the
    work, and in a gap the template is zero.
  - **Derived:** from the last sample at full carrier (99% of full amplitude or more; 99.9%
    gives the same) to the last sample where the leeway C²(f²s_k² + v_k) can equal the noise σ_y², for the strongest
    station the newborn prior allows (+85 dB S₅₀₀), over every edge position: 10 samples (8 at +60 dB S₅₀₀, 7 at
    +40 dB S₅₀₀). The plan adds (corrected 2026-10-24): "The amendment's revision (2026-10-24) found 9 to 10 samples
    depending on where the sampling grid falls relative to the edge, 10 the worst case; 10 covers every position, as
    the derivation requires."
  - **Rechecked with the decided chain** (the 25 taps' 8.000 ms delay, the decided v_k):
    over the eight choices of which sample is labeled j = 0 (the grid's phase, in steps of Δ/8), the memory is 9 or
    10 samples at +85 dB S₅₀₀ (7 or 8 at +60, 6 or 7 at +40) and the look-ahead the same; **the worst case is still
    10 samples**, at +85 dB S₅₀₀. Under the convention of part 5 (u uniform on [0, Δ)) both are 9 (7 and 8 at
    +60, 6 and 6 at +40). Under any one labeling, averaging over u already covers every edge position; the worst case
    over labelings, 10, covers whichever labeling the implementation finally uses, so finding 9 under the stated
    convention does not make 10 wrong. These counts rest on the template's ringing (part 5).
  - **Noise in the peak** (scratch simulation, correlated filter noise; plan D17): it reads high only, 1.77, 1.20, 1.06
    and 1.02 times the true power at 0, +10, +20 and +30 dB S₅₀₀, while the leeway is 0.005, 0.03, 0.28 and 2.7 times
    the noise, so it matters only where the peak is within a few percent.
  - Task 4e's failure of a 20 ms memory came from per-block booking, which per-sample booking removes (derived, not
    yet measured). A first proposal of 16 ms (a 1% tail cutoff borrowed from the own-power rule) is withdrawn, and the
    390 ms memory is gone. The prototype measures 26.67 ms
    with no other variants (part 15).
- **The look-ahead: 10 samples, 26.67 ms, the mirror of the memory** (owner, 2026-10-23: "Agree"; plan D17), replacing
  2 samples.
  - On a rise the template climbs while the signal at the sample may still be low, so C² must come from the flat part
    ahead.
  - **Derived** by the memory's rule, computed on the rise: from the first sample where the leeway
    can equal the noise to the first sample at full carrier, at +85 dB S₅₀₀, over every edge position: 10 samples (8
    at +60 dB S₅₀₀, 7 at +40 dB S₅₀₀). Rechecked with the decided chain: 9 or 10 across the labelings, 10 the worst
    case, 9 under the stated convention (as for the memory, above).
  - It covers D15's 4 ms minimum against the filter's ringing before a mark.
  - **Its costs:** 26.67 ms more delay before a sample is scored; one running maximum per channel, not per story.
- **The floor f = 0.02: a placeholder until the prototype measures it** (owner, 2026-10-23; plan D17).
  - The owner: "F seems like it might depend on a huge number of things that we have zero control over, and it seems
    very hard to choose an appropriate value."
  - Its 2% was measured in the block design (D15), where any amplitude change within a block had nowhere else to go.
    The new design models the amplitude per sample and gives edges their own term v_k. What remains for f is
    unmodeled and uncontrolled: a fading rate other than 2 Hz or non-Rayleigh fading, transmitter amplitude ripple,
    the receiving radio's AGC, a neighbor's leakage.
  - **The measurement that sets it** (part 15): decoding with the true story (the label), the scatter of strong
    stations' flat-part samples about the model's prediction beyond the noise, relative to the carrier, on the
    synthetic set and on real recordings. The value chosen comes back to the owner, and f goes to zero if the scatter
    is negligible on both.
  - **A consequence to weigh then** (noted 2026-10-24; plan D17): with f = 0 the flat-part cap w/f² (below)
    disappears and a strong station's flat part counts in full. Part 15, item 11 reruns the strong grid and the
    crashes at the proposed f before the value goes to the owner.
- **v_k** is the edge term of part 5: zero on the flat part and in gaps.
- **The caps** (derived, any data; the prior integrates to 1): score_k ≤ w_k·s_k²/(f²s_k² + v_k), since
  w_k·Λ_k ≤ w_k·z_k² and the own part keeps C² ≥ e_k²/s_k², so z_k² ≤ s_k²/(f²s_k² + v_k).
  - On the rise's samples j = 5 to 11: 0.50, 2.51, 8.65, 19.8, 51.6, 265 and 493 nats (w = 0.1986, f = 0.02, the
    decided v_k, the 25 taps). The value at j = 11, below the plateau's, comes from v_11 =
    2.7 × 10⁻⁶.
  - On the plateau: w/f² = 497 nats (496.5; plan D17).
  - **With w per sample (2026-10-25), checked rather than assumed** (derived): a cap is approached only where the
    systematic term dominates V′, which needs the sample's SNR e_k²/σ_y² well above q*_k = s_k²/(f²s_k² + v_k), and only
    for a story whose prediction sits near a = e_k/s_k, so that its expected SNR q_k is about e_k²/σ_y² ≥ q*_k. Since w
    falls with q, w_k ≤ w(q*_k) wherever a cap can bind. w(q*_k) is 0.2057 at j = 5 (q* = +4.0 dB, dB SNR in the
    filter's 74.48 Hz), 0.1998 at j = 6 (+11.0 dB SNR), 0.1990 at j = 7 (+16.4 dB SNR), and 0.1987 from j = 8 on and on
    the plateau (q* = 1/f² = +34.0 dB SNR), 0.1987 being the curve's Monte Carlo value of the strong limit. The caps at
    those weights are 0.52, 2.52, 8.67, 19.8, 51.6, 265 and 493 nats on the rise and 497 nats on the plateau: the stated
    caps hold within 4% at j = 5, where the cap is loose anyway, and within 0.6% elsewhere.
  - **For a story predicting a weak sample** (q_k small, w_k up to 0.2754) the guaranteed bound is up to
    0.2754/0.1986 = 1.387 times the caps above (689 nats on the plateau). Such a story's prior puts little weight
    near a = e_k/s_k, so its score is expected to stay well below that bound (an argument, not a derivation; the
    caps and crashes of part 15, item 6 test it).
  - The bound protects the first samples of a newborn, not a fall's tail; there, protection rests on the right story
    explaining the tail.
- **A static crash or click is the dot's shape** after a matched filter. The systematic term does not stop it: a known
  limitation (part 13).
- **Booked once per sample** with that sample's V′, so a later rise of C² takes nothing back. The key-up overshoot
  failure of 2026-10-21 goes by construction (derived).

#### Part 10. No frequency tracker in the decoder

- **The decision** (owner, 2026-10-23: "A"; plan D17). With the channelizer following the detector, the station
  arrives off-center only by the detector's error (part 1). The front filter loses almost nothing at 2 Hz and
  0.5 dB (dB relative to 0 Hz) at 15 Hz.
- The decoder neither re-centers nor reads the detector's frequency.
- Practice: no Morse decoder found estimates frequency inside the decoder (fldigi: none; Bell: fixed;
  `docs/research/2026-10-22-cw-detection-standard-practice.md`).

#### Part 11. The timing model, in 2.667 ms samples

- **The fist:** unchanged in form (topic 2, items 4 and 5; plan §2.5): log-normal lengths about medians of 1, 3, 1, 3
  and 7 dots, σ = 0.3 in ln(length), placeholders, heuristic; the hazard form (Murphy 2002; Bell 1977).
- **Counted in 2.667 ms samples** (owner, 2026-10-23; plan D17). A 100 WPM dot has a median of 4.50 samples, a 5.2 WPM
  dot 86.4; the longest median, a word gap of 7 dots at 5.2 WPM with standard spacing (D14), is 7 × 230.3 ms /
  2.667 ms = 605 samples (604.5; derived). The plan's §2.5, item 3 (6 562 pieces with a Farnsworth factor of 8.14)
  predates D14.
  - ±1 sample is ±22% of a 100 WPM dot, against σ = 0.3 (35% at one standard deviation): finer than the standard
    quarter-dot resolution (derived).
- **Timing reference:** a story's claimed key-down and key-up times are the keyed times. The template carries the
  20.8 ms from key-down to the response's half point (part 5), so printed letter times are keyed times, to within
  the sample convention of part 5 (a claimed edge sample lies 0 to 2.667 ms after the true edge).
- **Unchanged:** speeds (32, 12.0 to 230.3 ms per dot), standard spacing only (D14), text costs (ln 62 per letter;
  p = 1/3.06, q = 1/24), births, rescue rate ε = 10⁻³ per letter start, beam 40 nats and ceiling 1 700 stories
  (placeholders), merging (topic 5, item 2) and pruning, each counted per sample.

#### Part 12. What is removed from today's decoder

By name, in `engine/include/kz4ap/viterbi/` and `engine/src/viterbi/` (the implementation plan decides the code's
form):
- **The pieces** (`piece_s` = 2 ms) as the decoder's step and evidence; the complex running sums of marks and blocks.
- **The blocks:**
  - the block length derived from the spread (`block_length_s`, 390.37 ms at 1 Hz) and its heuristic `block_loss_db`
    (0.5 dB);
  - `score_block`, `score_first_block` and `BlockScore` as block-level scoring.
- **The noise of sums:** `sum_noise_hz`, c(m), and the 4 500 Hz × N₀ per piece rate.
- **The rise of 2026-10-20:**
  - `keying_rise_s` (5 ms, placeholder);
  - `rise_fractions`, `rise_fractions_by_position` and `rise_template_errors` (the piece fractions g_j and their
    errors).
  - They are replaced by the template and edge term of part 5.
- **The level's old prediction:** `predict` in `level.hpp`, P(t) = ρ²P + (0.523μ)²(1 − ρ²) with μ held. Its spread was
  scaled by the story's current level, the cause of the fading failure.
- **The flat prior's block tables:** `FlatPrior`'s Λ_flat(n, z) per block length and its large-z form. They are replaced
  by part 8's integral.
- **The block-level evidence calls:** `block_llr`, `block_llr_uncertain` and `block_evidence` in their block form.
  - The per-sample score reuses `ln_i0`, the Gauss–Hermite quadrature and the posterior's moments, with the tempering
    by w.
  - The broad-prior regime's J tables are rebuilt with the tempering. Tempered, the integrand carries I₀(·)^(w_k) in
    place of I₀(·), so J depends on w_k (derived): with w per sample (owner, 2026-10-25) the tables gain w as an input
    over 0.1986 to 0.2754, or that regime is served another way; the implementation plan decides. With the fixed-w
    variant one table serves.
- **The frequency tracker:** `CarrierTracker` (`tracker.hpp`, `tracker.cpp`) and its parameters:
  - `tracker_window_s` (1.0 s), `tracker_hop_s` (0.5 s), `tracker_search_hz` (15 Hz);
  - `tracker_gate_db` (9.0 dB in the code; 13 dB in the plan's §2.10; moot with the tracker removed, the difference
    noted only);
  - `tracker_drift_sd_hz_per_s` (0.3 Hz/s), `tracker_drift_noise` (1.5 × 10⁻⁴ Hz²/s³);
  - the trust tolerance (0.667 Hz), the untrusted search and the restart on two agreeing measurements;
  - the decoder's re-centering oscillator.
  - The genie frequency (`genie_frequency_offset_hz`, `genie_frequency_drift_hz_per_s`) moves to the test harness,
    which gives oracle channels the label's drifting frequency (plan D17).
- **The fading spread as a placeholder to sweep** (`fading_spread_hz` = 1 Hz, "set by measurement"): it becomes the
  owner's fixed 2 Hz. `genie_fading_spread_hz` stays, for the genie test.
- **The systematic term's 390 ms memory** of the channel's peak power: replaced by 10 samples (26.67 ms; owner,
  2026-10-23). `lookahead_pieces` (4 ms) becomes a look-ahead of 10 samples (owner, 2026-10-23).

**Kept:**
- the hiss estimator;
- the Morse tree and the text costs;
- the fist (in samples);
- the search's skeleton (births from silence, rescue, merging, pruning, beam, ceiling);
- the systematic term's rule (C² from the data, the floor, booked per sample);
- the Gauss–Hermite quadrature;
- the instrumentation.

#### Part 13. Known limitations and backlog items

Each is recorded in `docs/backlog.md` under the heading given.
- **Chirp beyond about ±25 Hz, key clicks and static crashes** (owner, 2026-10-22: "Agreed"; plan D16): "Chirp, key
  clicks and static crashes: known limitations of the new decoder".
  - **Chirp:** the front filter is −1.42 dB at 25 Hz and −3.8 dB at 40 Hz (dB relative to 0 Hz); beyond about ±25 Hz a
    mark's first samples are lost.
  - **Key clicks:** edges sharper than the template, mainly a neighbor's problem. Unshaped keying misfits the template
    by about 5.3% of full, outside the edge term's 0.8 to 4.7 ms range (part 5).
  - **Static crashes:** a filtered crash has the dot's shape, so a strong one will likely print a false E or T. A crash
    inside a story's key-down samples also enters its RMS: a 40 dB crash (dB relative to the key-down power) of 3
    samples in a 100-sample mark raises it 301-fold in power (17-fold in amplitude; derived), decaying as 1/n over the
    over, which is the safe direction.
  - **The standard remedy for crashes** is a wideband noise blanker ahead of the channelizer, as CW Skimmer offers; for
    later.
- **The amplitude model's over-confidence inside marks under fast fading** (owner, 2026-10-22): "The amplitude model
  inside marks under fast fading".
  - It matters with fading of about 2 Hz and faster at 25 WPM, earlier at slow speeds, mainly on strong signals.
  - A second number per story (the amplitude's rate of change) is the possible remedy, pending a standard-practice
    check.
- **Several fading rates per story** (owner, 2026-10-22: "B in backlog"): "Several fading rates per story in the new
  decoder". About 3 times the work per story; measure the CPU first.
- **A narrower filter for slower signals** (owner, 2026-10-22): "A narrower filter for slower signals in the new
  decoder". Slow signals pay up to about 1.8 dB of noise power against a 50 Hz filter.
- **The hang AGC as a fallback** (owner, 2026-10-23): "A hang AGC as a fallback for the new decoder's long-run
  amplitude". Work out its role and settings only if the prototype shows the per-story RMS failing.
- **The detector's frequency following** (owner, 2026-10-23: "a web of issues"): "The detector's frequency following
  and the fixed channel". The fixed channel is decided away (part 1). QSO turnovers and drift on the detector path
  stay unmeasured until the detector path is run.
- **Farnsworth spacing** (owner, 2026-10-21; plan D14): "Farnsworth spacing in the new decoder". Standard spacing only;
  Farnsworth-spaced groups (I, I2) decode poorly until a fix.
- **Others already in the backlog that apply:**
  - "Short overs of dots read as one longer symbol in the new decoder" (no text model; broad timing);
  - "A neighbor inside the channel takes over a weak station in the new decoder" (no interference model, O2). The
    76 Hz filter now rejects neighbors 70 Hz or more away by 15.3 dB (dB relative to 0 Hz) or more (its largest
    sidelobe, at 116.5 Hz; derived), so this is a likely gain, to be measured. A neighbor inside the filter also
    enters a story's RMS, widening its spread (the safe side) and raising its drift target.
  - "Fading splits marks in the new decoder": the diagnosis this design answers; its grid is the first measurement.
- **The approximations in the decisions of 2026-10-25** (no backlog item; each derived or measured in parts 1 to 11 and
  counted or measured in part 15):
  - **The default's Ω̄ clips Ω̂ at zero:** a buried story's Ω̄ then rests on the prior's −15 dB S₅₀₀ edge, a
    placeholder, and the information in a negative reading is dropped (part 7).
  - **In the noise-in-Ω̄ variant only** (part 15, item 17), the next three. Ω̄'s noise N is treated as Gaussian; on
    the first marks it is skewed (+1.8 after one 4-sample mark on noise alone; part 7).
  - With that Gaussian, Ω̄ rises again as Ω̂ falls below a point between −0.5 and −3.7 SE₀ (lower as the story's
    sums grow), at most 0.4 to 1.8 dB above its minimum (dB relative to the minimum's Ω̄) at Ω̂'s physical floor. This
    is an artifact of the Gaussian N, not physics: the true likelihood has a monotone likelihood ratio, so the exact
    Ω̄ does not rise (plan D17, 4896e97). Its remedy is decided only if the variant earns its place (owner,
    2026-10-25; plan D17, f507812); part 15, item 17 counts it meanwhile.
  - The signal-with-noise variance follows the story's realized power ΩG, not the local power at each sample: within
    3% on single marks at 2 Hz, 0.84 to 1.07 (5–95%) on "PARIS" at 100 WPM (part 7).
  - w_k is evaluated at the sample's own expected SNR, approximate on edges, where neighboring samples differ in
    strength (part 4; plan D17).
- **A start in a fade** (no new backlog item: the plan's findings under D16, "Findings of that derivation" and "The
  fix for starts: settled", record it; a residual of the decided design, inherent, counted by part 15, item 7).
  - About 9.5% of starts are 10 dB or more down, and 1.0% are 20 dB (dB relative to the long-run power; derived).
  - Ω̄ cuts their loss to a few nats in the measurements of 2026-10-22 (plan D16), but the residual at 95th percentiles
    is not zero.
  - Most of it is inherent: after one fade, a 10-fold recovery has the Rayleigh odds, about 9.5%.
  - Part 15, item 7 counts the splits.

#### Part 14. The test generator (D17)

- **The finding** (owner, 2026-10-22): "the development set can't be making good code at 100 WPM if it uses 5 ms rise
  and fall times."
  - The generator (`training/kz4ap_synth/generate.py`, `RISE_S`) used 5 ms raised-cosine edges at every speed, placed
    by default inside each element.
  - So every mark was 5 ms short at half amplitude and every space 5 ms long: 3% at 8 WPM, 10% at 25 WPM and 33% at
    80 WPM (the development set's top speed cell).
- **Decided** (owner, 2026-10-23: "I choose A …"; revised: "2.5 milliseconds"; settled: "Agreed"):
  - **Placement:** each edge starts the rise at key-down and the fall at key-up, so each mark keeps its keyed length at
    half amplitude.
  - **Shape:** a raised cosine with a 10–90% rise (and fall) time of 2.5 ms, full length 2.5 ms / 0.5903 = 4.23 ms
    (derived), at every speed, with no shortening at high speed.
  - At 100 WPM a dot is 65% flat, about the ITU-based guide of a fifth of a dot (a heuristic).
  - Real transmit paths' mark bias is not added to every group.
- **The fading groups built on VE3NEA's benchmark keep his edges** (owner, 2026-10-23: "A"; plan D17). B ("fading",
  stage-1 benchmark) and B2 ("fading", development set) keep his 2 ms raised-cosine edges (full length; 1.18 ms
  10–90%) centered on each element's ends (`training/kz4ap_synth/suites.py`, `VE3NEA_EDGE_S`, `edges_centered=True`),
  so they still reproduce his DeepCW conditions; every other group takes the 2.5 ms default.
  - His convention also keeps each mark's keyed length at half amplitude, differing from the default by a constant
    delay.
  - His edge lies inside the measured range of rises and misfits the template by about 1% of full after alignment
    (0.67–1.35%, derived; part 5), so the two groups also test the edge term on a sharper transmitter.
- **Then, before the new decoder's long runs:**
  - regenerate the development set from the same seeds;
  - rerun the bank decoder's reference on it. Old and new scores are not compared.
- **Oracle channels** get the label's true, drifting frequency, since the channelizer now follows the detector's
  frequency and on oracle channels that frequency is the label's, without drift (a test-harness change; plan D17).

#### Part 15. What the prototype must measure before anything is called done

Built in a copy and run paired against today's decoder, on the regenerated development set (part 14).

**Every CPU measurement reports where the time goes** (owner, 2026-10-24: "Sure, that sounds good"; plan D17): ms of
CPU per channel-second for making successors, ordinary scoring, the newborn's integral, merging and pruning, and the
rest, with the story counts behind them; Ω̄'s lookups are a line of their own (plan D17, 2026-10-25), and so are w's
lookups and the variant's noise-variance sums, so that items 17 and 18 can weigh them. **Costs are stated as
fractions of the new decoder's projected total, not of today's decoder** (owner, 2026-10-25: "I care about the
fraction of the total time we think we're going to achieve, not what some garbage decoder is doing today"; plan
D17, f507812): this section gives rough absolute ms per channel-second, and the implementation plan carries the
projected budget by part.

1. **Correctness:**
   - the front filter's taps, response and noise bandwidth against part 2;
   - σ_y² and w against the measured output-noise power and correlation at 2.667 ms;
   - score_k against a direct numerical integral of the tempered Rician likelihood;
   - the binned ν against the exact double sum on the same samples (0.03 to 2.3% apart in scratch, part 7; a
     check of the code);
   - the default's Ω̄ table against the closed form, and the w table against its curve (parts 7 and 4);
   - for the noise-in-Ω̄ variant: its noise-variance sums a₂ and b₂ against the exact double sums, Var(N) against
     A + B·ΩG on the decoder's own samples, and its table against the direct integral (part 7);
   - the template and the edge term against the channelizer and front filter run on the generator's keying, at each
     edge position and at rises of 0.8 to 4.7 ms;
   - the marks' timing offset (claimed against keyed times);
   - Ω̂, ν and Ω̄ against simulation of the generator's fading (the check of plan D16's derivation, on the decoder's own
     sums);
   - determinism across block splits of the input.
2. **The newborn's approximations** (owner, 2026-10-24; plan D17); anything over a fraction of a nat goes to the owner:
   - (a) the grid (plan D22) against a dense integral, per sample and per first mark, including levels above
     +40 dB S₅₀₀; the narrowest σ_u at handover (below 0.05 goes to the owner) and the deepest revival of a point
     below the largest (deeper than 40 nats goes to the owner);
   - (b) the amplitude held constant before the switch, under fading (0 dB S₅₀₀ at 2 Hz and 5 Hz);
   - its CPU, its own line in the breakdown.
3. **The fading diagnosis's grid:** f_D = 0.1, 0.3, 1, 2 and 5 Hz × +10, +20 and +30 dB S₅₀₀, "PARIS PARIS" at
   25 WPM, 4 edge positions, 5 seeds; exact texts and character error rate, against today's table.
   - **The same grid at 8 WPM and at 5.2 WPM**, with the in-mark E[z²]. The in-mark limitation "matters … earlier at
     slow speeds", and the derivation's 8 WPM run was never finished, so its size at slow speeds is otherwise
     unknown.
4. **The strong grid** to +60 dB S₅₀₀ at 16 edge positions, steady and at 0.3, 1, 2 and 5 Hz; mark splits and invented
   overs.
5. **The speed step** (20 to 30 WPM, +10 to +28 dB S₅₀₀).
6. **The caps and crashes:** the fall's tail, the filter's ringing and a filtered click, scored at every edge position
   (part 9's bounds made exact); false characters per crash at +20 to +60 dB S₅₀₀.
7. **The long-run amplitude in the loop:**
   - starts in a fade (10 and 20 dB down, dB relative to the long-run power) and normal starts at 1, 2 and 5 Hz:
     splits in an over's first 3 s;
   - rescues and over boundaries (the sums restarted);
   - a weak station beside a neighbor inside the filter;
   - crashes of 20 and 40 dB and static bursts of 10 to 100 ms inside marks (dB relative to the key-down power): the
     RMS's rise and its cost;
   - noise alone;
   - Ω̄'s dependence on the prior's top at ν ≈ 1 (+85 against +60 dB S₅₀₀), and on its bottom for weak stations
     (−15 dB S₅₀₀ against a bottom 10 dB higher);
   - how often Ω̂ ≤ 0, where the default's Ω̄ rests on the prior's bottom, on noise alone (item 14) and on steady
     weak signals (item 9);
   - the noise-in-Ω̄ variant is item 17.
8. **The fixed 2 Hz against the genie spread** on the grids of item 3 (25, 8 and 5.2 WPM), with the in-mark
   calibration E[z²]. This sizes what several rates (backlog) and the in-mark limitation cost; no several-rate variant
   is run.
9. **Steady weak signals** at 5, 12, 25, 40 and 100 WPM: the S₅₀₀ at 50% character error, against today and the bank.
   Slow signals are expected to pay up to 1.8 dB of noise power against a 50 Hz filter, part of which post-detection
   summing recovers, the owner's accepted cost; the measured cost is reported in dB S₅₀₀ at 50% character error.
10. **The systematic term's memory and look-ahead, 10 samples each (26.67 ms)** (owner, 2026-10-23; plan D17): on the
    strong-station grid (item 4), the stopped-neighbor grid, the speed step (item 5) and TEST/TU, with no other
    variants. Also how often a sample's own e_k²/s_k² sets C² (the own part on every sample with s_k ≠ 0; owner,
    2026-10-25; part 9), with the template value and the sample's SNR where it does. No threshold is swept.
11. **The floor f, which this measurement sets** (owner, 2026-10-23; plan D17): decoding with the true story (the
    label), the scatter of strong stations' flat-part samples about the model's prediction beyond the noise, relative
    to the carrier, on the synthetic set and on real recordings. The value chosen comes back to the owner; f goes to
    zero if the scatter is negligible on both. Before it goes to the owner, the strong grid (item 4) and the crashes
    (item 6) are rerun at the proposed f, since with f = 0 the flat-part cap disappears (noted 2026-10-24; plan
    D17).
12. **Other transmitters' edges** (owner, 2026-10-23; plan D17):
    - stations keyed with 0.8 ms and 4.7 ms rises (10–90%), the ends of the edge term's range;
    - stations keyed with a transmit-path mark bias over −7 to +4.7 ms, both signs (the measured range;
      `docs/research/2026-10-23-keying-envelopes.md`), for example −7, −3.5, 0, +2.5 and +4.7 ms;
    - at 25 and 100 WPM and at +10 to +40 dB S₅₀₀;
    - each scored for the template's misfit per sample against v_k, and decoded;
    - groups B and B2 (VE3NEA's 1.18 ms edges) test the edge term on a sharper transmitter (part 14).
13. **The merge variant** (owner, 2026-10-23; plan D17): the previous element's length in the state when shorter than
    10 samples, against the per-survivor default, on the high-speed signals (above about 45 WPM): its CPU (with the
    breakdown and the story counts) and any decoding gain, brought to the owner before anything changes.
14. **Noise alone:** false characters per hour (no more than today's decoder), the newborns alive in the integral, and
    CPU time per channel-second with the breakdown, each part against the implementation plan's projected budget.
15. **Neighbors and frequency:**
    - neighbors 40 to 70 Hz away (today a failure);
    - drifting stations and QSO turnovers on oracle channels given the label's drifting frequency.
16. **A measured variant: Bell's decay toward zero across spaces,** against the decided drift toward the Rayleigh mean
    amplitude 0.886·√Ω̄ (the owner's question, 2026-10-22: "How will Bell's choice to decay towards zero compared to
    our choice to decay towards the mean play out in all of the different test recordings…?"; recorded in the plan
    2026-10-24, D17, 6a13f77). A close neighbor is the case to watch; the results go to the owner.
    - **Bell's form:** across a space the amplitude decays by Φ = 10^(−2/(22.4·T_dot,ms)) per 5 ms (about 0.36 dB of
      amplitude per dot), so by Φ^(Δ/5 ms) = Φ^0.5333 per 2.667 ms sample, by at most 5 dB of amplitude (Bell 1977;
      `docs/research/bell-1977-notes.md`).
    - **Only the mean's target changes** (owner, 2026-10-26: "Sure iii"; plan D20): the variance across the space
      follows the decided rule, ρ_a²P + ½(1 − ρ²)Ω̄, unchanged. Bell's own variance, P + Q on re-entry to a mark with
      Q = 0.15·e^(0.6(B−14)) + 0.01·B·e^(0.2(1−B)) in units of the story's Ω̄ (B the space in bauds, capped at 14),
      is left out: adding it to the decided rule would count the gap's uncertainty twice, and putting it in place of
      that rule would change two things at once. Bell's form in full may follow as a second variant if this one shows a
      difference worth explaining.
    - **The comparison:** both are scored paired on every group (steady signals against speed, fading, keying styles,
      speed changes, a neighbor 0 to 120 Hz away, QSOs, noise only), reporting exact texts, character error rate,
      splits and false characters.
17. **A measured variant: the noise in Ω̄'s likelihood** (Ω̂ = Ω·G + N, its four-input table and its two
    noise-variance sums) against the default, Ω̂ clipped at zero in the fading-only closed form (owner, 2026-10-25:
    "We clip as the default, and we test option 7's improvement in decoding and in speed against that default"; plan
    D17, f507812; part 7). Both are scored paired on every group, with the weak-signal groups, starts (item 7)
    and noise alone (item 14) the cases to watch, reporting exact texts, character error rate, splits and false
    characters, and for CPU the Ω̄ line and the noise-variance sums' line of the breakdown. The variant also counts
    how often Ω̂ falls below the point where its Ω̄ stops falling (the Gaussian artifact, part 7), on noise alone
    and on steady weak signals; the artifact's remedy (A to C, part 7) is decided only if the variant earns its
    place (plan D17, f507812).
18. **A measured variant: the fixed w = 0.1986** against w per sample (owner, 2026-10-25; plan D17; part 4), on
    the weak-signal groups (item 9's steady weak signals and the development set's weak groups): the S₅₀₀ at 50%
    character error, exact texts and character error rate, and the CPU of w's lookups. If the curve shows no
    measurable gain, the decoder goes back to the fixed value (plan D17).

Every reported run's rounded per-signal score table is committed with its results record (CLAUDE.md).

#### Part 16. Standard practice for each stage

In the table, "survey" is the standard-practice survey of 2026-10-22, `docs/research/2026-10-22-cw-detection-standard-practice.md`.

| Stage | Practice (source) | This design | Departure and reason |
|---|---|---|---|
| Frequency | fldigi: no AFC; Bell: fixed; Skimmer: not stated; coarse-plus-fine FFT channelizers (survey; topic 4, item 8) | channelizer follows the detector; no decoder tracker | none |
| Predetection filter | Skimmer 50 Hz; fldigi 5/T_dit; Bell 64–100 Hz (survey); matched filter (Proakis and Salehi §4.5-1; Turin 1960) | 76 Hz, matched to a 12 ms dot with 2.5 ms edges | width above Skimmer's 50 Hz, to reach 100 WPM (owner's decision); slow signals pay up to 1.8 dB of noise power |
| Detection | envelope \|z\| in every decoder found (survey) | e = \|y\| | none |
| Sampling | fldigi 2 ms, Bell 5 ms | 2.667 ms, every 4th channel sample, at most a quarter of the 100 WPM dot | none (owner's standard frame step, on the channel's sample grid) |
| Correlated samples | decimate or weight (Kay; Gales and Young); the composite-likelihood magnitude adjustment evaluated at the parameter (Chandler and Bate 2007; Varin, Reid and Firth 2011); Bell treated them as independent | weight w_k per sample from its expected SNR, 0.275 (weak) to 0.1986 (strong), from the actual correlation; the fixed 0.1986 a measured variant | corrects Bell's over-count |
| Statistic | noncoherent Rician likelihood (Proakis and Salehi) | Λ_k(a), integrated over the amplitude | none |
| Mark shape | Bell: 0/1; MLSE with ISI (Forney 1972) | the smeared prediction | [departure] from Bell, for timing (several ms per mark otherwise) |
| Amplitude | Bell's scalar Kalman per path; per-survivor processing (Raheli et al. 1995) | N(μ, P) per story, every 2.667 ms, drifting toward the Rayleigh mean amplitude | spread scaled by the long-run amplitude (owner); 2 Hz against Bell's 1 Hz (owner) |
| Long-run level | Skimmer: RMS of key-on samples, transitions discarded; Bell, fldigi: AGC; a variance component estimated below zero is clipped at zero (Searle, Casella and McCulloch 1992) | story's RMS by ordinary least squares over every sample (e_k² counted in proportion to s_k²), clipped at zero, with Ω̄ (posterior mean of the fading-only inverse gamma under the flat prior; Satterthwaite's ν on 42.67 ms bins, weights s_k⁴) | edges counted less instead of discarded: a dot at 100 WPM has no flat part (owner, derived reason); Ω̄ is the derived correction for few fades; the noise in Ω̄'s likelihood a measured variant |
| Newborn level | Morse practice (Bell 1977): no mid-stream newborns; paths start at 0.5 ± 0.32 of the AGC level, and a new station enters through larger process noise after a space. Tracking practice: flat over a physical range, or from the first measurement (topic 5, item 3) | flat in dB, −15 to +85 dB S₅₀₀ | [departure] from Bell's Morse practice, standard in tracking (owner, 2026-10-19): a rescue or a new over must learn a new level from its own marks |
| Model error | systematic term in quadrature (D15, standard) | per sample, both parts of C² (the window's peak over 21 samples; the sample's own e_k²/s_k² wherever s_k ≠ 0); edge term v_k over 0.8–4.7 ms rises; floor a placeholder | none |
| Merging | reduced-state, per-survivor processing (Duel-Hallen and Heegard 1989; Eyuboglu and Qureshi 1988) | the state as it is; each story's own history | none; full MLSE (Forney 1972) a measured variant |
| Chirp, clicks, static | accepted as limitations; optional noise blanker (survey) | limitations; blanker in backlog | none |
| Timing | hazard form (Murphy 2002; Bell 1977) | in 2.667 ms samples | none |

#### Part 17. The values

| Value | Default | Unit | Class and source |
|---|---|---|---|
| Channel rate r | 1 500 | samples/s | `docs/signal-processing.md` §7 |
| Coarse step | 23.4, moved at ±11.7 | Hz | derived (192 kHz / 8 192) |
| Front filter | 12 ms dot at half amplitude, 2.5 ms 10–90% raised-cosine edges (4.235 ms full) | ms | owner, 2026-10-23 (D17) |
| Taps | 25 at 1 500 samples/s, unity gain at 0 Hz | — | derived |
| B_N | 76.0 | Hz | derived |
| σ_y² | N₀ × 74.48 | FS² | derived (channel's colored noise); owner, 2026-10-23, handled as obvious (D17) |
| Δ | 2.667 (4 channel samples; 375 samples/s) | ms | owner, 2026-10-23 (D17) |
| w_k | per sample from q_k = μ²s_k²/σ_y²: 0.2754 (weak limit) to 0.1986 (strong limit) | 1 | owner, 2026-10-25 (D17); limits derived (actual correlation on colored noise), curve measured; fixed 0.1986 a measured variant |
| w table | q_k from −40 to +40 dB SNR (in 74.48 Hz), clamped to the limits outside; 129 nodes uniform in q_k/(1 + q_k); error ≤ 0.0002 | 1 | grid heuristic, error measured |
| Quadrature order K | 5 | — | plan D4 |
| Front filter delay (causal) | 8.000 (12 taps) | ms | derived |
| Template | S(τ): half point 20.8 ms after key-down; 10–90% 10.03 ms; tail ≥ 0.01 of full through 11 samples, nonzero through 16 (42.7 ms) | ms | derived (D17's shape, the 25 taps) |
| Edge term v_k | over u uniform in a sample and t_r uniform over 0.8–4.7 ms, aligned at half amplitude | 1 | owner, 2026-10-23 (D17); values derived |
| Sample convention | j = 0 the first sample at or after the edge, u uniform on [0, Δ) | — | owner, 2026-10-25 |
| f_D | 2 | Hz | owner, 2026-10-22 (D16) |
| Amplitude drift target | 0.886·√Ω̄ at correlation ρ_a | FS | owner, 2026-10-23 (D17) |
| ν bins | 42.67 (16 samples), last 10 kept | ms | owner, 2026-10-23 (D17); binning 0.03–2.3% from exact (measured) |
| Ω̂ | Σ s_k²(e_k² − σ_y²)/Σ s_k⁴ over every sample of the over | FS² | owner, 2026-10-25 (D17), ordinary least squares |
| ν bin weights | W_i = Σ s_k⁴ over the bin's samples | 1 | owner, 2026-10-25 (D17); Satterthwaite match derived |
| Ω̂'s noise variance (variant only) | A + B·ΩG; A = σ_y⁴a₂/(Σs⁴)², B = 2σ_y²b₂/(Σs⁴)², exact sums over 6 lags | FS⁴ | derived; checked by simulation within 2%; exact sums over 6 lags (owner, 2026-10-25) |
| Ω̄ (default) | posterior mean of the inverse gamma of shape ν and scale ν·max(Ω̂, 0) under the flat prior | FS² | owner, 2026-10-25 (D17, f507812); closed form derived |
| Ω̄ (variant) | posterior mean under Ω̂ = ΩG + N, G ~ Gamma(ν), N ~ Normal(0, A + B·ΩG), flat prior | FS² | owner, 2026-10-25 (D17, f507812): a measured variant against the default |
| Ω̄ lookups | after each sample with s_k ≠ 0 only | — | owner, 2026-10-25 (D17); exact |
| Ω̄ table (default) | D16's, over ν and ln(β/Ω_hi), β = ν·max(Ω̂, 0), with the β = 0 limit | — | grid and error for the implementation plan |
| Ω̄ table (variant) | inputs 1/ν, asinh(Ω̂/SE₀), ln(SE₀/σ_y²) to ζ = 400, ln λ (λ = κζ, κ from 0 to 2.8); 31 × 85 × 42 × 29 nodes, 12.8 MB; error 99th percentile 0.87%, largest 2.5% | — | inputs derived; the fourth input and its λ axis owner, 2026-10-25 (D17); grid heuristic, error measured; the Gaussian artifact's remedy decided only if the variant earns its place |
| Level prior | flat, −15 to +85 | dB S₅₀₀ | placeholder (topic 5, item 3) |
| Newborn switch | σ_u < 0.1 in ln a | 1 | derived (0.0075 nats skew cost); owner, 2026-10-24 (D17) |
| Newborn grid | 128 points even in u = ln a over the prior (h = 0.0906 nepers), trapezoid; live range 80 nats | 1, nats | owner, 2026-10-26 (D22); 128 from the trapezoid bound and measured (0.003 nats per sample at worst); 80 heuristic, twice the deepest revival measured (41.5 nats) |
| Systematic floor f | 0.02 | relative amplitude | placeholder until measured (owner, 2026-10-23, D17) |
| C² memory | 10 (26.67 ms) | samples | owner, 2026-10-23 (D17) |
| C² look-ahead | 10 (26.67 ms) | samples | owner, 2026-10-23 (D17) |
| C² own part | e_k²/s_k² on every sample with s_k ≠ 0, no threshold | FS² | owner, 2026-10-25 (D17) |
| Caps | rise j = 5–11: 0.50 to 493; plateau w/f² = 497 (at w = 0.1986, where they bind; at most 1.387 times them for a story predicting a weak sample) | nats | derived |
| Fist medians, σ | 1, 3, 1, 3, 7 dots; 0.3 | dots; ln(length) | placeholders, heuristic |
| Over start per sample | 1.333 × 10⁻⁴ (rate 0.05/s; 8.92 nats) | per sample | placeholder rate, set by the plan's R5; per sample derived |
| Rescue ε | 10⁻³ | per letter start | placeholder, set by the plan's R5 |
| Beam, ceiling | 40; 1 700 | nats; stories | placeholders, set by the plan's R2 and R3 |
| Hiss neighborhood, rank | ±2 kHz, 25th percentile | Hz, % | placeholders, measured by the plan's R11 |
