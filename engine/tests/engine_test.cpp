#include "kz4ap/engine.hpp"

#include "test_signals.hpp"

#include <gtest/gtest.h>

#include <map>
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

Result run(const std::vector<Sample>& x, std::size_t chunk) {
    EventBus bus;
    Result r;
    bus.subscribe([&](const Event& e) {
        if (const auto* t = std::get_if<TrackEvent>(&e); t && t->kind == TrackEvent::Kind::Born) r.born.push_back(t->track);
        if (const auto* d = std::get_if<DecodedTextEvent>(&e)) {
            for (const auto& c : d->chars) r.text[d->track_id] += c.text;
        }
    });
    Engine engine(EngineConfig{}, bus);
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

TEST(Engine, ChunkingDoesNotChangeResults) {
    const auto x = band(9.0);
    const auto a = run(x, 1000);
    const auto b = run(x, 77777);
    ASSERT_EQ(a.born.size(), b.born.size());
    for (std::size_t i = 0; i < a.born.size(); ++i) EXPECT_EQ(a.born[i].freq_hz, b.born[i].freq_hz);
    EXPECT_EQ(a.text, b.text);
}

TEST(Engine, NoiseAloneMakesNoTracks) {
    const auto r = run(keyed_signal("", 25, kRate, 4.0, 0, 0.0, kNoiseSigma, 13), 65536);
    EXPECT_TRUE(r.born.empty());
}
