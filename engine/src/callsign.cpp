#include "kz4ap/callsign.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <fstream>
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
    if (!allocated_pair(s[0], s[1])) {
        // The one-letter prefixes pass allocated_pair with their digit below, so only a true refusal comes here.
        return false;
    }
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
    if (s.empty() || s.size() > 4 || s.size() < 1) return false;
    if (s.size() == 1) return is_letter(s[0]) && std::string_view("BFGIKMNRW").find(s[0]) != std::string_view::npos;
    return allocated_pair(s[0], s[1]);
}

bool is_keyword(std::string_view w) {
    return any_of_word(w, {"CQ", "DE", "TEST", "QRZ", "TU", "K", "KN", "BK", "SK", "AR", "5NN", "ENN", "599", "NR", "UR", "RST", "R", "UP", "DX", "GM", "GA", "GE", "GN", "OM", "ES", "ES", "FB", "TNX", "TKS", "HW", "73", "QSL", "AGN", "SRI", "QRL", "POTA", "SOTA", "NA", "SS", "FD"});
}

bool is_prosign(std::string_view w) { return !w.empty() && w[0] == '<'; }

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

Role role_from(std::string_view before, std::string_view after, std::string_view before_same, std::string_view after_same) {
    // The word after the call and its repeats says most: W1AW DE, W1AW 5NN.
    const std::string_view next = after_same;
    const std::string_view prev = before_same;
    (void)before;
    (void)after;
    if (any_of_word(next, {"DE"})) return Role::Named;
    if (any_of_word(prev, {"DE"})) return Role::Sender;
    if (named_before(next)) return Role::Named;
    if (sender_after(prev) || sender_before(next)) return Role::Sender;
    return Role::Unknown;
}

}  // namespace

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
        case 'S': return in("0235 6789");  // S0: Western Sahara's, in amateur use, outside the table
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

// ---- Reading a text

