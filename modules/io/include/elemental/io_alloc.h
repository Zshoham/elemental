/*
 * elemental/io_alloc.h - a writer that grows, over an allocator.
 *
 * el_grow_writer collects whatever is written to it in memory it takes from
 * an allocator, growing as needed. It is an el_writer, so anything that
 * writes to a stream can write into memory without knowing the size first.
 * It is part of elemental::io only when the Kconfig symbol IO_ALLOC is on,
 * and then io links elemental::alloc. With it off, io needs no allocator.
 *
 * STATUS: experimental. See docs/process/api-stability.md.
 *
 * Writes (elemental/io.h for the rules every writer follows):
 *
 *   EL_OK               At least one byte taken. When the buffer is full,
 *                       a write grows it first: to twice its capacity, or
 *                       to what the write needs if that is more, and at
 *                       least 16 bytes. If the allocator refuses that, the
 *                       writer asks once more for exactly what the write
 *                       needs. If that is refused too but some room is left,
 *                       the write takes what fits.
 *   EL_ERR_NO_SPACE     Nothing taken: the buffer is full and the allocator
 *                       refused to grow it. Not permanent: the same write
 *                       can succeed once the allocator has room again.
 *
 * It never returns EL_ERR_WOULD_BLOCK, EL_ERR_END_OF_STREAM, or EL_ERR_IO.
 *
 * Growing allocates the new buffer before freeing the old one. Over an
 * arena whose free gives nothing back, a writer that has grown to n bytes
 * has therefore used up to about 2n of the arena.
 *
 * Ownership and lifetime:
 *
 *   - The caller provides the el_grow_writer object. The writer owns its
 *     buffer and frees it in el_grow_writer_deinit(), which the caller must
 *     call. The allocator must outlive the writer.
 *   - el_grow_writer_data() points into the buffer, which moves when it
 *     grows. The pointer is valid until the next write or deinit.
 *   - The object may be moved while no handle made from it is in use.
 *
 * The struct layout is public so that callers can allocate it. The fields
 * are not part of the API: use the functions.
 *
 * Concurrency: no internal synchronization. One writer may be used from one
 * context at a time, and only where its allocator may be used.
 *
 * Contract violations, checked by assertions: a null writer; an allocator
 * handle that was never made; using the writer after deinit.
 */
#ifndef ELEMENTAL_IO_ALLOC_H
#define ELEMENTAL_IO_ALLOC_H

#include <stddef.h>
#include <stdint.h>

#include <elemental/alloc.h>
#include <elemental/io.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct el_grow_writer {
    el_allocator allocator;
    uint8_t *buf;
    size_t len;
    size_t cap;
} el_grow_writer;

/* Start an empty writer that takes its memory from `allocator`. Allocates nothing yet. */
void el_grow_writer_init(el_grow_writer *gw, el_allocator allocator);

/* A writer handle for `gw`. */
el_writer el_grow_writer_as_writer(el_grow_writer *gw);

/* The bytes written so far: el_grow_writer_len() of them. Null while nothing has been. */
const uint8_t *el_grow_writer_data(const el_grow_writer *gw);

/* How many bytes have been written. */
size_t el_grow_writer_len(const el_grow_writer *gw);

/* Free the buffer. The writer must not be used again until it is re-initialized. */
void el_grow_writer_deinit(el_grow_writer *gw);

#ifdef __cplusplus
}
#endif

#endif /* ELEMENTAL_IO_ALLOC_H */
