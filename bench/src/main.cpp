// kz4ap-bench: runs the engine over an I/Q recording and scores the result, or scores text decoded
// elsewhere (--score-decoded) against a labels file with the same scoring.

#include "channel_recorder.hpp"
#include "cpu_time.hpp"
#include "labels.hpp"
#include "report.hpp"
#include "scoring.hpp"
#include "audio_options.hpp"
#include "viterbi_json.hpp"

#include "kz4ap/engine.hpp"
#include "kz4ap/recording_reader.hpp"
#include "kz4ap/wav_reader.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
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
    bool judge_tracks = true;
    std::optional<std::filesystem::path> recording;
    std::optional<std::filesystem::path> labels;
    std::optional<std::filesystem::path> json;
    std::optional<std::filesystem::path> baseline;
    std::optional<std::filesystem::path> record_channels;
    std::optional<std::filesystem::path> score_decoded;
    bool timing = true;
    bool symbol_times = false;  // --symbol-times: each track's final characters with their start and end, s
    bool oracle = false;
    FrontEnd front_end = FrontEnd::Matched;  // the decoder (--decoder; --front-end is its old name, kept as an alias)
    // The new decoder (--decoder viterbi): its --set and --genie options as given, and its configuration with the
    // settings applied (each is applied as it is read, so that an unknown name or a wrong kind of value fails before
    // anything else is done).
    ViterbiOptions viterbi_options;
    viterbi::ViterbiConfig viterbi;
    // The segmental decoder (--decoder segmental): its configuration with the --set values applied, read after the
    // options, since which decoder a --set belongs to is known only then.
    segmental::SegmentalConfig segmental;
    std::vector<std::string> settings;
    std::vector<std::string> detector_settings;  // --detector NAME=VALUE: a variant of the detector (apply_detector_setting)
    std::optional<std::filesystem::path> stats;  // --stats: where the new decoder's statistics file goes
    int threads = 1;  // --threads: EngineConfig::worker_threads
    AudioOptions audio;  // receiver audio (audio_options.hpp)
    double label_shift_hz = 0.0;  // --label-shift-hz: added to every label's frequency, Hz (audio made from a labeled I/Q recording)
    std::optional<std::filesystem::path> impulse_csv;  // --impulse-csv: the impulse blanker's nonzero readings, a row each
};

const char* decoder_name(FrontEnd d) {
    switch (d) {
        case FrontEnd::Envelope: return "envelope";
        case FrontEnd::Matched: return "matched";
        case FrontEnd::Bank: return "bank";
        case FrontEnd::Viterbi: return "viterbi";
        case FrontEnd::Segmental: return "segmental";
    }
    return "?";
}

constexpr const char* kUsage =
    "usage: kz4ap-bench RECORDING.wav [--labels LABELS.json] [--json OUT.json]\n"
    "                   [--no-timing] [--baseline BASELINE.json] [--oracle]\n"
    "                   [--decoder envelope|matched|bank|viterbi|segmental] [--no-judge]\n"
    "                   [--record-channels DIR (with --oracle: the oracle channels; without: the detector's)]\n"
    "                   [--set NAME=VALUE ...] [--genie fist|fading ...] [--stats STATS.json [--stats-detail]]\n"
    "                   [--detector NAME=VALUE ...] (a field of DetectorConfig or dc_block_s, over the path's defaults)\n"
    "                   [--impulse-csv FILE] (with the impulse blanker on, impulse_mode=1, the default: its readings)\n"
    "                   [--omega-bar-default-table FILE]\n"
    "                   [--audio] [--audio-channel N] [--passband-hz LO,HI] [--sideband usb|lsb] [--label-shift-hz HZ]\n"
    "                   (receiver audio, as a one-channel WAV is read: docs/signal-processing.md, section 1;\n"
    "                   --label-shift-hz adds HZ to every label's frequency, for audio made from a labeled I/Q recording)\n"
    "                   (--decoder viterbi, whose oracle channels follow the labels' frequencies;\n"
    "                   NAME is a field of ViterbiConfig; --genie reads the labels;\n"
    "                   --genie and --stats need --oracle; --stats writes the decoder's search statistics: a summary\n"
    "                   for each channel, or with --stats-detail every newborn line, which is 45 kB per channel-second;\n"
    "                   --omega-bar-default-table: the default Omega_bar table for a level_min_db and level_max_db\n"
    "                   other than -15 and +85 dB S500, from kz4ap-viterbi-tables omega-bar --lo-db L --hi-db H)\n"
    "       kz4ap-bench --labels LABELS.json --score-decoded DECODED.json [--json OUT.json] [--baseline BASELINE.json]\n";

