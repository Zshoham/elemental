# Examples built as part of Elemental's own build. The consume_* directories
# are separate projects: they show Elemental from the outside and are built
# by the integration tests (tests/integration).
if(EL_CONFIG_ALLOC)
  add_subdirectory(backend_size_classes)
endif()
if(EL_CONFIG_TIME)
  add_subdirectory(backend_step_clock)
endif()
if(EL_CONFIG_LOG)
  add_subdirectory(backend_log_tee)
endif()
# Zephyr only, and only with a UART driver that is interrupt-driven: the
# receiver (RING_ZEPHYR_UART) is offered only in such a build.
if(EL_CONFIG_RING_ZEPHYR_UART AND EL_CONFIG_IO_ZEPHYR_UART AND EL_CONFIG_LOG_DEFERRED
   AND EL_CONFIG_TIME_ZEPHYR)
  add_subdirectory(uart_ring)
endif()
