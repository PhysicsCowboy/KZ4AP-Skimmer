// kz4ap-viterbi-tables: generates the new decoder's compiled-in tables (plan 1b, Ruling 65). It links the engine's
// sources without the generated tables (kz4ap_engine_core), so it builds before any table exists.
//
//   kz4ap-viterbi-tables weight --out FILE [--seeds 1,2] [--samples 4194304]
//       The weight w of a sample's evidence against its expected SNR q (design notes, "Detection, amplitude, frequency
//       and the mark's shape", part 4; engine/include/kz4ap/viterbi/weight.hpp), written as a C++ source of the 129
//       node values (engine/src/viterbi/weight_table.cpp). For each seed, one sequence of complex white noise (1 FS^2
//       per input sample) through the 192 kHz Channelizer (8 192-point FFT, the channel filter's 4 097 taps, decimation
//       128, 1 500 channel samples/s) and the front filter (25 taps: a 12 ms dot with 2.5 ms edges), read every 4th
//       channel sample after 1 s of warm-up, `samples` envelope samples, scaled by sigma_y = sqrt(N0 x 74.50 Hz)
//       (output_noise's sigma_y^2 / N0; N0 = 1 FS^2 / 192 000 Hz). The same noise serves every node. At each q, a steady
//       carrier of amplitude a = sqrt(q) sigma_y at full template (s = 1, V' = sigma_y^2) gives e_k = |a + n_k|, and
//           psi_k = dLambda_k/da = (-2 b + 2 z_k I1(2 b z_k) / I0(2 b z_k)) / sigma_y,
//           b = a / sigma_y, z_k = e_k / sigma_y,
//           w = Var(psi) / (Var(psi) + 2 sum_{m=1..11} Cov(psi_k, psi_{k+m})),
//       averaged over the seeds. Nodes i = 1 ... 127 at xi = i / 128 (q = xi / (1 - xi)); the end nodes are the derived
//       limits w_weak (xi = 0) and w_strong (xi = 1) from output_noise. It also evaluates the curve at the 128
//       midpoints and reports the linear interpolation's largest error there, the seeds' largest difference, and the
//       curve at the design's points. Progress and results go to stdout.
#include "kz4ap/channelizer.hpp"
#include "kz4ap/types.hpp"
#include "kz4ap/viterbi/front_filter.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <numbers>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

using kz4ap::Sample;

[[noreturn]] void usage(const std::string& why) {
    throw std::invalid_argument(why + "\nusage: kz4ap-viterbi-tables weight --out FILE [--seeds 1,2] [--samples N]");
}

// --- A portable generator: xoshiro256** seeded by splitmix64, Box-Muller for the normal deviates ---

std::uint64_t splitmix64(std::uint64_t& state) {
    std::uint64_t z = (state += 0x9e3779b97f4a7c15ULL);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
    return z ^ (z >> 31);
}

