elemental_add_module(io_posix
  SOURCES src/io_posix.c
  DEPENDS io)

if(COMMAND elemental_add_test)
  # Interface semantics: the shared contract suite, against this backend.
  elemental_add_test(io_posix.contract
    SOURCES tests/contract_posix_test.cpp tests/fixture_pipe.c
    LIBRARIES elemental::io_posix elemental::io_contract)

  # Platform behavior: what only this backend promises.
  elemental_add_test(io_posix.platform
    SOURCES tests/io_posix_test.cpp
    LIBRARIES elemental::io_posix)
endif()
