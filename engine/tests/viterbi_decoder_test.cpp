// The new decoder behind the engine (ViterbiDecoder; docs/signal-processing.md, section 8d): through the engine on
// oracle channels, and directly on channel streams from the engine's own channelizer at 48 kHz, DEV2's rate. The test
// noise is made from the Mersenne twister's bits alone, so that every standard library draws the same noise.
#include "kz4ap/viterbi_decoder.hpp"

#include "kz4ap/channelizer.hpp"
#include "kz4ap/classical_decoder.hpp"
#include "kz4ap/decoder.hpp"
#include "kz4ap/engine.hpp"
#include "kz4ap/event_bus.hpp"
#include "kz4ap/types.hpp"
#include "kz4ap/viterbi/config.hpp"
#include "kz4ap/viterbi/observation.hpp"
#include "kz4ap/viterbi/search.hpp"
#include "test_signals.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <future>
#include <limits>
#include <map>
#include <memory>
#include <numbers>
#include <optional>
#include <random>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace {

using namespace kz4ap;
using viterbi::ViterbiConfig;
namespace signals = kz4ap::test;

constexpr double kInf = std::numeric_limits<double>::infinity();
constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

constexpr int kInputRate = 48000;                     // samples/s: DEV2's recordings
constexpr double kSigma = 0.02;                       // FS per input sample: DEV2's noise
constexpr double kN0 = kSigma * kSigma / kInputRate;  // FS^2/Hz: N0 = sigma^2 / fs
constexpr double kChannelRate = 1500.0;               // channel samples/s
constexpr int kSamplesPerPiece = 3;                   // a piece of 2 ms

// The engine's channelizer at 48 kHz: an FFT of 2 048 points (bins of 23.4375 Hz), 1 500 channel samples/s.
ChannelizerConfig channelizer_config() {
    ChannelizerConfig config;
    config.sample_rate = kInputRate;
    config.fft_size = 2048;
    return config;
}

// FS: the amplitude of a key-down carrier at s500_db dB S500 over DEV2's noise, S500 = 10 log10(a^2 / (N0 x 500 Hz)).
double amplitude_at(double s500_db) { return std::sqrt(std::pow(10.0, s500_db / 10.0) * kN0 * 500.0); }

// Complex white noise of total power sigma^2 (FS^2) per sample from the Mersenne twister's bits alone (hiss_test.cpp's
// form): the power |z|^2 = -sigma^2 ln u1 is exponential with mean sigma^2 and the phase 2 pi u2 uniform.
class WhiteNoise {
public:
    WhiteNoise(unsigned seed, double sigma) : rng_(seed), sigma_(sigma) {}
    Sample next() {
        constexpr double kScale = 1.0 / 4294967296.0;  // 2^-32
        const double u1 = (static_cast<double>(rng_()) + 1.0) * kScale;  // in (0, 1]
        const double u2 = static_cast<double>(rng_()) * kScale;          // in [0, 1)
        const double radius = sigma_ * std::sqrt(-std::log(u1));
        const double phase = 2.0 * std::numbers::pi * u2;
        return {static_cast<float>(radius * std::cos(phase)), static_cast<float>(radius * std::sin(phase))};
    }

private:
    std::mt19937 rng_;
    double sigma_;
};

// x with DEV2's noise added to samples [from, end).
void add_noise(std::vector<Sample>& x, unsigned seed, std::size_t from = 0) {
    WhiteNoise noise(seed, kSigma);
    for (std::size_t i = from; i < x.size(); ++i) x[i] += noise.next();
}

// A station keyed by `keying` (s) at freq_hz (Hz from the span's center, plus drift_hz_per_s x t) and s500_db dB S500,
// with the test signals' 5 ms raised-cosine edges, added to x (48 kHz).
void add_station(std::vector<Sample>& x, const std::vector<std::pair<double, double>>& keying, double freq_hz,
                 double s500_db, double drift_hz_per_s = 0.0) {
    const double duration_s = static_cast<double>(x.size()) / kInputRate;
    const std::vector<Sample> s = signals::keyed_signal_of(keying, kInputRate, duration_s, freq_hz,
                                                           amplitude_at(s500_db), 0.0, 1, drift_hz_per_s);
    for (std::size_t i = 0; i < x.size() && i < s.size(); ++i) x[i] += s[i];
}

std::vector<Sample> silence(double duration_s) {
    return std::vector<Sample>(static_cast<std::size_t>(duration_s * kInputRate));
}

// The channel stream the engine's channelizer gives a 48 kHz input on the channel centered on the bin nearest
// center_hz, and that center, Hz.
struct Stream {
    std::vector<Sample> samples;
    double center_hz = 0.0;
};
Stream channel_stream(const std::vector<Sample>& input, double center_hz) {
    Channelizer channelizer(channelizer_config());
    const int bin = channelizer.hz_to_bin(center_hz);
    channelizer.add_channel(1, bin);
    Stream stream;
    stream.center_hz = channelizer.bin_to_hz(bin);
    channelizer.push(input, [&](std::uint32_t, std::uint64_t, std::span<const Sample> s) {
        stream.samples.insert(stream.samples.end(), s.begin(), s.end());
    });
    return stream;
}

// The shared parts of the default configuration on the channelizer at 48 kHz, built once.
const std::shared_ptr<const ViterbiShared>& default_shared() {
    static const std::shared_ptr<const ViterbiShared> shared =
        make_viterbi_shared(ViterbiConfig{}, channelizer_config());
    return shared;
}

std::string text_of(const std::vector<DecodedSymbol>& symbols) {
    std::string text;
    for (const DecodedSymbol& symbol : symbols) text += symbol.text;
    return text;
}

// The character error rate of texts without prosigns: the Levenshtein distance from the reference over its length.
double cer(const std::string& reference, const std::string& decoded) {
    std::vector<std::size_t> row(decoded.size() + 1);
    for (std::size_t j = 0; j < row.size(); ++j) row[j] = j;
    for (std::size_t i = 1; i <= reference.size(); ++i) {
        std::size_t diagonal = row[0];
        row[0] = i;
        for (std::size_t j = 1; j <= decoded.size(); ++j) {
            const std::size_t above = row[j];
            row[j] = std::min({row[j] + 1, row[j - 1] + 1, diagonal + (reference[i - 1] != decoded[j - 1] ? 1u : 0u)});
            diagonal = above;
        }
    }
    return static_cast<double>(row[decoded.size()]) / static_cast<double>(reference.size());
}

// What a decoder left after a stream: its text, statistics, live stories and normalization, and whether any update
// reported a frequency (the decoder tracks none: never).
struct Decoded {
    std::vector<DecodedSymbol> text;
    viterbi::SearchStats stats;
    std::vector<viterbi::StoryView> stories;
    double normalization_nats = 0.0;
    bool reported_a_frequency = false;
};

