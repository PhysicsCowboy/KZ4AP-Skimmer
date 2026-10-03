// The bank decoder's branch filters and envelope likelihood against the prototype's golden values
// (engine/tests/data/bank/filters.json, from training/kz4ap_proto/golden.py): integers exactly, doubles
// to relative 1e-9 (floor 1e-12 absolute).
#include "kz4ap/bank/filters.hpp"

#include "golden.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <complex>
#include <string>

namespace {

using namespace kz4ap::bank;
using kz4ap::test::load_golden;

using kz4ap::test::expect_close;

std::vector<std::complex<double>> input(const nlohmann::json& g) {
    const auto re = g.at("boxcar_input_re").get<std::vector<double>>();
    const auto im = g.at("boxcar_input_im").get<std::vector<double>>();
    std::vector<std::complex<double>> u(re.size());
    for (std::size_t i = 0; i < u.size(); ++i) u[i] = {re[i], im[i]};
    return u;
}

TEST(BankFilters, BranchLengths) {
    const auto g = load_golden("filters");
    const auto want = g.at("branch_lengths_s").get<std::vector<double>>();
    const auto got = branch_lengths_s(BankConfig{});
    ASSERT_EQ(got.size(), want.size());
    EXPECT_EQ(got.size(), 32u);
    for (std::size_t k = 0; k < want.size(); ++k) expect_close(got[k], want[k]);
}

TEST(BankFilters, BranchSamplesAndRealizedLengths) {
    const auto g = load_golden("filters");
    const auto lengths = branch_lengths_s(BankConfig{});
    for (const int rate : {1500, 2000}) {
        const auto n_want = g.at("branch_samples_" + std::to_string(rate)).get<std::vector<int>>();
        const auto n_got = branch_samples(lengths, rate);
        ASSERT_EQ(n_got.size(), n_want.size());
        for (std::size_t k = 0; k < n_want.size(); ++k) EXPECT_EQ(n_got[k], n_want[k]) << "rate " << rate << " k " << k;
        const auto r_want = g.at("realized_lengths_s_" + std::to_string(rate)).get<std::vector<double>>();
        const auto r_got = realized_lengths_s(BankConfig{}, rate);
        ASSERT_EQ(r_got.size(), r_want.size());
        for (std::size_t k = 0; k < r_want.size(); ++k) expect_close(r_got[k], r_want[k]);
    }
}

TEST(BankFilters, Boxcar) {
    const auto g = load_golden("filters");
    const auto u = input(g);
    ASSERT_EQ(u.size(), 400u);
    for (const int n : {14, 276}) {
        const auto re = g.at("boxcar_" + std::to_string(n) + "_re").get<std::vector<double>>();
        const auto im = g.at("boxcar_" + std::to_string(n) + "_im").get<std::vector<double>>();
        const auto v = boxcar(u, n);
        ASSERT_EQ(v.size(), re.size());
        for (std::size_t m = 0; m < v.size(); ++m) {
            expect_close(v[m].real(), re[m]);
            expect_close(v[m].imag(), im[m]);
        }
    }
}

TEST(BankFilters, BoxcarStartUpAndUnityGain) {
    const std::vector<std::complex<double>> ones(10, {1.0, 0.0});
    const auto v = boxcar(ones, 4);
    EXPECT_DOUBLE_EQ(v[0].real(), 0.25);  // zeros before the stream
    EXPECT_DOUBLE_EQ(v[2].real(), 0.75);
    EXPECT_DOUBLE_EQ(v[3].real(), 1.0);
    EXPECT_DOUBLE_EQ(v[9].real(), 1.0);
}

TEST(BankFilters, PowerResponse) {
    const auto g = load_golden("filters");
    const auto f = g.at("power_response_f_hz").get<std::vector<double>>();
    const auto want = g.at("power_response_n14_1500").get<std::vector<double>>();
    ASSERT_EQ(f.size(), want.size());
    for (std::size_t i = 0; i < f.size(); ++i) expect_close(power_response(f[i], 14, 1500.0), want[i]);
}

TEST(BankFilters, LogBesselI0) {
    const auto g = load_golden("filters");
    const auto z = g.at("log_bessel_i0_z").get<std::vector<double>>();
    const auto want = g.at("log_bessel_i0").get<std::vector<double>>();
    ASSERT_EQ(z.size(), want.size());
    for (std::size_t i = 0; i < z.size(); ++i) expect_close(log_bessel_i0(z[i]), want[i]);
}

TEST(BankFilters, EnvelopeLlr) {
    const auto g = load_golden("filters");
    const auto xs = g.at("envelope_llr_x").get<std::vector<double>>();
    const auto as = g.at("envelope_llr_a").get<std::vector<double>>();
    const auto want = g.at("envelope_llr").get<std::vector<double>>();
    ASSERT_EQ(want.size(), xs.size() * as.size());
    for (std::size_t i = 0; i < xs.size(); ++i)
        for (std::size_t j = 0; j < as.size(); ++j) expect_close(envelope_llr(xs[i], as[j]), want[i * as.size() + j]);
}

TEST(BankFilters, Logistic) {
    EXPECT_DOUBLE_EQ(logistic(0.0), 0.5);
    EXPECT_DOUBLE_EQ(logistic(1000.0), 1.0 / (1.0 + std::exp(-50.0)));
    EXPECT_DOUBLE_EQ(logistic(-1000.0), 1.0 / (1.0 + std::exp(50.0)));
}

TEST(BankFilters, MatrixIsRowMajor) {
    Matrix m;
    m.rows = 2;
    m.cols = 3;
    m.v.assign(6, 0.0);
    m.at(1, 2) = 7.0;
    EXPECT_EQ(m.v[5], 7.0);
    const Matrix& c = m;
    EXPECT_EQ(c.at(1, 2), 7.0);
}

}  // namespace
