// Bell's letter tree by rule N1: the seven tests of the reference sheet, section (f) ("Tests the reconstructed file
// must pass"), the sizes it derives, his alphabet, and the MORSEM file read back as INITL reads it.
#include "kz4ap/bell/tree.hpp"

#include "kz4ap/bell/config.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using kz4ap::bell::at1;
using kz4ap::bell::BellConfig;
using kz4ap::bell::bell_tree;
using kz4ap::bell::MorseTree;
using kz4ap::bell::TreeAlphabet;

constexpr std::array<TreeAlphabet, 3> kAlphabets = {TreeAlphabet::proposed, TreeAlphabet::letters_digits,
                                                    TreeAlphabet::with_semicolon_colon};

std::string name_of(TreeAlphabet a) {
    switch (a) {
    case TreeAlphabet::proposed: return "proposed";
    case TreeAlphabet::letters_digits: return "letters_digits";
    default: return "with_semicolon_colon";
    }
}

int memfcn(const MorseTree& t, int lambda, int k) { return at1(at1(t.memfcn, lambda), k); }

// INITL's DATA rows 1-16 [p. 161 06400-07000, column-major there]: MEMFCN(λ, dot ... pause).
const std::array<std::array<int, 6>, 16> kDataMemfcn = {{
    {9, 10, 1, 0, 0, 0},  {11, 12, 0, 2, 0, 0}, {13, 14, 0, 0, 3, 0}, {15, 16, 0, 0, 0, 4},
    {9, 10, 5, 0, 0, 0},  {11, 12, 0, 6, 0, 0}, {13, 14, 0, 0, 7, 0}, {15, 16, 0, 0, 0, 8},
    {9, 0, 1, 2, 3, 4},   {0, 10, 5, 6, 7, 8},  {11, 0, 1, 2, 3, 4},  {0, 12, 5, 6, 7, 8},
    {13, 0, 1, 2, 3, 4},  {0, 14, 5, 6, 7, 8},  {15, 0, 1, 2, 3, 4},  {0, 16, 5, 6, 7, 8},
}};

// Element-state m = 1 ... 16 as (previous element, current element), Table XII's labels (reference sheet (b),
// ELEMTR): typed here from the sheet, independently of the configuration's ILAM1.
const std::array<std::pair<int, int>, 16> kElementStateLabel = {{
    {1, 3}, {1, 4}, {1, 5}, {1, 6}, {2, 3}, {2, 4}, {2, 5}, {2, 6},
    {3, 1}, {3, 2}, {4, 1}, {4, 2}, {5, 1}, {5, 2}, {6, 1}, {6, 2},
}};

// The alphabet in International Morse and the sheet's proposals, typed here independently of tree.cpp's table:
// text, Morse code, IALPH code, and the alphabets that hold it.
struct Expected {
    const char* text;
    const char* morse;
    int code;
};
const Expected kLettersDigits[] = {
    {"A", ".-", 1},     {"B", "-...", 2},   {"C", "-.-.", 3},   {"D", "-..", 4},    {"E", ".", 5},
    {"F", "..-.", 6},   {"G", "--.", 7},    {"H", "....", 8},   {"I", "..", 9},     {"J", ".---", 10},
    {"K", "-.-", 11},   {"L", ".-..", 12},  {"M", "--", 13},    {"N", "-.", 14},    {"O", "---", 15},
    {"P", ".--.", 16},  {"Q", "--.-", 17},  {"R", ".-.", 18},   {"S", "...", 19},   {"T", "-", 20},
    {"U", "..-", 21},   {"V", "...-", 22},  {"W", ".--", 23},   {"X", "-..-", 43},  {"Y", "-.--", 25},
    {"Z", "--..", 26},  {"1", ".----", 27}, {"2", "..---", 28}, {"3", "...--", 29}, {"4", "....-", 30},
    {"5", ".....", 31}, {"6", "-....", 32}, {"7", "--...", 33}, {"8", "---..", 34}, {"9", "----.", 35},
    {"0", "-----", 36},
};
const Expected kExtra[] = {
    {".", ".-.-.-", 44}, {"AS", ".-...", 45}, {"SN", "...-.", 46}, {"NR", "-..-.", 51},  {"NO", "-.---", 52},
    {"GA", "--..-", 53}, {"OK", "----.-", 54}, {"AR", ".-.-.", 55}, {"SK", "...-.-", 56}, {"IMI", "..--..", 61},
    {"BT", "-...-", 66},
};
const Expected kSemicolonColon[] = {{";", "-.-.-.", 37}, {":", "---...", 38}};

