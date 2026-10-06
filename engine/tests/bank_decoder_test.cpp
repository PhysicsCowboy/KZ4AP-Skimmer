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
#include <filesystem>
#include <fstream>
#include <map>
#include <stdexcept>
#include <numbers>
#include <string>
#include <vector>

namespace kz4ap {
// The test-only seam declared in bank_decoder.hpp and bank/channel.hpp: the bank's published list, to make a change
// the bank has never made on a recorded stream (a same-text reorder; see AResync... below).
struct BankDecoderTestAccess {
    static bank::Output& output(BankDecoder& d) { return d.channel_->out_; }
};
}  // namespace kz4ap

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

// A recorded oracle channel where the bank's list holds characters that overlap in time: the first 10 s of
// E-qrm-s3, label 9 (the full suite's synthetic recording, seed 3; channel stream as the engine's channelizer gives
// it, single precision, 1500 samples/s, the label 2.9375 Hz above the channel's center). At 8.149 s a "switch"
// correction keeps a character that comes after a replaced one in the list (the kept characters are not a prefix),
// so a correction index that counts the kept characters would leave the consumer with the replaced one.
std::vector<Sample> overlap_stream() {
    const auto path = std::filesystem::path(KZ4AP_BANK_GOLDEN_DIR) / "overlap_stream.c64";
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot open " + path.string());
    std::vector<float> raw(2 * 15000);
    in.read(reinterpret_cast<char*>(raw.data()), static_cast<std::streamsize>(raw.size() * sizeof(float)));
    if (in.gcount() != static_cast<std::streamsize>(raw.size() * sizeof(float))) throw std::runtime_error("short file");
    std::vector<Sample> y(15000);
    for (std::size_t i = 0; i < y.size(); ++i) y[i] = {raw[2 * i], raw[2 * i + 1]};
    return y;
}

TEST(BankDecoder, TheConsumersListIsTheBanksAfterEveryUpdateWhenCharactersOverlapInTime) {
    const auto y = overlap_stream();
    constexpr double kRate = 1500.0, kOffsetHz = 2.9375;
    for (const std::size_t block : {std::size_t{32}, std::size_t{1000}}) {
        SCOPED_TRACE("blocks of " + std::to_string(block));
        // The stream was found to exercise the case at the prototype's time constants in seconds (W_min 0.8 s,
        // time-out 2 s, periodicity windows 2, 5 and 10 s), stage 1's guard margin and mask-bias table and its re-key
        // clocks (Plan B's B4d switches off) and wait in keyed time (B4e's wait in marks off), set explicitly: at Plan B's B4a defaults in dits it makes no such
        // correction.
        bank::BankConfig cfg;
        cfg.guard_margin_s = bank::kStage1GuardMarginS;
        cfg.mask_bias = bank::kStage1MaskBias;
        cfg.rekey_clear_moves_stretch = false;
        cfg.rekey_timeout_from_first_mark = false;
        cfg.rekey_wait_in_marks = false;
        BankDecoder d(kRate, cfg, kOffsetHz, bank::fixed_timing(cfg, 0.8, 2.0, {2.0, 5.0, 10.0}));
        Assembled a;
        bool not_prefix = false;  // the stream must exercise a correction whose kept characters are not a prefix
        auto check = [&](const DecodeUpdate& u) {
            a.apply(u);
            const auto& chars = d.channel().output().chars();
            ASSERT_EQ(a.final_chars.size(), chars.size());
            for (std::size_t i = 0; i < chars.size(); ++i) ASSERT_EQ(a.final_chars[i].text, chars[i].text) << i;
            for (const auto& c : d.channel().output().corrections()) not_prefix |= c.first_changed_index < c.from_index;
            for (const auto& k : u.corrections) {
                EXPECT_NE(k.reason, "resync");  // the corrections' own indices suffice
                EXPECT_GE(k.reach_s, 0.0);
                EXPECT_LE(k.reach_s, bank::BankConfig{}.correction_reach_s);
            }
        };
        for (std::size_t i = 0; i < y.size(); i += block) {
            d.set_frequency_anchor_hz(kOffsetHz);
            check(d.process(std::span<const Sample>(y).subspan(i, std::min(block, y.size() - i)),
                            static_cast<double>(i) / kRate));
        }
        check(d.flush());
        EXPECT_TRUE(not_prefix);
        EXPECT_EQ(a.text(), d.channel().result().text);
        EXPECT_EQ(a.corrections, d.channel().result().corrections.size());
    }
}

