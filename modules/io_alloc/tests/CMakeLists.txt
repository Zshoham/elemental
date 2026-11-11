# Interface semantics: the io contract suite, against the grow writer.
elemental_add_test(io_alloc.contract
  SOURCES contract_grow_test.cpp fixture_grow.c
  LIBRARIES elemental::io_alloc elemental::io_contract elemental::alloc_contract)

# What only the grow writer promises: growth, the capacity status, and that
# it frees exactly what it allocated.
elemental_add_test(io_alloc.unit
  SOURCES io_alloc_test.cpp
  LIBRARIES elemental::io_alloc elemental::alloc_contract)

if(EL_CONFIG_ASSERT AND ELEMENTAL_DEATH_TESTS)
  elemental_add_test(io_alloc.violations
    SOURCES io_alloc_violations_test.cpp
    LIBRARIES elemental::io_alloc)
endif()
