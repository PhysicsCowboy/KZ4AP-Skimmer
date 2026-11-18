// The callsign stage: from each track's decoded text to a list of calls, each reported once. It reads the engine's track and text
// events and nothing else, so it does not touch the decoders' search or the detector, and it costs a stream's pipeline no time when
// nothing subscribes it. docs/signal-processing.md, section 11, describes it; A.12 has the provenance of every value below.
//
// In order: (1) words of the settled text, (2) readings of a call in them (read_calls: the callsign grammar, words glued together
// or split apart, a stray E, T or I), each with the role its neighbors give it, (3) evidence per call and place, one occurrence per
// transmission however many tracks read it, (4) the report test and the arbitration between a call and its near variants.
#pragma once

#include "kz4ap/event_bus.hpp"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <istream>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
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
// A Maidenhead locator of six characters (FN31PR): two letters A to R, two digits, two letters A to X. Contest exchanges send them;
// such a word gives no reading of a call, whole, stripped or glued, though some parse as a special form (FN31PR) or, stripped, as
// a call (EL96UD, L96UD). A heuristic: a special-event call of that shape is lost.
bool is_locator(std::string_view word);

// A call made only of E, T, I, S, H, A, N, M, U and 5: what noise and a wide signal's fragments read as (track_evidence.hpp). The stage
// takes it only as a whole word and reports it at CallSearchConfig::tiny_points whole words, with or without a known-call list. A heuristic.
// A list helps nothing beyond that: a call with no digit after a letter-digit prefix (D4C, E7DX, C6AGU) has no form here and is
// never read, listed or not (docs/backlog.md).
bool only_tiny_pieces(std::string_view base);

// Levenshtein distance, stopping once it exceeds `limit` (it then returns limit + 1).
int edit_distance(std::string_view a, std::string_view b, int limit);

// ---- Reading a text

struct Word {
    std::string text;      // upper case, no spaces; a prosign is its own word, "<BT>"
    double start_s = 0;    // s, stream time
    double end_s = 0;      // s, stream time
    std::size_t first = 0;  // the index of its first symbol among those given to words_of
    std::size_t last = 0;   // and of the one after its last; a word's characters are these symbols, one each, in order
};

// The words of a list of symbols. A word space, a prosign and any punctuation but the slash end a word.
std::vector<Word> words_of(std::span<const DecodedSymbol> symbols);
// As words_of; with skip_heard, a symbol heard from a neighbor (DecodedSymbol::heard_from) ends a word and belongs to none.
std::vector<Word> words_of(std::span<const DecodedSymbol> symbols, bool skip_heard);
// As words_of; a symbol whose skip is not 0 ends a word and belongs to none (skip may be shorter than symbols: the rest are read).
std::vector<Word> words_of(std::span<const DecodedSymbol> symbols, std::span<const char> skip);

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
    bool doubled = false;  // the same word is right before or after it
    std::size_t first_word = 0;  // the words it came from: first, and the one after its last
    std::size_t last_word = 0;
    double start_s = 0;
    double end_s = 0;
    // nats: the least margin the decoder gave the symbols it came from, word spaces between joined words included, margin = ln(p / (1 - p))
    // of a symbol's probability p (the segmental decoder: SegmentalConfig::rival_margin_nats, at most that); kUnknownMargin when any of
    // them has probability 1, which a decoder that keeps no margins gives every symbol. CallSearch fills it.
    double margin_nats = kUnknownMargin;
    static constexpr double kUnknownMargin = -1.0;
};

// nats: a symbol's margin from its probability, ln(p / (1 - p)), at least 0; CallReading::kUnknownMargin for p = 1.
double margin_of(float probability);

// Every reading of a call that begins in the words [from, to). A word can give several: they are alternatives, and the evidence
// decides among them. `words` may reach past `to` and before `from`, for context.
std::vector<CallReading> read_calls(std::span<const Word> words, std::size_t from, std::size_t to);
// As read_calls; given the symbols the words were made of (words_of), each piece of a glued word has its own characters' times
// (CallSearchConfig::glued_pieces_timed); with an empty span, the word's.
std::vector<CallReading> read_calls(std::span<const Word> words, std::size_t from, std::size_t to, std::span<const DecodedSymbol> symbols);

// ---- A known-call list

