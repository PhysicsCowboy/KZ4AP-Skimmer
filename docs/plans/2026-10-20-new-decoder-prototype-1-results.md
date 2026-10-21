# New decoder (viterbi), first prototype: Results Record

Plan: `docs/plans/2026-10-20-new-decoder-prototype-1.md` (approved through its decisions by the owner, 2026-10-20).
Branch `new-decoder-1`. Every number here is **measured** unless marked otherwise (**derived**, **conjectured**, and the
classes of section 1.1), and is computed from the committed tables named with it.

**Status: conditions only.** This is the record's first commit (plan Task 7). It states the conditions every later run
shares: the decoder and its defaults, the bench command line, the machines, the sets, the code and the reference runs to
compare against (section 1), and the evidence for two findings made while the decoder was built (section 1.7). The
measurements come in the plan's Tasks 9 to 12, each as its own numbered section after section 1, with its rule, its
commit and its committed score tables.

## 0. Terms used in this record

- **The new decoder:** `viterbi` (`kz4ap-bench --decoder viterbi`; in the code `FrontEnd::Viterbi` and
  `ViterbiDecoder`). Of the four decoders (Envelope, Matched, the bank and the new decoder) it is selectable, not the
  default. Its design is `docs/signal-processing.md` section 8d and appendix A.8d.
- **Test case:** one recording with one labels file. A decoder is scored on a recording.
- **Signal:** one station's transmission in a recording, with its label.
- **Oracle channel:** one station's channel stream, opened at its labeled frequency for the whole recording
  (`kz4ap-bench --oracle`); the signal detector is bypassed. Every run of the plan uses oracle channels.
