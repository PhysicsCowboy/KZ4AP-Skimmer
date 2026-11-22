#include "decoded_file.hpp"

#include "kz4ap/utc_time.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>

namespace kz4ap::app {

namespace {

double ms_rounded(double s) { return std::round(s * 1000.0) / 1000.0; }

nlohmann::json row_json(const TrackRow& row, double stream_s) {
    nlohmann::json track;
    track["id"] = row.id;
    track["freq_hz"] = row.freq_hz;
    track["last_freq_hz"] = row.freq_hz;
    track["text"] = row.text();
    track["track_ids"] = row.track_ids;
    const double died_s = row.ended_at_s.value_or(stream_s);
    double start_s = row.start_s, end_s = died_s;
    nlohmann::json symbols = nlohmann::json::array();
    for (const auto& [text, i] : row.shown_indexed()) {
        const bool timed = i != TrackRow::kAdded && i < row.starts.size() && i < row.ends.size() &&
                           !(text == " " && row.starts[i] == 0.0 && row.ends[i] == 0.0);
        if (timed) {
            symbols.push_back({text, ms_rounded(row.starts[i]), ms_rounded(row.ends[i])});
            start_s = std::min(start_s, row.starts[i]);
            end_s = std::max(end_s, row.ends[i]);
        } else {
            symbols.push_back({text, nullptr, nullptr});
        }
    }
    track["born_s"] = ms_rounded(row.start_s);
    track["died_s"] = row.ended_at_s ? nlohmann::json(ms_rounded(*row.ended_at_s)) : nlohmann::json(nullptr);
    track["start_s"] = ms_rounded(start_s);
    track["end_s"] = ms_rounded(end_s);
    track["live"] = row.live();
    track["symbols"] = std::move(symbols);
    return track;
}

}  // namespace

std::string decoded_json(const DecodedFileInfo& info, const std::vector<TrackRow>& rows, const std::vector<TrackRow>& retired) {
    nlohmann::json tracks = nlohmann::json::array();
    for (const TrackRow& row : rows) tracks.push_back(row_json(row, info.stream_s));
    for (const TrackRow& row : retired) tracks.push_back(row_json(row, info.stream_s));
    nlohmann::json root;
    root["format_version"] = kDecodedFormatVersion;
    root["front_end"] = info.front_end;
    root["recording"] = info.recording;
    root["stream_start_utc"] = info.stream_start_unix_ms ? nlohmann::json(utc_text(*info.stream_start_unix_ms)) : nlohmann::json(nullptr);
    root["stream_s"] = ms_rounded(info.stream_s);
    root["tracks"] = std::move(tracks);
    // One line; a byte that is not UTF-8 (none is expected: the symbols are ASCII) becomes U+FFFD rather than failing the file.
    return root.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace) + "\n";
}

}  // namespace kz4ap::app
