#include "kz4ap/bank/timing.hpp"

#include "kz4ap/bank/filters.hpp"
#include "kz4ap/bank/periodicity.hpp"

#include <stdexcept>

namespace kz4ap::bank {

std::vector<double> branch_dits_s(const BankConfig& cfg) {
    std::vector<double> d = branch_lengths_s(cfg);
    for (double& x : d) x /= cfg.length_dits;
    return d;
}

BankTiming bank_timing(const BankConfig& cfg) {
    BankTiming t;
    // The overrides in seconds (ablations), when set, take precedence over the values in dits.
    for (const double d : branch_dits_s(cfg)) {
        const double wait = cfg.rekey_after_s > 0.0 ? cfg.rekey_after_s : cfg.rekey_after_dits * d;
        t.rekey_wait_s.push_back(wait);
        t.rekey_timeout_s.push_back(cfg.rekey_timeout_s > 0.0 ? cfg.rekey_timeout_s : cfg.rekey_timeout_ratio * wait);
        t.rekey_lead_s.push_back(cfg.rekey_lead_dits * d);
    }
    const std::string& mode = cfg.periodicity_window_mode;
    if (mode != "seconds" && mode != "per_candidate" && mode != "shared")
        throw std::invalid_argument("periodicity_window_mode \"" + mode +
                                    "\" is none of \"seconds\", \"per_candidate\" and \"shared\"");
    if (mode == "seconds") {
        // B4d (the default): stage 1's rule, every candidate over the same window, one row per value.
        if (cfg.periodicity_windows_s.empty())
            throw std::invalid_argument("periodicity_window_mode \"seconds\" needs periodicity_windows_s");
        for (const double w : cfg.periodicity_windows_s) t.periodicity_windows_s.push_back({w});
        return t;
    }
    if (mode == "shared") {
        // B4a-C: one window per row shared by every candidate, N_w x T-hat (the channel passes T-hat); before any
        // selection, the windows in seconds.
        for (const double w : cfg.periodicity_unselected_windows_s) t.periodicity_windows_s.push_back({w});
        t.periodicity_window_dits = cfg.periodicity_windows_dits;
        return t;
    }
    const std::vector<double> grid = t_grid(cfg);
    for (const double n_w : cfg.periodicity_windows_dits) {
        std::vector<double> row;
        row.reserve(grid.size());
        for (const double t_dit : grid) row.push_back(n_w * t_dit);
        t.periodicity_windows_s.push_back(std::move(row));
    }
    return t;
}

BankTiming fixed_timing(const BankConfig& cfg, double rekey_wait_s, double rekey_timeout_s,
                        const std::vector<double>& periodicity_windows_s) {
    const std::size_t k = branch_lengths_s(cfg).size();
    BankTiming t;
    t.rekey_wait_s.assign(k, rekey_wait_s);
    t.rekey_timeout_s.assign(k, rekey_timeout_s);
    for (const double d : branch_dits_s(cfg)) t.rekey_lead_s.push_back(cfg.rekey_lead_dits * d);
    for (const double w : periodicity_windows_s) t.periodicity_windows_s.push_back({w});
    return t;
}

}  // namespace kz4ap::bank
