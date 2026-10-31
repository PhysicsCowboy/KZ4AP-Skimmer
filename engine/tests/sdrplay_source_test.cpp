// SdrplaySource against a stand-in for the API's library (sdrplay_fake_api.cpp): the error messages for no service and no
// radio, the settings as the API receives them, the samples, the gain, the overload's acknowledgment, the radio's going away,
// and an orderly end, all with no radio attached and whatever the machine has installed.
#include "kz4ap/sdrplay_source.hpp"
#include "kz4ap/stream_runner.hpp"
#include "kz4ap/wav_reader.hpp"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using namespace kz4ap;
using namespace std::chrono_literals;

namespace {

const std::filesystem::path kFake = KZ4AP_FAKE_SDRPLAY_DLL;

// The stand-in's own counters. The module stays loaded for the whole test program, so they are cumulative.
struct Fake {
    HMODULE module = LoadLibraryW(kFake.c_str());
    template <class R = int>
    R call(const char* name) const {
        return reinterpret_cast<R (*)()>(GetProcAddress(module, name))();
    }
    void event(int which) const { reinterpret_cast<void (*)(int)>(GetProcAddress(module, "fake_event"))(which); }
};

void pretend(const char* scenario) { SetEnvironmentVariableA("KZ4AP_FAKE_SDRPLAY", scenario); }

std::string failure_of(const char* scenario) {
    pretend(scenario);
    try {
        SdrplaySource source(SdrplaySettings{}, kFake);
    } catch (const std::runtime_error& e) {
        return e.what();
    }
    return "";
}

// Waits until at least count samples have been read.
std::vector<Sample> read_samples(IqSource& source, std::size_t count) {
    std::vector<Sample> all;
    std::vector<Sample> block(4096);
    const auto limit = std::chrono::steady_clock::now() + 20s;
    while (all.size() < count && std::chrono::steady_clock::now() < limit) {
        const IqRead got = source.read(block, 50ms);
        all.insert(all.end(), block.begin(), block.begin() + static_cast<std::ptrdiff_t>(got.count));
    }
    return all;
}

TEST(SdrplaySourceFake, SaysWhenTheServiceIsNotAnswering) {
    const std::string message = failure_of("no-service");
    EXPECT_NE(message.find("service"), std::string::npos) << message;
}

TEST(SdrplaySourceFake, SaysWhenThereIsNoRadio) {
    const std::string message = failure_of("no-device");
    EXPECT_NE(message.find("No SDRplay radio"), std::string::npos) << message;
    EXPECT_EQ(Fake{}.call("fake_locked"), 0) << "the API must not stay locked";
}

TEST(SdrplaySourceFake, SaysWhenAnotherProgramHasTheRadio) {
    const std::string message = failure_of("busy");
    EXPECT_NE(message.find("another program"), std::string::npos) << message;
    EXPECT_EQ(Fake{}.call("fake_locked"), 0) << "the API must not stay locked";
}

TEST(SdrplaySourceFake, RefusesAnotherVersionOfTheApi) {
    const std::string message = failure_of("old-version");
    EXPECT_NE(message.find("version"), std::string::npos) << message;
}

TEST(SdrplaySourceFake, CheckingSettingsComesBeforeTheRadio) {
    pretend("");
    SdrplaySettings settings;
    settings.if_gain_reduction_db = 10;
    EXPECT_THROW(SdrplaySource(settings, kFake), std::invalid_argument);
}

TEST(SdrplaySourceFake, GivesTheApiTheSettingsAndReadsTheSamples) {
    pretend("");
    const Fake fake;
    SdrplaySettings settings;
    settings.center_hz = 14025000.0;
    settings.sample_rate = 384000;
    settings.if_gain_reduction_db = 33;
    settings.lna_state = 2;
    SdrplaySource source(settings, kFake);
    EXPECT_EQ(fake.call<double>("fake_center_hz"), 14025000.0);
    EXPECT_EQ(fake.call<double>("fake_adc_hz"), 3072000.0);
    EXPECT_EQ(fake.call("fake_decimation"), 8);
    EXPECT_EQ(fake.call("fake_bandwidth_khz"), 600);
    EXPECT_EQ(fake.call("fake_if_reduction_db"), 33);
    EXPECT_EQ(fake.call("fake_lna_state"), 2);
    EXPECT_EQ(fake.call("fake_agc_enable"), 0);
    EXPECT_EQ(source.sample_rate(), 384000);
    EXPECT_EQ(source.center_frequency_hz(), 14025000.0);
    EXPECT_NE(source.description().find("RSP1A"), std::string::npos);
    EXPECT_FALSE(source.playback());
    EXPECT_TRUE(source.radio());
    EXPECT_TRUE(source.recorder());

    const std::vector<Sample> samples = read_samples(source, 5000);
    ASSERT_GE(samples.size(), 5000u);
    for (std::size_t i = 1; i < samples.size(); ++i) {
        ASSERT_EQ(samples[i].real() * 32768.0f, samples[i - 1].real() * 32768.0f + 1.0f) << i;
        ASSERT_EQ(samples[i].imag(), -samples[i].real());
    }
    EXPECT_EQ(source.dropped_samples(), 0u);
    EXPECT_EQ(source.radio_status().resets, 0u) << "the API's first block's reset is not a restart";
}

TEST(SdrplaySourceFake, ChangesTheGainAsItRunsAndSaysWhenTheRadioRefuses) {
    pretend("");
    const Fake fake;
    SdrplaySource source(SdrplaySettings{}, kFake);
    source.set_gain(50, 3);
    EXPECT_EQ(fake.call("fake_if_reduction_db"), 50);
    EXPECT_EQ(fake.call("fake_lna_state"), 3);
    RadioStatus status = source.radio_status();
    EXPECT_EQ(status.if_gain_reduction_db, 50);
    EXPECT_EQ(status.lna_state, 3);
    ASSERT_TRUE(status.gain_db.has_value());
    EXPECT_DOUBLE_EQ(*status.gain_db, 10.0);
    EXPECT_THROW(source.set_gain(70, 0), std::invalid_argument);
    EXPECT_THROW(source.set_gain(50, 8), std::runtime_error);  // a state the stand-in's radio does not have
    status = source.radio_status();
    EXPECT_EQ(status.lna_state, 3);
    EXPECT_EQ(fake.call("fake_lna_state"), 3);
}

TEST(SdrplaySourceFake, AcknowledgesAnOverloadAndSaysSo) {
    pretend("");
    const Fake fake;
    SdrplaySource source(SdrplaySettings{}, kFake);
    const int before = fake.call("fake_overload_acks");
    fake.event(0);
    EXPECT_TRUE(source.radio_status().overload);
    EXPECT_EQ(fake.call("fake_overload_acks"), before + 1);
    fake.event(1);
    EXPECT_FALSE(source.radio_status().overload);
    EXPECT_EQ(fake.call("fake_overload_acks"), before + 2);
}

TEST(SdrplaySourceFake, ASourceThatLosesItsRadioEndsTheStreamSayingWhy) {
    pretend("");
    const Fake fake;
    auto owned = std::make_unique<SdrplaySource>(SdrplaySettings{}, kFake);
    StreamRunner runner(std::move(owned), EngineConfig{}, [](const Event&) {});
    std::this_thread::sleep_for(100ms);
    fake.event(2);
    const auto limit = std::chrono::steady_clock::now() + 20s;
    while (runner.status().state != StreamStatus::State::Failed && std::chrono::steady_clock::now() < limit)
        std::this_thread::sleep_for(5ms);
    EXPECT_EQ(runner.status().state, StreamStatus::State::Failed);
    EXPECT_NE(runner.status().error.find("unplugged"), std::string::npos) << runner.status().error;
}

TEST(SdrplaySourceFake, InterruptEndsAWaitingRead) {
    pretend("");
    SdrplaySource source(SdrplaySettings{}, kFake);
    source.interrupt();
    std::vector<Sample> block(1);
    // Samples still come while there are any; none are lost to a caller that stops reading.
    EXPECT_NO_THROW(source.read(block, 10ms));
}

TEST(SdrplaySourceFake, LetsGoOfTheRadioInOrder) {
    pretend("");
    const Fake fake;
    const int uninits = fake.call("fake_uninits"), releases = fake.call("fake_releases"), closes = fake.call("fake_closes");
    {
        SdrplaySource source(SdrplaySettings{}, kFake);
        read_samples(source, 1000);
    }
    EXPECT_EQ(fake.call("fake_uninits"), uninits + 1);
    EXPECT_EQ(fake.call("fake_releases"), releases + 1);
    EXPECT_EQ(fake.call("fake_closes"), closes + 1);
}

TEST(SdrplaySourceFake, RecordsTheRawSamplesEvenIfThePipelineReadsNothing) {
    pretend("");
    const auto path = std::filesystem::temp_directory_path() / "kz4ap_sdrplay_recording.wav";
    {
        SdrplaySource source(SdrplaySettings{}, kFake);
        source.start_recording(path);
        EXPECT_TRUE(source.recording_status().active);
        std::this_thread::sleep_for(300ms);
        source.stop_recording();
        const RecordingStatus status = source.recording_status();
        EXPECT_FALSE(status.active);
        EXPECT_GT(status.samples, 1000u);
        EXPECT_EQ(status.dropped, 0u);
        EXPECT_EQ(WavIqReader(path).total_samples(), status.samples);
        EXPECT_EQ(WavIqReader(path).sample_rate(), 192000);
    }
    {
        WavIqReader reader(path);
        std::vector<Sample> samples(static_cast<std::size_t>(reader.total_samples()));
        reader.read(samples);
        for (std::size_t i = 1; i < samples.size(); ++i)
            ASSERT_EQ(samples[i].real() * 32768.0f, samples[i - 1].real() * 32768.0f + 1.0f) << "a gap at " << i;
    }
    std::filesystem::remove(path);
}

}  // namespace
