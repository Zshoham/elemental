#include <gtest/gtest.h>

#include <elemental/status.h>

#include "status_cases.h"

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
    EXPECT_EQ(EL_ERR_NO_SPACE, 4);
    EXPECT_EQ(sizeof(el_status), 4u);
}

TEST(Status, EveryCodeHasAName)
{
    EXPECT_STREQ(el_status_name(EL_OK), "ok");
    EXPECT_STREQ(el_status_name(EL_ERR_WOULD_BLOCK), "would_block");
    EXPECT_STREQ(el_status_name(EL_ERR_END_OF_STREAM), "end_of_stream");
    EXPECT_STREQ(el_status_name(EL_ERR_IO), "io");
    EXPECT_STREQ(el_status_name(EL_ERR_NO_SPACE), "no_space");
}

TEST(Status, EveryElementalCodeIsPositive)
{
    /* Negative values are the application's. */
    for (const el_status code :
         {EL_ERR_WOULD_BLOCK, EL_ERR_END_OF_STREAM, EL_ERR_IO, EL_ERR_NO_SPACE}) {
        EXPECT_GT(code, 0) << el_status_name(code);
    }
}

TEST(Status, ValuesOutsideTheVocabularyAreNamedUnknown)
{
    /* Never NULL, so the result can always be printed. An application's
     * negative status is among them: the application names its own. */
    EXPECT_STREQ(el_status_name(5), "unknown");
    EXPECT_STREQ(el_status_name(-1), "unknown");
    EXPECT_STREQ(el_status_name(INT32_MAX), "unknown");
    EXPECT_STREQ(el_status_name(INT32_MIN), "unknown");
}

TEST(Status, NamesAreDistinct)
{
    for (el_status a = EL_OK; a <= EL_ERR_NO_SPACE; a++) {
        for (el_status b = a + 1; b <= EL_ERR_NO_SPACE; b++) {
            EXPECT_STRNE(el_status_name(a), el_status_name(b));
        }
    }
}

/* ---- Passing a status on (status_cases.c) --------------------------------- */

