#include "isr_tick.h"

#include <elemental/log_printf.h>

/* In the timer's interrupt: the log call does not wait, because the sink the
 * application gave the logger does not. */
static void isr_tick_expired(struct k_timer *timer)
{
    isr_tick *const tick = CONTAINER_OF(timer, isr_tick, timer);

    tick->count++;
    EL_LOG_INFO(tick->log, "tick %u of %u", (unsigned)tick->count, (unsigned)tick->limit);
    if (tick->count >= tick->limit) {
        k_timer_stop(timer);
        k_sem_give(&tick->done);
    }
}

void isr_tick_start(isr_tick *tick, const el_logger *log, uint32_t limit, uint32_t period_ms)
{
    tick->log = log;
    tick->count = 0;
    tick->limit = limit;
    k_sem_init(&tick->done, 0, 1);
    k_timer_init(&tick->timer, isr_tick_expired, NULL);
    k_timer_start(&tick->timer, K_MSEC(period_ms), K_MSEC(period_ms));
}

bool isr_tick_wait(isr_tick *tick, k_timeout_t timeout)
{
    return k_sem_take(&tick->done, timeout) == 0;
}

void isr_tick_stop(isr_tick *tick)
{
    k_timer_stop(&tick->timer);
}
