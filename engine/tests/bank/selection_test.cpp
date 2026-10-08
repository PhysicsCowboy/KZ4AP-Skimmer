// The bank decoder's text model and branch selection against the prototype (training/kz4ap_proto/text.py,
// select.py).
// 1. Golden values (engine/tests/data/bank/selection.json, from golden.py golden_selection): the selector on a
//    scripted sequence of 40 updates over the view sets of test_proto_select.py: eligibility, best, the
//    returned branch, the pending candidate and its count exactly, eligible_since (s) to relative 1e-9; the
//    text model's patterns exactly and its log-probabilities to relative 1e-9.
// 2. The Python tests (training/tests/test_proto_text.py, test_proto_select.py), one C++ test per Python test
//    with the same inputs and assertions.
#include "kz4ap/bank/filters.hpp"
#include "kz4ap/bank/selection.hpp"

#include "golden.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace kz4ap::bank;
using kz4ap::test::expect_close;
using kz4ap::test::load_golden;

constexpr double kTotal = 2688.0;  // sum of VE3NEA's character weights

// pytest.approx(expected): relative 1e-6, absolute 1e-12.
void expect_approx(double actual, double expected) {
    EXPECT_NEAR(actual, expected, std::max(1e-6 * std::abs(expected), 1e-12));
}

// ---------------------------------------------------------------- golden values

TEST(BankText, MatchesPrototype) {
    const auto g = load_golden("selection");
    const auto patterns = g.at("patterns").get<std::vector<std::string>>();
    const auto decoded = g.at("decoded").get<std::vector<std::string>>();
    for (std::size_t i = 0; i < patterns.size(); ++i) EXPECT_EQ(decode_pattern(patterns[i]), decoded[i]) << patterns[i];
    const TextModel m;
    const auto symbols = g.at("symbols").get<std::vector<std::string>>();
    const auto lp = g.at("char_logprob").get<std::vector<double>>();
    ASSERT_EQ(symbols.size(), 30u);
    for (std::size_t i = 0; i < symbols.size(); ++i) {
        SCOPED_TRACE("symbol '" + symbols[i] + "'");
        expect_close(m.char_logprob(symbols[i]), lp[i]);
    }
    const auto strings = g.at("strings").get<std::vector<std::vector<std::string>>>();
    const auto& means = g.at("mean_logprob");
    ASSERT_EQ(strings.size(), 5u);
    for (std::size_t i = 0; i < strings.size(); ++i) {
        const auto got = m.mean_logprob(strings[i]);
        if (means[i].is_null()) {
            EXPECT_FALSE(got.has_value()) << "string " << i;
        } else {
            ASSERT_TRUE(got.has_value()) << "string " << i;
            expect_close(*got, means[i].get<double>());
        }
    }
}

