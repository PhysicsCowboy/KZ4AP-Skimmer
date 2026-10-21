#include "kz4ap/viterbi/search.hpp"

#include "kz4ap/viterbi/fist.hpp"
#include "kz4ap/viterbi/morse_tree.hpp"
#include "viterbi/text.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

namespace kz4ap::viterbi {
namespace {

constexpr double kInf = std::numeric_limits<double>::infinity();
constexpr int kOpen = -1;                    // a speed or factor not yet taken
constexpr std::int32_t kNone = -1;           // no candidate
constexpr std::string_view kWordSpace = " ";
constexpr double kStatsWindowS = 0.1;        // s: the window of the live count's series

// What a story is doing. Its order is the first part of the state's order.
enum class Phase : std::uint8_t { NoSignal = 0, Mark = 1, Gap = 2 };

// The state (plan §2.7, item 4) packed into 64 bits, the phase (activity and key) in the top 2, tau in the next 32,
// then speed + 1 and factor + 1 in 8 bits each (open = 0) and node + 1 in 14. The packed order is one fixed order of
// the states. The no-signal state, which has no clock, speed, factor or node, is 0.
std::uint64_t pack_state(Phase phase, int tau, int speed, int factor, int node) {
    if (phase == Phase::NoSignal) return 0;
    return (static_cast<std::uint64_t>(phase) << 62) |
           (static_cast<std::uint64_t>(static_cast<std::uint32_t>(tau)) << 30) |
           (static_cast<std::uint64_t>(speed + 1) << 22) | (static_cast<std::uint64_t>(factor + 1) << 14) |
           static_cast<std::uint64_t>(node + 1);
}

// What became of a story's parent to make it.
enum class Move : std::uint8_t {
    Continue,          // the parent, one piece on
    EndMark,           // the parent's mark ended as a dot or a dash: a gap at the child node
    EndOver,           // the parent's mark ended and with it the over: no signal
    EndElementGap,     // the parent's gap ended between elements: a mark at the same node
    EndLetterGap,      // between letters: a letter starts at the root
    EndWordGap,        // between words: a word space, and a letter starts at the root
    BirthFromSilence,  // a newborn from the no-signal story
    BirthRescue,       // a rescue newborn, after its parent's gap ended between letters or words
};

struct Story {
    Phase phase = Phase::NoSignal;
    std::int32_t tau = 0;        // pieces in the current element
    std::int16_t speed = kOpen;  // 0 ... speed_count - 1, or open
    std::int16_t factor = kOpen; // 0 ... factor_count - 1, or open
    std::int16_t node = -1;      // a mark: the node it started from; a gap: the node of the mark before it
    std::uint64_t state = 0;     // the packed state
    double score = 0.0;          // nats, relative to the best
    // nats: the score without the current element's timing and the current block's evidence (the score at the
    // element's start plus the evidence of the element's finished blocks)
    double base = 0.0;
    Level level;                          // as learned, never predicted
    std::complex<double> block_sum = 0.0; // FS: the current block's coherent sum
    std::int32_t block_pieces = 0;        // pieces in it
    double mode_a = 0.0;                  // FS: the previous piece's mode in the block, read when block_pieces > 0
    std::int32_t text = SharedText::kEmpty;
    double letter_start_s = 0.0;   // s: the start of the current letter's first mark
    double last_mark_end_s = 0.0;  // s: the end of the last finished mark
    std::int64_t line = 0;
    std::uint64_t creation = 0;
};

// How a candidate is made from the previous population.
struct Origin {
    std::int32_t parent = kNone;  // its index in the previous population
    Move move = Move::Continue;
    std::uint8_t label = 0;        // EndMark, EndOver: 0 for a dot, 1 for a dash
    Move rescued_gap = Move::Continue;  // BirthRescue: EndLetterGap or EndWordGap, the gap its parent ended
    std::int16_t speed = kOpen;    // the new story's
    std::int16_t factor = kOpen;   // the new story's
    std::int64_t line = 0;
};

// One successor kind's best offer at a piece (a merge of everything offered into one state).
struct Slot {
    double score = -kInf;
    std::uint64_t creation = 0;
    Origin origin;
    std::int64_t piece = -1;  // the piece at which it was last filled; empty at any other
};

// A table from a state to the index of the candidate kept in it at the current piece: open addressing with linear
// probing, kept at most half full, emptied for each piece by a generation count. It is only searched, never walked.
class StateTable {
public:
    void reset(std::size_t insertions) {
        std::size_t capacity = 16;
        while (capacity < 2 * insertions + 2) capacity *= 2;
        if (capacity > entries_.size()) entries_.assign(capacity, Entry{});
        mask_ = entries_.size() - 1;
        ++generation_;
    }
    // The index kept for `state`, kNone if the state has none yet (the caller fills it in).
    std::int32_t& operator[](std::uint64_t state) {
        for (std::size_t i = spread(state) & mask_;; i = (i + 1) & mask_) {
            Entry& e = entries_[i];
            if (e.generation != generation_) {
                e = Entry{state, generation_, kNone};
                return e.index;
            }
            if (e.state == state) return e.index;
        }
    }

private:
    struct Entry {
        std::uint64_t state = 0;
        std::uint64_t generation = 0;
        std::int32_t index = kNone;
    };
    // splitmix64's finalizer: every bit of the state moves the slot.
    static std::uint64_t spread(std::uint64_t x) {
        x ^= x >> 30;
        x *= 0xbf58476d1ce4e5b9ULL;
        x ^= x >> 27;
        x *= 0x94d049bb133111ebULL;
        return x ^ (x >> 31);
    }
    std::vector<Entry> entries_;
    std::size_t mask_ = 0;
    std::uint64_t generation_ = 0;
};

// Pruning in place (plan §2.7, item 5): `alive` holds indices into `ranked`. Those more than beam_nats behind the best
// are dropped, keeping the order of the rest; then, if more than `ceiling` remain, the `ceiling` first by ranks_before
// are kept (in the order std::nth_element leaves them). Returns the index of the best. The dropped go to the lists
// given.
std::uint32_t prune_alive(const detail::Ranked* ranked, std::vector<std::uint32_t>& alive, double beam_nats,
                          std::size_t ceiling, std::vector<std::uint32_t>* beam_dropped,
                          std::vector<std::uint32_t>* ceiling_dropped) {
    std::uint32_t best = alive.front();
    for (const std::uint32_t j : alive)
        if (detail::ranks_before(ranked[j], ranked[best])) best = j;
    const double best_score = ranked[best].score_nats;
    std::size_t kept = 0;
    for (const std::uint32_t j : alive) {
        if (best_score - ranked[j].score_nats > beam_nats) {
            if (beam_dropped) beam_dropped->push_back(j);
        } else {
            alive[kept++] = j;
        }
    }
    alive.resize(kept);
    if (alive.size() > ceiling) {
        const auto cut = alive.begin() + static_cast<std::ptrdiff_t>(ceiling);
        std::nth_element(alive.begin(), cut, alive.end(), [ranked](std::uint32_t a, std::uint32_t b) {
            return detail::ranks_before(ranked[a], ranked[b]);
        });
        if (ceiling_dropped) ceiling_dropped->insert(ceiling_dropped->end(), cut, alive.end());
        alive.resize(ceiling);
    }
    return best;
}

// Grows v's capacity to hold `needed` elements, at most doubling it and never beyond `limit` elements (needed <=
// limit), so that v's storage never exceeds `limit` elements, whatever the library's own growth.
template <class T>
void grow_within(std::vector<T>& v, std::size_t needed, std::size_t limit) {
    if (needed > v.capacity()) v.reserve(std::min(limit, std::max(needed, 2 * v.capacity())));
}

// The fist's maxima that open speeds and factors need (plan §2.5, items 7 and 8), each the plain maximum of Fist's own
// values over the speeds or the factors. They depend only on the configuration: those of the first `cap` taus are kept
// in tables, filled as first asked for, and those of any later tau are computed again whenever asked, in the same
// way, so that no table ever holds more than `cap` taus (search.hpp, "Work saved", has the bound).
class OpenTiming {
public:
    // ln P_label(s, tau) of a newborn's first mark for both labels and every speed (label-major; -infinity for a label
    // the root does not allow), and its maxima. The pointer holds until the next call.
    struct FirstMarkEnds {
        const double* log_end;     // [label x speeds + speed]
        double label_best[2];      // the best over the speeds, per label
        double best;               // the best of all
    };

