/*
 * Compiled by cutoff_check.cmake with different values of EL_LOG_CUTOFF.
 * Each call's format string is a marker the check then looks for in the
 * object file: a call below the cut-off must leave nothing behind.
 */
#include <elemental/log_printf.h>

void cutoff_check(const el_logger *log, int value);

void cutoff_check(const el_logger *log, int value)
{
    EL_LOG_DEBUG(log, "cutoff-marker-debug %d", value);
    EL_LOG_INFO(log, "cutoff-marker-info %d", value);
    EL_LOG_WARN(log, "cutoff-marker-warn %d", value);
    EL_LOG_ERROR(log, "cutoff-marker-error %d", value);
}
