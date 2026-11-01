// kz4ap-bench run as the program it is, over a small recording made here: what main.cpp does with the options and the
// files, which the unit tests of the functions it calls (viterbi_json_test.cpp) do not reach. Two oracle channels
// (2.5 s of "CQ" and "TEST" over DEV2's noise), the new decoder, the statistics on.
//
// Each run is a child process (std::system, the program's own path from CMake), so the exit status, what the program
// prints and the files it leaves are the ones a script sees. The tests check: the configuration, the settings and the
// genie in the result file, and --stats forcing the statistics on; the statistics file in both forms (version 2, the
// default, and version 1 with --stats-detail) and that the summary is the reduction of the full file of the same run;
// that two runs write byte-identical files (the determinism the plan asks for); the result file written before the
// statistics, so that a statistics file that cannot be finished loses nothing of the run; a finished file whose last
// step, the rename into place, fails kept under its own name (the message says where); a destination that cannot be
// created refused before the run, not after it; one path given for the result and the statistics refused; the oracle
// channels reported at their labels' frequencies, which they follow; and a per-station label of a drifting QSO refused.
#include "viterbi_json.hpp"

#include "../../third_party/pocketfft/pocketfft_hdronly.h"

#include "viterbi/keyed_signals.hpp"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <numbers>
#include <random>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#ifndef _WIN32
#include <sys/wait.h>
#endif

namespace {

namespace fs = std::filesystem;
using nlohmann::json;

constexpr double kRate = 48000.0;
constexpr double kDuration = 2.5;

void put_u16(std::string& b, std::uint16_t v) {
    b.push_back(static_cast<char>(v & 0xFF));
    b.push_back(static_cast<char>(v >> 8));
}

void put_u32(std::string& b, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) b.push_back(static_cast<char>((v >> (8 * i)) & 0xFF));
}

// A 16-bit stereo WAV file: left I, right Q.
void write_wav(const fs::path& path, const std::vector<kz4ap::Sample>& samples, int rate) {
    std::string data;
    for (const kz4ap::Sample& s : samples) {
        for (const float x : {s.real(), s.imag()}) {
            const long v = std::clamp(std::lround(static_cast<double>(x) * 32767.0), -32768L, 32767L);
            put_u16(data, static_cast<std::uint16_t>(static_cast<std::int16_t>(v)));
        }
    }
    std::string fmt;
    put_u16(fmt, 1);
    put_u16(fmt, 2);
    put_u32(fmt, static_cast<std::uint32_t>(rate));
    put_u32(fmt, static_cast<std::uint32_t>(rate * 4));
    put_u16(fmt, 4);
    put_u16(fmt, 16);
    std::string body = "WAVEfmt ";
    put_u32(body, static_cast<std::uint32_t>(fmt.size()));
    body += fmt;
    body += "data";
    put_u32(body, static_cast<std::uint32_t>(data.size()));
    body += data;
    std::string file = "RIFF";
    put_u32(file, static_cast<std::uint32_t>(body.size()));
    file += body;
    std::ofstream(path, std::ios::binary).write(file.data(), static_cast<std::streamsize>(file.size()));
}

