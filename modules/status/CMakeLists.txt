elemental_add_module(status
  SOURCES src/status.c)

if(ELEMENTAL_BUILD_TESTS AND NOT CMAKE_CROSSCOMPILING)
  elemental_add_test(status.unit
    SOURCES tests/status_test.cpp
    LIBRARIES elemental::status)
endif()
