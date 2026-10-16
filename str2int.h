#pragma once

#include "utils.h"

#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <type_traits>



template <unsigned Radix, unsigned Len>
constexpr auto Impl(const char* str) {
    using T = pack_t<Len>;

    T t = Load<T>(str);
    if constexpr (Radix >= 11) {
        const auto mask = t & pack<Len>::mask0;
        if constexpr (Radix >= 17) {
            t &= mask >> 2 | pack<Len>::mask1;
        }
        t += mask >> 3 | mask >> 6;
    }
    return t;
}


namespace v0 {

#if FASTCONV_HAS_NATIVE_UINT128

/*----------------------------------------2----------------------------------------*/
template <unsigned Radix, unsigned Len>
    requires (Radix == 2 && Len == 4)
constexpr uint8_t str2int(const char* str) noexcept {
    auto t = Impl<Radix, Len>(str);
    t = t << 3 | t >> 6;
    t |= t >> 18;
    return static_cast<uint8_t>(t & 0xf);
}

template <unsigned Radix, unsigned Len>
    requires (Radix == 2 && Len == 8)
constexpr uint8_t str2int(const char* str) noexcept {
    auto t = Impl<Radix, Len>(str);
    t = (t << 7 | t >> 2) & Broadcast<pack_t<Len>, 16>(0x00c0);
    t |= t >> 18;
    t |= t >> 36;
    return static_cast<uint8_t>(t);
}

template <unsigned Radix, unsigned Len>
    requires (Radix == 2 && Len == 16)
constexpr uint16_t str2int(const char* str) noexcept {
    auto t = Impl<Radix, Len>(str);
    t = (t << 15 | t << 6) & Broadcast<pack_t<Len>, 16>(0xc000);
    t |= t >> 18;
    t |= t >> 36;
    t |= t >> 72;
    return static_cast<uint16_t>(t);
}

template <unsigned Radix, unsigned Len>
    requires (Radix == 2 && Len == 32)
constexpr uint32_t str2int(const char* str) noexcept {
    return static_cast<uint32_t>(str2int<2, 16>(str)) << 16 | str2int<2, 16>(str + 16);
}

template <unsigned Radix, unsigned Len>
    requires (Radix == 2 && Len == 64)
constexpr uint64_t str2int(const char* str) noexcept {
    return static_cast<uint64_t>(str2int<2, 32>(str)) << 32 | str2int<2, 32>(str + 32);
}

template <unsigned Radix, unsigned Len>
    requires (Radix == 2 && Len == 128 && BitsNeeded(Radix, Len) <= kFastconvMaxBits)
constexpr result_t<Radix, Len> str2int(const char* str) noexcept {
    using R = result_t<Radix, Len>;
    return static_cast<R>(str2int<2, 64>(str)) << 64 |
           str2int<2, 64>(str + 64);
}
/*----------------------------------------2----------------------------------------*/

/*----------------------------------------4----------------------------------------*/
template <unsigned Radix, unsigned Len>
    requires (Radix == 4 && Len == 4)
constexpr uint8_t str2int(const char* str) noexcept {
    auto t = Impl<Radix, Len>(str);
    t = (t << 6 | t >> 4) & Broadcast<pack_t<Len>, 16>(0x00f0);
    t |= t >> 20;
    return static_cast<uint8_t>(t);
}

template <unsigned Radix, unsigned Len>
    requires (Radix == 4 && Len == 8)
constexpr uint16_t str2int(const char* str) noexcept {
    auto t = Impl<Radix, Len>(str);
    t = (t << 14 | t << 4) & Broadcast<pack_t<Len>, 16>(0xf000);
    t |= t >> 20;
    t |= t >> 40;
    return static_cast<uint16_t>(t);
}

template <unsigned Radix, unsigned Len>
    requires (Radix == 4 && Len == 16)
constexpr uint32_t str2int(const char* str) noexcept {
    auto t = Impl<Radix, Len>(str);
    t = (t << 6 | t >> 4) & Broadcast<pack_t<Len>, 16>(0x00f0);
    t = (t | t >> 20) << 24;
    t = (t | t >> 40) & Broadcast<pack_t<Len>, 64>(0x00000000ffff0000);
    t |= t >> 80;
    return static_cast<uint32_t>(t);
}

template <unsigned Radix, unsigned Len>
    requires (Radix == 4 && Len == 32)
constexpr uint64_t str2int(const char* str) noexcept {
    return static_cast<uint64_t>(str2int<4, 16>(str)) << 32 | str2int<4, 16>(str + 16);
}

template <unsigned Radix, unsigned Len>
    requires (Radix == 4 && Len == 64 && BitsNeeded(Radix, Len) <= kFastconvMaxBits)
constexpr result_t<Radix, Len> str2int(const char* str) noexcept {
    using R = result_t<Radix, Len>;
    return static_cast<R>(str2int<4, 32>(str)) << 64 |
           str2int<4, 32>(str + 32);
}
/*----------------------------------------4----------------------------------------*/

/*----------------------------------------8----------------------------------------*/
template <unsigned Radix, unsigned Len>
    requires (Radix == 8 && Len == 4)
constexpr uint16_t str2int(const char* str) noexcept {
    auto t = Impl<Radix, Len>(str) & Broadcast<pack_t<Len>, 8>(0x07);
    t = (t << 9 | t >> 2) & Broadcast<pack_t<Len>, 16>(0x0fc0);
    t |= t >> 22;
    return static_cast<uint16_t>(t);
}

template <unsigned Radix, unsigned Len>
    requires (Radix == 8 && Len == 8)
constexpr uint32_t str2int(const char* str) noexcept {
    auto t = Impl<Radix, Len>(str) & Broadcast<pack_t<Len>, 8>(0x07);
    t = (t | t << 11) & Broadcast<pack_t<Len>, 16>(0x3f00);
    t = (t | t >> 22) & Broadcast<pack_t<Len>, 32>(0x00003ffc);
    t = t << 10 | t >> 34;
    return static_cast<uint32_t>(t);
}

template <unsigned Radix, unsigned Len>
    requires (Radix == 8 && Len == 16)
constexpr uint64_t str2int(const char* str) noexcept {
    auto t = Impl<Radix, Len>(str) & Broadcast<pack_t<Len>, 8>(0x07);
    t = (t | t << 11) & Broadcast<pack_t<Len>, 16>(0x3f00);
    t = (t | t >> 22) & Broadcast<pack_t<Len>, 32>(0x00003ffc);
    t = (t << 34 | t >> 10) & Broadcast<pack_t<Len>, 64>(0x0000ffffff000000);
    t |= t >> 88;
    return static_cast<uint64_t>(t);
}

template <unsigned Radix, unsigned Len>
    requires (Radix == 8 && Len == 32 && BitsNeeded(Radix, Len) <= kFastconvMaxBits)
constexpr result_t<Radix, Len> str2int(const char* str) noexcept {
    using R = result_t<Radix, Len>;
    return static_cast<R>(str2int<8, 16>(str)) << 48 |
           str2int<8, 16>(str + 16);
}
/*----------------------------------------8----------------------------------------*/

/*----------------------------------------16----------------------------------------*/
template <unsigned Radix, unsigned Len>
    requires (Radix == 16 && Len == 4)
constexpr uint16_t str2int(const char* str) noexcept {
    auto t = Impl<Radix, Len>(str) & Broadcast<pack_t<Len>, 8>(0x0f);
    t = (t | t << 12) & Broadcast<pack_t<Len>, 16>(0xff00);
    t |= t >> 24;
    return static_cast<uint16_t>(t);
}

template <unsigned Radix, unsigned Len>
    requires (Radix == 16 && Len == 8)
constexpr uint32_t str2int(const char* str) noexcept {
    auto t = Impl<Radix, Len>(str) & Broadcast<pack_t<Len>, 8>(0x0f);
    t = (t | t << 12) & Broadcast<pack_t<Len>, 16>(0xff00);
    t = ((t | t >> 24) & Broadcast<pack_t<Len>, 32>(0x0000ffff)) << 16;
    t |= t >> 48;
    return static_cast<uint32_t>(t);
}

template <unsigned Radix, unsigned Len>
    requires (Radix == 16 && Len == 16)
constexpr uint64_t str2int(const char* str) noexcept {
    auto t = Impl<Radix, Len>(str) & Broadcast<pack_t<Len>, 8>(0x0f);
    t = (t | t << 12) & Broadcast<pack_t<Len>, 16>(0xff00);
    t = ((t | t >> 24) & Broadcast<pack_t<Len>, 32>(0x0000ffff)) << 16;
    t = ((t | t >> 48) & Broadcast<pack_t<Len>, 64>(0x00000000ffffffff)) << 32;
    t |= t >> 96;
    return static_cast<uint64_t>(t);
}

template <unsigned Radix, unsigned Len>
    requires (Radix == 16 && Len == 32 && BitsNeeded(Radix, Len) <= kFastconvMaxBits)
constexpr result_t<Radix, Len> str2int(const char* str) noexcept {
    using R = result_t<Radix, Len>;
    return static_cast<R>(str2int<16, 16>(str)) << 64 |
           str2int<16, 16>(str + 16);
}
/*----------------------------------------16----------------------------------------*/

/*----------------------------------------32----------------------------------------*/
template <unsigned Radix, unsigned Len>
    requires (Radix == 32 && Len == 4)
constexpr uint32_t str2int(const char* str) noexcept {
    auto t = Impl<Radix, Len>(str);
    t = (t << 5 & Broadcast<pack_t<Len>, 16>(0x03e0)) | (t >> 8 & Broadcast<pack_t<Len>, 16>(0x001f));
    t = (t << 10 | t >> 16) & 0x000fffffu;
    return t;
}

template <unsigned Radix, unsigned Len>
    requires (Radix == 32 && Len == 8)
constexpr uint64_t str2int(const char* str) noexcept {
    auto t = Impl<Radix, Len>(str);
    t = (t << 5 & Broadcast<pack_t<Len>, 16>(0x03e0)) | (t >> 8 & Broadcast<pack_t<Len>, 16>(0x001f));
    t = (t << 10 & Broadcast<pack_t<Len>, 32>(0x000ffcc0)) | (t >> 16 & Broadcast<pack_t<Len>, 32>(0x000003ff));
    t = (t << 20 | t >> 32) & 0x000000ffffffffffull;
    return t;
}

template <unsigned Radix, unsigned Len>
    requires (Radix == 32 && Len == 16)
constexpr fastconv_uint128_t str2int(const char* str) noexcept {
    auto t = Impl<Radix, Len>(str);
    t = (t << 5 & Broadcast<pack_t<Len>, 16>(0x03e0)) | (t >> 8 & Broadcast<pack_t<Len>, 16>(0x001f));
    t = (t << 10 & Broadcast<pack_t<Len>, 32>(0x000ffcc0)) | (t >> 16 & Broadcast<pack_t<Len>, 32>(0x000003ff));
    t = (t << 20 & Broadcast<pack_t<Len>, 64>(0x000000fffff00000)) | (t >> 32 & Broadcast<pack_t<Len>, 64>(0x00000000000fffff));
    t = (t << 40 | t >> 64) & ((static_cast<fastconv_uint128_t>(1) << 80) - 1);
    return t;
}
/*----------------------------------------32----------------------------------------*/

/*----------------------------------------3----------------------------------------*/
template <unsigned Radix, unsigned Len>
    requires (Radix == 3 && Len == 4)
constexpr uint8_t str2int(const char* str) noexcept {
    auto t = Impl<Radix, Len>(str);
    t = (t & Broadcast<pack_t<Len>, 16>(0x000f)) * 3 + (t >> 8 & Broadcast<pack_t<Len>, 16>(0x000f));
    return static_cast<uint8_t>(t) * 9 + static_cast<uint8_t>(t >> 16);
}

template <unsigned Radix, unsigned Len>
    requires (Radix == 3 && Len == 8)
constexpr uint16_t str2int(const char* str) noexcept {
    auto t = Impl<Radix, Len>(str);
    t = (t & Broadcast<pack_t<Len>, 16>(0x000f)) * 3 + (t >> 8 & Broadcast<pack_t<Len>, 16>(0x000f));
    t = (t & Broadcast<pack_t<Len>, 32>(0x0000ffff)) * 9 + (t >> 16 & Broadcast<pack_t<Len>, 32>(0x0000ffff));
    return static_cast<uint16_t>(t) * 81 + static_cast<uint16_t>(t >> 32);
}

template <unsigned Radix, unsigned Len>
    requires (Radix == 3 && Len == 16)
constexpr uint32_t str2int(const char* str) noexcept {
    auto t = Impl<Radix, Len>(str);
    t = (t & Broadcast<pack_t<Len>, 16>(0x000f)) * 3 + (t >> 8 & Broadcast<pack_t<Len>, 16>(0x000f));
    t = (t & Broadcast<pack_t<Len>, 32>(0x0000ffff)) * 9 + (t >> 16 & Broadcast<pack_t<Len>, 32>(0x0000ffff));
    t = (t & Broadcast<pack_t<Len>, 64>(0x00000000ffffffff)) * 81 + (t >> 32 & Broadcast<pack_t<Len>, 64>(0x00000000ffffffff));
    return static_cast<uint32_t>(t) * 6561 + static_cast<uint32_t>(t >> 64);
}

template <unsigned Radix, unsigned Len>
    requires (Radix == 3 && Len == 32)
constexpr uint64_t str2int(const char* str) noexcept {
    constexpr auto r16 = Pow<uint64_t>(Radix, 16);
    return static_cast<uint64_t>(str2int<3, 16>(str)) * r16 + str2int<3, 16>(str + 16);
}

template <unsigned Radix, unsigned Len>
    requires (Radix == 3 && Len == 64)
constexpr fastconv_uint128_t str2int(const char* str) noexcept {
    constexpr auto r32 = Pow<fastconv_uint128_t>(Radix, 32);
    return static_cast<fastconv_uint128_t>(str2int<3, 32>(str)) * r32 +
           str2int<3, 32>(str + 32);
}
/*----------------------------------------3----------------------------------------*/

/*----------------------------------------[5, 7]U[9, 15]----------------------------------------*/
template <unsigned Radix, unsigned Len>
    requires (((5 <= Radix && Radix <= 7) || (9 <= Radix && Radix <= 15)) && Len == 4)
constexpr uint16_t str2int(const char* str) noexcept {
    constexpr uint16_t r1 = Radix;
    constexpr uint16_t r2 = r1 * r1;

    auto t = Impl<Radix, Len>(str);
    t = (t & Broadcast<pack_t<Len>, 16>(0x000f)) * r1 + (t >> 8 & Broadcast<pack_t<Len>, 16>(0x000f));
    return static_cast<uint16_t>(t) * r2 + static_cast<uint16_t>(t >> 16);
}

template <unsigned Radix, unsigned Len>
    requires (((5 <= Radix && Radix <= 7) || (9 <= Radix && Radix <= 15)) && Len == 8)
constexpr uint32_t str2int(const char* str) noexcept {
    constexpr uint32_t r1 = Radix;
    constexpr uint32_t r2 = r1 * r1;
    constexpr uint32_t r4 = r2 * r2;

    auto t = Impl<Radix, Len>(str);
    t = (t & Broadcast<pack_t<Len>, 16>(0x000f)) * r1 + (t >> 8 & Broadcast<pack_t<Len>, 16>(0x000f));
    t = (t & Broadcast<pack_t<Len>, 32>(0x0000ffff)) * r2 + (t >> 16 & Broadcast<pack_t<Len>, 32>(0x0000ffff));
    return static_cast<uint32_t>(t) * r4 + static_cast<uint32_t>(t >> 32);
}

template <unsigned Radix, unsigned Len>
    requires (((5 <= Radix && Radix <= 7) || (9 <= Radix && Radix <= 15)) && Len == 16)
constexpr uint64_t str2int(const char* str) noexcept {
    constexpr uint64_t r1 = Radix;
    constexpr uint64_t r2 = r1 * r1;
    constexpr uint64_t r4 = r2 * r2;
    constexpr uint64_t r8 = r4 * r4;

    auto t = Impl<Radix, Len>(str);
    t = (t & Broadcast<pack_t<Len>, 16>(0x000f)) * r1 + (t >> 8 & Broadcast<pack_t<Len>, 16>(0x000f));
    t = (t & Broadcast<pack_t<Len>, 32>(0x0000ffff)) * r2 + (t >> 16 & Broadcast<pack_t<Len>, 32>(0x0000ffff));
    t = (t & Broadcast<pack_t<Len>, 64>(0x00000000ffffffff)) * r4 + (t >> 32 & Broadcast<pack_t<Len>, 64>(0x00000000ffffffff));
    return static_cast<uint64_t>(t) * r8 + static_cast<uint64_t>(t >> 64);
}

template <unsigned Radix, unsigned Len>
    requires (((5 <= Radix && Radix <= 7) || (9 <= Radix && Radix <= 15)) && Len == 32)
constexpr fastconv_uint128_t str2int(const char* str) noexcept {
    constexpr auto r16 = Pow<fastconv_uint128_t>(Radix, 16);
    return static_cast<fastconv_uint128_t>(str2int<Radix, 16>(str)) * r16 +
           str2int<Radix, 16>(str + 16);
}
/*----------------------------------------[5, 7]U[9, 15]----------------------------------------*/

/*----------------------------------------[17, 31]U[33, 36]----------------------------------------*/
template <unsigned Radix, unsigned Len>
    requires (((17 <= Radix && Radix <= 31) || (33 <= Radix && Radix <= 36)) && Len == 4)
constexpr uint32_t str2int(const char* str) noexcept {
    constexpr uint32_t r1 = Radix;
    constexpr uint32_t r2 = r1 * r1;

    auto t = Impl<Radix, Len>(str);
    t = (t & Broadcast<pack_t<Len>, 16>(0x00ff)) * r1 + (t >> 8 & Broadcast<pack_t<Len>, 16>(0x00ff));
    t = (t & 0x0000ffffu) * r2 + (t >> 16 & 0x0000ffffu);
    return t;
}

template <unsigned Radix, unsigned Len>
    requires (((17 <= Radix && Radix <= 31) || (33 <= Radix && Radix <= 36)) && Len == 8)
constexpr uint64_t str2int(const char* str) noexcept {
    constexpr uint64_t r1 = Radix;
    constexpr uint64_t r2 = r1 * r1;
    constexpr uint64_t r4 = r2 * r2;

    auto t = Impl<Radix, Len>(str);
    t = (t & Broadcast<pack_t<Len>, 16>(0x00ff)) * r1 + (t >> 8 & Broadcast<pack_t<Len>, 16>(0x00ff));
    t = (t & Broadcast<pack_t<Len>, 32>(0x0000ffff)) * r2 + (t >> 16 & Broadcast<pack_t<Len>, 32>(0x0000ffff));
    t = (t & 0x00000000ffffffffull) * r4 + (t >> 32 & 0x00000000ffffffffull);
    return t;
}

template <unsigned Radix, unsigned Len>
    requires (((17 <= Radix && Radix <= 31) || (33 <= Radix && Radix <= 36)) && Len == 16)
constexpr fastconv_uint128_t str2int(const char* str) noexcept {
    constexpr fastconv_uint128_t r1 = Radix;
    constexpr fastconv_uint128_t r2 = r1 * r1;
    constexpr fastconv_uint128_t r4 = r2 * r2;
    constexpr fastconv_uint128_t r8 = r4 * r4;

    auto t = Impl<Radix, Len>(str);
    t = (t & Broadcast<pack_t<Len>, 16>(0x00ff)) * r1 + (t >> 8 & Broadcast<pack_t<Len>, 16>(0x00ff));
    t = (t & Broadcast<pack_t<Len>, 32>(0x0000ffff)) * r2 + (t >> 16 & Broadcast<pack_t<Len>, 32>(0x0000ffff));
    t = (t & Broadcast<pack_t<Len>, 64>(0x00000000ffffffff)) * r4 + (t >> 32 & Broadcast<pack_t<Len>, 64>(0x00000000ffffffff));
    t = static_cast<uint64_t>(t) * r8 + static_cast<uint64_t>(t >> 64);
    return t;
}
/*----------------------------------------[17, 31]U[33, 36]----------------------------------------*/

#else

template <unsigned Radix, unsigned Len>
    requires (2 <= Radix && Radix <= 36 && BitsNeeded(Radix, Len) <= kFastconvMaxBits)
constexpr result_t<Radix, Len> str2int(const char* str) noexcept {
    result_t<Radix, Len> value = 0;
    for (unsigned i = 0; i < Len; ++i) {
        const unsigned c = static_cast<unsigned char>(str[i]);
        const unsigned digit = c <= static_cast<unsigned>('9')
                                   ? c - static_cast<unsigned>('0')
                                   : (c | 0x20u) - static_cast<unsigned>('a') + 10;
        value = static_cast<result_t<Radix, Len>>(value * Radix + digit);
    }
    return value;
}

#endif

}  // namespace v0


