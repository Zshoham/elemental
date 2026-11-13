/*
 * FMT_PRINTF_LIBC: the specifications outside the portable subset that this
 * option accepts.
 *
 *   - %s and %c with the '-' flag or a '*' width: Elemental's text code,
 *     below. They never reach the C library.
 *   - Every other specification of d i u x X o f F e E g G a A p: rebuilt
 *     with its '*' values written in, and handed to the C library's
 *     snprintf() with its one argument. The result enters the target whole
 *     or not at all.
 *
 * Handing the whole format to vsnprintf() would cut numbers and lose the
 * item boundary, so the parser in fmt_printf.c walks the format and reads
 * each argument with the type its specification names; only one conversion
 * at a time reaches snprintf().
 */
#include "elemental_config.h"

#include <limits.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include <elemental/assert.h>
#include <elemental/fmt.h>

#include "fmt_private.h"

/* What snprintf() writes into first. A result that fits is reserved and
 * copied, exactly; a longer one is formatted again in place. */
#define SCRATCH_SIZE 32

/* One argument, kept with the type it was read as, so that it is passed to
 * snprintf() as that type. */
enum kind {
    KIND_INT,
    KIND_LONG,
    KIND_LLONG,
    KIND_INTMAX,
    KIND_PTRDIFF,
    KIND_UINT,
    KIND_ULONG,
    KIND_ULLONG,
    KIND_UINTMAX,
    KIND_SIZE,
    KIND_DOUBLE,
    KIND_LDOUBLE,
    KIND_POINTER
};

typedef struct argument {
    enum kind kind;
    union {
        int i;
        long l;
        long long ll;
        intmax_t im;
        ptrdiff_t pd;
        unsigned int u;
        unsigned long ul;
        unsigned long long ull;
        uintmax_t um;
        size_t sz;
        double d;
        long double ld;
        void *p;
    } value;
} argument;

static int is_signed_conversion(char c)
{
    return c == 'd' || c == 'i';
}

static int is_unsigned_conversion(char c)
{
    return c == 'u' || c == 'o' || c == 'x' || c == 'X';
}

static int is_float_conversion(char c)
{
    return c == 'f' || c == 'F' || c == 'e' || c == 'E' || c == 'g' || c == 'G' || c == 'a' ||
           c == 'A';
}

/* The kind a numeric specification reads, or 0 if C gives it none. */
static int kind_of(const el_fmt_spec_ *s, enum kind *kind)
{
    const char c = s->conversion;
    const enum el_fmt_length_ length = s->length;
    if (is_signed_conversion(c)) {
        *kind = length == EL_FMT_LENGTH_L_    ? KIND_LONG
                : length == EL_FMT_LENGTH_LL_ ? KIND_LLONG
                : length == EL_FMT_LENGTH_J_  ? KIND_INTMAX
                : length == EL_FMT_LENGTH_Z_  ? KIND_PTRDIFF
                : length == EL_FMT_LENGTH_T_  ? KIND_PTRDIFF
                                              : KIND_INT;
        return length != EL_FMT_LENGTH_BIG_L_;
    }
    if (is_unsigned_conversion(c)) {
        *kind = length == EL_FMT_LENGTH_L_    ? KIND_ULONG
                : length == EL_FMT_LENGTH_LL_ ? KIND_ULLONG
                : length == EL_FMT_LENGTH_J_  ? KIND_UINTMAX
                : length == EL_FMT_LENGTH_Z_  ? KIND_SIZE
                : length == EL_FMT_LENGTH_T_  ? KIND_PTRDIFF
                                              : KIND_UINT;
        return length != EL_FMT_LENGTH_BIG_L_;
    }
    if (is_float_conversion(c)) {
        *kind = length == EL_FMT_LENGTH_BIG_L_ ? KIND_LDOUBLE : KIND_DOUBLE;
        return length == EL_FMT_LENGTH_NONE_ || length == EL_FMT_LENGTH_L_ ||
               length == EL_FMT_LENGTH_BIG_L_;
    }
    if (c == 'p') {
        *kind = KIND_POINTER;
        return length == EL_FMT_LENGTH_NONE_;
    }
    return 0;
}

