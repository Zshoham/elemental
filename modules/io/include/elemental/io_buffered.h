/*
 * elemental/io_buffered.h - a buffered writer: an el_buf in front of
 * another writer.
 *
 * el_buffered_writer accepts bytes into a buffer it borrows and passes them
 * on to an inner writer, so that a caller can hand over output faster or in
 * smaller pieces than the inner stream takes it, without waiting. A UART
 * writer that takes what its FIFO has room for and would-blocks when it is
 * full is the typical inner writer. The buffer is an el_buf
 * (elemental/io_buf.h) with fixed or grown storage; an adapter works with
 * either.
 *
 * STATUS: experimental. See docs/process/api-stability.md.
 *
 * Flush is a function of this type, not an operation of el_writer: whoever
 * created the buffered writer flushes it (docs/adr/0014-flush-on-the-concrete-type.md,
 * provisional until the log writer sink exists).
 *
 * Writes through el_buffered_writer_as_writer(), of `len` bytes:
 *
 *   1. It fits in the free room at the tail: it is buffered, EL_OK, and the
 *      inner writer is not called.
 *   2. It does not fit and bytes are pending: ONE attempt to pass pending
 *      bytes on. A status other than EL_ERR_WOULD_BLOCK from it (IO,
 *      END_OF_STREAM, NO_SPACE) is returned with *n == 0 and nothing
 *      taken. Otherwise go to 4.
 *   3. It does not fit, nothing is pending, and len >= the capacity: the
 *      write goes straight to the inner writer, one attempt. Its result is
 *      returned as it is, except EL_ERR_WOULD_BLOCK, which goes on to 4. A
 *      grown buffer that has not allocated (capacity 0) skips this step and
 *      grows instead; a FIXED buffer of capacity 0 is therefore a
 *      pass-through writer.
 *   4. Make room: compact the bytes already passed on out of the front; if
 *      it still does not fit and the storage can grow, ask for room for all
 *      of it; buffer what fits (EL_OK). If nothing fits, return
 *      EL_ERR_WOULD_BLOCK with *n == 0: the stream refused, and room
 *      returns as it drains. If nothing fits and the stream was never asked
 *      (a grown buffer with nothing pending whose allocator refused), return
 *      EL_ERR_NO_SPACE, which el_writer allows for a writer that could not
 *      get storage.
 *
 * EL_ERR_END_OF_STREAM is permanent: once the inner writer has reported it,
 * every later write, flush and reserve returns it without asking again,
 * including a write that would fit. EL_ERR_IO, EL_ERR_WOULD_BLOCK and
 * EL_ERR_NO_SPACE are not remembered; the writer recovers from them as its
 * inner writer does.
 *
 * Grown storage grows only where fixed storage would have to wait: in a
 * write, after a pass-on that would-blocked or left too little room; in
 * el_buffered_writer_reserve(), when `min` exceeds what draining can free.
 * Flush never grows. Growth stops where the allocator refuses: the
 * allocator is the bound.
 *
 * Bytes move inside the buffer only when a write or reserve needs tail room
 * held by bytes already passed on, and only after passing on what the inner
 * writer takes. A flush that passes on part of the pending bytes moves none.
 *
 * Ownership and lifetime:
 *
 *   - The caller provides the object. The writer borrows its el_buf and its
 *     inner handle until it is no longer used. It owns nothing, so there is
 *     no deinit; the buffer's owner ends the buffer (el_grow_buf_deinit) once
 *     the writer is no longer used.
 *   - While the writer borrows the buffer, its owner may only read it
 *     (el_buf_data, el_buf_len, el_buf_room) and call
 *     el_buf_truncate(buf, mark) back to a `mark` whose bytes have not been
 *     passed on: mark >= el_buf_len(buf) - el_buffered_writer_pending(w).
 *     That is the rollback of a record that was started and not finished.
 *     Anything else (appending, committing, truncating past what was passed
 *     on, re-initializing the buffer, el_grow_buf_deinit) is a contract
 *     violation. The writer checks the cheap part, that the buffer still
 *     holds what was passed on, at its next call.
 *   - A mark taken before a write or printf call into the writer is still
 *     valid after it only if what the call wrote fit the room reserved
 *     before it (el_buffered_writer_reserve): then nothing was passed on or
 *     compacted. A call that needed more may compact the buffer, which moves
 *     the bytes behind the mark; a record written that way is left as it
 *     streamed, and a stall can leave it half-written.
 *   - The object may be moved while no handle made from it is in use.
 *
 * The struct layout is public so that callers can allocate it. The fields
 * are not part of the API: use the functions.
 *
 * Concurrency: no internal synchronization. The writer and the buffer it
 * borrows are one object: one context at a time. It never waits on its
 * own; whether a call can block is the inner writer's property, and with
 * grown storage, the allocator's.
 *
 * Contract violations, checked by assertions: a null object, buffer, or
 * inner handle; the owner using the borrowed buffer beyond the rule above,
 * where the writer can see it.
 */
