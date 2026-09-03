// Benchmark for the str2int algorithm variants, laid out as a 2-D table.
//
//   c++ -std=c++26 -O3 -mcpu=native benchmark_str2int.cpp -o bench && ./bench
//
// Columns : four standard library conversions (the narrowest function of each
//           family that can hold the value), then v0, then now, then simd.
// Rows    : every supported (radix, len) pair.
//
// Each cell converts a 1024-entry corpus; REPS is calibrated so one run lasts
// about TARGET_US. TRIALS runs are timed and grouped into blocks of BLOCK; the
// fastest run of each block becomes one sample, and the samples are averaged.
// Cells are ns per conversion.
// Every cell is checked against a reference Horner loop before it is timed.

#include "str2int.h"

#include <charconv>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <algorithm>
#include <array>
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

namespace {

constexpr unsigned kStride = 144;  // 128 chars + slack, NUL padded

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

// Boundary patterns followed by random strings, plus their expected values.
// NUL padding lets strtoull share the same buffer; no variant ever reads past
// `len` rounded up to its natural load width.
struct Corpus {
    std::vector<char> text;
    std::vector<std::string> strs;  // same values, for the std::stoi family
    std::vector<fastconv_widest_uint_t> want;

    Corpus(unsigned radix, unsigned len) : text(COUNT * kStride, '\0'), want(COUNT) {
        strs.reserve(COUNT);
        for (unsigned i = 0; i < COUNT; ++i) {
            char* p = text.data() + i * kStride;
            fastconv_widest_uint_t v = 0;
            for (unsigned j = 0; j < len; ++j) {
                unsigned d;
                if (i == 0) {
                    d = j == 0 ? 1 : 0;
                } else if (i == 1) {
                    d = radix - 1;
                } else if (i == 2) {
                    d = j == 0 ? 1 : j < len / 2 ? 0 : radix - 1;
                } else if (i == 3) {
                    d = j == 0 || j + 1 == len / 2 ? 1 : 0;
                } else {
                    d = Rand() % radix;
                }
                char c = kDigits[d];
                if (c > '9' && (Rand() & 1)) {
                    c -= 32;  // exercise both letter cases
                }
                p[j] = c;
                v = v * radix + d;
            }
            want[i] = v;
            strs.emplace_back(p, len);
        }
    }
};

enum Column {
    kAtoi,
    kStrtoull,
    kStoi,
    kFromChars,
    kV0,
    kNow,
    kMulLevel,
    kSimd,
    kNumColumns,
};

const char* kColumnNames[kNumColumns] = {
    "atoi", "strtoull", "stoi", "from_chars", "v0", "fastconv", "mullevel", "simd",
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

// Folds a result into the accumulator. Both 64-bit halves are consumed so that
// nothing of a >64-bit conversion can be eliminated as dead; for results that
// fit in 64 bits the high half is a provable zero and costs nothing.
template <typename T>
inline uint64_t Fold(T v) noexcept {
#if FASTCONV_HAS_NATIVE_UINT128
    return static_cast<uint64_t>(v) ^
           static_cast<uint64_t>(static_cast<fastconv_uint128_t>(v) >> 64);
#else
    return static_cast<uint64_t>(v);
#endif
}

// f is called as f(pointer_to_digits, corpus_index); columns that need a
// std::string use the index, the others use the pointer.
template <typename F>
Cell Measure(const Corpus& corpus, F f) {
    using Clock = std::chrono::steady_clock;

    // Read through a volatile zero so the compiler cannot prove the base
    // pointer is identical in every repetition and hoist the inner loop out.
    static volatile unsigned zero = 0;

    Cell cell;
    for (unsigned i = 0; i < COUNT; ++i) {
        if (f(corpus.text.data() + i * kStride, i) != corpus.want[i]) {
            cell.wrong = true;
            ++gFailures;
            return cell;
        }
    }

    // Calibrate REPS so that a trial takes roughly TARGET_US microseconds.
    unsigned reps = 1;
    for (unsigned attempt = 0; attempt < 20; ++attempt) {
        uint64_t acc = 0;
        const auto t0 = Clock::now();
        for (unsigned rep = 0; rep < reps; ++rep) {
            const char* base = corpus.text.data() + zero;
            for (unsigned i = 0; i < COUNT; ++i) {
                acc += Fold(f(base + i * kStride, i));
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
            const char* base = corpus.text.data() + zero;
            for (unsigned i = 0; i < COUNT; ++i) {
                acc += Fold(f(base + i * kStride, i));
            }
        }
        const auto t1 = Clock::now();
        gSink += acc;
        raw.push_back(std::chrono::duration<double, std::nano>(t1 - t0).count() /
                      (double(reps) * COUNT));
    }

    // The fastest run of every block of BLOCK becomes one sample: a sample is
    // only spoiled if the machine was disturbed for BLOCK consecutive runs,
    // which is what makes the figures reproducible.
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

#define MEASURE(NS, COL, R, L)                                                          \
    if constexpr (requires { NS::str2int<R, L>(static_cast<const char*>(nullptr)); }) { \
        row.cells[COL] = Measure(corpus, [](const char* p, unsigned) {                  \
            return static_cast<fastconv_widest_uint_t>(NS::str2int<R, L>(p));         \
        });                                                                             \
    }

template <unsigned R, unsigned L>
void Row() {
    if constexpr (requires { fastconv::str2int<R, L>(static_cast<const char*>(nullptr)); }) {
        constexpr unsigned kBits = BitsNeeded(R, L);
        const Corpus corpus(R, L);
        TableRow row{R, L, {}};

        // The narrowest standard library function that can still hold the value.
        // The atoi family takes no base, so it only applies to radix 10.
        if constexpr (R == 10 && kBits <= 31) {
            row.cells[kAtoi] = Measure(corpus, [](const char* p, unsigned) {
                return static_cast<fastconv_widest_uint_t>(std::atoi(p));
            });
        } else if constexpr (R == 10 && kBits <= 63) {
            row.cells[kAtoi] = Measure(corpus, [](const char* p, unsigned) {
                return static_cast<fastconv_widest_uint_t>(std::atoll(p));
            });
        }
        if constexpr (kBits <= 63) {
            row.cells[kStrtoull] = Measure(corpus, [](const char* p, unsigned) {
                return static_cast<fastconv_widest_uint_t>(std::strtoll(p, nullptr, R));
            });
        } else if constexpr (kBits <= 64) {
            row.cells[kStrtoull] = Measure(corpus, [](const char* p, unsigned) {
                return static_cast<fastconv_widest_uint_t>(std::strtoull(p, nullptr, R));
            });
        }
        if constexpr (kBits <= 31) {
            row.cells[kStoi] = Measure(corpus, [&corpus](const char*, unsigned i) {
                return static_cast<fastconv_widest_uint_t>(std::stoi(corpus.strs[i], nullptr, R));
            });
        } else if constexpr (kBits <= 63) {
            row.cells[kStoi] = Measure(corpus, [&corpus](const char*, unsigned i) {
                return static_cast<fastconv_widest_uint_t>(std::stoll(corpus.strs[i], nullptr, R));
            });
        } else if constexpr (kBits <= 64) {
            row.cells[kStoi] = Measure(corpus, [&corpus](const char*, unsigned i) {
                return static_cast<fastconv_widest_uint_t>(std::stoull(corpus.strs[i], nullptr, R));
            });
        }
        if constexpr (kBits <= 64) {
            row.cells[kFromChars] = Measure(corpus, [](const char* p, unsigned) {
                unsigned long long v = 0;
                std::from_chars(p, p + L, v, R);
                return static_cast<fastconv_widest_uint_t>(v);
            });
        }

        MEASURE(v0, kV0, R, L)
        MEASURE(fastconv, kNow, R, L)
        MEASURE(mullevel, kMulLevel, R, L)
#ifdef FASTCONV_HAS_SIMD
        MEASURE(simd, kSimd, R, L)
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

// Radices in table order: powers of two first, then the rest ascending.
constexpr unsigned kRadixOrder[] = {2,  4,  8,  16, 32, 3,  5,  6,  7,  9,  10, 11,
                                    12, 13, 14, 15, 17, 18, 19, 20, 21, 22, 23, 24,
                                    25, 26, 27, 28, 29, 30, 31, 33, 34, 35, 36};
constexpr unsigned kNumRadices = sizeof(kRadixOrder) / sizeof(kRadixOrder[0]);

// Instantiating all 124 rows at once exceeds Compiler Explorer's build time
// limit, so a build can be restricted to kRadixOrder[SLICE_FROM, SLICE_TO).
// The slices concatenated in order reproduce the full table exactly.
#ifndef SLICE_FROM
#define SLICE_FROM 0
#endif
#ifndef SLICE_TO
#define SLICE_TO kNumRadices
#endif

template <unsigned I>
void RowsByIndex() {
    if constexpr (I < kNumRadices) {
        if constexpr (I >= unsigned(SLICE_FROM) && I < unsigned(SLICE_TO)) {
            Rows<kRadixOrder[I]>();
        }
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

// The machine actually running the code. On Compiler Explorer the executor
// host is not selectable, so every table records which CPU it landed on.
void PrintCpu() {
    std::printf("cpu      : ");
#if defined(__linux__)
    char line[512];
    if (FILE* f = std::fopen("/proc/cpuinfo", "r")) {
        while (std::fgets(line, sizeof(line), f)) {
            if (!std::strncmp(line, "model name", 10)) {
                const char* colon = std::strchr(line, ':');
                std::printf("%s", colon ? colon + 2 : line);
                std::fclose(f);
                return;
            }
        }
        std::fclose(f);
    }
    std::printf("unknown\n");
#elif defined(__APPLE__)
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
#ifdef FASTCONV_HAS_SIMD
    std::printf(" (simd namespace enabled)");
#else
    std::printf(" (simd namespace DISABLED - no SSE4.1/NEON)");
#endif
    std::printf("\nuint128  : %s", FASTCONV_HAS_NATIVE_UINT128 ? "native" : "unavailable");
    std::printf("\nsampling : TRIALS=%d BLOCK=%d (=> %d samples) TARGET_US=%d COUNT=%d\n", TRIALS,
                BLOCK, TRIALS / BLOCK, TARGET_US, COUNT);
    PrintCpu();
    WarmUp();

    // Power-of-two radices first, then everything else in ascending order.
    // SUBSET builds only a representative set of rows. Compiler Explorer picks
    // a random executor host per request out of a pool with several different
    // CPUs, so a table is only comparable if it comes from ONE request; the
    // subset is small enough for that to compile inside CE's build time limit
    // even with trunk compilers.
#ifdef SUBSET
    Row<2, 8>();
    Row<2, 16>();
    Row<2, 64>();
    Row<4, 16>();
    Row<8, 16>();
    Row<16, 16>();
    Row<16, 32>();
    Row<32, 16>();
    Row<3, 16>();
    Row<10, 8>();
    Row<10, 16>();
    Row<15, 32>();
    Row<17, 16>();
    Row<36, 16>();
#else
    RowsByIndex<0>();
#endif

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
