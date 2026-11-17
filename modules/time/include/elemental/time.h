/*
 * elemental/time.h - the clock interface, and time as a count of ticks.
 *
 * Code that needs to know what time it is takes an el_clock as a parameter,
 * so its caller decides which clock: the system's in a program, one the
 * test advances in a test (elemental/time_manual.h). Nothing in Elemental
 * reads a clock it was not handed.
 *
 * STATUS: experimental. See docs/process/api-stability.md.
 *
 * Time values (docs/adr/0020-time-representation.md). A reading is an
 * el_ticks: a 64-bit count of the clock's own ticks since an origin the
 * clock chooses. How long a tick is, is the clock's to say:
 * el_clock_ticks_per_second(). So:
 *
 *   - Readings of one clock compare and subtract as plain integers. A clock
 *     never goes back, so the later reading is the larger one, and the
 *     count does not wrap: at one tick per nanosecond it lasts 584 years.
 *   - A reading means something only with the clock it came from. Readings
 *     of two clocks do not compare, even at the same frequency. Whatever
 *     passes a reading on to code that must print it passes the frequency
 *     too.
 *   - A duration is a difference of readings, also in ticks. Subtract the
 *     earlier from the later: the type is unsigned, so the other way round
 *     wraps. A deadline is a reading, and it has passed when
 *     el_clock_now(clock) >= deadline.
 *   - Converting to or from a human unit is a function call, below, and is
 *     where the division is. With the clock at hand, the el_clock_ forms
 *     read its frequency for you; a value that has left its clock, such as
 *     a log record's timestamp, is converted with the frequency it carries.
 *
 * What is not here: wall time, calendars, time zones, and sleeping. Nothing
 * in this module waits.
 *
 * Errors: none. Reading a monotonic clock cannot fail, so el_clock_now()
 * returns its result.
 *
 * Ownership and lifetime: el_clock is a handle, two pointers passed by
 * value, owning nothing. It is valid while the object it was made from is
 * alive and has not been moved. There is nothing to release.
 *
 * Concurrency and execution context: the functions here keep no state. A
 * clock may be read from two contexts at once, or from an interrupt
 * handler, exactly when its implementation says so. Reading never blocks.
 * The conversions are pure functions.
 *
 * Contract violations, checked by assertions when the library is built with
 * them: a handle that was never made; a frequency of 0, from an
 * implementation or passed to a conversion; a null output parameter.
 */
#ifndef ELEMENTAL_TIME_H
#define ELEMENTAL_TIME_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* A count of one clock's ticks: a reading, or a duration. */
typedef uint64_t el_ticks;

/* ---- Interface ----------------------------------------------------------
 *
 * To implement a clock, define the two operations over your own state and
 * hand out a handle that pairs them with it. Operations are called through
 * C and must not unwind.
 */
typedef struct el_clock_ops {
    /*
     * The clock's reading.
     *
     * Must: never return less than an earlier call returned. Not block.
     */
    el_ticks (*now)(void *ctx);

    /*
     * How many ticks make one second.
     *
     * Must: return the same value on every call, at least 1.
     */
    uint32_t (*ticks_per_second)(void *ctx);
} el_clock_ops;

/*
 * `ops` must outlive every handle that uses it; make it static const.
 * `ctx` is passed back to the operations unchanged and may be null if the
 * implementation has no state.
 */
typedef struct el_clock {
    void *ctx;
    const el_clock_ops *ops;
} el_clock;

/* The clock's reading now, in its ticks. Never less than an earlier one. */
el_ticks el_clock_now(el_clock clock);

/*
 * How many of this clock's ticks make one second: at least 1, and the same
 * for as long as the clock is used.
 */
uint32_t el_clock_ticks_per_second(el_clock clock);

/* ---- Conversions ---------------------------------------------------------
 *
 * Pure functions. They use no division or multiplication helper from the
 * compiler's runtime, so they add no symbol on a 32-bit target; they are
 * for setting a timeout up and for printing, not for a loop that runs per
 * tick.
 */

/*
 * The number of ticks that covers `ms` milliseconds, rounded up, so that a
 * timeout made from it never ends early:
 *
 *     const el_ticks deadline = el_clock_now(clock) + el_clock_ticks_from_ms(clock, 250);
 *
 * `ticks_per_second` is at least 1. The result cannot overflow.
 */
el_ticks el_ticks_from_ms(uint32_t ms, uint32_t ticks_per_second);

/*
 * The whole seconds in `ticks`, and through `us` the microseconds past
 * them, 0 to 999999, rounded down: a printed time is one that has passed.
 *
 *     uint32_t us;
 *     const uint64_t seconds = el_ticks_to_seconds(took, frequency, &us);
 *
 * `ticks_per_second` is at least 1. `us` is required and always written.
 */
uint64_t el_ticks_to_seconds(el_ticks ticks, uint32_t ticks_per_second, uint32_t *us);

/*
 * The same two conversions in the unit of `clock`, whose frequency they
 * read: el_ticks_from_ms(ms, el_clock_ticks_per_second(clock)), and the
 * same for el_ticks_to_seconds(). Only a convenience; the frequency is
 * read on every call, which costs one call through the clock's operations.
 *
 *     uint32_t us;
 *     const uint64_t seconds = el_clock_ticks_to_seconds(clock, took, &us);
 */
el_ticks el_clock_ticks_from_ms(el_clock clock, uint32_t ms);
uint64_t el_clock_ticks_to_seconds(el_clock clock, el_ticks ticks, uint32_t *us);

#ifdef __cplusplus
}
#endif

#endif /* ELEMENTAL_TIME_H */
