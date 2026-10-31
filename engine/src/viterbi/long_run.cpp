// The long-run amplitude: Omega_hat, nu and the default Omega_bar (long_run.hpp; design notes, part 7; plan 1b,
// Task 7); the noise-in-Omega_bar variant's sums, direct integral and table (plan 1b, Task 8).
// This file must not read a generated table: kz4ap-viterbi-tables links it to build them (Ruling 76).
#include "kz4ap/viterbi/long_run.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <limits>
#include <numbers>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace kz4ap::viterbi {

namespace {

bool finite_positive(double x) { return x > 0.0 && std::isfinite(x); }

// One step of a compensated sum (Neumaier).
void compensated_add(double& sum, double& carry, double x) {
    const double t = sum + x;
    carry += std::fabs(sum) >= std::fabs(x) ? (sum - t) + x : (x - t) + sum;
    sum = t;
}

}  // namespace

// --- LongRun ---

void LongRun::Sum::add(double x) { compensated_add(sum, carry, x); }

LongRun::LongRun(double spread_hz, int bin_samples, int bins_kept, double sample_interval_s)
    : bin_samples_(bin_samples), bins_kept_(bins_kept) {
    if (!(spread_hz >= 0.0) || !std::isfinite(spread_hz))
        throw std::invalid_argument("LongRun: the fading spread must be finite and not negative (Hz)");
    if (bin_samples < 1 || bins_kept < 1)
        throw std::invalid_argument("LongRun: bin_samples and bins_kept must be at least 1");
    if (!finite_positive(sample_interval_s))
        throw std::invalid_argument("LongRun: the sample interval must be positive and finite (s)");
    const double bin_s = bin_samples * sample_interval_s;
    rho2_.resize(static_cast<std::size_t>(bins_kept) + 1);
    for (int m = 0; m <= bins_kept; ++m) {
        const double tau = m * bin_s;  // s: between the centers of bins m apart
        rho2_[static_cast<std::size_t>(m)] =
            std::exp(-std::numbers::pi * std::numbers::pi * spread_hz * spread_hz * tau * tau);
    }
    kept_bin_.assign(static_cast<std::size_t>(bins_kept), 0);
    kept_w_.assign(static_cast<std::size_t>(bins_kept), 0.0);
}

void LongRun::restart() {
    numerator_ = {};
    s4_ = {};
    s2_ = {};
    closed_w_ = {};
    closed_pairs_ = {};
    open_ = false;
    open_bin_ = 0;
    open_w_ = 0.0;
    open_cross_ = 0.0;
    kept_next_ = 0;
    kept_count_ = 0;
}

void LongRun::at_sample(std::int64_t k) {
    // floor(k / bin_samples), for negative k too
    const std::int64_t bin = k >= 0 ? k / bin_samples_ : -((-k + bin_samples_ - 1) / bin_samples_);
    if (open_ && bin == open_bin_) return;
    if (open_) {
        if (bin < open_bin_)
            throw std::invalid_argument("LongRun::at_sample: a sample in an earlier bin than the open one");
        // Close the open bin: its own term and its products with the kept bins (fixed while it was open).
        if (open_w_ != 0.0) {
            closed_w_.add(open_w_);
            closed_pairs_.add(open_w_ * open_w_ + 2.0 * open_w_ * open_cross_);
            kept_bin_[static_cast<std::size_t>(kept_next_)] = open_bin_;
            kept_w_[static_cast<std::size_t>(kept_next_)] = open_w_;
            kept_next_ = (kept_next_ + 1) % bins_kept_;
            kept_count_ = std::min(kept_count_ + 1, bins_kept_);
        }
    }
    open_ = true;
    open_bin_ = bin;
    open_w_ = 0.0;
    // C of the new bin: the kept bins within bins_kept bins before it (any other weighted bin is further back).
    double cross = 0.0;
    for (int i = 0; i < kept_count_; ++i) {
        const std::int64_t apart = bin - kept_bin_[static_cast<std::size_t>(i)];
        if (apart >= 1 && apart <= bins_kept_)
            cross += kept_w_[static_cast<std::size_t>(i)] * rho2_[static_cast<std::size_t>(apart)];
    }
    open_cross_ = cross;
}

void LongRun::add(double s, double e_fs, double sigma2_fs2) {
    if (!open_) throw std::logic_error("LongRun::add: no at_sample() since the restart");
    if (s == 0.0) return;
    const double s2 = s * s;
    const double s4 = s2 * s2;
    numerator_.add(s2 * (e_fs * e_fs - sigma2_fs2));
    s4_.add(s4);
    s2_.add(s2);
    open_w_ += s4;
}

double LongRun::omega_hat_fs2() const {
    const double s4 = s4_.value();
    return s4 > 0.0 ? numerator_.value() / s4 : 0.0;
}

double LongRun::nu() const {
    const double w = closed_w_.value() + open_w_;
    if (!(w > 0.0)) return 1.0;
    const double pairs = closed_pairs_.value() + open_w_ * open_w_ + 2.0 * open_w_ * open_cross_;
    return w * w / pairs;
}

double LongRun::sum_s4() const { return s4_.value(); }
double LongRun::sum_s2() const { return s2_.value(); }

// --- The closed form ---

