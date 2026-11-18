/*
 * Contract fixture for the ring's reader and writer: how to put a ring in
 * each state the io contract suite asks for. One context drives both sides
 * here; what two contexts do is the stress test's (ring_stress_test.cpp).
 */
#include "fixture_ring.h"

#include <elemental/ring.h>

static struct {
    el_ring ring;
    uint8_t storage[EL_IO_CONTRACT_SIZE_MAX];
} state;

static void reset(void *ctx)
{
    (void)ctx;
    el_ring_init(&state.ring, state.storage, 0);
}

/*
 * A fresh ring, emptied again after some traffic, so that the streams the
 * suite asks for start in the middle of the storage and wrap around its end.
 * Without this the suite would only ever exercise the unwrapped case.
 */
static void start_mid_storage(void)
{
    uint8_t scratch[100] = {0};

    el_ring_init(&state.ring, state.storage, sizeof(state.storage));
    (void)el_ring_write(&state.ring, scratch, sizeof(scratch));
    (void)el_ring_read(&state.ring, scratch, sizeof(scratch));
}

static el_reader reader_with_content(void *ctx, const uint8_t *content, size_t len)
{
    (void)ctx;
    start_mid_storage();
    (void)el_ring_write(&state.ring, content, len);
    el_ring_close(&state.ring);
    return el_ring_as_reader(&state.ring);
}

static el_writer writer_with_room(void *ctx, size_t room)
{
    (void)ctx;
    (void)room; /* Never more than the storage holds. */
    start_mid_storage();
    return el_ring_as_writer(&state.ring);
}

static size_t writer_take(void *ctx, uint8_t *out, size_t cap)
{
    (void)ctx;
    return el_ring_read(&state.ring, out, cap);
}

static el_reader reader_stalled(void *ctx)
{
    (void)ctx;
    /* Empty, and the producer has not closed. */
    el_ring_init(&state.ring, state.storage, sizeof(state.storage));
    return el_ring_as_reader(&state.ring);
}

static el_writer writer_stalled(void *ctx)
{
    (void)ctx;
    /* Full, and nobody is reading. */
    static const uint8_t fill[16] = {0};
    el_ring_init(&state.ring, state.storage, sizeof(fill));
    (void)el_ring_write(&state.ring, fill, sizeof(fill));
    return el_ring_as_writer(&state.ring);
}

/* A ring stalls when full: it neither ends nor reports EL_ERR_NO_SPACE
 * after an exact byte count, so writer_ending_after and writer_full_after
 * are absent and the suite skips those tests. */
const el_io_contract_fixture ring_fixture = {
    .name = "ring",
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