std::vector<Expected> expected_symbols(TreeAlphabet a) {
    std::vector<Expected> out(std::begin(kLettersDigits), std::end(kLettersDigits));
    if (a != TreeAlphabet::letters_digits) out.insert(out.end(), std::begin(kExtra), std::end(kExtra));
    if (a == TreeAlphabet::with_semicolon_colon)
        out.insert(out.end(), std::begin(kSemicolonColon), std::end(kSemicolonColon));
    return out;
}

// The trie of an alphabet, computed here from the expected codes.
struct Trie {
    std::set<std::string> nodes;
    std::map<std::string, int> code;  // symbols only
    bool has(const std::string& v) const { return nodes.count(v) != 0; }
    bool symbol(const std::string& v) const { return code.count(v) != 0; }
    bool has_child(const std::string& v) const { return has(v + ".") || has(v + "-"); }
};
Trie trie_of(TreeAlphabet a) {
    Trie t;
    for (const Expected& e : expected_symbols(a)) {
        const std::string w = e.morse;
        for (std::size_t n = 1; n <= w.size(); ++n) t.nodes.insert(w.substr(0, n));
        t.code[w] = e.code;
    }
    return t;
}

int element_of(char c) { return c == '.' ? 1 : 2; }

}  // namespace

TEST(BellTree, IndexHelperIsOneBasedAndChecked) {
    std::array<int, 3> a = {7, 8, 9};
    EXPECT_EQ(at1(a, 1), 7);
    EXPECT_EQ(at1(a, 3), 9);
    EXPECT_THROW(at1(a, 0), std::out_of_range);
    EXPECT_THROW(at1(a, 4), std::out_of_range);
    at1(a, 2) = 5;
    EXPECT_EQ(a[1], 5);
}

// IALPH as listed [p. 161 07800-08300].
TEST(BellTree, AlphabetIsTheListings) {
    const MorseTree t = bell_tree();
    ASSERT_EQ(t.alphabet.size(), 70u);
    const std::string letters = "ABCDEFGHIJKLMNOPQRSTUVWXYZ1234567890";
    for (int c = 1; c <= 36; ++c) EXPECT_EQ(at1(t.alphabet, c), std::string(1, letters[c - 1])) << c;
    const std::map<int, std::string> rest = {
        {37, ";"},  {38, ":"},  {39, "%"},  {40, "&"},  {43, "X"},  {44, "."},   {45, "AS"}, {46, "SN"},
        {51, "NR"}, {52, "NO"}, {53, "GA"}, {54, "OK"}, {55, "AR"}, {56, "SK"}, {61, "IMI"}, {66, "BT"},
        {70, "EEE"}};
    for (int c = 37; c <= 70; ++c) {
        const auto it = rest.find(c);
        EXPECT_EQ(at1(t.alphabet, c), it == rest.end() ? std::string() : it->second) << c;
    }
}

// Each alphabet holds exactly its symbols, with their International Morse or proposed codes (reference sheet (f)).
TEST(BellTree, SymbolsAndTheirCodes) {
    for (TreeAlphabet a : kAlphabets) {
        const MorseTree t = bell_tree(a);
        ASSERT_EQ(t.morse.size(), 70u);
        std::set<int> in_tree;
        for (const Expected& e : expected_symbols(a)) {
            EXPECT_EQ(at1(t.alphabet, e.code), e.text) << name_of(a);
            EXPECT_EQ(at1(t.morse, e.code), e.morse) << name_of(a) << " " << e.text;
            in_tree.insert(e.code);
        }
        for (int c = 1; c <= 70; ++c)
            if (in_tree.count(c) == 0) EXPECT_EQ(at1(t.morse, c), "") << name_of(a) << " code " << c;
        EXPECT_EQ(at1(t.morse, 24), "") << "X's node takes code 43";
    }
}

