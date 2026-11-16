/*
 * Contract fixture for the tee sink, over two memory sinks. What it
 * delivers is read from the second of them, the one a tee that stopped
 * early would starve.
 *
 * A tee has no storage of its own to fill, so `sink_full_after` is left
 * null and the suite reports that test as skipped. A fixture never fakes a
 * capability.
 */
#include "tee_sink_fixture.h"

#include "tee_sink.h"

#include <elemental/log_mem.h>

#include <string.h>

#define STORAGE_SIZE (EL_LOG_CONTRACT_RECORDS_MAX * (EL_LOG_CONTRACT_MESSAGE_MAX + 64u))

static uint8_t first_storage[STORAGE_SIZE];
static uint8_t second_storage[STORAGE_SIZE];
static el_log_mem_sink first;
static el_log_mem_sink second;
static tee_sink tee;

static void reset(void *ctx)
{
    (void)ctx;
    el_log_mem_sink_init(&first, NULL, 0);
    el_log_mem_sink_init(&second, NULL, 0);
}

static el_log_sink sink(void *ctx)
{
    (void)ctx;
    el_log_mem_sink_init(&first, first_storage, sizeof(first_storage));
    el_log_mem_sink_init(&second, second_storage, sizeof(second_storage));
    tee_sink_init(&tee, el_log_mem_sink_as_sink(&first), el_log_mem_sink_as_sink(&second));
    return tee_sink_as_sink(&tee);
}

static size_t delivered(void *ctx)
{
    (void)ctx;
    return el_log_mem_sink_count(&second);
}

static size_t message(void *ctx, size_t index, char *out, size_t cap)
{
    (void)ctx;
    el_log_record record;
    el_log_mem_sink_get(&second, index, &record);
    memcpy(out, record.message, record.len < cap ? record.len : cap);
    return record.len;
}

static el_log_level level(void *ctx, size_t index)
{
    (void)ctx;
    el_log_record record;
    el_log_mem_sink_get(&second, index, &record);
    return record.level;
}

static size_t tag(void *ctx, size_t index, char *out, size_t cap)
{
    (void)ctx;
    el_log_record record;
    el_log_mem_sink_get(&second, index, &record);
    const size_t len = strlen(record.tag);
    memcpy(out, record.tag, len < cap ? len : cap);
    return len;
}

static void time_of(void *ctx, size_t index, el_ticks *timestamp, uint32_t *ticks_per_second)
{
    (void)ctx;
    el_log_record record;
    el_log_mem_sink_get(&second, index, &record);
    *timestamp = record.timestamp;
    *ticks_per_second = record.ticks_per_second;
}

const el_log_contract_fixture tee_sink_fixture = {
    .name = "tee_sink",
    .ctx = NULL,
    .reset = reset,
    .sink = sink,
    .delivered = delivered,
    .message = message,
    .level = level,
    .tag = tag,
    .time = time_of,
    .sink_full_after = NULL,
};