TEST(BankSelection, MatchesPrototypeOnAScriptedSequence) {
    const auto g = load_golden("selection");
    const BankConfig cfg;
    const auto lengths = g.at("lengths_s").get<std::vector<double>>();
    const auto ours = realized_lengths_s(cfg, 1500.0);
    ASSERT_EQ(ours.size(), lengths.size());
    for (std::size_t k = 0; k < lengths.size(); ++k) expect_close(ours[k], lengths[k]);
    Selector sel(cfg, lengths);
    const auto& steps = g.at("steps");
    ASSERT_EQ(steps.size(), 40u);
    for (std::size_t s = 0; s < steps.size(); ++s) {
        const auto& st = steps[s];
        SCOPED_TRACE("step " + std::to_string(s) + " (" + st.at("scenario").get<std::string>() + ")");
        std::vector<BranchView> views;
        for (std::size_t k = 0; k < lengths.size(); ++k)
            views.push_back(BranchView{static_cast<int>(k), lengths[k], std::nullopt, std::nullopt});
        for (const auto& f : st.at("fits")) {
            const auto k = f.at(0).get<std::size_t>();
            views[k].fit = Fit{f.at(1).get<double>(), f.at(2).get<double>(), f.at(3).get<double>(),
                               f.at(4).get<double>(), f.at(5).get<double>(), f.at(6).get<double>()};
        }
        for (const auto& t : st.at("texts")) views[t.at(0).get<std::size_t>()].text_logprob = t.at(1).get<double>();
        const std::optional<double> prior =
            st.at("prior_t_s").is_null() ? std::nullopt : std::optional<double>(st.at("prior_t_s").get<double>());

        std::string eligible;
        for (const auto& v : views) eligible += sel.eligible(v) ? '1' : '0';
        EXPECT_EQ(eligible, st.at("eligible").get<std::string>());
        const auto best = sel.best(views, prior);
        EXPECT_EQ(best.first, st.at("best").at(0).get<int>());
        EXPECT_EQ(best.second, st.at("best").at(1).get<bool>());

        EXPECT_EQ(sel.update(views, st.at("instants").get<int>(), st.at("t_now_s").get<double>(), prior),
                  st.at("current").get<int>());
        const auto& cand = st.at("candidate");
        if (cand.is_null()) {
            EXPECT_FALSE(sel.candidate().has_value());
        } else {
            ASSERT_TRUE(sel.candidate().has_value());
            EXPECT_EQ(sel.candidate()->first, cand.at(0).get<int>());
            EXPECT_EQ(sel.candidate()->second, cand.at(1).get<bool>());
        }
        EXPECT_EQ(sel.count(), st.at("count").get<int>());
        const auto& since = st.at("eligible_since");
        ASSERT_EQ(sel.eligible_since().size(), since.size());
        for (std::size_t k = 0; k < since.size(); ++k) {
            if (since[k].is_null()) {
                EXPECT_FALSE(sel.eligible_since()[k].has_value()) << "branch index " << k;
            } else {
                ASSERT_TRUE(sel.eligible_since()[k].has_value()) << "branch index " << k;
                expect_close(*sel.eligible_since()[k], since[k].get<double>());
            }
        }
    }
}

// ---------------------------------------------------------------- the Python text tests

TEST(BankTextPython, PatternsDecodeAsTheEngineReadsThem) {
    EXPECT_EQ(decode_pattern(".-"), "A");
    EXPECT_EQ(decode_pattern("-...-"), "<BT>");
    EXPECT_EQ(decode_pattern("...-.-"), "<SK>");
    EXPECT_EQ(decode_pattern("..--.."), "?");
    EXPECT_EQ(decode_pattern("........"), "<HH>");
    EXPECT_EQ(decode_pattern("........."), "<HH>");
    EXPECT_EQ(decode_pattern("--.--."), "*");
}

TEST(BankTextPython, TextLogProbabilityUsesVe3neaFrequencies) {
    const TextModel m;
    expect_approx(m.char_logprob("E"), std::log(321.0 / kTotal));
    expect_approx(m.char_logprob("<KN>"), std::log(8.0 / kTotal));  // valid but not in his table: his rarest
    EXPECT_EQ(m.char_logprob("*"), invalid_logprob());
    expect_approx(invalid_logprob(), std::log(1e-6));
    const auto mean = m.mean_logprob({"E", " ", "T"});
    ASSERT_TRUE(mean.has_value());
    expect_approx(*mean, (std::log(321.0 / kTotal) + std::log(236.0 / kTotal)) / 2.0);
    EXPECT_FALSE(m.mean_logprob({" "}).has_value());
    EXPECT_FALSE(m.mean_logprob({}).has_value());
}

// ---------------------------------------------------------------- the Python selection tests

const BankConfig kCfg{};

const std::vector<double>& L() {
    static const std::vector<double> l = realized_lengths_s(kCfg, 1500.0);
    return l;
}

