// The new decoder behind the engine's Decoder interface (first prototype; docs/signal-processing.md, section 8d and
// appendix A.8d): one per channel. The channel arrives centered on its station (the channelizer follows the detector,
// ChannelFollower; design Part 1), and the decoder reads it as received: it tracks no frequency and re-centers nothing
// (design Part 10). Each block's samples are summed into pieces of samples_per_piece samples (piece_s, 2 ms: 3 samples
// at 1 500 samples/s), which
// the search (viterbi/search.hpp) takes one by one with the engine's hiss N0, searching each 2 pieces (4 ms) later, the
// look-ahead of its systematic term. The text is the best story's, published when the channel ends (flush).
#pragma once

#include "kz4ap/channelizer.hpp"
#include "kz4ap/decoder.hpp"
#include "kz4ap/types.hpp"
#include "kz4ap/viterbi/config.hpp"
#include "kz4ap/viterbi/long_run.hpp"
#include "kz4ap/viterbi/observation.hpp"
#include "kz4ap/viterbi/search.hpp"

#include <complex>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace kz4ap {

// What every ViterbiDecoder of a configuration on one channel filter shares, computed once from the channelizer's own
// taps and held by each decoder as std::shared_ptr<const ViterbiShared>, so that one flat prior serves every channel of
// a configuration (its tables are 3 x 4 001 doubles per block length: 18.7 MB at the defaults, built in about 0.26 s):
// the noise sums c(m), the rise of a mark's first pieces and the flat prior built with that rise. make_viterbi_shared
// is its only maker; it cannot be made, copied or changed otherwise, so its rise and its flat prior always come from
// the channelizer's taps, and no decoder can be given a prior without the matched filter to the rise.
class ViterbiShared {
public:
    ViterbiShared(const ViterbiShared&) = delete;
    ViterbiShared& operator=(const ViterbiShared&) = delete;

    // samples/s: the channelizer's output rate
    double channel_rate_hz() const { return channel_rate_hz_; }
    // channel samples in a piece: piece_s x channel_rate_hz, a whole number
    int samples_per_piece() const { return samples_per_piece_; }
    // Hz: c(m) (viterbi::sum_noise_hz) for m = 0 ... samples_per_piece x the longest block of the configurations it was
    // built for, on the channelizer's taps
    const std::vector<double>& c() const { return c_; }
    // g_1 ... g_J: the expected fraction of a full piece's carrier sum in a mark's first pieces (viterbi::rise_fractions),
    // from the channelizer's taps, its decimation, samples_per_piece and keying_rise_s
    const std::vector<double>& rise() const { return rise_; }
    // E_1 ... E_N: the matched filter's systematic error at a mark's start (viterbi::rise_template_errors), from the
    // same taps and edge: the edge's share of the systematic term (search.hpp)
    const std::vector<double>& template_errors() const { return template_errors_; }
    // the flat prior of a newborn's first block (viterbi::FlatPrior), built with rise() and template_errors(), for
    // blocks of 1 ... max_pieces() pieces: the longest first block among the configurations
    // (viterbi_first_block_pieces)
    const std::shared_ptr<const viterbi::FlatPrior>& flat_prior() const { return flat_prior_; }
    // What it was built from, which every decoder that uses it must share: s, s, dB S500, dB S500
    double piece_s() const { return piece_s_; }
    double keying_rise_s() const { return keying_rise_s_; }
    double level_min_db() const { return level_min_db_; }
    double level_max_db() const { return level_max_db_; }
    // The default Omega_bar table for that level range (viterbi/long_run.hpp; omega_bar_default_table): the compiled one,
    // or the one loaded for other bounds. Not read by the decoder yet (plan 1b, Task 10).
    const viterbi::OmegaBarTable& omega_bar() const { return *omega_bar_; }

private:
    ViterbiShared() = default;
    friend std::shared_ptr<const ViterbiShared> make_viterbi_shared(std::span<const viterbi::ViterbiConfig> configs,
                                                                    const ChannelizerConfig& channel,
                                                                    const std::string& omega_bar_default_table);

    double channel_rate_hz_ = 0.0;
    int samples_per_piece_ = 0;
    std::vector<double> c_;
    std::vector<double> rise_;
    std::vector<double> template_errors_;
    std::shared_ptr<const viterbi::FlatPrior> flat_prior_;
    double piece_s_ = 0.0;
    double keying_rise_s_ = 0.0;
    double level_min_db_ = 0.0;
    double level_max_db_ = 0.0;
    std::shared_ptr<const viterbi::OmegaBarTable> omega_bar_;
};