// Whether two paths name the same file, which need not exist yet: the file itself where both exist (another spelling, a
// link, on Windows another letter case), else the paths made absolute with `.`, `..` and links resolved as far as they
// exist. False when either cannot be resolved.
bool same_file(const std::filesystem::path& a, const std::filesystem::path& b) {
    std::error_code error;
    if (std::filesystem::exists(a, error) && std::filesystem::exists(b, error)) {
        const bool same = std::filesystem::equivalent(a, b, error);
        if (!error) return same;
    }
    std::error_code absolute_a, absolute_b, resolved_a, resolved_b;
    const auto full_a = std::filesystem::absolute(a, absolute_a);
    const auto full_b = std::filesystem::absolute(b, absolute_b);
    if (absolute_a || absolute_b) return false;
    const auto canonical_a = std::filesystem::weakly_canonical(full_a, resolved_a);
    const auto canonical_b = std::filesystem::weakly_canonical(full_b, resolved_b);
    return !resolved_a && !resolved_b && canonical_a == canonical_b;
}

Args parse_args(int argc, char** argv) {
    Args args;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto text = [&]() -> std::string {
            if (i + 1 >= argc) throw std::runtime_error(a + " needs a value\n" + kUsage);
            return argv[++i];
        };
        auto value = [&]() -> std::filesystem::path { return text(); };
        if (a == "--labels") args.labels = value();
        else if (a == "--json") args.json = value();
        else if (a == "--baseline") args.baseline = value();
        else if (a == "--no-timing") args.timing = false;
        else if (a == "--symbol-times") args.symbol_times = true;
        else if (a == "--oracle") args.oracle = true;
        else if (a == "--threads") {
            args.threads = std::stoi(text());
            if (args.threads < 1) throw std::runtime_error("--threads must be at least 1");
        }
        else if (a == "--record-channels") args.record_channels = value();
        else if (a == "--score-decoded") args.score_decoded = value();
        else if (a == "--decoder" || a == "--front-end") {  // --front-end: the old name, kept for existing scripts
            const auto v = value().string();
            if (v == "envelope" || v == "baseline") args.front_end = FrontEnd::Envelope;
            else if (v == "matched") args.front_end = FrontEnd::Matched;
            else if (v == "bank") args.front_end = FrontEnd::Bank;
            else if (v == "viterbi") args.front_end = FrontEnd::Viterbi;
            else if (v == "segmental") args.front_end = FrontEnd::Segmental;
            else throw std::runtime_error(a + " must be envelope, matched, bank, viterbi or segmental\n" + std::string(kUsage));
        }
        else if (a == "--detector") args.detector_settings.push_back(text());  // NAME=VALUE, repeatable
        else if (a == "--set") {  // the new decoder's configuration: NAME=VALUE, repeatable
            args.settings.push_back(text());
        }
        else if (a == "--genie") {  // the new decoder's genie, from the labels: repeatable, each kind once
            const GenieKind kind = parse_genie_kind(text());
            auto& kinds = args.viterbi_options.genie;
            if (std::find(kinds.begin(), kinds.end(), kind) == kinds.end()) kinds.push_back(kind);
        }
        else if (a == "--stats") {  // the new decoder's search statistics, written when the run ends
            args.stats = value();
            args.viterbi_options.stats = true;
        }
        else if (parse_audio_option(a, text, args.audio)) {}
        else if (a == "--label-shift-hz") {
            const std::string v = text();
            std::size_t used = 0;
            try {
                args.label_shift_hz = std::stod(v, &used);
            } catch (const std::exception&) {
                used = 0;
            }
            if (used == 0 || used != v.size() || !std::isfinite(args.label_shift_hz))
                throw std::runtime_error("--label-shift-hz wants a number of Hz: " + v);
        }
        else if (a == "--impulse-csv") args.impulse_csv = value();
        else if (a == "--no-judge") args.judge_tracks = false;  // every track stays as the detector has it (track_evidence.hpp)
        else if (a == "--stats-detail") args.viterbi_options.stats_detail = true;  // the full form of that file
        else if (a == "--omega-bar-default-table") {  // the default Omega_bar table for other level bounds
            args.viterbi_options.omega_bar_default_table = text();
            if (args.viterbi_options.omega_bar_default_table.empty())
                throw std::runtime_error("--omega-bar-default-table needs a file\n" + std::string(kUsage));
        }
        else if (!a.empty() && a[0] == '-') throw std::runtime_error("unknown option " + a + "\n" + kUsage);
        else if (!args.recording) args.recording = a;
        else throw std::runtime_error(std::string("more than one recording given\n") + kUsage);
    }
    // std::invalid_argument for an unknown name or a wrong kind of value
    for (const std::string& setting : args.settings) {
        if (args.front_end == FrontEnd::Segmental) {
            apply_segmental_setting(args.segmental, setting);
        } else {
            apply_setting(args.viterbi, setting);
            args.viterbi_options.settings.push_back(setting);
        }
    }
    check_viterbi_options(args.front_end == FrontEnd::Viterbi, args.oracle, args.viterbi_options);
    // The statistics are written after the result so that a statistics file that fails loses nothing of the run; one
    // path for both would let the statistics replace the result.
    if (args.json && args.stats && same_file(*args.json, *args.stats))
        throw std::runtime_error("--json and --stats name the same file (" + args.json->string() +
                                 "): the statistics would replace the result");
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
    if (args.audio.rf_given)
        throw std::runtime_error("kz4ap-bench reports no RF: --dial-khz and --pitch-hz are kz4ap-calls' (receiver audio's frequencies here are audio Hz)");
    if (args.oracle && !args.labels) throw std::runtime_error("--oracle needs --labels");
    return args;
}

