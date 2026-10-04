// kz4ap-bank-replay: decodes the recorded oracle channel streams of a suite with the C++ bank decoder
// (kz4ap::bank::BankChannel) and writes the prototype's decoded files, so stage 1's Python tooling
// (kz4ap_proto.runner score, kz4ap_proto.experiments compare) reads them unchanged. The C++ counterpart of
// `python -m kz4ap_proto.runner decode` for oracle test cases:
//
//   kz4ap-bank-replay --out DIR --name NAME [--only REGEX] [--set KEY=VALUE ...] [--jobs N]
//
// reads DIR/manifest.json and, for every oracle test case (the oracle recordings' test cases and the oracle copies
// of the detector-path groups, as kz4ap_synth.suites names them) whose result name matches REGEX (searched, as
// Python's re.search), DIR/channels/<result>/channels.json and its channel files (complex64, little-endian);
// mixes each channel as streams.ChannelStream.baseband() does for a labeled channel (the labeled offset, and the
// label's drift from the label's start); decodes it; and writes DIR/proto/NAME/<result>.decoded.json in
// runner.decode's format (json.dumps of the same keys and values; "config" is the BankConfig with the --set
// values). A test case whose decoded file exists with an equal config is skipped, as runner.decode does. The
// channels of the detector path (non-oracle recordings) are not decoded here.
#include "bank_json.hpp"
#include "cpu_time.hpp"

#include "kz4ap/bank/channel.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <complex>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <mutex>
#include <numbers>
#include <optional>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;
using kz4ap::bench::ordered_json;

namespace {

struct Args {
    fs::path out;
    std::string name;
    std::optional<std::string> only;
    std::vector<std::pair<std::string, std::string>> sets;
    int jobs = 0;
};

[[noreturn]] void usage(const std::string& why) {
    throw std::invalid_argument(why +
                                "\nusage: kz4ap-bank-replay --out DIR --name NAME [--only REGEX] [--set KEY=VALUE ...] "
                                "[--jobs N]");
}

Args parse_args(int argc, char** argv) {
    Args a;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto value = [&]() -> std::string {
            if (i + 1 >= argc) usage(arg + " needs a value");
            return argv[++i];
        };
        if (arg == "--out") {
            a.out = value();
        } else if (arg == "--name") {
            a.name = value();
        } else if (arg == "--only") {
            a.only = value();
        } else if (arg == "--set") {
            const std::string kv = value();
            const auto eq = kv.find('=');
            if (eq == std::string::npos) usage("--set needs KEY=VALUE");
            a.sets.emplace_back(kv.substr(0, eq), kv.substr(eq + 1));
        } else if (arg == "--jobs") {
            a.jobs = std::stoi(value());
        } else {
            usage("unknown argument " + arg);
        }
    }
    if (a.out.empty() || a.name.empty()) usage("--out and --name are required");
    return a;
}

ordered_json read_json(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot open " + path.string());
    return ordered_json::parse(in);
}

// runner.parse_values: VALUE is JSON if it parses, else a string.
ordered_json parse_value(const std::string& text) {
    try {
        return ordered_json::parse(text);
    } catch (const ordered_json::parse_error&) {
        return ordered_json(text);
    }
}

// One oracle test case (runner.oracle_scorings).
struct Job {
    std::string result;
    std::string wav;
    std::string labels;
};

// kz4ap_synth.suites.ORACLE_COPY_GROUPS: the groups of the detector path that also get an oracle copy (a test in
// training/tests/test_suites.py checks that the two lists are equal).
const std::vector<std::string> kOracleCopyGroups = {"pauses", "strong", "tune-up", "first sample", "band", "crowded"};

std::vector<Job> oracle_jobs(const ordered_json& manifest, const std::optional<std::regex>& only) {
    std::vector<Job> jobs;
    for (const auto& rec : manifest.at("recordings")) {
        std::vector<std::pair<std::string, std::string>> scorings;  // (labels file, result name)
        const std::string name = rec.at("name").get<std::string>();
        if (rec.at("oracle").get<bool>()) {
            scorings.emplace_back(rec.at("labels").get<std::string>(), name);
            if (rec.contains("station_labels") && rec.at("station_labels").is_string() &&
                !rec.at("station_labels").get<std::string>().empty())
                scorings.emplace_back(rec.at("station_labels").get<std::string>(), name + ".stations");
        } else {
            const std::string group = rec.at("group").get<std::string>();
            if (std::find(kOracleCopyGroups.begin(), kOracleCopyGroups.end(), group) != kOracleCopyGroups.end())
                scorings.emplace_back(rec.at("labels").get<std::string>(), name + ".oracle");
        }
        for (const auto& [labels, result] : scorings)
            if (!only || std::regex_search(result, *only))
                jobs.push_back(Job{result, rec.at("wav").get<std::string>(), labels});
    }
    return jobs;
}

