# Milestone 2c, Plan B: the stage-2 design changes on the verified bank decoder (DRAFT for the owner's review)

> **Status: draft, written overnight 2026-10-03/04 at the owner's request (Q2 A); the owner's four decisions recorded 2026-10-04 (end of this plan). Nothing executed.** Once approved, each task gets its full step-by-step text (tests, code, commands) in the same form as Plan A before it is dispatched; this draft fixes the scope, order, measurements and decision rules.

**Goal:** Apply the stage-2 design changes (`docs/design/2026-10-03-filter-bank-stage-2-design.md`, "the stage-2 spec") to the C++ bank decoder that Plan A verified against the prototype, make it affordable in CPU and memory, put the frequency tracker in the loop, investigate the regimes where it loses to Matched, and end with the full comparison the owner decides from.

**Architecture:** Every change is measured against the previous reference on the development set (seed 1) with the stage-1 tooling (the C++ replay tool writes the prototype's decoded-file format, so `kz4ap_proto.experiments compare` and its paired CER work unchanged), one change at a time, each with a decision rule written before its run. Changes that should not move any text (speed, memory) are checked with Plan A's discipline (identical texts, or every difference traced). The held-out seeds 2–3 are used only in the final evaluation. Runs go to the Linux machine; at Plan A's measured 128 ms of CPU per channel-second a development run is about 16 minutes of wall time there, and faster after Task B2.

**Spec:** the stage-2 spec (§3 changes, §4 validation, §5 investigations), with the stage-1 spec `docs/design/2026-09-30-filter-bank-speed-estimator-design.md` where it does not differ.

**Reference at the start:** branch `milestone-2c-bank` after Plan A (the faithful port; its development-set run, `bank-cpp`, is the first reference). Branch for Plan B: `milestone-2c-stage2` from `milestone-2c-bank`.

## Global constraints (as Plan A's, plus)

- Every engine signal-processing change updates `docs/signal-processing.md` §8c in the same commit (project rule), with each value derived, measured or heuristic, and each time constant's class (dits / seconds, with its reason; owner, 2026-10-03).
- Envelope and Matched stay bit-identical; smoke unchanged.
- One change per measured step; each step's decision rule is in this plan before its run; held-out seeds untouched until Task B12.
- Wording, units, privacy and git rules as Plan A (no-prompt git recipe; text files via the Write and Edit tools).

## Order and why

Owner's decision (2026-10-04, option C): **pure speed first, the behavior-changing speed-up last.** B0 (CI names); B1 (memory) and B2(a, b) first, because they should not change any text and make every later run cheaper; then the defect fix (B3); then the stage-2 changes in the order of the spec (B4–B8), each measured; then the validation harnesses (B9) that some changes' rules need — **B9 is moved before B5 and B6 in execution**, since their rules use the stretch test and the new-over checks; then the investigations (B10–B11); then **B2(c)**, measured on the final design, because it skips branches far from the selected speed and the two-hypothesis new overs (B6) and the corrections (B7) work per branch; then the final evaluation (B12).

Execution order: B0, B1, B2(a), B2(b), B3, B4, B9, B5, B6, B7, B8, B10, B11, B2(c), B12.

## Tasks

### B0. CI: check names that do not depend on the runner image (no code change)
The `main` ruleset (owner, 2026-10-04) requires the checks `build-and-test (windows-latest, windows)` and `build-and-test (ubuntu-24.04, linux)`. GitHub names a matrix job's check after its matrix values, so changing a runner image (as the 2026-10-02 pin `ubuntu-latest` → `ubuntu-24.04` did) renames the check, and a required check under the old name never arrives and blocks the pull request. Give the job in `.github/workflows/ci.yml` a fixed display name built from the preset only, `name: build-and-test (${{ matrix.preset }})`, so the checks become `build-and-test (windows)` and `build-and-test (linux)` whatever runner image they use. **Check:** the CI run on the branch reports the two new names, both green. **Owner step after the merge into `main`:** in the ruleset, replace the two required checks with the new names (the old names stop reporting from that merge on).

### B1. Memory: share the fit's grid constants (no text change)
Each fresh `DurationFit` allocates its own 433 kB of grid constants (Plan A §8c: 26 to 59 MB per channel, derived). Share one immutable table per configuration across all branches and fits. **Check:** development-set texts identical to the reference (Plan A's discipline); memory per channel derived again and measured (peak resident size per channel on the Linux machine).

### B2. Speed (owner, 2026-10-03: prefer a good open-source library; restructure the arithmetic)
Three candidates, each measured for CPU per channel-second (Linux and Windows) and for its effect on the text:
- (a) **Algebraic restructuring of the fit's grid update**: precompute the terms that do not depend on the observation, and reduce the per-point exp/log/log1p count (75% of the port's time is in them, Plan A Task 9). Exact or near-exact.
- (b) **SLEEF** vectorized exp/log/log1p (open source, builds with MSVC, clang and g++; about 1 ulp): evaluated after (a).
- (c) **Update only branches near the selected speed** (for example within ±k ladder steps, k a measured choice): a behavior change, measured like a design change.
**Rules (owner, 2026-10-04):**
- **(a) and (b), the same rule:** adopt if the development-set texts are identical to the reference, or every difference is traced to a near-tie (a decision whose competing scores differ by less than the rounding change), as in Plan A. (b) is evaluated on top of (a), against (a)'s texts. If the differences are too many to trace in reasonable time, stop and report to the owner with the count; do not fall back to a statistical rule.
- **(c): try, then discuss.** Measure it (paired CER overall and by group, and CPU saving, for each k tried) and bring the results to the owner, who decides whether to adopt it. No adoption rule is set in advance.

**Target to report, not a gate:** CPU per channel-second, and channels per core.

### B3. Fix the zero-input noise defect
Plan A kept the prototype's behavior on exact zeros (three-tap level stuck at 1e-20 FS², spectrum ratio 0/0); fix it (a floor and recovery rule, stated and classified), enable Plan A's disabled test, and confirm development-set texts unchanged (the suite has no exact zeros).

### B4. Time constants in dits (stage-2 spec §3.1) and the guard margin (§3.3)
W_min = 16.7·d_k of key-down; re-key time-out = 2.5 × W_min; periodicity windows N_w·T with N_w ∈ {41.7, 104, 208}, and the comb's confidence threshold re-measured for the new windows (the calibration method of stage 1's E1, now on C++ decodes); guard margin 0.5·L₁ = 4.8 ms with the mask-bias table b_mask,k re-measured (stage-1 Task 5 method) and accepted segments per second reported at 12, 25, 40 WPM. **Rule:** these are design decisions already taken by the owner; the measurement reports their effect (paired CER by group, the stretch test from B9) and is not a gate; a group made measurably worse is reported to the owner.

