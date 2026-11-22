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
    // Two or more digits after the prefix (W100AW, TM100TF): a special-event form; unlisted, it needs its own bar
    // (CallSearchConfig::special_form).
    bool special = false;
    // Four letters after its one digit (VK3FABC, 8N3ACAF): a form RR 19.68 allows but few administrations issue, CW Skimmer's
    // "unusual pattern"; unlisted, it needs its own bar (CallSearchConfig::unusual_form).
    bool unusual = false;
    // No digit but the prefix's (C6AGU, E7DX, J6M, P3X): a letter-digit prefix whose digit is the call's, as administrations issue
    // outside the letter of RR 19.68; unlisted, it needs its own bar (CallSearchConfig::prefix_digit_form).
    bool prefix_digit = false;
    // A word of CW that has a call's form (is_cw_word: N1MM, K9AY, R5NN, DJ5NN); unlisted, it needs its own bar
    // (CallSearchConfig::cw_word_form).
    bool cw_word = false;
    // Around a slash, the other side when it too could be the base (K1A/W1AW: W1AW): the stage takes it when it alone is listed.
    std::string other_base;
};

// A call, or nothing. The form is the ITU's for amateur stations (Radio Regulations 19.68): an allocated prefix of one or two
// characters, or three in a half series (19.68.1: 3DA0RU, three letters at most after its digit), one digit, then one to four
// letters; or a prefix of a letter and a digit (outside the whole-letter series) directly followed by one to four letters, the
// prefix's digit being the call's (prefix_digit). A call of the United States' series (K, N, W, AA to AL) has the FCC's forms: one
// to three letters after the digit, two at most after AA to AL and NA to NZ (the FEMA stations AF#EMA and NF#EMA excepted), and a
// 1x1 never ends in X. A slash adds /P, /M, /MM, /AM, /QRP, a digit or a prefix on either side; when both sides can be the base
// (W1AW/VP2V, K1A/W1AW), the side that is not a location form (VP2V, VK9X) when exactly one is, else the left. Words of CW that have this form are marked
// (is_cw_word), not refused. Upper case. docs/signal-processing.md, A.12, "Form", has the sources.
std::optional<ParsedCall> parse_call(std::string_view word);
// The form alone, without the mark of words of CW.
std::optional<ParsedCall> parse_form(std::string_view word);
// A word with a call's form that in CW text is mostly something else: rigs, antennas, software (N1MM, K9AY, G5RV), exact; and any
// one or two letters glued to 5NN (R5NN, TU5NN, DJ5NN, W5NN), the contest report run onto a word. Real calls among them (K9AY,
// N1MM, K5NN, DJ5NN, W5NN are issued) need more readings when unlisted (CallSearchConfig::cw_word_form). A heuristic (manta's
// practice of exact matches).
bool is_cw_word(std::string_view base);
// A Maidenhead locator of six characters (FN31PR): two letters A to R, two digits, two letters A to X. Contest exchanges send them;
// such a word gives no reading of a call, whole, stripped or glued, though some parse as a special form (FN31PR) or, stripped, as
// a call (EL96UD, L96UD). A heuristic: a special-event call of that shape is lost.
bool is_locator(std::string_view word);

// A call made only of E, T, I, S, H, A, N, M, U and 5: what noise and a wide signal's fragments read as (track_evidence.hpp). The stage
// takes it only as a whole word and reports it at CallSearchConfig::tiny_points whole words, with or without a known-call list. A heuristic.
bool only_tiny_pieces(std::string_view base);

// A base call of three characters, one letter, a digit, one letter (N4A, W6S, G2M): the 1x1 form (CallSearchConfig::short_points).
bool is_short_call(std::string_view base);
// A base call whose prefix is a digit 2 to 9 and A, N or T, then its digit (2T7A, 3N4D, 5T7R), outside 9A, 7N and 8N (Croatia's and
// Japan's series, which most real calls of this shape are): the shape of a number sent in cut digits (CallSearchConfig::cut_number_form).
// Not 1 or 0 first: a number's 1 and 0 are themselves sent cut, as A and T, so a cut number never starts with either digit.
bool is_cut_number_form(std::string_view base);