std::string slurp(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

json read_json(const fs::path& path) { return json::parse(slurp(path)); }

bool contains(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }

std::string quoted(const std::string& s) { return "\"" + s + "\""; }

struct Outcome {
    int status = -1;
    std::string out;  // what the program printed
    std::string err;
};

class BenchRun : public ::testing::Test {
protected:
    fs::path dir_;
    fs::path wav_;
    fs::path labels_;

    void SetUp() override {
        const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
        dir_ = fs::temp_directory_path() / (std::string("kz4ap_bench_run_") + info->name());
        fs::remove_all(dir_);
        fs::create_directories(dir_);
        wav_ = dir_ / "recording.wav";
        labels_ = dir_ / "recording.json";
        write_recording();
    }

    void TearDown() override {
        std::error_code ignored;
        fs::remove_all(dir_, ignored);
    }

    // "CQ" at 25 WPM, 300 Hz above the span's center, and "TEST" at 20 WPM, 900 Hz below it, each at S500 +15 dB over
    // complex white noise of 0.02 FS (DEV2's): a = sqrt(10^(15 / 10) x sigma^2 x 500 Hz / 48 kHz) = 0.0115 FS.
    void write_recording() {
        const double amplitude = std::sqrt(std::pow(10.0, 1.5) * 0.02 * 0.02 * 500.0 / kRate);
        std::vector<kz4ap::Sample> x = kz4ap::test::keyed_signal("CQ", 25.0, kRate, kDuration, 300.0, amplitude, 0.02, 3, 0.3);
        const std::vector<kz4ap::Sample> y =
            kz4ap::test::keyed_signal("TEST", 20.0, kRate, kDuration, -900.0, amplitude, 0.0, 1, 0.3);
        for (std::size_t i = 0; i < x.size(); ++i) x[i] += y[i];
        write_wav(wav_, x, static_cast<int>(kRate));
        json signals = json::array();
        for (const auto& [text, wpm, freq] : {std::tuple<const char*, double, double>{"CQ", 25.0, 300.0},
                                              std::tuple<const char*, double, double>{"TEST", 20.0, -900.0}}) {
            signals.push_back({{"text", text}, {"freq_offset_hz", freq}, {"wpm", wpm}, {"snr_db", 15.0},
                               {"start_s", 0.3}, {"end_s", kz4ap::test::keying(text, wpm, 0.3).back().second},
                               {"keying", "machine"}});
        }
        std::ofstream(labels_) << json{{"sample_rate", 48000}, {"duration_s", kDuration}, {"signals", signals}}.dump(2)
                               << '\n';
    }

    // The bench on the recording and its labels, with `options`; what it printed is kept in files of the test's folder.
    Outcome run(const std::vector<std::string>& options, bool with_labels = true) {
        std::string command = quoted(fs::path(KZ4AP_BENCH_EXE).make_preferred().string()) + " " + quoted(wav_.string());
        if (with_labels) command += " --labels " + quoted(labels_.string());
        for (const std::string& option : options) command += " " + quoted(option);
        const fs::path out = dir_ / "stdout.txt", err = dir_ / "stderr.txt";
        command += " > " + quoted(out.string()) + " 2> " + quoted(err.string());
#ifdef _WIN32
        command = "\"" + command + "\"";  // cmd /c removes the first and the last quote of a command that has more
#endif
        const int raw = std::system(command.c_str());
        Outcome outcome;
#ifdef _WIN32
        outcome.status = raw;
#else
        outcome.status = WIFEXITED(raw) ? WEXITSTATUS(raw) : -1;
#endif
        outcome.out = slurp(out);
        outcome.err = slurp(err);
        return outcome;
    }

    std::string path(const char* name) const { return (dir_ / name).string(); }

    // The options of a viterbi run on the oracle channels that writes `result` and `stats`.
    std::vector<std::string> viterbi(const std::string& result, const std::string& stats,
                                     std::vector<std::string> more = {}) const {
        std::vector<std::string> options = {"--oracle", "--decoder", "viterbi", "--no-timing", "--json", result,
                                            "--stats", stats};
        options.insert(options.end(), more.begin(), more.end());
        return options;
    }
};

TEST_F(BenchRun, TheResultFileHoldsTheConfigurationTheSettingsAndTheGenie) {
    const Outcome o = run(viterbi(path("result.json"), path("stats.json"),
                                  {"--set", "beam_nats=30", "--set", "ceiling=500", "--genie", "fading"}));
    ASSERT_EQ(o.status, 0) << o.err;
    const json r = read_json(path("result.json"));
    EXPECT_EQ(r.at("decoder"), "viterbi");
    EXPECT_EQ(r.at("recording"), "recording.wav");
    EXPECT_EQ(r.at("config").at("beam_nats"), 30.0);
    EXPECT_EQ(r.at("config").at("ceiling"), 500);
    EXPECT_EQ(r.at("config").at("stats"), true);  // --stats turns the search's statistics on
    EXPECT_EQ(r.at("settings"), json::array({"beam_nats=30", "ceiling=500"}));
    EXPECT_EQ(r.at("genie").at("kinds"), json::array({"fading"}));
    ASSERT_EQ(r.at("genie").at("channels").size(), 2u);
    EXPECT_EQ(r.at("genie").at("channels").at(0).at("fading_spread_hz"), 0.0);  // the labels' truth: no fading
    EXPECT_FALSE(r.contains("timing"));                                          // --no-timing
    ASSERT_EQ(r.at("tracks").size(), 2u);
    EXPECT_EQ(r.at("score").at("signals").size(), 2u);
    EXPECT_GT(r.at("channel_seconds").get<double>(), 4.0);
    // a run without --stats leaves the statistics off in the configuration
    const Outcome plain = run({"--oracle", "--decoder", "viterbi", "--no-timing", "--json", path("plain.json")});
    ASSERT_EQ(plain.status, 0) << plain.err;
    EXPECT_EQ(read_json(path("plain.json")).at("config").at("stats"), false);
}

TEST_F(BenchRun, TheStatisticsFileIsASummaryByDefault) {
    const Outcome o = run(viterbi(path("result.json"), path("stats.json")));
    ASSERT_EQ(o.status, 0) << o.err;
    const json s = read_json(path("stats.json"));
    EXPECT_EQ(s.at("format"), "kz4ap-viterbi-search-stats");
    EXPECT_EQ(s.at("version"), 2);
    EXPECT_EQ(s.at("recording"), "recording.wav");
    EXPECT_EQ(s.at("config").at("stats"), true);
    ASSERT_EQ(s.at("channels").size(), 2u);
    for (std::size_t i = 0; i < 2; ++i) {
        const json& c = s.at("channels").at(i);
        EXPECT_EQ(c.at("track_id"), i + 1);
        EXPECT_EQ(c.at("label_index"), i);
        const std::int64_t samples = c.at("samples").get<std::int64_t>();
        EXPECT_GT(samples, 900);
        std::int64_t in_histogram = 0, in_fates = 0;
        for (const json& n : c.at("live_histogram")) in_histogram += n.get<std::int64_t>();
        for (const json& a : c.at("fates"))
            for (const json& b : a)
                for (const json& d : b)
                    for (const json& n : d) in_fates += n.get<std::int64_t>();
        EXPECT_EQ(in_histogram, samples);
        EXPECT_EQ(in_fates, c.at("lines").get<std::int64_t>());
        // the lines that led: equal columns, the first story among them
        const json& led = c.at("led_lines");
        ASSERT_GE(led.at("line").size(), 1u);
        EXPECT_EQ(led.at("line").at(0), 0);
        for (const char* column : {"birth_s", "kind", "first_led_s", "largest_deficit_nats", "end", "end_s",
                                   "ended_in_first_element"})
            EXPECT_EQ(led.at(column).size(), led.at("line").size()) << column;
        EXPECT_FALSE(c.contains("live_mean_100ms"));  // the version 1 series is not in the summary
    }
    // a summary is small: nothing like the 45 kB per channel-second of the full form
    EXPECT_LT(fs::file_size(path("stats.json")), 60000u);
}

TEST_F(BenchRun, StatsDetailWritesTheFullFileAndTheSummaryIsItsReduction) {
    ASSERT_EQ(run(viterbi(path("result-summary.json"), path("summary.json"))).status, 0);
    const Outcome o = run(viterbi(path("result-lines.json"), path("lines.json"), {"--stats-detail"}));
    ASSERT_EQ(o.status, 0) << o.err;
    const json full = read_json(path("lines.json"));
    EXPECT_EQ(full.at("version"), 1);
    ASSERT_EQ(full.at("channels").size(), 2u);
    const json& lines = full.at("channels").at(0).at("lines");
    const std::size_t n = lines.at("kind").size();
    EXPECT_GT(n, 1000u);
    for (const char* column : {"birth_s", "first_led_s", "largest_deficit_nats", "end", "end_s", "ended_in_first_element"})
        EXPECT_EQ(lines.at(column).size(), n) << column;
    EXPECT_TRUE(full.at("channels").at(0).contains("live_mean_100ms"));
    // the same run in the two forms: the summary is what the bench's reduction gives of the full file
    const kz4ap::bench::StatsSummaryFile reduced = kz4ap::bench::summarize(kz4ap::bench::read_stats_file(path("lines.json")));
    const kz4ap::bench::StatsSummaryFile written = kz4ap::bench::read_stats_summary_file(path("summary.json"));
    EXPECT_EQ(reduced.recording, written.recording);
    ASSERT_EQ(reduced.channels.size(), written.channels.size());
    for (std::size_t i = 0; i < written.channels.size(); ++i) EXPECT_EQ(reduced.channels[i], written.channels[i]) << i;
    // the result files agree too, but for nothing: --stats-detail changes the statistics file only
    EXPECT_EQ(slurp(path("result-summary.json")), slurp(path("result-lines.json")));
    // a full file is far larger than the summary of the same run
    EXPECT_GT(fs::file_size(path("lines.json")), 10 * fs::file_size(path("summary.json")));
}

TEST_F(BenchRun, TwoRunsWriteIdenticalFiles) {
    for (const char* tag : {"a", "b"}) {
        const std::string t = tag;
        ASSERT_EQ(run(viterbi(path(("result-" + t + ".json").c_str()), path(("summary-" + t + ".json").c_str()))).status, 0);
        ASSERT_EQ(run(viterbi(path(("detail-result-" + t + ".json").c_str()), path(("lines-" + t + ".json").c_str()),
                              {"--stats-detail", "--set", "beam_nats=30"}))
                      .status,
                  0);
    }
    const std::string result = slurp(path("result-a.json")), summary = slurp(path("summary-a.json"));
    EXPECT_FALSE(result.empty() || summary.empty());
    EXPECT_EQ(result, slurp(path("result-b.json")));
    EXPECT_EQ(summary, slurp(path("summary-b.json")));
    EXPECT_EQ(slurp(path("detail-result-a.json")), slurp(path("detail-result-b.json")));
    EXPECT_EQ(slurp(path("lines-a.json")), slurp(path("lines-b.json")));
    EXPECT_NE(result, slurp(path("detail-result-a.json")));  // and the files do depend on the settings
}

TEST_F(BenchRun, TheResultFileIsWrittenFirstSoAStatisticsFileThatFailsLosesNothing) {
    // the statistics file's name is a folder: the run cannot rename its file onto it when it is done
    const fs::path blocked = dir_ / "stats-is-a-folder";
    fs::create_directories(blocked);
    const Outcome o = run(viterbi(path("result.json"), blocked.string()));
    EXPECT_EQ(o.status, 2);
    EXPECT_TRUE(contains(o.err, "kz4ap-bench:")) << o.err;
    EXPECT_TRUE(contains(o.err, "stats-is-a-folder")) << o.err;
    EXPECT_TRUE(contains(o.out, "CER")) << o.out;  // the run was scored and printed
    ASSERT_TRUE(fs::exists(path("result.json")));
    const json r = read_json(path("result.json"));  // whole
    EXPECT_EQ(r.at("decoder"), "viterbi");
    EXPECT_EQ(r.at("tracks").size(), 2u);
    EXPECT_TRUE(fs::is_directory(blocked));
    EXPECT_FALSE(fs::exists(path("result.json.partial")));
    // The statistics file was finished: only its last step failed. It stays under the name it was written with, whole,
    // and the message says where it is, so that a person can rename it.
    const std::string kept = blocked.string() + ".partial";
    EXPECT_TRUE(contains(o.err, kept)) << o.err;
    ASSERT_TRUE(fs::exists(kept));
    const json s = read_json(kept);
    EXPECT_EQ(s.at("format"), "kz4ap-viterbi-search-stats");
    EXPECT_EQ(s.at("version"), 2);
    EXPECT_EQ(s.at("channels").size(), 2u);
}

TEST_F(BenchRun, AResultFileThatCannotBeRenamedIntoPlaceIsKeptWhole) {
    // the result file's name is a folder: the run is scored and printed, the file is written, and only the rename fails
    const fs::path blocked = dir_ / "result-is-a-folder";
    fs::create_directories(blocked);
    const Outcome o = run(viterbi(blocked.string(), path("stats.json")));
    EXPECT_EQ(o.status, 2);
    EXPECT_TRUE(contains(o.out, "CER")) << o.out;
    const std::string kept = blocked.string() + ".partial";
    EXPECT_TRUE(contains(o.err, kept)) << o.err;  // the message says where the finished result is
    ASSERT_TRUE(fs::exists(kept));
    const json r = read_json(kept);  // the whole result of the run
    EXPECT_EQ(r.at("decoder"), "viterbi");
    EXPECT_EQ(r.at("tracks").size(), 2u);
    EXPECT_EQ(r.at("score").at("signals").size(), 2u);
    EXPECT_TRUE(fs::is_directory(blocked));
}

TEST_F(BenchRun, TheResultAndTheStatisticsMayNotBeTheSameFile) {
    // One path for both would let the statistics replace the result, which costs the run: refused at once, before the
    // recording is read, however the one path is spelled (and nothing is printed or written).
    const std::vector<std::string> spellings = {path("same.json"), (dir_ / "." / "same.json").string(),
                                                (dir_ / "no-such-folder" / ".." / "same.json").string()};
    for (const std::string& stats : spellings) {
        const Outcome o = run(viterbi(path("same.json"), stats));
        EXPECT_EQ(o.status, 2) << stats;
        EXPECT_TRUE(contains(o.err, "--json and --stats name the same file")) << o.err;
        EXPECT_TRUE(o.out.empty()) << o.out;
        EXPECT_FALSE(fs::exists(path("same.json"))) << stats;
        EXPECT_FALSE(fs::exists(path("same.json.partial"))) << stats;
    }
    // a result that is already there is left as it is, whichever option names it
    ASSERT_EQ(run({"--oracle", "--decoder", "viterbi", "--no-timing", "--json", path("same.json")}).status, 0);
    const std::string before = slurp(path("same.json"));
    ASSERT_FALSE(before.empty());
    for (const std::string& stats : spellings) {
        EXPECT_EQ(run(viterbi(path("same.json"), stats)).status, 2) << stats;
        EXPECT_EQ(slurp(path("same.json")), before) << stats;
    }
    // two files in one folder are two files
    EXPECT_EQ(run(viterbi(path("result.json"), path("stats.json"))).status, 0);
}

TEST_F(BenchRun, ADestinationThatCannotBeCreatedIsRefusedBeforeTheRun) {
    // a folder that does not exist: found at once, not after the run (nothing printed, nothing written)
    const Outcome stats = run(viterbi(path("result.json"), (dir_ / "no-such-folder" / "stats.json").string()));
    EXPECT_EQ(stats.status, 2);
    EXPECT_TRUE(contains(stats.err, "stats.json")) << stats.err;
    EXPECT_TRUE(stats.out.empty()) << stats.out;
    EXPECT_FALSE(fs::exists(path("result.json")));
    const Outcome result = run(viterbi((dir_ / "no-such-folder" / "result.json").string(), path("stats.json")));
    EXPECT_EQ(result.status, 2);
    EXPECT_TRUE(contains(result.err, "result.json")) << result.err;
    EXPECT_TRUE(result.out.empty()) << result.out;
    EXPECT_FALSE(fs::exists(path("stats.json")));
    // the probe leaves nothing behind in a folder that does exist
    ASSERT_EQ(run(viterbi(path("result.json"), path("stats.json"))).status, 0);
    for (const auto& entry : fs::directory_iterator(dir_)) EXPECT_NE(entry.path().extension(), ".partial") << entry.path();
}

TEST_F(BenchRun, ARunReplacesTheFilesOfAnEarlierRunAndLeavesNoPartialFile) {
    // the files are renamed over the earlier run's (what the suite runner's --overwrite relies on)
    ASSERT_EQ(run(viterbi(path("result.json"), path("stats.json"), {"--set", "beam_nats=30"})).status, 0);
    ASSERT_EQ(read_json(path("result.json")).at("config").at("beam_nats"), 30.0);
    ASSERT_EQ(run(viterbi(path("result.json"), path("stats.json"), {"--set", "beam_nats=35"})).status, 0);
    EXPECT_EQ(read_json(path("result.json")).at("config").at("beam_nats"), 35.0);
    EXPECT_EQ(read_json(path("stats.json")).at("config").at("beam_nats"), 35.0);
    for (const auto& entry : fs::directory_iterator(dir_)) EXPECT_NE(entry.path().extension(), ".partial") << entry.path();
}

TEST_F(BenchRun, ARunWithoutStatsWritesNoStatisticsAndStatsDetailAloneIsRefused) {
    ASSERT_EQ(run({"--oracle", "--decoder", "viterbi", "--no-timing", "--json", path("result.json")}).status, 0);
    EXPECT_FALSE(fs::exists(path("stats.json")));
    const Outcome o = run({"--oracle", "--decoder", "viterbi", "--stats-detail", "--json", path("never.json")});
    EXPECT_EQ(o.status, 2);
    EXPECT_TRUE(contains(o.err, "--stats-detail needs --stats")) << o.err;
    EXPECT_FALSE(fs::exists(path("never.json")));
}

TEST_F(BenchRun, TheOracleChannelsFollowTheLabelsFrequency) {
    // --oracle --decoder viterbi gives each oracle channel its label's frequency (design Part 1; plan O3): the
    // channel follows it, and the track is reported there (the labels' 300 and -900 Hz, not their bins' centers,
    // 304.6875 and -890.625 Hz). The other decoders' channels stay on their bins.
    const Outcome o = run({"--oracle", "--decoder", "viterbi", "--no-timing", "--json", path("result.json")});
    ASSERT_EQ(o.status, 0) << o.err;
    const json r = read_json(path("result.json"));
    ASSERT_EQ(r.at("tracks").size(), 2u);
    EXPECT_EQ(r.at("tracks").at(0).at("freq_hz"), 300.0);
    EXPECT_EQ(r.at("tracks").at(1).at("freq_hz"), -900.0);
    const Outcome envelope = run({"--oracle", "--decoder", "envelope", "--no-timing", "--json", path("envelope.json")});
    ASSERT_EQ(envelope.status, 0) << envelope.err;
    EXPECT_EQ(read_json(path("envelope.json")).at("tracks").at(0).at("freq_hz"), 304.6875);
}

TEST_F(BenchRun, APerStationLabelOfADriftingQsoIsRefused) {
    // A per-station label of a QSO that drifts (qso_index, drift_hz_per_s): its start_s is the station's first over,
    // not the QSO's start from which the drift counts, so its carrier is not known; the run is refused before it
    // decodes anything, naming the label, and writes no result. The same label without drift runs.
    json labels = read_json(labels_);
    labels["signals"][1]["qso_index"] = 0;
    labels["signals"][1]["drift_hz_per_s"] = 0.5;
    std::ofstream(labels_) << labels.dump(2) << '\n';
    const Outcome o = run({"--oracle", "--decoder", "viterbi", "--no-timing", "--json", path("result.json")});
    EXPECT_EQ(o.status, 2);
    EXPECT_TRUE(contains(o.err, "signal 1")) << o.err;
    EXPECT_TRUE(contains(o.err, "drift")) << o.err;
    EXPECT_FALSE(fs::exists(path("result.json")));
    labels["signals"][1]["drift_hz_per_s"] = 0.0;
    std::ofstream(labels_) << labels.dump(2) << '\n';
    const Outcome still = run({"--oracle", "--decoder", "viterbi", "--no-timing", "--json", path("result.json")});
    EXPECT_EQ(still.status, 0) << still.err;
}

}  // namespace