namespace {

// The n-point Gauss-Legendre rule on [-1, 1]: nodes by Newton's method on P_n from the Chebyshev guesses, to rounding.
struct GaussLegendre {
    static constexpr int kN = 10;
    std::array<double, kN> x{};
    std::array<double, kN> w{};
    GaussLegendre() {
        for (int i = 0; i < kN; ++i) {
            double z = std::cos(std::numbers::pi * (i + 0.75) / (kN + 0.5));
            double dp = 0.0;
            for (int iteration = 0; iteration < 100; ++iteration) {
                double p0 = 1.0;
                double p1 = z;
                for (int k = 2; k <= kN; ++k) {
                    const double p2 = ((2.0 * k - 1.0) * z * p1 - (k - 1.0) * p0) / k;
                    p0 = p1;
                    p1 = p2;
                }
                dp = kN * (z * p1 - p0) / (z * z - 1.0);
                const double step = p1 / dp;
                z -= step;
                if (std::fabs(step) < 1e-16) break;
            }
            x[static_cast<std::size_t>(i)] = z;
            w[static_cast<std::size_t>(i)] = 2.0 / ((1.0 - z * z) * dp * dp);
        }
    }
};

const GaussLegendre& gauss_legendre() {
    static const GaussLegendre rule;
    return rule;
}

// The panel length's scale (header: omega_bar_closed_form; heuristic, its accuracy measured against half of it).
constexpr double kPanel = 2.0;

// E(a) = integral_0^l e^(-a t) dt = (1 - e^(-a l)) / a, l at a = 0.
double e_integral(double a, double l) { return a == 0.0 ? l : -std::expm1(-a * l) / a; }

// Omega_bar / Omega_hi for nu (> 0), b = beta / Omega_hi (>= 0) and l = ln(Omega_hi / Omega_lo) (header, stage 3).
double mean_ratio(double nu, double b, double l) {
    if (b == 0.0) return std::exp(-l) * e_integral(nu - 1.0, l) / e_integral(nu, l);
    if (std::isinf(b)) return 1.0;  // the posterior at the prior's top
    // y = ln(Omega / Omega_hi) on [-l, 0]; the log-density g(y) = -nu y - b e^(-y) is concave, its peak at
    // ln(b / nu) clamped to the interval.
    const auto g = [&](double y) { return -nu * y - b * std::exp(-y); };
    const double mode = std::clamp(std::log(b / nu), -l, 0.0);
    const double peak = g(mode);
    const double cut = 45.0 + l;  // nats below the peak (header)
    // The ends where g = peak - cut (or the interval's), by bisection on each monotone side.
    const auto end = [&](double inner, double outer) {
        if (g(outer) >= peak - cut) return outer;
        for (int i = 0; i < 200 && std::fabs(outer - inner) > 1e-13; ++i) {
            const double mid = 0.5 * (inner + outer);
            (g(mid) >= peak - cut ? inner : outer) = mid;
        }
        return outer;
    };
    const double from = end(mode, -l);
    const double to = end(mode, 0.0);
    const GaussLegendre& rule = gauss_legendre();
    double num = 0.0;
    double den = 0.0;
    double y = from;
    while (y < to) {
        const double curvature = b * std::exp(-y);          // |g''| at the panel's start, its largest in the panel
        const double slope = std::fabs(curvature - nu);     // |g'| there
        const double h = std::min(kPanel / (1.0 + slope + std::sqrt(curvature)), to - y);
        const double half = 0.5 * h;
        const double center = y + half;
        for (int i = 0; i < GaussLegendre::kN; ++i) {
            const double yi = center + half * rule.x[static_cast<std::size_t>(i)];
            const double f = half * rule.w[static_cast<std::size_t>(i)] * std::exp(g(yi) - peak);
            den += f;
            num += f * std::exp(yi);
        }
        y += h;
    }
    return num / den;
}

double omega_hi_per_sigma2(double hi_db) {
    return std::pow(10.0, hi_db / 10.0) * kS500BandwidthHz / kOmegaBarSigma2PerN0Hz;
}

void check_bounds(double lo_db, double hi_db, const char* who) {
    if (!std::isfinite(lo_db) || !std::isfinite(hi_db) || !(lo_db < hi_db))
        throw std::invalid_argument(std::string(who) +
                                    ": the prior's bounds must be finite with lo_db < hi_db (dB S500)");
}

}  // namespace

double omega_bar_closed_form(double nu, double omega_hat_fs2, double sigma2_fs2, double lo_db, double hi_db) {
    if (!(nu > 0.0)) throw std::invalid_argument("omega_bar_closed_form: nu must be positive");
    if (!finite_positive(sigma2_fs2))
        throw std::invalid_argument("omega_bar_closed_form: sigma_y^2 must be positive and finite (FS^2)");
    if (std::isnan(omega_hat_fs2)) throw std::invalid_argument("omega_bar_closed_form: Omega_hat is not a number");
    check_bounds(lo_db, hi_db, "omega_bar_closed_form");
    const double hi = omega_hi_per_sigma2(hi_db) * sigma2_fs2;  // FS^2
    const double l = (hi_db - lo_db) / 10.0 * std::numbers::ln10;
    // nu = infinity: no fading scatter, so Omega_bar is Omega_hat_+ clipped to the prior's range.
    if (std::isinf(nu)) return std::clamp(std::max(omega_hat_fs2, 0.0), hi * std::exp(-l), hi);
    return hi * mean_ratio(nu, nu * std::max(omega_hat_fs2, 0.0) / hi, l);
}

// --- The table ---

double OmegaBarTable::p_of_nu(double nu) {
    if (!(nu > 1.0)) return 1.0;
    if (std::isinf(nu)) return 0.0;
    return 1.0 / (1.0 + std::sqrt(nu - 1.0));
}

double OmegaBarTable::nu_of_p(double p) {
    const double r = (1.0 - p) / p;
    return 1.0 + r * r;
}

OmegaBarTable::OmegaBarTable(double lo_db, double hi_db, const OmegaBarLayout& layout, std::vector<double> zero_row,
                             std::vector<double> values)
    : lo_db_(lo_db), hi_db_(hi_db), layout_(layout), zero_row_(std::move(zero_row)), values_(std::move(values)) {
    check_bounds(lo_db, hi_db, "OmegaBarTable");
    l_ = (hi_db - lo_db) / 10.0 * std::numbers::ln10;
    hi_per_sigma2_ = omega_hi_per_sigma2(hi_db);
    const double w = layout.fine_half_width;
    const double m = layout.medium_half_width;
    if (layout.p_nodes < 2 || layout.zero_row_nodes < 2 || !finite_positive(layout.below_lo) || !finite_positive(w) ||
        !finite_positive(layout.fine_step) || !finite_positive(layout.medium_step) ||
        !finite_positive(layout.coarse_step) || !std::isfinite(layout.z_max) || !(m > w) || !(layout.below_lo > m) ||
        !(layout.z_max > m) || !(l_ > 2.0 * m))
        throw std::invalid_argument("OmegaBarTable: a layout that cannot be built for these bounds");
    z_min_ = -l_ - layout.below_lo;
    ratio_min_ = std::exp(z_min_);
    // Fine within w of each corner, medium out to m, coarse beyond (header, stage 4).
    breaks_ = {z_min_, -l_ - m, -l_ - w, -l_ + w, -l_ + m, -m, -w, w, m, layout.z_max};
    const double fine = layout.fine_step;
    const double medium = layout.medium_step;
    const double coarse = layout.coarse_step;
    const std::array<double, kSegments> steps = {coarse, medium, fine, medium, coarse, medium, fine, medium, coarse};
    z_nodes_.clear();
    for (std::size_t s = 0; s < kSegments; ++s) {
        const double length = breaks_[s + 1] - breaks_[s];
        const int count = std::max(1, static_cast<int>(std::ceil(length / steps[s] - 1e-9)));
        step_[s] = length / count;
        first_[s] = static_cast<int>(z_nodes_.size());
        for (int i = 0; i < count; ++i) z_nodes_.push_back(breaks_[s] + i * step_[s]);
    }
    first_[kSegments] = static_cast<int>(z_nodes_.size());
    z_nodes_.push_back(breaks_[kSegments]);
    const std::size_t p = static_cast<std::size_t>(layout.p_nodes);
    // An empty table is allowed only while it is being built (build() fills it).
    const bool empty = zero_row_.empty() && values_.empty();
    if (!empty && (zero_row_.size() != static_cast<std::size_t>(layout.zero_row_nodes) ||
                   values_.size() != p * z_nodes_.size()))
        throw std::invalid_argument("OmegaBarTable: the values do not fit the layout");
}

