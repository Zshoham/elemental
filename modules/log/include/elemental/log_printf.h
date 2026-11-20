/*
 * elemental/log_printf.h - the C call site: printf-style logging, and the
 * compile-time cut-off.
 *
 * A convenience for C and C++ callers, layered on the logger and its
 * sink's operations (elemental/log.h) and el_fmt_vprintf()
 * (elemental/fmt_printf.h). Bindings
 * to other languages call el_log_write() instead: a variadic function
 * cannot be called portably through a C ABI (docs/conventions.md, "ABI",
 * rule 5).
 *
 * STATUS: experimental. See docs/process/api-stability.md.
 *
 *     EL_LOG_INFO(&log, "listening on port %d", port);
 *     EL_LOG_WARN(&log, "%s did not answer, attempt %d of %d", peer, attempt, attempts);
 *
 * Formats: what el_fmt_printf() accepts in this build. The compiler checks
 * argument types for literal formats. Use elemental_check_formats(target)
 * in CMake to check those literals against the portable subset, including
 * calls through EL_LOG and EL_LOG_* below the cut-off.
 *
 * Message length: a message is formatted straight into the buffer the
 * logger's sink lends (elemental/log.h), after whatever the sink put there
 * first. A log call has no buffer of its own and adds no limit: a sink over
 * a grown buffer takes a message as long as its allocator allows. One that
 * does not fit is cut by printf's item rule
 * (docs/adr/0015-formatting-into-io-buffers.md): a number is whole or
 * absent, text is cut at a byte. The sink is told it is cut, and decides:
 * the writer sink writes the cut line, the memory sink keeps nothing, and
 * both count the record as lost.
 *
 * The cut-off (docs/adr/0021-log-cutoff.md). EL_LOG_CUTOFF is a macro the
 * consumer may define, for a target or for one file, before this header is
 * included, to one of the EL_LOG_LEVEL_* values:
 *
 *     target_compile_definitions(app PRIVATE EL_LOG_CUTOFF=EL_LOG_LEVEL_INFO)
 *
 * A call through the EL_LOG_* macros below that level compiles to nothing:
 * no call, no format string in the image. Not defined, every call is kept.
 * It is the consumer's own setting, like NDEBUG: Elemental's configuration
 * never defines it, and the library has no compile-time level, so the two
 * cannot disagree. What follows from it:
 *
 *   - The arguments of a call below the cut-off are not evaluated. They
 *     must have no side effect the program depends on.
 *   - Such a call is still compiled before it is dropped, so a format that
 *     does not match its arguments is reported either way.
 *   - Only the macros honor it. el_log_printf() and el_log_write() are
 *     functions, and always do what they say.
 *   - The run-time level still applies to what is kept: a call at or above
 *     the cut-off logs only if the logger's minimum lets it through.
 *
 * Concurrency: as el_log_write(). Nothing here adds state.
 *
 * Contract violations, checked by assertions: those of el_log_write() and
 * of el_fmt_vprintf() for the format and its arguments.
 */
#ifndef ELEMENTAL_LOG_PRINTF_H
#define ELEMENTAL_LOG_PRINTF_H

#include <stdarg.h>

#include <elemental/fmt_printf.h>
#include <elemental/log.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Format a message as el_fmt_printf() does, into the buffer the logger's
 * sink lends, and log it at `level` (below EL_LOG_LEVEL_OFF). If `level` is
 * below the logger's minimum, or the sink does not begin the record,
 * nothing is formatted and no argument is read.
 */
void el_log_printf(const el_logger *logger, el_log_level level, const char *format, ...)
    EL_FMT_PRINTF_CHECK(3, 4);
void el_log_vprintf(const el_logger *logger, el_log_level level, const char *format, va_list args)
    EL_FMT_PRINTF_CHECK(3, 0);

#ifndef EL_LOG_CUTOFF
#define EL_LOG_CUTOFF EL_LOG_LEVEL_DEBUG
#endif

/*
 * Log at `level`, unless `level` is below EL_LOG_CUTOFF, in which case the
 * statement compiles to nothing. The condition is a constant, so there is
 * no test at run time.
 */
#define EL_LOG(logger, level, ...)                         \
    do {                                                   \
        if ((level) >= EL_LOG_CUTOFF) {                    \
            el_log_printf((logger), (level), __VA_ARGS__); \
        }                                                  \
    } while (0)

#define EL_LOG_DEBUG(logger, ...) EL_LOG((logger), EL_LOG_LEVEL_DEBUG, __VA_ARGS__)
#define EL_LOG_INFO(logger, ...) EL_LOG((logger), EL_LOG_LEVEL_INFO, __VA_ARGS__)
#define EL_LOG_WARN(logger, ...) EL_LOG((logger), EL_LOG_LEVEL_WARN, __VA_ARGS__)
#define EL_LOG_ERROR(logger, ...) EL_LOG((logger), EL_LOG_LEVEL_ERROR, __VA_ARGS__)

#ifdef __cplusplus
}
#endif

#endif /* ELEMENTAL_LOG_PRINTF_H */
