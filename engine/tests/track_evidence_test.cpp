// Whether a track holds keyed Morse: the text's sequential test, the judge's rules on made-up evidence, the detector's holds and its
// one track to a peak, and the whole on the engine's segmental path (a carrier goes, a station stays through a 3 s fade).
#include "kz4ap/engine.hpp"
#include "kz4ap/iq_ring.hpp"
#include "kz4ap/signal_detector.hpp"
#include "kz4ap/track_evidence.hpp"

#include "test_signals.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <complex>
#include <map>
#include <random>
#include <string>
#include <variant>
#include <vector>

using namespace kz4ap;

namespace {

std::vector<std::string> symbols_of(const std::string& text) {
    std::vector<std::string> out;
    for (const char c : text) out.emplace_back(1, c);
    return out;
}

TextEvidence evidence_of(const std::string& text) {
    const std::vector<std::string> symbols = symbols_of(text);
    return text_evidence(symbols);
}

}  // namespace

TEST(TextEvidence, TinyPiecesReadAsJunkAndNeverAsPlausible) {
    // Real 40 m: speech read as tiny pieces.
    const TextEvidence e = evidence_of("EE I E E T E E E E N T EE E E");
    EXPECT_GE(e.tiny_nats, kTinyJunkNats);
    EXPECT_FALSE(e.plausible());
    EXPECT_FALSE(evidence_of("T T T T T T T T").plausible());
    EXPECT_FALSE(evidence_of("").plausible());
    EXPECT_FALSE(evidence_of("E").plausible());
}

TEST(TextEvidence, RealStationsAreSoonPlausible) {
    for (const char* text : {"KQ2RP", "HFM K", "N3BT U", "CQ DX DE AI5IN", "W3RJ QRZ", "DC WITH REDUCED RF OUTPUT"})
        EXPECT_TRUE(evidence_of(text).plausible()) << text;
}

TEST(TextEvidence, WordSpacesDoNotCount) {
    EXPECT_EQ(evidence_of("E E E").symbols, 3u);
    EXPECT_DOUBLE_EQ(evidence_of("E E E").tiny_nats, evidence_of("EEE").tiny_nats);
}

TEST(TextEvidence, TheSumIsFlooredSoThatAFadeCanStillBeSeen) {
    std::string text(200, 'K');
    EXPECT_DOUBLE_EQ(evidence_of(text).tiny_nats, -kTextFloorNats);
    // From the floor, tiny pieces reach a verdict within 20 or so.
    text += std::string(20, 'E');
    EXPECT_GE(evidence_of(text).tiny_nats, kTinyJunkNats);
}

TEST(TextEvidence, GarbageIsNeverPlausible) {
    const std::vector<std::string> symbols = {"O", "0", "0", "0", "<SOS>", ",", "!", "<CL>", ")", "'", ";", "<BK>", "0", "!", "1"};
    const TextEvidence e = text_evidence(symbols);
    EXPECT_GE(e.highest_garbage_nats, kGarbageJunkNats);
    EXPECT_FALSE(e.plausible());
}

namespace {

// A second of evidence at the given rates, nats/s.
struct Feed {
    TrackJudge judge;
    KeyingEvidence now;
    TrackJudge::Verdict step(double morse, double free_key, const std::string& text = "") {
        now.morse_nats += morse;
        now.free_key_nats += free_key;
        const std::vector<std::string> symbols = symbols_of(text);
        return judge.update(now, symbols);
    }
};

}  // namespace

TEST(TrackJudge, NoiseThatEarnsNothingGoesAfterTheStatedTime) {
    Feed f;
    int seconds = 0;
    TrackJudge::Verdict v;
    while (!(v = f.step(1.0, -4.0)).retire && seconds < 60) ++seconds;
    EXPECT_EQ(seconds, static_cast<int>(TrackJudge::kQuietNewS));
    EXPECT_EQ(v.end, TrackEnd::Quiet);
    EXPECT_GT(v.hold_s, 0.0);
}

TEST(TrackJudge, ACarrierGoesAfterAWindow) {
    Feed f;
    int seconds = 0;
    TrackJudge::Verdict v;
    while (!(v = f.step(7000.0, 7020.0)).retire && seconds < 60) ++seconds;
    EXPECT_EQ(seconds, static_cast<int>(TrackJudge::kWindowS));
    EXPECT_EQ(v.end, TrackEnd::NotMorse);
}

