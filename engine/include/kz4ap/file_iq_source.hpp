// A recording as an IqSource: the project's WAV form (wav_reader.hpp), delivered at a chosen pace (design section 4.1,
// step 1: real time, faster, or as fast as possible), or paused.
#pragma once

#include "kz4ap/iq_source.hpp"
#include "kz4ap/wav_reader.hpp"

#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <mutex>

namespace kz4ap {

// The speed that means no pacing at all (the benchmark's mode, and the transport's "max").
inline constexpr double kAsFastAsPossible = std::numeric_limits<double>::infinity();

// The pacing arithmetic, without a clock or a lock (times are given, s, on any one monotonic clock). The stream may
// have reached anchor + speed x rate x (now - anchor time) samples by now; a change of speed or a pause moves the
// anchor, so the stream's position is continuous.
class PlaybackPacer {
public:
    // How far a consumer that fell behind may catch up afterward, s of wall clock at the current speed; beyond it the
    // debt is forgiven, so a consumer that could not keep up at 10x does not race when set back to 1x. Heuristic.
    static constexpr double kMaxLagWallS = 0.25;
    // A read waits until this much wall clock's worth of samples is due, s: 50 reads/s at most, faster than the
    // engine's hop of 21.33 ms has any use for. Heuristic.
    static constexpr double kChunkWallS = 0.02;

    // rate_hz: samples/s of stream, > 0. No sample is due before start().
    explicit PlaybackPacer(double rate_hz) : rate_(rate_hz) {}

    void start(double now_s);
    void set_speed(double speed, double now_s);
    double speed() const { return speed_; }
    void set_paused(bool paused, double now_s);
    bool paused() const { return paused_; }
    // Samples due now beyond those already delivered; forgives a debt beyond kMaxLagWallS. A huge value at
    // kAsFastAsPossible.
    std::uint64_t available(double now_s);
    void delivered(std::uint64_t samples) { delivered_ += samples; }
    std::uint64_t total_delivered() const { return delivered_; }
    // Samples one chunk of kChunkWallS comes to at this speed, at least 1.
    std::uint64_t chunk_samples() const;
    // s from now_s until `samples` beyond those delivered are due; infinity while paused or not started.
    double seconds_until(std::uint64_t samples, double now_s) const;

private:
    double allowed(double now_s) const;

    double rate_;
    double speed_ = 1.0;
    bool paused_ = false;
    bool started_ = false;
    double anchor_time_s_ = 0.0;
    double anchor_samples_ = 0.0;
    std::uint64_t delivered_ = 0;
};

class FileIqSource final : public IqSource, public PlaybackControl {
public:
    // Throws std::runtime_error if the file cannot be opened or is not 16-bit stereo PCM (WavIqReader).
    // start_s: where the stream begins in the recording, s, clipped to its end: a seek is a new stream from there.
    explicit FileIqSource(const std::filesystem::path& path, double start_s = 0.0);

    int sample_rate() const override { return reader_.sample_rate(); }
    std::string description() const override { return description_; }
    // The samples this stream delivers: from start_s to the recording's end.
    std::optional<std::uint64_t> total_samples() const override { return remaining_; }
    // The recording's whole length, s, wherever this stream began.
    double recording_length_s() const { return static_cast<double>(reader_.total_samples()) / reader_.sample_rate(); }
    IqRead read(std::span<Sample> out, std::chrono::milliseconds timeout) override;
    void interrupt() override;
    PlaybackControl* playback() override { return this; }

    void set_speed(double speed) override;
    void set_paused(bool paused) override;
    bool paused() const override;
    double speed() const override;

private:
    WavIqReader reader_;  // read() only: the pipeline's thread
    std::uint64_t remaining_ = 0;  // samples from the stream's start to the recording's end
    std::string description_;
    mutable std::mutex mutex_;
    std::condition_variable changed_;
    PlaybackPacer pacer_;
    bool interrupted_ = false;
};

}  // namespace kz4ap