// Decodes a channel stream with a new decoder in blocks of `block` samples, the first sample at t0_s, with the density
// n0 (FS^2/Hz) given before every block, as the engine gives it, and the anchor anchor_hz before every block (the
// Decoder interface's; the new decoder ignores it).
Decoded decode(const ViterbiConfig& config, const std::shared_ptr<const ViterbiShared>& shared,
               const std::vector<Sample>& y, std::size_t block, double n0 = kN0, double t0_s = 0.0,
               double anchor_hz = 0.0) {
    ViterbiDecoder decoder(kChannelRate, config, shared);
    Decoded run;
    for (std::size_t first = 0; first < y.size(); first += block) {
        decoder.set_frequency_anchor_hz(anchor_hz);
        decoder.set_noise_density_fs2_per_hz(n0);
        const DecodeUpdate update =
            decoder.process(std::span<const Sample>(y).subspan(first, std::min(block, y.size() - first)),
                            t0_s + static_cast<double>(first) / kChannelRate);
        EXPECT_TRUE(update.chars.empty());
        run.reported_a_frequency = run.reported_a_frequency || update.freq_offset_hz.has_value();
    }
    const DecodeUpdate last = decoder.flush();
    run.reported_a_frequency = run.reported_a_frequency || last.freq_offset_hz.has_value();
    run.text = last.chars;
    run.stats = decoder.stats();
    run.stories = decoder.search().live_stories();
    run.normalization_nats = decoder.search().normalization_nats();
    return run;
}

// A run of the engine: each track's text, as published (the new decoder publishes it at the end), and its frequency
// in the latest event, Hz from the span's center.
struct EngineRun {
    std::map<std::uint32_t, std::string> text;
    std::map<std::uint32_t, double> freq_hz;
};
EngineRun run_engine(const EngineConfig& config, const std::vector<Sample>& x, std::size_t chunk = 65536) {
    EventBus bus;
    EngineRun run;
    bus.subscribe([&](const Event& e) {
        if (const auto* d = std::get_if<DecodedTextEvent>(&e)) {
            for (const DecodedSymbol& c : d->chars) run.text[d->track_id] += c.text;
            run.freq_hz[d->track_id] = d->freq_hz;
        }
    });
    Engine engine(config, bus);
    for (std::size_t i = 0; i < x.size(); i += chunk)
        engine.process(std::span<const Sample>(x).subspan(i, std::min(chunk, x.size() - i)));
    engine.finish();
    return run;
}

EngineConfig viterbi_engine() {
    EngineConfig config;
    config.sample_rate = kInputRate;
    config.decoder.front_end = FrontEnd::Viterbi;
    return config;
}

// Feeds x to the engine in chunks and finishes it, so that the test can read what the engine keeps afterwards.
void run_to_the_end(Engine& engine, const std::vector<Sample>& x, std::size_t chunk = 65536) {
    for (std::size_t i = 0; i < x.size(); i += chunk)
        engine.process(std::span<const Sample>(x).subspan(i, std::min(chunk, x.size() - i)));
    engine.finish();
}

// Leaves a measured number in the test's record (the XML report's properties).
void record(const std::string& key, double value) {
    std::ostringstream text;
    text.precision(6);
    text << value;
    ::testing::Test::RecordProperty(key, text.str());
}

// Whether the decoder's full-size tests run: KZ4AP_VITERBI_FULL_SWEEP=1, which the full-sweep presets set through the
// tests ViterbiDecoder.FullSweep and ViterbiDecoder.StrongFullSweep (engine/CMakeLists.txt).
bool full_sweep() {
    const char* value = std::getenv("KZ4AP_VITERBI_FULL_SWEEP");
    return value != nullptr && std::string(value) == "1";
}

}  // namespace

TEST(ViterbiDecoder, DecodesThroughTheEngine) {
    // Four stations at +15 dB S500, 25 WPM, on DEV2's noise at 48 kHz, each on its own oracle channel, the label its
    // true frequency, 0, +7.3, -9.8 and +11.0 Hz from the centers of bins -384, -128, 128 and 384 (+-9 000 and
    // +-3 000 Hz): each text exact. The decoder tracks no frequency: the channel follows the label's frequency, so
    // each station reaches the decoder at 0 Hz.
    struct Station {
        std::string text;
        double freq_hz;  // Hz from the span's center
        double start_s;
    };
    const std::vector<Station> stations = {{"CQ CQ DE K1ABC", -9000.0, 0.6},
                                           {"TEST W9XYZ", -3000.0 + 7.3, 0.9},
                                           {"PARIS 73", 3000.0 - 9.8, 0.7},
                                           {"QRL K", 9000.0 + 11.0, 1.1}};
    double duration_s = 0.0;
    for (const Station& s : stations)
        duration_s = std::max(duration_s, signals::keying(s.text, 25.0, s.start_s).back().second + 1.0);
    std::vector<Sample> x = silence(duration_s);
    EngineConfig config = viterbi_engine();
    for (const Station& s : stations) {
        add_station(x, signals::keying(s.text, 25.0, s.start_s), s.freq_hz, 15.0);
        config.oracle_frequencies_hz.push_back(s.freq_hz);
    }
    add_noise(x, 31);
    const EngineRun run = run_engine(config, x);
    for (std::size_t i = 0; i < stations.size(); ++i) {
        const auto id = static_cast<std::uint32_t>(i + 1);
        EXPECT_EQ(run.text.count(id) ? run.text.at(id) : std::string("(nothing)"), stations[i].text)
            << "track " << id << " at " << stations[i].freq_hz << " Hz";
    }
}

