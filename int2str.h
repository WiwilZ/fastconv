#pragma once

#include "utils.h"

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <type_traits>


// int2str writes exactly Len characters, zero padded, lower case for the digits
// above 9 - the exact inverse of str2int<Radix, Len>, so
// str2int<R, L>(buf) == v holds after int2str<R, L>(buf, v).

constexpr char kDigitChars[37] = "0123456789abcdefghijklmnopqrstuvwxyz";

// One digit value (< 36) to its character.
constexpr char DigitChar(unsigned d) noexcept {
    return kDigitChars[d];
}

// Every byte of t holds a digit value below 2^Bits; converts them all to ASCII.
// Radices up to 10 only need the 0x30 bias. Above that, the "is it above 9"
// test is (d + 2^Bits - 10) >> Bits: the carry into bit Bits appears exactly
// when d >= 10, and the largest intermediate byte is 35 + 54 = 89, so the SWAR
// add never carries into the next byte.
template <unsigned Radix, unsigned Bits, typename T>
constexpr T BytesToAscii(T t) noexcept {
    if constexpr (Radix <= 10) {
        return static_cast<T>(t + Broadcast<T, 8>(0x30));
    } else {
        static_assert(Bits >= 4, "the carry test needs 2^Bits > 10");
        const T above9 = (t + Broadcast<T, 8>((1u << Bits) - 10)) >> Bits & Broadcast<T, 8>(0x01);
        return static_cast<T>(t + Broadcast<T, 8>(0x30) + above9 * 39);
    }
}


// Repeating mask: `keep` low bits set out of every `period` bits.
template <typename T>
consteval T MaskKeep(unsigned keep, unsigned period) noexcept {
    T m = 0;
    for (unsigned sh = 0; sh + period <= sizeof(T) * 8; sh += period) {
        m |= static_cast<T>(((static_cast<T>(1) << keep) - 1) << sh);
    }
    return m;
}

template <typename T>
constexpr T ByteReverse(T x) noexcept {
    if constexpr (sizeof(T) == 4) {
        return std::byteswap(static_cast<uint32_t>(x));
    } else if constexpr (sizeof(T) == 8) {
        return std::byteswap(static_cast<uint64_t>(x));
    } else {
        const uint64_t lo = std::byteswap(static_cast<uint64_t>(x));
        const uint64_t hi = std::byteswap(static_cast<uint64_t>(x >> 64));
        return static_cast<T>(lo) << 64 | hi;
    }
}

// One halving step per level. Invariant: groups of `Group` digits, each dense
// (Bits * Group wide) and placed at stride 8 * Group. The step keeps the low
// half of every group where it is and lifts the high half to the next group
// slot. The two halves must be masked separately: the compact `x | x << n`
// idiom only avoids self-overlap while 2 * Bits <= 8, so it breaks for radix 32.
template <unsigned Bits, unsigned Group, typename T>
constexpr T Spread(T x) noexcept {
    constexpr T kLow = MaskKeep<T>(Bits * Group / 2, 8 * Group);

    x = static_cast<T>((x & kLow) |
                       (((x >> (Bits * Group / 2)) & kLow) << (4 * Group)));
    if constexpr (Group > 2) {
        return Spread<Bits, Group / 2>(x);
    } else {
        return x;
    }
}

// Two-digit table, 2 * Radix^2 bytes (2592 for radix 36), built at compile time.
template <unsigned Radix>
struct Pairs {
    char c[Radix * Radix][2];

    constexpr Pairs() : c{} {
        for (unsigned i = 0; i < Radix * Radix; ++i) {
            c[i][0] = DigitChar(i / Radix);
            c[i][1] = DigitChar(i % Radix);
        }
    }
};

template <unsigned Radix>
constexpr Pairs<Radix> kPairs{};

template <unsigned Radix>
concept Pow2Radix = Radix == 2 || Radix == 4 || Radix == 8 || Radix == 16 || Radix == 32;


