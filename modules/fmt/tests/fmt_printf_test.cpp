/*
 * The printf front end, against literal expectations. Every accepted part of
 * the subset is used at least once; fmt_oracle_test.cpp compares the same
 * subset with glibc's snprintf on the host.
 */
#include <gtest/gtest.h>

#include <elemental/fmt.h>
#include <elemental/fmt_printf.h>

#include <cinttypes>
#include <climits>
#include <cstdarg>
#include <cstdint>
#include <string>

namespace
{

/* Formats into a fresh buffer of `cap` bytes and returns the text; the
 * status goes to `status`. */
std::string Format(size_t cap, el_status *status, const char *format, ...)
    EL_FMT_PRINTF_CHECK(3, 4);

std::string Format(size_t cap, el_status *status, const char *format, ...)
{
    char buf[256];
    el_fmt f;
    el_fmt_init(&f, buf, cap);
    va_list args;
    va_start(args, format);
    *status = el_fmt_vprintf(&f, format, args);
    va_end(args);
    if (cap > 0) {
        EXPECT_EQ(buf[el_fmt_len(&f)], '\0');
    }
    return std::string(buf, el_fmt_len(&f));
}

/* An empty format is valid, and the tests use one; GCC warns about it
 * (-Wformat-zero-length), so it is formatted through this one place. */
el_status EmptyFormat(el_fmt *f)
{
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-zero-length"
#endif
    return el_fmt_printf(f, "");
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif
}

#define EXPECT_FORMAT(expected, ...)                                                               \
    do {                                                                                           \
        el_status status_;                                                                         \
        EXPECT_EQ(Format(256, &status_, __VA_ARGS__), expected);                                   \
        EXPECT_EQ(status_, EL_OK);                                                                 \
    } while (0)

TEST(FmtPrintf, EveryConversionInTheSubset)
{
    EXPECT_FORMAT("-7 -7 7 ff FF x hi %", "%d %i %u %x %X %c %s %%", -7, -7, 7u, 255u, 255u, 'x',
                  "hi");
    EXPECT_FORMAT("", "%s", "");
    EXPECT_FORMAT("plain text", "plain text");

    char buf[4];
    el_fmt f;
    el_fmt_init(&f, buf, sizeof buf);
    EXPECT_EQ(EmptyFormat(&f), EL_OK);
    EXPECT_EQ(el_fmt_len(&f), 0u);
    EXPECT_EQ(buf[0], '\0');
}

TEST(FmtPrintf, FixedWidthTypesThroughInttypes)
{
    EXPECT_FORMAT("18446744073709551615", "%" PRIu64, UINT64_MAX);
    EXPECT_FORMAT("-9223372036854775808", "%" PRId64, INT64_MIN);
    EXPECT_FORMAT("ffffffffffffffff", "%" PRIx64, UINT64_MAX);
    EXPECT_FORMAT("4294967295 -2147483648", "%" PRIu32 " %" PRId32, UINT32_MAX, INT32_MIN);
    EXPECT_FORMAT("255 65535", "%" PRIu8 " %" PRIu16, uint8_t{255}, uint16_t{65535});
    EXPECT_FORMAT("-128 -32768", "%" PRId8 " %" PRId16, int8_t{-128}, int16_t{-32768});
}

TEST(FmtPrintf, LengthModifiersTruncateLikePrintf)
{
    EXPECT_FORMAT("44 -56", "%hhu %hhd", 300, 200);
    EXPECT_FORMAT("4464 -1", "%hu %hd", 70000, 65535);
    EXPECT_FORMAT("-1 18446744073709551615", "%lld %llu", -1LL, ULLONG_MAX);
    EXPECT_FORMAT("-1 4294967295", "%ld %lu", -1L, 4294967295UL);
    EXPECT_FORMAT("123 -5", "%zu %zd", size_t{123}, ptrdiff_t{-5});
}

TEST(FmtPrintf, WidthAndZeroFlag)
{
    EXPECT_FORMAT("   42|00042|-0042|  -42", "%5d|%05d|%05d|%5d", 42, 42, -42, -42);
    EXPECT_FORMAT("0x0000beef", "0x%08x", 0xbeefu);
    EXPECT_FORMAT("123456", "%3d", 123456);
    EXPECT_FORMAT("42", "%0d", 42); /* the flag without a width */
    EXPECT_FORMAT("   ab|    x", "%5s|%5c", "ab", 'x');
}

TEST(FmtPrintf, PrecisionLimitsAString)
{
    EXPECT_FORMAT("abc", "%.3s", "abcdef");
    EXPECT_FORMAT("ab", "%.*s", 2, "abcdef");
    EXPECT_FORMAT("", "%.0s", "abc");
    EXPECT_FORMAT("abcdef", "%.*s", -1, "abcdef"); /* negative: as if omitted */
    EXPECT_FORMAT("   ab", "%5.2s", "abcdef");
    /* With a precision the string need not be terminated. */
    const char unterminated[3] = {'x', 'y', 'z'};
    EXPECT_FORMAT("xyz", "%.*s", 3, unterminated);
}

TEST(FmtPrintf, ACharacterMayBeNul)
{
    EXPECT_FORMAT(std::string("a\0b", 3), "a%cb", 0);
}

TEST(FmtPrintf, AppendsAfterTheCoreCalls)
{
    char buf[32];
    el_fmt f;
    el_fmt_init(&f, buf, sizeof buf);
    EXPECT_EQ(el_fmt_cstr(&f, "n="), EL_OK);
    EXPECT_EQ(el_fmt_printf(&f, "%d", 5), EL_OK);
    EXPECT_EQ(el_fmt_cstr(&f, ";"), EL_OK);
    EXPECT_EQ(std::string(buf), "n=5;");
}

/* ---- Truncation (docs/adr/0011-formatting-truncation.md) ------------------ */

TEST(FmtPrintf, ANumberIsWholeOrAbsentAndNothingFollows)
{
    el_status status;
    /* Room for 7: "abc" fits, 12345 does not, and "xyz" is not written
     * even though it would fit. */
    EXPECT_EQ(Format(8, &status, "abc%dxyz", 12345), "abc");
    EXPECT_EQ(status, EL_ERR_NO_SPACE);
    EXPECT_EQ(Format(8, &status, "abc%5dz", 1), "abc");
    EXPECT_EQ(status, EL_ERR_NO_SPACE);
}

TEST(FmtPrintf, TextAndPaddedStringsAreCutAtTheLastByte)
{
    el_status status;
    EXPECT_EQ(Format(6, &status, "hello world"), "hello");
    EXPECT_EQ(status, EL_ERR_NO_SPACE);
    EXPECT_EQ(Format(6, &status, "%8s", "ab"), "     ");
    EXPECT_EQ(status, EL_ERR_NO_SPACE);
    EXPECT_EQ(Format(6, &status, "%s!", "abcdefgh"), "abcde");
    EXPECT_EQ(status, EL_ERR_NO_SPACE);
    EXPECT_EQ(Format(4, &status, "ab%3c", 'z'), "ab ");
    EXPECT_EQ(status, EL_ERR_NO_SPACE);
}

TEST(FmtPrintf, TruncationIsStickyAcrossCalls)
{
    char buf[6];
    el_fmt f;
    el_fmt_init(&f, buf, sizeof buf);
    EXPECT_EQ(el_fmt_printf(&f, "%d", 123456), EL_ERR_NO_SPACE);
    EXPECT_EQ(el_fmt_printf(&f, "ok"), EL_ERR_NO_SPACE);
    EXPECT_EQ(EmptyFormat(&f), EL_ERR_NO_SPACE);
    EXPECT_EQ(el_fmt_len(&f), 0u);
}

TEST(FmtPrintf, ZeroCapacity)
{
    el_fmt f;
    el_fmt_init(&f, nullptr, 0);
    EXPECT_EQ(EmptyFormat(&f), EL_OK);
    EXPECT_EQ(el_fmt_printf(&f, "%s", ""), EL_OK);
    EXPECT_EQ(el_fmt_len(&f), 0u);

    el_status status;
    EXPECT_EQ(Format(0, &status, "%d", 0), "");
    EXPECT_EQ(status, EL_ERR_NO_SPACE);
}

TEST(FmtPrintf, ALogLineWithAMessageThatComesWithALength)
{
    /* docs/proposals/fmt.md, example 1, without the writer. */
    char line[96];
    el_fmt f;
    el_fmt_init(&f, line, sizeof line - 1);
    EXPECT_EQ(el_fmt_printf(&f, "%10" PRIu64 " %s: ", uint64_t{1234}, "warn"), EL_OK);
    const std::string message(100, 'm');
    EXPECT_EQ(el_fmt_str(&f, message.data(), message.size()), EL_ERR_NO_SPACE);
    size_t len = el_fmt_len(&f);
    line[len++] = '\n';
    EXPECT_EQ(std::string(line, len), "      1234 warn: " + std::string(77, 'm') + "\n");
}

} // namespace