OmegaBarTable OmegaBarTable::build(double lo_db, double hi_db, const OmegaBarLayout& layout) {
    OmegaBarTable table(lo_db, hi_db, layout, {}, {});
    const std::size_t pn = static_cast<std::size_t>(layout.p_nodes);
    const std::size_t zn = table.z_nodes_.size();
    table.zero_row_.resize(static_cast<std::size_t>(layout.zero_row_nodes));
    for (std::size_t i = 0; i < table.zero_row_.size(); ++i) {
        const double p = static_cast<double>(i) / (layout.zero_row_nodes - 1);
        table.zero_row_[i] = p == 0.0 ? -table.l_ : std::log(mean_ratio(nu_of_p(p), 0.0, table.l_));
    }
    table.values_.resize(pn * zn);
    for (std::size_t i = 0; i < pn; ++i) {
        const double p = table.p_node(static_cast<int>(i));
        // p = 0 is nu = infinity: Omega_bar is Omega_hat clipped to the prior's range.
        const double nu = p == 0.0 ? std::numeric_limits<double>::infinity() : nu_of_p(p);
        for (std::size_t j = 0; j < zn; ++j) {
            const double z = table.z_nodes_[j];
            table.values_[i * zn + j] =
                p == 0.0 ? std::clamp(z, -table.l_, 0.0) : std::log(mean_ratio(nu, nu * std::exp(z), table.l_));
        }
    }
    return table;
}

double OmegaBarTable::omega_bar_fs2(double nu, double omega_hat_fs2, double sigma2_fs2) const {
    if (!finite_positive(sigma2_fs2))
        throw std::invalid_argument("OmegaBarTable::omega_bar_fs2: sigma_y^2 must be positive and finite (FS^2)");
    const double hi = hi_per_sigma2_ * sigma2_fs2;  // FS^2
    const double p = p_of_nu(nu);                   // 1 for nu < 1 or not a number
    const int last_p = layout_.p_nodes - 1;
    const double position = p * last_p;
    const int i = std::min(static_cast<int>(position), last_p - 1);
    const double fp = position - i;
    const double ratio = omega_hat_fs2 / hi;  // Omega_hat / Omega_hi; not a number reads as 0 below
    double value;
    if (!(ratio > ratio_min_)) {
        // Omega_hat at or below e^z_min Omega_hi: the exact beta = 0 row.
        const int last_row = layout_.zero_row_nodes - 1;
        const double at = p * last_row;
        const int r = std::min(static_cast<int>(at), last_row - 1);
        const double a = zero_row_[static_cast<std::size_t>(r)];
        value = a + (at - r) * (zero_row_[static_cast<std::size_t>(r) + 1] - a);
    } else {
        const double z = std::min(std::log(ratio), layout_.z_max);
        std::size_t s = 0;
        while (s + 1 < kSegments && z >= breaks_[s + 1]) ++s;
        const double offset = (z - breaks_[s]) / step_[s];
        const int within = std::min(static_cast<int>(offset), first_[s + 1] - first_[s] - 1);
        const double fz = std::min(offset - within, 1.0);
        const std::size_t j = static_cast<std::size_t>(first_[s] + within);
        const std::size_t zn = z_nodes_.size();
        const double* row0 = values_.data() + static_cast<std::size_t>(i) * zn + j;
        const double* row1 = row0 + zn;
        const double v0 = row0[0] + fz * (row0[1] - row0[0]);
        const double v1 = row1[0] + fz * (row1[1] - row1[0]);
        value = v0 + fp * (v1 - v0);
    }
    return hi * std::exp(value);
}

// --- The file ---

namespace {

constexpr char kMagic[8] = {'K', 'Z', '4', 'A', 'P', 'O', 'B', 'T'};
constexpr std::uint32_t kVersion = 1;

void put(std::ofstream& out, const void* data, std::size_t bytes) {
    out.write(static_cast<const char*>(data), static_cast<std::streamsize>(bytes));
}

void get(std::ifstream& in, void* data, std::size_t bytes, const std::string& path) {
    in.read(static_cast<char*>(data), static_cast<std::streamsize>(bytes));
    if (!in) throw std::runtime_error("OmegaBarTable::load: " + path + " is not a default Omega_bar table (too short)");
}

}  // namespace

void OmegaBarTable::save(const std::string& path) const {
    std::ofstream out(path, std::ios::binary);
    if (!out) throw std::runtime_error("OmegaBarTable::save: cannot write " + path);
    // The format: the magic, version 1, the bounds (dB S500), the layout (its doubles, then
    // p_nodes and zero_row_nodes), the count of z nodes, the beta = 0 row and the values
    // (p-major), doubles and 32-bit integers in the machine's byte order (little-endian on every supported platform).
    put(out, kMagic, sizeof kMagic);
    put(out, &kVersion, sizeof kVersion);
    const std::uint32_t p_nodes = static_cast<std::uint32_t>(layout_.p_nodes);
    const std::uint32_t zero_row_nodes = static_cast<std::uint32_t>(layout_.zero_row_nodes);
    const std::uint64_t z_count = z_nodes_.size();
    const std::array<double, 9> header = {lo_db_,           hi_db_,
                                          layout_.below_lo, layout_.z_max,
                                          layout_.fine_half_width, layout_.fine_step,
                                          layout_.medium_half_width, layout_.medium_step,
                                          layout_.coarse_step};
    put(out, header.data(), sizeof header);
    put(out, &p_nodes, sizeof p_nodes);
    put(out, &zero_row_nodes, sizeof zero_row_nodes);
    put(out, &z_count, sizeof z_count);
    put(out, zero_row_.data(), zero_row_.size() * sizeof(double));
    put(out, values_.data(), values_.size() * sizeof(double));
    if (!out) throw std::runtime_error("OmegaBarTable::save: cannot write " + path);
}

OmegaBarTable OmegaBarTable::load(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("OmegaBarTable::load: cannot read " + path);
    char magic[8];
    std::uint32_t version = 0;
    get(in, magic, sizeof magic, path);
    get(in, &version, sizeof version, path);
    if (std::memcmp(magic, kMagic, sizeof magic) != 0 || version != kVersion)
        throw std::runtime_error("OmegaBarTable::load: " + path + " is not a default Omega_bar table of version 1");
    std::array<double, 9> header{};
    std::uint32_t p_nodes = 0;
    std::uint32_t zero_row_nodes = 0;
    std::uint64_t z_count = 0;
    get(in, header.data(), sizeof header, path);
    get(in, &p_nodes, sizeof p_nodes, path);
    get(in, &zero_row_nodes, sizeof zero_row_nodes, path);
    get(in, &z_count, sizeof z_count, path);
    OmegaBarLayout layout;
    layout.p_nodes = static_cast<int>(p_nodes);
    layout.zero_row_nodes = static_cast<int>(zero_row_nodes);
    layout.below_lo = header[2];
    layout.z_max = header[3];
    layout.fine_half_width = header[4];
    layout.fine_step = header[5];
    layout.medium_half_width = header[6];
    layout.medium_step = header[7];
    layout.coarse_step = header[8];
    if (p_nodes < 2 || p_nodes > 100000 || zero_row_nodes < 2 || zero_row_nodes > 100000 || z_count < 2 ||
        z_count > 10000000 || p_nodes * z_count > 100000000)
        throw std::runtime_error("OmegaBarTable::load: " + path + " has an impossible layout");
    std::vector<double> zero_row(zero_row_nodes);
    std::vector<double> values(static_cast<std::size_t>(p_nodes) * z_count);
    get(in, zero_row.data(), zero_row.size() * sizeof(double), path);
    get(in, values.data(), values.size() * sizeof(double), path);
    try {
        OmegaBarTable table(header[0], header[1], layout, std::move(zero_row), std::move(values));
        if (table.z_nodes_.size() != z_count) throw std::invalid_argument("the z nodes do not match");
        return table;
    } catch (const std::invalid_argument& e) {
        throw std::runtime_error("OmegaBarTable::load: " + path + ": " + e.what());
    }
}


