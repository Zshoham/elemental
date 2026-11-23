#include "elemental_config.h"

#include <elemental/alloc_arena.h>
#include <elemental/assert.h>

/*
 * Both arenas share one allocation path and one ownership check. They are
 * separate types so that which one a caller holds, and so what its free
 * does, is visible in the code that holds it. The shared helpers take the
 * fields, so neither type is accessed through the other.
 */

/* Allocate from the top: align it, then take `size` bytes. Masks, not
 * division: `align` is a power of two (checked by el_alloc). */
static void *arena_take(uint8_t *base, size_t cap, size_t *used, el_layout layout)
{
    const size_t size = layout.size;
    const size_t align = layout.align;
    EL_ASSERT(*used <= cap);

    const size_t remaining = cap - *used;
    if (size > remaining) {
        return NULL; /* Also covers an arena with no storage: base may be null. */
    }
    const uintptr_t top = (uintptr_t)(base + *used);
    const size_t pad = (size_t)(-top & (uintptr_t)(align - 1));
    if (pad > remaining - size) {
        return NULL;
    }
    uint8_t *const ptr = base + *used + pad;
    *used += pad + size;
    return ptr;
}

/*
 * Where `ptr`, at or after `base`, is from it. Computed on uintptr_t: a
 * pointer difference is a ptrdiff_t, which storage larger than PTRDIFF_MAX
 * would overflow.
 */
static size_t arena_offset(const uint8_t *base, const void *ptr)
{
    return (size_t)((uintptr_t)ptr - (uintptr_t)base);
}

/* [ptr, ptr + size) lies inside the first `used` bytes at `base`. */
static int arena_owns(const uint8_t *base, size_t used, const void *ptr, size_t size)
{
    const uintptr_t start = (uintptr_t)base;
    const uintptr_t at = (uintptr_t)ptr;
    return at >= start && at - start <= used && size <= used - arena_offset(base, ptr);
}

/*
 * Change the size of the allocation [ptr, ptr + layout.size). The one that
 * ends at the top is the latest: it grows while the storage has room, and
 * shrinking it moves the top back. Any other shrinks in place, giving
 * nothing back, and cannot grow: NULL, and el_realloc() moves it. The
 * address never changes, so the alignment holds.
 */
static void *arena_resize(uint8_t *base, size_t cap, size_t *used, void *ptr, el_layout layout,
                          size_t new_size)
{
    EL_ASSERT(arena_owns(base, *used, ptr, layout.size));

    const size_t offset = arena_offset(base, ptr);
    if (offset + layout.size == *used) {
        if (new_size > cap - offset) {
            return NULL;
        }
        *used = offset + new_size;
        return ptr;
    }
    return new_size < layout.size ? ptr : NULL;
}

/* ---- One-shot ----------------------------------------------------------- */

static void *bump_alloc(void *ctx, el_layout layout)
{
    el_bump_arena *const arena = ctx;
    return arena_take(arena->base, arena->cap, &arena->used, layout);
}

static void bump_free(void *ctx, void *ptr, el_layout layout)
{
    const el_bump_arena *const arena = ctx;
    EL_ASSERT(arena_owns(arena->base, arena->used, ptr, layout.size));
}

static void *bump_realloc(void *ctx, void *ptr, el_layout layout, size_t new_size)
{
    el_bump_arena *const arena = ctx;
    return arena_resize(arena->base, arena->cap, &arena->used, ptr, layout, new_size);
}

static const el_allocator_ops bump_ops = {
    .alloc = bump_alloc,
    .free = bump_free,
    .realloc = bump_realloc,
};

void el_bump_arena_init(el_bump_arena *arena, void *storage, size_t cap)
{
    EL_ASSERT(arena != NULL);
    EL_ASSERT(storage != NULL || cap == 0);
    arena->base = storage;
    arena->cap = cap;
    arena->used = 0;
}

el_allocator el_bump_arena_as_allocator(el_bump_arena *arena)
{
    EL_ASSERT(arena != NULL);
    return (el_allocator){.ctx = arena, .ops = &bump_ops};
}

void el_bump_arena_reset(el_bump_arena *arena)
{
    EL_ASSERT(arena != NULL);
    arena->used = 0;
}

/* ---- Last in, first out ------------------------------------------------- */

static void *stack_alloc(void *ctx, el_layout layout)
{
    el_stack_arena *const arena = ctx;
    return arena_take(arena->base, arena->cap, &arena->used, layout);
}

static void stack_free(void *ctx, void *ptr, el_layout layout)
{
    el_stack_arena *const arena = ctx;
    EL_ASSERT(arena_owns(arena->base, arena->used, ptr, layout.size));

    /* The most recent allocation ends at the top. Anything else: no-op. */
    const size_t offset = arena_offset(arena->base, ptr);
    if (offset + layout.size == arena->used) {
        arena->used = offset;
    }
}

static void *stack_realloc(void *ctx, void *ptr, el_layout layout, size_t new_size)
{
    el_stack_arena *const arena = ctx;
    return arena_resize(arena->base, arena->cap, &arena->used, ptr, layout, new_size);
}

static const el_allocator_ops stack_ops = {
    .alloc = stack_alloc,
    .free = stack_free,
    .realloc = stack_realloc,
};

void el_stack_arena_init(el_stack_arena *arena, void *storage, size_t cap)
{
    EL_ASSERT(arena != NULL);
    EL_ASSERT(storage != NULL || cap == 0);
    arena->base = storage;
    arena->cap = cap;
    arena->used = 0;
}

el_allocator el_stack_arena_as_allocator(el_stack_arena *arena)
{
    EL_ASSERT(arena != NULL);
    return (el_allocator){.ctx = arena, .ops = &stack_ops};
}

void el_stack_arena_reset(el_stack_arena *arena)
{
    EL_ASSERT(arena != NULL);
    arena->used = 0;
}
