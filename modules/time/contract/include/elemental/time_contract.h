/*
 * elemental/time_contract.h - the fixture for the clock contract suite.
 *
 * Every el_clock implementation is expected to pass the clock contract
 * suite (elemental/time_contract.hpp). The suite checks what the interface
 * promises (elemental/time.h), not what a particular platform does;
 * platform behavior belongs in the backend's own tests.
 *
 * A fixture tells the suite how to set up the implementation under test. It
 * is plain C, so a clock written in C is described in C; only the line that
 * hands the fixture to the suite is C++.
 *
 * See modules/time/tests/fixture_manual.c for the smallest complete fixture.
 */
#ifndef ELEMENTAL_TIME_CONTRACT_H
#define ELEMENTAL_TIME_CONTRACT_H

#include <elemental/time.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * How to set up the implementation under test.
 *
 * The suite uses one clock at a time. Before asking for a new one it calls
 * `reset`, which must release whatever the previous request created. `ctx`
 * is passed back to every function unchanged.
 */
typedef struct el_time_contract_fixture {
    /* Shown in failure reports. */
    const char *name;
    void *ctx;

    /* Required. Release the clock created by the previous request, if any. */
    void (*reset)(void *ctx);

    /* Required. A clock. */
    el_clock (*clock)(void *ctx);

    /*
     * Optional capability. Return once at least `ticks` of the current
     * clock's ticks have passed: by waiting, for a clock that runs on its
     * own, or by moving it. `ticks` is at most a hundredth of a second's
     * worth, and at least 1. Leave null if time cannot be made to pass from
     * outside the clock; the suite reports the tests that need it as
     * skipped.
     */
    void (*advance)(void *ctx, el_ticks ticks);
} el_time_contract_fixture;

#ifdef __cplusplus
}
#endif

#endif /* ELEMENTAL_TIME_CONTRACT_H */
