// Benchmark for the int2str algorithm variants, laid out as a 2-D table.
//
//   c++ -std=c++2c -O3 -mcpu=native benchmark_int2str.cpp -o bench && ./bench
//
// Columns : the standard library first - snprintf, itoa, std::to_string,
//           std::format, std::to_chars - then v0, fastconv, simd. The library
//           entries only support a few radices, so most of those cells are
//           empty; itoa is not in the C standard and Apple's libc does not ship
//           it, so that column is only built on Windows.
// Rows    : every (radix, len) pair that v0 supports - the same 124 pairs as
//           the str2int table, so the two are directly comparable.
//
// The most significant digit of every generated value is non-zero, so all
// columns produce exactly Len characters and the library entries - which write
// the minimal representation rather than a zero padded one - can take part.
//
// Each cell converts a 1024-entry corpus; REPS is calibrated so one run lasts
// about TARGET_US. TRIALS runs are timed and grouped into blocks of BLOCK; the
// fastest run of each block becomes one sample, and the samples are averaged.
// Cells are ns per conversion. Every cell is checked against a reference
// before it is timed.

#include "int2str.h"

#include <charconv>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <algorithm>
#include <array>
#include <format>
#include <string>
#include <vector>

#if defined(__APPLE__)
#include <sys/sysctl.h>
#endif

#ifndef TRIALS
#define TRIALS 120
#endif
#ifndef BLOCK
#define BLOCK 4
#endif
#ifndef TARGET_US
#define TARGET_US 50
#endif
#ifndef COUNT
#define COUNT 1024
#endif

// Candidate A: four digits per lookup. Limiting this to selected small radices
// keeps each table within 10 KiB; larger tables quickly lose to arithmetic due
// to cache pressure.
template <unsigned Radix>
struct QuadTable {
    static constexpr std::size_t kSize = Radix * Radix * Radix * Radix;
    uint32_t packed[kSize];

    constexpr QuadTable() : packed{} {
        for (std::size_t i = 0; i < kSize; ++i) {
            std::size_t v = i;
            const unsigned d3 = static_cast<unsigned>(v % Radix);
            v /= Radix;
            const unsigned d2 = static_cast<unsigned>(v % Radix);
            v /= Radix;
            const unsigned d1 = static_cast<unsigned>(v % Radix);
            v /= Radix;
            const unsigned d0 = static_cast<unsigned>(v);
            packed[i] = static_cast<uint32_t>(DigitChar(d0)) |
                        static_cast<uint32_t>(DigitChar(d1)) << 8 |
                        static_cast<uint32_t>(DigitChar(d2)) << 16 |
                        static_cast<uint32_t>(DigitChar(d3)) << 24;
        }
    }
};

template <unsigned Radix>
inline constexpr QuadTable<Radix> kQuadTable{};

namespace quad_candidate {

template <unsigned Radix>
concept SmallTableRadix = Radix == 3 || Radix == 5 || Radix == 6 || Radix == 7;

template <unsigned Radix, unsigned Len>
    requires (SmallTableRadix<Radix> &&
              (Len == 4 || Len == 8 || Len == 16 ||
               Len == 32 || Len == 64 || Len == 128) &&
              BitsNeeded(Radix, Len) <= kFastconvMaxBits)
constexpr void int2str(char* out, result_t<Radix, Len> value) noexcept {
    if constexpr (Len == 4) {
        Store<uint32_t>(out, kQuadTable<Radix>.packed[static_cast<std::size_t>(value)]);
    } else {
        constexpr unsigned low_len = Len / 2;
        constexpr unsigned high_len = Len - low_len;
        constexpr auto split = Pow<result_t<Radix, Len>>(Radix, low_len);
        const auto high = value / split;
        const auto low = value - high * split;
        int2str<Radix, high_len>(out, static_cast<result_t<Radix, high_len>>(high));
        int2str<Radix, low_len>(out + high_len,
                                static_cast<result_t<Radix, low_len>>(low));
    }
}

}  // namespace quad_candidate

