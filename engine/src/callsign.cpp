#include "kz4ap/callsign.hpp"

#include "kz4ap/utc_time.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

namespace kz4ap::callsign {

namespace {

bool is_digit(char c) { return c >= '0' && c <= '9'; }
bool is_letter(char c) { return c >= 'A' && c <= 'Z'; }

bool any_of_word(std::string_view w, std::initializer_list<std::string_view> list) {
    return std::find(list.begin(), list.end(), w) != list.end();
}

// A base call, no slash: prefix, one digit (or, a special form, several), one to four letters.
bool parse_base(std::string_view s, bool& special) {
    if (s.size() < 3 || s.size() > 9) return false;
    if (!allocated_pair(s[0], s[1])) return false;
    if (s.substr(0, 2) == "DE") return false;  // the word DE glued to what follows: no amateur series uses it
    // The prefix's lengths to try: one character only if the series is a whole letter's; two; three for a digit first.
    const bool whole_letter = is_letter(s[0]) && is_digit(s[1]) && std::string_view("BFGIKMNRW").find(s[0]) != std::string_view::npos;
    std::array<std::size_t, 3> lengths{};
    std::size_t tries = 0;
    if (whole_letter) lengths[tries++] = 1;
    lengths[tries++] = 2;
    if (is_digit(s[0])) lengths[tries++] = 3;
    for (std::size_t t = 0; t < tries; ++t) {
        const std::size_t p = lengths[t];
        if (p >= s.size() || !is_digit(s[p])) continue;
        if (p == 2 && is_digit(s[1]) && whole_letter) continue;  // K12A: tried as K, 1, then a digit
        if (p == 3 && !is_letter(s[1])) continue;
        if (p == 3 && !is_letter(s[2])) continue;
        std::size_t d = p;
        while (d < s.size() && is_digit(s[d])) ++d;
        const std::size_t digits = d - p;
        const std::size_t letters = s.size() - d;
        if (digits > 4 || letters < 1 || letters > 4) continue;
        bool all_letters = true;
        for (std::size_t i = d; i < s.size(); ++i) all_letters = all_letters && is_letter(s[i]);
        if (!all_letters) continue;
        special = digits > 1;
        return true;
    }
    return false;
}

bool is_prefix_like(std::string_view s) {
    // DL, VP9, W1, 3D2: a prefix with or without its digit.
    if (s.empty() || s.size() > 4) return false;
    if (s.size() == 1) return is_letter(s[0]) && std::string_view("BFGIKMNRW").find(s[0]) != std::string_view::npos;
    return allocated_pair(s[0], s[1]);
}

bool is_keyword(std::string_view w) {
    return any_of_word(w, {"CQ", "DE", "TEST", "QRZ", "TU", "K", "KN", "BK", "SK", "AR", "5NN", "ENN", "599", "NR", "UR", "RST", "R", "UP", "DX", "GM", "GA", "GE", "GN", "OM", "ES", "ES", "FB", "TNX", "TKS", "HW", "73", "QSL", "AGN", "SRI", "QRL", "POTA", "SOTA", "NA", "SS", "FD"});
}

bool is_prosign(std::string_view w) { return !w.empty() && w[0] == '<'; }

// A base call up to and with its last digit: K1 of K1ABC, VE3 of VE3XYZ, 3DA0 of 3DA0RU.
std::string_view call_area(std::string_view base) {
    std::size_t d = base.size();
    while (d > 0 && !is_digit(base[d - 1])) --d;
    return base.substr(0, d);
}
// One letter after the last digit: K1T, VE3F.
bool one_letter_suffix(std::string_view base) { return base.size() >= 2 && is_letter(base.back()) && is_digit(base[base.size() - 2]); }

bool sender_after(std::string_view w) { return any_of_word(w, {"DE", "CQ", "TEST", "QRZ", "CQCQ", "TESTTEST", "POTA", "SOTA", "DX", "UP"}); }
bool sender_before(std::string_view w) { return any_of_word(w, {"K", "KN", "<KN>", "<AR>", "<SK>", "AR", "SK", "TEST", "QRZ", "CQ", "UP", "<KA>"}); }
bool named_before(std::string_view w) { return any_of_word(w, {"DE", "5NN", "ENN", "599", "UR", "NR", "RST", "GM", "GA", "GE", "GN", "FB", "TNX", "TKS", "R", "SRI", "AGN"}); }
bool stray_start(char c) { return c == 'E' || c == 'T' || c == 'I'; }
bool stray_end(char c) { return c == 'E' || c == 'T' || c == 'K'; }
bool tiny_word(std::string_view w) { return any_of_word(w, {"E", "T", "I", "EE", "TT", "IE", "EI", "ET", "TE"}); }

// The piece of a word a keyword is glued to. Returned in order; empty when the word does not split that way.
std::vector<std::string> split_glued(const std::string& w) {
    std::vector<std::string> out;
    const auto good = [](std::string_view s) { return parse_call(s).has_value(); };
    // The call sent twice with no space, perhaps with a stray letter behind.
    for (std::size_t tail = 0; tail <= 1 && w.size() >= 6 + tail; ++tail) {
        const std::size_t n = (w.size() - tail) / 2;
        if ((w.size() - tail) % 2 == 0 && w.compare(0, n, w, n, n) == 0 && good(w.substr(0, n))) {
            out = {w.substr(0, n), w.substr(0, n)};
            return out;
        }
    }
    // A keyword in front.
    for (const std::string_view lead : {"DE", "CQ", "TU", "TEST", "QRZ"}) {
        if (w.size() > lead.size() + 2 && w.compare(0, lead.size(), lead) == 0) {
            std::string rest = w.substr(lead.size());
            if (good(rest)) {
                out = {std::string(lead), rest};
                return out;
            }
            auto inner = split_glued(rest);
            if (!inner.empty()) {
                out = {std::string(lead)};
                out.insert(out.end(), inner.begin(), inner.end());
                return out;
            }
        }
    }
    // DE inside: N5JJDEWA0QZK.
    for (std::size_t p = 3; p + 2 < w.size(); ++p) {
        if (w.compare(p, 2, "DE") == 0) {
            const std::string left = w.substr(0, p), right = w.substr(p + 2);
            if (!good(left)) continue;
            if (good(right)) {
                out = {left, "DE", right};
                return out;
            }
            auto inner = split_glued(right);
            if (!inner.empty()) {
                out = {left, "DE"};
                out.insert(out.end(), inner.begin(), inner.end());
                return out;
            }
        }
    }
    // A keyword behind. DE is here for a word no reader can take whole (a suffix of more than four letters).
    for (const std::string_view trail : {"DE", "5NN", "ENN", "TEST", "TU", "CQ", "QRZ", "KN", "BK"}) {
        if (w.size() > trail.size() + 2 && w.compare(w.size() - trail.size(), trail.size(), trail) == 0) {
            const std::string left = w.substr(0, w.size() - trail.size());
            if (good(left)) {
                out = {left, std::string(trail)};
                return out;
            }
        }
    }
    return out;
}

// The keywords a joined word is cut into (segment_word): is_keyword's list, each once, with its cost in nats, -ln of its share of
// the decoded words (the tiny E, T, I, EE, ... left out): measured, the mean over the spare radio's 2026-11-17 recordings (127 036
// words, general text) and 2026-11-18 recordings (366 101 words, mostly the CWT contest); SOTA, never seen, 11 (heuristic).
struct SegmentKeyword {
    std::string_view text;
    double nats;
};
constexpr std::array<SegmentKeyword, 39> kSegmentKeywords = {{
    {"CQ", 3.9},  {"DE", 5.1},  {"TEST", 6.1}, {"QRZ", 9.0},  {"TU", 4.5},  {"K", 4.2},   {"KN", 8.2},   {"BK", 6.2},  {"SK", 8.6},
    {"AR", 7.8},  {"5NN", 6.6}, {"ENN", 9.5},  {"599", 10.1}, {"NR", 7.9},  {"UR", 6.7},  {"RST", 8.9},  {"R", 4.4},   {"UP", 8.5},
    {"DX", 8.7},  {"GM", 6.9},  {"GA", 7.3},   {"GE", 7.7},   {"GN", 9.1},  {"OM", 8.1},  {"ES", 5.2},   {"FB", 7.5},  {"TNX", 8.0},
    {"TKS", 9.6}, {"HW", 8.7},  {"73", 6.9},   {"QSL", 8.9},  {"AGN", 8.3}, {"SRI", 9.2}, {"QRL", 7.0},  {"POTA", 7.2}, {"SOTA", 11.0},
    {"NA", 7.6},  {"SS", 6.9},  {"FD", 10.0},
}};
bool is_sign_off(std::string_view w) { return any_of_word(w, {"K", "KN"}); }
// A string that is a shorter one twice or more (3J3J): periodic garbage, which would be its own repeat in a joined word.
bool periodic(std::string_view s) {
    for (std::size_t p = 1; p * 2 <= s.size(); ++p) {
        if (s.size() % p != 0) continue;
        bool same = true;
        for (std::size_t i = p; i < s.size() && same; ++i) same = s[i] == s[i - p];
        if (same) return true;
    }
    return false;
}

// The role a call has from the words outside it and its repeats. The word after says most: W1AW DE, W1AW 5NN.
Role role_from(std::string_view prev, std::string_view next) {
    if (any_of_word(next, {"DE"})) return Role::Named;
    if (any_of_word(prev, {"DE"})) return Role::Sender;
    if (named_before(next)) return Role::Named;
    if (sender_after(prev) || sender_before(next)) return Role::Sender;
    return Role::Unknown;
}

}  // namespace

// nats: a character's cost in noise text, -ln p(c), with p(c) proportional to 2^-e, e its count of dots and dashes: what a decoder
// reads from noise favors short characters (E, T, then I, A, N, M). A model, heuristic; over the 36 letters and digits and the
// slash, the sum of 2^-e is 4.09375 (derived), so E and T cost 2.10 nats, a digit 4.87.
double noise_char_nats(char c) {
    static constexpr std::string_view one = "ET", two = "IANM", three = "SURWDKGO", four = "HVFLPJBXCYZQ";
    int e = 5;  // digits and the slash
    if (one.find(c) != std::string_view::npos) e = 1;
    else if (two.find(c) != std::string_view::npos) e = 2;
    else if (three.find(c) != std::string_view::npos) e = 3;
    else if (four.find(c) != std::string_view::npos) e = 4;
    return static_cast<double>(e) * std::log(2.0) + std::log(4.09375);
}

double call_form_nats(std::size_t n) {
    // -ln of the share of random strings of n letters and digits parse_call takes (2 000 000 a length, JoinedWords.FormGains); 9 for more.
    static constexpr std::array<double, 10> form = {0, 0, 0, 3.00, 1.63, 1.56, 1.77, 2.17, 3.23, 4.60};
    return n >= 3 ? form[std::min(n, form.size() - 1)] : std::numeric_limits<double>::infinity();
}

double call_length_nats(std::size_t n) {
    // ln of the share of calls of n characters: the 4 314 base calls three or more RBN skimmers spotted on 2026-11-17 (measured): 3
    // characters 0.63%, 4 20.0%, 5 41.1%, 6 37.3%, 7 0.65%, 8 and more 0.35%, spread evenly over 8 to 12 (a heuristic).
    static constexpr std::array<double, 8> length = {0, 0, 0, -5.07, -1.61, -0.89, -0.99, -5.04};
    if (n < 3) return -std::numeric_limits<double>::infinity();
    return n < length.size() ? length[n] : -7.26;
}

// ---- The grammar

bool allocated_pair(char a, char b) {
    if (!(is_letter(a) || is_digit(a)) || !(is_letter(b) || is_digit(b))) return false;
    if (a == 'Q') return false;
    if (is_digit(a)) {
        if (is_digit(b)) return false;
        if (a == '0') return false;
        if (a == '1') return b == 'A';  // the Sovereign Military Order of Malta's 1A, outside the table
        return !(a == '4' && b == 'N');  // the table's 4N: returned
    }
    if (is_letter(b)) return !(a == 'Y' && b == 'Z');  // the table's YZ: returned
    // A letter and a digit.
    const auto in = [&](std::string_view digits) { return digits.find(b) != std::string_view::npos; };
    switch (a) {
        case 'A': return in("23456789");
        case 'C': return in("23456789");
        case 'D': return in("23456789");
        case 'E': return in("234567");
        case 'H': return in("2346789");
        case 'J': return in("2345678");
        case 'L': return in("23456789");
        case 'P': return in("23456789");
        case 'S': return in("02356789");  // S0: Western Sahara's, in amateur use, outside the table
        case 'T': return in("2345678");
        case 'V': return in("2345678");
        case 'Y': return in("23456789");
        case 'Z': return in("2368");  // Z6: Kosovo's, in amateur use, outside the table
        // The series that are a whole letter's: any digit, the digit being the call's and not the prefix's.
        case 'B':
        case 'F':
        case 'G':
        case 'I':
        case 'K':
        case 'M':
        case 'N':
        case 'R':
        case 'W': return true;
        default: return false;
    }
}

bool only_tiny_pieces(std::string_view base) {
    // The elements noise and a wide signal read as, the tiny pieces of track_evidence.cpp and their neighbors.
    return std::all_of(base.begin(), base.end(), [](char c) { return std::string_view("ETISHANMU5").find(c) != std::string_view::npos; });
}

bool is_cw_word(std::string_view base) {
    // Glued to 5NN: exact, so that DJ5NN stays a call.
    if (any_of_word(base, {"R5NN", "K5NN", "E5NN", "T5NN", "N5NN", "TU5NN", "UR5NN", "ES5NN", "EE5NN", "TT5NN", "ET5NN", "TE5NN"})) return true;
    // Rigs, antennas and software that have a call's form.
    return any_of_word(base, {"G5RV", "K3S", "K4D", "N1MM", "K1EL", "K9AY", "TH7DX", "TH6DX", "TH5DX", "TH3JR", "TH2MK", "HF5B", "HF6V", "HF9V", "HF2V", "FT8DX", "FT5DX", "FT3DX", "W2DU", "W8JK", "W3DZZ"});
}

bool is_locator(std::string_view w) {
    const auto in = [](char c, char lo, char hi) { return c >= lo && c <= hi; };
    return w.size() == 6 && in(w[0], 'A', 'R') && in(w[1], 'A', 'R') && is_digit(w[2]) && is_digit(w[3]) && in(w[4], 'A', 'X') && in(w[5], 'A', 'X');
}

std::optional<ParsedCall> parse_form(std::string_view word) {
    if (word.empty()) return std::nullopt;
    for (char c : word)
        if (!is_letter(c) && !is_digit(c) && c != '/') return std::nullopt;
    const std::size_t slash = word.find('/');
    ParsedCall out;
    out.full = std::string(word);
    bool special = false;
    if (slash == std::string_view::npos) {
        if (!parse_base(word, special)) return std::nullopt;
        out.base = std::string(word);
        out.special = special;
        return out;
    }
    if (word.find('/', slash + 1) != std::string_view::npos) return std::nullopt;
    const std::string_view left = word.substr(0, slash), right = word.substr(slash + 1);
    if (left.empty() || right.empty()) return std::nullopt;
    if (parse_base(left, special)) {
        const bool modifier = any_of_word(right, {"P", "M", "MM", "AM", "QRP", "QRPP", "A"}) || (right.size() == 1 && is_digit(right[0]));
        if (modifier || is_prefix_like(right)) {
            out.base = std::string(left);
            out.special = special;
            return out;
        }
        return std::nullopt;
    }
    if (is_prefix_like(left) && parse_base(right, special)) {
        out.base = std::string(right);
        out.special = special;
        return out;
    }
    return std::nullopt;
}

std::optional<ParsedCall> parse_call(std::string_view word) {
    auto p = parse_form(word);
    if (!p || is_cw_word(p->base)) return std::nullopt;
    return p;
}

double margin_of(float probability) {
    if (!(probability < 1.0f)) return CallReading::kUnknownMargin;
    if (!(probability > 0.0f)) return 0.0;
    return std::max(0.0, std::log(static_cast<double>(probability) / (1.0 - static_cast<double>(probability))));
}

int edit_distance(std::string_view a, std::string_view b, int limit) {
    const int n = static_cast<int>(a.size()), m = static_cast<int>(b.size());
    if (std::abs(n - m) > limit) return limit + 1;
    std::array<int, 32> row{}, next{};
    if (m >= static_cast<int>(row.size())) return limit + 1;
    for (int j = 0; j <= m; ++j) row[static_cast<std::size_t>(j)] = j;
    for (int i = 1; i <= n; ++i) {
        next[0] = i;
        int best = i;
        for (int j = 1; j <= m; ++j) {
            const int sub = row[static_cast<std::size_t>(j - 1)] + (a[static_cast<std::size_t>(i - 1)] == b[static_cast<std::size_t>(j - 1)] ? 0 : 1);
            const int v = std::min({sub, row[static_cast<std::size_t>(j)] + 1, next[static_cast<std::size_t>(j - 1)] + 1});
            next[static_cast<std::size_t>(j)] = v;
            best = std::min(best, v);
        }
        if (best > limit) return limit + 1;
        row = next;
    }
    return std::min(row[static_cast<std::size_t>(m)], limit + 1);
}

// ---- Joined words

std::vector<Segment> segment_word(std::string_view w, const SegmentationCosts& costs) {
    const std::size_t n = w.size();
    if (n == 0 || n > costs.max_chars) return {};
    const double ln36 = std::log(36.0);  // nats: a character of a call, uniform over letters and digits (derived)
    // nats: what the characters [i, i + m) cost as noise text (noise_char_nats).
    const auto noise = [&](std::size_t i, std::size_t m) {
        double sum = 0.0;
        for (std::size_t k = i; k < i + m; ++k) sum += noise_char_nats(w[k]);
        return sum;
    };
    // The calls that start at each place: (length, id); the ids of the distinct strings.
    struct Piece {
        std::size_t length;
        std::size_t id;
    };
    std::vector<std::vector<Piece>> calls_at(n);
    std::vector<std::string> texts;
    std::vector<std::vector<std::size_t>> starts;  // each distinct call's places
    std::vector<std::vector<std::size_t>> keywords_at(n);  // the keywords (their index) that start at each place
    for (std::size_t i = 0; i < n; ++i) {
        for (std::size_t k = 0; k < kSegmentKeywords.size(); ++k) {
            const std::string_view t = kSegmentKeywords[k].text;
            if (i + t.size() <= n && w.compare(i, t.size(), t) == 0) keywords_at[i].push_back(k);
        }
        bool digit = false;  // every call has a digit: parse_call need not see a string without one
        for (std::size_t m = 3; m <= 12 && i + m <= n; ++m) {
            const std::string_view s = w.substr(i, m);
            digit = digit || std::any_of(s.begin(), s.end(), is_digit);
            if (!digit) continue;
            const auto p = parse_call(s);
            if (!p || only_tiny_pieces(p->base) || periodic(s)) continue;
            std::size_t id = 0;
            while (id < texts.size() && texts[id] != s) ++id;
            if (id == texts.size()) {
                texts.emplace_back(s);
                starts.emplace_back();
            }
            starts[id].push_back(i);
            calls_at[i].push_back({m, id});
        }
    }
    // A cache of one call, the last sent (a cache model, Kuhn and De Mori 1990): only a string found twice without overlap can repeat,
    // so only those get a state of their own; any other call leaves the cache at 0.
    std::vector<std::size_t> cache_of(texts.size(), 0);
    std::size_t caches = 0;
    for (std::size_t id = 0; id < texts.size(); ++id) {
        std::size_t copies = 0, next_free = 0;
        for (std::size_t s : starts[id]) {
            if (s < next_free) continue;
            ++copies;
            next_free = s + texts[id].size();
        }
        if (copies >= 2) cache_of[id] = ++caches;
    }
    // The state after a token: (place, cache, what the token was: 0 other, 1 DE, 2 a call).
    const std::size_t per_place = (caches + 1) * 3;
    const auto at = [&](std::size_t i, std::size_t cache, int last) { return (i * (caches + 1) + cache) * 3 + static_cast<std::size_t>(last); };
    struct Back {
        std::size_t from = 0;
        Segment::Kind kind = Segment::Kind::Stray;
        std::size_t length = 0;
        double gain = 0;          // nats: the token's own, the DE before a call included
        double evidence = 0;      // nats: a call's: a repeat's gain and the DE before it (a new call's own gain is no evidence)
        double to_previous = 0;   // nats: a DE or sign-off's gain for the call before it
    };
    const double none = -std::numeric_limits<double>::infinity();
    std::vector<double> best((n + 1) * per_place, none);
    std::vector<Back> back((n + 1) * per_place);
    best[at(0, 0, 0)] = 0.0;
    const auto relax = [&](std::size_t to, double score, const Back& b) {
        if (score > best[to]) {
            best[to] = score;
            back[to] = b;
        }
    };
    for (std::size_t i = 0; i < n; ++i) {
        for (std::size_t cache = 0; cache <= caches; ++cache) {
            for (int last = 0; last < 3; ++last) {
                const std::size_t from = at(i, cache, last);
                const double s = best[from];
                if (s == none) continue;
                // A stray character; it ends a run of repeats (the cache empties), so that copies apart in garbled text do not count.
                relax(at(i + 1, 0, 0), s - costs.stray_nats, {from, Segment::Kind::Stray, 1, -costs.stray_nats, 0.0, 0.0});
                // A keyword.
                for (const std::size_t index : keywords_at[i]) {
                    const SegmentKeyword& kw = kSegmentKeywords[index];
                    const std::string_view k = kw.text;
                    const double own = noise(i, k.size()) - kw.nats;
                    double to_previous = 0.0;
                    if (last == 2 && k == "DE") to_previous = costs.de_after_nats;
                    else if (last == 2 && is_sign_off(k)) to_previous = costs.sign_off_nats;
                    relax(at(i + k.size(), cache, k == "DE" ? 1 : 0), s + own + to_previous,
                          {from, Segment::Kind::Keyword, k.size(), own, 0.0, to_previous});
                }
                // A call: the cached one again, or a new one.
                for (const Piece& p : calls_at[i]) {
                    const std::size_t c = cache_of[p.id];
                    // A repeat explains its characters; a new call's are as likely as any call's of their length against noise's.
                    const bool repeat = c != 0 && c == cache;
                    double own = repeat ? noise(i, p.length) - costs.repeat_nats
                                        : noise(i, p.length) - static_cast<double>(p.length) * ln36 + call_form_nats(p.length) + call_length_nats(p.length) - costs.call_nats;
                    double evidence = repeat ? own : 0.0;
                    if (last == 1) {
                        own += costs.de_before_nats;
                        evidence += costs.de_before_nats;
                    }
                    relax(at(i + p.length, c, 2), s + own, {from, Segment::Kind::Call, p.length, own, evidence, 0.0});
                }
            }
        }
    }
    std::size_t end = at(n, 0, 0);
    for (std::size_t k = at(n, 0, 0); k < at(n, 0, 0) + per_place; ++k)
        if (best[k] > best[end]) end = k;
    if (best[end] == none) return {};
    // The word as a whole must be likelier text of the protocol than noise by word_min_nats, or no call on it is believed.
    const bool protocol = best[end] >= costs.word_min_nats;
    std::vector<Back> path;
    for (std::size_t k = end; k != at(0, 0, 0);) {
        path.push_back(back[k]);
        k = back[k].from;
    }
    std::reverse(path.begin(), path.end());
    std::vector<Segment> out;
    std::size_t offset = 0;
    for (const Back& b : path) {
        const std::string text(w.substr(offset, b.length));
        offset += b.length;
        if (b.kind == Segment::Kind::Stray && !out.empty() && out.back().kind == Segment::Kind::Stray) {
            out.back().text += text;  // strays in a row are one segment
            continue;
        }
        out.push_back({text, b.kind, false, b.kind == Segment::Kind::Call ? b.evidence : 0.0});
        if (b.to_previous != 0.0 && out.size() >= 2) {
            // A K or KN that would also end the call before it as a longer call (AK4J K, AK4JK) is no evidence: which it is, the
            // segmentation's score decides by fractions of a nat (costs.ambiguous_sign_off).
            Segment& call = out[out.size() - 2];
            const bool ambiguous = b.kind == Segment::Kind::Keyword && text != "DE" && costs.ambiguous_sign_off == 0.0 &&
                                   parse_call(call.text + text).has_value();
            if (!ambiguous) call.evidence_nats += b.to_previous;
        }
    }
    // A call's evidence: the gains of all its copies and of what frames them, the same on every copy.
    std::vector<double> total(out.size(), 0.0);
    for (std::size_t a = 0; a < out.size(); ++a) {
        if (out[a].kind != Segment::Kind::Call) continue;
        for (const Segment& o : out)
            if (o.kind == Segment::Kind::Call && o.text == out[a].text) total[a] += o.evidence_nats;
    }
    for (std::size_t a = 0; a < out.size(); ++a) {
        if (out[a].kind != Segment::Kind::Call) continue;
        out[a].evidence_nats = total[a];
        out[a].believed = protocol && total[a] >= costs.believe_nats;
    }
    return out;
}

// ---- Reading a text

std::vector<Word> words_of(std::span<const DecodedSymbol> symbols) { return words_of(symbols, std::span<const char>()); }

std::vector<Word> words_of(std::span<const DecodedSymbol> symbols, bool skip_heard) {
    std::vector<char> skip(symbols.size(), 0);
    for (std::size_t i = 0; i < symbols.size(); ++i) skip[i] = skip_heard && symbols[i].heard_from != 0 ? 1 : 0;
    return words_of(symbols, std::span<const char>(skip));
}

std::vector<Word> words_of(std::span<const DecodedSymbol> symbols, std::span<const char> skip) {
    std::vector<Word> out;
    Word current;
    const auto flush = [&] {
        if (!current.text.empty()) out.push_back(std::move(current));
        current = Word{};
    };
    for (std::size_t i = 0; i < symbols.size(); ++i) {
        const DecodedSymbol& s = symbols[i];
        if (i < skip.size() && skip[i]) {
            flush();
            continue;
        }
        const std::string& t = s.text;
        if (t.size() > 1 && t[0] == '<') {
            flush();
            Word w;
            w.text = t;
            w.start_s = s.start_s;
            w.end_s = s.end_s;
            w.first = i;
            w.last = i + 1;
            out.push_back(std::move(w));
            continue;
        }
        const bool keep = t.size() == 1 && (is_letter(t[0]) || is_digit(t[0]) || t[0] == '/');
        if (!keep) {
            flush();
            continue;
        }
        if (current.text.empty()) {
            current.start_s = s.start_s;
            current.first = i;
        }
        current.text += t;
        current.end_s = s.end_s;
        current.last = i + 1;
    }
    flush();
    return out;
}

std::vector<CallReading> read_calls(std::span<const Word> words, std::size_t from, std::size_t to) { return read_calls(words, from, to, {}); }

std::vector<CallReading> read_calls(std::span<const Word> words, std::size_t from, std::size_t to, std::span<const DecodedSymbol> symbols) {
    return read_calls(words, from, to, symbols, ReadOptions{});
}

std::vector<CallReading> read_calls(std::span<const Word> words, std::size_t from, std::size_t to, std::span<const DecodedSymbol> symbols,
                                    const ReadOptions& options) {
    std::vector<CallReading> out;
    to = std::min(to, words.size());
    // The word before index i, skipping the stray E and T a decoder scatters.
    const auto before = [&](std::size_t i) -> std::string_view {
        while (i > 0) {
            --i;
            if (!tiny_word(words[i].text)) return words[i].text;
        }
        return {};
    };
    const auto after = [&](std::size_t i) -> std::string_view {
        for (; i < words.size(); ++i)
            if (!tiny_word(words[i].text)) return words[i].text;
        return {};
    };
    const auto emit = [&](const std::string& text, Reading how, std::size_t first, std::size_t last, Role role, bool doubled = false) {
        const auto p = parse_call(text);
        // A tiny call only as a word by itself: glued, stripped or joined, such letters are what a decoder scatters.
        if (!p || (only_tiny_pieces(p->base) && how != Reading::Whole)) return;
        CallReading r;
        r.base = p->base;
        r.full = p->full;
        r.special = p->special;
        r.reading = how;
        r.role = role;
        r.doubled = doubled;
        r.first_word = first;
        r.last_word = last;
        r.start_s = words[first].start_s;
        r.end_s = words[last - 1].end_s;
        out.push_back(std::move(r));
    };
    // The role of a call in words [first, last): past its repeats on either side.
    const auto role_of = [&](const std::string& text, std::size_t first, std::size_t last) {
        std::size_t a = first;
        while (a > 0 && words[a - 1].text == text) --a;
        std::size_t b = last;
        while (b < words.size() && words[b].text == text) ++b;
        const std::string_view prev = before(a), next = after(b);
        return role_from(prev, next);
    };

    for (std::size_t i = from; i < to; ++i) {
        const std::string& w = words[i].text;
        if (is_prosign(w) || is_locator(w)) continue;
        // K and R begin calls as well as end overs: K X 9 F. Only the joining below may start from them.
        if (is_keyword(w) && w != "K" && w != "R") continue;
        const bool lone_keyword = is_keyword(w);
        bool direct = lone_keyword;
        // A call's form that ends in CQ, or in DE before another call, is that word glued on: N3XUCQ, K7I DE only with a call behind it.
        const bool ends_in_cq = w.size() >= 5 && w.ends_with("CQ") && parse_call(w.substr(0, w.size() - 2));
        const bool ends_in_de = w.size() >= 5 && w.ends_with("DE") && parse_call(w.substr(0, w.size() - 2)) && i + 1 < words.size() && words[i + 1].text != w && parse_call(words[i + 1].text);
        const bool ends_in_word = ends_in_cq || ends_in_de;
        if (!ends_in_word && parse_call(w)) {
            const bool twice = (i > 0 && words[i - 1].text == w) || (i + 1 < words.size() && words[i + 1].text == w);
            emit(w, Reading::Whole, i, i + 1, role_of(w, i, i + 1), twice);
            direct = true;
        }
        // A joined word cut by segment_word (ReadOptions::segment_joined): the copies of each believed call, each with the role its
        // neighbors on the path give (past its other copies and stray E, T, I; the words around at the word's ends).
        bool segmented = false;
        if (!direct && options.segment_joined != 0 && w.size() >= options.segment_min_chars &&
            (options.segment_joined == 2 || split_glued(w).empty())) {
            const std::vector<Segment> segs = segment_word(w, options.costs);
            const Word& word = words[i];
            const bool timed = !symbols.empty() && word.last <= symbols.size() && word.last - word.first == w.size();
            const auto skipped = [&](std::size_t k, const std::string& call) {
                return (segs[k].kind == Segment::Kind::Call && segs[k].text == call) ||
                       (segs[k].kind == Segment::Kind::Stray && tiny_word(segs[k].text));
            };
            std::size_t offset = 0;
            for (std::size_t k = 0; k < segs.size(); offset += segs[k].text.size(), ++k) {
                const Segment& s = segs[k];
                if (s.kind != Segment::Kind::Call || !s.believed) continue;
                std::size_t a = k;
                while (a > 0 && skipped(a - 1, s.text)) --a;
                std::size_t b = k + 1;
                while (b < segs.size() && skipped(b, s.text)) ++b;
                const std::string_view prev = a > 0 ? std::string_view(segs[a - 1].text) : before(i);
                const std::string_view next = b < segs.size() ? std::string_view(segs[b].text) : after(i + 1);
                const bool twice = (k > 0 && segs[k - 1].text == s.text) || (k + 1 < segs.size() && segs[k + 1].text == s.text);
                // The readings the segmentation cannot tell apart (ReadOptions::segment_alternatives): a call ending in E, I or K
                // that is a call without it (W3QRE, W3QR), and a call with a stray E or I or a K after it that is a call with it
                // (AK4J K, AK4JK). Each is a stripped reading, the segmentation's last, so that the stage's repeats choose and, on a
                // tie, its rule for readings that begin together (the later made) leans to the segmentation's choice.
                std::vector<std::string> readings;
                if (options.segment_alternatives != 0) {
                    const char end = s.text.back();
                    if ((end == 'E' || end == 'I' || end == 'K') && parse_call(std::string_view(s.text).substr(0, s.text.size() - 1)))
                        readings.push_back(s.text.substr(0, s.text.size() - 1));
                    if (k + 1 < segs.size()) {
                        const Segment& after_it = segs[k + 1];
                        const bool stray = after_it.kind == Segment::Kind::Stray && (after_it.text[0] == 'E' || after_it.text[0] == 'I');
                        const bool sign_off = after_it.kind == Segment::Kind::Keyword && after_it.text == "K";
                        if ((stray || sign_off) && parse_call(s.text + after_it.text[0])) readings.push_back(s.text + after_it.text[0]);
                    }
                }
                const Reading how = readings.empty() ? Reading::Glued : Reading::Stripped;
                readings.push_back(s.text);
                for (const std::string& text : readings) {
                    const std::size_t before_emit = out.size();
                    emit(text, how, i, i + 1, role_from(prev, next), twice);
                    if (out.size() > before_emit) {
                        segmented = true;
                        if (timed) {
                            out.back().start_s = symbols[word.first + offset].start_s;
                            out.back().end_s = symbols[word.first + offset + text.size() - 1].end_s;
                        }
                    }
                }
            }
        }
        if (!direct && !segmented) {
            // A word glued: its pieces, each with the role its neighbors in the word give.
            const std::vector<std::string> pieces = split_glued(w);
            const Word& word = words[i];
            // The word's characters are symbols [first, last), one each (words_of).
            const bool timed = !symbols.empty() && word.last <= symbols.size() && word.last - word.first == w.size();
            std::size_t offset = 0;  // the piece's first character in w: the pieces are w's characters in order, at most one left behind
            for (std::size_t k = 0; k < pieces.size(); offset += pieces[k].size(), ++k) {
                if (!parse_call(pieces[k])) continue;
                std::string_view prev = k > 0 ? std::string_view(pieces[k - 1]) : before(i);
                std::string_view next = k + 1 < pieces.size() ? std::string_view(pieces[k + 1]) : after(i + 1);
                if (prev == pieces[k]) prev = before(i);
                if (next == pieces[k]) next = after(i + 1);
                const bool twice = pieces.size() >= 2 && ((k > 0 && pieces[k - 1] == pieces[k]) || (k + 1 < pieces.size() && pieces[k + 1] == pieces[k]));
                const std::size_t before_emit = out.size();
                emit(pieces[k], Reading::Glued, i, i + 1, role_from(prev, next), twice);
                if (timed && out.size() > before_emit && offset + pieces[k].size() <= w.size() &&
                    w.compare(offset, pieces[k].size(), pieces[k]) == 0) {
                    out.back().start_s = symbols[word.first + offset].start_s;
                    out.back().end_s = symbols[word.first + offset + pieces[k].size() - 1].end_s;
                }
            }
        }
        // A stray character on either end; both sides of the question stay, for the evidence to decide.
        if (!lone_keyword && w.size() >= 4) {
            for (std::size_t n = 1; n <= 2 && n + 3 <= w.size(); ++n) {
                if (!stray_start(w[n - 1])) break;
                const std::string rest = w.substr(n);
                if (parse_call(rest)) emit(rest, Reading::Stripped, i, i + 1, role_of(rest, i, i + 1));
            }
            if (stray_end(w.back())) {
                const std::string rest = w.substr(0, w.size() - 1);
                if (parse_call(rest)) emit(rest, Reading::Stripped, i, i + 1, role_of(rest, i, i + 1));
            }
        }
        // Short words that make a call together.
        if (w.size() <= 4 && !is_cw_word(w)) {
            std::string joined = w;
            for (std::size_t j = i + 1; j < words.size() && j <= i + 5; ++j) {
                const std::string& next = words[j].text;
                if (is_prosign(next) || next.size() > 4) break;
                if ((is_keyword(next) && next != "K" && next != "R") || is_cw_word(next)) break;
                joined += next;
                if (joined.size() > 9) break;
                if (parse_call(joined)) emit(joined, Reading::Joined, i, j + 1, role_of(joined, i, j + 1));
            }
        }
    }
    return out;
}

// ---- A known-call list

void KnownCalls::add(std::string_view call) {
    std::string upper(call);
    for (char& c : upper) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    if (const auto p = parse_form(upper)) calls_.insert(p->base);
    else if (!upper.empty()) calls_.insert(upper);
}

KnownCalls KnownCalls::read(std::istream& in) {
    KnownCalls out;
    std::string line;
    std::size_t calls = 0;  // lines whose first word has a call's form
    while (std::getline(in, line)) {
        if (line.find('\0') != std::string::npos)
            throw std::runtime_error("the known-call list is not text (it holds NUL bytes); CW Skimmer's MASTER.DTA is binary: give a list of one call a line, such as MASTER.SCP");
        std::istringstream words(line);
        std::string first;
        if (!(words >> first)) continue;
        // A comment, or a directive to the logging programs (MASTER.SCP's first line, "!!Order,1,1"): not a call.
        if (first[0] == '#' || first[0] == ';' || first.starts_with("!!")) continue;
        std::string upper = first;
        for (char& c : upper) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        if (parse_form(upper)) ++calls;
        out.add(first);
    }
    if (calls == 0) throw std::runtime_error("the known-call list holds no call (no line starts with a call's form): give a list of one call a line, such as MASTER.SCP");
    return out;
}

KnownCalls KnownCalls::read_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary);  // binary: a Ctrl-Z in a binary file must not end it early under Windows' text mode
    if (!in) throw std::runtime_error("cannot read the known-call list: " + path);
    try {
        return read(in);
    } catch (const std::runtime_error& e) {
        throw std::runtime_error(std::string(e.what()) + ": " + path);
    }
}

