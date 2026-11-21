#include "kz4ap/segmental/evidence.hpp"

#include "kz4ap/segmental/fast_math.hpp"
#include "kz4ap/viterbi/observation.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <stdexcept>
#include <utility>

namespace kz4ap::segmental {

void RunningSums::push(double l1, const std::array<double, kEdgeCount>& rise, const std::array<double, kEdgeCount>& fall,
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
    term.inverse_v = static_cast<float>(1.0 / v);
    term.constant = static_cast<float>(-rs2 / v - std::log(v));
    term.i0_coeff = static_cast<float>(2.0 * std::sqrt(rs2) / v);
    return term;
}

// x less w z^2, nats.
inline double term_value(const LevelTables::Term& term, double z, double z2, double w) {
    return w * (-term.inverse_v * z2 + term.constant + fast::ln_i0(term.i0_coeff * z));
}

}  // namespace

LevelTables::LevelTables(const SegmentalConfig& config, const viterbi::OutputNoise& noise,
                         const viterbi::TemplateTables& tables) {
    if (!(config.level_step_db > 0.0) || !(config.level_max_db_s500 > config.level_min_db_s500))
        throw std::invalid_argument("segmental: the level grid needs a step above 0 dB and a top above its bottom");
    levels_ = 1 + static_cast<int>(std::round((config.level_max_db_s500 - config.level_min_db_s500) / config.level_step_db));
    if (levels_ > 256) throw std::invalid_argument("segmental: the level grid holds at most 256 levels");
    if (!(config.level_on_floor == 0.0 || config.level_on_floor == 1.0))
        throw std::invalid_argument("segmental: level_on_floor must be 0 or 1");
    if (!(config.level_noise_bottom == 0.0 || config.level_noise_bottom == 1.0))
        throw std::invalid_argument("segmental: level_noise_bottom must be 0 or 1");
    if (!(std::isfinite(config.level_noise_above_floor_max_db) && config.level_noise_above_floor_max_db >= 0.0))
        throw std::invalid_argument("segmental: level_noise_above_floor_max_db must be finite and not negative (dB re the floor)");
    on_floor_ = config.level_on_floor == 1.0;
    step_db_ = config.level_step_db;
    below_ = on_floor_ ? static_cast<int>(std::floor(config.level_noise_above_floor_max_db / config.level_step_db + 1e-9)) : 0;
    if (below_ > 255) throw std::invalid_argument("segmental: level_noise_above_floor_max_db reaches at most 255 grid steps");
    table_levels_ = levels_ + below_;
    w_ = noise.w_strong;
    sigma2_per_n0_hz_ = noise.sigma2_per_n0_hz;
    // The relative variance of the key-down amplitude about a level of the grid: the systematic floor's, and the grid's own,
    // a level being off by up to half a step, uniformly (variance step^2 / 12 in nepers^2; derived). Without it, above about
    // +30 dB S500 a plain sample pays for the grid's step while a sample in an edge's window, whose V is wider, pays less,
    // so that cutting a mark into pieces gains (measured: +40 dB S500 reads all dots at a beam of 60 nats).
    const double step_nepers_for_v = config.level_step_db / 20.0 * std::log(10.0);
    const double f2 = config.model.systematic_floor * config.model.systematic_floor +
                      config.level_grid_in_v * step_nepers_for_v * step_nepers_for_v / 12.0;
    // Table index i holds the level min + (i - below_) x step, dB S500 re sigma^2: the grid, and below_ steps under it for a
    // sigma^2 that stands above the floor the grid is relative to.
    const bool bottom_clamp = config.level_noise_bottom == 1.0;
    const auto r2_at = [&](int i) {
        const double s500_db = config.level_min_db_s500 + (bottom_clamp ? std::max(0, i - below_) : i - below_) * config.level_step_db;
        // a^2 / sigma_y^2 = S500 x 500 Hz / (sigma_y^2 / N0)  (S500 is a^2 over N0 x 500 Hz; derived)
        return std::pow(10.0, s500_db / 10.0) * 500.0 / sigma2_per_n0_hz_;
    };
    plain_.resize(static_cast<std::size_t>(table_levels_));
    rise_.resize(static_cast<std::size_t>(kEdgeCount * table_levels_));
    fall_.resize(static_cast<std::size_t>(kEdgeCount * table_levels_));
    for (int i = 0; i < table_levels_; ++i) {
        const double r2 = r2_at(i);
        plain_[static_cast<std::size_t>(i)] = make_term(r2, 1.0, f2, 0.0);
        for (int position = 0; position < kEdgeCount; ++position) {
            const double s = tables.s[static_cast<std::size_t>(kEdgeFirst + position)];
            const double v_m = tables.v[static_cast<std::size_t>(kEdgeFirst + position)];
            rise_[static_cast<std::size_t>(position * table_levels_ + i)] = make_term(r2, s, f2, v_m);
            fall_[static_cast<std::size_t>(position * table_levels_ + i)] = make_term(r2, 1.0 - s, f2, v_m);
        }
    }
    // The short elements' templates.
    short_.resize(static_cast<std::size_t>(2 * 5 * kShortPositions * table_levels_));
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
                for (int i = 0; i < table_levels_; ++i)
                    short_[static_cast<std::size_t>(((gap * 5 + length - 1) * kShortPositions + position) * table_levels_ + i)] =
                        make_term(r2_at(i), s, f2, v);
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
            values[g] = term_value(short_term(gap, length, position, context.level[static_cast<std::size_t>(g)], context.shift), z, z2,
                                   w_);
            top = std::max(top, values[g]);
        }
        double sum = 0.0;
        for (int g = 0; g < context.count; ++g) sum += context.probability[static_cast<std::size_t>(g)] * fast::exp_neg(values[g] - top);
        score += w_ * z2 + top + fast::log(sum);
    }
    return score;
}

