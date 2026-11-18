/*
 * elemental/fmt_printf.h - printf into an io buffer or a buffered writer.
 *
 * A convenience for C and C++ callers, layered on the conversions in
 * <elemental/fmt.h> and the buffers in <elemental/io_buf.h>. Bindings to
 * other languages should call those instead: a variadic function cannot be
 * called portably through a C ABI (docs/conventions.md, "ABI", rule 5).
 *
 * STATUS: experimental. See docs/process/api-stability.md.
 *
 * Two targets, the two things that know their free space:
 *
 *   - an el_buf, fixed or grown (el_fmt_printf, el_fmt_vprintf);
 *   - an el_buffered_writer (el_fmt_printf_buffered, ..._vprintf_buffered),
 *     which streams: when the buffer cannot take the next item, buffered
 *     bytes are passed on to make room, as el_buffered_writer_reserve()
 *     does, so output longer than the buffer still goes out.
 *
 * A bare el_writer is not a target: it cannot say whether a number will
 * fit before the number is written.
 *
 * The item rule (docs/adr/0015-formatting-into-io-buffers.md). The output is
 * a prefix of what unlimited room would hold:
 *
 *   - An item is one conversion, or one run of literal text.
 *   - A number is whole or absent: its sign, padding and digits enter the
 *     target together or not at all. Through a buffered writer, once it has
 *     entered the buffer, passing on may split it across writes, but the
 *     stream receives it whole unless the stream itself fails.
 *   - Text is cut at the last byte that fits: literal text, and %s and %c
 *     with their padding. A cut may split a multi-byte UTF-8 sequence.
 *   - Within one call, nothing follows the first item that did not fit.
 *     Nothing carries over to the next call: a caller that composes several
 *     calls checks each status, or rolls back with el_buf_truncate().
 *   - No terminator is written; el_buf_len() is the length.
 *   - A fixed buffered writer of capacity 0 accepts no item.
 *
 * Statuses:
 *
 *   el_fmt_printf      EL_OK, or EL_ERR_NO_SPACE: the buffer holds what
 *                        fitted, by the item rule.
 *   ..._buffered       EL_OK: everything was accepted (some may still be
 *                        pending). Otherwise el_buffered_writer_reserve()'s
 *                        status for the item that did not fit:
 *                        EL_ERR_WOULD_BLOCK (the stream stalled; resume by
 *                        flushing), EL_ERR_NO_SPACE, or the stream's
 *                        EL_ERR_END_OF_STREAM, EL_ERR_IO or EL_ERR_NO_SPACE.
 *
 * What is accepted depends on the printf option, FMT_PRINTF_BUILTIN (the
 * default) or FMT_PRINTF_LIBC, chosen once per image. Signatures are the
 * same under both; the set of accepted formats is not
 * (docs/adr/0016-private-choices-that-widen.md). Code that must build
 * against any configuration, a library over fmt for example, uses only the
 * portable subset.
 *
 * The portable subset, accepted under both options, decided by the whole
 * specification. It is built-in code under both, never snprintf(), so its
 * output is identical. For every format in it, the output is exactly what
 * C's snprintf() writes given enough space:
 *
 *   Conversions   d i u x X c s %
 *   Flags         0             d i u x X only
 *   Width         decimal digits (no '*')
 *   Precision     .N and .*     %s only: at most N bytes of the string,
 *                               which then need not be NUL-terminated
 *   Length        hh h l ll z   d i u x X only; %zd reads a ptrdiff_t,
 *                               the signed type of size_t's width on
 *                               every target Elemental builds for
 *
 * A bare dot has precision zero. Write a literal percent as %%.
 *
 * The length modifiers are there so that <inttypes.h> works: PRIu64 is "llu"
 * on 32-bit Arm and "lu" on x86-64, and PRIu8 may be "hhu". %.*s prints a
 * string that comes with a length:
 *
 *     el_fmt_printf(&line, "%.*s", (int)len, text);
 *
 * FMT_PRINTF_BUILTIN accepts only that. Anything else, such as %f, %p, %n,
 * the flags - + # and space, a '*' width, or a precision on an integer, is a
 * contract violation.
 *
 * FMT_PRINTF_LIBC also accepts:
 *
 *   - %s and %c with the '-' flag or a '*' width: Elemental's text code,
 *     which never reaches the C library;
 *   - every other specification of d i u x X o f F e E g G a A p that C
 *     defines (flags - + # 0 and space, precision, '*', length j t L):
 *     formatted one conversion at a time by the C library's snprintf(),
 *     and entered whole or not at all. Floats and %p are whatever the C
 *     library writes. A conversion of 32 bytes or more needs one free byte
 *     more than its length, because snprintf() writes a terminator.
 *
 * %n, %lc, %ls, and anything C does not define are violations under both.
 * With assertions compiled out, formatting stops at a violation, reads no
 * further arguments, and the call returns EL_OK with what came before it.
 *
 * Checking: EL_FMT_PRINTF_CHECK asks GCC and Clang to check argument types
 * against the format (-Wformat). Use elemental_check_formats(target) in
 * CMake to check literal formats against the portable subset at build time.
 * It checks expanded macros and adjacent literals too. Pass LIBC when the
 * target requires FMT_PRINTF_LIBC. Formats computed at run time are checked
 * by the formatter. See docs/build.md, "Checking printf formats".
 *
 * Concurrency: as the target allows; nothing here adds state. Formatting
 * the portable subset into a fixed el_buf is interrupt-safe under both
 * options (no lock, no allocation, nothing blocks). Into a grown buffer it
 * may allocate; into a buffered writer it calls the inner writer. Under
 * FMT_PRINTF_LIBC a conversion outside the portable subset is exactly as
 * safe as the C library's snprintf(): glibc does not promise
 * async-signal safety; picolibc's string snprintf() takes no lock and does
 * not allocate.
 *
 * Contract violations, checked by assertions: a null target or format; a
 * null %s argument; a specification outside what the option accepts; those
 * of the target
 * (elemental/io_buf.h, elemental/io_buffered.h).
 */
#ifndef ELEMENTAL_FMT_PRINTF_H
#define ELEMENTAL_FMT_PRINTF_H

#include <stdarg.h>

#include <elemental/io_buf.h>
#include <elemental/io_buffered.h>

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

/* Append `format`, with its arguments, to `out`. */
EL_NODISCARD el_status el_fmt_printf(el_buf *out, const char *format, ...)
    EL_FMT_PRINTF_CHECK(2, 3);

/* The same, for a caller that already has a va_list, such as a logging
 * function. As with vprintf(), `args` is indeterminate afterwards. */
EL_NODISCARD el_status el_fmt_vprintf(el_buf *out, const char *format, va_list args)
    EL_FMT_PRINTF_CHECK(2, 0);

/* Format into a buffered writer, streaming through it. */
EL_NODISCARD el_status el_fmt_printf_buffered(el_buffered_writer *out, const char *format, ...)
    EL_FMT_PRINTF_CHECK(2, 3);
EL_NODISCARD el_status el_fmt_vprintf_buffered(el_buffered_writer *out, const char *format,
                                               va_list args) EL_FMT_PRINTF_CHECK(2, 0);

#ifdef __cplusplus
}
#endif

#endif /* ELEMENTAL_FMT_PRINTF_H */
