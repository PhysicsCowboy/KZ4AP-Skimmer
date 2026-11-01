#include "cpu_meter.hpp"

namespace kz4ap::app {

namespace {

CpuRates rates_between(const StreamStatus& a, const StreamStatus& b) {
    CpuRates r;
    const double channel_s = b.channel_seconds - a.channel_seconds;
    if (channel_s > 0) {
        r.pipeline_ms_per_channel_s = 1000.0 * (b.thread_cpu_s - a.thread_cpu_s) / channel_s;
        r.decoder_ms_per_channel_s = 1000.0 * (b.decoder_seconds - a.decoder_seconds) / channel_s;
        r.per_track_ms_per_channel_s =
            1000.0 * (b.stages.filter_s + b.stages.decoder_s - a.stages.filter_s - a.stages.decoder_s) / channel_s;
    }
    const double wall_s = b.wall_s - a.wall_s;
    if (wall_s > 0) r.speed = (b.stream_s - a.stream_s) / wall_s;
    const double stream_s = b.stream_s - a.stream_s;
    if (stream_s > 0) {
        const auto fixed = [](const EngineStageTimes& s) {
            return s.spectrum_s + s.detector_s + s.hiss_s + s.publish_s + s.lifecycle_s + s.channelizer_s;
        };
        r.fixed_ms_per_stream_s = 1000.0 * (fixed(b.stages) - fixed(a.stages)) / stream_s;
    }
    return r;
}

}  // namespace

void CpuMeter::push(const StreamStatus& status) {
    last_ = status;
    window_.push_back(status);
    while (window_.size() > 1 && status.wall_s - window_.front().wall_s > kWindowS) window_.pop_front();
}

CpuRates CpuMeter::recent() const {
    if (window_.empty()) return {};
    return rates_between(window_.front(), window_.back());
}

CpuRates CpuMeter::whole_run() const {
    if (!last_) return {};
    // From the stream's start, not the first look at it: a status of all zeros.
    return rates_between(StreamStatus{}, *last_);
}

}  // namespace kz4ap::app