// Levenshtein distance, stopping once it exceeds `limit` (it then returns limit + 1).
int edit_distance(std::string_view a, std::string_view b, int limit);

// ---- Reading a text

struct Word {
    std::string text;      // upper case, no spaces; a prosign is its own word, "<BT>"; <BK> is the word BK (words_of)
    double start_s = 0;    // s, stream time
    double end_s = 0;      // s, stream time
    std::size_t first = 0;  // the index of its first symbol among those given to words_of
    std::size_t last = 0;   // and of the one after its last; a word's characters are these symbols, one each, in order
};

// The words of a list of symbols. A word space, a prosign and any punctuation but the slash end a word. A prosign is its own word,
// but <BK> (-...-.-, B and K run together) is the word BK, the abbreviation, as if its letters had been sent apart: the owner's
// reading (2026-11-21, "I don't really consider bk a prosign, it is an abbreviation").
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
    bool unusual = false;  // ParsedCall::unusual
    bool prefix_digit = false;  // ParsedCall::prefix_digit
    bool cw_word = false;  // ParsedCall::cw_word
    std::string other_base;  // ParsedCall::other_base
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
    // A joined reading whose first word is a call by itself (K4VR T: K4VRT, its first word K4VR): that call's base, the fragment the
    // same transmission also gives as a whole word (CallSearchConfig::joined_covers); empty otherwise, and when the words after it
    // are only K and R, sign-offs and rogers far more often than a call's last letters.
    std::string covers;
    // A stripped reading's characters taken off the front (EIK0HBN: EI; XW3RJ: X); empty for any other reading.
    std::string stripped_front;
};

// nats: a symbol's margin from its probability, ln(p / (1 - p)), at least 0; CallReading::kUnknownMargin for p = 1.
double margin_of(float probability);

// ---- Joined words: a segmentation into keywords and calls (docs/signal-processing.md, section 11; appendix A.12)
//
// A word the decoder joined across short word spaces (CQDXCQDXDEK8DXK8DX) is cut into tokens by dynamic programming: the most
// probable sequence (Viterbi over the split points) of keywords, calls and stray characters, under a model of tokens whose log
// probabilities are scored in nats against noise text, in which every character costs ln 36. Then a call on that path is believed
// only on the evidence noise does not give: its repeats, a DE beside it, a sign-off after it.
struct SegmentationCosts {
    // Gains are log probability ratios in nats against noise text, whose characters cost -ln p(c) each, p(c) proportional to
    // 2^-(the character's count of dots and dashes): E and T 2.10 nats, I A N M 2.80, a digit 4.87 (noise_char_nats; a model of what
    // a decoder reads from noise, heuristic). A keyword explains its characters: it gains their noise cost less its own, -ln of its
    // share of decoded words (measured, callsign.cpp's table): CQ 4.5 nats, DE 0.5, DX -1.0, K -0.7, AR -1.5.
    // nats: a new call: -ln P(a word is a call), measured: 3.32 (2026-11-17's decoded words) and 1.52 (2026-11-18's, the CWT contest),
    // their mean. Its characters cost n ln 36 less the gain of its form and length (call_form_nats, call_length_nats), against their noise cost.
    double call_nats = 2.4;
    // nats: a character no token explains (a stray or garbled one): -ln P(a stray). Heuristic: P = 1/10.
    double stray_nats = 2.3026;
    // nats: the call just sent, again, with nothing but keywords between: -ln P(a repeat); its characters are explained (K8DX again
    // gains 14.4 nats). Heuristic: P = 1/5.
    double repeat_nats = 1.6094;
    // nats: a DE right before a call, ln P(a call after DE) / P(a call); right after it (call DE call), ln P(DE after a call) / P(DE);
    // a K or KN right after it, likewise. Heuristic, of the size measured on 2026-11-17's decoded words (2.72, 1.22, 1.54 nats; the
    // CWT text of 2026-11-18 gives 1.06, -1.34, -0.03).
    double de_before_nats = 2.7726;
    double de_after_nats = 1.0;
    double sign_off_nats = 1.5;
    // When 0, a K or KN after a call that would also make it a longer call (AK4J K, AK4JK) gives no evidence; 1 counts it as any.
    double ambiguous_sign_off = 0.0;
    // When 0, a copy of a call right after a keyword that would also make it a longer call (TTW7QKW7Q: W7Q K W7Q, or W7Q KW7Q)
    // gains no evidence as a repeat: which it is, the costs decide by a few nats, and the station is as likely the longer call
    // (KW7Q, the K of its first copy garbled). 1 counts it as any repeat. Heuristic, the ambiguous K's rule turned around (call-form-1).
    double ambiguous_repeat = 0.0;
    // nats: a call on the path is believed when its evidence (its copies after the first, the DE beside them, the K after them;
    // a call's first copy is none, since noise text has many call forms) reaches this, and the whole path's gain (the word against
    // noise) reaches word_min_nats. A DE before it is enough, a DE after it only with an unambiguous K, a repeat always. Heuristic.
    double believe_nats = 2.5;
    double word_min_nats = 0.0;
    // Words longer than this are not cut (CPU; heuristic).
    std::size_t max_chars = 64;
};

