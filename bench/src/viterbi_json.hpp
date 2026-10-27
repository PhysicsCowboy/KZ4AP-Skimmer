// The new decoder (viterbi) in the bench: its configuration as JSON and as `--set NAME=VALUE`, the genie values read
// from the labels (`--genie`), the rules among those options, and the statistics file (`--stats FILE`). Bench only:
// the engine library does not link nlohmann/json.
//
// THE CONFIGURATION. to_json(ViterbiConfig) writes every field by its C++ name (engine/include/kz4ap/viterbi/
// config.hpp) in declaration order, each in its own physical unit: the field's comment says which, and most names end
// in it (_s seconds, _ms milliseconds, _hz hertz, _db decibels with the reference the comment names, _nats nats, _dots
// dots). A viterbi run's result file holds that object as "config" (its keys sorted, as the result file sorts all of
// them), so the run can be repeated from the file; viterbi_config_from_json reads it back.
// `--set NAME=VALUE` sets one field, repeatably, before the run. VALUE is JSON if it parses (30, 1e-3,
// [0.3, 0.3, 0.3, 0.3, 0.3], true, null) and text otherwise, as kz4ap-bank-replay's --set reads its values, and its
// kind must fit the field: a number for a double, a whole number for an int, an array of exactly five numbers for the
// fist's arrays, a number or null (none) for the genie's optional values, true or false for stats. A name that is not
// a field, or a value of the wrong kind, is an error that names the field. The ranges are the library's own: the
// engine, the shared parts and the decoder refuse what they cannot use when they are built (std::invalid_argument,
// with their message), so the bench copies none of their rules.
//
// THE ORACLE'S FREQUENCY (--oracle --decoder viterbi). Each oracle channel follows its label's true frequency (design
// Part 1; plan O3), given to the engine as EngineConfig::oracle_frequency_tracks, one track per label, in the order of
// the labels (oracle_frequency_track): for a label of one station one segment from 0 s, its freq_offset_hz in Hz from
// the span's center at its start_s and its drift_hz_per_s in Hz/s from there, so that the carrier is at freq_offset_hz +
// drift_hz_per_s x (t - start_s) at stream time t, in s, as the generator makes it (the angle is 2 pi f t + pi drift
// (t - start_s)^2); for a QSO label (its "senders") one segment per transmission, from the transmission's start_s (the
// first from 0 s), at freq_offset_hz + the sender's offset_hz with the QSO's drift from the QSO's start_s: the over in
// progress, held through the silence after it. A per-station label of a QSO that drifts is refused, naming its number in
// the labels file: its start_s is the station's first over, not the QSO's start from which the drift is measured. The
// engine evaluates the track at each channel block's start and holds it over the block. The frequency genie that gave
// the decoder the carrier (--genie frequency) is removed, and refused with that reason.
//
// THE GENIE (--genie fist|fading, repeatable, with --oracle). genie_from_label reads, from the label of an
// oracle channel's station, the truth the decoder is given in place of what it estimates, in the form the engine takes
// (EngineConfig::viterbi_genie, one entry per oracle channel, in the order of the labels):
//   fading     the label's fading_hz, the Rayleigh fading's frequency spread f_D in Hz (twice its standard deviation),
//              which is 0 for a signal that does not fade: that is the truth, and it is given (a block is then the
//              whole mark);
//   fist       the sender's timing: the dot of the label's wpm, 1200 / wpm ms; for each of the five kinds (dot, dash,
//              element gap, letter gap, word gap) the median in dots and the standard deviation of ln(length) of the
//              label's keying style (a copy of kz4ap_synth.keying.STYLES in viterbi_json.cpp, which
//              training/tests/test_keying.py checks); and the shifts in dots, + imbalance_dits for the marks and
//              - imbalance_dits for the gaps (the Farnsworth factor does not stretch them, as in the generator), where
//              length = shift + median x exp(sigma N).
//              The fist needs sigma > 0, and the style "machine" is exact (sigma 0); the decoder cannot resolve a
//              length better than a piece anyway. So each sigma is at least one piece over the kind's median length,
//              piece_s / (median_dots x dot), in ln(length). CLASS: heuristic (an exact sender's timing known to one
//              piece, 2 ms at the default; neither derived nor measured). It also raises the small sigmas of the
//              other styles at fast speeds (the "computer" style at 100 WPM, for one). The decoder has no Farnsworth
//              factor (standard spacing only, the owner's decision D14 of 2026-10-21) and the labels' farnsworth_wpm
//              is not read: for a Farnsworth-spaced signal the genie fist gives the style's standard medians, and
//              its stretched gaps decode poorly (docs/backlog.md, "Farnsworth spacing in the new decoder").
// A label that has no single truth to give is refused, naming its number in the labels file: a QSO label of several
// stations for fist (each station has its own fist; the per-station labels of a QSO have one), and a signal that
// changes speed (wpm_end) for fist (one dot cannot give both speeds).
//
// THE STATISTICS FILE (--stats FILE, with --stats-detail), format "kz4ap-viterbi-search-stats": the decoder's
// SearchStats (engine/include/kz4ap/viterbi/search.hpp) for every channel of a run, which the search keeps when
// ViterbiConfig::stats is true (kz4ap-bench sets it for a run that writes the file). A run writes it at its end, from
// Engine::viterbi_stats, for its oracle channels (a channel of the detector path is gone once its track dies, so
// --stats needs --oracle), after it has written the result file (--json), so that a failure to write the statistics
// loses nothing of the run (so --json and --stats may not name one file: the bench refuses that at once); both files
// are written under a name of their own and renamed when complete, and whether each can be created is tried before the
// engine runs; a file that is whole but cannot be renamed stays as FILE.partial, and the message says so. One JSON
// object, ASCII, with one line for each top-level key and for each channel; the Python analysis reads it with
// json.load (training/kz4ap_proto/search_stats.py, which also holds the reductions the runs need: they define what
// version 2 keeps). The numbers are exact: a double is written with the digits that read back to the same double and a
// float as the double it equals.
//
// Two versions of the one format; a reader refuses a version it does not read. A version changes when a key's meaning
// changes or a key is removed; keys may be added to the file's or a channel's object within a version, and a reader
// ignores the ones it does not know (this C++ reader reads "config" as a ViterbiConfig it knows).
//   version 2, the default of --stats: a summary for each channel (below), which holds what the reductions of the plan's
//     runs R0 to R4 need and no record of a line except of the lines that ever led: a header of 3.7 kB and, for each
//     channel, about 1.15 kB (0.9 to 1.75 kB) whatever its length, about 60 bytes of it for each line that led
//     (measured on a real recording of 14 channels, 307.9 channel-seconds, DEV2's group A2, the file's own bytes: 19.8
//     kB, where version 1 took 14.0 MB); one run over all of DEV2 (14 204 channels in 498 recordings) is about 18 MB.
//   version 1, --stats-detail: every newborn line of each channel (NewbornLine), as columns. The search creates a
//     Silence line and a Rescue line at every piece, so the file holds two lines per piece, always: 45 kB per channel-
//     second, whatever the signal (measured: 45.5 kB on the real recording above, 45.4 kB on a recording of two
//     stations), 164 MB per channel-hour. For a small run (R0, diagnosis); one run over all of DEV2 would be 48 GB.
//
// The keys both versions share:
//   "format"       "kz4ap-viterbi-search-stats"
//   "version"      1 or 2
//   "recording"    the recording's file name
//   "config"       the ViterbiConfig the run used, as the result file's "config": every field by its C++ name, in its
//                  unit. It is the run's base configuration: in a --genie run each channel ran with its own genie
//                  values (the true speed and fist or fading spread of its station), which only the result file's
//                  "genie" records
//   "fields"       what each key of a channel is: "SearchStats" (version 1) or "ChannelSummary" (version 2) lists the
//                  members of a channel and "NewbornLine" those of a line, each by its name, as a string that starts
//                  with the unit or the kind of the value (count, counts, stories, s, nats, a code into "line_kinds"
//                  or "line_ends", true or false, columns) and says what the member is
//   "line_kinds"   the names of LineKind (Start, Silence, Rescue) in the order of its values: "kind" is an index
//   "line_ends"    the names of LineEnd (Alive, Merge, Beam, Ceiling) in the order of its values: "end" is an index
//   "channels"     an array with one object per channel, in the order the run gives them (oracle: the labels')
// A channel's "track_id" is the engine's track id (an oracle channel i has the id i + 1), its "label_index" the label's
// index (oracle: track_id - 1) or null where there is no label, "pieces" (count) the pieces searched, each config.piece_s
// long, "ceiling_pieces" (count) those at which the ceiling dropped at least one story, and "live_histogram" (counts)
// the pieces by the number of live stories after pruning, in bins of 10 (entry k counts the pieces with 10 k to
// 10 k + 9 live stories). The times are stream times, in s, of the ends of pieces; a line's deficits are relative to the
// best story at their piece. Version 1 adds:
//     "live_mean_100ms", "live_max_100ms"  stories: the mean and the largest live count after pruning in each window of
//                      round(0.1 s / piece_s) = 50 pieces, from the channel's first piece; the last entry covers the
//                      pieces of its window searched so far
//     "lines"          the newborn lines in the order of their ids (the position is the id; line 0 is the channel's
//                      first story), as columns named by NewbornLine's members, each an array with one entry per line:
//                      "birth_s" s; "kind" an index into "line_kinds"; "first_led_s" s or null; "largest_deficit_nats"
//                      nats; "end" an index into "line_ends"; "end_s" s or null; "ended_in_first_element" true or false
// Columns, not one object per line: seven member names per line would triple the file. A column array becomes a data
// frame column in one step (pandas.DataFrame(channel["lines"])). Version 2 adds, and the header gains "fate_axes":
//     "live_mean_stories"  stories: the mean number of live stories after pruning over the channel's pieces, the 0.1 s
//                      windows' means weighted by their pieces (the last window holds the rest of the pieces)
//     "live_max_stories"   stories: the largest live count after pruning at any piece
//     "lines"          count: the newborn lines (version 1's column length)
//     "fates"          counts of the lines by fates[kind][end][ended_in_first_element][led]: kind and end are indices
//                      into "line_kinds" and "line_ends", the last two axes 0 (false) or 1 (true), "led" whether
//                      first_led_s is not null; 3 x 4 x 2 x 2 integers, whose axes the header's "fate_axes" names
//     "led_lines"      columns: the lines that ever led, as the columns of version 1's "lines" and one more, "line": the
//                      line's id in version 1's order (ascending); "first_led_s" is never null here
// WHY these lines and no others: a line that never led never had the best story at any piece, so it never supplied the
// text the decoder would have printed at any moment; R1 needs each deficit of the newborns that led (their maximum and
// upper quantiles: no counter gives them), and there are few of them (two for a channel of one station, measured); the
// fates count the rest by kind, end, first element and led, for R0 (the newborns' fates), R2 (the newborns the beam
// drops within their first element) and R3 (the newborns the ceiling removes). The reductions give the same numbers
// from either version (training/tests/test_search_stats.py; a test here against the same two committed files).
#pragma once

