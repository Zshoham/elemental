/*
 * Byte order is defined by its vectors: 0x0102...08 stored little endian is
 * 08 07 ... 01, and big endian is 01 02 ... 08. Every function is also run
 * at each offset 0..7, so every misalignment is covered, and checked not to
 * touch the bytes around it. Under the sanitize preset, UBSan would report a
 * misaligned wide access.
 */
#include <gtest/gtest.h>

#include <elemental/bytes.h>

#include <endian.h> /* glibc: the host oracle */

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
    Buffer() { bytes.fill(kGuard); }
    uint8_t *at(size_t offset) { return bytes.data() + offset; }
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

TEST(Bytes, RandomValuesMatchGlibcAndRoundTrip)
{
    std::mt19937_64 random(20261111);
    for (int i = 0; i < 10000; i++) {
        const uint64_t v = random();
        const size_t offset = static_cast<size_t>(i % 8);
        Buffer buffer;
        uint8_t *p = buffer.at(offset);

        el_bytes_store_le64(p, v);
        uint64_t le64;
        std::memcpy(&le64, p, 8);
        EXPECT_EQ(le64, htole64(v));
        EXPECT_EQ(el_bytes_load_le64(p), v);

        el_bytes_store_be64(p, v);
        uint64_t be64;
        std::memcpy(&be64, p, 8);
        EXPECT_EQ(be64, htobe64(v));
        EXPECT_EQ(el_bytes_load_be64(p), v);

        const auto v32 = static_cast<uint32_t>(v);
        el_bytes_store_le32(p, v32);
        uint32_t le32;
        std::memcpy(&le32, p, 4);
        EXPECT_EQ(le32, htole32(v32));
        EXPECT_EQ(el_bytes_load_le32(p), v32);

        el_bytes_store_be32(p, v32);
        uint32_t be32;
        std::memcpy(&be32, p, 4);
        EXPECT_EQ(be32, htobe32(v32));
        EXPECT_EQ(el_bytes_load_be32(p), v32);

        const auto v16 = static_cast<uint16_t>(v);
        el_bytes_store_le16(p, v16);
        uint16_t le16;
        std::memcpy(&le16, p, 2);
        EXPECT_EQ(le16, htole16(v16));
        EXPECT_EQ(el_bytes_load_le16(p), v16);

        el_bytes_store_be16(p, v16);
        uint16_t be16;
        std::memcpy(&be16, p, 2);
        EXPECT_EQ(be16, htobe16(v16));
        EXPECT_EQ(el_bytes_load_be16(p), v16);
    }
}

} // namespace
