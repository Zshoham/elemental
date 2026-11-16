/*
 * Example backend: a clock that moves forward by a fixed step every time it
 * is read.
 *
 * It needs nothing from a platform, and a program run twice sees the same
 * times: useful for a simulation, or for giving log records distinct,
 * ordered timestamps on a target that has no clock yet. Unlike the manual
 * clock (elemental/time_manual.h) it cannot be moved from outside, which is
 * why its contract fixture leaves `advance` out.
 *
 * Storage: the caller provides the object and keeps it alive while it or
 * any handle made from it is in use. Nothing to destroy. One context at a
 * time; nothing blocks.
 */
#ifndef STEP_CLOCK_H
#define STEP_CLOCK_H

#include <elemental/time.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct step_clock {
    el_ticks next;
    el_ticks step;
    uint32_t ticks_per_second;
} step_clock;

/*
 * The first reading is 0, and each reading after it is `step` ticks later.
 * `ticks_per_second` is at least 1. A `step` of 0 makes a clock that stands
 * still.
 */
void step_clock_init(step_clock *clock, uint32_t ticks_per_second, el_ticks step);

/*
 * A clock handle for `clock`. A reading that would pass the largest
 * el_ticks stays there: the clock never goes back.
 */
el_clock step_clock_as_clock(step_clock *clock);

#ifdef __cplusplus
}
#endif

#endif /* STEP_CLOCK_H */
