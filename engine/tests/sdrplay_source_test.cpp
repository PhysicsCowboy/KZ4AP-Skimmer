// SdrplaySource against a stand-in for the API's library (sdrplay_fake_api.cpp): the error messages for no service and no
// radio, which of two radios is taken (only the one with the serial number asked for, never the first listed by default),
// listing them without taking any, the API's lock let go on every path, the settings as the API receives them, the samples, the gain, the overload's acknowledgment, the radio's going away,
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

// Held from the program's start, never let go: else a source's FreeLibrary unloads the stand-in after the first test to run
// and the next load counts from zero again, which once let a test of the lock pass with the lock left taken.
[[maybe_unused]] const HMODULE kHeld = LoadLibraryW(kFake.c_str());

// The stand-in's own counters. The module stays loaded for the whole test program (kHeld), so they are cumulative.
struct Fake {
    HMODULE module = LoadLibraryW(kFake.c_str());
    template <class R = int>
    R call(const char* name) const {
        return reinterpret_cast<R (*)()>(GetProcAddress(module, name))();
    }
    void event(int which) const { reinterpret_cast<void (*)(int)>(GetProcAddress(module, "fake_event"))(which); }
};

void pretend(const char* scenario) { SetEnvironmentVariableA("KZ4AP_FAKE_SDRPLAY", scenario); }

// The settings for the stand-in's radio of that serial number: FAKE0001 is listed first, FAKE0002 second.
SdrplaySettings radio_with_serial(const char* serial) {
    SdrplaySettings settings;
    settings.serial = serial;
    return settings;
}
SdrplaySettings first_radio() { return radio_with_serial("FAKE0001"); }

std::string failure_of(const char* scenario, const SdrplaySettings& settings = first_radio()) {
    pretend(scenario);
    try {
        SdrplaySource source(settings, kFake);
    } catch (const std::runtime_error& e) {
        return e.what();
    }
    return "";
}

// What the stand-in counted: a source refused before taking a radio leaves selects and inits as they were and the API
// unlocked.
struct Counts {
    int listings, selects, inits, releases, closes;
    static Counts of(const Fake& fake) {
        return {fake.call("fake_listings"), fake.call("fake_selects"), fake.call("fake_inits"), fake.call("fake_releases"),
                fake.call("fake_closes")};
    }
};

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

TEST(SdrplaySourceFake, SaysWhenTheApiCannotListItsDevicesAndLetsGoOfTheLock) {
    const Fake fake;
    const Counts before = Counts::of(fake);
    const std::string message = failure_of("list-error");
    EXPECT_NE(message.find("could not list"), std::string::npos) << message;
    EXPECT_EQ(fake.call("fake_locked"), 0) << "the API must not stay locked";
    EXPECT_EQ(Counts::of(fake).selects, before.selects);
    EXPECT_EQ(Counts::of(fake).closes, before.closes + 1) << "the API is closed again";
}

TEST(SdrplaySourceFake, OfTwoRadiosTakesTheOneWithTheSerialAskedForEvenIfListedSecond) {
    pretend("");
    const Fake fake;
    const Counts before = Counts::of(fake);
    {
        SdrplaySource source(radio_with_serial("FAKE0002"), kFake);
        EXPECT_EQ(std::string(fake.call<const char*>("fake_selected_serial")), "FAKE0002");
        EXPECT_EQ(fake.call("fake_init_device"), 2) << "the stream started on FAKE0002's handle";
        EXPECT_EQ(fake.call("fake_locked"), 0) << "the lock is let go once the radio is taken";
        EXPECT_EQ(Counts::of(fake).selects, before.selects + 1) << "one radio taken, and only one";
        EXPECT_NE(source.description().find("FAKE0002"), std::string::npos) << source.description();
        EXPECT_NE(source.description().find("RSPdx"), std::string::npos) << source.description();
        ASSERT_GE(read_samples(source, 1000).size(), 1000u);
    }
    EXPECT_EQ(Counts::of(fake).releases, before.releases + 1);
}

