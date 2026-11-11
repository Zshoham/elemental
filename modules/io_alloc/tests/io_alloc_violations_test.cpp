/*
 * The grow writer's contract violations stop the program. Death tests, as
 * in io_violations_test.cpp.
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
    el_grow_writer gw;
    size_t n = 0;
};

using IoAllocViolationsDeathTest = IoAllocViolations;

TEST_F(IoAllocViolations, CorrectUseDoesNotTripACheck)
{
    el_grow_writer_init(&gw, a);
    const uint8_t byte = 1;
    EXPECT_EQ(el_write(el_grow_writer_as_writer(&gw), &byte, 1, &n), EL_OK);
    EXPECT_EQ(el_grow_writer_len(&gw), 1u);
    el_grow_writer_deinit(&gw);
}

TEST_F(IoAllocViolationsDeathTest, CallerBreaksAGrowWriterPrecondition)
{
    EXPECT_VIOLATION(el_grow_writer_init(nullptr, a));
    EXPECT_VIOLATION(el_grow_writer_init(&gw, el_allocator{nullptr, nullptr}));
    EXPECT_VIOLATION((void)el_grow_writer_as_writer(nullptr));
    EXPECT_VIOLATION((void)el_grow_writer_data(nullptr));
    EXPECT_VIOLATION((void)el_grow_writer_len(nullptr));
    EXPECT_VIOLATION(el_grow_writer_deinit(nullptr));
}

TEST_F(IoAllocViolationsDeathTest, CallerUsesAWriterAfterDeinit)
{
    el_grow_writer_init(&gw, a);
    const el_writer w = el_grow_writer_as_writer(&gw);
    el_grow_writer_deinit(&gw);
    const uint8_t byte = 1;
    EXPECT_VIOLATION(el_write(w, &byte, 1, &n));
    EXPECT_VIOLATION((void)el_grow_writer_as_writer(&gw));
    EXPECT_VIOLATION((void)el_grow_writer_len(&gw));
    EXPECT_VIOLATION(el_grow_writer_deinit(&gw));
}

} // namespace
