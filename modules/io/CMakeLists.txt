elemental_add_module(io
  SOURCES src/io.c src/io_mem.c
  DEPENDS status)

add_subdirectory(contract)

if(EL_CONFIG_IO_POSIX)
  add_subdirectory(backends/posix)
endif()

if(ELEMENTAL_BUILD_TESTS AND NOT CMAKE_CROSSCOMPILING)
  add_subdirectory(tests)
endif()
