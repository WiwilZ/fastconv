#pragma once

#include <bit>
#include <concepts>
#include <cstdint>
#include <cstring>

#include <limits>
#include <type_traits>


#if defined(__SIZEOF_INT128__) && !defined(_MSC_VER) && \
    !defined(FASTCONV_DISABLE_UINT128)
#define FASTCONV_HAS_NATIVE_UINT128 1
// `__extension__` keeps -Wpedantic -Werror builds usable while retaining the
// compiler's native type on GCC/Clang targets that provide it.
__extension__ typedef __int128 fastconv_int128_t;
__extension__ typedef unsigned __int128 fastconv_uint128_t;
using fastconv_widest_uint_t = fastconv_uint128_t;
inline constexpr unsigned kFastconvMaxBits = 128;
#else
// No software 128-bit alias is exposed in this configuration.  Fixed-width
// conversions wider than 64 bits are intentionally unavailable, while all
// supported narrower paths use 64-bit limbs/intrinsics instead of a hidden
// 128-bit intermediate.
#define FASTCONV_HAS_NATIVE_UINT128 0
using fastconv_widest_uint_t = uint64_t;
inline constexpr unsigned kFastconvMaxBits = 64;
#endif

// Older libstdc++ releases do not classify __int128 as `std::integral` in a
// strict `-std=c++23` build, even though the compiler supports the type. Keep
// that implementation detail out of the public constraints. The explicit
// native-type cases are compiled only when the type itself is available; an
// explicit FASTCONV_DISABLE_UINT128 build has no 128-bit alias to route here.
#if FASTCONV_HAS_NATIVE_UINT128
template <typename T>
concept FastconvNativeInteger =
    std::same_as<std::remove_cv_t<T>, fastconv_int128_t> ||
    std::same_as<std::remove_cv_t<T>, fastconv_uint128_t>;

template <typename T>
inline constexpr bool FastconvIsSigned =
    std::is_signed_v<std::remove_cv_t<T>> ||
    std::same_as<std::remove_cv_t<T>, fastconv_int128_t>;

template <typename T>
inline constexpr bool FastconvIsUnsigned =
    std::is_unsigned_v<std::remove_cv_t<T>> ||
    std::same_as<std::remove_cv_t<T>, fastconv_uint128_t>;
#else
template <typename T>
concept FastconvNativeInteger = false;

template <typename T>
inline constexpr bool FastconvIsSigned = std::is_signed_v<std::remove_cv_t<T>>;

template <typename T>
inline constexpr bool FastconvIsUnsigned = std::is_unsigned_v<std::remove_cv_t<T>>;
#endif

template <typename T>
concept FastconvInteger =
    (std::integral<std::remove_cv_t<T>> || FastconvNativeInteger<T>) &&
    (sizeof(std::remove_cv_t<T>) <= sizeof(fastconv_widest_uint_t));

template <typename T>
concept FastconvSignedInteger = FastconvInteger<T> && FastconvIsSigned<T>;

template <typename T>
concept FastconvUnsignedInteger =
    FastconvInteger<T> && FastconvIsUnsigned<T> &&
    !std::same_as<std::remove_cv_t<T>, bool>;

template <typename T>
struct FastconvUnsignedType {
    using type = std::make_unsigned_t<std::remove_cv_t<T>>;
};

#if FASTCONV_HAS_NATIVE_UINT128
template <>
struct FastconvUnsignedType<fastconv_int128_t> {
    using type = fastconv_uint128_t;
};

template <>
struct FastconvUnsignedType<fastconv_uint128_t> {
    using type = fastconv_uint128_t;
};
#endif

template <typename T>
using fastconv_unsigned_t =
    typename FastconvUnsignedType<std::remove_cv_t<T>>::type;

constexpr bool IsValidRadix(unsigned radix) noexcept {
    return radix >= 2 && radix <= 36;
}

#if defined(__SSE4_1__) || defined(__AVX2__) || defined(__AVX512BW__) || \
    defined(_M_SSE4_1) || defined(_M_AVX2) || defined(_M_AVX512BW)
#define FASTCONV_X86_SIMD 1
#endif

#if defined(__ARM_NEON) || defined(__ARM_NEON__) || defined(__aarch64__) || \
    defined(_M_ARM64) || defined(_M_ARM64EC)
#define FASTCONV_ARM_SIMD 1
#endif

// Normalize compiler-specific instruction feature macros for the SIMD
// implementation. GCC, Clang, and MSVC generally expose the double-
// underscore spellings; the `_M_*` aliases also cover MSVC-compatible
// frontends and build systems that provide them.
#if defined(__AVX2__) || defined(_M_AVX2)
#define FASTCONV_X86_AVX2 1
#endif

#if defined(__AVX512BW__) || defined(_M_AVX512BW)
#define FASTCONV_X86_AVX512BW 1
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
    T ans = static_cast<T>(1);
    T factor = static_cast<T>(Radix);
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
