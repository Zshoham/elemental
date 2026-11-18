#include <inttypes.h>

#define EL_LOG_CUTOFF EL_LOG_LEVEL_ERROR
#include <elemental/fmt_printf.h>
#include <elemental/log_printf.h>

#define LOG_FORMAT \
    "temp "        \
    "\x25"         \
    "f"
#define TARGET_INTMAX \
    "j"               \
    "u"

void print_here(int tag, const char *format, ...) EL_FMT_PRINTF_CHECK(2, 3);

void portable_rejects(el_logger *log, el_buf *buf, el_buffered_writer *writer, va_list args)
{
    EL_LOG(log, EL_LOG_LEVEL_INFO, "%f", 1.0);                      // reject: '%f'|conversion
    EL_LOG_DEBUG(log, "%-s", "abc");                                // reject: '%-s'|flags
    EL_LOG_INFO(log, "%+d", 42);                                    // reject: '%+d'|flags
    EL_LOG_WARN(log, "% d", 42);                                    // reject: '% d'|flags
    EL_LOG_ERROR(log, "%#x", 42u);                                  // reject: '%#x'|flags
    EL_LOG_ERROR(log, LOG_FORMAT, 1.0);                             // reject: '%f'|conversion
    EL_LOG_ERROR(log, "%" PRIo64, UINT64_C(42));                    // reject: conversion %o
    EL_LOG_ERROR(log, "%" TARGET_INTMAX, (uintmax_t)42);            // reject: '%ju'|length
    EL_LOG_ERROR(log, "%td", (ptrdiff_t)42);                        // reject: '%td'|length
    EL_LOG_ERROR(log, "%Lf", 1.0L);                                 // reject: '%Lf'|conversion
    el_log_printf(log, EL_LOG_LEVEL_DEBUG, "%.*d", 2, 42);          // reject: '%.*d'|precision
    el_log_vprintf(log, EL_LOG_LEVEL_DEBUG, "%*s", args);           // reject: '%*s'|width
    el_status_ignore(el_fmt_printf(buf, "%03c", 'x'));              // reject: '%03c'|flags
    el_status_ignore(el_fmt_vprintf(buf, "%0s", args));             // reject: '%0s'|flags
    el_status_ignore(el_fmt_printf_buffered(writer, "%.2c", 'x'));  // reject: '%.2c'|precision
    el_status_ignore(el_fmt_vprintf_buffered(writer, "%2%", args)); // reject: '%2%'|literal percent
    print_here(1, "%ls", L"abc");                                   // reject: '%ls'|length
    print_here(1, "%hc", 'a');                                      // reject: '%hc'|length
    print_here(1, "%lc", L'a');                                     // reject: '%lc'|length
    // reject next: '%n'|conversion; '%p'|conversion; '%.2u'|precision; '%'|incomplete
    print_here(1, "%n %p %.2u %", (int *)0, (void *)0, 42u);
    print_here(1, "%2$d", 42, 42);                        // reject: '%2$'|conversion
    print_here(1, "%99999999999999999999999999999d", 42); // reject: overflows
}
