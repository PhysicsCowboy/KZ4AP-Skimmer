// A stand-in for the SDRplay API's library, built only where its headers are, so that SdrplaySource's sequence and its error
// messages can be tested with no service and no radio: the same entry points by the same names. KZ4AP_FAKE_SDRPLAY in the
// environment picks the machine it pretends to be, when sdrplay_api_Open is called: "no-service", "no-device",
// "old-version", "busy", "list-error" (GetDevices fails), "one-device" (FAKE0001 alone), "duo" (FAKE0002 is an RSPduo), or
// anything else for two radios, FAKE0001 (an RSP1A) listed first and FAKE0002 (an RSPdx) second, that deliver samples.
// fake_* are for the tests.
#define _SDRPLAY_DLL_QUALIFIER __declspec(dllexport)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <sdrplay_api.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {

std::string scenario() {
    char text[64] = {};
    GetEnvironmentVariableA("KZ4AP_FAKE_SDRPLAY", text, sizeof text);
    return text;
}

sdrplay_api_DevParamsT dev_params{};
sdrplay_api_RxChannelParamsT channel{};
sdrplay_api_DeviceParamsT device_params{&dev_params, &channel, nullptr};
sdrplay_api_CallbackFnsT callbacks{};
void* context = nullptr;
std::thread streamer;
std::atomic<bool> streaming{false};
std::atomic<int> gain_updates{0};
std::atomic<int> overload_acks{0};
std::atomic<int> uninits{0};
std::atomic<int> releases{0};
std::atomic<int> closes{0};
std::atomic<int> locked{0};
std::atomic<int> listings{0};
std::atomic<int> selects{0};
std::atomic<int> inits{0};
// What the last SelectDevice was given, and the last Init's device handle (1 for FAKE0001, 2 for FAKE0002).
char selected_serial[SDRPLAY_MAX_SER_NO_LEN] = {};
int selected_tuner = 0;
int selected_duo_mode = 0;
std::intptr_t init_device = 0;

void fill(sdrplay_api_DeviceT& device, const char* serial, unsigned char hardware, std::intptr_t handle) {
    std::memset(&device, 0, sizeof device);
    std::strcpy(device.SerNo, serial);
    device.hwVer = hardware;
    device.tuner = sdrplay_api_Tuner_A;
    device.valid = 1;
    device.dev = reinterpret_cast<HANDLE>(handle);
}

