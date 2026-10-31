#include "kz4ap/track_evidence.hpp"

#include "kz4ap/viterbi/symbol_prior.hpp"

#include <algorithm>
#include <cmath>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace kz4ap {

namespace {

// The prior's floor, as config.hpp's symbol_prior_floor; 62 the tree's symbols.
constexpr double kPriorFloor = 0.002;
constexpr double kUniform = 1.0 / 62.0;

// The tiny pieces' shares: measured on a real recording's junk (speech and wide signals on 40 m, 4 968 symbols: E 0.635, I 0.133,
// T 0.072, S 0.046, N 0.024, A 0.014), T raised for the tones and carriers that read T and the rest kept above the measured
// tail. The remainder, 0.03, goes to the other symbols in proportion to the prior.
constexpr std::pair<std::string_view, double> kTinyShares[] = {{"E", 0.55}, {"T", 0.20}, {"I", 0.11}, {"S", 0.045}, {"N", 0.025}, {"A", 0.02}, {"H", 0.01}, {"U", 0.005}, {"R", 0.005}};
constexpr double kTinyRest = 0.03;

struct Ratios {
    double tiny;     // ln(q / p), nats
    double garbage;  // ln(1/62 / p), nats
};

struct Tables {
    std::unordered_map<std::string_view, Ratios> by_symbol;
    Ratios unknown{};

    Tables() {
        double total = 0.0;
        for (const viterbi::SymbolCount& s : viterbi::on_air_symbol_counts()) total += s.count;
        unknown.tiny = std::log(kTinyRest);
        unknown.garbage = std::log(kUniform / kPriorFloor);
        for (const viterbi::SymbolCount& s : viterbi::on_air_symbol_counts()) {
            const double p = std::max(s.count / total, kPriorFloor);
            double q = kTinyRest * p;
            for (const auto& [symbol, share] : kTinyShares)
                if (symbol == s.symbol) q = share;
            by_symbol.emplace(s.symbol, Ratios{std::log(q / p), std::log(kUniform / p)});
        }
    }
};

const Tables& tables() {
    static const Tables t;
    return t;
}

}  // namespace

bool TextEvidence::plausible() const {
    return lowest_tiny_nats <= -kPlausibleNats && highest_garbage_nats < kGarbageJunkNats;
}

namespace {

void add_symbol(TextEvidence& out, const std::string& symbol, const Tables& t) {
    if (symbol == " ") return;
    const auto it = t.by_symbol.find(symbol);
    const Ratios& r = it != t.by_symbol.end() ? it->second : t.unknown;
    out.tiny_nats = std::max(-kTextFloorNats, out.tiny_nats + r.tiny);
    out.garbage_nats = std::max(-kTextFloorNats, out.garbage_nats + r.garbage);
    out.lowest_tiny_nats = std::min(out.lowest_tiny_nats, out.tiny_nats);
    out.highest_garbage_nats = std::max(out.highest_garbage_nats, out.garbage_nats);
    ++out.symbols;
}

}  // namespace

TextEvidence text_evidence(std::span<const std::string> symbols) {
    const Tables& t = tables();
    TextEvidence out;
    for (const std::string& symbol : symbols) add_symbol(out, symbol, t);
    return out;
}

void RunningTextEvidence::back_to(std::size_t index) {
    if (index >= read_) return;
    // A state saved after k x kCheckpointEvery symbols holds only symbols before index if k x kCheckpointEvery <= index.
    const std::size_t k = index / kCheckpointEvery;
    sum_ = k == 0 ? TextEvidence{} : checkpoints_[k - 1];
    read_ = k * kCheckpointEvery;
    checkpoints_.resize(k);
}

void RunningTextEvidence::changed_from(std::size_t index) { back_to(index); }

const TextEvidence& RunningTextEvidence::update(std::span<const std::string> symbols) {
    back_to(symbols.size());
    const Tables& t = tables();
    for (; read_ < symbols.size(); ++read_) {
        add_symbol(sum_, symbols[read_], t);
        if ((read_ + 1) % kCheckpointEvery == 0) checkpoints_.push_back(sum_);
    }
    return sum_;
}

TrackJudge::Verdict TrackJudge::update(const KeyingEvidence& evidence, std::span<const std::string> symbols) {
    history_[count_ % kHistory] = evidence;
    ++count_;
    const TextEvidence& text = text_.update(symbols);
    proven_ = proven_ || text.plausible();
    const double age_s = static_cast<double>(count_ - 1);  // a second between entries
    const auto ago = [&](double seconds) -> const KeyingEvidence& {
        const std::size_t back = std::min(count_ - 1, static_cast<std::size_t>(seconds));
        return history_[(count_ - 1 - back) % kHistory];
    };
    const auto retire = [](TrackEnd end, double hold_s) {
        Verdict v;
        v.retire = true;
        v.end = end;
        v.hold_s = hold_s;
        return v;
    };

    const double morse_window = evidence.morse_nats - ago(kWindowS).morse_nats;
    const double free_window = evidence.free_key_nats - ago(kWindowS).free_key_nats;
    const double window_s = std::min(age_s, kWindowS);
    const double rate = window_s > 0.0 ? (morse_window - std::max(0.0, free_window)) / window_s : 0.0;
    const double earning = window_s > 0.0 ? morse_window / window_s : 0.0;
    earning_ = earning;
    const auto held_to_it = [&](double shortfall, double earned) {
        return !proven_ || (earned >= kStrongRate && -shortfall > kShortfallShare * earned);
    };

    // The text reads junk, and the signal does not insist otherwise.
    if (age_s >= kFirstVerdictS && rate < kKeyedRate && (text.tiny_nats >= kTinyJunkNats || text.garbage_nats >= kGarbageJunkNats))
        return retire(TrackEnd::NotMorse, kHoldS);
    // A tone, FT8, a carrier: timing earns nothing, a whole window long.
    if (age_s >= kWindowS && rate < kNotKeyedRate && held_to_it(rate, earning)) return retire(TrackEnd::NotMorse, kHoldS);
    if (!proven_ && rate < kKeyedRate) {
        if (age_s >= kQuietNewS && evidence.morse_nats - ago(kQuietNewS).morse_nats < kQuietNewNatsPerS * kQuietNewS)
            return retire(TrackEnd::Quiet, kHoldS);
        if (age_s >= kUnprovenS) return retire(TrackEnd::NotMorse, kHoldS);
    } else if (proven_ && age_s >= kQuietOldS && evidence.morse_nats - ago(kQuietOldS).morse_nats < kQuietOldNatsPerS * kQuietOldS) {
        return retire(TrackEnd::Quiet, 0.0);
    }
    return {};
}

}  // namespace kz4ap