TEST(SdrplaySourceFake, TakesTheFirstListedOnlyWhenItsSerialIsAskedFor) {
    pretend("");
    const Fake fake;
    SdrplaySource source(first_radio(), kFake);
    EXPECT_EQ(std::string(fake.call<const char*>("fake_selected_serial")), "FAKE0001");
    EXPECT_EQ(fake.call("fake_init_device"), 1);
    EXPECT_NE(source.description().find("FAKE0001"), std::string::npos) << source.description();
}

TEST(SdrplaySourceFake, RefusesASerialNoRadioHasNamingThoseFound) {
    const Fake fake;
    for (const char* wrong : {"FAKE0003", "fake0002", "FAKE000", "FAKE00021", " FAKE0002"}) {
        const Counts before = Counts::of(fake);
        const std::string message = failure_of("", radio_with_serial(wrong));
        EXPECT_NE(message.find(std::string("\"") + wrong + "\""), std::string::npos) << message;
        EXPECT_NE(message.find("FAKE0001 (RSP1A)"), std::string::npos) << message;
        EXPECT_NE(message.find("FAKE0002 (RSPdx)"), std::string::npos) << message;
        EXPECT_NE(message.find("none was opened"), std::string::npos) << message;
        const Counts after = Counts::of(fake);
        EXPECT_EQ(after.listings, before.listings + 1) << wrong;
        EXPECT_EQ(after.selects, before.selects) << "no radio may be taken for " << wrong;
        EXPECT_EQ(after.inits, before.inits) << wrong;
        EXPECT_EQ(after.closes, before.closes + 1) << wrong;
        EXPECT_EQ(fake.call("fake_locked"), 0) << "the API must not stay locked: " << wrong;
    }
}

TEST(SdrplaySourceFake, WithNoSerialTakesNoRadioAndSaysWhichThereAre) {
    const Fake fake;
    for (const char* scenario : {"", "one-device"}) {
        const Counts before = Counts::of(fake);
        const std::string message = failure_of(scenario, radio_with_serial(""));
        EXPECT_NE(message.find("No SDRplay radio was named"), std::string::npos) << message;
        EXPECT_NE(message.find("FAKE0001 (RSP1A)"), std::string::npos) << message;
        EXPECT_EQ(message.find("FAKE0002") != std::string::npos, std::string(scenario).empty()) << message;
        EXPECT_NE(message.find("--sdrplay-serial"), std::string::npos) << message;
        const Counts after = Counts::of(fake);
        EXPECT_EQ(after.selects, before.selects) << "not even the only radio is taken (scenario \"" << scenario << "\")";
        EXPECT_EQ(after.inits, before.inits) << scenario;
        EXPECT_EQ(fake.call("fake_locked"), 0) << "the API must not stay locked (scenario \"" << scenario << "\")";
    }
}

TEST(SdrplaySourceFake, TakesAnRspduoAsASingleTunerOnTunerA) {
    pretend("duo");
    const Fake fake;
    SdrplaySource source(radio_with_serial("FAKE0002"), kFake);
    EXPECT_EQ(std::string(fake.call<const char*>("fake_selected_serial")), "FAKE0002");
    // The API's values (sdrplay_api_tuner.h, sdrplay_api_rspDuo.h): sdrplay_api_Tuner_A = 1, sdrplay_api_RspDuoMode_Single_Tuner
    // = 1; the stand-in listed it with tuner 3 (both) and modes 7 (single, dual, master).
    EXPECT_EQ(fake.call("fake_selected_tuner"), 1);
    EXPECT_EQ(fake.call("fake_selected_duo_mode"), 1);
    EXPECT_NE(source.description().find("RSPduo"), std::string::npos) << source.description();
}

TEST(SdrplaySourceFake, ListsTheRadiosWithoutTakingOrStartingAny) {
    pretend("");
    const Fake fake;
    const Counts before = Counts::of(fake);
    const std::vector<SdrplayDevice> devices = list_sdrplay_devices(kFake);
    ASSERT_EQ(devices.size(), 2u);
    EXPECT_EQ(devices[0].serial, "FAKE0001");
    EXPECT_EQ(devices[0].model, "RSP1A");
    EXPECT_EQ(devices[1].serial, "FAKE0002");
    EXPECT_EQ(devices[1].model, "RSPdx");
    const Counts after = Counts::of(fake);
    EXPECT_EQ(after.listings, before.listings + 1);
    EXPECT_EQ(after.selects, before.selects) << "listing must never call SelectDevice";
    EXPECT_EQ(after.inits, before.inits) << "listing must never call Init";
    EXPECT_EQ(after.releases, before.releases);
    EXPECT_EQ(after.closes, before.closes + 1) << "the API is closed again";
    EXPECT_EQ(fake.call("fake_locked"), 0) << "the API must not stay locked";

    pretend("no-device");
    EXPECT_TRUE(list_sdrplay_devices(kFake).empty());
    EXPECT_EQ(Counts::of(fake).selects, before.selects);
}

