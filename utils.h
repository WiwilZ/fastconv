#pragma once

#include <bit>
#include <cstdint>
#include <cstring>

#include <limits>
#include <type_traits>


#if defined(__SIZEOF_INT128__) && !defined(_MSC_VER) && \
    !defined(FASTCONV_DISABLE_UINT128)
#define FASTCONV_HAS_NATIVE_UINT128 1
using fastconv_uint128_t = unsigned __int128;
using fastconv_widest_uint_t = fastconv_uint128_t;
inline constexpr unsigned kFastconvMaxBits = 128;
#else
#define FASTCONV_HAS_NATIVE_UINT128 0
using fastconv_widest_uint_t = uint64_t;
inline constexpr unsigned kFastconvMaxBits = 64;
#endif

#if defined(__SSE4_1__) || defined(__AVX2__)
#define FASTCONV_X86_SIMD 1
#endif

#if defined(__ARM_NEON) || defined(__ARM_NEON__) || defined(__aarch64__)
#define FASTCONV_ARM_SIMD 1
#endif


template <typename T>
constexpr T Load(const char* str) noexcept {
    T v = 0;
    if consteval {
        for (std::size_t i = 0; i < sizeof(T); ++i) {
            v |= static_cast<T>(static_cast<uint8_t>(str[i])) << (i * 8);
        }
    } else {
        if constexpr (std::endian::native == std::endian::little) {
            std::memcpy(&v, str, sizeof(T));
        } else {
            for (std::size_t i = 0; i < sizeof(T); ++i) {
                v |= static_cast<T>(static_cast<uint8_t>(str[i])) << (i * 8);
            }
        }
    }
    return v;
}

template <typename T>
constexpr void Store(char* p, T v) noexcept {
    if consteval {
        for (std::size_t i = 0; i < sizeof(T); ++i) {
            p[i] = static_cast<char>(v >> (i * 8));
        }
    } else {
        if constexpr (std::endian::native == std::endian::little) {
            std::memcpy(p, &v, sizeof(T));
        } else {
            for (std::size_t i = 0; i < sizeof(T); ++i) {
                p[i] = static_cast<char>(v >> (i * 8));
            }
        }
    }
}

template <typename T>
consteval T Pow(unsigned Radix, unsigned exponent) noexcept {
    T ans = 1;
    T factor = Radix;
    while (exponent) {
        if (exponent & 1) {
            ans *= factor;
        }
        factor *= factor;
        exponent >>= 1;
    }
    return ans;
}

template <typename T, unsigned NumBits>
consteval T Broadcast(auto v) noexcept {
    T u = static_cast<std::make_unsigned_t<decltype(v)>>(v);
    T result = 0;
    for (unsigned i = 0; i < std::numeric_limits<T>::digits; i += NumBits) {
        result |= u << i;
    }
    return result;
}

// Number of bits required to hold the largest value of `Len` digits in `Radix`.
// Power-of-two radices are computed exactly because Radix^Len may reach 2^128;
// anything wider than 128 bits reports 129 rather than wrapping silently.
consteval unsigned BitsNeeded(unsigned Radix, unsigned Len) noexcept {
    if ((Radix & (Radix - 1)) == 0) {
        unsigned log2 = 0;
        for (unsigned r = Radix; r > 1; r >>= 1) {
            ++log2;
        }
        return Len * log2;
    }
    // Two-word multiply-by-small keeps this calculation available on MSVC,
    // which has no native 128-bit integer type.
    uint64_t lo = 1;
    uint64_t hi = 0;
    for (unsigned i = 0; i < Len; ++i) {
        const uint64_t low_product = (lo & 0xffffffffu) * Radix;
        const uint64_t high_product = (lo >> 32) * Radix + (low_product >> 32);
        const uint64_t carry = high_product >> 32;
        lo = (high_product << 32) | (low_product & 0xffffffffu);

        if (hi > (std::numeric_limits<uint64_t>::max() - carry) / Radix) {
            return 129;
        }
        hi = hi * Radix + carry;
    }

    unsigned bits = 0;
    uint64_t word = hi ? hi : lo;
    for (; word; word >>= 1) {
        ++bits;
    }
    return hi ? 64 + bits : bits;
}

static_assert(BitsNeeded(2, 128) == 128);
static_assert(BitsNeeded(10, 19) == 64);
static_assert(BitsNeeded(36, 16) == 83);
static_assert(BitsNeeded(36, 25) == 129);

// Smallest unsigned type able to hold `Len` digits in `Radix`.
template <unsigned Radix, unsigned Len>
using result_t = std::conditional_t<BitsNeeded(Radix, Len) <= 8, uint8_t,
                 std::conditional_t<BitsNeeded(Radix, Len) <= 16, uint16_t,
                 std::conditional_t<BitsNeeded(Radix, Len) <= 32, uint32_t,
                 std::conditional_t<BitsNeeded(Radix, Len) <= 64, uint64_t,
#if FASTCONV_HAS_NATIVE_UINT128
                 fastconv_uint128_t
#else
                 void
#endif
                 >>>>;


template <std::size_t NumBytes>
struct pack;

template <std::size_t NumBytes>
using pack_t = pack<NumBytes>::type;

template <>
struct pack<4> {
    using type = uint32_t;
    static constexpr type mask0 = 0x40404040;
    static constexpr type mask1 = 0x0f0f0f0f;
};

template <>
struct pack<8> {
    using type = uint64_t;
    static constexpr type mask0 = 0x4040404040404040;
    static constexpr type mask1 = 0x0f0f0f0f0f0f0f0f;
};

#if FASTCONV_HAS_NATIVE_UINT128
template <>
struct pack<16> {
    using type = fastconv_uint128_t;
    static constexpr type mask0 = static_cast<type>(0x4040404040404040) << 64 |
                                  0x4040404040404040;
    static constexpr type mask1 = static_cast<type>(0x0f0f0f0f0f0f0f0f) << 64 |
                                  0x0f0f0f0f0f0f0f0f;
};
#endif
