// A live radio as an IqSource: an SDRplay RSP through the SDRplay API 3.x (design sections 4 and 4.1, step 1). The API's
// library is loaded when a source is made, never linked, so that everything builds and runs without it; its service delivers
// sample blocks on its own thread, which copies them into a lock-free ring and returns at once. Nothing here can make the
// radio do anything but receive. The API's headers are needed to build this file's real form (found by CMake, or
// KZ4AP_SDRPLAY_INCLUDE_DIR); without them, making a source says so.
#pragma once

#include "kz4ap/iq_recorder.hpp"
#include "kz4ap/iq_ring.hpp"
#include "kz4ap/iq_source.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace kz4ap {

// How the radio makes a sample rate: an ADC rate and the API's hardware decimation, and the tuner's analog filter.
struct SdrplayRatePlan {
    double adc_hz = 0;
    int decimation = 1;      // 1, 2, 4, 8, 16 or 32
    int bandwidth_khz = 0;   // the API's filter: 200, 300, 600 or 1536, the narrowest not narrower than the span
};

// The sample rates offered, complex samples/s. Exact multiples of the engine's 23.4 Hz bins: 96, 192, 384, 768 kHz.
std::vector<int> sdrplay_sample_rates();
// None for a rate not in that list.
std::optional<SdrplayRatePlan> sdrplay_rate_plan(int sample_rate);

// The operator's settings. The API's limits, checked by check_sdrplay_settings: 1 kHz to 2 GHz, IF gain reduction 20 to
// 59 dB, the LNA state 0 to 9 (a model's table of LNA reduction by state and band has fewer states in some bands, which the
// API itself refuses).
struct SdrplaySettings {
    // The serial number of the radio to open, exactly as the API lists it (SdrplayDevice::serial). Required: with none, or
    // with one no radio listed has, no radio is taken, even when only one is plugged in, so that a machine with two radios
    // can never open the wrong one. check_sdrplay_settings does not check it: only the API's list can.
    std::string serial;
    double center_hz = 7031500.0;
    int sample_rate = 192000;
    int if_gain_reduction_db = 40;  // dB, the IF's gain reduction; AGC is off, so a level stays a level
    int lna_state = 0;              // the LNA's gain reduction state: 0 is the most gain
};
// Throws std::invalid_argument, saying what is out of range.
void check_sdrplay_settings(const SdrplaySettings& settings);

// Where to look for the API's library: KZ4AP_SDRPLAY_DLL from the environment if set, then the system's search, then the
// API installer's location.
std::vector<std::filesystem::path> sdrplay_library_candidates();

// A radio as the API lists it.
struct SdrplayDevice {
    std::string serial;  // SdrplaySettings::serial's value for it
    std::string model;   // RSP1A, RSPdx, RSPduo, ...
};

// The radios the API lists, in its order, without taking or starting any: the API's GetDevices under its lock, and never
// SelectDevice or Init. Throws std::runtime_error with an operator's sentence, as SdrplaySource does: no library, no
// service, another version of the API. library: as for SdrplaySource.
std::vector<SdrplayDevice> list_sdrplay_devices(const std::filesystem::path& library = {});

class SdrplaySource final : public IqSource, public RecordControl, public RadioControl {
public:
    // Opens the API, takes the device whose serial number is exactly settings.serial and starts it receiving; no other
    // device is ever taken (an RSPduo as a single tuner, tuner A). Throws std::runtime_error with an operator's sentence: no
    // library, no service, no device, no serial given or none with that serial (each naming the serials found), another
    // program holding it. The API's lock is let go on every path. std::invalid_argument for settings out of range.
    // library: the API's library to load; empty for sdrplay_library_candidates().
    explicit SdrplaySource(const SdrplaySettings& settings, const std::filesystem::path& library = {});
    ~SdrplaySource() override;

    int sample_rate() const override { return settings_.sample_rate; }
    std::string description() const override { return description_; }
    std::optional<double> center_frequency_hz() const override { return settings_.center_hz; }
    // None until the first block of samples arrives; then the system clock's time of that block's first sample
    // (first_sample_unix_ms, utc_time.hpp): late by the radio's and its driver's buffering, a few ms to a few tens of ms (a
    // heuristic estimate). Stream time after a gap in the stream (dropped_samples) lags this clock by the samples lost.
    std::optional<std::int64_t> start_unix_ms() const override {
        const std::int64_t ms = start_unix_ms_.load(std::memory_order_acquire);
        return ms == kNoStart ? std::nullopt : std::optional<std::int64_t>(ms);
    }
    IqRead read(std::span<Sample> out, std::chrono::milliseconds timeout) override;
    void interrupt() override { interrupted_ = true; }
    std::uint64_t dropped_samples() const override { return dropped_.load(std::memory_order_relaxed); }
    double backlog_s() const override { return static_cast<double>(ring_.size()) / settings_.sample_rate; }
    RecordControl* recorder() override { return this; }
    RadioControl* radio() override { return this; }

    void start_recording(const std::filesystem::path& path) override { recorder_.start(path); }
    void stop_recording() override { recorder_.stop(); }
    RecordingStatus recording_status() const override { return recorder_.status(); }

    void set_gain(int if_gain_reduction_db, int lna_state) override;
    RadioStatus radio_status() const override;

    // The ring, at least this, s of stream: time for a loaded pipeline to catch up before samples are lost. Heuristic. SpscRing
    // rounds its capacity up to a power of two, so at each rate the radio offers (96, 192, 384, 768 kHz) it holds 2^k samples with
    // 2^k / rate = 5.46 s.
    static constexpr double kRingS = 4.0;

private:
    struct Api;
    // The API's callbacks reach these.
    void on_samples(const short* xi, const short* xq, unsigned int count, bool reset);
    void on_overload(bool detected);
    void on_gain(double gain_db);
    void on_lost(const std::string& message);

    friend struct SdrplayCallbacks;
    SdrplaySettings settings_;
    std::string description_;
    SpscRing<Sample> ring_;
    IqRecorder recorder_;
    std::vector<Sample> convert_;  // the API thread's
    std::atomic<std::uint64_t> received_{0};
    static constexpr std::int64_t kNoStart = INT64_MIN;
    std::atomic<std::int64_t> start_unix_ms_{kNoStart};  // set once, by the API's thread, at the first block
    std::atomic<std::uint64_t> dropped_{0};
    // The ring overflowed. Samples lost a few at a time, every block the pipeline reads, cut a strong station's carrier into
    // a splash of wide clicks that neighboring tracks read as E E E (docs/decision-log.md, 2026-10-31), and read the station itself
    // as garbage. So the reader then empties the ring: one gap, of about kRingS, then clean samples.
    std::atomic<bool> overflowed_{false};
    std::atomic<bool> interrupted_{false};
    std::atomic<bool> overload_{false};
    std::atomic<std::uint64_t> resets_{0};
    std::atomic<double> gain_db_{0.0};
    std::atomic<bool> have_gain_{false};
    std::atomic<bool> lost_{false};
    mutable std::mutex mutex_;  // the message below; the gain's settings
    std::string lost_message_;
    std::unique_ptr<Api> api_;
};

}  // namespace kz4ap
