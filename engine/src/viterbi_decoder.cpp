#include "kz4ap/viterbi_decoder.hpp"

#include "kz4ap/viterbi/fist.hpp"
#include "kz4ap/viterbi/observation.hpp"

#include <cmath>
#include <cstdint>
#include <iomanip>
#include <memory>
#include <numbers>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace kz4ap {

namespace {

bool positive_and_finite(double x) { return x > 0.0 && std::isfinite(x); }

void require(bool ok, const char* message) {
    if (!ok) throw std::invalid_argument(message);
}

// The channel samples per envelope sample: sample_interval_s at rate_hz samples/s, a whole number (within 1e-9 of one;
// 4 / 1 500 s is 4 samples at 1 500 samples/s, but 3.675 at 1 378.125 samples/s, 44.1 and 88.2 kHz, refused with the
// numbers and the field's name).
int whole_samples_per_sample(double sample_interval_s, double rate_hz) {
    require(positive_and_finite(sample_interval_s), "viterbi: sample_interval_s must be positive and finite (s)");
    return viterbi::whole_count(sample_interval_s, 1.0 / rate_hz, "sample_interval_s", "channel samples");
}

}  // namespace

viterbi::OmegaBarTable omega_bar_default_table(double level_min_db, double level_max_db, const std::string& path) {
    viterbi::OmegaBarTable table =
        path.empty() ? viterbi::OmegaBarTable::compiled() : viterbi::OmegaBarTable::load(path);
    // The bounds are configuration values, compared as written (within 1e-9 dB, for a file's rounding).
    if (std::fabs(table.lo_db() - level_min_db) > 1e-9 || std::fabs(table.hi_db() - level_max_db) > 1e-9) {
        std::ostringstream message;
        message << "the level range " << level_min_db << " to " << level_max_db << " dB S500 (level_min_db, "
                << "level_max_db) differs from the default Omega_bar table's, " << table.lo_db() << " to "
                << table.hi_db() << " dB S500 ("
                << (path.empty() ? std::string("the compiled table") : "the table in " + path)
                << "): give a table generated for that range (kz4ap-viterbi-tables omega-bar --lo-db L --hi-db H; "
                << "EngineConfig::viterbi_omega_bar_default_table, kz4ap-bench --omega-bar-default-table)";
        throw std::invalid_argument(message.str());
    }
    return table;
}

std::shared_ptr<const ViterbiShared> make_viterbi_shared(std::span<const viterbi::ViterbiConfig> configs,
                                                         const ChannelizerConfig& channel,
                                                         const std::string& omega_bar_default_table_path) {
    require(!configs.empty(), "make_viterbi_shared: no configuration");
    const viterbi::ViterbiConfig& first = configs.front();
    for (const viterbi::ViterbiConfig& config : configs)
        require(config.sample_interval_s == first.sample_interval_s && config.front_dot_s == first.front_dot_s &&
                    config.edge_rise_s == first.edge_rise_s && config.edge_rise_min_s == first.edge_rise_min_s &&
                    config.edge_rise_max_s == first.edge_rise_max_s && config.level_min_db == first.level_min_db &&
                    config.level_max_db == first.level_max_db &&
                    config.newborn_grid_points == first.newborn_grid_points,
                "make_viterbi_shared: the configurations must agree on sample_interval_s, front_dot_s, the edge rise "
                "times, the level range and newborn_grid_points");
    require(first.level_max_db > first.level_min_db && std::isfinite(first.level_min_db) &&
                std::isfinite(first.level_max_db),
            "make_viterbi_shared: the level range must be finite and level_max_db above level_min_db (dB S500)");
    // The default Omega_bar table for their level range, first: a range without one is refused before any building.
    auto omega_bar = std::make_shared<const viterbi::OmegaBarTable>(
        omega_bar_default_table(first.level_min_db, first.level_max_db, omega_bar_default_table_path));
    const std::vector<double> taps = channel_filter_taps(channel);  // checks the channelizer's configuration
    const int decimation = channel.fft_size / channel.channel_bins;
    std::shared_ptr<ViterbiShared> shared(new ViterbiShared());  // its constructor is private: this is its only maker
    // The rate exactly as the Channelizer computes it (a division of two integers held as doubles).
    shared->channel_rate_hz_ = static_cast<double>(channel.sample_rate) / decimation;
    // The sample interval is a time, converted here to channel samples, which must be a whole number of them.
    shared->channel_samples_per_sample_ = whole_samples_per_sample(first.sample_interval_s, shared->channel_rate_hz_);
    const int spacing = shared->channel_samples_per_sample_;
    // Every figure from the running channelizer's own taps (O4): the front filter, its output noise, the template.
    shared->front_taps_ = viterbi::front_filter_taps(shared->channel_rate_hz_, first.front_dot_s, first.edge_rise_s);
    shared->output_noise_ =
        viterbi::output_noise(taps, channel.sample_rate, decimation, shared->front_taps_, spacing);
    shared->template_tables_ =
        viterbi::make_template_tables(taps, channel.sample_rate, decimation, shared->front_taps_, spacing,
                                      first.edge_rise_s, first.edge_rise_min_s, first.edge_rise_max_s);
    shared->omega_bar_ = std::move(omega_bar);
    // The newborn's grid spans the level range in u = ln a: (level_max_db - level_min_db) / 20 x ln 10 nepers.
    const double span_nepers = (first.level_max_db - first.level_min_db) / 20.0 * std::numbers::ln10;
    shared->newborn_tables_ = std::make_unique<const viterbi::NewbornTables>(
        shared->output_noise_, shared->output_noise_.sigma2_per_n0_hz, first.newborn_grid_points, span_nepers);
    shared->sample_interval_s_ = first.sample_interval_s;
    shared->front_dot_s_ = first.front_dot_s;
    shared->edge_rise_s_ = first.edge_rise_s;
    shared->edge_rise_min_s_ = first.edge_rise_min_s;
    shared->edge_rise_max_s_ = first.edge_rise_max_s;
    shared->level_min_db_ = first.level_min_db;
    shared->level_max_db_ = first.level_max_db;
    shared->newborn_grid_points_ = first.newborn_grid_points;
    return shared;
}

