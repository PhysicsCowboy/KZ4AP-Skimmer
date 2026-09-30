#include "channel_recorder.hpp"

#include <nlohmann/json.hpp>

#include <bit>
#include <stdexcept>
#include <system_error>
#include <utility>

namespace kz4ap::bench {

static_assert(std::endian::native == std::endian::little, "channel files are written little-endian");
static_assert(sizeof(Sample) == 2 * sizeof(float), "a sample is two float32 values, I then Q");

ChannelRecorder::ChannelRecorder(std::filesystem::path dir, double sample_rate_hz)
    : dir_(std::move(dir)), rate_(sample_rate_hz) {
    std::error_code ec;
    std::filesystem::create_directories(dir_, ec);
    if (ec) throw std::runtime_error("cannot create " + dir_.string() + ": " + ec.message());
}

void ChannelRecorder::add_channel(std::uint32_t track_id, std::size_t label_index, double label_freq_hz) {
    Channel c;
    c.label_index = label_index;
    c.label_freq_hz = label_freq_hz;
    c.file = "channel-" + std::to_string(track_id) + ".c64";
    c.out.open(dir_ / c.file, std::ios::binary | std::ios::trunc);
    if (!c.out) throw std::runtime_error("cannot open " + (dir_ / c.file).string());
    channels_.emplace(track_id, std::move(c));
}

void ChannelRecorder::write(const ChannelBlock& block) {
    const auto it = channels_.find(block.track_id);
    if (it == channels_.end()) throw std::runtime_error("block for unknown channel " + std::to_string(block.track_id));
    Channel& c = it->second;
    if (c.first_index && block.first_index != *c.first_index + c.samples) {
        throw std::runtime_error("channel " + std::to_string(block.track_id) + ": block at sample " +
                                 std::to_string(block.first_index) + " does not follow sample " +
                                 std::to_string(*c.first_index + c.samples - 1));
    }
    if (c.center_hz && *c.center_hz != block.center_hz) {
        throw std::runtime_error("channel " + std::to_string(block.track_id) + " moved its center");
    }
    if (!c.first_index) c.first_index = block.first_index;
    c.center_hz = block.center_hz;
    c.out.write(reinterpret_cast<const char*>(block.samples.data()),
                static_cast<std::streamsize>(block.samples.size() * sizeof(Sample)));
    if (!c.out) throw std::runtime_error("cannot write " + (dir_ / c.file).string());
    c.samples += block.samples.size();
}

void ChannelRecorder::finish(const std::string& recording, const std::string& labels) {
    nlohmann::json j;
    j["recording"] = recording;
    j["labels"] = labels;
    j["sample_rate_hz"] = rate_;
    j["format"] = "complex64: float32 I then Q, little-endian (numpy dtype <c8), FS";
    j["channels"] = nlohmann::json::array();
    for (auto& [id, c] : channels_) {
        c.out.close();
        if (!c.out) throw std::runtime_error("cannot close " + (dir_ / c.file).string());
        j["channels"].push_back({{"track_id", id},
                                 {"label_index", c.label_index},
                                 {"label_freq_hz", c.label_freq_hz},
                                 {"center_hz", c.center_hz ? nlohmann::json(*c.center_hz) : nlohmann::json()},
                                 {"first_sample_index", c.first_index ? *c.first_index : 0},
                                 {"samples", c.samples},
                                 {"file", c.file}});
    }
    std::ofstream f(dir_ / "channels.json");
    f << j.dump(2) << '\n';
    f.close();
    if (!f) throw std::runtime_error("cannot write " + (dir_ / "channels.json").string());
}

}  // namespace kz4ap::bench
