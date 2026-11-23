/*
 * What only the size_classes example promises: which class serves a
 * request, that a freed block is reused by its class and no other, and
 * that free finds the class from the size and alignment alone.
 */
#include <gtest/gtest.h>

#include "size_classes.h"

#include <cstdint>

namespace
{

alignas(256) uint8_t storage[1024];

class SizeClasses : public testing::Test
{
  protected:
    void SetUp() override
    {
        size_classes_init(&sc, storage, sizeof(storage));
        a = size_classes_as_allocator(&sc);
    }

    size_classes sc;
    el_allocator a;
};

TEST_F(SizeClasses, BlocksAreAlignedToTheirClass)
{
    EXPECT_EQ(el_alloc(a, el_layout_make(1, 1)), storage);         /* 16 */
    EXPECT_EQ(el_alloc(a, el_layout_make(17, 1)), storage + 32);   /* 32, after padding */
    EXPECT_EQ(el_alloc(a, el_layout_make(8, 64)), storage + 64);   /* alignment picks 64 */
    EXPECT_EQ(el_alloc(a, el_layout_make(129, 1)), storage + 256); /* 256 */
}

TEST_F(SizeClasses, AFreedBlockIsReusedByItsClassOnly)
{
    void *const small = el_alloc(a, el_layout_make(10, 8));
    el_free(a, small, el_layout_make(10, 8));
    void *const larger = el_alloc(a, el_layout_make(20, 8));
    EXPECT_NE(larger, small) << "a 16-byte block served a 32-byte class";
    EXPECT_EQ(el_alloc(a, el_layout_make(16, 16)), small)
        << "any request of the same class reuses it";
}

TEST_F(SizeClasses, FreeFindsTheClassFromSizeAndAlignment)
{
    /* Same size, different alignment: different classes, different lists. */
    void *const by_size = el_alloc(a, el_layout_make(8, 8));    /* class 16 */
    void *const by_align = el_alloc(a, el_layout_make(8, 128)); /* class 128 */
    el_free(a, by_align, el_layout_make(8, 128));
    el_free(a, by_size, el_layout_make(8, 8));
    EXPECT_EQ(el_alloc(a, el_layout_make(100, 1)), by_align);
    EXPECT_EQ(el_alloc(a, el_layout_make(1, 1)), by_size);
}

TEST_F(SizeClasses, RefusesWhatNoClassHolds)
{
    EXPECT_EQ(el_alloc(a, el_layout_make(257, 1)), nullptr);
    EXPECT_EQ(el_alloc(a, el_layout_make(1, 512)), nullptr);
}

TEST_F(SizeClasses, ResizingWithinTheClassKeepsTheBlockWhereItIs)
{
    void *const block = el_alloc(a, el_layout_make(10, 8)); /* class 16 */
    EXPECT_EQ(el_realloc(a, block, el_layout_make(10, 8), 16), block);
    EXPECT_EQ(el_realloc(a, block, el_layout_make(16, 8), 1), block);
    EXPECT_EQ(a.ops->realloc(a.ctx, block, el_layout_make(1, 8), 15), block);
}

TEST_F(SizeClasses, ResizingToAnotherClassIsLeftToElRealloc)
{
    uint8_t *const block = static_cast<uint8_t *>(el_alloc(a, el_layout_make(10, 8)));
    for (uint8_t i = 0; i < 10; i++) {
        block[i] = i + 1;
    }
    /* The operation does not move blocks between classes... */
    EXPECT_EQ(a.ops->realloc(a.ctx, block, el_layout_make(10, 8), 17), nullptr);
    /* ...so el_realloc does, and the block it leaves is its old class's. */
    uint8_t *const moved = static_cast<uint8_t *>(el_realloc(a, block, el_layout_make(10, 8), 17));
    ASSERT_NE(moved, nullptr);
    EXPECT_NE(moved, block);
    for (uint8_t i = 0; i < 10; i++) {
        EXPECT_EQ(moved[i], i + 1);
    }
    EXPECT_EQ(el_alloc(a, el_layout_make(16, 1)), block);

    /* Past the largest class: refused, and the block is as it was. */
    EXPECT_EQ(el_realloc(a, moved, el_layout_make(17, 8), 257), nullptr);
    EXPECT_EQ(moved[9], 10);
}

TEST_F(SizeClasses, AResizedBlockIsFreedIntoItsNewClass)
{
    void *const block = el_alloc(a, el_layout_make(10, 8));
    void *const moved = el_realloc(a, block, el_layout_make(10, 8), 100); /* class 128 */
    ASSERT_NE(moved, nullptr);
    el_free(a, moved, el_layout_make(100, 8));
    EXPECT_EQ(el_alloc(a, el_layout_make(128, 1)), moved);
}

} // namespace