// A set of calls, by base call. MASTER.SCP's format: one call a line; a line starting with # or ; or blank is skipped; case is ignored;
// anything after the first white space on a line is ignored.
class KnownCalls {
public:
    KnownCalls() = default;
    // Throws std::runtime_error for a text with a NUL byte (a binary file, such as CW Skimmer's MASTER.DTA) or with no line whose first
    // word has a call's form: such a list matches nothing, and with named_listed_only it would refuse every call named once, silently.
    static KnownCalls read(std::istream& in);
    // Throws std::runtime_error when the file cannot be read, or as read does, naming the file.
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
    // measurement (appendix A.12); 99% of corrections on real 40 m reach within 36 s, and those past this are read again (reread_corrected).
    double settle_s = 10.0;
    // Hz: the readings of a call count together within this of each other. Heuristic, chosen by measurement: manta's bucket of 300 Hz; 100 Hz
    // loses a QSO's partner, named at one station's place and signing at his own 200 Hz away.
    double same_place_hz = 300.0;
    // s: the readings of a call count together within this of each other. Heuristic, chosen by measurement: CW Skimmer's 10 minutes between
    // spots at a place lost an ID sent every 255 s on real 40 m; 30 min added no misreading there.
    double window_s = 1800.0;
    // Hz: a near variant of a better-supported call within this is taken as its misreading. Heuristic, chosen by measurement.
    double variant_hz = 150.0;
    // s: and within this of it. Heuristic, as window_s.
    double variant_s = 1800.0;
    // Points needed to report: an unlisted call that is framed (DE, CQ, 5NN beside it, or sent twice), an unlisted bare one, a listed framed
    // one, a listed bare one. A whole word scores 1, a stripped reading stripped_points, a joined one joined_points. Heuristics, chosen by measurement; CW
    // Skimmer's Normal level is 2, 3 or 5 repeats by pattern, 1 for its watch list. An unlisted bare call and a listed one need the same; the list still weighs against rivals.
    double points_framed = 2.0;
    double points_bare = 2.0;
    double points_known = 1.0;
    double points_known_bare = 2.0;
    double stripped_points = 0.5;
    // Letters joined across spaces (K F 0 Y U X). Heuristic, chosen by measurement: at 0.5 a call read so three times stayed at 1.5 points on
    // real 40 m (KF0YUX); at 1 three false calls came there.
    double joined_points = 0.75;
    // A whole or glued word another station names (before DE, 5NN, UR and the like) passes at this many points. 2, the default since rob-1,
    // is no exception: a named word counts as any other, so a call heard on one word is not reported without a list, CW Skimmer's practice
    // (RBN 2021). At 1 (call-1 to rob-1) one named word passed alone: 11 more true and 4 more false calls on the contest of 280, but on the
    // four 2017 recordings 18 confirmed calls for 110 the RBN did not confirm (appendix A.12). Standard practice (CW Skimmer's Normal level,
    // secondhand through the RBN's 2021 post), checked by measurement.
    double points_named = 2.0;
    // Whole words a call of tiny pieces (only_tiny_pieces) needs, listed or not; 0 leaves it unread. Heuristic, chosen by measurement.
    double tiny_points = 2.0;
    // Two letters' difference holds a call back only against a rival with more than this many times its points. Heuristic, chosen by measurement.
    double far_variant_ratio = 2.0;
    // A call that is the start of another, or starts with it, whatever their distance (K5N and K5NXY, KX9F and KX9FK), is a near variant at
    // distance 1, when not 0. Heuristic, chosen by measurement.
    double prefix_held = 1.0;
    // When not 0, a reported call that loses at one place is withdrawn only if its rival's best is above its own best, or its readings
    // more than variant_hz from that place do not pass by themselves; at 0, any local defeat withdraws it. Heuristic, chosen by a hand test and measurement.
    // Read only with pool_places 0.
    double withdraw_own_place = 1.0;
    // A call sent twice in a row counts as framed, when not 0. Heuristic, chosen by measurement.
    double doubled_is_framed = 1.0;
    // A rival must have this many times the points of a listed call to hold it back; a listed rival holds back an unlisted call at equal points.
    double known_rival_factor = 3.0;
    // A correction that changes text already read has it read again, when not 0: the new reading adds, and what the old text gave stays, since
    // neither text is always the better (the decoder's streams trade places). Heuristic, chosen by measurement.
    double reread_corrected = 1.0;
    // s: a track's text is kept this far back for that. Heuristic: corrections reached 165 to 206 s on real 40 m (appendix A.12).
    double history_s = 200.0;
    // With a known-call list, a word named by another passes alone (points_named) only for a listed call, when not 0: CW Skimmer's single
    // repetition is for its lists. Heuristic, chosen by measurement (appendix A.12): on the two 2017 recordings tuned on, with CW
    // Skimmer's MASTER.DTA, 13 misreadings fewer for 3 confirmed calls.
    double named_listed_only = 1.0;
    // A call of one letter after its digit (K1T) is held back by a call two edits away with the same prefix and digit (K1BX) and more
    // than this many times its points within variant_hz; 0 leaves such pairs to the rule of edit distance 1 (where K1T and K1BX are no
    // pair). Heuristic, chosen by measurement: the four fragments of the two 2017 recordings tuned on go, nothing else changes.
    double short_variant_ratio = 2.0;
    // nats: a listed call read bare passes at points_known, as if framed, when some whole or glued reading of it has at least this margin
    // (CallReading::margin_nats): a single reading scored by the decoder's confidence in it; 0, the default, asks points_known_bare of every
    // bare listed call. Off since review 10: with the segmental decoder's margins at 16 nats and the 2018 list, 8 nats added 6 confirmed calls
    // and 5 misreadings on the two 2017 recordings tuned on, but 6 and 9 on the two held out (decision log). A margin reaches at most 15.94
    // nats there (the ceiling read through a float), so 16 nats is the rule off.
    double known_margin_nats = 0.0;
    // Readings of a call at different places add, when not 0: one call is one station, and a station that calls several runners, or
    // that is named at one place and signs at another, is read independently at each (CW Skimmer counts a call's repeats per decoder, so
    // this goes past it). Occurrences that overlap in time count once wherever they are (one transmission), and an occurrence at a place
    // where a better-supported variant holds the call back counts for that variant, not for it. The same pooled support decides whether a
    // reported call that loses at one place leaves the list, and is reported, on these pooled points alone (in place of withdraw_own_place's
    // test). Heuristic, motivated by the evidence rule (independent readings add, one transmission counts once): readings at different places
    // are not independent when the decoder misreads a call the same way at each (real30's K0HBN for IK0HBN), and the whole band and window_s
    // as its scope are choices. On by default, chosen by measurement (rob-1; appendix A.12).
    double pool_places = 1.0;
    // The pieces of a glued word take their own characters' times, when not 0, so that a call sent twice with no space between
    // (K1ABCK1ABC) is two readings, as it is with the space; at 0 every piece has the word's times and the two are one occurrence.
    // Derived from the evidence rule (one occurrence per transmission); rob-1.
    double glued_pieces_timed = 1.0;
    // A stretch of symbols the engine attributed to neighbors (DecodedSymbol::heard_from: their keying; consecutive, no word space) is
    // skipped when not 0, as a word break, where it is whole words and each neighbor's own track holds every symbol of it: the same
    // text, start and end each within neighbor_twin_s, s. A variant, off: on the five real recordings it gained and lost no call, but
    // changed 12 calls' fields (9 frequencies, 5 framings, 2 point totals: the readings left lie elsewhere, among other neighbors), while the stage already
    // counts readings that overlap in time within same_place_hz once (copy-2, review 18; appendix A.12). 25 ms is the duplicate rule's.
    double skip_neighbors_text = 0.0;
    double neighbor_twin_s = 0.025;
};

