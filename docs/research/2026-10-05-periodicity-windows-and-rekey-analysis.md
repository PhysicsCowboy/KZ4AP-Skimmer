# Periodicity windows and re-key settings in dits: analysis (Plan B, task B4a)

Written 2026-10-05 for the owner's decision on Plan B task B4a (results record
`docs/plans/2026-10-04-milestone-2c-plan-b-results.md`, sections 6 and 8). Every claim is labeled
**derived** (follows from the code's formulas), **measured** (from a run, named), or **conjectured**.
"Modeled" marks results computed on a model of the decoder's signals rather than the decoder itself.
Working scripts and raw outputs: `build/b4-analysis/` and `build/b4-trace/` (git-ignored).

## 0. Background

**The bank decoder.** 32 branches per channel; branch k is a boxcar of length L_k = 9.6 ms × 1.1^(k−1)
with nominal dit d_k = L_k / 0.8 (12 ms at k = 1, i.e. 100 WPM, to 230 ms at k = 32, 5.2 WPM). Each branch
keys (decides key-down or key-up), fits element and gap durations, and decodes; selection publishes the
best-fitting branch's text.

**Re-key wait W_min and time-out.** While a branch does not know the station's amplitude (at a stream's
start and at every new over), it keys with an amplitude-free test (thresholds set for 0.01 false marks per
second in noise). After W_min of key-down time it estimates the amplitude (the 90% quantile of the keyed
|v|², less the noise) and *re-keys*: keys the whole stretch since the amplitude became unknown again with
the full likelihood ratio, and replaces the provisional characters. If W_min is not reached within the
time-out (counted from when the amplitude became unknown), it re-keys at the previous over's amplitude, or
deletes the provisional characters. Stage 1: W_min = 0.8 s of key-down, time-out 2 s of channel time, the
same on every branch. B4a: W_min,k = 16.7·d_k, time-out 2.5·W_min,k (= 0.8 s and 2 s at 25 WPM).

**The periodicity estimate T_P.** From branch 1's posterior p (the probability of key-down, averaged to
750 samples/s), over a window of the last n samples: x = p − mean(p); the biased autocorrelation
r(τ) = Σᵢ xᵢ xᵢ₊τ / Σᵢ xᵢ²; for each candidate dit T (1% apart, 12 to 240 ms), a comb on the period Π = 2T
(one dit plus one space) with 4 teeth at kΠ and negative teeth at (k ± ½)Π, each the mean of r over
±0.075Π; score = mean over teeth of r(tooth) − ½[r(left) + r(right)]. A candidate is scored only if its
comb fits in half the window, (4.5 + 0.075)·Π ≤ (n − 1)/2, i.e. T ≤ window / 18.3 (the *reach condition*).
The estimate is the best-scoring candidate; it is *confident* if its score ≥ 0.03; T_P is the confident
estimate of the shortest window. Stage 1: windows 2, 5 and 10 s, every candidate on the same window. B4a:
for each candidate T its own window N_w·T, N_w = 41.7, 104, 208 (= 2, 5, 10 s at 25 WPM).

**Terms.** T₀: the station's true dit. Alias: a wrong candidate at a multiple of T₀ (3T₀ above all).
S₅₀₀: dB SNR in 500 Hz (key-down carrier power over the noise power in 500 Hz). Paired CER: per test case,
the variant's CER minus the reference's, averaged, with a 95% bootstrap interval.

## 1. What the development set showed (measured; results record §6.5)

| Run (against all settings in seconds) | Paired CER | Comb precision at 0.03 |
|---|---|---|
| Re-key in dits only | +0.0116 (+0.0042 to +0.0202), mostly first words | 0.808 (unchanged) |
| Windows in dits only | +0.0184 (+0.0122 to +0.0251) | 0.747 (from 0.804) |
| Both (B4a) | +0.0277 (+0.0194 to +0.0362) | 0.753 |

With the windows in dits, wrong estimates near 3T₀ rose from 3.7% to 26.0% of the wrong ones.

## 2. The periodicity windows

### 2.1 Algebra (derived)

1. **Damping.** For a stationary p, E[r(τ)] ≈ (1 − τ/n)·ρ(τ), ρ the true normalized autocorrelation. A
   tooth k sits at lag 2kT. With a window of N_w·T samples the factor is 1 − 2k/N_w, the same for every
   candidate (0.95, 0.90, 0.86, 0.81 for k = 1…4 at N_w = 41.7). With a fixed window W it is 1 − 2kT/W,
   which shrinks long candidates' scores.
2. **The reach condition with fixed windows** excludes candidates T > W/18.3: 109 ms (11 WPM) in a 2 s
   window, 273 ms in 5 s. Consequences: (a) a station slower than 11 WPM cannot be estimated from the 2 s
   window at all; (b) the alias 3T₀ cannot be chosen from the 2 s window when T₀ > 36 ms (slower than
   33 WPM), i.e. for most of the development set. Per-candidate windows (N_w ≥ 41.7 > 18.3) remove both:
   every candidate is eligible in every window.
