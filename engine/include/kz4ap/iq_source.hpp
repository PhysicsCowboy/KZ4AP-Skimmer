// Where the pipeline's I/Q samples come from (design section 4.1, step 1): a recording (FileIqSource) or, next, the
// SDRplay API. The stream runner (stream_runner.hpp) and the app see only this interface; a source that lives on a
// driver's thread keeps its own ring buffer behind read().
#pragma once

#include "kz4ap/types.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>

namespace kz4ap {

// What a recording can do and a live radio cannot. Every member may be called from any thread.
class PlaybackControl {
public:
    virtual ~PlaybackControl() = default;
    // speed: stream seconds per second of wall clock, > 0; kAsFastAsPossible (file_iq_source.hpp) for no pacing. Throws
    // std::invalid_argument otherwise.
    virtual void set_speed(double speed) = 0;
    virtual void set_paused(bool paused) = 0;
    virtual bool paused() const = 0;
    virtual double speed() const = 0;
};

struct IqRead {
    std::size_t count = 0;       // samples written to the front of out
    bool end_of_stream = false;  // no sample will follow these; a live source that stays open never sets it
};

class IqSource {
public:
    virtual ~IqSource() = default;
    // Complex samples/s; fixed for the source's life.
    virtual int sample_rate() const = 0;
    // A name for display: the recording's file name, the radio's model.
    virtual std::string description() const = 0;
    // The stream's length in samples, if it has one (a live source does not).
    virtual std::optional<std::uint64_t> total_samples() const { return std::nullopt; }
    // The radio's tuned frequency at 0 Hz in the samples, Hz, if known (a recording in the project's WAV form does not
    // say; the display then shows kHz relative to center only).
    virtual std::optional<double> center_frequency_hz() const { return std::nullopt; }
    // Fills the front of out with the next samples, at most out.size(). Waits at most timeout for samples; with none
    // by then returns count 0, end_of_stream false, and the caller calls again. Called from one thread at a time, the
    // pipeline's. Throws std::runtime_error if the source fails; the stream ends there.
    virtual IqRead read(std::span<Sample> out, std::chrono::milliseconds timeout) = 0;
    // Makes a blocked read return at once, and every later one without waiting. Callable from any thread.
    virtual void interrupt() = 0;
    // The transport of a recording; nullptr for a live source.
    virtual PlaybackControl* playback() { return nullptr; }
};

}  // namespace kz4ap
