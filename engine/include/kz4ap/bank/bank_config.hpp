// The bank decoder's configuration: every ProtoConfig field (training/kz4ap_proto/params.py) that the
// prototype's decoder modules read, with the prototype's settled default, unit and status (owner,
// measured, heuristic or placeholder). The port is faithful: change a value only together with the
// prototype's. Parameters are in physical units (Hz, s, FS, nats, dB with a named reference); lengths
// become samples only at the point of use. Defaults are checked against the prototype's by
// engine/tests/bank/config_test.cpp (golden file engine/tests/data/bank/config.json). Plan B adds fields the
// prototype does not have (marked "Plan B"); their defaults are checked by the same test file. Plan B's B4a
// replaced two of the prototype's fields in seconds by fields in dits (rekey_after_dits, rekey_timeout_ratio) and
// added periodicity windows in dits; B4d (the owner's decisions of 2026-10-06) put the periodicity windows back in
// seconds (periodicity_windows_s, the prototype's 2, 5 and 10 s; the windows in dits are variants) and added two
// switches for when the re-key clocks start. timing.hpp converts the values in dits to seconds per branch and per
// candidate dit. Plan B's B4b changed two prototype defaults, guard_margin_s (20 ms to 4.8 ms) and mask_bias
// (re-measured; B4d adopted the 200-seed measurement); stage 1's values are kStage1GuardMarginS and kStage1MaskBias.
#pragma once

