#include "viterbi_json.hpp"

#include "kz4ap/classical_decoder.hpp"
#include "kz4ap/engine.hpp"
#include "kz4ap/event_bus.hpp"
#include "kz4ap/viterbi/fist.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <system_error>
#include <utility>

namespace kz4ap::bench {

namespace {

using nlohmann::ordered_json;

// ---- The configuration: every ViterbiConfig field, by its C++ name, in declaration order ----

// The compile-time guard in bench/tests/viterbi_json_test.cpp stops that file from compiling when a field is added or
// removed, until this table is changed with it.
#define KZ4AP_VITERBI_CONFIG_FIELDS(X)                                                                              \
    X(piece_s) X(dot_min_ms) X(speed_ratio) X(speed_count) X(factor_ratio) X(factor_count) X(fist_medians_dots)     \
    X(fist_sigmas) X(fist_shifts_dots) X(beam_nats) X(ceiling) X(fading_spread_hz) X(block_loss_db)                 \
    X(keying_rise_s) X(over_rate_per_s) X(rescue_epsilon) X(word_end_p) X(over_end_q) X(level_min_db)               \
    X(level_max_db) X(quadrature_order) X(hiss_half_width_hz) X(hiss_percentile) X(hiss_average_s)                  \
    X(tracker_window_s) X(tracker_hop_s) X(tracker_search_hz) X(tracker_gate_db) X(tracker_drift_sd_hz_per_s)       \
    X(tracker_drift_noise) X(genie_frequency_offset_hz) X(genie_frequency_drift_hz_per_s)                           \
    X(genie_fading_spread_hz) X(stats)

ordered_json field_json(double v) { return ordered_json(v); }
ordered_json field_json(int v) { return ordered_json(v); }
ordered_json field_json(bool v) { return ordered_json(v); }
ordered_json field_json(const std::array<double, 5>& v) {
    ordered_json a = ordered_json::array();
    for (const double x : v) a.push_back(x);
    return a;
}
ordered_json field_json(const std::optional<double>& v) { return v ? ordered_json(*v) : ordered_json(nullptr); }

// The assign overloads check the value completely before they change the field, so that a refused setting changes
// nothing.
void assign(const std::string& name, double& field, const ordered_json& value) {
    if (!value.is_number() || !std::isfinite(value.get<double>()))
        throw std::invalid_argument(name + ": a finite number is needed");
    field = value.get<double>();
}

void assign(const std::string& name, int& field, const ordered_json& value) {
    const std::string needed = name + ": a whole number in the range of an int is needed";
    if (!value.is_number()) throw std::invalid_argument(needed);
    const double x = value.get<double>();
    if (!std::isfinite(x) || x != std::floor(x) || x < static_cast<double>(std::numeric_limits<int>::min()) ||
        x > static_cast<double>(std::numeric_limits<int>::max()))
        throw std::invalid_argument(needed);
    field = static_cast<int>(x);
}

void assign(const std::string& name, bool& field, const ordered_json& value) {
    if (!value.is_boolean()) throw std::invalid_argument(name + ": true or false is needed");
    field = value.get<bool>();
}

void assign(const std::string& name, std::array<double, 5>& field, const ordered_json& value) {
    const std::string needed = name + ": a list of exactly 5 finite numbers is needed";
    if (!value.is_array() || value.size() != field.size()) throw std::invalid_argument(needed);
    std::array<double, 5> out{};
    for (std::size_t i = 0; i < out.size(); ++i) {
        if (!value[i].is_number() || !std::isfinite(value[i].get<double>())) throw std::invalid_argument(needed);
        out[i] = value[i].get<double>();
    }
    field = out;
}

void assign(const std::string& name, std::optional<double>& field, const ordered_json& value) {
    if (value.is_null()) {
        field.reset();
        return;
    }
    if (!value.is_number() || !std::isfinite(value.get<double>()))
        throw std::invalid_argument(name + ": a finite number, or null for none, is needed");
    field = value.get<double>();
}

// ---- The genie from the labels ----

// Median (dots) and standard deviation of ln(length) of the five kinds, in the order dot, dash, element gap, letter
// gap, word gap, of one keying style.
struct Duration {
    double median_dots;
    double sigma;
};
struct Style {
    std::string name;
    std::array<Duration, 5> kinds;
};

// A copy of kz4ap_synth.keying.STYLES (training/kz4ap_synth/keying.py: "machine" exact PARIS timing, the others
// VE3NEA's DeepCW styles Computer, Paddle, Vibroplex and HandKey, each median exp(mu) of his mu), one row per style:
// its name, the five medians in dots, the five sigmas in ln(length). training/tests/test_keying.py reads the rows
// between the two KEYING_STYLES marks and checks that they are the generator's.
struct StyleRow {
    const char* name;
    double medians_dots[5];
    double sigmas[5];
};
const std::vector<Style>& styles() {
    static const std::vector<Style> table = [] {
        // KEYING_STYLES begin
        const StyleRow rows[] = {
            {"machine", {1.0, 3.0, 1.0, 3.0, 7.0}, {0.0, 0.0, 0.0, 0.0, 0.0}},
            {"computer", {1.0, std::exp(1.10), 1.0, std::exp(1.10), std::exp(1.94)}, {0.05, 0.016, 0.05, 0.016, 0.008}},
            {"paddle", {1.0, std::exp(1.10), 1.0, std::exp(1.10), std::exp(1.94)}, {0.05, 0.016, 0.05, 0.2, 0.2}},
            {"bug", {1.0, std::exp(1.10), 1.0, std::exp(1.10), std::exp(1.94)}, {0.05, 0.2, 0.05, 0.2, 0.2}},
            {"hand", {1.0, std::exp(1.50), 1.0, std::exp(1.50), std::exp(2.0)}, {0.15, 0.3, 0.2, 0.3, 0.2}},
        };
        // KEYING_STYLES end
        std::vector<Style> out;
        for (const StyleRow& row : rows) {
            Style style{row.name, {}};
            for (std::size_t kind = 0; kind < 5; ++kind) style.kinds[kind] = {row.medians_dots[kind], row.sigmas[kind]};
            out.push_back(std::move(style));
        }
        return out;
    }();
    return table;
}

// The fist's arrays are in the order of viterbi::Kind (fist.hpp); the styles' columns are in the same order.
static_assert(static_cast<int>(viterbi::Kind::Dot) == 0 && static_cast<int>(viterbi::Kind::Dash) == 1 &&
                  static_cast<int>(viterbi::Kind::ElementGap) == 2 && static_cast<int>(viterbi::Kind::LetterGap) == 3 &&
                  static_cast<int>(viterbi::Kind::WordGap) == 4,
              "the keying styles' columns follow viterbi::Kind");

ordered_json array_json(const std::array<double, 5>& v) {
    ordered_json a = ordered_json::array();
    for (const double x : v) a.push_back(x);
    return a;
}

// ---- The statistics file ----

const char* const kLineKindNames[] = {"Start", "Silence", "Rescue"};
const char* const kLineEndNames[] = {"Alive", "Merge", "Beam", "Ceiling"};
constexpr int kLineKinds = 3;
constexpr int kLineEnds = 4;
static_assert(static_cast<int>(viterbi::LineKind::Start) == 0 && static_cast<int>(viterbi::LineKind::Silence) == 1 &&
                  static_cast<int>(viterbi::LineKind::Rescue) == 2,
              "the file's line_kinds legend follows viterbi::LineKind");
static_assert(static_cast<int>(viterbi::LineEnd::Alive) == 0 && static_cast<int>(viterbi::LineEnd::Merge) == 1 &&
                  static_cast<int>(viterbi::LineEnd::Beam) == 2 && static_cast<int>(viterbi::LineEnd::Ceiling) == 3,
              "the file's line_ends legend follows viterbi::LineEnd");

// A statistic that is written must be finite (JSON has no NaN or infinity), and the message names the member.
double finite_or_throw(double x, const std::string& what) {
    if (!std::isfinite(x)) throw std::invalid_argument("statistics: " + what + " is not finite");
    return x;
}

ordered_json optional_time(const std::optional<double>& t, const std::string& what) {
    return t ? ordered_json(finite_or_throw(*t, what)) : ordered_json(nullptr);
}

ordered_json fields_json() {
    ordered_json stats = ordered_json::object();
    stats["pieces"] = "count: pieces searched, each config.piece_s long";
    stats["ceiling_pieces"] = "count: pieces at which the ceiling dropped at least one story";
    stats["live_histogram"] =
        "counts of pieces, by the number of live stories after pruning, in bins of 10 stories: entry k counts the "
        "pieces with 10 k to 10 k + 9 live stories";
    stats["live_mean_100ms"] =
        "stories: the mean number of live stories after pruning in each 0.1 s window of pieces (round(0.1 s / "
        "config.piece_s) pieces), from the channel's first piece; the last entry covers the pieces of its window "
        "searched so far";
    stats["live_max_100ms"] = "stories: the largest number of live stories after pruning in each window, as above";
    stats["lines"] =
        "the newborn lines in the order of their ids (line 0 is the channel's first story), as columns named by the "
        "members of NewbornLine, each an array with one entry per line";
    ordered_json line = ordered_json::object();
    line["birth_s"] = "s: the stream time of the piece at which the line was created (line 0: the first piece's)";
    line["kind"] =
        "code into line_kinds: how the line began (Start: line 0, the channel's first story, in the no-signal state; "
        "Silence: a newborn from silence, an over starts; Rescue: a rescue newborn, the sender begins a new story at "
        "a letter start)";
    line["first_led_s"] =
        "s or null: the stream time of the first piece after whose pruning one of the line's stories was the best; "
        "null if none ever was";
    line["largest_deficit_nats"] =
        "nats: how far the line's best story was behind the best, at most, after the pruning of each piece from its "
        "birth until it first led, or over its life if it never led; at its birth piece the newborn's own deficit, "
        "even where the newborn did not survive that piece";
    line["end"] =
        "code into line_ends: which step removed the line's last story (Alive while it has one; Merge: merged into a "
        "better story of its state; Beam: more than beam_nats behind the best; Ceiling: beyond the ceiling's best "
        "stories)";
    line["end_s"] =
        "s or null: the stream time of the piece at which the line's last story was removed; null while it is alive";
    line["ended_in_first_element"] =
        "true or false: the line's last story was removed before any story of the line had left the newborn's first "
        "mark (false for line 0)";
    ordered_json fields = ordered_json::object();
    fields["SearchStats"] = std::move(stats);
    fields["NewbornLine"] = std::move(line);
    return fields;
}

// The members of the file before its channels.
ordered_json header_json(const StatsFile& file) {
    ordered_json j = ordered_json::object();
    j["format"] = kStatsFormat;
    j["version"] = kStatsVersion;
    j["recording"] = file.recording;
    j["config"] = to_json(file.config);
    j["fields"] = fields_json();
    ordered_json kinds = ordered_json::array();
    for (const char* name : kLineKindNames) kinds.push_back(name);
    ordered_json ends = ordered_json::array();
    for (const char* name : kLineEndNames) ends.push_back(name);
    j["line_kinds"] = std::move(kinds);
    j["line_ends"] = std::move(ends);
    return j;
}

// ---- Reading the statistics file ----

[[noreturn]] void bad(const std::string& where, const std::string& what) {
    throw std::invalid_argument("statistics file" + (where.empty() ? std::string() : ", " + where) + ": " + what);
}

const ordered_json& member(const ordered_json& object, const char* key, const std::string& where) {
    if (!object.is_object()) bad(where, "an object is needed");
    const auto it = object.find(key);
    if (it == object.end()) bad(where, std::string("\"") + key + "\" is missing");
    return *it;
}

std::int64_t integer(const ordered_json& value, const std::string& what, const std::string& where) {
    if (!value.is_number_integer()) bad(where, what + " must be a whole number");
    return value.get<std::int64_t>();
}

double number(const ordered_json& value, const std::string& what, const std::string& where) {
    if (!value.is_number()) bad(where, what + " must be a number");
    return value.get<double>();
}

float single(const ordered_json& value, const std::string& what, const std::string& where) {
    const float x = static_cast<float>(number(value, what, where));
    if (!std::isfinite(x)) bad(where, what + " does not fit a float");
    return x;
}

const ordered_json& array_member(const ordered_json& object, const char* key, const std::string& where) {
    const ordered_json& a = member(object, key, where);
    if (!a.is_array()) bad(where, std::string("\"") + key + "\" must be an array");
    return a;
}

}  // namespace

// ---- The configuration ----

ordered_json to_json(const viterbi::ViterbiConfig& config) {
    ordered_json out = ordered_json::object();
#define KZ4AP_FIELD_TO_JSON(field) out[#field] = field_json(config.field);
    KZ4AP_VITERBI_CONFIG_FIELDS(KZ4AP_FIELD_TO_JSON)
#undef KZ4AP_FIELD_TO_JSON
    return out;
}

const std::vector<std::string>& viterbi_config_field_names() {
    static const std::vector<std::string> names = {
#define KZ4AP_FIELD_NAME(field) #field,
        KZ4AP_VITERBI_CONFIG_FIELDS(KZ4AP_FIELD_NAME)
#undef KZ4AP_FIELD_NAME
    };
    return names;
}

void set_config_value(viterbi::ViterbiConfig& config, const std::string& name, const ordered_json& value) {
#define KZ4AP_FIELD_SET(field)                \
    if (name == #field) {                     \
        assign(name, config.field, value);    \
        return;                               \
    }
    KZ4AP_VITERBI_CONFIG_FIELDS(KZ4AP_FIELD_SET)
#undef KZ4AP_FIELD_SET
    throw std::invalid_argument("unknown parameter '" + name + "'");
}

void apply_setting(viterbi::ViterbiConfig& config, const std::string& setting) {
    const std::size_t equals = setting.find('=');
    if (equals == std::string::npos || equals == 0)
        throw std::invalid_argument("--set needs NAME=VALUE, not '" + setting + "'");
    const std::string name = setting.substr(0, equals);
    const std::string text = setting.substr(equals + 1);
    ordered_json value;
    try {
        value = ordered_json::parse(text);
    } catch (const ordered_json::parse_error&) {
        value = ordered_json(text);  // not JSON: text, which no field takes, so the field's message follows
    }
    set_config_value(config, name, value);
}

viterbi::ViterbiConfig viterbi_config_from_json(const ordered_json& j) {
    if (!j.is_object()) throw std::invalid_argument("a configuration must be a JSON object");
    viterbi::ViterbiConfig config;
    for (const auto& item : j.items()) set_config_value(config, item.key(), item.value());
    return config;
}

void check_engine_accepts(const EngineConfig& config) {
    EngineConfig probe = config;
    probe.decoder.front_end = FrontEnd::Viterbi;
    probe.oracle_frequencies_hz = {0.0};  // one oracle channel: the engine builds a decoder for it now
    probe.viterbi_genie.clear();
    EventBus bus;
    Engine engine(probe, bus);  // throws what the engine, the shared parts or the decoder refuse; discarded
}

// ---- The genie ----

GenieKind parse_genie_kind(const std::string& name) {
    if (name == "fist") return GenieKind::Fist;
    if (name == "frequency") return GenieKind::Frequency;
    if (name == "fading") return GenieKind::Fading;
    throw std::invalid_argument("--genie '" + name + "': must be fist, frequency or fading");
}

const char* genie_kind_name(GenieKind kind) {
    switch (kind) {
        case GenieKind::Fist: return "fist";
        case GenieKind::Frequency: return "frequency";
        case GenieKind::Fading: return "fading";
    }
    return "?";
}

viterbi::ViterbiGenie genie_from_label(const LabeledSignal& label, std::size_t index, std::span<const GenieKind> kinds,
                                       double piece_s) {
    const auto wants = [&](GenieKind kind) { return std::find(kinds.begin(), kinds.end(), kind) != kinds.end(); };
    const std::string who = "genie, signal " + std::to_string(index);
    const bool several_stations = label.sender_count > 1;
    const std::string stations = std::to_string(label.sender_count) + " stations, each with its own ";
    viterbi::ViterbiGenie genie;
    if (wants(GenieKind::Frequency)) {
        if (several_stations)
            throw std::invalid_argument(who + ": the label holds " + stations +
                                        "carrier; the genie frequency needs the per-station labels");
        genie.frequency_hz = label.freq_offset_hz;
        genie.frequency_time_s = label.start_s;
        genie.drift_hz_per_s = label.drift_hz_per_s;
    }
    if (wants(GenieKind::Fading)) genie.fading_spread_hz = label.fading_hz;
    if (wants(GenieKind::Fist)) {
        if (several_stations)
            throw std::invalid_argument(who + ": the label holds " + stations +
                                        "fist; the genie fist needs the per-station labels");
        if (label.wpm_end && *label.wpm_end != label.wpm)
            throw std::invalid_argument(who + ": the speed changes from " + std::to_string(label.wpm) + " to " +
                                        std::to_string(*label.wpm_end) + " WPM; one dot cannot give both speeds");
        if (!(label.wpm > 0.0) || !std::isfinite(label.wpm))
            throw std::invalid_argument(who + ": the speed, " + std::to_string(label.wpm) + " WPM, is not a speed");
        if (!(piece_s > 0.0) || !std::isfinite(piece_s))
            throw std::invalid_argument(who + ": piece_s must be positive and finite (s)");
        const auto& table = styles();
        const auto style = std::find_if(table.begin(), table.end(),
                                        [&](const Style& candidate) { return candidate.name == label.keying; });
        if (style == table.end()) {
            std::string known;
            for (const Style& candidate : table) known += (known.empty() ? "" : ", ") + candidate.name;
            throw std::invalid_argument(who + ": the keying style '" + label.keying + "' is not one of " + known);
        }
        viterbi::ViterbiGenie::Fist fist;
        fist.dot_ms = 1200.0 / label.wpm;  // the generator's dot, 1.2 s / WPM
        const double dot_s = fist.dot_ms / 1000.0;
        for (std::size_t kind = 0; kind < 5; ++kind) {
            const Duration& duration = style->kinds[kind];
            fist.medians_dots[kind] = duration.median_dots;
            // at least one piece over the median length, in ln(length): the heuristic floor of the header
            fist.sigmas[kind] = std::max(duration.sigma, piece_s / (duration.median_dots * dot_s));
            // the marks are longer, the spaces shorter, by the imbalance; the Farnsworth factor does not stretch it
            // (0.0 - x, not -x: no imbalance is a shift of 0, not of -0)
            fist.shifts_dots[kind] = kind < 2 ? label.imbalance_dits : 0.0 - label.imbalance_dits;
        }
        genie.fist = fist;
    }
    return genie;
}

ordered_json to_json(const viterbi::ViterbiGenie& genie) {
    ordered_json j = ordered_json::object();
    j["frequency_hz"] = field_json(genie.frequency_hz);
    j["frequency_time_s"] = genie.frequency_time_s;
    j["drift_hz_per_s"] = genie.drift_hz_per_s;
    j["fading_spread_hz"] = field_json(genie.fading_spread_hz);
    if (genie.fist) {
        ordered_json fist = ordered_json::object();
        fist["dot_ms"] = genie.fist->dot_ms;
        fist["medians_dots"] = array_json(genie.fist->medians_dots);
        fist["sigmas"] = array_json(genie.fist->sigmas);
        fist["shifts_dots"] = array_json(genie.fist->shifts_dots);
        j["fist"] = std::move(fist);
    } else {
        j["fist"] = nullptr;
    }
    return j;
}

// ---- The rules among the options ----

void check_viterbi_options(bool decoder_is_viterbi, bool oracle, const ViterbiOptions& options) {
    if (!decoder_is_viterbi && !options.settings.empty())
        throw std::invalid_argument("--set belongs to --decoder viterbi (the other decoders take no settings here)");
    if (!decoder_is_viterbi && !options.genie.empty())
        throw std::invalid_argument("--genie belongs to --decoder viterbi");
    if (!options.genie.empty() && !oracle)
        throw std::invalid_argument(
            "--genie needs --oracle: its values come from the labels of the oracle channels, one set for each");
}

// ---- The statistics file ----

ordered_json to_json(const ChannelStats& channel) {
    const viterbi::SearchStats& s = channel.stats;
    ordered_json j = ordered_json::object();
    j["track_id"] = channel.track_id;
    j["label_index"] = channel.label_index ? ordered_json(*channel.label_index) : ordered_json(nullptr);
    j["pieces"] = s.pieces;
    j["ceiling_pieces"] = s.ceiling_pieces;
    ordered_json histogram = ordered_json::array();
    for (const std::int64_t count : s.live_histogram) histogram.push_back(count);
    j["live_histogram"] = std::move(histogram);
    const auto stories = [](const std::vector<float>& v, const char* name) {
        ordered_json a = ordered_json::array();
        for (const float x : v) a.push_back(finite_or_throw(static_cast<double>(x), name));  // a float is its double
        return a;
    };
    j["live_mean_100ms"] = stories(s.live_mean_100ms, "live_mean_100ms");
    j["live_max_100ms"] = stories(s.live_max_100ms, "live_max_100ms");
    ordered_json birth_s = ordered_json::array(), kind = ordered_json::array(), first_led_s = ordered_json::array(),
                 deficit = ordered_json::array(), end = ordered_json::array(), end_s = ordered_json::array(),
                 first_element = ordered_json::array();
    for (const viterbi::NewbornLine& line : s.lines) {
        birth_s.push_back(finite_or_throw(line.birth_s, "birth_s"));
        kind.push_back(static_cast<int>(line.kind));
        first_led_s.push_back(optional_time(line.first_led_s, "first_led_s"));
        deficit.push_back(finite_or_throw(line.largest_deficit_nats, "largest_deficit_nats"));
        end.push_back(static_cast<int>(line.end));
        end_s.push_back(optional_time(line.end_s, "end_s"));
        first_element.push_back(line.ended_in_first_element);
    }
    ordered_json lines = ordered_json::object();
    lines["birth_s"] = std::move(birth_s);
    lines["kind"] = std::move(kind);
    lines["first_led_s"] = std::move(first_led_s);
    lines["largest_deficit_nats"] = std::move(deficit);
    lines["end"] = std::move(end);
    lines["end_s"] = std::move(end_s);
    lines["ended_in_first_element"] = std::move(first_element);
    j["lines"] = std::move(lines);
    return j;
}

ordered_json to_json(const StatsFile& file) {
    ordered_json j = header_json(file);
    ordered_json channels = ordered_json::array();
    for (const ChannelStats& channel : file.channels) channels.push_back(to_json(channel));
    j["channels"] = std::move(channels);
    return j;
}

ChannelStats channel_stats_from_json(const ordered_json& j) {
    const std::string where = "channel";
    ChannelStats channel;
    const std::int64_t id = integer(member(j, "track_id", where), "track_id", where);
    if (id < 0 || id > std::numeric_limits<std::uint32_t>::max()) bad(where, "track_id is out of range");
    channel.track_id = static_cast<std::uint32_t>(id);
    const std::string at = "channel " + std::to_string(channel.track_id);
    const ordered_json& label = member(j, "label_index", at);
    if (!label.is_null()) channel.label_index = static_cast<int>(integer(label, "label_index", at));
    viterbi::SearchStats& s = channel.stats;
    s.pieces = integer(member(j, "pieces", at), "pieces", at);
    s.ceiling_pieces = integer(member(j, "ceiling_pieces", at), "ceiling_pieces", at);
    for (const ordered_json& count : array_member(j, "live_histogram", at))
        s.live_histogram.push_back(integer(count, "live_histogram", at));
    for (const ordered_json& x : array_member(j, "live_mean_100ms", at))
        s.live_mean_100ms.push_back(single(x, "live_mean_100ms", at));
    for (const ordered_json& x : array_member(j, "live_max_100ms", at))
        s.live_max_100ms.push_back(single(x, "live_max_100ms", at));
    const ordered_json& lines = member(j, "lines", at);
    const ordered_json& birth_s = array_member(lines, "birth_s", at + ", lines");
    const ordered_json& kind = array_member(lines, "kind", at + ", lines");
    const ordered_json& first_led_s = array_member(lines, "first_led_s", at + ", lines");
    const ordered_json& deficit = array_member(lines, "largest_deficit_nats", at + ", lines");
    const ordered_json& end = array_member(lines, "end", at + ", lines");
    const ordered_json& end_s = array_member(lines, "end_s", at + ", lines");
    const ordered_json& first_element = array_member(lines, "ended_in_first_element", at + ", lines");
    const std::size_t n = birth_s.size();
    if (kind.size() != n || first_led_s.size() != n || deficit.size() != n || end.size() != n || end_s.size() != n ||
        first_element.size() != n)
        bad(at + ", lines", "the columns have different lengths");
    const std::string in_lines = at + ", lines";
    const auto optional_time_at = [&](const ordered_json& value, const char* what) -> std::optional<double> {
        if (value.is_null()) return std::nullopt;
        return number(value, what, in_lines);
    };
    s.lines.reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
        viterbi::NewbornLine line;
        line.birth_s = number(birth_s[i], "birth_s", in_lines);
        const std::int64_t k = integer(kind[i], "kind", in_lines);
        if (k < 0 || k >= kLineKinds) bad(in_lines, "kind " + std::to_string(k) + " is not in line_kinds");
        line.kind = static_cast<viterbi::LineKind>(k);
        line.first_led_s = optional_time_at(first_led_s[i], "first_led_s");
        line.largest_deficit_nats = number(deficit[i], "largest_deficit_nats", in_lines);
        const std::int64_t e = integer(end[i], "end", in_lines);
        if (e < 0 || e >= kLineEnds) bad(in_lines, "end " + std::to_string(e) + " is not in line_ends");
        line.end = static_cast<viterbi::LineEnd>(e);
        line.end_s = optional_time_at(end_s[i], "end_s");
        if (!first_element[i].is_boolean()) bad(in_lines, "ended_in_first_element must be true or false");
        line.ended_in_first_element = first_element[i].get<bool>();
        s.lines.push_back(line);
    }
    return channel;
}

