// The bank decoder's channel (BankChannel, Output) against the prototype's ChannelDecoder.run
// (engine/tests/data/bank/channel.json and channel_stream_*.c64, from training/kz4ap_proto/golden.py
// golden_channel). Each golden stream is decoded in one push and compared with the prototype's full result:
// the text, the characters, corrections, over starts, selections and switches exactly (the discrete values) or
// to relative 1e-9 (times, fitted T, scores); T_P, each window's T and the window are grid values and compared
// exactly. Then the Review Focus tests (1: the same result however the stream is split into pushes; 2: a stream
// cut mid-character and mid-over; 3: exact zeros, missing data since Plan B's B3; 4: the correction cut's
// clipping; 5: 2000 samples/s) and the prototype's channel tests (training/tests/test_proto_channel.py), one for
// one. The golden comparisons and the prototype's tests run at the prototype's configuration (stage1_config: W_min
// 0.8 s and time-out 2 s, the defaults; periodicity windows 2, 5 and 10 s, guard margin and mask-bias table set
// explicitly); the default configuration is tested near the end.
#include "kz4ap/bank/channel.hpp"

#include "golden.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstdio>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace {

using namespace kz4ap::bank;
using kz4ap::test::expect_close;
using kz4ap::test::load_golden;

bool starts_with(const std::string& s, const std::string& p);

const nlohmann::json& golden() {
    static const nlohmann::json g = load_golden("channel");
    return g;
}

double rate_of(const std::string& name) { return golden().at("streams").at(name).at("rate_hz").get<double>(); }

const std::vector<std::complex<double>>& stream_of(const std::string& name) {
    static std::map<std::string, std::vector<std::complex<double>>> cache;
    auto it = cache.find(name);
    if (it == cache.end()) it = cache.emplace(name, kz4ap::test::channel_stream(golden(), name)).first;
    return it->second;
}

// Decodes u at rate_hz, pushed in pieces of `piece` samples (0: one push), at the given configuration (the
// prototype's unless stated: stage 1's periodicity windows, guard margin and mask-bias table).
ChannelResult decode(std::span<const std::complex<double>> u, double rate_hz, std::size_t piece = 0,
                     const BankConfig& cfg = kz4ap::test::stage1_config()) {
    BankChannel ch(cfg, rate_hz);
    if (piece == 0) {
        ch.push(u);
    } else {
        for (std::size_t i = 0; i < u.size(); i += piece) ch.push(u.subspan(i, std::min(piece, u.size() - i)));
    }
    ch.finish();
    return ch.result();
}

// The single-push result of a golden stream ("clean_cut": the clean stream cut at clean_cut_samples), decoded
// once per test run.
const ChannelResult& result_of(const std::string& name) {
    static std::map<std::string, ChannelResult> cache;
    auto it = cache.find(name);
    if (it != cache.end()) return it->second;
    ChannelResult r;
    if (name == "clean_cut") {
        const auto& u = stream_of("clean");
        const auto cut = golden().at("clean_cut_samples").get<std::size_t>();
        r = decode(std::span(u).first(cut), 1500.0);
    } else {
        r = decode(stream_of(name), rate_of(name));
    }
    return cache.emplace(name, std::move(r)).first->second;
}

double nan_if_null(const nlohmann::json& x) {
    return x.is_null() ? std::numeric_limits<double>::quiet_NaN() : x.get<double>();
}

void expect_close_or_nan(double actual, const nlohmann::json& expected) {
    if (expected.is_null())
        EXPECT_TRUE(std::isnan(actual)) << actual;
    else
        expect_close(actual, expected.get<double>());
}

void expect_equal_or_nan(double actual, const nlohmann::json& expected) {
    if (expected.is_null())
        EXPECT_TRUE(std::isnan(actual)) << actual;
    else
        EXPECT_EQ(actual, expected.get<double>());
}

// Traced near-ties (D2): a window's comb maximum that the port and the prototype pick differently because the
// two best candidates' scores differ by rounding. (stream, periodicity recomputation, window index). All three
// are the 2 s window over a buffer of p that is zero but for one short squelch opening, far below the confidence
// threshold (0.03), so T_P, the prior and everything downstream are unaffected. Three candidates tie, 97.95,
// 44.63 and 61.97 ms (the fourth best scores about half as much); ThePeriodicityNearTiesAreRoundingOnBothSides
// recomputes the port's scores of all three. Prototype: numpy 2.5.3 on Windows (the golden values, on the
// streams rounded to complex64, as stored); port: the Windows build (MSVC's libm) and the Linux build (g++ 11.4,
// glibc 2.35). Each side's pick marked *; a near-tie is listed if the port picks differently on any build.
//   noise #14,      97.95 ms: prototype 9.5011315632179236e-07,  Windows 9.5011315631715739e-07,
//                                                                Linux   9.5011315631715739e-07
//                   44.63 ms: prototype 9.5011315632185335e-07,  Windows 9.5011315632185335e-07,
//                                                                Linux  *9.5011315633110972e-07
//                   61.97 ms: prototype*9.5011315633108939e-07,  Windows*9.5011315633455884e-07,
//                                                                Linux   9.5011315633108939e-07
//     leads: prototype 9.2e-18 absolute (9.7e-12 relative), Windows 1.3e-17, Linux 2.0e-20
//   noise #38,      97.95 ms: prototype 2.3515786746381741e-06,  Windows 2.3515786746336205e-06,
//                                                                Linux   2.3515786746381470e-06
//                   44.63 ms: prototype*2.3515786746568495e-06,  Windows*2.3515786746568495e-06,
//                                                                Linux   2.3515786746383367e-06
//                   61.97 ms: prototype 2.3515786746405865e-06,  Windows 2.3515786746405865e-06,
//                                                                Linux  *2.3515786746544642e-06
//     leads: prototype and Windows 1.6e-17 absolute (6.9e-12 relative), Linux 1.6e-17
//   farnsworth #64, 97.95 ms: prototype*4.7329276538316477e-06,  Windows 4.7329276538037837e-06,
//                                                                Linux   4.7329276537942970e-06
//                   44.63 ms: prototype 4.7329276538129181e-06,  Windows*4.7329276538129181e-06,
//                                                                Linux   4.7329276537759468e-06
//                   61.97 ms: prototype 4.7329276537666498e-06,  Windows 4.7329276537944054e-06,
//                                                                Linux  *4.7329276538082832e-06
//     leads: prototype 1.9e-17 absolute (4.0e-12 relative), Windows 9.1e-18, Linux 1.4e-17
// The spread of the three scores is at most 6.5e-17 absolute on any side, and one candidate's score differs
// between the sides by up to 4.2e-17 absolute: each score is a difference of comb-tooth means of the normalized
// autocorrelation (values up to 1, summed as a cumulative sum), so last-bit differences of order 1e-17 in the
// means survive the cancellation down to a score of 1e-6 as relative differences of order 1e-11. That is
// rounding, and it exceeds every lead.
struct NearTie {
    const char* stream;
    std::size_t update;
    std::size_t window;
};
constexpr NearTie kNearTies[] = {{"noise", 14, 0}, {"noise", 38, 0}, {"farnsworth", 64, 0}};

