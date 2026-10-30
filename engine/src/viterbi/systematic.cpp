// The systematic term of a sample's predictive variance (systematic.hpp; design notes, part 9 as amended by D23).
#include "kz4ap/viterbi/systematic.hpp"

namespace kz4ap::viterbi {

double systematic_variance_fs2(double sigma2_fs2, double carrier_power_fs2, double s, double v, double floor) {
    return sigma2_fs2 + carrier_power_fs2 * (floor * floor * s * s + v);
}

}  // namespace kz4ap::viterbi