// ============================================================================
// v0: the inverse of str2int's v0.
//
// Power-of-two radices: str2int's v0 gathers the digits with a log-depth
// shift/or/mask chain, so the inverse spreads them with the same chain run
// backwards. Going from groups of s digits to groups of s / 2, the target
// stride is 8 * s bits and the source stride is k * s, hence the shift
// (8 - k) * s and a mask that keeps k * s bits out of every 8 * s. The spread
// naturally produces least-significant-digit-first, so the bytes are reversed
// before the store.
//
// Multiplicative radices: a strict inverse is not expressible. str2int's level
// is (t & m) * r + (t >> 8 & m), and undoing it needs a division per lane,
// which scalar SWAR cannot do - lane-by-lane division is exactly fastconv's
// split tree. This family therefore keeps a plain serial loop, which is the
// honest naive reference to measure against.
// ============================================================================
namespace v0 {

template <unsigned Radix, unsigned Len>
    requires (Pow2Radix<Radix> &&
              (Len == 4 || Len == 8 ||
               (Len == 16 && FASTCONV_HAS_NATIVE_UINT128 != 0)))
constexpr void int2str(char* out, result_t<Radix, Len> value) noexcept {
    using T = pack_t<Len>;
    constexpr unsigned kBits = BitsNeeded(Radix, 1);

    T x = Spread<kBits, Len>(static_cast<T>(value));
    Store<T>(out, BytesToAscii<Radix, kBits>(ByteReverse<T>(x)));
}

// Longer power-of-two inputs split into two independent halves.
template <unsigned Radix, unsigned Len>
    requires (Pow2Radix<Radix> &&
              (Len > 16 || (Len == 16 && FASTCONV_HAS_NATIVE_UINT128 == 0)) &&
              BitsNeeded(Radix, Len) <= kFastconvMaxBits)
constexpr void int2str(char* out, result_t<Radix, Len> value) noexcept {
    constexpr unsigned kLow = BitsNeeded(Radix, Len / 2);

    int2str<Radix, Len / 2>(out, static_cast<result_t<Radix, Len / 2>>(value >> kLow));
    int2str<Radix, Len / 2>(out + Len / 2, static_cast<result_t<Radix, Len / 2>>(value));
}

template <unsigned Radix, unsigned Len>
    requires (!Pow2Radix<Radix> && 2 <= Radix && Radix <= 36 &&
              BitsNeeded(Radix, Len) <= kFastconvMaxBits)
constexpr void int2str(char* out, result_t<Radix, Len> value) noexcept {
    auto v = value;
    for (unsigned i = Len; i-- > 0;) {
        out[i] = DigitChar(static_cast<unsigned>(v % Radix));
        v /= Radix;
    }
}

}  // namespace v0


