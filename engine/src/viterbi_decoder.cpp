#include "kz4ap/viterbi_decoder.hpp"

#include "kz4ap/viterbi/fist.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <numbers>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace kz4ap {

namespace {

// s: the longest first block of a newborn, the flat prior's span (heuristic: past the longest median mark of the grid,
// the 691 ms dash at 5.2 WPM; the tables cost 96 KB per piece of span). It binds only for a fading spread below
// 0.195 Hz, where the block length passes it (viterbi_decoder.hpp).
constexpr double kFirstBlockCapS = 2.0;

bool positive_and_finite(double x) { return x > 0.0 && std::isfinite(x); }

void require(bool ok, const char* message) {
    if (!ok) throw std::invalid_argument(message);
}

// The channel samples in a piece of piece_s seconds at rate_hz samples/s, which must be a whole number (within 1e-9 of
// one): 2 ms is 3 samples at 1 500 samples/s, but 2.756 at 1 378.125 samples/s (44.1 and 88.2 kHz), refused with the
// numbers. `who` names the caller in the message.
int whole_samples_per_piece(double piece_s, double rate_hz, const char* who) {
    const double per_piece = piece_s * rate_hz;
    const double whole = std::round(per_piece);
    if (positive_and_finite(per_piece) && whole >= 1.0 && whole <= std::numeric_limits<int>::max() &&
        std::fabs(per_piece - whole) <= 1e-9 * per_piece)
        return static_cast<int>(whole);
    std::ostringstream message;
    message << std::setprecision(10) << who << ": a piece of " << piece_s << " s is " << per_piece
            << " channel samples at " << rate_hz << " samples/s; piece_s x the channel rate must be a whole number of "
            << "samples (the default 2 ms piece is not one at 44.1 or 88.2 kHz)";
    throw std::invalid_argument(message.str());
}

}  // namespace

std::optional<int> viterbi_block_pieces(const viterbi::ViterbiConfig& config) {
    require(positive_and_finite(config.piece_s), "viterbi: piece_s must be positive and finite (s)");
    const double spread_hz = config.genie_fading_spread_hz.value_or(config.fading_spread_hz);
    const double block_s = viterbi::block_length_s(spread_hz, config.block_loss_db);
    if (std::isinf(block_s)) return std::nullopt;
    const double pieces = std::floor(block_s / config.piece_s);
    require(pieces >= 1.0, "viterbi: a block must hold at least one piece");
    return pieces >= static_cast<double>(std::numeric_limits<int>::max()) ? std::numeric_limits<int>::max()
                                                                          : static_cast<int>(pieces);
}

int viterbi_first_block_pieces(const viterbi::ViterbiConfig& config) {
    const std::optional<int> block = viterbi_block_pieces(config);
    const int cap = std::max(1, static_cast<int>(std::floor(kFirstBlockCapS / config.piece_s)));
    return block ? std::min(*block, cap) : cap;
}

