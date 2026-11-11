elemental_add_module(alloc_libc
  SOURCES src/alloc_libc.c
  DEPENDS alloc)

if(COMMAND elemental_add_test)
  # Interface semantics: the shared contract suite, against this backend.
  elemental_add_test(alloc_libc.contract
    SOURCES tests/contract_libc_test.cpp tests/fixture_libc.c
    LIBRARIES elemental::alloc_libc elemental::alloc_contract)

  # Platform behavior: what only this backend promises.
  elemental_add_test(alloc_libc.platform
    SOURCES tests/alloc_libc_test.cpp
    LIBRARIES elemental::alloc_libc)
endif()
