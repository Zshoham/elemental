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
    EXPECT_EQ(el_alloc(a, 1, 1), storage);         /* 16 */
    EXPECT_EQ(el_alloc(a, 17, 1), storage + 32);   /* 32, after padding */
    EXPECT_EQ(el_alloc(a, 8, 64), storage + 64);   /* alignment picks 64 */
    EXPECT_EQ(el_alloc(a, 129, 1), storage + 256); /* 256 */
}

TEST_F(SizeClasses, AFreedBlockIsReusedByItsClassOnly)
{
    void *const small = el_alloc(a, 10, 8);
    el_free(a, small, 10, 8);
    void *const larger = el_alloc(a, 20, 8);
    EXPECT_NE(larger, small) << "a 16-byte block served a 32-byte class";
    EXPECT_EQ(el_alloc(a, 16, 16), small) << "any request of the same class reuses it";
}

TEST_F(SizeClasses, FreeFindsTheClassFromSizeAndAlignment)
{
    /* Same size, different alignment: different classes, different lists. */
    void *const by_size = el_alloc(a, 8, 8);    /* class 16 */
    void *const by_align = el_alloc(a, 8, 128); /* class 128 */
    el_free(a, by_align, 8, 128);
    el_free(a, by_size, 8, 8);
    EXPECT_EQ(el_alloc(a, 100, 1), by_align);
    EXPECT_EQ(el_alloc(a, 1, 1), by_size);
}

TEST_F(SizeClasses, RefusesWhatNoClassHolds)
{
    EXPECT_EQ(el_alloc(a, 257, 1), nullptr);
    EXPECT_EQ(el_alloc(a, 1, 512), nullptr);
}

} // namespace
