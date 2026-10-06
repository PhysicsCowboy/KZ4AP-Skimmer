// Golden values for the bank decoder's tests: JSON files written by the prototype
// (python -m kz4ap_proto.golden, run in training/) under engine/tests/data/bank/. Test-only: the engine
// library does not link nlohmann/json.
#pragma once

#include "kz4ap/bank/filters.hpp"

#include <nlohmann/json.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <complex>
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

// <KZ4AP_BANK_GOLDEN_DIR>/<file>: a complex64 stream (golden.py's STREAMS: interleaved little-endian float32,
// real and imaginary, FS) of exactly `samples` samples, widened to double (exact: the prototype ran on the same
// float32-rounded values).
inline std::vector<std::complex<double>> read_c64(const std::string& file, std::size_t samples) {
    const std::filesystem::path path = std::filesystem::path(KZ4AP_BANK_GOLDEN_DIR) / file;
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot open golden stream " + path.string());
    static_assert(sizeof(float) == 4);
    std::vector<unsigned char> bytes(8 * samples);
    in.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (in.gcount() != static_cast<std::streamsize>(bytes.size()) || in.peek() != std::ifstream::traits_type::eof())
        throw std::runtime_error("golden stream " + path.string() + ": not " + std::to_string(samples) + " samples");
    auto f32 = [&](std::size_t at) {  // little-endian, whatever the host's byte order
        std::uint32_t v = 0;
        for (int b = 3; b >= 0; --b) v = (v << 8) | bytes[at + static_cast<std::size_t>(b)];
        float x;
        std::memcpy(&x, &v, sizeof x);
        return static_cast<double>(x);
    };
    std::vector<std::complex<double>> u(samples);
    for (std::size_t i = 0; i < samples; ++i) u[i] = {f32(8 * i), f32(8 * i + 4)};
    return u;
}

// A golden file's complex stream u (FS): its "stream_file" of "samples" samples (noise.json).
inline std::vector<std::complex<double>> stream(const nlohmann::json& g) {
    return read_c64(g.at("stream_file").get<std::string>(), g.at("samples").get<std::size_t>());
}

// The prototype's P: np.abs(boxcar(u, N_k)) ** 2, stored as float32 (ChannelDecoder.run), FS^2; rows are the
// branches n, columns the samples of u.
inline kz4ap::bank::Matrix powers(const std::vector<std::complex<double>>& u, const std::vector<int>& n) {
    kz4ap::bank::Matrix P;
    P.rows = static_cast<int>(n.size());
    P.cols = static_cast<int>(u.size());
    P.v.resize(static_cast<std::size_t>(P.rows) * u.size());
    for (int k = 0; k < P.rows; ++k) {
        const auto v = kz4ap::bank::boxcar(u, n[static_cast<std::size_t>(k)]);
        for (int i = 0; i < P.cols; ++i) P.at(k, i) = kz4ap::bank::boxcar_power_f32(v[static_cast<std::size_t>(i)]);
    }
    return P;
}

// golden_channel's stream `name` (channel.json "streams": its "file", channel_stream_<name>.c64, of "samples"
// samples); FS.
inline std::vector<std::complex<double>> channel_stream(const nlohmann::json& channel, const std::string& name) {
    const auto& s = channel.at("streams").at(name);
    return read_c64(s.at("file").get<std::string>(), s.at("samples").get<std::size_t>());
}

// The prototype's configuration, which the golden files were written with: the defaults with Plan B's B4b and B4d
// changes undone (stage 1's 20 ms guard margin and its mask-bias table; the re-key clocks of stage 1, both B4d switches
// off; the re-key wait in keyed time, B4e's wait in marks off). Stage 1's re-key time constants in seconds are passed separately (bank::fixed_timing, or the overrides
// rekey_after_s and rekey_timeout_s); its periodicity windows, 2, 5 and 10 s, are the default again since B4d.
inline kz4ap::bank::BankConfig stage1_config() {
    kz4ap::bank::BankConfig cfg;
    cfg.guard_margin_s = kz4ap::bank::kStage1GuardMarginS;
    cfg.mask_bias = kz4ap::bank::kStage1MaskBias;
    cfg.rekey_clear_moves_stretch = false;
    cfg.rekey_timeout_from_first_mark = false;
    cfg.rekey_wait_in_marks = false;
    return cfg;
}

}  // namespace kz4ap::test
