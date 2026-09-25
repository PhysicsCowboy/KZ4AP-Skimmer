#pragma once

#include <string_view>

namespace kz4ap::morse {

// Dot/dash pattern for a character (case-insensitive), e.g. ".-" for 'A'.
// Returns an empty view for characters that have no Morse code.
std::string_view encode(char c);

// Character for a dot/dash pattern, or '\0' if the pattern is not a known code.
char decode(std::string_view pattern);

}  // namespace kz4ap::morse
