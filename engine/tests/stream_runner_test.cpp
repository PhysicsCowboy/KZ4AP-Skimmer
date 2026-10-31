// The streaming entry point: the file source's pacing (PlaybackPacer, on given times), FileIqSource on a real file, and
// StreamRunner, whose events must be exactly what the engine gives when run directly on the same samples.
#include "kz4ap/decoder_name.hpp"
#include "kz4ap/file_iq_source.hpp"
#include "kz4ap/iq_recorder.hpp"
#include "kz4ap/stream_runner.hpp"

#include "test_signals.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

using namespace kz4ap;
using namespace std::chrono_literals;

namespace {

constexpr double kRate = 48000.0;

// A source over samples in memory, in blocks of a fixed size, optionally throwing at the end.
class MemorySource final : public IqSource {
public:
    MemorySource(std::vector<Sample> samples, std::size_t block, bool fail_at_end = false)
        : samples_(std::move(samples)), block_(block), fail_at_end_(fail_at_end) {}

    int sample_rate() const override { return static_cast<int>(kRate); }
    std::string description() const override { return "memory"; }
    IqRead read(std::span<Sample> out, std::chrono::milliseconds) override {
        if (next_ == samples_.size() && fail_at_end_) throw std::runtime_error("the radio went away");
        const std::size_t n = std::min({out.size(), block_, samples_.size() - next_});
        std::copy_n(samples_.begin() + static_cast<std::ptrdiff_t>(next_), n, out.begin());
        next_ += n;
        return IqRead{n, next_ == samples_.size() && !fail_at_end_};
    }
    void interrupt() override {}

private:
    std::vector<Sample> samples_;
    std::size_t block_;
    bool fail_at_end_;
    std::size_t next_ = 0;
};

// A live source behind by a time that changes by a fixed step with each read (0.1 s of stream).
class BehindSource final : public IqSource {
public:
    BehindSource(std::vector<Sample> samples, double backlog_s, double step_s = 0.0)
        : inner_(std::move(samples), 4800), backlog_s_(backlog_s), step_s_(step_s) {}
    int sample_rate() const override { return inner_.sample_rate(); }
    std::string description() const override { return "behind"; }
    IqRead read(std::span<Sample> out, std::chrono::milliseconds timeout) override {
        backlog_s_ = std::max(0.0, backlog_s_ + step_s_);
        return inner_.read(out, timeout);
    }
    void interrupt() override {}
    double backlog_s() const override { return backlog_s_; }

private:
    MemorySource inner_;
    double backlog_s_;
    double step_s_;
};

// A source that never has samples, as a live one between blocks: read waits until interrupted.
class SilentSource final : public IqSource {
public:
    int sample_rate() const override { return static_cast<int>(kRate); }
    std::string description() const override { return "silent"; }
    IqRead read(std::span<Sample>, std::chrono::milliseconds timeout) override {
        std::this_thread::sleep_for(std::min(timeout, 5ms));
        return {};
    }
    void interrupt() override {}
};

std::vector<Sample> test_band() {
    return test::keyed_signal("CQ TEST DE W1AW W1AW K", 25, kRate, 12.0, 6000.0, 0.02, 0.02, 11);
}

std::map<std::uint32_t, std::string> direct_text(const std::vector<Sample>& samples, std::string_view decoder) {
    EventBus bus;
    std::map<std::uint32_t, std::string> text;
    bus.subscribe([&](const Event& e) {
        if (const auto* d = std::get_if<DecodedTextEvent>(&e)) {
            for (const auto& c : d->chars) text[d->track_id] += c.text;
        }
    });
    EngineConfig config = engine_config_for_decoder(decoder);
    config.sample_rate = static_cast<int>(kRate);
    Engine engine(config, bus);
    engine.process(samples);
    engine.finish();
    return text;
}

// Waits for the stream to reach a final state; fails the test past the limit.
StreamStatus wait_until_over(const StreamRunner& runner) {
    const auto limit = std::chrono::steady_clock::now() + 120s;
    for (;;) {
        const StreamStatus s = runner.status();
        if (s.state == StreamStatus::State::Finished || s.state == StreamStatus::State::Failed) return s;
        if (std::chrono::steady_clock::now() > limit) {
            ADD_FAILURE() << "the stream did not end in time";
            return s;
        }
        std::this_thread::sleep_for(5ms);
    }
}

std::filesystem::path write_wav(const std::string& name, std::size_t samples, int rate = 48000) {
    const auto path = std::filesystem::temp_directory_path() / name;
    std::ofstream f(path, std::ios::binary);
    const auto u32 = [&](std::uint32_t v) { f.write(reinterpret_cast<const char*>(&v), 4); };
    const auto u16 = [&](std::uint16_t v) { f.write(reinterpret_cast<const char*>(&v), 2); };
    const auto data_bytes = static_cast<std::uint32_t>(samples * 4);
    f.write("RIFF", 4);
    u32(36 + data_bytes);
    f.write("WAVEfmt ", 8);
    u32(16);
    u16(1);
    u16(2);
    u32(static_cast<std::uint32_t>(rate));
    u32(static_cast<std::uint32_t>(rate) * 4);
    u16(4);
    u16(16);
    f.write("data", 4);
    u32(data_bytes);
    for (std::size_t i = 0; i < samples; ++i) {
        u16(static_cast<std::uint16_t>(static_cast<std::int16_t>(i % 1000)));
        u16(0);
    }
    return path;
}

}  // namespace

