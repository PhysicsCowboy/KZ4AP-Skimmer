// The segmental decoder behind the engine's Decoder interface: the fast form of the viterbi decoder (viterbi_decoder.hpp).
// The same channel, front filter and envelope samples; the evidence is computed once per channel (segmental/evidence.hpp)
// and the search touches stories only at element ends (segmental/search.hpp). The best story's text goes out every 0.25 s as
// new characters or, where the story changed its mind, a correction, with the story's speed; at the channel's end everything is checked once more.
//
// A channel runs this at several filter scales (config.hpp, at_filter_scale), a stream each: a wide filter reads a fast sender
// and lets a neighbor in, a narrower one keeps the neighbor out and smears a fast sender's dots. The bank decoder's ladder rests on
// the same fact. Which stream's text goes out is decided by the streams' evidence for Morse against an on-off key with no timing,
// S - F, S the stream's best story's score and F the free key's (KeyingEvidence), both on that stream's own evidence: S alone would
// favor the widest filter, which earns on the neighbor's marks too, and so does F; the difference asks what the filter's output
// has of Morse's timing and text. A heuristic chosen by measurement (docs/decision-log.md, 2026-10-31).
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
struct SegmentalParts {
    SegmentalParts(const segmental::SegmentalConfig& config, const ChannelizerConfig& channel);

    segmental::SegmentalConfig config;
    double channel_rate_hz;
    int channel_samples_per_sample;
    std::vector<double> front_taps;
    viterbi::OutputNoise output_noise;
    viterbi::TemplateTables template_tables;
    segmental::LevelTables level_tables;
    segmental::ModelTables model_tables;
};

// The parts at the widest filter, and at the narrower ones in the order of config.filter_scales.
struct SegmentalShared : SegmentalParts {
    SegmentalShared(const segmental::SegmentalConfig& config, const ChannelizerConfig& channel);

    std::vector<SegmentalParts> narrower;
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

    const segmental::SearchStats& stats() const { return streams_.front().search->stats(); }
    // The filter scale whose text goes out, and how many streams still run.
    int leading_scale() const;
    std::size_t streams_running() const;

private:
    void start();
    // Brings the consumer's list (decoder.hpp) from published_ to the story: new characters, or a correction.
    DecodeUpdate bring_up_to_date(const segmental::BestStory& story, double now_s);

    // 1/s: the chain's switch rate. Heuristic: of the size of a slow sender's element rate.
    static constexpr double kFreeKeySwitchPerS = 5.0;
    // How often the text goes out while the channel runs, s of stream. Heuristic: a display's refresh, against a text lag of
    // 0.387 s at most from the evidence; the cost per channel-second is 4 scans of the open elements and a tail.
    static constexpr double kPublishEveryS = 0.25;
    // The symbols compared each time, at most. Heuristic: over 25 s of text at 30 WPM; the story's changing its mind
    // further back goes out at the channel's end (flush compares everything).
    static constexpr std::size_t kPublishTailSymbols = 64;

    // The two-state chain of KeyingEvidence::free_key_nats, one step for every evidence sample the search has.
    struct FreeKey {
        double stay = 1.0, change = 0.0;  // per sample
        double up = 0.5, down = 0.5;
        double nats = 0.0;
        void step(double l1, double top);
    };
    // One filter scale's filter, evidence and search.
    struct Stream {
        Stream(const SegmentalParts& parts, int scale);
        void advance_free_key();
        // nats: S - F, what the choice of stream rests on
        double morse_over_free() const { return search->best_score() - free_key.nats; }

        const SegmentalParts* parts;
        int scale;
        std::unique_ptr<segmental::ChannelFloor> floor;
        std::unique_ptr<segmental::RunningSums> sums;
        std::unique_ptr<segmental::LevelEvidence> evidence;
        std::unique_ptr<segmental::Search> search;
        viterbi::EnvelopeSampler sampler;
        std::vector<viterbi::EnvelopeSample> envelope;
        FreeKey free_key;
        std::int64_t free_next = 0;
        std::int64_t pushed = 0;
        std::int64_t expected_k = -1;
    };
    // Chooses the leader from what stands, and at or after the drop time drops what has fallen behind it. True if the leader changed.
    bool choose_leader(double now_s);

    std::vector<Stream> streams_;  // the widest first; never empty
    std::size_t leader_ = 0;

    double next_publish_s_ = 0.0;
    float wpm_ = 0.0f;
    std::vector<std::string> published_;  // the consumer's list, as sent
    double rate_;
    std::shared_ptr<const SegmentalShared> shared_;
    double n0_ = 0.0;
    bool started_ = false;
    bool flushed_ = false;
    std::uint64_t next_index_ = 0;
};

}  // namespace kz4ap
