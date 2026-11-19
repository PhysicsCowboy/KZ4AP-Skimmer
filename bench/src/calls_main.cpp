// kz4ap-calls: the callsign stage over a recording, without the app: the list it reports, as CSV, and the stage's own CPU.
//   kz4ap-calls FILE.wav --out CALLS.csv [--decoder segmental] [--threads N] [--center-khz KHZ] [--mirror] [--known MASTER.SCP] [--log EVENTS.jsonl] [--candidates ALL.csv] [--readings ALL.csv] [--set NAME=VALUE]... [--dset NAME=VALUE]...
//   kz4ap-calls --replay EVENTS.jsonl --out CALLS.csv [--known MASTER.SCP] [--set NAME=VALUE]...
// --log keeps the decoder's events, so that --replay can try the stage's settings in seconds, on the same text.
// --dset sets the segmental decoder's settings, as kz4ap-bench's --set does; --set is the stage's.
// Receiver audio (a one-channel WAV, or --audio): [--audio] [--audio-channel N] [--passband-hz LO,HI] [--dial-khz KHZ] [--pitch-hz HZ]
// [--sideband usb|lsb] (audio_options.hpp); without --dial-khz the list's freq_hz are audio tone frequencies and freq_khz is empty.
#include "kz4ap/callsign.hpp"
#include "kz4ap/decoder_name.hpp"
#include "kz4ap/engine.hpp"
#include "kz4ap/event_bus.hpp"
#include "kz4ap/recording_reader.hpp"
#include "kz4ap/wav_reader.hpp"
#include "audio_options.hpp"
#include "viterbi_json.hpp"

#include <nlohmann/json.hpp>

#include <chrono>
#include <cmath>
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
    std::optional<double> center_hz;  // --center-khz, Hz: overrides the recording's header and file name
    bool mirror = false;              // --mirror: the receiver's spectrum is mirrored (RecordingOptions::mirror)
    int threads = 4;
    CallSearchConfig config;
    std::vector<std::string> decoder_settings;  // --dset
    std::vector<std::string> detector_settings;  // --detector NAME=VALUE (kz4ap::bench::apply_detector_setting)
    kz4ap::bench::AudioOptions audio;            // receiver audio (audio_options.hpp)
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
        {"stripped_points", &c.stripped_points}, {"joined_points", &c.joined_points}, {"points_named", &c.points_named},
        {"tiny_points", &c.tiny_points}, {"far_variant_ratio", &c.far_variant_ratio},
        {"prefix_held", &c.prefix_held}, {"withdraw_own_place", &c.withdraw_own_place},
        {"doubled_is_framed", &c.doubled_is_framed},
        {"known_rival_factor", &c.known_rival_factor},
        {"reread_corrected", &c.reread_corrected}, {"history_s", &c.history_s},
        {"named_listed_only", &c.named_listed_only}, {"short_variant_ratio", &c.short_variant_ratio},
        {"known_margin_nats", &c.known_margin_nats},
        {"pool_places", &c.pool_places}, {"glued_pieces_timed", &c.glued_pieces_timed},
        {"skip_neighbors_text", &c.skip_neighbors_text}, {"neighbor_twin_s", &c.neighbor_twin_s},
        {"segment_joined", &c.segment_joined}, {"segment_min_chars", &c.segment_min_chars},
        {"segment_alternatives", &c.segment_alternatives},
        {"unusual_listed_only", &c.unusual_listed_only}, {"joined_periodic", &c.joined_periodic},
        {"segment_stray_nats", &c.segmentation.stray_nats}, {"segment_call_nats", &c.segmentation.call_nats},
        {"segment_repeat_nats", &c.segmentation.repeat_nats}, {"segment_de_before_nats", &c.segmentation.de_before_nats},
        {"segment_de_after_nats", &c.segmentation.de_after_nats}, {"segment_word_min_nats", &c.segmentation.word_min_nats},
        {"segment_sign_off_nats", &c.segmentation.sign_off_nats}, {"segment_ambiguous_sign_off", &c.segmentation.ambiguous_sign_off}, {"segment_ambiguous_repeat", &c.segmentation.ambiguous_repeat}, {"segment_believe_nats", &c.segmentation.believe_nats},
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
        else if (a == "--center-khz") o.center_hz = 1000.0 * std::stod(next());
        else if (a == "--mirror") o.mirror = true;
        else if (a == "--set") set_value(o.config, next());
        else if (a == "--dset") o.decoder_settings.push_back(next());
        else if (a == "--detector") o.detector_settings.push_back(next());
        else if (kz4ap::bench::parse_audio_option(a, next, o.audio)) {}
        else if (!a.empty() && a[0] != '-' && o.wav.empty()) o.wav = a;
        else throw std::runtime_error("unknown argument: " + a);
    }
    if (o.wav.empty() == o.replay.empty()) throw std::runtime_error("give one recording, or --replay with a log");
    if (!o.replay.empty() && o.audio.given)
        throw std::runtime_error("--replay takes receiver audio's RF center from the log's \"input\" line: give no receiver-audio options");
    return o;
}

