#include "kz4ap/viterbi/morse_tree.hpp"

#include "kz4ap/morse.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace kz4ap;
using kz4ap::viterbi::MorseTree;

namespace {

// The engine's 62 symbols: 26 letters, 10 digits, 14 punctuation marks and 12 prosigns (the list of morse_test.cpp).
const std::vector<std::string_view> kSymbols = {
    "A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L", "M", "N", "O", "P", "Q", "R", "S",
    "T", "U", "V", "W", "X", "Y", "Z", "0", "1", "2", "3", "4", "5", "6", "7", "8", "9",
    ".", ",", ";", ":", "?", "!", "'", "\"", ")", "/", "-", "$", "@", "_",
    "<AA>", "<AR>", "<AS>", "<BK>", "<BT>", "<CL>", "<HH>", "<KA>", "<KN>", "<SK>", "<SN>", "<SOS>"};

// The node a pattern of '.' and '-' reaches from the root, -1 once a mark leaves the tree.
int walk(const MorseTree& tree, std::string_view pattern) {
    int node = tree.root();
    for (const char mark : pattern) node = tree.child(node, mark == '-');
    return node;
}

}  // namespace

TEST(MorseTree, HoldsTheEngines62Symbols) {
    const MorseTree tree;
    ASSERT_EQ(kSymbols.size(), 62u);
    for (const std::string_view symbol : kSymbols) {
        const std::string_view pattern = morse::encode(symbol);
        ASSERT_FALSE(pattern.empty()) << symbol;
        const int node = walk(tree, pattern);
        ASSERT_GE(node, 0) << symbol << " " << pattern;
        EXPECT_EQ(tree.symbol(node), symbol) << pattern;
    }
    // Exactly the 62 nodes of those patterns hold a symbol; the root holds none.
    int symbol_nodes = 0;
    for (std::size_t node = 0; node < tree.size(); ++node)
        if (!tree.symbol(static_cast<int>(node)).empty()) ++symbol_nodes;
    EXPECT_EQ(symbol_nodes, 62);
    EXPECT_TRUE(tree.symbol(tree.root()).empty());
}

TEST(MorseTree, MarksOutsideTheTreeHaveNoNode) {
    const MorseTree tree;
    // Every prefix of one mark or more of the 62 patterns: the nodes the tree must have, besides the root.
    std::set<std::string> prefixes;
    for (const std::string_view symbol : kSymbols) {
        const std::string pattern(morse::encode(symbol));
        for (std::size_t length = 1; length <= pattern.size(); ++length) prefixes.insert(pattern.substr(0, length));
    }
    // Walk the whole tree, keeping the pattern that leads to each node: a mark has a node below this one exactly
    // where a symbol's pattern goes on with it.
    std::size_t visited = 0;
    std::vector<std::pair<int, std::string>> stack = {{tree.root(), std::string()}};
    while (!stack.empty()) {
        const std::pair<int, std::string> here = stack.back();
        stack.pop_back();
        ++visited;
        bool any_child = false;
        for (const bool dash : {false, true}) {
            const std::string longer = here.second + (dash ? '-' : '.');
            const int next = tree.child(here.first, dash);
            if (prefixes.count(longer) != 0) {
                ASSERT_GE(next, 0) << "no node for " << longer;
                stack.emplace_back(next, longer);
                any_child = true;
            } else {
                EXPECT_EQ(next, -1) << "a node for " << longer << ", which no symbol's pattern begins with";
            }
        }
        EXPECT_EQ(tree.has_children(here.first), any_child) << "after " << here.second;
    }
    // No node is unreachable and none is reached twice: the root and one node per prefix (79 + 1 nodes).
    EXPECT_EQ(visited, prefixes.size() + 1);
    EXPECT_EQ(tree.size(), prefixes.size() + 1);
    EXPECT_EQ(tree.size(), 80u);
}

// morse::decode reads eight or more dots as <HH>, but the table's pattern for it is exactly eight dots. The tree
// follows the table's patterns, so the node of eight dots is a leaf and a ninth dot leaves the tree.
TEST(MorseTree, TheErrorSignalIsExactlyEightDots) {
    const MorseTree tree;
    const int seven = walk(tree, ".......");  // seven dots lead to a node (on the way to eight), but it is no symbol
    ASSERT_GE(seven, 0);
    EXPECT_TRUE(tree.symbol(seven).empty());
    EXPECT_TRUE(tree.has_children(seven));
    const int eight = walk(tree, "........");
    ASSERT_GE(eight, 0);
    EXPECT_EQ(tree.symbol(eight), "<HH>");
    EXPECT_FALSE(tree.has_children(eight));
    EXPECT_EQ(tree.child(eight, false), -1);
    EXPECT_EQ(tree.child(eight, true), -1);
    EXPECT_EQ(walk(tree, "........."), -1);
}

TEST(MorseTree, UnknownNodesAreEmpty) {
    const MorseTree tree;
    const int outside = static_cast<int>(tree.size());
    for (const int node : {-1, outside, outside + 100}) {
        EXPECT_EQ(tree.child(node, false), -1) << node;
        EXPECT_EQ(tree.child(node, true), -1) << node;
        EXPECT_TRUE(tree.symbol(node).empty()) << node;
        EXPECT_FALSE(tree.has_children(node)) << node;
    }
}
