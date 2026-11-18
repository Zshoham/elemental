# A UART's receive side as an el_reader, on Zephyr: the receive interrupt
# fills an el_ring and logs through a deferred sink. Built only in a Zephyr
# build whose UART driver is interrupt-driven (examples/CMakeLists.txt).
add_library(example_uart_reader STATIC uart_reader.c)
target_include_directories(example_uart_reader PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(example_uart_reader PUBLIC elemental::ring elemental::log)
elemental_target_c_standard(example_uart_reader)
elemental_target_warnings(example_uart_reader)

# Zephyr's headers, and the generated configuration they need, as
# modules/time/backends/zephyr takes them: system headers, so Elemental's
# -Wpedantic stays on the example's own code.
target_link_libraries(example_uart_reader PRIVATE zephyr_interface)
get_target_property(zephyr_includes zephyr_interface INTERFACE_INCLUDE_DIRECTORIES)
target_include_directories(example_uart_reader SYSTEM PRIVATE ${zephyr_includes})
add_dependencies(example_uart_reader zephyr_generated_headers)

# The test needs bytes to arrive: two of the board's UARTs joined by the
# emulator, which the test image sets up when asked to
# (tests/zephyr/CMakeLists.txt, ELEMENTAL_ZEPHYR_UART_WIRE). On mps2/an385
# under QEMU only.
if(COMMAND elemental_add_test AND ELEMENTAL_ZEPHYR_UART_WIRE)
  elemental_add_test(example.uart_ring
    SOURCES uart_ring_test.cpp uart_wire.c
    LIBRARIES example_uart_reader elemental::log elemental::time_zephyr)
endif()
