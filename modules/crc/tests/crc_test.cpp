/*
 * The CRCs against published and generated vectors, with whichever
 * implementation the build configured. The default presets build the
 * 16-entry tables and `sanitize` the 256-entry ones
 * (configs/variant_defconfig); integration.choices runs these tests with
 * every option the host's compiler can select, and the Zephyr scenario
 * elemental.tests.crc_zephyr with Zephyr's CRC functions. So every
 * implementation meets the same vectors.
 */
#include <gtest/gtest.h>

#include <elemental/crc.h>

#include <cstdint>
#include <cstring>
#include <vector>

namespace
{

struct Vector {
    size_t len;
    uint32_t crc32;
    uint16_t crc16;
};

const Vector kVectors[] = {
#include "crc_vectors.inc"
};

std::vector<uint8_t> Pattern(size_t len)
{
    std::vector<uint8_t> data(len);
    for (size_t i = 0; i < len; i++) {
        data[i] = static_cast<uint8_t>(i * 31 + 7);
    }
    return data;
}

const char kCheck[] = "123456789";

/* The catalogue's check values: reveng.sourceforge.io/crc-catalogue, 16.htm
 * and 17plus.htm, retrieved 2026-11-11. */
TEST(Crc, Crc32MatchesTheCatalogueCheckValue)
{
    EXPECT_EQ(el_crc32_update(EL_CRC32_INIT, kCheck, 9), 0xCBF43926u);
}

TEST(Crc, Crc16Ibm3740MatchesTheCatalogueCheckValue)
{
    EXPECT_EQ(el_crc16_ibm3740_update(EL_CRC16_IBM3740_INIT, kCheck, 9), 0x29B1u);
}

TEST(Crc, TheCrcOfNoBytesIsTheInitValue)
{
    EXPECT_EQ(el_crc32_update(EL_CRC32_INIT, nullptr, 0), uint32_t{EL_CRC32_INIT});
    EXPECT_EQ(el_crc16_ibm3740_update(EL_CRC16_IBM3740_INIT, nullptr, 0),
              uint16_t{EL_CRC16_IBM3740_INIT});
    /* Zero bytes leave any running value unchanged. */
    EXPECT_EQ(el_crc32_update(0x12345678u, kCheck, 0), 0x12345678u);
    EXPECT_EQ(el_crc16_ibm3740_update(0x1234u, kCheck, 0), 0x1234u);
}

TEST(Crc, EveryLengthMatchesTheGeneratedVectors)
{
    const std::vector<uint8_t> data = Pattern(300);
    for (const Vector &v : kVectors) {
        EXPECT_EQ(el_crc32_update(EL_CRC32_INIT, data.data(), v.len), v.crc32) << "len " << v.len;
        EXPECT_EQ(el_crc16_ibm3740_update(EL_CRC16_IBM3740_INIT, data.data(), v.len), v.crc16)
            << "len " << v.len;
    }
}

TEST(Crc, SplittingTheInputNeverChangesTheResult)
{
    const std::vector<uint8_t> data = Pattern(64);
    const uint32_t whole32 = el_crc32_update(EL_CRC32_INIT, data.data(), data.size());
    const uint16_t whole16 =
        el_crc16_ibm3740_update(EL_CRC16_IBM3740_INIT, data.data(), data.size());
    for (size_t split = 0; split <= data.size(); split++) {
        const size_t rest = data.size() - split;
        uint32_t crc32 = el_crc32_update(EL_CRC32_INIT, data.data(), split);
        crc32 = el_crc32_update(crc32, data.data() + split, rest);
        EXPECT_EQ(crc32, whole32) << "split at " << split;
        uint16_t crc16 = el_crc16_ibm3740_update(EL_CRC16_IBM3740_INIT, data.data(), split);
        crc16 = el_crc16_ibm3740_update(crc16, data.data() + split, rest);
        EXPECT_EQ(crc16, whole16) << "split at " << split;
    }
    /* One byte at a time. */
    uint32_t crc32 = EL_CRC32_INIT;
    uint16_t crc16 = EL_CRC16_IBM3740_INIT;
    for (uint8_t byte : data) {
        crc32 = el_crc32_update(crc32, &byte, 1);
        crc16 = el_crc16_ibm3740_update(crc16, &byte, 1);
    }
    EXPECT_EQ(crc32, whole32);
    EXPECT_EQ(crc16, whole16);
}

TEST(Crc, AMessageFollowedByItsBigEndianCrc16HasACrcOfZero)
{
    for (size_t len : {size_t{0}, size_t{1}, size_t{9}, size_t{200}}) {
        std::vector<uint8_t> data = Pattern(len);
        const uint16_t crc = el_crc16_ibm3740_update(EL_CRC16_IBM3740_INIT, data.data(), len);
        data.push_back(static_cast<uint8_t>(crc >> 8));
        data.push_back(static_cast<uint8_t>(crc));
        EXPECT_EQ(el_crc16_ibm3740_update(EL_CRC16_IBM3740_INIT, data.data(), data.size()), 0u)
            << "len " << len;
    }
}

TEST(Crc, LeadingZeroBytesChangeTheCrc16)
{
    /* The reason for IBM-3740 over an init-0 variant: a spurious 0x00 before
     * a message is detected. */
    const uint8_t with_zero[10] = {0, '1', '2', '3', '4', '5', '6', '7', '8', '9'};
    EXPECT_NE(el_crc16_ibm3740_update(EL_CRC16_IBM3740_INIT, with_zero, 10),
              el_crc16_ibm3740_update(EL_CRC16_IBM3740_INIT, kCheck, 9));
}

TEST(Crc, UnalignedInputGivesTheSameResult)
{
    const std::vector<uint8_t> data = Pattern(40);
    for (size_t offset = 1; offset < 8; offset++) {
        std::vector<uint8_t> shifted(offset, 0xEE);
        shifted.insert(shifted.end(), data.begin(), data.end());
        EXPECT_EQ(el_crc32_update(EL_CRC32_INIT, shifted.data() + offset, data.size()),
                  el_crc32_update(EL_CRC32_INIT, data.data(), data.size()));
        EXPECT_EQ(
            el_crc16_ibm3740_update(EL_CRC16_IBM3740_INIT, shifted.data() + offset, data.size()),
            el_crc16_ibm3740_update(EL_CRC16_IBM3740_INIT, data.data(), data.size()));
    }
}

} // namespace