    // For kind LetterGap or WordGap at a speed and tau, a gap whose factor is open: the maximum over the factors f of
    // ln P_kind(s, f, tau), the first factor where it is reached, and whether ln P, as Fist computes it, is unimodal in
    // f: not decreasing up to that factor and not increasing after it. Where it is, the factors whose successor a
    // threshold keeps are those around that factor up to the first on each side that the threshold drops.
    struct FactorEntry {
        double end;  // nats
        std::int32_t end_factor;
        bool unimodal;
    };

    // The tables' bytes per tau with `speeds` speeds: two kinds' factor entries per speed, and a first mark's row of
    // ends with its three maxima and its survival.
    static std::size_t bytes_per_tau(int speeds) {
        const std::size_t row = 2 * static_cast<std::size_t>(speeds);
        return row * sizeof(FactorEntry) + row * sizeof(double) + sizeof(std::array<double, 3>) + sizeof(double);
    }

    OpenTiming(const Fist& fist, const MorseTree& tree, int speeds, int factors, int cap)
        : fist_(fist),
          speeds_(speeds),
          factors_(factors),
          cap_(cap),
          by_kind_and_speed_(2 * static_cast<std::size_t>(speeds)) {
        for (int label = 0; label < 2; ++label) root_has_[label] = tree.child(tree.root(), label == 1) >= 0;
    }

    // max over speeds s and the labels the root allows of ln S_label(s, tau): a newborn's first mark.
    double first_mark_survival(int tau) {
        if (tau > cap_) return first_survival_at(tau);
        while (first_survival_.size() < static_cast<std::size_t>(tau)) {
            grow_within(first_survival_, first_survival_.size() + 1, static_cast<std::size_t>(cap_));
            first_survival_.push_back(first_survival_at(static_cast<int>(first_survival_.size()) + 1));
        }
        return first_survival_[static_cast<std::size_t>(tau) - 1];
    }

    FirstMarkEnds first_mark_ends(int tau) {
        const std::size_t row = 2 * static_cast<std::size_t>(speeds_);
        if (tau > cap_) {
            scratch_ends_.resize(row);
            const std::array<double, 3> best = first_ends_at(tau, scratch_ends_.data());
            return {scratch_ends_.data(), {best[0], best[1]}, best[2]};
        }
        while (first_best_.size() < static_cast<std::size_t>(tau)) {
            const std::size_t t = first_best_.size() + 1;
            grow_within(first_ends_, t * row, static_cast<std::size_t>(cap_) * row);
            grow_within(first_best_, t, static_cast<std::size_t>(cap_));
            first_ends_.resize(t * row);
            first_best_.push_back(first_ends_at(static_cast<int>(t), first_ends_.data() + (t - 1) * row));
        }
        const std::array<double, 3>& best = first_best_[static_cast<std::size_t>(tau) - 1];
        return {first_ends_.data() + (static_cast<std::size_t>(tau) - 1) * row, {best[0], best[1]}, best[2]};
    }

    // The entry for (kind, speed, tau). The reference holds until the next call.
    const FactorEntry& factor_entry(Kind kind, int speed, int tau) {
        if (tau > cap_) {
            scratch_entry_ = factor_entry_at(kind, speed, tau);
            return scratch_entry_;
        }
        std::vector<FactorEntry>& table = by_kind_and_speed_[static_cast<std::size_t>(kind == Kind::WordGap) *
                                                                 static_cast<std::size_t>(speeds_) +
                                                             static_cast<std::size_t>(speed)];
        while (table.size() < static_cast<std::size_t>(tau)) {
            grow_within(table, table.size() + 1, static_cast<std::size_t>(cap_));
            table.push_back(factor_entry_at(kind, speed, static_cast<int>(table.size()) + 1));
        }
        return table[static_cast<std::size_t>(tau) - 1];
    }

    static Kind mark_kind(int label) { return label == 1 ? Kind::Dash : Kind::Dot; }

private:
    double first_survival_at(int t) const {
        double best = -kInf;
        for (int label = 0; label < 2; ++label)
            if (root_has_[label])
                for (int s = 0; s < speeds_; ++s) best = std::max(best, fist_.log_survival(mark_kind(label), s, 0, t));
        return best;
    }

    // Fills `row` (2 x speeds values) with the first mark's ln P at tau t and returns the maxima: per label, of all.
    std::array<double, 3> first_ends_at(int t, double* row) const {
        std::array<double, 3> best = {-kInf, -kInf, -kInf};
        for (int label = 0; label < 2; ++label) {
            for (int s = 0; s < speeds_; ++s) {
                const double value = root_has_[label] ? fist_.log_end(mark_kind(label), s, 0, t) : -kInf;
                row[static_cast<std::size_t>(label * speeds_ + s)] = value;
                best[static_cast<std::size_t>(label)] = std::max(best[static_cast<std::size_t>(label)], value);
            }
        }
        best[2] = std::max(best[0], best[1]);
        return best;
    }

    FactorEntry factor_entry_at(Kind kind, int speed, int t) {
        FactorEntry entry{-kInf, 0, true};
        row_.resize(static_cast<std::size_t>(factors_));
        for (int f = 0; f < factors_; ++f) {
            row_[static_cast<std::size_t>(f)] = fist_.log_end(kind, speed, f, t);
            if (row_[static_cast<std::size_t>(f)] > entry.end) {
                entry.end = row_[static_cast<std::size_t>(f)];
                entry.end_factor = f;
            }
        }
        for (int f = 0; f + 1 < factors_; ++f) {
            const double here = row_[static_cast<std::size_t>(f)];
            const double next = row_[static_cast<std::size_t>(f) + 1];
            if (f < entry.end_factor ? next < here : next > here) entry.unimodal = false;
        }
        return entry;
    }

