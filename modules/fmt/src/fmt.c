#include "elemental_config.h"

#include <elemental/assert.h>
#include <elemental/fmt.h>

#include "fmt_private.h"

/*
 * memcpy() is this file's only runtime dependency. GCC and Clang require
 * every environment to provide it, freestanding ones included. (FMT_LIBC
 * adds snprintf(), in fmt_digits_libc.c.)
 */
#if __STDC_HOSTED__
#include <string.h>
#else
void *memcpy(void *dest, const void *src, size_t n);
#endif

/* ---- The cursor ---------------------------------------------------------- */

/* Bytes of text that still fit, keeping one for the terminator. */
static size_t room(const el_fmt *f)
{
    return f->cap == 0 ? 0 : f->cap - 1 - f->len;
}

static void terminate(el_fmt *f)
{
    if (f->cap > 0) {
        f->buf[f->len] = '\0';
    }
}

static el_status cut(el_fmt *f)
{
    f->status = EL_ERR_NO_SPACE;
    terminate(f);
    return EL_ERR_NO_SPACE;
}

static void fill(char *out, char c, size_t count)
{
    for (size_t i = 0; i < count; i++) {
        out[i] = c;
    }
}

void el_fmt_init(el_fmt *f, char *buf, size_t cap)
{
    EL_ASSERT(f != NULL);
    EL_ASSERT(buf != NULL || cap == 0);
    f->buf = buf;
    f->cap = cap;
    f->len = 0;
    f->status = EL_OK;
    terminate(f);
}

size_t el_fmt_len(const el_fmt *f)
{
    EL_ASSERT(f != NULL);
    return f->len;
}

/* ---- Items --------------------------------------------------------------- */

el_status el_fmt_text_(el_fmt *f, size_t pad, const char *s, size_t len)
{
    if (f->status != EL_OK) {
        return EL_ERR_NO_SPACE;
    }
    const size_t available = room(f);
    char *out = f->buf + f->len;

    if (pad > available) {
        fill(out, ' ', available);
        f->len += available;
        return cut(f);
    }
    fill(out, ' ', pad);
    f->len += pad;

    const size_t fits = available - pad;
    const size_t count = len < fits ? len : fits;
    if (count > 0) {
        memcpy(out + pad, s, count);
        f->len += count;
    }
    if (count < len) {
        return cut(f);
    }
    terminate(f);
    return EL_OK;
}

el_status el_fmt_number_(el_fmt *f, uint64_t magnitude, bool negative, unsigned base, bool upper,
                         size_t width, bool zero_pad)
{
    /* Before converting: with FMT_LIBC, a conversion is a snprintf() call. */
    if (f->status != EL_OK) {
        return EL_ERR_NO_SPACE;
    }

    char digits[EL_FMT_DIGITS_SIZE];
    size_t count;
    const char *const first = el_fmt_digits_(digits, magnitude, base, upper, &count);
    const size_t body = count + (negative ? 1 : 0);
    const size_t total = width > body ? width : body;

    if (total > room(f)) {
        return cut(f); /* whole or not at all */
    }

    char *out = f->buf + f->len;
    const size_t pad = total - body;
    if (!zero_pad) {
        fill(out, ' ', pad);
        out += pad;
    }
    if (negative) {
        *out++ = '-';
    }
    if (zero_pad) {
        fill(out, '0', pad);
        out += pad;
    }
    memcpy(out, first, count);
    f->len += total;
    terminate(f);
    return EL_OK;
}

size_t el_fmt_strnlen_(const char *s, size_t max)
{
    size_t n = 0;
    while (n < max && s[n] != '\0') {
        n++;
    }
    return n;
}

/* ---- The public functions ------------------------------------------------ */

el_status el_fmt_str(el_fmt *f, const char *s, size_t len)
{
    EL_ASSERT(f != NULL);
    EL_ASSERT(s != NULL || len == 0);
    return el_fmt_text_(f, 0, s, len);
}

el_status el_fmt_cstr(el_fmt *f, const char *s)
{
    EL_ASSERT(f != NULL);
    EL_ASSERT(s != NULL);
    return el_fmt_text_(f, 0, s, el_fmt_strnlen_(s, SIZE_MAX));
}

el_status el_fmt_dec_u64(el_fmt *f, uint64_t value, size_t width, uint32_t flags)
{
    EL_ASSERT(f != NULL);
    EL_ASSERT((flags & ~(uint32_t)EL_FMT_ZERO_PAD) == 0);
    return el_fmt_number_(f, value, false, 10, false, width, (flags & EL_FMT_ZERO_PAD) != 0);
}

el_status el_fmt_dec_i64(el_fmt *f, int64_t value, size_t width, uint32_t flags)
{
    EL_ASSERT(f != NULL);
    EL_ASSERT((flags & ~(uint32_t)EL_FMT_ZERO_PAD) == 0);
    /* Negate in unsigned arithmetic, which INT64_MIN survives. */
    const uint64_t magnitude = value < 0 ? 0u - (uint64_t)value : (uint64_t)value;
    return el_fmt_number_(f, magnitude, value < 0, 10, false, width,
                          (flags & EL_FMT_ZERO_PAD) != 0);
}

el_status el_fmt_hex_u64(el_fmt *f, uint64_t value, size_t width, uint32_t flags)
{
    EL_ASSERT(f != NULL);
    EL_ASSERT((flags & ~(uint32_t)(EL_FMT_ZERO_PAD | EL_FMT_UPPER)) == 0);
    return el_fmt_number_(f, value, false, 16, (flags & EL_FMT_UPPER) != 0, width,
                          (flags & EL_FMT_ZERO_PAD) != 0);
}

el_status el_fmt_hex_bytes(el_fmt *f, const void *data, size_t len)
{
    EL_ASSERT(f != NULL);
    EL_ASSERT(data != NULL || len == 0);
    static const char digits[] = "0123456789abcdef";
    const uint8_t *const p = data;

    if (f->status != EL_OK) {
        return EL_ERR_NO_SPACE;
    }
    for (size_t i = 0; i < len; i++) {
        const size_t need = i == 0 ? 2 : 3; /* a space before every byte but the first */
        if (need > room(f)) {
            return cut(f);
        }
        char *out = f->buf + f->len;
        if (i > 0) {
            *out++ = ' ';
        }
        out[0] = digits[p[i] >> 4];
        out[1] = digits[p[i] & 0xFu];
        f->len += need;
    }
    terminate(f);
    return EL_OK;
}
