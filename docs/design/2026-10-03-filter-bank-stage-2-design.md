# Filter bank, stage 2: design changes before the C++ implementation

Status: **draft for the owner's review, 2026-10-03.** Designed with the owner on 2026-10-03 (the
decision record, with the reasons and the rejected options, is
`docs/design/2026-10-03-stage-2-design-decisions.md`). It amends the approved filter-bank design,
`docs/design/2026-09-30-filter-bank-speed-estimator-design.md` (here "the stage-1 spec"), for what
stage 1 found (`docs/plans/2026-09-30-milestone-2b-stage-1-results.md`, "the results record"). Where
this document and the stage-1 spec differ, this document governs stage 2. Owner's choice of path
(2026-10-02, option E2): revise the design first, then implement the bank in C++ (`--front-end bank`)
and tune it there.

Conventions as in the stage-1 spec: S₅₀₀ is key-down carrier power over noise power in 500 Hz, in dB;
T is a station's dit, WPM = 1.2 s / T. Every value is marked **derived**, **measured**, **heuristic**
or **placeholder** (a heuristic that a stage-2 measurement sets). New here: every time constant also
carries a **class**: **(1) dits** (it collects enough Morse elements, so it scales with the dit),
**(2) seconds** (it tracks something that changes in real time, or is computational), with its reason.

## 1. Why

Stage 1 measured the prototype against Matched on oracle channels at −0.078 (−0.088 to −0.067) paired
CER, and found four design problems that a C++ port would otherwise carry:

1. **The decoder is not time-base invariant.** From 25 to 12 WPM every CER crossing must move 3.2 dB of
   S₅₀₀ lower for a decoder whose behavior depends only on the energy per dit, E/N₀ = S₅₀₀ × 500 Hz × T
   (derived; results record §5.1.1). The prototype's CER-0.10 crossing moved 0.1 dB and Matched's
   1.3 dB (measured). Suspects: settings fixed in seconds rather than in dits.
2. **New overs are decided by silence length alone,** which cannot separate a pause inside a
   transmission from a turnover to another station. Farnsworth word gaps started false overs in 16 of
   48 transmissions (E7), a clean station loses its first character with the 0.8 s re-key wait (E9b),
   and same-track QSO decoding is worse than Matched (results §5.3).
3. **Speed jumps within an over:** no switch persistence follows a 15 → 30 WPM step within the
   10-mark target (E6), and the text between the jump and the new branch's eligibility is never
   corrected.
4. **The displayed text is not measured:** stage 1 scored only the final text after corrections.

## 2. Principle: time constants in dits unless stated otherwise

Inside branch k, "a dit" means the branch's **nominal dit d_k = L_k / 0.8**, where L_k = 9.6 ms ×
1.1^(k−1) is its filter length (stage-1 spec §4.1). d_k is a constant of the branch, not an estimate:
d_1 = 12 ms (100 WPM) to d_32 = 230 ms (5.2 WPM). A time constant is expressed in nominal dits unless a
physical reason, stated with it, calls for seconds. Values are chosen so that **at 25 WPM (d = 48 ms)
they equal stage 1's**, so stage 1's measurements remain the reference at that speed.

## 3. Changes

### 3.1 Settings converted to dits (owner, 2026-10-03)

- **Re-key wait W_min,k = 16.7·d_k of key-down time** (was 0.8 s; 0.8 s / 48 ms = 16.7). Per branch.
- **Re-key time-out = 2.5 × W_min,k of wall-clock time = 41.7·d_k** (was 2 s; 2.5 is stage 1's ratio
  2.0 s / 0.8 s). Reason for tying it to W_min: a genuine station is keyed down about 44% of the time,
  so it needs about W_min / 0.44 of wall-clock time to reach W_min (derived); a fixed 2 s time-out
  would fire first at 12 WPM once W_min is in dits. Values: 4.2 s at 12 WPM, 2.0 s at 25 WPM, 1.25 s at
  40 WPM. After a slow station stops, false characters stay visible up to about 4 s (inside the 20 s
  correction reach).
