// kz4ap-viterbi-tables: generates the new decoder's compiled-in tables (plan 1b, Ruling 65). It links the engine's
// sources without the generated tables (a static archive of kz4ap_engine_core's objects; Ruling 76), so it builds
// before any table exists.
//
//   kz4ap-viterbi-tables omega-bar [--lo-db L --hi-db H] --out FILE
//       The default Omega_bar table (engine/include/kz4ap/viterbi/long_run.hpp, stage 4; design notes, part 7) for the
//       prior's bounds L and H (dB S500; -15 and +85 by default), every node from omega_bar_closed_form. The grid starts
//       at OmegaBarLayout's defaults and is refined (every step in z halved, the intervals in p and in the beta = 0
//       row's p doubled) until the table's largest relative error against the closed form on 10 000 random points
//       (seed 1; nu log-uniform on [1, 1 000], Omega_hat / sigma_y^2 uniform in asinh on [-5, 10^9]) is at most plan
//       O8's 0.1%; the error with nu up to 10^6 is reported too. At -15 and +85 dB S500 it writes the C++ source
//       of the compiled table (engine/src/viterbi/omega_bar_table.cpp: its command, grid and errors in its header);
//       at other bounds a binary file (OmegaBarTable::save) for EngineConfig::viterbi_omega_bar_default_table and
//       kz4ap-bench --omega-bar-default-table, kept in build/, not committed.
//
//   kz4ap-viterbi-tables omega-bar-variant --out FILE [--threads N]
//       The noise-in-Omega_bar variant's table (engine/include/kz4ap/viterbi/long_run.hpp, stages 6 and 7; design
//       notes, part 7; plan 1b, Task 8), written as a binary file (OmegaBarVariantTable::save) for
//       build/viterbi-tables/omega-bar-variant-v1.bin, never committed (Ruling 65). Every node from
//       omega_bar_variant_direct on N threads (by default one fewer than the machine's); for each (upsilon, zeta,
//       lambda) the side table's chi where Omega_bar stops falling: the lowest chi node's neighbors bracket it, and 30
//       steps of golden-section search on the direct integral narrow the bracket to 10^-6 of its width. Then the
//       table's relative error against the direct integral on the design's 4 000 points (seed 1; part 7's two halves,
//       variant_points below), overall and by region, goes to stdout with the build time.
#include "kz4ap/channelizer.hpp"
#include "kz4ap/types.hpp"
#include "kz4ap/viterbi/front_filter.hpp"
#include "kz4ap/viterbi/long_run.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <functional>
#include <fstream>
#include <iostream>
#include <numbers>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

using kz4ap::Sample;

[[noreturn]] void usage(const std::string& why) {
    throw std::invalid_argument(why +
                                "\nusage: kz4ap-viterbi-tables omega-bar [--lo-db L --hi-db H] --out FILE"
                                "\n       kz4ap-viterbi-tables omega-bar-variant --out FILE [--threads N]");
}

// --- A portable generator: xoshiro256** seeded by splitmix64, Box-Muller for the normal deviates ---

std::uint64_t splitmix64(std::uint64_t& state) {
    std::uint64_t z = (state += 0x9e3779b97f4a7c15ULL);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
    return z ^ (z >> 31);
}

// Uniform deviates on [0, 1), 53 bits, from xoshiro256** seeded by splitmix64: the tool's one generator (the weight
// table's noise and the Omega_bar table's random points).
class Uniform {
public:
    explicit Uniform(std::uint64_t seed) {
        for (auto& s : state_) s = splitmix64(seed);
    }
    double next() {
        const std::uint64_t result = rotl(state_[1] * 5, 7) * 9;
        const std::uint64_t t = state_[1] << 17;
        state_[2] ^= state_[0];
        state_[3] ^= state_[1];
        state_[1] ^= state_[2];
        state_[0] ^= state_[3];
        state_[2] ^= t;
        state_[3] = rotl(state_[3], 45);
        return static_cast<double>(result >> 11) * 0x1.0p-53;
    }

private:
    static std::uint64_t rotl(std::uint64_t x, int k) { return (x << k) | (x >> (64 - k)); }
    std::array<std::uint64_t, 4> state_{};
};

