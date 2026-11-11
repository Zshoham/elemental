/*
 * elemental/alloc.h - the allocator interface.
 *
 * An allocator hands out memory and takes it back. Code that needs dynamic
 * storage takes an el_allocator as a parameter, so its caller decides where
 * the memory comes from: an arena over a static array, the C library's heap,
 * or anything else that implements the two operations below.
 *
 * STATUS: experimental. See docs/process/api-stability.md.
 *
 * Exhaustion: el_alloc() and el_alloc_array() return NULL when the request
 * cannot be satisfied: the allocator has run out, the request is larger than
 * it can ever hold, it cannot provide that alignment, or (for the array form)
 * count * size overflows. NULL is not permanent; the same request can
 * succeed after memory is freed. A failed request changes nothing. A
 * function that allocates as part of other work reports this as
 * EL_ERR_NO_SPACE.
 *
 * Sized free: el_free() takes back the size and alignment the allocation
 * was made with (docs/adr/0011-sized-free.md). An allocator may rely on
 * them, so they must be exact. For an array, the size is count * size. The
 * size to free is the size that was allocated: for a growable buffer, its
 * capacity, not its length.
 *
 * Alignment: every request names its alignment, a power of two (1, 2, 4,
 * ...). The returned pointer is a multiple of it. There is no default.
 *
 * Zero-size requests succeed without consulting the allocator. They return
 * a non-null pointer, a multiple of the alignment, that must not be
 * dereferenced and need not be unique. Freeing it, with size 0, does
 * nothing. So an operation is never called with a size of 0.
 *
 * Ownership and lifetime:
 *
 *   - el_allocator is a handle: two pointers, passed by value, owning
 *     nothing. It is valid while the object it was made from is alive and
 *     has not been moved.
 *   - Memory goes back to the allocator that provided it, through el_free()
 *     or by ending that allocator as a whole (an arena's reset). Each
 *     allocator's header says which.
 *   - The allocator must outlive every allocation made from it. A component
 *     that allocates keeps the handle it was given and uses it to free.
 *
 * Concurrency: handles carry no synchronization. One allocator object may be
 * used from one context at a time unless its implementation documents more.
 *
 * Contract violations, checked by assertions when the library is built with
 * them: a handle that was never made; an alignment that is 0 or not a power
 * of two; el_free() with a null pointer and a nonzero size, or with a size
 * of 0 and a pointer other than null or the zero-size pointer; freeing
 * memory the allocator did not provide, twice, or with a different size or
 * alignment. The last group is checked only where an implementation can do
 * so cheaply, and is otherwise undefined behavior, as it is for free().
 */
#ifndef ELEMENTAL_ALLOC_H
#define ELEMENTAL_ALLOC_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Interface ----------------------------------------------------------
 *
 * To implement an allocator, define both operations over your own state and
 * hand out a handle that pairs them with it. The functions below check the
 * caller's arguments before an operation runs and check its result after,
 * so an operation may rely on the "called with" guarantees.
 *
 * Operations are called through C. They must not unwind: no C++ exception,
 * Rust panic, or longjmp may leave them.
 */

typedef struct el_allocator_ops {
    /*
     * Allocate `size` bytes aligned to `align`.
     *
     * Called with: size >= 1; align a power of two.
     * Must: return a pointer to `size` writable bytes that is a multiple of
     *       `align` and overlaps no live allocation; or return NULL and
     *       change nothing.
     */
    void *(*alloc)(void *ctx, size_t size, size_t align);

    /*
     * Take back an allocation.
     *
     * Called with: ptr not null; size >= 1. ptr, size, and align are, if
     *              the caller kept the contract, exactly those of a
     *              successful alloc on this ctx that has not been freed.
     * Must: not fail. It may make the memory available again, or not.
     */
    void (*free)(void *ctx, void *ptr, size_t size, size_t align);
} el_allocator_ops;

/*
 * `ops` must outlive every handle that uses it; make it static const.
 * `ctx` is passed back to the operations unchanged and may be null if the
 * implementation has no state.
 */
typedef struct el_allocator {
    void *ctx;
    const el_allocator_ops *ops;
} el_allocator;

/*
 * Allocate `size` bytes aligned to `align`. Returns NULL if the request
 * cannot be satisfied; see "Exhaustion" above. The contents are
 * unspecified. A `size` of 0 returns the zero-size pointer.
 */
void *el_alloc(el_allocator allocator, size_t size, size_t align);

/*
 * Allocate an array of `count` elements of `size` bytes, aligned to
 * `align`. Returns NULL, without consulting the allocator, if count * size
 * overflows. Use it whenever `count` or `size` comes from input. Free the
 * array with el_free(allocator, ptr, count * size, align); that product
 * cannot overflow once the allocation succeeded.
 */
void *el_alloc_array(el_allocator allocator, size_t count, size_t size, size_t align);

/*
 * Give back `ptr`, which was allocated from `allocator` with exactly this
 * `size` and `align`. `ptr` may be null only when `size` is 0, which frees
 * nothing; that makes the empty state (NULL, 0) of a buffer safe to free.
 */
void el_free(el_allocator allocator, void *ptr, size_t size, size_t align);

#ifdef __cplusplus
}
#endif

#endif /* ELEMENTAL_ALLOC_H */
