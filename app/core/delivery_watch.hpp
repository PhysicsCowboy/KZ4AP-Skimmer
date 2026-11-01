// Whether a live radio is still delivering samples, free of Qt: a radio can stop without a word (its service dies, the USB
// stalls, the machine slept), and the program must then not go on looking healthy.
#pragma once

#include <cstdint>

namespace kz4ap::app {

class DeliveryWatch {
public:
    // How long without a new sample counts as stopped, s. Heuristic: much longer than any pause seen on a running radio, short
    // enough for an operator to notice at once.
    static constexpr double kStalledAfterS = 5.0;

    // The samples the radio has delivered so far, and the wall clock, s.
    void update(std::uint64_t received, double wall_s);
    bool stalled() const { return silent_s_ >= kStalledAfterS; }
    // How long since the last new sample, s.
    double silent_s() const { return silent_s_; }

private:
    bool started_ = false;
    std::uint64_t received_ = 0;
    double moved_at_s_ = 0.0;
    double silent_s_ = 0.0;
};

}  // namespace kz4ap::app
