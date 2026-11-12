#include "elemental_config.h"

#include <elemental/assert.h>
#include <elemental/crc.h>

/*
 * GCC's CRC builtins (CRC_GCC_BUILTIN), available from GCC 15 and probed by
 * CMake. Each takes the register, one data byte, and the polynomial in its
 * normal (not reversed) form. The `rev_` form shifts LSB first, which is
 * CRC-32/ISO-HDLC's reflected order; the plain form shifts MSB first, which
 * is CRC-16/IBM-3740's. GCC lowers them to a CRC instruction where the
 * target has one, and otherwise to a table it generates.
 */

uint32_t el_crc32_update(uint32_t crc, const void *data, size_t len)
{
    EL_ASSERT(data != NULL || len == 0);
    const uint8_t *p = data;

    crc = ~crc;
    for (size_t i = 0; i < len; i++) {
        crc = __builtin_rev_crc32_data8(crc, p[i], 0x04C11DB7u);
    }
    return ~crc;
}

uint16_t el_crc16_ibm3740_update(uint16_t crc, const void *data, size_t len)
{
    EL_ASSERT(data != NULL || len == 0);
    const uint8_t *p = data;

    for (size_t i = 0; i < len; i++) {
        crc = __builtin_crc16_data8(crc, p[i], (uint16_t)0x1021u);
    }
    return crc;
}
