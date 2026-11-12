#include "elemental_config.h"

#include <stddef.h>

#include <elemental/assert.h>
#include <elemental/bytes.h>

/*
 * Every access is written as byte accesses combined with shifts, one width
 * at a time, so that no width pays for a wider one. Compilers merge them
 * into one load or store, plus a byte swap where needed, on cores that allow
 * unaligned access. Elsewhere they stay byte accesses.
 */

static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static uint32_t be32(const uint8_t *p)
{
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | (uint32_t)p[3];
}

static void put_le32(uint8_t *p, uint32_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
    p[2] = (uint8_t)(value >> 16);
    p[3] = (uint8_t)(value >> 24);
}

static void put_be32(uint8_t *p, uint32_t value)
{
    p[0] = (uint8_t)(value >> 24);
    p[1] = (uint8_t)(value >> 16);
    p[2] = (uint8_t)(value >> 8);
    p[3] = (uint8_t)value;
}

/* ---- Little endian ------------------------------------------------------ */

uint16_t el_bytes_load_le16(const void *src)
{
    EL_ASSERT(src != NULL);
    const uint8_t *p = src;
    return (uint16_t)(p[0] | p[1] << 8);
}

uint32_t el_bytes_load_le32(const void *src)
{
    EL_ASSERT(src != NULL);
    return le32(src);
}

uint64_t el_bytes_load_le64(const void *src)
{
    EL_ASSERT(src != NULL);
    const uint8_t *p = src;
    return (uint64_t)le32(p) | (uint64_t)le32(p + 4) << 32;
}

void el_bytes_store_le16(void *dst, uint16_t value)
{
    EL_ASSERT(dst != NULL);
    uint8_t *p = dst;
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
}

void el_bytes_store_le32(void *dst, uint32_t value)
{
    EL_ASSERT(dst != NULL);
    put_le32(dst, value);
}

void el_bytes_store_le64(void *dst, uint64_t value)
{
    EL_ASSERT(dst != NULL);
    uint8_t *p = dst;
    put_le32(p, (uint32_t)value);
    put_le32(p + 4, (uint32_t)(value >> 32));
}

/* ---- Big endian --------------------------------------------------------- */

uint16_t el_bytes_load_be16(const void *src)
{
    EL_ASSERT(src != NULL);
    const uint8_t *p = src;
    return (uint16_t)(p[0] << 8 | p[1]);
}

uint32_t el_bytes_load_be32(const void *src)
{
    EL_ASSERT(src != NULL);
    return be32(src);
}

uint64_t el_bytes_load_be64(const void *src)
{
    EL_ASSERT(src != NULL);
    const uint8_t *p = src;
    return (uint64_t)be32(p) << 32 | (uint64_t)be32(p + 4);
}

void el_bytes_store_be16(void *dst, uint16_t value)
{
    EL_ASSERT(dst != NULL);
    uint8_t *p = dst;
    p[0] = (uint8_t)(value >> 8);
    p[1] = (uint8_t)value;
}

void el_bytes_store_be32(void *dst, uint32_t value)
{
    EL_ASSERT(dst != NULL);
    put_be32(dst, value);
}

void el_bytes_store_be64(void *dst, uint64_t value)
{
    EL_ASSERT(dst != NULL);
    uint8_t *p = dst;
    put_be32(p, (uint32_t)(value >> 32));
    put_be32(p + 4, (uint32_t)value);
}

/* ---- Byte swap ---------------------------------------------------------- */

/* Shifts and masks, which GCC and Clang turn into one `rev` on Arm and one
 * `bswap` on x86-64. No builtin, so no target can need a helper for it. */

static uint32_t swap32(uint32_t value)
{
    return value << 24 | (value << 8 & 0x00FF0000u) | (value >> 8 & 0x0000FF00u) | value >> 24;
}

uint16_t el_bytes_swap16(uint16_t value)
{
    return (uint16_t)(value << 8 | value >> 8);
}

