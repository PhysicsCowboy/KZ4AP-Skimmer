#include "kz4ap/decoder_name.hpp"

#include <stdexcept>
#include <string>

namespace kz4ap {

std::optional<FrontEnd> front_end_from_name(std::string_view name) {
    if (name == "envelope") return FrontEnd::Envelope;
    if (name == "matched") return FrontEnd::Matched;
    if (name == "bank") return FrontEnd::Bank;
    if (name == "viterbi") return FrontEnd::Viterbi;
    return std::nullopt;
}

const char* front_end_name(FrontEnd front_end) {
    switch (front_end) {
        case FrontEnd::Envelope: return "envelope";
        case FrontEnd::Matched: return "matched";
        case FrontEnd::Bank: return "bank";
        case FrontEnd::Viterbi: return "viterbi";
    }
    return "unknown";
}

EngineConfig engine_config_for_decoder(std::string_view name) {
    const std::optional<FrontEnd> front_end = front_end_from_name(name);
    if (!front_end) throw std::invalid_argument("unknown decoder: " + std::string(name));
    EngineConfig config;
    if (*front_end == FrontEnd::Envelope) config = with_envelope_path(config);
    config.decoder.front_end = *front_end;
    return config;
}

}  // namespace kz4ap