- **Channel-second:** one second of one channel's stream, the unit of decoding cost.
- **S₅₀₀:** the key-down carrier power over the noise power in 500 Hz, in dB: "dB S₅₀₀" (the labels' `snr_db`).
- **Nat:** the natural-logarithm unit of information, used for scores: a prior of probability p costs ln(1/p) nats.
- **Piece:** the decoder's time step, the sum of 3 channel samples: 2.000 ms at the channel's 1 500 samples/s.
- **Story:** one continuous account of what has been sent on the channel since its start: no signal, or a mark or gap
  that has lasted some pieces, at a speed, a Farnsworth factor, a node of the Morse tree, with its own signal level and
  its own text. Its score, in nats, is the log-likelihood of the story and the signal so far against "noise only,
  throughout".
- **Beam, ceiling:** the beam drops every story more than `beam_nats` behind the best; the ceiling keeps at most
  `ceiling` stories.
- **Newborn, line:** a newborn is a story that starts a mark, from silence (an over starts) or as a rescue (the sender
  begins a new story at a letter start); a line is a newborn and its descendants.
- **Over:** one transmission by one station.
- **Search error:** a difference between the text a run decodes and the text of a run with a much wider beam and higher
  ceiling (the wide-beam reference), counted per 1 000 reference letters (A.11).
- **Genie run:** a run in which the decoder is given the truth about something it normally estimates: the sender's
  timing, the carrier's frequency or the fading's spread, taken from the labels, to bound what a better estimate could
  gain.
- **Variant:** one setting of the decoder in a comparison.
- **DEV2:** the development set, seed 1, at the owner's sizes (section 1.4). **N2:** the noise-only group (section 1.4).
- **The bank:** the bank decoder at its current defaults, frozen as the reference. **d5-bank:** its run on DEV2
  (section 1.6).
- **CER:** edits over reference symbols, per signal, a ratio (A.11).
- **The plan's tasks** (its section 9): Task 7 puts the decoder into the engine and gives the bench its options (it is
  built in two steps: 7a, the decoder in the engine and `docs/signal-processing.md`; 7b, the bench's options, the
  README and this record's section 1); Task 8 is the noise-only group, the suite runner and the analysis in Python;
  Task 9 is the first measurement (R0, below); Tasks 10 to 12 are the long runs.
- **The plan's runs R0 to R11:** the measurements of the plan's section 6.1, in words: R0, cost and instrumentation at
  the defaults and the reference settings; R1, the wide-beam reference; R2, the beam; R3, the ceiling; R4, the default
  against the bank; R5, the rates of overs and restarts; R6, the fading spread; R7, a finer speed grid; R8, the genie
  fist; R9, the tracker and the genie frequency; R10, the genie fading; R11, the hiss estimator's error.
- **Classes of a value** (as in the appendix's parameter table): *derived* (follows from stated facts), *measured*,
  *heuristic* (a choice without a derivation or a measurement), *placeholder* (kept until a measurement replaces it),
  *sourced* (taken from a named source), *counted* (counted on the development set), *owner* (the owner's decision),
  *conjectured* (a statement nobody has measured or derived).

## 1. Conditions

### 1.1 The decoder and its defaults

`ViterbiConfig` (`engine/include/kz4ap/viterbi/config.hpp`) is the decoder's whole configuration, in physical units; the
table gives each field's default, its unit and its class, as `docs/signal-processing.md` appendix A.10 does for the same
values. Pieces, bins and samples appear only where a value is used. The bench records all 34 fields in every viterbi
result file (section 1.2).

| Field | Default | Unit | Class and meaning |
|---|---|---|---|
| `piece_s` | 0.002 | s | derived: 3 channel samples at 1 500 samples/s; it must be a whole number of channel samples |
| `dot_min_ms` | 12.0 | ms | owner: the dot of the fastest speed, 100 WPM |
| `speed_ratio` | 1.1 | none | owner: each speed's dot is this times the previous speed's |
| `speed_count` | 32 | count | owner: speeds 0 … 31, dots of 12.0 to 230.3 ms, 100 to 5.2 WPM |
| `factor_ratio` | 1.1 | none | owner: the Farnsworth factor F stretches the gaps between letters and words by 1.1^f |
| `factor_count` | 23 | count | owner: factors f = 0 … 22, F = 1 to 8.14 |
| `fist_medians_dots` | 1, 3, 1, 3, 7 | dots | placeholder, heuristic: the median length of a dot, dash, element gap, letter gap and word gap (the last two times F); the ITU sending standard's ratios, not a measurement of senders |
| `fist_sigmas` | 0.3 for each | standard deviation of ln(length) | placeholder, heuristic |
| `fist_shifts_dots` | 0 for each | dots | a sender's weighting added to each kind's length; 0 in the baseline, set only by the genie fist |
| `beam_nats` | 40.0 | nats | placeholder: at least 28 nats (derived) |
| `ceiling` | 1 700 | stories | placeholder; open: the owner's decision |
| `fading_spread_hz` | 1.0 | Hz | placeholder, swept by the plan: the Doppler spread f_D of the fading, twice the standard deviation of a Gaussian Doppler spectrum |
| `block_loss_db` | 0.5 | dB of signal energy | heuristic: the largest loss of a coherent block; with `fading_spread_hz` it gives the block, B = 195 pieces = 390 ms (derived) |
| `keying_rise_s` | 0.005 | s | placeholder, heuristic: the raised-cosine rise at a mark's start, the generator's edge |
| `over_rate_per_s` | 0.05 | 1/s | placeholder (the development set's own rate): P = 1.0 × 10⁻⁴ per piece of silence that an over starts |
| `rescue_epsilon` | 1.0 × 10⁻³ | probability per letter start | placeholder (the development set's own count) |
| `word_end_p` | 1/3.06 = 0.3268 | probability | measured (VE3NEA's mean word length in CW Skimmer's decodes); a placeholder until recounted on real traffic |
| `over_end_q` | 1/24 = 0.04167 | probability | counted (the development set's own); a placeholder until recounted |
| `level_min_db` | −15.0 | dB S₅₀₀ | placeholder: a newborn's level is equally likely anywhere in dB between this and `level_max_db` |
| `level_max_db` | 85.0 | dB S₅₀₀ | placeholder |
| `quadrature_order` | 5 | count | owner (the plan's decision on the level's numerical form): Gauss–Hermite quadrature of order 5, within 0.01 nats over the tested range |
| `hiss_half_width_hz` | 2 000 | Hz | placeholder, heuristic: the hiss estimator reads the spectrum within ± this of the station |
| `hiss_percentile` | 25.0 | percent | placeholder, heuristic: the rank, among those bins, taken as the hiss |
| `hiss_average_s` | 1.0 | s | heuristic: the one-pole average's time constant, the signal detector's rule and value |
| `tracker_window_s` | 1.0 | s | placeholder: the carrier tracker's window |
| `tracker_hop_s` | 0.5 | s | placeholder: the time between its measurements |
| `tracker_search_hz` | 15.0 | Hz | placeholder: the half-width of its search |
| `tracker_gate_db` | 9.0 | dB relative to the mean noise power per bin | derived (the owner's decision B of 2026-10-20 on the tracker's design): a window is measured only if its peak stands this far above the noise |
| `tracker_drift_sd_hz_per_s` | 0.3 | Hz/s | sourced: older rigs' specified warm-up drift |
| `tracker_drift_noise` | 1.5 × 10⁻⁴ | Hz²/s³ | derived from the sourced drift over a heuristic ten minutes |
| `genie_frequency_offset_hz` | none | Hz from the channel's center, at stream time 0 s | genie runs only |
| `genie_frequency_drift_hz_per_s` | 0.0 | Hz/s | genie runs only |
| `genie_fading_spread_hz` | none | Hz | genie runs only: the true spread, in place of `fading_spread_hz` |
| `stats` | false | on or off | instrumentation: whether the search keeps its statistics (they grow with the channel's time, about 350 MB per channel-hour) |

The search also pays priors that are not configuration: a speed 1/32 (3.466 nats) and a Farnsworth factor 1/23
(3.135 nats) where taken, and a letter 1/62 (4.127 nats) at its start, all owner (flat, no text model).

### 1.2 The bench command line

    kz4ap-bench RECORDING.wav --labels LABELS.json --json RESULT.json --oracle --decoder viterbi
                [--set NAME=VALUE ...] [--genie fist|frequency|fading ...]

- `--oracle` opens the oracle channels from the labels (`--labels` is then required). `--decoder viterbi`
  selects the new decoder (`--front-end` is the option's old name).
- `--set NAME=VALUE`, repeatable: NAME is a field of `ViterbiConfig`, exactly as the first column of the table in
  section 1.1 spells it, and VALUE is in the field's unit, as JSON (`30`, `1e-3`, `[0.3, 0.3, 0.3, 0.3, 0.3]`, `true`,
  `null` for an optional value). A name that is not a field, or a value of the wrong kind, stops the bench before it
  processes any sample, with a message that names the field; the ranges are the library's own, so a value it cannot use
  (a beam that is not positive, say) is refused by the engine or the decoder when they are built, with their message,
  also before any sample is processed. For example, the plan's reference settings are `--set beam_nats=80 --set
  ceiling=17000`, and its finer speed grid `--set speed_ratio=1.05 --set speed_count=61`.
- `--genie fist|frequency|fading`, repeatable, needs `--oracle`: each oracle channel's decoder is given the truth about
  its station, read from the station's label:
  - `frequency`: the label's `freq_offset_hz` (its frequency at `start_s`) and `drift_hz_per_s`, so that the carrier is
    at `freq_offset_hz + drift_hz_per_s × (t − start_s)` at stream time t, as the generator makes it;
  - `fading`: the label's `fading_hz`, the fading's spread f_D, 0 Hz for a signal that does not fade;
  - `fist`: the label's speed as the dot, 1 200 / WPM ms, the keying style's median and standard deviation of
    ln(length) for each of the five kinds (the generator's `keying.STYLES`, copied in `bench/src/viterbi_json.cpp` and
    checked against it by `training/tests/test_keying.py`), and the label's `imbalance_dits` as the shifts, + for the
    marks and − for the gaps. A style with an exact kind (σ = 0: "machine") cannot be given to the fist, which needs
    σ > 0, and the decoder cannot resolve a length better than a piece: every σ is at least one piece over the kind's
    median length, `piece_s` / (median × dot) (heuristic: an exact sender's timing known to one piece, 2 ms; it binds
    for the machine style and, at a fast speed, for the small σ of the others). The Farnsworth factor is not given,
    only searched.
  - A label with no single truth to give is refused, by its number in the labels file: a QSO label of several stations
    (each has its own carrier and fist; the per-station labels of a QSO have one) for `frequency` and `fist`, and a
    signal that changes speed (`wpm_end`) for `fist`.
- `--set` and `--genie` belong to `--decoder viterbi`; `--genie` without `--oracle` is an error.
- The result file (`--json`) of a viterbi run gains `config` (every field of `ViterbiConfig` as the run used it, so
  the run can be repeated from the file), `settings` (the `--set` arguments as given) and, with `--genie`, `genie` (the
  kinds and each channel's values). Its other members are those of the other decoders' result files. The decoder
  prints its best story's text when the recording ends, as the text of each track, with no corrections.
- The suite runner calls the bench once per test case, ten at a time on the Linux machine: `python -m
  kz4ap_synth.suites run --out DIR --bench PATH --decoder viterbi --jobs 10 --name NAME [--set NAME=VALUE ...] [--only
  REGEX]`. The results go to `DIR/results/NAME/`, and a run gets a new name every time.

### 1.3 The machines

- **The Linux machine** (Ubuntu 22.04, 10-core Intel Xeon (Ice Lake), g++ 11.4): every measurement of the plan runs
  here, built with the CMake preset `linux` (Release). Builds on different platforms may differ in the last bit of a
  floating-point value, so a run is compared only with runs of the same platform.
- **The development desktop** (Intel i7-9700F, MSVC): builds, tests and the scratch runs of section 1.7; its timings are
  for comparison only.

### 1.4 The sets

- **DEV2, seed 1** (`docs/plans/2026-10-06-development-set-results.md`, sections 4.4 and 5.1): 498 oracle recordings,
  14 204 oracle channels, 1 065 742.1 channel-seconds, of which 14 114 signals are scored (the other 90 channels are
  E2's unscored interferers). Generated by `python -m kz4ap_synth.suites generate --suite dev2 --seeds 1 --per-cell
  A2=48 --per-cell B2=24 --per-cell C2=24 --per-cell I2=24 --out DIR`. The scored signals by group, counted in the
  rows of `d5-bank.csv`:

  | Group | Conditions | Scored signals |
  |---|---|---|
  | A2 | sensitivity: S₅₀₀ against speed | 6 720 |
  | B2 | fading: the fading spread against S₅₀₀ | 1 920 |
  | C2 | keying styles (machine, computer, paddle, bug, hand), each with an imbalance | 3 600 |
  | D2 | speed changes: steps and ramps | 108 |
  | E2 | a neighbor station at 0 to 120 Hz | 90 |
  | F2 | tuning offsets and drift | 80 |
  | G2 | QSOs on one track | 20 |
  | H2 | QSOs on two tracks, scored per station | 40 |
  | I2 | Farnsworth spacing | 864 |
  | S2 | A2's speed-cell-5 signals (20.10 to 25.30 WPM) stretched in time by exactly 2, S₅₀₀ 3.01 dB lower, keying edges 10 ms instead of 5 ms | 672 |

  The scored signals' S₅₀₀ lies between −10.98 and +21.33 dB S₅₀₀ (the three signals above +20 dB S₅₀₀ are H2
  stations, whose labels shift each station's S₅₀₀ by up to ±6 dB from the QSO's); E2's unscored neighbors reach
  higher (to +26 dB S₅₀₀ by the design: `docs/signal-processing.md`, A.8d; not counted here).
- **N2, the noise-only group:** 4 recordings at 48 kHz (DEV2's rate, noise σ = 0.02 FS per complex sample) of 25
  channels × 360 s, 36 000 channel-seconds. Each channel is opened at the frequency of a labeled station at S₅₀₀ −60 dB
  (its key-down power is 10⁻⁶ of the noise power in 500 Hz), which only opens the oracle channel, so every character
  a decoder prints there is false, and the false characters per hour of channel time are what N2 measures. Generated by
  `python -m kz4ap_synth.suites generate --suite n2 --seeds 1 --out DIR`. N2 is not part of DEV2.

### 1.5 The code

Branch `new-decoder-1`. The decoder, its place in the engine and `docs/signal-processing.md` are Task 7a's commits
`2923863` and `10299c1`; the bench's options (section 1.2), this record and its tables are Task 7b's commit, the commit
that adds this file. The code of a run is the branch at the commit the run's section names, and no section is written
before its runs finish on a committed state.

### 1.6 The reference runs to compare against

- **d5-bank:** the bank decoder at its current defaults on DEV2, seed 1, on the Linux machine, 28.85 ms of CPU per
  channel-second (`docs/plans/2026-10-06-development-set-results.md`, section 6). Its committed per-signal score table
  is `docs/plans/data/2026-10-06-development-set/d5-bank.csv`, 14 114 rows, one per scored signal, with the conditions
  of the signal, its cell, its edits and its reference symbols; Task 8 added the two columns `farnsworth_wpm` and
  `farnsworth_factor` (0.01) and left the other 17 columns byte for byte unchanged.
- The comparisons leave out I2's signals above a Farnsworth factor of 8 on both sides, and list them apart (181 of I2's
  864 signals: 20.9%, counted from the table's `farnsworth_factor`).
- The new decoder's own reference is the wide-beam run (R1: beam 80 nats, ceiling 17 000), against which its search
  errors are counted.

### 1.7 Evidence for findings made while building

Task 7a found two behaviors of the decoder while building it. Both stand in `docs/signal-processing.md` (section 8d,
"Open", and appendix A.8d, limitations) and in `engine/include/kz4ap/viterbi/search.hpp` ("what remains", item 4).
**The owner's decisions on both are pending.** The measurements were scratch runs through the decoder (temporary tests,
removed before the commit, not tests); this section states what they showed, with the conditions known, and the
outputs of the runs are committed as three tables in `docs/plans/data/2026-10-20-new-decoder-prototype-1/`, the
console's wrapped lines joined. Each claim carries its class.

**Conditions of the runs.** The signal "PARIS PARIS" (finding 1) or "TEST" (finding 2) at 25 WPM, machine keying (exact
PARIS timing), 5 ms raised-cosine keying edges inside each key-down interval, as the generator makes them, on DEV2's
noise (white, σ = 0.02 FS per complex sample at 48 kHz), one station on one channel; the decoder at its defaults except
where a table says otherwise, given the noise's own N₀ = σ² / fs = 8.33 × 10⁻⁹ FS²/Hz in place of the hiss estimator's
(derived from the noise). The columns of the tables are the runs' conditions; the code of the scratch tests was removed
with them, so the station's frequency within its channel and how the decoder was centered on it are not recorded. At 25
WPM every mark and gap is a whole number of pieces long (the dot 24 pieces, the dash 72, the gaps 24, 72 and 168), and
the key-down starts at 0.5 s (250 pieces) plus an offset of less than one piece, so in each run every keying edge sits
at the same position within its piece, and a position's result holds for every element of its run.

**The tables** (one row per run; levels to 0.1 dB S₅₀₀, start times to 0.1 ms; `exact` is 1 when the text is the one
sent):

| Table | Rows | Columns |
|---|---|---|
| `fall-edge-positions.csv` | 80 | `s500_db` (dB S₅₀₀); `edge_input_samples_of_96`, the key-down's offset after 0.5 s in input samples of 1/48 000 s, a piece being 96; `exact`; `decoded` |
| `fall-levels.csv` | 48 | `s500_db`; `decoder_fading_spread_hz` (Hz, the decoder's setting); `start_s` (s, the key-down's start); `seed` (the noise's); `exact`; `decoded` |
| `one-word-over.csv` | 24 | `s500_db`; `seed`; `factor_count`; `exact`; `decoded` |

#### Finding 1: strong stations decode wrongly above about +37 dB S₅₀₀

*Why (derived).* The decoder does not model the fall at a mark's end, and a gap piece adds 0 to a story's score whatever
it holds. A piece of the fall's tail that holds the fraction f of a full piece's carrier sum carries
f² × 10^(S₅₀₀/10) nats of energy (because (3 f a)² / (4 500 Hz × N₀) with a² = 10^(S₅₀₀/10) × N₀ × 500 Hz), which no
story that keeps the mark explains once the mark has ended. A story that ends the over after the mark and starts a new
over of one piece in the tail can claim it, and that over alone costs 34.3 nats: the over's end 4.30, the birth's
ln P 9.21, the letter 4.13, the speed's prior 3.47 and a one-piece dot at 100 WPM 13.17 (34.27 nats). The story also
pays for what follows, so the claim wins only where the piece carries at least about 34 nats, which is f = 0.093, 0.074
and 0.059 at +36, +38 and +40 dB S₅₀₀. The plan's design had judged the fall harmless; that holds for the story that
keeps the mark, not for the one that does not.

*What the runs showed (measured).*

| Level (dB S₅₀₀) | 16 edge positions in a piece, one seed (`fall-edge-positions.csv`) | edge at a piece's start, three seeds (`fall-levels.csv`) | same, decoder's fading spread 5 Hz |
|---|---|---|---|
| +30, +33, +36 | exact at all 16 | | |
| +38 | 2 wrong (edges 18 and 24 of the piece's 96 input samples) | | |
| +40 | 3 wrong (edges 12, 18 and 24) | exact on 3 of 3 | |
| +41 | | exact on 3 of 3 | |
| +42 | | 1 wrong (seed 34), 2 exact | |
| +43, +44, +45 | | wrong on 3 of 3 | +45: wrong on 3 of 3 |
| +50, +55, +60 | | | wrong on 3 of 3 |

Other runs in `fall-levels.csv` (1 Hz spread, three seeds each): the key-down starting 0.4 ms after a piece's start
(0.5004 s, about the 19th of 96 samples) is wrong on 3 of 3 seeds at +40 dB S₅₀₀, the starts 0.8 and 1.2 ms after
(0.5008 s and 0.5012 s) are exact on 3 of 3 at +40 dB S₅₀₀, and all three starts are wrong on 3 of 3 seeds at +60 dB
S₅₀₀. So near the onset the edge's position within its piece decides (at +40 dB S₅₀₀ the edges at 12 to 24 samples
fail and those at 0 to 6 and 30 to 90 do not), and at +60 dB S₅₀₀ all four positions tried (the piece's start, with
the 5 Hz spread, and the three later starts) fail.

*What the texts look like (measured).* Near the onset the texts keep parts of the words: "PARIS ET EGA E RIS" (+38 dB
S₅₀₀, edge 18), "PARIS E EGARIS" (+42 dB S₅₀₀, seed 34, edge at the piece's start) and "PARIS PARIS E" (+43 dB S₅₀₀,
seed 41). From +44 dB S₅₀₀ they break into short words, mostly of E and T. At +60 dB S₅₀₀ the text is
"T EE EE ET E T EE E T EE ET E T ET E T ET ET E" for each "PARIS": 19 words and 28 letters for its 14 elements (38
words and 56 letters for the two; counted from the table). The committed test `ViterbiDecoder.ExtremeLevels` prints
the same text at the default settings (a strict expected failure: it fails the day the text is exact). A 5 Hz fading
spread, which makes the level's prior 4.4 times broader across a dot's gap, fails alike from +45 to +60 dB S₅₀₀, so the
level's prior is not the lever (the first explanation tried, refuted).

*What is not shown (conjectured).* A real station's edges fall at mixed positions within their pieces, so from about
+37 dB S₅₀₀ some of its elements would be misread (conjectured: no run mixed the positions). DEV2 does not reach this
range, so its measurements are unaffected; real bands hold stronger stations.

#### Finding 2: an over of one word can read as one longer symbol

*What the runs showed (measured, `one-word-over.csv`).* "TEST" alone (a single word, one over) at +10, +15, +20 and +30
dB S₅₀₀, noise seeds 39, 40 and 41, decodes as the symbol "-" (the code −····−) in 12 of 12 runs with the default 23
Farnsworth factors, and as "TEST" in 12 of 12 runs with a single factor (`factor_count` = 1), at every level and seed.
The level did not matter in this range.

*Why (derived).* The mark sequence of "TEST" (a dash, four dots and a dash: six marks) is that of the symbol "-", if
the three gaps between its letters are read as gaps between elements, at a slower speed. At exact timing the four-letter
reading pays 16.70 nats more in priors than the one-symbol reading: three more letters (3 × 4.127), letter gaps instead
of element gaps (3 × 0.396) and the factor's prior (3.135 nats, paid at a story's first letter or word gap, never paid
by the one-symbol reading). The one-symbol reading pays the misfit of its timing at a speed k times slower,
[8 (ln k)² + 3 (ln(3/k))²] / 0.18 nats (six marks and two element gaps at 1/k of their medians, three letter gaps
at 3/k, σ = 0.3 in ln(length)): 14.64 nats at the speed grid's best k = 1.1³ = 1.331. So "-" wins by 2.06 nats; with a
single factor the prior is 3.135 nats lighter and "TEST" wins by 1.07 nats. These are the document's figures,
recomputed here from the formulas.

*When it happens (measured and derived).* It needs an over of one word: where a word gap follows, both readings pay
the factor's prior and the effect is gone ("TEST W9XYZ" decodes exactly in the committed test
`ViterbiDecoder.DecodesThroughTheEngine`). It depends on the word: "PARIS" alone decodes exactly (committed test
`ViterbiDecoder.FlushesOnceAndResetStartsAfresh`), while "TU" alone would read as "X" (−··−), which saves 7.66 nats
against a misfit of 5.79 nats at k = 1.1² (derived in the document; not run).

*What is not shown (conjectured).* Whether DEV2's QSO overs of one or two short words are read this way (the document
gives "TU" as "X" and "EE" as "I" as plausible) has not been run.

*Both findings:* the owner's decisions are pending: whether to model the fall at a mark's end, and how to treat the
text model's and the open factor's bias toward fewer, longer symbols in short overs.
