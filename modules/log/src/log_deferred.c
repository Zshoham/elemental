#include "elemental_config.h"

#include <elemental/assert.h>
#include <elemental/log_deferred.h>

#include <stdatomic.h>
#include <string.h>

#include "log_private.h"

/*
 * What travels through the queue for one record: the entry in
 * log_deferred.h, followed by its message bytes. Entries are copied in and
 * out, so neither the record area nor the queue needs an alignment. The
 * frequency is in every entry: one sink may serve loggers with different
 * clocks.
 */
size_t el_log_deferred_sink_record_size(size_t message_len)
{
    EL_ASSERT(message_len <= SIZE_MAX - sizeof(el_log_deferred_sink_entry_));
    return sizeof(el_log_deferred_sink_entry_) + message_len;
}

/* The count has one writer, the logging side: a load and a store, not an
 * atomic increment, which a Cortex-M0 does not have (docs/adr/0023). */
static el_status lose(el_log_deferred_sink *sink)
{
    const size_t lost = atomic_load_explicit(&sink->lost, memory_order_relaxed);
    atomic_store_explicit(&sink->lost, lost + 1, memory_order_release);
    return EL_ERR_NO_SPACE;
}

static el_status deferred_begin(void *ctx, const el_log_record *record, el_buf **message)
{
    (void)record;
    el_log_deferred_sink *const sink = ctx;
    /* A record already open here is being built in the record area this
     * would set up again. */
    EL_ASSERT(sink->open == 0);
    /* Nothing is formatted for a record that cannot be kept. */
    if (sink->record_cap < sizeof(el_log_deferred_sink_entry_) ||
        el_ring_room(&sink->queue) < sizeof(el_log_deferred_sink_entry_)) {
        return lose(sink);
    }
    el_buf_init(&sink->message, sink->record + sizeof(el_log_deferred_sink_entry_),
                sink->record_cap - sizeof(el_log_deferred_sink_entry_));
    sink->open = 1;
    *message = &sink->message;
    return EL_OK;
}

static el_status deferred_end(void *ctx, const el_log_record *record, el_status fit)
{
    el_log_deferred_sink *const sink = ctx;
    sink->open = 0;
    const size_t size = sizeof(el_log_deferred_sink_entry_) + record->len;
    /* Whole or not at all. Only this side adds to the queue, so the room
     * seen here is still there for the write. */
    if (fit != EL_OK || el_ring_room(&sink->queue) < size) {
        return lose(sink);
    }

    el_log_deferred_sink_entry_ entry;
    entry.timestamp = record->timestamp;
    entry.tag = record->tag;
    entry.len = record->len;
    entry.ticks_per_second = record->ticks_per_second;
    entry.level = record->level;
    memcpy(sink->record, &entry, sizeof(entry));
    /* One write: the other side sees the record whole or not yet. */
    const size_t written = el_ring_write(&sink->queue, sink->record, size);
    EL_ASSERT(written == size);
    (void)written;
    return EL_OK;
}

static const el_log_sink_ops deferred_ops = {
    .begin = deferred_begin,
    .end = deferred_end,
};

void el_log_deferred_sink_init(el_log_deferred_sink *sink, void *storage, size_t storage_cap,
                               size_t message_max)
{
    EL_ASSERT(sink != NULL);
    EL_ASSERT(storage != NULL || storage_cap == 0);

    /* No storage: no record area to size, so `message_max` is not looked
     * at. A sink that loses every record, and writes nothing. */
    size_t record_size = 0;
    if (storage_cap > 0) {
        record_size = el_log_deferred_sink_record_size(message_max);
        /* Swapped arguments are here: a valid call has room for one record,
         * which a size and a message length that were exchanged does not. */
        EL_ASSERT(storage_cap >= record_size);
    }

    if (storage_cap > 0 && storage_cap >= record_size) {
        sink->record = storage;
        sink->record_cap = record_size;
        el_ring_init(&sink->queue, (uint8_t *)storage + record_size, storage_cap - record_size);
    } else {
        sink->record = NULL;
        sink->record_cap = 0;
        el_ring_init(&sink->queue, NULL, 0);
    }
    el_buf_init(&sink->message, NULL, 0);
    atomic_init(&sink->lost, 0);
    sink->open = 0;
}

size_t el_log_deferred_sink_pending_bytes(const el_log_deferred_sink *sink)
{
    EL_ASSERT(sink != NULL);
    return el_ring_len(&sink->queue);
}

el_log_sink el_log_deferred_sink_as_sink(el_log_deferred_sink *sink)
{
    EL_ASSERT(sink != NULL);
    return (el_log_sink){.ctx = sink, .ops = &deferred_ops};
}

uint32_t el_log_deferred_sink_lost(const el_log_deferred_sink *sink)
{
    EL_ASSERT(sink != NULL);
    return (uint32_t)atomic_load_explicit(&sink->lost, memory_order_acquire);
}

/* Take `len` bytes out of the ring and drop them. */
static void discard(el_ring *ring, size_t len)
{
    uint8_t dropped[16];
    while (len > 0) {
        const size_t n = el_ring_read(ring, dropped, len < sizeof(dropped) ? len : sizeof(dropped));
        EL_ASSERT(n > 0);
        len -= n;
    }
}

size_t el_log_deferred_sink_drain(el_log_deferred_sink *sink, el_log_sink to)
{
    EL_ASSERT(sink != NULL);
    /* Checked here too, so that a bad handle is caught with an empty ring. */
    EL_ASSERT(to.ops != NULL);
    EL_ASSERT(to.ops->begin != NULL);
    EL_ASSERT(to.ops->end != NULL);
    el_ring *const ring = &sink->queue;

    /* Bounded by what is there now, however fast the other side logs.
     * Records are written whole, so this is a whole number of them. */
    size_t left = el_ring_len(ring);
    size_t count = 0;
    while (left > 0) {
        el_log_deferred_sink_entry_ entry;
        const size_t got = el_ring_read(ring, &entry, sizeof(entry));
        EL_ASSERT(got == sizeof(entry));
        (void)got;
        /* Records are whole: this one is all there. */
        EL_ASSERT(left >= sizeof(entry) && left - sizeof(entry) >= entry.len);
        left -= sizeof(entry) + entry.len;
        count++;

        el_log_record record;
        record.timestamp = entry.timestamp;
        record.ticks_per_second = entry.ticks_per_second;
        record.level = entry.level;
        record.tag = entry.tag;
        record.message = NULL;
        record.len = 0;

        el_buf *message;
        size_t start;
        if (el_log_sink_begin_(to, &record, &message, &start) != EL_OK) {
            /* Lost, and `to` counts it. */
            discard(ring, entry.len);
            continue;
        }
        /* From the ring straight into the buffer `to` lent; what does not
         * fit there is cut, as el_log_write() cuts. */
        const el_status fit = el_buf_reserve(message, entry.len);
        const size_t room = el_buf_room(message);
        const size_t n = entry.len < room ? entry.len : room;
        const size_t moved = el_ring_read(ring, el_buf_spare(message), n);
        EL_ASSERT(moved == n);
        (void)moved;
        el_buf_commit(message, n);
        discard(ring, entry.len - n);
        el_status_ignore(el_log_sink_end_(to, &record, message, start, fit));
    }
    return count;
}