static void read_argument(argument *a, va_list *args)
{
    const enum kind k = a->kind;
    /* hh and h arguments arrive promoted to int or unsigned int. */
    if (k == KIND_INT) {
        a->value.i = va_arg(*args, int);
    } else if (k == KIND_UINT) {
        a->value.u = va_arg(*args, unsigned int);
    } else if (k == KIND_LONG) {
        a->value.l = va_arg(*args, long);
    } else if (k == KIND_ULONG) {
        a->value.ul = va_arg(*args, unsigned long);
    } else if (k == KIND_LLONG) {
        a->value.ll = va_arg(*args, long long);
    } else if (k == KIND_ULLONG) {
        a->value.ull = va_arg(*args, unsigned long long);
    } else if (k == KIND_INTMAX) {
        a->value.im = va_arg(*args, intmax_t);
    } else if (k == KIND_UINTMAX) {
        a->value.um = va_arg(*args, uintmax_t);
    } else if (k == KIND_PTRDIFF) {
        a->value.pd = va_arg(*args, ptrdiff_t);
    } else if (k == KIND_SIZE) {
        a->value.sz = va_arg(*args, size_t);
    } else if (k == KIND_DOUBLE) {
        a->value.d = va_arg(*args, double);
    } else if (k == KIND_LDOUBLE) {
        a->value.ld = va_arg(*args, long double);
    } else {
        a->value.p = va_arg(*args, void *);
    }
}

/* The format is built at run time from a specification this file checked,
 * so the compiler cannot check it against the argument; kind_of() did. */
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
#endif
static int convert(char *out, size_t cap, const char *format, const argument *a)
{
    const enum kind k = a->kind;
    if (k == KIND_INT) {
        return snprintf(out, cap, format, a->value.i);
    }
    if (k == KIND_UINT) {
        return snprintf(out, cap, format, a->value.u);
    }
    if (k == KIND_LONG) {
        return snprintf(out, cap, format, a->value.l);
    }
    if (k == KIND_ULONG) {
        return snprintf(out, cap, format, a->value.ul);
    }
    if (k == KIND_LLONG) {
        return snprintf(out, cap, format, a->value.ll);
    }
    if (k == KIND_ULLONG) {
        return snprintf(out, cap, format, a->value.ull);
    }
    if (k == KIND_INTMAX) {
        return snprintf(out, cap, format, a->value.im);
    }
    if (k == KIND_UINTMAX) {
        return snprintf(out, cap, format, a->value.um);
    }
    if (k == KIND_PTRDIFF) {
        return snprintf(out, cap, format, a->value.pd);
    }
    if (k == KIND_SIZE) {
        return snprintf(out, cap, format, a->value.sz);
    }
    if (k == KIND_DOUBLE) {
        return snprintf(out, cap, format, a->value.d);
    }
    if (k == KIND_LDOUBLE) {
        return snprintf(out, cap, format, a->value.ld);
    }
    return snprintf(out, cap, format, a->value.p);
}
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

/* A '*' value: an int argument. */
static int star(va_list *args)
{
    return va_arg(*args, int);
}

/* Appends `value` in decimal at `p`. */
static char *put_decimal(char *p, size_t value)
{
    char digits[EL_FMT_INT_LEN_MAX];
    const size_t n = el_fmt_dec_u64(digits, sizeof digits, value);
    for (size_t i = 0; i < n; i++) {
        *p++ = digits[i];
    }
    return p;
}

/* Bytes the rebuilt specification may need: '%', five flags, a width and a
 * precision of up to 20 digits each, '.', two length characters, the
 * conversion and a terminator. */
#define FORMAT_SIZE 56

/* The specification again, with '*' values written in. */
static void rebuild(char *format, const el_fmt_spec_ *s, unsigned flags, size_t width,
                    int has_precision, size_t precision)
{
    char *p = format;
    *p++ = '%';
    if ((flags & EL_FMT_FLAG_MINUS_) != 0) {
        *p++ = '-';
    }
    if ((flags & EL_FMT_FLAG_PLUS_) != 0) {
        *p++ = '+';
    }
    if ((flags & EL_FMT_FLAG_SPACE_) != 0) {
        *p++ = ' ';
    }
    if ((flags & EL_FMT_FLAG_HASH_) != 0) {
        *p++ = '#';
    }
    if ((flags & EL_FMT_FLAG_ZERO_) != 0) {
        *p++ = '0';
    }
    if (width != 0) {
        p = put_decimal(p, width);
    }
    if (has_precision) {
        *p++ = '.';
        p = put_decimal(p, precision);
    }
    const enum el_fmt_length_ length = s->length;
    if (length == EL_FMT_LENGTH_HH_ || length == EL_FMT_LENGTH_H_) {
        *p++ = 'h';
    }
    if (length == EL_FMT_LENGTH_HH_) {
        *p++ = 'h';
    }
    if (length == EL_FMT_LENGTH_L_ || length == EL_FMT_LENGTH_LL_) {
        *p++ = 'l';
    }
    if (length == EL_FMT_LENGTH_LL_) {
        *p++ = 'l';
    }
    if (length == EL_FMT_LENGTH_Z_) {
        *p++ = 'z';
    }
    if (length == EL_FMT_LENGTH_J_) {
        *p++ = 'j';
    }
    if (length == EL_FMT_LENGTH_T_) {
        *p++ = 't';
    }
    if (length == EL_FMT_LENGTH_BIG_L_) {
        *p++ = 'L';
    }
    *p++ = s->conversion;
    *p = '\0';
}

