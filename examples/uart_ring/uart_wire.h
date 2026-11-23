/*
 * The test's side of the UART example: the two UARTs the emulator joins,
 * and a clock that notes where it is read from.
 */
#ifndef UART_WIRE_H
#define UART_WIRE_H

#include <stdint.h>

#include <zephyr/device.h>

#include <elemental/time.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The two ends of the wire: what is sent out of the first arrives at the
 * second. On mps2/an385 under QEMU, with the test image's
 * ELEMENTAL_ZEPHYR_UART_WIRE, UART1's output is UART2's input
 * (tests/zephyr/CMakeLists.txt). Not checked for readiness: the backend's
 * init does that.
 */
const struct device *uart_wire_sender(void);
const struct device *uart_wire_receiver(void);

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