3. **Scatter.** The score's scatter falls with the number of independent periods in the window. With
   per-candidate windows, the alias at 3T₀ is scored on three times the signal of T₀, so its scatter is
   about 1/√3 of the true candidate's, while the true candidate is scored on the short window.

### 2.2 The keying's autocorrelation (measured on 3000 s of random text per speed, noise-free)

Machine keying of VE3NEA-statistics random text. ρ at multiples of T₀ is the same at every speed
(10 to 60 WPM) to within ±0.03: m = 1…6: 0.00, +0.22, −0.31, +0.26, −0.23, +0.15; further out the
alternation continues at about ±0.2 to ±0.3. So the comb's expected scores depend only on T/T₀ and on the
window in dits (derived from this).

**Expected scores** (from ρ with the damping, derived): T₀ scores 0.31–0.37, the alias 3T₀ 0.18–0.24
(about 0.65 of T₀), 5T₀ 0.06–0.10, 2T₀ about 0.03, T₀/2 negative. The true dit wins on average in every
regime and at every speed. Aliases come from scatter.

### 2.3 How often the comb picks the alias (measured on noise-free keying; modeled with noise)

Windows ending every 2 s (noise-free) or 4 s (noisy) along fresh text; the share of windows whose best
candidate is T₀ (within 5%) and 3T₀.

**Noise-free** (text variation alone): the per-candidate 41.7-dit row picks 3T₀ in 8–10% of windows at
18–60 WPM (2% at 12 WPM, 0% at 10 WPM where 3T₀ = 360 ms is off the grid); stage 1's 2 s row in 0–1%
(3T₀ excluded by the reach at 25 WPM and slower); the 104-dit row in 0–4%. A 41.7-dit window holds about
4 characters (PARIS: 50 dits per 5-character word), and the true candidate's score scatters by about 30%
of its value over such windows (measured, modeled noise ≥ 0 dB: mean 0.31–0.34, standard deviation
0.08–0.11).

**With noise (modeled).** p modeled as branch 1's posterior: rectangular machine keying, complex white
noise at the given S₅₀₀, branch 1's boxcar, the known-amplitude likelihood ratio (Rician against Rayleigh),
prior 0.44; the decoder's hysteresis, squelch and estimated amplitude are omitted. "Shared" is option C
with an ideal T̂ = T₀: every candidate on one window of 41.7·T₀.

| S₅₀₀ | 2 s (stage 1) | 5 s (stage 1) | 41.7 dits per candidate (B4a) | 41.7·T₀ shared (option C, ideal) | 104 dits per candidate |
|---|---|---|---|---|---|
| T₀ / 3T₀ shares, 25 WPM, −5 dB | 0.61 / 0.00 | 0.85 / 0.04 | 0.51 / 0.10 | 0.61 / 0.00 | 0.77 / 0.13 |
| 25 WPM, 0 dB | 0.89 / 0.00 | 0.96 / 0.01 | 0.85 / 0.12 | 0.89 / 0.00 | 0.96 / 0.04 |
| 25 WPM, 10 dB | 0.97 / 0.00 | 0.97 / 0.03 | 0.86 / 0.14 | 0.97 / 0.00 | 1.00 / 0.00 |
| 10 WPM, 0 dB | 0.00 / 0.00 | 0.96 / 0.00 | 0.81 / 0.19 | 0.95 / 0.00 | 0.94 / 0.06 |
| 12 WPM, 10 dB | 0.99 / 0.00 | 0.96 / 0.00 | 0.91 / 0.09 | 0.97 / 0.00 | 0.97 / 0.03 |
| 40 WPM, 0 dB | 0.90 / 0.06 | 1.00 / 0.00 | 0.86 / 0.11 | 0.94 / 0.00 | 0.94 / 0.06 |
| 60 WPM, 5 dB | 0.94 / 0.05 | 1.00 / 0.00 | 0.80 / 0.19 | 0.92 / 0.00 | 0.92 / 0.08 |