// streams.baseband for a labeled channel: u[n] = y[n] exp(-j(2 pi f_off t + pi fdot max(t - t0, 0)^2)),
// t = (first index + n) / r, in numpy's order of operations: 2 pi f_off first, then times t; the drift term
// (pi fdot) times the square; exp(-j phase) = cos(phase) - j sin(phase) as numpy's complex exp gives it for a
// zero real part; the complex product written out as numpy computes it.
std::vector<std::complex<double>> baseband(const std::vector<std::complex<double>>& y, double rate_hz, double f_off_hz,
                                           double drift_hz_per_s, double start_s, std::int64_t first_sample_index) {
    std::vector<std::complex<double>> u(y.size());
    const double w = 2.0 * std::numbers::pi * f_off_hz;
    const double k = std::numbers::pi * drift_hz_per_s;
    for (std::size_t n = 0; n < y.size(); ++n) {
        const double t = static_cast<double>(first_sample_index + static_cast<std::int64_t>(n)) / rate_hz;
        double phase = w * t;
        if (drift_hz_per_s != 0.0) {
            const double dt = std::max(t - start_s, 0.0);
            phase = phase + k * (dt * dt);
        }
        // -1j * phase = (0, -phase); exp of it = (exp(0) cos(-phase), exp(0) sin(-phase))
        const double er = std::cos(-phase);
        const double ei = std::sin(-phase);
        const double yr = y[n].real(), yi = y[n].imag();
        u[n] = {yr * er - yi * ei, yr * ei + yi * er};
    }
    return u;
}

std::vector<std::complex<double>> read_c64(const fs::path& path, std::size_t samples) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot open " + path.string());
    std::vector<float> raw(2 * samples);
    in.read(reinterpret_cast<char*>(raw.data()), static_cast<std::streamsize>(raw.size() * sizeof(float)));
    if (static_cast<std::size_t>(in.gcount()) != raw.size() * sizeof(float) ||
        in.peek() != std::ifstream::traits_type::eof())
        throw std::runtime_error(path.filename().string() + ": not " + std::to_string(samples) +
                                 " samples, as the manifest says");
    std::vector<std::complex<double>> y(samples);
    for (std::size_t i = 0; i < samples; ++i) y[i] = {raw[2 * i], raw[2 * i + 1]};  // little-endian hosts
    return y;
}

struct Work {
    std::size_t job;
    int position;
};

struct Pending {
    Job job;
    fs::path record_dir;
    ordered_json manifest;  // channels.json
    ordered_json labels;    // the labels file
    std::size_t count = 0;
    std::vector<ordered_json> channels;
};

ordered_json decode_one(const kz4ap::bank::BankConfig& cfg, const Pending& p, int position) {
    const auto& ch = p.manifest.at("channels").at(static_cast<std::size_t>(position));
    const double rate = p.manifest.at("sample_rate_hz").get<double>();
    const int label_index = ch.at("label_index").get<int>();
    const auto& label = p.labels.at("signals").at(static_cast<std::size_t>(label_index));
    const auto samples = ch.at("samples").get<std::size_t>();
    const auto y = read_c64(p.record_dir / ch.at("file").get<std::string>(), samples);
    const double f_off = ch.at("label_freq_hz").get<double>() - ch.at("center_hz").get<double>();
    // float(label.get("drift_hz_per_s") or 0.0)
    double drift = 0.0;
    if (label.contains("drift_hz_per_s") && label.at("drift_hz_per_s").is_number())
        drift = label.at("drift_hz_per_s").get<double>();
    const double start_s = label.at("start_s").get<double>();
    const auto first = ch.at("first_sample_index").get<std::int64_t>();
    const double started = kz4ap::bench::thread_cpu_seconds();
    const auto u = baseband(y, rate, f_off, drift, start_s, first);
    kz4ap::bank::BankChannel channel(cfg, rate);
    channel.push(u);
    channel.finish();
    ordered_json out = kz4ap::bench::to_json(channel.result());
    out["label_index"] = label_index;
    out["rate_hz"] = rate;
    out["cpu_s"] = kz4ap::bench::thread_cpu_seconds() - started;
    // Plan B (B3): the noise estimate's stuck-level recoveries and skipped blocks of exact zeros
    out["noise_recoveries"] = channel.noise_recoveries();
    out["noise_zero_blocks"] = channel.noise_zero_blocks();
    out["channel_s"] = static_cast<double>(samples) / rate;
    return out;
}

