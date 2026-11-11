/*
 * elemental/fmt_printf.h - a printf-style front end for el_fmt.
 *
 * A convenience for C and C++ callers, layered on the typed calls in
 * <elemental/fmt.h>. Bindings to other languages should call those instead:
 * a variadic function cannot be called portably through a C ABI
 * (docs/conventions.md, "ABI", rule 5).
 *
 * STATUS: experimental. See docs/process/api-stability.md.
 *
 * The subset. For every format it accepts, the output is exactly what C's
 * snprintf() writes given enough space:
 *
 *   Conversions   d i u x X c s %
 *   Flags         0             integers only
 *   Width         decimal digits (no '*')
 *   Precision     .N and .*     %s only: at most N bytes of the string,
 *                               which then need not be NUL-terminated
 *   Length        hh h l ll z   integers only; %zd reads a ptrdiff_t,
 *                               the signed type of size_t's width on
 *                               every target Elemental builds for
 *
 * The length modifiers are there so that <inttypes.h> works: PRIu64 is "llu"
 * on 32-bit Arm and "lu" on x86-64, and PRIu8 may be "hhu". %.*s prints a
 * string that comes with a length:
 *
 *     el_fmt_printf(&f, "%.*s", (int)len, text);
 *
 * Anything else, such as %f, %p, %n, the flags - + # and space, or a
 * precision on an integer, is a contract violation. With assertions compiled
 * out, formatting stops at that conversion, reads no further arguments, and
 * the call returns as if the format had ended there.
 *
 * Truncation follows <elemental/fmt.h>. Each conversion is one item, and so
 * is each run of literal text: a number with its padding is whole or
 * absent; %s, %c, and literal text are cut at the last byte that fits,
 * padding included. Statuses are the same.
 *
 * Checking: EL_FMT_PRINTF_CHECK asks GCC and Clang to check argument types
 * against the format (-Wformat). It checks types, not the subset: %f is
 * valid printf, so only the run-time check above catches it.
 *
 * Concurrency, ownership, and the other violations: as in <elemental/fmt.h>.
 * Also a violation: a null format, or a null %s argument.
 */
#ifndef ELEMENTAL_FMT_PRINTF_H
#define ELEMENTAL_FMT_PRINTF_H

#include <stdarg.h>

#include <elemental/fmt.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Marks a function whose argument `format_arg` is a printf format checked
 * against the arguments from `first_arg` on (0 for a va_list). For use on
 * other variadic functions that forward to el_fmt_vprintf().
 */
#if defined(__GNUC__)
#define EL_FMT_PRINTF_CHECK(format_arg, first_arg) \
    __attribute__((format(printf, format_arg, first_arg)))
#else
#define EL_FMT_PRINTF_CHECK(format_arg, first_arg)
#endif

/* Append `format`, with its arguments, to `f`. */
el_status el_fmt_printf(el_fmt *f, const char *format, ...) EL_FMT_PRINTF_CHECK(2, 3);

/* The same, for a caller that already has a va_list, such as a logging
 * function. As with vprintf(), `args` is indeterminate afterwards. */
el_status el_fmt_vprintf(el_fmt *f, const char *format, va_list args) EL_FMT_PRINTF_CHECK(2, 0);

#ifdef __cplusplus
}
#endif

#endif /* ELEMENTAL_FMT_PRINTF_H */
