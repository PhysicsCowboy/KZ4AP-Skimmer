#include "kz4ap/morse.hpp"

#include <cctype>

namespace kz4ap::morse {
namespace {

struct Code {
    std::string_view symbol;
    std::string_view pattern;
};

// Prosigns are written as <XX> tokens. Four of them share a code with a
// character (<BT> '=', <AR> '+', <AS> '&', <KN> '('); the prosign wins, so
// those characters are not in the table. Must stay identical to
// training/kz4ap_synth/morse.py (a Python test compares them).
constexpr Code kCodes[] = {
    {"A", ".-"},    {"B", "-..."},  {"C", "-.-."},  {"D", "-.."},   {"E", "."},
    {"F", "..-."},  {"G", "--."},   {"H", "...."},  {"I", ".."},    {"J", ".---"},
    {"K", "-.-"},   {"L", ".-.."},  {"M", "--"},    {"N", "-."},    {"O", "---"},
    {"P", ".--."},  {"Q", "--.-"},  {"R", ".-."},   {"S", "..."},   {"T", "-"},
    {"U", "..-"},   {"V", "...-"},  {"W", ".--"},   {"X", "-..-"},  {"Y", "-.--"},
    {"Z", "--.."},
    {"0", "-----"}, {"1", ".----"}, {"2", "..---"}, {"3", "...--"}, {"4", "....-"},
    {"5", "....."}, {"6", "-...."}, {"7", "--..."}, {"8", "---.."}, {"9", "----."},
    {".", ".-.-.-"}, {",", "--..--"}, {";", "-.-.-."}, {":", "---..."}, {"?", "..--.."},
    {"!", "-.-.--"}, {"'", ".----."}, {"\"", ".-..-."}, {")", "-.--.-"}, {"/", "-..-."},
    {"-", "-....-"}, {"$", "...-..-"}, {"@", ".--.-."}, {"_", "..--.-"},
    {"<AA>", ".-.-"},     {"<AR>", ".-.-."},     {"<AS>", ".-..."},  {"<BK>", "-...-.-"},
    {"<BT>", "-...-"},    {"<CL>", "-.-..-.."},  {"<HH>", "........"}, {"<KA>", "-.-.-"},
    {"<KN>", "-.--."},    {"<SK>", "...-.-"},    {"<SN>", "...-."},  {"<SOS>", "...---..."},
};

bool same_ignoring_case(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (std::toupper(static_cast<unsigned char>(a[i])) != std::toupper(static_cast<unsigned char>(b[i])))
            return false;
    }
    return true;
}

}  // namespace

std::string_view encode(std::string_view symbol) {
    for (const auto& code : kCodes) {
        if (same_ignoring_case(code.symbol, symbol)) return code.pattern;
    }
    return {};
}

std::string_view decode(std::string_view pattern) {
    // Operators send the error signal as eight dits or more.
    if (pattern.size() >= 8 && pattern.find_first_not_of('.') == std::string_view::npos) return "<HH>";
    for (const auto& code : kCodes) {
        if (code.pattern == pattern) return code.symbol;
    }
    return {};
}

std::vector<std::string_view> symbols(std::string_view word) {
    std::vector<std::string_view> out;
    std::size_t i = 0;
    while (i < word.size()) {
        if (word[i] == '<') {
            const auto close = word.find('>', i);
            if (close != std::string_view::npos) {
                out.push_back(word.substr(i, close - i + 1));
                i = close + 1;
                continue;
            }
        }
        out.push_back(word.substr(i, 1));
        ++i;
    }
    return out;
}

}  // namespace kz4ap::morse
