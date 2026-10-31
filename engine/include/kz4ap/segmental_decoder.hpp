// The segmental decoder behind the engine's Decoder interface: the fast form of the viterbi decoder (viterbi_decoder.hpp).
// The same channel, front filter and envelope samples; the evidence is computed once per channel (segmental/evidence.hpp)
// and the search touches stories only at element ends (segmental/search.hpp). The text is published when the channel ends.
#pragma once

#include "kz4ap/channelizer.hpp"
#include "kz4ap/decoder.hpp"
#include "kz4ap/segmental/config.hpp"
#include "kz4ap/segmental/evidence.hpp"
#include "kz4ap/segmental/search.hpp"
#include "kz4ap/types.hpp"
#include "kz4ap/viterbi/front_filter.hpp"
#include "kz4ap/viterbi/mark_template.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace kz4ap {

// What every SegmentalDecoder of a configuration on one channel filter shares, built once from the channelizer's taps.
struct SegmentalShared {
    SegmentalShared(const segmental::SegmentalConfig& config, const ChannelizerConfig& channel);

    segmental::SegmentalConfig config;
    double channel_rate_hz;
    int channel_samples_per_sample;
    std::vector<double> front_taps;
    viterbi::OutputNoise output_noise;
    viterbi::TemplateTables template_tables;
    segmental::LevelTables level_tables;
    segmental::ModelTables model_tables;
};

class SegmentalDecoder final : public Decoder {
public:
    // rate_hz: the channel's sample rate, samples/s, which must be the one the shared parts were built for.
    SegmentalDecoder(double rate_hz, std::shared_ptr<const SegmentalShared> shared);

    DecodeUpdate process(std::span<const Sample> samples, double t0_s) override;
    DecodeUpdate flush() override;
    void reset() override;
    void set_noise_density_fs2_per_hz(double n0_fs2_per_hz) override;

    const segmental::SearchStats& stats() const { return search_->stats(); }

private:
    void start();

    double rate_;
    std::shared_ptr<const SegmentalShared> shared_;
    std::unique_ptr<segmental::ChannelFloor> floor_;
    std::unique_ptr<segmental::RunningSums> sums_;
    std::unique_ptr<segmental::LevelEvidence> evidence_;
    std::unique_ptr<segmental::Search> search_;
    viterbi::EnvelopeSampler sampler_;
    std::vector<viterbi::EnvelopeSample> envelope_;
    double n0_ = 0.0;
    bool started_ = false;
    bool flushed_ = false;
    std::uint64_t next_index_ = 0;
    std::int64_t pushed_ = 0;
    std::int64_t expected_k_ = -1;
};

}  // namespace kz4ap
