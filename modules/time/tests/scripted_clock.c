#include "scripted_clock.h"

#include <stddef.h>

static el_ticks scripted_now(void *ctx)
{
    scripted_clock *const clock = ctx;
    clock->now_calls++;
    clock->last_ctx = ctx;
    return clock->reading;
}

static uint32_t scripted_ticks_per_second(void *ctx)
{
    scripted_clock *const clock = ctx;
    clock->ticks_per_second_calls++;
    clock->last_ctx = ctx;
    return clock->ticks_per_second;
}

static const el_clock_ops scripted_ops = {
    .now = scripted_now,
    .ticks_per_second = scripted_ticks_per_second,
};

static const el_clock_ops ops_without_now = {
    .now = NULL,
    .ticks_per_second = scripted_ticks_per_second,
};

static const el_clock_ops ops_without_ticks_per_second = {
    .now = scripted_now,
    .ticks_per_second = NULL,
};

el_clock scripted_clock_as_clock(scripted_clock *clock)
{
    return (el_clock){.ctx = clock, .ops = &scripted_ops};
}

el_clock scripted_clock_without_now(scripted_clock *clock)
{
    return (el_clock){.ctx = clock, .ops = &ops_without_now};
}

el_clock scripted_clock_without_ticks_per_second(scripted_clock *clock)
{
    return (el_clock){.ctx = clock, .ops = &ops_without_ticks_per_second};
}
