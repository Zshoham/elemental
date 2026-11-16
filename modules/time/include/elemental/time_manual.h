/*
 * elemental/time_manual.h - a clock that moves only when it is told to.
 *
 * The portable implementation of el_clock: no operating system, no
 * hardware. Its use is in tests of code that depends on time passing: the
 * test advances the clock, so "250 ms later" is exact and takes no time.
 * It also serves a program that has no clock yet, as one that stays at 0.
 *
 * STATUS: experimental. See docs/process/api-stability.md.
 *
 * Storage and ownership: the caller provides the object and keeps it alive
 * while it or any handle made from it is in use. It holds no resources, so
 * there is nothing to destroy. It may be copied or moved when no handle
 * made from it is in use. The struct layout is public so that callers can
 * allocate it; use the functions.
 *
 * Concurrency: no internal synchronization. One object may be used from one
 * context at a time. Nothing here blocks, so use from an interrupt handler
 * is fine under that same rule.
 */
#ifndef ELEMENTAL_TIME_MANUAL_H
#define ELEMENTAL_TIME_MANUAL_H

#include <elemental/time.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct el_manual_clock {
    el_ticks now;
    uint32_t ticks_per_second;
} el_manual_clock;

/*
 * A clock of `ticks_per_second` (at least 1) that reads 0 until it is
 * advanced.
 */
void el_manual_clock_init(el_manual_clock *clock, uint32_t ticks_per_second);

/*
 * Move the reading forward by `ticks`. Moving it past the largest el_ticks
 * is a contract violation. There is no way to move it back.
 */
void el_manual_clock_advance(el_manual_clock *clock, el_ticks ticks);

/* A clock handle for `clock`: the reading is exactly what it was advanced to. */
el_clock el_manual_clock_as_clock(el_manual_clock *clock);

#ifdef __cplusplus
}
#endif

#endif /* ELEMENTAL_TIME_MANUAL_H */
