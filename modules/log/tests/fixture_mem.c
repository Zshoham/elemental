/*
 * Contract fixture for the memory sink: the smallest complete fixture. The
 * sink keeps records as records, so everything a record holds can be read
 * back, and its storage is a number of bytes, so it can be made to be full
 * after any number of records.
 */
#include "fixture_mem.h"

#include <elemental/log_mem.h>

#include <string.h>

/* Room for the most records the suite writes, with a little alignment
 * slack: sized in the functions below, by el_log_mem_sink_record_size(). */
static uint8_t storage[EL_LOG_CONTRACT_RECORDS_MAX * (EL_LOG_CONTRACT_MESSAGE_MAX + 64u)];
static el_log_mem_sink mem;

static void reset(void *ctx)
{
    (void)ctx;
    el_log_mem_sink_init(&mem, NULL, 0);
}

static el_log_sink sink(void *ctx)
{
    (void)ctx;
    el_log_mem_sink_init(&mem, storage, sizeof(storage));
    return el_log_mem_sink_as_sink(&mem);
}

static el_log_sink sink_full_after(void *ctx, size_t records)
{
    (void)ctx;
    /* Exactly `records` of the suite's longest records, and one byte short
     * of another. */
    const size_t one = el_log_mem_sink_record_size(EL_LOG_CONTRACT_MESSAGE_MAX);
    el_log_mem_sink_init(&mem, storage, records * one + one - 1);
    return el_log_mem_sink_as_sink(&mem);
}

static size_t delivered(void *ctx)
{
    (void)ctx;
    return el_log_mem_sink_count(&mem);
}

static size_t message(void *ctx, size_t index, char *out, size_t cap)
{
    (void)ctx;
    el_log_record record;
    el_log_mem_sink_get(&mem, index, &record);
    memcpy(out, record.message, record.len < cap ? record.len : cap);
    return record.len;
}

static el_log_level level(void *ctx, size_t index)
{
    (void)ctx;
    el_log_record record;
    el_log_mem_sink_get(&mem, index, &record);
    return record.level;
}

static size_t tag(void *ctx, size_t index, char *out, size_t cap)
{
    (void)ctx;
    el_log_record record;
    el_log_mem_sink_get(&mem, index, &record);
    const size_t len = strlen(record.tag);
    memcpy(out, record.tag, len < cap ? len : cap);
    return len;
}

static void time_of(void *ctx, size_t index, el_ticks *timestamp, uint32_t *ticks_per_second)
{
    (void)ctx;
    el_log_record record;
    el_log_mem_sink_get(&mem, index, &record);
    *timestamp = record.timestamp;
    *ticks_per_second = record.ticks_per_second;
}

const el_log_contract_fixture log_fixture_mem = {
    .name = "mem",
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
