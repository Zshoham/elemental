/*
 * elemental/bytes.h - fixed-width integers in a stated byte order.
 *
 * Load and store 16-, 32-, and 64-bit unsigned values, little or big endian,
 * at any address. Use these instead of casting a byte pointer to a wider
 * integer pointer: that cast is undefined behavior, and it faults at an
 * unaligned address on cores such as ARMv6-M.
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
 * Contract violations: a null `src` or `dst`.
 */
#ifndef ELEMENTAL_BYTES_H
#define ELEMENTAL_BYTES_H

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

#ifdef __cplusplus
}
#endif

#endif /* ELEMENTAL_BYTES_H */
