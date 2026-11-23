/*
 * The buffered writer over grown storage (decision B of
 * docs/proposals/io-buffers-fmt.md): it grows only where fixed storage
 * would have to wait, and the allocator is the bound.
 */
#include <gtest/gtest.h>

#include <elemental/alloc_arena.h>
#include <elemental/alloc_contract.h>
#include <elemental/io_alloc.h>
#include <elemental/io_buffered.h>

#include <cstddef>
#include <cstdint>
#include <string>

namespace
{

/* A stream that takes nothing while `stalled`, everything otherwise. */
struct Stream {
    std::string got;
    bool stalled = true;

    el_writer writer()
    {
        static const el_writer_ops ops = {Write};
        return el_writer{this, &ops};
    }

    static el_status Write(void *ctx, const void *data, size_t len, size_t *n)
    {
        Stream *const self = static_cast<Stream *>(ctx);
        if (self->stalled) {
            return EL_ERR_WOULD_BLOCK;
        }
        self->got.append(static_cast<const char *>(data), len);
        *n = len;
        return EL_OK;
    }
};

class BufferedGrown : public testing::Test
{
  protected:
    void Open(size_t arena_cap)
    {
        el_bump_arena_init(&arena, storage, arena_cap);
        el_alloc_checker_init(&checker, el_bump_arena_as_allocator(&arena), records, 8);
        buf = el_grow_buf_init(&grown, el_alloc_checker_as_allocator(&checker));
        el_buffered_writer_init(&bw, stream.writer(), buf);
        w = el_buffered_writer_as_writer(&bw);
    }

    void TearDown() override
    {
        el_grow_buf_deinit(&grown);
        EXPECT_EQ(el_alloc_checker_live(&checker), 0u);
    }

    alignas(16) uint8_t storage[512];
    el_bump_arena arena;
    el_alloc_check_record records[8];
    el_alloc_checker checker;
    el_grow_buf grown;
    el_buf *buf = nullptr;
    el_buffered_writer bw;
    el_writer w;
    Stream stream;
    size_t n = 0;
};

TEST_F(BufferedGrown, AnEmptyGrownBufferGrowsInsteadOfWritingThrough)
{
    Open(sizeof(storage));
    stream.stalled = false;
    EXPECT_EQ(el_write(w, "abcdefghijklmnopqrst", 20, &n), EL_OK);
    EXPECT_EQ(n, 20u);
    EXPECT_EQ(stream.got, "");
    EXPECT_EQ(el_buffered_writer_pending(&bw), 20u);
}

TEST_F(BufferedGrown, GrowsWhileTheStreamWouldBlockUntilTheAllocatorRefuses)
{
    Open(64);
    size_t total = 0;
    el_status status = EL_OK;
    while (status == EL_OK) {
        status = el_write(w, "0123456789", 10, &n);
        total += n;
    }
    /* 16, 32, then 64, each extending the arena's latest allocation. The
     * arena's 64 bytes are then spent (growing to 128, or to exactly what is
     * needed, does not fit); the last write took what fit. */
    EXPECT_EQ(status, EL_ERR_WOULD_BLOCK);
    EXPECT_EQ(total, 64u);
    EXPECT_EQ(el_buffered_writer_pending(&bw), 64u);

    stream.stalled = false;
    EXPECT_EQ(el_buffered_writer_flush(&bw), EL_OK);
    EXPECT_EQ(stream.got.size(), 64u);
}

TEST_F(BufferedGrown, NothingPendingAndTheAllocatorRefusingIsNoSpace)
{
    Open(0);
    stream.stalled = false;
    EXPECT_EQ(el_write(w, "a", 1, &n), EL_ERR_NO_SPACE);
    EXPECT_EQ(stream.got, "");
}

TEST_F(BufferedGrown, ReserveBeyondTheCapacityGrows)
{
    Open(sizeof(storage));
    ASSERT_EQ(el_write(w, "abc", 3, &n), EL_OK);
    EXPECT_EQ(el_buffered_writer_reserve(&bw, 100), EL_OK);
    EXPECT_GE(el_buf_room(buf), 100u);
    EXPECT_EQ(el_buffered_writer_pending(&bw), 3u);
}

TEST_F(BufferedGrown, ReserveBeyondTheAllocatorIsNoSpace)
{
    Open(64);
    EXPECT_EQ(el_buffered_writer_reserve(&bw, 65), EL_ERR_NO_SPACE);
    EXPECT_EQ(el_buffered_writer_reserve(&bw, SIZE_MAX), EL_ERR_NO_SPACE);
    EXPECT_EQ(el_buffered_writer_reserve(&bw, 64), EL_OK);
}

TEST_F(BufferedGrown, ReserveGrowsWhenTheStreamStallsAndIsWouldBlockWhenRefused)
{
    Open(32);
    ASSERT_EQ(el_write(w, "0123456789abcdef", 16, &n), EL_OK); /* 16 allocated */
    /* Draining could make room, but the stream is stalled: grow (to 32). */
    EXPECT_EQ(el_buffered_writer_reserve(&bw, 8), EL_OK);
    ASSERT_EQ(el_write(w, "01234567", 8, &n), EL_OK);
    ASSERT_EQ(el_write(w, "01234567", 8, &n), EL_OK);
    /* Full at 32, which is all the arena has: room only returns as it drains. */
    EXPECT_EQ(el_buffered_writer_reserve(&bw, 8), EL_ERR_WOULD_BLOCK);
}

TEST_F(BufferedGrown, FlushNeverGrows)
{
    Open(sizeof(storage));
    ASSERT_EQ(el_write(w, "abc", 3, &n), EL_OK);
    const size_t used = arena.used;
    EXPECT_EQ(el_buffered_writer_flush(&bw), EL_ERR_WOULD_BLOCK);
    EXPECT_EQ(arena.used, used);
}

} // namespace
