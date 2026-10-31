// kz4ap-calls: the callsign stage over a recording, without the app: the list it reports, as CSV, and the stage's own CPU.
//   kz4ap-calls FILE.wav --out CALLS.csv [--decoder segmental] [--threads N] [--known MASTER.SCP] [--log EVENTS.jsonl] [--candidates ALL.csv] [--readings ALL.csv] [--set NAME=VALUE]...
//   kz4ap-calls --replay EVENTS.jsonl --out CALLS.csv [--known MASTER.SCP] [--set NAME=VALUE]...
// --log keeps the decoder's events, so that --replay can try the stage's settings in seconds, on the same text.
#include "kz4ap/callsign.hpp"
#include "kz4ap/decoder_name.hpp"
#include "kz4ap/engine.hpp"
#include "kz4ap/event_bus.hpp"
#include "kz4ap/wav_reader.hpp"

#include <nlohmann/json.hpp>

#include <chrono>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

using namespace kz4ap;
using namespace kz4ap::callsign;

namespace {

using Clock = std::chrono::steady_clock;

struct Options {
    std::string wav, replay, out, known, log, candidates, readings, decoder = "segmental";
    int threads = 4;
    CallSearchConfig config;
};

void set_value(CallSearchConfig& c, const std::string& assignment) {
    const auto eq = assignment.find('=');
    if (eq == std::string::npos) throw std::runtime_error("--set wants NAME=VALUE: " + assignment);
    const std::string name = assignment.substr(0, eq);
    const double value = std::stod(assignment.substr(eq + 1));
    const std::map<std::string, double*> fields = {
        {"settle_s", &c.settle_s},           {"same_place_hz", &c.same_place_hz},
        {"window_s", &c.window_s},           {"variant_hz", &c.variant_hz},
        {"variant_s", &c.variant_s},         {"points_framed", &c.points_framed},
        {"points_bare", &c.points_bare},     {"points_known", &c.points_known}, {"points_known_bare", &c.points_known_bare},
        {"stripped_points", &c.stripped_points}, {"far_variant_ratio", &c.far_variant_ratio},
        {"doubled_is_framed", &c.doubled_is_framed},
        {"known_rival_factor", &c.known_rival_factor},
    };
    const auto it = fields.find(name);
    if (it == fields.end()) throw std::runtime_error("no such setting: " + name);
    *it->second = value;
}

Options parse(int argc, char** argv) {
    Options o;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        const auto next = [&]() -> std::string {
            if (++i >= argc) throw std::runtime_error("missing value after " + a);
            return argv[i];
        };
        if (a == "--replay") o.replay = next();
        else if (a == "--out") o.out = next();
        else if (a == "--known") o.known = next();
        else if (a == "--log") o.log = next();
        else if (a == "--candidates") o.candidates = next();
        else if (a == "--readings") o.readings = next();
        else if (a == "--decoder") o.decoder = next();
        else if (a == "--threads") o.threads = std::stoi(next());
        else if (a == "--set") set_value(o.config, next());
        else if (!a.empty() && a[0] != '-' && o.wav.empty()) o.wav = a;
        else throw std::runtime_error("unknown argument: " + a);
    }
    if (o.wav.empty() == o.replay.empty()) throw std::runtime_error("give one recording, or --replay with a log");
    return o;
}

nlohmann::json symbols_json(const std::vector<DecodedSymbol>& symbols) {
    nlohmann::json a = nlohmann::json::array();
    for (const DecodedSymbol& s : symbols) a.push_back({s.text, s.start_s, s.end_s});
    return a;
}

