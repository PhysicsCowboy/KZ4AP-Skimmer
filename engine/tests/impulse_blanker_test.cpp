// The impulse blanker (impulse_blanker.hpp) on generated signals: what it must find (a crash, a click train) and what it must not
// (a crowded band, a strong station's hard keying and its rise), and the segmental decoder's text with it.
#include "kz4ap/engine.hpp"
#include "kz4ap/impulse_blanker.hpp"
#include "kz4ap/segmental_decoder.hpp"

#include "test_signals.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <numbers>
#include <random>
#include <span>
#include <string>
#include <vector>

using namespace kz4ap;

namespace {

constexpr int kRate = 192000;
constexpr double kSigma = 0.02;  // FS: complex white noise, N0 = sigma^2 / rate

// The amplitude of a carrier of `db` dB S500 against kSigma at kRate.
double amplitude_s500(double db) { return std::sqrt(std::pow(10.0, db / 10.0) * 500.0 * kSigma * kSigma / kRate); }

std::vector<Sample> noise(double duration_s, unsigned seed) {
    std::mt19937 rng(seed);
    std::normal_distribution<float> g(0.0f, static_cast<float>(kSigma / std::sqrt(2.0)));
    std::vector<Sample> x(static_cast<std::size_t>(duration_s * kRate));
    for (Sample& s : x) s = Sample(g(rng), g(rng));
    return x;
}

// A crash: complex white noise of `ratio` times the noise's power per sample for `length_s`, from `start_s`, flat over the band.
void add_crash(std::vector<Sample>& x, double start_s, double length_s, double ratio, unsigned seed) {
    std::mt19937 rng(seed);
    std::normal_distribution<float> g(0.0f, static_cast<float>(std::sqrt(ratio) * kSigma / std::sqrt(2.0)));
    const auto first = static_cast<std::size_t>(start_s * kRate);
    const auto last = std::min(x.size(), first + static_cast<std::size_t>(length_s * kRate));
    for (std::size_t i = first; i < last; ++i) x[i] += Sample(g(rng), g(rng));
}

// A carrier keyed by intervals (s) at freq_hz, amplitude a, with raised-cosine edges of rise_s (0: hard keying, a click at
// every edge), added to x. A phasor turned sample by sample, renormalized every 4096 samples.
void add_keyed(std::vector<Sample>& x, const std::vector<std::pair<double, double>>& intervals, double freq_hz, double a,
               double rise_s, double phase = 0.0) {
    const double dt = 1.0 / kRate;
    std::complex<double> turn = std::polar(1.0, 2.0 * std::numbers::pi * freq_hz * dt);
    for (const auto& [on, off] : intervals) {
        const auto i0 = static_cast<std::size_t>(std::max(0.0, on) * kRate);
        const auto i1 = std::min(x.size(), static_cast<std::size_t>(off * kRate) + 1);
        std::complex<double> p = std::polar(1.0, phase + 2.0 * std::numbers::pi * freq_hz * static_cast<double>(i0) * dt);
        for (std::size_t i = i0; i < i1; ++i) {
            const double t = static_cast<double>(i) * dt;
            double e = 1.0;
            if (rise_s > 0.0) {
                if (t - on < rise_s) e = 0.5 - 0.5 * std::cos(std::numbers::pi * (t - on) / rise_s);
                if (off - t < rise_s) e = std::min(e, 0.5 - 0.5 * std::cos(std::numbers::pi * (off - t) / rise_s));
            } else if (t >= off) {
                e = 0.0;
            }
            x[i] += Sample(static_cast<float>(a * e * p.real()), static_cast<float>(a * e * p.imag()));
            p *= turn;
            if ((i & 4095) == 0) p /= std::abs(p);
        }
    }
}

struct Outcome {
    std::string text;               // the first channel's final text (oracle mode)
    std::uint64_t firings = 0;
    std::uint64_t fired_slices = 0;
    std::uint64_t slices = 0;
    std::uint64_t blanked = 0, pushed = 0;
    std::vector<float> series;      // the whole series, from channel sample 0
};

// The engine on x: oracle channels at `oracle_hz` (none: the detector's path), the blanker in `mode`.
Outcome run(const std::vector<Sample>& x, int mode, std::vector<double> oracle_hz, double threshold_db = 0.0) {
    EventBus bus;
    EngineConfig config;
    config.sample_rate = kRate;
    config.decoder.front_end = FrontEnd::Segmental;
    config.oracle_frequencies_hz = std::move(oracle_hz);
    config.impulse.mode = mode;
    config.impulse.threshold_db = threshold_db;
    std::vector<std::string> list;
    bus.subscribe([&](const Event& e) {
        if (const auto* d = std::get_if<DecodedTextEvent>(&e)) {
            if (d->track_id != 1) return;
            for (const auto& c : d->chars) list.push_back(c.text);
            for (const auto& k : d->corrections) {
                list.resize(std::min(list.size(), k.from_index));
                for (const auto& c : k.chars) list.push_back(c.text);
            }
        }
    });
    Engine engine(config, bus);
    Outcome r;
    constexpr std::size_t kChunk = 65536;
    for (std::size_t i = 0; i < x.size(); i += kChunk) {
        engine.process(std::span<const Sample>(x).subspan(i, std::min(kChunk, x.size() - i)));
        if (const ImpulseBlanker* b = engine.impulse_blanker()) {
            const ImpulseSeries& s = b->series();
            for (std::uint64_t c = std::max<std::uint64_t>(r.series.size(), s.first); c < s.end(); ++c) {
                r.series.resize(c + 1, 0.0f);
                r.series[c] = static_cast<float>(s.at(static_cast<std::int64_t>(c)));
            }
        }
    }
    engine.finish();
    for (const std::string& s : list) r.text += s;
    const EngineStats st = engine.stats();
    r.firings = st.impulse_firings;
    r.fired_slices = st.impulse_fired_slices;
    r.slices = st.impulse_slices;
    r.blanked = st.impulse_blanked_samples;
    r.pushed = st.impulse_pushed_samples;
    return r;
}

std::string squeeze(std::string s) {
    s.erase(std::remove(s.begin(), s.end(), ' '), s.end());
    return s;
}

}  // namespace

