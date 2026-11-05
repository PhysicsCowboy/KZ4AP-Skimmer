// The stories' text (plan §2.6, item 4; design notes, topic 1, item 8): a chain of letters and word spaces shared
// between stories. Each entry links to the one before it, so stories with a common past share the entries of that
// past; a story holds its text as its last entry. An entry is freed when no story and no later entry holds it, and its
// slot is reused. Symbols are views of static strings: the Morse table's (morse_tree.hpp) and the word space " ".
#pragma once

#include "kz4ap/decoder.hpp"

#include <algorithm>
#include <cmath>
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
    // nats: an entry's margin when no rival has been seen (lower_margin).
    static constexpr float kNoRival = 1e30f;

    // A new entry after `previous` (kEmpty for none): a symbol ("A", "<SK>", or " " for a word space) with its start
    // and end times, s. The caller holds the new entry once; the new entry holds `previous` once.
    std::int32_t append(std::int32_t previous, std::string_view symbol, double start_s, double end_s) {
        hold(previous);
        const std::int32_t length = previous == kEmpty ? 1 : entries_[static_cast<std::size_t>(previous)].length + 1;
        std::int32_t entry;
        if (!free_.empty()) {
            entry = free_.back();
            free_.pop_back();
        } else {
            entry = static_cast<std::int32_t>(entries_.size());
            entries_.emplace_back();
        }
        entries_[static_cast<std::size_t>(entry)] = Entry{symbol, start_s, end_s, previous, 1, length, kNoRival};
        ++used_;
        return entry;
    }

    // A rival story, this many nats below one that holds `entry`, read another symbol there (the segmental search's
    // merges, search.cpp): the entry's margin is the least such deficit seen. nats >= 0.
    void lower_margin(std::int32_t entry, float nats) {
        if (entry == kEmpty) return;
        float& m = entries_[static_cast<std::size_t>(entry)].margin;
        if (nats < m) m = nats;
    }
    const std::string_view& symbol(std::int32_t entry) const { return entries_[static_cast<std::size_t>(entry)].symbol; }
    std::int32_t previous(std::int32_t entry) const { return entries_[static_cast<std::size_t>(entry)].previous; }
    double start_s(std::int32_t entry) const { return entries_[static_cast<std::size_t>(entry)].start_s; }
    double end_s(std::int32_t entry) const { return entries_[static_cast<std::size_t>(entry)].end_s; }

    // nats: what an entry no rival came near reads as, the keeper's ceiling on the deficits it records (the segmental search's
    // rival_margin_nats); none set, such an entry reads probability 1, which says nothing (the viterbi decoder keeps no margins).
    void set_no_rival_margin(float nats) { no_rival_ = nats; }

    // The probability a symbol is published with: 1 / (1 + e^-margin), the posterior of the story against its closest rival
    // there, two hypotheses only.
    float probability_of(float margin_nats) const {
        const float m = margin_nats >= kNoRival ? no_rival_ : margin_nats;
        return m >= kNoRival ? 1.0f : static_cast<float>(1.0 / (1.0 + std::exp(-static_cast<double>(m))));
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

    // The text that ends at `entry`, first symbol first, each with probability_of its margin (plan §2.7, item 7: 1 without one).
    std::vector<DecodedSymbol> read(std::int32_t entry) const {
        std::vector<DecodedSymbol> text;
        for (; entry != kEmpty; entry = entries_[static_cast<std::size_t>(entry)].previous) {
            const Entry& e = entries_[static_cast<std::size_t>(entry)];
            text.push_back(DecodedSymbol{std::string(e.symbol), probability_of(e.margin), e.start_s, e.end_s});
        }
        std::reverse(text.begin(), text.end());
        return text;
    }

    // The last max_symbols symbols of that text, first symbol first, without walking the rest of it.
    std::vector<DecodedSymbol> read_tail(std::int32_t entry, std::size_t max_symbols) const {
        std::vector<DecodedSymbol> text;
        for (; entry != kEmpty && text.size() < max_symbols; entry = entries_[static_cast<std::size_t>(entry)].previous) {
            const Entry& e = entries_[static_cast<std::size_t>(entry)];
            text.push_back(DecodedSymbol{std::string(e.symbol), probability_of(e.margin), e.start_s, e.end_s});
        }
        std::reverse(text.begin(), text.end());
        return text;
    }

    // The length of the text that ends at `entry`, symbols.
    std::size_t length(std::int32_t entry) const {
        return entry == kEmpty ? 0 : static_cast<std::size_t>(entries_[static_cast<std::size_t>(entry)].length);
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
        std::int32_t length = 0;   // symbols in the text that ends here
        float margin = kNoRival;   // nats: lower_margin
    };
    std::vector<Entry> entries_;
    std::vector<std::int32_t> free_;  // freed slots, reused last freed first
    std::size_t used_ = 0;
    float no_rival_ = kNoRival;  // nats: set_no_rival_margin
};

}  // namespace kz4ap::viterbi
