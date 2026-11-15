// The Kaiser window's pieces shared by the engine's windowed-sinc designs (the resampler, the listening filter).
#pragma once

namespace kz4ap::detail {

// The modified Bessel function of the first kind, order 0, by its power series (terms below 1e-12 of the sum dropped).
inline double bessel_i0(double x) {
    double sum = 1.0, term = 1.0;
    const double q = x * x / 4.0;
    for (int k = 1; k < 200; ++k) {
        term *= q / (static_cast<double>(k) * k);
        sum += term;
        if (term < 1e-12 * sum) break;
    }
    return sum;
}

}  // namespace kz4ap::detail
