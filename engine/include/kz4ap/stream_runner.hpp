// Runs the engine over a source on a thread of its own, at the source's pace: the entry point for anything that
// streams (the desktop app now, a headless server later). The engine itself stays synchronous; its events reach the
// subscriber on this thread, in order, so a subscriber must not block for long, and a display hands them over.
#pragma once

#include "kz4ap/engine.hpp"
#include "kz4ap/event_bus.hpp"
#include "kz4ap/iq_source.hpp"

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace kz4ap {

struct StreamStatus {
    enum class State {
        Starting,  // building the engine
        Running,
        Finished,  // the source ended and the engine was finished: every event has been published
        Stopped,   // stop() before the source ended
        Failed,    // the engine or the source threw; error says why
    };
    State state = State::Starting;
    std::string error;
    double stream_s = 0;         // stream delivered to the engine so far, s
    double wall_s = 0;           // since the pipeline's thread started, s
    double channel_seconds = 0;  // EngineStats: channel output delivered to decoders, summed over channels, s
    double decoder_seconds = 0;  // EngineStats: CPU inside decoders, s
    EngineStageTimes stages;     // EngineStats: CPU per stage, s
    double thread_cpu_s = 0;     // CPU the pipeline's threads have used (its own and the worker pool's), user plus kernel, s:
                                 // spectrum, detector, channelizer, decoders and the subscriber's own work; about 16 ms
                                 // resolution on Windows
};

class StreamRunner {
public:
    // Starts the thread at once. config.sample_rate is overwritten by the source's rate. subscriber gets every event,
    // on the pipeline's thread. An engine configuration the Engine refuses ends the stream as Failed, as does a source
    // that throws.
    StreamRunner(std::unique_ptr<IqSource> source, EngineConfig config, EventBus::Handler subscriber);
    // Stops and joins.
    ~StreamRunner();
    StreamRunner(const StreamRunner&) = delete;
    StreamRunner& operator=(const StreamRunner&) = delete;

    // Asks the thread to end and waits for it. The engine is not finished: a character in progress stays unpublished.
    void stop();

    StreamStatus status() const;
    IqSource& source() { return *source_; }

private:
    void run(EngineConfig config, EventBus::Handler subscriber);

    std::unique_ptr<IqSource> source_;
    std::atomic<bool> stop_requested_{false};
    mutable std::mutex mutex_;
    StreamStatus status_;
    std::thread thread_;
};

}  // namespace kz4ap
