/*
 * elemental/log_deferred.h - a sink that hands records to another context.
 *
 * For code that must not wait for where its records are written: an
 * interrupt handler, or a thread that must not block on a console. The
 * deferred sink copies each record into a queue it keeps in storage the
 * caller provides, and returns. Another context drains the queue into the
 * sink that writes.
 *
 *   logging side    the handle from el_log_deferred_sink_as_sink(): the
 *                   queue's producer
 *   writing side    el_log_deferred_sink_drain(): the queue's consumer
 *
 * One context at a time may use each side, and the two sides may run at the
 * same moment. A thread and an interrupt handler that both log through one
 * deferred sink are two contexts on the logging side: the application
 * masks the interrupt around the thread's log calls, or gives each its own
 * sink and storage. (Every sink has that rule, elemental/log.h; it matters
 * more here because an interrupt cannot be asked to wait.)
 *
 * STATUS: experimental. See docs/process/api-stability.md.
 *
 * A record is built in the record area, then copied into the queue whole
 * or not at all. One that does not fit, in the record area or in the room
 * the queue has left, is lost and counted; the logging side never waits,
 * and a log call cannot fail. Records are kept as records, not as text: the
 * sink that drains them formats the line.
 *
 * Three sizes are the application's to choose: how many records may wait;
 * how long a message may be; and whatever the draining sink needs for a
 * line. The first two are one block of storage:
 * EL_LOG_DEFERRED_SINK_STORAGE_SIZE() sizes it for a number of records of
 * the longest message, and el_log_deferred_sink_record_size() sizes it for
 * records of different lengths. Its first bytes are the record area, one
 * record long; the rest is the queue. The queue is an el_ring
 * (elemental/ring.h) that is the sink's own: nothing else writes or reads
 * it, and the caller never sets one up.
 *
 * Storage and ownership: the caller provides the object and the storage,
 * of any alignment, and keeps both alive and in place while either side is
 * used. The sink borrows the storage, by pointer, from init until it is no
 * longer used, and must not move once a logger uses it: a logger holds the
 * sink's address. A record's tag travels as the pointer the logger had, and
 * must still be valid when the record is drained (elemental/log.h,
 * "Ownership and lifetime"). Nothing to destroy. The struct layout is
 * public so that callers can allocate it; use the functions.
 *
 * Execution context: nothing here blocks, allocates, or takes a lock. The
 * logging side is ISR-safe under the rule above; a logger that uses it
 * from an interrupt handler also needs a clock that may be read from one.
 * Draining calls the destination sink, so it may be made only where that
 * sink may be used: a thread, for a sink that writes to a stream.
 *
 * Contract violations, checked by assertions: a null sink; null storage
 * with a nonzero size; a storage size above 0 and below
 * el_log_deferred_sink_record_size(message_max), which is how swapped
 * `storage_cap` and `message_max` arguments are caught; a destination
 * handle that was never made; a record begun while another is open, which
 * is logging into the sink from inside a function given to
 * el_log_compose().
 */
#ifndef ELEMENTAL_LOG_DEFERRED_H
#define ELEMENTAL_LOG_DEFERRED_H

#include <stddef.h>
#include <stdint.h>

#include <elemental/io_buf.h>
#include <elemental/log.h>
#include <elemental/ring.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Private: the fields the deferred sink keeps before each record's
 * message. Public only so that EL_LOG_DEFERRED_SINK_STORAGE_SIZE is a
 * constant; not API.
 */
typedef struct el_log_deferred_sink_entry_ {
    el_ticks timestamp;
    const char *tag;
    size_t len;
    uint32_t ticks_per_second;
    el_log_level level;
} el_log_deferred_sink_entry_;

/*
 * The bytes the deferred sink needs for `waiting` records that may wait,
 * each with a message of `message_max` bytes: the area one record is built
 * in, then the queue, each record its fields and its message. A constant
 * expression when its arguments are, for a static array. It checks no
 * overflow, of any part: el_log_deferred_sink_record_size() asserts only
 * that one record's size, its fields plus `message_max`, is representable,
 * and knows nothing of `waiting`. The caller makes sure the whole
 * expression is: with `one` that record size, `waiting < SIZE_MAX / one`.
 * el_log_deferred_sink_init() then needs the queue's part, `waiting * one`,
 * to be at most SIZE_MAX / 2.
 */