TEST(ViterbiDecoder, InputSplitDoesNotMatter) {
    // One channel's stream, a station at +12 dB S500, 25 WPM, on the channel's center (where the channel following it
    // puts it), given to the decoder in blocks of 1, 7, 32 and 1 000 samples: the same text with the same times, the
    // same statistics and normalization and the same live stories (state, score, line and level) as the run in blocks
    // of 1 sample, to the bit (plan §5, item 3). A piece straddles blocks, and marks straddle blocks.
    ViterbiConfig config;
    config.stats = true;
    const std::string text = "PARIS PARIS PARIS";
    const auto keying = signals::keying(text, 25.0, 0.8);
    std::vector<Sample> x = silence(keying.back().second + 1.0);
    add_station(x, keying, 3000.0, 12.0);
    add_noise(x, 32);
    const Stream stream = channel_stream(x, 3000.0);
    ASSERT_EQ(stream.center_hz, 3000.0);
    default_shared();  // built before the threads start
    const std::vector<std::size_t> blocks = {1, 7, 32, 1000};
    std::vector<std::future<Decoded>> runs;
    for (const std::size_t block : blocks)
        runs.push_back(std::async(std::launch::async, [&, block] {
            return decode(config, default_shared(), stream.samples, block);
        }));
    std::vector<Decoded> results;
    for (auto& run : runs) results.push_back(run.get());
    const Decoded& one = results.front();
    EXPECT_EQ(text_of(one.text), text);
    for (std::size_t r = 1; r < results.size(); ++r) {
        SCOPED_TRACE("blocks of " + std::to_string(blocks[r]) + " samples");
        const Decoded& other = results[r];
        ASSERT_EQ(other.text.size(), one.text.size());
        for (std::size_t i = 0; i < one.text.size(); ++i) {
            EXPECT_EQ(other.text[i].text, one.text[i].text);
            EXPECT_EQ(other.text[i].start_s, one.text[i].start_s);
            EXPECT_EQ(other.text[i].end_s, one.text[i].end_s);
        }
        EXPECT_TRUE(other.stats == one.stats);
        EXPECT_EQ(other.normalization_nats, one.normalization_nats);
        ASSERT_EQ(other.stories.size(), one.stories.size());
        for (std::size_t i = 0; i < one.stories.size(); ++i) {
            const viterbi::StoryView& a = other.stories[i];
            const viterbi::StoryView& b = one.stories[i];
            EXPECT_EQ(a.activity, b.activity);
            EXPECT_EQ(a.key_down, b.key_down);
            EXPECT_EQ(a.tau, b.tau);
            EXPECT_EQ(a.speed, b.speed);
            EXPECT_EQ(a.node, b.node);
            EXPECT_EQ(a.score_nats, b.score_nats);
            EXPECT_EQ(a.line, b.line);
            EXPECT_EQ(a.level.learned, b.level.learned);
            EXPECT_EQ(a.level.mu, b.level.mu);
            EXPECT_EQ(a.level.p, b.level.p);
            EXPECT_EQ(a.level.t_s, b.level.t_s);
        }
    }
}

TEST(ViterbiDecoder, ReadsItsChannelAsReceived) {
    // The decoder has no frequency tracker and no oscillator (design Part 10): it neither re-centers its channel nor
    // reads the detector's frequency. A station 4 Hz from the channel's center, decoded with the anchor 0 Hz and with
    // the anchor 4 Hz (the Decoder interface's set_frequency_anchor_hz, called before every block): the same stories,
    // scores and normalization, to the bit, and no update reports a frequency.
    const auto keying = signals::keying("PARIS", 25.0, 0.5);
    std::vector<Sample> x = silence(keying.back().second + 1.0);
    add_station(x, keying, 750.0 + 4.0, 15.0);
    add_noise(x, 44);
    const Stream stream = channel_stream(x, 750.0);
    const Decoded plain = decode(ViterbiConfig{}, default_shared(), stream.samples, 32, kN0, 0.0, 0.0);
    const Decoded anchored = decode(ViterbiConfig{}, default_shared(), stream.samples, 32, kN0, 0.0, 4.0);
    EXPECT_FALSE(plain.reported_a_frequency);
    EXPECT_FALSE(anchored.reported_a_frequency);
    EXPECT_EQ(plain.normalization_nats, anchored.normalization_nats);
    ASSERT_EQ(plain.stories.size(), anchored.stories.size());
    for (std::size_t i = 0; i < plain.stories.size(); ++i) {
        EXPECT_EQ(plain.stories[i].score_nats, anchored.stories[i].score_nats) << i;
        EXPECT_EQ(plain.stories[i].line, anchored.stories[i].line) << i;
    }
}

TEST(ViterbiDecoder, ExactZerosThenAStation) {
    // 2 s of exact zeros at the input, noise included (the spectrum reads -200 dB re 1 FS^2 in every bin, so the hiss
    // estimate is 0 and the decoder decodes nothing), then DEV2's noise and, from 3 s, a station at +15 dB S500,
    // 25 WPM, through the engine on an oracle channel: no failure, and its text decoded.
    const std::string text = "CQ TEST";
    const auto keying = signals::keying(text, 25.0, 3.0);
    std::vector<Sample> x = silence(keying.back().second + 1.0);
    add_station(x, keying, 1203.0, 15.0);
    add_noise(x, 33, static_cast<std::size_t>(2.0 * kInputRate));
    for (std::size_t i = 0; i < static_cast<std::size_t>(2.0 * kInputRate); ++i) ASSERT_EQ(x[i], Sample{});
    EngineConfig config = viterbi_engine();
    config.oracle_frequencies_hz = {1203.0};
    EngineRun run;
    ASSERT_NO_THROW(run = run_engine(config, x));
    EXPECT_EQ(run.text.count(1) ? run.text.at(1) : std::string("(nothing)"), text);
}

TEST(ViterbiDecoder, ANoiseDensityOfZeroMidStreamSplicesTheStream) {
    // N0 = 0 means no estimate: the decoder does not search those pieces but keeps counting time, so when N0 returns
    // every story goes on from where it stood, the length of its element frozen across the skipped pieces and its level
    // predicted across the time: the search sees the stream spliced (A.8d). The engine holds N0 through frames of exact
    // zeros, so after its first counted spectrum frame it gives N0 = 0 only if a quarter of a channel's neighborhood
    // reads exact zeros for minutes; a direct caller can give it at any time. Two transmissions of "PARIS"
    // (+15 dB S500, 25 WPM) 3 s apart: with N0 = 0 for 1 s inside the silence between them, no failure and the text
    // exact; with N0 = 0 for 1 s inside the second, no failure, every score finite, and the first word still decoded.
    auto keying = signals::keying("PARIS", 25.0, 0.5);
    const double first_end_s = keying.back().second;
    const double second_start_s = first_end_s + 3.0;
    const auto second = signals::keying("PARIS", 25.0, second_start_s);
    keying.insert(keying.end(), second.begin(), second.end());
    std::vector<Sample> x = silence(keying.back().second + 1.0);
    add_station(x, keying, 750.0, 15.0);
    add_noise(x, 42);
    const Stream stream = channel_stream(x, 750.0);
    struct Case {
        double zero_from_s;
        double zero_to_s;
        bool inside_a_transmission;
    };
    for (const Case& c : {Case{first_end_s + 1.0, first_end_s + 2.0, false},
                          Case{second_start_s + 0.3, second_start_s + 1.3, true}}) {
        SCOPED_TRACE("N0 = 0 from " + std::to_string(c.zero_from_s) + " s to " + std::to_string(c.zero_to_s) + " s");
        ViterbiDecoder decoder(kChannelRate, ViterbiConfig{}, default_shared());
        for (std::size_t first = 0; first < stream.samples.size(); first += 32) {
            const double t = static_cast<double>(first) / kChannelRate;
            decoder.set_noise_density_fs2_per_hz(t >= c.zero_from_s && t < c.zero_to_s ? 0.0 : kN0);
            ASSERT_NO_THROW(decoder.process(std::span<const Sample>(stream.samples).subspan(first, 32), t));
        }
        for (const viterbi::StoryView& story : decoder.search().live_stories())
            ASSERT_TRUE(std::isfinite(story.score_nats));
        const std::string decoded = text_of(decoder.flush().chars);
        if (c.inside_a_transmission)
            EXPECT_EQ(decoded.substr(0, 6), "PARIS ") << "decoded \"" << decoded << "\"";
        else
            EXPECT_EQ(decoded, "PARIS PARIS");
    }
}