bool is_near_tie(const std::string& stream, std::size_t update, std::size_t window) {
    for (const auto& t : kNearTies)
        if (stream == t.stream && update == t.update && window == t.window) return true;
    return false;
}

void expect_matches_golden(const ChannelResult& r, const nlohmann::json& g, const std::string& stream = "") {
    EXPECT_EQ(r.text, g.at("text").get<std::string>());
    const auto& chars = g.at("chars");
    ASSERT_EQ(r.chars.size(), chars.size());
    for (std::size_t i = 0; i < chars.size(); ++i) {
        SCOPED_TRACE("char " + std::to_string(i));
        EXPECT_EQ(r.chars[i].text, chars[i][0].get<std::string>());
        expect_close(r.chars[i].start_s, chars[i][1].get<double>());
        expect_close(r.chars[i].end_s, chars[i][2].get<double>());
    }
    const auto& corr = g.at("corrections");
    ASSERT_EQ(r.corrections.size(), corr.size());
    for (std::size_t i = 0; i < corr.size(); ++i) {
        SCOPED_TRACE("correction " + std::to_string(i));
        expect_close(r.corrections[i].t_s, corr[i].at("t_s").get<double>());
        expect_close(r.corrections[i].from_s, corr[i].at("from_s").get<double>());
        expect_close(r.corrections[i].reach_s, corr[i].at("reach_s").get<double>());
        EXPECT_EQ(r.corrections[i].old_text, corr[i].at("old").get<std::string>());
        EXPECT_EQ(r.corrections[i].new_text, corr[i].at("new").get<std::string>());
        EXPECT_EQ(r.corrections[i].reason, corr[i].at("reason").get<std::string>());
    }
    const auto os = g.at("over_starts").get<std::vector<double>>();
    ASSERT_EQ(r.over_starts.size(), os.size());
    for (std::size_t i = 0; i < os.size(); ++i) expect_close(r.over_starts[i], os[i]);
    EXPECT_EQ(r.switches, g.at("switches").get<int>());
    const auto& sel = g.at("selections");
    ASSERT_EQ(r.selections.size(), sel.size());
    for (std::size_t i = 0; i < sel.size(); ++i) {
        SCOPED_TRACE("selection " + std::to_string(i));
        expect_close(r.selections[i].t_s, sel[i][0].get<double>());
        EXPECT_EQ(r.selections[i].branch, sel[i][1].get<int>());
        expect_close_or_nan(r.selections[i].t_dit_s, sel[i][2]);
    }
    const auto& per = g.at("periodicity");
    ASSERT_EQ(r.periodicity.size(), per.size());
    for (std::size_t i = 0; i < per.size(); ++i) {
        SCOPED_TRACE("periodicity " + std::to_string(i));
        const auto& p = r.periodicity[i];
        expect_close(p.t_s, per[i][0].get<double>());
        expect_equal_or_nan(p.t_p_s, per[i][1]);  // a grid point: exact
        expect_close_or_nan(p.confidence, per[i][2]);
        expect_equal_or_nan(p.window_s, per[i][3]);
        const auto& w = per[i][4];
        ASSERT_EQ(p.per_window.size(), w.size());
        for (std::size_t j = 0; j < w.size(); ++j) {
            EXPECT_EQ(p.per_window[j].first.has_value(), !w[j][0].is_null());
            if (p.per_window[j].first && !w[j][0].is_null() && *p.per_window[j].first != w[j][0].get<double>()) {
                // only a traced near-tie may differ (its score still agrees, below)
                EXPECT_TRUE(is_near_tie(stream, i, j))
                    << "window " << j << ": T " << *p.per_window[j].first << " s, prototype " << w[j][0].get<double>();
                EXPECT_LT(p.per_window[j].second, BankConfig{}.comb_confidence_min);
            }
            expect_close_or_nan(p.per_window[j].second, w[j][1]);
        }
    }
}

// Bitwise equality of two results (Review Focus 1).
void expect_identical(const ChannelResult& a, const ChannelResult& b) {
    auto same = [](double x, double y) { return (std::isnan(x) && std::isnan(y)) || x == y; };
    EXPECT_EQ(a.text, b.text);
    ASSERT_EQ(a.chars.size(), b.chars.size());
    for (std::size_t i = 0; i < a.chars.size(); ++i) {
        EXPECT_EQ(a.chars[i].text, b.chars[i].text);
        EXPECT_EQ(a.chars[i].start_s, b.chars[i].start_s);
        EXPECT_EQ(a.chars[i].end_s, b.chars[i].end_s);
    }
    ASSERT_EQ(a.corrections.size(), b.corrections.size());
    for (std::size_t i = 0; i < a.corrections.size(); ++i) {
        const auto &x = a.corrections[i], &y = b.corrections[i];
        EXPECT_TRUE(x.t_s == y.t_s && x.from_s == y.from_s && x.reach_s == y.reach_s && x.old_text == y.old_text &&
                    x.new_text == y.new_text && x.reason == y.reason && x.from_index == y.from_index);
    }
    EXPECT_EQ(a.over_starts, b.over_starts);
    EXPECT_EQ(a.switches, b.switches);
    ASSERT_EQ(a.selections.size(), b.selections.size());
    for (std::size_t i = 0; i < a.selections.size(); ++i)
        EXPECT_TRUE(a.selections[i].t_s == b.selections[i].t_s && a.selections[i].branch == b.selections[i].branch &&
                    same(a.selections[i].t_dit_s, b.selections[i].t_dit_s));
    ASSERT_EQ(a.periodicity.size(), b.periodicity.size());
    for (std::size_t i = 0; i < a.periodicity.size(); ++i) {
        const auto &x = a.periodicity[i], &y = b.periodicity[i];
        EXPECT_TRUE(x.t_s == y.t_s && same(x.t_p_s, y.t_p_s) && same(x.confidence, y.confidence) &&
                    same(x.window_s, y.window_s) && x.per_window.size() == y.per_window.size());
        for (std::size_t j = 0; j < std::min(x.per_window.size(), y.per_window.size()); ++j)
            EXPECT_TRUE(x.per_window[j].first == y.per_window[j].first &&
                        same(x.per_window[j].second, y.per_window[j].second));
    }
}

