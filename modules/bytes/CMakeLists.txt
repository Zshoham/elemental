elemental_add_module(bytes
  SOURCES src/bytes.c
  DEPENDS status)

if(ELEMENTAL_BUILD_TESTS AND NOT CMAKE_CROSSCOMPILING)
  add_subdirectory(tests)
endif()
