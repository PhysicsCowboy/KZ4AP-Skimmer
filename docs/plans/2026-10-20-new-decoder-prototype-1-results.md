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
  that has lasted some pieces, at a speed, a node of the Morse tree, with its own signal level and its own text (the
  spacing is standard: the decoder has no Farnsworth factor, the owner's decision D14 of 2026-10-21). Its score, in
  nats, is the log-likelihood of the story and the signal so far against "noise only, throughout".
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
values. Pieces, bins and samples appear only where a value is used. The bench records all 33 fields in every viterbi
result file (section 1.2).

| Field | Default | Unit | Class and meaning |
|---|---|---|---|
| `piece_s` | 0.002 | s | derived: 3 channel samples at 1 500 samples/s; it must be a whole number of channel samples |
| `dot_min_ms` | 12.0 | ms | owner: the dot of the fastest speed, 100 WPM |
| `speed_ratio` | 1.1 | none | owner: each speed's dot is this times the previous speed's |
| `speed_count` | 32 | count | owner: speeds 0 … 31, dots of 12.0 to 230.3 ms, 100 to 5.2 WPM |
| `fist_medians_dots` | 1, 3, 1, 3, 7 | dots | placeholder, heuristic: the median length of a dot, dash, element gap, letter gap and word gap, at standard spacing (no Farnsworth factor: owner, D14); the ITU sending standard's ratios, not a measurement of senders |
| `fist_sigmas` | 0.3 for each | standard deviation of ln(length) | placeholder, heuristic |
| `fist_shifts_dots` | 0 for each | dots | a sender's weighting added to each kind's length; 0 in the baseline, set only by the genie fist |
| `beam_nats` | 40.0 | nats | placeholder: at least 28 nats (derived) |
| `ceiling` | 1 700 | stories | placeholder; left to the ceiling run, R3 (owner, D11) |
| `fading_spread_hz` | 1.0 | Hz | placeholder, swept by the plan: the Doppler spread f_D of the fading, twice the standard deviation of a Gaussian Doppler spectrum |
| `block_loss_db` | 0.5 | dB of signal energy | heuristic: the largest loss of a coherent block; with `fading_spread_hz` it gives the block, B = 195 pieces = 390 ms (derived) |
| `keying_rise_s` | 0.005 | s | placeholder, heuristic: the raised-cosine rise at a mark's start, the generator's edge (S2's edges are 10 ms: section 1.4) |
| `systematic_floor` | 0.02 | of a full piece's carrier amplitude | placeholder, heuristic (the owner's decision D15 on the systematic term; section 1.7): the floor of the model's systematic uncertainty per piece, added in quadrature to the noise in the error budget |
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

The search also pays priors that are not configuration: a speed 1/32 (3.466 nats) where a newborn's speed is taken,
and a letter 1/62 (4.127 nats) at its start, both owner (flat, no text model).

### 1.2 The bench command line

    kz4ap-bench RECORDING.wav --labels LABELS.json --json RESULT.json --oracle --decoder viterbi
                [--set NAME=VALUE ...] [--genie fist|frequency|fading ...] [--stats STATS.json]

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
    median length, `piece_s` / (median × dot): an exact sender's timing taken as known to one piece, 2 ms. It binds
    for the machine style and, at a fast speed, for the small σ of the others. The decoder has no Farnsworth factor
    and the bench does not read the labels' `farnsworth_wpm`, so for a Farnsworth-spaced signal the genie fist gives
    the style's standard medians. **Class of the floor: heuristic** (neither derived nor measured); R8 on the machine
    style, and on the other styles at fast speeds, depends on it.
  - **Limitation of the genie, accepted for now:** a label with no single truth to give is refused, by its number in
    the labels file: a QSO label of several stations (each has its own carrier and fist; the per-station labels of a
    QSO have one) for `frequency` and `fist`, and a signal that changes speed (`wpm_end`) for `fist`. `ViterbiGenie`
    holds one frequency and one drift, so the tracker's measurement run on the QSOs on one track (G2) cannot yet
    compare the tracker with the true frequency there: that run needs a piecewise genie, a frequency for each over,
    and is decided when the run comes.
- `--stats FILE` (with `--oracle`) turns the search's statistics on (`stats` in the configuration, recorded as true)
  and writes them when the run ends, after the result file, for each oracle channel in the order of the labels, as
  JSON. The format ("kz4ap-viterbi-search-stats", with its units and codes) is documented in
  `bench/src/viterbi_json.hpp`, in two versions (Task 8b changed what `--stats` writes: Task 7b's file is now version 1):
  - **Version 2, the default: a summary for each channel.** The pieces searched; the live stories (a histogram of the
    pieces by live count, their mean over the pieces and their maximum); how often the ceiling decided; the counts of
    the newborn lines by kind (start, silence, rescue), end (alive, merge, beam, ceiling), whether they ended within
    their first element and whether they led; and the whole record of each line that ever led. That is what the
    reductions of R0 to R4 use (`training/kz4ap_proto/search_stats.py` defines them over the full file, and the bench's
    summary is their output). The lines that led are kept because R1 needs each newborn's deficit before it led (its
    maximum and upper quantiles, which no counter gives); they are few (1 or 2 for a channel of the small test file,
    2.6 and 5.0 on average, at most 18, for the channels of two real A2 recordings, the first story among them); and a
    line that never led never had the best story at any piece. The 0.1 s series of the live count is not kept. Size
    (measured, DEV2's group A2, seed 1, made by `suites generate --suite dev2 --per-cell A2=1 --groups A2`; recording
    A2-awgn-c10-0-s1, 14 channels, 307.9 channel-seconds, the defaults): 19 835 bytes, a header of 3.7 kB and, for a
    channel, 1.4 kB on average (at most 2.1 kB), where version 1 took 14.0 MB for the same run: 707 times less. A
    version 2 file is the header and about 1.4 kB for each channel whatever the channel's length, so one run over all of
    DEV2 (14 204 channels in 498 oracle recordings) takes about 22 MB at the default ceiling (derived: 14 204 x 1.43 kB
    and 498 x 3.7 kB), against 48 GB; a higher ceiling lengthens the live-count histogram, one bin for every 10
    stories (ceiling 17 000: up to 1 701 bins, a few kilobytes more for a channel).
  - **Version 1, with `--stats-detail`: every newborn line**, as columns, the full file of Task 7b. The search creates
    a silence line and a rescue line at every piece, so it holds two lines per piece whatever the signal, about 45
    bytes each: 45.5 kB per channel-second (measured: the recording above, 14.0 MB; a scratch run of two stations over
    16 s gave 1.45 MB, 45.4 kB per channel-second), so about 400 MB for an N2 recording of 25 channels x 360 s. For a
    small run (R0, diagnosis).

  The reductions give the same numbers from either version: on the recording above, all 14 channels' summaries equal
  the Python reductions of the full file of the same run (every member, the mean of the live stories to the last
  digit), and the repository's tests do the same on a small real file of each version. Two runs wrote identical files
  in both versions. The statistics file comes after the result file, which is written under a name of its own and
  renamed when complete, so a statistics file that cannot be finished loses nothing of the run; both files'
  destinations are tried before the run. The suite runner's `--stats` hands the bench `results/NAME/<test case>.stats.json`
  for every test case it runs (`--stats-detail`: the full form), so a run with `--stats` or `--genie` takes only the
  oracle recordings (`--only`; the runner refuses a selection with others before it starts). Writing the file comes
  after the timed window and does not count in the run's CPU time; the search's own bookkeeping is inside the
  window (R0 measures what it costs), and so is the engine's memory for the lines (96 bytes each, 0.9 GB for an N2
  recording), which the summary does not reduce (the bench reduces channel by channel as it writes, with no copy).
