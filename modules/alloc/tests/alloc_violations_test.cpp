/*
 * Contract violations are caught, on both sides of the interface: callers
 * that break a precondition, and allocators that break the operation
 * contract. Also the checking allocator, which turns the violations no
 * allocator can see on its own into a stopped program.
 *
 * Death tests: each violation runs in a child process, and the test passes
 * if that process is stopped, so they hold in every assertion
 * configuration.
 */
#include <gtest/gtest.h>

#include <elemental/alloc.h>
#include <elemental/alloc_arena.h>
#include <elemental/alloc_contract.h>

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

alignas(64) uint8_t storage[256];

/* ---- Allocators that break the operation contract ----------------------- */

void *AllocMisaligned(void *, size_t, size_t)
{
    return storage + 1;
}

void FreeNothing(void *, void *, size_t, size_t)
{
}

el_allocator Broken(void *(*alloc)(void *, size_t, size_t))
{
    static el_allocator_ops ops;
    ops.alloc = alloc;
    ops.free = FreeNothing;
    return el_allocator{nullptr, &ops};
}

el_allocator WithoutFree()
{
    static el_allocator_ops ops;
    ops.alloc = AllocMisaligned;
    ops.free = nullptr;
    return el_allocator{nullptr, &ops};
}

class AllocViolations : public testing::Test
{
  protected:
    static void SetUpTestSuite()
    {
        /* A failed check kills the child with a signal: leave neither a core
         * file nor a crash record behind. */
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
        el_bump_arena_init(&bump, storage, sizeof(storage));
        el_stack_arena_init(&stack, storage, sizeof(storage));
        a = el_bump_arena_as_allocator(&bump);
        s = el_stack_arena_as_allocator(&stack);
    }

    el_bump_arena bump;
    el_stack_arena stack;
    el_allocator a;
    el_allocator s;
};

/* GoogleTest runs suites named *DeathTest first, before any threads exist. */
using AllocViolationsDeathTest = AllocViolations;

TEST_F(AllocViolations, CorrectUseDoesNotTripACheck)
{
    /* Including the edge cases. A check that fired would end the test. */
    void *const block = el_alloc(a, 8, 8);
    el_free(a, block, 8, 8);
    el_free(a, nullptr, 0, 1);
    el_free(a, el_alloc(a, 0, 16), 0, 16);
    EXPECT_EQ(el_alloc_array(a, SIZE_MAX, 2, 1), nullptr);
    void *const top = el_alloc(s, 16, 1);
    el_free(s, top, 16, 1);
    el_bump_arena_init(&bump, nullptr, 0);
    el_stack_arena_init(&stack, nullptr, 0);
}

TEST_F(AllocViolationsDeathTest, CallerUsesAHandleThatWasNeverMade)
{
    EXPECT_VIOLATION(el_alloc(el_allocator{nullptr, nullptr}, 1, 1));
    EXPECT_VIOLATION(el_alloc_array(el_allocator{nullptr, nullptr}, 1, 1, 1));
    EXPECT_VIOLATION(el_free(el_allocator{nullptr, nullptr}, nullptr, 0, 1));
    EXPECT_VIOLATION(el_alloc(Broken(nullptr), 1, 1));
    EXPECT_VIOLATION(el_alloc(WithoutFree(), 1, 1));
}

TEST_F(AllocViolationsDeathTest, CallerPassesAnAlignmentThatIsNotAPowerOfTwo)
{
    EXPECT_VIOLATION(el_alloc(a, 8, 0));
    EXPECT_VIOLATION(el_alloc(a, 8, 3));
    EXPECT_VIOLATION(el_alloc(a, 0, 12));
    EXPECT_VIOLATION(el_alloc_array(a, 2, 4, 6));
    /* Checked even when the product overflows. */
    EXPECT_VIOLATION(el_alloc_array(a, SIZE_MAX, 2, 0));
    EXPECT_VIOLATION(el_free(a, nullptr, 0, 5));
}

TEST_F(AllocViolationsDeathTest, CallerFreesANullPointerWithANonzeroSize)
{
    EXPECT_VIOLATION(el_free(a, nullptr, 1, 1));
}

TEST_F(AllocViolationsDeathTest, CallerFreesARealPointerWithSizeZero)
{
    void *const block = el_alloc(a, 8, 8);
    EXPECT_VIOLATION(el_free(a, block, 0, 8));
    /* The zero-size pointer of another alignment is not this one's. */
    EXPECT_VIOLATION(el_free(a, el_alloc(a, 0, 1), 0, 2));
}