// ============================================================================
// fastconv: per-(Radix, Len) specialised implementation.
//
// Radix 2 uses a multiply bit-gather; the other power-of-two radices build
// Len >= 16 from two independent 8-character 64-bit halves; radix 3, [5, 7] and
// [9, 15] use one multiply per reduction level. All three replaced the original
// 128-bit SWAR chains after benchmarking (see benchmark_str2int.txt).
// [17, 36] keeps masked SWAR inside each 8-character half - see the note on
// that group for why the one-multiply form cannot be used there.
// ============================================================================
namespace fastconv {

namespace fastconv_str2int_detail {

template <typename T>
using unsigned_integer_t = fastconv_unsigned_t<T>;

constexpr unsigned DecodeDigit(unsigned c) noexcept {
    return c <= static_cast<unsigned>('9')
               ? c - static_cast<unsigned>('0')
               : (c | 0x20u) - static_cast<unsigned>('a') + 10;
}

template <typename U>
    requires FastconvUnsignedInteger<U>
constexpr U ParseUnsignedFallback(const char* str, std::size_t len,
                                  unsigned radix) noexcept {
    U value = 0;
    for (std::size_t i = 0; i < len; ++i) {
        const unsigned c = static_cast<unsigned char>(str[i]);
        value = static_cast<U>(value * static_cast<U>(radix) +
                               static_cast<U>(DecodeDigit(c)));
    }
    return value;
}

template <typename T>
    requires FastconvSignedInteger<T>
constexpr T FromUnsignedBits(unsigned_integer_t<T> bits) noexcept {
    // The supported platforms use two's-complement signed integers. bit_cast
    // makes modulo parsing well-defined at the language level and handles the
    // minimum signed value without an overflowing unary minus.
    return std::bit_cast<T>(bits);
}

}  // namespace fastconv_str2int_detail

template <FastconvInteger T>
constexpr T str2int_fallback(const char* str, std::size_t len, unsigned radix) noexcept {
    if (!IsValidRadix(radix)) {
        return 0;
    }

    if constexpr (std::same_as<std::remove_cv_t<T>, bool>) {
        bool value = false;
        for (std::size_t i = 0; i < len; ++i) {
            const unsigned c = static_cast<unsigned char>(str[i]);
            value = static_cast<bool>(value * radix +
                                      fastconv_str2int_detail::DecodeDigit(c));
        }
        return value;
    } else if constexpr (FastconvIsSigned<T>) {
        bool negative = false;
        if (len != 0 && (str[0] == '-' || str[0] == '+')) {
            negative = str[0] == '-';
            ++str;
            --len;
        }
        using U = fastconv_str2int_detail::unsigned_integer_t<T>;
        U value = fastconv_str2int_detail::ParseUnsignedFallback<U>(str, len, radix);
        if (negative) {
            value = static_cast<U>(U(0) - value);
        }
        return fastconv_str2int_detail::FromUnsignedBits<T>(value);
    } else {
        return fastconv_str2int_detail::ParseUnsignedFallback<T>(str, len, radix);
    }
}

/*----------------------------------------2----------------------------------------*/
template <unsigned Radix, unsigned Len>
    requires (Radix == 2 && Len == 4)
constexpr uint8_t str2int(const char* str) noexcept {
    const uint32_t t = Load<uint32_t>(str) & Broadcast<uint32_t, 8>(0x01);
    return static_cast<uint8_t>(t * 0x08040201u >> 24);
}

template <unsigned Radix, unsigned Len>
    requires (Radix == 2 && Len == 8)
constexpr uint8_t str2int(const char* str) noexcept {
    const uint64_t t = Load<uint64_t>(str) & Broadcast<uint64_t, 8>(0x01);
    return static_cast<uint8_t>(t * 0x8040201008040201ull >> 56);
}

template <unsigned Radix, unsigned Len>
    requires (Radix == 2 && Len == 16)
constexpr uint16_t str2int(const char* str) noexcept {
    return static_cast<uint16_t>(
        static_cast<uint16_t>(str2int<2, 8>(str)) << 8 | str2int<2, 8>(str + 8));
}

template <unsigned Radix, unsigned Len>
    requires (Radix == 2 && Len == 32)
constexpr uint32_t str2int(const char* str) noexcept {
    return static_cast<uint32_t>(str2int<2, 16>(str)) << 16 | str2int<2, 16>(str + 16);
}

template <unsigned Radix, unsigned Len>
    requires (Radix == 2 && Len == 64)
constexpr uint64_t str2int(const char* str) noexcept {
    return static_cast<uint64_t>(str2int<2, 32>(str)) << 32 | str2int<2, 32>(str + 32);
}

template <unsigned Radix, unsigned Len>
    requires (Radix == 2 && Len == 128 && BitsNeeded(Radix, Len) <= kFastconvMaxBits)
constexpr result_t<Radix, Len> str2int(const char* str) noexcept {
    using R = result_t<Radix, Len>;
    return static_cast<R>(str2int<2, 64>(str)) << 64 |
           str2int<2, 64>(str + 64);
}
/*----------------------------------------2----------------------------------------*/

/*----------------------------------------4----------------------------------------*/
template <unsigned Radix, unsigned Len>
    requires (Radix == 4 && Len == 4)
constexpr uint8_t str2int(const char* str) noexcept {
    auto t = Impl<Radix, Len>(str);
    t = (t << 6 | t >> 4) & Broadcast<pack_t<Len>, 16>(0x00f0);
    t |= t >> 20;
    return static_cast<uint8_t>(t);
}

template <unsigned Radix, unsigned Len>
    requires (Radix == 4 && Len == 8)
constexpr uint16_t str2int(const char* str) noexcept {
    auto t = Impl<Radix, Len>(str);
    t = (t << 14 | t << 4) & Broadcast<pack_t<Len>, 16>(0xf000);
    t |= t >> 20;
    t |= t >> 40;
    return static_cast<uint16_t>(t);
}

template <unsigned Radix, unsigned Len>
    requires (Radix == 4 && Len == 16)
constexpr uint32_t str2int(const char* str) noexcept {
    return static_cast<uint32_t>(str2int<4, 8>(str)) << 16 | str2int<4, 8>(str + 8);
}

template <unsigned Radix, unsigned Len>
    requires (Radix == 4 && Len == 32)
constexpr uint64_t str2int(const char* str) noexcept {
    return static_cast<uint64_t>(str2int<4, 16>(str)) << 32 | str2int<4, 16>(str + 16);
}

template <unsigned Radix, unsigned Len>
    requires (Radix == 4 && Len == 64 && BitsNeeded(Radix, Len) <= kFastconvMaxBits)
constexpr result_t<Radix, Len> str2int(const char* str) noexcept {
    using R = result_t<Radix, Len>;
    return static_cast<R>(str2int<4, 32>(str)) << 64 |
           str2int<4, 32>(str + 32);
}
/*----------------------------------------4----------------------------------------*/

/*----------------------------------------8----------------------------------------*/
template <unsigned Radix, unsigned Len>
    requires (Radix == 8 && Len == 4)
constexpr uint16_t str2int(const char* str) noexcept {
    auto t = Impl<Radix, Len>(str) & Broadcast<pack_t<Len>, 8>(0x07);
    t = (t << 9 | t >> 2) & Broadcast<pack_t<Len>, 16>(0x0fc0);
    t |= t >> 22;
    return static_cast<uint16_t>(t);
}

template <unsigned Radix, unsigned Len>
    requires (Radix == 8 && Len == 8)
constexpr uint32_t str2int(const char* str) noexcept {
    auto t = Impl<Radix, Len>(str) & Broadcast<pack_t<Len>, 8>(0x07);
    t = (t | t << 11) & Broadcast<pack_t<Len>, 16>(0x3f00);
    t = (t | t >> 22) & Broadcast<pack_t<Len>, 32>(0x00003ffc);
    t = t << 10 | t >> 34;
    return static_cast<uint32_t>(t);
}

template <unsigned Radix, unsigned Len>
    requires (Radix == 8 && Len == 16)
constexpr uint64_t str2int(const char* str) noexcept {
    return static_cast<uint64_t>(str2int<8, 8>(str)) << 24 | str2int<8, 8>(str + 8);
}

template <unsigned Radix, unsigned Len>
    requires (Radix == 8 && Len == 32 && BitsNeeded(Radix, Len) <= kFastconvMaxBits)
constexpr result_t<Radix, Len> str2int(const char* str) noexcept {
    using R = result_t<Radix, Len>;
    return static_cast<R>(str2int<8, 16>(str)) << 48 |
           str2int<8, 16>(str + 16);
}
/*----------------------------------------8----------------------------------------*/

/*----------------------------------------16----------------------------------------*/
template <unsigned Radix, unsigned Len>
    requires (Radix == 16 && Len == 4)
constexpr uint16_t str2int(const char* str) noexcept {
    auto t = Impl<Radix, Len>(str) & Broadcast<pack_t<Len>, 8>(0x0f);
    t = (t | t << 12) & Broadcast<pack_t<Len>, 16>(0xff00);
    t |= t >> 24;
    return static_cast<uint16_t>(t);
}

template <unsigned Radix, unsigned Len>
    requires (Radix == 16 && Len == 8)
constexpr uint32_t str2int(const char* str) noexcept {
    auto t = Impl<Radix, Len>(str) & Broadcast<pack_t<Len>, 8>(0x0f);
    t = (t | t << 12) & Broadcast<pack_t<Len>, 16>(0xff00);
    t = ((t | t >> 24) & Broadcast<pack_t<Len>, 32>(0x0000ffff)) << 16;
    t |= t >> 48;
    return static_cast<uint32_t>(t);
}

template <unsigned Radix, unsigned Len>
    requires (Radix == 16 && Len == 16)
constexpr uint64_t str2int(const char* str) noexcept {
    return static_cast<uint64_t>(str2int<16, 8>(str)) << 32 | str2int<16, 8>(str + 8);
}

template <unsigned Radix, unsigned Len>
    requires (Radix == 16 && Len == 32 && BitsNeeded(Radix, Len) <= kFastconvMaxBits)
constexpr result_t<Radix, Len> str2int(const char* str) noexcept {
    using R = result_t<Radix, Len>;
    return static_cast<R>(str2int<16, 16>(str)) << 64 |
           str2int<16, 16>(str + 16);
}
/*----------------------------------------16----------------------------------------*/

/*----------------------------------------32----------------------------------------*/
template <unsigned Radix, unsigned Len>
    requires (Radix == 32 && Len == 4)
constexpr uint32_t str2int(const char* str) noexcept {
    auto t = Impl<Radix, Len>(str);
    t = (t << 5 & Broadcast<pack_t<Len>, 16>(0x03e0)) | (t >> 8 & Broadcast<pack_t<Len>, 16>(0x001f));
    t = (t << 10 | t >> 16) & 0x000fffffu;
    return t;
}

template <unsigned Radix, unsigned Len>
    requires (Radix == 32 && Len == 8)
constexpr uint64_t str2int(const char* str) noexcept {
    auto t = Impl<Radix, Len>(str);
    t = (t << 5 & Broadcast<pack_t<Len>, 16>(0x03e0)) | (t >> 8 & Broadcast<pack_t<Len>, 16>(0x001f));
    t = (t << 10 & Broadcast<pack_t<Len>, 32>(0x000ffcc0)) | (t >> 16 & Broadcast<pack_t<Len>, 32>(0x000003ff));
    t = (t << 20 | t >> 32) & 0x000000ffffffffffull;
    return t;
}

template <unsigned Radix, unsigned Len>
    requires (Radix == 32 && Len == 16 && BitsNeeded(Radix, Len) <= kFastconvMaxBits)
constexpr result_t<Radix, Len> str2int(const char* str) noexcept {
    using R = result_t<Radix, Len>;
    return static_cast<R>(str2int<32, 8>(str)) << 40 |
           str2int<32, 8>(str + 8);
}
/*----------------------------------------32----------------------------------------*/


/*----------------------------------------3, [5, 7], [9, 15]----------------------------------------*/
// One multiply per reduction level: lane j of t * (1 + (r << k)) equals
// t[j] + r * t[j - 1], so a level costs (mul, shift, and) instead of
// (and, and, shift, mul, add). It needs every lane sum to stay inside its own
// lane, which holds only for Radix <= 16 -- that is why the [17, 36] group
// below keeps the masked form.
//
// Len 16 / 32 are composed from 64-bit halves: a full-width multiply is not
// lane-local, so a 128-bit SWAR here would be lowered to three 64-bit
// multiplies instead of decomposing the way the masked form does.
template <unsigned Radix>
concept Multiplicative = Radix == 3 || (5 <= Radix && Radix <= 7) || (9 <= Radix && Radix <= 15);

template <unsigned Radix, unsigned Len>
    requires (Multiplicative<Radix> && Len == 4)
constexpr result_t<Radix, Len> str2int(const char* str) noexcept {
    constexpr uint32_t r1 = Radix;
    constexpr uint32_t r2 = r1 * r1;

    uint32_t t = Impl<Radix, Len>(str) & Broadcast<pack_t<Len>, 8>(0x0f);
    t = (t * (1u + (r1 << 8)) >> 8) & Broadcast<pack_t<Len>, 16>(0x00ff);
    return static_cast<result_t<Radix, Len>>(t * (1u + (r2 << 16)) >> 16);
}

template <unsigned Radix, unsigned Len>
    requires (Multiplicative<Radix> && Len == 8)
constexpr result_t<Radix, Len> str2int(const char* str) noexcept {
    constexpr uint64_t r1 = Radix;
    constexpr uint64_t r2 = r1 * r1;
    constexpr uint64_t r4 = r2 * r2;

    uint64_t t = Impl<Radix, Len>(str) & Broadcast<pack_t<Len>, 8>(0x0f);
    t = (t * (1ull + (r1 << 8)) >> 8) & Broadcast<pack_t<Len>, 16>(0x00ff);
    t = (t * (1ull + (r2 << 16)) >> 16) & Broadcast<pack_t<Len>, 32>(0x0000ffff);
    return static_cast<result_t<Radix, Len>>(t * (1ull + (r4 << 32)) >> 32);
}

template <unsigned Radix, unsigned Len>
    requires (Multiplicative<Radix> && Len == 16)
constexpr result_t<Radix, Len> str2int(const char* str) noexcept {
    constexpr auto r8 = Pow<uint64_t>(Radix, 8);
    return static_cast<result_t<Radix, Len>>(
        static_cast<uint64_t>(str2int<Radix, 8>(str)) * r8 + str2int<Radix, 8>(str + 8));
}

template <unsigned Radix, unsigned Len>
    requires (Multiplicative<Radix> && Len == 32 &&
              BitsNeeded(Radix, Len) <= kFastconvMaxBits)
constexpr result_t<Radix, Len> str2int(const char* str) noexcept {
    using R = result_t<Radix, Len>;
    constexpr auto r16 = Pow<R>(Radix, 16);
    return static_cast<R>(static_cast<R>(str2int<Radix, 16>(str)) * r16 +
                          str2int<Radix, 16>(str + 16));
}

// At 64 characters only radix 3 still fits in 128 bits.
template <unsigned Radix, unsigned Len>
    requires (Radix == 3 && Len == 64 && BitsNeeded(Radix, Len) <= kFastconvMaxBits)
constexpr result_t<Radix, Len> str2int(const char* str) noexcept {
    using R = result_t<Radix, Len>;
    constexpr auto r32 = Pow<R>(Radix, 32);
    return static_cast<R>(static_cast<R>(str2int<3, 32>(str)) * r32 +
                          str2int<3, 32>(str + 32));
}
/*----------------------------------------3, [5, 7], [9, 15]----------------------------------------*/


// Cheaper digit fixup for the radices whose letters straddle two nibble rows
// ('A'..'O' = 0x41..0x4f, 'P'..'Z' = 0x50..0x5a) and therefore need a 5-bit
// mask: value = (c & 0x1f) - 16 + 25 * isLetter folds both cases into a single
// constant subtraction instead of building an 0x1f/0x0f mask and adding 9.
// Every intermediate per-byte value stays inside [0, 51] so the SWAR add and
// sub cannot carry. Radix 32 keeps Impl - its bit-packing path measured slower
// with this form.
template <unsigned Radix, unsigned Len>
    requires (Radix >= 17)
constexpr auto ImplWide(const char* str) {
    using T = pack_t<Len>;

    const T t = Load<T>(str);
    const T letter = (t & pack<Len>::mask0) >> 6;
    return static_cast<T>((t & Broadcast<T, 8>(0x1f)) + letter * 25 - Broadcast<T, 8>(0x10));
}

/*----------------------------------------[17, 31]U[33, 36]----------------------------------------*/
// These keep the masked (and, and, shift, mul, add) form. The one-multiply-per-
// level trick above is not applicable: it relies on every lane sum staying
// inside its own lane, and already at level 1 a pair reaches
// Radix * (Radix - 1) + (Radix - 1), which is 288 for radix 17 and 1295 for
// radix 36 - both overflow the 8-bit lane and would corrupt the neighbour.
template <unsigned Radix, unsigned Len>
    requires (((17 <= Radix && Radix <= 31) || (33 <= Radix && Radix <= 36)) && Len == 4)
constexpr uint32_t str2int(const char* str) noexcept {
    constexpr uint32_t r1 = Radix;
    constexpr uint32_t r2 = r1 * r1;

    auto t = ImplWide<Radix, Len>(str);
    t = (t & Broadcast<pack_t<Len>, 16>(0x00ff)) * r1 + (t >> 8 & Broadcast<pack_t<Len>, 16>(0x00ff));
    t = (t & 0x0000ffffu) * r2 + (t >> 16 & 0x0000ffffu);
    return t;
}

template <unsigned Radix, unsigned Len>
    requires (((17 <= Radix && Radix <= 31) || (33 <= Radix && Radix <= 36)) && Len == 8)
constexpr uint64_t str2int(const char* str) noexcept {
    constexpr uint64_t r1 = Radix;
    constexpr uint64_t r2 = r1 * r1;
    constexpr uint64_t r4 = r2 * r2;

    auto t = ImplWide<Radix, Len>(str);
    t = (t & Broadcast<pack_t<Len>, 16>(0x00ff)) * r1 + (t >> 8 & Broadcast<pack_t<Len>, 16>(0x00ff));
    t = (t & Broadcast<pack_t<Len>, 32>(0x0000ffff)) * r2 + (t >> 16 & Broadcast<pack_t<Len>, 32>(0x0000ffff));
    t = (t & 0x00000000ffffffffull) * r4 + (t >> 32 & 0x00000000ffffffffull);
    return t;
}

template <unsigned Radix, unsigned Len>
    requires (((17 <= Radix && Radix <= 31) || (33 <= Radix && Radix <= 36)) && Len == 16 &&
              BitsNeeded(Radix, Len) <= kFastconvMaxBits)
constexpr result_t<Radix, Len> str2int(const char* str) noexcept {
    using R = result_t<Radix, Len>;
    constexpr R r8 = Pow<R>(Radix, 8);
    return static_cast<R>(static_cast<R>(str2int<Radix, 8>(str)) * r8 +
                          str2int<Radix, 8>(str + 8));
}
/*----------------------------------------[17, 31]U[33, 36]----------------------------------------*/

// Parse an arbitrary-length field in eight-character blocks. The working type
// is at least uint64_t so radix^8 is always representable; narrower results
// retain the fallback's modulo semantics at the final cast. Native uint128 is
// used only when T itself is wider than 64 bits.
template <unsigned Radix, FastconvInteger T>
    requires (IsValidRadix(Radix) && FastconvUnsignedInteger<T>)
constexpr T str2int_chunked(const char* str, std::size_t len) noexcept {
    using U = fastconv_str2int_detail::unsigned_integer_t<T>;
    using W = std::conditional_t<(sizeof(U) < sizeof(uint64_t)), uint64_t, U>;
    if (len == 0) {
        return 0;
    }

    constexpr W kGroup = Pow<W>(Radix, 8);
    std::size_t leading_len = len & 7u;
    if (leading_len == 0) {
        leading_len = 8;
    }

    W value = 0;
    if (leading_len == 8) {
        value = static_cast<W>(str2int<Radix, 8>(str));
    } else {
        for (std::size_t i = 0; i < leading_len; ++i) {
            value = static_cast<W>(
                value * static_cast<W>(Radix) +
                static_cast<W>(fastconv_str2int_detail::DecodeDigit(
                    static_cast<unsigned char>(str[i]))));
        }
    }
    for (std::size_t i = leading_len; i < len; i += 8) {
        value = static_cast<W>(
            value * kGroup + static_cast<W>(str2int<Radix, 8>(str + i)));
    }
    return static_cast<T>(value);
}

template <unsigned Radix, unsigned Len, FastconvInteger T>
constexpr T str2int_padded(const char* str, std::size_t len) noexcept {
    // Keep this helper well-defined even when called outside the dispatcher's
    // `len <= Len` proof.  Besides avoiding size_t underflow, the explicit
    // branch prevents GCC's range checker from diagnosing the guarded copy
    // after recursive runtime-radix inlining.
    if (len > Len) {
        return str2int_fallback<T>(str, len, Radix);
    }
    // Keep <=64-bit runtime conversions on <=64-bit arithmetic even on
    // compilers that expose a native uint128 type. Wider fixed-width buckets
    // otherwise make the same API substantially slower and non-portable.
    if constexpr ((BitsNeeded(Radix, Len) <= 64 ||
                   sizeof(T) > sizeof(uint64_t)) &&
                  requires { str2int<Radix, Len>(str); }) {
        char padded[Len];
        const std::size_t padding = Len - len;
        // One bounded loop makes the source index obviously non-negative and
        // keeps GCC's object-size analysis from treating the two-loop copy as
        // an unconstrained `padding + i` expression after inlining.
        for (std::size_t i = 0; i < Len; ++i) {
            padded[i] = i < padding ? '0' : str[i - padding];
        }
        return static_cast<T>(str2int<Radix, Len>(padded));
    } else if constexpr (FastconvUnsignedInteger<T>) {
        return str2int_chunked<Radix, T>(str, len);
    } else {
        return str2int_fallback<T>(str, len, Radix);
    }
}

// Parse a decimal field in eight-digit chunks.  The leading chunk is kept
// unpadded so the runtime facade never has to materialize a temporary padded
// buffer.  Every operation is intentionally uint64_t arithmetic: for a
// narrower destination the final cast has the same modulo result as the
// scalar fallback, while no-native builds avoid a hidden wide-integer helper.
constexpr uint64_t ParseDecimalPrefix(const char* str, std::size_t len) noexcept {
    switch (len) {
        case 0:
            return 0;
        case 1:
            return static_cast<unsigned>(str[0] - '0');
        case 2:
            return static_cast<unsigned>(str[0] - '0') * 10u +
                   static_cast<unsigned>(str[1] - '0');
        case 3:
            return static_cast<unsigned>(str[0] - '0') * 100u +
                   static_cast<unsigned>(str[1] - '0') * 10u +
                   static_cast<unsigned>(str[2] - '0');
        case 4:
            return static_cast<unsigned>(str[0] - '0') * 1000u +
                   static_cast<unsigned>(str[1] - '0') * 100u +
                   static_cast<unsigned>(str[2] - '0') * 10u +
                   static_cast<unsigned>(str[3] - '0');
        case 5:
            return static_cast<unsigned>(str[0] - '0') * 10000u +
                   static_cast<unsigned>(str[1] - '0') * 1000u +
                   static_cast<unsigned>(str[2] - '0') * 100u +
                   static_cast<unsigned>(str[3] - '0') * 10u +
                   static_cast<unsigned>(str[4] - '0');
        case 6:
            return static_cast<unsigned>(str[0] - '0') * 100000u +
                   static_cast<unsigned>(str[1] - '0') * 10000u +
                   static_cast<unsigned>(str[2] - '0') * 1000u +
                   static_cast<unsigned>(str[3] - '0') * 100u +
                   static_cast<unsigned>(str[4] - '0') * 10u +
                   static_cast<unsigned>(str[5] - '0');
        case 7:
            return static_cast<unsigned>(str[0] - '0') * 1000000u +
                   static_cast<unsigned>(str[1] - '0') * 100000u +
                   static_cast<unsigned>(str[2] - '0') * 10000u +
                   static_cast<unsigned>(str[3] - '0') * 1000u +
                   static_cast<unsigned>(str[4] - '0') * 100u +
                   static_cast<unsigned>(str[5] - '0') * 10u +
                   static_cast<unsigned>(str[6] - '0');
        default:
            return str2int<10, 8>(str);
    }
}

constexpr uint64_t Str2intDecimalUint64(const char* str, std::size_t len) noexcept {
    if (len == 0) {
        return 0;
    }
    std::size_t leading_len = len & 7u;
    if (leading_len == 0) {
        leading_len = 8;
    }

    uint64_t value = ParseDecimalPrefix(str, leading_len);
    for (std::size_t i = leading_len; i < len; i += 8) {
        value = value * 100000000ull + str2int<10, 8>(str + i);
    }
    return value;
}

template <unsigned Radix, FastconvInteger T>
constexpr T str2int_dispatch_len(const char* str, std::size_t len) noexcept {
    if (len <= 4) {
        return str2int_padded<Radix, 4, T>(str, len);
    }
    if (len <= 8) {
        return str2int_padded<Radix, 8, T>(str, len);
    }
    if (len <= 16) {
        return str2int_padded<Radix, 16, T>(str, len);
    }
    if (len <= 32) {
        return str2int_padded<Radix, 32, T>(str, len);
    }
    if (len <= 64) {
        return str2int_padded<Radix, 64, T>(str, len);
    }
    if (len <= 128) {
        return str2int_padded<Radix, 128, T>(str, len);
    }
    if constexpr (FastconvUnsignedInteger<T>) {
        return str2int_chunked<Radix, T>(str, len);
    } else {
        return str2int_fallback<T>(str, len, Radix);
    }
}

template <unsigned Radix = 2, FastconvInteger T>
constexpr T str2int_dispatch_radix(
    const char* str, std::size_t len, unsigned radix) noexcept {
    if (radix == Radix) {
        return str2int_dispatch_len<Radix, T>(str, len);
    }
    if constexpr (Radix < 36) {
        return str2int_dispatch_radix<Radix + 1, T>(str, len, radix);
    }
    return str2int_fallback<T>(str, len, radix);
}

// Runtime dispatch facade. Inputs shorter than four characters use Horner
// directly. Longer inputs are left-padded with zeroes to the next supported
// power-of-two length and forwarded to the corresponding specialization.
template <FastconvInteger T>
constexpr T str2int(const char* str, std::size_t len, unsigned radix) noexcept {
    if (!IsValidRadix(radix)) {
        return 0;
    }

    if constexpr (FastconvIsSigned<T>) {
        bool negative = false;
        if (len != 0 && (str[0] == '-' || str[0] == '+')) {
            negative = str[0] == '-';
            ++str;
            --len;
        }
        using U = fastconv_str2int_detail::unsigned_integer_t<T>;
        U value = str2int<U>(str, len, radix);
        if (negative) {
            value = static_cast<U>(U(0) - value);
        }
        return fastconv_str2int_detail::FromUnsignedBits<T>(value);
    }

    if constexpr (FastconvUnsignedInteger<T> && sizeof(T) <= sizeof(uint64_t)) {
        if (radix == 10) {
            return static_cast<T>(Str2intDecimalUint64(str, len));
        }
    }
    if (len < 4) {
        return str2int_fallback<T>(str, len, radix);
    }
    return str2int_dispatch_radix<2, T>(str, len, radix);
}

}  // namespace fastconv


