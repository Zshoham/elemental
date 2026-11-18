#include "uart_reader.h"

#include <zephyr/drivers/uart.h>
#include <zephyr/irq.h>

#include <elemental/log_printf.h>

/*
 * The receive interrupt: the ring's producer. Everything it calls is
 * ISR-safe: el_ring_write() under the two-sides rule, the log call because
 * the application gave the logger a sink that does not wait, k_sem_give()
 * by Zephyr's documentation.
 */
static void uart_reader_isr(const struct device *dev, void *user_data)
{
    uart_reader *const rx = user_data;
    rx->counts.interrupts++;
    uart_irq_update(dev);
    while (uart_irq_rx_ready(dev) > 0) {
        uint8_t bytes[8];
        const int got = uart_fifo_read(dev, bytes, sizeof(bytes));
        if (got <= 0) {
            break;
        }
        rx->counts.bytes += (uint32_t)got;
        /* What does not fit is dropped: the handler cannot wait for the
         * thread. */
        const size_t kept = el_ring_write(&rx->ring, bytes, (size_t)got);
        if (kept < (size_t)got) {
            rx->counts.dropped += (uint32_t)((size_t)got - kept);
            EL_LOG_WARN(rx->log, "%u bytes dropped, the ring is full",
                        (unsigned)((size_t)got - kept));
        }
        if (bytes[got - 1] == '\n') {
            EL_LOG_INFO(rx->log, "end of line, interrupt %u", (unsigned)rx->counts.interrupts);
        }
    }
    /* The ring wakes nobody: what the thread waits on is the platform's. */
    k_sem_give(&rx->ready);
}

void uart_reader_start(uart_reader *rx, const struct device *dev, void *storage, size_t cap,
                       const el_logger *log)
{
    rx->dev = dev;
    rx->log = log;
    rx->counts = (uart_reader_counts){0};
    /* Set up before the handler can see it. */
    el_ring_init(&rx->ring, storage, cap);
    k_sem_init(&rx->ready, 0, 1);

    /* Bytes the device received before anyone listened are not this
     * reader's (uart_reader.h). */
    unsigned char stale;
    while (uart_poll_in(dev, &stale) == 0) {
    }
    uart_irq_callback_user_data_set(dev, uart_reader_isr, rx);
    uart_irq_rx_enable(dev);
}

void uart_reader_stop(uart_reader *rx)
{
    uart_irq_rx_disable(rx->dev);
    uart_irq_callback_user_data_set(rx->dev, NULL, NULL);
}

el_reader uart_reader_as_reader(uart_reader *rx)
{
    return el_ring_as_reader(&rx->ring);
}

bool uart_reader_wait(uart_reader *rx, k_timeout_t timeout)
{
    return k_sem_take(&rx->ready, timeout) == 0;
}

void uart_reader_counts_get(uart_reader *rx, uart_reader_counts *counts)
{
    /* The handler writes them, several words: masked, not atomic. */
    const unsigned int key = irq_lock();
    *counts = rx->counts;
    irq_unlock(key);
}
