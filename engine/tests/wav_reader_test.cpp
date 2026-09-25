#include "kz4ap/wav_reader.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

using namespace kz4ap;

namespace {

void put_u16(std::string& b, std::uint16_t v) {
    b.push_back(static_cast<char>(v & 0xFF));
    b.push_back(static_cast<char>(v >> 8));
}

void put_u32(std::string& b, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) b.push_back(static_cast<char>((v >> (8 * i)) & 0xFF));
}

struct WavSpec {
    int rate = 48000;
    int channels = 2;
    int bits = 16;
    std::vector<std::int16_t> samples;       // interleaved
    bool extra_chunk = false;                // odd-sized LIST chunk before data
    std::optional<std::uint32_t> data_size;  // override the data chunk's declared size
    // If set, insert an unknown "JUNK" chunk before data whose declared size lies
    // (only a handful of bytes are actually written), to probe the chunk-skip math.
    std::optional<std::uint32_t> huge_chunk_size;
    bool extensible = false;    // write a WAVE_FORMAT_EXTENSIBLE fmt chunk
    std::uint16_t sub_format = 0x0001;  // first two bytes of the sub-format GUID
};

std::string wav_bytes(const WavSpec& s) {
    std::string fmt;
    put_u16(fmt, s.extensible ? std::uint16_t{0xFFFE} : std::uint16_t{1});
    put_u16(fmt, static_cast<std::uint16_t>(s.channels));
    put_u32(fmt, static_cast<std::uint32_t>(s.rate));
    put_u32(fmt, static_cast<std::uint32_t>(s.rate * s.channels * s.bits / 8));
    put_u16(fmt, static_cast<std::uint16_t>(s.channels * s.bits / 8));
    put_u16(fmt, static_cast<std::uint16_t>(s.bits));
    if (s.extensible) {
        put_u16(fmt, 22);                              // cbSize
        put_u16(fmt, static_cast<std::uint16_t>(s.bits));  // valid bits per sample
        put_u32(fmt, 3);                               // channel mask (arbitrary)
        put_u16(fmt, s.sub_format);                     // first two bytes of the GUID
        fmt += std::string(14, '\0');                  // remaining 14 GUID bytes (unused)
    }
    std::string data;
    for (auto v : s.samples) put_u16(data, static_cast<std::uint16_t>(v));

    std::string body = "WAVE";
    body += "fmt ";
    put_u32(body, static_cast<std::uint32_t>(fmt.size()));
    body += fmt;
    if (s.extra_chunk) {
        body += "LIST";
        put_u32(body, 3);
        body += "abc";
        body.push_back('\0');  // pad byte for the odd size
    }
    if (s.huge_chunk_size) {
        body += "JUNK";
        put_u32(body, *s.huge_chunk_size);
        // The declared size lies: only a few bytes actually follow. A correct
        // reader must attempt to skip the declared (huge) size and run off the
        // end of the file rather than parsing these bytes as a chunk header.
        body += std::string(8, '\x7F');
    }
    body += "data";
    put_u32(body, s.data_size.value_or(static_cast<std::uint32_t>(data.size())));
    body += data;

    std::string file = "RIFF";
    put_u32(file, static_cast<std::uint32_t>(body.size()));
    return file + body;
}

class WavIqReaderTest : public ::testing::Test {
protected:
    std::filesystem::path path_;

    void SetUp() override {
        const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
        path_ = std::filesystem::temp_directory_path() /
                (std::string("kz4ap_") + info->name() + ".wav");
    }
    void TearDown() override {
        std::error_code ec;
        std::filesystem::remove(path_, ec);
    }
    void write(const WavSpec& spec) {
        const auto bytes = wav_bytes(spec);
        std::ofstream(path_, std::ios::binary).write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    }
};

}  // namespace

TEST_F(WavIqReaderTest, ReadsSampleRateAndLength) {
    write({.rate = 96000, .samples = {1, 2, 3, 4, 5, 6, 7, 8}});
    WavIqReader reader(path_);
    EXPECT_EQ(reader.sample_rate(), 96000);
    EXPECT_EQ(reader.total_samples(), 4u);
}

