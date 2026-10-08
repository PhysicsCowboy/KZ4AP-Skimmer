// VE3NEA's CW character frequencies and word-length probabilities, copied exactly from
// training/kz4ap_synth/messages.py (VE3NEA_CHAR_WEIGHTS, VE3NEA_WORD_LENGTH_PROBS), in the prototype's order.
// They come from VE3NEA's DeepCW (https://github.com/VE3NEA/DeepCW, commit
// 2c8fdac01bb2bf07d80989b0e5aabfe8cb87d76b, MIT License, Copyright (c) 2024 Alex Shovkoplyas), where they are
// said to be "collected from a large number of CW messages decoded with CW Skimmer on the Ham bands"; his "="
// (-...-) is written "<BT>". Used by the duration fit's class priors (fit.cpp) and the text model
// (selection.cpp).
#pragma once

#include <string_view>

namespace kz4ap::bank::detail {

struct CharWeight {
    std::string_view symbol;
    int weight;
};

inline constexpr CharWeight kVe3neaCharWeights[] = {
    {"1", 13},  {"2", 14},  {"3", 33},  {"4", 43},  {"5", 41},  {"6", 8},   {"7", 14},  {"8", 10},  {"9", 14},
    {"0", 11},  {"A", 127}, {"B", 62},  {"C", 69},  {"D", 84},  {"E", 321}, {"F", 55},  {"G", 43},  {"H", 68},
    {"I", 130}, {"J", 8},   {"K", 117}, {"L", 100}, {"M", 76},  {"N", 168}, {"O", 126}, {"P", 57},  {"Q", 68},
    {"R", 95},  {"S", 159}, {"T", 236}, {"U", 61},  {"V", 23},  {"W", 95},  {"X", 16},  {"Y", 40},  {"Z", 12},
    {"/", 19},  {".", 12},  {",", 9},   {"?", 16},  {"<BT>", 15},
};

// Index = word length, characters.
inline constexpr double kVe3neaWordLengthProbs[] = {0.0,   0.1672, 0.2569, 0.1939, 0.1745, 0.0921,
                                                    0.025, 0.008,  0.006,  0.004,  0.003,  0.003,
                                                    0.002, 0.002,  0.002,  0.001,  0.001};

}  // namespace kz4ap::bank::detail
