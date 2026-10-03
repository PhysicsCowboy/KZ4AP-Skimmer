// Golden values for the bank decoder's tests: JSON files written by the prototype
// (python -m kz4ap_proto.golden, run in training/) under engine/tests/data/bank/. Test-only: the engine
// library does not link nlohmann/json.
#pragma once

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

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

}  // namespace kz4ap::test
