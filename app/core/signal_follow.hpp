// The one-signal pane's rule, free of Qt: which signal it follows through the track table, and the text it shows. The pane follows the
// row that holds the chosen track. A table that keeps to plausible text already goes on in the same row when a new track starts within
// TrackTable::kSamePlaceHz of an ended one (TrackTable::apply), so the row's tracks change under the same row. Every other table lists
// the new track as a row of its own: then, once the followed row has ended, the pane moves on to a live row whose first track was born
// after the followed signal's newest and which started once that signal had fallen silent (kSuccessorOverlapS), within kSamePlaceHz of
// its last frequency, the nearest if several, and keeps the text it had before that row's.
#pragma once

#include "track_table.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace kz4ap::app {

// The listed row nearest to freq_hz, Hz from the span's center, within max_hz: a live one if any is that near, else an ended one. For a
// click on a call (CallRow::freq_hz) or on the waterfall.
std::optional<std::uint32_t> row_near(const TrackTable& table, double freq_hz, double max_hz);

class SignalFollow {
public:
    // The text kept from rows followed before the current one, bytes at most (heuristic: about 20 min at 30 WPM); its oldest part goes.
    static constexpr std::size_t kMaxEarlierBytes = 16384;
    // A successor row must have started (TrackRow::start_s) no earlier than this before the followed signal was last heard (its latest
    // symbol's end), s. Heuristic: the engine's replay, 2 s (EngineConfig::replay_s), lets a new track's text begin up to that much
    // before its birth, so one station's old and new tracks can overlap by about that; a neighbor keyed alongside it overlaps by more.
    // The followed row's end (TrackRow::ended_at_s) is not used: a track is retired seconds to tens of seconds after its station
    // stops (the judge's quiet tests), and a new track of that station is often born before.
    static constexpr double kSuccessorOverlapS = 2.0;

    // Follows the row holding this track from now on, forgetting the signal followed before. False if no listed row holds it: then what
    // was followed stays as it was.
    bool follow(const TrackTable& table, std::uint32_t track_id);
    void clear();
    bool active() const { return key_.has_value(); }
    // Whether this is the followed row (it holds the followed track): a selection restored after the list's layout changed.
    bool is_row(const TrackRow& row) const;

    // Looks the signal up again, after the table took new events: moves on to a successor row as above. True if what the pane shows
    // may have changed (text, frequency, speed, live or ended, or the row).
    bool update(const TrackTable& table);

    // The followed row's id now (TrackRow::id), while it is listed; none once it has left the table.
    std::optional<std::uint32_t> row_id() const { return listed_ ? key_ : std::nullopt; }
    // What the pane shows: the text of the rows followed before, then the current row's; the last frequency, Hz from the span's center;
    // the last speed, WPM (0 for none yet); whether it is live; and how many rows it has followed.
    std::string text() const;
    double freq_hz() const { return freq_hz_; }
    float wpm() const { return wpm_; }
    bool live() const { return live_; }
    std::size_t rows_followed() const { return rows_followed_; }

private:
    const TrackRow* find(const TrackTable& table) const;
    void take(const TrackRow& row);

    std::optional<std::uint32_t> key_;  // a track of the followed row, the newest known
    std::uint32_t newest_track_ = 0;    // the newest track id the followed signal has had
    double last_heard_s_ = -1e300;      // s of stream: its latest symbol's end, or its start before any
    bool listed_ = false;
    std::string earlier_;
    std::string current_;
    double freq_hz_ = 0;
    float wpm_ = 0;
    bool live_ = false;
    std::uint64_t revision_ = 0;
    std::size_t rows_followed_ = 0;
};

}  // namespace kz4ap::app
