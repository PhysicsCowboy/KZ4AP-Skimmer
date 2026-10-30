#include "track_table.hpp"

#include <algorithm>
#include <utility>

namespace kz4ap::app {

namespace {

bool lower(const TrackRow& a, const TrackRow& b) {
    if (a.freq_hz != b.freq_hz) return a.freq_hz < b.freq_hz;
    return a.id < b.id;
}

}  // namespace

std::string TrackRow::text_tail(std::size_t max_symbols) const {
    const std::size_t first = symbols.size() - std::min(max_symbols, symbols.size());
    std::string out;
    for (std::size_t i = first; i < symbols.size(); ++i) out += symbols[i];
    return out;
}

void TrackTable::apply(const TrackEvent& event) {
    if (event.kind == TrackEvent::Kind::Born) {
        TrackRow row;
        row.id = event.track.id;
        row.freq_hz = event.track.freq_hz;
        row.level_db = event.track.snr_db;
        row.start_s = event.track.start_time_s;
        rows_.push_back(std::move(row));
        restore_order();
        ++structure_version_;
        return;
    }
    const auto it = std::find_if(rows_.begin(), rows_.end(), [&](const TrackRow& r) { return r.id == event.track.id; });
    if (it == rows_.end()) return;
    if (it->symbols.empty()) {
        if (keep_retired_) retired_.push_back(std::move(*it));
        rows_.erase(it);
        ++structure_version_;
        return;
    }
    it->ended_at_s = now_s_;
}

void TrackTable::apply(const DecodedTextEvent& event) {
    const auto it = std::find_if(rows_.begin(), rows_.end(), [&](const TrackRow& r) { return r.id == event.track_id; });
    if (it == rows_.end()) return;
    for (const DecodedSymbol& c : event.chars) it->symbols.push_back(c.text);
    for (const TextCorrection& k : event.corrections) {
        it->symbols.resize(std::min(k.from_index, it->symbols.size()));
        for (const DecodedSymbol& c : k.chars) it->symbols.push_back(c.text);
    }
    if (event.wpm > 0) it->wpm = event.wpm;
    if (it->freq_hz != event.freq_hz) {
        it->freq_hz = event.freq_hz;
        if (!std::is_sorted(rows_.begin(), rows_.end(), lower)) {
            restore_order();
            ++structure_version_;
        }
    }
}

void TrackTable::advance(double now_s) {
    now_s_ = now_s;
    const auto expired = [&](const TrackRow& r) { return r.ended_at_s && now_s_ - *r.ended_at_s > kRetainEndedS; };
    if (std::none_of(rows_.begin(), rows_.end(), expired)) return;
    if (keep_retired_) {
        for (TrackRow& r : rows_) {
            if (expired(r)) retired_.push_back(r);
        }
    }
    std::erase_if(rows_, expired);
    ++structure_version_;
}

void TrackTable::end_of_stream() {
    for (TrackRow& r : rows_) {
        if (r.live()) r.ended_at_s = now_s_;
    }
}

std::size_t TrackTable::live_count() const {
    return static_cast<std::size_t>(std::count_if(rows_.begin(), rows_.end(), [](const TrackRow& r) { return r.live(); }));
}

std::optional<std::size_t> TrackTable::row_of(std::uint32_t id) const {
    for (std::size_t i = 0; i < rows_.size(); ++i) {
        if (rows_[i].id == id) return i;
    }
    return std::nullopt;
}

void TrackTable::restore_order() { std::sort(rows_.begin(), rows_.end(), lower); }

}  // namespace kz4ap::app
