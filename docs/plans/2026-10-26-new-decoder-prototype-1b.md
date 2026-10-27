# New decoder: the amended build (prototype 1b) Implementation Plan

> **Status: approved by the owner, 2026-10-26 ("Approved"), after two reviews and four fix rounds; its questions
> decided by him (D19–D22, §3.3).**
> The owner does not read this plan: it is the executors' document, and every decision reaches him in chat, in words.
> His answers are recorded here by the coordinator.
>
> **For agentic workers:** REQUIRED SUB-SKILL: superpowers:subagent-driven-development (the owner's choice for the
> building tasks of the first prototype, plan D9, carried over). Steps use checkbox (`- [ ]`) syntax for tracking.
>
> **Spec:** `docs/design/2026-09-25-kz4ap-skimmer-design.md` §5.1 and §5.2 steps 1 and 3 (amendments of 2026-10-25).
> **Design:** `docs/design/2026-10-09-new-decoder-design-notes.md`, the section "Detection, amplitude, frequency and
> the mark's shape", Parts 1 to 17 (cited here as "Part n"), and topics 1 to 5 as amended there. **Decisions:** the
> first prototype's plan, `docs/plans/2026-10-20-new-decoder-prototype-1.md` ("the prototype plan"), §3: **D15 to
> D18 are the authority** where this plan and the design notes differ; its conventions (§4 to §9) are reused where
> they still apply. **Rulings** 60 to 66 of the coordinator (the prototype plan's git-ignored ledger,
> `.superpowers/sdd/2026-10-20-new-decoder-prototype-1/progress.md`) are applied as stated in §3.1.
> **Evidence for this plan** (git-ignored workspace, same folder): `code-map-2026-10-25.md` (the built code against
> the amended design, file by file; "the code map") and `timing-slice-2026-10-25.md` (the measured CPU by part; "the
> timing slice").
>
> **Branch:** `new-decoder-1b`, from `new-decoder-1` at this plan's commit (§3.2, O1).

**Goal:** build the amended new decoder of Parts 1 to 17 in C++ in the engine, verify it, and measure its decoding
errors first (D18) on the regenerated development set, against today's decoder and the bank decoder, with the CPU
breakdown by part kept in every measurement.

**Architecture:** the channelizer follows the signal detector's frequency for each new-decoder channel (a coarse FFT
bin, then a fine phase-continuous shift), so the decoder receives its station near 0 Hz and has no frequency tracker.
The decoder passes the channel through one fixed 25-tap front filter (76.0 Hz noise bandwidth, matched to a 12 ms dot
with 2.5 ms edges) and takes its envelope every 4th channel sample (2.667 ms). The explicit-duration Viterbi beam
search of the first prototype is kept as a skeleton; each story now predicts the envelope from its claimed edges (the
smeared template), scores every sample by the tempered Rician likelihood integrated over its Gaussian amplitude, and
tracks that amplitude toward its own long-run power Ω̄; a newborn's first mark is scored against the owner's flat
prior until its belief narrows.

**Tech stack:** C++20 (MSVC 14.5x on the desktop, g++ 11.4 on the Linux machine), CMake presets, GoogleTest. Python
3.12 only for the test-signal generator (`training/kz4ap_synth`), the suite runner and the analysis
(`training/kz4ap_proto`), with their tests (CLAUDE.md, "Languages").

---

## 0. Terms

The prototype plan's §0 terms stand (story, score, beam, ceiling, newborn, over, search error, genie run, variant,
full-run equivalent, channel-second), except "piece" and "block", which this build removes. New or changed:

- **Sample** (Δ): the decoder's step, one envelope sample every 4th channel sample, Δ = 4 / (1 500 samples/s) =
  2.667 ms, 375 samples/s (owner, 2026-10-23; D17). Sample k is the channel sample of absolute index 4k (counted
  from the stream's start), at stream time t_k = 4k / r, r = 1 500 samples/s; the same numbering for oracle channels
  and for detector channels born mid-stream (Ruling 72, N2).
- **e_k:** the envelope |y| of the front filter's output at sample k, in FS.
- **Template s_k, edge term v_k:** a story's predicted front-filter output at sample k for its claimed keying, as a
  fraction of its key-down amplitude (1), and the variance of that prediction over the edge's position and the
  transmitter's rise time (1) (Part 5).
- **Claimed edge:** the sample a story names as a key-down or key-up: the first sample at or after the keyed edge
  (owner, 2026-10-25; Part 5). j counts samples from it, j = 0 at the edge's sample.
- **Edge history:** the claimed edges a story still feels: those within the last 16 samples (42.67 ms), beyond which
  the template is exactly 1 (key-down) or 0 (key-up) (Part 5).
- **Story-sample:** one story at one sample, the unit of the per-story CPU figures. **Scored sample:** a story-sample
  with s_k ≠ 0.
- **The newborn integral:** a newborn's score against the flat prior before it switches to N(μ, P) (Part 8).
- **q_k:** a sample's expected signal-to-noise power ratio, (μ_k|k−1)²·s_k²/σ_y² (1; quoted as dB SNR in the
  filter's 74.48 Hz).
- **σ_y²:** the front filter's output noise power on the channel's colored noise, N₀ × 74.48 Hz (FS²) (Part 2).
- **DEV2e, N2e:** DEV2 and N2 regenerated from the same seeds with D17's edges (Part 14). Old and new scores are not
  compared (Part 14). DEV2e has DEV2's 498 recordings, 14 204 oracle channels and 1 065 742.1 channel-seconds (the
  same seeds, texts, timings and durations).
- **Today's decoder:** the new decoder as built by the prototype plan's Tasks 1 to 8b, at commit `56bae92` (its code
  unchanged since `2160978`): 2 ms pieces, coherent blocks, its own frequency tracker. Part 15's paired comparisons
  are against it.
- **`d17-bank`:** the bank decoder's reference run on DEV2e (Part 14; prototype plan O13).

## 1. What this build delivers, and what it leaves out

It delivers:

1. **The amended decoder** behind `--decoder viterbi` (not the default), replacing the first prototype's scoring
   core in place on `new-decoder-1b`: Parts 1 to 11, with Part 12's removals.
2. **The channelizer's following** of the detector's frequency for new-decoder channels, and oracle channels that
   follow the label's true (drifting, per-over) frequency (Part 1; Part 14).
3. **The test generator's D17 edges** in Python, and a new-decoder-only C++ test-signal helper with the same edges
   (Part 14; Ruling 63).
4. **Table generators** as a C++ tool in the repository (Ruling 65): the weight table and the default Ω̄ table
   compiled in; the variant's 12.8 MB table generated into `build/` and loaded only when the variant runs.
