// The model behind the track list, free of Qt: what the engine's track and text events add up to. One thread only (the
// GUI's). Rows are kept in order of frequency; a track that dies stays, marked as ended, for a while so its text can
// still be read. A table that keeps to plausible text (set_plausible_only) lists a track only once its text has read as
// plausible (text_evidence, track_evidence.hpp), and a new track at the place of a recently ended one goes on with its text.
#pragma once

#include "kz4ap/event_bus.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

namespace kz4ap::app {

struct TrackRow {
    std::uint32_t id = 0;
    double freq_hz = 0;               // Hz from the span's center, as last reported
    float wpm = 0;                    // the latest nonzero speed sent with the text, WPM (segmental: its last 16 letters',
                                      // SegmentalDecoder::text_wpm); 0 until there is one, and for a decoder that gives none
    float level_db = 0;               // Track::snr_db at birth: dB above the noise floor, per FFT bin
    double start_s = 0;               // stream time the signal was first seen, s
    std::optional<double> ended_at_s; // stream time the track died or the stream ended, s; none while live
    std::vector<std::string> symbols; // the decoder's character list, with its corrections applied
    // For each symbol, the neighbor's track whose keying it is (DecodedSymbol::heard_from), or 0 for the track's own; whether the row
    // leaves it out (1): a stretch of such symbols goes whole once the neighbors' listed rows hold every symbol of it at the same times
    // (TrackTable::kTwinS, refresh), so that the text is shown there and only there; and the symbol's start and end, s of stream.
    std::vector<std::uint32_t> heard_from;
    std::vector<char> left_out;
    std::vector<double> starts, ends;
    std::vector<std::uint32_t> track_ids;  // the tracks whose text this row holds, in order (id is the last)
    std::size_t base = 0;             // symbols from the tracks at this place before this one: its corrections start after them;
                                      // counted from the row's first symbol, trimmed or not
    std::size_t trimmed = 0;          // symbols dropped from the front (TrackTable::kMaxRowSymbols)
    std::uint32_t tracks = 1;         // tracks this row's text has come from
    std::uint64_t revision = 0;       // moves whenever what the row shows may have changed: what a list repaints by

    bool live() const { return !ended_at_s; }
    // The text among its last max_symbols symbols, concatenated ("<SK>" is one symbol, " " a word space), but what it leaves out
    // (left_out), one word space in place of each such stretch.
    std::string text_tail(std::size_t max_symbols) const;
    std::string text() const { return text_tail(symbols.size()); }
    // The symbols it shows, as text_tail does.
    std::vector<std::string> shown_symbols() const;
};

class TrackTable {
public:
    // How long an ended track with text stays listed, s of stream. Heuristic. An ended track that never produced text
    // leaves at once: it was a false track or a carrier, and there is nothing to read.
    static constexpr double kRetainEndedS = 60.0;
    // Within this of an ended row's frequency, Hz, a new track goes on with that row. The segmental path's distance between tracks
    // (DetectorConfig::attribution_distance_hz, apply_path_defaults): closer than that they could be one station.
    static constexpr double kSamePlaceHz = 30.0;
    // A row keeps at most about this many symbols, its latest; the list shows 400 at most. Heuristic: at 30 WPM, about 2.5 symbols/s,
    // 27 min of text in about 130 kB; the report and --decoded then hold a long row's end only.
    static constexpr std::size_t kMaxRowSymbols = 4000;
    // A neighbor's row shows a symbol a row heard from it when it holds one of the same text whose start and end each lie within this,
    // s: two decoders behind like filters read one keying's character at the same times (the duplicate rule's 25 ms, about half a dot
    // at 25 WPM; heuristic, docs/signal-processing.md A.11).
    static constexpr double kTwinS = 0.025;
    // When a track's text comes, the symbols its neighbors heard from it within this of its newest one, s, and within this many Hz of
    // it, are looked at again (heuristic: past the decoders' corrections' reach of 20 s; the stage's 300 Hz).
    static constexpr double kRecheckS = 30.0;
    static constexpr double kRecheckHz = 300.0;

    void apply(const TrackEvent& event);
    // Appends the event's characters, then applies its corrections in order, clipping from_index to the list's length
    // (the consumer's rule, decoder.hpp). An event for an unknown track is ignored.
    void apply(const DecodedTextEvent& event);
    // Its retention runs on advance(); nothing here reads the clock in the events' order.
    void apply(const StreamClock&) {}
    // The stream's clock, s: removes ended tracks past their retention.
    void advance(double now_s);
    // The stream ended: every track that is still live ends now. Nothing leaves.
    void end_of_stream();

    const std::vector<TrackRow>& rows() const { return rows_; }
    std::size_t live_count() const;
    // Row index of a track, if it is listed.
    std::optional<std::size_t> row_of(std::uint32_t id) const;
    // Changes whenever rows are added, removed or reordered: a view must then redo its layout, otherwise only redraw.
    std::uint64_t structure_version() const { return structure_version_; }

    // Keep tracks that leave the list in retired() (for the run's report). Off by default: a long live run would grow
    // without bound.
    void set_keep_retired(bool keep) { keep_retired_ = keep; }
    const std::vector<TrackRow>& retired() const { return retired_; }
    // Which of retired() were ever listed: the others read nothing plausible, and hidden() holds those still waiting.
    const std::vector<TrackRow>& retired_hidden() const { return retired_hidden_; }
    const std::vector<TrackRow>& hidden() const { return hidden_; }
    // The list as above; off by default (every track listed from its birth).
    void set_plausible_only(bool on) { plausible_only_ = on; }
    // Tracks born so far.
    std::uint64_t opened() const { return opened_; }

private:
    static constexpr std::size_t kTrimStep = 256;  // symbols
    void restore_order();
    TrackRow* find(std::uint32_t id);
    // An ended row at this place to go on with, listed or hidden.
    TrackRow* ended_at(double freq_hz);
    // These tracks' rows are listed now: what other rows heard from them is left out there.
    void now_listed(const std::vector<std::uint32_t>& ids);
    // Leaves out what rows (within kRecheckHz of near_hz, if given) heard from these tracks after since_s, where shown_by.
    void leave_out_heard(const std::vector<std::uint32_t>& ids, std::optional<double> near_hz, double since_s);
    // Decides left_out from symbol `from` on (back to the start of a stretch heard from neighbors): a stretch is left out whole when
    // every symbol of it is shown_by its neighbor, else not at all.
    void refresh(TrackRow& r, std::size_t from);
    // Whether the listed row of track `source` holds this symbol (kTwinS).
    bool shown_by(std::uint32_t source, const std::string& text, double start_s, double end_s) const;
    std::unordered_set<std::uint32_t> listed_;  // the tracks of the rows listed now (a row leaving takes its own out)
public:
    // How many tracks listed_ holds (tests: it does not grow with the run).
    std::size_t listed_tracks() const { return listed_.size(); }
private:

    std::vector<TrackRow> rows_;
    std::vector<TrackRow> hidden_;  // tracks whose text has not read as plausible, live or ended (plausible_only_)
    std::vector<TrackRow> retired_;
    std::vector<TrackRow> retired_hidden_;
    bool keep_retired_ = false;
    bool plausible_only_ = false;
    std::uint64_t opened_ = 0;
    double now_s_ = 0;
    std::uint64_t structure_version_ = 0;
};

}  // namespace kz4ap::app
