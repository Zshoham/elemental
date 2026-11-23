/*
 * Contract fixture for the step clock. It moves only when it is read, so
 * nothing outside it can make time pass: `advance` is left null, and the
 * suite reports the two tests that need it as skipped. A fixture never
 * fakes a capability.
 */
#include "step_clock_fixture.h"

#include "step_clock.h"

#include <stddef.h>

static step_clock stepping;

static el_clock clock_new(void *ctx)
{
    (void)ctx;
    step_clock_init(&stepping, 1000, 7);
    return step_clock_as_clock(&stepping);
}

const el_time_contract_fixture step_clock_fixture = {
    .name = "step_clock",
    .ctx = NULL,
    .clock = clock_new,
    .advance = NULL,
};
