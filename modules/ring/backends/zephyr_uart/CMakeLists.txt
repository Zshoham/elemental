elemental_add_module(ring_zephyr_uart
  SOURCES src/ring_zephyr_uart.c
  DEPENDS ring)

# Zephyr's headers, and the generated configuration they need. Kconfig
# offers RING_ZEPHYR_UART only in a Zephyr build with interrupt-driven
# UARTs, where this target exists. Zephyr's headers are GNU C, so they are
# system headers here: Elemental's -Wpedantic stays on its own code (as
# modules/time/backends/zephyr does). For the build only: a static
# library's private link is still in its exported interface, and
# zephyr_interface is in no export set; the application that links the
# installed library is the Zephyr build that has it.
target_link_libraries(elemental_ring_zephyr_uart PRIVATE $<BUILD_INTERFACE:zephyr_interface>)
get_target_property(zephyr_includes zephyr_interface INTERFACE_INCLUDE_DIRECTORIES)
target_include_directories(elemental_ring_zephyr_uart SYSTEM PRIVATE ${zephyr_includes})
add_dependencies(elemental_ring_zephyr_uart zephyr_generated_headers)

if(COMMAND elemental_add_test)
  # What only this backend promises, against devices made by hand: the
  # failures of setting up and what they leave of the caller's ring, the
  # handler's counting, and closing the ring at deinit. They need no wire,
  # so they run in any image whose configuration has RING_ZEPHYR_UART.
  elemental_add_test(ring_zephyr_uart.platform
    SOURCES tests/ring_zephyr_uart_test.cpp tests/fake_devices.c
    LIBRARIES elemental::ring_zephyr_uart)

  # The tests below need bytes to arrive on a UART: two of the board's
  # UARTs joined by the emulator, which the test image sets up when asked to
  # (tests/zephyr/CMakeLists.txt, ELEMENTAL_ZEPHYR_UART_WIRE). On
  # mps2/an385 under QEMU only; the scenario is elemental.tests.ring.uart.
  # They send with io_zephyr_uart's polled writer, which io's directory
  # defined before this one (io is added before ring).
  if(ELEMENTAL_ZEPHYR_UART_WIRE AND EL_CONFIG_IO_ZEPHYR_UART)
    # Interface semantics: the io contract suite, against both ends of the
    # wire: the polled writer, and the reader of the ring this fills.
    elemental_add_test(ring_zephyr_uart.contract
      SOURCES tests/contract_zephyr_uart_test.cpp tests/fixture_uart.c tests/wire.c
      LIBRARIES elemental::ring_zephyr_uart elemental::io_zephyr_uart elemental::io_contract)

    # What only these backends promise, in a real receive interrupt.
    elemental_add_test(ring_zephyr_uart.wire
      SOURCES tests/ring_zephyr_uart_wire_test.cpp tests/wire.c
      LIBRARIES elemental::ring_zephyr_uart elemental::io_zephyr_uart)
  endif()
endif()