// ---- The stage

CallSearch::CallSearch(CallSearchConfig config, std::shared_ptr<const KnownCalls> known) : config_(config), known_(std::move(known)) {}

void CallSearch::set_known(std::shared_ptr<const KnownCalls> known) {
    known_ = std::move(known);
    for (auto& [base, c] : candidates_) {
        c.known = known_ && known_->contains(c.base);
        if (c.reported) update_row(c);
    }
}

void CallSearch::apply(const TrackEvent& event) {
    if (event.kind == TrackEvent::Kind::Born) {
        TrackText t;
        t.freq_hz = event.track.freq_hz;
        tracks_[event.track.id] = std::move(t);
        return;
    }
    const auto it = tracks_.find(event.track.id);
    if (it == tracks_.end()) return;
    read_track(it->second, true);
    tracks_.erase(it);
}

void CallSearch::apply(const DecodedTextEvent& event) {
    const auto it = tracks_.find(event.track_id);
    if (it == tracks_.end()) return;
    TrackText& t = it->second;
    t.freq_hz = event.freq_hz;
    t.symbols.insert(t.symbols.end(), event.chars.begin(), event.chars.end());
    for (const TextCorrection& k : event.corrections) {
        // What lies before what is kept cannot change here: what has been read stays read.
        std::size_t skip = 0;
        std::size_t from = k.from_index;
        if (from < t.base) {
            skip = t.base - from;
            from = t.base;
        }
        const std::size_t at = std::min(from - t.base, t.symbols.size());
        if (config_.reread_corrected != 0.0) {
            // The first symbol that differs, in the whole list's index: an unchanged tail of a correction is not news.
            std::size_t same = 0;
            while (at + same < t.symbols.size() && skip + same < k.chars.size() && t.symbols[at + same].text == k.chars[skip + same].text) ++same;
            const bool changed = at + same < t.symbols.size() || skip + same < k.chars.size();
            if (changed) t.rewind_to = std::min(t.rewind_to, t.base + at + same);
        }
        t.symbols.resize(at);
        for (std::size_t i = skip; i < k.chars.size(); ++i) t.symbols.push_back(k.chars[i]);
    }
}

