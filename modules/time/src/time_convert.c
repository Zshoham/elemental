#include "elemental_config.h"

#include <elemental/assert.h>
#include <elemental/time.h>

#include <stddef.h>

/*
 * No `/`, `%` or `*` on a 64-bit operand in this file. On 32-bit Arm each
 * is a call into the compiler's runtime (__aeabi_uldivmod, __aeabi_lmul),
 * which Elemental's libraries do not rely on
 * (tests/zephyr/allowed-undefined.txt; docs/proposals/time.md, "Dependencies
 * and cost"). Shifts by a constant and additions are inline everywhere.
 */

/* n / d, and the remainder through `rest`, by shift and subtract. d >= 1. */
static uint64_t divide(uint64_t n, uint32_t d, uint32_t *rest)
{
    uint64_t quotient = 0;
    uint32_t r = 0;
    for (int i = 0; i < 64; i++) {
        /* r < d before the shift, so the bit shifted out of it, if any,
         * means the shifted value is at least 2^32 and so at least d. */
        const uint32_t carry = r >> 31;
        r = (r << 1) | (uint32_t)(n >> 63);
        n <<= 1;
        quotient <<= 1;
        if (carry != 0 || r >= d) {
            r -= d;
            quotient |= 1u;
        }
    }
    *rest = r;
    return quotient;
}

/* a * b in 64 bits, from 16-bit halves: each partial product fits 32 bits. */
static uint64_t multiply(uint32_t a, uint32_t b)
{
    const uint32_t a_low = a & 0xFFFFu;
    const uint32_t a_high = a >> 16;
    const uint32_t b_low = b & 0xFFFFu;
    const uint32_t b_high = b >> 16;
    uint64_t product = ((uint64_t)(a_high * b_high) << 32) | (a_low * b_low);
    product += (uint64_t)(a_high * b_low) << 16;
    product += (uint64_t)(a_low * b_high) << 16;
    return product;
}

el_ticks el_ticks_from_ms(uint32_t ms, uint32_t ticks_per_second)
{
    EL_ASSERT(ticks_per_second >= 1);
    uint32_t rest;
    /* The product is at most (2^32 - 1)^2, so adding 999 cannot overflow. */
    return divide(multiply(ms, ticks_per_second) + 999u, 1000u, &rest);
}

uint64_t el_ticks_to_seconds(el_ticks ticks, uint32_t ticks_per_second, uint32_t *us)
{
    EL_ASSERT(ticks_per_second >= 1);
    EL_ASSERT(us != NULL);
    uint32_t rest;
    const uint64_t seconds = divide(ticks, ticks_per_second, &rest);
    /* rest < ticks_per_second, so the quotient is below 1 000 000. */
    uint32_t unused;
    *us = (uint32_t)divide(multiply(rest, 1000000u), ticks_per_second, &unused);
    return seconds;
}

el_ticks el_clock_ticks_from_ms(el_clock clock, uint32_t ms)
{
    return el_ticks_from_ms(ms, el_clock_ticks_per_second(clock));
}

uint64_t el_clock_ticks_to_seconds(el_clock clock, el_ticks ticks, uint32_t *us)
{
    return el_ticks_to_seconds(ticks, el_clock_ticks_per_second(clock), us);
}
