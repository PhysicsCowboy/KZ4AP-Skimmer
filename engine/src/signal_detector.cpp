#include "kz4ap/signal_detector.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace {

// Validates the detector's parameters. Runs as the first-declared member's
// (config_'s) initializer, before alpha_ divides by sample_rate/average_s or
// average_ is sized from fft_size, so an invalid parameter throws
// std::invalid_argument instead of producing NaN or a bad allocation size.
const kz4ap::DetectorConfig& ValidatedConfig(const kz4ap::DetectorConfig& config) {
    if (config.sample_rate <= 0 || config.fft_size <= 0 || config.hop <= 0 ||
        config.average_s <= 0 || config.birth_s < 0 || config.death_s <= 0 ||
        config.max_tracks == 0 || config.min_separation_bins < 1 || !(config.attribution_distance_hz > 0) ||
        !(config.peak_radius_hz > 0) || !(config.level_radius_hz >= 0) || !(config.candidate_step_hz >= 0))
        throw std::invalid_argument("invalid signal detector config");
    return config;
}

}  // namespace

namespace kz4ap {

SignalDetector::SignalDetector(const DetectorConfig& config)
    : config_(ValidatedConfig(config)),
      alpha_(1.0 - std::exp(-(static_cast<double>(config_.hop) / config_.sample_rate) / config_.average_s)),
      average_(static_cast<std::size_t>(config_.fft_size), 0.0) {
    // The neighborhoods are stated in Hz; convert them to bins at this bin width.
    const double bin_hz = static_cast<double>(config_.sample_rate) / config_.fft_size;
    peak_bins_ = std::max(1, static_cast<int>(std::lround(config_.peak_radius_hz / bin_hz)));
    level_bins_ = static_cast<int>(std::lround(config_.level_radius_hz / bin_hz));
    step_bins_ = static_cast<int>(std::lround(config_.candidate_step_hz / bin_hz));
}

DetectorUpdate SignalDetector::process(const SpectrumFrame& frame) {
    const auto n = frame.power_db.size();
    if (n != static_cast<std::size_t>(config_.fft_size))
        throw std::invalid_argument("spectrum frame size does not match detector fft_size");
    const double now = frame.time_s;
    if (frames_seen_ == 0) first_frame_s_ = now;
    ++frames_seen_;

    // Running mean at first, then an exponential average.
    const double a = std::max(alpha_, 1.0 / static_cast<double>(frames_seen_));
    std::vector<float> avg_db(n);
    for (std::size_t i = 0; i < n; ++i) {
        average_[i] += a * (std::pow(10.0, frame.power_db[i] / 10.0) - average_[i]);
        avg_db[i] = static_cast<float>(10.0 * std::log10(average_[i] + 1e-30));
    }
    std::vector<float> sorted = avg_db;
    std::nth_element(sorted.begin(), sorted.begin() + static_cast<std::ptrdiff_t>(n / 2), sorted.end());
    const float floor_db = sorted[n / 2];

    DetectorUpdate update;
    const int last = static_cast<int>(n) - 1;

    if (config_.attribution == Attribution::Distance) follow_peaks(avg_db, floor_db);

    // Refresh existing tracks; expire the ones that have been quiet too long.
    for (auto it = active_.begin(); it != active_.end();) {
        float level = avg_db[it->bin];
        for (int d = -level_bins_; d <= level_bins_; ++d) {
            const int b = it->bin + d;
            if (b >= 0 && b <= last) level = std::max(level, avg_db[b]);
        }
        it->track.snr_db = level - floor_db;
        if (it->track.snr_db >= config_.threshold_db - config_.hysteresis_db) it->track.last_active_s = now;
        if (now - it->track.last_active_s >= config_.death_s) {
            update.died.push_back(it->track.id);
            it = active_.erase(it);
        } else {
            ++it;
        }
    }

    // Until the averages settle, a single noisy frame can look like a signal.
    if (now - first_frame_s_ < config_.average_s) return update;

    // Local peaks above threshold that do not belong to an existing track.
    for (auto& c : candidates_) c.seen = false;
    for (int i = 0; i <= last; ++i) {
        const float snr = avg_db[i] - floor_db;
        if (snr < config_.threshold_db) continue;
        if (!is_peak(avg_db, i)) continue;
        bool near_track = false;
        if (config_.attribution == Attribution::Bins) {
            near_track = std::any_of(active_.begin(), active_.end(), [&](const Active& t) {
                return std::abs(t.bin - i) < config_.min_separation_bins;
            });
        } else {
            // Within D of a track's current frequency (which follows its own peak), the peak is that
            // track's station or its spread.
            const double f = refined_freq(avg_db, i);
            near_track = std::any_of(active_.begin(), active_.end(), [&](const Active& t) {
                return std::abs(t.track.freq_hz - f) < config_.attribution_distance_hz;
            });
        }
        if (near_track) continue;
        auto c = std::find_if(candidates_.begin(), candidates_.end(),
                              [&](const Candidate& k) { return std::abs(k.bin - i) <= step_bins_; });
        if (c != candidates_.end()) {
            c->bin = i;
            c->snr_db = snr;
            c->seen = true;
        } else {
            candidates_.push_back({i, now, snr, true});
        }
    }
    std::erase_if(candidates_, [](const Candidate& c) { return !c.seen; });

    // Promote candidates that have persisted long enough, strongest first.
    std::vector<Candidate> ready;
    std::erase_if(candidates_, [&](const Candidate& c) {
        if (now - c.first_seen_s < config_.birth_s) return false;
        ready.push_back(c);
        return true;
    });
    std::stable_sort(ready.begin(), ready.end(),
                     [](const Candidate& x, const Candidate& y) { return x.snr_db > y.snr_db; });
    for (const auto& c : ready) {
        if (active_.size() >= config_.max_tracks) {
            auto weakest = std::min_element(active_.begin(), active_.end(), [](const Active& x, const Active& y) {
                return x.track.snr_db < y.track.snr_db;
            });
            if (weakest == active_.end() || weakest->track.snr_db >= c.snr_db) continue;
            update.died.push_back(weakest->track.id);
            active_.erase(weakest);
        }
        Track t;
        t.id = next_id_++;
        t.freq_hz = refined_freq(avg_db, c.bin);
        t.snr_db = c.snr_db;
        t.start_time_s = c.first_seen_s;
        t.last_active_s = now;
        active_.push_back({t, c.bin});
        update.born.push_back(t);
    }
    return update;
}

std::vector<Track> SignalDetector::tracks() const {
    std::vector<Track> out;
    out.reserve(active_.size());
    for (const auto& a : active_) out.push_back(a.track);
    return out;
}

bool SignalDetector::is_peak(const std::vector<float>& avg_db, int i) const {
    const int last = static_cast<int>(avg_db.size()) - 1;
    if (i < peak_bins_ || i > last - peak_bins_) return false;
    for (int d = -peak_bins_; d <= peak_bins_; ++d) {
        if (d < 0 && avg_db[i + d] >= avg_db[i]) return false;  // ties go to the lower bin
        if (d > 0 && avg_db[i + d] > avg_db[i]) return false;
    }
    return true;
}

void SignalDetector::follow_peaks(const std::vector<float>& avg_db, float floor_db) {
    // Owner decisions 2026-09-29, option 1: the detector alone decides which station a channel
    // follows. Each track moves to the strongest peak (by the birth rule) that stands at least the
    // keep-alive level above the floor and whose interpolated frequency is within D of the track's
    // current frequency; with none (the station is silent, or only a stronger neighbor's skirt is
    // there, which is not a local maximum) it holds. A peak beyond D can become a track of its own.
    const float keep_alive_db = config_.threshold_db - config_.hysteresis_db;
    const double bin_hz = static_cast<double>(config_.sample_rate) / config_.fft_size;
    const int reach = static_cast<int>(std::ceil(config_.attribution_distance_hz / bin_hz)) + 1;
    const int last = static_cast<int>(avg_db.size()) - 1;
    for (auto& t : active_) {
        int best = -1;
        double best_freq = 0;
        for (int i = std::max(0, t.bin - reach); i <= std::min(last, t.bin + reach); ++i) {
            if (avg_db[i] - floor_db < keep_alive_db || !is_peak(avg_db, i)) continue;
            const double f = refined_freq(avg_db, i);
            if (!(std::abs(f - t.track.freq_hz) < config_.attribution_distance_hz)) continue;
            if (best < 0 || avg_db[i] > avg_db[best]) {
                best = i;
                best_freq = f;
            }
        }
        if (best >= 0) {
            t.bin = best;
            t.track.freq_hz = best_freq;
        }
    }
}

double SignalDetector::refined_freq(const std::vector<float>& avg_db, int bin) const {
    // Parabolic interpolation over the peak and its neighbors.
    double delta = 0;
    if (bin > 0 && bin + 1 < static_cast<int>(avg_db.size())) {
        const double a = avg_db[bin - 1], b = avg_db[bin], c = avg_db[bin + 1];
        const double denom = a - 2 * b + c;
        if (denom != 0) delta = std::clamp(0.5 * (a - c) / denom, -0.5, 0.5);
    }
    return (bin + delta - config_.fft_size / 2) * static_cast<double>(config_.sample_rate) / config_.fft_size;
}

}  // namespace kz4ap
