# Filter bank and speed estimator for the Matched decoder — design

Status: **approved by the owner, 2026-09-30.** Designed with the owner on
2026-09-30. Replaces the Matched front end's single dit-matched
boxcar, whose length follows the decoder's own speed estimate (milestone 2
part 1, Tasks 11–16). It amends §5.2 step 1 of the design document
`docs/design/2026-09-25-kz4ap-skimmer-design.md` (text in section 9 below,
applied there). Branch: `milestone-2b-filter-bank`, from `milestone-2`.

Conventions: S₅₀₀ is key-down carrier power over noise power in 500 Hz, in dB.
T is the dit duration; WPM = 1.2 s / T (PARIS). Every value is marked
**derived**, **measured**, **heuristic** or **placeholder** (a heuristic whose
value the prototype stage will set).

## 1. Why

The current Matched front end closes a loop: the speed estimate sets the
filter length (T_v = 0.8 T), and the filter length shapes the marks the speed
is estimated from. A wrong estimate can lock itself in: runaways, and the
regressions R1 (20 → 35 WPM step) and R2 (12 WPM first word) recorded in
`docs/plans/2026-09-27-milestone-2a-results.md`. Fixes only made a wrong start
less likely. This design removes the loop: **no filter's length depends on any
estimate.**

## 2. Scope

In scope: everything in a Matched channel after the frequency tracker — the
matched filtering, noise and amplitude estimation, keying, timing of marks and
spaces, speed estimation, and the selection of the text — plus the engine's
text output, which gains corrections.

Unchanged: the spectrum analyzer, detector and channelizer; the option-1
channel design (the detector decides which station a channel follows; the
tracker fine-tunes within ±12 Hz); the frequency tracker; the Envelope path,
which stays selectable and bit-identical.

## 3. Architecture (per channel, after the frequency tracker)

```
u[n] (1500 complex samples/s, station at 0 Hz)
  ├─ noise-spectrum estimator (shared) ──────────────┐
  ├─ branch 1  (L = 9.6 ms)  ─┐                      │ σ_v,k² per branch
  ├─ branch 2  (L = 10.6 ms) ─┤ each: boxcar → |v|,  │
  │   …                       │ amplitude, LLR,      │
  └─ branch 32 (L = 184 ms)  ─┘ keying, mark/space   │
        │                        timing, duration fit ◄─ prior T_P
        │                                              ▲
        └─ branch 1's keying probability → periodicity estimator (T_P)
  branch selection → selected branch's text and speed → text events
                                                        (with corrections)
```

## 4. Components

### 4.1 Filter bank
- Fixed boxcars (running means of the complex samples) of lengths on a
  geometric ladder: L_k = 9.6 ms × 1.1^(k−1), k = 1…32, from 0.8 × the 100 WPM
  dit (9.6 ms) to 184 ms; the 5 WPM optimum, 0.8 × 240 ms = 192 ms, is within
  one step (10·log₁₀(192/184) = 0.18 dB, **derived**). Speed range 5–100 WPM,
  configurable (owner).
- Spacing ×1.1 (owner; limit 0.5 dB): extra loss against a boxcar of exactly
  0.8 T at most 10·log₁₀ 1.1 = 0.41 dB relative to that filter, about 0.2 dB
  on average (**derived**). Total against a perfectly matched filter (L = T):
  at most 0.97 + 0.41 = 1.38 dB (**derived**).
- No variable-length filter (owner: dropped; it would gain at most 0.41 dB).
- The factor 0.8 (length relative to the dit) is **heuristic**: margin for key
  weighting and hand-keying variation shortening the 1-dit space.
- CPU is not a constraint for now (owner). Each branch costs about one current
  Matched front end.

### 4.2 Noise: one shared spectrum
- One estimate per channel of the noise power spectrum of u[n] (e.g. a small
  FFT over noise-only stretches, with a guard that skips samples near marks —
  the idea of the current three-tap guard, reused).
- Each branch's noise power: σ_v,k² = ∫ S_n(f)·|sinc(f·L_k)|² df. In white
  noise this reduces to σ_in² / (L_k·r) (**derived**); the spectrum handles
  non-white noise, e.g. a neighbor leaking into short branches.
