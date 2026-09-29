// kz4ap-bench: runs the engine over an I/Q recording and scores the result.

#include "cpu_time.hpp"
#include "labels.hpp"
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
    std::filesystem::path recording;
    std::optional<std::filesystem::path> labels;
    std::optional<std::filesystem::path> json;
    std::optional<std::filesystem::path> baseline;
    bool timing = true;
    bool oracle = false;
};

constexpr const char* kUsage =
    "usage: kz4ap-bench RECORDING.wav [--labels LABELS.json] [--json OUT.json]\n"
    "                   [--no-timing] [--baseline BASELINE.json] [--oracle]\n";

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
        else if (!a.empty() && a[0] == '-') throw std::runtime_error("unknown option " + a + "\n" + kUsage);
        else if (args.recording.empty()) args.recording = a;
        else throw std::runtime_error(std::string("more than one recording given\n") + kUsage);
    }
    if (args.recording.empty()) throw std::runtime_error(kUsage);
    if (args.baseline && !args.labels) throw std::runtime_error("--baseline needs --labels");
    if (args.oracle && !args.labels) throw std::runtime_error("--oracle needs --labels");
    return args;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const Args args = parse_args(argc, argv);
        WavIqReader reader(args.recording);

        std::optional<Labels> labels;
        if (args.labels) {
            labels = load_labels(*args.labels);
            if (labels->sample_rate != reader.sample_rate())
                throw std::runtime_error("labels sample rate does not match the recording");
        }

        EventBus bus;
        std::map<std::uint32_t, DecodedTrack> tracks;
        bus.subscribe([&](const Event& e) {
            if (const auto* t = std::get_if<TrackEvent>(&e); t && t->kind == TrackEvent::Kind::Born) {
                tracks[t->track.id] = {t->track.id, t->track.freq_hz, ""};
            } else if (const auto* d = std::get_if<DecodedTextEvent>(&e)) {
                for (const auto& c : d->chars) tracks[d->track_id].text += c.text;
            }
        });

        EngineConfig config;
        config.sample_rate = reader.sample_rate();
        if (args.oracle) {
            for (const auto& s : labels->signals) config.oracle_frequencies_hz.push_back(s.freq_offset_hz);
        }
        Engine engine(config, bus);
        std::vector<Sample> block(65536);
        const double cpu_started = process_cpu_seconds();
        const auto started = std::chrono::steady_clock::now();
        while (const auto n = reader.read(block)) engine.process(std::span<const Sample>(block).first(n));
        engine.finish();
        const double wall_s = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
        const double cpu_s = process_cpu_seconds() - cpu_started;
        const EngineStats stats = engine.stats();
        const double duration_s = static_cast<double>(reader.total_samples()) / reader.sample_rate();

        nlohmann::json out;
        out["recording"] = args.recording.filename().string();
        out["duration_s"] = duration_s;
        out["tracks"] = nlohmann::json::array();
        std::vector<DecodedTrack> track_list;
        for (const auto& [id, t] : tracks) {
            track_list.push_back(t);
            out["tracks"].push_back({{"id", id}, {"freq_hz", t.freq_hz}, {"text", normalize_text(t.text)}});
            std::printf("track %4u  %+10.1f Hz  %s\n", id, t.freq_hz, normalize_text(t.text).c_str());
        }
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
                        duration_s, wall_s, duration_s / wall_s, per_channel_ms(cpu_s),
                        per_channel_ms(stats.decoder_seconds));
        }

        int exit_code = 0;
        if (labels) {
            const Score s = score(labels->signals, track_list, 50.0, args.oracle);
            const double recall =
                s.scored == 0 ? 1.0 : static_cast<double>(s.detected) / static_cast<double>(s.scored);
            nlohmann::json signals = nlohmann::json::array();
            for (std::size_t i = 0; i < s.signals.size(); ++i) {
                const auto& sig = s.signals[i];
                nlohmann::json per_transmission = nlohmann::json::array();
                for (const auto& ts : sig.transmissions) {
                    per_transmission.push_back({{"symbols", ts.symbols}, {"edits", ts.edits},
                                                {"first_word_symbols", ts.first_word_symbols},
                                                {"first_word_edits", ts.first_word_edits}});
                }
                signals.push_back({{"index", i},
                                   {"freq_offset_hz", sig.label.freq_offset_hz},
                                   {"snr_db", sig.label.snr_db},
                                   {"wpm", sig.label.wpm},
                                   {"scored", sig.label.score},
                                   {"reference", normalize_text(sig.label.text)},
                                   {"decoded", sig.decoded},
                                   {"track_id", sig.track_id ? nlohmann::json(*sig.track_id) : nlohmann::json()},
                                   {"cer", sig.cer},
                                   {"symbols", sig.symbols},
                                   {"edits", sig.edits},
                                   {"chars", sig.chars},
                                   {"char_edits", sig.char_edits},
                                   {"spaces", sig.spaces},
                                   {"space_edits", sig.space_edits},
                                   {"first_word_symbols", sig.first_word_symbols},
                                   {"first_word_edits", sig.first_word_edits},
                                   {"nospace_symbols", sig.nospace_symbols},
                                   {"nospace_edits", sig.nospace_edits},
                                   {"transmissions", per_transmission}});
                std::printf("label %+10.1f Hz  CER %5.3f  %s%s\n", sig.label.freq_offset_hz, sig.cer,
                            sig.track_id ? "" : "(not detected)", sig.label.score ? "" : " (not scored)");
            }
            out["score"] = {{"cer", s.cer},
                            {"char_cer", s.char_cer},
                            {"space_error_rate", s.space_error_rate},
                            {"first_word_cer", s.first_word_cer},
                            {"nospace_cer", s.nospace_cer},
                            {"detected", s.detected},
                            {"labels", labels->signals.size()},
                            {"scored", s.scored},
                            {"detection_recall", recall},
                            {"false_tracks", s.false_tracks},
                            {"signals", signals}};
            std::printf("CER %.4f (characters %.4f, word spaces %.4f, first words %.4f, without spaces %.4f), "
                        "detected %zu of %zu, %zu false tracks\n",
                        s.cer, s.char_cer, s.space_error_rate, s.first_word_cer, s.nospace_cer, s.detected, s.scored,
                        s.false_tracks);

            if (args.baseline) {
                std::ifstream f(*args.baseline);
                if (!f) throw std::runtime_error("cannot open " + args.baseline->string());
                const auto b = nlohmann::json::parse(f);
                const double max_cer = b.at("max_cer").get<double>();
                const double min_recall = b.at("min_detection_recall").get<double>();
                if (s.cer > max_cer || recall < min_recall) {
                    std::fprintf(stderr, "FAIL: CER %.4f (max %.4f), detection recall %.3f (min %.3f)\n", s.cer,
                                 max_cer, recall, min_recall);
                    exit_code = 1;
                }
            }
        }
        if (args.json) {
            std::ofstream f(*args.json);
            f << out.dump(2) << '\n';
            f.close();
            if (!f) throw std::runtime_error("cannot write " + args.json->string());
        }
        return exit_code;
    } catch (const std::exception& e) {
        std::cerr << "kz4ap-bench: " << e.what() << '\n';
        return 2;
    }
}
