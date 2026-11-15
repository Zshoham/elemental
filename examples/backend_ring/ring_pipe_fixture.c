/*
 * How a backend author proves a backend: describe how to put it in each
 * state the contract suite asks for, and run the suite. The fixture is plain
 * C, like the backend; contract_ring_test.cpp hands it to the suite.
 */
#include "ring_pipe_fixture.h"

#include "ring_pipe.h"

static struct {
    ring_pipe pipe;
    uint8_t storage[EL_IO_CONTRACT_SIZE_MAX];
} state;

static void reset(void *ctx)
{
    (void)ctx;
    ring_pipe_init(&state.pipe, state.storage, 0);
}

static void fill(const uint8_t *content, size_t len)
{
    size_t n = 0;
    el_status_ignore(el_write_all(ring_pipe_as_writer(&state.pipe), content, len, &n));
}

/*
 * A fresh pipe, emptied again after some traffic, so that the streams the
 * suite asks for start in the middle of the storage and wrap around its end.
 * Without this the suite would only ever exercise the unwrapped case.
 */
static void start_mid_storage(void)
{
    uint8_t scratch[100] = {0};
    size_t n = 0;

    ring_pipe_init(&state.pipe, state.storage, sizeof(state.storage));
    fill(scratch, sizeof(scratch));
    el_status_ignore(el_read_exact(ring_pipe_as_reader(&state.pipe), scratch, sizeof(scratch), &n));
}

static el_reader reader_with_content(void *ctx, const uint8_t *content, size_t len)
{
    (void)ctx;
    start_mid_storage();
    fill(content, len);
    ring_pipe_close_write(&state.pipe);
    return ring_pipe_as_reader(&state.pipe);
}

static el_writer writer_with_room(void *ctx, size_t room)
{
    (void)ctx;
    (void)room; /* Never more than the storage holds. */
    start_mid_storage();
    return ring_pipe_as_writer(&state.pipe);
}

static size_t writer_take(void *ctx, uint8_t *out, size_t cap)
{
    (void)ctx;
    size_t total = 0;
    while (total < cap) {
        size_t n = 0;
        if (el_read(ring_pipe_as_reader(&state.pipe), out + total, cap - total, &n) != EL_OK) {
            break;
        }
        total += n;
    }
    return total;
}

static el_reader reader_stalled(void *ctx)
{
    (void)ctx;
    /* Empty, and the write side is still open. */
    ring_pipe_init(&state.pipe, state.storage, sizeof(state.storage));
    return ring_pipe_as_reader(&state.pipe);
}

static el_writer writer_stalled(void *ctx)
{
    (void)ctx;
    /* Full, and nobody is reading. */
    ring_pipe_init(&state.pipe, state.storage, 16);
    fill(state.storage, 16);
    return ring_pipe_as_writer(&state.pipe);
}

/* A ring pipe stalls when full: it neither ends nor reports EL_ERR_NO_SPACE
 * after an exact byte count, so writer_ending_after and writer_full_after
 * are absent and the suite skips those tests. */
const el_io_contract_fixture ring_pipe_fixture = {
    .name = "ring_pipe (example backend)",
    .ctx = NULL,
    .reset = reset,
    .reader_with_content = reader_with_content,
    .writer_with_room = writer_with_room,
    .writer_take = writer_take,
    .reader_stalled = reader_stalled,
    .writer_stalled = writer_stalled,
    .writer_ending_after = NULL,
    .writer_full_after = NULL,
};
