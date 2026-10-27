#include "kz4ap/viterbi/newborn.hpp"

#include "keyed_signals.hpp"
#include "kz4ap/channelizer.hpp"
#include "kz4ap/viterbi/amplitude.hpp"
#include "kz4ap/viterbi/front_filter.hpp"
#include "kz4ap/viterbi/mark_template.hpp"
#include "kz4ap/viterbi/observation.hpp"
#include "kz4ap/viterbi/systematic.hpp"
#include "kz4ap/viterbi/weight.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numbers>
#include <sstream>
#include <string>
#include <thread>
#include <tuple>
#include <utility>
#include <vector>

using namespace kz4ap;
using namespace kz4ap::viterbi;

// The sequences are the newborn study's (.superpowers/sdd/2026-10-20-new-decoder-prototype-1/
// newborn-integral-candidates.md, section 3; its scratch generator gen.py), drawn here in C++: the decided chain (the
// 4 097-tap channel filter at 192 kHz, decimation 128, the 25 front taps), sigma_y^2 = 1 FS^2 (N0 = 1 / 74.4998 Hz in
// FS^2/Hz); per first mark a carrier of constant amplitude a = sqrt(10^(L/10) N0 x 500 Hz) and random phase, keyed down
// at an edge offset u drawn from the 512 input samples of [0, Delta) before sample 0 (sample 0 is the first at or after
// the key-down) and up L samples later, S_u(k) - S_u(k - L) through the chain (step_response), plus complex Gaussian
// noise with the chain's correlation at the samples (rho to lag 15 from the taps; Cholesky factor of its Toeplitz
// matrix); the story is the right one (claims the true key-down, or one shifted by +/-4 samples for the stress cases,
// and the true length); its scored samples are j = 0 ... L + 15 after the claimed key-down with s != 0, s and v from its
// EdgeHistory, V' from the 21-sample window peak of e^2 and the sample's own e^2 / s^2, f = 0.02. The noise draws are
// portable (splitmix64 and Box-Muller), so both platforms draw the same sequences.