// ============================================================================
// fastconv
//
// Power-of-two radices: the strict inverse of str2int's bit packing is already
// the best known spread, so this family shares v0's log-depth shift/or/mask
// chain - there is nothing left to improve on it.
//
// Multiplicative radices: a balanced split tree down to two digits, where a
// Radix^2 entry table supplies both characters at once. Each interior step
// divides by a compile-time Radix^k, which the compiler lowers to a multiply by
// a reciprocal, and the two halves are independent - so the tree is as wide as
// it is deep, and half as deep as a per-digit chain, unlike v0's strictly
// serial chain of Len divisions.
// ============================================================================
namespace fastconv {

template <std::integral T>
constexpr std::size_t int2str_fallback(char* out, T value, unsigned radix) noexcept {
    if constexpr (std::same_as<std::remove_cv_t<T>, bool>) {
        out[0] = static_cast<char>('0' + value);
        return 1;
    } else {
        char reversed[std::numeric_limits<T>::digits + (std::is_signed_v<T> ? 1 : 0)];
        std::size_t len = 0;
        do {
            const unsigned digit = static_cast<unsigned>(value % radix);
            reversed[len++] = DigitChar(digit);
            value = static_cast<T>(value / radix);
        } while (value != 0);

        for (std::size_t i = 0; i < len; ++i) {
            out[i] = reversed[len - 1 - i];
        }
        return len;
    }
}

template <unsigned Radix>
concept MulRadix = !Pow2Radix<Radix> && 3 <= Radix && Radix <= 36;

template <unsigned Radix, unsigned Len>
    requires (Pow2Radix<Radix> &&
              (Len == 4 || Len == 8 ||
               (Len == 16 && FASTCONV_HAS_NATIVE_UINT128 != 0)))
constexpr void int2str(char* out, result_t<Radix, Len> value) noexcept {
    using T = pack_t<Len>;
    constexpr unsigned kBits = BitsNeeded(Radix, 1);

    T x = Spread<kBits, Len>(static_cast<T>(value));
    Store<T>(out, BytesToAscii<Radix, kBits>(ByteReverse<T>(x)));
}

// Longer inputs split into two independent halves at a byte boundary.
template <unsigned Radix, unsigned Len>
    requires (Pow2Radix<Radix> &&
              (Len > 16 || (Len == 16 && FASTCONV_HAS_NATIVE_UINT128 == 0)) &&
              BitsNeeded(Radix, Len) <= kFastconvMaxBits)
constexpr void int2str(char* out, result_t<Radix, Len> value) noexcept {
    constexpr unsigned kLow = BitsNeeded(Radix, Len / 2);

    int2str<Radix, Len / 2>(out, static_cast<result_t<Radix, Len / 2>>(value >> kLow));
    int2str<Radix, Len / 2>(out + Len / 2, static_cast<result_t<Radix, Len / 2>>(value));
}

template <unsigned Radix, unsigned Len>
    requires (MulRadix<Radix> && BitsNeeded(Radix, Len) <= kFastconvMaxBits)
constexpr void int2str(char* out, result_t<Radix, Len> value) noexcept {
    if constexpr (Len == 1) {
        out[0] = DigitChar(static_cast<unsigned>(value));
    } else if constexpr (Len == 2) {
        const unsigned i = static_cast<unsigned>(value);
        out[0] = kPairs<Radix>.c[i][0];
        out[1] = kPairs<Radix>.c[i][1];
    } else {
        constexpr unsigned kLow = Len / 2;
        constexpr unsigned kHigh = Len - kLow;
        constexpr auto kSplit = Pow<result_t<Radix, Len>>(Radix, kLow);

        int2str<Radix, kHigh>(out, static_cast<result_t<Radix, kHigh>>(value / kSplit));
        int2str<Radix, kLow>(out + kHigh, static_cast<result_t<Radix, kLow>>(value % kSplit));
    }
}

template <unsigned Radix, unsigned Len, std::integral T>
constexpr bool int2str_padded(
    char* out, T value, std::size_t output_len) noexcept {
    if constexpr (requires {
                      int2str<Radix, Len>(out, result_t<Radix, Len>{});
                  }) {
        char padded[Len];
        int2str<Radix, Len>(
            padded, static_cast<result_t<Radix, Len>>(value));
        const std::size_t padding = Len - output_len;
        for (std::size_t i = 0; i < output_len; ++i) {
            out[i] = padded[padding + i];
        }
        return true;
    }
    return false;
}

template <unsigned Radix, std::integral T>
constexpr bool int2str_dispatch_len(char* out, T value, std::size_t len) noexcept {
    if (len <= 4) {
        return int2str_padded<Radix, 4>(out, value, len);
    }
    if (len <= 8) {
        return int2str_padded<Radix, 8>(out, value, len);
    }
    if (len <= 16) {
        return int2str_padded<Radix, 16>(out, value, len);
    }
    if (len <= 32) {
        return int2str_padded<Radix, 32>(out, value, len);
    }
    if (len <= 64) {
        return int2str_padded<Radix, 64>(out, value, len);
    }
    if (len <= 128) {
        return int2str_padded<Radix, 128>(out, value, len);
    }
    return false;
}

template <unsigned Radix = 2, std::integral T>
constexpr bool int2str_dispatch_radix(
    char* out, T value, unsigned radix, std::size_t len) noexcept {
    if (radix == Radix) {
        return int2str_dispatch_len<Radix>(out, value, len);
    }
    if constexpr (Radix < 36) {
        return int2str_dispatch_radix<Radix + 1>(out, value, radix, len);
    }
    return false;
}

// Runtime dispatch facade. It first determines the shortest representation;
// supported lengths are then written by the compile-time specialization.
template <std::integral T>
constexpr std::size_t int2str(char* out, T value, unsigned radix) noexcept {
    if constexpr (std::same_as<std::remove_cv_t<T>, bool>) {
        out[0] = static_cast<char>('0' + value);
        return 1;
    } else {
        T remaining = value;
        std::size_t len = 1;
        while (remaining >= static_cast<T>(radix)) {
            remaining = static_cast<T>(remaining / radix);
            ++len;
        }

        if (int2str_dispatch_radix(out, value, radix, len)) {
            return len;
        }
        return int2str_fallback(out, value, radix);
    }
}

}  // namespace fastconv