// One token of a segmented word, in order; their texts make up the word.
struct Segment {
    enum class Kind { Keyword, Call, Stray };
    std::string text;
    Kind kind = Kind::Stray;
    bool believed = false;  // a call whose evidence reaches SegmentationCosts::believe_nats
    double evidence_nats = 0;  // a call's: the gains of its copies on the path and of the DE and sign-off beside them
};

// nats: a character's cost as noise text (SegmentationCosts).
double noise_char_nats(char c);
// nats: the gain of a call's form among strings of n characters, -ln of the share of them parse_call takes (derived from the grammar).
double call_form_nats(std::size_t n);
// nats: ln of the share of calls that have n characters (measured on the RBN; negative).
double call_length_nats(std::size_t n);
// The most probable segmentation of a word (upper case) into keywords, calls (parse_call; not only tiny pieces) and strays.
std::vector<Segment> segment_word(std::string_view word, const SegmentationCosts& costs = {});

// How read_calls reads a word that is not a call by itself.
struct ReadOptions {
    // 0: split_glued only (read_calls's own default; the stage's, CallSearchConfig::segment_joined, is 1); 1: segment_word
    // when split_glued finds no split; 2: segment_word in place of split_glued.
    int segment_joined = 0;
    std::size_t segment_min_chars = 6;  // words shorter than this are not segmented (heuristic)
    // When not 0, a believed call ending in E, I or K that is a call without it, or followed by a stray E or I or a K with which it
    // is a call, gives both readings, each stripped (0.5 point), for the stage to choose between.
    int segment_alternatives = 0;
    SegmentationCosts costs;
    // When not 0, a joined reading is refused when its characters, continued through the words after it, repeat with a period
    // shorter than the call over at least two periods and the whole call (L 9 6LL 9 6LL: L96LL; 5RE 5RE: 5RE5RE): periodic text, no
    // call, as in segment_word. A call sent twice (N 5 JJ N 5 JJ) has the call's own period and stays. Heuristic (call-form-1).
    int joined_periodic = 0;
    // When not 0, an X in front of a call is also read as TU run onto it, a stripped reading (XW3RJ: W3RJ, 0.5 point), as a stray
    // E, T or I is: T (-) and U (..-) with their letter space lost are X (-..-) (derived), and TU before his own call ends a
    // runner's every QSO in a contest. The half point is stripped_points, a heuristic; chosen by measurement (call-arb-1).
    int x_as_tu = 0;
    // When not 0, a call after CQ and one contest or event word (is_cq_event_word: CQ CWT K5NN, CQ SST W1AW, CQ SKCC N1MM) has the role
    // Sender, as after CQ itself: the station calling CQ in the event. A heuristic (calls-list-1's review).
    int cq_event = 0;
};

