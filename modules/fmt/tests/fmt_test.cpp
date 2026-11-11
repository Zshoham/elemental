/*
 * The core calls of <elemental/fmt.h>, against literal expectations, so the
 * same tests can run on a target whose libc printf differs from glibc's.
 * The truncation rules are those of docs/adr/0011-formatting-truncation.md.
 */
#include <gtest/gtest.h>

#include <elemental/fmt.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <string>

namespace
{

/* A buffer with room for `cap` bytes, filled with 'x' first so that a
 * missing terminator or a stray write shows. */
class Formatter
{
  public:
    explicit Formatter(size_t cap) : cap_(cap)
    {
        storage_.fill('x');
        el_fmt_init(&f, storage_.data(), cap_);
    }

    std::string Text() const
    {
        return std::string(storage_.data(), el_fmt_len(&f));
    }

    /* The invariant every call keeps: a terminator right after the text, and
     * nothing written past the capacity. */
    void ExpectTerminatedWithin() const
    {
        if (cap_ > 0) {
            EXPECT_LT(el_fmt_len(&f), cap_);
            EXPECT_EQ(storage_[el_fmt_len(&f)], '\0');
        }
        for (size_t i = cap_; i < storage_.size(); i++) {
            ASSERT_EQ(storage_[i], 'x') << "byte " << i << " is past the capacity";
        }
    }

    el_fmt f;

