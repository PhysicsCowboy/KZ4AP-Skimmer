#include "labels.hpp"

#include <nlohmann/json.hpp>

#include <fstream>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace kz4ap::bench {

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
                                                    tx.at("end_s").get<double>()});
                }
            }
            signal.score = s.value("score", true);
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