std::vector<Word> words_of(std::span<const DecodedSymbol> symbols) {
    std::vector<Word> out;
    Word current;
    const auto flush = [&] {
        if (!current.text.empty()) out.push_back(std::move(current));
        current = Word{};
    };
    for (std::size_t i = 0; i < symbols.size(); ++i) {
        const DecodedSymbol& s = symbols[i];
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

std::vector<CallReading> read_calls(std::span<const Word> words, std::size_t from, std::size_t to) {
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
        if (!p || only_tiny_pieces(p->base)) return;
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
        return role_from(prev, next, prev, next);
    };

    for (std::size_t i = from; i < to; ++i) {
        const std::string& w = words[i].text;
        if (is_prosign(w)) continue;
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
        if (!direct) {
            // A word glued: its pieces, each with the role its neighbors in the word give.
            const std::vector<std::string> pieces = split_glued(w);
            for (std::size_t k = 0; k < pieces.size(); ++k) {
                if (!parse_call(pieces[k])) continue;
                std::string_view prev = k > 0 ? std::string_view(pieces[k - 1]) : before(i);
                std::string_view next = k + 1 < pieces.size() ? std::string_view(pieces[k + 1]) : after(i + 1);
                if (prev == pieces[k]) prev = before(i);
                if (next == pieces[k]) next = after(i + 1);
                const bool twice = pieces.size() >= 2 && ((k > 0 && pieces[k - 1] == pieces[k]) || (k + 1 < pieces.size() && pieces[k + 1] == pieces[k]));
                emit(pieces[k], Reading::Glued, i, i + 1, role_from(prev, next, prev, next), twice);
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
    while (std::getline(in, line)) {
        std::istringstream words(line);
        std::string first;
        if (!(words >> first)) continue;
        if (first[0] == '#' || first[0] == ';') continue;
        out.add(first);
    }
    return out;
}

KnownCalls KnownCalls::read_file(const std::string& path) {
    std::ifstream in(path);
    if (!in) throw std::runtime_error("cannot read the known-call list: " + path);
    return read(in);
}

// ---- The stage

CallSearch::CallSearch(CallSearchConfig config, std::shared_ptr<const KnownCalls> known) : config_(config), known_(std::move(known)) {}

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
        t.symbols.resize(std::min(from - t.base, t.symbols.size()));
        for (std::size_t i = skip; i < k.chars.size(); ++i) t.symbols.push_back(k.chars[i]);
    }
}

void CallSearch::advance(double now_s) {
    now_s_ = now_s;
    if (now_s_ - last_read_s_ < 1.0) return;  // s: read once a second of stream
    last_read_s_ = now_s_;
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
    const std::vector<Word> words = words_of(t.symbols);
    if (words.empty()) return;
    const std::size_t read_at = t.read_to > t.base ? t.read_to - t.base : 0;
    // The first word not yet read, and the words that have settled: over before now - settle_s, with a word behind them, or a track
    // that has ended.
    const double settled = now_s_ - config_.settle_s;
    std::size_t from = 0;
    while (from < words.size() && words[from].first < read_at) ++from;
    std::size_t to = from;
    while (to < words.size() && (all || words[to].end_s <= settled)) ++to;
    if (to == from) return;
    for (const CallReading& r : read_calls(words, from, to)) add(r, t.freq_hz);
    t.read_to = t.base + words[to - 1].last;
    // Keep the context: eight words back. Heuristic: a call's frame is at most a few words.
    const std::size_t keep_word = to >= 8 ? to - 8 : 0;
    const std::size_t drop = words[keep_word].first;
    if (drop > 0) {
        t.symbols.erase(t.symbols.begin(), t.symbols.begin() + static_cast<std::ptrdiff_t>(drop));
        t.base += drop;
    }
}

void CallSearch::add(const CallReading& r, double freq_hz) {
    if (r.special && !(known_ && known_->contains(r.base))) return;
    auto [it, fresh] = candidates_.try_emplace(r.base);
    Candidate& c = it->second;
    if (fresh) {
        c.base = r.base;
        c.special = r.special;
        c.known = known_ && known_->contains(r.base);
        c.first_s = r.start_s;
    }
    const double points = r.reading == Reading::Whole || r.reading == Reading::Glued ? 1.0 : config_.stripped_points;
    // A keyword beside it, or the call sent twice: either way the word is used as a call.
    const bool framed = r.role != Role::Unknown || (r.doubled && config_.doubled_is_framed != 0.0);
    // One transmission, one occurrence: another track's or another reading's copy of the same moment at this place only betters it.
    for (Occurrence& o : c.recent) {
        if (r.start_s < o.end_s && o.start_s < r.end_s && std::abs(o.freq_hz - freq_hz) <= config_.same_place_hz) {
            if (points > o.points) o.points = points;
            if (framed) o.framed = true;
            if (r.role != Role::Unknown && o.role == Role::Unknown) o.role = r.role;
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
    c.recent.push_back(o);
    while (!c.recent.empty() && r.end_s - c.recent.front().end_s > config_.window_s) c.recent.pop_front();
    ++c.count;
    ++c.forms[r.full];
    c.last_s = std::max(c.last_s, r.end_s);
    // Its place: where it last signed, else where it was last read bare, else where another named it.
    const int rank = r.role == Role::Sender ? 2 : (r.role == Role::Unknown ? 1 : 0);
    if (rank >= c.freq_rank) {
        c.freq_rank = rank;
        c.freq_hz = freq_hz;
    }
    c.heard = c.heard || r.role != Role::Named;
    c.framed = c.framed || r.role == Role::Sender;
    evaluate(c, freq_hz, r.end_s);
}

CallSearch::Support CallSearch::support(const Candidate& c, double freq_hz, double now_s, double within_hz) const {
    Support s;
    for (const Occurrence& o : c.recent) {
        if (std::abs(o.freq_hz - freq_hz) > within_hz || now_s - o.end_s > config_.window_s) continue;
        s.points += o.points;
        s.framed = s.framed || o.framed;
    }
    return s;
}

bool CallSearch::passes(const Candidate& c, const Support& s) const {
    const double points = s.points;
    if (c.known) return points >= config_.points_known;
    return points >= (s.framed ? config_.points_framed : config_.points_bare);
}

void CallSearch::evaluate(Candidate& c, double freq_hz, double now_s) {
    const Support s = support(c, freq_hz, now_s, config_.same_place_hz);
    c.best_points = std::max(c.best_points, s.points);
    // Its near variants at this place: which are better supported, and what the weaker add.
    const auto limit_for = [](std::size_t n) { return n >= 4 ? 2 : 1; };
    bool held = false;
    std::vector<Candidate*> beaten;
    const double mine = support(c, freq_hz, now_s, config_.variant_hz).points;
    for (auto& [base, other] : candidates_) {
        if (&other == &c || other.recent.empty()) continue;
        if (now_s - other.recent.back().end_s > config_.variant_s) continue;
        const double theirs = support(other, freq_hz, now_s, config_.variant_hz).points;
        if (theirs <= 0) continue;
        const int limit = limit_for(std::min(c.base.size(), other.base.size()));
        const int d = edit_distance(c.base, other.base, limit);
        if (d > limit) continue;
        // Two letters' difference only against a much better call.
        const double need = d >= 2 ? config_.far_variant_ratio : 1.0;
        const auto beats = [&](const Candidate& a, double pa, const Candidate& b, double pb) {
            if (a.known && !b.known) return pa * config_.known_rival_factor >= pb;
            if (b.known && !a.known) return pa >= config_.known_rival_factor * pb;
            if (pa > need * pb) return true;
            return pa == pb && d == 1 && a.first_s < b.first_s;
        };
        if (beats(other, theirs, c, mine)) {
            held = true;
        } else if (beats(c, mine, other, theirs)) {
            if (other.reported) beaten.push_back(&other);
        }
    }
    if (held) {
        if (c.reported) withdraw(c);
        return;
    }
    if (c.reported) {
        update_row(c);
    } else if (passes(c, s)) {
        report(c, now_s);
    }
    if (c.reported) {
        for (Candidate* b : beaten) withdraw(*b);
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
    it->count = c.count;
    it->points = c.best_points;
    it->known = c.known;
    it->heard = c.heard;
    it->framed = c.framed;
}

void CallSearch::prune(double now_s) {
    // What has not been reported and has aged out of every window goes; a live run's junk would otherwise grow without bound.
    const double keep = std::max(config_.window_s, config_.variant_s);
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

std::string calls_csv(const std::vector<CallRow>& rows, std::optional<double> center_hz) {
    std::ostringstream out;
    out << "call,base,freq_hz,freq_khz,first_s,reported_s,last_s,count,points,known,heard,framed\n";
    char line[256];
    for (const CallRow& r : rows) {
        const double khz = center_hz ? (*center_hz + r.freq_hz) / 1000.0 : 0.0;
        std::snprintf(line, sizeof line, ",%.1f,%s,%.1f,%.1f,%.1f,%u,%.1f,%d,%d,%d\n", r.freq_hz,
                      center_hz ? std::to_string(khz).c_str() : "", r.first_s, r.reported_s, r.last_s, r.count, r.points,
                      r.known ? 1 : 0, r.heard ? 1 : 0, r.framed ? 1 : 0);
        out << r.call << ',' << r.base << line;
    }
    return out.str();
}

}  // namespace kz4ap::callsign
