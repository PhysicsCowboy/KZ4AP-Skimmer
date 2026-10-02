# Milestone 2b, Stage 1: Results Record

Plan: `docs/plans/2026-09-30-milestone-2b-stage-1-filter-bank-prototype.md`. Spec:
`docs/design/2026-09-30-filter-bank-speed-estimator-design.md`. Every number here is measured unless
marked otherwise; every interval is a bootstrap 95% interval (over signals for CER, over channels for
the periodicity and speed statistics). S₅₀₀: key-down carrier power over noise power in 500 Hz, dB.

## 0. Terms used in this record

**The pipeline, in order** (`docs/signal-processing.md` has the details):

- **Recording**: a WAV file holding a slice of the band, with many stations in it. The suite's recordings
  are synthetic, so every station's true text (its **labels**) is known.
- **Detector**: scans the spectrum, finds stations and opens a **track** for each.
- **Channelizer**: for each track, mixes the station to 0 Hz, filters it to ±150 Hz and decimates it to
  1500 complex samples per second. The result is that station's **channel** and its **channel
  stream**.
- **Decoder**: runs on each channel stream separately. Its first stage, the **front end**, turns samples
  into key-down/key-up; the later stages (elements, letters, speed, text) are shared. "Front end" means
  that first decoder stage, **not** the detector.
- **Oracle mode** (benchmark only): skips the detector and opens a channel at each station's labeled
  (true) frequency, from the first sample to the end. It measures the decoder apart from detection.

**The decoders compared:**

| Name | What it is | Where |
|---|---|---|
| Envelope (`baseline` in result folders) | Milestone 1: envelope smoother, adaptive threshold, hard decisions by duration | C++ engine (`--front-end envelope`) |
| Matched | Milestone 2 part 1: one matched filter whose length follows the decoder's own speed estimate; the engine's current default, which this milestone replaces | C++ engine (`--front-end matched`) |
| Prototype (filter bank) | The spec's design: 32 fixed boxcar branches, periodicity estimator, per-branch duration fit, branch selection | Python, `training/kz4ap_proto`; not in the engine (stage 2 would put it there) |

**The test material:**

- **Suite**: `build/suite/full3`, 123 synthetic recordings with labels, 3 random **seeds** (independent
  draws of noise, text and timing) per condition. Groups A–I each test one condition (A white noise, B
  fading, C fists, D speed changes, E interference, F drift and offset, G ragchew, H QSO, I Farnsworth);
  on band, crowded, pauses, strong, tune-up and first-sample the decoders are run through the detector
  (the **detector path**: detector → channelizer → decoder → text), because what these recordings test
  involves detection. A recording is only test material: it is never "scored"; a decoder is.
- **Scoring a decoder**: running it on a recording, decoding every station, and comparing the text with
  the labels. **CER**
  (character error rate) is the minimum number of symbol insertions, deletions and substitutions that turn the decoded text into the reference, divided by the number of reference symbols (a fraction; prosigns and word spaces count as symbols; `docs/signal-processing.md` section 11). A **test case** is one recording paired with one label file (group H's recordings have
  two: one label per QSO and one per station).
- **Recorded channel streams**: the engine run once with a tap that writes each channel stream to disk
  (`build/suite/full3/channels/`, complex64 files) exactly as the channelizer delivers it, before any
  decoder. The prototype reads these files, so it and the C++ decoders see the same samples, and
  detection and channelizing are not repeated per experiment. **Oracle channels** are one per labeled
  station; **detector channels** are whatever the detector opened, false tracks included.
- **Oracle copy**: for a recording normally decoded through the detector path, the extra test case in
  which the decoders run on its oracle channels instead. **Engine copies**: Envelope's and Matched's
  results on those oracle copies (27 recordings × 2 decoders), so that the prototype is compared with
  them on the same channels.
- **Channel-second**: one second of one channel's stream; the unit of decoding cost (CPU time per
  channel-second).

**Inside the prototype:**

- **Branch k** (k = 1…32): one boxcar filter of length L_k = 9.6 ms × 1.1^(k−1) (9.6 ms to 184 ms)
  with its own noise power, amplitude, likelihood ratio, squelch and keying.
- **Posterior**: for each sample, the probability that the key is down given the observations. Branch
  1's posterior is the input of the **periodicity estimator** (the coarse dit period T_P).

**The experiments:**

- **Experiment** (E1…E10): one question about one parameter or method, from the plan's list.
- **Variant**: one candidate answer to that question, meaning one setting. Each variant is a complete
  prototype **run**, a decoding of the development set with that one setting changed.
- **Development set**: seed 1 of the suite (21 test cases, 525 channels, 74 748 channel-seconds, 509
  signals). Seeds 2 and 3 are held out for the final evaluation, so that values chosen on seed 1 are
  judged on data they were not chosen on.
- **Reference run** (`exp-ref`): the prototype with every default. Each variant is compared with it
  signal by signal: the **paired CER difference** (variant − reference, per signal, pooled) with its
  bootstrap 95% interval.
- **Pre-registered rule**: the decision rule for each experiment, written in the plan before its runs.
  It decides whether a variant is adopted, so the choice is not made after seeing the numbers.
- **Batch**: several variants of one experiment run one after another and each compared with the
  reference (for example `E10.json`).

**The speed estimate T_P and the variants tested, at a glance:**

