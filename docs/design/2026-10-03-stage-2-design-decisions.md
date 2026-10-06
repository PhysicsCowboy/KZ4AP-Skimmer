# Stage 2 design session: decisions so far (working notes)

Working record of the owner's design decisions for stage 2 of the filter-bank redesign
(`docs/design/2026-09-30-filter-bank-speed-estimator-design.md`), taken one topic at a time. It feeds the
spec revision the owner approves before any stage-2 plan. Uncommitted until the owner approves a commit.

## 1. Fixed-times audit (started 2026-10-03)

Why: the decoders are not time-base invariant (results record, section 5.1.1): from 25 to 12 WPM the
CER-0.10 crossing should move 3.2 dB of S₅₀₀ lower (derived) and moved 0.1 dB (prototype), 1.3 dB
(Matched). Suspects: settings fixed in seconds. Classes: (1) scales with the dit (inside branch k, with
its nominal dit d_k = L_k / 0.8); (2) physical, seconds are right; (3) unclear, decided one by one.

Owner agreed (2026-10-03):

- **Converted to dits:** re-key wait W_min = 0.8 s of key-down time → about 17 nominal dits of key-down
  per branch (0.8 s / 48 ms; unchanged at 25 WPM); periodicity windows 2, 5, 10 s → a window of N·T for
  each candidate dit T (for example 40 T), which also removes the comb's reach limit (today the 2 s
  window cannot detect dits above 109 ms, about 11 WPM).
- **Kept in seconds (class 2):** noise average τ_n = 2 s on branch 1 and its 0.32 s warm-up; FFT segment
  171 ms and ±25 Hz smoothing of the noise spectrum; outlier durations 1 ms to 10 s; update blocks
  21.3 ms; periodicity update 0.25 s; correction reach 20 s (owner).
- **Already invariant:** fit memory 48 elements (8 before a fit counts); text window 10 characters.
- **Moved to class (3) at the owner's point:** the noise spectrum's 20 ms guard margin. With branch 1's
  smear it adds about 50 ms to every gap, so element spaces are fully excluded at 25 and 40 WPM and the
  clean fraction while sending is about 21% (40 WPM), 28% (25 WPM), 42% (12 WPM) (derived, PARIS); the
  spectrum shape then updates mostly between transmissions at high speed. Proposal attached: tie the
  margin to branch 1's filter (about 0.5·L₁ ≈ 5 ms, heuristic), re-measure the mask-bias table, and
  check accepted segments per second against speed in stage 2.

Class (3) decisions:

- **Amplitude average τ_a (owner, 2026-10-03): decide by measurement in stage 2.** Today 0.5 s of key-down
  time (about 1.1 s of wall-clock time at 44% key-down). In dits it would be about 10.4 nominal dits of
  key-down, 13 filter lengths (n = τ_a / L_k independent samples: 21 at 40 WPM, 13 at 25 WPM, 6 at 12 WPM today, so the
  12 WPM estimate has twice the variance; derived). In seconds it follows fading of correlation time
  about 0.38 / f_D (3.8 s at 0.1 Hz, 1.3 s at 0.3 Hz; derived), and in dits the 12 WPM span would grow to
  about 2.4 s. Stage 2 measures (a) seconds against (b) dits on the stretch test and on fading group B
  at 12 and 24 WPM, by a rule written before the runs. Fallback if neither is acceptable: an amplitude
  tracker that adapts its averaging time to the measured fading rate (Kalman type).

- **False-mark rate R_fa (owner, 2026-10-03): keep per second (class 2), a stated exception to "dits by
  default".** Reasons: the user sees false characters per minute of silence, a time rate; and the
  per-second calibration (x_on,k from 4.64 for branch 1 to 4.20 for branch 32, E9a) already gives slow
  stations' branches the lowest thresholds, so it cannot explain the slow-speed deficit (derived). The
  invariant alternative, one x_on for all branches (a fixed false-mark probability per nominal dit),
  would raise slow branches' thresholds and give fast branches more false marks per second.

- **Re-key time-out (owner, 2026-10-03): tie it to W_min, time-out = 2.5 × W_min of wall-clock time.**
  Today 2 s; with W_min in dits (about 17 nominal dits of key-down) a genuine 12 WPM station would need
  about 3.8 s of wall-clock time to reach W_min (W_min / 0.44 key-down fraction; derived), so a fixed 2 s
  time-out would re-key its over with the previous over's amplitude. 2.5 is today's ratio (2.0 s /
  0.8 s), so 25 WPM is unchanged; 12 WPM gives 4.2 s, 40 WPM 1.25 s. Cost: after a slow station stops,
  false characters stay visible up to about 4 s (inside the 20 s correction reach). Possible refinement
  if this misbehaves: a rate test (noise keys about 0.01 times per second, a station is down about 44%
  of the time).

