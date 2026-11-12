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
 * Sized free: el_free() takes back the layout the allocation was made with
 * (docs/adr/0012-sized-free.md). An allocator may rely on it, so it must be
 * exact: keep the layout you allocated with and free with that same value.
 * For a growable buffer it is the layout of its capacity, not its length.
 *
 * Alignment: every layout names its alignment, a power of two (1, 2, 4,
 * ...). The returned pointer is a multiple of it. There is no default.
 *
 * Zero-size requests succeed without consulting the allocator. They return
 * a non-null pointer, a multiple of the alignment, that must not be
 * dereferenced and need not be unique. Freeing it, with a layout of size 0,
 * does nothing. So an operation is never called with a size of 0.
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
 * otherwise undefined behavior, as it is for free().
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
 *                   allocation could hold it. *out is left unchanged.
 *
 * No division and no wider arithmetic, so it needs no compiler runtime
 * helper on a CPU without a divide or a 64-bit multiply.
 * Contract violation: `out` is null; `elem.align` is not a power of two.
 */
el_status el_layout_array(el_layout elem, size_t count, el_layout *out);

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
     *              the caller kept the contract, exactly those of a
     *              successful alloc on this ctx that has not been freed.
     * Must: not fail. It may make the memory available again, or not.
     */
    void (*free)(void *ctx, void *ptr, el_layout layout);
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
void *el_alloc(el_allocator allocator, el_layout layout);

/*
 * Give back `ptr`, which was allocated from `allocator` with exactly this
 * `layout`. `ptr` may be null only when `layout.size` is 0, which frees
 * nothing; that makes the empty state (NULL, size 0) of a buffer safe to
 * free.
 */
void el_free(el_allocator allocator, void *ptr, el_layout layout);

#ifdef __cplusplus
}
#endif

#endif /* ELEMENTAL_ALLOC_H */
