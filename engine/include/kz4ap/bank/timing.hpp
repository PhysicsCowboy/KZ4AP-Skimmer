// The bank decoder's time constants per branch and per periodicity candidate (docs/signal-processing.md section
// 8c, "Time constants in dits"). BankConfig states them in nominal dits (Plan B, B4a; stage-2 spec section 2): within
// branch k a dit is d_k = L_k / length_dits, with L_k the branch's nominal length (branch_lengths_s, 9.6 ms x
// 1.1^(k-1)), not its realized N_k / r, so d_k is a constant of the branch: 12 ms (k = 1) to 230 ms (k = 32) with
// the defaults. The periodicity variants in dits use the comb's candidate dit tau_c (the default windows are in
// seconds since B4d). This file converts them to seconds; the
// keyer, the channel and the periodicity estimator turn the seconds into samples at the point of use.
#pragma once

#include "kz4ap/bank/bank_config.hpp"

#include <vector>

namespace kz4ap::bank {

// d_k = L_k / length_dits per branch, s (L_k from branch_lengths_s: nominal).
std::vector<double> branch_dits_s(const BankConfig& cfg);

// The time constants the keyer, the channel and the periodicity estimator use, in seconds.
struct BankTiming {
    // W_min,k, s of keyed (key-down) time; one per branch. With BankConfig::rekey_wait_in_marks off, the keyed time
    // while the amplitude is unknown before the over's start is re-keyed; in every mode it sets the seed's memory
    // (seed_memory_rekeys x W_min,k).
    std::vector<double> rekey_wait_s;
    // Channel time an over's amplitude may stay unknown before it is re-keyed at the previous over's amplitude or
    // cleared, s; one per branch.
    std::vector<double> rekey_timeout_s;
    // The re-key stretch's lead before an over's first provisional mark (BankConfig::rekey_lead_dits x d_k), s; one per
    // branch (Plan B, B4d; used only with rekey_timeout_from_first_mark).
    std::vector<double> rekey_lead_s;
    // The periodicity windows, s: one row per window, each row either one value (every candidate dit is judged over
    // the same window, stage 1's form) or one value per candidate of t_grid(cfg) (each candidate over its own).
    std::vector<std::vector<double>> periodicity_windows_s;
    // Plan B, B4a-C (the "shared" variant, not the default): N_w per row, each row's window shared by every candidate
    // and N_w x T-hat long, T-hat the selected branch's eligible fitted dit; periodicity_windows_s then holds the
    // windows used while there is no T-hat (rows of one value each, as many as here). Empty: the windows are periodicity_windows_s alone.
    std::vector<double> periodicity_window_dits;
};

// The configuration's timing: W_min,k = rekey_after_dits x d_k; the time-out rekey_marks_timeout_s with
// rekey_wait_in_marks (B4e, the default), else rekey_timeout_ratio x W_min,k (the overrides rekey_after_s and
// rekey_timeout_s, when set, replace them); the periodicity windows by
// periodicity_window_mode: "seconds" (the default, B4d) a row of one value per entry of periodicity_windows_s;
// "per_candidate" (a variant, B4a) a row per N_w of periodicity_windows_dits, N_w x tau_c for each candidate tau_c of
// t_grid(cfg); "shared" (a variant, B4a-C) periodicity_window_dits = periodicity_windows_dits with rows of
// periodicity_unselected_windows_s while there is no T-hat. Throws std::invalid_argument for another mode, or for
// "seconds" with no windows.
BankTiming bank_timing(const BankConfig& cfg);

// Every constant in seconds, the same for every branch and every candidate (stage 1's form; Plan A and the
// prototype used 0.8 s, 2 s and {2, 5, 10} s). For the module golden tests, which compare the code with the
// prototype's at the prototype's values.
BankTiming fixed_timing(const BankConfig& cfg, double rekey_wait_s, double rekey_timeout_s,
                        const std::vector<double>& periodicity_windows_s);

}  // namespace kz4ap::bank
