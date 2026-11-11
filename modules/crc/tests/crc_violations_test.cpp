/* A null buffer with a nonzero length is a contract violation. */
#include <gtest/gtest.h>

#include <elemental/crc.h>

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

class CrcViolations : public testing::Test
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
};

using CrcViolationsDeathTest = CrcViolations;

TEST_F(CrcViolations, CorrectUseDoesNotTripACheck)
{
    const uint8_t byte = 0;
    (void)el_crc32_update(EL_CRC32_INIT, nullptr, 0);
    (void)el_crc16_ibm3740_update(EL_CRC16_IBM3740_INIT, nullptr, 0);
    (void)el_crc32_update(EL_CRC32_INIT, &byte, 1);
    (void)el_crc16_ibm3740_update(EL_CRC16_IBM3740_INIT, &byte, 1);
}

TEST_F(CrcViolationsDeathTest, NullDataWithANonzeroLength)
{
    EXPECT_VIOLATION((void)el_crc32_update(EL_CRC32_INIT, nullptr, 1));
    EXPECT_VIOLATION((void)el_crc16_ibm3740_update(EL_CRC16_IBM3740_INIT, nullptr, 1));
}

} // namespace
