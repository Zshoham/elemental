elemental_add_module(alloc
  SOURCES src/alloc.c src/alloc_arena.c
  DEPENDS status)

add_subdirectory(contract)

if(EL_CONFIG_ALLOC_LIBC)
  add_subdirectory(backends/libc)
endif()

if(COMMAND elemental_add_test)
  add_subdirectory(tests)
endif()
