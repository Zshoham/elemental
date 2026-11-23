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

/* A layout built field by field, which skips el_layout_make()'s check, so
 * that el_alloc() and el_free() can be shown to make their own. */
el_layout Raw(size_t size, size_t align)
{
    return el_layout{size, align};
}

/* ---- Allocators that break the operation contract ----------------------- */

void *AllocMisaligned(void *, el_layout)
{
    return storage + 1;
}

void FreeNothing(void *, void *, el_layout)
{
}

el_allocator Broken(void *(*alloc)(void *, el_layout))
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

void *AllocAligned(void *, el_layout)
{
    return storage;
}

void *ReallocMisaligned(void *, void *, el_layout, size_t)
{
    return storage + 1;
}

el_allocator BrokenRealloc()
{
    static el_allocator_ops ops;
    ops.alloc = AllocAligned;
    ops.free = FreeNothing;
    ops.realloc = ReallocMisaligned;
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
    void *const block = el_alloc(a, el_layout_make(8, 8));
    el_free(a, block, el_layout_make(8, 8));
    el_free(a, nullptr, el_layout_make(0, 1));
    el_free(a, el_alloc(a, el_layout_make(0, 16)), el_layout_make(0, 16));
    el_layout out = el_layout_make(1, 1);
    EXPECT_EQ(el_layout_array(el_layout_make(2, 1), SIZE_MAX, &out), EL_ERR_NO_SPACE);
    EXPECT_EQ(el_layout_array(el_layout_make(0, 1), SIZE_MAX, &out), EL_OK);
    EXPECT_EQ(el_layout_make(0, 1).size, 0u);
    void *const top = el_alloc(s, el_layout_make(16, 1));
    el_free(s, top, el_layout_make(16, 1));
    void *const resized =
        el_realloc(a, el_alloc(a, el_layout_make(8, 8)), el_layout_make(8, 8), 16);
    el_free(a, resized, el_layout_make(16, 8));
    /* From nothing to something, and back to nothing. */
    void *const some = el_realloc(a, nullptr, el_layout_make(0, 4), 8);
    void *const none = el_realloc(a, some, el_layout_make(8, 4), 0);
    el_free(a, none, el_layout_make(0, 4));
    el_bump_arena_init(&bump, nullptr, 0);
    el_stack_arena_init(&stack, nullptr, 0);
}

TEST_F(AllocViolationsDeathTest, CallerUsesAHandleThatWasNeverMade)
{
    EXPECT_VIOLATION((void)el_alloc(el_allocator{nullptr, nullptr}, el_layout_make(1, 1)));
    EXPECT_VIOLATION(el_free(el_allocator{nullptr, nullptr}, nullptr, el_layout_make(0, 1)));
    EXPECT_VIOLATION((void)el_alloc(Broken(nullptr), el_layout_make(1, 1)));
    EXPECT_VIOLATION((void)el_alloc(WithoutFree(), el_layout_make(1, 1)));
}

TEST_F(AllocViolationsDeathTest, CallerBuildsALayoutWithAnAlignmentThatIsNotAPowerOfTwo)
{
    EXPECT_VIOLATION((void)el_layout_make(8, 0));
    EXPECT_VIOLATION((void)el_layout_make(8, 3));
    EXPECT_VIOLATION((void)el_layout_make(0, 12));
    EXPECT_VIOLATION((void)el_layout_make(SIZE_MAX, SIZE_MAX));
}

TEST_F(AllocViolationsDeathTest, CallerPassesALayoutWithAnAlignmentThatIsNotAPowerOfTwo)
{
    /* Written field by field, so el_layout_make() did not see it. */
    EXPECT_VIOLATION((void)el_alloc(a, Raw(8, 0)));
    EXPECT_VIOLATION((void)el_alloc(a, Raw(8, 3)));
    EXPECT_VIOLATION((void)el_alloc(a, Raw(0, 12)));
    EXPECT_VIOLATION(el_free(a, nullptr, Raw(0, 5)));
    EXPECT_VIOLATION(el_free(a, el_alloc(a, el_layout_make(8, 8)), Raw(8, 6)));
}

TEST_F(AllocViolationsDeathTest, CallerBuildsAnArrayLayoutWrongly)
{
    el_layout out = el_layout_make(1, 1);
    EXPECT_VIOLATION((void)el_layout_array(Raw(4, 6), 2, &out));
    EXPECT_VIOLATION((void)el_layout_array(Raw(4, 0), 2, &out));
    /* Checked even when the product overflows. */
    EXPECT_VIOLATION((void)el_layout_array(Raw(2, 0), SIZE_MAX, &out));
    EXPECT_VIOLATION((void)el_layout_array(el_layout_make(4, 4), 2, nullptr));
    /* A null output is a bug even when the count would overflow. */
    EXPECT_VIOLATION((void)el_layout_array(el_layout_make(2, 1), SIZE_MAX, nullptr));
}

