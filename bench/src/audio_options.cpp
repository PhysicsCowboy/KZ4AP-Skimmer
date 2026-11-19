#include "audio_options.hpp"

#include <cmath>
#include <cstdio>
#include <stdexcept>

namespace kz4ap::bench {

const char* const kAudioUsage =
    "  receiver audio (a one-channel WAV is read as receiver audio; docs/signal-processing.md, section 1):\n"
    "  --audio               read the recording as receiver audio whatever its channels\n"
    "  --audio-channel N     the channel read, 0 the first (left); implies --audio\n"
    "  --passband-hz LO,HI   the receiver's audio passband, Hz (default 300,3000)\n"
    "  --dial-khz KHZ        the radio's dial frequency, kHz: frequencies are then RF\n"
    "  --pitch-hz HZ         the CW pitch, the audio tone of a station on the dial frequency, Hz (default 600)\n"
    "  --sideband usb|lsb    the receiver's sideband (default usb)\n";

namespace {

double number(const std::string& name, const std::string& text) {
    std::size_t used = 0;
    double v = 0.0;
    try {
        v = std::stod(text, &used);
    } catch (const std::exception&) {
        used = 0;
    }
    if (used == 0 || used != text.size() || !std::isfinite(v)) throw std::runtime_error(name + " wants a number: " + text);
    return v;
}

}  // namespace

bool parse_audio_option(const std::string& a, const std::function<std::string()>& next, AudioOptions& o) {
    if (a == "--audio") {
        o.audio = true;
    } else if (a == "--audio-channel") {
        const double c = number(a, next());
        if (c < 0 || c != std::floor(c)) throw std::runtime_error("--audio-channel wants a channel number, 0 the first");
        o.channel = static_cast<int>(c);
        o.audio = true;
    } else if (a == "--passband-hz") {
        const std::string v = next();
        const auto comma = v.find(',');
        if (comma == std::string::npos) throw std::runtime_error("--passband-hz wants LO,HI in Hz: " + v);
        o.passband_lo_hz = number(a, v.substr(0, comma));
        o.passband_hi_hz = number(a, v.substr(comma + 1));
        if (!(o.passband_lo_hz >= 0.0 && o.passband_hi_hz > o.passband_lo_hz))
            throw std::runtime_error("--passband-hz wants 0 <= LO < HI, Hz: " + v);
    } else if (a == "--dial-khz") {
        o.dial_hz = 1000.0 * number(a, next());
    } else if (a == "--pitch-hz") {
        o.pitch_hz = number(a, next());
        if (!(o.pitch_hz > 0.0)) throw std::runtime_error("--pitch-hz wants a positive audio frequency, Hz");
    } else if (a == "--sideband") {
        const std::string v = next();
        if (v == "usb") o.lower_sideband = false;
        else if (v == "lsb") o.lower_sideband = true;
        else throw std::runtime_error("--sideband wants usb or lsb: " + v);
    } else {
        return false;
    }
    o.given = true;
    return true;
}

RecordingOptions recording_options(const AudioOptions& o, bool mirror) {
    RecordingOptions r;
    r.input = o.audio ? RecordingInput::Audio : RecordingInput::Auto;
    r.audio_channel = o.channel;
    r.mirror = mirror || o.lower_sideband;
    return r;
}

void check_audio_options(const AudioOptions& o, const RecordingReader& reader) {
    if (o.given && !reader.is_audio())
        throw std::runtime_error("receiver-audio options given for an I/Q recording (two channels): add --audio to read it as audio");
}

void apply_audio_passband(EngineConfig& config, const AudioOptions& o) {
    config.passband_lo_hz = o.lower_sideband ? -o.passband_hi_hz : o.passband_lo_hz;
    config.passband_hi_hz = o.lower_sideband ? -o.passband_lo_hz : o.passband_hi_hz;
}

std::optional<double> audio_center_hz(const AudioOptions& o) {
    if (!o.dial_hz) return std::nullopt;
    return o.lower_sideband ? *o.dial_hz + o.pitch_hz : *o.dial_hz - o.pitch_hz;
}

std::string describe_audio_input(const RecordingReader& reader, const AudioOptions& o) {
    char line[768];
    const std::optional<double> center = audio_center_hz(o);
    std::snprintf(line, sizeof line,
                  "receiver audio: channel %d of %d at %d samples/s, analytic at %d complex samples/s; passband %.0f-%.0f Hz; %s; "
                  "SNRs are receiver-audio values (dB re the detector's floor per bin within the passband; dBFS re the sound "
                  "card's full scale, a full-scale sine 0 dBFS), not comparable with SDR I/Q",
                  reader.audio_channel(), reader.file_channels(), reader.file_rate(), reader.sample_rate(), o.passband_lo_hz,
                  o.passband_hi_hz,
                  center ? (std::string("RF = ") + (o.lower_sideband ? "dial - (audio - pitch)" : "dial + (audio - pitch)") +
                            ", dial " + std::to_string(*o.dial_hz / 1000.0) + " kHz, pitch " + std::to_string(o.pitch_hz) +
                            " Hz: freq_hz is Hz from " + std::to_string(*center / 1000.0) + " kHz" +
                            (o.lower_sideband ? " (the audio negated)" : ""))
                               .c_str()
                         : (o.lower_sideband ? "no dial frequency: frequencies are audio tone frequencies, Hz, negated (lower sideband)"
                                             : "no dial frequency: frequencies are audio tone frequencies, Hz"));
    return line;
}

}  // namespace kz4ap::bench
