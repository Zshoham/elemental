/*
 * elemental/io.h - byte stream reader and writer interfaces.
 *
 * A reader is a source of bytes and a writer is a sink. Code written against
 * these two interfaces works with memory, a file descriptor, a UART, or a
 * test double, and several different ones can be in use at once.
 *
 * STATUS: experimental. See docs/process/api-stability.md.
 *
 * The one rule behind every operation here: PROGRESS OR STATUS, NEVER BOTH.
 *
 *   el_read() and el_write() make one attempt. Either they move at least
 *   one byte and return EL_OK, or they move nothing and the status says
 *   why. A failure discovered after some bytes moved is reported by the next
 *   call. Moving fewer bytes than requested is normal and is not an error.
 *
 *   EL_OK                 1 <= *n <= requested. (Requesting 0 bytes is a
 *                           no-op that returns EL_OK with *n == 0.)
 *   EL_ERR_WOULD_BLOCK    *n == 0. Nothing can move right now; the same
 *                           call can succeed later. Only from streams that
 *                           were set up as non-blocking.
 *   EL_ERR_END_OF_STREAM  *n == 0. Permanent: a reader is exhausted, a
 *                           writer can accept nothing more (its buffer is
 *                           full, its peer is gone). Every later call
 *                           returns the same.
 *   EL_ERR_IO             *n == 0. The environment failed. The object
 *                           behind the stream holds the detail.
 *
 * Blocking: the interface does not say whether a call may wait. That is a
 * property of the stream, fixed by whoever created it, and documented by
 * each implementation. Code that must not block is handed streams that do
 * not block.
 *
 * Ownership and lifetime:
 *
 *   - el_reader and el_writer are handles: two pointers, passed by
 *     value, owning nothing. A handle is valid while the object it was made
 *     from is alive and has not been moved.
 *   - There is no close operation. Whoever created a stream object ends it;
 *     code that is merely handed a handle cannot.
 *   - Buffers passed to read and write are borrowed for the duration of the
 *     call. An implementation must not keep the pointer after returning.
 *
 * Concurrency: handles carry no synchronization. One stream object may be
 * used from one context at a time unless its implementation documents more.
 * Whether a stream may be used from an interrupt handler is likewise a
 * property of the implementation.
 *
 * Contract violations (a null handle, a null buffer with a nonzero size, a
 * null output parameter where one is required) are programmer errors, checked by assertions when
 * the library is built with them. They are never reported as a status.
 */
#ifndef ELEMENTAL_IO_H
#define ELEMENTAL_IO_H

#include <stddef.h>

#include <elemental/status.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Interfaces ---------------------------------------------------------
 *
 * To implement a reader or writer, define the operation below over your own
 * state and hand out a handle that pairs the two. The functions further down
 * validate arguments before your operation runs and check its result after,
 * so an implementation may rely on the "called with" guarantees and is
 * caught at the boundary if it breaks the "must" rules.
 *
 * Operations are called through C. They must not unwind: no C++ exception,
 * Rust panic, or longjmp may leave them.
 */

typedef struct el_reader_ops {
    /*
     * Make one attempt to read up to `cap` bytes into `buf`.
     *
     * Called with: cap >= 1, buf and n not null, *n == 0.
     * Must: return EL_OK and set 1 <= *n <= cap, having filled
     *       buf[0..*n); or leave *n == 0 and return another status.
     *       Once EL_ERR_END_OF_STREAM is returned, return it forever.
     */
    el_status (*read)(void *ctx, void *buf, size_t cap, size_t *n);
} el_reader_ops;

typedef struct el_writer_ops {
    /*
     * Make one attempt to write up to `len` bytes from `data`.
     *
     * Called with: len >= 1, data and n not null, *n == 0.
     * Must: return EL_OK and set 1 <= *n <= len, having taken
     *       data[0..*n); or leave *n == 0 and return another status.
     *       Copy what it takes: `data` is not valid after the call.
     *       Once EL_ERR_END_OF_STREAM is returned, return it forever.
     */
    el_status (*write)(void *ctx, const void *data, size_t len, size_t *n);
} el_writer_ops;

/*
 * `ops` must outlive every handle that uses it; make it static const.
 * `ctx` is passed back to the operation unchanged and may be null if the
 * implementation has no state.
 */
typedef struct el_reader {
    void *ctx;
    const el_reader_ops *ops;
} el_reader;

typedef struct el_writer {
    void *ctx;
    const el_writer_ops *ops;
} el_writer;

/* ---- Single-attempt operations ------------------------------------------ */

/*
 * Read up to `cap` bytes into `buf`. One attempt; see the table above.
 *
 * `n` is required. `buf` may be null only if `cap` is 0. On EL_OK,
 * buf[0..*n) holds the bytes read; the rest of `buf` is unspecified after
 * any call. A `cap` of 0 returns EL_OK with *n == 0 without consulting the
 * stream, so it says nothing about whether the stream has ended.
 */
el_status el_read(el_reader reader, void *buf, size_t cap, size_t *n);

/*
 * Write up to `len` bytes from `data`. One attempt; see the table above.
 *
 * `n` is required. `data` may be null only if `len` is 0. On EL_OK,
 * data[0..*n) was accepted and the remainder was not.
 */
el_status el_write(el_writer writer, const void *data, size_t len, size_t *n);

/* ---- Until-done helpers --------------------------------------------------
 *
 * These repeat the single-attempt operation, so they report a status AND the
 * progress made before it. That makes them resumable: after
 * EL_ERR_WOULD_BLOCK, wait for the stream and call again with the
 * remainder (buf + *n, len - *n).
 *
 * `n` is optional here, unlike everywhere else: on EL_OK it can only be
 * `len`. Pass null when any other status ends your use of the stream. Pass
 * it when you will resume, or need to know how much of a short stream or a
 * truncated write went through.
 *
 * They run at most `len` attempts and never wait on their own.
 */

/*
 * Read exactly `len` bytes. Returns EL_OK with *n == len, or the first
 * other status with *n < len bytes read before it. `n` may be null.
 */
el_status el_read_exact(el_reader reader, void *buf, size_t len, size_t *n);

/*
 * Write all `len` bytes. Returns EL_OK with *n == len, or the first other
 * status with *n < len bytes written before it. `n` may be null.
 */
el_status el_write_all(el_writer writer, const void *data, size_t len, size_t *n);

#ifdef __cplusplus
}
#endif

#endif /* ELEMENTAL_IO_H */