// The resync path (docs/signal-processing.md appendix A.8c, "The consumer's rule"): a replacement that changes the
// list's text without a correction (the replaced and the new text are equal, but a kept character that overlaps a
// replaced one moves ahead of it) leaves the consumer's list different from the bank's, and the BankDecoder must
// send one "resync" correction that makes them equal again. No recorded stream has made the bank do this (0 of
// 31 205 corrections on the full suite), so the test makes the change itself through the test-only seam.
TEST(BankDecoder, AResyncRestoresTheConsumersListAfterASameTextReorder) {
    double rate = 0;
    const auto y = shifted("noise", 0.0, rate);  // noise alone: the bank publishes nothing of its own
    ASSERT_EQ(rate, 1500.0);
    BankDecoder d(rate, bank::BankConfig{}, 0.0);
    Assembled a;
    std::size_t resyncs = 0;
    auto check = [&](const DecodeUpdate& u) {
        a.apply(u);
        for (const auto& k : u.corrections) resyncs += k.reason == "resync";
        const auto& chars = d.channel().output().chars();
        ASSERT_EQ(a.final_chars.size(), chars.size());
        for (std::size_t i = 0; i < chars.size(); ++i) ASSERT_EQ(a.final_chars[i].text, chars[i].text) << i;
    };
    auto feed = [&](std::size_t from, std::size_t to, std::size_t block) {
        for (std::size_t i = from; i < to; i += block)
            check(d.process(std::span<const Sample>(y).subspan(i, std::min(block, to - i)),
                            static_cast<double>(i) / rate));
    };
    constexpr std::size_t kWarm = 468 * 32;  // 9.984 s, whole 32-sample bank blocks
    feed(0, kWarm, 32);
    ASSERT_TRUE(a.final_chars.empty());
    ASSERT_EQ(d.channel().processed(), static_cast<std::int64_t>(kWarm));

    // Two published characters that overlap in time: "E" from 2.0 to 5.0 s and "T" from 3.0 to 3.5 s, inside it
    // (bank times, s from the channel's first sample). One sample more completes no bank block, so the bank itself
    // changes nothing; the update carries both characters.
    bank::Output& out = BankDecoderTestAccess::output(d);
    out.append_new({bank::Char{"E", 2.0, 5.0}, bank::Char{"T", 3.0, 3.5}});
    feed(kWarm, kWarm + 1, 1);
    ASSERT_EQ(a.text(), "ET");

    // A same-text replacement from 4.0 s, made at 9.9 s: "E" ends after the cut (4.0 s) and is replaced by an "E" from
    // 4.0 to 4.5 s; "T" ends before the cut and is kept. The list becomes "T", "E": its text changed at index 0, but
    // the replaced and the new text are both "E", so no correction is recorded.
    const std::size_t corrections_before = out.corrections().size();
    out.replace_from(4.0, {bank::Char{"E", 4.0, 4.5}}, 9.9, "switch");
    ASSERT_EQ(out.corrections().size(), corrections_before);
    ASSERT_EQ(out.text(), "TE");

    const auto u = d.process(std::span<const Sample>(y).subspan(kWarm + 1, 1), static_cast<double>(kWarm + 1) / rate);
    ASSERT_EQ(u.corrections.size(), 1u);
    const auto& r = u.corrections.front();
    EXPECT_EQ(r.reason, "resync");
    EXPECT_EQ(r.from_index, 0u);
    ASSERT_EQ(r.chars.size(), 2u);
    EXPECT_EQ(r.chars[0].text, "T");
    EXPECT_EQ(r.chars[1].text, "E");
    // Its time is the bank's processed samples (9.984 s) and its reach back to the start of the first character
    // that differs, "T" at 3.0 s.
    EXPECT_DOUBLE_EQ(r.t_s, static_cast<double>(kWarm) / rate);
    EXPECT_DOUBLE_EQ(r.reach_s, static_cast<double>(kWarm) / rate - 3.0);
    check(u);
    EXPECT_EQ(a.text(), "TE");
    EXPECT_EQ(resyncs, 1u);

    // The rest of the stream and the flush keep the lists equal, with no further resync.
    feed(kWarm + 2, y.size(), 32);
    check(d.flush());
    EXPECT_EQ(resyncs, 1u);
    EXPECT_EQ(a.text(), d.channel().output().text());
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
    EXPECT_NE(texts[1].text().find("K1ABC K1ABC K"), std::string::npos)
        << "final \"" << texts[1].text() << "\", immediate \"" << texts[1].immediate << "\", " << corrections
        << " corrections";
}

}  // namespace