// Scores, prints, fills out["score"] and checks the baseline (on the final text); returns the exit code.
// immediate: the same tracks with their immediate text (the engine's runs), scored beside the final text.
int score_and_report(const Labels& labels, const std::vector<DecodedTrack>& tracks,
                     const std::map<std::uint32_t, double>& tracked_freq_hz, bool match_by_order,
                     const std::optional<std::filesystem::path>& baseline, nlohmann::json& out,
                     const std::vector<DecodedTrack>* immediate = nullptr) {
    const Score s = score(labels.signals, tracks, 50.0, match_by_order);
    out["score"] = score_json(labels, s, tracked_freq_hz);
    if (immediate) {
        const Score si = score(labels.signals, *immediate, 50.0, match_by_order);
        add_immediate_score(out["score"], si);
        std::printf("immediate text (corrections ignored): CER %.4f\n", si.cer);
    }
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

// Whether `path` can be created, found out before a run that takes minutes to hours and not after it: creates and
// removes the file that the writers use first (path plus ".partial", renamed to path when the file is complete).
void require_writable(const std::filesystem::path& path) {
    const std::filesystem::path partial = path.string() + ".partial";
    {
        std::ofstream probe(partial);
        if (!probe) throw std::runtime_error("cannot write " + partial.string() + " (the folder must exist)");
    }
    std::error_code ignored;
    std::filesystem::remove(partial, ignored);
}

// The result file, written under a name of its own and renamed when complete, so that no failure leaves a file that
// looks complete (a result file costs a run). A file that could not be written is removed; a file that was written and
// only could not be renamed (the destination is a folder or a read-only file, or on Windows is held open by another
// program) is whole, so it stays as `path` plus ".partial" and the message says so, for a person to rename.
void write_json(const std::optional<std::filesystem::path>& path, const nlohmann::json& out) {
    if (!path) return;
    const std::filesystem::path partial = path->string() + ".partial";
    {
        std::ofstream f(partial);
        f << out.dump(2) << '\n';
        f.close();
        if (!f) {
            std::error_code ignored;
            std::filesystem::remove(partial, ignored);
            throw std::runtime_error("cannot write " + path->string());
        }
    }
    std::error_code error;
    std::filesystem::rename(partial, *path, error);
    if (error)
        throw std::runtime_error("cannot write " + path->string() + ": the finished file is kept as " +
                                 partial.string() + ", because renaming it failed: " + error.message());
}

// "text" is the final text; "text_immediate" (when immediate is given, one per track in the same order) the text as
// first published, corrections ignored.
// "corrections" (when corrections is given: the bank decoder) the track's corrections, corrections_json's form.
void add_tracks(nlohmann::json& out, const std::vector<DecodedTrack>& tracks,
                const std::vector<DecodedTrack>* immediate = nullptr,
                const std::map<std::uint32_t, TrackText>* corrections = nullptr) {
    out["tracks"] = nlohmann::json::array();
    for (std::size_t i = 0; i < tracks.size(); ++i) {
        const auto& t = tracks[i];
        nlohmann::json j = {{"id", t.id}, {"freq_hz", t.freq_hz}, {"text", normalize_text(t.text)}};
        if (immediate) j["text_immediate"] = normalize_text((*immediate)[i].text);
        if (corrections) {
            const auto it = corrections->find(t.id);
            j["corrections"] = corrections_json(it != corrections->end() ? it->second.corrections()
                                                                          : std::vector<TrackText::Received>{});
        }
        out["tracks"].push_back(std::move(j));
        std::printf("track %4u  %+10.1f Hz  %s\n", t.id, t.freq_hz, normalize_text(t.text).c_str());
    }
}

int run_engine(const Args& args) {
    // I/Q at the file's own rate (no resampling: the labels' rate must match), or receiver audio as its analytic signal.
    RecordingOptions reading = recording_options(args.audio, false);
    reading.resample = false;
    RecordingReader reader(*args.recording, reading);
    check_audio_options(args.audio, reader);
    if (reader.is_audio()) std::fprintf(stderr, "%s\n", describe_audio_input(reader, args.audio).c_str());
    std::optional<Labels> labels;
    if (args.labels) {
        labels = load_labels(*args.labels);
        // Receiver audio made from a labeled I/Q recording keeps the labels' times, not their rate.
        if (!reader.is_audio() && labels->sample_rate != reader.sample_rate())
            throw std::runtime_error("labels sample rate does not match the recording");
        for (LabeledSignal& s : labels->signals) {
            s.freq_offset_hz += args.label_shift_hz;
            if (reader.is_audio() && args.audio.lower_sideband) {
                // The lower sideband reads a tone at f at -f (audio_options.hpp): so are the labels' frequencies, offsets and drifts.
                s.freq_offset_hz = -s.freq_offset_hz;
                s.drift_hz_per_s = -s.drift_hz_per_s;
                for (auto& tx : s.transmissions) tx.offset_hz = -tx.offset_hz;
            }
        }
    }

    EventBus bus;
    std::map<std::uint32_t, DecodedTrack> tracks;  // text filled in at the end, from texts
    std::map<std::uint32_t, TrackText> texts;
    std::optional<ChannelRecorder> recorder;
    // When each track was born and died, s of stream, and the tracks alive at each whole second of stream.
    std::map<std::uint32_t, double> born_s, died_s, wpm;
    std::map<std::uint32_t, TrackEnd> died_end;
    std::vector<std::size_t> live_each_s;
    std::size_t live_now = 0;
    double stream_now_s = 0.0;
    bus.subscribe([&](const Event& e) {
        if (const auto* frame = std::get_if<SpectrumFrame>(&e)) {
            stream_now_s = frame->time_s;
            while (static_cast<double>(live_each_s.size()) < stream_now_s) live_each_s.push_back(live_now);
            return;
        }
        const auto* t = std::get_if<TrackEvent>(&e);
        if (t && t->kind == TrackEvent::Kind::Born) {
            born_s[t->track.id] = stream_now_s;
            ++live_now;
            tracks[t->track.id] = {t->track.id, t->track.freq_hz, "", t->track.freq_hz};
            if (recorder && !args.oracle) recorder->open_track(t->track.id, t->track.freq_hz);
        } else if (t && t->kind == TrackEvent::Kind::Died) {
            died_s[t->track.id] = stream_now_s;
            died_end[t->track.id] = t->end;
            --live_now;
            if (recorder) recorder->close_track(t->track.id);
        } else if (const auto* d = std::get_if<DecodedTextEvent>(&e)) {
            texts[d->track_id].apply(d->chars, d->corrections);
            tracks[d->track_id].last_freq_hz = d->freq_hz;
            if (d->wpm > 0) wpm[d->track_id] = d->wpm;
        }
    });

    EngineConfig config;
    config.sample_rate = reader.sample_rate();
    if (reader.is_audio()) apply_audio_passband(config, args.audio);
    if (args.front_end == FrontEnd::Envelope) config = with_envelope_path(config);
    config.decoder.front_end = args.front_end;
    config.worker_threads = args.threads;
    config.judge_tracks = args.judge_tracks;
    if (args.oracle) {
        for (const auto& s : labels->signals) config.oracle_frequencies_hz.push_back(s.freq_offset_hz);
    }
    if (!args.detector_settings.empty()) {
        apply_path_defaults(config);
        for (const std::string& setting : args.detector_settings) apply_detector_setting(config, setting);
        config.as_given = true;
    }
    if (args.front_end == FrontEnd::Segmental) {
        config.segmental = args.segmental;
        if (args.oracle) {
            for (std::size_t i = 0; i < labels->signals.size(); ++i)
                config.oracle_frequency_tracks.push_back(oracle_frequency_track(labels->signals[i], i));
        }
    }
    if (args.front_end == FrontEnd::Viterbi) {
        config.viterbi = args.viterbi;
        if (args.stats) config.viterbi.stats = true;  // the search keeps its statistics only when asked
        // A default Omega_bar table for level bounds other than the compiled table's (make_viterbi_shared)
        config.viterbi_omega_bar_default_table = args.viterbi_options.omega_bar_default_table;
        // Each oracle channel follows its label's true frequency, drifting or switching at a QSO's overs (design Part 1;
        // plan O3), in the order of the labels (EngineConfig::oracle_frequency_tracks).
        if (args.oracle) {
            for (std::size_t i = 0; i < labels->signals.size(); ++i)
                config.oracle_frequency_tracks.push_back(oracle_frequency_track(labels->signals[i], i));
        }
        // --genie: each oracle channel gets the truth of its station from its label, in the order of the labels, which
        // is the order of oracle_frequencies_hz and of the channels' track ids (EngineConfig::viterbi_genie)
        if (!args.viterbi_options.genie.empty()) {
            for (std::size_t i = 0; i < labels->signals.size(); ++i)
                config.viterbi_genie.push_back(
                    genie_from_label(labels->signals[i], i, args.viterbi_options.genie, config.viterbi.sample_interval_s));
        }
        // With oracle channels the engine builds their decoders now and refuses what the library cannot use; on the
        // detector path no decoder exists until a track is born, so the configuration is tried first.
        if (!args.oracle) check_engine_accepts(config);
    }
    // The run takes minutes to hours: a file it cannot write is found now, not when the run is over.
    if (args.json) require_writable(*args.json);
    if (args.stats) require_writable(*args.stats);
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
    // --impulse-csv: each channel sample with a pulse, as the blanker read it (its series keeps a few s; a block here is 0.34 s).
    std::optional<std::ofstream> impulse_csv;
    std::uint64_t impulse_written = 0;
    if (args.impulse_csv) {
        if (!engine.impulse_blanker()) throw std::runtime_error("--impulse-csv needs the impulse blanker on (--detector impulse_mode=1, the default)");
        impulse_csv.emplace(*args.impulse_csv);
        if (!*impulse_csv) throw std::runtime_error("cannot write " + args.impulse_csv->string());
        *impulse_csv << "channel_sample,time_s,excess\n";
    }
    const auto write_impulses = [&] {
        if (!impulse_csv) return;
        const ImpulseSeries& s = engine.impulse_blanker()->series();
        for (std::uint64_t c = std::max(impulse_written, s.first); c < s.end(); ++c) {
            const double u = s.at(static_cast<std::int64_t>(c));
            if (u > 0.0) *impulse_csv << c << ',' << static_cast<double>(c) / engine.channel_rate() << ',' << u << '\n';
        }
        impulse_written = std::max(impulse_written, s.end());
    };
    const double cpu_started = process_cpu_seconds();
    const auto started = std::chrono::steady_clock::now();
    double source_s = 0.0;  // CPU reading the recording
    for (;;) {
        const double read_started = thread_cpu_precise_seconds();
        const auto n = reader.read(block);
        source_s += thread_cpu_precise_seconds() - read_started;
        if (n == 0) break;
        engine.process(std::span<const Sample>(block).first(n));
        write_impulses();
    }
    engine.finish();
    write_impulses();
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
    if (reader.is_audio()) {
        // Receiver audio: its frequencies audio tones (Hz; negated on the lower sideband) and its SNRs receiver-audio values.
        out["input"] = {{"kind", "receiver audio"}, {"audio_rate", reader.file_rate()}, {"channel", reader.audio_channel()},
                        {"passband_hz", {args.audio.passband_lo_hz, args.audio.passband_hi_hz}},
                        {"sideband", args.audio.lower_sideband ? "lsb" : "usb"}, {"label_shift_hz", args.label_shift_hz}};
    }
    out["duration_s"] = duration_s;
    out["front_end"] = decoder_name(args.front_end);  // the old key, read by stage 1's tooling
    out["decoder"] = decoder_name(args.front_end);
    if (args.front_end == FrontEnd::Segmental) out["settings"] = args.settings;
    if (!args.detector_settings.empty()) out["detector_settings"] = args.detector_settings;
    if (args.front_end == FrontEnd::Viterbi) {
        // everything a run needs to be repeated from its result file: the configuration with every field, the --set
        // arguments as given, and the genie's kinds with each channel's values (the labels give them again)
        out["config"] = nlohmann::json(to_json(config.viterbi));
        out["settings"] = args.viterbi_options.settings;
        if (!args.viterbi_options.omega_bar_default_table.empty())
            out["omega_bar_default_table"] = args.viterbi_options.omega_bar_default_table;
        if (!args.viterbi_options.genie.empty()) {
            nlohmann::json genie = {{"kinds", nlohmann::json::array()}, {"channels", nlohmann::json::array()}};
            for (const GenieKind kind : args.viterbi_options.genie) genie["kinds"].push_back(genie_kind_name(kind));
            for (const auto& per_channel : config.viterbi_genie)
                genie["channels"].push_back(nlohmann::json(to_json(per_channel)));
            out["genie"] = std::move(genie);
        }
    }
    std::vector<DecodedTrack> track_list;     // final text
    std::vector<DecodedTrack> immediate_list; // immediate text
    std::map<std::uint32_t, double> tracked_freq_hz;
    for (const auto& [id, t] : tracks) {
        DecodedTrack final_track = t, immediate_track = t;
        if (const auto it = texts.find(id); it != texts.end()) {
            final_track.text = it->second.final_text();
            immediate_track.text = it->second.immediate_text();
        }
        track_list.push_back(std::move(final_track));
        immediate_list.push_back(std::move(immediate_track));
        tracked_freq_hz[id] = t.last_freq_hz;
    }
    add_tracks(out, track_list, &immediate_list, args.front_end == FrontEnd::Bank ? &texts : nullptr);
    for (nlohmann::json& j : out["tracks"]) {
        const auto id = j["id"].get<std::uint32_t>();
        if (const auto it = born_s.find(id); it != born_s.end()) j["born_s"] = it->second;
        if (const auto it = died_s.find(id); it != died_s.end()) j["died_s"] = it->second;
        if (const auto it = died_end.find(id); it != died_end.end()) j["end"] = track_end_name(it->second);
        j["last_freq_hz"] = tracked_freq_hz[id];
        if (const auto it = texts.find(id); it != texts.end()) {
            std::vector<std::string> symbols;
            for (const auto& symbol : it->second.final_symbols()) symbols.push_back(symbol);
            j["plausible"] = text_evidence(symbols).plausible();
            if (args.symbol_times) {  // [text, start_s, end_s] per final character, stream time
                nlohmann::json timed = nlohmann::json::array();
                for (const auto& c : it->second.final_timed()) timed.push_back({c.text, c.start_s, c.end_s});
                j["symbols"] = std::move(timed);
            }
        }
        if (const auto it = wpm.find(id); it != wpm.end()) j["wpm"] = it->second;
        if (args.timing) {  // not with --no-timing: two runs' files must be the same
            if (const auto it = engine.track_decoder_seconds().find(id); it != engine.track_decoder_seconds().end())
                j["decoder_cpu_s"] = it->second;
        }
    }
    out["live_tracks_each_s"] = live_each_s;
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
        // The bar's two terms (CLAUDE.md, CPU): what recurs whatever the tracks, per second of stream, and what each
        // track adds, per channel-second.
        const EngineStageTimes& st = stats.stages;
        const auto per_stream_ms = [&](double seconds) { return duration_s > 0 ? 1000.0 * seconds / duration_s : 0.0; };
        const double fixed_s = source_s + st.spectrum_s + st.detector_s + st.hiss_s + st.channelizer_s + st.publish_s + st.lifecycle_s;
        const double per_track_s = st.filter_s + st.decoder_s;
        out["timing"]["threads"] = args.threads;
        out["timing"]["fixed_ms_per_stream_s"] = {{"source", per_stream_ms(source_s)},
                                                  {"spectrum", per_stream_ms(st.spectrum_s)},
                                                  {"detector", per_stream_ms(st.detector_s)},
                                                  {"hiss", per_stream_ms(st.hiss_s)},
                                                  {"channelizer_fft", per_stream_ms(st.channelizer_s)},
                                                  {"publish", per_stream_ms(st.publish_s)},
                                                  {"lifecycle", per_stream_ms(st.lifecycle_s)},
                                                  {"total", per_stream_ms(fixed_s)}};
        out["timing"]["ms_per_channel_s"] = {{"filter", per_channel_ms(st.filter_s)},
                                             {"decoder", per_channel_ms(st.decoder_s)},
                                             {"total", per_channel_ms(per_track_s)}};
        out["timing"]["mean_tracks"] = duration_s > 0 ? stats.channel_seconds / duration_s : 0.0;
        std::printf("fixed %.2f ms per s of stream: source %.2f, spectrum %.2f, detector %.2f, hiss %.2f, channelizer FFT %.2f, "
                    "publish %.2f, open/close %.2f\n",
                    per_stream_ms(fixed_s), per_stream_ms(source_s), per_stream_ms(st.spectrum_s), per_stream_ms(st.detector_s),
                    per_stream_ms(st.hiss_s), per_stream_ms(st.channelizer_s), per_stream_ms(st.publish_s),
                    per_stream_ms(st.lifecycle_s));
        std::printf("per track %.3f ms per channel-second: filter %.3f, decoder %.3f; mean tracks %.1f; %d thread(s)\n",
                    per_channel_ms(per_track_s), per_channel_ms(st.filter_s), per_channel_ms(st.decoder_s),
                    duration_s > 0 ? stats.channel_seconds / duration_s : 0.0, args.threads);
    }
    if (engine.impulse_blanker()) {
        // How often the impulse blanker fired, and what share of the decoders' samples it reached.
        const double minutes = duration_s / 60.0;
        const double fired = stats.impulse_slices > 0 ? 100.0 * static_cast<double>(stats.impulse_fired_slices) / static_cast<double>(stats.impulse_slices) : 0.0;
        const double reached = stats.impulse_pushed_samples > 0
                                   ? 100.0 * static_cast<double>(stats.impulse_blanked_samples) / static_cast<double>(stats.impulse_pushed_samples)
                                   : 0.0;
        out["impulse"] = {{"subbands", engine.impulse_blanker()->subbands()},
                          {"slices", stats.impulse_slices},
                          {"fired_slices", stats.impulse_fired_slices},
                          {"firings", stats.impulse_firings},
                          {"firings_per_minute", minutes > 0 ? static_cast<double>(stats.impulse_firings) / minutes : 0.0},
                          {"pushed_samples", stats.impulse_pushed_samples},
                          {"blanked_samples", stats.impulse_blanked_samples}};
        std::printf("impulse blanker: %d subbands; %llu firings, %.2f per minute; pulses in %.3f%% of the band's channel samples; "
                    "%.3f%% of the decoders' widest-filter samples reached\n",
                    engine.impulse_blanker()->subbands(), static_cast<unsigned long long>(stats.impulse_firings),
                    minutes > 0 ? static_cast<double>(stats.impulse_firings) / minutes : 0.0, fired, reached);
    }
    int exit_code = 0;
    if (labels) {
        exit_code =
            score_and_report(*labels, track_list, tracked_freq_hz, args.oracle, args.baseline, out, &immediate_list);
    }
    write_json(args.json, out);
    // After the result file, and after the timed window (writing is not decoding): a statistics file that cannot be
    // finished then loses nothing of the run. The channels are read from the engine one at a time, with no copy.
    if (args.stats) {
        write_run_stats(*args.stats, engine, args.recording->filename().string(), config.viterbi,
                        labels->signals.size(),
                        args.viterbi_options.stats_detail ? StatsDetail::Lines : StatsDetail::Summary);
    }
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
    out["decoder"] = decoded.front_end;
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
