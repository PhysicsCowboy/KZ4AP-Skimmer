#include "kz4ap/engine.hpp"

#include "test_signals.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <limits>
#include <map>
#include <numbers>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using namespace kz4ap;
using kz4ap::test::keyed_signal;
using kz4ap::test::keying;

namespace {

constexpr double kRate = 192000;
constexpr double kNoiseSigma = 0.02;
constexpr double kAmplitude20dB = 0.0102;  // 20 dB SNR in 500 Hz against kNoiseSigma

struct Result {
    std::vector<Track> born;
    std::map<std::uint32_t, std::string> text;
};

Result run(const std::vector<Sample>& x, std::size_t chunk, int sample_rate = static_cast<int>(kRate)) {
    EventBus bus;
    Result r;
    bus.subscribe([&](const Event& e) {
        if (const auto* t = std::get_if<TrackEvent>(&e); t && t->kind == TrackEvent::Kind::Born) r.born.push_back(t->track);
        if (const auto* d = std::get_if<DecodedTextEvent>(&e)) {
            for (const auto& c : d->chars) r.text[d->track_id] += c.text;
        }
    });
    EngineConfig config;
    config.sample_rate = sample_rate;
    config = with_envelope_path(config);  // the milestone-1 pipeline
    Engine engine(config, bus);
    for (std::size_t i = 0; i < x.size(); i += chunk) {
        engine.process(std::span<const Sample>(x).subspan(i, std::min(chunk, x.size() - i)));
    }
    engine.finish();
    return r;
}

std::vector<Sample> band(double duration_s) {
    auto x = keyed_signal("VVV CQ K1ABC", 25, kRate, duration_s, 12000.0, kAmplitude20dB, kNoiseSigma, 11);
    const auto y = keyed_signal("VVV CQ W9XYZ", 22, kRate, duration_s, -30000.0, kAmplitude20dB, 0.0, 12);
    for (std::size_t i = 0; i < x.size(); ++i) x[i] += y[i];
    return x;
}

struct TextLog {
    std::map<std::uint32_t, std::string> text;
    std::map<std::uint32_t, std::vector<DecodedSymbol>> chars;  // every decoded symbol, in order
    std::map<std::uint32_t, double> last_freq;                  // DecodedTextEvent::freq_hz of the latest event
    std::map<std::uint32_t, double> last_end_s;                 // end time of the latest symbol
    // (end time of the event's last symbol, s; DecodedTextEvent::freq_hz) for every event
    std::map<std::uint32_t, std::vector<std::pair<double, double>>> freq_at;
    std::vector<Track> born;
    std::vector<std::uint32_t> died;  // Died events before finish()
};

TextLog run_with(const EngineConfig& config, const std::vector<Sample>& x, std::size_t chunk = 65536) {
    EventBus bus;
    TextLog log;
    bus.subscribe([&](const Event& e) {
        if (const auto* t = std::get_if<TrackEvent>(&e)) {
            if (t->kind == TrackEvent::Kind::Born) log.born.push_back(t->track);
            else log.died.push_back(t->track.id);
        }
        if (const auto* d = std::get_if<DecodedTextEvent>(&e)) {
            for (const auto& c : d->chars) log.text[d->track_id] += c.text;
            auto& all = log.chars[d->track_id];
            all.insert(all.end(), d->chars.begin(), d->chars.end());
            log.last_freq[d->track_id] = d->freq_hz;
            log.last_end_s[d->track_id] = d->chars.back().end_s;
            log.freq_at[d->track_id].emplace_back(d->chars.back().end_s, d->freq_hz);
        }
    });
    Engine engine(config, bus);
    for (std::size_t i = 0; i < x.size(); i += chunk) {
        engine.process(std::span<const Sample>(x).subspan(i, std::min(chunk, x.size() - i)));
    }
    engine.finish();
    return log;
}

EngineConfig matched_config(int sample_rate = static_cast<int>(kRate)) {
    EngineConfig c;
    c.sample_rate = sample_rate;  // the Matched path is the default
    return c;
}

constexpr double kAmplitude20dB48k = 0.0204;  // 20 dB SNR in 500 Hz against kNoiseSigma at 48 kHz

// Key-down amplitude giving s500_db (key-down power over the noise in 500 Hz, dB) against kNoiseSigma at 48 kHz.
double amplitude_48k(double s500_db) { return kNoiseSigma * std::sqrt(500.0 / 48000.0) * std::pow(10.0, s500_db / 20.0); }

void add_to(std::vector<Sample>& a, const std::vector<Sample>& b) {
    for (std::size_t i = 0; i < a.size() && i < b.size(); ++i) a[i] += b[i];
}

bool ends_with(const std::string& s, const std::string& tail) {
    return s.size() >= tail.size() && s.compare(s.size() - tail.size(), tail.size(), tail) == 0;
}

// The text of track id's symbols that start in [from_s, to_s), without leading or trailing spaces.
std::string segment(const TextLog& log, std::uint32_t id, double from_s, double to_s = 1e9) {
    std::string out;
    if (const auto it = log.chars.find(id); it != log.chars.end()) {
        for (const auto& c : it->second)
            if (c.start_s >= from_s && c.start_s < to_s) out += c.text;
    }
    const auto first = out.find_first_not_of(' ');
    if (first == std::string::npos) return {};
    return out.substr(first, out.find_last_not_of(' ') - first + 1);
}

// Levenshtein distance between a and b with word spaces removed: VE3NEA's no-space measure, the
// one the option-1 simulation used ("B decoded" = at most 3 edits against B's 12 characters, CER <= 0.3).
std::size_t nospace_edits(std::string a, std::string b) {
    std::erase(a, ' ');
    std::erase(b, ' ');
    std::vector<std::size_t> row(b.size() + 1);
    for (std::size_t j = 0; j <= b.size(); ++j) row[j] = j;
    for (std::size_t i = 1; i <= a.size(); ++i) {
        std::size_t diagonal = row[0];
        row[0] = i;
        for (std::size_t j = 1; j <= b.size(); ++j) {
            const std::size_t above = row[j];
            row[j] = std::min({row[j] + 1, row[j - 1] + 1, diagonal + (a[i - 1] == b[j - 1] ? 0 : 1)});
            diagonal = above;
        }
    }
    return row[b.size()];
}

// The published frequency of track id's latest event whose last symbol ended before t_s (NaN if none).
double freq_before(const TextLog& log, std::uint32_t id, double t_s) {
    double f = std::numeric_limits<double>::quiet_NaN();
    if (const auto it = log.freq_at.find(id); it != log.freq_at.end()) {
        for (const auto& [end_s, freq_hz] : it->second)
            if (end_s < t_s) f = freq_hz;
    }
    return f;
}

constexpr double kTurnoverA = 12010.0;  // A's carrier, Hz from the span's center (10 Hz above its bin's center)
const std::string kTurnoverB = "DE W9XYZ PARIS";

struct EngineTurnover {
    TextLog log;
    double b0;  // B's first key-down, s
    double a0;  // the first key-down of A's second over, s
};

// A two-station turnover at 48 kHz: A (kTurnoverA, S500 = 15 dB, 25 WPM, "PARIS PARIS PARIS PARIS"),
// 1 s of silence, B (offset_hz above A, relative_db re A's key-down power, 18 WPM, "DE W9XYZ PARIS",
// 9.4 s), 1 s, A again, then 1.5 s of noise: the scene of the option-1 simulation (Design decisions B;
// there A sat 7.8 Hz below its bin's center, here 10 Hz above).
EngineTurnover engine_turnover(double offset_hz, double relative_db, unsigned seed) {
    const std::string a = "PARIS PARIS PARIS PARIS";
    const double amp = amplitude_48k(15.0);
    const double b0 = keying(a, 25, 0.5).back().second + 1.0;
    const double a0 = keying(kTurnoverB, 18, b0).back().second + 1.0;
    const double total = keying(a, 25, a0).back().second + 1.5;
    auto x = keyed_signal(a, 25, 48000.0, total, kTurnoverA, amp, kNoiseSigma, seed);
    add_to(x, keyed_signal(kTurnoverB, 18, 48000.0, total, kTurnoverA + offset_hz,
                           amp * std::pow(10.0, relative_db / 20.0), 0.0, seed + 1, b0));
    add_to(x, keyed_signal(a, 25, 48000.0, total, kTurnoverA, amp, 0.0, seed + 2, a0));
    return {run_with(matched_config(48000), x), b0, a0};
}

}  // namespace

