#include "kz4ap/segmental/search.hpp"

#include "../viterbi/text.hpp"
#include "kz4ap/viterbi/fist.hpp"
#include "kz4ap/viterbi/symbol_prior.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <limits>
#include <new>
#include <stdexcept>
#include <string_view>

namespace kz4ap::segmental {

namespace {

constexpr double kInf = std::numeric_limits<double>::infinity();
constexpr int kDot = 0, kDash = 1, kElementGap = 2, kLetterGap = 3, kWordGap = 4;

enum ElementKind : std::uint8_t { kMark, kGap, kOpenMark };
enum GapEnd : std::uint8_t { kEndElement, kEndLetter, kEndWord };

}  // namespace

ModelTables::ModelTables(const SegmentalConfig& config) {
    const viterbi::ViterbiConfig& model = config.model;
    speeds_ = model.speed_count;
    nodes_ = static_cast<int>(tree_.size());
    if (nodes_ > 255) throw std::invalid_argument("segmental: at most 255 tree nodes");
    if (!(config.beam_nats > 0.0) || config.ceiling < 1 || !(config.duration_z_max > 0.0))
        throw std::invalid_argument("segmental: beam_nats and duration_z_max must be positive, and ceiling at least 1");
    if (config.gap_stretches < 1 || config.gap_stretch_speed_steps < 1)
        throw std::invalid_argument("segmental: gap_stretches and gap_stretch_speed_steps must be at least 1");
    // A float probability below 1 holds a margin of at most ln(2^24 - 1) = 16.64 nats; past 17.33 nats 1 / (1 + e^-m) rounds to 1, which
    // the callsign stage reads as "unknown" (derived).
    if (!(config.rival_margin_nats >= 0.0 && config.rival_margin_nats <= 16.6))
        throw std::invalid_argument("segmental: rival_margin_nats must lie in [0, 16.6] nats (a float probability holds no more)");
    if (!(config.gap_stretch_prior >= 0.0 && config.gap_stretch_prior < 1.0))
        throw std::invalid_argument("segmental: gap_stretch_prior must lie in [0, 1)");
    if (config.gap_stretches > 1 && !(config.gap_stretch_prior > 0.0))
        throw std::invalid_argument("segmental: gap_stretches above 1 needs gap_stretch_prior above 0");
    if (!(config.gap_unit_max_s > 0.0)) throw std::invalid_argument("segmental: gap_unit_max_s must be positive (s)");
    sample_interval_s_ = model.sample_interval_s;
    children_.resize(static_cast<std::size_t>(nodes_) * 2);
    for (int node = 0; node < nodes_; ++node) {
        children_[static_cast<std::size_t>(node * 2)] = tree_.child(node, false);
        children_[static_cast<std::size_t>(node * 2 + 1)] = tree_.child(node, true);
    }

    // The fists. A stretch of n steps reads a letter or word gap at speed s as speed s + n x gap_stretch_speed_steps does, so
    // the gaps' tables are kept per such a speed and shared. A speed keeps the stretches whose gap unit is within gap_unit_max_s.
    // A weight w moves every mark by +w T_s and the gap after it by -w T_s (letter and word gaps too with fist_weight_letter_gaps):
    // a Fist of its own, the model's shifts plus these, or with fist_weight_scaled its medians moved by them, the shifts as they are.
    // The weights' order is 0 first, then the others as configured.
    const std::vector<double>& weights = config.fist_weights_dots;
    if (std::count(weights.begin(), weights.end(), 0.0) != 1)
        throw std::invalid_argument("segmental: fist_weights_dots must hold 0, once");
    for (const double w : weights)
        if (!(std::fabs(w) < 1.0)) throw std::invalid_argument("segmental: every weight must lie strictly between -1 and +1 dots");
    if (!(config.fist_weight_prior >= 0.0 && config.fist_weight_prior < 1.0))
        throw std::invalid_argument("segmental: fist_weight_prior must lie in [0, 1)");
    if (weights.size() > 1 && !(config.fist_weight_prior > 0.0))
        throw std::invalid_argument("segmental: weights other than 0 need fist_weight_prior above 0");
    std::vector<double> order{0.0};
    for (const double w : weights)
        if (w != 0.0) order.push_back(w);
    const int weight_count = static_cast<int>(order.size());
    const int extra = (config.gap_stretches - 1) * config.gap_stretch_speed_steps;
    const int table_speeds = speeds_ + extra;
    std::vector<std::array<double, 5>> shifts_of;   // by weight: dots
    std::vector<std::array<double, 5>> medians_of;  // by weight: dots
    std::vector<viterbi::Fist> fist_of;             // by weight
    for (const double w : order) {
        viterbi::ViterbiConfig longer = model;
        longer.speed_count = table_speeds;
        std::array<double, 5>& moved = config.fist_weight_scaled ? longer.fist_medians_dots : longer.fist_shifts_dots;
        moved[kDot] += w;
        moved[kDash] += w;
        moved[kElementGap] -= w;
        if (config.fist_weight_letter_gaps) {
            moved[kLetterGap] -= w;
            moved[kWordGap] -= w;
        }
        shifts_of.push_back(longer.fist_shifts_dots);
        medians_of.push_back(longer.fist_medians_dots);
        fist_of.emplace_back(longer);
    }
    const auto dot_s = [&](int speed) { return model.dot_min_ms * std::pow(model.speed_ratio, speed) / 1000.0; };
    std::vector<int> gap_speed;     // by fist
    std::vector<int> weight_index;  // by fist, into order
    std::vector<int> group_weight_index;
    first_fist_.resize(static_cast<std::size_t>(speeds_) + 1);
    for (int speed = 0; speed < speeds_; ++speed) {
        first_fist_[static_cast<std::size_t>(speed)] = static_cast<int>(speed_of_.size());
        for (int k = 0; k < weight_count; ++k) {
            const int group = static_cast<int>(group_speed_.size());
            group_speed_.push_back(speed);
            group_weight_.push_back(order[static_cast<std::size_t>(k)]);
            group_weight_index.push_back(k);
            first_of_group_.push_back(static_cast<int>(speed_of_.size()));
            const int stretches = k == 0 || config.fist_weight_stretched ? config.gap_stretches : 1;
            for (int n = 0; n < stretches; ++n) {
                const int stepped = speed + n * config.gap_stretch_speed_steps;
                if (n > 0 && dot_s(stepped) > config.gap_unit_max_s) break;
                speed_of_.push_back(speed);
                group_of_.push_back(group);
                gap_speed.push_back(stepped);
                weight_index.push_back(k);
                stretch_of_.push_back(dot_s(stepped) / dot_s(speed));
            }
        }
    }
    fists_ = static_cast<int>(speed_of_.size());
    first_fist_[static_cast<std::size_t>(speeds_)] = fists_;
    first_of_group_.push_back(fists_);
    if (fists_ > 65534) throw std::invalid_argument("segmental: at most 65 534 fists (speed x weight x gap stretch)");
    log_plain_ = config.gap_stretches > 1 ? std::log1p(-config.gap_stretch_prior) : 0.0;
    log_stretched_ = config.gap_stretches > 1 ? std::log(config.gap_stretch_prior / (config.gap_stretches - 1)) : 0.0;
    std::vector<double> log_weight(static_cast<std::size_t>(weight_count), 0.0);
    if (weight_count > 1) {
        log_weight[0] = std::log1p(-config.fist_weight_prior);
        for (int k = 1; k < weight_count; ++k) log_weight[static_cast<std::size_t>(k)] = std::log(config.fist_weight_prior / (weight_count - 1));
    }
    // The weight whose shifts letter and word gaps read.
    const auto gap_weight = [&](int k) { return config.fist_weight_letter_gaps ? k : 0; };

    // Each kind's lengths per speed of its own and weight: dots, dashes and element gaps at the speed itself, letter and word gaps at
    // the stepped speed. Made as first asked for, kind by kind.
    std::vector<Range> by_speed(static_cast<std::size_t>(5 * weight_count * table_speeds));
    std::vector<std::uint8_t> made(by_speed.size(), 0);
    const auto table = [&](int kind, int speed, int k) {
        const std::size_t index = static_cast<std::size_t>((kind * weight_count + k) * table_speeds + speed);
        if (made[index] == 0) {
            const double sigma = model.fist_sigmas[static_cast<std::size_t>(kind)];
            const double dot_samples = dot_s(speed) / model.sample_interval_s;
            const double median = medians_of[static_cast<std::size_t>(k)][static_cast<std::size_t>(kind)] * dot_samples;
            const double shift = shifts_of[static_cast<std::size_t>(k)][static_cast<std::size_t>(kind)] * dot_samples;
            const int lo = std::max(1, static_cast<int>(std::floor(shift + median * std::exp(-config.duration_z_max * sigma))));
            const int hi = std::max(lo, static_cast<int>(std::ceil(shift + median * std::exp(config.duration_z_max * sigma))));
            by_speed[index] = Range{lo, hi, static_cast<int>(log_end_.size())};
            for (int length = lo; length <= hi; ++length)
                log_end_.push_back(fist_of[static_cast<std::size_t>(k)].log_end(static_cast<viterbi::Kind>(kind), speed, length));
            made[index] = 1;
        }
        return by_speed[index];
    };
    ranges_.resize(static_cast<std::size_t>(5 * fists_));
    for (int kind = 0; kind < 5; ++kind) {
        for (int f = 0; f < fists_; ++f) {
            const int k = weight_index[static_cast<std::size_t>(f)];
            const bool own = kind < kLetterGap;
            const int speed = own ? speed_of_[static_cast<std::size_t>(f)] : gap_speed[static_cast<std::size_t>(f)];
            ranges_[static_cast<std::size_t>(kind * fists_ + f)] = table(kind, speed, own ? k : gap_weight(k));
        }
    }

    const double p = model.word_end_p;
    const double q = model.over_end_q;
    letter_gap_cost_ = -std::log1p(-p);
    word_gap_cost_ = -(std::log(p) + std::log1p(-q));
    over_end_cost_ = -(std::log(p) + std::log(q));

    const int group_count = groups();
    mark_hi_.assign(static_cast<std::size_t>(group_count), 0);
    gap_hi_.assign(static_cast<std::size_t>(fists_), 0);
    for (int f = 0; f < fists_; ++f) {
        const int group = group_of(f);
        for (int kind = 0; kind < 5; ++kind) {
            const int hi = range(kind, f).hi;
            if (kind <= kDash) mark_hi_[static_cast<std::size_t>(group)] = std::max(mark_hi_[static_cast<std::size_t>(group)], hi);
            else gap_hi_[static_cast<std::size_t>(f)] = std::max(gap_hi_[static_cast<std::size_t>(f)], hi);
            longest_ = std::max(longest_, hi);
        }
    }

    mark_survival_offset_.resize(static_cast<std::size_t>(group_count));
    mark_survival_length_.resize(static_cast<std::size_t>(group_count));
    for (int group = 0; group < group_count; ++group) {
        const int speed = group_speed_[static_cast<std::size_t>(group)];
        const viterbi::Fist& fist = fist_of[static_cast<std::size_t>(group_weight_index[static_cast<std::size_t>(group)])];
        // As far as a mark can run; a birth's first mark, which may yet be a slower speed's, clips here.
        const int length_count = mark_hi_[static_cast<std::size_t>(group)] + 2;
        mark_survival_offset_[static_cast<std::size_t>(group)] = static_cast<int>(mark_survival_.size());
        mark_survival_length_[static_cast<std::size_t>(group)] = length_count;
        for (int length = 0; length < length_count; ++length) {
            const int tau = std::max(length, 1);
            mark_survival_.push_back(std::max(fist.log_survival(viterbi::Kind::Dot, speed, tau),
                                              fist.log_survival(viterbi::Kind::Dash, speed, tau)));
        }
    }
    gap_survival_offset_.resize(static_cast<std::size_t>(fists_));
    gap_survival_length_.resize(static_cast<std::size_t>(fists_));
    for (int f = 0; f < fists_; ++f) {
        const int speed = speed_of_[static_cast<std::size_t>(f)];
        const int stepped = gap_speed[static_cast<std::size_t>(f)];
        const int k = weight_index[static_cast<std::size_t>(f)];
        const viterbi::Fist& own = fist_of[static_cast<std::size_t>(k)];
        const viterbi::Fist& gaps = fist_of[static_cast<std::size_t>(gap_weight(k))];
        const int longest_length = gap_hi_[static_cast<std::size_t>(f)] + 1;
        gap_survival_offset_[static_cast<std::size_t>(f)] = static_cast<int>(gap_survival_.size());
        gap_survival_length_[static_cast<std::size_t>(f)] = survival_index(longest_length) + 1;
        for (int length = 0; length <= longest_length; ++length) {
            if (length >= kSurvivalFineLengths && ((length - kSurvivalFineLengths) & 15) != 0) continue;
            const int tau = std::max(length, 1);
            gap_survival_.push_back(std::max({own.log_survival(viterbi::Kind::ElementGap, speed, tau),
                                              gaps.log_survival(viterbi::Kind::LetterGap, stepped, tau) - letter_gap_cost_,
                                              gaps.log_survival(viterbi::Kind::WordGap, stepped, tau) - word_gap_cost_}));
        }
    }

    const double flat = std::log(62.0);
    letter_end_.assign(static_cast<std::size_t>(nodes_), 0.0);
    if (model.symbol_prior_on_air) {
        const std::vector<double> prior = viterbi::on_air_log_prior(tree_, model.symbol_prior_floor);
        for (int node = 0; node < nodes_; ++node)
            if (std::isfinite(prior[static_cast<std::size_t>(node)]))
                letter_end_[static_cast<std::size_t>(node)] = prior[static_cast<std::size_t>(node)] + flat;
    }
    log_keep_ = std::log1p(-model.rescue_epsilon);
    letter_start_ = -flat + log_keep_;
    const double over_start = model.over_rate_per_s * model.sample_interval_s;
    log_birth_ = std::log(over_start);
    log_silence_ = std::log1p(-over_start);
    log_first_speed_ = -std::log(static_cast<double>(speeds_));
    log_rescue_ = std::log(model.rescue_epsilon);

    // The search loop's rows and flags: the values above, read together.
    has_symbol_.resize(static_cast<std::size_t>(nodes_));
    has_children_.resize(static_cast<std::size_t>(nodes_));
    for (int node = 0; node < nodes_; ++node) {
        has_symbol_[static_cast<std::size_t>(node)] = tree_.symbol(node).empty() ? 0 : 1;
        has_children_[static_cast<std::size_t>(node)] = child(node, 0) >= 0 || child(node, 1) >= 0 ? 1 : 0;
    }
    rows_.resize(static_cast<std::size_t>(fists_));
    for (int f = 0; f < fists_; ++f) {
        FistRow& r = rows_[static_cast<std::size_t>(f)];
        for (int kind = 0; kind < 5; ++kind) r.ends[static_cast<std::size_t>(kind)] = range(kind, f);
        const auto group = static_cast<std::size_t>(group_of(f));
        r.mark_survival_first = mark_survival_offset_[group];
        r.mark_survival_last = mark_survival_length_[group] - 1;
        r.gap_survival_first = gap_survival_offset_[static_cast<std::size_t>(f)];
        r.gap_survival_last = gap_survival_length_[static_cast<std::size_t>(f)] - 1;
        r.mark_hi = mark_hi(f);
        r.gap_hi = gap_hi(f);
        r.stretched = stretch_of(f) > 1.0;
    }
    open_hi_ = 0;
    for (int group = 0; group < group_count; ++group) open_hi_ = std::max(open_hi_, mark_hi_[static_cast<std::size_t>(group)]);
    open_end_first_.reserve(static_cast<std::size_t>(open_hi_) + 2);
    open_survival_.reserve(static_cast<std::size_t>(open_hi_) + 1);
    for (int length = 0; length <= open_hi_; ++length) {
        open_end_first_.push_back(static_cast<int>(open_ends_.size()));
        double survive = -kInf;
        for (int group = 0; group < group_count; ++group) {
            const int plain = first_of_group(group);
            const int k = group_weight_index[static_cast<std::size_t>(group)];
            survive = std::max(survive, mark_survival(plain, length));
            for (int label = 0; label < 2; ++label) {
                const int next = child(0, label);
                if (next < 0) continue;
                const Range& r = range(label, plain);
                if (length >= r.lo && length <= r.hi)
                    open_ends_.push_back(OpenEnd{log_end(label, plain, length) + log_weight[static_cast<std::size_t>(k)], plain,
                                                 group_speed_[static_cast<std::size_t>(group)], group, next,
                                                 first_of_group(group + 1) > plain + 1, k != 0});
            }
        }
        open_survival_.push_back(survive);
    }
    open_spacing_.resize(static_cast<std::size_t>(fists_));
    for (int f = 0; f < fists_; ++f)
        open_spacing_[static_cast<std::size_t>(f)] = static_cast<int>(config.open_spacing_dots * dot_s(speed_of(f)) / model.sample_interval_s);
    open_end_first_.push_back(static_cast<int>(open_ends_.size()));
}

namespace {

// An open mark or gap.
struct Element {
    double base;           // nats: see search.hpp
    std::int64_t start;    // the sample of its claimed edge
    std::int64_t letter;   // the sample of its letter's first key-down; -1 for a mark at the root (it starts the letter)
    std::int32_t text;     // the shared text before the current letter; held
    std::uint16_t speed;   // the fist, 0 ... fists - 1 (speed x weight x gap stretch; search.hpp); kOpenFist while open
    std::uint8_t node;     // a mark: the node it starts from; a gap: the node of the mark before it
    std::uint8_t kind : 4;   // ElementKind
    std::uint8_t space : 4;  // a birth's: a word space goes before its letter once the text goes on
};
static_assert(sizeof(Element) == 32);
constexpr std::uint16_t kOpenFist = 0xffff;

// All zeros is a slot never offered at, so a table of them is lazily zeroed memory: a channel pays for the slots it touches, which for a long-lived track is nearly all of them.
struct Slot {
    double score;
    std::int32_t parent;       // the element whose end it is
    std::uint32_t stamp_how;   // (the boundary it was last offered at's stamp) x 4 + GapEnd, for a mark's slot
};
static_assert(sizeof(Slot) == 16);

// A boundary's stamp, 1 ... 2^30 - 1, about 33 days of boundaries at 2.667 ms.
inline std::uint32_t stamp_of(std::int64_t j) { return static_cast<std::uint32_t>(j % 0x3fffffff) + 1u; }

struct FreeSlots {
    void operator()(Slot* slots) const { std::free(slots); }
};
using SlotTable = std::unique_ptr<Slot[], FreeSlots>;

// The last element opened in a state; all zeros for none.
struct LastOpened {
    double base;
    std::int64_t boundary_plus_1;
};
static_assert(sizeof(LastOpened) == 16);

struct FreeLastOpened {
    void operator()(LastOpened* last) const { std::free(last); }
};
using LastOpenedTable = std::unique_ptr<LastOpened[], FreeLastOpened>;

LastOpenedTable new_last_opened_table(std::size_t count) {
    auto* last = static_cast<LastOpened*>(std::calloc(count, sizeof(LastOpened)));
    if (last == nullptr) throw std::bad_alloc();
    return LastOpenedTable(last);
}

SlotTable new_slot_table(std::size_t count) {
    auto* slots = static_cast<Slot*>(std::calloc(count, sizeof(Slot)));
    if (slots == nullptr) throw std::bad_alloc();
    return SlotTable(slots);
}

}  // namespace

struct Search::Impl {
    SegmentalConfig config;
    const ModelTables& model;
    const RunningSums& sums;
    viterbi::SharedText text;
    SearchStats stats;

