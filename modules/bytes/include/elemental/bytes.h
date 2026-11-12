/*
 * elemental/bytes.h - fixed-width integers in a stated byte order, and their
 * bits.
 *
 * Load and store 16-, 32-, and 64-bit unsigned values, little or big endian,
 * at any address. Use these instead of casting a byte pointer to a wider
 * integer pointer: that cast is undefined behavior, and it faults at an
 * unaligned address on cores such as ARMv6-M.
 *
 * Also: byte swap, and the bit counting of C23 <stdbit.h>, which C11 lacks.
 * None of these needs a compiler runtime helper on any target: a builtin is
 * used only where it becomes an instruction.
 *
 * STATUS: experimental. See docs/process/api-stability.md.
 *
 * These are real functions, not inline ones, so that other languages have
 * symbols to call. Link-time optimization inlines them where it matters.
 * Signed values: cast the result, or cast the value before a store.
 *
 * Ownership: the bytes are borrowed for the call and nothing is kept.
 *
 * Storage: none. A load reads exactly 2, 4, or 8 bytes at `src`; a store
 * writes exactly 2, 4, or 8 bytes at `dst` and touches nothing else. Neither
 * needs any alignment.
 *
 * Concurrency: pure functions. Safe from any context, interrupt handlers
 * included, and concurrently, as long as no one else writes the same bytes.
 *
 * Contract violations: a null `src` or `dst`. The functions that take no
 * pointer have none: every input, 0 included, has a defined result.
 */
#ifndef ELEMENTAL_BYTES_H
#define ELEMENTAL_BYTES_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Little endian: the least significant byte comes first -------------- */

uint16_t el_bytes_load_le16(const void *src);
uint32_t el_bytes_load_le32(const void *src);
uint64_t el_bytes_load_le64(const void *src);

void el_bytes_store_le16(void *dst, uint16_t value);
void el_bytes_store_le32(void *dst, uint32_t value);
void el_bytes_store_le64(void *dst, uint64_t value);

/* ---- Big endian (network order): the most significant byte comes first -- */

uint16_t el_bytes_load_be16(const void *src);
uint32_t el_bytes_load_be32(const void *src);
uint64_t el_bytes_load_be64(const void *src);

void el_bytes_store_be16(void *dst, uint16_t value);
void el_bytes_store_be32(void *dst, uint32_t value);
void el_bytes_store_be64(void *dst, uint64_t value);

/* ---- Byte swap: the same bytes in reverse order ------------------------- */

uint16_t el_bytes_swap16(uint16_t value);
uint32_t el_bytes_swap32(uint32_t value);
uint64_t el_bytes_swap64(uint64_t value);

/* ---- Bit counting, after C23 <stdbit.h> --------------------------------- */

/* The number of 0 bits above the highest 1 bit. 32 (or 64) for 0. */
uint32_t el_bytes_leading_zeros32(uint32_t value);
uint32_t el_bytes_leading_zeros64(uint64_t value);

/* The number of 0 bits below the lowest 1 bit. 32 (or 64) for 0. */
uint32_t el_bytes_trailing_zeros32(uint32_t value);
uint32_t el_bytes_trailing_zeros64(uint64_t value);

/* The number of 1 bits. */
uint32_t el_bytes_count_ones32(uint32_t value);
uint32_t el_bytes_count_ones64(uint64_t value);

/* True if `value` is a power of two: exactly one bit is 1. False for 0. */
bool el_bytes_has_single_bit32(uint32_t value);
bool el_bytes_has_single_bit64(uint64_t value);

/* The number of bits needed to represent `value`: 1 + the index of its
 * highest 1 bit. 0 for 0. */
uint32_t el_bytes_bit_width32(uint32_t value);
uint32_t el_bytes_bit_width64(uint64_t value);

/* The largest power of two not greater than `value`. 0 for 0. */
uint32_t el_bytes_bit_floor32(uint32_t value);
uint64_t el_bytes_bit_floor64(uint64_t value);

/* The smallest power of two not less than `value`. 1 for 0 and 1.
 *
 * When that power does not fit the type, for a `value` above 2^31 (or
 * 2^63), the result is 0, which is never a power of two: test for it. This
 * is not a contract violation, because `value` may come from input data
 * (docs/proposals/bytes-crc.md, "Amendment (2026-11-12)"). */
uint32_t el_bytes_bit_ceil32(uint32_t value);
uint64_t el_bytes_bit_ceil64(uint64_t value);

#ifdef __cplusplus
}
#endif

#endif /* ELEMENTAL_BYTES_H */
