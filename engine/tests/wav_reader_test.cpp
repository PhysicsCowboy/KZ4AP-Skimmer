#include "kz4ap/wav_reader.hpp"

#include "kz4ap/file_iq_source.hpp"
#include "kz4ap/utc_time.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
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
    std::uint16_t format_tag = 1;       // the plain fmt chunk's tag: 1 PCM, 3 IEEE float
    std::optional<std::string> raw_data;    // the data chunk's bytes, instead of samples
    std::optional<std::string> auxi;        // an auxi chunk's body, written before data
    std::optional<std::uint16_t> block_align;  // override the format's block align
};

// One 24-bit little-endian sample.
void put_i24(std::string& b, std::int32_t v) {
    const auto u = static_cast<std::uint32_t>(v);
    for (int i = 0; i < 3; ++i) b.push_back(static_cast<char>((u >> (8 * i)) & 0xFF));
}

void put_system_time(std::string& b, int year, int month, int day, int hour, int minute, int second, int ms) {
    for (const int v : {year, month, 1, day, hour, minute, second, ms}) put_u16(b, static_cast<std::uint16_t>(v));
}

// An auxi chunk's body as CWSL_File and HDSDR write it: 164 bytes, the center frequency after the two times.
std::string auxi_body(std::uint32_t center_hz, bool times = true) {
    std::string b;
    if (times) {
        put_system_time(b, 2017, 8, 21, 18, 0, 0, 93);
        put_system_time(b, 2017, 8, 21, 18, 15, 0, 5);
    } else {
        b += std::string(32, '\0');
    }
    put_u32(b, center_hz);
    b += std::string(164 - b.size(), '\0');
    return b;
}