    std::int64_t next = 0;  // the next boundary
    double last_best = 0.0;  // nats: the best partial score at the last boundary (best_score)
    // The open elements, gaps and marks apart: a mark's slot's parent is always a gap, a gap's slot's parent a mark, and the kinds' loops need no branch on the kind.
    std::vector<Element> gaps;
    std::vector<Element> marks;  // marks and a birth's or a rescue's first marks
    SlotTable gap_slots;   // by fist x nodes + node: a mark ended, a gap starts
    SlotTable mark_slots;  // by fist x nodes + node: a gap ended, a mark starts
    // Each slot's runner-up at the boundary of its stamp, for the margins (SegmentalConfig::rival_margin_nats); none when that is 0.
    SlotTable gap_seconds;
    SlotTable mark_seconds;
    float rival_margin = 0.0f;  // nats
    LastOpenedTable gap_opened;   // by fist x nodes + node, read only with SegmentalConfig::open_spacing_dots
    LastOpenedTable mark_opened;
    std::vector<double> above_ceiling;  // the ceiling's scratch
    std::vector<std::int32_t> gap_touched;
    std::vector<std::int32_t> mark_touched;
    std::vector<double> gap_partial;
    std::vector<double> mark_partial;
    std::vector<Element> opened_gaps;
    std::vector<Element> opened_marks;
    // A birth's first mark's ends that wait for the best so far (boundary): at a group's g > 1 if it has other stretches, and at its
    // g = 1 too for a weight other than 0.
    struct Stretched {
        double end;  // nats: the end's score at g = 1
        std::int32_t parent;
        int group;   // (speed, weight)
        int child;
        bool plain;  // offer it at g = 1 too
        bool stretched;
    };
    std::vector<Stretched> stretched;