namespace {

constexpr double kFloor = 0.02;         // f (1; placeholder, owner, 2026-10-23)
constexpr double kSigma2 = 1.0;         // sigma_y^2 (FS^2)
constexpr double kSwitchSigmaU = 0.1;   // nepers (derived; part 8)
constexpr int kPre = 24;                // samples generated before the true key-down
constexpr int kPost = 30;               // samples generated after the template's end
constexpr int kOffsets = 512;           // edge offsets per sample interval at 192 kHz

// --- The chain ---

struct Chain {
    OutputNoise noise;
    TemplateTables tables;
    std::vector<double> rho;                 // rho_0 ... rho_15 at lags of Delta (1)
    std::vector<std::vector<double>> step;   // [u][j]: S(j Delta + u) at the envelope samples j = 0 ... (1 beyond)
    double n0 = 0.0;                         // FS^2/Hz, with sigma_y^2 = 1 FS^2
    double u_lo = 0.0;                       // ln(a / 1 FS) at -15 dB S500
    double u_hi = 0.0;                       // ln(a / 1 FS) at +85 dB S500
};

const Chain& chain() {
    static const Chain c = [] {
        Chain out;
        ChannelizerConfig config;
        config.sample_rate = 192000;
        config.fft_size = 8192;
        const std::vector<double> taps = channel_filter_taps(config);
        const int decimation = config.fft_size / config.channel_bins;
        const std::vector<double> front = front_filter_taps(1500.0, 0.012, 0.0025);
        out.noise = output_noise(taps, config.sample_rate, decimation, front, 4);
        out.tables = make_template_tables(taps, config.sample_rate, decimation, front, 4, 0.0025, 0.0008, 0.0047);
        // rho_m to lag 15 by output_noise's formula (front_filter.hpp, stage 3); the response spans 14 samples.
        constexpr int kLags = 16;
        const std::size_t max_lag = (kLags - 1) * 4 + front.size() - 1;
        std::vector<double> r_channel(max_lag + 1, 0.0);
        for (std::size_t d = 0; d <= max_lag; ++d) {
            const std::size_t shift = d * static_cast<std::size_t>(decimation);
            for (std::size_t n = 0; n + shift < taps.size(); ++n) r_channel[d] += taps[n] * taps[n + shift];
        }
        std::vector<double> r_front(kLags, 0.0);
        for (int m = 0; m < kLags; ++m) {
            for (std::size_t a = 0; a < front.size(); ++a) {
                for (std::size_t b = 0; b < front.size(); ++b) {
                    const long long d = 4LL * m + static_cast<long long>(a) - static_cast<long long>(b);
                    r_front[static_cast<std::size_t>(m)] +=
                        front[a] * front[b] * r_channel[static_cast<std::size_t>(std::llabs(d))];
                }
            }
        }
        for (int m = 0; m < kLags; ++m) out.rho.push_back(r_front[static_cast<std::size_t>(m)] / r_front[0]);
        for (int u = 0; u < kOffsets; ++u) {
            const std::vector<double> s =
                step_response(taps, config.sample_rate, decimation, front, 0.0025, u / static_cast<double>(config.sample_rate));
            std::vector<double> at_samples;
            for (std::size_t j = 0; 4 * j < s.size(); ++j) at_samples.push_back(s[4 * j]);
            out.step.push_back(std::move(at_samples));
        }
        out.n0 = kSigma2 / out.noise.sigma2_per_n0_hz;
        out.u_lo = 0.5 * std::log(std::pow(10.0, -1.5) * out.n0 * 500.0);
        out.u_hi = 0.5 * std::log(std::pow(10.0, 8.5) * out.n0 * 500.0);
        return out;
    }();
    return c;
}

const NewbornTables& tables() {
    static const NewbornTables t(chain().noise, chain().noise.sigma2_per_n0_hz, std::nullopt);
    return t;
}

// a (FS) of a level (dB S500), and a level (dB S500) of u = ln(a / 1 FS).
double amplitude_of(double level_db) { return std::sqrt(std::pow(10.0, level_db / 10.0) * chain().n0 * 500.0); }
double level_of_u(double u) { return 20.0 * u / std::numbers::ln10 - 10.0 * std::log10(chain().n0 * 500.0); }

// S_u(k) at envelope sample k after the first sample at or after the key-down (0 before it, 1 past the table).
double step_at(int u, int k) {
    if (k < 0) return 0.0;
    const std::vector<double>& s = chain().step[static_cast<std::size_t>(u)];
    return static_cast<std::size_t>(k) < s.size() ? s[static_cast<std::size_t>(k)] : 1.0;
}

// --- Portable random draws ---

class Rng {
public:
    explicit Rng(std::uint64_t seed) : state_(seed) {}
    std::uint64_t next() {
        std::uint64_t z = (state_ += 0x9E3779B97F4A7C15ULL);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
        return z ^ (z >> 31);
    }
    double uniform() { return static_cast<double>(next() >> 11) * 0x1.0p-53; }  // [0, 1)
    double normal() {
        if (has_spare_) {
            has_spare_ = false;
            return spare_;
        }
        const double r = std::sqrt(-2.0 * std::log(1.0 - uniform()));
        const double phi = 2.0 * std::numbers::pi * uniform();
        spare_ = r * std::sin(phi);
        has_spare_ = true;
        return r * std::cos(phi);
    }

private:
    std::uint64_t state_;
    bool has_spare_ = false;
    double spare_ = 0.0;
};

// The lower Cholesky factor (row-major) of the n x n Toeplitz matrix of rho.
std::vector<double> cholesky_of_rho(int n) {
    const std::vector<double>& rho = chain().rho;
    const auto at = [&](int i, int j) {
        const auto d = static_cast<std::size_t>(std::abs(i - j));
        return d < rho.size() ? rho[d] : 0.0;
    };
    std::vector<double> l(static_cast<std::size_t>(n) * n, 0.0);
    for (int i = 0; i < n; ++i) {
        for (int j = 0; j <= i; ++j) {
            double sum = at(i, j);
            for (int k = 0; k < j; ++k) sum -= l[static_cast<std::size_t>(i) * n + k] * l[static_cast<std::size_t>(j) * n + k];
            if (i == j) {
                if (!(sum > 0.0)) throw std::runtime_error("cholesky_of_rho: not positive definite");
                l[static_cast<std::size_t>(i) * n + i] = std::sqrt(sum);
            } else {
                l[static_cast<std::size_t>(i) * n + j] = sum / l[static_cast<std::size_t>(j) * n + j];
            }
        }
    }
    return l;
}

// n samples of complex Gaussian noise of power sigma_y^2 = 1 FS^2 with the chain's correlation.
std::vector<std::complex<double>> correlated_noise(int n, const std::vector<double>& factor, Rng& rng) {
    std::vector<double> zr(static_cast<std::size_t>(n));
    std::vector<double> zi(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) {
        zr[static_cast<std::size_t>(i)] = rng.normal();
        zi[static_cast<std::size_t>(i)] = rng.normal();
    }
    std::vector<std::complex<double>> out(static_cast<std::size_t>(n));
    const double scale = std::sqrt(0.5 * kSigma2);
    for (int i = 0; i < n; ++i) {
        double re = 0.0;
        double im = 0.0;
        for (int k = 0; k <= i; ++k) {
            const double f = factor[static_cast<std::size_t>(i) * n + k];
            re += f * zr[static_cast<std::size_t>(k)];
            im += f * zi[static_cast<std::size_t>(k)];
        }
        out[static_cast<std::size_t>(i)] = {scale * re, scale * im};
    }
    return out;
}

// --- Sequences ---

struct Observation {
    double e;     // FS
    double s;     // 1
    double v;     // 1
    double vp;    // V', FS^2
    int j;        // samples after the claimed key-down
    double peak;  // the window's peak of e^2, FS^2 (V' is made from it)
};
using Sequence = std::vector<Observation>;

struct CaseSpec {
    std::string name;
    std::optional<double> level_db;  // dB S500; none: noise alone
    int length;                      // the mark's samples: 18 (25 WPM dot), 54 (dash), 9 (50 WPM dot), 27 (dash)
    int shift;                       // the claim's offset from the true key-down (samples)
    std::uint64_t seed;
};

// One first mark of a case. gain: the fading gain at each generated sample (empty: constant, 1).
Sequence make_sequence(const CaseSpec& c, std::uint64_t index, const std::vector<std::complex<double>>& gain = {}) {
    const int span = kPre + c.length + 16 + kPost;
    static thread_local std::vector<std::pair<int, std::vector<double>>> factors;
    const std::vector<double>* factor = nullptr;
    for (const auto& [n, f] : factors)
        if (n == span) factor = &f;
    if (factor == nullptr) {
        factors.emplace_back(span, cholesky_of_rho(span));
        factor = &factors.back().second;
    }
    Rng rng(c.seed * 1000003ULL + index);
    std::vector<std::complex<double>> x = correlated_noise(span, *factor, rng);
    if (c.level_db) {
        const double a = amplitude_of(*c.level_db);
        const int u = static_cast<int>(rng.next() % kOffsets);
        const std::complex<double> phase = std::polar(1.0, 2.0 * std::numbers::pi * rng.uniform());
        for (int k = 0; k < span; ++k) {
            const double shape = step_at(u, k - kPre) - step_at(u, k - kPre - c.length);
            const std::complex<double> g = gain.empty() ? 1.0 : gain[static_cast<std::size_t>(k)];
            x[static_cast<std::size_t>(k)] += a * phase * g * shape;
        }
    }
    std::vector<double> e2(static_cast<std::size_t>(span));
    for (int k = 0; k < span; ++k) e2[static_cast<std::size_t>(k)] = std::norm(x[static_cast<std::size_t>(k)]);
    Sequence out;
    EdgeHistory history;
    for (int j = 0; j <= c.length + 15; ++j) {
        advance(history, j == 0 || j == c.length);
        const TemplateValue tv = template_value(history, chain().tables);
        if (tv.s == 0.0) continue;
        const int k = kPre + c.shift + j;
        double peak = 0.0;
        for (int d = -10; d <= 10; ++d) peak = std::max(peak, e2[static_cast<std::size_t>(k + d)]);
        const double e = std::sqrt(e2[static_cast<std::size_t>(k)]);
        out.push_back({e, tv.s, tv.v, v_prime_fs2(kSigma2, peak, e, tv.s, tv.v, kFloor), j, peak});
    }
    return out;
}

// Runs fn(i) for i = 0 ... n - 1 on every hardware thread; fn must only write its own slot.
template <class Fn>
void parallel_for(int n, Fn fn) {
    unsigned threads = std::thread::hardware_concurrency();
    if (threads == 0) threads = 1;
    std::atomic<int> next{0};
    std::vector<std::thread> pool;
    for (unsigned t = 0; t < threads; ++t) {
        pool.emplace_back([&] {
            for (int i = next++; i < n; i = next++) fn(i);
        });
    }
    for (std::thread& t : pool) t.join();
}

struct Case {
    CaseSpec spec;
    std::vector<Sequence> sequences;
};

std::vector<Case> make_cases(const std::vector<CaseSpec>& specs, int per_case) {
    std::vector<Case> out;
    for (const CaseSpec& s : specs) out.push_back({s, std::vector<Sequence>(static_cast<std::size_t>(per_case))});
    parallel_for(static_cast<int>(specs.size()) * per_case, [&](int i) {
        Case& c = out[static_cast<std::size_t>(i / per_case)];
        c.sequences[static_cast<std::size_t>(i % per_case)] = make_sequence(c.spec, static_cast<std::uint64_t>(i % per_case));
    });
    return out;
}

// The study's 36 cases: noise and -10, -5, 0, +5, +10, +20, +40 dB S500 for each of the four marks, and the 25 WPM dash
// at +20 and +40 dB S500 claimed 4 samples early and late.
std::vector<CaseSpec> study_specs(const std::vector<std::optional<double>>& levels, bool stress) {
    std::vector<CaseSpec> out;
    std::uint64_t seed = 1000;
    const std::vector<std::pair<std::string, int>> marks = {{"dot25", 18}, {"dash25", 54}, {"dot50", 9}, {"dash50", 27}};
    for (const auto& [name, length] : marks) {
        for (const auto& level : levels) {
            ++seed;
            std::ostringstream n;
            n << name << '_';
            if (level) n << std::showpos << *level; else n << "noise";
            out.push_back({n.str(), level, length, 0, seed});
        }
    }
    if (stress) {
        for (const double level : {20.0, 40.0}) {
            for (const int shift : {-4, 4}) {
                ++seed;
                std::ostringstream n;
                n << "dash25_" << std::showpos << level << "_shift" << shift;
                out.push_back({n.str(), level, 54, shift, seed});
            }
        }
    }
    return out;
}

const std::vector<std::optional<double>> kStudyLevels = {std::nullopt, -10.0, -5.0, 0.0, 5.0, 10.0, 20.0, 40.0};
constexpr int kPerCase = 200;  // first marks per case (the brief: at least 200)

// --- The integrals ---

struct Scores {
    std::vector<double> score;  // per sample (nats)
    std::vector<double> sigma;  // sigma_u after each sample (nepers)
    std::vector<double> mean;   // u^ after each sample (nepers)
    int switch_index = -1;      // the first sample with sigma_u < 0.1, else the last
    double revival_at_switch = 0.0;
    double sigma_at_switch = 0.0;
};

// The same integrand on `points` points with ln_i0 and sample_weight at every point (the direct evaluation), trapezoid
// with the ends halved, over [u_lo, u_lo + span_nepers], live range live_nats (+infinity: every point); fixed_weight, if
// set, in place of sample_weight. Stops after the switch when asked.
Scores direct_grid(const Sequence& q, int points, double live_nats, bool stop_at_switch,
                   double span_nepers = kNewbornPriorSpanNepers, std::optional<double> fixed_weight = std::nullopt) {
    const Chain& c = chain();
    const double h = span_nepers / (points - 1);
    std::vector<double> a(static_cast<std::size_t>(points));
    std::vector<double> a2(static_cast<std::size_t>(points));
    for (int i = 0; i < points; ++i) {
        a[static_cast<std::size_t>(i)] = std::exp(c.u_lo + i * h);
        a2[static_cast<std::size_t>(i)] = a[static_cast<std::size_t>(i)] * a[static_cast<std::size_t>(i)];
    }
    std::vector<double> f(static_cast<std::size_t>(points), 0.0);
    int lo = 0;
    int hi = points - 1;
    double ln_z_before = 0.0;
    Scores run;
    const int n = static_cast<int>(q.size());
    for (int k = 0; k < n; ++k) {
        const Observation& o = q[static_cast<std::size_t>(k)];
        const double kappa = 2.0 * std::fabs(o.s) * o.e / o.vp;
        const double lambda = o.s * o.s / o.vp;
        const double q_per_a2 = o.s * o.s / kSigma2;
        double m = -std::numeric_limits<double>::infinity();
        int at = lo;
        for (int i = lo; i <= hi; ++i) {
            const auto ii = static_cast<std::size_t>(i);
            const double w = fixed_weight ? *fixed_weight : sample_weight(a2[ii] * q_per_a2);
            f[ii] += w * (-lambda * a2[ii] + ln_i0(kappa * a[ii]));
            if (f[ii] > m) {
                m = f[ii];
                at = i;
            }
        }
        double s0 = 0.0;
        double s1 = 0.0;
        double s2 = 0.0;
        for (int i = lo; i <= hi; ++i) {
            const double weight = (i == 0 || i == points - 1 ? 0.5 : 1.0) * std::exp(f[static_cast<std::size_t>(i)] - m);
            const double d = (i - at) * h;
            s0 += weight;
            s1 += weight * d;
            s2 += weight * d * d;
        }
        const double ln_z = m + std::log(s0) - std::log(static_cast<double>(points - 1));
        const double mean_d = s1 / s0;
        run.score.push_back(ln_z - ln_z_before);
        run.mean.push_back(c.u_lo + at * h + mean_d);
        run.sigma.push_back(std::sqrt(std::max(0.0, s2 / s0 - mean_d * mean_d)));
        ln_z_before = ln_z;
        while (lo < hi && f[static_cast<std::size_t>(lo)] < m - live_nats) ++lo;
        while (hi > lo && f[static_cast<std::size_t>(hi)] < m - live_nats) --hi;
        if (run.switch_index < 0 && (run.sigma.back() < kSwitchSigmaU || k == n - 1)) {
            run.switch_index = k;
            run.sigma_at_switch = run.sigma.back();
            if (stop_at_switch) break;
        }
    }
    return run;
}

Scores newborn_run(const Sequence& q, double live_nats, bool stop_at_switch, const NewbornTables& t = tables()) {
    NewbornIntegral newborn(t, chain().u_lo, chain().u_lo + t.span_nepers(), t.points(), live_nats);
    Scores run;
    const int n = static_cast<int>(q.size());
    for (int k = 0; k < n; ++k) {
        const Observation& o = q[static_cast<std::size_t>(k)];
        newborn.add(o.e, o.s, o.vp, kSigma2);
        run.score.push_back(newborn.last_score_nats());
        run.sigma.push_back(newborn.sigma_u());
        run.mean.push_back(newborn.mean_u());
        if (run.switch_index < 0 && newborn.should_switch(kSwitchSigmaU, k == n - 1)) {
            run.switch_index = k;
            run.revival_at_switch = newborn.deepest_revival_nats();
            run.sigma_at_switch = newborn.sigma_u();
            if (stop_at_switch) break;
        }
    }
    return run;
}

// Per-sample and per-first-mark differences of a against b through b's switch.
struct Difference {
    double per_sample = 0.0;  // the largest |a_k - b_k| (nats)
    double total = 0.0;       // |sum (a_k - b_k)| (nats)
};
Difference difference(const Scores& a, const Scores& b) {
    Difference d;
    double total = 0.0;
    for (int k = 0; k <= b.switch_index; ++k) {
        const double x = a.score[static_cast<std::size_t>(k)] - b.score[static_cast<std::size_t>(k)];
        d.per_sample = std::max(d.per_sample, std::fabs(x));
        total += x;
    }
    d.total = std::fabs(total);
    return d;
}

// The noise-free first mark of SwitchTimings (part 8): e = a s, V' = sigma_y^2 (1 + q (f^2 s^2 + v)), q = a^2 / sigma_y^2.
Sequence noise_free(double level_db, int length) {
    const double a = amplitude_of(level_db);
    const double q = a * a / kSigma2;
    Sequence out;
    EdgeHistory history;
    for (int j = 0; j <= length + 15; ++j) {
        advance(history, j == 0 || j == length);
        const TemplateValue tv = template_value(history, chain().tables);
        if (tv.s == 0.0) continue;
        out.push_back({a * std::fabs(tv.s), tv.s, tv.v, kSigma2 * (1.0 + q * (kFloor * kFloor * tv.s * tv.s + tv.v)), j,
                       a * a});
    }
    return out;
}

double percentile(std::vector<double> x, double p) {
    if (x.empty()) return std::numeric_limits<double>::quiet_NaN();
    std::sort(x.begin(), x.end());
    const auto i = static_cast<std::size_t>(std::min<double>(x.size() - 1.0, std::floor(p / 100.0 * x.size())));
    return x[i];
}

}  // namespace