#if defined(FASTCONV_X86_SIMD) || defined(FASTCONV_ARM_SIMD)
// Candidate B: fixed-width decimal conversion based on reciprocal division and
// packed lane arithmetic. This is deliberately specialised: generalising it to
// every radix adds constants and instructions that lose to the pair table.
namespace decimal16_candidate {

inline void Convert(char* out, uint64_t value) noexcept {
#if defined(FASTCONV_X86_SIMD)
    constexpr int kDiv10000Shift = 45;
    const uint32_t high8 = static_cast<uint32_t>(value / 100000000u);
    const uint32_t low8 = static_cast<uint32_t>(value - uint64_t(high8) * 100000000u);
    const __m128i chunks = _mm_set_epi64x(low8, high8);

    const __m128i div10000 = _mm_set1_epi32(static_cast<int>(0xd1b71759u));
    const __m128i q10000 = _mm_srli_epi64(_mm_mul_epu32(chunks, div10000), kDiv10000Shift);
    const __m128i r10000 =
        _mm_sub_epi32(chunks, _mm_mul_epu32(q10000, _mm_set1_epi32(10000)));
    const __m128i groups100 = _mm_or_si128(q10000, _mm_slli_epi64(r10000, 32));

    const __m128i q100 = _mm_srli_epi16(
        _mm_mulhi_epu16(groups100, _mm_set1_epi16(0x147b)), 3);
    const __m128i r100 =
        _mm_sub_epi16(groups100, _mm_mullo_epi16(q100, _mm_set1_epi16(100)));
    const __m128i pairs = _mm_or_si128(q100, _mm_slli_epi32(r100, 16));

    const __m128i tens = _mm_mulhi_epu16(pairs, _mm_set1_epi16(0x199a));
    const __m128i ones =
        _mm_sub_epi16(pairs, _mm_mullo_epi16(tens, _mm_set1_epi16(10)));
    const __m128i digits = _mm_add_epi8(
        _mm_or_si128(tens, _mm_slli_epi16(ones, 8)), _mm_set1_epi8('0'));
    _mm_storeu_si128(reinterpret_cast<__m128i*>(out), digits);
#else
#if FASTCONV_HAS_NATIVE_UINT128
    const uint32_t high8 = static_cast<uint32_t>(
        (static_cast<fastconv_uint128_t>(value) * 0xabcc77118461cefdull) >> 90);
#else
    const uint32_t high8 = static_cast<uint32_t>(value / 100000000u);
#endif
    const uint32_t low8 = static_cast<uint32_t>(value - uint64_t(high8) * 100000000u);
    const uint32x2_t chunks = vcreate_u32(uint64_t(high8) | uint64_t(low8) << 32);

    const int32x2_t q10000 = vshr_n_s32(
        vqdmulh_s32(vreinterpret_s32_u32(chunks), vdup_n_s32(0x068db8bb)), 9);
    const int32x2_t groups100 = vmla_s32(
        vreinterpret_s32_u32(chunks), q10000, vdup_n_s32(0x10000 - 10000));

    const int32x4_t zero = vdupq_n_s32(0);
    const uint16x8x2_t interleaved = vzipq_u16(
        vreinterpretq_u16_s32(vcombine_s32(groups100, vdup_n_s32(0))),
        vreinterpretq_u16_s32(zero));
    const int32x4_t widened = vreinterpretq_s32_u16(interleaved.val[0]);
    const int32x4_t q100 =
        vqdmulhq_s32(widened, vdupq_n_s32(0x0147b000));
    const int32x4_t pairs =
        vmlaq_s32(widened, q100, vdupq_n_s32(0x10000 - 100));

    const int16x8_t q10 = vqdmulhq_s16(vreinterpretq_s16_s32(pairs),
                                        vdupq_n_s16(0x0ce0));
    const int16x8_t digits = vmlaq_s16(
        vreinterpretq_s16_s8(vaddq_s8(vreinterpretq_s8_s32(pairs), vdupq_n_s8('0'))),
        q10, vdupq_n_s16(0x100 - 10));
    const uint8x16_t ordered = vrev64q_u8(vreinterpretq_u8_s16(digits));
    vst1q_u8(reinterpret_cast<uint8_t*>(out), ordered);
#endif
}

template <unsigned Radix, unsigned Len>
    requires (Radix == 10 && Len == 16)
inline void int2str(char* out, result_t<Radix, Len> value) noexcept {
    Convert(out, static_cast<uint64_t>(value));
}

}  // namespace decimal16_candidate
#endif