std::string fixed(double x, int digits) {
    char buffer[64];
    std::snprintf(buffer, sizeof buffer, "%.*f", digits, x);
    return buffer;
}

// The table's error against the closed form on `points` random points: nu log-uniform on [1, nu_max], Omega_hat /
// sigma_y^2 uniform in asinh on [-5, ratio_max] (plan 1b, Task 7: nu_max 1 000, ratio_max 10^9).
struct TableError {
    double largest = 0.0;  // relative
    double p99 = 0.0;
    double median = 0.0;
    double at_nu = 0.0;
    double at_ratio = 0.0;  // Omega_hat / sigma_y^2
};

TableError table_error(const kz4ap::viterbi::OmegaBarTable& table, int points, std::uint64_t seed, double nu_max,
                       double ratio_max) {
    Uniform uniform(seed);
    const double sigma2 = 1.0;  // FS^2: the relative error does not depend on it
    std::vector<double> errors;
    errors.reserve(static_cast<std::size_t>(points));
    TableError out;
    for (int i = 0; i < points; ++i) {
        const double nu = std::exp(uniform.next() * std::log(nu_max));
        const double ratio =
            std::sinh(std::asinh(-5.0) + uniform.next() * (std::asinh(ratio_max) - std::asinh(-5.0)));
        const double want =
            kz4ap::viterbi::omega_bar_closed_form(nu, ratio * sigma2, sigma2, table.lo_db(), table.hi_db());
        const double got = table.omega_bar_fs2(nu, ratio * sigma2, sigma2);
        const double error = std::fabs(got / want - 1.0);
        errors.push_back(error);
        if (error > out.largest) {
            out.largest = error;
            out.at_nu = nu;
            out.at_ratio = ratio;
        }
    }
    std::sort(errors.begin(), errors.end());
    out.p99 = errors[static_cast<std::size_t>(0.99 * static_cast<double>(errors.size() - 1))];
    out.median = errors[errors.size() / 2];
    return out;
}

// x as a C++ literal that reads back as exactly x: the shortest of 15 to 17 significant digits that does, with a
// decimal point.
std::string literal(double x) {
    char text[40];
    for (int digits = 15; digits <= 17; ++digits) {
        std::snprintf(text, sizeof text, "%.*g", digits, x);
        if (std::strtod(text, nullptr) == x) break;
    }
    std::string s = text;
    if (s.find_first_of(".e") == std::string::npos) s += ".0";
    return s;
}

std::string percent(double x) { return fixed(100.0 * x, 4) + "%"; }

std::string describe(const TableError& e) {
    char at[128];
    std::snprintf(at, sizeof at, "nu = %.4g, Omega_hat = %.4g sigma_y^2", e.at_nu, e.at_ratio);
    return "largest " + percent(e.largest) + " (at " + at + "), 99th percentile " + percent(e.p99) + ", median " +
           percent(e.median);
}

std::string layout_text(const kz4ap::viterbi::OmegaBarLayout& layout) {
    char text[320];
    std::snprintf(text, sizeof text,
                  "p_nodes %d, zero_row_nodes %d, below_lo %.4g, z_max %.4g, fine_half_width %.4g, fine_step %.4g, "
                  "medium_half_width %.4g, medium_step %.4g, coarse_step %.4g",
                  layout.p_nodes, layout.zero_row_nodes, layout.below_lo, layout.z_max, layout.fine_half_width,
                  layout.fine_step, layout.medium_half_width, layout.medium_step, layout.coarse_step);
    return text;
}