- **New-over threshold T_new = max(0.5 s, 12·T_g) (owner, 2026-10-03): not settled in the audit; to be
  redesigned in topic (b), "how the decoder decides that a new over has started".** Reasons: the 0.5 s
  floor is active only above about 29 WPM (12·T_g < 0.5 s), so it is not the cause of the Farnsworth false
  over starts (group I sends characters at 18 WPM or slower, 12·T_g ≥ 0.8 s); and no silence threshold can
  separate hesitation pauses from turnover gaps, whose durations overlap. The owner asked whether the
  wait should be even longer; a longer wait trades fewer false new overs for missed station changes.
- **Guard margin (owner, 2026-10-03): the proposal above is accepted** (tie to branch 1's filter, about
  0.5·L₁, re-measure the mask-bias table, check accepted segments per second against speed).

**The audit is closed (owner, 2026-10-03).** Outcome: W_min, the periodicity windows and the re-key
time-out scale with the dit; R_fa stays per second (stated exception); τ_a is decided by measurement in
stage 2 (Kalman-type tracker as fallback); the guard margin is tied to branch 1's filter; the new-over
rule moves to topic (b); everything else stays in seconds for the stated reasons or is already counted in
elements or characters.

## 2. Topic (b): how the decoder decides that a new over has started (started 2026-10-03)

Errors today (silence length alone, T_new = max(0.5 s, 12·T_g)): a false new over (a pause inside one
transmission; Farnsworth 16 of 48 transmissions in stage 1) costs threshold-keyed first marks and
correction churn; a missed new over (another station answered on the same channel) decodes the reply
with the previous station's amplitude and fit (same-track QSO decoding was worse than Matched).

- **Approach (owner, 2026-10-03): keep both hypotheses running.** After a qualifying silence, decode in
  parallel as "same over" (previous amplitude and fit, so the first marks get the full likelihood at
  once) and "new over" (fresh estimates, as today); publish the more likely at each moment; when the
  evidence settles, drop the other and correct the text. Extends stage 1's fresh-fit-against-previous-fit
  comparison to the amplitude and to the over decision itself. Expected to address the lost first
  character (hypothesis, not measured). Cost: about twice the per-branch work during the undecided
  stretch. Rejected: retuning the silence threshold (only trades the two errors); a single test after
  the silence (first marks still threshold-keyed until it decides).
- **Trigger (owner, 2026-10-03): a silence longer than 8·T_g starts the two hypotheses.** Between a word
  space (about 7·T_g) and the 10·T_g proposed; scales with the station's fitted gap unit, so Farnsworth's
  stretched word spaces do not trigger when T_g is fitted correctly. Heuristic; a false trigger now costs
  only CPU (the "same over" hypothesis keeps decoding correctly). To be checked in stage 2.
- **Prior (owner, 2026-10-03): neutral, 50/50 whatever the silence length; learn it from real recordings
  later.** Silence length is weak and not monotonic evidence: the suite's QSO turnovers come after
  0.5–2 s (`generate.py`, `turn_s`), while the `pauses` group repeats the same station after 2, 5, 10 and
  20 s, as real CQ loops do. The prior only affects which text is shown while undecided and when the
  decision is declared; both hypotheses decode every mark, and a wrong display is corrected later.
- **Evidence for "new" (owner, 2026-10-03): the marginal likelihood over the unknown amplitude**, with a
  broad prior (log-uniform, S₅₀₀ from −10 to +40 dB), computed on a grid of about 30 amplitudes per mark;
  this penalizes the free parameter from first principles. Evidence for "same": the likelihood at the
  previous over's amplitude a_prev (Rician, as in stage 1). Timing: stage 1's comparison of the previous
  fit with a fresh fit, unchanged. Λ = ln P(observations | new) − ln P(observations | same), in nats;
  with the neutral prior it is the log posterior odds. Rejected: maximum likelihood with a BIC penalty (an
  asymptotic approximation, poor over the first few marks); a fixed amplitude-difference rule.
- **Decision (owner, 2026-10-03; the owner prefers waiting longer for the right text):** declare the
  winner when |Λ| > 4.6 nats (posterior 99%; heuristic, to be checked in stage 2 on group H turnovers
  and the `pauses` group) and drop the loser. Cap: the undecided stretch lasts at most 15 s of wall-clock
  time, so the decision always falls inside the 20 s correction reach (5 s margin); at the cap the more
  likely hypothesis by Λ wins, exact ties to "same". At most two hypotheses: a new trigger while
  undecided forces the pending decision (the more likely wins) and starts a fresh pair. Cost: up to about
  twice the per-branch work for up to 15 s after each trigger; corrections up to 15 s late.
- **Publication (owner, 2026-10-03): show the "same over" text throughout the undecided stretch and
  correct once at the decision** (at most one correction per trigger; the least churn; right in the
  common case of hesitations, CQ loops and Farnsworth gaps). Rejected: showing the currently more likely
  hypothesis (flips when Λ wanders near 0); a second, earlier display threshold.