TEST(TrackJudge, AMarginalThatLooksAtTheTextGoesWithoutProof) {
    // Earns well, near what a free key earns, and reads nothing plausible: speech. It may not stay beyond the stated time.
    Feed f;
    int seconds = 0;
    TrackJudge::Verdict v;
    while (!(v = f.step(100.0, 100.0)).retire && seconds < 60) ++seconds;
    EXPECT_EQ(seconds, static_cast<int>(TrackJudge::kUnprovenS));
    EXPECT_EQ(v.end, TrackEnd::NotMorse);
}

TEST(TrackJudge, JunkTextGoesUnlessTheSignalIsKeyedBeyondDoubt) {
    Feed speech;
    int seconds = 0;
    TrackJudge::Verdict v;
    while (!(v = speech.step(100.0, 100.0, "EEEEEEEEEEEEEEEEEEEEEEEE")).retire && seconds < 60) ++seconds;
    EXPECT_EQ(seconds, static_cast<int>(TrackJudge::kFirstVerdictS));
    EXPECT_EQ(v.end, TrackEnd::NotMorse);
    // A generated contest exchange reads E, T, I, M, U; its timing earns far above the keyed rate.
    Feed station;
    for (int s = 0; s < 40; ++s) EXPECT_FALSE(station.step(5000.0, 4900.0, "EIMEE TU EEMEE TU EEMEE TU EEMEE TU EEMEE TU").retire) << s;
}

TEST(TrackJudge, AStationThatFadesFor3SKeepsItsTrack) {
    Feed f;
    for (int s = 0; s < 20; ++s) ASSERT_FALSE(f.step(200.0, 200.0, "CQ CQ DE KQ2RP").retire);
    EXPECT_TRUE(f.judge.proven());
    for (int s = 0; s < 3; ++s) EXPECT_FALSE(f.step(0.0, -5.0, "CQ CQ DE KQ2RP").retire);
    for (int s = 0; s < 20; ++s) EXPECT_FALSE(f.step(200.0, 200.0, "CQ CQ DE KQ2RP").retire);
}

TEST(TrackJudge, AStationGoneLeavesAfterTheLongQuietAndItsPlaceStaysOpen) {
    Feed f;
    for (int s = 0; s < 20; ++s) ASSERT_FALSE(f.step(200.0, 200.0, "CQ CQ DE KQ2RP").retire);
    int seconds = 0;
    TrackJudge::Verdict v;
    while (!(v = f.step(0.0, -5.0, "CQ CQ DE KQ2RP")).retire && seconds < 100) ++seconds;
    EXPECT_NEAR(seconds, TrackJudge::kQuietOldS, 1.0);
    EXPECT_EQ(v.end, TrackEnd::Quiet);
    EXPECT_EQ(v.hold_s, 0.0);
}

TEST(TrackJudge, AWeakStationIsNotHeldToTheRates) {
    // Generated, +6.5 dB S500 at 42 WPM: 230 nats/s, a free key above it by 11 nats/s. Its text keeps it.
    Feed f;
    for (int s = 0; s < 40; ++s) EXPECT_FALSE(f.step(230.0, 235.0, "TEAT KE2YT VE2YT CQ TEST").retire) << s;
}

TEST(TrackJudge, AStrongShownTrackThatEarnsNoTimingGoes) {
    Feed f;
    int seconds = 0;
    TrackJudge::Verdict v;
    while (!(v = f.step(3400.0, 3412.0, "WKQ5 XB9Z MK3D OJ7P 4UQ")).retire && seconds < 60) ++seconds;
    EXPECT_EQ(seconds, static_cast<int>(TrackJudge::kWindowS));
    EXPECT_EQ(v.end, TrackEnd::NotMorse);
}

TEST(TrackJudge, AStrongStationsSlipIsNoShortfall) {
    // Generated: 46 nats/s behind a free key for a moment, out of 277 000 nats/s.
    Feed f;
    for (int s = 0; s < 40; ++s) EXPECT_FALSE(f.step(277000.0, 277040.0, "CQ TEST PY9X PY9X").retire) << s;
}

