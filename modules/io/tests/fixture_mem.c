/*
 * Contract fixture for the memory reader and writer. The smallest complete
 * fixture: plain C, like the implementation it describes.
 *
 * Memory streams never stall, so the stalled-stream capabilities are absent.
 */
#include "fixture_mem.h"

#include <elemental/io_mem.h>

static struct {
    el_mem_reader reader;
    el_mem_writer writer;
    uint8_t sink[EL_IO_CONTRACT_SIZE_MAX];
    size_t taken;
} state;

static void reset(void *ctx)
{
    (void)ctx;
    /* Nothing to release; forget the previous stream. */
    el_mem_reader_init(&state.reader, NULL, 0);
    el_mem_writer_init(&state.writer, NULL, 0);
    state.taken = 0;
}

static el_reader reader_with_content(void *ctx, const uint8_t *content, size_t len)
{
    (void)ctx;
    el_mem_reader_init(&state.reader, content, len);
    return el_mem_reader_as_reader(&state.reader);
}

static el_writer writer_ending_after(void *ctx, size_t room)
{
    (void)ctx;
    el_mem_writer_init(&state.writer, state.sink, room);
    state.taken = 0;
    return el_mem_writer_as_writer(&state.writer);
}

static el_writer writer_with_room(void *ctx, size_t room)
{
    /* A memory writer has exactly the room it is given. */
    return writer_ending_after(ctx, room);
}

static size_t writer_take(void *ctx, uint8_t *out, size_t cap)
{
    (void)ctx;
    const size_t available = el_mem_writer_len(&state.writer) - state.taken;
    const size_t count = available < cap ? available : cap;
    for (size_t i = 0; i < count; i++) {
        out[i] = state.sink[state.taken + i];
    }
    state.taken += count;
    return count;
}

const el_io_contract_fixture io_fixture_mem = {
    .name = "io_mem",
    .ctx = NULL,
    .reset = reset,
    .reader_with_content = reader_with_content,
    .writer_with_room = writer_with_room,
    .writer_take = writer_take,
    .reader_stalled = NULL,
    .writer_stalled = NULL,
    .writer_ending_after = writer_ending_after,
};