- **Where the absolute level comes from was decided by measurement (owner,
  2026-09-30; stage 1, experiment E10, results §3.1): variant (a), branch 1's
  absolute level from the current three-tap guard, with the spectrum setting
  every branch's level relative to branch 1's (measured).** The spectrum's
  mark mask removes mostly low-frequency power, so its bias differs per
  branch (b_mask,k, measured in white noise: 0.8370 at k = 1 to 0.7852 at
  k = 32, Task 5) and does not cancel in the ratio: uncorrected, branch 32's
  level relative to branch 1's reads 6.2% low in white noise and 3.5% low in
  channel-shaped noise (measured, Task 5 review). Variant (a) therefore
  multiplies branch k's ratio by b_mask,1 / b_mask,k, and variant (b)
  divides branch k's level by b_mask,k, so both variants' relative levels
  are corrected by the same table and they differ only in the source of the
  absolute level. Against (a), on
  the development set (509 signals), variant (b), the absolute level from the
  spectrum itself divided by its mask bias (measured per branch, 0.8370 at
  k = 1 to 0.7852 at k = 32, Task 5), gave a paired CER of +0.0020 (−0.0026
  to +0.0072), not distinguishable from 0; the per-branch fallback (c) gave
  +0.0104 (+0.0033 to +0.0204), measurably worse (groups A, B, F and G). The
  pre-registered rule adopts a variant only if it is measurably better, so
  (a) stays. (The plan review had measured the mask keeping 63% of
  noise-only samples and reading their power 2.1% low; the prototype's own
  mask kept 67.2% and read 0.9745 of the true power, Task 5, white noise.)
