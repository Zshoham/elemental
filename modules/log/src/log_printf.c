#include "elemental_config.h"

#include <elemental/assert.h>
#include <elemental/io_buf.h>
#include <elemental/log_printf.h>

void el_log_vprintf(const el_logger *logger, el_log_level level, const char *format, va_list args)
{
    EL_ASSERT(logger != NULL);
    /* Before anything is formatted: a record that will not be made costs
     * one comparison. */
    if (level < logger->level_min) {
        return;
    }

    char storage[EL_CONFIG_LOG_MESSAGE_MAX];
    el_buf message;
    el_buf_init(&message, storage, sizeof(storage));
    /* EL_ERR_NO_SPACE: the message was cut to fit, and is logged cut. */
    el_status_ignore(el_fmt_vprintf(&message, format, args));
    el_log_write(logger, level, storage, el_buf_len(&message));
}

void el_log_printf(const el_logger *logger, el_log_level level, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    el_log_vprintf(logger, level, format, args);
    va_end(args);
}
