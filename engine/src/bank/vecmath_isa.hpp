// Included only by the per-instruction-set translation units vecmath_sse2.cpp, vecmath_avx.cpp,
// vecmath_avx2.cpp and vecmath_avx512.cpp (Plan B task B2(b)): SLEEF's declarations and the array loop. Only
// intrinsics, SLEEF's C declarations and macros: no inline function that the linker could share with code
// built for another instruction set.
#pragma once

#include <cstddef>

#include <immintrin.h>

// sleef.h declares each instruction set's functions under that set's predefined macro (__SSE2__, __AVX__,
// __AVX512F__); MSVC does not define __SSE2__ on x64 although every x64 target has SSE2.
#if defined(_MSC_VER) && defined(_M_X64) && !defined(__SSE2__)
#define __SSE2__ 1
#endif
#ifndef SLEEF_STATIC_LIBS
#define SLEEF_STATIC_LIBS 1  // linked statically (sleef.h would otherwise declare dllimport on Windows)
#endif
#include <sleef.h>

// The body of an array function: VEC doubles per call of F, the last partial vector padded with 0. LOAD and
// STORE are the unaligned load and store of VEC doubles; END is what runs before returning (vzeroupper or
// nothing).
#define KZ4AP_VECMATH_LOOP(VEC, F, LOAD, STORE, END)                     \
    std::size_t i = 0;                                                   \
    for (; i + (VEC) <= n; i += (VEC)) STORE(out + i, F(LOAD(x + i)));   \
    if (i < n) {                                                         \
        double pad[VEC] = {};                                            \
        for (std::size_t j = 0; i + j < n; ++j) pad[j] = x[i + j];       \
        STORE(pad, F(LOAD(pad)));                                        \
        for (std::size_t j = 0; i + j < n; ++j) out[i + j] = pad[j];     \
    }                                                                    \
    END