// Sizes derived in reference sheet (f): 47 symbols, 52 nodes, 227 states; 36: 39, 173; 49: 55, 239.
TEST(BellTree, SizesAreTheSheets) {
    const std::map<TreeAlphabet, std::array<int, 3>> expected = {{TreeAlphabet::proposed, {47, 52, 227}},
                                                                  {TreeAlphabet::letters_digits, {36, 39, 173}},
                                                                  {TreeAlphabet::with_semicolon_colon, {49, 55, 239}}};
    for (const auto& [a, sizes] : expected) {
        const MorseTree t = bell_tree(a);
        int symbols = 0;
        for (const std::string& m : t.morse) symbols += m.empty() ? 0 : 1;
        EXPECT_EQ(symbols, sizes[0]) << name_of(a);
        EXPECT_EQ(std::set<std::string>(t.node.begin(), t.node.end()).size(), static_cast<std::size_t>(sizes[1]))
            << name_of(a);
        EXPECT_EQ(t.states(), sizes[2]) << name_of(a);
        EXPECT_EQ(t.ltrmap.size(), t.ielmst.size());
        EXPECT_EQ(t.memfcn.size(), t.ielmst.size());
        EXPECT_EQ(t.node.size(), t.ielmst.size());
        EXPECT_EQ(trie_of(a).nodes.size(), static_cast<std::size_t>(sizes[1])) << name_of(a);
    }
}

// Test 1, every symbol decodes: from every letter-ending state, the symbol's elements with an e-sp between marks,
// then a c-sp, a w-sp or a pause, reach a letter-ending state whose LTRMAP is the symbol's code, through nonzero
// states that stay put on their own element.
TEST(BellTree, Test1EverySymbolDecodes) {
    const BellConfig config;
    for (TreeAlphabet a : kAlphabets) {
        const MorseTree t = bell_tree(a);
        const std::vector<int> blanks = kz4ap::bell::iblank(t, config);
        std::vector<int> starts;
        for (int lambda = 1; lambda <= t.states(); ++lambda) {
            const int m = at1(t.ielmst, lambda);
            if (m != 1 && m != 5 && m <= 8) starts.push_back(lambda);
        }
        ASSERT_FALSE(starts.empty());
        for (const Expected& e : expected_symbols(a)) {
            const std::string w = e.morse;
            for (int start : starts) {
                for (int s = kz4ap::bell::kCharacterSpace; s <= kz4ap::bell::kPause; ++s) {
                    int lambda = start;
                    for (std::size_t i = 0; i < w.size(); ++i) {
                        if (i > 0) {
                            lambda = memfcn(t, lambda, kz4ap::bell::kElementSpace);
                            ASSERT_NE(lambda, 0) << e.text << " e-sp " << i;
                            ASSERT_EQ(memfcn(t, lambda, kz4ap::bell::kElementSpace), lambda);
                        }
                        lambda = memfcn(t, lambda, element_of(w[i]));
                        ASSERT_NE(lambda, 0) << e.text << " element " << i;
                        ASSERT_EQ(memfcn(t, lambda, element_of(w[i])), lambda);
                    }
                    lambda = memfcn(t, lambda, s);
                    ASSERT_NE(lambda, 0) << e.text << " space " << s;
                    EXPECT_EQ(at1(t.ltrmap, lambda), e.code) << name_of(a) << " " << e.text;
                    // the element-state (last mark, space) and the blanks TRANSL prints after it
                    const int last = element_of(w.back());
                    EXPECT_EQ(at1(kElementStateLabel, at1(t.ielmst, lambda)), std::make_pair(last, s));
                    EXPECT_EQ(at1(blanks, lambda), s == kz4ap::bell::kCharacterSpace ? 0
                                                    : s == kz4ap::bell::kWordSpace   ? 1
                                                                                     : 2);
                }
            }
        }
    }
}

