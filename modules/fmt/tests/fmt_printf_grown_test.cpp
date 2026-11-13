/*
 * printf into grown storage (IO_ALLOC): docs/proposals/io-buffers-fmt.md,
 * Examples 3 and 2b, and a long string that grows the buffer at once.
 */
#include <gtest/gtest.h>

#include <elemental/alloc_arena.h>
#include <elemental/fmt_printf.h>
#include <elemental/io_alloc.h>
#include <elemental/io_buffered.h>

#include <cinttypes>
#include <cstdint>
#include <string>

namespace
{

std::string Text(const el_buf *b)
{
    const uint8_t *const data = el_buf_data(b);
    return data == nullptr ? std::string()
                           : std::string(reinterpret_cast<const char *>(data), el_buf_len(b));
}

/* docs/proposals/io-buffers-fmt.md, Example 3: a message of unknown size in
 * a grown buffer, bounded by the arena. */
el_status PublishReadings(size_t count, el_allocator scratch, std::string *message)
{
    el_grow_buf storage;
    el_buf *const msg = el_grow_buf_init(&storage, scratch);
    el_status st = el_buf_append(msg, "{\"r\":[", 6);
    for (size_t i = 0; i < count && st == EL_OK; i++) {
        st = el_fmt_printf(msg, "%s[%" PRIu16 ",%" PRId32 "]", i == 0 ? "" : ",",
                           static_cast<uint16_t>(i + 1), static_cast<int32_t>(i * 1290) - 40);
    }
    if (st == EL_OK) {
        st = el_buf_append(msg, "]}", 2);
    }
    if (st == EL_OK) {
        *message = Text(msg);
    }
    el_grow_buf_deinit(&storage);
    return st;
}

TEST(FmtPrintfGrown, AGrownBufferTakesAMessageOfUnknownSizeUpToItsAllocator)
{
    static uint8_t arena_storage[2048];
    el_stack_arena arena;
    std::string message;

    el_stack_arena_init(&arena, arena_storage, sizeof arena_storage);
    EXPECT_EQ(PublishReadings(3, el_stack_arena_as_allocator(&arena), &message), EL_OK);
    EXPECT_EQ(message, "{\"r\":[[1,-40],[2,1250],[3,2540]]}");

    el_stack_arena_init(&arena, arena_storage, sizeof arena_storage);
    EXPECT_EQ(PublishReadings(300, el_stack_arena_as_allocator(&arena), &message), EL_ERR_NO_SPACE);
}

TEST(FmtPrintfGrown, ALongStringGrowsAGrownBufferAtOnce)
{
    alignas(16) static uint8_t arena_storage[1024];
    el_bump_arena arena;
    el_bump_arena_init(&arena, arena_storage, sizeof arena_storage);
    el_grow_buf storage;
    el_buf *const b = el_grow_buf_init(&storage, el_bump_arena_as_allocator(&arena));
    const std::string text(100, 't');
    EXPECT_EQ(el_fmt_printf(b, "%s|%5d", text.c_str(), 7), EL_OK);
    EXPECT_EQ(Text(b), text + "|    7");
    el_grow_buf_deinit(&storage);
}

/* A transmit FIFO: takes at most `per_call` bytes per call and at most
 * `room` in all until drained; never waits. */
class Fifo
{
  public:
    el_writer writer()
    {
        static const el_writer_ops ops = {Write};
        return el_writer{this, &ops};
    }

    std::string sent;
    size_t per_call = 1000;
    size_t room = 0;
    el_status fail = EL_OK;

  private:
    static el_status Write(void *ctx, const void *data, size_t len, size_t *n)
    {
        Fifo *const self = static_cast<Fifo *>(ctx);
        if (self->fail != EL_OK) {
            return self->fail;
        }
        if (self->room == 0) {
            return EL_ERR_WOULD_BLOCK;
        }
        size_t count = len < self->per_call ? len : self->per_call;
        count = count < self->room ? count : self->room;
        self->sent.append(static_cast<const char *>(data), count);
        self->room -= count;
        *n = count;
        return EL_OK;
    }
};

std::string Pending(const el_buf *b, const el_buffered_writer *w)
{
    const size_t pending = el_buffered_writer_pending(w);
    const uint8_t *const data = el_buf_data(b);
    return std::string(reinterpret_cast<const char *>(data) + el_buf_len(b) - pending, pending);
}

/* ---- Example 2b: grown storage absorbs a burst ------------------------------ */

TEST(FmtExample2b, AGrownBufferAbsorbsABurstUpToItsArena)
{
    alignas(16) static uint8_t arena_storage[1024];
    el_bump_arena arena;
    el_grow_buf storage;
    Fifo pipe;
    el_buffered_writer trace;

    el_bump_arena_init(&arena, arena_storage, sizeof arena_storage);
    el_buffered_writer_init(&trace, pipe.writer(),
                            el_grow_buf_init(&storage, el_bump_arena_as_allocator(&arena)));
    std::string expected;
    el_status st = EL_OK;
    unsigned events = 0;
    for (; events < 1000; events++) {
        st = el_fmt_printf_buffered(&trace, "%u %s\n", events, "tick");
        if (st != EL_OK) {
            break;
        }
        expected += std::to_string(events) + " tick\n";
    }
    EXPECT_EQ(st, EL_ERR_WOULD_BLOCK);
    EXPECT_GT(events, 20u);
    /* Everything accepted is still pending. The event that did not fit was
     * cut at an item: its number is whole or absent, its text cut. */
    const std::string accepted = Pending(&storage.buf, &trace);
    ASSERT_GE(accepted.size(), expected.size());
    EXPECT_EQ(accepted.compare(0, expected.size(), expected), 0);
    const std::string partial = accepted.substr(expected.size());
    const std::string event = std::to_string(events) + " tick\n";
    EXPECT_EQ(event.compare(0, partial.size(), partial), 0) << partial;
    EXPECT_TRUE(partial.empty() || partial.size() >= std::to_string(events).size()) << partial;

    pipe.room = 100000;
    EXPECT_EQ(el_buffered_writer_flush(&trace), EL_OK);
    EXPECT_EQ(pipe.sent.compare(0, expected.size(), expected), 0);
    el_grow_buf_deinit(&storage);
}

} // namespace
