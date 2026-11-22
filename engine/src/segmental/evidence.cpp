#include "kz4ap/segmental/evidence.hpp"

#include "kz4ap/segmental/fast_math.hpp"
#include "kz4ap/viterbi/observation.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <numeric>
#include <stdexcept>
#include <utility>

namespace kz4ap::segmental {

void RunningSums::push(double l1, const std::array<double, kEdgeCount>& rise, const std::array<double, kEdgeCount>& fall,
                       const SampleContext& context, const double* tail, int tail_count) {
    const std::int64_t k = count_;
    // The claim k - 10 ... is past its fall's window: its tail starts empty.
    if (k >= kEdgeDone) {
        const std::size_t c = static_cast<std::size_t>(k - kEdgeDone);
        tail_filled_[c & kMask] = 0;
        tail_whole_[c & kMask] = 0.0;
    }
    tail_positions_ = tail_count;
    if (tail_count > 0) {
        if (tail_prefix_.empty()) tail_prefix_.assign((kTailMask + 1) * (kTailMax + 1), 0.0);
        for (int q = 0; q < tail_count; ++q) {
            const std::int64_t claim = k - kEdgeDone - q;
            if (claim < 0) break;
            const std::size_t c = static_cast<std::size_t>(claim);
            const int filled = tail_filled_[c & kMask];
            if (filled != q) continue;  // a shape that changed while this claim's tail was being read: it ends there
            double* prefix = tail_prefix_.data() + (c & kTailMask) * (kTailMax + 1);
            if (q == 0) prefix[0] = 0.0;
            prefix[q + 1] = prefix[q] + tail[q];
            tail_filled_[c & kMask] = static_cast<std::uint8_t>(q + 1);
            tail_whole_[c & kMask] = prefix[q + 1];
        }
    }
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

LevelTables::Term term_of(double r2, double s, double f2, double v_m) {
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

LevelTables::Term LevelTables::make_term(double r2, double s, double f2, double v_m) { return term_of(r2, s, f2, v_m); }

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
    f2_ = f2;
    r2_.resize(static_cast<std::size_t>(table_levels_));
    for (int position = 0; position < kEdgeCount; ++position) {
        edge_s_[static_cast<std::size_t>(position)] = tables.s[static_cast<std::size_t>(kEdgeFirst + position)];
        edge_v_[static_cast<std::size_t>(position)] = tables.v[static_cast<std::size_t>(kEdgeFirst + position)];
    }
    for (int i = 0; i < table_levels_; ++i) {
        const double r2 = r2_at(i);
        r2_[static_cast<std::size_t>(i)] = r2;
        plain_[static_cast<std::size_t>(i)] = term_of(r2, 1.0, f2, 0.0);
        for (int position = 0; position < kEdgeCount; ++position) {
            const double s = tables.s[static_cast<std::size_t>(kEdgeFirst + position)];
            const double v_m = tables.v[static_cast<std::size_t>(kEdgeFirst + position)];
            rise_[static_cast<std::size_t>(position * table_levels_ + i)] = term_of(r2, s, f2, v_m);
            fall_[static_cast<std::size_t>(position * table_levels_ + i)] = term_of(r2, 1.0 - s, f2, v_m);
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
                        term_of(r2_at(i), s, f2, v);
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
    if (!(std::isfinite(config.level_walk_high_nepers_per_sqrt_s) && config.level_walk_high_nepers_per_sqrt_s >= 0.0))
        throw std::invalid_argument("segmental: level_walk_high_nepers_per_sqrt_s must be finite and not negative (nepers per square root of a second)");
    if (config.level_walk_high_nepers_per_sqrt_s > 0.0) {
        if (!(config.level_walk_rise_to_db_s500 > config.level_walk_rise_from_db_s500))
            throw std::invalid_argument("segmental: level_walk_rise_to_db_s500 must lie above level_walk_rise_from_db_s500");
        // Column `from`: a Gaussian over the levels, in grid steps, of the variance the walk has from that level over a frame,
        // folded at both ends (reflecting, as the three-tap passes are), normalized to 1.
        const int n = levels_;
        walk_kernel_.assign(static_cast<std::size_t>(n * n), 0.0);
        for (int from = 0; from < n; ++from) {
            const double db = config.level_min_db_s500 + from * config.level_step_db;
            const double t = std::clamp((db - config.level_walk_rise_from_db_s500) /
                                        (config.level_walk_rise_to_db_s500 - config.level_walk_rise_from_db_s500), 0.0, 1.0);
            const double walk = config.level_walk_nepers_per_sqrt_s + t * (config.level_walk_high_nepers_per_sqrt_s - config.level_walk_nepers_per_sqrt_s);
            const double sd_steps = walk * std::sqrt(config.level_frame_s) / step_nepers;
            double total = 0.0;
            for (int image = -4 * n; image <= 4 * n; ++image) {
                // the reflections of level `to` about the ends lie at -1 - to, 2n - 1 - to, ...
                const int to_raw = image;
                int to = to_raw;
                while (to < 0 || to >= n) to = to < 0 ? -1 - to : 2 * n - 1 - to;
                const double d = (to_raw - from) / std::max(sd_steps, 1e-9);
                const double g = std::exp(-0.5 * d * d);
                walk_kernel_[static_cast<std::size_t>(to * n + from)] += g;
                total += g;
            }
            for (int to = 0; to < n; ++to) walk_kernel_[static_cast<std::size_t>(to * n + from)] /= total;
        }
    }
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

EchoEstimator::EchoEstimator(const SegmentalConfig& config, const viterbi::TemplateTables& tables) {
    const double interval_s = config.model.sample_interval_s;
    if (!(std::isfinite(config.echo_max_s) && config.echo_max_s >= 0.0))
        throw std::invalid_argument("segmental: echo_max_s must be finite and not negative (s; 0 leaves the tail out)");
    if (config.echo_max_s == 0.0) return;
    if (!(std::isfinite(config.echo_run_db) && std::isfinite(config.echo_min_db) && std::isfinite(config.echo_on_db) && std::isfinite(config.echo_end_db) &&
          config.echo_end_db <= config.echo_on_db))
        throw std::invalid_argument("segmental: echo_run_db, echo_on_db and echo_end_db must be finite, echo_end_db at most echo_on_db (dB)");
    if (!(config.echo_memory_runs >= 1.0 && config.echo_min_runs >= 1.0 && config.echo_min_run_s >= 0.0))
        throw std::invalid_argument("segmental: echo_memory_runs and echo_min_runs must be at least 1 run, echo_min_run_s not negative (s)");
    on_ = true;
    run_ratio_ = std::pow(10.0, config.echo_run_db / 10.0);
    min_level_ = std::pow(10.0, config.echo_min_db / 10.0);
    min_run_ = static_cast<std::size_t>(std::max(1.0, std::ceil(config.echo_min_run_s / interval_s - 1e-9)));
    forget_ = std::exp(-1.0 / config.echo_memory_runs);
    min_runs_ = config.echo_min_runs;
    on_ratio_ = std::pow(10.0, config.echo_on_db / 10.0);
    end_ratio_ = std::pow(10.0, config.echo_end_db / 10.0);
    positions_ = std::min(kTailMax, static_cast<int>(std::floor(config.echo_max_s / interval_s + 1e-9)));
    // h: the fall template's last position at or above half the key-down amplitude
    half_ = kEdgeFirst;
    for (int m = 0; m < static_cast<int>(tables.s.size()) && m < kEdgeDone; ++m)
        if (1.0 - tables.s[static_cast<std::size_t>(m)] >= 0.5) half_ = m;
    span_ = kEdgeDone + positions_ - half_;
    sum_.assign(static_cast<std::size_t>(span_), 0.0);
    count_.assign(static_cast<std::size_t>(span_), 0.0);
}

bool EchoEstimator::push(double e2_fs2, double floor_fs2) {
    if (!on_ || !(floor_fs2 > 0.0)) return false;
    const std::int64_t k = k_++;
    const bool down = e2_fs2 > run_ratio_ * floor_fs2;
    if (down && !in_run_) {
        // a run begins: what was being read of the last one's tail ends here
        if (profiling_) {
            close_profile();
        }
        in_run_ = true;
        run_.clear();
        run_floor_.clear();
    }
    if (in_run_) {
        if (down) {
            if (run_.size() < 4096) {
                run_.push_back(e2_fs2);
                run_floor_.push_back(floor_fs2);
            }
        } else {
            in_run_ = false;
            end_run(k);
        }
    }
    if (profiling_ && !in_run_) {
        profile_.push_back((e2_fs2 - floor_fs2) / profile_level_);
        if (static_cast<int>(profile_.size()) >= span_) {
            close_profile();
        }
    }
    const bool fresh = fresh_;
    fresh_ = false;
    return fresh;
}

void EchoEstimator::end_run(std::int64_t end) {
    (void)end;
    if (run_.size() < min_run_ || run_.size() >= 4096) return;
    std::vector<double> sorted = run_;
    std::nth_element(sorted.begin(), sorted.begin() + static_cast<std::ptrdiff_t>(sorted.size() / 2), sorted.end());
    const double level = sorted[sorted.size() / 2];
    if (level < min_level_ * run_floor_[run_floor_.size() / 2]) return;
    std::size_t last = 0;
    for (std::size_t i = 0; i < run_.size(); ++i)
        if (run_[i] >= level / 4.0) last = i;
    profiling_ = true;
    profile_level_ = level;
    profile_.clear();
    for (std::size_t i = last; i < run_.size() && static_cast<int>(profile_.size()) < span_; ++i)
        profile_.push_back((run_[i] - run_floor_[i]) / level);
    if (static_cast<int>(profile_.size()) >= span_) close_profile();
}

void EchoEstimator::close_profile() {
    profiling_ = false;
    for (std::size_t q = 0; q < sum_.size(); ++q) {
        sum_[q] *= forget_;
        count_[q] *= forget_;
    }
    for (std::size_t q = 0; q < profile_.size() && q < sum_.size(); ++q) {
        sum_[q] += profile_[q];
        count_[q] += 1.0;
    }
    runs_ = runs_ * forget_ + 1.0;
    profile_.clear();
    if (runs_ < min_runs_) return;
    // The first shape as soon as the memory holds min_runs_ (the 33rd strong run at 30 and 200), then every min_runs_ / 3.
    if (!decided_ || ++since_update_ >= std::max(1, static_cast<int>(min_runs_ / 3.0))) since_update_ = 0;
    if (since_update_ == 0) update_shape();
}

void EchoEstimator::update_shape() {
    const auto mean_at = [&](int m) {
        const int q = m - half_;
        if (q < 0 || q >= span_) return 0.0;
        const double c = count_[static_cast<std::size_t>(q)];
        return c >= min_runs_ / 3.0 ? sum_[static_cast<std::size_t>(q)] / c : 0.0;
    };
    EchoShape shape;
    const double first = mean_at(kEdgeDone);
    if (first >= on_ratio_) {
        shape.on = true;
        for (int q = 0; q < positions_; ++q) {
            const double g = mean_at(kEdgeDone + q);
            if (g < end_ratio_) break;
            shape.tail.push_back(g);
        }
        shape.in_fall.fill(first);
    }
    shape_ = shape;
    fresh_ = true;
    decided_ = true;
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

namespace {

// samples: the frame, the largest whole share of level_frame_s's samples within level_fast_frame_s (at least 1); with no fast
// frame (0 or not shorter), level_frame_s's.
int frame_samples_of(const SegmentalConfig& config) {
    const int slow = viterbi::whole_count(config.level_frame_s, config.model.sample_interval_s, "level_frame_s", "samples");
    if (!(std::isfinite(config.level_fast_frame_s) && config.level_fast_frame_s >= 0.0))
        throw std::invalid_argument("segmental: level_fast_frame_s must be finite and not negative (s)");
    const int fast = static_cast<int>(std::floor(config.level_fast_frame_s / config.model.sample_interval_s + 1e-9));
    if (fast <= 0 || fast >= slow) return slow;
    for (int d = fast; d > 1; --d)
        if (slow % d == 0) return d;
    return 1;
}

}  // namespace

DopplerEstimator::DopplerEstimator(const SegmentalConfig& config, const std::array<double, 7>& noise_rho) {
    if (!(std::isfinite(config.doppler_fast_hz) && config.doppler_fast_hz >= 0.0))
        throw std::invalid_argument("segmental: doppler_fast_hz must be finite and not negative (Hz; 0 leaves the walk as configured)");
    if (!(config.doppler_fast_hz > 0.0)) return;
    if (!(config.doppler_slow_hz >= 0.0 && config.doppler_slow_hz <= config.doppler_fast_hz && std::isfinite(config.doppler_min_db) &&
          std::isfinite(config.doppler_end_guard_s) && config.doppler_end_guard_s >= 0.0 && config.doppler_memory_runs >= 1.0 &&
          std::isfinite(config.doppler_memory_runs) && config.doppler_min_runs >= 1.0 && std::isfinite(config.doppler_min_runs) &&
          std::isfinite(config.doppler_walk_per_hz) && config.doppler_walk_per_hz >= 0.0 && std::isfinite(config.doppler_walk_max) &&
          config.doppler_walk_max >= config.level_walk_nepers_per_sqrt_s && config.doppler_shape_lo > 0.0 &&
          config.doppler_shape_lo <= 1.0 && config.doppler_shape_hi >= 1.0 && std::isfinite(config.doppler_shape_hi) &&
          config.doppler_min_runs_ratio >= 0.0 && config.doppler_min_runs_ratio <= 1.0 && std::isfinite(config.echo_run_db)))
        throw std::invalid_argument("segmental: a doppler_ setting is out of range (Hz, dB, s, runs; doppler_walk_max at least "
                                    "level_walk_nepers_per_sqrt_s; the shape's band around 1; the runs' ratio within 0 to 1)");
    interval_s_ = config.model.sample_interval_s;
    run_ratio_ = std::pow(10.0, config.echo_run_db / 10.0);
    guard_ = static_cast<int>(std::ceil(config.doppler_end_guard_s / interval_s_ - 1e-9));
    forget_ = std::exp(-1.0 / config.doppler_memory_runs);
    min_runs_ = config.doppler_min_runs;
    for (int i = 0; i < kLags; ++i) {
        const int tau = kFirstLag + i;
        noise_rho_[static_cast<std::size_t>(i)] = tau < static_cast<int>(noise_rho.size()) ? noise_rho[static_cast<std::size_t>(tau)] : 0.0;
    }
    on_ = true;
}

bool DopplerEstimator::push(std::complex<double> y_fs, double floor_fs2) {
    if (!on_ || !(floor_fs2 > 0.0)) return false;
    const bool down = std::norm(y_fs) > run_ratio_ * floor_fs2;
    if (down) {
        if (!in_run_) {
            in_run_ = true;
            run_.clear();
            run_floor_.clear();
        }
        if (run_.size() < 4096) {
            run_.push_back(y_fs);
            run_floor_.push_back(floor_fs2);
        }
        return false;
    }
    if (!in_run_) return false;
    in_run_ = false;
    return end_run() && runs_ >= min_runs_;
}

bool DopplerEstimator::end_run() {
    const int n = static_cast<int>(run_.size());
    if (n >= 4096 || n == 0) return false;
    // The marks' own samples, at or above -10 dB re the run's median power, less the front filter's edge at each end: so neither
    // a rise nor a fall nor a tail after it is read; a gap a tail fills (-12.6 to -13.4 dB re the mark at the widest filter's output
    // on the clips) splits the run, a fade to -6 dB re the median does not (heuristic).
    std::vector<double> power(run_.size());
    for (std::size_t k = 0; k < run_.size(); ++k) power[k] = std::norm(run_[k]);
    std::vector<double> sorted = power;
    std::nth_element(sorted.begin(), sorted.begin() + static_cast<std::ptrdiff_t>(sorted.size() / 2), sorted.end());
    const double median = sorted[sorted.size() / 2];
    std::array<std::complex<double>, kLags> cross{};
    std::array<double, kLags> p0{}, p1{};
    bool any = false;
    for (int k = 0; k < n;) {
        if (power[static_cast<std::size_t>(k)] < median / 10.0) {
            ++k;
            continue;
        }
        int e = k;
        while (e < n && power[static_cast<std::size_t>(e)] >= median / 10.0) ++e;
        const int first = k + guard_, end = e - guard_;  // pairs i, i + tau in [first, end)
        constexpr int longest = kFirstLag + kLags - 1;
        // Every lag over the same first samples, those with a partner at the longest lag, so that the lags compare like with like.
        for (int i = first; i + longest < end; ++i) {
            const std::size_t a = static_cast<std::size_t>(i);
            for (int l = 0; l < kLags; ++l) {
                const std::size_t b = a + static_cast<std::size_t>(kFirstLag + l);
                cross[static_cast<std::size_t>(l)] += run_[b] * std::conj(run_[a]) - run_floor_[a] * noise_rho_[static_cast<std::size_t>(l)];
                p0[static_cast<std::size_t>(l)] += power[a] - run_floor_[a];
                p1[static_cast<std::size_t>(l)] += power[b] - run_floor_[b];
            }
            any = true;
        }
        k = e;
    }
    if (!any) return false;
    for (int l = 0; l < kLags; ++l) {
        const std::size_t i = static_cast<std::size_t>(l);
        cross_[i] = cross_[i] * forget_ + cross[i];
        power0_[i] = power0_[i] * forget_ + p0[i];
        power1_[i] = power1_[i] * forget_ + p1[i];
        rho_[i] = power0_[i] > 0.0 && power1_[i] > 0.0 ? std::abs(cross_[i]) / std::sqrt(power0_[i] * power1_[i]) : 1.0;
    }
    cross_runs_ = cross_runs_ * forget_ + std::abs(cross[kLags - 1]);
    level_sum_ = level_sum_ * forget_ + 10.0 * std::log10(median / run_floor_[run_floor_.size() / 2]);
    runs_ = runs_ * forget_ + 1.0;
    ++taken_;
    level_db_ = level_sum_ / runs_;
    const std::size_t last = kLags - 1;
    const double tau_s = (kFirstLag + kLags - 1) * interval_s_;
    // |rho| = exp(-2 pi^2 sigma_D^2 tau^2), f_D = 2 sigma_D
    const auto spread_of = [&](double rho) {
        return rho < 1.0 && rho > 0.0 ? 2.0 * std::sqrt(-std::log(rho) / (2.0 * std::numbers::pi * std::numbers::pi)) / tau_s : 0.0;
    };
    spread_hz_ = spread_of(rho_[last]);
    runs_hz_ = spread_of(power0_[last] > 0.0 && power1_[last] > 0.0 ? cross_runs_ / std::sqrt(power0_[last] * power1_[last]) : 1.0);
    return true;
}

double DopplerEstimator::shape(int tau) const {
    const std::size_t i = static_cast<std::size_t>(tau - kFirstLag), last = kLags - 1;
    if (tau < kFirstLag || tau >= kFirstLag + kLags - 1) return 0.0;
    if (!(rho_[last] < 1.0 && rho_[last] > 0.0 && rho_[i] < 1.0 && rho_[i] > 0.0)) return 0.0;
    const double gaussian = std::pow(static_cast<double>(kFirstLag + kLags - 1) / tau, 2.0);
    return std::log(rho_[last]) / std::log(rho_[i]) / gaussian;
}

bool DopplerEstimator::shape_ok(double lo, double hi) const {
    for (int tau = kFirstLag; tau < kFirstLag + kLags - 1; ++tau) {
        const double q = shape(tau);
        if (!(q >= lo && q <= hi)) return false;
    }
    return true;
}

LevelEvidence::LevelEvidence(const SegmentalConfig& config, const LevelTables& tables, RunningSums& sums)
    : tables_(tables),
      sums_(sums),
      levels_(tables.levels()),
      frame_samples_(frame_samples_of(config)),
      slow_block_(viterbi::whole_count(config.level_frame_s, config.model.sample_interval_s, "level_frame_s", "samples") / frame_samples_),
      lookahead_frames_(viterbi::whole_count(config.level_lookahead_s, config.level_frame_s, "level_lookahead_s", "frames") * slow_block_),
      commit_frames_(viterbi::whole_count(config.level_commit_s, config.level_frame_s, "level_commit_s", "frames") * slow_block_),
      ring_frames_(lookahead_frames_ + commit_frames_ + 2 * slow_block_),
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
    block_start_.assign(ring, 0);
    step_after_.assign(ring, 0);
    beta_block_.assign(levels, 0.0);
    block_frames_ = slow_block_;
    fast_jump_ = config.level_jump_per_s * frame_samples_ * config.model.sample_interval_s;
    fast_block_jump_ = tables.jump();
    frame_s_ = frame_samples_ * config.model.sample_interval_s;
    // The forward message's prediction into frame 0.
    std::copy_n(alpha_.begin(), levels, entering_.begin());
    for (std::size_t level = 0; level < levels; ++level) entering_[level] += alpha_[levels + level];
}

void LevelEvidence::set_walk(bool fast, double walk_nepers_per_sqrt_s, bool short_frames) {
    fast_next_ = fast;
    short_next_ = short_frames;
    fast_walk_next_ = walk_nepers_per_sqrt_s;
}

void LevelEvidence::set_echo(const EchoShape& shape) {
    const int table_levels = tables_.table_levels();
    echo_on_ = shape.on && !shape.tail.empty();
    echo_positions_ = echo_on_ ? std::min(kTailMax, static_cast<int>(shape.tail.size())) : 0;
    if (!echo_on_) return;
    // The fall's window: the template's term with the tail's power added to its variance, v = 1 + r^2 (f^2 (1 - s)^2 + v_m + g).
    echo_fall_.resize(static_cast<std::size_t>(kEdgeCount * table_levels));
    for (int position = 0; position < kEdgeCount; ++position)
        for (int i = 0; i < table_levels; ++i)
            echo_fall_[static_cast<std::size_t>(position * table_levels + i)] =
                LevelTables::make_term(tables_.r2(i), 1.0 - tables_.edge_s(position), tables_.f2(),
                                       tables_.edge_v(position) + shape.in_fall[static_cast<std::size_t>(position)]);
    // The tail's positions: noise plus a zero-mean complex Gaussian of power g a^2, against noise alone:
    // w (z^2 rho / (1 + rho) - ln(1 + rho)), rho = g a^2 / sigma^2 (derived: the ratio of two complex Gaussians' densities).
    echo_gain_.resize(static_cast<std::size_t>(echo_positions_ * table_levels));
    echo_cost_.resize(echo_gain_.size());
    for (int q = 0; q < echo_positions_; ++q)
        for (int i = 0; i < table_levels; ++i) {
            const double rho = shape.tail[static_cast<std::size_t>(q)] * tables_.r2(i);
            echo_gain_[static_cast<std::size_t>(q * table_levels + i)] = static_cast<float>(rho / (1.0 + rho));
            echo_cost_[static_cast<std::size_t>(q * table_levels + i)] = static_cast<float>(std::log1p(rho));
        }
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

namespace {

// A walk over n levels as a matrix K[to][from], each column a Gaussian of sd_steps grid steps about from, folded at both ends
// (reflecting, as the three-tap passes are), normalized to 1 (the method of images).
std::vector<double> walk_matrix(int n, double sd_steps) {
    std::vector<double> kernel(static_cast<std::size_t>(n * n), 0.0);
    for (int from = 0; from < n; ++from) {
        double total = 0.0;
        for (int image = -4 * n; image <= 4 * n; ++image) {
            int to = image;
            while (to < 0 || to >= n) to = to < 0 ? -1 - to : 2 * n - 1 - to;
            const double d = (image - from) / std::max(sd_steps, 1e-9);
            const double g = std::exp(-0.5 * d * d);
            kernel[static_cast<std::size_t>(to * n + from)] += g;
            total += g;
        }
        for (int to = 0; to < n; ++to) kernel[static_cast<std::size_t>(to * n + from)] /= total;
    }
    return kernel;
}

// x' = K x (forward) or K^T x (backward), n at most 256.
void apply_matrix(const std::vector<double>& kernel, int n, double* x, bool backward) {
    double buffer[256];
    const std::size_t un = static_cast<std::size_t>(n);
    for (std::size_t i = 0; i < un; ++i) buffer[i] = 0.0;
    if (!backward) {
        for (std::size_t to = 0; to < un; ++to) {
            double acc = 0.0;
            const double* row = kernel.data() + to * un;
            for (std::size_t from = 0; from < un; ++from) acc += row[from] * x[from];
            buffer[to] = acc;
        }
    } else {
        for (std::size_t to = 0; to < un; ++to) {
            const double* row = kernel.data() + to * un;
            const double value = x[to];
            for (std::size_t from = 0; from < un; ++from) buffer[from] += row[from] * value;
        }
    }
    std::copy_n(buffer, n, x);
}

}  // namespace

void LevelEvidence::step_kind(double* x, int kind, bool backward) const {
    const int n = levels_;
    // J: x <- (1 - jump) x + jump x mean over the levels, the jump over the block's length.
    const auto jump_step = [&] {
        double sum = 0.0;
        for (int i = 0; i < n; ++i) sum += x[i];
        const double jump = kind == 2 ? fast_jump_ : kind == 3 ? fast_block_jump_ : tables_.jump();
        const double mean = sum / n;
        for (int i = 0; i < n; ++i) x[i] = (1.0 - jump) * x[i] + jump * mean;
    };
    // The fast walk's matrix (text-fast-1), or the walk from each level its own (level_walk_high_nepers_per_sqrt_s): x' = J K x
    // forward, K^T J x backward. K's columns sum to 1, its rows need not, so the order matters (review of text-level-1).
    const std::vector<double>& kernel = kind == 2 ? fast_kernel_ : kind == 3 ? fast_block_kernel_ : tables_.walk_kernel();
    if (!kernel.empty()) {
        if (backward) jump_step();
        apply_matrix(kernel, n, x, backward);
        if (!backward) jump_step();
        return;
    }
    {
        double buffer[256];
        const double p = tables_.walk_p();
        const double keep = 1.0 - 2.0 * p;
        // Three taps, ends reflecting, from one buffer into the other: a loop a compiler can make into vectors, as an update in place is not.
        double* from = x;
        double* to = buffer;
        for (int pass = 0; n > 1 && pass < tables_.walk_passes(); ++pass) {
            to[0] = keep * from[0] + p * (from[0] + from[1]);
            for (int i = 1; i < n - 1; ++i) to[i] = keep * from[i] + p * (from[i - 1] + from[i + 1]);
            to[n - 1] = keep * from[n - 1] + p * (from[n - 2] + from[n - 1]);
            std::swap(from, to);
        }
        if (from != x) std::copy_n(from, n, x);
    }
    jump_step();
}

void LevelEvidence::end_frame() {
    const std::size_t levels = static_cast<std::size_t>(levels_);
    frame_fill_ = 0;
    const std::size_t ended_slot = static_cast<std::size_t>(frames_ % ring_frames_);
    ++frames_;
    // At a block's end the level steps, and the next block takes the mode asked for (set_walk).
    int kind = 0;
    if (++block_fill_ >= block_frames_) {
        kind = !fast_ ? 1 : block_frames_ == 1 ? 2 : 3;
        step_kind(alpha_.data(), kind);
        step_kind(alpha_.data() + levels, kind);
        fast_ = fast_next_;
        short_ = short_next_;
        if (fast_ && fast_walk_next_ != fast_walk_) {
            // The walk's variance over a frame is W^2 T nepers^2, its sd W sqrt(T) / step in grid steps; over a block of
            // level_frame_s, sqrt(slow_block_) times that. A matrix, not the three-tap passes, whose count goes as W^2.
            fast_walk_ = fast_walk_next_;
            const double step_nepers = tables_.step_db() / 20.0 * std::log(10.0);
            const double sd_steps = fast_walk_ * std::sqrt(frame_s_) / step_nepers;
            fast_kernel_ = walk_matrix(levels_, sd_steps);
            fast_block_kernel_ = walk_matrix(levels_, sd_steps * std::sqrt(static_cast<double>(slow_block_)));
        }
        // Fast: a frame to a block, unless the channel's tail is modeled, where a short frame lets the level follow the tail into
        // the gaps (the clips: 0.352 and 0.247 with the long frame, 0.841 and 0.766 with the short; heuristic by measurement).
        block_frames_ = fast_ && short_ && !echo_on_ ? 1 : slow_block_;
        block_fill_ = 0;
        block_first_ = frames_;
    }
    step_after_[ended_slot] = static_cast<std::uint8_t>(kind);
    // The prediction into the next frame; the belief over levels it gives that frame sums the key out.
    const std::size_t slot = static_cast<std::size_t>(frames_ % ring_frames_);
    block_start_[slot] = block_first_;
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
        // The belief this frame's samples read: what enters its block from each side, the block's own samples left out.
        if (t == last || step_after_[slot] != 0)
            for (std::size_t level = 0; level < levels; ++level) beta_block_[level] = up[level] + down[level];
        if (t <= last_committed) {
            const std::size_t first = static_cast<std::size_t>(block_start_[slot] % ring_frames_);
            double total = 0.0;
            for (std::size_t level = 0; level < levels; ++level) {
                const double value = entering_[first * levels + level] * beta_block_[level];
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
        // The step at this frame's start, if a block ended there.
        if (t > 0) {
            const int kind = step_after_[static_cast<std::size_t>((t - 1) % ring_frames_)];
            if (kind != 0) {
                step_kind(up, kind, true);
                step_kind(down, kind, true);
            }
        }
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
                down[i] = echo_on_ ? term_value(echo_fall_[static_cast<std::size_t>(position * tables_.table_levels() + edge_level[i] - shift +
                                                                                    tables_.below())], z, z2, w)
                                   : term_value(tables_.fall(position, edge_level[i], shift), z, z2, w);
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
        int tail_count = 0;
        if (echo_on_ && edge_count > 0) {
            // The tail's positions after a fall: noise plus the tail's power at the belief's levels, against key-up.
            const int table_levels = tables_.table_levels();
            const int below = tables_.below();
            for (int q = 0; q < echo_positions_; ++q) {
                double values[kEdgeLevels];
                double value_top = -1e300;
                for (int i = 0; i < edge_count; ++i) {
                    const std::size_t at = static_cast<std::size_t>(q * table_levels + edge_level[i] - shift + below);
                    values[i] = w * (z2 * static_cast<double>(echo_gain_[at]) - static_cast<double>(echo_cost_[at]));
                    value_top = std::max(value_top, values[i]);
                }
                double tail_sum = 0.0;
                for (int i = 0; i < edge_count; ++i) tail_sum += edge_p[i] * fast::exp_neg(values[i] - value_top);
                tail_scratch_[q] = value_top + fast::log(tail_sum);
            }
            tail_count = echo_positions_;
        }
        sums_.push(l1, rise, fall, context, tail_scratch_, tail_count);
    }
}

}  // namespace kz4ap::segmental
