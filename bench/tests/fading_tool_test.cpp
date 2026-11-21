// kz4ap-fading run as the program it is (review of text-level-1): what it refuses before it measures, each with exit status 1
// and one line on stderr, and one measurement that runs. Each run is a child process (std::system, the program's own path from
// CMake), as bench_run_test.cpp runs kz4ap-bench.
#include "temp_dir.hpp"

#include "kz4ap/types.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <numbers>
#include <random>
#include <regex>
#include <string>
#include <vector>

#ifndef _WIN32
#include <sys/wait.h>
#endif

namespace {

namespace fs = std::filesystem;

constexpr int kRate = 48000;

void put_u16(std::string& b, std::uint16_t v) {
    b.push_back(static_cast<char>(v & 0xFF));
    b.push_back(static_cast<char>(v >> 8));
}

void put_u32(std::string& b, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) b.push_back(static_cast<char>((v >> (8 * i)) & 0xFF));
}

// A 16-bit stereo WAV file: left I, right Q.
void write_wav(const fs::path& path, const std::vector<kz4ap::Sample>& samples) {
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
    put_u32(fmt, static_cast<std::uint32_t>(kRate));
    put_u32(fmt, static_cast<std::uint32_t>(kRate * 4));
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

// Complex white noise of rms `noise` FS a component, and, if `keyed`, a carrier of 0.1 FS 300 Hz above the center keyed 60 ms on,
// 60 ms off, its gain cos(2 pi 4 Hz t) (two paths 4 Hz either side: it fades through nulls and decorrelates within marks).
std::vector<kz4ap::Sample> signal(double seconds, double noise, bool keyed) {
    std::mt19937 rng(17);
    std::normal_distribution<double> g(0.0, noise);
    std::vector<kz4ap::Sample> x(static_cast<std::size_t>(seconds * kRate));
    for (std::size_t n = 0; n < x.size(); ++n) {
        const double t = static_cast<double>(n) / kRate;
        std::complex<double> s(g(rng), g(rng));
        if (keyed && std::fmod(t, 0.12) < 0.06)
            s += 0.1 * std::cos(2.0 * std::numbers::pi * 4.0 * t) * std::polar(1.0, 2.0 * std::numbers::pi * 300.0 * t);
        x[n] = kz4ap::Sample(static_cast<float>(s.real()), static_cast<float>(s.imag()));
    }
    return x;
}

std::string slurp(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

std::string quoted(const std::string& s) { return "\"" + s + "\""; }

struct Outcome {
    int status = -1;
    std::string out;
    std::string err;
};

class FadingTool : public ::testing::Test {
protected:
    fs::path dir_;

    void SetUp() override {
        const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
        dir_ = kz4ap::test::temp_path(std::string("kz4ap_fading_") + info->name());
        fs::remove_all(dir_);
        fs::create_directories(dir_);
    }

    void TearDown() override {
        std::error_code ignored;
        fs::remove_all(dir_, ignored);
    }

    Outcome run(const std::vector<std::string>& arguments) {
        std::string command = quoted(fs::path(KZ4AP_FADING_EXE).make_preferred().string());
        for (const std::string& a : arguments) command += " " + quoted(a);
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

    // Refused: exit status 1, nothing on stdout, one line on stderr holding `message`.
    void expect_refused(const std::vector<std::string>& arguments, const std::string& message) {
        const Outcome o = run(arguments);
        std::string shown;
        for (const std::string& a : arguments) shown += " " + a;
        EXPECT_EQ(o.status, 1) << shown << "\n" << o.err;
        EXPECT_EQ(o.out, "") << shown;
        EXPECT_EQ(std::count(o.err.begin(), o.err.end(), '\n'), 1) << shown << "\n" << o.err;
        EXPECT_NE(o.err.find(message), std::string::npos) << shown << "\n" << o.err;
    }
};

TEST_F(FadingTool, RefusesAMissingValueABadNumberAndASpanThatIsNotPositiveBeforeReadingTheFile) {
    const std::string missing = (dir_ / "no-such-recording.wav").string();
    expect_refused({missing, "--start", "0", "--seconds", "1", "--offset-hz"}, "--offset-hz needs a value");
    expect_refused({missing, "--seconds"}, "--seconds needs a value");
    expect_refused({missing, "--offset-hz", "300", "--seconds", "0"}, "--seconds must be above 0 s");
    expect_refused({missing, "--offset-hz", "300", "--seconds", "-2"}, "--seconds must be above 0 s");
    expect_refused({missing, "--offset-hz", "300"}, "--seconds must be above 0 s");  // no span given
    expect_refused({missing, "--seconds", "1", "--start", "-1"}, "--start must not be below 0 s");
    expect_refused({missing, "--seconds", "1x"}, "--seconds needs a finite number");
    expect_refused({missing, "--seconds", "nan"}, "--seconds needs a finite number");
    expect_refused({missing, "--width", "3"}, "unknown option --width");
}

TEST_F(FadingTool, RefusesASpanTheFileDoesNotHoldAndASpanWithNoKeyDown) {
    const fs::path noise = dir_ / "noise.wav";
    write_wav(noise, signal(1.0, 0.01, false));
    expect_refused({noise.string(), "--offset-hz", "300", "--start", "5", "--seconds", "1"}, "no samples read at 5");
    // 1 ms of input: fewer than one noise block after the low-pass
    expect_refused({noise.string(), "--offset-hz", "300", "--start", "0", "--seconds", "0.001"}, "fewer than one noise block");
    expect_refused({noise.string(), "--offset-hz", "300", "--start", "0", "--seconds", "1"}, "no key-down run");
}

TEST_F(FadingTool, MeasuresAKeyedFadingCarrier) {
    const fs::path wav = dir_ / "keyed.wav";
    write_wav(wav, signal(4.0, 0.001, true));
    const Outcome o = run({wav.string(), "--offset-hz", "300", "--start", "0", "--seconds", "4"});
    ASSERT_EQ(o.status, 0) << o.err;
    EXPECT_EQ(o.err, "");
    std::smatch m;
    ASSERT_TRUE(std::regex_search(o.out, m, std::regex(R"(f_D = 2 sigma_D ([0-9.]+) Hz)"))) << o.out;
    const double f_d = std::stod(m[1].str());
    EXPECT_TRUE(std::isfinite(f_d) && f_d > 0.0) << o.out;
}

}  // namespace