TEST(Newborn, MatchesADenseIntegral) {
    // Every per-sample score within 0.005 nats of a 4 001-point trapezoid of the same integrand with direct ln I0 and w,
    // and every first-mark total within 0.015 nats (the study measured 0.0030 + 0.0003 and 0.0093 + 0.0037), through the
    // dense integral's switch, on the study's 36 cases, 200 first marks each.
    const std::vector<Case> cases = make_cases(study_specs(kStudyLevels, true), kPerCase);
    for (const Case& c : cases) {
        std::vector<Difference> d(c.sequences.size());
        parallel_for(static_cast<int>(c.sequences.size()), [&](int i) {
            const Sequence& q = c.sequences[static_cast<std::size_t>(i)];
            d[static_cast<std::size_t>(i)] = difference(newborn_run(q, kNewbornLiveRangeNats, false), direct_grid(q, 4001, INFINITY, true));
        });
        double per_sample = 0.0;
        double total = 0.0;
        for (const Difference& x : d) {
            per_sample = std::max(per_sample, x.per_sample);
            total = std::max(total, x.total);
        }
        std::cout << "measured: " << c.spec.name << ": largest per-sample difference " << per_sample
                  << " nats, largest first-mark total " << total << " nats\n";
        EXPECT_LE(per_sample, 0.005) << c.spec.name;
        EXPECT_LE(total, 0.015) << c.spec.name;
    }
}

TEST(Newborn, DenseReferenceConverged) {
    // The test's own reference: 4 001 against 16 001 points within 1e-5 nats per sample (measured 1.3e-6 in the study),
    // on the first 20 first marks of each of the 36 cases, through the 4 001-point integral's switch.
    const std::vector<Case> cases = make_cases(study_specs(kStudyLevels, true), 20);
    std::vector<double> worst(cases.size() * 20, 0.0);
    parallel_for(static_cast<int>(worst.size()), [&](int i) {
        const Sequence& q = cases[static_cast<std::size_t>(i / 20)].sequences[static_cast<std::size_t>(i % 20)];
        const Scores r4 = direct_grid(q, 4001, INFINITY, true);
        const Scores r16 = direct_grid(q, 16001, INFINITY, true);
        worst[static_cast<std::size_t>(i)] = difference(r16, r4).per_sample;
    });
    const double largest = *std::max_element(worst.begin(), worst.end());
    std::cout << "measured: 4 001 against 16 001 points, largest per-sample difference " << largest << " nats\n";
    EXPECT_LE(largest, 1e-5);
}

TEST(Newborn, LiveRangeLosesNothing) {
    // The live range at 80 nats equals the full 128-point grid within 1e-9 nats per sample, on every sample through the
    // switch (where the newborn hands over; after it, on a misaligned story, points dropped long before would come back),
    // on the study's set and on the misaligned stress cases, where points revive from 41.5 nats down (the study's deepest
    // revival, on the 25 WPM dash at +40 dB S500 claimed 4 samples early): the deepest revival at handover over the stress
    // cases is 41.5 +/- 1 nats.
    const std::vector<Case> cases = make_cases(study_specs(kStudyLevels, true), kPerCase);
    double stress_revival = 0.0;
    double other_revival = 0.0;
    for (const Case& c : cases) {
        double worst = 0.0;
        double revival = 0.0;
        for (const Sequence& q : c.sequences) {
            const Scores live = newborn_run(q, kNewbornLiveRangeNats, false);
            const Scores full = newborn_run(q, INFINITY, false);
            ASSERT_GE(live.switch_index, 0);
            EXPECT_EQ(live.switch_index, full.switch_index);
            for (int k = 0; k <= full.switch_index; ++k)
                worst = std::max(worst, std::fabs(live.score[static_cast<std::size_t>(k)] - full.score[static_cast<std::size_t>(k)]));
            revival = std::max(revival, live.revival_at_switch);
        }
        EXPECT_LE(worst, 1e-9) << c.spec.name;
        double& deepest = c.spec.shift != 0 ? stress_revival : other_revival;
        deepest = std::max(deepest, revival);
        std::cout << "measured: " << c.spec.name << ": live against full " << worst << " nats; deepest revival at handover "
                  << revival << " nats\n";
    }
    std::cout << "measured: deepest revival, stress cases " << stress_revival << " nats, others " << other_revival
              << " nats\n";
    EXPECT_NEAR(stress_revival, 41.5, 1.0);
}

TEST(Newborn, TablesMatchDirectEvaluation) {
    // G against ln_i0 within 1e-6 nats over x from 1e-3 to 1e6 (40 001 points even in ln x); the shifted G and w tables
    // (w at steps of h, Ruling 81) against ln_i0 and the xi table at the same 128 points (the same live range) within
    // 5e-4 nats per sample and 0.005 nats per first mark (the study measured 3.1e-4 and 0.0037 with w at 2h), through the
    // direct grid's switch, on the 32 level cases and the misaligned stress cases (Ruling 81; with w at 2h the 25 WPM dash
    // at +20 dB S500 claimed 4 samples early reached 7.0e-4 nats per sample).
    double worst_g = 0.0;
    for (int i = 0; i <= 40000; ++i) {
        const double x = std::exp(std::log(1e-3) + i * (std::log(1e6) - std::log(1e-3)) / 40000.0);
        worst_g = std::max(worst_g, std::fabs(tables().ln_i0_from_table(x) - ln_i0(x)));
    }
    std::cout << "measured: G table against ln_i0 over x of 1e-3 to 1e6: " << worst_g << " nats\n";
    EXPECT_LE(worst_g, 1e-6);
    // The w table against sample_weight (linear interpolation in ln q at h = 0.0906; Ruling 81), reported.
    double worst_w = 0.0;
    for (int i = 0; i <= 4000; ++i) {
        const double ln_q = -12.0 + i * 24.0 / 4000.0;
        worst_w = std::max(worst_w, std::fabs(tables().weight_from_table(ln_q) - sample_weight(std::exp(ln_q))));
    }
    std::cout << "measured: w table against sample_weight over ln q of -12 to +12: " << worst_w << "\n";
    const std::vector<Case> cases = make_cases(study_specs(kStudyLevels, true), kPerCase);
    for (const Case& c : cases) {
        std::vector<Difference> d(c.sequences.size());
        parallel_for(static_cast<int>(c.sequences.size()), [&](int i) {
            const Sequence& q = c.sequences[static_cast<std::size_t>(i)];
            d[static_cast<std::size_t>(i)] =
                difference(newborn_run(q, kNewbornLiveRangeNats, false), direct_grid(q, kNewbornGridPoints, kNewbornLiveRangeNats, true));
        });
        double per_sample = 0.0;
        double total = 0.0;
        for (const Difference& x : d) {
            per_sample = std::max(per_sample, x.per_sample);
            total = std::max(total, x.total);
        }
        std::cout << "measured: " << c.spec.name << ": tables against direct, per sample " << per_sample
                  << " nats, per first mark " << total << " nats\n";
        EXPECT_LE(per_sample, 5e-4) << c.spec.name;
        EXPECT_LE(total, 0.005) << c.spec.name;
    }
}

