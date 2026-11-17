#include "kz4ap/keying_coherence.hpp"

#include "kz4ap/callsign.hpp"
#include "kz4ap/engine.hpp"
#include "kz4ap/event_bus.hpp"
#include "test_signals.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <complex>
#include <functional>
#include <algorithm>
#include <map>
#include <random>
#include <stdexcept>
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
    for (const auto& [id, end] : r.ended) EXPECT_NE(end, TrackEnd::Copy) << "track " << id << " at " << r.freq_hz.at(id) << " Hz";
    // B's calls are read on its own tracks (how many differs by a call or two with the compiler's arithmetic).
    std::size_t calls = 0;
    std::string texts;
    for (const auto& [id, text] : r.text) {
        if (!near(r.freq_hz.at(id), 2060.0)) continue;
        calls += count(text, "KQ2RP");
        texts += " [" + text + "]";
    }
    EXPECT_GE(calls, 4u) << texts;
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


TEST(EngineCopies, TwoCallersAtOnceKeepTheirTracksOnTheirOwnPowerAlone) {
    // The own-power condition's own share: with the level difference at 6 dB (the callers stand 7 to 9 dB apart at the detector, so
    // the level does not protect them), the weaker caller's own power keeps its track.
    const Scene r = run(two_callers(), [](EngineConfig& c) { c.copy_level_db = 6.0; });
    for (const auto& [id, end] : r.ended) EXPECT_NE(end, TrackEnd::Copy) << "track " << id << " at " << r.freq_hz.at(id) << " Hz";
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

TEST(EngineCopies, RefusesBoundsThatAreNotNumbers) {
    // A NaN would turn the test off without a word.
    for (const int which : {0, 1, 2}) {
        EngineConfig c;
        c.sample_rate = static_cast<int>(kRate);
        c.decoder.front_end = FrontEnd::Segmental;
        if (which == 0) c.copy_gain_max = std::nan("");
        if (which == 1) c.copy_own_snr_max = std::nan("");
        if (which == 2) c.copy_gain_max = 0.0;
        EventBus bus;
        EXPECT_THROW({ Engine engine(c, bus); }, std::invalid_argument) << which;
    }
}


// ---- A neighbor's text (EngineConfig::attribute_pad_s): a runner and his caller 45 Hz apart, and a QSO pair 50 Hz apart ----

namespace {

// What each track printed: all of it, what it shows (its own, heard_from 0) and what it gave a neighbor; and the callsign stage's
// readings with the frequency of the track they came from.
struct Attributed {
    std::map<std::uint32_t, std::string> all, own, heard;
    std::map<std::uint32_t, double> freq_hz, born_s, ended_s;
    std::map<std::uint32_t, TrackEnd> ended;
    std::vector<std::pair<callsign::CallReading, double>> readings;
    std::vector<std::string> calls;
};

Attributed run_attributed(const std::vector<Sample>& x, const std::function<void(EngineConfig&)>& change = {},
                          callsign::CallSearchConfig stage_config = {}) {
    EventBus bus;
    Attributed r;
    callsign::CallSearch stage(stage_config);
    stage.set_reading_log([&](const callsign::CallReading& reading, double f) { r.readings.emplace_back(reading, f); });
    std::map<std::uint32_t, std::vector<DecodedSymbol>> lists;
    double now_s = 0.0;
    bus.subscribe([&](const Event& e) {
        if (const auto* f = std::get_if<SpectrumFrame>(&e)) {
            now_s = f->time_s;
            stage.advance(f->time_s);
        }
        if (const auto* t = std::get_if<TrackEvent>(&e)) {
            if (t->kind == TrackEvent::Kind::Born) {
                r.freq_hz[t->track.id] = t->track.freq_hz;
                r.born_s[t->track.id] = now_s;
            } else {
                r.ended[t->track.id] = t->end;
                r.ended_s[t->track.id] = now_s;
            }
            stage.apply(*t);
        } else if (const auto* d = std::get_if<DecodedTextEvent>(&e)) {
            std::vector<DecodedSymbol>& list = lists[d->track_id];
            for (const DecodedSymbol& c : d->chars) list.push_back(c);
            for (const TextCorrection& k : d->corrections) {
                list.resize(std::min(k.from_index, list.size()));
                for (const DecodedSymbol& c : k.chars) list.push_back(c);
            }
            stage.apply(*d);
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
    stage.finish();
    for (const auto& [id, list] : lists) {
        for (const DecodedSymbol& s : list) {
            r.all[id] += s.text;
            // A neighbor's symbol leaves a word space in the track's own text, so that no word of its own is glued to the next.
            if (s.heard_from == 0) {
                r.own[id] += s.text;
                r.heard[id] += " ";
            } else {
                r.own[id] += " ";
                r.heard[id] += s.text;
            }
        }
    }
    for (const callsign::CallRow& c : stage.calls()) r.calls.push_back(c.base);
    return r;
}

// The text of every track near a frequency (+/-15 Hz), one string.
std::string near_text(const std::map<std::uint32_t, std::string>& texts, const std::map<std::uint32_t, double>& freq_hz, double f) {
    std::string out;
    for (const auto& [id, text] : texts)
        if (near(freq_hz.at(id), f)) out += " [" + text + "]";
    return out;
}

// Every track: where, when, how it ended, and all it printed (for a failure's message).
std::string tracks_of(const Attributed& r) {
    std::string out;
    for (const auto& [id, f] : r.freq_hz) {
        out += "\n  " + std::to_string(id) + " at " + std::to_string(static_cast<int>(f)) + " Hz from " + std::to_string(r.born_s.at(id)) + " s";
        if (const auto e = r.ended.find(id); e != r.ended.end())
            out += " to " + std::to_string(r.ended_s.at(id)) + " s (end " + std::to_string(static_cast<int>(e->second)) + ")";
        if (const auto t = r.all.find(id); t != r.all.end()) out += ": " + t->second;
    }
    return out;
}

bool has_text(const std::string& s) { return s.find_first_not_of(' ') != std::string::npos; }

// The runner's own words in a text (the contest scene's).
std::size_t runner_words(const std::string& s) { return count(s, "TEST") + count(s, "W1AW") + count(s, " CT") + count(s, "TNX"); }

double over_end(const std::vector<std::pair<double, double>>& k) { return k.back().second; }

// A contest QSO: W1AW runs at 2 000 Hz, +32.6 dB S500; KQ2RP calls 45 Hz above, 8 dB weaker, and they trade short overs, 1 s apart.
// KQ2RP's channel hears W1AW through its skirt about 4 dB above KQ2RP's own keying, so that W1AW's overs print on KQ2RP's tracks until
// the copy test retires them, 6 s into an over of W1AW's, and on the next. N0 1e-9 FS^2/Hz. The runner's words: CQ, TEST, W1AW, CT,
// TNX; the caller's own: KQ2RP, MA, TU.
struct Contest {
    std::vector<Sample> x;
    double caller_hz = 2045.0, runner_hz = 2000.0;
    std::vector<std::pair<double, double>> caller_overs, runner_overs;  // (start, end), s
};

Contest contest() {
    Contest c;
    const double wpm = 26.0;
    std::vector<std::pair<std::string, bool>> overs = {
        {"CQ TEST W1AW W1AW TEST", true}, {"KQ2RP KQ2RP", false}, {"KQ2RP 5NN CT", true}, {"TU 5NN MA", false},
        {"TNX W1AW TEST", true},          {"CQ TEST W1AW", true}, {"KQ2RP", false},      {"KQ2RP 5NN CT CT", true},
        {"R 5NN MA MA", false},           {"TNX W1AW TEST", true}};
    const double duration_s = 66.0;
    c.x.assign(static_cast<std::size_t>(duration_s * kRate), Sample{});
    double t = 0.5;
    for (const auto& [text, runner] : overs) {
        const auto k = test::keying(text, wpm, t);
        (runner ? c.runner_overs : c.caller_overs).emplace_back(t, over_end(k));
        add_to(c.x, test::keyed_signal_of(k, kRate, duration_s, runner ? c.runner_hz : c.caller_hz, runner ? 0.03 : 0.012));
        t = over_end(k) + 1.0;
    }
    add_noise(c.x, 1e-9, 21);
    return c;
}

bool inside(const std::vector<std::pair<double, double>>& overs, double a, double b) {
    return std::any_of(overs.begin(), overs.end(), [&](const auto& o) { return a >= o.first - 0.1 && b <= o.second + 0.1; });
}

}  // namespace

TEST(EngineNeighborText, WithoutItTheCallersTracksPrintTheRunnersWords) {
    // The mutation check: with the attribution off the caller's tracks print some of the runner's words before each is retired as a
    // copy, so that the next tests' passes are the attribution's.
    const Contest c = contest();
    const Attributed r = run_attributed(c.x, [](EngineConfig& e) { e.attribute_pad_s = 0.0; });
    const std::string caller = near_text(r.own, r.freq_hz, c.caller_hz);
    EXPECT_GE(runner_words(caller), 2u) << caller << tracks_of(r);
    EXPECT_GE(count(caller, "KQ2RP"), 3u) << caller;
    for (const auto& [id, text] : r.heard) EXPECT_FALSE(has_text(text)) << id;
}

TEST(EngineNeighborText, TheRunnersWordsOnTheCallersTracksAreTheRunnersAndTheCallersOwnTextStays) {
    const Contest c = contest();
    const Attributed r = run_attributed(c.x);
    const std::string caller = near_text(r.own, r.freq_hz, c.caller_hz), heard = near_text(r.heard, r.freq_hz, c.caller_hz);
    // The runner's words are given to the runner's track, which prints them itself.
    EXPECT_EQ(runner_words(caller), 0u) << caller << " | heard:" << heard << tracks_of(r);
    EXPECT_GE(runner_words(heard), 2u) << heard;
    // The caller's own words stay on his tracks: his call, sent three times, and his exchange.
    EXPECT_GE(count(caller, "KQ2RP"), 3u) << caller;
    EXPECT_GE(count(caller, "MA"), 1u) << caller;
    // And the runner's track shows his own overs.
    const std::string runner = near_text(r.own, r.freq_hz, c.runner_hz);
    EXPECT_GE(count(runner, "W1AW"), 5u) << runner;
    EXPECT_GE(count(runner, "TEST"), 5u) << runner;
}

namespace {

// The callsign stage's readings on the caller's tracks: of his call in his own overs, and of any call in the runner's overs.
std::pair<std::size_t, std::size_t> caller_readings(const Contest& c, const Attributed& r) {
    std::size_t own = 0, runners = 0;
    for (const auto& [reading, f] : r.readings) {
        if (!near(f, c.caller_hz)) continue;
        if (reading.base == "KQ2RP" && inside(c.caller_overs, reading.start_s, reading.end_s)) ++own;
        if (inside(c.runner_overs, reading.start_s, reading.end_s)) ++runners;
    }
    return {own, runners};
}

}  // namespace

TEST(EngineNeighborText, TheCallsignStageReadsTheCallersCallOnHisTrackAndLessOfTheRunnersOverThere) {
    // A stretch is skipped where the runner's own track holds it whole (the same symbols at the same times); what it reads otherwise
    // (W1AA for W1AW) is read on the caller's track still.
    const Contest c = contest();
    const Attributed r = run_attributed(c.x);
    callsign::CallSearchConfig all;
    all.skip_neighbors_text = 0.0;
    const Attributed before = run_attributed(c.x, {}, all);
    const auto [own, runners] = caller_readings(c, r);
    const auto [own_before, runners_before] = caller_readings(c, before);
    EXPECT_GE(own, 2u) << "the caller's call, read in his own overs on his own tracks";
    EXPECT_EQ(own, own_before);
    EXPECT_LT(runners, runners_before) << "calls read on the caller's tracks during the runner's overs";
    EXPECT_NE(std::find(r.calls.begin(), r.calls.end(), "KQ2RP"), r.calls.end());
    EXPECT_NE(std::find(r.calls.begin(), r.calls.end(), "W1AW"), r.calls.end());
}
namespace {

// A QSO pair 50 Hz apart: A at 2 000 Hz, +32.6 dB S500, and B at 2 050 Hz, 12 dB weaker, each with long overs in turn, 1.5 s
// apart. B's own words: OHIO, BOB; A's: TEXAS, JIM.
std::vector<Sample> ragchew(double b_hz) {
    const double wpm = 24.0, duration_s = 70.0;
    std::vector<Sample> x(static_cast<std::size_t>(duration_s * kRate));
    const std::vector<std::pair<std::string, bool>> overs = {
        {"KQ2RP DE W1AW NAME JIM JIM QTH TEXAS TEXAS HW BK", true},
        {"BK W1AW DE KQ2RP NAME BOB BOB QTH OHIO OHIO HW BK", false},
        {"BK FB BOB QTH TEXAS RIG 100 WATTS HW BK", true},
        {"BK FB JIM OHIO RIG 5 WATTS HW BK", false}};
    double t = 0.5;
    for (const auto& [text, a] : overs) {
        const auto k = test::keying(text, wpm, t);
        add_to(x, test::keyed_signal_of(k, kRate, duration_s, a ? 2000.0 : b_hz, a ? 0.03 : 0.0075));
        t = over_end(k) + 1.5;
    }
    add_noise(x, 1e-9, 31);
    return x;
}

}  // namespace

TEST(EngineNeighborText, AQsoPartnerFortyToSixtyHzAwayKeepsItsOwnText) {
    for (const double b_hz : {2040.0, 2050.0, 2060.0}) {
        const Attributed r = run_attributed(ragchew(b_hz));
        const std::string b = near_text(r.own, r.freq_hz, b_hz), a = near_text(r.own, r.freq_hz, 2000.0);
        EXPECT_GE(count(b, "OHIO"), 2u) << b_hz << " Hz: " << b;
        EXPECT_GE(count(b, "BOB"), 1u) << b_hz << " Hz: " << b;
        EXPECT_GE(count(a, "TEXAS"), 2u) << b_hz << " Hz: " << a;
        // What either shows of the other's over is at most a word.
        EXPECT_LE(count(b, "TEXAS") + count(b, "JIM"), 1u) << b_hz << " Hz: " << b;
        EXPECT_LE(count(a, "OHIO"), 1u) << b_hz << " Hz: " << a;
    }
}

TEST(EngineNeighborText, IsTheSegmentalPathsDefaultAndRefusesBadBounds) {
    EngineConfig c;
    c.decoder.front_end = FrontEnd::Segmental;
    apply_path_defaults(c);
    EXPECT_DOUBLE_EQ(c.attribute_pad_s, 0.5);
    EngineConfig m;
    m.decoder.front_end = FrontEnd::Matched;
    apply_path_defaults(m);
    EXPECT_DOUBLE_EQ(m.attribute_pad_s, 0.0);
    for (const int which : {0, 1, 2}) {
        EngineConfig e;
        e.sample_rate = static_cast<int>(kRate);
        e.decoder.front_end = FrontEnd::Segmental;
        apply_path_defaults(e);
        e.as_given = true;
        if (which == 0) e.attribute_own_max = std::nan("");
        if (which == 1) e.attribute_pad_s = -1.0;
        if (which == 2) e.attribute_history_s = 0.0;
        EventBus bus;
        EXPECT_THROW({ Engine engine(e, bus); }, std::invalid_argument) << which;
    }
}

TEST(EnvelopeHistory, GivesARangeOfBlocksOnlyWhenItHoldsThemAll) {
    EnvelopeHistory h(4);
    for (std::uint64_t b = 10; b < 16; ++b) h.push(b, static_cast<float>(b));  // holds 12 to 15
    EXPECT_EQ(h.oldest_block(), 12u);
    std::vector<float> out;
    ASSERT_TRUE(h.range(13, 14, out));
    EXPECT_EQ(out, (std::vector<float>{13.0f, 14.0f}));
    ASSERT_TRUE(h.range(12, 15, out));
    EXPECT_EQ(out, (std::vector<float>{12.0f, 13.0f, 14.0f, 15.0f}));
    out = {1.0f};
    EXPECT_FALSE(h.range(11, 13, out));
    EXPECT_FALSE(h.range(14, 16, out));
    EXPECT_FALSE(h.range(14, 13, out));
    EXPECT_EQ(out, (std::vector<float>{1.0f}));
}

TEST(KeyingCorrelation, TheKeyingPowerSaysWhichChannelHearsItLouder) {
    // A key heard at 20 and at 10 dB SNR: the mean square of the envelope power about its mean is g^2 d (1 - d) + sigma^4 + 2 g sigma^2 d
    // (derived; g the key-down power, sigma^2 the noise's), 2 601 and 36 FS^4 at d = 1/2 and sigma^2 = 1 FS^2; the high-pass takes about
    // the same share of both.
    const std::vector<float> key = random_key(4000, 0.5, 41);
    const std::vector<float> source = heard(key, 100.0, 1.0, 42), copy = heard(key, 10.0, 1.0, 43);
    const KeyingCoherence k = keying_coherence(copy, source, 23);
    EXPECT_LT(k.weak_keying, k.strong_keying);
    EXPECT_NEAR(k.weak_keying / k.strong_keying, 36.0 / 2601.0, 0.003);
}
TEST(KeyingCorrelation, OfTwoNeighborsTheOneThatHearsTheKeyingLouderOwnsIt) {
    // c18c087's rule (Engine::attribute): of several neighbors that pass, the one of largest keying power owns a symbol, not the one of
    // highest rho. Here the nearer copy correlates better with the weak channel (both hear the key through skirts) but the station's own
    // channel holds it 10 dB louder.
    KeyingCoherence own_channel{0.6, 0.05, 1.0, 10.0, 0.01, 100.0};
    KeyingCoherence nearer_copy{0.9, 0.2, 1.0, 3.0, 0.01, 10.0};
    EXPECT_TRUE(louder_owner(own_channel, nearer_copy));
    EXPECT_FALSE(louder_owner(nearer_copy, own_channel));
}