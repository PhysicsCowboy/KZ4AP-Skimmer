// The new decoder behind the engine's Decoder interface (first prototype; docs/signal-processing.md, section 8d and
// appendix A.8d): one per channel. Each block of the channel's stream goes, sample by sample, to the carrier tracker
// (viterbi/tracker.hpp), is re-centered by a phase-continuous oscillator at minus the tracker's offset (or the genie's
// frequency), and is summed into pieces of samples_per_piece samples (piece_s, 2 ms: 3 samples at 1 500 samples/s), which
// the search (viterbi/search.hpp) takes one by one with the engine's hiss N0. The text is the best story's, published
// when the channel ends (flush).
#pragma once

#include "kz4ap/channelizer.hpp"
#include "kz4ap/decoder.hpp"
#include "kz4ap/types.hpp"
#include "kz4ap/viterbi/config.hpp"
#include "kz4ap/viterbi/observation.hpp"
#include "kz4ap/viterbi/search.hpp"
#include "kz4ap/viterbi/tracker.hpp"

#include <complex>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace kz4ap {

// What every ViterbiDecoder of a configuration on one channel filter shares (the controller's Ruling 4), computed once
// from the channelizer's own taps and held by each decoder as std::shared_ptr<const ViterbiShared>: the noise sums
// c(m), the rise of a mark's first pieces and the flat prior built with that rise (its tables are 3 x 4 001 doubles per
// block length: 18.7 MB at the defaults, built in about 0.2 s).
struct ViterbiShared {
    double channel_rate_hz = 0.0;  // samples/s: the channelizer's output rate
    int samples_per_piece = 0;     // channel samples in a piece: piece_s x channel_rate_hz, a whole number (Ruling 9)
    // Hz: c(m) (viterbi::sum_noise_hz) for m = 0 ... samples_per_piece x the longest block of the configurations it was
    // built for, on the channelizer's taps
    std::vector<double> c;
    // g_1 ... g_J: the expected fraction of a full piece's carrier sum in a mark's first pieces (viterbi::rise_fractions),
    // from the channelizer's taps, its decimation, samples_per_piece and keying_rise_s
    std::vector<double> rise;
    // the flat prior of a newborn's first block (viterbi::FlatPrior), built with the rise, for blocks of 1 ...
    // max_pieces() pieces: the longest first block among the configurations (viterbi_first_block_pieces)
    std::shared_ptr<const viterbi::FlatPrior> flat_prior;
    // What it was built from, which every decoder that uses it must share: s, s, dB S500, dB S500
    double piece_s = 0.0;
    double keying_rise_s = 0.0;
    double level_min_db = 0.0;
    double level_max_db = 0.0;
};

// pieces: the longest block of a configuration, B = floor(block_length_s(f_D, block_loss_db) / piece_s), f_D being
// genie_fading_spread_hz where set, else fading_spread_hz (195 at the defaults); empty with no fading (f_D = 0), where
// a block is the whole mark. Throws what block_length_s throws, and std::invalid_argument for a piece_s that is not
// positive and finite or a B below one piece.
std::optional<int> viterbi_block_pieces(const viterbi::ViterbiConfig& config);

// pieces: the longest first block of a newborn under a configuration, the flat prior's span: min(B, the cap), the cap
// being floor(2.0 s / piece_s), 1 000 pieces at 2 ms (heuristic: past the longest median mark of the grid, the dash at
// 5.2 WPM, 691 ms; the flat prior's tables cost 96 KB per piece of span, 96 MB at the cap). The cap binds only for
// f_D below 0.195 Hz, where a newborn's first block ends at the cap and its later blocks are scored with the level the
// first one left. The same errors as viterbi_block_pieces.
int viterbi_first_block_pieces(const viterbi::ViterbiConfig& config);

// The shared parts for decoders with the given configurations (one; or, for genie runs, one per oracle channel) on the
// channel filter of `channel` (channel_filter_taps; its decimation fft_size / channel_bins and its output rate). The
// configurations must agree on piece_s, keying_rise_s, level_min_db and level_max_db; c reaches samples_per_piece x the
// longest B among them (the longest first block where none fades), and the flat prior spans their longest first block.
// Throws std::invalid_argument for no configuration, configurations that disagree, a piece_s that is not a whole number
// of channel samples (within 1e-9 of one), and what channel_filter_taps, rise_fractions and FlatPrior refuse.
std::shared_ptr<const ViterbiShared> make_viterbi_shared(std::span<const viterbi::ViterbiConfig> configs,
                                                         const ChannelizerConfig& channel);
