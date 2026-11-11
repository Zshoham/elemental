/*
 * Backend-specific tests for the ring_pipe example: the behavior that
 * belongs to this backend rather than to the io interface.
 */
#include <gtest/gtest.h>

#include "ring_pipe.h"

#include <cstddef>
#include <cstdint>
#include <string>

namespace
{

constexpr size_t kUnset = static_cast<size_t>(-1);

TEST(RingPipe, ClosingTheWriteSideEndsBothDirections)
{
    uint8_t storage[8];
    ring_pipe pipe;
    ring_pipe_init(&pipe, storage, sizeof(storage));
    const el_reader reader = ring_pipe_as_reader(&pipe);
    const el_writer writer = ring_pipe_as_writer(&pipe);

    size_t n = kUnset;
    EXPECT_EQ(el_write_all(writer, "abc", 3, &n), EL_OK);
    ring_pipe_close_write(&pipe);

    /* The writer has ended, even though there is room. */
    n = kUnset;
    EXPECT_EQ(el_write(writer, "d", 1, &n), EL_ERR_END_OF_STREAM);
    EXPECT_EQ(n, 0u);

    /* The reader still gets what was written before the close, then ends. */
    char buf[8];
    n = kUnset;
    EXPECT_EQ(el_read_exact(reader, buf, sizeof(buf), &n), EL_ERR_END_OF_STREAM);
    ASSERT_EQ(n, 3u);
    EXPECT_EQ(std::string(buf, n), "abc");
}

TEST(RingPipe, BytesSurviveWrappingAroundTheStorage)
{
    /* A capacity that does not divide the traffic, so every offset is hit. */
    uint8_t storage[7];
    ring_pipe pipe;
    ring_pipe_init(&pipe, storage, sizeof(storage));
    const el_reader reader = ring_pipe_as_reader(&pipe);
    const el_writer writer = ring_pipe_as_writer(&pipe);

    uint8_t next_in = 0;
    uint8_t next_out = 0;
    for (int round = 0; round < 100; round++) {
        SCOPED_TRACE(testing::Message() << "round " << round);

        uint8_t chunk[5];
        for (uint8_t &byte : chunk) {
            byte = next_in++;
        }
        size_t n = kUnset;
        ASSERT_EQ(el_write_all(writer, chunk, sizeof(chunk), &n), EL_OK);

        uint8_t back[5];
        n = kUnset;
        ASSERT_EQ(el_read_exact(reader, back, sizeof(back), &n), EL_OK);
        for (const uint8_t byte : back) {
            ASSERT_EQ(byte, next_out++);
        }
    }
}

TEST(RingPipe, AFullPipeAcceptsMoreOnceDrained)
{
    uint8_t storage[4];
    ring_pipe pipe;
    ring_pipe_init(&pipe, storage, sizeof(storage));
    const el_reader reader = ring_pipe_as_reader(&pipe);
    const el_writer writer = ring_pipe_as_writer(&pipe);

    /* Six bytes into four: write_all reports the stall and what fit. */
    static const char message[] = "abcdef";
    size_t n = kUnset;
    EXPECT_EQ(el_write_all(writer, message, 6, &n), EL_ERR_WOULD_BLOCK);
    ASSERT_EQ(n, 4u);

    char buf[4];
    size_t got = kUnset;
    EXPECT_EQ(el_read(reader, buf, 2, &got), EL_OK);
    EXPECT_EQ(got, 2u);

    /* Resume with the remainder, exactly as elemental/io.h describes. */
    size_t more = kUnset;
    EXPECT_EQ(el_write_all(writer, message + n, 6 - n, &more), EL_OK);
    EXPECT_EQ(more, 2u);

    got = kUnset;
    EXPECT_EQ(el_read_exact(reader, buf, 4, &got), EL_OK);
    EXPECT_EQ(std::string(buf, 4), "cdef");
}

TEST(RingPipe, APipeWithoutStorageAlwaysStalls)
{
    ring_pipe pipe;
    ring_pipe_init(&pipe, nullptr, 0);

    size_t n = kUnset;
    EXPECT_EQ(el_write(ring_pipe_as_writer(&pipe), "a", 1, &n), EL_ERR_WOULD_BLOCK);
    char byte;
    n = kUnset;
    EXPECT_EQ(el_read(ring_pipe_as_reader(&pipe), &byte, 1, &n), EL_ERR_WOULD_BLOCK);
}

} // namespace
