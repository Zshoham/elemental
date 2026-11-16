# Each source is its own object, so a program links only what it calls: the
# front functions, the conversions, the manual clock.
elemental_add_module(time
  SOURCES src/time.c src/time_convert.c src/time_manual.c
  DEPENDS status)

add_subdirectory(contract)

if(EL_CONFIG_TIME_POSIX)
  add_subdirectory(backends/posix)
endif()
if(EL_CONFIG_TIME_ZEPHYR)
  add_subdirectory(backends/zephyr)
endif()

if(COMMAND elemental_add_test)
  add_subdirectory(tests)
endif()
