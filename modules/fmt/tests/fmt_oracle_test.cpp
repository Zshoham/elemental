/*
 * Host-only tests that use the host's C library as the oracle.
 *
 * 1. For every format in the subset and a spread of values, the output of
 *    el_fmt_printf() equals glibc's snprintf() given enough space. With too
 *    little space it equals the prefix rule of
 *    docs/adr/0011-formatting-truncation.md applied to snprintf's pieces,
 *    for every capacity from 0 to one past the full length.
 * 2. Decimal conversion, which divides by 10 with shifts and adds, equals
 *    std::to_string(), which uses the compiler's division.
 *
 * These cannot run on a target: they need glibc's printf, and a target's
 * libc printf may differ (newlib-nano drops %ll unless configured).
 */
#include <gtest/gtest.h>

#include <elemental/fmt.h>
#include <elemental/fmt_printf.h>

#include <climits>
#include <cstdint>
#include <cstdio>
#include <random>
#include <string>
#include <vector>

/* The formats here are data, built at run time, so the compiler cannot
 * check them; the test checks them against snprintf instead. */
#pragma GCC diagnostic ignored "-Wformat-nonliteral"

namespace
{

struct Piece {
    std::string text;
    bool number;
};

/* What the buffer must hold with `cap` bytes, by the prefix rule. */
std::string PrefixRule(const std::vector<Piece> &pieces, size_t cap)
{
    const size_t room = cap == 0 ? 0 : cap - 1;
    std::string out;
    for (const Piece &piece : pieces) {
        const size_t left = room - out.size();
        if (piece.text.size() <= left) {
            out += piece.text;
            continue;
        }
        if (!piece.number) {
            out += piece.text.substr(0, left);
        }
        break; /* sticky: nothing after the first cut */
    }
    return out;
}

template <typename T> void CheckAgainstSnprintf(const char *conversion, bool number, T value)
{
    SCOPED_TRACE(std::string("conversion ") + conversion);
    const std::string format = std::string("<") + conversion + ">";

    char expected[512];
    const int n = std::snprintf(expected, sizeof expected, format.c_str(), value);
    ASSERT_GE(n, 0);
    ASSERT_LT(static_cast<size_t>(n), sizeof expected);
    const std::string full(expected, static_cast<size_t>(n));

    char item[512];
    const int m = std::snprintf(item, sizeof item, conversion, value);
    ASSERT_GE(m, 0);
    const std::vector<Piece> pieces = {
        {"<", false}, {std::string(item, static_cast<size_t>(m)), number}, {">", false}};

    for (size_t cap = 0; cap <= full.size() + 2; cap++) {
        char buf[512];
        el_fmt f;
        el_fmt_init(&f, buf, cap);
        const el_status status = el_fmt_printf(&f, format.c_str(), value);
        const std::string got(buf, el_fmt_len(&f));
        EXPECT_EQ(got, PrefixRule(pieces, cap)) << "cap " << cap;
        EXPECT_EQ(status, cap > full.size() ? EL_OK : EL_ERR_NO_SPACE) << "cap " << cap;
        if (cap > full.size()) {
            EXPECT_EQ(got, full);
        }
    }
}

std::vector<long long> SignedValues()
{
    std::vector<long long> values = {
        0,   1,   -1,    9,     10,     -10,   42,    -42,     127,     128,       -128,     -129,
        255, 256, 32767, 32768, -32768, 65535, 65536, INT_MAX, INT_MIN, LLONG_MAX, LLONG_MIN};
    std::mt19937_64 random(1);
    for (int i = 0; i < 40; i++) {
        values.push_back(static_cast<long long>(random() >> (random() % 64)));
        values.push_back(-static_cast<long long>(random() >> (1 + random() % 63)));
    }
    return values;
}

TEST(FmtOracle, SignedConversionsMatchSnprintf)
{
    const char *const int_formats[] = {"%d", "%i", "%7d", "%07d", "%1d", "%hhd", "%hd", "%3hhd"};
    const char *const long_formats[] = {"%lld", "%22lld", "%022lld", "%ld", "%zd"};
    for (long long value : SignedValues()) {
        for (const char *format : int_formats) {
            CheckAgainstSnprintf(format, true, static_cast<int>(value));
        }
        for (const char *format : long_formats) {
            /* long and ptrdiff_t are long long's width on this host. */
            CheckAgainstSnprintf(format, true, value);
        }
    }
}

TEST(FmtOracle, UnsignedConversionsMatchSnprintf)
{
    const char *const int_formats[] = {"%u",   "%x",  "%X",   "%08x", "%5X",
                                       "%hhu", "%hu", "%hhx", "%04hx"};
    const char *const long_formats[] = {"%llu", "%llx", "%016llX", "%25llu",
                                        "%lu",  "%lx",  "%zu",     "%zx"};
    for (long long signed_value : SignedValues()) {
        const auto value = static_cast<unsigned long long>(signed_value);
        for (const char *format : int_formats) {
            CheckAgainstSnprintf(format, true, static_cast<unsigned int>(value));
        }
        for (const char *format : long_formats) {
            CheckAgainstSnprintf(format, true, value);
        }
    }
}

TEST(FmtOracle, StringAndCharacterConversionsMatchSnprintf)
{
    const char *const string_formats[] = {"%s", "%1s", "%9s", "%.0s", "%.3s", "%12.4s"};
    for (const char *value : {"", "a", "hello", "hello, world"}) {
        for (const char *format : string_formats) {
            CheckAgainstSnprintf(format, false, value);
        }
    }
    for (int value : {'a', 'Z', ' ', '%'}) {
        CheckAgainstSnprintf("%c", false, value);
        CheckAgainstSnprintf("%4c", false, value);
    }
}

TEST(FmtOracle, PercentAndLiteralTextMatchSnprintf)
{
    /* No argument: the conversion is the literal "%%" itself. */
    for (size_t cap = 0; cap <= 8; cap++) {
        char buf[16];
        el_fmt f;
        el_fmt_init(&f, buf, cap);
        el_fmt_printf(&f, "a%%b%%c");
        char expected[16];
        std::snprintf(expected, cap == 0 ? 1 : cap, "a%%b%%c");
        EXPECT_EQ(std::string(buf, el_fmt_len(&f)), cap == 0 ? "" : std::string(expected))
            << "cap " << cap;
    }
}

TEST(FmtOracle, DecimalMatchesTheCompilersDivision)
{
    /* Every value the 32-bit loop, the 64-bit loop, and the hand-over
     * between them can see. */
    std::mt19937_64 random(20261111);
    char buf[32];
    el_fmt f;
    for (int i = 0; i < 2000000; i++) {
        const uint64_t value = random() >> (random() % 64);
        el_fmt_init(&f, buf, sizeof buf);
        ASSERT_EQ(el_fmt_dec_u64(&f, value, 0, 0), EL_OK);
        ASSERT_EQ(std::string(buf, el_fmt_len(&f)), std::to_string(value));

        const auto signed_value = static_cast<int64_t>(random());
        el_fmt_init(&f, buf, sizeof buf);
        ASSERT_EQ(el_fmt_dec_i64(&f, signed_value, 0, 0), EL_OK);
        ASSERT_EQ(std::string(buf, el_fmt_len(&f)), std::to_string(signed_value));
    }
    for (int shift = 0; shift < 64; shift++) {
        for (int delta = -3; delta <= 3; delta++) {
            const uint64_t value = (uint64_t{1} << shift) + static_cast<uint64_t>(delta);
            el_fmt_init(&f, buf, sizeof buf);
            ASSERT_EQ(el_fmt_dec_u64(&f, value, 0, 0), EL_OK);
            ASSERT_EQ(std::string(buf, el_fmt_len(&f)), std::to_string(value));
        }
    }
}

} // namespace
