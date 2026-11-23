/*
 * isr_tick - an example: an interrupt handler that logs.
 *
 * The UART's receive handler is the backend's
 * (elemental/ring_zephyr_uart.h), and Elemental's own code never logs, so
 * what the application does in an interrupt handler is its own. This is
 * such a handler: a k_timer's expiry function, which Zephyr runs in
 * interrupt context. It logs through a logger the application gave it,
 * whose sink must not wait and whose clock must be readable from an
 * interrupt: a deferred sink (elemental/log_deferred.h) and Zephyr's
 * clock. A thread drains the sink into the sink that writes.
 *
 * Not part of Elemental's API: an example, named without the el_ prefix.
 */
#ifndef ISR_TICK_H
#define ISR_TICK_H

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/kernel.h>

#include <elemental/log.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct isr_tick {
    struct k_timer timer;
    struct k_sem done;    /* given by the handler after the last tick */
    const el_logger *log; /* the handler's */
    uint32_t count;       /* ticks so far; the handler's */
    uint32_t limit;
} isr_tick;

/* Log "tick n of limit" in the timer's interrupt every `period_ms`, `limit` times. */
void isr_tick_start(isr_tick *tick, const el_logger *log, uint32_t limit, uint32_t period_ms);

/* Wait until the last tick has been logged, or for `timeout`. False if the time ran out. */
bool isr_tick_wait(isr_tick *tick, k_timeout_t timeout);

/* Stop the timer, if it is running. */
void isr_tick_stop(isr_tick *tick);

#ifdef __cplusplus
}
#endif

#endif /* ISR_TICK_H */