TEST(Engine, FindsAndDecodesTwoSignals) {
    const auto r = run(band(9.0), 65536);
    ASSERT_EQ(r.born.size(), 2u);
    std::map<double, std::string> by_freq;
    for (const auto& t : r.born) by_freq[t.freq_hz] = r.text.count(t.id) ? r.text.at(t.id) : "";
    const auto low = by_freq.begin();
    const auto high = std::next(low);
    EXPECT_NEAR(low->first, -30000.0, 25.0);
    EXPECT_NEAR(high->first, 12000.0, 25.0);
    EXPECT_NE(low->second.find("CQ W9XYZ"), std::string::npos) << low->second;
    EXPECT_NE(high->second.find("CQ K1ABC"), std::string::npos) << high->second;
}

TEST(Engine, ChoosesFftSizeFromSampleRate) {
    // Largest power of two keeping bins at least 20 Hz wide (23.4 Hz at each of these rates).
    EXPECT_EQ(choose_fft_size(48000), 2048);
    EXPECT_EQ(choose_fft_size(96000), 4096);
    EXPECT_EQ(choose_fft_size(192000), 8192);
    EXPECT_EQ(choose_fft_size(768000), 32768);
}

TEST(Engine, RejectsUnusableSampleRates) {
    EXPECT_THROW(choose_fft_size(0), std::invalid_argument);
    EXPECT_THROW(choose_fft_size(7999), std::invalid_argument);
    EventBus bus;
    auto config = with_envelope_path(EngineConfig{});
    config.sample_rate = 4000;
    EXPECT_THROW(Engine(config, bus), std::invalid_argument);
}

TEST(Engine, FindsAndDecodesTwoSignalsAt48kHz) {
    // Same 20 dB SNR in 500 Hz as at 192 kHz: a quarter of the bandwidth needs half the noise sigma.
    constexpr double rate = 48000;
    constexpr double sigma = kNoiseSigma / 2;
    auto x = keyed_signal("VVV CQ K1ABC", 25, rate, 9.0, 8000.0, kAmplitude20dB, sigma, 21);
    const auto y = keyed_signal("VVV CQ W9XYZ", 22, rate, 9.0, -12000.0, kAmplitude20dB, 0.0, 22);
    for (std::size_t i = 0; i < x.size(); ++i) x[i] += y[i];

    const auto r = run(x, 16384, static_cast<int>(rate));
    ASSERT_EQ(r.born.size(), 2u);
    std::map<double, std::string> by_freq;
    for (const auto& t : r.born) by_freq[t.freq_hz] = r.text.count(t.id) ? r.text.at(t.id) : "";
    const auto low = by_freq.begin();
    const auto high = std::next(low);
    EXPECT_NEAR(low->first, -12000.0, 25.0);
    EXPECT_NEAR(high->first, 8000.0, 25.0);
    EXPECT_NE(low->second.find("CQ W9XYZ"), std::string::npos) << low->second;
    EXPECT_NE(high->second.find("CQ K1ABC"), std::string::npos) << high->second;
}

TEST(Engine, ChunkingDoesNotChangeResults) {
    const auto x = band(9.0);
    const auto a = run(x, 1000);
    const auto b = run(x, 77777);
    ASSERT_EQ(a.born.size(), b.born.size());
    for (std::size_t i = 0; i < a.born.size(); ++i) EXPECT_EQ(a.born[i].freq_hz, b.born[i].freq_hz);
    EXPECT_EQ(a.text, b.text);
}