#include "labels.hpp"

#include "kz4ap/engine.hpp"
#include "kz4ap/viterbi/config.hpp"
#include "kz4ap/viterbi/search.hpp"

#include <nlohmann/json.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace kz4ap::bench {

// ---- The configuration ----

// Every field of the configuration by its C++ name, in declaration order (see above).
nlohmann::ordered_json to_json(const viterbi::ViterbiConfig& config);

// The names of the fields, in declaration order.
const std::vector<std::string>& viterbi_config_field_names();

// Sets one field by its name from a JSON value of the kind the field needs (see above). Throws std::invalid_argument
// for a name that is not a field ("unknown parameter 'NAME'") and for a value of the wrong kind (naming the field),
// and then changes nothing.
void set_config_value(viterbi::ViterbiConfig& config, const std::string& name, const nlohmann::ordered_json& value);

// One `--set` argument, "NAME=VALUE": VALUE is JSON if it parses, else text (see above). Throws std::invalid_argument
// for an argument without a "=" or with an empty name, and as set_config_value.
void apply_setting(viterbi::ViterbiConfig& config, const std::string& setting);

// A configuration from the defaults with every field of the object j set (a result file's "config"); throws
// std::invalid_argument as set_config_value, and for a j that is not an object.
viterbi::ViterbiConfig viterbi_config_from_json(const nlohmann::ordered_json& j);

