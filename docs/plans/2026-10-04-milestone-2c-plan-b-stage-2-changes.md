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

## Full task texts

Written task by task before dispatch (the plan's status note). Common to the no-text-change tasks (B1, B2(a), B2(b)):

- **The reference** is `bank-b0`: the development set (seed 1, `kz4ap_proto.experiments.DEV`, 21 test cases, 525 oracle channels) replayed on the Linux machine by `kz4ap-bank-replay` at the Plan A code (a3a96d9), written to `build/suite/full3/proto/bank-b0/` there. B1 produces it first and checks its texts against Plan A's `bank-cpp`.
- **The check** is a channel-by-channel comparison of final texts against the reference (count identical; for each differing channel, its first differing character and time), plus `kz4ap_proto.experiments compare --base bank-b0 --variant <run>` for the paired CER. Pass: every text identical, or every difference traced to a near-tie with the values on both sides (owner, 2026-10-04). Too many to trace: stop and report the count.
- **CPU** per channel-second from the decoded files' `cpu_s` and `channel_s`, pooled and per group, on the Linux machine; on the Windows PC, F-drift-s1 (8 channels, 240.1 channel-seconds; Plan A Task 7: 453.6 ms per channel-second).

### Task B1 (full text): memory

**Files:** `engine/include/kz4ap/bank/fit.hpp`, `engine/src/bank/fit.cpp`, `engine/include/kz4ap/bank/channel.hpp`, `engine/src/bank/channel.cpp`, `engine/tests/bank/fit_test.cpp`, `engine/tests/bank/channel_test.cpp`, `docs/signal-processing.md` (§8c "Memory"), `docs/plans/2026-10-04-milestone-2c-plan-b-results.md` (new: the Plan B results record, section 1 conditions and section 2 memory).

- [x] **Step 1: The reference run.** Bundle the branch to the Linux machine, build the `linux` preset, run `ctest`, then as a job `kz4ap-bank-replay --out build/suite/full3 --name bank-b0 --only "<DEV>" --jobs 10`. Compare its texts with `bank-cpp`: expected identical on all 525 channels (Plan A's later commits changed the engine's correction events and the tests, not the bank's text). Any difference: stop and report.
- [x] **Step 2: Measure memory before the change.** Peak resident set size (`/usr/bin/time -v`, "Maximum resident set size", kB) of `kz4ap-bank-replay` on one long test case (G-ragchew-s1) with `--jobs 1` and with `--jobs 10`, at the reference build; per channel in flight = (RSS₁₀ − RSS₁) / 9 (measured), against §8c's derived 26–59 MB.
- [x] **Step 3: Failing tests.** (i) Two `DurationFit`s built from the same configuration share one grid-constants object (pointer equality through a test accessor), a fit built from a configuration with a different grid (e.g. `t_grid_step` 0.02) does not, and both decode their own grid correctly; (ii) the shared constants are immutable (`const`) and safe to read from several threads (a test constructing and using fits from 8 threads at once); (iii) the channel's |v_k|² window stores 4-byte floats (a size accessor or a static_assert on the element type).
- [x] **Step 4: Share the grid constants.** One immutable `DurationFit::Model` per configuration, shared process-wide by every fit built from an equal configuration (keyed by the exact values of the fields the model reads: `min_wpm`, `max_wpm`, `t_grid_step`, `q_grid`, `w_grid`, `tg_grid`, `outlier_prior`, `outlier_range_s`, `sigma_ln_mark`, `sigma_ln_space`, `fit_memory`, `prior_sigma_ln`, `refine_iterations`), held by `shared_ptr<const Model>` in a mutex-protected cache of `weak_ptr`s so it is freed when no fit uses it. The tables and history stay per fit. No arithmetic changes.
- [x] **Step 5: Store the |v_k|² window as float.** The window already holds values rounded to float32 (the prototype's rounding, Plan A); storing them as `float` is lossless. Every read converts to double exactly as now. No arithmetic changes.
- [x] **Step 6: Tests pass; golden tests unchanged** (ctest on Windows; smoke unchanged).
- [x] **Step 7: The check on the Linux machine:** run `bank-b1`; texts against `bank-b0` (expected identical: no arithmetic changed); CPU per channel-second (reported, not a gate); memory measured as in Step 2 at the new build.
- [x] **Step 8: Documents.** §8c "Memory": the new derived counts (the window 32 × 34 721 × 4 B = 4.4 MB; the grid constants once per process per configuration; per fit its tables and history), and the measured per-channel figures before and after, with the method. The new results record `docs/plans/2026-10-04-milestone-2c-plan-b-results.md`: section 0 terms, section 1 conditions (platform, the reference `bank-b0` and its check against `bank-cpp`), section 2 memory (before and after, derived and measured, and the text check).
- [x] **Step 9: Commit** (ctest on Windows and on the Linux machine passing).

### Task B2(a) (full text): restructure the fit's arithmetic

**Files:** `engine/src/bank/fit.cpp` (and `fit.hpp` if a declaration changes), `engine/tests/bank/fit_test.cpp`, `docs/signal-processing.md` (§8c "Duration fit" where the evaluation changes, and the measured cost), the results record (section 3, cost).

The target: 75% of the decoder's CPU time is in scalar `exp`, `log` and `log1p` inside `DurationFit::add` (`grid_loglik`: per grid point, 2 `log` + 2 logaddexp for marks over 3636 points, 3 `log` + 3 logaddexp for spaces over 6060 points; each logaddexp one `exp` and one `log1p`) and in `best` (`terms` evaluated 5 times per call on up to 192 retained observations, all 5 classes each, plus `refine`'s responsibilities `exp(ll − total)`).

Candidates, in this order:
1. **Exact (bit-identical by IEEE arithmetic):**
   - in `terms`, compute only the classes of the observation's kind (2 for a mark, 3 for a space); the others are −∞ by definition and enter only as logaddexp(x, −∞) = x + log1p(0) = x, and as `exp(−∞) = 0` weights adding +0 in `refine`;
   - in `best`, reuse the `terms` already computed (the grid point's terms are `refine`'s starting terms; the refined point's are its last) instead of recomputing them;
   - any further skip of a logaddexp whose smaller term is so far below that the result equals the larger term exactly (derive the exact condition from the rounding, with its proof in a comment; the test decides).
   Each change is tested **bit for bit** (`std::bit_cast<std::uint64_t>` equality, not a tolerance) against a frozen copy of the current code kept in the test file, over the golden inputs and a randomized sweep (≥ 10⁶ observations across durations at and beyond the outlier clamps 0.001 s and 10 s, var_t from 0 to 10⁻² s², marks and spaces, every grid point), including the edge cases of the Review Focus.
2. **Near-exact (changes last-bit rounding):** the log-sum-exp of all of an observation's classes and the outlier in one pass, m + log(Σᵢ exp(xᵢ − m)) with m the largest term (one `log` per grid point instead of one `log1p` per class), and any other algebraic rearrangement that reduces the transcendental count. Tested against the old code on the same sweep, with the largest relative difference measured and stated, and checked on the development set by the rule.

- [x] **Step 1:** Profile one channel on the Linux machine (gprof as Plan A Task 9: F-drift-s1 label 1) at the B1 build: the starting split by function.
- [x] **Step 2:** The exact candidates, test first (the frozen-copy bit-for-bit tests in place before the change); ctest; commit.
- [x] **Step 3:** Linux: run `bank-b2a-exact`; texts against `bank-b0` must be identical (bit-identical by construction; a difference is a bug); CPU per channel-second pooled and per group; profile again.
- [x] **Step 4:** The near-exact candidates, test first; ctest (a golden comparison that moves gets a traced, narrow allowance only, with values); commit.
- [x] **Step 5:** Linux: run `bank-b2a`; the check against `bank-b2a-exact` by the rule (identical, or every difference traced); CPU.
- [x] **Step 6:** Windows: CPU on F-drift-s1 at the reference, after Step 2 and after Step 4.
- [x] **Step 7: Adoption.** Keep the near-exact candidates only if Step 5 passes the rule; otherwise revert them by a new commit (never by discarding) and record why.
- [x] **Step 8: Documents.** §8c: how the fit's log-likelihood is now evaluated (if the near-exact form is kept: the formula, and the measured largest difference from the old one), and the cost figures; the results record section 3: CPU per channel-second before and after each step, Linux and Windows, and the profile split.
- [x] **Step 9: Commit.**

### Task B2(b) (full text): vectorized exp and log (SLEEF)

**Files:** `CMakeLists.txt` (the dependency), `engine/CMakeLists.txt`, `engine/src/bank/fit.cpp` (and a small vector-math wrapper header if useful, e.g. `engine/src/bank/vecmath.hpp`), `engine/tests/bank/fit_test.cpp`, `docs/signal-processing.md` (§8c "Duration fit" evaluation, the parameter table, the cost), build documentation if a build step changes, the results record (section 4, B2(b)).

After B2(a) the Linux profile still puts 56% of the time in the math library (`exp` and `log`; `log1p` is gone). SLEEF (Boost Software License, builds with MSVC, clang and g++) evaluates them on 2 or 4 doubles at once with a stated maximum error of 1.0 ulp (the `_u10` functions).

- [x] **Step 1: The dependency.** Add SLEEF by `FetchContent` at a pinned release, building only the library (no tests, no DFT, no quad), statically linked, in both presets. Requirement: the binaries run on any x86-64 processor (no illegal instruction where AVX2 is missing): SLEEF's dispatcher functions (which choose the instruction set at run time) or the SSE2 variants; measure both if the dispatcher's cost is in doubt. CI (GitHub Actions, Windows and Ubuntu runners) must build it without manual steps; record the added configure and build time.
- [x] **Step 2: Failing tests first.** The wrapper's exp and log against `std::exp` and `std::log` over a sweep (≥ 10⁶ arguments covering the fit's ranges and the edge cases: −∞, 0, subnormals, the 40-nat cut-off), the largest difference in ulp measured and asserted ≤ 1 ulp (the library's stated bound); the fit's grid values against the B2(a) code (a frozen copy in the test, as B2(a) did) within a bound derived from 1 ulp per call through the formula, stated with its derivation.
- [x] **Step 3: Use it** in the fit's grid update and, if the profile says it matters, in `terms` and the refinement's responsibilities: grid points in blocks of the vector width, with a scalar tail. No change to what is computed, only how exp and log are evaluated.
- [x] **Step 4:** ctest on Windows (a moved golden comparison gets a traced, narrow allowance only, with values); smoke unchanged.
- [x] **Step 5: The check on the Linux machine:** run `bank-b2b`; texts against `bank-b2a` by the rule (identical, or every difference traced to a near-tie with the values on both sides; too many to trace: stop and report the count); CPU per channel-second pooled and per group; profile one channel (F-drift-s1 label 1).
- [x] **Step 6:** Windows: CPU on F-drift-s1, the B2(a) build against the B2(b) build, two runs each in one session.
- [x] **Step 7: Adoption** (owner's rule): keep it only if Step 5 passes; otherwise revert by a new commit and record why.
- [x] **Step 8: Documents.** §8c: that exp and log in the fit are SLEEF's (version, functions, the stated 1.0 ulp bound and the measured largest difference); the parameter table row; the cost; the results record section 4: CPU before and after on both platforms, the profile split, the text check, the build-time cost.
- [x] **Step 9: Commit.**

### Task B3 (full text): exact zeros and a noise estimate that cannot recover

**Files:** `engine/src/bank/noise.cpp`, `engine/include/kz4ap/bank/noise.hpp`, `engine/include/kz4ap/bank/bank_config.hpp` (new parameters), `engine/src/bank/channel.cpp` if the estimators need to know which samples are exact zeros, `engine/tests/bank/noise_test.cpp`, `engine/tests/bank/channel_test.cpp`, `engine/tests/bank/config_test.cpp` (if it checks the field list), `docs/signal-processing.md` (§8c "Noise" exact zeros, "Keying" exact zeros, "Memory/End of stream" exact zeros, the parameter table), the results record (section 5, B3).

**The defect (Plan A, §8c "Exact zeros").** On exact-zero input the three-tap level falls to its floor 10⁻²⁰ FS² and, once noise arrives, no tap passes the guard again (acceptance about 10⁻⁵⁴ per tap at ρ = σ̂²/σ² = 10⁻²⁰/0.036), so it stays there; the spectrum accepts all-zero segments, its shape becomes identically 0 and variant (a)'s ratio is 0/0 = NaN. The same lock-up follows any rise of the true noise level by a large factor faster than the estimate can follow (derived from the same acceptance formula: at a rise of 60 dB relative to the estimate, ρ = 10⁻⁶ and the acceptance is about 10⁻¹⁷ per tap), for example a band change or a receiver gain step.

**The fix (two rules, each stated with its class):**
1. **Exact zeros are missing data** (derived: a receiver's output carries noise, so a block of exact zeros is padding or a dead channel, not a measurement of the noise). A block whose input samples are all exactly 0 FS updates neither the three-tap level nor the warm-up, and a spectrum segment containing only exact-zero input is not accepted. The warm-up runs on the first `noise_warmup_s` = 0.32 s of non-zero input. Until a first estimate exists the branch's σ² is unknown, as at the start of a stream now (nothing keyed, nothing published, no NaN published).
2. **Recovery of a stuck level** (heuristic): if no middle tap of branch k has been accepted for `noise_stuck_s` of non-zero input, branch k's level is set again by the warm-up rule (the 20% quantile of |v_k|² over the last `noise_warmup_s`, divided as the warm-up divides it) and the update resumes. Default `noise_stuck_s` = 4 τ_n = 8 s (heuristic; a time in seconds, not dits, because the noise process has no keying speed). A continuous carrier longer than 8 s triggers it; the level then decays back with τ_n once the carrier stops (state the derived recovery time for a carrier 40 dB above the noise, in dB relative to the noise, and measure it in a test).

- [ ] **Step 1: Failing tests.** (i) Enable `BankNoise.DISABLED_ExactZerosThenNoiseStayFiniteAndPositive`, with "after the warm-up" defined as from 0.32 s after the first non-zero input (state the change of definition in the test's comment); (ii) noise, then 5 s of exact zeros, then noise at the same level: σ² unchanged across the gap (bit for bit) and finite throughout; (iii) noise stepping up by 60 dB (relative to the earlier noise power): σ² within a factor 2 of the new power by `noise_stuck_s` + `noise_warmup_s` + 2 τ_n after the step (state the derived expectation); (iv) a 10 s continuous carrier 40 dB above the noise (dB relative to the noise power in the branch), then noise: σ² back within a factor 2 of the noise power by the derived recovery time; (v) the channel: a stream of 1 s of zeros, then a clean station: the decoded text equals the text decoded from the station without the leading zeros (characters' times shifted by 1 s), and no NaN in any published value; replace `BankNoise.ExactZerosThenNoiseAsThePrototype` (it pins the defect) by these.
- [ ] **Step 2: Implement** the two rules; new `BankConfig` fields with unit, default and status comments as the others.
- [ ] **Step 3:** ctest on Windows; smoke unchanged; golden tests unchanged (the golden streams have no exact zeros; if one moves, stop and report).
- [ ] **Step 4: The check on the Linux machine:** run `bank-b3`; texts against the latest reference (`bank-b2b`, or `bank-b2a2` if B2(b) was not adopted). Expected identical: the development set has no exact zeros, and rule 2 should not fire. Count how often rule 2 fires (a counter written to the decoded file or a log). Any text difference or firing: report each with its cause; the controller decides (the plan expects texts unchanged).
- [ ] **Step 5: Documents.** §8c: the three "Exact zeros" passages rewritten to the new behavior; the two rules in "Noise" with their classes; the parameter table rows; the results record section 5.
- [ ] **Step 6: Commit** (ctest on Windows and on the Linux machine passing).

### Common to the design-change tasks (B4 onward)

- **Reference:** the previous task's development-set run (for B4a: `bank-b3b`), replayed on the Linux machine.
- **Measure:** paired CER (variant − reference) pooled and per group with 95% bootstrap intervals over test cases (`kz4ap_proto.experiments compare`), first-word CER, CPU per channel-second; the texts that changed are counted.
- **Rule (stage-2 spec decisions already taken by the owner):** reported, not a gate. A group whose paired interval lies entirely above 0 is reported to the owner with its regimes before the next task starts.
- **The stretch test** (stage-2 spec §4.1) is built in B9, which runs after B4; B9 measures it on both `bank-b3b` and the final B4 code, so B4's effect on time-base invariance is reported there (controller's ruling: the plan's order puts B4 before B9).

### Task B4a (full text): time constants in dits (stage-2 spec §3.1)

**Files:** `engine/include/kz4ap/bank/bank_config.hpp`, `engine/src/bank/keying.cpp`, `engine/src/bank/channel.cpp`, `engine/src/bank/periodicity.cpp` (and their headers), `bench/src/bank_json.cpp` (the configuration list), tests in `engine/tests/bank/`, `docs/signal-processing.md` (§8c and the parameter table), the results record (section 6).

Within branch k, a dit is the branch's nominal dit d_k = L_k / 0.8, L_k = 9.6 ms × 1.1^(k−1) (d_1 = 12 ms, d_32 = 230 ms).

- **Re-key wait** W_min,k = 16.7·d_k of key-down time per branch (was 0.8 s for every branch; 16.7 = 0.8 s / 48 ms). The seed's memory bound stays `seed_memory_rekeys` × W_min,k (so it scales with it).
- **Re-key time-out** = 2.5 × W_min,k = 41.7·d_k of channel time, per branch (was 2 s per channel).
- **Periodicity windows:** each candidate dit T is judged over its own window N_w·T, N_w ∈ {41.7, 104, 208} (were 2, 5 and 10 s); the rule "the confident estimate with the shortest window" unchanged. The comb's reach (out to 9.15·T) is then always inside the window (N_w ≥ 41.7 > 18.3, derived). The history of p kept for the longest window: 208·T_max of the candidates (50 s at 5 WPM); state the memory it costs.
- **New parameters** replace the old ones in `BankConfig` (`rekey_after_dits` = 16.7, `rekey_timeout_ratio` = 2.5, `periodicity_windows_dits` = {41.7, 104, 208}), each with unit, default and status (measured at 25 WPM by E9b and scaled: derived; 2.5: heuristic, stage 1's ratio; N_w: placeholder, stage 1's at 25 WPM), and the time-constant class of each (dits, with the reason).

- [ ] **Step 1: Failing tests:** W_min,k and the time-out per branch in samples at 1500 samples/s for k = 1, 16, 32 (derived values: e.g. W_min,1 = 16.7 × 12 ms = 200.4 ms); at 25 WPM's branch (d_k nearest 48 ms) the values equal stage 1's within the ladder's granularity (state it); the periodicity window used for a candidate T is N_w·T; a channel test: a 12 WPM station re-keys after 16.7 dits of key-down, not 0.8 s.
- [ ] **Step 2: Implement.**
- [ ] **Step 3: Periodicity confidence.** The comb's threshold 0.03 (placeholder) was set with fixed windows. Measure, offline from the `bank-b4a` decoded files' per-window estimates and scores against the truth labels (stage 1's E1 method: points every 0.25 s inside transmissions of constant-speed stations counted at S₅₀₀ ≥ 0 dB, in groups A, B mixed style, C, G, H per-station view, and I; correct within 5% of the true dit; bootstrap 95% intervals over channels): precision and coverage at 0.03 with the new windows, against the same on `bank-b3b` (the old windows), and the threshold at which precision reaches 0.95. Keep 0.03 (stage 1's end-to-end check made decoding worse at the calibrated threshold); report the numbers. If precision at 0.03 falls below `bank-b3b`'s with non-overlapping intervals, report it to the owner.
- [ ] **Step 4:** ctest on Windows; the Plan A golden tests whose configuration is the old one either run with the old values set explicitly (preferred: the module goldens test the code, not the defaults) or are listed with the reason they moved; smoke unchanged.
- [ ] **Step 5: The run** `bank-b4a` on the Linux machine against `bank-b3b`: the measure and rule above; CPU (the longer windows cost); peak memory per channel (B1's method).
- [ ] **Step 6: Documents:** §8c (re-keying, periodicity: the values in dits and the class of each), the parameter table rows, the results record section 6.
- [ ] **Step 7: Commit.**

### Task B4b (full text): the noise-spectrum guard margin (stage-2 spec §3.3)

**Files:** `engine/include/kz4ap/bank/bank_config.hpp`, `engine/src/bank/noise.cpp`, tests, a measuring tool or test for b_mask,k (C++), `docs/signal-processing.md`, the results record (section 7).

- **Guard margin** 0.5·L_1 = 4.8 ms (was 20 ms; heuristic; class: a property of branch 1's filter and the transmitter, so seconds tied to L_1, not dits).
- **b_mask,k re-measured** with the new margin by stage 1's Task 5 method (white noise, 60 s, seeds 101–110; the ratio of the masked periodogram's mean power to the true noise power per branch), now with the C++ estimator; cross-check one seed against the prototype run with `guard_margin_s=0.0048`; the 32 values replace the table with status "measured (Plan B task B4b, white noise, seeds 101–110)".
- **Accepted segments per second** while a station sends, at 12, 25 and 40 WPM (PARIS text, S₅₀₀ = 20 dB, 60 s each), before and after, against the derived clean fractions (stage-2 spec §3.3).

- [ ] **Step 1:** the measuring test or tool, run at the old margin first: it must reproduce the current table within its stated scatter (a check of the method).
- [ ] **Step 2:** the new margin and the new table; tests (the mask's reach in samples at 1500 samples/s; the table's length 32).
- [ ] **Step 3:** ctest; golden tests as in B4a Step 4; smoke unchanged.
- [ ] **Step 4: The run** `bank-b4b` against `bank-b4a`: the measure and rule above; accepted segments per second.
- [ ] **Step 5: Documents:** §8c (the mask, the table, its status), the parameter table, the results record section 7.
- [ ] **Step 6: Commit.**

### Task B4a-C (full text): one shared periodicity window per decision, as a switchable variant (owner, 2026-10-04: measure overnight; no default changes)

Why: B4a's per-candidate windows (N_w·T for each candidate T) score an alias at 3T₀ over three times the signal of the true T₀, and remove stage 1's implicit cap T ≤ W/18.3 per window; wrong estimates near 3T₀ rose from 3.7% to 26.0% (results §6.5). Option C keeps the windows in dits but scores every candidate of a row over one shared window N_w·T̂.

**Files:** `engine/include/kz4ap/bank/bank_config.hpp`, `engine/src/bank/periodicity.cpp` (and header), `engine/src/bank/channel.cpp` if T̂ comes from there, `bench/src/bank_json.cpp` (configuration list), tests, `docs/signal-processing.md` §8c (the variant, marked not the default), the results record (section 6.6).

- **`periodicity_window_mode`**: `"per_candidate"` (default: B4a as committed) or `"shared"`.
- **Shared mode:** each row's window is N_w·T̂ samples of p, the same for every candidate of the row, with T̂ the dit of the branch currently selected (its fitted T if it has an eligible fit, else its nominal dit d_k); before any selection exists, stage 1's windows (2, 5, 10 s). The reach condition (comb inside half the window) applies as in stage 1, so candidates beyond N_w·T̂ / 18.3 are not scored in that row. State the feedback this introduces (the selection's speed sets the window that judges T_P, which feeds the fit's prior and selection's fallback) and that it is an experiment.
- [ ] **Step 1:** tests: in shared mode every candidate of a row is scored on the same samples (scores equal `comb_estimate` on that window); the window follows T̂; before a selection, stage 1's windows.
- [ ] **Step 2:** implement; ctest; defaults unchanged (the development set's default run must stay identical to `bank-b4a`: check on 3 golden streams bit for bit).
- [ ] **Step 3:** the Linux machine: `bank-b4a-shared` (`--set periodicity_window_mode=shared`, re-key in dits) and `bank-b4a-shared-rekeys` (shared windows, stage 1's re-key via `rekey_after_s=0.8`, `rekey_timeout_s=2`) against `bank-b3b`: paired CER pooled and per group, comb precision and coverage at 0.03 and the error classes (near 3T₀, etc., as in §6.5), by speed bin.
- [ ] **Step 4:** documents (§8c, results §6.6); commit.

### Task B9 (full text): the stretch test and the new-over checks (stage-2 spec §4.1, §4.3)

**Files:** `training/kz4ap_synth/suites.py` (new groups), `training/kz4ap_proto/experiments.py` and/or `metrics.py` (the measures), `training/tests/` (tests), the results record (section 8), `docs/backlog.md` only if a finding needs it.

**The stretch test (time-base invariance).** For every group-A 25 WPM condition (machine keying; S₅₀₀ −10 to +20 dB in 2 dB steps; the same texts), a stretched copy: the keying timeline stretched by 25/12 = 2.0833 (12 WPM) and S₅₀₀ lowered by 10·log₁₀(2.0833) = 3.19 dB, so that the energy per dit relative to the noise density is unchanged (derived). New suite group `S-stretch` (seeds 1–3 generated; only seed 1 used until Plan B's final evaluation, B12). An invariant decoder decodes both alike.
- **Measures:** per S₅₀₀ step and pooled, paired CER (stretched − original, paired by text and condition) with 95% bootstrap intervals over test cases; the S₅₀₀ at which CER crosses 0.10 for the original and the stretched copies (interpolated), and the shift between them (invariance target: 3.19 dB, i.e. the crossing at the same energy per dit); the same for first-word CER.
- **Runs:** the original group-A 25 WPM conditions and `S-stretch-s1`, replayed for each of: `bank-b3b` settings (all in seconds, via `--set rekey_after_s=0.8 rekey_timeout_s=2 periodicity_windows_s=[2,5,10]`), `bank-b4a` (defaults), re-key in dits only, windows in dits only, and B4a-C's shared mode (when it exists; the controller tells you). Name runs `stretch-<variant>`.

**The new-over checks.** On group H (two-station QSOs, oracle views) and the `pauses` group (seed 1): false new overs per transmission (a new over started inside a transmission), missed turnovers per QSO turnover (a station change with no new over within 2 s), and first-word CER per over; computed from the decoded files' records and the labels. Run them on `bank-b3b` and `bank-b4a` now (B6 will use them).

- [ ] **Step 1:** the suite group and its generation test (texts identical to group A's; keying times exactly 2.0833 × A's from the stream's first mark; S₅₀₀ 3.19 dB lower; the stretched recording's duration as needed).
- [ ] **Step 2:** the measures, with tests on small hand-made decoded files.
- [ ] **Step 3:** generate on the Linux machine (seed 1; seeds 2–3 generated but not decoded), run the variants, the measures.
- [ ] **Step 4:** results record section 8 (the stretch test table per variant: paired CER by S₅₀₀, the crossing shift; the new-over checks), each number measured; commit.
