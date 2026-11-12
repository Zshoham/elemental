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

/* [ptr, ptr + size) lies inside the first `used` bytes at `base`. */
static int arena_owns(const uint8_t *base, size_t used, const void *ptr, size_t size)
{
    const uintptr_t start = (uintptr_t)base;
    const uintptr_t at = (uintptr_t)ptr;
    return at >= start && at - start <= used && size <= used - (size_t)(at - start);
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

static const el_allocator_ops bump_ops = {
    .alloc = bump_alloc,
    .free = bump_free,
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
    const size_t offset = (size_t)((uint8_t *)ptr - arena->base);
    if (offset + layout.size == arena->used) {
        arena->used = offset;
    }
}

static const el_allocator_ops stack_ops = {
    .alloc = stack_alloc,
    .free = stack_free,
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
