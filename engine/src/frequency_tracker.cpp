#include "kz4ap/frequency_tracker.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>

namespace kz4ap {
namespace {

constexpr double kTwoPi = 2 * std::numbers::pi;

// A physical duration in samples (owner's rule: convert only at the point of use).
int samples_for(double seconds, double sample_rate) { return static_cast<int>(std::lround(seconds * sample_rate)); }

// Validates the parameters and returns sample_rate. Runs as rate_'s initializer,
// before alpha_ divides by them.
double validated_rate(double sample_rate, const FrequencyTrackerConfig& c) {
    if (!(sample_rate > 0) || !(c.lag_s > 0) || samples_for(c.lag_s, sample_rate) < 1 || !(c.tau_s > 0) ||
        !(c.min_weight >= 0) || !(c.min_weight < 1) || !(c.max_offset_hz > 0) || !(c.update_interval_s > 0) ||
        samples_for(c.update_interval_s, sample_rate) < 1 || !(c.fine_tune_hz > 0) ||
        !(c.min_coherence >= 0) || !(c.min_coherence < 1))
        throw std::invalid_argument("invalid frequency tracker config");
    if (!(c.max_offset_hz < sample_rate / (2.0 * samples_for(c.lag_s, sample_rate))))
        throw std::invalid_argument("frequency tracker max_offset_hz must be below 1 / (2 lag)");
    return sample_rate;
}

}  // namespace

FrequencyTracker::FrequencyTracker(double sample_rate, double initial_offset_hz, FrequencyTrackerConfig config)
    : rate_(validated_rate(sample_rate, config)),
      config_(config),
      lag_(samples_for(config.lag_s, sample_rate)),
      update_every_(samples_for(config.update_interval_s, sample_rate)),
      alpha_(1.0 - std::exp(-1.0 / (config.tau_s * sample_rate))) {
    reset(initial_offset_hz);
}

void FrequencyTracker::clear_average() {
    average_ = {};
    magnitude_ = 0;
    weight_ = 0;
}

void FrequencyTracker::reset(double initial_offset_hz) {
    offset_hz_ = std::clamp(initial_offset_hz, -config_.max_offset_hz, config_.max_offset_hz);
    anchor_hz_ = offset_hz_;
    phase_ = 0;
    history_.assign(static_cast<std::size_t>(lag_), Sample{});
    head_ = 0;
    seen_ = 0;
    clear_average();
    until_update_ = update_every_;
}

void FrequencyTracker::reacquire() { clear_average(); }

void FrequencyTracker::set_anchor(double anchor_hz) {
    anchor_hz_ = std::clamp(anchor_hz, -config_.max_offset_hz, config_.max_offset_hz);
    if (std::abs(anchor_hz_ - offset_hz_) > config_.fine_tune_hz) {
        // The detector's track moved to another peak (a turnover within the channel distance) or
        // drifted beyond the fine-tuning: go there and start a fresh average.
        offset_hz_ = anchor_hz_;
        clear_average();
    }
}

Sample FrequencyTracker::mix(Sample y) {
    const Sample lo(static_cast<float>(std::cos(phase_)), static_cast<float>(-std::sin(phase_)));
    phase_ += kTwoPi * offset_hz_ / rate_;
    if (phase_ >= std::numbers::pi) phase_ -= kTwoPi;
    else if (phase_ < -std::numbers::pi) phase_ += kTwoPi;
    return y * lo;
}

void FrequencyTracker::observe(Sample v, float weight) {
    const Sample old = history_[head_];
    history_[head_] = v;
    head_ = (head_ + 1) % history_.size();
    const double w = std::clamp(static_cast<double>(weight), 0.0, 1.0);
    if (seen_ < history_.size()) {
        ++seen_;  // `old` is not a real sample yet
    } else if (w > 0) {
        // The product's phase is the residual advance over `lag` samples; adding the
        // NCO's own advance turns it into a measurement of the absolute offset, so
        // the average does not depend on where the NCO happens to be.
        const std::complex<double> z = std::complex<double>(v) * std::conj(std::complex<double>(old));
        const std::complex<double> absolute = z * std::polar(1.0, kTwoPi * offset_hz_ * lag_ / rate_);
        const double a = alpha_ * w;
        average_ += a * (absolute - average_);
        magnitude_ += a * (std::abs(absolute) - magnitude_);
        weight_ += a * (1.0 - weight_);
    }
    if (--until_update_ == 0) {
        until_update_ = update_every_;
        // A coherent average (one station) moves the NCO; while the average changes from one
        // station to another its phasors cancel and it waits.
        if (weight_ >= config_.min_weight && std::abs(average_) > config_.min_coherence * magnitude_) {
            const double estimate = std::arg(average_) * rate_ / (kTwoPi * lag_);
            if (std::abs(estimate - anchor_hz_) <= config_.fine_tune_hz) {
                offset_hz_ = std::clamp(estimate, -config_.max_offset_hz, config_.max_offset_hz);
            } else {
                // Not this channel's station (the detector decides which station that is): forget
                // these products and return to the anchor.
                clear_average();
                offset_hz_ = anchor_hz_;
            }
        }
    }
}

}  // namespace kz4ap
