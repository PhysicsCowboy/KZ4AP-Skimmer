#include "kz4ap/segmental/evidence.hpp"

#include "kz4ap/viterbi/observation.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <stdexcept>

namespace kz4ap::segmental {

void RunningSums::push(double l1, const std::array<float, kEdgeCount>& rise, const std::array<float, kEdgeCount>& fall,
                       const SampleContext& context) {
    const std::int64_t k = count_;
    context_[static_cast<std::size_t>(k) & kContextMask] = context;
    // A claim's window starts empty when its first sample arrives, and C[k + 1] is written with sample k.
    rise_[static_cast<std::size_t>(k) & kMask] = 0.0;
    fall_[static_cast<std::size_t>(k) & kMask] = 0.0;
    sum_[static_cast<std::size_t>(k + 1) & kMask] = sum_[static_cast<std::size_t>(k) & kMask] + l1;
    for (int position = 0; position < kEdgeCount; ++position) {
        const std::int64_t claim = k - (kEdgeFirst + position);
        if (claim < 0) continue;
        rise_[static_cast<std::size_t>(claim) & kMask] += rise[static_cast<std::size_t>(position)];
        fall_[static_cast<std::size_t>(claim) & kMask] += fall[static_cast<std::size_t>(position)];
    }
    count_ = k + 1;
}

namespace {

LevelTables::Term make_term(double r2, double s, double f2, double v_m) {
    const double v = 1.0 + r2 * (f2 * s * s + v_m);
    const double rs2 = s * s * r2;
    LevelTables::Term term{};
    term.z2_coeff = static_cast<float>(1.0 - 1.0 / v);
    term.constant = static_cast<float>(-rs2 / v - std::log(v));
    term.i0_coeff = static_cast<float>(2.0 * std::sqrt(rs2) / v);
    return term;
}

inline double term_value(const LevelTables::Term& term, double z, double z2, double w) {
    return w * (term.z2_coeff * z2 + term.constant + viterbi::ln_i0(term.i0_coeff * z));
}

}  // namespace

LevelTables::LevelTables(const SegmentalConfig& config, const viterbi::OutputNoise& noise,
                         const viterbi::TemplateTables& tables) {
    if (!(config.level_step_db > 0.0) || !(config.level_max_db_s500 > config.level_min_db_s500))
        throw std::invalid_argument("segmental: the level grid needs a step above 0 dB and a top above its bottom");
    levels_ = 1 + static_cast<int>(std::round((config.level_max_db_s500 - config.level_min_db_s500) / config.level_step_db));
    if (levels_ > 256) throw std::invalid_argument("segmental: the level grid holds at most 256 levels");
    w_ = noise.w_strong;
    sigma2_per_n0_hz_ = noise.sigma2_per_n0_hz;
    // The relative variance of the key-down amplitude about a level of the grid: the systematic floor's, and the grid's own,
    // a level being off by up to half a step, uniformly (variance step^2 / 12 in nepers^2; derived). Without it, above about
    // +30 dB S500 a plain sample pays for the grid's step while a sample in an edge's window, whose V is wider, pays less,
    // so that cutting a mark into pieces gains (measured: +40 dB S500 reads all dots at a beam of 60 nats).
    const double step_nepers_for_v = config.level_step_db / 20.0 * std::log(10.0);
    const double f2 = config.model.systematic_floor * config.model.systematic_floor +
                      config.level_grid_in_v * step_nepers_for_v * step_nepers_for_v / 12.0;
    plain_.resize(static_cast<std::size_t>(levels_));
    rise_.resize(static_cast<std::size_t>(kEdgeCount * levels_));
    fall_.resize(static_cast<std::size_t>(kEdgeCount * levels_));
    for (int level = 0; level < levels_; ++level) {
        const double s500_db = config.level_min_db_s500 + level * config.level_step_db;
        // a^2 / sigma_y^2 = S500 x 500 Hz / (sigma_y^2 / N0)  (S500 is a^2 over N0 x 500 Hz; derived)
        const double r2 = std::pow(10.0, s500_db / 10.0) * 500.0 / sigma2_per_n0_hz_;
        plain_[static_cast<std::size_t>(level)] = make_term(r2, 1.0, f2, 0.0);
        for (int position = 0; position < kEdgeCount; ++position) {
            const double s = tables.s[static_cast<std::size_t>(kEdgeFirst + position)];
            const double v_m = tables.v[static_cast<std::size_t>(kEdgeFirst + position)];
            rise_[static_cast<std::size_t>(position * levels_ + level)] = make_term(r2, s, f2, v_m);
            fall_[static_cast<std::size_t>(position * levels_ + level)] = make_term(r2, 1.0 - s, f2, v_m);
        }
    }
    // The short elements' templates.
    short_.resize(static_cast<std::size_t>(2 * 5 * kShortPositions * levels_));
    const auto s_at = [&](int j) { return j < 0 ? 0.0 : j >= 16 ? 1.0 : tables.s[static_cast<std::size_t>(j)]; };
    const auto v_at = [&](int j) { return j < 0 || j >= 16 ? 0.0 : tables.v[static_cast<std::size_t>(j)]; };
    for (int gap = 0; gap < 2; ++gap) {
        for (int length = 1; length <= 5; ++length) {
            for (int position = 0; position < kShortPositions; ++position) {
                const int p = kEdgeFirst + position;
                const int q = p - length;
                const double shape = s_at(p) - s_at(q);
                const double s = std::fabs(gap ? 1.0 - shape : shape);
                double v = v_at(p) + v_at(q);
                if (p < 16 && q >= 0 && q < 16)
                    v -= 2.0 * tables.c[static_cast<std::size_t>(p)][static_cast<std::size_t>(q)];
                v = std::max(v, 0.0);
                for (int level = 0; level < levels_; ++level) {
                    const double s500_db = config.level_min_db_s500 + level * config.level_step_db;
                    const double r2 = std::pow(10.0, s500_db / 10.0) * 500.0 / sigma2_per_n0_hz_;
                    short_[static_cast<std::size_t>(((gap * 5 + length - 1) * kShortPositions + position) * levels_ + level)] =
                        make_term(r2, s, f2, v);
                }
            }
        }
    }
    // The walk's variance per frame is sigma^2 T nepers^2; one pass of (p, 1 - 2 p, p) adds 2 p step^2, step in nepers.
    const double step_nepers = config.level_step_db / 20.0 * std::log(10.0);
    const double sigma2 = config.level_walk_nepers_per_sqrt_s * config.level_walk_nepers_per_sqrt_s * config.level_frame_s;
    const double total_p = sigma2 / (2.0 * step_nepers * step_nepers);
    walk_passes_ = std::max(1, static_cast<int>(std::ceil(total_p / 0.33)));
    walk_p_ = total_p / walk_passes_;
    jump_ = config.level_jump_per_s * config.level_frame_s;
    if (!(jump_ >= 0.0 && jump_ < 1.0)) throw std::invalid_argument("segmental: level_jump_per_s x level_frame_s must lie in [0, 1)");
}

double LevelTables::short_score(bool gap, int length, std::int64_t i, const RunningSums& sums) const {
    double score = 0.0;
    for (int position = 0; position <= length + kEdgeCount - 1; ++position) {
        const SampleContext& context = sums.context(i + kEdgeFirst + position);
        if (context.count == 0) continue;
        const double z = context.z;
        const double z2 = z * z;
        double top = -1e300;
        double values[kEdgeLevels];
        for (int g = 0; g < context.count; ++g) {
            values[g] = term_value(short_term(gap, length, position, context.level[static_cast<std::size_t>(g)]), z, z2, w_);
            top = std::max(top, values[g]);
        }
        double sum = 0.0;
        for (int g = 0; g < context.count; ++g) sum += context.probability[static_cast<std::size_t>(g)] * std::exp(values[g] - top);
        score += top + std::log(sum);
    }
    return score;
}

LevelEvidence::LevelEvidence(const SegmentalConfig& config, const LevelTables& tables, RunningSums& sums)
    : tables_(tables),
      sums_(sums),
      levels_(tables.levels()),
      frame_samples_(viterbi::whole_count(config.level_frame_s, config.model.sample_interval_s, "level_frame_s", "samples")),
      lookahead_frames_(viterbi::whole_count(config.level_lookahead_s, config.level_frame_s, "level_lookahead_s", "frames")),
      commit_frames_(viterbi::whole_count(config.level_commit_s, config.level_frame_s, "level_commit_s", "frames")),
      ring_frames_(lookahead_frames_ + commit_frames_ + 2),
      key_switch_(config.key_switch_per_s * config.model.sample_interval_s) {
    if (!(key_switch_ > 0.0 && key_switch_ < 0.5))
        throw std::invalid_argument("segmental: key_switch_per_s x sample_interval_s must lie in (0, 0.5)");
    const std::size_t levels = static_cast<std::size_t>(levels_);
    const std::size_t ring = static_cast<std::size_t>(ring_frames_);
    const std::size_t ring_samples = ring * static_cast<std::size_t>(frame_samples_);
    alpha_.assign(2 * levels, 0.5 / levels_);
    entering_.assign(ring * levels, 0.0);
    x_.assign(ring_samples * levels, 0.0f);
    z_.assign(ring_samples, 0.0f);
    beta_.assign(2 * levels, 1.0 / (2 * levels_));
    belief_.assign(ring * levels, 0.0);
    // The forward message's prediction into frame 0.
    std::copy_n(alpha_.begin(), levels, entering_.begin());
    for (std::size_t level = 0; level < levels; ++level) entering_[level] += alpha_[levels + level];
}

void LevelEvidence::push(double e_fs, double sigma2_fs2) {
    const double z = e_fs / std::sqrt(sigma2_fs2);
    const double z2 = z * z;
    const std::size_t ring_samples = static_cast<std::size_t>(ring_frames_) * static_cast<std::size_t>(frame_samples_);
    const std::size_t slot = static_cast<std::size_t>(samples_) % ring_samples;
    z_[slot] = static_cast<float>(z);
    float* x = x_.data() + slot * static_cast<std::size_t>(levels_);
    const double w = tables_.w();
    double top = 0.0;
    for (int level = 0; level < levels_; ++level) {
        const double value = term_value(tables_.plain(level), z, z2, w);
        x[level] = static_cast<float>(value);
        top = std::max(top, value);
    }
    // One step of the key, then the sample: key-up scores 0, key-down x; relative to the largest, to stay in range.
    const double stay = 1.0 - key_switch_;
    const double up_scale = std::exp(-top);
    double total = 0.0;
    double* up = alpha_.data();
    double* down = alpha_.data() + levels_;
    for (int level = 0; level < levels_; ++level) {
        const double from_up = up[level];
        const double from_down = down[level];
        const double new_up = (stay * from_up + key_switch_ * from_down) * up_scale;
        const double new_down = (stay * from_down + key_switch_ * from_up) * std::exp(x[level] - top);
        up[level] = new_up;
        down[level] = new_down;
        total += new_up + new_down;
    }
    const double scale = 1.0 / total;
    for (double& value : alpha_) value *= scale;
    ++samples_;
    if (++frame_fill_ == frame_samples_) {
        end_frame();
        if (frames_ - next_commit_ >= lookahead_frames_ + commit_frames_) commit(false);
    }
}

void LevelEvidence::finish() {
    if (frame_fill_ > 0) end_frame();
    commit(true);
    const std::array<float, kEdgeCount> none{};
    for (int i = 0; i <= kHorizon; ++i) sums_.push(0.0, none, none);
}

void LevelEvidence::step_level(double* x) const {
    const double p = tables_.walk_p();
    const int n = levels_;
    double sum = 0.0;
    for (int pass = 0; pass < tables_.walk_passes(); ++pass) {
        double before = x[0];  // reflecting ends
        for (int i = 0; i < n; ++i) {
            const double here = x[i];
            const double after = i + 1 < n ? x[i + 1] : here;
            x[i] = (1.0 - 2.0 * p) * here + p * (before + after);
            before = here;
        }
    }
    for (int i = 0; i < n; ++i) sum += x[i];
    const double jump = tables_.jump();
    const double mean = sum / n;
    for (int i = 0; i < n; ++i) x[i] = (1.0 - jump) * x[i] + jump * mean;
}

void LevelEvidence::end_frame() {
    const std::size_t levels = static_cast<std::size_t>(levels_);
    frame_fill_ = 0;
    ++frames_;
    // The prediction into the next frame: the level steps; the belief over levels it gives that frame sums the key out.
    step_level(alpha_.data());
    step_level(alpha_.data() + levels);
    const std::size_t slot = static_cast<std::size_t>(frames_ % ring_frames_);
    for (std::size_t level = 0; level < levels; ++level) entering_[slot * levels + level] = alpha_[level] + alpha_[levels + level];
}

void LevelEvidence::commit(bool all) {
    if (frames_ == next_commit_) return;
    const std::size_t levels = static_cast<std::size_t>(levels_);
    const std::size_t ring_samples = static_cast<std::size_t>(ring_frames_) * static_cast<std::size_t>(frame_samples_);
    const std::int64_t last = frames_ - 1;
    const std::int64_t last_committed = all ? last : next_commit_ + commit_frames_ - 1;
    std::fill(beta_.begin(), beta_.end(), 1.0 / static_cast<double>(2 * levels));
    double* up = beta_.data();
    double* down = beta_.data() + levels;
    const double stay = 1.0 - key_switch_;
    for (std::int64_t t = last; t >= next_commit_; --t) {
        const std::size_t slot = static_cast<std::size_t>(t % ring_frames_);
        // The belief this frame's samples read: what enters it from each side, its own samples left out.
        if (t <= last_committed) {
            double total = 0.0;
            for (std::size_t level = 0; level < levels; ++level) {
                const double value = entering_[slot * levels + level] * (up[level] + down[level]);
                belief_[slot * levels + level] = value;
                total += value;
            }
            for (std::size_t level = 0; level < levels; ++level) belief_[slot * levels + level] /= total;
        }
        // Back through its samples, the last first, then the level's step.
        const std::int64_t first_sample = t * frame_samples_;
        const std::int64_t end_sample = std::min(first_sample + frame_samples_, samples_);
        for (std::int64_t k = end_sample - 1; k >= first_sample; --k) {
            const float* x = x_.data() + (static_cast<std::size_t>(k) % ring_samples) * levels;
            double top = 0.0;
            for (std::size_t level = 0; level < levels; ++level) top = std::max(top, static_cast<double>(x[level]));
            const double up_scale = std::exp(-top);
            double total = 0.0;
            for (std::size_t level = 0; level < levels; ++level) {
                const double seen_up = up[level] * up_scale;
                const double seen_down = down[level] * std::exp(x[level] - top);
                const double new_up = stay * seen_up + key_switch_ * seen_down;
                const double new_down = stay * seen_down + key_switch_ * seen_up;
                up[level] = new_up;
                down[level] = new_down;
                total += new_up + new_down;
            }
            const double scale = 1.0 / total;
            for (double& value : beta_) value *= scale;
        }
        step_level(up);
        step_level(down);
    }
    for (std::int64_t t = next_commit_; t <= last_committed; ++t) {
        const std::size_t slot = static_cast<std::size_t>(t % ring_frames_);
        commit_frame(t, belief_.data() + slot * levels);
    }
    next_commit_ = last_committed + 1;
}

void LevelEvidence::commit_frame(std::int64_t frame, const double* belief) {
    const std::size_t ring_samples = static_cast<std::size_t>(ring_frames_) * static_cast<std::size_t>(frame_samples_);
    const std::int64_t first = frame * frame_samples_;
    const std::int64_t end = std::min(first + frame_samples_, samples_);
    const double w = tables_.w();

    // The levels the belief holds, and at most 8 of them for the edges' terms (each the top of a group of neighbors, with
    // the group's probability): a heuristic that bounds the edges' cost when the belief is broad.
    int active[256];
    double active_p[256];
    int count = 0;
    for (int level = 0; level < levels_ && count < 256; ++level) {
        if (belief[level] >= 1e-4) {
            active[count] = level;
            active_p[count] = belief[level];
            ++count;
        }
    }
    int edge_level[kEdgeLevels];
    double edge_p[kEdgeLevels];
    int edge_count = 0;
    const int group = std::max(1, (count + kEdgeLevels - 1) / kEdgeLevels);
    for (int i = 0; i < count; i += group) {
        double total = 0.0;
        double center = 0.0;
        for (int g = i; g < std::min(i + group, count); ++g) {
            total += active_p[g];
            center += active_p[g] * active[g];
        }
        edge_level[edge_count] = static_cast<int>(std::lround(center / total));
        edge_p[edge_count] = total;
        ++edge_count;
    }

    for (std::int64_t k = first; k < end; ++k) {
        const std::size_t slot = static_cast<std::size_t>(k) % ring_samples;
        const double z = z_[slot];
        const double z2 = z * z;
        const float* x = x_.data() + slot * static_cast<std::size_t>(levels_);
        // ln sum P e^x, pulling out the largest exponent
        double top = -1e300;
        for (int i = 0; i < count; ++i) top = std::max(top, static_cast<double>(x[active[i]]));
        double sum = 0.0;
        for (int i = 0; i < count; ++i) sum += active_p[i] * std::exp(x[active[i]] - top);
        const double l1 = count > 0 ? top + std::log(sum) : 0.0;

        std::array<float, kEdgeCount> rise{};
        std::array<float, kEdgeCount> fall{};
        for (int position = 0; position < kEdgeCount; ++position) {
            double up_top = -1e300, down_top = -1e300;
            double up[kEdgeLevels], down[kEdgeLevels];
            for (int i = 0; i < edge_count; ++i) {
                up[i] = term_value(tables_.rise(position, edge_level[i]), z, z2, w);
                down[i] = term_value(tables_.fall(position, edge_level[i]), z, z2, w);
                up_top = std::max(up_top, up[i]);
                down_top = std::max(down_top, down[i]);
            }
            double up_sum = 0.0, down_sum = 0.0;
            for (int i = 0; i < edge_count; ++i) {
                up_sum += edge_p[i] * std::exp(up[i] - up_top);
                down_sum += edge_p[i] * std::exp(down[i] - down_top);
            }
            rise[static_cast<std::size_t>(position)] = edge_count > 0 ? static_cast<float>(up_top + std::log(up_sum)) : 0.0f;
            fall[static_cast<std::size_t>(position)] = edge_count > 0 ? static_cast<float>(down_top + std::log(down_sum)) : 0.0f;
        }
        SampleContext context;
        context.z = static_cast<float>(z);
        context.count = static_cast<std::uint8_t>(edge_count);
        for (int i = 0; i < edge_count; ++i) {
            context.level[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(edge_level[i]);
            context.probability[static_cast<std::size_t>(i)] = static_cast<float>(edge_p[i]);
        }
        sums_.push(l1, rise, fall, context);
    }
}

}  // namespace kz4ap::segmental
