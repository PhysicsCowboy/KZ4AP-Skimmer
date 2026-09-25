#include "kz4ap/morse.hpp"

#include <cctype>

namespace kz4ap::morse {
namespace {

struct Code {
    char ch;
    std::string_view pattern;
};

// '+' is the prosign AR, '=' is BT, '(' is KN.
constexpr Code kCodes[] = {
    {'A', ".-"},    {'B', "-..."},  {'C', "-.-."},  {'D', "-.."},   {'E', "."},
    {'F', "..-."},  {'G', "--."},   {'H', "...."},  {'I', ".."},    {'J', ".---"},
    {'K', "-.-"},   {'L', ".-.."},  {'M', "--"},    {'N', "-."},    {'O', "---"},
    {'P', ".--."},  {'Q', "--.-"},  {'R', ".-."},   {'S', "..."},   {'T', "-"},
    {'U', "..-"},   {'V', "...-"},  {'W', ".--"},   {'X', "-..-"},  {'Y', "-.--"},
    {'Z', "--.."},
    {'0', "-----"}, {'1', ".----"}, {'2', "..---"}, {'3', "...--"}, {'4', "....-"},
    {'5', "....."}, {'6', "-...."}, {'7', "--..."}, {'8', "---.."}, {'9', "----."},
    {'/', "-..-."}, {'?', "..--.."}, {'.', ".-.-.-"}, {',', "--..--"},
    {'=', "-...-"}, {'+', ".-.-."}, {'-', "-....-"}, {'(', "-.--."},
};

}  // namespace

std::string_view encode(char c) {
    const char upper = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    for (const auto& code : kCodes) {
        if (code.ch == upper) return code.pattern;
    }
    return {};
}

char decode(std::string_view pattern) {
    for (const auto& code : kCodes) {
        if (code.pattern == pattern) return code.ch;
    }
    return '\0';
}

}  // namespace kz4ap::morse
