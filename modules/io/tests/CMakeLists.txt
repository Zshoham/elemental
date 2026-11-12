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
# With assertions compiled out there is nothing to observe, and without a
# process to fork (on a target) there is no death test.
if(EL_CONFIG_ASSERT AND ELEMENTAL_DEATH_TESTS)
  elemental_add_test(io.violations
    SOURCES io_violations_test.cpp
    LIBRARIES elemental::io)
endif()

if(EL_CONFIG_IO_ALLOC)
  # Interface semantics: the io contract suite, against the grow writer.
  elemental_add_test(io.contract.grow
    SOURCES contract_grow_test.cpp fixture_grow.c
    LIBRARIES elemental::io_contract elemental::alloc_contract)

  # What only the grow writer promises: growth, the capacity status, and that
  # it frees exactly what it allocated.
  elemental_add_test(io.unit.grow
    SOURCES io_alloc_test.cpp
    LIBRARIES elemental::io elemental::alloc_contract)

  if(EL_CONFIG_ASSERT AND ELEMENTAL_DEATH_TESTS)
    elemental_add_test(io.violations.grow
      SOURCES io_alloc_violations_test.cpp
      LIBRARIES elemental::io)
  endif()
endif()
