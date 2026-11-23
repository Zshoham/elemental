/*
 * Devices that are not the emulator's UARTs, for what the wire cannot
 * stage: a device that is not ready, one whose driver has no
 * interrupt-driven receive, and a scripted one that holds the bytes the
 * test gives it, records how the backend drives it, and takes an interrupt
 * when the test says. They are Zephyr's `struct device`, made by hand.
 */
#ifndef RING_ZEPHYR_UART_FAKE_UART_H
#define RING_ZEPHYR_UART_FAKE_UART_H

#include <stddef.h>
#include <stdint.h>

#include <elemental/ring_zephyr_uart.h>

#include <zephyr/device.h>

#ifdef __cplusplus
extern "C" {
#endif

/* device_is_ready() is false. */
const struct device *fake_uart_not_ready(void);

/* Ready, and its driver has poll_in but no interrupt callback. */
const struct device *fake_uart_without_irq(void);

/* The scripted device: ready, with an interrupt-driven receive. */
const struct device *fake_uart(void);

/* The scripted device again, through a driver with no poll_in: uart_poll_in()
 * reports -ENOSYS. */
const struct device *fake_uart_without_poll_in(void);

/* Forget everything about the scripted device: no bytes, no callback, no events. */
void fake_uart_reset(void);

/* The device holds `len` bytes (at most 64) that nothing has read yet. */
void fake_uart_hold(const void *bytes, size_t len);

/*
 * When a callback is next set, the device receives `len` bytes (at most 64)
 * and runs the callback just set, from an interrupt that is not the receive
 * one (a transmission completing), so whether the receive interrupt is
 * enabled does not matter.
 */
void fake_uart_arrive_as_callback_is_set(const void *bytes, size_t len);

/* From now on poll_in reports -EBUSY, as when an asynchronous receive
 * (uart_rx_enable()) is active, and reads nothing. */
void fake_uart_poll_in_busy(void);

/* Bytes held and not yet read, by poll_in or the receive FIFO. */
size_t fake_uart_held(void);

/*
 * From now on the device reports a byte ready once per interrupt, as a
 * driver whose "RX ready" is edge-triggered does (uart_fifo_read() says to
 * read until it returns less than asked): only reading until the FIFO is
 * empty gets every byte.
 */
void fake_uart_edge_triggered(void);

/*
 * The receive interrupt: calls the callback, if the receive interrupt is
 * enabled and one is set. In each run the device's status is what
 * uart_irq_update() latched in that run, as on drivers that cache it there:
 * "RX ready" is false until the handler has called it.
 */
void fake_uart_interrupt(void);

/*
 * Runs of the callback that did not call uart_irq_update() exactly once,
 * and uart_irq_rx_ready() calls made before it or outside a run, since the
 * reset. Zephyr asks for the update once, before the status is tested.
 */
unsigned fake_uart_misuses(void);

/* Whether a callback is set, and whether the receive interrupt is enabled. */
int fake_uart_callback_is_set(void);
int fake_uart_rx_is_enabled(void);

/*
 * What the driver was asked, in order, one letter each, as a string:
 * 'p' a byte read by poll_in, 'c' a callback set, 'x' the callback cleared,
 * 'e' receive interrupt enabled, 'd' receive interrupt disabled, 'i' the
 * callback run from an interrupt.
 */
const char *fake_uart_events(void);

/*
 * Set the receiver's drop count, as its handler would have left it, with a
 * relaxed store: C++ does not touch the atomic word (docs/adr/0023). Only
 * while no interrupt can run the handler.
 */
void fake_uart_seed_dropped(el_zephyr_uart_irq *uart, size_t count);

#ifdef __cplusplus
}
#endif

#endif /* RING_ZEPHYR_UART_FAKE_UART_H */
