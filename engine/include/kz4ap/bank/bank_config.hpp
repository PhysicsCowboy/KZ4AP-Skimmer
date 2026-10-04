// The bank decoder's configuration: every ProtoConfig field (training/kz4ap_proto/params.py) that the
// prototype's decoder modules read, with the prototype's settled default, unit and status (owner,
// measured, heuristic or placeholder). The port is faithful: change a value only together with the
// prototype's. Parameters are in physical units (Hz, s, FS, nats, dB with a named reference); lengths
// become samples only at the point of use. Defaults are checked against the prototype's by
// engine/tests/bank/config_test.cpp (golden file engine/tests/data/bank/config.json). Plan B adds fields the
// prototype does not have (marked "Plan B"); their defaults are checked by the same test file. Plan B's B4a
// replaced three of the prototype's fields in seconds by fields in dits (rekey_after_dits, periodicity_windows_dits,
// rekey_timeout_ratio); timing.hpp converts them to seconds per branch and per candidate dit.
#pragma once

#include <cmath>
#include <string>
#include <vector>

namespace kz4ap::bank {

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
    // b_mask,k, one per branch, dimensionless; measured (white noise, seeds 101-110); valid only for the defaults
    // of the ladder, segment_s, spectrum_smoothing_hz, guard_margin_s, min_clean_fraction, neighbor_guard and the
    // three-tap settings at 1500 samples/s
    std::vector<double> mask_bias = {
        0.837, 0.8293, 0.8263, 0.8213,
        0.8171, 0.8136, 0.8095, 0.8074,
        0.8047, 0.8025, 0.8008, 0.7988,
        0.7973, 0.7958, 0.7945, 0.7935,
        0.7924, 0.7914, 0.7907, 0.7899,
        0.7892, 0.7885, 0.788, 0.7875,
        0.7871, 0.7867, 0.7864, 0.7861,
        0.7858, 0.7856, 0.7854, 0.7852,
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
    // s, the spectrum's mark flag reaches this far; heuristic; mask_bias measured with this default
    double guard_margin_s = 0.02;
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
    // nominal dits of branch k, d_k = L_k / length_dits (the nominal L_k: d_k = 12 ms ... 230 ms): W_min,k =
    // rekey_after_dits x d_k of keyed time while the amplitude is unknown. Class (1) dits: it collects enough marks to
    // seed the amplitude, and the marks a branch is matched to last a number of its dits. Status: derived from a
    // measurement, the prototype's 0.8 s (measured at 25 WPM by E9b) over 48 ms = 16.7 (stage-2 spec section 3.1).
    // Plan B (B4a), replaces rekey_after_s
    double rekey_after_dits = 16.7;
    // multiples of W_min,k, bound on the seed's memory of keyed time; heuristic
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
    // N_w, dimensionless: each candidate dit T of the comb is judged over a window of N_w x T, one window per value.
    // Class (1) dits: the comb needs enough dit periods (its reach is 9.15 T). Status: placeholder, stage 1's 2, 5
    // and 10 s at T = 48 ms (stage-2 spec section 3.1). Plan B (B4a), replaces periodicity_windows_s
    std::vector<double> periodicity_windows_dits = {41.7, 104.0, 208.0};
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
    // multiples of W_min,k, dimensionless: the channel time branch k's over may stay of unknown amplitude before it is
    // re-keyed or cleared is rekey_timeout_ratio x W_min,k = 41.7 d_k. Class (1) dits, through W_min,k: a station is
    // keyed down about 44% of the time, so it needs about W_min,k / 0.44 of channel time to reach W_min,k. Status:
    // heuristic, stage 1's ratio 2 s / 0.8 s (stage-2 spec section 3.1). Plan B (B4a), replaces rekey_timeout_s
    double rekey_timeout_ratio = 2.5;
    // marks and spaces of this over a fresh fit needs before it may replace the previous over's; placeholder, heuristic
    int fresh_fit_min_obs = 8;
    // s; owner
    double correction_reach_s = 20.0;
};

}  // namespace kz4ap::bank
