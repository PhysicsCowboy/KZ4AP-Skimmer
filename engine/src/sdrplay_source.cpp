#include "kz4ap/sdrplay_source.hpp"

#if defined(KZ4AP_HAVE_SDRPLAY_HEADERS)
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <dlfcn.h>
#endif
#include <sdrplay_api.h>
#endif

#include <cmath>
#include <cstdlib>
#include <stdexcept>
#include <thread>

namespace kz4ap {

std::vector<int> sdrplay_sample_rates() { return {96000, 192000, 384000, 768000}; }

std::optional<SdrplayRatePlan> sdrplay_rate_plan(int sample_rate) {
    // The ADC at 3.072 MHz (14 bits below 6.048 MHz, by the API's specification) divided by the API's largest decimations: the
    // API takes an ADC rate of 2 MHz or more, so 62.5 kHz is the least it can give, and 48 kHz is not a rate of the radio's.
    switch (sample_rate) {
        case 96000: return SdrplayRatePlan{3072000.0, 32, 200};
        case 192000: return SdrplayRatePlan{3072000.0, 16, 200};
        case 384000: return SdrplayRatePlan{3072000.0, 8, 600};
        case 768000: return SdrplayRatePlan{3072000.0, 4, 1536};
        default: return std::nullopt;
    }
}

void check_sdrplay_settings(const SdrplaySettings& s) {
    if (!(s.center_hz >= 1000.0 && s.center_hz <= 2.0e9))
        throw std::invalid_argument("the center frequency must lie from 1 kHz to 2 GHz");
    if (!sdrplay_rate_plan(s.sample_rate)) throw std::invalid_argument("the sample rate must be 96, 192, 384 or 768 kHz");
    if (s.if_gain_reduction_db < 20 || s.if_gain_reduction_db > 59)
        throw std::invalid_argument("the IF gain reduction must lie from 20 to 59 dB");
    if (s.lna_state < 0 || s.lna_state > 9) throw std::invalid_argument("the LNA state must lie from 0 to 9");
}

std::vector<std::filesystem::path> sdrplay_library_candidates() {
    std::vector<std::filesystem::path> paths;
#if defined(_WIN32)
    wchar_t* from_environment = nullptr;
    std::size_t size = 0;
    if (_wdupenv_s(&from_environment, &size, L"KZ4AP_SDRPLAY_DLL") == 0 && from_environment != nullptr) {
        paths.emplace_back(from_environment);
        std::free(from_environment);
        return paths;
    }
    paths.emplace_back(L"sdrplay_api.dll");
    paths.emplace_back(L"C:\\Program Files\\SDRplay\\API\\x64\\sdrplay_api.dll");
#else
    if (const char* from_environment = std::getenv("KZ4AP_SDRPLAY_DLL"); from_environment != nullptr) {
        paths.emplace_back(from_environment);
        return paths;
    }
    paths.emplace_back("libsdrplay_api.so.3");
    paths.emplace_back("/usr/local/lib/libsdrplay_api.so");
#endif
    return paths;
}

#if !defined(KZ4AP_HAVE_SDRPLAY_HEADERS)

struct SdrplaySource::Api {};

SdrplaySource::SdrplaySource(const SdrplaySettings& settings, const std::filesystem::path&)
    : settings_(settings), ring_(2), recorder_(settings.sample_rate > 0 ? settings.sample_rate : 1) {
    check_sdrplay_settings(settings);
    throw std::runtime_error(
        "This program was built without the SDRplay API's headers, so it cannot drive a radio: install the SDRplay API and "
        "build it again.");
}

SdrplaySource::~SdrplaySource() = default;
void SdrplaySource::on_samples(const short*, const short*, unsigned int, bool) {}
void SdrplaySource::on_overload(bool) {}
void SdrplaySource::on_gain(double) {}
void SdrplaySource::on_lost(const std::string&) {}
void SdrplaySource::set_gain(int, int) {}

#else

namespace {

#if defined(_WIN32)
using LibraryHandle = HMODULE;
LibraryHandle load_library(const std::filesystem::path& path) { return LoadLibraryW(path.c_str()); }
void* find_symbol(LibraryHandle library, const char* name) { return reinterpret_cast<void*>(GetProcAddress(library, name)); }
void free_library(LibraryHandle library) { FreeLibrary(library); }
#else
using LibraryHandle = void*;
LibraryHandle load_library(const std::filesystem::path& path) { return dlopen(path.c_str(), RTLD_NOW); }
void* find_symbol(LibraryHandle library, const char* name) { return dlsym(library, name); }
void free_library(LibraryHandle library) { dlclose(library); }
#endif

const char* model_name(unsigned char hardware) {
    switch (hardware) {
        case SDRPLAY_RSP1_ID: return "RSP1";
        case SDRPLAY_RSP1A_ID: return "RSP1A";
        case SDRPLAY_RSP1B_ID: return "RSP1B";
        case SDRPLAY_RSP2_ID: return "RSP2";
        case SDRPLAY_RSPduo_ID: return "RSPduo";
        case SDRPLAY_RSPdx_ID: return "RSPdx";
        case SDRPLAY_RSPdxR2_ID: return "RSPdx-R2";
        default: return "RSP";
    }
}

}  // namespace

// The API's entry points, found by name in the library loaded at run time.
struct SdrplaySource::Api {
    LibraryHandle library = nullptr;
    sdrplay_api_Open_t open = nullptr;
    sdrplay_api_Close_t close = nullptr;
    sdrplay_api_ApiVersion_t version = nullptr;
    sdrplay_api_LockDeviceApi_t lock = nullptr;
    sdrplay_api_UnlockDeviceApi_t unlock = nullptr;
    sdrplay_api_GetDevices_t get_devices = nullptr;
    sdrplay_api_SelectDevice_t select = nullptr;
    sdrplay_api_ReleaseDevice_t release = nullptr;
    sdrplay_api_GetErrorString_t error_string = nullptr;
    sdrplay_api_GetDeviceParams_t get_params = nullptr;
    sdrplay_api_Init_t init = nullptr;
    sdrplay_api_Uninit_t uninit = nullptr;
    sdrplay_api_Update_t update = nullptr;

