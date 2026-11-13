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
    /* 16, then 32, then 64: the arena's free gives nothing back. */
    EXPECT_EQ(el_buf_len(b) + el_buf_room(b), 64u);
    EXPECT_EQ(arena.used, 16u + 32u + 64u);
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
    /* 100 bytes in, then 50 more: doubling to 200 does not fit beside the
     * first block (100 + 200 > 260), but exactly 150 does. */
    Open(260);
    uint8_t data[100] = {};
    ASSERT_EQ(el_buf_append(b, data, 100), EL_OK);
    ASSERT_EQ(el_buf_append(b, data, 50), EL_OK);
    EXPECT_EQ(el_buf_len(b), 150u);
    EXPECT_EQ(el_buf_room(b), 0u);
}

TEST_F(GrowBuf, AppendIsWholeOrNothingWhenTheAllocatorRefuses)
{
    Open(30);
    uint8_t data[32] = {};
    ASSERT_EQ(el_buf_append(b, data, 10), EL_OK); /* 16 allocated, 14 left */
    EXPECT_EQ(el_buf_append(b, data, 14), EL_ERR_NO_SPACE);
    EXPECT_EQ(el_buf_len(b), 10u);
    EXPECT_EQ(el_buf_reserve(b, 7), EL_ERR_NO_SPACE);
    EXPECT_EQ(el_buf_room(b), 6u);
}

TEST_F(GrowBuf, AsWriterTakesWhatFitsThenReportsNoSpace)
{
    Open(30);
    uint8_t data[64];
    for (size_t i = 0; i < sizeof(data); i++) {
        data[i] = static_cast<uint8_t>(i);
    }
    /* The first write gets 16 bytes, leaving 14 in the arena. Growing to 32
     * fails, as does exactly 24, so the write takes the 6 bytes left. Then
     * even exactly 17 does not fit. */
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
    Open(30);
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
