/*
 * Contract fixture for the Zephyr kernel's uptime. It runs on its own, so
 * making time pass is sleeping.
 */
#include "fixture_zephyr.h"

#include <elemental/time_zephyr.h>

#include <stddef.h>

#include <zephyr/kernel.h>

static void reset(void *ctx)
{
    (void)ctx; /* The clock has no object to release. */
}

static el_clock clock_new(void *ctx)
{
    (void)ctx;
    return el_zephyr_clock();
}

static void advance(void *ctx, el_ticks ticks)
{
    (void)ctx;
    const el_clock clock = el_zephyr_clock();
    const el_ticks start = el_clock_now(clock);
    /* A tick is a kernel tick. A sleep may be cut short by a wake-up. */
    while (el_clock_now(clock) - start < ticks) {
        k_sleep(K_TICKS((k_ticks_t)ticks));
    }
}

const el_time_contract_fixture time_fixture_zephyr = {
    .name = "zephyr",
    .ctx = NULL,
    .reset = reset,
    .clock = clock_new,
    .advance = advance,
};
