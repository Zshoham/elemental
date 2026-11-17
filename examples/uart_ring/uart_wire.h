/*
 * The test's side of the UART example: the two UARTs the emulator joins,
 * and a clock that notes where it is read from.
 */
#ifndef UART_WIRE_H
#define UART_WIRE_H

#include <stddef.h>
#include <stdint.h>

#include <zephyr/device.h>

#include <elemental/time.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The UART that receives what uart_wire_send() sends, or null if either end
 * is not ready. On mps2/an385 under QEMU, with the test image's
 * ELEMENTAL_ZEPHYR_UART_WIRE, UART1's output is UART2's input
 * (tests/zephyr/CMakeLists.txt).
 */
const struct device *uart_wire_receiver(void);

/* Send `len` bytes out of the other end, one at a time, waiting for the
 * device to take each. */
void uart_wire_send(const void *data, size_t len);

/*
 * Zephyr's clock (elemental/time_zephyr.h), counting its readings by where
 * they were made. The counts are written wherever the clock is read: read
 * them once nothing reads the clock any more.
 */
typedef struct context_clock {
    uint32_t from_interrupt;
    uint32_t from_thread;
} context_clock;

void context_clock_init(context_clock *clock);
el_clock context_clock_as_clock(context_clock *clock);

#ifdef __cplusplus
}
#endif

#endif /* UART_WIRE_H */
