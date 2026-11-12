# The table size is a private choice: both sources define the same
# functions, and exactly one is built into the library.
# The defaults build the 16-entry tables; configs/variant_defconfig (the
# sanitize preset) builds the 256-entry ones.
if(EL_CONFIG_CRC_TABLE_8BIT)
  set(crc_source src/crc_8bit.c)
else()
  set(crc_source src/crc_4bit.c)
endif()

elemental_add_module(crc
  SOURCES ${crc_source}
  DEPENDS status)

if(COMMAND elemental_add_test)
  add_subdirectory(tests)
endif()
