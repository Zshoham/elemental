elemental_add_module(status
  SOURCES src/status.c)

if(COMMAND elemental_add_test)
  elemental_add_test(status.unit
    SOURCES tests/status_test.cpp
    LIBRARIES elemental::status)
endif()
