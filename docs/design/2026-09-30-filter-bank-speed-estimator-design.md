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
- **Where the absolute level comes from is decided by measurement (owner,
  2026-09-30; stage 1, experiment E10).** Two arms: (a) branch 1's absolute
  level from the current three-tap guard, with the spectrum setting every
  branch's level relative to branch 1's; (b) the absolute level from the
  spectrum itself, corrected for the bias of its mark mask (the plan review
  measured the mask keeping 63% of noise-only samples and reading their
  power 2.1% low, **measured**). The rule is fixed before the run
  (`docs/plans/2026-09-30-milestone-2b-stage-1-filter-bank-prototype.md`,
  Task 13).
- **Recorded fallback (owner):** per-branch noise estimates, if the spectrum
  estimate proves hard to guard against marks (E10's third arm). Not the
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
  lags up to about 9.2T. Farnsworth's stretched gaps still do not disturb
  it: they are 3·T_g ≥ 3T long and fall at no fixed multiple of 2T, so they
  add no tooth-locked structure, while the elements inside each character
  keep their 2T lattice. Measured in the plan review (3 seeds; clean and at
  S₅₀₀ = 10 and 3 dB): within 5% of T for machine, paddle and computer
  keying at 12–100 WPM, for Farnsworth spacing, and at 5 WPM with windows of
  5 s or more even at S₅₀₀ = 3 dB; it misses hand keying (1.13–1.22 T).
- Compared in stage 1 (section 7) against two other arms: a sign-weighted
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
  (a comparison of two fits, not a feedback loop).
- **First marks of a new over (owner: the combination):** key immediately with
  a test that does not need the amplitude (e.g. a generalized likelihood ratio
  maximized over the amplitude, or a threshold on |v|/σ_v) and publish that
  text at once; once the amplitude has been estimated from those marks (a
  second or two), re-key that stretch with the full log-likelihood ratio and
  issue any difference as a correction. Expected: the unknown-amplitude test
  loses some sensitivity near threshold for the first marks only (reasoning,
  to be measured).

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
| three-tap noise guard, floor, clean fraction 0.25 | decided by E10 (4.2; owner, 2026-09-30) | the guard idea is reused as the spectrum's mark mask either way; whether the three-tap estimate keeps setting branch 1's absolute level, or the bias-corrected spectrum sets it, is measured in stage 1 |
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
| Periodicity windows | 2, 5, 10 s | placeholder |
| Comb teeth / tooth width | 4 teeth on the 2T period (at 2T, 4T, 6T, 8T; negative teeth halfway); ±15% of T | owner (2T period, 2026-09-30); placeholder (teeth, width) |
| Comb lag reach | about 9.2T (last tooth at 8T) | follows from the teeth |
| Periodicity method | comb on 2T (default); edge comb and spectrum fit compared | owner (default); stage 1 E1 |
| Noise level source | three-tap level with spectrum ratios, or bias-corrected spectrum level | open: stage 1 E10 (owner) |
| Fit memory | ~24 marks and spaces, exponential | placeholder |
| Fit grid | ~1% in T; coarse in r, w, T_g | derived (cost), placeholder (steps) |
| Eligibility tolerance | ln 1.1 | heuristic (one ladder step) |
| Switch persistence M | 3–5 marks | placeholder |
| Text log-probability window / weight | recent characters; tie-breaker only | placeholder |
| New-over silence threshold | max(0.5 s, 12·T_g) | placeholder |
| Correction reach | 20 s | owner |
| LLR hysteresis | ±1 nat | kept (heuristic) |

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
- Cover every regime (owner, 2026-09-30): the detector-only groups (pauses,
  strong, tune-up, first sample, band, crowded) are scored again with oracle
  channels from the existing recordings, by Envelope, the current Matched
  path and the prototype, so the comparison is like for like.
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

## 8. Still open (settled in stage 1)

- Periodicity estimator: comb or spectrum-shape fit; window lengths; the
  confidence measure; 5 WPM (keying rate 4.2 Hz) against fading (up to
  f_D = 3 Hz) — reasoning says keying probability is little affected by slow
  fades; to be measured.
- Duration fit: the timing-resolution term; outlier class shape; grid steps.
- Selection: M; behavior when two branches alternate.
- The unknown-amplitude test for a new over's first marks (GLRT or a
  threshold) and when the full-LLR re-keying happens.
- The noise-spectrum estimator: FFT size, averaging, mark guard.
- Per-branch squelch derivation.

## 9. Amendment to the design document (§5.2, step 1)

Added after step 1 of §5.2 in `docs/design/2026-09-25-kz4ap-skimmer-design.md`
(owner, 2026-09-30):

> **Amendment (2026-09-30).** The single dit-matched filter whose length
> follows the decoder's speed estimate (built in milestone 2 part 1) is
> replaced by a bank of fixed-length matched filters with a loop-free speed
> estimate (a periodicity estimate plus per-branch duration fits) and branch
> selection by self-consistency; the engine's text output gains corrections
> reaching back up to 20 s. Design:
> `docs/design/2026-09-30-filter-bank-speed-estimator-design.md`.