// No NaN or infinity in a published value or a correction (Review Focus 3).
void expect_published_finite(const ChannelResult& r) {
    for (const auto& c : r.chars) EXPECT_TRUE(std::isfinite(c.start_s) && std::isfinite(c.end_s)) << c.text;
    for (const auto& c : r.corrections)
        EXPECT_TRUE(std::isfinite(c.t_s) && std::isfinite(c.from_s) && std::isfinite(c.reach_s)) << c.reason;
}

// ---- golden comparisons -----------------------------------------------------------------------------------

class ChannelGolden : public ::testing::TestWithParam<std::string> {};

TEST_P(ChannelGolden, MatchesThePrototypesRun) {
    const ChannelResult& r = result_of(GetParam());
    expect_matches_golden(r, golden().at("results").at(GetParam()), GetParam());
    expect_published_finite(r);
}

// "zero_pad" (1 s of exact zeros, then a station) left this list in Plan B's B3: its golden pinned the prototype's
// exact-zero defect (it keyed nothing); BankChannel.LeadingExactZerosDecodeAsTheStationAlone now requires it to
// decode as the station alone. It stays in ChannelSplit (pushes against the single push, not the prototype).
INSTANTIATE_TEST_SUITE_P(Streams, ChannelGolden,
                         ::testing::Values("clean", "turnover", "noise_tail", "step", "farnsworth",
                                           "first_sample", "slow", "noise", "two_speeds", "tune_up", "long", "short",
                                           "speed_turnover", "clean_2000", "clean_cut"),
                         [](const auto& info) { return info.param; });

// ---- the traced near-ties, both candidates on both sides (D2) ----------------------------------------------

// The port's score of one candidate T (s) in the 2 s window at a periodicity recomputation: the comb on the
// port's own buffer, restricted to that one grid point.
double port_score(const BankChannel& ch, double t_s) {
    const BankConfig cfg;
    const auto& per = ch.periodicity();
    const auto& buf = per.buffer();
    const auto w = static_cast<std::size_t>(per.windows().front());
    const std::span<const double> window(buf.data() + (buf.size() - w), w);
    const auto [t, score] = comb_estimate(window, per.rate_hz(), {t_s}, cfg.comb_teeth, cfg.comb_width);
    EXPECT_TRUE(t.has_value() && *t == t_s);
    return score;
}

TEST(BankChannel, ThePeriodicityNearTiesAreRoundingOnBothSides) {
    // Grid points (s), bit-identical in the port and the prototype, and the prototype's scores of both
    // candidates (Python, recomputed from its buffer at the same recomputation):
    // The three near-tied candidates (the fourth best scores about half as much):
    const std::array<double, 3> t = {0.0979469944911008,     // 97.95 ms: the prototype's pick
                                     0.04462750274310159,    // 44.63 ms: the Windows build's pick
                                     0.061973770591775994};  // 61.97 ms: the Linux (glibc) build's pick at farnsworth
    struct Case {
        const char* stream;
        std::size_t update;
        std::array<double, 3> proto;
        std::size_t proto_pick;  // index into t of the prototype's pick
    };
    const Case cases[] = {{"noise", 14, {9.501131563217924e-07, 9.501131563218533e-07, 9.501131563310894e-07}, 2},
                          {"noise", 38, {2.351578674638174e-06, 2.3515786746568495e-06, 2.3515786746405865e-06}, 1},
                          {"farnsworth", 64, {4.732927653831648e-06, 4.732927653812918e-06, 4.73292765376665e-06}, 0}};
    for (const auto& c : cases) {
        SCOPED_TRACE(c.stream);
        const auto& u = stream_of(c.stream);
        BankChannel ch(kz4ap::test::stage1_config(), 1500.0);
        const auto block = static_cast<std::size_t>(ch.block_samples());
        for (std::size_t i = 0; i < u.size() && ch.periodicity_records() <= c.update; i += block)
            ch.push(std::span(u).subspan(i, std::min(block, u.size() - i)));
        ASSERT_EQ(ch.periodicity_records(), c.update + 1);
        std::array<double, 3> port{};
        for (std::size_t j = 0; j < 3; ++j) {
            port[j] = port_score(ch, t[j]);
            std::printf("%s #%zu: T = %.2f ms: port %.16e, prototype %.16e\n", c.stream, c.update, 1000.0 * t[j],
                        port[j], c.proto[j]);
            expect_close(port[j], c.proto[j]);
        }
        // Each side picks its largest; which candidate the port picks depends on the platform's libm, so the
        // port's pick is checked against its own scores. The prototype's pick is its largest held score, and the
        // golden result records that pick at this recomputation.
        const std::optional<double> pick = ch.result().periodicity.back().per_window.front().first;  // a copy
        ASSERT_TRUE(pick.has_value());
        const auto at = std::find(t.begin(), t.end(), *pick);
        ASSERT_NE(at, t.end()) << *pick;
        EXPECT_EQ(port[static_cast<std::size_t>(at - t.begin())], *std::max_element(port.begin(), port.end()));
        EXPECT_EQ(c.proto[c.proto_pick], *std::max_element(c.proto.begin(), c.proto.end()));
        const auto& golden_record = golden().at("results").at(c.stream).at("periodicity").at(c.update);
        EXPECT_EQ(golden_record.at(4).at(0).at(0).get<double>(), t[c.proto_pick]);
        // The three candidates differ by rounding on each side: the spread of their scores is below 1e-16 absolute.
        // The bound is heuristic, not derived: a margin about 1.5x above the largest measured spread (the
        // prototype's 6.5e-17 at farnsworth #64; elsewhere, prototype / Windows / Linux: farnsworth #64 6.5e-17 /
        // 1.9e-17 / 3.2e-17, noise #14 9.3e-18 / 1.7e-17 / 1.4e-17, noise #38 1.9e-17 / 2.3e-17 / 1.6e-17), below
        // one unit in the last place of 1.0 (2.2e-16), the scale of the normalized autocorrelation's values whose
        // cumulative-sum means the scores are differences of. Relative to the scores it is 2.1e-11 at farnsworth
        // (4.73e-6), 4.3e-11 at noise #38 (2.35e-6) and 1.05e-10 at noise #14 (9.5e-7). (The checks on c.proto
        // check the held constants against the golden pick, not code: they document the prototype's pick and
        // spread.)
        const auto spread = [](const std::array<double, 3>& s) {
            return *std::max_element(s.begin(), s.end()) - *std::min_element(s.begin(), s.end());
        };
        std::printf("%s #%zu: spread port %.2e, prototype %.2e (absolute)\n", c.stream, c.update, spread(port),
                    spread(c.proto));
        EXPECT_LT(spread(port), 1e-16);
        EXPECT_LT(spread(c.proto), 1e-16);
    }
}