// ============================================================================
// mullevel: one multiply per reduction level for the power-of-two radices, the
// same form the multiplicative radices use, instead of the shift/or bit packing
// in fastconv. Lane j of t * (1 + (r << k)) equals t[j] + r * t[j - 1].
//
// Radix 32 is absent on purpose: a level-1 pair reaches 31 * 32 + 31 = 1023,
// which overflows the 8-bit lane, so it would need the masked form anyway.
// Lengths above 8 combine two halves with a shift/or because the radix is a
// power of two - no multiply is needed there.
// ============================================================================
namespace mullevel {

template <unsigned Radix>
concept Packable = Radix == 4 || Radix == 8 || Radix == 16;

template <unsigned Radix, unsigned Len>
    requires (Packable<Radix> && Len == 4)
constexpr result_t<Radix, Len> str2int(const char* str) noexcept {
    constexpr uint32_t r1 = Radix;
    constexpr uint32_t r2 = r1 * r1;

    uint32_t t = Impl<Radix, Len>(str) & Broadcast<pack_t<Len>, 8>(0x0f);
    t = (t * (1u + (r1 << 8)) >> 8) & Broadcast<pack_t<Len>, 16>(0x00ff);
    return static_cast<result_t<Radix, Len>>(t * (1u + (r2 << 16)) >> 16);
}

template <unsigned Radix, unsigned Len>
    requires (Packable<Radix> && Len == 8)
constexpr result_t<Radix, Len> str2int(const char* str) noexcept {
    constexpr uint64_t r1 = Radix;
    constexpr uint64_t r2 = r1 * r1;
    constexpr uint64_t r4 = r2 * r2;

    uint64_t t = Impl<Radix, Len>(str) & Broadcast<pack_t<Len>, 8>(0x0f);
    t = (t * (1ull + (r1 << 8)) >> 8) & Broadcast<pack_t<Len>, 16>(0x00ff);
    t = (t * (1ull + (r2 << 16)) >> 16) & Broadcast<pack_t<Len>, 32>(0x0000ffff);
    return static_cast<result_t<Radix, Len>>(t * (1ull + (r4 << 32)) >> 32);
}

template <unsigned Radix, unsigned Len>
    requires (Packable<Radix> && Len >= 16 &&
              BitsNeeded(Radix, Len) <= kFastconvMaxBits)
constexpr result_t<Radix, Len> str2int(const char* str) noexcept {
    using R = result_t<Radix, Len>;
    constexpr unsigned half = BitsNeeded(Radix, Len / 2);

    return static_cast<R>(static_cast<R>(str2int<Radix, Len / 2>(str)) << half |
                          str2int<Radix, Len / 2>(str + Len / 2));
}

}  // namespace mullevel


