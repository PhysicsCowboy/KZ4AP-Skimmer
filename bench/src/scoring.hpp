#pragma once

#include "labels.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace kz4ap::bench {

struct DecodedTrack {
    std::uint32_t id;
    double freq_hz;
    std::string text;
};

struct TransmissionScore {
    std::size_t symbols = 0;             // the transmission's reference symbols
    std::size_t edits = 0;               // edits charged to them
    std::size_t first_word_symbols = 0;  // symbols of its first word
    std::size_t first_word_edits = 0;    // edits charged to those
};

struct SignalScore {
    LabeledSignal label;
    std::optional<std::uint32_t> track_id;  // empty if no track matched
    std::string decoded;                    // normalized
    std::size_t edits;                      // symbol edits (a prosign counts as one)
    double cer;
    std::size_t symbols = 0;                // reference symbols
    std::size_t chars = 0;                  // reference symbols that are not word spaces
    std::size_t spaces = 0;                 // reference word spaces
    std::size_t char_edits = 0;
    std::size_t space_edits = 0;
    std::size_t first_word_symbols = 0;     // symbols in the first word of each transmission
    std::size_t first_word_edits = 0;       // edits charged to those symbols
    std::size_t nospace_symbols = 0;        // reference symbols without word spaces
    std::size_t nospace_edits = 0;          // Levenshtein distance with word spaces removed (VE3NEA's CER)
    std::vector<TransmissionScore> transmissions;  // one per transmission (one if the label lists none)
};

struct Score {
    std::vector<SignalScore> signals;
    double cer = 0;                // total edits / total reference symbols (scored signals)
    double char_cer = 0;           // character edits / reference characters
    double space_error_rate = 0;   // word-space edits / reference word spaces
    double first_word_cer = 0;     // edits charged to first words / their symbols
    double nospace_cer = 0;        // Levenshtein distance without word spaces / reference symbols without them
    std::size_t scored = 0;        // labeled signals with score = true
    std::size_t detected = 0;      // scored signals matched to a track
    std::size_t false_tracks = 0;  // unmatched tracks that decoded some text
};

struct EditCounts {
    std::size_t char_edits = 0;   // edits in which neither symbol is a word space
    std::size_t space_edits = 0;  // edits in which the reference or the decoded symbol is a word space
    std::size_t total() const { return char_edits + space_edits; }
};

struct Alignment {
    EditCounts counts;
    std::vector<std::size_t> charged;  // edits charged to each reference symbol
};

// Uppercase, whitespace runs collapsed to one space, ends trimmed.
std::string normalize_text(std::string_view s);

// Levenshtein distance over characters.
std::size_t edit_distance(std::string_view a, std::string_view b);

// Levenshtein distance over symbols, as split by kz4ap::morse::symbols() (a
// prosign token such as "<SK>" counts as one edit, not one per character).
std::size_t edit_distance(const std::vector<std::string_view>& a, const std::vector<std::string_view>& b);

// Minimum-edit alignment of decoded against reference symbols. Walking back
// from the end, ties prefer a match or substitution, then a deletion, then an
// insertion. A substitution or deletion is charged to its reference symbol; an
// insertion to the reference symbol it precedes (the last one if it comes
// after the end).
Alignment align(const std::vector<std::string_view>& reference, const std::vector<std::string_view>& decoded);

// [first, last) reference-symbol ranges of the first word of each transmission
// (the whole text is one transmission if the label lists none). Throws
// std::runtime_error if the transmissions do not add up to the label's text.
std::vector<std::pair<std::size_t, std::size_t>> first_word_ranges(const LabeledSignal& label);

// [first, last) reference-symbol range of each whole transmission (one range for
// the whole text if the label lists none); the word space between two
// transmissions belongs to neither. Throws like first_word_ranges.
std::vector<std::pair<std::size_t, std::size_t>> transmission_ranges(const LabeledSignal& label);

// Matches each labeled signal to the track within match_tolerance_hz that
// decoded the most text or, with match_by_order, label i to the track with
// id i + 1 (oracle mode), and scores the symbol error rate (a prosign counts
// as one symbol, same as space between words).
Score score(const std::vector<LabeledSignal>& labels, const std::vector<DecodedTrack>& tracks,
            double match_tolerance_hz = 50.0, bool match_by_order = false);

}  // namespace kz4ap::bench