ChannelFloor::ChannelFloor(const SegmentalConfig& config) {
    const double window_s = config.channel_floor_window_s;
    if (!(std::isfinite(window_s) && window_s >= 0.0))
        throw std::invalid_argument("segmental: channel_floor_window_s must be finite and not negative (s; 0 leaves the floor out)");
    if (window_s == 0.0) return;
    const double q = config.channel_floor_quantile;
    if (!(q > 0.0 && q < 1.0)) throw std::invalid_argument("segmental: channel_floor_quantile must lie in (0, 1)");
    if (!std::isfinite(config.channel_floor_margin_db))
        throw std::invalid_argument("segmental: channel_floor_margin_db must be finite (dB re the floor as estimated)");
    const double interval_s = config.model.sample_interval_s;
    stride_ = viterbi::whole_count(config.channel_floor_stride_s, interval_s, "channel_floor_stride_s", "samples");
    refresh_ = viterbi::whole_count(config.channel_floor_refresh_s, interval_s, "channel_floor_refresh_s", "samples");
    const double values_per_s = 1.0 / (stride_ * interval_s);
    if (!(window_s * values_per_s >= 1.0))
        throw std::invalid_argument("segmental: channel_floor_window_s must hold at least one channel_floor_stride_s");
    const double start_s = config.channel_floor_start_s;
    if (!(start_s >= 0.0 && start_s <= window_s))
        throw std::invalid_argument("segmental: channel_floor_start_s must lie between 0 s and channel_floor_window_s");
    capacity_ = static_cast<std::size_t>(window_s * values_per_s);
    minimum_ = std::max<std::size_t>(1, static_cast<std::size_t>(start_s * values_per_s));
    quantile_ = q;
    to_noise_ = std::pow(10.0, config.channel_floor_margin_db / 10.0) / -std::log1p(-q);
    ring_.assign(capacity_, 0.0);
    work_.reserve(capacity_);
    on_ = true;
}

