#include "kz4ap/wav_reader.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <stdexcept>
#include <string>

static_assert(std::endian::native == std::endian::little, "WAV parsing assumes a little-endian host");

namespace kz4ap {
namespace {

constexpr std::uint16_t kFormatPcm = 1;
constexpr std::uint16_t kFormatExtensible = 0xFFFE;

template <typename T>
T read_le(std::ifstream& f) {
    T value{};
    f.read(reinterpret_cast<char*>(&value), sizeof(T));
    if (!f) throw std::runtime_error("WAV file ended unexpectedly");
    return value;
}

std::string read_tag(std::ifstream& f) {
    std::array<char, 4> tag{};
    f.read(tag.data(), 4);
    if (!f) throw std::runtime_error("WAV file ended unexpectedly");
    return std::string(tag.data(), 4);
}

}  // namespace

WavIqReader::WavIqReader(const std::filesystem::path& path) : file_(path, std::ios::binary) {
    const std::string name = path.string();
    if (!file_) throw std::runtime_error("cannot open " + name);
    try {
        if (read_tag(file_) != "RIFF") throw std::runtime_error("not a WAV file");
        read_le<std::uint32_t>(file_);
        if (read_tag(file_) != "WAVE") throw std::runtime_error("not a WAV file");

        bool have_format = false;
        while (true) {
            if (file_.peek() == std::char_traits<char>::eof()) throw std::runtime_error("no data chunk");
            const std::string id = read_tag(file_);
            const auto size = read_le<std::uint32_t>(file_);
            if (id == "fmt ") {
                if (size < 16) throw std::runtime_error("malformed format chunk");
                auto format = read_le<std::uint16_t>(file_);
                const auto channels = read_le<std::uint16_t>(file_);
                sample_rate_ = static_cast<int>(read_le<std::uint32_t>(file_));
                read_le<std::uint32_t>(file_);  // byte rate
                read_le<std::uint16_t>(file_);  // block align
                const auto bits = read_le<std::uint16_t>(file_);
                std::uint32_t consumed = 16;
                if (format == kFormatExtensible && size >= 26) {
                    read_le<std::uint16_t>(file_);  // extension size
                    read_le<std::uint16_t>(file_);  // valid bits
                    read_le<std::uint32_t>(file_);  // channel mask
                    format = read_le<std::uint16_t>(file_);  // first two bytes of the sub-format GUID
                    consumed = 26;
                }
                if (format != kFormatPcm) throw std::runtime_error("only integer PCM WAV files are supported");
                if (channels != 2)
                    throw std::runtime_error("needs 2 channels (I and Q), found " + std::to_string(channels));
                if (bits != 16) throw std::runtime_error("needs 16-bit samples, found " + std::to_string(bits) + "-bit");
                file_.seekg(static_cast<std::streamoff>(static_cast<std::uint64_t>(size) - consumed + (size & 1)),
                            std::ios::cur);
                have_format = true;
            } else if (id == "data") {
                if (!have_format) throw std::runtime_error("data chunk comes before format chunk");
                const auto data_start = file_.tellg();
                file_.seekg(0, std::ios::end);
                const auto available = static_cast<std::uint64_t>(file_.tellg() - data_start);
                file_.seekg(data_start);
                total_samples_ = std::min<std::uint64_t>(size, available) / 4;
                return;
            } else {
                file_.seekg(static_cast<std::streamoff>(static_cast<std::uint64_t>(size) + (size & 1)),
                            std::ios::cur);
            }
        }
    } catch (const std::runtime_error& e) {
        throw std::runtime_error(name + ": " + e.what());
    }
}

std::size_t WavIqReader::read(std::span<Sample> out) {
    const auto n = static_cast<std::size_t>(std::min<std::uint64_t>(out.size(), total_samples_ - samples_read_));
    if (n == 0) return 0;
    raw_.resize(2 * n);
    file_.read(reinterpret_cast<char*>(raw_.data()), static_cast<std::streamsize>(raw_.size() * sizeof(std::int16_t)));
    const auto got = static_cast<std::size_t>(file_.gcount()) / 4;
    for (std::size_t i = 0; i < got; ++i) {
        out[i] = Sample(raw_[2 * i] / 32768.0f, raw_[2 * i + 1] / 32768.0f);
    }
    samples_read_ += got;
    return got;
}

}  // namespace kz4ap
