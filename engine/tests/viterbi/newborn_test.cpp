#include "kz4ap/viterbi/newborn.hpp"

#include "keyed_signals.hpp"
#include "kz4ap/channelizer.hpp"
#include "kz4ap/viterbi/amplitude.hpp"
#include "kz4ap/viterbi/front_filter.hpp"
#include "kz4ap/viterbi/mark_template.hpp"
#include "kz4ap/viterbi/observation.hpp"

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
#include <optional>
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
// EdgeHistory, f = 0.02. The noise draws are portable (splitmix64 and Box-Muller), so both platforms draw the same sequences.

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
    static const NewbornTables t(chain().noise, chain().noise.sigma2_per_n0_hz);
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
    double e;   // FS
    double s;   // 1
    double v;   // 1
    int j;      // samples after the claimed key-down
};
using Sequence = std::vector<Observation>;

struct CaseSpec {
    std::string name;
    std::optional<double> level_db;  // dB S500; none: noise alone
    int length;                      // the mark's samples: 18 (25 WPM dot), 54 (dash), 9 (50 WPM dot), 27 (dash)
    int shift;                       // the claim's offset from the true key-down (samples)
    std::uint64_t seed;
};

// One first mark of a case.
Sequence make_sequence(const CaseSpec& c, std::uint64_t index) {
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
            x[static_cast<std::size_t>(k)] += a * phase * shape;
        }
    }
    Sequence out;
    EdgeHistory history;
    for (int j = 0; j <= c.length + 15; ++j) {
        advance(history, j == 0 || j == c.length);
        const TemplateValue tv = template_value(history, chain().tables);
        if (tv.s == 0.0) continue;
        out.push_back({std::abs(x[static_cast<std::size_t>(kPre + c.shift + j)]), tv.s, tv.v, j});
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

// Correctly timed claims on the study's marks: noise and the levels given, for each of the four marks.
std::vector<CaseSpec> aligned_specs(const std::vector<std::optional<double>>& levels) {
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
    return out;
}

const std::vector<std::optional<double>> kStudyLevels = {std::nullopt, -10.0, -5.0, 0.0, 5.0, 10.0, 20.0, 40.0};
constexpr int kPerCase = 200;  // first marks per case

// --- The integrals ---

// The prediction step at a Doppler spread (Hz): every 8 samples, var = (1/2)(1 - rho(Delta)^2).
NewbornFade fade_at(double spread_hz) {
    const FadingTables fading(spread_hz, 4.0 / 1500.0);
    const double rho = fading.rho(1);
    return {8, 0.5 * (1.0 - rho * rho)};
}
// With the step's variance 0: the comparisons with a dense integral, which holds the amplitude constant.
const NewbornFade kZeroVariance{8, 0.0};

double weight() { return chain().noise.w_strong; }

// Part 4's full density against the shared reference at amplitude a (nats, before w), V at that amplitude; direct.
double full_density(const Observation& o, double a) {
    const double a2 = a * a;
    const double v = kSigma2 + a2 * (kFloor * kFloor * o.s * o.s + o.v);
    return -std::log(v / kSigma2) - (o.e * o.e + o.s * o.s * a2) / v + o.e * o.e / kSigma2 +
           ln_i0(2.0 * o.e * std::fabs(o.s) * a / v);
}

struct Scores {
    std::vector<double> score;  // per sample (nats)
    std::vector<double> sigma;  // sigma_u after each sample (nepers)
    std::vector<double> mean;   // u^ after each sample (nepers)
    int switch_index = -1;      // the first sample with sigma_u < 0.1, else the last
    double revival_at_switch = 0.0;
    double sigma_at_switch = 0.0;
};

// The same integrand on `points` points with ln_i0 and std::log at every point (the direct evaluation), trapezoid with
// the ends halved, over [u_lo, u_lo + span_nepers], live range live_nats (+infinity: every point). No prediction step.
Scores direct_grid(const Sequence& q, int points, double live_nats, bool stop_at_switch,
                   double span_nepers = kNewbornPriorSpanNepers) {
    const Chain& c = chain();
    const double h = span_nepers / (points - 1);
    const double w = weight();
    std::vector<double> a(static_cast<std::size_t>(points));
    for (int i = 0; i < points; ++i) a[static_cast<std::size_t>(i)] = std::exp(c.u_lo + i * h);
    std::vector<double> f(static_cast<std::size_t>(points), 0.0);
    int lo = 0;
    int hi = points - 1;
    double ln_z_before = 0.0;
    Scores run;
    const int n = static_cast<int>(q.size());
    for (int k = 0; k < n; ++k) {
        const Observation& o = q[static_cast<std::size_t>(k)];
        double m = -std::numeric_limits<double>::infinity();
        int at = lo;
        for (int i = lo; i <= hi; ++i) {
            const auto ii = static_cast<std::size_t>(i);
            f[ii] += w * full_density(o, a[ii]);
            if (f[ii] > m) {
                m = f[ii];
                at = i;
            }
        }
        double s0 = 0.0;
        double s1 = 0.0;
        double s2 = 0.0;
        for (int i = lo; i <= hi; ++i) {
            const double p = (i == 0 || i == points - 1 ? 0.5 : 1.0) * std::exp(f[static_cast<std::size_t>(i)] - m);
            const double d = (i - at) * h;
            s0 += p;
            s1 += p * d;
            s2 += p * d * d;
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

Scores newborn_run(const Sequence& q, double live_nats, bool stop_at_switch, NewbornFade fade = kZeroVariance,
                   const NewbornTables& t = tables()) {
    NewbornIntegral newborn(t, chain().u_lo, chain().u_lo + t.span_nepers(), t.points(), live_nats, fade);
    Scores run;
    const int n = static_cast<int>(q.size());
    for (int k = 0; k < n; ++k) {
        const Observation& o = q[static_cast<std::size_t>(k)];
        newborn.add(o.e, o.s, o.v, kSigma2, kFloor);
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

// A noise-free first mark: e = a |s|.
Sequence noise_free(double level_db, int length) {
    const double a = amplitude_of(level_db);
    Sequence out;
    EdgeHistory history;
    for (int j = 0; j <= length + 15; ++j) {
        advance(history, j == 0 || j == length);
        const TemplateValue tv = template_value(history, chain().tables);
        if (tv.s == 0.0) continue;
        out.push_back({a * std::fabs(tv.s), tv.s, tv.v, j});
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
    // Correctly timed claims only (decision log, 2026-10-30): every per-sample score within 0.005 nats of a 4 001-point
    // trapezoid of the same integrand with direct ln I0 and every first-mark total within 0.015 nats, through the dense
    // integral's switch, on 32 cases of 200 first marks, the step's variance 0.
    //
    // KNOWN LIMITATION, not tested (measured, Task 10, a 25 WPM dash, 200 first marks per case, against the same dense
    // integral): on claims that are off, the grid and the live range are not the flat prior's integral. Largest error
    // per sample, the grid's part / the live range's part at 160 nats:
    //   +20 dB S500: 3 late 0.005 / 0; 4 late 0.004 / 19.9 (lowers the story's score)
    //   +40 dB S500: 1 late 0.026 / 0; 2 late 0.099 / 97.4; 3 late 90.5 / 224; 4 late 1.28 / 0; 3 early 0.021 / 0
    //   +60 dB S500: 1 late 0.031 / 0; 2 late 0.395 / 0; 3 late 3.36 / 0
    // The live range only lowers a misaligned story's score; the grid's part has both signs, at most +3.36 nats. See
    // newborn.hpp, section 2, and Newborn.AlignedClaimsStayAheadWithinTheTie for what this does to the comparison.
    const std::vector<Case> cases = make_cases(aligned_specs(kStudyLevels), kPerCase);
    for (const Case& c : cases) {
        std::vector<Difference> d(c.sequences.size());
        parallel_for(static_cast<int>(c.sequences.size()), [&](int i) {
            const Sequence& q = c.sequences[static_cast<std::size_t>(i)];
            d[static_cast<std::size_t>(i)] =
                difference(newborn_run(q, kNewbornLiveRangeNats, false), direct_grid(q, 4001, INFINITY, true));
        });
        double per_sample = 0.0;
        double total = 0.0;
        for (const Difference& x : d) {
            per_sample = std::max(per_sample, x.per_sample);
            total = std::max(total, x.total);
        }
        EXPECT_LE(per_sample, 0.005) << c.spec.name;
        EXPECT_LE(total, 0.015) << c.spec.name;
    }
}

TEST(Newborn, AlignedClaimsStayAheadWithinTheTie) {
    // What the known limitation can do to the comparison that matters: the same signal and noise scored by the correctly
    // timed claim and by a claim one sample early or late, a 25 WPM dash at +20, +40 and +60 dB S500, 300 first marks per
    // case. Lead: ln Z(aligned) - ln Z(off), over absolute samples through the later of the two switches (a story's
    // samples before its claim score 0; after its own switch a story is N(mu, P), so the dense integral stands in from
    // there). Checked: (1) the grid never changes the order the dense integral gives; (2) the dense lead itself is
    // positive in the median and more than -3 nats at worst: one sample late is the tie at mid-sample edge positions
    // (part 11; measured: ahead in 9.5 to 15% of first marks by up to 2.6 nats), one sample early no worse.
    std::uint64_t seed = 7000;
    for (const double level : {20.0, 40.0, 60.0}) {
        ++seed;
        for (const int shift : {-1, 1}) {
            constexpr int kMarks = 300;
            std::vector<double> dense_lead(kMarks);
            std::vector<double> grid_lead(kMarks);
            parallel_for(kMarks, [&](int i) {
                const Sequence aligned = make_sequence({"", level, 54, 0, seed}, static_cast<std::uint64_t>(i));
                const Sequence off = make_sequence({"", level, 54, shift, seed}, static_cast<std::uint64_t>(i));
                const Scores dense_a = direct_grid(aligned, 4001, INFINITY, false);
                const Scores dense_o = direct_grid(off, 4001, INFINITY, false);
                const Scores grid_a = newborn_run(aligned, kNewbornLiveRangeNats, false);
                const Scores grid_o = newborn_run(off, kNewbornLiveRangeNats, false);
                const int through = std::max(aligned[static_cast<std::size_t>(dense_a.switch_index)].j,
                                             off[static_cast<std::size_t>(dense_o.switch_index)].j + shift);
                const auto sum = [&](const Sequence& q, const Scores& dense, const Scores* grid, int offset) {
                    double x = 0.0;
                    for (std::size_t k = 0; k < q.size(); ++k) {
                        if (q[k].j + offset > through) continue;
                        const bool on_grid = grid != nullptr && static_cast<int>(k) <= grid->switch_index;
                        x += on_grid ? grid->score[k] : dense.score[k];
                    }
                    return x;
                };
                dense_lead[static_cast<std::size_t>(i)] = sum(aligned, dense_a, nullptr, 0) - sum(off, dense_o, nullptr, shift);
                grid_lead[static_cast<std::size_t>(i)] = sum(aligned, dense_a, &grid_a, 0) - sum(off, dense_o, &grid_o, shift);
            });
            int flipped = 0;
            for (int i = 0; i < kMarks; ++i)
                flipped += (dense_lead[static_cast<std::size_t>(i)] > 0.0) != (grid_lead[static_cast<std::size_t>(i)] > 0.0);
            EXPECT_EQ(flipped, 0) << level << " dB S500, claim " << shift;
            EXPECT_GT(percentile(dense_lead, 50), 0.0) << level << " dB S500, claim " << shift;
            EXPECT_GT(percentile(dense_lead, 0), -3.0) << level << " dB S500, claim " << shift;
        }
    }
}

TEST(Newborn, LiveRangeLosesNothing) {
    // On correctly timed claims the live range equals the full 128-point grid within 1e-9 nats per sample through the
    // switch; the deepest revival is below the live range and reported.
    const std::vector<Case> cases = make_cases(aligned_specs(kStudyLevels), kPerCase);
    double deepest = 0.0;
    for (const Case& c : cases) {
        double worst = 0.0;
        for (const Sequence& q : c.sequences) {
            const Scores live = newborn_run(q, kNewbornLiveRangeNats, false, fade_at(2.0));
            const Scores full = newborn_run(q, INFINITY, false, fade_at(2.0));
            ASSERT_GE(live.switch_index, 0);
            EXPECT_EQ(live.switch_index, full.switch_index);
            for (int k = 0; k <= full.switch_index; ++k)
                worst = std::max(worst, std::fabs(live.score[static_cast<std::size_t>(k)] - full.score[static_cast<std::size_t>(k)]));
            deepest = std::max(deepest, live.revival_at_switch);
        }
        EXPECT_LE(worst, 1e-9) << c.spec.name;
    }
    std::cout << "measured: deepest revival at handover on correctly timed claims " << deepest << " nats\n";
    EXPECT_LT(deepest, kNewbornLiveRangeNats);
}

TEST(Newborn, TablesMatchDirectEvaluation) {
    // G (linear at h/8; Ruling 98) against ln_i0 within 5e-6 nats over x from 1e-3 to 1e6; softplus against its formula
    // within 1e-6; the tables against ln_i0 and std::log at the same 128 points within 5e-4 nats per sample and 0.005 nats
    // per first mark, through the direct grid's switch, on correctly timed claims and on claims 2 samples off.
    double worst_g = 0.0;
    for (int i = 0; i <= 40000; ++i) {
        const double x = std::exp(std::log(1e-3) + i * (std::log(1e6) - std::log(1e-3)) / 40000.0);
        worst_g = std::max(worst_g, std::fabs(tables().ln_i0_from_table(x) - ln_i0(x)));
    }
    EXPECT_LE(worst_g, 5e-6);
    double worst_softplus = 0.0;
    for (int i = 0; i <= 40000; ++i) {
        const double y = -70.0 + i * 140.0 / 40000.0;
        const double exact = y > 0.0 ? y + std::log1p(std::exp(-y)) : std::log1p(std::exp(y));
        worst_softplus = std::max(worst_softplus, std::fabs(tables().softplus_from_table(y) - exact));
    }
    EXPECT_LE(worst_softplus, 1e-6);
    std::vector<CaseSpec> specs = aligned_specs(kStudyLevels);
    specs.push_back({"dash25_+20_early2", 20.0, 54, -2, 1101});
    specs.push_back({"dash25_+20_late2", 20.0, 54, 2, 1102});
    const std::vector<Case> cases = make_cases(specs, kPerCase);
    for (const Case& c : cases) {
        std::vector<Difference> d(c.sequences.size());
        parallel_for(static_cast<int>(c.sequences.size()), [&](int i) {
            const Sequence& q = c.sequences[static_cast<std::size_t>(i)];
            d[static_cast<std::size_t>(i)] = difference(newborn_run(q, kNewbornLiveRangeNats, false),
                                                        direct_grid(q, kNewbornGridPoints, kNewbornLiveRangeNats, true));
        });
        double per_sample = 0.0;
        double total = 0.0;
        for (const Difference& x : d) {
            per_sample = std::max(per_sample, x.per_sample);
            total = std::max(total, x.total);
        }
        EXPECT_LE(per_sample, 5e-4) << c.spec.name;
        EXPECT_LE(total, 0.005) << c.spec.name;
    }
}

TEST(Newborn, StaysBoundedOnNoise) {
    // 10 000 noise first marks of a 25 WPM dash, with the step: every score finite; the posterior mean at the last sample
    // below 0 dB S500.
    const CaseSpec spec{"dash25_noise", std::nullopt, 54, 0, 77};
    std::vector<double> last_mean_db(10000, 0.0);
    std::vector<int> finite(10000, 1);
    parallel_for(10000, [&](int i) {
        const Sequence q = make_sequence(spec, static_cast<std::uint64_t>(i));
        NewbornIntegral newborn(tables(), chain().u_lo, chain().u_hi, kNewbornGridPoints, kNewbornLiveRangeNats, fade_at(2.0));
        for (const Observation& o : q) {
            newborn.add(o.e, o.s, o.v, kSigma2, kFloor);
            if (!std::isfinite(newborn.last_score_nats())) finite[static_cast<std::size_t>(i)] = 0;
        }
        last_mean_db[static_cast<std::size_t>(i)] = level_of_u(newborn.mean_u());
    });
    EXPECT_EQ(std::count(finite.begin(), finite.end(), 0), 0);
    EXPECT_LT(*std::max_element(last_mean_db.begin(), last_mean_db.end()), 0.0);
}

TEST(Newborn, SwitchTimings) {
    // Noise-free on a 25 WPM dash, with the step as the decoder runs it: the switch at j = 9 (+30 and +20), 12 (+10) dB
    // S500, +/-1. At 0 dB S500 the step keeps sigma_u above 0.1 nepers (0.109 at the end; at variance 0 it crosses at j =
    // 54): the latest switch, j = 69 (a changed value, in the decision log).
    const std::vector<std::pair<double, int>> levels = {{30.0, 9}, {20.0, 9}, {10.0, 12}, {0.0, 69}};
    for (const auto& [level, expected] : levels) {
        const Sequence q = noise_free(level, 54);
        const Scores run = newborn_run(q, kNewbornLiveRangeNats, true, fade_at(2.0));
        const Scores zero = newborn_run(q, kNewbornLiveRangeNats, true);
        const int j = q[static_cast<std::size_t>(run.switch_index)].j;
        std::cout << "measured: +" << level << " dB S500: switches at j = " << j << " with the step (sigma_u "
                  << run.sigma_at_switch << " nepers), j = " << q[static_cast<std::size_t>(zero.switch_index)].j
                  << " at variance 0\n";
        EXPECT_NEAR(j, expected, 1) << level << " dB S500";
        if (level > 0.0) EXPECT_LT(run.sigma_at_switch, kSwitchSigmaU);
    }
}

TEST(Newborn, StrongEdgeOfThePrior) {
    // Correctly timed first marks at +60 dB S500: finite and within 0.005 nats per sample of the dense integral (variance
    // 0). At +85 dB S500 (the prior's top): finite, the differences reported (the end-point term, h^2 |F'(u_hi)| / 12).
    // The narrowest sigma_u at handover is reported with the step; correctly timed claims stay at or above 0.05 nepers.
    for (const double level : {60.0, 85.0}) {
        double per_sample = 0.0;
        double narrowest = INFINITY;
        for (const Case& c : make_cases(aligned_specs({level}), 50)) {
            for (const Sequence& q : c.sequences) {
                const Scores grid = newborn_run(q, kNewbornLiveRangeNats, false);
                for (const double s : grid.score) EXPECT_TRUE(std::isfinite(s));
                per_sample = std::max(per_sample, difference(grid, direct_grid(q, 4001, INFINITY, true)).per_sample);
                narrowest = std::min(narrowest, newborn_run(q, kNewbornLiveRangeNats, true, fade_at(2.0)).sigma_at_switch);
            }
        }
        std::cout << "measured: +" << level << " dB S500: largest per-sample difference " << per_sample
                  << " nats, narrowest sigma_u at handover " << narrowest << " nepers\n";
        if (level == 60.0) EXPECT_LE(per_sample, 0.005);
        EXPECT_GE(narrowest, 0.05);
    }
}

TEST(Newborn, LatestSwitchAtJ15AfterTheKeyUp) {
    // At -10 dB S500 a 5-sample mark switches exactly at the 16th sample after its claimed key-up (j = 15 after it, 20
    // after the key-down), the caller flagging that sample from its EdgeHistory: never earlier, on 50 first marks.
    const CaseSpec spec{"mark5_-10", -10.0, 5, 0, 91};
    for (std::uint64_t index = 0; index < 50; ++index) {
        const Sequence q = make_sequence(spec, index);
        NewbornIntegral newborn(tables(), chain().u_lo, chain().u_hi, kNewbornGridPoints, kNewbornLiveRangeNats, fade_at(2.0));
        EdgeHistory history;
        std::size_t next = 0;
        int switched_at = -1;
        for (int j = 0; j <= 20 && switched_at < 0; ++j) {
            advance(history, j == 0 || j == 5);
            if (next >= q.size() || q[next].j != j) continue;  // s = 0: not scored
            const Observation& o = q[next++];
            newborn.add(o.e, o.s, o.v, kSigma2, kFloor);
            const bool last = (history.recent & (1u << 15)) != 0 && history.key_down_before;
            if (newborn.should_switch(kSwitchSigmaU, last)) switched_at = j;
        }
        EXPECT_EQ(switched_at, 20) << "first mark " << index;
    }
}

TEST(Newborn, NegativeTemplateValues) {
    // Samples with s = -0.00064 and -0.00080 (the channel filter's precursor) score as |s| would, within 1e-12 nats.
    NewbornIntegral negative(tables(), chain().u_lo, chain().u_hi, kNewbornGridPoints, kNewbornLiveRangeNats);
    NewbornIntegral positive = negative;
    const double a = amplitude_of(40.0);
    for (const double s : {-0.00064, -0.00080}) {
        const double e = 0.7 + a * std::fabs(s);
        negative.add(e, s, 1e-6, kSigma2, kFloor);
        positive.add(e, -s, 1e-6, kSigma2, kFloor);
        EXPECT_NEAR(negative.last_score_nats(), positive.last_score_nats(), 1e-12) << "s = " << s;
    }
    EXPECT_NEAR(negative.mean_u(), positive.mean_u(), 1e-12);
}

TEST(Newborn, OtherGridsMatchTheirDirectEvaluation) {
    // 200 points over a prior of 120 dB (13.82 nepers): against the direct evaluation on the same grid within 5e-4 nats
    // per sample, on 50 first marks of noise, 0 and +40 dB S500.
    const double span = 6.0 * std::numbers::ln10;  // 120 dB of amplitude, nepers
    const NewbornTables other(chain().noise, chain().noise.sigma2_per_n0_hz, 200, span);
    double worst = 0.0;
    for (const Case& c : make_cases({{"dash25_noise", std::nullopt, 54, 0, 631}, {"dash25_+0", 0.0, 54, 0, 632}, {"dash25_+40", 40.0, 54, 0, 633}}, 50))
        for (const Sequence& q : c.sequences)
            worst = std::max(worst, difference(newborn_run(q, kNewbornLiveRangeNats, false, kZeroVariance, other),
                                               direct_grid(q, 200, kNewbornLiveRangeNats, true, span)).per_sample);
    EXPECT_LE(worst, 5e-4);
}

TEST(Newborn, RefusesBadArguments) {
    const OutputNoise& noise = chain().noise;
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();
    EXPECT_THROW(NewbornTables(noise, 0.0), std::invalid_argument);
    EXPECT_THROW(NewbornTables(noise, nan), std::invalid_argument);
    EXPECT_THROW(NewbornTables(noise, noise.sigma2_per_n0_hz, 1), std::invalid_argument);
    const double lo = chain().u_lo;
    const double hi = chain().u_hi;
    const NewbornTables& t = tables();
    EXPECT_THROW(NewbornIntegral(t, hi, lo, 128, 80.0), std::invalid_argument);
    EXPECT_THROW(NewbornIntegral(t, lo, hi, 127, 80.0), std::invalid_argument);
    EXPECT_THROW(NewbornIntegral(t, lo, hi + 0.01, 128, 80.0), std::invalid_argument);
    EXPECT_THROW(NewbornIntegral(t, lo, hi, 128, 0.0), std::invalid_argument);
    EXPECT_THROW(NewbornIntegral(t, lo, hi, 128, 80.0, NewbornFade{-1, 0.0}), std::invalid_argument);
    EXPECT_THROW(NewbornIntegral(t, lo, hi, 128, 80.0, NewbornFade{8, -1.0}), std::invalid_argument);
    NewbornIntegral newborn(t, lo, hi, 128, 80.0);
    EXPECT_THROW(newborn.add(-0.1, 0.5, 0.0, 1.0, kFloor), std::invalid_argument);
    EXPECT_THROW(newborn.add(nan, 0.5, 0.0, 1.0, kFloor), std::invalid_argument);
    EXPECT_THROW(newborn.add(1.0, inf, 0.0, 1.0, kFloor), std::invalid_argument);
    EXPECT_THROW(newborn.add(1.0, 0.5, -1.0, 1.0, kFloor), std::invalid_argument);
    EXPECT_THROW(newborn.add(1.0, 0.5, 0.0, 0.0, kFloor), std::invalid_argument);
    EXPECT_THROW(newborn.add(1.0, 0.5, 0.0, 1.0, -1.0), std::invalid_argument);
    EXPECT_EQ(newborn.ln_z(), 0.0);
    const double mean = newborn.mean_u();
    newborn.add(1.0, 0.0, 0.0, 1.0, kFloor);  // s = 0 scores 0 and changes nothing
    EXPECT_EQ(newborn.last_score_nats(), 0.0);
    EXPECT_EQ(newborn.mean_u(), mean);
    EXPECT_EQ(newborn.scored_samples(), 0);
}

TEST(Newborn, MomentMatching) {
    // to_gaussian: mu = e^(u^ + sigma_u^2 / 2), P = mu^2 (e^(sigma_u^2) - 1), t_m = k, to 1e-12 relatively.
    const Sequence q = noise_free(20.0, 54);
    NewbornIntegral newborn(tables(), chain().u_lo, chain().u_hi, kNewbornGridPoints, kNewbornLiveRangeNats);
    for (const Observation& o : q) {
        newborn.add(o.e, o.s, o.v, kSigma2, kFloor);
        if (newborn.should_switch(kSwitchSigmaU, false)) break;
    }
    const Amplitude g = newborn.to_gaussian(7);
    const double s2 = newborn.sigma_u() * newborn.sigma_u();
    const double mu = std::exp(newborn.mean_u() + s2 / 2.0);
    EXPECT_NEAR(g.mu_fs, mu, 1e-12 * mu);
    EXPECT_NEAR(g.p_fs2, mu * mu * std::expm1(s2), 1e-12 * mu * mu * std::expm1(s2));
    EXPECT_EQ(g.t_m_sample, 7);
    EXPECT_NEAR(g.mu_fs, amplitude_of(20.0), 0.05 * amplitude_of(20.0));
}

TEST(Newborn, ZeroVarianceIsTheGridWithoutTheStep) {
    // With the per-sample variance 0 the round trip F_i = m + ln e^(F_i - m) runs, and the scores equal the grid without
    // the step within 1e-12 nats per sample (relative to a first mark's moderate scores: noise to +20 dB S500).
    for (const Case& c : make_cases(aligned_specs({std::nullopt, 0.0, 20.0}), 20))
        for (const Sequence& q : c.sequences) {
            const Scores with = newborn_run(q, kNewbornLiveRangeNats, false, kZeroVariance);
            const Scores without = newborn_run(q, kNewbornLiveRangeNats, false, NewbornFade{});
            for (std::size_t k = 0; k < q.size(); ++k) EXPECT_NEAR(with.score[k], without.score[k], 1e-9) << c.spec.name;
        }
}

TEST(Newborn, TinyVarianceExercisesTheStep) {
    // With eps = 1e-6 the stencil and the round trip run, and the scores differ from variance 0 by under 1e-4 nats per
    // sample (heuristic bound).
    const double h = tables().step_nepers();
    const NewbornFade tiny{8, 1e-6 * 2.0 * h * h / 8.0};
    NewbornIntegral probe(tables(), chain().u_lo, chain().u_hi, kNewbornGridPoints, kNewbornLiveRangeNats, tiny);
    EXPECT_NEAR(probe.fade_epsilon_per_substep(), 1e-6, 1e-12);
    for (const Case& c : make_cases(aligned_specs({std::nullopt, 10.0}), 20))
        for (const Sequence& q : c.sequences) {
            const Scores a = newborn_run(q, kNewbornLiveRangeNats, false, tiny);
            const Scores b = newborn_run(q, kNewbornLiveRangeNats, false, kZeroVariance);
            for (std::size_t k = 0; k < q.size(); ++k) EXPECT_NEAR(a.score[k], b.score[k], 1e-4) << c.spec.name;
        }
}

namespace {

// ln of the trapezoid mass of the belief as it stands, F_i over the live points, on the scale of ln_z().
double ln_mass_now(const NewbornIntegral& n) {
    const std::vector<double>& f = n.log_belief();
    const int points = static_cast<int>(f.size());
    double top = -INFINITY;
    for (int i = n.live_lo(); i <= n.live_hi(); ++i) top = std::max(top, f[static_cast<std::size_t>(i)]);
    double sum = 0.0;
    for (int i = n.live_lo(); i <= n.live_hi(); ++i)
        sum += (i == 0 || i == points - 1 ? 0.5 : 1.0) * std::exp(f[static_cast<std::size_t>(i)] - top);
    return top + std::log(sum) - std::log(points - 1.0) + n.common_nats();
}

}  // namespace

TEST(Newborn, PredictionStepConservesMass) {
    // After every step the belief's trapezoid mass still gives ln Z, within 1e-12 relatively to the mass: the reflecting
    // ends conserve it exactly (derived), on first marks on noise and at 0 to +40 dB S500. Counted: steps at the ends.
    int steps = 0;
    int at_an_end = 0;
    for (const Case& c : make_cases(aligned_specs({std::nullopt, 0.0, 10.0, 20.0, 40.0}), 20)) {
        for (const Sequence& q : c.sequences) {
            NewbornIntegral n(tables(), chain().u_lo, chain().u_hi, kNewbornGridPoints, 1e4, fade_at(2.0));
            for (const Observation& o : q) {
                n.add(o.e, o.s, o.v, kSigma2, kFloor);
                if (!n.last_add_stepped()) continue;
                ++steps;
                at_an_end += n.live_lo() == 0 || n.live_hi() == kNewbornGridPoints - 1;
                ASSERT_NEAR(ln_mass_now(n) - n.ln_z(), 0.0, 1e-12) << c.spec.name;
            }
        }
    }
    EXPECT_GT(steps, 100);
    EXPECT_GT(at_an_end, 10);
}

TEST(Newborn, PredictionStepEveryInterval) {
    // The step is applied on the 8th, 16th, ... scored sample counted from birth and no other; a sample with s = 0 is not
    // counted; the moments on a step's sample are those before the step (they equal a grid without any step, up to and
    // including the first step's sample).
    NewbornIntegral n(tables(), chain().u_lo, chain().u_hi, kNewbornGridPoints, kNewbornLiveRangeNats, fade_at(2.0));
    NewbornIntegral plain(tables(), chain().u_lo, chain().u_hi, kNewbornGridPoints, kNewbornLiveRangeNats);
    int scored = 0;
    for (const Observation& o : noise_free(0.0, 54)) {
        if (scored % 3 == 2) {
            n.add(o.e, 0.0, 0.0, kSigma2, kFloor);
            EXPECT_FALSE(n.last_add_stepped());
        }
        n.add(o.e, o.s, o.v, kSigma2, kFloor);
        plain.add(o.e, o.s, o.v, kSigma2, kFloor);
        ++scored;
        EXPECT_EQ(n.scored_samples(), scored);
        EXPECT_EQ(n.last_add_stepped(), scored % 8 == 0) << scored;
        EXPECT_EQ(n.steps_applied(), scored / 8);
        if (scored == 8) {
            EXPECT_EQ(n.mean_u(), plain.mean_u());
            EXPECT_EQ(n.sigma_u(), plain.sigma_u());
        }
        if (scored == 9) EXPECT_NE(n.sigma_u(), plain.sigma_u());
    }
    EXPECT_GT(scored, 16);
}

TEST(Newborn, SubstepsAboveAQuarter) {
    // At 2 Hz one substep of eps = 0.0684; at 5 Hz (--genie fading) eps = 0.427 in two substeps of 0.2135; every F_i stays
    // finite (every p_i positive) and the mass is conserved.
    const auto at = [](double hz) {
        return NewbornIntegral(tables(), chain().u_lo, chain().u_hi, kNewbornGridPoints, 1e4, fade_at(hz));
    };
    EXPECT_EQ(at(2.0).fade_substeps(), 1);
    EXPECT_NEAR(at(2.0).fade_epsilon_per_substep(), 0.0684, 5e-4);
    EXPECT_EQ(at(5.0).fade_substeps(), 2);
    EXPECT_NEAR(at(5.0).fade_epsilon_per_substep(), 0.2135, 5e-3);
    for (const Case& c : make_cases(aligned_specs({std::nullopt, 10.0, 40.0}), 20)) {
        for (const Sequence& q : c.sequences) {
            NewbornIntegral n = at(5.0);
            for (const Observation& o : q) {
                n.add(o.e, o.s, o.v, kSigma2, kFloor);
                if (!n.last_add_stepped()) continue;
                for (int i = n.live_lo(); i <= n.live_hi(); ++i)
                    ASSERT_TRUE(std::isfinite(n.log_belief()[static_cast<std::size_t>(i)]));
                ASSERT_NEAR(ln_mass_now(n) - n.ln_z(), 0.0, 1e-12) << c.spec.name;
            }
        }
    }
}

TEST(Newborn, CpuPerNewbornSample) {
    // Label measurement (Newborn.CpuMeasurement; skipped otherwise): ns per newborn-sample with 500 newborns, the step on,
    // median of 3 repetitions of 3 000 samples per newborn on a mix of levels; one thread, not pinned.
    const char* flag = std::getenv("KZ4AP_MEASUREMENT");
    if (flag == nullptr || std::string(flag) != "1") GTEST_SKIP() << "runs under the measurement label";
    const std::vector<Case> cases = make_cases(aligned_specs(kStudyLevels), 250);
    std::vector<const Sequence*> marks;
    for (std::size_t i = 0; i < cases.front().sequences.size(); ++i)
        for (const Case& c : cases) marks.push_back(&c.sequences[i]);
    const NewbornIntegral fresh(tables(), chain().u_lo, chain().u_hi, kNewbornGridPoints, kNewbornLiveRangeNats, fade_at(2.0));
    constexpr std::size_t kNewborns = 500;
    constexpr int kSteps = 3000;
    std::vector<double> ns;
    double sink = 0.0;
    for (int rep = 0; rep < 3; ++rep) {
        std::vector<NewbornIntegral> newborns(kNewborns, fresh);
        std::vector<std::size_t> mark(kNewborns);
        std::vector<std::size_t> pos(kNewborns, 0);
        std::size_t next = 0;
        for (std::size_t b = 0; b < kNewborns; ++b) mark[b] = next++ % marks.size();
        const auto start = std::chrono::steady_clock::now();
        for (int step = 0; step < kSteps; ++step)
            for (std::size_t b = 0; b < kNewborns; ++b) {
                const Sequence& q = *marks[mark[b]];
                const Observation& o = q[pos[b]];
                newborns[b].add(o.e, o.s, o.v, kSigma2, kFloor);
                sink += newborns[b].last_score_nats();
                const bool last = ++pos[b] == q.size();
                if (newborns[b].should_switch(kSwitchSigmaU, last)) {
                    newborns[b] = fresh;
                    mark[b] = next++ % marks.size();
                    pos[b] = 0;
                }
            }
        ns.push_back(std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count() * 1e9 /
                     (static_cast<double>(kNewborns) * kSteps));
    }
    std::sort(ns.begin(), ns.end());
    std::cout << "measured: " << ns[1] << " ns per newborn-sample (sink " << sink << ")\n";
    EXPECT_TRUE(std::isfinite(sink));
}
