#include "kz4ap/classical_decoder.hpp"

#include "test_signals.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <stdexcept>
#include <string>
#include <vector>

using namespace kz4ap;
using kz4ap::test::duration_for;
using kz4ap::test::keyed_signal;

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

}  // namespace

TEST(ClassicalDecoder, DecodesCleanSignal) {
    ClassicalDecoder d(kRate);
    const std::string msg = "CQ TEST K1ABC";
    EXPECT_EQ(text(decode_all(d, keyed_signal(msg, 25, kRate, duration_for(msg, 25)))), msg);
    EXPECT_NEAR(d.wpm(), 25.0, 2.5);
}

TEST(ClassicalDecoder, CleanSignalHasHighProbabilities) {
    ClassicalDecoder d(kRate);
    const std::string msg = "CQ TEST K1ABC";
    for (const auto& c : decode_all(d, keyed_signal(msg, 25, kRate, duration_for(msg, 25)))) {
        if (c.text != " ") EXPECT_GT(c.probability, 0.9f) << c.text;
    }
}

TEST(ClassicalDecoder, ReportsCharacterTimes) {
    ClassicalDecoder d(kRate);
    const auto chars = decode_all(d, keyed_signal("E", 25, kRate, 2.0));
    ASSERT_FALSE(chars.empty());
    EXPECT_EQ(chars[0].text, "E");
    EXPECT_NEAR(chars[0].start_s, 0.5, 0.02);
    EXPECT_NEAR(chars[0].end_s, 0.5 + 0.048, 0.02);
}

TEST(ClassicalDecoder, DecodesWithModerateNoise) {
    // 15 dB SNR in 500 Hz: white noise over 1500 Hz with total power 3 * 10^(-1.5).
    ClassicalDecoder d(kRate);
    const std::string msg = "CQ TEST K1ABC";
    const auto x = keyed_signal(msg, 25, kRate, duration_for(msg, 25), 0, 1.0, 0.31, 7);
    EXPECT_EQ(text(decode_all(d, x)), msg);
}

TEST(ClassicalDecoder, NoiseAloneDecodesNothing) {
    ClassicalDecoder d(kRate);
    const auto chars = decode_all(d, keyed_signal("", 25, kRate, 10.0, 0, 0.0, 0.3, 3));
    EXPECT_EQ(text(chars), "");
}

TEST(ClassicalDecoder, AdaptsToSlowSpeed) {
    ClassicalDecoder d(kRate);
    const std::string msg = "PARIS PARIS CQ K1ABC";
    const auto got = text(decode_all(d, keyed_signal(msg, 12, kRate, duration_for(msg, 12))));
    EXPECT_TRUE(ends_with(got, "CQ K1ABC")) << got;
    EXPECT_NEAR(d.wpm(), 12.0, 1.2);
}

TEST(ClassicalDecoder, AdaptsToFastSpeed) {
    ClassicalDecoder d(kRate);
    const std::string msg = "PARIS PARIS CQ K1ABC";
    const auto got = text(decode_all(d, keyed_signal(msg, 45, kRate, duration_for(msg, 45))));
    EXPECT_TRUE(ends_with(got, "CQ K1ABC")) << got;
    EXPECT_NEAR(d.wpm(), 45.0, 4.5);
}

TEST(ClassicalDecoder, PauseDecodesNothingThenResumes) {
    ClassicalDecoder d(kRate);
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
    ClassicalDecoder d(kRate);
    // ..-- (U-umlaut) is not in the English-only table.
    const auto chars = decode_all(d, keyed_elements({1, 1, 3, 3}));
    ASSERT_FALSE(chars.empty());
    EXPECT_EQ(chars[0].text, "*");
    EXPECT_EQ(chars[0].probability, 0.0f);
}

TEST(ClassicalDecoder, EightDitsDecodeAsErrorSignal) {
    ClassicalDecoder d(kRate);
    const auto chars = decode_all(d, keyed_elements({1, 1, 1, 1, 1, 1, 1, 1}));
    ASSERT_FALSE(chars.empty());
    EXPECT_EQ(chars[0].text, "<HH>");
}

TEST(ClassicalDecoder, DecodesProsigns) {
    ClassicalDecoder d(kRate);
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
    ClassicalDecoder d(kRate);
    const std::string msg = "PARIS PARIS";
    decode_all(d, keyed_signal(msg, 45, kRate, duration_for(msg, 45)));
    d.reset();
    EXPECT_DOUBLE_EQ(d.wpm(), 25.0);
}

TEST(ClassicalDecoder, RejectsInvalidConfig) {
    EXPECT_THROW(ClassicalDecoder d(0.0), std::invalid_argument);
    EXPECT_THROW(ClassicalDecoder d(-1500.0), std::invalid_argument);
    auto bad = [](auto mutate) {
        ClassicalDecoderConfig c;
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
    ClassicalDecoderConfig ok;
    ok.glitch_dits = 0;
    ok.min_wpm = ok.max_wpm = ok.initial_wpm = 25;
    EXPECT_NO_THROW(ClassicalDecoder d(kRate, ok));
}

TEST(ClassicalDecoder, TimeJumpBetweenChunksRestartsTheClock) {
    ClassicalDecoder d(kRate);
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
    ClassicalDecoder d(kRate);
    std::vector<Sample> x(static_cast<std::size_t>(0.7 * kRate));
    for (std::size_t i = static_cast<std::size_t>(0.5 * kRate); i < x.size(); ++i) x[i] = Sample(1.0f, 0.0f);
    for (std::size_t i = static_cast<std::size_t>(0.548 * kRate); i < static_cast<std::size_t>(0.560 * kRate); ++i)
        x[i] = Sample(0.0f, 0.0f);
    EXPECT_TRUE(d.process(x, 0.0).chars.empty());
    // No element has been completed, so there is no character to emit.
    EXPECT_TRUE(d.flush().chars.empty());
}