TEST(Newborn, StaysBoundedOnNoise) {
    // 10 000 noise first marks of a 25 WPM dash: every score finite; every per-sample score at most 1.387 times its cap
    // w s^2 / (f^2 s^2 + v) at w = 0.1986 (part 9; 1.387 = 0.2754 / 0.1986, the weak limit's w), the first mark's total at
    // most the sum of the caps, and the posterior mean at the last sample below 0 dB S500 (the study: at most -3.5 dB S500
    // over 4 000, median -10 to -13 dB S500).
    const CaseSpec spec{"dash25_noise", std::nullopt, 54, 0, 77};
    std::vector<double> largest_ratio(10000, 0.0);
    std::vector<double> total_margin(10000, 0.0);
    std::vector<double> last_mean_db(10000, 0.0);
    std::vector<double> largest_score(10000, 0.0);
    std::vector<int> finite(10000, 1);
    std::vector<int> scored(10000, 0);
    parallel_for(10000, [&](int i) {
        const Sequence q = make_sequence(spec, static_cast<std::uint64_t>(i));
        NewbornIntegral newborn(tables(), chain().u_lo, chain().u_hi, kNewbornGridPoints, kNewbornLiveRangeNats);
        double total = 0.0;
        double caps = 0.0;
        double ratio = 0.0;
        double top = -INFINITY;
        for (const Observation& o : q) {
            newborn.add(o.e, o.s, o.vp, kSigma2);
            const double score = newborn.last_score_nats();
            const double cap = score_cap_nats(kBroadPriorLowestW, o.s, o.v, kFloor);
            if (!std::isfinite(score)) finite[static_cast<std::size_t>(i)] = 0;
            ratio = std::max(ratio, score / cap);
            top = std::max(top, score);
            total += score;
            caps += cap;
        }
        largest_ratio[static_cast<std::size_t>(i)] = ratio;
        total_margin[static_cast<std::size_t>(i)] = caps - total;
        last_mean_db[static_cast<std::size_t>(i)] = level_of_u(newborn.mean_u());
        largest_score[static_cast<std::size_t>(i)] = top;
        scored[static_cast<std::size_t>(i)] = static_cast<int>(q.size());
    });
    EXPECT_EQ(std::count(finite.begin(), finite.end(), 0), 0);
    std::cout << "measured: " << scored[0] << " scored samples per first mark (j = 0 ... 69; s_0 = " << chain().tables.s[0]
              << "); largest score/cap "
              << *std::max_element(largest_ratio.begin(), largest_ratio.end()) << ", largest per-sample score "
              << *std::max_element(largest_score.begin(), largest_score.end()) << " nats; smallest (caps - total) "
              << *std::min_element(total_margin.begin(), total_margin.end()) << " nats; posterior mean at the last sample: "
              << "largest " << *std::max_element(last_mean_db.begin(), last_mean_db.end()) << " dB S500, median "
              << percentile(last_mean_db, 50) << " dB S500\n";
    EXPECT_LE(*std::max_element(largest_ratio.begin(), largest_ratio.end()), 1.387);
    EXPECT_GE(*std::min_element(total_margin.begin(), total_margin.end()), 0.0);
    EXPECT_LT(*std::max_element(last_mean_db.begin(), last_mean_db.end()), 0.0);
}

TEST(Newborn, SwitchAgreesWithDense) {
    // The switch sample (sigma_u < 0.1 from the grid's moments, else the last) equals the dense integral's on at least
    // 99.5% of first marks and is never more than one sample away (the study: none earlier, 0.1% later).
    const std::vector<Case> cases = make_cases(study_specs(kStudyLevels, true), kPerCase);
    int same = 0;
    int earlier = 0;
    int later = 0;
    int count = 0;
    int farthest = 0;
    for (const Case& c : cases) {
        std::vector<int> delta(c.sequences.size(), 0);
        parallel_for(static_cast<int>(c.sequences.size()), [&](int i) {
            const Sequence& q = c.sequences[static_cast<std::size_t>(i)];
            delta[static_cast<std::size_t>(i)] =
                newborn_run(q, kNewbornLiveRangeNats, true).switch_index - direct_grid(q, 4001, INFINITY, true).switch_index;
        });
        for (const int d : delta) {
            ++count;
            same += d == 0;
            earlier += d < 0;
            later += d > 0;
            farthest = std::max(farthest, std::abs(d));
        }
    }
    std::cout << "measured: switch against the dense integral's over " << count << " first marks: same " << same
              << ", earlier " << earlier << ", later " << later << ", farthest " << farthest << " samples\n";
    EXPECT_GE(same, 0.995 * count);
    EXPECT_LE(farthest, 1);
}

TEST(Newborn, SwitchTimings) {
    // Noise-free (e = a s, V' = sigma_y^2 (1 + q (f^2 s^2 + v))) on a 25 WPM dash. Part 8's table (derived from the
    // high-SNR curvature, sigma_u = 1 / sqrt(2 sum_k w s_k^2 a^2 / V'_k), w = 0.1986, counting from j = 5, the first sample
    // with s >= 0.01): the switch at j = 9 (+30 and +20 dB S500), 12 (+10) and 46 (0 dB S500). Checked: (1) the test's
    // own evaluation of that formula reproduces the table within 1 sample; (2) the grid switches at the dense integral's
    // sample; (3) the grid's switch is within 1 sample of the table at +30, +20 and +10 dB S500; at 0 dB S500 the integral
    // switches at j = 56 (recomputed, not the table's 46): at +8.3 dB SNR in the filter's 74.48 Hz per sample the high-SNR
    // curvature overstates a sample's information about u (the study's noisy first marks: median j = 52 there).
    const std::vector<std::tuple<double, int, int>> levels = {{30.0, 9, 9}, {20.0, 9, 9}, {10.0, 12, 12}, {0.0, 46, 56}};
    for (const auto& [level, table, integral] : levels) {
        const Sequence q = noise_free(level, 54);
        const Scores run = newborn_run(q, kNewbornLiveRangeNats, true);
        const Scores dense = direct_grid(q, 4001, INFINITY, true);
        const int j = q[static_cast<std::size_t>(run.switch_index)].j;
        const double a = amplitude_of(level);
        double information = 0.0;  // 1 / nepers^2
        int formula_j = -1;
        for (const Observation& o : q) {
            if (o.j < 5) continue;
            information += 2.0 * kBroadPriorLowestW * o.s * o.s * a * a / o.vp;
            if (1.0 / std::sqrt(information) < kSwitchSigmaU) {
                formula_j = o.j;
                break;
            }
        }
        std::cout << "measured: " << level << " dB S500: the grid switches at j = " << j << " (sigma_u "
                  << run.sigma_at_switch << ", the sample before "
                  << (run.switch_index > 0 ? run.sigma[static_cast<std::size_t>(run.switch_index - 1)] : 0.0)
                  << "), the dense integral at j = " << q[static_cast<std::size_t>(dense.switch_index)].j
                  << ", part 8's formula at j = " << formula_j << "\n";
        EXPECT_NEAR(formula_j, table, 1) << level << " dB S500";
        EXPECT_EQ(run.switch_index, dense.switch_index) << level << " dB S500";
        EXPECT_NEAR(j, integral, 1) << level << " dB S500";
        EXPECT_LT(run.sigma_at_switch, kSwitchSigmaU);
    }
}

TEST(Newborn, StrongEdgeOfThePrior) {
    // A +60 dB S500 first mark (25 dB, 2.9 nepers, inside the prior's top): scores finite and within 0.005 nats per sample
    // of the dense integral. A +85 dB S500 first mark (at the top): scores finite; its differences from the dense integral
    // are reported, not bounded (the end-point term of Euler-Maclaurin, about h^2 |F'(u_hi)| / 12, derived). The
    // narrowest sigma_u at handover is reported for both.
    for (const double level : {60.0, 85.0}) {
        const std::vector<Case> cases = make_cases(study_specs({level}, false), 50);
        double per_sample = 0.0;
        double total = 0.0;
        double narrowest = INFINITY;
        bool all_finite = true;
        for (const Case& c : cases) {
            std::vector<Difference> d(c.sequences.size());
            std::vector<double> sigma(c.sequences.size());
            std::vector<int> fin(c.sequences.size(), 1);
            parallel_for(static_cast<int>(c.sequences.size()), [&](int i) {
                const Sequence& q = c.sequences[static_cast<std::size_t>(i)];
                const Scores grid = newborn_run(q, kNewbornLiveRangeNats, false);
                for (const double s : grid.score)
                    if (!std::isfinite(s)) fin[static_cast<std::size_t>(i)] = 0;
                d[static_cast<std::size_t>(i)] = difference(grid, direct_grid(q, 4001, INFINITY, true));
                sigma[static_cast<std::size_t>(i)] = grid.sigma_at_switch;
            });
            for (std::size_t i = 0; i < d.size(); ++i) {
                per_sample = std::max(per_sample, d[i].per_sample);
                total = std::max(total, d[i].total);
                narrowest = std::min(narrowest, sigma[i]);
                all_finite = all_finite && fin[i] == 1;
            }
        }
        std::cout << "measured: +" << level << " dB S500: largest per-sample difference " << per_sample
                  << " nats, largest first-mark total " << total << " nats, narrowest sigma_u at handover " << narrowest
                  << " nepers\n";
        EXPECT_TRUE(all_finite);
        if (level == 60.0) EXPECT_LE(per_sample, 0.005);
    }
}

TEST(Newborn, LatestSwitchAtJ15AfterTheKeyUp) {
    // At -10 dB S500 a 5-sample mark switches exactly at the 16th sample after its claimed key-up (j = 15 after it, 20
    // after the key-down), the caller flagging that sample from its EdgeHistory: never earlier, on 50 first marks.
    const CaseSpec spec{"mark5_-10", -10.0, 5, 0, 91};
    for (std::uint64_t index = 0; index < 50; ++index) {
        const Sequence q = make_sequence(spec, index);
        NewbornIntegral newborn(tables(), chain().u_lo, chain().u_hi, kNewbornGridPoints, kNewbornLiveRangeNats);
        EdgeHistory history;
        std::size_t next = 0;
        int switched_at = -1;
        for (int j = 0; j <= 20 && switched_at < 0; ++j) {
            advance(history, j == 0 || j == 5);
            if (next >= q.size() || q[next].j != j) continue;  // s = 0: not scored
            const Observation& o = q[next++];
            newborn.add(o.e, o.s, o.vp, kSigma2);
            // The last scored sample of the first mark: its key-up 15 samples back, the edge at bit 15 of the history
            // being a key-up (the state before it, key_down_before, down).
            const bool last = (history.recent & (1u << 15)) != 0 && history.key_down_before;
            if (newborn.should_switch(kSwitchSigmaU, last)) switched_at = j;
        }
        EXPECT_EQ(switched_at, 20) << "first mark " << index;
    }
}