void CallSearch::advance(double now_s) {
    now_s_ = now_s;
    // Read once a second of stream, on whole seconds, so that what is read does not depend on how often the clock is given (a replay, the app).
    const double second = std::floor(now_s_);
    if (second <= last_read_s_) return;
    last_read_s_ = second;
    for (auto& [id, track] : tracks_) read_track(track, false);
    if (now_s_ - last_prune_s_ >= 30.0) {
        last_prune_s_ = now_s_;
        prune(now_s_);
    }
}

void CallSearch::finish() {
    for (auto& [id, track] : tracks_) read_track(track, true);
}

void CallSearch::read_track(TrackText& t, bool all) {
    if (t.symbols.empty()) return;
    // A stretch of a neighbor's symbols (heard_from) is skipped where the neighbor's own track holds every symbol of it, the same text at
    // the same times: it is read there. A stretch the neighbor's track holds otherwise is read here, whole.
    std::vector<char> skip(t.symbols.size(), 0);
    if (config_.skip_neighbors_text != 0.0) {
        const auto heard = [&](std::size_t i) { return t.symbols[i].heard_from != 0 && t.symbols[i].text != " "; };
        const auto held = [&](const DecodedSymbol& s) {
            const auto source = tracks_.find(s.heard_from);
            if (source == tracks_.end()) return false;
            const std::vector<DecodedSymbol>& theirs = source->second.symbols;
            for (std::size_t j = theirs.size(); j-- > 0;) {
                const DecodedSymbol& o = theirs[j];
                if (o.text == " ") continue;
                if (o.end_s < s.start_s - config_.neighbor_twin_s) break;  // a track's symbols come in the order of their times
                if (o.text == s.text && std::abs(o.start_s - s.start_s) <= config_.neighbor_twin_s &&
                    std::abs(o.end_s - s.end_s) <= config_.neighbor_twin_s)
                    return true;
            }
            return false;
        };
        for (std::size_t i = 0; i < t.symbols.size();) {
            if (!heard(i)) {
                ++i;
                continue;
            }
            std::size_t j = i;
            bool whole = true;
            for (; j < t.symbols.size() && heard(j); ++j) whole = whole && held(t.symbols[j]);
            // Never part of a word: a stretch is skipped only when it is whole words, a word's end (word space, prosign, punctuation or
            // the text's end) on either side; a word only partly heard from a neighbor is read whole, as the track's own (review 18: on
            // KC2LRC-2 W7QDM's "TEST W7QDM" lost its TES to a track 27 Hz away, and the call its role of sender).
            const auto word_char = [&](std::size_t k) {
                const std::string& x = t.symbols[k].text;
                return x.size() == 1 && (is_letter(x[0]) || is_digit(x[0]) || x[0] == '/');
            };
            const bool whole_words = (i == 0 || !word_char(i - 1)) && (j == t.symbols.size() || !word_char(j));
            for (std::size_t k = i; k < j; ++k) skip[k] = whole && whole_words ? 1 : 0;
            i = j;
        }
    }
    const std::vector<Word> words = words_of(t.symbols, std::span<const char>(skip));
    if (words.empty()) return;
    if (t.rewind_to < t.read_to) {
        // Back to the start of the word the change falls in. What stays the same there is one occurrence with what was read before (CallSearch::add).
        std::size_t i = 0;
        while (i < words.size() && t.base + words[i].last <= t.rewind_to) ++i;
        t.read_to = i < words.size() ? t.base + words[i].first : t.rewind_to;
    }
    t.rewind_to = static_cast<std::size_t>(-1);
    const std::size_t read_at = t.read_to > t.base ? t.read_to - t.base : 0;
    // The first word not yet read, and the words that have settled: over before now - settle_s, or a track that has ended.
    const double settled = last_read_s_ - config_.settle_s;
    std::size_t from = 0;
    while (from < words.size() && words[from].first < read_at) ++from;
    std::size_t to = from;
    while (to < words.size() && (all || words[to].end_s <= settled)) ++to;
    if (to == from) return;
    const std::span<const DecodedSymbol> timed_by =
        config_.glued_pieces_timed != 0.0 ? std::span<const DecodedSymbol>(t.symbols) : std::span<const DecodedSymbol>();
    ReadOptions options;
    options.segment_joined = static_cast<int>(config_.segment_joined);
    options.segment_min_chars = static_cast<std::size_t>(std::max(0.0, config_.segment_min_chars));
    options.segment_alternatives = static_cast<int>(config_.segment_alternatives);
    options.costs = config_.segmentation;
    for (CallReading& r : read_calls(words, from, to, timed_by, options)) {
        const std::size_t a = words[r.first_word].first, b = words[r.last_word - 1].last;
        // The least margin; an unknown one (-1) wins the minimum, so that a reading is sure only if every symbol says so.
        double least = std::numeric_limits<double>::infinity();
        for (std::size_t i = a; i < b; ++i) least = std::min(least, margin_of(t.symbols[i].probability));
        r.margin_nats = std::isfinite(least) ? least : CallReading::kUnknownMargin;
        add(r, t.freq_hz);
    }
    t.read_to = t.base + words[to - 1].last;
    // Keep the context, eight words back (heuristic: a call's frame is at most a few words), and what a correction may yet change.
    std::size_t keep_word = to >= 8 ? to - 8 : 0;
    if (config_.reread_corrected != 0.0) {
        while (keep_word > 0 && words[keep_word - 1].end_s >= now_s_ - config_.history_s) --keep_word;
    }
    const std::size_t drop = words[keep_word].first;
    if (drop > 0) {
        t.symbols.erase(t.symbols.begin(), t.symbols.begin() + static_cast<std::ptrdiff_t>(drop));
        t.base += drop;
    }
}

