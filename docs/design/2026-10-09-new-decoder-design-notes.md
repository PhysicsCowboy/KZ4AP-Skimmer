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
6. **Past text:** open until topic 6.
7. **The last stretch of keying,** if topic 4 finds that the expected filter output depends on keying before the
   current element.
8. **Its probability and a link to its parent,** to trace its text back when printing (topic 8).
9. **A clock for each non-Morse state:** how long the carrier or the silence has lasted.

At channel level, outside the hypotheses: the amplitude and the noise level (topic 3); the station's fist habits,
per channel or per hypothesis, open (topics 2 and 7).

### Topic 2: timing — speed, spacing and fists (owner, 2026-10-12)

Guiding principle (owner): the simplest version of Bell's structure that works; a neural decoder may follow, so no
refinement without a measured need.

1. **Speed** is one of the bank's 32 ladder speeds (dot lengths 10% apart, 5–100 WPM), nothing finer. A 10% speed
   error is small next to a hand sender's scatter and far inside the dot/dash (×3) and letter/word (×7/3) margins
   (derived). A hypothesis's speed names the filter it listens to.
2. **Speed changes only at word boundaries, by one ladder step (±10%),** with a small probability (heuristic, to be
   measured). Drift within a word is absorbed by the fist scatter. The start of an over is not a speed change:
   hypotheses are born at every ladder speed (topics 5 and 7). Check: the development set's speed-change group; if
   one step per word follows too slowly, allow two steps at a smaller probability.
3. **Farnsworth gap factors:** exactly geometric, 1, 1.68, 2.83, 4.76, 8.00 (ratio 8^(1/4)); the top value is about
   the slowest Farnsworth practice in use (letters at 18 WPM, overall 5 WPM: gaps about 7.8 times nominal, derived
   from the PARIS word). The factor changes only at word boundaries, by one step.
4. **Fist model: fixed, at channel level, never learned while running.** It is the rule that scores every
   hypothesis: how probable an element or gap of a given length is at that hypothesis's speed, when a mark or gap
   ends, and its classification (dot or dash; element, letter or word gap). Form: length = ideal × a random factor
   whose logarithm is Gaussian (as the generator's styles, from VE3NEA's analysis of real keying), with a slightly
   heavy tail so that one wild element does not kill the right hypothesis. Ideal lengths 1, 3, 7 dots. Spread
   σ ≈ 0.3 for marks and for gaps (two separate settings, both starting at 0.3: as forgiving as the generator's
   hand style; heuristic). One forgiving model to start; two fist classes ("precise", "hand") in the hypothesis only
   if the development set shows precise signals losing noticeably.
5. **Telling a speed change, fist scatter and Farnsworth spacing apart:** no special logic; the hypothesis
   probabilities do it (a speed change shifts all later lengths together; scatter is independent per element;
   Farnsworth stretches only letter and word gaps).

Caution: the development set comes from the same generator, so setting the fist from it and testing on it is partly
circular; recordings of real operators are the real check.