    const Fist& fist_;
    int speeds_;
    int factors_;
    int cap_;  // pieces: the taus kept
    bool root_has_[2] = {false, false};
    std::vector<double> first_survival_;                      // by tau - 1
    std::vector<double> first_ends_;                          // by tau - 1, rows of 2 x speeds
    std::vector<std::array<double, 3>> first_best_;           // by tau - 1
    std::vector<std::vector<FactorEntry>> by_kind_and_speed_; // [(kind is WordGap) x speeds + speed][tau - 1]
    std::vector<double> row_;                                 // ln P over the factors, while an entry is computed
    std::vector<double> scratch_ends_;                        // a first mark's row beyond the cap
    FactorEntry scratch_entry_{-kInf, 0, true};               // an entry beyond the cap
};

// bytes: the open timing's budget per Search, which sets its tables' cap (search.hpp, "Work saved").
constexpr std::size_t kOpenTimingBudgetBytes = 4'000'000;

// The open timing's cap, pieces: the options' where set (positive), else the most taus the budget holds.
int open_timing_cap(const ViterbiConfig& config, const detail::SearchOptions& options) {
    if (options.open_timing_cap_pieces > 0) return options.open_timing_cap_pieces;
    const std::size_t taus = kOpenTimingBudgetBytes / OpenTiming::bytes_per_tau(std::max(config.speed_count, 1));
    const std::size_t most = static_cast<std::size_t>(std::numeric_limits<int>::max());
    return static_cast<int>(std::clamp<std::size_t>(taus, 1, most));
}

bool positive_and_finite(double x) { return x > 0.0 && std::isfinite(x); }

}  // namespace

namespace detail {

bool ranks_before(const Ranked& a, const Ranked& b) {
    if (a.score_nats != b.score_nats) return a.score_nats > b.score_nats;
    if (a.state != b.state) return a.state < b.state;
    return a.creation < b.creation;
}

bool replaces(double score, std::uint64_t creation, double kept_score, std::uint64_t kept_creation) {
    return score > kept_score || (score == kept_score && creation < kept_creation);
}

std::vector<std::size_t> merge(std::span<const Ranked> candidates) {
    StateTable table;
    table.reset(candidates.size());
    std::vector<char> kept(candidates.size(), 0);
    for (std::size_t i = 0; i < candidates.size(); ++i) {
        std::int32_t& slot = table[candidates[i].state];
        if (slot != kNone) {
            const Ranked& incumbent = candidates[static_cast<std::size_t>(slot)];
            if (!replaces(candidates[i].score_nats, candidates[i].creation, incumbent.score_nats, incumbent.creation))
                continue;
            kept[static_cast<std::size_t>(slot)] = 0;
        }
        slot = static_cast<std::int32_t>(i);
        kept[i] = 1;
    }
    std::vector<std::size_t> survivors;
    for (std::size_t i = 0; i < candidates.size(); ++i)
        if (kept[i]) survivors.push_back(i);
    return survivors;
}

Pruning prune(std::span<const Ranked> candidates, double beam_nats, int ceiling) {
    Pruning pruning;
    if (candidates.empty()) return pruning;
    std::vector<std::uint32_t> alive(candidates.size());
    for (std::size_t i = 0; i < alive.size(); ++i) alive[i] = static_cast<std::uint32_t>(i);
    std::vector<std::uint32_t> beam_dropped;
    std::vector<std::uint32_t> ceiling_dropped;
    prune_alive(candidates.data(), alive, beam_nats, static_cast<std::size_t>(std::max(ceiling, 0)), &beam_dropped,
                &ceiling_dropped);
    pruning.kept.assign(alive.begin(), alive.end());
    pruning.beam_dropped.assign(beam_dropped.begin(), beam_dropped.end());
    pruning.ceiling_dropped.assign(ceiling_dropped.begin(), ceiling_dropped.end());
    return pruning;
}

}  // namespace detail

struct Search::Impl {
    Impl(const ViterbiConfig& config, std::span<const double> c, std::shared_ptr<const FlatPrior> prior,
         detail::SearchOptions options);

    void step(std::complex<double> piece, double t, double n0);
    std::vector<DecodedSymbol> best_text() const;
    std::vector<StoryView> live_stories() const;
    const SearchStats& stats() const { return stats_; }
    std::size_t live() const { return population_.size(); }
    double normalization() const { return normalization_; }

private:
    // c(m), Hz, extended past its end by its last increment.
    double noise_hz(int m) const {
        const std::size_t top = c_.size() - 1;
        if (static_cast<std::size_t>(m) <= top) return c_[static_cast<std::size_t>(m)];
        return c_[top] + static_cast<double>(static_cast<std::size_t>(m) - top) * (c_[top] - c_[top - 1]);
    }
    // max over the labels the node allows of ln S_label(speed, tau): a mark at a known speed.
    double mark_survival(int speed, int node, int tau) const;
    // max over the kinds the node allows of the kind's cost and ln S: a gap.
    double gap_survival(int speed, int factor, int node, int tau);

    void continue_mark(const Story& s, Story& c, BlockScore& scored, std::complex<double> piece, double t, double n0);
    void end_mark(std::size_t i);
    void end_gap(std::size_t i);

    // Offers, made in creation order. A successor more than the beam behind a score known at this piece is skipped
    // where the options allow, except in the no-signal state. An offer replaces the candidate kept in its state as
    // detail::replaces decides: only with a better score, since the one kept was created earlier, so an offer that
    // loses costs a compare; its Origin is built only if it wins.
    bool beyond_beam(double score) const { return skip_ && known_best_ - score > beam_nats_; }
    std::uint32_t push_candidate(std::uint64_t state, double score, std::uint64_t creation, const Origin& origin);
    template <class MakeOrigin>
    void offer_general(std::uint64_t state, double score, std::int64_t line, MakeOrigin make_origin);
    template <class MakeOrigin>
    void offer_slot(Slot& slot, double score, std::int64_t line, MakeOrigin make_origin);
    void emit_slot(const Slot& slot, std::uint64_t state);
    void consider_rescue(double score, std::size_t parent, Move gap);

    Story build(std::uint32_t j, double t);
    std::int32_t letter_text(const Story& parent, bool word_space, double t);

    std::int64_t new_line(LineKind kind, double t);
    void removed(std::int64_t line, LineEnd step);
    void update_stats(double t, std::size_t ceiling_drops);

    // --- The configuration, read once ---
    MorseTree tree_;
    Fist fist_;
    std::shared_ptr<const FlatPrior> prior_;
    std::vector<double> c_;  // Hz
    OpenTiming open_;
    bool skip_;
    bool scan_open_factors_;
    std::int64_t* cut_factor_scans_;  // detail::SearchOptions' counters, or null
    std::int64_t* passed_factors_;
    int widest_factor_;               // the factor with the largest median: an open factor's survival
    bool stats_on_;
    double beam_nats_;
    std::size_t ceiling_;
    int speeds_;
    int factors_;
    int samples_per_piece_;
    int block_pieces_;        // B; INT_MAX for none (f_D = 0)
    int first_block_pieces_;  // min(B, the flat prior's max_pieces)
    double spread_hz_;        // f_D
    int order_;
    double log_stay_silent_;  // ln(1 - P)
    double log_over_start_;   // ln P
    double log_letter_;       // ln(1 / the tree's symbols)
    double log_keep_story_;   // ln(1 - epsilon)
    double log_restart_;      // ln epsilon
    double letter_gap_cost_;  // ln(1 / (1 - p))
    double word_gap_cost_;    // ln(1 / (p (1 - q)))
    double over_end_cost_;    // ln(1 / (p q))
    double log_speed_prior_;  // ln(1 / speed_count)
    double log_factor_prior_; // ln(1 / factor_count)
    int stats_window_pieces_;

