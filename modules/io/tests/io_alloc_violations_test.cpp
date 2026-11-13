/*
 * el_grow_buf's contract violations stop the program. Death tests, as in
 * io_violations_test.cpp.
 */
#include <gtest/gtest.h>

#include <elemental/alloc_arena.h>
#include <elemental/io_alloc.h>

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

class IoAllocViolations : public testing::Test
{
  protected:
    static void SetUpTestSuite()
    {
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
        el_bump_arena_init(&arena, storage, sizeof(storage));
        a = el_bump_arena_as_allocator(&arena);
    }

    alignas(16) uint8_t storage[64];
    el_bump_arena arena;
    el_allocator a;
    el_grow_buf g;
};

using IoAllocViolationsDeathTest = IoAllocViolations;

TEST_F(IoAllocViolations, CorrectUseDoesNotTripACheck)
{
    el_buf *const b = el_grow_buf_init(&g, a);
    EXPECT_EQ(el_buf_append(b, "x", 1), EL_OK);
    el_grow_buf_deinit(&g);
    /* After deinit the buffer is a valid, always full fixed buffer. */
    EXPECT_EQ(el_buf_append(b, "x", 1), EL_ERR_NO_SPACE);
    (void)el_grow_buf_init(&g, a);
    el_grow_buf_deinit(&g);
}

TEST_F(IoAllocViolationsDeathTest, CallerBreaksAGrowBufPrecondition)
{
    EXPECT_VIOLATION((void)el_grow_buf_init(nullptr, a));
    EXPECT_VIOLATION((void)el_grow_buf_init(&g, el_allocator{nullptr, nullptr}));
    EXPECT_VIOLATION(el_grow_buf_deinit(nullptr));
}

TEST_F(IoAllocViolationsDeathTest, CallerDeinitializesTwice)
{
    (void)el_grow_buf_init(&g, a);
    el_grow_buf_deinit(&g);
    EXPECT_VIOLATION(el_grow_buf_deinit(&g));
}

} // namespace