5. **Instrumentation:** the CPU breakdown by part in every measurement (Part 15's preamble; D18), Part 15's counters,
   and a true-story trace (the model run along the label's keying) for the measurements that need it.
6. **The measured variants behind switches,** off by default: the fixed w (Part 15, item 18), Bell's decay (item 16),
   the noise in Ω̄'s likelihood (item 17), the previous element's length in the merge state (item 13).
7. **The measurements:** the prototype plan's runs revised (§7.1), Part 15's 18 items (§7.2), with the regenerated
   DEV2e and N2e, the bank's reference rerun, and the results record
   `docs/plans/2026-10-26-new-decoder-prototype-1b-results.md` with its committed score tables in
   `docs/plans/data/2026-10-26-new-decoder-prototype-1b/`.
8. **`docs/signal-processing.md`** rewritten for the amended decoder (§8, rule).

It leaves out what the prototype plan's §1 leaves out (text model, non-Morse states, interference model, channel
opening and closing, output timing, the overload governor), and: the detector path's frequency following and its
open questions (backlog, "The detector's frequency following and the fixed channel"); any change to the bank,
Envelope or Matched decoders (§3.2, O2, the coordinator's ruling); the speed work toward the 500-track target (D18: recorded, not yet
constraining); every backlog item of Part 13.

## 2. What is kept, changed, removed and new, by file

From the code map §1 to §3 against Parts 1 to 12. "Task n" is the task of §10 that makes the change. Paths under
`engine/include/kz4ap/viterbi/` and `engine/src/viterbi/` are written `viterbi/…`.

| File | Kept | Changed | Removed | New |
|---|---|---|---|---|
| `engine/include/kz4ap/channelizer.hpp`, `engine/src/channelizer.cpp` | everything | — | — | `Channelizer::set_center_bin(id, bin)`, a mid-stream move effective from the next block (Task 2) |
| `engine/include/kz4ap/channel_follower.hpp`, `engine/src/channel_follower.cpp` | — | — | — | `ChannelFollower`: the coarse bin rule and the fine phase-continuous shift (Task 2) |
| `engine/include/kz4ap/engine.hpp`, `engine/src/engine.cpp` | the hiss wiring, the other decoders' paths | new-decoder channels re-binned and shifted before their decoder (Task 2); oracle channels read `oracle_frequency_tracks` (Task 2); the variant's table path passed to the shared parts (Task 12) | — | `EngineConfig::oracle_frequency_tracks`, `OracleSegment` (Task 2); `EngineConfig::viterbi_omega_bar_table` (Task 12) |
| `viterbi/config.hpp` | the speed grid, fist, beam, ceiling, rates, level range, quadrature order, hiss fields, `genie_fading_spread_hz`, `stats`, `ViterbiGenie::fading_spread_hz` and `::fist` | `piece_s` → `sample_interval_s` = 4/1 500 s; `fading_spread_hz` 1.0 → 2.0 Hz (Task 10); `with_genie` keeps fading and fist only (Task 2) | `tracker_window_s`, `tracker_hop_s`, `tracker_search_hz`, `tracker_gate_db`, `tracker_drift_sd_hz_per_s`, `tracker_drift_noise`, `genie_frequency_offset_hz`, `genie_frequency_drift_hz_per_s`, `ViterbiGenie::frequency_hz`, `frequency_time_s`, `drift_hz_per_s` (Task 2); `block_loss_db`, `keying_rise_s`, `block_length_s()` (Task 10) | the fields of §10, Task 10 (front filter, edges, systematic memory and look-ahead, ν's bins, newborn), Task 11 (`profile`), Task 12 (variant switches) |
| `viterbi/morse_tree.*` | all | — | — | — |
| `viterbi/fist.*` | all (it counts in `config.sample_interval_s`) | reads `sample_interval_s` (Task 10) | — | — |
| `viterbi/observation.*` | `ln_i0`, `find_mode`, `fit_posterior`, the truncated-node table (Rulings 11, 15, 60), `Moments`, `evaluate` | the integrand gains the tempering weight w; the broad-prior regime gains w or is dropped by Task 5's measurement | `sum_noise_hz`, `rise_fractions`, `rise_template_errors`, `detail::rise_fractions_by_position`, `analyze_rise`, `FlatPrior`, `block_llr`, `block_llr_uncertain`, `level_posterior`, `block_evidence`, `BlockEvidence` (Task 10) | `sample_evidence`, `SampleEvidence` (Task 5) |
| `viterbi/level.*` | — | — | the whole file: `Level`, `predict`, `BlockScore`, `score_block`, `score_first_block` (Task 10) | — |
| `viterbi/tracker.*`, `engine/tests/viterbi/tracker_test.cpp` | — | — | all (Task 2) | — |
| `viterbi/search.hpp`, `search.cpp` | states, `pack_state`, `MorseTree` walk, `Fist` use, successors, births, rescue, `OpenTiming`, `StateTable`, merging, `prune_alive`, `SharedText`, `SearchStats`'s lines and live counts, `detail::merge`/`prune` | `Search` takes `ViterbiShared` parts instead of `c` and `FlatPrior`; `step(double e_fs, double t_s, double n0)`; `Story`: `tau` counts samples, `base` is the running sum of booked sample scores; every phase (mark, gap, no signal) scores its samples with s_k ≠ 0; the look-ahead 2 pieces → 10 samples; `carrier_power_` → the 21-sample window; `best_text` times are claimed edge times (Task 10) | `Story::block_sum`, `block_pieces`, `level`; `block_pieces_`, `first_block_pieces_`, `noise_rate_hz_`, `window_` (390 ms), `kLookaheadS`, `scored_` of `BlockScore` (Task 10) | `Story::edges` (`EdgeHistory`), `amp` (`Amplitude`), `mode_a`, `long_run` (`LongRun`), `omega_bar`, the newborn's state handle, the variant's sums (Tasks 10, 12); `CpuBreakdown` and counters in `SearchStats` (Task 11) |
| `viterbi/hiss.*` | all | — | — | — |
| `viterbi/front_filter.*` | — | — | — | `front_filter_taps`, `OutputNoise`, `output_noise`, `EnvelopeSampler` (Task 3) |
| `viterbi/mark_template.*` | — | — | — | `TemplateTables`, `make_template_tables`, `EdgeHistory`, `template_value`, `claimed_edge_sample` (Task 4) |
| `viterbi/weight.hpp`, `engine/src/viterbi/weight_table.cpp` (generated) | — | — | — | `sample_weight(q)` and its 129-node table (Task 5) |
| `viterbi/systematic.*` | — | — | — | `CarrierWindow`, `v_prime_fs2`, `score_cap_nats` (Task 5) |
| `viterbi/amplitude.*` | — | — | — | `Amplitude`, `FadingTables`, `predict_amplitude`, `predict_amplitude_bell` (Task 6) |
| `viterbi/long_run.*`, `engine/src/viterbi/omega_bar_table.cpp` (generated) | — | — | — | `LongRun`, `OmegaBarTable`, `omega_bar_closed_form` (Task 7); `NoiseVarianceSums`, `OmegaBarVariantTable`, `omega_bar_variant_direct` (Task 8) |
| `viterbi/newborn.*` | — | — | — | `NewbornIntegral` (Task 9) |
| `engine/include/kz4ap/viterbi_decoder.hpp`, `engine/src/viterbi_decoder.cpp` | `ViterbiDecoder`'s `Decoder` overrides, `flush`, `reset`, `stats()`, `search()`, `set_noise_density_fs2_per_hz` | `process` feeds the front filter and the envelope sampler, then the search, sample by sample (Task 10); `ViterbiShared` holds the front taps, `OutputNoise`, `TemplateTables`, `FadingTables`, `OmegaBarTable` and, for the variant, `OmegaBarVariantTable` (Tasks 10, 12) | `tracker_`, `anchor_hz_`, `phase_cycles_`, `offset_hz_`, the `tracker()` accessor, the re-centering (Task 2); `piece_sum_`, `viterbi_block_pieces`, `viterbi_first_block_pieces`, `ViterbiShared::c_`, `rise_`, `template_errors_`, `flat_prior_`, `keying_rise_s_` (Task 10) | — |
| `bench/src/viterbi_json.*`, `bench/src/main.cpp` | `--decoder viterbi`, `--set`, `--stats`, `--stats-detail`, `--genie fist|fading`, the result file's config | the field macro follows every config change (Tasks 2, 10, 11, 12); the genie fist's σ floor uses `sample_interval_s` (Task 10); statistics format version 3 (Task 11) | `--genie frequency` and `GenieKind::Frequency` (Task 2) | oracle frequency tracks from the labels (Task 2); `--omega-bar-table FILE` (Task 12); `--trace DIR` (Task 12) |
| `bench/src/viterbi_tables_main.cpp` | — | — | — | `kz4ap-viterbi-tables` with subcommands `weight` (Task 5), `omega-bar` (Task 7), `omega-bar-variant` (Task 8) |
| `engine/tests/test_signals.hpp` | unchanged (Ruling 63) | — | — | — |
| `engine/tests/viterbi/keyed_signals.hpp` | — | — | — | the new decoder's keyed-signal helper with D17 edges (Task 3) |
| `training/tests/data/search-stats/tiny.v1.stats.json`, `tiny.v2.stats.json` | kept as the examples of formats 1 and 2 | their `config` rewritten in Tasks 2 and 10 (Ruling 66; O13) | — | `tiny.v3.stats.json` and `tiny.v3-detail.stats.json` from one run (Task 11; Ruling 71) |
| `training/kz4ap_synth/generate.py`, `suites.py` | the randomness, texts, timings, noise seeds | edges start at key-down and key-up, 2.5 ms 10–90% (Task 1); S2 keeps scaling its edges (Ruling 62); label fields (Tasks 1, 13) | `edges_centered` (replaced by `edge_placement`, Task 1) | the measurement suites of §7.2 (Task 13) |
| `training/kz4ap_proto/*` | the runner, scoring, paired comparison, crossings | statistics version 3; the CPU lines; the trace reductions (Task 13) | the I2 cut (void since D14) | — |
| `docs/signal-processing.md` | other decoders' sections | §7 and A.7 (Task 2); §8d, A.8d, A.10, A.11 (Tasks 2, 10, 11, 12, and runs that change a default) | A.8d's tracker, pieces, blocks, flat-prior table sections | — |

**Unchanged and to stay unchanged:** the bank, Matched and Envelope decoders, their tests and goldens
(`engine/tests/data/bank/*`), `engine/tests/test_signals.hpp`, the hiss estimator, the signal detector.

## 3. Decisions

### 3.1 The authority, applied

- **D15** (systematic term), **D16** (the amplitude's change, the long-run power, the backlogged limitations),
  **D17** (edges, filter, sampling, template, edge term, ν, the amplitude prediction, Bell's decay variant, memory
  and look-ahead, the floor placeholder, merging, VE3NEA's groups, the noise figures, the CPU breakdown, the newborn,
  Ω̄ by clipping with the noise-in-Ω̄ variant, ordinary least squares, w per sample with the fixed variant, C²'s own
  part, the grid convention, one sample set, the newborn's latest switch, the variant's exact sums and four-input
  table), **D18** (decoding errors first; the CPU target recorded, not constraining; the breakdown kept), and this
  plan's questions as the owner answered them on 2026-10-26: **D19** (W1AW recordings aligned to the published text
  set f), **D20** (Bell's variant changes only the mean's target), **D21** (the newborn's weight from the curve at
  each amplitude), **D22** (the newborn's integral on a fixed 128-point grid in u). The design notes' Part 8, Part 15
  items 2 and 16 and Part 17 are amended to match (commits fa58d31, 54c2f60, 81566bb).
- **Ruling 60:** the per-sample score uses the quadrature as built: adaptive Gauss–Hermite of order 5, centered at
  the integrand's mode by Newton's method, scaled by its curvature, mapped onto the Gaussian truncated at a = 0.
  The budget's scoring line is therefore the timing slice's row 1′ (412 ns per scored sample), not row 1.
- **Ruling 61: moot** (fix round 2). It set the newborn's weak/strong crossover of ln I₀ at 2b_k·z_k = 1.73 for the
  running sums; D22's grid evaluates the exact ln I₀ at every point and D21's weight follows the curve, so no
  crossover remains.
- **Ruling 62:** S2 keeps scaling its edges with its time base; D17's 2.5 ms edges apply to every other group except
  B and B2 (VE3NEA's 2 ms centered edges).
- **Ruling 63:** a new-decoder-only C++ test-signal helper with D17 edges; `engine/tests/test_signals.hpp` and the
  other decoders' tests and goldens unchanged.
- **Ruling 64:** merging on the packed state as it is (the owner's per-survivor decision); the previous element's
  length in the state is the measured variant (item 13).
- **Ruling 65:** table generators are C++ tools in the repository; the weight table and the default Ω̄ table compiled
  in; the variant's 12.8 MB table generated into `build/` and loaded when the variant runs (not committed).
- **Ruling 66** (worded as O13, fix round 5): committed fixtures naming removed configuration fields are updated in
  the task that removes the fields, their `config` rewritten; their statistics stay the old formats' examples.
- **Rulings 69–72** (the coordinator's, on the pre-flight scan of 2026-10-26, `.superpowers/sdd/2026-10-26-new-
  decoder-prototype-1b/preflight-scan.md`): 69, Task 2's phase tests assert continuity against an independent
  reference, O3 standing; 70, the milestone-1 scenarios and suites keep milestone 1's edges ("inside", 5 ms), D17's
  "start" applying to DEV2 and the new suites; 71, the statistics fixtures (formats 1 and 2 kept, format 3 added);
  72, the scan's 24 non-blocking resolutions (N1–N24), with the coordinator's choices where it offered several.
  Each is applied in the task it concerns.

### 3.2 Choices made here, each the only one that makes sense (obvious), and one ruling

O2 is the coordinator's ruling, not an obvious choice; O5 and O11 are withdrawn (fix round 1), their numbers kept so
that references stay valid.

- **O1. The branch `new-decoder-1b`,** a copy of `new-decoder-1`: Part 15 says the prototype is "built in a copy and
  run paired against today's decoder", and D16 "a prototype in a copy … before anything enters the branch".
  `new-decoder-1` keeps today's decoder; bringing `new-decoder-1b` into it needs the owner's approval.
- **O2. Following is for new-decoder channels only: the coordinator's ruling (2026-10-26), not an obvious choice.**
  D17 says "how they adapt is for the implementation plan". The bank stays frozen as the reference: its replay
  decodes fixed-bin channel streams and mixes by the label's offset and drift itself (code map §3.4). Matched, the
  product's default, is left as it is in this plan; its divergence from the spec's amendment (5832379: "the
  channelizer follows the detector's frequency for the track") is recorded in `docs/backlog.md` by the coordinator.
- **O3. Oracle frequencies.** An oracle channel's detector frequency is the label's: f(t) = freq_offset_hz +
  drift_hz_per_s·(t − start_s) for a single station; for a QSO on one track, the frequency of the over in progress,
  freq_offset_hz + the sender's offset_hz (+ the drift term), switching at each transmission's start_s and held
  through the silence after it. It is evaluated at each 32-sample channel block's start and held over the block
  (21.33 ms, the detector's own update interval).
- **O4. The noise figures are computed at startup from the running channelizer's taps** (σ_y²/N₀, ρ_1 … ρ_6 and w's
  two limits), as today's c(m) is: they differ between the 48 kHz chain (1 025 channel taps) and the 192 kHz chain
  (4 097) by 0.03%, and tests pin the design's values. The same holds for the template tables (Part 5).
- **O5. Withdrawn (fix round 1).** The edge term where edges overlap is not a choice: Task 4 computes it exactly,
  as the variance of the story's combined template over the edges' positions and their shared rise time jointly
  (derived), precomputed per pair of edge ages, that is per mark or gap length wherever two edges lie within 16
  samples.
- **O6. The edge history keeps every claimed edge within 16 samples;** a rescue newborn keeps its parent's edge
  history (the template describes the keying the story claims, and a rescue keeps the parent's claims), but restarts
  its amplitude and long-run sums (Part 7).
- **O7. ν includes the open bin,** so that ν exists from a story's first scored sample (ν = 1 on one bin) and changes
  with Ω̂: the open bin's cross terms with the 10 kept bins are fixed during the bin, so the update stays a few
  operations per sample. Bins lie on the channel's common grid of 16 samples (every story closes a bin at the same
  sample index).
- **O8. The default Ω̄ table's error:** at most 0.1% of Ω̄ (0.004 dB relative to the closed form's Ω̄) against the closed form over its whole domain
  (heuristic: well under the Satterthwaite form's own ~3% and the binning's 2.3%); the generator chooses the grid to
  meet it and records the error.
- **O9. The broad-prior regime with w:** the requirement is D4's, the per-sample score within 0.01 nats of a dense
  integral everywhere, the broad-prior region included. If adaptive Gauss–Hermite of order 5 meets it there with w,
  the regime and its tables are removed; otherwise its J table gains w as an input over 0.1986 to 0.2754 (Part 12),
  its grid set by the same 0.01 nats.
- **O10. The newborn's prior bounds** in FS are fixed at its birth from N₀ then (a_lo,hi = √(10^(L/10)·N₀·500 Hz),
  L = −15 and +85 dB S₅₀₀).
- **O11. Replaced by D20 (fix round 2):** the variant changes only the mean's target across a space (decay by
  Φ^0.5333 per sample, by at most 5 dB of amplitude); the variance follows the decided rule everywhere, no Q; inside a
  mark (one sample since the last estimate) the decided rule applies to both.
- **O12. The fixed-w variant** (item 18) sets w = 0.1986 on every sample, the newborn's included: it is the state
  the decoder "goes back to" if the curve shows no gain.
- **O13. Fixtures** (Ruling 66; Ruling 71): Tasks 2 and 10, which remove fields, rewrite the fixtures' `config`
  objects (their statistics are format examples, kept so that the readers of formats 1 and 2 stay tested); Task 11,
  which adds format 3, adds `tiny.v3.stats.json` (summary) and `tiny.v3-detail.stats.json` (detail) from one run.
- **O14. Names:** DEV2e, N2e, `d17-bank`, `nd1b-*` for the measurement suites, `kz4ap-viterbi-tables`.
- **O15. "Negligible" scatter** for the floor f (item 11): its 95% bootstrap interval includes zero. Where the
  synthetic and the W1AW recordings' f̂ (D19) differ, the proposal is the W1AW recordings' (the floor stands for what
  the generator does not model), with the synthetic one beside it; the owner decides.
- **O16. Order of the measurements:** the cheap synthetic grids (§7.2) run after R0's stop and before the long runs,
  so a failure of the design shows before the long runs' hours are spent (D18: decoding errors first).
- **O17. Today's decoder runs once** on all of DEV2e and N2e at its defaults (Part 15: "run paired against today's
  decoder, on the regenerated development set"), about 1 full-run equivalent of its own (§6.4). On F2 it runs a
  second time with its `--genie frequency` (its build still has it), so that both decoders are given the label's
  frequency there; on G2 (QSO labels, which its genie refuses) the comparison is asymmetric: today's decoder tracks
  the turnovers itself while the new one is given them, and M15 says so.
- **O18. Statistics format version 3** (the step is a sample; the CPU lines and counters added), in a summary form
  (version 2's analog) and a detail form (version 1's line columns, `--stats-detail`); versions 1 and 2 still read
  for their own fields.
- **O19. The measures for items 8 and 11** (defined in Task 12): the in-mark E[z²] of the amplitude's standardized
  innovation, and f̂² from the flat part's residual beyond the model's spread and the noise.
- **O20. Profiling** by the x86 time-stamp counter, with each counter's own cost calibrated and subtracted (Task 11).
  Around the parts of 1 to 7 ns per call (w, Ω̂'s and ν's sums, the variant's sums) the reads cost more than the
  parts and do not serialize, so those lines carry a stated accuracy of ± the calibrated per-call overhead; where R0's
  lines and the timing slice's leave-one-out figures differ by more than that, the slice's figures stay the budget's
  until a leave-one-out measurement of the built decoder replaces them.
- **O21. The generator's label field `edge_placement`** ("start", "centered", "inside") replaces `edges_centered`:
  old and new recordings both exist, and the label must say which convention keyed it.

### 3.3 The plan's questions, decided (no open questions remain)

The background, candidates and costs are in the report of the task that wrote this plan
(`.superpowers/sdd/2026-10-20-new-decoder-prototype-1/task-19-implementation-plan-report.md`) and, for the newborn,
in the measured study `.superpowers/sdd/2026-10-20-new-decoder-prototype-1/newborn-integral-candidates.md`; the
decisions are recorded in the prototype plan's §3.

- **Q1, the newborn integral's numerics: decided as D21 and D22 (owner, 2026-10-26: "a"; "Okay with A").** The
  running sums with one form of ln I₀ frozen per sample let the integrand run to the prior's top on 82–100% of first
  marks, and Newton with curvature-scaled Gauss–Hermite of order 5 has no mode where the belief peaks at the prior's
  bottom edge (measured). Decided: each sample's evidence with the exact ln I₀ and Part 4's weight curve w(q) at each
  amplitude, q = a²s_k²/σ_y² (D21), integrated on a fixed grid of 128 points even in u = ln a over the prior's
  11.51 nepers, trapezoid rule, points more than 80 nats below the largest dropped for good, ln I₀ and w from two
  shifted tables (D22). Task 9 implements it.
- **Q2, recordings for the floor f: decided as D19 (owner, 2026-10-26: "Yes c", the coordinator's lettering, (b)
  here).** Live W1AW code-practice sessions recorded off the air by the owner as I/Q, aligned to the ARRL's published
  text for the same session (forced alignment). The ARRL's downloadable MP3 files are probably rendered from the text
  (not confirmed) and lossy, so they only check the alignment tooling. The owner's recordings with the truth taken
  where the new decoder and the bank agree stay a cross-check, biased low (it keeps the samples the model fits).
  Rejected: hand-labeled excerpts; synthetic only; not chosen: the owner's own transmitter with the key line
  recorded.
- **Q3, the variance of Bell's decay variant across a space: decided as D20 (owner, 2026-10-26: "Sure iii").** The
  variant changes only the mean's target (decay toward zero at Bell's rate instead of the drift toward 0.886·√Ω̄); the
  variance follows the decided rule ρ_a²P + ½(1 − ρ²)Ω̄, and Bell's Q is not added. Bell's form in full may follow as
  a second variant if this one shows a difference worth explaining.

## 4. Structure in the code

| Path | What it holds |
|---|---|
| `engine/include/kz4ap/channel_follower.hpp`, `engine/src/channel_follower.cpp` | Part 1: the coarse bin rule (move at more than half a bin, 11.72 Hz) and the fine shift |
| `viterbi/front_filter.hpp`, `.cpp` | Parts 2–3: the 25 taps, the output noise figures, the envelope sampler |
| `viterbi/mark_template.hpp`, `.cpp` | Part 5: S(τ), s_j and v_j, the edge history, the claimed-edge rule |
| `viterbi/observation.hpp`, `.cpp` | Part 4: ln I₀, the tempered per-sample score and posterior (adaptive Gauss–Hermite) |
| `viterbi/weight.hpp`, `engine/src/viterbi/weight_table.cpp` (generated) | Part 4: w_k from q_k |
| `viterbi/systematic.hpp`, `.cpp` | Part 9: the 21-sample window, V′_k, the caps |
| `viterbi/amplitude.hpp`, `.cpp` | Part 6: the prediction, ρ and ρ_a tables, Bell's decay (variant) |
| `viterbi/long_run.hpp`, `.cpp`, `engine/src/viterbi/omega_bar_table.cpp` (generated) | Part 7: Ω̂, ν, Ω̄ (default), the variant's sums and table |
| `viterbi/newborn.hpp`, `.cpp` | Part 8: the newborn integral and its switch |
| `viterbi/search.hpp`, `.cpp`, `engine/src/viterbi/text.hpp` | Parts 4–11 in the search; the shared text; statistics |
| `bench/src/align.hpp`, `.cpp` | forced alignment of a published text (Task 13b; D19) |
| `engine/include/kz4ap/viterbi_decoder.hpp`, `engine/src/viterbi_decoder.cpp` | `ViterbiShared`, `ViterbiDecoder`: front filter, sampler, search |
| `engine/tests/viterbi/*_test.cpp`, `engine/tests/viterbi/keyed_signals.hpp` | one test file per component; the D17 keyed-signal helper |
| `bench/src/viterbi_tables_main.cpp` | `kz4ap-viterbi-tables` (Ruling 65) |
| `bench/src/viterbi_json.*`, `bench/src/main.cpp` | configuration, genie, statistics v3, oracle tracks, `--trace`, `--omega-bar-table` |
| `training/kz4ap_synth/generate.py`, `suites.py`; `training/kz4ap_proto/*` | D17 edges, label fields, measurement suites; runner, analysis, reductions |

## 5. Verification, written before any run

1. **Unit tests** of every formula of Parts 1 to 11 against an independent computation, with the design's own
   numbers where it derived them (Tasks 2 to 10, each listing its assertions).
2. **Synthetic stations end to end** (Task 10), through the engine with D17 edges:
   - "PARIS PARIS" at 12, 25 and 40 WPM, +20 dB S₅₀₀, machine keying: the exact text;
   - a step from 20 to 30 WPM at +20 dB S₅₀₀: character error rate (CER) at most 0.05;
   - 5 s of silence, then "CQ CQ" at 25 WPM, +10 dB S₅₀₀: the exact text;
   - stations at +60 dB S₅₀₀ (exact text) and at +85 and −10 dB S₅₀₀ (finite scores);
   - 5.2 WPM and 100 WPM at +20 dB S₅₀₀: CER at most 0.05;
   - the strong grid, +30 to +60 dB S₅₀₀ at 16 edge positions: every text exact (D15's standard; full sweep).
   - The Farnsworth cases stay known failures (D14).
3. **Determinism:** two runs give identical output, and so do inputs split into blocks of 1, 7, 32 and 1 000
   channel samples (Task 10); a bin move in mid-stream changes nothing between splits (Task 2).
4. **Strong clean signals:** A2's signals at +18 to +20 dB S₅₀₀ at the minimum beam of 28 nats give the wide-beam
   reference's text exactly (R2).
5. **Ten minutes of noise:** every score finite, live stories never above the ceiling, the shared text's memory and
   every newborn's grid state (about 1 KB each) bounded (Task 10).

## 6. The projected CPU budget by part

### 6.1 The measured slice (desktop)

**Measured** on the desktop (Intel i7-9700F, MSVC Release, one thread pinned), the timing slice of 2026-10-25: the
amended decoder's per-story work in a fused loop, with each part's cost attributed leave-one-out, and today's built
search measured for successors, merging and pruning. ms of CPU per channel-second at N live stories =
ns per story-sample × N × 375 samples/s / 10⁶ (derived). The share of story-samples with s_k ≠ 0 was 0.74 in the
slice (Part 7's figures for "PARIS": 63–71% at 5.2–12 WPM, 86% at 25 WPM, 95–100% from 40 WPM); at a share of 1.0
the scoring, w, sums and Ω̄ lines rise by 1/0.739 = 1.35 (derived).

| Part | N = 200 | N = 500 | N = 1 700 | Share of the projected total at 1 700 (425.7 ms, the base) |
|---|---|---|---|---|
| Template and the per-story loop | 0.87 | 2.33 | 9.06 | 2.1% |
| Ordinary scoring and the amplitude update, adaptive quadrature (row 1′, Ruling 60; 412 ns per scored sample) | 23.10 | 58.24 | 194.11 | 45.6% |
| w's lookup | 0.11 | 0.12 | 0.69 | 0.2% |
| Ω̂'s two sums and ν's bins | 0.25 | 0.60 | 1.78 | 0.4% |
| Ω̄'s default lookup (after samples with s_k ≠ 0; 38 ns per lookup) | 1.86 | 5.54 | 18.02 | 4.2% |
| Interaction (fused total less the parts' sum) | −0.12 | −0.87 | −3.28 | −0.8% |
| **Per-story work, default (fused)** | **26.07** | **65.96** | **220.38** | 51.8% |
| Successors, merging and pruning (today's built search, measured; 299–311 ns per story-step) | 23.31 | 56.89 | 190.71 | 44.8% |
| Projected total without the newborn line (the base of the owner's decisions of 2026-10-25 and 2026-10-26) | 49.4 | 122.9 | 411.1 | 96.6% |
| *Variant (item 17), net over the default:* the stories' own table inputs; random cells (cache worst case) | +2.74; +6.84 | +6.74; +17.60 | +22.01; +47.04 | +5.2%; +11.1% |
| *of which:* the noise-variance sums (16 ns per scored sample) | 0.90 | 2.27 | 7.62 | 1.8% |
| *of which:* the four-input lookup, own inputs (78 ns); random cells (132 ns) | 4.17; 8.28 | 11.28; 21.88 | 36.75; 61.99 | 8.6%; 14.6% (gross, in place of the default's 4.2%) |
| *The newborn's grid (D22), net of row 1′ which it replaces on its stories:* 490 ns per newborn-sample on noise, 595 on a mix of levels (measured, desktop, 500 newborns, 43.5 and 53.8 live points); M = 59, 147, 500 newborns (29% of N, the design's 500 at the ceiling) | +1.73; +4.05 | +4.30; +10.09 | +14.7; +34.3 | 3.4%; 8.1% |
| **Projected total, default, with the newborn's grid: the base** (newborns on noise; beside it, on a mix of levels) | **51.1; 53.4** | **127.2; 133.0** | **425.7; 445.4** | **100%; 104.6%** |

**The base for every fraction in this plan** (the owner's rule, 2026-10-25: "the fraction of the total time we
think we're going to achieve"): the projected default total with the newborn's grid, **newborns on noise: 51.1,
127.2 and 425.7 ms per channel-second at 200, 500 and 1 700 stories**, with the mix of levels (53.4, 133.0 and
445.4 ms, 4.6% more) beside it. Noise is the base because it is the case the design's newborn count describes ("about
500 newborns inside the integral at once on noise") and the one measured with 500 newborns; the mix is the bound when
newborns sit on stations, and R0 replaces both with the measured mix. **The owner's decisions of 2026-10-25 and
2026-10-26 (D17 to D22) quoted fractions of the earlier 411.1 ms**, the total without the newborn line (D22's +3.6%
and +8.3% are 3.4% and 8.1% of 425.7 ms).

Shares at 200 and 500 stories (of 51.1 and 127.2 ms): scoring 45.2% and 45.8%, search 45.6% and 44.7%, Ω̄ 3.6% and
4.4%, the template 1.7% and 1.8%, the variant +5.4% to +13.4% and +5.3% to +13.8%, the newborn's grid 3.4% (noise)
to 7.9% (mix). The newborn's grid (gross 91.9 ms per channel-second on noise at 500 newborns, 111.5 on the mix)
replaces the timing slice's row 6 (153 ns, 28.7 ms), which computed the unbounded running sums.

**How the projection could be off** (the timing slice's own list): the story grows from about 140 to 376 bytes and
the search copies it twice per step (27.4 ms at 1 700 stories for the two copies, partly inside the search line
already); the newborn's 1 KB of grid state is copied whenever its story branches inside its first mark, about 60 ns
per copy (estimated, not timed; the study's §6); the ends section of today's search (59% of its overhead) was never optimized; the variant's 12.8 MB table exceeds the 12 MB third-level cache, so with many channels the
random-cell figures are the better guide; the stories of the slice did not decode, so the mix of ln I₀ branches and
Newton iterations is plausible, not the decoder's.

**The Linux machine:** about 0.7 times the desktop's figures, from one earlier comparison of a different slice
(83–94 ns per story per piece on the desktop against 63–64 ns on the Linux machine; prototype plan §2.11). **Rough:**
not a calibration of this slice. By it, the projected default totals are 34.6, 86.0 and 287.8 ms per channel-second
at 200, 500 and 1 700 stories without the newborn line, and 35.8–37.4, 89.0–93.1 and 298.0–311.8 ms with it.

**The target (D18):** about 500 tracks on one band on half the desktop's cores, about 8 ms per channel-second,
recorded and not constraining. Every CPU measurement keeps the breakdown by part, stated against this projected
total, so the later speed work starts from measurements.

### 6.2 What R0 replaces

R0 (§7.1) measures, on the Linux machine, per group: the CPU per channel-second with the breakdown of Task 11 (its
lines are the rows above, plus the newborn integral, the front filter and the rest), the live and scored
story-samples, the newborns in the integral, and the reference settings' cost multiple. Those numbers replace the
slice's projection, the share 0.74, the 0.7 factor and the assumed average story counts in §6.3 before the long
runs start, and the owner sees them first.

### 6.3 What the runs cost (the prototype plan's §6.3 redone)

One full-run equivalent (FRE) is 1 065 742 channel-seconds × c, c the CPU per channel-second on the Linux machine.
With the projected default totals including the newborn's grid × 0.7 (rough), one FRE is 10.6–11.1, 26.4–27.6 and
88.2–92.3 CPU-hours, that is 1.06–1.11, 2.64–2.76 and 8.82–9.23 hours of the Linux machine's 10 cores, at an average
of 200, 500 and 1 700 live stories (derived; the ranges are the newborn's grid on noise and on a mix of levels;
without it, 10.24, 25.47 and 85.19 CPU-hours).

| Runs (§7) | FRE |
|---|---|
| R1 + R1b (the reference at 3–10 times the default's cost; R1b on a quarter of DEV2e at twice R1's) | 4.5–15 |
| R2 (4 beams) | 2.2–4.6 |
| R3 (4 new ceilings) | 3.3–4.8 |
| R4 (DEV2e and N2e) | 1.03 |
| R5 (4 runs on DEV2e and N2e) | 4.1 |
| R7 (cost about 1.9 times) | 1.9 |
| R8, R10, R11 | 0.5 |
| M13: the merge variant on the signals above about 45 WPM | 0.3–0.6 |
| M16: Bell's decay on every group and N2e | 1.03 |
| M17: the noise-in-Ω̄ variant on every group and N2e (cost 1.05–1.14 times) | 1.09–1.18 |
| M18: the fixed w on the weak-signal groups | 0.5 |
| The synthetic grids of §7.2 (M2–M12, M14, M15) | under 0.2 |
| **Total** | **about 20.7–35.4** |

| Average live stories | Hours of the Linux machine's 10 cores |
|---|---|
| 200 | 22–39 |
| 500 | 54–98 |
| 1 700 | 182–327 |

Not in the table: today's decoder once on DEV2e and N2e (O17), about 59.5 CPU-hours on DEV2e and 61.5 with N2e,
6.2 hours of the 10 cores, at its recorded 287 ms per channel-second on noise (desktop, Task 4d) × 0.7 (rough), plus
its second run on F2 with the frequency genie; and each rerun of R4 that a
changed default requires (R7 adopted, M18 reverting w, M11's floor), 1.03 FRE each. As fractions of the new
decoder's projected total (the base, §6.1), the variants of §7.2 cost: M17's variant 5.2% to 13.8% more per
channel-second (own inputs to random cells, over 200 to 1 700 stories); M18's fixed w saves w's line, 0.2%; M16's Bell decay changes the prediction's arithmetic
only (within the scoring line's run-to-run scatter, conjectured); M13's merge variant adds stories, its cost measured.

### 6.4 The sizes of the sets

DEV2e: 1 065 742 channel-seconds, roughly 1 050 000 letters. A quarter of DEV2e for R1b only (every fourth recording
of each group, by name). N2e: 36 000 channel-seconds. The synthetic suites of §7.2: about 0.2 FRE together (counted
in Task 13 from their definitions).

## 7. The measurements

Every run decodes through `kz4ap-bench --oracle --decoder viterbi` on the Linux machine, one test case per process,
ten at a time; oracle channels follow the label's frequency (O3). Each rule is written here before it runs. "Paired"
is the project's paired comparison per signal with its 95% bootstrap interval (`docs/signal-processing.md`, A.11);
"better" and "worse" mean that interval lies wholly below or above zero. Every reported run's rounded per-signal
score table is committed with its results section (CLAUDE.md), and every CPU figure is given by part against §6's
projected total.

### 7.1 The prototype plan's runs, revised (code map §5)

| Run | Status | What it measures | Set | Rule |
|---|---|---|---|---|
| **R0** | applies; content changed | CPU per channel-second **with the breakdown by part** (Task 11), live and scored story-samples, how often the ceiling decides, newborns' fates and the newborns in the integral, at the defaults and the reference settings; the carried items: quadrature orders 3, 5 and 20 for texts and scores (prototype plan Rulings 16, 29, 30; D4's 0.05-nat rule) on B2's and N2e's recordings and the strongest signals | one recording per group of DEV2e, and one of N2e | Measurement only. **Stop:** the owner sees R0's measured costs, by part, against §6, in chat, before any long run (R1 on). |
| **R1, R1b** | apply unchanged | the wide-beam reference (80 nats, 17 000 stories; R1b 120 nats, 34 000); the largest deficit of a newborn before it leads | DEV2e; a quarter | As the prototype plan's §6.1; the starting costs read from the configuration (silence: ln(1/P) + ln 32 = 12.39 nats at P = 1.333 × 10⁻⁴ per sample; rescue 14.873 nats). |
| **R2** | applies unchanged | search errors at beams 28, 35, 45 and 60 nats | DEV2e | As the prototype plan's §6.1; §5, item 4 checked on the 28-nat run. |
| **R3** | applies unchanged | CER against ceilings 300, 600, 1 000, 1 700, 3 000 | DEV2e | As the prototype plan's §6.1. |
| **R4** | applies; reference changed | the default against `d17-bank` and against today's decoder, by group and speed cell; CPU by part and live stories per group; space error rate; false characters per hour on N2e | DEV2e, N2e | The main result. All of I2 reported apart from the comparisons, a known gap (D14; the I2 cut is void). |
| **R5** | applies unchanged | each rate at 10 times and a tenth | DEV2e, N2e | As the prototype plan's §6.1. |
| R6 | **void** | (the fading spread sweep) | — | f_D is the owner's fixed 2 Hz (D16); M3 and M8 replace it. |
| **R7** | applies unchanged | the 5% speed grid | DEV2e | As the prototype plan's §6.1; if adopted, R4 reruns. |
| **R8** | applies | genie timing on C2 | C2 | As the prototype plan's §6.1. |
| R9 | **void** | (the tracker's error) | — | No decoder tracker (Part 10); M15 replaces it. |
| **R10** | applies in a new form | the fixed 2 Hz against each signal's true spread (`--genie fading`) | B2 | Measurement: what one fixed spread loses per spread cell; M8's DEV2e part. |
| **R11** | applies unchanged | the hiss estimator's error | every DEV2e recording | Measurement only. |

### 7.2 Part 15's measurements

Synthetic suites are Task 13's `nd1b-*` suites, keyed by the generator with D17 edges unless stated, recorded at
48 kHz with the generator's noise (σ = 0.02 FS), decoded by the new decoder and by today's decoder, paired. "To the
owner" means the results go to him in chat before anything changes.

**Jittered grids** (CLAUDE.md; owner, 2026-10-06; `docs/plans/2026-10-06-development-set-redesign.md`): every sweep
over a range (levels, speeds, offsets, delays, rise times, crash strengths) is a set of cells over the range, each
signal's value drawn at random within its cell. Fixed only where the design names points: M3's fading rates (0.1,
0.3, 1, 2, 5 Hz), levels (+10, +20, +30 dB S₅₀₀), speeds (25, 8, 5.2 WPM) and 4 edge positions; M4's 16 edge
positions and fading rates (0.3, 1, 2, 5 Hz); M7's start depths (10, 20 dB relative to the long-run power), fading
rates (1, 2, 5 Hz) and crash strengths (20, 40 dB relative to the key-down power); M9's speeds (5, 12, 25, 40,
100 WPM); M12's rise times (0.8, 4.7 ms), mark biases (−7, −3.5, 0, +2.5, +4.7 ms) and speeds (25, 100 WPM). The
cells below are stated as ranges and counts.

| Item | What | Set | Rule |
|---|---|---|---|
| **M1** correctness | the unit tests' checks (Tasks 2–10) and, on recordings, through the true-story trace (Task 12), each tolerance written here before the run: the template's misfit per sample, its variance over v_j within [0.8, 1.25] at every j where v_j > 10⁻⁵ on 2.5 ms edges (heuristic bounds); the decoded symbols' start times (the bench's `symbols.csv`, Task 12) 0 to 2.667 ms (±0.1 ms) after the label's keyed times; ν against the empirical count of independent looks of the label's fading within 3% (Part 7's measured agreement); Ω̂ and Ω̄ against the generator's fading (reported); the output noise's power and correlation against σ_y² (±1.5%) and ρ_m (±0.01); the variant's E[N²]/(A + B·Ω) on the decoder's own samples of steady stations (N = Ω̂ − a², a from the label's S₅₀₀; at least 1 000 marks) within [0.95, 1.05]; determinism across block splits on one recording of each group (identical output) | `nd1b-fading`, `nd1b-strong` steady, A2's and B2's recordings with key intervals | Each check passes its tolerance; a failure stops the long runs and goes to the owner. |
| **M2** the newborn's approximations | (a) the grid (D22) against a dense integral of the same integrand (exact ln I₀, D21's weight), on the study's levels and, new, above +40 dB S₅₀₀ (+50, +60, +70 and +85 dB S₅₀₀), where the narrowest σ_u at handover is roughly 0.03 nepers by the floor-limited derivation that ignores the caps and about 0.074–0.078 nepers by the cap-limited one (both rough; this measurement settles it), and where the trapezoid's end-point error at the prior's top, about h²·|F′(u_hi)|/12 (derived), is reported per sample and per first mark; the narrowest σ_u at handover and the deepest revival of a grid point below the largest (Task 11's counters); (b) the amplitude held constant before the switch, under fading at 0 dB S₅₀₀, 2 Hz and 5 Hz, against the integral over the fading path; (c) the switch; the newborn's CPU line | the committed measurement test `Newborn.ApproximationsMeasured` (Task 9, label `measurement`), run in Task 16 | Measurement; every result, with its distribution per sample and per first mark (median, 95th percentile), to the owner (Part 8: anything over a fraction of a nat goes to him). A narrowest σ_u at handover below 0.05 nepers, or a revival deeper than 40 nats, goes to the owner (D22), here and in every run that records them (R0, M14, R4). |
| **M3** the fading grid | f_D = 0.1, 0.3, 1, 2, 5 Hz × +10, +20, +30 dB S₅₀₀ × "PARIS PARIS" at 25, 8 and 5.2 WPM × 4 edge positions × 5 seeds (900 signals); exact texts and CER; the in-mark E[z²] (O19) | `nd1b-fading` | Paired against today's decoder on the same signals; measurement, to the owner. |
| **M4** the strong grid | +30 to +60 dB S₅₀₀ in six 5 dB cells × 16 edge positions × steady and 0.3, 1, 2, 5 Hz; mark splits and invented overs | `nd1b-strong` | Steady: every text exact (D15's standard, as 36084bc reached); faded: measurement, to the owner. |
| **M5** the speed step | 20 to 30 WPM, +10 to +28 dB S₅₀₀ in nine 2 dB cells, 3 signals per cell | `nd1b-speed` | CER at most 0.05 on every signal at the default beam (the prototype plan's §5, item 2). |
| **M6** caps and crashes | the fall's tail, the filter's ringing and a filtered click at every edge position: each sample's score against its cap w_k s_k²/(f²s_k² + v_k); false characters per crash at +20 to +60 dB S₅₀₀ in four 10 dB cells (dB S₅₀₀ of the crash's peak power) | `nd1b-crash`; the trace | A score above its cap (beyond the quadrature's 0.01 nats) is a bug; false characters: measurement, a known limitation (Part 13), to the owner. |
| **M7** the long-run amplitude in the loop | starts in a fade (10 and 20 dB down, dB relative to the long-run power) and normal starts at 1, 2, 5 Hz: splits in an over's first 3 s; rescues and over boundaries; a weak station beside a neighbor inside the filter; crashes of 20 and 40 dB and static bursts of 10 to 100 ms inside marks (dB relative to the key-down power); noise alone; Ω̄ with the prior's top at +60 instead of +85 dB S₅₀₀ (ν ≈ 1) and its bottom at −5 instead of −15 dB S₅₀₀, each run with an Ω̄ table generated for its bounds (Task 7's `--lo-db/--hi-db`, loaded by `--omega-bar-default-table`); how often Ω̂ ≤ 0 at a lookup, on N2e and on M9's signals | `nd1b-longrun`, N2e, M9's suite | Measurement, to the owner. |
| **M8** the fixed 2 Hz against the genie spread | M3's grids with `--genie fading`; the in-mark E[z²] | `nd1b-fading` | Measurement: it sizes the backlogged several-rate model and the in-mark limitation. |
| **M9** steady weak signals | 5, 12, 25, 40, 100 WPM, −15 to +5 dB S₅₀₀ in twenty 1 dB cells, 10 signals per cell | `nd1b-weak` | S₅₀₀ at 50% CER per speed, with its interval, against today's decoder and the bank (`--decoder bank` on the same recordings); the slow speeds' cost against the expected up to 1.8 dB of noise power (Part 2). Measurement. |
| **M10** the systematic term's memory and look-ahead (10 samples each) | M4's grid, the stopped-neighbor grid (cells over a neighbor 30–40 dB stronger, dB relative to the station's key-down power, 50–200 Hz away, stopping 0.05–0.60 s before the station starts), M5, "TEST" and "TU"; how often a sample's own e_k²/s_k² sets C², with its template value and SNR | `nd1b-strong`, `nd1b-stopped`, `nd1b-speed` | Measurement, compared with D15's recorded figures (194 of 288 stopped-neighbor runs lost with 390 ms); no other variant (D17). |
| **M11** the floor f | the scatter of strong stations' flat-part samples about the model's prediction beyond the noise, relative to the carrier, along the true story (f̂, O19) | `nd1b-strong` steady and faded; W1AW code-practice sessions recorded off the air (D19), with their published text force-aligned; the owner's other recordings where the new decoder and the bank agree, as a cross-check (biased low) | The proposed f is f̂ (zero if its 95% interval includes zero on both sets; where the two sets differ, the real recordings' f̂, the synthetic one beside it; O15); on the W1AW sessions the excluded share of words and f̂ with and without the excluded words are reported, and the flat samples next to aligned edges are left out (Task 12); M4 and M6 are rerun at the proposal before it goes to the owner, who decides. |
| **M12** other transmitters' edges | 10–90% rises of 0.8 and 4.7 ms; mark bias −7, −3.5, 0, +2.5, +4.7 ms; 25 and 100 WPM; +10 to +40 dB S₅₀₀ in three 10 dB cells; the misfit's variance per sample against v_k; decoding | `nd1b-edges`; B2 (VE3NEA's 1.18 ms edges) | The misfit's variance per template sample over v_k reported (a ratio near 1 or below supports the edge term); decoding paired against the 2.5 ms signals. Measurement, to the owner. |
| **M13** the merge variant | the previous element's length in the state when shorter than 10 samples (`merge_previous_element`), against the default | DEV2e's signals above 45 WPM; `nd1b-speed` at 45–100 WPM | Paired decoding and the CPU breakdown with story counts, to the owner before anything changes (D17). |
| **M14** noise alone | false characters per hour, newborns in the integral, CPU by part against §6 | N2e | False characters per hour no more than today's decoder's on N2e (Part 15: "no more than today's decoder"); else to the owner. |
| **M15** neighbors and frequency | neighbors in three 10 Hz cells from 40 to 70 Hz away, of equal strength and 10 dB stronger (dB relative to the station's key-down power); drifting stations (F2) and QSO turnovers (G2) with the oracle following the label | `nd1b-neighbors`; F2, G2, E2 of R4 | Paired against today's decoder; on F2 against its run with `--genie frequency` (both given the label's frequency); on G2 the comparison is asymmetric (today's decoder tracks the turnovers itself; its genie refuses QSO labels) and is reported as such (O17). Measurement, to the owner. |
| **M16** Bell's decay variant | `bell_decay` against the default on every group and N2e: exact texts, CER, splits, false characters; a close neighbor (E2) the case to watch | DEV2e, N2e | Measurement, to the owner (D17). |
| **M17** the noise-in-Ω̄ variant | `omega_bar_noise` against the default on every group and N2e: exact texts, CER, splits, false characters; the Ω̄ and sums lines of the breakdown; how often Ω̂ falls below its Ω̄'s minimum position (the Gaussian artifact) on N2e and M9 | DEV2e, N2e, M9's suite | Measurement, to the owner, who decides whether it earns its place and then the artifact's remedy (Part 7). |
| **M18** the fixed w | `fixed_weight` against w per sample on M9's suite and DEV2e's weak groups (A2's cells at or below +2 dB S₅₀₀): S₅₀₀ at 50% CER, exact texts, CER, w's CPU line | `nd1b-weak`, A2 | Seven comparisons, named before the run: the pooled paired comparison on `nd1b-weak` and on A2's weak cells, and the 50%-CER crossing at each of M9's five speeds. If none is better with the curve (the paired interval wholly in its favor, or the crossing lower by an interval excluding zero), the decoder goes back to the fixed 0.1986 (owner, 2026-10-25; R4 reruns). Under no effect each 95% interval lies wholly on the curve's side with probability 0.025, so one chance "better" among seven has a probability of about 0.16 (derived, independent comparisons assumed); a result resting on a single comparison is reported as such to the owner. |

### 7.3 Order

R0 and M1 (Task 15) → **stop for the owner** → the synthetic grids M2–M12, M14, M15's synthetic part (Task 16) → R1,
R1b, R2, R3 (Task 17) → R4 with M13, M15, M16, M17, M18 (Task 18) → R5, R7, R8, R10, R11 and M11's proposal (Task 19).
A changed default (R7, M18, M11) reruns R4 at the new default.

## 8. Global constraints

- **Signal processing is C++, in the engine** (owner, 2026-10-07 and 2026-10-20). Python only for the generator,
  the suite runner and the analysis, with their tests. The table generators are C++ (Ruling 65).
- **Compilers:** MSVC 14.5x and g++ 11.4: no `std::format`, `<print>` or `std::expected`. `ctest --preset windows`
  passes on every commit, and `ctest --preset linux` on the Linux machine at Tasks 2, 10 and 14. The engine stays
  free of Qt and nlohmann/json.
- **`docs/signal-processing.md` describes the engine's signal processing as it actually is** (CLAUDE.md). Any change
  to signal processing (a parameter value, an algorithm, the order of stages, a new stage) updates the body and,
  where a derivation, provenance or limitation changes, the appendix (A.10 included), in the same commit. The body
  stays within 6 000 words (5 946 today; §8d is 2 029 of them). Library code the engine does not run yet changes no
  description (the prototype plan's Ruling 5): Tasks 3 to 9 supply their text to Task 10. **Every implementer is told
  this rule, and each task below says what it changes in the document.**
- **Units:** every quantity carries its unit; every dB names its reference (dB S₅₀₀, dB SNR in the filter's 74.48 Hz,
  dB relative to 0 Hz, dB relative to the long-run power, dB relative to the key-down power). Parameters are in
  physical units (Hz, s, FS, FS², nats, dots), converted to samples or bins only where used. Timing medians are in
  dots; the fading rate, the systematic memory and look-ahead and ν's bins are in seconds (fading and the filters'
  responses do not scale with the sending speed). The model's own mismatch is "systematic", never "noise".
- **Values:** every value in code comments and documents is labeled derived, measured, heuristic, placeholder or
  owner with its date, as the design marks it.
- **Determinism:** the same recording and settings give the same output on every run on one platform; ties are
  broken by a fixed key; tests compare across platforms with tolerances; every measurement runs on the Linux machine.
- **Git:** one git command per call, never chained (no `&&`, `;` or `|`, no `cd …`, no `git -C`); commit freely on
  `new-decoder-1b`; never push or merge without the owner's approval, each time; never rewrite commits; never discard
  work (stash or a worktree); no attribution lines.
- **The Linux machine:** code by git bundle, results by scp, long jobs under the machine's job helper; result folders
  are never deleted or overwritten; every run gets a new name; check that nothing is running before starting. The
  workflow is in `.superpowers/sdd/2026-10-20-new-decoder-prototype-1/server-workflow.md` (git-ignored).
- **No private infrastructure** in anything committed: "the Linux machine" and "the desktop", with no host, user,
  path or location. The staged diff and the message are checked before every commit.
- **Results:** the rounded per-signal score table of every reported run is committed in
  `docs/plans/data/2026-10-26-new-decoder-prototype-1b/`; the record's numbers are computed from those tables;
  figures follow CLAUDE.md. Recordings, traces and the variant's table stay in `build/`.
- **Deleting:** only files created in the current task; nothing under `build/` or `.superpowers/` otherwise.
- **Processes:** stop only processes you started, by process ID, never by image name.
- **Words:** American spelling; "variant", never "arm"; a decoder is scored on a recording; "front end" means the
  signal detector; every term and symbol defined where first used.

## 9. Review focus

The inputs most likely to break the amended decoder that §5 does not already pin, most likely first; each is tested
in the task named.

1. **A newborn on noise and at the prior's edges:** on noise the belief peaks at the prior's bottom edge, and at
   +85 dB S₅₀₀ at its top; the grid's end points carry both (D22). Expected: every score finite and under 1.387 times
   its cap, the posterior mean below 0 dB S₅₀₀ on noise. Tests: `Newborn.StaysBoundedOnNoise`,
   `Newborn.StrongEdgeOfThePrior` (Task 9).
2. **The noise density at zero or jumping:** N₀ = 0 before the first frame and after exact zeros makes σ_y² = 0, so
   V′ = 0 on spaces and b, z undefined. Expected: samples are not searched while N₀ ≤ 0, as today, and the window and
   sums carry over. Tests: `ViterbiDecoder.ExactZerosThenAStation`, `ANoiseDensityOfZeroMidStreamSplicesTheStream`
   (Task 10).
3. **Input split anywhere and a bin move in mid-block:** an envelope sample at a block's edge, the front filter's
   24-sample history across blocks, a coarse move between two blocks, the look-ahead's 10 samples at the stream's end
   (`finish`). Tests: `EnvelopeSampler.SplitsDoNotMatter` (Task 3), `ChannelFollower.PhaseIsContinuousAcrossABinMove`
   (Task 2), `ViterbiDecoder.InputSplitDoesNotMatter` and `Search.FinishSearchesTheLastSamples` (Task 10).
4. **The grid's ends:** 1-sample marks and gaps fill the edge history with up to 16 edges; a 5.2 WPM word gap is
   605 samples; a story that lives for an hour carries ν's running sums and Ω̂'s sums without overflow or loss of
   precision. Tests: `MarkTemplate.SixteenEdgesInTheHistory` (Task 4), `LongRun.AnHourOfKeyDownStaysExact` (Task 7),
   `ViterbiDecoder.TheGridsEnds` (Task 10).
5. **Extreme levels:** at +85 dB S₅₀₀ the carrier is q = 2.1 × 10⁹ times the noise, the edge term's values near 10⁻¹⁰
   matter (Part 5), the caps bind, Ω̄ sits at the prior's top, and a newborn's mode at a_hi. Expected: finite scores,
   no NaN, the text exact at +60 dB S₅₀₀. Test: `ViterbiDecoder.ExtremeLevels` at +60, +85 and −10 dB S₅₀₀ (Task 10).

---

## 10. Tasks

| Task | What | Depends on |
|---|---|---|
| 1 | The test generator's D17 edges (Python) | — |
| 2 | The channelizer follows the detector; the decoder's tracker removed; the oracle's true frequency | — |
| 3 | The front filter, the envelope samples, the output noise; the D17 keyed-signal helper (C++) | — |
| 4 | The mark template and the edge term | 3 |
| 5 | The evidence per sample, the weight w and its generator, the systematic term | 3, 4 |
| 6 | The amplitude prediction (with Bell's decay for the variant) | — |
| 7 | The long-run amplitude: Ω̂, ν and the default Ω̄ with its generator | 3, 4 |
| 8 | The noise-in-Ω̄ variant's sums and table | 7 |
| 9 | The newborn integral on a grid (D21, D22) | 4, 5, 6 |
| 10 | The search per sample; the decoder; configuration; the bench's JSON; fixtures; documents | 2–9 |
| 11 | Instrumentation: the CPU breakdown by part and Part 15's counters; statistics version 3 | 10 |
| 12 | The variants' switches; the true-story trace | 10, 11 |
| 13 | The measurement suites, label fields and the analysis (Python) | 1, 12 (formats) |
| 13b | Forced alignment of a published text to a recording (D19; M11's true story) | 10, 12 |
| 14 | DEV2e and N2e; `d17-bank`; today's decoder built (Linux machine) | 1, 13 |
| 15 | R0 and M1; **stop for the owner** | 11–14 |
| 16 | The synthetic grids (M2–M12, M14, M15's synthetic part) | 15 |
| 17 | R1–R3 | 16 |
| 18 | R4 with M13, M15, M16, M17, M18 | 17 |
| 19 | R5, R7, R8, R10, R11; M11's proposal; the conclusions | 13b, 18 |

Every building task ends with `ctest --preset windows` passing (and `pytest training -q` where Python changed), its
documents updated per §8, and one commit. Task numbers are this plan's own.

### Task 1. The test generator's D17 edges (Part 14; Ruling 62)

**Files:** modify `training/kz4ap_synth/generate.py`, `training/kz4ap_synth/suites.py`, `docs/signal-processing.md`
(A.11); tests in `training/tests/test_generate.py`, `test_suites.py`, `test_synth_suites2.py`.

**Interfaces (produced):**
- Constants: `RAISED_COSINE_10_90 = (pi - 2*acos(0.8))/pi` (0.590334, derived), `EDGE_RISE_10_90_S = 0.0025` (owner,
  2026-10-23), `EDGE_S = EDGE_RISE_10_90_S / RAISED_COSINE_10_90` (4.2349 ms, the full length), and
  `D17_EDGES = {"edge_s": EDGE_S, "edge_placement": "start"}`. `RISE_S = 0.005` stays: milestone 1's edge (Ruling 70).
- **Which recordings take which edges (Ruling 70):** the milestone-1 scenarios and suites (the band and single
  scenarios, `smoke`, `full`, the stage-1 benchmark's groups) keep milestone 1's edges, placement "inside" with
  `RISE_S`, because they are frozen evidence: `test_generate.py`'s frozen copies of milestone 1's generator
  (`test_default_signals_match_milestone_1_generator`, `test_band_defaults_match_milestone_1`) and A.11's smoke check
  (34 edits in 964 symbols on the Envelope path, 60 on Matched) rest on them and must pass unchanged. D17's edges
  (`D17_EDGES`) apply to DEV2 (DEV2e) and every new suite (Task 13's `nd1b-*`), set explicitly by their builders;
  B and B2 keep VE3NEA's centered 2 ms edges and S2 scales D17's (Ruling 62). So `SignalSpec`'s and
  `keying_envelope`'s defaults stay milestone 1's.
- `SignalSpec.edge_s: float = RISE_S` (full length of the raised cosine, as today), `SignalSpec.edge_placement: str =
  "inside"` (milestone 1's default, unchanged); values `"start"` (D17: the rise starts at key-down and the fall at
  key-up), `"centered"` (VE3NEA's: each edge centered on the element's end), `"inside"` (milestone 1).
  `edges_centered` is removed (its users in `suites.py` and `test_generate.py`'s non-frozen tests pass
  `edge_placement="centered"`); labels carry `edge_placement`.
- `keying_envelope(intervals, offset_s, n, sample_rate, rise_s=RISE_S, centered=False, placement=None)`: today's
  signature and defaults (milestone 1's "inside"), so the frozen copies' calls are unchanged; `placement`, when given,
  overrides `centered`, and `rise_s` is the edge's full length. For "start": env(t)
  = Σ over marks of c(t − on)·(1 − c(t − off)), c(x) = ½ − ½cos(π·clip(x/E, 0, 1)), E = edge_s, evaluated at the
  sample times; each mark covers [on, off + E] (Part 2's p(t)); a mark shorter than E follows the product, and a gap
  shorter than E sums a fall and the next rise, which stays at or below 1 because c is non-decreasing (derived; the
  template's superposition, Part 5). "centered" and "inside" reproduce today's output exactly.
- `generate` and `add_overs` size each signal's span from the placement (a "start" edge reaches E past the last
  key-up).

- [ ] **Step 1: Failing tests.**
  - `test_edge_constants`: `RAISED_COSINE_10_90` = 0.590334 ± 10⁻⁶; `EDGE_S` = 4.2349 ms ± 0.0001 ms.
  - `test_start_edges_keep_the_keyed_length`: one 12 ms dot at 192 kHz, no noise: the envelope crosses 0.5 at
    on + E/2 and off + E/2 (±1/192 000 s), its 10–90% rise is 2.500 ± 0.010 ms, and its length at half amplitude
    12.000 ± 0.010 ms; its flat part (envelope 1) is 12 − 4.235 = 7.765 ms ± 1 sample (65% of the dot).
  - `test_short_mark_is_the_product`: a 3 ms mark: env = c(t − on)(1 − c(t − off)) at every sample within 10⁻¹².
  - `test_centered_and_inside_are_unchanged`: for VE3NEA's 2 ms centered edges and the 5 ms inside edges, the
    envelope equals the pre-change function's output (copied into the test as the reference) bit for bit.
  - `test_default_signals_match_milestone_1_generator` and `test_band_defaults_match_milestone_1` pass **unchanged**
    (Ruling 70).
  - `test_milestone1_suites_keep_inside_edges`: every signal of `smoke` and `full` has `edge_placement` "inside",
    except B ("centered", 0.002 s); `RISE_S` everywhere except B and S (`RISE_S` × 25/12, its time base).
  - `test_regeneration_changes_only_the_envelope`: one recording of each DEV2 group generated before and after
    (the old generator's labels kept as a fixture, produced in Step 3 from the pre-change commit): texts,
    transmissions' start and end times, speeds and noise seeds identical; the label schema changes everywhere
    (`edges_centered` becomes `edge_placement`, B2's included, where it reads "centered"), and the values of
    `edge_s` and the placement change only outside B2.
  - `test_s2_scales_its_edges` (Ruling 62): S2's `edge_s` = 2 × A2's (8.4698 ms), placement "start".
  - `test_suites.py` lines 62–63 (the full, milestone-1 suite) become: B "centered" with `edge_s` 0.002; S
    "inside" with `RISE_S` × 25/12; every other group "inside" with `RISE_S`. The DEV2 suite's test
    (`test_synth_suites2.py`): B2 "centered" with 0.002; S2 "start" with 2 × `EDGE_S`; every other group "start"
    with `EDGE_S`.
- [ ] **Step 2: Run them; they fail.** `PYTHONPATH=training python -m pytest training -q`.
- [ ] **Step 3: Implement.** The fixture of Step 1's regeneration test is the labels of the pre-change generator for
  one recording per group (seed 1), committed under `training/tests/data/` (a few kB).
- [ ] **Step 4: Run them; they pass,** with every earlier Python test.
- [ ] **Step 5: Document** (same commit): `docs/signal-processing.md` A.11's entry "Keying edges (synthetic
  recordings, `edge_s`, `edges_centered`)" is rewritten: the fields `edge_s` (the raised cosine's full length) and
  `edge_placement`; "start", the default (owner, 2026-10-23; D17): the rise starts at key-down and the fall at
  key-up, 2.5 ms 10–90%, full length 4.235 ms (derived: 2.5 ms/0.5903), so each mark keeps its keyed length at half
  amplitude, at every speed, for DEV2 (DEV2e) and the new suites; "centered": VE3NEA's 2 ms edges centered on the
  element's ends (groups B and B2); "inside": milestone 1's 5 ms edges inside the mark (a mark 5 ms short at half
  amplitude), **kept by the milestone-1 scenarios and suites (band, single, smoke, full) because they are frozen
  evidence: the smoke check's figures below rest on them** (Ruling 70); S2 scales its edges with its time base
  (Ruling 62). The smoke-check paragraph stays valid and says so in a sentence. The body is unchanged. The
  generator's docstrings state the same.
- [ ] **Step 6: Commit.**

### Task 2. The channelizer follows the detector; the decoder's tracker removed; the oracle's true frequency (Parts 1, 10, 12; D17)

**Files:** create `engine/include/kz4ap/channel_follower.hpp`, `engine/src/channel_follower.cpp`,
`engine/tests/channel_follower_test.cpp`; modify `engine/include/kz4ap/channelizer.hpp`, `engine/src/channelizer.cpp`,
`engine/include/kz4ap/engine.hpp`, `engine/src/engine.cpp`, `engine/include/kz4ap/viterbi/config.hpp`,
`engine/include/kz4ap/viterbi_decoder.hpp`, `engine/src/viterbi_decoder.cpp`, `engine/CMakeLists.txt`,
`bench/src/main.cpp`, `bench/src/viterbi_json.hpp`, `bench/src/viterbi_json.cpp`, `bench/tests/viterbi_json_test.cpp`,
`bench/src/labels.hpp`, `bench/src/labels.cpp`, `bench/tests/labels_test.cpp`,
`bench/tests/bench_run_test.cpp`, `bench/tests/bench_refusal.cmake`, `engine/tests/viterbi_decoder_test.cpp`,
`engine/tests/engine_test.cpp`, `training/tests/data/search-stats/tiny.v1.stats.json`, `tiny.v2.stats.json`,
`docs/signal-processing.md`; delete `engine/include/kz4ap/viterbi/tracker.hpp`, `engine/src/viterbi/tracker.cpp`,
`engine/tests/viterbi/tracker_test.cpp`.

**Interfaces (produced):**
- `void Channelizer::set_center_bin(std::uint32_t id, int center_bin)`: the channel's output from the next block on
  is cut at the new bin; throws for an unknown id.
- `class ChannelFollower { public: ChannelFollower(double channel_rate_hz, double bin_hz); int bin_for(double
  f_det_hz, int current_bin, const Channelizer&) const; void shift(std::span<Sample> block, std::uint64_t
  first_index, double f_det_hz, double f_bin_hz); void reset(); }`. `bin_for` returns `current_bin` while
  |f_det − f_bin| ≤ bin_hz/2 (11.71875 Hz at 23.4375 Hz bins, derived: 192 000/8 192 = 48 000/2 048) and the bin
  nearest f_det otherwise. `shift` multiplies the block by a phase-continuous oscillator at −(f_det − f_bin), one
  complex multiplication per sample, holding f_det over the block (O3), and keeps the *total* demodulating phase
  (the channelizer's center bin plus the fine shift) continuous across a bin move: at a move it adjusts its own phase
  by the channelizer's phase difference between the two centers at that sample (the implementer derives it from
  `Channelizer::process_block`'s per-block phase reference; code map §2, Part 1).
- `struct OracleSegment { double start_s; double frequency_hz; double drift_hz_per_s; double drift_origin_s; };`
  (frequency in Hz from the span's center at `drift_origin_s`); `EngineConfig::oracle_frequency_tracks`:
  `std::vector<std::vector<OracleSegment>>`, one per oracle channel, empty for the constant `oracle_frequencies_hz`.
  An oracle channel's detector frequency at a block starting at t is the last segment with start_s ≤ t evaluated at t.
- `Engine`: for `FrontEnd::Viterbi` only (O2), before each block a channel is re-binned by `bin_for` (through
  `set_center_bin`, effective from the next block) and its block shifted by `shift` before the tap and the decoder;
  the hiss is read at the current bin; `Channel::bin` follows. Other front ends: unchanged.
- `bench::Transmission` gains `double offset_hz = 0` (Hz, the sender's carrier offset), parsed from the labels'
  transmissions (Ruling 72, N1).
- The bench: `--oracle --decoder viterbi` fills `oracle_frequency_tracks` from the labels (O3): one segment for a
  single station (start 0, the label's `freq_offset_hz`, `drift_hz_per_s`, origin `start_s`); for a QSO label, one
  segment per transmission at `freq_offset_hz + offset_hz` of its sender (the first starting at 0); a per-station
  label of a drifting QSO is refused, naming it (its `start_s` is not the QSO's). `--genie frequency` is refused with
  "the frequency genie is removed: oracle channels follow the label's frequency (design Part 1)".
- `ViterbiDecoder`: no tracker, no oscillator, no anchor; `set_frequency_anchor_hz` keeps `Decoder`'s empty default;
  `process` sums pieces of the samples as received. `ViterbiConfig` loses the six `tracker_*` fields and the two
  `genie_frequency_*` fields; `ViterbiGenie` loses `frequency_hz`, `frequency_time_s`, `drift_hz_per_s`; `with_genie`
  keeps fading and fist. `GenieKind { Fist, Fading }`.

- [ ] **Step 1: Failing tests.**
  - `ChannelFollower.AStationAtTheDetectorsFrequencyArrivesAt0Hz`: a steady carrier at +1 234.5 Hz from the span's
    center, at 48 and 192 kHz, f_det = 1 234.5 Hz: after 21.3 ms the output's frequency is within 10⁻³ Hz of 0
    (from the phase advance over 10 s) and its amplitude within 0.1% of the channelizer's gain.
  - `ChannelFollower.MovesTheBinBeyondHalfABin`: f_det from 0 to 60 Hz in 0.01 Hz steps: the bin changes exactly
    where |f_det − f_bin| first exceeds 11.71875 Hz, to the nearest bin.
  - **The phase tests assert continuity, not absolute phase** (Ruling 69): with f_det held from each block's start
    (O3), a drifting or scattered f_det moves the output's phase by design (1.43 × 10⁻³ rad per block at 1 Hz/s;
    0.027 rad per block at 0.2 Hz of scatter; derived), so the reference is computed independently: the input times
    exp(−j·Φ(n)), Φ(n) the channelizer's bin phase at sample n plus the running sum of the held shifts,
    2π·Σ (f_det − f_bin)/r over the samples so far.
  - `ChannelFollower.AStationParkedOnABinBoundary`: a steady station 11.72 Hz from a bin center, f_det scattered
    about it with a standard deviation of 0.2 Hz per block (the detector's steady scatter) for 60 s: the bin moves on
    many blocks; the output equals the reference within 0.01 rad at every sample, and at no bin move does the phase
    step beyond that block's own expected phase change (the rule has no hysteresis; continuity makes the moves
    harmless).
  - `ChannelFollower.PhaseIsContinuousAcrossABinMove`: a carrier drifting at 1 Hz/s for 60 s (through two moves),
    f_det its true frequency at each block's start: the output equals the reference within 0.01 rad at every
    sample, with no step beyond the block's own expected phase change at either move; the same with input blocks of
    1, 7, 32 and 1 000 samples, identical output.
  - `Engine.OracleChannelsFollowTheLabelsDrift`: a station at +5 Hz drifting 1 Hz/s for 30 s, its segment given:
    the tap's samples hold the carrier within 0.05 Hz of 0 Hz in every 1 s window.
  - `Engine.AnOracleQsoSwitchesAtEachOver`: segments 0 Hz until 5 s, then +25 Hz: the shift changes at the first
    block starting at or after 5 s (the block-start rule; 5 s × 1 500 samples/s is not a multiple of 32); the second
    station arrives within 0.05 Hz of 0 Hz from that block on.
  - `Engine.OtherDecodersChannelsAreUnchanged`: for Envelope, Matched and the bank in oracle mode, the tap's blocks
    on a 10 s two-station recording are bit-identical to the fixed-bin channelizer's output.
  - `ViterbiDecoder.DecodesThroughTheEngine` adapted: four stations at +15 dB S₅₀₀, 25 WPM, offsets of 0, +7.3,
    −9.8 and +11.0 Hz from their bins: each text exact (the follower centers them).
  - `Labels.TransmissionsCarryTheirOffset`: a QSO label's transmissions parse with their `offset_hz` (0 when absent).
  - Bench: `--genie frequency` refused with the message above; a single-station label gives one segment and a QSO
    label one per transmission at its sender's offset; a per-station drifting QSO label refused; the field macro's
    guard updated; the JSON round trip has no removed field.
  - The tracker's 25 tests and `ViterbiDecoder.IsTheSearchOfTheRecenteredPiecesWithTheRise` are deleted with the code
    they test (the latter's piece checks reappear in Task 10).
- [ ] **Step 2: Run them; they fail.**
- [ ] **Step 3: Implement.** Rewrite the two fixtures' `config` objects without the removed fields (O13).
- [ ] **Step 4: Run them; they pass,** with every earlier test (`ctest --preset windows`); build and `ctest --preset
  linux` on the Linux machine.
- [ ] **Step 5: Document** (same commit): §7, a paragraph: for the new decoder's channels the channelizer follows
  the detector's frequency (oracle: the label's), the coarse move at more than half a bin (11.72 Hz) and the fine
  phase-continuous shift, so the decoder receives its station near 0 Hz; derived response (a 1 Hz/s drift followed
  1.1 to 1.6 Hz behind; an answering station moves the peak after about 0.7 s). A.7: the exact form (bin rule, the
  phase rule at a move, the shift held per block), the response's derivation from the detector's 1 s average (plan
  D17), O2 and O3. §8d: the tracker's paragraph removed; the decoder reads its channel as received. A.8d: "The carrier
  tracker (new decoder)" removed; "behind the engine" states the oracle's segments. A.10: the eight removed rows.
  A.11: the frequency genie's definition removed; the oracle's frequency defined. Count the body's words (≤ 6 000).
- [ ] **Step 6: Commit.**

### Task 3. The front filter, the envelope samples, the output noise; the D17 keyed-signal helper (Parts 2, 3, 4; Ruling 63)

**Files:** create `engine/include/kz4ap/viterbi/front_filter.hpp`, `engine/src/viterbi/front_filter.cpp`,
`engine/tests/viterbi/front_filter_test.cpp`, `engine/tests/viterbi/keyed_signals.hpp`; modify
`engine/CMakeLists.txt`.

**Interfaces (produced):**
- `std::vector<double> front_filter_taps(double channel_rate_hz, double dot_s, double rise_10_90_s)`: h[j] =
  p(t_c + j/r)/Σp, j = −12 … 12 at r = 1 500 samples/s, p(t) = c(t)(1 − c(t − T)), c(x) = ½ − ½cos(π·clip(x/E, 0,
  1)), E = rise_10_90_s/0.590334, T = dot_s, t_c = (T + E)/2 (Part 2; derived). Throws unless the result has 25 taps
  at the defaults' rate (the count follows from T + E and r: 2·⌊(T + E)·r/2⌋ + 1).
- `struct OutputNoise { double sigma2_per_n0_hz; std::array<double, 7> rho; double w_strong; double w_weak; };`
  (σ_y²/N₀ in Hz; ρ_0 = 1 … ρ_6 at lags of Δ; w_strong = 1/(1 + 2Σρ_m), w_weak = 1/(1 + 2Σρ_m²)).
  `OutputNoise output_noise(std::span<const double> channel_taps, double input_rate_hz, int decimation,
  std::span<const double> front_taps, int channel_samples_per_sample)`: the output noise's power and correlation for
  white input noise of density N₀ through the channel filter and the front filter (derived; O4).
- `struct EnvelopeSample { std::int64_t k; std::complex<double> y_fs; double e_fs; };` (y the front filter's complex
  output, e = |y|; Ruling 72, N4) `class EnvelopeSampler { public: EnvelopeSampler(
  std::vector<double> taps, int channel_samples_per_sample); void push(std::span<const Sample> block,
  std::uint64_t first_index, std::vector<EnvelopeSample>& out); void reset(); }`: keeps the 24-sample complex history
  across blocks; emits y_k and e_k = |y_k| at every channel sample whose absolute index (from the stream's start) is
  a multiple of 4, k = index/4 (t_k = 4k/r; §0). The decoder passes each block's absolute first index.
- `engine/tests/viterbi/keyed_signals.hpp`: `std::vector<Sample> keyed_signal_d17(const std::vector<std::pair<double,
  double>>& intervals, double rate, double duration_s, double freq_hz = 0, double amplitude = 1.0, double
  noise_sigma = 0.0, unsigned seed = 1, double drift_hz_per_s = 0.0, double rise_10_90_s = 0.0025, Placement
  placement = Placement::Start)`, the generator's Task 1 envelope; `keyed_text_d17(text, wpm, …)` as
  `test_signals.hpp`'s `keyed_signal`; `rayleigh_gain(n, rate, spread_hz, seed)` (a Gaussian Doppler spectrum of
  two-sigma width spread_hz, unit mean power).

- [ ] **Step 1: Failing tests** (`front_filter_test.cpp`).
  - `FrontFilter.TapsAreThePulse`: the 25 taps equal 0.00011, 0.00457, 0.01459, 0.02778, 0.04096, 0.05099, 0.05545,
    0.05556 (× 11), and the mirror, within 5 × 10⁻⁶; symmetric; sum 1 within 10⁻¹².
  - `FrontFilter.NoiseBandwidthIs76Hz`: r·Σh² = 76.0 ± 0.05 Hz.
  - `FrontFilter.PowerResponse`: dB relative to 0 Hz, −0.01 (2 Hz), −0.22 (10), −0.50 (15), −0.90 (20), −1.42 (25),
    −2.07 (30), −3.82 (40), −6.31 (50) within ±0.03 dB; −15.5 (70), −15.4 (120), −24.5 (200) within ±0.1 dB.
  - `FrontFilter.ASteadyCarrierGivesItsAmplitude`: e_k = a within 10⁻⁹ relative from 24 channel samples after
    the carrier starts (the causal filter's full length; its end taps are 1.1 × 10⁻⁴).
  - `OutputNoise.MatchesTheDesign` at 48 and 192 kHz: σ_y²/N₀ = 74.48 ± 0.05 Hz; ρ_1 … ρ_6 = 0.878, 0.626, 0.367,
    0.138, 0.014, −0.005 within ±0.002; w_strong = 0.1986 ± 0.0003; w_weak = 0.2754 ± 0.0005; w_strong = Δ·σ_y²/N₀
    within 0.1% (Poisson summation; Part 4).
  - `OutputNoise.MeasuredThroughTheChannelizer` (Part 15, item 1; label `full-sweep`, too long for the default
    suite): 600 s of complex white noise, σ = 0.02 FS, at 48 and 192 kHz, through `Channelizer` and `EnvelopeSampler` (its `y_fs`): Var(y)/N₀ within 1.5% of
    `sigma2_per_n0_hz`, N₀ = σ²/f_s; the lag correlations within 0.01 of ρ_m.
  - `EnvelopeSampler.SplitsDoNotMatter`: blocks of 1, 7, 32 and 1 000 channel samples give identical (k, e_k).
  - `EnvelopeSampler.EveryFourthChannelSample`: k = index/4 for every absolute index divisible by 4, from any first
    index.
  - `KeyedSignals.D17EdgesAtHalfAmplitude`: the helper's dot has its half-amplitude points at on + E/2 and off + E/2.
- [ ] **Step 2: Run them; they fail.** **Step 3: Implement.** **Step 4: They pass.**
- [ ] **Step 5: Document:** none (library code not yet run; prototype plan Ruling 5). Supplies to Task 10: §8d's
  front-filter and sampling sentences; A.8d's exact form (taps formula, B_N, σ_y², ρ_m, w's limits, the power
  response) and the parameter rows.
- [ ] **Step 6: Commit.**

### Task 4. The mark template and the edge term (Part 5)

**Files:** create `engine/include/kz4ap/viterbi/mark_template.hpp`, `engine/src/viterbi/mark_template.cpp`,
`engine/tests/viterbi/mark_template_test.cpp`; modify `engine/CMakeLists.txt`.

**Interfaces:**
- Consumes `front_filter_taps`, `channel_filter_taps(ChannelizerConfig)`, `keyed_signals.hpp`.
- Produces:
  - `std::vector<double> step_response(std::span<const double> channel_taps, double input_rate_hz, int decimation,
    std::span<const double> front_taps, double rise_10_90_s, double offset_s)`: S(τ) at the channel samples for a
    key-down at input time `offset_s` before channel sample 0, both filters causal.
  - `struct TemplateTables { static constexpr int kSpan = 16; std::array<double, kSpan> s; std::array<double, kSpan>
    v; std::array<double, kSpan> v_position; double half_point_s; };` (s_j = S averaged over u uniform on [0, Δ) on
    the input-rate grid, 512 offsets at 192 kHz and 128 at 48 kHz; v_j over u and t_r uniform on [rise_min, rise_max]
    at 79 rise times, each rise aligned to the template at its half-amplitude point; `v_position` over u alone).
    `TemplateTables make_template_tables(std::span<const double> channel_taps, double input_rate_hz, int decimation,
    std::span<const double> front_taps, int channel_samples_per_sample, double edge_rise_s, double rise_min_s,
    double rise_max_s)`. s_j = 1 exactly for j ≥ 16 (the response's finite support). The tables also hold
    `std::array<std::array<double, kSpan>, kSpan> c`, the pair term about the 2.5 ms template (Part 5: "s̄_k stays
    the 2.5 ms template", so v_j is a mean square deviation about s̄_j, not a variance about the mean over rise
    times): c[a][b] = E_r[(m(a, r) − s̄_a)(m(b, r) − s̄_b)], m(j, r) the response at age j of a rise of 10–90% time r
    (aligned at its half-amplitude point) averaged over the edge's position u, and s̄_j the table's s_j.
  - `struct EdgeHistory { std::uint16_t recent = 0; bool key_down_before = false; };` bit j set when a claimed edge
    lies j samples back (bit 0: this sample); edges alternate, so the bits and the key state 16 samples back give
    every sign. `void advance(EdgeHistory&, bool edge_now)` shifts by one sample (an edge leaving bit 15 updates
    `key_down_before`). `struct TemplateValue { double s; double v; };` `TemplateValue template_value(const
    EdgeHistory&, const TemplateTables&)`: s = (key_down_before ? 1 : 0) + Σ_edges ±(S̄(age) − [1 if age ≥ 16]) in
    the implementer's exact form; v exact **given the assumption** that each edge's position u_e within its sample
    is independent and uniform on [0, Δ) (derived under it; fix rounds 1 and 4): with each edge e of age a_e and
    sign σ_e (+1 key-down, −1 key-up), and one rise time r shared by the transmitter's edges, the mean square
    deviation of the combined template about the combined 2.5 ms template, over (u, r) jointly, is
    v = Σ_e v_{a_e} + 2Σ_{e<e'} σ_e σ_e' c[a_e][a_e'] (given r the positions are independent, so a pair's cross
    term is the product of their position-averaged deviations, averaged over r). The pair table is the
    per-mark-length (or per-gap-length) precomputation wherever two edges lie within 16 samples; one edge alone
    gives v_j.
  - `std::int64_t claimed_edge_sample(double keyed_time_s, double sample_interval_s)`: the first sample at or after
    the keyed time, ⌈t/Δ⌉ with t_k = kΔ (owner, 2026-10-25).

- [ ] **Step 1: Failing tests** (192 kHz unless stated).
  - `MarkTemplate.MatchesTheDesign`: s_5 … s_13 = 0.0505, 0.2126, 0.4334, 0.6590, 0.8661, 0.9825, 1.0023, 0.9999,
    0.9999 within ±0.0002; s_14, s_15 within 10⁻⁴ of 1; s_0 … s_4 = 0.00000, 0.00001, 0.00016, −0.00064, −0.00080
    within ±0.00002. v_5 … v_13 = 1.0 × 10⁻³, 3.6 × 10⁻³, 4.2 × 10⁻³, 4.2 × 10⁻³, 2.6 × 10⁻³, 3.4 × 10⁻⁴,
    2.7 × 10⁻⁶, 1.4 × 10⁻⁷, 2.9 × 10⁻⁹ within ±6% relative; v_0 … v_4 = 8 × 10⁻¹⁴, 3.1 × 10⁻¹⁰, 7.8 × 10⁻⁹,
    5.8 × 10⁻⁷, 2.8 × 10⁻⁵ within ±12%; v_14, v_15 < 3 × 10⁻¹¹. `v_position` 5 … 13 = 9.5 × 10⁻⁴, 3.6 × 10⁻³,
    4.2 × 10⁻³, 4.2 × 10⁻³, 2.6 × 10⁻³, 2.7 × 10⁻⁴, 6.4 × 10⁻⁷, 6.5 × 10⁻⁸, 3.2 × 10⁻⁹ within ±6%.
  - `MarkTemplate.StepResponseFigures`: half amplitude 20.79 ± 0.02 ms after key-down; 10–90% rise 10.03 ± 0.02 ms;
    1–99% rise 14.73 ± 0.03 ms; S = 1 within 10⁻¹² from 16 samples after any offset.
  - `MarkTemplate.DotPeaks`: a 12 ms dot (100 WPM) peaks at 0.900 ± 0.002, a 15 ms dot (80 WPM) at 0.984 ± 0.002,
    a 48 ms dot (25 WPM) at 1.000 ± 0.001.
  - `MarkTemplate.MatchesTheChainOnKeyedCarriers` (Part 15, item 1): `keyed_signal_d17` noise-free, a 48 ms mark and
    a 12 ms dot at 8 edge positions u = (i + ½)Δ/8, through `Channelizer` and `EnvelopeSampler`: each e_k equals
    S(jΔ + u) (fall: its mirror) within 10⁻⁴ of full. This also fixes the channelizer's time reference: if channel
    sample n is not input time n/r, the test exposes the offset and `step_response` includes it.
  - `MarkTemplate.OtherRisesMisfit`: rises of 0.8 and 4.7 ms (10–90%), aligned at half amplitude: the largest
    |difference| from the 2.5 ms template is 0.78–1.56% and 1.92–3.83% of full (12 ms dot and 48 ms mark) within
    ±0.05 percentage points.
  - `MarkTemplate.SixteenEdgesInTheHistory`: alternating 1-sample marks and gaps for 40 samples: s and v equal the
    sum over every edge in the last 16 samples computed directly; an edge 16 samples back adds exactly 0 to the
    fall and 1 to the rise.
  - `MarkTemplate.OverlappingEdgesAreExact`: a 100 WPM dot claimed as 4 and as 5 samples, two dots with a 1-sample
    gap, and three edges within 16 samples: v from `template_value` equals the mean square deviation of the
    combined template about the combined 2.5 ms template, computed directly over a 64 × 64 grid of the edges'
    positions (64³ for three) and the 79 rise times, within 1% relative wherever v > 10⁻⁸; with every rise at
    2.5 ms the cross terms vanish.
  - `MarkTemplate.TheFallIsExactlyZeroFrom16Samples`: after a key-up, s_j ≥ 0.01 through j = 10, s_j ≠ 0 through
    j = 15 and s_j = 0 exactly from j = 16 (a long mark).
  - `MarkTemplate.ClaimedEdgeIsTheFirstSampleAtOrAfterTheKeyedEdge`: keyed times kΔ, kΔ + 10⁻⁹ s and (k + 1)Δ − 10⁻⁹ s
    give k, k + 1 and k + 1.
  - `MarkTemplate.At48kHz`: the 48 kHz tables (1 025 channel taps) differ from 192 kHz's by at most 0.01 of full in
    s_j (heuristic bound; the values are written to the test's log for A.8d).
  - `MarkTemplate.BuildsQuickly` (label `measurement`, out of the default presets; Ruling 72, N24):
    `make_template_tables` under 1 s on the desktop (heuristic; the time written to the log).
- [ ] **Step 2: Run them; they fail.** **Step 3: Implement.** **Step 4: They pass.**
- [ ] **Step 5: Document:** none (not yet run). Supplies to Task 10: §8d's template paragraph; A.8d's S(τ), s_j, v_j
  definitions and table (both rates), the sample convention, the exact edge term of overlapping edges, the edge history, the misfit figures and the timing
  reference (Part 11).
- [ ] **Step 6: Commit.**

### Task 5. The evidence per sample, the weight w and its generator, the systematic term (Parts 4, 9; Rulings 60, 65)

**Files:** modify `engine/include/kz4ap/viterbi/observation.hpp`, `engine/src/viterbi/observation.cpp`,
`engine/tests/viterbi/observation_test.cpp`; create `engine/include/kz4ap/viterbi/weight.hpp`,
`engine/src/viterbi/weight_table.cpp` (generated), `engine/include/kz4ap/viterbi/systematic.hpp`,
`engine/src/viterbi/systematic.cpp`, `engine/tests/viterbi/weight_test.cpp`, `engine/tests/viterbi/systematic_test.cpp`,
`bench/src/viterbi_tables_main.cpp`; modify `engine/CMakeLists.txt`, `bench/CMakeLists.txt`.

**Interfaces:**
- Consumes `OutputNoise`, `TemplateTables`, `Channelizer`, `front_filter_taps`.
- **The template's sign** (Ruling 72, N5): s_3 and s_4 are negative (−0.00064, −0.00080, the channel filter's
  precursor); Λ is even in b, so every function here and in Task 9 uses |s| (b = |s|·a/√V′; ln of |s| in Task 9's
  offsets).
- **The generated tables' build** (Ruling 72, N6): `engine/CMakeLists.txt` gains an object library
  `kz4ap_engine_core` holding every engine source except the generated tables; `kz4ap_engine` is that library plus
  the generated sources; `kz4ap-viterbi-tables` links `kz4ap_engine_core` only, so it builds before any table exists.
  Each generated table is committed as source by the task that adds it (`weight_table.cpp` here, `omega_bar_table.cpp`
  in Task 7).
- Produces:
  - `struct SampleEvidence { double score_nats; Moments amplitude; double mode_a_fs; };`
    `SampleEvidence sample_evidence(double e_fs, double s, double mu_fs, double p_fs2, double v_prime_fs2, double w,
    int order, std::optional<double> start_a_fs)`: score = ln ∫ exp(w·Λ(a)) N(a; μ, P) da with Λ(a) = −b² +
    ln I₀(2bz), b = s·a/√V′, z = e/√V′ (nats); the posterior's mean and variance; the mode for the next sample's
    Newton start. Adaptive Gauss–Hermite (Ruling 60) on the tempered integrand; V′ is not divided by w. s = 0 returns
    {0, {μ, P}, start}. The block-form functions stay until Task 10 removes them (the old search uses them).
  - `double sample_weight(double q)`: w from the 129-node table on ξ = q/(1 + q), linear interpolation, q clamped to
    [10⁻⁴, 10⁴] (−40 to +40 dB SNR in the filter's 74.48 Hz), end nodes the derived limits.
  - `class CarrierWindow { public: CarrierWindow(int back_samples, int ahead_samples); void push(double e2_fs2);
    std::optional<double> next_peak_fs2(); void finish(); }`: for the oldest sample not yet searched, the largest e²
    over it, the 10 before and the 10 after (fewer at the stream's ends); a monotonic deque.
  - `double v_prime_fs2(double sigma2_fs2, double window_peak_fs2, double e_fs, double s, double v, double floor)`:
    σ_y² + C²(f²s² + v), C² = max(window peak, e²/s²) where s ≠ 0, the window peak where s = 0.
  - `double score_cap_nats(double w, double s, double v, double floor)` = w·s²/(f²s² + v).
  - `kz4ap-viterbi-tables weight --out FILE --seeds 1,2 --samples 4194304`: complex white noise through the 192 kHz
    `Channelizer` and the front filter, every 4th sample (the decided chain's correlation), one noise sequence per
    seed shared by every node; for each node at q = ξ/(1 − ξ), ψ_k = ∂Λ_k/∂a at the true a for a steady carrier at
    full template, w = Var(ψ)/(Var(ψ) + 2Σ_{m=1}^{11} Cov(ψ_k, ψ_{k+m})), the mean of the two seeds; end nodes from
    `output_noise`; it also evaluates the curve at the midpoints and prints the interpolation error; writes a C++
    source with the 129 values and a header (command, seeds, sample count, chain, error).

- [ ] **Step 1: Failing tests.**
  - `Evidence.ScoreMatchesADenseIntegral` (Part 15, item 1; D4): within 0.01 nats of a 20 001-point integral over
    a ≥ 0, for z ∈ {0, 0.5, 1, 2, 5, 20, 100}, s ∈ {−0.0008, 0.0505, 0.4334, 1} (a negative s equals |s|), b at μ ∈ {0.1, 1, 3, 10, 30}, √P/μ ∈ {0.01,
    0.1, 0.5, 1, 3} (the broad-prior region included), w ∈ {0.1986, 0.23, 0.2754, 1} (w = 1, the untempered form, keeps the block form's check after Task 10); the posterior mean within 10⁻³
    relative and the variance within 1% relative of the dense integral's.
  - `Evidence.ZeroTemplateAddsNothing`; `Evidence.UnitWeightIsTodaysBlockEvidence`: w = 1 equals
    `block_evidence(e, s, μ, P, V′, order, start)` within 10⁻¹² nats wherever both use the same quadrature (outside
    the block form's broad-prior regime) and s > 0 (deleted with `block_evidence` in Task 10).
  - `Evidence.StaysFiniteAtTheExtremes`: q up to 2.1 × 10⁹ (+85 dB S₅₀₀), P down to 10⁻¹² μ² and up to 10⁴ μ².
  - `Weight.MatchesTheCurve`: q ≤ 10⁻⁴ → 0.2754 ± 0.0002; q ≥ 10⁴ → 0.1986 ± 0.0002; at −20, −10, −5, 0, +3, +6, +10,
    +15, +20, +30 dB SNR (in 74.48 Hz): 0.273, 0.259, 0.240, 0.217, 0.208, 0.203, 0.200, 0.199, 0.1987, 0.1986
    within ±0.0015 (Monte Carlo ±0.0006 and rounding); non-increasing in q within ±0.0006.
  - `Weight.EndNodesAreTheChainsLimits`: the table's ends equal `output_noise(...)`'s w_weak and w_strong at 192 kHz
    within 10⁻⁶.
  - `Systematic.WindowIsTheLargestPowerOver21Samples`: on 10 s of random e², the window equals a brute-force max
    over [k − 10, k + 10] at every k, including the stream's ends, for input in blocks of 1, 7 and 32.
  - `Systematic.OwnPartOnEverySampleWithATemplate`: s = 0.005 and e² above the window's peak × s²: C² = e²/s²; s = 0:
    the window's peak; a space beyond the tail (s = 0, v = 0): V′ = σ_y².
  - `Systematic.CapsMatchTheDesign`: w = 0.1986, f = 0.02, the 192 kHz template: caps at j = 5 … 11 of 0.50, 2.51,
    8.65, 19.8, 51.6, 265 and 493 nats within ±1% (or ±0.01 nats); the plateau 496.5 ± 0.5 nats.
  - `Systematic.EvidenceNeverExceedsTheCap`: 10⁵ random draws of e, s, μ, P, w with C² ≥ e²/s²: score ≤ cap +
    0.01 nats.
- [ ] **Step 2: Run them; they fail.**
- [ ] **Step 3: Implement.** Measure the broad-prior region first (O9): run `Evidence.ScoreMatchesADenseIntegral`
  with the regime off for the per-sample score; if it passes, the per-sample score has no regime, else give it a J
  table with a w axis over 0.1986 to 0.2754, nodes added until the test passes. The block form's regime and tables
  stay untouched until Task 10 removes the block form (the old search calls it until then). Run the generator (about a minute; record its command and output
  in the generated file's header and the commit message).
- [ ] **Step 4: Run them; they pass,** with every earlier test.
- [ ] **Step 5: Document:** none (not yet run). Supplies to Task 10: §8d's evidence and systematic paragraphs; A.8d's
  exact score, the quadrature (Ruling 60), the regime's outcome (O9), w's curve, table and provenance, V′, the
  window, the caps (Part 9), and the parameter rows.
- [ ] **Step 6: Commit** (with `weight_table.cpp`).

### Task 6. The amplitude prediction (Part 6; Bell's decay for item 16)

**Files:** create `engine/include/kz4ap/viterbi/amplitude.hpp`, `engine/src/viterbi/amplitude.cpp`,
`engine/tests/viterbi/amplitude_test.cpp`; modify `engine/CMakeLists.txt`.

**Interfaces (produced):**
- `struct Amplitude { double mu_fs; double p_fs2; std::int64_t t_m_sample; };`
- `class FadingTables { public: FadingTables(double spread_hz, double sample_interval_s); double rho(std::int64_t
  lag) const; double rho_a(std::int64_t lag) const; }`: ρ = exp(−π²f_D²(lagΔ)²/2); ρ_a = (π/4)(₂F₁(−½, −½; 1; ρ²) −
  1)/(1 − π/4) (derived), tabulated per lag until ρ < 10⁻¹², then 0.
- `struct Prediction { double mu_fs; double p_fs2; };` `Prediction predict_amplitude(const Amplitude&, std::int64_t
  k, double omega_bar_fs2, const FadingTables&)`: μ = ρ_a μ + (1 − ρ_a)(√π/2)√Ω̄, P = ρ_a²P + ½(1 − ρ²)Ω̄, lag =
  k − t_m (owner, 2026-10-22 and 2026-10-23).
- `Prediction predict_amplitude_bell(const Amplitude&, std::int64_t k, double omega_bar_fs2, double dot_s, const
  FadingTables&, double sample_interval_s)` (D20: the space's length in dots and the re-entry flag, which only
  Bell's Q used, are gone): across a space μ decays by
  Φ^(lagΔ/5 ms), Φ = 10^(−2/(22.4·T_dot,ms)), never below 10^(−5/20) of the last estimate; at lag 1 inside a mark it
  equals `predict_amplitude`. The variance follows the decided rule ρ_a²P + ½(1 − ρ²)Ω̄ exactly, and no Q is added
  (D20).

- [ ] **Step 1: Failing tests.**
  - `Amplitude.FadingValuesAt2Hz`: at lags 1, 18, 54, 126 samples (2.667, 48, 144, 336 ms): ρ = 0.99986, 0.9555,
    0.664, 0.108; ρ_a = 0.99967, 0.902, 0.416, 0.011; √((1 − ρ²)/2) = 0.012, 0.208, 0.529, 0.703; each within half
    a unit of its last printed digit.
  - `Amplitude.RhoAIsTheRayleighEnvelopeCorrelation`: against 10⁶ simulated pairs of Rayleigh envelopes with complex
    correlation ρ = 0.3, 0.6, 0.9: within 0.003.
  - `Amplitude.PredictionIsTheOwnersRule`: the formulas to 10⁻¹² relative; √π/2 = 0.886227; lag 0 unchanged; lag
    10⁴ gives μ = 0.886√Ω̄ and P = Ω̄/2 within 10⁻⁹ relative.
  - `Amplitude.BellDecaysAbout036DbPerDot`: −40/(22.4 × 5) = −0.357 dB of amplitude per dot ± 0.001 dB at 5.2, 25 and
    100 WPM; the floor at −5 dB of amplitude; lag 1 inside a mark equals the decided rule; across spaces of 1, 18,
    54 and 126 samples the variance equals `predict_amplitude`'s P to 10⁻¹² relative (D20: the decided rule, no Q),
    only μ differing.
- [ ] **Step 2: Run them; they fail.** **Step 3: Implement.** **Step 4: They pass.**
- [ ] **Step 5: Document:** none (not yet run). Supplies to Task 10: §8d's amplitude paragraph; A.8d's prediction,
  its derivation and limits (Part 6), the in-mark limitation, ρ and ρ_a tables; Bell's form for Task 12.
- [ ] **Step 6: Commit.**

### Task 7. The long-run amplitude: Ω̂, ν and the default Ω̄ with its generator (Part 7; Ruling 65)

**Files:** create `engine/include/kz4ap/viterbi/long_run.hpp`, `engine/src/viterbi/long_run.cpp`,
`engine/src/viterbi/omega_bar_table.cpp` (generated), `engine/tests/viterbi/long_run_test.cpp`; modify
`bench/src/viterbi_tables_main.cpp`, `engine/CMakeLists.txt`.

**Interfaces (produced):**
- `class LongRun { public: LongRun(double spread_hz, int bin_samples, int bins_kept, double sample_interval_s); void
  restart(); void add(double s, double e_fs, double sigma2_fs2); void at_sample(std::int64_t k); double
  omega_hat_fs2() const; double nu() const; double sum_s4() const; double sum_s2() const; }`: Ω̂ = Σs²(e² − σ_y²)/Σs⁴
  over the samples with s ≠ 0 since the restart (owner, 2026-10-25); ν's sums (ΣW and the double sum) run over the
  whole over, only the cross terms of a new bin using the last 10 bins; W_i = Σs⁴ per bin of 16 samples on the channel's
  grid (k mod 16), the last 10 closed bins kept, ν = (ΣW)²/ΣΣ W_i W_j ρ²(t_i − t_j) with ρ² = exp(−π²f_D²τ²) at the
  bins' centers, the open bin included (O7); `at_sample` closes a bin when k crosses a multiple of 16.
- `double omega_bar_closed_form(double nu, double omega_hat_fs2, double sigma2_fs2, double lo_db, double hi_db)`:
  the posterior mean of the inverse gamma of shape ν and scale β = ν·max(Ω̂, 0) truncated to [Ω_lo, Ω_hi], Ω = 10^(L/10)
  N₀·500 Hz, so Ω_lo = 0.2123 σ_y², Ω_hi = 2.123 × 10⁹ σ_y² at 74.48 Hz (derived). At β = 0 it is the exact mean
  under the truncated posterior ∝ Ω^(−ν) on [Ω_lo, Ω_hi] (in dΩ/Ω): Ω_lo·ν/(ν − 1) only where ν − 1 ≫
  1/ln(Ω_hi/Ω_lo) = 0.043, Ω_lo·ln(Ω_hi/Ω_lo) = 23.0 Ω_lo at ν = 1, and the exact form between them (about 21 Ω_lo
  at ν = 1.01, not 101). Computed by quadrature in ln Ω (the tool and the tests).
- `class OmegaBarTable { public: static OmegaBarTable compiled(); static OmegaBarTable load(const std::string& path);
  double lo_db() const; double hi_db() const; double omega_bar_fs2(double nu, double omega_hat_fs2, double sigma2_fs2)
  const; }`. The table records the prior's bounds it was built for (the compiled one −15 and +85 dB S₅₀₀);
  `make_viterbi_shared` refuses a configuration whose `level_min_db`/`level_max_db` differ from the table's, naming
  both, unless a table for those bounds is loaded (`EngineConfig::viterbi_omega_bar_default_table`, a path; bench
  `--omega-bar-default-table FILE`); M7's changed bounds run with such a table (Ruling 72, N14).
  on the generated table: υ = 1/ν at 31 nodes uniform in p = 1 − √(1 − υ), x = ln(β/Ω_hi) at 256 nodes on [−40, +8],
  bilinear, the exact β = 0 value (above) as a row over υ below x = −40, x clamped at +8 (the posterior at the prior's top); the
  generator adds nodes until O8's 0.1% holds.
- `kz4ap-viterbi-tables omega-bar [--lo-db L --hi-db H] --out FILE`: writes the table (C++ source for the compiled
  default; with other bounds, a binary file with its bounds in the header, into `build/`, not committed) and its
  measured error on 10 000 random points.

- [ ] **Step 1: Failing tests.**
  - `LongRun.OmegaHatIsOrdinaryLeastSquares`: on given (s, e) the formula to 10⁻¹² relative; samples with s = 0 change
    nothing.
  - `LongRun.OmegaHatIsUnbiased`: 20 000 trials of a steady carrier on the template of "PARIS" at 25 WPM in complex
    Gaussian noise with the chain's ρ_m (σ_y² = 1): mean Ω̂ = a² within 3 standard errors at 0 and +10 dB S₅₀₀.
  - `LongRun.PhysicalFloor`: −σ_y²Σs²/Σs⁴ = −2.09, −1.30 and −1.05 σ_y² (±0.01) for a 100 WPM dot claimed as 4
    samples with its tail, "PARIS" at 100 WPM and "PARIS" at 25 WPM.
  - `LongRun.BinnedNuMatchesTheExactSum` (Part 15, item 1): "PARIS" once, twice and four times at 8, 25 and 100 WPM,
    the template's s⁴ as weights, after the last tail: |ν_binned/ν_exact − 1| ≤ 2.5%; "PARIS" at 25 WPM gives
    ν = 6.55 ± 0.05.
  - `LongRun.NuOnKeyDownTimes`: contiguous samples with s = 1: ν = 2.17 after 0.5 s and 7.42 after 2 s at 2 Hz
    within 0.7% (the exact sample sum, recomputed in the review of this plan; the binning adds up to 0.6%); on the
    key-down sample times of "PARIS" repeated at 25 WPM (s = 1 on key-down samples, 0 elsewhere) the value is
    reported beside the design's 2.10 and 6.72 (derived in scratch on 25 WPM key-down times not in the repository)
    and asserted within 3% of them; keeping 10 bins changes ν by under 0.01% against keeping all.
  - `LongRun.AnHourOfKeyDownStaysExact`: one hour of s = 1 samples: Ω̂ of a constant e² is exact within 10⁻¹²
    relative and ν within 10⁻⁹ relative of the closed sum (Review focus 4).
  - `OmegaBar.TableMatchesTheClosedForm`: 10 000 random points, ν log-uniform on [1, 1 000], Ω̂/σ_y² uniform in
    asinh on [−5, 10⁹]: relative error ≤ 0.1% (O8).
  - `OmegaBar.BoundsAreChecked`: a configuration at −5 and +60 dB S₅₀₀ is refused without a matching table, naming
    the bounds; with a table generated for them, Ω̄ matches the closed form at those bounds within 0.1%.
  - `OmegaBar.KnownValues`: Ω̂ = 10³ σ_y²: Ω̄/Ω̂ = 3.00, 2.00, 1.25 at ν = 1.5, 2, 5 within 0.5%; ν = 1: Ω̄/Ω̂ = 14.4
    at +20 dB S₅₀₀ and 12.1 at +30 dB S₅₀₀ within ±0.1; Ω̂ ≤ 0: Ω_lo·ν/(ν − 1) at ν = 2 and 5, 23.0 Ω_lo at ν = 1,
    and the quadrature's exact value at ν = 1.01 (about 21 Ω_lo), each within 0.1%; Ω̂ < 0 gives Ω̂ = 0's value.
- [ ] **Step 2: Run them; they fail.** **Step 3: Implement;** run the generator. **Step 4: They pass.**
- [ ] **Step 5: Document:** none (not yet run). Supplies to Task 10: §8d's long-run paragraph; A.8d's Ω̂, its floor,
  ν (Satterthwaite, binning error, O7), the closed form, the table's grid and measured error (O8), the clipping, the
  restarts, and the parameter rows.
- [ ] **Step 6: Commit** (with `omega_bar_table.cpp`).

### Task 8. The noise-in-Ω̄ variant's sums and table (Part 7, item 17; Ruling 65)

**Files:** modify `engine/include/kz4ap/viterbi/long_run.hpp`, `engine/src/viterbi/long_run.cpp`,
`engine/tests/viterbi/long_run_test.cpp`, `bench/src/viterbi_tables_main.cpp`.

**Interfaces (produced):**
- `class NoiseVarianceSums { public: void restart(); void add(double s, const std::array<double, 7>& rho); double
  a2() const; double b2() const; }`: at every sample (s = 0 included, for the history), with s_{k−m} the story's own
  template at its previous 6 samples (0 before the restart): a₂ += s²(s² + 2Σ_{m=1…6}ρ_m²s_{k−m}²), b₂ += s³(s³ +
  2Σρ_m s_{k−m}³) (owner, 2026-10-25: exact sums over 6 lags).
- `double omega_bar_variant_direct(double nu, double chi, double zeta, double lambda)`: Ω̄/σ_y² by the direct integral
  over the realized power P_r (Part 7, "computed in scratch"), with Ω_lo,hi as in Task 7.
- `class OmegaBarVariantTable { public: struct Axes { /* the four axes' ranges and node counts */ };
  OmegaBarVariantTable(const Axes&, std::vector<float> values, std::vector<float> minimum_chi); /* for tests */
  static OmegaBarVariantTable load(const std::string& path); double
  omega_bar_fs2(double nu, double omega_hat_fs2, double a2, double b2, double sum_s4, double sigma2_fs2, const
  OmegaBarTable& fallback) const; double minimum_chi(double nu, double zeta, double lambda) const; }`: SE₀ =
  σ_y²√a₂/Σs⁴, χ = Ω̂/SE₀, ζ = SE₀/σ_y², λ = 2b₂/(Σs⁴·√a₂); axes 1/ν at 31 nodes (υ = 1 − (1 − p)²), asinh χ on
  [asinh(−8), asinh(10⁴)] at 85, ln ζ on [ln 0.003, ln 400] at 42, ln λ on [ln 10⁻³, ln 1 120] at 29; float32,
  layout [υ][χ][ζ][λ]; multilinear over 16 corners; λ < 10⁻³ read at 10⁻³; χ > 10⁴ → the fallback; χ < −8 → −8; ζ
  clamped to its axis (and counted, Task 11). `minimum_chi` reads a side table (31 × 42 × 29) of the χ where Ω̄
  stops falling (the Gaussian artifact, for item 17's count). `load` refuses a file whose header (magic, version,
  axes, prior bounds, σ_y²/N₀) differs from this build's.
- `kz4ap-viterbi-tables omega-bar-variant --out build/viterbi-tables/omega-bar-variant-v1.bin --threads N`.

- [ ] **Step 1: Failing tests.**
  - `NoiseVariance.RunningSumsAreTheDoubleSums`: on a 4-sample mark, a 25 WPM dot and "PARIS" at 100 WPM: a₂ =
    ΣΣs_j²s_l²ρ²_{j−l} and b₂ = ΣΣs_j³s_l³ρ_{j−l} over 6 lags within 10⁻¹² relative; against all 11 lags within 0.01%.
  - `NoiseVariance.MatchesSimulation` (Part 15, item 1): 20 000 trials each, the chain's ρ_m, a 25 WPM dot and "PARIS"
    at 25 WPM, steady and 2 Hz Rayleigh, noise alone and −10, 0, +10 dB S₅₀₀: E[N²]/(A + B·ΩG) in [0.96, 1.04].
  - `OmegaBarVariant.DirectIntegralLimits`: the ratio to the closed form at χ = 100, 10³, 10⁴ in [0.9986, 1.0000],
    [0.9998, 1.0000], 1.0000 ± 10⁻⁴ at ν = 1, 2, 6.7, 50 and ζ = 0.02, 0.3, 1.6 (κ = 2.6).
  - `OmegaBarVariant.DesignValuesAtZero`: Ω̂ = 0, κ = 2.6: ν = 1, ζ = 1.6, 0.3, 0.02 → 6.5, 1.1, −1.2 dB S₅₀₀; ν = 6.7
    → −4.9, −11.0, −14.2 dB S₅₀₀; each ±0.1 dB.
  - `OmegaBarVariant.LookupInterpolates`: on a small table built by the test constructor (3 nodes per axis) the
    lookup equals the
    multilinear formula; the clamps and the fallback act as stated (default suite, fast).
  - `OmegaBarVariant.TableMatchesTheDirectIntegral` (label `needs-table`, skipped with a message when the file is
    absent): 400 points drawn from Part 7's two halves: 99th percentile of |relative error| ≤ 1%, largest ≤ 3%
    (design: 0.87% and 2.5%); `minimum_chi` between −0.5 and −3.7 on Part 7's cases.
- [ ] **Step 2: Run them; they fail.** **Step 3: Implement;** generate the table on the desktop into `build/` (not
  committed; record the command, its time and the error report in the commit message). **Step 4: They pass.**
- [ ] **Step 5: Document:** none (the variant is not run until Task 12). Supplies to Task 12: A.8d's variant model,
  A + B·ΩG, the sums, the table and its error, the artifact.
- [ ] **Step 6: Commit.**

### Task 9. The newborn integral on a grid (Part 8 as amended; D21, D22)

**Files:** create `engine/include/kz4ap/viterbi/newborn.hpp`, `engine/src/viterbi/newborn.cpp`,
`engine/tests/viterbi/newborn_test.cpp`; modify `engine/CMakeLists.txt`, `CMakePresets.json` (the `measurement`
label: excluded from the `windows` and `linux` test presets, run by new `windows-measurement` and
`linux-measurement` presets; Ruling 72, N19).

**What it computes** (Part 8; the study `newborn-integral-candidates.md`, §§2–8). A newborn's score at sample k is
ln Z_k − ln Z_(k−1), Z_k = ∫ exp(Σ_(j≤k) w_j(a)·Λ_j(a)) p₀(u) du, p₀ flat in u = ln a over [u_lo, u_hi] (−15 to
+85 dB S₅₀₀, 11.51 nepers; bounds from N₀ at birth, O10), Λ_j(a) = −b_j² + ln I₀(2b_j z_j), b_j = s_j a/√V′_j,
z_j = e_j/√V′_j (Part 4), and w_j(a) = w(q) from Part 4's curve at q = a²s_j²/σ_y² (D21). The running sums, Newton's
method, Gauss–Hermite and the newborn's two regime weights (0.275 and 0.1986) are gone; Ruling 61's crossover is moot.

**Interfaces:**
- Consumes `ln_i0`, the weight table of Task 5 (`sample_weight`'s ξ table), `TemplateTables`, `Amplitude`.
- Produces `class NewbornIntegral { public: NewbornIntegral(const NewbornTables&, double u_lo, double u_hi, int
  points, double live_range_nats); void add(double e_fs, double s, double v_prime_fs2, double sigma2_fs2); double
  last_score_nats() const; double mean_u() const; double sigma_u() const; Amplitude to_gaussian(std::int64_t k)
  const; double narrowest_sigma_u() const; double deepest_revival_nats() const; bool should_switch(double
  switch_sigma_u, bool last_scored_sample_of_first_mark) const; }` and `class NewbornTables { public:
  NewbornTables(const OutputNoise&, double sigma2_per_n0_hz, std::optional<double> fixed_weight); }`, built once in
  `ViterbiShared` (the w table from `sample_weight`, or the constant w when `fixed_weight` is set, Task 12).
  `should_switch` returns true when σ_u < `switch_sigma_u` or at the last scored sample of the first mark's
  template (j = 15 after the claimed key-up, which the caller knows from its `EdgeHistory`); Task 10's search calls
  it after each `add` (Ruling 72, N11). The offsets use |s| (N5).
  - **The grid (D22):** N = 128 points u_i even over [u_lo, u_hi], spacing h = 11.51/127 = 0.0906 nepers (0.79 dB);
    each holds ln of the unnormalized belief, F_i = Σ_j w_j(a_i)Λ_j(a_i). Z_k = h·Σ_i (ends halved) exp F_i
    (trapezoid), computed as a log-sum-exp. 128 is heuristic with a derived bound and a measured check: the trapezoid
    rule's relative error on a belief of width σ is about 2·exp(−2π²σ²/h²) (Poisson summation), 4 × 10⁻⁷ at
    σ = 0.08, the narrowest measured at handover.
  - **The live range (D22):** only points within 80 nats of the largest F_i are evaluated; a point more than 80 nats
    below it is dropped for good, so the range [lo, hi] only shrinks (heuristic: about twice the deepest revival
    measured, 41.5 nats).
  - **The tables (D22; derived, their error measured):** on a grid even in u, ln x_i = ln(2s z/√V′) + u_i and
    ln q_i = 2u_i + ln(s²/σ_y²), so ln I₀ and w at every point are two fixed tables shifted by one offset per sample,
    with one interpolation fraction for all points. ln I₀(x) = x − ½ln x + G(ln x), G tabulated at steps of h/4 =
    0.023 nepers of ln x over [−20, +165], 4-point Lagrange interpolation; where every point has x < 10⁻³,
    ln I₀ = x²/4. w tabulated against ln q at steps of 2h = 0.18, linear interpolation, built from Task 5's 129-node
    ξ table (heuristic steps, measured).
  - **The switch** (owner, 2026-10-24 and 2026-10-25): when the grid's own posterior standard deviation in u falls
    below 0.1, or at the latest at the last sample of the first mark's template with s ≠ 0, j = 15 after the claimed
    key-up; then, from the grid's mean û and σ_u, μ = e^(û + σ_u²/2), P = μ²(e^(σ_u²) − 1).
  - **The state per newborn:** the 128 values F_i (about 1 KB in double precision), the live range [lo, hi], ln Z,
    and the two counters (narrowest σ_u at handover, deepest revival). Held by the story through a handle; when a
    newborn's story branches inside its first mark, each successor gets its own copy (about 60 ns per copy, estimated;
    copy on write, so successors that do not score before they are pruned never copy). Its size is the reason the
    `Story` keeps a handle rather than the grid inline.

- [ ] **Step 1: Failing tests** (the study's §8, with its values).
  - `Newborn.MatchesADenseIntegral`: sequences as the study's (the chain's correlated noise at the samples; noise and
    −10, −5, 0, +5, +10, +20, +40 dB S₅₀₀; dots and dashes at 25 and 50 WPM; at least 200 per case; the story the
    right one): every per-sample score within **0.005 nats** of a 4 001-point trapezoid of the same integrand with
    direct ln I₀ and w (measured worst 0.0030 + 0.0003), every first-mark total within **0.015 nats** (measured worst
    0.0093 + 0.0037).
  - `Newborn.DenseReferenceConverged` (a check of the test itself): 4 001 against 16 001 points within 10⁻⁵ nats per
    sample (measured 1.3 × 10⁻⁶).
  - `Newborn.LiveRangeLosesNothing`: the live range at 80 nats equals the full 128-point grid within 10⁻⁹ nats per
    sample on the same set and on the misaligned stress cases (a 25 WPM dash at +20 and +40 dB S₅₀₀ claimed 4 samples
    early and late), where points revive from 41.5 nats down; `deepest_revival_nats` reports 41.5 ± 1 there.
  - `Newborn.TablesMatchDirectEvaluation`: the shifted G and w tables against `ln_i0` and the ξ table within
    **5 × 10⁻⁴ nats per sample** and **0.005 nats per first mark** (measured 3.1 × 10⁻⁴ and 0.0037); G against
    `ln_i0` within 10⁻⁶ nats over x from 10⁻³ to 10⁶.
  - `Newborn.StaysBoundedOnNoise` (Review focus 1; replaces "every mode inside the prior", which cannot hold: on noise
    the belief peaks at the prior's bottom): 10 000 noise sequences of a 25 WPM dash (69 scored samples): every score
    finite; every per-sample score at most **1.387 times its cap** w_k s_k²/(f²s_k² + v_k) (Part 9; measured largest
    on noise 1.7 nats); the first mark's total at most the sum of those caps; the posterior mean at the last sample
    **below 0 dB S₅₀₀** (measured at most −3.5 dB S₅₀₀ over 4 000; median −10 to −13 dB S₅₀₀).
  - `Newborn.SwitchAgreesWithDense`: the switch sample (σ_u < 0.1 from the grid's moments) equals the dense
    integral's on at least **99.5%** of first marks and is never more than one sample away (measured: none earlier,
    0.1% later).
  - `Newborn.SwitchTimings` (Part 8): noise-free e_k = a·s_k, V′ = σ_y²(1 + q(f²s_k² + v_k)), f = 0.02: the switch at
    j = 9 (+30 and +20 dB S₅₀₀), 12 (+10) and 46 (0 dB S₅₀₀), each ±1 sample.
  - `Newborn.StrongEdgeOfThePrior` (not measured in the study): a +60 dB S₅₀₀ first mark (25 dB, 2.9 nepers, inside
    the prior's top): scores finite and within 0.005 nats per sample of the dense integral. A +85 dB S₅₀₀ first mark
    (at the top): scores finite; the per-sample and first-mark differences from the dense integral are **reported,
    not bounded at 0.005 nats**: where the belief is cut by the grid's last point (weight ½), the trapezoid's error
    is the end-point term of Euler–Maclaurin, about h²·F′(u_hi)/12 (derived), not the interior error that set 128
    points; a Gaussian belief of σ_u = 0.078 nepers cut at the top gives −0.035 to +0.166 nats in ln Z as its center
    moves from 1σ inside to 1σ beyond the top (the re-review's scratch check, derived numerically). `narrowest_sigma_u`
    reported. Two rough derivations bracket the narrowest σ_u at handover above +40 dB S₅₀₀: about 0.03 nepers from
    one plateau sample at the floor, f/√(2w) (Part 8; D22), which ignores the rise's caps; and about 0.074–0.078
    nepers once the caps are counted (each sample's precision in u is at most about 2w·s²/(f²s² + v), and the rise
    through j = 9 sums to about 166 at any level above about +30 dB S₅₀₀; the re-review). Both are rough; the
    prototype's record of the narrowest σ_u at handover (Task 11; M2) settles it.
  - `Newborn.LatestSwitchAtJ15AfterTheKeyUp`: at −10 dB S₅₀₀, a 5-sample mark switches exactly at the 16th sample
    after its claimed key-up (`should_switch` with the last-sample flag).
  - `Newborn.NegativeTemplateValues`: samples with s = −0.00064 and −0.00080 score as |s| would, within 10⁻¹² nats.
  - `Newborn.MomentMatching`: μ and P from the grid's û and σ_u as stated, to 10⁻¹²; the next sample's score from
    the matched Gaussian differs from the log-normal's by at most 0.01 nats at σ_u = 0.1 (approximation (c):
    0.008 nats, derived).
  - (The copy-on-write test of a branching newborn's grid is Task 10's `Search.NewbornGridsCopyOnBranch`, since
    the handle lives in the `Story`.)
  - `Newborn.ApproximationsMeasured` (label `measurement`, not in the default suite; Part 15, item 2, run in Task 16):
    writes to `build/` (a) the grid's per-sample and per-first-mark differences from the dense integral, on the
    study's levels and at +50, +60, +70 and +85 dB S₅₀₀ (the last including the end-point error at the top), with the
    narrowest σ_u at handover and the deepest revival;
    (b) the loss from holding the amplitude constant before the switch under fading at 0 dB S₅₀₀, 2 Hz and 5 Hz,
    against the integral over the fading path; its seeds and settings committed, so the record's numbers are
    reproducible from a commit.
- [ ] **Step 2: Run them; they fail.** **Step 3: Implement.** **Step 4: They pass;** write the CPU per
  newborn-sample on the desktop (noise, 500 newborns) to the commit message, against the study's 490 ns (595 ns on a
  mix) and row 1′'s 412 ns per scored sample, which it replaces on its stories.
- [ ] **Step 5: Document:** none (not yet run). Supplies to Task 10: §8d's newborn paragraph (the flat prior, the
  128-point grid in u, the live range, the switch); A.8d's exact form (Z_k, the trapezoid, the tables and their
  shifts, the switch from the grid's moments), the provenance of 128 (derived bound, measured check) and of 80 nats
  (heuristic, measured margin), D21's weight, the approximations (a)–(c) with the study's measured figures and their
  range of validity (to +40 dB S₅₀₀), **the end-point error at the prior's top, about h²·|F′(u_hi)|/12 (derived;
  Euler–Maclaurin), which the interior bound does not cover** (an end correction would change D22's rule and goes to
  the owner only if M2 shows the error matters), the two rough expectations of the narrowest σ_u, the counters, and the A.10 rows `newborn_grid_points` and
  `newborn_live_range_nats`; the removed running sums and Ruling 61's crossover leave no trace in the document.
- [ ] **Step 6: Commit.**

### Task 10. The search per sample; the decoder; configuration; the bench's JSON; fixtures; documents (Parts 3–12; Rulings 64, 66)

**Files:** modify `engine/include/kz4ap/viterbi/config.hpp`, `engine/src/viterbi/fist.cpp`,
`engine/include/kz4ap/viterbi/observation.hpp`, `engine/src/viterbi/observation.cpp`,
`engine/include/kz4ap/viterbi/search.hpp`, `engine/src/viterbi/search.cpp`, `engine/src/viterbi/text.hpp`,
`engine/include/kz4ap/viterbi_decoder.hpp`, `engine/src/viterbi_decoder.cpp`, `engine/src/engine.cpp`,
`engine/CMakeLists.txt`, `bench/src/viterbi_json.hpp`, `bench/src/viterbi_json.cpp`, `bench/src/main.cpp`, the tests
`engine/tests/viterbi/{fist,observation,search}_test.cpp`, `engine/tests/viterbi_decoder_test.cpp`,
`bench/tests/viterbi_json_test.cpp`, `bench/tests/bench_run_test.cpp`, the fixtures
`training/tests/data/search-stats/tiny.v{1,2}.stats.json`, `training/kz4ap_proto/search_stats.py` (the step read from
`sample_interval_s`, or `piece_s` in old files), `training/tests/test_search_stats.py` (Ruling 72, N8),
`docs/signal-processing.md`, `README.md` (the decoder's line); delete
`engine/include/kz4ap/viterbi/level.hpp`, `engine/src/viterbi/level.cpp`, `engine/tests/viterbi/level_test.cpp`.

**Interfaces:**
- Consumes everything of Tasks 2 to 9.
- `ViterbiConfig` (in this order; physical units; each with its class): `sample_interval_s` 4/1 500 (owner, D17);
  `dot_min_ms`, `speed_ratio`, `speed_count`; `fist_medians_dots`, `fist_sigmas`, `fist_shifts_dots`; `beam_nats`
  40, `ceiling` 1 700 (placeholders); `front_dot_s` 0.012, `edge_rise_s` 0.0025 (10–90%), `edge_rise_min_s` 0.0008,
  `edge_rise_max_s` 0.0047 (owner, D17); `fading_spread_hz` 2.0 (owner, D16); `systematic_floor` 0.02 (placeholder),
  `systematic_memory_s` and `systematic_lookahead_s` 10 × 4/1 500 (owner, D17); `nu_bin_s` 16 × 4/1 500,
  `nu_memory_s` 160 × 4/1 500 (owner, D17); `over_rate_per_s`, `rescue_epsilon`, `word_end_p`, `over_end_q`;
  `level_min_db` −15, `level_max_db` 85 (dB S₅₀₀, placeholders); `quadrature_order` 5; `newborn_switch_sigma_u` 0.1
  (derived), `newborn_grid_points` 128 (heuristic, measured; D22), `newborn_live_range_nats` 80 (heuristic; D22); `hiss_half_width_hz`, `hiss_percentile`, `hiss_average_s`;
  `genie_fading_spread_hz`; `stats`. Removed: `piece_s`, `block_loss_db`, `keying_rise_s`, `block_length_s()`.
  `sample_interval_s` must be a whole number of channel samples, and `systematic_memory_s`,
  `systematic_lookahead_s`, `nu_bin_s` and `nu_memory_s` whole numbers of decoder samples, each within 10⁻⁹
  relative, else construction throws naming the field; the other durations (`front_dot_s`, the edge rise times)
  are physical times used as such (Ruling 72, N7).
- `class ViterbiShared` holds: `channel_rate_hz`, `channel_samples_per_sample` (4), `front_taps`, `OutputNoise`,
  `TemplateTables`, `OmegaBarTable`, `NewbornTables` (Task 9); built once per configuration and channel filter by
  `make_viterbi_shared`, shared by every decoder. `FadingTables` belongs to each decoder, built from its own
  configuration's spread, so that `--genie fading` gives each oracle channel its own (Ruling 72, N12). `ViterbiShared`
  is forward-declared in `search.hpp` (it is defined in `viterbi_decoder.hpp`, which includes `search.hpp`).
- `class Search`: `Search(const ViterbiConfig&, std::shared_ptr<const ViterbiShared>, detail::SearchOptions = {})`;
  `void step(double e_fs, double t_s, double n0_fs2_per_hz)` (queues the sample; searches the oldest once 10 later
  samples are queued); `finish()` searches the rest with the window's ahead part shortened; `lookahead_samples()`;
  `best_text()`; `stats()`; test views as today (`StoryView` gains `amplitude`, `omega_bar_fs2`, `nu`,
  `in_newborn_integral`).
- The order at each searched sample k, for every story (Part 6's order; Part 9; Part 7):
  1. its template (s_k, v_k) from its edge history after this sample's claim;
  2. if s_k ≠ 0: the prediction from (μ, P, t_m) and the cached Ω̄; V′_k from the window's peak and the own part;
     w_k = `sample_weight(q_k)` at the predicted μ (a newborn: its grid, w at each point, D21 and D22, then
     `should_switch`); the score by
     `sample_evidence` (or the newborn integral); the amplitude set to the posterior, t_m = k; Ω̂'s and ν's sums
     updated; Ω̄ looked up again (Part 7: only after samples with s ≠ 0);
  3. the score booked at this sample into `base` (topic 5, item 9), with the timing and text costs as today.
  Every phase scores: a mark, a gap within 16 samples of its key-up, and the no-signal story after an over's end
  within 16 samples; beyond them s = 0 adds exactly 0.
- `Story` gains `EdgeHistory edges`, `Amplitude amp`, `double mode_a`, `LongRun long_run`, `double omega_bar`, a
  newborn handle (null once switched) and the variant's state slot (Task 12); loses `block_sum`, `block_pieces`,
  `level`. Sums restart at a newborn from silence, a rescue newborn (its edge history kept, O6) and an over's end
  (Part 7). Merging stays on `pack_state` (Ruling 64): of two stories in one state, the better keeps its own history.
- `DecodedSymbol::start_s` and `end_s` are the claimed edges' sample times (keyed times within 0 to 2.667 ms; Part 11).
- `ViterbiDecoder::process`: the envelope sampler on each block, then `search_.step(e_k, t_k, n0_)` for each sample
  while N₀ > 0 (as today: samples with N₀ ≤ 0 are not searched).

- [ ] **Step 1: Failing tests.**
  - Fist: the existing tests set `sample_interval_s = 0.002` explicitly and keep their numbers; new
    `Fist.MediansInSamples`: at the default, a 100 WPM dot's median is 4.50 samples and a 5.2 WPM word gap's
    604.5 ± 0.5 samples; P = 0.05 s⁻¹ × Δ = 1.333 × 10⁻⁴ per sample (8.92 nats).
  - `Config.DefaultsAreTheDesignTable`: every field's default as listed above; the durations are 10, 10, 16 and 160
    samples.
  - `Search.DecodesACleanStationAtThreeSpeeds` (§5, item 2), `Search.FollowsASpeedStep`, `Search.AnOverStartsAfterSilence`
    with `keyed_signals.hpp`'s D17 edges, through `Channelizer` and `EnvelopeSampler`.
  - `Search.FollowsASpeedStepAtEverySnr` (label `full-sweep`): +10 to +28 dB S₅₀₀, 3 seeds: CER ≤ 0.05 at each.
  - `Search.BooksEveryPointAtEverySample`: a story's score equals Σ of its per-sample scores plus its timing and text
    costs within 10⁻⁹ nats, at every sample of a dash and its tail.
  - `Search.TheTailIsScoredByTheStoryThatKeepsTheMark`: after a key-up the gap story scores j = 0 … 15 and updates
    its amplitude; at j = 16 a sample adds exactly 0; a no-signal story after an over's end does the same.
  - `Search.SumsRestartAtAnOversEndAndARescue`; `Search.MergesIdenticalStates`; `Search.PrunesByBeamThenCeiling`;
    `Search.NoiseKeepsScoresFinite` (60 s); `Search.IsDeterministic`.
  - `Search.HoldsTheLookAheadsSamplesUntilTheyAreSearched`: 10 samples; `Search.FinishSearchesTheLastSamples`: after
    `finish`, every queued sample is searched once, its window shortened at the end.
  - `Search.MarkTimesAreKeyedTimes` (Part 15, item 1): "PARIS PARIS" at 25 WPM, +30 dB S₅₀₀, at 8 edge positions:
    each letter's start_s lies 0 to 2.667 ms (±0.1 ms) after its keyed start.
  - `ViterbiDecoder.DecodesThroughTheEngine`, `InputSplitDoesNotMatter` (1, 7, 32, 1 000), `ExactZerosThenAStation`,
    `ANoiseDensityOfZeroMidStreamSplicesTheStream`, `ExtremeLevels` (+60 exact; +85 and −10 dB S₅₀₀ finite),
    `StrongStationsAtEveryEdgePosition` (full sweep: +30 to +60 dB S₅₀₀ at 16 positions, exact), `TheGridsEnds` (5.2
    and 100 WPM, +20 dB S₅₀₀: CER ≤ 0.05), `OneWordOversDecodeExactly`, `TenMinutesOfNoise` (1 min by default, 10 min
    under the existing `KZ4AP_*_FULL_SWEEP` environment variable; memory bounded,
    including the newborns' grids, about 1 KB each), `FlushesOnceAndResetStartsAfresh`, `RefusesWhatItCannotUse`, the genie
    fading and statistics plumbing.
  - `ViterbiDecoder.IsTheSearchOfTheEnvelopeSamples`: the decoder's search sees exactly `EnvelopeSampler`'s output.
  - Bench: the field macro and guard follow the new list; `--set sample_interval_s=…` round-trips; a removed name
    is refused naming it; the fixtures read back.
  - The void tests are deleted with their code (Ruling 72, N9): level 15, observation 10, search 2, fist 3
    (`Config.BlockLengthFollowsTheSpread`, `…ScalesAsTheInverseSpread`, `…RefusesNonsense`, testing `block_length_s()`)
    and Task 5's `Evidence.UnitWeightIsTodaysBlockEvidence` (its role kept by `Evidence.ScoreMatchesADenseIntegral`
    at w = 1 added to its grid); the decoder's void test went in Task 2.
  - `Search.NewbornGridsCopyOnBranch`: two successors of one newborn scored on different samples hold different
    grids; the parent's grid is unchanged; a successor pruned before it scores never copied (moved from Task 9).
  - `ViterbiDecoder.EachChannelHasItsOwnFadingTables`: with `--genie fading` two oracle channels at 0.5 and 3 Hz
    predict with their own ρ and ρ_a.
- [ ] **Step 2: Run them; they fail.**
- [ ] **Step 3: Implement.** Remove Part 12's names (§2's table). Rewrite the fixtures' `config` (O13).
- [ ] **Step 4: Run them; they pass,** with every earlier test; the three full-sweep tests once; record their times.
  Build and `ctest --preset linux` on the Linux machine.
- [ ] **Step 5: Document** (same commit). §8d rewritten in full for the amended decoder, within the body's 6 000 words
  (count it): the idea (stories over envelope samples); the order (front filter, envelope every 2.667 ms, the search
  10 samples behind); the template and edge term; the evidence and w; the systematic term; the amplitude and Ω̄; the
  newborn; timing and text; the search; what each non-default variant is, one sentence each, once Task 12 adds them.
  A.8d rewritten: the exact form of each part in the design's order with every formula, value and class, the
  derivations or their pointers (the design notes' Parts), the provenance of each measured or heuristic value with
  its validity, the limitations (Part 13), Rulings 60–64 and O4–O10; the sections on pieces, blocks, the flat prior's
  table and c(m) removed. A.10: Part 17's rows in place of the removed ones. A.11: sample, story-sample, scored
  sample, claimed edge, the timing reference. README: the decoder's line.
- [ ] **Step 6: Commit.**

### Task 11. Instrumentation: the CPU breakdown by part and Part 15's counters; statistics version 3 (Part 15; D18)

**Files:** modify `engine/include/kz4ap/viterbi/config.hpp` (`profile`), `engine/include/kz4ap/viterbi/search.hpp`,
`engine/src/viterbi/search.cpp`, `engine/src/viterbi_decoder.cpp`, `bench/src/viterbi_json.*`, the new fixtures
`training/tests/data/search-stats/tiny.v3.stats.json` and `tiny.v3-detail.stats.json` (one run of the bench on the tiny
recording `test_search_stats.py` names; Ruling 71), `training/kz4ap_proto/search_stats.py`,
`training/tests/test_search_stats.py`, `bench/tests/viterbi_json_test.cpp`, `engine/tests/viterbi/search_test.cpp`,
`docs/signal-processing.md`.

**Interfaces (produced):**
- `struct CpuBreakdown { double front_s, successors_s, scoring_s, newborn_s, weight_s, long_run_s, omega_bar_s,
  variant_sums_s, merge_prune_s, build_s, rest_s; std::int64_t story_samples, scored_samples, newborn_samples,
  weight_lookups, omega_bar_lookups; };` in `SearchStats` (`front_s` from the decoder), kept when `profile` is true.
  The lines are the timing slice's parts: successors (ends, births, slots); scoring (prediction, V′, quadrature,
  update) without w, the sums and Ω̄; the newborn integral; w's lookups; Ω̂'s and ν's sums; Ω̄'s lookups (the
  variant's lookup included); the variant's sums; merging and pruning; building the population; the rest.
- Timing (O20): `__rdtsc` (`<intrin.h>` on MSVC, `<x86intrin.h>` on g++) around each section, and around each call
  of the small parts (w, sums, Ω̄), with the counter's own cost (median of 10⁴ back-to-back reads at startup)
  subtracted; cycles converted by a rate calibrated against `steady_clock` over the run. The lines of the parts of
  1 to 7 ns per call (w, Ω̂'s and ν's sums, the variant's sums) carry their stated accuracy, ± the calibrated per-call
  overhead (the reads cost more than these parts and do not serialize; O20).
- Newborn counters (always with `stats`; D22): the narrowest σ_u at handover (per channel: the smallest, and a
  histogram in steps of 0.01 nepers), and the deepest revival, the largest depth in nats below the largest point from
  which a grid point later came back within 10 nats of it (per channel: the largest, and a histogram in steps of 5
  nats); a σ_u below 0.05 nepers or a revival deeper than 40 nats is flagged in the statistics' summary and goes to
  the owner. `NewbornIntegral` exposes `narrowest_sigma_u()` and `deepest_revival_nats()` for them.
- Counters (always with `stats`): own part sets C² (count; histogram by template value and by the sample's SNR in
  dB SNR in 74.48 Hz); Ω̂ ≤ 0 at a lookup (count); newborns in the integral per sample (mean, largest). The
  variant's fields (Ω̂ below `minimum_chi`, ζ outside its axis, and the `variant_sums_s` line) are defined here and
  stay zero; Task 12, which wires the variant, fills and tests them (Ruling 72, N15).
- Statistics format version 3 (Ruling 71), two forms: the **summary** (`--stats`), version 2's keys with "pieces" →
  "samples" (each `config.sample_interval_s` long), `ceiling_samples`, the 0.1 s windows of round(0.1 s/Δ) = 38
  samples, plus `cpu` (the breakdown, ms per channel-second and counts) and `counters`; the **detail**
  (`--stats-detail`), version 1's line columns with the same renamed keys, windows, `cpu` and `counters`. Versions 1
  and 2 still read for their own fields; `search_stats.py` reads version 3 and sums the CPU lines per group (Task 13
  extends this function).

- [ ] **Step 1: Failing tests.**
  - `Profile.ProfilingChangesNoDecoding`: texts and statistics (other than `cpu`) identical with `profile` on and off.
  - `Profile.LinesAddUpToTheDecodersTime`: on 60 s of noise, Σ lines within 5% of `Engine::stats().decoder_seconds`.
  - `Profile.OverheadIsSmall` (label `measurement`; Ruling 72, N24): the decoder's time with `profile` on at most
    1.15 times off (heuristic; desktop; the ratio written to the log).
  - `Counters.OwnPartIsCounted`: a +60 dB S₅₀₀ station's rise: the own part sets C² on the samples where e²/s² exceeds
    the window's peak, counted exactly.
  - Statistics (Ruling 71): version 3 round-trips in both forms; each version's reductions agree with that
    version's own committed numbers (`tiny.v1`, `tiny.v2` and the new `tiny.v3` fixtures); version 3's summary equals
    the reduction of version 3's detail file (`TheCommittedSummaryIsTheSummaryOfTheCommittedFullFile` gains a
    version-3 twin; its version-1/2 form stays).
- [ ] **Step 2: Run them; they fail.** **Step 3: Implement.** **Step 4: They pass.**
- [ ] **Step 5: Document:** A.11 defines each CPU line, each counter and the statistics fields (instrumentation
  changes no processing; the body is unchanged).
- [ ] **Step 6: Commit.**

### Task 12. The variants' switches; the true-story trace (Part 15, items 1, 3, 8, 11–13, 16–18)

**Files:** modify `engine/include/kz4ap/viterbi/config.hpp`, `engine/include/kz4ap/engine.hpp`,
`engine/src/engine.cpp`, `engine/src/viterbi/search.cpp`, `engine/src/viterbi_decoder.cpp`,
`engine/include/kz4ap/viterbi/newborn.hpp`, `engine/src/viterbi/newborn.cpp`, create
`engine/include/kz4ap/viterbi/trace.hpp`, `engine/src/viterbi/trace.cpp`,
`engine/include/kz4ap/viterbi_decoder.hpp`, `bench/src/main.cpp`, `bench/src/viterbi_json.*`, `bench/src/labels.*`,
tests in `engine/tests/viterbi/search_test.cpp`, `engine/tests/viterbi_decoder_test.cpp`,
`bench/tests/viterbi_json_test.cpp`, `bench/tests/labels_test.cpp`; `docs/signal-processing.md`.

**Interfaces (produced):**
- `ViterbiConfig`: `fixed_weight` (false; O12), `bell_decay` (false; D20), `omega_bar_noise` (false; its table from
  `EngineConfig::viterbi_omega_bar_table`, a path, refused when empty or mismatched), `merge_previous_element`
  (false: `pack_state` gains the previous element's length in samples when under 10, else 0, 4 bits).
- **The trace's plumbing** (Ruling 72, N13): `struct TraceRow` (the columns below); `class TrueStoryTrace` in
  `viterbi/trace.hpp`, which runs the per-sample model (template, V′, w, prediction, scoring or the newborn's grid,
  update, Ω̂, ν, Ω̄) along given keyed intervals with no search; `EngineConfig::viterbi_trace`, a vector indexed like
  the oracle channels of `struct TraceRequest { std::vector<std::pair<double, double>> key_intervals_s;
  std::function<void(const TraceRow&)> sink; }`, which `open_channel` hands to that channel's `ViterbiDecoder`. On a
  newborn's samples the `w` column is w at the grid's mean û.
- The variant's counters and CPU line defined in Task 11 are filled here (N15).
- The bench writes, with `--trace`, each channel's best-story symbols with their start and end times
  (`DecodedSymbol::start_s`, `end_s`, the claimed edges' sample times) to `symbols.csv` beside the trace; M1's timing
  check compares them with the label's keyed times (Ruling 72, N22).
- The bench: `--omega-bar-table FILE`; `--trace DIR` (oracle, `--decoder viterbi` only): for each oracle channel
  whose label carries `key_intervals_s` (`[[on_s, off_s], …]`, stream times, written by Task 13's generator option),
  the model run along that keying with its claimed edges at `claimed_edge_sample` of each keyed edge, no search;
  one CSV per channel, one row per sample: k, t_s, e_fs, s, v, mu_pred_fs, p_pred_fs2, sigma2_fs2, c2_fs2,
  v_prime_fs2, w, score_nats, omega_hat_fs2, nu, omega_bar_fs2, newborn (0/1), own_part_sets_c2 (0/1), and the
  variant's a2, b2 and sum_s4 (for M1's check of E[N²] against A + B·Ω on the decoder's own samples).
- The trace's measures (O19), defined in A.11 and computed by Task 13's analysis: the standardized innovation
  z_k = (e_k/s_k − μ_k|k−1)/√(P_k|k−1 + σ_y²/(2s_k²)) on flat-part samples (s_k ≥ 0.999) above +10 dB SNR in 74.48 Hz
  (the high-SNR Rician: e ≈ |a s| + Re(n), variance σ_y²/2), E[z²] = 1 when calibrated; and
  f̂² = mean over those samples of [(e_k − s_kμ_k|k−1)² − s_k²P_k|k−1 − σ_y²/2]/(s_k μ_k|k−1)², with its bootstrap
  interval over signals. On force-aligned recordings (Task 13b), whose edges may be one sample off, the first two
  flat samples after each claimed rise and the last two before each claimed fall are left out of f̂ (one sample
  early puts the true j = 10, s = 0.9825, where the claim says 1: a 1.75% residual, the size of f itself).

- [ ] **Step 1: Failing tests.**
  - `Variants.OffChangesNothing` (within one build; Ruling 72, N21): a configuration with every switch written out
    off, field by field, gives output identical to the default-constructed configuration's.
  - `Variants.FixedWeightIsConstant`: every scored sample's w = `output_noise().w_strong`, and the newborn's grid uses
    the same constant at every point (`NewbornTables` built with `fixed_weight`; N18).
  - `Variants.BellDecayAcrossSpacesOnly`: inside marks the prediction equals the default's; across a space the
    mean decays as Task 6's function, and the variance equals the default's (D20).
  - `Variants.OmegaBarNoiseNeedsItsTable`: refused without `--omega-bar-table`, or with a mismatched header; with a
    small table built by Task 8's test constructor and handed to the shared parts through a test hook
    (`make_viterbi_shared`'s overload taking an `OmegaBarVariantTable`), Ω̄ follows the variant's lookup (N17); the
    variant's counters (Ω̂ below `minimum_chi`, ζ outside its axis) count on a constructed case.
  - `Variants.MergePreviousElementSplitsStates`: two stories differing only in a previous element of 3 and 4
    samples stay apart; at 10 samples or more they merge.
  - `Trace.FollowsTheLabelsKeying`: a noise-free +30 dB S₅₀₀ station: e_k equals μ s_k within 0.5% on the flat part;
    the claimed edges are `claimed_edge_sample` of the label's; a label without `key_intervals_s` is refused for
    `--trace`, naming it; `symbols.csv` holds the best story's symbols with start and end times.
- [ ] **Step 2: Run them; they fail.** **Step 3: Implement.** **Step 4: They pass.**
- [ ] **Step 5: Document:** §8d gains one sentence per variant (count the body's words); A.8d the variants' exact
  forms (Bell's mean decay, D20; the noise-in-Ω̄ model, its sums, table, error and artifact from Task 8; the fixed w, O12;
  the merge state); A.10 the four switches; A.11 the trace, z and f̂.
- [ ] **Step 6: Commit.**

### Task 13. The measurement suites, label fields and the analysis (Python)

**Files:** modify `training/kz4ap_synth/generate.py`, `training/kz4ap_synth/suites.py`,
`training/kz4ap_proto/runner.py`, `experiments.py`, `devset2.py`, `search_stats.py`, `figures.py`; create
`training/kz4ap_proto/trace.py`; tests in `training/tests/`.

**Interfaces (produced):**
- Generator: `SignalSpec.record_intervals: bool = False` writes `key_intervals_s` (stream times) into the label;
  a transmit path's mark bias (item 12's −7 to +4.7 ms) expressed through the existing `imbalance_dits` = bias/dot
  (marks longer and spaces shorter), the bias in seconds recorded in the label's `design` (Ruling 72, N16); an
  impulse kind for item 6's and 7's crashes, clicks and static bursts: `ImpulseSpec(time_s, duration_s, peak_db,
  reference)` (peak in dB S₅₀₀, or dB relative to a named signal's key-down power), a filtered click being a 0.1 ms
  impulse.
- Suites (O14), each a function of the seed with the sizes of §7.2: `nd1b-fading` (M3, M8), `nd1b-strong` (M4, M10,
  M11), `nd1b-speed` (M5, M13), `nd1b-crash` (M6), `nd1b-longrun` (M7), `nd1b-weak` (M9, M18), `nd1b-stopped`
  (M10), `nd1b-edges` (M12), `nd1b-neighbors` (M15); `record_intervals` on in every `nd1b-*` suite and for DEV2e's A2
  and B2 groups (`--key-intervals` option of `generate`). Every sweep is a jittered grid (CLAUDE.md; owner,
  2026-10-06): cells over the range, each signal's value drawn within its cell by `kz4ap_synth.jitter` and recorded
  in the label's `design` field; only the points the design names (§7.2) are fixed.
- Runner: `--bench PATH` names the bench binary per run (today's decoder's build); the decoder `bank` on `nd1b-weak`.
- Analysis: the S₅₀₀ crossing at CER 0.5 beside 0.05 and 0.10; the CPU lines per group from statistics v3, in ms per
  channel-second and as fractions of §6's projected total (extending Task 11's function in `search_stats.py`, not a
  second one); the trace reductions in `trace.py` (E[z²]; f̂ with its interval, leaving out the two flat samples next
  to each claimed edge on aligned recordings; the misfit's variance per template sample over v_j; ν against the
  empirical count of independent looks of the label's fading; the decoded symbols' start times from `symbols.csv`
  against the label's keyed times, M1's timing check, N22); the newborn counts; the I2 cut removed.

- [ ] **Step 1: Failing tests:** `test_key_intervals_in_labels` (the label's intervals are the generator's to 10⁻⁹ s);
  `test_mark_bias` (a +4.7 ms bias, given as `imbalance_dits` = 4.7 ms/dot, lengthens every mark by 4.7 ms at half
  amplitude, and the label's `design` records 4.7 ms); `test_impulses`; one test per
  suite for its sizes, reproducibility by seed and the fields of §7.2, and that every swept value lies within its cell and the named points are exact; `test_crossing_at_half`;
  `test_cpu_lines_against_the_projection`; `test_trace_reductions` on a synthetic trace with known z and f.
- [ ] **Step 2–4: fail, implement, pass** (`pytest training -q`).
- [ ] **Step 5: Document:** none in `docs/signal-processing.md` (Python generator and analysis), unless A.11 defines
  a quantity the analysis now computes differently: then A.11 in the same commit.
- [ ] **Step 6: Commit.**

### Task 13b. Forced alignment of a published text to a recording (D19; M11's true story)

**Why:** M11 needs the true story of real W1AW sessions, which have no label (D19). Forced alignment, the standard way
to get a reference without hand labels (as in speech recognition), fixes the text and lets the model place the
edges: here, the new decoder's search with its letters forced to the published text. Its output is a labels file
with `key_intervals_s`, which `--trace` (Task 12) consumes.

**How a long session is aligned** (anchored, segmented alignment, the standard for long recordings: Moreno, Joerg,
Van Thong and Glickman 1998, recursive alignment of very long audio with anchor words; the per-word confidence as a
likelihood ratio against unconstrained recognition, Witt and Young 2000):
1. **The frequency:** the station's carrier is found from the recording's spectrum, the largest peak of the
   time-averaged spectrum within ±1 kHz of the frequency the owner names (heuristic search range); its drift over the
   session is measured as the peak per minute. The oracle channel holds the frequency fixed within each segment
   (step 3), through the oracle segments of Task 2 (O3); a drift above ±10 Hz within a segment (the front filter loses
   0.22 dB at 10 Hz, dB relative to 0 Hz) is reported, and the segment is split at the minute where it occurs.
2. **The session's structure is found, not assumed.** A free decoding of the whole recording by the new decoder
   (unforced, R1's wide beam) gives each region's speed (its stories' speed state), its gaps in dots (letter and word
   gaps stretched beyond 3 and 7 dots by a common factor mark Farnsworth spacing; the decoder has no Farnsworth model,
   D14, so such a region's gaps cost timing score and are reported) and its text. The published file's own markings
   (speed headings, if any) are read and cross-checked against it; neither is assumed.
3. **Segments and anchors:** the recording is cut at silences longer than 2 s (heuristic; longer than the 1.6 s
   word gap at 5.2 WPM) and at changes of the free decoding's speed; within a segment, words that the free decoding
   copies exactly and that occur once in the published text's window are anchors (Moreno et al.); the published text
   between consecutive anchors is aligned to the recording between them, recursively, as the cited method does.
4. **Each span (between anchors) is force-aligned** with an optional **filler** before and after every word: a free decoding of any
   symbols, at a penalty of 10 nats per filler letter (heuristic, set from the inserted-word test), so that keying
   not in the published text (station identification, announcements, a word sent twice) is absorbed instead of
   derailing what follows. Alignment runs at R1's wide beam (80 nats, 17 000 stories).
5. **Per word,** the forced story's score less the free decoding's over the word's span (`forced_minus_free_nats`);
   a word below −10 nats (heuristic; its measurement below) is marked `excluded`, and so is any span the filler took.

**Files:** modify `engine/include/kz4ap/viterbi/search.hpp`, `engine/src/viterbi/search.cpp`,
`engine/include/kz4ap/engine.hpp`, `engine/src/engine.cpp`, `engine/include/kz4ap/viterbi_decoder.hpp`,
`engine/src/viterbi_decoder.cpp` (the plumbing below), `engine/src/wav_reader.cpp` and its header,
`bench/src/main.cpp`, `bench/src/viterbi_json.*`, `bench/CMakeLists.txt`; create `bench/src/align.hpp`,
`bench/src/align.cpp`; tests in `engine/tests/viterbi/search_test.cpp`, `engine/tests/wav_reader_test.cpp`,
`bench/tests/align_test.cpp`; `docs/signal-processing.md` (create A.1, Input, and extend §1 for the mono input;
A.11 for the alignment) (Ruling 72, N20).

**Interfaces (produced):**
- `detail::SearchOptions::forced_text`: `std::optional<ForcedText>`, `struct ForcedText { std::vector<std::string>
  symbols; /* the engine's symbols, " " for a word space */ bool filler; double filler_letter_nats; };`. When set, a
  letter start may only begin the next symbol of the text (its tree path) or, with `filler`, a filler letter (any
  symbol, paying `filler_letter_nats`) at a word boundary; a letter may only end at that symbol's node; the gap after
  it is a word gap where the text has a space and a letter gap otherwise; births from silence only at the aligned
  span's first symbol or in the filler; no rescue newborn; everything else (timing, amplitude, scoring) as the default
  decoder, at the beam and ceiling given. `struct AlignedEdge { std::int64_t down, up; bool filler; };`
  `Search::best_edges()`: `std::vector<AlignedEdge>`, the best story's claimed key-down and key-up samples;
  `Search::best_speeds()`: the best story's speed (index into the speed grid, and WPM) per symbol.
- **Plumbing, not a second chain:** `EngineConfig::viterbi_search_options` (a vector indexed like the oracle channels
  of `detail::SearchOptions`, handed by `open_channel` to that channel's `ViterbiDecoder`, which passes it to its
  `Search`) and `Engine::viterbi_search(std::uint32_t track_id) const` (read-only, like `viterbi_stats`), so
  `align.cpp` runs the engine's own channelizer, follower, hiss and sampler.
- `struct WordAlignment { std::string word; double start_s, end_s; double forced_minus_free_nats; bool excluded; };`
  `struct AlignSegment { double start_s, end_s, frequency_hz, wpm, farnsworth_factor; std::vector<WordAlignment>
  words; std::vector<std::string> filler_text; };`
- Bench: `kz4ap-bench RECORDING.wav --align TEXT_FILE --align-near-hz HZ --labels-out LABELS.json [--mono-as-real]`:
  writes a labels file with the parser's required fields (`sample_rate`, `duration_s`; per signal `text`,
  `freq_offset_hz`, `wpm`, `snr_db` as estimated from the aligned marks, `start_s`, `end_s`) and one signal per
  segment: its `text` (the published text normalized to the engine's 62 symbols; any other character refused,
  naming it), `key_intervals_s` from the claimed edges as keyed-time estimates
  t_k − Δ/2 (the claimed edge lies 0 to Δ after the keyed edge, Part 5; `claimed_edge_sample` of the estimate returns
  k), its frequency, speed and Farnsworth factor as found, and `alignment`: the segment's `WordAlignment` list and
  filler text.
- `--mono-as-real`: a one-channel 16-bit WAV is read as I with Q = 0 (a real audio tone at f appears at ±f; the
  channel at +f is 2f away from the image, outside the 150 Hz channel filter for tones above 300 Hz). For the MP3
  smoke check only; the MP3 is decoded and resampled to 48 kHz outside the repository by a standard audio tool.

- [ ] **Step 1: Failing tests.**
  - `Search.ForcedTextAlignsEdgesWithinOneSample` (default suite; Ruling 72, N23): one seed, +20 dB S₅₀₀, a 10-word
    text at 25 WPM with a fist (σ = 0.1 in ln length), the default beam: every aligned edge estimate within one
    sample (2.667 ms) of its keyed time.
  - `Search.ForcedTextAlignsEdgesFullSweep` (label `full-sweep`): `keyed_signals.hpp`'s D17 keying of a 60-word text
    at 13, 25 and 35 WPM, each element's length drawn log-normal with σ = 0.1 about its median, 5 seeds, the true
    keyed times known, at R1's wide beam: at +20, +30 and +40 dB S₅₀₀ every aligned edge estimate lies within one
    sample of its keyed time; at +10 dB S₅₀₀ the share within one sample is reported.
  - `Align.WordThresholdMeasured` (label `full-sweep`, in `bench/tests/align_test.cpp`, where the per-word logic
    lives): the correct words' `forced_minus_free_nats` at +10, +20, +30 and +40 dB S₅₀₀ steady and under 2 Hz
    Rayleigh fading (the helper's `rayleigh_gain`; the free decoding may invent gaps there); asserted above −10 nats
    for every correct word at +20 to +40 dB S₅₀₀ steady; the distribution at +10 dB S₅₀₀ and under fading written to
    the log, and the threshold confirmed or reset from the gap between it and a mismatched word's values (the next
    test), with the result in the commit message (heuristic until then).
  - `Search.ForcedTextKeepsTheText`: without filler, the best story's text equals the forced text exactly.
  - `Search.UnforcedSearchUnchanged` (within one build; N21): a `Search` with `forced_text` unset gives output
    identical to one built without options.
  - `Align.ADifferentWordIsExcluded`: one word of the published text replaced in the signal by another of the same
    length: that word's `forced_minus_free_nats` is below −10 nats and it is marked `excluded`; every other word's
    is above −10 nats.
  - `Align.AnInsertedWordDoesNotDerailTheRest`: an unpublished word ("DE W1AW") keyed mid-text at +20 dB S₅₀₀: the
    filler takes it (its span `excluded`, its filler text "DE W1AW"), and every edge of the published words after it
    lies within one sample of its keyed time.
  - `Align.FindsTheSessionsSegments`: a synthetic session of three parts at 35, 25 and 15 WPM, the last with
    Farnsworth spacing (15 WPM overall at 18 WPM characters), separated by 5 s of silence, each preceded by an
    unpublished announcement, at +20 dB S₅₀₀, the published text giving no speeds: three segments found with their
    speeds within 5% and the Farnsworth factor within 10% (heuristic bounds); every published edge of the first two
    segments within one sample; the third's within one sample except next to stretched gaps, reported.
  - `Align.FrequencyAndDrift`: a station 300 Hz from the named frequency drifting 15 Hz over 3 minutes: the peak
    found within 1 Hz, the drift reported, the segment split where it passes ±10 Hz.
  - `Align.LabelsRoundTrip`: the written labels file reads back through `bench/src/labels.*`, and `--trace` on it
    gives claimed edges equal to `best_edges()`.
  - `WavReader.MonoAsReal`: a mono WAV with a 700 Hz tone reads as I = tone, Q = 0; without the flag a mono WAV is
    refused as today.
- [ ] **Step 2: Run them; they fail.** **Step 3: Implement.** **Step 4: They pass,** with every earlier test.
- [ ] **Step 5: Verification on the ARRL's downloadable MP3** (a smoke check of the tooling, not a measurement; the
  MP3 is probably rendered from the text and is lossy, D19): one session's MP3, decoded and resampled to 48 kHz,
  with **white Gaussian noise added at the level that puts the keyed tone at +30 dB S₅₀₀** (M11's range; a lossy
  rendered file carries almost no noise, and an encoder zeroes empty bands, which would leave N₀ ≤ 0 and nothing
  searched, Task 10, or the station beyond the prior's top); the run **checks and reports N₀ > 0** on every block after the
  first second, a check the run reports (the audio is not committed, so this is not a ctest); aligned with
  `--mono-as-real` to its published text: no published word excluded, and per speed
  segment an ideal timing grid (one dot length and one start time, fitted by least squares to the aligned edges in
  dots of the segment's standard or Farnsworth timing) leaves every edge's residual within one sample (2.667 ms), as
  a machine-rendered file should. The audio, the labels and the output stay in `build/` (not committed); the
  command, the added noise level and the summary go in the commit message.
- [ ] **Step 6: Document** (same commit): §1 and a new A.1 (Input), the mono-as-real input (a bench option; the engine's input
  otherwise unchanged); A.11, the forced alignment: the frequency search and drift check, the free decoding that finds
  the structure, the segments and anchors (Moreno et al. 1998), the constrained search with its filler and penalty,
  the keyed-time estimate t_k − Δ/2, the per-word exclusion at −10 nats with its measured basis, the heuristic values
  (±1 kHz, 2 s, 10 nats per filler letter, −10 nats) labeled, and its purpose (M11, D19).
- [ ] **Step 7: Commit.**

### Task 14. DEV2e and N2e; `d17-bank`; today's decoder built (Linux machine; Part 14; prototype plan O13)

- [ ] **Step 1:** bundle `new-decoder-1b` to the Linux machine; build in a new build folder; `ctest --preset linux`.
- [ ] **Step 2:** generate DEV2e (`python -m kz4ap_synth.suites generate --suite dev2 --seeds 1 --per-cell A2=48
  --per-cell B2=24 --per-cell C2=24 --per-cell I2=24 --key-intervals A2,B2 --out DIR`) and N2e into new folders,
  never over DEV2; check 498 recordings, 14 204 channels, 1 065 742.1 channel-seconds.
- [ ] **Step 3:** the bank's reference on DEV2e: `kz4ap_proto.runner record` (Envelope, fixed-bin streams: O2 leaves
  them unchanged), `kz4ap-bank-replay --name d17-bank --immediate d17-bank-immediate --jobs 10`, `runner score`,
  `experiments scores --csv d17-bank.csv` with the two Farnsworth columns; commit the rounded table in
  `docs/plans/data/2026-10-26-new-decoder-prototype-1b/`.
- [ ] **Step 4:** today's decoder: a worktree of the Linux machine's checkout at `56bae92`, built in its own build
  folder; `kz4ap-bench` of that build recorded as "today's decoder" (O17). Before its full run, check on one
  recording of each group that it reads the new labels (`edge_placement`, `key_intervals_s`) and gives the same
  output as on the same recording with those fields removed.
- [ ] **Step 5:** generate the `nd1b-*` suites; commit the results record's §1 (conditions: the sets, the commits,
  the builds, the machines), with `d17-bank`'s table.

### Task 15. R0 and M1; stop for the owner

- [ ] **Step 1:** R0 at the defaults and the reference settings, with `--stats` and `profile`; the quadrature orders
  3, 5, 20 on B2's and N2e's recordings and the strongest signals.
- [ ] **Step 2:** M1's checks on recordings (trace and analysis).
- [ ] **Step 3:** the record's §2: CPU per channel-second per group with the breakdown against §6.1's projection
  (fractions of the projected total), live and scored story-samples, the share of scored samples, newborns in the
  integral and their fates, the reference's cost multiple, §6.3's table redone with the measured c; M1's results;
  score tables; commit.
- [ ] **Step 4: Stop.** The owner sees R0's measured costs, by part, in words, in chat, before Task 16; at the same
  stop he is asked for the W1AW recordings M11 needs (D19; Task 19, Step 2).

### Task 16. The synthetic grids

- [ ] **Step 1:** M2 (Task 9's committed test `Newborn.ApproximationsMeasured`, label `measurement`, writing its
  tables to `build/`),
  M3–M10, M12, M14 and M15's synthetic part, each with its rule (§7.2), paired against today's decoder where stated.
- [ ] **Step 2:** one record section per item with its score tables; the items whose rule says "to the owner" go to
  him one at a time; commit.

### Task 17. R1–R3: the reference, the beam, the ceiling

- [ ] **Step 1:** R1 and R1b; their rules. **Step 2:** R2; its rule; §5, item 4. **Step 3:** R3; its rule.
- [ ] **Step 4:** the adopted defaults into `ViterbiConfig`, `docs/signal-processing.md` (§8d, A.10) and the record,
  with the score tables; commit.

### Task 18. R4 with M13, M15, M16, M17, M18

- [ ] **Step 1:** R4 on DEV2e and N2e; today's decoder on the same, and again on F2 with `--genie frequency` (O17). **Step 2:** the analysis against
  `d17-bank` and today's decoder; I2 apart. **Step 3:** M13, M16, M17, M18 with their rules; M15's E2, F2 and G2
  reading. **Step 4:** the record's sections with at most three figures each, score tables committed; a default
  changed by M18 goes into the code and documents and R4 reruns; commit.

### Task 19. R5, R7, R8, R10, R11; M11's proposal; the conclusions

- [ ] **Step 1:** each run with its rule (§7.1), one record section each.
- [ ] **Step 2:** M11 (D19) on `nd1b-strong` and on the W1AW recordings: the owner supplies live W1AW code-practice
  sessions recorded off the air as I/Q (at the engine's 48 or 192 kHz, with the session's date, time and frequency),
  **before Task 19 starts** (asked for at Task 15's stop, so they can be recorded during the long runs), each with the
  frequency it was received on; each is aligned to the ARRL's published text for that session by Task 13b's `--align`
  (segments, anchors and filler as there; words and filler spans marked `excluded` left out),
  and `--trace` on the resulting labels gives M11's samples; the ARRL's downloadable MP3s served only Task 13b's smoke
  check; the owner's other recordings, scored where the new
  decoder and the bank agree, as a cross-check with its bias named (biased low). M4 and M6 rerun at the proposed f;
  to the owner.
- [ ] **Step 3:** adopted values into the code, documents and tables (R4 rerun where a default changed); commit.
- [ ] **Step 4:** the record's conclusions and the decisions they raise for the owner.
