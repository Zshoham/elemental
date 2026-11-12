#include "elemental_config.h"

#include <elemental/assert.h>
#include <elemental/io_alloc.h>

/* As in io_mem.c: memcpy() is required of every environment by the compiler. */
#if __STDC_HOSTED__
#include <string.h>
#else
void *memcpy(void *dest, const void *src, size_t n);
#endif

/* The smallest buffer a writer grows to, so small writes do not each reallocate. */
#define GROW_MIN 16u

static el_grow_writer *checked(void *ctx)
{
    el_grow_writer *const gw = ctx;
    EL_ASSERT(gw->allocator.ops != NULL); /* not used after deinit */
    return gw;
}

/* Move the contents into a buffer of `cap` bytes. Returns 0 if refused. */
static int grow_to(el_grow_writer *gw, size_t cap)
{
    uint8_t *const buf = el_alloc(gw->allocator, cap, 1);
    if (buf == NULL) {
        return 0;
    }
    if (gw->len != 0) {
        memcpy(buf, gw->buf, gw->len);
    }
    el_free(gw->allocator, gw->buf, gw->cap, 1);
    gw->buf = buf;
    gw->cap = cap;
    return 1;
}

static el_status grow_write(void *ctx, const void *data, size_t len, size_t *n)
{
    el_grow_writer *const gw = checked(ctx);
    EL_ASSERT(gw->len <= gw->cap);

    if (len > gw->cap - gw->len) {
        /* Twice the capacity, or what this write needs if that is more. */
        const int needed_fits = len <= SIZE_MAX - gw->len;
        const size_t needed = needed_fits ? gw->len + len : SIZE_MAX;
        size_t target = gw->cap <= SIZE_MAX / 2 ? gw->cap * 2 : SIZE_MAX;
        if (target < needed) {
            target = needed;
        }
        if (target < GROW_MIN) {
            target = GROW_MIN;
        }
        /* Refused: ask once more for exactly what this write needs, so a
         * bounded allocator is not refused for the sake of slack. */
        if (!grow_to(gw, target) && needed_fits && needed < target) {
            (void)grow_to(gw, needed);
        }
        /* Still full: nothing can be taken. Otherwise take what fits. */
        if (gw->cap == gw->len) {
            return EL_ERR_NO_SPACE;
        }
    }

    const size_t room = gw->cap - gw->len;
    const size_t count = len < room ? len : room;
    memcpy(gw->buf + gw->len, data, count);
    gw->len += count;
    *n = count;
    return EL_OK;
}

static const el_writer_ops grow_writer_ops = {
    .write = grow_write,
};

void el_grow_writer_init(el_grow_writer *gw, el_allocator allocator)
{
    EL_ASSERT(gw != NULL);
    EL_ASSERT(allocator.ops != NULL);
    gw->allocator = allocator;
    gw->buf = NULL;
    gw->len = 0;
    gw->cap = 0;
}

el_writer el_grow_writer_as_writer(el_grow_writer *gw)
{
    EL_ASSERT(gw != NULL);
    return (el_writer){.ctx = checked(gw), .ops = &grow_writer_ops};
}

const uint8_t *el_grow_writer_data(const el_grow_writer *gw)
{
    EL_ASSERT(gw != NULL);
    EL_ASSERT(gw->allocator.ops != NULL);
    return gw->buf;
}

size_t el_grow_writer_len(const el_grow_writer *gw)
{
    EL_ASSERT(gw != NULL);
    EL_ASSERT(gw->allocator.ops != NULL);
    return gw->len;
}

void el_grow_writer_deinit(el_grow_writer *gw)
{
    EL_ASSERT(gw != NULL);
    el_free(checked(gw)->allocator, gw->buf, gw->cap, 1);
    gw->allocator.ops = NULL;
    gw->buf = NULL;
    gw->len = 0;
    gw->cap = 0;
}
