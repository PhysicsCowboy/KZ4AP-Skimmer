#include "kz4ap/engine.hpp"

#include "test_signals.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

using namespace kz4ap;
using kz4ap::test::keyed_signal;

namespace {

constexpr double kRate = 192000;
constexpr double kNoiseSigma = 0.02;
constexpr double kAmplitude20dB = 0.0102;  // 20 dB SNR in 500 Hz against kNoiseSigma

struct Result {
    std::vector<Track> born;
    std::map<std::uint32_t, std::string> text;
};

Result run(const std::vector<Sample>& x, std::size_t chunk, int sample_rate = static_cast<int>(kRate)) {
    EventBus bus;
    Result r;
    bus.subscribe([&](const Event& e) {
        if (const auto* t = std::get_if<TrackEvent>(&e); t && t->kind == TrackEvent::Kind::Born) r.born.push_back(t->track);
        if (const auto* d = std::get_if<DecodedTextEvent>(&e)) {
            for (const auto& c : d->chars) r.text[d->track_id] += c.text;
        }
    });
    EngineConfig config;
    config.sample_rate = sample_rate;
    Engine engine(config, bus);
    for (std::size_t i = 0; i < x.size(); i += chunk) {
        engine.process(std::span<const Sample>(x).subspan(i, std::min(chunk, x.size() - i)));
    }
    engine.finish();
    return r;
}

std::vector<Sample> band(double duration_s) {
    auto x = keyed_signal("VVV CQ K1ABC", 25, kRate, duration_s, 12000.0, kAmplitude20dB, kNoiseSigma, 11);
    const auto y = keyed_signal("VVV CQ W9XYZ", 22, kRate, duration_s, -30000.0, kAmplitude20dB, 0.0, 12);
    for (std::size_t i = 0; i < x.size(); ++i) x[i] += y[i];
    return x;
}

}  // namespace

TEST(Engine, FindsAndDecodesTwoSignals) {
    const auto r = run(band(9.0), 65536);
    ASSERT_EQ(r.born.size(), 2u);
    std::map<double, std::string> by_freq;
    for (const auto& t : r.born) by_freq[t.freq_hz] = r.text.count(t.id) ? r.text.at(t.id) : "";
    const auto low = by_freq.begin();
    const auto high = std::next(low);
    EXPECT_NEAR(low->first, -30000.0, 25.0);
    EXPECT_NEAR(high->first, 12000.0, 25.0);
    EXPECT_NE(low->second.find("CQ W9XYZ"), std::string::npos) << low->second;
    EXPECT_NE(high->second.find("CQ K1ABC"), std::string::npos) << high->second;
}

TEST(Engine, ChoosesFftSizeFromSampleRate) {
    // Largest power of two keeping bins at least 20 Hz wide (23.4 Hz at each of these rates).
    EXPECT_EQ(choose_fft_size(48000), 2048);
    EXPECT_EQ(choose_fft_size(96000), 4096);
    EXPECT_EQ(choose_fft_size(192000), 8192);
    EXPECT_EQ(choose_fft_size(768000), 32768);
}

TEST(Engine, RejectsUnusableSampleRates) {
    EXPECT_THROW(choose_fft_size(0), std::invalid_argument);
    EXPECT_THROW(choose_fft_size(7999), std::invalid_argument);
    EventBus bus;
    EngineConfig config;
    config.sample_rate = 4000;
    EXPECT_THROW(Engine(config, bus), std::invalid_argument);
}

TEST(Engine, FindsAndDecodesTwoSignalsAt48kHz) {
    // Same 20 dB SNR in 500 Hz as at 192 kHz: a quarter of the bandwidth needs half the noise sigma.
    constexpr double rate = 48000;
    constexpr double sigma = kNoiseSigma / 2;
    auto x = keyed_signal("VVV CQ K1ABC", 25, rate, 9.0, 8000.0, kAmplitude20dB, sigma, 21);
    const auto y = keyed_signal("VVV CQ W9XYZ", 22, rate, 9.0, -12000.0, kAmplitude20dB, 0.0, 22);
    for (std::size_t i = 0; i < x.size(); ++i) x[i] += y[i];

    const auto r = run(x, 16384, static_cast<int>(rate));
    ASSERT_EQ(r.born.size(), 2u);
    std::map<double, std::string> by_freq;
    for (const auto& t : r.born) by_freq[t.freq_hz] = r.text.count(t.id) ? r.text.at(t.id) : "";
    const auto low = by_freq.begin();
    const auto high = std::next(low);
    EXPECT_NEAR(low->first, -12000.0, 25.0);
    EXPECT_NEAR(high->first, 8000.0, 25.0);
    EXPECT_NE(low->second.find("CQ W9XYZ"), std::string::npos) << low->second;
    EXPECT_NE(high->second.find("CQ K1ABC"), std::string::npos) << high->second;
}

TEST(Engine, ChunkingDoesNotChangeResults) {
    const auto x = band(9.0);
    const auto a = run(x, 1000);
    const auto b = run(x, 77777);
    ASSERT_EQ(a.born.size(), b.born.size());
    for (std::size_t i = 0; i < a.born.size(); ++i) EXPECT_EQ(a.born[i].freq_hz, b.born[i].freq_hz);
    EXPECT_EQ(a.text, b.text);
}

