#pragma once

#include <cstdint>
#include <cstring>

#include <limits>
#include <type_traits>


template <typename T>
constexpr T Load(const char* str) noexcept {
    T v = 0;
    if consteval {
        for (std::size_t i = 0; i < sizeof(T); ++i) {
            v |= static_cast<T>(static_cast<uint8_t>(str[i])) << (i * 8);
        }
    } else {
        std::memcpy(&v, str, sizeof(T));
    }
    return v;
}

template <typename T>
constexpr void Store(char* p, T v) noexcept {
    if consteval {
        for (std::size_t i = 0; i < sizeof(T); ++i) {
            p[i] = v >> (i * 8);
        }
    } else {
        std::memcpy(p, &v, sizeof(T));
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
    __uint128_t v = 1;
    for (unsigned i = 0; i < Len; ++i) {
        if (v > ~static_cast<__uint128_t>(0) / Radix) {
            return 129;
        }
        v *= Radix;
    }
    unsigned bits = 0;
    for (; v; v >>= 1) {
        ++bits;
    }
    return bits;
}

// Smallest unsigned type able to hold `Len` digits in `Radix`.
template <unsigned Radix, unsigned Len>
using result_t = std::conditional_t<BitsNeeded(Radix, Len) <= 8, uint8_t,
                 std::conditional_t<BitsNeeded(Radix, Len) <= 16, uint16_t,
                 std::conditional_t<BitsNeeded(Radix, Len) <= 32, uint32_t,
                 std::conditional_t<BitsNeeded(Radix, Len) <= 64, uint64_t, __uint128_t>>>>;


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

template <>
struct pack<16> {
    using type = __uint128_t;
    static constexpr type mask0 = static_cast<__uint128_t>(0x4040404040404040) << 64 | 0x4040404040404040;
    static constexpr type mask1 = static_cast<__uint128_t>(0x0f0f0f0f0f0f0f0f) << 64 | 0x0f0f0f0f0f0f0f0f;
};
