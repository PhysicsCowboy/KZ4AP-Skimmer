#include "track_table.hpp"

#include "kz4ap/track_evidence.hpp"

#include <algorithm>
#include <cmath>
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
        ++opened_;
        if (plausible_only_) {
            if (TrackRow* ended = ended_at(event.track.freq_hz)) {
                // The same place: the text goes on, after a word space.
                ended->id = event.track.id;
                ended->ended_at_s.reset();
                ++ended->tracks;
                if (!ended->symbols.empty() && ended->symbols.back() != " ") ended->symbols.push_back(" ");
                ended->base = ended->trimmed + ended->symbols.size();
                ++structure_version_;
                return;
            }
        }
        TrackRow row;
        row.id = event.track.id;
        row.freq_hz = event.track.freq_hz;
        row.level_db = event.track.snr_db;
        row.start_s = event.track.start_time_s;
        if (plausible_only_) {
            hidden_.push_back(std::move(row));
            return;
        }
        rows_.push_back(std::move(row));
        restore_order();
        ++structure_version_;
        return;
    }
    if (const auto h = std::find_if(hidden_.begin(), hidden_.end(), [&](const TrackRow& r) { return r.id == event.track.id; });
        h != hidden_.end()) {
        // Nothing plausible yet. A duplicate's or an empty one's text is of no use; another's waits a while for a track here.
        if (h->symbols.empty() || event.end == TrackEnd::Merged) {
            if (keep_retired_) retired_hidden_.push_back(std::move(*h));
            hidden_.erase(h);
        } else {
            h->ended_at_s = now_s_;
        }
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
    TrackRow* row = find(event.track_id);
    if (row == nullptr) return;
    for (const DecodedSymbol& c : event.chars) row->symbols.push_back(c.text);
    for (const TextCorrection& k : event.corrections) {
        // A correction reaching into what has been trimmed keeps what is left.
        const std::size_t from = row->base + k.from_index;
        row->symbols.resize(std::min(from > row->trimmed ? from - row->trimmed : 0, row->symbols.size()));
        for (const DecodedSymbol& c : k.chars) row->symbols.push_back(c.text);
    }
    // A step at a time, so that a row at its limit does not move all its symbols for each new one.
    if (row->symbols.size() >= kMaxRowSymbols + kTrimStep) {
        row->symbols.erase(row->symbols.begin(), row->symbols.begin() + static_cast<std::ptrdiff_t>(kTrimStep));
        row->trimmed += kTrimStep;
    }
    if (event.wpm > 0) row->wpm = event.wpm;
    const bool moved = row->freq_hz != event.freq_hz;
    row->freq_hz = event.freq_hz;
    if (const auto h = std::find_if(hidden_.begin(), hidden_.end(), [&](const TrackRow& r) { return r.id == event.track_id; });
        h != hidden_.end()) {
        // Its text has come to read as plausible: it joins the list.
        if (text_evidence(h->symbols).plausible()) {
            rows_.push_back(std::move(*h));
            hidden_.erase(h);
            restore_order();
            ++structure_version_;
        }
        return;
    }
    if (moved && !std::is_sorted(rows_.begin(), rows_.end(), lower)) {
        restore_order();
        ++structure_version_;
    }
}

void TrackTable::advance(double now_s) {
    now_s_ = now_s;
    const auto expired = [&](const TrackRow& r) { return r.ended_at_s && now_s_ - *r.ended_at_s > kRetainEndedS; };
    if (keep_retired_) {
        for (TrackRow& r : hidden_) {
            if (expired(r)) retired_hidden_.push_back(r);
        }
    }
    std::erase_if(hidden_, expired);
    if (std::none_of(rows_.begin(), rows_.end(), expired)) return;
    if (keep_retired_) {
        for (TrackRow& r : rows_) {
            if (expired(r)) retired_.push_back(r);
        }
    }
    std::erase_if(rows_, expired);
    ++structure_version_;
}

TrackRow* TrackTable::find(std::uint32_t id) {
    for (TrackRow& r : rows_)
        if (r.id == id) return &r;
    for (TrackRow& r : hidden_)
        if (r.id == id) return &r;
    return nullptr;
}

TrackRow* TrackTable::ended_at(double freq_hz) {
    const auto near = [&](const TrackRow& r) { return r.ended_at_s && std::abs(r.freq_hz - freq_hz) < kSamePlaceHz; };
    for (TrackRow& r : rows_)
        if (near(r)) return &r;
    for (TrackRow& r : hidden_)
        if (near(r)) return &r;
    return nullptr;
}

void TrackTable::end_of_stream() {
    for (TrackRow& r : rows_) {
        if (r.live()) r.ended_at_s = now_s_;
    }
    for (TrackRow& r : hidden_) {
        if (r.live()) r.ended_at_s = now_s_;
    }
}

std::size_t TrackTable::live_count() const {
    const auto live = [](const TrackRow& r) { return r.live(); };
    return static_cast<std::size_t>(std::count_if(rows_.begin(), rows_.end(), live) + std::count_if(hidden_.begin(), hidden_.end(), live));
}

std::optional<std::size_t> TrackTable::row_of(std::uint32_t id) const {
    for (std::size_t i = 0; i < rows_.size(); ++i) {
        if (rows_[i].id == id) return i;
    }
    return std::nullopt;
}

void TrackTable::restore_order() { std::sort(rows_.begin(), rows_.end(), lower); }

}  // namespace kz4ap::app
