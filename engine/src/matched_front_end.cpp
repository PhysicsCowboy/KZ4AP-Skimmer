#include "kz4ap/matched_front_end.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <stdexcept>

namespace kz4ap {
namespace {

constexpr std::size_t kResumEvery = 4096;             // recompute the running sum this often, samples
constexpr double kMinNoiseVar = 1e-20;                // FS^2 (-200 dBFS): keeps x and a finite on noise-free input
constexpr double kWarmupNoiseQuantile = 0.2;           // the warm-up's noise estimate uses this quantile of |v|^2
// Noise weight left after the floor lifts sigma^2, as a duration of per-sample updates, s
// (16 samples at 1500 samples/s).
constexpr double kLiftWeightS = 16.0 / 1500.0;

// Validates the parameters and returns sample_rate. Runs as rate_'s initializer,
// before anything divides by them.
double validated_rate(double sample_rate, const MatchedFrontEndConfig& c) {
    if (!(sample_rate > 0) || !(c.min_wpm > 0) || !(c.initial_wpm >= c.min_wpm) || !(c.length_dits > 0) ||
        !(c.warmup_s > 0) || !(c.noise_tau_s > 0) || !(c.amplitude_tau_s > 0) || !(c.noise_guard > 0) ||
        !(c.neighbor_guard >= c.noise_guard) || !(c.floor_quantile > 0) || !(c.floor_min_clean <= 1) ||
        !(c.floor_quantile < c.floor_min_clean) || c.floor_samples < 2 || !(c.floor_margin >= 1) ||
        !(c.floor_restart_ratio >= 1) || !(c.prior_key_down > 0) || !(c.prior_key_down < 1) ||
        !(c.squelch_a >= 0) || !(c.squelch_exponent >= 0))
        throw std::invalid_argument("invalid matched front end config");
    return sample_rate;
}

double alpha_for(double tau_s, double rate) { return 1.0 - std::exp(-1.0 / (tau_s * rate)); }

// A filter of duration_s at rate, samples (at least 1).
int samples_for(double duration_s, double rate) {
    return std::max(1, static_cast<int>(std::lround(duration_s * rate)));
}

double logistic(double g) { return 1.0 / (1.0 + std::exp(-std::clamp(g, -50.0, 50.0))); }

}  // namespace

double log_bessel_i0(double z) {
    z = std::abs(z);
    if (z < 3.75) {
        const double t = (z / 3.75) * (z / 3.75);
        return std::log(1.0 + t * (3.5156229 + t * (3.0899424 + t * (1.2067492 + t * (0.2659732 +
                                                                                     t * (0.0360768 + t * 0.0045813))))));
    }
    const double t = 3.75 / z;
    const double poly =
        0.39894228 +
        t * (0.01328592 +
             t * (0.00225319 +
                  t * (-0.00157565 +
                       t * (0.00916281 + t * (-0.02057706 + t * (0.02635537 + t * (-0.01647633 + t * 0.00392377)))))));
    return z - 0.5 * std::log(z) + std::log(poly);
}

double envelope_llr(double x, double a) { return -0.5 * a * a + log_bessel_i0(a * x); }

MatchedFrontEnd::MatchedFrontEnd(double sample_rate, MatchedFrontEndConfig config)
    : rate_(validated_rate(sample_rate, config)),
      config_(config),
      noise_alpha_(alpha_for(config.noise_tau_s, sample_rate)),
      amplitude_alpha_(alpha_for(config.amplitude_tau_s, sample_rate)),
      log_prior_odds_(std::log(config.prior_key_down / (1.0 - config.prior_key_down))),
      guard_mean_(1.0 - config.noise_guard * std::exp(-config.noise_guard) / (1.0 - std::exp(-config.noise_guard))),
      // Noise alone: the q-quantile of |v|^2 is 2 sigma^2 (-ln(1 - q)). With a station leaving a clean
      // fraction c, it is at most noise's (q/c)-quantile, 2 sigma^2 (-ln(1 - q/c)): dividing by that
      // (and a margin for sampling spread) keeps the floor below sigma^2.
      floor_divisor_(-2.0 * std::log(1.0 - config.floor_quantile / config.floor_min_clean) * config.floor_margin),
      lift_weight_(kLiftWeightS * sample_rate),
      max_length_(samples_for(config.length_dits * 1.2 / config.min_wpm, sample_rate)),
      acquisition_length_(samples_for(config.length_dits * 1.2 / config.initial_wpm, sample_rate)) {
    reset();
}

int MatchedFrontEnd::length_for(double dit_s) const {
    // Clamped before rounding, so a huge dit cannot overflow the conversion.
    const double k = std::clamp(config_.length_dits * dit_s * rate_, static_cast<double>(acquisition_length_),
                                static_cast<double>(max_length_));
    return static_cast<int>(std::lround(k));
}

void MatchedFrontEnd::reset() {
    ring_.assign(static_cast<std::size_t>(max_length_), Sample{});
    head_ = 0;
    length_ = acquisition_length_;
    sum_ = {};
    since_resum_ = 0;
    power_ring_.assign(2 * static_cast<std::size_t>(max_length_) + 1, 0.0);
    count_ = 0;
    warmup_left_ = std::max<std::size_t>(1, static_cast<std::size_t>(std::lround(config_.warmup_s * rate_)));
    warmup_power_.clear();
    pending_dit_ = 0;
    floor_ring_.assign(static_cast<std::size_t>(config_.floor_samples), 0.0);
    floor_head_ = floor_filled_ = 0;
    noise_var_ = amp2_ = 0;
    noise_weight_ = amplitude_weight_ = 0;
}

double MatchedFrontEnd::noise_sigma() const { return std::sqrt(noise_var_); }

double MatchedFrontEnd::amplitude() const { return std::sqrt(amp2_); }

double MatchedFrontEnd::squelch() const {
    // In noise alone a-hat squared is a p-weighted mean over about tau_a r / K independent samples,
    // so its spread grows as sqrt(K): scaling a_min as K^(1/4) keeps the chance that noise lifts
    // a-hat past it the same at every K (derived, Gaussian approximation).
    return config_.squelch_a *
           std::pow(static_cast<double>(length_) / acquisition_length_, config_.squelch_exponent);
}

void MatchedFrontEnd::resum() {
    const std::size_t n = ring_.size();
    sum_ = {};
    for (int i = 1; i <= length_; ++i) sum_ += std::complex<double>(ring_[(head_ + n - static_cast<std::size_t>(i)) % n]);
    since_resum_ = 0;
}

void MatchedFrontEnd::set_dit(double dit_s) {
    if (!std::isfinite(dit_s) || !(dit_s > 0)) return;
    if (warmup_left_ > 0) {  // the warm-up always runs at the acquisition width
        pending_dit_ = dit_s;
        return;
    }
    apply_length(length_for(dit_s));
}

void MatchedFrontEnd::apply_length(int k) {
    if (k == length_) return;
    // The boxcar's output noise power is proportional to 1/K for noise flat across its passband.
    const double scale = static_cast<double>(length_) / k;
    noise_var_ = std::max(kMinNoiseVar, noise_var_ * scale);
    for (auto& x : floor_ring_) x *= scale;
    for (auto& x : power_ring_) x *= scale;  // so the guard compares like with like
    length_ = k;
    resum();
}

void MatchedFrontEnd::reacquire() {
    if (warmup_left_ > 0) {
        pending_dit_ = 0;
        return;
    }
    apply_length(acquisition_length_);  // rescales sigma^2
    amp2_ = 0;
    amplitude_weight_ = 0;
}

void MatchedFrontEnd::start_estimates() {
    std::vector<double> sorted = warmup_power_;
    std::sort(sorted.begin(), sorted.end());
    const auto quantile = [&](double q) {
        return sorted[static_cast<std::size_t>(q * static_cast<double>(sorted.size() - 1))];
    };
    // In noise alone |v|^2 is exponential with mean 2 sigma^2; its q-quantile is 2 sigma^2 (-ln(1 - q)).
    // 0.32 s at K = 24 holds about 20 independent samples (appendix A.8b).
    noise_var_ = std::max(kMinNoiseVar, quantile(kWarmupNoiseQuantile) /
                                            (2.0 * -std::log(1.0 - kWarmupNoiseQuantile)));
    amp2_ = std::max(0.0, quantile(0.9) - 2.0 * noise_var_);
    // The warm-up counts as this much weight, so the samples after it refine the estimates.
    noise_weight_ = 0.1 * static_cast<double>(sorted.size());
    amplitude_weight_ = 0.1 * static_cast<double>(sorted.size());
    warmup_power_.clear();
    if (pending_dit_ > 0) {
        apply_length(length_for(pending_dit_));
        amp2_ = 0;  // measured at the acquisition width
        amplitude_weight_ = 0;
        pending_dit_ = 0;
    }
}

void MatchedFrontEnd::update_floor(double power) {
    // Every K-th |v|^2 (such samples share no inputs). If sigma^2 lies below the floor, the noise
    // has risen (or the estimate started low): lift it and let the next updates count for more.
    // Only a large lift (the stuck-low case, where the low sigma-hat has let s-hat grow on noise)
    // restarts s-hat; a small one is the quantile's spread or a second station in the channel, and
    // restarting s-hat there refits it from a few samples, often on a filter ramp (final check F-3).
    if (count_ % static_cast<std::uint64_t>(length_) != 0) return;
    floor_ring_[floor_head_] = power;
    floor_head_ = (floor_head_ + 1) % floor_ring_.size();
    floor_filled_ = std::min(floor_filled_ + 1, floor_ring_.size());
    if (floor_filled_ < floor_ring_.size() / 2) return;
    std::vector<double> v(floor_ring_.begin(), floor_ring_.begin() + static_cast<std::ptrdiff_t>(floor_filled_));
    const auto nth = v.begin() + static_cast<std::ptrdiff_t>(config_.floor_quantile * static_cast<double>(v.size() - 1));
    std::nth_element(v.begin(), nth, v.end());
    const double floor = *nth / floor_divisor_;
    if (noise_var_ < floor) {
        const bool stuck_low = floor > config_.floor_restart_ratio * noise_var_;
        noise_var_ = floor;
        noise_weight_ = std::min(noise_weight_, lift_weight_);
        if (stuck_low) {
            amp2_ = 0;
            amplitude_weight_ = 0;
        }
    }
}

void MatchedFrontEnd::update_noise() {
    // Three taps K apart, v[n], v[n-K], v[n-2K], share no inputs, so in white noise they are
    // independent. The middle one updates sigma^2 only if it is below kappa * 2 sigma^2 and its
    // neighbors below kappa_n * 2 sigma^2: a mark or filter ramp within K of it lifts a tap above
    // that, except at low SNR. The middle tap is then an exponential truncated at kappa, whose
    // mean is guard_mean_ times the untruncated one; dividing by it makes the estimate unbiased
    // in noise (the neighbors are independent of it). Nothing here reads the posterior, the
    // log-odds or s-hat (review finding C1).
    const auto k = static_cast<std::uint64_t>(length_);
    if (count_ <= 2 * k) return;
    const std::size_t n = power_ring_.size();
    const double limit = config_.noise_guard * 2.0 * noise_var_;
    const double neighbor_limit = config_.neighbor_guard * 2.0 * noise_var_;
    const double now = power_ring_[(count_ - 1) % n];
    const double middle = power_ring_[(count_ - 1 - k) % n];
    const double oldest = power_ring_[(count_ - 1 - 2 * k) % n];
    if (!(middle < limit && now < neighbor_limit && oldest < neighbor_limit)) return;
    noise_weight_ += 1.0;
    noise_var_ = std::max(kMinNoiseVar, noise_var_ + std::max(noise_alpha_, 1.0 / noise_weight_) *
                                                         (0.5 * middle / guard_mean_ - noise_var_));
}

FrontEndSample MatchedFrontEnd::step(Sample u) {
    // Boxcar: add the new input, drop the one length_ inputs back.
    const std::size_t n = ring_.size();
    const Sample leaving = ring_[(head_ + n - static_cast<std::size_t>(length_)) % n];
    ring_[head_] = u;
    head_ = (head_ + 1) % n;
    sum_ += std::complex<double>(u) - std::complex<double>(leaving);
    if (++since_resum_ >= kResumEvery) resum();

    FrontEndSample out;
    const std::complex<double> v = sum_ / static_cast<double>(length_);
    out.filtered = Sample(static_cast<float>(v.real()), static_cast<float>(v.imag()));
    out.weight = 1.0f / static_cast<float>(length_);
    const double power = std::norm(v);
    power_ring_[count_ % power_ring_.size()] = power;
    ++count_;
    if (warmup_left_ > 0) {
        warmup_power_.push_back(power);
        if (--warmup_left_ == 0) start_estimates();
        return out;
    }

    update_floor(power);
    const double sigma = std::sqrt(noise_var_);
    const double a = std::sqrt(amp2_) / sigma;
    const double llr = envelope_llr(std::sqrt(power) / sigma, a);
    const double g = llr + log_prior_odds_;
    const double p = logistic(g);
    out.ready = true;
    out.signal = a >= squelch();
    out.llr = static_cast<float>(llr);
    out.log_odds = static_cast<float>(g);
    out.p_key_down = out.signal ? static_cast<float>(p) : 0.0f;

    update_noise();
    // Online EM for the Rician component: its mean square is 2 sigma^2 + s^2.
    amplitude_weight_ += p;
    const double step = p * std::max(amplitude_alpha_, 1.0 / amplitude_weight_);
    amp2_ = std::max(0.0, amp2_ + step * (power - 2.0 * noise_var_ - amp2_));
    return out;
}

}  // namespace kz4ap