inline std::shared_ptr<const ViterbiShared> make_viterbi_shared(const viterbi::ViterbiConfig& config,
                                                                const ChannelizerConfig& channel) {
    return make_viterbi_shared(std::span<const viterbi::ViterbiConfig>(&config, 1), channel);
}

class ViterbiDecoder final : public Decoder {
public:
    // rate_hz: the channel's sample rate, samples/s. config: the decoder's configuration, genie values included.
    // residual_hz: the station's offset from the channel's center, Hz, the anchor in force until set_frequency_anchor_hz
    // is called. shared: the parts built for this configuration on this channel's filter (make_viterbi_shared); the
    // brief's three arguments cannot reach the channelizer's taps, so the shared parts come as a fourth (Ruling 4).
    // Builds the tables the evidence reads lazily (viterbi::build_evidence_tables, and the fist's), so that no block
    // stalls on them. Throws std::invalid_argument for a rate_hz that is not positive and finite, a residual_hz that is
    // not finite, no shared parts, shared parts built for another channel rate, piece_s, keying_rise_s or level range,
    // or too short for this configuration's blocks (c, or the flat prior's span against viterbi_first_block_pieces), a
    // genie frequency or drift that is not finite, and what CarrierTracker and Search refuse.
    ViterbiDecoder(double rate_hz, const viterbi::ViterbiConfig& config, double residual_hz,
                   std::shared_ptr<const ViterbiShared> shared);

    // Takes the block, sample by sample: each sample is pushed to the tracker (with the latest N0, at its time t0_s +
    // its index over rate_hz), then re-centered by the oscillator's offset at that time, the tracker's offset_hz(t) or
    // the genie's frequency, then summed into the current piece; a piece completed goes to the search with its time,
    // the end of its last sample (t_first + samples_per_piece x (k + 1) / rate_hz for piece k), and N0, unless N0 is 0,
    // when it is not searched (exact zeros, or no hiss frame yet; Ruling 23). A remainder of 1 or 2 samples waits for
    // the next block. The first block's t0_s is the channel's time origin; every later block must follow the previous
    // one without a gap (to within half a sample; the tracker refuses a gap). Returns no characters (the text comes at
    // flush) and the oscillator's offset at the block's last sample as freq_offset_hz. After flush() the samples are
    // ignored. The output does not depend on how the stream is split into blocks.
    DecodeUpdate process(std::span<const Sample> samples, double t0_s) override;
    // End of the channel: the best story's text (Search::best_text; each character's probability 1, speed and
    // confidence 0: placeholders), all of it at once. A partial piece is dropped. Further calls return nothing.
    DecodeUpdate flush() override;
    // Starts afresh: a new search and tracker, a new time origin at the next block, the oscillator's phase at 0. The
    // anchor and N0 last set are kept (the engine sets both before every block).
    void reset() override;
    // The detector's frequency for the station, Hz from the channel's center (oracle: the label's). The tracker starts
    // at the anchor in force at the channel's first block; later anchors are not read (the tracker follows the carrier
    // itself; the first prototype runs on oracle channels).
    void set_frequency_anchor_hz(double offset_hz) override;
    // N0 around the channel, FS^2/Hz, for the next block. Throws std::invalid_argument for a density that is negative or
    // not finite.
    void set_noise_density_fs2_per_hz(double n0_fs2_per_hz) override;

    // The search's instrumentation (kept only when config.stats is true).
    const viterbi::SearchStats& stats() const { return search_.stats(); }
    // For tests and the bench: the search, and the tracker (null before the channel's first block).
    const viterbi::Search& search() const { return search_; }
    const viterbi::CarrierTracker* tracker() const { return tracker_ ? &*tracker_ : nullptr; }

private:
    double rate_;
    viterbi::ViterbiConfig config_;
    std::shared_ptr<const ViterbiShared> shared_;
    viterbi::Search search_;
    std::optional<viterbi::CarrierTracker> tracker_;
    double anchor_hz_;
    double n0_ = 0.0;                     // FS^2/Hz: the hiss for the next block
    bool started_ = false;                // a block has arrived since the start or the last reset
    bool flushed_ = false;
    double origin_s_ = 0.0;               // s: the time of the channel's first sample
    std::uint64_t samples_ = 0;           // channel samples taken since the origin
    std::uint64_t pieces_ = 0;            // pieces completed since the origin
    std::complex<double> piece_sum_ = 0.0;  // FS: the current piece's re-centered sum so far
    int held_ = 0;                        // samples in it
    double phase_cycles_ = 0.0;           // cycles, in [0, 1): the oscillator's phase at the next sample
    double offset_hz_ = 0.0;              // Hz: the oscillator's offset at the last sample
};

}  // namespace kz4ap