int run_omega_bar(int argc, char** argv) {
    std::string out_path;
    double lo_db = -15.0;
    double hi_db = 85.0;
    std::string lo_text = "-15";
    std::string hi_text = "85";
    for (int i = 2; i < argc; ++i) {
        const std::string arg = argv[i];
        if (i + 1 >= argc) usage(arg + " needs a value");
        if (arg == "--out") {
            out_path = argv[++i];
        } else if (arg == "--lo-db") {
            lo_text = argv[++i];
            lo_db = std::stod(lo_text);
        } else if (arg == "--hi-db") {
            hi_text = argv[++i];
            hi_db = std::stod(hi_text);
        } else {
            usage("unknown option " + arg);
        }
    }
    if (out_path.empty()) usage("--out is required");
    if (!std::isfinite(lo_db) || !std::isfinite(hi_db) || !(lo_db < hi_db)) usage("--lo-db must be below --hi-db");
    // The compiled table is C++ source; a table for other bounds is a binary file (OmegaBarTable::save).
    const bool compiled = lo_db == -15.0 && hi_db == 85.0;
    // The command as recorded: the output's file name only, never a path.
    std::string command = "kz4ap-viterbi-tables omega-bar";
    if (!compiled) command += " --lo-db " + lo_text + " --hi-db " + hi_text;
    command += " --out " + std::filesystem::path(out_path).filename().generic_string();
    const auto started = std::chrono::steady_clock::now();
    const auto elapsed = [&] {
        return std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    };
    std::cout << command << "\n";

    // The grid: the default layout, refined (every step in z halved, the intervals in p doubled) until the error on the
    // 10 000 points is at most O8's 0.1%.
    constexpr double kTarget = 1e-3;
    constexpr int kPoints = 10000;
    constexpr std::uint64_t kSeed = 1;
    kz4ap::viterbi::OmegaBarLayout layout;
    std::optional<kz4ap::viterbi::OmegaBarTable> table;
    TableError error;
    for (int round = 0;; ++round) {
        table.emplace(kz4ap::viterbi::OmegaBarTable::build(lo_db, hi_db, layout));
        error = table_error(*table, kPoints, kSeed, 1000.0, 1e9);
        std::cout << "grid " << layout_text(layout) << ": " << table->values().size() + table->zero_row().size()
                  << " values, built after " << fixed(elapsed(), 1) << " s; error " << describe(error) << "\n";
        if (error.largest <= kTarget) break;
        if (round == 4) throw std::runtime_error("the grid's error stays above the target after 4 refinements");
        layout.p_nodes = 2 * layout.p_nodes - 1;
        layout.zero_row_nodes = 2 * layout.zero_row_nodes - 1;
        layout.fine_step /= 2.0;
        layout.medium_step /= 2.0;
        layout.coarse_step /= 2.0;
    }
    // Beyond the test's range, for the record: nu up to 10^6.
    const TableError wide = table_error(*table, kPoints, kSeed + 1, 1e6, 1e9);
    std::cout << "nu log-uniform on [1, 10^6]: " << describe(wide) << "\n";
    std::cout << "done after " << fixed(elapsed(), 1) << " s\n";

    const std::string file = std::filesystem::path(out_path).filename().generic_string();
    if (!compiled) {
        table->save(out_path);
        std::cout << "wrote " << file << " (a binary table for " << lo_text << " and " << hi_text << " dB S500)\n";
        return 0;
    }
    std::ofstream out(out_path, std::ios::binary);
    if (!out) throw std::runtime_error("cannot write " + out_path);
    const kz4ap::viterbi::OmegaBarLayout& used = table->layout();
    const std::size_t z_count = table->z_nodes().size();
    out << "// Generated by kz4ap-viterbi-tables (bench/src/viterbi_tables_main.cpp); do not edit by hand.\n"
        << "//\n"
        << "// Command: " << command << "\n"
        << "// The default Omega_bar table (engine/include/kz4ap/viterbi/long_run.hpp, stage 4; design notes, part 7) for\n"
        << "// the prior's bounds -15 and +85 dB S500: ln(Omega_bar / Omega_hi) at p = 1 / (1 + sqrt(nu - 1)) on "
        << used.p_nodes << " nodes\n"
        << "// uniform on [0, 1] and z = ln(Omega_hat_+ / Omega_hi) on " << z_count
        << " nodes piecewise uniform (the layout below), and\n"
        << "// the beta = 0 row; every node from omega_bar_closed_form (at p = 0, nu = infinity, Omega_hat_+ clipped to "
        << "the\n"
        << "// prior's range). Values rounded to 6 decimals (at most 5e-7 relative in Omega_bar).\n"
        << "// Layout: p_nodes " << used.p_nodes << ", zero_row_nodes " << used.zero_row_nodes << ", below_lo "
        << literal(used.below_lo) << ", z_max " << literal(used.z_max) << ", fine_half_width "
        << literal(used.fine_half_width) << ",\n"
        << "// fine_step " << literal(used.fine_step) << ", medium_half_width " << literal(used.medium_half_width)
        << ", medium_step " << literal(used.medium_step) << ", coarse_step " << literal(used.coarse_step) << ".\n"
        << "// Error of the bilinear table against omega_bar_closed_form on 10 000 random points (seed " << kSeed
        << "; nu\n"
        << "// log-uniform on [1, 1 000], Omega_hat / sigma_y^2 uniform in asinh on [-5, 10^9]; plan O8: at most "
        << "0.1%):\n"
        << "//   " << describe(error) << ".\n"
        << "// With nu log-uniform on [1, 10^6] (seed " << kSeed + 1 << "):\n"
        << "//   " << describe(wide) << ".\n"
        << "#include \"kz4ap/viterbi/long_run.hpp\"\n\n"
        << "#include <iterator>\n"
        << "#include <vector>\n\n"
        << "namespace kz4ap::viterbi {\n\n"
        << "namespace {\n\n";
    const auto write_values = [&](const char* name, const std::vector<double>& values) {
        out << "const double " << name << "[" << values.size() << "] = {\n";
        for (std::size_t i = 0; i < values.size(); ++i) {
            if (i % 9 == 0) out << "   ";
            char item[32];
            std::snprintf(item, sizeof item, " %.6f", values[i]);
            out << item << (i + 1 < values.size() ? "," : "");
            if (i % 9 == 8 || i + 1 == values.size()) out << "\n";
        }
        out << "};\n\n";
    };
    write_values("kZeroRow", table->zero_row());
    write_values("kValues", table->values());
    std::ostringstream layout_code;
    layout_code << "        layout.p_nodes = " << used.p_nodes << ";\n"
                << "        layout.zero_row_nodes = " << used.zero_row_nodes << ";\n"
                << "        layout.below_lo = " << literal(used.below_lo) << ";\n"
                << "        layout.z_max = " << literal(used.z_max) << ";\n"
                << "        layout.fine_half_width = " << literal(used.fine_half_width) << ";\n"
                << "        layout.fine_step = " << literal(used.fine_step) << ";\n"
                << "        layout.medium_half_width = " << literal(used.medium_half_width) << ";\n"
                << "        layout.medium_step = " << literal(used.medium_step) << ";\n"
                << "        layout.coarse_step = " << literal(used.coarse_step) << ";\n";
    out << "}  // namespace\n\n"
        << "OmegaBarTable OmegaBarTable::compiled() {\n"
        << "    static const OmegaBarTable table = [] {\n"
        << "        OmegaBarLayout layout;\n"
        << layout_code.str()
        << "        return OmegaBarTable(-15.0, 85.0, layout, std::vector<double>(std::begin(kZeroRow), "
           "std::end(kZeroRow)),\n"
        << "                             std::vector<double>(std::begin(kValues), std::end(kValues)));\n"
        << "    }();\n"
        << "    return table;\n"
        << "}\n\n"
        << "}  // namespace kz4ap::viterbi\n";
    if (!out) throw std::runtime_error("cannot write " + out_path);
    std::cout << "wrote " << file << "\n";
    return 0;
}