    bool silent_alive = true;
    double silent = 0.0;              // nats: the no-signal story's score
    std::int32_t silent_text = viterbi::SharedText::kEmpty;  // held
    // An over's end at j reaches the no-signal story at j + 6, so that a birth's rise window cannot overlap its fall
    // window (each would count the same samples): by (j + 6) mod 7, the over's end's score and text (held), or none.
    struct Tail {
        double score = -kInf;
        std::int32_t text = viterbi::SharedText::kEmpty;
        std::int64_t fall = -1;  // the over's last fall, whose tail it takes when it reaches the no-signal story
    };
    // By the boundary, mod kTailMax + 7. An over reaches the no-signal story 6 samples after its end, or, with the channel's tail of
    // n positions, n + 6 samples after, so that a birth's rise window cannot overlap the tail's samples (text-fast-1).
    std::array<Tail, kTailMax + kEdgeCount + 1> tails;
    // The times of samples: t = origin + m Delta after the last break
    std::vector<std::pair<std::int64_t, double>> breaks;

    const LevelTables* levels;
    // Per boundary: the correction to an element shorter than 6 samples that ends here, by [gap][length - 1], nats; a NaN
    // until asked for.
    std::array<std::array<double, kEdgeCount - 1>, 2> short_correction{};

    bool spacing_on = false;
    // Whether an element of this base may open in this state at j (SegmentalConfig::open_spacing_dots); if so, it is noted.
    static bool may_open(LastOpened& last, int spacing, double base, std::int64_t j) {
        if (last.boundary_plus_1 != 0 && j + 1 - last.boundary_plus_1 <= spacing && base <= last.base) return false;
        last.base = base;
        last.boundary_plus_1 = j + 1;
        return true;
    }