// --- The noise-in-Omega_bar variant: the sums (stage 5) ---

void NoiseVarianceSums::Sum::add(double x) { compensated_add(sum, carry, x); }

void NoiseVarianceSums::restart() {
    a2_ = {};
    b2_ = {};
    s2_ = {};
    s3_ = {};
}

void NoiseVarianceSums::add(double s, const std::array<double, 7>& rho) {
    const double s2 = s * s;
    const double s3 = s2 * s;
    if (s != 0.0) {
        double cross_a = 0.0;  // sum_{m=1..6} rho_m^2 s_(k-m)^2
        double cross_b = 0.0;  // sum_{m=1..6} rho_m s_(k-m)^3
        for (std::size_t m = 1; m <= 6; ++m) {
            cross_a += rho[m] * rho[m] * s2_[m - 1];
            cross_b += rho[m] * s3_[m - 1];
        }
        a2_.add(s2 * (s2 + 2.0 * cross_a));
        b2_.add(s3 * (s3 + 2.0 * cross_b));
    }
    for (std::size_t m = s2_.size() - 1; m > 0; --m) {
        s2_[m] = s2_[m - 1];
        s3_[m] = s3_[m - 1];
    }
    s2_[0] = s2;
    s3_[0] = s3;
}

// --- The variant's direct integral (stage 6) ---

