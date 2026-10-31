#include "kz4ap/viterbi/symbol_prior.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <string>

namespace kz4ap::viterbi {
namespace {

// VE3NEA_CHAR_WEIGHTS of training/kz4ap_synth/messages.py. training/tests/test_messages.py reads the rows between the
// two marks and checks that they are the generator's.
// VE3NEA_CHAR_WEIGHTS begin
constexpr SymbolCount kCounts[] = {
    {"1", 13}, {"2", 14}, {"3", 33}, {"4", 43}, {"5", 41}, {"6", 8}, {"7", 14}, {"8", 10}, {"9", 14}, {"0", 11},
    {"A", 127}, {"B", 62}, {"C", 69}, {"D", 84}, {"E", 321}, {"F", 55}, {"G", 43}, {"H", 68}, {"I", 130},
    {"J", 8}, {"K", 117}, {"L", 100}, {"M", 76}, {"N", 168}, {"O", 126}, {"P", 57}, {"Q", 68}, {"R", 95},
    {"S", 159}, {"T", 236}, {"U", 61}, {"V", 23}, {"W", 95}, {"X", 16}, {"Y", 40}, {"Z", 12},
    {"/", 19}, {".", 12}, {",", 9}, {"?", 16}, {"<BT>", 15},
};
// VE3NEA_CHAR_WEIGHTS end

}  // namespace

std::span<const SymbolCount> on_air_symbol_counts() { return kCounts; }

std::vector<double> on_air_log_prior(const MorseTree& tree, double floor) {
    if (!(floor > 0.0 && floor < 1.0)) throw std::invalid_argument("on_air_log_prior: the floor must lie in (0, 1)");
    double total = 0.0;
    for (const SymbolCount& c : kCounts) total += c.count;
    std::vector<double> share(tree.size(), 0.0);
    std::size_t found = 0;
    double sum = 0.0;
    for (std::size_t node = 0; node < tree.size(); ++node) {
        const std::string_view symbol = tree.symbol(static_cast<int>(node));
        if (symbol.empty()) continue;
        double count = 0.0;
        for (const SymbolCount& c : kCounts)
            if (c.symbol == symbol) {
                count = c.count;
                ++found;
            }
        share[node] = std::max(count / total, floor);
        sum += share[node];
    }
    if (found != std::size(kCounts))
        throw std::invalid_argument("on_air_log_prior: the table has symbols the tree lacks");
    std::vector<double> log_prior(tree.size(), -std::numeric_limits<double>::infinity());
    for (std::size_t node = 0; node < tree.size(); ++node)
        if (share[node] > 0.0) log_prior[node] = std::log(share[node] / sum);
    return log_prior;
}

}  // namespace kz4ap::viterbi