double ChannelFloor::sigma2(double e_fs, double hiss_sigma2_fs2) {
    if (!on_) return hiss_sigma2_fs2;
    ++count_;
    if (count_ % stride_ == 0) ring_[written_++ % capacity_] = e_fs * e_fs;
    if (count_ % refresh_ == 0 && written_ >= minimum_) {
        const std::size_t held = std::min(written_, capacity_);
        work_.assign(ring_.begin(), ring_.begin() + static_cast<std::ptrdiff_t>(held));
        const std::size_t rank = static_cast<std::size_t>(quantile_ * static_cast<double>(held));
        std::nth_element(work_.begin(), work_.begin() + static_cast<std::ptrdiff_t>(rank), work_.end());
        floor_ = work_[rank] * to_noise_;
    }
    return std::max(hiss_sigma2_fs2, floor_);
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
    z_.assign(ring_samples, 0.0);
    shift_.assign(ring_samples, 0);
    top_.assign(ring_samples, 0.0);
    beta_.assign(2 * levels, 1.0 / (2 * levels_));
    belief_.assign(ring * levels, 0.0);
    // The forward message's prediction into frame 0.
    std::copy_n(alpha_.begin(), levels, entering_.begin());
    for (std::size_t level = 0; level < levels; ++level) entering_[level] += alpha_[levels + level];
}