TEST(ViterbiDecoder, ExtremeLevels) {
    // A station at +60 dB S500 (a piece's z = |S| / sqrt(V) near 1 000 without the systematic term, a 390 ms block's
    // near 14 000, far above the flat prior's table: its large-z form) and one at -10 dB S500, each alone on its
    // channel's center with DEV2's noise, N0 the noise's own: after every block every live story's score is finite and
    // the best is 0, and the normalization is finite (plan §5, item 2). The plan's other criterion, the +60 dB S500
    // station's exact text, holds with the systematic term (search.hpp, "The systematic term"; the owner's decision
    // D15 of 2026-10-21); a station at +30 dB S500 decodes exactly too. Without the term the fall's tail, which no
    // story that keeps the mark explains, was claimed by new one-piece overs from about +38 dB S500, and +60 dB S500
    // decoded "T EE EE ET E T EE E T EE ET E T ET E T ET ET E" for each "PARIS" (search.hpp, "The mark's rise and what
    // remains", item 4). ViterbiDecoder.StrongStationsAtEveryEdgePosition, a full-sweep test, holds +30 to +60 dB S500
    // at 16 positions of the edge within a piece.
    const std::string text = "PARIS PARIS";
    const auto keying = signals::keying(text, 25.0, 0.5);
    for (const double s500_db : {60.0, 30.0, -10.0}) {
        SCOPED_TRACE(std::to_string(s500_db) + " dB S500");
        std::vector<Sample> x = silence(keying.back().second + 1.0);
        add_station(x, keying, 750.0, s500_db);
        add_noise(x, 34);
        const Stream stream = channel_stream(x, 750.0);
        ViterbiDecoder decoder(kChannelRate, ViterbiConfig{}, default_shared());
        for (std::size_t first = 0; first < stream.samples.size(); first += 32) {
            decoder.set_noise_density_fs2_per_hz(kN0);
            decoder.process(std::span<const Sample>(stream.samples).subspan(first, 32),
                            static_cast<double>(first) / kChannelRate);
            double best = -kInf;
            for (const viterbi::StoryView& story : decoder.search().live_stories()) {
                ASSERT_TRUE(std::isfinite(story.score_nats)) << "sample " << first;
                best = std::max(best, story.score_nats);
            }
            ASSERT_EQ(best, 0.0) << "sample " << first;
            ASSERT_TRUE(std::isfinite(decoder.search().normalization_nats())) << "sample " << first;
        }
        const std::string decoded = text_of(decoder.flush().chars);
        if (s500_db != -10.0) EXPECT_EQ(decoded, text);
    }
}

TEST(ViterbiDecoder, StrongStationsAtEveryEdgePosition) {
    // The full sweep, labeled full-sweep (the presets windows-full-sweep and linux-full-sweep run it, through the test
    // ViterbiDecoder.StrongFullSweep of engine/CMakeLists.txt, which sets KZ4AP_VITERBI_FULL_SWEEP=1; skipped
    // otherwise): "PARIS PARIS" at 25 WPM, alone on its channel's center with DEV2's noise (seed 34), N0 the noise's
    // own, the decoder at its defaults, at +30, +36, +40, +45, +50, +55 and +60 dB S500, the key-down starting 0.5 s
    // plus q input samples, q = 0, 6, ..., 90: 16 positions of the edge within a piece of 96 input samples. Each mark
    // and gap is a whole number of pieces, so every edge of a run sits at its position. The exact text at every point
    // (the systematic term, search.hpp; without it 16, 16, 13, 10, 2, 0 and 0 positions decoded exactly at those
    // levels, measured).
    if (!full_sweep()) GTEST_SKIP() << "the full sweep runs under the full-sweep presets (KZ4AP_VITERBI_FULL_SWEEP=1)";
    const std::string text = "PARIS PARIS";
    for (const double s500_db : {30.0, 36.0, 40.0, 45.0, 50.0, 55.0, 60.0}) {
        for (int q = 0; q < 96; q += 6) {
            SCOPED_TRACE(std::to_string(s500_db) + " dB S500, the edge " + std::to_string(q) + " input samples in");
            // whole input samples (the margin keeps the edge's sample against rounding)
            const double start_s = (24000.0 + q + 1e-6) / kInputRate;
            const auto keying = signals::keying(text, 25.0, start_s);
            std::vector<Sample> x = silence(keying.back().second + 1.0);
            add_station(x, keying, 750.0, s500_db);
            add_noise(x, 34);
            const Decoded run = decode(ViterbiConfig{}, default_shared(), channel_stream(x, 750.0).samples, 32);
            EXPECT_EQ(text_of(run.text), text);
        }
    }
}

namespace {

// The text the decoder gives for `text` keyed at wpm with Farnsworth factor `factor` (1 for standard spacing) from
// 0.6 s, at +20 dB S500 on its channel's center with DEV2's noise (seed 35), N0 the noise's own.
std::string grid_end_text(const std::string& text, double wpm, double factor) {
    const auto keying = signals::keying_segments({{text, wpm, factor}}, 0.6);
    std::vector<Sample> x = silence(keying.back().second + 1.5);
    add_station(x, keying, 750.0, 20.0);
    add_noise(x, 35);
    return text_of(decode(ViterbiConfig{}, default_shared(), channel_stream(x, 750.0).samples, 32).text);
}

}  // namespace

TEST(ViterbiDecoder, TheGridsEnds) {
    // The ends of the speed grid at +20 dB S500, each on its channel's center with DEV2's noise, N0 the noise's own,
    // at standard spacing: characters at 5.2 WPM (the slowest speed, dots of 230.3 ms, dashes of 691 ms: two 390 ms
    // blocks; letter gaps of 691 ms and a word gap of 1.61 s), and 100 WPM (the fastest, dots of 6 pieces): a CER of
    // at most 0.05 (plan §5, item 2; review focus 4). Farnsworth spacing at 5.2 WPM is the next test's.
    struct Case {
        std::string name;
        std::string text;
        double wpm;
    };
    const std::vector<Case> cases = {{"5.2 WPM", "CQ CQ", 5.2}, {"100 WPM", "PARIS PARIS", 100.0}};
    default_shared();
    std::vector<std::future<std::string>> decoded;
    for (const Case& c : cases)
        decoded.push_back(std::async(std::launch::async, [&c] { return grid_end_text(c.text, c.wpm, 1.0); }));
    for (std::size_t i = 0; i < cases.size(); ++i) {
        const std::string text = decoded[i].get();
        EXPECT_LE(cer(cases[i].text, text), 0.05) << cases[i].name << ": decoded \"" << text << "\"";
    }
}