- **T_P**: the periodicity estimator's coarse, independent guess of the dit length. Each branch fits
  its own dit length T to the marks and spaces it keys; T_P is used as a prior in that fit (pulling
  the fitted T toward T_P) and in choosing which branch to use (the one whose filter length suits the
  station's speed).
- **How T_P is estimated**: from branch 1's posterior p(t). Morse timing is built from multiples of
  the dit (dit 1T, dah 3T, gaps 1T, 3T, 7T), and each method looks for that regularity:
  - **comb** (the owner's default): correlates p(t) with itself and scores each candidate period by
    "teeth" at multiples of 2T (the dit-plus-space period); the best-scoring T wins;
  - **edge comb**: uses the key-down and key-up edge times instead; intervals between edges should
    fall on whole multiples of T, and the comb scores how well they do;
  - **spectrum fit**: fits the power spectrum of p(t) to the shape Morse should produce. It never
    reached the required reliability (E1).
- **Windows**: each method runs in parallel on the last 2, 5 and 10 s (by default); the shortest
  window that is confident supplies T_P. Short windows respond fast; long ones are steadier.
- **Threshold**: each method also gives a confidence score (dimensionless for the two combs); below
  the threshold no T_P is published. The reference's 0.03 is a placeholder from the spec, never
  measured. A **calibrated** threshold is set so that 95% of the published T_P values are within 5%
  of the true dit.
- **Precision**: the fraction of published T_P values within 5% of the true dit. **Coverage**: the
  fraction of moments inside transmissions (every 0.25 s) at which a T_P is published at all.
- **First-word CER**: the CER of the first word of each transmission (each over, in a QSO), on the
  final text, after every correction. It is an upper bound: false characters decoded in the silence
  before a transmission are charged to its first word, so it can exceed 1.

| run | method | windows | threshold | T_P precision / coverage / median time to first confident | in plain terms |
|---|---|---|---|---|---|
| `exp-ref` (the reference) | comb | 2, 5, 10 s | 0.03 (placeholder) | 0.809 / 0.994 / 0.49 s | almost always has a T_P, early, but 19% of them are wrong |
| `exp-E1-3` (section 3.5) | edge comb | 5, 10 s | 0.02214 (calibrated) | 0.950 / 0.865 / 3.60 s | what E1–E3's rules chose; rarely wrong, but late; decoding got worse |
| `exp-diag-comb-cal` (section 3.6) | comb | 2, 5, 10 s | 0.2506 (calibrated) | 0.950 / 0.589 / 2.08 s | the reference with only the threshold raised; rarely wrong, sparser and later; decoding better overall, first words and interference worse |
| `exp-diag-edge-cal` (section 3.6) | edge comb | 2, 5, 10 s | 0.03232 (calibrated) | 0.951 / 0.749 / 1.16 s | the edge comb with the reference's windows; decoding worse, so the edge comb, not the windows, did the damage |

E10's variants (section 3.1) are a separate question, how each branch gets its noise power: (a)
`spectrum`, branch 1's three-tap level × the shared spectrum's ratios (kept); (b) `spectrum-level`, the
spectrum's own level ÷ the measured mask bias; (c) `branch`, each branch its own three-tap estimate.

## 1. Conditions

- Machine: 12th Gen Intel(R) Core(TM) i7-12700H (6 P-cores, 8 E-cores, 20 logical CPUs), Windows 11,
  on AC power. Every prototype process opts out of Windows power throttling
  (`kz4ap_proto.power.disable_power_throttling`, Task 11p). Code: `6d610be` (the experiment harness).
  Used for Tasks 1–12 and for the first `exp-ref` and E10 runs.
- From 2026-10-01 every experiment in section 3 is measured on a second machine: Linux (Ubuntu 22.04),
  10-core Intel Xeon (Ice Lake), 125 GB RAM, g++ 11.4, Python 3.12, numpy 2.5.3, 10 workers. The
  Windows machine's memory could not hold the runs alongside its other applications (section 3.0).
  Reproduction check on the second machine (code `d8160f5`): the suite generated there is identical
  to the Windows suite (all 123 WAV files bit for bit, all 133 label files after CRLF → LF); all
  3 556 recorded channel streams are bit for bit identical; all 318 Envelope and Matched result files
  agree in every decoded text and CER, and differ only in timing and in the detector's frequency
  fields (relative difference about 10⁻¹⁰, which we attribute to compiler and math-library rounding,
  not traced); the prototype's `exp-ref` and `exp-E10-level` reproduce the Windows runs line for line
  except the CPU times. So results from the two machines are interchangeable except CPU and wall
  time, and every CPU time in section 3 is from the Linux machine.
- Suite: `build/suite/full3` (3 seeds; 123 recordings with group I). Prototype input: the oracle
  channels' recorded streams (120 test cases, including the 27 oracle copies of the recordings normally decoded through the detector path), mixed to 0 Hz at the labeled frequency and drift.
- Development set (experiments): `experiments.DEV` — seed 1 of groups A, C, D, E, F, G, H (oracle, both
  views), I, and B's mixed-style recording. Held out: seeds 2 and 3. It is 21 test cases, 525 channels,
  74 748 channel-seconds, 79% of them inside transmissions (Task 11p's count). `experiments.DEV_E5`: 10
  test cases, 259 channels.
- Run times: group I generation 4 min 37 s and the decoders' scoring on it 24 s (Task 1); recording the streams 158 s for
  3 556 channels, 457 169 channel-seconds, 5.3 GB (Task 12); decoding CPU 2.61 s per channel-second
  (Task 11, throttled), 1.04 s per channel-second after Task 11p (14 workers, unthrottled, keyed
  stream at 25 WPM, S₅₀₀ = 10 dB), 0.506 s per channel-second on D-speed-s1 (Task 12, smoke run).
- Unknown-amplitude test, noise alone, nominal threshold (Task 6): 14 false key-downs in 200 s at
  branch 1 (0.070 /s; R_fa = 0.01 /s targeted; review: 0.07–0.10 /s).
- Noise mask (Task 5, white noise): kept fraction 0.672, accepted segments 258 of 350, whole-band mask
  bias 0.9745 (seed 8, 60 s; review: 63%, bias 0.979). Per-branch mask bias b_mask,k (seeds 101–110,
  60 s each, 2 362 of 3 500 segments accepted, kept fraction 0.619): 0.8370 (k = 1, N = 14) to 0.7852
  (k = 32, N = 276), stored as `ProtoConfig.mask_bias`; in channel-shaped noise the same ratios are
  2.9% (k = 1) to 5.9% (k = 32) higher.
- Decoding CPU on a keyed channel (Task 11 Step 5): 2.61 s per channel-second (throttled), 1.04 s
  after Task 11p; experiment budget updated to 1.0–1.3 h per development run and 31–39 h for the
  experiment set on 14 workers (Task 11p, from the sets' mix; accepted by the owner 2026-10-01).
- Speed step 15 → 30 WPM (Task 11 test, seeds 1–4): followed after 11 marks (1.73 s).

## 2. Owner's decisions (2026-09-30, after the plan review)

- Noise level: both measured (E10 variants a, b, c); spec §4.2 and §5 say it is decided by E10.
- Periodicity: the comb on Π = 2T with teeth ±15% of T is the default; the edge comb is E1's third variant;
  spec §4.4 and §6 corrected.
- First-mark threshold calibrated by measurement (E9); keyed samples only while the amplitude is unknown.
- The whole experiment list kept; runs in the background.
- 2026-10-01: the long experiment time (about 31–39 h, plus 5–7 h for the final pass) accepted; no
  speed-ups that change outputs.

## 3. Experiments

(One subsection per experiment, in the order run: question; setting(s) and command(s); development
set size (signals, channel-seconds); the table from `experiments/…md`; the decision rule as written in
the plan; its outcome; the value adopted and its status.)

### 3.0 The development reference, `exp-ref`

```
.venv\Scripts\python -m kz4ap_proto.experiments run --out build/suite/full3 --bench build/windows/bench/Release/kz4ap-bench.exe --name exp-ref --keep-p1
```

All defaults (`ProtoConfig()` at `6d610be`), branch 1's posteriors kept for E1–E3. 21 test cases, 525
channels (74 748 channel-seconds); the prototype scored on 509 signals. **Pooled CER 0.3060** (all groups pooled, S₅₀₀
from −10 dB up; per-group rows in the E10 compare files). Decoding CPU 317.7 ms per channel-second,
wall time 40.1 min (Linux machine, 10 workers; the same command with
`--bench build/linux/bench/kz4ap-bench --jobs 10`).

On the Windows machine the same run gave the same pooled CER, 750.2 ms of CPU per channel-second, and
about 71 min of wall time, run in two parts. The first part (18 workers, 01:23–02:04) decoded 14
recordings and was then stopped by the agent harness because the machine ran low on memory (other
applications held most of the 31.7 GB). The decode resumes, so the second part (10 workers, 30.3 min)
decoded the other 7 and scored the prototype on all 21 test cases. In the second part the prototype's processes used at most
195 MB per worker (about 1 GB in total for 10 workers; 18 workers would take at most about 3.5 GB),
and free memory stayed at 4.28 GB or more. Later runs use 10 workers and a watchdog that stops
the run cleanly if free memory falls below 2 GB.

### 3.1 E10 — the noise level

Question: how each branch gets its noise power σ_v,k². Variants: (a) `spectrum`, the reference
(branch 1's three-tap level × the shared spectrum's ratios); (b) `spectrum-level` (the masked
spectrum's own level ÷ the measured mask bias); (c) `branch` (every branch its own three-tap
estimate; the spec's recorded fallback). Development set: 509 signals, 74 748 channel-seconds.

```
.venv/bin/python -m kz4ap_proto.experiments batch --out build/suite/full3 --bench build/linux/bench/kz4ap-bench --spec build/suite/full3/experiments/E10.json --jobs 10
```

| variant | pooled CER | paired CER against (a), all 509 signals | groups with interval entirely above 0 | CPU per channel-second | wall |
|---|---|---|---|---|---|
| (a) `spectrum` (`exp-ref`) | 0.3060 | — | — | 317.7 ms | 40.1 min |
| (b) `spectrum-level` | 0.3089 | +0.0020 (−0.0026 to +0.0072) | none | 315.3 ms | 39.6 min |
| (c) `branch` | 0.3144 | **+0.0104 (+0.0033 to +0.0204)** | A +0.0213 (+0.0046 to +0.0463), B +0.0251 (+0.0051 to +0.0473), F +0.0115 (+0.0052 to +0.0180), G +0.0009 (+0.0002 to +0.0015) | 327.3 ms | 41.1 min |

Group E, read separately as the plan asks (a neighbor leaking into the short branches; 16 signals):
(b) −0.0007 (−0.0259 to +0.0181), (c) −0.0006 (−0.0450 to +0.0372); neither differs from (a). The only
interval entirely below 0 is variant (c) on group H, oracle view (12 signals): −0.0069 (−0.0148 to
−0.0002). Full tables: `build/suite/full3/experiments/compare-exp-E10-level-vs-exp-ref.md` and
`compare-exp-E10-branch-vs-exp-ref.md` (git-ignored).

Rule (pre-registered): a variant qualifies if its pooled paired CER interval lies entirely below 0 and
no group's paired interval lies entirely above 0; adopt the qualifying variant with the lower pooled
mean; if none qualifies, keep (a). **Outcome: neither qualifies.** (b)'s interval contains 0; (c)'s
lies entirely above 0, so (c) is measurably worse, mainly in groups A, B and F. **Kept: (a)
`noise_method = "spectrum"`**, unchanged in `ProtoConfig`; `exp-ref` stays the reference. Status of
the choice: measured (E10).

Task 5's mask numbers, which variant (b) depends on (it divides by the mask bias; section 1): in white
noise the mask keeps 0.672 of noise-only samples, accepts 258 of 350 segments and reads the whole-band
power at 0.9745 of the truth (seed 8, 60 s); the per-branch bias b_mask,k is 0.8370 (k = 1) to 0.7852
(k = 32) (seeds 101–110), and 2.9% to 5.9% higher in channel-shaped noise.

The Windows runs of this batch, kept for the record: variant (b) gave the same table except CPU
(567.2 ms per channel-second, 71.5 min wall); variant (c) was stopped after 16 of 21 recordings (the
agent harness, low memory at about 3.7 GB free) and was completed only on the Linux machine.

### 3.2 E1 — the periodicity method

Question: which estimator gives T_P (spec §4.4 as corrected, §8). Variants: the comb on Π = 2T (the
owner's default), the sign-weighted edge comb, and the spectrum-shape fit, each at its default teeth
or nulls and widths. Offline on `exp-ref`'s stored branch-1 posteriors (the estimate never feeds back
into them, so this is exact for every variant), every window 1, 2, 3, 5, 10 s logged, the rule "the
confident estimate with the shortest window" over the default windows (2, 5, 10) s. Points: every
0.25 s update inside a transmission of a constant-speed station whose label counts in the score at S₅₀₀ ≥ 0 dB in groups A, B
(mixed style), C, G, H (per-station view) and I of the development set: **175 551 points**. Correct:
within 5% of the true dit, 1.2 s / WPM. Precision: correct ÷ confident; coverage: confident ÷ points;
intervals: bootstrap 95% over channels. Each variant's threshold is calibrated as the lowest of 100
quantiles of its scores at which precision reaches 0.95. Scores: the comb's and the edge comb's are
dimensionless (means of normalized autocorrelation values, of mean-removed p for the comb and of its
signed edges for the edge comb; at most 1 in magnitude); the spectrum fit's are in nats.

```
.venv/bin/python -m kz4ap_proto.experiments periodicity --out build/suite/full3 --name exp-ref --set periodicity_method=<comb|edge|spectrum> --set "periodicity_windows_s=[1,2,3,5,10]" --subsets "2,5,10" --jobs 10
```

| variant | threshold | precision | coverage | median time to first confident, s |
|---|---|---|---|---|
| comb (default) | calibrated 0.2506 | 0.950 (0.943–0.957) | **0.589 (0.549–0.628)** | 2.08 |
| comb | configured 0.03 (placeholder) | 0.809 (0.786–0.832) | 0.994 (0.993–0.994) | 0.49 |
| edge comb | calibrated 0.03232 | 0.951 (0.938–0.961) | **0.749 (0.712–0.783)** | 1.16 |
| edge comb | configured 0.03 (placeholder) | 0.940 (0.926–0.953) | 0.795 (0.762–0.826) | 1.11 |
| spectrum fit | calibrated: no threshold reaches 0.95 | — | — | — |
| spectrum fit | configured 1.5 nats (placeholder) | 0.109 (0.086–0.131) | 0.758 (0.732–0.781) | 0.91 |

Files: `build/suite/full3/experiments/periodicity-exp-ref-periodicity_method=<variant>-periodicity_windows_s=[1,2,3,5,10].md`
on the Linux machine; local copies `build/suite/full3/experiments/linux/E1-<variant>.md` (git-ignored;
the edge comb's file on the Linux machine was overwritten by E2's, whose first two rows are these).

Rule (pre-registered): the comb stays unless another variant's coverage exceeds the comb's with
non-overlapping intervals; among variants that do, the higher coverage; ties on coverage (overlapping
intervals) go to the shorter median time to confident, then to the comb. If no variant reaches 0.95
precision at any threshold, keep the comb and report. **Outcome: the edge comb.** Its coverage,
0.749 (0.712–0.783), exceeds the comb's, 0.589 (0.549–0.628), with non-overlapping intervals, and it
is also faster to its first confident estimate (1.16 s against 2.08 s). The spectrum fit reaches 0.95
precision at no threshold (among the 100 quantiles of its scores), so it cannot qualify; at its
placeholder threshold its precision is 0.109. **Chosen by the rule: the edge comb** (status
measured, E1). It was not adopted: the end-to-end check (section 3.5) made decoding worse, so the
periodicity defaults were reverted and `ProtoConfig` keeps the comb (the owner's default).

The pooled points mix group C's hand and bug stations (where the review found the comb wrong and the
edge comb best at S₅₀₀ ≥ 10 dB) with the other groups; the plan's table does not show group C apart.
An observation-only split (not a decision rule) is in section 3.2.1.

#### 3.2.1 Observation only: E1 split by group and by speed

Not part of any rule; recorded because the review and Task 9 raised these questions. Same points and
windows (2, 5, 10) s as E1; "at the pooled threshold" uses each variant's E1 threshold, "own" calibrates
a threshold on the part alone (script `build/suite/full3/experiments/scripts/e1_split.py`, output
`e1-split.md` on the Linux machine; both git-ignored). Fast: true dit T ≤ 31.5 ms (≥ 38.1 WPM).

| variant | part | points (channels) | at the pooled threshold: precision, coverage | own threshold: precision, coverage |
|---|---|---|---|---|
| comb | C fists | 48 819 (135) | 0.942 (0.921–0.960), 0.539 (0.467–0.612) | 0.2875: 0.950, 0.418 (0.350–0.489) |
| comb | all but C | 126 732 (240) | 0.953 (0.946–0.959), 0.608 (0.560–0.653) | 0.2422: 0.950, 0.632 (0.585–0.678) |
| comb | fast | 22 076 (47) | 0.944 (0.935–0.951), 0.823 (0.731–0.905) | 0.2667: 0.951, 0.786 (0.685–0.869) |
| comb | slow | 153 475 (328) | 0.951 (0.943–0.960), 0.555 (0.513–0.597) | 0.2454: 0.950, 0.570 (0.529–0.612) |
| edge comb | C fists | 48 819 (135) | 0.950 (0.932–0.966), 0.743 (0.678–0.802) | 0.03283: 0.952, 0.735 (0.668–0.797) |
| edge comb | all but C | 126 732 (240) | 0.952 (0.937–0.965), 0.751 (0.707–0.791) | 0.03212: 0.951, 0.756 (0.710–0.797) |
| edge comb | fast | 22 076 (47) | 0.966 (0.940–0.988), 0.955 (0.916–0.983) | 0.02785: 0.955, 0.975 (0.948–0.991) |
| edge comb | slow | 153 475 (328) | 0.949 (0.935–0.960), 0.719 (0.679–0.758) | 0.03288: 0.951, 0.707 (0.667–0.747) |
| spectrum fit | every part | — | no threshold reaches 0.95 | no threshold reaches 0.95 |

So the edge comb's advantage holds in group C and outside it alike, and in both speed ranges. Task 9
suggested calibrating the spectrum fit's threshold separately for fast candidates (12–31.5 ms, which
average 2 usable nulls against 3): split by the true speed, neither part reaches 0.95 precision at any
threshold, so a speed-split threshold would not rescue the spectrum fit on this data. (The split is by
the true dit, which a decoder does not know; a split by the candidate's T was not computed.)

Where the spectrum fit goes wrong (script `build/suite/full3/experiments/scripts/e3_extra.py`, output
`e3-extra.md` on the Linux machine, local copy `build/suite/full3/experiments/linux/e3-extra.md`; all
git-ignored). Population 1, for the ratios: per point, the estimate of the shortest of the windows 2, 5,
10 s that gives one, whatever its score — 174 616 of the 175 551 points have one. Its estimate ÷ the
true dit falls in [0.95, 1.05) for 10.4% of them, [1.05, 1.4) for 21.0%, [2.8, 3.3) for 22.0% and ≥ 3.3
for 24.1% (below 0.95: 9.3%; elsewhere: 13.3%). So it most often locks on about 3T or longer, which
matches Task 9's 3T finding for paddle keying. Its score does not separate right from wrong well. Taking
the 50%, 90%, 99% and 99.9% quantiles of population 1's scores (1.638, 2.338, 3.341 and 4.166 nats) as
thresholds, the E1 rule itself (the confident estimate with the shortest window of 2, 5, 10 s, over all
175 551 points) gives precision 0.119 (0.094–0.143), 0.237 (0.156–0.323), 0.532 (0.167–0.714) and 0.823
(0.175–0.941), at coverage 0.614, 0.114, 0.013 and 0.0010. Population 2, the calibration's: `metrics.calibrate`
searches the 100 quantiles from 0 to 0.99 of every logged window's scores (1, 2, 3, 5 and 10 s; 852 725
scores), whose 0.99 quantile, its highest level, is 3.282 nats. Even at 4.166 nats, above every level it
searches, the rule's precision is 0.823, short of 0.95.

### 3.3 E2 — the periodicity windows

With E1's edge comb, offline as in E1, each window subset at its own calibrated threshold:

```
.venv/bin/python -m kz4ap_proto.experiments periodicity --out build/suite/full3 --name exp-ref --set periodicity_method=edge --set "periodicity_windows_s=[1,2,3,5,10]" --subsets "2,5,10;1,2,5,10;2,5;5,10;2,3,5,10;1,2,3,5,10" --jobs 10
```

| windows, s | calibrated threshold | precision | coverage | median time to first confident, s |
|---|---|---|---|---|
| 2, 5, 10 (default) | 0.03232 | 0.951 (0.938–0.961) | 0.749 (0.712–0.783) | 1.16 |
| 1, 2, 5, 10 | 0.04434 | 0.950 (0.938–0.960) | 0.572 (0.528–0.615) | 1.18 |
| 2, 5 | 0.03232 | 0.951 (0.939–0.962) | 0.726 (0.687–0.757) | 1.16 |
| **5, 10** | 0.02214 | 0.950 (0.935–0.964) | **0.865 (0.838–0.889)** | 3.60 |
| 2, 3, 5, 10 | 0.03336 | 0.952 (0.940–0.963) | 0.737 (0.702–0.775) | 1.21 |
| 1, 2, 3, 5, 10 | 0.04488 | 0.951 (0.939–0.962) | 0.568 (0.529–0.612) | 1.27 |

(At the placeholder threshold 0.03 the same subsets give precision 0.940, 0.873, 0.939, 0.982, 0.936,
0.871 and coverage 0.795, 0.841, 0.770, 0.736, 0.804, 0.847: file
`build/suite/full3/experiments/linux/E2-edge.md`, git-ignored.)

Rule (pre-registered): among subsets whose coverage is within 0.02 of the best coverage, choose the one
with the shortest median time to confident; ties within 0.25 s go to the fewest windows, then to
(2, 5, 10); adopt it only if it differs from (2, 5, 10). **Outcome: (5, 10) s.** The best coverage is
0.865, from (5, 10); no other subset is within 0.02 of it (the next is 0.749), so the time criterion
does not come into play. **Chosen by the rule: (5, 10) s** (status measured, E2); not adopted,
reverted with E1's choice after section 3.5, so `ProtoConfig` keeps (2, 5, 10) s. The price the rule
accepted, stated: the median time from a transmission's start to its first confident estimate rises
from 1.16 s to 3.60 s, because the rule ranks coverage first.

### 3.4 E3 — teeth and widths of the edge comb

With E1's edge comb and E2's windows (5, 10) s, offline as in E1, one command per setting; the edge
comb reads `comb_teeth` (teeth at kT, k = 1 … teeth, weighted (−1)^k) and a tooth half-width of
2 × `comb_width` × T, so `comb_width` = 0.05, 0.075, 0.10 is ±10%, ±15%, ±20% of T:

```
.venv/bin/python -m kz4ap_proto.experiments periodicity --out build/suite/full3 --name exp-ref --set periodicity_method=edge --set "periodicity_windows_s=[1,2,3,5,10]" --set comb_teeth=<3|4|5> --set comb_width=<0.05|0.075|0.10> --subsets "5,10" --jobs 10
```

Coverage at each setting's own calibrated threshold (precision 0.950–0.953 in every row; files
`periodicity-exp-ref-comb_teeth=…-comb_width=…-periodicity_method=edge-periodicity_windows_s=[1,2,3,5,10].md`
on the Linux machine, git-ignored):

| teeth \ tooth half-width | ±10% of T (0.05) | ±15% of T (0.075) | ±20% of T (0.10) |
|---|---|---|---|
| 3 | 0.692 (0.651–0.730), threshold 0.03821, 3.86 s | 0.586 (0.542–0.631), 0.0384, 4.02 s | 0.379 (0.339–0.420), 0.04221, 5.51 s |
| 4 | 0.874 (0.849–0.897), 0.02345, 3.59 s | **0.865 (0.838–0.889), 0.02214, 3.60 s (default)** | 0.831 (0.802–0.856), 0.02142, 3.66 s |
| 5 | 0.831 (0.802–0.860), 0.02455, 3.65 s | 0.831 (0.800–0.859), 0.02223, 3.68 s | 0.809 (0.778–0.838), 0.02054, 3.71 s |

(Each cell: coverage with its interval, the calibrated threshold, the median time to the first confident
estimate.)

Rule (pre-registered): adopt a setting other than the default (4 teeth, 0.075) only if its coverage
exceeds the default's by more than 0.02 with non-overlapping intervals; among several, the highest
coverage. **Outcome: none does.** The best other setting, 4 teeth at ±10% of T, is +0.009 above the
default with overlapping intervals. **Kept: `comb_teeth = 4`, `comb_width = 0.075`** (±15% of T for
the edge comb; the defaults, unchanged in `ProtoConfig`), status of the choice measured (E3). Lag reach of the adopted edge comb (its teeth at kT, not on
Π = 2T, so the comb's formula (teeth + 0.5 + width)·2T does not apply): (teeth + tooth half-width)·T
= (4 + 0.15)·T = 4.15·T, so a T candidate is evaluated only when 4.15·T is at most half the window,
T ≤ 0.6 s in the 5 s window — the whole grid (12–240 ms). The edge comb was reverted after section 3.5,
so for spec §6 the reach of the comb that stays (4 teeth on Π = 2T, half-width 0.075 Π) is also stated:
(teeth + 0.5 + width)·2T = (4 + 0.5 + 0.075)·2T = 9.15·T (`periodicity.py` rounds it to about 9.2 T),
so a candidate is evaluated only when 9.15·T is at most half the window, T ≤ W / 18.3: 109 ms (11 WPM)
in the 2 s window, 273 ms in the 5 s window and 546 ms in the 10 s window (the last two cover the
whole grid; derived).

Calibrated threshold for the chosen method and windows (E2's (5, 10) s row, unchanged by E3), at full
precision from `metrics.calibrate` called by `scripts/e3_extra.py` (output `build/suite/full3/experiments/e3-extra.md`
on the Linux machine, local copy `build/suite/full3/experiments/linux/e3-extra.md`; git-ignored):
0.022138968788232207 (printed 0.02214), precision 0.9503, coverage 0.8652.

### 3.5 E1–E3 end to end: `exp-E1-3`

The settings E1–E3's rules chose — the edge comb, windows (5, 10) s, 4 teeth at ±15% of T, and its
calibrated threshold 0.022138968788232207 — decoded on the development set and compared with the
current reference, `exp-ref`. The plan runs this with the new defaults in `ProtoConfig` and no `set`;
it was run with the same values passed explicitly, which gives the identical configuration (the
decoded files store it), so that `ProtoConfig` changed only if the rule kept them. Batch
`build/suite/full3/experiments/E1-3.json` (git-ignored):

```
{"name": "E1-3", "base": "exp-ref", "subset": "dev", "runs": [{"name": "exp-E1-3", "set": {"periodicity_method": "edge", "periodicity_windows_s": [5.0, 10.0], "edge_confidence_min": 0.022138968788232207}}]}
.venv/bin/python -m kz4ap_proto.experiments batch --out build/suite/full3 --bench build/linux/bench/kz4ap-bench --spec build/suite/full3/experiments/E1-3.json --jobs 10
```

509 signals; **pooled CER 0.3356** (against 0.3060); 315.6 ms of CPU per channel-second; 39.6 min wall.

| group | signals | paired CER, `exp-E1-3` − `exp-ref` | paired first-word CER |
|---|---|---|---|
| all | 509 | **+0.0476 (+0.0237 to +0.0726)** | +0.3247 (+0.1843 to +0.4873) |
| A sensitivity | 192 | +0.0634 (+0.0270 to +0.1067) | +0.5220 (+0.2127 to +0.9067) |
| B fading | 30 | +0.1715 (+0.0418 to +0.3338) | +1.0828 (+0.3694 to +2.0812) |
| C fists | 135 | −0.0153 (−0.0305 to +0.0014) | +0.0733 (−0.0031 to +0.1589) |
| D speed | 12 | −0.0515 (−0.1186 to −0.0030) | −0.0417 (−0.1250 to +0.0000) |
| E interference | 16 | +0.3377 (+0.0973 to +0.6243) | +0.6312 (−0.1918 to +1.6879) |
| F tuning | 28 | +0.1634 (+0.0762 to +0.2672) | +0.8839 (+0.4105 to +1.5092) |
| G ragchew | 12 | +0.0703 (−0.0320 to +0.2193) | +0.0093 (−0.0615 to +0.1040) |
| H two-station QSO, oracle | 12 | −0.0304 (−0.1090 to +0.0652) | −0.1777 (−0.3588 to −0.0005) |
| H two-station QSO, oracle (per station) | 24 | −0.0159 (−0.2287 to +0.2134) | −0.6473 (−1.6326 to +0.2447) |
| I Farnsworth | 48 | −0.0099 (−0.0272 to +0.0092) | +0.1219 (−0.0486 to +0.3545) |

The prototype's own statistics move the other way from CER in most groups (`exp-ref` → `exp-E1-3`):
selected speed off by more than ×1.5 at S₅₀₀ ≥ 6 dB, A 0.0248 → 0.0000, B 0.161 → 0.035, C 0.017 →
0.004, I 0.032 → 0.007, but E 0.184 → 0.482 (lock-ins 3 → 8 of 16 transmissions); switches per
minute fall in every group but F (C 7.8 → 2.6, G 10.8 → 4.9); false characters outside transmissions,
per minute, rise in A (5.4 → 8.1), B (0.79 → 7.99), E (37.7 → 59.9), F (10.0 → 14.8), G (0.86 → 1.22)
and I (0.150 → 0.172), fall in H (oracle view 4.44 → 0.22, per-station view 63.3 → 57.8) and are
unchanged in C and D. Measured: the fraction of selection instants whose selected speed is off by more
than ×1.5 fell in every group with such instants except E, where it rose, while CER rose in A, B, E and F.
The T_P the decoder used changed in three ways at once (section 6: more precise, less often available,
later); which of them, or which use of T_P, explains the CER is not measured. Full tables:
`build/suite/full3/experiments/linux/compare-exp-E1-3-vs-exp-ref.md` (git-ignored).

Rule (pre-registered): `exp-E1-3` becomes the current reference unless its pooled paired CER interval
lies entirely above 0; in that case revert the periodicity defaults, keep the current reference,
record it, and report to the owner (a better T_P that makes decoding worse is a design finding).
**Outcome: the interval lies entirely above 0 (+0.0237 to +0.0726). Reverted.** `ProtoConfig` keeps
the comb, windows (2, 5, 10) s, 4 teeth at ±15% of T and the placeholder thresholds (comb 0.03, edge
comb 0.03, spectrum 1.5 nats); `exp-ref` stays the current reference. Status of the periodicity
parameters: unchanged (placeholder), not rejected by measurement; the E1–E3 choices stand as measured
estimator properties, and their failure end to end is reported to the owner (section 6).

With the E1–E3 settings as defaults, four Python tests fail (`pytest training -q`: 4 failed, 244
passed, 2 xfailed, local run): two strict expected failures now pass — Task 11's Farnsworth 18/10
text ("CQ TEST K1ABC", which the comb's T_g lock broke) and the same-speed turnover's second over; one
test written for the comb's threshold (`test_an_unconfident_estimate_is_not_used` sets only
`comb_confidence_min`); and `test_two_overs_at_different_speeds` (25 then 15 WPM, S₅₀₀ = 20 dB, seed 7)
reads "CQ DE K1ABC K K ETTTTABC DE W9XYZ K", CER 0.2 against the test's 0.1 (with the edge comb and
windows (2, 5, 10) s it reads correctly). With the defaults reverted all tests pass as before.

### 3.6 Diagnostic runs after E1–E3 (observations only; not pre-registered)

Owner's decision, 2026-10-01: before Task 14, two runs that each change one thing against `exp-ref`, to
separate the causes of section 3.5's regression. Neither was pre-registered, so **neither changes a
setting**; they inform the owner's reading. Batch `build/suite/full3/experiments/diag-TP.json`
(git-ignored), on the Linux machine at `651d57d`:

```
.venv/bin/python -m kz4ap_proto.experiments batch --out build/suite/full3 --bench build/linux/bench/kz4ap-bench --spec build/suite/full3/experiments/diag-TP.json --jobs 10
```

- `exp-diag-comb-cal`: the default comb and windows (2, 5, 10) s, with only the threshold raised from the
  placeholder 0.03 to E1's calibrated 0.2506 (as printed in E1's table, 4 significant figures).
- `exp-diag-edge-cal`: the edge comb with the default windows (2, 5, 10) s at E1's calibrated threshold
  0.03232 (4 significant figures).

The T_P each run decoded with is E1's offline measurement on the same 175 551 update points (section
3.2); the end-to-end numbers are on the development set (509 signals, 74 748 channel-seconds). Paired
values are variant − `exp-ref`, per signal, mean over 509 signals, with bootstrap 95% intervals.

| run | method, windows, threshold | T_P precision / coverage / median time to confident | pooled CER | paired CER | paired first-word CER |
|---|---|---|---|---|---|
| `exp-ref` | comb, (2, 5, 10) s, 0.03 | 0.809 / 0.994 / 0.49 s | 0.3060 | — | — |
| `exp-diag-comb-cal` | comb, (2, 5, 10) s, 0.2506 | 0.950 / 0.589 / 2.08 s | 0.2872 | −0.0152 (−0.0280 to +0.0000) | +0.2612 (+0.1410 to +0.3906) |
| `exp-diag-edge-cal` | edge comb, (2, 5, 10) s, 0.03232 | 0.951 / 0.749 / 1.16 s | 0.3317 | +0.0502 (+0.0279 to +0.0742) | +0.2486 (+0.1298 to +0.3693) |
| `exp-E1-3` (section 3.5) | edge comb, (5, 10) s, 0.02214 | 0.950 / 0.865 / 3.60 s | 0.3356 | +0.0476 (+0.0237 to +0.0726) | (section 3.5) |

Paired CER by group (variant − `exp-ref`):

| group | signals | `exp-diag-comb-cal` | `exp-diag-edge-cal` |
|---|---|---|---|
| A sensitivity | 192 | −0.0283 (−0.0478 to −0.0100) | +0.0478 (+0.0236 to +0.0771) |
| B fading | 30 | +0.0201 (−0.0060 to +0.0485) | +0.1265 (+0.0392 to +0.2403) |
| C fists | 135 | −0.0153 (−0.0230 to −0.0081) | +0.0010 (−0.0192 to +0.0221) |
| D speed | 12 | −0.0492 (−0.1214 to +0.0013) | −0.0503 (−0.1156 to −0.0003) |
| E interference | 16 | +0.2222 (+0.0025 to +0.5082) | +0.5035 (+0.1123 to +0.9745) |
| F tuning | 28 | +0.0247 (+0.0087 to +0.0456) | +0.1967 (+0.0934 to +0.3119) |
| G ragchew | 12 | −0.0160 (−0.0262 to −0.0064) | +0.0651 (−0.0306 to +0.2031) |
| H two-station QSO, oracle | 12 | −0.0483 (−0.0953 to −0.0133) | −0.0541 (−0.1099 to −0.0015) |
| H two-station QSO, oracle (per station) | 24 | −0.0950 (−0.2489 to +0.0035) | −0.0599 (−0.2208 to +0.0564) |
| I Farnsworth | 48 | −0.0297 (−0.0421 to −0.0180) | +0.0162 (−0.0000 to +0.0342) |

Selected speed off by more than ×1.5 (fraction of selection instants, S₅₀₀ ≥ 6 dB) and false
characters outside transmissions (per minute), for the groups where they move:

| group | `exp-ref` | `exp-diag-comb-cal` | `exp-diag-edge-cal` |
|---|---|---|---|
| speed off ×1.5, A | 0.0248 | 0.0189 | 0.0000 |
| speed off ×1.5, B | 0.1608 | 0.0650 | 0.0564 |
| speed off ×1.5, E | 0.1842 | 0.4146 | 0.5007 |
| speed off ×1.5, I | 0.0324 | 0.0043 | 0.0126 |
| false characters, B (per min) | 0.79 | 1.86 | 7.03 |
| false characters, E (per min) | 37.7 | 42.6 | 59.3 |
| false characters, F (per min) | 9.97 | 9.97 | 21.3 |
| false characters, H oracle (per min) | 4.44 | 0.22 | 0.22 |

CPU: 315.0 and 316.4 ms per channel-second; wall 39.6 and 39.8 min. Full tables:
`build/suite/full3/experiments/linux/compare-exp-diag-comb-cal-vs-exp-ref.md` and
`compare-exp-diag-edge-cal-vs-exp-ref.md` (git-ignored).

**Reading (measured, except where marked).**

- Raising the comb's threshold alone, which makes T_P more precise but sparser and later, did **not**
  make decoding worse overall: the paired CER is −0.0152 with an interval reaching 0, and groups A, C, G,
  H (oracle) and I improve with intervals entirely below 0. It did make the first word worse
  (+0.2612) and groups E (+0.2222) and F (+0.0247) worse.
- The edge comb at the default windows is about as bad as `exp-E1-3` (+0.0502 against +0.0476, not
  compared pairwise), so the change of windows is not what made section 3.5 worse; the edge comb is, in
  groups A, B, E and F, with more false characters outside transmissions in B, E and F.
- Both runs cut the selected-speed errors in groups A, B and I and raise them in group E.
- So E1's offline measure (precision and coverage inside transmissions of constant-speed stations at
  S₅₀₀ ≥ 0 dB) did not predict the end-to-end result: at equal precision 0.95 the edge comb covers more
  points (0.749 against 0.589) yet decodes worse. Conjectured, not measured: the edge comb is confident
  and wrong where E1 does not look — outside transmissions, in fading gaps, under interference and
  during tuning — which E1's points exclude; and both higher thresholds hurt first words because
  T_P arrives later.
- These runs choose nothing. Whether the comb's threshold, or the E1 measure itself, should be
  revisited is the owner's decision (section 6).

#### 3.6.1 The comb at intermediate thresholds, and T_P where E1 did not look (observations only)

Owner's request, 2026-10-01, after choosing option A (Task 14 runs against `exp-ref`; the comb at
0.2506 is evaluated on the held-out seeds in Task 15): the information option C would have given,
where it is cheap.

