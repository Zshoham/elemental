#include "elemental_config.h"

#include <elemental/assert.h>
#include <elemental/log_mem.h>

#include <string.h>

/*
 * What is kept of one record, followed by its message bytes: the entry in
 * log_mem.h. Entries are packed one after another and copied in and out, so
 * the storage needs no alignment.
 */
size_t el_log_mem_sink_record_size(size_t message_len)
{
    EL_ASSERT(message_len <= SIZE_MAX - sizeof(el_log_mem_sink_entry_));
    return sizeof(el_log_mem_sink_entry_) + message_len;
}

static el_status mem_sink_begin(void *ctx, const el_log_record *record, el_buf **message)
{
    (void)record;
    el_log_mem_sink *const sink = ctx;
    /* A record already open here is being built where this one would be. */
    EL_ASSERT(sink->open == 0);
    const size_t room = sink->cap - sink->len;
    if (room < sizeof(el_log_mem_sink_entry_)) {
        sink->lost++;
        return EL_ERR_NO_SPACE;
    }
    /* The message goes where it is kept: after the entry, which end writes
     * once the length is known. */
    el_buf_init(&sink->message, sink->storage + sink->len + sizeof(el_log_mem_sink_entry_),
                room - sizeof(el_log_mem_sink_entry_));
    sink->open = 1;
    *message = &sink->message;
    return EL_OK;
}

static el_status mem_sink_end(void *ctx, const el_log_record *record, el_status fit)
{
    el_log_mem_sink *const sink = ctx;
    sink->open = 0;
    if (fit != EL_OK) {
        /* Whole or not at all: the cut message's bytes are left unused. */
        sink->lost++;
        return EL_ERR_NO_SPACE;
    }

    el_log_mem_sink_entry_ entry;
    entry.timestamp = record->timestamp;
    entry.tag = record->tag;
    entry.len = record->len;
    entry.ticks_per_second = record->ticks_per_second;
    entry.level = record->level;
    memcpy(sink->storage + sink->len, &entry, sizeof(entry));
    sink->len += sizeof(entry) + record->len;
    sink->count++;
    return EL_OK;
}

static const el_log_sink_ops mem_sink_ops = {
    .begin = mem_sink_begin,
    .end = mem_sink_end,
};

void el_log_mem_sink_init(el_log_mem_sink *sink, void *storage, size_t cap)
{
    EL_ASSERT(sink != NULL);
    EL_ASSERT(storage != NULL || cap == 0);
    sink->storage = storage;
    sink->cap = cap;
    sink->open = 0;
    el_log_mem_sink_clear(sink);
}

el_log_sink el_log_mem_sink_as_sink(el_log_mem_sink *sink)
{
    EL_ASSERT(sink != NULL);
    return (el_log_sink){.ctx = sink, .ops = &mem_sink_ops};
}

size_t el_log_mem_sink_count(const el_log_mem_sink *sink)
{
    EL_ASSERT(sink != NULL);
    return sink->count;
}

void el_log_mem_sink_get(const el_log_mem_sink *sink, size_t index, el_log_record *record)
{
    EL_ASSERT(sink != NULL);
    EL_ASSERT(record != NULL);
    EL_ASSERT(index < sink->count);

    /* Entries have no index: walk from the oldest. */
    const uint8_t *at = sink->storage;
    el_log_mem_sink_entry_ entry;
    memcpy(&entry, at, sizeof(entry));
    for (size_t i = 0; i < index; i++) {
        at += sizeof(entry) + entry.len;
        memcpy(&entry, at, sizeof(entry));
    }

    record->timestamp = entry.timestamp;
    record->ticks_per_second = entry.ticks_per_second;
    record->level = entry.level;
    record->tag = entry.tag;
    record->message = (const char *)(at + sizeof(entry));
    record->len = entry.len;
}

uint32_t el_log_mem_sink_lost(const el_log_mem_sink *sink)
{
    EL_ASSERT(sink != NULL);
    return sink->lost;
}

void el_log_mem_sink_clear(el_log_mem_sink *sink)
{
    EL_ASSERT(sink != NULL);
    sink->len = 0;
    sink->count = 0;
    sink->lost = 0;
}
