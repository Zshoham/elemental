/*
 * Digits for FMT_BUILTIN: no runtime dependency at all.
 */
#include "elemental_config.h"

#include "fmt_private.h"

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

const char *el_fmt_digits_(char *buf, uint64_t value, unsigned base, bool upper, size_t *count)
{
    char *const end = buf + EL_FMT_DIGITS_SIZE;
    const char *const first = base == 16 ? hexadecimal(end, value, upper) : decimal(end, value);
    *count = (size_t)(end - first);
    return first;
}
