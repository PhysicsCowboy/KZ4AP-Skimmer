#include "kz4ap/callsign.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <sstream>
#include <string>
#include <tuple>
#include <vector>

using namespace kz4ap;
using namespace kz4ap::callsign;

namespace {

bool is_call(const std::string& word) { return parse_call(word).has_value(); }

// A list of symbols for a text at 0.1 s a character, from t_s on; a space is a word space; <XX> is one symbol. probability: every
// symbol's (1 is what a decoder that keeps no margins says).
std::vector<DecodedSymbol> symbols(const std::string& text, double t_s = 0.0, float probability = 1.0f) {
    std::vector<DecodedSymbol> out;
    for (std::size_t i = 0; i < text.size(); ++i) {
        std::string s(1, text[i]);
        if (text[i] == '<') {
            const std::size_t close = text.find('>', i);
            s = text.substr(i, close - i + 1);
            i = close;
        }
        out.push_back({s, probability, t_s, t_s + 0.1});
        t_s += 0.1;
    }
    return out;
}

std::vector<CallReading> readings(const std::string& text) {
    const std::vector<DecodedSymbol> s = symbols(text);
    const std::vector<Word> w = words_of(s);
    return read_calls(w, 0, w.size());
}

bool has(const std::vector<CallReading>& r, const std::string& base, Role role) {
    return std::any_of(r.begin(), r.end(), [&](const CallReading& x) { return x.base == base && x.role == role; });
}
bool has_base(const std::vector<CallReading>& r, const std::string& base) {
    return std::any_of(r.begin(), r.end(), [&](const CallReading& x) { return x.base == base; });
}

// The stage on tracks whose text is given whole, with a clock that runs past the settling.
class Stage {
public:
    explicit Stage(CallSearchConfig config = {}, std::shared_ptr<const KnownCalls> known = nullptr) : search(config, std::move(known)) {}
    void born(std::uint32_t id, double freq_hz) {
        TrackEvent e{};
        e.kind = TrackEvent::Kind::Born;
        e.track.id = id;
        e.track.freq_hz = freq_hz;
        search.apply(e);
    }
    void text(std::uint32_t id, double freq_hz, const std::string& t, double t_s, float probability = 1.0f) {
        DecodedTextEvent e{};
        e.track_id = id;
        e.freq_hz = freq_hz;
        e.chars = symbols(t, t_s, probability);
        search.apply(e);
    }
    void died(std::uint32_t id) {
        TrackEvent e{};
        e.kind = TrackEvent::Kind::Died;
        e.track.id = id;
        search.apply(e);
    }
    const CallRow* row(const std::string& base) const {
        for (const CallRow& r : search.calls())
            if (r.base == base) return &r;
        return nullptr;
    }
    CallSearch search;
};

}  // namespace

TEST(CallGrammar, RegularCalls) {
    for (const char* call : {"K1ABC", "W1AW", "N5JJ", "WA0QZK", "KX9F", "IK0HBN", "S58W", "E51MWA", "ZL1TM", "9A1A", "3Y0J", "3DA0RU", "2E0ABC", "A61AA", "VK9XX",
                             "T88AA", "W1A", "K1N", "G3Z", "UA2MA", "JA3S", "DJ5NN", "1A0KM", "S01WS", "HB9CV", "PY2A", "V31XX", "4U1UN"})
        EXPECT_TRUE(is_call(call)) << call;
}

TEST(CallGrammar, NotCalls) {
    for (const char* word : {"CQ", "TEST", "5NN", "599", "73", "K", "DE", "QTH", "RST", "K1", "K12", "1ABC", "Q1ABC", "QRP5A", "0A1B", "A1ABC", "E81AB", "H51AB", "U1ABC", "X1ABC", "O1ABC",
                             "K1ABCDE", "IC7300", "FT8", "TS590", "K1ABC1", "YZ1AA", "DE1ABC", ""})
        EXPECT_FALSE(is_call(word)) << word;
}

TEST(CallGrammar, WordsOfCwWithACallsFormAreMarked) {
    // Rigs, antennas and software (exact), and one or two letters glued to 5NN: calls' forms, some of them issued (K9AY, N1MM,
    // K5NN, DJ5NN, W5NN), so marked to be spotted from a list only, not refused.
    for (const char* word : {"G5RV", "K3S", "N1MM", "K9AY", "K1EL", "W2DU", "W8JK", "W3DZZ", "R5NN", "UR5NN", "TU5NN", "K5NN", "N5NN",
                             "DJ5NN", "FN5NN", "NN5NN", "W5NN", "E5NN"}) {
        const auto p = parse_call(word);
        ASSERT_TRUE(p.has_value()) << word;
        EXPECT_TRUE(p->cw_word) << word;
        EXPECT_TRUE(is_cw_word(word)) << word;
    }
    // Exact matches and the 5NN form only: manta's lesson. A longer suffix, another digit or three letters before it are calls.
    for (const char* call : {"K9AYA", "N1MMX", "DJ5NNA", "DJ5NA", "K5NA", "W5NNX", "KC5NNA", "K1ABC"}) {
        const auto p = parse_call(call);
        ASSERT_TRUE(p.has_value()) << call;
        EXPECT_FALSE(p->cw_word) << call;
    }
    EXPECT_FALSE(is_cw_word("5NN"));
    EXPECT_FALSE(is_cw_word("ABC5NN"));
    EXPECT_FALSE(is_cw_word("K15NN"));
}

TEST(CallGrammar, APrefixsDigitCanBeTheCallsDigit) {
    // A letter and a digit outside the whole-letter series, then letters only: the prefix's digit is the call's (MASTER.SCP holds
    // C6AGU, E7DX, J6M, P3X, D4C, T8TT, V3A, Z3A; the RBN spots C8K, E6DX, T2DE, A6BB). Four letters are unusual, as after any digit.
    for (const char* call : {"C6AGU", "E7DX", "J6M", "P3X", "D4C", "T8TT", "V3A", "Z3A", "A2NEW", "E2QRP", "S0S", "C8K", "E6DX", "T2DE",
                             "A6BB", "E7DXAB"}) {
        const auto p = parse_call(call);
        ASSERT_TRUE(p.has_value()) << call;
        EXPECT_TRUE(p->prefix_digit) << call;
        EXPECT_FALSE(p->special) << call;
        EXPECT_EQ(p->unusual, std::string_view(call).size() == 6) << call;
        EXPECT_EQ(p->base, call);
    }
    // A digit after the prefix, a whole-letter series or a digit-letter prefix: as before, no mark.
    for (const char* call : {"E73A", "V31XX", "T88AA", "C6AGU1A", "3B8CF", "9H1A", "K4A", "G3Z", "S58W", "A61AA"}) {
        const auto p = parse_call(call);
        if (std::string_view(call) == "C6AGU1A") {
            EXPECT_FALSE(p.has_value());
            continue;
        }
        ASSERT_TRUE(p.has_value()) << call;
        EXPECT_FALSE(p->prefix_digit) << call;
    }
    // Unallocated pairs, a digit-letter prefix with no digit after it, five letters: no call.
    for (const char* word : {"O8FN", "U1BD", "H5AB", "E8AB", "9HABC", "3BABC", "E7ABCDE", "C6"})
        EXPECT_FALSE(is_call(word)) << word;
}

TEST(CallGrammar, EitherSideOfASlashCanBeTheBaseCall) {
    // A prefix with a call's form (VP2V, VK9X) before the call: the call is the base (callsign.cpp's parse_form once took the
    // left side whenever it parsed, and then refused VP2V/K1ABC).
    EXPECT_EQ(parse_call("VP2V/K1ABC")->base, "K1ABC");
    EXPECT_EQ(parse_call("VP2V/K1ABC")->full, "VP2V/K1ABC");
    EXPECT_EQ(parse_call("VP2E/DL1ABC")->base, "DL1ABC");
    EXPECT_EQ(parse_call("K1ABC/VP2V")->base, "K1ABC");
    // Both sides can be the base: the one that is not a location form (two characters, a digit, one letter, not a US series),
    // when exactly one is; else the left, as before (a 1x1 signed with its operator's call, N4A/K4SV, is N4A).
    for (const auto& [word, base, other] : std::vector<std::tuple<std::string, std::string, std::string>>{
             {"W1AW/VP2V", "W1AW", "VP2V"}, {"VP2V/W1AW", "W1AW", "VP2V"}, {"VK9X/W1AW", "W1AW", "VK9X"}, {"W1AW/VK9X", "W1AW", "VK9X"},
             {"K1AB/VP2E", "K1AB", "VP2E"}, {"W1AW/3D2R", "W1AW", "3D2R"}, {"CE0Y/K1A", "K1A", "CE0Y"}, {"K1A/W1AW", "K1A", "W1AW"},
             {"N4A/K4SV", "N4A", "K4SV"}, {"KH6A/W1AW", "KH6A", "W1AW"}, {"VP2V/VK9X", "VP2V", "VK9X"},
             {"DL1A/W1AW", "W1AW", "DL1A"}}) {  // the last a limitation: a call of the location form is taken for one
        const auto p = parse_call(word);
        ASSERT_TRUE(p.has_value()) << word;
        EXPECT_EQ(p->base, base) << word;
        EXPECT_EQ(p->other_base, other) << word;
    }
    EXPECT_TRUE(parse_call("VP2V/K1ABC")->other_base.empty());
    // As before.
    for (const auto& [word, base] : std::vector<std::pair<std::string, std::string>>{
             {"DL/K1ABC", "K1ABC"}, {"K1ABC/P", "K1ABC"}, {"K1ABC/4", "K1ABC"}, {"W1/DL1ABC", "DL1ABC"}, {"VE3/K1ABC", "K1ABC"},
             {"KH6/K1ABC", "K1ABC"}, {"K1ABC/KH6", "K1ABC"}, {"DL1ABC/P", "DL1ABC"}, {"K1ABC/MM", "K1ABC"}, {"K1ABC/QRP", "K1ABC"},
             {"4U1ITU/HB9", "4U1ITU"}}) {
        const auto p = parse_call(word);
        ASSERT_TRUE(p.has_value()) << word;
        EXPECT_EQ(p->base, base) << word;
    }
    EXPECT_FALSE(is_call("K1ABC/VE3XYZ"));  // two calls: neither side is a prefix
    EXPECT_FALSE(is_call("VP2V/K1ABC/P"));
    EXPECT_FALSE(is_call("VP2V/K9STPL"));
}

TEST(CallSearch, ACallOnBothSidesOfASlashIsTheListedOne) {
    // N4A/K4SV: N4A by the grammar; with K4SV listed and N4A not, K4SV; with both or neither listed, N4A.
    const auto run = [](std::vector<std::string> listed) {
        std::shared_ptr<KnownCalls> known;
        if (!listed.empty()) {
            known = std::make_shared<KnownCalls>();
            for (const std::string& c : listed) known->add(c);
        }
        Stage s(CallSearchConfig{}, known);
        s.born(1, 1000.0);
        s.text(1, 1000.0, "CQ DE N4A/K4SV N4A/K4SV K ", 0.0);
        s.search.advance(30.0);
        std::vector<std::string> out;
        for (const CallRow& r : s.search.calls()) out.push_back(r.base);
        return out;
    };
    EXPECT_EQ(run({}), std::vector<std::string>{"N4A"});
    EXPECT_EQ(run({"K4SV"}), std::vector<std::string>{"K4SV"});
    EXPECT_EQ(run({"N4A"}), std::vector<std::string>{"N4A"});
    EXPECT_EQ(run({"N4A", "K4SV"}), std::vector<std::string>{"N4A"});
    EXPECT_EQ(run({"W1AW"}), std::vector<std::string>{"N4A"});
}

TEST(CallGrammar, SpecialFormsAreMarked) {
    const auto p = parse_call("TM100TF");
    ASSERT_TRUE(p.has_value());
    EXPECT_TRUE(p->special);
    EXPECT_FALSE(parse_call("K1ABC")->special);
}

TEST(CallGrammar, Portable) {
    EXPECT_EQ(parse_call("K1ABC/P")->base, "K1ABC");
    EXPECT_EQ(parse_call("K1ABC/MM")->base, "K1ABC");
    EXPECT_EQ(parse_call("K1ABC/QRP")->base, "K1ABC");
    EXPECT_EQ(parse_call("K1ABC/4")->base, "K1ABC");
    EXPECT_EQ(parse_call("DL/K1ABC")->base, "K1ABC");
    EXPECT_EQ(parse_call("K1ABC/VE3")->base, "K1ABC");
    EXPECT_EQ(parse_call("K1ABC/P")->full, "K1ABC/P");
    EXPECT_FALSE(is_call("K1ABC/"));
    EXPECT_FALSE(is_call("/P"));
    EXPECT_FALSE(is_call("K1ABC/P/M"));
}

TEST(CallGrammar, ThreeCharactersBeforeTheDigitOnlyInAHalfSeries) {
    // RR 19.68.1: three characters before the digit only where Appendix 42 splits the first two between nations (3DA-3DM
    // Eswatini, 3DN-3DZ Fiji), and then three characters at most after it; and Guinea's 3XY, which its DXpeditions sign. Signal
    // reports run together (5NN5NN) and a short word repeated (5RE5RE) had a call's form as 5NN or 5RE, 5, then letters.
    for (const char* word : {"5NN5NN", "5NN5NNK", "5NN5NNCT", "5NN5NNNJ", "5RE5RE", "8RN8RN", "4XZ4XZ", "3XA1D", "3DA0ABCD", "3XY1ABCD"})
        EXPECT_FALSE(is_call(word)) << word;
    for (const char* call : {"3DA0RU", "3DA0GY", "3D2AB", "3D2ABC", "3XY1D", "3XY7C", "3X2ZS", "5N7M", "5N0OCH", "5N2AB", "5N5N", "9A1A",
                             "4U1ITU", "3B8CF", "6Y5WJ"})
        EXPECT_TRUE(is_call(call)) << call;
}

