/*
 * The three conversions (elemental/fmt.h), against literal expectations.
 * fmt_oracle_test.cpp checks decimal against the compiler's division on the
 * host.
 */
#include <gtest/gtest.h>

#include <elemental/fmt.h>

#include <cstdint>
#include <cstring>
#include <string>

namespace
{

/* Each conversion writes out[0..n) and nothing past it. */
template <typename Convert> std::string Run(Convert convert)
{
    char out[EL_FMT_INT_LEN_MAX + 4];
    std::memset(out, '#', sizeof(out));
    const size_t n = convert(out);
    EXPECT_GE(n, 1u);
    EXPECT_LE(n, size_t{EL_FMT_INT_LEN_MAX});
    for (size_t i = n; i < sizeof(out); i++) {
        EXPECT_EQ(out[i], '#') << "byte " << i << " written past the digits";
    }
    return std::string(out, n);
}

std::string Dec(uint64_t value)
{
    return Run([&](char *out) { return el_fmt_dec_u64(out, EL_FMT_INT_LEN_MAX, value); });
}

std::string Signed(int64_t value)
{
    return Run([&](char *out) { return el_fmt_dec_i64(out, EL_FMT_INT_LEN_MAX, value); });
}

std::string Hex(uint64_t value, uint32_t flags = 0)
{
    return Run([&](char *out) { return el_fmt_hex_u64(out, EL_FMT_INT_LEN_MAX, value, flags); });
}

TEST(Fmt, DecimalMatchesEveryPowerOfTenAndItsNeighbours)
{
    EXPECT_EQ(Dec(0), "0");
    uint64_t power = 1;
    for (int exponent = 0; exponent <= 19; exponent++) {
        for (uint64_t value : {power - 1, power, power + 1}) {
            EXPECT_EQ(Dec(value), std::to_string(value));
        }
        if (exponent < 19) {
            power *= 10;
        }
    }
}

TEST(Fmt, DecimalCrossesTheThirtyTwoBitBoundary)
{
    EXPECT_EQ(Dec(UINT32_MAX), "4294967295");
    EXPECT_EQ(Dec(uint64_t{UINT32_MAX} + 1), "4294967296");
    EXPECT_EQ(Dec(uint64_t{UINT32_MAX} * 10 + 9), "42949672959");
    EXPECT_EQ(Dec(UINT64_MAX), "18446744073709551615");
}

TEST(Fmt, SignedDecimalHasAMinusAndSurvivesTheMinimum)
{
    EXPECT_EQ(Signed(0), "0");
    EXPECT_EQ(Signed(-1), "-1");
    EXPECT_EQ(Signed(42), "42");
    EXPECT_EQ(Signed(INT64_MAX), "9223372036854775807");
    EXPECT_EQ(Signed(INT64_MIN), "-9223372036854775808");
}

TEST(Fmt, HexIsLowerCaseUnlessAskedAndHasNoPrefix)
{
    EXPECT_EQ(Hex(0), "0");
    EXPECT_EQ(Hex(0xbeef), "beef");
    EXPECT_EQ(Hex(0xbeef, EL_FMT_UPPER), "BEEF");
    EXPECT_EQ(Hex(UINT64_MAX), "ffffffffffffffff");
    EXPECT_EQ(Hex(0x8000000000000000u, EL_FMT_UPPER), "8000000000000000");
}

TEST(Fmt, TheLongestOutputsFitTheConstant)
{
    EXPECT_EQ(Dec(UINT64_MAX).size(), size_t{EL_FMT_INT_LEN_MAX});
    EXPECT_EQ(Signed(INT64_MIN).size(), size_t{EL_FMT_INT_LEN_MAX});
}

TEST(Fmt, ACapacityAboveTheMinimumIsFine)
{
    char out[64];
    EXPECT_EQ(el_fmt_dec_u64(out, sizeof(out), 7), 1u);
    EXPECT_EQ(out[0], '7');
}

} // namespace