namespace {

constexpr unsigned kStride = 144;  // 128 chars + slack for wide stores

const char* kDigits = "0123456789abcdefghijklmnopqrstuvwxyz";

uint64_t gSink = 0;
unsigned gFailures = 0;

uint64_t Rand() noexcept {
    static uint64_t s = 0x9e3779b97f4a7c15ull;
    s ^= s << 13;
    s ^= s >> 7;
    s ^= s << 17;
    return s;
}

// COUNT random values with a non-zero leading digit, plus their strings.
struct Corpus {
    std::vector<fastconv_widest_uint_t> value;
    std::vector<std::string> want;
    mutable std::vector<char> out;

    Corpus(unsigned radix, unsigned len)
        : value(COUNT), want(COUNT), out(COUNT * kStride, '\0') {
        for (unsigned i = 0; i < COUNT; ++i) {
            std::string s;
            fastconv_widest_uint_t v = 0;
            for (unsigned j = 0; j < len; ++j) {
                const unsigned d = j == 0 ? 1 + Rand() % (radix - 1) : Rand() % radix;
                s += kDigits[d];
                v = v * radix + d;
            }
            value[i] = v;
            want[i] = s;
        }
    }
};

enum Column {
    kSnprintf,
    kItoa,
    kToString,
    kFormat,
    kToChars,
    kV0,
    kFastconv,
    kQuad,
    kSimd,
    kDecimal16,
    kNumColumns,
};

const char* kColumnNames[kNumColumns] = {
    "snprintf", "itoa", "to_string", "format", "to_chars", "v0", "fastconv",
    "quad", "simd", "decimal16",
};

struct Cell {
    bool valid = false;
    bool wrong = false;
    double mean = 0;
};

struct TableRow {
    unsigned radix;
    unsigned len;
    std::array<Cell, kNumColumns> cells;
};

std::vector<TableRow> gTable;

// Folds the whole written field so that no digit can be eliminated as dead.
template <unsigned Len>
inline uint64_t FoldOut(const char* p) noexcept {
    uint64_t acc = 0;
    for (unsigned i = 0; i < (Len + 7) / 8; ++i) {
        uint64_t chunk;
        std::memcpy(&chunk, p + i * 8, sizeof(chunk));
        acc ^= chunk;
    }
    return acc;
}

// f is called as f(destination, corpus_index).
template <typename F>
Cell Measure(const Corpus& corpus, unsigned len, F f) {
    using Clock = std::chrono::steady_clock;

    static volatile unsigned zero = 0;

    Cell cell;
    for (unsigned i = 0; i < COUNT; ++i) {
        char* p = corpus.out.data() + i * kStride;
        std::memset(p, '?', kStride);
        f(p, i);
        if (std::string(p, len) != corpus.want[i]) {
            cell.wrong = true;
            ++gFailures;
            return cell;
        }
    }

    unsigned reps = 1;
    for (unsigned attempt = 0; attempt < 20; ++attempt) {
        uint64_t acc = 0;
        const auto t0 = Clock::now();
        for (unsigned rep = 0; rep < reps; ++rep) {
            char* base = corpus.out.data() + zero;
            for (unsigned i = 0; i < COUNT; ++i) {
                acc += f(base + i * kStride, i);
            }
        }
        const double us = std::chrono::duration<double, std::micro>(Clock::now() - t0).count();
        gSink += acc;
        if (us >= TARGET_US * 0.75) {
            break;
        }
        reps = us > 0 ? std::max(reps + 1, unsigned(reps * (TARGET_US / us))) : reps * 2;
    }

    std::vector<double> raw;
    raw.reserve(TRIALS);
    for (unsigned trial = 0; trial < TRIALS; ++trial) {
        uint64_t acc = 0;
        const auto t0 = Clock::now();
        for (unsigned rep = 0; rep < reps; ++rep) {
            char* base = corpus.out.data() + zero;
            for (unsigned i = 0; i < COUNT; ++i) {
                acc += f(base + i * kStride, i);
            }
        }
        const auto t1 = Clock::now();
        gSink += acc;
        raw.push_back(std::chrono::duration<double, std::nano>(t1 - t0).count() /
                      (double(reps) * COUNT));
    }

    std::vector<double> samples;
    for (std::size_t i = 0; i + BLOCK <= raw.size(); i += BLOCK) {
        samples.push_back(*std::min_element(raw.begin() + i, raw.begin() + i + BLOCK));
    }

    double sum = 0;
    for (double s : samples) {
        sum += s;
    }
    cell.mean = sum / samples.size();
    cell.valid = true;
    return cell;
}

#define MEASURE(NS, COL, R, L)                                                             \
    if constexpr (requires { NS::int2str<R, L>(nullptr, result_t<R, L>{}); }) {             \
        row.cells[COL] = Measure(corpus, L, [&corpus](char* p, unsigned i) {                \
            NS::int2str<R, L>(p, static_cast<result_t<R, L>>(corpus.value[i]));             \
            return FoldOut<L>(p);                                                           \
        });                                                                                 \
    }

template <unsigned R, unsigned L>
void Row() {
    if constexpr (requires { fastconv::int2str<R, L>(nullptr, result_t<R, L>{}); }) {
        const Corpus corpus(R, L);
        TableRow row{R, L, {}};

        if constexpr (BitsNeeded(R, L) <= 64) {
            // printf only knows octal, decimal and lower case hex.
            if constexpr (R == 8 || R == 10 || R == 16) {
                row.cells[kSnprintf] = Measure(corpus, L, [&corpus](char* p, unsigned i) {
                    constexpr const char* kFormat = R == 8 ? "%llo" : R == 10 ? "%llu" : "%llx";
                    std::snprintf(p, kStride, kFormat,
                                  static_cast<unsigned long long>(corpus.value[i]));
                    return FoldOut<L>(p);
                });
            }
#if defined(_WIN32)
            row.cells[kItoa] = Measure(corpus, L, [&corpus](char* p, unsigned i) {
                _ui64toa_s(static_cast<unsigned long long>(corpus.value[i]), p, kStride, R);
                return FoldOut<L>(p);
            });
#endif
            if constexpr (R == 10) {
                row.cells[kToString] = Measure(corpus, L, [&corpus](char* p, unsigned i) {
                    const std::string s =
                        std::to_string(static_cast<unsigned long long>(corpus.value[i]));
                    std::memcpy(p, s.data(), s.size());
                    return FoldOut<L>(p);
                });
            }
            // std::format adds binary to printf's set but still nothing else.
            if constexpr (R == 2 || R == 8 || R == 10 || R == 16) {
                row.cells[kFormat] = Measure(corpus, L, [&corpus](char* p, unsigned i) {
                    const auto v = static_cast<unsigned long long>(corpus.value[i]);
                    if constexpr (R == 2) {
                        std::format_to(p, "{:b}", v);
                    } else if constexpr (R == 8) {
                        std::format_to(p, "{:o}", v);
                    } else if constexpr (R == 10) {
                        std::format_to(p, "{}", v);
                    } else {
                        std::format_to(p, "{:x}", v);
                    }
                    return FoldOut<L>(p);
                });
            }
            row.cells[kToChars] = Measure(corpus, L, [&corpus](char* p, unsigned i) {
                std::to_chars(p, p + L, static_cast<unsigned long long>(corpus.value[i]), R);
                return FoldOut<L>(p);
            });
        }
        MEASURE(v0, kV0, R, L)
        MEASURE(fastconv, kFastconv, R, L)
        MEASURE(quad_candidate, kQuad, R, L)
#ifdef FASTCONV_INT2STR_HAS_SIMD
        MEASURE(simd, kSimd, R, L)
        MEASURE(decimal16_candidate, kDecimal16, R, L)
#endif
        gTable.push_back(row);
        std::fflush(stdout);
    }
}

template <unsigned R>
void Rows() {
    Row<R, 4>();
    Row<R, 8>();
    Row<R, 16>();
    Row<R, 32>();
    Row<R, 64>();
    Row<R, 128>();
}

constexpr unsigned kRadixOrder[] = {2,  4,  8,  16, 32, 3,  5,  6,  7,  9,  10, 11,
                                    12, 13, 14, 15, 17, 18, 19, 20, 21, 22, 23, 24,
                                    25, 26, 27, 28, 29, 30, 31, 33, 34, 35, 36};
constexpr unsigned kNumRadices = sizeof(kRadixOrder) / sizeof(kRadixOrder[0]);

template <unsigned I>
void RowsByIndex() {
    if constexpr (I < kNumRadices) {
        Rows<kRadixOrder[I]>();
        RowsByIndex<I + 1>();
    }
}

// Without this the first rows are timed while the CPU is still ramping its
// clock, which inflated the whole radix 2 block by up to 2x.
void WarmUp() {
    using Clock = std::chrono::steady_clock;

    const auto t0 = Clock::now();
    uint64_t x = 1;
    while (std::chrono::duration<double, std::milli>(Clock::now() - t0).count() < 300) {
        for (unsigned i = 0; i < 10000; ++i) {
            x = x * 6364136223846793005u + 1442695040888963407u;
        }
    }
    gSink += x;
}

std::string Format(const Cell& c) {
    if (c.wrong) {
        return "WRONG";
    }
    if (!c.valid) {
        return "-";
    }
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.2f", c.mean);
    return buf;
}

std::string Ratio(const Cell& c, const Cell& base) {
    if (!c.valid || !base.valid || c.wrong) {
        return "-";
    }
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.2fx", base.mean / c.mean);
    return buf;
}

void PrintCpu() {
    std::printf("cpu      : ");
#if defined(__APPLE__)
    char brand[256] = "unknown";
    std::size_t n = sizeof(brand);
    if (sysctlbyname("machdep.cpu.brand_string", brand, &n, nullptr, 0) != 0) {
        n = sizeof(brand);
        sysctlbyname("hw.model", brand, &n, nullptr, 0);
    }
    std::printf("%s\n", brand);
#else
    std::printf("unknown\n");
#endif
}

void PrintHeader(const char* title, int width) {
    std::printf("\n%s\n", title);
    std::printf("%-11s", "radix len");
    for (int c = 0; c < kNumColumns; ++c) {
        std::printf("%*s", width, kColumnNames[c]);
    }
    std::printf("\n%-11s", "---------");
    for (int c = 0; c < kNumColumns; ++c) {
        std::printf("%*s", width, "----------");
    }
    std::printf("\n");
}

}  // namespace