TEST(CallGrammar, TheUnitedStatesSeriesHaveTheFccsForms) {
    // K, N, W and AA to AL: one to three letters after the digit; AA to AL and NA to NZ two at most (the FEMA stations AF#EMA and
    // NF#EMA excepted); a 1x1 is a special-event call, never ending in X.
    for (const char* word : {"K9STPL", "AB9CAEE", "AB9CAE", "W0RAUW", "KE4COHK", "NA1ABC", "AL7ABC", "K1X", "W7X", "WA1ABCD"})
        EXPECT_FALSE(is_call(word)) << word;
    for (const char* call : {"K1ABC", "W1AW", "N2ASD", "KP4AA", "KP4ABC", "NP4A", "WP4ABC", "KH6ABC", "AH6AB", "NH6A", "KL7ABC",
                             "AL7AB", "WL7A", "KH0AA", "KG4AB", "AA1A", "KA1A", "NA1A", "WA1A", "AB9CA", "NN5NN", "AF1EMA", "NF0EMA",
                             "K1A", "W7Q", "N5V", "K4U", "W0Z", "WA0QZK", "KC4AAA"})
        EXPECT_TRUE(is_call(call)) << call;
    // Elsewhere four letters stay a call's form (RR 19.68), marked unusual; a US call's special form (W100AW) is as before.
    for (const char* call : {"VK3FABC", "8N3ACAF", "EE4EEEE", "TE7TTTD", "GA5NNON", "OM3MSTR"}) {
        const auto p = parse_call(call);
        ASSERT_TRUE(p.has_value()) << call;
        EXPECT_TRUE(p->unusual) << call;
        EXPECT_FALSE(p->special) << call;
    }
    EXPECT_FALSE(parse_call("K1ABC")->unusual);
    EXPECT_FALSE(parse_call("SH5SHI")->unusual);  // a call's form: SH is Sweden's (SH9YOTA), and only the tiny rule reads it (A.12)
    const auto w100aw = parse_call("W100AW");
    ASSERT_TRUE(w100aw.has_value());
    EXPECT_TRUE(w100aw->special);
    EXPECT_FALSE(w100aw->unusual);
    // Portable forms keep the base call's rules.
    EXPECT_EQ(parse_call("DL/K1ABC")->base, "K1ABC");
    EXPECT_EQ(parse_call("K1ABC/P")->base, "K1ABC");
    EXPECT_EQ(parse_call("W1/DL1ABC")->base, "DL1ABC");
    EXPECT_EQ(parse_call("KH6/K1ABC")->base, "K1ABC");
    EXPECT_FALSE(is_call("DL/K9STPL"));
    EXPECT_FALSE(is_call("K9STPL/P"));
}

TEST(CallGrammar, EditDistance) {
    EXPECT_EQ(edit_distance("IK0HBN", "IK0HBN", 2), 0);
    EXPECT_EQ(edit_distance("IK0HBN", "IK0HBT", 2), 1);
    EXPECT_EQ(edit_distance("IK0HBN", "K0HBN", 2), 1);
    EXPECT_EQ(edit_distance("KB4EGG", "KB4PG", 2), 2);
    EXPECT_EQ(edit_distance("K1ABC", "W9XYZ", 2), 3);  // limit + 1: it stopped counting
}

TEST(Words, SpacesProsignsAndPunctuation) {
    const auto s = symbols("CQ DE K1ABC<BT>R,TU");
    const auto w = words_of(s);
    ASSERT_EQ(w.size(), 6u);
    EXPECT_EQ(w[2].text, "K1ABC");
    EXPECT_EQ(w[3].text, "<BT>");
    EXPECT_EQ(w[4].text, "R");
    EXPECT_EQ(w[5].text, "TU");
    EXPECT_DOUBLE_EQ(w[0].start_s, 0.0);
    EXPECT_NEAR(w[0].end_s, 0.2, 1e-9);
}

TEST(Readings, RolesFromTheWordsAround) {
    EXPECT_TRUE(has(readings("CQ CQ DE K1ABC K1ABC K"), "K1ABC", Role::Sender));
    EXPECT_TRUE(has(readings("CQ TEST K1ABC"), "K1ABC", Role::Sender));
    EXPECT_TRUE(has(readings("TU W5L TEST"), "W5L", Role::Sender));
    const auto qso = readings("W2XYZ DE K1ABC GM");
    EXPECT_TRUE(has(qso, "W2XYZ", Role::Named));
    EXPECT_TRUE(has(qso, "K1ABC", Role::Sender));
    EXPECT_TRUE(has(readings("K1ABC 5NN 14"), "K1ABC", Role::Named));
    EXPECT_TRUE(has(readings("E K1ABC T"), "K1ABC", Role::Unknown));
}

TEST(Readings, RolesSkipStrayLetters) { EXPECT_TRUE(has(readings("DE E K1ABC"), "K1ABC", Role::Sender)); }

TEST(Readings, GluedWords) {
    EXPECT_TRUE(has(readings("DEK1ABC"), "K1ABC", Role::Sender));
    const auto qso = readings("N5JJDEWA0QZK");
    EXPECT_TRUE(has(qso, "N5JJ", Role::Named));
    EXPECT_TRUE(has(qso, "WA0QZK", Role::Sender));
    EXPECT_TRUE(has_base(readings("WA0QZKWA0QZK"), "WA0QZK"));
    EXPECT_TRUE(has(readings("N5JJDEWA0QZKWA0QZKK ER I E"), "WA0QZK", Role::Sender));
    EXPECT_TRUE(has_base(readings("K1ABC5NN"), "K1ABC"));
    const auto cq = readings("N3XUCQ TEST");
    EXPECT_TRUE(has_base(cq, "N3XU"));
    EXPECT_FALSE(has_base(cq, "N3XUCQ"));
}

TEST(Readings, ACallEndingInDeStaysWholeUnlessACallFollows) {
    EXPECT_TRUE(has_base(readings("CQ K7IDE K7IDE"), "K7IDE"));
    EXPECT_FALSE(has_base(readings("CQ K7IDE K7IDE"), "K7I"));
    EXPECT_TRUE(has_base(readings("K7IDE W2XY"), "K7I"));
}

TEST(Readings, StrayLetters) {
    EXPECT_TRUE(has_base(readings("EIK0HBN"), "IK0HBN"));
    const auto both = readings("EK0HBN");  // both sides of the question
    EXPECT_TRUE(has_base(both, "EK0HBN"));
    EXPECT_TRUE(has_base(both, "K0HBN"));
    EXPECT_TRUE(has_base(readings("N5JJK"), "N5JJ"));
}

TEST(Readings, ShortWordsJoined) {
    const auto r = readings("DE K X 9 F K");
    EXPECT_TRUE(has(r, "KX9F", Role::Sender));
    EXPECT_TRUE(has_base(readings("N 5 JJ"), "N5JJ"));
    EXPECT_TRUE(has_base(readings("WA 0 QZK"), "WA0QZK"));
    EXPECT_TRUE(has_base(readings("S 58W"), "S58W"));
    for (const CallReading& x : readings("WA 0 QZK"))
        if (x.base == "WA0QZK") EXPECT_EQ(x.reading, Reading::Joined);
}

TEST(Readings, TinyPiecesAreReadOnlyAsAWholeWord) {
    EXPECT_TRUE(only_tiny_pieces("I5E"));
    EXPECT_FALSE(only_tiny_pieces("K1ABC"));
    EXPECT_TRUE(has_base(readings("CQ N5AU K"), "N5AU"));
    EXPECT_FALSE(has_base(readings("I 5 E H E"), "I5EHE"));  // joined
    EXPECT_FALSE(has_base(readings("EE5E"), "E5E"));         // stripped
    EXPECT_FALSE(has_base(readings("DEN5AU"), "N5AU"));      // glued
}

TEST(KnownCallList, ReadsScpFormat) {
    std::istringstream in("# a comment\n; another\n\nk1abc\nW2XY  anything\nK1ABC/P\nnot-a-call\n");
    const KnownCalls known = KnownCalls::read(in);
    EXPECT_TRUE(known.contains("K1ABC"));
    EXPECT_TRUE(known.contains("W2XY"));
    EXPECT_FALSE(known.contains("N5JJ"));
    EXPECT_EQ(known.size(), 3u);
    EXPECT_THROW(KnownCalls::read_file("no such file.scp"), std::runtime_error);
    // MASTER.SCP's header, as on 2026-11-18: the directive and the comments are not calls.
    std::istringstream scp("!!Order,1,1\n#\n# Super Check Partial\n# Release 2026.11.18\nK1ABC\nW2XY\n");
    EXPECT_EQ(KnownCalls::read(scp).size(), 2u);
}

TEST(KnownCallList, RefusesABinaryFileAndOneWithNoCall) {
    // Review 10: MASTER.DTA given as the list matched nothing, and with named_listed_only refused every call named once, silently.
    std::istringstream binary(std::string("h\x15\0\0K1ABC\0W2XY\0", 16));
    EXPECT_THROW(KnownCalls::read(binary), std::runtime_error);
    std::istringstream no_call("# only comments\nhello world\n12345\n");
    EXPECT_THROW(KnownCalls::read(no_call), std::runtime_error);
    std::istringstream empty("");
    EXPECT_THROW(KnownCalls::read(empty), std::runtime_error);
    std::istringstream one("not-a-call\nK1ABC\n");
    EXPECT_EQ(KnownCalls::read(one).size(), 2u);  // a list with one call reads, the junk line counted as it was before
}

TEST(CallSearch, OnceIsNotEnoughTwiceFramedIs) {
    Stage s;
    s.born(1, 1000.0);
    s.text(1, 1000.0, "CQ DE K1ABC ", 0.0);
    s.search.advance(30.0);
    EXPECT_TRUE(s.search.calls().empty());
    s.text(1, 1000.0, "CQ DE K1ABC ", 40.0);
    s.search.advance(70.0);
    const CallRow* r = s.row("K1ABC");
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(r->count, 2u);
    EXPECT_TRUE(r->framed);
    EXPECT_TRUE(r->heard);
    EXPECT_DOUBLE_EQ(r->freq_hz, 1000.0);
    EXPECT_EQ(s.search.calls().size(), 1u);  // reported once
    s.text(1, 1000.0, "DE K1ABC ", 80.0);
    s.search.advance(110.0);
    EXPECT_EQ(s.search.calls().size(), 1u);
    EXPECT_EQ(s.row("K1ABC")->count, 3u);
}

TEST(CallSearch, BareNeedsTwoAndAFarPlaceAddsOnlyWhenPooled) {
    // pool_places: one call is one station; read once at 1 kHz and once at 9 kHz, it has two independent readings. Off (call-1 to rob-1),
    // points add only within same_place_hz.
    const auto run = [](double pool) {
        CallSearchConfig config;
        config.pool_places = pool;
        auto s = std::make_unique<Stage>(config);
        s->born(1, 1000.0);
        s->born(2, 9000.0);
        s->text(1, 1000.0, "E K1ABC T ", 0.0);
        s->search.advance(40.0);
        EXPECT_EQ(s->row("K1ABC"), nullptr);
        s->text(2, 9000.0, "E K1ABC T ", 80.0);
        s->search.advance(120.0);
        return s;
    };
    EXPECT_NE(run(1.0)->row("K1ABC"), nullptr);
    const auto local = run(0.0);
    EXPECT_EQ(local->row("K1ABC"), nullptr);
    local->text(1, 1000.0, "E K1ABC T ", 130.0);
    local->search.advance(170.0);
    EXPECT_NE(local->row("K1ABC"), nullptr);
}

TEST(CallSearch, PooledReadingsOfOneTransmissionCountOnce) {
    // Two tracks 2 kHz apart reading the same moment (a strong station's copy) are one transmission, wherever they lie.
    Stage s;
    s.born(1, 1000.0);
    s.born(2, 3000.0);
    s.text(1, 1000.0, "E K1ABC T ", 0.0);
    s.text(2, 3000.0, "E K1ABC T ", 0.05);
    s.search.advance(40.0);
    EXPECT_EQ(s.row("K1ABC"), nullptr);
}

TEST(CallSearch, OneTransmissionIsOneGroupOfOverlapsWhateverTheTrackOrder) {
    // Review 15: readings at three far places at [0.2, 0.7], [0.6, 1.1] and [1.0, 1.5] s are one chain of overlaps, so one transmission;
    // compared only with each group's first member, the first and last fell into different groups for one order of the tracks.
    const double times[3] = {0.2, 0.6, 1.0};
    const int orders[2][3] = {{0, 1, 2}, {1, 0, 2}};
    for (const auto& order : orders) {
        Stage s;
        for (int k = 0; k < 3; ++k) s.born(static_cast<std::uint32_t>(k + 1), 1000.0 + 4000.0 * k);
        for (int k = 0; k < 3; ++k) s.text(static_cast<std::uint32_t>(k + 1), 1000.0 + 4000.0 * k, "K1ABC ", times[order[k]]);
        s.search.advance(40.0);
        EXPECT_EQ(s.row("K1ABC"), nullptr) << "order " << order[0] << order[1] << order[2];
        s.text(1, 1000.0, "K1ABC ", 50.0);  // a second transmission
        s.search.advance(80.0);
        EXPECT_NE(s.row("K1ABC"), nullptr) << "order " << order[0] << order[1] << order[2];
    }
}

TEST(CallSearch, APooledRowDoesNotComeAndGo) {
    // Review 15: W9XYZ read three times at 1350 Hz; W9XYY once at 1250 Hz, where W9XYZ holds it, and once at 1000 Hz. Its local points at
    // 1000 Hz (2, the held reading within 300 Hz among them) reported it, its pooled points (1) withdrew it at W9XYZ's next reading, and
    // its next reading at 1000 Hz listed it again. Reported on the pooled points, it waits for a second free reading and then stays.
    Stage s;
    s.born(1, 1350.0);
    s.born(2, 1250.0);
    s.born(3, 1000.0);
    s.text(1, 1350.0, "E W9XYZ T E W9XYZ T E W9XYZ T ", 0.0);
    s.text(2, 1250.0, "E W9XYY T ", 40.0);
    s.text(3, 1000.0, "E W9XYY T ", 55.0);
    s.search.advance(70.0);
    EXPECT_NE(s.row("W9XYZ"), nullptr);
    EXPECT_EQ(s.row("W9XYY"), nullptr);
    s.text(1, 1350.0, "E W9XYZ T ", 90.0);
    s.search.advance(110.0);
    EXPECT_EQ(s.row("W9XYY"), nullptr);
    s.text(3, 1000.0, "E W9XYY T ", 130.0);
    s.search.advance(150.0);
    ASSERT_NE(s.row("W9XYY"), nullptr);
    s.text(1, 1350.0, "E W9XYZ T ", 160.0);
    s.search.advance(180.0);
    EXPECT_NE(s.row("W9XYY"), nullptr);
    EXPECT_EQ(s.search.withdrawn(), 0u);
}

