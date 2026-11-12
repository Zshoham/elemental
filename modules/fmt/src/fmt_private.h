/*
 * Shared by fmt.c and fmt_printf.c; not installed. The callers have already
 * checked their arguments, so these functions check nothing.
 */
#ifndef ELEMENTAL_FMT_PRIVATE_H
#define ELEMENTAL_FMT_PRIVATE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <elemental/fmt.h>

/* One text item: `pad` spaces, then `len` bytes of `s`, cut at the last
 * byte that fits. */
el_status el_fmt_text_(el_fmt *f, size_t pad, const char *s, size_t len);

/* One number item, whole or not at all. `base` is 10 or 16. */
el_status el_fmt_number_(el_fmt *f, uint64_t magnitude, bool negative, unsigned base, bool upper,
                         size_t width, bool zero_pad);

/* Bytes el_fmt_digits_() may use: UINT64_MAX has 20 decimal digits, and
 * snprintf() writes a terminator after them. */
#define EL_FMT_DIGITS_SIZE 21

/*
 * The digits of `value` in `base` (10 or 16), with no sign, padding, or
 * prefix, written somewhere in `buf`, which has EL_FMT_DIGITS_SIZE bytes.
 * Returns the first digit and sets `*count`. One source per configuration:
 * fmt_digits_builtin.c (FMT_BUILTIN) or fmt_digits_libc.c (FMT_LIBC).
 */
const char *el_fmt_digits_(char *buf, uint64_t value, unsigned base, bool upper, size_t *count);

/* Length of a NUL-terminated string, reading at most `max` bytes. */
size_t el_fmt_strnlen_(const char *s, size_t max);

#endif /* ELEMENTAL_FMT_PRIVATE_H */
