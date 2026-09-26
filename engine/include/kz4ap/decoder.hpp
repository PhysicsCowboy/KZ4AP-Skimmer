#pragma once

#include "kz4ap/types.hpp"

#include <span>
#include <string>
#include <vector>

namespace kz4ap {

struct DecodedSymbol {
    std::string text;   // "A", "?", "<SK>"; " " marks a word space, "*" a pattern with no Morse code
    float probability;  // the decoder's belief that text is right, 0..1
    double start_s;
    double end_s;
};

struct DecodeUpdate {
    std::vector<DecodedSymbol> chars;
    float wpm = 0;
    float confidence = 0;  // running average of recent character probabilities
};

// Decodes one channel's baseband stream into characters.
class Decoder {
public:
    virtual ~Decoder() = default;
    // samples: channel baseband; t0_s: time of samples[0].
    virtual DecodeUpdate process(std::span<const Sample> samples, double t0_s) = 0;
    // Emits any partly received character (end of input, or the track died).
    virtual DecodeUpdate flush() = 0;
    // Forgets all state (e.g. after a gap in the input).
    virtual void reset() = 0;
};

}  // namespace kz4ap