TEST(CallSearch, APooledReadingWhereABetterVariantHoldsItDoesNotCount) {
    // W9XYY read once at 1 kHz beside W9XYZ read three times there: that reading is W9XYZ's misreading, and with one bare reading of its
    // own at 5 kHz it does not pass. A second reading at 5 kHz passes it.
    Stage s;
    s.born(1, 1000.0);
    s.text(1, 1000.0, "E W9XYZ T E W9XYZ T E W9XYZ T E W9XYY T ", 0.0);
    s.born(2, 5000.0);
    s.text(2, 5000.0, "E W9XYY T ", 30.0);
    s.search.advance(60.0);
    EXPECT_NE(s.row("W9XYZ"), nullptr);
    EXPECT_EQ(s.row("W9XYY"), nullptr);
    s.text(2, 5000.0, "E W9XYY T ", 70.0);
    s.search.advance(100.0);
    EXPECT_NE(s.row("W9XYY"), nullptr);
}

TEST(CallSearch, APooledCallBeatenAtOnePlaceStaysIfItPassesElsewhere) {
    // KC2LRC-2's N5ZC: named once on N5ZO's track (one edit away, read 25 times there), signing three times 5 kHz away. Before rob-1
    // the local defeat withdrew it, N5ZO's best being above its own; pooled, its readings away from N5ZO keep it.
    Stage s;
    s.born(1, 1000.0);
    s.text(1, 1000.0, "CQ N5ZO N5ZO K CQ N5ZO N5ZO K ", 0.0);
    s.born(2, 6000.0);
    s.text(2, 6000.0, "CQ N5ZC K CQ N5ZC K ", 20.0);
    s.search.advance(50.0);
    ASSERT_NE(s.row("N5ZC"), nullptr);
    s.text(1, 1000.0, "N5ZC 5NN CQ N5ZO N5ZO K CQ N5ZO N5ZO K ", 60.0);
    s.search.advance(100.0);
    EXPECT_NE(s.row("N5ZO"), nullptr);
    EXPECT_NE(s.row("N5ZC"), nullptr);
    CallSearchConfig local;
    local.pool_places = 0.0;
    Stage old(local);
    old.born(1, 1000.0);
    old.text(1, 1000.0, "CQ N5ZO N5ZO K CQ N5ZO N5ZO K ", 0.0);
    old.born(2, 6000.0);
    old.text(2, 6000.0, "CQ N5ZC K CQ N5ZC K ", 20.0);
    old.search.advance(50.0);
    ASSERT_NE(old.row("N5ZC"), nullptr);
    old.text(1, 1000.0, "N5ZC 5NN CQ N5ZO N5ZO K CQ N5ZO N5ZO K ", 60.0);
    old.search.advance(100.0);
    EXPECT_EQ(old.row("N5ZC"), nullptr);
}

TEST(CallSearch, ACallSentTwiceWithNoSpaceIsTwoReadings) {
    // glued_pieces_timed: K9BGLK9BGL is the call twice, as K9BGL K9BGL is; at 0 the two pieces share the word's times and are one.
    const auto run = [](double timed) {
        CallSearchConfig config;
        config.glued_pieces_timed = timed;
        Stage s(config);
        s.born(1, 1000.0);
        s.text(1, 1000.0, "CQCQTESTDEK9BGLK9BGLK ", 0.0);
        s.search.advance(40.0);
        return s.row("K9BGL") != nullptr;
    };
    EXPECT_TRUE(run(1.0));
    EXPECT_FALSE(run(0.0));
}

TEST(CallSearch, ReadingsTooFarApartInTimeDoNotAdd) {
    CallSearchConfig config;
    config.window_s = 100.0;
    Stage s(config);
    s.born(1, 1000.0);
    s.text(1, 1000.0, "CQ K1ABC K ", 0.0);
    s.text(1, 1000.0, "CQ K1ABC K ", 500.0);
    s.search.advance(600.0);
    EXPECT_EQ(s.row("K1ABC"), nullptr);
}

TEST(CallSearch, TwoTracksOnOneTransmissionAreOneReading) {
    Stage s;
    s.born(1, 1000.0);
    s.born(2, 1030.0);
    s.text(1, 1000.0, "DE K1ABC ", 0.0);
    s.text(2, 1030.0, "DE K1ABC ", 0.05);
    s.search.advance(30.0);
    EXPECT_EQ(s.row("K1ABC"), nullptr);
}

TEST(CallSearch, TheBetterCopyOfOneTransmissionSetsThePlace) {
    Stage s;
    s.born(1, 1000.0);
    s.born(2, 1200.0);
    s.text(1, 1000.0, "E K1ABC T ", 0.0);  // a neighbor's track hears him bare
    s.text(2, 1200.0, "DE K1ABC ", -0.2);  // his own, at the same moment, has him signing
    s.search.finish();
    const auto all = s.search.candidates();
    ASSERT_EQ(all.size(), 1u);
    EXPECT_EQ(all[0].count, 1u);
    EXPECT_DOUBLE_EQ(all[0].freq_hz, 1200.0);
    EXPECT_TRUE(all[0].framed);
}

TEST(CallSearch, TheSameStationOnANearbyFrequencyIsOneRow) {
    Stage s;
    s.born(1, 1000.0);
    s.text(1, 1000.0, "DE K1ABC ", 0.0);
    s.died(1);
    s.born(2, 1040.0);
    s.text(2, 1040.0, "DE K1ABC ", 40.0);
    s.search.advance(70.0);
    ASSERT_EQ(s.search.calls().size(), 1u);
    EXPECT_DOUBLE_EQ(s.search.calls()[0].freq_hz, 1040.0);
}

TEST(CallSearch, HeardSIsTheLatestOwnReadingAtTheRowsPlace) {
    // K1ABC signs at 1 000 Hz at 0 s and 40 s; at 80 s W2XYZ names him 2 kHz away. last_s moves to the naming, freq_hz and heard_s do
    // not: the app draws the call at 1 000 Hz by when he was last heard there.
    Stage s;
    s.born(1, 1000.0);
    s.text(1, 1000.0, "DE K1ABC K ", 0.0);
    s.text(1, 1000.0, "DE K1ABC K ", 40.0);
    s.search.advance(70.0);
    const CallRow* row = s.row("K1ABC");
    ASSERT_NE(row, nullptr);
    const double heard_s = row->heard_s;
    EXPECT_GE(heard_s, 40.0);
    EXPECT_LT(heard_s, 80.0);
    s.born(2, 3000.0);
    s.text(2, 3000.0, "K1ABC DE W2XYZ K ", 80.0);
    s.search.advance(110.0);
    row = s.row("K1ABC");
    ASSERT_NE(row, nullptr);
    EXPECT_GE(row->last_s, 80.0);
    EXPECT_DOUBLE_EQ(row->freq_hz, 1000.0);
    EXPECT_DOUBLE_EQ(row->heard_s, heard_s);
}

TEST(CallSearch, APartnersCallIsNotGivenTheSendersTrack) {
    Stage s;
    s.born(1, 1000.0);
    s.text(1, 1000.0, "W2XYZ DE K1ABC K ", 0.0);
    s.text(1, 1000.0, "W2XYZ DE K1ABC K ", 40.0);
    s.search.advance(70.0);
    const CallRow* named = s.row("W2XYZ");
    ASSERT_NE(named, nullptr);
    EXPECT_FALSE(named->heard);  // shown as named by another
    EXPECT_FALSE(named->framed);
    const CallRow* sender = s.row("K1ABC");
    ASSERT_NE(sender, nullptr);
    EXPECT_TRUE(sender->heard);
    // He signs at his own place, 200 Hz away: the row goes there, and stays when he is named again.
    s.born(2, 1200.0);
    s.text(2, 1200.0, "K1ABC DE W2XYZ K ", 80.0);
    s.search.advance(110.0);
    EXPECT_TRUE(s.row("W2XYZ")->heard);
    EXPECT_DOUBLE_EQ(s.row("W2XYZ")->freq_hz, 1200.0);
    s.text(1, 1000.0, "W2XYZ DE K1ABC K ", 120.0);
    s.search.advance(150.0);
    EXPECT_DOUBLE_EQ(s.row("W2XYZ")->freq_hz, 1200.0);
    EXPECT_DOUBLE_EQ(s.row("K1ABC")->freq_hz, 1000.0);
}

TEST(CallSearch, AMisreadingLosesToItsBetterRival) {
    Stage s;
    s.born(1, 1000.0);
    for (int i = 0; i < 2; ++i) s.text(1, 1000.0, "DE IK0HBT ", 10.0 * i);
    s.search.advance(40.0);
    ASSERT_NE(s.row("IK0HBT"), nullptr);  // on what is known so far
    for (int i = 0; i < 5; ++i) s.text(1, 1000.0, "DE IK0HBN ", 50.0 + 10.0 * i);
    s.search.advance(120.0);
    EXPECT_NE(s.row("IK0HBN"), nullptr);
    EXPECT_EQ(s.row("IK0HBT"), nullptr);  // taken back
    EXPECT_EQ(s.search.withdrawn(), 1u);
    EXPECT_EQ(s.search.calls().size(), 1u);
}

// The 2026-11-17 80 m recording's WK0B: reported on its signed readings, then nothing of it for over 30 min, then a rival at its place
// that would have beaten what was left of it in the window. It stays; a rival within 30 min of its readings still takes it off (above).
TEST(CallSearch, AReportedCallDoesNotLeaveWhenItsReadingsAgeOutBeforeARivalComes) {
    Stage s;
    s.born(1, 1000.0);
    for (int i = 0; i < 4; ++i) s.text(1, 1000.0, "DE WK0B ", 10.0 * i);
    s.text(1, 1000.0, "DE WK0B ", 1000.0);
    s.search.advance(1030.0);
    ASSERT_NE(s.row("WK0B"), nullptr);
    for (int i = 0; i < 3; ++i) s.text(1, 1000.0, "DE WK0V ", 2500.0 + 10.0 * i);  // one edit from it, at its place
    s.search.advance(2560.0);
    EXPECT_NE(s.row("WK0B"), nullptr);
    EXPECT_NE(s.row("WK0V"), nullptr);  // and the rival's own row: two calls far apart in time
    EXPECT_EQ(s.search.withdrawn(), 0u);
}

// A rival whose readings come within window_s (1 800 s) of the loser's last reading withdraws it; past that, it does not.
TEST(CallSearch, ARivalWithdrawsAReportedCallOnlyWithinTheWindowOfItsLastReading) {
    for (const double after : {1700.0, 1900.0}) {
        Stage s;
        s.born(1, 1000.0);
        for (int i = 0; i < 4; ++i) s.text(1, 1000.0, "DE WK0B ", 10.0 * i);
        s.search.advance(60.0);
        ASSERT_NE(s.row("WK0B"), nullptr);
        for (int i = 0; i < 6; ++i) s.text(1, 1000.0, "DE WK0V ", 30.0 + after + 10.0 * i);
        s.search.advance(30.0 + after + 100.0);
        if (after < 1800.0) {
            EXPECT_EQ(s.row("WK0B"), nullptr) << after;
            EXPECT_EQ(s.search.withdrawn(), 1u) << after;
        } else {
            EXPECT_NE(s.row("WK0B"), nullptr) << after;
            EXPECT_EQ(s.search.withdrawn(), 0u) << after;
        }
        EXPECT_NE(s.row("WK0V"), nullptr) << after;
    }
}

// The list does not depend on how often the stage is advanced: a rival that can count for a withdrawal judged at a loser's last
// reading (within window_s of it, which may be variant_s before now) is not pruned first (review of stage-aging; its scenario).
TEST(CallSearch, TheListDoesNotDependOnWhenUnreportedCallsArePruned) {
    std::istringstream list("WK0V\n");
    const auto known = std::make_shared<const KnownCalls>(KnownCalls::read(list));
    std::vector<std::vector<std::string>> lists;
    std::vector<std::size_t> withdrawn;
    for (const bool extra : {false, true}) {
        Stage s({}, known);
        s.born(1, 1000.0);
        s.born(2, 3000.0);
        s.text(1, 1000.0, "DE WK0B ", 0.0);
        s.text(1, 1000.0, "DE WK0B ", 10.0);
        s.text(1, 1000.0, "WK0V ", 500.0);  // listed, once, bare: not reported, but it beats WK0B at 1 000 Hz
        s.text(2, 3000.0, "DE WK0B ", 1000.0);
        if (extra) s.search.advance(2400.0);
        s.text(2, 3000.0, "DE WK0X ", 2450.0);
        s.text(2, 3000.0, "DE WK0X ", 2460.0);
        s.search.advance(2500.0);
        std::vector<std::string> calls;
        for (const CallRow& r : s.search.calls()) calls.push_back(r.base);
        std::sort(calls.begin(), calls.end());
        lists.push_back(calls);
        withdrawn.push_back(s.search.withdrawn());
    }
    EXPECT_EQ(lists[0], lists[1]);
    EXPECT_EQ(withdrawn[0], withdrawn[1]);
}

TEST(CallSearch, TwoNearCallsFarApartAreTwoStations) {
    Stage s;
    s.born(1, 1000.0);
    s.born(2, 1500.0);
    for (int i = 0; i < 3; ++i) s.text(1, 1000.0, "DE ZL3V ", 10.0 * i);
    for (int i = 0; i < 6; ++i) s.text(2, 1500.0, "DE ZL3YV ", 40.0 + 10.0 * i);
    s.search.advance(130.0);
    EXPECT_NE(s.row("ZL3V"), nullptr);
    EXPECT_NE(s.row("ZL3YV"), nullptr);
}

