// The segmental decoder's running sums, shared level and search bookkeeping. The search is fed made-up evidence here, so
// that what it does with the evidence is checked apart from how the evidence is made; the exact decodes through the
// engine are BenchRun.SegmentalDecodesExactlyThroughTheBench (bench/tests/bench_run_test.cpp).
#include "kz4ap/channelizer.hpp"
#include "kz4ap/morse.hpp"
#include "kz4ap/segmental/evidence.hpp"
#include "kz4ap/segmental/search.hpp"
#include "kz4ap/segmental_decoder.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <random>
#include <string>
#include <vector>

using namespace kz4ap;
using namespace kz4ap::segmental;

namespace {

using Window = std::array<float, kEdgeCount>;

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

// The key for a text at dot samples per dot, exact timing, with lead samples before it and tail after.
Keyed keyed_text(const std::string& text, int dot, int lead, int tail) {
    Keyed keyed;
    keyed.add(0, lead);
    bool first_word = true;
    std::string word;
    const auto send_word = [&](const std::string& w) {
        if (!first_word) keyed.add(0, 7 * dot);
        first_word = false;
        bool first_letter = true;
        for (const char c : w) {
            if (!first_letter) keyed.add(0, 3 * dot);
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
    const ModelTables model(config);
    RunningSums sums;
    Search search(config, model, sums);
    feed(keyed_text("CQ", 18, 100, 100), 1.0, sums, &search);
    const double per_boundary = static_cast<double>(search.stats().opened) / static_cast<double>(search.stats().boundaries);
    EXPECT_LE(per_boundary, 2.0 * model.speeds() * model.nodes() + 2.0);
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

TEST(SegmentalShared, RefusesAnotherChannelRate) {
    const SegmentalConfig config = small_config();
    auto shared = std::make_shared<const SegmentalShared>(config, ChannelizerConfig{48000, 2048, 64, 150.0});
    EXPECT_THROW(SegmentalDecoder(2000.0, shared), std::invalid_argument);
    EXPECT_NO_THROW(SegmentalDecoder(shared->channel_rate_hz, shared));
}

}  // namespace
