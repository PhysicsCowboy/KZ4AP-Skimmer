// The segmental decoder behind the engine's Decoder interface: the fast form of the viterbi decoder (viterbi_decoder.hpp).
// The same channel, front filter and envelope samples; the evidence is computed once per channel (segmental/evidence.hpp)
// and the search touches stories only at element ends (segmental/search.hpp). The best story's text goes out every 0.25 s as
// new characters or, where the story changed its mind, a correction, with the speed of the text sent (text_wpm); at the channel's end
// everything is checked once more.
//
// A channel runs this at several filter scales (config.hpp, at_filter_scale), a stream each: a wide filter reads a fast sender
// and lets a neighbor in, a narrower one keeps the neighbor out and smears a fast sender's dots. The bank decoder's ladder rests on
// the same fact. Which stream's text goes out is decided by the streams' evidence for Morse against an on-off key with no timing,
// S - F, S the stream's best story's score and F the free key's (KeyingEvidence), both on that stream's own evidence: S alone would
// favor the widest filter, which earns on the neighbor's marks too, and so does F; the difference asks what the filter's output
// has of Morse's timing and text. A heuristic chosen by measurement (docs/decision-log.md, 2026-10-31). One stream's whole text is
// what goes out; a stream is not a state of the stories, and no story crosses from one to another.
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

// The channel the template and noise tables are computed on: the same filter at 48 kHz where the rate is a whole multiple of 48 kHz
// and fft_size allows. The channel filter's length in s, its cutoff and the channel's rate do not depend on the input rate (the
// engine's fft_size goes with it), so the tables differ only in the positions averaged over; computing them at the full rate takes
// time in proportion to the rate squared and the filter scale. Agreement: SegmentalShared.TablesFromTheStandInAgreeWithTheFullRate.
ChannelizerConfig segmental_tables_channel(ChannelizerConfig channel);

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
    // A neighbor's leak: |H_channel(k df)|^2 |H_front(k df)|^2, 1, at k = 0, 1, ... bins from the channel's center (df the band's bin),
    // read from leak_first_bin on; and the blocks its peak is held over. Empty with SegmentalConfig::leak_hold_s 0.
    std::vector<double> leak_response;
    std::size_t leak_first_bin = 0;
    std::size_t leak_hold_blocks = 0;
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
    void set_band_spectrum(std::span<const float> power_fs2, int center_index) override;

    std::optional<KeyingEvidence> keying_evidence() const override;
    std::span<const std::string> published_symbols() const override { return published_; }
    double block_envelope_fs2() const override { return block_envelope_fs2_; }
    double block_noise_fs2() const override { return block_noise_fs2_; }

    const segmental::SearchStats& stats() const { return streams_.front().search->stats(); }
    // The filter scale whose text goes out, and how many streams still run.
    int leading_scale() const;
    std::size_t streams_running() const;
    // FS^2: the noise the leak asks of the stream at this filter scale, 0 if it has none (or no such stream runs).
    double leak_floor_fs2(int scale) const;

private:
    void start();
    // Brings the consumer's list (decoder.hpp) from published_ to the story: new characters, or a correction.
    DecodeUpdate bring_up_to_date(const segmental::BestStory& story, double now_s);
    // A symbol's margin in steps, for telling a fallen margin from rounding (bring_up_to_date).
    static std::uint8_t confidence_level(float probability);

    // 1/s: the chain's switch rate. Heuristic: of the size of a slow sender's element rate.
    static constexpr double kFreeKeySwitchPerS = 5.0;
    // How often the text goes out while the channel runs, s of stream. Heuristic: a display's refresh, against a text lag of
    // 0.387 s at most from the evidence; the cost per channel-second is 4 scans of the open elements and a tail.
    static constexpr double kPublishEveryS = 0.25;
    // The symbols compared each time, at most. Heuristic: over 25 s of text at 30 WPM; the story's changing its mind
    // further back goes out at the channel's end (flush compares everything).
    static constexpr std::size_t kPublishTailSymbols = 64;
    // The speed that goes out with the text, WPM, is the median of the last this many published letters' own speeds (letter_wpm).
    // Heuristic: 16 letters of PARIS-like text are about 160 units, 4.8 s at 40 WPM and 12.8 s at 15 (derived); being a median,
    // it follows a change of speed after about 8 new letters (2.4 s at 40 WPM); a few junk letters (a burst in the silence read
    // as E) leave it. A junk row's speed can fall below the speed grid's slowest (5.2 WPM): its letters' own durations set it.
    static constexpr std::size_t kSpeedLetters = 16;
    // A letter's own speed, WPM: 1.2 s x its units / its duration (PARIS), its units a dot's 1, a dash's 3 and 1 between its elements,
    // its duration from its first mark's start to its last mark's end (DecodedSymbol::start_s, end_s): its marks and element gaps, so
    // a character speed, as the story's fist (a Farnsworth sender's spacing is left out). 0 for a word space, a symbol with no code
    // and a duration not positive.
    static float letter_wpm(const DecodedSymbol& symbol);
    // The median of the last kSpeedLetters nonzero speeds of published_wpm_ (the mean of the middle two for an even count); 0 if none.
    float text_wpm() const;

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
        // nats: S - F since the choice's start: what the choice of stream rests on
        double morse_over_free() const { return search->best_score() - free_key.nats - start_morse_over_free; }
        // Symbols of its best story since then
        double symbols() const { return static_cast<double>(search->best_story(1).length) - start_symbols; }

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
        double start_morse_over_free = 0.0;  // nats
        double start_symbols = 0.0;
        std::vector<double> leak_held;  // FS^2: the leak of the last SegmentalParts::leak_hold_blocks blocks, a ring
        std::size_t leak_next = 0;
        double leak_floor = 0.0;        // FS^2: the noise that asks for, as sigma^2 at this stream's output
    };
    // Chooses the leader from what stands, and at or after the drop time of the channel's age, if `may_drop`, drops what has fallen
    // behind it. True if the leader changed. stream_now_s: s of stream; a channel's age is that less the time of its first sample.
    bool choose_leader(double stream_now_s, bool may_drop);

    std::vector<Stream> streams_;  // the widest first; never empty
    std::size_t leader_ = 0;
    bool choice_started_ = false;

    double next_publish_s_ = 0.0;
    std::vector<std::string> published_;  // the consumer's list, as sent
    std::vector<std::uint8_t> published_levels_;  // and its symbols' margins, as sent (confidence_level)
    std::vector<float> published_wpm_;  // and its symbols' own speeds, WPM, as sent (letter_wpm)
    double rate_;
    std::shared_ptr<const SegmentalShared> shared_;
    double n0_ = 0.0;
    bool started_ = false;
    bool flushed_ = false;
    std::uint64_t next_index_ = 0;
    double first_s_ = 0.0;  // s of stream: the channel's first sample, a replay's included
    double block_envelope_fs2_ = -1.0;  // FS^2: the last block's mean e^2 at the leader's filter (block_envelope_fs2)
    double block_noise_fs2_ = -1.0;     // FS^2: the hiss at the leader's filter output (block_noise_fs2)
};

}  // namespace kz4ap
