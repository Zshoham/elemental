#include "elemental_config.h"

#include <elemental/assert.h>
#include <elemental/time.h>

#include <stddef.h>

el_ticks el_clock_now(el_clock clock)
{
    EL_ASSERT(clock.ops != NULL);
    EL_ASSERT(clock.ops->now != NULL);
    return clock.ops->now(clock.ctx);
}

uint32_t el_clock_ticks_per_second(el_clock clock)
{
    EL_ASSERT(clock.ops != NULL);
    EL_ASSERT(clock.ops->ticks_per_second != NULL);
    const uint32_t ticks_per_second = clock.ops->ticks_per_second(clock.ctx);
    EL_ASSERT(ticks_per_second >= 1);
    return ticks_per_second;
}
