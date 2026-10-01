// kz4ap-bench: runs the engine over an I/Q recording and scores the result, or scores text decoded
// elsewhere (--score-decoded) against a labels file with the same scoring.

#include "channel_recorder.hpp"
#include "cpu_time.hpp"
#include "labels.hpp"
#include "report.hpp"
#include "scoring.hpp"

#include "kz4ap/engine.hpp"
#include "kz4ap/wav_reader.hpp"

#include <nlohmann/json.hpp>

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

using namespace kz4ap;
using namespace kz4ap::bench;

namespace {

struct Args {
    std::optional<std::filesystem::path> recording;
    std::optional<std::filesystem::path> labels;
    std::optional<std::filesystem::path> json;
    std::optional<std::filesystem::path> baseline;
    std::optional<std::filesystem::path> record_channels;
    std::optional<std::filesystem::path> score_decoded;
    bool timing = true;
    bool oracle = false;
    FrontEnd front_end = FrontEnd::Matched;
};

constexpr const char* kUsage =
    "usage: kz4ap-bench RECORDING.wav [--labels LABELS.json] [--json OUT.json]\n"
    "                   [--no-timing] [--baseline BASELINE.json] [--oracle] [--front-end envelope|matched]\n"
    "                   [--record-channels DIR (with --oracle: the oracle channels; without: the detector's)]\n"
    "       kz4ap-bench --labels LABELS.json --score-decoded DECODED.json [--json OUT.json] [--baseline BASELINE.json]\n";

Args parse_args(int argc, char** argv) {
    Args args;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto value = [&]() -> std::filesystem::path {
            if (i + 1 >= argc) throw std::runtime_error(a + " needs a value\n" + kUsage);
            return argv[++i];
        };
        if (a == "--labels") args.labels = value();
        else if (a == "--json") args.json = value();
        else if (a == "--baseline") args.baseline = value();
        else if (a == "--no-timing") args.timing = false;
        else if (a == "--oracle") args.oracle = true;
        else if (a == "--record-channels") args.record_channels = value();
        else if (a == "--score-decoded") args.score_decoded = value();
        else if (a == "--front-end") {
            const auto v = value().string();
            if (v == "envelope" || v == "baseline") args.front_end = FrontEnd::Envelope;
            else if (v == "matched") args.front_end = FrontEnd::Matched;
            else throw std::runtime_error("--front-end must be envelope or matched\n" + std::string(kUsage));
        }
        else if (!a.empty() && a[0] == '-') throw std::runtime_error("unknown option " + a + "\n" + kUsage);
        else if (!args.recording) args.recording = a;
        else throw std::runtime_error(std::string("more than one recording given\n") + kUsage);
    }
    if (args.baseline && !args.labels) throw std::runtime_error("--baseline needs --labels");
    if (args.score_decoded) {
        if (args.recording) throw std::runtime_error("--score-decoded takes no recording\n" + std::string(kUsage));
        if (!args.labels) throw std::runtime_error("--score-decoded needs --labels");
        if (args.oracle || args.record_channels) {
            throw std::runtime_error("--score-decoded cannot be combined with --oracle or --record-channels");
        }
        return args;
    }
    if (!args.recording) throw std::runtime_error(kUsage);
    if (args.oracle && !args.labels) throw std::runtime_error("--oracle needs --labels");
    return args;
}

// Scores, prints, fills out["score"] and checks the baseline; returns the exit code.
int score_and_report(const Labels& labels, const std::vector<DecodedTrack>& tracks,
                     const std::map<std::uint32_t, double>& tracked_freq_hz, bool match_by_order,
                     const std::optional<std::filesystem::path>& baseline, nlohmann::json& out) {
    const Score s = score(labels.signals, tracks, 50.0, match_by_order);
    out["score"] = score_json(labels, s, tracked_freq_hz);
    print_score(s);
    if (!baseline) return 0;
    std::ifstream f(*baseline);
    if (!f) throw std::runtime_error("cannot open " + baseline->string());
    const auto b = nlohmann::json::parse(f);
    const double max_cer = b.at("max_cer").get<double>();
    const double min_recall = b.at("min_detection_recall").get<double>();
    const double recall = detection_recall(s);
    if (s.cer > max_cer || recall < min_recall) {
        std::fprintf(stderr, "FAIL: CER %.4f (max %.4f), detection recall %.3f (min %.3f)\n", s.cer, max_cer, recall,
                     min_recall);
        return 1;
    }
    return 0;
}

void write_json(const std::optional<std::filesystem::path>& path, const nlohmann::json& out) {
    if (!path) return;
    std::ofstream f(*path);
    f << out.dump(2) << '\n';
    f.close();
    if (!f) throw std::runtime_error("cannot write " + path->string());
}

void add_tracks(nlohmann::json& out, const std::vector<DecodedTrack>& tracks) {
    out["tracks"] = nlohmann::json::array();
    for (const auto& t : tracks) {
        out["tracks"].push_back({{"id", t.id}, {"freq_hz", t.freq_hz}, {"text", normalize_text(t.text)}});
        std::printf("track %4u  %+10.1f Hz  %s\n", t.id, t.freq_hz, normalize_text(t.text).c_str());
    }
}

