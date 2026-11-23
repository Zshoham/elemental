/* The io contract suite against a UART's two ends, over the emulator's
 * wire: io_zephyr_uart's polled sender, and the reader of a ring that
 * ring_zephyr_uart's receiver fills. */
#include <elemental/io_contract.hpp>

#include "fixture_uart.h"

INSTANTIATE_TEST_SUITE_P(ZephyrUart, ElIoContract, testing::Values(&zephyr_uart_fixture));
