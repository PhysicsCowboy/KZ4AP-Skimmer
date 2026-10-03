// Golden values for the bank decoder's tests: JSON files written by the prototype
// (python -m kz4ap_proto.golden, run in training/) under engine/tests/data/bank/. Test-only: the engine
// library does not link nlohmann/json.
#pragma once

#include <nlohmann/json.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

#ifndef KZ4AP_BANK_GOLDEN_DIR
#error "KZ4AP_BANK_GOLDEN_DIR must be defined by the build (engine/CMakeLists.txt)"
#endif

namespace kz4ap::test {

// Reads <KZ4AP_BANK_GOLDEN_DIR>/<name>.json, the directory being injected by the build so tests run
// from any working directory.
inline nlohmann::json load_golden(const std::string& name) {
    const std::filesystem::path path = std::filesystem::path(KZ4AP_BANK_GOLDEN_DIR) / (name + ".json");
    std::ifstream in(path);
    if (!in) throw std::runtime_error("cannot open golden file " + path.string());
    return nlohmann::json::parse(in);
}

// Decodes golden.py's _f64_base64: base64 (standard alphabet, "=" padding) of little-endian float64 values.
inline std::vector<double> decode_f64_base64(const std::string& text) {
    auto value = [](char c) -> int {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '+') return 62;
        if (c == '/') return 63;
        return -1;
    };
    std::vector<unsigned char> bytes;
    unsigned int acc = 0;
    int bits = 0;
    for (char c : text) {
        if (c == '=') break;
        const int v = value(c);
        if (v < 0) throw std::runtime_error("decode_f64_base64: not base64");
        acc = (acc << 6) | static_cast<unsigned int>(v);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            bytes.push_back(static_cast<unsigned char>((acc >> bits) & 0xFFu));
        }
    }
    if (bytes.size() % 8 != 0) throw std::runtime_error("decode_f64_base64: not whole float64 values");
    static_assert(sizeof(double) == 8);
    std::vector<double> out(bytes.size() / 8);
    for (std::size_t i = 0; i < out.size(); ++i) {
        std::uint64_t u = 0;
        for (int b = 7; b >= 0; --b) u = (u << 8) | bytes[i * 8 + static_cast<std::size_t>(b)];
        std::memcpy(&out[i], &u, sizeof u);
    }
    return out;
}

// Relative 1e-9 with an absolute floor of 1e-12 (for expected values at zero).
inline void expect_close(double actual, double expected) {
    EXPECT_NEAR(actual, expected, 1e-9 * std::max(std::abs(expected), 1e-12));
}

}  // namespace kz4ap::test