// Test 2, every state is reachable from state 5, PROCES's start state.
TEST(BellTree, Test2EveryStateIsReachableFromTheStartState) {
    for (TreeAlphabet a : kAlphabets) {
        const MorseTree t = bell_tree(a);
        std::vector<bool> seen(static_cast<std::size_t>(t.states()) + 1, false);
        const int start = BellConfig{}.start_state;
        ASSERT_EQ(start, 5);
        std::vector<int> queue = {start};
        seen[static_cast<std::size_t>(start)] = true;
        while (!queue.empty()) {
            const int lambda = queue.back();
            queue.pop_back();
            for (int k = 1; k <= 6; ++k) {
                const int next = memfcn(t, lambda, k);
                ASSERT_GE(next, 0);
                ASSERT_LE(next, t.states());
                if (next != 0 && !seen[static_cast<std::size_t>(next)]) {
                    seen[static_cast<std::size_t>(next)] = true;
                    queue.push_back(next);
                }
            }
        }
        for (int lambda = 1; lambda <= t.states(); ++lambda)
            EXPECT_TRUE(seen[static_cast<std::size_t>(lambda)]) << name_of(a) << " state " << lambda;
    }
}

// Test 3, the DATA rows: IELMST(λ) = λ for λ = 1 ... 16; MEMFCN rows 1-4, 6-8 and 11-16 equal INITL's DATA rows;
// rows 5, 9 and 10 differ only in their links into the tree: each entry leads to a state of the same element-state
// as DATA's (whose IELMST(λ) = λ), and the links are 5: dot to N's mark, dash to M's; 9 and 10: the spaces of I and
// of A.
TEST(BellTree, Test3TheDataRows) {
    for (TreeAlphabet a : kAlphabets) {
        const MorseTree t = bell_tree(a);
        for (int lambda = 1; lambda <= 16; ++lambda) {
            EXPECT_EQ(at1(t.ielmst, lambda), lambda);
            const auto& data = at1(kDataMemfcn, lambda);
            const bool kept = lambda != 5 && lambda != 9 && lambda != 10;
            for (int k = 1; k <= 6; ++k) {
                const int ours = memfcn(t, lambda, k);
                const int theirs = at1(data, k);
                if (kept) {
                    EXPECT_EQ(ours, theirs) << name_of(a) << " row " << lambda << " element " << k;
                } else if (theirs == 0) {
                    EXPECT_EQ(ours, 0) << name_of(a) << " row " << lambda << " element " << k;
                } else {
                    ASSERT_NE(ours, 0) << name_of(a) << " row " << lambda << " element " << k;
                    EXPECT_EQ(at1(t.ielmst, ours), theirs) << name_of(a) << " row " << lambda << " element " << k;
                }
            }
        }
        // the links
        EXPECT_EQ(at1(t.node, 5), "-");
        EXPECT_EQ(at1(t.node, memfcn(t, 5, 1)), "-.");  // N
        EXPECT_EQ(at1(t.node, memfcn(t, 5, 2)), "--");  // M
        EXPECT_EQ(at1(t.node, 9), "..");                 // I
        EXPECT_EQ(at1(t.node, 10), ".-");                // A
        for (int k = 3; k <= 6; ++k) {
            EXPECT_EQ(at1(t.node, memfcn(t, 9, k)), "..");
            EXPECT_EQ(at1(t.node, memfcn(t, 10, k)), ".-");
        }
        // rows 5, 9 and 10 do differ from DATA (the reason the plan's 16-row test was replaced)
        EXPECT_NE(at1(t.memfcn, 5), at1(kDataMemfcn, 5));
        EXPECT_NE(at1(t.memfcn, 9), at1(kDataMemfcn, 9));
        EXPECT_NE(at1(t.memfcn, 10), at1(kDataMemfcn, 10));
    }
}

// Test 4, staying: MEMFCN(λ, ILAM1(IELMST(λ))) = λ for every state.
TEST(BellTree, Test4Staying) {
    const BellConfig config;
    for (TreeAlphabet a : kAlphabets) {
        const MorseTree t = bell_tree(a);
        for (int lambda = 1; lambda <= t.states(); ++lambda)
            EXPECT_EQ(memfcn(t, lambda, at1(config.ilam1, at1(t.ielmst, lambda))), lambda) << lambda;
    }
}

