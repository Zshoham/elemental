#include "elemental_config.h"

#include <elemental/time_zephyr.h>

#include <stddef.h>

#include <zephyr/kernel.h>

static el_ticks zephyr_now(void *ctx)
{
    (void)ctx;
    /* Uptime starts at 0 and only grows, so it is never negative. */
    return (el_ticks)k_uptime_ticks();
}

static uint32_t zephyr_ticks_per_second(void *ctx)
{
    (void)ctx;
    return CONFIG_SYS_CLOCK_TICKS_PER_SEC;
}

static const el_clock_ops zephyr_ops = {
    .now = zephyr_now,
    .ticks_per_second = zephyr_ticks_per_second,
};

el_clock el_zephyr_clock(void)
{
    return (el_clock){.ctx = NULL, .ops = &zephyr_ops};
}
