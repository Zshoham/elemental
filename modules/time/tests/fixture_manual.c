/*
 * Contract fixture for the manual clock: the smallest complete fixture.
 * Time passes when the suite asks, by exactly what it asks.
 */
#include "fixture_manual.h"

#include <elemental/time_manual.h>

#include <stddef.h>

static el_manual_clock manual;

static void reset(void *ctx)
{
    (void)ctx; /* The clock holds nothing to release. */
}

static el_clock clock_new(void *ctx)
{
    (void)ctx;
    /* A frequency no code under test should assume. */
    el_manual_clock_init(&manual, 32768);
    return el_manual_clock_as_clock(&manual);
}

static void advance(void *ctx, el_ticks ticks)
{
    (void)ctx;
    el_manual_clock_advance(&manual, ticks);
}

const el_time_contract_fixture time_fixture_manual = {
    .name = "manual",
    .ctx = NULL,
    .reset = reset,
    .clock = clock_new,
    .advance = advance,
};
