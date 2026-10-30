// The decoder chosen by one name: whatever picks a decoder outside the engine (the app's default, its --decoder option)
// goes through here, so replacing the decoder means changing kDefaultDecoderName.
#pragma once

#include "kz4ap/engine.hpp"

#include <optional>
#include <string_view>

namespace kz4ap {

// The decoder the desktop app runs unless told otherwise.
inline constexpr std::string_view kDefaultDecoderName = "bank";

// envelope, matched, bank or viterbi (the names kz4ap-bench's --decoder takes); none for any other.
std::optional<FrontEnd> front_end_from_name(std::string_view name);
const char* front_end_name(FrontEnd front_end);

// The engine's configuration for the detector path (no oracle channels) with the named decoder, every other value at
// its default, as kz4ap-bench sets them. Throws std::invalid_argument for an unknown name.
EngineConfig engine_config_for_decoder(std::string_view name);

}  // namespace kz4ap