// ============================================================================
// v0: the original implementation (fastconv/../tmp/str2int-src.h), kept as a
// performance reference. Three defects of that file are corrected so the column
// is measurable: `power` squared the base inside the `if`, radix 4 len 16
// dropped the high half (`t = t >> 80` instead of `t |= t >> 80`) and had a
// truncated mask constant, and radix 4 len 64 shifted by 32 instead of 64.
// The reinterpret_cast load is replaced by Load<T>. The algorithms themselves
// are unchanged.
// ============================================================================



// ============================================================================
// simd (optimisation 6)
//
// 16 characters at a time. The entire first reduction level collapses into a
// single pmaddubsw (x86) or vmull + vpaddl (NEON): "multiply neighbouring
// digits by (Radix, 1) and add". Valid for every Radix <= 36 because
// 35 * 36 + 35 = 1295 never saturates a 16-bit accumulator.
// ============================================================================
#if defined(FASTCONV_X86_SIMD) || defined(FASTCONV_ARM_SIMD)
#define FASTCONV_HAS_SIMD 1

#if defined(FASTCONV_X86_SIMD)
#include <immintrin.h>
#else
#if defined(_MSC_VER) && !defined(__clang__)
#include <arm64_neon.h>
#else
#include <arm_neon.h>
#endif
#endif

