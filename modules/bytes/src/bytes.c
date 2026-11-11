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
