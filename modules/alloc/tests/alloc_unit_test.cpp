/*
 * The front functions and the two arenas: what each promises beyond the
 * contract suite. Exact placement, padding, the difference between the
 * arenas' frees, and the overflow check against the compiler's own.
 */
#include <gtest/gtest.h>

#include <elemental/alloc.h>
#include <elemental/alloc_arena.h>

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
        EXPECT_EQ(Address(el_alloc(a, 0, align)), ~uintptr_t{0} & ~(uintptr_t{align} - 1));
    }
    /* Nothing came from the arena. */
    EXPECT_NE(el_alloc(a, sizeof(storage), 1), nullptr);
}

TEST(AllocFront, ArrayOverflowAgreesWithTheCompiler)
{
    el_bump_arena arena;
    el_bump_arena_init(&arena, nullptr, 0);
    const el_allocator a = el_bump_arena_as_allocator(&arena);

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
            /* The arena has no storage, so anything that does not overflow
             * and is not zero is refused by the arena instead; either way the
             * answer is null. What distinguishes them is a zero product. */
            void *const ptr = el_alloc_array(a, count, size, 1);
            if (!overflows && product == 0) {
                EXPECT_NE(ptr, nullptr) << count << " * " << size;
            } else {
                EXPECT_EQ(ptr, nullptr) << count << " * " << size;
            }
        }
    }
}

TEST(AllocFront, ArrayAllocatesExactlyTheProduct)
{
    el_bump_arena arena;
    el_bump_arena_init(&arena, storage, sizeof(storage));
    const el_allocator a = el_bump_arena_as_allocator(&arena);

    /* Exactly the storage: 16 * 32 fits; one more byte would not. */
    EXPECT_NE(el_alloc_array(a, 16, 32, 1), nullptr);
    EXPECT_EQ(el_alloc(a, 1, 1), nullptr);
}

/* ---- Bump arena --------------------------------------------------------- */

TEST(BumpArena, AllocatesInOrderWithOnlyTheNeededPadding)
{
    el_bump_arena arena;
    el_bump_arena_init(&arena, storage, sizeof(storage));
    const el_allocator a = el_bump_arena_as_allocator(&arena);

    uint8_t *const first = static_cast<uint8_t *>(el_alloc(a, 3, 1));
    uint8_t *const second = static_cast<uint8_t *>(el_alloc(a, 8, 8));
    uint8_t *const third = static_cast<uint8_t *>(el_alloc(a, 1, 1));
    EXPECT_EQ(first, storage);
    EXPECT_EQ(second, storage + 8);
    EXPECT_EQ(third, storage + 16);
}

TEST(BumpArena, FillsItsStorageExactlyThenRefuses)
{
    el_bump_arena arena;
    el_bump_arena_init(&arena, storage, 100);
    const el_allocator a = el_bump_arena_as_allocator(&arena);

    EXPECT_EQ(el_alloc(a, 101, 1), nullptr);
    EXPECT_NE(el_alloc(a, 99, 1), nullptr);
    /* One byte left, but aligning to 2 needs a byte of padding first. */
    EXPECT_EQ(el_alloc(a, 1, 2), nullptr);
    EXPECT_EQ(el_alloc(a, 1, 1), storage + 99);
    EXPECT_EQ(el_alloc(a, 1, 1), nullptr);
}

TEST(BumpArena, FreeingGivesNothingBackUntilReset)
{
    el_bump_arena arena;
    el_bump_arena_init(&arena, storage, 64);
    const el_allocator a = el_bump_arena_as_allocator(&arena);

    void *const block = el_alloc(a, 64, 1);
    ASSERT_NE(block, nullptr);
    el_free(a, block, 64, 1);
    EXPECT_EQ(el_alloc(a, 1, 1), nullptr) << "a bump arena's free gave storage back";

    el_bump_arena_reset(&arena);
    EXPECT_EQ(el_alloc(a, 64, 1), storage);
}

TEST(BumpArena, WithNoStorageRefusesEveryRequest)
{
    el_bump_arena arena;
    el_bump_arena_init(&arena, nullptr, 0);
    const el_allocator a = el_bump_arena_as_allocator(&arena);

    EXPECT_EQ(el_alloc(a, 1, 1), nullptr);
    EXPECT_NE(el_alloc(a, 0, 1), nullptr); /* zero size never asks the arena */
    el_bump_arena_reset(&arena);
}

TEST(BumpArena, PaddingIsAlignedToTheAddressNotTheOffset)
{
    /* Storage that starts one byte past an alignment boundary. */
    el_bump_arena arena;
    el_bump_arena_init(&arena, storage + 1, 64);
    const el_allocator a = el_bump_arena_as_allocator(&arena);

    EXPECT_EQ(el_alloc(a, 4, 4), storage + 4);
    /* The storage ends at storage + 65: one byte fits at storage + 64, two do not. */
    EXPECT_EQ(el_alloc(a, 2, 64), nullptr);
    EXPECT_EQ(el_alloc(a, 1, 64), storage + 64);
}

/* ---- Stack arena -------------------------------------------------------- */

TEST(StackArena, FreeingTheLatestAllocationMovesTheTopBack)
{
    el_stack_arena arena;
    el_stack_arena_init(&arena, storage, sizeof(storage));
    const el_allocator a = el_stack_arena_as_allocator(&arena);

    void *const first = el_alloc(a, 16, 8);
    void *const second = el_alloc(a, 16, 8);
    el_free(a, second, 16, 8);
    EXPECT_EQ(el_alloc(a, 16, 8), second);
    el_free(a, second, 16, 8);
    el_free(a, first, 16, 8);
    EXPECT_EQ(el_alloc(a, 32, 8), first) << "frees in reverse order with no padding give all back";
}

TEST(StackArena, FreeingAnythingElseIsANoOp)
{
    el_stack_arena arena;
    el_stack_arena_init(&arena, storage, sizeof(storage));
    const el_allocator a = el_stack_arena_as_allocator(&arena);

    uint8_t *const first = static_cast<uint8_t *>(el_alloc(a, 16, 8));
    uint8_t *const second = static_cast<uint8_t *>(el_alloc(a, 16, 8));
    el_free(a, first, 16, 8);
    EXPECT_EQ(el_alloc(a, 8, 8), second + 16) << "an out-of-order free gave storage back";
}

TEST(StackArena, PaddingBeforeAnAllocationIsNotGivenBack)
{
    el_stack_arena arena;
    el_stack_arena_init(&arena, storage, sizeof(storage));
    const el_allocator a = el_stack_arena_as_allocator(&arena);

    uint8_t *const small = static_cast<uint8_t *>(el_alloc(a, 1, 1));
    uint8_t *const aligned = static_cast<uint8_t *>(el_alloc(a, 8, 8));
    ASSERT_EQ(aligned, storage + 8);
    el_free(a, aligned, 8, 8);
    /* The top is back at storage + 8, not at storage + 1, so `small` no
     * longer ends at the top and its free is not recognized. */
    el_free(a, small, 1, 1);
    EXPECT_EQ(el_alloc(a, 1, 1), storage + 8);
}

TEST(StackArena, ResetGivesEverythingBack)
{
    el_stack_arena arena;
    el_stack_arena_init(&arena, storage, 64);
    const el_allocator a = el_stack_arena_as_allocator(&arena);

    ASSERT_NE(el_alloc(a, 30, 1), nullptr);
    ASSERT_NE(el_alloc(a, 30, 1), nullptr);
    el_stack_arena_reset(&arena);
    EXPECT_EQ(el_alloc(a, 64, 1), storage);
}

} // namespace
