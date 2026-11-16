/* clock_gettime() and CLOCK_MONOTONIC. */
#define _POSIX_C_SOURCE 200809L

#include "elemental_config.h"

#include <elemental/assert.h>
#include <elemental/time_posix.h>

#include <stddef.h>
#include <time.h>

static el_ticks posix_now(void *ctx)
{
    (void)ctx;
    struct timespec ts;
    /* With a valid clock and a valid pointer this call has no failure. */
    const int result = clock_gettime(CLOCK_MONOTONIC, &ts);
    EL_ASSERT(result == 0);
    (void)result;
    return (el_ticks)ts.tv_sec * 1000000000u + (el_ticks)ts.tv_nsec;
}

static uint32_t posix_ticks_per_second(void *ctx)
{
    (void)ctx;
    return 1000000000u;
}

static const el_clock_ops posix_ops = {
    .now = posix_now,
    .ticks_per_second = posix_ticks_per_second,
};

el_clock el_posix_clock(void)
{
    return (el_clock){.ctx = NULL, .ops = &posix_ops};
}
