/*
 * A null pointer is a contract violation for every function that takes one.
 * On a core without memory protection, address 0 is readable, so without the
 * check a null load would return whatever is there.
 *
 * So is a bit field that does not lie within its type, a mask wider than its
 * type, and an alignment that is not a power of two. A `field` wider than its
 * `width` is not one: it is ignored (`bits_test.cpp`).
 */
#include <gtest/gtest.h>

#include <elemental/bytes.h>

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

TEST_F(BytesViolations, TheEdgesOfTheBitFieldAndAlignmentContractsDoNotTripACheck)
{
    EXPECT_EQ(el_bytes_mask32(32), UINT32_MAX);
    EXPECT_EQ(el_bytes_mask64(64), UINT64_MAX);
    EXPECT_EQ(el_bytes_field_get32(UINT32_MAX, 0, 32), UINT32_MAX);
    EXPECT_EQ(el_bytes_field_get32(UINT32_MAX, 31, 1), 1u);
    EXPECT_EQ(el_bytes_field_get32(UINT32_MAX, 32, 0), 0u);
    EXPECT_EQ(el_bytes_field_get64(UINT64_MAX, 0, 64), UINT64_MAX);
    EXPECT_EQ(el_bytes_field_get64(UINT64_MAX, 63, 1), 1u);
    EXPECT_EQ(el_bytes_field_get64(UINT64_MAX, 64, 0), 0u);
    EXPECT_EQ(el_bytes_field_set32(0, 0, 32, UINT32_MAX), UINT32_MAX);
    EXPECT_EQ(el_bytes_field_set32(0, 32, 0, UINT32_MAX), 0u);
    EXPECT_EQ(el_bytes_field_set64(0, 0, 64, UINT64_MAX), UINT64_MAX);
    EXPECT_EQ(el_bytes_field_set64(0, 64, 0, UINT64_MAX), 0u);
    /* A field wider than its width is ignored, not a violation. */
    EXPECT_EQ(el_bytes_field_set32(0, 0, 4, UINT32_MAX), 0xFu);
    EXPECT_EQ(el_bytes_field_set64(0, 60, 4, UINT64_MAX), 0xF000000000000000u);
    EXPECT_EQ(el_bytes_align_up32(1, 0x80000000u), 0x80000000u);
    EXPECT_EQ(el_bytes_align_down32(UINT32_MAX, 0x80000000u), 0x80000000u);
    EXPECT_TRUE(el_bytes_is_aligned32(0, 1));
    EXPECT_EQ(el_bytes_align_up64(1, 0x8000000000000000u), 0x8000000000000000u);
    EXPECT_EQ(el_bytes_align_down64(UINT64_MAX, 0x8000000000000000u), 0x8000000000000000u);
    EXPECT_TRUE(el_bytes_is_aligned64(0, 1));
    /* Not a violation: the multiple does not fit, and the result is 0. */
    EXPECT_EQ(el_bytes_align_up32(UINT32_MAX, 2), 0u);
    EXPECT_EQ(el_bytes_align_up64(UINT64_MAX, 2), 0u);
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

TEST_F(BytesViolationsDeathTest, AMaskWiderThanTheType)
{
    EXPECT_VIOLATION((void)el_bytes_mask32(33));
    EXPECT_VIOLATION((void)el_bytes_mask32(UINT32_MAX));
    EXPECT_VIOLATION((void)el_bytes_mask64(65));
    EXPECT_VIOLATION((void)el_bytes_mask64(UINT32_MAX));
}

TEST_F(BytesViolationsDeathTest, GettingAFieldThatIsNotWithinTheType)
{
    EXPECT_VIOLATION((void)el_bytes_field_get32(0, 0, 33));
    EXPECT_VIOLATION((void)el_bytes_field_get32(0, 1, 32));
    EXPECT_VIOLATION((void)el_bytes_field_get32(0, 32, 1));
    EXPECT_VIOLATION((void)el_bytes_field_get32(0, 33, 0));
    EXPECT_VIOLATION((void)el_bytes_field_get32(0, 16, 17));
    /* shift + width wraps to 1: the check must not be fooled. */
    EXPECT_VIOLATION((void)el_bytes_field_get32(0, UINT32_MAX, 2));
    EXPECT_VIOLATION((void)el_bytes_field_get64(0, 0, 65));
    EXPECT_VIOLATION((void)el_bytes_field_get64(0, 1, 64));
    EXPECT_VIOLATION((void)el_bytes_field_get64(0, 64, 1));
    EXPECT_VIOLATION((void)el_bytes_field_get64(0, 65, 0));
    EXPECT_VIOLATION((void)el_bytes_field_get64(0, 32, 33));
    EXPECT_VIOLATION((void)el_bytes_field_get64(0, UINT32_MAX, 2));
}

TEST_F(BytesViolationsDeathTest, SettingAFieldThatIsNotWithinTheType)
{
    EXPECT_VIOLATION((void)el_bytes_field_set32(0, 0, 33, 0));
    EXPECT_VIOLATION((void)el_bytes_field_set32(0, 1, 32, 0));
    EXPECT_VIOLATION((void)el_bytes_field_set32(0, 32, 1, 0));
    EXPECT_VIOLATION((void)el_bytes_field_set32(0, 33, 0, 0));
    EXPECT_VIOLATION((void)el_bytes_field_set32(0, UINT32_MAX, 2, 0));
    EXPECT_VIOLATION((void)el_bytes_field_set64(0, 0, 65, 0));
    EXPECT_VIOLATION((void)el_bytes_field_set64(0, 1, 64, 0));
    EXPECT_VIOLATION((void)el_bytes_field_set64(0, 64, 1, 0));
    EXPECT_VIOLATION((void)el_bytes_field_set64(0, 65, 0, 0));
    EXPECT_VIOLATION((void)el_bytes_field_set64(0, UINT32_MAX, 2, 0));
}

TEST_F(BytesViolationsDeathTest, AlignmentThatIsNotAPowerOfTwo)
{
    for (uint32_t align : {0u, 3u, 6u, 12u, 0x80000001u, UINT32_MAX}) {
        EXPECT_VIOLATION((void)el_bytes_align_up32(8, align));
        EXPECT_VIOLATION((void)el_bytes_align_down32(8, align));
        EXPECT_VIOLATION((void)el_bytes_is_aligned32(8, align));
    }
    for (uint64_t align : {uint64_t{0}, uint64_t{3}, uint64_t{6}, uint64_t{0x100000001u},
                           uint64_t{0x8000000000000001u}, UINT64_MAX}) {
        EXPECT_VIOLATION((void)el_bytes_align_up64(8, align));
        EXPECT_VIOLATION((void)el_bytes_align_down64(8, align));
        EXPECT_VIOLATION((void)el_bytes_is_aligned64(8, align));
    }
}

} // namespace
