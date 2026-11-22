// The app's --decoded file: the tracks' text as kz4ap-bench --score-decoded reads it (bench/src/report.hpp, DecodedTexts: "front_end",
// "recording" and "tracks" of {"id", "freq_hz", "last_freq_hz", "text"}; other keys it ignores), and since format_version 2 the
// times: the stream's start in UTC when known, each row's span and each shown character's start and end, s of stream.
#pragma once

#include "track_table.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace kz4ap::app {

// 1 was the file without "format_version" (id, freq_hz, last_freq_hz, text); 2 adds the times.
inline constexpr int kDecodedFormatVersion = 2;

struct DecodedFileInfo {
    std::string front_end;  // the decoder's name
    std::string recording;  // the source's description
    // The stream's first sample, ms since 1970 UTC, when known (a recording's header; a live radio's system clock at its first
    // block, SdrplaySource::start_unix_ms); "stream_start_utc" is then its text (utc_text), else null.
    std::optional<std::int64_t> stream_start_unix_ms;
    double stream_s = 0;  // the stream's time when the file is written, s: the end of a row still live
};

// The JSON text. Each row, of rows and then of retired: "id" (its latest track), "freq_hz" and "last_freq_hz" (its latest
// frequency, Hz from the span's center, as before), "text" (TrackRow::text), and "track_ids", "born_s" (the stream time the detector
// first saw its first track), "died_s" (when its last track ended; null while live), "start_s" and "end_s" (its span: from the
// earlier of born_s and its first timed character's start, to the later of died_s, or info.stream_s while live, and its last
// character's end; a character can start before born_s, since a new channel decodes up to 2 s of kept past, A.9b "Replay"),
// "live", and "symbols": the shown
// characters, [text, start_s, end_s] each, whose texts concatenated are "text". The times are the decoder's own
// (DecodedSymbol::start_s, end_s), rounded to 1 ms; a word space the table puts in (in place of a stretch left out, or between
// two tracks at one place, whose stored times are both 0) has none: [" ", null, null].
std::string decoded_json(const DecodedFileInfo& info, const std::vector<TrackRow>& rows, const std::vector<TrackRow>& retired);

}  // namespace kz4ap::app
