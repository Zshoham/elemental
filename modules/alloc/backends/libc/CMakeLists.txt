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

  # What the backend asks of the C library, seen by wrapping malloc() and
  # realloc() at link time (GNU ld and lld; the backend is a static
  # library, so its calls are wrapped too).
  if(NOT ELEMENTAL_TEST_IMAGE AND CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    elemental_add_test(alloc_libc.platform.requests
      SOURCES tests/alloc_libc_requests_test.cpp tests/heap_requests.c
      LIBRARIES elemental::alloc_libc)
    target_link_options(test_alloc_libc_platform_requests PRIVATE
      "LINKER:--wrap=malloc" "LINKER:--wrap=realloc")
  endif()
endif()
