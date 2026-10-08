# Decoder chain audit (2026-10-07)

Asked by the owner after the window and re-key experiments of Plan B (B4 to D5) kept trading one failure for
another: what are we doing wrong, what is a poor way of doing things, and should the bank decoder be continued
with smaller changes or rethought? Priorities set by the owner: noise level, speed changes, the fastest and
slowest speeds, and overs; poor fists and Farnsworth spacing in the middle; neighbors and fading later. Claims
are labeled **measured** (with the record), **derived**, or **judgment**. All S₅₀₀ values are dB SNR in 500 Hz.

## 1. What the chain is

Detector → channelizer → one decoder per channel. The detector and channelizer (signal-processing.md §6–§7) are
not the problem for the owner's priorities and are left out of this audit, apart from §5's tests. The bank
decoder (§8c) runs, per channel:

1. 32 boxcar filters, one per speed (9.6–184 ms, 100–5 WPM); a likelihood ratio and a key-down probability
   per sample on each.
2. **A hard key decision per branch**, with ±1 nat hysteresis.
3. **Amplitude bootstrapping**: at the stream's start and after every new over the amplitude is unknown; the
   branch keys with a fixed threshold, collects key-down time, estimates the amplitude, and re-keys the over;
   a time-out deletes or re-keys what it cannot confirm.
4. **A duration fit per branch** on the hard marks and spaces (log-normal durations around dit, dah and three
   gap classes; grid plus Gauss–Newton).
5. **A separate speed estimate T_P** (the periodicity comb on branch 1's probability), used as a prior on every
   fit and as selection's fallback.
6. **Selection** of one branch to publish (fit quality, text score, length; four instants of persistence).
7. **Overs**: a long key-up resets a branch (fresh fit, unknown amplitude).
8. **Corrections**: on a branch switch, a re-key or a time-out, up to 20 s back.

## 2. Scorecard against the owner's priorities

New development set, seed 1, bank with the current defaults (`d5-bank`), against the ideal decoder that knows
the timing (noncoherent bound, CER 0.10 needs E/N₀ = 11.9 dB per dit; derived, stage-1 record §5.1).
Development-set record §5–§6.

| Priority | Measured | Gap or symptom |
|---|---|---|
| Noise (white), mid speeds (20–25 WPM) | crossing +0.14 dB | 2.5 dB above the bound (−2.4 dB) |
| Slowest (8–10 WPM) | crossing −0.92 dB (fixed only by dropping the 2 s window, D5) | **5.4 dB** above the bound (−6.3 dB) |
| Fastest (64–80 WPM) | crossing +6.25 dB; only the bank decodes there at all | 3.6 dB above the bound (+2.65 dB) |
| Time-base invariance | 12 WPM needs 2.7–3.7 dB more energy per dit than 25 WPM in every variant tried (B9) | the slow-speed gap above |
| Speed changes (D2) | better than Matched; corrections reach only from a switch's eligibility point | text before the switch stays wrong |
| Overs, first words | **first-word CER on the final text 0.58–0.85 in every speed cell** (D5); 23–31% of turnovers missed and 0.05–0.10 false new overs per transmission (B9, old set) | the start of every transmission is mostly wrong |
| Poor fists | worse than machine keying; dropping the 2 s window (D5) cost fists up to +0.099 | middling |
| Farnsworth | the bank is far better than Matched (I2: −0.46, baseline) | middling |

**Judgment:** the steady state at 15–50 WPM is decent. The failures are at the edges the owner cares about —
starts of transmissions, slow speeds, speed changes — and they are the same failures each Plan B task has moved
around rather than removed. For a skimmer, whose product is a callsign usually sent at the start of a short
transmission, a first-word CER of about 0.6 is the most serious number in the table.

## 3. Root cause: hard decisions at the seams

Every failure traced in Plan B sits where one stage hands a **hard decision** on partial information to the next
(measured traces: `b4-rekey-trace.md`, `slow-trace.md`, B4d–B4g, D5):

| Failure | Seam | Trace |
|---|---|---|
| Pre-station noise published as "I EEEE" | the provisional key decision feeds a re-key that reaches back into noise | B4a re-key trace |
| Time-outs firing before a station's own wait | amplitude bootstrap clocks vs. a station's start | B4a re-key trace |
| Slow stations decoded at a third of their speed | the 2 s window's confident-but-wrong T_P becomes a hard prior on every fit | slow trace |
| First words permanently wrong without a 2 s T_P | early text is never revisited when the speed estimate arrives | D5 |
| Neighbor leakage re-keyed and selected | a mark count or wait is met by another station's keying | B4f |
| 3T aliases | per-candidate windows vs. "shortest confident window wins" | B4a analysis |

The pattern (judgment, supported by those traces): the bank decides *key up or down* before it knows the speed,
decides *the amplitude* before it knows where the station starts, decides *the speed* three times in three
places (the ladder, each fit, T_P) and reconciles them with heuristics, and decides *where an over starts* by a
silence threshold. Each decision is made once and then only revised by special-case corrections. When one is
wrong, the next stage cannot recover, so we patch the hand-off — and the patch moves the failure to another
condition. That is the circling.

## 4. What is sound, and what is a poor way of doing things

**Sound, worth keeping** (judgment, with measured support):
- The detector and channelizer, for these priorities.
- **The filter bank as a front end**: one matched filter per speed gives every speed hypothesis its own
  noise bandwidth and its own soft likelihood; it is why the bank decodes 64–80 WPM where Matched cannot.
- The noise estimate (level from branch 1, ratios from the masked spectrum), exact zeros as missing data.
- **The duration model**: log-normal element and gap durations around T + w, qT + w, T − w, 3T_g − w and
  7T_g − w, with VE3NEA's priors — exactly what a sequence model needs.
- The unigram text model, the development set, the stretch test, the score tables and the analysis tooling.

**Poor ways of doing things** (judgment):
1. **Hard keying before timing.** Soft information (how likely key-down was) is discarded at the ±1 nat
   threshold; a dip splits a dah, a noise burst merges two dits, and the fit and the classification inherit
   it. The decoder survey's main finding (`decoder-survey.md`, "Hard decisions throw away…") is the same.
2. **The amplitude bootstrap.** Provisional keying, the wait, the time-out, re-keying, and now clock fixes and
   guards: about eight interacting mechanisms, four tasks of variants (B4a, B4d–B4g), none better than stage 1's
   seconds. The stage-2 design already contains the cleaner idea: the likelihood **marginalized over a
   log-uniform amplitude prior** (§3.5), which needs no "unknown amplitude" mode at all.
3. **Three speed estimators reconciled by heuristics** (ladder selection, per-branch fits, the periodicity
   comb as a hard prior with a 0.03 placeholder threshold and window rules).
4. **Overs by a silence threshold that resets state**, throwing away speed and amplitude knowledge exactly
   when the next station's first word arrives.
5. **Corrections as special cases** (switch, re-key, time-out) instead of a decoder whose best interpretation of
   the last seconds simply changes as evidence arrives.
6. **The text model only as a tie-breaker** between branches, never inside the character decision.
7. **Parameters**: the bank has 46 parameters in signal-processing.md A.10; their class labels include 31
   "heuristic" and 11 "placeholder" (some rows carry several). One-at-a-time tuning of interacting heuristics on
   a development set is slow and, as Plan B showed, circular.

## 5. Recommendation: rethink the back end, keep the front end

Not a restart. **Replace stages 2–8 (hard keying, amplitude bootstrap, per-branch fits, T_P, selection, overs,
corrections) with one probabilistic sequence decoder that consumes the bank's soft likelihoods** (judgment):

- **State:** the current element or gap (dit, dah, element space, character gap, word gap, a long "between
  overs" gap), the position within the character, the speed (dit length on a log grid, e.g. the 32 branches or a
  finer grid), the time spent in the current element (a hidden semi-Markov model, HSMM).
- **Observation:** at each block, the key-down likelihood from the branch matched to the state's speed. Each
  speed hypothesis is judged with its own filter and its own durations in its own dits, so time-base invariance
  holds by construction (derived from the structure, to be measured).
- **Amplitude:** marginalized over a log-uniform prior, or estimated per hypothesis — no provisional mode, no
  re-key, no time-out.
- **Speed changes:** allowed transitions between speed states at element boundaries (as Bell 1977), with a
  larger allowance after a long gap — so a new over may change speed and level without a reset.
- **Text:** the unigram (or later an n-gram or callsign) model as the prior over character sequences.
- **Decoding:** beam search with a fixed lag; the published text is the best path, and it changes when a better
  path overtakes — corrections are automatic, bounded by the 20 s reach.
- **What carries over:** the filter bank, the noise estimate, the duration model, the priors, the text model,
  the development set and its analysis.

**Evidence for and against** (from `decoder-survey.md`): Bell's 1977 sequence decoder handled speed changes and
poor fists in simulation at −1 to −3 dB at 20 WPM; manta's `hsmm` is a working open-source example (beam of 12,
about 3.5 ms CPU per track-second); VE3NEA described CW Skimmer's approach as carrying probabilities "all the way
to the word recognition unit". Against: no published result shows a sequence decoder beating a well-built
hard-decision decoder in *sensitivity* by more than the SNR-definition ambiguity — the expected gains are in
robustness (starts, speed changes, fists), not necessarily in the steady-state cliff. The CPU cost is unknown.