namespace {

constexpr double kPriorLoDb = -15.0;  // dB S500: the default's prior bounds (stage 3), the variant's
constexpr double kPriorHiDb = 85.0;
constexpr double kInf = std::numeric_limits<double>::infinity();
constexpr double kTiny = 1e-300;

// h of the continued fraction Gamma(a, x) = e^(-x) x^a h, x > 0 (Lentz's method; Numerical Recipes 6.2), used where
// x >= a + 1 and, for a < 0.5, where x >= 1.
double gamma_continued_fraction(double a, double x) {
    double b = x + 1.0 - a;
    double c = 1.0 / kTiny;
    double d = 1.0 / b;
    double h = d;
    for (int i = 1; i < 100000; ++i) {
        const double an = -i * (i - a);
        b += 2.0;
        d = an * d + b;
        if (std::fabs(d) < kTiny) d = kTiny;
        c = b + an / c;
        if (std::fabs(c) < kTiny) c = kTiny;
        d = 1.0 / d;
        const double delta = d * c;
        h *= delta;
        if (std::fabs(delta - 1.0) < 1e-16) break;
    }
    return h;
}

// ln P(a, x) and ln Q(a, x), the regularized incomplete gamma functions, for a > 0 and x >= 0, lgamma_a = ln Gamma(a):
// the series for x < a + 1, the continued fraction otherwise (Numerical Recipes 6.2), in logs so that neither
// underflows.
struct LogPQ {
    double log_p;
    double log_q;
};

LogPQ log_gamma_pq(double a, double x, double lgamma_a) {
    if (!(x > 0.0)) return {-kInf, 0.0};
    if (std::isinf(x)) return {0.0, -kInf};
    const double log_prefactor = a * std::log(x) - x - lgamma_a;
    if (x < a + 1.0) {
        // P = e^(-x) x^a / Gamma(a) x sum_n x^n / (a (a + 1) ... (a + n))
        double ap = a;
        double term = 1.0 / a;
        double sum = term;
        for (int n = 0; n < 100000; ++n) {
            ap += 1.0;
            term *= x / ap;
            sum += term;
            if (term < sum * 1e-17) break;
        }
        const double log_p = std::log(sum) + log_prefactor;
        return {log_p, std::log1p(-std::exp(log_p))};
    }
    const double log_q = std::log(gamma_continued_fraction(a, x)) + log_prefactor;
    return {std::log1p(-std::exp(log_q)), log_q};
}

// ln(1 - e^d) for d <= 0.
double log1m_exp(double d) {
    if (d == -kInf) return 0.0;
    if (!(d < 0.0)) return -kInf;  // a window that rounding closed
    return d > -std::numbers::ln2 ? std::log(-std::expm1(d)) : std::log1p(-std::exp(d));
}

// ln(P(a, x2) - P(a, x1)) for 0 < x1 < x2, from the pair of P and Q that does not cancel.
double log_gamma_window(double a, double x1, double x2, double lgamma_a) {
    const LogPQ one = log_gamma_pq(a, x1, lgamma_a);
    const LogPQ two = log_gamma_pq(a, x2, lgamma_a);
    if (x2 <= a) return two.log_p + log1m_exp(one.log_p - two.log_p);
    if (x1 >= a) return one.log_q + log1m_exp(two.log_q - one.log_q);
    const double window = std::exp(one.log_q) - std::exp(two.log_q);
    return window > 0.0 ? std::log(window) : -kInf;
}

// integral from x1 to x2 of t^(s - 1) e^(-t) dt for 0 <= s < 0.5 and 0 < x1 < x2, unregularized (finite at s = 0,
// where Gamma(s) is not): below t = 1 the series sum_n (-1)^n / n! (x2^(s + n) - x1^(s + n)) / (s + n), its n = 0 term
// x1^s expm1(s ln(x2 / x1)) / s (ln(x2 / x1) at s = 0); above it Gamma(s, x) = e^(-x) x^s h by the continued fraction.
double gamma_window_small(double s, double x1, double x2) {
    const auto upper = [s](double x) { return std::exp(s * std::log(x) - x) * gamma_continued_fraction(s, x); };
    const auto series = [s](double lo, double hi) {
        const double l = std::log(hi / lo);
        double power_lo = std::exp(s * std::log(lo));
        double power_hi = std::exp(s * std::log(hi));
        double sum = s == 0.0 ? l : power_lo * std::expm1(s * l) / s;
        double factor = 1.0;  // (-1)^n / n!
        for (int n = 1; n < 60; ++n) {
            factor *= -1.0 / n;
            power_lo *= lo;
            power_hi *= hi;
            const double term = factor * (power_hi - power_lo) / (s + n);
            sum += term;
            if (std::fabs(term) < 1e-17 * std::fabs(sum)) break;
        }
        return sum;
    };
    if (x1 >= 1.0) return upper(x1) - upper(x2);
    if (x2 <= 1.0) return series(x1, x2);
    return series(x1, 1.0) + upper(1.0) - upper(x2);
}

// The gamma's partial moments over the window G in (P / Omega_hi, P / Omega_lo), G ~ Gamma(shape nu, mean 1), in
// logs (stage 6): ln Pr[window] = ln(P(nu, nu P / Omega_hi ... nu P / Omega_lo)) and ln E[1/G; window] =
// ln(nu / (nu - 1)) + the same window of P(nu - 1, .) for nu - 1 >= 0.5, or ln nu - ln Gamma(nu) + ln of the
// unregularized window of t^(nu - 2) e^(-t) below; at nu = infinity G = 1 (both 1 inside the prior, 0 outside).
class GammaMoments {
public:
    explicit GammaMoments(double nu)
        : nu_(nu), fixed_(std::isinf(nu)), s_(nu - 1.0), lgamma_nu_(fixed_ ? 0.0 : std::lgamma(nu)),
          lgamma_s_(s_ >= 0.5 && !fixed_ ? std::lgamma(s_) : 0.0),
          log_ratio_(s_ >= 0.5 && !fixed_ ? std::log(nu / s_) : std::log(nu) - lgamma_nu_) {}
    // log_m0 = ln Pr[window], log_m1 = ln E[1/G; window] at realized power p for the prior's bounds lo < hi (all in
    // one unit).
    void at(double p, double lo, double hi, double& log_m0, double& log_m1) const {
        if (fixed_) {
            log_m0 = log_m1 = (p > lo && p < hi) ? 0.0 : -kInf;
            return;
        }
        const double x1 = nu_ * p / hi;
        const double x2 = nu_ * p / lo;
        log_m0 = log_gamma_window(nu_, x1, x2, lgamma_nu_);
        if (s_ >= 0.5) {
            log_m1 = log_ratio_ + log_gamma_window(s_, x1, x2, lgamma_s_);
        } else {
            const double window = gamma_window_small(s_, x1, x2);
            log_m1 = window > 0.0 ? log_ratio_ + std::log(window) : -kInf;
        }
    }

private:
    double nu_;
    bool fixed_;
    double s_;
    double lgamma_nu_;
    double lgamma_s_;
    double log_ratio_;
};

// The integration's heuristic constants (stage 6).
constexpr double kPanelU = 0.25;          // the initial panels' length in u = ln(P / SE0)
constexpr double kSmallestP = 1e-16;      // SE0: the integrals' lower end
constexpr double kDroppedNats = 90.0;     // panels whose ends are this far below the peak are left out
constexpr double kTolerance = 1e-11;      // relative: the halving's estimated error at the end, each integral
constexpr std::size_t kMostPanels = 20000;

// Omega_bar / SE0 for nu, chi = Omega_hat / SE0, lambda = B / SE0 and the prior's bounds lo < hi in SE0 (stage 6).
double variant_mean(double nu, double chi, double lambda, double lo, double hi) {
    const GammaMoments moments(nu);
    const bool fixed = std::isinf(nu);
    // ln of the two integrands in u: n(u) = phi(P) E[1/G; window] P and d(u) = phi(P) Pr[window] (dP = P du), phi
    // without its constant 1 / sqrt(2 pi).
    const auto evaluate = [&](double u, double& ln_n, double& ln_d) {
        const double p = std::exp(u);
        const double v = 1.0 + lambda * p;
        const double r = chi - p;
        const double ln_phi = -r * r / (2.0 * v) - 0.5 * std::log(v);
        double m0 = 0.0;
        double m1 = 0.0;
        moments.at(p, lo, hi, m0, m1);
        ln_n = ln_phi + m1 + u;
        ln_d = ln_phi + m0;
    };
    // The range: from kSmallestP to where phi is below e^-700 of its peak (or 4 Omega_lo, beyond which the window is
    // near 1 and phi falls); at nu = infinity strictly inside the prior, where G = 1 gives 1 (its ends can hold the
    // peak).
    const double chi_plus = std::max(chi, 0.0);
    const double p_phi = chi_plus + 1500.0 * lambda + 40.0 * std::sqrt(1.0 + lambda * chi_plus) + 100.0;
    double u_lo = std::log(kSmallestP);
    double u_hi = std::log(std::max(p_phi, 4.0 * lo));
    if (fixed) {
        u_lo = std::log(lo);
        while (!(std::exp(u_lo) > lo)) u_lo = std::nextafter(u_lo, kInf);
        u_hi = std::min(std::log(hi), std::max(u_hi, u_lo + 1.0));
        while (!(std::exp(u_hi) < hi)) u_hi = std::nextafter(u_hi, -kInf);
    }
    // The breakpoints, each with both log-integrands.
    struct Point {
        double u;
        double ln_n;
        double ln_d;
    };
    std::vector<Point> points;
    const auto add = [&](double u) {
        if (u >= u_lo && u <= u_hi) {
            Point point{u, 0.0, 0.0};
            evaluate(u, point.ln_n, point.ln_d);
            points.push_back(point);
        }
    };
    const auto tidy = [&] {
        std::sort(points.begin(), points.end(), [](const Point& a, const Point& b) { return a.u < b.u; });
        points.erase(std::unique(points.begin(), points.end(),
                                 [](const Point& a, const Point& b) { return b.u - a.u < 1e-12; }),
                     points.end());
    };
    const int count = std::max(1, static_cast<int>(std::ceil((u_hi - u_lo) / kPanelU)));
    for (int i = 0; i <= count; ++i) add(i == count ? u_hi : u_lo + (u_hi - u_lo) * i / count);
    // phi's peak, where it is narrower than the panels: about P = chi, of width sqrt(1 + lambda chi) in P.
    if (chi > 0.0) {
        const double width = std::sqrt(1.0 + lambda * chi);
        if (width / chi < kPanelU) {
            for (const double k : {-24.0, -16.0, -12.0, -8.0, -6.0, -4.0, -3.0, -2.0, -1.0, -0.5, 0.0, 0.5, 1.0, 2.0,
                                   3.0, 4.0, 6.0, 8.0, 12.0, 16.0, 24.0}) {
                const double p = chi + k * width;
                if (p > 0.0) add(std::log(p));
            }
        }
    }
    // The prior's bounds, and above nu = 16 the window's edges about them (width 1 / sqrt(nu) in u).
    for (const double edge : {lo, hi}) {
        add(std::log(edge));
        if (!fixed && nu > 1.0 / (kPanelU * kPanelU)) {
            for (int k = -6; k <= 6; ++k) {
                const double q = 1.0 + k / std::sqrt(nu);
                if (k != 0 && q > 0.0) add(std::log(edge * q));
            }
        }
    }
    tidy();
    // Each integrand's peak: golden-section search between the best breakpoint's neighbors, and, where the peak is
    // narrower than the panels (its scale 1 / (|g'| + sqrt(|g''|)), g the log-integrand, by differences of 10^-4 in u),
    // breakpoints about it at 0.5 to 32 scales, so that every narrow feature lies at a breakpoint. The references of the
    // exponentials are the peaks' values.
    double ref_n = -kInf;
    double ref_d = -kInf;
    for (int which = 0; which < 2; ++which) {
        const auto g = [&](double u) {
            double ln_n = 0.0;
            double ln_d = 0.0;
            evaluate(std::clamp(u, u_lo, u_hi), ln_n, ln_d);
            return which == 0 ? ln_n : ln_d;
        };
        const auto value = [which](const Point& p) { return which == 0 ? p.ln_n : p.ln_d; };
        std::size_t best = 0;
        for (std::size_t i = 1; i < points.size(); ++i)
            if (value(points[i]) > value(points[best])) best = i;
        double a = points[best == 0 ? 0 : best - 1].u;
        double b = points[std::min(best + 1, points.size() - 1)].u;
        const double golden = (std::sqrt(5.0) - 1.0) / 2.0;
        double c = b - golden * (b - a);
        double d = a + golden * (b - a);
        double gc = g(c);
        double gd = g(d);
        for (int iteration = 0; iteration < 40; ++iteration) {
            if (gc >= gd) {
                b = d;
                d = c;
                gd = gc;
                c = b - golden * (b - a);
                gc = g(c);
            } else {
                a = c;
                c = d;
                gc = gd;
                d = a + golden * (b - a);
                gd = g(d);
            }
        }
        double peak = std::max(gc, gd) >= value(points[best]) ? (gc >= gd ? c : d) : points[best].u;
        const double top = std::max({gc, gd, value(points[best])});
        if (!std::isfinite(top))
            throw std::runtime_error("omega_bar_variant_direct: the likelihood vanishes on the whole range");
        (which == 0 ? ref_n : ref_d) = top;
        const double h = 1e-4;
        peak = std::clamp(peak, u_lo + h, u_hi - h);
        const double g0 = g(peak);
        const double gp = g(peak + h);
        const double gm = g(peak - h);
        const double slope = std::fabs(gp - gm) / (2.0 * h);
        const double curvature = std::fabs(gp - 2.0 * g0 + gm) / (h * h);
        const double scale = 1.0 / (slope + std::sqrt(curvature) + 1e-300);
        if (scale < kPanelU) {
            add(peak);
            for (const double k : {0.5, 1.0, 2.0, 3.0, 4.0, 6.0, 8.0, 12.0, 16.0, 24.0, 32.0}) {
                add(peak - k * scale);
                add(peak + k * scale);
            }
        }
    }
    tidy();
    // 10-point Gauss-Legendre on [a, b], the integrands scaled by their references; excess holds the largest amount (in
    // nats) by which a node's log-integrand passed its reference.
    const GaussLegendre& rule = gauss_legendre();
    double excess_n = 0.0;
    double excess_d = 0.0;
    const auto quadrature = [&](double a, double b, double& n, double& d) {
        const double half = 0.5 * (b - a);
        const double center = a + half;
        n = 0.0;
        d = 0.0;
        for (int i = 0; i < GaussLegendre::kN; ++i) {
            double ln_n = 0.0;
            double ln_d = 0.0;
            evaluate(center + half * rule.x[static_cast<std::size_t>(i)], ln_n, ln_d);
            excess_n = std::max(excess_n, ln_n - ref_n);
            excess_d = std::max(excess_d, ln_d - ref_d);
            const double w = half * rule.w[static_cast<std::size_t>(i)];
            n += w * std::exp(ln_n - ref_n);
            d += w * std::exp(ln_d - ref_d);
        }
    };
    // A panel: its halves' sums, and their difference from the whole's.
    struct Panel {
        double a, b;
        double left_n, left_d, right_n, right_d;
        double err_n, err_d;
    };
    const auto make = [&](double a, double b, double whole_n, double whole_d) {
        Panel panel{a, b, 0, 0, 0, 0, 0, 0};
        const double m = 0.5 * (a + b);
        quadrature(a, m, panel.left_n, panel.left_d);
        quadrature(m, b, panel.right_n, panel.right_d);
        panel.err_n = std::fabs(panel.left_n + panel.right_n - whole_n);
        panel.err_d = std::fabs(panel.left_d + panel.right_d - whole_d);
        return panel;
    };
    // The references are the peaks found, so no node should pass them by more than rounding; if one passes by more
    // than 1 nat (a peak the search missed), the references move up to it and the sums are taken again.
    for (int attempt = 0; attempt < 3; ++attempt) {
        excess_n = 0.0;
        excess_d = 0.0;
        std::vector<Panel> panels;
        for (std::size_t i = 0; i + 1 < points.size(); ++i) {
            if (std::max(points[i].ln_n, points[i + 1].ln_n) < ref_n - kDroppedNats &&
                std::max(points[i].ln_d, points[i + 1].ln_d) < ref_d - kDroppedNats)
                continue;
            double n = 0.0;
            double d = 0.0;
            quadrature(points[i].u, points[i + 1].u, n, d);
            panels.push_back(make(points[i].u, points[i + 1].u, n, d));
        }
        double total_n = 0.0;
        double total_d = 0.0;
        while (true) {
            total_n = 0.0;
            total_d = 0.0;
            double err_n = 0.0;
            double err_d = 0.0;
            for (const Panel& p : panels) {
                total_n += p.left_n + p.right_n;
                total_d += p.left_d + p.right_d;
                err_n += p.err_n;
                err_d += p.err_d;
            }
            if ((err_n <= kTolerance * total_n && err_d <= kTolerance * total_d) || panels.size() >= kMostPanels ||
                excess_n > 1.0 || excess_d > 1.0)
                break;
            // Halve the panel of the largest share of the error.
            std::size_t worst = 0;
            double worst_share = -1.0;
            for (std::size_t i = 0; i < panels.size(); ++i) {
                const double share = panels[i].err_n / total_n + panels[i].err_d / total_d;
                if (share > worst_share) {
                    worst_share = share;
                    worst = i;
                }
            }
            const Panel old = panels[worst];
            const double m = 0.5 * (old.a + old.b);
            panels[worst] = make(old.a, m, old.left_n, old.left_d);
            panels.push_back(make(m, old.b, old.right_n, old.right_d));
        }
        if (excess_n <= 1.0 && excess_d <= 1.0) return std::exp(ref_n - ref_d) * total_n / total_d;
        ref_n += excess_n;
        ref_d += excess_d;
    }
    throw std::runtime_error("omega_bar_variant_direct: the integrands' peaks were not found");
}

// The prior's bounds per sigma_y^2 (stage 3).
double omega_per_sigma2(double db) { return std::pow(10.0, db / 10.0) * kS500BandwidthHz / kOmegaBarSigma2PerN0Hz; }

}  // namespace

