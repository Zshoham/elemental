/*
 * Every contract violation listed in <elemental/fmt.h> and
 * <elemental/fmt_printf.h> stops the program, in whichever way the
 * configuration makes a failed check stop it.
 */
#include <gtest/gtest.h>

#include <elemental/fmt.h>
#include <elemental/io_buf.h>
#include <elemental/io_buffered.h>
#include <elemental/fmt_printf.h>

#include <cstdarg>
#include <cstdint>

#include <csignal>
#include <sys/wait.h>

#if __has_include(<sys/resource.h>)
#include <sys/resource.h>
#define HAVE_RLIMIT 1
#endif
#if __has_include(<sys/prctl.h>)
#include <sys/prctl.h>
#define HAVE_PRCTL 1
#endif

namespace
{

/*
 * Stopped by the check, not by a crash: a failed check traps (SIGILL on
 * this host) or calls the test handler, which aborts (SIGABRT). Without the
 * check a null pointer would still die here, of SIGSEGV, or be reported by
 * AddressSanitizer, which exits; neither is accepted. On a Cortex-M without
 * memory protection the same null access would not fault at all, so the
 * check is what these tests are about.
 */
bool StoppedByACheck(int status)
{
    return WIFSIGNALED(status) && (WTERMSIG(status) == SIGILL || WTERMSIG(status) == SIGTRAP ||
                                   WTERMSIG(status) == SIGABRT);
}

#define EXPECT_VIOLATION(statement) EXPECT_EXIT(statement, StoppedByACheck, "")

/*
 * Formats outside the subset, passed where the compiler cannot see them:
 * many are valid printf with matching arguments, which is the point (the
 * format attribute does not catch them), and the rest would be diagnosed.
 */
el_status Unchecked(el_buf *b, const char *format, ...)
{
    va_list args;
    va_start(args, format);
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
    const el_status status = el_fmt_vprintf(b, format, args);
#pragma GCC diagnostic pop
    va_end(args);
    return status;
}

el_status UncheckedBuffered(el_buffered_writer *w, const char *format, ...)
{
    va_list args;
    va_start(args, format);
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
    const el_status status = el_fmt_vprintf_buffered(w, format, args);
#pragma GCC diagnostic pop
    va_end(args);
    return status;
}

class FmtViolations : public testing::Test
{
  protected:
    static void SetUpTestSuite()
    {
        /* Expected deaths should leave neither a core file nor a crash
         * record; see io_violations_test.cpp. */
#ifdef HAVE_RLIMIT
        const rlimit no_core = {0, 0};
        setrlimit(RLIMIT_CORE, &no_core);
#endif
#ifdef HAVE_PRCTL
        prctl(PR_SET_DUMPABLE, 0);
#endif
    }

    void SetUp() override
    {
        el_buf_init(&b, storage, sizeof storage);
        el_buf_init(&sink, sink_storage, sizeof sink_storage);
        el_buffered_writer_init(&w, el_buf_as_writer(&sink), &b);
    }

