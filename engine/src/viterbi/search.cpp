#include "kz4ap/viterbi/search.hpp"

#include "kz4ap/viterbi/fist.hpp"
#include "kz4ap/viterbi/long_run.hpp"
#include "kz4ap/viterbi/mark_template.hpp"
#include "kz4ap/viterbi/morse_tree.hpp"
#include "kz4ap/viterbi/newborn.hpp"
#include "kz4ap/viterbi/observation.hpp"
#include "kz4ap/viterbi/symbol_prior.hpp"
#include "kz4ap/viterbi/systematic.hpp"
#include "kz4ap/viterbi_decoder.hpp"
#include "viterbi/text.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numbers>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace kz4ap::viterbi {
namespace {

constexpr double kInf = std::numeric_limits<double>::infinity();
constexpr int kOpen = -1;                    // a speed not yet taken
constexpr std::int32_t kNone = -1;           // no candidate
constexpr std::string_view kWordSpace = " ";
constexpr double kStatsWindowS = 0.1;        // s: the window of the live count's series
// The newborn's first mark: it must switch at the latest at this many samples after its first claimed key-up (j = 15,
// the last sample of the first mark's template with s != 0; owner, 2026-10-25; part 8).
constexpr int kLastScoredSampleAfterKeyUp = TemplateTables::kSpan - 1;

// What a story is doing. Its order is the first part of the state's order.
enum class Phase : std::uint8_t { NoSignal = 0, Mark = 1, Gap = 2 };

// The state (plan §2.7, item 4) packed into 64 bits, the phase (activity and key) in the top 2, tau in the next 32,
// then speed + 1 in 8 bits (open = 0) and node + 1 in 14. The packed order is one fixed order of the states. The
// no-signal state has no speed or node; its tau counts the samples since an over's end while the over's tail still
// scores (1 ... 17), and is 0 once the tail is over, the quiet state, 0 as a whole.
std::uint64_t pack_state(Phase phase, int tau, int speed, int node) {
    return (static_cast<std::uint64_t>(phase) << 62) |
           (static_cast<std::uint64_t>(static_cast<std::uint32_t>(tau)) << 30) |
           (static_cast<std::uint64_t>(speed + 1) << 22) | static_cast<std::uint64_t>(node + 1);
}

// A successor's merge key adds to its state (search.hpp, step 4), in bits its state leaves free: a successor's tau is 0 or
// 1, so tau's bits from bit 40 up hold its recent edges (bit 40 + j: an edge j samples back); bits 14 ... 21 lie between
// the node and the speed, and one of them marks a rescue.
constexpr int kEdgeKeyShift = 40;
constexpr std::uint64_t kRescueKeyBit = std::uint64_t{1} << 16;

// What became of a story's parent to make it.
enum class Move : std::uint8_t {
    Continue,          // the parent, one sample on
    EndMark,           // the parent's mark ended as a dot or a dash: a gap at the child node
    EndOver,           // the parent's mark ended and with it the over: no signal
    EndElementGap,     // the parent's gap ended between elements: a mark at the same node
    EndLetterGap,      // between letters: a letter starts at the root
    EndWordGap,        // between words: a word space, and a letter starts at the root
    BirthFromSilence,  // a newborn from the no-signal story
    BirthRescue,       // a rescue newborn, after its parent's gap ended between letters or words
};

// One story (search.hpp). Copied from a prototype, never default-made, so that its long-run sums carry the search's
// spread, bin and memory.
struct Story {
    explicit Story(const LongRun& sums) : long_run(sums) {}

    Phase phase = Phase::NoSignal;
    std::int32_t tau = 0;        // samples in the current element; no signal: since an over's end, 0 once quiet
    std::int16_t speed = kOpen;  // 0 ... speed_count - 1, or open
    std::int16_t node = -1;      // a mark: the node it started from; a gap: the node of the mark before it
    std::uint64_t state = 0;     // the packed state; at its creation sample, a successor's merge key
    double score = 0.0;          // nats, relative to the best
    double base = 0.0;           // nats: the score at the element's start plus the scores of its samples since
    EdgeHistory edges;           // its claimed edges of the last 16 samples
    bool edge_next = false;      // it claims an edge, its element's start, at the next sample
    // A story in the newborn integral: samples since its first mark's claimed key-up (0 at that sample), -1 before it.
    std::int16_t key_up_age = -1;
    Amplitude amp{0.0, 0.0, 0};  // mu (FS), P (FS^2) and t_m, as updated (zeros before the newborn switches)
    double mode_a = 0.0;         // FS: the last scored sample's mode, Newton's start at the next
    double omega_bar = 0.0;      // FS^2: Omega_bar, looked up after each scored sample once out of the integral
    LongRun long_run;            // Omega_hat's and nu's sums since the over's start or the rescue
    std::shared_ptr<NewbornIntegral> newborn;  // the newborn integral while it lasts (shared until written)
    std::int32_t text = SharedText::kEmpty;
    double letter_start_s = 0.0;   // s: the current letter's first claimed key-down
    double last_mark_end_s = 0.0;  // s: the last finished mark's claimed key-up
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
    std::int64_t line = 0;
};

// One successor kind's best offer at a sample (a merge of everything offered into one state).
struct Slot {
    double score = -kInf;
    std::uint64_t creation = 0;
    Origin origin;
    std::int64_t sample = -1;  // the sample at which it was last filled; empty at any other
};

// A table from a state to the index of the candidate kept in it at the current sample: open addressing with linear
// probing, kept at most half full, emptied for each sample by a generation count. It is only searched, never walked.
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

// The fist's maxima that an open speed needs (plan §2.5, items 7 and 8): a newborn's first mark, at the root, keeps the
// best over the speeds and both labels, each the plain maximum of Fist's own values. They depend only on the
// configuration: those of the first `cap` taus are kept in tables, filled as first asked for, and those of any later
// tau are computed again whenever asked, in the same way, so that no table ever holds more than `cap` taus (search.hpp,
// "Work saved", has the bound).
class OpenTiming {
public:
    // ln P_label(s, tau) of a newborn's first mark for both labels and every speed (label-major; -infinity for a label
    // the root does not allow), and its maxima. The pointer holds until the next call.
    struct FirstMarkEnds {
        const double* log_end;     // [label x speeds + speed]
        double label_best[2];      // the best over the speeds, per label
        double best;               // the best of all
    };

