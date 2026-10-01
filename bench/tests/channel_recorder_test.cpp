#include "channel_recorder.hpp"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <vector>

using namespace kz4ap;
using namespace kz4ap::bench;

namespace {

std::filesystem::path fresh_dir(const char* name) {
    const auto dir = std::filesystem::temp_directory_path() / name;
    std::filesystem::remove_all(dir);
    return dir;
}

}  // namespace

TEST(ChannelRecorder, WritesEachChannelsSamplesAndAManifest) {
    const auto dir = fresh_dir("kz4ap_recorder_writes");
    {
        ChannelRecorder rec(dir, 1500.0);
        rec.add_channel(1, 0, 1009.0);
        const std::vector<Sample> a{{1.0f, -2.0f}, {0.5f, 0.25f}};
        const std::vector<Sample> b{{3.0f, 4.0f}};
        rec.write({1, 0, 1000.0, a});
        rec.write({1, 2, 1000.0, b});
        rec.finish("x.wav", "x.json");
    }
    std::ifstream f(dir / "channel-1.c64", std::ios::binary);
    std::vector<float> v(7, -9.0f);
    f.read(reinterpret_cast<char*>(v.data()), 7 * sizeof(float));
    EXPECT_EQ(f.gcount(), static_cast<std::streamsize>(6 * sizeof(float)));  // exactly three samples
    v.resize(6);
    EXPECT_EQ(v, (std::vector<float>{1.0f, -2.0f, 0.5f, 0.25f, 3.0f, 4.0f}));
    std::ifstream m(dir / "channels.json");
    const auto j = nlohmann::json::parse(m);
    EXPECT_EQ(j.at("recording"), "x.wav");
    EXPECT_EQ(j.at("labels"), "x.json");
    EXPECT_DOUBLE_EQ(j.at("sample_rate_hz").get<double>(), 1500.0);
    const auto& c = j.at("channels").at(0);
    EXPECT_EQ(c.at("track_id"), 1);
    EXPECT_EQ(c.at("label_index"), 0);
    EXPECT_DOUBLE_EQ(c.at("label_freq_hz").get<double>(), 1009.0);
    EXPECT_DOUBLE_EQ(c.at("center_hz").get<double>(), 1000.0);
    EXPECT_EQ(c.at("first_sample_index"), 0);
    EXPECT_EQ(c.at("samples"), 3);
    EXPECT_EQ(c.at("file"), "channel-1.c64");
    f.close();  // Windows cannot remove a file that is still open
    m.close();
    std::filesystem::remove_all(dir);
}

TEST(ChannelRecorder, RejectsGapsUnknownChannelsAndMovedCenters) {
    const auto dir = fresh_dir("kz4ap_recorder_rejects");
    ChannelRecorder rec(dir, 1500.0);
    rec.add_channel(1, 0, 0.0);
    const std::vector<Sample> a(4);
    rec.write({1, 0, 0.0, a});
    EXPECT_THROW(rec.write({1, 5, 0.0, a}), std::runtime_error);   // samples 4 and 5 missing
    EXPECT_THROW(rec.write({2, 0, 0.0, a}), std::runtime_error);   // no such channel
    EXPECT_THROW(rec.write({1, 4, 23.4, a}), std::runtime_error);  // the channel's center moved
    rec.finish("x.wav", "x.json");  // closes the file, so the directory can go
    std::filesystem::remove_all(dir);
}

TEST(ChannelRecorder, RecordsDetectorTracksWithOpeningClosingAndAnchors) {
    const auto dir = fresh_dir("kz4ap_recorder_detector");
    {
        ChannelRecorder rec(dir, 1500.0);
        rec.open_track(7, 1011.0);
        const std::vector<Sample> a(32);
        ChannelBlock b{7, 3000, 1000.0, a};
        b.anchor_hz = 1011.0;
        rec.write(b);
        b.first_index = 3032;
        b.anchor_hz = 1012.5;
        rec.write(b);
        rec.close_track(7);
        EXPECT_THROW(rec.write(b), std::runtime_error);  // a closed track takes no more blocks
        rec.open_track(9, -500.0);                         // opened, never fed: 0 samples
        rec.finish("x.wav", "");
    }
    std::ifstream m(dir / "channels.json");
    const auto j = nlohmann::json::parse(m);
    m.close();  // Windows: an open file keeps the directory from being removed
    const auto& c = j.at("channels").at(0);
    EXPECT_EQ(c.at("track_id"), 7);
    EXPECT_TRUE(c.at("label_index").is_null());
    EXPECT_DOUBLE_EQ(c.at("birth_freq_hz").get<double>(), 1011.0);
    EXPECT_EQ(c.at("first_sample_index"), 3000);
    EXPECT_DOUBLE_EQ(c.at("open_s").get<double>(), 2.0);
    EXPECT_DOUBLE_EQ(c.at("close_s").get<double>(), 3064.0 / 1500.0);
    EXPECT_EQ(c.at("samples"), 64);
    EXPECT_EQ(c.at("anchors"), nlohmann::json::parse("[[3000, 1011.0], [3032, 1012.5]]"));
    EXPECT_TRUE(j.at("channels").at(1).at("close_s").is_null());
    std::filesystem::remove_all(dir);
}
