/*
 * Contract violations of elemental/ring.h are caught.
 *
 * These are death tests: each violation runs in a child process, and the
 * test passes if that process is stopped. So they hold in every assertion
 * configuration, whether a failed check traps in place or calls the
 * application's handler (which, in tests, reports and aborts).
 */
#include <gtest/gtest.h>

#include <elemental/ring.h>

#include <cstddef>
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

/* How a violation ends depends on the configuration, so any death counts. */
#define EXPECT_VIOLATION(statement) EXPECT_DEATH(statement, "")

class RingViolations : public testing::Test
{
  protected:
    static void SetUpTestSuite()
    {
        /* A failed check kills the child with a signal. That is the expected
         * outcome, so it should leave neither a core file nor a crash record
         * behind. The limit covers core files; on Linux, a crash handler that
         * core dumps are piped to ignores the limit, so the process is also
         * marked as not dumpable. */
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
        el_ring_init(&ring, storage, sizeof(storage));
    }

    uint8_t storage[4] = {};
    uint8_t buf[4] = {};
    el_ring ring;
};

using RingViolationsDeathTest = RingViolations;

TEST_F(RingViolations, CorrectUseDoesNotTripACheck)
{
    el_ring empty;
    el_ring_init(&empty, nullptr, 0);
    /* Storage that is not used may be given with a capacity of 0. */
    el_ring_init(&empty, storage, 0);

    EXPECT_EQ(el_ring_write(&ring, nullptr, 0), 0u);
    EXPECT_EQ(el_ring_read(&ring, nullptr, 0), 0u);
    EXPECT_EQ(el_ring_write(&ring, buf, sizeof(buf)), 4u);
    EXPECT_EQ(el_ring_write(&ring, buf, sizeof(buf)), 0u);
    el_ring_close(&ring);
    el_ring_close(&ring);
    EXPECT_EQ(el_ring_write(&ring, buf, sizeof(buf)), 0u);
    EXPECT_EQ(el_ring_read(&ring, buf, sizeof(buf)), 4u);
    EXPECT_EQ(el_ring_read(&ring, buf, sizeof(buf)), 0u);
    (void)el_ring_as_reader(&ring);
    (void)el_ring_as_writer(&ring);
}

TEST_F(RingViolationsDeathTest, ARingIsSetUpWithoutARingOrWithoutStorage)
{
    EXPECT_VIOLATION(el_ring_init(nullptr, storage, sizeof(storage)));
    EXPECT_VIOLATION(el_ring_init(&ring, nullptr, 1));
}

TEST_F(RingViolationsDeathTest, ARingIsSetUpWithACapacityItsIndicesCannotCount)
{
    EXPECT_VIOLATION(el_ring_init(&ring, storage, SIZE_MAX / 2 + 1));
}

TEST_F(RingViolationsDeathTest, AFunctionIsGivenNoRing)
{
    EXPECT_VIOLATION((void)el_ring_write(nullptr, buf, sizeof(buf)));
    EXPECT_VIOLATION((void)el_ring_read(nullptr, buf, sizeof(buf)));
    EXPECT_VIOLATION((void)el_ring_room(nullptr));
    EXPECT_VIOLATION((void)el_ring_len(nullptr));
    EXPECT_VIOLATION(el_ring_close(nullptr));
    EXPECT_VIOLATION((void)el_ring_as_reader(nullptr));
    EXPECT_VIOLATION((void)el_ring_as_writer(nullptr));
}

TEST_F(RingViolationsDeathTest, ANullBufferIsGivenWithANonzeroSize)
{
    EXPECT_VIOLATION((void)el_ring_write(&ring, nullptr, 1));
    EXPECT_VIOLATION((void)el_ring_read(&ring, nullptr, 1));
}

} // namespace
