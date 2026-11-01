#include "kz4ap/segmental/fast_math.hpp"

#include "kz4ap/viterbi/observation.hpp"

#include <cmath>

namespace kz4ap::segmental::fast {

Tables::Tables() {
    for (std::size_t i = 0; i < two_to.size(); ++i) two_to[i] = std::pow(2.0, static_cast<double>(i) / 32.0);
    for (std::size_t i = 0; i < log_center.size(); ++i) {
        const double center = 1.0 + (static_cast<double>(i) + 0.5) / 256.0;
        log_center[i] = std::log(center);
        inverse_center[i] = 1.0 / center;
    }
    for (std::size_t i = 0; i < i0_rest.size(); ++i) {
        const double x = static_cast<double>(i) / 64.0;
        i0_rest[i] = viterbi::ln_i0(x) - x;
    }
}

const Tables tables;

}  // namespace kz4ap::segmental::fast
