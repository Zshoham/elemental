/* A null buffer with a nonzero length is a contract violation. */
#include <gtest/gtest.h>

#include <elemental/crc.h>

#include <cstdint>

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

#define EXPECT_VIOLATION(statement) EXPECT_DEATH(statement, "")

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