// ---- The first end-to-end proof: the bench's own path, a recording and its labels to a track's text ----

namespace {

// A recording of one station at 300 Hz above the span's center over DEV2's noise (0.02 FS), D17's 2.5 ms edges, and its
// labels. An empty text is noise alone, with the label a station would have.
void write_station(const fs::path& wav, const fs::path& labels, const std::string& text, double wpm, double s500_db,
                   double duration_s, unsigned seed) {
    const double amplitude = std::sqrt(std::pow(10.0, s500_db / 10.0) * 0.02 * 0.02 * 500.0 / kRate);
    std::vector<std::pair<double, double>> keying;
    if (!text.empty()) keying = kz4ap::test::keying(text, wpm, 0.7);
    const double seconds = keying.empty() ? duration_s : keying.back().second + 1.0;
    write_wav(wav, kz4ap::test::keyed_signal_d17(keying, kRate, seconds, 300.0, amplitude, 0.02, seed),
              static_cast<int>(kRate));
    json signals = json::array();
    signals.push_back({{"text", text}, {"freq_offset_hz", 300.0}, {"wpm", wpm}, {"snr_db", s500_db},
                       {"start_s", 0.7}, {"end_s", keying.empty() ? seconds : keying.back().second},
                       {"keying", "machine"}});
    std::ofstream(labels) << json{{"sample_rate", 48000}, {"duration_s", seconds}, {"signals", signals}}.dump(2) << '\n';
}

// Complex noise of 0.02 FS (DEV2's) shaped across the whole recording by an amplitude gain in frequency (Hz, -24 to +24 kHz).
template <class Gain>
std::vector<kz4ap::Sample> shaped_noise(std::size_t n, unsigned seed, Gain gain) {
    std::mt19937 rng(seed);
    std::normal_distribution<double> draw(0.0, 0.02 / std::sqrt(2.0));
    std::vector<std::complex<double>> x(n);
    for (std::size_t i = 0; i < n; ++i) {
        const double k = static_cast<double>(i) - (i < (n + 1) / 2 ? 0.0 : static_cast<double>(n));
        const double re = draw(rng);
        const double im = draw(rng);
        x[i] = std::complex<double>(re, im) * gain(k * kRate / static_cast<double>(n));
    }
    const pocketfft::stride_t stride{static_cast<std::ptrdiff_t>(sizeof(std::complex<double>))};
    pocketfft::c2c(pocketfft::shape_t{n}, stride, stride, pocketfft::shape_t{0}, pocketfft::BACKWARD, x.data(), x.data(),
                   1.0 / std::sqrt(static_cast<double>(n)));
    std::vector<kz4ap::Sample> out(n);
    for (std::size_t i = 0; i < n; ++i) out[i] = kz4ap::Sample(static_cast<float>(x[i].real()), static_cast<float>(x[i].imag()));
    return out;
}

// The label of one machine-keyed station.
void write_label(const fs::path& labels, const std::string& text, double wpm, double freq_hz, double s500_db, double start_s,
                 double end_s, double duration_s) {
    json signals = json::array();
    signals.push_back({{"text", text}, {"freq_offset_hz", freq_hz}, {"wpm", wpm}, {"snr_db", s500_db},
                       {"start_s", start_s}, {"end_s", end_s}, {"keying", "machine"}});
    std::ofstream(labels) << json{{"sample_rate", 48000}, {"duration_s", duration_s}, {"signals", signals}}.dump(2) << '\n';
}

}  // namespace

