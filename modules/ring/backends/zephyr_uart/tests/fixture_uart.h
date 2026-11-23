#ifndef RING_ZEPHYR_UART_FIXTURE_UART_H
#define RING_ZEPHYR_UART_FIXTURE_UART_H

#include <elemental/io_contract.h>

#ifdef __cplusplus
extern "C" {
#endif

/* A UART's two ends under the io contract suite: the writer of
 * elemental/io_zephyr_uart.h, and the reader of a ring that
 * elemental/ring_zephyr_uart.h fills. */
extern const el_io_contract_fixture zephyr_uart_fixture;

#ifdef __cplusplus
}
#endif

#endif /* RING_ZEPHYR_UART_FIXTURE_UART_H */
