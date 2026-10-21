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
// THE GENIE (--genie fist|frequency|fading, repeatable, with --oracle). genie_from_label reads, from the label of an
// oracle channel's station, the truth the decoder is given in place of what it estimates, in the form the engine takes
// (EngineConfig::viterbi_genie, one entry per oracle channel, in the order of the labels):
//   frequency  the label's freq_offset_hz, its frequency at start_s in Hz from the span's center, and its
//              drift_hz_per_s, in Hz/s: the carrier is at freq_offset_hz + drift_hz_per_s x (t - start_s) at stream
//              time t, in s, as the generator makes it (the angle is 2 pi f t + pi drift (t - start_s)^2);
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
// stations for frequency and for fist (each station has its own carrier and fist; the per-station labels of a QSO have
// one), and a signal that changes speed (wpm_end) for fist (one dot cannot give both speeds). LIMITATION, accepted for
// now: the tracker's measurement run on the QSOs on one track needs a piecewise genie (a frequency for each over)
// before it can compare with the true frequency there; ViterbiGenie holds one frequency and one drift.
//
// THE STATISTICS FILE (--stats FILE), format version 1: the decoder's SearchStats (engine/include/kz4ap/viterbi/
// search.hpp) for every channel of a run, with the newborn lines (NewbornLine), which the search keeps when
// ViterbiConfig::stats is true (kz4ap-bench sets it for a run that writes the file). A run writes it at its end, from
// Engine::viterbi_stats, for its oracle channels (a channel of the detector path is gone once its track dies, so
// --stats needs --oracle). One JSON object, ASCII, written by write_stats_file with one line for each top-level key and
// for each channel, and read by read_stats_file; the Python analysis reads it with json.load. The numbers are exact: a
// double is written with the digits that read back to the same double and a float as the double it equals, so
// read_stats_file returns the SearchStats that was written.
//
//   "format"       "kz4ap-viterbi-search-stats"
//   "version"      1. A reader refuses another version. A version changes when a key's meaning changes or a key is
//                  removed; keys may be added to the file's or a channel's object within a version, and a reader
//                  ignores the ones it does not know (this C++ reader reads "config" as a ViterbiConfig it knows)
//   "recording"    the recording's file name
//   "config"       the ViterbiConfig the run used: every field by its C++ name, in its unit (as the result file's)
//   "fields"       what each key of a channel is: "SearchStats" lists its members and "NewbornLine" its, each by its
//                  C++ name, as a string that starts with the unit (count, stories, s, nats, a code into "line_kinds"
//                  or "line_ends", true or false) and says what the member is
//   "line_kinds"   the names of LineKind (Start, Silence, Rescue) in the order of its values: "kind" is an index
//   "line_ends"    the names of LineEnd (Alive, Merge, Beam, Ceiling) in the order of its values: "end" is an index
//   "channels"     an array with one object per channel, in the order the run gives them (oracle: the labels'):
//     "track_id"       the engine's track id (an oracle channel i has the id i + 1)
//     "label_index"    the label's index (oracle: track_id - 1), or null where there is no label
//     "pieces"         count: pieces searched, each config.piece_s long
//     "ceiling_pieces" count: pieces at which the ceiling dropped at least one story
//     "live_histogram" counts of pieces by the number of live stories after pruning, in bins of 10: entry k counts the
//                      pieces with 10 k to 10 k + 9 live stories
//     "live_mean_100ms", "live_max_100ms"  stories: the mean and the largest live count after pruning in each window of
//                      round(0.1 s / piece_s) = 50 pieces, from the channel's first piece; the last entry covers the
//                      pieces of its window searched so far
//     "lines"          the newborn lines in the order of their ids (the position is the id; line 0 is the channel's
//                      first story), as columns named by NewbornLine's members, each an array with one entry per line:
//                      "birth_s" s; "kind" an index into "line_kinds"; "first_led_s" s or null; "largest_deficit_nats"
//                      nats; "end" an index into "line_ends"; "end_s" s or null; "ended_in_first_element" true or false
// The times are stream times, in s, of the ends of pieces; a line's deficits are relative to the best story at their
// piece. Columns, not one object per line: a channel keeps up to two lines per piece (1 000 per second of channel
// time, about 96 bytes each in memory: search.hpp), and seven member names per line would triple the file. A column
// array becomes a data frame column in one step (pandas.DataFrame(channel["lines"])).
#pragma once

#include "labels.hpp"

#include "kz4ap/engine.hpp"
#include "kz4ap/viterbi/config.hpp"
#include "kz4ap/viterbi/search.hpp"

#include <nlohmann/json.hpp>

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
// config.decoder.front_end as FrontEnd::Viterbi and ignores config.oracle_frequencies_hz and config.viterbi_genie.
void check_engine_accepts(const EngineConfig& config);

// ---- The genie ----

enum class GenieKind { Fist, Frequency, Fading };

// "fist", "frequency" or "fading"; throws std::invalid_argument for anything else.
GenieKind parse_genie_kind(const std::string& name);
const char* genie_kind_name(GenieKind kind);

// The genie values of one oracle channel from the label of its station, the parts `kinds` names (see above). `index` is
// the label's number in the labels file, for the messages; piece_s, in s, is the configuration's piece, for the fist's
// smallest sigma. Throws std::invalid_argument, naming the signal, for a label that has no single truth to give, a
// keying style that the table does not have, and a speed or piece that is not positive and finite.
viterbi::ViterbiGenie genie_from_label(const LabeledSignal& label, std::size_t index, std::span<const GenieKind> kinds,
                                       double piece_s);

// A channel's genie values as the result file records them: frequency_hz (or null), frequency_time_s, drift_hz_per_s,
// fading_spread_hz (or null) and fist (or null; else dot_ms, medians_dots, sigmas, shifts_dots).
nlohmann::ordered_json to_json(const viterbi::ViterbiGenie& genie);

// ---- The rules among the options ----

struct ViterbiOptions {
    std::vector<std::string> settings;  // each --set, as given ("NAME=VALUE")
    std::vector<GenieKind> genie;       // --genie, each kind once, in the order first given
    bool stats = false;                 // --stats was given
};

// The rules between the bench's options: --set, --genie and --stats belong to --decoder viterbi; --genie takes its
// values from the labels of the oracle channels, and --stats reads the statistics of the oracle channels at the end of
// the run (a channel of the detector path is gone once its track dies), so both need --oracle. Throws
// std::invalid_argument, naming the options.
void check_viterbi_options(bool decoder_is_viterbi, bool oracle, const ViterbiOptions& options);

// ---- The statistics file ----

inline constexpr const char* kStatsFormat = "kz4ap-viterbi-search-stats";
inline constexpr int kStatsVersion = 1;

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
// finite. Reads it: std::runtime_error if it cannot be opened, else as stats_file_from_json.
void write_stats_file(const std::filesystem::path& path, const StatsFile& file);
StatsFile read_stats_file(const std::filesystem::path& path);

}  // namespace kz4ap::bench
