#include "kz4ap/signal_detector.hpp"

#include <gtest/gtest.h>

#include <random>
#include <stdexcept>
#include <utility>
#include <vector>

using namespace kz4ap;

namespace {

constexpr int kN = 256;
constexpr int kRate = 25600;  // 100 Hz bins
constexpr int kHop = 128;     // 5 ms frames

DetectorConfig config() {
    DetectorConfig c;
    c.sample_rate = kRate;
    c.fft_size = kN;
    c.hop = kHop;
    c.average_s = 0.05;
    c.birth_s = 0.5;
    c.death_s = 2.0;
    return c;
}

double frame_time(int i) { return (i + 1) * double(kHop) / kRate; }

SpectrumFrame frame(double t, const std::vector<std::pair<int, float>>& peaks = {}) {
    SpectrumFrame f;
    f.time_s = t;
    f.power_db.assign(kN, -100.0f);
    for (auto [bin, db] : peaks) f.power_db[bin] = db;
    return f;
}

}  // namespace

TEST(SignalDetector, TrackIsBornOnlyAfterSignalPersists) {
    SignalDetector d(config());
    std::vector<Track> born;
    double born_at = -1;
    for (int i = 0; i < 200; ++i) {
        const auto u = d.process(frame(frame_time(i), {{200, -70.0f}}));
        if (!u.born.empty() && born_at < 0) born_at = frame_time(i);
        born.insert(born.end(), u.born.begin(), u.born.end());
    }
    ASSERT_EQ(born.size(), 1u);
    EXPECT_GE(born_at, 0.55);  // 0.05 s of averaging warm-up, then 0.5 s of persistence
    EXPECT_LT(born_at, 0.58);
    EXPECT_EQ(born[0].id, 1u);
    EXPECT_NEAR(born[0].freq_hz, (200 - kN / 2) * 100.0, 1e-6);
    EXPECT_NEAR(born[0].snr_db, 30.0f, 0.5f);
    EXPECT_EQ(d.tracks().size(), 1u);
}

TEST(SignalDetector, NoiseAloneMakesNoTracks) {
    SignalDetector d(config());
    std::mt19937 rng(1);
    std::uniform_real_distribution<float> jitter(-2.0f, 2.0f);
    for (int i = 0; i < 400; ++i) {
        auto f = frame(frame_time(i));
        for (auto& p : f.power_db) p += jitter(rng);
        EXPECT_TRUE(d.process(f).born.empty()) << "frame " << i;
    }
}

TEST(SignalDetector, TrackDiesAfterSilence) {
    SignalDetector d(config());
    int i = 0;
    for (; i < 200; ++i) d.process(frame(frame_time(i), {{200, -70.0f}}));
    ASSERT_EQ(d.tracks().size(), 1u);
    double died_at = -1;
    for (; i < 1000 && died_at < 0; ++i) {
        if (!d.process(frame(frame_time(i))).died.empty()) died_at = frame_time(i);
    }
    EXPECT_GT(died_at, 1.0 + 2.0);
    EXPECT_LT(died_at, 1.0 + 2.0 + 0.5);
    EXPECT_TRUE(d.tracks().empty());
}

TEST(SignalDetector, WideSignalMakesOneTrack) {
    SignalDetector d(config());
    std::size_t born = 0;
    for (int i = 0; i < 200; ++i)
        born += d.process(frame(frame_time(i), {{199, -72.0f}, {200, -70.0f}, {201, -72.0f}})).born.size();
    EXPECT_EQ(born, 1u);
}

TEST(SignalDetector, TwoSignalsMakeTwoTracks) {
    SignalDetector d(config());
    std::size_t born = 0;
    for (int i = 0; i < 200; ++i)
        born += d.process(frame(frame_time(i), {{100, -70.0f}, {150, -75.0f}})).born.size();
    EXPECT_EQ(born, 2u);
}

TEST(SignalDetector, CapKeepsStrongestSignals) {
    auto c = config();
    c.max_tracks = 2;
    SignalDetector d(c);
    std::vector<Track> born;
    for (int i = 0; i < 400; ++i) {
        const auto u = d.process(frame(frame_time(i), {{50, -70.0f}, {100, -80.0f}, {150, -90.0f}}));
        born.insert(born.end(), u.born.begin(), u.born.end());
    }
    ASSERT_EQ(born.size(), 2u);
    EXPECT_NEAR(born[0].freq_hz, (50 - kN / 2) * 100.0, 1e-6);
    EXPECT_NEAR(born[1].freq_hz, (100 - kN / 2) * 100.0, 1e-6);
}

TEST(SignalDetector, StrongerNewSignalReplacesWeakestAtCap) {
    auto c = config();
    c.max_tracks = 1;
    SignalDetector d(c);
    int i = 0;
    for (; i < 200; ++i) d.process(frame(frame_time(i), {{50, -90.0f}}));
    ASSERT_EQ(d.tracks().size(), 1u);
    const auto weak_id = d.tracks()[0].id;
    std::vector<std::uint32_t> died;
    std::vector<Track> born;
    for (; i < 400; ++i) {
        const auto u = d.process(frame(frame_time(i), {{50, -90.0f}, {150, -70.0f}}));
        died.insert(died.end(), u.died.begin(), u.died.end());
        born.insert(born.end(), u.born.begin(), u.born.end());
    }
    ASSERT_EQ(died.size(), 1u);
    EXPECT_EQ(died[0], weak_id);
    ASSERT_EQ(born.size(), 1u);
    EXPECT_NEAR(born[0].freq_hz, (150 - kN / 2) * 100.0, 1e-6);
}

TEST(SignalDetector, RejectsFrameOfWrongSize) {
    SignalDetector d(config());
    SpectrumFrame f;
    f.power_db.assign(100, -100.0f);
    EXPECT_THROW(d.process(f), std::invalid_argument);
}

TEST(SignalDetector, RejectsInvalidConfig) {
    auto bad = [](auto mutate) {
        DetectorConfig c = config();
        mutate(c);
        EXPECT_THROW(SignalDetector d(c), std::invalid_argument);
    };
    bad([](DetectorConfig& c) { c.sample_rate = 0; });
    bad([](DetectorConfig& c) { c.sample_rate = -1; });
    bad([](DetectorConfig& c) { c.fft_size = 0; });
    bad([](DetectorConfig& c) { c.fft_size = -1; });
    bad([](DetectorConfig& c) { c.hop = 0; });
    bad([](DetectorConfig& c) { c.hop = -1; });
    bad([](DetectorConfig& c) { c.average_s = 0; });
    bad([](DetectorConfig& c) { c.average_s = -1; });
    bad([](DetectorConfig& c) { c.birth_s = -1; });
    bad([](DetectorConfig& c) { c.death_s = 0; });
    bad([](DetectorConfig& c) { c.death_s = -1; });
    bad([](DetectorConfig& c) { c.max_tracks = 0; });
    bad([](DetectorConfig& c) { c.min_separation_bins = 0; });
    bad([](DetectorConfig& c) { c.min_separation_bins = -1; });
}
