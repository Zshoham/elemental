/*
 * What el_buffered_writer promises beyond the io contract
 * (elemental/io_buffered.h): how a write that does not fit is handled
 * (decision A of docs/proposals/io-buffers-fmt.md), that only
 * EL_ERR_END_OF_STREAM is remembered (decision 17), the status when nothing
 * fits (decision 18), flush and reserve, and when bytes move. The inner
 * writer is a programmable stand-in.
 */
#include <gtest/gtest.h>

#include <elemental/io_buf.h>
#include <elemental/io_buffered.h>

#include <cstddef>
#include <cstdint>
#include <string>

namespace
{

/* An inner writer whose behavior the test sets between calls. */
class Inner
{
  public:
    el_writer writer()
    {
        static const el_writer_ops ops = {Write};
        return el_writer{this, &ops};
    }

    std::string got;        /* what it took, in order */
    unsigned calls = 0;     /* how often it was called */
    size_t per_call = 1000; /* at most this much per call */
    size_t room = 1000;     /* then it stalls */
    el_status fail = EL_OK; /* returned instead, while set */

  private:
    static el_status Write(void *ctx, const void *data, size_t len, size_t *n)
    {
        Inner *const self = static_cast<Inner *>(ctx);
        self->calls++;
        if (self->fail != EL_OK) {
            return self->fail;
        }
        if (self->room == 0) {
            return EL_ERR_WOULD_BLOCK;
        }
        size_t count = len < self->per_call ? len : self->per_call;
        count = count < self->room ? count : self->room;
        self->got.append(static_cast<const char *>(data), count);
        self->room -= count;
        *n = count;
        return EL_OK;
    }
};

class Buffered : public testing::Test
{
  protected:
    void Open(size_t cap)
    {
        el_buf_init(&buf, storage, cap);
        el_buffered_writer_init(&bw, inner.writer(), &buf);
        w = el_buffered_writer_as_writer(&bw);
    }

    std::string Held() const
    {
        return std::string(reinterpret_cast<const char *>(el_buf_data(&buf)), el_buf_len(&buf));
    }

    el_status Write(const char *text, size_t len)
    {
        n = 99;
        return el_write(w, text, len, &n);
    }

