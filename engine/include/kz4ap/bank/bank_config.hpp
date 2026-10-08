// The bank decoder's configuration: every field of the stage-1 prototype's ProtoConfig that its decoder modules read
// (training/kz4ap_proto/params.py, removed on 2026-10-07: engine/tests/data/bank/README.md), with the prototype's
// settled default, unit and status (owner, measured, heuristic or placeholder). Parameters are in physical units (Hz,
// s, FS, nats, dB with a named reference); lengths become samples only at the point of use. Defaults are checked
// against the prototype's by engine/tests/bank/config_test.cpp (golden file engine/tests/data/bank/config.json).
// Plan B adds one field the prototype does not have (noise_stuck_s, marked "Plan B"), checked by the same test file.
// Plan B's B4b changed two prototype defaults, guard_margin_s (20 ms to 4.8 ms) and mask_bias (re-measured; B4d
// adopted the 200-seed measurement); stage 1's values are kStage1GuardMarginS and kStage1MaskBias. The owner's
// amendment of 2026-10-07 dropped the 2 s periodicity window (default 5 and 10 s); stage 1's windows are
// kStage1PeriodicityWindowsS. Plan B's experimental variants (the re-key wait and time-out in dits, periodicity
// windows in dits per candidate or following the selected branch's dit, the re-key clocks started at the first
// provisional mark with a lead, the re-key wait counted in marks and its two guards) were measured, off by default,
// and removed on 2026-10-07 when the bank was frozen as the reference (results:
// docs/plans/2026-10-04-milestone-2c-plan-b-results.md, sections 6 and 10 to 13; the code in git history).
#pragma once

#include <cmath>
#include <stdexcept>
#include <string>
#include <vector>

