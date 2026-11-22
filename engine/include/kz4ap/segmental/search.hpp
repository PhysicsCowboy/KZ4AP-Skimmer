// The segmental decoder's search: the viterbi decoder's explicit-duration model (viterbi/search.hpp: the fist, the Morse
// tree, the symbol prior, the over's ends) over edges rather than over samples.
//
// The search runs behind the evidence. Boundary j is searched once the evidence through sample j + 16 exists, so a claimed
// edge at j is scored in full (its window, evidence.hpp) when it is merged: the review's merge before the edge's evidence
// (F3) cannot happen, and no edge history is needed in the state. For a next element of at least 6 samples the future
// depends on the past only through (j, speed, node, element kind), so merging on them at j is exact given the shared
// evidence; a next element shorter than that overlaps this edge's window, and its score reads only the pair of edges.
//
// An open element is a mark or gap that started at a claimed edge. It is touched at each boundary to offer its ends:
//   - a mark from node n at speed s, started at i, ends at j as a dot and as a dash, into a gap at the child node, scoring
//     its base + C[j + 5] + fall(j) + ln P_label(s, j - i), base = (score at i) + rise(i) - C[i + 11];
//   - a gap ends as each kind its node allows: element gap (mark at the same node), letter gap and word gap (mark at the
//     root, paying the letter's prior).
// The scores are the viterbi decoder's: the letter's ln 62 at its start and ln p + ln 62 at its end, the gap kinds', the
// over's end, the no-signal story's ln(1 - P) per sample and a birth's ln P. A birth starts a mark of open speed: its end
// offers every speed with ln(1 / speed_count). A rescue is the same mark from the best story that starts a letter at this
// boundary, paying ln epsilon for ln(1 - epsilon). An over's end reaches the no-signal story 6 samples later, so that a
// birth's rise window cannot overlap the fall's window that ended the over; with the channel's tail of n positions n + 6 samples
// later, taking the tail's score (text-fast-1).
//
// Pruning compares, at boundary j, every story on what it can reach through the horizon H = j + 16: an element already
// open cannot be charged for an edge it has not claimed yet, whose evidence already stands inside the horizon (this is
// the review's F3 in another form: pruning on the evidence through j + 10 alone dropped every gap one sample before its
// true rise once the level was high, CER above 3 at +20 dB S500 in stage 1). So a story in a gap reaches, beyond its base,
//     max(0, max over d = 1 ... 6 of rise(j + d) + C[H + 1] - C[j + d + 11]),
// and one in a mark
//     max(C[H + 1], max over d = 1 ... 6 of C[j + d + 5] + fall(j + d)),
// the same for every story, so computed once per boundary. Both add the element's survival so far. With the channel's tail a gap's
// reach takes the tail along each way it can go: ending at j + d with its first length + d - 6 positions, or going on through H
// (text-fast-1; evidence.hpp, RunningSums::tail).
//
// A story's fist. Farnsworth spacing and any sender with a separate time base for his gaps: letter and word gaps have
// medians 3 g T_s and 7 g T_s where marks and element gaps have T_s, 3 T_s and T_s, g the fist's gap stretch. (ARRL's
// standard spreads the added time over the 19 gap units of PARIS, so g = T_g / T_s, and letter and word gaps stretch alike;
// QEX, April 1990.) The search's state is (j, fist, node, kind), a fist being a pair (speed, stretch), and an element's `speed`
// below is that fist. A story takes its fist at the end of its first mark, a birth's or a rescue's, paying
// ln(1 / speed_count) as before, and ln(1 - pi) for g = 1 or ln(pi / (stretches - 1)) for any other (pi: gap_stretch_prior).
// It keeps it to the over's end. So standard text pays ln(1 - pi) once per over, and a stretch a story has no use for falls
// out of the beam within a few letters.
#pragma once

#include "kz4ap/decoder.hpp"
#include "kz4ap/segmental/config.hpp"
#include "kz4ap/segmental/evidence.hpp"
#include "kz4ap/viterbi/morse_tree.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace kz4ap::segmental {

// The fist's duration tables for every fist (speed x gap stretch) and kind, and the text's costs by node: built once, shared
// by every channel.
class ModelTables {
public:
    explicit ModelTables(const SegmentalConfig& config);

