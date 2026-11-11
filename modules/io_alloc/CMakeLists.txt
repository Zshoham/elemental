elemental_add_module(io_alloc
  SOURCES src/io_alloc.c
  DEPENDS io alloc)

if(COMMAND elemental_add_test)
  add_subdirectory(tests)
endif()