TEST(CallSearch, CommonWordsAreNoCalls) {
    Stage s;
    s.born(1, 1000.0);
    for (int i = 0; i < 6; ++i) s.text(1, 1000.0, "UR 5NN ANT G5RV RIG K3S LOG N1MM TU5NN ", 10.0 * i);
    s.search.advance(100.0);
    EXPECT_TRUE(s.search.calls().empty());
}

TEST(CallSearch, ACorrectionIsReadAsItStands) {
    Stage s;
    s.born(1, 1000.0);
    s.text(1, 1000.0, "DE K1ABD ", 0.0);
    DecodedTextEvent fix{};
    fix.track_id = 1;
    fix.freq_hz = 1000.0;
    TextCorrection k;
    k.from_index = 3;
    k.chars = symbols("K1ABC ", 0.3);
    fix.corrections.push_back(k);
    s.search.apply(fix);
    s.text(1, 1000.0, "DE K1ABC ", 40.0);
    s.search.advance(70.0);
    ASSERT_NE(s.row("K1ABC"), nullptr);
    EXPECT_EQ(s.row("K1ABC")->count, 2u);
    bool any_abd = false;
    for (const CallRow& r : s.search.candidates()) any_abd = any_abd || r.base == "K1ABD";
    EXPECT_FALSE(any_abd);
}

// A correction reaching back past what has been read, as when the decoder's other stream takes over.
DecodedTextEvent correction(std::uint32_t id, double freq_hz, std::size_t from_index, const std::string& text, double t_s) {
    DecodedTextEvent fix{};
    fix.track_id = id;
    fix.freq_hz = freq_hz;
    TextCorrection k;
    k.from_index = from_index;
    k.chars = symbols(text, t_s);
    fix.corrections.push_back(k);
    return fix;
}

TEST(CallSearch, ACorrectionOfTextAlreadyReadIsReadAgain) {
    Stage s;
    s.born(1, 1000.0);
    s.text(1, 1000.0, "OL1TM OL1TM TU ", 100.0);
    s.search.advance(130.0);
    ASSERT_NE(s.row("OL1TM"), nullptr);
    // 50 s after it was read: the same word, one letter different, and the same time.
    s.search.apply(correction(1, 1000.0, 0, "ZL1TM ZL1TM TU ", 100.0));
    s.search.advance(180.0);
    EXPECT_NE(s.row("ZL1TM"), nullptr);
    EXPECT_EQ(s.row("OL1TM"), nullptr);
    EXPECT_EQ(s.search.withdrawn(), 1u);
}

TEST(CallSearch, WhatTheOldTextGaveStaysUnlessANearCallTookItsPlace) {
    Stage s;
    s.born(1, 1000.0);
    s.text(1, 1000.0, "DE K1ABC K1ABC TU ", 100.0);
    s.search.advance(130.0);
    ASSERT_NE(s.row("K1ABC"), nullptr);
    s.search.apply(correction(1, 1000.0, 0, "DE KXTO T TU ", 100.0));
    s.search.advance(180.0);
    EXPECT_NE(s.row("K1ABC"), nullptr);
}

TEST(CallSearch, ACorrectionThatChangesNothingCountsNothing) {
    Stage s;
    s.born(1, 1000.0);
    s.text(1, 1000.0, "DE K1ABC ", 100.0);
    s.search.advance(130.0);
    s.search.apply(correction(1, 1000.0, 0, "DE K1ABC ", 100.0));
    s.search.advance(180.0);
    s.search.finish();
    EXPECT_EQ(s.row("K1ABC"), nullptr);  // one reading, however often it is read
    for (const CallRow& r : s.search.candidates()) {
        if (r.base == "K1ABC") EXPECT_EQ(r.count, 1u);
    }
}

TEST(CallSearch, ACorrectionInTheLastWordReadReadsTheWholeWord) {
    Stage s;
    s.born(1, 1000.0);
    s.text(1, 1000.0, "DE K1ABD ", 100.0);
    s.search.advance(130.0);
    s.search.apply(correction(1, 1000.0, 7, "C ", 100.7));
    s.search.advance(140.0);
    bool found = false;
    for (const CallRow& r : s.search.candidates()) found = found || r.base == "K1ABC";
    EXPECT_TRUE(found);
}

TEST(CallSearch, TextOlderThanTheHistoryIsNotReadAgain) {
    CallSearchConfig c;
    c.history_s = 20.0;
    Stage s(c);
    s.born(1, 1000.0);
    s.text(1, 1000.0, "K1ABD K1ABD ", 100.0);
    for (double t = 110.0; t <= 500.0; t += 10.0) {
        s.text(1, 1000.0, "ABCD ABCD ABCD ", t);
        s.search.advance(t + 15.0);
    }
    s.search.apply(correction(1, 1000.0, 0, "K2XYZ K2XYZ ", 100.0));
    s.search.advance(520.0);
    EXPECT_EQ(s.row("K2XYZ"), nullptr);
}

TEST(CallSearch, TheReReadCanBeTurnedOff) {
    CallSearchConfig c;
    c.reread_corrected = 0.0;
    Stage s(c);
    s.born(1, 1000.0);
    s.text(1, 1000.0, "OL1TM OL1TM TU ", 100.0);
    s.search.advance(130.0);
    s.search.apply(correction(1, 1000.0, 0, "ZL1TM ZL1TM TU ", 100.0));
    s.search.advance(180.0);
    EXPECT_NE(s.row("OL1TM"), nullptr);
    EXPECT_EQ(s.row("ZL1TM"), nullptr);
}

TEST(CallSearch, AFirstReadingFoundLaterMovesTheFirstTimeBack) {
    Stage s;
    s.born(1, 1000.0);
    s.text(1, 1000.0, "DE K1ABD ", 100.0);
    s.text(1, 1000.0, "DE K1ABC ", 200.0);
    s.search.advance(230.0);
    s.search.apply(correction(1, 1000.0, 0, "DE K1ABC ", 100.0));
    s.search.finish();
    bool found = false;
    for (const CallRow& r : s.search.candidates()) {
        if (r.base == "K1ABC") {
            found = true;
            EXPECT_NEAR(r.first_s, 100.3, 1e-6);
        }
    }
    EXPECT_TRUE(found);
}

TEST(CallSearch, HowOftenTheClockIsGivenDoesNotChangeWhatIsRead) {
    const auto list = [](double step_s) {
        Stage s;
        s.born(1, 1000.0);
        s.text(1, 1000.0, "DE K1ABC K ", 100.0);
        s.text(1, 1000.0, "DE K1ABC K ", 120.0);
        std::vector<double> first;
        for (double t = 100.0; t < 140.0; t += step_s) {
            s.search.advance(t);
            if (!s.search.calls().empty() && first.empty()) first.push_back(std::floor(t));
        }
        return first;
    };
    EXPECT_EQ(list(0.5), list(0.0213));
}

TEST(CallSearch, TheTextIsReadOnlyOnceItHasSettled) {
    CallSearchConfig config;
    config.settle_s = 10.0;
    Stage s(config);
    s.born(1, 1000.0);
    s.text(1, 1000.0, "DE K1ABC ", 0.0);
    s.text(1, 1000.0, "DE K1ABC ", 5.0);
    s.search.advance(9.0);
    EXPECT_TRUE(s.search.candidates().empty());
    s.search.advance(20.0);
    EXPECT_FALSE(s.search.calls().empty());
}

TEST(CallSearch, ATrackThatEndsIsReadAtOnceAndFinishReadsTheRest) {
    Stage s;
    s.born(1, 1000.0);
    s.text(1, 1000.0, "DE K1ABC DE K1ABC ", 0.0);
    s.died(1);
    EXPECT_NE(s.row("K1ABC"), nullptr);
    s.born(2, 2000.0);
    s.text(2, 2000.0, "DE W2XY DE W2XY ", 0.0);
    s.search.finish();
    EXPECT_NE(s.row("W2XY"), nullptr);
}

TEST(CallSearch, AKnownCallNeedsOneFramedReadingOrTwoBareAndSpecialFormsNeedTheList) {
    auto known = std::make_shared<KnownCalls>();
    known->add("K1ABC");
    known->add("TM100TF");
    Stage s(CallSearchConfig{}, known);
    s.born(1, 1000.0);
    s.text(1, 1000.0, "E K1ABC T ", 0.0);
    s.search.advance(15.0);
    EXPECT_EQ(s.row("K1ABC"), nullptr);
    s.text(1, 1000.0, "E K1ABC T ", 15.0);
    s.text(1, 1000.0, "DE TM100TF ", 20.0);
    s.text(1, 1000.0, "DE OL50HQ DE OL50HQ ", 40.0);
    s.search.advance(70.0);
    ASSERT_NE(s.row("K1ABC"), nullptr);
    EXPECT_TRUE(s.row("K1ABC")->known);
    EXPECT_NE(s.row("TM100TF"), nullptr);
    EXPECT_EQ(s.row("OL50HQ"), nullptr);
}

TEST(CallSearch, AListedCallReadOnceBarePassesOnlyIfTheDecoderWasSureOfIt) {
    // known_margin_nats, a variant (off by default), here at 8 nats: a probability of 0.9999 is a margin of ln(0.9999 / 0.0001) = 9.2 nats,
    // 0.99 one of 4.6 nats; a probability of 1 says nothing (a decoder that keeps no margins), an unlisted call is not helped, and a stripped
    // reading (half a point) is never sure.
    auto known = std::make_shared<KnownCalls>();
    known->add("K1ABC");
    CallSearchConfig on;
    on.known_margin_nats = 8.0;
    const auto once = [&](float probability, const std::string& text, const std::string& call, std::shared_ptr<KnownCalls> list,
                          CallSearchConfig config) {
        Stage s(config, list);
        s.born(1, 1000.0);
        s.text(1, 1000.0, text, 0.0, probability);
        s.search.advance(15.0);
        return s.row(call) != nullptr;
    };
    EXPECT_TRUE(once(0.9999f, "E K1ABC T ", "K1ABC", known, on));
    EXPECT_FALSE(once(0.99f, "E K1ABC T ", "K1ABC", known, on));
    EXPECT_FALSE(once(1.0f, "E K1ABC T ", "K1ABC", known, on));
    EXPECT_FALSE(once(0.9999f, "E W1XYZ T ", "W1XYZ", known, on));
    EXPECT_FALSE(once(0.9999f, "E K1ABC T ", "K1ABC", nullptr, on));
    EXPECT_FALSE(once(0.9999f, "E EK1ABC T QRL QRL E EK1ABC T ", "K1ABC", known, on));  // stripped twice: 1 point, never sure
    EXPECT_FALSE(once(0.9999f, "E K1ABC T ", "K1ABC", known, CallSearchConfig{}));  // the default: off
}

TEST(CallSearch, WithAListAWordNamedOncePassesOnlyListed) {
    // named_listed_only: CW Skimmer repeats once for its lists only. With points_named at 1 (not the default since rob-1), without a list
    // the named word passes alone; with the default, a listed call named once passes as framed (points_known), an unlisted one does not.
    auto known = std::make_shared<KnownCalls>();
    known->add("K1ABC");
    CallSearchConfig one;
    one.points_named = 1.0;
    const auto named = [&](const std::string& call, std::shared_ptr<KnownCalls> list, CallSearchConfig config) {
        Stage s(config, list);
        s.born(1, 1000.0);
        s.text(1, 1000.0, call + " 5NN TU ", 0.0);
        s.search.advance(15.0);
        return s.row(call) != nullptr;
    };
    EXPECT_TRUE(named("K1ABC", known, one));
    EXPECT_FALSE(named("W1XYZ", known, one));
    EXPECT_TRUE(named("W1XYZ", nullptr, one));
    CallSearchConfig off = one;
    off.named_listed_only = 0.0;
    EXPECT_TRUE(named("W1XYZ", known, off));
    EXPECT_TRUE(named("K1ABC", known, CallSearchConfig{}));
    EXPECT_FALSE(named("W1XYZ", known, CallSearchConfig{}));
    EXPECT_FALSE(named("W1XYZ", nullptr, CallSearchConfig{}));
}

TEST(CallSearch, AOneLetterSuffixIsHeldByAMuchBetterCallTwoEditsAwayWithItsPrefixAndDigit) {
    // short_variant_ratio, 2: K1T beside K1BX at one place, read three times against seven. Off, K1T stands (two edits, a call of
    // three characters: no pair under the rule of edit distance 1). Another prefix and digit, or a weaker rival, holds nothing back.
    const auto run = [](CallSearchConfig config, const std::string& rival, int rival_times) {
        Stage s(config);
        s.born(1, 1000.0);
        double t = 0.0;
        for (int i = 0; i < 3; ++i, t += 5.0) s.text(1, 1000.0, "DE K1T K ", t);
        for (int i = 0; i < rival_times; ++i, t += 5.0) s.text(1, 1000.0, "DE " + rival + " K ", t);
        s.search.advance(t + 20.0);
        return s.row("K1T") != nullptr;
    };
    EXPECT_FALSE(run(CallSearchConfig{}, "K1BX", 7));
    CallSearchConfig off;
    off.short_variant_ratio = 0.0;
    EXPECT_TRUE(run(off, "K1BX", 7));
    EXPECT_TRUE(run(CallSearchConfig{}, "K2BX", 7));
    EXPECT_TRUE(run(CallSearchConfig{}, "K1BX", 5));
    // The pair's own ratio, not far_variant_ratio: at 3 the rival's 7 points do not reach 3 x 3, at 2 they pass 2 x 3.
    CallSearchConfig three;
    three.short_variant_ratio = 3.0;
    three.far_variant_ratio = 2.0;
    EXPECT_TRUE(run(three, "K1BX", 7));
    CallSearchConfig two;
    two.short_variant_ratio = 2.0;
    two.far_variant_ratio = 3.0;
    EXPECT_FALSE(run(two, "K1BX", 7));
    // More than two edits apart (K1T and K1BXY, three) is no pair. (K1BXYZ, four, has no US form since call-form-1.)
    EXPECT_TRUE(run(CallSearchConfig{}, "K1BXY", 7));
}

