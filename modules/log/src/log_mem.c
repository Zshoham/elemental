#include "elemental_config.h"

#include <elemental/assert.h>
#include <elemental/log_mem.h>

#include <string.h>

/*
 * What is kept of one record, followed by its message bytes. Entries are
 * packed one after another and copied in and out, so the storage needs no
 * alignment.
 */
typedef struct mem_entry {
    el_ticks timestamp;
    const char *tag;
    size_t len;
    uint32_t ticks_per_second;
    el_log_level level;
} mem_entry;

size_t el_log_mem_sink_record_size(size_t message_len)
{
    EL_ASSERT(message_len <= SIZE_MAX - sizeof(mem_entry));
    return sizeof(mem_entry) + message_len;
}

static el_status mem_sink_write(void *ctx, const el_log_record *record)
{
    el_log_mem_sink *const sink = ctx;
    const size_t room = sink->cap - sink->len;
    if (room < sizeof(mem_entry) || room - sizeof(mem_entry) < record->len) {
        sink->lost++;
        return EL_ERR_NO_SPACE;
    }

    mem_entry entry;
    entry.timestamp = record->timestamp;
    entry.tag = record->tag;
    entry.len = record->len;
    entry.ticks_per_second = record->ticks_per_second;
    entry.level = record->level;
    uint8_t *const at = sink->storage + sink->len;
    memcpy(at, &entry, sizeof(entry));
    if (record->len > 0) {
        memcpy(at + sizeof(entry), record->message, record->len);
    }
    sink->len += sizeof(entry) + record->len;
    sink->count++;
    return EL_OK;
}

static const el_log_sink_ops mem_sink_ops = {
    .write = mem_sink_write,
};

void el_log_mem_sink_init(el_log_mem_sink *sink, void *storage, size_t cap)
{
    EL_ASSERT(sink != NULL);
    EL_ASSERT(storage != NULL || cap == 0);
    sink->storage = storage;
    sink->cap = cap;
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
    mem_entry entry;
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
