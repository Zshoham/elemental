/*
 * Contract fixtures for the buffered writer. The inner writer is a sink
 * written for these fixtures: it holds what it is given until the suite
 * takes it, takes at most 3 bytes per call (so passing on is partial), and
 * when it holds all it can, stalls (EL_ERR_WOULD_BLOCK) or, if asked to,
 * has ended (EL_ERR_END_OF_STREAM).
 *
 * writer_take flushes the buffered writer first, as its owner would; the
 * suite counts what reached the sink.
 *
 * A buffered writer with room accepts bytes its inner writer has not, so it
 * cannot be made to end, or to be full, after an exact byte count: those
 * capabilities are absent, except that the capacity-0 pass-through ends
 * exactly when its sink does. There is no reader half: a buffered writer
 * has no reader, and the memory reader has its own fixture.
 */
#include "fixture_buffered.h"

#include <elemental/io_buf.h>
#include <elemental/io_buffered.h>

#define SINK_PER_CALL 3u

static el_status sink_write(void *ctx, const void *data, size_t len, size_t *n)
{
    io_fixture_sink_state *const s = ctx;
    if (s->len == s->cap) {
        return s->ends ? EL_ERR_END_OF_STREAM : EL_ERR_WOULD_BLOCK;
    }
    size_t count = s->cap - s->len;
    count = count < len ? count : len;
    count = count < SINK_PER_CALL ? count : SINK_PER_CALL;
    const uint8_t *const bytes = data;
    for (size_t i = 0; i < count; i++) {
        s->data[s->len + i] = bytes[i];
    }
    s->len += count;
    *n = count;
    return EL_OK;
}

static const el_writer_ops sink_ops = {.write = sink_write};

el_writer io_fixture_sink_writer(void)
{
    return (el_writer){.ctx = &io_fixture_sink, .ops = &sink_ops};
}

io_fixture_sink_state io_fixture_sink;
el_buffered_writer io_fixture_buffered_writer;

static struct {
    uint8_t storage[16];
    el_buf buf;
} state;

void io_fixture_sink_open(size_t cap, int ends)
{
    io_fixture_sink.len = 0;
    io_fixture_sink.cap = cap;
    io_fixture_sink.ends = ends;
    io_fixture_sink.taken = 0;
}

static el_writer open_fixed(size_t storage_cap)
{
    el_buf_init(&state.buf, state.storage, storage_cap);
    el_buffered_writer_init(&io_fixture_buffered_writer, io_fixture_sink_writer(), &state.buf);
    return el_buffered_writer_as_writer(&io_fixture_buffered_writer);
}

el_writer io_fixture_fill_until_stalled(el_writer w)
{
    static const uint8_t filler[8] = {0};
    size_t n = 0;
    while (el_write(w, filler, sizeof(filler), &n) == EL_OK) {
    }
    return w;
}

static void reset(void *ctx)
{
    (void)ctx;
    io_fixture_sink_open(0, 0);
}

size_t io_fixture_buffered_take(void *ctx, uint8_t *out, size_t cap)
{
    (void)ctx;
    /* What the inner writer refused stays pending, and is not taken. */
    el_status_ignore(el_buffered_writer_flush(&io_fixture_buffered_writer));
    const size_t available = io_fixture_sink.len - io_fixture_sink.taken;
    const size_t count = available < cap ? available : cap;
    for (size_t i = 0; i < count; i++) {
        out[i] = io_fixture_sink.data[io_fixture_sink.taken + i];
    }
    io_fixture_sink.taken += count;
    return count;
}

/* ---- Fixed storage, 16 bytes -------------------------------------------- */

static el_writer fixed_with_room(void *ctx, size_t room)
{
    (void)ctx;
    io_fixture_sink_open(room, 0);
    return open_fixed(sizeof(state.storage));
}

static el_writer fixed_stalled(void *ctx)
{
    (void)ctx;
    io_fixture_sink_open(0, 0);
    return io_fixture_fill_until_stalled(open_fixed(sizeof(state.storage)));
}

const el_io_contract_fixture io_fixture_buffered_fixed = {
    .name = "el_buffered_writer (fixed, 16 bytes)",
    .reset = reset,
    .writer_with_room = fixed_with_room,
    .writer_take = io_fixture_buffered_take,
    .writer_stalled = fixed_stalled,
};

/* ---- Fixed storage, capacity 0: a pass-through writer ------------------- */

static el_writer pass_through_with_room(void *ctx, size_t room)
{
    (void)ctx;
    io_fixture_sink_open(room, 0);
    return open_fixed(0);
}

static el_writer pass_through_stalled(void *ctx)
{
    (void)ctx;
    io_fixture_sink_open(0, 0);
    return open_fixed(0);
}

static el_writer pass_through_ending_after(void *ctx, size_t room)
{
    (void)ctx;
    io_fixture_sink_open(room, 1);
    return open_fixed(0);
}

const el_io_contract_fixture io_fixture_buffered_pass_through = {
    .name = "el_buffered_writer (fixed, capacity 0)",
    .reset = reset,
    .writer_with_room = pass_through_with_room,
    .writer_take = io_fixture_buffered_take,
    .writer_stalled = pass_through_stalled,
    .writer_ending_after = pass_through_ending_after,
};
