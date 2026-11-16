/*
 * elemental/time_posix.h - the system's monotonic clock.
 *
 * STATUS: experimental. See docs/process/api-stability.md, and PORT.md next
 * to this backend for what it does and does not support.
 *
 * The clock is CLOCK_MONOTONIC, read with clock_gettime(). A tick is a
 * nanosecond: el_clock_ticks_per_second() is 1 000 000 000. The origin is
 * whatever the system's is, on Linux the boot. It is not affected by
 * setting the system's time, and on Linux it does not count time the
 * system spent suspended.
 *
 * The handle has no object behind it and holds no state, so it is valid for
 * the life of the program and may be made and used from any thread at once.
 * clock_gettime() is async-signal-safe, so a signal handler may read it too.
 */
#ifndef ELEMENTAL_TIME_POSIX_H
#define ELEMENTAL_TIME_POSIX_H

#include <elemental/time.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The system's monotonic clock, in nanoseconds. */
el_clock el_posix_clock(void);

#ifdef __cplusplus
}
#endif

#endif /* ELEMENTAL_TIME_POSIX_H */
