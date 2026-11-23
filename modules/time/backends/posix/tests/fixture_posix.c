/*
 * Contract fixture for the system's monotonic clock. It runs on its own,
 * so making time pass is waiting for it.
 */
#define _POSIX_C_SOURCE 200809L

#include "fixture_posix.h"

#include <elemental/time_posix.h>

#include <stddef.h>
#include <time.h>

static el_clock clock_new(void *ctx)
{
    (void)ctx;
    return el_posix_clock();
}

static void advance(void *ctx, el_ticks ticks)
{
    (void)ctx;
    const el_clock clock = el_posix_clock();
    const el_ticks start = el_clock_now(clock);
    /* A tick is a nanosecond. A sleep may end early, on a signal. */
    while (el_clock_now(clock) - start < ticks) {
        const struct timespec nap = {.tv_sec = 0, .tv_nsec = (long)ticks};
        nanosleep(&nap, NULL);
    }
}

const el_time_contract_fixture time_fixture_posix = {
    .name = "posix",
    .ctx = NULL,
    .clock = clock_new,
    .advance = advance,
};
