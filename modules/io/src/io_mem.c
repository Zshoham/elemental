#include "elemental_config.h"

#include <elemental/assert.h>
#include <elemental/io_mem.h>

/*
 * memcpy() is this module's only runtime dependency. A freestanding
 * toolchain is not required to ship <string.h>, but GCC and Clang both
 * require the environment to provide memcpy() itself, so declaring it here
 * asks for nothing the compiler does not already assume.
 */
#if __STDC_HOSTED__
#include <string.h>
#else
void *memcpy(void *dest, const void *src, size_t n);
#endif

static size_t min_size(size_t a, size_t b)
{
    return a < b ? a : b;
}

/* ---- Reader ------------------------------------------------------------- */

static el_status mem_read(void *ctx, void *buf, size_t cap, size_t *n)
{
    el_mem_reader *const mr = ctx;
    EL_ASSERT(mr->pos <= mr->len);

    const size_t remaining = mr->len - mr->pos;
    if (remaining == 0) {
        return EL_ERR_END_OF_STREAM;
    }

    const size_t count = min_size(cap, remaining);
    memcpy(buf, mr->data + mr->pos, count);
    mr->pos += count;
    *n = count;
    return EL_OK;
}

static const el_reader_ops mem_reader_ops = {
    .read = mem_read,
};

void el_mem_reader_init(el_mem_reader *mr, const void *data, size_t len)
{
    EL_ASSERT(mr != NULL);
    EL_ASSERT(data != NULL || len == 0);

    mr->data = data;
    mr->len = len;
    mr->pos = 0;
}

el_reader el_mem_reader_as_reader(el_mem_reader *mr)
{
    EL_ASSERT(mr != NULL);

    const el_reader reader = {.ctx = mr, .ops = &mem_reader_ops};
    return reader;
}

size_t el_mem_reader_remaining(const el_mem_reader *mr)
{
    EL_ASSERT(mr != NULL);
    EL_ASSERT(mr->pos <= mr->len);

    return mr->len - mr->pos;
}

/* ---- Writer ------------------------------------------------------------- */

static el_status mem_write(void *ctx, const void *data, size_t len, size_t *n)
{
    el_mem_writer *const mw = ctx;
    EL_ASSERT(mw->len <= mw->cap);

    const size_t space = mw->cap - mw->len;
    if (space == 0) {
        return EL_ERR_END_OF_STREAM;
    }

    const size_t count = min_size(len, space);
    memcpy(mw->buf + mw->len, data, count);
    mw->len += count;
    *n = count;
    return EL_OK;
}

static const el_writer_ops mem_writer_ops = {
    .write = mem_write,
};

void el_mem_writer_init(el_mem_writer *mw, void *buf, size_t cap)
{
    EL_ASSERT(mw != NULL);
    EL_ASSERT(buf != NULL || cap == 0);

    mw->buf = buf;
    mw->cap = cap;
    mw->len = 0;
}

el_writer el_mem_writer_as_writer(el_mem_writer *mw)
{
    EL_ASSERT(mw != NULL);

    const el_writer writer = {.ctx = mw, .ops = &mem_writer_ops};
    return writer;
}

size_t el_mem_writer_len(const el_mem_writer *mw)
{
    EL_ASSERT(mw != NULL);
    EL_ASSERT(mw->len <= mw->cap);

    return mw->len;
}
