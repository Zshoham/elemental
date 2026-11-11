/*
 * Byte order is defined by its vectors: 0x0102...08 stored little endian is
 * 08 07 ... 01, and big endian is 01 02 ... 08. Every function is also run
 * at each offset 0..7, so every misalignment is covered, and checked not to
 * touch the bytes around it. Random values are compared with a reference
 * written byte by byte in the test. Under the sanitize preset, UBSan would
 * report a misaligned wide access.
 */
#include <gtest/gtest.h>

#include <elemental/bytes.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <random>

namespace
{

constexpr uint8_t kGuard = 0xA5;

/* A buffer of guard bytes, so a store that writes outside its width shows. */
struct Buffer {
    std::array<uint8_t, 24> bytes;
    Buffer()
    {
        bytes.fill(kGuard);
    }
    uint8_t *at(size_t offset)
    {
        return bytes.data() + offset;
    }
};

void ExpectOnly(Buffer &buffer, size_t offset, const std::array<uint8_t, 8> &expected, size_t size)
{
    for (size_t i = 0; i < buffer.bytes.size(); i++) {
        if (i >= offset && i < offset + size) {
            EXPECT_EQ(buffer.bytes[i], expected[i - offset]) << "byte " << i;
        } else {
            EXPECT_EQ(buffer.bytes[i], kGuard) << "byte " << i << " was touched";
        }
    }
}

const std::array<uint8_t, 8> kAscending = {1, 2, 3, 4, 5, 6, 7, 8};
const std::array<uint8_t, 8> kDescending8 = {8, 7, 6, 5, 4, 3, 2, 1};
const std::array<uint8_t, 8> kDescending4 = {4, 3, 2, 1, 0, 0, 0, 0};
const std::array<uint8_t, 8> kDescending2 = {2, 1, 0, 0, 0, 0, 0, 0};

TEST(Bytes, LittleEndianLoadsPutTheFirstByteLowestAtEveryOffset)
{
    for (size_t offset = 0; offset < 8; offset++) {
        Buffer buffer;
        std::memcpy(buffer.at(offset), kAscending.data(), 8);
        EXPECT_EQ(el_bytes_load_le16(buffer.at(offset)), 0x0201u);
        EXPECT_EQ(el_bytes_load_le32(buffer.at(offset)), 0x04030201u);
        EXPECT_EQ(el_bytes_load_le64(buffer.at(offset)), 0x0807060504030201u);
    }
}

TEST(Bytes, BigEndianLoadsPutTheFirstByteHighestAtEveryOffset)
{
    for (size_t offset = 0; offset < 8; offset++) {
        Buffer buffer;
        std::memcpy(buffer.at(offset), kAscending.data(), 8);
        EXPECT_EQ(el_bytes_load_be16(buffer.at(offset)), 0x0102u);
        EXPECT_EQ(el_bytes_load_be32(buffer.at(offset)), 0x01020304u);
        EXPECT_EQ(el_bytes_load_be64(buffer.at(offset)), 0x0102030405060708u);
    }
}

TEST(Bytes, LittleEndianStoresWriteExactlyTheirWidthAtEveryOffset)
{
    for (size_t offset = 0; offset < 8; offset++) {
        Buffer b16, b32, b64;
        el_bytes_store_le16(b16.at(offset), 0x0102u);
        el_bytes_store_le32(b32.at(offset), 0x01020304u);
        el_bytes_store_le64(b64.at(offset), 0x0102030405060708u);
        ExpectOnly(b16, offset, kDescending2, 2);
        ExpectOnly(b32, offset, kDescending4, 4);
        ExpectOnly(b64, offset, kDescending8, 8);
    }
}

TEST(Bytes, BigEndianStoresWriteExactlyTheirWidthAtEveryOffset)
{
    for (size_t offset = 0; offset < 8; offset++) {
        Buffer b16, b32, b64;
        el_bytes_store_be16(b16.at(offset), 0x0102u);
        el_bytes_store_be32(b32.at(offset), 0x01020304u);
        el_bytes_store_be64(b64.at(offset), 0x0102030405060708u);
        ExpectOnly(b16, offset, kAscending, 2);
        ExpectOnly(b32, offset, kAscending, 4);
        ExpectOnly(b64, offset, kAscending, 8);
    }
}

TEST(Bytes, HighBitsSurviveEveryWidth)
{
    uint8_t ones[8];
    std::memset(ones, 0xFF, sizeof ones);
    EXPECT_EQ(el_bytes_load_le16(ones), UINT16_MAX);
    EXPECT_EQ(el_bytes_load_be16(ones), UINT16_MAX);
    EXPECT_EQ(el_bytes_load_le32(ones), UINT32_MAX);
    EXPECT_EQ(el_bytes_load_be32(ones), UINT32_MAX);
    EXPECT_EQ(el_bytes_load_le64(ones), UINT64_MAX);
    EXPECT_EQ(el_bytes_load_be64(ones), UINT64_MAX);
}

/* The reference: byte i of a little-endian value is bits 8i to 8i+7, and
 * big endian is the same bytes in reverse order. */
void Reference(uint8_t *out, uint64_t value, size_t size, bool big)
{
    for (size_t i = 0; i < size; i++) {
        out[big ? size - 1 - i : i] = static_cast<uint8_t>(value >> (8 * i));
    }
}

void ExpectStored(const uint8_t *stored, uint64_t value, size_t size, bool big)
{
    uint8_t expected[8];
    Reference(expected, value, size, big);
    EXPECT_EQ(std::memcmp(stored, expected, size), 0)
        << (big ? "big" : "little") << " endian, " << size << " bytes, value " << value;
}

TEST(Bytes, RandomValuesMatchTheReferenceAndRoundTrip)
{
    std::mt19937_64 random(20261111);
    for (int i = 0; i < 2000; i++) {
        const uint64_t v = random();
        const size_t offset = static_cast<size_t>(i % 8);
        Buffer buffer;
        uint8_t *p = buffer.at(offset);
        const auto v32 = static_cast<uint32_t>(v);
        const auto v16 = static_cast<uint16_t>(v);

        el_bytes_store_le64(p, v);
        ExpectStored(p, v, 8, false);
        EXPECT_EQ(el_bytes_load_le64(p), v);
        el_bytes_store_be64(p, v);
        ExpectStored(p, v, 8, true);
        EXPECT_EQ(el_bytes_load_be64(p), v);

        el_bytes_store_le32(p, v32);
        ExpectStored(p, v32, 4, false);
        EXPECT_EQ(el_bytes_load_le32(p), v32);
        el_bytes_store_be32(p, v32);
        ExpectStored(p, v32, 4, true);
        EXPECT_EQ(el_bytes_load_be32(p), v32);

        el_bytes_store_le16(p, v16);
        ExpectStored(p, v16, 2, false);
        EXPECT_EQ(el_bytes_load_le16(p), v16);
        el_bytes_store_be16(p, v16);
        ExpectStored(p, v16, 2, true);
        EXPECT_EQ(el_bytes_load_be16(p), v16);
    }
}

} // namespace
