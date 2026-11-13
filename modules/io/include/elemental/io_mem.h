/*
 * elemental/io_mem.h - a reader over caller-provided memory.
 *
 * The portable implementation of el_reader: no allocation, no operating
 * system, no blocking. Useful on its own for parsing messages in memory, and
 * as the stand-in for a device in tests. Its bytes are const, so it reads
 * flash tables and received frames without a cast. The writing side, a
 * stream that ends in memory, is el_buf_as_writer() (elemental/io_buf.h).
 *
 * STATUS: experimental. See docs/process/api-stability.md.
 *
 * Storage and ownership:
 *
 *   - The caller provides the object (el_mem_reader) and the bytes it
 *     reads, and keeps both alive while the object or any handle made
 *     from it is in use. Static, stack, or heap storage all work.
 *   - The object borrows the bytes; it never frees them.
 *   - The reader holds no resources, so there is nothing to destroy.
 *   - An object may be copied or moved when no handle made from it is in
 *     use; handles made earlier refer to the old address.
 *
 * The struct layout is public so that callers can allocate it. The
 * fields are not part of the API: use the functions.
 *
 * Concurrency: no internal synchronization. One object may be used from one
 * context at a time. Nothing here blocks, so use from an interrupt handler
 * is fine under that same rule.
 */
#ifndef ELEMENTAL_IO_MEM_H
#define ELEMENTAL_IO_MEM_H

#include <stddef.h>
#include <stdint.h>

#include <elemental/io.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct el_mem_reader {
    const uint8_t *data;
    size_t len;
    size_t pos;
} el_mem_reader;

/*
 * Start reading the `len` bytes at `data`. `data` may be null only if `len`
 * is 0, which makes a reader that is already at its end.
 */
void el_mem_reader_init(el_mem_reader *mr, const void *data, size_t len);

/*
 * A reader handle for `mr`. Never returns EL_ERR_WOULD_BLOCK or
 * EL_ERR_IO: each read yields min(cap, remaining) bytes, then
 * EL_ERR_END_OF_STREAM once nothing remains.
 */
el_reader el_mem_reader_as_reader(el_mem_reader *mr);

/* Bytes not yet read. */
size_t el_mem_reader_remaining(const el_mem_reader *mr);

#ifdef __cplusplus
}
#endif

#endif /* ELEMENTAL_IO_MEM_H */