// One line of the list.
struct CallRow {
    std::string call;         // what to show: the base call, or its most common form with a slash, seen at least twice
    std::string base;
    double freq_hz = 0;       // Hz from the span's center, at the latest reading where it was not named by another
    double first_s = 0;       // s, stream time of its first reading
    double reported_s = 0;    // s, stream time of the end of the reading that reported it; the list shows it at least settle_s later
    double last_s = 0;        // s, stream time of its latest reading
    std::uint32_t count = 0;  // readings, one per transmission
    double points = 0;        // the best support it has had at one place
    bool known = false;       // on the known-call list
    bool heard = false;       // some reading was not named by another: the station itself was heard
    // Some reading had the role Sender (signed): after DE, CQ, TEST and the like, or before K, KN, AR and the like (Role). Unlike an
    // occurrence's framing, a word named by another or sent twice does not set it. Every reading counts, at any place and time, held by
    // a variant or not, so that it never goes back to false. The CSV's column `framed`; is_signed reads it.
    bool framed = false;
};

// Whether the call signed: some reading of it had the role Sender (CallRow::framed). The list's option "signed calls only" shows these
// rows alone; by default every row is shown, signed, bare or named, as CW Skimmer lists every call (the owner's ruling, 2026-11-18).
inline bool is_signed(const CallRow& row) { return row.framed; }

class CallSearch {
public:
    explicit CallSearch(CallSearchConfig config = {}, std::shared_ptr<const KnownCalls> known = nullptr);

    // A new known-call list, or none, from now on: every call held is marked listed or not by it (its rows too), and is judged so at its
    // next reading. What has been reported stays until a reading takes it back; a special form refused before is not read again.
    void set_known(std::shared_ptr<const KnownCalls> known);
    const std::shared_ptr<const KnownCalls>& known() const { return known_; }