    // --- The stories ---
    std::vector<Story> population_;
    std::uint32_t best_ = 0;  // the best story's index in population_
    SharedText text_;
    double normalization_ = 0.0;
    std::int64_t piece_ = 0;  // pieces searched
    double last_t_ = 0.0;
    std::uint64_t next_creation_ = 1;
    std::int64_t next_line_ = 1;

    // --- One piece's work, kept between pieces for its storage ---
    std::vector<Story> continuation_;  // [i]: population_[i] one piece on
    std::vector<BlockScore> scored_;   // [i]: the scoring call of population_[i] at this piece, if in a mark
    std::vector<char> transferred_;    // [i]: population_[i]'s hold on its text passed to its continuation
    std::vector<detail::Ranked> ranked_;
    std::vector<Origin> origins_;
    std::vector<char> dead_;
    std::vector<std::uint32_t> alive_;
    StateTable general_;
    std::vector<Slot> newborn_ends_;   // [label x speeds + speed]: gaps after newborns' first marks, factor open
    std::vector<Slot> letter_starts_;  // [speed x factors + factor]: marks at the root at a known speed and factor
    Slot no_signal_;
    Slot newborn_;
    double known_best_ = -kInf;  // nats: the best score offered so far at this piece
    struct Rescue {
        double score = -kInf;
        std::int32_t parent = kNone;
        Move gap = Move::EndLetterGap;
    } rescue_;
    std::vector<Story> next_population_;
    std::vector<std::uint32_t> beam_dropped_;
    std::vector<std::uint32_t> ceiling_dropped_;