- **Recorded fallback (owner):** per-branch noise estimates, if the spectrum
  estimate proves hard to guard against marks (E10's third variant, (c)). Not the
  default because a branch of length L gives about 1/L independent noise
  samples per second (104/s at 9.6 ms, 5.4/s at 184 ms), so over a 2 s time
  constant long branches would scatter about 32% against 7% (**derived,
  rough**).

### 4.3 Per branch: amplitude, log-likelihood ratio, squelch, keying, timing
- Amplitude estimate per branch (a long branch smears dits and sees a lower
  amplitude): the current p-weighted estimate, kept.
- Log-likelihood ratio Λ = −a²/2 + ln I₀(a·x), x = |v|/σ_v, a = s/σ_v; keying
  with ±1 nat hysteresis: kept, per branch.
- Squelch per branch: re-derived with each branch's own σ_v on the current
  principle (a constant chance that noise alone passes the squelch).
- Each branch times its own marks and spaces and decodes its own text with its
  own fit (4.5).

### 4.4 Periodicity estimator (coarse speed T_P)
- Input: branch 1's keying probability p[n] = 1/(1 + e^(−Λ[n])), soft, no
  keying decisions; branch 1 resolves every speed up to 100 WPM.
- **Why a comb with positive teeth at T, 2T, 3T, 4T lands on 2T** (as first
  written here; corrected 2026-09-30, owner). Consecutive keying edges T
  apart have opposite signs (a dit's key-down and key-up), so p's structure
  repeats at 2T, not T: the autocorrelation of mean-removed p is low at odd
  and high at even multiples of T, and that comb peaks at 2T on every
  keying style tried (planning check, and the plan review: 0 of 3 seeds
  right in every case, **measured**).
- **Default method (owner): the comb on the period 2T of a dit and its
  element space**, on mean-removed p: 4 teeth at 2T, 4T, 6T, 8T, negative
  teeth halfway between (at T, 3T, 5T, 7T), each ±15% of T wide; the
  estimate is half the best period. Its last tooth is at 8T, so it reads
  lags up to about 9.2T. Measured in the plan review (3 seeds; clean and at
  S₅₀₀ = 10 and 3 dB): within 5% of T for machine, paddle and computer
  keying at 12–100 WPM, and at 5 WPM with windows of 5 s or more even at
  S₅₀₀ = 3 dB; it misses hand keying (1.13–1.22 T). **Farnsworth spacing
  can disturb it (corrected 2026-10-02; stage 1, Task 9, measured).** The
  plan review found it within 5% on Farnsworth text with a centered
  smoother; the prototype's comb (branch 1's causal boxcar, averaged down to
  750 samples/s) locked near the gap timebase at Farnsworth 18/10 WPM: T =
  204.5 ms against the true 66.7 ms (T_g = 207 ms, about 3.1 T) in a 10 s
  window, and over seeds 1–5 × 10 windows it missed 2 of 50 at 10 s, 4 of 50
  at 5 s and 7 of 50 at 2 s (at 0.19–1.38 T), where the spectrum fit
  (67.8 ms) and the edge comb (66.4 ms) read the first window right. In
  decoding, the confident T_P prior then pulled the selected branch's fit to
  T = 149.5 ms (the stage-1 Farnsworth channel test). The earlier reasoning,
  that gaps of 3·T_g fall at no fixed multiple of 2T and so add no
  tooth-locked structure, is contradicted by this measurement; why the comb
  prefers T_g there is not traced. The comb on 2T stayed the default after
  E1–E3 (results §3.5).
- Compared in stage 1 (section 7) against two other variants: a sign-weighted
  comb on the signed edges e[n] = p[n] − p[n−1] (teeth at kT weighted
  negative at odd and positive at even k; best on hand and bug keying at
  S₅₀₀ ≥ 10 dB, but it fails 5 WPM at 3 dB, review), and a spectrum-shape
  fit (nulls of the keying spectrum at k/T). The comb and the spectrum fit
  are related by the Wiener–Khinchin theorem; they differ in the features
  they rely on.
- Windows (owner): several fixed windows in parallel (placeholders 2, 5 and
  10 s), each with its own estimate and confidence; the confident one with the
  shortest window is used. About 20 elements are needed: roughly 2 s at
  25 WPM, 10 s at 5 WPM (**derived, rough**). T_P never feeds back onto its
  own window.
- Use: only as a prior in the duration fits and as the fallback in branch
  selection. It sets nothing directly.

### 4.5 Duration fit (per-branch speed T_k)
- Model: dit mark ≈ T + w; dah mark ≈ r·T + w; element space ≈ T − w;
  character gap ≈ 3·T_g − w; word gap ≈ 7·T_g − w; plus an outlier class (a
  broad distribution) for fragments, merges and noise. r is the dah/dit ratio
  (3 normally, 3.5–5 for bug keying); w the key weighting; T_g the gap
  timebase (= T for standard spacing, larger under Farnsworth, which keeps the
  3 : 7 ratio of character to word gaps).
- Scatter: log-normal (proportional to duration; matches VE3NEA's model and
  the generator's keying styles) plus a per-branch timing-resolution term of
  order a fraction of L_k (size to be derived).
- Memory (owner): exponential weighting over about 24 marks and spaces
  (placeholder). A real speed jump is followed by branch selection, not by one
  fit chasing it.
- Method (owner): a grid over T (logarithmic, about 1% steps over 5–100 WPM,
  about 300 points; coarse grids in r, w and T_g) to find the global maximum
  of the likelihood, then a local refinement within that peak. Not a local fit
  from a starting guess: the likelihood has a peak for each consistent class
  assignment (e.g. near T/3 and 3T), and a local method climbs the nearest
  one — the lock-in of regression R2. T_P enters as a prior on T, weighted by
  its confidence.
- Outputs: T_k, r_k, w_k, T_g,k; fit quality Q_k (mean log-likelihood per
  element); that branch's dit/dah threshold (between T and r·T) and
  character/word threshold (about 5·T_g).

### 4.6 Branch selection
1. **Eligibility:** a branch is eligible when its own fitted dit agrees with
   its own length: |ln(L_k / (0.8·T_k))| ≤ ln 1.1 (within one ladder step).
   A branch too long merges elements and fits a longer T; one too short breaks
   them up and fits a shorter T or mostly outliers.
2. **Among eligible branches:** highest Q_k (fair, since neighbors see nearly
   the same data). Ties are broken first by the **text log-probability** of
   each branch's recent decoded characters (below), then by the longer
   branch (better SNR).
3. **None eligible** (start-up, noise, a fade): the branch whose recent text
   is most plausible by the text log-probability, when that clearly
   separates the branches; otherwise the branch nearest 0.8·T_P when T_P is
   confident; otherwise the shortest branch.
4. **Stickiness:** switch only after another branch has been the best eligible
   one for M marks in a row (M placeholder, about 3–5). A real speed jump
   (e.g. 15 → 30 WPM) must be followed within about 10 marks.
- **Text log-probability (owner: a tie-breaker, not a veto):** the mean
  log-probability per character of a branch's recent decoded text under a
  unigram model: VE3NEA's CW character frequencies
  (`training/kz4ap_synth/messages.py`, `VE3NEA_CHAR_WEIGHTS`, MIT), with a
  very low probability for an element sequence that is not a valid code.
  Why only a tie-breaker: Morse is dense at short lengths (all sequences of
  1–3 elements and 12 of the 16 of 4 elements are letters, from the code
  table), so a dit/dah swap in a short letter usually gives another valid
  letter; validity mainly catches characters run together (long invalid
  sequences). Callsigns and exchanges are full of digits and rare letters,
  and the generator's filler text is drawn from the same frequencies, so
  the benchmark will flatter the model. Its weight and how far back it
  looks are placeholders set in stage 1; it may later use character pairs
  or callsign patterns.
- Speeds: the selected branch's T_k* is the reported speed. The filters never
  use any speed.

### 4.7 Silences and turnovers
- The filter bank, the noise spectrum, the periodicity windows and the
  channel frequency (option 1) carry on without reset.
- A silence longer than a threshold (placeholder: max(0.5 s, 12·T_g), in the
  gap timebase so a Farnsworth word gap, 7·T_g, does not trigger it; with
  standard spacing T_g = T: 0.58 s at 25 WPM, 2.88 s at 5 WPM, the 0.5 s floor
  only above about 50 WPM, **derived**) starts a possible new over: each branch starts a fresh duration fit and a
  fresh amplitude estimate, keeping the previous over's as a fallback in case
  the same station resumes; whichever explains the new marks better wins
  (a comparison of two fits, not a feedback loop). Stage 1 implemented "explains better" as a penalized comparison
  (controller ruling, Task 11 review; heuristic): the fresh fit wins only
  after at least 8 of the over's marks and spaces, and only if its weighted
  log-likelihood beats the previous fit's by ½·k·ln n (k = 4 fitted
  parameters, n = observations; BIC-style). Unpenalized, the fresh fit won
  on 2–3 observations almost always (a same-speed over read T = 101.6 ms
  against the true 48 ms). A later win of the fresh fit does not re-decode
  the over's earlier text (a 15 → 30 WPM turnover's first word, "TEST",
  then read "5T"), and with no element spaces in the first words a
  same-speed turnover is genuinely ambiguous: "EE TT" flipped at n = 12
  (penalty 5.0 nats) to T = 102.4 ms against the true 48 ms.