// --- The noise-in-Omega_bar variant's table (long_run.hpp, stages 6 and 7) ---

// Runs job(i) for i = 0 ... jobs - 1 on `threads` threads; the first exception is rethrown.
template <typename Job>
void parallel(std::size_t jobs, unsigned threads, Job job) {
    std::atomic<std::size_t> next{0};
    std::vector<std::thread> pool;
    std::vector<std::exception_ptr> errors(threads);
    for (unsigned t = 0; t < threads; ++t) {
        pool.emplace_back([&, t] {
            try {
                for (std::size_t i = next++; i < jobs; i = next++) job(i);
            } catch (...) {
                errors[t] = std::current_exception();
                next = jobs;
            }
        });
    }
    for (auto& thread : pool) thread.join();
    for (const auto& e : errors)
        if (e) std::rethrow_exception(e);
}

// One point of the error report: the design's two halves (part 7).
struct VariantPoint {
    double nu;
    double chi;
    double zeta;
    double kappa;
    bool first_marks;
};

// The design's 4 000 points (part 7): half with zeta log-uniform on [0.003, 4], kappa uniform on [0.4, 2.8] and nu
// log-uniform on [1, 1 000]; half first marks, zeta log-uniform on [4, 400], kappa uniform on [0, 1.2] (0 on a tenth of
// them) and nu log-uniform on [1, 3]; chi uniform in asinh(chi) from max(-8, -max(2.1 / zeta, 1.3)) to 30 on alternate
// points and to 10^4 on the others.
std::vector<VariantPoint> variant_points(int count, std::uint64_t seed) {
    Uniform uniform(seed);
    const auto log_uniform = [&](double lo, double hi) {
        return std::exp(std::log(lo) + uniform.next() * std::log(hi / lo));
    };
    std::vector<VariantPoint> points;
    for (int i = 0; i < count; ++i) {
        VariantPoint p{};
        p.first_marks = i >= count / 2;
        p.zeta = p.first_marks ? log_uniform(4.0, 400.0) : log_uniform(0.003, 4.0);
        p.kappa = p.first_marks ? (i % 10 == 0 ? 0.0 : 1.2 * uniform.next()) : 0.4 + 2.4 * uniform.next();
        p.nu = p.first_marks ? log_uniform(1.0, 3.0) : log_uniform(1.0, 1000.0);
        const double chi_lo = std::max(-8.0, -std::max(2.1 / p.zeta, 1.3));
        const double chi_hi = i % 2 == 0 ? 30.0 : 1e4;
        p.chi = std::sinh(std::asinh(chi_lo) + uniform.next() * (std::asinh(chi_hi) - std::asinh(chi_lo)));
        points.push_back(p);
    }
    return points;
}