// ============================================================================
// simd
//
// The split tree is only vectorised at its last level, which is where all the
// work is: four pair values (each below Radix^2, so each fits a 16-bit lane)
// become eight ASCII digits with one multiply-high, one multiply-low, one
// subtract and one narrowing interleave. Everything above that level stays
// scalar because the values are too wide for useful lanes.
//
// Only the NEON path has been executed; SSE4.1 is compile-verified only.
// ============================================================================
#if defined(FASTCONV_X86_SIMD) || defined(FASTCONV_ARM_SIMD)
#define FASTCONV_INT2STR_HAS_SIMD 1

#if defined(FASTCONV_X86_SIMD)
#include <immintrin.h>
#else
#include <arm_neon.h>
#endif

namespace simd {

// Eight digits from four pair values, pair[0] the most significant.
template <unsigned Radix>
inline void EightDigits(char* out, const uint16_t pair[4]) noexcept {
    // floor(p / Radix) via a 16-bit reciprocal: for p < Radix^2 <= 1296 the
    // shift is small enough that the classic (p * m) >> s is exact.
    constexpr unsigned kShift = 21;
    constexpr uint32_t kMagic = (1u << kShift) / Radix + 1;
    static_assert(Pow<uint32_t>(Radix, 2) * kMagic >> kShift <= 0xffff);

#if defined(FASTCONV_X86_SIMD)
    const __m128i p = _mm_loadl_epi64(reinterpret_cast<const __m128i*>(pair));
    const __m128i hi32 = _mm_srli_epi32(_mm_mullo_epi32(_mm_cvtepu16_epi32(p),
                                                        _mm_set1_epi32(int(kMagic))),
                                        kShift);
    const __m128i hi = _mm_packus_epi32(hi32, hi32);
    const __m128i lo = _mm_sub_epi16(p, _mm_mullo_epi16(hi, _mm_set1_epi16(Radix)));
    // unpack already interleaves hi/lo as hi0 lo0 hi1 lo1 ...
    const __m128i digits =
        _mm_packus_epi16(_mm_unpacklo_epi16(hi, lo), _mm_setzero_si128());
    uint64_t bytes;
    std::memcpy(&bytes, &digits, sizeof(bytes));
    Store<uint64_t>(out, BytesToAscii<Radix, 6>(bytes));
#else
    const uint16x4_t p = vld1_u16(pair);
    const uint32x4_t prod = vmulq_n_u32(vmovl_u16(p), kMagic);
    const uint16x4_t hi = vmovn_u32(vshrq_n_u32(prod, kShift));
    const uint16x4_t lo = vsub_u16(p, vmul_n_u16(hi, static_cast<uint16_t>(Radix)));
    const uint8x8_t packed = vmovn_u16(vcombine_u16(hi, lo));  // hi0..hi3 lo0..lo3
    const uint8x8_t idx = {0, 4, 1, 5, 2, 6, 3, 7};
    uint64_t bytes;
    const uint8x8_t interleaved = vtbl1_u8(packed, idx);
    vst1_u8(reinterpret_cast<uint8_t*>(&bytes), interleaved);
    Store<uint64_t>(out, BytesToAscii<Radix, 6>(bytes));
#endif
}

template <unsigned Radix, unsigned Len>
    requires (2 <= Radix && Radix <= 36 && Len % 8 == 0 &&
              BitsNeeded(Radix, Len) <= kFastconvMaxBits)
constexpr void int2str(char* out, result_t<Radix, Len> value) noexcept {
    constexpr auto kGroup = Pow<result_t<Radix, Len>>(Radix, 8);

    if constexpr (Len > 8) {
        int2str<Radix, Len - 8>(out, static_cast<result_t<Radix, Len - 8>>(value / kGroup));
        int2str<Radix, 8>(out + Len - 8,
                          static_cast<result_t<Radix, 8>>(value % kGroup));
    } else {
        constexpr auto r2 = Pow<uint64_t>(Radix, 2);
        constexpr auto r4 = r2 * r2;

        const uint64_t v = static_cast<uint64_t>(value);
        const uint64_t h = v / r4;
        const uint64_t l = v % r4;
        const uint16_t pair[4] = {static_cast<uint16_t>(h / r2), static_cast<uint16_t>(h % r2),
                                  static_cast<uint16_t>(l / r2), static_cast<uint16_t>(l % r2)};
        EightDigits<Radix>(out, pair);
    }
}

}  // namespace simd

#endif  // SIMD available