TEST(ImpulseBlanker, ResponseSharesSumToOneAndCarryTheFiltersNoise) {
    // The cascade's |g|^2 summed is sigma_y^2 / N0 / rate (derived: white noise of N0 FS^2/Hz has N0 rate FS^2 a sample).
    const SegmentalShared shared(segmental::SegmentalConfig{}, ChannelizerConfig{kRate, 8192, 64, 150.0});
    double total = 0.0;
    for (double share : shared.impulse_response) total += share;
    EXPECT_NEAR(total, 1.0, 1e-12);
    EXPECT_GT(shared.impulse_lead, 10);
    // Its center at the two filters' delays, 10.67 ms + 8 ms = 28 channel samples, and its width the front filter's dot and edge
    // with the channel filter's main lobe: 12 ms + 4.2 ms + about 3 ms, some 29 channel samples (derived).
    double centroid = 0.0;
    std::size_t first = 0, last = 0;
    for (std::size_t d = 0; d < shared.impulse_response.size(); ++d) {
        centroid += static_cast<double>(d) * shared.impulse_response[d];
        if (shared.impulse_response[d] >= 1e-3) {
            if (first == 0) first = d;
            last = d;
        }
    }
    EXPECT_NEAR(centroid, 28.5, 0.5);
    EXPECT_NEAR(static_cast<double>(last - first + 1), 25.0, 4.0);
    // At the narrower filter (m = 3) the same center and three times the front filter's span.
    const segmental::SegmentalConfig wide;
    double narrow_centroid = 0.0;
    for (std::size_t d = 0; d < shared.narrower.at(0).impulse_response.size(); ++d)
        narrow_centroid += static_cast<double>(d) * shared.narrower.at(0).impulse_response[d];
    EXPECT_GT(narrow_centroid, centroid + 10.0);
}

TEST(ImpulseBlanker, NoiseAloneNeverFires) {
    const Outcome r = run(noise(12.0, 1), 1, {20000.0});
    EXPECT_GT(r.slices, 10000u);
    EXPECT_EQ(r.firings, 0u);
}

