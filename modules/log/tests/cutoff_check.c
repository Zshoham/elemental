/*
 * Compiled by cutoff_check.cmake with different values of EL_LOG_CUTOFF.
 * Each call's format string is a marker the check then looks for in the
 * object file: a call below the cut-off must leave nothing behind. The
 * composed messages are the same four calls under EL_LOG_KEPT: the compose
 * function and its text must go with the call.
 *
 * With CUTOFF_CHECK_PROBE defined to a level, the file is instead that
 * level's compose call alone, so that whether the object calls
 * el_log_compose answers for that one level, optimized or not.
 */
#include <elemental/log_printf.h>

/* One function for each level, so that each leaves its own marker. */
#define COMPOSE(name, text)                                    \
    static el_status name(void *ctx, el_buf *message)          \
    {                                                          \
        (void)ctx;                                             \
        return el_buf_append(message, text, sizeof(text) - 1); \
    }

#ifdef CUTOFF_CHECK_PROBE

void cutoff_check_probe(const el_logger *log);

COMPOSE(compose_probe, "cutoff-marker-probe")

void cutoff_check_probe(const el_logger *log)
{
    if (EL_LOG_KEPT(CUTOFF_CHECK_PROBE)) {
        el_log_compose(log, CUTOFF_CHECK_PROBE, compose_probe, NULL);
    }
}

#else

void cutoff_check(const el_logger *log, int value);

COMPOSE(compose_debug, "cutoff-marker-compose-debug")
COMPOSE(compose_info, "cutoff-marker-compose-info")
COMPOSE(compose_warn, "cutoff-marker-compose-warn")
COMPOSE(compose_error, "cutoff-marker-compose-error")

void cutoff_check(const el_logger *log, int value)
{
    EL_LOG_DEBUG(log, "cutoff-marker-debug %d", value);
    EL_LOG_INFO(log, "cutoff-marker-info %d", value);
    EL_LOG_WARN(log, "cutoff-marker-warn %d", value);
    EL_LOG_ERROR(log, "cutoff-marker-error %d", value);

    if (EL_LOG_KEPT(EL_LOG_LEVEL_DEBUG)) {
        el_log_compose(log, EL_LOG_LEVEL_DEBUG, compose_debug, NULL);
    }
    if (EL_LOG_KEPT(EL_LOG_LEVEL_INFO)) {
        el_log_compose(log, EL_LOG_LEVEL_INFO, compose_info, NULL);
    }
    if (EL_LOG_KEPT(EL_LOG_LEVEL_WARN)) {
        el_log_compose(log, EL_LOG_LEVEL_WARN, compose_warn, NULL);
    }
    if (EL_LOG_KEPT(EL_LOG_LEVEL_ERROR)) {
        el_log_compose(log, EL_LOG_LEVEL_ERROR, compose_error, NULL);
    }
}

#endif