TEST_F(BenchRun, DecodesExactlyThroughTheBench) {
    // "PARIS PARIS" and a mixed CQ text at 25 and 40 WPM, +10 and +20 dB S500: exact, through `kz4ap-bench --oracle
    // --decoder viterbi` from the recording to the result file.
    unsigned seed = 100;
    for (const std::string text : {"PARIS PARIS", "CQ CQ DE W1AW W1AW K", "TEST 5NN 73 DE K1ABC"}) {
        for (const double wpm : {25.0, 40.0}) {
            for (const double level : {10.0, 20.0}) {
                write_station(wav_, labels_, text, wpm, level, 0.0, ++seed);
                const Outcome o = run({"--oracle", "--decoder", "viterbi", "--no-timing", "--json", path("result.json")});
                ASSERT_EQ(o.status, 0) << o.err;
                const json r = read_json(path("result.json"));
                ASSERT_EQ(r.at("tracks").size(), 1u);
                EXPECT_EQ(r.at("tracks").at(0).at("text").get<std::string>(), text)
                    << wpm << " WPM, +" << level << " dB S500, seed " << seed;
            }
        }
    }
}

TEST_F(BenchRun, SegmentalDecodesExactlyThroughTheBench) {
    // As DecodesExactlyThroughTheBench, for the fast form: exact, through `kz4ap-bench --oracle --decoder segmental`.
    unsigned seed = 100;
    for (const std::string text : {"PARIS PARIS", "CQ CQ DE W1AW W1AW K", "TEST 5NN 73 DE K1ABC"}) {
        for (const double wpm : {25.0, 40.0}) {
            for (const double level : {10.0, 20.0}) {
                write_station(wav_, labels_, text, wpm, level, 0.0, ++seed);
                const Outcome o = run({"--oracle", "--decoder", "segmental", "--no-timing", "--json", path("result.json")});
                ASSERT_EQ(o.status, 0) << o.err;
                const json r = read_json(path("result.json"));
                ASSERT_EQ(r.at("tracks").size(), 1u);
                EXPECT_EQ(r.at("tracks").at(0).at("text").get<std::string>(), text)
                    << wpm << " WPM, +" << level << " dB S500, seed " << seed;
            }
        }
    }
}