- **First marks of a new over (owner: the combination):** key immediately with
  a test that does not need the amplitude (e.g. a generalized likelihood ratio
  maximized over the amplitude, or a threshold on |v|/σ_v) and publish that
  text at once; once the amplitude has been estimated from those marks (a
  second or two), re-key that stretch with the full log-likelihood ratio and
  issue any difference as a correction. Expected: the unknown-amplitude test
  loses some sensitivity near threshold for the first marks only (reasoning,
  to be measured).
- **Re-key time-out (stage 1, controller ruling, Task 11 review;
  placeholder, heuristic):** if an over's amplitude is still unknown 2 s of
  channel time after it became unknown (W_min of keyed time not reached),
  what exists is re-keyed with the full LLR at the previous over's amplitude
  (no keying if there is none) and corrected, usually deleting characters
  decoded from noise; an over start counts only once its re-key has keyed a
  mark. In the final evaluation 8 284 of 31 158 corrections were time-out
  re-keyings (results §4.4).

### 4.8 Output: text with corrections
- The engine publishes text as soon as it is decided and issues
  **corrections** that replace an earlier span of text; it does not hold text
  back (owner). A correction may reach back up to **20 s** (owner).
- After a branch switch, the new branch's text replaces the old from the start
  of the character in which the new branch became eligible (owner), at most
  20 s back.
- The re-keying of a new over's first marks (4.7) uses the same mechanism.
- This extends the engine's text events, which today only append.

## 5. What happens to the milestone-2 mechanisms

Owner agreed this table on 2026-09-30 (the revisit promised when the matched
filter was redesigned):

