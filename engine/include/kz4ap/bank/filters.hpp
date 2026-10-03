// The bank decoder's branch filters and envelope likelihood: a port of training/kz4ap_proto/bank.py and
// detect.py (docs/signal-processing.md section 8c). Lengths are in seconds; they become samples only in
// branch_samples (the point of use).
#pragma once

#include "kz4ap/bank/bank_config.hpp"

#include <complex>
#include <cstddef>
#include <span>
#include <vector>

namespace kz4ap::bank {

// A small row-major matrix of doubles: rows are branches (K), columns are samples.
struct Matrix {
    int rows = 0, cols = 0;
    std::vector<double> v;
    double& at(int r, int c) { return v[index(r, c)]; }
    double at(int r, int c) const { return v[index(r, c)]; }

private:
    std::size_t index(int r, int c) const {
        return static_cast<std::size_t>(r) * static_cast<std::size_t>(cols) + static_cast<std::size_t>(c);
    }
};

// L_k = length_dits x 1.2 s / max_wpm x ladder_step^(k-1), k = 1 ... count, up to the first length within
// one step of the min_wpm optimum: 9.6 ms ... 184.3 ms, 32 branches with the defaults; s.
std::vector<double> branch_lengths_s(const BankConfig& cfg);

// N_k = round(L_k r), at least 1 (ties to even, as numpy rint does); lengths in s, rate in samples/s.
std::vector<int> branch_samples(const std::vector<double>& lengths_s, double rate_hz);

// N_k / r, s: the lengths the branches really have.
std::vector<double> realized_lengths_s(const BankConfig& cfg, double rate_hz);

// v[m] = (1/n) sum of u[m-n+1 .. m], zeros before the stream: causal, unity gain; computed from a running
// cumulative sum c, v[m] = (c[m+1] - c[max(m+1-n, 0)]) / n, as in the prototype.
std::vector<std::complex<double>> boxcar(std::span<const std::complex<double>> u, int n);

// |H(f)|^2 of an n-sample boxcar at rate_hz: (sin(pi f n / r) / (n sin(pi f / r)))^2, 1 at 0 Hz
// (dimensionless, relative to the 0 Hz response); f in Hz, rate in samples/s.
double power_response(double f_hz, int n, double rate_hz);

// ln I0(z) for any z: kz4ap::log_bessel_i0 (matched_front_end.cpp), the same Abramowitz & Stegun
// 9.8.1 / 9.8.2 formula as the prototype's detect.py (compared term by term).
double log_bessel_i0(double z);

// Lambda = -a^2/2 + ln I0(a x), nats (kz4ap::envelope_llr, shared with Matched): key-down over key-up for x = |v|/sigma_v and a = s/sigma_v.
double envelope_llr(double x, double a);

// 1 / (1 + exp(-g)), g clipped to [-50, 50] nats.
double logistic(double g);

}  // namespace kz4ap::bank
