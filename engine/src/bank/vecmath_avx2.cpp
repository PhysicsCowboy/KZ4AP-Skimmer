// SLEEF's finz AVX2 (with FMA) exp and log over arrays (Plan B task B2(b); vecmath.hpp). Compiled with AVX2 and
// FMA enabled (engine/CMakeLists.txt); run only where vecmath.cpp has found them.
#include "vecmath_isa.hpp"

namespace kz4ap::bank::vecmath::isa {

void exp_finz_avx2(const double* x, double* out, std::size_t n) {
    KZ4AP_VECMATH_LOOP(4, Sleef_finz_expd4_u10avx2, _mm256_loadu_pd, _mm256_storeu_pd, _mm256_zeroupper();)
}

void log_finz_avx2(const double* x, double* out, std::size_t n) {
    KZ4AP_VECMATH_LOOP(4, Sleef_finz_logd4_u10avx2, _mm256_loadu_pd, _mm256_storeu_pd, _mm256_zeroupper();)
}

}  // namespace kz4ap::bank::vecmath::isa
