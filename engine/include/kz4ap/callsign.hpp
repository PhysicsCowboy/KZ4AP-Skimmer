// The callsign stage: from each track's decoded text to a list of calls, each reported once. It reads the engine's track and text
// events and nothing else, so it does not touch the decoders' search or the detector, and it costs a stream's pipeline no time when
// nothing subscribes it. docs/signal-processing.md, section 11, describes it; A.11b has the provenance of every value below.
//
// In order: (1) words of the settled text, (2) readings of a call in them (read_calls: the callsign grammar, words glued together
// or split apart, a stray E, T or I), each with the role its neighbors give it, (3) evidence per call and place, one occurrence per
// transmission however many tracks read it, (4) the report test and the arbitration between a call and its near variants.
#pragma once

#include "kz4ap/event_bus.hpp"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <istream>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace kz4ap::callsign {

// ---- The grammar

// Whether the first two characters of a call may begin a call: the ITU's table of series (Radio Regulations, Appendix 42), Q and
// the pairs with 0 or 1 unavailable, and three pairs amateurs use outside it (S0, Z6, 1A). Upper case.
bool allocated_pair(char first, char second);

struct ParsedCall {
    std::string base;     // the station's own call: K1ABC for K1ABC/P, DL/K1ABC and K1ABC/DL
    std::string full;     // as read, with what is around the slash
    bool special = false;  // two or more digits after the prefix (W100AW, TM100TF): a special-event form, spotted from a list only
};

// A call, or nothing. The form is the ITU's for amateur stations (Radio Regulations 19.68): an allocated prefix of one or two
// characters (three for a digit first: 3DA0, 3XY1), one digit, then one to four letters. A slash adds /P, /M, /MM, /AM, /QRP, a digit
// or a prefix on either side. Words of CW that have this form are refused (is_cw_word). Upper case.
std::optional<ParsedCall> parse_call(std::string_view word);
// The form alone, before the refusal of words of CW.
std::optional<ParsedCall> parse_form(std::string_view word);
// A word with a call's form that in CW text is something else: rigs, antennas, software, and a word glued to 5NN (R5NN, UR5NN). Exact
// matches only, since real calls contain them: DJ5NN stays a call. A heuristic list (manta's practice of exact matches).
bool is_cw_word(std::string_view base);

// Levenshtein distance, stopping once it exceeds `limit` (it then returns limit + 1).
int edit_distance(std::string_view a, std::string_view b, int limit);

// ---- Reading a text

struct Word {
    std::string text;      // upper case, no spaces; a prosign is its own word, "<BT>"
    double start_s = 0;    // s, stream time
    double end_s = 0;      // s, stream time
    std::size_t first = 0;  // the index of its first symbol among those given to words_of
    std::size_t last = 0;   // and of the one after its last
};

// The words of a list of symbols. A word space, a prosign and any punctuation but the slash end a word.
std::vector<Word> words_of(std::span<const DecodedSymbol> symbols);

// How a call came out of the words.
enum class Reading {
    Whole,     // a word by itself
    Glued,     // a word with a keyword or the call again glued to it: DEK1ABC, N5JJDEWA0QZK, K1ABCK1ABC
    Stripped,  // a word with a stray E, T or I in front or an E, T or K behind: EIK0HBN
    Joined,    // several short words: N 5 JJ, K X 9 F
};

// What the words around it say of who sent it.
enum class Role {
    Unknown,  // no context: a bare call
    Sender,   // DE call, CQ ... call, TEST call, call K, TU call TEST: the station's own
    Named,    // call DE, call 5NN, call UR: another station's, sent by whoever holds this track
};

struct CallReading {
    std::string base;
    std::string full;
    bool special = false;
    Reading reading = Reading::Whole;
    Role role = Role::Unknown;
    std::size_t first_word = 0;  // the words it came from: first, and the one after its last
    std::size_t last_word = 0;
    double start_s = 0;
    double end_s = 0;
};

// Every reading of a call that begins in the words [from, to). A word can give several: they are alternatives, and the evidence
// decides among them. `words` may reach past `to` and before `from`, for context.
std::vector<CallReading> read_calls(std::span<const Word> words, std::size_t from, std::size_t to);

// ---- A known-call list

// A set of calls, by base call. MASTER.SCP's format: one call a line; a line starting with # or ; or blank is skipped; case is ignored;
// anything after the first white space on a line is ignored.
class KnownCalls {
public:
    KnownCalls() = default;
    static KnownCalls read(std::istream& in);
    // Throws std::runtime_error when the file cannot be read.
    static KnownCalls read_file(const std::string& path);
    void add(std::string_view call);
    bool contains(std::string_view base) const { return calls_.count(std::string(base)) != 0; }
    std::size_t size() const { return calls_.size(); }

private:
    std::unordered_set<std::string> calls_;
};

// ---- The stage

