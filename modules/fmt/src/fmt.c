#include "elemental_config.h"

#include <elemental/assert.h>
#include <elemental/fmt.h>

#include "fmt_private.h"

/*
 * memcpy() is this module's only runtime dependency. GCC and Clang require
 * every environment to provide it, freestanding ones included.
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

/*
 * Division by 10 with shifts and adds only (Hacker's Delight, 2nd ed.,
 * 10-17). A `/` or `%` here would need __aeabi_uldivmod on any 32-bit Arm
 * core, and even the 32-bit form needs __aeabi_uidiv on ARMv6-M, which has
 * no divide instruction. The quotient estimate is low by at most one, which
 * the remainder check corrects.
 */
static uint32_t div10_u32(uint32_t n, uint32_t *remainder)
{
    uint32_t q = (n >> 1) + (n >> 2);
    q += q >> 4;
    q += q >> 8;
    q += q >> 16;
    q >>= 3;
    uint32_t r = n - ((q << 3) + (q << 1));
    if (r > 9) {
        q += 1;
        r -= 10;
    }
    *remainder = r;
    return q;
}

static uint64_t div10_u64(uint64_t n, uint32_t *remainder)
{
    uint64_t q = (n >> 1) + (n >> 2);
    q += q >> 4;
    q += q >> 8;
    q += q >> 16;
    q += q >> 32;
    q >>= 3;
    uint64_t r = n - ((q << 3) + (q << 1));
    if (r > 9) {
        q += 1;
        r -= 10;
    }
    *remainder = (uint32_t)r;
    return q;
}

/* Writes the digits so that they end just before `end`; returns the first. */
static char *decimal(char *end, uint64_t value)
{
    uint32_t r;
    /* The 64-bit steps cost several times the 32-bit ones on a 32-bit core,
     * so they are used only while the value needs them. */
    while (value > UINT32_MAX) {
        value = div10_u64(value, &r);
        *--end = (char)('0' + r);
    }
    uint32_t small = (uint32_t)value;
    do {
        small = div10_u32(small, &r);
        *--end = (char)('0' + r);
    } while (small != 0);
    return end;
}

static char *hexadecimal(char *end, uint64_t value, bool upper)
{
    const char *const digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    do {
        *--end = digits[value & 0xFu];
        value >>= 4;
    } while (value != 0);
    return end;
}

el_status el_fmt_number_(el_fmt *f, uint64_t magnitude, bool negative, unsigned base,
                         bool upper, size_t width, bool zero_pad)
{
    char digits[20]; /* UINT64_MAX has 20 decimal digits */
    char *const end = digits + sizeof digits;
    const char *const first = base == 16 ? hexadecimal(end, magnitude, upper)
                                         : decimal(end, magnitude);
    const size_t count = (size_t)(end - first);
    const size_t body = count + (negative ? 1 : 0);
    const size_t total = width > body ? width : body;

    if (f->status != EL_OK) {
        return EL_ERR_NO_SPACE;
    }
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
