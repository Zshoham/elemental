#include "elemental_config.h"

#include <elemental/assert.h>
#include <elemental/crc.h>

/*
 * 16-entry tables (CRC_TABLE_4BIT, the default): two lookups per byte, one
 * per 4-bit half, and 96 bytes of read-only data. Entry i is the register
 * after shifting the 4-bit value i through it: for CRC-32, reflected, low
 * half first; for CRC-16, MSB first, high half first. The tests check the
 * results against the catalogue's check values and against the 256-entry
 * implementation.
 */

static const uint32_t crc32_table[16] = {
    0x00000000u, 0x1DB71064u, 0x3B6E20C8u, 0x26D930ACu, 0x76DC4190u, 0x6B6B51F4u,
    0x4DB26158u, 0x5005713Cu, 0xEDB88320u, 0xF00F9344u, 0xD6D6A3E8u, 0xCB61B38Cu,
    0x9B64C2B0u, 0x86D3D2D4u, 0xA00AE278u, 0xBDBDF21Cu,
};

static const uint16_t crc16_table[16] = {
    0x0000u, 0x1021u, 0x2042u, 0x3063u, 0x4084u, 0x50A5u, 0x60C6u, 0x70E7u,
    0x8108u, 0x9129u, 0xA14Au, 0xB16Bu, 0xC18Cu, 0xD1ADu, 0xE1CEu, 0xF1EFu,
};

uint32_t el_crc32_update(uint32_t crc, const void *data, size_t len)
{
    EL_ASSERT(data != NULL || len == 0);
    const uint8_t *p = data;

    crc = ~crc;
    for (size_t i = 0; i < len; i++) {
        crc = (crc >> 4) ^ crc32_table[(crc ^ p[i]) & 0xFu];
        crc = (crc >> 4) ^ crc32_table[(crc ^ (uint32_t)(p[i] >> 4)) & 0xFu];
    }
    return ~crc;
}

uint16_t el_crc16_ibm3740_update(uint16_t crc, const void *data, size_t len)
{
    EL_ASSERT(data != NULL || len == 0);
    const uint8_t *p = data;

    for (size_t i = 0; i < len; i++) {
        crc = (uint16_t)((crc << 4) ^ crc16_table[((crc >> 12) ^ (p[i] >> 4)) & 0xFu]);
        crc = (uint16_t)((crc << 4) ^ crc16_table[((crc >> 12) ^ p[i]) & 0xFu]);
    }
    return crc;
}
