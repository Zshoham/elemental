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
 * The portable subset. For every format in it, the output is exactly what
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
 * The length modifiers are there so that <inttypes.h> works: PRIu64 is "llu"
 * on 32-bit Arm and "lu" on x86-64, and PRIu8 may be "hhu". %.*s prints a
 * string that comes with a length:
 *
 *     el_fmt_printf(&line, "%.*s", (int)len, text);
 *
 * Anything else, such as %f, %p, %n, the flags - + # and space, or a
 * precision on an integer, is a contract violation. With assertions compiled
 * out, formatting stops at that specification, reads no further arguments,
 * and the call returns EL_OK with what came before it.
 *
 * Checking: EL_FMT_PRINTF_CHECK asks GCC and Clang to check argument types
 * against the format (-Wformat). It checks types, not the subset: %f is
 * valid printf, so only the run-time check above catches it.
 *
 * Concurrency: as the target allows; nothing here adds state. Formatting
 * into a fixed el_buf is interrupt-safe (no lock, no allocation, nothing
 * blocks). Into a grown buffer it may allocate; into a buffered writer it
 * calls the inner writer.
 *
 * Contract violations, checked by assertions: a null target or format; a
 * null %s argument; a specification outside the subset; those of the target
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
el_status el_fmt_printf(el_buf *out, const char *format, ...) EL_FMT_PRINTF_CHECK(2, 3);

/* The same, for a caller that already has a va_list, such as a logging
 * function. As with vprintf(), `args` is indeterminate afterwards. */
el_status el_fmt_vprintf(el_buf *out, const char *format, va_list args) EL_FMT_PRINTF_CHECK(2, 0);

/* Format into a buffered writer, streaming through it. */
el_status el_fmt_printf_buffered(el_buffered_writer *out, const char *format, ...)
    EL_FMT_PRINTF_CHECK(2, 3);
el_status el_fmt_vprintf_buffered(el_buffered_writer *out, const char *format, va_list args)
    EL_FMT_PRINTF_CHECK(2, 0);

#ifdef __cplusplus
}
#endif

#endif /* ELEMENTAL_FMT_PRINTF_H */