double omega_bar_variant_direct(double nu, double chi, double zeta, double lambda) {
    if (std::isnan(nu)) throw std::invalid_argument("omega_bar_variant_direct: nu is not a number");
    if (!std::isfinite(chi)) throw std::invalid_argument("omega_bar_variant_direct: chi must be finite");
    if (!finite_positive(zeta))
        throw std::invalid_argument("omega_bar_variant_direct: zeta must be positive and finite");
    if (!(lambda >= 0.0) || !std::isfinite(lambda))
        throw std::invalid_argument("omega_bar_variant_direct: lambda must be finite and not negative");
    const double lo = omega_per_sigma2(kPriorLoDb) / zeta;  // SE0
    const double hi = omega_per_sigma2(kPriorHiDb) / zeta;
    return zeta * variant_mean(std::max(nu, 1.0), chi, lambda, lo, hi);
}

// --- The variant's table (stage 7) ---

double OmegaBarVariantTable::p_of_nu(double nu) {
    if (!(nu > 1.0)) return 1.0;
    if (std::isinf(nu)) return 0.0;
    return 1.0 - std::sqrt(1.0 - 1.0 / nu);
}

OmegaBarVariantTable::OmegaBarVariantTable(const Axes& axes, std::vector<float> values, std::vector<float> minimum_chi)
    : axes_(axes), values_(std::move(values)), minimum_chi_(std::move(minimum_chi)) {
    const bool ordered = std::isfinite(axes.chi_min) && std::isfinite(axes.chi_max) && axes.chi_min < axes.chi_max &&
                         finite_positive(axes.zeta_min) && finite_positive(axes.zeta_max) &&
                         axes.zeta_min < axes.zeta_max && finite_positive(axes.lambda_min) &&
                         finite_positive(axes.lambda_max) && axes.lambda_min < axes.lambda_max;
    if (axes.upsilon_nodes < 2 || axes.chi_nodes < 2 || axes.zeta_nodes < 2 || axes.lambda_nodes < 2 || !ordered)
        throw std::invalid_argument("OmegaBarVariantTable: axes with fewer than 2 nodes or ends not finite and ordered");
    const std::size_t side = static_cast<std::size_t>(axes.upsilon_nodes) * static_cast<std::size_t>(axes.zeta_nodes) *
                             static_cast<std::size_t>(axes.lambda_nodes);
    if (values_.size() != side * static_cast<std::size_t>(axes.chi_nodes) || minimum_chi_.size() != side)
        throw std::invalid_argument("OmegaBarVariantTable: the values do not fit the axes");
    asinh_chi_min_ = std::asinh(axes.chi_min);
    asinh_chi_step_ = (std::asinh(axes.chi_max) - asinh_chi_min_) / (axes.chi_nodes - 1);
    ln_zeta_min_ = std::log(axes.zeta_min);
    ln_zeta_step_ = (std::log(axes.zeta_max) - ln_zeta_min_) / (axes.zeta_nodes - 1);
    ln_lambda_min_ = std::log(axes.lambda_min);
    ln_lambda_step_ = (std::log(axes.lambda_max) - ln_lambda_min_) / (axes.lambda_nodes - 1);
}