TEST(Readings, AMaidenheadLocatorGivesNoCall) {
    // EL96UD would read as the special form EL96UD and, its E stripped, as L96UD; FN31PR as a special form. Neither is read.
    EXPECT_TRUE(is_locator("EL96UD"));
    EXPECT_TRUE(is_locator("FN31PR"));
    EXPECT_FALSE(is_locator("K1ABCD"));
    EXPECT_FALSE(is_locator("ZZ12AB"));  // Z is past R
    EXPECT_FALSE(is_locator("SA12AB"));  // a field's letters stop at R; the subsquare's go to X
    EXPECT_FALSE(is_locator("AS12AB"));
    EXPECT_TRUE(is_locator("AR12XX"));
    EXPECT_FALSE(is_locator("AR12XY"));
    EXPECT_FALSE(is_locator("AR1XAB"));
    EXPECT_FALSE(is_locator("FN31"));
    const std::vector<CallReading> r = readings("K8BAT 5NN EL96UD TU ");
    EXPECT_TRUE(has_base(r, "K8BAT"));
    EXPECT_FALSE(has_base(r, "L96UD"));
    EXPECT_FALSE(has_base(r, "EL96UD"));
    EXPECT_FALSE(has_base(readings("CQ FN31PR K "), "FN31PR"));
}

TEST(CallSearch, ACallOfTinyPiecesNeedsTwoWholeWordsListedOrNot) {
    // Review 5: a comment said a known-call list rescues these. It did not, and a call read this way now needs the same with a list or without.
    auto known = std::make_shared<KnownCalls>();
    known->add("N5AU");
    for (const auto& list : {std::shared_ptr<KnownCalls>{}, known}) {
        Stage s(CallSearchConfig{}, list);
        s.born(1, 1000.0);
        s.text(1, 1000.0, "DE N5AU ", 0.0);
        s.search.advance(15.0);
        EXPECT_EQ(s.row("N5AU"), nullptr);  // framed, once: not enough, on the list or off it
        s.text(1, 1000.0, "DE N5AU K ", 20.0);
        s.search.advance(40.0);
        EXPECT_NE(s.row("N5AU"), nullptr);
    }
    CallSearchConfig off;
    off.tiny_points = 0.0;
    Stage s(off);
    s.born(1, 1000.0);
    s.text(1, 1000.0, "DE N5AU DE N5AU ", 0.0);
    s.search.advance(30.0);
    EXPECT_EQ(s.row("N5AU"), nullptr);
}

TEST(CallSearch, ACallWhoseOnlyDigitIsItsPrefixsIsReportedFromAListOnly) {
    // D4C, E7DX, C6AGU (call-form-2): read, and by default reported only when listed; with prefix_digit_listed_only 0, as any call.
    const auto run = [](CallSearchConfig config, std::shared_ptr<const KnownCalls> known) {
        Stage s(config, std::move(known));
        s.born(1, 1000.0);
        s.text(1, 1000.0, "DE D4C D4C K DE E7DX E7DX K DE C6AGU C6AGU K ", 0.0);
        s.search.advance(30.0);
        std::vector<std::string> out;
        for (const CallRow& r : s.search.calls()) out.push_back(r.base);
        std::sort(out.begin(), out.end());
        return out;
    };
    EXPECT_EQ(CallSearchConfig{}.prefix_digit_listed_only, 1.0);
    EXPECT_TRUE(run(CallSearchConfig{}, nullptr).empty());
    auto known = std::make_shared<KnownCalls>();
    for (const char* call : {"D4C", "E7DX"}) known->add(call);
    EXPECT_EQ(run(CallSearchConfig{}, known), (std::vector<std::string>{"D4C", "E7DX"}));
    CallSearchConfig any;
    any.prefix_digit_listed_only = 0.0;
    EXPECT_EQ(run(any, nullptr), (std::vector<std::string>{"C6AGU", "D4C", "E7DX"}));
    // The readings carry the mark.
    const auto r = readings("DE C6AGU K");
    const auto it = std::find_if(r.begin(), r.end(), [](const CallReading& x) { return x.base == "C6AGU"; });
    ASSERT_NE(it, r.end());
    EXPECT_TRUE(it->prefix_digit);
}

TEST(CallSearch, AWordOfCwWithACallsFormIsReportedFromAListOnly) {
    // K9AY and N1MM (MASTER.SCP holds both; the FCC's file has them active), DJ5NN (listed): from a list only, always. FN5NN, a
    // prefix glued to 5NN that no list holds, is never reported, and with no list neither is any of them.
    const auto run = [](std::shared_ptr<const KnownCalls> known) {
        Stage s(CallSearchConfig{}, std::move(known));
        s.born(1, 1000.0);
        s.text(1, 1000.0, "DE K9AY K9AY K DE N1MM N1MM K DE DJ5NN DJ5NN K TU FN5NN FN5NN K ", 0.0);
        s.search.advance(30.0);
        std::vector<std::string> out;
        for (const CallRow& r : s.search.calls()) out.push_back(r.base);
        std::sort(out.begin(), out.end());
        return out;
    };
    EXPECT_TRUE(run(nullptr).empty());
    auto known = std::make_shared<KnownCalls>();
    for (const char* call : {"K9AY", "N1MM", "DJ5NN"}) known->add(call);
    EXPECT_EQ(run(known), (std::vector<std::string>{"DJ5NN", "K9AY", "N1MM"}));
    const auto r = readings("DE K9AY K");
    const auto it = std::find_if(r.begin(), r.end(), [](const CallReading& x) { return x.base == "K9AY"; });
    ASSERT_NE(it, r.end());
    EXPECT_TRUE(it->cw_word);
}

TEST(CallSearch, AFullCallAfterAPrefixOfACallsFormIsReportedAsTheBase) {
    // VP2V/K1ABC: once refused, now K1ABC's reading, shown with its slash once seen twice.
    Stage s;
    s.born(1, 1000.0);
    s.text(1, 1000.0, "CQ DE VP2V/K1ABC VP2V/K1ABC K ", 0.0);
    s.search.advance(30.0);
    const CallRow* r = s.row("K1ABC");
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(r->call, "VP2V/K1ABC");
    EXPECT_EQ(s.row("VP2V"), nullptr);
}

TEST(CallSearch, AWordNamedOnceIsNotEnoughByDefault) {
    // rob-1: points_named 2, no exception for a named word without a list (CW Skimmer's practice); a second reading anywhere passes it.
    Stage once;
    once.born(1, 1000.0);
    once.text(1, 1000.0, "NQ6B 5NN TU ", 0.0);
    once.search.advance(15.0);
    EXPECT_EQ(once.row("NQ6B"), nullptr);
    once.born(2, 5000.0);  // the caller's own signal, elsewhere
    once.text(2, 5000.0, "DE NQ6B K ", 30.0);
    once.search.advance(50.0);
    EXPECT_NE(once.row("NQ6B"), nullptr);
}

TEST(CallSearch, AWholeWordNamedOnceIsEnoughAtOnePoint) {
    CallSearchConfig one;
    one.points_named = 1.0;
    Stage s(one);
    s.born(1, 1000.0);
    s.text(1, 1000.0, "NQ6B 5NN TU ", 0.0);
    s.search.advance(15.0);
    ASSERT_NE(s.row("NQ6B"), nullptr);
    EXPECT_FALSE(s.row("NQ6B")->heard);  // named only: the row says so, and its frequency is the namer's
    Stage bare(one);
    bare.born(1, 1000.0);
    bare.text(1, 1000.0, "E K1ABC T ", 0.0);
    bare.search.advance(15.0);
    EXPECT_EQ(bare.row("K1ABC"), nullptr);
    Stage stripped(one);  // a stripped word's half a point does not pass as named
    stripped.born(1, 1000.0);
    stripped.text(1, 1000.0, "EK1ABC 5NN ", 0.0);
    stripped.search.advance(15.0);
    EXPECT_EQ(stripped.row("K1ABC"), nullptr);
    CallSearchConfig off;
    off.points_named = 1e9;
    Stage refused(off);
    refused.born(1, 1000.0);
    refused.text(1, 1000.0, "NQ6B 5NN TU ", 0.0);
    refused.search.advance(15.0);
    EXPECT_EQ(refused.row("NQ6B"), nullptr);
}

TEST(CallSearch, LettersJoinedAcrossSpacesPassOnTheThirdReading) {
    Stage s;
    s.born(1, 1000.0);
    s.text(1, 1000.0, "K1 ABC ", 0.0);
    s.text(1, 1000.0, "K1 ABC ", 5.0);
    s.search.advance(20.0);
    EXPECT_EQ(s.row("K1ABC"), nullptr);  // 1.5 points
    s.text(1, 1000.0, "K1 ABC ", 20.0);
    s.search.advance(40.0);
    EXPECT_NE(s.row("K1ABC"), nullptr);  // 2.25 points
}

TEST(CallSearch, ACallThatStartsAnotherIsHeldAsAVariant) {
    Stage s;
    s.born(1, 1000.0);
    s.text(1, 1000.0, "DE KX9F DE KX9F DE KX9F K ", 0.0);
    s.text(1, 1000.0, "DE KX9FKX DE KX9FKX ", 20.0);  // edit distance 2 from KX9F, at 2 points against 3 and 2 letters from it
    s.search.advance(40.0);
    EXPECT_NE(s.row("KX9F"), nullptr);
    EXPECT_EQ(s.row("KX9FKX"), nullptr);
    CallSearchConfig off;
    off.prefix_held = 0.0;
    Stage free(off);
    free.born(1, 1000.0);
    free.text(1, 1000.0, "DE KX9F DE KX9F DE KX9F K ", 0.0);
    free.text(1, 1000.0, "DE KX9FKX DE KX9FKX ", 20.0);
    free.search.advance(40.0);
    EXPECT_NE(free.row("KX9FKX"), nullptr);
}

TEST(CallSearch, ALocalDefeatDoesNotWithdrawACallThatStandsElsewhere) {
    // Review 5: a call doubled and signed at 1 kHz left the list for a misreading at 2 kHz, 60 s later.
    // withdraw_own_place is read with pool_places 0 only; pooled, the call stays as well (APooledCallBeatenAtOnePlaceStaysIfItPassesElsewhere).
    const auto run = [](double own_place, double pool = 0.0) {
        CallSearchConfig config;
        config.withdraw_own_place = own_place;
        config.pool_places = pool;
        auto s = std::make_unique<Stage>(config);
        s->born(1, 1000.0);
        s->text(1, 1000.0, "CQ TEST W9XYZ W9XYZ K ", 0.0);
        s->search.advance(15.0);
        EXPECT_NE(s->row("W9XYZ"), nullptr);
        s->born(2, 2000.0);
        s->text(2, 2000.0, "W9XYZ W9XYY W9XYY K ", 60.0);
        s->search.advance(90.0);
        return s;
    };
    const auto kept = run(1.0);
    EXPECT_NE(kept->row("W9XYZ"), nullptr);
    EXPECT_NE(kept->row("W9XYY"), nullptr);
    const auto as_merged = run(0.0);
    EXPECT_EQ(as_merged->row("W9XYZ"), nullptr);
    EXPECT_NE(as_merged->row("W9XYY"), nullptr);
    const auto pooled = run(0.0, 1.0);
    EXPECT_NE(pooled->row("W9XYZ"), nullptr);
    EXPECT_NE(pooled->row("W9XYY"), nullptr);
}

TEST(CallSearch, ALocalDefeatStillWithdrawsAMisreadingThatStandsNowhereElse) {
    Stage s;
    s.born(1, 1000.0);
    s.text(1, 1000.0, "DE W9XYY DE W9XYY K ", 0.0);
    s.search.advance(15.0);
    ASSERT_NE(s.row("W9XYY"), nullptr);
    s.text(1, 1000.0, "DE W9XYZ DE W9XYZ DE W9XYZ K ", 20.0);
    s.search.advance(40.0);
    EXPECT_NE(s.row("W9XYZ"), nullptr);
    EXPECT_EQ(s.row("W9XYY"), nullptr);
}

TEST(CallSearch, TracksAreReadInTheOrderOfTheirIds) {
    // Review 5: the same events gave 1 call on one platform and 2 on another, by the hash table's order. A tie at distance 1 on one word
    // goes to the later candidate, so here the track read last wins: the highest id, on every platform.
    Stage s;
    for (std::uint32_t id = 1; id <= 9; ++id) s.born(id, 1000.0 + 10.0 * id);
    for (std::uint32_t id = 1; id <= 9; ++id) s.text(id, 1000.0 + 10.0 * id, id == 9 ? "DE W9XYZ W9XYZ K " : "DE W9XYY W9XYY K ", 0.0);
    s.search.advance(20.0);
    EXPECT_NE(s.row("W9XYZ"), nullptr);
    EXPECT_EQ(s.search.calls().size(), 1u);
}

TEST(CallSearch, ItWorksWithoutAList) {
    Stage s;
    s.born(1, 1000.0);
    s.text(1, 1000.0, "DE TM100TF DE TM100TF ", 0.0);
    s.search.advance(30.0);
    EXPECT_TRUE(s.search.calls().empty());
}

TEST(CallSearch, ABetterRivalOutweighsAKnownCallOnlyByFar) {
    auto known = std::make_shared<KnownCalls>();
    known->add("K1ABD");
    Stage s(CallSearchConfig{}, known);
    s.born(1, 1000.0);
    for (int i = 0; i < 2; ++i) s.text(1, 1000.0, "DE K1ABC ", 10.0 * i);
    s.text(1, 1000.0, "DE K1ABD ", 30.0);
    s.search.advance(60.0);
    EXPECT_NE(s.row("K1ABD"), nullptr);  // on the list, with 1 point against 2
    for (int i = 0; i < 4; ++i) s.text(1, 1000.0, "DE K1ABC ", 70.0 + 10.0 * i);
    s.search.advance(130.0);
    EXPECT_EQ(s.row("K1ABD"), nullptr);
    EXPECT_NE(s.row("K1ABC"), nullptr);
}

