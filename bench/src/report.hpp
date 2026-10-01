#pragma once

#include "labels.hpp"
#include "scoring.hpp"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace kz4ap::bench {

// Scored signals matched to a track over scored signals (1 if none is scored).
double detection_recall(const Score& s);

// kz4ap-bench's "score" object: totals, and one entry per labeled signal. tracked_freq_hz: the frequency
// of each track's latest decoded text, Hz, by track id; a signal whose track is not in it gets null
// (text decoded outside the engine carries no frequency).
nlohmann::json score_json(const Labels& labels, const Score& s, const std::map<std::uint32_t, double>& tracked_freq_hz);

// The per-label lines and the summary line kz4ap-bench prints.
void print_score(const Score& s);

// Text decoded outside the engine (kz4ap-bench --score-decoded), in one of two forms: {"front_end", "recording",
// "texts": [one per label, in the labels file's order]} (oracle channels, matched by order), or {"front_end",
// "recording", "tracks": [{"id", "freq_hz" (birth), "text", "last_freq_hz" (optional)}]} (detector channels,
// matched by frequency as the engine's detector path is). Other keys are ignored.
struct DecodedTexts {
    std::string front_end;
    std::string recording;
    std::vector<std::string> texts;
    bool detector = false;
    std::vector<DecodedTrack> tracks;
};

// Throw std::runtime_error on bad input.
DecodedTexts parse_decoded_texts(const std::string& json_text);
DecodedTexts load_decoded_texts(const std::filesystem::path& path);

// Track i + 1 at label i's frequency with text i, numbered as oracle channels are. Throws
// std::runtime_error unless there is exactly one text per label.
std::vector<DecodedTrack> tracks_for(const Labels& labels, const DecodedTexts& decoded);

}  // namespace kz4ap::bench