uint32_t el_bytes_swap32(uint32_t value)
{
    return swap32(value);
}

uint64_t el_bytes_swap64(uint64_t value)
{
    return (uint64_t)swap32((uint32_t)value) << 32 | swap32((uint32_t)(value >> 32));
}

/* ---- Bit counting ------------------------------------------------------- */

/*
 * A compiler builtin is used only where it is known to become instructions.
 * Elsewhere it becomes a call into the compiler's runtime: on ARMv6-M, which
 * has no CLZ, __builtin_clz calls __clzsi2; on 32-bit Arm, which has no
 * population count instruction, __builtin_popcount calls __popcountsi2; and
 * on x86-64 without POPCNT it calls __popcountdi2. Elemental's libraries
 * leave no such symbol undefined (tests/zephyr/allowed-undefined.txt), so
 * those targets get portable C.
 *
 *   BYTES_CLZ    __builtin_clz and __builtin_ctz are instructions.
 *   BYTES_WIDE   so are the 64-bit forms: the registers are 64 bits wide.
 *   BYTES_POPCNT __builtin_popcount and __builtin_popcountll are too.
 *
 * __ARM_FEATURE_CLZ is the Arm C Language Extensions' name for "this core
 * has CLZ"; GCC and Clang define it for ARMv7-M and not for ARMv6-M. A
 * 64-bit function on a 32-bit core is built from 32-bit halves, so it needs
 * no 64-bit shift or multiply helper either.
 *
 * BYTES_TEST_PORTABLE is defined only by the bytes.portable test, so that
 * the portable code is also run on a host whose compiler has the builtins.
 */
#if defined(BYTES_TEST_PORTABLE)
#define BYTES_CLZ 0
#define BYTES_WIDE 0
#elif defined(__GNUC__) && (defined(__x86_64__) || defined(__aarch64__))
#define BYTES_CLZ 1
#define BYTES_WIDE 1
#elif defined(__GNUC__) && defined(__ARM_FEATURE_CLZ)
#define BYTES_CLZ 1
#define BYTES_WIDE 0
#else
#define BYTES_CLZ 0
#define BYTES_WIDE 0
#endif

#if !defined(BYTES_TEST_PORTABLE) && defined(__GNUC__) && \
    (defined(__POPCNT__) || defined(__aarch64__))
#define BYTES_POPCNT 1
#else
#define BYTES_POPCNT 0
#endif

#if BYTES_CLZ || BYTES_POPCNT
/* The builtins take unsigned int and unsigned long long. */
_Static_assert(sizeof(unsigned) == 4 && sizeof(unsigned long long) == 8,
               "bit-counting builtins need a 32-bit unsigned int");
#endif

static unsigned clz32(uint32_t x)
{
    if (x == 0) {
        return 32;
    }
#if BYTES_CLZ
    return (unsigned)__builtin_clz((unsigned)x);
#else
    /* Binary search: shift the highest 1 bit up to bit 31. */
    unsigned n = 0;
    if (x <= 0x0000FFFFu) {
        n += 16;
        x <<= 16;
    }
    if (x <= 0x00FFFFFFu) {
        n += 8;
        x <<= 8;
    }
    if (x <= 0x0FFFFFFFu) {
        n += 4;
        x <<= 4;
    }
    if (x <= 0x3FFFFFFFu) {
        n += 2;
        x <<= 2;
    }
    if (x <= 0x7FFFFFFFu) {
        n += 1;
    }
    return n;
#endif
}

static unsigned ctz32(uint32_t x)
{
    if (x == 0) {
        return 32;
    }
#if BYTES_CLZ
    return (unsigned)__builtin_ctz((unsigned)x);
#else
    /* Binary search: shift the lowest 1 bit down to bit 0. */
    unsigned n = 0;
    if ((x & 0x0000FFFFu) == 0) {
        n += 16;
        x >>= 16;
    }
    if ((x & 0x000000FFu) == 0) {
        n += 8;
        x >>= 8;
    }
    if ((x & 0x0000000Fu) == 0) {
        n += 4;
        x >>= 4;
    }
    if ((x & 0x00000003u) == 0) {
        n += 2;
        x >>= 2;
    }
    if ((x & 0x00000001u) == 0) {
        n += 1;
    }
    return n;
#endif
}

