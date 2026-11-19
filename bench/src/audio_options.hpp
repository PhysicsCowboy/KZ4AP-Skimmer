// The bench tools' options for receiver audio (kz4ap-calls, kz4ap-bench): how a recording is read, the receiver's passband and,
// with the radio's dial frequency, RF. docs/signal-processing.md, section 1, "Receiver audio".
//   --audio               read the recording as receiver audio, whatever its channels (a one-channel file is audio anyway)
//   --audio-channel N     the channel read, 0 the first (left); implies --audio
//   --passband-hz LO,HI   the receiver's audio passband, Hz (default 300,3000)
//   --dial-khz KHZ        the radio's dial frequency, kHz: frequencies are then reported as RF
//   --pitch-hz HZ         the CW pitch, the audio frequency of a station on the dial frequency, Hz (default 600)
//   --sideband usb|lsb    the receiver's sideband (default usb, CW's usual)
#pragma once

#include "kz4ap/audio_input.hpp"
#include "kz4ap/engine.hpp"
#include "kz4ap/recording_reader.hpp"

#include <functional>
#include <optional>
#include <string>

namespace kz4ap::bench {

struct AudioOptions {
    bool audio = false;          // --audio or --audio-channel given
    bool given = false;          // any of these options given
    bool rf_given = false;       // --dial-khz or --pitch-hz given
    int channel = 0;
    double passband_lo_hz = kAudioPassbandLoHz;
    double passband_hi_hz = kAudioPassbandHiHz;
    std::optional<double> dial_hz;  // Hz
    double pitch_hz = 600.0;        // Hz: the default a CW radio's pitch is usually set to (heuristic, the operator's)
    bool lower_sideband = false;
};

// The usage lines of the options above.
extern const char* const kAudioUsage;

// Takes option `a` if it is one of these, its value from next(); false otherwise. Throws std::runtime_error for a bad value.
bool parse_audio_option(const std::string& a, const std::function<std::string()>& next, AudioOptions& o);

// How to read the recording: as receiver audio when --audio or --audio-channel says so, else as RecordingInput::Auto decides
// (a one-channel file is audio). The lower sideband conjugates the analytic signal (RecordingOptions::mirror), so that a tone at
// f Hz is read at -f Hz and RF = center + offset holds on both sidebands (audio_center_hz).
RecordingOptions recording_options(const AudioOptions& o, bool mirror);

// Checks the options against what the file turned out to be: throws std::runtime_error for audio options on an I/Q recording.
void check_audio_options(const AudioOptions& o, const RecordingReader& reader);

// The engine's passband for receiver audio: [lo, hi] Hz, or [-hi, -lo] on the lower sideband (the conjugated signal).
void apply_audio_passband(EngineConfig& config, const AudioOptions& o);

// The RF the engine's frequency 0 Hz stands for, Hz, given the dial frequency: dial - pitch on the upper sideband (RF = dial +
// (audio - pitch)), dial + pitch on the lower (RF = dial - (audio - pitch), the audio read at -audio). None without a dial
// frequency: frequencies are then audio tone frequencies, Hz (negated on the lower sideband).
std::optional<double> audio_center_hz(const AudioOptions& o);

// One line for a tool's report: what the input is and what its frequencies and SNRs mean.
std::string describe_audio_input(const RecordingReader& reader, const AudioOptions& o);

}  // namespace kz4ap::bench
