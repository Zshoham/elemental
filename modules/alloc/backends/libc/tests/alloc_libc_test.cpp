/*
 * What only the C library backend promises: alignments beyond
 * max_align_t, the overflow when a size is rounded up for one, and that the
 * handle carries no state.
 */
#include <gtest/gtest.h>

#include <elemental/alloc_libc.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace
{

TEST(AllocLibc, EveryCallReturnsTheSameStatelessHandle)
{
    const el_allocator first = el_libc_allocator();
    const el_allocator second = el_libc_allocator();
    EXPECT_EQ(first.ctx, nullptr);
    EXPECT_EQ(first.ops, second.ops);
}

TEST(AllocLibc, AlignsBeyondMaxAlignT)
{
    const el_allocator a = el_libc_allocator();
    for (size_t align = alignof(std::max_align_t) * 2; align <= 65536; align *= 2) {
        for (const size_t size : {size_t{1}, align - 1, align, align + 1}) {
            uint8_t *const ptr = static_cast<uint8_t *>(el_alloc(a, el_layout_make(size, align)));
            ASSERT_NE(ptr, nullptr) << size << " bytes aligned to " << align;
            EXPECT_EQ(reinterpret_cast<uintptr_t>(ptr) % align, 0u);
            std::memset(ptr, 0xA5, size);
            el_free(a, ptr, el_layout_make(size, align));
        }
    }
}

TEST(AllocLibc, RoundingASizeUpForAlignmentCannotOverflow)
{
    const el_allocator a = el_libc_allocator();
    /* SIZE_MAX - 100 rounded up to 4096 does not fit in a size_t. */
    EXPECT_EQ(el_alloc(a, el_layout_make(SIZE_MAX - 100, 4096)), nullptr);
    EXPECT_EQ(el_alloc(a, el_layout_make(SIZE_MAX, 128)), nullptr);
}

TEST(AllocLibc, TheReallocOperationAnswersUpToMaxAlignTAndNullAbove)
{
    const el_allocator a = el_libc_allocator();
    ASSERT_NE(a.ops->realloc, nullptr);

    const size_t small = alignof(std::max_align_t);
    void *const block = el_alloc(a, el_layout_make(16, small));
    ASSERT_NE(block, nullptr);
    void *const grown = a.ops->realloc(a.ctx, block, el_layout_make(16, small), 1 << 16);
    ASSERT_NE(grown, nullptr);
    EXPECT_EQ(reinterpret_cast<uintptr_t>(grown) % small, 0u);
    el_free(a, grown, el_layout_make(1 << 16, small));

    /* C has no aligned realloc(): the operation leaves it to el_realloc. */
    const size_t big = small * 2;
    void *const aligned = el_alloc(a, el_layout_make(16, big));
    ASSERT_NE(aligned, nullptr);
    EXPECT_EQ(a.ops->realloc(a.ctx, aligned, el_layout_make(16, big), 64), nullptr);
    el_free(a, aligned, el_layout_make(16, big));
}

TEST(AllocLibc, AnAlignmentBeyondMaxAlignTIsResizedByMovingItAndKeepsItsAlignment)
{
    const el_allocator a = el_libc_allocator();
    for (size_t align = alignof(std::max_align_t) * 2; align <= 4096; align *= 2) {
        uint8_t *block = static_cast<uint8_t *>(el_alloc(a, el_layout_make(24, align)));
        ASSERT_NE(block, nullptr);
        for (size_t i = 0; i < 24; i++) {
            block[i] = static_cast<uint8_t>(i + 1);
        }
        uint8_t *const grown =
            static_cast<uint8_t *>(el_realloc(a, block, el_layout_make(24, align), 1000));
        ASSERT_NE(grown, nullptr) << "alignment " << align;
        EXPECT_EQ(reinterpret_cast<uintptr_t>(grown) % align, 0u) << "alignment " << align;
        for (size_t i = 0; i < 24; i++) {
            ASSERT_EQ(grown[i], i + 1) << "offset " << i << ", alignment " << align;
        }
        el_free(a, grown, el_layout_make(1000, align));
    }
}

TEST(AllocLibc, AFailedReallocKeepsTheBlock)
{
    const el_allocator a = el_libc_allocator();
    uint8_t *const block = static_cast<uint8_t *>(el_alloc(a, el_layout_make(64, 8)));
    ASSERT_NE(block, nullptr);
    std::memset(block, 0xA5, 64);
    /* C's realloc() keeps the block when it fails, as the operation must. */
    EXPECT_EQ(a.ops->realloc(a.ctx, block, el_layout_make(64, 8), SIZE_MAX), nullptr);
    EXPECT_EQ(el_realloc(a, block, el_layout_make(64, 8), SIZE_MAX), nullptr);
    for (size_t i = 0; i < 64; i++) {
        ASSERT_EQ(block[i], 0xA5) << "offset " << i;
    }
    el_free(a, block, el_layout_make(64, 8));
}

} // namespace