// ---- Review Focus 1: the split into pushes does not matter ------------------------------------------------

class ChannelSplit : public ::testing::TestWithParam<std::string> {};

TEST_P(ChannelSplit, PiecesOf1And47And1000SamplesGiveTheSingleResult) {
    const auto& u = stream_of(GetParam());
    const ChannelResult& whole = result_of(GetParam());
    for (std::size_t piece : {std::size_t{1}, std::size_t{47}, std::size_t{1000}}) {
        SCOPED_TRACE("pieces of " + std::to_string(piece) + " samples");
        expect_identical(decode(u, rate_of(GetParam()), piece), whole);
    }
}

INSTANTIATE_TEST_SUITE_P(Streams, ChannelSplit,
                         ::testing::Values("clean", "turnover", "noise_tail", "step", "farnsworth", "zero_pad"),
                         [](const auto& info) { return info.param; });

// ---- Review Focus 2: a stream that ends mid-character and mid-over ----------------------------------------

TEST(BankChannel, AStreamCutMidCharacterPublishesThePartialCharacterAndNothingPastTheEnd) {
    const auto cut = golden().at("clean_cut_samples").get<std::int64_t>();
    const double end_s = static_cast<double>(cut) / 1500.0;
    // not a whole number of blocks: finish() processes a partial block, as the prototype's last one
    ASSERT_NE(cut % BankChannel(BankConfig{}, 1500.0).block_samples(), 0);
    const ChannelResult& r = result_of("clean_cut");
    expect_matches_golden(r, golden().at("results").at("clean_cut"));
    // the over's last character, C (-.-.), was cut in its third element: what was keyed is published ("N", -.)
    ASSERT_FALSE(r.chars.empty());
    EXPECT_EQ(r.chars.back().text, "N");
    for (const auto& c : r.chars) EXPECT_LE(c.end_s, end_s);
    for (const auto& c : r.corrections) {
        EXPECT_LE(c.t_s, end_s);
        EXPECT_LE(c.from_s, c.t_s);
        EXPECT_LE(c.reach_s, 20.0 + 1e-9);
    }
    // the same cut stream pushed in pieces
    const auto& u = stream_of("clean");
    expect_identical(decode(std::span(u).first(static_cast<std::size_t>(cut)), 1500.0, 47), r);
}

// ---- Review Focus 3: exact zeros at the start (Plan B, B3) --------------------------------------------------

// Plan B task B3 (replaces Plan A's ExactZerosAtTheStartPublishNoNaN, which pinned the prototype's defect: it
// keyed nothing here): exact zeros are missing data. The zero_pad stream (1 s = 1500 samples of exact zeros,
// then a 25 words/min station from 0.5 s) decodes as the station alone (the same stream without its leading
// zeros): the same text, the same characters with their times shifted by 1 s, and no NaN in a published value.
// The shift is not exact to the last bit: 1500 samples are 46.875 blocks of 32, so every block boundary of the
// padded stream falls 4 samples (2.67 ms) later in the station's time than the unpadded stream's, and the noise
// estimate, the keying and the periodicity are updated once per block. A character's time may therefore move
// by the edges' quantization to the block grid: the tolerance is one block (21.33 ms). Checked at the prototype's
// configuration and at the default configuration.
void expect_leading_zeros_decode_as_the_station_alone(const BankConfig& cfg, const std::string& want_text) {
    const auto& u = stream_of("zero_pad");
    const double rate = rate_of("zero_pad");
    const std::size_t zeros = 1500;
    for (std::size_t i = 0; i < zeros; ++i) ASSERT_EQ(u[i], std::complex<double>(0.0, 0.0));
    ASSERT_NE(u[zeros], std::complex<double>(0.0, 0.0));
    const ChannelResult padded = decode(u, rate, 0, cfg);
    const ChannelResult alone = decode(std::span(u).subspan(zeros), rate, 0, cfg);
    const double shift = static_cast<double>(zeros) / rate;    // 1 s
    const double tol = BankConfig{}.block_s;                    // s
    EXPECT_EQ(alone.text, want_text);
    EXPECT_EQ(padded.text, alone.text);
    ASSERT_EQ(padded.chars.size(), alone.chars.size());
    double worst = 0.0;
    for (std::size_t i = 0; i < alone.chars.size(); ++i) {
        EXPECT_EQ(padded.chars[i].text, alone.chars[i].text) << "char " << i;
        worst = std::max({worst, std::abs(padded.chars[i].start_s - (alone.chars[i].start_s + shift)),
                          std::abs(padded.chars[i].end_s - (alone.chars[i].end_s + shift))});
    }
    EXPECT_LE(worst, tol) << "largest time difference " << worst << " s";
    std::printf("[ info ] largest character-time difference after the 1 s shift: %.6f s\n", worst);
    expect_published_finite(padded);
    for (const auto& s : padded.selections) EXPECT_TRUE(std::isfinite(s.t_s));
    for (const auto& p : padded.periodicity) EXPECT_TRUE(std::isfinite(p.t_s) && std::isfinite(p.confidence));
    for (const auto& o : padded.over_starts) EXPECT_TRUE(std::isfinite(o));
}