  private:
    size_t cap_;
    std::array<char, 80> storage_; /* small: also runs on a 4 KiB target stack */
};

std::string Dec(uint64_t value, size_t width = 0, uint32_t flags = 0)
{
    Formatter out(64);
    EXPECT_EQ(el_fmt_dec_u64(&out.f, value, width, flags), EL_OK);
    out.ExpectTerminatedWithin();
    return out.Text();
}

std::string Signed(int64_t value, size_t width = 0, uint32_t flags = 0)
{
    Formatter out(64);
    EXPECT_EQ(el_fmt_dec_i64(&out.f, value, width, flags), EL_OK);
    out.ExpectTerminatedWithin();
    return out.Text();
}

std::string Hex(uint64_t value, size_t width = 0, uint32_t flags = 0)
{
    Formatter out(64);
    EXPECT_EQ(el_fmt_hex_u64(&out.f, value, width, flags), EL_OK);
    out.ExpectTerminatedWithin();
    return out.Text();
}

/* ---- The cursor ---------------------------------------------------------- */

TEST(Fmt, InitStartsEmptyAndTerminated)
{
    Formatter out(8);
    EXPECT_EQ(el_fmt_len(&out.f), 0u);
    out.ExpectTerminatedWithin();
}

TEST(Fmt, ZeroCapacityWritesNothingAndFitsOnlyEmptyItems)
{
    el_fmt f;
    el_fmt_init(&f, nullptr, 0);
    EXPECT_EQ(el_fmt_str(&f, nullptr, 0), EL_OK);
    EXPECT_EQ(el_fmt_hex_bytes(&f, nullptr, 0), EL_OK);
    EXPECT_EQ(el_fmt_str(&f, "a", 1), EL_ERR_NO_SPACE);
    EXPECT_EQ(el_fmt_len(&f), 0u);

    el_fmt_init(&f, nullptr, 0);
    EXPECT_EQ(el_fmt_dec_u64(&f, 0, 0, 0), EL_ERR_NO_SPACE);
    EXPECT_EQ(el_fmt_len(&f), 0u);
}

TEST(Fmt, CapacityOneHoldsOnlyTheTerminator)
{
    Formatter out(1);
    EXPECT_EQ(el_fmt_cstr(&out.f, "a"), EL_ERR_NO_SPACE);
    EXPECT_EQ(out.Text(), "");
    out.ExpectTerminatedWithin();
}

TEST(Fmt, InitAgainStartsOverAndClearsTruncation)
{
    Formatter out(4);
    EXPECT_EQ(el_fmt_cstr(&out.f, "too long"), EL_ERR_NO_SPACE);
    char buf[8];
    el_fmt_init(&out.f, buf, sizeof buf);
    EXPECT_EQ(el_fmt_cstr(&out.f, "fits"), EL_OK);
    EXPECT_EQ(std::string(buf), "fits");
}

/* ---- Text ---------------------------------------------------------------- */

TEST(Fmt, TextIsAppendedAsGiven)
{
    Formatter out(32);
    EXPECT_EQ(el_fmt_str(&out.f, "abc", 3), EL_OK);
    EXPECT_EQ(el_fmt_cstr(&out.f, "def"), EL_OK);
    EXPECT_EQ(el_fmt_str(&out.f, "a\0b", 3), EL_OK); /* the NUL is a byte like any other */
    EXPECT_EQ(out.Text(), std::string("abcdefa\0b", 9));
    out.ExpectTerminatedWithin();
}

TEST(Fmt, TextThatFitsExactlyLeavesRoomOnlyForTheTerminator)
{
    Formatter out(6);
    EXPECT_EQ(el_fmt_cstr(&out.f, "hello"), EL_OK);
    EXPECT_EQ(out.Text(), "hello");
    EXPECT_EQ(el_fmt_cstr(&out.f, "!"), EL_ERR_NO_SPACE);
    EXPECT_EQ(out.Text(), "hello");
    out.ExpectTerminatedWithin();
}

TEST(Fmt, TextIsCutAtTheLastByteThatFits)
{
    Formatter str(6);
    EXPECT_EQ(el_fmt_str(&str.f, "hello world", 11), EL_ERR_NO_SPACE);
    EXPECT_EQ(str.Text(), "hello");
    str.ExpectTerminatedWithin();

    Formatter cstr(6);
    EXPECT_EQ(el_fmt_cstr(&cstr.f, "ab"), EL_OK);
    EXPECT_EQ(el_fmt_cstr(&cstr.f, "cdefg"), EL_ERR_NO_SPACE);
    EXPECT_EQ(cstr.Text(), "abcde");
    cstr.ExpectTerminatedWithin();
}

TEST(Fmt, EmptyItemsSucceedAndChangeNothing)
{
    Formatter out(4);
    EXPECT_EQ(el_fmt_cstr(&out.f, "abc"), EL_OK);
    EXPECT_EQ(el_fmt_str(&out.f, nullptr, 0), EL_OK);
    EXPECT_EQ(el_fmt_cstr(&out.f, ""), EL_OK);
    EXPECT_EQ(el_fmt_hex_bytes(&out.f, nullptr, 0), EL_OK);
    EXPECT_EQ(out.Text(), "abc");
}

/* ---- Numbers ------------------------------------------------------------- */

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

TEST(Fmt, WidthPadsWithSpacesBeforeOrZerosAfterTheSign)
{
    EXPECT_EQ(Dec(42, 5), "   42");
    EXPECT_EQ(Dec(42, 5, EL_FMT_ZERO_PAD), "00042");
    EXPECT_EQ(Dec(12345, 3), "12345"); /* a width is a minimum */
    EXPECT_EQ(Dec(7, 1, EL_FMT_ZERO_PAD), "7");
    EXPECT_EQ(Signed(-42, 5), "  -42");
    EXPECT_EQ(Signed(-42, 5, EL_FMT_ZERO_PAD), "-0042");
    EXPECT_EQ(Signed(-42, 2, EL_FMT_ZERO_PAD), "-42");
    EXPECT_EQ(Hex(0xab, 4), "  ab");
    EXPECT_EQ(Hex(0xab, 8, EL_FMT_ZERO_PAD), "000000ab");
}

TEST(Fmt, HexIsLowerCaseUnlessAskedAndHasNoPrefix)
{
    EXPECT_EQ(Hex(0), "0");
    EXPECT_EQ(Hex(0xdeadbeef), "deadbeef");
    EXPECT_EQ(Hex(0xdeadbeef, 0, EL_FMT_UPPER), "DEADBEEF");
    EXPECT_EQ(Hex(UINT64_MAX), "ffffffffffffffff");
    EXPECT_EQ(Hex(0x0123456789abcdefu, 0, EL_FMT_UPPER | EL_FMT_ZERO_PAD), "123456789ABCDEF");
}

TEST(Fmt, ANumberIsWholeOrAbsent)
{
    Formatter out(6); /* room for 5 */
    EXPECT_EQ(el_fmt_cstr(&out.f, "ab"), EL_OK);
    EXPECT_EQ(el_fmt_dec_u64(&out.f, 12345, 0, 0), EL_ERR_NO_SPACE);
    EXPECT_EQ(out.Text(), "ab");
    out.ExpectTerminatedWithin();

    Formatter negative(4); /* "-1" fits, "-100" does not */
    EXPECT_EQ(el_fmt_dec_i64(&negative.f, -100, 0, 0), EL_ERR_NO_SPACE);
    EXPECT_EQ(negative.Text(), "");

    Formatter padded(8); /* the digits fit; the padding does not */
    EXPECT_EQ(el_fmt_hex_u64(&padded.f, 0xab, 8, EL_FMT_ZERO_PAD), EL_ERR_NO_SPACE);
    EXPECT_EQ(padded.Text(), "");
}

TEST(Fmt, ANumberThatFitsExactlyIsWritten)
{
    Formatter out(6);
    EXPECT_EQ(el_fmt_dec_i64(&out.f, -1234, 0, 0), EL_OK);
    EXPECT_EQ(out.Text(), "-1234");
    out.ExpectTerminatedWithin();
}

/* ---- Hex dumps ----------------------------------------------------------- */

TEST(Fmt, HexBytesAreSeparatedBySingleSpaces)
{
    const uint8_t bytes[] = {0xde, 0xad, 0x0b, 0xef};
    Formatter out(32);
    EXPECT_EQ(el_fmt_cstr(&out.f, "x="), EL_OK);
    EXPECT_EQ(el_fmt_hex_bytes(&out.f, bytes, sizeof bytes), EL_OK);
    EXPECT_EQ(el_fmt_hex_bytes(&out.f, bytes, 1), EL_OK);
    EXPECT_EQ(out.Text(), "x=de ad 0b efde");
    out.ExpectTerminatedWithin();
}

TEST(Fmt, AHexDumpIsCutAfterTheLastWholeByte)
{
    const uint8_t bytes[] = {0xde, 0xad, 0xbe, 0xef};
    Formatter three(9); /* room for 8: "de ad be" */
    EXPECT_EQ(el_fmt_hex_bytes(&three.f, bytes, sizeof bytes), EL_ERR_NO_SPACE);
    EXPECT_EQ(three.Text(), "de ad be");
    three.ExpectTerminatedWithin();

    Formatter one(5); /* room for 4: "de" and not a dangling " a" */
    EXPECT_EQ(el_fmt_hex_bytes(&one.f, bytes, sizeof bytes), EL_ERR_NO_SPACE);
    EXPECT_EQ(one.Text(), "de");

    Formatter none(2); /* room for 1: not even half a byte */
    EXPECT_EQ(el_fmt_hex_bytes(&none.f, bytes, sizeof bytes), EL_ERR_NO_SPACE);
    EXPECT_EQ(none.Text(), "");
}

/* ---- Sticky truncation --------------------------------------------------- */

TEST(Fmt, AfterTheFirstCutNothingMoreIsWritten)
{
    const uint8_t byte = 0x01;
    Formatter out(8); /* room for 7 */
    EXPECT_EQ(el_fmt_cstr(&out.f, "abc"), EL_OK);
    EXPECT_EQ(el_fmt_dec_u64(&out.f, 123456, 0, 0), EL_ERR_NO_SPACE);
    /* Each of these would fit on its own. */
    EXPECT_EQ(el_fmt_cstr(&out.f, "x"), EL_ERR_NO_SPACE);
    EXPECT_EQ(el_fmt_dec_u64(&out.f, 1, 0, 0), EL_ERR_NO_SPACE);
    EXPECT_EQ(el_fmt_hex_u64(&out.f, 1, 0, 0), EL_ERR_NO_SPACE);
    EXPECT_EQ(el_fmt_hex_bytes(&out.f, &byte, 1), EL_ERR_NO_SPACE);
    /* Even an empty item reports the earlier cut. */
    EXPECT_EQ(el_fmt_str(&out.f, nullptr, 0), EL_ERR_NO_SPACE);
    EXPECT_EQ(el_fmt_hex_bytes(&out.f, nullptr, 0), EL_ERR_NO_SPACE);
    EXPECT_EQ(out.Text(), "abc");
    out.ExpectTerminatedWithin();
}

TEST(Fmt, AfterACutTextNothingMoreIsWritten)
{
    Formatter out(4);
    EXPECT_EQ(el_fmt_cstr(&out.f, "abcdef"), EL_ERR_NO_SPACE);
    EXPECT_EQ(el_fmt_str(&out.f, "", 0), EL_ERR_NO_SPACE);
    EXPECT_EQ(out.Text(), "abc");
}

/* ---- The proposal's examples --------------------------------------------- */

TEST(Fmt, AHexDumpLineThroughASmallBufferKeepsWholeBytes)
{
    /* docs/proposals/fmt.md, example 2, with a 24-byte buffer. */
    const uint8_t frame[16] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
    Formatter out(24);
    EXPECT_EQ(el_fmt_hex_u64(&out.f, 0, 4, EL_FMT_ZERO_PAD), EL_OK);
    EXPECT_EQ(el_fmt_cstr(&out.f, ": "), EL_OK);
    EXPECT_EQ(el_fmt_hex_bytes(&out.f, frame, sizeof frame), EL_ERR_NO_SPACE);
    EXPECT_EQ(el_fmt_cstr(&out.f, "\n"), EL_ERR_NO_SPACE);
    EXPECT_EQ(out.Text(), "0000: 01 02 03 04 05 06");
}

TEST(Fmt, HoldingOneByteBackKeepsTheNewlineOfACutLine)
{
    /* docs/proposals/fmt.md, example 1: give el_fmt_init() one byte less,
     * then put the '\n' where the terminator is. */
    char line[12];
    el_fmt f;
    el_fmt_init(&f, line, sizeof line - 1);
    EXPECT_EQ(el_fmt_cstr(&f, "a long message"), EL_ERR_NO_SPACE);
    size_t len = el_fmt_len(&f);
    line[len++] = '\n';
    EXPECT_EQ(std::string(line, len), "a long mes\n");
}

} // namespace