TEST(Newborn, NegativeTemplateValues) {
    // Samples with s = -0.00064 and -0.00080 (s_3 and s_4, the channel filter's precursor) score as |s| would, within
    // 1e-12 nats, after a strong rise (+40 dB S500) and alone.
    for (const bool after_rise : {false, true}) {
        NewbornIntegral negative(tables(), chain().u_lo, chain().u_hi, kNewbornGridPoints, kNewbornLiveRangeNats);
        NewbornIntegral positive = negative;
        if (after_rise) {
            for (const Observation& o : noise_free(40.0, 54)) {
                if (o.j > 8) break;
                negative.add(o.e, o.s, o.vp, kSigma2);
                positive.add(o.e, o.s, o.vp, kSigma2);
            }
        }
        const double a = amplitude_of(40.0);
        for (const double s : {-0.00064, -0.00080}) {
            const double e = 0.7 + a * std::fabs(s);
            const double vp = v_prime_fs2(kSigma2, a * a, e, s, 1e-6, kFloor);
            negative.add(e, s, vp, kSigma2);
            positive.add(e, -s, vp, kSigma2);
            EXPECT_NEAR(negative.last_score_nats(), positive.last_score_nats(), 1e-12) << "s = " << s;
            EXPECT_NE(negative.last_score_nats(), 0.0);
        }
        EXPECT_NEAR(negative.mean_u(), positive.mean_u(), 1e-12);
    }
}

namespace {

// The score of one sample (nats) under a log-normal prior on a, u = ln a ~ N(u_mean, u_sigma^2), with the constant
// weight w: ln of the integral over u of N(u) exp(w Lambda(e^u)), by the trapezoid rule over u_mean +/- 12 u_sigma
// (40 001 points).
double log_normal_score(const Observation& o, double u_mean, double u_sigma, double w) {
    constexpr int kPoints = 40001;
    const double kappa = 2.0 * std::fabs(o.s) * o.e / o.vp;
    const double lambda = o.s * o.s / o.vp;
    std::vector<double> v(kPoints);
    double m = -INFINITY;
    for (int i = 0; i < kPoints; ++i) {
        const double z = -12.0 + 24.0 * i / (kPoints - 1);
        const double a = std::exp(u_mean + u_sigma * z);
        v[static_cast<std::size_t>(i)] = -0.5 * z * z + w * (-lambda * a * a + ln_i0(kappa * a));
        m = std::max(m, v[static_cast<std::size_t>(i)]);
    }
    double sum = 0.0;
    for (int i = 0; i < kPoints; ++i)
        sum += (i == 0 || i == kPoints - 1 ? 0.5 : 1.0) * std::exp(v[static_cast<std::size_t>(i)] - m);
    return m + std::log(sum * 24.0 / (kPoints - 1) / std::sqrt(2.0 * std::numbers::pi));
}

}  // namespace

TEST(Newborn, NarrowestSigmaUIsTheSmallestSoFar) {
    // narrowest_sigma_u() is +infinity before any sample and then the smallest sigma_u() after any sample so far, also on
    // samples past the switch where sigma_u() widens again (a 25 WPM dash at +20 dB S500 claimed 4 samples late: the
    // story's template stays up 4 samples after the true key-up), checked on every sample of 20 first marks of each case.
    const std::vector<Case> cases = make_cases({{"dash25_+20_shift+4", 20.0, 54, 4, 611},
                                                {"dash25_+10", 10.0, 54, 0, 612},
                                                {"dot25_noise", std::nullopt, 18, 0, 613}},
                                               20);
    int widened = 0;
    for (const Case& c : cases) {
        for (const Sequence& q : c.sequences) {
            NewbornIntegral newborn(tables(), chain().u_lo, chain().u_hi, kNewbornGridPoints, kNewbornLiveRangeNats);
            EXPECT_EQ(newborn.narrowest_sigma_u(), std::numeric_limits<double>::infinity());
            double smallest = std::numeric_limits<double>::infinity();
            for (const Observation& o : q) {
                newborn.add(o.e, o.s, o.vp, kSigma2);
                widened += newborn.sigma_u() > smallest;
                smallest = std::min(smallest, newborn.sigma_u());
                ASSERT_EQ(newborn.narrowest_sigma_u(), smallest) << c.spec.name;
            }
        }
    }
    std::cout << "measured: samples where sigma_u widened again: " << widened << "\n";
    EXPECT_GT(widened, 0);  // the counter is not just the current sigma_u
}

TEST(Newborn, FixedWeightIsConstantAtEveryPoint) {
    // The fixed-weight variant (O12; Task 12): tables built with fixed_weight = w_strong read that constant at every
    // ln q, and the integral with them matches the direct grid with that constant w at every point within the tables'
    // 5e-4 nats per sample; it differs from the curve's integral (w varies by point there).
    const double w = chain().noise.w_strong;
    const NewbornTables fixed(chain().noise, chain().noise.sigma2_per_n0_hz, w);
    ASSERT_TRUE(fixed.fixed_weight().has_value());
    for (int i = 0; i <= 4000; ++i) EXPECT_EQ(fixed.weight_from_table(-40.0 + i * 80.0 / 4000.0), w);
    const std::vector<Case> cases = make_cases({{"dash25_+0", 0.0, 54, 0, 621},
                                                {"dash25_noise", std::nullopt, 54, 0, 622},
                                                {"dot50_+20", 20.0, 9, 0, 623}},
                                               50);
    double worst = 0.0;
    double from_curve = 0.0;
    for (const Case& c : cases) {
        for (const Sequence& q : c.sequences) {
            const Scores grid = newborn_run(q, kNewbornLiveRangeNats, false, fixed);
            const Scores direct =
                direct_grid(q, kNewbornGridPoints, kNewbornLiveRangeNats, false, kNewbornPriorSpanNepers, w);
            const Scores curve = newborn_run(q, kNewbornLiveRangeNats, false);
            for (std::size_t k = 0; k < q.size(); ++k) {
                worst = std::max(worst, std::fabs(grid.score[k] - direct.score[k]));
                from_curve = std::max(from_curve, std::fabs(grid.score[k] - curve.score[k]));
            }
        }
    }
    std::cout << "measured: fixed w = " << w << ": against the direct grid " << worst
              << " nats per sample; against the curve's integral up to " << from_curve << " nats\n";
    EXPECT_LE(worst, 5e-4);
    EXPECT_GT(from_curve, 1e-3);
}

TEST(Newborn, OtherGridsMatchTheirDirectEvaluation) {
    // A grid other than the default: 200 points over a prior of 120 dB (13.82 nepers, h = 0.0694 nepers), whose G table
    // reaches below -20 nepers of ln x. Against the direct evaluation on the same grid within 5e-4 nats per sample, and
    // against a 4 001-point dense integral over the same prior within 0.005 nats per sample, on 50 first marks of noise,
    // 0 and +40 dB S500.
    const double span = 6.0 * std::numbers::ln10;  // 120 dB of amplitude, nepers
    const NewbornTables other(chain().noise, chain().noise.sigma2_per_n0_hz, std::nullopt, 200, span);
    EXPECT_NEAR(other.step_nepers(), span / 199.0, 1e-15);
    const std::vector<Case> cases = make_cases({{"dash25_noise", std::nullopt, 54, 0, 631},
                                                {"dash25_+0", 0.0, 54, 0, 632},
                                                {"dash25_+40", 40.0, 54, 0, 633}},
                                               50);
    double worst_direct = 0.0;
    double worst_dense = 0.0;
    for (const Case& c : cases) {
        std::vector<Difference> direct(c.sequences.size());
        std::vector<Difference> dense(c.sequences.size());
        parallel_for(static_cast<int>(c.sequences.size()), [&](int i) {
            const Sequence& q = c.sequences[static_cast<std::size_t>(i)];
            const Scores grid = newborn_run(q, kNewbornLiveRangeNats, false, other);
            direct[static_cast<std::size_t>(i)] = difference(grid, direct_grid(q, 200, kNewbornLiveRangeNats, true, span));
            dense[static_cast<std::size_t>(i)] = difference(grid, direct_grid(q, 4001, INFINITY, true, span));
        });
        for (std::size_t i = 0; i < c.sequences.size(); ++i) {
            worst_direct = std::max(worst_direct, direct[i].per_sample);
            worst_dense = std::max(worst_dense, dense[i].per_sample);
        }
    }
    std::cout << "measured: 200 points over 120 dB: against the direct grid " << worst_direct
              << " nats per sample, against the dense integral " << worst_dense << " nats per sample\n";
    EXPECT_LE(worst_direct, 5e-4);
    EXPECT_LE(worst_dense, 0.005);
}