TEST(BankChannel, LeadingExactZerosDecodeAsTheStationAlone) {
    expect_leading_zeros_decode_as_the_station_alone(kz4ap::test::stage1_config(), "CQ TEST K1ABC ");
}

TEST(BankChannelDefaults, LeadingExactZerosDecodeAsTheStationAlone) {
    expect_leading_zeros_decode_as_the_station_alone(BankConfig{}, "CQ TEST K1ABC ");
}

TEST(BankChannel, ThePowerWindowStoresFourByteFloats) {
    // Plan B task B1: the |v_k|^2 window holds values already rounded to float32 and stores them as 4-byte
    // floats. 24 s of exact zeros at 1500 samples/s fill it to its full size (keyed nothing: "Exact zeros"):
    // 32 branches x 34 675 columns (docs/signal-processing.md appendix A.8c, "Memory"): the look-back 31 642 samples
    // (30 000 + 3 x 276 + round((0.1707 + 2 x 0.0048 + 0.32) s x 1500) = 750 + 2 x 32) plus a block, 2 s and 1;
    // 34 721 with stage 1's 20 ms guard margin (look-back 31 688).
    BankChannel ch(BankConfig{}, 1500.0);
    const std::vector<std::complex<double>> zeros(36000, {0.0, 0.0});
    ch.push(zeros);
    EXPECT_EQ(ch.p_window_values(), 32u * 34675u);
    BankChannel stage1(kz4ap::test::stage1_config(), 1500.0);
    stage1.push(zeros);
    EXPECT_EQ(stage1.p_window_values(), 32u * 34721u);
    EXPECT_EQ(ch.p_window_bytes(), ch.p_window_values() * 4u);
}

// ---- Review Focus 4 and the prototype's Output tests ------------------------------------------------------

TEST(BankOutput, AppendsAndCorrectsWithinTheReach) {
    Output out(20.0);
    out.append_new({{"A", 1.0, 1.1}, {"B", 2.0, 2.1}});
    out.append_new({{"A", 1.0, 1.1}, {"B", 2.0, 2.1}, {"C", 3.0, 3.1}});
    EXPECT_EQ(out.text(), "ABC");
    out.replace_from(2.0, {{"X", 2.0, 2.1}, {"Y", 3.0, 3.1}}, 4.0, "switch");
    EXPECT_EQ(out.text(), "AXY");
    const Correction& c = out.corrections().back();
    EXPECT_EQ(c.from_s, 2.0);
    EXPECT_EQ(c.reach_s, 2.0);
    EXPECT_EQ(c.old_text, "BC");
    EXPECT_EQ(c.new_text, "XY");
    EXPECT_EQ(c.reason, "switch");
    EXPECT_EQ(c.from_index, 1u);  // "A" kept
    out.replace_from(0.0, {{"Z", 1.0, 1.1}}, 30.0, "rekey");  // reaches only 20 s back: from 10 s
    EXPECT_EQ(out.text(), "AXY");
    EXPECT_EQ(out.corrections().size(), 1u);
}

TEST(BankOutput, ReplacesACharacterThatTwoBranchesTimeAFewMsApartOnce) {
    Output out(20.0);
    out.append_new({{"C", 1.004, 1.40}, {"Q", 1.6750, 1.95}, {" ", 1.95, 1.95}});
    out.replace_from(1.6757, {{"C", 1.0045, 1.40}, {"Q", 1.6757, 1.95}, {" ", 1.95, 1.95}}, 2.8, "switch");
    EXPECT_EQ(out.text(), "CQ ");
    EXPECT_EQ(out.chars()[1].start_s, 1.6757);  // the new branch's copy
    EXPECT_TRUE(out.corrections().empty());     // same text: no correction
    // a character that starts before the reach (t - 20 s) is kept, and a new one overlapping it is not taken
    Output o2(20.0);
    o2.append_new({{"A", 9.98, 10.10}, {"B", 10.5, 10.6}});
    o2.replace_from(9.0, {{"A", 9.99, 10.10}, {"X", 10.5, 10.6}}, 30.0, "switch");
    EXPECT_EQ(o2.text(), "AX");
    const Correction& c = o2.corrections().back();
    EXPECT_EQ(c.from_s, 10.5);
    EXPECT_EQ(c.reach_s, 19.5);
    EXPECT_EQ(c.old_text, "B");
    EXPECT_EQ(c.new_text, "X");
    EXPECT_EQ(c.from_index, 1u);
}

TEST(BankOutput, ACutBeforeTheFirstCharacterAndMoreThanTheReachBackIsClipped) {
    // Review Focus 4: from_s before the stream's first sample and more than 20 s before t_s. The cut is
    // max(from_s, t_s - 20 s) = 5 s: characters starting before it (and ended by it) are kept, the rest are
    // replaced, and the correction's start is the first replaced character's, so its reach is at most 20 s.
    Output out(20.0);
    out.append_new({{"A", 1.0, 1.2}, {"B", 4.9, 5.1}, {"C", 6.0, 6.2}, {"D", 8.0, 8.2}});
    out.replace_from(-3.0, {{"P", 0.5, 0.7}, {"Q", 5.2, 5.4}, {"R", 6.0, 6.2}, {"S", 8.0, 8.2}}, 25.0, "rekey");
    // "B" started before the reach (4.9 s < 5 s) and is kept; nothing new may start before its end (5.1 s)
    EXPECT_EQ(out.text(), "ABQRS");
    ASSERT_EQ(out.corrections().size(), 1u);
    const Correction& c = out.corrections()[0];
    EXPECT_EQ(c.old_text, "CD");
    EXPECT_EQ(c.new_text, "QRS");
    EXPECT_EQ(c.from_s, 6.0);
    EXPECT_EQ(c.reach_s, 19.0);
    EXPECT_EQ(c.from_index, 2u);
    // Nothing published at all, and the cut far back: the correction starts at the cut, t_s - 20 s.
    Output empty(20.0);
    empty.replace_from(-100.0, {{"E", -1.0, -0.9}, {"T", 12.0, 12.3}}, 30.0, "switch");
    EXPECT_EQ(empty.text(), "T");
    ASSERT_EQ(empty.corrections().size(), 1u);
    EXPECT_EQ(empty.corrections()[0].from_s, 10.0);
    EXPECT_EQ(empty.corrections()[0].reach_s, 20.0);
    EXPECT_EQ(empty.corrections()[0].from_index, 0u);
}

