/*
 * elemental/alloc.h - the allocator interface.
 *
 * An allocator hands out memory and takes it back. Code that needs dynamic
 * storage takes an el_allocator as a parameter, so its caller decides where
 * the memory comes from: an arena over a static array, the C library's heap,
 * or anything else that implements the two operations below, and optionally
 * a third that changes an allocation's size.
 *
 * STATUS: experimental. See docs/process/api-stability.md.
 *
 * Layout: every request is described by an el_layout, a size and an
 * alignment (docs/adr/0013-allocation-layout.md). Build one with
 * el_layout_make(), EL_LAYOUT_OF(type), or, for an array, el_layout_array().
 *
 * Exhaustion: el_alloc() returns NULL when the request cannot be satisfied:
 * the allocator has run out, the request is larger than it can ever hold, or
 * it cannot provide that alignment. NULL is not permanent; the same request
 * can succeed after memory is freed. A failed request changes nothing. A
 * function that allocates as part of other work reports this as
 * EL_ERR_NO_SPACE. The size of an array that overflows is caught earlier,
 * by el_layout_array(), which reports EL_ERR_NO_SPACE.
 *
 * Sized free: el_free() takes back the allocation's current layout
 * (docs/adr/0012-sized-free.md): the one it was allocated with, or, after a
 * successful el_realloc(), the alignment with the size it was changed to.
 * An allocator may rely on it, so it must be exact: keep that layout beside
 * the pointer and free with that same value. For a growable buffer it is
 * the layout of its capacity, not its length.
 *
 * Alignment: every layout names its alignment, a power of two (1, 2, 4,
 * ...). The returned pointer is a multiple of it. There is no default.
 *
 * Zero-size requests succeed without consulting the allocator. They return
 * a non-null pointer, a multiple of the alignment, that must not be
 * dereferenced and need not be unique. Freeing it, with a layout of size 0,
 * does nothing. So an operation is never called with a size of 0.
 *
 * Changing size: el_realloc() changes an allocation's size, in place if the
 * allocator can and by allocating, copying and freeing if not. The
 * allocator's third operation, `realloc`, is optional.
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
 * them: a handle that was never made; a layout whose alignment is 0 or not
 * a power of two (el_layout_make() checks it where the layout is built,
 * el_alloc() and el_free() where it is used, so a layout written field by
 * field is caught too); a null output parameter; el_free() with a null
 * pointer and a layout of nonzero size, or with a layout of size 0 and a
 * pointer other than null or the zero-size pointer; freeing memory the
 * allocator did not provide, twice, or with a different layout. The last
 * group is checked only where an implementation can do so cheaply, and is
 * otherwise undefined behavior, as it is for free(). For el_realloc(): the
 * same for `ptr` and `layout`, and a realloc operation that returns a
 * pointer not aligned to the layout. A call to the size the layout already
 * has returns `ptr` before any of the allocator's own checks, so there a
 * wrong pointer or layout, the zero-size pointer included, goes unchecked
 * (still a violation).
 */
#ifndef ELEMENTAL_ALLOC_H
#define ELEMENTAL_ALLOC_H

#include <stddef.h>

#include <elemental/status.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Layout -------------------------------------------------------------
 *
 * What an allocation looks like: how many bytes, and what they are aligned
 * to. It is a value: build it, copy it, keep it beside the pointer, and give
 * it back to el_free(). The fields may be read; build one with the functions
 * below, which check what they are given.
 *
 * A layout is two size_t, so it is passed and returned by value like a
 * handle (docs/conventions.md, ABI rule 8). On the AAPCS (Arm) and the
 * System V x86-64 ABI a two-word struct passed by value goes in registers,
 * so el_alloc(allocator, layout) costs no more than passing the four words
 * separately. (A result of two words comes back in registers on x86-64 and
 * through a caller-provided buffer on 32-bit Arm; either way the compiler
 * does it.) It holds no bool, enum, or bit-field.
 */
typedef struct el_layout {
    size_t size;
    size_t align; /* a power of two */
} el_layout;

/*
 * The layout of `size` bytes aligned to `align`. A `size` of 0 is valid.
 * Contract violation: `align` is 0 or not a power of two.
 */
el_layout el_layout_make(size_t size, size_t align);

/*
 * The layout of the type `T`: sizeof(T), _Alignof(T). A macro because only
 * a macro can take a type; it does nothing el_layout_make() does not
 * (docs/conventions.md, ABI rule 6). In C++, use el_layout_make(sizeof(T),
 * alignof(T)) for the same value.
 */
#define EL_LAYOUT_OF(T) el_layout_make(sizeof(T), _Alignof(T))