// A contest or event word that a station calling CQ puts between CQ and its call (CQ CWT K5NN): CWT, SST, MST, NS, NAQP, SS, FD, NA,
// QRP, FOC, SKCC, BUG, ARRL, WAE, WWFF, IOTA, BOTA, LOTA, SEQP, SPRINT (TEST, POTA, SOTA and DX already sign the call after them).
bool is_cq_event_word(std::string_view word);

// Every reading of a call that begins in the words [from, to). A word can give several: they are alternatives, and the evidence
// decides among them. `words` may reach past `to` and before `from`, for context.
std::vector<CallReading> read_calls(std::span<const Word> words, std::size_t from, std::size_t to);
// As read_calls; given the symbols the words were made of (words_of), each piece of a glued word has its own characters' times
// (CallSearchConfig::glued_pieces_timed); with an empty span, the word's.
std::vector<CallReading> read_calls(std::span<const Word> words, std::size_t from, std::size_t to, std::span<const DecodedSymbol> symbols);
std::vector<CallReading> read_calls(std::span<const Word> words, std::size_t from, std::size_t to, std::span<const DecodedSymbol> symbols,
                                    const ReadOptions& options);

// ---- A known-call list

// A set of calls, by base call. MASTER.SCP's format: one call a line; a line starting with # or ; or !! (a directive, "!!Order,1,1") or
// blank is skipped; case is ignored; anything after the first white space on a line is ignored. size() counts every other first word.
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
    // Joined words read by segmentation (segment_word): 1, the default, for a word split_glued cannot split; 2 for every word of
    // segment_min_chars or more that is not a call by itself, in place of split_glued (a variant); 0 off. A believed call's copies
    // are Glued readings with the roles their neighbors on the path give. On since joined-words-1: 8 more window-confirmed calls on
    // the 2026-11-17 recordings, and the calls it adds checked against the FCC's license file (appendix A.12).
    double segment_joined = 1.0;
    double segment_min_chars = 6.0;
    // Both readings of a call whose last E, I or K the segmentation cannot place, each stripped (ReadOptions::segment_alternatives);
    // 1, the default.
    double segment_alternatives = 1.0;
    SegmentationCosts segmentation;
    // The call forms that need more than any call when unlisted, each by its setting: 2, the default, it needs *_points_signed when
    // some occurrence counted had the role Sender, else *_points, as the 1x1's bar works (short_points); 1, it is reported from a
    // known-call list only (the rule before calls-list-1); 0, as any call. A listed call needs only what any listed call needs. The
    // owner's ruling (2026-11-21): without the list no real form may be excluded outright ("poor N1MM would never be spotted when he
    // chooses to operate"), several repeats should be enough, a signature not required, fewer repeats if signed. The bars are
    // heuristics, chosen by measurement (calls-list-1's review; appendix A.12); a call of two such forms needs the higher bar. The
    // bars apply with a known-call list loaded too, to a call the list does not hold: MASTER.SCP is one-sided evidence (a call off it
    // proves nothing), and CW Skimmer does the same. The signed bar, 3 for each form: at 2, a misreading of DL3I as L3I, signed once
    // and read twice, became a false call on the generated contest (fresh seed) at every bare bar (the scorer's own call pattern
    // refuses the form, so its score lines did not show it); at 3 no generated list changes. The bare bar, 4 for each form: of 4, 5,
    // 6, 7 and 8 the lowest, and every one of them loses no window-confirmed call on the three replay sets, without the list or with
    // MASTER.SCP, and adds no generated false call. The data give no recall evidence for any bare bar: every call these forms
    // gained on their bare bar was junk, an exchange number or an RBN skimmer's own one-spot misreading (T5M); the one real call
    // gained, AM800CT, signed. CW Skimmer's bar for a pattern absent from its table is 5 repeats; the owner asked for "several".
    //
    // Four letters after the digit (ParsedCall::unusual: VK3FABC, EE4EEEE). CW Skimmer's practice in kind (an unusual pattern needs
    // 5 repeats at its Normal level, against 2 or 3; RBN 2021). Measured (call-form-1): of the base calls three or more RBN skimmers
    // spotted on 2026-11-17, 11 of 4 366 (0.25%) have this form, most of them busts (SD6MCWT, IK4EATX, W0RAUW), while on the seven
    // 2026-11-17 recordings all 8 such calls the stage reported as any call were false (EE4EEEE, TE7TTTD, GA5NNON).
    double unusual_form = 2.0;
    double unusual_points = 4.0;
    double unusual_points_signed = 3.0;
    // Its only digit its prefix's (ParsedCall::prefix_digit: C6AGU, E7DX, D4C). Measured (call-form-2): as any call, the form added
    // only junk on the three replay sets (E5E, T5E, A9E; 2026-11-17 misread 85 to 115, no confirmed call gained), though the RBN
    // spots real ones (C8K, E6DX, T2DE).
    double prefix_digit_form = 2.0;
    double prefix_digit_points = 4.0;
    double prefix_digit_points_signed = 3.0;
    // A word of CW with a call's form (ParsedCall::cw_word: N1MM, K9AY, G5RV, R5NN, DJ5NN): read in text about rigs, antennas and
    // software, or a report glued to a word, far more often than sent as a call.
    double cw_word_form = 2.0;
    double cw_word_points = 4.0;
    double cw_word_points_signed = 3.0;
    // Two or more digits after the prefix (ParsedCall::special: W100AW, TM100TF, II0IARU), a special-event form.
    double special_form = 2.0;
    double special_points = 4.0;
    double special_points_signed = 3.0;
    // Which readings of those four forms reach a candidate, unlisted: 1, the default, whole words and glued ones only (a joined or
    // stripped reading is dropped, as a tiny call's is); 0, every reading. Joined pieces of grid squares (CM 96AX), of serial numbers
    // and of fragments (A 9E, E 7L) make these forms far more often than a station does.
    double form_whole_words = 1.0;
    // A word of CW that is a keyword run onto 5NN (R5NN, TU5NN, UR5NN, K5NN: is_keyword_report), unlisted, needs a counted signed
    // reading, when not 0: a runner's UR 5NN or TU 5NN read as one word in every exchange passes any bar of repeats.
    double keyword_report_signed = 1.0;
    // In the arbitration between near variants, an unlisted call of those four forms holds a rival back only once it is reported,
    // that is once it has passed its own bar, when not 0: a candidate that cannot be reported holds nothing back, and one that has
    // reached its bar holds and defends at its full points, as any call. Its defense is never weighed down, so its readings are
    // never left out of its own pooled support by a weaker rival. Without it, on the 2026-11-18 CWT, E5EB (never reported) held
    // G5EB and junk candidates of these forms held S58M and others. Heuristic (calls-list-1's review; appendix A.12).
    double form_rival_reported = 1.0;
    // ReadOptions::cq_event.
    double cq_event_signed = 1.0;
    // ReadOptions::joined_periodic; on since call-form-1.
    double joined_periodic = 1.0;
    // ReadOptions::x_as_tu; on since call-arb-1: on the 2026-11-18 CWT W3RJ (116 RBN skimmers) was withdrawn for XW3RJ (2), its
    // TU W3RJ run together, and listed again only at 3 455 s on 40 m.
    double x_as_tu = 1.0;
    // A transmission read both as a fragment's whole word and as a joined reading that starts with it (K4VR T: K4VR at 1 point,
    // K4VRT at joined_points; CallReading::covers) counts, in the comparison of the two calls as near variants (held_at), 3, the
    // default: for the longer only, the fragment's occurrence left out, when the longer has points of its own at this place
    // outside the transmissions it covers, else for both; 1: for the longer only, always; 2: for neither, unless neither then has
    // any points, when both count; 0: for both, each at its points (the fragment wins every such transmission, 1 against 0.75).
    // The report test is unchanged. Heuristic, chosen by measurement (call-arb-1): 1 lost window-confirmed calls read with a stray
    // letter after them (W6SX E, HC5A M), and covering a shorter joined reading too (K 4VR T) lost more (HB9T, KZ8R, K8BZ).
    double joined_covers = 3.0;
    // Points the longer call needs at the place outside the transmissions it covers for joined_covers 3 to take them: more than
    // one reading, two joined ones at least (1.5). Heuristic (review of call-arb-1: above 0, any one reading of the longer call took
    // a real call's transmissions; at 1.0, one whole word still did, the reviewer's probe W1AW T against one QRZ W1AWT).
    double covers_own_points = 1.5;
    // A stripped reading made by taking a stray off the front counts flat, without the listed weight (known_rival_factor), in the
    // arbitration: 1, the default, when an X was taken (x_as_tu: a listed F4ABC at 8 x 0.5 x 3 points beat XF4ABC at 8); 2 also
    // for E, T, I; 0 never. Heuristic (ruled on review of call-arb-1).
    double flat_stripped = 1.0;
    // Points an unlisted call of three characters (one letter, a digit, one letter: a 1x1, N4A, W6S, K1A; is_short_call) needs to
    // be reported, as pooled or local points are tested: short_points_signed when some occurrence counted had the role Sender (DE
    // N4A, CQ ... N4A, N4A K), else short_points; the rules for any call (points_framed, points_bare, tiny_points) still apply, so
    // 0 leaves the bar as it is. Only 0.63% to 0.73% of the calls the RBN spots have three characters (measured; call_length_nats),
    // against 20.0% with four, while fragments of longer calls read as this form (K7U from K7UT, summed over scattered places).
    // CW Skimmer's practice in kind (more repeats for a less common pattern; RBN 2021). Heuristic, chosen by measurement
    // (call-short-1).
    double short_points = 3.0;
    double short_points_signed = 2.0;
    // A call with the shape of a number sent in cut digits (is_cut_number_form: 2T7A, 3N4D, 5A3BOB; A for 1, N for 9, T for 0, as
    // the CWT's members' numbers are sent): 2, the default, unlisted, it needs cut_number_points_signed when some occurrence counted
    // had the role Sender, else cut_number_points, as the 1x1's bar works (short_points); 1, from a list only; 0, as any call. Not
    // a signed reading required: a DX station running the CWT (CQ CWT 8T2BH) never signs by the Role rule, and a call heard often
    // enough is reported (the owner's ruling, review of call-short-1). Measured (call-short-1): outside 9A, 7N and 8N, 4 of the
    // 11 370 base calls three or more RBN skimmers spotted on 2026-11-17, 2026-11-18 and 2017-08-21 have it (5A1AL, 5N80Y, 5T5T,
    // 9N1HA), while the replays of the 2026-11-18 CWT reported 27 such strings. The bar, chosen by measurement on that CWT (review of
    // call-short-1): 3, 4 and 5 points remove 7, 10 and 13 of the 25 numbers a signed reading required removed, with no window-confirmed
    // call lost and no generated false call added; 5, the lowest to remove most. The runners' own numbers (2T7A, 3N7N), read in every
    // exchange, pass any bar of several repeats. Heuristic.
    double cut_number_form = 2.0;
    double cut_number_points = 5.0;
    double cut_number_points_signed = 2.0;
};