void write_decoded(const fs::path& target, const std::string& name, const ordered_json& cfg_json, Pending& p) {
    std::sort(p.channels.begin(), p.channels.end(), [](const ordered_json& a, const ordered_json& b) {
        return a.at("label_index").get<int>() < b.at("label_index").get<int>();
    });
    ordered_json body = ordered_json::object();
    body["front_end"] = name;
    body["recording"] = p.job.wav;
    body["labels"] = p.job.labels;
    body["config"] = cfg_json;
    ordered_json texts = ordered_json::array();
    for (const auto& c : p.channels) texts.push_back(c.at("text"));
    body["texts"] = std::move(texts);
    body["channels"] = ordered_json(p.channels);
    const fs::path path = target / (p.job.result + ".decoded.json");
    const fs::path partial = target / (p.job.result + ".decoded.json.partial");
    {
        std::ofstream out(partial, std::ios::binary);
        out << kz4ap::bench::py_dumps(body) << "\n";
        if (!out) throw std::runtime_error("cannot write " + partial.string());
    }
    fs::rename(partial, path);  // a crash mid-write leaves no complete-looking decoded file
    std::cout << "decoded " << p.job.result << std::endl;
}

int run(const Args& args) {
    kz4ap::bank::BankConfig cfg;
    for (const auto& [key, text] : args.sets) kz4ap::bench::set_config_value(cfg, key, parse_value(text));
    const ordered_json cfg_json = kz4ap::bench::to_json(cfg);
    std::optional<std::regex> only;
    if (args.only) only.emplace(*args.only, std::regex::ECMAScript);
    const ordered_json manifest = read_json(args.out / "manifest.json");
    const fs::path target = args.out / "proto" / args.name;
    fs::create_directories(target);

    std::vector<Pending> pending;
    std::vector<Work> work;
    int skipped = 0;
    for (const auto& job : oracle_jobs(manifest, only)) {
        const fs::path decoded = target / (job.result + ".decoded.json");
        if (fs::exists(decoded)) {
            try {
                if (read_json(decoded).value("config", ordered_json()) == cfg_json) {
                    ++skipped;
                    continue;
                }
            } catch (const ordered_json::exception&) {
                // unreadable: decode it again
            }
        }
        Pending p;
        p.job = job;
        p.record_dir = args.out / "channels" / job.result;
        p.manifest = read_json(p.record_dir / "channels.json");
        p.labels = read_json(args.out / job.labels);
        p.count = p.manifest.at("channels").size();
        pending.push_back(std::move(p));
        for (std::size_t i = 0; i < pending.back().count; ++i)
            work.push_back(Work{pending.size() - 1, static_cast<int>(i)});
    }
    if (skipped) std::cout << "skipped " << skipped << " recordings already decoded with this config" << std::endl;
    std::mutex mutex;
    for (auto& p : pending)
        if (p.count == 0) write_decoded(target, args.name, cfg_json, p);

    std::atomic<std::size_t> next{0};
    std::vector<std::string> errors;
    auto worker = [&]() {
        for (;;) {
            const std::size_t i = next.fetch_add(1);
            if (i >= work.size()) return;
            const Work& w = work[i];
            Pending& p = pending[w.job];
            try {
                ordered_json channel = decode_one(cfg, p, w.position);
                const std::lock_guard<std::mutex> lock(mutex);
                p.channels.push_back(std::move(channel));
                if (p.channels.size() == p.count) {
                    write_decoded(target, args.name, cfg_json, p);
                    p.channels.clear();
                    p.channels.shrink_to_fit();
                }
            } catch (const std::exception& e) {
                const std::lock_guard<std::mutex> lock(mutex);
                errors.push_back("decoding " + p.record_dir.string() + ", channel position " +
                                 std::to_string(w.position) + " failed: " + e.what());
            }
        }
    };
    const unsigned hw = std::thread::hardware_concurrency();
    const int workers = args.jobs > 0 ? args.jobs : std::max(1, static_cast<int>(hw) - 2);
    std::vector<std::thread> threads;
    for (int t = 0; t < workers; ++t) threads.emplace_back(worker);
    for (auto& t : threads) t.join();
    for (const auto& e : errors) std::cerr << e << "\n";
    return errors.empty() ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        return run(parse_args(argc, argv));
    } catch (const std::exception& e) {
        std::cerr << "kz4ap-bank-replay: " << e.what() << "\n";
        return 2;
    }
}