// test_proto_select.fit_for: the dit this branch matches, times scale.
Fit fit_for(int k, double quality, double weight = 24.0, double scale = 1.0) {
    const double t = L()[static_cast<std::size_t>(k)] / 0.8 * scale;
    return Fit{t, 3.0, 0.0, t, quality, weight};
}

std::vector<BranchView> views(const std::map<int, Fit>& fits = {}, const std::map<int, double>& texts = {}) {
    std::vector<BranchView> out;
    for (std::size_t k = 0; k < L().size(); ++k) {
        const int i = static_cast<int>(k);
        BranchView v{i, L()[k], std::nullopt, std::nullopt};
        if (const auto f = fits.find(i); f != fits.end()) v.fit = f->second;
        if (const auto t = texts.find(i); t != texts.end()) v.text_logprob = t->second;
        out.push_back(v);
    }
    return out;
}

BranchView view15(std::optional<Fit> fit) { return BranchView{15, L()[15], fit, std::nullopt}; }

using Best = std::pair<int, bool>;

TEST(BankSelectionPython, ABranchIsEligibleWhenItsFittedDitMatchesItsLength) {
    const Selector sel(kCfg, L());
    EXPECT_TRUE(sel.eligible(view15(fit_for(15, -1.0))));
    EXPECT_TRUE(sel.eligible(view15(fit_for(15, -1.0, 24.0, 1.09))));
    EXPECT_FALSE(sel.eligible(view15(fit_for(15, -1.0, 24.0, 1.12))));  // beyond one ladder step
    EXPECT_FALSE(sel.eligible(view15(fit_for(15, -1.0, 3.0))));         // too little memory
    EXPECT_FALSE(sel.eligible(view15(std::nullopt)));
}

TEST(BankSelectionPython, TheBestEligibleQualityWins) {
    const auto v = views({{14, fit_for(14, -1.0)}, {15, fit_for(15, -0.5)}, {20, fit_for(20, 0.0, 24.0, 1.5)}});
    EXPECT_EQ(Selector(kCfg, L()).best(v, std::nullopt), Best(15, true));  // branch 20 fits better but is not eligible
}

TEST(BankSelectionPython, QualityTiesGoToTheLikelierTextThenToTheLongerBranch) {
    const Selector sel(kCfg, L());
    const std::map<int, Fit> fits = {{14, fit_for(14, -0.50)}, {15, fit_for(15, -0.52)}};  // within 0.05 nats: a tie
    EXPECT_EQ(sel.best(views(fits, {{14, -2.0}, {15, -3.5}}), std::nullopt), Best(14, true));
    EXPECT_EQ(sel.best(views(fits, {{14, -2.0}, {15, -2.05}}), std::nullopt), Best(15, true));  // texts tie too
    EXPECT_EQ(sel.best(views(fits), std::nullopt), Best(15, true));                            // no text
}

TEST(BankSelectionPython, WithoutAnEligibleBranchTextThenPeriodicityThenTheShortest) {
    const Selector sel(kCfg, L());
    EXPECT_EQ(sel.best(views({}, {{3, -2.0}, {20, -4.0}}), std::nullopt), Best(3, false));  // the text separates
    // np.argmin(np.abs(np.log(L / (0.8 x 0.048)))): derived 15 (0.8 x 48 ms = 38.4 ms = 9.6 ms x 1.1^14.55,
    // nearer 1.1^15 in ln L).
    int nearest = 0;
    double d_best = 0.0;
    for (std::size_t k = 0; k < L().size(); ++k) {
        const double d = std::abs(std::log(L()[k] / (0.8 * 0.048)));
        if (k == 0 || d < d_best) {
            d_best = d;
            nearest = static_cast<int>(k);
        }
    }
    EXPECT_EQ(nearest, 15);
    EXPECT_EQ(sel.best(views({}, {{3, -2.0}, {20, -2.5}}), 0.048), Best(nearest, false));  // nearest 0.8 T_P
    EXPECT_EQ(sel.best(views(), std::nullopt), Best(0, false));
}

