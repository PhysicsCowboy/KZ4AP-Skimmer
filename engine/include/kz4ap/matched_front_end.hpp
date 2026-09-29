#pragma once

#include "kz4ap/types.hpp"

#include <complex>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace kz4ap {

// ln I0(z), I0 the modified Bessel function of the first kind, order zero, for
// any z >= 0 without overflow. Abramowitz & Stegun 9.8.1 (z < 3.75) and 9.8.2;
// relative error in I0 below 5e-7 (up to 4.7e-7 for z >= 3.75).
double log_bessel_i0(double z);

// Log-likelihood ratio, nats, of key-down (Rician envelope) against key-up
// (Rayleigh envelope) for the normalized envelope x = R / sigma and the
// normalized key-down amplitude a = s / sigma: -a^2/2 + ln I0(a x)
// (Proakis & Salehi, eq. 4.5-21, the on-off keying case).
double envelope_llr(double x, double a);

struct MatchedFrontEndConfig {
    double initial_wpm = 60.0;          // filter length until the decoder's speed is trusted: the fastest code
    double min_wpm = 5.0;               // sets the longest filter
    double length_dits = 0.8;           // beta: filter length as a fraction of the dit
    double warmup_s = 0.32;             // collected at the acquisition width before the first estimates, s
    double noise_tau_s = 2.0;           // noise estimate's time constant, s of noise updates
    double amplitude_tau_s = 0.5;       // amplitude estimate's time constant, s of key-down weight
    double noise_guard = 1.75;          // kappa: v[n-K] updates sigma^2 only if |v|^2 / (2 sigma^2) is below this...
    double neighbor_guard = 4.0;        // ...and v[n], v[n-2K] are below this (a mark or ramp next to it)
    double floor_quantile = 0.1;        // floor: this quantile of |v|^2 taken every K samples...
    int floor_samples = 64;             // ...over this many of them...
    double floor_min_clean = 0.25;      // ...assuming a station leaves at least this fraction of them clean...
    double floor_margin = 2.5;          // ...and dividing by this for sampling spread
    double floor_restart_ratio = 4.0;   // a lift restarts s-hat only if the floor exceeds this times sigma^2
    double prior_key_down = 0.44;       // P1: PARIS keys down 22 of 50 dit units
    double squelch_a = 3.0;             // key-down evidence is ignored while a = s / sigma is below this with the
                                        // acquisition filter (16 ms: 0.8 of the 60 WPM dit)...
    double squelch_exponent = 0.25;     // ...times (filter duration / 16 ms)^this at other filter lengths
};

struct FrontEndSample {
    Sample filtered;       // v: matched-filter output, FS
    float llr = 0;         // Lambda = ln p1(x) / p0(x), nats (no prior)
    float log_odds = 0;    // g = Lambda + ln(P1 / P0), nats
    float p_key_down = 0;  // posterior probability of key-down, 0..1; 0 while squelched or warming up
    float weight = 0;      // 1/K: scale each llr by this before summing over samples (they are correlated)
    bool ready = false;    // warm-up is over; llr, log_odds and p_key_down are valid
    bool signal = false;   // a >= squelch_a
};

// One station's front end at the channel rate: a boxcar filter matched to the
// dit (K = round(beta * dit * rate) samples, normalized to unity gain), then the
// envelope, then a Rician-versus-Rayleigh log-likelihood ratio from running
// estimates of the noise and of the key-down amplitude.
class MatchedFrontEnd {
public:
    // Throws std::invalid_argument for a non-positive sample rate or an invalid config.
    explicit MatchedFrontEnd(double sample_rate, MatchedFrontEndConfig config = {});

    FrontEndSample step(Sample u);
    // Follows the decoder's dit estimate, s. A dit that is not finite or not positive is
    // ignored; K is clamped between the acquisition width (initial_wpm) and min_wpm's width.
    void set_dit(double dit_s);
    // After a long silence: back to the acquisition width (initial_wpm), a fresh
    // amplitude estimate; the noise estimate is kept (rescaled to the new width).
    void reacquire();
    void reset();

    int length() const { return length_; }  // K, samples
    double noise_sigma() const;             // sigma: noise RMS per real component of v, FS
    double amplitude() const;               // s: key-down amplitude of v, FS
    double squelch() const;                 // a_min at the current K

private:
    int length_for(double dit_s) const;
    void apply_length(int k);
    void resum();
    void start_estimates();
    void update_noise();
    void update_floor(double power);

    double rate_;
    MatchedFrontEndConfig config_;
    double noise_alpha_;
    double amplitude_alpha_;
    double log_prior_odds_;
    double guard_mean_;                 // E[y | y < kappa] for y ~ Exp(1): undoes the guard's truncation
    double floor_divisor_;              // the floor's quantile of |v|^2 over this is at most sigma^2
    double lift_weight_;                // noise weight left after the floor lifts sigma^2, samples
    int max_length_;                    // K at min_wpm, the narrowest filter
    int acquisition_length_;            // K at initial_wpm, the widest filter
    int length_ = 1;
    std::vector<Sample> ring_;          // the last max_length_ inputs, circular
    std::size_t head_ = 0;              // where the next input goes
    std::complex<double> sum_{};        // sum of the last length_ inputs
    std::size_t since_resum_ = 0;
    std::vector<double> power_ring_;    // |v|^2 of the last 2 max_length_ + 1 outputs, circular
    std::uint64_t count_ = 0;           // samples stepped since reset
    std::size_t warmup_left_ = 0;
    std::vector<double> warmup_power_;
    double pending_dit_ = 0;            // a set_dit during the warm-up, applied when it ends (0 = none)
    std::vector<double> floor_ring_;    // |v|^2 every K samples, circular
    std::size_t floor_head_ = 0;
    std::size_t floor_filled_ = 0;
    double noise_var_ = 0;              // sigma^2, FS^2
    double amp2_ = 0;                   // s^2, FS^2
    double noise_weight_ = 0;           // W_n: noise updates so far (the warm-up counts as some)
    double amplitude_weight_ = 0;       // W_a: key-down weight so far (the warm-up counts as some)
};

}  // namespace kz4ap
