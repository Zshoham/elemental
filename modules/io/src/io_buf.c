#include "elemental_config.h"

#include <elemental/assert.h>
#include <elemental/io_buf.h>

#include "io_private.h"

/*
 * memcpy() and memmove() are this file's runtime dependencies. A
 * freestanding toolchain is not required to ship <string.h>, but GCC and
 * Clang both require the environment to provide these two, so declaring
 * them asks for nothing the compiler does not already assume.
 */
#if __STDC_HOSTED__
#include <string.h>
#else
void *memcpy(void *dest, const void *src, size_t n);
void *memmove(void *dest, const void *src, size_t n);
#endif

static int is_valid(const el_buf *b)
{
    return b->len <= b->cap && (b->data != NULL || b->cap == 0);
}

/* True if `p` points into the buffer's storage. Compared as integers: the
 * relational operators are only defined within one object. */
static int points_into(const el_buf *b, const void *p)
{
    const uintptr_t start = (uintptr_t)b->data;
    const uintptr_t at = (uintptr_t)p;
    return b->data != NULL && p != NULL && at >= start && at - start < b->cap;
}

void el_buf_init(el_buf *b, void *storage, size_t cap)
{
    EL_ASSERT(b != NULL);
    EL_ASSERT(storage != NULL || cap == 0);

    b->data = storage;
    b->len = 0;
    b->cap = cap;
    b->grow = NULL;
}

const uint8_t *el_buf_data(const el_buf *b)
{
    EL_ASSERT(b != NULL);
    EL_ASSERT(is_valid(b));
    return b->data;
}

uint8_t *el_buf_data_mut(el_buf *b)
{
    EL_ASSERT(b != NULL);
    EL_ASSERT(is_valid(b));
    return b->data;
}

size_t el_buf_len(const el_buf *b)
{
    EL_ASSERT(b != NULL);
    EL_ASSERT(is_valid(b));
    return b->len;
}

size_t el_buf_room(const el_buf *b)
{
    EL_ASSERT(b != NULL);
    EL_ASSERT(is_valid(b));
    return b->cap - b->len;
}

el_status el_buf_reserve(el_buf *b, size_t min)
{
    EL_ASSERT(b != NULL);
    EL_ASSERT(is_valid(b));

    if (min <= b->cap - b->len) {
        return EL_OK;
    }
    /* Fixed storage cannot grow, and a request past SIZE_MAX cannot be met. */
    if (b->grow == NULL || min > SIZE_MAX - b->len) {
        return EL_ERR_NO_SPACE;
    }
    const el_status status = b->grow(b, min);
    EL_ASSERT(status == EL_OK || status == EL_ERR_NO_SPACE);
    EL_ASSERT(is_valid(b));
    EL_ASSERT(status != EL_OK || min <= b->cap - b->len);
    return status;
}

el_status el_buf_append(el_buf *b, const void *data, size_t len)
{
    EL_ASSERT(b != NULL);
    EL_ASSERT(data != NULL || len == 0);
    EL_ASSERT(!points_into(b, data));

    if (len == 0) {
        return EL_OK;
    }
    EL_TRY(el_buf_reserve(b, len));
    memcpy(b->data + b->len, data, len);
    b->len += len;
    return EL_OK;
}

uint8_t *el_buf_spare(el_buf *b)
{
    EL_ASSERT(b != NULL);
    EL_ASSERT(is_valid(b));
    return b->data != NULL ? b->data + b->len : NULL;
}

void el_buf_commit(el_buf *b, size_t n)
{
    EL_ASSERT(b != NULL);
    EL_ASSERT(is_valid(b));
    EL_ASSERT(n <= b->cap - b->len);
    b->len += n;
}

void el_buf_truncate(el_buf *b, size_t len)
{
    EL_ASSERT(b != NULL);
    EL_ASSERT(is_valid(b));
    EL_ASSERT(len <= b->len);
    b->len = len;
}

el_status el_buf_append_some_(el_buf *b, const void *data, size_t len, size_t *n)
{
    EL_ASSERT(!points_into(b, data));

    /* Room for all of it if the storage can grow; if it cannot, or is
     * refused, what is left is taken. */
    el_status_ignore(el_buf_reserve(b, len));
    const size_t room = b->cap - b->len;
    if (room == 0) {
        return EL_ERR_NO_SPACE;
    }
    const size_t count = len < room ? len : room;
    memcpy(b->data + b->len, data, count);
    b->len += count;
    *n = count;
    return EL_OK;
}

void el_buf_drop_front_(el_buf *b, size_t n)
{
    EL_ASSERT(n <= b->len);
    if (n == 0) {
        return;
    }
    b->len -= n;
    if (b->len != 0) {
        memmove(b->data, b->data + n, b->len);
    }
}

static el_status buf_write(void *ctx, const void *data, size_t len, size_t *n)
{
    el_buf *const b = ctx;
    EL_ASSERT(is_valid(b));
    return el_buf_append_some_(b, data, len, n);
}

static const el_writer_ops buf_writer_ops = {
    .write = buf_write,
};

el_writer el_buf_as_writer(el_buf *b)
{
    EL_ASSERT(b != NULL);
    const el_writer writer = {.ctx = b, .ops = &buf_writer_ops};
    return writer;
}