void CallSearch::add(const CallReading& r, double freq_hz) {
    if (reading_log_) reading_log_(r, freq_hz);
    if (r.special && !(known_ && known_->contains(r.base))) return;
    if (only_tiny_pieces(r.base) && !(config_.tiny_points > 0.0)) return;
    auto [it, fresh] = candidates_.try_emplace(r.base);
    Candidate& c = it->second;
    if (fresh) {
        c.base = r.base;
        c.made = ++made_;
        c.known = known_ && known_->contains(r.base);
        c.first_s = r.start_s;
    }
    c.first_s = std::min(c.first_s, r.start_s);  // a correction can read something earlier than what came first
    const bool word = r.reading == Reading::Whole || r.reading == Reading::Glued;
    const double points = word ? 1.0 : (r.reading == Reading::Joined ? config_.joined_points : config_.stripped_points);
    // A keyword beside it, or the call sent twice: either way the word is used as a call.
    const bool framed = r.role != Role::Unknown || (r.doubled && config_.doubled_is_framed != 0.0);
    // Its place: where it last signed, else where it was last read bare, else where another named it, and among those a whole word before
    // a stripped or joined one. A second track's copy of one transmission can better what the first track's copy said.
    const int rank = 2 * (r.role == Role::Sender ? 2 : (r.role == Role::Unknown ? 1 : 0)) + (points >= 1.0 ? 1 : 0);
    if (rank >= c.freq_rank) {
        c.freq_rank = rank;
        c.freq_hz = freq_hz;
    }
    c.heard = c.heard || r.role != Role::Named;
    c.framed = c.framed || r.role == Role::Sender;
    const double margin = word ? r.margin_nats : 0.0;
    // One transmission, one occurrence: another track's or another reading's copy of the same moment at this place only betters it.
    for (Occurrence& o : c.recent) {
        if (r.start_s < o.end_s && o.start_s < r.end_s && std::abs(o.freq_hz - freq_hz) <= config_.same_place_hz) {
            if (points > o.points) o.points = points;
            if (framed) o.framed = true;
            if (r.role != Role::Named) o.own = true;
            if (r.role != Role::Unknown && o.role == Role::Unknown) o.role = r.role;
            o.margin = std::max(o.margin, margin);
            evaluate(c, freq_hz, r.end_s);
            return;
        }
    }
    Occurrence o;
    o.start_s = r.start_s;
    o.end_s = r.end_s;
    o.freq_hz = freq_hz;
    o.points = points;
    o.role = r.role;
    o.framed = framed;
    o.own = r.role != Role::Named;
    o.margin = margin;
    c.recent.push_back(o);
    while (!c.recent.empty() && r.end_s - c.recent.front().end_s > config_.window_s) c.recent.pop_front();
    ++c.count;
    ++c.forms[r.full];
    c.last_s = std::max(c.last_s, r.end_s);
    evaluate(c, freq_hz, r.end_s);
}