    double correction(bool gap, int length, std::int64_t j) {
        if (levels == nullptr) return 0.0;
        double& cached = short_correction[gap ? 1 : 0][static_cast<std::size_t>(length - 1)];
        if (std::isnan(cached)) {
            const std::int64_t i = j - length;
            const double whole = levels->short_score(gap, length, i, sums);
            cached = gap ? whole - sums.fall(i) - sums.rise(j)
                         : whole - (sums.rise(i) + sums.sum(j + kEdgeFirst) - sums.sum(i + kEdgeDone) + sums.fall(j));
        }
        return cached;
    }

    Impl(const SegmentalConfig& c, const ModelTables& m, const RunningSums& s, const LevelTables* l)
        : config(c), model(m), sums(s), levels(l) {
        const std::size_t slots = static_cast<std::size_t>(m.fists()) * static_cast<std::size_t>(m.nodes());
        gap_slots = new_slot_table(slots);
        mark_slots = new_slot_table(slots);
        if (c.rival_margin_nats > 0.0) {
            gap_seconds = new_slot_table(slots);
            mark_seconds = new_slot_table(slots);
            rival_margin = static_cast<float>(c.rival_margin_nats);
            text.set_no_rival_margin(rival_margin);
        }
        spacing_on = c.open_spacing_dots > 0.0;
        if (spacing_on) {
            gap_opened = new_last_opened_table(slots);
            mark_opened = new_last_opened_table(slots);
        }
        gap_partial.reserve(4096);
        mark_partial.reserve(1024);
    }

    double time_of(std::int64_t m) const {
        if (breaks.empty()) return static_cast<double>(m) * model.sample_interval_s();
        auto it = breaks.end();
        while (it != breaks.begin() && std::prev(it)->first > m) --it;
        if (it == breaks.begin()) return static_cast<double>(m) * model.sample_interval_s();
        const auto& b = *std::prev(it);
        return b.second + static_cast<double>(m - b.first) * model.sample_interval_s();
    }

    std::size_t slot_index(int fist, int node) const {
        return static_cast<std::size_t>(fist) * static_cast<std::size_t>(model.nodes()) + static_cast<std::size_t>(node);
    }

    // seconds: the runner-up's table, or none. A runner-up is valid only at its own stamp, so a new boundary needs no clearing.
    static void offer(Slot* slots, Slot* seconds, std::vector<std::int32_t>& touched, std::size_t index, double score,
                      std::int32_t parent, std::uint32_t how, std::uint32_t stamp) {
        Slot& slot = slots[index];
        if ((slot.stamp_how >> 2) != stamp) {
            slot.stamp_how = stamp << 2 | how;
            slot.score = score;
            slot.parent = parent;
            touched.push_back(static_cast<std::int32_t>(index));
        } else if (score > slot.score) {
            if (seconds != nullptr) seconds[index] = slot;
            slot.stamp_how = stamp << 2 | how;
            slot.score = score;
            slot.parent = parent;
        } else if (seconds != nullptr) {
            Slot& second = seconds[index];
            if ((second.stamp_how >> 2) != stamp || score > second.score) {
                second.stamp_how = stamp << 2 | how;
                second.score = score;
                second.parent = parent;
            }
        }
    }

    // The runner-up of a slot this boundary, within rival_margin of its winner, or none.
    const Slot* rival_of(const Slot* seconds, std::size_t index, const Slot& winner, std::uint32_t stamp) const {
        if (seconds == nullptr) return nullptr;
        const Slot& second = seconds[index];
        if ((second.stamp_how >> 2) != stamp || !(winner.score - second.score < rival_margin)) return nullptr;
        return &second;
    }

    // A symbol a rival's text ends with that is not yet in the shared text (a word space, a letter just ended), with its times, s.
    struct Pending {
        std::string_view symbol;
        double start_s = 0.0;
        double end_s = 0.0;
    };

    // A rival, `deficit` nats behind the story whose text ends at `winner`, read the text that ends at rival_base followed by
    // extra[0 .. extras). Both stories stand at one boundary, so their symbols are compared by time, from there back: a symbol of
    // each with the same text, starting and ending within half the shorter's length (at least 30 ms) of the other's, agree. Of
    // two that do not, the one that ends later is unmatched: the winner's takes the deficit as its margin if lower
    // (SharedText::lower_margin), and the rival's lowers the winner's symbol beside it if that one overlaps it or ends where it
    // begins (a word space the winner has not: the letter before it). This goes on past their common end until three agree in a
    // row, an entry both share, or kMarginSymbols steps. A soft-output Viterbi's update (Hagenauer and Hoeher 1989) at the
    // symbols rather than the bits; the match by time, its tolerances and the three are heuristics.
    void note_rival(std::int32_t winner, std::int32_t rival_base, const std::array<Pending, 2>& extra, int extras, double deficit) {
        constexpr int kMarginSymbols = 24;  // heuristic: a call with a word or two on either side
        constexpr double kTouch = 2e-3;      // s: less than a sample, 2.667 ms
        const float nats = static_cast<float>(std::max(0.0, deficit));
        std::int32_t w = winner;
        std::int32_t r = rival_base;
        int v = extras;  // the rival's pending symbols left; extra[v - 1] is its tip
        const auto r_valid = [&] { return v > 0 || r != viterbi::SharedText::kEmpty; };
        const auto r_step = [&] {
            if (v > 0) --v;
            else r = text.previous(r);
        };
        const auto r_pending = [&] {
            return v > 0 ? extra[static_cast<std::size_t>(v - 1)] : Pending{text.symbol(r), text.start_s(r), text.end_s(r)};
        };
        const auto agree = [](const Pending& a, const Pending& b) {
            if (a.symbol != b.symbol) return false;
            const double tolerance = std::max(0.03, 0.5 * std::min(a.end_s - a.start_s, b.end_s - b.start_s));
            return std::abs(a.start_s - b.start_s) <= tolerance && std::abs(a.end_s - b.end_s) <= tolerance;
        };
        bool differed = false;
        int agreed = 0;
        for (int steps = 0; steps < kMarginSymbols && w != viterbi::SharedText::kEmpty; ++steps) {
            if (v == 0 && r == w) return;
            const Pending a{text.symbol(w), text.start_s(w), text.end_s(w)};
            if (!r_valid()) {
                text.lower_margin(w, nats);
                w = text.previous(w);
                continue;
            }
            const Pending b = r_pending();
            if (agree(a, b)) {
                if (differed && ++agreed >= 3) return;
                w = text.previous(w);
                r_step();
                continue;
            }
            differed = true;
            agreed = 0;
            if (b.end_s > a.end_s + kTouch) {
                // The rival's symbol is unmatched: the winner's before it, if it reaches it.
                if (a.end_s >= b.start_s - kTouch) text.lower_margin(w, nats);
                r_step();
            } else {
                text.lower_margin(w, nats);
                w = text.previous(w);
            }
        }
    }

