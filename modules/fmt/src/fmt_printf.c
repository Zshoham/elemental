#include "elemental_config.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <elemental/assert.h>
#include <elemental/fmt_printf.h>

#include "fmt_private.h"

enum length { LENGTH_NONE, LENGTH_HH, LENGTH_H, LENGTH_L, LENGTH_LL, LENGTH_Z };

static bool is_digit(char c)
{
    return c >= '0' && c <= '9';
}

/* A run of decimal digits in the format. Overflow is a contract violation:
 * the format is part of the program. */
static size_t parse_number(const char **p)
{
    size_t value = 0;
    while (is_digit(**p)) {
        EL_ASSERT(value <= (SIZE_MAX - 9) / 10);
        value = value * 10 + (size_t)(**p - '0');
        (*p)++;
    }
    return value;
}

static enum length parse_length(const char **p)
{
    switch (**p) {
    case 'h':
        (*p)++;
        if (**p == 'h') {
            (*p)++;
            return LENGTH_HH;
        }
        return LENGTH_H;
    case 'l':
        (*p)++;
        if (**p == 'l') {
            (*p)++;
            return LENGTH_LL;
        }
        return LENGTH_L;
    case 'z':
        (*p)++;
        return LENGTH_Z;
    default:
        return LENGTH_NONE;
    }
}

/* va_arg on a va_list passed by pointer, so the helpers below can share one
 * list (C11 7.16, footnote 253). */
static int64_t signed_argument(va_list *args, enum length length)
{
    switch (length) {
    case LENGTH_HH:
        return (signed char)va_arg(*args, int);
    case LENGTH_H:
        return (short)va_arg(*args, int);
    case LENGTH_L:
        return va_arg(*args, long);
    case LENGTH_LL:
        return va_arg(*args, long long);
    case LENGTH_Z:
        return va_arg(*args, ptrdiff_t);
    case LENGTH_NONE:
    default:
        return va_arg(*args, int);
    }
}

static uint64_t unsigned_argument(va_list *args, enum length length)
{
    switch (length) {
    case LENGTH_HH:
        return (unsigned char)va_arg(*args, unsigned int);
    case LENGTH_H:
        return (unsigned short)va_arg(*args, unsigned int);
    case LENGTH_L:
        return va_arg(*args, unsigned long);
    case LENGTH_LL:
        return va_arg(*args, unsigned long long);
    case LENGTH_Z:
        return va_arg(*args, size_t);
    case LENGTH_NONE:
    default:
        return va_arg(*args, unsigned int);
    }
}

static el_status format_with(el_fmt *f, const char *format, va_list *args)
{
    const char *p = format;

    while (*p != '\0') {
        if (*p != '%') {
            const char *const start = p;
            while (*p != '\0' && *p != '%') {
                p++;
            }
            (void)el_fmt_text_(f, 0, start, (size_t)(p - start));
            continue;
        }
        p++;
        if (*p == '%') {
            (void)el_fmt_text_(f, 0, "%", 1);
            p++;
            continue;
        }

        bool zero_pad = false;
        while (*p == '0') {
            zero_pad = true;
            p++;
        }
        const size_t width = parse_number(&p);

        bool has_precision = false;
        size_t precision = 0;
        if (*p == '.') {
            p++;
            has_precision = true;
            if (*p == '*') {
                p++;
                const int given = va_arg(*args, int);
                /* A negative precision is taken as if it were omitted. */
                has_precision = given >= 0;
                precision = given >= 0 ? (size_t)given : 0;
            } else {
                precision = parse_number(&p);
            }
        }
        const enum length length = parse_length(&p);
        const char conversion = *p;
        if (conversion != '\0') {
            p++;
        }

        switch (conversion) {
        case 'd':
        case 'i': {
            EL_ASSERT(!has_precision);
            if (has_precision) {
                return f->status;
            }
            const int64_t value = signed_argument(args, length);
            const uint64_t magnitude = value < 0 ? 0u - (uint64_t)value : (uint64_t)value;
            (void)el_fmt_number_(f, magnitude, value < 0, 10, false, width, zero_pad);
            break;
        }
        case 'u':
        case 'x':
        case 'X': {
            EL_ASSERT(!has_precision);
            if (has_precision) {
                return f->status;
            }
            const uint64_t value = unsigned_argument(args, length);
            (void)el_fmt_number_(f, value, false, conversion == 'u' ? 10 : 16,
                                 conversion == 'X', width, zero_pad);
            break;
        }
        case 'c': {
            EL_ASSERT(!zero_pad && !has_precision && length == LENGTH_NONE);
            if (zero_pad || has_precision || length != LENGTH_NONE) {
                return f->status;
            }
            const char c = (char)(unsigned char)va_arg(*args, int);
            (void)el_fmt_text_(f, width > 1 ? width - 1 : 0, &c, 1);
            break;
        }
        case 's': {
            EL_ASSERT(!zero_pad && length == LENGTH_NONE);
            if (zero_pad || length != LENGTH_NONE) {
                return f->status;
            }
            const char *const s = va_arg(*args, const char *);
            EL_ASSERT(s != NULL);
            if (s == NULL) {
                return f->status;
            }
            const size_t len = el_fmt_strnlen_(s, has_precision ? precision : SIZE_MAX);
            (void)el_fmt_text_(f, width > len ? width - len : 0, s, len);
            break;
        }
        default:
            /* Outside the subset, or the format ended inside a conversion.
             * Stop: reading on could take an argument as the wrong type. */
            EL_ASSERT(false);
            return f->status;
        }
    }
    return f->status;
}

el_status el_fmt_vprintf(el_fmt *f, const char *format, va_list args)
{
    EL_ASSERT(f != NULL);
    EL_ASSERT(format != NULL);
    va_list copy;
    va_copy(copy, args);
    const el_status status = format_with(f, format, &copy);
    va_end(copy);
    return status;
}

el_status el_fmt_printf(el_fmt *f, const char *format, ...)
{
    EL_ASSERT(f != NULL);
    EL_ASSERT(format != NULL);
    va_list args;
    va_start(args, format);
    const el_status status = format_with(f, format, &args);
    va_end(args);
    return status;
}
