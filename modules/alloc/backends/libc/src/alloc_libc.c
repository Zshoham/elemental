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
static void *libc_alloc(void *ctx, size_t size, size_t align)
{
    (void)ctx;
    if (align <= _Alignof(max_align_t)) {
        return malloc(size);
    }
    if (size > SIZE_MAX - (align - 1)) {
        return NULL;
    }
    const size_t rounded = (size + (align - 1)) & ~(align - 1);
    return aligned_alloc(align, rounded);
}

static void libc_free(void *ctx, void *ptr, size_t size, size_t align)
{
    (void)ctx;
    (void)size;
    (void)align;
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