TEST(BankOutput, FromIndexCountsTheKeptCharacters) {
    // The text published before a correction = its first from_index characters + old_text; after it, the same
    // from_index characters + new_text (then appends). Checked block by block on the streams with corrections:
    // after every block whose corrections number one, the characters before it and after it agree so.
    for (const std::string name : {"clean", "turnover", "speed_turnover"}) {
        SCOPED_TRACE(name);
        const auto& u = stream_of(name);
        BankChannel ch(kz4ap::test::stage1_config(), rate_of(name));
        const auto block = static_cast<std::size_t>(ch.block_samples());
        std::vector<Char> before;
        std::size_t seen = 0;
        for (std::size_t i = 0; i < u.size(); i += block) {
            ch.push(std::span(u).subspan(i, std::min(block, u.size() - i)));
            const auto& corr = ch.output().corrections();
            const auto& now = ch.output().chars();
            if (corr.size() == seen + 1) {
                const Correction& c = corr.back();
                ASSERT_LE(c.from_index, before.size());
                ASSERT_LE(c.from_index, now.size());
                std::string old_text, kept_before, kept_after;
                for (std::size_t j = 0; j < c.from_index; ++j) kept_before += before[j].text;
                for (std::size_t j = 0; j < c.from_index; ++j) kept_after += now[j].text;
                for (std::size_t j = c.from_index; j < before.size(); ++j) old_text += before[j].text;
                EXPECT_EQ(kept_before, kept_after);
                EXPECT_EQ(old_text, c.old_text);
                std::string tail;
                for (std::size_t j = c.from_index; j < now.size(); ++j) tail += now[j].text;
                EXPECT_TRUE(starts_with(tail, c.new_text)) << tail << " / " << c.new_text;
            }
            seen = corr.size();
            before = now;
        }
        ch.finish();
        EXPECT_EQ(ch.result().corrections.size(), result_of(name).corrections.size());
    }
    Output out(20.0);
    out.append_new({{"C", 1.0, 1.2}, {"Q", 1.5, 1.8}, {" ", 1.8, 1.8}, {"D", 2.5, 2.7}});
    out.replace_from(1.4, {{"Q", 1.5, 1.8}, {"E", 2.5, 2.6}}, 3.0, "switch");
    EXPECT_EQ(out.text(), "CQE");
    EXPECT_EQ(out.corrections().back().from_index, 1u);
    EXPECT_EQ(out.corrections().back().old_text, "Q D");
    EXPECT_EQ(out.corrections().back().new_text, "QE");
}

TEST(BankOutput, FirstChangedIndexIsBelowFromIndexWhenKeptCharactersAreNotAPrefix) {
    // Two T's overlap in time with a Q between them in the list (published in start order): a cut at 1.2 s keeps
    // both T's (ended before it) and replaces Q (not ended). The kept characters are not a prefix of the list: the
    // prototype's from_index counts 2 of them, while the first character whose text changed is at index 1.
    Output out(20.0);
    out.append_new({{"T", 1.000, 1.100}, {"Q", 1.002, 1.400}, {"T", 1.004, 1.105}, {"O", 1.500, 1.800}});
    out.replace_from(1.2, {{"O", 1.500, 1.800}}, 3.0, "switch");
    EXPECT_EQ(out.text(), "TTO");
    ASSERT_EQ(out.corrections().size(), 1u);
    const Correction& c = out.corrections()[0];
    EXPECT_EQ(c.old_text, "QO");
    EXPECT_EQ(c.new_text, "O");
    EXPECT_EQ(c.from_index, 2u);
    EXPECT_EQ(c.first_changed_index, 1u);
    EXPECT_EQ(out.text_changes(), std::vector<std::size_t>{1u});
    // A same-text replacement that reorders overlapping characters changes the list's text without a correction.
    Output o2(20.0);
    o2.append_new({{"T", 1.000, 1.100}, {"Q", 1.002, 1.400}, {"T", 1.004, 1.105}});
    o2.replace_from(1.2, {{"Q", 1.250, 1.400}}, 3.0, "switch");
    EXPECT_EQ(o2.text(), "TTQ");
    EXPECT_TRUE(o2.corrections().empty());
    EXPECT_EQ(o2.text_changes(), std::vector<std::size_t>{1u});
    // A prefix cut: first_changed_index is at or after from_index (here a re-timed "Q" keeps its text).
    Output o3(20.0);
    o3.append_new({{"C", 1.0, 1.2}, {"Q", 1.5, 1.8}, {" ", 1.8, 1.8}, {"D", 2.5, 2.7}});
    o3.replace_from(1.4, {{"Q", 1.5, 1.8}, {"E", 2.5, 2.6}}, 3.0, "switch");
    EXPECT_EQ(o3.corrections().back().from_index, 1u);
    EXPECT_EQ(o3.corrections().back().first_changed_index, 2u);
}

// ---- Review Focus 5: 2000 samples/s -----------------------------------------------------------------------

TEST(BankChannel, TwoThousandSamplesPerSecondDecodesTheSameText) {
    const ChannelResult& r2000 = result_of("clean_2000");
    expect_matches_golden(r2000, golden().at("results").at("clean_2000"));
    EXPECT_EQ(r2000.text, result_of("clean").text);
    BankChannel ch(BankConfig{}, 2000.0);
    EXPECT_EQ(ch.block_samples(), 43);  // round(32/1500 s x 2000 samples/s) = round(42.67)
    EXPECT_EQ(ch.branch_samples_n().front(), 19);
    EXPECT_EQ(ch.branch_samples_n().back(), 369);
}

// ---- the prototype's channel tests (training/tests/test_proto_channel.py) ---------------------------------

std::string norm(const std::string& s) {
    std::istringstream in(s);
    std::string w, out;
    while (in >> w) out += (out.empty() ? "" : " ") + w;
    return out;
}

double cer(const std::string& reference, const std::string& decoded) {
    std::vector<std::size_t> row(decoded.size() + 1);
    for (std::size_t j = 0; j < row.size(); ++j) row[j] = j;
    for (std::size_t i = 1; i <= reference.size(); ++i) {
        std::size_t diag = row[0];
        row[0] = i;
        for (std::size_t j = 1; j <= decoded.size(); ++j) {
            const std::size_t above = row[j];
            row[j] = std::min({row[j] + 1, row[j - 1] + 1, diag + (reference[i - 1] != decoded[j - 1] ? 1u : 0u)});
            diag = above;
        }
    }
    return static_cast<double>(row[decoded.size()]) / static_cast<double>(reference.size());
}