TEST(CallSearch, ThePortableFormShowsOnceSeenTwice) {
    Stage s;
    s.born(1, 1000.0);
    s.text(1, 1000.0, "DE K1ABC/P ", 0.0);
    s.text(1, 1000.0, "DE K1ABC ", 20.0);
    s.search.advance(50.0);
    ASSERT_NE(s.row("K1ABC"), nullptr);
    EXPECT_EQ(s.row("K1ABC")->call, "K1ABC");
    s.text(1, 1000.0, "DE K1ABC/P ", 60.0);
    s.search.advance(90.0);
    EXPECT_EQ(s.row("K1ABC")->call, "K1ABC/P");
    EXPECT_EQ(s.search.calls().size(), 1u);
}

TEST(CallSearch, CandidatesThatAgeOutLeave) {
    CallSearchConfig config;
    config.window_s = 100.0;
    config.variant_s = 100.0;
    Stage s(config);
    s.born(1, 1000.0);
    s.text(1, 1000.0, "DE K1ABC ", 0.0);
    s.search.advance(30.0);
    EXPECT_EQ(s.search.candidates().size(), 1u);
    s.search.advance(500.0);
    EXPECT_TRUE(s.search.candidates().empty());
}

TEST(CallsCsv, ColumnsAndValues) {
    CallRow r;
    r.call = "K1ABC/P";
    r.base = "K1ABC";
    r.freq_hz = -1500.25;
    r.count = 3;
    r.points = 2.5;
    r.heard = true;
    const std::string csv = calls_csv({r});
    EXPECT_NE(csv.find("call,base,freq_hz,freq_khz,"), std::string::npos);
    EXPECT_NE(csv.find("K1ABC/P,K1ABC,-1500.2"), std::string::npos);
    const std::string absolute = calls_csv({r}, 7031500.0);
    EXPECT_NE(absolute.find("7029.9"), std::string::npos);
    EXPECT_NE(csv.find(",framed,first_utc,last_utc\n"), std::string::npos);
    EXPECT_NE(csv.find(",0,1,0,,\n"), std::string::npos);  // known, heard, framed; no times without a start
}

TEST(CallsCsv, TimesInUtcWhenTheStreamsStartIsKnown) {
    CallRow r;
    r.call = r.base = "K1ABC";
    r.first_s = 12.25;
    r.last_s = 3600.0;
    const std::string csv = calls_csv({r}, std::nullopt, 1503338400093);  // 2017-08-21 18:00:00.093 UTC
    EXPECT_NE(csv.find(",2017-08-21T18:00:12.343Z,2017-08-21T19:00:00.093Z\n"), std::string::npos);
}

TEST(Words, ANeighborsSymbolsEndAWordAndBelongToNone) {
    // KQ2RP's track: his call, then his runner's TU W1AW heard through the skirt, glued on (DecodedSymbol::heard_from).
    std::vector<DecodedSymbol> s = symbols("KQ2RPTUW1AW CQ");
    for (std::size_t i = 5; i < 11; ++i) s[i].heard_from = 9;
    const auto all = words_of(s);
    ASSERT_EQ(all.size(), 2u);
    EXPECT_EQ(all[0].text, "KQ2RPTUW1AW");
    const auto own = words_of(s, true);
    ASSERT_EQ(own.size(), 2u);
    EXPECT_EQ(own[0].text, "KQ2RP");
    EXPECT_EQ(own[0].last, 5u);
    EXPECT_EQ(own[1].text, "CQ");
    EXPECT_EQ(own[1].first, 12u);
}

TEST(CallSearch, ANeighborsTextOnATrackIsReadOnlyOnTheNeighborsTrack) {
    // The runner's call heard on his caller's track, 45 Hz away: not read there where the runner's own track holds the same symbols at
    // the same times; read with skip_neighbors_text 0, and read where the runner's track holds something else then (a second reading).
    for (const int mode : {0, 1, 2}) {  // 0: skip, the runner holds it; 1: skip, the runner holds other text; 2: no skip
        CallSearchConfig c;
        c.skip_neighbors_text = mode == 2 ? 0.0 : 1.0;
        Stage stage(c);
        std::vector<std::pair<std::string, double>> read;
        stage.search.set_reading_log([&](const CallReading& r, double f) { read.emplace_back(r.base, f); });
        stage.born(1, 2000.0);
        stage.born(2, 2045.0);
        DecodedTextEvent e{};
        e.track_id = 2;
        e.freq_hz = 2045.0;
        e.chars = symbols("KQ2RP TU W1AW W1AW", 1.0);
        for (std::size_t i = 6; i < e.chars.size(); ++i)
            if (e.chars[i].text != " ") e.chars[i].heard_from = 1;
        // The runner's own track: the same symbols at the same times, or another reading of them.
        stage.text(1, 2000.0, mode == 1 ? "       TU W1AX W1AX" : "       TU W1AW W1AW", 0.9);
        stage.search.apply(e);
        stage.died(2);
        stage.died(1);
        stage.search.finish();
        const bool own = std::any_of(read.begin(), read.end(), [](const auto& r) { return r.first == "KQ2RP"; });
        const bool there = std::any_of(read.begin(), read.end(), [](const auto& r) { return r.first == "W1AW" && r.second == 2045.0; });
        EXPECT_TRUE(own) << mode;
        EXPECT_EQ(there, mode != 0) << mode;
    }
}
TEST(CallSearch, AFrameWordPartlyHeardFromANeighborIsReadWholeAndKeepsTheRole) {
    // Review 18's case (KC2LRC-2): W7QDM's own track reads "TEST W7QDM", but its "TES" is given to a track 27 Hz away that heard the
    // keying more strongly and holds the same three letters. The stage never acts on part of a word: TEST stays whole, so the call is
    // still the sender's. (The variant skip_neighbors_text 1; off by default.)
    CallSearchConfig on;
    on.skip_neighbors_text = 1.0;
    Stage stage(on);
    std::vector<std::pair<CallReading, double>> read;
    stage.search.set_reading_log([&](const CallReading& r, double f) { read.emplace_back(r, f); });
    stage.born(1, 1973.0);
    stage.born(2, 2000.0);
    stage.text(1, 1973.0, "TES", 1.0);
    DecodedTextEvent e{};
    e.track_id = 2;
    e.freq_hz = 2000.0;
    e.chars = symbols("TEST W7QDM", 1.0);
    for (std::size_t i = 0; i < 3; ++i) e.chars[i].heard_from = 1;
    stage.search.apply(e);
    stage.died(2);
    stage.died(1);
    stage.search.finish();
    const auto it = std::find_if(read.begin(), read.end(), [](const auto& r) { return r.first.base == "W7QDM"; });
    ASSERT_NE(it, read.end());
    EXPECT_EQ(it->first.role, Role::Sender);
}

TEST(CallSearch, AWholeWordHeardFromANeighborIsSkippedEvenBesideTheTracksOwn) {
    // The same, its whole word "TEST" given to the neighbor, which holds it: skipped, so W7QDM is bare on this track (the neighbor's
    // track reads the frame). The variant skip_neighbors_text 1.
    CallSearchConfig on;
    on.skip_neighbors_text = 1.0;
    Stage stage(on);
    std::vector<std::pair<CallReading, double>> read;
    stage.search.set_reading_log([&](const CallReading& r, double f) { read.emplace_back(r, f); });
    stage.born(1, 1973.0);
    stage.born(2, 2000.0);
    stage.text(1, 1973.0, "TEST", 1.0);
    DecodedTextEvent e{};
    e.track_id = 2;
    e.freq_hz = 2000.0;
    e.chars = symbols("TEST W7QDM", 1.0);
    for (std::size_t i = 0; i < 4; ++i) e.chars[i].heard_from = 1;
    stage.search.apply(e);
    stage.died(2);
    stage.died(1);
    stage.search.finish();
    const auto it = std::find_if(read.begin(), read.end(), [](const auto& r) { return r.first.base == "W7QDM" && r.second == 2000.0; });
    ASSERT_NE(it, read.end());
    EXPECT_EQ(it->first.role, Role::Unknown);
}
TEST(CallSearch, ANeighborsTextIsReadByDefault) {
    // skip_neighbors_text is off by default (review 18): every symbol is read, as before copy-2, whatever heard_from says.
    EXPECT_EQ(CallSearchConfig{}.skip_neighbors_text, 0.0);
    Stage stage;
    std::vector<std::pair<std::string, double>> read;
    stage.search.set_reading_log([&](const CallReading& r, double f) { read.emplace_back(r.base, f); });
    stage.born(1, 2000.0);
    stage.born(2, 2045.0);
    stage.text(1, 2000.0, "       TU W1AW W1AW", 0.9);
    DecodedTextEvent e{};
    e.track_id = 2;
    e.freq_hz = 2045.0;
    e.chars = symbols("KQ2RP TU W1AW W1AW", 1.0);
    for (std::size_t i = 6; i < e.chars.size(); ++i)
        if (e.chars[i].text != " ") e.chars[i].heard_from = 1;
    stage.search.apply(e);
    stage.died(2);
    stage.died(1);
    stage.search.finish();
    EXPECT_TRUE(std::any_of(read.begin(), read.end(), [](const auto& r) { return r.first == "W1AW" && r.second == 2045.0; }));
}

TEST(CallSearch, SignedIsASendersReadingOnly) {
    // The list's option "signed calls only" (the owner's ruling, 2026-11-18) shows the rows with a reading in the role Sender; a call
    // sent twice is framed for the report test but has not signed, nor has a call only named by another.
    Stage s;
    s.born(1, 1000.0);
    s.born(2, 3000.0);
    s.born(3, 5000.0);
    s.born(4, 7000.0);
    for (const double t : {0.0, 40.0}) {
        s.text(1, 1000.0, "CQ DE K1ABC ", t);
        s.text(2, 3000.0, "W2XY ", t);
        s.text(3, 5000.0, "N5JJ N5JJ ", t);
        s.text(4, 7000.0, "VE3ABC 5NN ", t);
    }
    s.search.advance(70.0);
    ASSERT_NE(s.row("K1ABC"), nullptr);
    ASSERT_NE(s.row("W2XY"), nullptr);
    ASSERT_NE(s.row("N5JJ"), nullptr);
    ASSERT_NE(s.row("VE3ABC"), nullptr);
    EXPECT_TRUE(is_signed(*s.row("K1ABC")));
    EXPECT_FALSE(is_signed(*s.row("W2XY")));    // bare
    EXPECT_FALSE(is_signed(*s.row("N5JJ")));    // sent twice: framed for the report test, not signed
    EXPECT_FALSE(is_signed(*s.row("VE3ABC")));  // named only
    EXPECT_FALSE(s.row("VE3ABC")->heard);
    EXPECT_EQ(std::count_if(s.search.calls().begin(), s.search.calls().end(), is_signed), 1);
    // One signed reading among bare ones is enough, and it stays.
    s.text(2, 3000.0, "TEST W2XY ", 80.0);
    s.text(2, 3000.0, "W2XY ", 120.0);
    s.search.advance(150.0);
    EXPECT_TRUE(is_signed(*s.row("W2XY")));
}

TEST(CallSearch, ANewKnownCallListMarksTheCallsHeld) {
    CallSearchConfig config;
    config.points_framed = 3.0;  // so that a listed call (points_known 1) passes where an unlisted one does not
    auto list = std::make_shared<KnownCalls>();
    list->add("W2XY");
    list->add("K1ABC");
    const auto run = [&](bool give_list) {
        Stage s(config);
        s.born(1, 1000.0);
        s.born(2, 5000.0);
        s.text(1, 1000.0, "CQ DE W2XY ", 0.0);
        s.text(2, 5000.0, "CQ DE K1ABC CQ DE K1ABC CQ DE K1ABC ", 0.0);
        s.search.advance(30.0);
        EXPECT_EQ(s.row("W2XY"), nullptr);
        EXPECT_NE(s.row("K1ABC"), nullptr);
        if (give_list) {
            s.search.set_known(list);
            EXPECT_EQ(s.search.known(), list);
            const std::vector<CallRow> held = s.search.candidates();
            const auto w = std::find_if(held.begin(), held.end(), [](const CallRow& r) { return r.base == "W2XY"; });
            EXPECT_TRUE(w != held.end() && w->known);  // marked now, judged at its next reading
            EXPECT_EQ(s.row("W2XY"), nullptr);
            EXPECT_TRUE(s.row("K1ABC")->known);  // a reported row too
        }
        s.text(1, 1000.0, "DE W2XY ", 40.0);
        s.search.advance(70.0);
        return s.row("W2XY") != nullptr;
    };
    EXPECT_FALSE(run(false));
    EXPECT_TRUE(run(true));
    Stage s(config, list);
    s.born(1, 1000.0);
    s.text(1, 1000.0, "CQ DE K1ABC ", 0.0);
    s.search.advance(30.0);
    ASSERT_NE(s.row("K1ABC"), nullptr);
    EXPECT_TRUE(s.row("K1ABC")->known);
    s.search.set_known(nullptr);
    EXPECT_FALSE(s.row("K1ABC")->known);  // the list taken away: the row stays, unlisted
}

// ---- Joined words: segment_word and the segment_joined variant (branch joined-words-1)

namespace {

std::vector<CallReading> readings_with(const std::string& text, int mode, int alternatives = 0) {
    const std::vector<DecodedSymbol> s = symbols(text);
    const std::vector<Word> w = words_of(s);
    ReadOptions options;
    options.segment_joined = mode;
    options.segment_alternatives = alternatives;
    return read_calls(w, 0, w.size(), s, options);
}

const CallReading* reading_of(const std::vector<CallReading>& r, const std::string& base) {
    for (const CallReading& x : r)
        if (x.base == base) return &x;
    return nullptr;
}

// The segments' texts, a believed call marked with *.
std::string cut(const std::string& word) {
    std::string out;
    for (const Segment& s : segment_word(word)) {
        if (!out.empty()) out += ' ';
        out += s.text;
        if (s.kind == Segment::Kind::Call && s.believed) out += '*';
    }
    return out;
}

std::size_t count_of(const std::vector<CallReading>& r, const std::string& base) {
    return static_cast<std::size_t>(std::count_if(r.begin(), r.end(), [&](const CallReading& x) { return x.base == base; }));
}

}  // namespace

