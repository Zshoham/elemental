/*
 * elemental/io_alloc.h - buffer storage that grows, over an allocator.
 *
 * el_grow_buf holds an el_buf (elemental/io_buf.h) whose storage comes from
 * an allocator and grows as needed. Everything that takes an el_buf * takes
 * it: el_buf_append, the memory endpoint, a buffered writer, printf. It is
 * part of elemental::io only when the Kconfig symbol IO_ALLOC is on, and
 * then io links elemental::alloc. With it off, io needs no allocator.
 *
 * STATUS: experimental. See docs/process/api-stability.md.
 *
 * Growth: when the buffer needs room it does not have, it grows to twice
 * its capacity, or to what is needed if that is more, and to at least 16
 * bytes. If the allocator refuses that, it asks once more for exactly what
 * is needed. If that is refused too, the request fails with EL_ERR_NO_SPACE
 * and the buffer is unchanged. The allocator decides how far the buffer
 * grows; the buffer has no limit of its own.
 *
 * Storage is allocated and freed with el_layout_make(cap, 1) (ADR 0013):
 * freed with the capacity it was allocated with, never the length. Growing
 * allocates the new storage before freeing the old, so over an arena whose
 * free gives nothing back, a buffer that has grown to n bytes has used up
 * to about 2n of the arena.
 *
 * Ownership and lifetime:
 *
 *   - The caller provides the el_grow_buf object. It owns its storage until
 *     el_grow_buf_deinit(), which the caller must call. The allocator must
 *     outlive the buffer.
 *   - The el_buf * that el_grow_buf_init() returns points into the object,
 *     so the object must not move while that pointer is in use.
 *
 * The struct layout is public so that callers can allocate it. The fields
 * are not part of the API: use the functions.
 *
 * Concurrency: no internal synchronization. One buffer, one context at a
 * time, and only where its allocator may be used.
 *
 * Contract violations, checked by assertions: a null object; an allocator
 * handle that was never made; a second el_grow_buf_deinit() without an
 * el_grow_buf_init() between them.
 */
#ifndef ELEMENTAL_IO_ALLOC_H
#define ELEMENTAL_IO_ALLOC_H

#include <elemental/alloc.h>
#include <elemental/io_buf.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct el_grow_buf {
    el_buf buf; /* first: the growth hook recovers the allocator from it */
    el_allocator allocator;
} el_grow_buf;

/*
 * Start an empty buffer that grows from `allocator`, and return it: no
 * storage yet (el_buf_data() is null, el_buf_len() and el_buf_room() are 0),
 * and nothing allocated until something is appended or reserved.
 */
el_buf *el_grow_buf_init(el_grow_buf *g, el_allocator allocator);

/*
 * Free the storage. The el_buf is then a zero-capacity fixed buffer, always
 * full (appends report EL_ERR_NO_SPACE), until el_grow_buf_init() again.
 */
void el_grow_buf_deinit(el_grow_buf *g);

#ifdef __cplusplus
}
#endif

#endif /* ELEMENTAL_IO_ALLOC_H */