void LevelEvidence::push(double e_fs, double sigma2_fs2, double floor_fs2) {
    // The grid is relative to the floor; sigma^2 above it moves the tables' levels, not the carrier's (config.hpp,
    // level_on_floor): sigma^2 in whole grid steps above the floor, the nearest. Past below() steps sigma^2 is taken as it is and
    // the grid is relative to sigma^2 less below() steps: only the excess moves the carrier's level.
    int shift = 0;
    double sigma2 = sigma2_fs2;
    if (tables_.on_floor() && floor_fs2 > 0.0 && sigma2_fs2 > floor_fs2) {
        const double steps = 10.0 * std::log10(sigma2_fs2 / floor_fs2) / tables_.step_db();
        if (steps >= tables_.below()) {
            shift = tables_.below();
        } else {
            shift = static_cast<int>(std::lround(steps));
            sigma2 = floor_fs2 * std::pow(10.0, shift * tables_.step_db() / 10.0);
        }
    }
    const double z = e_fs / std::sqrt(sigma2);
    const double z2 = z * z;
    const std::size_t ring_samples = static_cast<std::size_t>(ring_frames_) * static_cast<std::size_t>(frame_samples_);
    const std::size_t slot = static_cast<std::size_t>(samples_) % ring_samples;
    z_[slot] = z;
    shift_[slot] = static_cast<std::uint8_t>(shift);
    float* x = x_.data() + slot * static_cast<std::size_t>(levels_);
    const double w = tables_.w();
    const double common = w * z2;  // what x_k(l) holds apart from x[level]
    double top = 0.0;
    for (int level = 0; level < levels_; ++level) {
        x[level] = static_cast<float>(term_value(tables_.plain(level, shift), z, z2, w));
        top = std::max(top, common + static_cast<double>(x[level]));
    }
    top_[slot] = top;
    // One step of the key, then the sample: key-up scores 0, key-down x; relative to the largest, to stay in range.
    const double stay = 1.0 - key_switch_;
    const double up_scale = fast::exp_neg(-top);
    double total = 0.0;
    double* up = alpha_.data();
    double* down = alpha_.data() + levels_;
    for (int level = 0; level < levels_; ++level) {
        const double from_up = up[level];
        const double from_down = down[level];
        const double new_up = (stay * from_up + key_switch_ * from_down) * up_scale;
        const double new_down = (stay * from_down + key_switch_ * from_up) * fast::exp_neg(common + x[level] - top);
        up[level] = new_up;
        down[level] = new_down;
        total += new_up + new_down;
    }
    // What cannot be scaled (an exponent past a double's range, at about +100 dB S500 and above) starts the message over:
    // a NaN here would stay in the message for good, and the channel would be deaf.
    if (!(total > 0.0) || !std::isfinite(total)) {
        std::fill(alpha_.begin(), alpha_.end(), 1.0);
        total = 2.0 * levels_;
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
    const std::array<double, kEdgeCount> none{};
    for (int i = 0; i <= kHorizon; ++i) sums_.push(0.0, none, none);
}

void LevelEvidence::step_level(double* x) const {
    const double p = tables_.walk_p();
    const double keep = 1.0 - 2.0 * p;
    const int n = levels_;
    // Three taps, ends reflecting, from one buffer into the other: a loop a compiler can make into vectors, as an update in place is not.
    double buffer[256];
    double* from = x;
    double* to = buffer;
    for (int pass = 0; n > 1 && pass < tables_.walk_passes(); ++pass) {
        to[0] = keep * from[0] + p * (from[0] + from[1]);
        for (int i = 1; i < n - 1; ++i) to[i] = keep * from[i] + p * (from[i - 1] + from[i + 1]);
        to[n - 1] = keep * from[n - 1] + p * (from[n - 2] + from[n - 1]);
        std::swap(from, to);
    }
    if (from != x) std::copy_n(from, n, x);
    double sum = 0.0;
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
            const std::size_t sample_slot = static_cast<std::size_t>(k) % ring_samples;
            const float* x = x_.data() + sample_slot * levels;
            const double common = tables_.w() * z_[sample_slot] * z_[sample_slot];
            const double top = top_[sample_slot];
            const double up_scale = fast::exp_neg(-top);
            double total = 0.0;
            for (std::size_t level = 0; level < levels; ++level) {
                const double seen_up = up[level] * up_scale;
                const double seen_down = down[level] * fast::exp_neg(common + x[level] - top);
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
        const int shift = shift_[slot];
        const float* x = x_.data() + slot * static_cast<std::size_t>(levels_);
        const double common = w * z2;
        // ln sum P e^x, pulling out the largest exponent
        double top = -1e300;
        for (int i = 0; i < count; ++i) top = std::max(top, static_cast<double>(x[active[i]]));
        double sum = 0.0;
        for (int i = 0; i < count; ++i) sum += active_p[i] * fast::exp_neg(x[active[i]] - top);
        const double l1 = count > 0 ? common + top + fast::log(sum) : 0.0;

        std::array<double, kEdgeCount> rise{};
        std::array<double, kEdgeCount> fall{};
        for (int position = 0; position < kEdgeCount; ++position) {
            double up_top = -1e300, down_top = -1e300;
            double up[kEdgeLevels], down[kEdgeLevels];
            for (int i = 0; i < edge_count; ++i) {
                up[i] = term_value(tables_.rise(position, edge_level[i], shift), z, z2, w);
                down[i] = term_value(tables_.fall(position, edge_level[i], shift), z, z2, w);
                up_top = std::max(up_top, up[i]);
                down_top = std::max(down_top, down[i]);
            }
            double up_sum = 0.0, down_sum = 0.0;
            for (int i = 0; i < edge_count; ++i) {
                up_sum += edge_p[i] * fast::exp_neg(up[i] - up_top);
                down_sum += edge_p[i] * fast::exp_neg(down[i] - down_top);
            }
            rise[static_cast<std::size_t>(position)] = edge_count > 0 ? common + up_top + fast::log(up_sum) : 0.0;
            fall[static_cast<std::size_t>(position)] = edge_count > 0 ? common + down_top + fast::log(down_sum) : 0.0;
        }
        SampleContext context;
        context.z = z;
        context.count = static_cast<std::uint8_t>(edge_count);
        context.shift = static_cast<std::uint8_t>(shift);
        for (int i = 0; i < edge_count; ++i) {
            context.level[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(edge_level[i]);
            context.probability[static_cast<std::size_t>(i)] = static_cast<float>(edge_p[i]);
        }
        sums_.push(l1, rise, fall, context);
    }
}

}  // namespace kz4ap::segmental