class Noise {
public:
    explicit Noise(std::uint64_t seed) {
        for (auto& s : state_) s = splitmix64(seed);
    }
    // A complex deviate of E|n|^2 = 1 (each component N(0, 1/2)).
    std::complex<double> next() {
        const double u1 = 1.0 - uniform();  // (0, 1]
        const double u2 = uniform();
        const double r = std::sqrt(-std::log(u1));
        const double phase = 2.0 * std::numbers::pi * u2;
        return {r * std::cos(phase), r * std::sin(phase)};
    }

private:
    static std::uint64_t rotl(std::uint64_t x, int k) { return (x << k) | (x >> (64 - k)); }
    double uniform() {  // [0, 1), 53 bits
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
    std::array<std::uint64_t, 4> state_{};
};

// --- I1(x) / I0(x) for x >= 0: Abramowitz and Stegun 9.8.1 to 9.8.4 (relative errors below 2.2e-7) ---

double bessel_ratio(double x) {
    if (x <= 3.75) {
        const double t = (x / 3.75) * (x / 3.75);
        const double i0 = 1.0 + t * (3.5156229 + t * (3.0899424 + t * (1.2067492 + t * (0.2659732 + t * (0.0360768 +
                                                                                                    t * 0.0045813)))));
        const double i1_over_x = 0.5 + t * (0.87890594 + t * (0.51498869 + t * (0.15084934 + t * (0.02658733 +
                                                                                             t * (0.00301532 +
                                                                                                  t * 0.00032411)))));
        return x * i1_over_x / i0;
    }
    const double t = 3.75 / x;
    const double p0 = 0.39894228 + t * (0.01328592 + t * (0.00225319 + t * (-0.00157565 + t * (0.00916281 +
                      t * (-0.02057706 + t * (0.02635537 + t * (-0.01647633 + t * 0.00392377)))))));
    const double p1 = 0.39894228 + t * (-0.03988024 + t * (-0.00362018 + t * (0.00163801 + t * (-0.01031555 +
                      t * (0.02282967 + t * (-0.02895312 + t * (0.01787654 - t * 0.00420059)))))));
    return p1 / p0;
}

// --- The chain's noise at the envelope samples ---

constexpr int kInputRate = 192000;     // samples/s
constexpr int kFftSize = 8192;
constexpr double kChannelRate = 1500.0;  // samples/s
constexpr int kSpacing = 4;              // channel samples per envelope sample
constexpr int kWarmupSamples = 375;      // envelope samples (1 s) left out while the filters fill
constexpr int kLags = 11;                // lags of Delta counted in the covariance sum

kz4ap::ChannelizerConfig chain_config() {
    kz4ap::ChannelizerConfig config;
    config.sample_rate = kInputRate;
    config.fft_size = kFftSize;
    return config;
}

std::vector<double> front_taps() { return kz4ap::viterbi::front_filter_taps(kChannelRate, 0.012, 0.0025); }

// n_k / sigma_y for `samples` envelope samples of the seed's noise.
std::vector<std::complex<double>> chain_noise(std::uint64_t seed, std::size_t samples, double sigma_y) {
    const kz4ap::ChannelizerConfig config = chain_config();
    kz4ap::Channelizer channelizer(config);
    channelizer.add_channel(1, channelizer.hz_to_bin(1000.0));
    kz4ap::viterbi::EnvelopeSampler sampler(front_taps(), kSpacing);
    Noise noise(seed);
    std::vector<std::complex<double>> out;
    out.reserve(samples);
    std::vector<kz4ap::viterbi::EnvelopeSample> envelope;
    std::vector<Sample> block(static_cast<std::size_t>(kInputRate));
    std::size_t seen = 0;
    while (out.size() < samples) {
        for (Sample& x : block) {
            const std::complex<double> n = noise.next();
            x = Sample(static_cast<float>(n.real()), static_cast<float>(n.imag()));
        }
        channelizer.push(block, [&](std::uint32_t, std::uint64_t first, std::span<const Sample> channel) {
            envelope.clear();
            sampler.push(channel, first, envelope);
            for (const auto& e : envelope) {
                if (seen++ >= static_cast<std::size_t>(kWarmupSamples) && out.size() < samples)
                    out.push_back(e.y_fs / sigma_y);
            }
        });
    }
    return out;
}

// w at q for one seed's normalized noise (header).
double weight_at(double q, const std::vector<std::complex<double>>& n) {
    const double b = std::sqrt(q);
    const std::size_t count = n.size();
    std::array<double, kLags + 1> products{};  // sum psi_k psi_{k+m}
    std::array<double, kLags> head{};          // the first kLags values of psi
    std::array<double, kLags> ring{};          // the last kLags values of psi, ring[k % kLags]
    double sum = 0.0;
    for (std::size_t k = 0; k < count; ++k) {
        const double z = std::abs(b + n[k]);
        const double psi = -2.0 * b + 2.0 * z * bessel_ratio(2.0 * b * z);
        sum += psi;
        products[0] += psi * psi;
        for (std::size_t m = 1; m <= kLags && m <= k; ++m) products[m] += ring[(k - m) % kLags] * psi;
        ring[k % kLags] = psi;
        if (k < kLags) head[k] = psi;
    }
    const double mean = sum / static_cast<double>(count);
    // Sum over k < count - m of (psi_k - mean)(psi_{k+m} - mean) from the raw sums: the products less mean x (the sum
    // without the last m values + the sum without the first m) + (count - m) mean^2.
    const auto centered = [&](std::size_t m) {
        double without_last = sum;
        double without_first = sum;
        for (std::size_t i = 0; i < m; ++i) {
            without_last -= ring[(count - 1 - i) % kLags];
            without_first -= head[i];
        }
        const double pairs = static_cast<double>(count - m);
        return (products[m] - mean * (without_last + without_first) + pairs * mean * mean) / pairs;
    };
    const double variance = centered(0);
    double covariances = 0.0;
    for (std::size_t m = 1; m <= kLags; ++m) covariances += centered(m);
    return variance / (variance + 2.0 * covariances);
}

std::vector<std::uint64_t> parse_seeds(const std::string& text) {
    std::vector<std::uint64_t> seeds;
    std::stringstream in(text);
    std::string item;
    while (std::getline(in, item, ',')) {
        if (item.empty()) usage("--seeds: an empty seed");
        seeds.push_back(std::stoull(item));
    }
    if (seeds.empty()) usage("--seeds: no seeds");
    return seeds;
}

std::string fixed(double x, int digits) {
    char buffer[64];
    std::snprintf(buffer, sizeof buffer, "%.*f", digits, x);
    return buffer;
}

int run_weight(int argc, char** argv) {
    std::string out_path;
    std::string seeds_text = "1,2";
    std::size_t samples = 4194304;
    for (int i = 2; i < argc; ++i) {
        const std::string arg = argv[i];
        if (i + 1 >= argc) usage(arg + " needs a value");
        if (arg == "--out")
            out_path = argv[++i];
        else if (arg == "--seeds")
            seeds_text = argv[++i];
        else if (arg == "--samples")
            samples = std::stoull(argv[++i]);
        else
            usage("unknown option " + arg);
    }
    if (out_path.empty()) usage("--out is required");
    if (samples < 1000) usage("--samples: at least 1000");
    const std::vector<std::uint64_t> seeds = parse_seeds(seeds_text);
    // The command as recorded: the output's file name only, never a path.
    const std::string command = "kz4ap-viterbi-tables weight --out " +
                                std::filesystem::path(out_path).filename().generic_string() + " --seeds " +
                                seeds_text + " --samples " + std::to_string(samples);
    const auto started = std::chrono::steady_clock::now();
    const auto elapsed = [&] {
        return std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    };

    const kz4ap::ChannelizerConfig config = chain_config();
    const std::vector<double> channel_taps = kz4ap::channel_filter_taps(config);
    const int decimation = config.fft_size / config.channel_bins;
    const kz4ap::viterbi::OutputNoise limits =
        kz4ap::viterbi::output_noise(channel_taps, kInputRate, decimation, front_taps(), kSpacing);
    const double n0 = 1.0 / kInputRate;  // FS^2/Hz: 1 FS^2 per input sample
    const double sigma_y = std::sqrt(n0 * limits.sigma2_per_n0_hz);
    std::cout << command << "\nchain: sigma_y^2 / N0 = " << fixed(limits.sigma2_per_n0_hz, 4)
              << " Hz, w_weak = " << fixed(limits.w_weak, 8) << ", w_strong = " << fixed(limits.w_strong, 8) << "\n";

    // The noise of every seed, one thread per seed.
    std::vector<std::vector<std::complex<double>>> noise(seeds.size());
    {
        std::vector<std::thread> threads;
        std::vector<std::exception_ptr> errors(seeds.size());
        for (std::size_t i = 0; i < seeds.size(); ++i) {
            threads.emplace_back([&, i] {
                try {
                    noise[i] = chain_noise(seeds[i], samples, sigma_y);
                } catch (...) {
                    errors[i] = std::current_exception();
                }
            });
        }
        for (auto& t : threads) t.join();
        for (const auto& e : errors)
            if (e) std::rethrow_exception(e);
    }
    for (std::size_t i = 0; i < seeds.size(); ++i) {
        double power = 0.0;
        for (const auto& x : noise[i]) power += std::norm(x);
        std::cout << "seed " << seeds[i] << ": " << noise[i].size() << " samples, mean |n|^2 / sigma_y^2 = "
                  << fixed(power / static_cast<double>(noise[i].size()), 5) << "\n";
    }
    std::cout << "noise done after " << fixed(elapsed(), 1) << " s\n";

    // The q values: nodes 1 ... 127, midpoints 0 ... 127, the design's points and the clamp's ends.
    constexpr int kNodes = 129;
    const auto q_of_xi = [](double xi) { return xi / (1.0 - xi); };
    std::vector<double> qs;
    for (int i = 1; i < kNodes - 1; ++i) qs.push_back(q_of_xi(i / 128.0));
    for (int i = 0; i < kNodes - 1; ++i) qs.push_back(q_of_xi((i + 0.5) / 128.0));
    const std::array<double, 12> design_db = {-40.0, -20.0, -10.0, -5.0, 0.0, 3.0, 6.0, 10.0, 15.0, 20.0, 30.0, 40.0};
    for (const double db : design_db) qs.push_back(std::pow(10.0, db / 10.0));
    // w per seed and q, the q values shared out among threads.
    std::vector<std::vector<double>> w(seeds.size(), std::vector<double>(qs.size()));
    {
        const unsigned workers = std::max(1u, std::min(16u, std::thread::hardware_concurrency()));
        std::atomic<std::size_t> next{0};
        std::vector<std::thread> threads;
        for (unsigned t = 0; t < workers; ++t) {
            threads.emplace_back([&] {
                for (std::size_t job = next++; job < qs.size() * seeds.size(); job = next++) {
                    const std::size_t seed = job / qs.size();
                    const std::size_t at = job % qs.size();
                    w[seed][at] = weight_at(qs[at], noise[seed]);
                }
            });
        }
        for (auto& t : threads) t.join();
    }
    std::vector<double> curve(qs.size());
    double spread = 0.0;
    for (std::size_t j = 0; j < qs.size(); ++j) {
        double total = 0.0;
        double low = w[0][j];
        double high = w[0][j];
        for (std::size_t s = 0; s < seeds.size(); ++s) {
            total += w[s][j];
            low = std::min(low, w[s][j]);
            high = std::max(high, w[s][j]);
        }
        curve[j] = total / static_cast<double>(seeds.size());
        spread = std::max(spread, high - low);
    }
    std::array<double, kNodes> table{};
    table[0] = limits.w_weak;
    table[kNodes - 1] = limits.w_strong;
    for (int i = 1; i < kNodes - 1; ++i) table[static_cast<std::size_t>(i)] = curve[static_cast<std::size_t>(i - 1)];
    double worst = 0.0;
    double worst_db = 0.0;
    for (int i = 0; i < kNodes - 1; ++i) {
        const double mid = curve[static_cast<std::size_t>(kNodes - 2 + i)];
        const double interpolated = 0.5 * (table[static_cast<std::size_t>(i)] + table[static_cast<std::size_t>(i) + 1]);
        const double error = std::fabs(interpolated - mid);
        if (error > worst) {
            worst = error;
            worst_db = 10.0 * std::log10(q_of_xi((i + 0.5) / 128.0));
        }
    }
    std::ostringstream points;
    for (std::size_t d = 0; d < design_db.size(); ++d) {
        points << (d == 0 ? "" : ", ") << fixed(design_db[d], 0) << " dB: "
               << fixed(curve[2 * (kNodes - 1) - 1 + d], 4);
    }
    std::cout << "the curve at the design's points (q in dB SNR in the filter's 74.48 Hz): " << points.str() << "\n";
    std::cout << "seeds' largest difference: " << fixed(spread, 5) << "\n";
    std::cout << "interpolation's largest error at the midpoints: " << fixed(worst, 5) << " at q = "
              << fixed(worst_db, 2) << " dB SNR in the filter's 74.48 Hz\n";
    std::cout << "done after " << fixed(elapsed(), 1) << " s\n";

    std::ofstream out(out_path, std::ios::binary);
    if (!out) throw std::runtime_error("cannot write " + out_path);
    std::ostringstream seeds_list;
    for (std::size_t i = 0; i < seeds.size(); ++i) seeds_list << (i == 0 ? "" : ", ") << seeds[i];
    out << "// Generated by kz4ap-viterbi-tables (bench/src/viterbi_tables_main.cpp); do not edit by hand.\n"
        << "//\n"
        << "// Command: " << command << "\n"
        << "// The weight w of a sample's evidence (engine/include/kz4ap/viterbi/weight.hpp; design notes, part 4): 129\n"
        << "// nodes at xi = q / (1 + q) = i / 128, i = 0 ... 128. The end nodes are the derived limits of the chain\n"
        << "// (output_noise): w_weak = " << fixed(limits.w_weak, 8) << " at xi = 0 and w_strong = "
        << fixed(limits.w_strong, 8) << " at xi = 1. The other 127 are\n"
        << "// measured: w = Var(psi) / (Var(psi) + 2 sum_{m=1..11} Cov(psi_k, psi_{k+m})), psi_k = dLambda_k/da at the\n"
        << "// true a for a steady carrier at full template, the mean over seeds " << seeds_list.str() << ", "
        << samples << " envelope samples\n"
        << "// per seed, one noise sequence per seed shared by every node.\n"
        << "// Chain: complex white noise (1 FS^2 per input sample) at 192 kHz through the Channelizer (8 192-point FFT,\n"
        << "// the channel filter's 4 097 taps, decimation 128, 1 500 channel samples/s) and the front filter (25 taps,\n"
        << "// a 12 ms dot with 2.5 ms edges), every 4th channel sample after 1 s of warm-up; sigma_y^2 / N0 = "
        << fixed(limits.sigma2_per_n0_hz, 2) << " Hz.\n"
        << "// The seeds' largest difference over every q evaluated: " << fixed(spread, 5) << ".\n"
        << "// Linear interpolation's largest error against the curve at the 128 midpoints: " << fixed(worst, 5)
        << " (at q = " << fixed(worst_db, 2) << " dB SNR\n"
        << "// in the filter's 74.48 Hz).\n"
        << "// The curve at the design's points (q in dB SNR in the filter's 74.48 Hz):";
    // The points, wrapped.
    {
        std::istringstream items(points.str());
        std::string item;
        std::string line;
        out << "\n";
        while (std::getline(items, item, ',')) {
            if (!item.empty() && item[0] == ' ') item.erase(0, 1);
            if (line.size() + item.size() + 2 > 112) {
                out << "//   " << line << ",\n";
                line.clear();
            }
            line += (line.empty() ? "" : ", ") + item;
        }
        if (!line.empty()) out << "//   " << line << ".\n";
    }
    out << "#include \"kz4ap/viterbi/weight.hpp\"\n\n"
        << "namespace kz4ap::viterbi {\n\n"
        << "const std::array<double, kWeightNodes> kWeightTable = {{\n";
    for (int i = 0; i < kNodes; ++i) {
        if (i % 8 == 0) out << "   ";
        out << " " << fixed(table[static_cast<std::size_t>(i)], 8) << (i + 1 < kNodes ? "," : "");
        if (i % 8 == 7 || i + 1 == kNodes) out << "\n";
    }
    out << "}};\n\n"
        << "}  // namespace kz4ap::viterbi\n";
    if (!out) throw std::runtime_error("cannot write " + out_path);
    std::cout << "wrote " << std::filesystem::path(out_path).filename().generic_string() << "\n";
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        if (argc < 2) usage("no table named");
        const std::string table = argv[1];
        if (table == "weight") return run_weight(argc, argv);
        usage("unknown table " + table);
    } catch (const std::exception& e) {
        std::cerr << "kz4ap-viterbi-tables: " << e.what() << "\n";
        return 2;
    }
}
