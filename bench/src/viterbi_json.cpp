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
#include <functional>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <system_error>
#include <tuple>
#include <utility>

namespace kz4ap::bench {

namespace {

using nlohmann::ordered_json;

// ---- The configuration: every ViterbiConfig field, by its C++ name, in declaration order ----

// The compile-time guard in bench/tests/viterbi_json_test.cpp stops that file from compiling when a field is added or
// removed, until this table is changed with it.
#define KZ4AP_VITERBI_CONFIG_FIELDS(X) \
    X(sample_interval_s) X(dot_min_ms) X(speed_ratio) X(speed_count) X(fist_medians_dots) X(fist_sigmas)  \
    X(fist_shifts_dots) X(beam_nats) X(ceiling) X(merge_edge_history_samples) X(front_dot_s) X(edge_rise_s)  \
    X(edge_rise_min_s) X(edge_rise_max_s) X(fading_spread_hz) X(added_variance_multiplier_max)  \
    X(added_variance_level_knee) X(amplitude_update_min_template) X(systematic_floor) X(nu_bin_s) X(nu_memory_s)  \
    X(over_rate_per_s) X(rescue_epsilon) X(word_end_p) X(over_end_q) X(symbol_prior_on_air) X(symbol_prior_floor)  \
    X(level_min_db) X(level_max_db) X(quadrature_order)  \
    X(newborn_switch_sigma_u) X(newborn_grid_points) X(newborn_live_range_nats) X(newborn_fade_interval_s)  \
    X(hiss_half_width_hz) X(hiss_percentile) X(hiss_average_s) X(genie_fading_spread_hz) X(stats)

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
static_assert(std::size(kLineKindNames) == kLineKinds && std::size(kLineEndNames) == kLineEnds,
              "the legends' names are as many as the codes");
// ChannelSummary::fates is [kind][end][ended_in_first_element][led]: the legends' sizes, then false or true twice
static_assert(std::tuple_size_v<FateCounts> == kLineKinds &&
                  std::tuple_size_v<FateCounts::value_type> == kLineEnds &&
                  std::tuple_size_v<FateCounts::value_type::value_type> == 2 &&
                  std::tuple_size_v<FateCounts::value_type::value_type::value_type> == 2,
              "the fates table follows the kinds' and the ends' legends");
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
    stats["samples"] = "count: samples searched, each config.sample_interval_s long";
    stats["ceiling_samples"] = "count: samples at which the ceiling dropped at least one story";
    stats["live_histogram"] =
        "counts: samples, by the number of live stories after pruning, in bins of 10 stories: entry k counts the "
        "samples with 10 k to 10 k + 9 live stories";
    stats["live_mean_100ms"] =
        "stories: the mean number of live stories after pruning in each 0.1 s window of samples (round(0.1 s / "
        "config.sample_interval_s) samples), from the channel's first sample; the last entry covers the samples of its window "
        "searched so far";
    stats["live_max_100ms"] = "stories: the largest number of live stories after pruning in each window, as above";
    stats["lines"] =
        "columns: the newborn lines in the order of their ids (line 0 is the channel's first story), as columns named "
        "by the members of NewbornLine, each an array with one entry per line";
    ordered_json line = ordered_json::object();
    line["birth_s"] = "s: the stream time of the sample at which the line was created (line 0: the first sample's)";
    line["kind"] =
        "code into line_kinds: how the line began (Start: line 0, the channel's first story, in the no-signal state; "
        "Silence: a newborn from silence, an over starts; Rescue: a rescue newborn, the sender begins a new story at "
        "a letter start)";
    line["first_led_s"] =
        "s or null: the stream time of the first sample after whose pruning one of the line's stories was the best; "
        "null if none ever was";
    line["largest_deficit_nats"] =
        "nats: how far the line's best story was behind the best, at most, after the pruning of each sample from its "
        "birth until it first led, or over its life if it never led; at its birth sample the newborn's own deficit, "
        "even where the newborn did not survive that sample";
    line["end"] =
        "code into line_ends: which step removed the line's last story (Alive while it has one; Merge: merged into a "
        "better story of its state; Beam: more than beam_nats behind the best; Ceiling: beyond the ceiling's best "
        "stories)";
    line["end_s"] =
        "s or null: the stream time of the sample at which the line's last story was removed; null while it is alive";
    line["ended_in_first_element"] =
        "true or false: the line's last story was removed before any story of the line had left the newborn's first "
        "mark (false for line 0)";
    ordered_json fields = ordered_json::object();
    fields["SearchStats"] = std::move(stats);
    fields["NewbornLine"] = std::move(line);
    return fields;
}

// What each member of a version 2 channel is, and of a line that led (the version 1 legend of a line and the line's id).
ordered_json fields_summary_json() {
    ordered_json summary = ordered_json::object();
    summary["samples"] = "count: samples searched, each config.sample_interval_s long";
    summary["ceiling_samples"] = "count: samples at which the ceiling dropped at least one story";
    summary["live_histogram"] =
        "counts: samples, by the number of live stories after pruning, in bins of 10 stories: entry k counts the "
        "samples with 10 k to 10 k + 9 live stories";
    summary["live_mean_stories"] =
        "stories: the mean number of live stories after pruning over the channel's samples: the means of the 0.1 s "
        "windows (round(0.1 s / config.sample_interval_s) samples each, the last one the rest of the samples) weighted by their "
        "samples";
    summary["live_max_stories"] = "stories: the largest number of live stories after pruning at any sample";
    summary["lines"] = "count: the newborn lines (line 0 is the channel's first story)";
    summary["fates"] =
        "counts: the newborn lines by fates[kind][end][ended_in_first_element][led], 3 x 4 x 2 x 2 integers: kind is "
        "an index into line_kinds, end an index into line_ends, ended_in_first_element and led are 0 (false) or 1 "
        "(true; led: first_led_s is not null); fate_axes names the axes";
    summary["led_lines"] =
        "columns: the lines that ever led (first_led_s is not null), in the order of their ids, as the members of "
        "NewbornLine and the line's id as line; no other line is kept";
    const ordered_json lines = fields_json();  // named: items() of a temporary would dangle in the loop below
    ordered_json line = ordered_json::object();
    line["line"] = "count: the line's id, its position among the channel's lines in the full form (version 1); ascending";
    for (const auto& item : lines.at("NewbornLine").items()) line[item.key()] = item.value();
    ordered_json fields = ordered_json::object();
    fields["ChannelSummary"] = std::move(summary);
    fields["NewbornLine"] = std::move(line);
    return fields;
}

// The members of the file before its channels, for version 1 (every line) or 2 (the summary).
ordered_json header_json(int version, const std::string& recording, const viterbi::ViterbiConfig& config) {
    ordered_json j = ordered_json::object();
    j["format"] = kStatsFormat;
    j["version"] = version;
    j["recording"] = recording;
    j["config"] = to_json(config);
    j["fields"] = version == kStatsVersion ? fields_json() : fields_summary_json();
    ordered_json kinds = ordered_json::array();
    for (const char* name : kLineKindNames) kinds.push_back(name);
    ordered_json ends = ordered_json::array();
    for (const char* name : kLineEndNames) ends.push_back(name);
    j["line_kinds"] = std::move(kinds);
    j["line_ends"] = std::move(ends);
    if (version == kStatsSummaryVersion) j["fate_axes"] = ordered_json::array({"kind", "end", "ended_in_first_element", "led"});
    return j;
}

ordered_json header_json(const StatsFile& file) { return header_json(kStatsVersion, file.recording, file.config); }
ordered_json header_json(const StatsSummaryFile& file) {
    return header_json(kStatsSummaryVersion, file.recording, file.config);
}

// s: the window of the search's live series (search.cpp, kStatsWindowS)
constexpr double kStatsWindowS = 0.1;

// The statistics of a track's search, or std::runtime_error naming the track.
const viterbi::SearchStats& engine_stats(const Engine& engine, std::uint32_t track) {
    const viterbi::SearchStats* stats = engine.viterbi_stats(track);
    if (!stats)
        throw std::runtime_error("no search statistics for track " + std::to_string(track) +
                                 " (no such channel, or its decoder is not the new decoder)");
    return *stats;
}

// A version 1 channel's object from its parts; to_json(ChannelStats) and write_run_stats make it, the latter straight
// from the engine's statistics.
ordered_json channel_json(std::uint32_t track_id, const std::optional<int>& label_index,
                          const viterbi::SearchStats& s) {
    ordered_json j = ordered_json::object();
    j["track_id"] = track_id;
    j["label_index"] = label_index ? ordered_json(*label_index) : ordered_json(nullptr);
    j["samples"] = s.samples;
    j["ceiling_samples"] = s.ceiling_samples;
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

// Writes a statistics file under a name of its own, `path` plus ".partial", renamed when complete: the header's keys,
// then the channels one at a time, each turned into JSON when its turn comes (a full channel is large, so not all of
// them at once). A failure to write it (a statistic that is not finite, a channel without statistics, a full disk)
// removes the partial file and leaves `path` as it was. A failure of the rename alone (the destination is a folder or a
// read-only file, or on Windows is held open by another program) leaves a whole file, so it stays as the partial file
// and the message says where it is, for a person to rename.
void write_stats_stream(const std::filesystem::path& path, const ordered_json& header, std::size_t channels,
                        const std::function<ordered_json(std::size_t)>& channel_object) {
    const std::filesystem::path partial = path.string() + ".partial";
    std::ofstream out(partial, std::ios::binary);
    if (!out) throw std::runtime_error("cannot write " + partial.string());
    try {
        out << "{\n";
        for (const auto& item : header.items())
            out << ordered_json(item.key()).dump(-1, ' ', true) << ": " << item.value().dump(-1, ' ', true) << ",\n";
        out << "\"channels\": [\n";
        for (std::size_t i = 0; i < channels; ++i)
            out << channel_object(i).dump(-1, ' ', true) << (i + 1 < channels ? ",\n" : "\n");
        out << "]\n}\n";
        out.close();
        if (!out) throw std::runtime_error("cannot write " + partial.string());
    } catch (...) {
        out.close();
        std::error_code ignored;
        std::filesystem::remove(partial, ignored);
        throw;
    }
    std::error_code error;
    std::filesystem::rename(partial, path, error);
    if (error)
        throw std::runtime_error("cannot write " + path.string() + ": the finished file is kept as " +
                                 partial.string() + ", because renaming it failed: " + error.message());
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

void apply_segmental_setting(segmental::SegmentalConfig& config, const std::string& setting) {
    const std::size_t equals = setting.find('=');
    if (equals == std::string::npos || equals == 0)
        throw std::invalid_argument("--set needs NAME=VALUE, not '" + setting + "'");
    const std::string name = setting.substr(0, equals);
    const std::string text = setting.substr(equals + 1);
    const std::pair<const char*, double*> fields[] = {
        {"level_min_db_s500", &config.level_min_db_s500},
        {"level_max_db_s500", &config.level_max_db_s500},
        {"level_step_db", &config.level_step_db},
        {"level_grid_in_v", &config.level_grid_in_v},
        {"level_on_floor", &config.level_on_floor},
        {"level_noise_above_floor_max_db", &config.level_noise_above_floor_max_db},
        {"level_frame_s", &config.level_frame_s},
        {"level_lookahead_s", &config.level_lookahead_s},
        {"level_commit_s", &config.level_commit_s},
        {"key_switch_per_s", &config.key_switch_per_s},
        {"level_walk_nepers_per_sqrt_s", &config.level_walk_nepers_per_sqrt_s},
        {"level_jump_per_s", &config.level_jump_per_s},
        {"channel_floor_window_s", &config.channel_floor_window_s},
        {"channel_floor_quantile", &config.channel_floor_quantile},
        {"channel_floor_margin_db", &config.channel_floor_margin_db},
        {"leak_hold_s", &config.leak_hold_s},
        {"leak_floor_db", &config.leak_floor_db},
        {"leak_min_offset_hz", &config.leak_min_offset_hz},
        {"leak_max_offset_hz", &config.leak_max_offset_hz},
        {"leak_own", &config.leak_own},
        {"leak_own_hold_s", &config.leak_own_hold_s},
        {"channel_floor_stride_s", &config.channel_floor_stride_s},
        {"channel_floor_refresh_s", &config.channel_floor_refresh_s},
        {"channel_floor_start_s", &config.channel_floor_start_s},
        {"beam_nats", &config.beam_nats},
        {"front_taper_ratio", &config.front_taper_ratio},
        {"open_spacing_dots", &config.open_spacing_dots},
        {"ceiling_stretch_bonus_nats", &config.ceiling_stretch_bonus_nats},
        {"duration_z_max", &config.duration_z_max},
        {"rival_margin_nats", &config.rival_margin_nats},
        {"gap_unit_max_s", &config.gap_unit_max_s},
        {"filter_scale_min_symbols", &config.filter_scale_min_symbols},
        {"filter_scale_enough_nats", &config.filter_scale_enough_nats},
        {"filter_scale_start_s", &config.filter_scale_start_s},
        {"filter_scale_drop_after_s", &config.filter_scale_drop_after_s},
        {"filter_scale_drop_margin_nats", &config.filter_scale_drop_margin_nats},
        {"filter_scale_switch_nats", &config.filter_scale_switch_nats},
        {"gap_stretch_prior", &config.gap_stretch_prior},
    };
    for (const auto& [field, target] : fields) {
        if (name == field) {
            *target = std::stod(text);
            return;
        }
    }
    if (name == "ceiling") {
        config.ceiling = std::stoi(text);
        return;
    }
    if (name == "filter_scales") {  // a JSON list, or one number
        const ordered_json value = ordered_json::parse(text);
        config.filter_scales.clear();
        if (value.is_array()) {
            for (const auto& item : value) config.filter_scales.push_back(item.get<int>());
        } else {
            config.filter_scales.push_back(value.get<int>());
        }
        return;
    }
    if (name == "filter_scale_ceiling") {
        config.filter_scale_ceiling = std::stoi(text);
        return;
    }
    if (name == "gap_stretches") {
        config.gap_stretches = std::stoi(text);
        return;
    }
    if (name == "gap_stretch_speed_steps") {
        config.gap_stretch_speed_steps = std::stoi(text);
        return;
    }
    apply_setting(config.model, setting);
}

void apply_detector_setting(EngineConfig& config, const std::string& setting) {
    const std::size_t equals = setting.find('=');
    if (equals == std::string::npos || equals == 0)
        throw std::invalid_argument("--detector needs NAME=VALUE, not '" + setting + "'");
    const std::string name = setting.substr(0, equals);
    const double value = std::stod(setting.substr(equals + 1));
    DetectorConfig& d = config.detector;
    const std::pair<const char*, double*> doubles[] = {
        {"average_s", &d.average_s},
        {"birth_s", &d.birth_s},
        {"death_s", &d.death_s},
        {"attribution_distance_hz", &d.attribution_distance_hz},
        {"peak_radius_hz", &d.peak_radius_hz},
        {"level_radius_hz", &d.level_radius_hz},
        {"candidate_step_hz", &d.candidate_step_hz},
        {"local_floor_radius_hz", &d.local_floor_radius_hz},
        {"dc_guard_hz", &d.dc_guard_hz},
        {"prominence_radius_hz", &d.prominence_radius_hz},
        {"follow_radius_hz", &d.follow_radius_hz},
        {"sideband_radius_hz", &d.sideband_radius_hz},
        {"dc_block_s", &config.dc_block_s},
        {"skirt_exclusion_hz", &d.skirt_exclusion_hz},
        {"skirt_exclusion_radius_hz", &d.skirt_exclusion_radius_hz},
        {"copy_window_s", &config.copy_window_s},
        {"copy_rho", &config.copy_rho},
        {"copy_level_db", &config.copy_level_db},
        {"copy_radius_hz", &config.copy_radius_hz},
        {"copy_detrend_s", &config.copy_detrend_s},
        {"copy_gain_max", &config.copy_gain_max},
        {"copy_own_snr_max", &config.copy_own_snr_max},
        {"attribute_pad_s", &config.attribute_pad_s},
        {"attribute_rho", &config.attribute_rho},
        {"attribute_radius_hz", &config.attribute_radius_hz},
        {"attribute_own_max", &config.attribute_own_max},
        {"attribute_min_s", &config.attribute_min_s},
        {"attribute_history_s", &config.attribute_history_s},
    };
    for (const auto& [field, target] : doubles) {
        if (name == field) {
            *target = value;
            return;
        }
    }
    if (name == "threshold_db") d.threshold_db = static_cast<float>(value);
    else if (name == "hysteresis_db") d.hysteresis_db = static_cast<float>(value);
    else if (name == "prominence_db") d.prominence_db = static_cast<float>(value);
    else if (name == "sideband_db") d.sideband_db = static_cast<float>(value);
    else if (name == "sideband_mirror_db") d.sideband_mirror_db = static_cast<float>(value);
    else if (name == "skirt_exclusion_db") d.skirt_exclusion_db = static_cast<float>(value);
    else if (name == "fft_factor") d.fft_factor = static_cast<int>(value);
    else if (name == "floor_every_frames") d.floor_every_frames = static_cast<int>(value);
    else if (name == "max_tracks") d.max_tracks = static_cast<std::size_t>(value);
    else if (name == "one_track_per_peak") d.one_track_per_peak = value != 0.0;
    else if (name == "first_retirement_open") config.first_retirement_open = static_cast<int>(value);
    else if (name == "copy_older_only") config.copy_older_only = value != 0.0;
    else if (name == "copy_source_proven") config.copy_source_proven = value != 0.0;
    else if (name == "attribute_source_proven") config.attribute_source_proven = value != 0.0;
    else throw std::invalid_argument("--detector: unknown parameter '" + name + "'");
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
    probe.oracle_frequency_tracks.clear();
    probe.viterbi_genie.clear();
    EventBus bus;
    Engine engine(probe, bus);  // throws what the engine, the shared parts or the decoder refuse; discarded
}

// ---- The genie ----

GenieKind parse_genie_kind(const std::string& name) {
    if (name == "fist") return GenieKind::Fist;
    if (name == "fading") return GenieKind::Fading;
    if (name == "frequency")
        throw std::invalid_argument(
            "the frequency genie is removed: oracle channels follow the label's frequency (design Part 1)");
    throw std::invalid_argument("--genie '" + name + "': must be fist or fading");
}

const char* genie_kind_name(GenieKind kind) {
    switch (kind) {
        case GenieKind::Fist: return "fist";
        case GenieKind::Fading: return "fading";
    }
    return "?";
}

viterbi::ViterbiGenie genie_from_label(const LabeledSignal& label, std::size_t index, std::span<const GenieKind> kinds,
                                       double sample_interval_s) {
    const auto wants = [&](GenieKind kind) { return std::find(kinds.begin(), kinds.end(), kind) != kinds.end(); };
    const std::string who = "genie, signal " + std::to_string(index);
    const bool several_stations = label.sender_count > 1;
    const std::string stations = std::to_string(label.sender_count) + " stations, each with its own ";
    viterbi::ViterbiGenie genie;
    if (wants(GenieKind::Fading)) {
        if (label.per_station && !label.fading_hz_written)
            throw std::invalid_argument(who + ": a per-station label of a QSO written without fading_hz (by a "
                                              "generator that did not give each station its QSO's fading spread): "
                                              "whether its station fades is not known; make the labels again");
        genie.fading_spread_hz = label.fading_hz;
    }
    if (wants(GenieKind::Fist)) {
        if (several_stations)
            throw std::invalid_argument(who + ": the label holds " + stations +
                                        "fist; the genie fist needs the per-station labels");
        if (label.wpm_end && *label.wpm_end != label.wpm)
            throw std::invalid_argument(who + ": the speed changes from " + std::to_string(label.wpm) + " to " +
                                        std::to_string(*label.wpm_end) + " WPM; one dot cannot give both speeds");
        if (!(label.wpm > 0.0) || !std::isfinite(label.wpm))
            throw std::invalid_argument(who + ": the speed, " + std::to_string(label.wpm) + " WPM, is not a speed");
        if (!(sample_interval_s > 0.0) || !std::isfinite(sample_interval_s))
            throw std::invalid_argument(who + ": sample_interval_s must be positive and finite (s)");
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
            // At least one sample over the median length, in ln(length): the floor of the header (class: heuristic; the
            // fist needs sigma > 0, and "machine" is exact, sigma 0, so the sender's timing is taken as known to one
            // sample, 2 ms at the default; neither derived nor measured).
            fist.sigmas[kind] = std::max(duration.sigma, sample_interval_s / (duration.median_dots * dot_s));
            // the marks are longer, the spaces shorter, by the imbalance; in the generator the Farnsworth factor
            // does not stretch it (0.0 - x, not -x: no imbalance is a shift of 0, not of -0)
            fist.shifts_dots[kind] = kind < 2 ? label.imbalance_dits : 0.0 - label.imbalance_dits;
        }
        genie.fist = fist;
    }
    return genie;
}

ordered_json to_json(const viterbi::ViterbiGenie& genie) {
    ordered_json j = ordered_json::object();
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

// ---- The oracle's frequency ----

std::vector<OracleSegment> oracle_frequency_track(const LabeledSignal& label, std::size_t index) {
    if (label.per_station && label.drift_hz_per_s != 0.0)
        throw std::invalid_argument("oracle frequency, signal " + std::to_string(index) +
                                    ": a per-station label of a QSO that drifts (" +
                                    std::to_string(label.drift_hz_per_s) +
                                    " Hz/s): its start_s is the station's first over, not the QSO's start from which "
                                    "the drift is measured, so its carrier is not known");
    // A QSO on one track: the over in progress, at its sender's carrier, from each transmission's start (the first
    // from 0 s), held through the silence after it.
    if (label.sender_count > 0 && !label.transmissions.empty()) {
        std::vector<OracleSegment> track;
        for (std::size_t i = 0; i < label.transmissions.size(); ++i) {
            const Transmission& t = label.transmissions[i];
            track.push_back({i == 0 ? 0.0 : t.start_s, label.freq_offset_hz + t.offset_hz, label.drift_hz_per_s,
                             label.start_s});
        }
        return track;
    }
    // One station: its frequency at start_s with its drift from there, from 0 s.
    return {{0.0, label.freq_offset_hz, label.drift_hz_per_s, label.start_s}};
}

// ---- The rules among the options ----

void check_viterbi_options(bool decoder_is_viterbi, bool oracle, const ViterbiOptions& options) {
    if (options.stats_detail && !options.stats)
        throw std::invalid_argument("--stats-detail needs --stats FILE (it asks for the statistics file's full form)");
    if (!decoder_is_viterbi && !options.settings.empty())
        throw std::invalid_argument("--set belongs to --decoder viterbi (the other decoders take no settings here)");
    if (!decoder_is_viterbi && !options.genie.empty())
        throw std::invalid_argument("--genie belongs to --decoder viterbi");
    if (!decoder_is_viterbi && options.stats)
        throw std::invalid_argument("--stats belongs to --decoder viterbi (it writes the new decoder's statistics)");
    if (!decoder_is_viterbi && !options.omega_bar_default_table.empty())
        throw std::invalid_argument(
            "--omega-bar-default-table belongs to --decoder viterbi (it is the new decoder's Omega_bar table)");
    if (!options.genie.empty() && !oracle)
        throw std::invalid_argument(
            "--genie needs --oracle: its values come from the labels of the oracle channels, one set for each");
    if (options.stats && !oracle)
        throw std::invalid_argument(
            "--stats needs --oracle: the statistics of a channel are read when the run ends, and a channel of the "
            "detector path is gone once its track dies");
}

// ---- The statistics file ----

StatsFile collect_stats_file(const Engine& engine, const std::string& recording, const viterbi::ViterbiConfig& config,
                             std::size_t channels) {
    StatsFile file;
    file.recording = recording;
    file.config = config;
    for (std::size_t i = 0; i < channels; ++i) {
        const auto track = static_cast<std::uint32_t>(i + 1);
        ChannelStats channel;
        channel.track_id = track;
        channel.label_index = static_cast<int>(i);
        channel.stats = engine_stats(engine, track);
        file.channels.push_back(std::move(channel));
    }
    return file;
}

ordered_json to_json(const ChannelStats& channel) {
    return channel_json(channel.track_id, channel.label_index, channel.stats);
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
    s.samples = integer(member(j, "samples", at), "samples", at);
    s.ceiling_samples = integer(member(j, "ceiling_samples", at), "ceiling_samples", at);
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
                       std::to_string(kStatsVersion) + " (every line)" +
                       (version == kStatsSummaryVersion ? "; version 2 is the summary, for stats_summary_file_from_json"
                                                        : ""));
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
    // lines per sample, so all of them at once would be large); the file is written under a name of its own and
    // renamed when complete, so that a statistic that is not finite, or a full disk, leaves no file that looks
    // complete (as the replay tool writes its decoded files).
    write_stats_stream(path, header_json(file), file.channels.size(),
                       [&](std::size_t i) { return to_json(file.channels[i]); });
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

// ---- The statistics file, version 2: the summary ----

ChannelSummary summarize_channel(std::uint32_t track_id, std::optional<int> label_index,
                                 const viterbi::SearchStats& stats, double sample_interval_s) {
    if (!(sample_interval_s > 0.0) || !std::isfinite(sample_interval_s))
        throw std::invalid_argument("statistics: sample_interval_s must be positive and finite (s)");
    ChannelSummary summary;
    summary.track_id = track_id;
    summary.label_index = label_index;
    summary.samples = stats.samples;
    summary.ceiling_samples = stats.ceiling_samples;
    summary.live_histogram = stats.live_histogram;
    // The live series is kept in windows of round(0.1 s / sample_interval_s) samples, the last window holding the rest of the
    // samples (search.cpp, update_stats): the mean over the samples weights each window's mean by its samples.
    const std::int64_t window = std::max<std::int64_t>(1, std::llround(kStatsWindowS / sample_interval_s));
    const auto windows = static_cast<std::int64_t>(stats.live_mean_100ms.size());
    if (stats.live_max_100ms.size() != stats.live_mean_100ms.size())
        throw std::invalid_argument("statistics: " + std::to_string(stats.live_mean_100ms.size()) +
                                    " windows of means but " + std::to_string(stats.live_max_100ms.size()) +
                                    " of maxima");
    const std::int64_t last = stats.samples - window * (windows - 1);
    if ((stats.samples == 0) != (windows == 0) || (windows > 0 && (last < 1 || last > window)))
        throw std::invalid_argument("statistics: " + std::to_string(windows) + " windows of " + std::to_string(window) +
                                    " samples do not cover the " + std::to_string(stats.samples) + " samples searched");
    double weighted = 0.0;
    for (std::int64_t w = 0; w < windows; ++w)
        weighted += static_cast<double>(stats.live_mean_100ms[static_cast<std::size_t>(w)]) *
                    static_cast<double>(w + 1 < windows ? window : last);
    summary.live_mean_stories = stats.samples > 0 ? weighted / static_cast<double>(stats.samples) : 0.0;
    for (const float x : stats.live_max_100ms)
        summary.live_max_stories = std::max(summary.live_max_stories, static_cast<double>(x));
    summary.lines = static_cast<std::int64_t>(stats.lines.size());
    for (std::size_t i = 0; i < stats.lines.size(); ++i) {
        const viterbi::NewbornLine& line = stats.lines[i];
        const int kind = static_cast<int>(line.kind), end = static_cast<int>(line.end);
        if (kind < 0 || kind >= kLineKinds || end < 0 || end >= kLineEnds)
            throw std::invalid_argument("statistics: line " + std::to_string(i) +
                                        " has a kind or an end that is not in the legends");
        const bool led = line.first_led_s.has_value();
        ++summary.fates[static_cast<std::size_t>(kind)][static_cast<std::size_t>(end)]
                       [line.ended_in_first_element ? 1u : 0u][led ? 1u : 0u];
        if (led) {
            summary.led_line_ids.push_back(static_cast<std::int64_t>(i));
            summary.led_lines.push_back(line);
        }
    }
    return summary;
}

ChannelSummary summarize(const ChannelStats& channel, double sample_interval_s) {
    return summarize_channel(channel.track_id, channel.label_index, channel.stats, sample_interval_s);
}

StatsSummaryFile summarize(const StatsFile& file) {
    StatsSummaryFile out;
    out.recording = file.recording;
    out.config = file.config;
    for (const ChannelStats& channel : file.channels) out.channels.push_back(summarize(channel, file.config.sample_interval_s));
    return out;
}

StatsSummaryFile collect_stats_summary(const Engine& engine, const std::string& recording,
                                       const viterbi::ViterbiConfig& config, std::size_t channels) {
    StatsSummaryFile file;
    file.recording = recording;
    file.config = config;
    for (std::size_t i = 0; i < channels; ++i) {
        const auto track = static_cast<std::uint32_t>(i + 1);
        file.channels.push_back(
            summarize_channel(track, static_cast<int>(i), engine_stats(engine, track), config.sample_interval_s));
    }
    return file;
}

ordered_json to_json(const ChannelSummary& c) {
    if (c.led_line_ids.size() != c.led_lines.size())
        throw std::invalid_argument("statistics: " + std::to_string(c.led_line_ids.size()) + " line ids for " +
                                    std::to_string(c.led_lines.size()) + " lines that led");
    ordered_json j = ordered_json::object();
    j["track_id"] = c.track_id;
    j["label_index"] = c.label_index ? ordered_json(*c.label_index) : ordered_json(nullptr);
    j["samples"] = c.samples;
    j["ceiling_samples"] = c.ceiling_samples;
    ordered_json histogram = ordered_json::array();
    for (const std::int64_t count : c.live_histogram) histogram.push_back(count);
    j["live_histogram"] = std::move(histogram);
    j["live_mean_stories"] = finite_or_throw(c.live_mean_stories, "live_mean_stories");
    j["live_max_stories"] = finite_or_throw(c.live_max_stories, "live_max_stories");
    j["lines"] = c.lines;
    ordered_json fates = ordered_json::array();
    for (const auto& of_kind : c.fates) {  // the fates of one kind, an array indexed by end
        ordered_json ends = ordered_json::array();
        for (const auto& of_end : of_kind) {  // of one kind and end, indexed by ended_in_first_element
            ordered_json firsts = ordered_json::array();
            for (const auto& of_first : of_end)  // of one kind, end and first-element flag, indexed by led
                firsts.push_back(ordered_json::array({of_first[0], of_first[1]}));
            ends.push_back(std::move(firsts));
        }
        fates.push_back(std::move(ends));
    }
    j["fates"] = std::move(fates);
    ordered_json line = ordered_json::array(), birth_s = ordered_json::array(), kind = ordered_json::array(),
                 first_led_s = ordered_json::array(), deficit = ordered_json::array(), end = ordered_json::array(),
                 end_s = ordered_json::array(), first_element = ordered_json::array();
    for (std::size_t i = 0; i < c.led_lines.size(); ++i) {
        const viterbi::NewbornLine& led = c.led_lines[i];
        line.push_back(c.led_line_ids[i]);
        birth_s.push_back(finite_or_throw(led.birth_s, "birth_s"));
        kind.push_back(static_cast<int>(led.kind));
        first_led_s.push_back(optional_time(led.first_led_s, "first_led_s"));
        deficit.push_back(finite_or_throw(led.largest_deficit_nats, "largest_deficit_nats"));
        end.push_back(static_cast<int>(led.end));
        end_s.push_back(optional_time(led.end_s, "end_s"));
        first_element.push_back(led.ended_in_first_element);
    }
    ordered_json columns = ordered_json::object();
    columns["line"] = std::move(line);
    columns["birth_s"] = std::move(birth_s);
    columns["kind"] = std::move(kind);
    columns["first_led_s"] = std::move(first_led_s);
    columns["largest_deficit_nats"] = std::move(deficit);
    columns["end"] = std::move(end);
    columns["end_s"] = std::move(end_s);
    columns["ended_in_first_element"] = std::move(first_element);
    j["led_lines"] = std::move(columns);
    return j;
}

ordered_json to_json(const StatsSummaryFile& file) {
    ordered_json j = header_json(file);
    ordered_json channels = ordered_json::array();
    for (const ChannelSummary& channel : file.channels) channels.push_back(to_json(channel));
    j["channels"] = std::move(channels);
    return j;
}

ChannelSummary channel_summary_from_json(const ordered_json& j) {
    const std::string where = "channel";
    ChannelSummary c;
    const std::int64_t id = integer(member(j, "track_id", where), "track_id", where);
    if (id < 0 || id > std::numeric_limits<std::uint32_t>::max()) bad(where, "track_id is out of range");
    c.track_id = static_cast<std::uint32_t>(id);
    const std::string at = "channel " + std::to_string(c.track_id);
    const ordered_json& label = member(j, "label_index", at);
    if (!label.is_null()) c.label_index = static_cast<int>(integer(label, "label_index", at));
    c.samples = integer(member(j, "samples", at), "samples", at);
    c.ceiling_samples = integer(member(j, "ceiling_samples", at), "ceiling_samples", at);
    std::int64_t in_histogram = 0;
    for (const ordered_json& count : array_member(j, "live_histogram", at)) {
        const std::int64_t n = integer(count, "live_histogram", at);
        if (n < 0) bad(at, "live_histogram holds a negative count");
        c.live_histogram.push_back(n);
        in_histogram += n;
    }
    // each sample is in one bin
    if (in_histogram != c.samples)
        bad(at, "live_histogram counts " + std::to_string(in_histogram) + " samples, not the " + std::to_string(c.samples) +
                    " searched");
    c.live_mean_stories = number(member(j, "live_mean_stories", at), "live_mean_stories", at);
    c.live_max_stories = number(member(j, "live_max_stories", at), "live_max_stories", at);
    c.lines = integer(member(j, "lines", at), "lines", at);
    // the fates: 3 x 4 x 2 x 2 counts
    const ordered_json& fates = array_member(j, "fates", at);
    if (fates.size() != static_cast<std::size_t>(kLineKinds))
        bad(at, "fates must hold " + std::to_string(kLineKinds) + " kinds");
    std::int64_t in_fates = 0, led_in_fates = 0;
    for (int k = 0; k < kLineKinds; ++k) {
        const ordered_json& by_end = fates[static_cast<std::size_t>(k)];
        if (!by_end.is_array() || by_end.size() != static_cast<std::size_t>(kLineEnds))
            bad(at, "fates[" + std::to_string(k) + "] must hold " + std::to_string(kLineEnds) + " ends");
        for (int e = 0; e < kLineEnds; ++e) {
            const ordered_json& by_first = by_end[static_cast<std::size_t>(e)];
            if (!by_first.is_array() || by_first.size() != 2)
                bad(at, "fates[" + std::to_string(k) + "][" + std::to_string(e) + "] must hold 2 values of ended_in_first_element");
            for (int f = 0; f < 2; ++f) {
                const ordered_json& by_led = by_first[static_cast<std::size_t>(f)];
                const std::string name =
                    "fates[" + std::to_string(k) + "][" + std::to_string(e) + "][" + std::to_string(f) + "]";
                if (!by_led.is_array() || by_led.size() != 2) bad(at, name + " must hold 2 values of led");
                for (int l = 0; l < 2; ++l) {
                    const std::string count_name = name + "[" + std::to_string(l) + "]";
                    const std::int64_t n = integer(by_led[static_cast<std::size_t>(l)], count_name, at);
                    if (n < 0) bad(at, count_name + " is negative");
                    c.fates[static_cast<std::size_t>(k)][static_cast<std::size_t>(e)][static_cast<std::size_t>(f)]
                           [static_cast<std::size_t>(l)] = n;
                    in_fates += n;
                    if (l == 1) led_in_fates += n;
                }
            }
        }
    }
    if (in_fates != c.lines)
        bad(at, "the fates add up to " + std::to_string(in_fates) + " lines, not " + std::to_string(c.lines));
    // the lines that led: columns of equal length, the ids ascending and below the number of lines
    const ordered_json& led = member(j, "led_lines", at);
    const std::string in_led = at + ", led_lines";
    const ordered_json& ids = array_member(led, "line", in_led);
    const ordered_json& birth_s = array_member(led, "birth_s", in_led);
    const ordered_json& kind = array_member(led, "kind", in_led);
    const ordered_json& first_led_s = array_member(led, "first_led_s", in_led);
    const ordered_json& deficit = array_member(led, "largest_deficit_nats", in_led);
    const ordered_json& end = array_member(led, "end", in_led);
    const ordered_json& end_s = array_member(led, "end_s", in_led);
    const ordered_json& first_element = array_member(led, "ended_in_first_element", in_led);
    const std::size_t n = ids.size();
    if (birth_s.size() != n || kind.size() != n || first_led_s.size() != n || deficit.size() != n || end.size() != n ||
        end_s.size() != n || first_element.size() != n)
        bad(in_led, "the columns have different lengths");
    if (static_cast<std::int64_t>(n) != led_in_fates)
        bad(at, "the fates count " + std::to_string(led_in_fates) + " lines that led, led_lines holds " +
                    std::to_string(n));
    std::int64_t previous = -1;
    std::int64_t led_tally[kLineKinds][kLineEnds][2] = {};  // the lines kept, by kind, end and first element
    for (std::size_t i = 0; i < n; ++i) {
        const std::int64_t line_id = integer(ids[i], "line", in_led);
        if (line_id <= previous)
            bad(in_led, "line ids must ascend (line " + std::to_string(line_id) + " after " + std::to_string(previous) + ")");
        if (line_id >= c.lines)
            bad(in_led, "line " + std::to_string(line_id) + " is not below the channel's " + std::to_string(c.lines) +
                            " lines");
        previous = line_id;
        viterbi::NewbornLine line;
        line.birth_s = number(birth_s[i], "birth_s", in_led);
        const std::int64_t k = integer(kind[i], "kind", in_led);
        if (k < 0 || k >= kLineKinds) bad(in_led, "kind " + std::to_string(k) + " is not in line_kinds");
        line.kind = static_cast<viterbi::LineKind>(k);
        if (first_led_s[i].is_null()) bad(in_led, "first_led_s is null: only lines that led are kept here");
        line.first_led_s = number(first_led_s[i], "first_led_s", in_led);
        line.largest_deficit_nats = number(deficit[i], "largest_deficit_nats", in_led);
        const std::int64_t e = integer(end[i], "end", in_led);
        if (e < 0 || e >= kLineEnds) bad(in_led, "end " + std::to_string(e) + " is not in line_ends");
        line.end = static_cast<viterbi::LineEnd>(e);
        if (!end_s[i].is_null()) line.end_s = number(end_s[i], "end_s", in_led);
        if (!first_element[i].is_boolean()) bad(in_led, "ended_in_first_element must be true or false");
        line.ended_in_first_element = first_element[i].get<bool>();
        ++led_tally[k][e][line.ended_in_first_element ? 1 : 0];
        c.led_line_ids.push_back(line_id);
        c.led_lines.push_back(line);
    }
    // the lines kept agree with the fates' led counts, kind by kind, end by end, first element by first element (the
    // count of them alone, checked above, would pass columns that contradict the table)
    for (int k = 0; k < kLineKinds; ++k)
        for (int e = 0; e < kLineEnds; ++e)
            for (int f = 0; f < 2; ++f) {
                const std::int64_t counted = c.fates[static_cast<std::size_t>(k)][static_cast<std::size_t>(e)]
                                                    [static_cast<std::size_t>(f)][1];
                if (led_tally[k][e][f] != counted)
                    bad(at, "led_lines holds " + std::to_string(led_tally[k][e][f]) + " line(s) of kind " +
                                kLineKindNames[k] + " that ended " + kLineEndNames[e] + (f == 1 ? " within" : " after") +
                                " their first element, but fates[" + std::to_string(k) + "][" + std::to_string(e) + "][" +
                                std::to_string(f) + "][1] counts " + std::to_string(counted));
            }
    return c;
}

StatsSummaryFile stats_summary_file_from_json(const ordered_json& j) {
    const std::string where;
    const ordered_json& format = member(j, "format", where);
    if (!format.is_string() || format.get<std::string>() != kStatsFormat)
        bad(where, std::string("\"format\" must be \"") + kStatsFormat + "\"");
    const std::int64_t version = integer(member(j, "version", where), "version", where);
    if (version != kStatsSummaryVersion)
        bad(where, "version " + std::to_string(version) + " is not the version this reader reads, " +
                       std::to_string(kStatsSummaryVersion) + " (the summary)" +
                       (version == kStatsVersion ? "; version 1 holds every line, for stats_file_from_json" : ""));
    StatsSummaryFile file;
    const ordered_json& recording = member(j, "recording", where);
    if (!recording.is_string()) bad(where, "\"recording\" must be text");
    file.recording = recording.get<std::string>();
    file.config = viterbi_config_from_json(member(j, "config", where));
    for (const ordered_json& channel : array_member(j, "channels", where))
        file.channels.push_back(channel_summary_from_json(channel));
    return file;
}

void write_stats_summary_file(const std::filesystem::path& path, const StatsSummaryFile& file) {
    write_stats_stream(path, header_json(file), file.channels.size(),
                       [&](std::size_t i) { return to_json(file.channels[i]); });
}

StatsSummaryFile read_stats_summary_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot open " + path.string());
    ordered_json j;
    try {
        j = ordered_json::parse(in);
    } catch (const ordered_json::exception& e) {
        throw std::invalid_argument("statistics file " + path.string() + " is not JSON: " + e.what());
    }
    return stats_summary_file_from_json(j);
}

// ---- Writing a run's statistics ----

void write_run_stats(const std::filesystem::path& path, const Engine& engine, const std::string& recording,
                     const viterbi::ViterbiConfig& config, std::size_t channels, StatsDetail detail) {
    const auto track_of = [](std::size_t i) { return static_cast<std::uint32_t>(i + 1); };
    if (detail == StatsDetail::Lines) {
        write_stats_stream(path, header_json(kStatsVersion, recording, config), channels, [&](std::size_t i) {
            return channel_json(track_of(i), static_cast<int>(i), engine_stats(engine, track_of(i)));
        });
    } else {
        write_stats_stream(path, header_json(kStatsSummaryVersion, recording, config), channels, [&](std::size_t i) {
            return to_json(
                summarize_channel(track_of(i), static_cast<int>(i), engine_stats(engine, track_of(i)), config.sample_interval_s));
        });
    }
}

}  // namespace kz4ap::bench
