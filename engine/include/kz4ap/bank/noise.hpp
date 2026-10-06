// The bank decoder's noise estimates: a port of training/kz4ap_proto/noise.py (docs/signal-processing.md
// section 8c, "Noise"). Branch 1's level by the milestone-2 three-tap guard and every branch's by the shape
// of the shared noise spectrum ("spectrum", the default), the spectrum's own level ("spectrum-level"), or
// the recorded fallback, a three-tap estimate per branch ("branch"). Variances are per real component of
// v_k, FS^2. P is the branches' |v_k|^2, FS^2 (rows = branches, columns = samples from the stream's start,
// indexed absolutely); u is the channel stream, FS.
#pragma once

#include "kz4ap/bank/bank_config.hpp"
#include "kz4ap/bank/filters.hpp"

#include <complex>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace kz4ap::bank {

// FS^2 (-200 dBFS): keeps x and a finite on noise-free input.
inline constexpr double kMinVar = 1e-20;

// E[y | y < kappa] for y ~ Exp(1): what the guard's truncation leaves of the mean (0.632 at 1.75),
// dimensionless (derived).
double guard_mean(double kappa);

// numpy.quantile(x, q) with numpy's default method "linear" (Hyndman & Fan type 7), including numpy's
// interpolation form (_lerp); NaN if x holds a NaN. x must not be empty.
double quantile_linear(std::vector<double> x, double q);

// sigma_v,k^2 of each boxcar in branch_n by the milestone-2 guard: the tap v[n-N] updates sigma^2 only if
// |v[n-N]|^2 / (2 sigma^2) < kappa and |v[n]|^2, |v[n-2N]|^2 are below kappa_n 2 sigma^2; the mean of the
// accepted taps is divided by m(kappa). The first estimate is the 20% quantile of |v|^2 over the warm-up
// (a provisional one before). Updated once per block with the block's starting sigma^2. Branch k reads
// row k of P (P may have more rows: SpectrumNoise passes the whole matrix for branch 1).
// Plan B task B3 (docs/signal-processing.md section 8c and appendix A.8c, "Noise"):
// - exact zeros are missing data: an input sample of exactly 0 FS enters neither the warm-up nor the recovery's
//   history or count (a block of zeros only updates nothing); the warm-up runs on the first noise_warmup_s of
//   non-zero input, and a tap counts only from 2 N_k + 1 samples after the latest exact zero;
// - recovery of a stuck level: if no middle tap of branch 1 (row 0, the shortest boxcar) has been accepted for
//   noise_stuck_s of non-zero input, every branch's level is set again by the warm-up rule over its last
//   noise_warmup_s of non-zero input (gated on branch 1: a long branch sees no noise while a station keys).
class ThreeTapNoise {
public:
    // first_sample: the absolute sample index of the stream's first sample (0 for a stream counted from its
    // start, as the prototype's); every index below is absolute (64-bit).
    ThreeTapNoise(const BankConfig& cfg, double rate_hz, std::vector<int> branch_n, std::int64_t first_sample = 0);
    // Element j of u and column j of P are absolute sample base + j; n0, n1 are absolute sample indices. u is
    // read sample by sample only to recognize exact zeros (Plan B, B3): a block of them, which updates nothing,
    // and each one, which stays out of the warm-up and the recovery's history and count, and blocks branch k's taps
    // at it and the 2 N_k samples after it. P is read as double in either storage (a PowerMatrix's float32 values
    // convert exactly).
    void update(std::span<const std::complex<double>> u, const Matrix& P, std::int64_t n0, std::int64_t n1,
                std::int64_t base = 0);
    void update(std::span<const std::complex<double>> u, const PowerMatrix& P, std::int64_t n0, std::int64_t n1,
                std::int64_t base = 0);
    // sigma_v,k^2, FS^2; NaN until the first block of non-zero input.
    const std::vector<double>& var() const { return var_; }
    // The warm-up is over.
    bool started() const { return started_; }
    // A first estimate exists (a block of non-zero input has been seen).
    bool ready() const { return nonzero_ > 0; }
    const std::vector<double>& weight() const { return weight_; }
    // How often the recovery has fired (each firing resets every branch), and how many blocks of exact zeros
    // were skipped.
    int recoveries() const { return recoveries_; }
    std::int64_t zero_blocks() const { return zero_blocks_; }

private:
    template <class M>
    void update_impl(std::span<const std::complex<double>> u, const M& P, std::int64_t n0, std::int64_t n1,
                     std::int64_t base);

