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

namespace {

// The symbols [first, end) a row shows, in order, into `add`: a stretch left out (heard from a listed neighbor) becomes one word space
// between the row's own words, none at the start; a row with nothing left out shows every symbol as it is.
template <typename Add>
void shown(const TrackRow& row, std::size_t first, Add add) {
    bool gap = false, any = false, after_space = false;
    for (std::size_t i = first; i < row.symbols.size(); ++i) {
        if (i < row.left_out.size() && row.left_out[i]) {
            gap = true;
            continue;
        }
        const std::string& s = row.symbols[i];
        if (gap) {
            gap = false;
            const bool space_here = any && !after_space;
            if (s == " ") {
                // The stretch and this word space: one space.
                if (space_here) {
                    add(s);
                    after_space = true;
                }
                continue;
            }
            if (space_here) add(std::string(" "));
        }
        add(s);
        any = true;
        after_space = s == " ";
    }
}

}  // namespace

std::string TrackRow::text_tail(std::size_t max_symbols) const {
    const std::size_t first = symbols.size() - std::min(max_symbols, symbols.size());
    std::string out;
    shown(*this, first, [&](const std::string& s) { out += s; });
    return out;
}

std::vector<std::string> TrackRow::shown_symbols() const {
    std::vector<std::string> out;
    shown(*this, 0, [&](const std::string& s) { out.push_back(s); });
    return out;
}