CallSearch::Support CallSearch::support(const Candidate& c, double freq_hz, double now_s, double within_hz) const {
    Support s;
    for (const Occurrence& o : c.recent) {
        if (std::abs(o.freq_hz - freq_hz) > within_hz || std::abs(now_s - o.end_s) > config_.window_s) continue;
        s.points += o.points;
        s.framed = s.framed || o.framed;
        s.named = s.named || (o.role == Role::Named && o.points >= 1.0);
        s.sure = s.sure || (config_.known_margin_nats > 0.0 && o.points >= 1.0 && o.margin >= config_.known_margin_nats);
    }
    return s;
}

bool CallSearch::passes(const Candidate& c, const Support& s) const {
    const double points = s.points;
    if (only_tiny_pieces(c.base)) return points >= config_.tiny_points;
    if (s.named && points >= config_.points_named && (c.known || !known_ || config_.named_listed_only == 0.0)) return true;
    if (c.known) return points >= (s.framed || s.sure ? config_.points_known : config_.points_known_bare);
    return points >= (s.framed ? config_.points_framed : config_.points_bare);
}

bool CallSearch::stands_elsewhere(const Candidate& c, double freq_hz, double now_s) const {
    for (const Occurrence& a : c.recent) {
        if (std::abs(a.freq_hz - freq_hz) <= config_.variant_hz || now_s - a.end_s > config_.window_s) continue;
        Support alone;
        for (const Occurrence& o : c.recent) {
            if (std::abs(o.freq_hz - freq_hz) <= config_.variant_hz) continue;
            if (std::abs(o.freq_hz - a.freq_hz) > config_.same_place_hz || now_s - o.end_s > config_.window_s) continue;
            alone.points += o.points;
            alone.framed = alone.framed || o.framed;
            alone.sure = alone.sure || (config_.known_margin_nats > 0.0 && o.points >= 1.0 && o.margin >= config_.known_margin_nats);
        }
        if (passes(c, alone)) return true;
    }
    return false;
}