TEST(Engine, EventsFollowTrackLifecycle) {
    // One signal, then 12 s of noise only, so the track is born, decodes, and dies before finish().
    // Death is slow: the detector's 1 s power average takes about 7 s to fall from ~27 dB to the
    // 3 dB hold level, then death_s runs out (measured: died 7.8 s after the last mark).
    const std::string sent = "VVV CQ K1ABC";
    const auto x = keyed_signal(sent, 25, kRate, kz4ap::test::duration_for(sent, 25, 12.0), 12000.0,
                                kAmplitude20dB, kNoiseSigma, 14);

    struct Logged {
        enum class Kind { Born, Died, Text } kind;
        std::uint32_t id;
        std::vector<double> starts;
        std::string text;
    };
    std::vector<Logged> log;
    EventBus bus;
    bus.subscribe([&](const Event& e) {
        if (const auto* t = std::get_if<TrackEvent>(&e)) {
            log.push_back({t->kind == TrackEvent::Kind::Born ? Logged::Kind::Born : Logged::Kind::Died,
                           t->track.id, {}, {}});
        }
        if (const auto* d = std::get_if<DecodedTextEvent>(&e)) {
            Logged l{Logged::Kind::Text, d->track_id, {}, {}};
            for (const auto& c : d->chars) {
                l.starts.push_back(c.start_s);
                l.text += c.text;
            }
            log.push_back(std::move(l));
        }
    });
    auto config = with_envelope_path(EngineConfig{});
    config.detector.death_s = 1.0;
    Engine engine(config, bus);
    for (std::size_t i = 0; i < x.size(); i += 65536) {
        engine.process(std::span<const Sample>(x).subspan(i, std::min<std::size_t>(65536, x.size() - i)));
    }
    engine.finish();

    std::map<std::uint32_t, std::size_t> born_at, died_at, borns, dieds;
    for (std::size_t i = 0; i < log.size(); ++i) {
        if (log[i].kind == Logged::Kind::Born) {
            born_at[log[i].id] = i;
            ++borns[log[i].id];
        }
        if (log[i].kind == Logged::Kind::Died) {
            died_at[log[i].id] = i;
            ++dieds[log[i].id];
        }
    }
    ASSERT_EQ(borns.size(), 1u);
    const std::uint32_t id = borns.begin()->first;
    EXPECT_EQ(borns[id], 1u);
    ASSERT_EQ(dieds.size(), 1u);
    EXPECT_EQ(dieds.count(id), 1u);
    EXPECT_EQ(dieds[id], 1u);

    std::string text;
    double last_start = -1.0;
    for (std::size_t i = 0; i < log.size(); ++i) {
        if (log[i].kind != Logged::Kind::Text) continue;
        ASSERT_EQ(born_at.count(log[i].id), 1u) << "text for a track that was never born: " << log[i].id;
        EXPECT_LT(born_at[log[i].id], i);
        EXPECT_GT(died_at[log[i].id], i);
        for (const double s : log[i].starts) {
            EXPECT_GT(s, last_start) << "symbol delivered twice or out of order";
            last_start = s;
        }
        text += log[i].text;
    }
    EXPECT_NE(text.find("CQ K1ABC"), std::string::npos) << text;
}

TEST(Engine, NoiseAloneMakesNoTracks) {
    const auto r = run(keyed_signal("", 25, kRate, 4.0, 0, 0.0, kNoiseSigma, 13), 65536);
    EXPECT_TRUE(r.born.empty());
}

TEST(Engine, OracleOpensChannelsAtTheGivenFrequencies) {
    EventBus bus;
    std::vector<Track> born;
    std::map<std::uint32_t, std::string> text;
    bus.subscribe([&](const Event& e) {
        if (const auto* t = std::get_if<TrackEvent>(&e); t && t->kind == TrackEvent::Kind::Born) born.push_back(t->track);
        if (const auto* d = std::get_if<DecodedTextEvent>(&e)) {
            for (const auto& c : d->chars) text[d->track_id] += c.text;
        }
    });
    auto config = with_envelope_path(EngineConfig{});
    config.oracle_frequencies_hz = {12003.0, -30000.0};
    Engine engine(config, bus);
    ASSERT_EQ(born.size(), 2u);  // published by the constructor
    EXPECT_EQ(born[0].id, 1u);
    EXPECT_DOUBLE_EQ(born[0].freq_hz, 512 * 23.4375);  // 12003 Hz rounded to its bin
    EXPECT_EQ(born[1].id, 2u);
    EXPECT_DOUBLE_EQ(born[1].freq_hz, -30000.0);
    const auto x = band(9.0);
    engine.process(x);
    engine.finish();
    EXPECT_EQ(born.size(), 2u);  // the detector made no tracks of its own
    EXPECT_NE(text[1].find("CQ K1ABC"), std::string::npos) << text[1];
    EXPECT_NE(text[2].find("CQ W9XYZ"), std::string::npos) << text[2];
    EXPECT_NEAR(engine.stats().channel_seconds, 2 * 9.0, 0.05);
}

TEST(Engine, StatsCountChannelTimeOfDetectedTracks) {
    EventBus bus;
    const auto config = with_envelope_path(EngineConfig{});
    Engine engine(config, bus);
    engine.process(band(9.0));
    engine.finish();
    // Two stations, each tracked from about 1.5 s to the end.
    EXPECT_GT(engine.stats().channel_seconds, 2 * 6.0);
    EXPECT_LT(engine.stats().channel_seconds, 2 * 9.0);
    EXPECT_GE(engine.stats().decoder_seconds, 0.0);
}

TEST(Engine, MatchedFrontEndDecodesTwoSignals) {
    const auto log = run_with(matched_config(), band(9.0));
    ASSERT_EQ(log.born.size(), 2u);
    std::string all;
    for (const auto& [id, t] : log.text) all += t + "|";
    EXPECT_NE(all.find("CQ K1ABC"), std::string::npos) << all;
    EXPECT_NE(all.find("CQ W9XYZ"), std::string::npos) << all;
}

TEST(Engine, MatchedReportsDriftingFrequency) {
    // Slow drift only (owner, 2026-09-29): a station at 12003 Hz (3 Hz above its bin's center)
    // drifting +1 Hz/s, about 19 Hz over the 19.3 s message. The detector's track follows its own
    // peak across the bin boundary (11.7 Hz above the center), the engine moves the tracker's anchor
    // with it, and the published frequency follows. Simulated (option 1, engine level, 30 seeds; the
    // scene of Design decisions B, "Also re-checked"): one channel and the last six words intact in
    // 30 of 30; published frequency 1.49-1.63 Hz behind the carrier at the end of the last mark.
    const std::string msg = "PARIS PARIS PARIS PARIS PARIS PARIS PARIS PARIS";
    const double f0 = 12003.0;
    const auto x = keyed_signal(msg, 25, 48000.0, kz4ap::test::duration_for(msg, 25), f0, kAmplitude20dB48k,
                                kNoiseSigma, 41, 0.5, 1.0);
    const auto log = run_with(matched_config(48000), x);
    ASSERT_EQ(log.born.size(), 1u);
    const auto id = log.born[0].id;
    const double truth = f0 + 1.0 * log.last_end_s.at(id);  // the carrier's frequency when the last symbol ended
    // The simulated lag (at most 1.63 Hz) plus the spec's 2 Hz target.
    EXPECT_NEAR(log.last_freq.at(id), truth, 3.6);
    // It moved more than the tracker's own +/-12 Hz: the anchor followed the detector's peak.
    EXPECT_GT(std::abs(log.last_freq.at(id) - log.born[0].freq_hz), 12.0);
    // Trimmed: the decoder emits a designed word space after the silent tail.
    EXPECT_TRUE(ends_with(segment(log, id, 0.0), "PARIS PARIS PARIS PARIS PARIS PARIS")) << log.text.at(id);
}

