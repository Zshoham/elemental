/*
 * elemental/bytes.h - fixed-width integers in a stated byte order, and their
 * bits.
 *
 * Load and store 16-, 32-, and 64-bit unsigned values, little or big endian,
 * at any address. Use these instead of casting a byte pointer to a wider
 * integer pointer: that cast is undefined behavior, and it faults at an
 * unaligned address on cores such as ARMv6-M.
 *
 * Also: byte swap; the bit counting of C23 <stdbit.h>, which C11 lacks;
 * rotate and bit reversal; bit fields; and power-of-two alignment. None of
 * these needs a compiler runtime helper on any target: a builtin is used
 * only where it becomes an instruction.
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
 * Contract violations: a null `src` or `dst`; a bit field that does not lie
 * within its type, and a mask wider than its type; an alignment that is not a
 * power of two. They are listed where each function is. The other functions
 * have none: every input, 0 included, has a defined result.
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

/* ---- Bit counting, after C23 <stdbit.h> --------------------------------- *
 *
 * Each count returns uint32_t (docs/conventions.md, ABI rule 2), where C23
 * returns unsigned int. There are no 8- and 16-bit forms and no first_*
 * functions (docs/proposals/bytes-crc.md, "Amendment (2026-11-13)").
 */

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

/* The number of 1 bits above the highest 0 bit. 32 (or 64) for all ones. */
uint32_t el_bytes_leading_ones32(uint32_t value);
uint32_t el_bytes_leading_ones64(uint64_t value);

/* The number of 1 bits below the lowest 0 bit. 32 (or 64) for all ones. */
uint32_t el_bytes_trailing_ones32(uint32_t value);
uint32_t el_bytes_trailing_ones64(uint64_t value);

/* The number of 0 bits. */
uint32_t el_bytes_count_zeros32(uint32_t value);
uint32_t el_bytes_count_zeros64(uint64_t value);

/* ---- Rotate and reverse ------------------------------------------------- */

/* Move every bit `count` places toward the high (left) or low (right) end;
 * the bits that leave one end enter at the other.
 *
 * Defined for every `count`: it is taken modulo the width, so 0 and the
 * width itself return `value`, and 33 rotates a 32-bit value by 1. */
uint32_t el_bytes_rotate_left32(uint32_t value, uint32_t count);
uint64_t el_bytes_rotate_left64(uint64_t value, uint32_t count);
uint32_t el_bytes_rotate_right32(uint32_t value, uint32_t count);
uint64_t el_bytes_rotate_right64(uint64_t value, uint32_t count);

/* The bits in reverse order: bit 0 becomes bit 31 (or 63), and so on. */
uint32_t el_bytes_reverse_bits32(uint32_t value);
uint64_t el_bytes_reverse_bits64(uint64_t value);

/* ---- Bit fields --------------------------------------------------------- *
 *
 * A field is `width` bits starting at bit `shift`, bit 0 being the least
 * significant. It must lie within the type: width <= 32 (or 64) and
 * shift + width <= 32 (or 64). A width of 0 is an empty field, valid at any
 * shift up to the type's width. These are for layouts that the code fixes,
 * so a field outside the type is a contract violation, checked without
 * overflowing (a huge `shift` is caught, not wrapped).
 *
 * No function here shifts by the full width of its type, which C leaves
 * undefined: the masks and fields that touch the top bit are as defined as
 * the others.
 */

/* The low `width` bits set: 0 for 0, all ones for 32 (or 64).
 * Contract violation: `width` above 32 (or 64). */
uint32_t el_bytes_mask32(uint32_t width);
uint64_t el_bytes_mask64(uint32_t width);

/* The field, moved down to bit 0: (value >> shift) & mask(width).
 * Contract violation: the field is not within the type. */
uint32_t el_bytes_field_get32(uint32_t value, uint32_t shift, uint32_t width);
uint64_t el_bytes_field_get64(uint64_t value, uint32_t shift, uint32_t width);

/* `value` with the field replaced by the low `width` bits of `field`; every
 * other bit of `value` is kept.
 *
 * Bits of `field` above `width` are ignored, as when a value is assigned to
 * a C bit-field. They are not a contract violation: `field` may be a
 * number read from input, and for it to be out of range is a data error,
 * which an assertion (compiled out in some configurations) is the wrong
 * tool for (docs/conventions.md, "Arguments"). The result is defined and
 * touches only the field. A caller that must reject such a value tests it
 * first: el_bytes_bit_width32(field) <= width.
 * Contract violation: the field is not within the type. */
uint32_t el_bytes_field_set32(uint32_t value, uint32_t shift, uint32_t width, uint32_t field);
uint64_t el_bytes_field_set64(uint64_t value, uint32_t shift, uint32_t width, uint64_t field);

/* ---- Alignment to a power of two ---------------------------------------- *
 *
 * `align` must be a power of two: 1, 2, 4, ... 2^31 (or 2^63). That is
 * a contract violation otherwise, 0 included.
 */

/* The largest multiple of `align` not greater than `value`. */
uint32_t el_bytes_align_down32(uint32_t value, uint32_t align);
uint64_t el_bytes_align_down64(uint64_t value, uint64_t align);

/* The smallest multiple of `align` not less than `value`.
 *
 * When that multiple does not fit the type, the result is 0, as for
 * el_bytes_bit_ceil: `value` may be a size read from input, so this is not
 * a contract violation. A result is never less than `value` except in that
 * case, so test `result < value`. (A `value` of 0 gives 0, which is not
 * that case.) */
uint32_t el_bytes_align_up32(uint32_t value, uint32_t align);
uint64_t el_bytes_align_up64(uint64_t value, uint64_t align);

/* True if `value` is a multiple of `align`. Always true for 0. */
bool el_bytes_is_aligned32(uint32_t value, uint32_t align);
bool el_bytes_is_aligned64(uint64_t value, uint64_t align);

#ifdef __cplusplus
}
#endif

#endif /* ELEMENTAL_BYTES_H */
