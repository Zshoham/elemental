#include "elemental_config.h"

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#include <elemental/assert.h>
#include <elemental/fmt.h>
#include <elemental/fmt_printf.h>

#include "fmt_private.h"

/*
 * This file is compiled without jump tables (CMakeLists.txt): on ARMv6-M a
 * jump table calls a libgcc helper. Writing the dispatch as if chains was
 * not enough, because GCC turns a chain back into a table.
 */

typedef el_fmt_spec_ spec;

static int is_digit(char c)
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

static enum el_fmt_length_ parse_length(const char **p)
{
    const char c = **p;
    if (c == 'h' || c == 'l') {
        (*p)++;
        if (**p == c) {
            (*p)++;
            return c == 'h' ? EL_FMT_LENGTH_HH_ : EL_FMT_LENGTH_LL_;
        }
        return c == 'h' ? EL_FMT_LENGTH_H_ : EL_FMT_LENGTH_L_;
    }
    const enum el_fmt_length_ length = c == 'z'   ? EL_FMT_LENGTH_Z_
                                       : c == 'j' ? EL_FMT_LENGTH_J_
                                       : c == 't' ? EL_FMT_LENGTH_T_
                                       : c == 'L' ? EL_FMT_LENGTH_BIG_L_
                                                  : EL_FMT_LENGTH_NONE_;
    if (length != EL_FMT_LENGTH_NONE_) {
        (*p)++;
    }
    return length;
}

/* Parses one specification after its '%', up to and including its
 * conversion character ('\0' if the format ended). Reads no argument. */
static void parse_spec(const char **p, spec *s)
{
    s->flags = 0;
    for (;;) {
        const char c = **p;
        const unsigned flag = c == '0'   ? EL_FMT_FLAG_ZERO_
                              : c == '-' ? EL_FMT_FLAG_MINUS_
                              : c == '+' ? EL_FMT_FLAG_PLUS_
                              : c == ' ' ? EL_FMT_FLAG_SPACE_
                              : c == '#' ? EL_FMT_FLAG_HASH_
                                         : 0u;
        if (flag == 0) {
            break;
        }
        s->flags |= flag;
        (*p)++;
    }
    s->width_star = **p == '*';
    if (s->width_star) {
        (*p)++;
    }
    s->width = parse_number(p);
    s->has_precision = **p == '.';
    s->precision_star = 0;
    s->precision = 0;
    if (s->has_precision) {
        (*p)++;
        s->precision_star = **p == '*';
        if (s->precision_star) {
            (*p)++;
        } else {
            s->precision = parse_number(p);
        }
    }
    s->length = parse_length(p);
    s->conversion = **p;
    if (s->conversion != '\0') {
        (*p)++;
    }
}

static int is_integer(char c)
{
    return c == 'd' || c == 'i' || c == 'u' || c == 'x' || c == 'X';
}

/* The portable subset, decided by the whole specification. */
static int is_portable(const spec *s)
{
    if (s->width_star || s->length > EL_FMT_LENGTH_Z_) {
        return 0;
    }
    if (is_integer(s->conversion)) {
        return (s->flags & ~(unsigned)EL_FMT_FLAG_ZERO_) == 0 && !s->has_precision;
    }
    if (s->flags != 0 || s->length != EL_FMT_LENGTH_NONE_) {
        return 0;
    }
    if (s->conversion == 'c') {
        return !s->has_precision;
    }
    if (s->conversion == 's') {
        return 1;
    }
    return s->conversion == '%' && s->width == 0 && !s->has_precision;
}

/* va_arg on a va_list passed by pointer, so the helpers can share one list
 * (C11 7.16, footnote 253). */