int main() {
    std::printf("compiler : ");
#if defined(__clang__) && defined(_MSC_VER)
    std::printf("clang-cl %d.%d.%d (MSVC ABI %d)", __clang_major__, __clang_minor__,
                __clang_patchlevel__, _MSC_VER);
#elif defined(_MSC_VER)
    std::printf("msvc %d", _MSC_VER);
#elif defined(__clang__)
    std::printf("clang %d.%d.%d", __clang_major__, __clang_minor__, __clang_patchlevel__);
#elif defined(__GNUC__)
    std::printf("gcc %d.%d.%d", __GNUC__, __GNUC_MINOR__, __GNUC_PATCHLEVEL__);
#else
    std::printf("unknown");
#endif
    std::printf("\ntarget   : ");
#if defined(__x86_64__) || defined(_M_X64)
    std::printf("x86-64");
#elif defined(__aarch64__) || defined(_M_ARM64)
    std::printf("aarch64");
#else
    std::printf("unknown");
#endif
#ifdef FASTCONV_INT2STR_HAS_SIMD
    std::printf(" (simd namespace enabled)");
#else
    std::printf(" (simd namespace DISABLED)");
#endif
    std::printf("\nuint128  : %s", FASTCONV_HAS_NATIVE_UINT128 ? "native" : "unavailable");
    std::printf("\nsampling : TRIALS=%d BLOCK=%d (=> %d samples) TARGET_US=%d COUNT=%d\n", TRIALS,
                BLOCK, TRIALS / BLOCK, TARGET_US, COUNT);
    PrintCpu();

    WarmUp();
    RowsByIndex<0>();

    PrintHeader("[ns/conv]", 11);
    for (const TableRow& r : gTable) {
        std::printf("%-4u%-7u", r.radix, r.len);
        for (int c = 0; c < kNumColumns; ++c) {
            std::printf("%11s", Format(r.cells[c]).c_str());
        }
        std::printf("\n");
    }

    PrintHeader("[speedup vs v0]", 11);
    for (const TableRow& r : gTable) {
        std::printf("%-4u%-7u", r.radix, r.len);
        for (int c = 0; c < kNumColumns; ++c) {
            std::printf("%11s", Ratio(r.cells[c], r.cells[kV0]).c_str());
        }
        std::printf("\n");
    }

    std::printf("\nrows %zu   sink %llu\n", gTable.size(), (unsigned long long)gSink);
    if (gFailures) {
        std::printf("CORRECTNESS FAILURES: %u\n", gFailures);
        return 1;
    }
    std::printf("all variants agree with the reference\n");
    return 0;
}