namespace kz4ap::bank {

// Stage 1's b_mask,k (prototype Task 5: white noise, seeds 101-110, 60 s each, 20 ms guard margin), dimensionless;
// with guard_margin_s = kStage1GuardMarginS, the prototype's configuration (golden tests, ablations).
inline constexpr double kStage1GuardMarginS = 0.02;
inline const std::vector<double> kStage1MaskBias = {
    0.837, 0.8293, 0.8263, 0.8213, 0.8171, 0.8136, 0.8095, 0.8074, 0.8047, 0.8025, 0.8008,
    0.7988, 0.7973, 0.7958, 0.7945, 0.7935, 0.7924, 0.7914, 0.7907, 0.7899, 0.7892, 0.7885,
    0.788, 0.7875, 0.7871, 0.7867, 0.7864, 0.7861, 0.7858, 0.7856, 0.7854, 0.7852,
};

// Stage 1's periodicity windows (the prototype's periodicity_windows_s), s: 2, 5 and 10 s; the default until the
// owner's amendment of 2026-10-07 (golden tests, ablations: --set periodicity_windows_s=[2,5,10]).
inline const std::vector<double> kStage1PeriodicityWindowsS = {2.0, 5.0, 10.0};

struct BankConfig {
    // words/min; owner
    double min_wpm = 5.0;
    // words/min; owner
    double max_wpm = 100.0;
    // ratio of neighboring branch speeds, dimensionless; owner; mask_bias measured with this default
    double ladder_step = 1.1;
    // branch filter length, dits; heuristic; mask_bias measured with this default
    double length_dits = 0.8;
    // s: estimates advance once per block (32/1500 s = 21.33 ms, the engine's channel block); heuristic
    double block_s = 32.0 / 1500.0;
    // "spectrum" (three-tap level x spectrum ratios), "spectrum-level" or "branch"; measured (E10): "spectrum"
    std::string noise_method = "spectrum";
    // b_mask,k, one per branch, dimensionless; measured (Plan B B4b, white noise, 200 seeds): the C++ estimate at
    // guard_margin_s = 4.8 ms on kz4ap-noise-mask's white noise (std::mt19937_64, Box-Muller, 1 FS^2 per complex
    // sample), seeds 1001-1200, 60 s each, rounded to four decimals; standard error about 0.2% (per-seed scatter 1.9%
    // to 3.2%, over sqrt(200)). Adopted by Plan B's B4d (owner, 2026-10-06) in place of B4b's ten-seed table (stage 1's
    // numpy noise, seeds 101-110: 0.8281 ... 0.7713, 1.2% to 1.5% lower, mostly the ten seeds' sampling error). Valid
    // only for the defaults of the ladder, segment_s, spectrum_smoothing_hz, guard_margin_s, min_clean_fraction,
    // neighbor_guard and the three-tap settings at 1500 samples/s. Stage 1's table (20 ms margin) is kStage1MaskBias
    // above
    std::vector<double> mask_bias = {
        0.8381, 0.8304, 0.8273, 0.8221,
        0.8179, 0.8144, 0.8103, 0.808,
        0.8051, 0.8027, 0.8007, 0.7985,
        0.7967, 0.7949, 0.7934, 0.7921,
        0.7909, 0.7897, 0.7887, 0.7877,
        0.7869, 0.7861, 0.7854, 0.7847,
        0.7842, 0.7836, 0.7831, 0.7827,
        0.7823, 0.782, 0.7816, 0.7812,
    };
    // s, time constant of noise updates (tau_n); mask_bias measured with this default
    double noise_tau_s = 2.0;
    // s, first estimate: 20% quantile of |v|^2 over this; mask_bias measured with this default. Plan B (B3): of
    // non-zero input (exact zeros are missing data); the recovery below re-uses it
    double noise_warmup_s = 0.32;
    // s, of non-zero input: when branch 1's three-tap guard has accepted no tap for this long, every branch's
    // level is set again by the warm-up rule (the stuck-level recovery); 4 tau_n; heuristic. Seconds, not dits:
    // the noise has no keying speed. Plan B (B3), not a prototype field
    double noise_stuck_s = 8.0;
    // kappa, dimensionless; mask_bias measured with this default
    double noise_guard = 1.75;
    // kappa_n, dimensionless; also the spectrum's mark flag; mask_bias measured with this default
    double neighbor_guard = 4.0;
    // s, T_seg (bins 5.86 Hz wide); heuristic; mask_bias measured with this default
    double segment_s = 256.0 / 1500.0;
    // Hz, the shape is averaged over +/- this; heuristic; mask_bias measured with this default
    double spectrum_smoothing_hz = 25.0;
    // s, the spectrum's mark flag reaches this far: 0.5 L_1 (L_1 = 0.8 dits at max_wpm = 9.6 ms, branch 1's nominal
    // length), round(7.2) = 7 samples = 4.67 ms at 1500 samples/s. Class: seconds tied to branch 1's filter and the
    // transmitter's keying edges, not to the station's speed (stage-2 spec section 3.3; owner). Heuristic. mask_bias
    // measured with this default. Plan B (B4b); stage 1's was 0.02 s
    double guard_margin_s = 0.0048;
    // fraction, a segment enters the spectrum only if this much of it is unflagged; heuristic
    double min_clean_fraction = 0.5;
    // s, tau_a, time constant of key-down weight; milestone 2
    double amplitude_tau_s = 0.5;
    // P1, probability (PARIS)
    double prior_key_down = 0.44;
    // nats, h; heuristic
    double hysteresis_nats = 1.0;
    // a_min at squelch_ref_s, dimensionless; heuristic
    double squelch_a = 3.0;
    // s
    double squelch_ref_s = 0.016;
    // a_min proportional to L^(1/4), dimensionless; derived scaling
    double squelch_exponent = 0.25;
    // 1/s, R_fa target per branch, noise alone; heuristic target, kept by E9b
    double false_marks_per_s = 0.01;
    // x_on per branch k = 1 ... 32, dimensionless; measured (E9a, channel-shaped noise, R_fa 0.01 /s); must match
    // false_marks_per_s
    std::vector<double> x_on_values = {
        4.6428, 4.637, 4.6206, 4.6163,
        4.6196, 4.6047, 4.5827, 4.5936,
        4.5548, 4.528, 4.5647, 4.5285,
        4.4602, 4.5024, 4.472, 4.4743,
        4.4149, 4.4676, 4.4935, 4.4207,
        4.3846, 4.2876, 4.3223, 4.3391,
        4.3002, 4.3336, 4.2214, 4.2471,
        4.2129, 4.2335, 4.2228, 4.2036,
    };
    // probability, key up where noise alone exceeds x this often (x_off = 1.55); heuristic
    double release_probability = 0.3;
    // s, W_min: the keyed (key-down) time a branch collects while its amplitude is unknown before the over's start is
    // re-keyed, the same for every branch; also sets the seed's memory (seed_memory_rekeys x W_min). Must be positive.
    // Class: seconds. Status: measured (E9b, stage 1: 0.8 s adopted over 0.4 s and 0.2 s at 25 WPM); the default again
    // since Plan B's B4g (owner, 2026-10-06, option a')
    double rekey_after_s = 0.8;
    // multiples of rekey_after_s, bound on the seed's memory of keyed time; heuristic
    double seed_memory_rekeys = 4.0;
    // N_mem, marks and spaces; measured (E4)
    double fit_memory = 48.0;
    // relative step of the T grid, dimensionless; placeholder, kept by E5
    double t_grid_step = 0.01;
    // measured (E5), "coarse"
    std::vector<double> q_grid = {3.0, 4.0, 5.0};
    // w/T, dimensionless; measured (E5), "coarse"
    std::vector<double> w_grid = {-0.4, 0.0, 0.4, 0.8};
    // T_g/T, dimensionless; measured (E5), "coarse"
    std::vector<double> tg_grid = {1.0, 1.59, 2.52, 4.0, 6.35};
    // width in ln(duration), dimensionless; heuristic
    double sigma_ln_mark = 0.15;
    // width in ln(duration), dimensionless; heuristic
    double sigma_ln_space = 0.25;
    // epsilon, probability; heuristic
    double outlier_prior = 0.05;
    // s, {low, high} of the log-uniform outlier class; heuristic
    std::vector<double> outlier_range_s = {0.001, 10.0};
    // width of the T_P prior in ln T, dimensionless; heuristic
    double prior_sigma_ln = 0.1;
    // Gauss-Newton steps in ln(duration) after the grid; heuristic
    int refine_iterations = 2;
    // elements of memory weight before a fit counts for eligibility; heuristic
    double min_fit_weight = 8.0;
    // "comb" (on Pi = 2T; owner), "edge" or "spectrum"; E1
    std::string periodicity_method = "comb";
    // s, the periodicity windows: every candidate dit tau_c of the comb is judged over the same windows (stage 1's
    // rule). Class (2) seconds, a latency (owner, 2026-10-06; stage-2 spec sections 3.2 and 6): several windows exist
    // so that the estimate reacts quickly after a change (a stream's start, a new over, a speed change), and how
    // quickly is felt in seconds; the shortest confident window wins. The part that depends on speed is physics any
    // rule must wait for: a window W can measure only tau_c <= W / 18.3 (the comb's reach, (4.5 + 0.075) x 2 tau_c <=
    // W / 2), so 2 s reaches down to 11 WPM, 5 s to 4.4 WPM and 10 s to 2.2 WPM. Owner, 2026-10-07 (stage-2
    // decisions, section 7): the 2 s window is dropped, the default is 5 and 10 s. Reason: below about 10 WPM the 2 s
    // window cannot score the true dit (reach 109 ms) and returns a false match near a third of the dit, scoring just
    // above comb_confidence_min (0.03); as the shortest confident window it sets T_P, and the T_P prior pulls the
    // fits to about half the dit (measured, the development set's slow trace). Diagnostic replay with 5 and 10 s
    // alone (development-set pilot, speed cell 8 to 10 WPM, S500 >= +4 dB SNR in 500 Hz, 32 signals): mean CER 0.008
    // against 0.497 with 2, 5 and 10 s. Cost: a window scores only once full, so the first T_P comes after 5 s of the
    // channel's stream instead of 2 s, and after a speed change the estimate follows over 5 s rather than 2 s
    // (derived). Stage 1's windows remain reachable (kStage1PeriodicityWindowsS; --set
    // periodicity_windows_s=[2,5,10]). Status: placeholder (E1), 2 s dropped on the measured failure
    std::vector<double> periodicity_windows_s = {5.0, 10.0};
    // s; heuristic
    double periodicity_update_s = 0.25;
    // samples/s, p is averaged down to this rate; heuristic
    double periodicity_rate_hz = 750.0;
    // count, teeth at k Pi (comb) or k T (edge comb); placeholder (E3)
    int comb_teeth = 4;
    // comb tooth half-width, fraction of Pi, dimensionless; the edge comb uses 2 x this, fraction of T;
    // placeholder (E3)
    double comb_width = 0.075;
    // count; placeholder (E3)
    int spectrum_nulls = 3;
    // band width x T, dimensionless; placeholder (E3)
    double spectrum_null_width = 0.15;
    // dB relative to the first branch's DC power gain (boxcar, then averaging to periodicity_rate_hz); heuristic
    double spectrum_front_floor_db = -20.0;
    // dimensionless; placeholder (E1)
    double comb_confidence_min = 0.03;
    // dimensionless; placeholder (E1)
    double edge_confidence_min = 0.03;
    // nats; placeholder (E1)
    double spectrum_confidence_min = 1.5;
    // ln(duration ratio), dimensionless: one ladder step; heuristic
    double eligibility_tolerance = std::log(1.1);
    // M, selection instants in a row; placeholder, kept by E6
    int switch_persistence = 4;
    // epsilon_Q, nats per element; placeholder, kept by E8
    double quality_tie_nats = 0.05;
    // nats per character; heuristic
    double text_tie_nats = 0.1;
    // characters; placeholder, kept by E8
    int text_window_chars = 10;
    // nats per character, "clearly separates" with none eligible; heuristic
    double text_separation_nats = 1.0;
    // s; placeholder, kept by E7
    double new_over_min_s = 0.5;
    // multiples of T_g, dimensionless; placeholder, kept by E7
    double new_over_gaps = 12.0;
    // s of channel time an over's amplitude may stay unknown before it is re-keyed at the previous over's amplitude or
    // cleared, the same for every branch, counted from where the amplitude became unknown (and again from each time-out
    // that keyed nothing). Must be positive. Class: seconds. Status: heuristic (stage 1); the default again since Plan
    // B's B4g (owner, 2026-10-06, option a')
    double rekey_timeout_s = 2.0;
    // marks and spaces of this over a fresh fit needs before it may replace the previous over's; placeholder, heuristic
    int fresh_fit_min_obs = 8;
    // s; owner
    double correction_reach_s = 20.0;
};

// Throws std::invalid_argument unless rekey_after_s and rekey_timeout_s are positive (s), as they are by default: a 0
// once selected Plan B's re-key variants in dits and in marks, which were removed (2026-10-07), and is refused rather
// than read as a zero wait or time-out. BankChannel's constructor calls it, and kz4ap-bank-replay calls it once on
// its --set values before it reads any channel.
inline void check_rekey_times(const BankConfig& cfg) {
    if (!(cfg.rekey_after_s > 0.0)) throw std::invalid_argument("rekey_after_s must be positive (s)");
    if (!(cfg.rekey_timeout_s > 0.0)) throw std::invalid_argument("rekey_timeout_s must be positive (s)");
}

}  // namespace kz4ap::bank
