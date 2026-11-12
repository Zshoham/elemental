elemental_add_test(bytes.unit
  SOURCES bytes_test.cpp bits_test.cpp
  LIBRARIES elemental::bytes)

# With assertions compiled out there is nothing to observe, and without a
# process to fork (on a target) there is no death test.
if(EL_CONFIG_ASSERT AND ELEMENTAL_DEATH_TESTS)
  elemental_add_test(bytes.violations
    SOURCES bytes_violations_test.cpp
    LIBRARIES elemental::bytes)
endif()

# The portable bit counting, which a host compiler's builtins would
# otherwise replace: bytes.c compiled again with them turned off. Host only:
# in the Zephyr test image its symbols would clash with the library's, and
# ARMv6-M runs the portable code through the library anyway.
if(NOT ELEMENTAL_TEST_IMAGE)
  elemental_add_test(bytes.portable
    SOURCES bits_test.cpp ../src/bytes.c
    LIBRARIES elemental::status)
  target_compile_definitions(test_bytes_portable PRIVATE BYTES_TEST_PORTABLE)
  target_include_directories(test_bytes_portable PRIVATE ${ELEMENTAL_CONFIG_INCLUDE_DIRS}
                                                         ../include)
endif()