TEST(Newborn, RefusesBadArguments) {
    // The constructors' and add's checks (newborn.hpp); +infinity is a valid live range.
    const OutputNoise& noise = chain().noise;
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();
    EXPECT_THROW(NewbornTables(noise, 0.0, std::nullopt), std::invalid_argument);
    EXPECT_THROW(NewbornTables(noise, nan, std::nullopt), std::invalid_argument);
    EXPECT_THROW(NewbornTables(noise, inf, std::nullopt), std::invalid_argument);
    OutputNoise bad = noise;
    bad.sigma2_per_n0_hz = -1.0;
    EXPECT_THROW(NewbornTables(bad, noise.sigma2_per_n0_hz, std::nullopt), std::invalid_argument);
    for (const double w : {0.0, -0.2, nan, inf})
        EXPECT_THROW(NewbornTables(noise, noise.sigma2_per_n0_hz, w), std::invalid_argument) << "w = " << w;
    EXPECT_THROW(NewbornTables(noise, noise.sigma2_per_n0_hz, std::nullopt, 1), std::invalid_argument);
    for (const double span : {0.0, -1.0, nan, inf})
        EXPECT_THROW(NewbornTables(noise, noise.sigma2_per_n0_hz, std::nullopt, 128, span), std::invalid_argument);
    EXPECT_THROW((void)tables().ln_i0_from_table(0.0), std::invalid_argument);
    EXPECT_THROW((void)tables().ln_i0_from_table(nan), std::invalid_argument);

    const double lo = chain().u_lo;
    const double hi = chain().u_hi;
    const NewbornTables& t = tables();
    EXPECT_THROW(NewbornIntegral(t, nan, hi, 128, 80.0), std::invalid_argument);
    EXPECT_THROW(NewbornIntegral(t, lo, inf, 128, 80.0), std::invalid_argument);
    EXPECT_THROW(NewbornIntegral(t, hi, lo, 128, 80.0), std::invalid_argument);
    EXPECT_THROW(NewbornIntegral(t, lo, hi, 127, 80.0), std::invalid_argument);         // not the tables' points
    EXPECT_THROW(NewbornIntegral(t, lo, hi + 0.01, 128, 80.0), std::invalid_argument);  // not the tables' span
    for (const double live : {0.0, -1.0, nan})
        EXPECT_THROW(NewbornIntegral(t, lo, hi, 128, live), std::invalid_argument) << "live = " << live;
    EXPECT_NO_THROW(NewbornIntegral(t, lo, hi, 128, inf));

    NewbornIntegral newborn(t, lo, hi, 128, 80.0);
    EXPECT_THROW(newborn.add(-0.1, 0.5, 1.0, 1.0), std::invalid_argument);
    EXPECT_THROW(newborn.add(nan, 0.5, 1.0, 1.0), std::invalid_argument);
    EXPECT_THROW(newborn.add(inf, 0.5, 1.0, 1.0), std::invalid_argument);
    EXPECT_THROW(newborn.add(1.0, nan, 1.0, 1.0), std::invalid_argument);
    EXPECT_THROW(newborn.add(1.0, inf, 1.0, 1.0), std::invalid_argument);
    for (const double v : {0.0, -1.0, nan, inf}) {
        EXPECT_THROW(newborn.add(1.0, 0.5, v, 1.0), std::invalid_argument) << "V' = " << v;
        EXPECT_THROW(newborn.add(1.0, 0.5, 1.0, v), std::invalid_argument) << "sigma_y^2 = " << v;
    }
    // The refused calls changed nothing; s = 0 scores 0 and changes nothing either.
    EXPECT_EQ(newborn.ln_z(), 0.0);
    const double mean = newborn.mean_u();
    newborn.add(1.0, 0.0, 1.0, 1.0);
    EXPECT_EQ(newborn.last_score_nats(), 0.0);
    EXPECT_EQ(newborn.mean_u(), mean);
    EXPECT_EQ(newborn.narrowest_sigma_u(), inf);
}

TEST(Newborn, MomentMatching) {
    // to_gaussian: mu = e^(u^ + sigma_u^2 / 2), P = mu^2 (e^(sigma_u^2) - 1) from the grid's u^ and sigma_u, to 1e-12
    // (relative), and t_m = k. Approximation (c), the log-normal replaced by its matched Gaussian: the next sample's score
    // from N(mu, P) (sample_evidence, order 5, w at mu, as an ordinary story scores it) against its score under the
    // log-normal of the same u^ and sigma_u (a dense integral, the same w), at the switch with the grid's sigma_u and with
    // sigma_u = 0.1 exactly. The derived 0.008 nats, (3 sigma_u)^2 / 12 at sigma_u = 0.1, is the expected cost (the
    // second-order Edgeworth term, the Kullback-Leibler divergence of the Gaussian from the log-normal); a single sample's
    // difference is first order in the skewness gamma = 3 sigma_u, about (gamma / 6)(z^3 - 3 z) at z standard deviations
    // from the predictive's center. So: noise-free at +10, +20, +30 and +40 dB S500 (the sample at the center) every
    // difference at most 0.01 nats; on 200 first marks at +10 and +20 dB S500 the mean difference at most 0.01 nats, the
    // largest reported. Also reported: the grid's own next score against the matched Gaussian's (the posterior at handover
    // is not exactly log-normal).
    std::vector<Sequence> sequences = {noise_free(10.0, 54), noise_free(20.0, 54), noise_free(30.0, 54),
                                       noise_free(40.0, 54)};
    constexpr std::size_t kNoiseFree = 4;
    for (const Case& c : make_cases({{"dash25_+10", 10.0, 54, 0, 501}, {"dash25_+20", 20.0, 54, 0, 502}}, 100))
        for (const Sequence& q : c.sequences) sequences.push_back(q);
    std::vector<double> c_grid_sigma;  // S_N - S_LN at the grid's sigma_u
    std::vector<double> c_at_01;       // S_N - S_LN at sigma_u = 0.1
    std::vector<double> grid_minus_n;  // the grid's score - S_N
    double sigma_lo = INFINITY;
    double sigma_hi = 0.0;
    for (const Sequence& q : sequences) {
        NewbornIntegral newborn(tables(), chain().u_lo, chain().u_hi, kNewbornGridPoints, kNewbornLiveRangeNats);
        for (std::size_t k = 0; k + 1 < q.size(); ++k) {
            newborn.add(q[k].e, q[k].s, q[k].vp, kSigma2);
            if (!newborn.should_switch(kSwitchSigmaU, false)) continue;
            const Amplitude g = newborn.to_gaussian(static_cast<std::int64_t>(k));
            const double u = newborn.mean_u();
            const double sigma = newborn.sigma_u();
            const double s2 = sigma * sigma;
            const double mu = std::exp(u + s2 / 2.0);
            EXPECT_NEAR(g.mu_fs, mu, 1e-12 * mu);
            EXPECT_NEAR(g.p_fs2, mu * mu * (std::exp(s2) - 1.0), 1e-12 * mu * mu * (std::exp(s2) - 1.0));
            EXPECT_EQ(g.t_m_sample, static_cast<std::int64_t>(k));
            sigma_lo = std::min(sigma_lo, sigma);
            sigma_hi = std::max(sigma_hi, sigma);
            const Observation& o = q[k + 1];
            const double w = sample_weight(g.mu_fs * g.mu_fs * o.s * o.s / kSigma2);
            const double gaussian = sample_evidence(o.e, o.s, g.mu_fs, g.p_fs2, o.vp, w, 5).score_nats;
            c_grid_sigma.push_back(gaussian - log_normal_score(o, u, sigma, w));
            // The same at sigma_u = 0.1 exactly, about the same u^.
            const double mu_01 = std::exp(u + 0.005);
            const double p_01 = mu_01 * mu_01 * std::expm1(0.01);
            const double w_01 = sample_weight(mu_01 * mu_01 * o.s * o.s / kSigma2);
            c_at_01.push_back(sample_evidence(o.e, o.s, mu_01, p_01, o.vp, w_01, 5).score_nats -
                              log_normal_score(o, u, 0.1, w_01));
            newborn.add(o.e, o.s, o.vp, kSigma2);
            grid_minus_n.push_back(newborn.last_score_nats() - gaussian);
            break;
        }
    }
    ASSERT_EQ(c_grid_sigma.size(), sequences.size());
    const auto largest_abs = [](const std::vector<double>& x, std::size_t from, std::size_t to) {
        double m = 0.0;
        for (std::size_t i = from; i < to; ++i) m = std::max(m, std::fabs(x[i]));
        return m;
    };
    const auto mean = [](const std::vector<double>& x, std::size_t from, std::size_t to) {
        double s = 0.0;
        for (std::size_t i = from; i < to; ++i) s += x[i];
        return s / static_cast<double>(to - from);
    };
    const std::size_t n = sequences.size();
    std::cout << "measured: next sample after the switch (sigma_u " << sigma_lo << " to " << sigma_hi << " nepers)\n"
              << "measured:   matched Gaussian - log-normal, noise-free at +10, +20, +30, +40 dB S500: " << c_grid_sigma[0]
              << ", " << c_grid_sigma[1] << ", " << c_grid_sigma[2] << ", " << c_grid_sigma[3] << " nats (at sigma_u = 0.1: "
              << c_at_01[0] << ", " << c_at_01[1] << ", " << c_at_01[2] << ", " << c_at_01[3] << ")\n"
              << "measured:   on 200 noisy first marks: mean " << mean(c_grid_sigma, kNoiseFree, n) << " nats, largest |.| "
              << largest_abs(c_grid_sigma, kNoiseFree, n) << " nats (at sigma_u = 0.1: mean " << mean(c_at_01, kNoiseFree, n)
              << ", largest |.| " << largest_abs(c_at_01, kNoiseFree, n) << ")\n"
              << "measured:   the grid - matched Gaussian: noise-free " << grid_minus_n[0] << ", " << grid_minus_n[1] << ", "
              << grid_minus_n[2] << ", " << grid_minus_n[3] << " nats; noisy mean " << mean(grid_minus_n, kNoiseFree, n)
              << ", largest |.| " << largest_abs(grid_minus_n, kNoiseFree, n) << " nats\n";
    EXPECT_LE(largest_abs(c_grid_sigma, 0, kNoiseFree), 0.01);
    EXPECT_LE(largest_abs(c_at_01, 0, kNoiseFree), 0.01);
    EXPECT_LE(std::fabs(mean(c_grid_sigma, kNoiseFree, n)), 0.01);
    EXPECT_LE(std::fabs(mean(c_at_01, kNoiseFree, n)), 0.01);
}

