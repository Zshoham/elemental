/*
 * What only the grow writer promises: how it grows, when it reports
 * EL_ERR_NO_SPACE, that it recovers, and that it frees exactly the buffers
 * it allocated, with the sizes it allocated them with. Every allocator here
 * is wrapped in the checking allocator.
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

class GrowWriter : public testing::Test
{
  protected:
    /* A writer over a bump arena of `cap` bytes, checked. */
    void Open(size_t cap)
    {
        el_bump_arena_init(&arena, storage, cap);
        el_alloc_checker_init(&checker, el_bump_arena_as_allocator(&arena), records, 8);
        el_grow_writer_init(&gw, el_alloc_checker_as_allocator(&checker));
        w = el_grow_writer_as_writer(&gw);
    }

    void TearDown() override
    {
        el_grow_writer_deinit(&gw);
        EXPECT_EQ(el_alloc_checker_live(&checker), 0u) << "the writer left a buffer behind";
    }

    std::vector<uint8_t> Contents() const
    {
        const uint8_t *const data = el_grow_writer_data(&gw);
        return std::vector<uint8_t>(data, data + el_grow_writer_len(&gw));
    }

    alignas(16) uint8_t storage[1024];
    el_bump_arena arena;
    el_alloc_check_record records[8];
    el_alloc_checker checker;
    el_grow_writer gw;
    el_writer w;
    size_t n = 0;
};

TEST_F(GrowWriter, StartsEmptyWithoutAllocating)
{
    Open(sizeof(storage));
    EXPECT_EQ(el_grow_writer_len(&gw), 0u);
    EXPECT_EQ(el_grow_writer_data(&gw), nullptr);
    EXPECT_EQ(el_alloc_checker_live(&checker), 0u);
}

TEST_F(GrowWriter, GrowsToSixteenThenDoubles)
{
    Open(sizeof(storage));
    const uint8_t byte = 'x';
    for (int i = 0; i < 40; i++) {
        ASSERT_EQ(el_write(w, &byte, 1, &n), EL_OK);
    }
    /* 16, then 32, then 64: the arena's free gives nothing back. */
    EXPECT_EQ(gw.cap, 64u);
    EXPECT_EQ(arena.used, 16u + 32u + 64u);
    EXPECT_EQ(Contents(), std::vector<uint8_t>(40, 'x'));
}

TEST_F(GrowWriter, GrowsStraightToWhatALargeWriteNeeds)
{
    Open(sizeof(storage));
    uint8_t data[300];
    std::memset(data, 7, sizeof(data));
    ASSERT_EQ(el_write(w, data, sizeof(data), &n), EL_OK);
    EXPECT_EQ(n, sizeof(data));
    EXPECT_EQ(gw.cap, sizeof(data));
}

TEST_F(GrowWriter, AsksForExactlyWhatIsNeededWhenDoublingIsRefused)
{
    /* 100 bytes in, then 50 more: doubling to 200 does not fit beside the
     * first buffer (100 + 200 > 260), but exactly 150 does. */
    Open(260);
    uint8_t data[100] = {};
    ASSERT_EQ(el_write(w, data, 100, &n), EL_OK);
    ASSERT_EQ(el_write(w, data, 50, &n), EL_OK);
    EXPECT_EQ(n, 50u);
    EXPECT_EQ(gw.cap, 150u);
}

TEST_F(GrowWriter, TakesWhatFitsThenReportsNoSpace)
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

    /* el_write_all reports the status and what went through before it. */
    EXPECT_EQ(el_write_all(w, data + 16, 4, &n), EL_ERR_NO_SPACE);
    EXPECT_EQ(n, 0u);
    EXPECT_EQ(Contents(), std::vector<uint8_t>(data, data + 16));
}

TEST_F(GrowWriter, NoSpaceIsNotPermanent)
{
    Open(30);
    uint8_t data[32] = {};
    ASSERT_EQ(el_write_all(w, data, 16, nullptr), EL_OK);
    ASSERT_EQ(el_write(w, data, 1, &n), EL_ERR_NO_SPACE);

    /* The arena gets more room, as if something else had freed. */
    arena.cap = sizeof(storage);
    EXPECT_EQ(el_write(w, data, 1, &n), EL_OK);
    EXPECT_EQ(el_grow_writer_len(&gw), 17u);
}

TEST_F(GrowWriter, AnAllocatorThatRefusesEverythingReportsNoSpace)
{
    Open(0);
    const uint8_t byte = 1;
    EXPECT_EQ(el_write(w, &byte, 1, &n), EL_ERR_NO_SPACE);
    EXPECT_EQ(n, 0u);
    EXPECT_EQ(el_grow_writer_len(&gw), 0u);
}

TEST_F(GrowWriter, DeinitThenInitStartsAgain)
{
    Open(sizeof(storage));
    const uint8_t byte = 1;
    ASSERT_EQ(el_write(w, &byte, 1, &n), EL_OK);
    el_grow_writer_deinit(&gw);
    EXPECT_EQ(el_alloc_checker_live(&checker), 0u);
    el_grow_writer_init(&gw, el_alloc_checker_as_allocator(&checker));
    EXPECT_EQ(el_grow_writer_len(&gw), 0u);
}

} // namespace