    // The tables' bytes per tau with `speeds` speeds: a first mark's row of ends with its three maxima and its
    // survival.
    static std::size_t bytes_per_tau(int speeds) {
        const std::size_t row = 2 * static_cast<std::size_t>(speeds);
        return row * sizeof(double) + sizeof(std::array<double, 3>) + sizeof(double);
    }

    OpenTiming(const Fist& fist, const MorseTree& tree, int speeds, int cap) : fist_(fist), speeds_(speeds), cap_(cap) {
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

    // bytes: what the tables' entries hold, their vectors' capacities (search.hpp, "Work saved": at most 4 000 000).
    std::size_t bytes() const {
        return first_survival_.capacity() * sizeof(double) + first_ends_.capacity() * sizeof(double) +
               first_best_.capacity() * sizeof(std::array<double, 3>);
    }

    static Kind mark_kind(int label) { return label == 1 ? Kind::Dash : Kind::Dot; }

private:
    double first_survival_at(int t) const {
        double best = -kInf;
        for (int label = 0; label < 2; ++label)
            if (root_has_[label])
                for (int s = 0; s < speeds_; ++s) best = std::max(best, fist_.log_survival(mark_kind(label), s, t));
        return best;
    }

    // Fills `row` (2 x speeds values) with the first mark's ln P at tau t and returns the maxima: per label, of all.
    std::array<double, 3> first_ends_at(int t, double* row) const {
        std::array<double, 3> best = {-kInf, -kInf, -kInf};
        for (int label = 0; label < 2; ++label) {
            for (int s = 0; s < speeds_; ++s) {
                const double value = root_has_[label] ? fist_.log_end(mark_kind(label), s, t) : -kInf;
                row[static_cast<std::size_t>(label * speeds_ + s)] = value;
                best[static_cast<std::size_t>(label)] = std::max(best[static_cast<std::size_t>(label)], value);
            }
        }
        best[2] = std::max(best[0], best[1]);
        return best;
    }

    const Fist& fist_;
    int speeds_;
    int cap_;  // samples: the taus kept
    bool root_has_[2] = {false, false};
    std::vector<double> first_survival_;             // by tau - 1
    std::vector<double> first_ends_;                 // by tau - 1, rows of 2 x speeds
    std::vector<std::array<double, 3>> first_best_;  // by tau - 1
    std::vector<double> scratch_ends_;               // a first mark's row beyond the cap
};

// bytes: the open timing's budget per Search, which sets its tables' cap (search.hpp, "Work saved").
constexpr std::size_t kOpenTimingBudgetBytes = 4'000'000;

// The open timing's cap, samples: the options' where set (positive), else the most taus the budget holds.
int open_timing_cap(const ViterbiConfig& config, const detail::SearchOptions& options) {
    if (options.open_timing_cap_samples > 0) return options.open_timing_cap_samples;
    const std::size_t taus = kOpenTimingBudgetBytes / OpenTiming::bytes_per_tau(std::max(config.speed_count, 1));
    const std::size_t most = static_cast<std::size_t>(std::numeric_limits<int>::max());
    return static_cast<int>(std::clamp<std::size_t>(taus, 1, most));
}

bool positive_and_finite(double x) { return x > 0.0 && std::isfinite(x); }

// The shared parts after checking them.
std::shared_ptr<const ViterbiShared> checked_shared(std::shared_ptr<const ViterbiShared> shared) {
    if (!shared) throw std::invalid_argument("search: no shared parts");
    return shared;
}

// The spread f_D (Hz): the genie's where set (search.hpp, point 4).
double spread_of(const ViterbiConfig& config) {
    return config.genie_fading_spread_hz.value_or(config.fading_spread_hz);
}

// The long-run sums' prototype, after converting nu's bin and memory to samples and bins (whole numbers, else the
// field is named).
LongRun long_run_of(const ViterbiConfig& config) {
    const int bin = whole_count(config.nu_bin_s, config.sample_interval_s, "nu_bin_s", "samples");
    (void)whole_count(config.nu_memory_s, config.sample_interval_s, "nu_memory_s", "samples");
    const int bins = whole_count(config.nu_memory_s, config.nu_bin_s, "nu_memory_s", "bins (nu_bin_s)");
    return LongRun(spread_of(config), bin, bins, config.sample_interval_s);
}

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
    Impl(const ViterbiConfig& config, std::shared_ptr<const ViterbiShared> shared, detail::SearchOptions options);

    void step(double e, double t, double n0);
    void finish();
    std::vector<DecodedSymbol> best_text() const;
    std::vector<StoryView> live_stories() const;
    const SearchStats& stats() const { return stats_; }
    std::size_t live() const { return live_; }
    double normalization() const { return normalization_; }
    std::size_t text_entries() const { return text_.size(); }
    std::size_t open_timing_bytes() const { return open_.bytes(); }
    std::size_t newborn_integrals() const;
    std::size_t newborn_integral_bytes() const;

private:
    // A sample given.
    struct Pending {
        double e;   // FS
        double t;   // s
        double n0;  // FS^2/Hz
        std::int64_t k;
    };
    // One sample (search.hpp, "One sample searched, in this order").
    void search_sample(const Pending& sample);
    // The sample's evidence for one story (search.hpp, "One sample k searched", steps 1 and 2): claims its edge,
    // reads its template, scores the sample and updates its amplitude, sums and Omega_bar. Returns the score, nats.
    double score_sample(Story& c, const Pending& sample, double sigma2);
    double omega_bar_of(const Story& s, double sigma2) const {
        return shared_->omega_bar().omega_bar_fs2(s.long_run.nu(), s.long_run.omega_hat_fs2(), sigma2);
    }
    // max over the labels the node allows of ln S_label(speed, tau): a mark at a known speed.
    double mark_survival(int speed, int node, int tau) const;
    // max over the kinds the node allows of the kind's cost and ln S: a gap.
    double gap_survival(int speed, int node, int tau) const;
    // The edges a successor's merge key holds: those of the last merge_edge_history_samples samples.
    std::uint64_t edge_key(const EdgeHistory& edges) const {
        return static_cast<std::uint64_t>(edges.recent & merge_edge_mask_) << kEdgeKeyShift;
    }

