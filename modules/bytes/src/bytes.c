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

/*
 * 64-bit shifts by a variable count, for n < 64. A 32-bit core has no such
 * instruction, and the compiler would call __aeabi_llsl or __aeabi_llsr, so
 * there they are built from the two 32-bit halves. A shift of a half by 32
 * is undefined, so the bits that cross from one half to the other are
 * shifted twice, by 1 and by (31 - n), which is correct for n = 0 as well.
 */
static uint64_t shl64(uint64_t value, unsigned n)
{
#if BYTES_WIDE
    return value << n;
#else
    uint32_t low = (uint32_t)value;
    uint32_t high = (uint32_t)(value >> 32);
    if (n >= 32) {
        high = low << (n - 32);
        low = 0;
    } else {
        high = high << n | (low >> 1) >> (31 - n);
        low <<= n;
    }
    return (uint64_t)high << 32 | low;
#endif
}

static uint64_t shr64(uint64_t value, unsigned n)
{
#if BYTES_WIDE
    return value >> n;
#else
    uint32_t low = (uint32_t)value;
    uint32_t high = (uint32_t)(value >> 32);
    if (n >= 32) {
        low = high >> (n - 32);
        high = 0;
    } else {
        low = low >> n | (high << 1) << (31 - n);
        high >>= n;
    }
    return (uint64_t)high << 32 | low;
#endif
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
    return value == 0 ? 0 : shl64(1, 63u - el_bytes_leading_zeros64(value));
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
    return width == 64 ? 0 : shl64(1, width);
}

uint32_t el_bytes_leading_ones32(uint32_t value)
{
    return clz32(~value);
}

uint32_t el_bytes_leading_ones64(uint64_t value)
{
    return el_bytes_leading_zeros64(~value);
}

uint32_t el_bytes_trailing_ones32(uint32_t value)
{
    return ctz32(~value);
}

uint32_t el_bytes_trailing_ones64(uint64_t value)
{
    return el_bytes_trailing_zeros64(~value);
}

uint32_t el_bytes_count_zeros32(uint32_t value)
{
    return 32u - popcount32(value);
}

uint32_t el_bytes_count_zeros64(uint64_t value)
{
    return 64u - el_bytes_count_ones64(value);
}

/* ---- Rotate and reverse ------------------------------------------------- */

/*
 * The count is reduced modulo the width first, and the opposite shift is
 * written as (-count & 31), which is 0, not 32, for a count of 0: no shift
 * is ever by the full width. GCC and Clang recognize the pattern and emit
 * one rotate instruction where the core has one (`rol` on x86-64, `ror` on
 * Arm). On a 32-bit core the 64-bit rotate is built from two 32-bit
 * rotates and a mask, so it needs no 64-bit shift helper.
 */

static uint32_t rotl32(uint32_t value, uint32_t count)
{
    count &= 31u;
    return value << count | value >> (-count & 31u);
}

static uint32_t rotr32(uint32_t value, uint32_t count)
{
    count &= 31u;
    return value >> count | value << (-count & 31u);
}

static uint64_t rotl64(uint64_t value, uint32_t count)
{
    count &= 63u;
#if BYTES_WIDE
    return value << count | value >> (-count & 63u);
#else
    uint32_t low = (uint32_t)value;
    uint32_t high = (uint32_t)(value >> 32);
    if (count >= 32) {
        /* A rotation by 32 swaps the halves. */
        const uint32_t swapped = low;
        low = high;
        high = swapped;
        count -= 32;
    }
    /* Rotate both halves by count < 32. The bits of each that stay in
     * their half are the ones above `mask`; the rest belong to the other. */
    const uint32_t mask = ((uint32_t)1 << count) - 1;
    const uint32_t low_rotated = rotl32(low, count);
    const uint32_t high_rotated = rotl32(high, count);
    return (uint64_t)((high_rotated & ~mask) | (low_rotated & mask)) << 32 |
           ((low_rotated & ~mask) | (high_rotated & mask));
#endif
}

static uint64_t rotr64(uint64_t value, uint32_t count)
{
#if BYTES_WIDE
    count &= 63u;
    return value >> count | value << (-count & 63u);
#else
    return rotl64(value, -count);
#endif
}

uint32_t el_bytes_rotate_left32(uint32_t value, uint32_t count)
{
    return rotl32(value, count);
}

uint64_t el_bytes_rotate_left64(uint64_t value, uint32_t count)
{
    return rotl64(value, count);
}

uint32_t el_bytes_rotate_right32(uint32_t value, uint32_t count)
{
    return rotr32(value, count);
}