// The relative error's median, 99th percentile and largest over a set, and where the largest lies.
struct ErrorSummary {
    std::size_t count = 0;
    double median = 0.0;
    double p99 = 0.0;
    double largest = 0.0;
    VariantPoint at{};
};

ErrorSummary summarize(const std::vector<double>& errors, const std::vector<VariantPoint>& points,
                       const std::function<bool(const VariantPoint&)>& keep) {
    std::vector<double> chosen;
    ErrorSummary out;
    for (std::size_t i = 0; i < errors.size(); ++i) {
        if (!keep(points[i])) continue;
        chosen.push_back(errors[i]);
        if (errors[i] >= out.largest) {
            out.largest = errors[i];
            out.at = points[i];
        }
    }
    out.count = chosen.size();
    if (chosen.empty()) return out;
    std::sort(chosen.begin(), chosen.end());
    out.median = chosen[chosen.size() / 2];
    out.p99 = chosen[static_cast<std::size_t>(0.99 * static_cast<double>(chosen.size() - 1))];
    return out;
}

std::string describe_variant(const ErrorSummary& e) {
    char at[160];
    std::snprintf(at, sizeof at, "nu = %.4g, chi = %.4g, zeta = %.4g, kappa = %.3g", e.at.nu, e.at.chi, e.at.zeta,
                  e.at.kappa);
    return std::to_string(e.count) + " points: median " + percent(e.median) + ", 99th percentile " + percent(e.p99) +
           ", largest " + percent(e.largest) + " (at " + at + ")";
}