namespace {

// The shared parts after checking that they serve this decoder: built for its channel rate, front filter, edges,
// sample interval, level range and newborn grid.
std::shared_ptr<const ViterbiShared> checked(double rate_hz, const viterbi::ViterbiConfig& config,
                                             std::shared_ptr<const ViterbiShared> shared) {
    require(positive_and_finite(rate_hz), "ViterbiDecoder: rate_hz must be positive and finite (samples/s)");
    require(shared != nullptr, "ViterbiDecoder: no shared parts");
    require(std::fabs(shared->channel_rate_hz() - rate_hz) <= 1e-9 * rate_hz,
            "ViterbiDecoder: the shared parts were built for another channel rate");
    require(shared->sample_interval_s() == config.sample_interval_s && shared->front_dot_s() == config.front_dot_s &&
                shared->edge_rise_s() == config.edge_rise_s && shared->edge_rise_min_s() == config.edge_rise_min_s &&
                shared->edge_rise_max_s() == config.edge_rise_max_s &&
                shared->level_min_db() == config.level_min_db && shared->level_max_db() == config.level_max_db &&
                shared->newborn_grid_points() == config.newborn_grid_points,
            "ViterbiDecoder: the shared parts were built for another sample_interval_s, front_dot_s, edge rise, "
            "level range or newborn_grid_points");
    return shared;
}

}  // namespace

ViterbiDecoder::ViterbiDecoder(double rate_hz, const viterbi::ViterbiConfig& config,
                               std::shared_ptr<const ViterbiShared> shared)
    : rate_(rate_hz),
      config_(config),
      shared_(checked(rate_hz, config, std::move(shared))),
      search_(config, shared_),
      sampler_(shared_->front_taps(), shared_->channel_samples_per_sample()) {
    // Builds now the tables that would otherwise be built on first use: the evidence's (the broad-prior regime's and
    // the truncated nodes') and the fist's ln Phi_c (1.8 ms), each built once for every caller.
    viterbi::build_evidence_tables(config.quadrature_order);
    (void)viterbi::Fist(config).log_survival(viterbi::Kind::Dot, 0, 1);
}

DecodeUpdate ViterbiDecoder::process(std::span<const Sample> samples, double t0_s) {
    if (flushed_ || samples.empty()) return {};
    require(std::isfinite(t0_s), "ViterbiDecoder::process: t0_s must be finite (s)");
    if (!started_) {
        // The absolute index of the first channel sample (the engine's blocks start at first_index / rate).
        const double position = t0_s * rate_;
        const double index = std::round(position);
        require(index >= 0.0 && index < 9e15 && std::fabs(position - index) <= 1e-3,
                "ViterbiDecoder::process: the first block's t0_s must be a channel sample's stream time, n / rate_hz "
                "with n a whole number (to within 0.001 of a sample), not below 0");
        next_index_ = static_cast<std::uint64_t>(index);
        started_ = true;
    } else {
        const double expected_s = static_cast<double>(next_index_) / rate_;
        require(std::fabs(t0_s - expected_s) <= 0.5 / rate_,
                "ViterbiDecoder::process: a block must start where the previous one ended (to within half a sample)");
    }
    // The block's absolute indices continue the previous block's, so the front filter's history is always the
    // stream's own (EnvelopeSampler resets it only for a block that does not follow).
    envelope_.clear();
    sampler_.push(samples, next_index_, envelope_);
    next_index_ += samples.size();
    const double spacing = static_cast<double>(shared_->channel_samples_per_sample());
    for (const viterbi::EnvelopeSample& sample : envelope_) {
        // Sample k at t_k = 4 k / r: one expression from the stream's start, never a sum of increments.
        const double t = spacing * static_cast<double>(sample.k) / rate_;
        if (n0_ > 0.0) search_.step(sample.e_fs, t, n0_);
    }
    return {};
}

DecodeUpdate ViterbiDecoder::flush() {
    if (flushed_) return {};
    flushed_ = true;
    search_.finish();
    DecodeUpdate update;
    update.chars = search_.best_text();
    return update;
}

void ViterbiDecoder::reset() {
    search_ = viterbi::Search(config_, shared_);
    sampler_.reset();
    started_ = false;
    flushed_ = false;
    next_index_ = 0;
}

void ViterbiDecoder::set_noise_density_fs2_per_hz(double n0_fs2_per_hz) {
    require(std::isfinite(n0_fs2_per_hz) && n0_fs2_per_hz >= 0.0,
            "ViterbiDecoder: the noise density must be finite and not negative (FS^2/Hz)");
    n0_ = n0_fs2_per_hz;
}

}  // namespace kz4ap