// Test 5, consistency with ELEMTR: every nonzero MEMFCN(λ, k), k not λ's own element, has ELEMTR(IELMST(λ), k) > 0,
// and the successor's IELMST is the element-state (λ's element, k).
TEST(BellTree, Test5ConsistencyWithElemtr) {
    const BellConfig config;
    for (TreeAlphabet a : kAlphabets) {
        const MorseTree t = bell_tree(a);
        for (int lambda = 1; lambda <= t.states(); ++lambda) {
            const int m = at1(t.ielmst, lambda);
            ASSERT_GE(m, 1);
            ASSERT_LE(m, 16);
            const int own = at1(kElementStateLabel, m).second;
            ASSERT_EQ(own, at1(config.ilam1, m));
            for (int k = 1; k <= 6; ++k) {
                const int next = memfcn(t, lambda, k);
                if (k == own || next == 0) continue;
                EXPECT_GT(at1(at1(config.elemtr, m), k), 0.0) << lambda << " " << k;
                EXPECT_EQ(at1(kElementStateLabel, at1(t.ielmst, next)), std::make_pair(own, k)) << lambda << " " << k;
            }
        }
    }
}

// Test 6, zeros: where ELEMTR(IELMST(λ), k) > 0 (k not λ's element), MEMFCN(λ, k) = 0 exactly where the trie has no
// such continuation; where ELEMTR = 0, MEMFCN = 0. The proposed alphabet has 47 transitions that ELEMTR allows and
// the tree does not (sheet (f)).
TEST(BellTree, Test6Zeros) {
    const BellConfig config;
    for (TreeAlphabet a : kAlphabets) {
        const MorseTree t = bell_tree(a);
        const Trie trie = trie_of(a);
        int forbidden = 0;
        for (int lambda = 1; lambda <= t.states(); ++lambda) {
            const int m = at1(t.ielmst, lambda);
            const int own = at1(config.ilam1, m);
            const std::string v = at1(t.node, lambda);
            ASSERT_TRUE(trie.has(v)) << v;
            for (int k = 1; k <= 6; ++k) {
                if (k == own) continue;
                const int next = memfcn(t, lambda, k);
                if (at1(at1(config.elemtr, m), k) == 0.0) {
                    EXPECT_EQ(next, 0) << lambda << " " << k;
                    continue;
                }
                bool exists = false;
                if (own <= 2) {  // a mark of v
                    exists = k == kz4ap::bell::kElementSpace ? trie.has_child(v) : trie.symbol(v);
                } else if (own == kz4ap::bell::kElementSpace) {  // E(v)
                    exists = trie.has(v + (k == 1 ? "." : "-"));
                } else {  // a letter-ending space: the next letter's first mark
                    exists = true;
                }
                EXPECT_EQ(next != 0, exists) << name_of(a) << " state " << lambda << " (" << v << ") element " << k;
                forbidden += next == 0 ? 1 : 0;
            }
        }
        if (a == TreeAlphabet::proposed) EXPECT_EQ(forbidden, 47);
    }
}

// Test 7, size and format: at most 300 states; every value fits I3; LTRMAP is 0 or a code of the alphabet.
TEST(BellTree, Test7SizeAndValues) {
    for (TreeAlphabet a : kAlphabets) {
        const MorseTree t = bell_tree(a);
        EXPECT_LE(t.states(), kz4ap::bell::kMorseFileRows);
        for (int lambda = 1; lambda <= t.states(); ++lambda) {
            const int c = at1(t.ltrmap, lambda);
            EXPECT_TRUE(c == 0 || !at1(t.morse, c).empty()) << lambda;
            for (int k = 1; k <= 6; ++k) {
                EXPECT_GE(memfcn(t, lambda, k), 0);
                EXPECT_LE(memfcn(t, lambda, k), 999);
            }
        }
    }
}

// LTRMAP of mark and e-sp states: the node's code if it is a symbol, else 0; of letter-ending states: the symbol's.
TEST(BellTree, LtrmapFollowsRuleN1) {
    const BellConfig config;
    const MorseTree t = bell_tree();
    const Trie trie = trie_of(TreeAlphabet::proposed);
    for (int lambda = 1; lambda <= t.states(); ++lambda) {
        const std::string v = at1(t.node, lambda);
        const int expected = trie.symbol(v) ? trie.code.at(v) : 0;
        EXPECT_EQ(at1(t.ltrmap, lambda), expected) << lambda;
        const int own = at1(config.ilam1, at1(t.ielmst, lambda));
        if (own >= kz4ap::bell::kCharacterSpace) EXPECT_NE(expected, 0) << "letter-ending state of a non-symbol";
    }
}