TEST(JoinedWords, EK8DXsTextsAreCutIntoKeywordsAndTheCall) {
    // The defaults' text of EK8DX on 80 m (2026-11-17): word gaps from 3.5 dots, so the decoder joined every word.
    EXPECT_EQ(cut("CQCQCQDXCQDXCQDXDEK8DXK8DXK8DXK"), "CQ CQ CQ DX CQ DX CQ DX DE K8DX* K8DX* K8DX* K");
    EXPECT_EQ(cut("CQCQDXCQDXCQDXCQDXDEK8DXK8DX"), "CQ CQ DX CQ DX CQ DX CQ DX DE K8DX* K8DX*");
    EXPECT_EQ(cut("DEK8DXK8DXK8DXK"), "DE K8DX* K8DX* K8DX* K");
    // A garbled last copy: the believed call is read, the other is not.
    EXPECT_EQ(cut("CQCQDXCQDXCQDXDEK8DXK8DK"), "CQ CQ DX CQ DX CQ DX DE K8DX* K8DK");
    const std::string text = "CQCQCQDXCQDXCQDXDEK8DXK8DXK8DXK CQCQDXCQDXCQDXCQDXDEK8DXK8DX KQDX K";
    EXPECT_FALSE(has_base(readings(text), "K8DX"));  // split_glued alone cannot read it
    for (int mode : {1, 2}) {
        const auto r = readings_with(text, mode);
        EXPECT_EQ(count_of(r, "K8DX"), 5u) << mode;
        for (const CallReading& x : r) {
            if (x.base != "K8DX") continue;
            EXPECT_EQ(x.role, Role::Sender);
            EXPECT_EQ(x.reading, Reading::Glued);
            EXPECT_TRUE(x.doubled);
        }
    }
    // Each copy has its own characters' times (0.1 s a character): the first K8DX is characters 18 to 21.
    const auto r = readings_with(text, 1);
    ASSERT_FALSE(r.empty());
    EXPECT_NEAR(r.front().start_s, 1.8, 1e-9);
    EXPECT_NEAR(r.front().end_s, 2.2, 1e-9);
}

TEST(JoinedWords, ACallDeACall) {
    // A K after a call is likelier than a call ending in K (the lengths' shares and the K's bigram); a DE after a call (1.0 nats) is
    // not enough alone, a DE before one (2.77) is.
    EXPECT_EQ(cut("N5JJDEWA0QZK"), "N5JJ DE WA0QZ* K");
    const auto r = readings_with("N5JJDEWA0QZK", 2);
    EXPECT_FALSE(has_base(r, "N5JJ"));
    EXPECT_TRUE(has(r, "WA0QZ", Role::Sender));
    EXPECT_EQ(cut("N5JJDEWA0QZKK"), "N5JJ DE WA0QZK* K");
    // A string that is a shorter one repeated is no call: periodic garbage would be its own repeat.
    EXPECT_EQ(cut("ATTM3J3J3J3JIGS").find('*'), std::string::npos);
    EXPECT_FALSE(has_base(readings_with("ATTM3J3J3J3JIGS", 1), "3J3J"));
    // As 1, split_glued reads it first, as by default.
    const auto first = readings_with("N5JJDEWA0QZK", 1);
    EXPECT_TRUE(has(first, "N5JJ", Role::Named));
    EXPECT_TRUE(has(first, "WA0QZK", Role::Sender));
    EXPECT_EQ(cut("CQCQPOTADENA9M"), "CQ CQ POTA DE NA9M*");
    EXPECT_EQ(cut("CQPOKDEKG5VNQ"), "CQ PO K DE KG5VNQ*");
}

TEST(JoinedWords, ACallThatIsThePrefixOfAnotherIsCutWhereTheRepeatsSay) {
    EXPECT_EQ(cut("DEK1ABCK1ABC"), "DE K1ABC* K1ABC*");  // K1AB is a call too
    EXPECT_EQ(cut("W1AWW1AWK"), "W1AW* W1AW* K");        // and W1AWW
    EXPECT_EQ(cut("DEW4PMK"), "DE W4PM* K");
}

TEST(JoinedWords, PlainWordsReadAsByDefault) {
    for (const std::string text : {"CQ CQ DE K1ABC K1ABC K", "W2XYZ DE K1ABC GM", "DEK1ABC", "N5JJDEWA0QZK", "CQCQTESTDEK9BGLK9BGLK",
                                   "K1ABC5NN", "TNX FER CALL UR RST 599 599", "EIK0HBN", "DE K X 9 F K"}) {
        const auto a = readings_with(text, 0), b = readings_with(text, 1);
        ASSERT_EQ(a.size(), b.size()) << text;
        for (std::size_t k = 0; k < a.size(); ++k) {
            EXPECT_EQ(a[k].full, b[k].full) << text;
            EXPECT_EQ(a[k].role, b[k].role) << text;
            EXPECT_EQ(a[k].reading, b[k].reading) << text;
        }
    }
    // Words of CW that are no call stay none.
    for (const std::string text : {"TNXFERCALL", "CQCQCQDXCQDX", "GMOMTNXFB", "5NNTU73", "RRRTU73"}) {
        if (std::getenv("JW_DEBUG")) std::printf("%s -> %s\n", text.c_str(), cut(text).c_str());
        EXPECT_TRUE(readings_with(text, 2).empty()) << text;
    }
}

TEST(JoinedWords, GarbledTextGivesNoCall) {
    // A call's form alone, among strays, is not enough: no repeat, no DE beside it.
    EXPECT_TRUE(readings_with("EITTEAK1ABCNIEMTS", 2).empty());
    EXPECT_TRUE(readings_with("TEEHK3ZZTIMNAE", 2).empty());
    // A sign-off alone is not enough either (0.39 + 1.5 nats).
    EXPECT_TRUE(readings_with("ETTAK1ABCK", 1).empty());
    // Noise text: characters drawn with weight 2^-(their count of dots and dashes), as a decoder reading noise favors short ones
    // (E, T, I, A, N, M, ...), 12 to 40 characters a word. The rate of words with a believed call is printed and bounded.
    std::uint64_t state = 12345;
    const auto next = [&] {
        state = state * 6364136223846793005ULL + 1442695040888963407ULL;
        return static_cast<double>(state >> 11) / 9007199254740992.0;
    };
    const std::string alphabet = "ETIANMSURWDKGOHVFLPJBXCYZQ0123456789";
    const int elements[] = {1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 3, 3, 3, 3, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5};
    std::vector<double> cdf;
    double sum = 0.0;
    for (int e : elements) cdf.push_back(sum += std::pow(2.0, -e));
    int believed = 0;
    const int words = 2000;
    for (int k = 0; k < words; ++k) {
        const int length = 12 + static_cast<int>(next() * 29.0);
        std::string w;
        for (int c = 0; c < length; ++c) {
            const double u = next() * sum;
            const auto at = static_cast<std::size_t>(std::lower_bound(cdf.begin(), cdf.end(), u) - cdf.begin());
            w += alphabet[std::min(at, alphabet.size() - 1)];
        }
        for (const Segment& s : segment_word(w)) {
            if (s.kind == Segment::Kind::Call && s.believed) {
                ++believed;
                if (std::getenv("JW_DEBUG")) std::printf("%s -> %s\n", w.c_str(), cut(w).c_str());
                break;
            }
        }
    }
    std::printf("noise words with a believed call: %d of %d\n", believed, words);
    // Measured: 0 of 2 000 (the generator is seeded, so the count is exact for this code). The margin of 2 lets a change of costs
    // move a word or two without failing; 3 or more (0.15%, the 95% upper bound "rule of three" gives for 0 in 2 000) fails.
    EXPECT_LE(believed, 2);
}

TEST(JoinedWords, FormGains) {
    // call_form_nats is the grammar's: -ln of the share of random strings of letters and digits that parse_call takes (its table was
    // made with 2 000 000 strings a length; 200 000 here, within 0.1 nats: about 4 standard errors at 9 characters, share 1%).
    std::uint64_t state = 99;
    const auto next = [&] {
        state = state * 6364136223846793005ULL + 1442695040888963407ULL;
        return static_cast<std::size_t>((state >> 33) % 36);
    };
    const std::string alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    // With FORM_TABLE set, 2 000 000 strings a length and the gains printed: how the table is made.
    const bool table = std::getenv("FORM_TABLE") != nullptr;
    for (std::size_t n = 3; n <= 9; ++n) {
        int hits = 0;
        const int draws = table ? 2000000 : 200000;
        for (int k = 0; k < draws; ++k) {
            std::string w;
            for (std::size_t c = 0; c < n; ++c) w += alphabet[next()];
            if (parse_call(w)) ++hits;
        }
        if (table) std::printf("call_form_nats(%zu) = %.2f nats (%d of %d)\n", n, -std::log(hits / static_cast<double>(draws)), hits, draws);
        EXPECT_NEAR(call_form_nats(n), -std::log(hits / static_cast<double>(draws)), 0.1) << n;
    }
    // The shares of call lengths sum to 1 (3 to 12 characters).
    double sum = 0.0;
    for (std::size_t n = 3; n <= 12; ++n) sum += std::exp(call_length_nats(n));
    EXPECT_NEAR(sum, 1.0, 0.01);
}

TEST(JoinedWords, TheStageReportsEK8DXByDefaultAndNotWithTheSegmentationOff) {
    const auto run = [](CallSearchConfig config) {
        Stage s(config);
        s.born(1, 1000.0);
        s.text(1, 1000.0, "CQCQCQDXCQDXCQDXDEK8DXK8DXK8DXK ", 0.0);
        s.search.advance(60.0);
        const CallRow* r = s.row("K8DX");
        return r != nullptr && is_signed(*r) && r->count == 3;
    };
    EXPECT_EQ(CallSearchConfig{}.segment_joined, 1.0);
    EXPECT_EQ(CallSearchConfig{}.segment_alternatives, 1.0);
    EXPECT_TRUE(run(CallSearchConfig{}));
    for (double mode : {0.0, 1.0, 2.0}) {
        CallSearchConfig config;
        config.segment_joined = mode;
        EXPECT_EQ(run(config), mode != 0.0) << mode;
    }
}

TEST(JoinedWords, ALastLetterTheSegmentationCannotPlaceGivesBothReadingsStripped) {
    // A stray I behind a call after DE (73DEWN4ATI): WN4AT and WN4ATI, each stripped, the segmentation's choice (WN4ATI) last.
    const auto off = readings_with("73DEWN4ATI", 1);
    ASSERT_NE(reading_of(off, "WN4ATI"), nullptr);
    EXPECT_EQ(reading_of(off, "WN4ATI")->reading, Reading::Glued);
    EXPECT_EQ(reading_of(off, "WN4AT"), nullptr);
    const auto on = readings_with("73DEWN4ATI", 1, 1);
    ASSERT_EQ(on.size(), 2u);
    EXPECT_EQ(on[0].base, "WN4AT");
    EXPECT_EQ(on[1].base, "WN4ATI");
    for (const CallReading& x : on) {
        EXPECT_EQ(x.reading, Reading::Stripped);
        EXPECT_EQ(x.role, Role::Sender);
        EXPECT_NEAR(x.start_s, 0.4, 1e-9);
    }
    EXPECT_NEAR(on[0].end_s, 0.9, 1e-9);
    EXPECT_NEAR(on[1].end_s, 1.0, 1e-9);
    // A K after a call that is a call with it (K5NUDEAK4JK73): AK4JK and AK4J.
    const auto k = readings_with("K5NUDEAK4JK73", 1, 1);
    ASSERT_NE(reading_of(k, "AK4JK"), nullptr);
    ASSERT_NE(reading_of(k, "AK4J"), nullptr);
    EXPECT_EQ(reading_of(k, "AK4JK")->reading, Reading::Stripped);
    // EK8DX: only the copy before the K has an alternative; the others stay glued.
    const auto ek = readings_with("CQCQCQDXCQDXCQDXDEK8DXK8DXK8DXK", 1, 1);
    EXPECT_EQ(count_of(ek, "K8DX"), 3u);
    EXPECT_EQ(count_of(ek, "K8DXK"), 1u);
    EXPECT_EQ(std::count_if(ek.begin(), ek.end(), [](const CallReading& x) { return x.reading == Reading::Glued; }), 2);
    // A word with no such letter reads as without the option.
    EXPECT_EQ(readings_with("CQCQPOTADENA9M", 1, 1).size(), readings_with("CQCQPOTADENA9M", 1).size());
}

TEST(JoinedWords, TheStageChoosesBetweenTheTwoReadingsByItsEvidence) {
    const auto run = [](double alternatives) {
        CallSearchConfig config;
        config.segment_joined = 1.0;
        config.segment_alternatives = alternatives;
        Stage s(config);
        s.born(1, 1000.0);
        s.text(1, 1000.0, "73DEWN4ATI 73DEWN4ATI DE WN4AT K ", 0.0);
        s.search.advance(60.0);
        return std::make_pair(s.row("WN4AT") != nullptr, s.row("WN4ATI") != nullptr);
    };
    EXPECT_EQ(run(0.0), std::make_pair(false, true));  // the segmentation's choice, 2 points, holds WN4AT's 1
    EXPECT_EQ(run(1.0), std::make_pair(true, false));  // WN4AT 0.5 + 0.5 + 1 against WN4ATI 0.5 + 0.5
}

TEST(JoinedWords, AnAlternativeEmitRefusesLeavesTheCallGlued) {
    // N5AK less its K is N5A, of tiny pieces only, which no glued or stripped reading takes: N5AK stays one glued reading of 1
    // point, not a stripped one alone. Likewise AA5UK (AA5U).
    for (const auto& [text, call] : std::vector<std::pair<std::string, std::string>>{{"CQPOTADEN5AK", "N5AK"}, {"CQPOTADEAA5UK", "AA5UK"}}) {
        const auto r = readings_with(text, 1, 1);
        ASSERT_EQ(r.size(), 1u) << text;
        EXPECT_EQ(r[0].base, call) << text;
        EXPECT_EQ(r[0].reading, Reading::Glued) << text;
        EXPECT_EQ(r[0].role, Role::Sender) << text;
    }
}

