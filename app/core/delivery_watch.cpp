#include "delivery_watch.hpp"

namespace kz4ap::app {

void DeliveryWatch::update(std::uint64_t received, double wall_s) {
    if (!started_ || received != received_) {
        started_ = true;
        received_ = received;
        moved_at_s_ = wall_s;
    }
    silent_s_ = wall_s - moved_at_s_;
}

}  // namespace kz4ap::app
