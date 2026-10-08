#include "kz4ap/bank/selection.hpp"

#include "kz4ap/bank/python_sum.hpp"
#include "kz4ap/morse.hpp"
#include "ve3nea.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string_view>
#include <utility>
#include <vector>

namespace kz4ap::bank {

namespace {

// "symbol in CODES" of kz4ap_synth.morse, case-sensitive: kz4ap::morse holds the same table (a Python test
// compares them) but encodes case-insensitively, so the symbol must also be what its pattern decodes to
// (the table's patterns are distinct).
bool is_code(const std::string& symbol) {
    if (symbol.empty()) return false;
    const std::string_view pattern = morse::encode(symbol);
    return !pattern.empty() && morse::decode(pattern) == symbol;
}

}  // namespace

std::string decode_pattern(const std::string& pattern) {
    if (pattern.size() >= 8 && pattern.find_first_not_of('.') == std::string::npos) return "<HH>";
    const std::string_view symbol = morse::decode(pattern);
    return symbol.empty() ? std::string("*") : std::string(symbol);
}

double invalid_logprob() { return std::log(1e-6); }

TextModel::TextModel() {
    int total = 0;
    int rarest = 0;
    bool first = true;
    for (const auto& cw : detail::kVe3neaCharWeights) {
        total += cw.weight;
        if (first || cw.weight < rarest) rarest = cw.weight;
        first = false;
    }
    for (const auto& cw : detail::kVe3neaCharWeights)
        logp_[std::string(cw.symbol)] = std::log(static_cast<double>(cw.weight) / static_cast<double>(total));
    floor_ = std::log(static_cast<double>(rarest) / static_cast<double>(total));
}

double TextModel::char_logprob(const std::string& symbol) const {
    if (const auto it = logp_.find(symbol); it != logp_.end()) return it->second;
    return is_code(symbol) ? floor_ : invalid_logprob();
}

std::optional<double> TextModel::mean_logprob(const std::vector<std::string>& symbols) const {
    std::vector<double> values;
    for (const auto& s : symbols)
        if (s != " ") values.push_back(char_logprob(s));
    if (values.empty()) return std::nullopt;
    return python_sum(values) / static_cast<double>(values.size());
}

Selector::Selector(const BankConfig& cfg, std::vector<double> lengths_s)
    : cfg_(cfg), lengths_(std::move(lengths_s)), eligible_since_(lengths_.size()) {}

bool Selector::eligible(const BranchView& view) const {
    if (!view.fit) return false;
    const Fit& f = *view.fit;
    return f.weight >= cfg_.min_fit_weight && f.t_s > 0.0 &&
           std::abs(std::log(view.length_s / (cfg_.length_dits * f.t_s))) <= cfg_.eligibility_tolerance;
}

std::pair<int, bool> Selector::best(const std::vector<BranchView>& views, std::optional<double> prior_t_s) const {
    std::vector<const BranchView*> elig;
    for (const auto& v : views)
        if (eligible(v)) elig.push_back(&v);
    if (!elig.empty()) {
        double q_best = elig.front()->fit->quality;
        for (const auto* v : elig) q_best = std::max(q_best, v->fit->quality);
        std::vector<const BranchView*> tied;
        for (const auto* v : elig)
            if (v->fit->quality >= q_best - cfg_.quality_tie_nats) tied.push_back(v);
        // The text step runs only when every tied branch has decoded text: a branch with none yet is not read
        // as infinitely unlikely text, so the tie then goes straight to the longer branch (heuristic).
        const bool all_text =
            std::all_of(tied.begin(), tied.end(), [](const BranchView* v) { return v->text_logprob.has_value(); });
        if (tied.size() > 1 && all_text) {
            double t_best = *tied.front()->text_logprob;
            for (const auto* v : tied) t_best = std::max(t_best, *v->text_logprob);
            std::vector<const BranchView*> kept;
            for (const auto* v : tied)
                if (*v->text_logprob >= t_best - cfg_.text_tie_nats) kept.push_back(v);
            tied = std::move(kept);
        }
        int longest = tied.front()->index;
        for (const auto* v : tied) longest = std::max(longest, v->index);
        return {longest, true};  // the longer branch (better SNR)
    }
    // sorted((text_logprob, index)): by text, then by index.
    std::vector<std::pair<double, int>> texts;
    for (const auto& v : views)
        if (v.text_logprob) texts.emplace_back(*v.text_logprob, v.index);
    std::sort(texts.begin(), texts.end());
    if (texts.size() >= 2 && texts[texts.size() - 1].first - texts[texts.size() - 2].first >= cfg_.text_separation_nats)
        return {texts.back().second, false};
    if (prior_t_s) {
        // np.argmin(|ln(L_k / (length_dits x T_P))|): the first minimum.
        const double target = cfg_.length_dits * *prior_t_s;
        std::size_t nearest = 0;
        double d_best = 0.0;
        for (std::size_t k = 0; k < lengths_.size(); ++k) {
            const double d = std::abs(std::log(lengths_[k] / target));
            if (std::isnan(d)) {
                nearest = k;
                break;
            }
            if (k == 0 || d < d_best) {
                nearest = k;
                d_best = d;
            }
        }
        return {static_cast<int>(nearest), false};
    }
    return {0, false};
}

int Selector::update(const std::vector<BranchView>& views, int instants, double t_now,
                     std::optional<double> prior_t_s) {
    if (instants <= 0) return current_;
    for (const auto& v : views) {
        auto& since = eligible_since_.at(static_cast<std::size_t>(v.index));
        if (eligible(v)) {
            if (!since) since = t_now;
        } else {
            since.reset();
        }
    }
    const std::pair<int, bool> b = best(views, prior_t_s);
    if (b.first == current_) {
        candidate_.reset();
        count_ = 0;
    } else if (candidate_ && *candidate_ == b) {
        count_ += instants;
    } else {
        candidate_ = b;
        count_ = instants;
    }
    if (candidate_ && count_ >= cfg_.switch_persistence) {
        current_ = candidate_->first;
        candidate_.reset();
        count_ = 0;
    }
    return current_;
}

}  // namespace kz4ap::bank
