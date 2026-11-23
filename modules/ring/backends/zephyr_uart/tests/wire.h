/*
 * The two UARTs the emulator joins for the zephyr_uart tests: what the image
 * sends on one arrives on the other (tests/zephyr/CMakeLists.txt,
 * ELEMENTAL_ZEPHYR_UART_WIRE). On mps2/an385 under QEMU: UART1 sends,
 * UART2 receives.
 */
#ifndef RING_ZEPHYR_UART_TEST_WIRE_H
#define RING_ZEPHYR_UART_TEST_WIRE_H

#include <zephyr/device.h>

#ifdef __cplusplus
extern "C" {
#endif

/* UART1: the end the tests send from. Not checked for readiness. */
const struct device *zephyr_wire_sender(void);

/* UART2: the end the tests receive on. Not checked for readiness. */
const struct device *zephyr_wire_receiver(void);

/*
 * Take one byte from the receiving end by polling, as if nobody else
 * listened, waiting up to `timeout_ms` for it: 1 and `*byte` if one came,
 * else 0.
 */
int zephyr_wire_poll_receive(unsigned char *byte, int timeout_ms);

/*
 * Take what the receiving end holds, and what is still on its way, until
 * nothing has come for 50 ms: a test starts from an empty wire, whatever the
 * one before left.
 */
void zephyr_wire_flush(void);

#ifdef __cplusplus
}
#endif

#endif /* RING_ZEPHYR_UART_TEST_WIRE_H */
