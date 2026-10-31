// The name the Record button suggests. The project's WAV form does not say where the radio was tuned, so the name does.
#pragma once

#include <string>

namespace kz4ap::app {

// For example "kz4ap_7030.000kHz_192kHz_20261030-213000.wav". center_hz: the radio's tuned frequency, Hz; sample_rate:
// complex samples/s; stamp: the time, in a form a file name may hold.
std::string recording_file_name(double center_hz, int sample_rate, const std::string& stamp);

}  // namespace kz4ap::app