    struct Range {
        int lo;       // samples: the shortest length allowed
        int hi;       // samples: the longest
        int offset;   // into the table of ln P: ln P(L) = log_end[offset + L - lo]
    };
    const Range& range(int kind, int fist) const { return ranges_[static_cast<std::size_t>(kind * fists_ + fist)]; }
    double log_end(int kind, int fist, int length) const {
        const Range& r = range(kind, fist);
        return log_end_[static_cast<std::size_t>(r.offset + length - r.lo)];
    }
    // The optimistic survival of a mark (either label) and of a gap (any kind, with the kind's cost), nats, by length; a
    // length past the table reads its last entry.
    double mark_survival(int fist, int length) const {
        const int speed = speed_of(fist);
        return mark_survival_[static_cast<std::size_t>(mark_survival_offset_[static_cast<std::size_t>(speed)] +
                                                       std::min(length, mark_survival_length_[static_cast<std::size_t>(speed)] - 1))];
    }
    double gap_survival(int fist, int length) const {
        return gap_survival_[static_cast<std::size_t>(gap_survival_offset_[static_cast<std::size_t>(fist)] +
                                                      std::min(survival_index(length), gap_survival_length_[static_cast<std::size_t>(fist)] - 1))];
    }
    // The gaps' survival table reads every length to kSurvivalFineLengths samples, then one length in 16: a long gap's survival barely moves, and at 8 bytes a length the
    // table of about 100 fists over thousands of lengths is what the search's loop mostly waits for. Each entry is the first length's, so that the bound stays optimistic.
    static constexpr int kSurvivalFineLengths = 256;
    static int survival_index(int length) {
        return length < kSurvivalFineLengths ? length : kSurvivalFineLengths + ((length - kSurvivalFineLengths) >> 4);
    }
    int mark_hi(int fist) const { return mark_hi_[static_cast<std::size_t>(speed_of(fist))]; }
    int gap_hi(int fist) const { return gap_hi_[static_cast<std::size_t>(fist)]; }

    // The same values for the search's loop, which reads them for every open element at every boundary: one fist's lengths in one row.
    struct FistRow {
        std::array<Range, 5> ends;    // by kind: dot, dash, element gap, letter gap, word gap
        int mark_survival_first = 0;  // into mark_survivals()
        int mark_survival_last = 0;   // the last length it holds
        int gap_survival_first = 0;   // into gap_survivals()
        int gap_survival_last = 0;
        int mark_hi = 0;
        int gap_hi = 0;
        bool stretched = false;  // g above 1
    };
    const FistRow& row(int fist) const { return rows_[static_cast<std::size_t>(fist)]; }
    const double* log_ends() const { return log_end_.data(); }
    const double* mark_survivals() const { return mark_survival_.data(); }
    const double* gap_survivals() const { return gap_survival_.data(); }
    const std::uint8_t* symbol_at_nodes() const { return has_symbol_.data(); }
    const std::uint8_t* children_at_nodes() const { return has_children_.data(); }
    // A birth's first mark at a length: what it can end as, in the order of the speeds and labels, and the best speed's survival.
    struct OpenEnd {
        double log_end;  // nats: ln P_label(speed, length)
        int fist;        // the speed's first fist, g = 1
        int speed;
        int child;       // the node of its label
        bool stretched;  // the speed has other stretches
    };
    std::span<const OpenEnd> open_ends(int length) const {
        const auto first = static_cast<std::size_t>(open_end_first_[static_cast<std::size_t>(length)]);
        return std::span<const OpenEnd>(open_ends_).subspan(first, static_cast<std::size_t>(open_end_first_[static_cast<std::size_t>(length) + 1]) - first);
    }
    double open_survival(int length) const { return open_survival_[static_cast<std::size_t>(length)]; }
    // samples: SegmentalConfig::open_spacing_dots at a fist's speed
    int open_spacing(int fist) const { return open_spacing_[static_cast<std::size_t>(fist)]; }
    // The fists, in the order of their speeds: speed s's are first_fist(s) ... first_fist(s + 1) - 1, g = 1 first.
    int fists() const { return fists_; }
    int first_fist(int speed) const { return first_fist_[static_cast<std::size_t>(speed)]; }
    int speed_of(int fist) const { return speed_of_[static_cast<std::size_t>(fist)]; }
    // 1: the fist's gap stretch g
    double stretch_of(int fist) const { return stretch_of_[static_cast<std::size_t>(fist)]; }
    // nats: what taking g = 1 costs, and any other (see above)
    double log_plain() const { return log_plain_; }
    double log_stretched() const { return log_stretched_; }
    int speeds() const { return speeds_; }
    int nodes() const { return nodes_; }
    const viterbi::MorseTree& tree() const { return tree_; }
    int child(int node, int label) const { return children_[static_cast<std::size_t>(node * 2 + label)]; }
    bool has_children(int node) const { return has_children_[static_cast<std::size_t>(node)] != 0; }
    bool has_symbol(int node) const { return has_symbol_[static_cast<std::size_t>(node)] != 0; }
    // nats: ln p(symbol) + ln 62, paid when a letter at this node ends
    double letter_end(int node) const { return letter_end_[static_cast<std::size_t>(node)]; }
    double letter_start() const { return letter_start_; }      // nats: -ln 62 - ln(1 / (1 - epsilon)) at a letter's start
    double letter_gap_cost() const { return letter_gap_cost_; }
    double word_gap_cost() const { return word_gap_cost_; }
    double over_end_cost() const { return over_end_cost_; }
    double log_birth() const { return log_birth_; }            // nats: ln P per sample of silence
    double log_silence() const { return log_silence_; }        // nats: ln(1 - P) per sample of silence
    double log_first_speed() const { return log_first_speed_; }  // nats: ln(1 / speed_count)
    double log_rescue() const { return log_rescue_; }          // nats: ln epsilon
    double log_keep() const { return log_keep_; }              // nats: ln(1 - epsilon)
    double sample_interval_s() const { return sample_interval_s_; }
    int longest() const { return longest_; }

private:
    int speeds_ = 0;
    int fists_ = 0;
    int nodes_ = 0;
    int longest_ = 0;
    viterbi::MorseTree tree_;
    std::vector<int> children_;
    std::vector<std::uint8_t> has_symbol_;    // by node
    std::vector<std::uint8_t> has_children_;  // by node
    std::vector<FistRow> rows_;               // by fist
    std::vector<OpenEnd> open_ends_;
    std::vector<int> open_end_first_;         // by length, and the count at the end
    std::vector<double> open_survival_;       // by length
    std::vector<int> open_spacing_;           // by fist
    std::vector<Range> ranges_;
    std::vector<double> log_end_;
    std::vector<double> mark_survival_;
    std::vector<double> gap_survival_;
    std::vector<int> mark_survival_offset_;  // by speed
    std::vector<int> mark_survival_length_;
    std::vector<int> gap_survival_offset_;   // by fist
    std::vector<int> gap_survival_length_;
    std::vector<int> mark_hi_;               // by speed
    std::vector<int> gap_hi_;                // by fist
    std::vector<int> first_fist_;            // by speed, and the fists' count at the end
    std::vector<int> speed_of_;              // by fist
    std::vector<double> stretch_of_;         // by fist
    double log_plain_ = 0.0;
    double log_stretched_ = 0.0;
    std::vector<double> letter_end_;
    double letter_start_ = 0.0;
    double letter_gap_cost_ = 0.0;
    double word_gap_cost_ = 0.0;
    double over_end_cost_ = 0.0;
    double log_birth_ = 0.0;
    double log_silence_ = 0.0;
    double log_first_speed_ = 0.0;
    double log_rescue_ = 0.0;
    double log_keep_ = 0.0;
    double sample_interval_s_ = 0.0;
};

