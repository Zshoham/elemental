# Interface semantics: the shared contract suite, against both arenas.
elemental_add_test(alloc.contract.arena
  SOURCES contract_arena_test.cpp fixture_arena.c
  LIBRARIES elemental::alloc_contract)

# The front functions and the arenas: placement, padding, what each free does.
elemental_add_test(alloc.unit
  SOURCES alloc_unit_test.cpp layout_probe.c
  LIBRARIES elemental::alloc)

# Contract checks fire, in whichever way the configuration makes them fire,
# and the checking allocator stops the program on a mismatched free. Death
# tests: host only, and only with assertions compiled in.
if(EL_CONFIG_ASSERT AND ELEMENTAL_DEATH_TESTS)
  elemental_add_test(alloc.violations
    SOURCES alloc_violations_test.cpp
    LIBRARIES elemental::alloc elemental::alloc_contract)
endif()