    void end_mark(std::size_t i);
    void end_gap(std::size_t i);

    // Offers, made in creation order. A successor more than the beam behind a score known at this sample is skipped
    // where the options allow, except in the no-signal state. An offer replaces the candidate kept in its state as
    // detail::replaces decides: only with a better score, since the one kept was created earlier, so an offer that
    // loses costs a compare; its Origin is built only if it wins.
    bool beyond_beam(double score) const {
        const bool beyond = skip_ && known_best_ - score > beam_nats_;
        if (beyond && skipped_successors_) ++*skipped_successors_;
        return beyond;
    }
    std::uint32_t push_candidate(std::uint64_t state, double score, std::uint64_t creation, const Origin& origin);
    template <class MakeOrigin>
    void offer_general(std::uint64_t state, double score, std::int64_t line, MakeOrigin make_origin);
    // The same, never skipped: a birth, whose line has no other story to record how it ended.
    template <class MakeOrigin>
    void offer_birth(std::uint64_t state, double score, std::int64_t line, MakeOrigin make_origin);
    void emit_slot(const Slot& slot, std::uint64_t state);
    void consider_rescue(double score, std::size_t parent, Move gap);

    // Makes candidate j into `out` (storage reused); t: the sample's time (s), n0: its N0 (FS^2/Hz).
    void build(std::uint32_t j, double t, double n0, Story& out);
    // A newborn's new level: a new newborn integral (its prior's bounds from N0 at its birth, O10) and empty long-run
    // sums and amplitude.
    void restart_level(Story& s, double n0) const;
    std::int32_t letter_text(const Story& parent, bool word_space, double t);

    std::int64_t new_line(LineKind kind, double t);
    void removed(std::int64_t line, LineEnd step);
    void update_stats(double t, std::size_t ceiling_drops);

    // --- The configuration, read once ---
    std::shared_ptr<const ViterbiShared> shared_;
    MorseTree tree_;
    Fist fist_;
    FadingTables fading_;
    Story prototype_;  // a story as made: its long-run sums carry the spread, bin and memory
    OpenTiming open_;
    bool skip_;
    std::int64_t* skipped_successors_;  // detail::SearchOptions' counters, or null
    std::int64_t* newborn_copies_;
    bool stats_on_;
    double beam_nats_;
    std::size_t ceiling_;
    int speeds_;
    double interval_s_;        // Delta
    int order_;
    double floor_;             // systematic_floor
    double sigma2_per_n0_hz_;  // sigma_y^2 / N0 (Hz)
    double switch_sigma_u_;    // nepers
    int grid_points_;
    double live_range_nats_;
    double level_min_db_;
    double level_max_db_;
    double w_;                // the one constant weight, the output noise's w_strong (D23)
    double multiplier_max_;   // M: added_variance_multiplier_max
    double level_knee_;       // X0: added_variance_level_knee
    double update_min_s_;     // amplitude_update_min_template
    std::uint32_t merge_edge_mask_;  // the bits of EdgeHistory::recent in a successor's merge key
    NewbornFade fade_;        // the newborn's prediction step (D24)
    double log_stay_silent_;  // ln(1 - P)
    double log_over_start_;   // ln P
    double log_letter_;       // ln(1 / the tree's symbols), paid when a letter starts
    std::vector<double> letter_end_;  // by node: ln p(symbol) + ln(the tree's symbols), paid when its letter ends
    double log_keep_story_;   // ln(1 - epsilon)
    double log_restart_;      // ln epsilon
    double letter_gap_cost_;  // ln(1 / (1 - p))
    double word_gap_cost_;    // ln(1 / (p (1 - q)))
    double over_end_cost_;    // ln(1 / (p q))
    double log_speed_prior_;  // ln(1 / speed_count)
    int stats_window_samples_;

    // --- The stories: population_[0 ... live_) (the vectors' later elements are storage kept for reuse) ---
    std::vector<Story> population_;
    std::size_t live_ = 0;
    std::uint32_t best_ = 0;  // the best story's index in population_
    SharedText text_;
    double normalization_ = 0.0;
    std::int64_t searched_ = 0;  // samples searched
    std::uint64_t next_creation_ = 1;
    std::int64_t next_line_ = 1;

    // --- The samples given ---
    std::int64_t given_ = 0;       // samples given to step
    std::int64_t last_given_k_ = 0;
    bool finished_ = false;

    // --- One sample's work, kept between samples for its storage ---
    std::vector<Story> continuation_;  // [i]: population_[i] one sample on
    std::vector<char> transferred_;    // [i]: population_[i]'s hold on its text passed to its continuation
    std::vector<detail::Ranked> ranked_;
    std::vector<Origin> origins_;
    std::vector<char> dead_;
    std::vector<std::uint32_t> alive_;
    StateTable general_;
    Slot no_signal_;   // the quiet no-signal state
    double known_best_ = -kInf;  // nats: the best score offered so far at this sample
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
        std::int64_t alive_sample = -1;   // the last sample after whose pruning it had a live story
        double best_score = -kInf;        // nats: its best live story's relative score at that sample
        LineEnd last_removal = LineEnd::Alive;
        bool left_first_element = false;  // one of its live stories was not in the newborn's first mark
    };
    std::vector<LineTrack> tracks_;
    std::vector<std::int64_t> active_lines_;  // alive after the last sample
    std::vector<std::pair<std::int64_t, double>> born_;  // this sample's newborns: line and score
    double window_sum_ = 0.0;
    int window_count_ = 0;
};