// How much of the work the search did, for the bench and the tests.
struct SearchStats {
    std::int64_t boundaries = 0;      // boundaries searched
    std::int64_t touches = 0;         // open elements touched, summed over the boundaries
    std::int64_t opened = 0;          // elements opened
    std::int64_t largest_live = 0;    // the most open elements after a boundary's pruning
    friend bool operator==(const SearchStats&, const SearchStats&) = default;
};

// The best story at a moment, as a decoder that publishes while the channel runs needs it (Search::best_story).
struct BestStory {
    std::vector<DecodedSymbol> tail;  // the story's last symbols, first symbol first
    std::size_t length = 0;           // the story's whole text, symbols, so tail[0] is symbol length - tail.size()
    bool open_letter = false;         // the last symbol is a letter still open: a gap has begun, which may turn out an
                                      // element gap and the letter a longer one
    std::optional<int> speed;         // its speed on the grid, 0 ... speed_count - 1 (dot_min_ms x speed_ratio^s); none for
                                      // the no-signal story and a birth's first mark, which has no speed yet
};

class Search {
public:
    // tables and levels outlive the Search; sums is read as it grows. levels: the short elements' templates; without them
    // an element shorter than 6 samples scores by the plain formula (for tests of the bookkeeping).
    Search(const SegmentalConfig& config, const ModelTables& tables, const RunningSums& sums,
           const LevelTables* levels = nullptr);
    ~Search();
    Search(Search&&) noexcept;
    Search& operator=(Search&&) noexcept;

    // Searches every boundary whose evidence has been committed.
    void advance();
    // The time of sample m (s): a stream's samples are consecutive unless it was told otherwise.
    void note_sample_time(std::int64_t m, double t_s);

    // The best story's text.
    std::vector<DecodedSymbol> best_text() const;
    // The same story's last max_symbols symbols and its speed, at a cost that does not grow with the text.
    BestStory best_story(std::size_t max_symbols) const;
    // The best story's score at the last boundary searched, reach through the horizon included, nats against key-up throughout
    // (the no-signal story's scale): what the channel's evidence for keyed Morse is read from.
    double best_score() const;
    const SearchStats& stats() const;
    // The open elements after the last boundary's pruning.
    std::size_t live() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace kz4ap::segmental
