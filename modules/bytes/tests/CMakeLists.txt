elemental_add_test(bytes.unit
  SOURCES bytes_test.cpp
  LIBRARIES elemental::bytes)

# With assertions compiled out there is nothing to observe, and without a
# process to fork (on a target) there is no death test.
if(EL_CONFIG_ASSERT AND ELEMENTAL_DEATH_TESTS)
  elemental_add_test(bytes.violations
    SOURCES bytes_violations_test.cpp
    LIBRARIES elemental::bytes)
endif()
