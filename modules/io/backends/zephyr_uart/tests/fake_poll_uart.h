/*
 * Devices that are not a board's UARTs, for the polled writer's tests: one
 * that is not ready, and one whose driver records every byte passed to its
 * poll_out. They are Zephyr's `struct device`, made by hand.
 */
#ifndef IO_ZEPHYR_UART_FAKE_POLL_UART_H
#define IO_ZEPHYR_UART_FAKE_POLL_UART_H

#include <stddef.h>
#include <stdint.h>

#include <zephyr/device.h>

#ifdef __cplusplus
extern "C" {
#endif

/* device_is_ready() is false. */
const struct device *fake_poll_uart_not_ready(void);

/* Ready, with a poll_out that records each byte. */
const struct device *fake_poll_uart(void);

/* Forget the bytes recorded. */
void fake_poll_uart_reset(void);

/* The bytes passed to poll_out since the reset, in order (at most 64 are
 * kept), and how many. */
const uint8_t *fake_poll_uart_sent(void);
size_t fake_poll_uart_sent_count(void);

#ifdef __cplusplus
}
#endif

#endif /* IO_ZEPHYR_UART_FAKE_POLL_UART_H */
