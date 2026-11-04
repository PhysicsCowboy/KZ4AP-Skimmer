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
        config.max_tracks == 0 || config.floor_every_frames < 1 || config.min_separation_bins < 1 || !(config.attribution_distance_hz > 0) ||
        !(config.peak_radius_hz > 0) || !(config.level_radius_hz >= 0) || !(config.candidate_step_hz >= 0) || !(config.local_floor_radius_hz >= 0) ||
        !(config.prominence_db >= 0) || !(config.prominence_radius_hz >= 0) || !(config.dc_guard_hz >= 0) || !(config.follow_radius_hz >= 0) ||
        !(config.sideband_db >= 0) || !(config.sideband_mirror_db >= 0) || !(config.sideband_radius_hz >= 0) || config.fft_factor < 1 ||
        (config.fft_factor & (config.fft_factor - 1)) != 0)
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
    local_floor_bins_ = static_cast<int>(std::lround(config_.local_floor_radius_hz / bin_hz));
    prominence_bins_ = static_cast<int>(std::lround(config_.prominence_radius_hz / bin_hz));
    sideband_bins_ = static_cast<int>(std::lround(config_.sideband_radius_hz / bin_hz));
    track_limit_ = config_.max_tracks;
}

DetectorUpdate SignalDetector::process(const SpectrumFrame& frame) {
    // A frame from an analyzer without decibels carries linear power only.
    const auto n = frame.power_db.empty() ? frame.power_fs2.size() : frame.power_db.size();
    if (n != static_cast<std::size_t>(config_.fft_size))
        throw std::invalid_argument("spectrum frame size does not match detector fft_size");
    const double now = frame.time_s;
    if (frames_seen_ == 0) first_frame_s_ = now;
    ++frames_seen_;

    // Running mean at first, then an exponential average. All this works on the linear average: every bin's dB is
    // needed by nothing but the few peaks that clear the threshold (a logarithm per bin was most of the detector's CPU).
    const double a = std::max(alpha_, 1.0 / static_cast<double>(frames_seen_));
    if (frame.power_fs2.size() == n) {
        for (std::size_t i = 0; i < n; ++i) average_[i] += a * (static_cast<double>(frame.power_fs2[i]) - average_[i]);
    } else {
        for (std::size_t i = 0; i < n; ++i) average_[i] += a * (std::pow(10.0, frame.power_db[i] / 10.0) - average_[i]);
    }
    if ((frames_seen_ - 1) % static_cast<std::uint64_t>(config_.floor_every_frames) == 0) {
        scratch_ = average_;
        std::nth_element(scratch_.begin(), scratch_.begin() + static_cast<std::ptrdiff_t>(n / 2), scratch_.end());
        floor_db_ = static_cast<float>(10.0 * std::log10(scratch_[n / 2] + 1e-30));
    }
    const float floor_db = floor_db_;

    DetectorUpdate update;
    const int last = static_cast<int>(n) - 1;

    if (config_.attribution == Attribution::Distance) follow_peaks(floor_db);
    if (config_.one_track_per_peak) {
        for (std::size_t i = active_.size(); i-- > 0;) {
            if (!within_older(i, active_[i].track.freq_hz)) continue;
            update.died.push_back(active_[i].track.id);
            update.merged.push_back(active_[i].track.id);
            active_.erase(active_.begin() + static_cast<std::ptrdiff_t>(i));
        }
    }

    // Refresh existing tracks; expire the ones that have been quiet too long.
    for (auto it = active_.begin(); it != active_.end();) {
        double level_power = average_[it->bin];
        for (int d = -level_bins_; d <= level_bins_; ++d) {
            const int b = it->bin + d;
            if (b >= 0 && b <= last) level_power = std::max(level_power, average_[b]);
        }
        it->track.snr_db = static_cast<float>(10.0 * std::log10(level_power + 1e-30)) - floor_db;
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
    // Below this linear power a bin cannot clear the threshold, dB rounded to float or not (the margin: 1e-3 dB).
    const double threshold_power = std::pow(10.0, (static_cast<double>(floor_db) + config_.threshold_db - 1e-3) / 10.0);
    for (int i = 0; i <= last; ++i) {
        if (average_[static_cast<std::size_t>(i)] + 1e-30 < threshold_power) continue;
        const float snr = db_at(i) - floor_db;
        if (snr < config_.threshold_db) continue;
        if (!is_peak(i)) continue;
        bool near_track = false;
        if (config_.attribution == Attribution::Bins) {
            near_track = std::any_of(active_.begin(), active_.end(), [&](const Active& t) {
                return std::abs(t.bin - i) < config_.min_separation_bins;
            });
        } else {
            // Within D_ch of a track's current frequency (which follows its own peak), the peak is that
            // track's station or its spread.
            const double f = refined_freq(i);
            near_track = std::any_of(active_.begin(), active_.end(), [&](const Active& t) {
                return std::abs(t.track.freq_hz - f) < config_.attribution_distance_hz;
            });
        }
        if (near_track) continue;
        if (std::abs(refined_freq(i)) < config_.dc_guard_hz) continue;
        if (!holds_.empty()) {
            const double f = refined_freq(i);
            const bool held = std::any_of(holds_.begin(), holds_.end(), [&](const Hold& h) {
                return now < h.until_s && std::abs(h.freq_hz - f) < h.radius_hz;
            });
            if (held) continue;
        }
        if (local_floor_bins_ > 0) {
            // A carrier stands out from what is around it; a peak of a wide signal's spectrum does not, though it may stand
            // well above the band's median.
            const auto lo = static_cast<std::ptrdiff_t>(std::max(0, i - local_floor_bins_));
            const auto hi = static_cast<std::ptrdiff_t>(std::min(last, i + local_floor_bins_)) + 1;
            scratch_.assign(average_.begin() + lo, average_.begin() + hi);
            const auto middle = scratch_.begin() + static_cast<std::ptrdiff_t>(scratch_.size() / 4);
            std::nth_element(scratch_.begin(), middle, scratch_.end());
            if (db_at(i) - static_cast<float>(10.0 * std::log10(*middle + 1e-30)) < config_.threshold_db) continue;
        }
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
        if (active_.size() >= track_limit_) {
            auto weakest = std::min_element(active_.begin(), active_.end(), [](const Active& x, const Active& y) {
                return x.track.snr_db < y.track.snr_db;
            });
            if (weakest == active_.end() || weakest->track.snr_db >= c.snr_db) continue;
            update.died.push_back(weakest->track.id);
            active_.erase(weakest);
        }
        Track t;
        t.id = next_id_++;
        t.freq_hz = refined_freq(c.bin);
        t.snr_db = c.snr_db;
        t.start_time_s = c.first_seen_s;
        t.last_active_s = now;
        active_.push_back({t, c.bin});
        update.born.push_back(t);
    }
    return update;
}

void SignalDetector::retire(std::uint32_t id, double now_s, double hold_s) {
    const auto it = std::find_if(active_.begin(), active_.end(), [&](const Active& a) { return a.track.id == id; });
    if (it == active_.end()) return;
    const double freq_hz = it->track.freq_hz;
    active_.erase(it);
    if (!(hold_s > 0.0)) return;
    // A hold forgets its repeats once the place has been open as long as the longest hold (heuristic).
    std::erase_if(holds_, [&](const Hold& h) { return now_s - h.until_s > kLongestHoldS; });
    close_place(freq_hz, config_.attribution_distance_hz, now_s, hold_s);
    std::erase_if(retirements_, [&](const Retirement& r) { return now_s - r.at_s > kRegionMemoryS; });
    retirements_.push_back({freq_hz, now_s});
    const auto nearby = std::count_if(retirements_.begin(), retirements_.end(),
                                      [&](const Retirement& r) { return std::abs(r.freq_hz - freq_hz) < kRegionRadiusHz; });
    if (nearby >= kRegionRetirements) close_place(freq_hz, kRegionRadiusHz, now_s, kRegionHoldS);
}

void SignalDetector::close_place(double freq_hz, double radius_hz, double now_s, double hold_s) {
    auto hold = std::find_if(holds_.begin(), holds_.end(),
                             [&](const Hold& h) { return h.radius_hz == radius_hz && std::abs(h.freq_hz - freq_hz) < radius_hz; });
    if (hold == holds_.end()) {
        holds_.push_back({freq_hz, 0.0, 0, radius_hz});
        hold = holds_.end() - 1;
    }
    // A repeat is a retirement after the hold before it was over. While the hold runs (the tracks of one wide signal, retiring
    // within seconds of each other) the place is already closed: the hold reaches as far from now as the same step would, no further.
    const bool repeat = hold->count == 0 || now_s >= hold->until_s;
    if (repeat) ++hold->count;
    hold->until_s = std::max(hold->until_s, now_s + std::min(hold_s * std::ldexp(1.0, hold->count - 1), kLongestHoldS));
}

bool SignalDetector::within_older(std::size_t track, double freq_hz) const {
    for (std::size_t older = 0; older < track; ++older) {
        if (std::abs(active_[older].track.freq_hz - freq_hz) < config_.attribution_distance_hz) return true;
    }
    return false;
}

std::vector<Track> SignalDetector::tracks() const {
    std::vector<Track> out;
    out.reserve(active_.size());
    for (const auto& a : active_) out.push_back(a.track);
    return out;
}

float SignalDetector::db_at(int bin) const {
    return static_cast<float>(10.0 * std::log10(average_[static_cast<std::size_t>(bin)] + 1e-30));
}

bool SignalDetector::is_peak(int i) const {
    const int last = static_cast<int>(average_.size()) - 1;
    if (i < peak_bins_ || i > last - peak_bins_) return false;
    for (int d = -peak_bins_; d <= peak_bins_; ++d) {
        if (d < 0 && average_[i + d] >= average_[i]) return false;  // ties go to the lower bin
        if (d > 0 && average_[i + d] > average_[i]) return false;
    }
    if (config_.prominence_db > 0.0f) {
        // Each side: walking out, the lowest bin so far; at the first bin above this one, that low must lie prominence_db below.
        const double ratio = std::pow(10.0, static_cast<double>(config_.prominence_db) / 10.0);
        for (const int step : {-1, 1}) {
            double low = average_[i];
            for (int d = 1; d <= prominence_bins_; ++d) {
                const int b = i + step * d;
                if (b < 0 || b > last) break;
                if (average_[b] > average_[i]) {
                    if (average_[i] < ratio * low) return false;
                    break;
                }
                low = std::min(low, average_[b]);
            }
        }
    }
    if (config_.sideband_db > 0.0f) {
        // A keyed carrier's power spectrum is symmetric about it (a real envelope), so its keying sidebands come in pairs: a peak
        // whose mirror about a much stronger bin on one side stands as high, within sideband_mirror_db, is that bin's sideband.
        const double above = std::pow(10.0, static_cast<double>(config_.sideband_db) / 10.0);
        const double mirror_low = std::pow(10.0, -static_cast<double>(config_.sideband_mirror_db) / 10.0);
        for (const int step : {-1, 1}) {
            int carrier = -1;
            for (int d = 1; d <= sideband_bins_; ++d) {
                const int b = i + step * d;
                if (b < 0 || b > last) break;
                if (carrier < 0 || average_[b] > average_[carrier]) carrier = b;
            }
            if (carrier < 0 || average_[carrier] < above * average_[i]) continue;
            const int m = 2 * carrier - i;
            double mirror = 0.0;
            for (int b = m - 1; b <= m + 1; ++b)
                if (b >= 0 && b <= last) mirror = std::max(mirror, average_[b]);
            if (mirror >= mirror_low * average_[i]) return false;
        }
    }
    return true;
}

void SignalDetector::follow_peaks(float floor_db) {
    // Owner decisions 2026-09-29, option 1: the detector alone decides which station a channel
    // follows. Each track moves to the strongest peak (by the birth rule) that stands at least the
    // keep-alive level above the floor and whose interpolated frequency is within D_ch of the track's
    // current frequency; with none (the station is silent, or only a stronger neighbor's skirt is
    // there, which is not a local maximum) it holds. A peak beyond D_ch can become a track of its own.
    // Without one_track_per_peak it does not check whether another track already holds the peak, and tracks
    // do converge on one station: on real 40 m up to 19 of them on one frequency.
    const float keep_alive_db = config_.threshold_db - config_.hysteresis_db;
    const double bin_hz = static_cast<double>(config_.sample_rate) / config_.fft_size;
    const double radius_hz = config_.follow_radius_hz > 0.0 ? config_.follow_radius_hz : config_.attribution_distance_hz;
    const int reach = static_cast<int>(std::ceil(radius_hz / bin_hz)) + 1;
    const int last = static_cast<int>(average_.size()) - 1;
    for (std::size_t index = 0; index < active_.size(); ++index) {
        Active& t = active_[index];
        int best = -1;
        double best_freq = 0;
        for (int i = std::max(0, t.bin - reach); i <= std::min(last, t.bin + reach); ++i) {
            if (db_at(i) - floor_db < keep_alive_db || !is_peak(i)) continue;
            const double f = refined_freq(i);
            if (!(std::abs(f - t.track.freq_hz) < radius_hz)) continue;
            if (config_.one_track_per_peak && within_older(index, f)) continue;
            if (best < 0 || average_[i] > average_[best]) {
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

double SignalDetector::refined_freq(int bin) const {
    // Parabolic interpolation over the peak and its neighbors.
    double delta = 0;
    if (bin > 0 && bin + 1 < static_cast<int>(average_.size())) {
        const double a = db_at(bin - 1), b = db_at(bin), c = db_at(bin + 1);
        const double denom = a - 2 * b + c;
        if (denom != 0) delta = std::clamp(0.5 * (a - c) / denom, -0.5, 0.5);
    }
    return (bin + delta - config_.fft_size / 2) * static_cast<double>(config_.sample_rate) / config_.fft_size;
}

}  // namespace kz4ap
