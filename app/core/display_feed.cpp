#include "display_feed.hpp"

#include <iterator>
#include <optional>
#include <utility>

namespace kz4ap::app {

DisplayFeed::DisplayFeed(int frames_per_row, int max_columns) : builder_(frames_per_row, max_columns) {}

void DisplayFeed::on_event(const Event& event) {
    if (const auto* frame = std::get_if<SpectrumFrame>(&event)) {
        std::optional<WaterfallRow> row = builder_.push(*frame);  // outside the lock: this is the costly part
        if (!row) return;
        std::lock_guard lock(mutex_);
        rows_.push_back(std::move(*row));
        if (rows_.size() > kMaxQueuedRows) {
            rows_.pop_front();
            ++rows_dropped_;
        }
    } else if (const auto* track = std::get_if<TrackEvent>(&event)) {
        std::lock_guard lock(mutex_);
        updates_.emplace_back(*track);
    } else if (const auto* text = std::get_if<DecodedTextEvent>(&event)) {
        std::lock_guard lock(mutex_);
        updates_.emplace_back(*text);
    }
}

DisplayBatch DisplayFeed::drain() {
    DisplayBatch batch;
    std::lock_guard lock(mutex_);
    batch.rows.assign(std::make_move_iterator(rows_.begin()), std::make_move_iterator(rows_.end()));
    rows_.clear();
    batch.updates = std::move(updates_);
    updates_.clear();
    batch.rows_dropped = rows_dropped_;
    return batch;
}

}  // namespace kz4ap::app