TEST(ImpulseBlanker, FindsACrashWhereItIsAndAsStrongAsItIs) {
    // 1 ms of 30 dB re the noise's power per sample: 192 samples, 1.5 channel samples of 1000 times the noise each.
    auto x = noise(8.0, 2);
    add_crash(x, 5.0, 0.001, 1000.0, 3);
    const Outcome r = run(x, 1, {20000.0});
    EXPECT_EQ(r.firings, 1u);
    const auto peak = std::max_element(r.series.begin(), r.series.end()) - r.series.begin();
    EXPECT_NEAR(static_cast<double>(peak), 5.0 * 1500.0, 1.0);
    double sum = 0.0;
    for (float u : r.series) sum += u;
    // The crash's energy in channel samples of the noise: 1000 x 1.5 (white over the band; the median of 16 subbands).
    EXPECT_NEAR(sum, 1500.0, 300.0);
}

TEST(ImpulseBlanker, ACrowdedBandOfAHundredCarriersNeverFires) {
    // 100 keyed carriers over the band, +5 to +45 dB S500, 12 to 40 WPM; half of them keyed in step (the same text and timing), so
    // that their edges come at once.
    auto x = noise(12.0, 4);
    std::mt19937 rng(5);
    std::uniform_real_distribution<double> f(-90000.0, 90000.0), level(5.0, 45.0), speed(12.0, 40.0), phase(0.0, 6.283);
    const auto together = test::keying("CQ TEST DE K1ABC K1ABC TEST", 30.0, 0.7);
    const char* texts[] = {"CQ CQ DE W1AW K", "TEST N5XX N5XX", "5NN TU", "QRZ DE DL1ABC", "R R 599 001 K"};
    for (int i = 0; i < 100; ++i) {
        const auto keys = i % 2 == 0 ? together : test::keying(std::string(texts[i % 5]) + " " + texts[(i + 2) % 5], speed(rng), 0.3 + 0.01 * i);
        add_keyed(x, keys, f(rng), amplitude_s500(level(rng)), 0.005, phase(rng));
    }
    const Outcome r = run(x, 1, {20000.0});
    EXPECT_EQ(r.firings, 0u) << r.fired_slices << " of " << r.slices << " channel samples";
}

TEST(ImpulseBlanker, AStrongStationsHardKeyingAndRiseDoNotFire) {
    // Hard keying (no rise: a click at every edge, its power falling as 1 / (4 sin^2(pi f / fs)) from the carrier, the sampled step's)
    // at +60 and +70 dB S500, each with its first rise from silence; a second station at +40 dB S500. At +60 dB S500 the clicks stand
    // above the noise in most subbands, so the flatness test keeps them out: no firing. At +70 dB S500 they stand 10 dB above the
    // noise across the whole band, far side included (the sampled step's spectrum flattens to 1 / 4 there), and of its 98 edges
    // at most a few pass as a pulse, which they then are for a channel far away. Either way the strong station's own text is the
    // same with the blanker as without.
    for (const double level : {60.0, 70.0}) {
        auto x = noise(12.0, 6);
        add_keyed(x, test::keying("CQ CQ DE W1AW W1AW K", 28.0, 1.5), 30000.0, amplitude_s500(level), 0.0);
        add_keyed(x, test::keying("TEST K1ABC TEST K1ABC", 35.0, 2.2), -45000.0, amplitude_s500(40.0), 0.0);
        const Outcome on = run(x, 2, {30000.0});
        if (level == 60.0) {
            EXPECT_EQ(on.firings, 0u) << on.fired_slices << " of " << on.slices << " channel samples";
        } else {
            EXPECT_LE(on.firings, 3u) << on.fired_slices << " of " << on.slices << " channel samples";
        }
        EXPECT_EQ(on.text, run(x, 0, {30000.0}).text) << level << " dB S500";
    }
}

