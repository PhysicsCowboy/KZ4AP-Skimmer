#include "kz4ap/classical_decoder.hpp"

#include "test_signals.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace kz4ap;
using kz4ap::test::duration_for;
using kz4ap::test::keyed_signal;
using kz4ap::test::keying;

namespace {

constexpr double kRate = 1500.0;

std::vector<DecodedSymbol> decode_all(ClassicalDecoder& d, const std::vector<Sample>& x, std::size_t chunk = 256) {
    std::vector<DecodedSymbol> chars;
    for (std::size_t i = 0; i < x.size(); i += chunk) {
        const auto n = std::min(chunk, x.size() - i);
        auto u = d.process(std::span<const Sample>(x).subspan(i, n), static_cast<double>(i) / kRate);
        chars.insert(chars.end(), u.chars.begin(), u.chars.end());
    }
    auto u = d.flush();
    chars.insert(chars.end(), u.chars.begin(), u.chars.end());
    return chars;
}

// Uppercase text with runs of spaces collapsed and ends trimmed.
std::string text(const std::vector<DecodedSymbol>& chars) {
    std::string out;
    for (const auto& c : chars) {
        if (c.text == " " && (out.empty() || out.back() == ' ')) continue;
        out += c.text;
    }
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}

bool ends_with(const std::string& s, const std::string& suffix) {
    return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

// The milestone-1 decoder (the Matched front end is the default from milestone 2 on).
ClassicalDecoderConfig envelope() {
    ClassicalDecoderConfig c;
    c.front_end = FrontEnd::Envelope;
    return c;
}

}  // namespace

TEST(ClassicalDecoder, DecodesCleanSignal) {
    ClassicalDecoder d(kRate, envelope());
    const std::string msg = "CQ TEST K1ABC";
    EXPECT_EQ(text(decode_all(d, keyed_signal(msg, 25, kRate, duration_for(msg, 25)))), msg);
    EXPECT_NEAR(d.wpm(), 25.0, 2.5);
}

TEST(ClassicalDecoder, CleanSignalHasHighProbabilities) {
    ClassicalDecoder d(kRate, envelope());
    const std::string msg = "CQ TEST K1ABC";
    for (const auto& c : decode_all(d, keyed_signal(msg, 25, kRate, duration_for(msg, 25)))) {
        if (c.text != " ") EXPECT_GT(c.probability, 0.9f) << c.text;
    }
}

TEST(ClassicalDecoder, ReportsCharacterTimes) {
    ClassicalDecoder d(kRate, envelope());
    const auto chars = decode_all(d, keyed_signal("E", 25, kRate, 2.0));
    ASSERT_FALSE(chars.empty());
    EXPECT_EQ(chars[0].text, "E");
    EXPECT_NEAR(chars[0].start_s, 0.5, 0.02);
    EXPECT_NEAR(chars[0].end_s, 0.5 + 0.048, 0.02);
}

TEST(ClassicalDecoder, DecodesWithModerateNoise) {
    // 15 dB SNR in 500 Hz: white noise over 1500 Hz with total power 3 * 10^(-1.5).
    ClassicalDecoder d(kRate, envelope());
    const std::string msg = "CQ TEST K1ABC";
    const auto x = keyed_signal(msg, 25, kRate, duration_for(msg, 25), 0, 1.0, 0.31, 7);
    EXPECT_EQ(text(decode_all(d, x)), msg);
}

TEST(ClassicalDecoder, NoiseAloneDecodesNothing) {
    ClassicalDecoder d(kRate, envelope());
    const auto chars = decode_all(d, keyed_signal("", 25, kRate, 10.0, 0, 0.0, 0.3, 3));
    EXPECT_EQ(text(chars), "");
}

TEST(ClassicalDecoder, AdaptsToSlowSpeed) {
    ClassicalDecoder d(kRate, envelope());
    const std::string msg = "PARIS PARIS CQ K1ABC";
    const auto got = text(decode_all(d, keyed_signal(msg, 12, kRate, duration_for(msg, 12))));
    EXPECT_TRUE(ends_with(got, "CQ K1ABC")) << got;
    EXPECT_NEAR(d.wpm(), 12.0, 1.2);
}

TEST(ClassicalDecoder, AdaptsToFastSpeed) {
    ClassicalDecoder d(kRate, envelope());
    const std::string msg = "PARIS PARIS CQ K1ABC";
    const auto got = text(decode_all(d, keyed_signal(msg, 45, kRate, duration_for(msg, 45))));
    EXPECT_TRUE(ends_with(got, "CQ K1ABC")) << got;
    EXPECT_NEAR(d.wpm(), 45.0, 4.5);
}

TEST(ClassicalDecoder, PauseDecodesNothingThenResumes) {
    ClassicalDecoder d(kRate, envelope());
    auto x = keyed_signal("CQ K1ABC", 25, kRate, 14.5, 0, 1.0, 0.2, 5, 0.5);
    const auto later = keyed_signal("TU", 25, kRate, 14.5, 0, 1.0, 0.0, 1, 12.0);
    for (std::size_t i = 0; i < x.size(); ++i) x[i] += later[i];
    EXPECT_EQ(text(decode_all(d, x)), "CQ K1ABC TU");
}

namespace {

// Baseband keyed with the given element lengths (in dits), one dit apart, at 25 wpm.
std::vector<Sample> keyed_elements(const std::vector<int>& dits) {
    std::vector<Sample> x(static_cast<std::size_t>(3.0 * kRate));
    const double dit = 1.2 / 25;
    double t = 0.5;
    for (int n : dits) {
        const auto i0 = static_cast<std::size_t>(t * kRate);
        const auto i1 = static_cast<std::size_t>((t + n * dit) * kRate);
        for (std::size_t i = i0; i < i1; ++i) x[i] = Sample(1.0f, 0.0f);
        t += (n + 1) * dit;
    }
    return x;
}

}  // namespace

TEST(ClassicalDecoder, UnknownPatternBecomesAsterisk) {
    ClassicalDecoder d(kRate, envelope());
    // ..-- (U-umlaut) is not in the English-only table.
    const auto chars = decode_all(d, keyed_elements({1, 1, 3, 3}));
    ASSERT_FALSE(chars.empty());
    EXPECT_EQ(chars[0].text, "*");
    EXPECT_EQ(chars[0].probability, 0.0f);
}

TEST(ClassicalDecoder, EightDitsDecodeAsErrorSignal) {
    ClassicalDecoder d(kRate, envelope());
    const auto chars = decode_all(d, keyed_elements({1, 1, 1, 1, 1, 1, 1, 1}));
    ASSERT_FALSE(chars.empty());
    EXPECT_EQ(chars[0].text, "<HH>");
}

TEST(ClassicalDecoder, DecodesProsigns) {
    ClassicalDecoder d(kRate, envelope());
    const std::string msg = "CQ DE K1ABC <KN>";
    EXPECT_EQ(text(decode_all(d, keyed_signal(msg, 25, kRate, duration_for(msg, 25)))), msg);
}

TEST(ClassicalDecoder, ChunkSizeDoesNotChangeOutput) {
    const std::string msg = "CQ TEST K1ABC";
    const auto x = keyed_signal(msg, 25, kRate, duration_for(msg, 25), 0, 1.0, 0.31, 7);
    ClassicalDecoder a(kRate), b(kRate);
    const auto ca = decode_all(a, x, 7);
    const auto cb = decode_all(b, x, 1000);
    ASSERT_EQ(ca.size(), cb.size());
    for (std::size_t i = 0; i < ca.size(); ++i) {
        EXPECT_EQ(ca[i].text, cb[i].text);
        EXPECT_EQ(ca[i].probability, cb[i].probability);
        EXPECT_EQ(ca[i].start_s, cb[i].start_s);
    }
}

TEST(ClassicalDecoder, ResetRestoresInitialSpeed) {
    ClassicalDecoder d(kRate, envelope());
    const std::string msg = "PARIS PARIS";
    decode_all(d, keyed_signal(msg, 45, kRate, duration_for(msg, 45)));
    d.reset();
    EXPECT_DOUBLE_EQ(d.wpm(), 25.0);
}

TEST(ClassicalDecoder, RejectsInvalidConfig) {
    EXPECT_THROW(ClassicalDecoder d(0.0, envelope()), std::invalid_argument);
    EXPECT_THROW(ClassicalDecoder d(-1500.0, envelope()), std::invalid_argument);
    auto bad = [](auto mutate) {
        auto c = envelope();
        mutate(c);
        EXPECT_THROW(ClassicalDecoder d(kRate, c), std::invalid_argument);
    };
    bad([](ClassicalDecoderConfig& c) { c.initial_wpm = 0; });
    bad([](ClassicalDecoderConfig& c) { c.initial_wpm = -25; });
    bad([](ClassicalDecoderConfig& c) { c.min_wpm = 0; });
    bad([](ClassicalDecoderConfig& c) { c.min_wpm = -5; });
    bad([](ClassicalDecoderConfig& c) { c.max_wpm = 0; });
    bad([](ClassicalDecoderConfig& c) { c.max_wpm = -60; });
    bad([](ClassicalDecoderConfig& c) { c.min_wpm = 30; c.max_wpm = 20; c.initial_wpm = 25; });
    bad([](ClassicalDecoderConfig& c) { c.initial_wpm = 4; });
    bad([](ClassicalDecoderConfig& c) { c.initial_wpm = 61; });
    bad([](ClassicalDecoderConfig& c) { c.attack_s = 0; });
    bad([](ClassicalDecoderConfig& c) { c.attack_s = -0.004; });
    bad([](ClassicalDecoderConfig& c) { c.decay_s = 0; });
    bad([](ClassicalDecoderConfig& c) { c.decay_s = -3; });
    bad([](ClassicalDecoderConfig& c) { c.squelch_ratio = 1.0f; });
    bad([](ClassicalDecoderConfig& c) { c.squelch_ratio = 0.5f; });
    bad([](ClassicalDecoderConfig& c) { c.smoothing_dits = 0; });
    bad([](ClassicalDecoderConfig& c) { c.smoothing_dits = -0.25; });
    bad([](ClassicalDecoderConfig& c) { c.glitch_dits = -0.1; });
    // Boundary values that are valid.
    auto ok = envelope();
    ok.glitch_dits = 0;
    ok.min_wpm = ok.max_wpm = ok.initial_wpm = 25;
    EXPECT_NO_THROW(ClassicalDecoder d(kRate, ok));
}

TEST(ClassicalDecoder, TimeJumpBetweenChunksRestartsTheClock) {
    ClassicalDecoder d(kRate, envelope());
    const auto x = keyed_signal("E", 25, kRate, 2.0);  // E keyed at 0.5 s
    const auto split = static_cast<std::size_t>(0.25 * kRate);
    const auto first = d.process(std::span<const Sample>(x).first(split), 0.0);
    // The rest is stamped 100 s later than where the first chunk ended.
    const auto second = d.process(std::span<const Sample>(x).subspan(split), 100.25);
    EXPECT_TRUE(first.chars.empty());
    ASSERT_FALSE(second.chars.empty());
    EXPECT_EQ(second.chars[0].text, "E");
    EXPECT_NEAR(second.chars[0].start_s, 100.5, 0.02);
}

TEST(ClassicalDecoder, FlushDuringMarkAfterDropoutEmitsNothing) {
    // A dit-long mark, a 12 ms dropout (long enough to key up, short enough to be
    // merged back as a glitch), then key-down until the input ends mid-mark.
    ClassicalDecoder d(kRate, envelope());
    std::vector<Sample> x(static_cast<std::size_t>(0.7 * kRate));
    for (std::size_t i = static_cast<std::size_t>(0.5 * kRate); i < x.size(); ++i) x[i] = Sample(1.0f, 0.0f);
    for (std::size_t i = static_cast<std::size_t>(0.548 * kRate); i < static_cast<std::size_t>(0.560 * kRate); ++i)
        x[i] = Sample(0.0f, 0.0f);
    EXPECT_TRUE(d.process(x, 0.0).chars.empty());
    // No element has been completed, so there is no character to emit.
    EXPECT_TRUE(d.flush().chars.empty());
}

namespace {

// Hand-keyed baseband: text at wpm with dahs of dah_dits dits (standard 1/3/7 dit
// spacing) and hard key edges, beginning at 0.5 s.
std::vector<Sample> hand_keyed(const std::string& msg, double wpm, double dah_dits, double tail_s = 1.5) {
    const double dit = 1.2 / wpm;
    std::vector<std::pair<double, double>> marks;
    double t = 0.5;
    std::size_t start = 0;
    while (start < msg.size()) {
        auto end = msg.find(' ', start);
        if (end == std::string::npos) end = msg.size();
        const auto symbols = morse::symbols(std::string_view(msg).substr(start, end - start));
        for (std::size_t si = 0; si < symbols.size(); ++si) {
            const auto code = morse::encode(symbols[si]);
            for (std::size_t ei = 0; ei < code.size(); ++ei) {
                const double len = code[ei] == '.' ? dit : dah_dits * dit;
                marks.emplace_back(t, t + len);
                t += len + (ei + 1 < code.size() ? dit : 0.0);
            }
            if (si + 1 < symbols.size()) t += 3 * dit;
        }
        t += 7 * dit;
        start = end + 1;
    }
    std::vector<Sample> x(static_cast<std::size_t>((t + tail_s) * kRate));
    for (auto [on, off] : marks) {
        for (auto i = static_cast<std::size_t>(on * kRate); i < static_cast<std::size_t>(off * kRate); ++i)
            x[i] = Sample(1.0f, 0.0f);
    }
    return x;
}

}  // namespace

TEST(ClassicalDecoder, TuneUpCarrierDoesNotDerailSpeed) {
    // A 1 s tune-up carrier (longer than a four-dit dah at the 5 wpm minimum),
    // then CW at 25 wpm from 2 s.
    ClassicalDecoder d(kRate, envelope());
    const std::string msg = "CQ TEST K1ABC DE K1ABC K";
    auto x = keyed_signal(msg, 25, kRate, kz4ap::test::keying(msg, 25, 2.0).back().second + 1.5, 0, 1.0, 0.0, 1, 2.0);
    for (auto i = static_cast<std::size_t>(0.5 * kRate); i < static_cast<std::size_t>(1.5 * kRate); ++i)
        x[i] = Sample(1.0f, 0.0f);
    const auto got = text(decode_all(d, x, 32));
    EXPECT_TRUE(ends_with(got, msg)) << got;
    EXPECT_NEAR(d.wpm(), 25.0, 2.5);
}

TEST(ClassicalDecoder, DecodesHeavyHandKeying) {
    // 20 wpm with four-dit dahs, as a heavy-handed operator might send.
    ClassicalDecoder d(kRate, envelope());
    EXPECT_EQ(text(decode_all(d, hand_keyed("CQ TEST K1ABC", 20, 4.0), 32)), "CQ TEST K1ABC");
}

namespace {

ClassicalDecoderConfig matched() {
    ClassicalDecoderConfig c;
    c.front_end = FrontEnd::Matched;
    return c;
}

// Noise RMS (total, complex) giving s500_db for a unit-amplitude carrier in a
// white 1500 samples/s stream: S500 = A^2 / (sigma^2 * 500 Hz / 1500 Hz).
double sigma_for_s500(double s500_db) { return std::sqrt(3.0 / std::pow(10.0, s500_db / 10.0)); }

std::vector<Sample> add(std::vector<Sample> a, const std::vector<Sample>& b) {
    for (std::size_t i = 0; i < a.size() && i < b.size(); ++i) a[i] += b[i];
    return a;
}

// Owner decision 2026-09-29: a test whose simulated pass rate is below 100% runs 20 fixed seeds
// and asserts a pass count t, the smallest with P(X < t) <= 0.002 for X ~ Binomial(20, simulated
// rate). Deterministic, and still catches a regression. `run` returns true on a pass and may
// describe a failure in `why`; seeds are first_seed, first_seed + 10, ... (each run may use
// seed, seed + 1, seed + 2).
constexpr unsigned kSeeds = 20;

struct Tally {
    int passed = 0;
    std::string failures;
};

template <class Run>
Tally count_passes(unsigned first_seed, Run&& run) {
    Tally tally;
    for (unsigned i = 0; i < kSeeds; ++i) {
        const unsigned seed = first_seed + 10 * i;
        std::string why;
        if (run(seed, why)) ++tally.passed;
        else tally.failures += "seed " + std::to_string(seed) + ": " + why + "\n";
    }
    return tally;
}

// K (samples) and the count of marks for speed, read just before the sample at each physical mark's
// keyed start (keying() times), and once more after the input: between one mark's last key event and
// the next mark's first. At 12 WPM a mark's key-up, and any dropout merge after it (within 0.3 dit),
// come before the next mark's start, one dit (100 ms) after the mark's end (derived: the key-up lags the
// mark's end by about half the filter, at most 0.4 dit).
struct BetweenMarks {
    std::vector<int> k;
    std::vector<std::size_t> counted;
    std::vector<DecodedSymbol> chars;
};

BetweenMarks decode_between_marks(ClassicalDecoder& d, const std::vector<Sample>& x,
                                  const std::vector<std::pair<double, double>>& marks) {
    BetweenMarks out;
    std::size_t next = 0;
    for (std::size_t i = 0; i < x.size(); ++i) {
        if (next < marks.size() && i == static_cast<std::size_t>(std::ceil(marks[next].first * kRate))) {
            out.k.push_back(d.filter_length());
            out.counted.push_back(d.marks_since_reacquire());
            ++next;
        }
        auto u = d.process(std::span<const Sample>(x).subspan(i, 1), static_cast<double>(i) / kRate);
        out.chars.insert(out.chars.end(), u.chars.begin(), u.chars.end());
    }
    auto f = d.flush();
    out.chars.insert(out.chars.end(), f.chars.begin(), f.chars.end());
    out.k.push_back(d.filter_length());
    out.counted.push_back(d.marks_since_reacquire());
    return out;
}

}  // namespace

TEST(ClassicalDecoder, EnvelopeModeTracksNoFrequency) {
    ClassicalDecoder d(kRate, envelope(), 3.0);
    const auto u = d.process(keyed_signal("E", 25, kRate, 1.0), 0.0);
    EXPECT_FALSE(u.freq_offset_hz.has_value());
    EXPECT_EQ(d.filter_length(), 0);
    EXPECT_EQ(d.frequency_offset_hz(), 3.0);
    d.set_frequency_anchor_hz(20.0);  // ignored on the Envelope path (bit-identical to milestone 1)
    EXPECT_EQ(d.frequency_offset_hz(), 3.0);
}

TEST(ClassicalDecoder, MatchedFollowsTheAnchorItIsGiven) {
    // Option 1 (owner decisions 2026-09-29): the detector decides where the station is; the engine
    // passes it as the anchor before every block. An anchor 30 Hz from the NCO (more than the
    // tracker's 12 Hz) moves the NCO there at once, so a station at 30 Hz decodes as if the decoder
    // had started there (the case of MatchedDecodesCleanSignal, derived; not separately simulated).
    ClassicalDecoder d(kRate, matched(), 0.0);
    d.set_frequency_anchor_hz(30.0);
    EXPECT_EQ(d.frequency_offset_hz(), 30.0);
    const std::string msg = "CQ TEST K1ABC";
    EXPECT_EQ(text(decode_all(d, keyed_signal(msg, 25, kRate, duration_for(msg, 25), 30.0, 1.0,
                                              sigma_for_s500(30), 43))), msg);
    EXPECT_NEAR(d.frequency_offset_hz(), 30.0, 2.0);
}

TEST(ClassicalDecoder, MatchedFilterGrowsAtMostTheBoundPerMark) {
    // Owner decision 3 of option 1 (2026-09-29): the x1.25 growth bound applies from the filter's
    // first follow step, per mark. Checked per physical mark (Task 15): K is read between marks, at
    // each mark's keyed start, so every update within one mark (a dropout merge and its re-measure)
    // counts as one step. A clean 12 WPM station (T = 100 ms): the decoder's estimate is near 100 ms
    // after 7 unbounded marks, but the filter must grow from the 20 ms acquisition dit (K = 24) by at
    // most x1.25 per mark: 24, 30, 38, 47, 59, 73, 92, 114, 120 samples (derived), each step at most
    // 1.25 K + 1.125 (both K rounded). Before Task 15 the bound held per speed update only, and a
    // widening that re-opened the mark could apply it several times within one mark (Task 14).
    // Deterministic apart from the noise at S500 = 30 dB; not simulated.
    const std::string msg = "PARIS PARIS PARIS";
    const auto marks = keying(msg, 12, 0.5);
    const auto x = keyed_signal(msg, 12, kRate, marks.back().second + 0.3, 0, 1.0, sigma_for_s500(30), 44);
    ClassicalDecoder d(kRate, matched());
    const auto got = decode_between_marks(d, x, marks);
    int steps = 0;
    for (std::size_t m = 0; m + 1 < got.k.size(); ++m) {
        if (got.k[m + 1] > got.k[m]) ++steps;
        EXPECT_LE(got.k[m + 1], 1.25 * got.k[m] + 1.125) << "mark " << m << ": K " << got.k[m] << " -> " << got.k[m + 1];
    }
    EXPECT_GE(steps, 7);  // ln(100 ms / 20 ms) / ln 1.25 = 7.2 (derived)
    EXPECT_NEAR(got.k.back(), std::lround(0.8 * 0.1 * kRate), 12);
    EXPECT_TRUE(ends_with(text(got.chars), "PARIS")) << text(got.chars);
}

TEST(ClassicalDecoder, MatchedMergedDropoutIsOneMarkForSpeed) {
    // Task 15. A dropout merged back into its mark must not update the speed twice: the count of
    // marks for speed rises by at most 1 per physical mark, and K by at most x1.25 (+1.125 for
    // rounding both K). A clean 12 WPM station (T = 100 ms) at S500 = 30 dB, with a 20 ms hard dropout
    // in the middle of every dah from the sixth mark (A's dah) on. "PAR MMM ...": the filter starts
    // following after 8 or 9 marks (Task 16 may leave the first one uncounted), so the dahs of MMM
    // arrive while K <= 59 samples. Derived for a strong signal (Lambda ~ a^2 (x/a - 1/2)): the key
    // goes up where the boxcar holds less than half the carrier, so a 20 ms (30-sample) gap keys it
    // up while K < 60, and the key goes down again 20 ms later, within 0.3 dit (30 ms at a 100 ms
    // estimate), so the decoder merges it. Before Task 15, A's and R's dahs (marks 5 and 7) count
    // twice, and a merged dah while following grows K by up to 1.25^2 (derived from the code).
    // Deterministic apart from the noise; not simulated.
    const std::string msg = "PAR MMM PARIS PARIS";
    const auto marks = keying(msg, 12, 0.5);
    const double duration = marks.back().second + 0.3;
    auto x = keyed_signal(msg, 12, kRate, duration, 0, 1.0, 0.0);
    for (std::size_t m = 5; m < marks.size(); ++m) {
        const auto [on, off] = marks[m];
        if (off - on < 0.2) continue;  // dits are 0.1 s, dahs 0.3 s
        const double mid = 0.5 * (on + off);
        for (auto i = static_cast<std::size_t>((mid - 0.010) * kRate); i < static_cast<std::size_t>((mid + 0.010) * kRate); ++i)
            x[i] = Sample(0.0f, 0.0f);
    }
    x = add(std::move(x), keyed_signal("", 12, kRate, duration, 0, 1.0, sigma_for_s500(30), 47));  // noise only
    ClassicalDecoder d(kRate, matched());
    const auto got = decode_between_marks(d, x, marks);
    for (std::size_t m = 0; m + 1 < got.k.size(); ++m) {
        EXPECT_LE(got.counted[m + 1], got.counted[m] + 1)
            << "mark " << m << ": count " << got.counted[m] << " -> " << got.counted[m + 1];
        EXPECT_LE(got.k[m + 1], 1.25 * got.k[m] + 1.125) << "mark " << m << ": K " << got.k[m] << " -> " << got.k[m + 1];
    }
    EXPECT_TRUE(ends_with(text(got.chars), "PARIS")) << text(got.chars);
}

TEST(ClassicalDecoder, MatchedChannelOpeningInsideADahCountsNoFragment) {
    // Task 14 (docs/plans/2026-09-27-milestone-2a-results.md 8.3, "Limits measured"): on the
    // smoke recording an 18.5 WPM station (T = 64.9 ms) at S500 = 25.1 dB, whose channel opened so that
    // the front end's 0.32 s warm-up ended inside the dah of U, timed a 15.3 ms fragment as its first
    // mark; the speed estimate took it as the whole dit cluster (dit clamped to 20 ms) and garbled the
    // next words until it left the 24-mark window. Here the input starts at cut times such that the
    // warm-up ends lead_s before U's dah ends, lead_s = -10 ... +40 ms in 1 ms steps. A fragment of
    // 14.4 ms (the glitch limit, 0.3 x the 48 ms initial dit) to 21.6 ms (a third of the 64.9 ms dit,
    // below which 65 ms / fragment exceeds the dit-dah ratio of 3 and the fragment is a cluster of its
    // own; derived from section 8, step 10) derails the estimate before Task 16. After it the fragment
    // is decoded (as E) but not counted for speed, so every word after the first, which the cut
    // shortens, decodes exactly. Deterministic; not simulated.
    const double wpm = 18.5;
    const std::string msg = "UA7L TU UA7L TU UA7L K";
    const auto marks = keying(msg, wpm, 0.5);
    const double dah_end = marks[2].second;  // U is ..-: its dah is the third mark
    const auto x = keyed_signal(msg, wpm, kRate, marks.back().second + 1.5, 0, 1.0, sigma_for_s500(25), 45);
    int fragments = 0;
    std::string failures;
    for (int i = 0; i <= 50; ++i) {
        const double lead_s = -0.010 + 0.001 * i;
        const auto first = static_cast<std::ptrdiff_t>(std::lround((dah_end - 0.32 - lead_s) * kRate));
        const std::vector<Sample> y(x.begin() + first, x.end());
        ClassicalDecoder d(kRate, matched());
        const auto chars = decode_all(d, y);
        const std::string got = text(chars);
        std::string first_e = "none";
        if (!chars.empty() && chars[0].text == "E") {
            const double len = chars[0].end_s - chars[0].start_s;
            first_e = std::to_string(len * 1000.0) + " ms";
            if (len > 0.3 * 1.2 / 25 && len < 1.2 / wpm / 3) ++fragments;
        }
        if (!ends_with(got, " TU UA7L TU UA7L K"))
            failures += "lead " + std::to_string(lead_s * 1000.0) + " ms, first E " + first_e + ": " + got + "\n";
    }
    EXPECT_GE(fragments, 1) << "no cut timed a 14.4-21.6 ms fragment: the case is not exercised";
    EXPECT_TRUE(failures.empty()) << failures;
}

TEST(ClassicalDecoder, MatchedFirstMarkAfterReacquisitionIsNotCounted) {
    // Task 16, the squelch closing and re-opening mid-stream. After a re-acquisition the amplitude
    // estimate restarts at 0 (a = 0 < a_min), which closes the squelch and clears the decoder's
    // key-up evidence (no re-acquisition-specific code); the next station's first mark lifts s-hat and
    // opens the squelch, and is keyed with no key-up sample (g < -1 nat) seen since, so its start was not
    // observed (keyed early on its ramp at this S500, derived from the code, appendix A.8b,
    // "Re-acquisition"). It is decoded but not counted for speed. 1.5 s of silence at 25 WPM: the
    // re-acquisition comes 0.576 s after the last key-up (12 dits), and the second over starts within
    // its 2 s window, so nothing set aside comes back. "K1ABC" has 18 marks; 17 count. Noise alone could
    // open the squelch in the silence first (then 18); not simulated: if this reads 18, report it with
    // the seed rather than changing the seed.
    const std::string first = "CQ TEST";
    const std::string second = "K1ABC";
    const double start2 = keying(first, 25, 0.5).back().second + 1.5;
    const double end2 = keying(second, 25, start2).back().second;
    const auto x = add(keyed_signal(first, 25, kRate, end2 + 0.3, 0, 1.0, sigma_for_s500(30), 48),
                       keyed_signal(second, 25, kRate, end2 + 0.3, 0, 1.0, 0.0, 49, start2));
    ClassicalDecoder d(kRate, matched());
    EXPECT_EQ(text(decode_all(d, x)), first + " " + second);
    EXPECT_EQ(d.marks_since_reacquire(), 17u);
}

TEST(ClassicalDecoder, MatchedDecodesCleanSignal) {
    ClassicalDecoder d(kRate, matched());
    const std::string msg = "CQ TEST K1ABC";
    EXPECT_EQ(text(decode_all(d, keyed_signal(msg, 25, kRate, duration_for(msg, 25), 0, 1.0,
                                              sigma_for_s500(30), 21))), msg);
}

TEST(ClassicalDecoder, MatchedDecodesAtThreeDbS500) {
    // Simulated (whole decoder, numpy seeds): "K1ABC DE W9XYZ" found in 81 of 100 (final check) and
    // 79 of 100 (2026-09-29, with the growth bound); the failures are errors in the first words
    // while the speed estimate and the filter settle. 20 seeds; at 0.79, t = 10. Not re-simulated
    // with the first-step growth bound: if below 10, stop and report (Task 12 intro).
    const std::string msg = "CQ TEST K1ABC DE W9XYZ";
    const auto tally = count_passes(22, [&](unsigned seed, std::string& why) {
        ClassicalDecoder d(kRate, matched());
        why = text(decode_all(d, keyed_signal(msg, 25, kRate, duration_for(msg, 25), 0, 1.0,
                                              sigma_for_s500(3), seed)));
        return why.find("K1ABC DE W9XYZ") != std::string::npos;
    });
    EXPECT_GE(tally.passed, 10) << tally.failures;
}

TEST(ClassicalDecoder, MatchedDecodesStrongSignal) {
    ClassicalDecoder d(kRate, matched());
    const std::string msg = "CQ TEST K1ABC";
    EXPECT_EQ(text(decode_all(d, keyed_signal(msg, 25, kRate, duration_for(msg, 25), 0, 1.0,
                                              sigma_for_s500(60), 23))), msg);
}

TEST(ClassicalDecoder, MatchedFollowsSpeedChange) {
    // K is read 0.3 s after the last mark, before the 0.5 s re-acquisition silence puts it back to
    // 24 (final check F-2). Without the growth bound, 2 of 100 simulated seeds ran away (the dit
    // estimate jumped from 41 to 81 ms in one update, K 105). With the x1.25 bound (owner decision
    // 2026-09-29), simulated over 100 numpy seeds: K = 41 +/- 5 samples (27.3 ms) and the text ends
    // in K1ABC in 98; one ended with a stray E, one garbled the last call while K was still 61.
    // 20 seeds; at 0.98, t = 17. Not re-simulated with the first-step growth bound: if below 17,
    // stop and report (Task 12 intro).
    const std::string first = "CQ CQ CQ";
    const std::string second = "TEST K1ABC K1ABC";
    const double start2 = keying(first, 20, 0.5).back().second + 7 * 1.2 / 20;
    const double end2 = keying(second, 35, start2).back().second;
    const double total = end2 + 1.5;
    const auto tally = count_passes(24, [&](unsigned seed, std::string& why) {
        const auto x = add(keyed_signal(first, 20, kRate, total, 0, 1.0, sigma_for_s500(20), seed),
                           keyed_signal(second, 35, kRate, total, 0, 1.0, 0.0, seed + 1, start2));
        ClassicalDecoder d(kRate, matched());
        const auto split = static_cast<std::size_t>((end2 + 0.3) * kRate);
        std::vector<DecodedSymbol> chars;
        auto a = d.process(std::span<const Sample>(x).subspan(0, split), 0.0);
        chars.insert(chars.end(), a.chars.begin(), a.chars.end());
        const int k = d.filter_length();
        auto b = d.process(std::span<const Sample>(x).subspan(split), static_cast<double>(split) / kRate);
        chars.insert(chars.end(), b.chars.begin(), b.chars.end());
        auto f = d.flush();
        chars.insert(chars.end(), f.chars.begin(), f.chars.end());
        why = text(chars) + " (K " + std::to_string(k) + ")";
        return ends_with(text(chars), "K1ABC") && std::abs(k - std::lround(0.8 * 1.2 / 35 * kRate)) <= 5;
    });
    EXPECT_GE(tally.passed, 17) << tally.failures;
}

TEST(ClassicalDecoder, MatchedTracksTheResidualOffset) {
    // Spec 5.2 target: within 2 Hz, starting 11 Hz off, as bin rounding alone can leave a station.
    // The station is 11 Hz from the anchor (0 Hz), 1 Hz inside the tracker's +/-12 Hz: a noisy early
    // estimate beyond 12 Hz is rejected (average emptied, NCO back to 0 Hz) and the average rebuilds;
    // such transient rejections are possible and harmless to the final assertion.
    const std::string msg = "PARIS PARIS PARIS PARIS PARIS PARIS";
    ClassicalDecoder d(kRate, matched(), 0.0);
    const auto t = text(decode_all(d, keyed_signal(msg, 20, kRate, duration_for(msg, 20), 11.0, 1.0,
                                                   sigma_for_s500(10), 26)));
    EXPECT_NEAR(d.frequency_offset_hz(), 11.0, 2.0);
    EXPECT_NE(t.find("PARIS PARIS"), std::string::npos) << t;
}

TEST(ClassicalDecoder, MatchedHoldsThroughPause) {
    const std::string msg = "CQ TEST K1ABC";
    const double end1 = keying(msg, 25, 0.5).back().second;
    const double start2 = end1 + 10.0;
    const double total = keying(msg, 25, start2).back().second + 1.5;
    const auto x = add(keyed_signal(msg, 25, kRate, total, 6.0, 1.0, sigma_for_s500(20), 27),
                       keyed_signal(msg, 25, kRate, total, 6.0, 1.0, 0.0, 28, start2));
    ClassicalDecoder d(kRate, matched());
    const auto split = static_cast<std::size_t>((start2 - 0.1) * kRate);
    std::vector<DecodedSymbol> chars;
    for (std::size_t i = 0; i < x.size(); i += 256) {
        const auto n = std::min<std::size_t>(256, x.size() - i);
        if (i <= split && split < i + n) {
            // Process up to the resume point, then compare the estimate with the one before the pause.
            auto a = d.process(std::span<const Sample>(x).subspan(i, split - i), static_cast<double>(i) / kRate);
            chars.insert(chars.end(), a.chars.begin(), a.chars.end());
            const double after_pause = d.frequency_offset_hz();
            EXPECT_NEAR(after_pause, 6.0, 2.0);
            auto b = d.process(std::span<const Sample>(x).subspan(split, i + n - split), static_cast<double>(split) / kRate);
            chars.insert(chars.end(), b.chars.begin(), b.chars.end());
            continue;
        }
        auto u = d.process(std::span<const Sample>(x).subspan(i, n), static_cast<double>(i) / kRate);
        chars.insert(chars.end(), u.chars.begin(), u.chars.end());
    }
    auto f = d.flush();
    chars.insert(chars.end(), f.chars.begin(), f.chars.end());
    EXPECT_EQ(text(chars), msg + " " + msg);
    EXPECT_NEAR(d.frequency_offset_hz(), 6.0, 2.0);
}

TEST(ClassicalDecoder, MatchedNoiseAfterStationStopsDecodesNothing) {
    const std::string msg = "CQ TEST K1ABC";
    const double end = keying(msg, 25, 0.5).back().second;
    ClassicalDecoder d(kRate, matched());
    const auto chars = decode_all(d, keyed_signal(msg, 25, kRate, end + 10.0, 0, 1.0, sigma_for_s500(20), 29));
    EXPECT_EQ(text(chars), msg);
    for (const auto& c : chars) {
        if (c.text != " ") EXPECT_LT(c.start_s, end + 0.2) << c.text << " at " << c.start_s;
    }
}

TEST(ClassicalDecoder, MatchedIgnoresStrongerNeighbor) {
    // Wanted at 0 Hz, S500 = 15 dB; a neighbor 100 Hz away inside the same channel,
    // 10 dB stronger (re the wanted station's key-down power), at another speed, keying at the
    // same time. Simulated (100 numpy seeds): f-hat within 2 Hz in all 100; K1ABC decoded in 98
    // (final check's port) and 88 (the 2026-09-29 port, the same with the 35 Hz pull-in and without
    // the growth bound, so the difference is between the ports). 20 seeds; at 0.88, t = 13. Not
    // re-simulated with the first-step growth bound: if below 13, stop and report (Task 12 intro).
    const std::string msg = "CQ TEST K1ABC K1ABC K1ABC";
    const double total = duration_for(msg, 25);
    const auto tally = count_passes(30, [&](unsigned seed, std::string& why) {
        const auto x = add(keyed_signal(msg, 25, kRate, total, 0.0, 1.0, sigma_for_s500(15), seed),
                           keyed_signal("TU W9XYZ 5NN TU W9XYZ 5NN TU W9XYZ", 30, kRate, total, 100.0,
                                        std::sqrt(10.0), 0.0, seed + 1, 0.3));
        ClassicalDecoder d(kRate, matched(), 0.0);
        why = text(decode_all(d, x));
        why += " (f " + std::to_string(d.frequency_offset_hz()) + " Hz)";
        return std::abs(d.frequency_offset_hz()) <= 2.0 && why.find("K1ABC") != std::string::npos;
    });
    EXPECT_GE(tally.passed, 13) << tally.failures;
}

namespace {

// A (25 WPM, 0 Hz, S500 = 15 dB), 1 s of silence, B (18 WPM, offset_hz away, relative_db re A's
// key-down power), 1 s of silence, A again. Returns the text and f-hat just before A's second over.
// There is no detector here, so the tracker's anchor stays on A (0 Hz) throughout.
struct Turnover {
    std::string text;
    double f_before_second_over;
};

Turnover turnover(double offset_hz, double relative_db, unsigned seed) {
    const std::string a = "PARIS PARIS PARIS PARIS";
    const std::string b = "DE W9XYZ PARIS PARIS PARIS";
    const double b0 = keying(a, 25, 0.5).back().second + 1.0;
    const double a0 = keying(b, 18, b0).back().second + 1.0;
    const double total = keying(a, 25, a0).back().second + 1.5;
    auto x = add(keyed_signal(a, 25, kRate, total, 0.0, 1.0, sigma_for_s500(15), seed),
                 keyed_signal(b, 18, kRate, total, offset_hz, std::pow(10.0, relative_db / 20.0), 0.0, seed + 1, b0));
    x = add(x, keyed_signal(a, 25, kRate, total, 0.0, 1.0, 0.0, seed + 2, a0));
    ClassicalDecoder d(kRate, matched());
    const auto split = static_cast<std::size_t>((a0 - 0.1) * kRate);
    std::vector<DecodedSymbol> chars;
    auto first = d.process(std::span<const Sample>(x).subspan(0, split), 0.0);
    chars.insert(chars.end(), first.chars.begin(), first.chars.end());
    const double f_before = d.frequency_offset_hz();
    auto rest = d.process(std::span<const Sample>(x).subspan(split), static_cast<double>(split) / kRate);
    chars.insert(chars.end(), rest.chars.begin(), rest.chars.end());
    auto f = d.flush();
    chars.insert(chars.end(), f.chars.begin(), f.chars.end());
    return {text(chars), f_before};
}

}  // namespace

TEST(ClassicalDecoder, MatchedIgnoresAWeakStation50HzAway) {
    // B 50 Hz away, 10 dB weaker (re A's key-down power), is 12.6 dB further down at the 16 ms
    // acquisition width (|sinc(50 Hz * 16 ms)|^2, derived), so it is not keyed and leaves A alone.
    // Simulated with the first design (100 numpy seeds): f-hat within 0.2 Hz of A and A's second
    // over intact in 99; option 1's tracker (+/-12 Hz around an anchor held on A) accepts a subset of
    // what that tracker accepted, so the rate carries over (argued; Task 12 intro). Whether B's
    // station gets the channel is the detector's decision (Task 13). 20 seeds; t = 18.
    const auto tally = count_passes(36, [](unsigned seed, std::string& why) {
        const auto r = turnover(50.0, -10.0, seed);
        why = r.text + " (f " + std::to_string(r.f_before_second_over) + " Hz)";
        return std::abs(r.f_before_second_over) <= 2.0 && ends_with(r.text, "PARIS PARIS PARIS PARIS");
    });
    EXPECT_GE(tally.passed, 18) << tally.failures;
}

TEST(ClassicalDecoder, MatchedIgnoresAStation70HzAway) {
    // Beyond D_ch = 47 Hz the answering station has its own track and this channel, its anchor held on
    // A, ignores it. B 70 Hz away, 6 dB weaker (re A's key-down power): simulated with the first
    // design over 100 numpy seeds, f-hat within 0.2 Hz of A and A's second over intact in 99; the rate
    // carries over to option 1 (argued; Task 12 intro). (At A's level or stronger, 60-70 Hz away, B
    // leaks through the boxcar's sidelobe and corrupts the speed estimate: stated limit (a), not
    // asserted.) 20 seeds; t = 18.
    const auto tally = count_passes(37, [](unsigned seed, std::string& why) {
        const auto r = turnover(70.0, -6.0, seed);
        why = r.text + " (f " + std::to_string(r.f_before_second_over) + " Hz)";
        return std::abs(r.f_before_second_over) <= 2.0 && ends_with(r.text, "PARIS PARIS PARIS PARIS");
    });
    EXPECT_GE(tally.passed, 18) << tally.failures;
}

TEST(ClassicalDecoder, MatchedIgnoresANeighbor100HzAwayInASilence) {
    // Re-review finding I-4: at equal level the earlier design let B, aliased to -87.5 Hz, pull
    // f-hat to the -75 Hz clamp during A's silence. Simulated with the first design (2026-09-29, 100
    // numpy seeds per level; the rates carry over to option 1, argued in the Task 12 intro): f-hat
    // within 0.2 Hz of A; A's second over intact in 99 of 100 at -6 dB and 100 of 100
    // at +10 dB re A's key-down power (the +10 dB level was not re-simulated with the first-step
    // growth bound: if below its count, stop and report; Task 12 intro). At A's level B sits at the acquisition squelch's edge and cost
    // A's second over in 13 of 60 seeds (final check): a stated failure, not asserted. 20 seeds per
    // level; t = 18.
    for (const double relative_db : {-6.0, 10.0}) {
        const auto tally = count_passes(39, [&](unsigned seed, std::string& why) {
            const auto r = turnover(100.0, relative_db, seed);
            why = r.text + " (f " + std::to_string(r.f_before_second_over) + " Hz)";
            return std::abs(r.f_before_second_over) <= 2.0 && ends_with(r.text, "PARIS PARIS PARIS PARIS");
        });
        EXPECT_GE(tally.passed, 18) << relative_db << " dB:\n" << tally.failures;
    }
}

TEST(ClassicalDecoder, MatchedReturnsToTheNarrowFilterWhenNothingAnswers) {
    // After a re-acquisition finds nothing within 2 s, the filter goes back to the station's width
    // (and the speed window comes back). Simulated: K back at 58 +/- 5 samples (38.7 ms) in 196 of
    // 200 (final check) and 199 of 200 (2026-09-29). In the others noise was keyed as a stray E right
    // after the re-acquisition (s-hat restarts from its first few noise samples), which counts as
    // an answer: postponed to the backlog, about 1% of silences. 20 seeds; at 0.98, t = 17.
    const std::string msg = "CQ TEST K1ABC CQ TEST K1ABC";
    const double end = keying(msg, 25, 0.5).back().second;
    const auto tally = count_passes(40, [&](unsigned seed, std::string& why) {
        ClassicalDecoder d(kRate, matched());
        decode_all(d, keyed_signal(msg, 25, kRate, end + 4.0, 0, 1.0, sigma_for_s500(20), seed));
        why = "K " + std::to_string(d.filter_length());
        return std::abs(d.filter_length() - std::lround(0.8 * 1.2 / 25 * kRate)) <= 5;
    });
    EXPECT_GE(tally.passed, 17) << tally.failures;
}

TEST(ClassicalDecoder, MatchedChunkSizeDoesNotChangeOutput) {
    const std::string msg = "CQ TEST K1ABC";
    const auto x = keyed_signal(msg, 25, kRate, duration_for(msg, 25), 7.0, 1.0, sigma_for_s500(10), 32);
    std::vector<std::vector<DecodedSymbol>> runs;
    for (const std::size_t chunk : {std::size_t{1}, std::size_t{7}, std::size_t{256}}) {
        ClassicalDecoder d(kRate, matched());
        runs.push_back(decode_all(d, x, chunk));
    }
    for (std::size_t r = 1; r < runs.size(); ++r) {
        ASSERT_EQ(runs[r].size(), runs[0].size());
        for (std::size_t i = 0; i < runs[0].size(); ++i) {
            EXPECT_EQ(runs[r][i].text, runs[0][i].text);
            EXPECT_EQ(runs[r][i].start_s, runs[0][i].start_s);
            EXPECT_EQ(runs[r][i].end_s, runs[0][i].end_s);
        }
    }
}

TEST(ClassicalDecoder, MatchedModeRejectsInvalidSettings) {
    auto c = matched();
    c.llr_hysteresis = -1.0;
    EXPECT_THROW(ClassicalDecoder d(kRate, c), std::invalid_argument);
    c = matched();
    c.follow_after_marks = 1;
    EXPECT_THROW(ClassicalDecoder d(kRate, c), std::invalid_argument);
    c = matched();
    c.max_dit_growth = 0.9;  // must be at least 1
    EXPECT_THROW(ClassicalDecoder d(kRate, c), std::invalid_argument);
    c = matched();
    c.reacquire_after_dits = 0.0;
    EXPECT_THROW(ClassicalDecoder d(kRate, c), std::invalid_argument);
    c = matched();
    c.reacquire_min_s = -1.0;
    EXPECT_THROW(ClassicalDecoder d(kRate, c), std::invalid_argument);
    c = matched();
    c.reacquire_window_s = -1.0;
    EXPECT_THROW(ClassicalDecoder d(kRate, c), std::invalid_argument);
}
