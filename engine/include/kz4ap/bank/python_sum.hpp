// Python's built-in sum() of floats, reproduced so that the bank decoder's sums match the removed prototype's
// (training/kz4ap_proto) bit for bit. Since CPython 3.12, sum() of floats is not a left-to-right loop: it is
// Neumaier's compensated summation (the prototype runs on Python 3.12). numpy's np.sum is different again
// (pairwise; numpy_sum.hpp).
#pragma once

#include <cmath>
#include <cstddef>
#include <vector>

namespace kz4ap::bank {

// Neumaier's compensated summation from 0.0, the compensation added at the end when it is non-zero and finite
// (CPython 3.12's float path of sum()).
inline double python_sum(const double* values, std::size_t n) {
    double sum = 0.0;
    double c = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        const double x = values[i];
        const double t = sum + x;
        if (std::abs(sum) >= std::abs(x))
            c += (sum - t) + x;
        else
            c += (x - t) + sum;
        sum = t;
    }
    if (c != 0.0 && std::isfinite(c)) sum += c;
    return sum;
}

inline double python_sum(const std::vector<double>& values) { return python_sum(values.data(), values.size()); }

}  // namespace kz4ap::bank