Search::Impl::Impl(const ViterbiConfig& config, std::shared_ptr<const ViterbiShared> shared,
                   detail::SearchOptions options)
    : shared_(checked_shared(std::move(shared))),
      tree_(),
      fist_(config),
      fading_(spread_of(config), config.sample_interval_s),
      prototype_(long_run_of(config)),
      open_(fist_, tree_, config.speed_count, open_timing_cap(config, options)),
      skip_(options.skip_successors_beyond_beam),
      skipped_successors_(options.skipped_successors),
      newborn_copies_(options.newborn_copies),
      stats_on_(config.stats),
      beam_nats_(config.beam_nats),
      ceiling_(0),
      speeds_(config.speed_count),
      interval_s_(config.sample_interval_s),
      order_(config.quadrature_order),
      floor_(config.systematic_floor),
      sigma2_per_n0_hz_(shared_->output_noise().sigma2_per_n0_hz),
      switch_sigma_u_(config.newborn_switch_sigma_u),
      grid_points_(config.newborn_grid_points),
      live_range_nats_(config.newborn_live_range_nats),
      level_min_db_(config.level_min_db),
      level_max_db_(config.level_max_db),
      w_(shared_->output_noise().w_strong),
      multiplier_max_(config.added_variance_multiplier_max),
      level_knee_(config.added_variance_level_knee),
      update_min_s_(config.amplitude_update_min_template),
      merge_edge_mask_(0),
      fade_{whole_count(config.newborn_fade_interval_s, config.sample_interval_s, "newborn_fade_interval_s", "samples"),
            0.5 * (1.0 - fading_.rho(1) * fading_.rho(1))} {
    // The shared parts must be this configuration's: its sample interval, edges, level range and newborn grid.
    if (!(std::fabs(shared_->sample_interval_s() - config.sample_interval_s) <= 1e-9 * config.sample_interval_s))
        throw std::invalid_argument("search: the shared parts were built for another sample_interval_s");
    if (shared_->level_min_db() != config.level_min_db || shared_->level_max_db() != config.level_max_db)
        throw std::invalid_argument("search: the shared parts were built for another level range");
    if (shared_->newborn_tables().points() != config.newborn_grid_points)
        throw std::invalid_argument("search: the shared parts were built for another newborn_grid_points");
    if (!(config.systematic_floor >= 0.0 && config.systematic_floor <= 1.0))
        throw std::invalid_argument("search: systematic_floor must lie in [0, 1] (relative amplitude)");
    if (!(config.beam_nats > 0.0)) throw std::invalid_argument("search: beam_nats must be positive (nats)");
    if (config.ceiling < 1) throw std::invalid_argument("search: the ceiling must be at least 1 story");
    if (config.speed_count > 255) throw std::invalid_argument("search: at most 255 speeds");
    if (tree_.size() > 16383) throw std::invalid_argument("search: the Morse tree has too many nodes");
    if (!positive_and_finite(config.newborn_switch_sigma_u))
        throw std::invalid_argument("search: newborn_switch_sigma_u must be positive and finite (nepers)");
    if (!(config.newborn_live_range_nats > 0.0))
        throw std::invalid_argument("search: newborn_live_range_nats must be positive (nats)");
    const double over_start = config.over_rate_per_s * config.sample_interval_s;  // P, per sample
    const auto probability = [](double x) { return x > 0.0 && x < 1.0; };
    if (!probability(over_start))
        throw std::invalid_argument(
            "search: over_rate_per_s x sample_interval_s must lie in (0, 1) (a probability per sample)");
    if (!probability(config.rescue_epsilon)) throw std::invalid_argument("search: rescue_epsilon must lie in (0, 1)");
    if (!probability(config.word_end_p)) throw std::invalid_argument("search: word_end_p must lie in (0, 1)");
    if (!probability(config.over_end_q)) throw std::invalid_argument("search: over_end_q must lie in (0, 1)");
    if (config.quadrature_order < 2 || config.quadrature_order > 20)
        throw std::invalid_argument("search: the quadrature's order must be 2 to 20");
    if (!(config.added_variance_multiplier_max >= 1.0) || !std::isfinite(config.added_variance_multiplier_max))
        throw std::invalid_argument("search: added_variance_multiplier_max must be finite and at least 1");
    if (!positive_and_finite(config.added_variance_level_knee))
        throw std::invalid_argument(
            "search: added_variance_level_knee must be positive and finite (Omega_bar / sigma_y^2)");
    if (!(config.amplitude_update_min_template >= 0.0 && config.amplitude_update_min_template <= 1.0))
        throw std::invalid_argument("search: amplitude_update_min_template must lie in [0, 1] (the template's |s|)");
    if (config.merge_edge_history_samples < 0 || config.merge_edge_history_samples > TemplateTables::kSpan)
        throw std::invalid_argument("search: merge_edge_history_samples must be 0 to 16 (samples)");
    merge_edge_mask_ = (std::uint32_t{1} << config.merge_edge_history_samples) - 1u;
    ceiling_ = static_cast<std::size_t>(config.ceiling);

    int symbols = 0;
    for (std::size_t node = 0; node < tree_.size(); ++node)
        if (!tree_.symbol(static_cast<int>(node)).empty()) ++symbols;
    if (config.symbol_prior_on_air && !(config.symbol_prior_floor > 0.0 && config.symbol_prior_floor < 1.0 / symbols))
        throw std::invalid_argument("search: symbol_prior_floor must lie in (0, 1 / the tree's symbols) (a probability)");
    letter_end_.assign(tree_.size(), 0.0);
    if (config.symbol_prior_on_air) {
        const std::vector<double> log_prior = on_air_log_prior(tree_, config.symbol_prior_floor);
        for (std::size_t node = 0; node < tree_.size(); ++node)
            if (!tree_.symbol(static_cast<int>(node)).empty())
                letter_end_[node] = log_prior[node] + std::log(static_cast<double>(symbols));
    }
    log_stay_silent_ = std::log1p(-over_start);
    log_over_start_ = std::log(over_start);
    log_letter_ = -std::log(static_cast<double>(symbols));
    log_keep_story_ = std::log1p(-config.rescue_epsilon);
    log_restart_ = std::log(config.rescue_epsilon);
    letter_gap_cost_ = -std::log1p(-config.word_end_p);
    word_gap_cost_ = -(std::log(config.word_end_p) + std::log1p(-config.over_end_q));
    over_end_cost_ = -(std::log(config.word_end_p) + std::log(config.over_end_q));
    log_speed_prior_ = -std::log(static_cast<double>(speeds_));
    stats_window_samples_ = std::max(1, static_cast<int>(std::lround(kStatsWindowS / config.sample_interval_s)));

    // At the channel's first sample there is one story: no signal, score 0, empty text, no edges, line 0.
    Story first = prototype_;
    first.state = pack_state(Phase::NoSignal, 0, kOpen, -1);
    population_.push_back(first);
    live_ = 1;
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
            best = std::max(best, fist_.log_survival(OpenTiming::mark_kind(label), speed, tau));
    return best;
}

