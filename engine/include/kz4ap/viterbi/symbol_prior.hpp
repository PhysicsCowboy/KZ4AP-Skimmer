// The prior of a symbol (independent review of faa283f, 2026-10-30, F2). A flat 1/62 makes two letters cost ln 62 + 0.396
// nats more than one merged symbol, so a gap reads as a letter gap only above 2.51 dots (derived), and a fifth of a
// paddle's letter gaps fall below that. The prior here is each symbol's share of VE3NEA's on-air counts, the table the
// generator draws its filler text from (training/kz4ap_synth/messages.py, VE3NEA_CHAR_WEIGHTS; said there to be
// collected from CW Skimmer's decodes on the Ham bands, so its digits and rare letters are those of real callsigns):
//
//     p(symbol) = max(count / total, floor) / (the sum of that over the tree's 62 symbols)        (1)
//
// The 21 symbols the table lacks (<AR>, <KN>, <SK> among them) have count 0 and stand at the floor; at the default floor,
// 0.002, every symbol the table has stands above it. The floor is a heuristic (config.hpp, symbol_prior_floor).
#pragma once

#include "kz4ap/viterbi/morse_tree.hpp"

#include <span>
#include <string_view>
#include <vector>

namespace kz4ap::viterbi {

struct SymbolCount {
    std::string_view symbol;
    int count;
};

// The on-air counts (1), in the order of the source. training/tests/test_messages.py checks them against the generator's.
std::span<const SymbolCount> on_air_symbol_counts();

// By node of the tree: ln p(symbol) (nats, (1)) at a node holding a symbol, -infinity at any other. floor: a probability
// in (0, 1). Throws std::invalid_argument for a floor outside that, and if the table names a symbol the tree lacks.
std::vector<double> on_air_log_prior(const MorseTree& tree, double floor);

}  // namespace kz4ap::viterbi
