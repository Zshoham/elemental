/*
 * The front functions and the two arenas: what each promises beyond the
 * contract suite. Exact placement, padding, the difference between the
 * arenas' frees, and the overflow check against the compiler's own.
 */
#include <gtest/gtest.h>

#include <elemental/alloc.h>
#include <elemental/alloc_arena.h>
#include <elemental/status.h>

#include "layout_probe.h"

#include <climits>
#include <cstddef>
#include <cstdint>

namespace
{

alignas(64) uint8_t storage[512];

uintptr_t Address(const void *ptr)
{
    return reinterpret_cast<uintptr_t>(ptr);
}

/* ---- Front functions ---------------------------------------------------- */

TEST(AllocFront, ZeroSizePointerIsTheHighestAlignedAddress)
{
    el_bump_arena arena;
    el_bump_arena_init(&arena, storage, sizeof(storage));
    const el_allocator a = el_bump_arena_as_allocator(&arena);

    for (size_t align = 1; align != 0; align <<= 1) {
        EXPECT_EQ(Address(el_alloc(a, el_layout_make(0, align))),
                  ~uintptr_t{0} & ~(uintptr_t{align} - 1));
    }
    /* Nothing came from the arena. */
    EXPECT_NE(el_alloc(a, el_layout_make(sizeof(storage), 1)), nullptr);
}

TEST(AllocFront, AnArrayLayoutAllocatesExactlyTheProduct)
{
    el_bump_arena arena;
    el_bump_arena_init(&arena, storage, sizeof(storage));
    const el_allocator a = el_bump_arena_as_allocator(&arena);

    /* Exactly the storage: 16 * 32 fits; one more byte would not. */
    el_layout array;
    ASSERT_EQ(el_layout_array(el_layout_make(32, 1), 16, &array), EL_OK);
    EXPECT_NE(el_alloc(a, array), nullptr);
    EXPECT_EQ(el_alloc(a, el_layout_make(1, 1)), nullptr);
}

/* ---- Layout ------------------------------------------------------------- */

constexpr el_layout kUntouched = {0x5eed, 0x80};

TEST(AllocLayout, MakeStoresTheSizeAndTheAlignment)
{
    for (size_t align = 1; align != 0; align <<= 1) {
        for (const size_t size : {size_t{0}, size_t{1}, align, SIZE_MAX}) {
            const el_layout layout = el_layout_make(size, align);
            EXPECT_EQ(layout.size, size);
            EXPECT_EQ(layout.align, align);
        }
    }
}

TEST(AllocLayout, OfATypeIsItsSizeAndAlignment)
{
    const struct {
        el_layout layout;
        size_t size;
        size_t align;
    } cases[] = {
        {alloc_probe_char(), sizeof(char), alignof(char)},
        {alloc_probe_u16(), sizeof(uint16_t), alignof(uint16_t)},
        {alloc_probe_u64(), sizeof(uint64_t), alignof(uint64_t)},
        {alloc_probe_double(), sizeof(double), alignof(double)},
        {alloc_probe_pointer(), sizeof(void *), alignof(void *)},
        {alloc_probe_struct(), sizeof(alloc_probe_mixed), alignof(alloc_probe_mixed)},
        {alloc_probe_array(), sizeof(int32_t[3]), alignof(int32_t[3])},
    };
    for (const auto &c : cases) {
        EXPECT_EQ(c.layout.size, c.size);
        EXPECT_EQ(c.layout.align, c.align);
    }
}

TEST(AllocLayout, ArrayMultipliesTheSizeAndKeepsTheAlignment)
{
    el_layout out = kUntouched;
    ASSERT_EQ(el_layout_array(el_layout_make(12, 4), 10, &out), EL_OK);
    EXPECT_EQ(out.size, 120u);
    EXPECT_EQ(out.align, 4u);

    out = kUntouched;
    ASSERT_EQ(el_layout_array(el_layout_make(1, 1), 1, &out), EL_OK);
    EXPECT_EQ(out.size, 1u);
    EXPECT_EQ(out.align, 1u);
}

TEST(AllocLayout, ArrayOfNothingHasSizeZeroAndTheElementsAlignment)
{
    el_layout out = kUntouched;
    ASSERT_EQ(el_layout_array(el_layout_make(8, 8), 0, &out), EL_OK);
    EXPECT_EQ(out.size, 0u);
    EXPECT_EQ(out.align, 8u);

    out = kUntouched;
    ASSERT_EQ(el_layout_array(el_layout_make(0, 16), 1000, &out), EL_OK);
    EXPECT_EQ(out.size, 0u);
    EXPECT_EQ(out.align, 16u);

    /* Zero on either side wins over a huge other side: no overflow. */
    out = kUntouched;
    ASSERT_EQ(el_layout_array(el_layout_make(SIZE_MAX, 1), 0, &out), EL_OK);
    EXPECT_EQ(out.size, 0u);
    out = kUntouched;
    ASSERT_EQ(el_layout_array(el_layout_make(0, 1), SIZE_MAX, &out), EL_OK);
    EXPECT_EQ(out.size, 0u);
}

TEST(AllocLayout, ArrayAcceptsTheLargestProductAndRefusesOneMore)
{
    struct Case {
        size_t size;
        size_t count;
        bool fits;
    };
    const Case cases[] = {
        {1, SIZE_MAX, true},          {SIZE_MAX, 1, true},          {2, SIZE_MAX / 2, true},
        {2, SIZE_MAX / 2 + 1, false}, {3, SIZE_MAX / 3, true},      {3, SIZE_MAX / 3 + 1, false},
        {SIZE_MAX / 2, 2, true},      {SIZE_MAX / 2 + 1, 2, false}, {SIZE_MAX, 2, false},
        {2, SIZE_MAX, false},         {SIZE_MAX, SIZE_MAX, false},
    };
    for (const Case &c : cases) {
        SCOPED_TRACE(testing::Message() << c.count << " elements of " << c.size << " bytes");
        el_layout out = kUntouched;
        const el_status status = el_layout_array(el_layout_make(c.size, 2), c.count, &out);
        if (c.fits) {
            ASSERT_EQ(status, EL_OK) << el_status_name(status);
            EXPECT_EQ(out.size, c.size * c.count);
            EXPECT_EQ(out.align, 2u);
        } else {
            EXPECT_EQ(status, EL_ERR_NO_SPACE) << el_status_name(status);
            EXPECT_EQ(out.size, SIZE_MAX) << "*out on failure";
            EXPECT_EQ(out.align, 2u) << "*out on failure";
        }
    }
}

TEST(AllocLayout, TheLayoutWrittenOnOverflowIsRefusedByEveryArena)
{
    /* A caller that ignores the status gets NULL, not a short allocation. */
    el_layout out = el_layout_make(1, 1);
    ASSERT_EQ(el_layout_array(el_layout_make(2, 1), SIZE_MAX, &out), EL_ERR_NO_SPACE);

    el_bump_arena bump;
    el_bump_arena_init(&bump, storage, sizeof(storage));
    EXPECT_EQ(el_alloc(el_bump_arena_as_allocator(&bump), out), nullptr);

    el_stack_arena stack;
    el_stack_arena_init(&stack, storage, sizeof(storage));
    EXPECT_EQ(el_alloc(el_stack_arena_as_allocator(&stack), out), nullptr);
}

TEST(AllocLayout, ArrayOverflowAgreesWithTheCompiler)
{
    /* Operands around every boundary the split into halves has. */
    constexpr unsigned kHalfBits = sizeof(size_t) * CHAR_BIT / 2;
    constexpr size_t kHalf = size_t{1} << kHalfBits;
    const size_t values[] = {0,
                             1,
                             2,
                             3,
                             kHalf - 1,
                             kHalf,
                             kHalf + 1,
                             kHalf * 2,
                             kHalf * 3,
                             SIZE_MAX / 3,
                             SIZE_MAX / 2,
                             SIZE_MAX / 2 + 1,
                             SIZE_MAX - 1,
                             SIZE_MAX,
                             (kHalf - 1) * 3,
                             kHalf / 2,
                             kHalf / 2 + 1,
                             255};
    for (const size_t count : values) {
        for (const size_t size : values) {
            size_t product = 0;
            const bool overflows = __builtin_mul_overflow(count, size, &product);
            el_layout out = kUntouched;
            const el_status status = el_layout_array(el_layout_make(size, 8), count, &out);
            if (overflows) {
                EXPECT_EQ(status, EL_ERR_NO_SPACE) << count << " * " << size;
                EXPECT_EQ(out.size, SIZE_MAX) << count << " * " << size;
                EXPECT_EQ(out.align, 8u) << count << " * " << size;
            } else {
                ASSERT_EQ(status, EL_OK) << count << " * " << size;
                EXPECT_EQ(out.size, product) << count << " * " << size;
                EXPECT_EQ(out.align, 8u);
            }
        }
    }
}

/* ---- Bump arena --------------------------------------------------------- */

TEST(BumpArena, AllocatesInOrderWithOnlyTheNeededPadding)
{
    el_bump_arena arena;
    el_bump_arena_init(&arena, storage, sizeof(storage));
    const el_allocator a = el_bump_arena_as_allocator(&arena);

    uint8_t *const first = static_cast<uint8_t *>(el_alloc(a, el_layout_make(3, 1)));
    uint8_t *const second = static_cast<uint8_t *>(el_alloc(a, el_layout_make(8, 8)));
    uint8_t *const third = static_cast<uint8_t *>(el_alloc(a, el_layout_make(1, 1)));
    EXPECT_EQ(first, storage);
    EXPECT_EQ(second, storage + 8);
    EXPECT_EQ(third, storage + 16);
}

TEST(BumpArena, FillsItsStorageExactlyThenRefuses)
{
    el_bump_arena arena;
    el_bump_arena_init(&arena, storage, 100);
    const el_allocator a = el_bump_arena_as_allocator(&arena);

    EXPECT_EQ(el_alloc(a, el_layout_make(101, 1)), nullptr);
    EXPECT_NE(el_alloc(a, el_layout_make(99, 1)), nullptr);
    /* One byte left, but aligning to 2 needs a byte of padding first. */
    EXPECT_EQ(el_alloc(a, el_layout_make(1, 2)), nullptr);
    EXPECT_EQ(el_alloc(a, el_layout_make(1, 1)), storage + 99);
    EXPECT_EQ(el_alloc(a, el_layout_make(1, 1)), nullptr);
}

TEST(BumpArena, FreeingGivesNothingBackUntilReset)
{
    el_bump_arena arena;
    el_bump_arena_init(&arena, storage, 64);
    const el_allocator a = el_bump_arena_as_allocator(&arena);

    void *const block = el_alloc(a, el_layout_make(64, 1));
    ASSERT_NE(block, nullptr);
    el_free(a, block, el_layout_make(64, 1));
    EXPECT_EQ(el_alloc(a, el_layout_make(1, 1)), nullptr)
        << "a bump arena's free gave storage back";

    el_bump_arena_reset(&arena);
    EXPECT_EQ(el_alloc(a, el_layout_make(64, 1)), storage);
}

TEST(BumpArena, WithNoStorageRefusesEveryRequest)
{
    el_bump_arena arena;
    el_bump_arena_init(&arena, nullptr, 0);
    const el_allocator a = el_bump_arena_as_allocator(&arena);

    EXPECT_EQ(el_alloc(a, el_layout_make(1, 1)), nullptr);
    EXPECT_NE(el_alloc(a, el_layout_make(0, 1)), nullptr); /* zero size never asks the arena */
    el_bump_arena_reset(&arena);
}

TEST(BumpArena, PaddingIsAlignedToTheAddressNotTheOffset)
{
    /* Storage that starts one byte past an alignment boundary. */
    el_bump_arena arena;
    el_bump_arena_init(&arena, storage + 1, 64);
    const el_allocator a = el_bump_arena_as_allocator(&arena);

    EXPECT_EQ(el_alloc(a, el_layout_make(4, 4)), storage + 4);
    /* The storage ends at storage + 65: one byte fits at storage + 64, two do not. */
    EXPECT_EQ(el_alloc(a, el_layout_make(2, 64)), nullptr);
    EXPECT_EQ(el_alloc(a, el_layout_make(1, 64)), storage + 64);
}

/* ---- Stack arena -------------------------------------------------------- */

TEST(StackArena, FreeingTheLatestAllocationMovesTheTopBack)
{
    el_stack_arena arena;
    el_stack_arena_init(&arena, storage, sizeof(storage));
    const el_allocator a = el_stack_arena_as_allocator(&arena);

    void *const first = el_alloc(a, el_layout_make(16, 8));
    void *const second = el_alloc(a, el_layout_make(16, 8));
    el_free(a, second, el_layout_make(16, 8));
    EXPECT_EQ(el_alloc(a, el_layout_make(16, 8)), second);
    el_free(a, second, el_layout_make(16, 8));
    el_free(a, first, el_layout_make(16, 8));
    EXPECT_EQ(el_alloc(a, el_layout_make(32, 8)), first)
        << "frees in reverse order with no padding give all back";
}

TEST(StackArena, FreeingAnythingElseIsANoOp)
{
    el_stack_arena arena;
    el_stack_arena_init(&arena, storage, sizeof(storage));
    const el_allocator a = el_stack_arena_as_allocator(&arena);

    uint8_t *const first = static_cast<uint8_t *>(el_alloc(a, el_layout_make(16, 8)));
    uint8_t *const second = static_cast<uint8_t *>(el_alloc(a, el_layout_make(16, 8)));
    el_free(a, first, el_layout_make(16, 8));
    EXPECT_EQ(el_alloc(a, el_layout_make(8, 8)), second + 16)
        << "an out-of-order free gave storage back";
}

TEST(StackArena, PaddingBeforeAnAllocationIsNotGivenBack)
{
    el_stack_arena arena;
    el_stack_arena_init(&arena, storage, sizeof(storage));
    const el_allocator a = el_stack_arena_as_allocator(&arena);

    uint8_t *const small = static_cast<uint8_t *>(el_alloc(a, el_layout_make(1, 1)));
    uint8_t *const aligned = static_cast<uint8_t *>(el_alloc(a, el_layout_make(8, 8)));
    ASSERT_EQ(aligned, storage + 8);
    el_free(a, aligned, el_layout_make(8, 8));
    /* The top is back at storage + 8, not at storage + 1, so `small` no
     * longer ends at the top and its free is not recognized. */
    el_free(a, small, el_layout_make(1, 1));
    EXPECT_EQ(el_alloc(a, el_layout_make(1, 1)), storage + 8);
}

TEST(StackArena, ResetGivesEverythingBack)
{
    el_stack_arena arena;
    el_stack_arena_init(&arena, storage, 64);
    const el_allocator a = el_stack_arena_as_allocator(&arena);

    ASSERT_NE(el_alloc(a, el_layout_make(30, 1)), nullptr);
    ASSERT_NE(el_alloc(a, el_layout_make(30, 1)), nullptr);
    el_stack_arena_reset(&arena);
    EXPECT_EQ(el_alloc(a, el_layout_make(64, 1)), storage);
}

/* ---- Changing an allocation's size, in both arenas ---------------------- */

/*
 * Both arenas resize alike, so each check takes the handle of a fresh arena
 * of 64 bytes over `storage` and the test calls it for each arena.
 */

void CheckTheLatestAllocationGrowsInPlaceWhileThereIsRoom(el_allocator a)
{
    uint8_t *const first = static_cast<uint8_t *>(el_alloc(a, el_layout_make(8, 8)));
    uint8_t *const latest = static_cast<uint8_t *>(el_alloc(a, el_layout_make(16, 8)));
    ASSERT_EQ(first, storage);
    ASSERT_EQ(latest, storage + 8);
    for (size_t i = 0; i < 16; i++) {
        latest[i] = static_cast<uint8_t>(i + 1);
    }

    /* The operation itself, then through el_realloc: the same address. */
    ASSERT_NE(a.ops->realloc, nullptr);
    EXPECT_EQ(a.ops->realloc(a.ctx, latest, el_layout_make(16, 8), 40), latest);
    EXPECT_EQ(el_realloc(a, latest, el_layout_make(40, 8), 56), latest);
    for (size_t i = 0; i < 16; i++) {
        EXPECT_EQ(latest[i], i + 1) << "offset " << i;
    }
    /* 8 + 56 is the whole arena: one byte more is refused, and changes nothing. */
    EXPECT_EQ(a.ops->realloc(a.ctx, latest, el_layout_make(56, 8), 57), nullptr);
    EXPECT_EQ(el_realloc(a, latest, el_layout_make(56, 8), 57), nullptr);
    EXPECT_EQ(el_alloc(a, el_layout_make(1, 1)), nullptr) << "the grown allocation holds the rest";
    EXPECT_EQ(el_realloc(a, latest, el_layout_make(56, 8), 56), latest);
}

void CheckShrinkingTheLatestAllocationGivesTheTailBack(el_allocator a)
{
    uint8_t *const first = static_cast<uint8_t *>(el_alloc(a, el_layout_make(32, 8)));
    ASSERT_EQ(first, storage);
    EXPECT_EQ(el_realloc(a, first, el_layout_make(32, 8), 4), first);
    EXPECT_EQ(el_alloc(a, el_layout_make(60, 1)), storage + 4)
        << "the 28 bytes cut off the end were not given back";
    EXPECT_EQ(el_alloc(a, el_layout_make(1, 1)), nullptr);
}

void CheckAnEarlierAllocationShrinksInPlaceAndGivesNothingBack(el_allocator a)
{
    uint8_t *const first = static_cast<uint8_t *>(el_alloc(a, el_layout_make(16, 8)));
    ASSERT_NE(el_alloc(a, el_layout_make(16, 8)), nullptr);
    ASSERT_EQ(first, storage);
    for (size_t i = 0; i < 16; i++) {
        first[i] = static_cast<uint8_t>(i + 1);
    }

    EXPECT_EQ(el_realloc(a, first, el_layout_make(16, 8), 4), first);
    EXPECT_EQ(el_alloc(a, el_layout_make(1, 1)), storage + 32)
        << "shrinking an allocation below the latest gave storage back";

    /* It cannot grow where it is: the operation says so... */
    EXPECT_EQ(a.ops->realloc(a.ctx, first, el_layout_make(4, 8), 8), nullptr);
    /* ...and el_realloc moves it, keeping what it held. */
    uint8_t *const moved = static_cast<uint8_t *>(el_realloc(a, first, el_layout_make(4, 8), 24));
    ASSERT_EQ(moved, storage + 40);
    for (size_t i = 0; i < 4; i++) {
        EXPECT_EQ(moved[i], i + 1) << "offset " << i;
    }
}

void CheckTheLatestAllocationIsTheOneEndingAtTheTop(el_allocator a)
{
    /* The second allocation starts at 8, after 7 bytes of padding. */
    uint8_t *const first = static_cast<uint8_t *>(el_alloc(a, el_layout_make(1, 1)));
    uint8_t *const second = static_cast<uint8_t *>(el_alloc(a, el_layout_make(8, 8)));
    ASSERT_EQ(first, storage);
    ASSERT_EQ(second, storage + 8);

    /* The first one ends at 1, not at the top (16), though only padding follows it. */
    EXPECT_EQ(a.ops->realloc(a.ctx, first, el_layout_make(1, 1), 8), nullptr);
    EXPECT_EQ(a.ops->realloc(a.ctx, second, el_layout_make(8, 8), 56), second);
}

TEST(BumpArena, TheLatestAllocationGrowsInPlaceWhileThereIsRoom)
{
    el_bump_arena arena;
    el_bump_arena_init(&arena, storage, 64);
    CheckTheLatestAllocationGrowsInPlaceWhileThereIsRoom(el_bump_arena_as_allocator(&arena));
}

TEST(BumpArena, ShrinkingTheLatestAllocationGivesTheTailBack)
{
    el_bump_arena arena;
    el_bump_arena_init(&arena, storage, 64);
    CheckShrinkingTheLatestAllocationGivesTheTailBack(el_bump_arena_as_allocator(&arena));
}

TEST(BumpArena, AnEarlierAllocationShrinksInPlaceAndGivesNothingBack)
{
    el_bump_arena arena;
    el_bump_arena_init(&arena, storage, 64);
    CheckAnEarlierAllocationShrinksInPlaceAndGivesNothingBack(el_bump_arena_as_allocator(&arena));
}

TEST(BumpArena, TheLatestAllocationIsTheOneEndingAtTheTop)
{
    el_bump_arena arena;
    el_bump_arena_init(&arena, storage, 64);
    CheckTheLatestAllocationIsTheOneEndingAtTheTop(el_bump_arena_as_allocator(&arena));
}

TEST(StackArena, TheLatestAllocationGrowsInPlaceWhileThereIsRoom)
{
    el_stack_arena arena;
    el_stack_arena_init(&arena, storage, 64);
    CheckTheLatestAllocationGrowsInPlaceWhileThereIsRoom(el_stack_arena_as_allocator(&arena));
}

TEST(StackArena, ShrinkingTheLatestAllocationGivesTheTailBack)
{
    el_stack_arena arena;
    el_stack_arena_init(&arena, storage, 64);
    CheckShrinkingTheLatestAllocationGivesTheTailBack(el_stack_arena_as_allocator(&arena));
}

TEST(StackArena, AnEarlierAllocationShrinksInPlaceAndGivesNothingBack)
{
    el_stack_arena arena;
    el_stack_arena_init(&arena, storage, 64);
    CheckAnEarlierAllocationShrinksInPlaceAndGivesNothingBack(el_stack_arena_as_allocator(&arena));
}

TEST(StackArena, TheLatestAllocationIsTheOneEndingAtTheTop)
{
    el_stack_arena arena;
    el_stack_arena_init(&arena, storage, 64);
    CheckTheLatestAllocationIsTheOneEndingAtTheTop(el_stack_arena_as_allocator(&arena));
}

TEST(StackArena, AShrunkLatestAllocationIsFreedWithItsNewSize)
{
    el_stack_arena arena;
    el_stack_arena_init(&arena, storage, 64);
    const el_allocator a = el_stack_arena_as_allocator(&arena);

    void *const block = el_alloc(a, el_layout_make(40, 8));
    ASSERT_EQ(el_realloc(a, block, el_layout_make(40, 8), 10), block);
    el_free(a, block, el_layout_make(10, 8));
    EXPECT_EQ(el_alloc(a, el_layout_make(64, 1)), storage) << "the free did not reach the bottom";
}

TEST(StackArena, AnAllocationShrunkBelowTheLatestKeepsItsOldEndAndIsNotGivenBackInReverseOrder)
{
    el_stack_arena arena;
    el_stack_arena_init(&arena, storage, 64);
    const el_allocator a = el_stack_arena_as_allocator(&arena);

    void *const first = el_alloc(a, el_layout_make(16, 8));
    void *const second = el_alloc(a, el_layout_make(16, 8));
    ASSERT_EQ(el_realloc(a, first, el_layout_make(16, 8), 8), first);

    el_free(a, second, el_layout_make(16, 8));
    EXPECT_EQ(el_alloc(a, el_layout_make(1, 1)), storage + 16);
    el_free(a, storage + 16, el_layout_make(1, 1));
    /* `first` now ends at 8 but the top is at 16: its free is not recognized,
     * though it is the last one freed in reverse order. */
    el_free(a, first, el_layout_make(8, 8));
    EXPECT_EQ(el_alloc(a, el_layout_make(48, 1)), storage + 16);
}
} // namespace
