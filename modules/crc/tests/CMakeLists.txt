# The configured implementation, through the library.
elemental_add_test(crc.unit
  SOURCES crc_test.cpp
  LIBRARIES elemental::crc)

# Host only: the other table size, so that both choices are run in every
# host preset. Its source is compiled straight into a test executable of its
# own, with this build's configuration. A test image (tests/zephyr) is one
# program, where it would define the library's functions a second time.
if(NOT ELEMENTAL_TEST_IMAGE)
  elemental_add_test(crc.other_table
    SOURCES crc_test.cpp "${CMAKE_CURRENT_SOURCE_DIR}/../${crc_other_source}"
    LIBRARIES elemental::status)
  target_include_directories(test_crc_other_table PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../include" ${ELEMENTAL_CONFIG_INCLUDE_DIRS})
  add_dependencies(test_crc_other_table ${ELEMENTAL_CONFIG_TARGET})
endif()

# With assertions compiled out there is nothing to observe, and without a
# process to fork (on a target) there is no death test.
if(EL_CONFIG_ASSERT AND ELEMENTAL_DEATH_TESTS)
  elemental_add_test(crc.violations
    SOURCES crc_violations_test.cpp
    LIBRARIES elemental::crc)
endif()