// One line of the list.
struct CallRow {
    std::string call;         // what to show: the base call, or its most common form with a slash, seen at least twice
    std::string base;
    double freq_hz = 0;       // Hz from the span's center, at the latest reading where it was not named by another
    double first_s = 0;       // s, stream time of its first reading
    double reported_s = 0;    // s, stream time of the end of the reading that reported it; the list shows it at least settle_s later
    double last_s = 0;        // s, stream time of its latest reading
    // s, stream time of the end of its latest occurrence with a reading not named by another (Role::Named) within same_place_hz of
    // freq_hz: when the station itself was last heard where the row places it; -1 for none within the evidence window (window_s). The
    // app's calls on the waterfall go by it, since a reading that names the call elsewhere moves last_s but not freq_hz.
    double heard_s = -1;
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
    // next reading. What has been reported stays until a reading takes it back; a reading of a form from a list only (a
    // form's setting at 1, CallSearchConfig::unusual_form and the like) refused before is not read again.
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
        bool own = false;   // some reading of it was not Role::Named: the station itself was heard here (CallRow::heard_s)
        double margin = 0;  // nats: the best of the margins of the readings it holds that are whole or glued words (CallReading::margin_nats)
        bool flat = false;  // its points are a flat stripped reading's (CallSearchConfig::flat_stripped)
        std::string covers;  // CallReading::covers of a joined reading it holds, the first one
        bool sender = false;  // some reading of it had the role Sender (CallSearchConfig::short_points_signed, cut_number_points_signed)
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
        bool covers = false;  // some occurrence covers a fragment (Occurrence::covers)
        // Its form's marks (ParsedCall): the forms with their own bar when unlisted (CallSearchConfig::unusual_form and the like).
        bool special = false;
        bool unusual = false;
        bool prefix_digit = false;
        bool cw_word = false;
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
        double flat = 0;     // of points, those of flat occurrences (Occurrence::flat)
        bool sender = false;  // some occurrence counted is Occurrence::sender
    };

    void read_track(TrackText& track, bool all);
    void add(const CallReading& reading, double freq_hz);
    void evaluate(Candidate& candidate, double freq_hz, double now_s);
    // Its occurrences within within_hz of this place and window_s of now_s, on either side: now_s is the stream's present, except when
    // a withdrawal is judged at the loser's own last reading (evaluate), where a rival's later readings count up to window_s after it.
    Support support(const Candidate& candidate, double freq_hz, double now_s, double within_hz) const;
    // CallRow::heard_s for this candidate.
    double heard_at_place_s(const Candidate& candidate) const;
    bool passes(const Candidate& candidate, const Support& s) const;
    // Whether a call of these marks is reported from a known-call list only (a form's setting at 1, CallSearchConfig::unusual_form
    // and the like).
    bool listed_only(std::string_view base, bool special, bool unusual, bool prefix_digit, bool cw_word) const;
    // Whether an unlisted call of a form with its own bar holds no rival back yet (CallSearchConfig::form_rival_reported).
    bool holds_nothing(const Candidate& candidate) const;
    // Whether a better-supported near variant holds the call back at this place (CallSearchConfig::variant_hz); the best such rival's best
    // is added to rival_best, and the reported calls it beats here to beaten, when given.
    bool held_at(const Candidate& candidate, double freq_hz, double now_s, double* rival_best, std::vector<Candidate*>* beaten);
    // The points, within variant_hz of this place and window_s of now_s, of the fragment's occurrences that a joined occurrence of
    // the longer call covers (one transmission: they overlap in time within same_place_hz), and of those covering occurrences
    // (CallSearchConfig::joined_covers).
    std::pair<double, double> covered_points(const Candidate& fragment, const Candidate& longer, double freq_hz, double now_s) const;
    // The call's support over the whole band (CallSearchConfig::pool_places): its occurrences within window_s of now_s, one per
    // transmission, leaving out those at places where it is held.
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