TEST(PlaybackPacer, DeliversNothingBeforeItStarts) {
    PlaybackPacer pacer(1000.0);
    EXPECT_EQ(pacer.available(100.0), 0u);
    EXPECT_TRUE(std::isinf(pacer.seconds_until(1, 100.0)));
}

TEST(PlaybackPacer, PacesAtTheSpeed) {
    PlaybackPacer pacer(1000.0);
    pacer.start(10.0);
    EXPECT_EQ(pacer.available(10.0), 0u);
    EXPECT_NEAR(static_cast<double>(pacer.available(10.1)), 100.0, 1.0);
    pacer.delivered(100);
    EXPECT_EQ(pacer.available(10.1), 0u);
    EXPECT_NEAR(pacer.seconds_until(50, 10.1), 0.05, 1e-6);
    pacer.set_speed(10.0, 10.1);
    EXPECT_NEAR(static_cast<double>(pacer.available(10.2)), 1000.0, 1.0);
}

TEST(PlaybackPacer, APauseHoldsThePositionAndForgivesNothingElse) {
    PlaybackPacer pacer(1000.0);
    pacer.start(0.0);
    pacer.delivered(100);
    pacer.set_paused(true, 0.2);  // 100 were due and undelivered: a pause stops the stream where it is
    EXPECT_EQ(pacer.available(50.0), 0u);
    EXPECT_TRUE(std::isinf(pacer.seconds_until(1, 50.0)));
    pacer.set_paused(false, 50.0);
    EXPECT_EQ(pacer.available(50.0), 0u);
    EXPECT_NEAR(static_cast<double>(pacer.available(50.1)), 100.0, 1.0);
}

TEST(PlaybackPacer, ForgivesADebtBeyondTheLagLimit) {
    PlaybackPacer pacer(1000.0);
    pacer.start(0.0);
    const auto cap = static_cast<std::uint64_t>(PlaybackPacer::kMaxLagWallS * 1000.0);
    EXPECT_EQ(pacer.available(100.0), cap);  // 100 s late: only the limit is owed
    pacer.delivered(cap);
    EXPECT_EQ(pacer.available(100.0), 0u);
    EXPECT_NEAR(static_cast<double>(pacer.available(100.1)), 100.0, 1.0);
}

TEST(PlaybackPacer, AsFastAsPossibleHasNoLimitAndLeavesItCleanly) {
    PlaybackPacer pacer(1000.0);
    pacer.start(0.0);
    pacer.set_speed(kAsFastAsPossible, 0.0);
    EXPECT_GT(pacer.available(0.0), 1000000000u);
    pacer.delivered(5000);
    pacer.set_speed(1.0, 1.0);
    EXPECT_EQ(pacer.available(1.0), 0u);
    EXPECT_NEAR(static_cast<double>(pacer.available(1.1)), 100.0, 1.0);
}

TEST(PlaybackPacer, RefusesASpeedThatIsNotPositive) {
    PlaybackPacer pacer(1000.0);
    EXPECT_THROW(pacer.set_speed(0.0, 0.0), std::invalid_argument);
    EXPECT_THROW(pacer.set_speed(-1.0, 0.0), std::invalid_argument);
}

TEST(FileIqSource, ReadsAWholeFileAsFastAsPossible) {
    const auto path = write_wav("kz4ap_file_iq_source_all.wav", 5000);
    {
        FileIqSource source(path);
        EXPECT_EQ(source.sample_rate(), 48000);
        EXPECT_EQ(source.total_samples(), 5000u);
        EXPECT_EQ(source.description(), "kz4ap_file_iq_source_all.wav");
        ASSERT_NE(source.playback(), nullptr);
        source.set_speed(kAsFastAsPossible);
        std::vector<Sample> out(1024);
        std::size_t total = 0;
        IqRead r;
        do {
            r = source.read(out, 100ms);
            total += r.count;
        } while (!r.end_of_stream);
        EXPECT_EQ(total, 5000u);
        EXPECT_TRUE(source.read(out, 0ms).end_of_stream);
    }
    std::filesystem::remove(path);
}

