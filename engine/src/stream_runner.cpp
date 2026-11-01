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

// A pipeline that falls behind a live source gives up tracks rather than lose samples. All heuristics, against the source's ring
// (5.46 s at every rate the radio offers: SdrplaySource::kRingS). Behind by more than this much, s of stream, with the backlog grown
// over kGrowingChecks successive checks kShedEveryS of stream apart and more than kFewestTracks live, the budget becomes three
// quarters of the live tracks. A backlog that is draining (left by a stall, or by the engine's own construction at a high rate) goes
// by itself, and shedding would lose stations for nothing. Caught up to kCaughtUpS, the budget grows by kRestoreTracks each
// kRestoreEveryS and in the end goes away.
constexpr double kShedAboveS = 1.0;
constexpr int kGrowingChecks = 2;
constexpr double kGrowthFromNothingS = 0.05;  // below kCaughtUpS, growth smaller than this, s of stream per check, is not growth
constexpr double kCaughtUpS = 0.25;
constexpr double kShedEveryS = 1.0;
constexpr double kRestoreEveryS = 5.0;
constexpr std::size_t kRestoreTracks = 5;
constexpr std::size_t kFewestTracks = 8;

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
    std::size_t budget = 0;  // tracks; 0: none
    const auto report = [&](const Engine* engine, StreamStatus::State state, std::string error = {}) {
        StreamStatus s;
        s.state = state;
        s.error = std::move(error);
        s.stream_s = static_cast<double>(delivered) / rate;
        s.wall_s = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
        s.thread_cpu_s = thread_cpu_seconds() - cpu_started + (engine ? engine->worker_cpu_seconds() : 0.0);
        if (engine) {
            const EngineStats stats = engine->stats();
            s.channel_seconds = stats.channel_seconds;
            s.replayed_channel_seconds = stats.replayed_channel_seconds;
            s.longest_replay_wait_s = stats.longest_replay_wait_s;
            s.decoder_seconds = stats.decoder_seconds;
            s.stages = stats.stages;
            s.track_budget = budget;
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
        double budget_changed_s = 0.0;
        double backlog_checked_s = 0.0, checked_at_s = 0.0;  // s of stream
        int growing_checks = 0;
        while (!stop_requested_) {
            const IqRead got = source_->read(buffer, kReadTimeout);
            if (got.count > 0) {
                engine.process(std::span<const Sample>(buffer).first(got.count));
                delivered += got.count;
                const double stream_s = static_cast<double>(delivered) / rate;
                const double backlog_s = source_->backlog_s();
                bool shed = false;
                if (stream_s - checked_at_s >= kShedEveryS) {
                    // Near no backlog at all it wanders by milliseconds, read to read, and a stall's one jump would then be the
                    // second "growth" in a row.
                    const double margin_s = backlog_checked_s > kCaughtUpS ? 0.0 : kGrowthFromNothingS;
                    growing_checks = backlog_s > backlog_checked_s + margin_s ? growing_checks + 1 : 0;
                    shed = backlog_s > kShedAboveS && growing_checks >= kGrowingChecks && engine.live_tracks() > kFewestTracks;
                    backlog_checked_s = backlog_s;
                    checked_at_s = stream_s;
                }
                if (shed) {
                    budget = std::max(kFewestTracks, engine.live_tracks() * 3 / 4);
                    engine.set_track_budget(budget);
                    budget_changed_s = stream_s;
                } else if (budget > 0 && backlog_s < kCaughtUpS && stream_s - budget_changed_s >= kRestoreEveryS) {
                    budget += kRestoreTracks;
                    if (budget >= config.detector.max_tracks) budget = 0;
                    engine.set_track_budget(budget);
                    budget_changed_s = stream_s;
                }
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