std::shared_ptr<const ViterbiShared> make_viterbi_shared(std::span<const viterbi::ViterbiConfig> configs,
                                                         const ChannelizerConfig& channel) {
    require(!configs.empty(), "make_viterbi_shared: no configuration");
    const viterbi::ViterbiConfig& first = configs.front();
    for (const viterbi::ViterbiConfig& config : configs)
        require(config.piece_s == first.piece_s && config.keying_rise_s == first.keying_rise_s &&
                    config.level_min_db == first.level_min_db && config.level_max_db == first.level_max_db,
                "make_viterbi_shared: the configurations must agree on piece_s, keying_rise_s and the level range");
    const std::vector<double> taps = channel_filter_taps(channel);  // checks the channelizer's configuration
    const int decimation = channel.fft_size / channel.channel_bins;
    std::shared_ptr<ViterbiShared> shared(new ViterbiShared());  // its constructor is private: this is its only maker
    shared->channel_rate_hz_ = static_cast<double>(channel.sample_rate) / decimation;
    // The piece is a time, converted here to channel samples, which must be a whole number of them.
    shared->samples_per_piece_ =
        whole_samples_per_piece(first.piece_s, shared->channel_rate_hz_, "make_viterbi_shared");
    int first_block = 1;  // pieces: the longest first block, the flat prior's span
    int longest = 1;      // pieces: the longest block, c's span
    for (const viterbi::ViterbiConfig& config : configs) {
        const int first_pieces = viterbi_first_block_pieces(config);
        first_block = std::max(first_block, first_pieces);
        longest = std::max(longest, viterbi_block_pieces(config).value_or(first_pieces));
    }
    const auto samples = static_cast<std::int64_t>(shared->samples_per_piece_) * longest;
    require(samples < std::numeric_limits<int>::max(),
            "make_viterbi_shared: the longest block must stay below 2^31 channel samples");
    shared->c_ = viterbi::sum_noise_hz(taps, channel.sample_rate, decimation, static_cast<int>(samples));
    // The rise from the channelizer's own taps, and the flat prior built with it: the matched filter to the rise that
    // every newborn's first block is scored with (the decoder cannot be given a prior without it).
    shared->rise_ = viterbi::rise_fractions(taps, channel.sample_rate, decimation, shared->samples_per_piece_,
                                            first.keying_rise_s);
    // The template's systematic error at a mark's start, from the same taps and edge: the edge's share of the
    // systematic term, which the flat prior hands to the search.
    shared->template_errors_ = viterbi::rise_template_errors(taps, channel.sample_rate, decimation,
                                                             shared->samples_per_piece_, first.keying_rise_s);
    shared->flat_prior_ = std::make_shared<const viterbi::FlatPrior>(
        first, shared->c_, first_block, shared->samples_per_piece_, shared->rise_, shared->template_errors_);
    shared->piece_s_ = first.piece_s;
    shared->keying_rise_s_ = first.keying_rise_s;
    shared->level_min_db_ = first.level_min_db;
    shared->level_max_db_ = first.level_max_db;
    return shared;
}

namespace {

// The shared parts after checking that they serve this decoder: built for its channel rate, piece, rise and level
// range, with c and the flat prior long enough for its blocks.
std::shared_ptr<const ViterbiShared> checked(double rate_hz, const viterbi::ViterbiConfig& config,
                                             std::shared_ptr<const ViterbiShared> shared) {
    require(positive_and_finite(rate_hz), "ViterbiDecoder: rate_hz must be positive and finite (samples/s)");
    (void)whole_samples_per_piece(config.piece_s, rate_hz, "ViterbiDecoder");
    require(shared && shared->flat_prior(), "ViterbiDecoder: no shared parts");
    require(std::fabs(shared->channel_rate_hz() - rate_hz) <= 1e-9 * rate_hz,
            "ViterbiDecoder: the shared parts were built for another channel rate");
    require(shared->piece_s() == config.piece_s && shared->keying_rise_s() == config.keying_rise_s &&
                shared->level_min_db() == config.level_min_db && shared->level_max_db() == config.level_max_db,
            "ViterbiDecoder: the shared parts were built for another piece_s, keying_rise_s or level range");
    const int first_block = viterbi_first_block_pieces(config);
    require(shared->flat_prior()->max_pieces() >= first_block,
            "ViterbiDecoder: the shared flat prior is shorter than this configuration's first block");
    const int longest = viterbi_block_pieces(config).value_or(shared->flat_prior()->max_pieces());
    require(shared->c().size() >
                static_cast<std::size_t>(shared->samples_per_piece()) * static_cast<std::size_t>(longest),
            "ViterbiDecoder: the shared c is shorter than this configuration's blocks");
    if (config.genie_frequency_offset_hz)
        require(std::isfinite(*config.genie_frequency_offset_hz) &&
                    std::isfinite(config.genie_frequency_drift_hz_per_s),
                "ViterbiDecoder: the genie's frequency and drift must be finite (Hz, Hz/s)");
    return shared;
}

}  // namespace

ViterbiDecoder::ViterbiDecoder(double rate_hz, const viterbi::ViterbiConfig& config, double residual_hz,
                               std::shared_ptr<const ViterbiShared> shared)
    : rate_(rate_hz),
      config_(config),
      shared_(checked(rate_hz, config, std::move(shared))),
      search_(config, shared_->c(), shared_->flat_prior()),
      anchor_hz_(residual_hz) {
    require(std::isfinite(residual_hz), "ViterbiDecoder: residual_hz must be finite (Hz)");
    // The tracker's configuration is checked now; the tracker itself starts at the first block's anchor.
    (void)viterbi::CarrierTracker(rate_hz, config, residual_hz);
    // Builds now the tables that would otherwise be built on first use: the evidence's (the broad-prior regime's,
    // 31 ms, and the truncated nodes') and the fist's ln Phi_c (1.8 ms), each built once for every caller.
    viterbi::build_evidence_tables(config.quadrature_order);
    (void)viterbi::Fist(config).log_survival(viterbi::Kind::Dot, 0, 1);
}