TEST(FileIqSource, APausedReadTimesOutAndAnInterruptEndsTheWait) {
    const auto path = write_wav("kz4ap_file_iq_source_paused.wav", 5000);
    {
        FileIqSource source(path);
        source.set_paused(true);
        EXPECT_TRUE(source.paused());
        std::vector<Sample> out(1024);
        const IqRead r = source.read(out, 20ms);
        EXPECT_EQ(r.count, 0u);
        EXPECT_FALSE(r.end_of_stream);

        std::thread stopper([&] {
            std::this_thread::sleep_for(20ms);
            source.interrupt();
        });
        const auto started = std::chrono::steady_clock::now();
        const IqRead woken = source.read(out, 30s);
        stopper.join();
        EXPECT_EQ(woken.count, 0u);
        EXPECT_LT(std::chrono::steady_clock::now() - started, 10s);
    }
    std::filesystem::remove(path);
}

TEST(FileIqSource, RealTimeTakesRealTime) {
    // 0.2 s of stream at 1x: at least the stream's length, within a wide margin (this is a test machine, not a clock).
    const auto path = write_wav("kz4ap_file_iq_source_paced.wav", 9600);
    {
        FileIqSource source(path);
        std::vector<Sample> out(4096);
        const auto started = std::chrono::steady_clock::now();
        IqRead r;
        do {
            r = source.read(out, 100ms);
        } while (!r.end_of_stream);
        const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
        EXPECT_GE(elapsed, 0.19);
        EXPECT_LT(elapsed, 5.0);
    }
    std::filesystem::remove(path);
}

TEST(StreamRunner, GivesTheEventsOfADirectRunWhateverTheBlocks) {
    const std::vector<Sample> samples = test_band();
    const auto expected = direct_text(samples, kDefaultDecoderName);
    ASSERT_FALSE(expected.empty());

    std::mutex mutex;
    std::map<std::uint32_t, std::string> text;
    auto source = std::make_unique<MemorySource>(samples, 777);
    StreamRunner runner(std::move(source), engine_config_for_decoder(kDefaultDecoderName), [&](const Event& e) {
        if (const auto* d = std::get_if<DecodedTextEvent>(&e)) {
            std::lock_guard lock(mutex);
            for (const auto& c : d->chars) text[d->track_id] += c.text;
        }
    });
    const StreamStatus status = wait_until_over(runner);
    runner.stop();
    EXPECT_EQ(status.state, StreamStatus::State::Finished);
    EXPECT_EQ(text, expected);
    EXPECT_DOUBLE_EQ(status.stream_s, static_cast<double>(samples.size()) / kRate);
    EXPECT_GT(status.channel_seconds, 0.0);
    EXPECT_GT(status.thread_cpu_s, 0.0);
}

TEST(StreamRunner, ASourceThatThrowsEndsTheStreamAsFailed) {
    StreamRunner runner(std::make_unique<MemorySource>(std::vector<Sample>(4096), 512, true),
                        engine_config_for_decoder(kDefaultDecoderName), [](const Event&) {});
    const StreamStatus status = wait_until_over(runner);
    EXPECT_EQ(status.state, StreamStatus::State::Failed);
    EXPECT_EQ(status.error, "the radio went away");
}

TEST(StreamRunner, AnEngineThatRefusesTheConfigurationEndsTheStreamAsFailed) {
    EngineConfig config = engine_config_for_decoder(kDefaultDecoderName);
    config.channel_distance_hz = -1.0;
    StreamRunner runner(std::make_unique<SilentSource>(), config, [](const Event&) {});
    const StreamStatus status = wait_until_over(runner);
    EXPECT_EQ(status.state, StreamStatus::State::Failed);
    EXPECT_FALSE(status.error.empty());
}

TEST(StreamRunner, StopEndsALiveStream) {
    StreamRunner runner(std::make_unique<SilentSource>(), engine_config_for_decoder(kDefaultDecoderName), [](const Event&) {});
    std::this_thread::sleep_for(20ms);
    runner.stop();
    EXPECT_EQ(runner.status().state, StreamStatus::State::Stopped);
    runner.stop();  // and again, harmlessly
}

