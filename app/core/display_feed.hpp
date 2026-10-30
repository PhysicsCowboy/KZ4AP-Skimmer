// The hand-over from the pipeline's thread to the display: the engine's events go in as they happen, and the GUI takes
// what has gathered on its own timer. Text events are never dropped. Waterfall rows are: past kMaxQueuedRows the oldest
// go, so a pipeline running far faster than the display (the transport's "max") neither stalls nor grows without bound.
#pragma once

#include "waterfall.hpp"

#include "kz4ap/event_bus.hpp"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <variant>
#include <vector>

namespace kz4ap::app {

// The target for the waterfall's row period, s: about 25 rows/s (a whole number of hops: 23.4 rows/s at the usual hop of
// 21.33 ms), so a window 700 pixels tall shows about 30 s. Heuristic; a 20 WPM dit (60 ms)
// still spans one to two rows, so keying stays visible.
inline constexpr double kRowPeriodS = 0.04;
// A row's columns at most: rows from spans wider than a 4K display's pixels are reduced at the source. Heuristic.
inline constexpr int kMaxRowColumns = 4096;

using TrackUpdate = std::variant<TrackEvent, DecodedTextEvent>;

struct DisplayBatch {
    std::vector<WaterfallRow> rows;      // oldest first
    std::vector<TrackUpdate> updates;    // in the order they happened
    std::uint64_t rows_dropped = 0;      // rows lost to the queue's limit since the feed began
};

class DisplayFeed {
public:
    // A display cannot use more rows at once than it has pixel rows; a 4K display has 2160. Heuristic.
    static constexpr std::size_t kMaxQueuedRows = 2160;

    // frames_per_row: RowBuilder's. Throws std::invalid_argument as it does.
    explicit DisplayFeed(int frames_per_row, int max_columns = kMaxRowColumns);

    // The pipeline's thread: subscribe this to the engine's bus.
    void on_event(const Event& event);
    // The GUI's thread: everything gathered since the last call.
    DisplayBatch drain();

private:
    RowBuilder builder_;  // the pipeline's thread only
    std::mutex mutex_;
    std::deque<WaterfallRow> rows_;
    std::vector<TrackUpdate> updates_;
    std::uint64_t rows_dropped_ = 0;
};

}  // namespace kz4ap::app