(Full grid 10, 12, 18, 25, 40, 60 WPM × −10, −5, 0, 5, 10, 20 dB: `build/b4-analysis/comb_noisy.log`.)
At −10 dB no regime works (the true candidate's mean score is about 0.01, below the 0.03 threshold).
From −5 dB up, the per-candidate 41.7-dit row picks 3T₀ in 5–23% of windows at every speed; the shared
window never does; stage 1's 2 s window rarely does, but cannot estimate 10 WPM at all.

### 2.4 Is the comparison fair? (derived from 2.1–2.3)

A fair comparison should give every candidate the same chance of winning when it is not the truth. The
per-candidate windows equalize the damping (fair in expectation) but not the information: T₀ is judged on
41.7 of its dits, the alias on 125 of T₀'s dits, so the alias's score is steadier while T₀'s scatters by
about 30% from the text alone; since E[score(3T₀)] ≈ 0.65·E[score(T₀)], T₀'s score falls below the alias's
about 10% of the time. Stage 1's fixed windows were unfair the other way (they shrank and, in the 2 s
window, excluded long candidates), which happened to suppress the alias but disabled slow stations in the
shortest window. **One shared window per decision** compares every candidate on the same data, so their
scores are strongly correlated and the alias loses whenever the true dit's structure is present; with a
window N_w·T̂ the reach condition also excludes candidates above about 2.3·T̂ (41.7/18.3) from the
shortest row, i.e. the alias, as long as T̂ is near T₀. Its weakness (conjectured): T̂ must come from
somewhere (option C uses the selected branch's dit), which ties the window to the decoder's own speed
estimate.

## 3. The re-key settings

### 3.1 Algebra (derived)

- **Per-branch SNR.** A boxcar of length L passes the carrier power s² and leaves noise power N₀/L, so the
  branch's SNR is S₅₀₀ × 500 Hz × L_k: S₅₀₀ + 6.8 dB on branch 1, + 12.8 dB on the branch matched to
  25 WPM (L = 38.4 ms), + 19.6 dB on branch 32.
- **Independent samples behind the amplitude estimate:** about (keyed time)/L_k. Stage 1's 0.8 s gave 83
  on branch 1, 21 on the 25 WPM branch, 4.3 on branch 32; W_min,k = 16.7·d_k gives 21 on every branch.
- **Precision of the seed** (Gaussian approximation, high SNR): relative standard deviation of ŝ² ≈
  3.4 / (a√N), a = s/σ the branch's amplitude-to-noise ratio: 25% at a = 3, 7.5% at a = 10 for N = 21.
- **Effect on timing:** a threshold near ŝ/2 on a boxcar rising over L moves each edge by about ε·L/4, so
  a 25% amplitude error changes a mark by about 0.1 dit. The precision of the estimate is therefore not
  what the re-key settings cost.

### 3.2 What the settings actually cost (measured, trace on 302 channels; `b4-rekey-trace.md`)

The instrumented replays reproduce both runs exactly.

1. **Stream start (groups A, F, B).** Before any branch is eligible, selection holds branch 1. In dits it
   re-keys a median 0.5 s after the station starts (stage 1: about 2.5 s), before the first switch in 115
   of 132 group-A channels at S₅₀₀ ≥ 0 dB (stage 1: 33). Its re-key reaches back to the stream's start,
   because a time-out that clears nothing does not move the stretch's start. At S₅₀₀ 0 to +2 dB its seed
   is a ≈ 4.6–5.9, at which noise crosses the full-LLR key-down threshold on 0.3–0.9% of samples (derived),
   so pre-station noise is published as text ("I EEEE EEEE"); the later switch replaces text only from the
   new branch's eligibility start, so the junk stays. This is 83 of group A's 84 added first-word edits (all
   at S₅₀₀ 0 to +4 dB) and 60 of 62 in F and B.
2. **Later overs (H per station).** The time-out counts from when the amplitude became unknown, a median
   1.23 s before the new over starts (a gap in seconds); with the time-out in dits it fires before W_min,k
   for stations above about 25.6 WPM and re-keys at the previous over's amplitude ("OK2E" became "SUK2E"):
   73 of 107 added first-word edits in later overs.
3. **Condition (derived):** a branch re-keys before its time-out if Δ ≤ W_min,k (2.5 − 1/f), Δ the gap from
   the amplitude becoming unknown to the station's start, f the keyed fraction. Both settings keep
   W_min/time-out = 0.4; what changed is that the clock now runs a few of the station's dits while Δ stays
   in seconds.

So the re-key losses come from **when the clocks start**, not from the dits values. Two changes suggested
by the trace (conjectured effects, untested): move the stretch's start when a time-out clears nothing (so
pre-station noise is never re-keyed into text); count the time-out from the over's first provisional
mark rather than from when the amplitude became unknown.

## 4. Time-base invariance (measured, B9; results record §8)

Group A's 25 WPM signals stretched to 12 WPM at the same energy per dit (S₅₀₀ 3.19 dB lower; an invariant
decoder decodes both alike):

| Variant | Paired CER (stretched − original) | CER-0.10 crossing shift, dB of S₅₀₀ (invariant: +3.19) |
|---|---|---|
| All in seconds | +0.117 (+0.058 to +0.190) | −0.49 |
| Re-key in dits | +0.116 | −0.28 |
| Windows in dits | +0.107 | +0.33 |
| Both (B4a) | +0.126 | +0.10 |

The decoder needs about 2.9 to 3.7 dB more energy per dit at 12 WPM than at 25 WPM; the dits conversion
recovers about 0.6 dB. The rest lies elsewhere (conjectured: settings still in seconds, e.g. the amplitude
average τ_a = 0.5 s (task B5), the false-mark rate per second, the noise time constants).

## 5. Open for the owner

- Periodicity: per-candidate windows (B4a), stage 1's fixed windows, or one shared window (option C,
  measured on the development set and the stretch test in results record §6.6 when it completes).
- Re-key: the dits values with the two clock changes of §3.2 (untested), or stage 1's values.
- Whether to redesign the development set first (backlog, 2026-10-04): its speeds are concentrated near
  25 WPM, where the dits values equal stage 1's by construction.