nlohmann::json symbols_json(const std::vector<DecodedSymbol>& symbols) {
    nlohmann::json a = nlohmann::json::array();
    // The probability only when below 1, so that a log of a decoder without margins reads as before; the neighbor whose keying a
    // symbol is (DecodedSymbol::heard_from) fifth, only when there is one.
    for (const DecodedSymbol& s : symbols) {
        if (s.heard_from != 0) a.push_back({s.text, s.start_s, s.end_s, s.probability, s.heard_from});
        else if (s.probability < 1.0f) a.push_back({s.text, s.start_s, s.end_s, s.probability});
        else a.push_back({s.text, s.start_s, s.end_s});
    }
    return a;
}

std::vector<DecodedSymbol> symbols_from(const nlohmann::json& a) {
    std::vector<DecodedSymbol> out;
    out.reserve(a.size());
    for (const auto& s : a)
        out.push_back({s[0].get<std::string>(), s.size() > 3 ? s[3].get<float>() : 1.0f, s[1].get<double>(), s[2].get<double>(),
                       s.size() > 4 ? s[4].get<std::uint32_t>() : 0u});
    return out;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const Options o = parse(argc, argv);
        std::shared_ptr<const KnownCalls> known;
        if (!o.known.empty()) {
            known = std::make_shared<const KnownCalls>(KnownCalls::read_file(o.known));
            std::fprintf(stderr, "known-call list: %zu calls from %s\n", known->size(), o.known.c_str());
        }
        CallSearch stage(o.config, known);
        std::ofstream readings;
        if (!o.readings.empty()) {
            readings.open(o.readings);
            readings << "call,how,role,doubled,start_s,end_s,freq_hz,margin_nats\n";
            stage.set_reading_log([&](const CallReading& r, double freq_hz) {
                static constexpr const char* kHow[] = {"whole", "glued", "stripped", "joined"};
                static constexpr const char* kRole[] = {"unknown", "sender", "named"};
                readings << r.full << ',' << kHow[static_cast<int>(r.reading)] << ',' << kRole[static_cast<int>(r.role)] << ',' << (r.doubled ? 1 : 0) << ','
                         << r.start_s << ',' << r.end_s << ',' << freq_hz << ',' << r.margin_nats << '\n';  // nats; -1: unknown
            });
        }
        double stage_s = 0.0, channel_seconds = 0.0, stream_s = 0.0;
        std::optional<double> center_hz = o.center_hz;
        std::optional<std::int64_t> start_unix_ms;
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
                } else if (kind == "input") {
                    // Receiver audio: the RF center its run used (dial and pitch), unless --center-khz overrides it.
                    if (!o.center_hz && j.contains("center_hz")) center_hz = j["center_hz"].get<double>();
                } else if (kind == "end") {
                    channel_seconds = j["channel_seconds"];
                }
            }
        } else {
            RecordingReader reader(o.wav, kz4ap::bench::recording_options(o.audio, o.mirror));
            kz4ap::bench::check_audio_options(o.audio, reader);
            if (reader.is_audio()) {
                // Receiver audio: no RF center in the file; with the dial frequency, RF = dial +/- (audio - pitch).
                if (o.center_hz) throw std::runtime_error("receiver audio: give the radio's dial frequency with --dial-khz, not --center-khz");
                if (o.mirror) throw std::runtime_error("receiver audio: give --sideband lsb, not --mirror");
                center_hz = kz4ap::bench::audio_center_hz(o.audio);
                std::fprintf(stderr, "%s\n", kz4ap::bench::describe_audio_input(reader, o.audio).c_str());
            } else {
                // As FileIqSource: the caller's center, else the header's, else the file name's.
                center_hz = o.center_hz ? o.center_hz : reader.info().center_hz;
                if (!center_hz) center_hz = center_hz_from_file_name(path_text(std::filesystem::path(o.wav).filename()), reader.file_rate());
            }
            if (reader.resampled())
                std::fprintf(stderr, "resampling %d to %d samples/s\n", reader.file_rate(), reader.sample_rate());
            start_unix_ms = reader.info().start_unix_ms;
            EngineConfig config = engine_config_for_decoder(o.decoder);
            config.sample_rate = reader.sample_rate();
            if (reader.is_audio()) kz4ap::bench::apply_audio_passband(config, o.audio);
            config.worker_threads = o.threads;
            for (const std::string& setting : o.decoder_settings) kz4ap::bench::apply_segmental_setting(config.segmental, setting);
            if (!o.detector_settings.empty()) {
                apply_path_defaults(config);
                for (const std::string& setting : o.detector_settings) kz4ap::bench::apply_detector_setting(config, setting);
                config.as_given = true;
            }
            std::unique_ptr<std::ofstream> log;
            if (!o.log.empty()) log = std::make_unique<std::ofstream>(o.log);
            if (log && reader.is_audio()) {
                // What the log's frequencies and levels mean for receiver audio; --replay ignores it.
                nlohmann::json j{{"k", "input"}, {"kind", "receiver audio"}, {"audio_rate", reader.file_rate()},
                                 {"passband_hz", {o.audio.passband_lo_hz, o.audio.passband_hi_hz}},
                                 {"sideband", o.audio.lower_sideband ? "lsb" : "usb"}, {"pitch_hz", o.audio.pitch_hz},
                                 {"note", kz4ap::bench::describe_audio_input(reader, o.audio)}};
                if (center_hz) j["center_hz"] = *center_hz;
                *log << j.dump() << '\n';
            }
            double next_mark = 0.0;
            double logged_second = -1.0;  // s: the whole second of the last frame logged
            EventBus bus;
            bus.subscribe([&](const Event& e) {
                if (const auto* f = std::get_if<SpectrumFrame>(&e)) {
                    stream_s = f->time_s;
                    // s: the log's clock, twice the stage's reading rate, and every frame that starts a whole second, where the stage
                    // reads (CallSearch::advance), so that --replay reads the same text at the same point of the events as the run did.
                    if (stream_s >= next_mark || std::floor(stream_s) > logged_second) {
                        if (stream_s >= next_mark) next_mark = stream_s + 0.5;
                        logged_second = std::floor(stream_s);
                        if (log) *log << nlohmann::json{{"k", "a"}, {"t", stream_s}}.dump() << '\n';
                    }
                    timed([&] { stage.advance(stream_s); });
                } else if (const auto* t = std::get_if<TrackEvent>(&e)) {
                    if (log) {
                        nlohmann::json j{{"k", t->kind == TrackEvent::Kind::Born ? "b" : "d"}, {"id", t->track.id}, {"f", t->track.freq_hz}, {"s", t->track.snr_db}};
                        if (t->kind == TrackEvent::Kind::Died) {
                            j["e"] = track_end_name(t->end);  // why it ended; --replay ignores it
                            j["p"] = t->proven ? 1 : 0;       // whether the track judge had proven it (TrackEvent::proven); likewise
                        }
                        *log << j.dump() << '\n';
                    }
                    timed([&] { stage.apply(*t); });
                } else if (const auto* d = std::get_if<DecodedTextEvent>(&e)) {
                    if (log) {
                        // "w": the event's speed, WPM (--replay ignores it).
                        nlohmann::json j{{"k", "t"}, {"id", d->track_id}, {"f", d->freq_hz}, {"w", d->wpm}, {"c", symbols_json(d->chars)}};
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
            if (log) {
                // Each track's decoder CPU, s (the thread's clock): --replay ignores it.
                nlohmann::json cpu = nlohmann::json::object();
                for (const auto& [id, s] : engine.track_decoder_seconds()) cpu[std::to_string(id)] = s;
                *log << nlohmann::json{{"k", "cpu"}, {"decoder_s", cpu}}.dump() << '\n';
                *log << nlohmann::json{{"k", "end"}, {"channel_seconds", channel_seconds}}.dump() << '\n';
            }
        }
        timed([&] { stage.finish(); });
        const std::string csv = calls_csv(stage.calls(), center_hz, start_unix_ms);
        if (!o.candidates.empty()) std::ofstream(o.candidates) << calls_csv(stage.candidates(), center_hz, start_unix_ms);
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
