#include "signal_follow.hpp"

#include <algorithm>
#include <cmath>

namespace kz4ap::app {

std::optional<std::uint32_t> row_near(const TrackTable& table, double freq_hz, double max_hz) {
    const TrackRow* best = nullptr;
    const auto better = [&](const TrackRow& r) {
        if (best == nullptr) return true;
        if (r.live() != best->live()) return r.live();
        return std::abs(r.freq_hz - freq_hz) < std::abs(best->freq_hz - freq_hz);
    };
    for (const TrackRow& r : table.rows()) {
        if (std::abs(r.freq_hz - freq_hz) <= max_hz && better(r)) best = &r;
    }
    return best ? std::optional<std::uint32_t>(best->id) : std::nullopt;
}

bool SignalFollow::follow(const TrackTable& table, std::uint32_t track_id) {
    // Looked up first: a track no listed row holds leaves what is followed as it was.
    SignalFollow next;
    next.key_ = track_id;
    const TrackRow* row = next.find(table);
    if (row == nullptr) return false;
    next.take(*row);
    next.rows_followed_ = 1;
    *this = std::move(next);
    return true;
}

void SignalFollow::clear() { *this = SignalFollow{}; }

bool SignalFollow::is_row(const TrackRow& r) const {
    return key_ && (r.id == *key_ || std::find(r.track_ids.begin(), r.track_ids.end(), *key_) != r.track_ids.end());
}

const TrackRow* SignalFollow::find(const TrackTable& table) const {
    for (const TrackRow& r : table.rows()) {
        if (is_row(r)) return &r;
    }
    return nullptr;
}

void SignalFollow::take(const TrackRow& row) {
    key_ = row.id;
    for (const std::uint32_t id : row.track_ids) newest_track_ = std::max(newest_track_, id);
    newest_track_ = std::max(newest_track_, row.id);
    listed_ = true;
    current_ = row.text();
    freq_hz_ = row.freq_hz;
    if (row.wpm > 0) wpm_ = row.wpm;
    live_ = row.live();
    revision_ = row.revision;
    // When the signal was last heard: its latest symbol's end (a word space the table adds has none), else its start.
    double last = row.start_s;
    for (const double end_s : row.ends) last = std::max(last, end_s);
    last_heard_s_ = std::max(last_heard_s_, last);
}

bool SignalFollow::update(const TrackTable& table) {
    if (!key_) return false;
    bool changed = false;
    if (const TrackRow* row = find(table)) {
        changed = !listed_ || row->id != *key_ || row->revision != revision_ || row->live() != live_ || row->freq_hz != freq_hz_;
        take(*row);
    } else if (listed_) {
        // It has left the table: its last text, frequency and speed stay.
        listed_ = false;
        live_ = false;
        changed = true;
    }
    if (live_) return changed;
    // Ended or gone: a live row at its place whose first track was born after the followed signal's newest and which started when the
    // signal had fallen silent (kSuccessorOverlapS) goes on with it; a neighbor that started while the signal was still keyed does not.
    const TrackRow* next = nullptr;
    for (const TrackRow& r : table.rows()) {
        if (!r.live() || r.track_ids.empty() || r.track_ids.front() <= newest_track_) continue;
        if (r.start_s < last_heard_s_ - kSuccessorOverlapS) continue;
        const double distance = std::abs(r.freq_hz - freq_hz_);
        if (distance < TrackTable::kSamePlaceHz && (next == nullptr || distance < std::abs(next->freq_hz - freq_hz_))) next = &r;
    }
    if (next == nullptr) return changed;
    if (!current_.empty()) {
        earlier_ += current_;
        if (earlier_.back() != ' ') earlier_ += ' ';
    }
    if (earlier_.size() > kMaxEarlierBytes) {
        // Its oldest part goes, at a word space where there is one.
        std::size_t cut = earlier_.size() - kMaxEarlierBytes;
        const std::size_t space = earlier_.find(' ', cut);
        if (space != std::string::npos) cut = space + 1;
        earlier_.erase(0, cut);
    }
    current_.clear();
    wpm_ = 0;
    take(*next);
    ++rows_followed_;
    return true;
}

std::string SignalFollow::text() const { return earlier_ + current_; }

}  // namespace kz4ap::app