double OmegaBarVariantTable::nu_node(int i) const {
    const double p = static_cast<double>(i) / (axes_.upsilon_nodes - 1);
    if (p == 0.0) return kInf;
    return 1.0 / (1.0 - (1.0 - p) * (1.0 - p));
}
double OmegaBarVariantTable::chi_node(int j) const { return std::sinh(asinh_chi_min_ + j * asinh_chi_step_); }
double OmegaBarVariantTable::zeta_node(int k) const { return std::exp(ln_zeta_min_ + k * ln_zeta_step_); }
double OmegaBarVariantTable::lambda_node(int m) const { return std::exp(ln_lambda_min_ + m * ln_lambda_step_); }

namespace {

// The node index below coordinate x on an axis of `nodes` nodes from x0 in steps of `step`, and the fraction toward the
// next, x clamped to the axis.
struct Located {
    int i;
    double f;
};
Located locate(double x, double x0, double step, int nodes) {
    const double position = std::clamp((x - x0) / step, 0.0, static_cast<double>(nodes - 1));
    const int i = std::min(static_cast<int>(position), nodes - 2);
    return {i, position - i};
}

}  // namespace

OmegaBarVariantTable::Position OmegaBarVariantTable::upsilon_position(double nu) const {
    const Located l = locate(p_of_nu(nu), 0.0, 1.0 / (axes_.upsilon_nodes - 1), axes_.upsilon_nodes);
    return {l.i, l.f};
}
OmegaBarVariantTable::Position OmegaBarVariantTable::chi_position(double chi) const {
    const double x = std::asinh(std::clamp(chi, axes_.chi_min, axes_.chi_max));
    const Located l = locate(x, asinh_chi_min_, asinh_chi_step_, axes_.chi_nodes);
    return {l.i, l.f};
}
OmegaBarVariantTable::Position OmegaBarVariantTable::zeta_position(double zeta) const {
    const double z = !(zeta > axes_.zeta_min) ? axes_.zeta_min : std::min(zeta, axes_.zeta_max);
    const Located l = locate(std::log(z), ln_zeta_min_, ln_zeta_step_, axes_.zeta_nodes);
    return {l.i, l.f};
}
OmegaBarVariantTable::Position OmegaBarVariantTable::lambda_position(double lambda) const {
    const double z = !(lambda > axes_.lambda_min) ? axes_.lambda_min : std::min(lambda, axes_.lambda_max);
    const Located l = locate(std::log(z), ln_lambda_min_, ln_lambda_step_, axes_.lambda_nodes);
    return {l.i, l.f};
}

double OmegaBarVariantTable::omega_bar_fs2(double nu, double omega_hat_fs2, double a2, double b2, double sum_s4,
                                           double sigma2_fs2, const OmegaBarTable& fallback) const {
    if (!finite_positive(sigma2_fs2))
        throw std::invalid_argument("OmegaBarVariantTable::omega_bar_fs2: sigma_y^2 must be positive and finite (FS^2)");
    if (fallback.lo_db() != kPriorLoDb || fallback.hi_db() != kPriorHiDb)
        throw std::invalid_argument("OmegaBarVariantTable::omega_bar_fs2: the fallback table is not for -15 and +85 dB "
                                    "S500, the variant's prior bounds");
    if (!(a2 > 0.0) || !(sum_s4 > 0.0)) return fallback.omega_bar_fs2(nu, omega_hat_fs2, sigma2_fs2);
    const double root = std::sqrt(a2);
    const double zeta = root / sum_s4;                       // SE0 / sigma_y^2
    double chi = omega_hat_fs2 / (sigma2_fs2 * zeta);        // Omega_hat / SE0
    if (chi > axes_.chi_max) return fallback.omega_bar_fs2(nu, omega_hat_fs2, sigma2_fs2);
    if (std::isnan(chi)) chi = 0.0;
    const double lambda = 2.0 * b2 / (sum_s4 * root);        // B / SE0
    const Position u = upsilon_position(nu);
    const Position c = chi_position(chi);
    const Position z = zeta_position(zeta);
    const Position l = lambda_position(lambda);
    const std::size_t nc = static_cast<std::size_t>(axes_.chi_nodes);
    const std::size_t nz = static_cast<std::size_t>(axes_.zeta_nodes);
    const std::size_t nl = static_cast<std::size_t>(axes_.lambda_nodes);
    double value = 0.0;
    for (int du = 0; du < 2; ++du) {
        const double wu = du == 0 ? 1.0 - u.f : u.f;
        for (int dc = 0; dc < 2; ++dc) {
            const double wc = wu * (dc == 0 ? 1.0 - c.f : c.f);
            for (int dz = 0; dz < 2; ++dz) {
                const double wz = wc * (dz == 0 ? 1.0 - z.f : z.f);
                const std::size_t row =
                    ((static_cast<std::size_t>(u.i + du) * nc + static_cast<std::size_t>(c.i + dc)) * nz +
                     static_cast<std::size_t>(z.i + dz)) * nl + static_cast<std::size_t>(l.i);
                value += wz * ((1.0 - l.f) * values_[row] + l.f * values_[row + 1]);
            }
        }
    }
    return sigma2_fs2 * std::exp(value);
}

