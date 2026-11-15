// The live source's parts that need no radio: the ring, the recorder and what it writes, the radio's settings and rate plan,
// and a recording's stream from a chosen place. The API's sequence itself is tested against a stand-in library
// (sdrplay_source_test.cpp).
#include "kz4ap/file_iq_source.hpp"
#include "kz4ap/iq_recorder.hpp"
#include "kz4ap/iq_ring.hpp"
#include "kz4ap/sdrplay_source.hpp"
#include "kz4ap/signal_detector.hpp"
#include "kz4ap/wav_reader.hpp"

#include "temp_dir.hpp"

#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <optional>
#include <numeric>
#include <span>
#include <stdexcept>
#include <thread>
#include <vector>

using namespace kz4ap;
using namespace std::chrono_literals;

namespace {

std::filesystem::path temp_file(const char* name) { return kz4ap::test::temp_path(name); }

TEST(SpscRing, KeepsOrderAcrossTheWrapAndRefusesWhatDoesNotFit) {
    SpscRing<int> ring(8);
    EXPECT_EQ(ring.capacity(), 8u);
    std::vector<int> in(5);
    std::iota(in.begin(), in.end(), 0);
    EXPECT_EQ(ring.push(in), 5u);
    std::vector<int> out(3);
    EXPECT_EQ(ring.pop(out), 3u);
    EXPECT_EQ(out, (std::vector<int>{0, 1, 2}));
    std::vector<int> more(10);
    std::iota(more.begin(), more.end(), 5);
    EXPECT_EQ(ring.push(more), 6u);  // 2 left in the ring, 8 in all
    std::vector<int> rest(20);
    EXPECT_EQ(ring.pop(rest), 8u);
    for (int i = 0; i < 8; ++i) EXPECT_EQ(rest[static_cast<std::size_t>(i)], i + 3);
    EXPECT_EQ(ring.pop(rest), 0u);
}

TEST(SpscRing, CarriesEveryItemBetweenTwoThreads) {
    SpscRing<std::uint32_t> ring(1024);
    constexpr std::uint32_t kCount = 2000000;
    std::thread producer([&] {
        std::array<std::uint32_t, 137> block{};
        for (std::uint32_t next = 0; next < kCount;) {
            const std::size_t n = std::min<std::size_t>(block.size(), kCount - next);
            for (std::size_t i = 0; i < n; ++i) block[i] = next + static_cast<std::uint32_t>(i);
            std::size_t done = 0;
            while (done < n) done += ring.push(std::span<const std::uint32_t>(block.data() + done, n - done));
            next += static_cast<std::uint32_t>(n);
        }
    });
    std::vector<std::uint32_t> got(509);
    std::uint32_t expected = 0;
    while (expected < kCount) {
        const std::size_t n = ring.pop(got);
        for (std::size_t i = 0; i < n; ++i) ASSERT_EQ(got[i], expected++);
    }
    producer.join();
}

TEST(IqRecorder, WritesTheProjectsWavFormSampleForSample) {
    const auto path = temp_file("kz4ap_recorder_round_trip.wav");
    constexpr int kRate = 192000;
    std::vector<short> xi(5000), xq(5000);
    for (std::size_t i = 0; i < xi.size(); ++i) {
        xi[i] = static_cast<short>(static_cast<int>(i) - 2500);
        xq[i] = static_cast<short>(-3 * static_cast<int>(i));
    }
    {
        IqRecorder recorder(kRate);
        recorder.push(xi.data(), xq.data(), 100);  // nothing is recording yet
        recorder.start(path);
        EXPECT_TRUE(recorder.status().active);
        EXPECT_THROW(recorder.start(path), std::runtime_error);
        for (std::size_t i = 0; i < xi.size(); i += 500) recorder.push(xi.data() + i, xq.data() + i, 500);
        recorder.stop();
        const RecordingStatus status = recorder.status();
        EXPECT_FALSE(status.active);
        EXPECT_EQ(status.samples, xi.size());
        EXPECT_EQ(status.dropped, 0u);
        EXPECT_TRUE(status.error.empty());
        recorder.push(xi.data(), xq.data(), 100);  // nothing is recording any more
    }
    EXPECT_EQ(std::filesystem::file_size(path), 44u + 4u * xi.size());
    std::optional<WavIqReader> opened(std::in_place, path);
    WavIqReader& reader = *opened;
    EXPECT_EQ(reader.sample_rate(), kRate);
    ASSERT_EQ(reader.total_samples(), xi.size());
    std::vector<Sample> samples(xi.size());
    ASSERT_EQ(reader.read(samples), xi.size());
    for (std::size_t i = 0; i < xi.size(); ++i) {
        ASSERT_EQ(samples[i].real(), xi[i] / 32768.0f) << i;
        ASSERT_EQ(samples[i].imag(), xq[i] / 32768.0f) << i;
    }
    opened.reset();
    std::filesystem::remove(path);
}

TEST(IqRecorder, AFileCanBeReadWhileItIsWrittenAndASecondFollows) {
    const auto path = temp_file("kz4ap_recorder_live.wav");
    const auto second = temp_file("kz4ap_recorder_second.wav");
    constexpr int kRate = 1000;  // the header's kHeaderEveryS = 1 s is then 1000 samples
    std::vector<short> xi(2500, 7), xq(2500, -7);
    IqRecorder recorder(kRate);
    recorder.start(path);
    recorder.push(xi.data(), xq.data(), xi.size());
    const auto limit = std::chrono::steady_clock::now() + 10s;
    while (recorder.status().samples < xi.size() && std::chrono::steady_clock::now() < limit) std::this_thread::sleep_for(5ms);
    EXPECT_EQ(recorder.status().samples, xi.size());
    // A crash now would leave a file whose header counts all but the last second at most.
    std::this_thread::sleep_for(50ms);
    EXPECT_GE(WavIqReader(path).total_samples(), 1000u);
    recorder.stop();
    recorder.start(second);
    recorder.push(xi.data(), xq.data(), 10);
    recorder.stop();
    EXPECT_EQ(recorder.status().samples, 10u);
    EXPECT_EQ(WavIqReader(second).total_samples(), 10u);
    std::filesystem::remove(path);
    std::filesystem::remove(second);
}

TEST(IqRecorder, SaysWhenItCannotCreateTheFile) {
    IqRecorder recorder(48000);
    EXPECT_THROW(recorder.start(temp_file("no_such_folder_kz4ap") / "x.wav"), std::runtime_error);
    EXPECT_FALSE(recorder.status().active);
}

TEST(IqRecorder, CountsWhatItsRingCouldNotHold) {
    const auto path = temp_file("kz4ap_recorder_lost.wav");
    IqRecorder recorder(1000);  // a ring of 10 s, rounded up to 16 384 samples
    recorder.start(path);
    std::vector<short> xi(4000000, 1), xq(4000000, 1);
    recorder.push(xi.data(), xq.data(), xi.size());
    recorder.stop();
    const RecordingStatus status = recorder.status();
    EXPECT_GT(status.dropped, 0u);
    EXPECT_EQ(status.samples + status.dropped, xi.size());
    std::filesystem::remove(path);
}

TEST(SdrplayRatePlan, GivesEachRateItsExactFactors) {
    for (const int rate : sdrplay_sample_rates()) {
        const auto plan = sdrplay_rate_plan(rate);
        ASSERT_TRUE(plan.has_value()) << rate;
        EXPECT_DOUBLE_EQ(plan->adc_hz / plan->decimation, rate);
        EXPECT_GE(plan->adc_hz, 2.0e6);  // the API's least
        const int d = plan->decimation;
        EXPECT_TRUE(d == 2 || d == 4 || d == 8 || d == 16 || d == 32) << d;
        // The tuner's filter passes the whole span, and no wider filter than the next would do.
        EXPECT_GE(plan->bandwidth_khz * 1000, rate);
    }
    EXPECT_FALSE(sdrplay_rate_plan(48000).has_value());
    EXPECT_FALSE(sdrplay_rate_plan(0).has_value());
}

TEST(SdrplaySettings, RefusesWhatIsOutOfRangeSayingSo) {
    EXPECT_NO_THROW(check_sdrplay_settings(SdrplaySettings{}));
    SdrplaySettings s;
    s.center_hz = 500.0;
    EXPECT_THROW(check_sdrplay_settings(s), std::invalid_argument);
    s = {};
    s.center_hz = 3.0e9;
    EXPECT_THROW(check_sdrplay_settings(s), std::invalid_argument);
    s = {};
    s.sample_rate = 48000;
    EXPECT_THROW(check_sdrplay_settings(s), std::invalid_argument);
    s = {};
    s.if_gain_reduction_db = 19;
    EXPECT_THROW(check_sdrplay_settings(s), std::invalid_argument);
    s = {};
    s.if_gain_reduction_db = 60;
    EXPECT_THROW(check_sdrplay_settings(s), std::invalid_argument);
    s = {};
    s.lna_state = 10;
    EXPECT_THROW(check_sdrplay_settings(s), std::invalid_argument);
}

TEST(SdrplaySource, WithoutTheLibrarySaysSoAndNothingElseBreaks) {
    SdrplaySettings settings;
    try {
        SdrplaySource source(settings, temp_file("no_such_library_kz4ap.dll"));
        FAIL() << "a source with no library";
    } catch (const std::runtime_error& e) {
        const std::string message = e.what();
        EXPECT_TRUE(message.find("not installed") != std::string::npos || message.find("built without") != std::string::npos)
            << message;
    }
    try {
        list_sdrplay_devices(temp_file("no_such_library_kz4ap.dll"));
        FAIL() << "a listing with no library";
    } catch (const std::runtime_error& e) {
        const std::string message = e.what();
        EXPECT_TRUE(message.find("not installed") != std::string::npos || message.find("built without") != std::string::npos)
            << message;
    }
}

std::filesystem::path counting_wav(const char* name, int rate, int samples) {
    const auto path = temp_file(name);
    IqRecorder recorder(rate);
    recorder.start(path);
    std::vector<short> xi(static_cast<std::size_t>(samples)), xq(static_cast<std::size_t>(samples), 0);
    std::iota(xi.begin(), xi.end(), static_cast<short>(0));
    recorder.push(xi.data(), xq.data(), xi.size());
    recorder.stop();
    return path;
}

TEST(FileIqSource, BeginsAtAChosenPlaceInTheRecording) {
    const auto path = counting_wav("kz4ap_seek.wav", 1000, 4000);
    auto source_owned = std::make_unique<FileIqSource>(path, 1.5);
    FileIqSource& source = *source_owned;
    source.set_speed(kAsFastAsPossible);
    EXPECT_EQ(source.total_samples(), 2500u);
    EXPECT_DOUBLE_EQ(source.recording_length_s(), 4.0);
    std::vector<Sample> out(4096);
    std::size_t total = 0;
    Sample first{};
    for (;;) {
        const IqRead got = source.read(out, 100ms);
        if (total == 0 && got.count > 0) first = out[0];
        total += got.count;
        if (got.end_of_stream) break;
    }
    EXPECT_EQ(total, 2500u);
    EXPECT_EQ(first.real(), 1500 / 32768.0f);

    source_owned.reset();
    auto past_owned = std::make_unique<FileIqSource>(path, 100.0);
    FileIqSource& past_the_end = *past_owned;
    past_the_end.set_speed(kAsFastAsPossible);
    EXPECT_EQ(past_the_end.total_samples(), 0u);
    EXPECT_TRUE(past_the_end.read(out, 100ms).end_of_stream);
    past_owned.reset();
    EXPECT_THROW(FileIqSource(path, -1.0), std::invalid_argument);
    std::filesystem::remove(path);
}

TEST(WavIqReader, SeeksForwardAndBack) {
    const auto path = counting_wav("kz4ap_seek_reader.wav", 1000, 1000);
    std::optional<WavIqReader> opened(std::in_place, path);
    WavIqReader& reader = *opened;
    std::vector<Sample> one(1);
    reader.seek(700);
    ASSERT_EQ(reader.read(one), 1u);
    EXPECT_EQ(one[0].real(), 700 / 32768.0f);
    reader.seek(3);
    ASSERT_EQ(reader.read(one), 1u);
    EXPECT_EQ(one[0].real(), 3 / 32768.0f);
    reader.seek(5000);
    EXPECT_EQ(reader.read(one), 0u);
    opened.reset();
    std::filesystem::remove(path);
}

// The detector's local floor: a comb of peaks standing on a wide signal, against one carrier on the same level of noise.
SpectrumFrame frame_of(double time_s, const std::vector<float>& db) {
    SpectrumFrame frame;
    frame.time_s = time_s;
    frame.power_db = db;
    return frame;
}

TEST(SignalDetectorLocalFloor, TakesACarrierButNotAPeakOfAWideSignal) {
    constexpr int kBins = 2048;
    const auto run = [&](double radius_hz) {
        DetectorConfig config;
        config.sample_rate = 48000;
        config.fft_size = kBins;
        config.hop = kBins / 2;
        config.local_floor_radius_hz = radius_hz;
        SignalDetector detector(config);
        // -80 dBFS of noise everywhere; a wide signal of -60 dBFS over bins 400 to 700 with a peak of -55 dBFS, 5 dB above it,
        // at bin 500; a carrier of -60 dBFS at bin 1500.
        std::vector<float> db(kBins, -80.0f);
        for (int i = 400; i <= 700; ++i) db[static_cast<std::size_t>(i)] = -60.0f;
        db[500] = -55.0f;
        db[1500] = -60.0f;
        for (int n = 0; n < 200; ++n) detector.process(frame_of(n * 1024.0 / 48000.0, db));
        return detector.tracks();
    };
    const auto bin_of = [](const Track& t) { return static_cast<int>(std::lround(t.freq_hz / (48000.0 / kBins))) + kBins / 2; };
    bool wide = false, carrier = false;
    for (const Track& t : run(1000.0)) {
        wide = wide || std::abs(bin_of(t) - 500) < 3;
        carrier = carrier || std::abs(bin_of(t) - 1500) < 3;
    }
    EXPECT_TRUE(carrier);
    EXPECT_FALSE(wide);
    // Without the test, the wide signal's peak is a track.
    wide = false;
    for (const Track& t : run(0.0)) wide = wide || std::abs(bin_of(t) - 500) < 3;
    EXPECT_TRUE(wide);
}

TEST(SignalDetector, ReadsLinearPowerWhenTheFrameHasIt) {
    DetectorConfig config;
    config.sample_rate = 48000;
    config.fft_size = 256;
    config.hop = 128;
    SignalDetector from_db(config), from_linear(config);
    std::vector<float> db(256, -80.0f);
    db[100] = -40.0f;
    std::vector<float> linear(256);
    for (std::size_t i = 0; i < 256; ++i) linear[i] = static_cast<float>(std::pow(10.0, db[i] / 10.0));
    for (int n = 0; n < 1000; ++n) {
        SpectrumFrame a = frame_of(n * 128.0 / 48000.0, db);
        SpectrumFrame b = a;
        b.power_fs2 = linear;
        from_db.process(a);
        from_linear.process(b);
    }
    ASSERT_EQ(from_db.tracks().size(), 1u);
    ASSERT_EQ(from_linear.tracks().size(), 1u);
    EXPECT_NEAR(from_db.tracks()[0].freq_hz, from_linear.tracks()[0].freq_hz, 1e-3);
    EXPECT_NEAR(from_db.tracks()[0].snr_db, from_linear.tracks()[0].snr_db, 1e-3);
}

}  // namespace
