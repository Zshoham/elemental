elemental_add_module(time_posix
  SOURCES src/time_posix.c
  DEPENDS time)

if(COMMAND elemental_add_test)
  # Interface semantics: the shared contract suite, against this backend.
  elemental_add_test(time_posix.contract
    SOURCES tests/contract_posix_test.cpp tests/fixture_posix.c
    LIBRARIES elemental::time_posix elemental::time_contract)

  # Platform behavior: what only this backend promises.
  elemental_add_test(time_posix.platform
    SOURCES tests/time_posix_test.cpp
    LIBRARIES elemental::time_posix)
endif()