double OmegaBarVariantTable::minimum_chi(double nu, double zeta, double lambda) const {
    const Position u = upsilon_position(nu);
    const Position z = zeta_position(zeta);
    const Position l = lambda_position(lambda);
    const std::size_t nz = static_cast<std::size_t>(axes_.zeta_nodes);
    const std::size_t nl = static_cast<std::size_t>(axes_.lambda_nodes);
    double value = 0.0;
    for (int du = 0; du < 2; ++du) {
        const double wu = du == 0 ? 1.0 - u.f : u.f;
        for (int dz = 0; dz < 2; ++dz) {
            const double wz = wu * (dz == 0 ? 1.0 - z.f : z.f);
            const std::size_t row =
                (static_cast<std::size_t>(u.i + du) * nz + static_cast<std::size_t>(z.i + dz)) * nl +
                static_cast<std::size_t>(l.i);
            value += wz * ((1.0 - l.f) * minimum_chi_[row] + l.f * minimum_chi_[row + 1]);
        }
    }
    return value;
}

// --- The variant's file ---

namespace {

constexpr char kVariantMagic[8] = {'K', 'Z', '4', 'A', 'P', 'O', 'B', 'V'};
constexpr std::uint32_t kVariantVersion = 1;

void get_variant(std::ifstream& in, void* data, std::size_t bytes, const std::string& path) {
    in.read(static_cast<char*>(data), static_cast<std::streamsize>(bytes));
    if (!in)
        throw std::runtime_error("OmegaBarVariantTable::load: " + path +
                                 " is not a variant Omega_bar table (too short)");
}

std::string axes_text(const OmegaBarVariantTable::Axes& a) {
    std::ostringstream text;
    text << "upsilon " << a.upsilon_nodes << " nodes; chi " << a.chi_min << " to " << a.chi_max << " at "
         << a.chi_nodes << "; zeta " << a.zeta_min << " to " << a.zeta_max << " at " << a.zeta_nodes << "; lambda "
         << a.lambda_min << " to " << a.lambda_max << " at " << a.lambda_nodes;
    return text.str();
}

}  // namespace

void OmegaBarVariantTable::save(const std::string& path) const {
    std::ofstream out(path, std::ios::binary);
    if (!out) throw std::runtime_error("OmegaBarVariantTable::save: cannot write " + path);
    // The format: the magic, version 1, sigma_y^2 / N0 (Hz), the prior's bounds (dB S500), each axis's ends and node
    // count, the values and the side table (floats); doubles, 32-bit integers and floats in the machine's byte order
    // (little-endian on every supported platform).
    const auto put_double = [&](double x) { put(out, &x, sizeof x); };
    const auto put_int = [&](int n) {
        const std::int32_t v = n;
        put(out, &v, sizeof v);
    };
    put(out, kVariantMagic, sizeof kVariantMagic);
    put(out, &kVariantVersion, sizeof kVariantVersion);
    put_double(kOmegaBarSigma2PerN0Hz);
    put_double(kPriorLoDb);
    put_double(kPriorHiDb);
    put_int(axes_.upsilon_nodes);
    put_double(axes_.chi_min);
    put_double(axes_.chi_max);
    put_int(axes_.chi_nodes);
    put_double(axes_.zeta_min);
    put_double(axes_.zeta_max);
    put_int(axes_.zeta_nodes);
    put_double(axes_.lambda_min);
    put_double(axes_.lambda_max);
    put_int(axes_.lambda_nodes);
    put(out, values_.data(), values_.size() * sizeof(float));
    put(out, minimum_chi_.data(), minimum_chi_.size() * sizeof(float));
    if (!out) throw std::runtime_error("OmegaBarVariantTable::save: cannot write " + path);
}

OmegaBarVariantTable OmegaBarVariantTable::load(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("OmegaBarVariantTable::load: cannot read " + path);
    char magic[8];
    std::uint32_t version = 0;
    get_variant(in, magic, sizeof magic, path);
    get_variant(in, &version, sizeof version, path);
    if (std::memcmp(magic, kVariantMagic, sizeof magic) != 0 || version != kVariantVersion)
        throw std::runtime_error("OmegaBarVariantTable::load: " + path +
                                 " is not a variant Omega_bar table of version 1");
    const auto get_double = [&]() {
        double x = 0.0;
        get_variant(in, &x, sizeof x, path);
        return x;
    };
    const auto get_int = [&]() {
        std::int32_t n = 0;
        get_variant(in, &n, sizeof n, path);
        return static_cast<int>(n);
    };
    const double sigma2_per_n0 = get_double();
    const double lo_db = get_double();
    const double hi_db = get_double();
    Axes axes;
    axes.upsilon_nodes = get_int();
    axes.chi_min = get_double();
    axes.chi_max = get_double();
    axes.chi_nodes = get_int();
    axes.zeta_min = get_double();
    axes.zeta_max = get_double();
    axes.zeta_nodes = get_int();
    axes.lambda_min = get_double();
    axes.lambda_max = get_double();
    axes.lambda_nodes = get_int();
    if (sigma2_per_n0 != kOmegaBarSigma2PerN0Hz) {
        std::ostringstream why;
        why << "OmegaBarVariantTable::load: " << path << " was built for sigma_y^2 / N0 = " << sigma2_per_n0
            << " Hz, this build's is " << kOmegaBarSigma2PerN0Hz << " Hz";
        throw std::runtime_error(why.str());
    }
    if (lo_db != kPriorLoDb || hi_db != kPriorHiDb) {
        std::ostringstream why;
        why << "OmegaBarVariantTable::load: " << path << " was built for the prior's bounds " << lo_db << " and "
            << hi_db << " dB S500, this build's are " << kPriorLoDb << " and " << kPriorHiDb << " dB S500";
        throw std::runtime_error(why.str());
    }
    if (!(axes == Axes{}))
        throw std::runtime_error("OmegaBarVariantTable::load: " + path + " has the axes " + axes_text(axes) +
                                 ", this build's are " + axes_text(Axes{}));
    const std::size_t side = static_cast<std::size_t>(axes.upsilon_nodes) * static_cast<std::size_t>(axes.zeta_nodes) *
                             static_cast<std::size_t>(axes.lambda_nodes);
    std::vector<float> values(side * static_cast<std::size_t>(axes.chi_nodes));
    std::vector<float> minimum(side);
    get_variant(in, values.data(), values.size() * sizeof(float), path);
    get_variant(in, minimum.data(), minimum.size() * sizeof(float), path);
    if (in.peek() != std::char_traits<char>::eof())
        throw std::runtime_error("OmegaBarVariantTable::load: " + path + " is longer than its table");
    return OmegaBarVariantTable(axes, std::move(values), std::move(minimum));
}

}  // namespace kz4ap::viterbi
