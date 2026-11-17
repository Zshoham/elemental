#include "elemental_config.h"

#include <elemental/assert.h>
#include <elemental/io_buf.h>
#include <elemental/log_printf.h>

#include "log_private.h"

void el_log_vprintf(const el_logger *logger, el_log_level level, const char *format, va_list args)
{
    EL_ASSERT(logger != NULL);
    /* Before anything is formatted: a record that will not be made costs
     * one comparison. */
    if (level < logger->level_min) {
        return;
    }

    el_log_record record;
    el_log_record_start_(logger, level, &record);
    el_buf *message;
    size_t start;
    if (el_log_sink_begin_(logger->sink, &record, &message, &start) != EL_OK) {
        /* Lost, and counted by the sink: nothing is formatted. */
        return;
    }
    /* Straight into the sink's buffer. EL_ERR_NO_SPACE: it did not fit, and
     * the sink is told that the message is cut. */
    const el_status fit = el_fmt_vprintf(message, format, args);
    /* A record the sink does not take is lost, and the sink counts it. */
    el_status_ignore(el_log_sink_end_(logger->sink, &record, message, start, fit));
}

void el_log_printf(const el_logger *logger, el_log_level level, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    el_log_vprintf(logger, level, format, args);
    va_end(args);
}