TEST(Engine, OracleMatchedFindsTheResidualFromTheBinCenter) {
    // The oracle opens the channel on the bin center (12000 Hz), 9 Hz below the station; the NCO
    // starts there, and the tracker must find the 9 Hz itself (its anchor is the label, 12009 Hz).
    const std::string msg = "CQ TEST K1ABC K1ABC";
    const auto x = keyed_signal(msg, 25, 48000.0, kz4ap::test::duration_for(msg, 25), 12009.0, kAmplitude20dB48k,
                                kNoiseSigma, 42);
    auto config = matched_config(48000);
    config.oracle_frequencies_hz = {12009.0};
    const auto log = run_with(config, x);
    ASSERT_EQ(log.born.size(), 1u);
    EXPECT_DOUBLE_EQ(log.born[0].freq_hz, 12000.0);
    EXPECT_NEAR(log.last_freq.at(1), 12009.0, 2.0);
    EXPECT_NE(log.text.at(1).find("K1ABC"), std::string::npos) << log.text.at(1);
}

TEST(Engine, OracleAnchorsTheTrackerAtTheLabeledFrequency) {
    // Review finding I1 (controller's ruling): in oracle mode the tracker's anchor is the labeled
    // frequency, not the bin center. The label says 12011 Hz (bin center 12000 Hz); the station is
    // at 12019 Hz, 8 Hz from the label but 19 Hz from the bin center. Anchored at the label, the
    // tracker accepts 19 Hz (within +/-12 Hz of 11 Hz) and publishes about 12019 Hz; anchored at the
    // bin center it would reject every estimate and publish 12000 Hz (derived from Task 10's rule).
    const std::string msg = "CQ TEST K1ABC K1ABC";
    const auto x = keyed_signal(msg, 25, 48000.0, kz4ap::test::duration_for(msg, 25), 12019.0, kAmplitude20dB48k,
                                kNoiseSigma, 45);
    auto config = matched_config(48000);
    config.oracle_frequencies_hz = {12011.0};
    const auto log = run_with(config, x);
    ASSERT_EQ(log.born.size(), 1u);
    EXPECT_DOUBLE_EQ(log.born[0].freq_hz, 12000.0);  // the channel itself sits on the bin center
    EXPECT_NEAR(log.last_freq.at(1), 12019.0, 2.0);
}

TEST(Engine, ChannelTapSeesEachOracleChannelWithoutChangingTheDecode) {
    // Benchmark tooling (kz4ap-bench --record-channels): the tap sees every block of the channel as the
    // channelizer delivers it, before the decoder, and the decoded text is the same with or without it.
    const std::string msg = "CQ TEST K1ABC";
    const auto x = keyed_signal(msg, 25, 48000.0, kz4ap::test::duration_for(msg, 25), 12009.0, 1.0, 0.0, 42);
    auto config = matched_config(48000);
    config.oracle_frequencies_hz = {12009.0};
    const auto plain = run_with(config, x);

    EventBus bus;
    std::map<std::uint32_t, std::string> text;
    bus.subscribe([&](const Event& e) {
        if (const auto* d = std::get_if<DecodedTextEvent>(&e)) {
            for (const auto& c : d->chars) text[d->track_id] += c.text;
        }
    });
    Engine engine(config, bus);
    std::vector<Sample> stream;
    double center_hz = 0;
    std::uint64_t next_index = 0;
    bool contiguous = true;
    engine.set_channel_tap([&](const ChannelBlock& b) {
        EXPECT_EQ(b.track_id, 1u);
        contiguous = contiguous && b.first_index == next_index;
        next_index = b.first_index + b.samples.size();
        center_hz = b.center_hz;
        stream.insert(stream.end(), b.samples.begin(), b.samples.end());
    });
    for (std::size_t i = 0; i < x.size(); i += 65536) {
        engine.process(std::span<const Sample>(x).subspan(i, std::min<std::size_t>(65536, x.size() - i)));
    }
    engine.finish();

    EXPECT_EQ(text, plain.text);
    EXPECT_TRUE(contiguous);
    EXPECT_DOUBLE_EQ(engine.channel_rate(), 1500.0);
    EXPECT_DOUBLE_EQ(center_hz, 12000.0);
    EXPECT_EQ(stream.size(), engine.stats().channel_samples);
    // The station sits at label - center = 9 Hz in the stream (the channelizer is a complex shift by the
    // bin center, derived): mixed down by 9 Hz, consecutive key-down samples no longer rotate.
    double peak = 0;
    for (const auto& s : stream) peak = std::max(peak, static_cast<double>(std::abs(s)));
    std::complex<double> sum{};
    for (std::size_t n = 1; n < stream.size(); ++n) {
        if (std::abs(stream[n]) < 0.5 * peak || std::abs(stream[n - 1]) < 0.5 * peak) continue;
        const auto mix = [](std::size_t i) { return std::polar(1.0, -2.0 * std::numbers::pi * 9.0 * static_cast<double>(i) / 1500.0); };
        sum += std::complex<double>(stream[n]) * mix(n) * std::conj(std::complex<double>(stream[n - 1]) * mix(n - 1));
    }
    const double residual_hz = std::arg(sum) * 1500.0 / (2.0 * std::numbers::pi);
    EXPECT_LT(std::abs(residual_hz), 0.05);
}

TEST(Engine, MatchedChunkingDoesNotChangeResults) {
    const auto x = band(9.0);
    const auto a = run_with(matched_config(), x, 1000);
    const auto b = run_with(matched_config(), x, 77777);
    EXPECT_EQ(a.text, b.text);
    EXPECT_EQ(a.last_freq, b.last_freq);
}