CallSearch::Support CallSearch::pooled_support(const Candidate& c, double now_s) {
    Support s;
    std::vector<const Occurrence*> counted;
    for (const Occurrence& o : c.recent) {
        if (std::abs(now_s - o.end_s) > config_.window_s) continue;
        if (held_at(c, o.freq_hz, now_s, nullptr, nullptr)) continue;
        counted.push_back(&o);
        s.framed = s.framed || o.framed;
        s.named = s.named || (o.role == Role::Named && o.points >= 1.0);
        s.sure = s.sure || (config_.known_margin_nats > 0.0 && o.points >= 1.0 && o.margin >= config_.known_margin_nats);
    }
    // One per transmission: occurrences connected by overlaps in time are one, wherever they lie, at the best of their points. A sweep in
    // order of start, so that the groups do not depend on the order of the tracks.
    std::sort(counted.begin(), counted.end(), [](const Occurrence* a, const Occurrence* b) {
        return a->start_s < b->start_s || (a->start_s == b->start_s && a->end_s < b->end_s);
    });
    double group_end = -std::numeric_limits<double>::infinity(), group_points = 0.0;
    for (const Occurrence* o : counted) {
        if (o->start_s < group_end) {
            group_points = std::max(group_points, o->points);
            group_end = std::max(group_end, o->end_s);
        } else {
            s.points += group_points;
            group_points = o->points;
            group_end = o->end_s;
        }
    }
    s.points += group_points;
    return s;
}

