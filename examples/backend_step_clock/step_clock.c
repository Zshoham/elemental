#include "step_clock.h"

#include <stddef.h>

static el_ticks step_now(void *ctx)
{
    step_clock *const clock = ctx;
    const el_ticks reading = clock->next;
    /* Stop at the top instead of wrapping: a clock never goes back. */
    clock->next = clock->step <= UINT64_MAX - reading ? reading + clock->step : UINT64_MAX;
    return reading;
}

static uint32_t step_ticks_per_second(void *ctx)
{
    const step_clock *const clock = ctx;
    return clock->ticks_per_second;
}

static const el_clock_ops step_ops = {
    .now = step_now,
    .ticks_per_second = step_ticks_per_second,
};

void step_clock_init(step_clock *clock, uint32_t ticks_per_second, el_ticks step)
{
    clock->next = 0;
    clock->step = step;
    clock->ticks_per_second = ticks_per_second;
}

el_clock step_clock_as_clock(step_clock *clock)
{
    return (el_clock){.ctx = clock, .ops = &step_ops};
}