uint64_t el_bytes_rotate_right64(uint64_t value, uint32_t count)
{
    return rotr64(value, count);
}

/* Swap the bits of each pair, then the pairs of each nibble, then the
 * nibbles of each byte, then the bytes. */
static uint32_t reverse32(uint32_t value)
{
    value = (value >> 1 & 0x55555555u) | (value & 0x55555555u) << 1;
    value = (value >> 2 & 0x33333333u) | (value & 0x33333333u) << 2;
    value = (value >> 4 & 0x0F0F0F0Fu) | (value & 0x0F0F0F0Fu) << 4;
    return swap32(value);
}

uint32_t el_bytes_reverse_bits32(uint32_t value)
{
    return reverse32(value);
}

uint64_t el_bytes_reverse_bits64(uint64_t value)
{
    return (uint64_t)reverse32((uint32_t)value) << 32 | reverse32((uint32_t)(value >> 32));
}

/* ---- Bit fields --------------------------------------------------------- */

/*
 * Every shift below is by less than the width of its type, whatever the
 * arguments: a mask of the full width is UINT_MAX >> 0, not (1 << width) - 1,
 * and a field of width 0 returns before it shifts at all, because its shift
 * may legitimately be the full width. The range check is written as
 * `shift <= bits - width`, which cannot overflow, where `shift + width`
 * could wrap.
 */

uint32_t el_bytes_mask32(uint32_t width)
{
    EL_ASSERT(width <= 32);
    return width == 0 ? 0 : UINT32_MAX >> (32 - width);
}

uint64_t el_bytes_mask64(uint32_t width)
{
    EL_ASSERT(width <= 64);
    return width == 0 ? 0 : shr64(UINT64_MAX, 64 - width);
}

uint32_t el_bytes_field_get32(uint32_t value, uint32_t shift, uint32_t width)
{
    EL_ASSERT(width <= 32 && shift <= 32 - width);
    if (width == 0) {
        return 0;
    }
    return value >> shift & el_bytes_mask32(width);
}

uint64_t el_bytes_field_get64(uint64_t value, uint32_t shift, uint32_t width)
{
    EL_ASSERT(width <= 64 && shift <= 64 - width);
    if (width == 0) {
        return 0;
    }
    return shr64(value, shift) & el_bytes_mask64(width);
}

uint32_t el_bytes_field_set32(uint32_t value, uint32_t shift, uint32_t width, uint32_t field)
{
    EL_ASSERT(width <= 32 && shift <= 32 - width);
    if (width == 0) {
        return value;
    }
    const uint32_t mask = el_bytes_mask32(width);
    return (value & ~(mask << shift)) | (field & mask) << shift;
}

uint64_t el_bytes_field_set64(uint64_t value, uint32_t shift, uint32_t width, uint64_t field)
{
    EL_ASSERT(width <= 64 && shift <= 64 - width);
    if (width == 0) {
        return value;
    }
    const uint64_t mask = el_bytes_mask64(width);
    return (value & ~shl64(mask, shift)) | shl64(field & mask, shift);
}

/* ---- Alignment ---------------------------------------------------------- */

uint32_t el_bytes_align_down32(uint32_t value, uint32_t align)
{
    EL_ASSERT(el_bytes_has_single_bit32(align));
    return value & ~(align - 1);
}

uint64_t el_bytes_align_down64(uint64_t value, uint64_t align)
{
    EL_ASSERT(el_bytes_has_single_bit64(align));
    return value & ~(align - 1);
}

uint32_t el_bytes_align_up32(uint32_t value, uint32_t align)
{
    EL_ASSERT(el_bytes_has_single_bit32(align));
    const uint32_t mask = align - 1;
    if ((value & mask) == 0) {
        return value;
    }
    /* value | mask is the last byte before the next multiple: adding 1 to
     * it wraps to 0 exactly when that multiple is 2^32. */
    return (value | mask) + 1;
}

uint64_t el_bytes_align_up64(uint64_t value, uint64_t align)
{
    EL_ASSERT(el_bytes_has_single_bit64(align));
    const uint64_t mask = align - 1;
    if ((value & mask) == 0) {
        return value;
    }
    return (value | mask) + 1;
}

bool el_bytes_is_aligned32(uint32_t value, uint32_t align)
{
    EL_ASSERT(el_bytes_has_single_bit32(align));
    return (value & (align - 1)) == 0;
}

bool el_bytes_is_aligned64(uint64_t value, uint64_t align)
{
    EL_ASSERT(el_bytes_has_single_bit64(align));
    return (value & (align - 1)) == 0;
}