**Offline** (`build/suite/full3/experiments/tp_observe.py`, output `linux/tp-observe.md`, both
git-ignored; exp-ref's branch-1 posteriors, no decoding). The comb's thresholds for precision 0.85 and
0.90 on E1's points are 0.10230 and 0.14874 (dimensionless score). Measured:

- Inside transmissions of groups D, E and F (not among E1's points): under interference (group E)
  every configuration's precision is 0.57–0.68, so a neighbor's keying gives a wrong T_P about a
  third of the time at any threshold; under tuning and drift (group F) the edge comb at 0.03232 is less
  precise and less often available than the comb (0.903 and 0.633 against 0.971 and 0.772 for the comb
  at 0.2506).
- Outside transmissions (padded by 0.5 s): at equal precision 0.95, the edge comb publishes a confident
  T_P 2 to 15 times as often as the comb in groups B, C, G, H and I (for example group B 0.147 against
  0.016 of update points), which fits its extra false characters in B. Against the comb at 0.03,
  though, the edge comb is less often confident outside transmissions, so this alone does not explain
  why it decodes worse than the reference. Every configuration publishes a confident T_P at 0.41–0.88
  of the update points outside transmissions in groups A (noise only) and E; whether the decoder acts
  on T_P in silence was not measured.

**Decoding**, two more runs against `exp-ref` (batch `diag-TP2.json`, fit memory pinned to the
reference's 24 so that all four comb runs differ only in the threshold):

| comb threshold | T_P precision / coverage / median time to first confident | pooled CER | paired CER | paired first-word CER |
|---|---|---|---|---|
| 0.03 (`exp-ref`) | 0.809 / 0.994 / 0.49 s | 0.3060 | — | — |
| 0.1023 (`exp-diag-comb-p85`) | 0.854 / 0.928 / 0.77 s | 0.3098 | +0.0012 (−0.0073 to +0.0093) | +0.0360 (−0.0233 to +0.0976) |
| 0.1487 (`exp-diag-comb-p90`) | 0.900 / 0.834 / 1.12 s | 0.2971 | −0.0089 (−0.0199 to +0.0011) | +0.0958 (+0.0238 to +0.1738) |
| 0.2506 (`exp-diag-comb-cal`) | 0.950 / 0.589 / 2.08 s | 0.2872 | −0.0152 (−0.0280 to +0.0000) | +0.2612 (+0.1410 to +0.3906) |

CPU 314.5 and 315.0 ms per channel-second, wall 39.5 and 39.6 min. Full tables:
`linux/compare-exp-diag-comb-p85-vs-exp-ref.md` and `…-p90-…` (git-ignored).

**Reading (measured).** Across the four thresholds the trade is smooth: a higher threshold gives a
lower overall CER and a higher first-word CER, in step with the later first T_P. No intermediate
threshold keeps the overall gain without the first-word cost; at 0.1023 neither changes measurably. So
the choice of threshold is a priority between first words and the rest, for the owner, not a missed
optimum. At 0.1487 group H (QSO, oracle view) gains clearly in first-word CER, −0.1688 (−0.2932 to
−0.0612), and leans better in CER, −0.0326 (−0.0849 to +0.0001); group F loses CER at every raised
threshold (+0.0098, +0.0158, +0.0247, each interval above 0).

### 3.7 E4 — the fit memory N_mem

Question: how many recent marks and spaces each branch's duration fit remembers. Every new mark or
space multiplies the fit's likelihood tables by λ = e^(−1/N_mem) before adding its own, so an
observation N_mem elements old counts e^(−1) ≈ 0.37 as much as the newest. A long memory gives a
steadier fit; a short one follows a speed change sooner. Reference: N_mem = 24 elements (placeholder,
λ = 0.959).

Variants (each changes only `fit_memory`): `exp-E4-12` (N_mem = 12, λ = 0.920) and `exp-E4-48`
(N_mem = 48, λ = 0.979). Against `exp-ref` (owner's option A, 2026-10-01: comb, threshold 0.03, windows
(2, 5, 10) s). Development set: 509 signals, 74 748 channel-seconds. Batch
`build/suite/full3/experiments/E4.json` (git-ignored), on the Linux machine at `651d57d`:

```
{"name": "E4", "base": "exp-ref", "subset": "dev", "runs": [{"name": "exp-E4-12", "set": {"fit_memory": 12.0}}, {"name": "exp-E4-48", "set": {"fit_memory": 48.0}}]}
.venv/bin/python -m kz4ap_proto.experiments batch --out build/suite/full3 --bench build/linux/bench/kz4ap-bench --spec build/suite/full3/experiments/E4.json --jobs 10
```

(Two other jobs, E9's single-process x_on calibration and an offline analysis
with 6 processes shared the machine during part of this batch, so the CPU and wall times below are
not comparable with other runs; E4's rule does not use them.)

| run | N_mem | pooled CER | paired CER against `exp-ref`, all 509 signals | paired first-word CER | CPU per channel-second | wall |
|---|---|---|---|---|---|---|
| `exp-ref` | 24 | 0.3060 | — | — | 317.7 ms | 40.1 min |
| `exp-E4-12` | 12 | 0.3351 | +0.0255 (+0.0172 to +0.0334) | −0.0424 (−0.0954 to +0.0113) | 303.3 ms | 38.2 min |
| `exp-E4-48` | 48 | 0.2871 | **−0.0151 (−0.0252 to −0.0045)** | +0.0504 (−0.0239 to +0.1234) | 339.0 ms | 43.5 min |

Paired CER by group (variant − `exp-ref`, per signal):

| group | signals | `exp-E4-12` | `exp-E4-48` |
|---|---|---|---|
| A sensitivity | 192 | +0.0206 (+0.0148 to +0.0265) | −0.0188 (−0.0279 to −0.0108) |
| B fading | 30 | +0.0183 (−0.0051 to +0.0432) | +0.0007 (−0.0244 to +0.0296) |
| C fists | 135 | +0.0520 (+0.0416 to +0.0640) | −0.0223 (−0.0321 to −0.0140) |
| D speed | 12 | +0.0148 (+0.0022 to +0.0347) | +0.0057 (−0.0015 to +0.0148) |
| E interference | 16 | −0.1147 (−0.2576 to +0.0210) | +0.0485 (−0.0995 to +0.2082) |
| F tuning | 28 | +0.0084 (−0.0009 to +0.0178) | −0.0079 (−0.0233 to +0.0043) |
| G ragchew | 12 | +0.0800 (+0.0729 to +0.0878) | −0.0322 (−0.0405 to −0.0246) |
| H two-station QSO, oracle | 12 | +0.0229 (−0.0195 to +0.0622) | −0.0319 (−0.1029 to +0.0476) |
| H two-station QSO, oracle (per station) | 24 | −0.0140 (−0.1032 to +0.0880) | +0.0135 (−0.1549 to +0.1496) |
| I Farnsworth | 48 | +0.0410 (+0.0292 to +0.0519) | −0.0264 (−0.0374 to −0.0171) |

Group D's rows (CER of each condition; the speed changes are what a memory length trades against):

| D condition | `exp-ref` (24) | `exp-E4-12` | `exp-E4-48` |
|---|---|---|---|
| 10 WPM | 0.3023 | 0.3023 | 0.3023 |
| 60 WPM | 0.0072 | 0.0197 | 0.0072 |
| ramp 15 → 30 WPM | 0.0000 | 0.0069 | 0.0000 |
| ramp 30 → 15 WPM | 0.0075 | 0.0672 | 0.0224 |
| step 20 → 35 WPM | 0.0349 | 0.0465 | 0.0407 |
| step 35 → 20 WPM | 0.0301 | 0.0301 | 0.0422 |

Selected speed off by more than ×1.5 (fraction of selection instants, S₅₀₀ ≥ 6 dB; compare's "Selected
speed" table):

| group | `exp-ref` (24) | `exp-E4-12` | `exp-E4-48` |
|---|---|---|---|
| A sensitivity | 0.0248 | 0.0529 | 0.0004 |
| B fading | 0.1608 | 0.1959 | 0.0752 |
| C fists | 0.0171 | 0.0722 | 0.0038 |
| D speed | 0.0193 | 0.0193 | 0.0193 |
| E interference | 0.1842 | 0.1330 | 0.3657 |
| G ragchew | 0.0189 | 0.0843 | 0.0003 |
| H two-station QSO, oracle (per station) | 0.0462 | 0.1097 | 0.0107 |
| I Farnsworth | 0.0324 | 0.0926 | 0.0077 |

Full tables: `build/suite/full3/experiments/linux/compare-exp-E4-12-vs-exp-ref.md`,
`compare-exp-E4-48-vs-exp-ref.md`, `summary-E4.md` (git-ignored).

Rule (pre-registered): adopt a value only if its pooled paired CER interval lies entirely below 0 and
neither group D nor group I has a paired interval entirely above 0; if both 12 and 48 qualify, the one
with the lower pooled mean. Otherwise keep 24. **Outcome: 48 qualifies, 12 does not.** 48: pooled
−0.0151 (−0.0252 to −0.0045), entirely below 0; group D +0.0057 (−0.0015 to +0.0148) and group I
−0.0264 (−0.0374 to −0.0171), neither entirely above 0. 12: pooled +0.0255 (+0.0172 to +0.0334),
entirely above 0. **Adopted: `fit_memory = 48`** (status measured, E4); `exp-E4-48` is the current
reference for E5.

Observed, not part of the rule: with 48 the first-word CER rises in group A, +0.1276 (+0.0132 to
+0.2610), and the selected-speed errors in group E rise (0.184 → 0.366), while they fall in every
other group with such instants. Group D's speed changes read slightly worse with 48 in three of four
conditions (each condition is 2 signals; not tested separately).

**Tests after the change** (`pytest training -q`, local): 3 failed, 244 passed, 4 xfailed with
N_mem = 48. One asserted the default's decay by value (`test_copy_is_independent_and_best_needs_an_observation`,
λ = e^(−1/24) written out); it now reads `CFG.fit_memory`. Two are findings, left failing and reported
to the owner (section 6): `test_the_fit_follows_a_speed_step_within_its_memory` (20 → 35 WPM step,
true dit 34.29 ms; the test expects T within 5% after 72 new marks and spaces, 3 × the old N_mem; the
fit gives 37.50 ms after 72, 35.60 ms after 96, 34.88 ms after 120 and 34.58 ms after 144: it now
needs about twice as many elements to follow a step) and the channel test
`test_a_same_speed_turnover_keeps_the_previous_over_s_fit` (two overs at 25 WPM; it now reads
"CCQ DE K1ABC K EE TT EE TT K1ABC": a spurious C before the first over's CQ, while the second over,
which N_mem = 24 read as "… U1ABC", is now complete).

**Owner's decision (2026-10-01): keep N_mem = 48** (option A), with these measured costs, which the rule
does not look at, stated: group A's paired first-word CER +0.1276 (+0.0132 to +0.2610), entirely above
0; the pooled paired first-word CER +0.0504 (−0.0239 to +0.1234), not distinguishable from 0; group E's
selected speed off by more than ×1.5 at 0.366 of selection instants against 0.184. The tests, as the owner
directed: the speed-step test now asks for T within 5% after 3 × N_mem elements ("within three memory
lengths": 144 at N_mem = 48, where the fit gives 34.58 ms against the true 34.29 ms), and the turnover
test is a strict expected failure stating the finding (spurious leading C; cause not traced); the
neighboring strict expected failure (the whole second over) still fails, now only on that leading C,
and its reason says so. `pytest training -q` (local): 246 passed, 5 xfailed.

## 4. Final evaluation (Task 15)

## 5. The comparison for the owner (no acceptance gate), and what stage 1 cannot measure

## 6. Open items for the owner

- **E1–E3 (section 3.5): a more precise but sparser and later T_P made decoding worse.** Measured, on the
  same 175 551 update points (offline, `exp-ref`'s posteriors), the T_P each run decoded with:
  `exp-ref` (comb, placeholder threshold 0.03, windows (2, 5, 10) s): precision 0.809 (0.786–0.832),
  coverage 0.994 (0.993–0.994), median 0.49 s from a transmission's start to its first confident
  estimate; `exp-E1-3` (edge comb, calibrated threshold 0.02214, windows (5, 10) s): precision 0.950
  (0.935–0.964), coverage 0.865 (0.838–0.889), median 3.60 s. Measured end to end: the paired CER
  (`exp-E1-3` − `exp-ref`, per signal, mean over 509 signals) rose by +0.048 (+0.024 to +0.073), most
  in groups B, E and F; the pooled CER went from 0.3060 to 0.3356. The selected-speed errors fell
  (group A 2.5% → 0%, B 16% → 3.5%) except in group E (18% → 48%), and false characters outside
  transmissions rose in A, B, E, F, G and I and fell in H. By the plan's rule the periodicity defaults
  were reverted. The run changed the method, the windows and the threshold together, so the regression
  is attributed to none of them. Conjectured mechanisms, none measured: the later and sparser T_P; the
  T_P prior's use in the fit (a gate at the confidence threshold with width 0.1 in ln T, a
  reinterpretation of "weighted by its confidence"); its effect on branch selection. Separately, the
  spectrum fit reached 0.95 precision at no threshold (it most often locks on about 3T).

- **Diagnostic runs after E1–E3 (section 3.6).** The edge comb, not the windows, made `exp-E1-3`
  worse. The default comb with its threshold raised from 0.03 to the calibrated 0.2506 gave paired CER
  −0.0152 (−0.0280 to +0.0000) overall, better in A, C, G, H and I, but first-word CER +0.2612 and worse in
  E and F. E1's offline measure did not predict decoding. Not pre-registered, so nothing was adopted;
  `exp-ref` (threshold 0.03) stays the reference.

- **E4 (section 3.7): N_mem = 48 adopted by the rule; two Python tests now fail.** The channel test
  `test_a_same_speed_turnover_keeps_the_previous_over_s_fit` reads "CCQ DE K1ABC K EE TT EE TT K1ABC"
  (a spurious leading C; the second over is now right), and the fit's speed-step test needs about 144
  elements, not 72, to come within 5% of a 20 → 35 WPM step (37.50 ms after 72, true 34.29 ms). The
  plan says a channel test that fails after an adopted change is reported, not reverted silently.
  Owner's decision (2026-10-01): keep 48; the speed-step test now asks for 3 × N_mem elements, and
  the turnover test is a strict expected failure stating the finding (cause not traced).

- **Extra configurations for the final evaluation (owner, 2026-10-01).** Task 15 evaluates, on the
  held-out seeds 2 and 3 and beside the prototype with every adopted value, two more configurations
  that differ from it only in the comb's confidence threshold: 0.2506 (T_P precision 0.95 on E1's
  points) and 0.1487 (precision 0.90). They show the trade between overall and first-word CER
  (section 3.6.1) on data no choice was made on. Neither is adopted by a rule; the owner decides from
  the comparison.
