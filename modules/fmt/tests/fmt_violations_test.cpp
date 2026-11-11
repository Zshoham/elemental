/*
 * Every contract violation listed in <elemental/fmt.h> and
 * <elemental/fmt_printf.h> stops the program, in whichever way the
 * configuration makes a failed check stop it.
 */
#include <gtest/gtest.h>

#include <elemental/fmt.h>
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
el_status Unchecked(el_fmt *f, const char *format, ...)
{
    va_list args;
    va_start(args, format);
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
    const el_status status = el_fmt_vprintf(f, format, args);
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
        el_fmt_init(&f, buf, sizeof buf);
    }

    char buf[64];
    el_fmt f;
    const uint8_t byte = 0;
};

using FmtViolationsDeathTest = FmtViolations;

TEST_F(FmtViolations, CorrectUseDoesNotTripACheck)
{
    /* Including the edge cases. A check that fired would end the test. */
    el_fmt empty;
    el_fmt_init(&empty, nullptr, 0);
    (void)el_fmt_str(&f, nullptr, 0);
    (void)el_fmt_hex_bytes(&f, nullptr, 0);
    (void)el_fmt_dec_u64(&f, 1, 3, EL_FMT_ZERO_PAD);
    (void)el_fmt_dec_i64(&f, -1, 3, EL_FMT_ZERO_PAD);
    (void)el_fmt_hex_u64(&f, 1, 3, EL_FMT_ZERO_PAD | EL_FMT_UPPER);
    (void)Unchecked(&f, "%0hhd%05hu%lld%zu%zd%3c%.*s%5.2s%%", 1, 2u, 3LL, size_t{4}, ptrdiff_t{5},
                    'c', -1, "s", "str");
}

TEST_F(FmtViolationsDeathTest, NullObject)
{
    EXPECT_VIOLATION(el_fmt_init(nullptr, buf, sizeof buf));
    EXPECT_VIOLATION((void)el_fmt_len(nullptr));
    EXPECT_VIOLATION((void)el_fmt_str(nullptr, "a", 1));
    EXPECT_VIOLATION((void)el_fmt_cstr(nullptr, "a"));
    EXPECT_VIOLATION((void)el_fmt_dec_u64(nullptr, 1, 0, 0));
    EXPECT_VIOLATION((void)el_fmt_dec_i64(nullptr, 1, 0, 0));
    EXPECT_VIOLATION((void)el_fmt_hex_u64(nullptr, 1, 0, 0));
    EXPECT_VIOLATION((void)el_fmt_hex_bytes(nullptr, &byte, 1));
    EXPECT_VIOLATION((void)el_fmt_printf(nullptr, "a"));
    EXPECT_VIOLATION((void)Unchecked(nullptr, "a"));
}

TEST_F(FmtViolationsDeathTest, NullBufferWithANonzeroCapacity)
{
    EXPECT_VIOLATION(el_fmt_init(&f, nullptr, 1));
}

TEST_F(FmtViolationsDeathTest, NullTextOrDataWithANonzeroLength)
{
    EXPECT_VIOLATION((void)el_fmt_str(&f, nullptr, 1));
    EXPECT_VIOLATION((void)el_fmt_hex_bytes(&f, nullptr, 1));
}

TEST_F(FmtViolationsDeathTest, NullCString)
{
    EXPECT_VIOLATION((void)el_fmt_cstr(&f, nullptr));
    EXPECT_VIOLATION((void)Unchecked(&f, nullptr));
    EXPECT_VIOLATION((void)Unchecked(&f, "%s", static_cast<const char *>(nullptr)));
}

TEST_F(FmtViolationsDeathTest, FlagsAFunctionDoesNotList)
{
    EXPECT_VIOLATION((void)el_fmt_dec_u64(&f, 1, 0, EL_FMT_UPPER));
    EXPECT_VIOLATION((void)el_fmt_dec_i64(&f, 1, 0, EL_FMT_UPPER));
    EXPECT_VIOLATION((void)el_fmt_dec_u64(&f, 1, 0, 4));
    EXPECT_VIOLATION((void)el_fmt_hex_u64(&f, 1, 0, 4));
}

TEST_F(FmtViolationsDeathTest, ConversionsOutsideTheSubset)
{
    int count = 0;
    EXPECT_VIOLATION((void)Unchecked(&f, "%f", 1.0));
    EXPECT_VIOLATION((void)Unchecked(&f, "%p", static_cast<void *>(buf)));
    EXPECT_VIOLATION((void)Unchecked(&f, "%n", &count));
    EXPECT_VIOLATION((void)Unchecked(&f, "%o", 8u));
    EXPECT_VIOLATION((void)Unchecked(&f, "%e", 1.0));
}

TEST_F(FmtViolationsDeathTest, FlagsWidthsAndModifiersOutsideTheSubset)
{
    EXPECT_VIOLATION((void)Unchecked(&f, "%-5d", 1));
    EXPECT_VIOLATION((void)Unchecked(&f, "%+d", 1));
    EXPECT_VIOLATION((void)Unchecked(&f, "% d", 1));
    EXPECT_VIOLATION((void)Unchecked(&f, "%#x", 1u));
    EXPECT_VIOLATION((void)Unchecked(&f, "%*d", 5, 1));
    EXPECT_VIOLATION((void)Unchecked(&f, "%jd", intmax_t{1}));
    EXPECT_VIOLATION((void)Unchecked(&f, "%td", ptrdiff_t{1}));
    EXPECT_VIOLATION((void)Unchecked(&f, "%Lf", 1.0L));
}

TEST_F(FmtViolationsDeathTest, PrecisionOutsideStrings)
{
    EXPECT_VIOLATION((void)Unchecked(&f, "%.2d", 1));
    EXPECT_VIOLATION((void)Unchecked(&f, "%.2u", 1u));
    EXPECT_VIOLATION((void)Unchecked(&f, "%.*x", 2, 1u));
    EXPECT_VIOLATION((void)Unchecked(&f, "%.2c", 'c'));
}

TEST_F(FmtViolationsDeathTest, ModifiersOnCharactersAndStrings)
{
    EXPECT_VIOLATION((void)Unchecked(&f, "%05s", "s"));
    EXPECT_VIOLATION((void)Unchecked(&f, "%05c", 'c'));
    EXPECT_VIOLATION((void)Unchecked(&f, "%ls", L"s"));
    EXPECT_VIOLATION((void)Unchecked(&f, "%lc", 'c'));
    EXPECT_VIOLATION((void)Unchecked(&f, "%hhs", "s"));
}

TEST_F(FmtViolationsDeathTest, MalformedPercent)
{
    EXPECT_VIOLATION((void)Unchecked(&f, "%"));
    EXPECT_VIOLATION((void)Unchecked(&f, "%5%"));
    EXPECT_VIOLATION((void)Unchecked(&f, "%l"));
}

} // namespace