    uint8_t storage[64];
    uint8_t sink_storage[64];
    el_buf b;
    el_buf sink;
    el_buffered_writer w;
    char digits[EL_FMT_INT_LEN_MAX];
};

using FmtViolationsDeathTest = FmtViolations;

TEST_F(FmtViolations, CorrectUseDoesNotTripACheck)
{
    /* Including the edge cases. A check that fired would end the test. */
    (void)el_fmt_dec_u64(digits, sizeof digits, UINT64_MAX);
    (void)el_fmt_dec_i64(digits, sizeof digits, INT64_MIN);
    (void)el_fmt_hex_u64(digits, sizeof digits, 1, EL_FMT_UPPER);
    (void)el_fmt_hex_u64(digits, sizeof digits, 1, 0);
    (void)Unchecked(&b, "%0hhd%05hu%lld%zu%zd%3c%.*s%5.2s%.s%%", 1, 2u, 3LL, size_t{4},
                    ptrdiff_t{5}, 'c', -1, "s", "str", "x");
    (void)UncheckedBuffered(&w, "%05d%s", 1, "s");
    el_buf empty;
    el_buf_init(&empty, nullptr, 0);
    (void)Unchecked(&empty, "%d", 1);
}

TEST_F(FmtViolationsDeathTest, AConversionGivenTooLittleRoom)
{
    EXPECT_VIOLATION((void)el_fmt_dec_u64(digits, EL_FMT_INT_LEN_MAX - 1, 1));
    EXPECT_VIOLATION((void)el_fmt_dec_i64(digits, EL_FMT_INT_LEN_MAX - 1, 1));
    EXPECT_VIOLATION((void)el_fmt_hex_u64(digits, EL_FMT_INT_LEN_MAX - 1, 1, 0));
    EXPECT_VIOLATION((void)el_fmt_dec_u64(nullptr, EL_FMT_INT_LEN_MAX, 1));
    EXPECT_VIOLATION((void)el_fmt_dec_i64(nullptr, EL_FMT_INT_LEN_MAX, 1));
    EXPECT_VIOLATION((void)el_fmt_hex_u64(nullptr, EL_FMT_INT_LEN_MAX, 1, 0));
}

TEST_F(FmtViolationsDeathTest, FlagsTheHexConversionDoesNotList)
{
    EXPECT_VIOLATION((void)el_fmt_hex_u64(digits, sizeof digits, 1, 2));
    EXPECT_VIOLATION((void)el_fmt_hex_u64(digits, sizeof digits, 1, EL_FMT_UPPER | 4u));
}

TEST_F(FmtViolationsDeathTest, NullTargetFormatOrString)
{
    EXPECT_VIOLATION((void)el_fmt_printf(nullptr, "a"));
    EXPECT_VIOLATION((void)Unchecked(nullptr, "a"));
    EXPECT_VIOLATION((void)el_fmt_printf_buffered(nullptr, "a"));
    EXPECT_VIOLATION((void)UncheckedBuffered(nullptr, "a"));
    EXPECT_VIOLATION((void)Unchecked(&b, nullptr));
    EXPECT_VIOLATION((void)UncheckedBuffered(&w, nullptr));
    EXPECT_VIOLATION((void)Unchecked(&b, "%s", static_cast<const char *>(nullptr)));
    EXPECT_VIOLATION((void)UncheckedBuffered(&w, "%s", static_cast<const char *>(nullptr)));
}

#if !FMT_TEST_PRINTF_LIBC

/* FMT_PRINTF_BUILTIN: everything outside the portable subset. Under
 * FMT_PRINTF_LIBC most of these are accepted (fmt_libc_test.cpp). */

TEST_F(FmtViolationsDeathTest, ConversionsOutsideTheSubset)
{
    int count = 0;
    EXPECT_VIOLATION((void)Unchecked(&b, "%f", 1.0));
    EXPECT_VIOLATION((void)Unchecked(&b, "%p", static_cast<void *>(storage)));
    EXPECT_VIOLATION((void)Unchecked(&b, "%n", &count));
    EXPECT_VIOLATION((void)Unchecked(&b, "%o", 8u));
    EXPECT_VIOLATION((void)Unchecked(&b, "%e", 1.0));
    EXPECT_VIOLATION((void)UncheckedBuffered(&w, "%f", 1.0));
}

TEST_F(FmtViolationsDeathTest, FlagsWidthsAndModifiersOutsideTheSubset)
{
    EXPECT_VIOLATION((void)Unchecked(&b, "%-5d", 1));
    EXPECT_VIOLATION((void)Unchecked(&b, "%+d", 1));
    EXPECT_VIOLATION((void)Unchecked(&b, "% d", 1));
    EXPECT_VIOLATION((void)Unchecked(&b, "%#x", 1u));
    EXPECT_VIOLATION((void)Unchecked(&b, "%*d", 5, 1));
    EXPECT_VIOLATION((void)Unchecked(&b, "%jd", intmax_t{1}));
    EXPECT_VIOLATION((void)Unchecked(&b, "%td", ptrdiff_t{1}));
    EXPECT_VIOLATION((void)Unchecked(&b, "%Lf", 1.0L));
    EXPECT_VIOLATION((void)Unchecked(&b, "%-8s", "s"));
    EXPECT_VIOLATION((void)Unchecked(&b, "%*c", 3, 'c'));
}

TEST_F(FmtViolationsDeathTest, PrecisionOutsideStrings)
{
    EXPECT_VIOLATION((void)Unchecked(&b, "%.2d", 1));
    EXPECT_VIOLATION((void)Unchecked(&b, "%.2u", 1u));
    EXPECT_VIOLATION((void)Unchecked(&b, "%.*x", 2, 1u));
    EXPECT_VIOLATION((void)Unchecked(&b, "%.2c", 'c'));
}

TEST_F(FmtViolationsDeathTest, ModifiersOnCharactersAndStrings)
{
    EXPECT_VIOLATION((void)Unchecked(&b, "%05s", "s"));
    EXPECT_VIOLATION((void)Unchecked(&b, "%05c", 'c'));
    EXPECT_VIOLATION((void)Unchecked(&b, "%ls", L"s"));
    EXPECT_VIOLATION((void)Unchecked(&b, "%lc", 'c'));
    EXPECT_VIOLATION((void)Unchecked(&b, "%hhs", "s"));
}

#else

/* FMT_PRINTF_LIBC: what neither option accepts. */

TEST_F(FmtViolations, TheWiderSetDoesNotTripACheck)
{
    (void)Unchecked(&b, "%+d%-5d%#x%o%p%jd%td%*d%-*s%*c", 1, 2, 3u, 4u,
                    static_cast<void *>(storage), intmax_t{5}, ptrdiff_t{6}, 3, 7, 4, "s", 2, 'c');
}

TEST_F(FmtViolationsDeathTest, WritesBackAndWideCharacters)
{
    int count = 0;
    EXPECT_VIOLATION((void)Unchecked(&b, "%n", &count));
    EXPECT_VIOLATION((void)Unchecked(&b, "%ls", L"s"));
    EXPECT_VIOLATION((void)Unchecked(&b, "%lc", 'c'));
}

TEST_F(FmtViolationsDeathTest, ModifiersCGivesNoMeaning)
{
    EXPECT_VIOLATION((void)Unchecked(&b, "%Ld", 1));
    EXPECT_VIOLATION((void)Unchecked(&b, "%hf", 1.0));
    EXPECT_VIOLATION((void)Unchecked(&b, "%lp", static_cast<void *>(storage)));
    EXPECT_VIOLATION((void)Unchecked(&b, "%+s", "s"));
    EXPECT_VIOLATION((void)Unchecked(&b, "%05c", 'c'));
    EXPECT_VIOLATION((void)Unchecked(&b, "%.2c", 'c'));
    EXPECT_VIOLATION((void)Unchecked(&b, "%q", 1));
}

#endif

/* In both options. */
TEST_F(FmtViolationsDeathTest, MalformedPercent)
{
    EXPECT_VIOLATION((void)Unchecked(&b, "%"));
    EXPECT_VIOLATION((void)Unchecked(&b, "%5%"));
    EXPECT_VIOLATION((void)Unchecked(&b, "%l"));
    EXPECT_VIOLATION((void)Unchecked(&b, "%-%"));
}

} // namespace
