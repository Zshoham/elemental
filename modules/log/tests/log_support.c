#include "log_support.h"

#include <string.h>

/* ---- recording_sink ------------------------------------------------------ */

static el_status recording_begin(void *ctx, const el_log_record *record, el_buf **message)
{
    recording_sink *const sink = ctx;
    sink->begins++;
    sink->last_ctx = ctx;
    sink->begun = *record;
    if (sink->begin_answer != EL_OK) {
        return sink->begin_answer;
    }
    if (sink->lend == &sink->buf) {
        el_buf_truncate(&sink->buf, 0);
    }
    *message = sink->lend;
    return EL_OK;
}

static el_status recording_end(void *ctx, const el_log_record *record, el_status fit)
{
    recording_sink *const sink = ctx;
    sink->calls++;
    sink->last_ctx = ctx;
    sink->last = *record;
    sink->last_message_pointer = record->message;
    sink->last_fit = fit;
    const size_t kept = record->len < sizeof(sink->message) ? record->len : sizeof(sink->message);
    if (kept > 0) {
        memcpy(sink->message, record->message, kept);
    }
    /* The record's pointer is not valid after the call: keep the copy. */
    sink->last.message = sink->message;
    return sink->answer;
}

static const el_log_sink_ops recording_ops = {.begin = recording_begin, .end = recording_end};
static const el_log_sink_ops ops_without_begin = {.begin = NULL, .end = recording_end};
static const el_log_sink_ops ops_without_end = {.begin = recording_begin, .end = NULL};

void recording_sink_init(recording_sink *sink)
{
    sink->begin_answer = EL_OK;
    sink->answer = EL_OK;
    sink->begins = 0;
    sink->calls = 0;
    sink->last_message_pointer = NULL;
    sink->last_fit = EL_OK;
    sink->last_ctx = NULL;
    sink->last.len = 0;
    sink->last.message = sink->message;
    sink->last.tag = "";
    sink->last.level = EL_LOG_LEVEL_OFF;
    sink->last.timestamp = 0;
    sink->last.ticks_per_second = 0;
    sink->begun = sink->last;
    sink->lend = &sink->buf;
    recording_sink_set_capacity(sink, sizeof(sink->storage));
}

void recording_sink_set_capacity(recording_sink *sink, size_t cap)
{
    el_buf_init(&sink->buf, sink->storage, cap);
}

el_log_sink recording_sink_as_sink(recording_sink *sink)
{
    return (el_log_sink){.ctx = sink, .ops = &recording_ops};
}

el_log_sink recording_sink_without_begin(recording_sink *sink)
{
    return (el_log_sink){.ctx = sink, .ops = &ops_without_begin};
}

el_log_sink recording_sink_without_end(recording_sink *sink)
{
    return (el_log_sink){.ctx = sink, .ops = &ops_without_end};
}

/* ---- counting_clock ------------------------------------------------------ */

static el_ticks counting_now(void *ctx)
{
    counting_clock *const clock = ctx;
    clock->now_calls++;
    return clock->reading;
}

static uint32_t counting_ticks_per_second(void *ctx)
{
    counting_clock *const clock = ctx;
    clock->ticks_per_second_calls++;
    return clock->ticks_per_second;
}

static const el_clock_ops counting_ops = {
    .now = counting_now,
    .ticks_per_second = counting_ticks_per_second,
};

void counting_clock_init(counting_clock *clock, uint32_t ticks_per_second)
{
    clock->reading = 0;
    clock->ticks_per_second = ticks_per_second;
    clock->now_calls = 0;
    clock->ticks_per_second_calls = 0;
}

el_clock counting_clock_as_clock(counting_clock *clock)
{
    return (el_clock){.ctx = clock, .ops = &counting_ops};
}

/* ---- scripted_writer ----------------------------------------------------- */

static el_status scripted_write(void *ctx, const void *data, size_t len, size_t *n)
{
    scripted_writer *const writer = ctx;
    writer->calls++;
    if (writer->len >= writer->fail_after) {
        return writer->failure;
    }
    size_t take = len;
    if (writer->chunk != 0 && take > writer->chunk) {
        take = writer->chunk;
    }
    if (take > writer->fail_after - writer->len) {
        take = writer->fail_after - writer->len;
    }
    memcpy(writer->bytes + writer->len, data, take);
    writer->len += take;
    *n = take;
    return EL_OK;
}

static const el_writer_ops scripted_ops = {.write = scripted_write};

void scripted_writer_init(scripted_writer *writer)
{
    writer->len = 0;
    writer->calls = 0;
    writer->chunk = 0;
    writer->fail_after = sizeof(writer->bytes);
    writer->failure = EL_ERR_NO_SPACE;
}

el_writer scripted_writer_as_writer(scripted_writer *writer)
{
    return (el_writer){.ctx = writer, .ops = &scripted_ops};
}
