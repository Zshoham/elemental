#include "ring_pipe.h"

/*
 * The operations below rely on what el_read() and el_write() guarantee
 * before calling them: the buffer is not null, the size is at least 1, and
 * *n starts at 0. So there is no argument checking here, and a path that
 * moves no bytes only has to return its status.
 */

static size_t min_size(size_t a, size_t b)
{
    return a < b ? a : b;
}

static el_status ring_read(void *ctx, void *buf, size_t cap, size_t *n)
{
    ring_pipe *const pipe = ctx;

    if (pipe->count == 0) {
        return pipe->write_closed ? EL_ERR_END_OF_STREAM : EL_ERR_WOULD_BLOCK;
    }

    const size_t count = min_size(cap, pipe->count);
    uint8_t *const out = buf;
    for (size_t i = 0; i < count; i++) {
        out[i] = pipe->storage[pipe->head];
        pipe->head = pipe->head + 1 == pipe->capacity ? 0 : pipe->head + 1;
    }
    pipe->count -= count;

    *n = count;
    return EL_OK;
}

static el_status ring_write(void *ctx, const void *data, size_t len, size_t *n)
{
    ring_pipe *const pipe = ctx;

    if (pipe->write_closed) {
        return EL_ERR_END_OF_STREAM;
    }
    const size_t space = pipe->capacity - pipe->count;
    if (space == 0) {
        return EL_ERR_WOULD_BLOCK;
    }

    /* Copy, do not keep `data`: it is the caller's again when we return. */
    const size_t count = min_size(len, space);
    const uint8_t *const in = data;
    size_t tail = pipe->head + pipe->count;
    if (tail >= pipe->capacity) {
        tail -= pipe->capacity;
    }
    for (size_t i = 0; i < count; i++) {
        pipe->storage[tail] = in[i];
        tail = tail + 1 == pipe->capacity ? 0 : tail + 1;
    }
    pipe->count += count;

    *n = count;
    return EL_OK;
}

/* One table per interface, static const: handles point at it forever. */
static const el_reader_ops ring_reader_ops = {.read = ring_read};
static const el_writer_ops ring_writer_ops = {.write = ring_write};

void ring_pipe_init(ring_pipe *pipe, uint8_t *storage, size_t capacity)
{
    pipe->storage = storage;
    pipe->capacity = capacity;
    pipe->head = 0;
    pipe->count = 0;
    pipe->write_closed = false;
}

void ring_pipe_close_write(ring_pipe *pipe)
{
    pipe->write_closed = true;
}

el_reader ring_pipe_as_reader(ring_pipe *pipe)
{
    const el_reader reader = {.ctx = pipe, .ops = &ring_reader_ops};
    return reader;
}

el_writer ring_pipe_as_writer(ring_pipe *pipe)
{
    const el_writer writer = {.ctx = pipe, .ops = &ring_writer_ops};
    return writer;
}
