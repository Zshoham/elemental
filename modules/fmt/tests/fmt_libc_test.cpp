/*
 * FMT_PRINTF_LIBC: the specifications outside the portable subset, against
 * the platform's own snprintf() (glibc on the host, picolibc on Zephyr), by
 * the item rule at every capacity; '-' and '*' on %s and %c, which stay
 * Elemental's text code; and the same through a buffered writer. Built only
 * with FMT_PRINTF_LIBC. The portable subset is fmt.unit's, which runs in
 * this configuration too.
 *
 * Float cases need a C library that formats floats: on Zephyr they run only
 * with CONFIG_PICOLIBC_IO_FLOAT.
 */
#include <gtest/gtest.h>

#include <elemental/fmt_printf.h>

#include <cinttypes>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>

#if !defined(__ZEPHYR__) || defined(CONFIG_PICOLIBC_IO_FLOAT)
#define FLOATS 1
#else
#define FLOATS 0
#endif

/* The formats here are data; the test checks them against snprintf. */
#pragma GCC diagnostic ignored "-Wformat-nonliteral"

namespace
{

/* What snprintf writes into before a conversion is copied: a conversion of
 * this many bytes or more needs one byte more than its length. */
constexpr size_t kScratch = 32;

std::string Text(const el_buf *b)
{
    const uint8_t *const data = el_buf_data(b);
    return data == nullptr ? std::string()
                           : std::string(reinterpret_cast<const char *>(data), el_buf_len(b));
}

/* "<" item ">" into a buffer of every capacity: the item whole or absent,
 * the text around it cut at the last byte. */
template <typename T> void CheckItem(const char *conversion, T value)
{
    SCOPED_TRACE(std::string("conversion ") + conversion);
    char item[160];
    const int m = std::snprintf(item, sizeof item, conversion, value);
    ASSERT_GE(m, 0);
    ASSERT_LT(static_cast<size_t>(m), sizeof item);
    const std::string number(item, static_cast<size_t>(m));
    const std::string format = std::string("<") + conversion + ">";
    const std::string full = "<" + number + ">";
    const size_t needed = number.size() >= kScratch ? number.size() + 1 : number.size();

    for (size_t cap = 0; cap <= full.size() + 2; cap++) {
        uint8_t storage[192];
        el_buf b;
        el_buf_init(&b, storage, cap);
        const el_status status = el_fmt_printf(&b, format.c_str(), value);

        std::string expected = full.substr(0, cap < 1 ? cap : 1);
        if (cap >= 1 && cap - 1 >= needed) {
            expected += number;
            expected += cap - 1 - number.size() >= 1 ? ">" : "";
        }
        EXPECT_EQ(Text(&b), expected) << "cap " << cap;
        EXPECT_EQ(status, expected == full ? EL_OK : EL_ERR_NO_SPACE) << "cap " << cap;
    }
}

TEST(FmtLibc, FlagsPrecisionAndSignsOnIntegers)
{
    for (int value : {0, 1, -1, 42, -42, INT_MAX, INT_MIN}) {
        for (const char *c : {"%+d", "%-6d", "% d", "%.3d", "%+08d", "%-+6i", "%.0d", "%#x", "%#X",
                              "%o", "%#o", "%#.5x"}) {
            CheckItem(c, value);
        }
    }
    CheckItem("%+lld", LLONG_MIN);
    CheckItem("%#llx", ULLONG_MAX);
    CheckItem("%jd", INTMAX_MIN);
    CheckItem("%ju", UINTMAX_MAX);
    CheckItem("%td", static_cast<ptrdiff_t>(-7));
    CheckItem("%+zd", static_cast<ptrdiff_t>(7));
    CheckItem("%-5hhu", 300);
}

TEST(FmtLibc, ConversionsOfThirtyTwoBytesOrMoreNeedOneMoreByte)
{
    CheckItem("%+31d", 1);
    CheckItem("%+32d", 1);
    CheckItem("%+33d", 1);
    CheckItem("%-40u", 7u);
    CheckItem("%+60lld", LLONG_MAX);
}

TEST(FmtLibc, Pointers)
{
    int object = 0;
    CheckItem("%p", static_cast<void *>(&object));
    CheckItem("%20p", static_cast<void *>(&object));
}

TEST(FmtLibc, Floats)
{
#if FLOATS
    for (double value : {0.0, 1.5, -2.25, 3.14159265358979, 1e30, -1e-30}) {
        for (const char *c : {"%f", "%.2f", "%e", "%E", "%g", "%G", "%10.3f", "%+.1e", "%a"}) {
            CheckItem(c, value);
        }
    }
    CheckItem("%Lf", 2.5L);
#else
    GTEST_SKIP() << "this C library formats no floats (CONFIG_PICOLIBC_IO_FLOAT is off)";
#endif
}

TEST(FmtLibc, StarWidthAndPrecisionAreReadInOrder)
{
    uint8_t storage[64];
    el_buf b;
    el_buf_init(&b, storage, sizeof storage);
    EXPECT_EQ(el_fmt_printf(&b, "[%*d|%-*d|%.*d|%*.*d|%*d]", 5, 42, 4, 7, 3, 9, 6, 2, -1, -4, 3),
              EL_OK);
    char expected[64];
    std::snprintf(expected, sizeof expected, "[%*d|%-*d|%.*d|%*.*d|%*d]", 5, 42, 4, 7, 3, 9, 6, 2,
                  -1, -4, 3);
    EXPECT_EQ(Text(&b), expected);
}

TEST(FmtLibc, LeftAlignedAndStarWidthTextIsElementalsOwn)
{
    uint8_t storage[64];
    el_buf b;
    el_buf_init(&b, storage, sizeof storage);
    EXPECT_EQ(el_fmt_printf(&b, "[%-5s|%*s|%*s|%-3c|%*c|%-*.*s]", "ab", 4, "cd", -4, "ef", 'g', 3,
                            'h', 6, 2, "ijkl"),
              EL_OK);
    EXPECT_EQ(Text(&b), "[ab   |  cd|ef  |g  |  h|ij    ]");

    /* Text, so cut at the last byte that fits, padding included. */
    el_buf_init(&b, storage, 5);
    EXPECT_EQ(el_fmt_printf(&b, "%-8s|", "abc"), EL_ERR_NO_SPACE);
    EXPECT_EQ(Text(&b), "abc  ");
}

TEST(FmtLibc, ThePortableSubsetIsUnchanged)
{
    uint8_t storage[64];
    el_buf b;
    el_buf_init(&b, storage, sizeof storage);
    EXPECT_EQ(el_fmt_printf(&b, "%05d|%x|%.2s|%3c", -42, 255u, "abc", 'z'), EL_OK);
    EXPECT_EQ(Text(&b), "-0042|ff|ab|  z");
}

/* A stream that takes 3 bytes per call and stalls after `limit`. */
class Stalling
{
  public:
    explicit Stalling(size_t limit) : limit_(limit)
    {
    }

