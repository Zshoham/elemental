/*
 * elemental/crc.h - cyclic redundancy checks.
 *
 * Two CRCs, each named exactly as in Greg Cook's catalogue of parametrised
 * CRC algorithms (https://reveng.sourceforge.io/crc-catalogue/), because
 * names such as "CRC-16/CCITT" are used for several different algorithms.
 *
 * STATUS: experimental. See docs/process/api-stability.md.
 *
 * How to use them: start from the INIT constant, which is the CRC of no
 * bytes, and pass each result back in to continue. Every return value is
 * already final; there is no separate finalize step and no state struct.
 *
 *     uint32_t crc = el_crc32_update(EL_CRC32_INIT, data, len);   // one shot
 *
 *     uint32_t crc = EL_CRC32_INIT;                               // in pieces
 *     crc = el_crc32_update(crc, part1, len1);
 *     crc = el_crc32_update(crc, part2, len2);
 *
 * Splitting the input differently never changes the result.
 *
 * Ownership: the data is borrowed for the call and nothing is kept.
 *
 * Concurrency: pure functions over read-only tables. Safe from any context,
 * interrupt handlers included, and concurrently.
 *
 * Contract violations: a null `data` with a nonzero `len`.
 *
 * Configuration: the choice CRC_IMPLEMENTATION picks what computes the CRCs:
 * 16-entry tables (CRC_TABLE_4BIT, the default), 256-entry tables
 * (CRC_TABLE_8BIT), GCC's CRC builtins (CRC_GCC_BUILTIN), or Zephyr's
 * <zephyr/sys/crc.h> (CRC_ZEPHYR). Results are identical with every option.
 */
#ifndef ELEMENTAL_CRC_H
#define ELEMENTAL_CRC_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    /* The CRC of no bytes. Start every computation from these. Note that
     * EL_CRC32_INIT is not the catalogue's register init (0xFFFFFFFF): it is
     * the finished CRC of an empty message. */
    EL_CRC32_INIT = 0,
    EL_CRC16_IBM3740_INIT = 0xFFFF
};

/*
 * CRC-32/ISO-HDLC, the catalogue's "CRC-32", used by PKZIP, PNG, and HDLC:
 * poly 0x04C11DB7, init 0xFFFFFFFF, reflected in and out, xorout 0xFFFFFFFF.
 * The CRC of the ASCII bytes "123456789" is 0xCBF43926.
 *
 * `crc` is the CRC of everything before `data`: EL_CRC32_INIT at the start,
 * then the previous return value. Returns the CRC of everything so far.
 * `data` may be null if `len` is 0. Cannot fail.
 *
 * The same convention as zlib's crc32().
 */
uint32_t el_crc32_update(uint32_t crc, const void *data, size_t len);

/*
 * CRC-16/IBM-3740, also called CRC-16/CCITT-FALSE: poly 0x1021, init 0xFFFF,
 * not reflected, xorout 0x0000. The CRC of "123456789" is 0x29B1.
 *
 * This is NOT the reflected CRC-16/KERMIT, which the catalogue calls
 * "CRC-16/CCITT", and not CRC-16/XMODEM, which starts from 0.
 *
 * Same convention as el_crc32_update(). A message followed by its own CRC,
 * stored big endian, has a CRC of 0, so a receiver can run every byte it
 * gets through this function and compare the result with 0.
 */
uint16_t el_crc16_ibm3740_update(uint16_t crc, const void *data, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* ELEMENTAL_CRC_H */
