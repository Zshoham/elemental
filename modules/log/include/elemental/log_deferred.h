/*
 * elemental/log_deferred.h - a sink that hands records to another context.
 *
 * For code that must not wait for where its records are written: an
 * interrupt handler, or a thread that must not block on a console. The
 * deferred sink copies each record into an el_ring (elemental/ring.h) and
 * returns. Another context drains the ring into the sink that writes.
 *
 *   logging side    the handle from el_log_deferred_sink_as_sink(): the
 *                   ring's producer
 *   writing side    el_log_deferred_sink_drain(): the ring's consumer
 *
 * One context at a time may use each side, and the two sides may run at the
 * same moment. A thread and an interrupt handler that both log through one
 * deferred sink are two contexts on the logging side: the application
 * masks the interrupt around the thread's log calls, or gives each its own
 * sink and ring. (Every sink has that rule, elemental/log.h; it matters
 * more here because an interrupt cannot be asked to wait.)
 *
 * STATUS: experimental. See docs/process/api-stability.md.
 *
 * A record is built in the record storage, then copied into the ring whole
 * or not at all. One that does not fit, in the record storage or in the
 * room the ring has left, is lost and counted; the logging side never
 * waits, and a log call cannot fail. Records are kept as records, not as
 * text: the sink that drains them formats the line.
 *
 * Three sizes are the application's to choose: the ring, how many records
 * may wait; the record storage, how long a message may be; and whatever the
 * draining sink needs for a line. el_log_deferred_sink_record_size() sizes
 * the first two.
 *
 * Storage and ownership: the caller provides the object, the ring and its
 * storage, and the record storage, of any alignment, and keeps them alive
 * and in place while either side is used. The ring is the sink's: nothing
 * else writes or reads it. A record's tag travels as the pointer the logger
 * had, and must still be valid when the record is drained (elemental/log.h,
 * "Ownership and lifetime"). Nothing to destroy. The struct layout is
 * public so that callers can allocate it; use the functions.
 *
 * Execution context: nothing here blocks, allocates, or takes a lock. The
 * logging side is ISR-safe under the rule above; a logger that uses it
 * from an interrupt handler also needs a clock that may be read from one.
 * Draining calls the destination sink, so it may be made only where that
 * sink may be used: a thread, for a sink that writes to a stream.
 *
 * Contract violations, checked by assertions: a null sink or ring; null
 * record storage with a nonzero size; a destination handle that was never
 * made.
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

typedef struct el_log_deferred_sink {
    el_ring *ring;
    uint8_t *record; /* the record begun is built here: its fields, then its message */
    size_t record_cap;
    el_buf message;    /* lent for the record begun: `record` after the fields */
    el_ring_word lost; /* the logging side's */
} el_log_deferred_sink;

/*
 * The bytes one record with a message of `message_len` bytes takes, in the
 * ring and in the record storage. The record storage is sized for the
 * longest message; n records of that size wait in a ring of n times it.
 * `message_len` must leave room for the record's own fields below SIZE_MAX.
 */
size_t el_log_deferred_sink_record_size(size_t message_len);

/*
 * Hand records on through `ring`, building each in the `record_cap` bytes
 * at `record`. `ring` is set up by the caller, before or after, and is
 * empty when the first record is logged. `record` may be null only if
 * `record_cap` is 0, which makes a sink that loses every record.
 *
 * Called before either side is used.
 */
void el_log_deferred_sink_init(el_log_deferred_sink *sink, el_ring *ring, void *record,
                               size_t record_cap);

/*
 * The logging side: a sink handle for `sink`. Its begin lends the record
 * storage after the record's own fields, and returns EL_ERR_NO_SPACE if not
 * even those fit there or in the ring's room. Its end returns:
 *
 *   EL_OK             The record is in the ring, whole.
 *   EL_ERR_NO_SPACE   Its message did not fit in the record storage, or
 *                     the record did not fit in the ring's room. Nothing
 *                     of it is in the ring.
 *
 * Either EL_ERR_NO_SPACE counts the record as lost.
 */
el_log_sink el_log_deferred_sink_as_sink(el_log_deferred_sink *sink);

/*
 * The writing side. Take every record that was in the ring when the call
 * began, oldest first, and give each to `to`; return how many were taken.
 * Records logged while it runs wait for the next call, so the call is
 * bounded however fast they arrive.
 *
 * Each record's message is read from the ring straight into the buffer
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
