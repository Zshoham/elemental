# The configured implementation, through the library.
elemental_add_test(crc.unit
  SOURCES crc_test.cpp
  LIBRARIES elemental::crc)

# The other table size, so that both choices are run in every preset. Its
# source is compiled straight into the test, with this build's configuration.
elemental_add_test(crc.other_table
  SOURCES crc_test.cpp "${CMAKE_CURRENT_SOURCE_DIR}/../${crc_other_source}"
  LIBRARIES elemental::status)
target_include_directories(test_crc_other_table PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/../include" ${ELEMENTAL_CONFIG_INCLUDE_DIRS})
add_dependencies(test_crc_other_table ${ELEMENTAL_CONFIG_TARGET})

# With assertions compiled out there is nothing to observe.
if(EL_CONFIG_ASSERT)
  elemental_add_test(crc.violations
    SOURCES crc_violations_test.cpp
    LIBRARIES elemental::crc)
endif()