    // --- The statistics ---
    SearchStats stats_;
    struct LineTrack {
        std::int64_t alive_piece = -1;    // the last piece after whose pruning it had a live story
        double best_score = -kInf;        // nats: its best live story's relative score at that piece
        LineEnd last_removal = LineEnd::Alive;
        bool left_first_element = false;  // one of its live stories was not in the newborn's first mark
    };
    std::vector<LineTrack> tracks_;
    std::vector<std::int64_t> active_lines_;  // alive after the last piece
    std::vector<std::pair<std::int64_t, double>> born_;  // this piece's newborns: line and score
    double window_sum_ = 0.0;
    int window_count_ = 0;
};

Search::Impl::Impl(const ViterbiConfig& config, std::span<const double> c, std::shared_ptr<const FlatPrior> prior,
                   detail::SearchOptions options)
    : tree_(),
      fist_(config),
      prior_(std::move(prior)),
      open_(fist_, tree_, config.speed_count, config.factor_count, open_timing_cap(config, options)),
      skip_(options.skip_successors_beyond_beam),
      scan_open_factors_(options.scan_open_factors),
      cut_factor_scans_(options.cut_factor_scans),
      passed_factors_(options.passed_factors),
      widest_factor_(config.factor_ratio >= 1.0 ? config.factor_count - 1 : 0),
      stats_on_(config.stats),
      beam_nats_(config.beam_nats),
      ceiling_(0),
      speeds_(config.speed_count),
      factors_(config.factor_count),
      samples_per_piece_(0),
      block_pieces_(0),
      first_block_pieces_(0),
      spread_hz_(config.genie_fading_spread_hz.value_or(config.fading_spread_hz)),
      order_(config.quadrature_order) {
    if (!prior_) throw std::invalid_argument("search: no flat prior");
    if (!(config.beam_nats > 0.0)) throw std::invalid_argument("search: beam_nats must be positive (nats)");
    if (config.ceiling < 1) throw std::invalid_argument("search: the ceiling must be at least 1 story");
    if (config.speed_count > 255 || config.factor_count > 255)
        throw std::invalid_argument("search: at most 255 speeds and 255 factors");
    if (tree_.size() > 16383) throw std::invalid_argument("search: the Morse tree has too many nodes");
    const double over_start = config.over_rate_per_s * config.piece_s;  // P, per piece
    const auto probability = [](double x) { return x > 0.0 && x < 1.0; };
    if (!probability(over_start))
        throw std::invalid_argument("search: over_rate_per_s x piece_s must lie in (0, 1) (a probability per piece)");
    if (!probability(config.rescue_epsilon)) throw std::invalid_argument("search: rescue_epsilon must lie in (0, 1)");
    if (!probability(config.word_end_p)) throw std::invalid_argument("search: word_end_p must lie in (0, 1)");
    if (!probability(config.over_end_q)) throw std::invalid_argument("search: over_end_q must lie in (0, 1)");
    if (config.quadrature_order < 2 || config.quadrature_order > 20)
        throw std::invalid_argument("search: the quadrature's order must be 2 to 20");
    const double block_s = block_length_s(spread_hz_, config.block_loss_db);
    if (std::isinf(block_s)) {
        block_pieces_ = std::numeric_limits<int>::max();
    } else {
        const double pieces = std::floor(block_s / config.piece_s);
        if (!(pieces >= 1.0)) throw std::invalid_argument("search: a block must hold at least one piece");
        block_pieces_ = pieces >= static_cast<double>(std::numeric_limits<int>::max())
                            ? std::numeric_limits<int>::max()
                            : static_cast<int>(pieces);
    }
    first_block_pieces_ = std::min(block_pieces_, prior_->max_pieces());
    samples_per_piece_ = prior_->samples_per_piece();
    const int longest =
        block_pieces_ == std::numeric_limits<int>::max() ? first_block_pieces_ : block_pieces_;  // pieces
    if (c.size() < 2 || c.size() <= static_cast<std::size_t>(samples_per_piece_) * static_cast<std::size_t>(longest))
        throw std::invalid_argument("search: c must reach m = samples_per_piece x the longest block (channel samples)");
    for (std::size_t m = 1; m < c.size(); ++m)
        if (!positive_and_finite(c[m])) throw std::invalid_argument("search: c(m) must be positive and finite (Hz)");
    c_.assign(c.begin(), c.end());
    ceiling_ = static_cast<std::size_t>(config.ceiling);

    int symbols = 0;
    for (std::size_t node = 0; node < tree_.size(); ++node)
        if (!tree_.symbol(static_cast<int>(node)).empty()) ++symbols;
    log_stay_silent_ = std::log1p(-over_start);
    log_over_start_ = std::log(over_start);
    log_letter_ = -std::log(static_cast<double>(symbols));
    log_keep_story_ = std::log1p(-config.rescue_epsilon);
    log_restart_ = std::log(config.rescue_epsilon);
    letter_gap_cost_ = -std::log1p(-config.word_end_p);
    word_gap_cost_ = -(std::log(config.word_end_p) + std::log1p(-config.over_end_q));
    over_end_cost_ = -(std::log(config.word_end_p) + std::log(config.over_end_q));
    log_speed_prior_ = -std::log(static_cast<double>(speeds_));
    log_factor_prior_ = -std::log(static_cast<double>(factors_));
    stats_window_pieces_ = std::max(1, static_cast<int>(std::lround(kStatsWindowS / config.piece_s)));

    newborn_ends_.assign(2 * static_cast<std::size_t>(speeds_), Slot{});
    letter_starts_.assign(static_cast<std::size_t>(speeds_) * static_cast<std::size_t>(factors_), Slot{});

    // At the channel's first piece there is one story: no signal, score 0, empty text, line 0.
    Story first;
    first.state = pack_state(Phase::NoSignal, 0, kOpen, kOpen, -1);
    population_.push_back(first);
    if (stats_on_) {
        stats_.lines.push_back(NewbornLine{});
        tracks_.push_back(LineTrack{});
        active_lines_.push_back(0);
    }
}

double Search::Impl::mark_survival(int speed, int node, int tau) const {
    double best = -kInf;
    for (int label = 0; label < 2; ++label)
        if (tree_.child(node, label == 1) >= 0)
            best = std::max(best, fist_.log_survival(OpenTiming::mark_kind(label), speed, 0, tau));
    return best;
}

double Search::Impl::gap_survival(int speed, int factor, int node, int tau) {
    double best = -kInf;
    if (tree_.has_children(node)) best = fist_.log_survival(Kind::ElementGap, speed, 0, tau);
    if (!tree_.symbol(node).empty()) {
        for (const Kind kind : {Kind::LetterGap, Kind::WordGap}) {
            const double cost = kind == Kind::LetterGap ? letter_gap_cost_ : word_gap_cost_;
            // An open factor survives as its widest does (search.hpp, "Work saved").
            const double survival = factor == kOpen
                                        ? log_factor_prior_ + fist_.log_survival(kind, speed, widest_factor_, tau)
                                        : fist_.log_survival(kind, speed, factor, tau);
            best = std::max(best, survival - cost);
        }
    }
    return best;
}

void Search::Impl::continue_mark(const Story& s, Story& c, BlockScore& scored, std::complex<double> piece, double t,
                                 double n0) {
    c.tau = s.tau + 1;
    c.block_sum = s.block_sum + piece;
    c.block_pieces = s.block_pieces + 1;
    const int m = samples_per_piece_ * c.block_pieces;  // channel samples in the block
    const double v = n0 * noise_hz(m);                  // FS^2
    const double s_abs = std::abs(c.block_sum);         // FS
    int limit = 0;
    if (s.level.learned) {
        scored = score_block(predict(s.level, t, spread_hz_), s_abs, m, v, order_, t,
                             s.block_pieces > 0 ? std::optional<double>(s.mode_a) : std::nullopt);
        limit = block_pieces_;
    } else {
        scored = score_first_block(*prior_, s_abs, c.block_pieces, v, t);
        limit = first_block_pieces_;
    }
    const double timing = s.speed == kOpen ? log_speed_prior_ + open_.first_mark_survival(c.tau)
                                           : mark_survival(s.speed, s.node, c.tau);
    c.score = s.base + scored.llr + timing;
    if (c.block_pieces == limit) {  // the block is full: finished, its evidence for good, its posterior the level
        c.base = s.base + scored.llr;
        c.level = scored.level;
        c.block_sum = 0.0;
        c.block_pieces = 0;
        c.mode_a = 0.0;
    } else {
        c.mode_a = scored.mode_a;
    }
}

std::uint32_t Search::Impl::push_candidate(std::uint64_t state, double score, std::uint64_t creation,
                                           const Origin& origin) {
    ranked_.push_back(detail::Ranked{state, score, creation});
    origins_.push_back(origin);
    dead_.push_back(0);
    return static_cast<std::uint32_t>(ranked_.size() - 1);
}

template <class MakeOrigin>
void Search::Impl::offer_general(std::uint64_t state, double score, std::int64_t line, MakeOrigin make_origin) {
    if (beyond_beam(score)) return;
    std::int32_t& kept = general_[state];
    if (kept != kNone) {
        const detail::Ranked& incumbent = ranked_[static_cast<std::size_t>(kept)];
        if (!detail::replaces(score, next_creation_, incumbent.score_nats, incumbent.creation)) {
            if (stats_on_) removed(line, LineEnd::Merge);
            return;
        }
        dead_[static_cast<std::size_t>(kept)] = 1;
        if (stats_on_) removed(origins_[static_cast<std::size_t>(kept)].line, LineEnd::Merge);
    }
    known_best_ = std::max(known_best_, score);
    kept = static_cast<std::int32_t>(push_candidate(state, score, next_creation_++, make_origin()));
}

template <class MakeOrigin>
void Search::Impl::offer_slot(Slot& slot, double score, std::int64_t line, MakeOrigin make_origin) {
    if (slot.piece == piece_) {
        if (!detail::replaces(score, next_creation_, slot.score, slot.creation)) {
            if (stats_on_) removed(line, LineEnd::Merge);
            return;
        }
        if (stats_on_) removed(slot.origin.line, LineEnd::Merge);
    }
    known_best_ = std::max(known_best_, score);
    slot.score = score;
    slot.creation = next_creation_++;
    slot.origin = make_origin();
    slot.piece = piece_;
}

void Search::Impl::emit_slot(const Slot& slot, std::uint64_t state) {
    if (slot.piece == piece_) push_candidate(state, slot.score, slot.creation, slot.origin);
}

void Search::Impl::consider_rescue(double score, std::size_t parent, Move gap) {
    if (score > rescue_.score) rescue_ = Rescue{score, static_cast<std::int32_t>(parent), gap};
}

void Search::Impl::end_mark(std::size_t i) {
    const Story& s = population_[i];
    const int tau = continuation_[i].tau;
    const double with_block = s.base + scored_[i].llr;  // the block finished: its evidence for good
    if (s.speed == kOpen) {
        // A newborn's first mark, at the root: it branches into every speed and label with a finite ln P, each paying
        // ln(1/speed_count). Every such successor lands in a state (a gap after the first mark, factor open) that only
        // newborns' first marks reach, so they merge in newborn_ends_. The over may end after E or T: all speeds land
        // in the no-signal state, so only each label's best speed is offered, and always (search.hpp, step 5).
        const OpenTiming::FirstMarkEnds ends = open_.first_mark_ends(tau);
        const double head = with_block + log_speed_prior_;
        const bool gaps_beyond = beyond_beam(head + ends.best);  // then every gap successor is beyond the beam
        for (int label = 0; label < 2; ++label) {
            const int child = tree_.child(tree_.root(), label == 1);
            if (child < 0) continue;
            for (int speed = 0; speed < speeds_ && !gaps_beyond; ++speed) {
                const double log_end = ends.log_end[static_cast<std::size_t>(label * speeds_ + speed)];
                if (log_end == -kInf) continue;
                const double score = head + log_end;
                if (beyond_beam(score)) continue;
                offer_slot(newborn_ends_[static_cast<std::size_t>(label * speeds_ + speed)], score, s.line, [&] {
                    return Origin{static_cast<std::int32_t>(i), Move::EndMark, static_cast<std::uint8_t>(label),
                                  Move::Continue, static_cast<std::int16_t>(speed), kOpen, s.line};
                });
            }
            if (!tree_.symbol(child).empty() && ends.label_best[label] > -kInf)
                offer_slot(no_signal_, head + ends.label_best[label] - over_end_cost_, s.line, [&] {
                    return Origin{static_cast<std::int32_t>(i), Move::EndOver, static_cast<std::uint8_t>(label),
                                  Move::Continue, kOpen, kOpen, s.line};
                });
        }
        return;
    }
    for (int label = 0; label < 2; ++label) {
        const int child = tree_.child(s.node, label == 1);
        if (child < 0) continue;
        const double log_end = fist_.log_end(OpenTiming::mark_kind(label), s.speed, 0, tau);
        if (log_end == -kInf) continue;
        const double score = with_block + log_end;
        offer_general(pack_state(Phase::Gap, 0, s.speed, s.factor, child), score, s.line, [&] {
            return Origin{static_cast<std::int32_t>(i), Move::EndMark, static_cast<std::uint8_t>(label), Move::Continue,
                          s.speed, s.factor, s.line};
        });
        if (!tree_.symbol(child).empty())  // the over's end: always offered (search.hpp, step 5)
            offer_slot(no_signal_, score - over_end_cost_, s.line, [&] {
                return Origin{static_cast<std::int32_t>(i), Move::EndOver, static_cast<std::uint8_t>(label),
                              Move::Continue, kOpen, kOpen, s.line};
            });
    }
}

void Search::Impl::end_gap(std::size_t i) {
    const Story& s = population_[i];
    const int tau = continuation_[i].tau;
    const int node = s.node;
    if (tree_.has_children(node)) {
        const double log_end = fist_.log_end(Kind::ElementGap, s.speed, 0, tau);
        if (log_end > -kInf)
            offer_general(pack_state(Phase::Mark, 0, s.speed, s.factor, node), s.base + log_end, s.line, [&] {
                return Origin{static_cast<std::int32_t>(i), Move::EndElementGap, 0, Move::Continue, s.speed, s.factor,
                              s.line};
            });
    }
    if (tree_.symbol(node).empty()) return;
    // A letter starts at the root: it pays the letter, ln(1/62), and ln(1/(1 - epsilon)) for not restarting. Every such
    // successor lands in a state (a mark at the root at a known speed and factor) that only letter starts reach, so
    // they merge in letter_starts_.
    const double head = s.base + log_letter_ + log_keep_story_;
    Slot* const starts = letter_starts_.data() + static_cast<std::size_t>(s.speed) * static_cast<std::size_t>(factors_);
    for (const Kind kind : {Kind::LetterGap, Kind::WordGap}) {
        const bool word = kind == Kind::WordGap;
        const Move move = word ? Move::EndWordGap : Move::EndLetterGap;
        const double top = head - (word ? word_gap_cost_ : letter_gap_cost_);
        if (s.factor != kOpen) {
            const double log_end = fist_.log_end(kind, s.speed, s.factor, tau);
            if (log_end == -kInf) continue;
            const double score = top + log_end;
            consider_rescue(score, i, move);
            if (beyond_beam(score)) continue;
            offer_slot(starts[s.factor], score, s.line, [&] {
                return Origin{static_cast<std::int32_t>(i), move, 0, Move::Continue, s.speed, s.factor, s.line};
            });
            continue;
        }
        // The factor is taken here: one successor per factor, each paying ln(1/factor_count).
        const double opened = top + log_factor_prior_;
        // Offers the successor at factor f, whose ln P is the finite log_end; false where the beam would drop it.
        const auto offer_factor = [&](int f, double log_end) {
            const double score = opened + log_end;
            if (beyond_beam(score)) return false;
            offer_slot(starts[f], score, s.line, [&] {
                return Origin{static_cast<std::int32_t>(i), move, 0, Move::Continue, s.speed,
                              static_cast<std::int16_t>(f), s.line};
            });
            return true;
        };
        // Every factor in order, each considered for the rescue: for an entry that is not unimodal, and for every gap
        // where the options turn the scan off (no table is read then).
        const auto offer_every_factor = [&] {
            for (int f = 0; f < factors_; ++f) {
                const double log_end = fist_.log_end(kind, s.speed, f, tau);
                if (log_end == -kInf) continue;
                consider_rescue(opened + log_end, i, move);
                offer_factor(f, log_end);
            }
        };
        if (!scan_open_factors_) {
            offer_every_factor();
            continue;
        }
        const OpenTiming::FactorEntry& entry = open_.factor_entry(kind, s.speed, tau);
        if (entry.end == -kInf) continue;
        consider_rescue(opened + entry.end, i, move);
        if (beyond_beam(opened + entry.end)) continue;
        if (!entry.unimodal) {
            offer_every_factor();
            continue;
        }
        // Outward from the best factor. On each side ln P only falls, so the last value computed bounds every one
        // further out: a factor whose bound cannot beat the score its slot keeps would lose there, and is passed over
        // without computing its ln P; the side ends at the first factor not created, or once the bound itself is
        // beyond the beam, since the beam's threshold only rises (known_best_ only grows) and no further factor on that
        // side would be created.
        const auto scan = [&](int f, int direction) {
            double bound = entry.end;
            for (; f >= 0 && f < factors_; f += direction) {
                if (beyond_beam(opened + bound)) return f;
                const Slot& slot = starts[f];
                if (slot.piece == piece_ &&
                    !detail::replaces(opened + bound, next_creation_, slot.score, slot.creation)) {
                    if (passed_factors_) ++*passed_factors_;
                    continue;
                }
                const double log_end = fist_.log_end(kind, s.speed, f, tau);
                if (log_end == -kInf || !offer_factor(f, log_end)) return f;
                bound = log_end;
            }
            return f;
        };
        const int low = scan(entry.end_factor, -1);
        const int high = scan(entry.end_factor + 1, +1);
        if (cut_factor_scans_ && (low >= 0 || high < factors_)) ++*cut_factor_scans_;
    }
}

std::int64_t Search::Impl::new_line(LineKind kind, double t) {
    const std::int64_t line = next_line_++;
    if (stats_on_) {
        NewbornLine record;
        record.birth_s = t;
        record.kind = kind;
        stats_.lines.push_back(record);
        tracks_.push_back(LineTrack{});
    }
    return line;
}

void Search::Impl::removed(std::int64_t line, LineEnd step) {
    if (stats_on_) tracks_[static_cast<std::size_t>(line)].last_removal = step;
}

std::int32_t Search::Impl::letter_text(const Story& parent, bool word_space, double t) {
    // The letter: from the start of its first mark to the end of its last; a word space: across the gap.
    const std::int32_t letter =
        text_.append(parent.text, tree_.symbol(parent.node), parent.letter_start_s, parent.last_mark_end_s);
    if (!word_space) return letter;
    const std::int32_t space = text_.append(letter, kWordSpace, parent.last_mark_end_s, t);
    text_.release(letter);  // the space holds it
    return space;
}

Story Search::Impl::build(std::uint32_t j, double t) {
    const Origin& o = origins_[j];
    const detail::Ranked& r = ranked_[j];
    const std::size_t parent_index = static_cast<std::size_t>(o.parent);
    if (o.move == Move::Continue) {
        transferred_[parent_index] = 1;  // the continuation takes over its parent's hold on the text
        return continuation_[parent_index];
    }
    const Story& parent = population_[parent_index];
    Story s;
    s.state = r.state;
    s.score = r.score_nats;
    s.base = r.score_nats;
    s.creation = r.creation;
    s.line = o.line;
    s.tau = 0;
    switch (o.move) {
        case Move::EndMark:
            s.phase = Phase::Gap;
            s.speed = o.speed;
            s.factor = parent.factor;
            s.node = static_cast<std::int16_t>(tree_.child(parent.node, o.label == 1));
            s.level = scored_[parent_index].level;
            text_.hold(parent.text);
            s.text = parent.text;
            s.letter_start_s = parent.letter_start_s;
            s.last_mark_end_s = t;
            break;
        case Move::EndOver: {
            s.phase = Phase::NoSignal;
            const int child = tree_.child(parent.node, o.label == 1);
            s.text = text_.append(parent.text, tree_.symbol(child), parent.letter_start_s, t);
            s.letter_start_s = t;
            s.last_mark_end_s = t;
            break;
        }
        case Move::EndElementGap:
            s.phase = Phase::Mark;
            s.speed = parent.speed;
            s.factor = parent.factor;
            s.node = parent.node;
            s.level = parent.level;
            text_.hold(parent.text);
            s.text = parent.text;
            s.letter_start_s = parent.letter_start_s;
            s.last_mark_end_s = parent.last_mark_end_s;
            break;
        case Move::EndLetterGap:
        case Move::EndWordGap:
            s.phase = Phase::Mark;
            s.speed = parent.speed;
            s.factor = o.factor;
            s.node = static_cast<std::int16_t>(tree_.root());
            s.level = parent.level;
            s.text = letter_text(parent, o.move == Move::EndWordGap, t);
            s.letter_start_s = t;
            s.last_mark_end_s = parent.last_mark_end_s;
            break;
        case Move::BirthFromSilence:
            s.phase = Phase::Mark;
            s.node = static_cast<std::int16_t>(tree_.root());
            // A new over after silence begins with a word space if the text is not empty.
            s.text = parent.text == SharedText::kEmpty ? SharedText::kEmpty
                                                       : text_.append(parent.text, kWordSpace, t, t);
            s.letter_start_s = t;
            s.last_mark_end_s = t;
            break;
        case Move::BirthRescue:
            s.phase = Phase::Mark;
            s.node = static_cast<std::int16_t>(tree_.root());
            s.text = letter_text(parent, o.rescued_gap == Move::EndWordGap, t);
            s.letter_start_s = t;
            s.last_mark_end_s = t;
            break;
        case Move::Continue:
            break;
    }
    return s;
}

void Search::Impl::step(std::complex<double> piece, double t, double n0) {
    if (!positive_and_finite(n0))
        throw std::invalid_argument("search: n0_fs2_per_hz must be positive and finite (FS^2/Hz)");
    if (!std::isfinite(t) || (piece_ > 0 && !(t > last_t_)))
        throw std::invalid_argument("search: t_s must be finite and later than the previous piece's (s)");
    if (!std::isfinite(piece.real()) || !std::isfinite(piece.imag()))
        throw std::invalid_argument("search: piece_fs must be finite (FS)");
    if (stats_on_ && piece_ == 0) stats_.lines[0].birth_s = t;

    const std::size_t n_old = population_.size();
    continuation_.resize(n_old);
    scored_.resize(n_old);
    transferred_.assign(n_old, 0);
    ranked_.clear();
    origins_.clear();
    dead_.clear();
    general_.reset(2 * n_old);
    known_best_ = -kInf;
    rescue_ = Rescue{};
    born_.clear();
    std::int32_t no_signal = kNone;

    // 2. Every story continues: a mark makes its one scoring call of the piece; a gap's piece adds 0 and its survival
    // changes; no signal pays ln(1 - P). The continuations are candidates in the previous population's order, the
    // no-signal story through its slot, where the over's ends of this piece merge with it.
    for (std::size_t i = 0; i < n_old; ++i) {
        const Story& s = population_[i];
        Story& c = continuation_[i];
        c = s;
        switch (s.phase) {
            case Phase::NoSignal:
                c.score = s.score + log_stay_silent_;
                no_signal = static_cast<std::int32_t>(i);
                break;
            case Phase::Mark:
                continue_mark(s, c, scored_[i], piece, t, n0);
                break;
            case Phase::Gap:
                c.tau = s.tau + 1;
                c.score = s.base + gap_survival(s.speed, s.factor, s.node, c.tau);
                break;
        }
        c.state = pack_state(c.phase, c.tau, c.speed, c.factor, c.node);
        known_best_ = std::max(known_best_, c.score);
        if (s.phase != Phase::NoSignal)
            push_candidate(c.state, c.score, c.creation,
                           Origin{static_cast<std::int32_t>(i), Move::Continue, 0, Move::Continue, c.speed, c.factor,
                                  c.line});
    }
    if (no_signal != kNone) {  // the first in its slot, with its own creation order
        const Story& c = continuation_[static_cast<std::size_t>(no_signal)];
        no_signal_ = Slot{c.score, c.creation,
                          Origin{no_signal, Move::Continue, 0, Move::Continue, kOpen, kOpen, c.line}, piece_};
    }
    // ... and ends, into successors that start their next element at the next piece.
    for (std::size_t i = 0; i < n_old; ++i) {
        if (population_[i].phase == Phase::Mark)
            end_mark(i);
        else if (population_[i].phase == Phase::Gap)
            end_gap(i);
    }

    // 3. Births: from silence, off the continuing no-signal story (silent at this piece); and one rescue newborn from
    // the best letter-starting successor. Both are marks at the root with speed and factor open: they merge.
    if (no_signal != kNone) {
        const Story& c = continuation_[static_cast<std::size_t>(no_signal)];
        const double score = c.score + log_over_start_ + log_letter_;
        const std::int64_t line = new_line(LineKind::Silence, t);
        born_.emplace_back(line, score);
        offer_slot(newborn_, score, line,
                   [&] { return Origin{no_signal, Move::BirthFromSilence, 0, Move::Continue, kOpen, kOpen, line}; });
    }
    if (rescue_.parent != kNone) {
        const double score = rescue_.score - log_keep_story_ + log_restart_;
        const std::int64_t line = new_line(LineKind::Rescue, t);
        born_.emplace_back(line, score);
        offer_slot(newborn_, score, line,
                   [&] { return Origin{rescue_.parent, Move::BirthRescue, 0, rescue_.gap, kOpen, kOpen, line}; });
    }

    // 4. Merging is done as successors are offered; the slots' winners join the candidates in the slots' order.
    const int root = tree_.root();
    for (int label = 0; label < 2; ++label)
        for (int speed = 0; speed < speeds_; ++speed)
            emit_slot(newborn_ends_[static_cast<std::size_t>(label * speeds_ + speed)],
                      pack_state(Phase::Gap, 0, speed, kOpen, tree_.child(root, label == 1)));
    for (int speed = 0; speed < speeds_; ++speed)
        for (int factor = 0; factor < factors_; ++factor)
            emit_slot(letter_starts_[static_cast<std::size_t>(speed) * static_cast<std::size_t>(factors_) +
                                     static_cast<std::size_t>(factor)],
                      pack_state(Phase::Mark, 0, speed, factor, root));
    emit_slot(no_signal_, pack_state(Phase::NoSignal, 0, kOpen, kOpen, -1));
    emit_slot(newborn_, pack_state(Phase::Mark, 0, kOpen, kOpen, root));
    alive_.clear();
    for (std::size_t j = 0; j < ranked_.size(); ++j)
        if (!dead_[j]) alive_.push_back(static_cast<std::uint32_t>(j));

    // 5. Pruning: the beam, then the ceiling.
    beam_dropped_.clear();
    ceiling_dropped_.clear();
    const std::uint32_t best = prune_alive(ranked_.data(), alive_, beam_nats_, ceiling_,
                                           stats_on_ ? &beam_dropped_ : nullptr,
                                           stats_on_ ? &ceiling_dropped_ : nullptr);
    const double best_score = ranked_[best].score_nats;
    if (!std::isfinite(best_score)) throw std::logic_error("search: the best score is not finite");
    if (stats_on_) {
        for (const std::uint32_t j : beam_dropped_) removed(origins_[j].line, LineEnd::Beam);
        for (const std::uint32_t j : ceiling_dropped_) removed(origins_[j].line, LineEnd::Ceiling);
        for (const auto& [line, score] : born_)
            stats_.lines[static_cast<std::size_t>(line)].largest_deficit_nats = best_score - score;
    }

    // 6. The new population, scores relative to the best. Its texts are held before the old population's holds go.
    next_population_.clear();
    for (const std::uint32_t j : alive_) {
        if (j == best) best_ = static_cast<std::uint32_t>(next_population_.size());
        next_population_.push_back(build(j, t));
        Story& s = next_population_.back();
        s.score -= best_score;
        s.base -= best_score;
    }
    for (std::size_t i = 0; i < n_old; ++i)
        if (!transferred_[i]) text_.release(population_[i].text);
    population_.swap(next_population_);
    normalization_ += best_score;
    last_t_ = t;

    if (stats_on_) update_stats(t, ceiling_dropped_.size());
    ++piece_;
}

void Search::Impl::update_stats(double t, std::size_t ceiling_drops) {
    ++stats_.pieces;
    if (ceiling_drops > 0) ++stats_.ceiling_pieces;
    const std::size_t live = population_.size();
    const std::size_t bin = live / 10;
    if (stats_.live_histogram.size() <= bin) stats_.live_histogram.resize(bin + 1, 0);
    ++stats_.live_histogram[bin];
    if (window_count_ == stats_window_pieces_ || stats_.live_mean_100ms.empty()) {
        stats_.live_mean_100ms.push_back(0.0f);
        stats_.live_max_100ms.push_back(0.0f);
        window_sum_ = 0.0;
        window_count_ = 0;
    }
    window_sum_ += static_cast<double>(live);
    ++window_count_;
    stats_.live_mean_100ms.back() = static_cast<float>(window_sum_ / window_count_);
    stats_.live_max_100ms.back() = std::max(stats_.live_max_100ms.back(), static_cast<float>(live));

    // The lines: which have live stories, their best, and whether one has left the newborn's first mark.
    for (const Story& s : population_) {
        LineTrack& track = tracks_[static_cast<std::size_t>(s.line)];
        if (track.alive_piece != piece_) {
            track.alive_piece = piece_;
            track.best_score = s.score;
        } else {
            track.best_score = std::max(track.best_score, s.score);
        }
        if (!(s.phase == Phase::Mark && s.speed == kOpen)) track.left_first_element = true;
    }
    for (const auto& born : born_) active_lines_.push_back(born.first);
    std::size_t still = 0;
    for (const std::int64_t line : active_lines_) {
        LineTrack& track = tracks_[static_cast<std::size_t>(line)];
        NewbornLine& record = stats_.lines[static_cast<std::size_t>(line)];
        if (track.alive_piece == piece_) {
            if (!record.first_led_s)
                record.largest_deficit_nats = std::max(record.largest_deficit_nats, -track.best_score);
            active_lines_[still++] = line;
        } else {
            record.end = track.last_removal;
            record.end_s = t;
            record.ended_in_first_element = record.kind != LineKind::Start && !track.left_first_element;
        }
    }
    active_lines_.resize(still);
    NewbornLine& leader = stats_.lines[static_cast<std::size_t>(population_[best_].line)];
    if (!leader.first_led_s) leader.first_led_s = t;
}

std::vector<DecodedSymbol> Search::Impl::best_text() const {
    const Story& best = population_[best_];
    std::vector<DecodedSymbol> text = text_.read(best.text);
    // A story at a node holding a symbol: that symbol is its last letter, from its first mark's start to its last
    // finished mark's end.
    if (best.phase != Phase::NoSignal) {
        const std::string_view symbol = tree_.symbol(best.node);
        if (!symbol.empty())
            text.push_back(DecodedSymbol{std::string(symbol), 1.0f, best.letter_start_s, best.last_mark_end_s});
    }
    return text;
}

std::vector<StoryView> Search::Impl::live_stories() const {
    std::vector<const Story*> order;
    order.reserve(population_.size());
    for (const Story& s : population_) order.push_back(&s);
    std::sort(order.begin(), order.end(), [](const Story* a, const Story* b) { return a->state < b->state; });
    std::vector<StoryView> views;
    views.reserve(order.size());
    for (const Story* s : order) {
        const bool morse = s->phase != Phase::NoSignal;
        views.push_back(StoryView{morse ? Activity::Morse : Activity::NoSignal, s->phase == Phase::Mark, s->tau,
                                  s->speed, s->factor, morse ? s->node : -1, s->score, s->line, s->level});
    }
    return views;
}

Search::Search(const ViterbiConfig& config, std::span<const double> c, std::shared_ptr<const FlatPrior> prior,
               detail::SearchOptions options)
    : impl_(std::make_unique<Impl>(config, c, std::move(prior), options)) {}
Search::~Search() = default;
Search::Search(Search&&) noexcept = default;
Search& Search::operator=(Search&&) noexcept = default;

void Search::step(std::complex<double> piece_fs, double t_s, double n0_fs2_per_hz) {
    impl_->step(piece_fs, t_s, n0_fs2_per_hz);
}
std::vector<DecodedSymbol> Search::best_text() const { return impl_->best_text(); }
const SearchStats& Search::stats() const { return impl_->stats(); }
std::size_t Search::live() const { return impl_->live(); }
std::vector<StoryView> Search::live_stories() const { return impl_->live_stories(); }
double Search::normalization_nats() const { return impl_->normalization(); }

}  // namespace kz4ap::viterbi
