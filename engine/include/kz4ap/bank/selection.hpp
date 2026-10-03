// The bank decoder's characters, text model and branch selection: a port of training/kz4ap_proto/text.py and
// select.py (docs/signal-processing.md section 8c, "Text model and branch selection").
#pragma once

#include "kz4ap/bank/bank_config.hpp"
#include "kz4ap/bank/fit.hpp"

#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace kz4ap::bank {

// The symbol for a dot/dash pattern: eight or more dits read "<HH>", a pattern with no code "*" (the
// prototype's decode_pattern, from the same code table as kz4ap::morse).
std::string decode_pattern(const std::string& pattern);

// ln(1e-6), nats: the log-probability of "*", an element sequence that is no Morse character (heuristic).
double invalid_logprob();

// Unigram log-probability, nats, under VE3NEA's CW character frequencies (kz4ap_synth.messages, MIT): a symbol
// in his table gets ln(weight / total) (total 2688); a valid code missing from it (prosigns other than <BT>,
// rarer punctuation) his rarest character's, ln(8 / 2688) (heuristic); anything else ln(1e-6).
class TextModel {
public:
    TextModel();
    double char_logprob(const std::string& symbol) const;
    // Mean log-probability per character, nats (word spaces " " ignored), summed in order with Python's
    // compensated (Neumaier) float sum; none without characters.
    std::optional<double> mean_logprob(const std::vector<std::string>& symbols) const;

private:
    std::map<std::string, double> logp_;
    double floor_;
};

// One branch as selection sees it.
struct BranchView {
    int index;                            // 0-based ladder position (branch k = index + 1); longer is larger
    double length_s;                      // realized length N_k / r, s
    std::optional<Fit> fit;               // the branch's current duration fit
    std::optional<double> text_logprob;   // mean over the branch's recent characters, nats per character
};

class Selector {
public:
    // lengths_s: the branches' realized lengths, s.
    Selector(const BankConfig& cfg, std::vector<double> lengths_s);

    // The branch's own fitted dit agrees with its length: |ln(L_k / (length_dits x T_k))| <= eligibility_tolerance,
    // once the fit's memory holds min_fit_weight elements (and T_k > 0 s).
    bool eligible(const BranchView& view) const;
    // (branch, chosen among eligible branches). Eligible branches: the best quality and those within
    // quality_tie_nats of it; if more than one and all have text, those within text_tie_nats of the likeliest
    // text; then the longest. None eligible: the likeliest text if it leads the next by text_separation_nats,
    // else the branch whose length is nearest length_dits x T_P in ln L (prior_t_s: T_P, s, only when the
    // periodicity estimate is confident), else branch 0.
    std::pair<int, bool> best(const std::vector<BranchView>& views, std::optional<double> prior_t_s) const;
    // One more selection standing for `instants` selection instants: a switch needs the same other branch best
    // for switch_persistence instants in a row, all as the best eligible branch or all as the fallback pick.
    // instants <= 0 changes nothing. t_now: stream time, s. Returns the current branch.
    int update(const std::vector<BranchView>& views, int instants, double t_now, std::optional<double> prior_t_s);

    int current() const { return current_; }
    // The pending switch (branch, chosen among eligible branches) and its count of instants.
    const std::optional<std::pair<int, bool>>& candidate() const { return candidate_; }
    int count() const { return count_; }
    // Per branch: stream time, s, at which its current eligible run began; none while it is not eligible.
    const std::vector<std::optional<double>>& eligible_since() const { return eligible_since_; }

private:
    BankConfig cfg_;
    std::vector<double> lengths_;
    int current_ = 0;
    std::optional<std::pair<int, bool>> candidate_;
    int count_ = 0;
    std::vector<std::optional<double>> eligible_since_;
};

}  // namespace kz4ap::bank