TEST(DecoderName, MapsTheNamesAndRefusesOthers) {
    for (const FrontEnd f : {FrontEnd::Envelope, FrontEnd::Matched, FrontEnd::Bank, FrontEnd::Viterbi})
        EXPECT_EQ(front_end_from_name(front_end_name(f)), f);
    EXPECT_FALSE(front_end_from_name("neural"));
    EXPECT_THROW(engine_config_for_decoder("neural"), std::invalid_argument);
    EXPECT_EQ(engine_config_for_decoder("bank").decoder.front_end, FrontEnd::Bank);
    EXPECT_EQ(engine_config_for_decoder("envelope").detector.attribution, Attribution::Bins);
}

namespace {

// Stations on separate frequencies, a keyed one every 400 Hz.
std::vector<Sample> many_stations(int count, double duration_s) {
    std::vector<Sample> x(static_cast<std::size_t>(duration_s * kRate));
    for (int i = 0; i < count; ++i) {
        const std::vector<Sample> one = test::keyed_signal("CQ CQ DE KQ2RP KQ2RP K CQ CQ DE KQ2RP KQ2RP K", 20, kRate, duration_s, -8000.0 + 400.0 * i, 0.01);
        for (std::size_t k = 0; k < x.size(); ++k) x[k] += one[k];
    }
    return x;
}

StreamStatus run_to_end(std::unique_ptr<IqSource> source) {
    EngineConfig config;
    config.decoder.front_end = FrontEnd::Segmental;
    std::size_t most = 0, live = 0;
    StreamRunner runner(std::move(source), config, [&](const Event& e) {
        if (const auto* t = std::get_if<TrackEvent>(&e)) {
            live += t->kind == TrackEvent::Kind::Born ? 1 : static_cast<std::size_t>(-1);
            most = std::max(most, live);
        }
    });
    while (runner.status().state != StreamStatus::State::Finished && runner.status().state != StreamStatus::State::Failed)
        std::this_thread::sleep_for(5ms);
    return runner.status();
}

}  // namespace

TEST(StreamRunnerBudget, ABacklogThatDoesNotGrowCostsNoTracks) {
    // Left by a stall or by the engine's own construction: it stands, or drains, and goes by itself.
    const StreamStatus standing = run_to_end(std::make_unique<BehindSource>(many_stations(24, 8.0), 2.0));
    ASSERT_EQ(standing.state, StreamStatus::State::Finished) << standing.error;
    EXPECT_EQ(standing.track_budget, 0u);
    const StreamStatus draining = run_to_end(std::make_unique<BehindSource>(many_stations(24, 8.0), 4.0, -0.05));
    ASSERT_EQ(draining.state, StreamStatus::State::Finished) << draining.error;
    EXPECT_EQ(draining.track_budget, 0u);
}

TEST(StreamRunnerBudget, WithNoTracksLiveTheBudgetStaysOff) {
    const StreamStatus few = run_to_end(std::make_unique<BehindSource>(many_stations(0, 8.0), 0.0, 0.1));
    ASSERT_EQ(few.state, StreamStatus::State::Finished) << few.error;
    EXPECT_EQ(few.track_budget, 0u);
}

TEST(StreamRunnerBudget, APipelineBehindALiveSourceGivesUpTracks) {
    const StreamStatus behind = run_to_end(std::make_unique<BehindSource>(many_stations(24, 8.0), 0.0, 0.1));
    ASSERT_EQ(behind.state, StreamStatus::State::Finished) << behind.error;
    EXPECT_GT(behind.track_budget, 0u);
    EXPECT_LE(behind.track_budget, 24u);
    const StreamStatus caught_up = run_to_end(std::make_unique<BehindSource>(many_stations(24, 8.0), 0.0));
    ASSERT_EQ(caught_up.state, StreamStatus::State::Finished) << caught_up.error;
    EXPECT_EQ(caught_up.track_budget, 0u);
    EXPECT_LT(behind.channel_seconds, caught_up.channel_seconds);
}

TEST(FileIqSource, OpensAFileNamedOutsideTheSystemsCodePage) {
    const std::filesystem::path path = std::filesystem::temp_directory_path() / std::filesystem::path(u8"kz4ap_\u03a9\u4e2d_test.wav");
    {
        IqRecorder recorder(48000);
        recorder.start(path);
        const std::vector<short> i(4800, 8000), q(4800, -8000);
        recorder.push(i.data(), q.data(), i.size());
        recorder.stop();
    }
    {
        FileIqSource source(path);
        EXPECT_EQ(source.sample_rate(), 48000);
        EXPECT_NE(source.description().find("kz4ap_"), std::string::npos);
        EXPECT_NE(source.description().find(reinterpret_cast<const char*>(u8"\u03a9")), std::string::npos);
    }
    std::filesystem::remove(path);
}