int run_omega_bar_variant(int argc, char** argv) {
    std::string out_path;
    unsigned threads = std::max(1u, std::thread::hardware_concurrency() - 1);
    for (int i = 2; i < argc; ++i) {
        const std::string arg = argv[i];
        if (i + 1 >= argc) usage(arg + " needs a value");
        if (arg == "--out")
            out_path = argv[++i];
        else if (arg == "--threads")
            threads = static_cast<unsigned>(std::stoul(argv[++i]));
        else
            usage("unknown option " + arg);
    }
    if (out_path.empty()) usage("--out is required");
    if (threads < 1) usage("--threads: at least 1");
    namespace v = kz4ap::viterbi;
    // The command as recorded: the output's file name only, never a path.
    const std::string command = "kz4ap-viterbi-tables omega-bar-variant --out " +
                                std::filesystem::path(out_path).filename().generic_string() + " --threads " +
                                std::to_string(threads);
    const auto started = std::chrono::steady_clock::now();
    const auto elapsed = [&] {
        return std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    };
    std::cout << command << std::endl;

    const v::OmegaBarVariantTable::Axes axes;
    const std::size_t nu_count = static_cast<std::size_t>(axes.upsilon_nodes);
    const std::size_t nc = static_cast<std::size_t>(axes.chi_nodes);
    const std::size_t nz = static_cast<std::size_t>(axes.zeta_nodes);
    const std::size_t nl = static_cast<std::size_t>(axes.lambda_nodes);
    // A table of zeros on the axes, for its nodes.
    const v::OmegaBarVariantTable grid(axes, std::vector<float>(nu_count * nc * nz * nl, 0.0f),
                                       std::vector<float>(nu_count * nz * nl, 0.0f));
    std::vector<float> values(nu_count * nc * nz * nl);
    std::vector<float> minimum(nu_count * nz * nl);
    std::atomic<std::size_t> done{0};
    // One job per (upsilon, zeta): every chi and lambda, then each lambda's minimum over chi.
    parallel(nu_count * nz, threads, [&](std::size_t job) {
        const std::size_t i = job / nz;
        const std::size_t k = job % nz;
        const double nu = grid.nu_node(static_cast<int>(i));
        const double zeta = grid.zeta_node(static_cast<int>(k));
        for (std::size_t m = 0; m < nl; ++m) {
            const double lambda = grid.lambda_node(static_cast<int>(m));
            const auto ln_omega_bar = [&](double chi) {
                return std::log(v::omega_bar_variant_direct(nu, chi, zeta, lambda));
            };
            std::size_t lowest = 0;
            for (std::size_t j = 0; j < nc; ++j) {
                const std::size_t at = ((i * nc + j) * nz + k) * nl + m;
                values[at] = static_cast<float>(ln_omega_bar(grid.chi_node(static_cast<int>(j))));
                if (values[at] < values[((i * nc + lowest) * nz + k) * nl + m]) lowest = j;
            }
            // The minimum over chi: golden-section search between the lowest node's neighbors, to 10^-6 of the bracket
            // (chi_min when Omega_bar still falls there).
            double a = grid.chi_node(static_cast<int>(lowest == 0 ? 0 : lowest - 1));
            double b = grid.chi_node(static_cast<int>(std::min(lowest + 1, nc - 1)));
            const double golden = (std::sqrt(5.0) - 1.0) / 2.0;
            double c = b - golden * (b - a);
            double d = a + golden * (b - a);
            double fc = ln_omega_bar(c);
            double fd = ln_omega_bar(d);
            for (int iteration = 0; iteration < 30; ++iteration) {
                if (fc <= fd) {
                    b = d;
                    d = c;
                    fd = fc;
                    c = b - golden * (b - a);
                    fc = ln_omega_bar(c);
                } else {
                    a = c;
                    c = d;
                    fc = fd;
                    d = a + golden * (b - a);
                    fd = ln_omega_bar(d);
                }
            }
            minimum[(i * nz + k) * nl + m] = static_cast<float>(0.5 * (a + b));
        }
        const std::size_t finished = ++done;
        if (finished % 62 == 0 || finished == nu_count * nz)
            std::cout << finished << " of " << nu_count * nz << " (upsilon, zeta) pairs after " << fixed(elapsed(), 1)
                      << " s" << std::endl;
    });
    const double built_s = elapsed();
    for (const float x : values)
        if (!std::isfinite(x)) throw std::runtime_error("a node of the table is not finite");
    for (const float x : minimum)
        if (!std::isfinite(x)) throw std::runtime_error("a node of the side table is not finite");
    const v::OmegaBarVariantTable table(axes, std::move(values), std::move(minimum));

    // The error report against the direct integral on the design's 4 000 points (seed 1).
    constexpr int kPoints = 4000;
    constexpr std::uint64_t kSeed = 1;
    const std::vector<VariantPoint> points = variant_points(kPoints, kSeed);
    // The fallback the lookup asks for: never read here (every point has chi <= 10^4), and built rather than compiled,
    // since this tool may not read a generated table (Ruling 76).
    const v::OmegaBarTable fallback = v::OmegaBarTable::build(-15.0, 85.0, v::OmegaBarLayout{});
    std::vector<double> errors(points.size());
    parallel(points.size(), threads, [&](std::size_t n) {
        const VariantPoint& p = points[n];
        const double lambda = p.kappa * p.zeta;
        const double want = v::omega_bar_variant_direct(p.nu, p.chi, p.zeta, lambda);
        const double s4 = 10.0;  // arbitrary: zeta and lambda fix a2 and b2 (sigma_y^2 = 1 FS^2)
        const double a2 = (p.zeta * s4) * (p.zeta * s4);
        const double b2 = lambda * s4 * std::sqrt(a2) / 2.0;
        const double got = table.omega_bar_fs2(p.nu, p.chi * p.zeta, a2, b2, s4, 1.0, fallback);
        errors[n] = std::fabs(got / want - 1.0);
    });
    struct Region {
        const char* name;
        std::function<bool(const VariantPoint&)> keep;
    };
    const std::vector<Region> regions = {
        {"all", [](const VariantPoint&) { return true; }},
        {"zeta < 4", [](const VariantPoint& p) { return !p.first_marks; }},
        {"first marks (zeta >= 4)", [](const VariantPoint& p) { return p.first_marks; }},
        {"chi <= 30", [](const VariantPoint& p) { return p.chi <= 30.0; }},
        {"chi > 30", [](const VariantPoint& p) { return p.chi > 30.0; }},
        {"kappa = 0", [](const VariantPoint& p) { return p.kappa == 0.0; }},
        {"nu > 15.25 (between the last two upsilon nodes)", [](const VariantPoint& p) { return p.nu > 15.25; }}};
    std::cout << "built after " << fixed(built_s, 1) << " s; relative error of the table against "
              << "omega_bar_variant_direct on " << kPoints << " points (seed " << kSeed
              << "; design part 7's halves):\n";
    for (const Region& region : regions)
        std::cout << "  " << region.name << ": " << describe_variant(summarize(errors, points, region.keep)) << "\n";
    std::cout << "done after " << fixed(elapsed(), 1) << " s" << std::endl;
    const std::filesystem::path out(out_path);
    if (out.has_parent_path()) std::filesystem::create_directories(out.parent_path());
    table.save(out_path);
    std::cout << "wrote " << out.filename().generic_string() << " ("
              << std::filesystem::file_size(out) << " bytes)" << std::endl;
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        if (argc < 2) usage("no table named");
        const std::string table = argv[1];
        if (table == "omega-bar") return run_omega_bar(argc, argv);
        if (table == "omega-bar-variant") return run_omega_bar_variant(argc, argv);
        usage("unknown table " + table);
    } catch (const std::exception& e) {
        std::cerr << "kz4ap-viterbi-tables: " << e.what() << "\n";
        return 2;
    }
}
