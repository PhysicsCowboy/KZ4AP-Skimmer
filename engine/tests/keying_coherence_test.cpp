#include "kz4ap/keying_coherence.hpp"

#include "kz4ap/engine.hpp"
#include "kz4ap/event_bus.hpp"
#include "test_signals.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <complex>
#include <functional>
#include <map>
#include <random>
#include <string>
#include <vector>

using namespace kz4ap;

namespace {

// A key, one value a block: down with probability d, each block drawn on its own.
std::vector<float> random_key(std::size_t n, double d, unsigned seed) {
    std::mt19937 rng(seed);
    std::bernoulli_distribution down(d);
    std::vector<float> k(n);
    for (float& v : k) v = down(rng) ? 1.0f : 0.0f;
    return k;
}

// The power of g k + complex white noise of power sigma2 a value: |sqrt(g k) + n|^2, FS^2.
std::vector<float> heard(const std::vector<float>& key, double g, double sigma2, unsigned seed) {
    std::mt19937 rng(seed);
    std::normal_distribution<double> normal(0.0, std::sqrt(sigma2 / 2.0));
    std::vector<float> p(key.size());
    for (std::size_t i = 0; i < key.size(); ++i) {
        const std::complex<double> y = std::sqrt(g * key[i]) + std::complex<double>(normal(rng), normal(rng));
        p[i] = static_cast<float>(std::norm(y));
    }
    return p;
}

// The derived correlation of a copy with its key (keying_coherence.hpp): S the copy's key-down SNR, d the key's duty, one noise value a
// block (q = 1).
double derived_rho(double s, double d) { return s * std::sqrt(d * (1 - d)) / std::sqrt(s * s * d * (1 - d) + 1 + 2 * s * d); }

}  // namespace

TEST(KeyingCorrelation, ACopyAtItsSnrReadsTheDerivedCorrelation) {
    const std::size_t n = 40000;
    const std::vector<float> key = random_key(n, 0.5, 1);
    for (const double snr_db : {-6.0, -3.0, 0.0, 3.0, 10.0}) {
        const double s = std::pow(10.0, snr_db / 10.0);
        const std::vector<float> copy = heard(key, s, 1.0, 2);
        const double rho = keying_correlation(copy, key, 1);
        EXPECT_NEAR(rho, derived_rho(s, 0.5), 0.02) << snr_db << " dB";
    }
    // At d = 1/2 the form is S / (S + 2).
    EXPECT_NEAR(derived_rho(2.0, 0.5), 0.5, 1e-12);
}

TEST(KeyingCorrelation, TwoIndependentKeysReadNearZeroWithinOneOverRootN) {
    for (const std::size_t n : {300u, 3000u}) {
        double sum2 = 0.0;
        const int trials = 200;
        for (int t = 0; t < trials; ++t) {
            const std::vector<float> a = heard(random_key(n, 0.45, 100 + t), 4.0, 1.0, 300 + t);
            const std::vector<float> b = heard(random_key(n, 0.45, 500 + t), 100.0, 1.0, 700 + t);
            const double rho = keying_correlation(a, b, 1);
            sum2 += rho * rho;
        }
        // The spread of rho over trials, against 1 / sqrt(N).
        EXPECT_NEAR(std::sqrt(sum2 / trials) * std::sqrt(static_cast<double>(n)), 1.0, 0.15) << n;
    }
}

TEST(KeyingCorrelation, TheHighPassTakesOutAPileUpsCommonStart) {
    // Two callers, silent for the first 5 s of an 8 s window and then keying together, each its own key: the plain mean leaves the
    // common step (derived: rho = 0.385 without noise), the moving mean over 0.5 s does not.
    const std::size_t n = 375, start = 234, m = 23;
    std::vector<float> a = random_key(n, 0.5, 11), b = random_key(n, 0.5, 12);
    for (std::size_t i = 0; i < start; ++i) a[i] = b[i] = 0.0f;
    EXPECT_GT(keying_correlation(a, b, 1), 0.25);
    EXPECT_LT(std::abs(keying_correlation(a, b, m)), 0.15);
}

TEST(KeyingCorrelation, ConstantOrShortSeriesReadNoCorrelation) {
    const std::vector<float> flat(50, 1.0f), key = random_key(50, 0.5, 3);
    EXPECT_TRUE(std::isnan(keying_correlation(flat, key, 1)));
    EXPECT_TRUE(std::isnan(keying_correlation(std::vector<float>{1.0f}, std::vector<float>{2.0f}, 1)));
    EXPECT_THROW(keying_correlation(flat, std::vector<float>(49, 1.0f), 1), std::invalid_argument);
    EXPECT_THROW(keying_correlation(flat, key, 0), std::invalid_argument);
}