- ~~**Periodicity windows: each candidate dit T is judged over its own window of N_w·T,** with
  N_w ∈ {41.7, 104, 208}.~~ **Withdrawn (owner, 2026-10-06): the periodicity windows stay in seconds**
  (§3.2). Reason: several windows exist so that the estimator reacts quickly after a change (a stream's
  start, a new over, a speed change), which is a latency, felt in seconds; the speed-dependent part,
  that a slow station's rhythm needs a longer window to be seen at all (the comb reaches 9.15·T and
  must fit in half the window, so a window W measures only T ≤ W/18.3), is physics that any window
  rule must wait for. Evidence (Plan B task B4a, results record §6 and
  `docs/research/2026-10-05-periodicity-windows-and-rekey-analysis.md`): windows of N_w·T per
  candidate cost paired CER of about +0.02 and let the alias at 3T win 10–20% of short windows; a
  shared window of N_w·T̂ was not significantly better than the fixed windows (−0.0033,
  −0.0071 to +0.0001) and ties the window to the decoder's own speed belief.

### 3.2 Settings kept in seconds, each with its reason (class 2)

| Setting | Value | Reason |
|---|---|---|
| False-mark rate R_fa of the unknown-amplitude test | 0.01 per second, per branch | The user sees false characters per minute of silence. The per-second calibration already gives slow branches the lowest thresholds (x_on from 4.64, branch 1, to 4.20, branch 32; E9a), so it cannot explain the slow-speed deficit (derived). Owner, 2026-10-03: a stated exception. |
| Noise average τ_n and its warm-up | 2 s; 0.32 s | Measured on branch 1 (9.6 ms), about 100 independent samples per second at any station speed; band noise changes in real time. |
| Noise-spectrum segment and smoothing | 171 ms (5.86 Hz bins); ±25 Hz | Properties of the channel and the noise, not of the station's speed. |
| Outlier durations of the fit | 1 ms to 10 s, log-uniform | A deliberately broad prior, not an estimator. |
| Estimate update block | 21.3 ms | Computational. |
| Periodicity update | every 0.25 s | Latency. |
| Periodicity windows | 2, 5, 10 s | Latency after a change (a stream's start, a new over, a speed change); a slow station's estimate comes from the longer windows (a window W measures T ≤ W/18.3: 2 s down to 11 WPM, 5 s down to 4.4 WPM, 10 s down to 2.2 WPM). Owner, 2026-10-06 (§3.1). |
| Correction reach | 20 s | What a reader tolerates (owner). |

Already counted in elements or characters, unchanged: fit memory 48 elements (8 before a fit counts);
text window 10 characters.

### 3.3 Noise-spectrum guard margin tied to branch 1's filter (owner, 2026-10-03)

The spectrum leaves out every sample whose branch-1 outputs, or any within the guard margin of them, are
flagged as a mark. With stage 1's 20 ms margin this adds about 57 ms to every gap (20 ms plus L_1 on
each side: N₁ − 1 + R samples per side, 2 · (13 + 30) = 86 samples at 1500 samples/s; erratum, owner,
2026-10-06: this said "about 50 ms … 20 ms plus L_1 after", counting L_1 once), so element spaces are fully excluded at 25 and 40 WPM, and the clean fraction while a
station sends is about 21% (40 WPM), 28% (25 WPM) and 42% (12 WPM) (derived, PARIS); with the 50%
acceptance rule the spectrum's shape then updates mostly between transmissions at high speed. The
margin covers what the flag misses near a mark (keying transients, sub-threshold edges): properties of
the filter and the transmitter, not of the station's speed. **New margin: 0.5·L_1 = 4.8 ms**
(heuristic); a gap then loses 2 · (13 + 7) = 40 samples, 26.7 ms (derived). Because the mask changes, the per-branch mask-bias table b_mask,k must be re-measured
(stage 1, Task 5 method), and stage 2 reports accepted segments per second at 12, 25 and 40 WPM.

### 3.4 Amplitude average τ_a: decided by measurement in stage 2 (owner, 2026-10-03)

