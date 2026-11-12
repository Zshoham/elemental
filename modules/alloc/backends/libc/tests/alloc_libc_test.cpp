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

} // namespace