bool CallSearch::held_at(const Candidate& c, double freq_hz, double now_s, double* rival_best, std::vector<Candidate*>* beaten) {
    const auto limit_for = [](std::size_t n) { return n >= 4 ? 2 : 1; };
    bool held = false;
    const double mine = support(c, freq_hz, now_s, config_.variant_hz).points;
    for (auto& [base, other] : candidates_) {
        if (&other == &c || other.recent.empty()) continue;
        // Its occurrences' span must come within variant_s of now, on either side (now is a loser's last reading when a withdrawal is judged).
        const auto [oldest, newest] = std::minmax_element(other.recent.begin(), other.recent.end(),
                                                          [](const Occurrence& a, const Occurrence& b) { return a.end_s < b.end_s; });
        if (newest->end_s < now_s - config_.variant_s || oldest->end_s > now_s + config_.variant_s) continue;
        // A call of one letter after its digit and another with the same prefix and digit: two edits apart count (K1T, K1BX).
        const bool short_pair = config_.short_variant_ratio > 0.0 && (one_letter_suffix(c.base) || one_letter_suffix(other.base)) &&
                                call_area(c.base) == call_area(other.base);
        const int limit = short_pair ? 2 : limit_for(std::min(c.base.size(), other.base.size()));
        const bool prefix = config_.prefix_held != 0.0 && (c.base.starts_with(other.base) || other.base.starts_with(c.base));
        const int distance = edit_distance(c.base, other.base, limit);  // cheap when the lengths differ by more than limit
        if (distance > limit && !prefix) continue;
        const double theirs = support(other, freq_hz, now_s, config_.variant_hz).points;
        if (theirs <= 0) continue;
        const int d = prefix ? 1 : distance;
        // Two letters' difference only against a much better call.
        const double need = d >= 2 ? (short_pair ? config_.short_variant_ratio : config_.far_variant_ratio) : 1.0;
        const auto beats = [&](const Candidate& a, double pa, const Candidate& b, double pb) {
            if (a.known && !b.known) return pa * config_.known_rival_factor >= pb;
            if (b.known && !a.known) return pa >= config_.known_rival_factor * pb;
            if (pa > need * pb) return true;
            if (pa != pb || d != 1) return false;
            // A tie: the earlier call, unless they begin on one word (within 1 s; heuristic), where the later is what a correction made of it.
            if (std::abs(a.first_s - b.first_s) <= 1.0) return a.made > b.made;
            return a.first_s < b.first_s;
        };
        if (beats(other, theirs, c, mine)) {
            held = true;
            if (rival_best) *rival_best = std::max(*rival_best, other.best_points);
            if (!rival_best) return true;  // only the answer is wanted
        } else if (beaten && beats(c, mine, other, theirs)) {
            if (other.reported) beaten->push_back(&other);
        }
    }
    return held;
}