    el_writer writer()
    {
        static const el_writer_ops ops = {Write};
        return el_writer{this, &ops};
    }

    std::string sent;

  private:
    static el_status Write(void *ctx, const void *data, size_t len, size_t *n)
    {
        Stalling *const self = static_cast<Stalling *>(ctx);
        size_t count = self->limit_ - self->sent.size();
        if (count == 0) {
            return EL_ERR_WOULD_BLOCK;
        }
        count = count < len ? count : len;
        count = count < 3 ? count : 3;
        self->sent.append(static_cast<const char *>(data), count);
        *n = count;
        return EL_OK;
    }

    size_t limit_;
};

TEST(FmtLibc, ThroughAStallingBufferedWriterAnExtendedNumberIsWholeOrAbsent)
{
    char full[64];
    const int m = std::snprintf(full, sizeof full, "a=%+d b=%#x c=%-4d.", 12345, 255u, 7);
    ASSERT_GT(m, 0);
    for (size_t k = 0; k <= static_cast<size_t>(m); k++) {
        Stalling stream(k);
        uint8_t storage[8];
        el_buf b;
        el_buf_init(&b, storage, sizeof storage);
        el_buffered_writer w;
        el_buffered_writer_init(&w, stream.writer(), &b);
        const el_status status = el_fmt_printf_buffered(&w, "a=%+d b=%#x c=%-4d.", 12345, 255u, 7);
        const size_t pending = el_buffered_writer_pending(&w);
        const std::string accepted =
            stream.sent +
            std::string(reinterpret_cast<const char *>(storage) + el_buf_len(&b) - pending,
                        pending);
        const std::string expected(full, static_cast<size_t>(m));
        EXPECT_EQ(expected.compare(0, accepted.size(), accepted), 0) << "k " << k;
        /* The prefix ends at an item boundary, or inside literal text. */
        static const size_t boundaries[] = {0, 1, 2, 8, 9, 10, 11, 15, 16, 17, 18, 22, 23};
        bool on_boundary = false;
        for (size_t at : boundaries) {
            on_boundary = on_boundary || accepted.size() == at;
        }
        EXPECT_TRUE(on_boundary) << "k " << k << ": " << accepted;
        EXPECT_EQ(status == EL_OK, accepted == expected) << "k " << k;
    }
}

} // namespace
