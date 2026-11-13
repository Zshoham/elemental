/*
 * What el_buf promises (elemental/io_buf.h): whole-or-nothing append, the
 * capacity status, writing in place, truncation, and the memory endpoint.
 */
#include <gtest/gtest.h>

#include <elemental/io_buf.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

namespace
{

std::string Contents(const el_buf *b)
{
    const uint8_t *const data = el_buf_data(b);
    return data == nullptr ? std::string()
                           : std::string(reinterpret_cast<const char *>(data), el_buf_len(b));
}

class Buf : public testing::Test
{
  protected:
    void SetUp() override
    {
        el_buf_init(&b, storage, sizeof(storage));
    }

    uint8_t storage[8] = {};
    el_buf b;
    size_t n = 0;
};

TEST_F(Buf, StartsEmptyWithAllItsRoom)
{
    EXPECT_EQ(el_buf_len(&b), 0u);
    EXPECT_EQ(el_buf_room(&b), 8u);
    EXPECT_EQ(el_buf_data(&b), storage);
    EXPECT_EQ(el_buf_spare(&b), storage);
}

TEST_F(Buf, AppendIsWholeOrNothing)
{
    EXPECT_EQ(el_buf_append(&b, "abcde", 5), EL_OK);
    EXPECT_EQ(el_buf_append(&b, "fghi", 4), EL_ERR_NO_SPACE);
    EXPECT_EQ(Contents(&b), "abcde");
    EXPECT_EQ(el_buf_append(&b, "fgh", 3), EL_OK);
    EXPECT_EQ(Contents(&b), "abcdefgh");
    EXPECT_EQ(el_buf_room(&b), 0u);
    EXPECT_EQ(el_buf_append(&b, "x", 1), EL_ERR_NO_SPACE);
    /* Nothing is still something a full buffer can take. */
    EXPECT_EQ(el_buf_append(&b, nullptr, 0), EL_OK);
    EXPECT_EQ(Contents(&b), "abcdefgh");
}

TEST_F(Buf, NoSpaceIsNotPermanent)
{
    ASSERT_EQ(el_buf_append(&b, "abcdefgh", 8), EL_OK);
    ASSERT_EQ(el_buf_append(&b, "i", 1), EL_ERR_NO_SPACE);
    el_buf_truncate(&b, 7);
    EXPECT_EQ(el_buf_append(&b, "i", 1), EL_OK);
    EXPECT_EQ(Contents(&b), "abcdefgi");
}

TEST_F(Buf, AnOverflowingRequestIsNoSpace)
{
    ASSERT_EQ(el_buf_append(&b, "ab", 2), EL_OK);
    EXPECT_EQ(el_buf_reserve(&b, SIZE_MAX), EL_ERR_NO_SPACE);
    EXPECT_EQ(el_buf_reserve(&b, SIZE_MAX - 1), EL_ERR_NO_SPACE);
    EXPECT_EQ(Contents(&b), "ab");
}

TEST_F(Buf, ReserveReportsWhetherTheRoomIsThere)
{
    EXPECT_EQ(el_buf_reserve(&b, 0), EL_OK);
    EXPECT_EQ(el_buf_reserve(&b, 8), EL_OK);
    EXPECT_EQ(el_buf_reserve(&b, 9), EL_ERR_NO_SPACE);
    EXPECT_EQ(el_buf_room(&b), 8u);
}

TEST_F(Buf, SpareAndCommitWriteInPlace)
{
    ASSERT_EQ(el_buf_append(&b, "ab", 2), EL_OK);
    ASSERT_EQ(el_buf_reserve(&b, 3), EL_OK);
    uint8_t *const spare = el_buf_spare(&b);
    EXPECT_EQ(spare, storage + 2);
    std::memcpy(spare, "cdefg", 5);
    el_buf_commit(&b, 3);
    EXPECT_EQ(Contents(&b), "abcde");
    el_buf_commit(&b, 0);
    EXPECT_EQ(el_buf_len(&b), 5u);
}

TEST_F(Buf, DataMutPatchesBytesAlreadyWritten)
{
    ASSERT_EQ(el_buf_append(&b, "?hello", 6), EL_OK);
    el_buf_data_mut(&b)[0] = 5;
    EXPECT_EQ(el_buf_data(&b)[0], 5);
    EXPECT_EQ(el_buf_data_mut(&b), storage);
}

TEST_F(Buf, TruncateKeepsAPrefixAndTheStorage)
{
    ASSERT_EQ(el_buf_append(&b, "abcdef", 6), EL_OK);
    el_buf_truncate(&b, 2);
    EXPECT_EQ(Contents(&b), "ab");
    EXPECT_EQ(el_buf_room(&b), 6u);
    el_buf_truncate(&b, 2);
    el_buf_truncate(&b, 0);
    EXPECT_EQ(el_buf_len(&b), 0u);
    EXPECT_EQ(el_buf_data(&b), storage);
}

TEST(BufZero, AllZerosAndNullStorageAreValidAndAlwaysFull)
{
    el_buf zeros;
    std::memset(&zeros, 0, sizeof(zeros));
    el_buf none;
    el_buf_init(&none, nullptr, 0);
    for (el_buf *b : {&zeros, &none}) {
        EXPECT_EQ(el_buf_len(b), 0u);
        EXPECT_EQ(el_buf_room(b), 0u);
        EXPECT_EQ(el_buf_data(b), nullptr);
        EXPECT_EQ(el_buf_spare(b), nullptr);
        EXPECT_EQ(el_buf_append(b, "x", 1), EL_ERR_NO_SPACE);
        EXPECT_EQ(el_buf_reserve(b, 1), EL_ERR_NO_SPACE);
        EXPECT_EQ(el_buf_reserve(b, 0), EL_OK);
        size_t n = 7;
        EXPECT_EQ(el_write(el_buf_as_writer(b), "x", 1, &n), EL_ERR_NO_SPACE);
        EXPECT_EQ(n, 0u);
    }
}

/* ---- The memory endpoint ------------------------------------------------- */

TEST_F(Buf, AsWriterTakesWhatFitsThenReportsNoSpace)
{
    const el_writer w = el_buf_as_writer(&b);
    EXPECT_EQ(el_write(w, "abcde", 5, &n), EL_OK);
    EXPECT_EQ(n, 5u);
    EXPECT_EQ(el_write(w, "fghij", 5, &n), EL_OK);
    EXPECT_EQ(n, 3u);
    EXPECT_EQ(el_write(w, "x", 1, &n), EL_ERR_NO_SPACE);
    EXPECT_EQ(n, 0u);
    EXPECT_EQ(Contents(&b), "abcdefgh");

    /* el_write_all reports the status and what went through before it. */
    el_buf_truncate(&b, 6);
    EXPECT_EQ(el_write_all(w, "xyz", 3, &n), EL_ERR_NO_SPACE);
    EXPECT_EQ(n, 2u);
    EXPECT_EQ(Contents(&b), "abcdefxy");
}

TEST_F(Buf, AsWriterAppendsAfterWhatTheOwnerAppended)
{
    ASSERT_EQ(el_buf_append(&b, "id=", 3), EL_OK);
    EXPECT_EQ(el_write_all(el_buf_as_writer(&b), "42", 2, nullptr), EL_OK);
    EXPECT_EQ(Contents(&b), "id=42");
}

} // namespace