- **Scope (owner, 2026-10-03): per branch, as in stage 1.** Each branch keeps both states (amplitude,
  keying, fit) and accumulates and decides its own Λ from its own marks; branch selection then decides
  whose text is shown, as it already does. Rejected: one decision per channel from the selected branch's
  evidence (a selection change mid-stretch would mix evidence from different filters into Λ). CPU is the
  same either way.

**Topic (b) is complete (owner, 2026-10-03).** Summary: after a silence longer than 8·T_g each branch
decodes "same over" and "new over" in parallel; Λ (marginal likelihood for "new", Rician likelihood at
a_prev for "same", plus stage 1's timing comparison) decides at |Λ| > 4.6 nats or at a 15 s cap (the
more likely wins; ties to "same"); at most two hypotheses; "same" is shown and corrected once. This
replaces T_new = max(0.5 s, 12·T_g), and so the 0.5 s floor, and is expected to address the Farnsworth
false over starts, missed turnovers and the lost first character (hypotheses, to be measured in stage 2).


## 3. Topic (c): following a jump in speed (2026-10-03)

Finding (E6, measured): no switch persistence M follows a 15 → 30 WPM step within the spec's 10 marks
(medians 10.5, 11.5, 14, 16, 18 marks for M = 1, 2, 4, 6, 8); the likely limit is the 48-element fit
memory (conjectured). After a within-over jump, the text between the jump and the point where the new
branch became eligible keeps the old branch's decoding and is never corrected (§4.8 corrects only from
the eligibility point); in group D's 20 → 35 WPM step the prototype's CER is 0.038 (Matched 0.394).

- **Decision (owner, 2026-10-03): B′.** Turnovers are handled by topic (b) (a "new over" starts a fresh
  fit). Within an over, the 10-mark target is relaxed to "measured and reported", and fit memory stays
  48. Added: after a branch switch, the correction reaches back to the **estimated time of the speed
  change**, not only to the eligibility point, within the 20 s reach, so errors after a jump become late
  corrections in the common case. Proposed estimator, to be confirmed in the spec: the split point among
  the recent marks and spaces that maximizes the likelihood of "old fit before, new fit after". Rejected:
  a full in-over change-point hypothesis pair (A), as more machinery than a rare event justifies.

## 4. Topic (d): the text model in character decisions (2026-10-03)

- **Decision (owner, 2026-10-03): deferred until after stage 2**, to a follow-on step designed once the
  C++ bank is built and measured, with real recordings scored against real spots. Reasons: stage 2 is
  already large (the port plus topics (a)–(c)); adding it at the same time would confound the
  measurement; and its main risk, pulling unusual callsigns toward plausible but wrong text, can only
  be judged on real data (the owner shares this concern). The suite's filler text is drawn from the
  model's own character frequencies, which flatters any text model. Options for later: per-character
  soft decisions with an n-gram prior; a sequence decoder (beam search, like the survey's option 3 and
  manta's `hsmm`) with a callsign-aware text model. Backlog item "Let the text model take part in
  character decisions" to be updated to say so.

## 5. Topic (e): measuring the text a user sees (2026-10-03)

- **Decision (owner, 2026-10-03): the minimal version.** Stage 2 reports the CER of the text as displayed
  the instant each character is first published (d = 0) and the final CER, for the bank beside Matched
  and Envelope (which never correct), per group, plus the correction statistics stage 1 already
  produces (count per channel-minute, characters changed, reach). It replays the engine's text events
  with corrections, which stage 2 adds anyway (spec §7). Reason: for stage 2 the question is a regression
  check of the immediately displayed text; the real decision the provisional text bears on, when a
  callsign is trustworthy enough to spot (a busted spot cannot be recalled), belongs to the
  callsign-matching and spotting milestones, which need their own measure (correct and busted spots
  against time to spot). Rejected for now: the full CER-against-delay curve.

## 6. Amendment (2026-10-06): the periodicity windows stay in seconds

- **Decision (owner, 2026-10-06): fixed windows of 2, 5 and 10 s**, withdrawing §3.1's windows of N_w·T
  per candidate. Reasoning, agreed in discussion: several windows exist so that the speed estimate
  reacts quickly after a change (a stream's start, a new over, a speed change); that is a latency, felt
  in seconds, so the windows are class (2), seconds with a stated reason. The speed-dependent part is
  physics: the comb reaches 9.15·T and must fit in half the window, so a window W can measure only
  T ≤ W/18.3 (2 s: 11 WPM and faster; 5 s: 4.4 WPM; 10 s: 2.2 WPM, beyond the decoder's 5 WPM floor),
  and a slow station's estimate necessarily comes from the longer windows. Evidence: Plan B task B4a's
  ablation and option C (results record §6; `docs/research/2026-10-05-periodicity-windows-and-rekey-analysis.md`):
  per-candidate windows cost about +0.02 paired CER and let the 3T alias win 10–20% of short windows; a
  shared window N_w·T̂ was not significantly better than the fixed windows and ties the window to the
  decoder's own speed belief. Spec §3.1, §3.2 and §7 amended.
