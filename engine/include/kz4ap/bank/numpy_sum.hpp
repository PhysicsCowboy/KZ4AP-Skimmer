// numpy's pairwise summation (np.sum of a contiguous float64 array, numpy's pairwise_sum in
// loops_utils), reproduced so that the bank decoder's sums match the prototype's (training/kz4ap_proto)
// bit for bit: floating-point addition is not associative, and numpy does not add left to right.
// Measured: equal to np.sum on 900 random arrays of 1 to 299 values (numpy 2.5.3, Task 5).
#pragma once

#include <cstddef>
#include <vector>

namespace kz4ap::bank {

// Under 8 values a plain loop from 0.0; up to 128, eight interleaved partial sums combined pairwise,
// ((r0 + r1) + (r2 + r3)) + ((r4 + r5) + (r6 + r7)), then the remainder added in order; above 128, the
// two halves (split at a multiple of 8) summed recursively.
inline double pairwise_sum(const double* a, std::size_t n) {
    if (n < 8) {
        double res = 0.0;
        for (std::size_t i = 0; i < n; ++i) res += a[i];
        return res;
    }
    if (n <= 128) {
        double r[8];
        for (std::size_t j = 0; j < 8; ++j) r[j] = a[j];
        std::size_t i = 8;
        for (; i < n - (n % 8); i += 8)
            for (std::size_t j = 0; j < 8; ++j) r[j] += a[i + j];
        double res = ((r[0] + r[1]) + (r[2] + r[3])) + ((r[4] + r[5]) + (r[6] + r[7]));
        for (; i < n; ++i) res += a[i];
        return res;
    }
    std::size_t n2 = n / 2;
    n2 -= n2 % 8;
    return pairwise_sum(a, n2) + pairwise_sum(a + n2, n - n2);
}

inline double pairwise_sum(const std::vector<double>& a) { return pairwise_sum(a.data(), a.size()); }

}  // namespace kz4ap::bank
