#include "elemental_config.h"

#include <elemental/assert.h>
#include <elemental/log.h>

#include "log_private.h"

const char *el_log_level_name(el_log_level level)
{
    switch (level) {
    case EL_LOG_LEVEL_DEBUG:
        return "debug";
    case EL_LOG_LEVEL_INFO:
        return "info";
    case EL_LOG_LEVEL_WARN:
        return "warn";
    case EL_LOG_LEVEL_ERROR:
        return "error";
    case EL_LOG_LEVEL_OFF:
        return "off";
    default:
        return "unknown";
    }
}

void el_log_record_start_(const el_logger *logger, el_log_level level, el_log_record *record)
{
    record->timestamp = el_clock_now(logger->clock);
    record->ticks_per_second = logger->ticks_per_second;
    record->level = level;
    record->tag = logger->tag;
    record->message = NULL;
    record->len = 0;
}

el_status el_log_sink_begin_(el_log_sink sink, const el_log_record *record, el_buf **message,
                             size_t *start)
{
    EL_ASSERT(sink.ops != NULL);
    EL_ASSERT(sink.ops->begin != NULL);
    EL_ASSERT(sink.ops->end != NULL);
    EL_ASSERT(record != NULL);
    EL_ASSERT(record->level < EL_LOG_LEVEL_OFF);
    EL_ASSERT(record->ticks_per_second >= 1);
    EL_ASSERT(record->tag != NULL);
    EL_ASSERT(record->message == NULL && record->len == 0);

    el_buf *lent = NULL;
    EL_TRY(sink.ops->begin(sink.ctx, record, &lent));
    /* A sink that lent no buffer is caught by el_buf_len()'s own check. */
    *message = lent;
    *start = el_buf_len(lent);
    return EL_OK;
}

el_status el_log_sink_end_(el_log_sink sink, el_log_record *record, const el_buf *message,
                           size_t start, el_status fit)
{
    /* Only appended to since begin: nothing before `start` was taken back. */
    EL_ASSERT(el_buf_len(message) >= start);
    record->len = el_buf_len(message) - start;
    record->message = record->len > 0 ? (const char *)el_buf_data(message) + start : NULL;
    return sink.ops->end(sink.ctx, record, fit);
}

/*
 * Begin `record` (message null, len 0), append `len` bytes of `text` to the
 * buffer the sink lends, cut at a byte if they do not fit, and end it.
 */
static el_status sink_put(el_log_sink sink, el_log_record *record, const char *text, size_t len)
{
    el_buf *message;
    size_t start;
    EL_TRY(el_log_sink_begin_(sink, record, &message, &start));

    el_status fit = el_buf_append(message, text, len);
    if (fit != EL_OK) {
        /* Cannot fail: the room is there without growing, and is less than
         * len. */
        el_status_ignore(el_buf_append(message, text, el_buf_room(message)));
    }
    return el_log_sink_end_(sink, record, message, start, fit);
}

el_status el_log_sink_write(el_log_sink sink, const el_log_record *record)
{
    EL_ASSERT(record != NULL);
    EL_ASSERT(record->message != NULL || record->len == 0);

    el_log_record begun = *record;
    begun.message = NULL;
    begun.len = 0;
    return sink_put(sink, &begun, record->message, record->len);
}

void el_logger_init(el_logger *logger, el_log_sink sink, el_clock clock, const char *tag,
                    el_log_level level_min)
{
    EL_ASSERT(logger != NULL);
    EL_ASSERT(sink.ops != NULL);
    EL_ASSERT(tag != NULL);
    EL_ASSERT(level_min <= EL_LOG_LEVEL_OFF);
    logger->sink = sink;
    logger->clock = clock;
    logger->tag = tag;
    /* Constant for the life of the clock, so it is asked for once. */
    logger->ticks_per_second = el_clock_ticks_per_second(clock);
    logger->level_min = level_min;
}

void el_logger_set_level(el_logger *logger, el_log_level level_min)
{
    EL_ASSERT(logger != NULL);
    EL_ASSERT(level_min <= EL_LOG_LEVEL_OFF);
    logger->level_min = level_min;
}

void el_log_write(const el_logger *logger, el_log_level level, const char *message, size_t len)
{
    EL_ASSERT(logger != NULL);
    /* Checked here too, so that a bad call is caught even when the record
     * is not made. A level that is not a record's level is never below a
     * minimum, so el_log_sink_write() always sees that one. */
    EL_ASSERT(message != NULL || len == 0);
    if (level < logger->level_min) {
        return;
    }

    el_log_record record;
    el_log_record_start_(logger, level, &record);
    /* A record the sink does not take is lost, and the sink counts it. */
    el_status_ignore(sink_put(logger->sink, &record, message, len));
}
