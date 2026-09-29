#include "scoring.hpp"

#include <kz4ap/morse.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <set>
#include <stdexcept>

namespace kz4ap::bench {

namespace {

// Levenshtein distance over any random-access sequence of equality-comparable
// elements (characters or symbols).
template <typename Seq>
std::size_t levenshtein(const Seq& a, const Seq& b) {
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

}  // namespace

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

std::size_t edit_distance(std::string_view a, std::string_view b) { return levenshtein(a, b); }

std::size_t edit_distance(const std::vector<std::string_view>& a, const std::vector<std::string_view>& b) {
    return levenshtein(a, b);
}

Alignment align(const std::vector<std::string_view>& reference, const std::vector<std::string_view>& decoded) {
    const std::size_t n = reference.size();
    const std::size_t m = decoded.size();
    std::vector<std::uint32_t> d((n + 1) * (m + 1));
    const auto at = [&](std::size_t i, std::size_t j) -> std::uint32_t& { return d[i * (m + 1) + j]; };
    for (std::size_t i = 0; i <= n; ++i) at(i, 0) = static_cast<std::uint32_t>(i);
    for (std::size_t j = 0; j <= m; ++j) at(0, j) = static_cast<std::uint32_t>(j);
    for (std::size_t i = 1; i <= n; ++i) {
        for (std::size_t j = 1; j <= m; ++j) {
            const std::uint32_t substitute = at(i - 1, j - 1) + (reference[i - 1] == decoded[j - 1] ? 0u : 1u);
            at(i, j) = std::min({at(i - 1, j) + 1, at(i, j - 1) + 1, substitute});
        }
    }

    Alignment result;
    result.charged.assign(n, 0);
    const auto is_space = [](std::string_view s) { return s == " "; };
    const auto charge = [&](std::size_t ref_index, bool space) {
        if (space) ++result.counts.space_edits;
        else ++result.counts.char_edits;
        if (n > 0) ++result.charged[std::min(ref_index, n - 1)];
    };
    std::size_t i = n;
    std::size_t j = m;
    while (i > 0 || j > 0) {
        if (i > 0 && j > 0) {
            const bool same = reference[i - 1] == decoded[j - 1];
            if (at(i, j) == at(i - 1, j - 1) + (same ? 0u : 1u)) {
                if (!same) charge(i - 1, is_space(reference[i - 1]) || is_space(decoded[j - 1]));
                --i;
                --j;
                continue;
            }
        }
        if (i > 0 && at(i, j) == at(i - 1, j) + 1) {  // reference[i-1] was dropped
            charge(i - 1, is_space(reference[i - 1]));
            --i;
            continue;
        }
        charge(i, is_space(decoded[j - 1]));  // decoded[j-1] was inserted before reference[i]
        --j;
    }
    return result;
}

std::vector<std::pair<std::size_t, std::size_t>> first_word_ranges(const LabeledSignal& label) {
    const std::string reference = normalize_text(label.text);
    const std::size_t total = kz4ap::morse::symbols(reference).size();
    std::vector<std::string> texts;
    for (const auto& t : label.transmissions) texts.push_back(normalize_text(t.text));
    if (texts.empty()) texts.push_back(reference);
    std::vector<std::pair<std::size_t, std::size_t>> ranges;
    std::size_t pos = 0;
    for (std::size_t k = 0; k < texts.size(); ++k) {
        const std::string first_word = texts[k].substr(0, texts[k].find(' '));
        ranges.emplace_back(pos, pos + kz4ap::morse::symbols(first_word).size());
        pos += kz4ap::morse::symbols(texts[k]).size();
        if (k + 1 < texts.size()) ++pos;  // the word space between transmissions
    }
    if (pos != total) throw std::runtime_error("labels: transmissions do not add up to the signal's text");
    return ranges;
}

std::vector<std::pair<std::size_t, std::size_t>> transmission_ranges(const LabeledSignal& label) {
    const std::size_t total = kz4ap::morse::symbols(normalize_text(label.text)).size();
    if (label.transmissions.empty()) return {{0, total}};
    std::vector<std::pair<std::size_t, std::size_t>> ranges;
    std::size_t pos = 0;
    for (const auto& t : label.transmissions) {
        const std::size_t n = kz4ap::morse::symbols(normalize_text(t.text)).size();
        ranges.emplace_back(pos, pos + n);
        pos += n + 1;  // the word space between transmissions belongs to neither
    }
    if (pos - 1 != total) throw std::runtime_error("labels: transmissions do not add up to the signal's text");
    return ranges;
}

namespace {

double ratio(std::size_t num, std::size_t den) {
    return den == 0 ? 0.0 : static_cast<double>(num) / static_cast<double>(den);
}

}  // namespace

Score score(const std::vector<LabeledSignal>& labels, const std::vector<DecodedTrack>& tracks,
            double match_tolerance_hz, bool match_by_order) {
    Score result;
    std::set<std::uint32_t> used;
    SignalScore totals{LabeledSignal{}, std::nullopt, "", 0, 0.0};
    for (std::size_t li = 0; li < labels.size(); ++li) {
        const auto& label = labels[li];
        const std::string reference = normalize_text(label.text);
        const DecodedTrack* best = nullptr;
        std::size_t best_len = 0;
        if (match_by_order) {
            for (const auto& t : tracks) {
                if (t.id == li + 1 && !used.count(t.id)) best = &t;
            }
        } else {
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
        }
        SignalScore s{label, std::nullopt, "", 0, 0.0};
        if (best) {
            used.insert(best->id);
            s.track_id = best->id;
            s.decoded = normalize_text(best->text);
            if (label.score) ++result.detected;
        }
        const auto reference_symbols = kz4ap::morse::symbols(reference);
        const auto decoded_symbols = kz4ap::morse::symbols(s.decoded);
        const Alignment a = align(reference_symbols, decoded_symbols);
        s.edits = a.counts.total();
        s.symbols = reference_symbols.size();
        s.spaces = static_cast<std::size_t>(
            std::count(reference_symbols.begin(), reference_symbols.end(), std::string_view(" ")));
        s.chars = s.symbols - s.spaces;
        s.char_edits = a.counts.char_edits;
        s.space_edits = a.counts.space_edits;
        const auto charged_in = [&](std::pair<std::size_t, std::size_t> r) {
            std::size_t sum = 0;
            for (std::size_t i = r.first; i < r.second && i < a.charged.size(); ++i) sum += a.charged[i];
            return sum;
        };
        const auto first_words = first_word_ranges(label);
        const auto whole = transmission_ranges(label);
        for (std::size_t k = 0; k < whole.size(); ++k) {
            const TransmissionScore ts{whole[k].second - whole[k].first, charged_in(whole[k]),
                                       first_words[k].second - first_words[k].first, charged_in(first_words[k])};
            s.first_word_symbols += ts.first_word_symbols;
            s.first_word_edits += ts.first_word_edits;
            s.transmissions.push_back(ts);
        }
        std::vector<std::string_view> reference_nospace, decoded_nospace;
        for (const auto v : reference_symbols) if (v != " ") reference_nospace.push_back(v);
        for (const auto v : decoded_symbols) if (v != " ") decoded_nospace.push_back(v);
        s.nospace_symbols = reference_nospace.size();
        s.nospace_edits = edit_distance(reference_nospace, decoded_nospace);
        s.cer = ratio(s.edits, s.symbols);
        if (label.score) {
            ++result.scored;
            totals.edits += s.edits;
            totals.symbols += s.symbols;
            totals.char_edits += s.char_edits;
            totals.chars += s.chars;
            totals.space_edits += s.space_edits;
            totals.spaces += s.spaces;
            totals.first_word_edits += s.first_word_edits;
            totals.first_word_symbols += s.first_word_symbols;
            totals.nospace_edits += s.nospace_edits;
            totals.nospace_symbols += s.nospace_symbols;
        }
        result.signals.push_back(std::move(s));
    }
    result.cer = ratio(totals.edits, totals.symbols);
    result.char_cer = ratio(totals.char_edits, totals.chars);
    result.space_error_rate = ratio(totals.space_edits, totals.spaces);
    result.first_word_cer = ratio(totals.first_word_edits, totals.first_word_symbols);
    result.nospace_cer = ratio(totals.nospace_edits, totals.nospace_symbols);
    for (const auto& t : tracks) {
        if (!used.count(t.id) && !normalize_text(t.text).empty()) ++result.false_tracks;
    }
    return result;
}

}  // namespace kz4ap::bench
