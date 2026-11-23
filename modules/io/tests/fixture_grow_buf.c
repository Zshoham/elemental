/*
 * Contract fixture for an el_grow_buf as a writer, over a stack arena
 * wrapped in the checking allocator, so every block it frees is checked
 * against the size it was allocated with, and none is left behind.
 *
 * io_alloc has no reader, so the fixture has no reader half.
 *
 * A grown buffer cannot stall and does not end. It reports EL_ERR_NO_SPACE
 * when its allocator refuses, but where that happens depends on how growth
 * meets the arena, not on an exact byte count. So the stalled, ending and
 * full capabilities are absent.
 */
#include "fixture_grow_buf.h"

#include <elemental/alloc_arena.h>
#include <elemental/alloc_contract.h>
#include <elemental/io_alloc.h>

#include <stdalign.h>
#include <stdlib.h>

static struct {
    alignas(16) unsigned char storage[4 * EL_IO_CONTRACT_SIZE_MAX];
    el_stack_arena arena;
    el_alloc_check_record records[4];
    el_alloc_checker checker;
    el_grow_buf grown;
    el_buf *buf;
    size_t taken;
} state;

static void reset(void *ctx)
{
    (void)ctx;
    if (state.buf != NULL) {
        el_grow_buf_deinit(&state.grown);
        state.buf = NULL;
        if (el_alloc_checker_live(&state.checker) != 0) {
            abort(); /* The buffer left a block behind. */
        }
    }
    state.taken = 0;
}

static el_writer writer_with_room(void *ctx, size_t room)
{
    (void)ctx;
    (void)room; /* Doubling to EL_IO_CONTRACT_SIZE_MAX fits the storage. */
    el_stack_arena_init(&state.arena, state.storage, sizeof(state.storage));
    el_alloc_checker_init(&state.checker, el_stack_arena_as_allocator(&state.arena), state.records,
                          sizeof(state.records) / sizeof(state.records[0]));
    state.buf = el_grow_buf_init(&state.grown, el_alloc_checker_as_allocator(&state.checker));
    state.taken = 0;
    return el_buf_as_writer(state.buf);
}

static size_t writer_take(void *ctx, uint8_t *out, size_t cap)
{
    (void)ctx;
    const uint8_t *const data = el_buf_data(state.buf);
    const size_t available = el_buf_len(state.buf) - state.taken;
    const size_t count = available < cap ? available : cap;
    for (size_t i = 0; i < count; i++) {
        out[i] = data[state.taken + i];
    }
    state.taken += count;
    return count;
}

const el_io_contract_fixture io_alloc_fixture_grow_buf = {
    .name = "el_grow_buf",
    .reset = reset,
    .writer_with_room = writer_with_room,
    .writer_take = writer_take,
};
