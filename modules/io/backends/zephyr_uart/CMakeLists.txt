elemental_add_module(io_zephyr_uart
  SOURCES src/io_zephyr_uart.c
  DEPENDS io)

# Zephyr's headers, and the generated configuration they need. Kconfig
# offers IO_ZEPHYR_UART only in a Zephyr build with a serial subsystem,
# where this target exists. Zephyr's headers are GNU C, so they are system
# headers here: Elemental's -Wpedantic stays on its own code (as
# modules/time/backends/zephyr does). For the build only: a static
# library's private link is still in its exported interface, and
# zephyr_interface is in no export set; the application that links the
# installed library is the Zephyr build that has it.
target_link_libraries(elemental_io_zephyr_uart PRIVATE $<BUILD_INTERFACE:zephyr_interface>)
get_target_property(zephyr_includes zephyr_interface INTERFACE_INCLUDE_DIRECTORIES)
target_include_directories(elemental_io_zephyr_uart SYSTEM PRIVATE ${zephyr_includes})
add_dependencies(elemental_io_zephyr_uart zephyr_generated_headers)

if(COMMAND elemental_add_test)
  # What only this backend promises, against devices made by hand: a device
  # that is not ready, and every byte passed to poll_out in order. They
  # need no wire, so they run in any image whose configuration has
  # IO_ZEPHYR_UART. The writer over a real UART, and the io contract suite
  # against it, are with the receiver's tests
  # (modules/ring/backends/zephyr_uart): they need both ends of the wire.
  elemental_add_test(io_zephyr_uart.platform
    SOURCES tests/io_zephyr_uart_test.cpp tests/fake_poll_uart.c
    LIBRARIES elemental::io_zephyr_uart)
endif()