Stage 1: 0.5 s of key-down time (about 1.1 s of wall-clock time at 44% key-down), on about
n = τ_a / L_k independent samples: 21 at 40 WPM, 13 at 25 WPM, 6 at 12 WPM (derived), so at 12 WPM the
estimate has twice the variance. In dits it follows fading less well at slow speeds: under Rayleigh
fading the amplitude stays correlated for about 0.38 / f_D (3.8 s at 0.1 Hz, 1.3 s at 0.3 Hz; derived).
Variants measured in stage 2: **(i) 0.5 s of key-down time** (class 2) and **(ii) 10.4·d_k of key-down
time** (class 1; 0.5 s at 25 WPM, 1.04 s at 12 WPM). Rule, written now (placeholder until the owner
confirms it in the plan): adopt (ii) if its paired CER against (i) on the stretch test (§4.1) has an
interval entirely below 0 and no fading-group regime (group B) has a paired interval entirely above 0;
otherwise keep (i). If (ii) helps the stretch test but hurts fading, report to the owner; the fallback
is an amplitude tracker that adapts its averaging time to the measured fading rate (Kalman type).

### 3.5 New overs: two hypotheses decoded in parallel (owner, 2026-10-03)

Replaces stage-1 spec §4.7's silence threshold T_new = max(0.5 s, 12·T_g), and with it the 0.5 s floor.
T_g is the gap unit of the branch's duration fit (about one dit; longer for Farnsworth spacing).

- **Trigger:** in each branch, a silence longer than **8·T_g** (heuristic; a word space is about 7·T_g)
  starts two hypotheses: **"same over"** (the previous over's amplitude a_prev and fit, so the first
  marks are keyed with the full likelihood at once) and **"new over"** (amplitude unknown and a fresh
  fit, as stage 1 does: unknown-amplitude test, re-key after W_min).
- **Prior:** neutral, 50/50, whatever the silence length (silence length is weak and not monotonic
  evidence: the suite's turnovers come after 0.5–2 s, its CQ loops repeat the same station after
  2–20 s). To be learned from real recordings later.
- **Evidence:** Λ = ln P(observations | new) − ln P(observations | same), in nats, accumulated over the
  marks after the trigger; with the neutral prior it is the log posterior odds. "Same": the likelihood
  at a_prev (the Rician envelope likelihood of stage 1). "New": the **marginal likelihood** over the
  unknown amplitude, with a log-uniform prior over the carrier power from S₅₀₀ = −10 to +40 dB mapped to
  the branch's output, on a grid of about 30 amplitudes (heuristic grid; the prior range is the range
  the skimmer must handle). Timing: stage 1's comparison of the previous fit with a fresh fit
  (at least 8 observations and ½·k·ln n), unchanged.
- **Decision:** when |Λ| > **4.6 nats** (posterior 99%; heuristic) the winner is declared and the loser
  dropped. **Cap:** the undecided stretch lasts at most **15 s** of wall-clock time, so the decision
  always falls inside the 20 s correction reach (class 2: tied to the reach); at the cap the more likely
  hypothesis by Λ wins, exact ties to "same". **At most two hypotheses:** a new trigger while undecided
  forces the pending decision (the more likely wins) and starts a fresh pair.
- **Publication:** the "same over" text is shown throughout the undecided stretch; if "new" wins, the
  text is corrected once.
- **Scope:** per branch, as in stage 1; each branch keeps both states (amplitude, keying, fit) and
  decides its own Λ; branch selection decides whose text is shown.
- **Cost:** up to about twice the per-branch work for up to 15 s after each trigger (to be measured).
- Expected effects, all hypotheses to be measured: fewer false new overs (Farnsworth, pauses), turnovers
  no longer missed, and no lost first character when the station did not change.

### 3.6 Corrections after a branch switch reach back to the speed change (owner, 2026-10-03)

Stage-1 spec §4.8 corrects from the character where the new branch became eligible. Added: after a
switch, the correction reaches back to the **estimated time of the speed change**, within the 20 s
reach. Estimator (proposal, heuristic): the split point among the selected branch's recent marks and
spaces that maximizes the likelihood of "old fit before, new fit after". Within an over, following a
jump is **measured and reported**, no longer required within 10 marks; the fit memory stays 48. Turnovers
to a different station are handled by §3.5 (a "new over" starts a fresh fit).

## 4. Stage 2 validation additions

Stage-1 spec §7 (stage 2) stands: the bank as `--front-end bank`, the full comparison against Matched
and Envelope on the 3-seed suite with no acceptance gate, and the detection measures confirmed behind the
live detector with the frequency tracker. Added:

### 4.1 Stretch test (time-base invariance)

