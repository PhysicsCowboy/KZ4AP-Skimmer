#pragma once

#include "kz4ap/decoder.hpp"
#include "kz4ap/signal_detector.hpp"
#include "kz4ap/spectrum.hpp"
#include "kz4ap/track_evidence.hpp"

#include <cstdint>
#include <functional>
#include <utility>
#include <variant>
#include <vector>

namespace kz4ap {

struct TrackEvent {
    enum class Kind { Born, Died };
    Kind kind;
    Track track;
    TrackEnd end = TrackEnd::Faded;  // Died: why
};

struct DecodedTextEvent {
    std::uint32_t track_id;
    double freq_hz;
    std::vector<DecodedSymbol> chars;
    float wpm;
    float confidence;
    std::vector<TextCorrection> corrections = {};  // applied after chars, in order (DecodeUpdate); bank and segmental decoders
};

using Event = std::variant<SpectrumFrame, TrackEvent, DecodedTextEvent>;

// The stream's clock for a consumer that takes the events in batches (the app's display, DisplayFeed): where it stands among the
// events around it, so that what the consumer does with the clock does not depend on how the batches fell.
struct StreamClock {
    double time_s = 0.0;  // s of stream
};

// Delivers engine events to every subscriber, synchronously, in subscription order.
class EventBus {
public:
    using Handler = std::function<void(const Event&)>;

    void subscribe(Handler handler) { handlers_.push_back(std::move(handler)); }

    void publish(const Event& event) const {
        for (const auto& h : handlers_) h(event);
    }

private:
    std::vector<Handler> handlers_;
};

}  // namespace kz4ap
