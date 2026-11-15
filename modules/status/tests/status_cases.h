/*
 * C functions written with the spellings of elemental/status.h, as an
 * application would write them, for status_test.cpp to call. They are C
 * because the spellings are for C: compiling this file with the library's
 * warnings is part of the test.
 */
#ifndef STATUS_CASES_H
#define STATUS_CASES_H

#include <stdint.h>

#include <elemental/status.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Two steps with EL_TRY. `*steps` counts the steps that ran. */
EL_NODISCARD el_status status_case_try(el_status first, el_status second, int *steps);

/* EL_TRY as the whole body of an unbraced if and else. */
EL_NODISCARD el_status status_case_try_unbraced(int take_first, el_status first, el_status second);

/* Two steps with EL_TRY_GOTO and a label that releases. `*released` counts
 * how often the label was reached. */
EL_NODISCARD el_status status_case_try_goto(el_status first, el_status second, int *steps,
                                            int *released);

/* EL_TRY_NEG on a variable: `*value` gets what the platform call returned,
 * if that was not negative. `*calls` counts the platform calls. */
EL_NODISCARD el_status status_case_try_neg(int result, int *calls, int *value);

/* EL_TRY_NEG on the call itself, of a type wider than el_status. */
EL_NODISCARD el_status status_case_try_neg_wide(int64_t result, int *calls);

/* EL_ENSURE. `*status_evaluations` counts how often its status argument
 * was evaluated. */
EL_NODISCARD el_status status_case_ensure(int condition, el_status status, int *status_evaluations);

/* el_status_ignore() on a must-use function's status. */
void status_case_ignore(el_status status, int *steps);

#ifdef __cplusplus
}
#endif

#endif /* STATUS_CASES_H */
