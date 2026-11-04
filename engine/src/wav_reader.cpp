#include "kz4ap/wav_reader.hpp"

#include "kz4ap/utc_time.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>

static_assert(std::endian::native == std::endian::little, "WAV parsing assumes a little-endian host");

namespace kz4ap {
namespace {

constexpr std::uint16_t kFormatPcm = 1;
constexpr std::uint16_t kFormatFloat = 3;
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

std::uint16_t u16_at(const std::vector<unsigned char>& b, std::size_t at) {
    return static_cast<std::uint16_t>(b[at] | (b[at + 1] << 8));
}

std::uint32_t u32_at(const std::vector<unsigned char>& b, std::size_t at) {
    return static_cast<std::uint32_t>(b[at]) | (static_cast<std::uint32_t>(b[at + 1]) << 8) |
           (static_cast<std::uint32_t>(b[at + 2]) << 16) | (static_cast<std::uint32_t>(b[at + 3]) << 24);
}

// A Windows SYSTEMTIME (year, month, day of week, day, hour, minute, second, ms; eight 16-bit words) at `at`, as ms since
// 1970 UTC; none if it is all zero or not a valid date. SpectraVue and HDSDR fill it from the system's UTC clock.
std::optional<std::int64_t> system_time_at(const std::vector<unsigned char>& b, std::size_t at) {
    const int year = u16_at(b, at), month = u16_at(b, at + 2), day = u16_at(b, at + 6);
    const int hour = u16_at(b, at + 8), minute = u16_at(b, at + 10), second = u16_at(b, at + 12), ms = u16_at(b, at + 14);
    if (year < 1980 || year > 2200 || month < 1 || month > 12 || day < 1 || day > 31 || hour > 23 || minute > 59 ||
        second > 60 || ms > 999)
        return std::nullopt;
    return unix_ms_from_civil(year, month, day, hour, minute, second, ms);
}

// The auxi chunk: two SYSTEMTIMEs (start, stop, 16 bytes each), then the center frequency, a 32-bit unsigned Hz; the
// rest (the converter's rates, an IF, a DC offset) is not used here.
WavRecordingInfo parse_auxi(const std::vector<unsigned char>& b) {
    WavRecordingInfo info;
    if (b.size() >= 16) info.start_unix_ms = system_time_at(b, 0);
    if (b.size() >= 32) info.stop_unix_ms = system_time_at(b, 16);
    if (b.size() >= 36) {
        const std::uint32_t center = u32_at(b, 32);
        if (center != 0) info.center_hz = static_cast<double>(center);
    }
    return info;
}

std::string lower(std::string_view s) {
    std::string out(s);
    for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

// A plain decimal number: digits, at most one point, at least one digit.
std::optional<double> plain_number(std::string_view s) {
    if (s.empty() || s.size() > 20) return std::nullopt;
    int points = 0, digits = 0;
    for (const char c : s) {
        if (c == '.') ++points;
        else if (std::isdigit(static_cast<unsigned char>(c))) ++digits;
        else return std::nullopt;
    }
    if (points > 1 || digits == 0) return std::nullopt;
    return std::stod(std::string(s));
}

// A word `<number><unit>` with unit Hz, kHz or MHz (any case): the number and the unit's factor to Hz.
std::optional<std::pair<double, double>> frequency_word(std::string_view word) {
    const std::string w = lower(word);
    for (const auto& [unit, factor] : {std::pair<const char*, double>{"khz", 1e3}, {"mhz", 1e6}, {"hz", 1.0}}) {
        const std::size_t n = std::strlen(unit);
        if (w.size() > n && w.compare(w.size() - n, n, unit) == 0) {
            const std::optional<double> value = plain_number(std::string_view(w).substr(0, w.size() - n));
            if (value) return std::pair{*value, factor};
            return std::nullopt;  // "khz" ends in "hz" too: a word that fails as kHz is not tried as Hz
        }
    }
    return std::nullopt;
}

}  // namespace

std::optional<double> center_hz_from_file_name(std::string_view file_name, int sample_rate) {
    std::string_view stem = file_name;
    if (const std::size_t dot = stem.rfind('.'); dot != std::string_view::npos && dot + 1 < stem.size() &&
                                                  stem.size() - dot <= 5) {
        const std::string_view ext = stem.substr(dot + 1);
        if (std::all_of(ext.begin(), ext.end(), [](char c) { return std::isalpha(static_cast<unsigned char>(c)) != 0; }))
            stem = stem.substr(0, dot);
    }
    std::vector<std::string_view> words;
    std::size_t begin = 0;
    for (std::size_t i = 0; i <= stem.size(); ++i) {
        if (i == stem.size() || stem[i] == '_' || stem[i] == '-' || stem[i] == ' ') {
            if (i > begin) words.push_back(stem.substr(begin, i - begin));
            begin = i + 1;
        }
    }
    const double rate = sample_rate;
    // The span's edges first: a plain number, then a frequency with a unit, the two a sample rate's span apart.
    for (std::size_t i = 0; i + 1 < words.size(); ++i) {
        const std::optional<double> low = plain_number(words[i]);
        const auto high = frequency_word(words[i + 1]);
        if (!low || !high) continue;
        const double low_hz = *low * high->second, high_hz = high->first * high->second;
        const double span = high_hz - low_hz;
        if (span >= 0.8 * rate && span <= 1.001 * rate) return (low_hz + high_hz) / 2.0;
    }
    for (const std::string_view word : words) {
        const auto f = frequency_word(word);
        if (!f) continue;
        const double hz = f->first * f->second;
        if (hz > 0.0 && hz != rate) return hz;
    }
    return std::nullopt;
}

WavIqReader::WavIqReader(const std::filesystem::path& path) : file_(path, std::ios::binary) {
    const std::string name = path_text(path);
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
                const auto block_align = read_le<std::uint16_t>(file_);
                const auto bits = read_le<std::uint16_t>(file_);
                std::uint32_t consumed = 16;
                if (format == kFormatExtensible && size >= 26) {
                    read_le<std::uint16_t>(file_);  // extension size
                    read_le<std::uint16_t>(file_);  // valid bits: the values are left-justified in `bits`, so unused
                    read_le<std::uint32_t>(file_);  // channel mask
                    format = read_le<std::uint16_t>(file_);  // first two bytes of the sub-format GUID
                    consumed = 26;
                }
                if (format != kFormatPcm && format != kFormatFloat)
                    throw std::runtime_error("only integer PCM or 32-bit float WAV files are supported");
                if (channels != 2)
                    throw std::runtime_error("needs 2 channels (I and Q), found " + std::to_string(channels));
                if (format == kFormatFloat) {
                    if (bits != 32) throw std::runtime_error("needs 32-bit float samples, found " + std::to_string(bits) + "-bit");
                    format_ = WavSampleFormat::Float32;
                } else if (bits == 16) {
                    format_ = WavSampleFormat::Int16;
                } else if (bits == 24) {
                    format_ = WavSampleFormat::Int24;
                } else if (bits == 32) {
                    format_ = WavSampleFormat::Int32;
                } else {
                    throw std::runtime_error("needs 16-, 24- or 32-bit samples, found " + std::to_string(bits) + "-bit");
                }
                frame_bytes_ = 2 * (bits / 8);
                if (block_align != frame_bytes_)
                    throw std::runtime_error("block align " + std::to_string(block_align) + " bytes does not match 2 channels of " +
                                             std::to_string(bits) + " bits");
                if (sample_rate_ <= 0) throw std::runtime_error("sample rate must be positive");
                file_.seekg(static_cast<std::streamoff>(static_cast<std::uint64_t>(size) - consumed + (size & 1)),
                            std::ios::cur);
                have_format = true;
            } else if (id == "auxi" && size <= 4096) {
                std::vector<unsigned char> body(size);
                file_.read(reinterpret_cast<char*>(body.data()), static_cast<std::streamsize>(size));
                if (!file_) throw std::runtime_error("WAV file ended unexpectedly");
                info_ = parse_auxi(body);
                if (size & 1) file_.seekg(1, std::ios::cur);
            } else if (id == "data") {
                if (!have_format) throw std::runtime_error("data chunk comes before format chunk");
                const auto data_start = file_.tellg();
                file_.seekg(0, std::ios::end);
                const auto available = static_cast<std::uint64_t>(file_.tellg() - data_start);
                file_.seekg(data_start);
                data_start_ = static_cast<std::streamoff>(data_start);
                total_samples_ = std::min<std::uint64_t>(size, available) / static_cast<std::uint64_t>(frame_bytes_);
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
    const auto frame = static_cast<std::size_t>(frame_bytes_);
    raw_.resize(frame * n);
    file_.read(reinterpret_cast<char*>(raw_.data()), static_cast<std::streamsize>(raw_.size()));
    const auto got = static_cast<std::size_t>(file_.gcount()) / frame;
    const unsigned char* p = raw_.data();
    switch (format_) {
        case WavSampleFormat::Int16:
            for (std::size_t i = 0; i < got; ++i, p += 4) {
                std::int16_t v[2];
                std::memcpy(v, p, 4);
                out[i] = Sample(v[0] / 32768.0f, v[1] / 32768.0f);
            }
            break;
        case WavSampleFormat::Int24: {
            // Three bytes, little-endian, two's complement: into the top of a 32-bit word, then an arithmetic shift down.
            const auto value = [](const unsigned char* b) {
                const std::uint32_t u = (static_cast<std::uint32_t>(b[0]) << 8) | (static_cast<std::uint32_t>(b[1]) << 16) |
                                        (static_cast<std::uint32_t>(b[2]) << 24);
                return static_cast<std::int32_t>(u) >> 8;
            };
            constexpr float kScale = 1.0f / 8388608.0f;  // 2^-23
            for (std::size_t i = 0; i < got; ++i, p += 6)
                out[i] = Sample(static_cast<float>(value(p)) * kScale, static_cast<float>(value(p + 3)) * kScale);
            break;
        }
        case WavSampleFormat::Int32: {
            constexpr double kScale = 1.0 / 2147483648.0;  // 2^-31
            for (std::size_t i = 0; i < got; ++i, p += 8) {
                std::int32_t v[2];
                std::memcpy(v, p, 8);
                out[i] = Sample(static_cast<float>(v[0] * kScale), static_cast<float>(v[1] * kScale));
            }
            break;
        }
        case WavSampleFormat::Float32:
            for (std::size_t i = 0; i < got; ++i, p += 8) {
                float v[2];
                std::memcpy(v, p, 8);
                out[i] = Sample(v[0], v[1]);
            }
            break;
    }
    samples_read_ += got;
    return got;
}

void WavIqReader::seek(std::uint64_t sample) {
    samples_read_ = std::min(sample, total_samples_);
    file_.clear();
    file_.seekg(data_start_ + static_cast<std::streamoff>(samples_read_ * static_cast<std::uint64_t>(frame_bytes_)));
    if (!file_) throw std::runtime_error("cannot seek in the recording");
}

}  // namespace kz4ap
