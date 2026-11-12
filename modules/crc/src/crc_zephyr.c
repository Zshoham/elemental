#include "elemental_config.h"

#include <zephyr/sys/crc.h>

#include <elemental/assert.h>
#include <elemental/crc.h>

/*
 * Zephyr's own CRC functions (CRC_ZEPHYR). Both chain exactly as
 * el_crc*_update do. Source facts, Zephyr v4.5.0, subsys/crc:
 *
 *   crc32_ieee_update(crc, ...) inverts the register on entry and on exit,
 *   and crc32_ieee() starts it from 0, which is EL_CRC32_INIT.
 *
 *   crc16_itu_t(seed, ...) shifts MSB first through polynomial 0x1021 with
 *   no final XOR and returns the register, so seeded with 0xFFFF, which is
 *   EL_CRC16_IBM3740_INIT, it is CRC-16/IBM-3740, and a result passed back
 *   as the seed continues it.
 *
 * Both are the weak software versions only without CONFIG_CRC_HW_HANDLER,
 * which the CMake probe for this option requires.
 */

uint32_t el_crc32_update(uint32_t crc, const void *data, size_t len)
{
    EL_ASSERT(data != NULL || len == 0);
    return crc32_ieee_update(crc, data, len);
}

uint16_t el_crc16_ibm3740_update(uint16_t crc, const void *data, size_t len)
{
    EL_ASSERT(data != NULL || len == 0);
    return crc16_itu_t(crc, data, len);
}