namespace {

bool measurement_requested() {
    const char* flag = std::getenv("KZ4AP_MEASUREMENT");
    return flag != nullptr && std::string(flag) == "1";
}

// The "integral over the fading path" of approximation (b), on the outer grid in u of `points` points (the prior, flat in
// u, trapezoid): for each level Omega = e^(2 u_i) (FS^2) the amplitude a = x sqrt(Omega) follows the decoder's own
// amplitude model (part 6) from a Rayleigh start, p(x) = 2 x e^(-x^2), x on [0, 5] in steps of 0.004; between samples
// lag apart the transition is N(rho_a x + (1 - rho_a) 0.886, (1/2)(1 - rho^2)) in x (P' = rho_a^2 P + (1/2)(1 - rho^2)
// Omega_bar from P = 0, Omega_bar = Omega), truncated to x >= 0 and renormalized on the grid; each sample multiplies by
// exp(w(q) Lambda(a)) with the direct ln_i0 and sample_weight. Levels more than 80 nats below the best at the range's
// ends are dropped, as the newborn's live range does. Returns ln Z through each sample up to `through`.
std::vector<double> fading_path_ln_z(const Sequence& q, int points, const FadingTables& fading, int through) {
    const Chain& c = chain();
    constexpr double kStep = 0.004;
    constexpr int kInner = 1251;  // x = 0 ... 5
    constexpr double kRayleighMean = 0.886226925452758;  // sqrt(pi) / 2: E[x] (derived)
    const double h = (c.u_hi - c.u_lo) / (points - 1);
    std::vector<double> start(kInner);
    for (int m = 0; m < kInner; ++m) {
        const double x = m * kStep;
        start[static_cast<std::size_t>(m)] = 2.0 * x * std::exp(-x * x) * kStep;
    }
    std::vector<std::vector<double>> belief(static_cast<std::size_t>(points), start);  // normalized per level
    std::vector<double> log_scale(static_cast<std::size_t>(points), 0.0);              // ln of each level's mass
    int lo = 0;
    int hi = points - 1;
    std::vector<double> out;
    std::vector<double> moved(kInner);
    std::vector<double> term(kInner);
    int last_j = 0;
    for (int k = 0; k <= through; ++k) {
        const Observation& o = q[static_cast<std::size_t>(k)];
        const int lag = k == 0 ? 0 : o.j - last_j;
        last_j = o.j;
        // The transition kernel in x for this lag (none at lag 0), [source m][d + reach], each row summing to 1.
        const double rho = fading.rho(lag);
        const double rho_a = fading.rho_a(lag);
        const double spread = std::sqrt(std::max(0.0, 0.5 * (1.0 - rho * rho)));
        const int reach = spread > 0.0 ? static_cast<int>(std::ceil(6.0 * spread / kStep)) : 0;
        std::vector<std::vector<double>> kernel;
        std::vector<int> center_index;
        if (lag > 0 && reach > 0) {
            kernel.assign(kInner, std::vector<double>(static_cast<std::size_t>(2 * reach + 1), 0.0));
            center_index.assign(kInner, 0);
            for (int m = 0; m < kInner; ++m) {
                const double center = rho_a * m * kStep + (1.0 - rho_a) * kRayleighMean;
                const int mid = static_cast<int>(std::lround(center / kStep));
                center_index[static_cast<std::size_t>(m)] = mid;
                double sum = 0.0;
                std::vector<double>& row = kernel[static_cast<std::size_t>(m)];
                for (int d = -reach; d <= reach; ++d) {
                    const int to = mid + d;
                    if (to < 0 || to >= kInner) continue;
                    const double z = (to * kStep - center) / spread;
                    row[static_cast<std::size_t>(d + reach)] = std::exp(-0.5 * z * z);
                    sum += row[static_cast<std::size_t>(d + reach)];
                }
                for (double& w : row) w /= sum;
            }
        }
        const double kappa = 2.0 * std::fabs(o.s) * o.e / o.vp;
        const double lambda = o.s * o.s / o.vp;
        double best = -INFINITY;
        for (int i = lo; i <= hi; ++i) {
            std::vector<double>& p = belief[static_cast<std::size_t>(i)];
            if (!kernel.empty()) {
                std::fill(moved.begin(), moved.end(), 0.0);
                for (int m = 0; m < kInner; ++m) {
                    const double pm = p[static_cast<std::size_t>(m)];
                    if (pm == 0.0) continue;
                    const int mid = center_index[static_cast<std::size_t>(m)];
                    const std::vector<double>& row = kernel[static_cast<std::size_t>(m)];
                    for (int d = -reach; d <= reach; ++d) {
                        const int to = mid + d;
                        if (to >= 0 && to < kInner) moved[static_cast<std::size_t>(to)] += pm * row[static_cast<std::size_t>(d + reach)];
                    }
                }
                p.swap(moved);
            }
            const double root_omega = std::exp(c.u_lo + i * h);
            double top = -INFINITY;
            for (int m = 0; m < kInner; ++m) {
                const double a = m * kStep * root_omega;
                const double t = sample_weight(a * a * o.s * o.s / kSigma2) * (-lambda * a * a + ln_i0(kappa * a));
                term[static_cast<std::size_t>(m)] = t;
                if (p[static_cast<std::size_t>(m)] > 0.0) top = std::max(top, t);
            }
            double mass = 0.0;
            for (int m = 0; m < kInner; ++m) {
                p[static_cast<std::size_t>(m)] *= std::exp(term[static_cast<std::size_t>(m)] - top);
                mass += p[static_cast<std::size_t>(m)];
            }
            for (double& x : p) x /= mass;
            log_scale[static_cast<std::size_t>(i)] += top + std::log(mass);
            best = std::max(best, log_scale[static_cast<std::size_t>(i)]);
        }
        double sum = 0.0;
        for (int i = lo; i <= hi; ++i)
            sum += (i == 0 || i == points - 1 ? 0.5 : 1.0) * std::exp(log_scale[static_cast<std::size_t>(i)] - best);
        out.push_back(best + std::log(sum) - std::log(static_cast<double>(points - 1)));
        while (lo < hi && log_scale[static_cast<std::size_t>(lo)] < best - kNewbornLiveRangeNats) ++lo;
        while (hi > lo && log_scale[static_cast<std::size_t>(hi)] < best - kNewbornLiveRangeNats) --hi;
    }
    return out;
}

std::string measurement_directory() {
    const char* dir = std::getenv("KZ4AP_MEASUREMENT_DIR");
    const std::filesystem::path path = std::filesystem::path(dir != nullptr ? dir : "measurements") / "newborn";
    std::filesystem::create_directories(path);
    return path.string();
}

}  // namespace

