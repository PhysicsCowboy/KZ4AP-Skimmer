#pragma once

#include "labels.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace kz4ap::bench {

struct DecodedTrack {
    std::uint32_t id;
    double freq_hz;
    std::string text;
};

struct SignalScore {
    LabeledSignal label;
    std::optional<std::uint32_t> track_id;  // empty if no track matched
    std::string decoded;                    // normalized
    std::size_t edits;
    double cer;
};

struct Score {
    std::vector<SignalScore> signals;
    double cer = 0;                // total edits / total reference characters
    std::size_t detected = 0;      // labeled signals matched to a track
    std::size_t false_tracks = 0;  // unmatched tracks that decoded some text
};

// Uppercase, whitespace runs collapsed to one space, ends trimmed.
std::string normalize_text(std::string_view s);

// Levenshtein distance.
std::size_t edit_distance(std::string_view a, std::string_view b);

// Matches each labeled signal to the track within match_tolerance_hz that
// decoded the most text, and scores the character error rate.
Score score(const std::vector<LabeledSignal>& labels, const std::vector<DecodedTrack>& tracks,
            double match_tolerance_hz = 50.0);

}  // namespace kz4ap::bench
