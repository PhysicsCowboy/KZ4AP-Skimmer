// The fit's exp and log: the implementations and the run-time choice (Plan B task B2(b); vecmath.hpp).
#include "vecmath.hpp"

#include <cstdint>
#include <cstdlib>
#include <string_view>

#if defined(_MSC_VER)
#include <immintrin.h>
#include <intrin.h>
#endif

namespace kz4ap::bank::vecmath {

namespace isa {
void exp_cinz_sse2(const double* x, double* out, std::size_t n);
void log_cinz_sse2(const double* x, double* out, std::size_t n);
void exp_cinz_avx(const double* x, double* out, std::size_t n);
void log_cinz_avx(const double* x, double* out, std::size_t n);
void exp_finz_avx2(const double* x, double* out, std::size_t n);
void log_finz_avx2(const double* x, double* out, std::size_t n);
void exp_cinz_avx512(const double* x, double* out, std::size_t n);
void log_cinz_avx512(const double* x, double* out, std::size_t n);
void exp_finz_avx512(const double* x, double* out, std::size_t n);
void log_finz_avx512(const double* x, double* out, std::size_t n);
}  // namespace isa

namespace {

// What the processor and the operating system support (the OS must save the AVX and AVX-512 registers).
struct Cpu {
    bool avx = false;
    bool avx2_fma = false;
    bool avx512f = false;
};

Cpu detect() {
    Cpu c;
#if defined(_MSC_VER)
    int r[4];
    __cpuid(r, 0);
    const int max_leaf = r[0];
    __cpuid(r, 1);
    const bool osxsave = (r[2] & (1 << 27)) != 0;
    const bool avx = (r[2] & (1 << 28)) != 0;
    const bool fma = (r[2] & (1 << 12)) != 0;
    if (!osxsave || !avx) return c;
    const unsigned long long xcr0 = _xgetbv(0);
    if ((xcr0 & 0x6) != 0x6) return c;  // the XMM and YMM state
    c.avx = true;
    if (max_leaf < 7) return c;
    __cpuidex(r, 7, 0);
    // MSVC's /arch:AVX2 (vecmath_avx2.cpp, and vecmath_avx512.cpp as SLEEF builds its own AVX-512 code with
    // MSVC: /arch:AVX2 plus AVX-512F intrinsics) may also emit BMI1 and BMI2 instructions: required too.
    const bool bmi = (r[1] & (1 << 3)) != 0 && (r[1] & (1 << 8)) != 0;
    c.avx2_fma = fma && (r[1] & (1 << 5)) != 0 && bmi;
    // AVX-512F with the opmask and ZMM state; the AVX-512 file's other code is AVX2's (every AVX-512F
    // processor has AVX2 and FMA; required explicitly).
    c.avx512f = (r[1] & (1 << 16)) != 0 && (xcr0 & 0xe6) == 0xe6 && c.avx2_fma;
#else
    // GCC and Clang check the operating system's support (XGETBV) before reporting avx, avx2 and avx512f. Each
    // file is compiled with exactly the extensions checked here (-mavx; -mavx2 -mfma; -mavx512f).
    __builtin_cpu_init();
    c.avx = __builtin_cpu_supports("avx");
    c.avx2_fma = __builtin_cpu_supports("avx2") && __builtin_cpu_supports("fma");
    c.avx512f = __builtin_cpu_supports("avx512f") && c.avx2_fma;
#endif
    return c;
}

const Cpu& cpu() {
    static const Cpu c = detect();
    return c;
}

bool always() { return true; }
bool has_avx() { return cpu().avx; }
bool has_avx2_fma() { return cpu().avx2_fma; }
bool has_avx512f() { return cpu().avx512f; }

// The fit's order of preference (chosen by measurement, Plan B task B2(b); results record section 4): the finz
// family on the widest vectors the processor has (AVX-512F, else AVX2 with FMA: most Intel Core processors
// since Haswell, 2013, and AMD since Excavator, 2015; not every Pentium, Celeron or Atom-class one), else the
// cinz family's widest (AVX-512F, AVX, else SSE2, which every x86-64 processor has). Only the finz functions made the fit faster than the C library's exp and log on both machines measured; cinz SSE2
// made it slower. Within a family the bits do not depend on the instruction set (SLEEF's statement, tested);
// the two families differ in the last bits, so a processor without FMA gives other last bits than one with it.
constexpr Impl kImpls[] = {
    {"finz avx512", true, 8, isa::exp_finz_avx512, isa::log_finz_avx512, has_avx512f},
    {"finz avx2", true, 4, isa::exp_finz_avx2, isa::log_finz_avx2, has_avx2_fma},
    {"cinz avx512", false, 8, isa::exp_cinz_avx512, isa::log_cinz_avx512, has_avx512f},
    {"cinz avx", false, 4, isa::exp_cinz_avx, isa::log_cinz_avx, has_avx},
    {"cinz sse2", false, 2, isa::exp_cinz_sse2, isa::log_cinz_sse2, always},
};

// TEST ONLY: the environment variable KZ4AP_FIT_MATH, read once at the first use, restricts the choice to the
// implementations whose name starts with its value ("cinz" or "finz" for a family, or a full name such as
// "cinz sse2"); the first available of those is used. Unset, empty or matching none available: the order
// above. It exists so that the tests and a development-set replay can run the fit through the family a
// processor without FMA would use; decoding is not meant to set it.
const Impl& choose() {
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996)  // getenv: read once; the program never changes its environment
#endif
    const char* force = std::getenv("KZ4AP_FIT_MATH");
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
    if (force != nullptr && *force != '\0') {
        const std::string_view f(force);
        for (const Impl& i : kImpls)
            if (std::string_view(i.name).substr(0, f.size()) == f && i.available()) return i;
    }
    for (const Impl& i : kImpls)
        if (i.available()) return i;
    return kImpls[4];  // unreachable: cinz sse2 is always available
}

}  // namespace

const Impl* implementations(std::size_t& count) {
    count = sizeof kImpls / sizeof kImpls[0];
    return kImpls;
}

const Impl& selected() {
    static const Impl& s = choose();
    return s;
}

void exp(const double* x, double* out, std::size_t n) { selected().exp(x, out, n); }
void log(const double* x, double* out, std::size_t n) { selected().log(x, out, n); }

}  // namespace kz4ap::bank::vecmath
