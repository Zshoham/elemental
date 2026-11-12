# The configured implementation, through the library.
elemental_add_test(crc.unit
  SOURCES crc_test.cpp
  LIBRARIES elemental::crc)

# With assertions compiled out there is nothing to observe, and without a
# process to fork (on a target) there is no death test.
if(EL_CONFIG_ASSERT AND ELEMENTAL_DEATH_TESTS)
  elemental_add_test(crc.violations
    SOURCES crc_violations_test.cpp
    LIBRARIES elemental::crc)
endif()
