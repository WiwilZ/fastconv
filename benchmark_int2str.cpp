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
    std::vector<__uint128_t> value;
    std::vector<std::string> want;
    mutable std::vector<char> out;

    Corpus(unsigned radix, unsigned len)
        : value(COUNT), want(COUNT), out(COUNT * kStride, '\0') {
        for (unsigned i = 0; i < COUNT; ++i) {
            std::string s;
            __uint128_t v = 0;
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
    kSimd,
    kNumColumns,
};

const char* kColumnNames[kNumColumns] = {
    "snprintf", "itoa", "to_string", "format", "to_chars", "v0", "fastconv", "simd",
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
    if constexpr (requires { v0::int2str<R, L>(nullptr, result_t<R, L>{}); }) {
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
                _ui64toa(static_cast<unsigned long long>(corpus.value[i]), p, R);
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
#ifdef FASTCONV_INT2STR_HAS_SIMD
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
#if defined(__clang__)
    std::printf("clang %d.%d.%d", __clang_major__, __clang_minor__, __clang_patchlevel__);
#elif defined(__GNUC__)
    std::printf("gcc %d.%d.%d", __GNUC__, __GNUC_MINOR__, __GNUC_PATCHLEVEL__);
#else
    std::printf("unknown");
#endif
    std::printf("\ntarget   : ");
#if defined(__x86_64__)
    std::printf("x86-64");
#elif defined(__aarch64__)
    std::printf("aarch64");
#else
    std::printf("unknown");
#endif
#ifdef FASTCONV_INT2STR_HAS_SIMD
    std::printf(" (simd namespace enabled)");
#else
    std::printf(" (simd namespace DISABLED)");
#endif
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
