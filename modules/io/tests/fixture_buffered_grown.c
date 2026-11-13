/*
 * Contract fixture for the buffered writer with grown storage, over a
 * 256-byte stack arena, in front of the sink of fixture_buffered.c. Only
 * with IO_ALLOC.
 */
#include "fixture_buffered.h"

#include <elemental/alloc_arena.h>
#include <elemental/io_alloc.h>
#include <elemental/io_buffered.h>

#include <stdalign.h>

static struct {
    alignas(16) uint8_t arena_storage[256];
    el_stack_arena arena;
    el_grow_buf grown;
    int open;
} state;

static void reset(void *ctx)
{
    (void)ctx;
    if (state.open) {
        el_grow_buf_deinit(&state.grown);
        state.open = 0;
    }
    io_fixture_sink_open(0, 0);
}

static el_writer open_grown(void)
{
    el_stack_arena_init(&state.arena, state.arena_storage, sizeof(state.arena_storage));
    el_buf *const buf = el_grow_buf_init(&state.grown, el_stack_arena_as_allocator(&state.arena));
    state.open = 1;
    el_buffered_writer_init(&io_fixture_buffered_writer, io_fixture_sink_writer(), buf);
    return el_buffered_writer_as_writer(&io_fixture_buffered_writer);
}

static el_writer grown_with_room(void *ctx, size_t room)
{
    (void)ctx;
    io_fixture_sink_open(room, 0);
    return open_grown();
}

/* Stalled means the sink is full AND the arena is spent (decision B). */
static el_writer grown_stalled(void *ctx)
{
    (void)ctx;
    io_fixture_sink_open(0, 0);
    return io_fixture_fill_until_stalled(open_grown());
}

const el_io_contract_fixture io_alloc_fixture_buffered_grown = {
    .name = "el_buffered_writer (grown, 256-byte arena)",
    .ctx = NULL,
    .reset = reset,
    .reader_with_content = io_fixture_buffered_reader,
    .writer_with_room = grown_with_room,
    .writer_take = io_fixture_buffered_take,
    .reader_stalled = NULL,
    .writer_stalled = grown_stalled,
    .writer_ending_after = NULL,
    .writer_full_after = NULL,
};