#define EL_LOG_DEFERRED_SINK_STORAGE_SIZE(waiting, message_max) \
    (((size_t)(waiting) + 1u) * (sizeof(el_log_deferred_sink_entry_) + (size_t)(message_max)))

typedef struct el_log_deferred_sink {
    el_ring queue;   /* the sink's own: the logging side writes, the drain reads */
    uint8_t *record; /* the record begun is built here: its fields, then its message */
    size_t record_cap;
    el_buf message;    /* lent for the record begun: `record` after the fields */
    el_ring_word lost; /* the logging side's */
    uint8_t open;      /* 1 from a begin that lent the buffer until its end; the logging side's */
} el_log_deferred_sink;

/*
 * The bytes one record with a message of `message_len` bytes takes, in the
 * queue and in the record area: for sizing storage for records of
 * different lengths, and what el_log_deferred_sink_pending_bytes() counts
 * in. `message_len` must leave room for the record's own fields below
 * SIZE_MAX.
 */
size_t el_log_deferred_sink_record_size(size_t message_len);

/*
 * Hand records on through a queue kept in the `storage_cap` bytes at
 * `storage`, of any alignment. The first
 * el_log_deferred_sink_record_size(message_max) bytes are where each record
 * is built, so a message of more than `message_max` bytes is lost; the rest
 * is the queue, so storage of exactly one record has no queue and loses
 * every record. Size it with EL_LOG_DEFERRED_SINK_STORAGE_SIZE(). A queue of
 * records shorter than `message_max` needs fewer bytes than that macro says.
 * `storage` may be null only if `storage_cap` is 0, which makes a sink that
 * loses every record; `message_max` is then not used. A `storage_cap` above
 * 0 and below el_log_deferred_sink_record_size(message_max) is a contract
 * violation; with assertions off, a sink that loses every record. A queue,
 * `storage_cap` less that record size, of more than SIZE_MAX / 2 bytes is
 * a contract violation too: the ring the sink keeps it in counts to twice
 * its size (elemental/ring.h), however the storage was sized.
 *
 * Called before either side is used. The sink must not move afterwards.
 */
void el_log_deferred_sink_init(el_log_deferred_sink *sink, void *storage, size_t storage_cap,
                               size_t message_max);

/*
 * The bytes of records waiting now, at least: the logging side can only
 * make it larger. The writing side's, as el_ring_len() is the ring's
 * consumer's.
 */
size_t el_log_deferred_sink_pending_bytes(const el_log_deferred_sink *sink);

/*
 * The logging side: a sink handle for `sink`. Its begin lends the record
 * area after the record's own fields, and returns EL_ERR_NO_SPACE if not
 * even those fit there or in the queue's room. Its end returns:
 *
 *   EL_OK             The record is in the queue, whole.
 *   EL_ERR_NO_SPACE   Its message did not fit in the record area, or the
 *                     record did not fit in the queue's room. Nothing of it
 *                     is in the queue.
 *
 * Either EL_ERR_NO_SPACE counts the record as lost.
 */
el_log_sink el_log_deferred_sink_as_sink(el_log_deferred_sink *sink);

/*
 * The writing side. Take every record that was in the queue when the call
 * began, oldest first, and give each to `to`; return how many were taken.
 * Records logged while it runs wait for the next call, so the call is
 * bounded however fast they arrive.
 *
 * Each record's message is read from the queue straight into the buffer
 * `to` lends. One longer than that buffer takes is given cut, as a logger
 * would give it (elemental/log.h). A record `to` does not take is lost
 * there, and counted by `to` if it counts: it is still one of the records
 * returned, and is not in the deferred sink's own lost count.
 */
size_t el_log_deferred_sink_drain(el_log_deferred_sink *sink, el_log_sink to);

/*
 * Records the logging side lost, since the sink was set up. May be called
 * from any context.
 */
uint32_t el_log_deferred_sink_lost(const el_log_deferred_sink *sink);

#ifdef __cplusplus
}
#endif

#endif /* ELEMENTAL_LOG_DEFERRED_H */