TEST_F(BenchRun, SegmentalReadsNothingOnNoiseAndRefusesAnUnknownSetting) {
    write_station(wav_, labels_, "", 25.0, 20.0, 20.0, 7);
    const Outcome noise = run({"--oracle", "--decoder", "segmental", "--no-timing", "--json", path("result.json")});
    ASSERT_EQ(noise.status, 0) << noise.err;
    EXPECT_EQ(read_json(path("result.json")).at("tracks").at(0).at("text").get<std::string>(), "");
    const Outcome refused = run({"--oracle", "--decoder", "segmental", "--no-timing", "--set", "no_such_field=1"});
    EXPECT_NE(refused.status, 0);
}

TEST_F(BenchRun, SegmentalHonorsTheHissSettings) {
    // Once silently ignored: the hiss read the engine's other configuration.
    write_station(wav_, labels_, "CQ CQ", 25.0, 20.0, 0.0, 8);
    const Outcome refused = run({"--oracle", "--decoder", "segmental", "--no-timing", "--set", "hiss_percentile=200"});
    EXPECT_NE(refused.status, 0);
    const Outcome wrong = run({"--oracle", "--decoder", "segmental", "--no-timing", "--set", "hiss_average_s=-1"});
    EXPECT_NE(wrong.status, 0);
    const Outcome taken = run({"--oracle", "--decoder", "segmental", "--no-timing", "--set", "hiss_percentile=20", "--json", path("result.json")});
    ASSERT_EQ(taken.status, 0) << taken.err;
    EXPECT_EQ(read_json(path("result.json")).at("tracks").at(0).at("text").get<std::string>(), "CQ CQ");
}

TEST_F(BenchRun, SegmentalReadsFarnsworthSpacingThroughTheBench) {
    // W1AW's code practice at 18 WPM characters and 5 WPM overall, and 25 at 10 (ARRL: gap unit (60 / overall - 37.2 / character) / 19 s).
    const std::string text = "CQ CQ DE W1AW W1AW K";
    const double start_s = 0.7;
    for (const auto& [character, overall] : {std::pair<double, double>{18.0, 5.0}, {25.0, 10.0}}) {
        const double dot_s = 1.2 / character;
        const double gap_unit_s = (60.0 / overall - 37.2 / character) / 19.0;
        std::vector<std::pair<double, double>> keying;
        double t = start_s;
        bool first_word = true, new_word = true;
        for (const char c : text) {
            if (c == ' ') {
                new_word = true;
                continue;
            }
            if (!keying.empty()) t += (new_word ? 7.0 : 3.0) * gap_unit_s;
            (void)first_word;
            new_word = false;
            bool first_mark = true;
            for (const char mark : kz4ap::morse::encode(std::string(1, c))) {
                if (!first_mark) t += dot_s;
                first_mark = false;
                const double length = (mark == '-' ? 3.0 : 1.0) * dot_s;
                keying.emplace_back(t, t + length);
                t += length;
            }
        }
        const double seconds = t + 1.0;
        const double level_db = 12.0;
        const double amplitude = std::sqrt(std::pow(10.0, level_db / 10.0) * 0.02 * 0.02 * 500.0 / kRate);
        write_wav(wav_, kz4ap::test::keyed_signal_d17(keying, kRate, seconds, 300.0, amplitude, 0.02, 4346), static_cast<int>(kRate));
        write_label(labels_, text, character, 300.0, level_db, start_s, t, seconds);
        const Outcome o = run({"--oracle", "--decoder", "segmental", "--no-timing", "--json", path("result.json")});
        ASSERT_EQ(o.status, 0) << o.err;
        EXPECT_EQ(read_json(path("result.json")).at("tracks").at(0).at("text").get<std::string>(), text)
            << character << " WPM at " << overall << " WPM overall";
    }
}

