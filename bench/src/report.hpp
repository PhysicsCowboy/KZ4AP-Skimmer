#pragma once

#include "labels.hpp"
#include "scoring.hpp"

#include "kz4ap/decoder.hpp"

#include <nlohmann/json.hpp>

#include <cstddef>
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

// Adds the score of the immediate text (characters as first published, corrections ignored) to a score_json
// object: "cer_immediate" in the totals and, per signal, "decoded_immediate", "edits_immediate" and "cer_immediate".
// immediate must score the same labels as the object.
void add_immediate_score(nlohmann::json& score, const Score& immediate);

// One track's text as the engine's DecodedTextEvents build it (kz4ap::TextCorrection): the final text, with every
// correction applied, and the immediate text, the characters as first published with corrections ignored.
class TrackText {
public:
    // An event's characters are appended (to both texts), then its corrections are applied to the final text, in
    // order: each keeps the first from_index characters (clipped to the characters there are) and replaces the
    // rest by its chars.
    void apply(const std::vector<kz4ap::DecodedSymbol>& chars, const std::vector<kz4ap::TextCorrection>& corrections);
    std::string final_text() const;
    const std::string& immediate_text() const { return immediate_; }
    // Every correction received, in order: when it was made (stream time, s), its reach (s), its reason, and the
    // characters it changed in the final text: the replaced characters (from its index on) and its new ones, less
    // the characters the two share at their start and at their end, as `removed` and `inserted` (characters; the
    // smallest contiguous block that differs, so an upper bound of the correction's edit distance).
    struct Received {
        double t_s;
        double reach_s;
        std::string reason;
        std::size_t removed = 0;
        std::size_t inserted = 0;
    };
    const std::vector<Received>& corrections() const { return corrections_; }

private:
    std::vector<std::string> final_;
    std::string immediate_;
    std::vector<Received> corrections_;
};

// A track's corrections as the bench writes them: [{"t_s", "reach_s", "reason", "removed", "inserted"}, ...] (the
// bank decoder only).
nlohmann::json corrections_json(const std::vector<TrackText::Received>& corrections);

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
