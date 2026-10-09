// Bell's letter tree by rule N1 (reference sheet (f)); see tree.hpp.
#include "kz4ap/bell/tree.hpp"

#include "kz4ap/bell/config.hpp"

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <map>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

namespace kz4ap::bell {
namespace {

// IALPH(1..70) as listed, "" for the listing's 0 [p. 161 07800-08300].
const std::array<const char*, kAlphabetCodes> kIalph = {
    "A",   "B",  "C",  "D",  "E",  "F",  "G",  "H",  "I",  "J",  // 1-10
    "K",   "L",  "M",  "N",  "O",  "P",  "Q",  "R",  "S",  "T",  // 11-20
    "U",   "V",  "W",  "X",  "Y",  "Z",  "1",  "2",  "3",  "4",  // 21-30
    "5",   "6",  "7",  "8",  "9",  "0",  ";",  ":",  "%",  "&",  // 31-40
    "",    "",   "X",  ".",  "AS", "SN", "",   "",   "",   "",   // 41-50
    "NR",  "NO", "GA", "OK", "AR", "SK", "",   "",   "",   "",   // 51-60
    "IMI", "",   "",   "",   "",   "BT", "",   "",   "",   "EEE",  // 61-70
};

// The Morse code of each IALPH code the alphabets use (reference sheet (f), "His alphabet"): A-Z and 1-9, 0 in
// International Morse, as KEY's MORSE table lists them in IALPH order (pp. 163-164 05200-07000, whose column 3 holds
// B's code for C: a defect of the simulator, not of the alphabet); X under code 43, not 24; the rest our choice.
struct Symbol {
    int code;
    const char* morse;
};
const Symbol kLettersDigits[] = {
    {1, ".-"},     {2, "-..."},   {3, "-.-."},   {4, "-.."},    {5, "."},      {6, "..-."},   {7, "--."},
    {8, "...."},   {9, ".."},     {10, ".---"},  {11, "-.-"},   {12, ".-.."},  {13, "--"},    {14, "-."},
    {15, "---"},   {16, ".--."},  {17, "--.-"},  {18, ".-."},   {19, "..."},   {20, "-"},     {21, "..-"},
    {22, "...-"},  {23, ".--"},   {43, "-..-"},  {25, "-.--"},  {26, "--.."},  {27, ".----"}, {28, "..---"},
    {29, "...--"}, {30, "....-"}, {31, "....."}, {32, "-...."}, {33, "--..."}, {34, "---.."}, {35, "----."},
    {36, "-----"},
};
const Symbol kPeriodAndProsigns[] = {
    {44, ".-.-.-"},  // .
    {45, ".-..."},   // AS
    {46, "...-."},   // SN
    {51, "-..-."},   // NR
    {52, "-.---"},   // NO
    {53, "--..-"},   // GA
    {54, "----.-"},  // OK
    {55, ".-.-."},   // AR
    {56, "...-.-"},  // SK
    {61, "..--.."},  // IMI
    {66, "-...-"},   // BT
};
const Symbol kSemicolonColon[] = {
    {37, "-.-.-."},  // ;
    {38, "---..."},  // :
};

std::vector<Symbol> symbols_of(TreeAlphabet alphabet) {
    std::vector<Symbol> out(std::begin(kLettersDigits), std::end(kLettersDigits));
    if (alphabet != TreeAlphabet::letters_digits)
        out.insert(out.end(), std::begin(kPeriodAndProsigns), std::end(kPeriodAndProsigns));
    if (alphabet == TreeAlphabet::with_semicolon_colon)
        out.insert(out.end(), std::begin(kSemicolonColon), std::end(kSemicolonColon));
    return out;
}

// Breadth-first order of trie nodes: shorter first, then dot before dash, element by element.
bool node_before(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return a.size() < b.size();
    for (std::size_t i = 0; i < a.size(); ++i)
        if (a[i] != b[i]) return a[i] == '.';
    return false;
}

int element_of(char c) { return c == '.' ? kDot : kDash; }

// The kinds of state of rule N1.
enum Kind { kMark = 0, kSpaceAfter = 1, kLetterEnd = 2 };
// A state's key: (kind, node, letter-ending space s for M(v, s) and S(v, s), else 0).
using StateKey = std::tuple<int, std::string, int>;

// IELMST of each state (rule N1's bracketed values).
int element_state(const StateKey& key) {
    const auto& [kind, v, s] = key;
    const bool dash = v.back() == '-';
    switch (kind) {
    case kMark:
        if (s == 0) return dash ? 10 : 9;                       // M(v): after an e-sp
        return (dash ? 12 : 11) + 2 * (s - kCharacterSpace);  // M(v, s): 11/12, 13/14, 15/16
    case kSpaceAfter:
        return dash ? 5 : 1;                                   // E(v)
    default:
        return (dash ? 6 : 2) + (s - kCharacterSpace);         // S(v, s): 2/6, 3/7, 4/8
    }
}

}  // namespace

MorseTree bell_tree(TreeAlphabet alphabet) {
    const std::vector<Symbol> symbols = symbols_of(alphabet);

    // Trie nodes and the symbol (IALPH code) of each.
    std::map<std::string, int> code_of;  // node -> code, 0 if not a symbol
    for (const Symbol& sym : symbols) {
        const std::string w = sym.morse;
        for (std::size_t len = 1; len <= w.size(); ++len) code_of.emplace(w.substr(0, len), 0);
    }
    for (const Symbol& sym : symbols) {
        int& c = code_of.at(sym.morse);
        if (c != 0) throw std::logic_error("bell_tree: two symbols with the code " + std::string(sym.morse));
        c = sym.code;
    }
    std::vector<std::string> nodes;
    for (const auto& [v, c] : code_of) nodes.push_back(v);
    std::sort(nodes.begin(), nodes.end(), node_before);
    const auto exists = [&](const std::string& v) { return code_of.count(v) != 0; };
    const auto is_symbol = [&](const std::string& v) { return exists(v) && code_of.at(v) != 0; };
    const auto has_child = [&](const std::string& v) { return exists(v + ".") || exists(v + "-"); };
    if (!is_symbol(".") || !is_symbol("-") || !has_child("-") || !exists("..") || !exists(".-"))
        throw std::logic_error("bell_tree: rule N1 numbers E, T and the nodes .. and .- first; they must exist");

    // Numbering: states 1-16 as INITL's DATA (IELMST(λ) = λ), then by node and kind.
    std::vector<StateKey> keys;
    std::map<StateKey, int> lambda_of;
    const auto add = [&](const StateKey& key) {
        if (lambda_of.count(key) != 0) return;
        keys.push_back(key);
        lambda_of.emplace(key, static_cast<int>(keys.size()));
    };
    add({kSpaceAfter, ".", 0});
    for (int s = kCharacterSpace; s <= kPause; ++s) add({kLetterEnd, ".", s});
    add({kSpaceAfter, "-", 0});
    for (int s = kCharacterSpace; s <= kPause; ++s) add({kLetterEnd, "-", s});
    add({kMark, "..", 0});
    add({kMark, ".-", 0});
    for (int s = kCharacterSpace; s <= kPause; ++s) {
        add({kMark, ".", s});
        add({kMark, "-", s});
    }
    for (const std::string& v : nodes) {
        if (v.size() == 1)
            for (int s = kCharacterSpace; s <= kPause; ++s) add({kMark, v, s});
        else
            add({kMark, v, 0});
        if (has_child(v)) add({kSpaceAfter, v, 0});
        if (is_symbol(v))
            for (int s = kCharacterSpace; s <= kPause; ++s) add({kLetterEnd, v, s});
    }

    MorseTree tree;
    tree.alphabet.assign(kIalph.begin(), kIalph.end());
    tree.morse.assign(kAlphabetCodes, "");
    for (const Symbol& sym : symbols) at1(tree.morse, sym.code) = sym.morse;

    const auto lambda = [&](const StateKey& key) { return lambda_of.count(key) != 0 ? lambda_of.at(key) : 0; };
    for (const StateKey& key : keys) {
        const auto& [kind, v, s] = key;
        std::array<int, 6> next{};
        const int self = lambda_of.at(key);
        int ltr = 0;
        switch (kind) {
        case kMark:
            at1(next, element_of(v.back())) = self;
            at1(next, kElementSpace) = lambda({kSpaceAfter, v, 0});
            for (int t = kCharacterSpace; t <= kPause; ++t) at1(next, t) = lambda({kLetterEnd, v, t});
            ltr = code_of.at(v);
            break;
        case kSpaceAfter:
            at1(next, kElementSpace) = self;
            at1(next, kDot) = lambda({kMark, v + ".", 0});
            at1(next, kDash) = lambda({kMark, v + "-", 0});
            ltr = code_of.at(v);
            break;
        default:
            at1(next, s) = self;
            at1(next, kDot) = lambda({kMark, ".", s});
            at1(next, kDash) = lambda({kMark, "-", s});
            ltr = code_of.at(v);
            break;
        }
        tree.ltrmap.push_back(ltr);
        tree.ielmst.push_back(element_state(key));
        tree.memfcn.push_back(next);
        tree.node.push_back(v);
    }
    return tree;
}

std::vector<int> iblank(const MorseTree& tree, const BellConfig& config) {
    std::vector<int> out(tree.ielmst.size(), 0);
    for (std::size_t i = 0; i < out.size(); ++i) {
        const int m = tree.ielmst[i];
        if (m == 7 || m == 3) out[i] = config.iblank_word_space;
        if (m == 8 || m == 4) out[i] = config.iblank_pause;
    }
    return out;
}

void write_morse_file(const MorseTree& tree, std::ostream& out) {
    if (tree.states() > kMorseFileRows)
        throw std::invalid_argument("write_morse_file: " + std::to_string(tree.states()) + " states, INITL reads " +
                                    std::to_string(kMorseFileRows) + " rows");
    const auto field = [](int value) {
        if (value < 0 || value > 999)
            throw std::invalid_argument("write_morse_file: " + std::to_string(value) + " does not fit I3");
        std::string text = std::to_string(value);
        return std::string(3 - text.size(), ' ') + text;
    };
    for (int lambda = 1; lambda <= kMorseFileRows; ++lambda) {
        std::array<int, 8> row{};
        if (lambda <= tree.states()) {
            row[0] = at1(tree.ltrmap, lambda);
            row[1] = at1(tree.ielmst, lambda);
            for (int k = 1; k <= kElements; ++k) at1(row, k + 2) = at1(at1(tree.memfcn, lambda), k);
        }
        for (int value : row) out << field(value);
        out << '\n';
    }
}

}  // namespace kz4ap::bell
