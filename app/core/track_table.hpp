// The model behind the track list, free of Qt: what the engine's track and text events add up to. One thread only (the
// GUI's). Rows are kept in order of frequency; a track that dies stays, marked as ended, for a while so its text can
// still be read.
#pragma once

#include "kz4ap/event_bus.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace kz4ap::app {

struct TrackRow {
    std::uint32_t id = 0;
    double freq_hz = 0;               // Hz from the span's center, as last reported
    float wpm = 0;                    // the decoder's latest estimate, WPM; 0 until it has one, and for a decoder
                                      // that gives none
    float level_db = 0;               // Track::snr_db at birth: dB above the noise floor, per FFT bin
    double start_s = 0;               // stream time the signal was first seen, s
    std::optional<double> ended_at_s; // stream time the track died or the stream ended, s; none while live
    std::vector<std::string> symbols; // the decoder's character list, with its corrections applied

    bool live() const { return !ended_at_s; }
    // The last max_symbols symbols, concatenated ("<SK>" is one symbol, " " a word space).
    std::string text_tail(std::size_t max_symbols) const;
    std::string text() const { return text_tail(symbols.size()); }
};

class TrackTable {
public:
    // How long an ended track with text stays listed, s of stream. Heuristic. An ended track that never produced text
    // leaves at once: it was a false track or a carrier, and there is nothing to read.
    static constexpr double kRetainEndedS = 60.0;

    void apply(const TrackEvent& event);
    // Appends the event's characters, then applies its corrections in order, clipping from_index to the list's length
    // (the consumer's rule, decoder.hpp). An event for an unknown track is ignored.
    void apply(const DecodedTextEvent& event);
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

private:
    void restore_order();

    std::vector<TrackRow> rows_;
    std::vector<TrackRow> retired_;
    bool keep_retired_ = false;
    double now_s_ = 0;
    std::uint64_t structure_version_ = 0;
};

}  // namespace kz4ap::app
