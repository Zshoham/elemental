/*
 * elemental/alloc_arena.h - allocators over caller-provided storage.
 *
 * Two arenas. Both carve allocations from one block of memory the caller
 * owns, in order, with no per-allocation header, and both give everything
 * back at once with a reset. They differ only in what el_free() does:
 *
 *   el_bump_arena    One-shot. Freeing gives nothing back; memory returns
 *                    only through el_bump_arena_reset(). For storage whose
 *                    lifetime is one request, one frame, one message.
 *
 *   el_stack_arena   Last in, first out. Freeing the most recent allocation
 *                    gives its bytes back; freeing any other does nothing.
 *                    For scratch memory that is freed in reverse order, such
 *                    as a temporary inside a function that allocates.
 *
 * STATUS: experimental. See docs/process/api-stability.md.
 *
 * What the stack arena recognizes as "most recent": the allocation whose
 * end is the arena's current top. Freeing it moves the top back to where
 * that allocation starts. The padding inserted before an allocation to
 * align it is not given back, so after freeing the most recent allocation,
 * the one before it is recognized only if no padding separated them. Frees
 * in reverse order therefore give back everything when all requests share
 * one alignment and their sizes are multiples of it, and may stop early
 * otherwise. A free that is not recognized is a no-op; the memory comes
 * back at the next reset.
 *
 * Changing an allocation's size (el_realloc(), whose operation both arenas
 * have, and which acts alike in both): the latest allocation, the one that
 * ends at the arena's top, grows in place while the storage has room and,
 * when shrunk, gives the tail back, in the bump arena too. Any other
 * allocation shrinks in place and gives nothing back; asked to grow, the
 * operation returns NULL, and el_realloc() allocates a new one, copies and
 * frees the old.
 *
 * An exception to the reverse-order promise above: an allocation shrunk
 * while it was not the latest keeps its old end, so freeing it later, even
 * last in reverse order, is not recognized and gives nothing back until the
 * reset. (Two allocations of 16 bytes, the first shrunk to 8: after the
 * second is freed the top is at 16, but the first now ends at 8.)
 *
 * Storage and ownership:
 *
 *   - The caller provides the arena object and the storage, and keeps both
 *     alive while the arena, or any handle or allocation made from it, is in
 *     use. The arena borrows the storage; it never frees it.
 *   - An arena holds no resource, so there is no deinit. A reset ends every
 *     allocation at once.
 *   - Allocations point into the storage, not into the arena object, so the
 *     object may be moved while no handle made from it is in use.
 *
 * Exhaustion: a request that does not fit in the remaining storage, after
 * the padding its alignment needs, returns NULL and changes nothing.
 *
 * The struct layouts are public so that callers can allocate them. The
 * fields are not part of the API: use the functions.
 *
 * Concurrency: no internal synchronization. One arena may be used from one
 * context at a time. Nothing here blocks or takes a lock, so an arena that
 * only one interrupt handler uses is safe there. (docs/conventions.md does
 * not call that "ISR-safe", because its definition excludes allocating.)
 *
 * Contract violations, checked by assertions: a null arena; null storage
 * with a nonzero capacity; freeing or resizing a range that is not inside
 * what the arena has handed out since its last reset, which includes
 * freeing after a reset and, for the stack arena, freeing the most recent
 * allocation twice.
 */
#ifndef ELEMENTAL_ALLOC_ARENA_H
#define ELEMENTAL_ALLOC_ARENA_H

#include <stddef.h>
#include <stdint.h>

#include <elemental/alloc.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- One-shot ----------------------------------------------------------- */

typedef struct el_bump_arena {
    uint8_t *base;
    size_t cap;
    size_t used;
} el_bump_arena;

/*
 * Start an arena over the `cap` bytes at `storage`. `storage` may be null
 * only if `cap` is 0, which makes an arena that refuses every request.
 */
void el_bump_arena_init(el_bump_arena *arena, void *storage, size_t cap);

/* An allocator handle for `arena`. Its free checks ownership and does nothing more. */
el_allocator el_bump_arena_as_allocator(el_bump_arena *arena);

/* End every allocation made from `arena`; its whole storage is available again. */
void el_bump_arena_reset(el_bump_arena *arena);

/* ---- Last in, first out ------------------------------------------------- */

typedef struct el_stack_arena {
    uint8_t *base;
    size_t cap;
    size_t used;
} el_stack_arena;

/*
 * Start an arena over the `cap` bytes at `storage`. `storage` may be null
 * only if `cap` is 0, which makes an arena that refuses every request.
 */
void el_stack_arena_init(el_stack_arena *arena, void *storage, size_t cap);

/* An allocator handle for `arena`. Freeing the most recent allocation gives it back. */
el_allocator el_stack_arena_as_allocator(el_stack_arena *arena);

/* End every allocation made from `arena`; its whole storage is available again. */
void el_stack_arena_reset(el_stack_arena *arena);

#ifdef __cplusplus
}
#endif

#endif /* ELEMENTAL_ALLOC_ARENA_H */