void TrackTable::apply(const TrackEvent& event) {
    if (event.kind == TrackEvent::Kind::Born) {
        ++opened_;
        if (plausible_only_) {
            if (TrackRow* ended = ended_at(event.track.freq_hz)) {
                // The same place: the text goes on, after a word space.
                ended->id = event.track.id;
                ended->track_ids.push_back(event.track.id);
                ended->ended_at_s.reset();
                ++ended->tracks;
                if (!ended->symbols.empty() && ended->symbols.back() != " ") {
                    ended->symbols.push_back(" ");
                    ended->heard_from.push_back(0);
                    ended->left_out.push_back(0);
                    ended->starts.push_back(0.0);
                    ended->ends.push_back(0.0);
                }
                ended->base = ended->trimmed + ended->symbols.size();
                if (std::any_of(rows_.begin(), rows_.end(), [&](const TrackRow& r) { return &r == ended; })) listed_.insert(event.track.id);
                ++structure_version_;
                return;
            }
        }
        TrackRow row;
        row.id = event.track.id;
        row.track_ids = {event.track.id};
        row.freq_hz = event.track.freq_hz;
        row.level_db = event.track.snr_db;
        row.start_s = event.track.start_time_s;
        if (plausible_only_) {
            hidden_.push_back(std::move(row));
            return;
        }
        listed_.insert(event.track.id);
        rows_.push_back(std::move(row));
        restore_order();
        ++structure_version_;
        return;
    }
    if (const auto h = std::find_if(hidden_.begin(), hidden_.end(), [&](const TrackRow& r) { return r.id == event.track.id; });
        h != hidden_.end()) {
        // Nothing plausible yet. A duplicate's or an empty one's text is of no use; another's waits a while for a track here. A copy's
        // own text (what was not heard from a neighbor) waits too: a caller's track that read its own call and then its runner is
        // retired as a copy.
        bool own_text = false;
        for (std::size_t i = 0; i < h->symbols.size() && !own_text; ++i)
            own_text = h->symbols[i] != " " && (i >= h->heard_from.size() || h->heard_from[i] == 0);
        if (h->symbols.empty() || event.end == TrackEnd::Merged || (event.end == TrackEnd::Copy && !own_text)) {
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
    ++it->revision;
    it->ended_at_s = now_s_;
}

void TrackTable::apply(const DecodedTextEvent& event) {
    TrackRow* row = find(event.track_id);
    if (row == nullptr) return;
    ++row->revision;
    const std::size_t n0 = row->symbols.size();
    std::size_t first_changed = n0;
    row->heard_from.resize(n0, 0);
    row->left_out.resize(n0, 0);
    row->starts.resize(n0, 0.0);
    row->ends.resize(n0, 0.0);
    double latest_s = -1e300;
    const auto push = [&](const DecodedSymbol& c) {
        row->symbols.push_back(c.text);
        row->heard_from.push_back(c.heard_from);
        row->starts.push_back(c.start_s);
        row->ends.push_back(c.end_s);
        latest_s = std::max(latest_s, c.end_s);
        row->left_out.push_back(0);  // decided below (refresh)
    };
    for (const DecodedSymbol& c : event.chars) push(c);
    for (const TextCorrection& k : event.corrections) {
        // A correction reaching into what has been trimmed keeps what is left.
        const std::size_t from = row->base + k.from_index;
        row->symbols.resize(std::min(from > row->trimmed ? from - row->trimmed : 0, row->symbols.size()));
        first_changed = std::min(first_changed, row->symbols.size());
        row->heard_from.resize(row->symbols.size());
        row->left_out.resize(row->symbols.size());
        row->starts.resize(row->symbols.size());
        row->ends.resize(row->symbols.size());
        for (const DecodedSymbol& c : k.chars) push(c);
    }
    // A step at a time, so that a row at its limit does not move all its symbols for each new one.
    if (row->symbols.size() >= kMaxRowSymbols + kTrimStep) {
        const auto step = static_cast<std::ptrdiff_t>(kTrimStep);
        row->symbols.erase(row->symbols.begin(), row->symbols.begin() + step);
        row->heard_from.erase(row->heard_from.begin(), row->heard_from.begin() + step);
        row->left_out.erase(row->left_out.begin(), row->left_out.begin() + step);
        row->starts.erase(row->starts.begin(), row->starts.begin() + step);
        row->ends.erase(row->ends.begin(), row->ends.begin() + step);
        row->trimmed += kTrimStep;
        first_changed = first_changed > kTrimStep ? first_changed - kTrimStep : 0;
    }
    if (event.wpm > 0) row->wpm = event.wpm;
    const bool moved = row->freq_hz != event.freq_hz;
    row->freq_hz = event.freq_hz;
    // Left out where the neighbor's own listed row shows the same stretch: the text is then shown there, and only there.
    refresh(*row, first_changed);
    // What its neighbors heard of this track before its own symbols came is left out now, where they match.
    if (latest_s > -1e300 && listed_.count(event.track_id) != 0) leave_out_heard({event.track_id}, event.freq_hz, latest_s - kRecheckS);
    if (const auto h = std::find_if(hidden_.begin(), hidden_.end(), [&](const TrackRow& r) { return r.id == event.track_id; });
        h != hidden_.end()) {
        // Its text has come to read as plausible: it joins the list.
        if (text_evidence(h->shown_symbols()).plausible()) {
            const std::vector<std::uint32_t> ids = h->track_ids;
            rows_.push_back(std::move(*h));
            hidden_.erase(h);
            restore_order();
            ++structure_version_;
            now_listed(ids);
        }
        return;
    }
    if (moved && !std::is_sorted(rows_.begin(), rows_.end(), lower)) {
        restore_order();
        ++structure_version_;
    }
}

void TrackTable::now_listed(const std::vector<std::uint32_t>& ids) {
    listed_.insert(ids.begin(), ids.end());
    // What other rows heard from these tracks is shown on their row now, not on theirs.
    leave_out_heard(ids, std::nullopt, -1e300);
}

void TrackTable::leave_out_heard(const std::vector<std::uint32_t>& ids, std::optional<double> near_hz, double since_s) {
    const auto pass = [&](TrackRow& r) {
        if (near_hz && std::abs(r.freq_hz - *near_hz) > kRecheckHz) return;
        // From the oldest symbol the recheck reaches; only rows that heard from these tracks there.
        const std::size_t n = std::min({r.symbols.size(), r.heard_from.size(), r.ends.size()});
        std::size_t from = n;
        bool heard = false;
        while (from > 0 && (r.ends[from - 1] >= since_s || r.symbols[from - 1] == " ")) {
            --from;
            heard = heard || std::find(ids.begin(), ids.end(), r.heard_from[from]) != ids.end();
        }
        if (heard) refresh(r, from);
    };
    for (TrackRow& r : rows_) pass(r);
    for (TrackRow& r : hidden_) pass(r);
}

void TrackTable::refresh(TrackRow& r, std::size_t from) {
    const std::size_t n = std::min({r.symbols.size(), r.heard_from.size(), r.left_out.size(), r.starts.size(), r.ends.size()});
    if (from > n) from = n;
    const auto heard = [&](std::size_t i) { return r.heard_from[i] != 0 && r.symbols[i] != " "; };
    while (from > 0 && from < n && heard(from - 1)) --from;  // back to the start of the stretch
    bool changed = false;
    const auto set = [&](std::size_t i, char v) {
        if (r.left_out[i] != v) {
            r.left_out[i] = v;
            changed = true;
        }
    };
    for (std::size_t i = from; i < n;) {
        if (!heard(i)) {
            set(i, 0);
            ++i;
            continue;
        }
        // A stretch of symbols heard from neighbors, between word spaces or the track's own: left out whole, or not at all, so that a
        // reading of it the neighbor's row does not hold whole stays readable here.
        std::size_t j = i;
        bool all = true;
        bool gone = false;  // a source whose row has left the list: what was decided stays
        for (; j < n && heard(j); ++j) {
            all = all && shown_by(r.heard_from[j], r.symbols[j], r.starts[j], r.ends[j]);
            gone = gone || listed_.count(r.heard_from[j]) == 0;
        }
        if (gone && r.left_out[i]) {
            i = j;
            continue;
        }
        for (std::size_t k = i; k < j; ++k) set(k, all ? 1 : 0);
        i = j;
    }
    if (changed) ++r.revision;
}
bool TrackTable::shown_by(std::uint32_t source, const std::string& text, double start_s, double end_s) const {
    if (listed_.count(source) == 0) return false;
    for (const TrackRow& r : rows_) {
        if (std::find(r.track_ids.begin(), r.track_ids.end(), source) == r.track_ids.end()) continue;
        const std::size_t n = std::min({r.symbols.size(), r.starts.size(), r.ends.size()});
        for (std::size_t i = n; i-- > 0;) {
            if (r.symbols[i] == " ") continue;
            if (r.ends[i] < start_s - kTwinS) break;  // a row's symbols come in the order of their times
            // The neighbor's own symbol, not one it heard from yet another and leaves out.
            if (r.symbols[i] == text && std::abs(r.starts[i] - start_s) <= kTwinS && std::abs(r.ends[i] - end_s) <= kTwinS)
                return !(i < r.left_out.size() && r.left_out[i]);
        }
        return false;
    }
    return false;
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
    // A row that leaves takes its tracks out of listed_, which then holds no more than the list does; what other rows left out as
    // theirs stays left out (refresh).
    for (const TrackRow& r : rows_)
        if (expired(r))
            for (const std::uint32_t id : r.track_ids) listed_.erase(id);
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
        if (r.live()) {
            r.ended_at_s = now_s_;
            ++r.revision;
        }
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
