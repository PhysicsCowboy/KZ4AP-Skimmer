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

1. **Speed** is one of 32 values whose dot lengths are 10% apart, covering 5–100 WPM (the bank's ladder, used only
   as a grid), nothing finer. A 10% speed error is small next to a hand sender's scatter and far inside the dot/dash
   (×3) and letter/word (×7/3) margins (derived). Speed only sets the expected lengths of elements and gaps
   (amended by topic 4: a hypothesis does not listen to a speed-specific filter).
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

### Topic 3: amplitude (owner, 2026-10-12)

The amplitude A is the expected key-down level the hypotheses score against. One estimate per channel, independent of
every hypothesis and speed (owner's tap idea).

1. **The tap** is a fixed sum matched to the fastest speed: about 10 ms of the signal (5 of topic 4's 2 ms pieces),
   the same as the bank's shortest branch (9.6 ms). Every element at every speed up to 100 WPM lasts at least 12 ms,
   so the tap reaches the full A on every mark (derived). Its noise bandwidth is about 100 Hz against the channel's
   300 Hz, about 4.6 dB less noise than the raw channel (derived). (Amended by topic 4: built from the pieces, not
   from the bank.)
2. **Estimate:** A² = the weighted average of the tap's power minus the noise power, each sample weighted by its
   probability of being key-down, judged on the tap itself (carrier plus noise against noise alone, at the current
   estimates). Gaps get weights near zero, so the estimate freezes during gaps and silence. Feeding back the
   decoder's own key-down probability is a later option (a loop).
3. **Speed of following:** an average over about the last 0.5 s of key-down time (gaps do not count); heuristic, to
   be tested on fading (QSB).
4. **Noise level:** measured on the pieces that the hypotheses agree are gaps (amended by topic 4; first proposed as
   the bank's per-branch noise estimate).
5. **Use:** the scoring of topic 4 compares a hypothesis's summed mark with a carrier of amplitude A plus noise.
6. **Start of an over:** A starts from the level the signal detector measured when it found the station, then refines
   from the first marks.

Errors: A too high makes weak marks look like gaps (lost elements); too low makes noise look like marks (false dots).

### Topic 4: the observation (owner, 2026-10-12)

1. **Every hypothesis is scored on the same evidence:** the channel signal cut into short non-overlapping pieces,
   each the sum of the complex samples over about 2 ms (3 samples at 1500 samples/s; heuristic: well under the
   12 ms dot at 100 WPM, long enough for nearly independent noise). Reading speed-specific filters instead would
   score hypotheses at different speeds on different data, and overlapping samples would count evidence several
   times.
2. **Each hypothesis does its own matched filtering:** it adds up the pieces it claims fall inside its current mark
   (one complex running sum). A Morse carrier keeps its phase through a mark, so the sum is what a filter matched to
   that mark would give (about 10·log₁₀ n dB gain over one piece for n pieces, derived). The score is the standard
   carrier-of-unknown-phase-plus-noise against noise-alone statistics, with A from topic 3; gaps are scored as noise.
   Edges fall where the hypothesis puts them (to one piece); there are no filter delays; speeds between grid values
   do not matter for the observation.
3. **The time step** is the piece length, about 2 ms.
4. **Keying history (topic 1, item 7):** only the running sum of the current mark.
5. **The bank is not used by the new decoder** (owner): signal level and noise level come from the pieces too
   (topic 3, amended). The existing bank decoder stays frozen as the yardstick until the new decoder beats it.
6. **Standard parts:** the noncoherent matched filter (textbook communications theory), segment scoring in hidden
   semi-Markov models, per-survivor estimates (Raheli, Polydoros and Tzou, 1995). The combination for Morse is ours;
   no published Morse decoder doing exactly this is known to us (not searched specifically).

Risk: it relies on the carrier's phase staying steady through a mark. A residual frequency error after mixing to
0 Hz rotates the phase (0.5 Hz over a 720 ms dash at 5 WPM: about a third of a cycle, derived), losing part of the
gain at slow speeds; the channelizer's frequency hold is to be measured before relying on it.