TEST(JoinedWords, CopiesThatAreAPhaseOfPeriodicTextAreNoRepeat) {
    // A grid square sent three times (EL98EL98EL98) cuts as E L98E L98E L98; L98E twice is a phase of the period, not a call
    // repeated: widened by a character on each side the run still repeats every 4 characters. Likewise FN20FN20FN20 (N20F).
    EXPECT_EQ(cut("EL98EL98EL98").find('*'), std::string::npos);
    EXPECT_TRUE(readings_with("EL98EL98EL98", 1, 1).empty());
    EXPECT_EQ(cut("FN20FN20FN20").find('*'), std::string::npos);
    // A call that fills its word, or whose copies stand between other characters, is still a repeat.
    EXPECT_EQ(cut("AB4CDAB4CDAB4CD"), "AB4CD* AB4CD* AB4CD*");
    EXPECT_EQ(cut("DEK8DXK8DXK8DXK"), "DE K8DX* K8DX* K8DX* K");
    // A run that ends its word is not tested: KE2EH on 40 m (2026-11-17), the H before it a garbled copy's tail.
    EXPECT_NE(cut("GEHKE2EHKE2EH").find("KE2EH*"), std::string::npos);
    // The cost of that: a shifted phase that starts its word is still a call repeated (a known limitation, A.12).
    EXPECT_NE(cut("L98EL98EL98").find("L98E*"), std::string::npos);
}

TEST(JoinedWords, AGarbledMiddleCopyIsReadAsTheShorterCallRepeated) {
    // K1ABC with its middle copy garbled (K1ABCK1ABK1ABC) cuts as K1ABC K1AB K1AB C: the last copy's first four characters repeat the
    // garbled K1AB before them, so K1AB is twice side by side and K1ABC once, unframed. A known limitation (no repeat within one
    // edit): the stage reports K1AB, not K1ABC.
    EXPECT_EQ(cut("K1ABCK1ABK1ABC"), "K1ABC K1AB* K1AB* C");
    Stage s;
    s.born(1, 1000.0);
    s.text(1, 1000.0, "K1ABCK1ABK1ABC ", 0.0);
    s.search.advance(60.0);
    EXPECT_NE(s.row("K1AB"), nullptr);
    EXPECT_EQ(s.row("K1ABC"), nullptr);
}

// ---- Call forms (branch call-form-1)

TEST(CallForms, AnUnusualFormIsReportedFromAListOnly) {
    // Four letters after the digit (EE4EEEE, TE7TTTD, VK3FABC): CW Skimmer's unusual pattern, from a list only by default, as a
    // special form is; with unusual_listed_only 0 it is any call.
    const auto run = [](CallSearchConfig config, std::shared_ptr<const KnownCalls> known, const std::string& call) {
        Stage s(config, std::move(known));
        s.born(1, 1000.0);
        s.text(1, 1000.0, "CQ DE " + call + " " + call + " K ", 0.0);
        s.text(1, 1000.0, "CQ DE " + call + " " + call + " K ", 10.0);
        s.search.advance(60.0);
        return s.row(call) != nullptr;
    };
    EXPECT_EQ(CallSearchConfig{}.unusual_listed_only, 1.0);
    EXPECT_FALSE(run(CallSearchConfig{}, nullptr, "VK3FABC"));
    EXPECT_FALSE(run(CallSearchConfig{}, nullptr, "TE7TTTD"));
    EXPECT_TRUE(run(CallSearchConfig{}, nullptr, "VK3FAB"));
    auto known = std::make_shared<KnownCalls>();
    known->add("VK3FABC");
    EXPECT_TRUE(run(CallSearchConfig{}, known, "VK3FABC"));
    CallSearchConfig off;
    off.unusual_listed_only = 0.0;
    EXPECT_TRUE(run(off, nullptr, "VK3FABC"));
    // The readings carry the mark.
    const auto r = readings("DE VK3FABC K");
    const auto it = std::find_if(r.begin(), r.end(), [](const CallReading& x) { return x.base == "VK3FABC"; });
    ASSERT_NE(it, r.end());
    EXPECT_TRUE(it->unusual);
}

TEST(CallForms, JoinedWordsOfPeriodicTextGiveNoCall) {
    // L 9 6LL 9 6LL (20 m, 2026-11-17): joined, L96LL; its characters go on repeating every 4 (96LL), shorter than the call, over
    // more than two periods. A call sent twice keeps its own period and stays.
    const auto joined = [](const std::string& text, int periodic) {
        const std::vector<DecodedSymbol> s = symbols(text);
        const std::vector<Word> w = words_of(s);
        ReadOptions options;
        options.joined_periodic = periodic;
        return read_calls(w, 0, w.size(), s, options);
    };
    EXPECT_TRUE(has_base(joined("L 9 6LL 9 6LL 9 6LL", 0), "L96LL"));
    EXPECT_FALSE(has_base(joined("L 9 6LL 9 6LL 9 6LL", 1), "L96LL"));
    for (const auto& [text, call] : std::vector<std::pair<std::string, std::string>>{
             {"N 5 JJ N 5 JJ K", "N5JJ"}, {"DE K X 9 F K X 9 F K", "KX9F"}, {"WA 0 QZK", "WA0QZK"}, {"N 5 JJ DE K 1 ABC", "K1ABC"}}) {
        EXPECT_EQ(count_of(joined(text, 0), call), count_of(joined(text, 1), call)) << text;
        EXPECT_GE(count_of(joined(text, 1), call), 1u) << text;
    }
    // A phase of the call sent twice is periodic text: N5JJN (N 5 JJ N, then 5 JJ) goes, the call stays. (KX9FK from the last copy
    // and the K after it is not periodic text: the K is the ambiguous sign-off, for the evidence to decide.)
    EXPECT_TRUE(has_base(joined("N 5 JJ N 5 JJ K", 0), "N5JJN"));
    EXPECT_FALSE(has_base(joined("N 5 JJ N 5 JJ K", 1), "N5JJN"));
    EXPECT_EQ(CallSearchConfig{}.joined_periodic, 1.0);
}

TEST(CallForms, ARepeatAfterAKeywordThatMakesItALongerCallIsNoEvidence) {
    // TTW7QKW7Q (20 m, 2026-11-18, the CWT): W7Q K W7Q, or W7Q KW7Q with KW7Q's first K garbled to TT. KW7Q was the station (the
    // RBN spotted it); W7Q was never issued. Its second copy's repeat is no evidence, so W7Q is not believed; with
    // ambiguous_repeat 1 it is, as before.
    EXPECT_EQ(cut("TTW7QKW7Q").find('*'), std::string::npos);
    SegmentationCosts as_before;
    as_before.ambiguous_repeat = 1.0;
    bool believed = false;
    for (const Segment& s : segment_word("TTW7QKW7Q", as_before))
        believed = believed || (s.kind == Segment::Kind::Call && s.text == "W7Q" && s.believed);
    EXPECT_TRUE(believed);
    // A repeat with no keyword before it, or after one that makes no call with it, still counts.
    EXPECT_EQ(cut("DEK8DXK8DXK8DXK"), "DE K8DX* K8DX* K8DX* K");
    EXPECT_EQ(cut("W1AWW1AWK"), "W1AW* W1AW* K");
}

// ---- Arbitration (call-arb-1)

namespace {

std::vector<CallReading> readings_x(const std::string& text, int x_as_tu) {
    const std::vector<DecodedSymbol> s = symbols(text);
    const std::vector<Word> w = words_of(s);
    ReadOptions options;
    options.x_as_tu = x_as_tu;
    return read_calls(w, 0, w.size(), s, options);
}

}  // namespace

TEST(Arbitration, AnXInFrontIsAlsoReadAsTuRunOntoTheCall) {
    // T (-) and U (..-) with their letter space lost are X (-..-): TU W3RJ read as XW3RJ.
    const auto r = readings_x("XW3RJ CQ", 1);
    const CallReading* w3rj = reading_of(r, "W3RJ");
    ASSERT_NE(w3rj, nullptr);
    EXPECT_EQ(w3rj->reading, Reading::Stripped);
    EXPECT_EQ(w3rj->role, Role::Sender);
    EXPECT_TRUE(has_base(r, "XW3RJ"));  // both sides of the question stay
    EXPECT_FALSE(has_base(readings_x("XW3RJ CQ", 0), "W3RJ"));
    EXPECT_TRUE(has_base(readings_x("EXW3RJ", 1), "W3RJ"));  // a stray E, then the X
    // A real call that starts with X keeps its whole word's point against the stripped half point (XE2ABC: E2ABC at 0.5).
    const std::vector<CallReading> x = readings_x("XE2ABC", 1);
    const CallReading* e2abc = reading_of(x, "E2ABC");
    ASSERT_NE(e2abc, nullptr);
    EXPECT_EQ(e2abc->reading, Reading::Stripped);
}

// The 2026-11-18 20 m CWT recording's W3RJ (spotted by 116 RBN skimmers): reported at 17.7 s on CQ CWT W3RJ, then withdrawn at
// 2 150.4 s when its runs of QSOs, each ended TU W3RJ and decoded XW3RJ, gave the misreading more points in the window at its
// place (52.5 against 46.0); XW3RJ was listed instead. Read as TU and the call, each XW3RJ gives W3RJ half a point too.
TEST(Arbitration, ACallSignedAfterTuRunTogetherIsNotLostToTheMisreading) {
    const auto run = [](double x_as_tu) {
        CallSearchConfig config;
        config.x_as_tu = x_as_tu;
        auto s = std::make_unique<Stage>(config);
        s->born(1, 1000.0);
        for (int i = 0; i < 4; ++i) s->text(1, 1000.0, "CQ CWT W3RJ ", 10.0 * i);
        s->search.advance(60.0);
        EXPECT_NE(s->row("W3RJ"), nullptr);
        for (int i = 0; i < 5; ++i) s->text(1, 1000.0, "5NN XW3RJ CQ ", 100.0 + 10.0 * i);
        s->search.advance(200.0);
        return s;
    };
    const auto before = run(0.0);
    EXPECT_EQ(before->row("W3RJ"), nullptr);  // 4 points against 5
    EXPECT_NE(before->row("XW3RJ"), nullptr);
    const auto after = run(1.0);
    EXPECT_NE(after->row("W3RJ"), nullptr);  // 4 + 5 x 0.5 = 6.5 against 5
    EXPECT_EQ(after->row("XW3RJ"), nullptr);
    EXPECT_EQ(CallSearchConfig{}.x_as_tu, 1.0);  // the default since call-arb-1
}

TEST(Arbitration, AJoinedReadingCoversTheWholeWordItStartsWith) {
    const auto covers = [](const std::string& text, const std::string& base) {
        const std::vector<CallReading> r = readings(text);
        const CallReading* x = reading_of(r, base);
        return x ? x->covers : std::string("(no reading)");
    };
    EXPECT_EQ(covers("DE K4VR T", "K4VRT"), "K4VR");
    EXPECT_EQ(covers("DE K4VR T", "K4VR"), "");    // the whole word covers nothing
    EXPECT_EQ(covers("DE K 4VR T", "K4VRT"), "");  // its first word is no call
    EXPECT_EQ(covers("DE W4PM K", "W4PMK"), "");   // a sign-off
    EXPECT_EQ(covers("DE W4PM R", "W4PMR"), "");   // a roger
    EXPECT_EQ(covers("X W3RJ", "XW3RJ"), "");      // a letter before: the fragment must start the reading
}

// The 2026-11-17 40 m recording's K4VRT (RBN-confirmed), who sends DE K4VR T with a word space before the T: each such transmission
// gave K4VR a whole word's point and K4VRT a joined reading's 0.75, so the fragment beat the call it is part of.
TEST(Arbitration, ATransmissionAJoinedReadingCoversCountsForTheLongerCallWhenItHasPointsOfItsOwn) {
    const auto run = [](double covers) {
        CallSearchConfig config;
        config.joined_covers = covers;
        auto s = std::make_unique<Stage>(config);
        s->born(1, 1000.0);
        for (int i = 0; i < 4; ++i) s->text(1, 1000.0, "DE K4VR T K ", 10.0 * i);
        s->text(1, 1000.0, "DE K4VRT K ", 50.0);
        s->search.advance(100.0);
        return s;
    };
    const auto before = run(0.0);  // K4VR 4 + 0.5 (K4VRT stripped) against K4VRT 4 x 0.75 + 1
    EXPECT_NE(before->row("K4VR"), nullptr);
    EXPECT_EQ(before->row("K4VRT"), nullptr);
    const auto after = run(3.0);  // K4VR's four covered transmissions left out: 0.5 against 4, and K4VRT has 1 of its own
    EXPECT_NE(after->row("K4VRT"), nullptr);
    EXPECT_EQ(after->row("K4VR"), nullptr);
    EXPECT_NE(run(1.0)->row("K4VRT"), nullptr);
    EXPECT_EQ(CallSearchConfig{}.joined_covers, 3.0);  // the default since call-arb-1
}

// KC2LRC-2's W6SX (RBN-confirmed), read W6SX E twice: the E is a stray, and W6SXE, read only so, has no points of its own.
TEST(Arbitration, ALongerCallReadOnlyAsTheFragmentAndALetterTakesNothing) {
    const auto run = [](double covers) {
        CallSearchConfig config;
        config.joined_covers = covers;
        auto s = std::make_unique<Stage>(config);
        s->born(1, 1000.0);
        s->text(1, 1000.0, "QRZ W6SX E ", 0.0);
        s->text(1, 1000.0, "QRZ W6SX E ", 10.0);
        s->search.advance(60.0);
        return s;
    };
    EXPECT_NE(run(0.0)->row("W6SX"), nullptr);
    EXPECT_NE(run(3.0)->row("W6SX"), nullptr);
    EXPECT_EQ(run(3.0)->row("W6SXE"), nullptr);
    EXPECT_NE(run(2.0)->row("W6SX"), nullptr);  // for neither: neither has any points then, so both count as before
}