TEST_F(BenchRun, SegmentalReadsASlowStationPastACloseNeighbor) {
    // The review's F2: wanted +15 dB S500; a keyed neighbor 40 Hz above at -10 dB and 0 dB re wanted, at another speed. The widest
    // filter alone cannot; the narrower filters can.
    const std::string text = "CQ CQ DE W1AW W1AW K TEST 5NN 73 DE K1ABC";
    const std::string other = "QRZ DE N3VO N3VO PSE K UR 599 BK TU 73 EE EE QRZ DE N3VO K";
    const double wpm = 15.0, start_s = 0.7;
    const std::vector<std::pair<double, double>> keying = kz4ap::test::keying(text, wpm, start_s);
    const double seconds = keying.back().second + 1.0;
    const double level_db = 15.0;
    for (const double relative_db : {-10.0, 0.0}) {
        const double amplitude = std::sqrt(std::pow(10.0, level_db / 10.0) * 0.02 * 0.02 * 500.0 / kRate);
        const double neighbor = amplitude * std::pow(10.0, relative_db / 20.0);
        std::vector<kz4ap::Sample> x = kz4ap::test::keyed_signal_d17(keying, kRate, seconds, 300.0, amplitude, 0.02, 4347);
        const std::vector<kz4ap::Sample> y =
            kz4ap::test::keyed_signal_d17(kz4ap::test::keying(other, 22.0, start_s), kRate, seconds, 340.0, neighbor, 0.0, 4348);
        for (std::size_t i = 0; i < x.size(); ++i) x[i] += y[i];
        write_wav(wav_, x, static_cast<int>(kRate));
        write_label(labels_, text, wpm, 300.0, level_db, start_s, keying.back().second, seconds);
        const auto cer = [&](std::vector<std::string> options) {
            options.insert(options.end(), {"--oracle", "--decoder", "segmental", "--no-timing", "--json", path("result.json")});
            const Outcome o = run(options);
            EXPECT_EQ(o.status, 0) << o.err;
            return read_json(path("result.json")).at("score").at("signals").at(0).at("cer").get<double>();
        };
        const double scales = cer({});
        const double widest = cer({"--set", "filter_scales=1"});
        std::cout << "measured: neighbor 40 Hz, " << relative_db << " dB re wanted: CER " << scales << ", widest filter alone " << widest << "\n";
        // At 0 dB re wanted the narrower filter reads it and the choice is marginal on a text this short: 0.07 on one noise, 0.80
        // on another, within 120 nats of the lead (config.hpp). It must not read worse than the widest filter.
        if (relative_db < -5.0) EXPECT_LE(scales, 0.1) << relative_db << " dB re wanted";
        EXPECT_LE(scales, widest + 1e-9) << relative_db << " dB re wanted";
        EXPECT_GE(widest, 0.3) << relative_db << " dB re wanted";
    }
}

TEST_F(BenchRun, SegmentalReadsPastAStrongNeighborBehindTheNarrowerFiltersSidelobes) {
    // Wanted +15 dB S500; a keyed neighbor 100 Hz above at +12 dB re wanted. Outside the main lobe, so what gets in is the m = 3 pulse's sidelobes:
    // -23.0 dB re 0 Hz with edges of 4.235 ms, -28.9 with E = T/4 = 9 ms (derived).
    const std::string text = "CQ CQ DE W1AW W1AW K TEST 5NN 73 DE K1ABC CQ CQ DE W1AW W1AW K TEST 5NN 73 DE K1ABC";
    const std::string other = "QRZ DE N3VO N3VO PSE K UR 599 BK TU 73 EE EE QRZ DE N3VO K QRZ DE N3VO N3VO PSE K UR 599 BK TU 73 EE EE QRZ DE N3VO K";
    const double wpm = 20.0, start_s = 0.7, level_db = 15.0, relative_db = 12.0;
    const std::vector<std::pair<double, double>> keying = kz4ap::test::keying(text, wpm, start_s);
    const double seconds = keying.back().second + 1.0;
    const double amplitude = std::sqrt(std::pow(10.0, level_db / 10.0) * 0.02 * 0.02 * 500.0 / kRate);
    const double neighbor = amplitude * std::pow(10.0, relative_db / 20.0);
    std::vector<kz4ap::Sample> x = kz4ap::test::keyed_signal_d17(keying, kRate, seconds, 300.0, amplitude, 0.02, 4351);
    const std::vector<kz4ap::Sample> y =
        kz4ap::test::keyed_signal_d17(kz4ap::test::keying(other, 25.0, start_s), kRate, seconds, 400.0, neighbor, 0.0, 4352);
    for (std::size_t i = 0; i < x.size(); ++i) x[i] += y[i];
    write_wav(wav_, x, static_cast<int>(kRate));
    write_label(labels_, text, wpm, 300.0, level_db, start_s, keying.back().second, seconds);
    const auto cer = [&](std::vector<std::string> options) {
        options.insert(options.end(), {"--oracle", "--decoder", "segmental", "--no-timing", "--json", path("result.json")});
        const Outcome o = run(options);
        EXPECT_EQ(o.status, 0) << o.err;
        return read_json(path("result.json")).at("score").at("signals").at(0).at("cer").get<double>();
    };
    const double tapered = cer({});
    const double untapered = cer({"--set", "front_taper_ratio=0"});
    std::cout << "measured: neighbor 100 Hz, +12 dB re wanted: CER " << tapered << " with E = T/4, " << untapered << " with E = 4.235 ms\n";
    EXPECT_LE(tapered, 0.1);
    EXPECT_GE(untapered, 0.5);  // the control: at 4.235 ms the narrower stream does not earn the lead (measured 0.807)
}

