/*
 * elemental/alloc_libc.h - an allocator over the C library's heap.
 *
 * STATUS: experimental. See docs/process/api-stability.md, and PORT.md next
 * to this backend for what it does and does not support.
 *
 * Requests with an alignment up to _Alignof(max_align_t) go to malloc().
 * Larger alignments go to aligned_alloc(), with the size rounded up to a
 * multiple of the alignment, as C11 requires. Freeing calls free(); the
 * size and alignment el_free() passes are not used.
 *
 * Exhaustion: whatever the C library reports. A request it refuses returns
 * NULL, as does a size that overflows when rounded up for a large
 * alignment. The heap is not bounded by this backend.
 *
 * Ownership: the heap is process-wide and has no object here, so there is
 * nothing to initialize, reset, or end. Memory that is not freed stays
 * allocated until the process exits.
 *
 * Concurrency: the handle carries no state, so it may be used from several
 * threads at once, to the extent the C library's malloc() and free() may
 * (glibc's may). Not for interrupt handlers. May block inside the C
 * library, for a lock or for memory from the operating system.
 */
#ifndef ELEMENTAL_ALLOC_LIBC_H
#define ELEMENTAL_ALLOC_LIBC_H

#include <elemental/alloc.h>

#ifdef __cplusplus
extern "C" {
#endif

/* An allocator handle for the C library's heap. Every call returns the same handle. */
el_allocator el_libc_allocator(void);

#ifdef __cplusplus
}
#endif

#endif /* ELEMENTAL_ALLOC_LIBC_H */
