#include "kz4ap/engine.hpp"

#include "test_signals.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <complex>
#include <limits>
#include <map>
#include <numbers>
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
