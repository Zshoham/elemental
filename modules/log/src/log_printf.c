#include "elemental_config.h"

#include <elemental/io_buf.h>
#include <elemental/log_printf.h>

#include "log_private.h"

void el_log_vprintf(const el_logger *logger, el_log_level level, const char *format, va_list args)
{
    el_log_open_record_ open;
    /* Before anything is formatted: a record that will not be made, or is
     * lost where it begins, costs no formatting. */
    if (!el_log_open_(logger, level, &open)) {
        return;
    }
    /* Straight into the sink's buffer. EL_ERR_NO_SPACE: it did not fit, and
     * the sink is told that the message is cut. */
    el_log_close_(&open, el_fmt_vprintf(open.message, format, args));
}

void el_log_printf(const el_logger *logger, el_log_level level, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    el_log_vprintf(logger, level, format, args);
    va_end(args);
}
