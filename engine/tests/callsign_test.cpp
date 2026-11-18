#include "kz4ap/callsign.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <sstream>
#include <string>
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
    // More than two edits apart (K1T and K1BXYZ, four) is no pair.
    EXPECT_TRUE(run(CallSearchConfig{}, "K1BXYZ", 7));
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

TEST(CallSearch, ACallWithNoDigitAfterALetterDigitPrefixIsNotReadEvenListed) {
    // A limit, stated in callsign.hpp and appendix A.12: these have no form in the grammar.
    auto known = std::make_shared<KnownCalls>();
    for (const char* call : {"D4C", "E7DX", "C6AGU"}) known->add(call);
    Stage s(CallSearchConfig{}, known);
    s.born(1, 1000.0);
    s.text(1, 1000.0, "DE D4C D4C K DE E7DX E7DX K DE C6AGU C6AGU K ", 0.0);
    s.search.advance(30.0);
    EXPECT_TRUE(s.search.calls().empty());
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