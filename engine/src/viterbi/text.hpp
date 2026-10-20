// The stories' text (plan §2.6, item 4; design notes, topic 1, item 8): a chain of letters and word spaces shared
// between stories. Each entry links to the one before it, so stories with a common past share the entries of that
// past; a story holds its text as its last entry. An entry is freed when no story and no later entry holds it, and its
// slot is reused. Symbols are views of static strings: the Morse table's (morse_tree.hpp) and the word space " ".
#pragma once

#include "kz4ap/decoder.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace kz4ap::viterbi {

class SharedText {
public:
    // The text with no entry.
    static constexpr std::int32_t kEmpty = -1;

    // A new entry after `previous` (kEmpty for none): a symbol ("A", "<SK>", or " " for a word space) with its start
    // and end times, s. The caller holds the new entry once; the new entry holds `previous` once.
    std::int32_t append(std::int32_t previous, std::string_view symbol, double start_s, double end_s) {
        hold(previous);
        std::int32_t entry;
        if (!free_.empty()) {
            entry = free_.back();
            free_.pop_back();
        } else {
            entry = static_cast<std::int32_t>(entries_.size());
            entries_.emplace_back();
        }
        entries_[static_cast<std::size_t>(entry)] = Entry{symbol, start_s, end_s, previous, 1};
        ++used_;
        return entry;
    }

    // One more holder of an entry; nothing for kEmpty.
    void hold(std::int32_t entry) {
        if (entry != kEmpty) ++entries_[static_cast<std::size_t>(entry)].holders;
    }

    // One holder fewer. An entry that no one holds any longer is freed, and with it its hold on the entry before it,
    // down the chain as far as the entries fall free (a loop, not a recursion: a chain can be the whole text).
    void release(std::int32_t entry) {
        while (entry != kEmpty) {
            Entry& e = entries_[static_cast<std::size_t>(entry)];
            if (--e.holders > 0) return;
            const std::int32_t previous = e.previous;
            e = Entry{};
            free_.push_back(entry);
            --used_;
            entry = previous;
        }
    }

    // The text that ends at `entry`, first symbol first, each with probability 1 (plan §2.7, item 7).
    std::vector<DecodedSymbol> read(std::int32_t entry) const {
        std::vector<DecodedSymbol> text;
        for (; entry != kEmpty; entry = entries_[static_cast<std::size_t>(entry)].previous) {
            const Entry& e = entries_[static_cast<std::size_t>(entry)];
            text.push_back(DecodedSymbol{std::string(e.symbol), 1.0f, e.start_s, e.end_s});
        }
        std::reverse(text.begin(), text.end());
        return text;
    }

    // The entries in use.
    std::size_t size() const { return used_; }

private:
    struct Entry {
        std::string_view symbol;
        double start_s = 0.0;  // s
        double end_s = 0.0;    // s
        std::int32_t previous = kEmpty;
        std::int32_t holders = 0;  // stories and later entries that hold it
    };
    std::vector<Entry> entries_;
    std::vector<std::int32_t> free_;  // freed slots, reused last freed first
    std::size_t used_ = 0;
};

}  // namespace kz4ap::viterbi