struct CallSearchConfig {
    // s: the text is read this far behind the stream, so that the decoder's corrections have mostly settled. Heuristic, chosen by
    // measurement (section A.11b); corrections reach at most 20 s.
    double settle_s = 6.0;
    // Hz: the readings of a call count together within this of each other. Heuristic: manta's 93.75 Hz within a bucket of 300 Hz.
    double same_place_hz = 100.0;
    // s: the readings of a call count together within this of each other. Heuristic: CW Skimmer's 10 minutes between spots at a place.
    double window_s = 600.0;
    // Hz: a near variant of a better-supported call within this is taken as its misreading. Heuristic, chosen by measurement.
    double variant_hz = 500.0;
    // s: and within this of it. Heuristic, as window_s.
    double variant_s = 600.0;
    // Points needed to report, an unlisted call that is framed (DE, CQ, ...), an unlisted bare one, a listed one. A whole word scores 1,
    // a stripped or joined reading stripped_points. Heuristics; CW Skimmer's Normal level is 2, 3 or 5 repeats by pattern, 1 for its
    // watch list.
    double points_framed = 2.0;
    double points_bare = 3.0;
    double points_known = 1.0;
    double stripped_points = 0.5;
    // A variant's points that also count for the better call. Heuristic.
    double variant_share = 0.5;
    // A rival must have this many times the points of a listed call to hold it back; a listed rival holds back an unlisted call at equal points.
    double known_rival_factor = 3.0;
};

// One line of the list.
struct CallRow {
    std::string call;         // what to show: the base call, or its most common form with a slash, seen at least twice
    std::string base;
    double freq_hz = 0;       // Hz from the span's center, at the latest reading where it was not named by another
    double first_s = 0;       // s, stream time of its first reading
    double reported_s = 0;    // s, stream time it was reported
    double last_s = 0;        // s, stream time of its latest reading
    std::uint32_t count = 0;  // readings, one per transmission
    double points = 0;        // the best support it has had at one place
    bool known = false;       // on the known-call list
    bool heard = false;       // some reading was not named by another: the station itself was heard
    bool framed = false;      // some reading was framed: it signed with DE, called CQ or the like
};

class CallSearch {
public:
    explicit CallSearch(CallSearchConfig config = {}, std::shared_ptr<const KnownCalls> known = nullptr);

    void apply(const TrackEvent& event);
    void apply(const DecodedTextEvent& event);
    // The stream's clock, s: reads what has settled.
    void advance(double now_s);
    // The stream ended: reads everything.
    void finish();

    // The calls reported so far, in the order they were reported. A call that lost to a variant has left.
    const std::vector<CallRow>& calls() const { return rows_; }
    // Changes when rows are added or removed; otherwise only their values do.
    std::uint64_t version() const { return version_; }
    // Reported, then taken back for a better-supported variant.
    std::uint64_t withdrawn() const { return withdrawn_; }
    // Every candidate still held, reported or not, as rows (for looking into a run: kz4ap-calls --candidates).
    std::vector<CallRow> candidates() const;

private:
    struct Occurrence {
        double start_s = 0;
        double end_s = 0;
        double freq_hz = 0;
        double points = 0;
        Role role = Role::Unknown;
        bool framed = false;
    };
    struct Candidate {
        std::string base;
        bool special = false;
        bool known = false;
        bool reported = false;
        std::deque<Occurrence> recent;       // within window_s of the latest
        std::map<std::string, std::uint32_t> forms;  // base, or with a slash: how often
        std::uint32_t count = 0;
        double first_s = 0;
        double last_s = 0;
        double reported_s = 0;
        double freq_hz = 0;
        double best_points = 0;
        bool heard = false;
        bool framed = false;
    };
    struct TrackText {
        std::vector<DecodedSymbol> symbols;  // kept: from the context before what has been read
        std::size_t base = 0;                // the index, in the track's whole list, of symbols[0]
        std::size_t read_to = 0;             // the whole list's index up to which words have been read
        double freq_hz = 0;
        std::size_t correction_base = 0;
    };
    struct Support {
        double points = 0;
        bool framed = false;
    };

    void read_track(TrackText& track, bool all);
    void add(const CallReading& reading, double freq_hz);
    void evaluate(Candidate& candidate, double freq_hz, double now_s);
    Support support(const Candidate& candidate, double freq_hz, double now_s, double within_hz) const;
    bool passes(const Candidate& candidate, const Support& s, double variant_points) const;
    void report(Candidate& candidate, double now_s);
    void withdraw(Candidate& candidate);
    void update_row(const Candidate& candidate);
    void prune(double now_s);

    CallSearchConfig config_;
    std::shared_ptr<const KnownCalls> known_;
    std::unordered_map<std::uint32_t, TrackText> tracks_;
    std::unordered_map<std::string, Candidate> candidates_;
    std::vector<CallRow> rows_;
    std::uint64_t version_ = 0;
    std::uint64_t withdrawn_ = 0;
    double now_s_ = 0;
    double last_read_s_ = 0;
    double last_prune_s_ = 0;
};

// The list as the scoring tool (training/kz4ap_proto/calls_score.py) reads it: CSV, a header line, one row per call. center_hz, when
// the radio's frequency is known, adds the absolute frequency.
std::string calls_csv(const std::vector<CallRow>& rows, std::optional<double> center_hz = std::nullopt);

}  // namespace kz4ap::callsign