/*
 * The layout of an array of `count` elements, each with the layout `elem`:
 * size count * elem.size, alignment elem.align. Use it whenever `count`
 * comes from input.
 *
 *   EL_OK           *out holds the layout. A count of 0, or an element of
 *                   size 0, gives a layout of size 0.
 *   EL_ERR_NO_SPACE count * elem.size does not fit in a size_t; no
 *                   allocation could hold it. *out is set to size
 *                   SIZE_MAX, which no allocator can satisfy, so a caller
 *                   that ignores the status gets NULL from el_alloc().
 *
 * No division and no wider arithmetic, so it needs no compiler runtime
 * helper on a CPU without a divide or a 64-bit multiply.
 * Contract violation: `out` is null; `elem.align` is not a power of two.
 */
EL_NODISCARD el_status el_layout_array(el_layout elem, size_t count, el_layout *out);

/* ---- Interface ----------------------------------------------------------
 *
 * To implement an allocator, define the two operations over your own state
 * (and, if you can do better than allocate, copy and free, the third) and
 * hand out a handle that pairs them with it. The functions below check the
 * caller's arguments before an operation runs and check its result after,
 * so an operation may rely on the "called with" guarantees.
 *
 * Operations are called through C. They must not unwind: no C++ exception,
 * Rust panic, or longjmp may leave them.
 */

typedef struct el_allocator_ops {
    /*
     * Allocate `layout.size` bytes aligned to `layout.align`.
     *
     * Called with: layout.size >= 1; layout.align a power of two.
     * Must: return a pointer to `layout.size` writable bytes that is a
     *       multiple of `layout.align` and overlaps no live allocation; or
     *       return NULL and change nothing.
     */
    void *(*alloc)(void *ctx, el_layout layout);

    /*
     * Take back an allocation.
     *
     * Called with: ptr not null; layout.size >= 1. ptr and layout are, if
     *              the caller kept the contract, exactly the current ones
     *              of a live allocation on this ctx: from the most recent
     *              successful alloc or realloc that produced it, at the
     *              address that returned and with its size and alignment.
     * Must: not fail. It may make the memory available again, or not.
     */
    void (*free)(void *ctx, void *ptr, el_layout layout);

    /*
     * Optional: null if the allocator can do no better than allocate, copy
     * and free, which el_realloc() then does.
     *
     * Change an allocation's size, moving it if the allocator chooses.
     *
     * Called with: ptr and layout exactly those of a live allocation from
     *              this ctx; new_size >= 1 and different from layout.size.
     * Must: return the allocation's address, `ptr` or another multiple of
     *       layout.align, with new_size bytes there whose first
     *       min(layout.size, new_size) are the allocation's; the caller
     *       then uses only the returned pointer, even if it equals `ptr`,
     *       as after C's realloc(). Or
     *       return NULL and change nothing: el_realloc() then allocates,
     *       copies and frees, so NULL means "no better than that", not
     *       only "no memory".
     */
    void *(*realloc)(void *ctx, void *ptr, el_layout layout, size_t new_size);
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
 * Allocate `layout.size` bytes aligned to `layout.align`. Returns NULL if
 * the request cannot be satisfied; see "Exhaustion" above. The contents are
 * unspecified. A layout of size 0 returns the zero-size pointer.
 */
EL_NODISCARD void *el_alloc(el_allocator allocator, el_layout layout);

/*
 * Give back `ptr`, which was allocated from `allocator` and has exactly
 * this `layout` now: the one it was allocated with, or the one its last
 * el_realloc() gave it. `ptr` may be null only when `layout.size` is 0,
 * which frees nothing; that makes the empty state (NULL, size 0) of a
 * buffer safe to free.
 */
void el_free(el_allocator allocator, void *ptr, el_layout layout);

/*
 * Change the allocation at `ptr`, made from `allocator` with `layout`, to
 * `new_size` bytes, moving it if need be. Returns its address, `ptr` or
 * another; its first min(layout.size, new_size) bytes are what they were,
 * and the rest are unspecified. From then on it is freed with
 * el_layout_make(new_size, layout.align), at the returned address. NULL if
 * it cannot be done: nothing changed, and the allocation is still at `ptr`
 * with `layout`.
 *
 * The allocator's realloc operation is asked first. Without one, or if it
 * returns NULL, a new allocation is made, the old one's bytes copied into
 * it, and the old one freed. So an allocator that is out of memory may be
 * asked twice.
 *
 * A new size equal to layout.size returns `ptr` without consulting the
 * allocator. A layout of size 0 (`ptr` null or the zero-size pointer) is
 * el_alloc() of the new size, and so is not a failure when the new size is
 * 0 too: it returns the zero-size pointer. A new size of 0 frees the
 * allocation and returns the zero-size pointer. No operation is called with
 * a size of 0.
 *
 * After a success only the returned pointer is used: the old one, and every
 * pointer derived from it, are not used again, even if the address is the
 * same. A pointer into the allocation is derived again from the returned
 * one. (A backend such as el_libc_allocator() hands the call to C's
 * realloc(), which ends the old object even when it returns the same
 * address.)
 */
EL_NODISCARD void *el_realloc(el_allocator allocator, void *ptr, el_layout layout, size_t new_size);

#ifdef __cplusplus
}
#endif

#endif /* ELEMENTAL_ALLOC_H */