void stream_loop() {
    constexpr unsigned int kBlock = 1000;
    std::vector<short> xi(kBlock), xq(kBlock);
    short next = 0;
    bool first = true;
    sdrplay_api_StreamCbParamsT params{};
    while (streaming) {
        for (unsigned int i = 0; i < kBlock; ++i) {
            xi[i] = next++;
            xq[i] = static_cast<short>(-xi[i]);
        }
        callbacks.StreamACbFn(xi.data(), xq.data(), &params, kBlock, first ? 1 : 0, context);
        first = false;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

}  // namespace

extern "C" {

sdrplay_api_ErrT sdrplay_api_Open(void) {
    if (scenario() == "no-service") return sdrplay_api_ServiceNotResponding;
    return sdrplay_api_Success;
}

sdrplay_api_ErrT sdrplay_api_Close(void) {
    ++closes;
    return sdrplay_api_Success;
}

sdrplay_api_ErrT sdrplay_api_ApiVersion(float* version) {
    *version = scenario() == "old-version" ? 3.0f : SDRPLAY_API_VERSION;
    return sdrplay_api_Success;
}

sdrplay_api_ErrT sdrplay_api_LockDeviceApi(void) {
    ++locked;
    return sdrplay_api_Success;
}

sdrplay_api_ErrT sdrplay_api_UnlockDeviceApi(void) {
    --locked;
    return sdrplay_api_Success;
}

sdrplay_api_ErrT sdrplay_api_GetDevices(sdrplay_api_DeviceT* devices, unsigned int* count, unsigned int) {
    ++listings;
    const std::string which = scenario();
    *count = 0;
    if (which == "no-device") return sdrplay_api_Success;
    if (which == "list-error") return sdrplay_api_Fail;
    fill(devices[0], "FAKE0001", SDRPLAY_RSP1A_ID, 1);
    if (which == "one-device") {
        *count = 1;
        return sdrplay_api_Success;
    }
    if (which == "duo") {
        // As the API lists a free RSPduo: both tuners, and the modes it may be taken in.
        fill(devices[1], "FAKE0002", SDRPLAY_RSPduo_ID, 2);
        devices[1].tuner = sdrplay_api_Tuner_Both;
        devices[1].rspDuoMode = static_cast<sdrplay_api_RspDuoModeT>(sdrplay_api_RspDuoMode_Single_Tuner | sdrplay_api_RspDuoMode_Dual_Tuner |
                                                                     sdrplay_api_RspDuoMode_Master);
    } else {
        fill(devices[1], "FAKE0002", SDRPLAY_RSPdx_ID, 2);
    }
    *count = 2;
    return sdrplay_api_Success;
}

sdrplay_api_ErrT sdrplay_api_SelectDevice(sdrplay_api_DeviceT* device) {
    ++selects;
    std::memcpy(selected_serial, device->SerNo, sizeof selected_serial);
    selected_tuner = static_cast<int>(device->tuner);
    selected_duo_mode = static_cast<int>(device->rspDuoMode);
    return scenario() == "busy" ? sdrplay_api_Fail : sdrplay_api_Success;
}

sdrplay_api_ErrT sdrplay_api_ReleaseDevice(sdrplay_api_DeviceT*) {
    ++releases;
    return sdrplay_api_Success;
}

const char* sdrplay_api_GetErrorString(sdrplay_api_ErrT error) { return error == sdrplay_api_Fail ? "Fail" : "error"; }

sdrplay_api_ErrT sdrplay_api_GetDeviceParams(HANDLE, sdrplay_api_DeviceParamsT** params) {
    *params = &device_params;
    return sdrplay_api_Success;
}

sdrplay_api_ErrT sdrplay_api_Init(HANDLE device, sdrplay_api_CallbackFnsT* functions, void* cb_context) {
    ++inits;
    init_device = reinterpret_cast<std::intptr_t>(device);
    callbacks = *functions;
    context = cb_context;
    streaming = true;
    streamer = std::thread(stream_loop);
    return sdrplay_api_Success;
}

sdrplay_api_ErrT sdrplay_api_Uninit(HANDLE) {
    ++uninits;
    streaming = false;
    if (streamer.joinable()) streamer.join();
    return sdrplay_api_Success;
}

sdrplay_api_ErrT sdrplay_api_Update(HANDLE, sdrplay_api_TunerSelectT, sdrplay_api_ReasonForUpdateT reason,
                                    sdrplay_api_ReasonForUpdateExtension1T) {
    if (reason == sdrplay_api_Update_Tuner_Gr) {
        ++gain_updates;
        if (channel.tunerParams.gain.LNAstate > 6) return sdrplay_api_GainUpdateError;  // as an RSP1A below 60 MHz
        sdrplay_api_EventParamsT event{};
        event.gainParams.currGain = 60.0 - channel.tunerParams.gain.gRdB;
        callbacks.EventCbFn(sdrplay_api_GainChange, sdrplay_api_Tuner_A, &event, context);
    } else if (reason == sdrplay_api_Update_Ctrl_OverloadMsgAck) {
        ++overload_acks;
    }
    return sdrplay_api_Success;
}

// For the tests.
__declspec(dllexport) int fake_gain_updates() { return gain_updates; }
__declspec(dllexport) int fake_overload_acks() { return overload_acks; }
__declspec(dllexport) int fake_uninits() { return uninits; }
__declspec(dllexport) int fake_releases() { return releases; }
__declspec(dllexport) int fake_closes() { return closes; }
__declspec(dllexport) int fake_locked() { return locked; }
__declspec(dllexport) int fake_listings() { return listings; }
__declspec(dllexport) int fake_selects() { return selects; }
__declspec(dllexport) int fake_inits() { return inits; }
__declspec(dllexport) const char* fake_selected_serial() { return selected_serial; }
__declspec(dllexport) int fake_selected_tuner() { return selected_tuner; }
__declspec(dllexport) int fake_selected_duo_mode() { return selected_duo_mode; }
__declspec(dllexport) int fake_init_device() { return static_cast<int>(init_device); }
__declspec(dllexport) double fake_center_hz() { return channel.tunerParams.rfFreq.rfHz; }
__declspec(dllexport) double fake_adc_hz() { return dev_params.fsFreq.fsHz; }
__declspec(dllexport) int fake_decimation() { return channel.ctrlParams.decimation.enable ? channel.ctrlParams.decimation.decimationFactor : 1; }
__declspec(dllexport) int fake_bandwidth_khz() { return static_cast<int>(channel.tunerParams.bwType); }
__declspec(dllexport) int fake_if_reduction_db() { return channel.tunerParams.gain.gRdB; }
__declspec(dllexport) int fake_lna_state() { return channel.tunerParams.gain.LNAstate; }
__declspec(dllexport) int fake_agc_enable() { return static_cast<int>(channel.ctrlParams.agc.enable); }
// 0: overload, 1: the overload corrected, 2: the radio unplugged. From the caller's thread, as the API's threads do it.
__declspec(dllexport) void fake_event(int which) {
    sdrplay_api_EventParamsT event{};
    if (which <= 1) {
        event.powerOverloadParams.powerOverloadChangeType = which == 0 ? sdrplay_api_Overload_Detected : sdrplay_api_Overload_Corrected;
        callbacks.EventCbFn(sdrplay_api_PowerOverloadChange, sdrplay_api_Tuner_A, &event, context);
    } else {
        callbacks.EventCbFn(sdrplay_api_DeviceRemoved, sdrplay_api_Tuner_A, &event, context);
    }
}
}