namespace
{

/* An application's status, and a platform's negative errno value. */
constexpr el_status kApplicationStatus = -1000;
constexpr int kNegativeErrno = -5;

TEST(StatusTry, ContinuesWhileEveryStepIsOk)
{
    int steps = 0;
    EXPECT_EQ(status_case_try(EL_OK, EL_OK, &steps), EL_OK);
    /* Each expression is evaluated exactly once. */
    EXPECT_EQ(steps, 2);
}

TEST(StatusTry, ReturnsTheFirstStatusThatIsNotOk)
{
    const el_status statuses[] = {EL_ERR_WOULD_BLOCK, EL_ERR_END_OF_STREAM, EL_ERR_IO,
                                  EL_ERR_NO_SPACE, kApplicationStatus};
    for (const el_status status : statuses) {
        int steps = 0;
        EXPECT_EQ(status_case_try(status, EL_OK, &steps), status);
        EXPECT_EQ(steps, 1) << "the step after a failure ran";

        steps = 0;
        EXPECT_EQ(status_case_try(EL_OK, status, &steps), status);
        EXPECT_EQ(steps, 2);
    }
}

TEST(StatusTry, IsOneStatement)
{
    EXPECT_EQ(status_case_try_unbraced(1, EL_ERR_IO, EL_ERR_NO_SPACE), EL_ERR_IO);
    EXPECT_EQ(status_case_try_unbraced(0, EL_ERR_IO, EL_ERR_NO_SPACE), EL_ERR_NO_SPACE);
    EXPECT_EQ(status_case_try_unbraced(1, EL_OK, EL_ERR_NO_SPACE), EL_OK);
    EXPECT_EQ(status_case_try_unbraced(0, EL_ERR_IO, EL_OK), EL_OK);
}

TEST(StatusTryGoto, LeavesOkInTheStatusWhenEveryStepSucceeds)
{
    int steps = 0;
    int released = 0;
    EXPECT_EQ(status_case_try_goto(EL_OK, EL_OK, &steps, &released), EL_OK);
    EXPECT_EQ(steps, 2);
    EXPECT_EQ(released, 1);
}

TEST(StatusTryGoto, StoresTheFirstFailureAndJumpsToTheLabel)
{
    const el_status statuses[] = {EL_ERR_IO, kApplicationStatus};
    for (const el_status status : statuses) {
        int steps = 0;
        int released = 0;
        EXPECT_EQ(status_case_try_goto(status, EL_OK, &steps, &released), status);
        EXPECT_EQ(steps, 1) << "the step after a failure ran";
        EXPECT_EQ(released, 1);

        steps = 0;
        released = 0;
        EXPECT_EQ(status_case_try_goto(EL_OK, status, &steps, &released), status);
        EXPECT_EQ(steps, 2);
        EXPECT_EQ(released, 1);
    }
}

TEST(StatusTryNeg, ReturnsANegativeValueAsTheStatus)
{
    int calls = 0;
    int value = 77;
    EXPECT_EQ(status_case_try_neg(kNegativeErrno, &calls, &value), kNegativeErrno);
    EXPECT_EQ(calls, 1);
    EXPECT_EQ(value, 77) << "the function went on after a failure";

    calls = 0;
    EXPECT_EQ(status_case_try_neg_wide(kNegativeErrno, &calls), kNegativeErrno);
    EXPECT_EQ(calls, 1);
    EXPECT_EQ(status_case_try_neg_wide(INT32_MIN, &calls), INT32_MIN);
}

TEST(StatusTryNeg, ContinuesOnZeroAndOnACount)
{
    for (const int result : {0, 1, 12}) {
        int calls = 0;
        int value = -1;
        EXPECT_EQ(status_case_try_neg(result, &calls, &value), EL_OK);
        EXPECT_EQ(calls, 1);
        EXPECT_EQ(value, result);
    }
    /* A count that does not fit el_status is still a count, not a failure. */
    int calls = 0;
    EXPECT_EQ(status_case_try_neg_wide(INT64_MAX, &calls), EL_OK);
    EXPECT_EQ(calls, 1);
}

TEST(StatusEnsure, ReturnsTheStatusUnlessTheConditionHolds)
{
    int evaluations = 0;
    EXPECT_EQ(status_case_ensure(0, EL_ERR_NO_SPACE, &evaluations), EL_ERR_NO_SPACE);
    EXPECT_EQ(status_case_ensure(0, kApplicationStatus, &evaluations), kApplicationStatus);
    EXPECT_EQ(evaluations, 2);
}

TEST(StatusEnsure, EvaluatesTheStatusOnlyWhenTheConditionFails)
{
    int evaluations = 0;
    EXPECT_EQ(status_case_ensure(1, EL_ERR_NO_SPACE, &evaluations), EL_OK);
    EXPECT_EQ(evaluations, 0);
}

TEST(StatusIgnore, TakesAMustUseStatusAndDoesNothingElse)
{
    /* That status_cases.c compiles is the test: el_status_ignore() is the
     * spelling every compiler accepts for a must-use result. */
    int steps = 0;
    status_case_ignore(EL_ERR_IO, &steps);
    EXPECT_EQ(steps, 1);
}

/* The spellings are valid C++ too, for an application or a test that is. */
el_status CxxTry(el_status first, int *reached)
{
    EL_TRY(first);
    EL_ENSURE(reached != nullptr, EL_ERR_NO_SPACE);
    EL_TRY_NEG(0);
    *reached = 1;
    return EL_OK;
}

TEST(StatusTry, CompilesAsCxx)
{
    int reached = 0;
    EXPECT_EQ(CxxTry(EL_ERR_IO, &reached), EL_ERR_IO);
    EXPECT_EQ(reached, 0);
    EXPECT_EQ(CxxTry(EL_OK, &reached), EL_OK);
    EXPECT_EQ(reached, 1);
    EXPECT_EQ(CxxTry(EL_OK, nullptr), EL_ERR_NO_SPACE);
}

} // namespace
