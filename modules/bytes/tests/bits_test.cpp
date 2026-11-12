/*
 * Byte swap, bit counting, rotate, bit reversal, bit fields, and alignment
 * against references written bit by bit in this file: every 8-bit pattern at
 * every bit position, every 16-bit value, each power of two and its
 * neighbours, every rotate count, every field position, and random values
 * of every bit width. The references are loops with no cleverness, so that
 * they are obviously the C23 <stdbit.h> definitions, 0 included.
 */
#include <gtest/gtest.h>

#include <elemental/bytes.h>

#include <cstdint>

namespace
{

/* ---- References --------------------------------------------------------- */

bool Bit(uint64_t value, unsigned i)
{
    return ((value >> i) & 1u) != 0;
}

unsigned RefLeadingZeros(uint64_t value, unsigned width)
{
    unsigned n = 0;
    for (unsigned i = width; i-- > 0 && !Bit(value, i);) {
        n++;
    }
    return n;
}

unsigned RefTrailingZeros(uint64_t value, unsigned width)
{
    unsigned n = 0;
    for (unsigned i = 0; i < width && !Bit(value, i); i++) {
        n++;
    }
    return n;
}

unsigned RefCountOnes(uint64_t value, unsigned width)
{
    unsigned n = 0;
    for (unsigned i = 0; i < width; i++) {
        n += Bit(value, i) ? 1u : 0u;
    }
    return n;
}

/* The highest power of two <= value, or 0. */
uint64_t RefBitFloor(uint64_t value, unsigned width)
{
    for (unsigned i = width; i-- > 0;) {
        if (Bit(value, i)) {
            return uint64_t{1} << i;
        }
    }
    return 0;
}

/* The lowest power of two >= value, or 0 when none fits in `width` bits. */
uint64_t RefBitCeil(uint64_t value, unsigned width)
{
    for (unsigned i = 0; i < width; i++) {
        if ((uint64_t{1} << i) >= value) {
            return uint64_t{1} << i;
        }
    }
    return 0;
}

unsigned RefLeadingOnes(uint64_t value, unsigned width)
{
    unsigned n = 0;
    for (unsigned i = width; i-- > 0 && Bit(value, i);) {
        n++;
    }
    return n;
}

unsigned RefTrailingOnes(uint64_t value, unsigned width)
{
    unsigned n = 0;
    for (unsigned i = 0; i < width && Bit(value, i); i++) {
        n++;
    }
    return n;
}

/* Bit i of the input is bit (i + count) % width of the output. */
uint64_t RefRotateLeft(uint64_t value, uint64_t count, unsigned width)
{
    uint64_t rotated = 0;
    for (unsigned i = 0; i < width; i++) {
        if (Bit(value, i)) {
            rotated |= uint64_t{1} << ((i + count) % width);
        }
    }
    return rotated;
}

uint64_t RefReverse(uint64_t value, unsigned width)
{
    uint64_t reversed = 0;
    for (unsigned i = 0; i < width; i++) {
        if (Bit(value, i)) {
            reversed |= uint64_t{1} << (width - 1 - i);
        }
    }
    return reversed;
}

uint64_t RefMask(unsigned width)
{
    uint64_t mask = 0;
    for (unsigned i = 0; i < width; i++) {
        mask |= uint64_t{1} << i;
    }
    return mask;
}

uint64_t RefFieldGet(uint64_t value, unsigned shift, unsigned width)
{
    uint64_t field = 0;
    for (unsigned i = 0; i < width; i++) {
        if (Bit(value, shift + i)) {
            field |= uint64_t{1} << i;
        }
    }
    return field;
}

/* Bit by bit: the bits of the field's range come from `field`, the rest from
 * `value`. */
uint64_t RefFieldSet(uint64_t value, unsigned shift, unsigned width, uint64_t field)
{
    uint64_t result = value;
    for (unsigned i = 0; i < width; i++) {
        const uint64_t bit = uint64_t{1} << (shift + i);
        result = Bit(field, i) ? (result | bit) : (result & ~bit);
    }
    return result;
}

/* By remainder, not by masking, so that it is a different computation. 0 when
 * the multiple does not fit in `width` bits. */
uint64_t RefAlignUp(uint64_t value, uint64_t align, unsigned width)
{
    const uint64_t max = width == 64 ? UINT64_MAX : (uint64_t{1} << width) - 1;
    const uint64_t rest = value % align;
    if (rest == 0) {
        return value;
    }
    return value > max - (align - rest) ? 0 : value + (align - rest);
}

uint64_t RefAlignDown(uint64_t value, uint64_t align)
{
    return value - value % align;
}

uint64_t RefSwap(uint64_t value, unsigned bytes)
{
    uint64_t swapped = 0;
    for (unsigned i = 0; i < bytes; i++) {
        swapped = swapped << 8 | ((value >> (8 * i)) & 0xFFu);
    }
    return swapped;
}

/* Every function of one width against the references. */
void ExpectBits32(uint32_t v)
{
    EXPECT_EQ(el_bytes_leading_zeros32(v), RefLeadingZeros(v, 32)) << std::hex << v;
    EXPECT_EQ(el_bytes_trailing_zeros32(v), RefTrailingZeros(v, 32)) << std::hex << v;
    EXPECT_EQ(el_bytes_count_ones32(v), RefCountOnes(v, 32)) << std::hex << v;
    EXPECT_EQ(el_bytes_has_single_bit32(v), RefCountOnes(v, 32) == 1) << std::hex << v;
    EXPECT_EQ(el_bytes_bit_width32(v), 32 - RefLeadingZeros(v, 32)) << std::hex << v;
    EXPECT_EQ(el_bytes_bit_floor32(v), RefBitFloor(v, 32)) << std::hex << v;
    EXPECT_EQ(el_bytes_bit_ceil32(v), RefBitCeil(v, 32)) << std::hex << v;
    EXPECT_EQ(el_bytes_leading_ones32(v), RefLeadingOnes(v, 32)) << std::hex << v;
    EXPECT_EQ(el_bytes_trailing_ones32(v), RefTrailingOnes(v, 32)) << std::hex << v;
    EXPECT_EQ(el_bytes_count_zeros32(v), 32 - RefCountOnes(v, 32)) << std::hex << v;
}

void ExpectBits64(uint64_t v)
{
    EXPECT_EQ(el_bytes_leading_zeros64(v), RefLeadingZeros(v, 64)) << std::hex << v;
    EXPECT_EQ(el_bytes_trailing_zeros64(v), RefTrailingZeros(v, 64)) << std::hex << v;
    EXPECT_EQ(el_bytes_count_ones64(v), RefCountOnes(v, 64)) << std::hex << v;
    EXPECT_EQ(el_bytes_has_single_bit64(v), RefCountOnes(v, 64) == 1) << std::hex << v;
    EXPECT_EQ(el_bytes_bit_width64(v), 64 - RefLeadingZeros(v, 64)) << std::hex << v;
    EXPECT_EQ(el_bytes_bit_floor64(v), RefBitFloor(v, 64)) << std::hex << v;
    EXPECT_EQ(el_bytes_bit_ceil64(v), RefBitCeil(v, 64)) << std::hex << v;
    EXPECT_EQ(el_bytes_leading_ones64(v), RefLeadingOnes(v, 64)) << std::hex << v;
    EXPECT_EQ(el_bytes_trailing_ones64(v), RefTrailingOnes(v, 64)) << std::hex << v;
    EXPECT_EQ(el_bytes_count_zeros64(v), 64 - RefCountOnes(v, 64)) << std::hex << v;
}

/* splitmix64; see bytes_test.cpp for why not std::mt19937_64. */
struct Random {
    uint64_t state;
    uint64_t operator()()
    {
        uint64_t z = (state += 0x9E3779B97F4A7C15u);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9u;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBu;
        return z ^ (z >> 31);
    }
};

/* ---- Byte swap ---------------------------------------------------------- */

TEST(BytesSwap, ReversesTheBytes)
{
    EXPECT_EQ(el_bytes_swap16(0x0102u), 0x0201u);
    EXPECT_EQ(el_bytes_swap32(0x01020304u), 0x04030201u);
    EXPECT_EQ(el_bytes_swap64(0x0102030405060708u), 0x0807060504030201u);
    EXPECT_EQ(el_bytes_swap16(0xFF00u), 0x00FFu);
    EXPECT_EQ(el_bytes_swap32(0xFF000000u), 0x000000FFu);
    EXPECT_EQ(el_bytes_swap64(0xFF00000000000000u), 0x00000000000000FFu);
}

TEST(BytesSwap, EverySixteenBitValueMatchesTheReference)
{
    for (uint32_t v = 0; v <= 0xFFFFu; v++) {
        const auto v16 = static_cast<uint16_t>(v);
        ASSERT_EQ(el_bytes_swap16(v16), RefSwap(v16, 2)) << v;
        ASSERT_EQ(el_bytes_swap16(el_bytes_swap16(v16)), v16) << v;
    }
}

TEST(BytesSwap, RandomValuesMatchTheReferenceAndSwapBack)
{
    Random random{20261112};
    for (int i = 0; i < 2000; i++) {
        const uint64_t v = random();
        const auto v32 = static_cast<uint32_t>(v);
        EXPECT_EQ(el_bytes_swap32(v32), RefSwap(v32, 4));
        EXPECT_EQ(el_bytes_swap32(el_bytes_swap32(v32)), v32);
        EXPECT_EQ(el_bytes_swap64(v), RefSwap(v, 8));
        EXPECT_EQ(el_bytes_swap64(el_bytes_swap64(v)), v);
    }
}

TEST(BytesSwap, SwappingConvertsBetweenTheByteOrders)
{
    uint8_t buffer[8];
    el_bytes_store_le64(buffer, 0x0102030405060708u);
    EXPECT_EQ(el_bytes_load_be64(buffer), el_bytes_swap64(0x0102030405060708u));
    EXPECT_EQ(el_bytes_load_be32(buffer), el_bytes_swap32(el_bytes_load_le32(buffer)));
    EXPECT_EQ(el_bytes_load_be16(buffer), el_bytes_swap16(el_bytes_load_le16(buffer)));
}

/* ---- Bit counting ------------------------------------------------------- */

TEST(BytesBits, ZeroHasTheC23Results)
{
    EXPECT_EQ(el_bytes_leading_zeros32(0), 32u);
    EXPECT_EQ(el_bytes_leading_zeros64(0), 64u);
    EXPECT_EQ(el_bytes_trailing_zeros32(0), 32u);
    EXPECT_EQ(el_bytes_trailing_zeros64(0), 64u);
    EXPECT_EQ(el_bytes_count_ones32(0), 0u);
    EXPECT_EQ(el_bytes_count_ones64(0), 0u);
    EXPECT_FALSE(el_bytes_has_single_bit32(0));
    EXPECT_FALSE(el_bytes_has_single_bit64(0));
    EXPECT_EQ(el_bytes_bit_width32(0), 0u);
    EXPECT_EQ(el_bytes_bit_width64(0), 0u);
    EXPECT_EQ(el_bytes_bit_floor32(0), 0u);
    EXPECT_EQ(el_bytes_bit_floor64(0), 0u);
    EXPECT_EQ(el_bytes_bit_ceil32(0), 1u);
    EXPECT_EQ(el_bytes_bit_ceil64(0), 1u);
    EXPECT_EQ(el_bytes_leading_ones32(0), 0u);
    EXPECT_EQ(el_bytes_leading_ones64(0), 0u);
    EXPECT_EQ(el_bytes_trailing_ones32(0), 0u);
    EXPECT_EQ(el_bytes_trailing_ones64(0), 0u);
    EXPECT_EQ(el_bytes_count_zeros32(0), 32u);
    EXPECT_EQ(el_bytes_count_zeros64(0), 64u);
}

TEST(BytesBits, AllOnesHasTheExpectedResults)
{
    EXPECT_EQ(el_bytes_leading_zeros32(UINT32_MAX), 0u);
    EXPECT_EQ(el_bytes_leading_zeros64(UINT64_MAX), 0u);
    EXPECT_EQ(el_bytes_trailing_zeros32(UINT32_MAX), 0u);
    EXPECT_EQ(el_bytes_trailing_zeros64(UINT64_MAX), 0u);
    EXPECT_EQ(el_bytes_count_ones32(UINT32_MAX), 32u);
    EXPECT_EQ(el_bytes_count_ones64(UINT64_MAX), 64u);
    EXPECT_EQ(el_bytes_bit_width32(UINT32_MAX), 32u);
    EXPECT_EQ(el_bytes_bit_width64(UINT64_MAX), 64u);
    EXPECT_EQ(el_bytes_bit_floor32(UINT32_MAX), 0x80000000u);
    EXPECT_EQ(el_bytes_bit_floor64(UINT64_MAX), 0x8000000000000000u);
    EXPECT_EQ(el_bytes_leading_ones32(UINT32_MAX), 32u);
    EXPECT_EQ(el_bytes_leading_ones64(UINT64_MAX), 64u);
    EXPECT_EQ(el_bytes_trailing_ones32(UINT32_MAX), 32u);
    EXPECT_EQ(el_bytes_trailing_ones64(UINT64_MAX), 64u);
    EXPECT_EQ(el_bytes_count_zeros32(UINT32_MAX), 0u);
    EXPECT_EQ(el_bytes_count_zeros64(UINT64_MAX), 0u);
}

TEST(BytesBits, OnesAndZerosCountFromTheEnds)
{
    EXPECT_EQ(el_bytes_leading_ones32(0xF0000000u), 4u);
    EXPECT_EQ(el_bytes_leading_ones32(0xFFFFFFFEu), 31u);
    EXPECT_EQ(el_bytes_leading_ones32(0x7FFFFFFFu), 0u);
    EXPECT_EQ(el_bytes_leading_ones64(0xFFFFFFFF00000000u), 32u);
    EXPECT_EQ(el_bytes_leading_ones64(0xFFFFFFFFFFFFFFFEu), 63u);
    EXPECT_EQ(el_bytes_trailing_ones32(0x0000000Fu), 4u);
    EXPECT_EQ(el_bytes_trailing_ones32(0x7FFFFFFFu), 31u);
    EXPECT_EQ(el_bytes_trailing_ones32(0xFFFFFFFEu), 0u);
    EXPECT_EQ(el_bytes_trailing_ones64(0x00000000FFFFFFFFu), 32u);
    EXPECT_EQ(el_bytes_trailing_ones64(0x7FFFFFFFFFFFFFFFu), 63u);
    EXPECT_EQ(el_bytes_count_zeros32(0x0000FFFFu), 16u);
    EXPECT_EQ(el_bytes_count_zeros64(0xF0F0F0F0F0F0F0F0u), 32u);
}

TEST(BytesBits, BitCeilIsZeroWhenThePowerDoesNotFit)
{
    EXPECT_EQ(el_bytes_bit_ceil32(0x80000000u), 0x80000000u);
    EXPECT_EQ(el_bytes_bit_ceil32(0x80000001u), 0u);
    EXPECT_EQ(el_bytes_bit_ceil32(UINT32_MAX), 0u);
    EXPECT_EQ(el_bytes_bit_ceil64(0x8000000000000000u), 0x8000000000000000u);
    EXPECT_EQ(el_bytes_bit_ceil64(0x8000000000000001u), 0u);
    EXPECT_EQ(el_bytes_bit_ceil64(UINT64_MAX), 0u);
    EXPECT_EQ(el_bytes_bit_ceil32(1), 1u);
    EXPECT_EQ(el_bytes_bit_ceil64(1), 1u);
}

TEST(BytesBits, EveryPowerOfTwoAndItsNeighboursMatchTheReference)
{
    for (unsigned i = 0; i < 64; i++) {
        const uint64_t p = uint64_t{1} << i;
        for (uint64_t v : {p - 1, p, p + 1}) {
            ExpectBits64(v);
            if (i < 32) {
                ExpectBits32(static_cast<uint32_t>(v));
            }
        }
        EXPECT_TRUE(el_bytes_has_single_bit64(p)) << i;
        EXPECT_EQ(el_bytes_bit_floor64(p), p) << i;
        EXPECT_EQ(el_bytes_bit_ceil64(p), p) << i;
        EXPECT_EQ(el_bytes_trailing_zeros64(p), i);
        EXPECT_EQ(el_bytes_leading_zeros64(p), 63 - i);
    }
    ExpectBits32(UINT32_MAX);
}

TEST(BytesBits, EveryByteAtEveryBitPositionMatchesTheReference)
{
    for (uint32_t byte = 0; byte <= 0xFFu; byte++) {
        for (unsigned shift = 0; shift <= 24; shift++) {
            ExpectBits32(byte << shift);
        }
        for (unsigned shift = 0; shift <= 56; shift++) {
            ExpectBits64(uint64_t{byte} << shift);
        }
        if (HasFailure()) {
            return;
        }
    }
}

TEST(BytesBits, EverySixteenBitValueMatchesTheReference)
{
    /* At the bottom and at the top of each width: the references only
     * loop over the sixteen bits that can be set. */
    for (uint32_t v = 0; v <= 0xFFFFu; v++) {
        const unsigned lz = RefLeadingZeros(v, 16);
        const unsigned tz = RefTrailingZeros(v, 16);
        const unsigned ones = RefCountOnes(v, 16);
        const uint64_t floor = RefBitFloor(v, 16);
        const uint32_t high32 = v << 16;
        const uint64_t high64 = uint64_t{v} << 48;
        ASSERT_EQ(el_bytes_leading_zeros32(v), 16 + lz) << v;
        ASSERT_EQ(el_bytes_leading_zeros64(v), 48 + lz) << v;
        ASSERT_EQ(el_bytes_leading_zeros32(high32), v == 0 ? 32 : lz) << v;
        ASSERT_EQ(el_bytes_leading_zeros64(high64), v == 0 ? 64 : lz) << v;
        ASSERT_EQ(el_bytes_trailing_zeros32(v), v == 0 ? 32 : tz) << v;
        ASSERT_EQ(el_bytes_trailing_zeros64(v), v == 0 ? 64 : tz) << v;
        ASSERT_EQ(el_bytes_trailing_zeros32(high32), v == 0 ? 32 : 16 + tz) << v;
        ASSERT_EQ(el_bytes_trailing_zeros64(high64), v == 0 ? 64 : 48 + tz) << v;
        ASSERT_EQ(el_bytes_count_ones32(v), ones) << v;
        ASSERT_EQ(el_bytes_count_ones64(high64 | v), 2 * ones) << v;
        ASSERT_EQ(el_bytes_has_single_bit32(high32), ones == 1) << v;
        ASSERT_EQ(el_bytes_has_single_bit64(v), ones == 1) << v;
        ASSERT_EQ(el_bytes_bit_width32(v), 16 - lz) << v;
        ASSERT_EQ(el_bytes_bit_width64(high64), v == 0 ? 0 : 64 - lz) << v;
        ASSERT_EQ(el_bytes_bit_floor32(v), floor) << v;
        ASSERT_EQ(el_bytes_bit_floor64(high64), floor << 48) << v;
        ASSERT_EQ(el_bytes_bit_ceil32(v), RefBitCeil(v, 32)) << v;
        ASSERT_EQ(el_bytes_bit_ceil64(v), RefBitCeil(v, 32)) << v;
        ASSERT_EQ(el_bytes_leading_ones32(v), RefLeadingOnes(v, 32)) << v;
        ASSERT_EQ(el_bytes_leading_ones32(~high32), RefLeadingOnes(~high32, 32)) << v;
        ASSERT_EQ(el_bytes_trailing_ones64(~high64), RefTrailingOnes(~high64, 64)) << v;
        ASSERT_EQ(el_bytes_count_zeros32(v), 32 - ones) << v;
        ASSERT_EQ(el_bytes_count_zeros64(high64 | v), 64 - 2 * ones) << v;
    }
}

TEST(BytesBits, RandomValuesOfEveryWidthMatchTheReference)
{
    Random random{20261112};
    for (int i = 0; i < 2000; i++) {
        /* Shifted right by 0..63 bits, so that every leading-zero count
         * and every bit width appears, not only those of full-width
         * random numbers. */
        const uint64_t v = random() >> (i % 64);
        ExpectBits64(v);
        ExpectBits32(static_cast<uint32_t>(v >> 32));
        ExpectBits32(static_cast<uint32_t>(v));
        if (HasFailure()) {
            return;
        }
    }
}

/* ---- Rotate and reverse ------------------------------------------------- */

/* Values that differ in where their bits are: none, one at each end, all, a
 * pattern with no symmetry, and its complement. */
const uint64_t kPatterns[] = {0,
                              1,
                              0x80000000u,
                              0x8000000000000000u,
                              0xFFFFFFFFu,
                              UINT64_MAX,
                              0x0123456789ABCDEFu,
                              0xFEDCBA9876543210u,
                              0x00000000FFFF0000u,
                              0xFFFF0000FFFF0000u};

TEST(BytesRotate, KnownValues)
{
    EXPECT_EQ(el_bytes_rotate_left32(0x80000001u, 1), 0x00000003u);
    EXPECT_EQ(el_bytes_rotate_right32(0x80000001u, 1), 0xC0000000u);
    EXPECT_EQ(el_bytes_rotate_left32(0x12345678u, 8), 0x34567812u);
    EXPECT_EQ(el_bytes_rotate_right32(0x12345678u, 8), 0x78123456u);
    EXPECT_EQ(el_bytes_rotate_left64(0x8000000000000001u, 1), 0x0000000000000003u);
    EXPECT_EQ(el_bytes_rotate_right64(0x8000000000000001u, 1), 0xC000000000000000u);
    EXPECT_EQ(el_bytes_rotate_left64(0x0123456789ABCDEFu, 32), 0x89ABCDEF01234567u);
    EXPECT_EQ(el_bytes_rotate_right64(0x0123456789ABCDEFu, 4), 0xF0123456789ABCDEu);
}

TEST(BytesRotate, CountZeroAndTheWidthReturnTheValue)
{
    for (uint64_t v : kPatterns) {
        const auto v32 = static_cast<uint32_t>(v);
        for (uint32_t count : {0u, 32u, 64u, 96u, 0x80000000u}) {
            EXPECT_EQ(el_bytes_rotate_left32(v32, count), v32) << std::hex << v32 << " " << count;
            EXPECT_EQ(el_bytes_rotate_right32(v32, count), v32) << std::hex << v32 << " " << count;
        }
        for (uint32_t count : {0u, 64u, 128u, 0x80000000u}) {
            EXPECT_EQ(el_bytes_rotate_left64(v, count), v) << std::hex << v << " " << count;
            EXPECT_EQ(el_bytes_rotate_right64(v, count), v) << std::hex << v << " " << count;
        }
    }
}

TEST(BytesRotate, EveryCountFromZeroToTwiceTheWidthMatchesTheReference)
{
    for (uint64_t v : kPatterns) {
        const auto v32 = static_cast<uint32_t>(v);
        for (uint32_t count = 0; count <= 64; count++) {
            ASSERT_EQ(el_bytes_rotate_left32(v32, count), RefRotateLeft(v32, count, 32))
                << std::hex << v32 << " " << std::dec << count;
            ASSERT_EQ(el_bytes_rotate_right32(v32, count), RefRotateLeft(v32, 32 - count % 32, 32))
                << std::hex << v32 << " " << std::dec << count;
        }
        for (uint32_t count = 0; count <= 128; count++) {
            ASSERT_EQ(el_bytes_rotate_left64(v, count), RefRotateLeft(v, count, 64))
                << std::hex << v << " " << std::dec << count;
            ASSERT_EQ(el_bytes_rotate_right64(v, count), RefRotateLeft(v, 64 - count % 64, 64))
                << std::hex << v << " " << std::dec << count;
        }
    }
}

TEST(BytesRotate, ALargeCountIsTakenModuloTheWidth)
{
    EXPECT_EQ(el_bytes_rotate_left32(0x12345678u, UINT32_MAX),
              el_bytes_rotate_left32(0x12345678u, 31));
    EXPECT_EQ(el_bytes_rotate_right32(0x12345678u, UINT32_MAX),
              el_bytes_rotate_right32(0x12345678u, 31));
    EXPECT_EQ(el_bytes_rotate_left64(0x0123456789ABCDEFu, UINT32_MAX),
              el_bytes_rotate_left64(0x0123456789ABCDEFu, 63));
    EXPECT_EQ(el_bytes_rotate_right64(0x0123456789ABCDEFu, UINT32_MAX),
              el_bytes_rotate_right64(0x0123456789ABCDEFu, 63));
}

TEST(BytesRotate, RandomValuesAndCountsMatchTheReferenceAndRotateBack)
{
    Random random{20261113};
    for (int i = 0; i < 2000; i++) {
        const uint64_t v = random();
        const auto v32 = static_cast<uint32_t>(v);
        const auto count = static_cast<uint32_t>(random());
        EXPECT_EQ(el_bytes_rotate_left32(v32, count), RefRotateLeft(v32, count, 32));
        EXPECT_EQ(el_bytes_rotate_left64(v, count), RefRotateLeft(v, count, 64));
        EXPECT_EQ(el_bytes_rotate_right32(el_bytes_rotate_left32(v32, count), count), v32);
        EXPECT_EQ(el_bytes_rotate_right64(el_bytes_rotate_left64(v, count), count), v);
        EXPECT_EQ(el_bytes_rotate_right64(v, count), el_bytes_rotate_left64(v, 64 - count % 64));
        if (HasFailure()) {
            return;
        }
    }
}

TEST(BytesReverseBits, KnownValues)
{
    EXPECT_EQ(el_bytes_reverse_bits32(0), 0u);
    EXPECT_EQ(el_bytes_reverse_bits32(1), 0x80000000u);
    EXPECT_EQ(el_bytes_reverse_bits32(UINT32_MAX), UINT32_MAX);
    EXPECT_EQ(el_bytes_reverse_bits32(0x12345678u), 0x1E6A2C48u);
    EXPECT_EQ(el_bytes_reverse_bits64(0), 0u);
    EXPECT_EQ(el_bytes_reverse_bits64(1), 0x8000000000000000u);
    EXPECT_EQ(el_bytes_reverse_bits64(UINT64_MAX), UINT64_MAX);
    EXPECT_EQ(el_bytes_reverse_bits64(0x0123456789ABCDEFu), 0xF7B3D591E6A2C480u);
}

TEST(BytesReverseBits, EveryBitMovesToTheOppositePosition)
{
    for (unsigned i = 0; i < 64; i++) {
        EXPECT_EQ(el_bytes_reverse_bits64(uint64_t{1} << i), uint64_t{1} << (63 - i)) << i;
        if (i < 32) {
            EXPECT_EQ(el_bytes_reverse_bits32(uint32_t{1} << i), uint32_t{1} << (31 - i)) << i;
        }
    }
}

TEST(BytesReverseBits, EverySixteenBitValueMatchesTheReference)
{
    for (uint32_t v = 0; v <= 0xFFFFu; v++) {
        ASSERT_EQ(el_bytes_reverse_bits32(v), RefReverse(v, 32)) << v;
        ASSERT_EQ(el_bytes_reverse_bits32(v << 16), RefReverse(v << 16, 32)) << v;
        ASSERT_EQ(el_bytes_reverse_bits64(uint64_t{v} << 24), RefReverse(uint64_t{v} << 24, 64))
            << v;
    }
}

TEST(BytesReverseBits, RandomValuesMatchTheReferenceAndReverseBack)
{
    Random random{20261113};
    for (int i = 0; i < 2000; i++) {
        const uint64_t v = random();
        const auto v32 = static_cast<uint32_t>(v);
        EXPECT_EQ(el_bytes_reverse_bits32(v32), RefReverse(v32, 32));
        EXPECT_EQ(el_bytes_reverse_bits64(v), RefReverse(v, 64));
        EXPECT_EQ(el_bytes_reverse_bits32(el_bytes_reverse_bits32(v32)), v32);
        EXPECT_EQ(el_bytes_reverse_bits64(el_bytes_reverse_bits64(v)), v);
        /* Reversing the bits and the bytes, in either order. */
        EXPECT_EQ(el_bytes_reverse_bits64(el_bytes_swap64(v)),
                  el_bytes_swap64(el_bytes_reverse_bits64(v)));
        if (HasFailure()) {
            return;
        }
    }
}

/* ---- Bit fields --------------------------------------------------------- */

TEST(BytesBitField, MaskOfEveryWidthMatchesTheReference)
{
    for (unsigned width = 0; width <= 32; width++) {
        EXPECT_EQ(el_bytes_mask32(width), RefMask(width)) << width;
    }
    for (unsigned width = 0; width <= 64; width++) {
        EXPECT_EQ(el_bytes_mask64(width), RefMask(width)) << width;
    }
}

TEST(BytesBitField, MaskAtTheBoundaries)
{
    EXPECT_EQ(el_bytes_mask32(0), 0u);
    EXPECT_EQ(el_bytes_mask32(1), 1u);
    EXPECT_EQ(el_bytes_mask32(31), 0x7FFFFFFFu);
    EXPECT_EQ(el_bytes_mask32(32), UINT32_MAX);
    EXPECT_EQ(el_bytes_mask64(0), 0u);
    EXPECT_EQ(el_bytes_mask64(1), 1u);
    EXPECT_EQ(el_bytes_mask64(32), 0xFFFFFFFFu);
    EXPECT_EQ(el_bytes_mask64(33), 0x1FFFFFFFFu);
    EXPECT_EQ(el_bytes_mask64(63), 0x7FFFFFFFFFFFFFFFu);
    EXPECT_EQ(el_bytes_mask64(64), UINT64_MAX);
}

TEST(BytesBitField, KnownFields)
{
    EXPECT_EQ(el_bytes_field_get32(0xABCD1234u, 16, 8), 0xCDu);
    EXPECT_EQ(el_bytes_field_get32(0xABCD1234u, 0, 32), 0xABCD1234u);
    EXPECT_EQ(el_bytes_field_get32(0xABCD1234u, 31, 1), 1u);
    EXPECT_EQ(el_bytes_field_get64(0x0123456789ABCDEFu, 28, 12), 0x678u);
    EXPECT_EQ(el_bytes_field_get64(0x0123456789ABCDEFu, 0, 64), 0x0123456789ABCDEFu);
    EXPECT_EQ(el_bytes_field_get64(0x8000000000000000u, 63, 1), 1u);
    EXPECT_EQ(el_bytes_field_set32(0xFFFFFFFFu, 8, 8, 0x12), 0xFFFF12FFu);
    EXPECT_EQ(el_bytes_field_set32(0, 0, 32, 0xABCD1234u), 0xABCD1234u);
    EXPECT_EQ(el_bytes_field_set32(0, 31, 1, 1), 0x80000000u);
    EXPECT_EQ(el_bytes_field_set64(UINT64_MAX, 28, 12, 0x123), 0xFFFFFF123FFFFFFFu);
    EXPECT_EQ(el_bytes_field_set64(0, 0, 64, 0x0123456789ABCDEFu), 0x0123456789ABCDEFu);
    EXPECT_EQ(el_bytes_field_set64(0, 63, 1, 1), 0x8000000000000000u);
}

TEST(BytesBitField, AnEmptyFieldIsValidAtEveryShiftUpToTheWidth)
{
    for (uint32_t shift = 0; shift <= 32; shift++) {
        EXPECT_EQ(el_bytes_field_get32(UINT32_MAX, shift, 0), 0u) << shift;
        EXPECT_EQ(el_bytes_field_set32(0x12345678u, shift, 0, UINT32_MAX), 0x12345678u) << shift;
    }
    for (uint32_t shift = 0; shift <= 64; shift++) {
        EXPECT_EQ(el_bytes_field_get64(UINT64_MAX, shift, 0), 0u) << shift;
        EXPECT_EQ(el_bytes_field_set64(0x0123456789ABCDEFu, shift, 0, UINT64_MAX),
                  0x0123456789ABCDEFu)
            << shift;
    }
}

TEST(BytesBitField, EveryFieldPositionMatchesTheReference)
{
    const uint64_t values[] = {0, UINT64_MAX, 0x0123456789ABCDEFu, 0xFEDCBA9876543210u};
    const uint64_t fields[] = {0, 1, UINT64_MAX, 0xA5A5A5A5A5A5A5A5u, 0x5A5A5A5A5A5A5A5Au};
    for (uint32_t width = 0; width <= 32; width++) {
        for (uint32_t shift = 0; shift + width <= 32; shift++) {
            for (uint64_t value : values) {
                const auto v = static_cast<uint32_t>(value);
                ASSERT_EQ(el_bytes_field_get32(v, shift, width), RefFieldGet(v, shift, width))
                    << std::hex << v << " " << std::dec << shift << " " << width;
                for (uint64_t field : fields) {
                    const auto f = static_cast<uint32_t>(field);
                    ASSERT_EQ(el_bytes_field_set32(v, shift, width, f),
                              RefFieldSet(v, shift, width, f))
                        << std::hex << v << " " << f << " " << std::dec << shift << " " << width;
                }
            }
        }
    }
    for (uint32_t width = 0; width <= 64; width++) {
        for (uint32_t shift = 0; shift + width <= 64; shift++) {
            for (uint64_t v : values) {
                ASSERT_EQ(el_bytes_field_get64(v, shift, width), RefFieldGet(v, shift, width))
                    << std::hex << v << " " << std::dec << shift << " " << width;
                for (uint64_t f : fields) {
                    ASSERT_EQ(el_bytes_field_set64(v, shift, width, f),
                              RefFieldSet(v, shift, width, f))
                        << std::hex << v << " " << f << " " << std::dec << shift << " " << width;
                }
            }
        }
    }
}

TEST(BytesBitField, SettingAFieldIgnoresItsBitsAboveTheWidthAndKeepsTheOthers)
{
    /* The field's own bits above `width` would land in the next field. */
    EXPECT_EQ(el_bytes_field_set32(0, 4, 4, 0xFFFFFFFFu), 0x000000F0u);
    EXPECT_EQ(el_bytes_field_set32(UINT32_MAX, 4, 4, 0xFFFFFF00u), 0xFFFFFF0Fu);
    EXPECT_EQ(el_bytes_field_set32(0, 0, 1, 2), 0u);
    EXPECT_EQ(el_bytes_field_set64(0, 36, 4, UINT64_MAX), 0x000000F000000000u);
    EXPECT_EQ(el_bytes_field_set64(UINT64_MAX, 36, 4, 0xFFFFFFFFFFFFFFF0u), 0xFFFFFF0FFFFFFFFFu);
}

TEST(BytesBitField, RandomFieldsMatchTheReferenceAndReadBackWhatWasSet)
{
    Random random{20261113};
    for (int i = 0; i < 2000; i++) {
        const uint64_t v = random();
        const uint64_t f = random();
        const auto width64 = static_cast<uint32_t>(random() % 65);
        const auto shift64 = static_cast<uint32_t>(random() % (65 - width64));
        const auto width32 = static_cast<uint32_t>(random() % 33);
        const auto shift32 = static_cast<uint32_t>(random() % (33 - width32));
        const auto v32 = static_cast<uint32_t>(v);
        const auto f32 = static_cast<uint32_t>(f);

        const uint64_t set64 = el_bytes_field_set64(v, shift64, width64, f);
        EXPECT_EQ(set64, RefFieldSet(v, shift64, width64, f));
        EXPECT_EQ(el_bytes_field_get64(set64, shift64, width64), f & el_bytes_mask64(width64));
        /* Outside the field nothing changed. */
        const uint64_t outside = ~(el_bytes_mask64(width64) << (shift64 % 64));
        if (width64 != 0) {
            EXPECT_EQ(set64 & outside, v & outside);
        }

        const uint32_t set32 = el_bytes_field_set32(v32, shift32, width32, f32);
        EXPECT_EQ(set32, RefFieldSet(v32, shift32, width32, f32));
        EXPECT_EQ(el_bytes_field_get32(set32, shift32, width32), f32 & el_bytes_mask32(width32));
        if (HasFailure()) {
            return;
        }
    }
}

/* ---- Alignment ---------------------------------------------------------- */

TEST(BytesAlign, KnownValues)
{
    EXPECT_EQ(el_bytes_align_up32(0, 16), 0u);
    EXPECT_EQ(el_bytes_align_up32(1, 16), 16u);
    EXPECT_EQ(el_bytes_align_up32(16, 16), 16u);
    EXPECT_EQ(el_bytes_align_up32(17, 16), 32u);
    EXPECT_EQ(el_bytes_align_up32(17, 1), 17u);
    EXPECT_EQ(el_bytes_align_down32(0, 16), 0u);
    EXPECT_EQ(el_bytes_align_down32(15, 16), 0u);
    EXPECT_EQ(el_bytes_align_down32(16, 16), 16u);
    EXPECT_EQ(el_bytes_align_down32(31, 16), 16u);
    EXPECT_EQ(el_bytes_align_down32(31, 1), 31u);
    EXPECT_TRUE(el_bytes_is_aligned32(0, 16));
    EXPECT_TRUE(el_bytes_is_aligned32(48, 16));
    EXPECT_FALSE(el_bytes_is_aligned32(49, 16));
    EXPECT_TRUE(el_bytes_is_aligned32(49, 1));
    EXPECT_EQ(el_bytes_align_up64(0x100000001u, 0x100000000u), 0x200000000u);
    EXPECT_EQ(el_bytes_align_down64(0x1FFFFFFFFu, 0x100000000u), 0x100000000u);
    EXPECT_TRUE(el_bytes_is_aligned64(0x300000000u, 0x100000000u));
    EXPECT_FALSE(el_bytes_is_aligned64(0x300000001u, 0x100000000u));
}

TEST(BytesAlign, AlignUpIsZeroWhenTheMultipleDoesNotFit)
{
    EXPECT_EQ(el_bytes_align_up32(UINT32_MAX, 1), UINT32_MAX);
    EXPECT_EQ(el_bytes_align_up32(UINT32_MAX, 2), 0u);
    EXPECT_EQ(el_bytes_align_up32(UINT32_MAX - 15, 16), UINT32_MAX - 15);
    EXPECT_EQ(el_bytes_align_up32(UINT32_MAX - 14, 16), 0u);
    EXPECT_EQ(el_bytes_align_up32(0x80000000u, 0x80000000u), 0x80000000u);
    EXPECT_EQ(el_bytes_align_up32(0x80000001u, 0x80000000u), 0u);
    EXPECT_EQ(el_bytes_align_up64(UINT64_MAX, 1), UINT64_MAX);
    EXPECT_EQ(el_bytes_align_up64(UINT64_MAX, 2), 0u);
    EXPECT_EQ(el_bytes_align_up64(0x8000000000000000u, 0x8000000000000000u), 0x8000000000000000u);
    EXPECT_EQ(el_bytes_align_up64(0x8000000000000001u, 0x8000000000000000u), 0u);
    /* The documented test for it. */
    EXPECT_LT(el_bytes_align_up32(UINT32_MAX, 4), UINT32_MAX);
    EXPECT_GE(el_bytes_align_up32(UINT32_MAX - 3, 4), UINT32_MAX - 3);
}

TEST(BytesAlign, EveryPowerOfTwoMatchesTheReference)
{
    Random random{20261113};
    for (unsigned i = 0; i < 64; i++) {
        const uint64_t align = uint64_t{1} << i;
        uint64_t values[] = {0,
                             1,
                             align - 1,
                             align,
                             align + 1,
                             3 * align,
                             UINT64_MAX,
                             UINT64_MAX - align,
                             UINT64_MAX - align + 1,
                             UINT64_MAX - align + 2,
                             random(),
                             random() >> 32};
        for (uint64_t v : values) {
            ASSERT_EQ(el_bytes_align_up64(v, align), RefAlignUp(v, align, 64)) << v << " " << align;
            ASSERT_EQ(el_bytes_align_down64(v, align), RefAlignDown(v, align)) << v << " " << align;
            ASSERT_EQ(el_bytes_is_aligned64(v, align), v % align == 0) << v << " " << align;
            if (i < 32) {
                const auto a32 = static_cast<uint32_t>(align);
                const auto v32 = static_cast<uint32_t>(v);
                ASSERT_EQ(el_bytes_align_up32(v32, a32), RefAlignUp(v32, a32, 32))
                    << v32 << " " << a32;
                ASSERT_EQ(el_bytes_align_down32(v32, a32), RefAlignDown(v32, a32))
                    << v32 << " " << a32;
                ASSERT_EQ(el_bytes_is_aligned32(v32, a32), v32 % a32 == 0) << v32 << " " << a32;
            }
        }
    }
}

TEST(BytesAlign, RandomValuesAndAlignmentsMatchTheReferenceAndSatisfyTheirDefinition)
{
    Random random{20261113};
    for (int i = 0; i < 2000; i++) {
        /* Shifted, so that values near the top of the range, where
         * align_up overflows, and small ones both appear. */
        const uint64_t v = random() >> (i % 64) << (i % 7 == 0 ? 63 - i % 64 : 0);
        const uint64_t align = uint64_t{1} << (random() % 64);
        const uint64_t up = el_bytes_align_up64(v, align);
        const uint64_t down = el_bytes_align_down64(v, align);
        ASSERT_EQ(up, RefAlignUp(v, align, 64)) << v << " " << align;
        ASSERT_EQ(down, RefAlignDown(v, align)) << v << " " << align;
        ASSERT_TRUE(el_bytes_is_aligned64(down, align));
        ASSERT_LE(down, v);
        ASSERT_LT(v - down, align);
        if (up >= v) {
            ASSERT_TRUE(el_bytes_is_aligned64(up, align));
            ASSERT_LT(up - v, align);
        }
        const auto v32 = static_cast<uint32_t>(v);
        const auto a32 = static_cast<uint32_t>(uint32_t{1} << (random() % 32));
        ASSERT_EQ(el_bytes_align_up32(v32, a32), RefAlignUp(v32, a32, 32)) << v32 << " " << a32;
        ASSERT_EQ(el_bytes_align_down32(v32, a32), RefAlignDown(v32, a32)) << v32 << " " << a32;
    }
}

} // namespace