    std::vector<int> n_;
    double kappa_, kappa_n_, m_, alpha_;
    int warmup_;               // samples (from noise_warmup_s at the point of use)
    std::int64_t stuck_;       // samples (from noise_stuck_s at the point of use); INT64_MAX: never
    std::int64_t origin_ = 0;  // absolute index of the stream's first sample
    std::int64_t last_zero_ = -1;  // absolute index of the latest exact-zero input sample (origin - 1: none)
    std::int64_t nonzero_ = 0;     // samples of non-zero input during the warm-up (then frozen)
    std::vector<double> var_, weight_;
    // |v_k|^2 of non-zero input, FS^2, per branch: every sample during the warm-up, then the last warmup_
    // samples (a ring written at hist_pos_; the quantile does not need their order)
    std::vector<std::vector<double>> hist_;
    std::size_t hist_pos_ = 0;
    std::int64_t quiet_;  // samples of non-zero input since branch 1 (row 0) last accepted a tap
    int recoveries_ = 0;
    std::int64_t zero_blocks_ = 0;
    bool started_ = false;
};

// The common interface of the per-branch estimates (the channel calls update, then sigma2, once per block).
class NoiseEstimator {
public:
    virtual ~NoiseEstimator() = default;
    // Element j of u and column j of P are absolute sample base + j; n0, n1 are absolute sample indices
    // (the channel keeps only a recent window of u and P, P as a PowerMatrix: float32 values read as double).
    virtual void update(std::span<const std::complex<double>> u, const Matrix& P, std::int64_t n0, std::int64_t n1,
                        std::int64_t base = 0) = 0;
    virtual void update(std::span<const std::complex<double>> u, const PowerMatrix& P, std::int64_t n0,
                        std::int64_t n1, std::int64_t base = 0) = 0;
    // sigma_v,k^2 per real component, FS^2, one per branch; NaN until ready().
    virtual std::vector<double> sigma2() const = 0;
    // A first estimate exists: input other than exact zeros has been seen.
    virtual bool ready() const = 0;
    // How often the stuck-level recovery has fired (each firing resets every branch), and the blocks of exact
    // zeros skipped (diagnostics, docs/signal-processing.md section 8c, "Noise").
    virtual int recoveries() const = 0;
    virtual std::int64_t zero_blocks() const = 0;
};

// The recorded fallback: each branch's own three-tap estimate.
class BranchNoise : public NoiseEstimator {
public:
    BranchNoise(const BankConfig& cfg, double rate_hz, std::vector<int> branch_n, std::int64_t first_sample = 0);
    void update(std::span<const std::complex<double>> u, const Matrix& P, std::int64_t n0, std::int64_t n1,
                std::int64_t base = 0) override;
    void update(std::span<const std::complex<double>> u, const PowerMatrix& P, std::int64_t n0, std::int64_t n1,
                std::int64_t base = 0) override;
    std::vector<double> sigma2() const override { return est_.var(); }
    bool ready() const override { return est_.ready(); }
    int recoveries() const override { return est_.recoveries(); }
    std::int64_t zero_blocks() const override { return est_.zero_blocks(); }

private:
    ThreeTapNoise est_;
};

