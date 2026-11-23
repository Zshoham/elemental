# Each source is its own object, so a program links only what it calls: the
# ring, and its two sides as streams.
elemental_add_module(ring
  SOURCES src/ring.c src/ring_io.c
  DEPENDS status io)

if(EL_CONFIG_RING_ZEPHYR_UART)
  add_subdirectory(backends/zephyr_uart)
endif()

if(COMMAND elemental_add_test)
  add_subdirectory(tests)
endif()