TEST(Engine, TurnoverWithinTheChannelDistanceFollowsTheAnsweringStation) {
    // Option 1: B 25 Hz away (within D_ch = 47 Hz), 6 dB weaker (re A's key-down power). The detector's
    // track moves to B's peak (1.80 s into B's over, median) and back; A's channel follows. Simulated
    // (engine level, 30 numpy seeds): one channel in 30, B decoded (CER <= 0.3) by A's channel in 30
    // (at least 11 of 12 characters in 30), A's next over exact in 30, B's published frequency before
    // A resumes at most 0.15 Hz off. One seed. (Replaces the decoder-level test of the first design.)
    const auto r = engine_turnover(25.0, -6.0, 51);
    ASSERT_EQ(r.log.born.size(), 1u);
    const auto id = r.log.born[0].id;
    const auto b = segment(r.log, id, r.b0, r.a0);
    EXPECT_LE(nospace_edits(b, kTurnoverB), 3u) << b;
    EXPECT_EQ(segment(r.log, id, r.a0), "PARIS PARIS PARIS PARIS") << r.log.text.at(id);
    EXPECT_NEAR(freq_before(r.log, id, r.a0), kTurnoverA + 25.0, 1.0);
}

TEST(Engine, TurnoverAt40HzFollowsTheAnsweringStation) {
    // Option 1: B 40 Hz away at A's level (0 dB re A's key-down power). Simulated (engine level, 30
    // numpy seeds): one channel in 30, B decoded by A's channel in 30 (at least 11 of 12 characters
    // in 30; the channel on B 1.46 s into B's over, median), A's next over exact in 30, B's published
    // frequency before A resumes at most 0.07 Hz off. One seed. (At -10 dB re A: B decoded in 22 of 30,
    // 3 characters lost at the start, median: stated limit (b), not asserted.)
    const auto r = engine_turnover(40.0, 0.0, 52);
    ASSERT_EQ(r.log.born.size(), 1u);
    const auto id = r.log.born[0].id;
    const auto b = segment(r.log, id, r.b0, r.a0);
    EXPECT_LE(nospace_edits(b, kTurnoverB), 3u) << b;
    EXPECT_EQ(segment(r.log, id, r.a0), "PARIS PARIS PARIS PARIS") << r.log.text.at(id);
    EXPECT_NEAR(freq_before(r.log, id, r.a0), kTurnoverA + 40.0, 1.0);
}

TEST(Engine, TurnoverBeyondTheChannelDistanceGetsItsOwnTrack) {
    // B 100 Hz away, 6 dB weaker (re A's key-down power), has its own track, and A's channel ignores
    // it. Simulated (engine level, 30 numpy seeds): two channels in 30, B decoded by its own track in
    // 30 (its first 1.2 s lost to the detector's latency), A's next over exact in 30, no duplicate
    // (A's channel matched fewer than 6 of B's 12 characters, so at least 6 edits). One seed.
    const auto r = engine_turnover(100.0, -6.0, 53);
    ASSERT_EQ(r.log.born.size(), 2u);
    const auto a_id = r.log.born[0].id;
    const auto b_id = r.log.born[1].id;
    const auto b = segment(r.log, b_id, r.b0, r.a0);
    EXPECT_LE(nospace_edits(b, kTurnoverB), 3u) << b;
    EXPECT_GT(nospace_edits(segment(r.log, a_id, r.b0, r.a0), kTurnoverB), 3u);
    EXPECT_EQ(segment(r.log, a_id, r.a0), "PARIS PARIS PARIS PARIS") << r.log.text.at(a_id);
}

TEST(Engine, StrongerStation60HzAwayKeepsItsOwnTrack) {
    // B 60 Hz away (beyond D_ch), 6 dB stronger than A (re A's key-down power): the first design's
    // walk-and-merge case (B decoded 0 of 30 there). Simulated with option 1 (engine level, 30 numpy
    // seeds): two channels in 30, B decoded by its own track in 30, no merge, A's next over's last
    // three words intact in 30. Not asserted: A's next over exact (0 of 30; B leaks through A's
    // filter's sidelobe and corrupts A's first word: stated limit (a)). One seed.
    const auto r = engine_turnover(60.0, 6.0, 54);
    ASSERT_EQ(r.log.born.size(), 2u);
    const auto b = segment(r.log, r.log.born[1].id, r.b0, r.a0);
    EXPECT_LE(nospace_edits(b, kTurnoverB), 3u) << b;
    bool a_tail = false;
    for (const auto& t : r.log.born) a_tail = a_tail || ends_with(segment(r.log, t.id, r.a0), "PARIS PARIS PARIS");
    EXPECT_TRUE(a_tail) << r.log.text.at(r.log.born[0].id);
}

TEST(Engine, RejectsAnInvalidChannelDistance) {
    EventBus bus;
    auto c = matched_config();
    c.channel_distance_hz = 0.0;
    EXPECT_THROW(Engine(c, bus), std::invalid_argument);
}

TEST(Engine, ChannelTapReportsTheAnchorOfEachBlock) {
    // Oracle mode: the anchor is the labeled frequency itself. Detector mode: the detector's current frequency
    // for the track, which the Matched path gives the tracker; a clean station's is within 1 Hz of its carrier.
    const std::string msg = "CQ TEST K1ABC";
    const auto x = keyed_signal(msg, 25, 48000.0, kz4ap::test::duration_for(msg, 25), 12009.0, kAmplitude20dB48k,
                                kNoiseSigma, 42);
    for (const bool oracle : {true, false}) {
        auto config = matched_config(48000);
        if (oracle) config.oracle_frequencies_hz = {12009.0};
        EventBus bus;
        Engine engine(config, bus);
        std::vector<double> anchors;
        engine.set_channel_tap([&](const ChannelBlock& b) { anchors.push_back(b.anchor_hz); });
        engine.process(x);
        engine.finish();
        ASSERT_FALSE(anchors.empty());
        if (oracle) {
            for (const double a : anchors) EXPECT_DOUBLE_EQ(a, 12009.0);
        } else {
            EXPECT_NEAR(anchors.back(), 12009.0, 1.0);
        }
    }
}

// ---- The channelizer follows the detector: the new decoder's channels only (design Part 1; plan O2, O3) ----