| Mechanism | Disposition | Why |
|---|---|---|
| ×1.25 growth bound and its per-mark fix (`59cd450`) | drop | no filter length follows an estimate |
| unobserved-mark rule (`76e2c27`) | drop | fragments go to the fit's outlier class; a new over's first marks per 4.7 |
| re-acquisition and the 2 s speed-window restore | replace (4.7) | fresh fit plus fallback fit |
| warm-up at the acquisition width | replace | only the noise spectrum needs a start-up |
| squelch ∝ (T_v / 16 ms)^(1/4) | re-derive per branch (4.3) | same principle, each branch's own σ_v |
| three-tap noise guard, floor, clean fraction 0.25 | keep as branch 1's absolute level (E10, variant (a); results §3.1) | the guard idea is reused as the spectrum's mark mask; the three-tap estimate keeps setting branch 1's absolute level, the spectrum the other branches' levels relative to it (measured, stage 1) |
| amplitude estimate (p-weighted, 0.5 s) | keep, per branch | plus 4.7 |
| log-likelihood ratio and ±1 nat hysteresis | keep, per branch | the decision rule |
| front-end robustness (`590da5a`) | mostly drop | invalid-input guards stay where a speed is used |
| tracker robustness (`c99f5e2`) | keep | tracker unchanged |
| option-1 channel design | keep | independent of this redesign |
| unimplemented f1, g1, g2 | drop | superseded by the duration fit (g1's use of spaces is built in) |

## 6. Parameters

| Parameter | Value | Status |
|---|---|---|
| Speed range | 5–100 WPM | owner |
| Ladder spacing | ×1.1 (32 branches) | owner; loss derived |
| Branch length relative to the dit | 0.8 | heuristic |
| Periodicity windows | 2, 5, 10 s | placeholder — not measured for the comb on 2T. Stage 1 tuned these only for the edge comb, which was not adopted (results §3.3–3.5) |
| Comb teeth / tooth width | 4 teeth on the 2T period (at 2T, 4T, 6T, 8T; negative teeth halfway); ±15% of T | owner (2T period); teeth and width: placeholder — not measured for the comb on 2T. Stage 1 tuned these only for the edge comb, which was not adopted (results §3.3–3.5) |
| Comb lag reach | about 9.2T (last tooth at 8T; 9.15 T: T ≤ W / 18.3, so 109 ms, 11 WPM, in the 2 s window) | follows from the teeth (derived) |
| Periodicity method | comb on 2T | owner (default), kept (stage 1, E1: the offline rule chose the edge comb, coverage 0.749 against 0.589 at precision 0.95, but end to end it decoded worse, paired CER +0.0476 (+0.0237 to +0.0726), so it was reverted; the spectrum fit reached precision 0.95 at no threshold; results §3.2, §3.5, §3.6) |
| Noise level source | three-tap level (branch 1) with spectrum ratios | measured (stage 1, E10; results §3.1) |
| Fit memory | 48 marks and spaces, exponential (λ = e^(−1/48) = 0.979 per element) | measured (stage 1, E4; results §3.7); owner kept it, 2026-10-01 |
| Fit grid | T: 1% steps, 12–240 ms; r ∈ {3, 4, 5}; w/T ∈ {−0.4, 0, 0.4, 0.8}; T_g/T ∈ {1, 1.59, 2.52, 4, 6.35} | T step: kept (stage 1, E5: the adopted variant kept it); r, w, T_g grids: measured (stage 1, E5: the cheapest grid not measurably worse than the finest; results §3.8) |
| Eligibility tolerance | ln 1.1 | heuristic (one ladder step) |
| Switch persistence M | 4 (selection instants, branch 1's key-ups) | kept (stage 1, E6: no setting met the rule — none followed a 15 → 30 WPM step within a median of 10 marks; results §3.11) |
| Text log-probability window / weight | the last 10 characters; tie when Q within ε_Q = 0.05 nats per element | kept (stage 1, E8: no setting met the rule; results §3.12) |
| New-over silence threshold | max(0.5 s, 12·T_g) | kept (stage 1, E7: no setting met the rule; results §3.10) |
| Correction reach | 20 s | owner |
| LLR hysteresis | ±1 nat | kept (heuristic) |
| Noise mask bias b_mask,k (used by both variants: each divides branch k by b_mask,k; in the adopted variant (a) the per-branch bias does not cancel in the ratio) | 0.8370 (k = 1) to 0.7852 (k = 32), white noise | measured (stage 1, Task 5; results §1) |
| Unknown-amplitude test: thresholds x_on,k | per branch, 4.6428 (k = 1) to 4.2036 (k = 32), calibrated so that channel-shaped noise alone keys a branch down R_fa times per second | measured (stage 1, E9a; results §3.9) |
| Unknown-amplitude test: target R_fa; release x_off | 0.01 /s; 1.55 | heuristic target, kept (stage 1, E9b: no setting met the rule); heuristic |
| Re-key after W_min of keyed time | 0.8 s | measured (stage 1, E9b; results §3.9) |
| Re-key time-out (an over's amplitude still unknown) | 2.0 s of channel time after the amplitude became unknown; then re-key what exists with the full LLR at the previous over's amplitude, and correct | placeholder, heuristic (controller ruling, Task 11 review; never varied); 8 284 of the final evaluation's 31 158 corrections are time-out re-keyings (results §4.4) |
| Fresh fit against the previous over's fit (§4.7) | the fresh fit replaces the previous over's only after at least 8 of this over's marks and spaces (fresh_fit_min_obs) AND when its weighted log-likelihood beats the previous fit's by ½·k·ln n (k = 4 fitted parameters, n = observations; BIC-style) | placeholder, heuristic (controller ruling, Task 11 review; never varied) |
| Periodicity confidence thresholds | comb 0.03; edge comb 0.03; spectrum fit 1.5 nats | placeholder, measured in stage 1 but not adopted (E1–E3 reverted; results §3.5): E1 calibrated thresholds to T_P precision 0.95 (comb 0.2506, edge comb 0.03232, spectrum fit none reaches it; results §3.2); the comb's calibrated alternatives 0.1487 and 0.2506 were decoded and not adopted (results §3.6.1, §5.5: on the held-out seeds both worse than 0.03 overall and on first words) |

## 7. Validation plan (owner: approach (c))

**Stage 1 — prototype the estimators in Python on real channel streams.**
- Add to `kz4ap-bench` a way to record each oracle channel's complex stream
  to a file: the channelizer's output, before the tracker, mixed to 0 Hz at
  the labeled frequency in Python (the tracker is unchanged by this redesign;
  the stage-1 plan gives the reasons).
- In Python (numpy), on those streams from the benchmark recordings
  (`build/suite/full3`, 3 seeds): build the bank, the noise spectrum, the
  per-branch keying, the periodicity estimator (comb vs spectrum-shape fit),
  the duration fit, the selection and the new-over handling; settle the
  placeholders of section 6 on the benchmark, recording each choice as
  measured.
- Add Farnsworth-spaced text to the generator (not in the suite today), so the
  periodicity estimator and the fit are tested on it.
- Cover every regime (owner, 2026-09-30): the groups normally decoded through
  the detector path (pauses, strong, tune-up, first sample, band, crowded)
  are also decoded on oracle channels from the existing recordings (the
  oracle copies) by Envelope, the current Matched path and the prototype,
  each scored on the same test cases, so the comparison is like for like.
- Through the detector too (owner, 2026-09-30): the bench also records the
  channels the Matched path's detector opens (no oracle), each from its
  opening, with the detector's frequency block by block; the prototype
  decodes them mixed by that frequency (no tracker fine-tuning, stage 2's),
  and its tracks are scored by frequency exactly as the engine's detector
  path is. That covers group H through the detector, the band and crowded
  groups as the detector sees them, and the late-opening case. The prototype is
  compared with Matched per track (the same detector channels) and with
  Envelope per label (its detector opens its own tracks). Stage 1 also
  reports detection recall, false tracks and tracks per QSO for the
  prototype beside Matched and Envelope: as the bench counts them (a track
  counts only if it decoded text) they depend on the decoder. Which tracks
  the detector opens is unchanged by the redesign.
- **No acceptance gate (owner, 2026-09-30).** Stage 1 ends with a full
  comparison for the owner's decision: the prototype against the current
  Matched and Envelope paths on the same signals (oracle channels and the
  detector's channels), with intervals —
  group A crossings at each speed; regressions R1 (group D step 20 → 35 WPM)
  and R2 (group A 12 WPM first-word CER) and the start-up runaway cases; and
  every regime better, worse or unchanged beyond its interval, by how much.
  The owner decides afterwards whether the redesign is better and whether
  stage 2 is worth doing. The experiments' pre-registered rules only choose
  parameters.

**Stage 2 — C++, judged by the full suite.**
- A new selectable front end `--front-end bank` beside Envelope and Matched;
  the corrections in the engine's text events and in the bench's scoring.
- Judged the same way as stage 1, with no acceptance gate: the full
  comparison against the current Matched and Envelope paths on the 3-seed
  full suite, for the owner's decision (owner, 2026-09-30); Envelope
  unchanged.
- **Required part of the stage-2 evaluation: confirm the detection
  measures** stage 1 reports on recorded detector channels (detection recall,
  false tracks, tracks per QSO) in C++ behind the live detector, with the
  frequency tracker in the loop (backlog: "Stage-2 evaluation of the filter
  bank through the detector"). Detection itself, which tracks the detector
  opens, is unchanged by this redesign.

## 8. Open questions (stage 1's answers in §10)

- Periodicity estimator: comb or spectrum-shape fit; window lengths; the
  confidence measure; 5 WPM (keying rate 4.2 Hz) against fading (up to
  f_D = 3 Hz) — reasoning says keying probability is little affected by slow
  fades; to be measured. Stage 1: comb on 2T kept, edge comb and spectrum fit
  measured and not adopted (results §3.2–3.6), windows and confidence
  measured only for the edge comb; 5 WPM against fading not measured (no
  fading signal slower than 12 WPM in the suite).
- Duration fit: the timing-resolution term; outlier class shape; grid steps.
  Stage 1: grid steps measured (E5), timing-resolution term derived, outlier
  class shape not measured.
- Selection: M; behavior when two branches alternate. Stage 1: M kept at 4
  (E6), alternations measured (results §3.11).
- The unknown-amplitude test for a new over's first marks (GLRT or a
  threshold) and when the full-LLR re-keying happens. Stage 1: a per-sample
  threshold on x, calibrated (E9a), re-keying after 0.8 s of keyed time
  (E9b).
- The noise-spectrum estimator: FFT size, averaging, mark guard. Stage 1:
  FFT size, averaging and guard not measured (only compared as a whole with
  the per-branch fallback, E10).
- Per-branch squelch derivation. Stage 1: scaling derived, constant 3 not
  measured.

## 9. Amendment to the design document (§5.2, step 1)

Added after step 1 of §5.2 in `docs/design/2026-09-25-kz4ap-skimmer-design.md`
(owner, 2026-09-30):

> **Amendment (2026-09-30).** The single dit-matched filter whose length
> follows the decoder's speed estimate (built in milestone 2 part 1) is
> replaced by a bank of fixed-length matched filters with a loop-free speed
> estimate (a periodicity estimate plus per-branch duration fits) and branch
> selection by self-consistency, with the decoded text's log-probability as a
> tie-breaker; the engine's text output gains corrections reaching back up to
> 20 s. Design: `docs/design/2026-09-30-filter-bank-speed-estimator-design.md`.

## 10. Stage 1 results (measured, 2026-10-02)

Prototype on the recorded oracle channel streams of the 3-seed full suite, and on the channels the
Matched path's detector opened (`docs/plans/2026-09-30-milestone-2b-stage-1-results.md`, sections 4–5).
This is the Python prototype, not a C++ bank. Intervals are bootstrap 95% intervals over signals; "better"
or "worse" means the interval of the per-signal CER difference (prototype minus reference) excludes 0, with
no correction for the 131 regimes compared (about 7 would read better or worse by chance). The prototype
is scored on its final text, after corrections reaching back up to 20 s (4.09 per channel-minute); Matched
and Envelope on text that only grows. The provisional text a user would see before the corrections is not
measured; stage 2 must score it (added 2026-10-02, final review I3).

Group A (white noise), S₅₀₀ at CER 0.10: 12 WPM −0.1 dB (−0.1 to −0.0 dB) for the prototype, −0.2 dB
(−0.5 to 0.7 dB) Matched, 7.2 dB (6.8 to 7.4 dB) Envelope; 25 WPM −0.0 dB (−0.1 to 0.2 dB), 1.1 dB (0.4 to
1.4 dB), 5.1 dB (4.8 to 5.2 dB); 40 WPM 1.8 dB (1.8 to 1.9 dB), 2.9 dB (2.2 to 3.3 dB), 6.0 dB (5.9 to
15.2 dB). At CER 0.05 the prototype needs 0.1–0.5 dB of S₅₀₀ less than Matched and 4.3–11.7 dB less than
Envelope. The held-out seeds' crossings lie within the development seed's intervals; the largest
difference is 1.5 dB of S₅₀₀ (12 WPM, CER 0.05, where both intervals are about 2.5 dB wide), the others at
most 0.2 dB.

R1 (group D, step 20 → 35 WPM, CER): prototype 0.038 (0.029–0.048), Matched 0.394 (0.222–0.499), Envelope
0.525 (0.257–0.845). R2 (group A 12 WPM, first-word CER at S₅₀₀ 6–20 dB): 0.119 (0.079–0.171), 0.985
(0.823–1.174), 0.801 (0.667–0.974). The two start-up runaway cases: CER 0.003 and 0.048 (prototype), 0.000
and 0.015 (Matched), 0.000 and 0.851 (Envelope). Regimes (group and tag, oracle copies and detector-path
groups included, not-comparable rows left out): against Matched 49 better, 26 worse, 56 unchanged; against
Envelope 83 better, 16 worse, 32 unchanged. Largest differences against Matched: better on a 2 s
tune-up carrier (oracle copy), −0.847 (−0.932 to −0.700), then bug keying, −0.702 (−0.789 to −0.597),
and Farnsworth, −0.27 to −0.57 in all 8 rows; worse with a neighbor 150 Hz away at +20 dB relative to the
wanted station's key-down power, +1.912 (+1.159 to +2.759), then 100 Hz at +20 dB, +1.332 (+0.698 to
+2.093), and in crowded channels 0–100 Hz apart (oracle copies, +0.106 to +0.192). Largest differences
against Envelope: better on group H per station, separate-track 100 Hz (oracle), −1.164 (−1.832 to
−0.644); worse with the neighbor 150 Hz away at +20 dB, +1.223 (+0.391 to +2.397), then 100 Hz at +20 dB,
+1.141 (+0.733 to +1.701). Pooled over oracle channels the paired CER is
−0.078 (−0.088 to −0.067) against Matched and −0.259 (−0.283 to −0.238) against Envelope. No verdict: the
owner decides.

On the detector path (the Matched path's detector's channels), detection recall is the same for the
prototype, Matched and Envelope in every group (1.000, except crowded 0.957 for all three); false tracks
are the same for the prototype and Matched except on very strong signals (prototype 1, Matched 12,
Envelope 12, in 3 recordings), and Envelope differs by one in group H (109 against 110); tracks
per QSO are the prototype's and Matched's alike in every group-H tag (Envelope differs only in the
ambiguous rows). Paired CER against Matched on the same tracks: +0.009 (−0.008 to +0.026).

Stage 2 confirms these detection measures in C++ behind the live detector, with the frequency tracker in
the loop.

Settled in stage 1: the noise level is branch 1's three-tap level with the shared spectrum's ratios
(variant (a) of E10; the spectrum's own level and the per-branch fallback were not better). The
periodicity method stays the comb on 2T at its placeholder threshold 0.03: the edge comb won E1's offline
measure but made decoding worse end to end, and the spectrum fit reached precision 0.95 at no threshold;
higher comb thresholds (0.1487, 0.2506) were worse on the held-out seeds overall and on first words. The
fit remembers 48 marks and spaces, on grids r ∈ {3, 4, 5}, w/T ∈ {−0.4, 0, 0.4, 0.8}, T_g/T ∈ {1, 1.59,
2.52, 4, 6.35} with 1% steps in T. The unknown-amplitude test for an over's first marks is a per-sample
threshold on x = |v|/σ_v, calibrated by measurement per branch (4.64 at k = 1 to 4.20 at k = 32, for
0.01 false key-downs per second in channel-shaped noise), and the over's start is re-keyed with the full
LLR after 0.8 s of keyed time. M = 4, the text tie-break and T_new were kept: no setting met their rules.
The comb's windows, teeth and width remain placeholders: stage 1 measured them only for the edge comb.

Stage 2 must decide what stage 1 did not: which branch's output and key-down probability feed the
frequency tracker.
