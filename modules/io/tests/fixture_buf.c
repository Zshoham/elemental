/*
 * Contract fixture for the memory reader and for a fixed el_buf as a writer
 * (the memory endpoint). The smallest complete fixture: plain C, like the
 * implementations it describes.
 *
 * Memory streams never stall, and neither one ends as a writer: a full
 * buffer reports EL_ERR_NO_SPACE, which is not permanent. So the stalled and
 * ending capabilities are absent, and writer_full_after is present.
 */
#include "fixture_buf.h"

#include <elemental/io_buf.h>
#include <elemental/io_mem.h>

static struct {
    el_mem_reader reader;
    el_buf buf;
    uint8_t sink[EL_IO_CONTRACT_SIZE_MAX];
    size_t taken;
} state;

static void reset(void *ctx)
{
    (void)ctx;
    /* Nothing to release; forget the previous stream. */
    el_mem_reader_init(&state.reader, NULL, 0);
    el_buf_init(&state.buf, NULL, 0);
    state.taken = 0;
}

static el_reader reader_with_content(void *ctx, const uint8_t *content, size_t len)
{
    (void)ctx;
    el_mem_reader_init(&state.reader, content, len);
    return el_mem_reader_as_reader(&state.reader);
}

static el_writer writer_full_after(void *ctx, size_t room)
{
    (void)ctx;
    el_buf_init(&state.buf, state.sink, room);
    state.taken = 0;
    return el_buf_as_writer(&state.buf);
}

static el_writer writer_with_room(void *ctx, size_t room)
{
    /* A fixed buffer has exactly the room it is given. */
    return writer_full_after(ctx, room);
}

static size_t writer_take(void *ctx, uint8_t *out, size_t cap)
{
    (void)ctx;
    const uint8_t *const data = el_buf_data(&state.buf);
    const size_t available = el_buf_len(&state.buf) - state.taken;
    const size_t count = available < cap ? available : cap;
    for (size_t i = 0; i < count; i++) {
        out[i] = data[state.taken + i];
    }
    state.taken += count;
    return count;
}

const el_io_contract_fixture io_fixture_buf = {
    .name = "el_buf (fixed) and el_mem_reader",
    .ctx = NULL,
    .reset = reset,
    .reader_with_content = reader_with_content,
    .writer_with_room = writer_with_room,
    .writer_take = writer_take,
    .reader_stalled = NULL,
    .writer_stalled = NULL,
    .writer_ending_after = NULL,
    .writer_full_after = writer_full_after,
};