    void apply(const TrackEvent& event);
    void apply(const DecodedTextEvent& event);
    // The stream's clock, s: reads what has settled.
    void advance(double now_s);
    void apply(const StreamClock& clock) { advance(clock.time_s); }
    // The stream ended: reads everything.
    void finish();

    // The calls reported so far, in the order they were reported. A call that lost to a variant has left.
    const std::vector<CallRow>& calls() const { return rows_; }
    // Changes when rows are added or removed; otherwise only their values do.
    std::uint64_t version() const { return version_; }
    // Reported, then taken back for a better-supported variant.
    std::uint64_t withdrawn() const { return withdrawn_; }
    // Called for every reading, with its track's frequency, Hz (for looking into a run: kz4ap-calls --readings).
    void set_reading_log(std::function<void(const CallReading&, double)> log) { reading_log_ = std::move(log); }
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
        double margin = 0;  // nats: the best of the margins of the readings it holds that are whole or glued words (CallReading::margin_nats)
    };
    struct Candidate {
        std::string base;
        std::uint64_t made = 0;  // the order of its first reading
        bool known = false;
        bool reported = false;
        std::deque<Occurrence> recent;       // within window_s of the latest
        std::map<std::string, std::uint32_t> forms;  // base, or with a slash: how often
        std::uint32_t count = 0;
        double first_s = 0;
        double last_s = 0;
        double reported_s = 0;
        double freq_hz = 0;
        int freq_rank = -1;  // how good the place's reading is (CallSearch::add)
        double best_points = 0;
        bool heard = false;
        bool framed = false;
    };
    struct TrackText {
        std::vector<DecodedSymbol> symbols;  // kept: from the context before what has been read
        std::size_t base = 0;                // the index, in the track's whole list, of symbols[0]
        std::size_t read_to = 0;             // the whole list's index up to which words have been read
        std::size_t rewind_to = static_cast<std::size_t>(-1);  // the whole list's index of the first symbol changed behind read_to, if any
        double freq_hz = 0;
    };
    struct Support {
        double points = 0;
        bool framed = false;
        bool named = false;  // a whole or glued word named by another (CallSearchConfig::points_named)
        bool sure = false;   // a whole or glued word the decoder had no near rival for (CallSearchConfig::known_margin_nats)
    };

    void read_track(TrackText& track, bool all);
    void add(const CallReading& reading, double freq_hz);
    void evaluate(Candidate& candidate, double freq_hz, double now_s);
    Support support(const Candidate& candidate, double freq_hz, double now_s, double within_hz) const;
    bool passes(const Candidate& candidate, const Support& s) const;
    // Whether a better-supported near variant holds the call back at this place (CallSearchConfig::variant_hz); the best such rival's best
    // is added to rival_best, and the reported calls it beats here to beaten, when given.
    bool held_at(const Candidate& candidate, double freq_hz, double now_s, double* rival_best, std::vector<Candidate*>* beaten);
    // The call's support over the whole band (CallSearchConfig::pool_places): its occurrences within window_s, one per transmission,
    // leaving out those at places where it is held.
    Support pooled_support(const Candidate& candidate, double now_s);
    // Whether the call's readings more than variant_hz from this place pass by themselves, at any one place.
    bool stands_elsewhere(const Candidate& candidate, double freq_hz, double now_s) const;
    void report(Candidate& candidate, double now_s);
    void withdraw(Candidate& candidate);
    void update_row(const Candidate& candidate);
    void prune(double now_s);

    CallSearchConfig config_;
    std::shared_ptr<const KnownCalls> known_;
    std::function<void(const CallReading&, double)> reading_log_;
    std::map<std::uint32_t, TrackText> tracks_;  // in the order of their ids, so that the order of reading does not depend on the library
    std::unordered_map<std::string, Candidate> candidates_;
    std::vector<CallRow> rows_;
    std::uint64_t version_ = 0;
    std::uint64_t withdrawn_ = 0;
    std::uint64_t made_ = 0;
    double now_s_ = 0;
    double last_read_s_ = 0;
    double last_prune_s_ = 0;
};

// The list as the scoring tool (training/kz4ap_proto/calls_score.py) reads it: CSV, a header line, one row per call. center_hz, when
// the radio's frequency is known, adds the absolute frequency; start_unix_ms, the stream's first sample in ms since 1970 UTC when
// known, adds first_utc and last_utc (ISO 8601); the columns stay, empty, when they are not.
std::string calls_csv(const std::vector<CallRow>& rows, std::optional<double> center_hz = std::nullopt,
                      std::optional<std::int64_t> start_unix_ms = std::nullopt);

}  // namespace kz4ap::callsign
