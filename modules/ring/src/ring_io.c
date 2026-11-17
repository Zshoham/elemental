/*
 * The ring's two sides as io streams. Its own object, so a program that
 * only calls el_ring_read() and el_ring_write() links no operation table.
 */
#include "elemental_config.h"

#include <elemental/assert.h>
#include <elemental/ring.h>

#include <stdatomic.h>

static el_status ring_read_op(void *ctx, void *buf, size_t cap, size_t *n)
{
    el_ring *const ring = ctx;
    /* Before the bytes. The producer stores `closed` after the last of
     * them, so a ring seen closed and then empty has ended; one seen empty
     * and then closed may hold bytes written in between. */
    const size_t closed = atomic_load_explicit(&ring->closed, memory_order_acquire);
    *n = el_ring_read(ring, buf, cap);
    if (*n > 0) {
        return EL_OK;
    }
    return closed != 0 ? EL_ERR_END_OF_STREAM : EL_ERR_WOULD_BLOCK;
}

static el_status ring_write_op(void *ctx, const void *data, size_t len, size_t *n)
{
    el_ring *const ring = ctx;
    /* The producer's own word. */
    if (atomic_load_explicit(&ring->closed, memory_order_relaxed) != 0) {
        return EL_ERR_END_OF_STREAM;
    }
    *n = el_ring_write(ring, data, len);
    return *n > 0 ? EL_OK : EL_ERR_WOULD_BLOCK;
}

static const el_reader_ops ring_reader_ops = {.read = ring_read_op};
static const el_writer_ops ring_writer_ops = {.write = ring_write_op};

el_reader el_ring_as_reader(el_ring *ring)
{
    EL_ASSERT(ring != NULL);
    return (el_reader){.ctx = ring, .ops = &ring_reader_ops};
}

el_writer el_ring_as_writer(el_ring *ring)
{
    EL_ASSERT(ring != NULL);
    return (el_writer){.ctx = ring, .ops = &ring_writer_ops};
}
