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

}  // namespace kz4ap::detail
