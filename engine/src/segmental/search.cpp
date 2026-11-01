#include "kz4ap/segmental/search.hpp"

#include "../viterbi/text.hpp"
#include "kz4ap/viterbi/fist.hpp"
#include "kz4ap/viterbi/symbol_prior.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
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
    const int extra = (config.gap_stretches - 1) * config.gap_stretch_speed_steps;
    viterbi::ViterbiConfig longer = model;
    longer.speed_count = speeds_ + extra;
    const viterbi::Fist fist(longer);
    const auto dot_s = [&](int speed) { return model.dot_min_ms * std::pow(model.speed_ratio, speed) / 1000.0; };
    std::vector<int> gap_speed;
    first_fist_.resize(static_cast<std::size_t>(speeds_) + 1);
    for (int speed = 0; speed < speeds_; ++speed) {
        first_fist_[static_cast<std::size_t>(speed)] = static_cast<int>(speed_of_.size());
        for (int n = 0; n < config.gap_stretches; ++n) {
            const int stepped = speed + n * config.gap_stretch_speed_steps;
            if (n > 0 && dot_s(stepped) > config.gap_unit_max_s) break;
            speed_of_.push_back(speed);
            gap_speed.push_back(stepped);
            stretch_of_.push_back(dot_s(stepped) / dot_s(speed));
        }
    }
    fists_ = static_cast<int>(speed_of_.size());
    first_fist_[static_cast<std::size_t>(speeds_)] = fists_;
    if (fists_ > 254) throw std::invalid_argument("segmental: at most 254 fists (speed x gap stretch)");
    log_plain_ = config.gap_stretches > 1 ? std::log1p(-config.gap_stretch_prior) : 0.0;
    log_stretched_ = config.gap_stretches > 1 ? std::log(config.gap_stretch_prior / (config.gap_stretches - 1)) : 0.0;

    // Each kind's lengths per speed of its own: dots, dashes and element gaps at the speed itself, letter and word gaps at the
    // stepped speed.
    const int table_speeds = speeds_ + extra;
    std::vector<Range> by_speed(static_cast<std::size_t>(5 * table_speeds));
    for (int kind = 0; kind < 5; ++kind) {
        const double sigma = model.fist_sigmas[static_cast<std::size_t>(kind)];
        const int kind_speeds = kind < kLetterGap ? speeds_ : table_speeds;
        for (int speed = 0; speed < kind_speeds; ++speed) {
            if (kind >= kLetterGap && std::find(gap_speed.begin(), gap_speed.end(), speed) == gap_speed.end()) continue;
            const double dot_samples = dot_s(speed) / model.sample_interval_s;
            const double median = model.fist_medians_dots[static_cast<std::size_t>(kind)] * dot_samples;
            const double shift = model.fist_shifts_dots[static_cast<std::size_t>(kind)] * dot_samples;
            const int lo = std::max(1, static_cast<int>(std::floor(shift + median * std::exp(-config.duration_z_max * sigma))));
            const int hi = std::max(lo, static_cast<int>(std::ceil(shift + median * std::exp(config.duration_z_max * sigma))));
            by_speed[static_cast<std::size_t>(kind * table_speeds + speed)] = Range{lo, hi, static_cast<int>(log_end_.size())};
            for (int length = lo; length <= hi; ++length)
                log_end_.push_back(fist.log_end(static_cast<viterbi::Kind>(kind), speed, length));
        }
    }
    ranges_.resize(static_cast<std::size_t>(5 * fists_));
    for (int kind = 0; kind < 5; ++kind) {
        for (int f = 0; f < fists_; ++f) {
            const int speed = kind < kLetterGap ? speed_of_[static_cast<std::size_t>(f)] : gap_speed[static_cast<std::size_t>(f)];
            ranges_[static_cast<std::size_t>(kind * fists_ + f)] = by_speed[static_cast<std::size_t>(kind * table_speeds + speed)];
        }
    }

    const double p = model.word_end_p;
    const double q = model.over_end_q;
    letter_gap_cost_ = -std::log1p(-p);
    word_gap_cost_ = -(std::log(p) + std::log1p(-q));
    over_end_cost_ = -(std::log(p) + std::log(q));

    mark_hi_.assign(static_cast<std::size_t>(speeds_), 0);
    gap_hi_.assign(static_cast<std::size_t>(fists_), 0);
    for (int f = 0; f < fists_; ++f) {
        const int speed = speed_of_[static_cast<std::size_t>(f)];
        for (int kind = 0; kind < 5; ++kind) {
            const int hi = range(kind, f).hi;
            if (kind <= kDash) mark_hi_[static_cast<std::size_t>(speed)] = std::max(mark_hi_[static_cast<std::size_t>(speed)], hi);
            else gap_hi_[static_cast<std::size_t>(f)] = std::max(gap_hi_[static_cast<std::size_t>(f)], hi);
            longest_ = std::max(longest_, hi);
        }
    }

    mark_survival_offset_.resize(static_cast<std::size_t>(speeds_));
    mark_survival_length_.resize(static_cast<std::size_t>(speeds_));
    for (int speed = 0; speed < speeds_; ++speed) {
        // As far as a mark can run; a birth's first mark, which may yet be a slower speed's, clips here.
        const int length_count = mark_hi_[static_cast<std::size_t>(speed)] + 2;
        mark_survival_offset_[static_cast<std::size_t>(speed)] = static_cast<int>(mark_survival_.size());
        mark_survival_length_[static_cast<std::size_t>(speed)] = length_count;
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
        const int length_count = gap_hi_[static_cast<std::size_t>(f)] + 2;
        gap_survival_offset_[static_cast<std::size_t>(f)] = static_cast<int>(gap_survival_.size());
        gap_survival_length_[static_cast<std::size_t>(f)] = length_count;
        for (int length = 0; length < length_count; ++length) {
            const int tau = std::max(length, 1);
            gap_survival_.push_back(std::max({fist.log_survival(viterbi::Kind::ElementGap, speed, tau),
                                              fist.log_survival(viterbi::Kind::LetterGap, stepped, tau) - letter_gap_cost_,
                                              fist.log_survival(viterbi::Kind::WordGap, stepped, tau) - word_gap_cost_}));
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
}

namespace {

// An open mark or gap.
struct Element {
    double base;           // nats: see search.hpp
    std::int64_t start;    // the sample of its claimed edge
    std::int64_t letter;   // the sample of its letter's first key-down; -1 for a mark at the root (it starts the letter)
    std::int32_t text;     // the shared text before the current letter; held
    std::uint8_t speed;    // the fist, 0 ... fists - 1 (speed x gap stretch; search.hpp); 255 while open
    std::uint8_t node;     // a mark: the node it starts from; a gap: the node of the mark before it
    std::uint8_t kind;     // ElementKind
    std::uint8_t space;    // a birth's: a word space goes before its letter once the text goes on
};

struct Slot {
    double score = -kInf;
    std::int32_t parent = -1;   // the element whose end it is
    std::int32_t stamp = -1;    // the boundary it was last offered at, low 31 bits
    std::uint8_t how = 0;       // GapEnd, for a mark's slot
};

}  // namespace

struct Search::Impl {
    SegmentalConfig config;
    const ModelTables& model;
    const RunningSums& sums;
    viterbi::SharedText text;
    SearchStats stats;

    std::int64_t next = 0;  // the next boundary
    double last_best = 0.0;  // nats: the best partial score at the last boundary (best_score)
    std::vector<Element> live;
    std::vector<Slot> gap_slots;   // by fist x nodes + node: a mark ended, a gap starts
    std::vector<Slot> mark_slots;  // by fist x nodes + node: a gap ended, a mark starts
    std::vector<std::int32_t> gap_touched;
    std::vector<std::int32_t> mark_touched;
    std::vector<double> partial;
    std::vector<Element> opened;
    // A birth's first mark's ends at a speed that has other stretches: what is offered at g > 1 (boundary).
    struct Stretched {
        double end;  // nats: the end's score at g = 1
        std::int32_t parent;
        int speed;
        int child;
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
    };
    std::array<Tail, kEdgeCount + 1> tails;
    // The times of samples: t = origin + m Delta after the last break
    std::vector<std::pair<std::int64_t, double>> breaks;

    const LevelTables* levels;
    // Per boundary: the correction to an element shorter than 6 samples that ends here, by [gap][length - 1], nats; a NaN
    // until asked for.
    std::array<std::array<double, kEdgeCount - 1>, 2> short_correction{};

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
        gap_slots.resize(slots);
        mark_slots.resize(slots);
        partial.reserve(4096);
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

    void offer(std::vector<Slot>& slots, std::vector<std::int32_t>& touched, std::size_t index, double score,
               std::int32_t parent, std::uint8_t how, std::int32_t stamp) {
        Slot& slot = slots[index];
        if (slot.stamp != stamp) {
            slot.stamp = stamp;
            slot.score = score;
            slot.parent = parent;
            slot.how = how;
            touched.push_back(static_cast<std::int32_t>(index));
        } else if (score > slot.score) {
            slot.score = score;
            slot.parent = parent;
            slot.how = how;
        }
    }

    void boundary(std::int64_t j);
};

void Search::Impl::boundary(std::int64_t j) {
    const std::int32_t stamp = static_cast<std::int32_t>(j & 0x7fffffff);
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
    {
        double gap_value = 0.0;
        double mark_value = horizon;
        for (int d = kEdgeCount; d >= 1; --d) {
            gap_value = std::max(gap_value, sums.rise(j + d) + horizon - sums.sum(j + d + kEdgeDone));
            mark_value = std::max(mark_value, sums.sum(j + d + kEdgeFirst) + sums.fall(j + d));
            gap_reach_from[static_cast<std::size_t>(d - 1)] = gap_value;
            mark_reach_from[static_cast<std::size_t>(d - 1)] = mark_value;
        }
    }
    const auto first_d = [](int length) { return static_cast<std::size_t>(std::max(1, kEdgeCount - length) - 1); };
    const double gap_reach = gap_reach_from[0];      // an element at least 5 samples old, and the no-signal story
    const double gap_reach_new = gap_reach_from[kEdgeCount - 1];    // a gap that starts here
    const double mark_reach_new = mark_reach_from[kEdgeCount - 1];  // a mark that starts here
    const int speeds = model.speeds();
    const std::int32_t count = static_cast<std::int32_t>(live.size());
    partial.resize(live.size());
    for (auto& by_kind : short_correction) by_kind.fill(std::numeric_limits<double>::quiet_NaN());
    gap_touched.clear();
    mark_touched.clear();
    double best = -kInf;

    // 1. The open elements: each offers its ends here, and its partial score.
    for (std::int32_t index = 0; index < count; ++index) {
        const Element& e = live[static_cast<std::size_t>(index)];
        const int length = static_cast<int>(j - e.start);
        double score;
        if (e.kind == kGap) {
            const int speed = e.speed;
            const int node = e.node;
            score = e.base + model.gap_survival(speed, length) + gap_reach_from[first_d(length)];
            const double ended_gap = length < kEdgeCount ? e.base + correction(true, length, j) : e.base;
            if (model.has_children(node)) {
                const ModelTables::Range& r = model.range(kElementGap, speed);
                if (length >= r.lo && length <= r.hi)
                    offer(mark_slots, mark_touched, slot_index(speed, node), ended_gap + model.log_end(kElementGap, speed, length),
                          index, kEndElement, stamp);
            }
            if (model.has_symbol(node)) {
                const double letter = ended_gap + model.letter_end(node) + model.letter_start();
                const ModelTables::Range& lg = model.range(kLetterGap, speed);
                if (length >= lg.lo && length <= lg.hi)
                    offer(mark_slots, mark_touched, slot_index(speed, 0),
                          letter - model.letter_gap_cost() + model.log_end(kLetterGap, speed, length), index, kEndLetter, stamp);
                const ModelTables::Range& wg = model.range(kWordGap, speed);
                if (length >= wg.lo && length <= wg.hi)
                    offer(mark_slots, mark_touched, slot_index(speed, 0),
                          letter - model.word_gap_cost() + model.log_end(kWordGap, speed, length), index, kEndWord, stamp);
            }
        } else if (e.kind == kMark) {
            const int speed = e.speed;
            const int node = e.node;
            score = e.base + mark_reach_from[first_d(length)] + model.mark_survival(speed, length);
            const ModelTables::Range& shortest = model.range(kDot, speed);
            const double ended = length < kEdgeCount && length >= shortest.lo ? e.base + mark_end + correction(false, length, j)
                                                                              : e.base + mark_end;
            for (int label = 0; label < 2; ++label) {
                const int child = model.child(node, label);
                if (child < 0) continue;
                const ModelTables::Range& r = model.range(label, speed);
                if (length >= r.lo && length <= r.hi)
                    offer(gap_slots, gap_touched, slot_index(speed, child), ended + model.log_end(label, speed, length), index, 0,
                          stamp);
            }
        } else {  // a birth's first mark: the best speed's survival, and its end offers every speed, each at g = 1 here
            double survive = -kInf;
            const double ended = e.base + mark_end + model.log_first_speed() + model.log_plain() +
                                 (length < kEdgeCount ? correction(false, length, j) : 0.0);
            for (int speed = 0; speed < speeds; ++speed) {
                const int plain = model.first_fist(speed);
                survive = std::max(survive, model.mark_survival(plain, length));
                for (int label = 0; label < 2; ++label) {
                    const int child = model.child(0, label);
                    if (child < 0) continue;
                    const ModelTables::Range& r = model.range(label, plain);
                    if (length >= r.lo && length <= r.hi) {
                        const double end = ended + model.log_end(label, plain, length);
                        offer(gap_slots, gap_touched, slot_index(plain, child), end, index, 0, stamp);
                        if (model.first_fist(speed + 1) > plain + 1) stretched.push_back(Stretched{end, index, speed, child});
                    }
                }
            }
            score = e.base + mark_reach_from[first_d(length)] + survive + model.log_first_speed();
        }
        partial[static_cast<std::size_t>(index)] = score;
        best = std::max(best, score);
    }
    stats.touches += count;

    // The same ends at the other stretches. Only those inside the beam of the best so far, which the final best can only
    // raise: this keeps the bookkeeping of a stretch nobody needs off every birth on noise, without changing what opens.
    {
        const double stretched_floor = best - config.beam_nats - gap_reach_new - (model.log_stretched() - model.log_plain());
        for (const Stretched& s : stretched) {
            if (s.end < stretched_floor) continue;
            const double score = s.end - model.log_plain() + model.log_stretched();
            for (int f = model.first_fist(s.speed) + 1; f < model.first_fist(s.speed + 1); ++f)
                offer(gap_slots, gap_touched, slot_index(f, s.child), score, s.parent, 0, stamp);
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
        best = std::max(best, gap_slots[static_cast<std::size_t>(index)].score + gap_reach_new);
    }
    for (const std::int32_t index : mark_touched) {
        best = std::max(best, mark_slots[static_cast<std::size_t>(index)].score + rise - c11 + mark_reach_new);
    }

    // The over's ends: from each mark's end at a node with a symbol, into the tail that reaches the no-signal story at
    // j + 6; and the tail that reaches it now.
    Tail& tail_now = tails[static_cast<std::size_t>(j % static_cast<std::int64_t>(tails.size()))];
    if (tail_now.score > -kInf) {
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
    for (const std::int32_t index : gap_touched) {
        const int node = index % model.nodes();
        if (!model.has_symbol(node)) continue;
        const double score = gap_slots[static_cast<std::size_t>(index)].score + model.letter_end(node) - model.over_end_cost();
        if (score > silent_end) {
            silent_end = score;
            silent_parent = gap_slots[static_cast<std::size_t>(index)].parent;
            silent_node = node;
        }
    }
    if (silent_alive) best = std::max(best, silent + gap_reach);
    if (silent_end > -kInf) best = std::max(best, silent_end + gap_reach_new);
    if (best == -kInf) best = 0.0;
    last_best = best;
    const double floor = best - config.beam_nats;

    // 4. Open the new elements inside the beam.
    opened.clear();
    for (const std::int32_t index : gap_touched) {
        const Slot& slot = gap_slots[static_cast<std::size_t>(index)];
        if (slot.score + gap_reach_new < floor) continue;
        const Element& parent = live[static_cast<std::size_t>(slot.parent)];
        Element e{};
        e.base = slot.score;
        e.start = j;
        e.kind = kGap;
        e.speed = static_cast<std::uint8_t>(index / model.nodes());
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
        opened.push_back(e);
    }
    for (const std::int32_t index : mark_touched) {
        const Slot& slot = mark_slots[static_cast<std::size_t>(index)];
        if (slot.score + rise - c11 + mark_reach_new < floor) continue;
        const Element& parent = live[static_cast<std::size_t>(slot.parent)];
        Element e{};
        e.base = slot.score + rise - c11;
        e.start = j;
        e.kind = kMark;
        e.speed = parent.speed;
        e.node = static_cast<std::uint8_t>(index % model.nodes());
        e.space = 0;
        if (slot.how == kEndElement) {
            e.letter = parent.letter;
            e.text = parent.text;
            text.hold(e.text);
        } else {
            e.letter = -1;
            e.text = text.append(parent.text, model.tree().symbol(parent.node), time_of(parent.letter), time_of(parent.start));
            if (slot.how == kEndWord) {
                const std::int32_t letter_entry = e.text;
                e.text = text.append(letter_entry, " ", time_of(parent.start), time_of(j));
                text.release(letter_entry);
            }
        }
        opened.push_back(e);
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
                const Element& parent = live[static_cast<std::size_t>(slot.parent)];
                Element e{};
                e.base = score + rise - c11;
                e.start = j;
                e.kind = kOpenMark;
                e.speed = 255;
                e.node = 0;
                e.space = 0;
                e.letter = -1;
                e.text = text.append(parent.text, model.tree().symbol(parent.node), time_of(parent.letter), time_of(parent.start));
                if (slot.how == kEndWord) {
                    const std::int32_t letter_entry = e.text;
                    e.text = text.append(letter_entry, " ", time_of(parent.start), time_of(j));
                    text.release(letter_entry);
                }
                opened.push_back(e);
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
            e.speed = 255;
            e.node = 0;
            e.space = silent_text != viterbi::SharedText::kEmpty ? 1 : 0;
            e.text = silent_text;
            text.hold(e.text);
            opened.push_back(e);
        }
    }
    // The over's end that won the no-signal story.
    if (silent_parent >= 0) {
        const Element& parent = live[static_cast<std::size_t>(silent_parent)];
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
        Tail& tail = tails[static_cast<std::size_t>((j + kEdgeCount) % static_cast<std::int64_t>(tails.size()))];
        tail.score = silent_end + kEdgeCount * model.log_silence();
        tail.text = entry;
    }

    // 5. Prune: the beam, then elements past their longest length. The best one's text is held until the end of this
    // boundary, for the no-signal story to go on with should nothing else stand (a long carrier outlasts every element).
    std::int32_t best_dropped_text = viterbi::SharedText::kEmpty;
    double best_dropped = -kInf;
    for (std::int32_t index = 0; index < count; ++index) {
        if (partial[static_cast<std::size_t>(index)] > best_dropped) {
            best_dropped = partial[static_cast<std::size_t>(index)];
            best_dropped_text = live[static_cast<std::size_t>(index)].text;
        }
    }
    text.hold(best_dropped_text);
    std::size_t kept = 0;
    for (std::int32_t index = 0; index < count; ++index) {
        Element& e = live[static_cast<std::size_t>(index)];
        const int length = static_cast<int>(j - e.start);
        bool keep = partial[static_cast<std::size_t>(index)] >= floor;
        if (keep) {
            if (e.kind == kGap) keep = length < model.gap_hi(e.speed);
            else if (e.kind == kMark) keep = length < model.mark_hi(e.speed);
            else keep = length < model.mark_hi(model.first_fist(model.speeds() - 1));
        }
        if (keep) {
            partial[kept] = partial[static_cast<std::size_t>(index)];
            live[kept++] = e;
        } else {
            text.release(e.text);
        }
    }
    live.resize(kept);
    partial.resize(kept);
    stats.opened += static_cast<std::int64_t>(opened.size());
    live.insert(live.end(), opened.begin(), opened.end());

    // The ceiling, on what stands: the new ones count at their own partial scores.
    const std::size_t ceiling = static_cast<std::size_t>(config.ceiling);
    if (live.size() > ceiling) {
        partial.resize(live.size());
        for (std::size_t i = kept; i < live.size(); ++i) {
            const Element& e = live[i];
            partial[i] = e.kind == kGap ? e.base + gap_reach_new : e.base + mark_reach_new;
        }
        std::vector<std::uint32_t> order(live.size());
        for (std::size_t i = 0; i < order.size(); ++i) order[i] = static_cast<std::uint32_t>(i);
        std::nth_element(order.begin(), order.begin() + static_cast<std::ptrdiff_t>(ceiling), order.end(),
                         [&](std::uint32_t a, std::uint32_t b) {
                             if (partial[a] != partial[b]) return partial[a] > partial[b];
                             return a < b;
                         });
        for (std::size_t i = ceiling; i < order.size(); ++i) {
            text.release(live[order[i]].text);
            live[order[i]].start = -1;  // dropped
        }
        std::size_t out = 0;
        for (std::size_t i = 0; i < live.size(); ++i)
            if (live[i].start >= 0) live[out++] = live[i];
        live.resize(out);
    }

    // The no-signal story leaves when behind the beam, except when nothing else stands.
    if (silent_alive && silent + gap_reach < floor) silent_alive = false;
    bool tail_waits = false;
    for (const Tail& tail : tails) tail_waits = tail_waits || tail.score > -kInf;
    if (!silent_alive && live.empty() && !tail_waits) {
        silent_alive = true;
        silent = best;
        if (best_dropped > -kInf) {
            text.release(silent_text);
            silent_text = best_dropped_text;
            text.hold(silent_text);
        }
    }
    text.release(best_dropped_text);
    stats.largest_live = std::max(stats.largest_live, static_cast<std::int64_t>(live.size()));
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
        for (const Element& e : s.live) {
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
        for (const Element& e : s.live) {
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
std::size_t Search::live() const { return impl_->live.size(); }

}  // namespace kz4ap::segmental