TEST(ViterbiDecoder, FarnsworthSpacingAtTheSlowestSpeed) {
    // Characters at 5.2 WPM with Farnsworth spacing F = 8 (letter gaps of 5.5 s and a word gap of 12.9 s) at
    // +20 dB S500, as in the test before: the plan's criterion is a CER of at most 0.05 (plan §5, item 2).
    //
    // A strict expected failure since the owner's decision D14 of 2026-10-21: the decoder has no Farnsworth factor
    // (standard spacing only), so Farnsworth-spaced signals decode poorly until a later fix (docs/backlog.md,
    // "Farnsworth spacing in the new decoder"). The test fails if the criterion is met, so that the marker is removed
    // once the fix comes, and is skipped otherwise, with the decoded text.
    const std::string text = grid_end_text("CQ CQ", 5.2, 8.0);
    const double error_rate = cer("CQ CQ", text);
    record("cer", error_rate);
    std::ostringstream outcome;
    outcome.precision(3);
    outcome << "F = 8 at 5.2 WPM decodes \"" << text << "\", a CER of " << error_rate;
    if (error_rate <= 0.05) FAIL() << outcome.str() << ": the limitation is gone, remove the expected failure";
    GTEST_SKIP() << "strict expected failure: " << outcome.str()
                 << " (no Farnsworth factor: docs/backlog.md, \"Farnsworth spacing in the new decoder\")";
}

TEST(ViterbiDecoder, OneWordOversDecodeExactly) {
    // An over of one short word at 25 WPM, +20 dB S500, alone on its channel's center with DEV2's noise (seeds 39, 40
    // and 41), N0 the noise's own: "TEST" (a dash, four dots and a dash) and "TU" (a dash, then two dots and a dash),
    // each exact. Their marks are also those of one longer symbol, "-" (the code -....-) and "X" (-..-), read at a
    // slower speed with the letter gaps taken for element gaps. With the Farnsworth factor, whose prior of ln 23 =
    // 3.135 nats was paid at a story's first letter or word gap and never by the one-symbol reading, "TEST" alone read
    // as "-" in 12 of 12 runs (the results record, finding 2). Without it, at exact timing, the right reading wins by
    // 1.07 nats for "TEST" and 1.27 nats for "TU" (derived: docs/signal-processing.md, A.8d), thin margins.
    struct Case {
        std::string word;
        unsigned seed;
    };
    std::vector<Case> cases;
    for (const std::string word : {"TEST", "TU"})
        for (const unsigned seed : {39u, 40u, 41u}) cases.push_back({word, seed});
    default_shared();
    std::vector<std::future<std::string>> decoded;
    for (const Case& c : cases)
        decoded.push_back(std::async(std::launch::async, [&c] {
            const auto keying = signals::keying(c.word, 25.0, 0.5);
            std::vector<Sample> x = silence(keying.back().second + 1.0);
            add_station(x, keying, 750.0, 20.0);
            add_noise(x, c.seed);
            return text_of(decode(ViterbiConfig{}, default_shared(), channel_stream(x, 750.0).samples, 32).text);
        }));
    for (std::size_t i = 0; i < cases.size(); ++i)
        EXPECT_EQ(decoded[i].get(), cases[i].word) << cases[i].word << ", seed " << cases[i].seed;
}

TEST(ViterbiDecoder, TenMinutesOfNoise) {
    // DEV2's noise alone on one channel, N0 its own, at the defaults, where the ceiling binds (plan §5, item 5; review
    // focus 1): after every block no more stories live than the ceiling and the shared text holds at most 4 x the
    // ceiling entries (bounded: it does not grow with the channel's time); after every eighth block (5.9 times a
    // second) every live story's score is finite, the best is 0 and the normalization is finite; at the end the open
    // timing's tables hold at most the 4 000 000 bytes search.hpp states, and the ceiling was reached. Ten minutes
    // under the full-sweep presets (KZ4AP_VITERBI_FULL_SWEEP=1, the test ViterbiDecoder.FullSweep), one minute
    // otherwise. The steady-clock time inside the decoder per channel-second is recorded (the CPU of one thread on an
    // idle machine).
    const double duration_s = full_sweep() ? 600.0 : 60.0;
    const ViterbiConfig config;
    ViterbiDecoder decoder(kChannelRate, config, default_shared());
    Channelizer channelizer(channelizer_config());
    channelizer.add_channel(1, 32);
    WhiteNoise noise(36, kSigma);
    const auto hops = static_cast<std::size_t>(duration_s * kInputRate / 1024.0);
    std::vector<Sample> hop(1024);
    double decoder_s = 0.0;
    std::size_t most_text = 0;
    std::size_t most_live = 0;
    std::uint64_t samples = 0;
    for (std::size_t h = 0; h < hops; ++h) {
        for (Sample& s : hop) s = noise.next();
        channelizer.push(hop, [&](std::uint32_t, std::uint64_t first, std::span<const Sample> block) {
            decoder.set_noise_density_fs2_per_hz(kN0);
            const auto started = std::chrono::steady_clock::now();
            decoder.process(block, static_cast<double>(first) / kChannelRate);
            decoder_s += std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
            samples += block.size();
            const viterbi::Search& search = decoder.search();
            ASSERT_LE(search.live(), static_cast<std::size_t>(config.ceiling)) << "sample " << first;
            most_live = std::max(most_live, search.live());
            most_text = std::max(most_text, search.text_entries());
            ASSERT_LE(search.text_entries(), 4u * static_cast<std::size_t>(config.ceiling)) << "sample " << first;
            if ((first / 32) % 8 != 0) return;  // every story's score at every eighth block (5.9 per second)
            double best = -kInf;
            for (const viterbi::StoryView& story : search.live_stories()) {
                ASSERT_TRUE(std::isfinite(story.score_nats)) << "sample " << first;
                best = std::max(best, story.score_nats);
            }
            ASSERT_EQ(best, 0.0) << "sample " << first;
            ASSERT_TRUE(std::isfinite(search.normalization_nats()));
        });
        if (::testing::Test::HasFatalFailure()) return;
    }
    const double channel_s = static_cast<double>(samples) / kChannelRate;
    ASSERT_GE(channel_s, duration_s - 1.0);
    EXPECT_EQ(most_live, static_cast<std::size_t>(config.ceiling));  // the ceiling bound
    EXPECT_LE(decoder.search().open_timing_bytes(), 4'000'000u);
    const double ms_per_channel_s = 1000.0 * decoder_s / channel_s;
    record("channel_seconds", channel_s);
    record("decoder_ms_per_channel_s", ms_per_channel_s);
    record("most_text_entries", static_cast<double>(most_text));
    record("open_timing_bytes", static_cast<double>(decoder.search().open_timing_bytes()));
    std::printf("[ info ] %.1f s of noise: %.1f ms of decoder time per channel-second, at most %zu text "
                "entries, %zu bytes of open-timing tables\n",
                channel_s, ms_per_channel_s, most_text, decoder.search().open_timing_bytes());
}