std::vector<DecodedSymbol> symbols_from(const nlohmann::json& a) {
    std::vector<DecodedSymbol> out;
    out.reserve(a.size());
    for (const auto& s : a) out.push_back({s[0].get<std::string>(), 0.0f, s[1].get<double>(), s[2].get<double>()});
    return out;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const Options o = parse(argc, argv);
        std::shared_ptr<const KnownCalls> known;
        if (!o.known.empty()) known = std::make_shared<const KnownCalls>(KnownCalls::read_file(o.known));
        CallSearch stage(o.config, known);
        std::ofstream readings;
        if (!o.readings.empty()) {
            readings.open(o.readings);
            readings << "call,how,role,doubled,start_s,end_s,freq_hz\n";
            stage.set_reading_log([&](const CallReading& r, double freq_hz) {
                static constexpr const char* kHow[] = {"whole", "glued", "stripped", "joined"};
                static constexpr const char* kRole[] = {"unknown", "sender", "named"};
                readings << r.full << ',' << kHow[static_cast<int>(r.reading)] << ',' << kRole[static_cast<int>(r.role)] << ',' << (r.doubled ? 1 : 0) << ','
                         << r.start_s << ',' << r.end_s << ',' << freq_hz << '\n';
            });
        }
        double stage_s = 0.0, channel_seconds = 0.0, stream_s = 0.0;
        const auto timed = [&](auto&& work) {
            const auto t0 = Clock::now();
            work();
            stage_s += std::chrono::duration<double>(Clock::now() - t0).count();
        };
        if (!o.replay.empty()) {
            std::ifstream in(o.replay);
            if (!in) throw std::runtime_error("cannot read " + o.replay);
            std::string line;
            while (std::getline(in, line)) {
                const nlohmann::json j = nlohmann::json::parse(line);
                const std::string kind = j["k"];
                if (kind == "a") {
                    stream_s = j["t"];
                    timed([&] { stage.advance(stream_s); });
                } else if (kind == "b" || kind == "d") {
                    TrackEvent e{};
                    e.kind = kind == "b" ? TrackEvent::Kind::Born : TrackEvent::Kind::Died;
                    e.track.id = j["id"];
                    e.track.freq_hz = j["f"];
                    timed([&] { stage.apply(e); });
                } else if (kind == "t") {
                    DecodedTextEvent e{};
                    e.track_id = j["id"];
                    e.freq_hz = j["f"];
                    e.chars = symbols_from(j["c"]);
                    if (j.contains("x")) {
                        for (const auto& x : j["x"]) {
                            TextCorrection k;
                            k.from_index = x[0];
                            k.chars = symbols_from(x[1]);
                            e.corrections.push_back(std::move(k));
                        }
                    }
                    timed([&] { stage.apply(e); });
                } else if (kind == "end") {
                    channel_seconds = j["channel_seconds"];
                }
            }
        } else {
            WavIqReader reader(o.wav);
            EngineConfig config = engine_config_for_decoder(o.decoder);
            config.sample_rate = reader.sample_rate();
            config.worker_threads = o.threads;
            std::unique_ptr<std::ofstream> log;
            if (!o.log.empty()) log = std::make_unique<std::ofstream>(o.log);
            double next_mark = 0.0;
            EventBus bus;
            bus.subscribe([&](const Event& e) {
                if (const auto* f = std::get_if<SpectrumFrame>(&e)) {
                    stream_s = f->time_s;
                    if (stream_s >= next_mark) {
                        next_mark = stream_s + 0.5;  // s: the log's clock, twice the stage's reading rate
                        if (log) *log << nlohmann::json{{"k", "a"}, {"t", stream_s}}.dump() << '\n';
                    }
                    timed([&] { stage.advance(stream_s); });
                } else if (const auto* t = std::get_if<TrackEvent>(&e)) {
                    if (log) {
                        *log << nlohmann::json{{"k", t->kind == TrackEvent::Kind::Born ? "b" : "d"}, {"id", t->track.id}, {"f", t->track.freq_hz}}.dump() << '\n';
                    }
                    timed([&] { stage.apply(*t); });
                } else if (const auto* d = std::get_if<DecodedTextEvent>(&e)) {
                    if (log) {
                        nlohmann::json j{{"k", "t"}, {"id", d->track_id}, {"f", d->freq_hz}, {"c", symbols_json(d->chars)}};
                        if (!d->corrections.empty()) {
                            nlohmann::json x = nlohmann::json::array();
                            for (const TextCorrection& k : d->corrections) x.push_back({k.from_index, symbols_json(k.chars)});
                            j["x"] = x;
                        }
                        *log << j.dump() << '\n';
                    }
                    timed([&] { stage.apply(*d); });
                }
            });
            Engine engine(config, bus);
            std::vector<Sample> block(65536);
            for (;;) {
                const std::size_t n = reader.read(block);
                if (n == 0) break;
                engine.process(std::span<const Sample>(block).first(n));
            }
            engine.finish();
            channel_seconds = engine.stats().channel_seconds;
            if (log) *log << nlohmann::json{{"k", "end"}, {"channel_seconds", channel_seconds}}.dump() << '\n';
        }
        timed([&] { stage.finish(); });
        const std::string csv = calls_csv(stage.calls());
        if (!o.candidates.empty()) std::ofstream(o.candidates) << calls_csv(stage.candidates());
        if (o.out.empty()) {
            std::cout << csv;
        } else {
            std::ofstream(o.out) << csv;
        }
        std::fprintf(stderr, "calls %zu, withdrawn %llu; stream %.1f s, channel-seconds %.0f; the stage's CPU %.3f s wall, %.4f ms per channel-second\n",
                     stage.calls().size(), static_cast<unsigned long long>(stage.withdrawn()), stream_s, channel_seconds, stage_s,
                     channel_seconds > 0 ? 1000.0 * stage_s / channel_seconds : 0.0);
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "kz4ap-calls: %s\n", e.what());
        return 1;
    }
}