namespace {

constexpr int kBins = 8192;
constexpr double kBinHz = 192000.0 / kBins;

// A detector's frame: power, FS^2, a floor with peaks at these bins.
SpectrumFrame frame_with(double time_s, const std::vector<std::pair<int, double>>& peaks) {
    SpectrumFrame f;
    f.time_s = time_s;
    f.power_fs2.assign(kBins, 1e-9f);
    for (const auto& [bin, power] : peaks) {
        f.power_fs2[static_cast<std::size_t>(bin)] = static_cast<float>(power);
        // A window's main lobe, so that the peak is a peak and its frequency interpolates.
        f.power_fs2[static_cast<std::size_t>(bin - 1)] = static_cast<float>(power / 4);
        f.power_fs2[static_cast<std::size_t>(bin + 1)] = static_cast<float>(power / 4);
    }
    f.power_db.resize(kBins);
    for (int i = 0; i < kBins; ++i) f.power_db[static_cast<std::size_t>(i)] = 10.0f * std::log10(f.power_fs2[static_cast<std::size_t>(i)]);
    return f;
}

DetectorConfig strict_detector() {
    DetectorConfig c;
    c.sample_rate = 192000;
    c.fft_size = kBins;
    c.hop = kBins / 2;
    c.one_track_per_peak = true;
    return c;
}

constexpr double kFrameS = (kBins / 2) / 192000.0;

// Runs frames with those peaks over a time span; returns how many tracks were born, and those that left because of another.
struct DetectorRun {
    SignalDetector detector;
    double now = 0.0;
    std::vector<std::uint32_t> born;
    std::vector<std::uint32_t> merged;
    explicit DetectorRun(const DetectorConfig& c) : detector(c) {}
    void frames(double seconds, const std::vector<std::pair<int, double>>& peaks) {
        const double end = now + seconds;
        while (now < end) {
            const DetectorUpdate u = detector.process(frame_with(now, peaks));
            for (const Track& t : u.born) born.push_back(t.id);
            merged.insert(merged.end(), u.merged.begin(), u.merged.end());
            now += kFrameS;
        }
    }
};

}  // namespace

TEST(SignalDetectorOneTrackPerPeak, TwoTracksThatComeTogetherBecomeOne) {
    DetectorRun run(strict_detector());
    const int a = kBins / 2 + 1000, b = a + 4;  // 94 Hz apart: two tracks
    run.frames(3.0, {{a, 1e-6}, {b - 1, 1e-6}});
    ASSERT_EQ(run.born.size(), 2u);
    // The older's station moves toward the younger's, whose own peak has gone: the younger holds its frequency, 2 bins from it.
    run.frames(4.0, {{a + 1, 1e-6}});  // the detector's average takes a few of its 1 s time constants to show it
    ASSERT_EQ(run.merged.size(), 1u);
    EXPECT_EQ(run.detector.tracks().size(), 1u);
    EXPECT_EQ(run.detector.tracks().front().id, run.born.front());
    // And nothing opens again there.
    run.frames(3.0, {{a + 1, 1e-6}});
    EXPECT_EQ(run.born.size(), 2u);
}

TEST(SignalDetectorOneTrackPerPeak, AYoungerTrackDoesNotTakeAnOldersPeak) {
    DetectorRun run(strict_detector());
    const int a = kBins / 2 + 1000;
    run.frames(3.0, {{a, 1e-6}, {a + 3, 1e-6}});  // 70 Hz apart
    ASSERT_EQ(run.detector.tracks().size(), 2u);
    const double younger = run.detector.tracks()[1].freq_hz;
    // The younger's peak stops; only the older's stays, within the younger's reach of its frequency.
    run.frames(1.0, {{a + 1, 1e-6}});
    // It holds or dies, but it never sits on the older's frequency: no two tracks within the distance.
    const std::vector<Track> tracks = run.detector.tracks();
    if (tracks.size() == 2) EXPECT_GE(std::abs(tracks[1].freq_hz - tracks[0].freq_hz), 47.0 - 1e-9) << younger;
}

