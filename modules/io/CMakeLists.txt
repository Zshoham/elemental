# The grow writer is part of io when IO_ALLOC is on. It is the only thing in
# io that needs alloc, so its source and the dependency come and go together.
set(io_sources src/io.c src/io_mem.c)
set(io_dependencies status)
set(io_omitted_headers "")
if(EL_CONFIG_IO_ALLOC)
  list(APPEND io_sources src/io_alloc.c)
  list(APPEND io_dependencies alloc)
else()
  list(APPEND io_omitted_headers elemental/io_alloc.h)
endif()

elemental_add_module(io
  SOURCES ${io_sources}
  DEPENDS ${io_dependencies}
  OMIT_HEADERS ${io_omitted_headers})

add_subdirectory(contract)

if(EL_CONFIG_IO_POSIX)
  add_subdirectory(backends/posix)
endif()

if(COMMAND elemental_add_test)
  add_subdirectory(tests)
endif()