#ifndef ELEMENTAL_IO_BUFFERED_H
#define ELEMENTAL_IO_BUFFERED_H

#include <stddef.h>

#include <elemental/io.h>
#include <elemental/io_buf.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct el_buffered_writer {
    el_buf *buf; /* borrowed: [pos, len) accepted and not yet passed on */
    size_t pos;  /* bytes at the front already passed on; SIZE_MAX once ended */
    el_writer inner;
} el_buffered_writer;

/*
 * Start a buffered writer in front of `inner`, over `buf`, fixed or grown.
 * Whatever `buf` already holds is pending: it is passed on first.
 */
void el_buffered_writer_init(el_buffered_writer *w, el_writer inner, el_buf *buf);

/* Bytes accepted and not yet passed on. 0 once the inner writer has ended:
 * nothing will be passed on any more. */
size_t el_buffered_writer_pending(const el_buffered_writer *w);

/*
 * Pass every pending byte on, as el_write_all() to the inner writer.
 *
 *   EL_OK      None pending.
 *   otherwise  The inner writer's status (EL_ERR_WOULD_BLOCK,
 *              EL_ERR_END_OF_STREAM, EL_ERR_IO, EL_ERR_NO_SPACE). Bytes
 *              passed on before it are no longer pending; call again to
 *              resume.
 *
 * Never grows, never moves bytes, never waits on its own.
 */
EL_NODISCARD el_status el_buffered_writer_flush(el_buffered_writer *w);

/*
 * Make at least `min` bytes of room at the tail of the buffer: pass bytes
 * on, one attempt at a time, only until draining would give the room; then
 * compact; then, for grown storage, grow.
 *
 *   EL_OK                 el_buf_room() >= min.
 *   EL_ERR_WOULD_BLOCK    The inner writer stalled before enough was passed
 *                           on, and the storage is fixed or its allocator
 *                           refused. Room returns as the stream drains.
 *   EL_ERR_NO_SPACE       `min` exceeds what the buffer can hold even
 *                           drained: fixed storage with min > its capacity
 *                           (no byte is passed on), or grown storage whose
 *                           allocator refused.
 *   other                 The inner writer's EL_ERR_END_OF_STREAM, EL_ERR_IO
 *                           or EL_ERR_NO_SPACE, as it returned them.
 *
 * Afterwards a record of at most `min` bytes can be appended without any of
 * it reaching the inner writer before it is complete (the reserve-first
 * pattern).
 */
EL_NODISCARD el_status el_buffered_writer_reserve(el_buffered_writer *w, size_t min);

/* The buffered writer as an el_writer. See "Writes" above. */
el_writer el_buffered_writer_as_writer(el_buffered_writer *w);

#ifdef __cplusplus
}
#endif

#endif /* ELEMENTAL_IO_BUFFERED_H */
