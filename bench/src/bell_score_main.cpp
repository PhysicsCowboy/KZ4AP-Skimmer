// kz4ap-bell-score: scores decoded texts against their references with the project's scorer (scoring.hpp):
// A.11's character and space edits and the Bell-style count (Bell stage-1 plan, decision D10).
//
//   kz4ap-bell-score < texts.tsv > scores.csv
//
// Input: one line per decoded text, tab-separated: a key, the reference text, the decoded text; any further fields
// are ignored. reference/bell1977/v2.py ("texts") writes such lines from the output of Bell's compiled program.
// Output: CSV, one row per input line: key, chars (reference symbols that are not word spaces), bell_count,
// char_edits, spaces (reference word spaces), space_edits. The key is copied as is and must hold no comma.
#include "scoring.hpp"

#include <cstddef>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

std::vector<std::string> split_tabs(const std::string& line) {
    std::vector<std::string> fields;
    std::size_t start = 0;
    while (true) {
        const std::size_t tab = line.find('\t', start);
        fields.push_back(line.substr(start, tab == std::string::npos ? std::string::npos : tab - start));
        if (tab == std::string::npos) return fields;
        start = tab + 1;
    }
}

int run() {
    std::cout << "key,chars,bell_count,char_edits,spaces,space_edits\n";
    std::string line;
    std::size_t number = 0;
    while (std::getline(std::cin, line)) {
        ++number;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        const auto fields = split_tabs(line);
        if (fields.size() < 3) throw std::runtime_error("line " + std::to_string(number) + ": fewer than 3 fields");
        if (fields[0].find(',') != std::string::npos)
            throw std::runtime_error("line " + std::to_string(number) + ": the key holds a comma");
        const auto s = kz4ap::bench::score_text(fields[1], fields[2]);
        std::cout << fields[0] << ',' << s.chars << ',' << s.bell_count << ',' << s.char_edits << ',' << s.spaces
                  << ',' << s.space_edits << '\n';
    }
    return 0;
}

}  // namespace

int main() {
    try {
        return run();
    } catch (const std::exception& e) {
        std::cerr << "kz4ap-bell-score: " << e.what() << "\n";
        return 2;
    }
}
