/*
 * elemental/fmt.h - integers to text, into a caller's scratch array.
 *
 * Three pure conversions: unsigned and signed decimal, and hex. Each writes
 * the digits of one value at the start of a small array the caller owns and
 * returns how many it wrote. They append nothing anywhere, pad nothing, and
 * write no terminator: the caller copies the digits where they belong, for
 * example with el_buf_append() (elemental/io_buf.h). A printf-style front
 * end that formats into an io buffer or a buffered writer is in
 * <elemental/fmt_printf.h>.
 *
 * STATUS: experimental. See docs/process/api-stability.md.
 *
 *     char digits[EL_FMT_INT_LEN_MAX];
 *     const size_t n = el_fmt_dec_i64(digits, sizeof digits, celsius);
 *     el_status st = el_buf_append(&line, "temp=", 5);
 *     if (st == EL_OK) {
 *         st = el_buf_append(&line, digits, n);
 *     }
 *
 * The output equals C's printf for "%" PRIu64, "%" PRId64, "%" PRIx64 and
 * "%" PRIX64: no sign but '-', no padding, no prefix ("0x" is the caller's).
 * The decimal conversion uses shifts and adds, so it needs no division
 * helper on a core without a divide instruction.
 *
 * These functions cannot fail: `cap` must be at least EL_FMT_INT_LEN_MAX,
 * which holds the longest output. `cap` is there so that a binding, or a
 * caller who got the size wrong, hits an assertion rather than an overrun.
 *
 * Concurrency: pure functions. No object, no lock, no allocation, no
 * configuration: safe from any context, an interrupt handler included, and
 * reentrant without conditions.
 *
 * Contract violations, checked by assertions: a null `out`; a `cap` below
 * EL_FMT_INT_LEN_MAX; flags other than EL_FMT_UPPER.
 */
#ifndef ELEMENTAL_FMT_H
#define ELEMENTAL_FMT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Bytes the longest conversion writes: "-9223372036854775808" and
 * "18446744073709551615" are 20. */
enum { EL_FMT_INT_LEN_MAX = 20 };

/* Flags for el_fmt_hex_u64(): hex digits A-F instead of a-f. */
enum { EL_FMT_UPPER = 1 };

/*
 * Write `value` at out[0..n) and return n, 1 to 20. `cap` is the size of
 * `out`, at least EL_FMT_INT_LEN_MAX.
 */
size_t el_fmt_dec_u64(char *out, size_t cap, uint64_t value);
size_t el_fmt_dec_i64(char *out, size_t cap, int64_t value);
size_t el_fmt_hex_u64(char *out, size_t cap, uint64_t value, uint32_t flags);

#ifdef __cplusplus
}
#endif

#endif /* ELEMENTAL_FMT_H */