TEST(SignalDetectorHolds, ARetiredPlaceStaysClosedThenOpensAndDoublesOnARepeat) {
    DetectorRun run(strict_detector());
    const int a = kBins / 2 + 1000;
    run.frames(3.0, {{a, 1e-6}});
    ASSERT_EQ(run.born.size(), 1u);
    run.detector.retire(run.born[0], run.now, 8.0);
    EXPECT_TRUE(run.detector.tracks().empty());
    run.frames(7.0, {{a, 1e-6}});
    EXPECT_EQ(run.born.size(), 1u) << "closed for 8 s";
    run.frames(3.0, {{a, 1e-6}});
    ASSERT_EQ(run.born.size(), 2u) << "open again after 8 s and the birth time";
    run.detector.retire(run.born[1], run.now, 8.0);
    run.frames(15.0, {{a, 1e-6}});
    EXPECT_EQ(run.born.size(), 2u) << "16 s, the second time";
    run.frames(3.0, {{a, 1e-6}});
    EXPECT_EQ(run.born.size(), 3u);
}

TEST(SignalDetectorHolds, ANoHoldLeavesThePlaceOpen) {
    DetectorRun run(strict_detector());
    const int a = kBins / 2 + 1000;
    run.frames(3.0, {{a, 1e-6}});
    run.detector.retire(run.born[0], run.now, 0.0);
    run.frames(2.0, {{a, 1e-6}});
    EXPECT_EQ(run.born.size(), 2u);
}

TEST(SignalDetectorHolds, AWideSignalClosesItsRegion) {
    DetectorRun run(strict_detector());
    const int first = kBins / 2 + 1000;
    // Peak after peak of a wide signal, each retired at once, 94 Hz apart: the region around them closes at the fourth.
    for (int i = 0; i < SignalDetector::kRegionRetirements; ++i) {
        run.frames(2.0, {{first + 4 * i, 1e-6}});
        const auto tracks = run.detector.tracks();
        ASSERT_EQ(tracks.size(), 1u) << i;
        run.detector.retire(tracks[0].id, run.now, 8.0);
    }
    const std::size_t before = run.born.size();
    // A new peak inside the radius, away from every place retired: closed.
    const int inside = first + 4 * SignalDetector::kRegionRetirements + 4;
    ASSERT_LT(4 * kBinHz * 4, SignalDetector::kRegionRadiusHz);
    run.frames(5.0, {{inside, 1e-6}});
    EXPECT_EQ(run.born.size(), before);
    // Outside the radius: open.
    const int outside = first + static_cast<int>(SignalDetector::kRegionRadiusHz / kBinHz) + 40;
    run.frames(3.0, {{inside, 1e-6}, {outside, 1e-6}});
    EXPECT_EQ(run.born.size(), before + 1);
}

TEST(SignalDetectorDcGuard, NoTrackOnTheRadiosOwnOffset) {
    DetectorConfig c = strict_detector();
    c.dc_guard_hz = 47.0;
    DetectorRun run(c);
    run.frames(3.0, {{kBins / 2, 1e-6}, {kBins / 2 + 100, 1e-6}});
    ASSERT_EQ(run.detector.tracks().size(), 1u);
    EXPECT_NEAR(run.detector.tracks()[0].freq_hz, 100 * kBinHz, kBinHz);
}

TEST(SignalDetectorLimit, ASmallerLimitLetsNoMoreIn) {
    DetectorRun run(strict_detector());
    const int a = kBins / 2 + 1000;
    run.frames(3.0, {{a, 1e-6}, {a + 100, 1e-6}});
    ASSERT_EQ(run.detector.tracks().size(), 2u);
    run.detector.set_track_limit(2);
    // Weaker than either: it may not replace one.
    run.frames(3.0, {{a, 1e-6}, {a + 100, 1e-6}, {a + 200, 1e-7}});
    EXPECT_EQ(run.detector.tracks().size(), 2u);
}

TEST(SpscRingClear, SaysHowManyItForgot) {
    SpscRing<int> ring(8);
    const std::vector<int> items = {1, 2, 3};
    ASSERT_EQ(ring.push(items), 3u);
    EXPECT_EQ(ring.size(), 3u);
    EXPECT_EQ(ring.clear(), 3u);
    EXPECT_EQ(ring.size(), 0u);
    EXPECT_EQ(ring.clear(), 0u);
}

