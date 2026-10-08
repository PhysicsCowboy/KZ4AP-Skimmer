// The bank decoder's keying: a port of the removed prototype's keying.py (docs/signal-processing.md section
// 8c, "Keying"). Per branch: the amplitude s_k (online EM, tau_a of key-down weight), the envelope
// log-likelihood ratio with the PARIS prior, hysteresis +/- h nats, the squelch a_min,k, and, while a
// branch is at the start of an over (`unknown`), the unknown-amplitude threshold test x_on,k / x_off with
// the amplitude seeded from the keyed samples; rekey keys a stored stretch again with the full LLR. All
// branches advance one block at a time. Times are in seconds and become samples only at the point of use.
#pragma once

#include "kz4ap/bank/bank_config.hpp"
#include "kz4ap/bank/filters.hpp"

#include <cstdint>
#include <span>
#include <utility>
#include <vector>

namespace kz4ap::bank {

// Key state of one branch over n samples: down (1) where down[i] != 0, up (0) where up[i] != 0 (up wins),
// otherwise the state before; `initial` (0 or 1) is the state before the first sample.
std::vector<int> hysteresis(std::span<const double> down, std::span<const double> up, int initial);

// Per branch, (sample index, key down after it) at every change of key state. key is (K, n) of 0/1,
// `before` the key state before its first column, n0 the absolute sample index of that column. Edges of
// marks keyed by the unknown-amplitude test are provisional (lengthened by up to L_k) until re-keyed.
std::vector<std::vector<std::pair<std::int64_t, bool>>> edges(const Matrix& key, const std::vector<int>& before,
                                                            std::int64_t n0);

// What step returns: the key state (K, n) of 0/1; the posterior p (K, n), 0 where squelched; the key state
// before the block (K); a_k = s_k / sigma_v,k (K, dimensionless) at the block's start.
struct KeyStep {
    Matrix key;
    Matrix p;
    std::vector<int> before;
    std::vector<double> a;
};

// The prototype's BankKeyer. Its state is public, as the channel reads (and the prototype exposes) it.
class BankKeyer {
public:
    // lengths_s: the branches' realized lengths L_k, s. W_min is cfg.rekey_after_s for every branch. Throws
    // std::invalid_argument if x_on_values is set and does not hold one value per branch.
    BankKeyer(const BankConfig& cfg, double rate_hz, const std::vector<double>& lengths_s);

    // P: (K, n) |v_k|^2 of the block, FS^2; sigma2: (K) sigma_v,k^2, FS^2.
    KeyStep step(const Matrix& P, const std::vector<double>& sigma2);
    // unknown & (weight >= rekey_weight), per branch.
    std::vector<bool> ready_to_rekey() const;
    // A possible new over: a fresh amplitude and the unknown-amplitude test; an established amplitude is kept
    // as the fallback (prev_amp2).
    void start_over(int k);
    // After the re-keying: the winning amplitude amp2 (FS^2), the full LLR from now on; key_now is the key
    // state at the end of the re-keyed stretch. `weight` keeps the keyed-sample count as the EM's W.
    void finish_over_start(int k, double amp2, bool key_now);

    double alpha;                     // EM step per sample of weight: 1 - exp(-1 / (tau_a r)), dimensionless
    double log_prior;                 // ln(P1 / (1 - P1)), nats
    double h;                         // hysteresis, nats
    std::vector<double> a_min;        // squelch per branch, dimensionless
    std::vector<double> x_on;         // unknown-amplitude key-down threshold per branch, dimensionless
    double x_off;                     // unknown-amplitude key-up threshold, dimensionless
    double rekey_weight;              // W_min, samples of keyed time (rekey_after_s x r, not rounded)
    std::vector<double> amp2;         // s_k^2, FS^2
    std::vector<double> weight;       // W behind s_k^2, samples
    std::vector<double> prev_amp2;    // the previous over's s_k^2, FS^2 (NaN: none)
    std::vector<bool> unknown;        // at the start of an over (the stream's start is one)
    std::vector<std::vector<double>> keyed;  // |v|^2 of the keyed samples while unknown, FS^2 (last keyed_cap)
    int keyed_cap;                    // samples: seed_memory_rekeys x W_min, rounded
    std::vector<int> key;             // key state at the end of the last block (0/1)

private:
    void update_amplitude(const Matrix& P, const Matrix& p, const std::vector<double>& sigma2, const Matrix& key);
};

// Key state (0/1) over a stored stretch P (|v|^2, FS^2) with the full LLR at fixed sigma2 and amp2 (FS^2),
// from key up; a_min is the branch's squelch (dimensionless).
std::vector<int> rekey(std::span<const double> P, double sigma2, double amp2, const BankConfig& cfg, double a_min);

}  // namespace kz4ap::bank
