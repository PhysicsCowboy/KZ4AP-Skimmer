// The segmental decoder's running sums, shared level and search bookkeeping. The search is fed made-up evidence here, so
// that what it does with the evidence is checked apart from how the evidence is made; the exact decodes through the
// engine are BenchRun.SegmentalDecodesExactlyThroughTheBench (bench/tests/bench_run_test.cpp).
#include "kz4ap/channelizer.hpp"
#include "kz4ap/morse.hpp"
#include "kz4ap/segmental/evidence.hpp"
#include "kz4ap/segmental/fast_math.hpp"
#include "kz4ap/segmental/search.hpp"
#include "kz4ap/segmental_decoder.hpp"
#include "kz4ap/spectrum.hpp"
#include "kz4ap/viterbi/observation.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <complex>
#include <iostream>
#include <limits>
#include <numbers>
#include <random>
#include <string>
#include <vector>

using namespace kz4ap;
using namespace kz4ap::segmental;

namespace {

using Window = std::array<double, kEdgeCount>;

TEST(SegmentalFastMath, HoldsItsStatedErrors) {
    for (double y = 0.0; y > -40.0; y -= 0.0037) {
        const double exact = std::exp(y);
        EXPECT_LE(std::fabs(fast::exp_neg(y) - exact), 7e-10 * exact) << y;
    }
    EXPECT_EQ(fast::exp_neg(0.0), 1.0);
    EXPECT_EQ(fast::exp_neg(-41.0), 0.0);
    EXPECT_EQ(fast::exp_neg(-1e9), 0.0);
    for (double x = 1e-300; x < 1e300; x *= 1.37) EXPECT_NEAR(fast::log(x), std::log(x), 2e-11) << x;
    for (double x = 1e-6; x < 2.0; x *= 1.0003) EXPECT_NEAR(fast::log(x), std::log(x), 2e-11) << x;
    for (double x = 0.0; x < 4000.0; x += x < 40.0 ? 0.0013 : 0.37) EXPECT_NEAR(fast::ln_i0(x), viterbi::ln_i0(x), 2e-5) << x;
    EXPECT_NEAR(fast::ln_i0(1e7), viterbi::ln_i0(1e7), 1e-5);
}

TEST(RunningSums, ScoreAnIntervalByOneSubtraction) {
    std::mt19937 rng(1);
    std::normal_distribution<double> value(0.0, 5.0);
    RunningSums sums;
    std::vector<double> l1;
    std::vector<Window> rise, fall;
    for (int k = 0; k < 400; ++k) {
        Window r{}, f{};
        for (int p = 0; p < kEdgeCount; ++p) {
            r[static_cast<std::size_t>(p)] = static_cast<float>(value(rng));
            f[static_cast<std::size_t>(p)] = static_cast<float>(value(rng));
        }
        l1.push_back(value(rng));
        rise.push_back(r);
        fall.push_back(f);
        sums.push(l1.back(), r, f);
    }
    ASSERT_EQ(sums.committed(), 400);
    for (const auto& [i, j] : {std::pair<int, int>{20, 26}, {20, 90}, {100, 133}, {200, 380}}) {
        // A mark from a rise claimed at i to a fall claimed at j, added up sample by sample.
        double direct = 0.0;
        for (int p = 0; p < kEdgeCount; ++p)
            direct += rise[static_cast<std::size_t>(i + kEdgeFirst + p)][static_cast<std::size_t>(p)];
        for (int k = i + kEdgeDone; k < j + kEdgeFirst; ++k) direct += l1[static_cast<std::size_t>(k)];
        for (int p = 0; p < kEdgeCount; ++p)
            direct += fall[static_cast<std::size_t>(j + kEdgeFirst + p)][static_cast<std::size_t>(p)];
        EXPECT_NEAR(sums.mark(i, j), direct, 1e-3) << i << " to " << j;
    }
}

TEST(RunningSums, ItsRingOutlastsTheLongestElement) {
    // sum(m) reaches back 8 192 samples, and a push never disturbs what an open element still reads.
    RunningSums sums;
    const Window none{};
    for (int k = 0; k < 20000; ++k) sums.push(1.0, none, none);
    EXPECT_DOUBLE_EQ(sums.sum(20000) - sums.sum(20000 - 4096), 4096.0);
}

// Made-up evidence for a keyed pattern: score(t) = gain x (2 key - 1) x t per sample under the template value t, the key as
// the filter delays it, 8 samples after its claimed edge.
struct Keyed {
    std::vector<int> key;  // by sample: the key as keyed, 0 or 1
    void add(int value, int samples) { key.insert(key.end(), static_cast<std::size_t>(samples), value); }
};

void feed(const Keyed& keyed, double gain, RunningSums& sums, Search* search = nullptr) {
    static constexpr std::array<double, kEdgeCount> kTemplate = {0.05, 0.21, 0.43, 0.66, 0.87, 0.98};
    for (std::size_t k = 0; k < keyed.key.size() + 40; ++k) {
        const std::size_t delayed = k >= 8 ? k - 8 : keyed.key.size();
        const int key = delayed < keyed.key.size() ? keyed.key[delayed] : 0;
        const double sign = gain * (2 * key - 1);
        Window rise{}, fall{};
        for (std::size_t p = 0; p < kEdgeCount; ++p) {
            rise[p] = static_cast<float>(sign * kTemplate[p]);
            fall[p] = static_cast<float>(sign * (1.0 - kTemplate[p]));
        }
        sums.push(sign, rise, fall);
        if (search != nullptr) search->advance();
    }
}

// The key for a text at dot samples per dot, exact timing, with lead samples before it and tail after; letter and word gaps
// on a time base of gap_dot samples (0: the dot).
Keyed keyed_text(const std::string& text, int dot, int lead, int tail, int gap_dot = 0) {
    Keyed keyed;
    if (gap_dot == 0) gap_dot = dot;
    keyed.add(0, lead);
    bool first_word = true;
    std::string word;
    const auto send_word = [&](const std::string& w) {
        if (!first_word) keyed.add(0, 7 * gap_dot);
        first_word = false;
        bool first_letter = true;
        for (const char c : w) {
            if (!first_letter) keyed.add(0, 3 * gap_dot);
            first_letter = false;
            const std::string letter(1, c);
            const std::string_view pattern = kz4ap::morse::encode(letter);
            bool first_mark = true;
            for (const char mark : pattern) {
                if (!first_mark) keyed.add(0, dot);
                first_mark = false;
                keyed.add(1, mark == '-' ? 3 * dot : dot);
            }
        }
    };
    for (const char c : text) {
        if (c == ' ') {
            send_word(word);
            word.clear();
        } else {
            word += c;
        }
    }
    send_word(word);
    keyed.add(0, tail);
    return keyed;
}

std::string text_of(const std::vector<DecodedSymbol>& symbols) {
    std::string out;
    for (const DecodedSymbol& s : symbols) out += s.text;
    return out;
}

SegmentalConfig small_config() {
    SegmentalConfig config;
    return config;
}

TEST(SegmentalSearch, ReadsMadeUpEvidenceAtAWeakAndAStrongLevel) {
    // gain 100 nats per sample is the level at which pruning on the evidence through j + 10 alone lost the right story, and
    // a window counted twice preferred split marks (stage 1 of the build).
    const SegmentalConfig config = small_config();
    const ModelTables model(config);
    for (const double gain : {2.0, 100.0}) {
        for (const int dot : {18, 11}) {
            RunningSums sums;
            Search search(config, model, sums);
            feed(keyed_text("PARIS PARIS", dot, 200, 700), gain, sums, &search);
            EXPECT_EQ(text_of(search.best_text()), "PARIS PARIS") << "gain " << gain << ", dot " << dot;
        }
    }
}

TEST(ModelTables, OpenSpacingIsAShareOfTheStorysDot) {
    SegmentalConfig config;
    config.open_spacing_dots = 0.5;
    const ModelTables half(config);
    config.open_spacing_dots = 0.0;
    const ModelTables none(config);
    const viterbi::ViterbiConfig& model = config.model;
    for (int f = 0; f < half.fists(); ++f) {
        const double dot_s = model.dot_min_ms * std::pow(model.speed_ratio, half.speed_of(f)) / 1000.0;
        EXPECT_EQ(half.open_spacing(f), static_cast<int>(0.5 * dot_s / model.sample_interval_s));
        EXPECT_EQ(none.open_spacing(f), 0);
    }
}

TEST(ModelTables, ALongGapsSurvivalStaysOptimistic) {
    const SegmentalConfig config;
    const ModelTables tables(config);
    // Past the fine lengths a bin reads its first length's value, and the bound never rises with the length.
    const int fist = tables.fists() - 1;
    const int last = tables.gap_hi(fist);
    ASSERT_GT(last, ModelTables::kSurvivalFineLengths + 32);
    const int bin = ModelTables::kSurvivalFineLengths + 16;
    for (int k = 1; k < 16; ++k) EXPECT_EQ(tables.gap_survival(fist, bin + k), tables.gap_survival(fist, bin));
    for (int length = 1; length <= last; ++length) EXPECT_LE(tables.gap_survival(fist, length), tables.gap_survival(fist, length - 1) + 1e-12);
    // A row holds what the accessors read.
    const ModelTables::FistRow& row = tables.row(fist);
    EXPECT_EQ(row.gap_hi, tables.gap_hi(fist));
    EXPECT_EQ(row.mark_hi, tables.mark_hi(fist));
    EXPECT_EQ(row.ends[3].hi, tables.range(3, fist).hi);
    EXPECT_TRUE(row.stretched == (tables.stretch_of(fist) > 1.0));
}

TEST(ModelTables, ABirthsEndsAreEverySpeedsAtItsLength) {
    const SegmentalConfig config;
    const ModelTables tables(config);
    const int length = tables.range(0, tables.first_fist(tables.speeds() / 2)).hi;
    std::size_t count = 0;
    double survive = -std::numeric_limits<double>::infinity();
    for (int speed = 0; speed < tables.speeds(); ++speed) {
        const int plain = tables.first_fist(speed);
        survive = std::max(survive, tables.mark_survival(plain, length));
        for (int label = 0; label < 2; ++label) {
            const ModelTables::Range& r = tables.range(label, plain);
            if (tables.child(0, label) >= 0 && length >= r.lo && length <= r.hi) ++count;
        }
    }
    const auto ends = tables.open_ends(length);
    EXPECT_EQ(ends.size(), count);
    EXPECT_DOUBLE_EQ(tables.open_survival(length), survive);
    for (const ModelTables::OpenEnd& end : ends) EXPECT_EQ(tables.speed_of(end.fist), end.speed);
}

TEST(ModelTables, AFistsGapsAreThoseOfTheSteppedSpeedAndItsMarksItsOwn) {
    const SegmentalConfig config;
    const ModelTables tables(config);
    const viterbi::ViterbiConfig& model = config.model;
    const double ratio = std::pow(model.speed_ratio, config.gap_stretch_speed_steps);
    EXPECT_EQ(tables.speeds(), model.speed_count);
    // A stretch's gap unit is inside the limit, and the first fist of a speed is standard spacing.
    for (int speed = 0; speed < tables.speeds(); ++speed) {
        const int first = tables.first_fist(speed);
        EXPECT_EQ(tables.speed_of(first), speed);
        EXPECT_DOUBLE_EQ(tables.stretch_of(first), 1.0);
        const double dot_s = model.dot_min_ms * std::pow(model.speed_ratio, speed) / 1000.0;
        for (int f = first + 1; f < tables.first_fist(speed + 1); ++f) {
            EXPECT_EQ(tables.speed_of(f), speed);
            EXPECT_NEAR(tables.stretch_of(f), tables.stretch_of(f - 1) * ratio, 1e-9);
            EXPECT_LE(tables.stretch_of(f) * dot_s, config.gap_unit_max_s);
        }
    }
    // At speed 3, stretch r^2: letter and word gaps as speed 13 reads them; marks and element gaps as speed 3's.
    const int fist = tables.first_fist(3) + 2;
    const int plain = tables.first_fist(13);
    const int own = tables.first_fist(3);
    for (const int kind : {3, 4}) {
        EXPECT_EQ(tables.range(kind, fist).lo, tables.range(kind, plain).lo);
        EXPECT_EQ(tables.range(kind, fist).hi, tables.range(kind, plain).hi);
        const int length = (tables.range(kind, plain).lo + tables.range(kind, plain).hi) / 2;
        EXPECT_DOUBLE_EQ(tables.log_end(kind, fist, length), tables.log_end(kind, plain, length));
    }
    for (const int kind : {0, 1, 2}) {
        EXPECT_EQ(tables.range(kind, fist).hi, tables.range(kind, own).hi);
        const int length = tables.range(kind, own).lo;
        EXPECT_DOUBLE_EQ(tables.log_end(kind, fist, length), tables.log_end(kind, own, length));
    }
    EXPECT_GT(tables.gap_hi(fist), tables.gap_hi(own));
    EXPECT_EQ(tables.mark_hi(fist), tables.mark_hi(own));
    // The charge: pi over six stretches.
    EXPECT_NEAR(tables.log_plain(), std::log(1.0 - config.gap_stretch_prior), 1e-12);
    EXPECT_NEAR(tables.log_stretched(), std::log(config.gap_stretch_prior / 6.0), 1e-12);
}

TEST(ModelTables, OneStretchLeavesTheOldTablesAndWhatCannotBeIsRefused) {
    SegmentalConfig config;
    config.gap_stretches = 1;
    const ModelTables tables(config);
    EXPECT_EQ(tables.fists(), config.model.speed_count);
    EXPECT_DOUBLE_EQ(tables.log_plain(), 0.0);
    SegmentalConfig bad = config;
    bad.gap_stretches = 0;
    EXPECT_THROW(ModelTables{bad}, std::invalid_argument);
    bad = config;
    bad.gap_stretch_prior = 1.0;
    EXPECT_THROW(ModelTables{bad}, std::invalid_argument);
    bad = SegmentalConfig{};
    bad.gap_stretch_prior = 0.0;
    EXPECT_THROW(ModelTables{bad}, std::invalid_argument);
    bad = SegmentalConfig{};
    bad.gap_unit_max_s = 0.0;
    EXPECT_THROW(ModelTables{bad}, std::invalid_argument);
}

TEST(SegmentalSearch, ReadsFarnsworthSpacingAsLettersAndWords) {
    // 25 WPM characters, the gaps on a time base 1.9 to 15 times the dot: text and word spaces exact. With one stretch the same
    // key reads a word space after letters (the old limitation), or no text like it.
    const SegmentalConfig config;
    SegmentalConfig plain;
    plain.gap_stretches = 1;
    const ModelTables tables(config);
    const ModelTables plain_tables(plain);
    const std::string text = "CQ DE W1AW PSE K TEST";
    for (const int gap_dot : {34, 57, 96, 150, 270}) {
        for (const double gain : {4.0, 100.0}) {
            const Keyed keyed = keyed_text(text, 18, 300, 400, gap_dot);
            RunningSums sums;
            Search search(config, tables, sums);
            feed(keyed, gain, sums, &search);
            EXPECT_EQ(text_of(search.best_text()), text) << gap_dot << " samples, gain " << gain;
            RunningSums plain_sums;
            Search plain_search(plain, plain_tables, plain_sums);
            feed(keyed, gain, plain_sums, &plain_search);
            EXPECT_NE(text_of(plain_search.best_text()), text) << gap_dot << " samples, gain " << gain;
        }
    }
}

TEST(SegmentalSearch, StandardSpacingPaysTheChargeOnceAndReadsTheSame) {
    SegmentalConfig plain;
    plain.gap_stretches = 1;
    const SegmentalConfig config;
    const ModelTables tables(config);
    const ModelTables plain_tables(plain);
    for (const std::string text : {"CQ CQ DE W1AW W1AW K", "TEST TEST", "TU", "E", "PARIS PARIS PARIS"}) {
        const Keyed keyed = keyed_text(text, 18, 300, 400);
        RunningSums a, b;
        Search with(config, tables, a), without(plain, plain_tables, b);
        feed(keyed, 8.0, a, &with);
        feed(keyed, 8.0, b, &without);
        EXPECT_EQ(text_of(with.best_text()), text);
        EXPECT_EQ(text_of(without.best_text()), text);
        // One over: the fist's choice, once.
        EXPECT_NEAR(with.best_score() - without.best_score(), std::log(1.0 - config.gap_stretch_prior), 1e-6) << text;
    }
}

TEST(SegmentalSearch, ClaimsEachEdgeAtItsSample) {
    // The times of a letter are its first key-down's and its last key-up's samples, within the template's own rounding.
    const SegmentalConfig config = small_config();
    const ModelTables model(config);
    RunningSums sums;
    Search search(config, model, sums);
    feed(keyed_text("K", 18, 300, 700), 20.0, sums, &search);
    const std::vector<DecodedSymbol> text = search.best_text();
    ASSERT_EQ(text.size(), 1u);
    const double delta = config.model.sample_interval_s;
    EXPECT_NEAR(text[0].start_s, 300 * delta, 1.5 * delta);
    EXPECT_NEAR(text[0].end_s, (300 + 9 * 18) * delta, 1.5 * delta);
}

TEST(SegmentalSearch, AWindowIsNeverCountedTwiceAcrossAnOversEnd) {
    // One long mark. An over's end followed at once by a birth would count the samples of its fall's and its rise's
    // windows twice; the tail of 6 samples forbids it, so one letter stays one letter.
    const SegmentalConfig config = small_config();
    const ModelTables model(config);
    RunningSums sums;
    Search search(config, model, sums);
    Keyed keyed;
    keyed.add(0, 300);
    keyed.add(1, 54);
    keyed.add(0, 700);
    feed(keyed, 100.0, sums, &search);
    // E or T: alone, its length says nothing of its speed.
    EXPECT_EQ(search.best_text().size(), 1u);
}

TEST(SegmentalSearch, KeepsNoTextOnNothing) {
    const SegmentalConfig config = small_config();
    const ModelTables model(config);
    RunningSums sums;
    Search search(config, model, sums);
    Keyed keyed;
    keyed.add(0, 4000);
    feed(keyed, 5.0, sums, &search);
    EXPECT_TRUE(search.best_text().empty());
    // Noise is cheap: nothing outlives the evidence against it.
    EXPECT_LT(search.live(), 3u);
    EXPECT_EQ(search.stats().boundaries, 4000 + 40 - kHorizon);
}

TEST(SegmentalSearch, RespectsTheCeiling) {
    SegmentalConfig config = small_config();
    config.ceiling = 20;
    config.beam_nats = 1000.0;
    const ModelTables model(config);
    RunningSums sums;
    Search search(config, model, sums);
    feed(keyed_text("CQ", 18, 100, 300), 1.0, sums, &search);
    EXPECT_LE(search.stats().largest_live, 20);
}

TEST(SegmentalSearch, MergesOnSpeedNodeAndKindAtAnEdge) {
    // Many stories end elements at one sample: what opens there is at most one element per (speed, node, kind), so the
    // elements opened per boundary stay below the state count however wide the beam.
    SegmentalConfig config = small_config();
    config.beam_nats = 100.0;
    config.ceiling = 1000000;
    config.gap_stretches = 1;  // the bound is the same with them; the wide beam is slow
    const ModelTables model(config);
    RunningSums sums;
    Search search(config, model, sums);
    feed(keyed_text("CQ", 18, 100, 100), 1.0, sums, &search);
    const double per_boundary = static_cast<double>(search.stats().opened) / static_cast<double>(search.stats().boundaries);
    EXPECT_LE(per_boundary, 2.0 * model.fists() * model.nodes() + 2.0);
}

SegmentalShared make_shared_parts(const SegmentalConfig& config) {
    return SegmentalShared(config, ChannelizerConfig{48000, 2048, 64, 150.0});
}

// Envelope samples of a steady key-down level through the shared level.
struct Simulated {
    std::vector<int> key;
    std::vector<double> e;
};

Simulated simulate(double s500_db, double sigma2_per_n0_hz, unsigned seed) {
    std::mt19937 rng(seed);
    std::normal_distribution<double> noise(0.0, std::sqrt(0.5));
    Simulated out;
    const double r = std::sqrt(std::pow(10.0, s500_db / 10.0) * 500.0 / sigma2_per_n0_hz);
    for (int k = 0; k < 6000; ++k) {
        const bool down = k >= 1000 && (k / 54) % 2 == 0;
        out.key.push_back(down ? 1 : 0);
        // sigma_y = 1: noise of variance 1 in the complex plane; independent samples are enough for this check
        const double re = (down ? r : 0.0) + noise(rng);
        const double im = noise(rng);
        out.e.push_back(std::sqrt(re * re + im * im));
    }
    return out;
}

TEST(LevelEvidence, ScoresKeyDownAboveKeyUpAndCommitsBehindItsLookAhead) {
    const SegmentalConfig config = small_config();
    const SegmentalShared shared = make_shared_parts(config);
    const Simulated sim = simulate(10.0, shared.output_noise.sigma2_per_n0_hz, 3);
    RunningSums sums;
    LevelEvidence evidence(config, shared.level_tables, sums);
    double previous = 0.0;
    double down_sum = 0.0, up_sum = 0.0;
    int down_count = 0, up_count = 0;
    std::int64_t last_committed = 0;
    for (std::size_t k = 0; k < sim.e.size(); ++k) {
        evidence.push(sim.e[k], 1.0);
        // Never ahead of the samples, and behind them by at most the look-ahead and the commit block.
        EXPECT_LE(sums.committed(), static_cast<std::int64_t>(k) + 1);
        EXPECT_GE(sums.committed(), static_cast<std::int64_t>(k) - 48 * 8 - 8);
        for (std::int64_t m = last_committed; m < sums.committed(); ++m) {
            const double l1 = sums.sum(m + 1) - previous;
            previous = sums.sum(m + 1);
            if (m > 1100 && sim.key[static_cast<std::size_t>(m)] == 1) {
                down_sum += l1;
                ++down_count;
            } else if (m > 1100) {
                up_sum += l1;
                ++up_count;
            }
        }
        last_committed = sums.committed();
    }
    // +10 dB S500: a^2 / sigma_y^2 = 21 (13 dB), w about 0.2: about +3 nats per key-down sample, well below 0 per key-up.
    EXPECT_GT(down_sum / down_count, 2.0);
    EXPECT_LT(up_sum / up_count, -1.0);
}

TEST(LevelEvidence, FindsTheEdgeInItsRiseAndFall) {
    // A mark's score is largest at the claim that matches the edge. The key here steps at once rather than through the
    // filter, so the claim that fits is about 8 samples before the step (the template's half point).
    const SegmentalConfig config = small_config();
    const SegmentalShared shared = make_shared_parts(config);
    const Simulated sim = simulate(20.0, shared.output_noise.sigma2_per_n0_hz, 4);
    RunningSums sums;
    LevelEvidence evidence(config, shared.level_tables, sums);
    for (const double e : sim.e) evidence.push(e, 1.0);
    evidence.finish();
    // The step up at k = 1 026 (a multiple of 54 x 2 above 1 000), the step down 54 samples later: read the ring's window.
    int best_i = 0;
    double best = -1e300;
    for (int i = 5940 - 2 * 54 - 30; i < 5940 - 2 * 54 + 30; ++i) {
        const double score = sums.rise(i) + sums.sum(5880) - sums.sum(i + kEdgeDone);
        if (score > best) {
            best = score;
            best_i = i;
        }
    }
    // 5 832 is a rise of the pattern (5 832 / 54 = 108, even); the template's half point is about 8 samples after its claim.
    EXPECT_NEAR(best_i, 5832 - 8, 3);
}

TEST(LevelEvidence, OneExtremeSampleLeavesTheChannelAlive) {
    // One sample of a carrier at +102 to +120 dB S500 in the noise: x_k(l) reaches 1e10 nats and more, where a float's half
    // step passes the 709.8 nats a double's exp can take (review F6). The forward message must not turn to NaN: a keyed
    // +10 dB S500 signal after it still scores key-down above key-up.
    const SegmentalConfig config = small_config();
    const SegmentalShared shared = make_shared_parts(config);
    const double sigma2_per_n0_hz = shared.output_noise.sigma2_per_n0_hz;
    for (const double burst_db : {102.0, 108.0, 120.0}) {
        const Simulated sim = simulate(10.0, sigma2_per_n0_hz, 5);
        RunningSums sums;
        LevelEvidence evidence(config, shared.level_tables, sums);
        const double burst = std::sqrt(std::pow(10.0, burst_db / 10.0) * 500.0 / sigma2_per_n0_hz);
        for (std::size_t k = 0; k < sim.e.size(); ++k) evidence.push(k == 500 ? burst : sim.e[k], 1.0);
        evidence.finish();
        double down_sum = 0.0, up_sum = 0.0;
        int down_count = 0, up_count = 0;
        for (int m = 1100; m < 5500; ++m) {
            const double l1 = sums.sum(m + 1) - sums.sum(m);
            if (sim.key[static_cast<std::size_t>(m)] == 1) {
                down_sum += l1;
                ++down_count;
            } else {
                up_sum += l1;
                ++up_count;
            }
        }
        EXPECT_GT(down_sum / down_count, 2.0) << burst_db << " dB S500";
        EXPECT_LT(up_sum / up_count, -1.0) << burst_db << " dB S500";
    }
}

TEST(ChannelFloor, ReadsTheNoiseLessItsMarginOnceItsStartIsIn) {
    SegmentalConfig config;
    ChannelFloor floor(config);
    std::mt19937 rng(11);
    std::exponential_distribution<double> power(1.0 / 4.0);  // e^2 of noise of 4 FS^2
    const double hiss = 0.5;                                 // FS^2: reads 9 dB low
    double last = 0.0;
    for (int k = 0; k < 3000; ++k) {
        last = floor.sigma2(std::sqrt(power(rng)), hiss);
        // 375 samples/s: nothing before 1 s; the first value at the first refresh after it (36 samples apart)
        if (k < 360) EXPECT_EQ(last, hiss) << k;
    }
    EXPECT_NEAR(last, 4.0 * std::pow(10.0, -0.3), 0.5);
    // Never below the hiss.
    EXPECT_EQ(floor.sigma2(0.0, 100.0), 100.0);
}

TEST(ChannelFloor, ForgetsWhatLeavesItsWindow) {
    SegmentalConfig config;
    ChannelFloor floor(config);
    std::mt19937 rng(12);
    std::exponential_distribution<double> loud(1.0 / 400.0), quiet(1.0 / 4.0);
    for (int k = 0; k < 1875; ++k) floor.sigma2(std::sqrt(loud(rng)), 1e-6);  // 5 s of 400 FS^2
    EXPECT_NEAR(floor.sigma2(std::sqrt(loud(rng)), 1e-6), 400.0 * std::pow(10.0, -0.3), 60.0);
    double last = 0.0;
    for (int k = 0; k < 1875 + 72; ++k) last = floor.sigma2(std::sqrt(quiet(rng)), 1e-6);  // 5 s and two refreshes of 4 FS^2
    EXPECT_NEAR(last, 4.0 * std::pow(10.0, -0.3), 0.6);
}

TEST(ChannelFloor, IsLeftOutAtAWindowOfZeroAndRefusesWhatCannotBe) {
    SegmentalConfig off;
    off.channel_floor_window_s = 0.0;
    ChannelFloor floor(off);
    for (int k = 0; k < 2000; ++k) EXPECT_EQ(floor.sigma2(100.0, 0.5), 0.5);
    const auto refuses = [](auto change) {
        SegmentalConfig config;
        change(config);
        EXPECT_THROW(ChannelFloor{config}, std::invalid_argument);
    };
    refuses([](SegmentalConfig& c) { c.channel_floor_window_s = -1.0; });
    refuses([](SegmentalConfig& c) { c.channel_floor_quantile = 0.0; });
    refuses([](SegmentalConfig& c) { c.channel_floor_quantile = 1.0; });
    refuses([](SegmentalConfig& c) { c.channel_floor_margin_db = std::nan(""); });
    refuses([](SegmentalConfig& c) { c.channel_floor_stride_s = 0.001; });  // not a whole number of samples
    refuses([](SegmentalConfig& c) { c.channel_floor_refresh_s = 0.0; });
    refuses([](SegmentalConfig& c) { c.channel_floor_start_s = 6.0; });     // longer than the window
}

TEST(SegmentalConfig, HasItsOwnSystematicFloorAndLeavesTheViterbiDecodersAlone) {
    EXPECT_EQ(SegmentalConfig{}.model.systematic_floor, 0.1);
    EXPECT_EQ(viterbi::ViterbiConfig{}.systematic_floor, 0.02);
}

TEST(FilterScale, KeepsTheFiltersAndDotsShapeInSamples) {
    const SegmentalConfig config;
    for (const int m : {2, 3, 4, 6}) {
        const SegmentalConfig scaled = at_filter_scale(config, m);
        EXPECT_DOUBLE_EQ(scaled.model.sample_interval_s, config.model.sample_interval_s * m);
        EXPECT_DOUBLE_EQ(scaled.model.front_dot_s, config.model.front_dot_s * m);
        EXPECT_DOUBLE_EQ(scaled.model.dot_min_ms, config.model.dot_min_ms * m);
        // The slowest speed stays within a step of where it was.
        const double slowest = scaled.model.dot_min_ms * std::pow(scaled.model.speed_ratio, scaled.model.speed_count - 1);
        const double before = config.model.dot_min_ms * std::pow(config.model.speed_ratio, config.model.speed_count - 1);
        EXPECT_NEAR(std::log(slowest / before), 0.0, std::log(config.model.speed_ratio));
        // In whole samples, and the look-ahead about what it was (a frame, at most).
        EXPECT_NO_THROW(viterbi::whole_count(scaled.level_frame_s, scaled.model.sample_interval_s, "frame", "samples"));
        EXPECT_NO_THROW(viterbi::whole_count(scaled.level_lookahead_s, scaled.level_frame_s, "look-ahead", "frames"));
        EXPECT_NO_THROW(viterbi::whole_count(scaled.channel_floor_stride_s, scaled.model.sample_interval_s, "stride", "samples"));
        EXPECT_NO_THROW(viterbi::whole_count(scaled.channel_floor_refresh_s, scaled.model.sample_interval_s, "refresh", "samples"));
        EXPECT_NEAR(scaled.level_lookahead_s, config.level_lookahead_s, scaled.level_frame_s);
    }
    EXPECT_THROW(at_filter_scale(config, 0), std::invalid_argument);
}

TEST(FilterScale, ItsEdgesLieInTheEvidencesWindow) {
    // The search reads an edge between samples 5 and 10 after its claim (evidence.hpp): before them the template must be about 0,
    // after them about 1, at every scale. At scale 1: 8e-4 and 1.0023.
    SegmentalConfig config;
    config.filter_scales = {1, 2, 3, 4, 6};
    const SegmentalShared shared(config, ChannelizerConfig{48000, 2048, 64, 150.0});
    ASSERT_EQ(shared.narrower.size(), 4u);
    for (const SegmentalParts& parts : shared.narrower) {
        std::cout << "measured: dot " << parts.config.model.front_dot_s * 1000.0 << " ms: sigma^2 / N0 "
                  << parts.output_noise.sigma2_per_n0_hz << " Hz, w " << parts.output_noise.w_strong << ", s_4 "
                  << parts.template_tables.s[4] << ", s_5 " << parts.template_tables.s[5] << ", s_10 " << parts.template_tables.s[10]
                  << ", s_11 " << parts.template_tables.s[11] << "\n";
        EXPECT_LT(std::fabs(parts.template_tables.s[4]), 0.03);
        EXPECT_NEAR(parts.template_tables.s[11], 1.0, 0.03);
        EXPECT_GT(parts.template_tables.s[5], 0.0);
        EXPECT_LT(parts.template_tables.s[10], 1.03);
    }
    // The filter's noise bandwidth narrows with the scale, to about its share.
    EXPECT_NEAR(shared.narrower[1].output_noise.sigma2_per_n0_hz, shared.output_noise.sigma2_per_n0_hz / 3.0,
                0.1 * shared.output_noise.sigma2_per_n0_hz / 3.0);
}

TEST(FilterScale, AFastStationDropsTheNarrowFilterAndAWideMarginKeepsItForASlowOne) {
    // A channel's samples at 1 500 samples/s, carrier keyed, +20 dB S500, noise at 0.02 FS over 48 kHz's density: past the drop
    // time a narrow filter that reads a 60 WPM station is gone. At 15 WPM it can read, so the drop is the margin's alone: with a wide
    // margin the widest and the narrow one still run (at the default 30 nats a clean station may drop it). The text is exact.
    const double n0_fs2_per_hz = 0.02 * 0.02 / 48000.0;
    const double amplitude = std::sqrt(100.0 * n0_fs2_per_hz * 500.0);
    std::mt19937 rng(11);
    std::normal_distribution<double> draw(0.0, std::sqrt(n0_fs2_per_hz * 1500.0 / 2.0));
    const std::string message = "CQ CQ DE W1AW W1AW K TEST 5NN 73 DE K1ABC K1ABC K";
    for (const int dot_samples : {30, 120}) {  // 20 ms, 80 ms
        std::string text = message;
        while (static_cast<int>(text.size()) * dot_samples * 8 < 1500 * 16) text += " " + message;
        const Keyed keyed = keyed_text(text, dot_samples, 300, 1500);
        SegmentalConfig config;
        if (dot_samples != 30) config.filter_scale_drop_margin_nats = 1e6;
        auto shared = std::make_shared<const SegmentalShared>(config, ChannelizerConfig{48000, 2048, 64, 150.0});
        SegmentalDecoder decoder(1500.0, shared);
        decoder.set_noise_density_fs2_per_hz(n0_fs2_per_hz);
        std::vector<Sample> block;
        std::vector<std::string> list;
        std::size_t k = 0;
        const auto take = [&](const DecodeUpdate& update) {
            for (const TextCorrection& correction : update.corrections) {
                list.resize(correction.from_index);
                for (const DecodedSymbol& symbol : correction.chars) list.push_back(symbol.text);
            }
            for (const DecodedSymbol& symbol : update.chars) list.push_back(symbol.text);
        };
        while (k < keyed.key.size()) {
            block.clear();
            const std::size_t first = k;
            for (; k < keyed.key.size() && block.size() < 32; ++k)
                block.emplace_back(static_cast<float>(amplitude * keyed.key[k] + draw(rng)), static_cast<float>(draw(rng)));
            take(decoder.process(block, static_cast<double>(first) / 1500.0));
        }
        if (dot_samples == 30) {
            EXPECT_EQ(decoder.streams_running(), 1u);
            EXPECT_EQ(decoder.leading_scale(), 1);
        } else {
            EXPECT_EQ(decoder.streams_running(), 2u);
        }
        take(decoder.flush());
        std::string read;
        for (const std::string& symbol : list) read += symbol;
        EXPECT_EQ(read, text) << dot_samples << " samples per dot";
    }
}

TEST(FilterScale, TheChoiceRunsOnTheChannelsAgeNotTheStreams) {
    // Review 4: a channel that opened after 10 s of stream had its narrower filter dropped within seconds. The same samples, a 60 WPM
    // station the narrower filter cannot read, from 0 s and from 100 s of stream: at 5 s of age both still run both streams,
    // at 16 s both have dropped it, and the text is the same.
    auto shared = std::make_shared<const SegmentalShared>(SegmentalConfig{}, ChannelizerConfig{48000, 2048, 64, 150.0});
    const double n0_fs2_per_hz = 0.02 * 0.02 / 48000.0;
    const double amplitude = std::sqrt(100.0 * n0_fs2_per_hz * 500.0);
    std::mt19937 rng(11);
    std::normal_distribution<double> draw(0.0, std::sqrt(n0_fs2_per_hz * 1500.0 / 2.0));
    std::string text = "CQ CQ DE W1AW W1AW K TEST 5NN 73 DE K1ABC K1ABC K";
    while (static_cast<int>(text.size()) * 30 * 8 < 1500 * 18) text += " CQ CQ DE W1AW W1AW K";
    const Keyed keyed = keyed_text(text, 30, 300, 1500);
    std::vector<Sample> samples;
    for (const double key : keyed.key) samples.emplace_back(static_cast<float>(amplitude * key + draw(rng)), static_cast<float>(draw(rng)));
    struct Result {
        std::size_t at_5_s = 0, at_16_s = 0;
        std::vector<std::string> published;
    };
    const auto run = [&](double t0_s) {
        Result result;
        SegmentalDecoder decoder(1500.0, shared);
        decoder.set_noise_density_fs2_per_hz(n0_fs2_per_hz);
        for (std::size_t k = 0; k < samples.size(); k += 32) {
            const std::size_t n = std::min<std::size_t>(32, samples.size() - k);
            (void)decoder.process(std::span<const Sample>(samples).subspan(k, n), t0_s + static_cast<double>(k) / 1500.0);
            const double age_s = static_cast<double>(k + n) / 1500.0;
            if (result.at_5_s == 0 && age_s >= 5.0) result.at_5_s = decoder.streams_running();
            if (result.at_16_s == 0 && age_s >= 16.0) result.at_16_s = decoder.streams_running();
        }
        (void)decoder.flush();
        const auto list = decoder.published_symbols();
        result.published.assign(list.begin(), list.end());
        return result;
    };
    const Result early = run(0.0);
    const Result late = run(100.0);
    EXPECT_EQ(early.at_5_s, 2u);
    EXPECT_EQ(late.at_5_s, 2u);
    EXPECT_EQ(early.at_16_s, 1u);
    EXPECT_EQ(late.at_16_s, 1u);
    EXPECT_EQ(late.published, early.published);
}

TEST(LeakFloor, ReadsTheBinsBesideTheChannelWeightedAndHeld) {
    const auto shared = std::make_shared<const SegmentalShared>(SegmentalConfig{}, ChannelizerConfig{48000, 2048, 64, 150.0});
    const double up = std::pow(10.0, SegmentalConfig{}.leak_floor_db / 10.0);
    ASSERT_EQ(shared->leak_first_bin, 3u);         // 60 Hz at 23.4375 Hz a bin
    ASSERT_EQ(shared->leak_response.size(), 15u);  // to 320 Hz
    ASSERT_EQ(shared->leak_hold_blocks, 9u);       // 0.2 s at 21.33 ms a block: 9.375, rounded
    EXPECT_NEAR(shared->leak_response[0], 1.0, 0.02);
    SegmentalDecoder decoder(1500.0, shared);
    const int center = 1024;
    std::vector<float> band(2048, 0.0f);
    const auto floor_with = [&](std::size_t bin) {
        std::fill(band.begin(), band.end(), 0.0f);
        band[bin] = 1.5f;  // a tone of amplitude 1 FS counts as 1 FS^2 over its bins; here all in one
        SegmentalDecoder fresh(1500.0, shared);
        fresh.set_band_spectrum(band, center);
        return fresh.leak_floor_fs2(1);
    };
    EXPECT_NEAR(floor_with(center + 5), up * shared->leak_response[5], 1e-12);
    EXPECT_NEAR(floor_with(center - 5), up * shared->leak_response[5], 1e-12);
    EXPECT_EQ(floor_with(center + 2), 0.0);   // the station's own
    EXPECT_EQ(floor_with(center - 15), 0.0);  // past the last bin
    // The peak holds for the blocks of its hold, and then leaves.
    std::fill(band.begin(), band.end(), 0.0f);
    band[static_cast<std::size_t>(center + 5)] = 1.5f;
    decoder.set_band_spectrum(band, center);
    const double peak = decoder.leak_floor_fs2(1);
    std::fill(band.begin(), band.end(), 0.0f);
    for (std::size_t i = 0; i + 1 < shared->leak_hold_blocks; ++i) decoder.set_band_spectrum(band, center);
    EXPECT_EQ(decoder.leak_floor_fs2(1), peak);
    decoder.set_band_spectrum(band, center);
    EXPECT_EQ(decoder.leak_floor_fs2(1), 0.0);
    // A channel at the span's edge reads what there is.
    SegmentalDecoder at_edge(1500.0, shared);
    at_edge.set_band_spectrum(band, 0);
    at_edge.set_band_spectrum(band, 2047);
    EXPECT_EQ(at_edge.leak_floor_fs2(1), 0.0);
}

TEST(LeakFloor, TheNarrowerFilterLetsLessThroughAtTheSameBinsOnAverage) {
    const SegmentalShared shared(SegmentalConfig{}, ChannelizerConfig{48000, 2048, 64, 150.0});
    ASSERT_EQ(shared.narrower.size(), 1u);
    double wide = 0.0, narrow = 0.0;
    for (std::size_t k = shared.leak_first_bin; k < shared.leak_response.size(); ++k) {
        wide += shared.leak_response[k];
        narrow += shared.narrower[0].leak_response[k];
    }
    EXPECT_LT(narrow, wide / 3.0);
}

TEST(LeakFloor, WhiteNoiseStaysUnderTheHissOnAverage) {
    // Derived: under a Hann window white noise reads P = 1.5 N0 df in a bin, so L = N0 df x the sum of the response over the bins read, against sigma^2 = N0 B.
    // The floor stands in for what the spectrum holds beside the channel; on noise alone it must not be what sets sigma^2.
    const SegmentalShared shared(SegmentalConfig{}, ChannelizerConfig{48000, 2048, 64, 150.0});
    const double df = 48000.0 / 2048.0;
    const double up = std::pow(10.0, SegmentalConfig{}.leak_floor_db / 10.0);
    const auto ratio = [&](const SegmentalParts& parts) {
        double sum = 0.0;
        for (std::size_t k = parts.leak_first_bin; k < parts.leak_response.size(); ++k) sum += 2.0 * parts.leak_response[k];
        return up * df * sum / parts.output_noise.sigma2_per_n0_hz;
    };
    std::cout << "derived: on white noise, the leak floor over N0 B: " << ratio(shared) << " at m = 1, " << ratio(shared.narrower[0]) << " at m = 3\n";
    EXPECT_LT(ratio(shared), 1.0);
    EXPECT_LT(ratio(shared.narrower[0]), 1.0);
}

TEST(LeakFloor, IsLeftOutAtAHoldOfZeroAndRefusesWhatCannotBe) {
    SegmentalConfig off;
    off.leak_hold_s = 0.0;
    const auto shared = std::make_shared<const SegmentalShared>(off, ChannelizerConfig{48000, 2048, 64, 150.0});
    EXPECT_TRUE(shared->leak_response.empty());
    SegmentalDecoder decoder(1500.0, shared);
    std::vector<float> band(2048, 1.0f);
    decoder.set_band_spectrum(band, 1024);
    EXPECT_EQ(decoder.leak_floor_fs2(1), 0.0);
    SegmentalConfig bad;
    bad.leak_max_offset_hz = bad.leak_min_offset_hz;
    EXPECT_THROW(SegmentalShared(bad, ChannelizerConfig{48000, 2048, 64, 150.0}), std::invalid_argument);
    SegmentalConfig negative;
    negative.leak_hold_s = -1.0;
    EXPECT_THROW(SegmentalShared(negative, ChannelizerConfig{48000, 2048, 64, 150.0}), std::invalid_argument);
}

TEST(LeakFloor, TheChannelsOwnKeyedCarrierIsNotANeighbor) {
    // text-level-1: a strong station's own keying, spread by the analyzer's window, read as a neighbor's leak (up to 40 dB re the
    // hiss at +57.78 dB S500). A bin counts only above rho_k times the own key-down power (derived: hard edges anywhere in the window).
    // The bound is a variant (leak_own 1), not the default.
    EXPECT_TRUE(SegmentalShared(SegmentalConfig{}, ChannelizerConfig{48000, 2048, 64, 150.0}).leak_own_bound.empty());
    SegmentalConfig on_config;
    on_config.leak_own = 1.0;
    const auto shared = std::make_shared<const SegmentalShared>(on_config, ChannelizerConfig{48000, 2048, 64, 150.0});
    ASSERT_EQ(shared->leak_own_bound.size(), 15u);
    ASSERT_EQ(shared->leak_own_hold_blocks, 47u);  // 1 s at 21.33 ms a block: 46.875, rounded
    const auto db = [](double x) { return 10.0 * std::log10(x); };
    std::cout << "derived: own splatter bound, dB re key-down, k = 3 ... 14:";
    for (std::size_t k = 3; k < 15; ++k) std::cout << " " << std::round(10.0 * db(shared->leak_own_bound[k])) / 10.0;
    std::cout << "\n";
    EXPECT_NEAR(db(shared->leak_own_bound[3]), -10.6, 0.2);
    EXPECT_NEAR(db(shared->leak_own_bound[14]), -25.1, 0.2);
    for (std::size_t k = 4; k < 15; ++k) EXPECT_LT(shared->leak_own_bound[k], shared->leak_own_bound[k - 1]);
    // A keyed carrier, hard edges, alone and with a steady neighbor 6 dB below it 100 Hz away, through the analyzer.
    SegmentalConfig off_config;
    off_config.leak_own = 0.0;
    const auto off = std::make_shared<const SegmentalShared>(off_config, ChannelizerConfig{48000, 2048, 64, 150.0});
    const double df = 48000.0 / 2048.0;
    const int c0 = 40;
    const double f_own = (c0 + 0.3) * df, f_neighbor = f_own + 100.0;
    for (const bool neighbor : {false, true}) {
        std::mt19937 rng(11);
        std::uniform_int_distribution<int> length(48, 14400);  // 1 ms to 300 ms at 48 kHz
        std::vector<Sample> x(48000 * 6);
        bool down = false;
        std::size_t next = 0;
        for (std::size_t n = 0; n < x.size(); ++n) {
            if (n == next) {
                down = !down;
                next = n + static_cast<std::size_t>(length(rng));
            }
            const double t = static_cast<double>(n) / 48000.0;
            std::complex<double> s = down ? 0.5 * std::polar(1.0, 2.0 * std::numbers::pi * f_own * t) : 0.0;
            if (neighbor) s += 0.25 * std::polar(1.0, 2.0 * std::numbers::pi * f_neighbor * t);
            x[n] = Sample(static_cast<float>(s.real()), static_cast<float>(s.imag()));
        }
        SpectrumAnalyzer analyzer(48000, 2048, 1024, false);
        SegmentalDecoder with(1500.0, shared), without(1500.0, off);
        double most_with = 0.0, most_without = 0.0, least_with = 1e300, least_without = 1e300;
        int frames = 0;
        for (const SpectrumFrame& frame : analyzer.push(x)) {
            with.set_band_spectrum(frame.power_fs2, c0 + 1024);
            without.set_band_spectrum(frame.power_fs2, c0 + 1024);
            if (++frames < 60) continue;  // past the first second's hold
            most_with = std::max(most_with, with.leak_floor_fs2(1));
            most_without = std::max(most_without, without.leak_floor_fs2(1));
            least_with = std::min(least_with, with.leak_floor_fs2(1));
            least_without = std::min(least_without, without.leak_floor_fs2(1));
        }
        ASSERT_GT(frames, 200);
        if (!neighbor) {
            // The own keying alone: what it reads without the bound is the defect; with it, next to nothing.
            EXPECT_GT(most_without, 0.0);
            EXPECT_LT(most_with, 1e-3 * most_without);
        } else {
            // A neighbor 6 dB below stands well above the bound (about -15 dB re the own key-down there): most of it still counts.
            EXPECT_GT(least_with, 0.5 * least_without);
        }
    }
}

TEST(LevelEvidence, NoiseAboveTheFloorLeavesTheCarriersLevelWhereItIs) {
    // text-level-1: sigma^2 stepping 30 dB above the floor (a leak floor's 0.2 s holds) under a steady carrier at +50 dB S500 re the
    // floor. On the floor (the default) the level the belief gives the samples stays; relative to sigma^2 (level_on_floor 0) it moves
    // by the step.
    const auto mean_level_db = [](const SegmentalConfig& config, bool stepped_samples) {
        const SegmentalShared shared = make_shared_parts(config);
        std::mt19937 rng(5);
        std::normal_distribution<double> noise(0.0, std::sqrt(0.5));
        const double r = std::sqrt(std::pow(10.0, 50.0 / 10.0) * 500.0 / shared.output_noise.sigma2_per_n0_hz);
        RunningSums sums;
        LevelEvidence evidence(config, shared.level_tables, sums);
        const auto stepped = [](std::int64_t k) { return k >= 1000 && (k / 75) % 2 == 1; };
        double total = 0.0;
        int count = 0;
        std::int64_t read = 0;
        for (std::int64_t k = 0; k < 4000; ++k) {
            const double re = r + noise(rng), im = noise(rng);
            evidence.push(std::sqrt(re * re + im * im), stepped(k) ? 1000.0 : 1.0, 1.0);
            for (; read < sums.committed(); ++read) {
                if (read < 1500 || read % 75 < 20 || read % 75 > 55 || stepped(read) != stepped_samples) continue;
                const SampleContext& context = sums.context(read);
                double level = 0.0, p = 0.0;
                for (int g = 0; g < context.count; ++g) {
                    level += context.probability[static_cast<std::size_t>(g)] * context.level[static_cast<std::size_t>(g)];
                    p += context.probability[static_cast<std::size_t>(g)];
                }
                total += config.level_min_db_s500 + config.level_step_db * level / p;
                ++count;
            }
        }
        return total / count;
    };
    SegmentalConfig on_floor = small_config();
    SegmentalConfig on_sigma = small_config();
    on_sigma.level_on_floor = 0.0;
    EXPECT_NEAR(mean_level_db(on_floor, false), 50.0, 2.0);
    EXPECT_NEAR(mean_level_db(on_floor, true), 50.0, 2.0);
    EXPECT_NEAR(mean_level_db(on_sigma, false), 50.0, 2.0);
    EXPECT_NEAR(mean_level_db(on_sigma, true), 20.0, 4.0);
    // The tables below the grid: level l at a shift d is the grid's level l - d.
    const SegmentalShared shared = make_shared_parts(on_floor);
    const LevelTables& t = shared.level_tables;
    EXPECT_EQ(t.below(), 40);  // 60 dB re the floor in 1.5 dB steps
    EXPECT_EQ(t.plain(30, 20).constant, t.plain(10, 0).constant);
    EXPECT_EQ(t.rise(2, 30, 20).i0_coeff, t.rise(2, 10).i0_coeff);
    EXPECT_LT(t.plain(0, 40).constant, 0.0f);
    SegmentalConfig bad = small_config();
    bad.level_on_floor = 0.5;
    EXPECT_THROW(make_shared_parts(bad), std::invalid_argument);
}

TEST(LevelEvidence, NoiseFarAboveTheFloorIsTakenAsItIsAndTheShiftStopsAtSixtyDecibels) {
    // Review of text-level-1. s = 10 log10(sigma^2 / F) / 1.5 dB, unrounded: below D = 40 steps (60 dB re F) the shift is the
    // nearest whole step and sigma^2 is taken at F x 10^(1.5 shift / 10); from D on the shift is D and sigma^2 is taken as it is.
    // So s in [39.5, 40) gives the shift 40 with sigma^2 still rounded, to 60 dB re F.
    const SegmentalConfig config = small_config();
    const SegmentalShared shared = make_shared_parts(config);
    ASSERT_EQ(shared.level_tables.below(), 40);
    const double floor = 2.0e-6;  // FS^2
    struct Case {
        double sigma2_db;  // dB re the floor
        int shift;
        double taken_db;  // the sigma^2 z is taken at, dB re the floor; NaN: sigma^2 as it is
    };
    const double as_is = std::numeric_limits<double>::quiet_NaN();
    const std::vector<Case> cases = {{0.0, 0, 0.0},    {1.0, 1, 1.5},    {2.2, 1, 1.5},   {2.3, 2, 3.0},   {58.4, 39, 58.5},
                                     {59.4, 40, 60.0}, {60.3, 40, as_is}, {70.0, 40, as_is}, {95.0, 40, as_is}};
    RunningSums sums;
    LevelEvidence evidence(config, shared.level_tables, sums);
    std::mt19937 rng(9);
    std::uniform_real_distribution<double> envelope(0.0, 0.01);
    std::vector<double> e(1200);
    std::int64_t read = 0;
    int checked = 0;
    const auto check = [&] {
        for (; read < sums.committed() && read < static_cast<std::int64_t>(e.size()); ++read) {
            const Case& c = cases[static_cast<std::size_t>(read) % cases.size()];
            const double sigma2 = floor * std::pow(10.0, c.sigma2_db / 10.0);
            const double taken = std::isnan(c.taken_db) ? sigma2 : floor * std::pow(10.0, c.shift * 1.5 / 10.0);
            const SampleContext& context = sums.context(read);
            EXPECT_EQ(context.shift, c.shift) << c.sigma2_db << " dB re the floor";
            EXPECT_DOUBLE_EQ(context.z, e[static_cast<std::size_t>(read)] / std::sqrt(taken)) << c.sigma2_db << " dB re the floor";
            ++checked;
        }
    };
    for (std::size_t k = 0; k < e.size(); ++k) {
        e[k] = envelope(rng);
        evidence.push(e[k], floor * std::pow(10.0, cases[k % cases.size()].sigma2_db / 10.0), floor);
        check();
    }
    evidence.finish();
    check();
    EXPECT_EQ(checked, 1200);
    // With level_on_floor 0 nothing shifts and z is taken at sigma^2 itself.
    SegmentalConfig on_sigma = small_config();
    on_sigma.level_on_floor = 0.0;
    const SegmentalShared shared0 = make_shared_parts(on_sigma);
    EXPECT_EQ(shared0.level_tables.below(), 0);
    RunningSums sums0;
    LevelEvidence evidence0(on_sigma, shared0.level_tables, sums0);
    for (int k = 0; k < 300; ++k) evidence0.push(0.004, floor * 1000.0, floor);
    evidence0.finish();
    ASSERT_GT(sums0.committed(), 100);
    EXPECT_EQ(sums0.context(100).shift, 0);
    EXPECT_DOUBLE_EQ(sums0.context(100).z, 0.004 / std::sqrt(floor * 1000.0));
}

TEST(LevelTables, BelowTheBottomReSigmaIsTheBottomOnlyWithLevelNoiseBottom) {
    // level_noise_bottom 1 (the default): a table level below -6 dB S500 re sigma^2 holds the bottom's term; 0: the tables go on
    // down, 1.5 dB a step, to -66 dB S500 re sigma^2. Above the bottom both are the same.
    SegmentalConfig clamp = small_config();
    SegmentalConfig open = small_config();
    open.level_noise_bottom = 0.0;
    const SegmentalShared with = make_shared_parts(clamp);
    const SegmentalShared without = make_shared_parts(open);
    const LevelTables& a = with.level_tables;
    const LevelTables& b = without.level_tables;
    ASSERT_EQ(a.below(), 40);
    ASSERT_EQ(b.below(), 40);
    for (const int shift : {1, 20, 40}) {
        // grid level 0 at a shift: below the bottom re sigma^2
        EXPECT_EQ(a.plain(0, shift).constant, a.plain(0).constant) << shift;
        EXPECT_EQ(a.plain(0, shift).i0_coeff, a.plain(0).i0_coeff) << shift;
        EXPECT_EQ(a.rise(3, 0, shift).inverse_v, a.rise(3, 0).inverse_v) << shift;
        EXPECT_LT(b.plain(0, shift).i0_coeff, b.plain(0).i0_coeff) << shift;  // a lower level: a smaller r
        EXPECT_NE(b.plain(0, shift).constant, b.plain(0).constant) << shift;
        // at or above the bottom re sigma^2: the same tables
        EXPECT_EQ(a.plain(shift, shift).constant, b.plain(shift, shift).constant) << shift;
        EXPECT_EQ(a.plain(shift + 5, shift).constant, b.plain(5).constant) << shift;
        EXPECT_EQ(a.short_term(true, 3, 4, shift + 2, shift).i0_coeff, b.short_term(true, 3, 4, 2).i0_coeff) << shift;
    }
    // r^2 at the lowest table level, -66 dB S500 re sigma^2: i0_coeff = 2 r / v with v about 1.
    const double r = std::sqrt(std::pow(10.0, -66.0 / 10.0) * 500.0 / without.output_noise.sigma2_per_n0_hz);
    EXPECT_NEAR(b.plain(0, 40).i0_coeff, 2.0 * r, 1e-6 * r);
}

TEST(LevelTables, RefusesTheSwitchesAtValuesOtherThanZeroOrOne) {
    // level_on_floor, level_noise_bottom and leak_own are switches: 0 or 1, nothing else.
    for (const double value : {0.5, 2.0, -1.0, std::numeric_limits<double>::quiet_NaN()}) {
        SegmentalConfig floor_switch = small_config();
        floor_switch.level_on_floor = value;
        EXPECT_THROW(make_shared_parts(floor_switch), std::invalid_argument) << "level_on_floor " << value;
        SegmentalConfig bottom = small_config();
        bottom.level_noise_bottom = value;
        EXPECT_THROW(make_shared_parts(bottom), std::invalid_argument) << "level_noise_bottom " << value;
        SegmentalConfig own = small_config();
        own.leak_own = value;
        EXPECT_THROW(make_shared_parts(own), std::invalid_argument) << "leak_own " << value;
    }
    for (const double value : {0.0, 1.0}) {
        SegmentalConfig c = small_config();
        c.level_on_floor = value;
        c.level_noise_bottom = value;
        c.leak_own = value;
        EXPECT_NO_THROW(make_shared_parts(c)) << value;
    }
}

namespace {

// The level's step on unit vectors: column `from` of the forward step's matrix, or of the backward step's.
std::vector<double> stepped(const LevelEvidence& evidence, int levels, int from, bool backward) {
    std::vector<double> x(static_cast<std::size_t>(levels), 0.0);
    x[static_cast<std::size_t>(from)] = 1.0;
    evidence.step_level(x.data(), backward);
    return x;
}

}  // namespace

TEST(LevelEvidence, TheLevelDependentWalksKernelIsNormalizedMatchesThePassesAndStepsBackByItsTranspose) {
    // Review of text-level-1: the walk variant (level_walk_high_nepers_per_sqrt_s above 0) replaces the three-tap passes by a dense
    // kernel K[to][from], each column a Gaussian over the levels folded at both ends.
    SegmentalConfig passes_config = small_config();
    SegmentalConfig equal_config = small_config();
    equal_config.level_walk_high_nepers_per_sqrt_s = equal_config.level_walk_nepers_per_sqrt_s;  // the same walk from every level
    SegmentalConfig rising_config = small_config();
    rising_config.level_walk_high_nepers_per_sqrt_s = 10.0;
    const SegmentalShared passes = make_shared_parts(passes_config);
    const SegmentalShared equal = make_shared_parts(equal_config);
    const SegmentalShared rising = make_shared_parts(rising_config);
    EXPECT_TRUE(passes.level_tables.walk_kernel().empty());
    const int n = rising.level_tables.levels();
    ASSERT_EQ(n, 61);
    const double step_nepers = 1.5 / 20.0 * std::log(10.0);
    const double frame_s = rising_config.level_frame_s;
    for (const SegmentalShared* shared : {&equal, &rising}) {
        const std::vector<double>& k = shared->level_tables.walk_kernel();
        ASSERT_EQ(k.size(), static_cast<std::size_t>(n * n));
        // Each column sums to 1 and is not negative.
        for (int from = 0; from < n; ++from) {
            double sum = 0.0;
            for (int to = 0; to < n; ++to) {
                EXPECT_GE(k[static_cast<std::size_t>(to * n + from)], 0.0);
                sum += k[static_cast<std::size_t>(to * n + from)];
            }
            EXPECT_NEAR(sum, 1.0, 1e-12) << "column " << from;
        }
    }
    // Its spread: at +15 dB S500 re the floor (level 14) the walk is 4.8 nepers per root second, at +30 dB (level 24, half way from
    // +20 to +40 dB) 7.4, at +45 dB (level 34) 10; variance (walk x sqrt(frame) / step)^2 in steps^2, far from both ends.
    const auto column_variance = [&](const std::vector<double>& k, int from) {
        double mean = 0.0, second = 0.0;
        for (int to = 0; to < n; ++to) {
            mean += to * k[static_cast<std::size_t>(to * n + from)];
            second += to * to * k[static_cast<std::size_t>(to * n + from)];
        }
        return second - mean * mean;
    };
    const auto walk_variance = [&](double walk) { return std::pow(walk * std::sqrt(frame_s) / step_nepers, 2.0); };
    EXPECT_NEAR(column_variance(rising.level_tables.walk_kernel(), 14), walk_variance(4.8), 0.01 * walk_variance(4.8));
    EXPECT_NEAR(column_variance(rising.level_tables.walk_kernel(), 24), walk_variance(7.4), 0.01 * walk_variance(7.4));
    EXPECT_NEAR(column_variance(rising.level_tables.walk_kernel(), 34), walk_variance(10.0), 0.01 * walk_variance(10.0));
    // With equal walks the kernel matches the 25 three-tap passes (the same variance, 16.48 steps^2; a Gaussian against a sum of
    // 25 three-point steps, whose excess kurtosis is -0.06), the jump included, at every level, ends too.
    RunningSums sums_a, sums_b, sums_c;
    const LevelEvidence by_passes(passes_config, passes.level_tables, sums_a);
    const LevelEvidence by_kernel(equal_config, equal.level_tables, sums_b);
    const LevelEvidence by_rising(rising_config, rising.level_tables, sums_c);
    double most = 0.0, most_l1 = 0.0;
    for (int from = 0; from < n; ++from) {
        const std::vector<double> a = stepped(by_passes, n, from, false);
        const std::vector<double> b = stepped(by_kernel, n, from, false);
        double l1 = 0.0;
        for (int to = 0; to < n; ++to) {
            most = std::max(most, std::abs(a[static_cast<std::size_t>(to)] - b[static_cast<std::size_t>(to)]));
            l1 += std::abs(a[static_cast<std::size_t>(to)] - b[static_cast<std::size_t>(to)]);
        }
        most_l1 = std::max(most_l1, l1);
    }
    std::cout << "measured: equal walks, kernel against the passes: largest difference " << most << " (the peak 0.098), largest L1 "
              << most_l1 << "\n";
    EXPECT_LT(most, 2e-3);
    EXPECT_LT(most_l1, 0.02);
    // The passes step the same both ways (symmetric); the kernel's backward step is the forward step's transpose, the jump included:
    // forward(e_i)[j] = backward(e_j)[i].
    for (const LevelEvidence* evidence : {&by_passes, &by_kernel, &by_rising}) {
        std::vector<std::vector<double>> forward, backward;
        for (int i = 0; i < n; ++i) {
            forward.push_back(stepped(*evidence, n, i, false));
            backward.push_back(stepped(*evidence, n, i, true));
        }
        double worst = 0.0;
        for (int i = 0; i < n; ++i)
            for (int j = 0; j < n; ++j)
                worst = std::max(worst, std::abs(forward[static_cast<std::size_t>(i)][static_cast<std::size_t>(j)] -
                                                 backward[static_cast<std::size_t>(j)][static_cast<std::size_t>(i)]));
        EXPECT_LT(worst, 1e-15);
    }
    // The rising kernel is not symmetric: the transpose matters there.
    double asymmetry = 0.0;
    for (int i = 0; i < n; ++i) {
        const std::vector<double> f = stepped(by_rising, n, i, false);
        for (int j = 0; j < n; ++j) asymmetry = std::max(asymmetry, std::abs(f[static_cast<std::size_t>(j)] - stepped(by_rising, n, j, false)[static_cast<std::size_t>(i)]));
    }
    EXPECT_GT(asymmetry, 1e-3);
}

TEST(LevelTables, RefusesAWalkThatRisesToALevelNotAboveWhereItRisesFrom) {
    // With the level-dependent walk on, level_walk_rise_to_db_s500 must lie above level_walk_rise_from_db_s500; the walk itself
    // must be finite and not negative. With it off (0) the levels are not read.
    for (const double to : {20.0, 19.0, std::numeric_limits<double>::quiet_NaN()}) {
        SegmentalConfig c = small_config();
        c.level_walk_high_nepers_per_sqrt_s = 10.0;
        c.level_walk_rise_from_db_s500 = 20.0;
        c.level_walk_rise_to_db_s500 = to;
        EXPECT_THROW(make_shared_parts(c), std::invalid_argument) << to;
    }
    for (const double walk : {-1.0, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()}) {
        SegmentalConfig c = small_config();
        c.level_walk_high_nepers_per_sqrt_s = walk;
        EXPECT_THROW(make_shared_parts(c), std::invalid_argument) << walk;
    }
    SegmentalConfig off = small_config();
    off.level_walk_rise_to_db_s500 = 10.0;  // below rise_from, but the walk is off
    EXPECT_NO_THROW(make_shared_parts(off));
    SegmentalConfig on = small_config();
    on.level_walk_high_nepers_per_sqrt_s = 10.0;
    on.level_walk_rise_to_db_s500 = 20.5;
    EXPECT_NO_THROW(make_shared_parts(on));
}

TEST(SegmentalShared, BuildsQuicklyAtTheRadiosHighestRate) {
    // Review 4: as merged 16.0 s at 768 kHz with three filters, on the pipeline's thread with the radio already running, longer than
    // the radio's ring of 5.46 s. The limit is what is short against the ring on a loaded machine.
    const auto start = std::chrono::steady_clock::now();
    const SegmentalShared shared(SegmentalConfig{}, ChannelizerConfig{768000, 32768, 64, 150.0});
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    std::cout << "measured: built at 768 kHz in " << seconds << " s\n";
    EXPECT_LT(seconds, 2.0);
}

TEST(SegmentalShared, TablesFromTheStandInAgreeWithTheFullRate) {
    // The tables are computed on the 48 kHz stand-in (segmental_tables_channel). Against the same tables computed at the full rate, at
    // the widest and the narrowest filter; 192 kHz for the narrow one to keep the test short.
    EXPECT_EQ(segmental_tables_channel(ChannelizerConfig{768000, 32768, 64, 150.0}).sample_rate, 48000);
    EXPECT_EQ(segmental_tables_channel(ChannelizerConfig{768000, 32768, 64, 150.0}).fft_size, 2048);
    EXPECT_EQ(segmental_tables_channel(ChannelizerConfig{44100, 2048, 64, 150.0}).sample_rate, 44100);
    const struct {
        int rate, fft, scale;
    } cases[] = {{768000, 32768, 1}, {192000, 8192, 3}};
    for (const auto& c : cases) {
        const SegmentalConfig config = at_filter_scale(SegmentalConfig{}, c.scale);
        const ChannelizerConfig channel{c.rate, c.fft, 64, 150.0};
        const SegmentalParts parts(config, channel);
        std::vector<double> taps = channel_filter_taps(channel);
        taps.insert(taps.begin(), static_cast<std::size_t>(std::lround(config.template_delay_s * c.rate)), 0.0);
        const viterbi::TemplateTables full =
            viterbi::make_template_tables(taps, c.rate, c.fft / 64, parts.front_taps, parts.channel_samples_per_sample,
                                          config.model.edge_rise_s, config.model.edge_rise_min_s, config.model.edge_rise_max_s);
        const viterbi::OutputNoise noise = viterbi::output_noise(channel_filter_taps(channel), c.rate, c.fft / 64, parts.front_taps,
                                                                 parts.channel_samples_per_sample);
        for (int j = 0; j < viterbi::TemplateTables::kSpan; ++j)
            EXPECT_NEAR(parts.template_tables.s[static_cast<std::size_t>(j)], full.s[static_cast<std::size_t>(j)], 1e-3) << c.rate << " j " << j;
        EXPECT_NEAR(parts.template_tables.half_point_s, full.half_point_s, 1e-5) << c.rate;
        EXPECT_NEAR(parts.output_noise.sigma2_per_n0_hz, noise.sigma2_per_n0_hz, 1e-4 * noise.sigma2_per_n0_hz) << c.rate;
        EXPECT_NEAR(parts.output_noise.w_strong, noise.w_strong, 1e-4 * noise.w_strong) << c.rate;
    }
}

namespace {
// dB re 0 Hz: the response of real taps at the channel's 1 500 samples/s, at a tone f_hz from the center.
double front_response_db(const std::vector<double>& taps, double f_hz) {
    double re = 0.0, im = 0.0;
    for (std::size_t i = 0; i < taps.size(); ++i) {
        const double phase = 2.0 * std::numbers::pi * f_hz * static_cast<double>(i) / 1500.0;
        re += taps[i] * std::cos(phase);
        im += taps[i] * std::sin(phase);
    }
    return 10.0 * std::log10(re * re + im * im);
}
}  // namespace

TEST(FilterScale, ANarrowerFiltersEdgesAreAQuarterOfItsDot) {
    const ChannelizerConfig channel{48000, 2048, 64, 150.0};
    const SegmentalConfig widest = at_filter_scale(SegmentalConfig{}, 1);
    EXPECT_EQ(widest.front_filter_rise_s, 0.0);
    const SegmentalParts wide(widest, channel);
    EXPECT_EQ(wide.front_taps.size(), 25u);  // the widest filter keeps the chain's own edges

    // E = T / 4 = 9 ms at T = 36 ms: 2 floor(45 ms x 1 500 /s / 2) + 1 taps (derived).
    const SegmentalConfig tapered = at_filter_scale(SegmentalConfig{}, 3);
    EXPECT_NEAR(tapered.front_filter_rise_s, 0.009 * 0.590334, 1e-8);
    const SegmentalParts parts(tapered, channel);
    ASSERT_EQ(parts.front_taps.size(), 67u);
    double sum = 0.0, squares = 0.0;
    for (const double t : parts.front_taps) {
        sum += t;
        squares += t * t;
    }
    EXPECT_NEAR(sum, 1.0, 1e-12);
    EXPECT_NEAR(squares * 1500.0, 26.04, 0.01);  // Hz: noise bandwidth, 26.96 Hz as it was
    // Half a step's response at 35.29 ms: 4.41 samples of 8 ms, and 3 samples of delay put it at 7.41, against the target's 7.84 (derived).
    EXPECT_NEAR(tapered.template_delay_s, 0.024, 1e-12);

    // With 0, the 4.235 ms of the widest filter, 61 taps, 26.96 Hz and 32 ms: what the change is measured against.
    SegmentalConfig plain;
    plain.front_taper_ratio = 0.0;
    const SegmentalConfig untapered = at_filter_scale(plain, 3);
    EXPECT_EQ(untapered.front_filter_rise_s, 0.0);
    EXPECT_NEAR(untapered.template_delay_s, 0.032, 1e-12);
    const SegmentalParts old_parts(untapered, channel);
    ASSERT_EQ(old_parts.front_taps.size(), 61u);

    // Where a strong neighbor comes in: dB re 0 Hz, derived from the taps. The band's middle gains, the main lobe hardly moves.
    const struct {
        double f_hz, tapered_db, untapered_db;
    } points[] = {{40.0, -14.3, -13.5}, {100.0, -28.9, -23.0}, {120.0, -35.7, -26.3}, {150.0, -47.8, -28.5}};
    for (const auto& p : points) {
        EXPECT_NEAR(front_response_db(parts.front_taps, p.f_hz), p.tapered_db, 0.1) << p.f_hz << " Hz";
        EXPECT_NEAR(front_response_db(old_parts.front_taps, p.f_hz), p.untapered_db, 0.1) << p.f_hz << " Hz";
    }
    // The longer pulse's edges must still lie in the evidence's window, samples 5 to 10 after a claim (evidence.hpp).
    const auto& s = parts.template_tables.s;
    std::cout << "measured: m = 3, E = 9 ms: s_4 " << s[4] << ", s_5 " << s[5] << ", s_10 " << s[10] << ", s_11 " << s[11] << "\n";
    EXPECT_LT(s[4], 0.05);
    EXPECT_GT(s[10], 0.9);
}

TEST(FilterScale, RefusesATaperOutsideZeroToOne) {
    SegmentalConfig config;
    config.front_taper_ratio = -0.1;
    EXPECT_THROW(at_filter_scale(config, 1), std::invalid_argument);
    EXPECT_THROW(at_filter_scale(config, 3), std::invalid_argument);
    config.front_taper_ratio = 1.5;
    EXPECT_THROW(at_filter_scale(config, 3), std::invalid_argument);
    config.front_taper_ratio = std::numeric_limits<double>::quiet_NaN();
    EXPECT_THROW(at_filter_scale(config, 3), std::invalid_argument);
}

TEST(FilterScale, ScalesThatCannotBeAreRefused) {
    const ChannelizerConfig channel{48000, 2048, 64, 150.0};
    SegmentalConfig config;
    config.filter_scales = {2, 3};
    EXPECT_THROW(SegmentalShared(config, channel), std::invalid_argument);
    config.filter_scales = {};
    EXPECT_THROW(SegmentalShared(config, channel), std::invalid_argument);
    config.filter_scales = {1, 3, 2};
    EXPECT_THROW(SegmentalShared(config, channel), std::invalid_argument);
}

TEST(SegmentalShared, RefusesAnotherChannelRate) {
    const SegmentalConfig config = small_config();
    auto shared = std::make_shared<const SegmentalShared>(config, ChannelizerConfig{48000, 2048, 64, 150.0});
    EXPECT_THROW(SegmentalDecoder(2000.0, shared), std::invalid_argument);
    EXPECT_NO_THROW(SegmentalDecoder(shared->channel_rate_hz, shared));
}

// A keyed text through the decoder, as a consumer's list keeps it (decoder.hpp): its symbols with their probabilities, and how many
// corrections changed no text (reason "confidence").
struct Published {
    std::vector<DecodedSymbol> symbols;
    int confidence_corrections = 0;
    std::string text() const {
        std::string t;
        for (const DecodedSymbol& s : symbols) t += s.text;
        return t;
    }
};

Published publish(const std::string& text, double snr_s500, double rival_margin_nats, unsigned seed) {
    const double n0_fs2_per_hz = 0.02 * 0.02 / 48000.0;
    const double amplitude = std::sqrt(std::pow(10.0, snr_s500 / 10.0) * n0_fs2_per_hz * 500.0);  // S500: dB SNR in 500 Hz
    std::mt19937 rng(seed);
    std::normal_distribution<double> draw(0.0, std::sqrt(n0_fs2_per_hz * 1500.0 / 2.0));
    const Keyed keyed = keyed_text(text, 45, 300, 1500);  // 30 ms dots: 40 WPM at 1 500 samples/s
    SegmentalConfig config;
    config.rival_margin_nats = rival_margin_nats;
    auto shared = std::make_shared<const SegmentalShared>(config, ChannelizerConfig{48000, 2048, 64, 150.0});
    SegmentalDecoder decoder(1500.0, shared);
    decoder.set_noise_density_fs2_per_hz(n0_fs2_per_hz);
    Published out;
    const auto take = [&](const DecodeUpdate& update) {
        for (const DecodedSymbol& s : update.chars) out.symbols.push_back(s);
        for (const TextCorrection& c : update.corrections) {
            out.confidence_corrections += c.reason == "confidence" ? 1 : 0;
            out.symbols.resize(std::min(out.symbols.size(), c.from_index));
            for (const DecodedSymbol& s : c.chars) out.symbols.push_back(s);
        }
    };
    std::vector<Sample> block;
    for (std::size_t k = 0; k < keyed.key.size();) {
        block.clear();
        const std::size_t first = k;
        for (; k < keyed.key.size() && block.size() < 32; ++k)
            block.emplace_back(static_cast<float>(amplitude * keyed.key[k] + draw(rng)), static_cast<float>(draw(rng)));
        take(decoder.process(block, static_cast<double>(first) / 1500.0));
    }
    take(decoder.flush());
    return out;
}

TEST(SegmentalMargins, ChangeNoTextAndSayLessForAWeakStation) {
    // The margins are kept beside the text and published as probabilities 1 / (1 + e^-margin): the text is the same with them and
    // without, every probability lies in [0.5, 1], without them all are 1, and a weak station's symbols are on average less sure
    // than a strong one's. A change of probability alone goes out as a correction of reason "confidence".
    const std::string text = "CQ CQ DE W1AW W1AW K TEST 5NN 73 DE K1ABC K1ABC K";
    const auto mean = [](const Published& p) {
        double sum = 0.0;
        for (const DecodedSymbol& s : p.symbols) sum += s.probability;
        return sum / static_cast<double>(std::max<std::size_t>(1, p.symbols.size()));
    };
    for (const double snr : {20.0, 3.0}) {
        const Published with = publish(text, snr, 16.0, 7);
        const Published without = publish(text, snr, 0.0, 7);
        EXPECT_EQ(with.text(), without.text()) << snr << " dB S500";
        for (const DecodedSymbol& s : with.symbols) {
            EXPECT_GE(s.probability, 0.5f);
            EXPECT_LT(s.probability, 1.0f);  // no rival: the ceiling, 16 nats, read through a float as 15.94 nats; 1 says "none kept"
        }
        for (const DecodedSymbol& s : without.symbols) EXPECT_EQ(s.probability, 1.0f);
        EXPECT_EQ(without.confidence_corrections, 0);
    }
    const Published strong = publish(text, 20.0, 16.0, 7);
    const Published weak = publish(text, 3.0, 16.0, 7);
    EXPECT_EQ(strong.text(), text);
    EXPECT_GT(mean(strong), mean(weak));
    EXPECT_GT(weak.confidence_corrections, 0);
    // Deficits are recorded as they are: some of the weak station's symbols lie strictly between a tie (0.5) and the ceiling, and most of
    // the strong one's letters are at the ceiling.
    const auto between = std::count_if(weak.symbols.begin(), weak.symbols.end(), [](const DecodedSymbol& s) { return s.probability > 0.55f && s.probability < 0.9999f; });
    EXPECT_GT(between, 0);
    const auto letters = std::count_if(strong.symbols.begin(), strong.symbols.end(), [](const DecodedSymbol& s) { return s.text != " "; });
    const auto ceiling = std::count_if(strong.symbols.begin(), strong.symbols.end(), [](const DecodedSymbol& s) { return s.text != " " && s.probability > 0.99999f; });
    EXPECT_GT(2 * ceiling, letters);
}

TEST(SegmentalSpeed, IsTheTextsAndAJunkLetterInTheSilenceAfterLeavesIt) {
    // Backlog, "A long row's speed reads low at its end": on the owner's recording K4TZ's row ended at 5.7 WPM, the speed of a slow
    // story that read one long burst in the silence after the station as E, the last text the track published. A station at 40 WPM
    // (30 ms dots at 1 500 samples/s), +20 dB S500, then 3 s of key-up, one 180 ms burst at the same level, 3 s of key-up: the burst
    // goes out as a letter, and the speed every update carries from the first letter on, the last included, is the text's, within
    // 10% of 40 WPM.
    const double n0_fs2_per_hz = 0.02 * 0.02 / 48000.0;
    const double amplitude = std::sqrt(100.0 * n0_fs2_per_hz * 500.0);
    std::mt19937 rng(5);
    std::normal_distribution<double> draw(0.0, std::sqrt(n0_fs2_per_hz * 1500.0 / 2.0));
    const std::string text = "CQ CQ DE W1AW W1AW K";
    Keyed keyed = keyed_text(text, 45, 300, 4500);
    keyed.add(1, 270);
    keyed.add(0, 4500);
    auto shared = std::make_shared<const SegmentalShared>(SegmentalConfig{}, ChannelizerConfig{48000, 2048, 64, 150.0});
    SegmentalDecoder decoder(1500.0, shared);
    decoder.set_noise_density_fs2_per_hz(n0_fs2_per_hz);
    std::vector<std::string> list;
    std::vector<float> speeds;  // WPM: each update's that carries text
    const auto take = [&](const DecodeUpdate& update) {
        for (const DecodedSymbol& s : update.chars) list.push_back(s.text);
        for (const TextCorrection& c : update.corrections) {
            list.resize(std::min(list.size(), c.from_index));
            for (const DecodedSymbol& s : c.chars) list.push_back(s.text);
        }
        if (!update.chars.empty() || !update.corrections.empty()) speeds.push_back(update.wpm);
    };
    std::vector<Sample> block;
    for (std::size_t k = 0; k < keyed.key.size();) {
        block.clear();
        const std::size_t first = k;
        for (; k < keyed.key.size() && block.size() < 32; ++k)
            block.emplace_back(static_cast<float>(amplitude * keyed.key[k] + draw(rng)), static_cast<float>(draw(rng)));
        take(decoder.process(block, static_cast<double>(first) / 1500.0));
    }
    take(decoder.flush());
    std::string read;
    for (const std::string& s : list) read += s;
    ASSERT_GT(read.size(), text.size()) << read;  // the burst read as text after the message
    EXPECT_EQ(read.substr(0, text.size()), text);
    ASSERT_FALSE(speeds.empty());
    for (const float wpm : speeds) EXPECT_NEAR(wpm, 40.0f, 4.0f) << read;
}

TEST(SegmentalMargins, ACapAFloatCannotHoldIsRefusedAndTheDefaultIsOff) {
    EXPECT_EQ(SegmentalConfig{}.rival_margin_nats, 0.0);
    SegmentalConfig config;
    for (const double bad : {-1.0, 16.7, 17.4, std::numeric_limits<double>::infinity()}) {
        config.rival_margin_nats = bad;
        EXPECT_THROW(SegmentalShared(config, ChannelizerConfig{48000, 2048, 64, 150.0}), std::invalid_argument) << bad;
    }
    config.rival_margin_nats = 16.6;
    EXPECT_NO_THROW(SegmentalShared(config, ChannelizerConfig{48000, 2048, 64, 150.0}));
}

// --- text-fast-1: the channel's tail and the walk set by the Doppler spread ---

TEST(RunningSums, ATailIsSummedFromTheFallsWindowOn) {
    // tail(i, n) is the sum of the tail's values at positions 0 ... n - 1 after a fall claimed at i, position q read at sample
    // i + 11 + q; past the positions pushed it reads them all.
    RunningSums sums;
    const Window none{};
    constexpr int kPositions = 9;
    const auto value = [](std::int64_t k, int q) { return 0.01 * static_cast<double>(k % 97) - 0.3 * q; };
    for (std::int64_t k = 0; k < 3000; ++k) {
        double tail[kTailMax];
        for (int q = 0; q < kPositions; ++q) tail[q] = value(k, q);
        sums.push(0.0, none, none, {}, tail, kPositions);
    }
    for (const std::int64_t i : {2100, 2500, 2900}) {  // n below the positions filled is read for the last 1 024 claims
        double direct = 0.0;
        for (int n = 0; n <= kPositions + 3; ++n) {
            EXPECT_NEAR(sums.tail(i, n), direct, 1e-12) << i << " " << n;
            if (n < kPositions) direct += value(i + kEdgeDone + n, n);
        }
        EXPECT_EQ(sums.tail_filled(i), kPositions);
    }
    // No tail pushed: nothing to add.
    RunningSums clean;
    for (int k = 0; k < 100; ++k) clean.push(1.0, none, none);
    EXPECT_EQ(clean.tail(20, 9), 0.0);
    EXPECT_EQ(clean.tail_filled(20), 0);
}

// Envelope power through the estimator: marks of `mark` samples at `level` re the floor (1), gaps of `gap`, and after each fall a
// zero-mean complex Gaussian of `tail` x level for `tail_samples`; noise everywhere.
EchoShape learn_tail(double level, double tail, int tail_samples, unsigned seed) {
    const SegmentalConfig config;
    const SegmentalShared shared(config, ChannelizerConfig{48000, 2048, 64, 150.0});
    EchoEstimator estimator(config, shared.template_tables);
    std::mt19937 rng(seed);
    std::normal_distribution<double> unit(0.0, std::sqrt(0.5));
    const auto gaussian = [&](double power) {
        const std::complex<double> z(unit(rng), unit(rng));
        return std::norm(z) * power;
    };
    for (int run = 0; run < 400; ++run) {
        const int mark = run % 3 == 0 ? 39 : 13, gap = run % 5 == 0 ? 39 : 13;  // 35 WPM's dot and dash, element and letter gaps
        for (int k = 0; k < mark; ++k) {
            const std::complex<double> z(std::sqrt(level) + unit(rng), unit(rng));
            estimator.push(std::norm(z), 1.0);
        }
        for (int k = 0; k < gap; ++k) estimator.push(gaussian(1.0) + (k < tail_samples ? gaussian(tail * level) : 0.0), 1.0);
    }
    return estimator.shape();
}

TEST(EchoEstimator, LearnsATailAfterTheFallAndNoneWhereThereIsNone) {
    // +30 dB re the floor, a tail at -10 dB re the mark for 12 samples (32 ms) after an abrupt fall. The last -6 dB sample is the
    // mark's last, which the estimator takes as the fall template's half point, h samples after the claim (7 at the widest filter):
    // position 11 + q reads the sample 4 + q after it, so the tail's 12 samples give 8 positions. They read about 0.06, -2 dB re the
    // truth: 8% of a Rayleigh tail's samples stand above -6 dB re the mark and move the last -6 dB sample into the tail (measured).
    const EchoShape with = learn_tail(1000.0, 0.1, 12, 21);
    ASSERT_TRUE(with.on);
    ASSERT_GE(with.tail.size(), 7u);
    ASSERT_LE(with.tail.size(), 10u);
    for (std::size_t q = 0; q + 1 < with.tail.size() && q < 7; ++q) EXPECT_NEAR(with.tail[q], 0.065, 0.025) << q;
    for (const double g : with.in_fall) EXPECT_DOUBLE_EQ(g, with.tail[0]);
    // A clean key leaves no tail; nor does a station under 20 dB re the floor, whatever follows its marks (echo_min_db).
    EXPECT_FALSE(learn_tail(1000.0, 0.0, 0, 22).on);
    EXPECT_FALSE(learn_tail(60.0, 0.1, 12, 23).on);
}

TEST(LevelEvidence, ShortFramesJoinedIntoBlocksScoreAsTheLongFrame) {
    // While the walk is slow the frames of 10.67 ms are joined two by two into the 21.33 ms frame: the evidence is the old one,
    // to the bit.
    SegmentalConfig split;
    SegmentalConfig whole;
    whole.level_fast_frame_s = 0.0;
    const SegmentalShared shared = make_shared_parts(split);
    const Simulated sim = simulate(15.0, shared.output_noise.sigma2_per_n0_hz, 31);
    RunningSums a, b;
    LevelEvidence ea(split, shared.level_tables, a), eb(whole, shared.level_tables, b);
    for (const double e : sim.e) {
        ea.push(e, 1.0);
        eb.push(e, 1.0);
    }
    ea.finish();
    eb.finish();
    ASSERT_EQ(a.committed(), b.committed());
    for (std::int64_t m = a.committed() - 4000; m <= a.committed(); ++m) ASSERT_EQ(a.sum(m), b.sum(m)) << m;
    for (std::int64_t m = a.committed() - 4000; m < a.committed() - kEdgeDone; ++m) {
        ASSERT_EQ(a.rise(m), b.rise(m)) << m;
        ASSERT_EQ(a.fall(m), b.fall(m)) << m;
    }
}

// A complex fading gain with a Gaussian Doppler spectrum of f_D = 2 sigma_D, unit mean power, at `rate` samples/s: white complex
// noise through h(t) = exp(-4 pi^2 sigma_D^2 t^2), whose |H(f)|^2 is the spectrum (derived).
std::vector<std::complex<double>> fading_gain(double f_d_hz, double rate, std::size_t count, unsigned seed) {
    std::mt19937 rng(seed);
    std::normal_distribution<double> unit(0.0, std::sqrt(0.5));
    const double sigma = f_d_hz / 2.0;
    const double width_s = 1.0 / (2.0 * std::sqrt(2.0) * std::numbers::pi * sigma);
    const int half = static_cast<int>(std::ceil(4.0 * width_s * rate));
    std::vector<double> h(static_cast<std::size_t>(2 * half + 1));
    double energy = 0.0;
    for (int n = -half; n <= half; ++n) {
        const double t = n / rate;
        h[static_cast<std::size_t>(n + half)] = std::exp(-4.0 * std::numbers::pi * std::numbers::pi * sigma * sigma * t * t);
        energy += h[static_cast<std::size_t>(n + half)] * h[static_cast<std::size_t>(n + half)];
    }
    std::vector<std::complex<double>> white(count + h.size());
    for (auto& w : white) w = {unit(rng), unit(rng)};
    std::vector<std::complex<double>> gain(count);
    for (std::size_t k = 0; k < count; ++k) {
        std::complex<double> acc = 0.0;
        for (std::size_t j = 0; j < h.size(); ++j) acc += h[j] * white[k + j];
        gain[k] = acc / std::sqrt(energy);
    }
    return gain;
}

// The widest filter's noise correlation at lags 0 ... 6 (OutputNoise::rho), for the estimator's tests.
std::array<double, 7> widest_noise_rho() { return make_shared_parts(SegmentalConfig{}).output_noise.rho; }

// Marks of 100 ms and gaps of 60 ms at +30 dB re the floor through gain, plus other (another carrier, or none), at 375 samples/s
// (the widest filter's), white noise of 1 (FS^2) a sample, into the estimator; runs times over.
void key_into(DopplerEstimator& estimator, const std::vector<std::complex<double>>& gain, const std::vector<std::complex<double>>& other,
              unsigned seed, int times = 1) {
    std::mt19937 rng(seed);
    std::normal_distribution<double> unit(0.0, std::sqrt(0.5));
    for (int r = 0; r < times; ++r)
        for (std::size_t k = 0; k < gain.size(); ++k) {
            const bool down = k % 60 < 38;
            const std::complex<double> y = (down ? std::sqrt(1000.0) * gain[k] + other[k] : 0.0) + std::complex<double>(unit(rng), unit(rng));
            estimator.push(y, 1.0);
        }
}

TEST(DopplerEstimator, ReadsAFadingCarriersSpreadAndShapeAndNoneOnASteadyOne) {
    // f_D = 2 sigma_D read at 21.33 ms; a Gaussian spread keeps its shape at the five shorter lags and reads run by run at 0.6 to 0.8 of
    // the vector sum's (measured); a steady carrier reads none.
    const SegmentalConfig config;
    const std::size_t count = 375 * 120;
    const std::vector<std::complex<double>> none(count, 0.0);
    for (const double f_d : {0.0, 3.0, 5.0, 10.0, 20.0, 30.0}) {
        DopplerEstimator estimator(config, widest_noise_rho());
        key_into(estimator, f_d > 0.0 ? fading_gain(f_d, 375.0, count, 41) : std::vector<std::complex<double>>(count, 1.0), none, 42);
        ASSERT_GE(estimator.runs(), config.doppler_min_runs) << f_d;
        std::cout << "f_D " << f_d << " Hz: reads " << estimator.spread_hz() << " Hz, run by run " << estimator.runs_hz() << " Hz; shape at 3 to 7 samples";
        for (int tau = 3; tau <= 7; ++tau) std::cout << " " << estimator.shape(tau);
        std::cout << "\n";
        if (f_d == 0.0) {
            EXPECT_LT(estimator.spread_hz(), 1.0);
            continue;
        }
        EXPECT_NEAR(estimator.spread_hz(), f_d, 0.25 * f_d) << f_d;
        if (f_d <= 20.0) EXPECT_TRUE(estimator.shape_ok(config.doppler_shape_lo, config.doppler_shape_hi)) << f_d;
        EXPECT_GE(estimator.runs_hz(), config.doppler_min_runs_ratio * estimator.spread_hz()) << f_d;
        EXPECT_NEAR(estimator.level_db(), 28.0, 3.0) << f_d;  // a Rayleigh mark's median power is -1.6 dB re its mean
    }
}

TEST(DopplerEstimator, ANeighborsBeatIsNotASpreadFromThirtyHertzOut) {
    // A steady carrier at +30 dB re the floor with a steady neighbor keyed with it, df = 30 to 120 Hz away, at 0.05 and 0.25 of its
    // power (before any filter; the widest filter takes 18.8 dB off at 100 Hz): it may read as a spread at 21.33 ms, but never with a
    // spread's shape at all five shorter lags (ln |rho| ~ r (cos 2 pi df tau - 1); review of text-fast-1: one ratio aliased every
    // 93.75 Hz).
    const SegmentalConfig config;
    const std::size_t count = 375 * 120;
    const std::vector<std::complex<double>> steady(count, 1.0);
    for (const double r : {0.05, 0.25})
        for (double df = 30.0; df <= 120.0; df += 2.5) {
            std::vector<std::complex<double>> neighbor(count);
            for (std::size_t k = 0; k < count; ++k)
                neighbor[k] = std::sqrt(1000.0 * r) * std::polar(1.0, 2.0 * std::numbers::pi * df * static_cast<double>(k) / 375.0);
            DopplerEstimator estimator(config, widest_noise_rho());
            key_into(estimator, steady, neighbor, 43);
            const bool fast = estimator.spread_hz() >= config.doppler_fast_hz && estimator.shape_ok(config.doppler_shape_lo, config.doppler_shape_hi);
            EXPECT_FALSE(fast) << "r " << r << ", " << df << " Hz: reads " << estimator.spread_hz() << " Hz";
        }
}

TEST(DopplerEstimator, StationsTakingTurnsAFewHertzApartAreNotASpread) {
    // A run station and his caller in one channel, steady carriers 4 Hz apart, +30 dB re the floor, keying in turn: summed as vectors
    // the runs read a spread, but each run is one station, so its products turn alike and its own magnitude reads none.
    const SegmentalConfig config;
    const std::size_t count = 375 * 120;
    std::vector<std::complex<double>> turns(count);
    for (std::size_t k = 0; k < count; ++k)
        turns[k] = std::polar(1.0, 2.0 * std::numbers::pi * ((k / 600) % 2 == 0 ? 2.0 : -2.0) * static_cast<double>(k) / 375.0);  // 1.6 s overs
    DopplerEstimator estimator(config, widest_noise_rho());
    key_into(estimator, turns, std::vector<std::complex<double>>(count, 0.0), 44);
    EXPECT_GT(estimator.spread_hz(), config.doppler_fast_hz);
    EXPECT_LT(estimator.runs_hz(), config.doppler_min_runs_ratio * estimator.spread_hz());
}

TEST(DopplerEstimator, KeepsReadingAfterItsMemoryIsFull) {
    // The memory's weight converges to 1 / (1 - e^(-1/100)) = 100.5 runs and stops changing in a double after about 3 250 runs; the
    // estimate must still say it changed at every run taken (review of text-fast-1): 6 000 runs of a steady carrier, then 600 of a
    // 10 Hz spread, which it must follow.
    const SegmentalConfig config;
    DopplerEstimator estimator(config, widest_noise_rho());
    const std::size_t count = 60 * 1000;  // 1 000 runs
    key_into(estimator, std::vector<std::complex<double>>(count, 1.0), std::vector<std::complex<double>>(count, 0.0), 45, 6);
    EXPECT_GE(estimator.taken(), 6000);
    EXPECT_LT(estimator.spread_hz(), 1.0);
    std::mt19937 rng(46);
    std::normal_distribution<double> unit(0.0, std::sqrt(0.5));
    const auto gain = fading_gain(10.0, 375.0, 60 * 600, 47);
    int updates = 0;
    for (std::size_t k = 0; k < gain.size(); ++k) {
        const bool down = k % 60 < 38;
        updates += estimator.push((down ? std::sqrt(1000.0) * gain[k] : 0.0) + std::complex<double>(unit(rng), unit(rng)), 1.0) ? 1 : 0;
    }
    EXPECT_GE(updates, 590);
    EXPECT_NEAR(estimator.spread_hz(), 10.0, 3.0);
}

// A channel's samples at 1 500 samples/s: a text keyed at 35 WPM (51 samples a dot), +30 dB S500, through a fading gain of f_D (or none),
// with another path's, a zero-mean complex Gaussian fading at 5 Hz of `tail` x the mark's power, for `tail_samples` after each fall;
// then the decoder's text, its walk and its tail.
struct Channel35 {
    std::string text;
    std::string read;
    bool walk_fast = false;
    EchoShape echo;
};

Channel35 decode_35_wpm(const SegmentalConfig& config, double f_d_hz, double tail, int tail_samples, unsigned seed) {
    const double n0_fs2_per_hz = 0.02 * 0.02 / 48000.0;
    const double amplitude = std::sqrt(1000.0 * n0_fs2_per_hz * 500.0);
    std::mt19937 rng(seed);
    std::normal_distribution<double> draw(0.0, std::sqrt(n0_fs2_per_hz * 1500.0 / 2.0));
    Channel35 out;
    const std::string message = "THE QUICK BROWN FOX JUMPS OVER THE LAZY DOG 0123456789";
    out.text = message;
    while (static_cast<int>(out.text.size()) * 51 * 8 < 1500 * 70) out.text += " " + message;
    const Keyed keyed = keyed_text(out.text, 51, 600, 1500);
    const auto gain = f_d_hz > 0.0 ? fading_gain(f_d_hz, 1500.0, keyed.key.size(), seed + 1)
                                   : std::vector<std::complex<double>>(keyed.key.size(), 1.0);
    auto shared = std::make_shared<const SegmentalShared>(config, ChannelizerConfig{48000, 2048, 64, 150.0});
    SegmentalDecoder decoder(1500.0, shared);
    decoder.set_noise_density_fs2_per_hz(n0_fs2_per_hz);
    std::vector<Sample> block;
    std::vector<std::string> list;
    const auto take = [&](const DecodeUpdate& update) {
        for (const TextCorrection& correction : update.corrections) {
            list.resize(correction.from_index);
            for (const DecodedSymbol& symbol : correction.chars) list.push_back(symbol.text);
        }
        for (const DecodedSymbol& symbol : update.chars) list.push_back(symbol.text);
    };
    const auto other = fading_gain(5.0, 1500.0, keyed.key.size(), seed + 2);  // a scatter's path is as narrow as its Doppler spread
    int since_fall = 1 << 30;
    for (std::size_t k = 0; k < keyed.key.size();) {
        block.clear();
        const std::size_t first = k;
        for (; k < keyed.key.size() && block.size() < 32; ++k) {
            since_fall = keyed.key[k] == 1 ? 0 : since_fall + 1;
            std::complex<double> y = amplitude * static_cast<double>(keyed.key[k]) * gain[k];
            // the scatter of the last tail_samples of keying: under a mark as after it, ending tail_samples after the fall
            if (since_fall <= tail_samples) y += amplitude * std::sqrt(tail) * other[k];
            block.emplace_back(static_cast<float>(y.real() + draw(rng)), static_cast<float>(y.imag() + draw(rng)));
        }
        take(decoder.process(block, static_cast<double>(first) / 1500.0));
    }
    out.walk_fast = decoder.walk_fast();
    out.echo = decoder.echo_shape(1);
    take(decoder.flush());
    for (const std::string& symbol : list) out.read += symbol;
    return out;
}

// No-space edit distance, per character of the text.
double cer(const std::string& text, const std::string& read) {
    std::string a, b;
    for (const char c : text) if (c != ' ') a += c;
    for (const char c : read) if (c != ' ') b += c;
    std::vector<int> row(b.size() + 1);
    for (std::size_t j = 0; j <= b.size(); ++j) row[j] = static_cast<int>(j);
    for (std::size_t i = 1; i <= a.size(); ++i) {
        int diagonal = row[0];
        row[0] = static_cast<int>(i);
        for (std::size_t j = 1; j <= b.size(); ++j) {
            const int up = row[j];
            row[j] = std::min({row[j] + 1, row[j - 1] + 1, diagonal + (a[i - 1] == b[j - 1] ? 0 : 1)});
            diagonal = up;
        }
    }
    return static_cast<double>(row[b.size()]) / static_cast<double>(a.size());
}

TEST(SegmentalTail, ReadsAKeyedCarrierThroughADiffuseTailThatFillsItsGaps) {
    // At 35 WPM a tail at -10 dB re the mark for 30 ms (45 samples at 1 500 samples/s) fills most of each 34 ms element gap, as on
    // the owner's 80 m clips. With the tail learned the text reads; without it (echo_max_s 0) the gaps are filled.
    SegmentalConfig config;
    const Channel35 with = decode_35_wpm(config, 0.0, 0.1, 45, 51);
    EXPECT_TRUE(with.echo.on);
    config.echo_max_s = 0.0;
    const Channel35 without = decode_35_wpm(config, 0.0, 0.1, 45, 51);
    EXPECT_FALSE(without.echo.on);
    const double cer_with = cer(with.text, with.read), cer_without = cer(without.text, without.read);
    std::cout << "tail: CER " << cer_with << " with the tail learned, " << cer_without << " without\n";
    // Its first 30 runs, about 10 s, are read without it.
    EXPECT_LT(cer_with, 0.25);
    EXPECT_GT(cer_without, 0.5);
}

TEST(SegmentalDoppler, AFastFadingCarrierSetsTheFastWalkAndReadsBetter) {
    // f_D 10 Hz at +30 dB S500, 35 WPM: the estimator sets the fast walk, which reads the text better than the slow one; a steady
    // carrier keeps the slow walk.
    SegmentalConfig config;
    const Channel35 fast = decode_35_wpm(config, 10.0, 0.0, 0, 61);
    EXPECT_TRUE(fast.walk_fast);
    config.doppler_fast_hz = 0.0;
    const Channel35 slow = decode_35_wpm(config, 10.0, 0.0, 0, 61);
    EXPECT_FALSE(slow.walk_fast);
    const double cer_fast = cer(fast.text, fast.read), cer_slow = cer(slow.text, slow.read);
    std::cout << "f_D 10 Hz: CER " << cer_fast << " with the Doppler walk, " << cer_slow << " without\n";
    EXPECT_LT(cer_fast, cer_slow);
    const Channel35 steady = decode_35_wpm(SegmentalConfig{}, 0.0, 0.0, 0, 62);
    EXPECT_FALSE(steady.walk_fast);
    EXPECT_FALSE(steady.echo.on);
    EXPECT_LT(cer(steady.text, steady.read), 0.005);
}

TEST(SegmentalDoppler, SlowFadingReadsAsBeforeTheTailAndTheWalkToTheSymbol) {
    // f_D 1 Hz at +30 dB S500, 35 WPM: the walk stays slow and no tail is learned, so the decoder reads every symbol as with both
    // left out and the frame unsplit (the path before text-fast-1).
    SegmentalConfig before;
    before.echo_max_s = 0.0;
    before.doppler_fast_hz = 0.0;
    before.level_fast_frame_s = 0.0;
    const Channel35 now = decode_35_wpm(SegmentalConfig{}, 1.0, 0.0, 0, 63);
    const Channel35 old = decode_35_wpm(before, 1.0, 0.0, 0, 63);
    EXPECT_FALSE(now.walk_fast);
    EXPECT_FALSE(now.echo.on);
    EXPECT_EQ(now.read, old.read);
}

}  // namespace
