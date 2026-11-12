#include "elemental_config.h"

#include <elemental/alloc_libc.h>

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

/*
 * malloc() already aligns to max_align_t. Above that, C11 aligned_alloc()
 * requires a size that is a multiple of the alignment (C17 relaxed this;
 * rounding is correct under both), and returns NULL for an alignment the
 * implementation does not support.
 */
static void *libc_alloc(void *ctx, el_layout layout)
{
    (void)ctx;
    if (layout.align <= _Alignof(max_align_t)) {
        return malloc(layout.size);
    }
    if (layout.size > SIZE_MAX - (layout.align - 1)) {
        return NULL;
    }
    const size_t rounded = (layout.size + (layout.align - 1)) & ~(layout.align - 1);
    return aligned_alloc(layout.align, rounded);
}

static void libc_free(void *ctx, void *ptr, el_layout layout)
{
    (void)ctx;
    (void)layout;
    free(ptr);
}

static const el_allocator_ops libc_ops = {
    .alloc = libc_alloc,
    .free = libc_free,
};

el_allocator el_libc_allocator(void)
{
    return (el_allocator){.ctx = NULL, .ops = &libc_ops};
}
