// VE3NEA's CW character frequencies and word-length probabilities, copied exactly from
// training/kz4ap_synth/messages.py (VE3NEA_CHAR_WEIGHTS, VE3NEA_WORD_LENGTH_PROBS), in the prototype's order.
// They come from VE3NEA's DeepCW (https://github.com/VE3NEA/DeepCW, commit
// 2c8fdac01bb2bf07d80989b0e5aabfe8cb87d76b, MIT License, Copyright (c) 2024 Alex Shovkoplyas), where they are
// said to be "collected from a large number of CW messages decoded with CW Skimmer on the Ham bands"; his "="
// (-...-) is written "<BT>". Used by the duration fit's class priors (fit.cpp) and the text model
// (selection.cpp).
//
// This notice applies to the two tables in this file only.
//
// MIT License
//
// Copyright (c) 2024 Alex Shovkoplyas
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.
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
