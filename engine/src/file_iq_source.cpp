#include "kz4ap/file_iq_source.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <stdexcept>

namespace kz4ap {

namespace {

constexpr std::uint64_t kUnlimited = std::numeric_limits<std::uint64_t>::max() / 2;

double monotonic_s() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

}  // namespace

void PlaybackPacer::start(double now_s) {
    if (started_) return;
    started_ = true;
    anchor_time_s_ = now_s;
    anchor_samples_ = static_cast<double>(delivered_);
}

double PlaybackPacer::allowed(double now_s) const {
    if (!started_ || paused_) return anchor_samples_;
    if (std::isinf(speed_)) return std::numeric_limits<double>::infinity();
    return anchor_samples_ + speed_ * rate_ * (now_s - anchor_time_s_);
}

void PlaybackPacer::set_speed(double speed, double now_s) {
    if (!(speed > 0)) throw std::invalid_argument("playback speed must be positive");
    const double here = allowed(now_s);
    // Leaving no pacing: the stream's position is what was delivered.
    anchor_samples_ = std::isinf(here) ? static_cast<double>(delivered_) : here;
    anchor_time_s_ = now_s;
    speed_ = speed;
}

void PlaybackPacer::set_paused(bool paused, double now_s) {
    if (paused == paused_) return;
    if (paused) {
        // Pausing stops the stream where it is: what was due but not yet delivered is no longer owed.
        anchor_samples_ = std::min(allowed(now_s), static_cast<double>(delivered_));
    }
    anchor_time_s_ = now_s;
    paused_ = paused;
}

std::uint64_t PlaybackPacer::available(double now_s) {
    const double here = allowed(now_s);
    if (std::isinf(here)) return kUnlimited;
    const double due = here - static_cast<double>(delivered_);
    if (due <= 0.0) return 0;
    const double cap = kMaxLagWallS * speed_ * rate_;
    if (due > cap) {
        anchor_samples_ = static_cast<double>(delivered_) + cap;
        anchor_time_s_ = now_s;
        return static_cast<std::uint64_t>(cap);
    }
    return static_cast<std::uint64_t>(due);
}

std::uint64_t PlaybackPacer::chunk_samples() const {
    if (std::isinf(speed_)) return kUnlimited;
    return std::max<std::uint64_t>(1, static_cast<std::uint64_t>(kChunkWallS * speed_ * rate_));
}

double PlaybackPacer::seconds_until(std::uint64_t samples, double now_s) const {
    if (!started_ || paused_) return std::numeric_limits<double>::infinity();
    const double here = allowed(now_s);
    if (std::isinf(here)) return 0.0;
    const double due = static_cast<double>(delivered_) + static_cast<double>(samples) - here;
    return due <= 0.0 ? 0.0 : due / (speed_ * rate_);
}

FileIqSource::FileIqSource(const std::filesystem::path& path, double start_s, std::optional<double> center_hz,
                           RecordingOptions options)
    : reader_(path, options), description_(path_text(path.filename())), pacer_(static_cast<double>(reader_.sample_rate())) {
    if (!(start_s >= 0.0)) throw std::invalid_argument("the start of a recording's stream must not be negative (s)");
    const auto start = std::min(reader_.total_samples(), static_cast<std::uint64_t>(start_s * reader_.sample_rate()));
    reader_.seek(start);
    remaining_ = reader_.total_samples() - start;
    if (center_hz) {
        center_hz_ = center_hz;
        center_origin_ = CenterOrigin::Given;
    } else if (reader_.info().center_hz) {
        center_hz_ = reader_.info().center_hz;
        center_origin_ = CenterOrigin::Header;
    } else if ((center_hz_ = center_hz_from_file_name(description_, reader_.file_rate()))) {
        center_origin_ = CenterOrigin::FileName;
    }
    if (reader_.info().start_unix_ms) {
        // The stream's first sample, at the recording's nominal rate.
        start_unix_ms_ = *reader_.info().start_unix_ms +
                         static_cast<std::int64_t>(std::llround(1000.0 * static_cast<double>(start) / reader_.sample_rate()));
    }
}

IqRead FileIqSource::read(std::span<Sample> out, std::chrono::milliseconds timeout) {
    using Clock = std::chrono::steady_clock;
    const Clock::time_point deadline = Clock::now() + timeout;
    const std::uint64_t total = remaining_;
    std::size_t take = 0;
    {
        std::unique_lock lock(mutex_);
        pacer_.start(monotonic_s());
        for (;;) {
            if (interrupted_) return {};
            const std::uint64_t remaining = total - std::min(total, pacer_.total_delivered());
            if (remaining == 0) return IqRead{0, true};
            const std::uint64_t most = std::min<std::uint64_t>(remaining, out.size());
            const std::uint64_t wanted = std::min(most, pacer_.chunk_samples());
            const std::uint64_t due = pacer_.available(monotonic_s());
            if (due >= wanted) {
                take = static_cast<std::size_t>(std::min(most, due));
                break;
            }
            const double wait_s = pacer_.seconds_until(wanted, monotonic_s());
            const Clock::time_point now = Clock::now();
            if (now >= deadline) return {};
            Clock::time_point until = deadline;
            if (wait_s < std::chrono::duration<double>(deadline - now).count())
                until = now + std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(wait_s));
            changed_.wait_until(lock, until);
        }
    }
    // The file read is the slow part, and only this thread touches the reader; set_speed and set_paused do not wait.
    const std::size_t got = reader_.read(out.first(take));
    std::lock_guard lock(mutex_);
    pacer_.delivered(got);
    return IqRead{got, got == 0 || pacer_.total_delivered() >= total};
}

void FileIqSource::interrupt() {
    std::lock_guard lock(mutex_);
    interrupted_ = true;
    changed_.notify_all();
}

void FileIqSource::set_speed(double speed) {
    std::lock_guard lock(mutex_);
    pacer_.set_speed(speed, monotonic_s());
    changed_.notify_all();
}

void FileIqSource::set_paused(bool paused) {
    std::lock_guard lock(mutex_);
    pacer_.set_paused(paused, monotonic_s());
    changed_.notify_all();
}

bool FileIqSource::paused() const {
    std::lock_guard lock(mutex_);
    return pacer_.paused();
}

double FileIqSource::speed() const {
    std::lock_guard lock(mutex_);
    return pacer_.speed();
}

}  // namespace kz4ap