double Search::Impl::gap_survival(int speed, int node, int tau) const {
    double best = -kInf;
    if (tree_.has_children(node)) best = fist_.log_survival(Kind::ElementGap, speed, tau);
    if (!tree_.symbol(node).empty()) {
        const double letter_end = letter_end_[static_cast<std::size_t>(node)];
        best = std::max(best, fist_.log_survival(Kind::LetterGap, speed, tau) - letter_gap_cost_ + letter_end);
        best = std::max(best, fist_.log_survival(Kind::WordGap, speed, tau) - word_gap_cost_ + letter_end);
    }
    return best;
}

double Search::Impl::score_sample(Story& c, const Pending& sample, double sigma2) {
    // 1. The edge this story claims at this sample (its element's first), then its template and edge term.
    const bool claim = c.edge_next;
    advance(c.edges, claim);
    c.edge_next = false;
    if (c.newborn) {
        if (claim && c.phase != Phase::Mark && c.key_up_age < 0)
            c.key_up_age = 0;  // the newborn's first mark's key-up
        else if (c.key_up_age >= 0)
            ++c.key_up_age;
    }
    const TemplateValue tv = template_value(c.edges, shared_->template_tables());
    double score = 0.0;
    if (tv.s != 0.0) {
        // 2. The score by the newborn integral (V(a_i) at each point, the prediction step) or by the story's prediction,
        // with V_k from that prediction, against the reference every story shares.
        if (c.newborn) {
            if (c.newborn.use_count() > 1) {  // shared with a story descended from the same newborn: copy on write
                c.newborn = std::make_shared<NewbornIntegral>(*c.newborn);
                if (newborn_copies_) ++*newborn_copies_;
            }
            c.newborn->add(sample.e, tv.s, tv.v, sigma2, floor_);
            score = c.newborn->last_score_nats();
        } else {
            const Prediction predicted = predict_amplitude_at_level(c.amp, sample.k, c.omega_bar, sigma2, multiplier_max_,
                                                                    level_knee_, fading_);
            const double variance =
                systematic_variance_fs2(sigma2, predicted.mu_fs * predicted.mu_fs + predicted.p_fs2, tv.s, tv.v, floor_);
            const SampleEvidence evidence = story_evidence(sample.e, tv.s, predicted.mu_fs, predicted.p_fs2, variance,
                                                           sigma2, w_, order_, c.mode_a);
            score = evidence.score_nats;
            // Where the template is tiny the sample says nothing of the amplitude: it is scored, but the lag runs on.
            if (std::fabs(tv.s) >= update_min_s_) {
                c.amp = Amplitude{evidence.amplitude.mean, evidence.amplitude.var, sample.k};
                c.mode_a = evidence.mode_a_fs;
            }
        }
        c.long_run.at_sample(sample.k);
        c.long_run.add(tv.s, sample.e, sigma2);
        if (!c.newborn) c.omega_bar = omega_bar_of(c, sigma2);
    }
    // The newborn's switch to N(mu, P), after each of its samples (and at the latest at j = 15 after its first key-up).
    if (c.newborn &&
        c.newborn->should_switch(switch_sigma_u_, c.key_up_age == kLastScoredSampleAfterKeyUp)) {
        c.amp = c.newborn->to_gaussian(sample.k);
        c.mode_a = c.amp.mu_fs;
        c.newborn.reset();
        c.key_up_age = -1;
        c.omega_bar = omega_bar_of(c, sigma2);
    }
    return score;
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
    offer_birth(state, score, line, make_origin);
}