    bool opened = false;
    bool selected = false;
    bool streaming = false;
    sdrplay_api_DeviceT device{};
    sdrplay_api_RxChannelParamsT* channel = nullptr;
    sdrplay_api_DeviceParamsT* params = nullptr;

    template <class F>
    void bind(F& entry, const char* name) {
        entry = reinterpret_cast<F>(find_symbol(library, name));
        if (entry == nullptr)
            throw std::runtime_error(std::string("The SDRplay API library has no ") + name + ": is it version 3?");
    }

    std::string text(sdrplay_api_ErrT error) const {
        const char* s = error_string ? error_string(error) : nullptr;
        return s ? s : std::to_string(static_cast<int>(error));
    }

    ~Api() {
        if (streaming) uninit(device.dev);
        if (selected) release(&device);
        if (opened) close();
        if (library != nullptr) free_library(library);
    }
};

// The API calls these on its own threads, with the source as the context.
struct SdrplayCallbacks {
    static void stream(short* xi, short* xq, sdrplay_api_StreamCbParamsT*, unsigned int count, unsigned int reset, void* context) {
        static_cast<SdrplaySource*>(context)->on_samples(xi, xq, count, reset != 0);
    }
    static void stream_b(short*, short*, sdrplay_api_StreamCbParamsT*, unsigned int, unsigned int, void*) {}
    static void event(sdrplay_api_EventT id, sdrplay_api_TunerSelectT tuner, sdrplay_api_EventParamsT* params, void* context) {
        auto* source = static_cast<SdrplaySource*>(context);
        switch (id) {
            case sdrplay_api_PowerOverloadChange: {
                const bool detected = params->powerOverloadParams.powerOverloadChangeType == sdrplay_api_Overload_Detected;
                source->on_overload(detected);
                // The API asks for an acknowledgment of each change (specification, section 3.17).
                source->api_->update(source->api_->device.dev, tuner, sdrplay_api_Update_Ctrl_OverloadMsgAck, sdrplay_api_Update_Ext1_None);
                break;
            }
            case sdrplay_api_GainChange: source->on_gain(params->gainParams.currGain); break;
            case sdrplay_api_DeviceRemoved: source->on_lost("The radio was unplugged."); break;
            case sdrplay_api_DeviceFailure: source->on_lost("The radio failed."); break;
            default: break;
        }
    }
};

SdrplaySource::SdrplaySource(const SdrplaySettings& settings, const std::filesystem::path& library)
    : settings_(settings),
      ring_(static_cast<std::size_t>(kRingS * (settings.sample_rate > 0 ? settings.sample_rate : 1))),
      recorder_(settings.sample_rate > 0 ? settings.sample_rate : 1),
      api_(std::make_unique<Api>()) {
    check_sdrplay_settings(settings);
    const SdrplayRatePlan plan = *sdrplay_rate_plan(settings.sample_rate);

    std::vector<std::filesystem::path> candidates;
    if (library.empty()) candidates = sdrplay_library_candidates();
    else candidates.push_back(library);
    std::string tried;
    for (const std::filesystem::path& path : candidates) {
        api_->library = load_library(path);
        if (api_->library != nullptr) break;
        tried += (tried.empty() ? "" : ", ") + path.string();
    }
    if (api_->library == nullptr)
        throw std::runtime_error("The SDRplay API is not installed: no library at " + tried +
                                 ". Install it from sdrplay.com, or set KZ4AP_SDRPLAY_DLL to its library.");

    api_->bind(api_->open, "sdrplay_api_Open");
    api_->bind(api_->close, "sdrplay_api_Close");
    api_->bind(api_->version, "sdrplay_api_ApiVersion");
    api_->bind(api_->lock, "sdrplay_api_LockDeviceApi");
    api_->bind(api_->unlock, "sdrplay_api_UnlockDeviceApi");
    api_->bind(api_->get_devices, "sdrplay_api_GetDevices");
    api_->bind(api_->select, "sdrplay_api_SelectDevice");
    api_->bind(api_->release, "sdrplay_api_ReleaseDevice");
    api_->bind(api_->error_string, "sdrplay_api_GetErrorString");
    api_->bind(api_->get_params, "sdrplay_api_GetDeviceParams");
    api_->bind(api_->init, "sdrplay_api_Init");
    api_->bind(api_->uninit, "sdrplay_api_Uninit");
    api_->bind(api_->update, "sdrplay_api_Update");

    const char* const kNoService =
        "The SDRplay API service is not answering. Start it: Windows Services, \"SDRplay API Service\" (or RestartService.bat "
        "in the API's folder).";
    sdrplay_api_ErrT error = api_->open();
    if (error != sdrplay_api_Success)
        throw std::runtime_error(error == sdrplay_api_ServiceNotResponding || error == sdrplay_api_Fail
                                     ? std::string(kNoService)
                                     : "The SDRplay API did not open: " + api_->text(error));
    api_->opened = true;

    float version = 0.0f;
    error = api_->version(&version);
    if (error == sdrplay_api_ServiceNotResponding) throw std::runtime_error(kNoService);
    if (error != sdrplay_api_Success) throw std::runtime_error("The SDRplay API gave no version: " + api_->text(error));
    if (std::fabs(version - SDRPLAY_API_VERSION) > 0.001f)
        throw std::runtime_error("The SDRplay API is version " + std::to_string(version) + " and this program was built for " +
                                 std::to_string(SDRPLAY_API_VERSION) + ": the device structures may differ, so install that version or build again.");

    error = api_->lock();
    if (error == sdrplay_api_ServiceNotResponding) throw std::runtime_error(kNoService);
    sdrplay_api_DeviceT devices[SDRPLAY_MAX_DEVICES];
    unsigned int count = 0;
    error = api_->get_devices(devices, &count, SDRPLAY_MAX_DEVICES);
    if (error != sdrplay_api_Success) {
        api_->unlock();
        throw std::runtime_error(error == sdrplay_api_ServiceNotResponding ? std::string(kNoService)
                                                                           : "The SDRplay API could not list its devices: " + api_->text(error));
    }
    if (count == 0) {
        api_->unlock();
        throw std::runtime_error(
            "No SDRplay radio found. Is it plugged in? If another program is using it, close that program.");
    }
    api_->device = devices[0];
    if (api_->device.hwVer == SDRPLAY_RSPduo_ID) {
        api_->device.tuner = sdrplay_api_Tuner_A;
        api_->device.rspDuoMode = sdrplay_api_RspDuoMode_Single_Tuner;
    }
    error = api_->select(&api_->device);
    api_->unlock();
    if (error != sdrplay_api_Success) throw std::runtime_error("The SDRplay radio could not be taken: " + api_->text(error) + ". Is another program using it?");
    api_->selected = true;

    error = api_->get_params(api_->device.dev, &api_->params);
    if (error != sdrplay_api_Success || api_->params == nullptr || api_->params->devParams == nullptr)
        throw std::runtime_error("The SDRplay API gave no device parameters: " + api_->text(error));
    api_->channel = api_->device.tuner == sdrplay_api_Tuner_B ? api_->params->rxChannelB : api_->params->rxChannelA;
    if (api_->channel == nullptr) throw std::runtime_error("The SDRplay API gave no channel parameters.");

    api_->params->devParams->fsFreq.fsHz = plan.adc_hz;
    sdrplay_api_TunerParamsT& tuner = api_->channel->tunerParams;
    tuner.rfFreq.rfHz = settings.center_hz;
    tuner.bwType = static_cast<sdrplay_api_Bw_MHzT>(plan.bandwidth_khz);
    tuner.ifType = sdrplay_api_IF_Zero;
    tuner.gain.gRdB = settings.if_gain_reduction_db;
    tuner.gain.LNAstate = static_cast<unsigned char>(settings.lna_state);
    sdrplay_api_ControlParamsT& control = api_->channel->ctrlParams;
    control.agc.enable = sdrplay_api_AGC_DISABLE;
    control.decimation.enable = plan.decimation > 1 ? 1 : 0;
    control.decimation.decimationFactor = static_cast<unsigned char>(plan.decimation);
    control.decimation.wideBandSignal = 1;  // the half-band filters, flat over the span, not the cheap box

    convert_.resize(4096);
    sdrplay_api_CallbackFnsT callbacks{};
    callbacks.StreamACbFn = SdrplayCallbacks::stream;
    callbacks.StreamBCbFn = SdrplayCallbacks::stream_b;
    callbacks.EventCbFn = SdrplayCallbacks::event;
    error = api_->init(api_->device.dev, &callbacks, this);
    if (error != sdrplay_api_Success) throw std::runtime_error("The SDRplay radio did not start: " + api_->text(error));
    api_->streaming = true;

    description_ = std::string("SDRplay ") + model_name(api_->device.hwVer) + " " + api_->device.SerNo;
}

SdrplaySource::~SdrplaySource() {
    api_.reset();  // the API's threads have stopped when this returns, so the recorder's producer is gone
    recorder_.stop();
}

void SdrplaySource::on_samples(const short* xi, const short* xq, unsigned int count, bool reset) {
    if (reset) resets_.fetch_add(1, std::memory_order_relaxed);
    received_.fetch_add(count, std::memory_order_relaxed);
    recorder_.push(xi, xq, count);
    if (convert_.size() < count) convert_.resize(count);  // once, at the first block larger than any before
    constexpr float kScale = 1.0f / 32768.0f;  // as WavIqReader, so a recording of this reads the same
    for (unsigned int i = 0; i < count; ++i) convert_[i] = Sample(xi[i] * kScale, xq[i] * kScale);
    const std::size_t taken = ring_.push(std::span<const Sample>(convert_.data(), count));
    if (taken < count) dropped_.fetch_add(count - taken, std::memory_order_relaxed);
}

void SdrplaySource::on_overload(bool detected) { overload_ = detected; }

void SdrplaySource::on_gain(double gain_db) {
    gain_db_ = gain_db;
    have_gain_ = true;
}

void SdrplaySource::on_lost(const std::string& message) {
    {
        std::lock_guard lock(mutex_);
        lost_message_ = message;
    }
    lost_ = true;
}

void SdrplaySource::set_gain(int if_gain_reduction_db, int lna_state) {
    SdrplaySettings next = settings_;
    next.if_gain_reduction_db = if_gain_reduction_db;
    next.lna_state = lna_state;
    check_sdrplay_settings(next);
    std::lock_guard lock(mutex_);
    api_->channel->tunerParams.gain.gRdB = if_gain_reduction_db;
    api_->channel->tunerParams.gain.LNAstate = static_cast<unsigned char>(lna_state);
    const sdrplay_api_ErrT error =
        api_->update(api_->device.dev, api_->device.tuner, sdrplay_api_Update_Tuner_Gr, sdrplay_api_Update_Ext1_None);
    if (error != sdrplay_api_Success) {
        api_->channel->tunerParams.gain.gRdB = settings_.if_gain_reduction_db;
        api_->channel->tunerParams.gain.LNAstate = static_cast<unsigned char>(settings_.lna_state);
        throw std::runtime_error("The radio refused that gain: " + api_->text(error) + " (a model has fewer LNA states in some bands).");
    }
    settings_ = next;
}

#endif  // KZ4AP_HAVE_SDRPLAY_HEADERS

RadioStatus SdrplaySource::radio_status() const {
    RadioStatus status;
    status.overload = overload_.load();
    if (have_gain_.load()) status.gain_db = gain_db_.load();
    status.resets = resets_.load();
    status.received = received_.load();
    std::lock_guard lock(mutex_);
    status.if_gain_reduction_db = settings_.if_gain_reduction_db;
    status.lna_state = settings_.lna_state;
    return status;
}

IqRead SdrplaySource::read(std::span<Sample> out, std::chrono::milliseconds timeout) {
    using Clock = std::chrono::steady_clock;
    const Clock::time_point deadline = Clock::now() + timeout;
    for (;;) {
        const std::size_t got = ring_.pop(out);
        if (got > 0) return IqRead{got, false};
        if (lost_.load()) {
            std::lock_guard lock(mutex_);
            throw std::runtime_error(lost_message_);
        }
        if (interrupted_.load() || Clock::now() >= deadline) return {};
        // The ring has no way to wake a reader without the producer's touching a lock: a short sleep, against a hop of 21.33 ms.
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
}

}  // namespace kz4ap
