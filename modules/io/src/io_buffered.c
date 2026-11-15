#include "elemental_config.h"

#include <stdint.h>

#include <elemental/assert.h>
#include <elemental/io_buffered.h>

#include "io_private.h"

/* pos holds this once the inner writer has reported EL_ERR_END_OF_STREAM: a
 * value it cannot otherwise hold, since pos <= len <= cap. The one fact a
 * buffered writer remembers about its inner writer (decision 17 of
 * docs/proposals/io-buffers-fmt.md). */
#define ENDED SIZE_MAX

static int ended(const el_buffered_writer *w)
{
    return w->pos == ENDED;
}

/* The cheap part of the borrowing rule: the buffer still holds what was
 * passed on. */
static void check(const el_buffered_writer *w)
{
    EL_ASSERT(ended(w) || w->pos <= el_buf_len(w->buf));
}

static size_t pending(const el_buffered_writer *w)
{
    return w->buf->len - w->pos;
}

/* After the inner writer took bytes: when none are pending, both go back to
 * 0 without moving anything. */
static void passed_on(el_buffered_writer *w, size_t n)
{
    w->pos += n;
    if (w->pos == w->buf->len) {
        w->pos = 0;
        w->buf->len = 0;
    }
}

/* One attempt to pass pending bytes on. */
static el_status pass_on_once(el_buffered_writer *w)
{
    size_t n = 0;
    const el_status status = el_write(w->inner, w->buf->data + w->pos, pending(w), &n);
    if (status == EL_OK) {
        passed_on(w, n);
    } else if (status == EL_ERR_END_OF_STREAM) {
        w->pos = ENDED;
    }
    return status;
}

/* Move the bytes already passed on out of the front, if any. */
static void compact(el_buffered_writer *w)
{
    if (w->pos != 0) {
        el_buf_drop_front_(w->buf, w->pos);
        w->pos = 0;
    }
}

void el_buffered_writer_init(el_buffered_writer *w, el_writer inner, el_buf *buf)
{
    EL_ASSERT(w != NULL);
    EL_ASSERT(buf != NULL);
    EL_ASSERT(inner.ops != NULL);
    (void)el_buf_len(buf); /* checks the buffer */
    w->buf = buf;
    w->pos = 0;
    w->inner = inner;
}

size_t el_buffered_writer_pending(const el_buffered_writer *w)
{
    EL_ASSERT(w != NULL);
    check(w);
    return ended(w) ? 0 : pending(w);
}

el_status el_buffered_writer_flush(el_buffered_writer *w)
{
    EL_ASSERT(w != NULL);
    check(w);
    if (ended(w)) {
        return EL_ERR_END_OF_STREAM;
    }
    if (pending(w) == 0) {
        passed_on(w, 0); /* back to 0, if the owner rolled back to pos */
        return EL_OK;
    }
    size_t n = 0;
    const el_status status = el_write_all(w->inner, w->buf->data + w->pos, pending(w), &n);
    passed_on(w, n);
    if (status == EL_ERR_END_OF_STREAM) {
        w->pos = ENDED;
    }
    return status;
}

el_status el_buffered_writer_reserve(el_buffered_writer *w, size_t min)
{
    EL_ASSERT(w != NULL);
    check(w);
    if (ended(w)) {
        return EL_ERR_END_OF_STREAM;
    }
    el_buf *const b = w->buf;
    if (min <= b->cap - b->len) {
        return EL_OK;
    }

    if (min > b->cap) {
        /* Draining cannot free enough. Fixed storage fails without passing
         * anything on; grown storage grows, after compacting so that less
         * is copied. */
        if (b->grow == NULL) {
            return EL_ERR_NO_SPACE;
        }
        compact(w);
        return el_buf_reserve(b, min) == EL_OK ? EL_OK : EL_ERR_NO_SPACE;
    }

    /* Draining can: pass on only until compacting would give the room. */
    el_status status = EL_OK;
    while (pending(w) != 0 && b->cap - pending(w) < min) {
        status = pass_on_once(w);
        if (status != EL_OK) {
            break;
        }
    }
    if (status != EL_OK && status != EL_ERR_WOULD_BLOCK) {
        return status;
    }
    if (b->cap - b->len < min) {
        compact(w);
    }
    if (b->cap - b->len >= min) {
        return EL_OK;
    }
    /* Stalled: grown storage grows instead of waiting. */
    return el_buf_reserve(b, min) == EL_OK ? EL_OK : EL_ERR_WOULD_BLOCK;
}

static el_status buffered_write(void *ctx, const void *data, size_t len, size_t *n)
{
    el_buffered_writer *const w = ctx;
    check(w);
    if (ended(w)) {
        return EL_ERR_END_OF_STREAM;
    }
    el_buf *const b = w->buf;

    /* 1. It fits: buffered, the inner writer is not called. */
    if (len <= b->cap - b->len) {
        const el_status fits = el_buf_append(b, data, len);
        EL_ASSERT(fits == EL_OK);
        *n = len;
        return EL_OK;
    }

    int stalled = 0;
    if (pending(w) != 0) {
        /* 2. One attempt to pass pending bytes on. */
        const el_status status = pass_on_once(w);
        if (status == EL_ERR_WOULD_BLOCK) {
            stalled = 1;
        } else if (status != EL_OK) {
            return status;
        }
    } else if (len >= b->cap && !(b->grow != NULL && b->cap == 0)) {
        /* 3. Nothing pending and at least the capacity: straight through. */
        const el_status status = el_write(w->inner, data, len, n);
        if (status == EL_ERR_END_OF_STREAM) {
            w->pos = ENDED;
        }
        if (status != EL_ERR_WOULD_BLOCK) {
            return status;
        }
        stalled = 1;
    }

    /* 4. Make room, then buffer what fits. */
    compact(w);
    if (len > b->cap - b->len) {
        /* Grown storage may give room for all of it; whatever room there
         * is afterwards is used. */
        el_status_ignore(el_buf_reserve(b, len));
    }
    const size_t room = b->cap - b->len;
    if (room == 0) {
        /* Nothing fits. Space returns as the stream drains, if it refused;
         * if it was never asked, the storage could not be had. */
        return stalled ? EL_ERR_WOULD_BLOCK : EL_ERR_NO_SPACE;
    }
    const size_t count = len < room ? len : room;
    const el_status fits = el_buf_append(b, data, count);
    EL_ASSERT(fits == EL_OK);
    *n = count;
    return EL_OK;
}

static const el_writer_ops buffered_writer_ops = {
    .write = buffered_write,
};

el_writer el_buffered_writer_as_writer(el_buffered_writer *w)
{
    EL_ASSERT(w != NULL);
    const el_writer writer = {.ctx = w, .ops = &buffered_writer_ops};
    return writer;
}