namespace {

double frac(double cycles) { return cycles - std::floor(cycles); }

// Adds to x (48 kHz) a carrier of amplitude a, FS, present in [from_s, to_s), whose frequency is f0 + drift x t Hz from
// the span's center (t in s from the recording's first sample).
void add_carrier(std::vector<Sample>& x, double f0_hz, double drift_hz_per_s, double a, double from_s, double to_s) {
    const auto first = static_cast<std::size_t>(std::llround(from_s * 48000.0));
    const auto last = std::min(x.size(), static_cast<std::size_t>(std::llround(to_s * 48000.0)));
    for (std::size_t n = first; n < last; ++n) {
        const double t = static_cast<double>(n) / 48000.0;
        const double angle = 2.0 * std::numbers::pi * frac(f0_hz * t + 0.5 * drift_hz_per_s * t * t);
        x[n] += Sample(static_cast<float>(a * std::cos(angle)), static_cast<float>(a * std::sin(angle)));
    }
}

// duration_s of complex white noise of kNoiseSigma (DEV2's) at 48 kHz.
std::vector<Sample> noise_48k(double duration_s, unsigned seed) {
    return kz4ap::test::keyed_signal_of(std::vector<std::pair<double, double>>{}, 48000.0, duration_s, 0.0, 1.0,
                                        kNoiseSigma, seed);
}

// One channel's blocks as the engine's tap saw them.
struct TappedChannel {
    std::vector<Sample> samples;
    std::vector<std::uint64_t> first_index;  // per block
    std::vector<double> center_hz;           // per block
    std::vector<double> anchor_hz;           // per block
};

std::map<std::uint32_t, TappedChannel> tapped(const EngineConfig& config, const std::vector<Sample>& x) {
    EventBus bus;
    Engine engine(config, bus);
    std::map<std::uint32_t, TappedChannel> channels;
    engine.set_channel_tap([&](const ChannelBlock& b) {
        TappedChannel& c = channels[b.track_id];
        c.samples.insert(c.samples.end(), b.samples.begin(), b.samples.end());
        c.first_index.push_back(b.first_index);
        c.center_hz.push_back(b.center_hz);
        c.anchor_hz.push_back(b.anchor_hz);
    });
    for (std::size_t i = 0; i < x.size(); i += 65536)
        engine.process(std::span<const Sample>(x).subspan(i, std::min<std::size_t>(65536, x.size() - i)));
    engine.finish();
    return channels;
}

// Hz: the frequency of the carrier in samples [from, to) of a channel stream at 1 500 samples/s, from the phase of
// sum y[n] y*[n - L] with L = 375 samples (0.25 s; unambiguous within +-2 Hz), over 2 pi L / r.
double carrier_hz(const std::vector<Sample>& y, std::size_t from, std::size_t to) {
    constexpr std::size_t kLag = 375;
    std::complex<double> sum = 0.0;
    for (std::size_t n = from + kLag; n < to; ++n)
        sum += std::complex<double>(y[n]) * std::conj(std::complex<double>(y[n - kLag]));
    return std::arg(sum) * 1500.0 / (2.0 * std::numbers::pi * static_cast<double>(kLag));
}

// The new decoder at 48 kHz, its ceiling lowered to 100 stories to keep its CPU down: these tests read the channel.
EngineConfig following_config() {
    EngineConfig c;
    c.sample_rate = 48000;
    c.decoder.front_end = FrontEnd::Viterbi;
    c.viterbi.ceiling = 100;
    return c;
}

}  // namespace

TEST(Engine, OracleChannelsFollowTheLabelsDrift) {
    // A carrier at +30 dB S500 over DEV2's noise, 5 Hz above the center of bin 32 (755 Hz) at 0 s and drifting at
    // 1 Hz/s for 30 s, its oracle segment given (755 Hz at 0 s, 1 Hz/s from 0 s): every block's demodulating frequency
    // (the tap's center_hz, and its anchor) is the label's frequency at the block's start, and the tap's samples hold
    // the carrier within 0.05 Hz of 0 Hz in every 1 s window (the held frequency leaves 0 to 0.021 Hz; derived). The bin
    // moves once, at 16.75 s, without a trace in the windows.
    std::vector<Sample> x = noise_48k(30.0, 51);
    add_carrier(x, 755.0, 1.0, amplitude_48k(30.0), 0.0, 30.0);
    EngineConfig config = following_config();
    config.oracle_frequencies_hz = {755.0};
    config.oracle_frequency_tracks = {{OracleSegment{0.0, 755.0, 1.0, 0.0}}};
    const auto channels = tapped(config, x);
    ASSERT_EQ(channels.size(), 1u);
    const TappedChannel& c = channels.at(1);
    for (std::size_t k = 0; k < c.first_index.size(); ++k) {
        const double expected = 755.0 + 1.0 * static_cast<double>(c.first_index[k]) / 1500.0;
        ASSERT_NEAR(c.center_hz[k], expected, 1e-9) << "block " << k;
        ASSERT_EQ(c.anchor_hz[k], c.center_hz[k]) << "block " << k;
    }
    ASSERT_GE(c.samples.size(), 30u * 1500u);
    for (std::size_t w = 0; w < 30; ++w)
        EXPECT_NEAR(carrier_hz(c.samples, std::max<std::size_t>(32, w * 1500), (w + 1) * 1500), 0.0, 0.05)
            << "window " << w;
}

