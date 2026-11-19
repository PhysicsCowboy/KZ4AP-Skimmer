// The bench tools' receiver-audio options (audio_options.hpp): parsing, the passband on either sideband and RF from the dial
// frequency and the pitch.
#include "audio_options.hpp"

#include <gtest/gtest.h>

#include <stdexcept>
#include <string>
#include <vector>

using namespace kz4ap;
using namespace kz4ap::bench;

namespace {

AudioOptions parsed(const std::vector<std::string>& args) {
    AudioOptions o;
    for (std::size_t i = 0; i < args.size(); ++i) {
        const auto next = [&]() -> std::string {
            if (++i >= args.size()) throw std::runtime_error("missing value");
            return args[i];
        };
        if (!parse_audio_option(args[i], next, o)) throw std::runtime_error("not an audio option: " + args[i]);
    }
    return o;
}

}  // namespace

TEST(AudioOptions, Defaults) {
    const AudioOptions o;
    EXPECT_FALSE(o.audio);
    EXPECT_FALSE(o.given);
    EXPECT_DOUBLE_EQ(o.passband_lo_hz, 300.0);
    EXPECT_DOUBLE_EQ(o.passband_hi_hz, 3000.0);
    EXPECT_DOUBLE_EQ(o.pitch_hz, 600.0);
    EXPECT_FALSE(audio_center_hz(o));
    EXPECT_EQ(recording_options(o, false).input, RecordingInput::Auto);
}

// RF = dial + (audio - pitch) on the upper sideband: a station heard at 750 Hz with the dial at 14 050 kHz and a 600 Hz pitch is
// at 14 050.150 kHz, center + audio with the center at dial - pitch.
TEST(AudioOptions, TheUpperSidebandAddsTheAudioOffset) {
    const AudioOptions o = parsed({"--dial-khz", "14050", "--pitch-hz", "600", "--passband-hz", "200,2800"});
    EXPECT_TRUE(o.given);
    ASSERT_TRUE(audio_center_hz(o));
    EXPECT_DOUBLE_EQ(*audio_center_hz(o) + 750.0, 14050150.0);
    EngineConfig c;
    apply_audio_passband(c, o);
    EXPECT_DOUBLE_EQ(c.passband_lo_hz, 200.0);
    EXPECT_DOUBLE_EQ(c.passband_hi_hz, 2800.0);
    EXPECT_FALSE(recording_options(o, false).mirror);
}

// RF = dial - (audio - pitch) on the lower sideband: the signal is conjugated (the tone at 750 Hz read at -750 Hz) and the center is
// dial + pitch, so center + (-750 Hz) is 14 049.850 kHz; the passband is negated with it.
TEST(AudioOptions, TheLowerSidebandSubtractsIt) {
    const AudioOptions o = parsed({"--dial-khz", "14050", "--sideband", "lsb", "--audio-channel", "1"});
    EXPECT_TRUE(o.audio);
    EXPECT_EQ(o.channel, 1);
    ASSERT_TRUE(audio_center_hz(o));
    EXPECT_DOUBLE_EQ(*audio_center_hz(o) - 750.0, 14049850.0);
    const RecordingOptions r = recording_options(o, false);
    EXPECT_TRUE(r.mirror);
    EXPECT_EQ(r.input, RecordingInput::Audio);
    EXPECT_EQ(r.audio_channel, 1);
    EngineConfig c;
    apply_audio_passband(c, o);
    EXPECT_DOUBLE_EQ(c.passband_lo_hz, -3000.0);
    EXPECT_DOUBLE_EQ(c.passband_hi_hz, -300.0);
}

TEST(AudioOptions, RefusesBadValues) {
    EXPECT_THROW(parsed({"--passband-hz", "3000,300"}), std::runtime_error);
    EXPECT_THROW(parsed({"--passband-hz", "300"}), std::runtime_error);
    EXPECT_THROW(parsed({"--sideband", "am"}), std::runtime_error);
    EXPECT_THROW(parsed({"--pitch-hz", "0"}), std::runtime_error);
    EXPECT_THROW(parsed({"--audio-channel", "-1"}), std::runtime_error);
    EXPECT_THROW(parsed({"--dial-khz", "x"}), std::runtime_error);
    AudioOptions o;
    EXPECT_FALSE(parse_audio_option("--mirror", [] { return std::string(); }, o));
}
