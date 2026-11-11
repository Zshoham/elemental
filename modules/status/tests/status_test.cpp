#include <gtest/gtest.h>

#include <elemental/status.h>

#include <cstdint>

TEST(Status, OkIsZero)
{
    /* So a zero-initialized status means success. */
    EXPECT_EQ(EL_OK, 0);
}

TEST(Status, CodesNeverChange)
{
    /* Codes are part of the ABI. */
    EXPECT_EQ(EL_ERR_WOULD_BLOCK, 1);
    EXPECT_EQ(EL_ERR_END_OF_STREAM, 2);
    EXPECT_EQ(EL_ERR_IO, 3);
    EXPECT_EQ(sizeof(el_status), 4u);
}

TEST(Status, EveryCodeHasAName)
{
    EXPECT_STREQ(el_status_name(EL_OK), "ok");
    EXPECT_STREQ(el_status_name(EL_ERR_WOULD_BLOCK), "would_block");
    EXPECT_STREQ(el_status_name(EL_ERR_END_OF_STREAM), "end_of_stream");
    EXPECT_STREQ(el_status_name(EL_ERR_IO), "io");
}

TEST(Status, ValuesOutsideTheVocabularyAreNamedUnknown)
{
    /* Never NULL, so the result can always be printed. */
    EXPECT_STREQ(el_status_name(4), "unknown");
    EXPECT_STREQ(el_status_name(-1), "unknown");
    EXPECT_STREQ(el_status_name(INT32_MAX), "unknown");
    EXPECT_STREQ(el_status_name(INT32_MIN), "unknown");
}

TEST(Status, NamesAreDistinct)
{
    for (el_status a = EL_OK; a <= EL_ERR_IO; a++) {
        for (el_status b = a + 1; b <= EL_ERR_IO; b++) {
            EXPECT_STRNE(el_status_name(a), el_status_name(b));
        }
    }
}