For group A's 25 WPM conditions, a stretched copy: the same text and keying timeline scaled in time by
2.08, so 12 WPM, at S₅₀₀ lowered by 10·log₁₀ 2.08 = 3.19 dB, so the energy per dit is unchanged. A
time-base-invariant decoder gives the same CER per signal on both (derived). Measure: paired CER
(stretched − original) per S₅₀₀ point, and the CER-0.10 crossing shift (target 3.2 dB of S₅₀₀).

### 4.2 Displayed text (owner, 2026-10-03: minimal)

Per group, for the bank beside Matched and Envelope: the CER of the text as displayed the instant each
character is first published, and the final CER; plus correction statistics (corrections per
channel-minute, characters changed, reach: median, 99th percentile, maximum). Computed by replaying
the engine's text events with corrections.

### 4.3 New-over checks

On group H (QSO turnovers) and the `pauses` group: false new overs per transmission, missed turnovers
per QSO turnover, and first-word CER, to check the 8·T_g trigger and the 4.6-nat threshold.

### 4.4 Cost

Before the stage-2 plan is approved: the measured CPU cost of a representative slice of the C++ bank,
and the projected cost of every planned run (owner, 2026-10-01: Python or C++ decided case by case).

## 5. Investigate first in stage 2 (no design decided)

1. **A strong neighbor 20–150 Hz away** (group E): worse than Matched by up to +1.9 CER, the largest
   loss (results §5.3). Conjecture, not traced: the long filters pass the neighbor's keying.
2. **Crowded channels** (0–100 Hz spacing): worse than Matched; probably related to item 1.
3. **The Farnsworth gap fit with the coarse grids** fits T_g 7.2% low (strict expected failure); the
   adopted T_g/T grid's steps are about 59% apart (1, 1.59, 2.52, 4, 6.35), finer refinement may suffice.
4. **The 12 WPM cliff and the 10 WPM losses** (results §5.1, §5.3): expected to change with §3.1–3.4;
   the stretch test measures it.
5. **Two C++ cleanups** from stage 1's final review (results §6), done in the port.

## 6. Deferred

- **The text model in character decisions** (owner, 2026-10-03): after stage 2, with real recordings
  scored against real spots. Main risk: pulling unusual callsigns toward plausible but wrong text.
- **The full displayed-text curve** (CER against delay after publication) and the decision of when a
  callsign is trustworthy enough to spot: the callsign-matching and spotting milestones.

## 7. Parameters changed or added by this document

| Parameter | Stage 1 | Stage 2 | Class | Status |
|---|---|---|---|---|
| Re-key wait W_min,k | 0.8 s of key-down | 16.7·d_k of key-down | (1) dits | measured at 25 WPM (E9b), scaled (derived) |
| Re-key time-out | 2 s | 2.5 × W_min,k = 41.7·d_k, wall-clock | (1) dits | heuristic ratio (stage 1's) |
| Periodicity windows | 2, 5, 10 s | unchanged: 2, 5, 10 s (the dits form, N_w·T per candidate, withdrawn by the owner 2026-10-06, §3.1) | (2) seconds: latency | placeholder (stage 1's) |
| Periodicity confidence thresholds | comb 0.03 | unchanged, 0.03 (no re-measurement needed: the windows stay as stage 1's; owner, 2026-10-06) | — | placeholder |
| Amplitude average τ_a | 0.5 s of key-down | (i) 0.5 s or (ii) 10.4·d_k, by the rule of §3.4 | decided in stage 2 | placeholder |
| Noise-spectrum guard margin | 20 ms | 0.5·L_1 = 4.8 ms | (2) filter | heuristic |
| Mask bias b_mask,k | measured with the 20 ms margin | re-measured | — | measured (stage 2) |
| New-over rule | silence > max(0.5 s, 12·T_g) | two hypotheses after silence > 8·T_g | (1) dits | heuristic |
| New-over decision threshold | — | \|Λ\| > 4.6 nats | — | heuristic |
| New-over cap | — | 15 s | (2) tied to the 20 s reach | heuristic |
| "New" amplitude prior | — | log-uniform, S₅₀₀ −10 to +40 dB, about 30 grid points | — | heuristic |
| Correction start after a switch | eligibility point | estimated speed change (maximum-likelihood split) | — | heuristic |
| False-mark rate R_fa | 0.01 /s | unchanged | (2) seconds (stated exception) | heuristic, kept by E9b |