// Builds, and discards, an engine of `config` with the new decoder and one oracle channel, so that every check that the
// engine, the shared parts and the decoder make of the configuration is made now, with their own messages
// (std::invalid_argument), instead of when the first track is born in the middle of a recording on the detector path.
// The engine builds the shared parts from its own channelizer's filter, as for its own decoders. Takes
// config.decoder.front_end as FrontEnd::Viterbi and ignores config.oracle_frequencies_hz, config.oracle_frequency_tracks
// and config.viterbi_genie (the probe has one oracle channel at 0 Hz, with no track and no genie).
void check_engine_accepts(const EngineConfig& config);

// ---- The genie ----

enum class GenieKind { Fist, Fading };

// "fist" or "fading"; throws std::invalid_argument for anything else ("frequency": "the frequency genie is removed:
// oracle channels follow the label's frequency (design Part 1)").
GenieKind parse_genie_kind(const std::string& name);
const char* genie_kind_name(GenieKind kind);

// The genie values of one oracle channel from the label of its station, the parts `kinds` names (see above). `index` is
// the label's number in the labels file, for the messages; piece_s, in s, is the configuration's piece, for the fist's
// smallest sigma. Throws std::invalid_argument, naming the signal, for a label that has no single truth to give, a
// keying style that the table does not have, and a speed or piece that is not positive and finite.
viterbi::ViterbiGenie genie_from_label(const LabeledSignal& label, std::size_t index, std::span<const GenieKind> kinds,
                                       double piece_s);

