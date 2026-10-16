#pragma once

#include "utils.h"

#include <bit>
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

#if defined(__x86_64__) || defined(_M_X64)
// Scalar decimal blocks use the same 4-digit parallel reduction as Abseil.
// On x86-64 this avoids the pair-table loads; ARM64 measurements favor the
// table path below. The arithmetic is endian-neutral because Store writes the
// packed bytes.
namespace fastconv_detail {

constexpr uint64_t PrepareDecimalEight(uint32_t value) noexcept {
    constexpr uint64_t kDiv100Mul = 10486;
    constexpr uint64_t kDiv10Mul = 103;
    const uint32_t high = value / 10000;
    const uint32_t low = value - high * 10000;
    const uint64_t merged = uint64_t(high) | (uint64_t(low) << 32);
    const uint64_t div100 = ((merged * kDiv100Mul) >> 20) &
                            ((uint64_t(0x7f) << 32) | 0x7f);
    const uint64_t mod100 = merged - 100 * div100;
    const uint64_t hundreds = (mod100 << 16) + div100;
    uint64_t tens = (hundreds * kDiv10Mul) >> 10;
    tens &= (uint64_t(0xf) << 48) | (uint64_t(0xf) << 32) |
            (uint64_t(0xf) << 16) | 0xf;
    tens += (hundreds - 10 * tens) << 8;
    return tens;
}

constexpr void Decimal16Scalar(char* out, uint64_t value) noexcept {
    constexpr uint64_t kBase = 100000000;
    const uint32_t high = static_cast<uint32_t>(value / kBase);
    const uint32_t low = static_cast<uint32_t>(value - uint64_t(high) * kBase);
    constexpr uint64_t kZero = 0x3030303030303030ull;
    Store<uint64_t>(out, PrepareDecimalEight(high) + kZero);
    Store<uint64_t>(out + 8, PrepareDecimalEight(low) + kZero);
}

}  // namespace fastconv_detail
#endif

// Four-digit tables stay reasonably small only for these radices: 324 bytes
// for radix 3, then 2500, 5184, and 9604 bytes for radices 5, 6, and 7.
template <unsigned Radix>
concept QuadTableRadix = Radix == 3 || Radix == 5 || Radix == 6 || Radix == 7;

template <unsigned Radix>
    requires QuadTableRadix<Radix>
struct Quads {
    static constexpr std::size_t kSize = Radix * Radix * Radix * Radix;
    uint32_t packed[kSize];

