#pragma once

#include "kz4ap/types.hpp"

#include <complex>
#include <cstddef>

#include <pocketfft_hdronly.h>

namespace kz4ap::detail {

// Unnormalized forward DFT: out[k] = sum_n in[n] * exp(-2*pi*i*k*n/N).
inline void fft_forward(const Sample* in, Sample* out, std::size_t n) {
    const pocketfft::stride_t stride{static_cast<std::ptrdiff_t>(sizeof(Sample))};
    pocketfft::c2c<float>({n}, stride, stride, {0}, pocketfft::FORWARD, in, out, 1.0f);
}

// Unnormalized inverse DFT: out[n] = sum_k in[k] * exp(+2*pi*i*k*n/N).
inline void fft_inverse(const Sample* in, Sample* out, std::size_t n) {
    const pocketfft::stride_t stride{static_cast<std::ptrdiff_t>(sizeof(Sample))};
    pocketfft::c2c<float>({n}, stride, stride, {0}, pocketfft::BACKWARD, in, out, 1.0f);
}

// Unnormalized forward DFT in double precision, same definition and ordering as numpy.fft.fft (bin k at
// k / N cycles per sample, k = 0 ... N-1): the bank decoder's noise spectrum (bank/noise.cpp).
inline void fft_forward(const std::complex<double>* in, std::complex<double>* out, std::size_t n) {
    const pocketfft::stride_t stride{static_cast<std::ptrdiff_t>(sizeof(std::complex<double>))};
    pocketfft::c2c<double>({n}, stride, stride, {0}, pocketfft::FORWARD, in, out, 1.0);
}

// numpy.fft.rfft(in, n) in double precision: the n / 2 + 1 non-negative-frequency bins of the unnormalized
// forward DFT of n real values (the caller zero-pads to n), out[k] = sum_m in[m] exp(-2 pi i k m / n).
// pocketfft's real transform, the one numpy.fft uses: the bank decoder's periodicity estimate
// (bank/periodicity.cpp).
inline void rfft_forward(const double* in, std::complex<double>* out, std::size_t n) {
    const pocketfft::stride_t stride_in{static_cast<std::ptrdiff_t>(sizeof(double))};
    const pocketfft::stride_t stride_out{static_cast<std::ptrdiff_t>(sizeof(std::complex<double>))};
    pocketfft::r2c<double>({n}, stride_in, stride_out, std::size_t{0}, pocketfft::FORWARD, in, out, 1.0);
}

// numpy.fft.irfft(in, n) in double precision (norm "backward"): the n real values whose rfft is in (n / 2 + 1
// bins; the imaginary parts of the 0 and n / 2 bins are ignored), out[m] = (1/n) sum_k in[k] exp(+2 pi i k m / n)
// over the Hermitian extension.
inline void irfft(const std::complex<double>* in, double* out, std::size_t n) {
    const pocketfft::stride_t stride_in{static_cast<std::ptrdiff_t>(sizeof(std::complex<double>))};
    const pocketfft::stride_t stride_out{static_cast<std::ptrdiff_t>(sizeof(double))};
    pocketfft::c2r<double>({n}, stride_in, stride_out, std::size_t{0}, pocketfft::BACKWARD, in, out,
                           1.0 / static_cast<double>(n));
}

}  // namespace kz4ap::detail
