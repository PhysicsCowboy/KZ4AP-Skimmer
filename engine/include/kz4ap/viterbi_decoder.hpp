// The new decoder behind the engine's Decoder interface (docs/signal-processing.md, section 8d and appendix A.8d): one
// per channel. The channel arrives centered on its station (the channelizer follows the detector, ChannelFollower;
// design Part 1), and the decoder reads it as received: it tracks no frequency and re-centers nothing (design Part 10).
// Each block's channel samples go through the 25-tap front filter, matched to a 100 WPM dot with 2.5 ms edges, read
// every 4th channel sample (EnvelopeSampler, viterbi/front_filter.hpp: e_k = |y_k|, every Delta = 2.667 ms, sample k
// being the channel sample of absolute index 4 k from the stream's start); the search (viterbi/search.hpp) takes the
// envelope samples one by one with the engine's hiss N0, searching each as it arrives. The text is the best story's,
// published when the channel ends (flush).
#pragma once

#include "kz4ap/channelizer.hpp"
#include "kz4ap/decoder.hpp"
#include "kz4ap/types.hpp"
#include "kz4ap/viterbi/config.hpp"
#include "kz4ap/viterbi/front_filter.hpp"
#include "kz4ap/viterbi/long_run.hpp"
#include "kz4ap/viterbi/mark_template.hpp"
#include "kz4ap/viterbi/newborn.hpp"
#include "kz4ap/viterbi/search.hpp"

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace kz4ap {

// What every ViterbiDecoder of a configuration on one channel filter shares, computed once from the channelizer's own
// taps (plan 1b, O4) and held by each decoder as std::shared_ptr<const ViterbiShared>: the front filter's taps, its
// output noise (sigma_y^2 / N0, the correlations and w's limits), the mark template's tables, the default Omega_bar
// table (0.42 MB) and the newborn's tables. make_viterbi_shared is its only maker; it cannot be made, copied or changed
// otherwise, so every part comes from the channelizer's taps. The fading's tables are each decoder's own
// (viterbi::FadingTables in the search, from its configuration's spread, so that each genie channel has its own;
// Ruling 72, N12).
class ViterbiShared {
public:
    ViterbiShared(const ViterbiShared&) = delete;
    ViterbiShared& operator=(const ViterbiShared&) = delete;

    // samples/s: the channelizer's output rate
    double channel_rate_hz() const { return channel_rate_hz_; }
    // channel samples per envelope sample: sample_interval_s x channel_rate_hz, a whole number (4)
    int channel_samples_per_sample() const { return channel_samples_per_sample_; }
    // the front filter's taps (viterbi::front_filter_taps), listed from j = -M
    const std::vector<double>& front_taps() const { return front_taps_; }
    // the output noise of the channel filter and the front filter (viterbi::output_noise)
    const viterbi::OutputNoise& output_noise() const { return output_noise_; }
    // the template's s_j, v_j and pair terms (viterbi::make_template_tables)
    const viterbi::TemplateTables& template_tables() const { return template_tables_; }
    // The default Omega_bar table for the level range (viterbi/long_run.hpp; omega_bar_default_table): the compiled one,
    // or the one loaded for other bounds; built once here, never per channel.
    const viterbi::OmegaBarTable& omega_bar() const { return *omega_bar_; }
    // the newborn integral's tables (viterbi::NewbornTables): its grid of newborn_grid_points over the level range
    const viterbi::NewbornTables& newborn_tables() const { return *newborn_tables_; }
    // What it was built from, which every decoder that uses it must share: s, s, s, s, s, dB S500, dB S500, count
    double sample_interval_s() const { return sample_interval_s_; }
    double front_dot_s() const { return front_dot_s_; }
    double edge_rise_s() const { return edge_rise_s_; }
    double edge_rise_min_s() const { return edge_rise_min_s_; }
    double edge_rise_max_s() const { return edge_rise_max_s_; }
    double level_min_db() const { return level_min_db_; }
    double level_max_db() const { return level_max_db_; }
    int newborn_grid_points() const { return newborn_grid_points_; }

private:
    ViterbiShared() = default;
    friend std::shared_ptr<const ViterbiShared> make_viterbi_shared(std::span<const viterbi::ViterbiConfig> configs,
                                                                    const ChannelizerConfig& channel,
                                                                    const std::string& omega_bar_default_table);

    double channel_rate_hz_ = 0.0;
    int channel_samples_per_sample_ = 0;
    std::vector<double> front_taps_;
    viterbi::OutputNoise output_noise_{};
    viterbi::TemplateTables template_tables_{};
    std::shared_ptr<const viterbi::OmegaBarTable> omega_bar_;
    std::unique_ptr<const viterbi::NewbornTables> newborn_tables_;
    double sample_interval_s_ = 0.0;
    double front_dot_s_ = 0.0;
    double edge_rise_s_ = 0.0;
    double edge_rise_min_s_ = 0.0;
    double edge_rise_max_s_ = 0.0;
    double level_min_db_ = 0.0;
    double level_max_db_ = 0.0;
    int newborn_grid_points_ = 0;
};

// The default Omega_bar table for the prior's bounds level_min_db and level_max_db (dB S500; viterbi/long_run.hpp): the
// compiled table (-15 and +85 dB S500) when path is empty, else the table in the file at path (kz4ap-viterbi-tables
// omega-bar --lo-db L --hi-db H). Throws std::invalid_argument naming both pairs of bounds when the table's differ from
// them (Ruling 72, N14), and std::runtime_error for a file that is not such a table.
viterbi::OmegaBarTable omega_bar_default_table(double level_min_db, double level_max_db, const std::string& path);

// The shared parts for decoders with the given configurations (one; or, for genie runs, one per oracle channel) on the
// channel filter of `channel` (channel_filter_taps; its decimation fft_size / channel_bins and its output rate): the
// only way to make them. The configurations must agree on sample_interval_s, front_dot_s, the edge rise times, the level
// range and newborn_grid_points. Throws std::invalid_argument for no configuration, configurations that disagree, a
// sample_interval_s that is not a whole number of channel samples (within 1e-9 of one: 4 / 1 500 s is 4 samples at
// 1 500 samples/s, 48 kHz x 2^j, but 3.675 at 1 378.125 samples/s, 44.1 and 88.2 kHz, which is refused, naming the
// field, with the numbers), and what channel_filter_taps, front_filter_taps, output_noise, make_template_tables and
// NewbornTables refuse. omega_bar_default_table: the path of a default Omega_bar table for the configurations' level
// range, or empty for the compiled one; a range other than the compiled table's (-15 and +85 dB S500) without a table
// for it is refused, naming both (omega_bar_default_table above).
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
    // shared: the parts built for this configuration on this channel's filter (make_viterbi_shared), shared by every
    // channel; the decoder could not reach the channelizer's taps otherwise. Builds now the tables that would otherwise
    // be built on first use (viterbi::build_evidence_tables, and the fist's), so that no block stalls on them. Throws
    // std::invalid_argument for a rate_hz that is not positive and finite, no shared parts, shared parts built for
    // another channel rate, front filter, edges, sample interval, level range or newborn grid, and what Search refuses.
    ViterbiDecoder(double rate_hz, const viterbi::ViterbiConfig& config, std::shared_ptr<const ViterbiShared> shared);

    // Takes the block's channel samples as received, through the front filter (EnvelopeSampler), and gives each
    // envelope sample to the search with its time t_k = 4 k / rate_hz and the block's N0, unless N0 is 0, when it is not
    // searched (exact zeros, or no hiss frame yet; the search refuses an N0 that is not positive). The front filter runs on
    // every channel sample whatever N0, so a sample not searched still counts in time: if N0 falls to 0 and returns
    // mid-stream the search sees the stream spliced, every story going on from where it stood, its edges aged and its
    // element's length counted over the samples searched only, its amplitude predicted across the time (the engine
    // holds N0 through frames of exact zeros, so after its first counted spectrum frame it does this only if a quarter
    // of a channel's neighborhood reads exact zeros for minutes; a direct caller can at any time). The first block's
    // t0_s fixes the absolute index of its first channel sample, round(t0_s x rate_hz), which must lie within 0.001 of a
    // sample and not below 0 (the engine's blocks start at first_index / rate_hz); every later block must follow the
    // previous one without a gap (to within half a sample), so the front filter's history is always the stream's own.
    // Returns nothing: the text comes at flush, and the decoder tracks no frequency (no freq_offset_hz). After flush()
    // the samples are ignored. The output does not depend on how the stream is split into blocks.
    DecodeUpdate process(std::span<const Sample> samples, double t0_s) override;
    // End of the channel: the search ends (Search::finish), then the best story's text (Search::best_text; each character's probability 1, speed and confidence 0: placeholders), all of
    // it at once. Further calls return nothing.
    DecodeUpdate flush() override;
    // Starts afresh: a new search and front filter, a new time origin at the next block. The N0 last set is kept (the
    // engine sets it before every block).
    void reset() override;
    // (set_frequency_anchor_hz: the Decoder interface's empty default. The decoder neither re-centers nor reads the
    // detector's frequency: the channelizer follows it, design Parts 1 and 10.)
    // N0 around the channel, FS^2/Hz, for the next block. Throws std::invalid_argument for a density that is negative or
    // not finite.
    void set_noise_density_fs2_per_hz(double n0_fs2_per_hz) override;

    // The search's instrumentation (kept only when config.stats is true).
    const viterbi::SearchStats& stats() const { return search_.stats(); }
    // For tests and the bench: the search, as of the samples given so far.
    const viterbi::Search& search() const { return search_; }

private:
    double rate_;
    viterbi::ViterbiConfig config_;
    std::shared_ptr<const ViterbiShared> shared_;
    viterbi::Search search_;
    viterbi::EnvelopeSampler sampler_;
    std::vector<viterbi::EnvelopeSample> envelope_;  // one block's envelope samples (storage kept)
    double n0_ = 0.0;                // FS^2/Hz: the hiss for the next block
    bool started_ = false;           // a block has arrived since the start or the last reset
    bool flushed_ = false;
    std::uint64_t next_index_ = 0;   // the absolute index of the next channel sample
};

}  // namespace kz4ap