static unsigned popcount32(uint32_t x)
{
#if BYTES_POPCNT
    return (unsigned)__builtin_popcount((unsigned)x);
#else
    /* Count in parallel: pairs of bits, then nibbles, then bytes, and add
     * the four byte counts with one 32-bit multiply (MULS on ARMv6-M). */
    x = x - (x >> 1 & 0x55555555u);
    x = (x & 0x33333333u) + (x >> 2 & 0x33333333u);
    x = (x + (x >> 4)) & 0x0F0F0F0Fu;
    return (unsigned)((x * 0x01010101u) >> 24);
#endif
}

/* 2^n for n < 64, without a variable 64-bit shift. */
static uint64_t power_of_two64(unsigned n)
{
    if (n >= 32) {
        return (uint64_t)((uint32_t)1 << (n - 32)) << 32;
    }
    return (uint64_t)((uint32_t)1 << n);
}

uint32_t el_bytes_leading_zeros32(uint32_t value)
{
    return clz32(value);
}

uint32_t el_bytes_leading_zeros64(uint64_t value)
{
#if BYTES_WIDE
    return value == 0 ? 64u : (unsigned)__builtin_clzll(value);
#else
    const uint32_t high = (uint32_t)(value >> 32);
    return high != 0 ? clz32(high) : 32u + clz32((uint32_t)value);
#endif
}

uint32_t el_bytes_trailing_zeros32(uint32_t value)
{
    return ctz32(value);
}

uint32_t el_bytes_trailing_zeros64(uint64_t value)
{
#if BYTES_WIDE
    return value == 0 ? 64u : (unsigned)__builtin_ctzll(value);
#else
    const uint32_t low = (uint32_t)value;
    return low != 0 ? ctz32(low) : 32u + ctz32((uint32_t)(value >> 32));
#endif
}

uint32_t el_bytes_count_ones32(uint32_t value)
{
    return popcount32(value);
}

uint32_t el_bytes_count_ones64(uint64_t value)
{
#if BYTES_POPCNT
    return (unsigned)__builtin_popcountll(value);
#else
    return popcount32((uint32_t)value) + popcount32((uint32_t)(value >> 32));
#endif
}

bool el_bytes_has_single_bit32(uint32_t value)
{
    return value != 0 && (value & (value - 1)) == 0;
}

bool el_bytes_has_single_bit64(uint64_t value)
{
    return value != 0 && (value & (value - 1)) == 0;
}

uint32_t el_bytes_bit_width32(uint32_t value)
{
    return 32u - clz32(value);
}

uint32_t el_bytes_bit_width64(uint64_t value)
{
    return 64u - el_bytes_leading_zeros64(value);
}

uint32_t el_bytes_bit_floor32(uint32_t value)
{
    return value == 0 ? 0 : (uint32_t)1 << (31u - clz32(value));
}

uint64_t el_bytes_bit_floor64(uint64_t value)
{
    return value == 0 ? 0 : power_of_two64(63u - el_bytes_leading_zeros64(value));
}

uint32_t el_bytes_bit_ceil32(uint32_t value)
{
    if (value <= 1) {
        return 1;
    }
    const unsigned width = el_bytes_bit_width32(value - 1);
    /* 2^32 does not fit: 0, as documented. */
    return width == 32 ? 0 : (uint32_t)1 << width;
}

uint64_t el_bytes_bit_ceil64(uint64_t value)
{
    if (value <= 1) {
        return 1;
    }
    const unsigned width = el_bytes_bit_width64(value - 1);
    return width == 64 ? 0 : power_of_two64(width);
}
