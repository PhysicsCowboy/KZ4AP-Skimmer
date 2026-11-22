// The app's --decoded file: the tracks' text as kz4ap-bench --score-decoded reads it (bench/src/report.hpp, DecodedTexts: "front_end",
// "recording" and "tracks" of {"id", "freq_hz", "last_freq_hz", "text"}; other keys it ignores), and since format_version 2 the
// times: the stream's start in UTC when known, each row's span and each shown character's start and end, s of stream; since 3 what
// a reader needs to judge those times (samples lost, the radio's resets, the clock at writing, symbols trimmed).
#pragma once

#include "track_table.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace kz4ap::app {

// 1 was the file without "format_version" (id, freq_hz, last_freq_hz, text); 2 added the times; 3 adds lost_samples,
// radio_resets, written_utc and each row's trimmed.
inline constexpr int kDecodedFormatVersion = 3;

struct DecodedFileInfo {
    std::string front_end;  // the decoder's name
    std::string recording;  // the source's description
    // The stream's first sample, ms since 1970 UTC, when known (a recording's header; a live radio's system clock at its first
    // block, SdrplaySource::start_unix_ms); "stream_start_utc" is then its text (utc_text), else null.
    std::optional<std::int64_t> stream_start_unix_ms;
    double stream_s = 0;  // the stream's time when the file is written, s: the end of a row still live
    // The system clock when the file is written, ms since 1970 UTC: "written_utc". For a live stream,
    // (written_utc − stream_start_utc) − stream_s is the stream's total lag behind the clock at writing: the samples lost or
    // never counted (a radio's reset, a USB transfer lost uncounted), the backlog not yet decoded, and the sample clock's drift
    // against the system clock (about 1.8 ms per hour at 0.5 ppm, an estimate), less the start's own lateness (its buffering).
    std::int64_t written_unix_ms = 0;
    // Samples the pipeline lost, as the report's pipeline_lost_samples (a gap in stream time each): "lost_samples".
    std::uint64_t lost_samples = 0;
    // The radio's restarts of its stream (RadioStatus::resets), or none for a source that is not a radio: "radio_resets" (null).
    std::optional<std::uint64_t> radio_resets;
};

// The JSON text, one line. Each row, of rows and then of retired: "id" (its latest track), "freq_hz" and "last_freq_hz" (its latest
// frequency, Hz from the span's center, as before), "text" (TrackRow::text), and
// - "track_ids": the tracks whose text the row holds, in order;
// - "born_s": the stream time the detector first saw its first track;
// - "died_s": the table's stream time when its last track's Died event was applied (TrackTable's clock, which the GUI advances
//   each tick: up to one tick late), or the stream's end for one still live then; null while live;
// - "start_s", "end_s": its span, from the earlier of born_s and its first timed character's start (a character can start before
//   born_s, since a new channel decodes up to 2 s of kept past, docs/signal-processing.md A.9b "Replay") to the later of died_s
//   (info.stream_s while live) and its last character's end;
// - "live";
// - "trimmed": symbols dropped from the row's head (TrackTable::kMaxRowSymbols), 0 for most: text and symbols start after them;
// - "symbols": the shown characters, [text, start_s, end_s] each, whose texts concatenated are "text". The times are the
//   decoder's own (DecodedSymbol::start_s, end_s), rounded to 1 ms. A word space the table puts in has none, [" ", null, null]:
//   one in place of a stretch left out, and one between two tracks at one place, which the table stores with start and end both
//   0.0. So a " " stored at 0.0 and 0.0 is taken for the table's; no decoder's word space has both (it spans a gap of 7 dits).
std::string decoded_json(const DecodedFileInfo& info, const std::vector<TrackRow>& rows, const std::vector<TrackRow>& retired);

}  // namespace kz4ap::app
