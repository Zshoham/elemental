/*
 * elemental/ring.h - a byte FIFO between two contexts.
 *
 * An el_ring is a first-in, first-out buffer of bytes over storage the
 * caller provides. One context fills it while another empties it, with no
 * lock: an interrupt handler and a thread, or two threads. It is what goes
 * between a device's receive interrupt and the code that wants the bytes.
 *
 * STATUS: experimental. See docs/process/api-stability.md.
 *
 * Two sides. Every function below belongs to one of them:
 *
 *   the producer's   el_ring_write, el_ring_room, el_ring_close, and the
 *                    writer from el_ring_as_writer
 *   the consumer's   el_ring_read, el_ring_len, and the reader from
 *                    el_ring_as_reader
 *
 * One context at a time may use each side, and the two sides may run at the
 * same moment. That is the whole concurrency contract. Two producers, or
 * two consumers, need the application's exclusion: a thread and an
 * interrupt handler that both write mask the interrupt around the thread's
 * writes. Two handlers that can preempt each other are two contexts.
 *
 * Bytes are copied in and out; no pointer into the storage is handed to
 * either side. The plain functions cannot fail: they return how many bytes
 * moved, and 0 means full, empty, or closed. The reader and the writer
 * follow elemental/io.h; a full or an empty ring is a stream that stalls,
 * EL_ERR_WOULD_BLOCK.
 *
 * The ring wakes nobody. What a consumer waits on when the ring is empty
 * is the platform's (a semaphore the producer gives, say), and the
 * application's to write.
 *
 * Storage and ownership: the caller provides the el_ring and its storage,
 * of any size and alignment, and keeps both alive while either side uses
 * the ring. Every byte of the storage holds data. The ring borrows the
 * storage; there is nothing to destroy. The struct layout is public so that
 * callers can allocate it. The fields are not part of the API: use the
 * functions.
 *
 * A ring is set up before the second context can see it, and must not be
 * moved or copied once either side uses it. (Elsewhere an object may be
 * moved while no handle refers to it. Here the other context always does.)
 *
 * Execution context: nothing here blocks, allocates, or takes a lock.
 * Every function is ISR-safe under the two-sides rule. The only atomic
 * operations are a load and a store of one word (docs/adr/0023), which the
 * compiler turns into instructions, not calls, on every target Elemental
 * is verified on.
 *
 * Contract violations, checked by assertions: a null ring; null storage
 * with a nonzero capacity; a capacity above SIZE_MAX / 2; a null buffer
 * with a nonzero size.
 */
#ifndef ELEMENTAL_RING_H
#define ELEMENTAL_RING_H

#include <stddef.h>
#include <stdint.h>

#include <elemental/io.h>

#ifdef __cplusplus
extern "C" {
/* C++ has no _Atomic. It sees a word of the same size and alignment, which
 * the library's source asserts, and never touches it. */
typedef size_t el_ring_word;
#else
/* A word one context writes and another reads. */
typedef _Atomic size_t el_ring_word;
#endif

typedef struct el_ring {
    uint8_t *storage;
    size_t cap;
    el_ring_word head;   /* the consumer's: where the oldest byte is */
    el_ring_word tail;   /* the producer's: where the next byte goes */
    el_ring_word closed; /* the producer's: 1 once it will write no more */
} el_ring;

/*
 * An empty, open ring over the `cap` bytes at `storage`. `storage` may be
 * null only if `cap` is 0, which makes a ring that is always full and
 * empty. `cap` is at most SIZE_MAX / 2, and need not be a power of two.
 *
 * Called before either side uses the ring. Setting a ring up again is the
 * only way to reopen it, and needs both sides stopped.
 */
void el_ring_init(el_ring *ring, void *storage, size_t cap);

/* ---- The producer's side ------------------------------------------------ */

/*
 * Copy in as many of the `len` bytes at `data` as there is room for, oldest
 * first, and return how many: 0 to `len`. 0 means the ring is full or
 * closed, or `len` is 0. `data` may be null only if `len` is 0.
 */
size_t el_ring_write(el_ring *ring, const void *data, size_t len);

/*
 * The bytes el_ring_write() would take now, at least: the consumer can only
 * make it larger. Room seen by the producer is still there for its next
 * write, so a producer that needs to write all of something or none of it
 * checks here first. 0 once the ring is closed.
 */
size_t el_ring_room(const el_ring *ring);

/*
 * The producer will write no more. Bytes already written are still read.
 * Afterwards el_ring_write() takes nothing, the writer reports
 * EL_ERR_END_OF_STREAM, and the reader reports it once the ring is empty.
 * Closing twice is closing once.
 */
void el_ring_close(el_ring *ring);

/*
 * The producer's side as a writer. One call copies in what fits:
 *
 *   EL_OK                  1 <= *n <= len bytes were taken.
 *   EL_ERR_WOULD_BLOCK     The ring is full. The same call can succeed once
 *                          the consumer has read.
 *   EL_ERR_END_OF_STREAM   The ring was closed. Permanent.
 *
 * It never waits and never reports EL_ERR_NO_SPACE: a writer cannot give a
 * ring more storage, and the room comes back when the other side reads.
 * The handle is valid while the ring is.
 */
el_writer el_ring_as_writer(el_ring *ring);

/* ---- The consumer's side ------------------------------------------------ */

/*
 * Copy out as many bytes as the ring holds, oldest first, up to `cap`, and
 * return how many: 0 to `cap`. 0 means the ring is empty, or `cap` is 0.
 * `buf` may be null only if `cap` is 0.
 */
size_t el_ring_read(el_ring *ring, void *buf, size_t cap);

/*
 * The bytes el_ring_read() would give now, at least: the producer can only
 * make it larger. Bytes seen by the consumer are still there for its next
 * read.
 */
size_t el_ring_len(const el_ring *ring);

/*
 * The consumer's side as a reader. One call copies out what is held:
 *
 *   EL_OK                  1 <= *n <= cap bytes were given.
 *   EL_ERR_WOULD_BLOCK     The ring is empty and open. The same call can
 *                          succeed once the producer has written.
 *   EL_ERR_END_OF_STREAM   The ring is closed and every byte written
 *                          before the close has been read. Permanent.
 *
 * It never waits. The handle is valid while the ring is.
 */
el_reader el_ring_as_reader(el_ring *ring);

#ifdef __cplusplus
}
#endif

#endif /* ELEMENTAL_RING_H */
