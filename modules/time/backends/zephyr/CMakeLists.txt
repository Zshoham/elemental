elemental_add_module(time_zephyr
  SOURCES src/time_zephyr.c
  DEPENDS time)

# Zephyr's headers, and the generated configuration they need. Kconfig
# offers TIME_ZEPHYR only in a Zephyr build, where this target exists.
# Zephyr's headers are GNU C, so they are system headers here: Elemental's
# -Wpedantic stays on its own code (as modules/crc does for CRC_ZEPHYR).
# For the build only, as in modules/io/backends/zephyr_uart:
# zephyr_interface is in no export set.
target_link_libraries(elemental_time_zephyr PRIVATE $<BUILD_INTERFACE:zephyr_interface>)
get_target_property(zephyr_includes zephyr_interface INTERFACE_INCLUDE_DIRECTORIES)
target_include_directories(elemental_time_zephyr SYSTEM PRIVATE ${zephyr_includes})
# <zephyr/kernel.h> includes headers Zephyr's build generates (system call
# lists, heap constants). A zephyr_library() waits for them by itself.
add_dependencies(elemental_time_zephyr zephyr_generated_headers)

if(COMMAND elemental_add_test)
  # Interface semantics: the shared contract suite, against this backend.
  elemental_add_test(time_zephyr.contract
    SOURCES tests/contract_zephyr_test.cpp tests/fixture_zephyr.c
    LIBRARIES elemental::time_zephyr elemental::time_contract)

  # Platform behavior: what only this backend promises.
  elemental_add_test(time_zephyr.platform
    SOURCES tests/time_zephyr_test.cpp
    LIBRARIES elemental::time_zephyr)
endif()
