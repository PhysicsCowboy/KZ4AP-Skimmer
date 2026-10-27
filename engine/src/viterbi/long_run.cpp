// The long-run amplitude: Omega_hat, nu and the default Omega_bar (long_run.hpp; design notes, part 7; plan 1b,
// Task 7).
// This file must not read a generated table: kz4ap-viterbi-tables links it to build them (Ruling 76).
#include "kz4ap/viterbi/long_run.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <numbers>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace kz4ap::viterbi {

namespace {

bool finite_positive(double x) { return x > 0.0 && std::isfinite(x); }

}  // namespace

// --- LongRun ---

void LongRun::Sum::add(double x) {
    const double t = sum + x;
    carry += std::fabs(sum) >= std::fabs(x) ? (sum - t) + x : (x - t) + sum;
    sum = t;
}

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

}  // namespace kz4ap::viterbi
