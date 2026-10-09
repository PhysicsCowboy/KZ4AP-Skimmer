// kz4ap-bell-tables: Bell's letter tree, reconstructed by rule N1 of the reference sheet
// (docs/research/bell-1977-reference.md, section (f); engine/include/kz4ap/bell/tree.hpp).
//
//   kz4ap-bell-tables [--alphabet proposed|letters-digits|with-semicolon-colon] morsem FILE
//       writes the file MORSEM that Bell's INITL reads (p. 161 10300-11500): 300 rows of FORMAT(8I3), LTRMAP,
//       IELMST, MEMFCN(1..6), with '\n' line ends; the compiled reference (plan Task 3) reads it.
//   kz4ap-bell-tables [--alphabet ...] list
//       one line per state on stdout: λ, its trie node, IELMST, LTRMAP and its IALPH text, MEMFCN(dot ... pause).
//
// The default alphabet is the sheet's proposed one (47 symbols); the other two are its variants for diagnosis.
#include "kz4ap/bell/tree.hpp"

#include <cstddef>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

[[noreturn]] void usage(const std::string& why) {
    throw std::invalid_argument(why +
                                "\nusage: kz4ap-bell-tables [--alphabet proposed|letters-digits|with-semicolon-colon] "
                                "morsem FILE\n       kz4ap-bell-tables [--alphabet ...] list");
}

kz4ap::bell::TreeAlphabet parse_alphabet(const std::string& name) {
    if (name == "proposed") return kz4ap::bell::TreeAlphabet::proposed;
    if (name == "letters-digits") return kz4ap::bell::TreeAlphabet::letters_digits;
    if (name == "with-semicolon-colon") return kz4ap::bell::TreeAlphabet::with_semicolon_colon;
    usage("unknown alphabet: " + name);
}

void list(const kz4ap::bell::MorseTree& tree, std::ostream& out) {
    out << "lambda node      IELMST LTRMAP text  MEMFCN(dot dash e-sp c-sp w-sp pause)\n";
    for (int lambda = 1; lambda <= tree.states(); ++lambda) {
        const int code = kz4ap::bell::at1(tree.ltrmap, lambda);
        out << std::setw(6) << lambda << ' ' << std::left << std::setw(9) << kz4ap::bell::at1(tree.node, lambda)
            << std::right << std::setw(7) << kz4ap::bell::at1(tree.ielmst, lambda) << std::setw(7) << code << ' '
            << std::left << std::setw(5) << (code == 0 ? std::string("-") : kz4ap::bell::at1(tree.alphabet, code))
            << std::right;
        for (int next : kz4ap::bell::at1(tree.memfcn, lambda)) out << std::setw(5) << next;
        out << '\n';
    }
}

int run(int argc, char** argv) {
    std::vector<std::string> args(argv + 1, argv + argc);
    auto alphabet = kz4ap::bell::TreeAlphabet::proposed;
    std::size_t i = 0;
    if (i < args.size() && args[i] == "--alphabet") {
        if (i + 1 >= args.size()) usage("--alphabet needs a value");
        alphabet = parse_alphabet(args[i + 1]);
        i += 2;
    }
    if (i >= args.size()) usage("no command");
    const std::string command = args[i++];
    const kz4ap::bell::MorseTree tree = kz4ap::bell::bell_tree(alphabet);
    if (command == "morsem") {
        if (i + 1 != args.size()) usage("morsem needs one FILE");
        std::ofstream out(args[i], std::ios::binary);  // '\n' line ends on every platform
        if (!out) throw std::runtime_error("cannot write " + args[i]);
        kz4ap::bell::write_morse_file(tree, out);
        if (!out) throw std::runtime_error("write failed: " + args[i]);
        return 0;
    }
    if (command == "list") {
        if (i != args.size()) usage("list takes no argument");
        list(tree, std::cout);
        return 0;
    }
    usage("unknown command: " + command);
}

}  // namespace

int main(int argc, char** argv) {
    try {
        return run(argc, argv);
    } catch (const std::exception& e) {
        std::cerr << "kz4ap-bell-tables: " << e.what() << "\n";
        return 2;
    }
}
