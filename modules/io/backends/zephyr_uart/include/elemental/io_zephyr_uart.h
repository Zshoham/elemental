/*
 * elemental/io_zephyr_uart.h - a Zephyr UART as a writer.
 *
 * STATUS: experimental. See docs/process/api-stability.md, and PORT.md next
 * to this backend for what it does and does not support.
 *
 *   el_zephyr_uart_poll   a writer that passes each byte to uart_poll_out()
 *
 * The receive side of a UART is not here: a receive interrupt fills an
 * el_ring, which is the reader (elemental/ring_zephyr_uart.h).
 *
 * This header includes none of Zephyr's: `struct device` is declared and
 * used by pointer only, as time_zephyr.h uses none of Zephyr's types. The
 * application includes <zephyr/device.h> for DEVICE_DT_GET().
 *
 * Ownership: the caller owns the device, and keeps it alive and
 * initialized while a writer made from it is in use. This backend never
 * configures it (speed, parity), and uses only uart_poll_out().
 *
 * Blocking: the writer waits, as uart_poll_out() does. Nothing here
 * allocates, starts a thread, or takes a lock.
 *
 * Configuration: IO_ZEPHYR_UART, which needs a Zephyr build with
 * CONFIG_SERIAL. In such a build the writer needs only a ready device with
 * uart_poll_out(); interrupt-driven mode is not used.
 *
 * No layout in this header depends on Zephyr's configuration.
 */
#ifndef ELEMENTAL_IO_ZEPHYR_UART_H
#define ELEMENTAL_IO_ZEPHYR_UART_H

#include <elemental/io.h>

#ifdef __cplusplus
extern "C" {
#endif

struct device; /* Zephyr's, <zephyr/device.h>: used by pointer only */

/*
 * Polled. A writer whose every write passes each byte, in order, to
 * uart_poll_out(), and returns when the last call has: `*n` is always
 * `len`. That is what it promises. uart_poll_out() reports nothing, so
 * whether the device kept a byte is the driver's: the write is lossless
 * only where the driver's poll_out waits until the device accepts the
 * byte, as drivers of a hardware transmitter do (on most, a busy loop). A
 * driver that discards instead does so unseen: Zephyr's RTT UART
 * (uart_rtt) skips a byte its full buffer has no room for. Never
 * EL_ERR_WOULD_BLOCK, EL_ERR_NO_SPACE, EL_ERR_IO or EL_ERR_END_OF_STREAM.
 * Not for interrupt handlers: it waits as the driver does. It may share a
 * device with a receiver (elemental/ring_zephyr_uart.h).
 *
 * A sender may be used from one thread at a time. The object may be moved
 * while no writer made from it is in use.
 */
typedef struct el_zephyr_uart_poll {
    const struct device *dev;
} el_zephyr_uart_poll;

/*
 * EL_OK, or EL_ERR_IO: the device is not ready (device_is_ready(), which
 * reports a null `dev`, as DEVICE_DT_GET_ANY() gives when there is none, as
 * not ready too). `uart` must not be null.
 */
EL_NODISCARD el_status el_zephyr_uart_poll_init(el_zephyr_uart_poll *uart,
                                                const struct device *dev);

/* The writer for `uart`, valid while `uart` is. */
el_writer el_zephyr_uart_poll_as_writer(el_zephyr_uart_poll *uart);

#ifdef __cplusplus
}
#endif

#endif /* ELEMENTAL_IO_ZEPHYR_UART_H */
