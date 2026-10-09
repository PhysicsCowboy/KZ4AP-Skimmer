// Bell's letter tree: the tables INITL reads from the file MORSEM (LTRMAP, IELMST, MEMFCN; p. 161 10300-11500) and
// his alphabet IALPH (p. 161 07800-08300). The file is not printed in the thesis; this is its reconstruction by rule
// N1 of the reference sheet (docs/research/bell-1977-reference.md, section (f); our choice, its numbering ours, its
// meaning his), with the sheet's proposed alphabet of 47 symbols and its two variants for diagnosis.
//
// A state λ = 1, 2, ... is a node v of the Morse code trie (a non-empty prefix of a symbol's code) together with the
// element being keyed:
//   M(v)     the mark of v after an element space, |v| >= 2           IELMST 9 (v ends in a dot) or 10 (a dash)
//   M(v, s)  the mark of a one-element node after the letter-ending space s = c-sp, w-sp, pause
//                                                                     IELMST 11/12, 13/14, 15/16
//   E(v)     the element space after v, for every node with a child   IELMST 1 or 5
//   S(v, s)  the letter-ending space s after a symbol v               IELMST 2/6 (c-sp), 3/7 (w-sp), 4/8 (pause)
// MEMFCN(λ, k) is the state reached when element k is keyed (λ itself for its own element, 0 where the trie has no
// such continuation); LTRMAP(λ) is the IALPH code printed for a letter-ending state. States 1-16 are numbered so that
// IELMST(λ) = λ, as in INITL's DATA; the rest follow the nodes in breadth-first order (shorter first, dot before
// dash), each node's states in the order M, E, S(c-sp), S(w-sp), S(pause).
#pragma once

#include "kz4ap/bell/config.hpp"

#include <array>
#include <ostream>
#include <string>
#include <vector>

namespace kz4ap::bell {

// Rows INITL reads from MORSEM, FORMAT(8I3) each: LTRMAP, IELMST, MEMFCN(1..6) [p. 161 10400-10600]; its arrays are
// dimensioned 400 [p. 160 05000-05300], rows 301-400 keep their DATA value 0.
inline constexpr int kMorseFileRows = 300;
// IALPH(70) [p. 160 05200]
inline constexpr int kAlphabetCodes = 70;

// The alphabets of reference sheet (f) (our choice). The proposed one takes every IALPH entry with a known or
// standard Morse code, without ; and : (in KEY's text format 37 and 38 mean w-sp and pause), % and EEE (code
// unknown) and & (the code of AS). The letter X's node takes code 43 (TRANSL's full stop X) in all three.
enum class TreeAlphabet {
    proposed,              // 47 symbols: A-Z (X as 43), 1-9, 0, the period, AS, SN, NR, NO, GA, OK, AR, SK, IMI, BT
    letters_digits,        // 36 symbols: A-Z (X as 43), 1-9, 0
    with_semicolon_colon,  // 49 symbols: the proposed 47 and ; (code 37) and : (code 38)
};

struct MorseTree {
    // One entry per state λ = 1 ... size(); Bell's 1-based codes are stored as values (index with at1()). Rows after
    // the last state, up to INITL's 300 and its arrays' 400, are 0 and are not stored.
    std::vector<int> ltrmap;                  // LTRMAP(λ): IALPH code, 0 if λ prints nothing
    std::vector<int> ielmst;                  // IELMST(λ): element-state 1-16
    std::vector<std::array<int, 6>> memfcn;   // MEMFCN(λ, k), k = dot ... pause: next state, 0 if none
    // IALPH(c), c = 1 ... 70: the printed text of code c, "" where the listing has 0 [p. 161 07800-08300]
    std::vector<std::string> alphabet;
    // Ours, not Bell's (the decoder never reads them): the Morse code of IALPH code c in this tree, '.' and '-', ""
    // if c is not in the tree (c = 1 ... 70); and each state's trie node (λ = 1 ... size()).
    std::vector<std::string> morse;
    std::vector<std::string> node;

    int states() const { return static_cast<int>(ielmst.size()); }
};

// The tree by rule N1 over the chosen alphabet.
MorseTree bell_tree(TreeAlphabet alphabet = TreeAlphabet::proposed);

// IBLANK(λ), λ = 1 ... states(), as INITL derives it from IELMST: 1 for IELMST 3 or 7 (w-sp states), 2 for 4 or 8
// (pause states), else 0 [p. 161 11100-11400].
std::vector<int> iblank(const MorseTree& tree, const BellConfig& config);

// Writes MORSEM as INITL reads it: exactly kMorseFileRows lines of FORMAT(8I3) (eight right-justified 3-column
// integers, 24 characters, then '\n'): LTRMAP, IELMST, MEMFCN(1..6) of state 1, 2, ..., then rows of zeros. Throws
// std::invalid_argument if the tree has more than kMorseFileRows states or a value outside 0 ... 999 (I3).
void write_morse_file(const MorseTree& tree, std::ostream& out);

}  // namespace kz4ap::bell
