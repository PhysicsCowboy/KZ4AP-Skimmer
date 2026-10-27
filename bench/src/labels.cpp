#include "labels.hpp"

#include <nlohmann/json.hpp>

#include <fstream>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace kz4ap::bench {

namespace {

// A number that the generator may leave out, or write as null (an optional it did not set): its member of `object`, or
// `fallback`. Anything else is a bad file (JSON's true and false are not numbers here).
double number_or(const nlohmann::json& object, const char* key, double fallback) {
    const auto it = object.find(key);
    if (it == object.end() || it->is_null()) return fallback;
    if (!it->is_number()) throw std::runtime_error(std::string("bad labels file: ") + key + " must be a number");
    return it->get<double>();
}

}  // namespace

Labels parse_labels(const std::string& json_text) {
    try {
        const auto j = nlohmann::json::parse(json_text);
        Labels labels;
        labels.sample_rate = j.at("sample_rate").get<int>();
        labels.duration_s = j.at("duration_s").get<double>();
        for (const auto& s : j.at("signals")) {
            LabeledSignal signal{s.at("text").get<std::string>(), s.at("freq_offset_hz").get<double>(),
                                 s.at("wpm").get<double>(),       s.at("snr_db").get<double>(),
                                 s.at("start_s").get<double>(),   s.at("end_s").get<double>()};
            if (const auto t = s.find("transmissions"); t != s.end()) {
                for (const auto& tx : *t) {
                    signal.transmissions.push_back({tx.at("text").get<std::string>(), tx.at("start_s").get<double>(),
                                                    tx.at("end_s").get<double>(), number_or(tx, "offset_hz", 0.0)});
                }
            }
            signal.score = s.value("score", true);
            signal.drift_hz_per_s = number_or(s, "drift_hz_per_s", signal.drift_hz_per_s);
            signal.fading_hz = number_or(s, "fading_hz", signal.fading_hz);
            signal.fading_hz_written = s.contains("fading_hz") && !s.at("fading_hz").is_null();
            signal.per_station = s.contains("qso_index");
            signal.imbalance_dits = number_or(s, "imbalance_dits", signal.imbalance_dits);
            if (const auto it = s.find("keying"); it != s.end()) signal.keying = it->get<std::string>();
            if (const auto it = s.find("wpm_end"); it != s.end() && !it->is_null()) {
                if (!it->is_number()) throw std::runtime_error("bad labels file: wpm_end must be a number or null");
                signal.wpm_end = it->get<double>();
            }
            if (const auto it = s.find("senders"); it != s.end() && it->is_array())
                signal.sender_count = static_cast<int>(it->size());
            labels.signals.push_back(std::move(signal));
        }
        return labels;
    } catch (const nlohmann::json::exception& e) {
        throw std::runtime_error(std::string("bad labels file: ") + e.what());
    }
}

Labels load_labels(const std::filesystem::path& path) {
    std::ifstream f(path);
    if (!f) throw std::runtime_error("cannot open " + path.string());
    std::ostringstream text;
    text << f.rdbuf();
    return parse_labels(text.str());
}

}  // namespace kz4ap::bench
