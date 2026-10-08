#pragma once

#include "kz4ap/types.hpp"

#include <complex>
#include <cstddef>
#include <vector>

namespace kz4ap {

struct FrequencyTrackerConfig {
    double lag_s = 0.00533;             // time between the two samples of each phase difference, s; rounded to
                                        // whole samples: tau_L = 8/r = 5.333 ms at r = 1500 samples/s, whose
                                        // unambiguous range is +/- 1 / (2 tau_L) = +/- 93.75 Hz
    double tau_s = 0.5;                 // time constant of the average, s at weight 1
    double min_weight = 0.6;            // the average must hold this much weight (0..1) before it moves the NCO
    double max_offset_hz = 75.0;        // the NCO frequency (and the anchor) are clamped to +/- this, Hz
    double update_interval_s = 0.0213;  // time between NCO frequency updates, s (32 samples at 1500 samples/s)
    double fine_tune_hz = 12.0;         // the tracker's own estimates are accepted only within +/- this of the
                                        // anchor, Hz (owner decision 2026-09-29, option 1; heuristic)
    double min_coherence = 0.3;         // |average of products| / average of |products| needed to move the NCO
};

// Re-centers one station's channel on its carrier. A numerically controlled
// oscillator (NCO) mixes the channel down by f, the estimated residual offset
// of the carrier from the channel's center, Hz. The phase advance of the
// narrow-filtered, re-centered stream over `lag` samples measures what is left
// of the offset, the NCO's own advance is added back to make it absolute, and
// the result is averaged with the key-down probability as weight.
//
// The tracker does not decide which station it follows (owner decision
// 2026-09-29, option 1): its owner sets an anchor (the engine: the detector's
// current frequency for the track, minus the channel center), and the tracker
// fine-tunes within +/- fine_tune_hz of it.
class FrequencyTracker {
public:
    // Throws std::invalid_argument for a non-positive sample rate, a non-finite initial offset, an
    // invalid config (including a lag or update interval that rounds to 0 samples), or a
    // max_offset_hz or fine_tune_hz not below the unambiguous range 1 / (2 tau_L), where tau_L is
    // the lag rounded to whole samples.
    FrequencyTracker(double sample_rate, double initial_offset_hz, FrequencyTrackerConfig config = {});

    // Mixes one channel sample down by the current offset and advances the NCO.
    Sample mix(Sample y);

    // Feeds one narrow-filtered sample of the mixed stream. weight (0..1) is the
    // probability that the key is down; 0 leaves the estimate unchanged.
    void observe(Sample v, float weight);

    // Where the station is, Hz from the channel center (clamped to the NCO range). If
    // it is more than fine_tune_hz from the NCO, the NCO jumps there and the average
    // starts afresh; otherwise fine-tuning continues around it. A non-finite value is
    // ignored (the previous anchor, offset and average are kept).
    void set_anchor(double anchor_hz);

    double offset_hz() const { return offset_hz_; }  // the NCO frequency, Hz
    double anchor_hz() const { return anchor_hz_; }  // Hz
    // Sets the offset and the anchor to initial_offset_hz and starts afresh. A non-finite
    // value is ignored (nothing changes).
    void reset(double initial_offset_hz);
    // Starts a fresh average (weight 0) but keeps the offset, the anchor and the NCO
    // phase: after a long silence the next station may be a different one.
    void reacquire();

private:
    void clear_average();

    double rate_;
    FrequencyTrackerConfig config_;
    int lag_;                         // lag_s in samples, converted here from the physical value
    int update_every_;                // update_interval_s in samples
    double alpha_;
    double offset_hz_ = 0;
    double anchor_hz_ = 0;            // where the station is, as the owner says, Hz
    double phase_ = 0;                // NCO phase, rad, kept in [-pi, pi)
    std::vector<Sample> history_;     // the last `lag` observed samples, circular
    std::size_t head_ = 0;
    std::size_t seen_ = 0;
    std::complex<double> average_{};  // weighted average of lag products at the absolute offset, FS^2
    double magnitude_ = 0;            // the same average of |product|, FS^2
    double weight_ = 0;               // weight the average holds, 0..1
    int until_update_ = 0;
};

}  // namespace kz4ap
