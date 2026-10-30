#include "kz4ap/stream_runner.hpp"

#include "kz4ap/cpu_time.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <exception>
#include <span>
#include <utility>
#include <vector>

namespace kz4ap {

namespace {

// The longest wait in one read, ms: how long a stop() can take on a source that ignores interrupt(). Heuristic.
constexpr std::chrono::milliseconds kReadTimeout{100};
// The read buffer, s of stream: the most the engine takes at once, so how stale the status can be when unpaced.
// Heuristic.
constexpr double kBufferS = 0.1;

}  // namespace

StreamRunner::StreamRunner(std::unique_ptr<IqSource> source, EngineConfig config, EventBus::Handler subscriber)
    : source_(std::move(source)) {
    config.sample_rate = source_->sample_rate();
    thread_ = std::thread([this, config = std::move(config), subscriber = std::move(subscriber)]() mutable {
        run(std::move(config), std::move(subscriber));
    });
}

StreamRunner::~StreamRunner() { stop(); }

void StreamRunner::stop() {
    stop_requested_ = true;
    source_->interrupt();
    if (thread_.joinable()) thread_.join();
}

StreamStatus StreamRunner::status() const {
    std::lock_guard lock(mutex_);
    return status_;
}

void StreamRunner::run(EngineConfig config, EventBus::Handler subscriber) {
    const auto started = std::chrono::steady_clock::now();
    const double cpu_started = thread_cpu_seconds();
    const double rate = static_cast<double>(config.sample_rate);
    std::uint64_t delivered = 0;
    const auto report = [&](const Engine* engine, StreamStatus::State state, std::string error = {}) {
        StreamStatus s;
        s.state = state;
        s.error = std::move(error);
        s.stream_s = static_cast<double>(delivered) / rate;
        s.wall_s = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
        s.thread_cpu_s = thread_cpu_seconds() - cpu_started;
        if (engine) {
            const EngineStats stats = engine->stats();
            s.channel_seconds = stats.channel_seconds;
            s.decoder_seconds = stats.decoder_seconds;
        }
        std::lock_guard lock(mutex_);
        status_ = std::move(s);
    };
    try {
        EventBus bus;
        bus.subscribe(std::move(subscriber));
        Engine engine(config, bus);
        report(&engine, StreamStatus::State::Running);
        std::vector<Sample> buffer(std::max<std::size_t>(1024, static_cast<std::size_t>(kBufferS * rate)));
        while (!stop_requested_) {
            const IqRead got = source_->read(buffer, kReadTimeout);
            if (got.count > 0) {
                engine.process(std::span<const Sample>(buffer).first(got.count));
                delivered += got.count;
            }
            if (got.end_of_stream) {
                engine.finish();
                report(&engine, StreamStatus::State::Finished);
                return;
            }
            report(&engine, StreamStatus::State::Running);
        }
        report(&engine, StreamStatus::State::Stopped);
    } catch (const std::exception& e) {
        report(nullptr, StreamStatus::State::Failed, e.what());
    }
}

}  // namespace kz4ap