    Inner inner;
    uint8_t storage[8] = {};
    el_buf buf;
    el_buffered_writer bw;
    el_writer w;
    size_t n = 0;
};

/* ---- Writes ------------------------------------------------------------- */

TEST_F(Buffered, AWriteThatFitsIsBufferedWithoutCallingTheInnerWriter)
{
    Open(8);
    EXPECT_EQ(Write("abc", 3), EL_OK);
    EXPECT_EQ(n, 3u);
    EXPECT_EQ(Write("defgh", 5), EL_OK);
    EXPECT_EQ(inner.calls, 0u);
    EXPECT_EQ(Held(), "abcdefgh");
    EXPECT_EQ(el_buffered_writer_pending(&bw), 8u);
}

TEST_F(Buffered, AWriteThatDoesNotFitPassesOnOnceThenBuffersWhatFits)
{
    Open(8);
    inner.per_call = 3;
    ASSERT_EQ(Write("abcdefgh", 8), EL_OK);
    EXPECT_EQ(Write("ijklm", 5), EL_OK);
    EXPECT_EQ(inner.calls, 1u);
    EXPECT_EQ(inner.got, "abc");
    EXPECT_EQ(n, 3u); /* the room the pass-on made, after compacting */
    EXPECT_EQ(Held(), "defghijk");
}

TEST_F(Buffered, AStalledPassOnBuffersWhatFitsOrReportsWouldBlock)
{
    Open(8);
    inner.room = 0;
    ASSERT_EQ(Write("abcdef", 6), EL_OK);
    EXPECT_EQ(Write("ghij", 4), EL_OK);
    EXPECT_EQ(n, 2u);
    EXPECT_EQ(Write("x", 1), EL_ERR_WOULD_BLOCK);
    EXPECT_EQ(n, 0u);
    EXPECT_EQ(Held(), "abcdefgh");

    /* Room returns as the stream drains. */
    inner.room = 1000;
    EXPECT_EQ(Write("x", 1), EL_OK);
    EXPECT_EQ(inner.got, "abcdefgh");
    EXPECT_EQ(Held(), "x");
}

TEST_F(Buffered, AnIoFailureFromThePassOnIsReturnedWithNothingTakenAndNotRemembered)
{
    Open(8);
    ASSERT_EQ(Write("abcdefgh", 8), EL_OK);
    inner.fail = EL_ERR_IO;
    EXPECT_EQ(Write("ij", 2), EL_ERR_IO);
    EXPECT_EQ(n, 0u);
    EXPECT_EQ(Held(), "abcdefgh");
    EXPECT_EQ(el_buffered_writer_flush(&bw), EL_ERR_IO);

    inner.fail = EL_OK;
    EXPECT_EQ(Write("ij", 2), EL_OK);
    EXPECT_EQ(el_buffered_writer_flush(&bw), EL_OK);
    EXPECT_EQ(inner.got, "abcdefghij");
}

TEST_F(Buffered, AnApplicationStatusFromTheInnerWriterIsReturnedUnchangedAndNotRemembered)
{
    /* Negative statuses are the application's (elemental/status.h). */
    constexpr el_status kMine = -42;
    Open(8);
    ASSERT_EQ(Write("abcdefgh", 8), EL_OK);
    inner.fail = kMine;
    EXPECT_EQ(Write("ij", 2), kMine);
    EXPECT_EQ(n, 0u);
    EXPECT_EQ(el_buffered_writer_flush(&bw), kMine);
    EXPECT_EQ(el_buffered_writer_reserve(&bw, 1), kMine);
    EXPECT_EQ(Held(), "abcdefgh");

    inner.fail = EL_OK;
    EXPECT_EQ(el_buffered_writer_flush(&bw), EL_OK);
    EXPECT_EQ(inner.got, "abcdefgh");
}

TEST_F(Buffered, NoSpaceFromThePassOnIsReturnedWithNothingTaken)
{
    Open(8);
    ASSERT_EQ(Write("abcdefgh", 8), EL_OK);
    inner.fail = EL_ERR_NO_SPACE;
    EXPECT_EQ(Write("i", 1), EL_ERR_NO_SPACE);
    EXPECT_EQ(n, 0u);
    EXPECT_EQ(el_buffered_writer_pending(&bw), 8u);
    inner.fail = EL_OK;
    EXPECT_EQ(Write("i", 1), EL_OK);
}

TEST_F(Buffered, EndOfStreamFromThePassOnIsPermanentEvenForAWriteThatFits)
{
    Open(8);
    ASSERT_EQ(Write("abcdefgh", 8), EL_OK);
    inner.fail = EL_ERR_END_OF_STREAM;
    EXPECT_EQ(Write("i", 1), EL_ERR_END_OF_STREAM);
    EXPECT_EQ(n, 0u);
    const unsigned calls = inner.calls;

    /* Remembered: not asked again, whatever the call. */
    inner.fail = EL_OK;
    EXPECT_EQ(Write("i", 1), EL_ERR_END_OF_STREAM);
    EXPECT_EQ(el_buffered_writer_flush(&bw), EL_ERR_END_OF_STREAM);
    EXPECT_EQ(el_buffered_writer_reserve(&bw, 0), EL_ERR_END_OF_STREAM);
    EXPECT_EQ(el_buffered_writer_pending(&bw), 0u);
    EXPECT_EQ(inner.calls, calls);

    /* Writing nothing still succeeds without consulting the stream. */
    EXPECT_EQ(Write(nullptr, 0), EL_OK);
}

TEST_F(Buffered, EndOfStreamFromAFlushIsPermanent)
{
    Open(8);
    ASSERT_EQ(Write("ab", 2), EL_OK);
    inner.fail = EL_ERR_END_OF_STREAM;
    EXPECT_EQ(el_buffered_writer_flush(&bw), EL_ERR_END_OF_STREAM);
    inner.fail = EL_OK;
    EXPECT_EQ(Write("c", 1), EL_ERR_END_OF_STREAM);
}

TEST_F(Buffered, AWriteOfAtLeastTheCapacityGoesStraightThroughWhenNothingIsPending)
{
    Open(8);
    inner.per_call = 5;
    EXPECT_EQ(Write("abcdefghij", 10), EL_OK);
    EXPECT_EQ(n, 5u);
    EXPECT_EQ(inner.got, "abcde");
    EXPECT_EQ(el_buf_len(&buf), 0u);
}

TEST_F(Buffered, AWriteThroughThatEndsIsReturnedAndRemembered)
{
    Open(8);
    inner.fail = EL_ERR_END_OF_STREAM;
    EXPECT_EQ(Write("abcdefghi", 9), EL_ERR_END_OF_STREAM);
    EXPECT_EQ(n, 0u);
    inner.fail = EL_OK;
    EXPECT_EQ(Write("a", 1), EL_ERR_END_OF_STREAM);
    EXPECT_EQ(el_buf_len(&buf), 0u);
}

TEST_F(Buffered, AWriteThroughThatFailsIsReturnedWithNothingBuffered)
{
    Open(8);
    inner.fail = EL_ERR_IO;
    EXPECT_EQ(Write("abcdefghi", 9), EL_ERR_IO);
    EXPECT_EQ(n, 0u);
    EXPECT_EQ(el_buf_len(&buf), 0u);
    inner.fail = EL_OK;
    EXPECT_EQ(Write("abcdefghi", 9), EL_OK);
    EXPECT_EQ(inner.got, "abcdefghi");
}

TEST_F(Buffered, AWriteThroughThatWouldBlockFallsBackToBuffering)
{
    Open(8);
    inner.room = 0;
    EXPECT_EQ(Write("abcdefghij", 10), EL_OK);
    EXPECT_EQ(n, 8u);
    EXPECT_EQ(inner.calls, 1u);
    EXPECT_EQ(Held(), "abcdefgh");
}

TEST_F(Buffered, FixedCapacityZeroIsAPassThroughWriter)
{
    Open(0);
    inner.per_call = 2;
    EXPECT_EQ(Write("abc", 3), EL_OK);
    EXPECT_EQ(n, 2u);
    inner.room = 0;
    EXPECT_EQ(Write("c", 1), EL_ERR_WOULD_BLOCK);
    EXPECT_EQ(el_buffered_writer_pending(&bw), 0u);
    EXPECT_EQ(el_buffered_writer_flush(&bw), EL_OK);
    EXPECT_EQ(el_buffered_writer_reserve(&bw, 1), EL_ERR_NO_SPACE);
    EXPECT_EQ(el_buffered_writer_reserve(&bw, 0), EL_OK);
}

/* ---- Flush, and when bytes move ------------------------------------------ */

TEST_F(Buffered, FlushPassesEverythingOnAndEmptiesTheBuffer)
{
    Open(8);
    inner.per_call = 3;
    ASSERT_EQ(Write("abcdefgh", 8), EL_OK);
    EXPECT_EQ(el_buffered_writer_flush(&bw), EL_OK);
    EXPECT_EQ(inner.got, "abcdefgh");
    EXPECT_EQ(el_buffered_writer_pending(&bw), 0u);
    EXPECT_EQ(el_buf_len(&buf), 0u);
    EXPECT_EQ(el_buf_room(&buf), 8u);
    /* Nothing pending: done without asking the stream. */
    const unsigned calls = inner.calls;
    EXPECT_EQ(el_buffered_writer_flush(&bw), EL_OK);
    EXPECT_EQ(inner.calls, calls);
}

TEST_F(Buffered, APartialFlushMovesNoByteAndIsResumable)
{
    Open(8);
    inner.room = 3;
    ASSERT_EQ(Write("abcdefgh", 8), EL_OK);
    EXPECT_EQ(el_buffered_writer_flush(&bw), EL_ERR_WOULD_BLOCK);
    EXPECT_EQ(el_buffered_writer_pending(&bw), 5u);
    EXPECT_EQ(Held(), "abcdefgh"); /* in place: passed on, not moved */
    EXPECT_EQ(el_buf_room(&buf), 0u);

    inner.room = 2;
    EXPECT_EQ(el_buffered_writer_flush(&bw), EL_ERR_WOULD_BLOCK);
    EXPECT_EQ(el_buffered_writer_pending(&bw), 3u);
    inner.room = 1000;
    EXPECT_EQ(el_buffered_writer_flush(&bw), EL_OK);
    EXPECT_EQ(inner.got, "abcdefgh");
}

TEST_F(Buffered, TheNextWriteCompactsWhatAPartialFlushPassedOn)
{
    Open(8);
    inner.room = 3;
    ASSERT_EQ(Write("abcdefgh", 8), EL_OK);
    ASSERT_EQ(el_buffered_writer_flush(&bw), EL_ERR_WOULD_BLOCK);
    EXPECT_EQ(Write("ij", 2), EL_OK);
    EXPECT_EQ(n, 2u);
    EXPECT_EQ(Held(), "defghij");
    EXPECT_EQ(el_buffered_writer_pending(&bw), 7u);
}

TEST_F(Buffered, WhatTheBufferHeldAtInitIsPassedOnFirst)
{
    el_buf_init(&buf, storage, sizeof(storage));
    ASSERT_EQ(el_buf_append(&buf, "ab", 2), EL_OK);
    el_buffered_writer_init(&bw, inner.writer(), &buf);
    EXPECT_EQ(el_buffered_writer_pending(&bw), 2u);
    w = el_buffered_writer_as_writer(&bw);
    ASSERT_EQ(Write("c", 1), EL_OK);
    EXPECT_EQ(el_buffered_writer_flush(&bw), EL_OK);
    EXPECT_EQ(inner.got, "abc");
}

/* ---- Reserve -------------------------------------------------------------- */

TEST_F(Buffered, ReserveWithRoomAlreadyThereDoesNothing)
{
    Open(8);
    ASSERT_EQ(Write("abc", 3), EL_OK);
    EXPECT_EQ(el_buffered_writer_reserve(&bw, 5), EL_OK);
    EXPECT_EQ(inner.calls, 0u);
}

TEST_F(Buffered, ReservePassesOnOnlyUntilThereIsRoom)
{
    Open(8);
    inner.per_call = 2;
    ASSERT_EQ(Write("abcdefgh", 8), EL_OK);
    EXPECT_EQ(el_buffered_writer_reserve(&bw, 3), EL_OK);
    EXPECT_EQ(inner.got, "abcd"); /* two attempts of 2: 4 >= 3 */
    EXPECT_EQ(el_buf_room(&buf), 4u);
    EXPECT_EQ(Held(), "efgh");
}

TEST_F(Buffered, ReserveBeyondFixedStorageIsNoSpaceAndPassesNothingOn)
{
    Open(8);
    ASSERT_EQ(Write("abc", 3), EL_OK);
    EXPECT_EQ(el_buffered_writer_reserve(&bw, 9), EL_ERR_NO_SPACE);
    EXPECT_EQ(el_buffered_writer_reserve(&bw, SIZE_MAX), EL_ERR_NO_SPACE);
    EXPECT_EQ(inner.calls, 0u);
    EXPECT_EQ(el_buffered_writer_pending(&bw), 3u);
}

TEST_F(Buffered, ReserveReportsAStallAsWouldBlock)
{
    Open(8);
    inner.room = 2;
    ASSERT_EQ(Write("abcdefgh", 8), EL_OK);
    EXPECT_EQ(el_buffered_writer_reserve(&bw, 4), EL_ERR_WOULD_BLOCK);
    EXPECT_EQ(el_buf_room(&buf), 2u); /* what was passed on, compacted */
    inner.room = 1000;
    EXPECT_EQ(el_buffered_writer_reserve(&bw, 4), EL_OK);
}

TEST_F(Buffered, ReserveReturnsTheInnerWritersFailures)
{
    Open(8);
    ASSERT_EQ(Write("abcdefgh", 8), EL_OK);
    inner.fail = EL_ERR_IO;
    EXPECT_EQ(el_buffered_writer_reserve(&bw, 1), EL_ERR_IO);
    inner.fail = EL_ERR_END_OF_STREAM;
    EXPECT_EQ(el_buffered_writer_reserve(&bw, 1), EL_ERR_END_OF_STREAM);
    inner.fail = EL_OK;
    EXPECT_EQ(el_buffered_writer_reserve(&bw, 1), EL_ERR_END_OF_STREAM);
}

TEST_F(Buffered, ARecordReservedFirstReachesTheStreamWholeOrCanBeTakenBack)
{
    Open(8);
    inner.room = 0;
    ASSERT_EQ(Write("ab", 2), EL_OK);
    ASSERT_EQ(el_buffered_writer_reserve(&bw, 4), EL_OK);
    const size_t mark = el_buf_len(&buf);
    ASSERT_EQ(Write("cde", 3), EL_OK);
    /* Not passed on, so the owner may take it back. */
    EXPECT_LE(el_buf_len(&buf) - el_buffered_writer_pending(&bw), mark);
    el_buf_truncate(&buf, mark);
    EXPECT_EQ(el_buffered_writer_pending(&bw), 2u);
    inner.room = 1000;
    EXPECT_EQ(el_buffered_writer_flush(&bw), EL_OK);
    EXPECT_EQ(inner.got, "ab");
}

} // namespace