TEST(SdrplaySourceFake, ListingSaysWhatIsWrongAndLetsGoOfTheLock) {
    const Fake fake;
    const auto failure = [](const char* scenario) -> std::string {
        pretend(scenario);
        try {
            list_sdrplay_devices(kFake);
        } catch (const std::runtime_error& e) {
            return e.what();
        }
        return "";
    };
    EXPECT_NE(failure("no-service").find("service"), std::string::npos);
    EXPECT_NE(failure("old-version").find("version"), std::string::npos);
    EXPECT_NE(failure("list-error").find("could not list"), std::string::npos);
    EXPECT_EQ(fake.call("fake_locked"), 0) << "the API must not stay locked";
}

TEST(SdrplaySourceFake, RefusesAnotherVersionOfTheApi) {
    const std::string message = failure_of("old-version");
    EXPECT_NE(message.find("version"), std::string::npos) << message;
}

TEST(SdrplaySourceFake, CheckingSettingsComesBeforeTheRadio) {
    pretend("");
    SdrplaySettings settings = first_radio();
    settings.if_gain_reduction_db = 10;
    EXPECT_THROW(SdrplaySource(settings, kFake), std::invalid_argument);
}

TEST(SdrplaySourceFake, GivesTheApiTheSettingsAndReadsTheSamples) {
    pretend("");
    const Fake fake;
    SdrplaySettings settings = first_radio();
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
    EXPECT_EQ(std::string(fake.call<const char*>("fake_selected_serial")), "FAKE0001");
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
    SdrplaySource source(first_radio(), kFake);
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
    SdrplaySource source(first_radio(), kFake);
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
    auto owned = std::make_unique<SdrplaySource>(first_radio(), kFake);
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
    SdrplaySource source(first_radio(), kFake);
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
        SdrplaySource source(first_radio(), kFake);
        read_samples(source, 1000);
    }
    EXPECT_EQ(fake.call("fake_uninits"), uninits + 1);
    EXPECT_EQ(fake.call("fake_releases"), releases + 1);
    EXPECT_EQ(fake.call("fake_closes"), closes + 1);
}

TEST(SdrplaySourceFake, AFullRingIsCountedAndTheReaderGoesOnFromNewSamples) {
    // What a reader that stops for longer than the ring holds meets (at the lowest rate, the smallest ring: 5.46 s of samples): the
    // samples that did not fit are counted, what the ring held is thrown away with them, and what follows has no gap.
    pretend("");
    SdrplaySettings settings = first_radio();
    settings.sample_rate = 96000;
    SdrplaySource source(settings, kFake);
    const auto limit = std::chrono::steady_clock::now() + 60s;
    while (source.dropped_samples() == 0 && std::chrono::steady_clock::now() < limit) std::this_thread::sleep_for(50ms);
    ASSERT_GT(source.dropped_samples(), 0u) << "the stand-in delivers faster than the rate, so an unread ring fills";
    const std::vector<Sample> samples = read_samples(source, 5000);
    ASSERT_GE(samples.size(), 5000u);
    EXPECT_GE(source.dropped_samples(), 96000u * 5u) << "the ring's contents count as lost";
    for (std::size_t i = 1; i < samples.size(); ++i) {
        const float step = samples[i].real() * 32768.0f - samples[i - 1].real() * 32768.0f;
        ASSERT_TRUE(step == 1.0f || step == -65535.0f) << "a gap at " << i;
    }
}

TEST(SdrplaySourceFake, RecordsTheRawSamplesEvenIfThePipelineReadsNothing) {
    pretend("");
    const auto path = std::filesystem::temp_directory_path() / "kz4ap_sdrplay_recording.wav";
    {
        SdrplaySource source(first_radio(), kFake);
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