#include <cmath>
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
    // Overrides in seconds of B4a's re-key time constants in dits, for ablations (Plan B, B4a; default unset). When set,
    // they take precedence over the fields in dits: rekey_after_s > 0 sets W_min,k = rekey_after_s of keyed time for
    // every branch (instead of rekey_after_dits x d_k), rekey_timeout_s > 0 the re-key time-out to rekey_timeout_s of
    // channel time for every branch (instead of rekey_timeout_ratio x W_min,k, or rekey_marks_timeout_s with
    // rekey_wait_in_marks). Stage 1's values are 0.8 s and 2 s. With rekey_wait_in_marks, W_min,k sets only the seed's
    // memory.
    // s; 0: unset
    double rekey_after_s = 0.0;
    double rekey_timeout_s = 0.0;
    // s, the periodicity windows of the default mode (periodicity_window_mode "seconds"): every candidate dit tau_c of
    // the comb is judged over the same window, one row per value (stage 1's rule and values; the prototype's field).
    // Class (2) seconds, a latency (owner, 2026-10-06; stage-2 spec sections 3.2 and 6): several windows exist so that
    // the estimate reacts quickly after a change (a stream's start, a new over, a speed change), and how quickly is
    // felt in seconds; the shortest confident window wins. The part that depends on speed is physics any rule must
    // wait for: a window W can measure only tau_c <= W / 18.3 (the comb's reach, (4.5 + 0.075) x 2 tau_c <= W / 2), so
    // 2 s reaches down to 11 WPM, 5 s to 4.4 WPM and 10 s to 2.2 WPM. Status: placeholder (stage 1's values, E1).
    // Plan B (B4d) made them the default again after B4a's windows in dits
    std::vector<double> periodicity_windows_s = {2.0, 5.0, 10.0};
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
    // nominal dits of branch k, d_k = L_k / length_dits (the nominal L_k: d_k = 12 ms ... 230 ms): W_min,k =
    // rekey_after_dits x d_k of keyed time. With rekey_wait_in_marks off (the B4a-B4d variant) the re-key waits for
    // W_min,k of keyed time while the amplitude is unknown; in every mode W_min,k sets the seed's memory
    // (seed_memory_rekeys). Class (1) dits: it collects enough marks to seed the amplitude, and the marks a branch is
    // matched to last a number of its dits. Status: derived from a measurement, the prototype's 0.8 s (measured at
    // 25 WPM by E9b) over 48 ms = 16.7 (stage-2 spec section 3.1). Plan B (B4a), replaces rekey_after_s
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
    // N_w, dimensionless, the windows in dits of the two variants of periodicity_window_mode below (not the default):
    // "per_candidate" judges each candidate dit tau_c over its own window N_w x tau_c (B4a), "shared" every candidate of
    // a row over N_w x T-hat (B4a-C). Class (1) dits. Status: placeholder, stage 1's 2, 5 and 10 s at 48 ms (stage-2
    // spec section 3.1, withdrawn for the default by the owner's decision of 2026-10-06). Plan B (B4a)
    std::vector<double> periodicity_windows_dits = {41.7, 104.0, 208.0};
    // How the periodicity windows apply (Plan B: B4a, B4a-C, B4d). Default "seconds" (B4d, the owner's decision of
    // 2026-10-06): the windows periodicity_windows_s, the same for every candidate (stage 1's rule). Variants for
    // experiments, not the default: "per_candidate" (B4a), each candidate tau_c over its own window N_w x tau_c of
    // periodicity_windows_dits; "shared" (B4a-C), every candidate of a row over one window N_w x T-hat, T-hat the dit
    // of the branch currently selected (its fitted T, only if that fit is eligible and is not a previous over's fit
    // held across a new over's start: at the stream's start the first over's fit counts once eligible; after a new
    // over's start, only once the over's start has been re-keyed), so candidates beyond N_w T-hat / 18.3 are out of
    // the comb's reach in that row; without such a T-hat the windows periodicity_unselected_windows_s. The two
    // variants do not use periodicity_windows_s
    std::string periodicity_window_mode = "seconds";
    // s, the "shared" variant's windows while there is no T-hat: stage 1's 2, 5 and 10 s. Plan B (B4a-C); unused in the
    // other modes
    std::vector<double> periodicity_unselected_windows_s = {2.0, 5.0, 10.0};
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
    // multiples of W_min,k, dimensionless; with rekey_wait_in_marks off (the B4a-B4d variant): the channel time branch
    // k's over may stay of unknown amplitude before it is re-keyed or cleared is rekey_timeout_ratio x W_min,k =
    // 41.75 d_k. Class (1) dits, through W_min,k: a station is
    // keyed down about 44% of the time, so it needs about W_min,k / 0.44 of channel time to reach W_min,k. Status:
    // heuristic, stage 1's ratio 2 s / 0.8 s (stage-2 spec section 3.1). Plan B (B4a), replaces rekey_timeout_s
    double rekey_timeout_ratio = 2.5;
    // When the re-key clocks start (Plan B, B4d; owner, 2026-10-06; found by the re-key trace,
    // docs/research/2026-10-05-periodicity-windows-and-rekey-analysis.md section 3.2). Each switch can be measured
    // alone; both off is stage 1's (and the prototype's) rule. Status: heuristic.
    // (i) A time-out that keys nothing (its provisional characters deleted, the amplitude still unknown) also moves
    // the start of the stretch a later re-key keys again to the time-out's instant, so that a re-key does not reach back
    // into the noise the time-out cleared. Off: the stretch starts where the amplitude became unknown, however many
    // time-outs cleared nothing since (the stream's start for the first over).
    bool rekey_clear_moves_stretch = true;
    // (ii) The time-out counts from the branch's first provisional mark (the key-down sample of the first mark the
    // unknown-amplitude test keys) since the amplitude became unknown or since the last time-out that keyed nothing;
    // before that mark nothing is counted. A new over started while the amplitude is still unknown does not restart
    // the count. Off: it counts from when the amplitude became unknown, a gap in seconds before an over's first mark,
    // so a time-out in dits could fire before the station had keyed W_min,k
    bool rekey_timeout_from_first_mark = true;
    // nominal dits of branch k (d_k), the lead of the re-key stretch under rekey_timeout_from_first_mark: when the count
    // starts at the first provisional mark, the stretch a later re-key keys again starts rekey_lead_dits x d_k before
    // that mark's key-down sample (or where it started, if later), so that the full rule can still find weak marks
    // just before the first provisional mark that the unknown-amplitude threshold missed, while noise further back
    // (before a station's start) is not keyed again. 7 d_k is one word gap in the branch's own dits. Class (1) dits:
    // the marks it must reach are the station's, and the branch is matched to a dit of d_k. Status: heuristic (Plan B,
    // B4d; the controller's choice for the owner, 2026-10-06). Unused when rekey_timeout_from_first_mark is off
    double rekey_lead_dits = 7.0;
    // The re-key wait counted in the station's marks (Plan B, B4e; the owner's decision of 2026-10-06, option d). On:
    // the over's start is re-keyed once the branch has rekey_marks provisional marks, and its time-out is
    // rekey_marks_timeout_s of channel time for every branch. Off: the B4a-B4d variant, W_min,k = rekey_after_dits x
    // d_k of keyed time and the time-out rekey_timeout_ratio x W_min,k. Why: the wait exists to collect enough of the
    // station's marks to choose the re-key's amplitude and seed the fit; in the branch's dits, a branch much faster
    // than the station (branch 1, d_1 = 12 ms, under a 25 WPM station) reached its wait after one or two of the
    // station's marks. Counting marks does not depend on the speed (by construction).
    bool rekey_wait_in_marks = true;
    // marks, the re-key wait K: provisional marks (keyed by the unknown-amplitude test, ended: key-down then key-up)
    // the branch has in its over (since the over started or since the last time-out that keyed nothing) before the
    // over's start is re-keyed. Class: counted in marks, like the fit's memory (fit_memory). Reason: stage 1's 0.8 s
    // of keyed time at 25 WPM (E9b, measured) holds about 9 marks, as a mark averages 1.86 dits (VE3NEA's statistics:
    // 57% dits, 43% dahs) = 89 ms at 25 WPM (derived); and the decoder already requires 8 marks and spaces before a
    // fresh fit counts (fresh_fit_min_obs, heuristic). Status: heuristic (Plan B, B4e)
    int rekey_marks = 8;
    // s of channel time, the re-key time-out with rekey_wait_in_marks, the same for every branch; counted from the
    // over's first provisional mark (rekey_timeout_from_first_mark). Class (2) seconds: a latency. Reason: a genuine
    // station must be able to key rekey_marks marks first; in PARIS a mark comes every 50/14 = 3.57 dits, so 8 marks
    // take 8 x 3.57 x 240 ms = 6.9 s at the decoder's 5 WPM floor (min_wpm; derived), rounded to 7 s. Noise alone
    // keys false marks at 0.01 per second per branch (false_marks_per_s), so it reaches 8 marks only after about
    // 800 s (derived from the calibration target). Status: derived (Plan B, B4e). rekey_timeout_s, when set, takes
    // precedence
    double rekey_marks_timeout_s = 7.0;
    // marks and spaces of this over a fresh fit needs before it may replace the previous over's; placeholder, heuristic
    int fresh_fit_min_obs = 8;
    // s; owner
    double correction_reach_s = 20.0;
};

}  // namespace kz4ap::bank
