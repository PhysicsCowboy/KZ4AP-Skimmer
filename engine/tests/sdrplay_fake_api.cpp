// A stand-in for the SDRplay API's library, built only where its headers are, so that SdrplaySource's sequence and its error
// messages can be tested with no service and no radio: the same entry points by the same names. KZ4AP_FAKE_SDRPLAY in the
// environment picks the machine it pretends to be, when sdrplay_api_Open is called: "no-service", "no-device",
// "old-version", "busy", or anything else for a radio that delivers samples. fake_* are for the tests.
#define _SDRPLAY_DLL_QUALIFIER __declspec(dllexport)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <sdrplay_api.h>

#include <atomic>
#include <chrono>
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
    if (scenario() == "no-device") {
        *count = 0;
        return sdrplay_api_Success;
    }
    *count = 1;
    std::memset(&devices[0], 0, sizeof devices[0]);
    std::strcpy(devices[0].SerNo, "FAKE0001");
    devices[0].hwVer = SDRPLAY_RSP1A_ID;
    devices[0].tuner = sdrplay_api_Tuner_A;
    devices[0].valid = 1;
    devices[0].dev = reinterpret_cast<HANDLE>(1);
    return sdrplay_api_Success;
}

sdrplay_api_ErrT sdrplay_api_SelectDevice(sdrplay_api_DeviceT*) {
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

sdrplay_api_ErrT sdrplay_api_Init(HANDLE, sdrplay_api_CallbackFnsT* functions, void* cb_context) {
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
