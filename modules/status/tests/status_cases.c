#include "status_cases.h"

EL_NODISCARD static el_status step(el_status result, int *steps)
{
    (*steps)++;
    return result;
}

el_status status_case_try(el_status first, el_status second, int *steps)
{
    EL_TRY(step(first, steps));
    EL_TRY(step(second, steps));
    return EL_OK;
}

/* The braces are left out on purpose. */
/* clang-format off */
el_status status_case_try_unbraced(int take_first, el_status first, el_status second)
{
    if (take_first)
        EL_TRY(first);
    else
        EL_TRY(second);
    return EL_OK;
}
/* clang-format on */

el_status status_case_try_goto(el_status first, el_status second, int *steps, int *released)
{
    el_status status;
    EL_TRY_GOTO(step(first, steps), status, release);
    EL_TRY_GOTO(step(second, steps), status, release);
release:
    (*released)++;
    return status;
}

/* A platform call in the negative-errno convention: a count, or a negative
 * code. */
static int platform_call(int result, int *calls)
{
    (*calls)++;
    return result;
}

static int64_t platform_call_wide(int64_t result, int *calls)
{
    (*calls)++;
    return result;
}

el_status status_case_try_neg(int result, int *calls, int *value)
{
    const int got = platform_call(result, calls);
    EL_TRY_NEG(got);
    *value = got;
    return EL_OK;
}

el_status status_case_try_neg_wide(int64_t result, int *calls)
{
    EL_TRY_NEG(platform_call_wide(result, calls));
    return EL_OK;
}

el_status status_case_ensure(int condition, el_status status, int *status_evaluations)
{
    EL_ENSURE(condition, step(status, status_evaluations));
    return EL_OK;
}

void status_case_ignore(el_status status, int *steps)
{
    el_status_ignore(step(status, steps));
}