### B9 (run before B5). Validation harnesses (stage-2 spec §4.1, §4.3)
The stretch test: group A's 25 WPM conditions with the keying timeline stretched by 2.08 (12 WPM) at S₅₀₀ lowered by 3.19 dB, generated by `kz4ap_synth` (new suite group, seeds 1–3); measure paired CER (stretched − original) and the CER-0.10 crossing shift (target 3.2 dB of S₅₀₀). The new-over checks: false new overs per transmission, missed turnovers per QSO turnover, first-word CER, on group H and the `pauses` group. Both added to the stage-1 tooling.

### B5. The amplitude average τ_a (stage-2 spec §3.4; rule confirmed by the owner, 2026-10-03, Q3 A)
Variants (i) 0.5 s of key-down, (ii) 10.4·d_k of key-down. **Rule (owner-confirmed):** adopt (ii) if its paired CER against (i) on the stretch test has an interval entirely below 0 and no group-B regime has a paired interval entirely above 0; otherwise keep (i); if (ii) helps the stretch test but hurts fading, report to the owner (fallback: an adaptive, Kalman-type amplitude tracker). This plan also updates spec §3.4 to record that the rule is confirmed.

### B6. New overs: two hypotheses in parallel (stage-2 spec §3.5)
Trigger 8·T_g; neutral prior; Λ from the marginal likelihood (log-uniform amplitude prior S₅₀₀ −10…+40 dB, about 30 grid points) against the likelihood at a_prev, plus the timing comparison; decide at |Λ| > 4.6 nats or a 15 s cap; at most two hypotheses; "same" shown, corrected once; per branch. **Measure:** the B9 new-over checks, Farnsworth false over starts (stage 1: 16 of 48), the lost-first-character tests (strict expected failures from stage 1; they should start passing — each one that does is converted to a normal test), paired CER by group, CPU. **Rule:** adopted (owner's design); reported, with the trigger 8·T_g and the threshold 4.6 nats checked: if false new overs or missed turnovers are worse than the reference in any group, report to the owner before tuning.

### B7. Corrections reach back to the speed change (stage-2 spec §3.6)
The maximum-likelihood split point among recent marks and spaces; measure on group D (speed steps) and E6's follow test: characters corrected, final CER, displayed (immediate) CER. Reported.

### B8. The frequency tracker in the loop
The engine's existing tracker re-centering (as the Matched decoder uses it) feeding the bank decoder, replacing anchor mixing for detector channels and for oracle channels with drift. **Measure:** group F (drift, offset; stage 1 marked these not comparable), and the detector path. Reported; the oracle-anchor "not meaningful" marks are removed where the tracker makes the rows comparable.

### B10. Investigation: strong neighbors and crowded channels (stage-2 spec §5, items 1–2)
The largest losses against Matched (group E up to +1.9 CER; crowded 0–100 Hz). Diagnose with traces first (which branches pass the neighbor's keying, and where the selection goes wrong); then propose a design change **to the owner** before implementing it (this plan does not pre-decide the fix).

### B11. Investigation: the Farnsworth gap fit and the 12 WPM cliff (§5, items 3–4)
The coarse T_g grid fits Farnsworth gaps 7.2% short (strict expected failure) — test a finer T_g refinement (its rule: adopt if the Farnsworth test passes and no group's paired interval is entirely above 0); the 12 WPM cliff and 10 WPM losses — read from B4/B5's stretch test; a remaining cause is reported to the owner.

### B12. Final evaluation and write-back
All three seeds (held-out 2–3 reported separately), oracle and through the detector path with the tracker; the bank against Matched and Envelope by group with intervals; the displayed-text measure (immediate and final CER, correction statistics); detection recall, false tracks and tracks per QSO behind the live detector (stage-1 spec §7, required); CPU and memory per channel; distance from the genie bound (results record of stage 1, §5.1.1) at 12, 25, 40 WPM. The results record and a spec write-back draft for the owner's approval; no verdict.

## Owner's decisions (2026-10-04)

1. **B2's adoption rules:** (a) and (b) identical texts or every difference traced to a near-tie; (c) measured, then discussed with the owner.
2. **B10:** diagnose first, then bring the proposed fix to the owner (option A).
3. **Order:** B1 and B2(a, b) first, the design changes next, B2(c) last before the final evaluation (option C).
4. **Branch:** a new branch `milestone-2c-stage2` from `milestone-2c-bank` (option A).