TEST_F(BenchRun, SegmentalReadsALateStationPastACloseNeighborThroughTheDetector) {
    // Review 4, finding 2, as the app meets it: on the detector's path, a pair that starts 20 s into the stream, wanted +15 dB S500 at
    // 15 WPM and a keyed neighbor 40 Hz above, -10 dB re wanted. A channel that opens after 10 s of stream must keep its narrower filter
    // to 10 s of its own age, as one that opens at the stream's start does. (This pair reads either way; the test that fails without the fix is
    // FilterScale.TheChoiceRunsOnTheChannelsAgeNotTheStreams. This one is the whole path, which no test read before.)
    const std::string text = "CQ CQ DE W1AW W1AW K TEST 5NN 73 DE K1ABC";
    const std::string other = "QRZ DE N3VO N3VO PSE K UR 599 BK TU 73 EE EE QRZ DE N3VO K";
    const double wpm = 15.0, start_s = 20.0, level_db = 15.0;
    const std::vector<std::pair<double, double>> keying = kz4ap::test::keying(text, wpm, start_s);
    const double seconds = keying.back().second + 1.0;
    const double amplitude = std::sqrt(std::pow(10.0, level_db / 10.0) * 0.02 * 0.02 * 500.0 / kRate);
    std::vector<kz4ap::Sample> x = kz4ap::test::keyed_signal_d17(keying, kRate, seconds, 300.0, amplitude, 0.02, 4347);
    const std::vector<kz4ap::Sample> y = kz4ap::test::keyed_signal_d17(kz4ap::test::keying(other, 22.0, start_s), kRate, seconds, 340.0,
                                                                       amplitude * std::pow(10.0, -10.0 / 20.0), 0.0, 4348);
    for (std::size_t i = 0; i < x.size(); ++i) x[i] += y[i];
    write_wav(wav_, x, static_cast<int>(kRate));
    write_label(labels_, text, wpm, 300.0, level_db, start_s, keying.back().second, seconds);
    const auto cer = [&](std::vector<std::string> options) {
        options.insert(options.end(), {"--decoder", "segmental", "--no-timing", "--json", path("result.json")});
        const Outcome o = run(options);
        EXPECT_EQ(o.status, 0) << o.err;
        return read_json(path("result.json")).at("score").at("signals").at(0).at("cer").get<double>();
    };
    const double scales = cer({});
    const double widest = cer({"--set", "filter_scales=1"});
    std::cout << "measured: late, neighbor 40 Hz, -10 dB re wanted, detector's path: CER " << scales << ", widest filter alone " << widest << "\n";
    EXPECT_LE(scales, 0.1);
    EXPECT_GE(widest, 0.3);
}

TEST_F(BenchRun, SegmentalSurvivesABurstAtAndAboveOneHundredAndTwoDecibels) {
    // Review F6: 1 s of carrier on an FFT bin (281.25 Hz, where the hiss stays below the limit), +102 and +108 dB S500,
    // then a +20 dB S500 station on the same frequency. The recording is scaled to fit 16 bits. Before the fix the first
    // read CER 1.000; the review reads 0.049 and 0.098 with it.
    const std::string text = "CQ CQ DE W1AW W1AW K CQ CQ DE W1AW W1AW K";
    const double wpm = 25.0, freq_hz = 281.25, start_s = 5.0, level_db = 20.0;
    const std::vector<std::pair<double, double>> keying = kz4ap::test::keying(text, wpm, start_s);
    const double seconds = keying.back().second + 1.0;
    const auto amplitude_at = [](double s500_db) { return std::sqrt(std::pow(10.0, s500_db / 10.0) * 0.02 * 0.02 * 500.0 / kRate); };
    for (const double burst_db : {102.0, 108.0}) {
        std::vector<kz4ap::Sample> x = kz4ap::test::keyed_signal_d17(keying, kRate, seconds, freq_hz, amplitude_at(level_db), 0.02, 4344);
        double peak = 0.0;
        for (std::size_t i = 0; i < x.size(); ++i) {
            const double t = static_cast<double>(i) / kRate;
            const double edge = std::clamp(std::min(t - 2.0, 3.0 - t) / 0.02, 0.0, 1.0);  // raised-cosine edges of 20 ms
            const double a = amplitude_at(burst_db) * (0.5 - 0.5 * std::cos(std::numbers::pi * edge));
            const std::complex<double> burst = std::polar(a, 2.0 * std::numbers::pi * freq_hz * t);
            x[i] += kz4ap::Sample(static_cast<float>(burst.real()), static_cast<float>(burst.imag()));
            peak = std::max({peak, std::fabs(static_cast<double>(x[i].real())), std::fabs(static_cast<double>(x[i].imag()))});
        }
        for (kz4ap::Sample& v : x) v *= static_cast<float>(0.95 / peak);
        write_wav(wav_, x, static_cast<int>(kRate));
        write_label(labels_, text, wpm, freq_hz, level_db, start_s, keying.back().second, seconds);
        const Outcome o = run({"--oracle", "--decoder", "segmental", "--no-timing", "--json", path("result.json")});
        ASSERT_EQ(o.status, 0) << o.err;
        const double cer = read_json(path("result.json")).at("score").at("signals").at(0).at("cer").get<double>();
        std::cout << "measured: burst at +" << burst_db << " dB S500: CER " << cer << '\n';
        EXPECT_LE(cer, 0.2) << "burst at +" << burst_db << " dB S500";
    }
}

TEST_F(BenchRun, SegmentalReadsAStationAboveItsGridWithoutFalseLetters) {
    // Review F8: above +80 dB S500 w z^2 passes 1e9 nats, and when x was held whole in a float its rounding, tens of nats per
    // sample, stood above the beam of 15 nats: +90 dB S500 read 0.34 and +100 dB S500 1.36 (at f = 0.02, 0.07 and 0.27).
    // The recording is scaled to fit 16 bits.
    const std::string text = "CQ CQ DE W1AW W1AW K TEST 5NN 73 DE K1ABC";
    const double wpm = 25.0, freq_hz = 300.0, start_s = 0.7;
    const std::vector<std::pair<double, double>> keying = kz4ap::test::keying(text, wpm, start_s);
    const double seconds = keying.back().second + 1.0;
    for (const double level_db : {84.0, 90.0, 100.0, 110.0}) {
        const double amplitude = std::sqrt(std::pow(10.0, level_db / 10.0) * 0.02 * 0.02 * 500.0 / kRate);
        std::vector<kz4ap::Sample> x = kz4ap::test::keyed_signal_d17(keying, kRate, seconds, freq_hz, amplitude, 0.02, 4345);
        double peak = 0.0;
        for (const kz4ap::Sample& v : x)
            peak = std::max({peak, std::fabs(static_cast<double>(v.real())), std::fabs(static_cast<double>(v.imag()))});
        for (kz4ap::Sample& v : x) v *= static_cast<float>(0.95 / peak);
        write_wav(wav_, x, static_cast<int>(kRate));
        write_label(labels_, text, wpm, freq_hz, level_db, start_s, keying.back().second, seconds);
        const Outcome o = run({"--oracle", "--decoder", "segmental", "--no-timing", "--json", path("result.json")});
        ASSERT_EQ(o.status, 0) << o.err;
        EXPECT_EQ(read_json(path("result.json")).at("tracks").at(0).at("text").get<std::string>(), text)
            << "+" << level_db << " dB S500";
    }
}

