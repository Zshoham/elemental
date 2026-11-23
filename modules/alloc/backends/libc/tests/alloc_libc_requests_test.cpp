/*
 * What the C library backend asks of the C library: a block below its
 * alignment is asked for at least the alignment in bytes, so that a heap
 * that aligns small blocks only as far as their size needs (a
 * weak-alignment heap, WG14 N2293) still returns it aligned. glibc aligns
 * every block to 16, so the result alone cannot show it on this host; the
 * size the backend asks for can.
 */
#include <gtest/gtest.h>

#include <elemental/alloc_libc.h>

#include <cstddef>
#include <cstdint>

#include "heap_requests.h"

namespace
{

constexpr size_t kAlign = alignof(std::max_align_t);

TEST(AllocLibcRequests, ABlockSmallerThanItsAlignmentIsAskedForAtLeastTheAlignment)
{
    const el_allocator a = el_libc_allocator();

    heap_requests_malloc_size = 0;
    void *const block = el_alloc(a, el_layout_make(1, kAlign));
    ASSERT_NE(block, nullptr);
    EXPECT_GE(heap_requests_malloc_size, kAlign);
    EXPECT_EQ(reinterpret_cast<uintptr_t>(block) % kAlign, 0u);
    el_free(a, block, el_layout_make(1, kAlign));

    /* A block at least as large as its alignment is asked for as it is. */
    heap_requests_malloc_size = 0;
    void *const large = el_alloc(a, el_layout_make(3 * kAlign + 1, kAlign));
    ASSERT_NE(large, nullptr);
    EXPECT_EQ(heap_requests_malloc_size, 3 * kAlign + 1);
    el_free(a, large, el_layout_make(3 * kAlign + 1, kAlign));
}

TEST(AllocLibcRequests, ABlockShrunkBelowItsAlignmentIsAskedForAtLeastTheAlignment)
{
    const el_allocator a = el_libc_allocator();
    void *const block = el_alloc(a, el_layout_make(64, kAlign));
    ASSERT_NE(block, nullptr);

    heap_requests_realloc_size = 0;
    void *const shrunk = el_realloc(a, block, el_layout_make(64, kAlign), 1);
    ASSERT_NE(shrunk, nullptr);
    EXPECT_GE(heap_requests_realloc_size, kAlign);
    EXPECT_EQ(reinterpret_cast<uintptr_t>(shrunk) % kAlign, 0u);

    heap_requests_realloc_size = 0;
    void *const grown = el_realloc(a, shrunk, el_layout_make(1, kAlign), 100);
    ASSERT_NE(grown, nullptr);
    EXPECT_EQ(heap_requests_realloc_size, 100u);
    el_free(a, grown, el_layout_make(100, kAlign));
}

} // namespace
