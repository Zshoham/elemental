#include "elemental_config.h"

#include <elemental/assert.h>
#include <elemental/log.h>

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

el_status el_log_sink_write(el_log_sink sink, const el_log_record *record)
{
    EL_ASSERT(sink.ops != NULL);
    EL_ASSERT(sink.ops->write != NULL);
    EL_ASSERT(record != NULL);
    EL_ASSERT(record->level < EL_LOG_LEVEL_OFF);
    EL_ASSERT(record->ticks_per_second >= 1);
    EL_ASSERT(record->tag != NULL);
    EL_ASSERT(record->message != NULL || record->len == 0);
    return sink.ops->write(sink.ctx, record);
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
    record.timestamp = el_clock_now(logger->clock);
    record.ticks_per_second = logger->ticks_per_second;
    record.level = level;
    record.tag = logger->tag;
    record.message = message;
    record.len = len;
    /* A record the sink does not take is lost, and the sink counts it. */
    el_status_ignore(el_log_sink_write(logger->sink, &record));
}