TEST(Engine, AnOracleQsoSwitchesAtEachOver) {
    // A QSO on one oracle channel: station A at 750 Hz until 5 s, then B at 775 Hz until 10 s, each a carrier at
    // +30 dB S500 over DEV2's noise; the segments 750 Hz from 0 s and 775 Hz from 5 s. The shift changes at the first
    // block starting at or after 5 s, the block of channel sample 7 520 (5.0133 s: 5 s x 1 500 samples/s = 7 500 is
    // not a multiple of 32), and B arrives within 0.05 Hz of 0 Hz from that block on (in 1 s windows from 0.1 s after
    // it, past the tail of the first station in the channel filter), as A did before.
    std::vector<Sample> x = noise_48k(10.0, 52);
    add_carrier(x, 750.0, 0.0, amplitude_48k(30.0), 0.0, 5.0);
    add_carrier(x, 775.0, 0.0, amplitude_48k(30.0), 5.0, 10.0);
    EngineConfig config = following_config();
    config.oracle_frequencies_hz = {750.0};
    config.oracle_frequency_tracks = {{OracleSegment{0.0, 750.0, 0.0, 0.0}, OracleSegment{5.0, 775.0, 0.0, 0.0}}};
    const TappedChannel c = tapped(config, x).at(1);
    ASSERT_GT(c.first_index.size(), 236u);
    ASSERT_EQ(c.first_index[235], 7520u);
    for (std::size_t k = 0; k < c.first_index.size(); ++k)
        ASSERT_EQ(c.center_hz[k], c.first_index[k] >= 7520 ? 775.0 : 750.0) << "block " << k;
    for (std::size_t from = 32; from + 1500 <= 7400; from += 1500)
        EXPECT_NEAR(carrier_hz(c.samples, from, from + 1500), 0.0, 0.05) << "A from sample " << from;
    for (std::size_t from = 7520 + 150; from < 15000; from += 1500)
        EXPECT_NEAR(carrier_hz(c.samples, from, std::min<std::size_t>(from + 1500, 15000)), 0.0, 0.05)
            << "B from sample " << from;
}

TEST(Engine, OtherDecodersChannelsAreUnchanged) {
    // Following is for the new decoder's channels only (plan O2). For Envelope, Matched and the bank in oracle mode, on
    // a 10 s recording of two stations (7.69 Hz above and 4.06 Hz below their bins' centers), the tap's blocks are
    // bit-identical to the fixed-bin channelizer's output, at the bins' centers. The new decoder's are not (followed).
    const double duration_s = 10.0;
    std::vector<Sample> x = noise_48k(duration_s, 53);
    add_to(x, keyed_signal("CQ TEST K1ABC", 25, 48000.0, duration_s, 1203.0, amplitude_48k(15.0), 0.0, 1, 0.5));
    add_to(x, keyed_signal("PARIS PARIS", 20, 48000.0, duration_s, -2501.0, amplitude_48k(15.0), 0.0, 1, 0.8));
    // The engine pads its last hop of 1 024 samples with zeros; the reference gets the same input.
    std::vector<Sample> padded = x;
    padded.resize((x.size() + 1023) / 1024 * 1024);
    ChannelizerConfig channel_config;
    channel_config.sample_rate = 48000;
    channel_config.fft_size = 2048;
    Channelizer reference(channel_config);
    const int bins[] = {reference.hz_to_bin(1203.0), reference.hz_to_bin(-2501.0)};
    reference.add_channel(1, bins[0]);
    reference.add_channel(2, bins[1]);
    std::map<std::uint32_t, std::vector<Sample>> fixed;
    reference.push(padded, [&](std::uint32_t id, std::uint64_t, std::span<const Sample> s) {
        fixed[id].insert(fixed[id].end(), s.begin(), s.end());
    });
    for (const FrontEnd front_end : {FrontEnd::Envelope, FrontEnd::Matched, FrontEnd::Bank}) {
        SCOPED_TRACE("decoder " + std::to_string(static_cast<int>(front_end)));
        EngineConfig config;
        config.sample_rate = 48000;
        config.decoder.front_end = front_end;
        config.oracle_frequencies_hz = {1203.0, -2501.0};
        const auto channels = tapped(config, x);
        for (const std::uint32_t id : {1u, 2u}) {
            const TappedChannel& c = channels.at(id);
            ASSERT_EQ(c.samples.size(), fixed.at(id).size()) << "track " << id;
            EXPECT_TRUE(c.samples == fixed.at(id)) << "track " << id;
            for (const double center : c.center_hz) ASSERT_EQ(center, reference.bin_to_hz(bins[id - 1]));
        }
    }
    EngineConfig followed = following_config();
    followed.oracle_frequencies_hz = {1203.0, -2501.0};
    const auto channels = tapped(followed, x);
    EXPECT_FALSE(channels.at(1).samples == fixed.at(1));
    EXPECT_EQ(channels.at(1).center_hz.front(), 1203.0);
}

TEST(Engine, RefusesOracleTracksItCannotUse) {
    // Oracle tracks belong to the new decoder's oracle channels: one per oracle frequency (or none), each empty (that
    // channel's constant frequency) or starting at or before 0 s, its segments in the order of their starts, every
    // value finite.
    EventBus bus;
    EngineConfig config = following_config();
    config.oracle_frequencies_hz = {750.0, 1500.0};
    config.oracle_frequency_tracks = {{OracleSegment{0.0, 750.0, 0.0, 0.0}}, {}};
    EXPECT_NO_THROW(Engine(config, bus));
    const auto refused = [&](std::vector<std::vector<OracleSegment>> tracks, FrontEnd front_end = FrontEnd::Viterbi) {
        EngineConfig c = config;
        c.decoder.front_end = front_end;
        c.oracle_frequency_tracks = std::move(tracks);
        EXPECT_THROW(Engine(c, bus), std::invalid_argument);
    };
    refused({{OracleSegment{0.0, 750.0, 0.0, 0.0}}});      // one track for two channels
    refused({{OracleSegment{0.5, 750.0, 0.0, 0.0}}, {}});  // starts after 0 s
    refused({{OracleSegment{0.0, 750.0, 0.0, 0.0}, OracleSegment{3.0, 760.0, 0.0, 0.0},
              OracleSegment{2.0, 770.0, 0.0, 0.0}},
             {}});  // out of order
    refused({{OracleSegment{0.0, std::numeric_limits<double>::quiet_NaN(), 0.0, 0.0}}, {}});
    refused({{OracleSegment{0.0, 750.0, std::numeric_limits<double>::infinity(), 0.0}}, {}});
    for (const FrontEnd other : {FrontEnd::Envelope, FrontEnd::Matched, FrontEnd::Bank})
        refused({{OracleSegment{0.0, 750.0, 0.0, 0.0}}, {}}, other);
}

