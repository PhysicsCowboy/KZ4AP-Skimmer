// The fit's exp and log: the implementations and the run-time choice (Plan B task B2(b); vecmath.hpp).
#include "vecmath.hpp"

#include <cstdint>

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
    c.avx2_fma = fma && (r[1] & (1 << 5)) != 0;
    c.avx512f = (r[1] & (1 << 16)) != 0 && (xcr0 & 0xe6) == 0xe6;  // and the opmask and ZMM state
#else
    // GCC and Clang check the operating system's support (XGETBV) before reporting avx, avx2 and avx512f.
    __builtin_cpu_init();
    c.avx = __builtin_cpu_supports("avx");
    c.avx2_fma = __builtin_cpu_supports("avx2") && __builtin_cpu_supports("fma");
    c.avx512f = __builtin_cpu_supports("avx512f");
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
// family on the widest vectors the processor has (AVX-512F, else AVX2 with FMA: Intel since 2013, AMD since
// 2015), else the cinz family's widest (AVX-512F, AVX, else SSE2, which every x86-64 processor has). Only the
// finz functions made the fit faster than the C library's exp and log on both machines measured; cinz SSE2
// made it slower. Within a family the bits do not depend on the instruction set (SLEEF's statement, tested);
// the two families differ in the last bits, so a processor without FMA gives other last bits than one with it.
constexpr Impl kImpls[] = {
    {"finz avx512", true, 8, isa::exp_finz_avx512, isa::log_finz_avx512, has_avx512f},
    {"finz avx2", true, 4, isa::exp_finz_avx2, isa::log_finz_avx2, has_avx2_fma},
    {"cinz avx512", false, 8, isa::exp_cinz_avx512, isa::log_cinz_avx512, has_avx512f},
    {"cinz avx", false, 4, isa::exp_cinz_avx, isa::log_cinz_avx, has_avx},
    {"cinz sse2", false, 2, isa::exp_cinz_sse2, isa::log_cinz_sse2, always},
};

const Impl& choose() {
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
