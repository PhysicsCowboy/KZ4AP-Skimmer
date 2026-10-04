// The bank decoder behind the engine (BankDecoder): fed a channel stream through the engine's block interface, its
// updates, with their TextCorrections applied by index, rebuild exactly BankChannel's final characters for the same
// stream mixed down by the anchor (the prototype's streams.anchored_baseband); the immediate text (characters as
// first appended) does not depend on how the stream is split; Envelope and Matched updates carry no corrections.
#include "kz4ap/bank_decoder.hpp"
#include "kz4ap/classical_decoder.hpp"
#include "kz4ap/engine.hpp"

#include "bank/golden.hpp"
#include "test_signals.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdio>
#include <map>
#include <numbers>
#include <string>
#include <vector>

namespace {

using namespace kz4ap;

// A channel's text as a consumer assembles it from updates: chars appended, then corrections applied by index.
struct Assembled {
    std::vector<DecodedSymbol> final_chars;
    std::string immediate;
    std::size_t corrections = 0;

    void apply(const DecodeUpdate& u) {
        for (const auto& c : u.chars) {
            final_chars.push_back(c);
            immediate += c.text;
        }
        for (const auto& k : u.corrections) {
            ASSERT_LE(k.from_index, final_chars.size());
            final_chars.resize(std::min(k.from_index, final_chars.size()));
            final_chars.insert(final_chars.end(), k.chars.begin(), k.chars.end());
            ++corrections;
        }
    }
    std::string text() const {
        std::string s;
        for (const auto& c : final_chars) s += c.text;
        return s;
    }
};

const nlohmann::json& golden() {
    static const nlohmann::json g = test::load_golden("channel");
    return g;
}

// The golden stream `name` (station at 0 Hz) moved to offset_hz from the channel's center and rounded to the
// engine's single-precision samples.
std::vector<Sample> shifted(const std::string& name, double offset_hz, double& rate_hz) {
    rate_hz = golden().at("streams").at(name).at("rate_hz").get<double>();
    const auto u = test::channel_stream(golden(), name);
    std::vector<Sample> y(u.size());
    for (std::size_t n = 0; n < u.size(); ++n) {
        const double ph = 2.0 * std::numbers::pi * offset_hz * static_cast<double>(n) / rate_hz;
        const std::complex<double> v = u[n] * std::complex<double>(std::cos(ph), std::sin(ph));
        y[n] = Sample(static_cast<float>(v.real()), static_cast<float>(v.imag()));
    }
    return y;
}

// streams.anchored_baseband, restated: offset[n] the anchor in force at sample n (Hz from the channel's center),
// phase[n] = 2 pi (cumsum(offset)[n] - offset[n]) / r, u = y exp(-j phase) in numpy's order of operations.
std::vector<std::complex<double>> anchored(const std::vector<Sample>& y, const std::vector<double>& offset,
                                           double rate_hz) {
    std::vector<std::complex<double>> u(y.size());
    double sum = 0;
    for (std::size_t n = 0; n < y.size(); ++n) {
        sum += offset[n];
        const double phase = 2.0 * std::numbers::pi * (sum - offset[n]) / rate_hz;
        const double er = std::cos(-phase), ei = std::sin(-phase);
        const double yr = y[n].real(), yi = y[n].imag();
        u[n] = {yr * er - yi * ei, yr * ei + yi * er};
    }
    return u;
}

bank::ChannelResult reference(const std::vector<Sample>& y, const std::vector<double>& offset, double rate_hz) {
    bank::BankChannel ch(bank::BankConfig{}, rate_hz);
    ch.push(anchored(y, offset, rate_hz));
    ch.finish();
    return ch.result();
}

// Feeds y to a BankDecoder in blocks of `block` samples, the anchor of each block from anchor_of(first index).
template <class AnchorOf>
Assembled run_decoder(const std::vector<Sample>& y, double rate_hz, std::size_t block, double t0_s,
                      AnchorOf anchor_of) {
    BankDecoder d(rate_hz, bank::BankConfig{}, anchor_of(0));
    Assembled a;
    for (std::size_t i = 0; i < y.size(); i += block) {
        d.set_frequency_anchor_hz(anchor_of(i));
        a.apply(d.process(std::span<const Sample>(y).subspan(i, std::min(block, y.size() - i)),
                          t0_s + static_cast<double>(i) / rate_hz));
    }
    a.apply(d.flush());
    a.apply(d.flush());  // a second flush publishes nothing
    return a;
}

// The same characters (text, one by one). Times are not compared: a replacement whose text is unchanged re-times
// characters without a correction (the prototype records none), and the consumer keeps the times first published.
// They are offset by the channel's time origin and finite.
void expect_same_chars(const Assembled& a, const bank::ChannelResult& r, double t0_s) {
    ASSERT_EQ(a.final_chars.size(), r.chars.size()) << a.text() << " | " << r.text;
    for (std::size_t i = 0; i < r.chars.size(); ++i) {
        EXPECT_EQ(a.final_chars[i].text, r.chars[i].text) << i;
        EXPECT_TRUE(std::isfinite(a.final_chars[i].start_s) && std::isfinite(a.final_chars[i].end_s));
        EXPECT_GE(a.final_chars[i].start_s, t0_s);
        EXPECT_TRUE(std::isfinite(a.final_chars[i].probability));
    }
}

TEST(BankDecoder, CorrectionsAppliedByIndexGiveTheChannelsFinalTextHoweverTheStreamIsSplit) {
    for (const std::string name : {"turnover", "speed_turnover"}) {
        double rate = 0;
        constexpr double kOffsetHz = 23.4;
        const auto y = shifted(name, kOffsetHz, rate);
        const auto r = reference(y, std::vector<double>(y.size(), kOffsetHz), rate);
        ASSERT_FALSE(r.corrections.empty()) << name << ": the stream must exercise corrections";
        std::string immediate;
        for (const std::size_t block : {std::size_t{32}, std::size_t{47}, std::size_t{1}, std::size_t{1000}}) {
            if (name != "turnover" && block != 32) continue;  // the bank's cost: every split on one stream only
            constexpr double kT0 = 3.0;
            const auto a = run_decoder(y, rate, block, kT0, [&](std::size_t) { return kOffsetHz; });
            SCOPED_TRACE(name + ", blocks of " + std::to_string(block));
            EXPECT_EQ(a.text(), r.text);
            expect_same_chars(a, r, kT0);
            EXPECT_EQ(a.corrections, r.corrections.size());
            // The immediate text (characters as first appended) does not depend on the split either.
            if (immediate.empty()) immediate = a.immediate;
            EXPECT_EQ(a.immediate, immediate);
        }
        EXPECT_NE(immediate, r.text) << name << ": corrections should change the text";
    }
}

TEST(BankDecoder, MixesEachBlockByTheLatestAnchorWithAContinuousPhase) {
    double rate = 0;
    const auto y = shifted("turnover", 31.0, rate);
    // The anchor moves by a few Hz every second, as a detector's frequency does; per 32-sample block.
    auto anchor_of = [&](std::size_t i) { return 31.0 + 2.5 * std::sin(static_cast<double>(i / 32) * 0.02); };
    std::vector<double> offset(y.size());
    for (std::size_t n = 0; n < y.size(); ++n) offset[n] = anchor_of(n - n % 32);
    const auto r = reference(y, offset, rate);
    const auto a = run_decoder(y, rate, 32, 0.0, anchor_of);
    EXPECT_EQ(a.text(), r.text);
    expect_same_chars(a, r, 0.0);
}

TEST(BankDecoder, CorrectionsAreCarriedWithTheirReasonAndTime) {
    double rate = 0;
    const auto y = shifted("turnover", 0.0, rate);
    const auto r = reference(y, std::vector<double>(y.size(), 0.0), rate);
    BankDecoder d(rate, bank::BankConfig{}, 0.0);
    std::vector<TextCorrection> got;
    for (std::size_t i = 0; i < y.size(); i += 32) {
        auto u = d.process(std::span<const Sample>(y).subspan(i, std::min<std::size_t>(32, y.size() - i)),
                           10.0 + static_cast<double>(i) / rate);
        got.insert(got.end(), u.corrections.begin(), u.corrections.end());
    }
    auto u = d.flush();
    got.insert(got.end(), u.corrections.begin(), u.corrections.end());
    ASSERT_EQ(got.size(), r.corrections.size());
    for (std::size_t i = 0; i < got.size(); ++i) {
        EXPECT_EQ(got[i].from_index, r.corrections[i].from_index);
        EXPECT_EQ(got[i].reason, r.corrections[i].reason);
        EXPECT_DOUBLE_EQ(got[i].t_s, r.corrections[i].t_s + 10.0);
    }
}

TEST(BankDecoder, ExactZerosPublishNothing) {
    BankDecoder d(1500.0, bank::BankConfig{}, 0.0);
    const std::vector<Sample> zeros(3000);
    Assembled a;
    for (std::size_t i = 0; i < zeros.size(); i += 32)
        a.apply(d.process(std::span<const Sample>(zeros).subspan(i, std::min<std::size_t>(32, zeros.size() - i)), 0.0));
    a.apply(d.flush());
    EXPECT_TRUE(a.final_chars.empty());
    EXPECT_EQ(a.corrections, 0u);
}

TEST(BankDecoder, EnvelopeAndMatchedUpdatesCarryNoCorrections) {
    constexpr double kRate = 1500.0;
    const auto x = test::keyed_signal("CQ TEST K1ABC", 25, kRate, test::duration_for("CQ TEST K1ABC", 25), 0.0, 1.0,
                                      0.05, 3);
    for (const FrontEnd fe : {FrontEnd::Envelope, FrontEnd::Matched}) {
        ClassicalDecoderConfig cfg;
        cfg.front_end = fe;
        ClassicalDecoder d(kRate, cfg);
        std::string text;
        for (std::size_t i = 0; i < x.size(); i += 32) {
            const auto u = d.process(std::span<const Sample>(x).subspan(i, std::min<std::size_t>(32, x.size() - i)),
                                     static_cast<double>(i) / kRate);
            EXPECT_TRUE(u.corrections.empty());
            for (const auto& c : u.chars) text += c.text;
        }
        const auto u = d.flush();
        EXPECT_TRUE(u.corrections.empty());
        for (const auto& c : u.chars) text += c.text;
        EXPECT_NE(text.find("K1ABC"), std::string::npos) << text;
    }
    ClassicalDecoderConfig bank_cfg;
    bank_cfg.front_end = FrontEnd::Bank;
    EXPECT_THROW(ClassicalDecoder(kRate, bank_cfg), std::invalid_argument);
}

TEST(BankDecoder, TheEngineRunsTheBankOnOracleChannels) {
    constexpr double kRate = 48000;
    const std::string text = "CQ CQ TEST DE K1ABC K1ABC K";
    const auto x = test::keyed_signal(text, 25, kRate, test::duration_for(text, 25), 1203.0, 0.0102, 0.02, 5);
    EventBus bus;
    std::map<std::uint32_t, Assembled> texts;
    std::size_t corrections = 0;
    bus.subscribe([&](const Event& e) {
        if (const auto* d = std::get_if<DecodedTextEvent>(&e)) {
            DecodeUpdate u;
            u.chars = d->chars;
            u.corrections = d->corrections;
            corrections += d->corrections.size();
            texts[d->track_id].apply(u);
        }
    });
    EngineConfig config;
    config.sample_rate = static_cast<int>(kRate);
    config.decoder.front_end = FrontEnd::Bank;
    config.oracle_frequencies_hz = {1203.0};
    Engine engine(config, bus);
    engine.process(x);
    engine.finish();
    ASSERT_EQ(texts.size(), 1u);
    EXPECT_NE(texts[1].text().find("K1ABC K1ABC K"), std::string::npos) << texts[1].text();
    std::printf("bank via the engine: final \"%s\", immediate \"%s\", %zu corrections\n", texts[1].text().c_str(),
                texts[1].immediate.c_str(), corrections);
}

}  // namespace