TEST(ViterbiDecoder, TheEngineGivesEachChannelItsGenieAndItsFrequency) {
    // Two oracle channels at 25 WPM, +15 dB S500. The first station drifts at 0.1 Hz/s from -4 008.1 Hz at 0 s, and its
    // oracle track says so (one segment); it is given the genie's fist, its own (a 48 ms dot, the ITU ratios). The
    // second has no genie and a constant label. Both texts exact; each channel's reported frequency (the event's
    // freq_hz) is the frequency the channel follows at its last block's start: the first's label line there, within
    // 1e-9 Hz, and the second's label.
    const std::string first_text = "CQ CQ DE K1ABC";
    const std::string second_text = "TEST W9XYZ";
    const auto first_keying = signals::keying(first_text, 25.0, 0.6);
    const auto second_keying = signals::keying(second_text, 25.0, 0.8);
    const double duration_s = std::max(first_keying.back().second, second_keying.back().second) + 1.0;
    std::vector<Sample> x = silence(duration_s);
    constexpr double kFirstHz = -4008.1;  // at 0 s; then + 0.1 Hz/s x t
    constexpr double kSecondHz = 5006.4;
    add_station(x, first_keying, kFirstHz, 15.0, 0.1);
    add_station(x, second_keying, kSecondHz, 15.0);
    add_noise(x, 38);
    EngineConfig config = viterbi_engine();
    config.oracle_frequencies_hz = {kFirstHz, kSecondHz};
    config.oracle_frequency_tracks = {{OracleSegment{0.0, kFirstHz, 0.1, 0.0}}, {}};
    viterbi::ViterbiGenie genie;
    genie.fist = viterbi::ViterbiGenie::Fist{48.0, {{1.0, 3.0, 1.0, 3.0, 7.0}}, {{0.3, 0.3, 0.3, 0.3, 0.3}},
                                             {{0.0, 0.0, 0.0, 0.0, 0.0}}};
    config.viterbi_genie = {genie, viterbi::ViterbiGenie{}};
    const EngineRun run = run_engine(config, x);
    EXPECT_EQ(run.text.count(1) ? run.text.at(1) : std::string("(nothing)"), first_text);
    EXPECT_EQ(run.text.count(2) ? run.text.at(2) : std::string("(nothing)"), second_text);
    // The engine pads the last hop of 1 024 input samples with zeros: the channel's last block starts 32 samples before
    // the end of its last hop.
    const std::size_t hops = (x.size() + 1023) / 1024;
    const double last_block_s = static_cast<double>((hops - 1) * 32) / kChannelRate;
    ASSERT_TRUE(run.freq_hz.count(1) && run.freq_hz.count(2));
    EXPECT_NEAR(run.freq_hz.at(1), kFirstHz + 0.1 * last_block_s, 1e-9);
    EXPECT_EQ(run.freq_hz.at(2), kSecondHz);
}

TEST(ViterbiDecoder, TheEngineGivesTheBenchEachChannelsSearchStatistics) {
    // Two oracle channels with the statistics on (ViterbiConfig::stats), a station at +15 dB S500 on the first and
    // noise alone on the second: Engine::viterbi_stats(id) is that channel's decoder's SearchStats, read after the
    // run. Its pieces are the channel's less the first block, which is not searched (N0 is 0 until the first spectrum
    // frame: 10 pieces of 3 samples in the block's 32); the histogram holds one count per piece searched, the first
    // newborn line is the channel's first story, and the two channels' statistics differ. An id that is no channel
    // gives nullptr.
    const auto keying = signals::keying("CQ TEST", 25.0, 0.6);
    std::vector<Sample> x = silence(keying.back().second + 1.0);
    add_station(x, keying, 1203.0, 15.0);
    add_noise(x, 41);
    EngineConfig config = viterbi_engine();
    config.oracle_frequencies_hz = {1203.0, -2501.0};
    config.viterbi.stats = true;
    EventBus bus;
    Engine engine(config, bus);
    run_to_the_end(engine, x);
    const viterbi::SearchStats* first = engine.viterbi_stats(1);
    const viterbi::SearchStats* second = engine.viterbi_stats(2);
    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);
    EXPECT_NE(first, second);
    const auto channel_pieces = static_cast<std::int64_t>(engine.stats().channel_samples / 2) / kSamplesPerPiece;
    for (const viterbi::SearchStats* s : {first, second}) {
        EXPECT_LE(s->pieces, channel_pieces);
        EXPECT_GE(s->pieces, channel_pieces - 25);
        std::int64_t counted = 0;
        for (const std::int64_t count : s->live_histogram) counted += count;
        EXPECT_EQ(counted, s->pieces);
        ASSERT_FALSE(s->lines.empty());
        EXPECT_EQ(s->lines[0].kind, viterbi::LineKind::Start);
        EXPECT_FALSE(s->live_mean_100ms.empty());
        EXPECT_EQ(s->live_mean_100ms.size(), s->live_max_100ms.size());
    }
    EXPECT_FALSE(*first == *second);  // SearchStats' own comparison: different signals, different statistics
    EXPECT_EQ(engine.viterbi_stats(0), nullptr);
    EXPECT_EQ(engine.viterbi_stats(3), nullptr);
    EXPECT_EQ(engine.viterbi_stats(4294967295u), nullptr);
}

TEST(ViterbiDecoder, TheEnginesStatisticsAreForTheNewDecoderAndEmptyUnlessKept) {
    // A channel of another decoder has no statistics to give: nullptr. The new decoder's channel with the statistics
    // off gives its SearchStats, empty (the search keeps nothing then).
    std::vector<Sample> x = silence(1.0);
    add_noise(x, 43);
    for (const FrontEnd front_end : {FrontEnd::Envelope, FrontEnd::Matched, FrontEnd::Bank}) {
        EngineConfig config;
        config.sample_rate = kInputRate;
        config.decoder.front_end = front_end;
        config.oracle_frequencies_hz = {1203.0};
        EventBus bus;
        Engine engine(config, bus);
        run_to_the_end(engine, x);
        EXPECT_EQ(engine.viterbi_stats(1), nullptr) << "decoder " << static_cast<int>(front_end);
    }
    EngineConfig config = viterbi_engine();
    config.oracle_frequencies_hz = {1203.0};
    ASSERT_FALSE(config.viterbi.stats);
    EventBus bus;
    Engine engine(config, bus);
    run_to_the_end(engine, x);
    const viterbi::SearchStats* stats = engine.viterbi_stats(1);
    ASSERT_NE(stats, nullptr);
    EXPECT_EQ(*stats, viterbi::SearchStats{});
    EXPECT_EQ(engine.viterbi_stats(2), nullptr);
}

