// The channels' worker pool, and what the engine promises with it: the same events in the same order, whatever the threads.
// Also the segmental decoder as the app uses it: text while the channel runs, with the story's speed.
#include "worker_pool.hpp"

#include "kz4ap/decoder_name.hpp"
#include "kz4ap/engine.hpp"

#include "test_signals.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <cmath>
#include <cstddef>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <variant>
#include <vector>

using namespace kz4ap;

namespace {

constexpr double kRate = 48000.0;

TEST(WorkerPool, RunsEveryJobOnce) {
    for (const int threads : {1, 2, 4}) {
        WorkerPool pool(threads);
        EXPECT_EQ(pool.threads(), threads);
        for (const std::size_t count : {std::size_t{0}, std::size_t{1}, std::size_t{3}, std::size_t{1000}}) {
            for (int batch = 0; batch < 20; ++batch) {
                std::vector<std::atomic<int>> ran(count);
                pool.run(count, [&](std::size_t i) { ++ran[i]; });
                for (std::size_t i = 0; i < count; ++i) ASSERT_EQ(ran[i].load(), 1) << "job " << i;
            }
        }
    }
}

TEST(WorkerPool, RefusesNoThreads) { EXPECT_THROW(WorkerPool(0), std::invalid_argument); }

TEST(WorkerPool, RethrowsTheLowestFailedJobsErrorAfterAllHaveRun) {
    WorkerPool pool(4);
    std::atomic<int> ran{0};
    try {
        pool.run(100, [&](std::size_t i) {
            ++ran;
            if (i == 7 || i == 60) throw std::runtime_error(std::to_string(i));
        });
        FAIL() << "nothing was rethrown";
    } catch (const std::runtime_error& e) {
        EXPECT_STREQ(e.what(), "7");
    }
    EXPECT_EQ(ran.load(), 100);
    // The pool goes on working afterward.
    ran = 0;
    pool.run(5, [&](std::size_t) { ++ran; });
    EXPECT_EQ(ran.load(), 5);
}

TEST(WorkerPool, CountsItsThreadsCpu) {
    WorkerPool pool(3);
    std::atomic<double> sink{0.0};
    pool.run(300, [&](std::size_t i) {
        double x = static_cast<double>(i);
        for (int k = 0; k < 200000; ++k) x = std::sqrt(x + 1.0);
        sink = x;
    });
    pool.run(3, [](std::size_t) {});
    EXPECT_GE(pool.worker_cpu_seconds(), 0.0);
}

// Everything the bus delivers but the spectrum frames and their times, as text, in order.
std::vector<std::string> event_log(const std::vector<Sample>& samples, std::string_view decoder, int threads) {
    EventBus bus;
    std::vector<std::string> log;
    bus.subscribe([&](const Event& e) {
        if (const auto* t = std::get_if<TrackEvent>(&e)) {
            log.push_back((t->kind == TrackEvent::Kind::Born ? "born " : "died ") + std::to_string(t->track.id) + " " +
                          std::to_string(t->track.freq_hz));
        } else if (const auto* d = std::get_if<DecodedTextEvent>(&e)) {
            std::string line = "text " + std::to_string(d->track_id) + " " + std::to_string(d->freq_hz) + " " +
                               std::to_string(d->wpm) + " ";
            for (const auto& c : d->chars) line += c.text;
            for (const auto& k : d->corrections) {
                line += " fix@" + std::to_string(k.from_index) + ":";
                for (const auto& c : k.chars) line += c.text;
            }
            log.push_back(std::move(line));
        }
    });
    EngineConfig config = engine_config_for_decoder(decoder);
    config.sample_rate = static_cast<int>(kRate);
    config.worker_threads = threads;
    Engine engine(config, bus);
    engine.process(samples);
    engine.finish();
    return log;
}

// `stations` keyed carriers, 900 Hz apart, 18 dB S500 or so, at their own speeds.
std::vector<Sample> a_band(int stations, double duration_s) {
    std::vector<Sample> band(static_cast<std::size_t>(duration_s * kRate));
    for (int s = 0; s < stations; ++s) {
        const double wpm = 18.0 + 3.0 * s;
        const std::vector<Sample> one = test::keyed_signal("CQ TEST DE W1AW W1AW K CQ TEST DE W1AW W1AW K", wpm, kRate, duration_s,
                                                           -5000.0 + 900.0 * s, 0.01, 0.0, 1, 0.3 + 0.1 * s);
        for (std::size_t i = 0; i < band.size(); ++i) band[i] += one[i];
    }
    const std::vector<Sample> noise = test::keyed_signal_of({}, kRate, duration_s, 0.0, 0.0, 0.02, 5);
    for (std::size_t i = 0; i < band.size(); ++i) band[i] += noise[i];
    return band;
}

TEST(EngineWorkers, TheThreadsDoNotChangeTheEvents) {
    const std::vector<Sample> band = a_band(10, 10.0);
    const std::vector<std::string> one = event_log(band, "segmental", 1);
    ASSERT_GE(one.size(), 30u);
    EXPECT_EQ(event_log(band, "segmental", 2), one);
    EXPECT_EQ(event_log(band, "segmental", 4), one);
}

TEST(EngineWorkers, NorForTheBank) {
    const std::vector<Sample> band = a_band(3, 6.0);
    const std::vector<std::string> one = event_log(band, "bank", 1);
    ASSERT_GE(one.size(), 3u);
    EXPECT_EQ(event_log(band, "bank", 4), one);
}

TEST(EngineWorkers, RefusesNoThreads) {
    EventBus bus;
    EngineConfig config = engine_config_for_decoder("segmental");
    config.sample_rate = static_cast<int>(kRate);
    config.worker_threads = 0;
    EXPECT_THROW(Engine(config, bus), std::invalid_argument);
}

// The app's default decoder: what the consumer's rule makes of the updates is the text a single reading at the end gives,
// and on the way it has shown text and the speed.
TEST(SegmentalInTheEngine, PublishesTextAndSpeedWhileTheChannelRuns) {
    const double wpm = 25.0;
    const std::string text = "CQ TEST DE W1AW W1AW K CQ TEST DE W1AW W1AW K";
    const std::vector<Sample> samples = test::keyed_signal(text, wpm, kRate, test::duration_for(text, wpm), 6000.0, 0.02, 0.02, 11);
    EventBus bus;
    std::vector<std::string> list;
    std::size_t text_updates = 0;
    std::vector<float> speeds;
    bool finished = false;
    std::size_t updates_before_finish = 0;
    bus.subscribe([&](const Event& e) {
        const auto* d = std::get_if<DecodedTextEvent>(&e);
        if (!d) return;
        ++text_updates;
        if (!finished) ++updates_before_finish;
        for (const auto& c : d->chars) list.push_back(c.text);
        for (const auto& k : d->corrections) {
            list.resize(std::min(k.from_index, list.size()));
            for (const auto& c : k.chars) list.push_back(c.text);
        }
        if (d->wpm > 0) speeds.push_back(d->wpm);
    });
    EngineConfig config = engine_config_for_decoder("segmental");
    config.sample_rate = static_cast<int>(kRate);
    Engine engine(config, bus);
    engine.process(samples);
    finished = true;
    engine.finish();
    EXPECT_GE(updates_before_finish, 5u);
    std::string read;
    for (const auto& s : list) read += s;
    // The channel opens about a second in, so the first word or so is gone; the rest reads exactly.
    EXPECT_NE(read.find("W1AW W1AW K CQ TEST DE W1AW W1AW K"), std::string::npos) << read;
    ASSERT_FALSE(speeds.empty());
    // 10% is one grid step (speed_ratio 1.1) of slack on either side.
    EXPECT_NEAR(speeds.back(), wpm, 0.1 * wpm);
}

TEST(DecoderName, KnowsTheSegmentalDecoderAndMakesItTheDefault) {
    EXPECT_EQ(kDefaultDecoderName, "segmental");
    ASSERT_TRUE(front_end_from_name("segmental").has_value());
    EXPECT_EQ(*front_end_from_name("segmental"), FrontEnd::Segmental);
    EXPECT_STREQ(front_end_name(FrontEnd::Segmental), "segmental");
    EXPECT_EQ(engine_config_for_decoder(kDefaultDecoderName).decoder.front_end, FrontEnd::Segmental);
}

}  // namespace
