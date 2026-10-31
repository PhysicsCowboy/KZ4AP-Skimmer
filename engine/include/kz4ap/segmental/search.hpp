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
// birth's rise window cannot overlap the fall's window that ended the over.
//
// Pruning compares, at boundary j, every story on what it can reach through the horizon H = j + 16: an element already
// open cannot be charged for an edge it has not claimed yet, whose evidence already stands inside the horizon (this is
// the review's F3 in another form: pruning on the evidence through j + 10 alone dropped every gap one sample before its
// true rise once the level was high, CER above 3 at +20 dB S500 in stage 1). So a story in a gap reaches, beyond its base,
//     max(0, max over d = 1 ... 6 of rise(j + d) + C[H + 1] - C[j + d + 11]),
// and one in a mark
//     max(C[H + 1], max over d = 1 ... 6 of C[j + d + 5] + fall(j + d)),
// the same for every story, so computed once per boundary. Both add the element's survival so far.
#pragma once

#include "kz4ap/decoder.hpp"
#include "kz4ap/segmental/config.hpp"
#include "kz4ap/segmental/evidence.hpp"
#include "kz4ap/viterbi/morse_tree.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace kz4ap::segmental {

// The fist's duration tables for every speed and kind, and the text's costs by node: built once, shared by every channel.
class ModelTables {
public:
    explicit ModelTables(const SegmentalConfig& config);

    struct Range {
        int lo;       // samples: the shortest length allowed
        int hi;       // samples: the longest
        int offset;   // into the table of ln P: ln P(L) = log_end[offset + L - lo]
    };
    const Range& range(int kind, int speed) const { return ranges_[static_cast<std::size_t>(kind * speeds_ + speed)]; }
    double log_end(int kind, int speed, int length) const {
        const Range& r = range(kind, speed);
        return log_end_[static_cast<std::size_t>(r.offset + length - r.lo)];
    }
    // The optimistic survival of a mark (either label) and of a gap (any kind, with the kind's cost), nats, by length.
    double mark_survival(int speed, int length) const {
        return mark_survival_[static_cast<std::size_t>(survival_offset_[static_cast<std::size_t>(speed)] + length)];
    }
    double gap_survival(int speed, int length) const {
        return gap_survival_[static_cast<std::size_t>(survival_offset_[static_cast<std::size_t>(speed)] + length)];
    }
    int mark_hi(int speed) const { return mark_hi_[static_cast<std::size_t>(speed)]; }
    int gap_hi(int speed) const { return gap_hi_[static_cast<std::size_t>(speed)]; }
    int survival_length(int speed) const { return survival_length_[static_cast<std::size_t>(speed)]; }
    int speeds() const { return speeds_; }
    int nodes() const { return nodes_; }
    const viterbi::MorseTree& tree() const { return tree_; }
    int child(int node, int label) const { return children_[static_cast<std::size_t>(node * 2 + label)]; }
    bool has_children(int node) const { return child(node, 0) >= 0 || child(node, 1) >= 0; }
    bool has_symbol(int node) const { return !tree_.symbol(node).empty(); }
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
    int nodes_ = 0;
    int longest_ = 0;
    viterbi::MorseTree tree_;
    std::vector<int> children_;
    std::vector<Range> ranges_;
    std::vector<double> log_end_;
    std::vector<double> mark_survival_;
    std::vector<double> gap_survival_;
    std::vector<int> survival_offset_;
    std::vector<int> survival_length_;
    std::vector<int> mark_hi_;
    std::vector<int> gap_hi_;
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