TEST(ViterbiDecoder, WithGenieSetsOnlyThePartsItIsGiven) {
    // with_genie (config.hpp) puts one oracle channel's genie values into its configuration: the fading's spread and
    // the fist (one speed, the sender's dot, and its medians, sigmas and shifts); a part not given leaves the
    // configuration as it was (the carrier is no genie value: oracle channels follow the label's frequency). A decoder
    // runs on each (a station decoded with the genie's fist and with its spread). The engine refuses genie values for
    // any other decoder.
    ViterbiConfig base;
    base.beam_nats = 33.0;  // a field no genie touches
    const auto rest_unchanged = [&base](const ViterbiConfig& c) {
        EXPECT_EQ(c.beam_nats, 33.0);
        EXPECT_EQ(c.ceiling, base.ceiling);
        EXPECT_EQ(c.fading_spread_hz, base.fading_spread_hz);
        EXPECT_EQ(c.keying_rise_s, base.keying_rise_s);
    };
    const auto fist_unchanged = [&base](const ViterbiConfig& c) {
        EXPECT_EQ(c.speed_count, base.speed_count);
        EXPECT_EQ(c.dot_min_ms, base.dot_min_ms);
        EXPECT_EQ(c.fist_medians_dots, base.fist_medians_dots);
        EXPECT_EQ(c.fist_sigmas, base.fist_sigmas);
        EXPECT_EQ(c.fist_shifts_dots, base.fist_shifts_dots);
    };
    {
        SCOPED_TRACE("nothing given");
        const ViterbiConfig c = viterbi::with_genie(base, viterbi::ViterbiGenie{});
        EXPECT_FALSE(c.genie_fading_spread_hz.has_value());
        fist_unchanged(c);
        rest_unchanged(c);
    }
    const auto keying = signals::keying("PARIS", 25.0, 0.5);
    std::vector<Sample> x = silence(keying.back().second + 1.0);
    add_station(x, keying, 750.0, 15.0);
    add_noise(x, 43);
    const Stream stream = channel_stream(x, 750.0);
    {
        SCOPED_TRACE("the spread");
        viterbi::ViterbiGenie genie;
        genie.fading_spread_hz = 0.6;
        const ViterbiConfig c = viterbi::with_genie(base, genie);
        ASSERT_TRUE(c.genie_fading_spread_hz.has_value());
        EXPECT_EQ(*c.genie_fading_spread_hz, 0.6);
        fist_unchanged(c);
        rest_unchanged(c);
        // Its blocks, 650 ms (325 pieces) at 0.6 Hz, need shared parts built for it: the default ones are refused.
        EXPECT_EQ(viterbi_block_pieces(c), std::optional<int>(325));
        EXPECT_THROW(ViterbiDecoder(kChannelRate, c, default_shared()), std::invalid_argument);
        EXPECT_EQ(text_of(decode(c, make_viterbi_shared(c, channelizer_config()), stream.samples, 32).text),
                  "PARIS");
    }
    {
        SCOPED_TRACE("the fist");  // the sender's: 25 WPM, the ITU ratios, sigmas and shifts of its own
        viterbi::ViterbiGenie genie;
        genie.fist = viterbi::ViterbiGenie::Fist{48.0, {{1.0, 3.0, 1.0, 3.0, 7.0}}, {{0.1, 0.12, 0.15, 0.2, 0.25}},
                                                 {{0.02, -0.02, 0.0, 0.0, 0.05}}};
        const ViterbiConfig c = viterbi::with_genie(base, genie);
        EXPECT_EQ(c.speed_count, 1);
        EXPECT_EQ(c.dot_min_ms, 48.0);
        EXPECT_EQ(c.fist_medians_dots, genie.fist->medians_dots);
        EXPECT_EQ(c.fist_sigmas, genie.fist->sigmas);
        EXPECT_EQ(c.fist_shifts_dots, genie.fist->shifts_dots);
        EXPECT_FALSE(c.genie_fading_spread_hz.has_value());
        rest_unchanged(c);
        EXPECT_EQ(text_of(decode(c, default_shared(), stream.samples, 32).text), "PARIS");
    }
    // Genie values belong to the new decoder's oracle channels only.
    EventBus bus;
    for (const FrontEnd other : {FrontEnd::Matched, FrontEnd::Envelope, FrontEnd::Bank}) {
        EngineConfig engine;
        engine.sample_rate = kInputRate;
        engine.decoder.front_end = other;
        engine.oracle_frequencies_hz = {1000.0};
        engine.viterbi_genie = {viterbi::ViterbiGenie{}};
        EXPECT_THROW(Engine(engine, bus), std::invalid_argument) << static_cast<int>(other);
    }
}

TEST(ViterbiDecoder, FlushesOnceAndResetStartsAfresh) {
    // flush publishes the whole text once: a second flush and later blocks publish nothing. reset starts afresh: the same
    // stream again gives the same text with the same times as a new decoder.
    const auto keying = signals::keying("PARIS", 25.0, 0.5);
    std::vector<Sample> x = silence(keying.back().second + 1.0);
    add_station(x, keying, 750.0, 15.0);
    add_noise(x, 39);
    const Stream stream = channel_stream(x, 750.0);
    ViterbiDecoder decoder(kChannelRate, ViterbiConfig{}, default_shared());
    const auto feed = [&] {
        for (std::size_t first = 0; first < stream.samples.size(); first += 32) {
            decoder.set_noise_density_fs2_per_hz(kN0);
            EXPECT_TRUE(decoder
                            .process(std::span<const Sample>(stream.samples).subspan(first, 32),
                                     static_cast<double>(first) / kChannelRate)
                            .chars.empty());
        }
    };
    feed();
    const std::vector<DecodedSymbol> text = decoder.flush().chars;
    EXPECT_EQ(text_of(text), "PARIS");
    EXPECT_TRUE(decoder.flush().chars.empty());
    EXPECT_FALSE(decoder.process(std::span<const Sample>(stream.samples).subspan(0, 32), 99.0).freq_offset_hz);
    decoder.reset();
    feed();
    const std::vector<DecodedSymbol> again = decoder.flush().chars;
    ASSERT_EQ(again.size(), text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        EXPECT_EQ(again[i].text, text[i].text);
        EXPECT_EQ(again[i].start_s, text[i].start_s);
        EXPECT_EQ(again[i].end_s, text[i].end_s);
        EXPECT_EQ(again[i].probability, 1.0f);
    }
}

