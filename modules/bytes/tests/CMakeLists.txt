elemental_add_test(bytes.unit
  SOURCES bytes_test.cpp
  LIBRARIES elemental::bytes)

# With assertions compiled out there is nothing to observe.
if(EL_CONFIG_ASSERT)
  elemental_add_test(bytes.violations
    SOURCES bytes_violations_test.cpp
    LIBRARIES elemental::bytes)
endif()