TEST_F(AllocViolationsDeathTest, AllocatorReturnsAMisalignedPointer)
{
    EXPECT_VIOLATION(el_alloc(Broken(AllocMisaligned), 4, 4));
}

TEST_F(AllocViolationsDeathTest, ArenaIsGivenAPointerItDidNotHandOut)
{
    uint8_t other[16];
    void *const block = el_alloc(a, 8, 8);
    EXPECT_VIOLATION(el_free(a, other, sizeof(other), 1));
    EXPECT_VIOLATION(el_free(a, block, 9, 8)); /* runs past what was handed out */
    EXPECT_VIOLATION(el_free(s, other, sizeof(other), 1));
}

TEST_F(AllocViolationsDeathTest, ArenaIsFreedIntoAfterReset)
{
    void *const block = el_alloc(a, 8, 8);
    el_bump_arena_reset(&bump);
    EXPECT_VIOLATION(el_free(a, block, 8, 8));

    void *const top = el_alloc(s, 8, 8);
    el_stack_arena_reset(&stack);
    EXPECT_VIOLATION(el_free(s, top, 8, 8));
}

TEST_F(AllocViolationsDeathTest, StackArenaFreesTheLatestAllocationTwice)
{
    void *const top = el_alloc(s, 8, 8);
    el_free(s, top, 8, 8);
    EXPECT_VIOLATION(el_free(s, top, 8, 8));
}

TEST_F(AllocViolationsDeathTest, CallerBreaksAnArenaPrecondition)
{
    EXPECT_VIOLATION(el_bump_arena_init(nullptr, storage, sizeof(storage)));
    EXPECT_VIOLATION(el_bump_arena_init(&bump, nullptr, 1));
    EXPECT_VIOLATION((void)el_bump_arena_as_allocator(nullptr));
    EXPECT_VIOLATION(el_bump_arena_reset(nullptr));
    EXPECT_VIOLATION(el_stack_arena_init(nullptr, storage, sizeof(storage)));
    EXPECT_VIOLATION(el_stack_arena_init(&stack, nullptr, 1));
    EXPECT_VIOLATION((void)el_stack_arena_as_allocator(nullptr));
    EXPECT_VIOLATION(el_stack_arena_reset(nullptr));
}

/* ---- The checking allocator --------------------------------------------- */

class AllocChecker : public AllocViolations
{
  protected:
    void SetUp() override
    {
        AllocViolations::SetUp();
        el_alloc_checker_init(&checker, a, records, 4);
        c = el_alloc_checker_as_allocator(&checker);
    }

    el_alloc_check_record records[4];
    el_alloc_checker checker;
    el_allocator c;
};

using AllocCheckerDeathTest = AllocChecker;

TEST_F(AllocChecker, PassesCorrectUseThroughAndCountsLiveAllocations)
{
    void *const first = el_alloc(c, 8, 8);
    void *const second = el_alloc(c, 24, 4);
    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);
    EXPECT_EQ(el_alloc_checker_live(&checker), 2u);
    el_free(c, first, 8, 8);
    EXPECT_EQ(el_alloc_checker_live(&checker), 1u);
    el_free(c, second, 24, 4);
    EXPECT_EQ(el_alloc_checker_live(&checker), 0u);
    /* A refusal from the inner allocator is passed on, and not recorded. */
    EXPECT_EQ(el_alloc(c, sizeof(storage) * 2, 1), nullptr);
    EXPECT_EQ(el_alloc_checker_live(&checker), 0u);
}

TEST_F(AllocCheckerDeathTest, StopsAFreeWithTheWrongSize)
{
    void *const block = el_alloc(c, 32, 8);
    EXPECT_DEATH(el_free(c, block, 16, 8), "different size");
}

TEST_F(AllocCheckerDeathTest, StopsAFreeWithTheWrongAlignment)
{
    void *const block = el_alloc(c, 32, 8);
    EXPECT_DEATH(el_free(c, block, 32, 4), "different alignment");
}

TEST_F(AllocCheckerDeathTest, StopsADoubleFreeOrAForeignPointer)
{
    void *const block = el_alloc(c, 32, 8);
    el_free(c, block, 32, 8);
    EXPECT_DEATH(el_free(c, block, 32, 8), "not a live allocation");
    uint8_t other[4];
    EXPECT_DEATH(el_free(c, other, sizeof(other), 1), "not a live allocation");
}

TEST_F(AllocCheckerDeathTest, StopsWhenItRunsOutOfRecords)
{
    for (int i = 0; i < 4; i++) {
        ASSERT_NE(el_alloc(c, 1, 1), nullptr);
    }
    EXPECT_DEATH(el_alloc(c, 1, 1), "out of records");
}

} // namespace
