// kz4ap-bench: runs the engine over an I/Q recording and scores the result.

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
};

constexpr const char* kUsage =
    "usage: kz4ap-bench RECORDING.wav [--labels LABELS.json] [--json OUT.json]\n"
    "                   [--no-timing] [--baseline BASELINE.json]\n";

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
        else if (!a.empty() && a[0] == '-') throw std::runtime_error("unknown option " + a + "\n" + kUsage);
        else if (args.recording.empty()) args.recording = a;
        else throw std::runtime_error(std::string("more than one recording given\n") + kUsage);
    }
    if (args.recording.empty()) throw std::runtime_error(kUsage);
    if (args.baseline && !args.labels) throw std::runtime_error("--baseline needs --labels");
    return args;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const Args args = parse_args(argc, argv);
        WavIqReader reader(args.recording);

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
        Engine engine(config, bus);
        std::vector<Sample> block(65536);
        const auto started = std::chrono::steady_clock::now();
        while (const auto n = reader.read(block)) engine.process(std::span<const Sample>(block).first(n));
        engine.finish();
        const double wall_s = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
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
        if (args.timing) {
            out["realtime_factor"] = duration_s / wall_s;
            std::printf("processed %.1f s of audio in %.2f s (%.1fx real time)\n", duration_s, wall_s,
                        duration_s / wall_s);
        }

        int exit_code = 0;
        if (args.labels) {
            const Labels labels = load_labels(*args.labels);
            if (labels.sample_rate != reader.sample_rate())
                throw std::runtime_error("labels sample rate does not match the recording");
            const Score s = score(labels.signals, track_list);
            const double recall = labels.signals.empty()
                                      ? 1.0
                                      : static_cast<double>(s.detected) / static_cast<double>(labels.signals.size());
            nlohmann::json signals = nlohmann::json::array();
            for (const auto& sig : s.signals) {
                signals.push_back({{"freq_offset_hz", sig.label.freq_offset_hz},
                                   {"reference", normalize_text(sig.label.text)},
                                   {"decoded", sig.decoded},
                                   {"track_id", sig.track_id ? nlohmann::json(*sig.track_id) : nlohmann::json()},
                                   {"cer", sig.cer}});
                std::printf("label %+10.1f Hz  CER %5.3f  %s\n", sig.label.freq_offset_hz, sig.cer,
                            sig.track_id ? "" : "(not detected)");
            }
            out["score"] = {{"cer", s.cer},
                            {"detected", s.detected},
                            {"labels", labels.signals.size()},
                            {"detection_recall", recall},
                            {"false_tracks", s.false_tracks},
                            {"signals", signals}};
            std::printf("CER %.4f, detected %zu of %zu, %zu false tracks\n", s.cer, s.detected,
                        labels.signals.size(), s.false_tracks);

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