static int64_t signed_argument(va_list *args, enum el_fmt_length_ length)
{
    if (length == EL_FMT_LENGTH_HH_) {
        return (signed char)va_arg(*args, int);
    }
    if (length == EL_FMT_LENGTH_H_) {
        return (short)va_arg(*args, int);
    }
    if (length == EL_FMT_LENGTH_L_) {
        return va_arg(*args, long);
    }
    if (length == EL_FMT_LENGTH_LL_) {
        return va_arg(*args, long long);
    }
    if (length == EL_FMT_LENGTH_Z_) {
        return va_arg(*args, ptrdiff_t);
    }
    return va_arg(*args, int);
}

static uint64_t unsigned_argument(va_list *args, enum el_fmt_length_ length)
{
    if (length == EL_FMT_LENGTH_HH_) {
        return (unsigned char)va_arg(*args, unsigned int);
    }
    if (length == EL_FMT_LENGTH_H_) {
        return (unsigned short)va_arg(*args, unsigned int);
    }
    if (length == EL_FMT_LENGTH_L_) {
        return va_arg(*args, unsigned long);
    }
    if (length == EL_FMT_LENGTH_LL_) {
        return va_arg(*args, unsigned long long);
    }
    if (length == EL_FMT_LENGTH_Z_) {
        return va_arg(*args, size_t);
    }
    return va_arg(*args, unsigned int);
}

/* ---- Writing items --------------------------------------------------------- */

el_status el_fmt_need_(const el_fmt_target_ *t, size_t min)
{
    if (el_buf_room(t->buf) >= min) {
        return EL_OK;
    }
    return t->reserve(t->ctx, min);
}

/*
 * Room is asked for all of the run (a grown buffer grows), then for one byte
 * at a time when there is none (a buffered writer passes bytes on); what
 * fits is written. Stops at the first refusal.
 */
el_status el_fmt_put_run_(const el_fmt_target_ *t, const char *s, size_t len)
{
    while (len > 0) {
        if (el_buf_room(t->buf) < len) {
            const el_status status = t->reserve(t->ctx, len);
            if (status != EL_OK && el_buf_room(t->buf) == 0) {
                const el_status one = t->reserve(t->ctx, 1);
                if (one != EL_OK) {
                    return one;
                }
            }
        }
        const size_t room = el_buf_room(t->buf);
        const size_t count = len < room ? len : room;
        uint8_t *const out = el_buf_spare(t->buf);
        for (size_t i = 0; i < count; i++) {
            out[i] = s != NULL ? (uint8_t)s[i] : (uint8_t)' ';
        }
        el_buf_commit(t->buf, count);
        if (s != NULL) {
            s += count;
        }
        len -= count;
    }
    return EL_OK;
}

/* One text item: `pad` spaces, then `len` bytes of `s`, cut at the last
 * byte that fits. */
static el_status put_text(const el_fmt_target_ *t, size_t pad, const char *s, size_t len)
{
    const el_status status = el_fmt_put_run_(t, NULL, pad);
    if (status != EL_OK) {
        return status;
    }
    return el_fmt_put_run_(t, s, len);
}

/* One number item, whole or absent: padding to `width`, the sign, zeros
 * with the 0 flag, then the digits. */
static el_status put_number(const el_fmt_target_ *t, const char *digits, size_t count, int negative,
                            size_t width, int zero_pad)
{
    const size_t body = count + (negative ? 1u : 0u);
    const size_t total = width > body ? width : body;
    const el_status status = el_fmt_need_(t, total);
    if (status != EL_OK) {
        return status;
    }
    uint8_t *out = el_buf_spare(t->buf);
    const size_t pad = total - body;
    for (size_t i = 0; !zero_pad && i < pad; i++) {
        *out++ = ' ';
    }
    if (negative) {
        *out++ = '-';
    }
    for (size_t i = 0; zero_pad && i < pad; i++) {
        *out++ = '0';
    }
    for (size_t i = 0; i < count; i++) {
        *out++ = (uint8_t)digits[i];
    }
    el_buf_commit(t->buf, total);
    return EL_OK;
}

