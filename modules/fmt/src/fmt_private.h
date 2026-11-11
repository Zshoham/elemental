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
el_status el_fmt_number_(el_fmt *f, uint64_t magnitude, bool negative, unsigned base,
                         bool upper, size_t width, bool zero_pad);

/* Length of a NUL-terminated string, reading at most `max` bytes. */
size_t el_fmt_strnlen_(const char *s, size_t max);

#endif /* ELEMENTAL_FMT_PRIVATE_H */
