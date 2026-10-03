# Milestone 2c, Plan A: the filter bank in C++, a faithful port of the stage-1 prototype

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Port the stage-1 Python prototype (`training/kz4ap_proto`, as settled at the end of stage 1) to C++ as a third decoder, the **bank decoder** (`--decoder bank`), verified against the prototype as a reference implementation; add text corrections to the engine's events and the bench's scoring; measure its cost.

**Architecture:** A new `BankChannel` (one station's decoder: 32 branches, noise, keying, duration fits, periodicity, selection, output with corrections) ported module by module from the prototype, each module checked against "golden" values the prototype writes for fixed inputs. A replay tool decodes the recorded channel streams in C++ and writes the prototype's decoded-file format, so stage 1's Python tooling scores and compares it against the prototype's own decoded files. Then `BankDecoder` wraps `BankChannel` behind the engine's `Decoder` interface; the engine's events and the bench gain corrections. **No stage-2 design change is made here** (those are Plan B): the port reproduces the prototype.

**Tech Stack:** C++20, CMake presets `windows` and `linux`, GoogleTest (existing), the engine's FFT (`engine/src/fft.hpp`), Python 3.12 + numpy for golden values and scoring (existing `training/` tooling).

**Spec:** `docs/design/2026-09-30-filter-bank-speed-estimator-design.md` (the design), amended by `docs/design/2026-10-03-filter-bank-stage-2-design.md` (stage 2; this plan implements none of its §3 changes, only the port and the plumbing its §4.2 needs). Reference behavior: `training/kz4ap_proto/*.py` at the commit this plan starts from, with the settled `ProtoConfig()` defaults (results record `docs/plans/2026-09-30-milestone-2b-stage-1-results.md`, §3.13).

## Global Constraints

- **Faithful port.** Every formula, constant, order of operations and default value is the prototype's. Where Python and C++ differ unavoidably (libm versus numpy last-bit rounding, integer conversions), take the C++ that computes the same mathematical value; document any intended deviation in the task report. No stage-2 design change (stage-2 spec §3) in this plan.
- **Exact math build.** No `-ffast-math`, `/fp:fast` or vector math libraries in this plan (the spike measured 4× from them; that is Plan B's choice). Compile flags stay the presets'.
- **Envelope and Matched unchanged:** their decoded output stays bit-identical; `bench/baselines/*` unchanged; `bash bench/smoke.sh build/windows` passes (smoke Envelope CER 0.0353, Matched 0.0436).
- **Physical units** in every parameter (s, Hz, FS, nats); convert to samples only at the point of use (project rule).
- **`docs/signal-processing.md` describes the engine as it is:** every task that adds engine signal processing updates it in the same commit (new section "8c. Bank decoder"), with its parameter table entries and each value marked derived, measured or heuristic. Treat a stale description as a bug.
- Every quantity carries a unit; every dB names its reference.
- Wording (owner): "variant", never "arm"; a decoder is scored ON a recording, never a recording scored; a recording plus one label file is a "test case"; "through the detector path", never "detector-only". American spelling.
- Nothing committed names the owner's private infrastructure (host names, domains, user names, home paths, network shares, locations): call the server "the Linux machine". Grep staged diffs before committing.
- Git: commits on the feature branch `milestone-2c-bank` only; never push, merge, rebase, amend, `reset --hard`, or delete branches; one git command per call; **never any Co-Authored-By or "Generated with Claude Code" line**.
- Long runs on the Linux machine use `build/kz4ap-job` there; results come back by scp; code reaches it by git bundle (`.superpowers/sdd/<this plan's workspace>/server-workflow.md`, copied from stage 1's).

## Review Focus

1. **Input split across blocks:** the engine promises identical output however the input is split into `process()` calls; the bank must decode the same text whatever the block sizes (1, 32, 47, 1000 samples).
2. **A track that dies mid-over or mid-character:** `flush()` must publish what exists and settle pending corrections without reading past the end.
3. **Silence of exact zeros** (zero-padded first-sample recordings, a dead channel): no NaN or ±Inf may reach a published value or a correction (log of zero power, zero amplitude, zero noise variance).
4. **A correction reaching before the channel's first sample or across a re-key:** the replaced span is clipped to what was published, at most 20 s back, and the bench's text assembly never indexes outside the published characters.
5. **A channel rate other than 1500 samples/s** (the engine's `channel_rate()` is configuration-dependent): every length is converted from seconds at the point of use, so a 2000 samples/s channel decodes the same timing.

Each item has its test in the task that owns the code (Tasks 3, 7, 8, 9).

---

## Design decisions

**Reference methodology.** The prototype is the specification of behavior. Two levels of checking:
1. *Module level — golden values.* A Python script (`training/kz4ap_proto/golden.py`, Task 1) runs each prototype module on fixed, seeded inputs and writes the outputs to small JSON files under `engine/tests/data/bank/` (committed). Each C++ module test reads them and requires agreement to a stated tolerance: relative 1e-9 for continuous values (both sides double precision, same formulas), exact for discrete ones (keyed sample indices, classifications, argmax indices, characters).
2. *System level — the replay check (Task 9).* The C++ replay tool decodes every recorded oracle channel of the development set (seed 1; 21 test cases, 525 channels) and writes `build/suite/full3/proto/bank-cpp/<result>.decoded.json` in the prototype's format; the existing `kz4ap_proto.experiments compare` scores it against the prototype's `bank-proto` decoded files. **Pass criteria (owner decision D2, 2026-10-03):** (a) module level: every discrete output (keyed sample indices, edges, classifications, chosen grid points, characters) **exactly** equal to the prototype's on the golden inputs, continuous values within relative 1e-9; (b) system level: **every** development-set channel whose final text differs from the prototype's is **traced to a near-tie**, a discrete choice whose two candidates differ by no more than rounding (about 1e-12 relative) on one side and the other, shown with the values; any difference not so traced is a bug. Why not bit-identical text: numpy evaluates exp and log with its own vectorized routines, sums arrays pairwise and uses its own FFT build, so values differ from C++'s in the last bit (about 1e-16 relative), and a near-tie can then flip a discrete choice (stage 1 saw the same between Windows and Linux builds of one C++ code: tracked frequencies differing by about 1e-10). The exact-math build keeps such flips rare and traceable.

**Re-centering.** The prototype decodes oracle channels mixed to 0 Hz at the labeled frequency and drift, and detector channels mixed by the detector's frequency block by block, without the frequency tracker. The C++ port does the same: `BankDecoder` mixes by the engine's frequency anchor (`set_frequency_anchor_hz`). The frequency tracker in the loop (stage-1 spec §7, stage 2) is Plan B's.

**Corrections in the engine.** A correction replaces the channel's published characters from an index on: `TextCorrection{std::size_t from_index; std::vector<DecodedSymbol> chars; double t_s; std::string reason}`. `DecodeUpdate` and `DecodedTextEvent` gain `std::vector<TextCorrection> corrections` (empty for Envelope and Matched, so their events are unchanged). The bench keeps each track's character list, appends new characters, applies corrections by index, and writes two texts per track: **final** (corrections applied) and **immediate** (the characters as appended, corrections ignored: what a reader saw the instant each character appeared; stage-2 spec §4.2). For Envelope and Matched the two are equal.

**Cost.** The spike (2026-10-03, Linux machine, exact-math build) measured the fit's grid update at 329 µs per observation and the best-fit search at 66 µs per call; with the prototype's event rates (about 160 observations and 145 searches per channel-second) the port should cost about 65 ms of CPU per channel-second (derived from the spike, not a measurement of the port). Task 9 measures it. Budget: a development-set decode (74 748 channel-seconds) about 81 CPU-minutes, about 8 minutes of wall time on the Linux machine's 10 workers; the full 3-seed oracle set (about 430 000 channel-seconds) about 47 minutes of wall time.

## File structure

New, under `engine/` (one module per prototype module; names mirror them):

| File | Responsibility | Ported from |
|---|---|---|
| `engine/include/kz4ap/bank/bank_config.hpp` | `BankConfig`: every `ProtoConfig` field used by the decoder, same names, units and defaults | `params.py` |
| `engine/include/kz4ap/bank/filters.hpp`, `engine/src/bank/filters.cpp` | branch lengths, samples, boxcar outputs, power response; ln I₀, envelope LLR, logistic | `bank.py`, `detect.py` |
| `engine/include/kz4ap/bank/noise.hpp`, `engine/src/bank/noise.cpp` | three-tap noise, spectrum noise with mask bias, per-branch fallback | `noise.py` |
| `engine/include/kz4ap/bank/keying.hpp`, `engine/src/bank/keying.cpp` | hysteresis, edges, `BankKeyer` (amplitude, squelch, unknown-amplitude test), `rekey` | `keying.py` |
| `engine/include/kz4ap/bank/fit.hpp`, `engine/src/bank/fit.cpp` | `DurationFit` (grid tables, best, refine), class priors, classification | `fit.py` |
| `engine/include/kz4ap/bank/periodicity.hpp`, `engine/src/bank/periodicity.cpp` | the comb on 2T and `Periodicity` (windows, update cadence) | `periodicity.py` (comb only) |
| `engine/include/kz4ap/bank/selection.hpp`, `engine/src/bank/selection.cpp` | `TextModel` (VE3NEA weights), `Selector` | `text.py`, `select.py` |
| `engine/include/kz4ap/bank/channel.hpp`, `engine/src/bank/channel.cpp` | `Branch`, `Output` with corrections, `BankChannel` (the per-station decoder) | `channel.py` |
| `engine/include/kz4ap/bank_decoder.hpp`, `engine/src/bank_decoder.cpp` | `BankDecoder : Decoder`: block input, anchor mixing, `DecodeUpdate` with corrections | — |
| `engine/tests/bank/*_test.cpp`, `engine/tests/data/bank/*.json` | module tests against golden values | — |
| `bench/src/replay.cpp` (+ target `kz4ap-bank-replay`) | decodes recorded channels in C++, writes the prototype's decoded-file format | `runner.py` decode path |
| `training/kz4ap_proto/golden.py` | writes the golden JSON files from the prototype | — |

Modified: `engine/include/kz4ap/decoder.hpp`, `engine/include/kz4ap/event_bus.hpp` (corrections), `engine/include/kz4ap/classical_decoder.hpp` (`FrontEnd::Bank`), `engine/src/engine.cpp` (creates `BankDecoder`), `bench/src/main.cpp` and `bench/src/report.*` (corrections, immediate text, `--front-end bank`), `training/kz4ap_synth/suites.py` (front end `bank`), `docs/signal-processing.md` (§8c), CMake files.

JSON in C++: nlohmann/json (fetched in the top-level `CMakeLists.txt`) is linked into the bench only, and the engine library stays free of it. JSON conversion of `BankConfig` and `ChannelResult` lives in `bench/src/bank_json.hpp/.cpp` (used by the replay tool), and the engine's bank tests link nlohmann/json as a test-only dependency (`engine/tests/bank/golden.hpp`: `load_golden(name)` reads `engine/tests/data/bank/<name>.json`).

---

### Task 1: Branch, configuration, golden-value tooling

**Files:**
- Create: `engine/include/kz4ap/bank/bank_config.hpp`, `training/kz4ap_proto/golden.py`, `training/tests/test_golden.py`, `engine/tests/data/bank/config.json`, `engine/tests/bank/golden.hpp`, `engine/tests/bank/config_test.cpp`
- Modify: `engine/CMakeLists.txt` (a `bank` source list; a `kz4ap_bank_tests` target linking `nlohmann_json::nlohmann_json` for tests only)

**Interfaces:**
- Produces: `struct BankConfig` with one member per `ProtoConfig` field the decoder reads, same name, type (double, int, `std::vector<double>`), unit comment and default. (Its JSON conversion, `bank_config_from_json` and `to_json`, is in `bench/src/bank_json.*`, Task 7.) `golden.py`: `write_all(out_dir: Path) -> list[Path]`, one function per module `golden_<module>(cfg) -> dict`.

- [ ] **Step 1:** Create the branch: `git switch -c milestone-2c-bank` (from `milestone-2b-filter-bank`).
- [ ] **Step 2: Write the failing test** `engine/tests/bank/config_test.cpp`:
```cpp
#include "kz4ap/bank/bank_config.hpp"
#include "golden.hpp"   // tests' helper: load_golden("config") -> json (Task 1 adds it)
#include <gtest/gtest.h>

TEST(BankConfig, DefaultsEqualThePrototypes) {
    const auto g = kz4ap::test::load_golden("config");   // ProtoConfig() as JSON (asdict)
    const kz4ap::bank::BankConfig c{};
    EXPECT_DOUBLE_EQ(c.length_dits, g["length_dits"].get<double>());
    EXPECT_DOUBLE_EQ(c.fit_memory, g["fit_memory"].get<double>());
    EXPECT_DOUBLE_EQ(c.rekey_after_s, g["rekey_after_s"].get<double>());
    EXPECT_EQ(c.x_on_values.size(), g["x_on_values"].size());
    for (std::size_t k = 0; k < c.x_on_values.size(); ++k)
        EXPECT_DOUBLE_EQ(c.x_on_values[k], g["x_on_values"][k].get<double>());
    // ... one EXPECT per field: generate this list from ProtoConfig's field names (Step 4)
}
```
- [ ] **Step 3: Write `golden.py`** with `golden_config(cfg) -> dict` returning `dataclasses.asdict(cfg)`, and `write_all`; a pytest `test_golden.py::test_write_all_round_trips` that writes to a temp dir and reloads every file. Run: `.venv\Scripts\python -m pytest training/tests/test_golden.py -q` → passes; then `.venv\Scripts\python -m kz4ap_proto.golden --out engine/tests/data/bank` writes `config.json`.
- [ ] **Step 4: Write `BankConfig`**: one member per `ProtoConfig` field read by `bank.py, noise.py, detect.py, keying.py, fit.py, periodicity.py, select.py, text.py, channel.py` (grep `cfg\.` in them), with the default value copied exactly (the 32 `x_on_values` and `mask_bias` values verbatim), the same unit comment, and the status (measured/heuristic/placeholder/owner) from `params.py`. Complete the test with one EXPECT per member.
- [ ] **Step 5:** Build and run: `cmake --build build/windows --config Release` then `ctest --test-dir build/windows -C Release -R BankConfig` → PASS.
- [ ] **Step 6: Commit** (`git add` the files above; message "Bank port: BankConfig mirrors the prototype's settled defaults; golden-value tooling"). No signal-processing.md change yet (no engine signal processing).

### Task 2: Filters and the envelope likelihood

**Files:** Create `engine/include/kz4ap/bank/filters.hpp`, `engine/src/bank/filters.cpp`, `engine/tests/bank/filters_test.cpp`; extend `golden.py` (`golden_filters`); Modify `docs/signal-processing.md` (§8c opening: what the bank is, its ladder L_k = 9.6 ms × 1.1^(k−1), boxcar, ln I₀ and LLR, units, statuses).

**Interfaces:**
- Produces (namespace `kz4ap::bank`): `std::vector<double> branch_lengths_s(const BankConfig&)`; `std::vector<int> branch_samples(const std::vector<double>& lengths_s, double rate_hz)`; `std::vector<double> realized_lengths_s(const BankConfig&, double rate_hz)`; `std::vector<std::complex<double>> boxcar(std::span<const std::complex<double>> u, int n)` (the prototype's definition, including its start-up convention); `double power_response(double f_hz, int n, double rate_hz)`; `double log_bessel_i0(double z)`; `double envelope_llr(double x, double a)`; `double logistic(double g)`.

- [ ] **Step 1: Golden values:** `golden_filters(cfg)` writes `branch_lengths_s`, `branch_samples` at 1500 and 2000 samples/s, `realized_lengths_s`, the boxcar of a seeded complex sequence (numpy `default_rng(7)`, 400 samples) for n = 14 and 276, `power_response` at 0, 10, 50, 150 Hz for n = 14, `log_bessel_i0` at 0, 0.5, 3.74, 3.76, 10, 50, and `envelope_llr` on a 5×5 grid of x, a.
- [ ] **Step 2: Failing tests** in `filters_test.cpp`, one `TEST` per function, each looping over the golden arrays with `EXPECT_NEAR(actual, expected, 1e-9 * std::max(1.0, std::abs(expected)))` (integers with `EXPECT_EQ`).
- [ ] **Step 3: Port** `bank.py` and `detect.py` line for line into `filters.cpp` (same expressions and order; `log_bessel_i0` is the prototype's formula, check whether it equals the engine's `matched_front_end.cpp` one and reuse that if identical).
- [ ] **Step 4:** Run `ctest ... -R Bank` → PASS. Run the full engine tests and the smoke check → unchanged.
- [ ] **Step 5: Docs:** add §8c's opening to `docs/signal-processing.md` and its rows in the parameter table (section 10).
- [ ] **Step 6: Commit** "Bank port: filters and the envelope likelihood (signal-processing.md §8c)".

### Task 3: Noise

**Files:** Create `noise.hpp/.cpp`, `noise_test.cpp`; extend `golden.py` (`golden_noise`); Modify `docs/signal-processing.md` §8c (noise: three-tap guard κ = 1.75, κ_n = 4, truncation mean m(κ) = 0.632 derived; spectrum segments 171 ms, ±25 Hz smoothing, 20 ms guard margin, 50% clean fraction, per-branch mask bias b_mask,k measured; statuses as `params.py`).

**Interfaces:**
- Consumes: `filters.hpp`; the engine FFT `engine/src/fft.hpp`.
- Produces: `double guard_mean(double kappa)`; `class ThreeTapNoise { ThreeTapNoise(const BankConfig&, double rate_hz, std::vector<int> branch_n); void update(const Matrix& P, int n0, int n1); const std::vector<double>& var() const; }`; `class SpectrumNoise { ...; void update(std::span<const std::complex<double>> u, const Matrix& P, int n0, int n1); std::vector<double> sigma2() const; }`; `class BranchNoise` (the fallback, same interface); `std::unique_ptr<NoiseEstimator> make_noise(const BankConfig&, double rate_hz, std::vector<int> branch_n)` with `NoiseEstimator` the common base (`update`, `sigma2`). `Matrix` = row-major `std::vector<double>` with `rows = K`, `cols = samples`, defined in `filters.hpp`.

- [ ] **Step 1: Golden values:** run the prototype's `make_noise(ProtoConfig(), 1500, n)` (and with `noise_method="branch"`) over 20 s of a seeded channel-shaped noise stream with a keyed carrier (reuse `kz4ap_proto.testsignals.stream`, fixed seed) block by block exactly as `ChannelDecoder.run` calls it; record `sigma2()` after every 10th block, and the spectrum's accepted-segment count.
- [ ] **Step 2: Failing tests** comparing every recorded `sigma2` (relative 1e-9) and the segment counts (exact).
- [ ] **Step 3: Review Focus 3 test:** feed 5 s of exact zeros, then noise; assert every `sigma2` value is finite and positive after the warm-up and no NaN appears at any time (`std::isfinite`).
- [ ] **Step 4: Port** `noise.py` line for line (Hann window, periodogram, smoothing, mask, segment acceptance, the exponential average, the bias division, the warm-up's 20% quantile with the same quantile definition as numpy's default "linear").
- [ ] **Step 5:** Tests PASS; full engine tests and smoke unchanged.
- [ ] **Step 6: Docs** (§8c noise) and **commit** "Bank port: noise (three-tap level, spectrum shape, mask bias)".

### Task 4: Keying

**Files:** Create `keying.hpp/.cpp`, `keying_test.cpp`; extend `golden.py`; Modify `docs/signal-processing.md` §8c (amplitude EM τ_a = 0.5 s of key-down weight, P₁ = 0.44, ±1 nat hysteresis, squelch a_min,k = 3·(L_k/16 ms)^(1/4), unknown-amplitude test with x_on,k (measured, E9a) and x_off = 1.55, W_min = 0.8 s, seed memory, re-key).

**Interfaces:**
- Consumes: `filters.hpp`, `noise.hpp`.
- Produces: `std::vector<int> hysteresis(std::span<const double> down, std::span<const double> up, int initial)`; `std::vector<std::vector<std::pair<int,bool>>> edges(const Matrix& key, const std::vector<int>& before, int n0)`; `class BankKeyer` with the prototype's members and methods (`step(const Matrix& P, const std::vector<double>& sigma2)` returning `KeyStep{Matrix key; Matrix p; std::vector<int> before; std::vector<double> a;}`, `ready_to_rekey()`, `start_over(int k)`, `finish_over_start(int k, double amp2, bool key_now)`, and the public state the channel reads: `unknown`, `prev_amp2`, `a_min`, `rekey_weight`, `weight`); `std::vector<int> rekey(std::span<const double> P, double sigma2, double amp2, const BankConfig&, double a_min)`.

- [ ] **Step 1: Golden values:** drive the prototype's `BankKeyer` with the Task 3 golden stream's filter powers and noise for 20 s, including two over starts (call `start_over` at fixed indices as `ChannelDecoder.run` would); record per block the key matrix's changed indices, `a`, `unknown`, and the edges; plus `rekey` on a fixed segment.
- [ ] **Step 2: Failing tests:** keyed indices and edges exact; `a` relative 1e-9.
- [ ] **Step 3: Port** `keying.py` line for line (vectorized hysteresis as a per-sample loop with identical semantics).
- [ ] **Step 4:** PASS; smoke unchanged. **Docs** §8c keying; **commit** "Bank port: keying, amplitude, squelch and the unknown-amplitude test".

### Task 5: Duration fit

**Files:** Create `fit.hpp/.cpp`, `fit_test.cpp`; extend `golden.py`; Modify `docs/signal-processing.md` §8c (classes and priors, log-normal plus timing-resolution term σ_t², outlier class 1 ms–10 s log-uniform, memory 48 elements λ = e^(−1/48), grid: T 1% steps 12–240 ms, q ∈ {3, 4, 5}, w/T ∈ {−0.4, 0, 0.4, 0.8}, T_g/T ∈ {1, 1.59, 2.52, 4, 6.35} (measured, E5), 2 Gauss–Newton steps in ln d with damping 0.2 T, the T_P prior width 0.1 in ln T, classification).

**Interfaces:**
- Produces: `struct Fit { double t_s, w_s, q_t_s, t_g_s; double loglik; std::array<double,4> theta() const; }`; `double resolution_var_s2(double length_s, double a, double rate_hz)`; `class DurationFit { explicit DurationFit(const BankConfig&); DurationFit copy() const; void add(bool is_mark, double duration_s, double var_t); std::optional<std::array<double,4>> grid_theta(std::optional<double> prior_t_s, double prior_weight) const; double weighted_loglik(const std::array<double,4>&, std::optional<double> prior_t_s, double prior_weight) const; std::optional<Fit> best(std::optional<double> prior_t_s, double prior_weight) const; double weight() const; }`; `bool classify_mark(const Fit&, double d, double var_t, const BankConfig&)`; `std::string classify_space(const Fit&, double d, double var_t, const BankConfig&)` ("element", "character", "word"); `double observations_loglik(const Fit&, const std::vector<Obs>&, const BankConfig&)`.

- [ ] **Step 1: Golden values:** a sequence of 300 observations (marks and spaces from `keying_intervals` of a fixed text at 25 WPM then 15 → 30 WPM, with timing jitter from a fixed seed, plus one tune-up outlier); after observations 1, 2, 8, 48, 100, 300 record the full grid tables (mark and space), `grid_theta` with and without a T_P prior (0.05 s, weight 3), `best`, and the classifications of 20 probe durations.
- [ ] **Step 2: Failing tests:** tables relative 1e-9 (both tables in full), `grid_theta` exact (indices), `best` theta relative 1e-9, classifications exact.
- [ ] **Step 3: Port** `fit.py` (the grid, `_grid_loglik`'s formula and class order; `logaddexp` as numpy's `max + log1p(exp(-|x-y|))`; the retained history of `ceil(4·N_mem)` observations; `_refine`'s EM-style Gauss–Newton with the clipping bounds; the accept-if-better rule).
- [ ] **Step 4:** Port the Python fit tests' assertions too (`training/tests/test_proto_fit.py`: one C++ test per Python test, same inputs and assertions; strict xfails become `GTEST_SKIP()` with the same reason).
- [ ] **Step 5:** PASS; **docs** §8c fit; **commit** "Bank port: the duration fit".

### Task 6: Periodicity (comb on 2T), text model and selection

**Files:** Create `periodicity.hpp/.cpp`, `selection.hpp/.cpp`, their tests; extend `golden.py`; Modify `docs/signal-processing.md` §8c (comb on Π = 2T, 4 teeth ±15% of T, windows 2/5/10 s, update every 0.25 s, p averaged to 750 samples/s, confidence threshold 0.03 (placeholder); text model; eligibility ln 1.1, M = 4, ε_Q = 0.05 nats per element, text window 10 characters).

**Interfaces:**
- Produces: `std::vector<double> t_grid(const BankConfig&)`; `std::pair<std::optional<double>,double> comb_estimate(std::span<const double> p, double rate_hz, const std::vector<double>& grid, int teeth, double width)`; `class Periodicity { Periodicity(const BankConfig&, double rate_hz); void push(std::span<const double> p); PeriodicityUpdate update(bool force); const std::vector<std::pair<std::optional<double>,double>>& per_window() const; int update_every() const; }` (only the comb; `periodicity_method` other than "comb" throws `std::invalid_argument`); `std::string decode_pattern(const std::string&)`; `class TextModel { double char_logprob(const std::string&) const; std::optional<double> mean_logprob(const std::vector<std::string>&) const; }`; `struct BranchView` and `class Selector` with `eligible`, `best`, `update(const std::vector<BranchView>&, int instants, double t_now, std::optional<double> prior_t_s) -> int`, `eligible_since`.

- [ ] **Step 1: Golden values:** the comb on branch-1 posteriors from a fixed keyed stream at 12, 25, 40 WPM (every update: estimate, confidence, per-window values); the text model on 30 symbols and 5 strings; the selector on a scripted sequence of 40 views (copy the inputs from `test_proto_select.py`'s scenarios).
- [ ] **Step 2: Failing tests**, **Step 3: port** `periodicity.py` (comb only: `_normalized_acf`, `comb_estimate`, `Periodicity`), `text.py`, `select.py`; **Step 4:** port the Python tests' assertions; **Step 5:** PASS; **docs**; **commit** "Bank port: periodicity (comb), text model and branch selection".

### Task 7: The channel decoder, output with corrections, and the replay tool

**Files:** Create `channel.hpp/.cpp`, `channel_test.cpp`, `bench/src/bank_json.hpp/.cpp`, `bench/src/replay.cpp` and its target `kz4ap-bank-replay`; extend `golden.py`; Modify `docs/signal-processing.md` §8c (block cadence 21.3 ms, over start T_new = max(0.5 s, 12·T_g), re-key and time-out 2 s, fresh fit against the previous (8 observations, ½·k·ln n), corrections reaching 20 s, the overlap cut).

**Interfaces:**
- Consumes: Tasks 2–6.
- Produces: `struct Char { std::string text; double start_s, end_s; }`; `struct Correction { double t_s, from_s, reach_s; std::string old_text, new_text, reason; }`; `class Output { explicit Output(double reach_s); void append_new(const std::vector<Char>&); void replace_from(double from_s, const std::vector<Char>&, double t_s, const std::string& reason); std::string text() const; const std::vector<Char>& chars() const; const std::vector<Correction>& corrections() const; }`; `class BankChannel { BankChannel(const BankConfig&, double rate_hz); void push(std::span<const std::complex<double>> u); void finish(); ChannelResult result() const; }` — **streaming**: `push` accepts any number of samples and processes complete blocks (32 samples at 1500 samples/s, `block_s` converted at the point of use), carrying the remainder; `finish` processes the remainder exactly as the prototype's `run` handles its last partial block. `ChannelResult` with the prototype's fields (`text, chars, corrections, selections, periodicity, over_starts, switches`); its JSON form, in `bench/src/bank_json.cpp` (`nlohmann::json to_json(const ChannelResult&)`, plus `bank_config_from_json` and `to_json(const BankConfig&)`), reproduces the prototype's `to_json` byte for byte where values agree (same rounding: 4 or 6 decimals as there). Replay: `kz4ap-bank-replay --out DIR --name NAME [--only REGEX] [--set KEY=VALUE ...] [--jobs N]` reads `DIR/manifest.json`, the oracle test cases and `DIR/channels/<recording>/channels.json`, mixes each channel exactly as `streams.ChannelStream.baseband()` (port that function: labeled offset and drift from the label's start), decodes, and writes `DIR/proto/NAME/<result>.decoded.json` in `runner.decode`'s format (same keys, `config` = the BankConfig as JSON, `cpu_s`, `channel_s`).

- [ ] **Step 1: Golden values:** the prototype's `ChannelDecoder(ProtoConfig(), 1500).run(u)` on six fixed streams: a clean 25 WPM CQ; the same-speed turnover; noise after the last over; a 15 → 30 WPM step; Farnsworth 18/10; zero-padded start (Review Focus 3). Record the full `to_json()`.
- [ ] **Step 2: Failing tests:** `BankChannel` fed the same streams in one `push` gives the same `text`, `chars` (start and end to 1e-4 s), `corrections` and `over_starts`.
- [ ] **Step 3: Review Focus 1 test:** the same streams pushed in blocks of 1, 47 and 1000 samples give identical results to the single push.
- [ ] **Step 4: Review Focus 2 test:** a stream cut mid-character and mid-over: `finish()` publishes the partial character as the prototype does and no correction refers past the end.
- [ ] **Step 5: Review Focus 4 test:** `Output::replace_from` with `from_s` before the first character and more than 20 s back: the cut is clipped to `t_s − reach_s` and to the first character, as in the prototype.
- [ ] **Step 6: Review Focus 5 test:** a stream resampled to 2000 samples/s (golden from the prototype at 2000) decodes the same text.
- [ ] **Step 7: Port** `channel.py` (`Branch`, over handling, `rekey_over`, `clear_over`, `_redecode`, `Output`, `ChannelDecoder.run` as `BankChannel`), then the replay tool.
- [ ] **Step 8:** Port the Python channel tests' assertions (`test_proto_channel.py`; strict xfails → `GTEST_SKIP()` with their reasons, including E4's and E9's recorded findings).
- [ ] **Step 9:** PASS; smoke unchanged; **docs** §8c channel; **commit** "Bank port: the channel decoder with corrections, and the replay tool".

### Task 8: Engine and bench plumbing — `--decoder bank`, corrections, immediate text

**Files:** Create `engine/include/kz4ap/bank_decoder.hpp`, `engine/src/bank_decoder.cpp`, `engine/tests/bank_decoder_test.cpp`; Modify `decoder.hpp`, `event_bus.hpp`, `classical_decoder.hpp` (`FrontEnd::Bank`), `engine.cpp` (create `BankDecoder` when `front_end == Bank`), `bench/src/main.cpp`, `bench/src/report.cpp/.hpp`, `bench/tests/report_test.cpp`, `training/kz4ap_synth/suites.py` (accept `--front-end bank`, results folder `results/bank`), `docs/signal-processing.md` (§8c: the bank behind the engine, anchor mixing; the event format with corrections).

**Interfaces:**
- Produces: `struct TextCorrection { std::size_t from_index; std::vector<DecodedSymbol> chars; double t_s; std::string reason; };` in `decoder.hpp`; `DecodeUpdate::corrections` and `DecodedTextEvent::corrections` (`std::vector<TextCorrection>`, default empty); `class BankDecoder : public Decoder` (constructor `(double rate_hz, const BankConfig&, double residual_hz)`; mixes each block by the latest anchor, as `streams.anchored_baseband` does for detector channels and the label for oracle channels); bench JSON per track: `"text"` (final, as now) and `"text_immediate"`; the bench report scores both (`cer`, `cer_immediate`). **Owner decision D1 (2026-10-03):** the bench's option becomes `--decoder envelope|matched|bank`, with `--front-end` kept as an alias so existing scripts keep working; `kz4ap_synth.suites` and `kz4ap_proto.runner` pass `--decoder`. The JSON key `front_end` gains a twin `decoder` (same value) and keeps its old name for the stage-1 tooling. In prose and docs, Envelope, Matched and the bank are **decoders**, never "front ends" (owner: to him the front end is the detector); `FrontEnd` stays only as an internal C++ identifier.

- [ ] **Step 1: Failing test** (`bank_decoder_test.cpp`): a `BankDecoder` fed a stream through the engine's block interface publishes, after applying its `TextCorrection`s by index, exactly `BankChannel`'s final text for the same mixed stream; Envelope and Matched updates carry no corrections.
- [ ] **Step 2: Failing bench test** (`report_test.cpp`): assembling `["C","Q"," ","D"]` then a correction `{from_index: 1, chars: ["Q","E"]}` gives final "CQE" and immediate "CQ D".
- [ ] **Step 3: Implement** the plumbing; Envelope and Matched code paths untouched except passing empty corrections.
- [ ] **Step 4:** All tests PASS; **`bash bench/smoke.sh build/windows`** passes with unchanged CERs; `git diff --stat main -- bench/baselines` shows only what milestone 2 already added (no change from this task).
- [ ] **Step 5: Docs** and **commit** "The bank decoder behind the engine: `--decoder`, corrections in events, final and immediate text in the bench".

### Task 9: Reference check and cost (Linux machine)

**Files:** Modify `docs/plans/2026-10-03-milestone-2c-bank-results.md` (create: the results record of this plan, sections: conditions, reference check, cost, full-suite run); no code unless a defect is found (then fix in the owning module with a test, in its own commit).

- [ ] **Step 1:** Bundle the branch to the Linux machine (`server-workflow.md`), build the `linux` preset there (`cmake --preset linux`, `cmake --build build/linux`), run `ctest`.
- [ ] **Step 2:** As a job: `build/kz4ap-job start replay-dev build/linux/bench/kz4ap-bank-replay --out build/suite/full3 --name bank-cpp --only "<the development set's regex from kz4ap_proto.experiments.DEV>" --jobs 10`.
- [ ] **Step 3:** Compare: `PYTHONPATH=training .venv/bin/python -m kz4ap_proto.runner score --out build/suite/full3 --bench build/linux/bench/kz4ap-bench --name bank-cpp --only "<DEV>"` then `... -m kz4ap_proto.experiments compare --out build/suite/full3 --base bank-proto --variant bank-cpp` (the base is the prototype's final decoded files, which cover seed 1). Also a channel-by-channel text diff (a small git-ignored helper): count of identical texts, and for each differing channel the first differing character and its time.
- [ ] **Step 4: Apply the pass criteria** (Design decisions, D2): trace every channel whose text differs to a near-tie, with the two candidates' values; report the pooled CER and the paired CER interval as information. If they fail, debug (superpowers:systematic-debugging) module by module with the golden tests before changing anything; report to the controller if a difference is a prototype ambiguity rather than a port defect.
- [ ] **Step 5: Cost:** CPU per channel-second from the decoded files' `cpu_s` and `channel_s` (pooled, and per group), on the Linux machine, exact-math build; compare with the spike's projection (about 65 ms) and the prototype's 157 ms.
- [ ] **Step 6:** Record both in the results document; **commit** "Bank port: reference check against the prototype and measured cost".

### Task 10: Full-suite run through the engine

**Files:** Modify the Task 9 results document; `docs/signal-processing.md` §8c (the measured section, like §8b's "Measured: Matched against Envelope").

- [ ] **Step 1:** As a job on the Linux machine: `PYTHONPATH=training .venv/bin/python -m kz4ap_synth.suites run --out build/suite/full3 --bench build/linux/bench/kz4ap-bench --decoder bank` (every recording: oracle test cases and through the detector path, all three seeds).
- [ ] **Step 2:** Summarize with `kz4ap_synth.suites summarize`; paired comparisons bank − matched and bank − envelope per group (the suite's existing paired tables), and bank (engine) − bank-proto on the oracle test cases.
- [ ] **Step 3:** Record in the results document: pooled and per-group CER with intervals, the displayed-text measure (`cer_immediate` and `cer` per group, beside Matched and Envelope, for which they are equal), correction statistics, CPU per channel-second through the engine. No verdict; the owner reads it.
- [ ] **Step 4:** Update §8c's measured section; **commit** "The bank decoder: first full-suite measurement".

---

## Self-review

**Spec coverage.** Stage-1 spec §3–§4 components: Tasks 2 (§4.1, §4.3 LLR), 3 (§4.2), 4 (§4.3), 5 (§4.5), 6 (§4.4 comb, §4.6), 7 (§4.7, §4.8). Stage-1 spec §7 stage 2 "a new selectable front end `--front-end bank` … corrections in the engine's text events and in the bench's scoring": Task 8. Stage-2 spec §4.2 (displayed text: immediate and final CER, correction statistics): Tasks 8, 10. Stage-2 spec §4.4 (cost measured before the stage-2 plan): the spike (before this plan) and Task 9 (the port). Not in this plan, by design: stage-2 spec §3 (Plan B), §4.1 stretch test, §4.3 new-over checks, §5 investigations, the frequency tracker in the loop and the detection measures behind the live detector (Plan B).

**Placeholder scan.** Each port step names the exact Python source it translates, which is the specification; golden values and tolerances are stated; the pass criteria and the budget are numbers. The one open number, the port's measured cost, is Task 9's output.

**Type consistency.** `BankConfig` (Task 1) is consumed by every module; `Matrix` (Task 2) by Tasks 3–4; `KeyStep` (Task 4) and `Fit`/`DurationFit` (Task 5) by Task 7; `PeriodicityUpdate`, `BranchView`, `Selector` (Task 6) by Task 7; `Char`, `Correction`, `ChannelResult` (Task 7) by Task 8's `BankDecoder` and the replay tool; `TextCorrection` (Task 8) by the bench.

**Review Focus.** Five items, each with its test: Task 3 (zeros), Task 7 (block split, finish, correction clipping, other rate), Task 8 (bench assembly by index).
