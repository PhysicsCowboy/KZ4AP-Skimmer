// SLEEF's cinz SSE2 exp and log over arrays (Plan B task B2(b); vecmath.hpp). Compiled for plain x86-64.
#include "vecmath_isa.hpp"

namespace kz4ap::bank::vecmath::isa {

void exp_cinz_sse2(const double* x, double* out, std::size_t n) {
    KZ4AP_VECMATH_LOOP(2, Sleef_cinz_expd2_u10sse2, _mm_loadu_pd, _mm_storeu_pd, )
}

void log_cinz_sse2(const double* x, double* out, std::size_t n) {
    KZ4AP_VECMATH_LOOP(2, Sleef_cinz_logd2_u10sse2, _mm_loadu_pd, _mm_storeu_pd, )
}

}  // namespace kz4ap::bank::vecmath::isa