// The default Omega_bar table for the prior's bounds level_min_db and level_max_db (dB S500; viterbi/long_run.hpp): the
// compiled table (-15 and +85 dB S500) when path is empty, else the table in the file at path (kz4ap-viterbi-tables
// omega-bar --lo-db L --hi-db H). Throws std::invalid_argument naming both pairs of bounds when the table's differ from
// them (Ruling 72, N14), and std::runtime_error for a file that is not such a table.
viterbi::OmegaBarTable omega_bar_default_table(double level_min_db, double level_max_db, const std::string& path);

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
// channel filter of `channel` (channel_filter_taps; its decimation fft_size / channel_bins and its output rate): the
// only way to make them. The configurations must agree on piece_s, keying_rise_s, level_min_db and level_max_db; c
// reaches samples_per_piece x the longest B among them (the longest first block where none fades), the rise is
// rise_fractions of the channelizer's taps, and the flat prior, built with that rise, spans their longest first block.
// Throws std::invalid_argument for no configuration, configurations that disagree, a piece_s that is not a whole
// number of channel samples (within 1e-9 of one: 2 ms is 3 samples at 1 500 samples/s, 48 kHz x 2^j, and 4 at 2 000,
// 32 kHz, but 2.756 at 1 378.125 samples/s, 44.1 and 88.2 kHz, which is refused, with the numbers), and what
// channel_filter_taps, rise_fractions and FlatPrior refuse. omega_bar_default_table: the path of a default Omega_bar
// table for the configurations' level range, or empty for the compiled one; a range other than the compiled table's
// (-15 and +85 dB S500) without a table for it is refused, naming both (omega_bar_default_table above).
std::shared_ptr<const ViterbiShared> make_viterbi_shared(std::span<const viterbi::ViterbiConfig> configs,
                                                         const ChannelizerConfig& channel,
                                                         const std::string& omega_bar_default_table = {});
inline std::shared_ptr<const ViterbiShared> make_viterbi_shared(const viterbi::ViterbiConfig& config,
                                                                const ChannelizerConfig& channel,
                                                                const std::string& omega_bar_default_table = {}) {
    return make_viterbi_shared(std::span<const viterbi::ViterbiConfig>(&config, 1), channel, omega_bar_default_table);
}

class ViterbiDecoder final : public Decoder {
public:
    // rate_hz: the channel's sample rate, samples/s. config: the decoder's configuration, genie values included.
    // shared: the parts built for this configuration on this channel's filter (make_viterbi_shared), shared so that one
    // flat prior serves every channel; the decoder could not reach the channelizer's taps otherwise.
    // Builds now the tables that would otherwise be built on first use (viterbi::build_evidence_tables, and the
    // fist's), so that no block stalls on them. Throws std::invalid_argument for a rate_hz that is not positive and
    // finite, a piece_s that is not a whole number of channel samples at rate_hz (with the numbers: 44.1 and 88.2 kHz
    // give 1 378.125 samples/s, where 2 ms is 2.756 samples), no shared parts, shared parts built for another channel
    // rate, piece_s, keying_rise_s or level range, or too short for this configuration's blocks (c, or the flat prior's
    // span against viterbi_first_block_pieces), and what Search refuses.
    ViterbiDecoder(double rate_hz, const viterbi::ViterbiConfig& config, std::shared_ptr<const ViterbiShared> shared);

    // Takes the block's samples as received and sums them into pieces: a piece completed goes to the search with its
    // time, the end of its last sample (t_first + samples_per_piece x (k + 1) / rate_hz for piece k), and N0, unless N0
    // is 0, when it is not searched (exact zeros, or no hiss frame yet; the search refuses an N0 that is not positive).
    // The search holds the last pieces it was given for its look-ahead (Search::step) until more come or flush. A
    // piece not searched still counts in time, so if N0 falls to 0 and returns mid-stream the search sees the stream
    // spliced: every story goes on from where it stood, the length of its element frozen across the skipped pieces and
    // its level predicted across the time (the engine holds N0 through frames of exact zeros, so after its first
    // counted spectrum frame it does this only if a quarter of a channel's neighborhood reads exact zeros for minutes;
    // a direct caller can at any time). A remainder of 1 or 2 samples waits for the next block. The first block's t0_s
    // is the channel's time origin; every later block must follow the previous one without a gap (to within half a
    // sample). Returns nothing: the text comes at flush, and the decoder tracks no frequency (no freq_offset_hz). After
    // flush() the samples are ignored. The output does not depend on how the stream is split into blocks.
    DecodeUpdate process(std::span<const Sample> samples, double t0_s) override;
    // End of the channel: the search takes the pieces it held for its look-ahead (Search::finish), then the best
    // story's text (Search::best_text; each character's probability 1, speed and confidence 0: placeholders), all of
    // it at once. A partial piece is dropped. Further calls return nothing.
    DecodeUpdate flush() override;
    // Starts afresh: a new search, a new time origin at the next block. The N0 last set is kept (the engine sets it
    // before every block).
    void reset() override;
    // (set_frequency_anchor_hz: the Decoder interface's empty default. The decoder neither re-centers nor reads the
    // detector's frequency: the channelizer follows it, design Parts 1 and 10.)
    // N0 around the channel, FS^2/Hz, for the next block. Throws std::invalid_argument for a density that is negative or
    // not finite.
    void set_noise_density_fs2_per_hz(double n0_fs2_per_hz) override;

    // The search's instrumentation (kept only when config.stats is true).
    const viterbi::SearchStats& stats() const { return search_.stats(); }
    // For tests and the bench: the search (until flush, as of the pieces it has searched: all but those it holds for
    // its look-ahead).
    const viterbi::Search& search() const { return search_; }

private:
    double rate_;
    viterbi::ViterbiConfig config_;
    std::shared_ptr<const ViterbiShared> shared_;
    viterbi::Search search_;
    double n0_ = 0.0;                     // FS^2/Hz: the hiss for the next block
    bool started_ = false;                // a block has arrived since the start or the last reset
    bool flushed_ = false;
    double origin_s_ = 0.0;               // s: the time of the channel's first sample
    std::uint64_t samples_ = 0;           // channel samples taken since the origin
    std::uint64_t pieces_ = 0;            // pieces completed since the origin
    std::complex<double> piece_sum_ = 0.0;  // FS: the current piece's sum so far
    int held_ = 0;                        // samples in it
};

}  // namespace kz4ap