TEST(ViterbiDecoder, RefusesWhatItCannotUse) {
    const ViterbiConfig defaults;
    const std::shared_ptr<const ViterbiShared>& shared = default_shared();
    // Shared parts are made only by make_viterbi_shared, which computes the rise from the channelizer's taps and builds
    // the flat prior with it, so no decoder can be given a prior without the matched filter to the rise: they cannot be
    // made, copied or assigned otherwise.
    static_assert(!std::is_default_constructible_v<ViterbiShared>);
    static_assert(!std::is_copy_constructible_v<ViterbiShared>);
    static_assert(!std::is_move_constructible_v<ViterbiShared>);
    static_assert(!std::is_copy_assignable_v<ViterbiShared>);
    static_assert(!std::is_aggregate_v<ViterbiShared>);
    // The shared parts of the defaults at 48 kHz: 3 samples per piece, c to 3 x 195 samples, the rise from the taps
    // (computed here), the flat prior over 195 pieces built with that rise.
    EXPECT_EQ(shared->channel_rate_hz(), 1500.0);
    EXPECT_EQ(shared->samples_per_piece(), 3);
    EXPECT_EQ(shared->c().size(), 586u);
    const ChannelizerConfig channel = channelizer_config();
    const std::vector<double> rise =
        viterbi::rise_fractions(channel_filter_taps(channel), kInputRate, 32, kSamplesPerPiece, defaults.keying_rise_s);
    ASSERT_EQ(rise.size(), 4u);
    EXPECT_TRUE(std::ranges::equal(shared->rise(), rise));
    EXPECT_EQ(shared->flat_prior()->max_pieces(), 195);
    EXPECT_TRUE(std::ranges::equal(shared->flat_prior()->rise(), rise));
    EXPECT_EQ(viterbi_first_block_pieces(defaults), 195);
    EXPECT_EQ(viterbi_block_pieces(defaults), std::optional<int>(195));
    ViterbiConfig no_fading;
    no_fading.fading_spread_hz = 0.0;
    EXPECT_EQ(viterbi_block_pieces(no_fading), std::nullopt);
    EXPECT_EQ(viterbi_first_block_pieces(no_fading), 1000);  // the cap, 2 s
    EXPECT_NO_THROW(ViterbiDecoder(kChannelRate, defaults, shared));

    // The ClassicalDecoder is not the new decoder.
    ClassicalDecoderConfig classical;
    classical.front_end = FrontEnd::Viterbi;
    EXPECT_THROW(ClassicalDecoder(kChannelRate, classical), std::invalid_argument);

    // Shared parts: at least one configuration; all of them agreeing on the piece, the rise and the level range; a piece
    // of whole channel samples.
    EXPECT_THROW(make_viterbi_shared(std::span<const ViterbiConfig>{}, channelizer_config()), std::invalid_argument);
    ViterbiConfig other_rise;
    other_rise.keying_rise_s = 0.004;
    const std::vector<ViterbiConfig> disagree = {defaults, other_rise};
    EXPECT_THROW(make_viterbi_shared(disagree, channelizer_config()), std::invalid_argument);
    ViterbiConfig fractional;
    fractional.piece_s = 0.0021;  // 3.15 channel samples
    EXPECT_THROW(make_viterbi_shared(fractional, channelizer_config()), std::invalid_argument);
    // One set of shared parts serves configurations whose blocks differ: its flat prior spans the longest first block.
    ViterbiConfig slow_fading;
    slow_fading.genie_fading_spread_hz = 0.5;  // B = 390 pieces
    const std::vector<ViterbiConfig> both = {defaults, slow_fading};
    const auto wide = make_viterbi_shared(both, channelizer_config());
    EXPECT_EQ(wide->flat_prior()->max_pieces(), 390);
    EXPECT_NO_THROW(ViterbiDecoder(kChannelRate, slow_fading, wide));
    EXPECT_NO_THROW(ViterbiDecoder(kChannelRate, defaults, wide));

    // A piece must be a whole number of channel samples: at 44.1 and 88.2 kHz the channel rate is 1 378.125 samples/s,
    // where 2 ms is 2.756 samples, so neither the engine nor the decoder runs there (with the default piece), and each
    // says why.
    const auto message_of = [](const auto& make) -> std::string {
        try {
            make();
        } catch (const std::invalid_argument& e) {
            return e.what();
        }
        return "(nothing thrown)";
    };
    const std::string decoder_message =
        message_of([&] { return ViterbiDecoder(44100.0 / 32.0, defaults, shared); });
    EXPECT_NE(decoder_message.find("whole number"), std::string::npos) << decoder_message;
    EXPECT_NE(decoder_message.find("1378.125"), std::string::npos) << decoder_message;
    for (const int rate : {44100, 88200}) {
        EngineConfig engine = viterbi_engine();
        engine.sample_rate = rate;
        engine.oracle_frequencies_hz = {1000.0};
        EventBus no_bus;
        const std::string engine_message = message_of([&] { Engine refused(engine, no_bus); });
        EXPECT_NE(engine_message.find("whole number"), std::string::npos) << rate << " Hz: " << engine_message;
        EXPECT_NE(engine_message.find("1378.125"), std::string::npos) << rate << " Hz: " << engine_message;
    }

    // The decoder: a positive rate, shared parts built for its rate, piece, rise, level range and blocks, and what the
    // search refuses.
    const auto refuses = [](const auto& make) { EXPECT_THROW(make(), std::invalid_argument); };
    refuses([&] { return ViterbiDecoder(0.0, defaults, shared); });
    refuses([&] { return ViterbiDecoder(kNaN, defaults, shared); });
    refuses([&] { return ViterbiDecoder(kInf, defaults, shared); });
    refuses([&] { return ViterbiDecoder(kChannelRate, defaults, nullptr); });
    refuses([&] { return ViterbiDecoder(3000.0, defaults, shared); });
    refuses([&] { return ViterbiDecoder(kChannelRate, other_rise, shared); });
    refuses([&] { return ViterbiDecoder(kChannelRate, slow_fading, shared); });  // its first block is 390 pieces
    ViterbiConfig other_level;
    other_level.level_max_db = 80.0;
    refuses([&] { return ViterbiDecoder(kChannelRate, other_level, shared); });
    ViterbiConfig no_ceiling;
    no_ceiling.ceiling = 0;
    refuses([&] { return ViterbiDecoder(kChannelRate, no_ceiling, shared); });

    // N0: finite and not negative (0: nothing searched).
    ViterbiDecoder decoder(kChannelRate, defaults, shared);
    for (const double n0 : {-1e-9, kNaN, kInf})
        EXPECT_THROW(decoder.set_noise_density_fs2_per_hz(n0), std::invalid_argument) << n0;
    EXPECT_NO_THROW(decoder.set_noise_density_fs2_per_hz(0.0));
    // A block must follow the one before it: a gap of a sample or more is refused, and changes nothing.
    const std::vector<Sample> block(32);
    decoder.set_noise_density_fs2_per_hz(kN0);
    decoder.process(block, 10.0);
    EXPECT_THROW(decoder.process(block, 10.0 + 33.0 / kChannelRate), std::invalid_argument);
    EXPECT_NO_THROW(decoder.process(block, 10.0 + 32.0 / kChannelRate));

    // The engine: genie values only with oracle channels, one per oracle frequency.
    EngineConfig engine = viterbi_engine();
    engine.viterbi_genie = {viterbi::ViterbiGenie{}};
    EventBus bus;
    EXPECT_THROW(Engine(engine, bus), std::invalid_argument);  // no oracle frequencies
    engine.oracle_frequencies_hz = {1000.0, 2000.0};
    EXPECT_THROW(Engine(engine, bus), std::invalid_argument);  // one entry for two channels
    engine.viterbi_genie.push_back(viterbi::ViterbiGenie{});
    EXPECT_NO_THROW(Engine(engine, bus));
}