TEST_F(AllocViolationsDeathTest, CallerFreesANullPointerWithANonzeroSize)
{
    EXPECT_VIOLATION(el_free(a, nullptr, el_layout_make(1, 1)));
}

TEST_F(AllocViolationsDeathTest, CallerFreesARealPointerWithSizeZero)
{
    void *const block = el_alloc(a, el_layout_make(8, 8));
    EXPECT_VIOLATION(el_free(a, block, el_layout_make(0, 8)));
    /* The zero-size pointer of another alignment is not this one's. */
    EXPECT_VIOLATION(el_free(a, el_alloc(a, el_layout_make(0, 1)), el_layout_make(0, 2)));
}

TEST_F(AllocViolationsDeathTest, AllocatorReturnsAMisalignedPointer)
{
    EXPECT_VIOLATION((void)el_alloc(Broken(AllocMisaligned), el_layout_make(4, 4)));
}

TEST_F(AllocViolationsDeathTest, AllocatorReturnsAMisalignedPointerFromRealloc)
{
    EXPECT_VIOLATION((void)el_realloc(BrokenRealloc(), storage, el_layout_make(4, 4), 8));
}

TEST_F(AllocViolationsDeathTest, CallerReallocsANullPointerWithANonzeroSize)
{
    EXPECT_VIOLATION((void)el_realloc(a, nullptr, el_layout_make(1, 1), 2));
}

TEST_F(AllocViolationsDeathTest, CallerReallocsARealPointerWithSizeZero)
{
    void *const block = el_alloc(a, el_layout_make(8, 8));
    EXPECT_VIOLATION((void)el_realloc(a, block, el_layout_make(0, 8), 16));
}

TEST_F(AllocViolationsDeathTest, CallerReallocsWithAnAlignmentThatIsNotAPowerOfTwo)
{
    void *const block = el_alloc(a, el_layout_make(8, 8));
    EXPECT_VIOLATION((void)el_realloc(a, block, Raw(8, 6), 16));
    EXPECT_VIOLATION((void)el_realloc(a, nullptr, Raw(0, 3), 16));
}

TEST_F(AllocViolationsDeathTest, ArenaIsResizedThroughAPointerItDidNotHandOut)
{
    uint8_t other[16];
    void *const block = el_alloc(a, el_layout_make(8, 8));
    EXPECT_VIOLATION((void)el_realloc(a, other, el_layout_make(sizeof(other), 1), 4));
    EXPECT_VIOLATION((void)el_realloc(a, block, el_layout_make(9, 8), 4));
    EXPECT_VIOLATION((void)el_realloc(s, other, el_layout_make(sizeof(other), 1), 4));
    el_bump_arena_reset(&bump);
    EXPECT_VIOLATION((void)el_realloc(a, block, el_layout_make(8, 8), 4));
}

TEST_F(AllocViolationsDeathTest, ArenaIsGivenAPointerItDidNotHandOut)
{
    uint8_t other[16];
    void *const block = el_alloc(a, el_layout_make(8, 8));
    EXPECT_VIOLATION(el_free(a, other, el_layout_make(sizeof(other), 1)));
    EXPECT_VIOLATION(el_free(a, block, el_layout_make(9, 8))); /* runs past what was handed out */
    EXPECT_VIOLATION(el_free(s, other, el_layout_make(sizeof(other), 1)));
}

TEST_F(AllocViolationsDeathTest, ArenaIsFreedIntoAfterReset)
{
    void *const block = el_alloc(a, el_layout_make(8, 8));
    el_bump_arena_reset(&bump);
    EXPECT_VIOLATION(el_free(a, block, el_layout_make(8, 8)));

    void *const top = el_alloc(s, el_layout_make(8, 8));
    el_stack_arena_reset(&stack);
    EXPECT_VIOLATION(el_free(s, top, el_layout_make(8, 8)));
}

TEST_F(AllocViolationsDeathTest, StackArenaFreesTheLatestAllocationTwice)
{
    void *const top = el_alloc(s, el_layout_make(8, 8));
    el_free(s, top, el_layout_make(8, 8));
    EXPECT_VIOLATION(el_free(s, top, el_layout_make(8, 8)));
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
    void *const first = el_alloc(c, el_layout_make(8, 8));
    void *const second = el_alloc(c, el_layout_make(24, 4));
    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);
    EXPECT_EQ(el_alloc_checker_live(&checker), 2u);
    el_free(c, first, el_layout_make(8, 8));
    EXPECT_EQ(el_alloc_checker_live(&checker), 1u);
    el_free(c, second, el_layout_make(24, 4));
    EXPECT_EQ(el_alloc_checker_live(&checker), 0u);
    /* A refusal from the inner allocator is passed on, and not recorded. */
    EXPECT_EQ(el_alloc(c, el_layout_make(sizeof(storage) * 2, 1)), nullptr);
    EXPECT_EQ(el_alloc_checker_live(&checker), 0u);
}