void CallSearch::evaluate(Candidate& c, double freq_hz, double now_s) {
    const Support s = support(c, freq_hz, now_s, config_.same_place_hz);
    c.best_points = std::max(c.best_points, s.points);
    // Its near variants at this place: which are better supported, and what the weaker add.
    double rival_best = 0.0;  // the best any rival that beats it here has had at one place
    std::vector<Candidate*> beaten;
    const bool held = held_at(c, freq_hz, now_s, &rival_best, &beaten);
    const bool pool = config_.pool_places != 0.0;
    // Whether a call that loses here leaves the list. Pooled: when its points where nothing holds it no longer pass. Else a defeat here
    // takes it off only if the rival is the better at its best, or the call stands nowhere else: a call doubled and signed at 1 kHz does
    // not leave because of a misreading at 2 kHz.
    const bool own_place = config_.withdraw_own_place != 0.0;
    // Pooled, the loser is judged at its own last reading, against rivals within window_s of it either side: a call reported on its
    // readings does not leave because they aged out of the window before a rival came (the 2026-11-17 80 m recording's WK0B, five signed
    // readings, taken off at 2 647 s by W5FB's three at its place, when only its reading at 1 042 s was still within 30 min).
    const auto leaves = [&](Candidate& loser, double winner_best) {
        if (pool) return !passes(loser, pooled_support(loser, std::min(now_s, loser.last_s)));
        return !own_place || winner_best > loser.best_points || !stands_elsewhere(loser, freq_hz, now_s);
    };
    if (held) {
        if (c.reported) {
            if (leaves(c, rival_best)) withdraw(c);
            else update_row(c);
        }
        return;
    }
    if (c.reported) {
        update_row(c);
    } else if (pool ? passes(c, pooled_support(c, now_s)) : passes(c, s)) {
        // Pooled, the test that reports is the test that withdraws (local points can exceed pooled ones: they keep held occurrences
        // within same_place_hz), so a row does not come and go.
        report(c, now_s);
    }
    if (c.reported) {
        for (Candidate* b : beaten) {
            if (leaves(*b, c.best_points)) withdraw(*b);
        }
    }
}

void CallSearch::report(Candidate& c, double now_s) {
    c.reported = true;
    c.reported_s = now_s;
    rows_.emplace_back();
    rows_.back().base = c.base;
    ++version_;
    update_row(c);
}

void CallSearch::withdraw(Candidate& c) {
    c.reported = false;
    std::erase_if(rows_, [&](const CallRow& r) { return r.base == c.base; });
    ++withdrawn_;
    ++version_;
}

double CallSearch::heard_at_place_s(const Candidate& c) const {
    double latest = -1.0;
    for (const Occurrence& o : c.recent) {
        if (o.own && std::abs(o.freq_hz - c.freq_hz) <= config_.same_place_hz) latest = std::max(latest, o.end_s);
    }
    return latest;
}

void CallSearch::update_row(const Candidate& c) {
    const auto it = std::find_if(rows_.begin(), rows_.end(), [&](const CallRow& r) { return r.base == c.base; });
    if (it == rows_.end()) return;
    it->call = c.base;
    std::uint32_t most = 1;
    for (const auto& [form, n] : c.forms) {
        if (form != c.base && n >= 2 && n > most) {
            most = n;
            it->call = form;
        }
    }
    it->freq_hz = c.freq_hz;
    it->first_s = c.first_s;
    it->reported_s = c.reported_s;
    it->last_s = c.last_s;
    it->heard_s = heard_at_place_s(c);
    it->count = c.count;
    it->points = c.best_points;
    it->known = c.known;
    it->heard = c.heard;
    it->framed = c.framed;
}

void CallSearch::prune(double now_s) {
    // What has not been reported and has aged out of every window goes; a live run's junk would otherwise grow without bound. A
    // withdrawal is judged as late as variant_s before now (a beaten call's readings reach that far) and counts rivals within
    // window_s of that time, so a rival matters for window_s + variant_s after its last reading (derived): prune sooner and the
    // result would depend on when prune ran.
    const double keep = config_.window_s + config_.variant_s;
    for (auto it = candidates_.begin(); it != candidates_.end();) {
        Candidate& c = it->second;
        if (!c.reported && now_s - c.last_s > keep) {
            it = candidates_.erase(it);
        } else {
            ++it;
        }
    }
}

std::vector<CallRow> CallSearch::candidates() const {
    std::vector<CallRow> out;
    for (const auto& [base, c] : candidates_) {
        CallRow r;
        r.call = c.base;
        r.base = c.base;
        r.freq_hz = c.freq_hz;
        r.first_s = c.first_s;
        r.reported_s = c.reported ? c.reported_s : -1.0;
        r.last_s = c.last_s;
        r.heard_s = heard_at_place_s(c);
        r.count = c.count;
        r.points = c.best_points;
        r.known = c.known;
        r.heard = c.heard;
        r.framed = c.framed;
        out.push_back(std::move(r));
    }
    std::sort(out.begin(), out.end(), [](const CallRow& a, const CallRow& b) { return a.freq_hz < b.freq_hz; });
    return out;
}

std::string calls_csv(const std::vector<CallRow>& rows, std::optional<double> center_hz, std::optional<std::int64_t> start_unix_ms) {
    std::ostringstream out;
    out << "call,base,freq_hz,freq_khz,first_s,reported_s,last_s,count,points,known,heard,framed,first_utc,last_utc\n";
    char line[256];
    const auto utc = [&](double stream_s) {
        return start_unix_ms ? utc_text(*start_unix_ms + static_cast<std::int64_t>(std::llround(1000.0 * stream_s))) : std::string();
    };
    for (const CallRow& r : rows) {
        const double khz = center_hz ? (*center_hz + r.freq_hz) / 1000.0 : 0.0;
        std::snprintf(line, sizeof line, ",%.1f,%s,%.1f,%.1f,%.1f,%u,%.1f,%d,%d,%d,", r.freq_hz,
                      center_hz ? std::to_string(khz).c_str() : "", r.first_s, r.reported_s, r.last_s, r.count, r.points,
                      r.known ? 1 : 0, r.heard ? 1 : 0, r.framed ? 1 : 0);
        out << r.call << ',' << r.base << line << utc(r.first_s) << ',' << utc(r.last_s) << '\n';
    }
    return out.str();
}

}  // namespace kz4ap::callsign