namespace {

constexpr double kEngineRate = 48000.0;

void add_to(std::vector<Sample>& a, const std::vector<Sample>& b) {
    for (std::size_t i = 0; i < a.size(); ++i) a[i] += b[i];
}

// White noise over the whole span at this density, FS^2/Hz.
void add_noise(std::vector<Sample>& x, double n0_fs2_per_hz, unsigned seed) {
    std::mt19937 rng(seed);
    const double sigma = std::sqrt(n0_fs2_per_hz * kEngineRate / 2.0);
    std::normal_distribution<float> normal(0.0f, static_cast<float>(sigma));
    for (Sample& s : x) s += Sample(normal(rng), normal(rng));
}

struct Outcome {
    std::map<std::uint32_t, std::vector<std::string>> text;
    std::map<std::uint32_t, TrackEnd> ended;
    std::map<std::uint32_t, double> freq_hz;
};

Outcome run_segmental(const std::vector<Sample>& x, int threads = 2) {
    EventBus bus;
    Outcome outcome;
    bus.subscribe([&](const Event& e) {
        if (const auto* t = std::get_if<TrackEvent>(&e)) {
            if (t->kind == TrackEvent::Kind::Born) outcome.freq_hz[t->track.id] = t->track.freq_hz;
            else outcome.ended[t->track.id] = t->end;
        } else if (const auto* d = std::get_if<DecodedTextEvent>(&e)) {
            std::vector<std::string>& list = outcome.text[d->track_id];
            for (const DecodedSymbol& c : d->chars) list.push_back(c.text);
            for (const TextCorrection& k : d->corrections) {
                list.resize(std::min(k.from_index, list.size()));
                for (const DecodedSymbol& c : k.chars) list.push_back(c.text);
            }
        }
    });
    EngineConfig config;
    config.sample_rate = static_cast<int>(kEngineRate);
    config.decoder.front_end = FrontEnd::Segmental;
    config.worker_threads = threads;
    Engine engine(config, bus);
    engine.process(x);
    engine.finish();
    return outcome;
}

std::string joined(const std::vector<std::string>& symbols) {
    std::string out;
    for (const std::string& s : symbols) out += s;
    return out;
}

}  // namespace

TEST(EngineJudge, ACarrierIsRetired) {
    const double duration_s = 30.0;
    std::vector<Sample> x(static_cast<std::size_t>(duration_s * kEngineRate));
    const double w = 2.0 * 3.14159265358979 * 3000.0 / kEngineRate;
    for (std::size_t i = 0; i < x.size(); ++i) x[i] = std::polar(0.01f, static_cast<float>(w * static_cast<double>(i)));
    add_noise(x, 1e-9, 1);
    const Outcome outcome = run_segmental(x);
    ASSERT_FALSE(outcome.ended.empty());
    const bool retired = std::any_of(outcome.ended.begin(), outcome.ended.end(), [](const auto& e) { return e.second == TrackEnd::NotMorse || e.second == TrackEnd::Quiet; });
    EXPECT_TRUE(retired);
    for (const auto& [id, symbols] : outcome.text) EXPECT_FALSE(text_evidence(symbols).plausible()) << joined(symbols);
}

TEST(EngineJudge, AStationThatFadesOutFor3SKeepsItsTrack) {
    const std::string message = "CQ CQ DE KQ2RP KQ2RP K CQ CQ DE KQ2RP KQ2RP K CQ CQ DE KQ2RP KQ2RP K CQ CQ DE KQ2RP KQ2RP K ";
    const double duration_s = 40.0;
    std::vector<Sample> x(static_cast<std::size_t>(duration_s * kEngineRate));
    add_to(x, test::keyed_signal(message, 20, kEngineRate, duration_s, 2500.0, 0.003, 0.0, 1, 0.5));
    // The whole of its level, gone from 15 s to 18 s, the noise left as it is.
    const auto first = static_cast<std::size_t>(15.0 * kEngineRate), last = static_cast<std::size_t>(18.0 * kEngineRate);
    for (std::size_t i = first; i < last; ++i) x[i] = Sample{};
    add_noise(x, 1e-9, 2);
    const Outcome outcome = run_segmental(x);
    std::size_t tracks_with_text = 0;
    for (const auto& [id, symbols] : outcome.text) {
        if (std::abs(outcome.freq_hz.at(id) - 2500.0) > 47.0) continue;
        if (text_evidence(symbols).plausible()) ++tracks_with_text;
    }
    EXPECT_EQ(tracks_with_text, 1u);
    for (const auto& [id, end] : outcome.ended) {
        if (std::abs(outcome.freq_hz.at(id) - 2500.0) < 47.0) ADD_FAILURE() << "the station's track ended";
    }
    for (const auto& [id, symbols] : outcome.text) {
        if (std::abs(outcome.freq_hz.at(id) - 2500.0) > 47.0) continue;
        const std::string text = joined(symbols);
        // The text goes on after the fade: 13.5 s to a repeat at 20 WPM, so the call comes four times in all, two after it.
        std::size_t calls = 0;
        for (std::size_t at = text.find("KQ2RP"); at != std::string::npos; at = text.find("KQ2RP", at + 1)) ++calls;
        EXPECT_GE(calls, 4u) << text;
    }
}

