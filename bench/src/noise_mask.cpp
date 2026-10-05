#include "noise_mask.hpp"

#include "kz4ap/bank/filters.hpp"
#include "kz4ap/bank/noise.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <random>
#include <stdexcept>

namespace kz4ap::bench {

namespace {

int round_int(double x) { return static_cast<int>(std::nearbyint(x)); }

// (0, 1]: 53 random bits, never 0 (log is taken of it).
double unit_open_closed(std::mt19937_64& rng) {
    return (static_cast<double>(rng() >> 11) + 1.0) * (1.0 / 9007199254740992.0);
}

}  // namespace

std::vector<std::complex<double>> white_noise(std::size_t samples, std::uint64_t seed) {
    std::mt19937_64 rng(seed);
    std::vector<std::complex<double>> u(samples);
    const double sigma = std::sqrt(0.5);  // per real component, FS
    for (auto& x : u) {
        const double r = sigma * std::sqrt(-2.0 * std::log(unit_open_closed(rng)));
        const double phi = 2.0 * std::numbers::pi * unit_open_closed(rng);
        x = {r * std::cos(phi), r * std::sin(phi)};
    }
    return u;
}

MaskRun run_mask(const bank::BankConfig& base_cfg, std::span<const std::complex<double>> u, double rate_hz,
                 double from_s, double to_s) {
    bank::BankConfig cfg = base_cfg;
    cfg.noise_method = "spectrum";
    const auto n = bank::branch_samples(bank::branch_lengths_s(cfg), rate_hz);
    bank::Matrix P;
    P.rows = static_cast<int>(n.size());
    P.cols = static_cast<int>(u.size());
    P.v.resize(static_cast<std::size_t>(P.rows) * u.size());
    for (int k = 0; k < P.rows; ++k) {
        const auto v = bank::boxcar(u, n[static_cast<std::size_t>(k)]);
        for (int i = 0; i < P.cols; ++i) P.at(k, i) = bank::boxcar_power_f32(v[static_cast<std::size_t>(i)]);
    }
    bank::SpectrumNoise est(cfg, rate_hz, n);
    // The estimate takes segment j = [j M, (j + 1) M) once the block's end reaches (j + 1) M + look (noise.cpp);
    // with M > block at most one segment per block, so a block's change in the counts belongs to that segment.
    const int m = std::max(16, round_int(cfg.segment_s * rate_hz));
    const int look = n[0] - 1 + round_int(cfg.guard_margin_s * rate_hz);
    const int block = std::max(1, round_int(cfg.block_s * rate_hz));
    if (m <= block) throw std::invalid_argument("run_mask: a segment must be longer than a block");
    const bool windowed = from_s < to_s;
    MaskRun r;
    r.seconds = static_cast<double>(u.size()) / rate_hz;
    r.window_s = windowed ? to_s - from_s : 0.0;
    int processed = 0;
    for (int n0 = 0; n0 < P.cols; n0 += block) {
        const int n1 = std::min(n0 + block, P.cols);
        const int before_seg = est.segments(), before_off = est.segments_offered();
        const double before_kept = est.kept_fraction_sum();
        est.update(u, P, n0, n1);
        (void)est.sigma2();
        const int now = n1 - look >= m ? (n1 - look) / m : 0;
        if (now - processed > 1) throw std::logic_error("run_mask: more than one segment in a block");
        if (now > processed && windowed) {
            const double start_s = static_cast<double>(processed) * m / rate_hz;
            if (start_s >= from_s && start_s < to_s) {
                r.window_segments += est.segments() - before_seg;
                r.window_offered += est.segments_offered() - before_off;
                r.window_kept_fraction_sum += est.kept_fraction_sum() - before_kept;
            }
        }
        processed = now;
    }
    r.segments = est.segments();
    r.segments_offered = est.segments_offered();
    r.kept_fraction_sum = est.kept_fraction_sum();
    if (r.segments > 0) r.masked_power = est.masked_branch_power();
    return r;
}

MaskBias measure_mask_bias(const bank::BankConfig& cfg, double rate_hz, const std::vector<std::uint64_t>& seeds,
                           double seconds) {
    const auto n = bank::branch_samples(bank::branch_lengths_s(cfg), rate_hz);
    const std::size_t K = n.size();
    const auto samples = static_cast<std::size_t>(std::llround(seconds * rate_hz));
    MaskBias b;
    std::vector<double> num(K, 0.0);
    for (std::uint64_t seed : seeds) {
        const auto u = white_noise(samples, seed);
        const MaskRun r = run_mask(cfg, u, rate_hz);
        if (r.segments == 0) throw std::runtime_error("measure_mask_bias: no segment entered");
        std::vector<double> ratio(K);
        for (std::size_t k = 0; k < K; ++k) {
            const double truth = 0.5 / n[k];  // sigma_v,k^2 per real component of 1 FS^2 white noise, FS^2
            ratio[k] = r.masked_power[k] / truth;
            num[k] += r.masked_power[k] * r.segments;
        }
        b.per_seed.push_back(std::move(ratio));
        b.segments += r.segments;
        b.segments_offered += r.segments_offered;
        b.kept_fraction += r.kept_fraction_sum;
    }
    b.bias.resize(K);
    b.scatter.resize(K);
    for (std::size_t k = 0; k < K; ++k) {
        b.bias[k] = num[k] / b.segments / (0.5 / n[k]);
        double ss = 0.0;
        for (const auto& s : b.per_seed) ss += (s[k] - b.bias[k]) * (s[k] - b.bias[k]);
        const double sd = b.per_seed.size() > 1 ? std::sqrt(ss / static_cast<double>(b.per_seed.size() - 1)) : 0.0;
        b.scatter[k] = sd / b.bias[k];
    }
    b.kept_fraction /= b.segments_offered;
    return b;
}

}  // namespace kz4ap::bench
