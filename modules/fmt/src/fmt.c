#include "elemental_config.h"

#include <elemental/assert.h>
#include <elemental/fmt.h>

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

/* Digits are produced last first, into the end of a scratch array, then
 * copied to the front of `out`. */
static size_t copy_out(char *out, const char *first, const char *end)
{
    const size_t n = (size_t)(end - first);
    for (size_t i = 0; i < n; i++) {
        out[i] = first[i];
    }
    return n;
}

static size_t decimal(char *out, uint64_t value)
{
    char scratch[EL_FMT_INT_LEN_MAX];
    char *const end = scratch + sizeof(scratch);
    char *first = end;
    uint32_t r;
    /* The 64-bit steps cost several times the 32-bit ones on a 32-bit core,
     * so they are used only while the value needs them. */
    while (value > UINT32_MAX) {
        value = div10_u64(value, &r);
        *--first = (char)('0' + r);
    }
    uint32_t small = (uint32_t)value;
    do {
        small = div10_u32(small, &r);
        *--first = (char)('0' + r);
    } while (small != 0);
    return copy_out(out, first, end);
}

size_t el_fmt_dec_u64(char *out, size_t cap, uint64_t value)
{
    EL_ASSERT(out != NULL);
    EL_ASSERT(cap >= EL_FMT_INT_LEN_MAX);
    return decimal(out, value);
}

size_t el_fmt_dec_i64(char *out, size_t cap, int64_t value)
{
    EL_ASSERT(out != NULL);
    EL_ASSERT(cap >= EL_FMT_INT_LEN_MAX);
    if (value >= 0) {
        return decimal(out, (uint64_t)value);
    }
    /* The magnitude of INT64_MIN does not fit an int64_t; it does fit a
     * uint64_t, where the negation is defined. */
    out[0] = '-';
    return 1 + decimal(out + 1, 0u - (uint64_t)value);
}

size_t el_fmt_hex_u64(char *out, size_t cap, uint64_t value, uint32_t flags)
{
    EL_ASSERT(out != NULL);
    EL_ASSERT(cap >= EL_FMT_INT_LEN_MAX);
    EL_ASSERT((flags & ~(uint32_t)EL_FMT_UPPER) == 0);

    const char *const digits =
        (flags & EL_FMT_UPPER) != 0 ? "0123456789ABCDEF" : "0123456789abcdef";
    char scratch[16];
    char *const end = scratch + sizeof(scratch);
    char *first = end;
    do {
        *--first = digits[value & 0xFu];
        value >>= 4;
    } while (value != 0);
    return copy_out(out, first, end);
}
