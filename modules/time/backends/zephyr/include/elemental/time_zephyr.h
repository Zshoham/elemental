/*
 * elemental/time_zephyr.h - the Zephyr kernel's uptime as a clock.
 *
 * STATUS: experimental. See docs/process/api-stability.md, and PORT.md next
 * to this backend for what it does and does not support.
 *
 * The reading is k_uptime_ticks(): the kernel's ticks since it started. A
 * tick is whatever the application's Zephyr configuration makes it,
 * CONFIG_SYS_CLOCK_TICKS_PER_SEC of them to the second. Reading it is one
 * kernel call and no arithmetic.
 *
 * The handle has no object behind it and holds no state, so it is valid for
 * the life of the program and may be read from any thread. From an interrupt
 * handler: Zephyr's header does not say either way; the kernel reads the
 * tick count under a spin lock (kernel/timeout.c, v4.5.0), which an
 * interrupt handler may take. Not run from one here (PORT.md).
 */
#ifndef ELEMENTAL_TIME_ZEPHYR_H
#define ELEMENTAL_TIME_ZEPHYR_H

#include <elemental/time.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The kernel's uptime, in its ticks. */
el_clock el_zephyr_clock(void);

#ifdef __cplusplus
}
#endif

#endif /* ELEMENTAL_TIME_ZEPHYR_H */
