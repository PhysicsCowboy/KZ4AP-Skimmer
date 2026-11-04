// Where the pipeline's I/Q samples come from (design section 4.1, step 1): a recording (FileIqSource) or, next, the
// SDRplay API. The stream runner (stream_runner.hpp) and the app see only this interface; a source that lives on a
// driver's thread keeps its own ring buffer behind read().
#pragma once

#include "kz4ap/iq_recorder.hpp"
#include "kz4ap/types.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
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

// Saving the raw I/Q of a live source (the project's WAV form). May be called from any thread but the source's driver's.
class RecordControl {
public:
    virtual ~RecordControl() = default;
    // Throws std::runtime_error if the file cannot be created or a recording is running.
    virtual void start_recording(const std::filesystem::path& path) = 0;
    virtual void stop_recording() = 0;
    virtual RecordingStatus recording_status() const = 0;
};

struct RadioStatus {
    int if_gain_reduction_db = 0;   // dB, as set
    int lna_state = 0;              // as set
    std::optional<double> gain_db;  // the radio's own figure for its total gain, dB, when it has said
    bool overload = false;          // the radio's ADC is overloaded now
    std::uint64_t resets = 0;       // times the radio's stream restarted
    std::uint64_t received = 0;     // samples the radio has delivered
};

// The controls of a live radio that do not end the stream. A radio can only receive: nothing here or anywhere else in this
// program transmits. May be called from any thread.
class RadioControl {
public:
    virtual ~RadioControl() = default;
    // Throws std::invalid_argument out of range, std::runtime_error if the radio refuses.
    virtual void set_gain(int if_gain_reduction_db, int lna_state) = 0;
    virtual RadioStatus radio_status() const = 0;
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
    // The radio's tuned frequency at 0 Hz in the samples, Hz, if known (FileIqSource: from the recording's header or its
    // file name; otherwise the display shows kHz relative to center only).
    virtual std::optional<double> center_frequency_hz() const { return std::nullopt; }
    // The time of the stream's first sample, ms since 1970-01-01 00:00 UTC, if known (a recording whose header says).
    virtual std::optional<std::int64_t> start_unix_ms() const { return std::nullopt; }
    // Fills the front of out with the next samples, at most out.size(). Waits at most timeout for samples; with none
    // by then returns count 0, end_of_stream false, and the caller calls again. Called from one thread at a time, the
    // pipeline's. Throws std::runtime_error if the source fails; the stream ends there.
    virtual IqRead read(std::span<Sample> out, std::chrono::milliseconds timeout) = 0;
    // Makes a blocked read return at once, and every later one without waiting. Callable from any thread.
    virtual void interrupt() = 0;
    // The transport of a recording; nullptr for a live source.
    virtual PlaybackControl* playback() { return nullptr; }
    // A live source's: samples lost because the pipeline fell behind (a gap in the stream), the recorder, the radio's controls.
    virtual std::uint64_t dropped_samples() const { return 0; }
    // A live source's: the samples waiting for the pipeline, s of stream. Called from the pipeline's thread.
    virtual double backlog_s() const { return 0.0; }
    virtual RecordControl* recorder() { return nullptr; }
    virtual RadioControl* radio() { return nullptr; }
};

}  // namespace kz4ap
