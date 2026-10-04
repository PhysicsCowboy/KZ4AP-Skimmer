// SLEEF's AVX-512 exp and log over arrays, both families (Plan B task B2(b); vecmath.hpp). Compiled with
// AVX-512F enabled (engine/CMakeLists.txt); run only where vecmath.cpp has found it.
#include "vecmath_isa.hpp"

namespace kz4ap::bank::vecmath::isa {

void exp_cinz_avx512(const double* x, double* out, std::size_t n) {
    KZ4AP_VECMATH_LOOP(8, Sleef_cinz_expd8_u10avx512fnofma, _mm512_loadu_pd, _mm512_storeu_pd, _mm256_zeroupper();)
}

void log_cinz_avx512(const double* x, double* out, std::size_t n) {
    KZ4AP_VECMATH_LOOP(8, Sleef_cinz_logd8_u10avx512fnofma, _mm512_loadu_pd, _mm512_storeu_pd, _mm256_zeroupper();)
}

void exp_finz_avx512(const double* x, double* out, std::size_t n) {
    KZ4AP_VECMATH_LOOP(8, Sleef_finz_expd8_u10avx512f, _mm512_loadu_pd, _mm512_storeu_pd, _mm256_zeroupper();)
}

void log_finz_avx512(const double* x, double* out, std::size_t n) {
    KZ4AP_VECMATH_LOOP(8, Sleef_finz_logd8_u10avx512f, _mm512_loadu_pd, _mm512_storeu_pd, _mm256_zeroupper();)
}

}  // namespace kz4ap::bank::vecmath::isa
