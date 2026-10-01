# Milestone 2b, Stage 1: Results Record

Plan: `docs/plans/2026-09-30-milestone-2b-stage-1-filter-bank-prototype.md`. Spec:
`docs/design/2026-09-30-filter-bank-speed-estimator-design.md`. Every number here is measured unless
marked otherwise; every interval is a bootstrap 95% interval (over signals for CER, over channels for
the periodicity and speed statistics). S₅₀₀: key-down carrier power over noise power in 500 Hz, dB.

## 1. Conditions

- Machine: 12th Gen Intel(R) Core(TM) i7-12700H (6 P-cores, 8 E-cores, 20 logical CPUs), Windows 11,
  on AC power. Every prototype process opts out of Windows power throttling
  (`kz4ap_proto.power.disable_power_throttling`, Task 11p). Code: `6d610be` (the experiment harness).
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

- Noise level: both measured (E10 arms a, b, c); spec §4.2 and §5 say it is decided by E10.
- Periodicity: the comb on Π = 2T with teeth ±15% of T is the default; the edge comb is E1's third arm;
  spec §4.4 and §6 corrected.
- First-mark threshold calibrated by measurement (E9); keyed samples only while the amplitude is unknown.
- The whole experiment list kept; runs in the background.
- 2026-10-01: the long experiment time (about 31–39 h, plus 5–7 h for the final pass) accepted; no
  speed-ups that change outputs.

## 3. Experiments

(One subsection per experiment, in the order run: question; setting(s) and command(s); development
set size (signals, channel-seconds); the table from `experiments/…md`; the decision rule as written in
the plan; its outcome; the value adopted and its status.)

## 4. Final evaluation (Task 15)

## 5. The comparison for the owner (no acceptance gate), and what stage 1 cannot measure

## 6. Open items for the owner