TEST(ImpulseBlanker, ACrashInAWordGapIsNotReadAsAMark) {
    // A strong machine-sent station (+30 dB S500, 20 WPM) and a crash in its word gap that puts about +19 dB re sigma^2 into its
    // channel (ear02-control's): 3 ms of 300 times the noise per sample, E / sigma^2 = 1.7e5 samples of noise.
    const double wpm = 20.0, dit = 1.2 / wpm;
    const auto keys = test::keying("CQ CQ DE K2PDJ K2PDJ CQ CQ DE K2PDJ K2PDJ", wpm, 0.6);
    auto x = noise(keys.back().second + 1.5, 7);
    add_keyed(x, keys, 20000.0, amplitude_s500(30.0), 0.004);
    // The gaps of 7 dits: a crash 3 dits into each.
    for (std::size_t i = 1; i < keys.size(); ++i)
        if (keys[i].first - keys[i - 1].second > 6.0 * dit) add_crash(x, keys[i - 1].second + 3.0 * dit, 0.003, 300.0, 8 + static_cast<unsigned>(i));
    const Outcome on = run(x, 1, {20000.0});
    EXPECT_EQ(squeeze(on.text), "CQCQDEK2PDJK2PDJCQCQDEK2PDJK2PDJ") << on.text;
    EXPECT_GE(on.firings, 7u);
    const Outcome noise_mode = run(x, 2, {20000.0});
    EXPECT_EQ(squeeze(noise_mode.text), "CQCQDEK2PDJK2PDJCQCQDEK2PDJK2PDJ") << noise_mode.text;
    // Without the blanker a crash is read as a mark (the test's point: ear08-control's CQECQ).
    const Outcome off = run(x, 0, {20000.0});
    EXPECT_NE(squeeze(off.text), "CQCQDEK2PDJK2PDJCQCQDEK2PDJK2PDJ") << off.text;
}

TEST(ImpulseBlanker, AClickTrainThroughAKeyedSignalLeavesItsText) {
    // Clicks every 97 ms (out of step with the keying), each 0.5 ms of 1000 times the noise per sample, through a station at
    // +20 dB S500 and 25 WPM: many land inside its marks and on its edges.
    const auto keys = test::keying("CQ CQ DE W1AW W1AW K", 25.0, 1.2);
    auto x = noise(keys.back().second + 1.5, 9);
    add_keyed(x, keys, -20000.0, amplitude_s500(20.0), 0.005);
    for (double t = 1.01; t < x.size() / static_cast<double>(kRate); t += 0.097) add_crash(x, t, 0.0005, 1000.0, static_cast<unsigned>(t * 1000));
    for (const int mode : {1, 2}) {
        const Outcome on = run(x, mode, {-20000.0});
        EXPECT_EQ(squeeze(on.text), "CQCQDEW1AWW1AWK") << "mode " << mode << ": " << on.text;
    }
    const Outcome off = run(x, 0, {-20000.0});
    EXPECT_NE(squeeze(off.text), "CQCQDEW1AWW1AWK") << off.text;
}

TEST(ImpulseBlanker, AStationKeyingAtTheCrashKeepsItsText) {
    // A crash on each of a station's marks in turn, at its middle, its rise or its fall: the station keys exactly at the impulse.
    const auto keys = test::keying("TEST K1ABC K1ABC TEST", 22.0, 0.6);
    auto x = noise(keys.back().second + 1.5, 10);
    add_keyed(x, keys, 5000.0, amplitude_s500(25.0), 0.005);
    for (std::size_t i = 0; i < keys.size(); i += 2) {
        const double at = i % 6 == 0 ? keys[i].first : i % 6 == 2 ? 0.5 * (keys[i].first + keys[i].second) : keys[i].second;
        add_crash(x, at - 0.0015, 0.003, 300.0, 100 + static_cast<unsigned>(i));
    }
    const Outcome on = run(x, 1, {5000.0});
    EXPECT_EQ(squeeze(on.text), "TESTK1ABCK1ABCTEST") << on.text;
    const Outcome noise_mode = run(x, 2, {5000.0});
    EXPECT_EQ(squeeze(noise_mode.text), "TESTK1ABCK1ABCTEST") << noise_mode.text;
}
