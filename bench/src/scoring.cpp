#include "scoring.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <set>

namespace kz4ap::bench {

std::string normalize_text(std::string_view s) {
    std::string out;
    for (const char c : s) {
        if (std::isspace(static_cast<unsigned char>(c))) {
            if (!out.empty() && out.back() != ' ') out += ' ';
        } else {
            out += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        }
    }
    if (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}

std::size_t edit_distance(std::string_view a, std::string_view b) {
    std::vector<std::size_t> prev(b.size() + 1), cur(b.size() + 1);
    for (std::size_t j = 0; j <= b.size(); ++j) prev[j] = j;
    for (std::size_t i = 1; i <= a.size(); ++i) {
        cur[0] = i;
        for (std::size_t j = 1; j <= b.size(); ++j) {
            const std::size_t substitute = prev[j - 1] + (a[i - 1] == b[j - 1] ? 0 : 1);
            cur[j] = std::min({prev[j] + 1, cur[j - 1] + 1, substitute});
        }
        std::swap(prev, cur);
    }
    return prev[b.size()];
}

Score score(const std::vector<LabeledSignal>& labels, const std::vector<DecodedTrack>& tracks,
            double match_tolerance_hz) {
    Score result;
    std::set<std::uint32_t> used;
    std::size_t total_edits = 0;
    std::size_t total_chars = 0;
    for (const auto& label : labels) {
        const std::string reference = normalize_text(label.text);
        const DecodedTrack* best = nullptr;
        std::size_t best_len = 0;
        for (const auto& t : tracks) {
            if (used.count(t.id) || std::abs(t.freq_hz - label.freq_offset_hz) > match_tolerance_hz) continue;
            const std::size_t len = normalize_text(t.text).size();
            if (!best || len > best_len ||
                (len == best_len && std::abs(t.freq_hz - label.freq_offset_hz) <
                                        std::abs(best->freq_hz - label.freq_offset_hz))) {
                best = &t;
                best_len = len;
            }
        }
        SignalScore s{label, std::nullopt, "", 0, 0.0};
        if (best) {
            used.insert(best->id);
            s.track_id = best->id;
            s.decoded = normalize_text(best->text);
            ++result.detected;
        }
        s.edits = edit_distance(reference, s.decoded);
        s.cer = reference.empty() ? 0.0 : static_cast<double>(s.edits) / static_cast<double>(reference.size());
        total_edits += s.edits;
        total_chars += reference.size();
        result.signals.push_back(std::move(s));
    }
    result.cer = total_chars == 0 ? 0.0 : static_cast<double>(total_edits) / static_cast<double>(total_chars);
    for (const auto& t : tracks) {
        if (!used.count(t.id) && !normalize_text(t.text).empty()) ++result.false_tracks;
    }
    return result;
}

}  // namespace kz4ap::bench
