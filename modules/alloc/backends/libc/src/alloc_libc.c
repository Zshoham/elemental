#include "elemental_config.h"

#include <elemental/alloc_libc.h>

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

/*
 * The bytes asked of malloc() or realloc() for `size` bytes aligned to
 * `align`, at most _Alignof(max_align_t): at least `align`. A C library
 * may align a small block only as far as its size needs (a weak-alignment
 * heap, such as jemalloc's: WG14 N2293), so 1 byte asked for at alignment
 * 16 could come back at 8. The heaps N2293 describes align a block of at
 * least `align` bytes to `align`. Only the size the caller asked for is
 * used.
 */
static size_t fundamental_size(size_t size, size_t align)
{
    return size < align ? align : size;
}

/*
 * malloc() aligns to max_align_t a block large enough for it. Above that,
 * C11 aligned_alloc() requires a size that is a multiple of the alignment
 * (C17 relaxed this; rounding is correct under both), and returns NULL for
 * an alignment the implementation does not support.
 */
static void *libc_alloc(void *ctx, el_layout layout)
{
    (void)ctx;
    if (layout.align <= _Alignof(max_align_t)) {
        return malloc(fundamental_size(layout.size, layout.align));
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

/*
 * C's realloc() serves what malloc() served, with the same floor: a block
 * shrunk below its alignment is still asked for at least that many bytes,
 * so that the result is aligned however the heap aligns small blocks. It
 * has no aligned form, so a larger alignment is NULL, and el_realloc()
 * allocates, copies and frees. A realloc() that fails leaves the block as
 * it was, as the operation must.
 */
static void *libc_realloc(void *ctx, void *ptr, el_layout layout, size_t new_size)
{
    (void)ctx;
    if (layout.align > _Alignof(max_align_t)) {
        return NULL;
    }
    return realloc(ptr, fundamental_size(new_size, layout.align));
}

static const el_allocator_ops libc_ops = {
    .alloc = libc_alloc,
    .free = libc_free,
    .realloc = libc_realloc,
};

el_allocator el_libc_allocator(void)
{
    return (el_allocator){.ctx = NULL, .ops = &libc_ops};
}
