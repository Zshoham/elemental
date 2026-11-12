/*
 * Byte swap and bit counting against references written bit by bit in this
 * file: every 8-bit pattern at every bit position, every 16-bit value, each
 * power of two and its neighbours, and random values of every bit width.
 * The references are loops with no cleverness, so that they are obviously
 * the C23 <stdbit.h> definitions, 0 included.
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

} // namespace