/* The width and precision, '*' values read in order. A negative '*' width
 * is the '-' flag and its magnitude; a negative '*' precision is as if it
 * were omitted (C11 7.21.6.1p5). */
static void read_stars(const el_fmt_spec_ *s, va_list *args, unsigned *flags, size_t *width,
                       int *has_precision, size_t *precision)
{
    *flags = s->flags;
    *width = s->width;
    if (s->width_star) {
        const int given = star(args);
        if (given < 0) {
            *flags |= EL_FMT_FLAG_MINUS_;
            *width = given == INT_MIN ? (size_t)INT_MAX + 1 : (size_t)-given;
        } else {
            *width = (size_t)given;
        }
    }
    *has_precision = s->has_precision;
    *precision = s->precision;
    if (s->precision_star) {
        const int given = star(args);
        *has_precision = given >= 0;
        *precision = given >= 0 ? (size_t)given : 0;
    }
}

/* %s and %c with '-' or a '*' width: text, padded on the left or right. */
static int text(const el_fmt_target_ *t, const el_fmt_spec_ *s, va_list *args, el_status *status)
{
    const char c = s->conversion;
    if ((c != 's' && c != 'c') || (s->flags & ~(unsigned)EL_FMT_FLAG_MINUS_) != 0 ||
        s->length != EL_FMT_LENGTH_NONE_ || (c == 'c' && s->has_precision)) {
        return 0;
    }
    unsigned flags;
    size_t width;
    int has_precision;
    size_t precision;
    read_stars(s, args, &flags, &width, &has_precision, &precision);

    char ch;
    const char *str;
    size_t len = 0;
    if (c == 'c') {
        ch = (char)(unsigned char)va_arg(*args, int);
        str = &ch;
        len = 1;
    } else {
        str = va_arg(*args, const char *);
        EL_ASSERT(str != NULL);
        if (str == NULL) {
            *status = EL_OK; /* assertions off: stop, as for any violation */
            return 1;
        }
        const size_t max = has_precision ? precision : SIZE_MAX;
        while (len < max && str[len] != '\0') {
            len++;
        }
    }
    const size_t pad = width > len ? width - len : 0;
    const int left = (flags & EL_FMT_FLAG_MINUS_) != 0;
    *status = el_fmt_put_run_(t, NULL, left ? 0 : pad);
    if (*status == EL_OK) {
        *status = el_fmt_put_run_(t, str, len);
    }
    if (*status == EL_OK) {
        *status = el_fmt_put_run_(t, NULL, left ? pad : 0);
    }
    return 1;
}

int el_fmt_extended_(const el_fmt_target_ *t, const el_fmt_spec_ *s, va_list *args,
                     el_status *status)
{
    if (text(t, s, args, status)) {
        return 1;
    }
    argument a;
    if (!kind_of(s, &a.kind)) {
        return 0; /* %n, %lc, %ls, %%, anything else */
    }
    /* A width or precision from the format is part of the program: one
     * snprintf() cannot take more than INT_MAX. */
    EL_ASSERT(s->width <= INT_MAX && s->precision <= INT_MAX);

    unsigned flags;
    size_t width;
    int has_precision;
    size_t precision;
    read_stars(s, args, &flags, &width, &has_precision, &precision);
    read_argument(&a, args);

    char format[FORMAT_SIZE];
    rebuild(format, s, flags, width, has_precision, precision);

    char scratch[SCRATCH_SIZE];
    const int n = convert(scratch, sizeof scratch, format, &a);
    EL_ASSERT(n >= 0); /* a C library that cannot format what it accepts */
    if (n < 0) {
        *status = EL_OK;
        return 1;
    }
    const size_t len = (size_t)n;
    if (len < sizeof scratch) {
        *status = el_fmt_need_(t, len);
        if (*status == EL_OK) {
            uint8_t *const out = el_buf_spare(t->buf);
            for (size_t i = 0; i < len; i++) {
                out[i] = (uint8_t)scratch[i];
            }
            el_buf_commit(t->buf, len);
        }
        return 1;
    }
    /* Longer than the scratch: formatted again in place, which needs one
     * more byte for the terminator snprintf() writes. */
    *status = len < SIZE_MAX ? el_fmt_need_(t, len + 1) : EL_ERR_NO_SPACE;
    if (*status == EL_OK) {
        const int again = convert((char *)el_buf_spare(t->buf), len + 1, format, &a);
        EL_ASSERT(again == n);
        el_buf_commit(t->buf, len);
    }
    return 1;
}