std::string wav_bytes(const WavSpec& s) {
    std::string fmt;
    put_u16(fmt, s.extensible ? std::uint16_t{0xFFFE} : s.format_tag);
    put_u16(fmt, static_cast<std::uint16_t>(s.channels));
    put_u32(fmt, static_cast<std::uint32_t>(s.rate));
    put_u32(fmt, static_cast<std::uint32_t>(s.rate * s.channels * s.bits / 8));
    put_u16(fmt, s.block_align.value_or(static_cast<std::uint16_t>(s.channels * s.bits / 8)));
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
    if (s.raw_data) data = *s.raw_data;

    std::string body = "WAVE";
    body += "fmt ";
    put_u32(body, static_cast<std::uint32_t>(fmt.size()));
    body += fmt;
    if (s.auxi) {
        body += "auxi";
        put_u32(body, static_cast<std::uint32_t>(s.auxi->size()));
        body += *s.auxi;
        if (s.auxi->size() & 1) body.push_back('\0');
    }
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

TEST_F(WavIqReaderTest, Reads24BitSignExtendedAndScaled) {
    std::string raw;
    for (const std::int32_t v : {0x400000, -0x400000, 0x7FFFFF, -0x800000, -1, 1}) put_i24(raw, v);
    write({.bits = 24, .raw_data = raw});
    WavIqReader reader(path_);
    EXPECT_EQ(reader.sample_format(), WavSampleFormat::Int24);
    ASSERT_EQ(reader.total_samples(), 3u);
    std::vector<Sample> out(3);
    ASSERT_EQ(reader.read(out), 3u);
    EXPECT_FLOAT_EQ(out[0].real(), 0.5f);
    EXPECT_FLOAT_EQ(out[0].imag(), -0.5f);
    EXPECT_FLOAT_EQ(out[1].real(), 8388607.0f / 8388608.0f);
    EXPECT_FLOAT_EQ(out[1].imag(), -1.0f);
    EXPECT_FLOAT_EQ(out[2].real(), -1.0f / 8388608.0f);
    EXPECT_FLOAT_EQ(out[2].imag(), 1.0f / 8388608.0f);
}

// As the 2017 eclipse wideband recordings: WAVE_FORMAT_EXTENSIBLE, 24 valid bits of 24, PCM sub-format, block align 6.
TEST_F(WavIqReaderTest, Reads24BitExtensibleAndSeeks) {
    std::string raw;
    for (std::int32_t k = 0; k < 10; ++k) {
        put_i24(raw, 1000 * k);
        put_i24(raw, -1000 * k);
    }
    write({.rate = 192000, .bits = 24, .extensible = true, .raw_data = raw});
    WavIqReader reader(path_);
    EXPECT_EQ(reader.sample_rate(), 192000);
    ASSERT_EQ(reader.total_samples(), 10u);
    reader.seek(7);
    std::vector<Sample> out(5);
    ASSERT_EQ(reader.read(out), 3u);
    EXPECT_FLOAT_EQ(out[0].real(), 7000.0f / 8388608.0f);
    EXPECT_FLOAT_EQ(out[0].imag(), -7000.0f / 8388608.0f);
    EXPECT_FLOAT_EQ(out[2].real(), 9000.0f / 8388608.0f);
}

TEST_F(WavIqReaderTest, Reads32BitIntegerAndFloat) {
    std::string raw;
    put_u32(raw, 0x40000000u);  // 0.5
    put_u32(raw, 0x80000000u);  // -1
    write({.bits = 32, .raw_data = raw});
    {
        WavIqReader reader(path_);
        EXPECT_EQ(reader.sample_format(), WavSampleFormat::Int32);
        std::vector<Sample> out(1);
        ASSERT_EQ(reader.read(out), 1u);
        EXPECT_FLOAT_EQ(out[0].real(), 0.5f);
        EXPECT_FLOAT_EQ(out[0].imag(), -1.0f);
    }
    std::string floats;
    for (const float f : {0.25f, -1.5f}) {
        std::uint32_t u = 0;
        std::memcpy(&u, &f, 4);
        put_u32(floats, u);
    }
    write({.bits = 32, .format_tag = 3, .raw_data = floats});
    {
        WavIqReader reader(path_);
        EXPECT_EQ(reader.sample_format(), WavSampleFormat::Float32);
        std::vector<Sample> out(1);
        ASSERT_EQ(reader.read(out), 1u);
        EXPECT_FLOAT_EQ(out[0].real(), 0.25f);
        EXPECT_FLOAT_EQ(out[0].imag(), -1.5f);  // floats as stored, beyond full scale too
    }
    write({.bits = 32, .extensible = true, .sub_format = 0x0003, .raw_data = floats});
    EXPECT_EQ(WavIqReader(path_).sample_format(), WavSampleFormat::Float32);
}

TEST_F(WavIqReaderTest, RejectsOtherWidthsAndAMismatchedBlockAlign) {
    write({.bits = 8, .raw_data = std::string(4, '\0')});
    EXPECT_THROW(WavIqReader{path_}, std::runtime_error);
    write({.bits = 20, .raw_data = std::string(10, '\0')});
    EXPECT_THROW(WavIqReader{path_}, std::runtime_error);
    write({.bits = 64, .format_tag = 3, .raw_data = std::string(16, '\0')});
    EXPECT_THROW(WavIqReader{path_}, std::runtime_error);
    write({.bits = 24, .raw_data = std::string(12, '\0'), .block_align = 8});
    EXPECT_THROW(WavIqReader{path_}, std::runtime_error);
}

TEST_F(WavIqReaderTest, ReadsTheAuxiChunksCenterAndTimes) {
    write({.samples = {1, 2}, .auxi = auxi_body(7086101)});
    WavIqReader reader(path_);
    ASSERT_TRUE(reader.info().center_hz);
    EXPECT_EQ(*reader.info().center_hz, 7086101.0);
    ASSERT_TRUE(reader.info().start_unix_ms);
    EXPECT_EQ(*reader.info().start_unix_ms, 1503338400093);  // 2017-08-21 18:00:00.093 UTC
    ASSERT_TRUE(reader.info().stop_unix_ms);
    EXPECT_EQ(utc_text(*reader.info().stop_unix_ms), "2017-08-21T18:15:00.005Z");
    std::vector<Sample> out(1);
    ASSERT_EQ(reader.read(out), 1u);
    EXPECT_FLOAT_EQ(out[0].imag(), 2.0f / 32768.0f);
}

TEST_F(WavIqReaderTest, AnEmptyOrShortAuxiChunkSaysNothing) {
    write({.samples = {1, 2}, .auxi = auxi_body(0, false)});
    EXPECT_FALSE(WavIqReader(path_).info().center_hz);
    EXPECT_FALSE(WavIqReader(path_).info().start_unix_ms);
    write({.samples = {1, 2}, .auxi = std::string(5, '\x01')});
    EXPECT_FALSE(WavIqReader(path_).info().start_unix_ms);
    EXPECT_EQ(WavIqReader(path_).total_samples(), 1u);
    write({.samples = {1, 2}});
    EXPECT_FALSE(WavIqReader(path_).info().center_hz);
}

TEST(UtcTime, CivilDatesAndText) {
    EXPECT_EQ(unix_ms_from_civil(1970, 1, 1, 0, 0, 0, 0), 0);
    EXPECT_EQ(unix_ms_from_civil(2000, 3, 1, 0, 0, 0, 0), 951868800000);
    EXPECT_EQ(utc_text(unix_ms_from_civil(2024, 2, 29, 23, 59, 59, 999)), "2024-02-29T23:59:59.999Z");
    EXPECT_EQ(utc_text(-1), "1969-12-31T23:59:59.999Z");
    EXPECT_EQ(utc_text(1503338400093), "2017-08-21T18:00:00.093Z");
}

TEST(CenterFromFileName, TheFormsSkimmerRecordersUse) {
    EXPECT_EQ(center_hz_from_file_name("N2GZ_wideband_7000_7192kHz_201708_1801UT.wav", 192000), 7096000.0);
    EXPECT_EQ(center_hz_from_file_name("W3OA_wideband_6995_7177kHz_20170821_180000UT.wav", 192000), 7086000.0);
    EXPECT_EQ(center_hz_from_file_name("KC2LRC_20170821_180000_14091kHz.wav", 192000), 14091000.0);
    EXPECT_EQ(center_hz_from_file_name("kz4ap_7030.000kHz_192kHz_20261030-213000.wav", 192000), 7030000.0);
    EXPECT_EQ(center_hz_from_file_name("HDSDR_20170821_180000Z_7086kHz_RF.wav", 192000), 7086000.0);
    EXPECT_EQ(center_hz_from_file_name("SDRSharp_20170821_180000Z_7086000Hz_IQ.wav", 192000), 7086000.0);
    EXPECT_NEAR(center_hz_from_file_name("band 14.091MHz.wav", 192000).value(), 14091000.0, 1e-6);
    EXPECT_EQ(center_hz_from_file_name("x_7030.5kHz", 48000), 7030500.0);  // no extension to drop
}

TEST(CenterFromFileName, NoneWithoutAFrequency) {
    EXPECT_FALSE(center_hz_from_file_name("real30.wav", 192000));
    EXPECT_FALSE(center_hz_from_file_name("capture_192kHz.wav", 192000));  // the sample rate, not a frequency
    EXPECT_FALSE(center_hz_from_file_name("kHz_khz_Hz.wav", 192000));
    EXPECT_FALSE(center_hz_from_file_name("x_7.0.3kHz.wav", 192000));
    // Edges that are not a sample rate's span apart are not edges: the word with a unit is the center then.
    EXPECT_EQ(center_hz_from_file_name("a_7000_7100kHz.wav", 192000), 7100000.0);
}

TEST_F(WavIqReaderTest, AFileSourcesCenterComesFromTheCallerTheHeaderOrTheName) {
    // The header's center, though the name gives another.
    const std::filesystem::path named = std::filesystem::temp_directory_path() / "kz4ap_test_14091kHz.wav";
    const auto bytes = wav_bytes({.rate = 1000, .samples = std::vector<std::int16_t>(4000, 0), .auxi = auxi_body(7086101)});
    std::ofstream(named, std::ios::binary).write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    {
        FileIqSource header(named);
        EXPECT_EQ(header.center_frequency_hz(), 7086101.0);
        EXPECT_EQ(header.center_origin(), FileIqSource::CenterOrigin::Header);
        EXPECT_EQ(header.start_unix_ms(), 1503338400093);
        FileIqSource later(named, 1.5);  // 1.5 s in: 1 500 samples at 1 000 samples/s
        EXPECT_EQ(later.start_unix_ms(), 1503338401593);
        FileIqSource given(named, 0.0, 7000000.0);
        EXPECT_EQ(given.center_frequency_hz(), 7000000.0);
        EXPECT_EQ(given.center_origin(), FileIqSource::CenterOrigin::Given);
    }
    // No auxi: the name's center, and no time.
    const auto plain = wav_bytes({.rate = 1000, .samples = std::vector<std::int16_t>(40, 0)});
    std::ofstream(named, std::ios::binary).write(plain.data(), static_cast<std::streamsize>(plain.size()));
    {
        FileIqSource name(named);
        EXPECT_EQ(name.center_frequency_hz(), 14091000.0);
        EXPECT_EQ(name.center_origin(), FileIqSource::CenterOrigin::FileName);
        EXPECT_FALSE(name.start_unix_ms());
    }
    std::error_code ec;
    std::filesystem::remove(named, ec);
    // Neither.
    write({.rate = 1000, .samples = {1, 2}});
    FileIqSource unknown(path_);
    EXPECT_FALSE(unknown.center_frequency_hz());
    EXPECT_EQ(unknown.center_origin(), FileIqSource::CenterOrigin::Unknown);
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
