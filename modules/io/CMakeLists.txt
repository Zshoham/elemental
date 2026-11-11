elemental_add_module(io
  SOURCES src/io.c src/io_mem.c
  DEPENDS status)

add_subdirectory(contract)

if(EL_CONFIG_IO_POSIX)
  add_subdirectory(backends/posix)
endif()

if(COMMAND elemental_add_test)
  add_subdirectory(tests)
endif()