// ============================================================================
// simd: the first reduction level is one pmaddubsw / vmull+vpaddl - "multiply
// neighbouring digits by (Radix, 1) and add". Valid for every Radix <= 36
// because 35 * 36 + 35 = 1295 never saturates a 16-bit accumulator.
//
// One vector pass handles kBlock characters and yields kBlock / 8 partial
// results, each below Radix^8; str2int then folds them with Horner. The widest
// available unit is picked at compile time: AVX512BW 64 chars, AVX2 32 chars,
// SSE4.1 / NEON 16 chars. aarch64 has no wider vector than 128 bits, so longer
// inputs there are simply repeated 16-character passes.
//
// Only the SSE4.1 and NEON paths have been executed; AVX2 and AVX512BW are
// compile-verified only (no such hardware available here).
// ============================================================================
namespace simd {

#if defined(FASTCONV_X86_AVX512BW)
constexpr unsigned kBlock = 64;
#elif defined(FASTCONV_X86_AVX2)
constexpr unsigned kBlock = 32;
#else
constexpr unsigned kBlock = 16;
#endif

// out[0 .. 1], most significant first, each below Radix^8.
template <unsigned Radix>
inline void Halves16(const char* str, uint64_t* out) noexcept {
    constexpr uint32_t r1 = Radix;
    constexpr uint32_t r2 = r1 * r1;
    constexpr uint32_t r4 = r2 * r2;

#if defined(FASTCONV_X86_SIMD)
    __m128i v = _mm_loadu_si128(reinterpret_cast<const __m128i*>(str));
    if constexpr (Radix <= 10) {
        v = _mm_and_si128(v, _mm_set1_epi8(0x0f));
    } else {
        const __m128i bit6 = _mm_set1_epi8(0x40);
        const __m128i isLetter = _mm_cmpeq_epi8(_mm_and_si128(v, bit6), bit6);
        if constexpr (Radix <= 16) {
            v = _mm_add_epi8(_mm_and_si128(v, _mm_set1_epi8(0x0f)),
                             _mm_and_si128(_mm_set1_epi8(9), isLetter));
        } else {
            v = _mm_add_epi8(_mm_and_si128(v, _mm_set1_epi8(0x1f)),
                             _mm_and_si128(_mm_set1_epi8(25), isLetter));
            v = _mm_sub_epi8(v, _mm_set1_epi8(16));
        }
    }
    v = _mm_maddubs_epi16(v, _mm_set1_epi16(static_cast<int16_t>(0x0100 | r1)));
    v = _mm_madd_epi16(v, _mm_set1_epi32(static_cast<int>((1u << 16) | r2)));
    const __m128i even = _mm_mul_epu32(v, _mm_set1_epi32(static_cast<int>(r4)));
    _mm_storeu_si128(reinterpret_cast<__m128i*>(out),
                     _mm_add_epi64(even, _mm_srli_epi64(v, 32)));
#else
    uint8x16_t v = vld1q_u8(reinterpret_cast<const uint8_t*>(str));
    if constexpr (Radix <= 10) {
        v = vandq_u8(v, vdupq_n_u8(0x0f));
    } else {
        const uint8x16_t bit6 = vdupq_n_u8(0x40);
        const uint8x16_t isLetter = vceqq_u8(vandq_u8(v, bit6), bit6);
        if constexpr (Radix <= 16) {
            v = vaddq_u8(vandq_u8(v, vdupq_n_u8(0x0f)), vandq_u8(vdupq_n_u8(9), isLetter));
        } else {
            v = vaddq_u8(vandq_u8(v, vdupq_n_u8(0x1f)), vandq_u8(vdupq_n_u8(25), isLetter));
            v = vsubq_u8(v, vdupq_n_u8(16));
        }
    }
    const uint8x8_t w1 = {r1, 1, r1, 1, r1, 1, r1, 1};
    const uint32x4_t w2 = {r2, 1, r2, 1};
    const uint64x2_t a = vpaddlq_u32(vmulq_u32(vpaddlq_u16(vmull_u8(vget_low_u8(v), w1)), w2));
    const uint64x2_t b = vpaddlq_u32(vmulq_u32(vpaddlq_u16(vmull_u8(vget_high_u8(v), w1)), w2));
    out[0] = vgetq_lane_u64(a, 0) * r4 + vgetq_lane_u64(a, 1);
    out[1] = vgetq_lane_u64(b, 0) * r4 + vgetq_lane_u64(b, 1);
#endif
}

#if defined(FASTCONV_X86_AVX2)
// out[0 .. 3], most significant first. maddubs / madd stay inside their 128-bit
// lanes, and mul_epu32 pairs adjacent 32-bit lanes, so the global character
// order is preserved throughout.
template <unsigned Radix>
inline void Halves32(const char* str, uint64_t* out) noexcept {
    constexpr uint32_t r1 = Radix;
    constexpr uint32_t r2 = r1 * r1;
    constexpr uint32_t r4 = r2 * r2;

    __m256i v = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(str));
    if constexpr (Radix <= 10) {
        v = _mm256_and_si256(v, _mm256_set1_epi8(0x0f));
    } else {
        const __m256i bit6 = _mm256_set1_epi8(0x40);
        const __m256i isLetter = _mm256_cmpeq_epi8(_mm256_and_si256(v, bit6), bit6);
        if constexpr (Radix <= 16) {
            v = _mm256_add_epi8(_mm256_and_si256(v, _mm256_set1_epi8(0x0f)),
                                _mm256_and_si256(_mm256_set1_epi8(9), isLetter));
        } else {
            v = _mm256_add_epi8(_mm256_and_si256(v, _mm256_set1_epi8(0x1f)),
                                _mm256_and_si256(_mm256_set1_epi8(25), isLetter));
            v = _mm256_sub_epi8(v, _mm256_set1_epi8(16));
        }
    }
    v = _mm256_maddubs_epi16(v, _mm256_set1_epi16(static_cast<int16_t>(0x0100 | r1)));
    v = _mm256_madd_epi16(v, _mm256_set1_epi32(static_cast<int>((1u << 16) | r2)));
    const __m256i even = _mm256_mul_epu32(v, _mm256_set1_epi32(static_cast<int>(r4)));
    _mm256_storeu_si256(reinterpret_cast<__m256i*>(out),
                        _mm256_add_epi64(even, _mm256_srli_epi64(v, 32)));
}
#endif

