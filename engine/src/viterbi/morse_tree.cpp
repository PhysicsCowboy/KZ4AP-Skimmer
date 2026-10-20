#include "kz4ap/viterbi/morse_tree.hpp"

#include "kz4ap/morse.hpp"

#include <string>

namespace kz4ap::viterbi {
namespace {

// The marks in the longest pattern of the table: <SOS>, "...---...".
constexpr int kLongestPatternMarks = 9;

}  // namespace

MorseTree::MorseTree() {
    nodes_.push_back(Node{{-1, -1}, {}});  // the root
    // Take every pattern of dots and dashes from one mark to the longest, shortest first, and keep those the table
    // gives a symbol. morse::decode also reads nine dots as "<HH>" (eight or more dots are the error signal), but the
    // table's pattern for it is eight: encode() tells the table's patterns from that alias.
    std::string pattern;
    for (int length = 1; length <= kLongestPatternMarks; ++length) {
        for (unsigned bits = 0; bits < (1u << length); ++bits) {
            pattern.assign(static_cast<std::size_t>(length), '.');
            for (int mark = 0; mark < length; ++mark)
                if (((bits >> (length - 1 - mark)) & 1u) != 0) pattern[static_cast<std::size_t>(mark)] = '-';
            const std::string_view symbol = morse::decode(pattern);
            if (symbol.empty() || morse::encode(symbol) != pattern) continue;
            // Down the tree along the pattern, adding the nodes it lacks, to the node of the symbol.
            int node = root();
            for (const char mark : pattern) {
                const std::size_t side = mark == '-' ? 1 : 0;
                if (nodes_[static_cast<std::size_t>(node)].child[side] < 0) {
                    nodes_.push_back(Node{{-1, -1}, {}});
                    nodes_[static_cast<std::size_t>(node)].child[side] = static_cast<int>(nodes_.size()) - 1;
                }
                node = nodes_[static_cast<std::size_t>(node)].child[side];
            }
            nodes_[static_cast<std::size_t>(node)].symbol = symbol;
        }
    }
}

int MorseTree::child(int node, bool dash) const {
    if (node < 0 || static_cast<std::size_t>(node) >= nodes_.size()) return -1;
    return nodes_[static_cast<std::size_t>(node)].child[dash ? 1 : 0];
}

std::string_view MorseTree::symbol(int node) const {
    if (node < 0 || static_cast<std::size_t>(node) >= nodes_.size()) return {};
    return nodes_[static_cast<std::size_t>(node)].symbol;
}

bool MorseTree::has_children(int node) const {
    if (node < 0 || static_cast<std::size_t>(node) >= nodes_.size()) return false;
    const Node& n = nodes_[static_cast<std::size_t>(node)];
    return n.child[0] >= 0 || n.child[1] >= 0;
}

}  // namespace kz4ap::viterbi
