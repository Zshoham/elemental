#include "elemental_config.h"

#include <elemental/assert.h>
#include <elemental/time_manual.h>

#include <stddef.h>

static el_ticks manual_now(void *ctx)
{
    const el_manual_clock *const clock = ctx;
    return clock->now;
}

static uint32_t manual_ticks_per_second(void *ctx)
{
    const el_manual_clock *const clock = ctx;
    return clock->ticks_per_second;
}

static const el_clock_ops manual_ops = {
    .now = manual_now,
    .ticks_per_second = manual_ticks_per_second,
};

void el_manual_clock_init(el_manual_clock *clock, uint32_t ticks_per_second)
{
    EL_ASSERT(clock != NULL);
    EL_ASSERT(ticks_per_second >= 1);
    clock->now = 0;
    clock->ticks_per_second = ticks_per_second;
}

void el_manual_clock_advance(el_manual_clock *clock, el_ticks ticks)
{
    EL_ASSERT(clock != NULL);
    EL_ASSERT(ticks <= UINT64_MAX - clock->now);
    clock->now += ticks;
}

el_clock el_manual_clock_as_clock(el_manual_clock *clock)
{
    EL_ASSERT(clock != NULL);
    return (el_clock){.ctx = clock, .ops = &manual_ops};
}
