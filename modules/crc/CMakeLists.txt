# The implementation is a private choice: every source defines the same
# functions, and exactly one is built into the library. The defaults build
# the 16-entry tables; configs/variant_defconfig (the sanitize preset) builds
# the 256-entry ones; integration.choices builds every option this host's
# compiler can; and the Zephyr test scenario elemental.tests.crc_zephyr
# builds CRC_ZEPHYR.
if(EL_CONFIG_CRC_TABLE_8BIT)
  set(crc_source src/crc_8bit.c)
elseif(EL_CONFIG_CRC_GCC_BUILTIN)
  set(crc_source src/crc_builtin.c)
elseif(EL_CONFIG_CRC_ZEPHYR)
  set(crc_source src/crc_zephyr.c)
else()
  set(crc_source src/crc_4bit.c)
endif()

elemental_add_module(crc
  SOURCES ${crc_source}
  DEPENDS status)

# Zephyr's headers, and the generated configuration they need. Kconfig
# offers CRC_ZEPHYR only in a Zephyr build, where this target exists.
if(EL_CONFIG_CRC_ZEPHYR)
  target_link_libraries(elemental_crc PRIVATE zephyr_interface)
endif()

if(COMMAND elemental_add_test)
  add_subdirectory(tests)
endif()