// A floor the hiss cannot read (review F1), read with and without the channel's own floor. Pooled over two stations per cell.
class BenchRunFloor : public BenchRun {
protected:
    struct Read {
        double with_floor;
        double without;
    };
    // gain(f_hz) of the noise; level re the floor of 0.02 FS, 25 WPM, machine keying.
    template <class Gain>
    Read read_on(double s500_db, double freq_hz, Gain gain) {
        const std::string text = "CQ CQ DE W1AW W1AW K TEST 5NN 73 DE K1ABC";
        const double wpm = 25.0, start_s = 1.5;
        const std::vector<std::pair<double, double>> keying = kz4ap::test::keying(text, wpm, start_s);
        const double seconds = keying.back().second + 1.0;
        const double amplitude = std::sqrt(std::pow(10.0, s500_db / 10.0) * 0.02 * 0.02 * 500.0 / kRate);
        double with_edits = 0.0, without_edits = 0.0, symbols = 0.0;
        for (const unsigned seed : {51u, 52u}) {
            std::vector<kz4ap::Sample> x = kz4ap::test::keyed_signal_d17(keying, kRate, seconds, freq_hz, amplitude, 0.0, seed);
            const std::vector<kz4ap::Sample> noise = shaped_noise(x.size(), seed, gain);
            for (std::size_t i = 0; i < x.size(); ++i) x[i] += noise[i];
            write_wav(wav_, x, static_cast<int>(kRate));
            write_label(labels_, text, wpm, freq_hz, s500_db, start_s, keying.back().second, seconds);
            const auto score = [&](std::vector<std::string> options) {
                options.insert(options.end(), {"--oracle", "--decoder", "segmental", "--no-timing", "--json", path("result.json")});
                const Outcome o = run(options);
                EXPECT_EQ(o.status, 0) << o.err;
                return read_json(path("result.json")).at("score").at("signals").at(0);
            };
            const json with = score({});
            // The leak floor too: it reads the noise beside the channel, where the hiss does not, and alone reads these at CER 0.05 to 0.41.
            const json without = score({"--set", "channel_floor_window_s=0", "--set", "leak_hold_s=0"});
            with_edits += with.at("edits").get<double>();
            without_edits += without.at("edits").get<double>();
            symbols += with.at("symbols").get<double>();
        }
        return {with_edits / symbols, without_edits / symbols};
    }
};

TEST_F(BenchRunFloor, ReadsBehindAReceiversPassband) {
    // Noise only within 500 or 2 400 Hz around 0 Hz, 50 dB lower outside, one station at +20 dB S500 inside: the hiss falls
    // in the stopband and reads about 50 dB low. The review: 2.32 and 2.01 without the floor, 0.127 and 0.146 with it; review 7, E = T/4 at
    // m = 3: 0.829 without at 2 400 Hz, 0.049 and 0.061 with, so the control asks for a clear fault and not for a CER over 1.
    for (const double width_hz : {500.0, 2400.0}) {
        const double outside = std::pow(10.0, -50.0 / 20.0);
        const Read r = read_on(20.0, 0.2 * width_hz, [=](double f_hz) { return std::fabs(f_hz) < width_hz / 2.0 ? 1.0 : outside; });
        std::cout << "measured: passband " << width_hz << " Hz: CER " << r.with_floor << " with the floor, " << r.without
                  << " without\n";
        EXPECT_LE(r.with_floor, 0.25) << width_hz << " Hz";
        EXPECT_GE(r.without, 0.5) << width_hz << " Hz";
    }
}

TEST_F(BenchRunFloor, ReadsUnderANoiseRaisedAroundTheChannel) {
    // Noise raised +6 dB re the white floor over 600 Hz around the channel: the hiss reads the white floor, 6 dB low. The
    // review: 0.689 and 0.647 without the floor, 0.057 and 0.050 with it, at +20 and +40 dB S500.
    for (const double level_db : {20.0, 40.0}) {
        const double raised = std::pow(10.0, 6.0 / 20.0), freq_hz = 300.0;
        const Read r = read_on(level_db, freq_hz, [=](double f_hz) { return std::fabs(f_hz - freq_hz) < 300.0 ? raised : 1.0; });
        std::cout << "measured: raised +6 dB, +" << level_db << " dB S500: CER " << r.with_floor << " with the floor, " << r.without
                  << " without\n";
        EXPECT_LE(r.with_floor, 0.15) << level_db << " dB S500";
        EXPECT_GE(r.without, 0.3) << level_db << " dB S500";
    }
}

TEST_F(BenchRun, CpuPerChannelSecond) {
    // Label measurement (skipped otherwise): the bench's CPU per channel-second on one channel at the default ceiling, on
    // 60 s of noise and on a station sending for about 60 s, +20 dB S500, 25 WPM.
    const char* flag = std::getenv("KZ4AP_MEASUREMENT");
    if (flag == nullptr || std::string(flag) != "1") GTEST_SKIP() << "runs under the measurement label";
    std::string text;
    for (int i = 0; i < 6; ++i) text += "CQ CQ DE W1AW W1AW K ";
    text.pop_back();
    const auto cpu = [&](const std::string& what, const std::string& sent) {
        write_station(wav_, labels_, sent, 25.0, 20.0, 60.0, 7);
        const Outcome o = run({"--oracle", "--decoder", "viterbi", "--json", path("result.json")});
        ASSERT_EQ(o.status, 0) << o.err;
        const json r = read_json(path("result.json"));
        std::cout << "measured: " << what << ": " << r.at("channel_seconds").get<double>() << " channel-seconds, "
                  << r.at("timing").at("cpu_ms_per_channel_s").get<double>() << " ms per channel-second of CPU, decoder "
                  << r.at("timing").at("decoder_ms_per_channel_s").get<double>() << " ms; text \""
                  << r.at("tracks").at(0).at("text").get<std::string>().substr(0, 60) << "\"" << std::endl;
    };
    cpu("noise", "");
    cpu("signal", text);
}
