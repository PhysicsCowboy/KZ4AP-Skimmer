#include "recording_name.hpp"

#include <cstdio>

namespace kz4ap::app {

std::string recording_file_name(double center_hz, int sample_rate, const std::string& stamp) {
    char text[64];
    std::snprintf(text, sizeof text, "kz4ap_%.3fkHz_%gkHz_", center_hz / 1000.0, sample_rate / 1000.0);
    return std::string(text) + stamp + ".wav";
}

}  // namespace kz4ap::app