TEST(BankSelectionPython, ASwitchNeedsMInstantsInARow) {
    Selector sel(kCfg, L());  // M = 4
    const auto v15 = views({{15, fit_for(15, -0.5)}});
    const auto v16 = views({{16, fit_for(16, -0.5)}});
    for (const double t : {1.0, 2.0, 3.0}) EXPECT_EQ(sel.update(v15, 1, t, std::nullopt), 0);
    EXPECT_EQ(sel.update(v16, 1, 4.0, std::nullopt), 0);   // the run is broken
    EXPECT_EQ(sel.update(v16, 3, 5.0, std::nullopt), 16);  // three more instants in one update: four in a row
    ASSERT_TRUE(sel.eligible_since()[16].has_value());
    EXPECT_EQ(*sel.eligible_since()[16], 4.0);
    EXPECT_FALSE(sel.eligible_since()[15].has_value());
}

TEST(BankSelectionPython, FallbackPicksDoNotCompleteAnEligibleRun) {
    // Spec 4.6: the best *eligible* branch for M instants in a row. Branch 16 as a fallback pick (text only)
    // twice, then eligible twice: no switch yet with M = 4.
    Selector sel(kCfg, L());
    const auto fallback16 = views({}, {{16, -2.0}, {20, -4.0}});
    const auto eligible16 = views({{16, fit_for(16, -0.5)}});
    for (const double t : {1.0, 2.0}) EXPECT_EQ(sel.update(fallback16, 1, t, std::nullopt), 0);
    for (const double t : {3.0, 4.0}) EXPECT_EQ(sel.update(eligible16, 1, t, std::nullopt), 0);
    EXPECT_EQ(sel.update(eligible16, 2, 5.0, std::nullopt), 16);
}

TEST(BankSelectionPython, AQualityTieSkipsTheTextStepWhenATiedBranchHasNoTextYet) {
    // Branch 15 has no decoded text yet (for example it just started an over): it is not read as infinitely
    // unlikely text; the text step is skipped and the tie goes to the longer branch.
    const Selector sel(kCfg, L());
    const std::map<int, Fit> fits = {{14, fit_for(14, -0.50)}, {15, fit_for(15, -0.52)}};  // a tie
    EXPECT_EQ(sel.best(views(fits, {{14, -2.0}}), std::nullopt), Best(15, true));
    EXPECT_EQ(sel.best(views(fits, {{15, -2.0}}), std::nullopt), Best(15, true));
    EXPECT_EQ(sel.best(views(fits, {{14, -2.0}, {15, -3.5}}), std::nullopt), Best(14, true));  // the text decides
}

TEST(BankSelectionPython, AnUpdateWithoutSelectionInstantsChangesNothing) {
    Selector sel(kCfg, L());
    const auto v16 = views({{16, fit_for(16, -0.5)}});
    EXPECT_EQ(sel.update(v16, 3, 1.0, std::nullopt), 0);
    EXPECT_EQ(sel.update(v16, 0, 2.0, std::nullopt), 0);  // no instant: no count, no switch
    ASSERT_TRUE(sel.candidate().has_value());
    EXPECT_EQ(*sel.candidate(), Best(16, true));
    EXPECT_EQ(sel.count(), 3);
    ASSERT_TRUE(sel.eligible_since()[16].has_value());
    EXPECT_EQ(*sel.eligible_since()[16], 1.0);
    EXPECT_EQ(sel.update(views(), 0, 3.0, std::nullopt), 0);  // nor does it clear the eligibility time
    ASSERT_TRUE(sel.eligible_since()[16].has_value());
    EXPECT_EQ(*sel.eligible_since()[16], 1.0);
    EXPECT_EQ(sel.update(v16, 1, 4.0, std::nullopt), 16);  // the fourth instant in a row
}

}  // namespace