template <class MakeOrigin>
void Search::Impl::offer_birth(std::uint64_t state, double score, std::int64_t line, MakeOrigin make_origin) {
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

void Search::Impl::emit_slot(const Slot& slot, std::uint64_t state) {
    if (slot.sample == searched_) push_candidate(state, slot.score, slot.creation, slot.origin);
}

void Search::Impl::consider_rescue(double score, std::size_t parent, Move gap) {
    if (score > rescue_.score) rescue_ = Rescue{score, static_cast<std::int32_t>(parent), gap};
}

void Search::Impl::end_mark(std::size_t i) {
    const Story& s = continuation_[i];  // the parent after this sample: its base holds the sample's score
    const int tau = s.tau;
    const double base = s.base;
    const std::uint64_t edges = edge_key(s.edges);
    const auto offer_over_end = [&](int label, int child, double score) {
        offer_general(pack_state(Phase::NoSignal, 1, kOpen, -1) | edges,
                      score - over_end_cost_ + letter_end_[static_cast<std::size_t>(child)], s.line, [&] {
                          return Origin{static_cast<std::int32_t>(i), Move::EndOver, static_cast<std::uint8_t>(label),
                                        Move::Continue, kOpen, s.line};
                      });
    };
    if (s.speed == kOpen) {
        // A newborn's first mark, at the root: it branches into every speed and label with a finite ln P, each paying
        // ln(1/speed_count). The over may end after E or T: all speeds land in the no-signal state, so only each
        // label's best speed is offered.
        const OpenTiming::FirstMarkEnds ends = open_.first_mark_ends(tau);
        const double head = base + log_speed_prior_;
        const bool gaps_beyond = beyond_beam(head + ends.best);  // then every gap successor is beyond the beam
        for (int label = 0; label < 2; ++label) {
            const int child = tree_.child(tree_.root(), label == 1);
            if (child < 0) continue;
            for (int speed = 0; speed < speeds_ && !gaps_beyond; ++speed) {
                const double log_end = ends.log_end[static_cast<std::size_t>(label * speeds_ + speed)];
                if (log_end == -kInf) continue;
                offer_general(pack_state(Phase::Gap, 0, speed, child) | edges, head + log_end, s.line, [&] {
                    return Origin{static_cast<std::int32_t>(i), Move::EndMark, static_cast<std::uint8_t>(label),
                                  Move::Continue, static_cast<std::int16_t>(speed), s.line};
                });
            }
            if (!tree_.symbol(child).empty() && ends.label_best[label] > -kInf)
                offer_over_end(label, child, head + ends.label_best[label]);
        }
        return;
    }
    for (int label = 0; label < 2; ++label) {
        const int child = tree_.child(s.node, label == 1);
        if (child < 0) continue;
        const double log_end = fist_.log_end(OpenTiming::mark_kind(label), s.speed, tau);
        if (log_end == -kInf) continue;
        const double score = base + log_end;
        offer_general(pack_state(Phase::Gap, 0, s.speed, child) | edges, score, s.line, [&] {
            return Origin{static_cast<std::int32_t>(i), Move::EndMark, static_cast<std::uint8_t>(label), Move::Continue,
                          s.speed, s.line};
        });
        if (!tree_.symbol(child).empty()) offer_over_end(label, child, score);
    }
}

void Search::Impl::end_gap(std::size_t i) {
    const Story& s = continuation_[i];  // the parent after this sample: its base holds the sample's score
    const int tau = s.tau;
    const int node = s.node;
    const std::uint64_t edges = edge_key(s.edges);
    if (tree_.has_children(node)) {
        const double log_end = fist_.log_end(Kind::ElementGap, s.speed, tau);
        if (log_end > -kInf)
            offer_general(pack_state(Phase::Mark, 0, s.speed, node) | edges, s.base + log_end, s.line, [&] {
                return Origin{static_cast<std::int32_t>(i), Move::EndElementGap, 0, Move::Continue, s.speed, s.line};
            });
    }
    if (tree_.symbol(node).empty()) return;
    // A letter ends and the next starts at the root: it pays the letter that ended, ln p(symbol) + ln 62 (the flat
    // ln(1/62) having been paid when that letter started), the new letter, ln(1/62), and ln(1/(1 - epsilon)) for not
    // restarting.
    const double head = s.base + letter_end_[static_cast<std::size_t>(node)] + log_letter_ + log_keep_story_;
    const std::uint64_t start = pack_state(Phase::Mark, 0, s.speed, tree_.root()) | edges;
    for (const Kind kind : {Kind::LetterGap, Kind::WordGap}) {
        const bool word = kind == Kind::WordGap;
        const Move move = word ? Move::EndWordGap : Move::EndLetterGap;
        const double log_end = fist_.log_end(kind, s.speed, tau);
        if (log_end == -kInf) continue;
        const double score = head - (word ? word_gap_cost_ : letter_gap_cost_) + log_end;
        consider_rescue(score, i, move);
        offer_general(start, score, s.line,
                      [&] { return Origin{static_cast<std::int32_t>(i), move, 0, Move::Continue, s.speed, s.line}; });
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
    // The letter: from its first mark's claimed key-down to its last mark's claimed key-up; a word space: across the
    // gap, to t, the next mark's claimed key-down.
    const std::int32_t letter =
        text_.append(parent.text, tree_.symbol(parent.node), parent.letter_start_s, parent.last_mark_end_s);
    if (!word_space) return letter;
    const std::int32_t space = text_.append(letter, kWordSpace, parent.last_mark_end_s, t);
    text_.release(letter);  // the space holds it
    return space;
}

void Search::Impl::restart_level(Story& s, double n0) const {
    // The prior's bounds in u = ln(a / 1 FS): a = sqrt(10^(L/10) N0 x 500 Hz) at L = level_min_db and level_max_db.
    const double ln_noise = std::log(n0 * kS500BandwidthHz);
    const double ln10 = std::numbers::ln10;
    const double u_lo = 0.5 * (level_min_db_ / 10.0 * ln10 + ln_noise);
    const double u_hi = 0.5 * (level_max_db_ / 10.0 * ln10 + ln_noise);
    s.newborn = std::make_shared<NewbornIntegral>(shared_->newborn_tables(), u_lo, u_hi, grid_points_,
                                                  live_range_nats_, fade_);
    s.key_up_age = -1;
    s.amp = Amplitude{0.0, 0.0, 0};
    s.mode_a = 0.0;
    s.omega_bar = 0.0;
    s.long_run.restart();
}

void Search::Impl::build(std::uint32_t j, double t, double n0, Story& out) {
    const Origin& o = origins_[j];
    const detail::Ranked& r = ranked_[j];
    const std::size_t parent_index = static_cast<std::size_t>(o.parent);
    const Story& parent = continuation_[parent_index];  // the parent after this sample
    out = parent;  // its edges, amplitude, long-run sums and newborn integral (shared until written)
    if (o.move == Move::Continue) {
        transferred_[parent_index] = 1;  // the continuation takes over its parent's hold on the text
        return;
    }
    // The successor's element starts at the next sample, where it claims its edge, at time t + Delta.
    const double edge_t = t + interval_s_;
    out.state = r.state;
    out.score = r.score_nats;
    out.base = r.score_nats;
    out.creation = r.creation;
    out.line = o.line;
    out.tau = 0;
    out.edge_next = true;
    switch (o.move) {
        case Move::EndMark:
            out.phase = Phase::Gap;
            out.speed = o.speed;
            out.node = static_cast<std::int16_t>(tree_.child(parent.node, o.label == 1));
            text_.hold(parent.text);
            out.last_mark_end_s = edge_t;
            break;
        case Move::EndOver: {
            out.phase = Phase::NoSignal;
            out.tau = 1;
            out.speed = kOpen;
            out.node = -1;
            const int child = tree_.child(parent.node, o.label == 1);
            out.text = text_.append(parent.text, tree_.symbol(child), parent.letter_start_s, edge_t);
            out.letter_start_s = edge_t;
            out.last_mark_end_s = edge_t;
            break;
        }
        case Move::EndElementGap:
            out.phase = Phase::Mark;
            out.speed = parent.speed;
            out.node = parent.node;
            text_.hold(parent.text);
            break;
        case Move::EndLetterGap:
        case Move::EndWordGap:
            out.phase = Phase::Mark;
            out.speed = parent.speed;
            out.node = static_cast<std::int16_t>(tree_.root());
            out.text = letter_text(parent, o.move == Move::EndWordGap, edge_t);
            out.letter_start_s = edge_t;
            break;
        case Move::BirthFromSilence:
            out.phase = Phase::Mark;
            out.speed = kOpen;
            out.node = static_cast<std::int16_t>(tree_.root());
            // A new over after silence begins with a word space if the text is not empty.
            out.text = parent.text == SharedText::kEmpty ? SharedText::kEmpty
                                                         : text_.append(parent.text, kWordSpace, edge_t, edge_t);
            out.letter_start_s = edge_t;
            out.last_mark_end_s = edge_t;
            restart_level(out, n0);
            break;
        case Move::BirthRescue:
            out.phase = Phase::Mark;
            out.speed = kOpen;
            out.node = static_cast<std::int16_t>(tree_.root());
            out.text = letter_text(parent, o.rescued_gap == Move::EndWordGap, edge_t);
            out.letter_start_s = edge_t;
            out.last_mark_end_s = edge_t;
            // Its edge history, amplitude and long-run sums kept: a sender who changes speed keeps his level (decision log).
            break;
        case Move::Continue:
            break;
    }
}

void Search::Impl::step(double e, double t, double n0) {
    if (finished_) throw std::logic_error("search: a sample given after finish()");
    if (!positive_and_finite(n0))
        throw std::invalid_argument("search: n0_fs2_per_hz must be positive and finite (FS^2/Hz)");
    if (!(e >= 0.0) || !std::isfinite(e))
        throw std::invalid_argument("search: e_fs must be finite and not negative (FS)");
    if (!std::isfinite(t)) throw std::invalid_argument("search: t_s must be finite (s)");
    const double position = t / interval_s_;
    if (!(std::fabs(position) < 9e15)) throw std::invalid_argument("search: t_s is too far from 0 (s)");
    const auto k = static_cast<std::int64_t>(std::llround(position));
    if (given_ > 0 && !(k > last_given_k_))
        throw std::invalid_argument("search: t_s must lie at a later sample than the previous sample's (s)");
    ++given_;
    last_given_k_ = k;
    search_sample(Pending{e, t, n0, k});
}

void Search::Impl::finish() {
    if (finished_) return;
    finished_ = true;
}

void Search::Impl::search_sample(const Pending& sample) {
    const double t = sample.t;
    if (stats_on_ && searched_ == 0) stats_.lines[0].birth_s = t;
    const double sigma2 = sample.n0 * sigma2_per_n0_hz_;  // FS^2: sigma_y^2 at this sample

    const std::size_t n_old = live_;
    while (continuation_.size() < n_old) continuation_.push_back(prototype_);
    transferred_.assign(n_old, 0);
    ranked_.clear();
    origins_.clear();
    dead_.clear();
    known_best_ = -kInf;
    rescue_ = Rescue{};
    born_.clear();
    std::int32_t no_signal = kNone;
    std::size_t open_marks = 0;  // stories in a newborn's first mark, each offering a gap at every speed and label

    // 2. Every story continues: it scores the sample (search.hpp, "One sample k searched"), books it in its base and
    // adds its element's timing. The continuations are candidates in the previous population's order, the no-signal
    // story through its slot, where the over's ends of this sample merge with it.
    for (std::size_t i = 0; i < n_old; ++i) {
        Story& s = population_[i];
        Story& c = continuation_[i];
        c = s;
        c.newborn = std::move(s.newborn);  // the old story is done with it: no copy on write for a lone story
        const double score = score_sample(c, sample, sigma2);
        switch (c.phase) {
            case Phase::NoSignal:
                c.base = s.base + score + log_stay_silent_;
                c.score = c.base;
                // An over's end restarts the level once its tail has been scored (part 7): every over starts afresh,
                // and the story joins the quiet state. Until then its clock keeps it apart: stories whose key-ups differ
                // predict different samples, so merging them would keep whichever had scored best before the fall.
                if (c.edges.recent == 0 && !c.edges.key_down_before && !c.newborn) {
                    c.tau = 0;
                    c.long_run.restart();
                    c.amp = Amplitude{0.0, 0.0, 0};
                    c.mode_a = 0.0;
                    c.omega_bar = 0.0;
                } else {
                    c.tau = s.tau + 1;
                }
                if (no_signal == kNone || c.score > continuation_[static_cast<std::size_t>(no_signal)].score)
                    no_signal = static_cast<std::int32_t>(i);
                break;
            case Phase::Mark:
                c.tau = s.tau + 1;
                c.base = s.base + score;
                c.score = c.base + (s.speed == kOpen ? log_speed_prior_ + open_.first_mark_survival(c.tau)
                                                     : mark_survival(s.speed, s.node, c.tau));
                if (s.speed == kOpen) ++open_marks;
                break;
            case Phase::Gap:
                c.tau = s.tau + 1;
                c.base = s.base + score;
                c.score = c.base + gap_survival(s.speed, s.node, c.tau);
                break;
        }
        c.state = pack_state(c.phase, c.tau, c.speed, c.node);
        known_best_ = std::max(known_best_, c.score);
        const Origin continued{static_cast<std::int32_t>(i), Move::Continue, 0, Move::Continue, c.speed, c.line};
        if (c.state != 0) {
            push_candidate(c.state, c.score, c.creation, continued);
        } else if (no_signal_.sample != searched_ ||  // the quiet state, in its slot: one story, and one coming to it
                   detail::replaces(c.score, c.creation, no_signal_.score, no_signal_.creation)) {
            no_signal_ = Slot{c.score, c.creation, continued, searched_};
        }
    }
    // ... and ends, into successors that start their next element at the next sample. The most it can offer into the
    // table: 4 keys per story (2 gaps, 2 over's ends; a gap's 3 are fewer), 2 x speeds + 2 per first mark, and 2 births.
    general_.reset(4 * n_old + (2 * static_cast<std::size_t>(speeds_) + 2) * open_marks + 2);
    for (std::size_t i = 0; i < n_old; ++i) {
        if (continuation_[i].phase == Phase::Mark)
            end_mark(i);
        else if (continuation_[i].phase == Phase::Gap)
            end_gap(i);
    }

    // 3. Births: from silence, off the best continuing no-signal story (silent at this sample); and one rescue newborn from
    // the best letter-starting successor. Both are marks at the root with their speed open, in keys of their own: one has a
    // level and the other a flat prior over it, so their scores at birth cannot choose between them before any evidence.
    if (no_signal != kNone) {
        const Story& c = continuation_[static_cast<std::size_t>(no_signal)];
        const double score = c.score + log_over_start_ + log_letter_;
        const std::int64_t line = new_line(LineKind::Silence, t);
        born_.emplace_back(line, score);
        offer_birth(pack_state(Phase::Mark, 0, kOpen, tree_.root()), score, line,
                    [&] { return Origin{no_signal, Move::BirthFromSilence, 0, Move::Continue, kOpen, line}; });
    }
    if (rescue_.parent != kNone) {
        const double score = rescue_.score - log_keep_story_ + log_restart_;
        const std::int64_t line = new_line(LineKind::Rescue, t);
        born_.emplace_back(line, score);
        offer_birth(pack_state(Phase::Mark, 0, kOpen, tree_.root()) | kRescueKeyBit, score, line,
                    [&] { return Origin{rescue_.parent, Move::BirthRescue, 0, rescue_.gap, kOpen, line}; });
    }

    // 4. Merging is done as successors are offered; the quiet state's winner joins the candidates.
    emit_slot(no_signal_, pack_state(Phase::NoSignal, 0, kOpen, -1));
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

    // 6. The new population, scores relative to the best, built into storage kept from earlier samples. Its texts are
    // held before the old population's holds go; then the old population and the continuations let go of their newborn
    // integrals, so that only live stories share one (copy on write counts the holders).
    std::size_t next = 0;
    for (const std::uint32_t j : alive_) {
        if (j == best) best_ = static_cast<std::uint32_t>(next);
        if (next_population_.size() <= next) next_population_.push_back(prototype_);
        Story& s = next_population_[next];
        build(j, t, sample.n0, s);
        s.score -= best_score;
        s.base -= best_score;
        ++next;
    }
    for (std::size_t i = 0; i < n_old; ++i) {
        if (!transferred_[i]) text_.release(population_[i].text);
        population_[i].newborn.reset();
        continuation_[i].newborn.reset();
    }
    population_.swap(next_population_);
    live_ = next;
    normalization_ += best_score;

    if (stats_on_) update_stats(t, ceiling_dropped_.size());
    ++searched_;
}

void Search::Impl::update_stats(double t, std::size_t ceiling_drops) {
    ++stats_.samples;
    if (ceiling_drops > 0) ++stats_.ceiling_samples;
    const std::size_t live = live_;
    const std::size_t bin = live / 10;
    if (stats_.live_histogram.size() <= bin) stats_.live_histogram.resize(bin + 1, 0);
    ++stats_.live_histogram[bin];
    if (window_count_ == stats_window_samples_ || stats_.live_mean_100ms.empty()) {
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
    for (std::size_t i = 0; i < live_; ++i) {
        const Story& s = population_[i];
        LineTrack& track = tracks_[static_cast<std::size_t>(s.line)];
        if (track.alive_sample != searched_) {
            track.alive_sample = searched_;
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
        if (track.alive_sample == searched_) {
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
    // A story at a node holding a symbol: that symbol is its last letter, from its first mark's claimed key-down to its
    // last finished mark's claimed key-up.
    if (best.phase != Phase::NoSignal) {
        const std::string_view symbol = tree_.symbol(best.node);
        if (!symbol.empty())
            text.push_back(DecodedSymbol{std::string(symbol), 1.0f, best.letter_start_s, best.last_mark_end_s});
    }
    return text;
}

std::vector<StoryView> Search::Impl::live_stories() const {
    std::vector<const Story*> order;
    order.reserve(live_);
    for (std::size_t i = 0; i < live_; ++i) order.push_back(&population_[i]);
    std::sort(order.begin(), order.end(), [](const Story* a, const Story* b) {
        return a->state != b->state ? a->state < b->state : a->creation < b->creation;
    });
    std::vector<StoryView> views;
    views.reserve(order.size());
    for (const Story* s : order) {
        const bool morse = s->phase != Phase::NoSignal;
        views.push_back(StoryView{morse ? Activity::Morse : Activity::NoSignal, s->phase == Phase::Mark, s->tau,
                                  s->speed, morse ? s->node : -1, s->score, s->line, s->amp, s->omega_bar,
                                  s->long_run.nu(), s->long_run.omega_hat_fs2(), s->newborn != nullptr, s->edges,
                                  s->mode_a, reinterpret_cast<std::uintptr_t>(s->newborn.get()), s->creation});
    }
    return views;
}

std::size_t Search::Impl::newborn_integrals() const {
    std::unordered_set<const NewbornIntegral*> held;
    for (std::size_t i = 0; i < live_; ++i)
        if (population_[i].newborn) held.insert(population_[i].newborn.get());
    return held.size();
}

std::size_t Search::Impl::newborn_integral_bytes() const {
    std::unordered_set<const NewbornIntegral*> held;
    std::size_t bytes = 0;
    for (std::size_t i = 0; i < live_; ++i) {
        const NewbornIntegral* n = population_[i].newborn.get();
        if (n && held.insert(n).second) bytes += n->bytes();
    }
    return bytes;
}

Search::Search(const ViterbiConfig& config, std::shared_ptr<const ViterbiShared> shared,
               detail::SearchOptions options)
    : impl_(std::make_unique<Impl>(config, std::move(shared), options)) {}
Search::~Search() = default;
Search::Search(Search&&) noexcept = default;
Search& Search::operator=(Search&&) noexcept = default;

void Search::step(double e_fs, double t_s, double n0_fs2_per_hz) { impl_->step(e_fs, t_s, n0_fs2_per_hz); }
void Search::finish() { impl_->finish(); }
std::vector<DecodedSymbol> Search::best_text() const { return impl_->best_text(); }
const SearchStats& Search::stats() const { return impl_->stats(); }
std::size_t Search::live() const { return impl_->live(); }
std::vector<StoryView> Search::live_stories() const { return impl_->live_stories(); }
double Search::normalization_nats() const { return impl_->normalization(); }
std::size_t Search::text_entries() const { return impl_->text_entries(); }
std::size_t Search::open_timing_bytes() const { return impl_->open_timing_bytes(); }
std::size_t Search::newborn_integrals() const { return impl_->newborn_integrals(); }
std::size_t Search::newborn_integral_bytes() const { return impl_->newborn_integral_bytes(); }

}  // namespace kz4ap::viterbi