#if defined(FASTCONV_X86_AVX512BW)
// out[0 .. 7], most significant first.
template <unsigned Radix>
inline void Halves64(const char* str, uint64_t* out) noexcept {
    constexpr uint32_t r1 = Radix;
    constexpr uint32_t r2 = r1 * r1;
    constexpr uint32_t r4 = r2 * r2;

    __m512i v = _mm512_loadu_si512(str);
    if constexpr (Radix <= 10) {
        v = _mm512_and_si512(v, _mm512_set1_epi8(0x0f));
    } else {
        const __mmask64 isLetter = _mm512_test_epi8_mask(v, _mm512_set1_epi8(0x40));
        if constexpr (Radix <= 16) {
            v = _mm512_and_si512(v, _mm512_set1_epi8(0x0f));
            v = _mm512_mask_add_epi8(v, isLetter, v, _mm512_set1_epi8(9));
        } else {
            v = _mm512_and_si512(v, _mm512_set1_epi8(0x1f));
            v = _mm512_mask_add_epi8(v, isLetter, v, _mm512_set1_epi8(25));
            v = _mm512_sub_epi8(v, _mm512_set1_epi8(16));
        }
    }
    v = _mm512_maddubs_epi16(v, _mm512_set1_epi16(static_cast<int16_t>(0x0100 | r1)));
    v = _mm512_madd_epi16(v, _mm512_set1_epi32(static_cast<int>((1u << 16) | r2)));
    const __m512i even = _mm512_mul_epu32(v, _mm512_set1_epi32(static_cast<int>(r4)));
    _mm512_storeu_si512(out, _mm512_add_epi64(even, _mm512_srli_epi64(v, 32)));
}
#endif