// A channel's genie values as the result file records them: fading_spread_hz (or null) and fist (or null; else dot_ms,
// medians_dots, sigmas, shifts_dots).
nlohmann::ordered_json to_json(const viterbi::ViterbiGenie& genie);

// ---- The oracle's frequency ----

// The track of the oracle channel of one label (see above); `index` is the label's number in the labels file, for the
// message. Throws std::invalid_argument, naming the signal, for a per-station label of a QSO that drifts.
std::vector<OracleSegment> oracle_frequency_track(const LabeledSignal& label, std::size_t index);

// ---- The rules among the options ----

struct ViterbiOptions {
    std::vector<std::string> settings;  // each --set, as given ("NAME=VALUE")
    std::vector<GenieKind> genie;       // --genie, each kind once, in the order first given
    bool stats = false;                 // --stats was given
    bool stats_detail = false;          // --stats-detail was given: the statistics file in its full form (version 1)
    std::string omega_bar_default_table;  // --omega-bar-default-table FILE, or empty (the compiled table)
};

// The rules between the bench's options: --set, --genie, --stats and --omega-bar-default-table belong to --decoder
// viterbi; --genie takes its values from the labels of the oracle channels, and --stats reads the statistics of the
// oracle channels at the end of the run (a channel of the detector path is gone once its track dies), so both need
// --oracle; --stats-detail changes what --stats writes, so it needs --stats. Throws std::invalid_argument, naming the
// options.
void check_viterbi_options(bool decoder_is_viterbi, bool oracle, const ViterbiOptions& options);

// ---- The statistics file ----

inline constexpr const char* kStatsFormat = "kz4ap-viterbi-search-stats";
inline constexpr int kStatsVersion = 1;         // every newborn line (--stats-detail)
inline constexpr int kStatsSummaryVersion = 2;  // a summary for each channel (the default of --stats)

// Which form of the file a run writes.
enum class StatsDetail { Summary, Lines };

// One channel's statistics (SearchStats) with its place in the run.
struct ChannelStats {
    std::uint32_t track_id = 0;
    std::optional<int> label_index;
    viterbi::SearchStats stats;
    friend bool operator==(const ChannelStats&, const ChannelStats&) = default;
};

// The file: the run's recording and configuration, and its channels' statistics.
struct StatsFile {
    std::string recording;
    viterbi::ViterbiConfig config;
    std::vector<ChannelStats> channels;
};

// The statistics file of a finished run on `channels` oracle channels: oracle channel i has the track id i + 1 and the
// label i (EngineConfig::oracle_frequencies_hz), and its statistics are Engine::viterbi_stats(i + 1), copied.
// `recording` is the recording's file name and `config` the configuration the run used. Throws std::runtime_error,
// naming the track, for a channel that has no statistics (the engine has no such channel, or its decoder is not the
// new decoder).
StatsFile collect_stats_file(const Engine& engine, const std::string& recording, const viterbi::ViterbiConfig& config,
                             std::size_t channels);

// A channel's object / the whole file's object (see above). to_json(StatsFile) is the whole object in memory;
// write_stats_file writes the same object channel by channel. Both throw std::invalid_argument, naming the member,
// for a statistic that is not finite.
nlohmann::ordered_json to_json(const ChannelStats& channel);
nlohmann::ordered_json to_json(const StatsFile& file);

// The reverse. Throw std::invalid_argument, naming the key, for another format or version, a missing or mistyped key, a
// code outside its legend, or columns of unequal length.
ChannelStats channel_stats_from_json(const nlohmann::ordered_json& j);
StatsFile stats_file_from_json(const nlohmann::ordered_json& j);

