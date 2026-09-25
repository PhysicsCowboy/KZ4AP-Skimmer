#pragma once

#include <string_view>
#include <vector>

namespace kz4ap::morse {

// A symbol is one character ("A", "5", "?") or a prosign token ("<SK>").

// Dot/dash pattern for a symbol (case-insensitive), e.g. ".-" for "A".
// Returns an empty view for symbols that have no Morse code.
std::string_view encode(std::string_view symbol);

// Symbol for a dot/dash pattern. Eight or more dits decode as "<HH>" (error).
// Returns an empty view if the pattern is not a known code.
std::string_view decode(std::string_view pattern);

// Splits a word into symbols, keeping "<...>" prosign tokens whole.
std::vector<std::string_view> symbols(std::string_view word);

}  // namespace kz4ap::morse
