/*
 * A null pointer is a contract violation for every function. On a core
 * without memory protection, address 0 is readable, so without the check a
 * null load would return whatever is there.
 */
#include <gtest/gtest.h>

#include <elemental/bytes.h>

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

class BytesViolations : public testing::Test
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

    uint8_t bytes[8] = {};
};

using BytesViolationsDeathTest = BytesViolations;

TEST_F(BytesViolations, CorrectUseDoesNotTripACheck)
{
    el_bytes_store_le64(bytes, el_bytes_load_be64(bytes));
    el_bytes_store_be16(bytes + 1, el_bytes_load_le16(bytes + 3));
}

TEST_F(BytesViolationsDeathTest, LoadFromANullPointer)
{
    EXPECT_VIOLATION((void)el_bytes_load_le16(nullptr));
    EXPECT_VIOLATION((void)el_bytes_load_le32(nullptr));
    EXPECT_VIOLATION((void)el_bytes_load_le64(nullptr));
    EXPECT_VIOLATION((void)el_bytes_load_be16(nullptr));
    EXPECT_VIOLATION((void)el_bytes_load_be32(nullptr));
    EXPECT_VIOLATION((void)el_bytes_load_be64(nullptr));
}

TEST_F(BytesViolationsDeathTest, StoreToANullPointer)
{
    EXPECT_VIOLATION(el_bytes_store_le16(nullptr, 1));
    EXPECT_VIOLATION(el_bytes_store_le32(nullptr, 1));
    EXPECT_VIOLATION(el_bytes_store_le64(nullptr, 1));
    EXPECT_VIOLATION(el_bytes_store_be16(nullptr, 1));
    EXPECT_VIOLATION(el_bytes_store_be32(nullptr, 1));
    EXPECT_VIOLATION(el_bytes_store_be64(nullptr, 1));
}

} // namespace