**How to decide without another circle** (proposal for the owner): freeze the bank as the reference (it beats
Matched by −0.064 CER on the new set); prototype the sequence decoder in Python offline on the bank's recorded
likelihood streams, as stage 1 did, time-boxed; pre-register the comparison on the new development set's
priority groups — A2 (noise × speed), D2 (speed changes), G2/H2 (overs), C2/I2 (fists, Farnsworth) — by first-word
CER on the final text, the crossings per speed cell, and CPU per channel-second. If the prototype wins on first
words and slow speeds without losing the steady state, it replaces the back end; if not, Plan B continues with
B5–B7 as small changes. Plan B's remaining stage-2 tasks would pause until then (B8, the tracker, and B10,
neighbors, stay relevant either way).

## 6. Tests the owner may be missing

Beyond noise, speed changes, the speed extremes, overs, fists and Farnsworth (judgment, in order of importance):

1. **Real off-air recordings.** Every test so far is synthetic. The backlog's "manta oracle" item: real contest
   audio with spots as approximate labels.
2. **Short transmissions and callsign accuracy.** A skimmer's product is a callsign, usually in a short
   transmission ("CQ TEST K1ABC", exchanges). A whole-callsign-correct rate on short overs measures what users
   see; the development set's 100-character signals do not.
3. **High-speed CW above 80 WPM** (the owner's interest): the bank supports 100 WPM; the set stops at 80.
4. **Noise without a station**: false characters and false tracks per hour in pure noise, and in impulsive noise
   (static crashes) and non-white noise.
5. **Keying artifacts**: clicks, chirp, very short QSK pauses, tune-up carriers, a station that starts
   mid-character (the old groups had some; the new set does not).
6. **Through the detector path at low SNR, and drift beyond the channel** (backlog items).
7. **Scale and duration**: hundreds of channels at once, CPU per channel, and hours of continuous running.