TEST(Engine, EventsFollowTrackLifecycle) {
    // One signal, then 12 s of noise only, so the track is born, decodes, and dies before finish().
    // Death is slow: the detector's 1 s power average takes about 7 s to fall from ~27 dB to the
    // 3 dB hold level, then death_s runs out (measured: died 7.8 s after the last mark).
    const std::string sent = "VVV CQ K1ABC";
    const auto x = keyed_signal(sent, 25, kRate, kz4ap::test::duration_for(sent, 25, 12.0), 12000.0,
                                kAmplitude20dB, kNoiseSigma, 14);

    struct Logged {
        enum class Kind { Born, Died, Text } kind;
        std::uint32_t id;
        std::vector<double> starts;
        std::string text;
    };
    std::vector<Logged> log;
    EventBus bus;
    bus.subscribe([&](const Event& e) {
        if (const auto* t = std::get_if<TrackEvent>(&e)) {
            log.push_back({t->kind == TrackEvent::Kind::Born ? Logged::Kind::Born : Logged::Kind::Died,
                           t->track.id, {}, {}});
        }
        if (const auto* d = std::get_if<DecodedTextEvent>(&e)) {
            Logged l{Logged::Kind::Text, d->track_id, {}, {}};
            for (const auto& c : d->chars) {
                l.starts.push_back(c.start_s);
                l.text += c.text;
            }
            log.push_back(std::move(l));
        }
    });
    EngineConfig config;
    config.detector.death_s = 1.0;
    Engine engine(config, bus);
    for (std::size_t i = 0; i < x.size(); i += 65536) {
        engine.process(std::span<const Sample>(x).subspan(i, std::min<std::size_t>(65536, x.size() - i)));
    }
    engine.finish();

    std::map<std::uint32_t, std::size_t> born_at, died_at, borns, dieds;
    for (std::size_t i = 0; i < log.size(); ++i) {
        if (log[i].kind == Logged::Kind::Born) {
            born_at[log[i].id] = i;
            ++borns[log[i].id];
        }
        if (log[i].kind == Logged::Kind::Died) {
            died_at[log[i].id] = i;
            ++dieds[log[i].id];
        }
    }
    ASSERT_EQ(borns.size(), 1u);
    const std::uint32_t id = borns.begin()->first;
    EXPECT_EQ(borns[id], 1u);
    ASSERT_EQ(dieds.size(), 1u);
    EXPECT_EQ(dieds.count(id), 1u);
    EXPECT_EQ(dieds[id], 1u);

    std::string text;
    double last_start = -1.0;
    for (std::size_t i = 0; i < log.size(); ++i) {
        if (log[i].kind != Logged::Kind::Text) continue;
        ASSERT_EQ(born_at.count(log[i].id), 1u) << "text for a track that was never born: " << log[i].id;
        EXPECT_LT(born_at[log[i].id], i);
        EXPECT_GT(died_at[log[i].id], i);
        for (const double s : log[i].starts) {
            EXPECT_GT(s, last_start) << "symbol delivered twice or out of order";
            last_start = s;
        }
        text += log[i].text;
    }
    EXPECT_NE(text.find("CQ K1ABC"), std::string::npos) << text;
}

TEST(Engine, NoiseAloneMakesNoTracks) {
    const auto r = run(keyed_signal("", 25, kRate, 4.0, 0, 0.0, kNoiseSigma, 13), 65536);
    EXPECT_TRUE(r.born.empty());
}

TEST(Engine, OracleOpensChannelsAtTheGivenFrequencies) {
    EventBus bus;
    std::vector<Track> born;
    std::map<std::uint32_t, std::string> text;
    bus.subscribe([&](const Event& e) {
        if (const auto* t = std::get_if<TrackEvent>(&e); t && t->kind == TrackEvent::Kind::Born) born.push_back(t->track);
        if (const auto* d = std::get_if<DecodedTextEvent>(&e)) {
            for (const auto& c : d->chars) text[d->track_id] += c.text;
        }
    });
    EngineConfig config;
    config.oracle_frequencies_hz = {12003.0, -30000.0};
    Engine engine(config, bus);
    ASSERT_EQ(born.size(), 2u);  // published by the constructor
    EXPECT_EQ(born[0].id, 1u);
    EXPECT_DOUBLE_EQ(born[0].freq_hz, 512 * 23.4375);  // 12003 Hz rounded to its bin
    EXPECT_EQ(born[1].id, 2u);
    EXPECT_DOUBLE_EQ(born[1].freq_hz, -30000.0);
    const auto x = band(9.0);
    engine.process(x);
    engine.finish();
    EXPECT_EQ(born.size(), 2u);  // the detector made no tracks of its own
    EXPECT_NE(text[1].find("CQ K1ABC"), std::string::npos) << text[1];
    EXPECT_NE(text[2].find("CQ W9XYZ"), std::string::npos) << text[2];
    EXPECT_NEAR(engine.stats().channel_seconds, 2 * 9.0, 0.05);
}

TEST(Engine, StatsCountChannelTimeOfDetectedTracks) {
    EventBus bus;
    EngineConfig config;
    Engine engine(config, bus);
    engine.process(band(9.0));
    engine.finish();
    // Two stations, each tracked from about 1.5 s to the end.
    EXPECT_GT(engine.stats().channel_seconds, 2 * 6.0);
    EXPECT_LT(engine.stats().channel_seconds, 2 * 9.0);
    EXPECT_GE(engine.stats().decoder_seconds, 0.0);
}
