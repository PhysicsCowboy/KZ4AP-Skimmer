// The Morse tree: the tree of dot-and-dash patterns that spell the engine's 62 symbols (kz4ap::morse, src/morse.cpp:
// 26 letters, 10 digits, 14 punctuation marks and 12 prosigns). A node is a prefix of at least one symbol's pattern;
// the root is the empty prefix. A node holds a symbol when its prefix is that symbol's whole pattern. A mark that would
// leave the tree has no node. The tree follows the table's patterns exactly: morse::decode reads eight or more dots as
// "<HH>", but the tree holds only the node of eight dots, a leaf. What each symbol costs is the search's
// (symbol_prior.hpp).
#pragma once

#include <cstddef>
#include <string_view>
#include <vector>

namespace kz4ap::viterbi {

class MorseTree {
public:
    // Builds the tree from kz4ap::morse.
    MorseTree();

    // The empty prefix: no mark of the letter sent yet.
    int root() const { return 0; }
    // The node after one more mark, a dash or a dot; -1 if no symbol's pattern goes on that way, and for a node that
    // does not exist.
    int child(int node, bool dash) const;
    // The symbol ("A", "5", "?", "<SK>") whose whole pattern leads to the node; empty if none, and for a node that
    // does not exist.
    std::string_view symbol(int node) const;
    // Whether some symbol's pattern goes on from the node (the letter may continue).
    bool has_children(int node) const;
    // The number of nodes, the root included.
    std::size_t size() const { return nodes_.size(); }

private:
    struct Node {
        int child[2];             // after a dot, after a dash; -1 if none
        std::string_view symbol;  // empty if the node is no symbol's whole pattern
    };
    std::vector<Node> nodes_;
};

}  // namespace kz4ap::viterbi
