# Checkpoint 1: first scores (2026-10-31)

**Answer.** The full Bayesian model clearly beats the bank under fading, and its fast form keeps the gain at 5 ms CPU per
channel-second: the per-sample `viterbi` decoder reads B2 fading at CER 0.250 and the `segmental` decoder at 0.257 against
the bank's 0.553, on the same recordings. CER is edits over reference symbols, spaces counted, pooled over the scored
signals. The bank still wins one group, I2 Farnsworth. No decoder has yet been scored against truth on a real signal.

## Runs

All on the Linux machine, under load from other jobs; seed-1 recordings of the development set `DEV2`; oracle path (the
decoder is told the frequency and nothing else) unless a row says detector.

| Label | Decoder and commit | Recordings | Channel-seconds | CPU, ms per channel-second | Score table in `docs/plans/data/` |
|---|---|---|---|---|---|
| viterbi, slice | `viterbi`, 8678adc | 154 | 349 212 | 696.5 | `2026-10-31-checkpoint-1/viterbi-8678adc-slice.csv` |
| segmental, slice | `segmental`, d78d45f | 154 | 349 212 | 5.2 | `2026-10-31-checkpoint-1/segmental-fast2-slice.csv` |
| bank, slice | `bank`, 8678adc | 154 | 349 212 | 30.6 | `2026-10-31-checkpoint-1/bank-8678adc-slice.csv` |
| segmental, whole | `segmental`, d78d45f | 708 | 1 395 785 | 5.6 | `2026-10-31-checkpoint-1/segmental-fast2-whole.csv` |
| bank, whole | `bank`, task D5's replay | 498 | not recorded here | not recorded here | `2026-10-06-development-set/d5-bank.csv` |

The slice is not a random sample: within a cell, recordings are ordered by condition, so its low-level cells carry more
weight than the whole set's (A2 reads 0.518 on the slice and 0.241 on the whole). Compare decoders within a column of runs,
never a slice against a whole. Its only purpose is that the `viterbi` decoder, at about 700 ms per channel-second, takes
about 6 hours on 154 recordings. The whole set's 708 recordings are 498 oracle recordings and 210 on the detector's path.

`2026-10-31-checkpoint-1/conditions-b2-c2.csv` gives each B2 and C2 signal's Doppler spread (Hz) and keying, joined on
`test_case` and `index`. Every number below is computed from these rounded tables.

## Scores by group

| Group | viterbi, slice | segmental, slice | bank, slice | segmental, whole | bank, whole |
|---|---|---|---|---|---|
| A2 sensitivity | 0.528 | 0.518 | 0.543 | 0.241 | 0.287 |
| A2, detector's path | | | | 0.274 | |
| B2 fading | 0.250 | 0.257 | 0.553 | 0.251 | 0.557 |
| C2 fists | 0.063 | 0.064 | 0.171 | 0.061 | 0.157 |
| D2 speed changes | 0.000 | 0.000 | 0.021 | 0.000 | 0.027 |
| E2 interference | 2.337 | 0.845 | 0.986 | 0.691 | 0.776 |
| F2 tuning | 0.000 | 0.001 | 0.417 | 0.087 | 0.133 |
| G2 QSO, same track | 0.050 | 0.065 | 0.362 | 0.055 | 0.206 |
| H2 QSO, separate tracks | 0.046 | 0.045 | 0.081 | 0.058 | 0.198 |
| I2 Farnsworth | 0.517 | 0.508 | 0.137 | 0.523 | 0.261 |
| S2 stretch | 0.494 | 0.500 | 0.506 | 0.264 | 0.337 |

E2 counts only the wanted stations. E2's slice holds 18 signals, G2's 4, H2's 8: small. The slice's bank on F2 (live bench,
0.417) differs from the whole set's (D5's replay, 0.133, over more cells); the two bank columns are different runs.

## Whole set by condition

| A2, S500 (dB SNR in 500 Hz) | segmental | bank |
|---|---|---|
| -10 to -5 | 0.971 | 0.998 |
| -5 to 0 | 0.601 | 0.739 |
| 0 to +5 | 0.156 | 0.201 |
| +5 to +10 | 0.007 | 0.030 |
| +10 to +15 | 0.000 | 0.019 |
| +15 to +20 | 0.000 | 0.018 |

| B2, S500 at least +10 dB, Doppler spread f_D | segmental | bank |
|---|---|---|
| 0.05 to 0.2 Hz | 0.089 | 0.232 |
| 0.2 to 0.6 Hz | 0.103 | 0.389 |
| 0.6 to 2 Hz | 0.102 | 0.591 |
| 2 to 5 Hz | 0.128 | 0.753 |

| C2, S500 at least +10 dB, keying | segmental | bank |
|---|---|---|
| machine | 0.000 | 0.019 |
| computer | 0.000 | 0.019 |
| paddle | 0.022 | 0.048 |
| bug | 0.031 | 0.118 |
| hand | 0.148 | 0.436 |

## Findings

1. **Measured.** The stop rule's test passes: the full model beats the bank under fading (0.250 against 0.553), and the
   fast form holds it (0.257).
2. **Measured.** The fast form loses little against the full model on the slice: B2 0.257 against 0.250, C2 0.064 against
   0.063, G2 0.065 against 0.050, S500 +5 to +10 dB 0.012 against 0.009. It wins E2 (0.845 against 2.337) and near
   threshold (S500 -5 to 0 dB, 0.827 against 0.945).
3. **Measured.** CPU against the bar (at most 40 ms required, about 8 ms the goal): `segmental` 5.6 ms per channel-second
   on the whole set under load. This depends on the level (independent review, desktop, decoder alone): 3.7 ms at +20 dB
   S500, 13 ms at 0 dB S500, 24 ms at -3 dB S500.
4. **Measured.** I2 Farnsworth is the group the bank wins. The letters are right and a word space follows each one:
   without spaces, `segmental` reads 0.019 on the slice against the bank's 0.143 (from the result files, not the tables).
5. **Conjectured.** These scores flatter the text prior: the on-air symbol prior is the table the test text is drawn from.
   In the review's own recordings, removing it raised CER by up to 3.6 times. Real text sits somewhere between.

## Limits

The test set's noise is white. The independent review found failures it cannot show, some fixed at d78d45f (N0 below the
channel's real floor; flutter at strong levels; one extreme sample deafening a channel) and some not (a neighbor within
about 60 Hz; impulses; a tune-up writing T's). See `docs/signal-processing.md`, appendix A.8e.

## Reproducing

Recordings: `python -m kz4ap_synth.suites` from the seeds. Runs: `--decoder viterbi` at 8678adc and `--decoder segmental`
at d78d45f, with `--only` selecting the recordings named in each table's `test_case` column.
