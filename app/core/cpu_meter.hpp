// The status bar's CPU numbers: from StreamStatus's running totals, the rates over the last kWindowS seconds of wall
// clock and over the whole run, against the project's bar (ms per channel-second; CLAUDE.md, "CPU").
#pragma once

#include "kz4ap/stream_runner.hpp"

#include <deque>
#include <optional>

namespace kz4ap::app {

struct CpuRates {
    // ms of CPU on the pipeline's thread (spectrum, detector, channelizer, decoders, waterfall rows) per channel-second:
    // the bench's cpu_ms_per_channel_s but for its thread alone; none with no channel output in the span.
    std::optional<double> pipeline_ms_per_channel_s;
    // ms of steady-clock time inside decoders per channel-second (the bench's decoder_ms_per_channel_s).
    std::optional<double> decoder_ms_per_channel_s;
    // Stream seconds per wall-clock second over the span; none over an empty span.
    std::optional<double> speed;
    // The bar's two terms from the engine's stage CPU (EngineStageTimes): what recurs whatever the tracks, ms per s of
    // stream, which is ms per s of wall clock at real time; and what one track costs, ms per channel-second.
    std::optional<double> fixed_ms_per_stream_s;
    std::optional<double> per_track_ms_per_channel_s;
};

class CpuMeter {
public:
    // Heuristic: long enough for the thread clock's 16 ms resolution to matter below 1%, short enough to follow a change.
    static constexpr double kWindowS = 10.0;

    void push(const StreamStatus& status);
    CpuRates recent() const;
    CpuRates whole_run() const;

private:
    std::deque<StreamStatus> window_;
    std::optional<StreamStatus> last_;
};

}  // namespace kz4ap::app