// Fills Len / 8 partial results using the widest pass that fits.
template <unsigned Radix, unsigned Len>
inline void Blocks(const char* str, uint64_t* out) noexcept {
    constexpr unsigned kPass = Len < kBlock ? Len : kBlock;

    for (unsigned i = 0; i < Len / kPass; ++i) {
        if constexpr (kPass == 64) {
#if defined(FASTCONV_X86_AVX512BW)
            Halves64<Radix>(str + i * kPass, out + i * 8);
#endif
        } else if constexpr (kPass == 32) {
#if defined(FASTCONV_X86_AVX2)
            Halves32<Radix>(str + i * kPass, out + i * 4);
#else
            // AVX512BW normally implies AVX2, but keep unusual feature
            // combinations correct when AVX2 is explicitly disabled.
            Halves16<Radix>(str + i * kPass, out + i * 4);
            Halves16<Radix>(str + i * kPass + 16, out + i * 4 + 2);
#endif
        } else {
            Halves16<Radix>(str + i * kPass, out + i * 2);
        }
    }
}

template <unsigned Radix, unsigned Len>
    requires (2 <= Radix && Radix <= 36 && Len % 16 == 0 && Len <= 128 &&
              BitsNeeded(Radix, Len) <= kFastconvMaxBits)
inline result_t<Radix, Len> str2int(const char* str) noexcept {
    constexpr auto r8 = Pow<uint64_t>(Radix, 8);

    uint64_t part[Len / 8];
    Blocks<Radix, Len>(str, part);

    if constexpr (BitsNeeded(Radix, Len) <= 64) {
        uint64_t acc = part[0];
        for (unsigned i = 1; i < Len / 8; ++i) {
            acc = acc * static_cast<uint64_t>(r8) + part[i];
        }
        return static_cast<result_t<Radix, Len>>(acc);
    } else {
        result_t<Radix, Len> acc = part[0];
        for (unsigned i = 1; i < Len / 8; ++i) {
            acc = acc * r8 + part[i];
        }
        return static_cast<result_t<Radix, Len>>(acc);
    }
}

}  // namespace simd

#endif  // SIMD available