TEST(Newborn, ApproximationsMeasured) {
    // Label measurement (Newborn.Measurement in engine/CMakeLists.txt sets KZ4AP_MEASUREMENT=1 and the output folder;
    // skipped otherwise); design notes part 15, item 2, run in Task 16. Writes to build/measurements/newborn/:
    // (a) grid-first-marks.csv and grid-summary.csv: the grid (128 points, live range 80 nats, the tables) against the
    //     4 001-point dense integral of the same integrand, per sample and per first mark through the dense integral's
    //     switch, the switch samples, sigma_u at handover and the deepest revival at handover, on the study's cases
    //     (seeds 1001 to 1036) plus +50, +60, +70 and +85 dB S500 on the four marks (seeds 1101 to 1116), 1 000 first
    //     marks each;
    // (b) fading-first-marks.csv and fading-summary.csv: the loss from holding the amplitude constant before the switch,
    //     ln Z through the newborn's switch sample under the fading path model (fading_path_ln_z) less ln Z of the
    //     constant-amplitude integrand on the same 128-point grid with the same direct evaluation, at 0 dB S500 on the
    //     four marks under flat Rayleigh fading of Gaussian Doppler spectrum (keyed_signals.hpp, rayleigh_gain, two-sigma
    //     width f_D) at f_D = 0 (the control: no fading; only the path model's Rayleigh start differs), 2 and 5 Hz, 100
    //     first marks each (seeds 2001 to 2012; the gain's seed is seed x 1000 + 7 + the mark's index). The gain multiplies
    //     the keyed carrier at the samples (the chain's filtering of a 2 to 5 Hz gain is ignored: its response is 42 ms).
    if (!measurement_requested()) GTEST_SKIP() << "runs under the measurement label";
    const std::string dir = measurement_directory();
    // (a)
    {
        std::vector<CaseSpec> specs = study_specs(kStudyLevels, true);
        std::vector<CaseSpec> strong = study_specs({50.0, 60.0, 70.0, 85.0}, false);
        for (CaseSpec& s : strong) s.seed += 100;
        specs.insert(specs.end(), strong.begin(), strong.end());
        constexpr int kMarks = 1000;
        std::ofstream rows(dir + "/grid-first-marks.csv");
        std::ofstream summary(dir + "/grid-summary.csv");
        rows << "case,level_db_s500,length_samples,shift_samples,seed,index,scored_samples,switch_j_grid,switch_j_dense,"
                "largest_per_sample_difference_nats,first_mark_total_difference_nats,sigma_u_at_handover_nepers,"
                "deepest_revival_at_handover_nats\n";
        summary << "case,first_marks,per_sample_p95_nats,per_sample_largest_nats,total_median_nats,total_p95_nats,"
                   "total_largest_nats,switch_same,switch_earlier,switch_later,narrowest_sigma_u_at_handover_nepers,"
                   "deepest_revival_nats\n";
        for (const CaseSpec& spec : specs) {
            const std::vector<Case> one = make_cases({spec}, kMarks);
            const Case& c = one.front();
            struct Row {
                Difference d;
                double signed_total = 0.0;
                int switch_grid = 0;
                int switch_dense = 0;
                double sigma = 0.0;
                double revival = 0.0;
                std::vector<double> per_sample;
            };
            std::vector<Row> r(c.sequences.size());
            parallel_for(kMarks, [&](int i) {
                const Sequence& q = c.sequences[static_cast<std::size_t>(i)];
                const Scores grid = newborn_run(q, kNewbornLiveRangeNats, false);
                const Scores dense = direct_grid(q, 4001, INFINITY, true);
                Row& row = r[static_cast<std::size_t>(i)];
                row.d = difference(grid, dense);
                for (int k = 0; k <= dense.switch_index; ++k) {
                    const double x = grid.score[static_cast<std::size_t>(k)] - dense.score[static_cast<std::size_t>(k)];
                    row.signed_total += x;
                    row.per_sample.push_back(std::fabs(x));
                }
                row.switch_grid = q[static_cast<std::size_t>(grid.switch_index)].j;
                row.switch_dense = q[static_cast<std::size_t>(dense.switch_index)].j;
                row.sigma = grid.sigma_at_switch;
                row.revival = grid.revival_at_switch;
            });
            std::vector<double> per_sample;
            std::vector<double> totals;
            int same = 0;
            int earlier = 0;
            int later = 0;
            double narrowest = INFINITY;
            double deepest = 0.0;
            for (int i = 0; i < kMarks; ++i) {
                const Row& row = r[static_cast<std::size_t>(i)];
                rows << c.spec.name << ',' << (c.spec.level_db ? std::to_string(*c.spec.level_db) : "noise") << ','
                     << c.spec.length << ',' << c.spec.shift << ',' << c.spec.seed << ',' << i << ','
                     << c.sequences[static_cast<std::size_t>(i)].size() << ',' << row.switch_grid << ','
                     << row.switch_dense << ',' << std::setprecision(4) << row.d.per_sample << ',' << row.signed_total
                     << ',' << row.sigma << ',' << row.revival << '\n';
                per_sample.insert(per_sample.end(), row.per_sample.begin(), row.per_sample.end());
                totals.push_back(row.d.total);
                same += row.switch_grid == row.switch_dense;
                earlier += row.switch_grid < row.switch_dense;
                later += row.switch_grid > row.switch_dense;
                narrowest = std::min(narrowest, row.sigma);
                deepest = std::max(deepest, row.revival);
            }
            summary << c.spec.name << ',' << kMarks << ',' << std::setprecision(4) << percentile(per_sample, 95) << ','
                    << percentile(per_sample, 100) << ',' << percentile(totals, 50) << ',' << percentile(totals, 95) << ','
                    << percentile(totals, 100) << ',' << same << ',' << earlier << ',' << later << ',' << narrowest << ','
                    << deepest << '\n';
            std::cout << "measured: " << c.spec.name << ": per sample p95 " << percentile(per_sample, 95) << ", largest "
                      << percentile(per_sample, 100) << " nats; per first mark p95 " << percentile(totals, 95)
                      << ", largest " << percentile(totals, 100) << " nats; switch same " << same << ", earlier "
                      << earlier << ", later " << later << "; narrowest sigma_u at handover " << narrowest
                      << " nepers; deepest revival " << deepest << " nats" << std::endl;
        }
    }
    // (b)
    {
        constexpr int kMarks = 100;
        std::ofstream rows(dir + "/fading-first-marks.csv");
        std::ofstream summary(dir + "/fading-summary.csv");
        rows << "spread_hz,case,seed,index,switch_j,ln_z_fading_path_nats,ln_z_constant_nats,loss_nats,"
                "newborn_ln_z_nats\n";
        summary << "spread_hz,case,first_marks,loss_median_nats,loss_p95_nats,loss_largest_nats\n";
        std::uint64_t seed = 2000;
        for (const double spread : {0.0, 2.0, 5.0}) {
            const FadingTables fading(spread, 4.0 / 1500.0);
            for (const auto& [name, length] : std::vector<std::pair<std::string, int>>{
                     {"dot25", 18}, {"dash25", 54}, {"dot50", 9}, {"dash50", 27}}) {
                const CaseSpec spec{name + "_+0", 0.0, length, 0, ++seed};
                const int span = kPre + length + 16 + kPost;
                std::vector<double> loss(kMarks);
                std::vector<std::string> lines(kMarks);
                parallel_for(kMarks, [&](int i) {
                    std::vector<std::complex<double>> gain;
                    if (spread > 0.0)
                        gain = test::rayleigh_gain(static_cast<std::size_t>(span), 1500.0 / 4.0, spread,
                                                   static_cast<unsigned>(spec.seed * 1000 + 7 + static_cast<std::uint64_t>(i)));
                    const Sequence q = make_sequence(spec, static_cast<std::uint64_t>(i), gain);
                    const Scores grid = newborn_run(q, kNewbornLiveRangeNats, true);
                    const int through = grid.switch_index;
                    const Scores constant = direct_grid(q, kNewbornGridPoints, kNewbornLiveRangeNats, false);
                    double ln_z_constant = 0.0;
                    double ln_z_newborn = 0.0;
                    for (int k = 0; k <= through; ++k) {
                        ln_z_constant += constant.score[static_cast<std::size_t>(k)];
                        ln_z_newborn += grid.score[static_cast<std::size_t>(k)];
                    }
                    const double ln_z_path = fading_path_ln_z(q, kNewbornGridPoints, fading, through).back();
                    loss[static_cast<std::size_t>(i)] = ln_z_path - ln_z_constant;
                    std::ostringstream line;
                    line << std::setprecision(6) << spread << ',' << spec.name << ',' << spec.seed << ',' << i << ','
                         << q[static_cast<std::size_t>(through)].j << ',' << ln_z_path << ',' << ln_z_constant << ','
                         << ln_z_path - ln_z_constant << ',' << ln_z_newborn << '\n';
                    lines[static_cast<std::size_t>(i)] = line.str();
                });
                for (const std::string& line : lines) rows << line;
                summary << spread << ',' << spec.name << ',' << kMarks << ',' << std::setprecision(4)
                        << percentile(loss, 50) << ',' << percentile(loss, 95) << ',' << percentile(loss, 100) << '\n';
                std::cout << "measured: fading " << spread << " Hz, " << spec.name << ": loss (path - constant) median "
                          << percentile(loss, 50) << ", p95 " << percentile(loss, 95) << ", largest "
                          << percentile(loss, 100) << " nats" << std::endl;
            }
        }
    }
}

TEST(Newborn, CpuPerNewbornSample) {
    // Label measurement (Newborn.CpuMeasurement; skipped otherwise): ns per newborn-sample with 500 newborns, each sample
    // V' from (e, s, v, the window's peak), add and should_switch; at the switch, to_gaussian and a fresh newborn on the
    // next first mark (the study's tnb.cpp loop). Two sets: noise (the four marks' noise cases, 1 000 first marks each:
    // the design's "500 newborns on noise") and mixed (the 32 level cases, 250 each). Median of 7 repetitions of 3 000
    // samples per newborn after a warm-up; one thread, not pinned.
    if (!measurement_requested()) GTEST_SKIP() << "runs under the measurement label";
    for (const bool noise_only : {true, false}) {
        std::vector<CaseSpec> specs;
        for (const CaseSpec& s : study_specs(kStudyLevels, false))
            if (!noise_only || !s.level_db) specs.push_back(s);
        const std::vector<Case> cases = make_cases(specs, noise_only ? 1000 : 250);
        std::vector<const Sequence*> marks;  // interleaved over the cases
        for (std::size_t i = 0; i < cases.front().sequences.size(); ++i)
            for (const Case& c : cases) marks.push_back(&c.sequences[i]);
        constexpr int kNewborns = 500;
        constexpr int kSteps = 3000;
        const NewbornIntegral fresh(tables(), chain().u_lo, chain().u_hi, kNewbornGridPoints, kNewbornLiveRangeNats);
        std::vector<double> ns;
        double live = 0.0;
        double samples = 0.0;
        double sink = 0.0;
        for (int rep = 0; rep <= 7; ++rep) {
            std::vector<NewbornIntegral> newborns(kNewborns, fresh);
            std::vector<std::size_t> mark(kNewborns);
            std::vector<std::size_t> pos(kNewborns, 0);
            std::size_t next = 0;
            for (std::size_t b = 0; b < kNewborns; ++b) mark[b] = next++ % marks.size();
            // Stagger: each newborn starts a different number of samples into its first mark (untimed).
            std::uint32_t lcg = 12345;
            for (std::size_t b = 0; b < kNewborns; ++b) {
                lcg = lcg * 1664525u + 1013904223u;
                const Sequence& q = *marks[mark[b]];
                const std::size_t skip = lcg % q.size();
                for (std::size_t k = 0; k < skip; ++k) newborns[b].add(q[k].e, q[k].s, q[k].vp, kSigma2);
                pos[b] = skip;
            }
            double rep_live = 0.0;
            const auto start = std::chrono::steady_clock::now();
            for (int step = 0; step < kSteps; ++step) {
                for (std::size_t b = 0; b < kNewborns; ++b) {
                    NewbornIntegral& newborn = newborns[b];
                    const Sequence& q = *marks[mark[b]];
                    const Observation& o = q[pos[b]];
                    rep_live += newborn.live_points();
                    const double vp = v_prime_fs2(kSigma2, o.peak, o.e, o.s, o.v, kFloor);
                    newborn.add(o.e, o.s, vp, kSigma2);
                    sink += newborn.last_score_nats();
                    const bool last = ++pos[b] == q.size();
                    if (newborn.should_switch(kSwitchSigmaU, last)) {
                        sink += newborn.to_gaussian(static_cast<std::int64_t>(step)).p_fs2 * 1e-30;
                        newborn = fresh;
                        mark[b] = next++ % marks.size();
                        pos[b] = 0;
                    }
                }
            }
            const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
            if (rep > 0) {
                ns.push_back(seconds * 1e9 / (static_cast<double>(kNewborns) * kSteps));
                live += rep_live;
                samples += static_cast<double>(kNewborns) * kSteps;
            }
        }
        std::sort(ns.begin(), ns.end());
        std::cout << "measured: " << (noise_only ? "noise" : "mixed") << ": " << ns[ns.size() / 2]
                  << " ns per newborn-sample (median of 7; " << ns.front() << " to " << ns.back() << "), "
                  << live / samples << " live points per sample (sink " << sink << ")" << std::endl;
        EXPECT_TRUE(std::isfinite(sink));
    }
}
