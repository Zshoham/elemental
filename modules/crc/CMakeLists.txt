# The table size is a private choice: both sources define the same
# functions, and exactly one is built into the library.
if(EL_CONFIG_CRC_TABLE_8BIT)
  set(crc_source src/crc_8bit.c)
  set(crc_other_source src/crc_4bit.c)
else()
  set(crc_source src/crc_4bit.c)
  set(crc_other_source src/crc_8bit.c)
endif()

elemental_add_module(crc
  SOURCES ${crc_source}
  DEPENDS status)

if(ELEMENTAL_BUILD_TESTS AND NOT CMAKE_CROSSCOMPILING)
  add_subdirectory(tests)
endif()