// The shared noise spectrum. Level "three-tap" (variant (a)):
// sigma_v,k^2 = sigma_v,1^2 [(W_k . S) / (W_1 . S)] [b_mask,1 / b_mask,k]; level "spectrum" (variant (b)):
// sigma_v,k^2 = 0.5 (W_k . S) / (M b_mask,k). S is an exponential average (tau_n) of Hann-windowed,
// masked periodograms of u over segments of T_seg, smoothed over +/- spectrum_smoothing_hz; W_k[m] is the
// mean of |H_k(f)|^2 over bin m. Until the three-tap warm-up is over no segment enters and the shape is
// white (ratio N_1 / N_k). Exact-zero samples of u are missing data: a segment of exact zeros only is not
// offered, and in any other segment they are left out of the mask as flagged samples are.
class SpectrumNoise : public NoiseEstimator {
public:
    SpectrumNoise(const BankConfig& cfg, double rate_hz, std::vector<int> branch_n,
                  const std::string& level = "three-tap", std::int64_t first_sample = 0);
    void update(std::span<const std::complex<double>> u, const Matrix& P, std::int64_t n0, std::int64_t n1,
                std::int64_t base = 0) override;
    void update(std::span<const std::complex<double>> u, const PowerMatrix& P, std::int64_t n0, std::int64_t n1,
                std::int64_t base = 0) override;
    std::vector<double> sigma2() const override;
    bool ready() const override { return ref_.ready(); }
    int recoveries() const override { return ref_.recoveries(); }
    std::int64_t zero_blocks() const override { return ref_.zero_blocks(); }
    // sigma_v,k^2 per real component, FS^2, from the flat mean of every accepted masked periodogram so far,
    // smoothed as sigma2() smooths it, with no bias correction (the calibration of b_mask,k). Throws if no
    // segment has entered.
    std::vector<double> masked_branch_power() const;

    int segments() const { return segments_; }                  // accepted
    int segments_offered() const { return segments_offered_; }  // after the warm-up
    int reach() const { return reach_; }                        // the guard margin, samples
    double kept_fraction_sum() const { return kept_fraction_sum_; }
    double masked_power_sum() const { return masked_power_sum_; }  // FS^2
    const ThreeTapNoise& ref() const { return ref_; }

    static constexpr int kSubsamples = 16;  // points per bin for W_k

private:
    template <class M>
    void update_impl(std::span<const std::complex<double>> u, const M& P, std::int64_t n0, std::int64_t n1,
                     std::int64_t base);
    template <class M>
    std::vector<char> clean(std::span<const std::complex<double>> u, const M& P, std::int64_t s,
                            std::int64_t base) const;
    void accept(std::span<const std::complex<double>> seg, const std::vector<char>& clean);
    std::vector<double> smoothed(const std::vector<double>& s) const;

    bool spectrum_level_;
    std::vector<int> n_;
    std::vector<double> mask_bias_;
    ThreeTapNoise ref_;
    int m_;                                  // samples per segment (from segment_s at the point of use)
    std::vector<double> window_;             // Hann, periodic
    std::vector<std::vector<double>> bin_weights_;  // (K, M), dimensionless
    int half_;                               // smoothing half-width, bins (from spectrum_smoothing_hz)
    double kappa_n_;
    int reach_;                              // samples (from guard_margin_s)
    double min_clean_;
    double beta_;
    std::optional<std::vector<double>> shape_;            // FS^2 per bin
    std::optional<std::vector<double>> periodogram_sum_;  // FS^2 per bin
    mutable std::optional<std::vector<double>> ratio_;    // cached with absolute_
    mutable std::vector<double> absolute_;
    int segments_ = 0;
    int segments_offered_ = 0;
    double kept_fraction_sum_ = 0.0;
    double masked_power_sum_ = 0.0;
    std::int64_t origin_ = 0;      // absolute index of the stream's first sample
    std::int64_t next_start_ = 0;  // absolute sample index
};

// noise_method "spectrum", "spectrum-level" or "branch"; throws std::invalid_argument otherwise.
// first_sample: the absolute index of the stream's first sample (0 by default).
std::unique_ptr<NoiseEstimator> make_noise(const BankConfig& cfg, double rate_hz, std::vector<int> branch_n,
                                           std::int64_t first_sample = 0);

}  // namespace kz4ap::bank