bool starts_with(const std::string& s, const std::string& p) { return s.rfind(p, 0) == 0; }
bool ends_with(const std::string& s, const std::string& p) {
    return s.size() >= p.size() && s.compare(s.size() - p.size(), p.size(), p) == 0;
}

// A strict expected failure of the prototype: the assertion must still fail here (else the finding no longer
// reproduces, which the prototype's strict xfail would also report); it is then skipped with the reason.
#define KZ4AP_STRICT_XFAIL(passes, reason)                                                                   \
    do {                                                                                                     \
        if (passes) FAIL() << "unexpectedly passes (the prototype's strict xfail): " << (reason);             \
        GTEST_SKIP() << (reason);                                                                            \
    } while (0)

TEST(BankChannelPrototypeTests, DecodesACleanStation) {
    const std::string got = norm(result_of("clean").text);
    KZ4AP_STRICT_XFAIL(got == "CQ TEST K1ABC K1ABC",
                       "Finding (Task 14, E9): with W_min = 0.8 s of keyed time (adopted by E9b; 0.4 s before) the "
                       "text reads 'Q TEST K1ABC K1ABC': the over's first character, C, is lost (25 WPM, S500 = 20 "
                       "dB, seed 1). The cause has not been traced. [port: reads '" + got + "']");
}

TEST(BankChannelPrototypeTests, ACleanStationKeepsEverythingAfterTheFirstCharacter) {
    EXPECT_TRUE(ends_with(norm(result_of("clean").text), "TEST K1ABC K1ABC")) << result_of("clean").text;
}

TEST(BankChannelPrototypeTests, DecodesAStationFromTheFirstSample) {
    EXPECT_LE(cer("CQ TEST K1ABC K1ABC", norm(result_of("first_sample").text)), 0.1);
}

TEST(BankChannelPrototypeTests, SlowFirstWordIsRightAfterCorrections) {
    EXPECT_TRUE(starts_with(norm(result_of("slow").text), "HI HI")) << result_of("slow").text;
}

TEST(BankChannelPrototypeTests, FollowsASpeedStepWithinTenMarks) {
    const ChannelResult& r = result_of("step");
    const auto& in = golden().at("test_inputs");
    const double step_s = in.at("step_s").get<double>();
    const auto lengths = BankChannel(BankConfig{}, 1500.0).lengths_s();
    std::optional<double> followed;
    for (const auto& s : r.selections)
        if (s.t_s > step_s &&
            std::abs(std::log(lengths[static_cast<std::size_t>(s.branch)] / (0.8 * 1.2 / 30.0))) <= std::log(1.1)) {
            followed = s.t_s;
            break;
        }
    ASSERT_TRUE(followed.has_value()) << "no branch matched to 30 WPM was ever selected after the step";
    int marks = 0;
    for (double a : in.at("step_mark_starts_s").get<std::vector<double>>())
        if (step_s <= 1.0 + a && 1.0 + a <= *followed) ++marks;
    KZ4AP_STRICT_XFAIL(marks <= 10,
                       "Finding (Task 11), design/placeholders: the step is followed after 11 marks (1.73 s), the same "
                       "for seeds 1-4. The fits stay at the 15 WPM dit (about 80 ms) while the 2 s periodicity "
                       "window's confident T_P is 80.3 ms (the T_P prior, 0.1 in ln T, holds them); T_P changes to "
                       "40.0 ms 1.0 s after the step (t = 11.605 s), the index-13 fit follows at the next observation, "
                       "and the switch then needs M = 4 eligible instants in a row (the one fallback pick of index 13 "
                       "just before restarts the count): 11.776 ... 12.331 s. Without the T_P prior it takes 15 marks "
                       "(2.22 s). Placeholders: M (E6), windows (E2); prior width heuristic. Update (Task 14, after "
                       "E4-E9: N_mem 48, coarse grids, calibrated x_on, W_min 0.8 s): this seed (4) follows after 13 "
                       "marks (1.90 s); E6 measured 14, 14, 16, 13 marks for seeds 1-4 at M = 4 (the fit details above "
                       "are from Task 11 and were not re-checked). [port: " + std::to_string(marks) + " marks]");
}

TEST(BankChannelPrototypeTests, FarnsworthWordGapsDoNotStartANewOver) {
    const double last = golden().at("test_inputs").at("farnsworth_last_key_up_s").get<double>();
    for (double t : result_of("farnsworth").over_starts) EXPECT_GE(t, last);
}

TEST(BankChannelPrototypeTests, FarnsworthTextIsRight) {
    const std::string got = norm(result_of("farnsworth").text);
    KZ4AP_STRICT_XFAIL(got == "CQ TEST K1ABC",
                       "Finding (Task 11), design: no word gap starts an over (the test above), but the text reads 'CQ "
                       "TEST U1ABC'. The default comb's T_P locks on T_g for Farnsworth 18/10 WPM in the 5 s window "
                       "(202.5-206.6 ms against T = 66.7 ms; the Task 9 finding), and the confident T_P prior (0.1 in "
                       "ln T, about 63 nats at ln(204.5/66.8)) pulls the selected branch's fit (index 18, L = 53.3 ms) "
                       "from T = 66.6 ms to T = 149.5 ms, w = 71 ms, so K's first dah (199 ms) reads as a dit. With "
                       "the edge comb, or without the prior, the text is right. Periodicity method: E1. Update (Task 14, "
                       "E9b, W_min = 0.8 s): the text reads 'Q TEST U1ABC' (at W_min = 0.4 s, 'CQ TEST U1ABC'): the "
                       "over's first character is also lost, as in test_decodes_a_clean_station. The fit details above "
                       "are from Task 11 and were not re-checked. [port: reads '" + got + "']");
}

TEST(BankChannelPrototypeTests, NoiseAloneLeavesNoText) {
    const ChannelResult& r = result_of("noise");
    std::string t = norm(r.text);
    t.erase(std::remove(t.begin(), t.end(), ' '), t.end());
    EXPECT_LE(t.size(), 2u);
    for (const auto& c : r.corrections) EXPECT_LE(c.reach_s, 20.0 + 1e-9);
}