StatsFile stats_file_from_json(const ordered_json& j) {
    const std::string where;
    const ordered_json& format = member(j, "format", where);
    if (!format.is_string() || format.get<std::string>() != kStatsFormat)
        bad(where, std::string("\"format\" must be \"") + kStatsFormat + "\"");
    const std::int64_t version = integer(member(j, "version", where), "version", where);
    if (version != kStatsVersion)
        bad(where, "version " + std::to_string(version) + " is not the version this reader reads, " +
                       std::to_string(kStatsVersion));
    StatsFile file;
    const ordered_json& recording = member(j, "recording", where);
    if (!recording.is_string()) bad(where, "\"recording\" must be text");
    file.recording = recording.get<std::string>();
    file.config = viterbi_config_from_json(member(j, "config", where));
    for (const ordered_json& channel : array_member(j, "channels", where))
        file.channels.push_back(channel_stats_from_json(channel));
    return file;
}

void write_stats_file(const std::filesystem::path& path, const StatsFile& file) {
    // The channels are written one at a time, each turned into JSON when its turn comes (a channel keeps up to two
    // lines per piece, so all of them at once would be large); the file is written under a name of its own and
    // renamed when complete, so that a statistic that is not finite, or a full disk, leaves no file that looks
    // complete (as the replay tool writes its decoded files).
    const std::filesystem::path partial = path.string() + ".partial";
    std::ofstream out(partial, std::ios::binary);
    if (!out) throw std::runtime_error("cannot write " + partial.string());
    try {
        const ordered_json header = header_json(file);  // named: items() of a temporary would dangle in the loop
        out << "{\n";
        for (const auto& item : header.items())
            out << ordered_json(item.key()).dump(-1, ' ', true) << ": " << item.value().dump(-1, ' ', true) << ",\n";
        out << "\"channels\": [\n";
        for (std::size_t i = 0; i < file.channels.size(); ++i)
            out << to_json(file.channels[i]).dump(-1, ' ', true) << (i + 1 < file.channels.size() ? ",\n" : "\n");
        out << "]\n}\n";
        out.close();
        if (!out) throw std::runtime_error("cannot write " + partial.string());
    } catch (...) {
        out.close();
        std::error_code ignored;
        std::filesystem::remove(partial, ignored);
        throw;
    }
    std::filesystem::rename(partial, path);
}

StatsFile read_stats_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot open " + path.string());
    ordered_json j;
    try {
        j = ordered_json::parse(in);
    } catch (const ordered_json::exception& e) {
        throw std::invalid_argument("statistics file " + path.string() + " is not JSON: " + e.what());
    }
    return stats_file_from_json(j);
}

}  // namespace kz4ap::bench
