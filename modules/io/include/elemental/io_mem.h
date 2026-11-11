/*
 * elemental/io_mem.h - readers and writers over caller-provided memory.
 *
 * The portable implementation of the io interfaces: no allocation, no
 * operating system, no blocking. Useful on its own for building and parsing
 * messages in fixed buffers, and as the stand-in for a device in tests.
 *
 * STATUS: experimental. See docs/process/api-stability.md.
 *
 * Storage and ownership:
 *
 *   - The caller provides the object (el_mem_reader, el_mem_writer) and
 *     the bytes it works over, and keeps both alive while the object or any
 *     handle made from it is in use. Static, stack, or heap storage all work.
 *   - The object borrows the bytes; it never frees them.
 *   - These objects hold no resources, so there is nothing to destroy.
 *   - An object may be copied or moved when no handle made from it is in
 *     use; handles made earlier refer to the old address.
 *
 * The struct layouts are public so that callers can allocate them. The
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

typedef struct el_mem_writer {
    uint8_t *buf;
    size_t cap;
    size_t len;
} el_mem_writer;

/*
 * Start writing into the `cap` bytes at `buf`. `buf` may be null only if
 * `cap` is 0, which makes a writer that is already full.
 */
void el_mem_writer_init(el_mem_writer *mw, void *buf, size_t cap);

/*
 * A writer handle for `mw`. Never returns EL_ERR_WOULD_BLOCK or
 * EL_ERR_IO: each write accepts min(len, free space) bytes, then
 * EL_ERR_END_OF_STREAM once the buffer is full. A write larger than the
 * free space is therefore truncated, and el_write_all() reports that as
 * EL_ERR_END_OF_STREAM with the count that fit.
 */
el_writer el_mem_writer_as_writer(el_mem_writer *mw);

/* Bytes written so far: buf[0..len) is valid. */
size_t el_mem_writer_len(const el_mem_writer *mw);

#ifdef __cplusplus
}
#endif

#endif /* ELEMENTAL_IO_MEM_H */