DecodeUpdate ViterbiDecoder::process(std::span<const Sample> samples, double t0_s) {
    if (flushed_ || samples.empty()) return {};
    require(std::isfinite(t0_s), "ViterbiDecoder::process: t0_s must be finite (s)");
    if (!started_) {
        tracker_.emplace(rate_, config_, anchor_hz_);  // the anchor in force at the channel's first block
        origin_s_ = t0_s;
        started_ = true;
    } else {
        const double expected_s = origin_s_ + static_cast<double>(samples_) / rate_;
        require(std::fabs(t0_s - expected_s) <= 0.5 / rate_,
                "ViterbiDecoder::process: a block must start where the previous one ended (to within half a sample)");
    }
    const bool genie = config_.genie_frequency_offset_hz.has_value();
    const double genie_hz = config_.genie_frequency_offset_hz.value_or(0.0);
    const double genie_drift = config_.genie_frequency_drift_hz_per_s;
    const int samples_per_piece = shared_->samples_per_piece();
    const auto per_piece = static_cast<std::uint64_t>(samples_per_piece);
    for (const Sample& y : samples) {
        // Sample n's time, one expression from the origin; the tracker takes the sample first, so that the oscillator's
        // offset includes every window the sample completes, then the offset at that time is read.
        const double t = origin_s_ + static_cast<double>(samples_) / rate_;
        tracker_->push(std::span<const Sample>(&y, 1), t, n0_);
        offset_hz_ = genie ? genie_hz + genie_drift * t : tracker_->offset_hz(t);
        // Re-centered by the oscillator's phase before this sample's own advance: exp(-j 2 pi phase).
        const std::complex<double> turn = std::polar(1.0, -2.0 * std::numbers::pi * phase_cycles_);
        piece_sum_ += std::complex<double>(y.real(), y.imag()) * turn;
        phase_cycles_ += offset_hz_ / rate_;
        phase_cycles_ -= std::floor(phase_cycles_);
        ++samples_;
        if (++held_ < samples_per_piece) continue;
        // Piece k ends with its last sample: t_first + samples_per_piece x (k + 1) / r.
        ++pieces_;
        const double t_piece = origin_s_ + static_cast<double>(per_piece * pieces_) / rate_;
        if (n0_ > 0.0) search_.step(piece_sum_, t_piece, n0_);
        piece_sum_ = 0.0;
        held_ = 0;
    }
    DecodeUpdate update;
    update.freq_offset_hz = offset_hz_;
    return update;
}

DecodeUpdate ViterbiDecoder::flush() {
    if (flushed_) return {};
    flushed_ = true;
    search_.finish();  // the pieces the search holds for its look-ahead
    DecodeUpdate update;
    update.chars = search_.best_text();
    if (started_) update.freq_offset_hz = offset_hz_;
    return update;
}

void ViterbiDecoder::reset() {
    search_ = viterbi::Search(config_, shared_->c(), shared_->flat_prior());
    tracker_.reset();
    started_ = false;
    flushed_ = false;
    origin_s_ = 0.0;
    samples_ = 0;
    pieces_ = 0;
    piece_sum_ = 0.0;
    held_ = 0;
    phase_cycles_ = 0.0;
    offset_hz_ = 0.0;
}

void ViterbiDecoder::set_frequency_anchor_hz(double offset_hz) { anchor_hz_ = offset_hz; }

void ViterbiDecoder::set_noise_density_fs2_per_hz(double n0_fs2_per_hz) {
    require(std::isfinite(n0_fs2_per_hz) && n0_fs2_per_hz >= 0.0,
            "ViterbiDecoder: the noise density must be finite and not negative (FS^2/Hz)");
    n0_ = n0_fs2_per_hz;
}

}  // namespace kz4ap