- `--set`, `--genie` and `--stats` belong to `--decoder viterbi`; `--genie` and `--stats` without `--oracle` are
  errors.
- The result file (`--json`) of a viterbi run gains `config` (every field of `ViterbiConfig` as the run used it, so
  the run can be repeated from the file), `settings` (the `--set` arguments as given) and, with `--genie`, `genie` (the
  kinds and each channel's values). Its other members are those of the other decoders' result files. The decoder
  prints its best story's text when the recording ends, as the text of each track, with no corrections.
- The suite runner calls the bench once per test case, ten at a time on the Linux machine: `python -m
  kz4ap_synth.suites run --out DIR --bench PATH --decoder viterbi --jobs 10 --name NAME [--set NAME=VALUE ...] [--genie
  KIND ...] [--stats [--stats-detail]] [--only REGEX] [--overwrite | --skip-existing]`. The results go to
  `DIR/results/NAME/`, and a run gets a new name every time: a folder that already holds result files is refused
  unless `--overwrite` (replace the test cases this run does) or `--skip-existing` (resume the run: skip the test cases
  that are done, if they were made with the same decoder, settings, genie and statistics form) is given. The bench
  writes each file under a name of its own and renames it when it is complete, so a file that is there is whole, and
  the runner removes a test case's stale statistics file and partial files before it launches the bench.
  `kz4ap_proto.experiments` reduces a run: `search-errors` (the oracle test cases, `experiments.DEV2`, by default, which
  is what the plan's runs pass as `--only` to the runner too; the per-signal counts also as a rounded CSV),
  `search-stats` (the statistics per group), `false-characters` (the characters decoded on N2 per channel-hour) and
  `devset2 --leave-out-group "I2 Farnsworth"` (the new decoder has no Farnsworth spacing, D14, so all of I2 is reported
  apart from the comparison with the bank).

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

  **A condition on S2: its keying edges are 10 ms, and `keying_rise_s` is 5 ms by default.** S2's envelope is
  exactly A2's in a time base 2 times slower, edges included. The decoder's matched filter to a mark's rise takes the
  edge from `keying_rise_s`, in seconds and not in dots (the design takes the edge as a property of the transmitter,
  not of the sending speed), so at the default it assumes an edge half as long as S2's. A run on S2 that is to be a pure time-base test against A2 sets
  `--set keying_rise_s=0.010`; a run at the default measures the decoder with an edge it does not expect.
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
- I2 is reported apart from the comparison with the bank, all of it, as a known gap of this prototype: the decoder has
  no Farnsworth factor (the owner's decision D14 of 2026-10-21), so Farnsworth-spaced signals are expected to decode
  poorly. The earlier cut at a Farnsworth factor of 8 (181 of I2's 864 signals: 20.9%, counted from the table's
  `farnsworth_factor`) is moot.
- The new decoder's own reference is the wide-beam run (R1: beam 80 nats, ceiling 17 000), against which its search
  errors are counted.

### 1.7 Evidence for findings made while building

Task 7a found two behaviors of the decoder while building it. Both stand in `docs/signal-processing.md` (section 8d,
"Open", and appendix A.8d, limitations) and in `engine/include/kz4ap/viterbi/search.hpp` ("what remains", item 4).
The owner decided both on 2026-10-21 (the end of this section); the fix of finding 1, the systematic term, has its own
part below. The measurements were scratch runs through the decoder (temporary tests, removed before the commit, not
tests); this section states what they showed, with the conditions known, and the outputs of the runs are committed as
tables in `docs/plans/data/2026-10-20-new-decoder-prototype-1/`, the console's wrapped lines joined. Each claim carries
its class.

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
| `systematic-strong-grid.csv` | 224 | `systematic_term` (0 without the term, at ae8f65d; 1 with it); `s500_db`; `edge_input_samples_of_96`; `exact`; `decoded` |
| `systematic-neighbors.csv` | 777 | `grid` (1, 2, or `E-qrm-s3 label 9`, the existing tests' stream); `station_s500_db` and `neighbor_relative_db` (dB S₅₀₀, and dB relative to the station's key-down power, to 0.01 dB); `neighbor_offset_hz` (Hz above the station; 0.01 Hz); `seed`; `systematic_term` (`without`, `floor_0`, `with`); `edits`; `reference_symbols`; `exact` |

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
words and 56 letters for the two; counted from the table). The committed test `ViterbiDecoder.ExtremeLevels` printed
the same text at the default settings (a strict expected failure until the systematic term, below). A 5 Hz fading
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

*Both findings: the owner's decisions (2026-10-21; the plan's D13 to D15).*
- **Finding 2.** The Farnsworth factor was removed from the decoder (D14). Then "TEST" and "TU" sent alone decode
  exactly in 12 of 12 runs each (committed test `ViterbiDecoder.OneWordOversDecodeExactly`; measured).
  Farnsworth-spaced signals now decode poorly: a known gap of this prototype, recorded in `docs/backlog.md`.
- **Finding 1.** Modeling the fall at a mark's end (D13) did not fix it (§1.8). The owner then chose a systematic term
  in the error budget (D15), which fixes it (below).

#### The systematic term (D15): the fix of finding 1

*The design* (`docs/signal-processing.md`, section 8d and appendix A.8d; `search.hpp`, "The systematic term"). The model
is close to the signal, not exact, and each block of a mark is now judged against an error budget that adds the model's
systematic uncertainty to the noise in quadrature: V = N₀ × 1 500 Hz × m_signal + C²(E_n + f²W), W the sum of the
block's squared weights. E_n is the template's error over where the keying edge falls in its piece (a mark's first
block only); f = `systematic_floor` = 0.02 is a floor for everything else; C² is a full piece's squared carrier sum,
taken from the data and the same for every story: the channel's largest piece over the last 390 ms and the next 4 ms,
or the block's own estimate where larger. The search runs 4 ms behind its pieces for that look-ahead.

*What the runs showed (measured, `systematic-strong-grid.csv`).* Finding 1's grid through the decoder at its defaults
(the tracker re-centers): "PARIS PARIS" at 25 WPM on DEV2's noise (seed 34), N₀ the noise's own, the station on its
channel's center, the key-down at 0.5 s plus q input samples, q = 0, 6, …, 90, so that every keying edge of a run sits
at one of 16 positions within its piece. Exact texts, of 16:

| Code | +30 | +36 | +40 | +45 | +50 | +55 | +60 dB S₅₀₀ |
|---|---|---|---|---|---|---|---|
| without the term (ae8f65d), `systematic_term` 0 | 16 | 16 | 13 | 10 | 2 | 0 | 0 |
| with it (the commit that adds this part), `systematic_term` 1 | 16 | 16 | 16 | 16 | 16 | 16 | 16 |

The new full-sweep test `ViterbiDecoder.StrongStationsAtEveryEdgePosition` holds the second row, and
`ViterbiDecoder.ExtremeLevels` now requires the exact text at +60 dB S₅₀₀ (the edge at a piece's start); it was a
strict expected failure.

*Ordinary levels (tested, and measured in scratch runs).* `Search.FollowsASpeedStepAtEverySnr` (the step from 20 to
30 WPM and the constant-speed stations at 12, 25 and 40 WPM, every level from +10 to +28 dB S₅₀₀, three seeds each, at
the 40-nat beam) passes at the commit. In scratch runs in a copy of the branch at 20f0e4f with the term (the same
arithmetic as the commit; not committed), the speed step at an 80-nat beam and "TEST" and "TU" sent alone at +10, +15,
+20 and +30 dB S₅₀₀ on three seeds stayed exact too. What the term costs a mark's evidence at these levels, as the drop
in S₅₀₀, in dB of S₅₀₀, that would give the same mean evidence without it (scratch: the copy's scoring path, a learned
story's level known to 2%, the carrier at the template's fractions, 2 000 noise draws per point):

| Block | at +10 dB S₅₀₀ | at +20 dB S₅₀₀ | at +28 dB S₅₀₀ |
|---|---|---|---|
| first block, 24 pieces (a dot at 25 WPM) | 0.12 | 0.70 | 2.88 |
| first block, 72 pieces (a dash) | 0.07 | 0.40 | 1.77 |
| later block, 195 pieces | 0.05 | 0.24 | 1.12 |

The later block's figure is 10 log₁₀(1 + f²·10^(S₅₀₀/10)) = 0.02, 0.17 and 0.98 dB of S₅₀₀ with C² at the true carrier
(derived); the measured values are higher because the channel's largest piece is a maximum over noisy pieces.

*A strong neighbor (measured, `systematic-neighbors.csv`).* C² is the channel's, so a neighbor inside the channel's
passband raises a weak station's term. Derived (A.8d): where the term's share of a block's variance reaches the noise,
the neighbor's own residual in the same block sum is already, on average, at least 5 times larger. Two grids through
the decoder with the genie's frequency (so the tracker plays no part): "PARIS PARIS" at 25 WPM from 0.5 s, and above
it the neighbor "CQ TEST DE KZ4AP KZ4AP" at 22 WPM from 0.3 s, both machine keying, on DEV2's noise, seeds 1, 2 and 3:
(1) the station at +6 and +10 dB S₅₀₀, the neighbor 0, 10, 20, 30 and 40 dB stronger (relative to the station's
key-down power) and 40, 70, 100, 150 and 200 Hz away; (2) cells like the development set's E2 group, the station at
+8, +11 and +14 dB S₅₀₀, the neighbor −6, 0, +6 and +12 dB relative, 12, 40 and 90 Hz away. Each run three ways:
without the term (the search of 20f0e4f, which ae8f65d's equals), with the term at a floor of 0 (the edge's share
alone), and with the term. Exact texts:

| Grid | Runs | Without the term | Floor 0 | With the term |
|---|---|---|---|---|
| (1) | 150 | 23 | 27 | 27 |
| (2) | 108 | 6 | 6 | 6 |

No run exact without the term is wrong with it, at either floor; four more are exact with it (the station at
+6 dB S₅₀₀ with the neighbor 10 dB stronger 150 Hz away on one seed, and 20 dB stronger 200 Hz away on all three).
Where the term's share would matter, a neighbor 20 to 40 dB stronger within 100 Hz, the station is lost without the
term too (54 of 54 runs; CER 74.8 to 176.5 without it, 18.3 to 26.5 with it, the false text shorter). Without the term
the decoder also loses the station to an equally strong neighbor 40 or 70 Hz away and to one 10 dB stronger within
100 Hz: its block sums keep much of a carrier inside the channel's passband (a limitation of the decoder, A.8d). The
development set's QRM case in the existing tests, `overlap_stream.c64` (10 s of E-qrm-s3, label 9: a station at
0 dB S₅₀₀ and one at +10 dB S₅₀₀ 50 Hz below it), decoded with the genie's frequency, is lost all three ways: 146, 143
and 143 edits for the 23 symbols of "CQ CQ DE N7RL N7RL K CQ" (the rows `E-qrm-s3 label 9`; with the tracker the texts
are the same).

*Cost (measured).* The search alone on the channelizer's pieces, statistics off, the least of three rounds in scratch
runs (other processes used 0.61 to 0.83 cores of 8): 335 → 337 ns per story per piece on 60 s of noise, 423 → 425 at
+20 dB S₅₀₀ and 456 → 459 at +10 dB S₅₀₀, within the rounds' own spread (up to 2.2%). The decoder at the commit, in
`ViterbiDecoder.TenMinutesOfNoise`'s full form (600 s of DEV2's noise at the defaults, the development desktop): 287.3,
288.2 and 291.5 ms of one thread per channel-second in three runs, so 287 ms at the least, as without the term (287 ms,
the least of three, 2026-10-21; the machine's other load not measured); at most 82 entries of shared text at any block,
against 106 without it.

### 1.8 Alternatives tried while building, and not kept

Each was measured, compared with what was kept, and set aside. The figures are from the builders' scratch runs, not from
committed tests.

- **The mark's fall** (D13), the brief's form: a mark's last pieces weighted by the fall's expected fractions when the
  mark ends.
  - "PARIS PARIS" at 25 WPM over 16 edge positions decoded exactly at 16, 16, 15, 9 and 0 positions at +30, +40, +45,
    +50 and +60 dB S₅₀₀, against 16, 13, 11, 3 and 0 without it.
  - But the speed step newly failed at +21, +22 and +25 to +28 dB S₅₀₀ (up to 3 seeds at a 40-nat beam, character
    error rate up to 0.343).
  - A variant that also protected the mark's continuation through the fall removed most of those failures. It still
    failed the strong stations at +50 and +60 dB S₅₀₀, and cost 79% to 212% more CPU.
  - Why, traced: while a mark lasts its block counts the fall's pieces as full, so the correct reading falls behind the
    one that ends a piece early. And any one edge position differs from the template, averaged over positions, by up to
    0.087 of a full piece's energy, which at high levels outweighs the cost of a wrong reading.
  - The fall's fractions turned out to be the rise's complement, F(p) = 1 − R(p), not the rise reversed.
- **The tracker, confirming only from windows that share no samples** (1 s apart): relocking fell to about 92% and the
  strong-neighbor case to 38–60%, against 100% and 73% without it (1 000 seeds each).
- **The tracker, a keyed-carrier factor of 1.6 on each measurement's variance:** once the first window's fill factor
  was in, the keyed scatter was 0.91–0.98 of the variance the model gave, so the factor had no basis left.
- **The tracker's trust tolerance following the block length** (0.26 / T_block, a ruling made while building):
  - at a 0.3 Hz fading spread (1.3 s blocks), 96.5% of runs held a keyed carrier at 0 dB S₅₀₀ within 0.667 Hz, against
    100% at 1 Hz;
  - the strong neighbor captured the track in 71 of 200 runs, against 4 of 200;
  - below about a 0.57 Hz spread a keyed station at 0 dB S₅₀₀ was never confirmed.
  - The tolerance is fixed at 0.667 Hz at every spread.
- **The systematic term's other forms** (D15; scratch runs on finding 1's grid in a copy of the branch at 20f0e4f, with
  switches for each form; exact texts of 16 at +30, +36, +40, +45, +50, +55 and +60 dB S₅₀₀):
  - **The carrier power from a story's own level** (the predicted level of a learned story, the block's own estimate
    for a newborn's first block): 0 at every level, texts like "AETA ET ATTT ETETAE …" at +30 dB S₅₀₀. Derived: a story
    that lowers its own level lowers its own V and gains (A.8d). **The exact form, V(a) inside the level's integral,**
    is dominated by the same small a and grows as z₀²/√ρ instead of being capped (derived; not built).
  - **The block's own estimate alone, for every story:** 0 at every level. It over- or understates the carrier as the
    template sits on the rise (traced with it or the block's strongest piece as C², which gave 16, 1, 0, 0, 0, 0 and 0:
    with the edge at 24 of 96 input samples, a template one piece early led the aligned one by about 45 nats after two
    pieces at +36 dB S₅₀₀).
  - **The channel's largest piece alone:** 16, 16, 7, 2, 1, 0 and 0; at an over's first mark the channel holds no
    recent carrier, so the claim's budget uses that piece's own small power ("E PARIS PARIS" from +40 dB S₅₀₀).
  - **The edge's error over independent pieces** (the mean squares summed): 3 of 16 at +60 dB S₅₀₀, against 14 with
    the error summed as one edge's, both without the look-ahead.
  - **The edge's term in a newborn's first block only** (with the block's own estimate or its strongest piece as C²):
    0, 0, 0, 0, 2, 1 and 0; a story whose level was learned on noise (54 times too low) took a strong mark, and every
    mark then read as T at three times the speed.
  - **No look-ahead:** 16 everywhere but +60 dB S₅₀₀, 14 (the edge at 48 and 54 of 96 input samples, "E PARIS PARIS":
    the filter's ringing before the mark read as the end of a weak mark, about 87 nats). With 2 or 3 pieces: 16.
  - **Other floors** (without the look-ahead): 0 gave 16, 16, 16, 0, 0, 3 and 5 (two marks merged across a gap: "T
    NARIS PARIS" at +45 dB S₅₀₀), and at ordinary levels lost the speed step at +28 dB S₅₀₀ (beam 40, seed 1:
    "PARIS PARIS PARIS PA<AS>S PARIS PARIS", CER 0.114) and "TEST" at +30 dB S₅₀₀ (seed 41: "-"); 0.01 gave 14 at
    +60 dB S₅₀₀ and lost "TEST" and "TU" at +30 dB S₅₀₀ (seed 40: "-" and "X"); 0.05 gave 15 at +60 dB S₅₀₀ and was
    clean at ordinary levels, but costs as much evidence as 1.3 to 1.6 dB of S₅₀₀ at +20 dB S₅₀₀, against 0.24 to
    0.70 dB of S₅₀₀ at 0.02.