namespace {

struct ReplayRun {
    std::map<std::uint32_t, std::string> text;  // by track, with corrections applied
    std::map<std::uint32_t, double> first_symbol_s;
    std::size_t tracks = 0;
    EngineStats stats;
};

ReplayRun run_segmental(const std::vector<Sample>& x, double replay_s, double replay_tracks_per_s = 100.0, int threads = 1) {
    EventBus bus;
    ReplayRun r;
    std::map<std::uint32_t, std::vector<std::string>> symbols;
    bus.subscribe([&](const Event& e) {
        if (const auto* t = std::get_if<TrackEvent>(&e); t && t->kind == TrackEvent::Kind::Born) ++r.tracks;
        if (const auto* d = std::get_if<DecodedTextEvent>(&e)) {
            std::vector<std::string>& list = symbols[d->track_id];
            for (const auto& c : d->chars) {
                if (!r.first_symbol_s.count(d->track_id)) r.first_symbol_s[d->track_id] = c.start_s;
                list.push_back(c.text);
            }
            for (const auto& k : d->corrections) {
                list.resize(std::min(k.from_index, list.size()));
                for (const auto& c : k.chars) list.push_back(c.text);
            }
        }
    });
    EngineConfig config;
    config.sample_rate = static_cast<int>(kRate);
    config.decoder.front_end = FrontEnd::Segmental;
    config.replay_s = replay_s;
    config.replay_tracks_per_s = replay_tracks_per_s;
    config.worker_threads = threads;
    Engine engine(config, bus);
    for (std::size_t i = 0; i < x.size(); i += 16384) engine.process(std::span<const Sample>(x).subspan(i, std::min<std::size_t>(16384, x.size() - i)));
    engine.finish();
    for (const auto& [id, list] : symbols)
        for (const std::string& s : list) r.text[id] += s;
    r.stats = engine.stats();
    return r;
}

std::string all_text(const ReplayRun& r) {
    std::string out;
    for (const auto& [id, text] : r.text) out += text + "|";
    return out;
}

// Stations on separate frequencies, all starting at start_s.
std::vector<Sample> stations_from(int count, double duration_s, double start_s) {
    std::vector<Sample> x = keyed_signal("VVV", 25, kRate, duration_s, 40000.0, 0.0, kNoiseSigma, 5, duration_s);
    for (int i = 0; i < count; ++i) {
        const std::vector<Sample> one = keyed_signal("CQ TEST DE W1AW K", 25, kRate, duration_s, -20000.0 + 400.0 * i, 3.0 * kAmplitude20dB, 0.0, 1, start_s);
        for (std::size_t k = 0; k < x.size(); ++k) x[k] += one[k];
    }
    return x;
}

}  // namespace

TEST(EngineReplay, ANewChannelReadsTheStartOfTheTransmission) {
    const std::vector<Sample> x = stations_from(1, 14.0, 5.0);
    const ReplayRun without = run_segmental(x, 0.0);
    const ReplayRun with = run_segmental(x, 2.0);
    ASSERT_EQ(with.tracks, 1u);
    ASSERT_EQ(without.tracks, 1u);
    // The first letters are in the time the detector takes to decide.
    EXPECT_EQ(all_text(without).find("CQ"), std::string::npos) << all_text(without);
    EXPECT_NE(all_text(with).find("CQ"), std::string::npos) << all_text(with);
    EXPECT_NE(all_text(with).find("W1AW"), std::string::npos) << all_text(with);
    // A symbol's time is when it was on the air, before the track's birth.
    EXPECT_LT(with.first_symbol_s.begin()->second, without.first_symbol_s.begin()->second);
    // Live time only in the channel-seconds, the replay's apart.
    EXPECT_NEAR(with.stats.channel_seconds, without.stats.channel_seconds, 0.05);
    EXPECT_NEAR(with.stats.replayed_channel_seconds, 2.0, 0.05);
    EXPECT_EQ(without.stats.replayed_channel_seconds, 0.0);
}

TEST(EngineReplay, AttheStreamsStartItReadsWhatThereIs) {
    const std::vector<Sample> x = stations_from(1, 10.0, 0.5);
    const ReplayRun with = run_segmental(x, 2.0);
    ASSERT_EQ(with.tracks, 1u);
    EXPECT_NE(all_text(with).find("CQ"), std::string::npos) << all_text(with);
    EXPECT_LE(with.stats.replayed_channel_seconds, 2.0 + 0.05);
}

TEST(EngineReplay, ManyTracksOpeningTogetherTakeTurnsAndLoseNothing) {
    const std::vector<Sample> x = stations_from(12, 14.0, 5.0);
    // At once, the budget far above the block's tracks.
    const ReplayRun at_once = run_segmental(x, 2.0, 1e6);
    EXPECT_EQ(at_once.stats.longest_replay_wait_s, 0.0);
    // One every 100 ms: the last of 12 waits about 1.1 s, within what the band has kept since the station began.
    const ReplayRun turns = run_segmental(x, 2.0, 10.0);
    ASSERT_EQ(turns.tracks, 12u);
    EXPECT_GT(turns.stats.longest_replay_wait_s, 0.9);
    EXPECT_LT(turns.stats.longest_replay_wait_s, 1.3);
    for (const auto& [id, text] : turns.text) EXPECT_NE(text.find("W1AW"), std::string::npos) << "track " << id << ": " << text;
    // The same, however many threads.
    const ReplayRun threads = run_segmental(x, 2.0, 10.0, 4);
    EXPECT_EQ(all_text(threads), all_text(turns));
}

TEST(EngineReplay, ThreadsAndChunksChangeNothing) {
    const std::vector<Sample> x = stations_from(3, 12.0, 5.0);
    EXPECT_EQ(all_text(run_segmental(x, 2.0, 100.0, 4)), all_text(run_segmental(x, 2.0, 100.0, 1)));
}

TEST(EngineReplay, RefusesValuesThatAreNotAMeasure) {
    EventBus bus;
    EngineConfig config;
    config.decoder.front_end = FrontEnd::Segmental;
    config.replay_s = -1.0;
    EXPECT_THROW(Engine(config, bus), std::invalid_argument);
    config.replay_s = 2.0;
    config.replay_tracks_per_s = 0.0;
    EXPECT_THROW(Engine(config, bus), std::invalid_argument);
}

TEST(EngineRates, ARateTheNewDecodersCannotUseSaysWhichTheyCan) {
    EventBus bus;
    EngineConfig config;
    config.decoder.front_end = FrontEnd::Segmental;
    config.sample_rate = 44100;
    try {
        Engine engine(config, bus);
        FAIL() << "not refused";
    } catch (const std::invalid_argument& e) {
        EXPECT_NE(std::string(e.what()).find("48, 96, 192, 384 or 768 kHz"), std::string::npos) << e.what();
    }
}
