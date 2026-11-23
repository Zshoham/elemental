#include "elemental_config.h"

#include <elemental/assert.h>
#include <elemental/io_alloc.h>

/* The smallest capacity a buffer grows to, so small appends do not each
 * reallocate. */
#define GROW_MIN 16u

/* Change the storage to `cap` bytes with el_realloc(): in place where the
 * allocator can, otherwise moved, with the old capacity copied. Returns 0
 * if refused, and the buffer is then unchanged. */
static int grow_to(el_grow_buf *g, size_t cap)
{
    uint8_t *const data = el_realloc(g->allocator, g->buf.data, el_layout_make(g->buf.cap, 1), cap);
    if (data == NULL) {
        return 0;
    }
    g->buf.data = data;
    g->buf.cap = cap;
    return 1;
}

/*
 * The el_buf growth hook. el_buf_reserve() calls it with room < min and
 * len + min representable. `b` is the first member of an el_grow_buf, so
 * the conversion back is valid (C11 6.7.2.1p15).
 */
static el_status grow(el_buf *b, size_t min)
{
    el_grow_buf *const g = (el_grow_buf *)b;
    EL_ASSERT(g->allocator.ops != NULL);

    const size_t needed = b->len + min;
    size_t target = b->cap <= SIZE_MAX / 2 ? b->cap * 2 : SIZE_MAX;
    if (target < needed) {
        target = needed;
    }
    if (target < GROW_MIN) {
        target = GROW_MIN;
    }
    /* Refused: ask once more for exactly what is needed, so a bounded
     * allocator is not refused for the sake of slack. */
    if (grow_to(g, target) || (needed < target && grow_to(g, needed))) {
        return EL_OK;
    }
    return EL_ERR_NO_SPACE;
}

el_buf *el_grow_buf_init(el_grow_buf *g, el_allocator allocator)
{
    EL_ASSERT(g != NULL);
    EL_ASSERT(allocator.ops != NULL);
    g->allocator = allocator;
    g->buf.data = NULL;
    g->buf.len = 0;
    g->buf.cap = 0;
    g->buf.grow = grow;
    return &g->buf;
}

void el_grow_buf_deinit(el_grow_buf *g)
{
    EL_ASSERT(g != NULL);
    EL_ASSERT(g->allocator.ops != NULL); /* not deinitialized already */
    el_free(g->allocator, g->buf.data, el_layout_make(g->buf.cap, 1));
    g->allocator.ops = NULL;
    g->allocator.ctx = NULL;
    g->buf.data = NULL;
    g->buf.len = 0;
    g->buf.cap = 0;
    g->buf.grow = NULL;
}
