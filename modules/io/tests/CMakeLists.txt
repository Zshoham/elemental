# Interface semantics: the shared contract suite, against the memory streams.
elemental_add_test(io.contract.mem
  SOURCES contract_mem_test.cpp fixture_mem.c
  LIBRARIES elemental::io_contract)

# The helpers, driven by a scripted stream so every interleaving of progress,
# would-block, and failure is reproducible.
elemental_add_test(io.helpers
  SOURCES io_helpers_test.cpp
  LIBRARIES elemental::io)

# Contract checks fire, in whichever way the configuration makes them fire.
# With assertions compiled out there is nothing to observe.
if(EL_CONFIG_ASSERT)
  elemental_add_test(io.violations
    SOURCES io_violations_test.cpp
    LIBRARIES elemental::io)
endif()