/* One specification of the portable subset; its arguments are read. */
static el_status put_portable(const el_fmt_target_ *t, const spec *s, va_list *args)
{
    const char c = s->conversion;
    if (is_integer(c)) {
        char digits[EL_FMT_INT_LEN_MAX];
        size_t count;
        int negative = 0;
        if (c == 'd' || c == 'i') {
            const int64_t value = signed_argument(args, s->length);
            negative = value < 0;
            count = el_fmt_dec_u64(digits, sizeof(digits),
                                   negative ? 0u - (uint64_t)value : (uint64_t)value);
        } else {
            const uint64_t value = unsigned_argument(args, s->length);
            count = c == 'u' ? el_fmt_dec_u64(digits, sizeof(digits), value)
                             : el_fmt_hex_u64(digits, sizeof(digits), value,
                                              c == 'X' ? (uint32_t)EL_FMT_UPPER : 0u);
        }
        return put_number(t, digits, count, negative, s->width,
                          (s->flags & EL_FMT_FLAG_ZERO_) != 0);
    }
    if (c == 'c') {
        const char ch = (char)(unsigned char)va_arg(*args, int);
        return put_text(t, s->width > 1 ? s->width - 1 : 0, &ch, 1);
    }
    if (c == 's') {
        size_t max = s->has_precision ? s->precision : SIZE_MAX;
        if (s->precision_star) {
            const int given = va_arg(*args, int);
            /* A negative precision is taken as if it were omitted. */
            max = given >= 0 ? (size_t)given : SIZE_MAX;
        }
        const char *const str = va_arg(*args, const char *);
        EL_ASSERT(str != NULL);
        if (str == NULL) {
            return EL_OK; /* assertions off: stop, as for any violation */
        }
        size_t len = 0;
        while (len < max && str[len] != '\0') {
            len++;
        }
        return put_text(t, s->width > len ? s->width - len : 0, str, len);
    }
    return put_text(t, 0, "%", 1);
}

el_status el_fmt_format_(const el_fmt_target_ *t, const char *format, va_list *args)
{
    const char *p = format;

    while (*p != '\0') {
        if (*p != '%') {
            const char *const start = p;
            while (*p != '\0' && *p != '%') {
                p++;
            }
            const el_status status = put_text(t, 0, start, (size_t)(p - start));
            if (status != EL_OK) {
                return status;
            }
            continue;
        }
        p++;
        spec s;
        parse_spec(&p, &s);
        el_status status = EL_OK;
        if (is_portable(&s)) {
            status = put_portable(t, &s, args);
#if defined(EL_CONFIG_FMT_PRINTF_LIBC)
        } else if (el_fmt_extended_(t, &s, args, &status)) {
            /* FMT_PRINTF_LIBC accepts it, in fmt_printf_libc.c. */
#endif
        } else {
            /* Outside what this configuration accepts, or the format ended
             * inside a specification. Stop: reading on could take an
             * argument as the wrong type. */
            EL_ASSERT(0);
            return EL_OK;
        }
        if (status != EL_OK) {
            return status;
        }
    }
    return EL_OK;
}

/* ---- Into an el_buf -------------------------------------------------------- */

static el_status buf_reserve(void *ctx, size_t min)
{
    return el_buf_reserve(ctx, min);
}

el_status el_fmt_vprintf(el_buf *out, const char *format, va_list args)
{
    EL_ASSERT(out != NULL);
    EL_ASSERT(format != NULL);
    const el_fmt_target_ target = {.buf = out, .reserve = buf_reserve, .ctx = out};
    va_list copy;
    va_copy(copy, args);
    const el_status status = el_fmt_format_(&target, format, &copy);
    va_end(copy);
    return status;
}

el_status el_fmt_printf(el_buf *out, const char *format, ...)
{
    EL_ASSERT(out != NULL);
    EL_ASSERT(format != NULL);
    const el_fmt_target_ target = {.buf = out, .reserve = buf_reserve, .ctx = out};
    va_list args;
    va_start(args, format);
    const el_status status = el_fmt_format_(&target, format, &args);
    va_end(args);
    return status;
}
