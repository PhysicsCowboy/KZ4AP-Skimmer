// The segmental decoder behind the engine's Decoder interface: the fast form of the viterbi decoder (viterbi_decoder.hpp).
// The same channel, front filter and envelope samples; the evidence is computed once per channel (segmental/evidence.hpp)
// and the search touches stories only at element ends (segmental/search.hpp). The best story's text goes out every 0.25 s as
// new characters or, where the story changed its mind, a correction, with the story's speed; at the channel's end everything is checked once more.
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
#include <string>
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

    std::optional<KeyingEvidence> keying_evidence() const override;
    std::span<const std::string> published_symbols() const override { return published_; }

    const segmental::SearchStats& stats() const { return search_->stats(); }

private:
    void start();
    // Brings the consumer's list (decoder.hpp) from published_ to the story: new characters, or a correction.
    DecodeUpdate bring_up_to_date(const segmental::BestStory& story, double now_s);

    // 1/s: the slow chain's switch rate. Heuristic: of the size of a slow sender's element rate.
    static constexpr double kFreeSlowSwitchPerS = 5.0;
    // How often the text goes out while the channel runs, s of stream. Heuristic: a display's refresh, against a text lag of
    // 0.387 s at most from the evidence; the cost per channel-second is 4 scans of the open elements and a tail.
    static constexpr double kPublishEveryS = 0.25;
    // The symbols compared each time, at most. Heuristic: over 25 s of text at 30 WPM; the story's changing its mind
    // further back goes out at the channel's end (flush compares everything).
    static constexpr std::size_t kPublishTailSymbols = 64;

    // The two-state chain of KeyingEvidence::free_key_nats, one step for every evidence sample the search has.
    void advance_free_key();
    struct FreeKey {
        double stay = 1.0, change = 0.0;  // per sample
        double up = 0.5, down = 0.5;
        double nats = 0.0;
        void step(double l1, double top);
    };
    FreeKey free_key_, free_slow_;
    std::int64_t free_next_ = 0;

    double next_publish_s_ = 0.0;
    float wpm_ = 0.0f;
    std::vector<std::string> published_;  // the consumer's list, as sent
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