TEST_F(WavIqReaderTest, ScalesLeftToIAndRightToQ) {
    write({.samples = {16384, -16384, 32767, -32768}});
    WavIqReader reader(path_);
    std::vector<Sample> out(2);
    ASSERT_EQ(reader.read(out), 2u);
    EXPECT_FLOAT_EQ(out[0].real(), 0.5f);
    EXPECT_FLOAT_EQ(out[0].imag(), -0.5f);
    EXPECT_FLOAT_EQ(out[1].real(), 32767.0f / 32768.0f);
    EXPECT_FLOAT_EQ(out[1].imag(), -1.0f);
}

TEST_F(WavIqReaderTest, ReadsInChunksUntilEnd) {
    write({.samples = {1, 0, 2, 0, 3, 0, 4, 0, 5, 0}});
    WavIqReader reader(path_);
    std::vector<Sample> out(2);
    EXPECT_EQ(reader.read(out), 2u);
    EXPECT_EQ(reader.read(out), 2u);
    ASSERT_EQ(reader.read(out), 1u);
    EXPECT_FLOAT_EQ(out[0].real(), 5.0f / 32768.0f);
    EXPECT_EQ(reader.read(out), 0u);
}

TEST_F(WavIqReaderTest, SkipsUnknownChunksIncludingPadByte) {
    write({.samples = {100, 200}, .extra_chunk = true});
    WavIqReader reader(path_);
    std::vector<Sample> out(1);
    ASSERT_EQ(reader.read(out), 1u);
    EXPECT_FLOAT_EQ(out[0].real(), 100.0f / 32768.0f);
    EXPECT_FLOAT_EQ(out[0].imag(), 200.0f / 32768.0f);
}

TEST_F(WavIqReaderTest, RejectsMono) {
    write({.channels = 1, .samples = {1, 2}});
    EXPECT_THROW(WavIqReader{path_}, std::runtime_error);
}

TEST_F(WavIqReaderTest, Rejects24Bit) {
    write({.bits = 24, .samples = {1, 2, 3}});
    EXPECT_THROW(WavIqReader{path_}, std::runtime_error);
}

TEST_F(WavIqReaderTest, RejectsMissingFile) {
    EXPECT_THROW(WavIqReader{path_}, std::runtime_error);
}

TEST_F(WavIqReaderTest, RejectsNonWavFile) {
    std::ofstream(path_, std::ios::binary) << "this is not a wav file";
    EXPECT_THROW(WavIqReader{path_}, std::runtime_error);
}

TEST_F(WavIqReaderTest, TruncatedFileReadsWhatIsPresent) {
    write({.samples = {1, 2, 3, 4, 5, 6}, .data_size = 4000});
    WavIqReader reader(path_);
    EXPECT_EQ(reader.total_samples(), 3u);
    std::vector<Sample> out(10);
    EXPECT_EQ(reader.read(out), 3u);
}

TEST_F(WavIqReaderTest, RejectsChunkSizeThatWouldOverflow32BitSkip) {
    // An odd-sized unknown chunk declaring size 0xFFFFFFFF, ahead of the real
    // data chunk. Skipping it correctly runs off the end of the file, so
    // construction must fail rather than misparsing the chunk's own bytes
    // (or the following data chunk) as new chunk headers.
    write({.samples = {1, 2}, .huge_chunk_size = 0xFFFFFFFFu});
    EXPECT_THROW(WavIqReader{path_}, std::runtime_error);
}

TEST_F(WavIqReaderTest, ExtensibleFormatWithPcmSubFormatReadsSamples) {
    write({.samples = {16384, -16384, 32767, -32768}, .extensible = true, .sub_format = 0x0001});
    WavIqReader reader(path_);
    std::vector<Sample> out(2);
    ASSERT_EQ(reader.read(out), 2u);
    EXPECT_FLOAT_EQ(out[0].real(), 0.5f);
    EXPECT_FLOAT_EQ(out[0].imag(), -0.5f);
    EXPECT_FLOAT_EQ(out[1].real(), 32767.0f / 32768.0f);
    EXPECT_FLOAT_EQ(out[1].imag(), -1.0f);
}

TEST_F(WavIqReaderTest, ExtensibleFormatWithNonPcmSubFormatThrows) {
    write({.samples = {1, 2}, .extensible = true, .sub_format = 0x0003});
    EXPECT_THROW(WavIqReader{path_}, std::runtime_error);
}
