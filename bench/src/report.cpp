#include "report.hpp"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace kz4ap::bench {

double detection_recall(const Score& s) {
    return s.scored == 0 ? 1.0 : static_cast<double>(s.detected) / static_cast<double>(s.scored);
}

nlohmann::json score_json(const Labels& labels, const Score& s, const std::map<std::uint32_t, double>& tracked_freq_hz) {
    nlohmann::json signals = nlohmann::json::array();
    for (std::size_t i = 0; i < s.signals.size(); ++i) {
        const auto& sig = s.signals[i];
        nlohmann::json per_transmission = nlohmann::json::array();
        for (const auto& ts : sig.transmissions) {
            per_transmission.push_back({{"symbols", ts.symbols}, {"edits", ts.edits},
                                        {"first_word_symbols", ts.first_word_symbols},
                                        {"first_word_edits", ts.first_word_edits}});
        }
        nlohmann::json tracked;  // null
        if (sig.track_id) {
            if (const auto it = tracked_freq_hz.find(*sig.track_id); it != tracked_freq_hz.end()) tracked = it->second;
        }
        signals.push_back({{"index", i},
                           {"freq_offset_hz", sig.label.freq_offset_hz},
                           {"snr_db", sig.label.snr_db},
                           {"wpm", sig.label.wpm},
                           {"scored", sig.label.score},
                           {"reference", normalize_text(sig.label.text)},
                           {"decoded", sig.decoded},
                           {"track_id", sig.track_id ? nlohmann::json(*sig.track_id) : nlohmann::json()},
                           {"tracked_freq_hz", tracked},
                           {"cer", sig.cer},
                           {"symbols", sig.symbols},
                           {"edits", sig.edits},
                           {"chars", sig.chars},
                           {"char_edits", sig.char_edits},
                           {"spaces", sig.spaces},
                           {"space_edits", sig.space_edits},
                           {"first_word_symbols", sig.first_word_symbols},
                           {"first_word_edits", sig.first_word_edits},
                           {"nospace_symbols", sig.nospace_symbols},
                           {"nospace_edits", sig.nospace_edits},
                           {"transmissions", per_transmission}});
    }
    return {{"cer", s.cer},
            {"char_cer", s.char_cer},
            {"space_error_rate", s.space_error_rate},
            {"first_word_cer", s.first_word_cer},
            {"nospace_cer", s.nospace_cer},
            {"detected", s.detected},
            {"labels", labels.signals.size()},
            {"scored", s.scored},
            {"detection_recall", detection_recall(s)},
            {"false_tracks", s.false_tracks},
            {"signals", signals}};
}

void print_score(const Score& s) {
    for (const auto& sig : s.signals) {
        std::printf("label %+10.1f Hz  CER %5.3f  %s%s\n", sig.label.freq_offset_hz, sig.cer,
                    sig.track_id ? "" : "(not detected)", sig.label.score ? "" : " (not scored)");
    }
    std::printf("CER %.4f (characters %.4f, word spaces %.4f, first words %.4f, without spaces %.4f), "
                "detected %zu of %zu, %zu false tracks\n",
                s.cer, s.char_cer, s.space_error_rate, s.first_word_cer, s.nospace_cer, s.detected, s.scored,
                s.false_tracks);
}

void add_immediate_score(nlohmann::json& score, const Score& immediate) {
    score["cer_immediate"] = immediate.cer;
    auto& signals = score.at("signals");
    if (signals.size() != immediate.signals.size()) throw std::logic_error("immediate score of other labels");
    for (std::size_t i = 0; i < signals.size(); ++i) {
        const auto& sig = immediate.signals[i];
        signals[i]["decoded_immediate"] = sig.decoded;
        signals[i]["edits_immediate"] = sig.edits;
        signals[i]["cer_immediate"] = sig.cer;
    }
}

void TrackText::apply(const std::vector<kz4ap::DecodedSymbol>& chars,
                      const std::vector<kz4ap::TextCorrection>& corrections) {
    for (const auto& c : chars) {
        final_.push_back(c.text);
        immediate_ += c.text;
    }
    for (const auto& k : corrections) {
        final_.resize(std::min(k.from_index, final_.size()));  // never indexes past the published characters
        for (const auto& c : k.chars) final_.push_back(c.text);
        corrections_.push_back({k.t_s, k.reach_s, k.reason});
    }
}

nlohmann::json corrections_json(const std::vector<TrackText::Received>& corrections) {
    nlohmann::json a = nlohmann::json::array();
    for (const auto& c : corrections) a.push_back({{"t_s", c.t_s}, {"reach_s", c.reach_s}, {"reason", c.reason}});
    return a;
}

std::string TrackText::final_text() const {
    std::string s;
    for (const auto& t : final_) s += t;
    return s;
}

DecodedTexts parse_decoded_texts(const std::string& json_text) {
    try {
        const auto j = nlohmann::json::parse(json_text);
        DecodedTexts d;
        d.front_end = j.at("front_end").get<std::string>();
        d.recording = j.value("recording", std::string());
        const bool has_texts = j.contains("texts"), has_tracks = j.contains("tracks");
        if (has_texts == has_tracks) throw std::runtime_error("bad decoded-text file: give either texts or tracks");
        if (has_texts) {
            d.texts = j.at("texts").get<std::vector<std::string>>();
        } else {
            d.detector = true;
            for (const auto& t : j.at("tracks")) {
                const double f = t.at("freq_hz").get<double>();
                d.tracks.push_back({t.at("id").get<std::uint32_t>(), f, t.at("text").get<std::string>(),
                                    t.value("last_freq_hz", f)});
            }
        }
        return d;
    } catch (const nlohmann::json::exception& e) {
        throw std::runtime_error(std::string("bad decoded-text file: ") + e.what());
    }
}

DecodedTexts load_decoded_texts(const std::filesystem::path& path) {
    std::ifstream f(path);
    if (!f) throw std::runtime_error("cannot open " + path.string());
    std::ostringstream text;
    text << f.rdbuf();
    return parse_decoded_texts(text.str());
}

std::vector<DecodedTrack> tracks_for(const Labels& labels, const DecodedTexts& decoded) {
    if (decoded.texts.size() != labels.signals.size()) {
        throw std::runtime_error("decoded text for " + std::to_string(decoded.texts.size()) + " signals, labels for " +
                                 std::to_string(labels.signals.size()));
    }
    std::vector<DecodedTrack> tracks;
    for (std::size_t i = 0; i < labels.signals.size(); ++i) {
        const double f = labels.signals[i].freq_offset_hz;
        tracks.push_back({static_cast<std::uint32_t>(i + 1), f, decoded.texts[i], f});
    }
    return tracks;
}

}  // namespace kz4ap::bench
