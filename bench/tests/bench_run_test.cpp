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

#include "test_signals.hpp"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <tuple>
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
        const std::int64_t pieces = c.at("pieces").get<std::int64_t>();
        EXPECT_GT(pieces, 1000);
        std::int64_t in_histogram = 0, in_fates = 0;
        for (const json& n : c.at("live_histogram")) in_histogram += n.get<std::int64_t>();
        for (const json& a : c.at("fates"))
            for (const json& b : a)
                for (const json& d : b)
                    for (const json& n : d) in_fates += n.get<std::int64_t>();
        EXPECT_EQ(in_histogram, pieces);
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
