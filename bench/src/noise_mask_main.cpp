// kz4ap-noise-mask: measures the bank decoder's spectrum noise mask (Plan B, B4b) with the C++ estimator.
//
//   kz4ap-noise-mask [--set KEY=VALUE ...] white [--seeds FIRST-LAST] [--seconds S]
//       b_mask,k by stage 1's Task 5 method (default: seeds 101-110, 60 s each, white noise of 1 FS^2 per complex
//       sample at 1500 samples/s): the pooled table, each seed's ratios, the per-seed scatter, segments accepted
//       and offered, and the kept fraction.
//   kz4ap-noise-mask [--set KEY=VALUE ...] stream FILE [--rate R] [--from S --to S]
//       one complex64 stream (interleaved little-endian float32, FS) at R samples/s (default 1500): segments
//       offered and accepted, the kept fraction, masked_branch_power and N_k; with --from/--to, the same counts for
//       the segments starting in [S, S) of stream time, and accepted segments per second there.
//
// --set takes the ProtoConfig names of kz4ap-bank-replay (for example guard_margin_s=0.02). Output: JSON on stdout.
#include "bank_json.hpp"
#include "noise_mask.hpp"

#include "kz4ap/bank/filters.hpp"

#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using kz4ap::bench::ordered_json;

namespace {

[[noreturn]] void usage(const std::string& why) {
    throw std::invalid_argument(why +
                                "\nusage: kz4ap-noise-mask [--set KEY=VALUE ...] white [--seeds FIRST-LAST] "
                                "[--seconds S]\n       kz4ap-noise-mask [--set KEY=VALUE ...] stream FILE [--rate R] "
                                "[--from S --to S]");
}

ordered_json parse_value(const std::string& text) {  // as kz4ap-bank-replay: JSON if it parses, else a string
    try {
        return ordered_json::parse(text);
    } catch (const ordered_json::parse_error&) {
        return text;
    }
}

std::vector<std::complex<double>> read_c64(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot open " + path);
    std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (bytes.size() % 8 != 0) throw std::runtime_error(path + ": not whole complex64 samples");
    auto f32 = [&](std::size_t at) {
        std::uint32_t v = 0;
        for (int b = 3; b >= 0; --b) v = (v << 8) | bytes[at + static_cast<std::size_t>(b)];
        float x;
        std::memcpy(&x, &v, sizeof x);
        return static_cast<double>(x);
    };
    std::vector<std::complex<double>> u(bytes.size() / 8);
    for (std::size_t i = 0; i < u.size(); ++i) u[i] = {f32(8 * i), f32(8 * i + 4)};
    return u;
}

int run(int argc, char** argv) {
    kz4ap::bank::BankConfig cfg;
    std::vector<std::string> rest;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--set") {
            if (++i >= argc) usage("--set needs KEY=VALUE");
            const std::string kv = argv[i];
            const auto eq = kv.find('=');
            if (eq == std::string::npos) usage("--set needs KEY=VALUE");
            kz4ap::bench::set_config_value(cfg, kv.substr(0, eq), parse_value(kv.substr(eq + 1)));
        } else {
            rest.push_back(arg);
        }
    }
    if (rest.empty()) usage("no mode");
    const std::string mode = rest[0];
    double rate = 1500.0, seconds = 60.0, from = 0.0, to = 0.0;
    std::uint64_t first = 101, last = 110;
    std::string file;
    for (std::size_t i = 1; i < rest.size(); ++i) {
        const std::string& a = rest[i];
        auto next = [&]() -> std::string {
            if (i + 1 >= rest.size()) usage(a + " needs a value");
            return rest[++i];
        };
        if (a == "--seeds") {
            const std::string s = next();
            const auto dash = s.find('-');
            first = std::stoull(s.substr(0, dash));
            last = dash == std::string::npos ? first : std::stoull(s.substr(dash + 1));
        } else if (a == "--seconds") {
            seconds = std::stod(next());
        } else if (a == "--rate") {
            rate = std::stod(next());
        } else if (a == "--from") {
            from = std::stod(next());
        } else if (a == "--to") {
            to = std::stod(next());
        } else if (mode == "stream" && file.empty()) {
            file = a;
        } else {
            usage("unknown argument " + a);
        }
    }
    ordered_json out;
    out["guard_margin_s"] = cfg.guard_margin_s;
    out["rate_hz"] = rate;
    out["n_k"] = kz4ap::bank::branch_samples(kz4ap::bank::branch_lengths_s(cfg), rate);
    if (mode == "white") {
        std::vector<std::uint64_t> seeds;
        for (std::uint64_t s = first; s <= last; ++s) seeds.push_back(s);
        const auto b = kz4ap::bench::measure_mask_bias(cfg, rate, seeds, seconds);
        out["seeds"] = seeds;
        out["seconds"] = seconds;
        out["mask_bias"] = b.bias;
        out["scatter"] = b.scatter;
        out["per_seed"] = b.per_seed;
        out["segments"] = b.segments;
        out["segments_offered"] = b.segments_offered;
        out["kept_fraction"] = b.kept_fraction;
    } else if (mode == "stream") {
        if (file.empty()) usage("stream needs FILE");
        const auto u = read_c64(file);
        const auto r = kz4ap::bench::run_mask(cfg, u, rate, from, to);
        out["file"] = file;
        out["seconds"] = r.seconds;
        out["segments"] = r.segments;
        out["segments_offered"] = r.segments_offered;
        out["kept_fraction"] = r.segments_offered > 0 ? r.kept_fraction_sum / r.segments_offered : 0.0;
        out["masked_power"] = r.masked_power;
        if (from < to) {
            out["from_s"] = from;
            out["to_s"] = to;
            out["window_segments"] = r.window_segments;
            out["window_offered"] = r.window_offered;
            out["window_kept_fraction"] = r.window_offered > 0 ? r.window_kept_fraction_sum / r.window_offered : 0.0;
            out["window_accepted_per_s"] = r.window_segments / r.window_s;
        }
    } else {
        usage("unknown mode " + mode);
    }
    std::cout << out.dump(1) << "\n";
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        return run(argc, argv);
    } catch (const std::exception& e) {
        std::cerr << "kz4ap-noise-mask: " << e.what() << "\n";
        return 2;
    }
}
