// The duration fit's exp and log (Plan B task B2(b); docs/signal-processing.md section 8c, "Duration fit"):
// SLEEF 3.9.0's vectorized _u10 functions applied elementwise to arrays, the instruction set chosen once at run
// time from what the processor and the operating system support (vecmath.cpp).
//
// - Accuracy: SLEEF states an error bound of 1.0 ulp for the returned value ("_u10"), and non-number
//   arguments and results as C99 specifies (exp(-inf) = 0, log(0) = -inf, log(+inf) = +inf, NaN gives NaN).
// - Two families, each bit-wise consistent by SLEEF's statement: "cinz" (no FMA; the same bits from its SSE2,
//   AVX and AVX-512 versions on every platform) and "finz" (FMA; the same bits from its AVX2 and AVX-512
//   versions). The families differ from each other in the last bits.
// - Each element's value depends only on that element: an array is processed a vector at a time, the last
//   partial vector padded with 0, so exp(x) and log(x) are one fixed function of x wherever x sits.
// - Every implementation's code is in its own translation unit compiled for its instruction set
//   (vecmath_sse2.cpp, vecmath_avx.cpp, vecmath_avx2.cpp, vecmath_avx512.cpp), which includes only intrinsics
//   and SLEEF's C declarations, so nothing compiled there can be shared with (and run by) code built for plain
//   x86-64; the AVX ones end with vzeroupper.
#pragma once

#include <cstddef>

namespace kz4ap::bank::vecmath {

// out[i] = e^x[i] or ln x[i] for i < n (out may be x).
using ArrayFn = void (*)(const double* x, double* out, std::size_t n);

// One SLEEF implementation.
struct Impl {
    const char* name;      // family and instruction set, e.g. "finz avx2"
    bool fma;              // the finz family (FMA) rather than cinz
    std::size_t width;     // doubles per call
    ArrayFn exp;
    ArrayFn log;
    bool (*available)();   // this processor and operating system can run it
};

// Every implementation built in, in the order the fit prefers them (the first available is used).
const Impl* implementations(std::size_t& count);

// The implementation the fit uses on this processor (chosen once, at the first use; for tests only, the
// environment variable KZ4AP_FIT_MATH can restrict the choice: vecmath.cpp, choose).
const Impl& selected();

// The fit's exp and log: the selected implementation's.
void exp(const double* x, double* out, std::size_t n);
void log(const double* x, double* out, std::size_t n);

}  // namespace kz4ap::bank::vecmath