namespace {

// INITL's READ(20,30) with FORMAT(8I3) [p. 161 10500-10600]: eight fields of three columns; a blank field reads 0.
std::vector<std::array<int, 8>> read_as_initl(const std::string& text) {
    std::vector<std::array<int, 8>> rows;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        std::array<int, 8> row{};
        for (std::size_t f = 0; f < 8; ++f) {
            const std::string field = line.size() > 3 * f ? line.substr(3 * f, 3) : std::string();
            row[f] = field.find_first_not_of(' ') == std::string::npos ? 0 : std::stoi(field);
        }
        rows.push_back(row);
    }
    return rows;
}

}  // namespace

// write_morse_file writes exactly INITL's 300 rows of FORMAT(8I3), and reading it back as INITL does gives the tree.
TEST(BellTree, MorseFileReadsBackAsInitlReadsIt) {
    for (TreeAlphabet a : kAlphabets) {
        const MorseTree t = bell_tree(a);
        std::ostringstream out;
        kz4ap::bell::write_morse_file(t, out);
        const std::string text = out.str();
        EXPECT_EQ(text.find('\r'), std::string::npos);
        ASSERT_EQ(text.size(), 300u * 25u);  // 24 columns and '\n' per row
        for (std::size_t r = 0; r < 300; ++r) {
            EXPECT_EQ(text[r * 25 + 24], '\n') << r;
            for (std::size_t f = 0; f < 8; ++f)
                EXPECT_NE(text[r * 25 + 3 * f + 2], ' ') << "I3 right-justified, row " << r + 1;
        }
        const auto rows = read_as_initl(text);
        ASSERT_EQ(rows.size(), 300u);
        for (int lambda = 1; lambda <= 300; ++lambda) {
            const auto& row = at1(rows, lambda);
            if (lambda > t.states()) {
                EXPECT_EQ(row, (std::array<int, 8>{})) << lambda;
                continue;
            }
            EXPECT_EQ(row[0], at1(t.ltrmap, lambda)) << lambda;  // LTRMAP(I) = IARRAY(1)
            EXPECT_EQ(row[1], at1(t.ielmst, lambda)) << lambda;  // IELMST(I) = IARRAY(2)
            for (int k = 1; k <= 6; ++k) EXPECT_EQ(row[static_cast<std::size_t>(k + 1)], memfcn(t, lambda, k));
        }
    }
    // the first rows, literally: E(.) prints E (code 5); S(., c-sp) prints E; M(.., ) is I's mark (code 9)
    std::ostringstream out;
    kz4ap::bell::write_morse_file(bell_tree(), out);
    const std::string text = out.str();
    EXPECT_EQ(text.substr(0, 25), "  5  1  9 10  1  0  0  0\n");
    EXPECT_EQ(text.substr(25, 25), "  5  2 11 12  0  2  0  0\n");
    EXPECT_EQ(text.substr(8 * 25, 25).substr(0, 9), "  9  9  9");
}

TEST(BellTree, MorseFileRefusesWhatInitlCannotRead) {
    MorseTree t = bell_tree();
    MorseTree big = t;
    while (big.states() <= kz4ap::bell::kMorseFileRows) {
        big.ltrmap.push_back(0);
        big.ielmst.push_back(1);
        big.memfcn.push_back({});
    }
    std::ostringstream out;
    EXPECT_THROW(kz4ap::bell::write_morse_file(big, out), std::invalid_argument);
    MorseTree wide = t;
    at1(at1(wide.memfcn, 1), 1) = 1000;
    EXPECT_THROW(kz4ap::bell::write_morse_file(wide, out), std::invalid_argument);
    MorseTree negative = t;
    at1(negative.ltrmap, 1) = -1;
    EXPECT_THROW(kz4ap::bell::write_morse_file(negative, out), std::invalid_argument);
}
