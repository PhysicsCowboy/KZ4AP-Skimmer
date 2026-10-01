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
  band, crowded, pauses, strong, tune-up and first-sample are scored through the detector
  (**detector-only** recordings).
- **Scoring**: decoding every station of a recording and comparing the text with the labels. **CER**
  (character error rate) is the minimum number of symbol insertions, deletions and substitutions that turn the decoded text into the reference, divided by the number of reference symbols (a fraction; prosigns and word spaces count as symbols; `docs/signal-processing.md` section 11). A **scoring** is one recording
  scored against one label file.
- **Recorded channel streams**: the engine run once with a tap that writes each channel stream to disk
  (`build/suite/full3/channels/`, complex64 files) exactly as the channelizer delivers it, before any
  decoder. The prototype reads these files, so it and the C++ decoders see the same samples, and
  detection and channelizing are not repeated per experiment. **Oracle channels** are one per labeled
  station; **detector channels** are whatever the detector opened, false tracks included.
- **Oracle copy**: a detector-only recording also scored on its oracle channels. **Engine copies**: the
  Envelope and Matched scorings of those oracle copies (27 recordings × 2 decoders), so that the
  prototype is compared with them on the same channels.
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
- **Development set**: seed 1 of the suite (21 scorings, 525 channels, 74 748 channel-seconds, 509
  signals). Seeds 2 and 3 are held out for the final evaluation, so that values chosen on seed 1 are
  judged on data they were not chosen on.
- **Reference run** (`exp-ref`): the prototype with every default. Each variant is compared with it
  signal by signal: the **paired CER difference** (variant − reference, per signal, pooled) with its
  bootstrap 95% interval.
- **Pre-registered rule**: the decision rule for each experiment, written in the plan before its runs.
  It decides whether a variant is adopted, so the choice is not made after seeing the numbers.
- **Batch**: several variants of one experiment run one after another and each compared with the
  reference (for example `E10.json`).

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
  channels' recorded streams (120 scorings, the 27 oracle copies of the detector-only groups included), mixed to 0 Hz at the labeled frequency and drift.
- Development set (experiments): `experiments.DEV` — seed 1 of groups A, C, D, E, F, G, H (oracle, both
  views), I, and B's mixed-style recording. Held out: seeds 2 and 3. It is 21 scorings, 525 channels,
  74 748 channel-seconds, 79% of them inside transmissions (Task 11p's count). `experiments.DEV_E5`: 10
  scorings, 259 channels.
- Run times: group I generation 4 min 37 s and scoring 24 s (Task 1); recording the streams 158 s for
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

All defaults (`ProtoConfig()` at `6d610be`), branch 1's posteriors kept for E1–E3. 21 scorings, 525
channels (74 748 channel-seconds); 509 signals scored. **Pooled CER 0.3060** (all groups pooled, S₅₀₀
from −10 dB up; per-group rows in the E10 compare files). Decoding CPU 317.7 ms per channel-second,
wall time 40.1 min (Linux machine, 10 workers; the same command with
`--bench build/linux/bench/kz4ap-bench --jobs 10`).

On the Windows machine the same run gave the same pooled CER, 750.2 ms of CPU per channel-second, and
about 71 min of wall time, run in two parts. The first part (18 workers, 01:23–02:04) decoded 14
recordings and was then stopped by the agent harness because the machine ran low on memory (other
applications held most of the 31.7 GB). The decode resumes, so the second part (10 workers, 30.3 min)
decoded the other 7 and scored all 21. In the second part the prototype's processes used at most
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

The Windows runs of this batch, kept for the record: variant (b) gave the same table except CPU
(567.2 ms per channel-second, 71.5 min wall); variant (c) was stopped after 16 of 21 recordings (the
agent harness, low memory at about 3.7 GB free) and was completed only on the Linux machine.

## 4. Final evaluation (Task 15)

## 5. The comparison for the owner (no acceptance gate), and what stage 1 cannot measure

## 6. Open items for the owner
