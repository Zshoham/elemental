#include "elemental_config.h"

#include <elemental/assert.h>
#include <elemental/log_deferred.h>

#include <stdatomic.h>
#include <string.h>

#include "log_private.h"

/*
 * What travels through the ring for one record, followed by its message
 * bytes. Entries are copied in and out, so neither the record storage nor
 * the ring's needs an alignment. The frequency is in every entry: one sink
 * may serve loggers with different clocks.
 */
typedef struct deferred_entry {
    el_ticks timestamp;
    const char *tag;
    size_t len;
    uint32_t ticks_per_second;
    el_log_level level;
} deferred_entry;

size_t el_log_deferred_sink_record_size(size_t message_len)
{
    EL_ASSERT(message_len <= SIZE_MAX - sizeof(deferred_entry));
    return sizeof(deferred_entry) + message_len;
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
    /* Nothing is formatted for a record that cannot be kept. */
    if (sink->record_cap < sizeof(deferred_entry) ||
        el_ring_room(sink->ring) < sizeof(deferred_entry)) {
        return lose(sink);
    }
    el_buf_init(&sink->message, sink->record + sizeof(deferred_entry),
                sink->record_cap - sizeof(deferred_entry));
    *message = &sink->message;
    return EL_OK;
}

static el_status deferred_end(void *ctx, const el_log_record *record, el_status fit)
{
    el_log_deferred_sink *const sink = ctx;
    const size_t size = sizeof(deferred_entry) + record->len;
    /* Whole or not at all. Only this side adds to the ring, so the room
     * seen here is still there for the write. */
    if (fit != EL_OK || el_ring_room(sink->ring) < size) {
        return lose(sink);
    }

    deferred_entry entry;
    entry.timestamp = record->timestamp;
    entry.tag = record->tag;
    entry.len = record->len;
    entry.ticks_per_second = record->ticks_per_second;
    entry.level = record->level;
    memcpy(sink->record, &entry, sizeof(entry));
    /* One write: the other side sees the record whole or not yet. */
    const size_t written = el_ring_write(sink->ring, sink->record, size);
    EL_ASSERT(written == size);
    (void)written;
    return EL_OK;
}

static const el_log_sink_ops deferred_ops = {
    .begin = deferred_begin,
    .end = deferred_end,
};

void el_log_deferred_sink_init(el_log_deferred_sink *sink, el_ring *ring, void *record,
                               size_t record_cap)
{
    EL_ASSERT(sink != NULL);
    EL_ASSERT(ring != NULL);
    EL_ASSERT(record != NULL || record_cap == 0);
    sink->ring = ring;
    sink->record = record;
    sink->record_cap = record_cap;
    el_buf_init(&sink->message, NULL, 0);
    atomic_init(&sink->lost, 0);
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
    el_ring *const ring = sink->ring;

    /* Bounded by what is there now, however fast the other side logs.
     * Records are written whole, so this is a whole number of them. */
    size_t left = el_ring_len(ring);
    size_t count = 0;
    while (left > 0) {
        deferred_entry entry;
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