TEST(BankChannelPrototypeTests, TwoOversAtDifferentSpeeds) {
    EXPECT_LE(cer("CQ DE K1ABC K K1ABC DE W9XYZ K", norm(result_of("two_speeds").text)), 0.1);
}

TEST(BankChannelPrototypeTests, ATuneUpCarrierDoesNotDerailDecoding) {
    EXPECT_NE(norm(result_of("tune_up").text).find("TEST K1ABC K1ABC"), std::string::npos);
}

TEST(BankChannelPrototypeTests, CorrectionsNeverReachBackMoreThan20s) {
    // (The prototype's second assertion, to_json's corrections equal to the dataclasses, is the bench's JSON
    // form: bench/tests/bank_json_test.cpp.)
    for (const auto& c : result_of("long").corrections) EXPECT_LE(c.reach_s, 20.0 + 1e-9);
}

TEST(BankChannelPrototypeTests, ShortAndEmptyStreams) {
    EXPECT_EQ(decode({}, 1500.0).text, "");
    (void)result_of("short").text;  // a string, as the prototype's isinstance check
    SUCCEED();
}

TEST(BankChannelPrototypeTests, KeepsBranchOnesPosteriorForTheOfflineExperiments) {
    GTEST_SKIP() << "run(keep_p1=True), branch 1's posterior for the prototype's offline experiments (Task 13), is "
                    "not ported: no engine stage uses it";
}

TEST(BankChannelPrototypeTests, ASameSpeedTurnoverKeepsThePreviousOversFit) {
    const std::string got = norm(result_of("turnover").text);
    KZ4AP_STRICT_XFAIL(starts_with(got, "CQ DE K1ABC K EE TT EE TT "),
                       "Finding (Task 14, E4): with N_mem = 48 (adopted by E4) the text reads 'CCQ DE K1ABC K EE TT EE "
                       "TT K1ABC': a spurious leading 'C' before the first over, while the second over's callsign is "
                       "now correct (with N_mem = 24 it read '... U1ABC'). The cause has not been traced. [port and "
                       "prototype now read '" + got + "']");
}

TEST(BankChannelPrototypeTests, ASameSpeedTurnoverDecodesTheWholeSecondOver) {
    const std::string got = norm(result_of("turnover").text);
    KZ4AP_STRICT_XFAIL(got == "CQ DE K1ABC K EE TT EE TT K1ABC",
                       "Finding (Task 11 fix round 1), design: the second over reads 'EE TT EE TT U1ABC'. The previous "
                       "fit wins the re-key (11.2 s), but its rival fresh fit takes over at about 12.4 s (12 marks and "
                       "spaces of the over, penalty 1/2 x 4 x ln 12 = 5.0 nats) with T = 102.4 ms, q = 2.0, w = -54.7 "
                       "ms: it reads the character gaps of EE TT (144 ms) as element spaces (157 ms) and the word gaps "
                       "as character gaps, an ambiguity this text cannot resolve, favored by the element-space prior "
                       "(0.647 against 0.238, about 1 nat per space). K's first dah (143 ms) then reads as a dit under "
                       "T = 128 ms before the fit recovers (T = 47.9 ms by 13.26 s). Update (Task 14, E4, N_mem = 48): "
                       "the text reads 'CCQ DE K1ABC K EE TT EE TT K1ABC'; the second over is now correct and the test "
                       "fails only on the first over's spurious leading 'C' (the finding of the test above). [port and "
                       "prototype now read '" + got + "']");
}

TEST(BankChannelPrototypeTests, NoiseAfterTheLastOverIsCorrectedAwayAndOneOverStartPerOver) {
    const ChannelResult& r = result_of("turnover");
    const auto& in = golden().at("test_inputs");
    const double first_end = in.at("turnover_first_end_s").get<double>();
    const double gap = in.at("turnover_gap_s").get<double>();
    EXPECT_TRUE(ends_with(norm(r.text), "1ABC")) << r.text;
    ASSERT_EQ(r.over_starts.size(), 1u);
    EXPECT_LT(first_end, r.over_starts[0]);
    EXPECT_LT(r.over_starts[0], 1.0 + gap);
    EXPECT_TRUE(std::any_of(r.corrections.begin(), r.corrections.end(),
                            [](const Correction& c) { return c.reason == "timeout"; }));
}

TEST(BankChannelPrototypeTests, ASpeedChangeAcrossATurnoverIsTakenUp) {
    const ChannelResult& r = result_of("speed_turnover");
    const auto lengths = BankChannel(BankConfig{}, 1500.0).lengths_s();
    ASSERT_FALSE(r.selections.empty());
    const Selection& last = r.selections.back();
    EXPECT_LE(std::abs(std::log(lengths[static_cast<std::size_t>(last.branch)] / (0.8 * 1.2 / 30.0))), std::log(1.1));
    EXPECT_LT(std::abs(last.t_dit_s / 0.040 - 1.0), 0.05);
    const std::string t = norm(r.text);
    EXPECT_TRUE(starts_with(t, "CQ DE K1ABC K ")) << t;
    EXPECT_TRUE(ends_with(t, "DE W9XYZ W9XYZ K")) << t;
    EXPECT_EQ(r.over_starts.size(), 1u);
}

// ---- The default configuration ---------------------------------------------------------------------------------

// Review Focus 1 at the default configuration (stage 1's re-key wait 0.8 s and time-out 2 s; the periodicity windows 5
// and 10 s; Plan B's guard margin and mask-bias table): the split into pushes does not matter.
class ChannelSplitDefaults : public ::testing::TestWithParam<std::string> {};

TEST_P(ChannelSplitDefaults, PiecesOf1And47And1000SamplesGiveTheSingleResult) {
    const auto& u = stream_of(GetParam());
    const BankConfig cfg;
    const ChannelResult whole = decode(u, rate_of(GetParam()), 0, cfg);
    expect_published_finite(whole);
    for (std::size_t piece : {std::size_t{1}, std::size_t{47}, std::size_t{1000}}) {
        SCOPED_TRACE("pieces of " + std::to_string(piece) + " samples");
        expect_identical(decode(u, rate_of(GetParam()), piece, cfg), whole);
    }
}

INSTANTIATE_TEST_SUITE_P(Streams, ChannelSplitDefaults, ::testing::Values("clean", "farnsworth", "zero_pad"),
                         [](const auto& info) { return info.param; });

}  // namespace
