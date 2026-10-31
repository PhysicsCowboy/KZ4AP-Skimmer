#include "kz4ap/callsign.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <sstream>
#include <string>
#include <vector>

using namespace kz4ap;
using namespace kz4ap::callsign;

namespace {

bool is_call(const std::string& word) { return parse_call(word).has_value(); }

// A list of symbols for a text at 0.1 s a character, from t_s on; a space is a word space; <XX> is one symbol.
std::vector<DecodedSymbol> symbols(const std::string& text, double t_s = 0.0) {
    std::vector<DecodedSymbol> out;
    for (std::size_t i = 0; i < text.size(); ++i) {
        std::string s(1, text[i]);
        if (text[i] == '<') {
            const std::size_t close = text.find('>', i);
            s = text.substr(i, close - i + 1);
            i = close;
        }
        out.push_back({s, 1.0f, t_s, t_s + 0.1});
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
    void text(std::uint32_t id, double freq_hz, const std::string& t, double t_s) {
        DecodedTextEvent e{};
        e.track_id = id;
        e.freq_hz = freq_hz;
        e.chars = symbols(t, t_s);
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

TEST(CallGrammar, WordsOfCwWithACallsFormAreRefused) {
    for (const char* word : {"G5RV", "K3S", "N1MM", "R5NN", "UR5NN", "TU5NN", "K5NN"}) {
        EXPECT_TRUE(parse_form(word).has_value()) << word;
        EXPECT_FALSE(is_call(word)) << word;
    }
    EXPECT_TRUE(is_call("DJ5NN"));  // exact matches only: manta's lesson
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

TEST(Readings, TinyPiecesMakeNoCall) {
    EXPECT_TRUE(only_tiny_pieces("I5E"));
    EXPECT_FALSE(only_tiny_pieces("K1ABC"));
    EXPECT_FALSE(has_base(readings("I 5 E H E"), "I5EHE"));
    EXPECT_FALSE(has_base(readings("EE5E"), "EE5E"));
}

TEST(KnownCallList, ReadsScpFormat) {
    std::istringstream in("# a comment\n; another\n\nk1abc\nW2XY  anything\nK1ABC/P\nnot-a-call\n");
    const KnownCalls known = KnownCalls::read(in);
    EXPECT_TRUE(known.contains("K1ABC"));
    EXPECT_TRUE(known.contains("W2XY"));
    EXPECT_FALSE(known.contains("N5JJ"));
    EXPECT_EQ(known.size(), 3u);
    EXPECT_THROW(KnownCalls::read_file("no such file.scp"), std::runtime_error);
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

TEST(CallSearch, BareNeedsThreeAndAFarPlaceDoesNotAdd) {
    Stage s;
    s.born(1, 1000.0);
    s.born(2, 9000.0);
    s.text(1, 1000.0, "E K1ABC T ", 0.0);
    s.text(1, 1000.0, "E K1ABC T ", 40.0);
    s.text(2, 9000.0, "E K1ABC T ", 80.0);
    s.search.advance(120.0);
    EXPECT_EQ(s.row("K1ABC"), nullptr);
    s.text(1, 1000.0, "E K1ABC T ", 130.0);
    s.search.advance(170.0);
    EXPECT_NE(s.row("K1ABC"), nullptr);
}

TEST(CallSearch, ReadingsTooFarApartInTimeDoNotAdd) {
    CallSearchConfig config;
    config.window_s = 100.0;
    Stage s(config);
    s.born(1, 1000.0);
    s.text(1, 1000.0, "DE K1ABC ", 0.0);
    s.text(1, 1000.0, "DE K1ABC ", 500.0);
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
}
