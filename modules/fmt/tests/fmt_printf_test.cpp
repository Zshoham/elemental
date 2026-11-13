/*
 * printf into an el_buf, against literal expectations. Every accepted part
 * of the portable subset is used at least once; fmt_oracle_test.cpp compares
 * the same subset with glibc's snprintf on the host. These run in every
 * printf configuration, so the output of the portable subset is checked
 * identical in each.
 */
#include <gtest/gtest.h>

#include <elemental/fmt_printf.h>

#include <cinttypes>
#include <climits>
#include <cstdarg>
#include <cstdint>
#include <string>

namespace
{

std::string Text(const el_buf *b)
{
    const uint8_t *const data = el_buf_data(b);
    return data == nullptr ? std::string()
                           : std::string(reinterpret_cast<const char *>(data), el_buf_len(b));
}

/* Formats into a fresh fixed buffer of `cap` bytes and returns the text; the
 * status goes to `status`. */
std::string Format(size_t cap, el_status *status, const char *format, ...)
    EL_FMT_PRINTF_CHECK(3, 4);

std::string Format(size_t cap, el_status *status, const char *format, ...)
{
    uint8_t storage[64]; /* small: the tests also run on a 4 KiB target stack */
    el_buf b;
    el_buf_init(&b, cap == 0 ? nullptr : storage, cap);
    va_list args;
    va_start(args, format);
    *status = el_fmt_vprintf(&b, format, args);
    va_end(args);
    return Text(&b);
}

/* An empty format is valid, and the tests use one; GCC warns about it
 * (-Wformat-zero-length), so it is formatted through this one place. */
el_status EmptyFormat(el_buf *b)
{
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-zero-length"
#endif
    return el_fmt_printf(b, "");
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif
}

#define EXPECT_FORMAT(expected, ...)                            \
    do {                                                        \
        el_status status_;                                      \
        EXPECT_EQ(Format(64, &status_, __VA_ARGS__), expected); \
        EXPECT_EQ(status_, EL_OK);                              \
    } while (0)

TEST(FmtPrintf, EveryConversionInTheSubset)
{
    EXPECT_FORMAT("-7 -7 7 ff FF x hi %", "%d %i %u %x %X %c %s %%", -7, -7, 7u, 255u, 255u, 'x',
                  "hi");
    EXPECT_FORMAT("", "%s", "");
    EXPECT_FORMAT("plain text", "plain text");

    uint8_t storage[4];
    el_buf b;
    el_buf_init(&b, storage, sizeof storage);
    EXPECT_EQ(EmptyFormat(&b), EL_OK);
    EXPECT_EQ(el_buf_len(&b), 0u);
}

TEST(FmtPrintf, FixedWidthTypesThroughInttypes)
{
    EXPECT_FORMAT("18446744073709551615", "%" PRIu64, UINT64_MAX);
    EXPECT_FORMAT("-9223372036854775808", "%" PRId64, INT64_MIN);
    EXPECT_FORMAT("ffffffffffffffff", "%" PRIx64, UINT64_MAX);
    /* uint32_t{UINT32_MAX}, not UINT32_MAX: picolibc defines the macro as an
     * unsigned long while uint32_t is unsigned int, so PRIu32 would not
     * match the macro's type there. */
    EXPECT_FORMAT("4294967295 -2147483648", "%" PRIu32 " %" PRId32, uint32_t{UINT32_MAX},
                  int32_t{INT32_MIN});
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
    EXPECT_FORMAT("", "%.s", "abc");               /* a bare '.' is a precision of 0 */
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

TEST(FmtPrintf, AppendsAfterWhatTheBufferHolds)
{
    uint8_t storage[32];
    el_buf b;
    el_buf_init(&b, storage, sizeof storage);
    ASSERT_EQ(el_buf_append(&b, "n=", 2), EL_OK);
    EXPECT_EQ(el_fmt_printf(&b, "%d", 5), EL_OK);
    ASSERT_EQ(el_buf_append(&b, ";", 1), EL_OK);
    EXPECT_EQ(Text(&b), "n=5;");
}

/* ---- The item rule (docs/adr/0015-formatting-into-io-buffers.md) ----------- */

TEST(FmtPrintf, ANumberIsWholeOrAbsentAndNothingFollows)
{
    el_status status;
    /* Room for 7: "abc" fits, 12345 does not, and "xyz" is not written
     * even though it would fit. */
    EXPECT_EQ(Format(7, &status, "abc%dxyz", 12345), "abc");
    EXPECT_EQ(status, EL_ERR_NO_SPACE);
    EXPECT_EQ(Format(7, &status, "abc%5dz", 1), "abc");
    EXPECT_EQ(status, EL_ERR_NO_SPACE);
    /* Exactly enough: no byte is held back for a terminator. */
    EXPECT_EQ(Format(8, &status, "abc%d", 12345), "abc12345");
    EXPECT_EQ(status, EL_OK);
}

TEST(FmtPrintf, TextAndPaddedStringsAreCutAtTheLastByte)
{
    el_status status;
    EXPECT_EQ(Format(5, &status, "hello world"), "hello");
    EXPECT_EQ(status, EL_ERR_NO_SPACE);
    EXPECT_EQ(Format(5, &status, "%8s", "ab"), "     ");
    EXPECT_EQ(status, EL_ERR_NO_SPACE);
    EXPECT_EQ(Format(5, &status, "%s!", "abcdefgh"), "abcde");
    EXPECT_EQ(status, EL_ERR_NO_SPACE);
    EXPECT_EQ(Format(3, &status, "ab%3c", 'z'), "ab ");
    EXPECT_EQ(status, EL_ERR_NO_SPACE);
}

TEST(FmtPrintf, TruncationDoesNotCarryOverToTheNextCall)
{
    uint8_t storage[5];
    el_buf b;
    el_buf_init(&b, storage, sizeof storage);
    EXPECT_EQ(el_fmt_printf(&b, "%d", 123456), EL_ERR_NO_SPACE);
    EXPECT_EQ(el_buf_len(&b), 0u);
    /* A later call that fits succeeds: callers that compose check each. */
    EXPECT_EQ(el_fmt_printf(&b, "ok"), EL_OK);
    EXPECT_EQ(Text(&b), "ok");
}

TEST(FmtPrintf, ZeroCapacity)
{
    el_buf b;
    el_buf_init(&b, nullptr, 0);
    EXPECT_EQ(EmptyFormat(&b), EL_OK);
    EXPECT_EQ(el_fmt_printf(&b, "%s", ""), EL_OK);
    EXPECT_EQ(el_buf_len(&b), 0u);

    el_status status;
    EXPECT_EQ(Format(0, &status, "%d", 0), "");
    EXPECT_EQ(status, EL_ERR_NO_SPACE);
    EXPECT_EQ(Format(0, &status, "x"), "");
    EXPECT_EQ(status, EL_ERR_NO_SPACE);
}

/* docs/proposals/io-buffers-fmt.md, Example 1: a log line formatted once
 * into a fixed buffer, its last byte replaced by the newline when full. */
TEST(FmtPrintf, ALogLineThatIsTooLongEndsInANewline)
{
    uint8_t storage[96];
    el_buf line;
    el_buf_init(&line, storage, sizeof storage);
    const std::string message(100, 'm');
    unsigned truncated = 0;

    el_status st = el_fmt_printf(&line, "%10" PRIu64 " %s: ", uint64_t{1234}, "warn");
    if (st == EL_OK) {
        st = el_fmt_printf(&line, "%.*s", static_cast<int>(message.size()), message.data());
    }
    if (st != EL_OK) {
        truncated++;
    }
    if (el_buf_append(&line, "\n", 1) != EL_OK) {
        el_buf_data_mut(&line)[el_buf_len(&line) - 1] = '\n';
    }
    EXPECT_EQ(truncated, 1u);
    EXPECT_EQ(Text(&line), "      1234 warn: " + std::string(78, 'm') + "\n");
}

} // namespace
