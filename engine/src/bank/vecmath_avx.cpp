// SLEEF's cinz AVX exp and log over arrays (Plan B task B2(b); vecmath.hpp). Compiled with AVX enabled
// (engine/CMakeLists.txt); run only where vecmath.cpp has found AVX.
#include "vecmath_isa.hpp"

namespace kz4ap::bank::vecmath::isa {

void exp_cinz_avx(const double* x, double* out, std::size_t n) {
    KZ4AP_VECMATH_LOOP(4, Sleef_cinz_expd4_u10avx, _mm256_loadu_pd, _mm256_storeu_pd, _mm256_zeroupper();)
}

void log_cinz_avx(const double* x, double* out, std::size_t n) {
    KZ4AP_VECMATH_LOOP(4, Sleef_cinz_logd4_u10avx, _mm256_loadu_pd, _mm256_storeu_pd, _mm256_zeroupper();)
}

}  // namespace kz4ap::bank::vecmath::isa