int run_engine(const Args& args) {
    WavIqReader reader(*args.recording);
    std::optional<Labels> labels;
    if (args.labels) {
        labels = load_labels(*args.labels);
        if (labels->sample_rate != reader.sample_rate())
            throw std::runtime_error("labels sample rate does not match the recording");
    }

    EventBus bus;
    std::map<std::uint32_t, DecodedTrack> tracks;
    std::optional<ChannelRecorder> recorder;
    bus.subscribe([&](const Event& e) {
        const auto* t = std::get_if<TrackEvent>(&e);
        if (t && t->kind == TrackEvent::Kind::Born) {
            tracks[t->track.id] = {t->track.id, t->track.freq_hz, "", t->track.freq_hz};
            if (recorder && !args.oracle) recorder->open_track(t->track.id, t->track.freq_hz);
        } else if (t && t->kind == TrackEvent::Kind::Died) {
            if (recorder) recorder->close_track(t->track.id);
        } else if (const auto* d = std::get_if<DecodedTextEvent>(&e)) {
            for (const auto& c : d->chars) tracks[d->track_id].text += c.text;
            tracks[d->track_id].last_freq_hz = d->freq_hz;
        }
    });

    EngineConfig config;
    config.sample_rate = reader.sample_rate();
    if (args.front_end == FrontEnd::Envelope) config = with_envelope_path(config);
    if (args.oracle) {
        for (const auto& s : labels->signals) config.oracle_frequencies_hz.push_back(s.freq_offset_hz);
    }
    Engine engine(config, bus);
    if (args.record_channels) {
        recorder.emplace(*args.record_channels, engine.channel_rate());
        if (args.oracle) {  // oracle channels were born in the constructor, before the recorder existed
            for (std::size_t i = 0; i < labels->signals.size(); ++i) {
                recorder->add_channel(static_cast<std::uint32_t>(i + 1), i, labels->signals[i].freq_offset_hz);
            }
        }
        engine.set_channel_tap([&](const ChannelBlock& b) { recorder->write(b); });
    }
    std::vector<Sample> block(65536);
    const double cpu_started = process_cpu_seconds();
    const auto started = std::chrono::steady_clock::now();
    while (const auto n = reader.read(block)) engine.process(std::span<const Sample>(block).first(n));
    engine.finish();
    if (recorder) {
        recorder->finish(args.recording->filename().string(),
                         args.labels ? args.labels->filename().string() : std::string());
    }
    const double wall_s = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    const double cpu_s = process_cpu_seconds() - cpu_started;
    const EngineStats stats = engine.stats();
    const double duration_s = static_cast<double>(reader.total_samples()) / reader.sample_rate();

    nlohmann::json out;
    out["recording"] = args.recording->filename().string();
    out["duration_s"] = duration_s;
    out["front_end"] = args.front_end == FrontEnd::Matched ? "matched" : "envelope";
    std::vector<DecodedTrack> track_list;
    std::map<std::uint32_t, double> tracked_freq_hz;
    for (const auto& [id, t] : tracks) {
        track_list.push_back(t);
        tracked_freq_hz[id] = t.last_freq_hz;
    }
    add_tracks(out, track_list);
    out["channel_seconds"] = stats.channel_seconds;
    if (args.timing) {
        const auto per_channel_ms = [&](double seconds) {
            return stats.channel_seconds > 0 ? 1000.0 * seconds / stats.channel_seconds : 0.0;
        };
        out["realtime_factor"] = duration_s / wall_s;
        out["timing"] = {{"wall_s", wall_s},
                         {"cpu_s", cpu_s},
                         {"cpu_ms_per_channel_s", per_channel_ms(cpu_s)},
                         {"decoder_ms_per_channel_s", per_channel_ms(stats.decoder_seconds)}};
        std::printf("processed %.1f s of audio in %.2f s (%.1fx real time); CPU %.2f ms per channel-second "
                    "(decoders %.3f ms)\n",
                    duration_s, wall_s, duration_s / wall_s, per_channel_ms(cpu_s), per_channel_ms(stats.decoder_seconds));
    }
    int exit_code = 0;
    if (labels) exit_code = score_and_report(*labels, track_list, tracked_freq_hz, args.oracle, args.baseline, out);
    write_json(args.json, out);
    return exit_code;
}

int score_decoded(const Args& args) {
    const Labels labels = load_labels(*args.labels);
    const DecodedTexts decoded = load_decoded_texts(*args.score_decoded);
    // Oracle texts are matched by order; detector tracks by frequency (50 Hz), false tracks counted: the
    // engine's own two rules, through the same score() and score_json().
    const auto tracks = decoded.detector ? decoded.tracks : tracks_for(labels, decoded);
    std::map<std::uint32_t, double> tracked_freq_hz;
    if (decoded.detector) {
        for (const auto& t : tracks) tracked_freq_hz[t.id] = t.last_freq_hz;
    }
    nlohmann::json out;
    out["recording"] = decoded.recording;
    out["duration_s"] = labels.duration_s;
    out["front_end"] = decoded.front_end;
    add_tracks(out, tracks);
    out["channel_seconds"] = 0.0;  // no engine ran
    const int exit_code = score_and_report(labels, tracks, tracked_freq_hz, !decoded.detector, args.baseline, out);
    write_json(args.json, out);
    return exit_code;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const Args args = parse_args(argc, argv);
        return args.score_decoded ? score_decoded(args) : run_engine(args);
    } catch (const std::exception& e) {
        std::cerr << "kz4ap-bench: " << e.what() << '\n';
        return 2;
    }
}
