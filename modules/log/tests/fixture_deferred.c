/*
 * Contract fixture for the deferred sink. What it delivers is in its queue
 * until it is drained, so the fixture drains it, into a memory sink, each
 * time the suite looks: records are kept as records all the way, and
 * everything a record holds can be read back. The queue is a number of
 * bytes, so the sink can be made to be full after any number of records.
 */
#include "fixture_deferred.h"

#include <elemental/log_deferred.h>
#include <elemental/log_mem.h>

#include <string.h>

/* The record area, and room for the most records the suite writes. */
static uint8_t storage[EL_LOG_DEFERRED_SINK_STORAGE_SIZE(EL_LOG_CONTRACT_RECORDS_MAX,
                                                         EL_LOG_CONTRACT_MESSAGE_MAX)];
/* The same records again in the memory sink, sized by its own macro. */
static uint8_t kept_storage[EL_LOG_MEM_SINK_STORAGE_SIZE(EL_LOG_CONTRACT_RECORDS_MAX,
                                                         EL_LOG_CONTRACT_MESSAGE_MAX)];
static el_log_deferred_sink deferred;
static el_log_mem_sink kept;

static el_log_sink open_with_storage(size_t storage_cap)
{
    el_log_deferred_sink_init(&deferred, storage, storage_cap, EL_LOG_CONTRACT_MESSAGE_MAX);
    el_log_mem_sink_init(&kept, kept_storage, sizeof(kept_storage));
    return el_log_deferred_sink_as_sink(&deferred);
}

static void reset(void *ctx)
{
    (void)ctx;
    el_log_deferred_sink_init(&deferred, NULL, 0, EL_LOG_CONTRACT_MESSAGE_MAX);
    el_log_mem_sink_init(&kept, NULL, 0);
}

static el_log_sink sink(void *ctx)
{
    (void)ctx;
    return open_with_storage(sizeof(storage));
}

static el_log_sink sink_full_after(void *ctx, size_t records)
{
    (void)ctx;
    /* The record area, then exactly `records` of the suite's longest
     * records, and one byte short of another. Nothing is drained until the
     * suite looks. */
    const size_t one = el_log_deferred_sink_record_size(EL_LOG_CONTRACT_MESSAGE_MAX);
    return open_with_storage(one + records * one + one - 1);
}

/* Everything the sink took is in its queue or already kept. */
static void drain(void)
{
    (void)el_log_deferred_sink_drain(&deferred, el_log_mem_sink_as_sink(&kept));
}

static size_t delivered(void *ctx)
{
    (void)ctx;
    drain();
    return el_log_mem_sink_count(&kept);
}

static size_t message(void *ctx, size_t index, char *out, size_t cap)
{
    (void)ctx;
    drain();
    el_log_record record;
    el_log_mem_sink_get(&kept, index, &record);
    if (record.len > 0) {
        memcpy(out, record.message, record.len < cap ? record.len : cap);
    }
    return record.len;
}

static el_log_level level(void *ctx, size_t index)
{
    (void)ctx;
    drain();
    el_log_record record;
    el_log_mem_sink_get(&kept, index, &record);
    return record.level;
}

static size_t tag(void *ctx, size_t index, char *out, size_t cap)
{
    (void)ctx;
    drain();
    el_log_record record;
    el_log_mem_sink_get(&kept, index, &record);
    const size_t len = strlen(record.tag);
    memcpy(out, record.tag, len < cap ? len : cap);
    return len;
}

static void time_of(void *ctx, size_t index, el_ticks *timestamp, uint32_t *ticks_per_second)
{
    (void)ctx;
    drain();
    el_log_record record;
    el_log_mem_sink_get(&kept, index, &record);
    *timestamp = record.timestamp;
    *ticks_per_second = record.ticks_per_second;
}

const el_log_contract_fixture log_fixture_deferred = {
    .name = "deferred",
    .ctx = NULL,
    .reset = reset,
    .sink = sink,
    .delivered = delivered,
    .message = message,
    .level = level,
    .tag = tag,
    .time = time_of,
    .sink_full_after = sink_full_after,
};