TEST(EnvelopeHistory, KeepsTheLastValuesInOrderAndStartsAgainAfterAGap) {
    EnvelopeHistory h(3);
    for (std::uint64_t b = 10; b < 15; ++b) h.push(b, static_cast<float>(b));
    std::vector<float> out;
    h.last(3, out);
    EXPECT_EQ(out, (std::vector<float>{12.0f, 13.0f, 14.0f}));
    EXPECT_EQ(h.newest_block(), 14u);
    h.push(16, 16.0f);  // block 15 missed
    EXPECT_EQ(h.size(), 1u);
    h.last(3, out);
    EXPECT_EQ(out, (std::vector<float>{16.0f}));
}

// ---- The engine: a QSO pair 50 Hz apart, and two callers keying at once ----

namespace {

constexpr double kRate = 48000.0;

void add_to(std::vector<Sample>& a, const std::vector<Sample>& b) {
    for (std::size_t i = 0; i < a.size(); ++i) a[i] += b[i];
}

void add_noise(std::vector<Sample>& x, double n0_fs2_per_hz, unsigned seed) {
    std::mt19937 rng(seed);
    const double sigma = std::sqrt(n0_fs2_per_hz * kRate / 2.0);
    std::normal_distribution<float> normal(0.0f, static_cast<float>(sigma));
    for (Sample& s : x) s += Sample(normal(rng), normal(rng));
}

struct Scene {
    std::map<std::uint32_t, std::string> text;
    std::map<std::uint32_t, TrackEnd> ended;
    std::map<std::uint32_t, double> freq_hz, born_s, ended_s;
};

// The segmental path as the engine sets it (apply_path_defaults), then `change`.
Scene run(const std::vector<Sample>& x, const std::function<void(EngineConfig&)>& change = {}) {
    EventBus bus;
    Scene r;
    double now_s = 0.0;
    std::map<std::uint32_t, std::vector<std::string>> lists;
    bus.subscribe([&](const Event& e) {
        if (const auto* f = std::get_if<SpectrumFrame>(&e)) now_s = f->time_s;
        if (const auto* t = std::get_if<TrackEvent>(&e)) {
            if (t->kind == TrackEvent::Kind::Born) {
                r.freq_hz[t->track.id] = t->track.freq_hz;
                r.born_s[t->track.id] = now_s;
            } else {
                r.ended[t->track.id] = t->end;
                r.ended_s[t->track.id] = now_s;
            }
        } else if (const auto* d = std::get_if<DecodedTextEvent>(&e)) {
            std::vector<std::string>& list = lists[d->track_id];
            for (const DecodedSymbol& c : d->chars) list.push_back(c.text);
            for (const TextCorrection& k : d->corrections) {
                list.resize(std::min(k.from_index, list.size()));
                for (const DecodedSymbol& c : k.chars) list.push_back(c.text);
            }
        }
    });
    EngineConfig config;
    config.sample_rate = static_cast<int>(kRate);
    config.decoder.front_end = FrontEnd::Segmental;
    config.worker_threads = 2;
    apply_path_defaults(config);
    if (change) change(config);
    config.as_given = true;
    Engine engine(config, bus);
    engine.process(x);
    engine.finish();
    for (const auto& [id, list] : lists)
        for (const std::string& s : list) r.text[id] += s;
    return r;
}

std::size_t count(const std::string& text, const std::string& word) {
    std::size_t n = 0;
    for (std::size_t at = text.find(word); at != std::string::npos; at = text.find(word, at + 1)) ++n;
    return n;
}

// A at 2 000 Hz, +32.6 dB S500, calls B from 0.5 s; B at 2 050 Hz, +18.6 dB S500, answers from 12 s; A sends a long over from 22 s.
// N0 1e-9 FS^2/Hz. B's channel hears A through its filter: on its own track, A's long over is a copy.
std::vector<Sample> qso_pair() {
    const double duration_s = 52.0;
    std::vector<Sample> x(static_cast<std::size_t>(duration_s * kRate));
    add_to(x, test::keyed_signal_of(test::keying("KQ2RP DE W1AW W1AW K", 25, 0.5), kRate, duration_s, 2000.0, 0.03));
    add_to(x, test::keyed_signal_of(test::keying("W1AW DE KQ2RP KQ2RP K", 25, 12.0), kRate, duration_s, 2050.0, 0.006));
    add_to(x, test::keyed_signal_of(test::keying("KQ2RP DE W1AW TNX FER CALL UR RST 599 599 NAME JOHN JOHN QTH CT CT HW BK", 25, 22.0),
                                    kRate, duration_s, 2000.0, 0.03));
    add_noise(x, 1e-9, 7);
    return x;
}

bool near(double f, double target) { return std::abs(f - target) < 15.0; }

}  // namespace