// Writes the file, under a name of its own that is renamed when the file is complete (so that a failure leaves no
// file that looks complete): std::runtime_error if it cannot, std::invalid_argument for a statistic that is not
// finite. A file that could not be written is removed. A file that was written and only could not be renamed (the
// destination is a folder or a read-only file, or on Windows is held open by another program) is whole, so it stays
// under its own name, `path` plus ".partial", and the std::runtime_error's message says so. Reads it:
// std::runtime_error if it cannot be opened, else as stats_file_from_json.
void write_stats_file(const std::filesystem::path& path, const StatsFile& file);
StatsFile read_stats_file(const std::filesystem::path& path);

// ---- The statistics file, version 2: the summary ----

// The counts of a channel's newborn lines by [kind][end][ended_in_first_element][led]: LineKind (3), LineEnd (4), and
// false or true (2 each), as the file's "fates" (see above).
using FateCounts = std::array<std::array<std::array<std::array<std::int64_t, 2>, 2>, 4>, 3>;

// What the reductions of a channel's statistics keep (see above): everything of SearchStats but the lines and the 0.1 s
// series, and of the lines the counts of their fates and the lines that ever led.
struct ChannelSummary {
    std::uint32_t track_id = 0;
    std::optional<int> label_index;
    std::int64_t pieces = 0;          // count
    std::int64_t ceiling_pieces = 0;  // count
    std::vector<std::int64_t> live_histogram;  // counts, bins of 10 stories
    double live_mean_stories = 0.0;   // stories: over the pieces, the 0.1 s windows' means weighted by their pieces
    double live_max_stories = 0.0;    // stories: at any piece
    std::int64_t lines = 0;           // count: the newborn lines
    FateCounts fates{};               // counts
    std::vector<std::int64_t> led_line_ids;       // the ids of the lines that ever led, ascending
    std::vector<viterbi::NewbornLine> led_lines;  // those lines, in the same order
    friend bool operator==(const ChannelSummary&, const ChannelSummary&) = default;
};

// The summary file: the run's recording and configuration, and its channels' summaries.
struct StatsSummaryFile {
    std::string recording;
    viterbi::ViterbiConfig config;
    std::vector<ChannelSummary> channels;
};

// A channel's summary from its statistics; piece_s, in s, is the configuration's. The windows of the live series must
// cover the pieces (all of round(0.1 s / piece_s) pieces, the last one the rest: the search's rule), else
// std::invalid_argument; so for a line whose kind or end is not in the legends.
ChannelSummary summarize_channel(std::uint32_t track_id, std::optional<int> label_index,
                                 const viterbi::SearchStats& stats, double piece_s);
ChannelSummary summarize(const ChannelStats& channel, double piece_s);
// The summary of a whole version 1 file, with the file's piece_s.
StatsSummaryFile summarize(const StatsFile& file);

// The summary file of a finished run on `channels` oracle channels, as collect_stats_file's channels but reduced as they
// are read from the engine, with no copy of the lines. Throws as collect_stats_file.
StatsSummaryFile collect_stats_summary(const Engine& engine, const std::string& recording,
                                       const viterbi::ViterbiConfig& config, std::size_t channels);

// A channel's object / the whole file's object. Both throw std::invalid_argument, naming the member, for a statistic that
// is not finite.
nlohmann::ordered_json to_json(const ChannelSummary& channel);
nlohmann::ordered_json to_json(const StatsSummaryFile& file);

// The reverse. Throw std::invalid_argument, naming the key, for another format or version (a version 1 file is read by
// stats_file_from_json), a missing or mistyped key, a code outside its legend, columns of unequal length, line ids that
// do not ascend, or counts that do not agree (the histogram adds up to the pieces, the fates to the lines, and the
// lines that led, by kind, end and first element, are the fates' led counts).
ChannelSummary channel_summary_from_json(const nlohmann::ordered_json& j);
StatsSummaryFile stats_summary_file_from_json(const nlohmann::ordered_json& j);

// As write_stats_file and read_stats_file.
void write_stats_summary_file(const std::filesystem::path& path, const StatsSummaryFile& file);
StatsSummaryFile read_stats_summary_file(const std::filesystem::path& path);

// ---- Writing a run's statistics ----

// Writes the statistics file of a finished run (collect_stats_file's channels) in the form `detail` names, one channel at
// a time straight from Engine::viterbi_stats, with no copy of the engine's statistics: the summary's channels are
// reduced one by one, and the full form's turned into JSON one by one. Written under a name of its own and renamed when
// complete; throws as collect_stats_file and write_stats_file.
void write_run_stats(const std::filesystem::path& path, const Engine& engine, const std::string& recording,
                     const viterbi::ViterbiConfig& config, std::size_t channels, StatsDetail detail);

}  // namespace kz4ap::bench