TEST(EngineJudge, JudgingCanBeTurnedOff) {
    const double duration_s = 20.0;
    std::vector<Sample> x(static_cast<std::size_t>(duration_s * kEngineRate));
    const double w = 2.0 * 3.14159265358979 * 3000.0 / kEngineRate;
    for (std::size_t i = 0; i < x.size(); ++i) x[i] = std::polar(0.01f, static_cast<float>(w * static_cast<double>(i)));
    add_noise(x, 1e-9, 1);
    EventBus bus;
    int ended = 0;
    bus.subscribe([&](const Event& e) {
        if (const auto* t = std::get_if<TrackEvent>(&e); t && t->kind == TrackEvent::Kind::Died) ++ended;
    });
    EngineConfig config;
    config.sample_rate = static_cast<int>(kEngineRate);
    config.decoder.front_end = FrontEnd::Segmental;
    config.judge_tracks = false;
    Engine engine(config, bus);
    engine.process(x);
    EXPECT_EQ(ended, 0);
}

TEST(EngineBudget, TheTracksWithLeastEvidenceGoFirst) {
    const double duration_s = 14.0;
    std::vector<Sample> x(static_cast<std::size_t>(duration_s * kEngineRate));
    const std::string message = "CQ CQ DE KQ2RP KQ2RP K CQ CQ DE KQ2RP KQ2RP K CQ CQ DE KQ2RP KQ2RP K";
    add_to(x, test::keyed_signal(message, 20, kEngineRate, duration_s, 2500.0, 0.003, 0.0, 1, 0.5));
    const double w = 2.0 * 3.14159265358979 * 5000.0 / kEngineRate;
    for (std::size_t i = 0; i < x.size(); ++i) x[i] += std::polar(0.001f, static_cast<float>(w * static_cast<double>(i)));
    add_noise(x, 1e-9, 3);
    EventBus bus;
    std::map<std::uint32_t, double> freq_hz;
    std::map<std::uint32_t, TrackEnd> ended;
    bus.subscribe([&](const Event& e) {
        if (const auto* t = std::get_if<TrackEvent>(&e)) {
            if (t->kind == TrackEvent::Kind::Born) freq_hz[t->track.id] = t->track.freq_hz;
            else ended[t->track.id] = t->end;
        }
    });
    EngineConfig config;
    config.sample_rate = static_cast<int>(kEngineRate);
    config.decoder.front_end = FrontEnd::Segmental;
    Engine engine(config, bus);
    const std::size_t half = x.size() / 2;
    engine.process(std::span<const Sample>(x).first(half));
    ASSERT_GE(engine.live_tracks(), 2u);
    engine.set_track_budget(1);
    engine.process(std::span<const Sample>(x).subspan(half));
    EXPECT_EQ(engine.live_tracks(), 1u);
    bool station_shed = false, carrier_shed = false;
    for (const auto& [id, end] : ended) {
        if (end != TrackEnd::Shed) continue;
        if (std::abs(freq_hz[id] - 2500.0) < 47.0) station_shed = true;
        if (std::abs(freq_hz[id] - 5000.0) < 47.0) carrier_shed = true;
    }
    EXPECT_FALSE(station_shed);
    EXPECT_TRUE(carrier_shed);
}
