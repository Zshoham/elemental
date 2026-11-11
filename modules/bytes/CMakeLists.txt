elemental_add_module(bytes
  SOURCES src/bytes.c
  DEPENDS status)

if(COMMAND elemental_add_test)
  add_subdirectory(tests)
endif()
