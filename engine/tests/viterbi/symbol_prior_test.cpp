#include "kz4ap/viterbi/symbol_prior.hpp"

#include "kz4ap/viterbi/morse_tree.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

using namespace kz4ap::viterbi;

namespace {

constexpr double kFloor = 0.002;

// ln p (nats) by symbol.
std::map<std::string, double> priors(double floor = kFloor) {
    const MorseTree tree;
    const std::vector<double> log_prior = on_air_log_prior(tree, floor);
    std::map<std::string, double> out;
    for (std::size_t node = 0; node < tree.size(); ++node) {
        const std::string_view symbol = tree.symbol(static_cast<int>(node));
        if (symbol.empty()) {
            EXPECT_EQ(log_prior[node], -std::numeric_limits<double>::infinity());
        } else {
            out.emplace(std::string(symbol), log_prior[node]);
        }
    }
    return out;
}

}  // namespace

TEST(SymbolPrior, TheTableIsTheGeneratorsWeights) {
    // A checked-in constant of VE3NEA_CHAR_WEIGHTS (training/kz4ap_synth/messages.py): 41 symbols, their total and a weighted
    // sum that changes if two values swap. training/tests/test_messages.py compares every row with the generator's.
    int total = 0;
    long long weighted = 0;
    int position = 0;
    for (const SymbolCount& c : on_air_symbol_counts()) {
        total += c.count;
        weighted += static_cast<long long>(++position) * c.count;
    }
    EXPECT_EQ(position, 41);
    EXPECT_EQ(total, 2688);
    EXPECT_EQ(weighted, 57632);
}

TEST(SymbolPrior, IsAProbabilityOverTheTreesSymbols) {
    const auto p = priors();
    EXPECT_EQ(p.size(), 62u);
    double sum = 0.0;
    for (const auto& [symbol, log_p] : p) sum += std::exp(log_p);
    EXPECT_NEAR(sum, 1.0, 1e-12);
    // The shares of the table, over the sum with the 21 symbols it lacks at the floor.
    const double norm = 1.0 + 21.0 * kFloor;
    EXPECT_NEAR(p.at("E"), std::log(321.0 / 2688.0 / norm), 1e-12);
    EXPECT_NEAR(p.at("<BT>"), std::log(15.0 / 2688.0 / norm), 1e-12);
    for (const char* lacking : {"<AR>", "<KN>", "<SK>", "<SOS>", "$", "-"})
        EXPECT_NEAR(p.at(lacking), std::log(kFloor / norm), 1e-12) << lacking;
    // A floor above a rare symbol's share lifts it too.
    const auto high = priors(0.01);
    EXPECT_DOUBLE_EQ(high.at("J"), high.at("<AR>"));
    EXPECT_GT(high.at("A"), high.at("J"));
    EXPECT_THROW(on_air_log_prior(MorseTree{}, 0.0), std::invalid_argument);
    EXPECT_THROW(on_air_log_prior(MorseTree{}, 1.0), std::invalid_argument);
}

TEST(SymbolPrior, ACallsignIsNotPenalizedAbsurdly) {
    // Against the flat ln 62 = 4.127 nats per symbol: the rarest symbols of the table, J and 6 (count 8), cost 1.73 nats more
    // than flat, and a symbol at the floor at most 2.2; a callsign of rare characters only, JA6XQZ, 4.2 nats in all; a
    // common one, KZ4AP, costs less than flat.
    const auto p = priors();
    const double flat = std::log(62.0);
    double worst = 0.0;
    for (const auto& [symbol, log_p] : p) worst = std::max(worst, -log_p - flat);
    EXPECT_LE(worst, 2.2);
    const auto over_flat = [&](const std::vector<std::string>& call) {
        double cost = 0.0;
        for (const std::string& symbol : call) cost += -p.at(symbol) - flat;
        return cost;
    };
    EXPECT_NEAR(-p.at("J") - flat, 1.73, 0.01);
    EXPECT_NEAR(-p.at("6") - flat, 1.73, 0.01);
    EXPECT_LE(over_flat({"J", "A", "6", "X", "Q", "Z"}), 4.5);
    EXPECT_LE(over_flat({"K", "Z", "4", "A", "P"}), 0.0);
    // Every digit stays dearer than E or T but within the table's own range.
    for (const char* digit : {"0", "1", "2", "3", "4", "5", "6", "7", "8", "9"}) EXPECT_LE(-p.at(digit), 5.9) << digit;
}
