/*
 * What only el_grow_buf promises: how it grows, when it reports
 * EL_ERR_NO_SPACE, that it recovers, and that it frees exactly the storage
 * it allocated, with the sizes it allocated it with. Every allocator here is
 * wrapped in the checking allocator.
 */
#include <gtest/gtest.h>

#include <elemental/alloc_arena.h>
#include <elemental/alloc_contract.h>
#include <elemental/io_alloc.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace
{

class GrowBuf : public testing::Test
{
  protected:
    /* A buffer over a bump arena of `cap` bytes, checked. */
    void Open(size_t cap)
    {
        el_bump_arena_init(&arena, storage, cap);
        el_alloc_checker_init(&checker, el_bump_arena_as_allocator(&arena), records, 8);
        b = el_grow_buf_init(&g, el_alloc_checker_as_allocator(&checker));
        w = el_buf_as_writer(b);
    }

    void TearDown() override
    {
        el_grow_buf_deinit(&g);
        EXPECT_EQ(el_alloc_checker_live(&checker), 0u) << "the buffer left a block behind";
    }

    std::vector<uint8_t> Contents() const
    {
        const uint8_t *const data = el_buf_data(b);
        return std::vector<uint8_t>(data, data + el_buf_len(b));
    }

    alignas(16) uint8_t storage[1024];
    el_bump_arena arena;
    el_alloc_check_record records[8];
    el_alloc_checker checker;
    el_grow_buf g;
    el_buf *b = nullptr;
    el_writer w;
    size_t n = 0;
};

TEST_F(GrowBuf, StartsEmptyWithoutAllocating)
{
    Open(sizeof(storage));
    EXPECT_EQ(b, &g.buf);
    EXPECT_EQ(el_buf_len(b), 0u);
    EXPECT_EQ(el_buf_room(b), 0u);
    EXPECT_EQ(el_buf_data(b), nullptr);
    EXPECT_EQ(el_buf_spare(b), nullptr);
    EXPECT_EQ(el_alloc_checker_live(&checker), 0u);
}

TEST_F(GrowBuf, GrowsToSixteenThenDoubles)
{
    Open(sizeof(storage));
    const uint8_t byte = 'x';
    for (int i = 0; i < 40; i++) {
        ASSERT_EQ(el_buf_append(b, &byte, 1), EL_OK);
    }
    /* 16, then 32, then 64: each extends the arena's latest allocation. */
    EXPECT_EQ(el_buf_len(b) + el_buf_room(b), 64u);
    EXPECT_EQ(arena.used, 64u);
    EXPECT_EQ(Contents(), std::vector<uint8_t>(40, 'x'));
}

TEST_F(GrowBuf, GrowsStraightToWhatALargeAppendNeeds)
{
    Open(sizeof(storage));
    uint8_t data[300];
    std::memset(data, 7, sizeof(data));
    ASSERT_EQ(el_buf_append(b, data, sizeof(data)), EL_OK);
    EXPECT_EQ(el_buf_room(b), 0u);
    EXPECT_EQ(Contents(), std::vector<uint8_t>(data, data + sizeof(data)));
}

TEST_F(GrowBuf, ReserveGrowsAndSpareIsThenWritable)
{
    Open(sizeof(storage));
    ASSERT_EQ(el_buf_reserve(b, 5), EL_OK);
    EXPECT_GE(el_buf_room(b), 5u);
    std::memcpy(el_buf_spare(b), "hello", 5);
    el_buf_commit(b, 5);
    EXPECT_EQ(Contents(), std::vector<uint8_t>({'h', 'e', 'l', 'l', 'o'}));
}

TEST_F(GrowBuf, AsksForExactlyWhatIsNeededWhenDoublingIsRefused)
{
    /* 100 bytes in, then 50 more: doubling to 200 does not fit the 180-byte
     * arena, but exactly 150 does. */
    Open(180);
    uint8_t data[100] = {};
    ASSERT_EQ(el_buf_append(b, data, 100), EL_OK);
    ASSERT_EQ(el_buf_append(b, data, 50), EL_OK);
    EXPECT_EQ(el_buf_len(b), 150u);
    EXPECT_EQ(el_buf_room(b), 0u);
}

TEST_F(GrowBuf, GrowsInPlaceWhileItIsTheArenasLatestAllocation)
{
    Open(100);
    /* Six ten-byte fields in a 100-byte arena (the grown buffer's example in
     * docs/proposals/seams.md): the buffer is the arena's latest allocation,
     * so each growth, to 16, 32 and 64, extends it where it is. */
    for (int field = 0; field < 6; field++) {
        ASSERT_EQ(el_buf_append(b, "0123456789", 10), EL_OK) << "field " << field;
    }
    EXPECT_EQ(el_buf_len(b), 60u);
    EXPECT_EQ(arena.used, 64u) << "the old blocks were left behind in the arena";
    EXPECT_EQ(el_buf_data(b), storage);
}

TEST_F(GrowBuf, MovesTheContentsWhenSomethingWasAllocatedAfterIt)
{
    Open(sizeof(storage));
    ASSERT_EQ(el_buf_append(b, "0123456789", 10), EL_OK); /* 16 bytes, at the start */
    ASSERT_NE(el_alloc(el_bump_arena_as_allocator(&arena), el_layout_make(8, 1)), nullptr);

    /* Growing to 32 cannot extend the buffer in place any more. */
    ASSERT_EQ(el_buf_append(b, "abcdefghij", 10), EL_OK);
    EXPECT_NE(el_buf_data(b), storage);
    EXPECT_EQ(arena.used, 16u + 8u + 32u);
    EXPECT_EQ(Contents(), std::vector<uint8_t>({'0', '1', '2', '3', '4', '5', '6', '7', '8', '9',
                                                'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j'}));
}

TEST_F(GrowBuf, AppendIsWholeOrNothingWhenTheAllocatorRefuses)
{
    Open(20);
    uint8_t data[32] = {};
    ASSERT_EQ(el_buf_append(b, data, 10), EL_OK); /* 16 allocated, 4 left */
    EXPECT_EQ(el_buf_append(b, data, 14), EL_ERR_NO_SPACE);
    EXPECT_EQ(el_buf_len(b), 10u);
    EXPECT_EQ(el_buf_reserve(b, 11), EL_ERR_NO_SPACE);
    EXPECT_EQ(el_buf_room(b), 6u);
}

TEST_F(GrowBuf, AsWriterTakesWhatFitsThenReportsNoSpace)
{
    Open(16);
    uint8_t data[64];
    for (size_t i = 0; i < sizeof(data); i++) {
        data[i] = static_cast<uint8_t>(i);
    }
    /* The first write gets 16 bytes, which is all the arena has. Growing
     * fails, so the second write takes the 6 bytes of room left, and then
     * even one more does not fit. */
    ASSERT_EQ(el_write(w, data, 10, &n), EL_OK);
    ASSERT_EQ(el_write(w, data + 10, 14, &n), EL_OK);
    EXPECT_EQ(n, 6u);
    EXPECT_EQ(el_write(w, data + 16, 1, &n), EL_ERR_NO_SPACE);
    EXPECT_EQ(n, 0u);

    EXPECT_EQ(el_write_all(w, data + 16, 4, &n), EL_ERR_NO_SPACE);
    EXPECT_EQ(n, 0u);
    EXPECT_EQ(Contents(), std::vector<uint8_t>(data, data + 16));
}

TEST_F(GrowBuf, NoSpaceIsNotPermanent)
{
    Open(16);
    uint8_t data[32] = {};
    ASSERT_EQ(el_buf_append(b, data, 16), EL_OK);
    ASSERT_EQ(el_buf_append(b, data, 1), EL_ERR_NO_SPACE);

    /* The arena gets more room, as if something else had freed. */
    arena.cap = sizeof(storage);
    EXPECT_EQ(el_buf_append(b, data, 1), EL_OK);
    EXPECT_EQ(el_buf_len(b), 17u);
}

TEST_F(GrowBuf, AnAllocatorThatRefusesEverythingReportsNoSpace)
{
    Open(0);
    const uint8_t byte = 1;
    EXPECT_EQ(el_buf_append(b, &byte, 1), EL_ERR_NO_SPACE);
    EXPECT_EQ(el_write(w, &byte, 1, &n), EL_ERR_NO_SPACE);
    EXPECT_EQ(n, 0u);
    EXPECT_EQ(el_buf_len(b), 0u);
}

TEST_F(GrowBuf, AnOverflowingRequestIsNoSpaceAndAsksNothing)
{
    Open(sizeof(storage));
    ASSERT_EQ(el_buf_append(b, "ab", 2), EL_OK);
    const size_t used = arena.used;
    EXPECT_EQ(el_buf_reserve(b, SIZE_MAX - 1), EL_ERR_NO_SPACE);
    EXPECT_EQ(arena.used, used);
    EXPECT_EQ(el_buf_len(b), 2u);
}

TEST_F(GrowBuf, DeinitLeavesAnAlwaysFullBufferThenInitStartsAgain)
{
    Open(sizeof(storage));
    ASSERT_EQ(el_buf_append(b, "x", 1), EL_OK);
    el_grow_buf_deinit(&g);
    EXPECT_EQ(el_alloc_checker_live(&checker), 0u);
    EXPECT_EQ(el_buf_len(b), 0u);
    EXPECT_EQ(el_buf_room(b), 0u);
    EXPECT_EQ(el_buf_append(b, "x", 1), EL_ERR_NO_SPACE);
    EXPECT_EQ(el_write(w, "x", 1, &n), EL_ERR_NO_SPACE);
    EXPECT_EQ(el_alloc_checker_live(&checker), 0u);

    b = el_grow_buf_init(&g, el_alloc_checker_as_allocator(&checker));
    EXPECT_EQ(el_buf_len(b), 0u);
    EXPECT_EQ(el_buf_append(b, "y", 1), EL_OK);
}

} // namespace
