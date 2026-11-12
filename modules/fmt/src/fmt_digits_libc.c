/*
 * Digits for FMT_LIBC: the C library's snprintf() converts each number into
 * a scratch buffer, so an image that already links printf does not carry a
 * second conversion. el_fmt_number_() (fmt.c) adds the sign and padding and
 * copies the result only if it fits whole, as it does for FMT_BUILTIN.
 *
 * Only the digits come from snprintf(): its width is an int and its output
 * would have to fit the scratch buffer, while an el_fmt width is a size_t.
 * The conversions used, %llu, %llx and %llX on an unsigned long long, are
 * the ones the PLATFORM_SNPRINTF probe guarantees.
 */
#include "elemental_config.h"

#include <stdio.h>

#include <elemental/assert.h>

#include "fmt_private.h"

const char *el_fmt_digits_(char *buf, uint64_t value, unsigned base, bool upper, size_t *count)
{
    const char *const format = base != 16 ? "%llu" : upper ? "%llX" : "%llx";
    const int written = snprintf(buf, EL_FMT_DIGITS_SIZE, format, (unsigned long long)value);
    /* At least one digit, and never more than UINT64_MAX has: a C library
     * that breaks this does not provide what the probe promised. */
    EL_ASSERT(written > 0 && written < EL_FMT_DIGITS_SIZE);
    *count = written > 0 && written < EL_FMT_DIGITS_SIZE ? (size_t)written : 0;
    return buf;
}