TEST(EngineCopies, WithoutTheTestTheAnsweringStationsTrackPrintsTheCallersOver) {
    // The mutation check: with the rule off the scene does hold a copy, so the next test's pass is the rule's.
    const Scene r = run(qso_pair(), [](EngineConfig& c) { c.copy_window_s = 0.0; });
    std::size_t copies = 0;
    for (const auto& [id, text] : r.text)
        if (near(r.freq_hz.at(id), 2050.0) && count(text, "JOHN") > 0) ++copies;
    EXPECT_GE(copies, 1u);
    for (const auto& [id, end] : r.ended) EXPECT_NE(end, TrackEnd::Copy);
}

TEST(EngineCopies, TheAnsweringStationsTrackIsRetiredAsACopyAndTheCallersIsNot) {
    const Scene r = run(qso_pair());
    bool b_read = false, b_retired = false;
    for (const auto& [id, text] : r.text) {
        if (near(r.freq_hz.at(id), 2050.0)) {
            b_read = b_read || count(text, "KQ2RP") > 0;
            // B's track goes before A's over ends: its copy of the over is at most a few words.
            if (const auto e = r.ended.find(id); e != r.ended.end() && e->second == TrackEnd::Copy) {
                b_retired = true;
                EXPECT_LT(r.ended_s.at(id), 45.0);
            }
            EXPECT_LT(count(text, "JOHN"), 2u) << text;
        }
        if (near(r.freq_hz.at(id), 2000.0)) {
            const auto e = r.ended.find(id);
            if (e != r.ended.end()) EXPECT_NE(e->second, TrackEnd::Copy) << "A's track retired as a copy";
        }
    }
    EXPECT_TRUE(b_read) << "B's own over is read";
    EXPECT_TRUE(b_retired);
}

namespace {

// A pile-up: two callers 60 Hz apart, 8 dB apart, sending their calls at the same time, each its own text and speed. The weaker one's
// channel hears the stronger at about -9 dB re its own power, so that their envelopes correlate at 0.5 to 0.6 (measured).
std::vector<Sample> two_callers() {
    const double duration_s = 40.0;
    std::vector<Sample> x(static_cast<std::size_t>(duration_s * kRate));
    std::string a, b;
    for (int i = 0; i < 6; ++i) {
        a += "W1AW W1AW ";
        b += "KQ2RP KQ2RP ";
    }
    add_to(x, test::keyed_signal_of(test::keying(a, 26, 0.5), kRate, duration_s, 2000.0, 0.02));
    add_to(x, test::keyed_signal_of(test::keying(b, 22, 0.8), kRate, duration_s, 2060.0, 0.008));
    add_noise(x, 1e-9, 9);
    return x;
}

}  // namespace

TEST(EngineCopies, TwoCallersKeyingAtOnceKeepTheirTracks) {
    const Scene r = run(two_callers());
    bool b_read = false;
    for (const auto& [id, end] : r.ended) EXPECT_NE(end, TrackEnd::Copy) << "track " << id << " at " << r.freq_hz.at(id) << " Hz";
    for (const auto& [id, text] : r.text)
        if (near(r.freq_hz.at(id), 2060.0)) b_read = b_read || count(text, "KQ2RP") >= 4;
    EXPECT_TRUE(b_read);
}

TEST(EngineCopies, WithoutItsOwnPowerTheWeakerCallerWouldBeTakenForACopy) {
    // The mutation check of the own-power condition: without it, and with the level difference at 6 dB (the callers stand 7 to 9 dB
    // apart at the detector), the weaker caller's track is retired as a copy.
    const Scene r = run(two_callers(), [](EngineConfig& c) {
        c.copy_own_snr_max = -1.0;
        c.copy_level_db = 6.0;
    });
    std::size_t copies = 0;
    for (const auto& [id, end] : r.ended)
        if (end == TrackEnd::Copy && near(r.freq_hz.at(id), 2060.0)) ++copies;
    EXPECT_GE(copies, 1u);
}


TEST(EngineCopies, TwoCallersAtOnceKeepTheirTracksOnTheLevelAloneToo) {
    // The level difference's own share: with the own-power condition off, 15 dB keeps the callers 7 to 9 dB apart.
    const Scene r = run(two_callers(), [](EngineConfig& c) { c.copy_own_snr_max = -1.0; });
    for (const auto& [id, end] : r.ended) EXPECT_NE(end, TrackEnd::Copy) << "track " << id << " at " << r.freq_hz.at(id) << " Hz";
}

TEST(EngineCopies, TheTestIsTheSegmentalPathsDefault) {
    EngineConfig c;
    c.decoder.front_end = FrontEnd::Segmental;
    apply_path_defaults(c);
    EXPECT_DOUBLE_EQ(c.copy_window_s, 6.0);
    EngineConfig m;
    m.decoder.front_end = FrontEnd::Matched;
    apply_path_defaults(m);
    EXPECT_DOUBLE_EQ(m.copy_window_s, 0.0);
}
