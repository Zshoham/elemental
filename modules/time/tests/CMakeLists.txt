# Interface semantics: the shared contract suite, against the manual clock.
elemental_add_test(time.contract.manual
  SOURCES contract_manual_test.cpp fixture_manual.c
  LIBRARIES elemental::time_contract)

# The front functions, the conversions, and the manual clock.
elemental_add_test(time.unit
  SOURCES time_unit_test.cpp scripted_clock.c
  LIBRARIES elemental::time)

# Contract checks fire, in whichever way the configuration makes them fire.
# Death tests: host only, and only with assertions compiled in.
if(EL_CONFIG_ASSERT AND ELEMENTAL_DEATH_TESTS)
  elemental_add_test(time.violations
    SOURCES time_violations_test.cpp scripted_clock.c
    LIBRARIES elemental::time)
endif()
