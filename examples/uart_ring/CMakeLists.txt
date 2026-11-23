# The UART example on a target: a UART's receive interrupt filling a ring
# the example owns (elemental::ring_zephyr_uart), read through the ring's
# el_reader, a polled writer that sends (elemental::io_zephyr_uart), and an
# interrupt handler of the application's own that logs through a deferred
# sink. Built only in a Zephyr build whose UART driver is interrupt-driven
# (examples/CMakeLists.txt).
#
# The test needs bytes to arrive: two of the board's UARTs joined by the
# emulator, which the test image sets up when asked to
# (tests/zephyr/CMakeLists.txt, ELEMENTAL_ZEPHYR_UART_WIRE). On mps2/an385
# under QEMU only. The C files take Zephyr's headers from the image they are
# compiled into.
if(COMMAND elemental_add_test AND ELEMENTAL_ZEPHYR_UART_WIRE)
  elemental_add_test(example.uart_ring
    SOURCES uart_ring_test.cpp uart_wire.c isr_tick.c
    LIBRARIES elemental::ring_zephyr_uart elemental::io_zephyr_uart elemental::log
              elemental::time_zephyr)
endif()