    // A mark slot's runner-up (a gap that ended into it) against the text its winner opened with, at boundary j.
    void note_mark_rival(std::int32_t winner, const Slot& rival, double deficit, std::int64_t j) {
        const Element& other = gaps[static_cast<std::size_t>(rival.parent)];
        const std::uint32_t how = rival.stamp_how & 3u;
        if (how == kEndElement) {
            note_rival(winner, other.text, {}, 0, deficit);
            return;
        }
        const Pending letter{model.tree().symbol(other.node), time_of(other.letter), time_of(other.start)};
        const Pending space{" ", time_of(other.start), time_of(j)};
        note_rival(winner, other.text, {letter, space}, how == kEndWord ? 2 : 1, deficit);
    }

    void boundary(std::int64_t j);
};

void Search::Impl::boundary(std::int64_t j) {
    const std::uint32_t stamp = stamp_of(j);
    Slot* const gap_table = gap_slots.get();
    Slot* const mark_table = mark_slots.get();
    Slot* const gap_second = gap_seconds.get();
    Slot* const mark_second = mark_seconds.get();
    const double c5 = sums.sum(j + kEdgeFirst);
    const double c11 = sums.sum(j + kEdgeDone);
    const double fall = sums.fall(j);
    const double rise = sums.rise(j);
    const double mark_end = c5 + fall;
    // The value of the evidence through the horizon H = j + 16 that a story in a gap, or in a mark, can still reach, whatever
    // the story: it stays there, or changes once at j + d, d = 1 ... 6 (search.hpp).
    const double horizon = sums.sum(j + kHorizon + 1);
    // By the first d the element's age allows (an element is at least 6 samples long): [d - 1] holds the reach over d ... 6.
    std::array<double, kEdgeCount> gap_reach_from;
    std::array<double, kEdgeCount> mark_reach_from;
    // [d - 1]: what a gap reaches by ending at j + d, the tail aside.
    std::array<double, kEdgeCount> gap_reach_at;
    {
        double gap_value = 0.0;
        double mark_value = horizon;
        for (int d = kEdgeCount; d >= 1; --d) {
            gap_reach_at[static_cast<std::size_t>(d - 1)] = sums.rise(j + d) + horizon - sums.sum(j + d + kEdgeDone);
            gap_value = std::max(gap_value, gap_reach_at[static_cast<std::size_t>(d - 1)]);
            mark_value = std::max(mark_value, sums.sum(j + d + kEdgeFirst) + sums.fall(j + d));
            gap_reach_from[static_cast<std::size_t>(d - 1)] = gap_value;
            mark_reach_from[static_cast<std::size_t>(d - 1)] = mark_value;
        }
    }
    const auto first_d = [](int length) { return static_cast<std::size_t>(std::max(1, kEdgeCount - length) - 1); };
    const double gap_reach = gap_reach_from[0];      // an element at least 5 samples old, and the no-signal story
    const double gap_reach_new = gap_reach_from[kEdgeCount - 1];    // a gap that starts here
    const double mark_reach_new = mark_reach_from[kEdgeCount - 1];  // a mark that starts here
    // The channel's tail (text-fast-1): what a gap reaches through the horizon with its tail, ending at j + d (its tail then
    // n = length + d - 6 positions, the next rise's window after them) or going on (n = length + 6); without a tail, gap_reach_from.
    const auto gap_reach_with_tail = [&](std::int64_t start, int length) {
        const int filled = sums.tail_filled(start);
        const std::size_t first = first_d(length);
        if (filled == 0) return gap_reach_from[first];
        if (length - kEdgeCount + 1 >= filled) return gap_reach_from[first] + sums.tail(start, filled);
        double value = sums.tail(start, length + kEdgeCount);
        for (std::size_t d = first; d < static_cast<std::size_t>(kEdgeCount); ++d)
            value = std::max(value, gap_reach_at[d] + sums.tail(start, length + static_cast<int>(d) + 1 - kEdgeCount));
        return value;
    };
    // A gap that starts here, less gap_reach_new: 0 with no tail.
    const double tail_reach_new = gap_reach_with_tail(j, 0) - gap_reach_new;
    const std::int32_t gap_count = static_cast<std::int32_t>(gaps.size());
    const std::int32_t mark_count = static_cast<std::int32_t>(marks.size());
    gap_partial.resize(gaps.size());
    mark_partial.resize(marks.size());
    for (auto& by_kind : short_correction) by_kind.fill(std::numeric_limits<double>::quiet_NaN());
    gap_touched.clear();
    mark_touched.clear();
    double best = -kInf;
    const double* const log_ends = model.log_ends();
    const double* const mark_survivals = model.mark_survivals();
    const double* const gap_survivals = model.gap_survivals();
    const std::uint8_t* const symbol_at = model.symbol_at_nodes();
    const std::uint8_t* const children_at = model.children_at_nodes();
    const std::size_t nodes = static_cast<std::size_t>(model.nodes());
    const double letter_start = model.letter_start();
    const double letter_gap_cost = model.letter_gap_cost();
    const double word_gap_cost = model.word_gap_cost();
    const auto ends_at = [](const ModelTables::Range& r, int length) { return static_cast<unsigned>(length - r.lo) <= static_cast<unsigned>(r.hi - r.lo); };
    const auto end_cost = [log_ends](const ModelTables::Range& r, int length) { return log_ends[r.offset + length - r.lo]; };
    // The best partial score and whose, kept for the no-signal story should nothing stand (5. below).
    double best_dropped = -kInf;
    std::int32_t best_dropped_text = viterbi::SharedText::kEmpty;

    // 1. The open elements: each offers its ends here, and its partial score.
    for (std::int32_t index = 0; index < gap_count; ++index) {
        const Element& e = gaps[static_cast<std::size_t>(index)];
        const int length = static_cast<int>(j - e.start);
        const ModelTables::FistRow& row = model.row(e.speed);
        const int node = e.node;
        const double score = e.base + gap_survivals[row.gap_survival_first + std::min(ModelTables::survival_index(length), row.gap_survival_last)] + gap_reach_with_tail(e.start, length);
        const ModelTables::Range& r = row.ends[kElementGap];
        const ModelTables::Range& lg = row.ends[kLetterGap];
        const ModelTables::Range& wg = row.ends[kWordGap];
        const bool element_ends = (children_at[node] != 0) & ends_at(r, length);
        const bool has_letter = symbol_at[node] != 0;
        const bool letter_ends = has_letter & ends_at(lg, length), word_ends = has_letter & ends_at(wg, length);
        if (element_ends | letter_ends | word_ends) {
            const double ended_gap = length < kEdgeCount ? e.base + correction(true, length, j) : e.base + sums.tail(e.start, length - kEdgeCount);
            const std::size_t first_slot = static_cast<std::size_t>(e.speed) * nodes;
            if (element_ends)
                offer(mark_table, mark_second, mark_touched, first_slot + static_cast<std::size_t>(node), ended_gap + end_cost(r, length),
                      index, kEndElement, stamp);
            if (letter_ends || word_ends) {
                const double letter = ended_gap + model.letter_end(node) + letter_start;
                if (letter_ends)
                    offer(mark_table, mark_second, mark_touched, first_slot, letter - letter_gap_cost + end_cost(lg, length), index, kEndLetter, stamp);
                if (word_ends)
                    offer(mark_table, mark_second, mark_touched, first_slot, letter - word_gap_cost + end_cost(wg, length), index, kEndWord, stamp);
            }
        }
        gap_partial[static_cast<std::size_t>(index)] = score;
        if (score > best) {
            best = score;
            best_dropped = score;
            best_dropped_text = e.text;
        }
    }
    for (std::int32_t index = 0; index < mark_count; ++index) {
        const Element& e = marks[static_cast<std::size_t>(index)];
        const int length = static_cast<int>(j - e.start);
        double score;
        if (e.kind == kMark) {
            const ModelTables::FistRow& row = model.row(e.speed);
            const int node = e.node;
            score = e.base + mark_reach_from[first_d(length)] + mark_survivals[row.mark_survival_first + std::min(length, row.mark_survival_last)];
            const ModelTables::Range& dot = row.ends[kDot];
            const ModelTables::Range& dash = row.ends[kDash];
            const bool dot_ends = ends_at(dot, length), dash_ends = ends_at(dash, length);
            if (dot_ends || dash_ends) {
                const double ended = length < kEdgeCount && length >= dot.lo ? e.base + mark_end + correction(false, length, j)
                                                                             : e.base + mark_end;
                const std::size_t first_slot = static_cast<std::size_t>(e.speed) * nodes;
                if (dot_ends) {
                    const int child = model.child(node, 0);
                    if (child >= 0) offer(gap_table, gap_second, gap_touched, first_slot + static_cast<std::size_t>(child), ended + end_cost(dot, length), index, 0, stamp);
                }
                if (dash_ends) {
                    const int child = model.child(node, 1);
                    if (child >= 0) offer(gap_table, gap_second, gap_touched, first_slot + static_cast<std::size_t>(child), ended + end_cost(dash, length), index, 0, stamp);
                }
            }
        } else {  // a birth's first mark: the best speed's survival, and its end offers every speed, each at g = 1 here
            const std::span<const ModelTables::OpenEnd> ends = model.open_ends(length);
            if (!ends.empty()) {
                const double ended = e.base + mark_end + model.log_first_speed() + model.log_plain() +
                                     (length < kEdgeCount ? correction(false, length, j) : 0.0);
                for (const ModelTables::OpenEnd& open : ends) {
                    const double end = ended + open.log_end;
                    if (open.weighted) {
                        stretched.push_back(Stretched{end, index, open.group, open.child, true, open.stretched});
                        continue;
                    }
                    offer(gap_table, gap_second, gap_touched, static_cast<std::size_t>(open.fist) * nodes + static_cast<std::size_t>(open.child), end, index, 0, stamp);
                    if (open.stretched) stretched.push_back(Stretched{end, index, open.group, open.child, false, true});
                }
            }
            score = e.base + mark_reach_from[first_d(length)] + model.open_survival(length) + model.log_first_speed();
        }
        mark_partial[static_cast<std::size_t>(index)] = score;
        if (score > best) {
            best = score;
            best_dropped = score;
            best_dropped_text = e.text;
        }
    }
    stats.touches += gap_count + mark_count;

    // The same ends at the other stretches, and at the other weights. Only those inside the beam of the best so far, which the final
    // best can only raise: this keeps the bookkeeping of a stretch or weight nobody needs off every birth on noise, without changing
    // what opens.
    {
        // As the opening test below: a gap opened here reaches gap_reach_new and its tail's (review of text-fast-1).
        const double plain_floor = best - config.beam_nats - gap_reach_new - tail_reach_new;
        const double stretched_floor = plain_floor - (model.log_stretched() - model.log_plain());
        for (const Stretched& s : stretched) {
            const int first = model.first_of_group(s.group);
            if (s.plain && s.end >= plain_floor) offer(gap_table, gap_second, gap_touched, slot_index(first, s.child), s.end, s.parent, 0, stamp);
            if (!s.stretched || s.end < stretched_floor) continue;
            const double score = s.end - model.log_plain() + model.log_stretched();
            for (int f = first + 1; f < model.first_of_group(s.group + 1); ++f)
                offer(gap_table, gap_second, gap_touched, slot_index(f, s.child), score, s.parent, 0, stamp);
        }
        stretched.clear();
    }

    // 2. The no-signal story continues.
    if (silent_alive) {
        silent += model.log_silence();
        best = std::max(best, silent + gap_reach);
    }

    // 3. The slots' scores as partial scores: a gap's as it stands, a mark's with its rise.
    for (const std::int32_t index : gap_touched) {
        best = std::max(best, gap_slots[static_cast<std::size_t>(index)].score + gap_reach_new + tail_reach_new);
    }
    for (const std::int32_t index : mark_touched) {
        best = std::max(best, mark_slots[static_cast<std::size_t>(index)].score + rise - c11 + mark_reach_new);
    }

    // The over's ends: from each mark's end at a node with a symbol, into the tail that reaches the no-signal story at
    // j + 6; and the tail that reaches it now.
    Tail& tail_now = tails[static_cast<std::size_t>(j % static_cast<std::int64_t>(tails.size()))];
    if (tail_now.score > -kInf) {
        if (tail_now.fall >= 0) tail_now.score += sums.tail(tail_now.fall, kTailMax);
        if (!silent_alive || tail_now.score > silent) {
            text.release(silent_text);
            silent_text = tail_now.text;
            silent = tail_now.score;
            silent_alive = true;
        } else {
            text.release(tail_now.text);
        }
        tail_now = Tail{};
    }
    const double before = silent;  // a birth here comes from the no-signal story as it now stands
    const bool before_alive = silent_alive;
    std::int32_t silent_parent = -1;
    int silent_node = 0;
    double silent_end = -kInf;
    // The runner-up among the over's ends, for the margins.
    std::int32_t second_parent = -1;
    int second_node = 0;
    double second_end = -kInf;
    for (const std::int32_t index : gap_touched) {
        const int node = index % model.nodes();
        if (!model.has_symbol(node)) continue;
        const double score = gap_slots[static_cast<std::size_t>(index)].score + model.letter_end(node) - model.over_end_cost();
        if (score > silent_end) {
            second_end = silent_end;
            second_parent = silent_parent;
            second_node = silent_node;
            silent_end = score;
            silent_parent = gap_slots[static_cast<std::size_t>(index)].parent;
            silent_node = node;
        } else if (score > second_end) {
            second_end = score;
            second_parent = gap_slots[static_cast<std::size_t>(index)].parent;
            second_node = node;
        }
    }
    if (silent_alive) best = std::max(best, silent + gap_reach);
    if (silent_end > -kInf) best = std::max(best, silent_end + gap_reach_new + tail_reach_new);
    if (best == -kInf) best = 0.0;
    last_best = best;
    const double floor = best - config.beam_nats;

    // 4. Open the new elements inside the beam.
    opened_gaps.clear();
    opened_marks.clear();
    for (const std::int32_t index : gap_touched) {
        const Slot& slot = gap_slots[static_cast<std::size_t>(index)];
        if (slot.score + gap_reach_new + tail_reach_new < floor) continue;
        if (spacing_on && !may_open(gap_opened[static_cast<std::size_t>(index)], model.open_spacing(index / model.nodes()), slot.score, j)) continue;
        const Element& parent = marks[static_cast<std::size_t>(slot.parent)];
        Element e{};
        e.base = slot.score;
        e.start = j;
        e.kind = kGap;
        e.speed = static_cast<std::uint16_t>(index / model.nodes());
        e.node = static_cast<std::uint8_t>(index % model.nodes());
        e.letter = parent.letter < 0 ? parent.start : parent.letter;
        e.space = 0;
        if (parent.space) {
            const double t = time_of(parent.start);
            e.text = text.append(parent.text, " ", t, t);
        } else {
            e.text = parent.text;
            text.hold(e.text);
        }
        if (const Slot* rival = rival_of(gap_second, static_cast<std::size_t>(index), slot, stamp)) {
            const Element& other = marks[static_cast<std::size_t>(rival->parent)];
            const double t = time_of(other.start);
            note_rival(e.text, other.text, {Pending{" ", t, t}, Pending{}}, other.space ? 1 : 0, slot.score - rival->score);
        }
        opened_gaps.push_back(e);
    }
    for (const std::int32_t index : mark_touched) {
        const Slot& slot = mark_slots[static_cast<std::size_t>(index)];
        if (slot.score + rise - c11 + mark_reach_new < floor) continue;
        if (spacing_on && !may_open(mark_opened[static_cast<std::size_t>(index)], model.open_spacing(index / model.nodes()), slot.score + rise - c11, j)) continue;
        const Element& parent = gaps[static_cast<std::size_t>(slot.parent)];
        Element e{};
        e.base = slot.score + rise - c11;
        e.start = j;
        e.kind = kMark;
        e.speed = parent.speed;
        e.node = static_cast<std::uint8_t>(index % model.nodes());
        e.space = 0;
        if ((slot.stamp_how & 3u) == kEndElement) {
            e.letter = parent.letter;
            e.text = parent.text;
            text.hold(e.text);
        } else {
            e.letter = -1;
            e.text = text.append(parent.text, model.tree().symbol(parent.node), time_of(parent.letter), time_of(parent.start));
            if ((slot.stamp_how & 3u) == kEndWord) {
                const std::int32_t letter_entry = e.text;
                e.text = text.append(letter_entry, " ", time_of(parent.start), time_of(j));
                text.release(letter_entry);
            }
        }
        if (const Slot* rival = rival_of(mark_second, static_cast<std::size_t>(index), slot, stamp)) note_mark_rival(e.text, *rival, slot.score - rival->score, j);
        opened_marks.push_back(e);
    }
    // A rescue: the sender takes a new speed at a letter's start, from the best story that starts a letter here.
    {
        double best_start = -kInf;
        std::int32_t best_index = -1;
        for (const std::int32_t index : mark_touched) {
            if (index % model.nodes() != 0) continue;
            const double score = mark_slots[static_cast<std::size_t>(index)].score;
            if (score > best_start) {
                best_start = score;
                best_index = index;
            }
        }
        if (best_index >= 0) {
            const double score = best_start - model.log_keep() + model.log_rescue();
            if (score + rise - c11 + mark_reach_new + model.log_first_speed() >= floor) {
                const Slot& slot = mark_slots[static_cast<std::size_t>(best_index)];
                const Element& parent = gaps[static_cast<std::size_t>(slot.parent)];
                Element e{};
                e.base = score + rise - c11;
                e.start = j;
                e.kind = kOpenMark;
                e.speed = kOpenFist;
                e.node = 0;
                e.space = 0;
                e.letter = -1;
                e.text = text.append(parent.text, model.tree().symbol(parent.node), time_of(parent.letter), time_of(parent.start));
                if ((slot.stamp_how & 3u) == kEndWord) {
                    const std::int32_t letter_entry = e.text;
                    e.text = text.append(letter_entry, " ", time_of(parent.start), time_of(j));
                    text.release(letter_entry);
                }
                if (const Slot* rival = rival_of(mark_second, static_cast<std::size_t>(best_index), slot, stamp))
                    note_mark_rival(e.text, *rival, slot.score - rival->score, j);
                opened_marks.push_back(e);
            }
        }
    }
    // A birth's first mark, with the no-signal story's text.
    if (before_alive) {
        const double score = before + model.log_birth() + model.letter_start();
        if (score + rise - c11 + mark_reach_new >= floor) {
            Element e{};
            e.base = score + rise - c11;
            e.start = j;
            e.letter = -1;
            e.kind = kOpenMark;
            e.speed = kOpenFist;
            e.node = 0;
            e.space = silent_text != viterbi::SharedText::kEmpty ? 1 : 0;
            e.text = silent_text;
            text.hold(e.text);
            opened_marks.push_back(e);
        }
    }
    // The over's end that won the no-signal story.
    if (silent_parent >= 0) {
        const Element& parent = marks[static_cast<std::size_t>(silent_parent)];
        std::int32_t base_text = parent.text;
        std::int32_t held = viterbi::SharedText::kEmpty;
        if (parent.space) {
            const double t = time_of(parent.start);
            held = text.append(base_text, " ", t, t);
            base_text = held;
        }
        const std::int64_t letter = parent.letter < 0 ? parent.start : parent.letter;
        const std::int32_t entry = text.append(base_text, model.tree().symbol(silent_node), time_of(letter), time_of(j));
        if (held != viterbi::SharedText::kEmpty) text.release(held);
        if (gap_second != nullptr && second_parent >= 0 && silent_end - second_end < rival_margin) {
            const Element& other = marks[static_cast<std::size_t>(second_parent)];
            const double t = time_of(other.start);
            const Pending letter_end{model.tree().symbol(second_node), time_of(other.letter < 0 ? other.start : other.letter), time_of(j)};
            if (other.space) note_rival(entry, other.text, {Pending{" ", t, t}, letter_end}, 2, silent_end - second_end);
            else note_rival(entry, other.text, {letter_end, Pending{}}, 1, silent_end - second_end);
        }
        // A birth's rise window starts 5 samples after it, so the tail's last position, 10 + n after the fall, must stand before it:
        // n the tail of the fall claimed at j, not of the last sample pushed, which at the stream's end is a flush sample with no tail
        // (text-tail-2: a mark the end cut off read as a row of one-sample E's).
        const int tail_delay = std::max(kEdgeCount, sums.tail_length(j) + kEdgeCount);
        Tail& tail = tails[static_cast<std::size_t>((j + tail_delay) % static_cast<std::int64_t>(tails.size()))];
        const double score = silent_end + tail_delay * model.log_silence();
        // An over's end due at the same boundary from a shorter tail: the better stays.
        if (tail.score > -kInf && tail.score + (tail.fall >= 0 ? sums.tail(tail.fall, kTailMax) : 0.0) >= score + sums.tail(j, kTailMax)) {
            text.release(entry);
        } else {
            if (tail.score > -kInf) text.release(tail.text);
            tail.score = score;
            tail.text = entry;
            tail.fall = j;
        }
    }

    // 5. Prune: the beam, then elements past their longest length. The best one's text is held until the end of this
    // boundary, for the no-signal story to go on with should nothing else stand (a long carrier outlasts every element).
    text.hold(best_dropped_text);
    std::size_t kept_gaps = 0;
    for (std::int32_t index = 0; index < gap_count; ++index) {
        Element& e = gaps[static_cast<std::size_t>(index)];
        const int length = static_cast<int>(j - e.start);
        if (gap_partial[static_cast<std::size_t>(index)] >= floor && length < model.row(e.speed).gap_hi) {
            gap_partial[kept_gaps] = gap_partial[static_cast<std::size_t>(index)];
            if (kept_gaps != static_cast<std::size_t>(index)) gaps[kept_gaps] = e;
            ++kept_gaps;
        } else {
            text.release(e.text);
        }
    }
    std::size_t kept_marks = 0;
    const int open_hi = model.open_hi();
    for (std::int32_t index = 0; index < mark_count; ++index) {
        Element& e = marks[static_cast<std::size_t>(index)];
        const int length = static_cast<int>(j - e.start);
        if (mark_partial[static_cast<std::size_t>(index)] >= floor && length < (e.kind == kMark ? model.row(e.speed).mark_hi : open_hi)) {
            mark_partial[kept_marks] = mark_partial[static_cast<std::size_t>(index)];
            if (kept_marks != static_cast<std::size_t>(index)) marks[kept_marks] = e;
            ++kept_marks;
        } else {
            text.release(e.text);
        }
    }
    gaps.resize(kept_gaps);
    marks.resize(kept_marks);
    stats.opened += static_cast<std::int64_t>(opened_gaps.size() + opened_marks.size());
    gaps.insert(gaps.end(), opened_gaps.begin(), opened_gaps.end());
    marks.insert(marks.end(), opened_marks.begin(), opened_marks.end());

    // The ceiling, on what stands: the new ones count at their own partial scores.
    const std::size_t ceiling = static_cast<std::size_t>(config.ceiling);
    if (gaps.size() + marks.size() > ceiling) {
        gap_partial.resize(gaps.size());
        for (std::size_t i = kept_gaps; i < gaps.size(); ++i) gap_partial[i] = gaps[i].base + gap_reach_new + tail_reach_new;
        mark_partial.resize(marks.size());
        for (std::size_t i = kept_marks; i < marks.size(); ++i) mark_partial[i] = marks[i].base + mark_reach_new;
        if (config.ceiling_stretch_bonus_nats > 0.0) {
            for (std::size_t i = 0; i < gaps.size(); ++i)
                if (model.row(gaps[i].speed).stretched) gap_partial[i] += config.ceiling_stretch_bonus_nats;
            for (std::size_t i = 0; i < marks.size(); ++i)
                if (marks[i].kind == kMark && model.row(marks[i].speed).stretched) mark_partial[i] += config.ceiling_stretch_bonus_nats;
        }
        // The ceiling's best stand, the earlier of a tie first, gaps before marks: the ceiling-th score over plain doubles, then one pass.
        above_ceiling.assign(gap_partial.begin(), gap_partial.end());
        above_ceiling.insert(above_ceiling.end(), mark_partial.begin(), mark_partial.end());
        const auto last_kept = above_ceiling.begin() + static_cast<std::ptrdiff_t>(ceiling) - 1;
        std::nth_element(above_ceiling.begin(), last_kept, above_ceiling.end(), std::greater<double>());
        const double lowest = *last_kept;
        const auto above = [lowest](double p) { return p > lowest; };
        std::size_t ties_left = ceiling - static_cast<std::size_t>(std::count_if(gap_partial.begin(), gap_partial.end(), above)) -
                                static_cast<std::size_t>(std::count_if(mark_partial.begin(), mark_partial.end(), above));
        const auto keep_best = [&](std::vector<Element>& elements, const std::vector<double>& scores) {
            std::size_t out = 0;
            for (std::size_t i = 0; i < elements.size(); ++i) {
                bool stand = scores[i] > lowest;
                if (!stand && scores[i] == lowest && ties_left > 0) {
                    stand = true;
                    --ties_left;
                }
                if (stand) {
                    if (out != i) elements[out] = elements[i];
                    ++out;
                } else {
                    text.release(elements[i].text);
                }
            }
            elements.resize(out);
        };
        keep_best(gaps, gap_partial);
        keep_best(marks, mark_partial);
    }

    // The no-signal story leaves when behind the beam, except when nothing else stands.
    if (silent_alive && silent + gap_reach < floor) silent_alive = false;
    bool tail_waits = false;
    for (const Tail& tail : tails) tail_waits = tail_waits || tail.score > -kInf;
    if (!silent_alive && gaps.empty() && marks.empty() && !tail_waits) {
        silent_alive = true;
        silent = best;
        if (best_dropped > -kInf) {
            text.release(silent_text);
            silent_text = best_dropped_text;
            text.hold(silent_text);
        }
    }
    text.release(best_dropped_text);
    stats.largest_live = std::max(stats.largest_live, static_cast<std::int64_t>(gaps.size() + marks.size()));
    ++stats.boundaries;
}

Search::Search(const SegmentalConfig& config, const ModelTables& tables, const RunningSums& sums, const LevelTables* levels)
    : impl_(std::make_unique<Impl>(config, tables, sums, levels)) {}
Search::~Search() = default;
Search::Search(Search&&) noexcept = default;
Search& Search::operator=(Search&&) noexcept = default;

void Search::advance() {
    Impl& s = *impl_;
    const std::int64_t last = s.sums.committed() - kHorizon - 1;
    while (s.next <= last) s.boundary(s.next++);
}

void Search::note_sample_time(std::int64_t m, double t_s) {
    impl_->breaks.emplace_back(m, t_s);
}

std::vector<DecodedSymbol> Search::best_text() const {
    const Impl& s = *impl_;
    // The best story through the last boundary searched.
    const std::int64_t j = s.next - 1;
    double best = s.silent_alive ? s.silent : -kInf;
    const Element* winner = nullptr;
    std::int32_t silent_text = s.silent_text;
    for (const Impl::Tail& tail : s.tails) {
        if (tail.score > best) {
            best = tail.score;
            silent_text = tail.text;
        }
    }
    if (j >= 0) {
        const double c11 = s.sums.sum(j + kEdgeDone);
        for (const std::vector<Element>* elements : {&s.gaps, &s.marks}) {
            for (const Element& e : *elements) {
            const int length = static_cast<int>(j - e.start);
            double score;
            if (e.kind == kGap) {
                score = e.base + s.model.gap_survival(e.speed, length);
            } else if (e.kind == kMark) {
                score = e.base + c11 + s.model.mark_survival(e.speed, length);
            } else {
                score = e.base + c11 + s.model.log_first_speed();
            }
            if (score > best) {
                best = score;
                winner = &e;
            }
            }
        }
    }
    if (winner == nullptr) return s.text.read(silent_text);
    std::vector<DecodedSymbol> out = s.text.read(winner->text);
    if (winner->kind == kGap && s.model.has_symbol(winner->node)) {
        out.push_back(DecodedSymbol{std::string(s.model.tree().symbol(winner->node)), 1.0f, s.time_of(winner->letter),
                                    s.time_of(winner->start)});
    }
    return out;
}

BestStory Search::best_story(std::size_t max_symbols) const {
    const Impl& s = *impl_;
    // The same choice as best_text's.
    const std::int64_t j = s.next - 1;
    double best = s.silent_alive ? s.silent : -kInf;
    const Element* winner = nullptr;
    std::int32_t silent_text = s.silent_text;
    for (const Impl::Tail& tail : s.tails) {
        if (tail.score > best) {
            best = tail.score;
            silent_text = tail.text;
        }
    }
    if (j >= 0) {
        const double c11 = s.sums.sum(j + kEdgeDone);
        for (const std::vector<Element>* elements : {&s.gaps, &s.marks}) {
            for (const Element& e : *elements) {
            const int length = static_cast<int>(j - e.start);
            double score;
            if (e.kind == kGap) {
                score = e.base + s.model.gap_survival(e.speed, length);
            } else if (e.kind == kMark) {
                score = e.base + c11 + s.model.mark_survival(e.speed, length);
            } else {
                score = e.base + c11 + s.model.log_first_speed();
            }
            if (score > best) {
                best = score;
                winner = &e;
            }
            }
        }
    }
    BestStory story;
    if (winner == nullptr) {
        story.tail = s.text.read_tail(silent_text, max_symbols);
        story.length = s.text.length(silent_text);
        return story;
    }
    if (winner->kind != kOpenMark) story.speed = s.model.speed_of(winner->speed);
    story.length = s.text.length(winner->text);
    const bool open_letter = winner->kind == kGap && s.model.has_symbol(winner->node);
    story.tail = s.text.read_tail(winner->text, open_letter && max_symbols > 0 ? max_symbols - 1 : max_symbols);
    if (open_letter) {
        story.open_letter = true;
        ++story.length;
        story.tail.push_back(DecodedSymbol{std::string(s.model.tree().symbol(winner->node)), 1.0f, s.time_of(winner->letter),
                                           s.time_of(winner->start)});
    }
    return story;
}

double Search::best_score() const { return impl_->last_best; }
const SearchStats& Search::stats() const { return impl_->stats; }
std::size_t Search::live() const { return impl_->gaps.size() + impl_->marks.size(); }

}  // namespace kz4ap::segmental
