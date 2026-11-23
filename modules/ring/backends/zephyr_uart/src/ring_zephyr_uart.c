#include "elemental_config.h"

#include <elemental/assert.h>
#include <elemental/ring_zephyr_uart.h>

#include <errno.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>

/*
 * The receive interrupt: the ring's producer. It runs to the end of the
 * FIFO, then tells the application once. Bytes the ring has no room for are
 * counted: the one writer of `dropped` does a load and a store, not an
 * atomic increment, which a Cortex-M0 does not have (docs/adr/0023).
 */
static void irq_handler(const struct device *dev, void *user_data)
{
    el_zephyr_uart_irq *const uart = user_data;
    size_t lost = 0;
    int took = 0;

    (void)uart_irq_update(dev);
    while (uart_irq_rx_ready(dev) > 0) {
        /* Once ready is seen, read until the FIFO gives less than asked: for
         * some drivers it is edge-triggered (uart_fifo_read()). */
        uint8_t chunk[16];
        int got;
        do {
            got = uart_fifo_read(dev, chunk, (int)sizeof(chunk));
            if (got > 0) {
                took = 1;
                lost += (size_t)got - el_ring_write(uart->ring, chunk, (size_t)got);
            }
        } while (got == (int)sizeof(chunk));
        if (got <= 0) {
            break;
        }
    }

    if (lost > 0) {
        const size_t before = atomic_load_explicit(&uart->dropped, memory_order_relaxed);
        atomic_store_explicit(&uart->dropped, before + lost, memory_order_release);
    }
    if (took && uart->on_arrival != NULL) {
        uart->on_arrival(uart->on_arrival_ctx);
    }
}

el_status el_zephyr_uart_irq_init(el_zephyr_uart_irq *uart, const struct device *dev, el_ring *ring,
                                  void (*on_arrival)(void *ctx), void *ctx)
{
    EL_ASSERT(uart != NULL);
    EL_ASSERT(ring != NULL);

    uart->dev = dev;
    uart->ring = ring;
    uart->on_arrival = on_arrival;
    uart->on_arrival_ctx = ctx;
    atomic_init(&uart->dropped, 0);

    EL_ENSURE(device_is_ready(dev), EL_ERR_IO);

    /* No receive interrupt while this sets up, whatever the device did before. */
    uart_irq_rx_disable(dev);

    /* Bytes the device received before anyone listened are not this
     * receiver's. -1 is empty; so is any other answer but these two, as
     * some drivers report empty (-ENODATA, -EPERM). An asynchronous receive
     * owns the input (-EBUSY): nothing to receive here. A driver without
     * poll_in (-ENOSYS) cannot be drained, and what it held is received. */
    unsigned char stale;
    int polled;
    while ((polled = uart_poll_in(dev, &stale)) == 0) {
    }
    EL_ENSURE(polled != -EBUSY, EL_ERR_IO);

    /* The ring is the caller's, already open: the driver may run the
     * callback as soon as it is set, from an interrupt other than the
     * receive one (a transmission completing), and find a byte ready. If
     * setting it fails it was not set (Zephyr's call reports an error only
     * before setting it), so the handler never ran and the ring is as the
     * caller left it. */
    EL_ENSURE(uart_irq_callback_user_data_set(dev, irq_handler, uart) >= 0, EL_ERR_IO);
    uart_irq_rx_enable(dev);
    return EL_OK;
}

void el_zephyr_uart_irq_deinit(el_zephyr_uart_irq *uart)
{
    EL_ASSERT(uart != NULL);

    /* Once the callback is cleared the handler cannot start (on one CPU,
     * with a driver that runs it from the device's interrupt:
     * elemental/ring_zephyr_uart.h). Until then another of the device's
     * interrupts may still run it; what it stores is in the ring for the
     * reader, which is closed only after, by this thread, the producer
     * again. */
    uart_irq_rx_disable(uart->dev);
    (void)uart_irq_callback_user_data_set(uart->dev, NULL, NULL);
    el_ring_close(uart->ring);
}

uint32_t el_zephyr_uart_irq_dropped(const el_zephyr_uart_irq *uart)
{
    EL_ASSERT(uart != NULL);

    return (uint32_t)atomic_load_explicit(&uart->dropped, memory_order_acquire);
}