    constexpr Quads() : packed{} {
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
    requires QuadTableRadix<Radix>
inline constexpr Quads<Radix> kQuads{};

template <unsigned Radix>
concept Pow2Radix = Radix == 2 || Radix == 4 || Radix == 8 || Radix == 16 || Radix == 32;

#if FASTCONV_HAS_NATIVE_UINT128
// A reciprocal at the width of the full value gives a quotient that is exact
// or one too high.  The supported splits cover the fixed-width cases
// where the divisor is at most 64 bits but the dividend is wider than 64 bits:
// 64 digits as two 32-digit blocks for radix 3, 32 digits as two 16-digit
// blocks, and 16 digits as two 8-digit blocks.
template <unsigned Radix, unsigned Digits, unsigned Chunk>
concept ReciprocalSplit =
    !Pow2Radix<Radix> && Digits == 2 * Chunk &&
    BitsNeeded(Radix, Digits) > 64 && BitsNeeded(Radix, Digits) < 128 &&
    BitsNeeded(Radix, Chunk) <= 64;

namespace fastconv_detail {

template <unsigned Radix, unsigned Digits, unsigned Chunk>
    requires ReciprocalSplit<Radix, Digits, Chunk>
inline constexpr unsigned kReciprocalPrecision = BitsNeeded(Radix, Digits);

template <unsigned Radix, unsigned Digits, unsigned Chunk>
    requires ReciprocalSplit<Radix, Digits, Chunk>
inline constexpr fastconv_uint128_t kReciprocalBase =
    Pow<fastconv_uint128_t>(Radix, Chunk);

template <unsigned Radix, unsigned Digits, unsigned Chunk>
    requires ReciprocalSplit<Radix, Digits, Chunk>
inline constexpr fastconv_uint128_t kReciprocalMagicValue =
    (static_cast<fastconv_uint128_t>(1) << kReciprocalPrecision<Radix, Digits, Chunk>) /
        kReciprocalBase<Radix, Digits, Chunk> +
    (((static_cast<fastconv_uint128_t>(1) <<
       kReciprocalPrecision<Radix, Digits, Chunk>) %
      kReciprocalBase<Radix, Digits, Chunk>) != 0);

template <unsigned Radix, unsigned Digits, unsigned Chunk>
    requires ReciprocalSplit<Radix, Digits, Chunk>
inline constexpr uint64_t kReciprocalMagic = [] {
    constexpr fastconv_uint128_t magic =
        kReciprocalMagicValue<Radix, Digits, Chunk>;
    static_assert(magic <= static_cast<fastconv_uint128_t>(
                              std::numeric_limits<uint64_t>::max()));
    return static_cast<uint64_t>(magic);
}();

// For some splits the reciprocal product is known to fit in 64 bits.  Keeping
// that intermediate narrow lets the compiler use one low-half multiply and one
// multiply-add instead of constructing a full 128-bit sum.  The bound is exact:
// high < 2^H and the high half of low * magic is below magic, so their sum is
// below 2^H * magic.
template <unsigned Radix, unsigned Digits, unsigned Chunk>
concept NarrowReciprocal =
    ReciprocalSplit<Radix, Digits, Chunk> &&
    (kReciprocalPrecision<Radix, Digits, Chunk> - 64) +
            std::bit_width(kReciprocalMagic<Radix, Digits, Chunk>) <=
        64;

// Return the quotient and remainder together so callers do not repeat the
// wide product needed to correct the reciprocal estimate.
struct ReciprocalParts {
    uint64_t quotient;
    uint64_t remainder;
};

template <unsigned Radix, unsigned Digits, unsigned Chunk>
    requires ReciprocalSplit<Radix, Digits, Chunk>
constexpr ReciprocalParts ReciprocalDivide(fastconv_uint128_t value) noexcept {
    constexpr unsigned kPrecision = kReciprocalPrecision<Radix, Digits, Chunk>;
    constexpr unsigned kShift = kPrecision - 64;
    constexpr uint64_t kMagic = kReciprocalMagic<Radix, Digits, Chunk>;
    uint64_t estimate;
    if constexpr (NarrowReciprocal<Radix, Digits, Chunk>) {
        const uint64_t high = static_cast<uint64_t>(value >> 64);
        const uint64_t low = static_cast<uint64_t>(value);
        const uint64_t scaled =
            high * kMagic +
            static_cast<uint64_t>(
                (static_cast<fastconv_uint128_t>(low) * kMagic) >> 64);
        estimate = scaled >> kShift;
    } else {
        const fastconv_uint128_t scaled =
            static_cast<fastconv_uint128_t>(static_cast<uint64_t>(value >> 64)) * kMagic +
            (static_cast<fastconv_uint128_t>(static_cast<uint64_t>(value)) * kMagic >> 64);
        estimate = static_cast<uint64_t>(scaled >> kShift);
    }
    constexpr fastconv_uint128_t kBase = kReciprocalBase<Radix, Digits, Chunk>;
    const fastconv_uint128_t product = static_cast<fastconv_uint128_t>(estimate) * kBase;
    const uint64_t too_high = static_cast<unsigned>(product > value);
    return {estimate - too_high,
            static_cast<uint64_t>(value - product +
                                  static_cast<fastconv_uint128_t>(too_high) * kBase)};
}

template <unsigned Radix, unsigned Digits, unsigned Chunk>
    requires ReciprocalSplit<Radix, Digits, Chunk>
constexpr uint64_t ReciprocalHigh(fastconv_uint128_t value) noexcept {
    return ReciprocalDivide<Radix, Digits, Chunk>(value).quotient;
}

static_assert(kReciprocalMagic<10, 32, 16> == 16225927682921337ull);
static_assert(kReciprocalBase<10, 32, 16> ==
              static_cast<fastconv_uint128_t>(10000000000000000ull));
static_assert(kReciprocalMagicValue<36, 16, 8> <=
              static_cast<fastconv_uint128_t>(std::numeric_limits<uint64_t>::max()));
static_assert(NarrowReciprocal<5, 32, 16>);
static_assert(NarrowReciprocal<6, 32, 16>);
static_assert(NarrowReciprocal<17, 16, 8>);
static_assert(NarrowReciprocal<36, 16, 8>);
static_assert(!NarrowReciprocal<3, 64, 32>);
static_assert(!NarrowReciprocal<10, 32, 16>);

template <unsigned Radix, unsigned Digits, unsigned Chunk>
    requires ReciprocalSplit<Radix, Digits, Chunk>
constexpr bool ReciprocalCheck(fastconv_uint128_t value, uint64_t expected) noexcept {
    return ReciprocalHigh<Radix, Digits, Chunk>(value) == expected;
}

static_assert(ReciprocalCheck<10, 32, 16>(0, 0));
static_assert(ReciprocalCheck<10, 32, 16>(kReciprocalBase<10, 32, 16> - 1, 0));
static_assert(ReciprocalCheck<10, 32, 16>(kReciprocalBase<10, 32, 16>, 1));
static_assert(ReciprocalCheck<10, 32, 16>(
    static_cast<fastconv_uint128_t>(354) * kReciprocalBase<10, 32, 16> - 1, 353));
static_assert(ReciprocalCheck<10, 32, 16>(
    static_cast<fastconv_uint128_t>(kReciprocalBase<10, 32, 16>) *
        kReciprocalBase<10, 32, 16> - 1,
    9999999999999999ull));
static_assert(ReciprocalCheck<3, 64, 32>(0, 0));
static_assert(ReciprocalCheck<3, 64, 32>(kReciprocalBase<3, 64, 32> - 1, 0));
static_assert(ReciprocalCheck<3, 64, 32>(kReciprocalBase<3, 64, 32>, 1));
static_assert(ReciprocalCheck<36, 16, 8>(0, 0));
static_assert(ReciprocalCheck<36, 16, 8>(kReciprocalBase<36, 16, 8> - 1, 0));
static_assert(ReciprocalCheck<36, 16, 8>(kReciprocalBase<36, 16, 8>, 1));

}  // namespace fastconv_detail
#endif


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

namespace fastconv_runtime_detail {

template <typename T>
using unsigned_integer_t = fastconv_unsigned_t<T>;

// Converting through the corresponding unsigned type makes the absolute value
// of the most-negative signed value representable without signed overflow.
template <typename T>
    requires (!std::same_as<std::remove_cv_t<T>, bool>)
constexpr unsigned_integer_t<T> UnsignedMagnitude(T value) noexcept {
    using U = unsigned_integer_t<T>;
    const U bits = static_cast<U>(value);
    if constexpr (FastconvIsSigned<T>) {
        return value < 0 ? U(0) - bits : bits;
    } else {
        return bits;
    }
}

template <typename U>
    requires FastconvUnsignedInteger<U>
constexpr unsigned UnsignedBitWidth(U value) noexcept {
    if constexpr (sizeof(U) <= sizeof(uint64_t)) {
        return static_cast<unsigned>(std::bit_width(static_cast<uint64_t>(value)));
    } else {
        const uint64_t high = static_cast<uint64_t>(value >> 64);
        return high ? 64u + static_cast<unsigned>(std::bit_width(high))
                    : static_cast<unsigned>(
                          std::bit_width(static_cast<uint64_t>(value)));
    }
}

template <typename U>
    requires FastconvUnsignedInteger<U>
constexpr std::size_t RuntimeDigitCount(U value, unsigned radix) noexcept {
    if (value == 0) {
        return 1;
    }
    if ((radix & (radix - 1)) == 0) {
        unsigned radix_bits = 0;
        for (unsigned r = radix; r > 1; r >>= 1) {
            ++radix_bits;
        }
        const unsigned value_bits = UnsignedBitWidth(value);
        return (value_bits + radix_bits - 1) / radix_bits;
    }

    // A runtime radix cannot be strength-reduced to a reciprocal.  Divide by
    // a small power of the radix instead, so the expensive variable divide is
    // paid once per group rather than once per output digit.  The group size
    // is chosen to keep radix^group representable in U on every supported
    // width (radix^8 fits in 64 bits for radix <= 36).
    constexpr unsigned kGroup = sizeof(U) >= 8 ? 8 :
                                sizeof(U) >= 4 ? 4 :
                                sizeof(U) >= 2 ? 2 : 1;
    const U base = static_cast<U>(radix);
    std::size_t digits = 1;
    if constexpr (kGroup > 1) {
        // For short values the setup multiplies cost more than the divisions
        // it could save.  The threshold scales with the operand width and is
        // deliberately conservative; values below it use the small scalar
        // loop below without even constructing group_base.
        constexpr unsigned kThresholdBits = sizeof(U) >= 8 ? 24 :
                                             sizeof(U) >= 4 ? 16 : 8;
        if (value >= (static_cast<U>(1) << kThresholdBits)) {
            U group_base = base;
            const U square = static_cast<U>(base * base);
            if constexpr (kGroup == 2) {
                group_base = square;
            } else {
                const U fourth = static_cast<U>(square * square);
                if constexpr (kGroup == 4) {
                    group_base = fourth;
                } else {
                    group_base = static_cast<U>(fourth * fourth);
                }
            }
            while (value >= group_base) {
                value = static_cast<U>(value / group_base);
                digits += kGroup;
            }
        }
    }
    while (value >= base) {
        value = static_cast<U>(value / base);
        ++digits;
    }
    return digits;
}

}  // namespace fastconv_runtime_detail

template <FastconvInteger T>
constexpr std::size_t int2str_fallback(char* out, T value, unsigned radix) noexcept {
    if (!IsValidRadix(radix)) {
        return 0;
    }
    if constexpr (std::same_as<std::remove_cv_t<T>, bool>) {
        out[0] = static_cast<char>('0' + value);
        return 1;
    } else {
        using U = fastconv_runtime_detail::unsigned_integer_t<T>;
        if constexpr (FastconvIsSigned<T>) {
            if (value < 0) {
                out[0] = '-';
                return 1 + int2str_fallback(
                               out + 1,
                               fastconv_runtime_detail::UnsignedMagnitude(value), radix);
            }
        }

        char reversed[std::numeric_limits<U>::digits];
        std::size_t len = 0;
        U remaining = fastconv_runtime_detail::UnsignedMagnitude(value);
        do {
            const unsigned digit = static_cast<unsigned>(remaining % static_cast<U>(radix));
            reversed[len++] = DigitChar(digit);
            remaining = static_cast<U>(remaining / static_cast<U>(radix));
        } while (remaining != 0);

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
#if defined(__x86_64__) || defined(_M_X64)
    if constexpr (Radix == 10 && Len == 16) {
        fastconv_detail::Decimal16Scalar(out, static_cast<uint64_t>(value));
    } else
#endif
#if FASTCONV_HAS_NATIVE_UINT128
    if constexpr (Len == 64 && ReciprocalSplit<Radix, 64, 32>) {
        const auto parts = fastconv_detail::ReciprocalDivide<Radix, 64, 32>(value);
        int2str<Radix, 32>(out, parts.quotient);
        int2str<Radix, 32>(out + 32, parts.remainder);
    } else if constexpr (Len == 32 && ReciprocalSplit<Radix, 32, 16>) {
        const auto parts = fastconv_detail::ReciprocalDivide<Radix, 32, 16>(value);
        int2str<Radix, 16>(out, parts.quotient);
        int2str<Radix, 16>(out + 16, parts.remainder);
    } else if constexpr (Len == 16 && ReciprocalSplit<Radix, 16, 8>) {
        const auto parts = fastconv_detail::ReciprocalDivide<Radix, 16, 8>(value);
        int2str<Radix, 8>(out, parts.quotient);
        int2str<Radix, 8>(out + 8, parts.remainder);
    } else
#endif
    if constexpr (Len == 1) {
        out[0] = DigitChar(static_cast<unsigned>(value));
    } else if constexpr (Len == 2) {
        const unsigned i = static_cast<unsigned>(value);
        out[0] = kPairs<Radix>.c[i][0];
        out[1] = kPairs<Radix>.c[i][1];
    } else if constexpr (QuadTableRadix<Radix> && Len == 4) {
        Store<uint32_t>(out, kQuads<Radix>.packed[static_cast<std::size_t>(value)]);
    } else {
        constexpr unsigned kLow = Len / 2;
        constexpr unsigned kHigh = Len - kLow;
        constexpr auto kSplit = Pow<result_t<Radix, Len>>(Radix, kLow);
        const auto high = value / kSplit;
        const auto low = value - high * kSplit;

        int2str<Radix, kHigh>(out, static_cast<result_t<Radix, kHigh>>(high));
        int2str<Radix, kLow>(out + kHigh, static_cast<result_t<Radix, kLow>>(low));
    }
}

// Runtime uint64_t values can need 17 to 20 decimal digits.  Routing those
// values through the 32-digit result_t specialization needlessly promotes the
// operation to native uint128 (and falls back to a long serial loop when
// native uint128 is unavailable).  Three bounded 64-bit chunks cover the full
// uint64_t range: an unpadded leading chunk followed by two eight-digit chunks.
namespace fastconv_detail {

template <unsigned Len>
constexpr std::size_t WriteDecimalSmall(char* out, uint64_t value) noexcept {
    static_assert(Len >= 1 && Len <= 8);
    if constexpr (Len == 1) {
        int2str<10, 1>(out, static_cast<uint8_t>(value));
    } else if constexpr (Len == 2) {
        int2str<10, 2>(out, static_cast<uint8_t>(value));
    } else if constexpr (Len == 3) {
        int2str<10, 3>(out, static_cast<uint16_t>(value));
    } else if constexpr (Len == 4) {
        int2str<10, 4>(out, static_cast<uint16_t>(value));
    } else if constexpr (Len == 5) {
        int2str<10, 5>(out, static_cast<uint32_t>(value));
    } else if constexpr (Len == 6) {
        int2str<10, 6>(out, static_cast<uint32_t>(value));
    } else if constexpr (Len == 7) {
        int2str<10, 7>(out, static_cast<uint32_t>(value));
    } else {
        int2str<10, 8>(out, static_cast<uint32_t>(value));
    }
    return Len;
}

constexpr std::size_t WriteDecimalSmallest(char* out, uint64_t value) noexcept {
    if (value < 10) {
        return WriteDecimalSmall<1>(out, value);
    }
    if (value < 100) {
        return WriteDecimalSmall<2>(out, value);
    }
    if (value < 1000) {
        return WriteDecimalSmall<3>(out, value);
    }
    if (value < 10000) {
        return WriteDecimalSmall<4>(out, value);
    }
    if (value < 100000) {
        return WriteDecimalSmall<5>(out, value);
    }
    if (value < 1000000) {
        return WriteDecimalSmall<6>(out, value);
    }
    if (value < 10000000) {
        return WriteDecimalSmall<7>(out, value);
    }
    return WriteDecimalSmall<8>(out, value);
}

#if defined(__x86_64__) || defined(_M_X64)
constexpr void WriteDecimalEightPadded(char* out, uint32_t value) noexcept {
    constexpr uint64_t kZero = 0x3030303030303030ull;
    Store<uint64_t>(out, ::fastconv_detail::PrepareDecimalEight(value) + kZero);
}
#endif

constexpr std::size_t WriteDecimalUint64(char* out, uint64_t value) noexcept {
    constexpr uint64_t kChunk = 100000000ull;
    constexpr uint64_t kChunk2 = 10000000000000000ull;

    if (value < kChunk) {
        return WriteDecimalSmallest(out, value);
    }

    if (value < kChunk2) {
        const uint64_t high = value / kChunk;
        const uint64_t low = value - high * kChunk;
        const std::size_t high_len = WriteDecimalSmallest(out, high);
#if defined(__x86_64__) || defined(_M_X64)
        WriteDecimalEightPadded(out + high_len, static_cast<uint32_t>(low));
#else
        int2str<10, 8>(out + high_len, static_cast<uint32_t>(low));
#endif
        return high_len + 8;
    }

    const uint64_t high = value / kChunk2;
    const uint64_t remainder = value - high * kChunk2;
    const uint64_t middle = remainder / kChunk;
    const uint64_t low = remainder - middle * kChunk;
    const std::size_t high_len = WriteDecimalSmallest(out, high);
#if defined(__x86_64__) || defined(_M_X64)
    WriteDecimalEightPadded(out + high_len, static_cast<uint32_t>(middle));
    WriteDecimalEightPadded(out + high_len + 8, static_cast<uint32_t>(low));
#else
    int2str<10, 8>(out + high_len, static_cast<uint32_t>(middle));
    int2str<10, 8>(out + high_len + 8, static_cast<uint32_t>(low));
#endif
    return high_len + 16;
}

}  // namespace fastconv_detail

template <unsigned Radix, unsigned Len, FastconvInteger T>
constexpr bool int2str_padded(
    char* out, T value, std::size_t output_len) noexcept {
    // A <=64-bit runtime value should not be promoted to uint128 merely
    // because the next fixed-width bucket is wider than the value itself.
    // The chunked path below is both portable to MSVC and faster on the
    // measured GCC/Clang targets.
    if constexpr ((BitsNeeded(Radix, Len) <= 64 ||
                   sizeof(T) > sizeof(uint64_t)) &&
                  requires {
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

template <unsigned Radix, FastconvInteger T>
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

template <unsigned Radix = 2, FastconvInteger T>
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

// Encoding is commonly called with a runtime radix.  A switch lets the
// compiler select the constant-radix body in one branch, while the analogous
// parser switch was measured slower and remains on the compact recursive
// chain in str2int.h.
template <FastconvInteger T>
constexpr bool int2str_dispatch_radix_switch(
    char* out, T value, unsigned radix, std::size_t len) noexcept {
    switch (radix) {
        case 2: return int2str_dispatch_len<2>(out, value, len);
        case 3: return int2str_dispatch_len<3>(out, value, len);
        case 4: return int2str_dispatch_len<4>(out, value, len);
        case 5: return int2str_dispatch_len<5>(out, value, len);
        case 6: return int2str_dispatch_len<6>(out, value, len);
        case 7: return int2str_dispatch_len<7>(out, value, len);
        case 8: return int2str_dispatch_len<8>(out, value, len);
        case 9: return int2str_dispatch_len<9>(out, value, len);
        case 10: return int2str_dispatch_len<10>(out, value, len);
        case 11: return int2str_dispatch_len<11>(out, value, len);
        case 12: return int2str_dispatch_len<12>(out, value, len);
        case 13: return int2str_dispatch_len<13>(out, value, len);
        case 14: return int2str_dispatch_len<14>(out, value, len);
        case 15: return int2str_dispatch_len<15>(out, value, len);
        case 16: return int2str_dispatch_len<16>(out, value, len);
        case 17: return int2str_dispatch_len<17>(out, value, len);
        case 18: return int2str_dispatch_len<18>(out, value, len);
        case 19: return int2str_dispatch_len<19>(out, value, len);
        case 20: return int2str_dispatch_len<20>(out, value, len);
        case 21: return int2str_dispatch_len<21>(out, value, len);
        case 22: return int2str_dispatch_len<22>(out, value, len);
        case 23: return int2str_dispatch_len<23>(out, value, len);
        case 24: return int2str_dispatch_len<24>(out, value, len);
        case 25: return int2str_dispatch_len<25>(out, value, len);
        case 26: return int2str_dispatch_len<26>(out, value, len);
        case 27: return int2str_dispatch_len<27>(out, value, len);
        case 28: return int2str_dispatch_len<28>(out, value, len);
        case 29: return int2str_dispatch_len<29>(out, value, len);
        case 30: return int2str_dispatch_len<30>(out, value, len);
        case 31: return int2str_dispatch_len<31>(out, value, len);
        case 32: return int2str_dispatch_len<32>(out, value, len);
        case 33: return int2str_dispatch_len<33>(out, value, len);
        case 34: return int2str_dispatch_len<34>(out, value, len);
        case 35: return int2str_dispatch_len<35>(out, value, len);
        case 36: return int2str_dispatch_len<36>(out, value, len);
        default: return false;
    }
}

// The fixed-width table is intentionally sparse for narrow runtime values: for
// example, a 22-digit radix-5 value fits in uint64_t but not in the next
// power-of-two template (32 digits). Divide the value into radix^8 chunks in
// that case. radix^8 is below 2^42 for every supported radix, so this path is
// entirely uint64_t arithmetic and never asks the compiler for a 128-bit
// division helper.
template <FastconvInteger U>
    requires (FastconvUnsignedInteger<U> && sizeof(U) <= sizeof(uint64_t))
constexpr std::size_t int2str_chunked64(
    char* out, U value, unsigned radix, std::size_t len) noexcept {
    const uint64_t r = radix;
    const uint64_t r2 = r * r;
    const uint64_t r4 = r2 * r2;
    const uint64_t group = r4 * r4;

    uint64_t remaining = static_cast<uint64_t>(value);
    const std::size_t count = (len - 1) / 8;
    std::size_t offset = len;

    // The low chunks are independent fixed-width fields.  Writing them from
    // the end lets the leading, non-padded chunk be emitted in place too; no
    // scratch arrays (and therefore no stack-protector frame) are required.
    for (std::size_t i = 0; i < count; ++i) {
        const uint64_t chunk = remaining % group;
        remaining /= group;
        offset -= 8;
        if (!int2str_dispatch_radix_switch(
                out + offset, static_cast<uint64_t>(chunk), radix, 8)) {
            uint64_t padded = chunk;
            for (unsigned j = 8; j-- > 0;) {
                out[offset + j] = DigitChar(
                    static_cast<unsigned>(padded % static_cast<uint64_t>(radix)));
                padded /= static_cast<uint64_t>(radix);
            }
        }
    }

    // `remaining` is the leading chunk and has exactly `len - offset` digits.
    // Fill it right-to-left so the caller receives the unpadded representation.
    while (offset != 0) {
        out[--offset] = DigitChar(
            static_cast<unsigned>(remaining % static_cast<uint64_t>(radix)));
        remaining /= static_cast<uint64_t>(radix);
    }
    return len;
}

// A compact table of constant-radix writers keeps the hot short-value path
// small while replacing variable-width division for values that do not fit a
// <=64-bit fixed-width bucket. This path is used even when native uint128 is
// available so conversions of narrower integer types never depend on it.
template <unsigned Radix, FastconvInteger U>
    requires (IsValidRadix(Radix) && FastconvUnsignedInteger<U> &&
              sizeof(U) <= sizeof(uint64_t))
constexpr std::size_t int2str_chunked64_const(
    char* out, U value, std::size_t len) noexcept {
    constexpr uint64_t group = Pow<uint64_t>(Radix, 8);
    uint64_t remaining = static_cast<uint64_t>(value);
    const std::size_t count = (len - 1) / 8;
    std::size_t offset = len;
    for (std::size_t i = 0; i < count; ++i) {
        const uint64_t chunk = remaining % group;
        remaining /= group;
        offset -= 8;
        int2str<Radix, 8>(out + offset,
                          static_cast<result_t<Radix, 8>>(chunk));
    }
    while (offset != 0) {
        out[--offset] = DigitChar(
            static_cast<unsigned>(remaining % static_cast<uint64_t>(Radix)));
        remaining /= static_cast<uint64_t>(Radix);
    }
    return len;
}

// Octal is the one runtime radix whose 16-digit group still fits comfortably
// in 64 bits (`8^16 == 2^48`).  Using that wider group removes one variable
// divide for the 17..22 digit uint64_t cases; the two resulting 8-digit
// fields are handled by the existing packed writer.
template <FastconvInteger U>
    requires (FastconvUnsignedInteger<U> && sizeof(U) <= sizeof(uint64_t))
constexpr std::size_t int2str_chunked64_oct(
    char* out, U value, std::size_t len) noexcept {
    constexpr uint64_t kGroup = Pow<uint64_t>(8, 16);
    constexpr uint64_t kHalf = Pow<uint64_t>(8, 8);
    uint64_t remaining = static_cast<uint64_t>(value);
    const std::size_t count = (len - 1) / 16;
    std::size_t offset = len;

    for (std::size_t i = 0; i < count; ++i) {
        const uint64_t chunk = remaining % kGroup;
        remaining /= kGroup;
        offset -= 16;
        int2str<8, 8>(out + offset,
                      static_cast<result_t<8, 8>>(chunk / kHalf));
        int2str<8, 8>(out + offset + 8,
                      static_cast<result_t<8, 8>>(chunk % kHalf));
    }
    while (offset != 0) {
        out[--offset] = DigitChar(static_cast<unsigned>(remaining & 7u));
        remaining >>= 3;
    }
    return len;
}

template <FastconvInteger U>
    requires (FastconvUnsignedInteger<U> && sizeof(U) <= sizeof(uint64_t))
constexpr std::size_t int2str_chunked64_dispatch(
    char* out, U value, unsigned radix, std::size_t len) noexcept {
    switch (radix) {
        case 8: return int2str_chunked64_oct(out, value, len);
        case 3: return int2str_chunked64_const<3>(out, value, len);
        case 5: return int2str_chunked64_const<5>(out, value, len);
        case 6: return int2str_chunked64_const<6>(out, value, len);
        case 7: return int2str_chunked64_const<7>(out, value, len);
        case 9: return int2str_chunked64_const<9>(out, value, len);
        case 10: return int2str_chunked64_const<10>(out, value, len);
        case 11: return int2str_chunked64_const<11>(out, value, len);
        case 12: return int2str_chunked64_const<12>(out, value, len);
        case 13: return int2str_chunked64_const<13>(out, value, len);
        case 14: return int2str_chunked64_const<14>(out, value, len);
        case 15: return int2str_chunked64_const<15>(out, value, len);
        case 17: return int2str_chunked64_const<17>(out, value, len);
        case 18: return int2str_chunked64_const<18>(out, value, len);
        case 19: return int2str_chunked64_const<19>(out, value, len);
        case 20: return int2str_chunked64_const<20>(out, value, len);
        case 21: return int2str_chunked64_const<21>(out, value, len);
        case 22: return int2str_chunked64_const<22>(out, value, len);
        case 23: return int2str_chunked64_const<23>(out, value, len);
        case 24: return int2str_chunked64_const<24>(out, value, len);
        case 25: return int2str_chunked64_const<25>(out, value, len);
        case 26: return int2str_chunked64_const<26>(out, value, len);
        case 27: return int2str_chunked64_const<27>(out, value, len);
        case 28: return int2str_chunked64_const<28>(out, value, len);
        case 29: return int2str_chunked64_const<29>(out, value, len);
        case 30: return int2str_chunked64_const<30>(out, value, len);
        case 31: return int2str_chunked64_const<31>(out, value, len);
        case 32: return int2str_chunked64_const<32>(out, value, len);
        case 33: return int2str_chunked64_const<33>(out, value, len);
        case 34: return int2str_chunked64_const<34>(out, value, len);
        case 35: return int2str_chunked64_const<35>(out, value, len);
        case 36: return int2str_chunked64_const<36>(out, value, len);
        default: return int2str_chunked64(out, value, radix, len);
    }
}

// Runtime dispatch facade. It first determines the shortest representation;
// supported lengths are then written by the compile-time specialization.
template <FastconvInteger T>
constexpr std::size_t int2str(char* out, T value, unsigned radix) noexcept {
    if (!IsValidRadix(radix)) {
        return 0;
    }
    if constexpr (std::same_as<std::remove_cv_t<T>, bool>) {
        out[0] = static_cast<char>('0' + value);
        return 1;
    } else {
        if constexpr (FastconvIsSigned<T>) {
            using U = fastconv_runtime_detail::unsigned_integer_t<T>;
            const U magnitude = fastconv_runtime_detail::UnsignedMagnitude(value);
            if (value < 0) {
                out[0] = '-';
                return 1 + int2str(out + 1, magnitude, radix);
            }
            return int2str(out, magnitude, radix);
        }
        if constexpr (FastconvIsUnsigned<T> && sizeof(T) <= sizeof(uint64_t) &&
                      !std::same_as<std::remove_cv_t<T>, bool>) {
            if (radix == 10) {
                return fastconv_detail::WriteDecimalUint64(
                    out, static_cast<uint64_t>(value));
            }
        }
        using U = fastconv_runtime_detail::unsigned_integer_t<T>;
        const U magnitude = fastconv_runtime_detail::UnsignedMagnitude(value);
        const std::size_t len =
            fastconv_runtime_detail::RuntimeDigitCount(magnitude, radix);
        if (int2str_dispatch_radix_switch(out, magnitude, radix, len)) {
            return len;
        }
        if constexpr (sizeof(U) <= sizeof(uint64_t)) {
            return int2str_chunked64_dispatch(out, magnitude, radix, len);
        }
        return int2str_fallback(out, magnitude, radix);
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
// Decimal 16-character blocks use a separate reciprocal-division pipeline;
// the general path below remains preferable for other radices and widths.
// ============================================================================
#if defined(FASTCONV_X86_SIMD) || defined(FASTCONV_ARM_SIMD)
#define FASTCONV_INT2STR_HAS_SIMD 1

#if defined(FASTCONV_X86_SIMD)
#include <immintrin.h>
#else
#if defined(_MSC_VER) && !defined(__clang__)
#include <arm64_neon.h>
#else
#include <arm_neon.h>
#endif
#endif

namespace simd {

// Exactly 16 decimal digits, split into two 8-digit chunks and reduced in
// parallel. All reciprocal quotients are exact over the bounded lane ranges.
inline void Decimal16(char* out, uint64_t value) noexcept {
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
    if consteval {
        auto v = value;
        for (unsigned i = Len; i-- > 0;) {
            out[i] = DigitChar(static_cast<unsigned>(v % Radix));
            v /= Radix;
        }
    } else {
        constexpr auto kGroup = Pow<uint64_t>(Radix, 8);

#if FASTCONV_HAS_NATIVE_UINT128
        if constexpr (Len == 64 && ReciprocalSplit<Radix, 64, 32>) {
            const auto parts = fastconv_detail::ReciprocalDivide<Radix, 64, 32>(value);
            int2str<Radix, 32>(out, parts.quotient);
            int2str<Radix, 32>(out + 32, parts.remainder);
        } else if constexpr (Len == 32 && ReciprocalSplit<Radix, 32, 16>) {
            const auto parts = fastconv_detail::ReciprocalDivide<Radix, 32, 16>(value);
            int2str<Radix, 16>(out, parts.quotient);
            int2str<Radix, 16>(out + 16, parts.remainder);
        } else if constexpr (Len == 16 && ReciprocalSplit<Radix, 16, 8>) {
            const auto parts = fastconv_detail::ReciprocalDivide<Radix, 16, 8>(value);
            int2str<Radix, 8>(out, parts.quotient);
            int2str<Radix, 8>(out + 8, parts.remainder);
        } else
#endif
        if constexpr (Radix == 10 && Len == 16) {
            Decimal16(out, static_cast<uint64_t>(value));
        } else if constexpr (Len > 8) {
            int2str<Radix, Len - 8>(
                out, static_cast<result_t<Radix, Len - 8>>(value / kGroup));
            int2str<Radix, 8>(out + Len - 8,
                              static_cast<result_t<Radix, 8>>(value % kGroup));
        } else {
            constexpr auto r2 = Pow<uint64_t>(Radix, 2);
            constexpr auto r4 = r2 * r2;

            const uint64_t v = static_cast<uint64_t>(value);
            const uint64_t h = v / r4;
            const uint64_t l = v % r4;
            const uint16_t pair[4] = {
                static_cast<uint16_t>(h / r2), static_cast<uint16_t>(h % r2),
                static_cast<uint16_t>(l / r2), static_cast<uint16_t>(l % r2)};
            EightDigits<Radix>(out, pair);
        }
    }
}

}  // namespace simd

#endif  // SIMD available