TEST_F(AllocChecker, ARecordedResizeIsFreedWithItsNewPointerAndLayout)
{
    void *const first = el_alloc(c, el_layout_make(32, 8));
    void *const second = el_alloc(c, el_layout_make(8, 8));
    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);

    /* The second is the latest and grows in place; the first cannot, so it moves. */
    void *const grown = el_realloc(c, second, el_layout_make(8, 8), 16);
    void *const moved = el_realloc(c, first, el_layout_make(32, 8), 64);
    ASSERT_NE(moved, nullptr);
    ASSERT_NE(moved, first);
    EXPECT_EQ(grown, second);
    EXPECT_EQ(el_alloc_checker_live(&checker), 2u) << "a resize is not a new allocation";

    /* A refusal is passed on and changes the record of nothing. */
    EXPECT_EQ(el_realloc(c, moved, el_layout_make(64, 8), sizeof(storage) * 2), nullptr);
    EXPECT_EQ(el_alloc_checker_live(&checker), 2u);
    el_free(c, moved, el_layout_make(64, 8));
    el_free(c, grown, el_layout_make(16, 8));
    EXPECT_EQ(el_alloc_checker_live(&checker), 0u);

    /* To nothing and from nothing go through as a free and an allocation. */
    void *const block = el_alloc(c, el_layout_make(8, 8));
    void *const zero = el_realloc(c, block, el_layout_make(8, 8), 0);
    EXPECT_NE(zero, nullptr);
    EXPECT_EQ(el_alloc_checker_live(&checker), 0u);
    void *const back = el_realloc(c, zero, el_layout_make(0, 8), 24);
    ASSERT_NE(back, nullptr);
    EXPECT_EQ(el_alloc_checker_live(&checker), 1u);
    el_free(c, back, el_layout_make(24, 8));
}

TEST_F(AllocCheckerDeathTest, StopsAFreeWithTheWrongSize)
{
    void *const block = el_alloc(c, el_layout_make(32, 8));
    EXPECT_DEATH(el_free(c, block, el_layout_make(16, 8)), "different size");
}

TEST_F(AllocCheckerDeathTest, StopsAFreeWithTheWrongAlignment)
{
    void *const block = el_alloc(c, el_layout_make(32, 8));
    EXPECT_DEATH(el_free(c, block, el_layout_make(32, 4)), "different alignment");
}

TEST_F(AllocCheckerDeathTest, StopsADoubleFreeOrAForeignPointer)
{
    void *const block = el_alloc(c, el_layout_make(32, 8));
    el_free(c, block, el_layout_make(32, 8));
    EXPECT_DEATH(el_free(c, block, el_layout_make(32, 8)), "not a live allocation");
    uint8_t other[4];
    EXPECT_DEATH(el_free(c, other, el_layout_make(sizeof(other), 1)), "not a live allocation");
}

TEST_F(AllocCheckerDeathTest, StopsWhenItRunsOutOfRecords)
{
    for (int i = 0; i < 4; i++) {
        ASSERT_NE(el_alloc(c, el_layout_make(1, 1)), nullptr);
    }
    EXPECT_DEATH((void)el_alloc(c, el_layout_make(1, 1)), "out of records");
}

TEST_F(AllocCheckerDeathTest, StopsAResizeWithTheWrongSizeAlignmentOrPointer)
{
    void *const block = el_alloc(c, el_layout_make(32, 8));
    EXPECT_DEATH((void)el_realloc(c, block, el_layout_make(16, 8), 64), "different size");
    EXPECT_DEATH((void)el_realloc(c, block, el_layout_make(32, 4), 64), "different alignment");
    uint8_t other[4];
    EXPECT_DEATH((void)el_realloc(c, other, el_layout_make(sizeof(other), 1), 64),
                 "not a live allocation");
}

TEST_F(AllocCheckerDeathTest, StopsAFreeWithTheOldSizeAfterAResize)
{
    void *const block = el_alloc(c, el_layout_make(32, 8));
    void *const grown = el_realloc(c, block, el_layout_make(32, 8), 64);
    ASSERT_NE(grown, nullptr);
    EXPECT_DEATH(el_free(c, grown, el_layout_make(32, 8)), "different size");
}

TEST_F(AllocCheckerDeathTest, StopsAFreeWithTheOldPointerAfterAResizeMoved)
{
    void *const block = el_alloc(c, el_layout_make(32, 8));
    ASSERT_NE(el_alloc(c, el_layout_make(8, 8)), nullptr); /* so `block` cannot grow in place */
    void *const moved = el_realloc(c, block, el_layout_make(32, 8), 64);
    ASSERT_NE(moved, nullptr);
    ASSERT_NE(moved, block);
    EXPECT_DEATH(el_free(c, block, el_layout_make(32, 8)), "not a live allocation");
    EXPECT_DEATH((void)el_realloc(c, block, el_layout_make(32, 8), 16), "not a live allocation");
}

} // namespace
