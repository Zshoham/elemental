# Interface semantics: the shared contract suite, against both sinks.
elemental_add_test(log.contract.sinks
  SOURCES contract_sinks_test.cpp fixture_mem.c fixture_writer.c
  LIBRARIES elemental::log_contract)

# The logger and the printf call site: when a record is made, what it
# holds, when the clock is read. The printf tests are told the configured
# message limit; on Zephyr the sources are compiled into the image's target,
# which is defined in another directory.
set(log_unit_properties COMPILE_DEFINITIONS "EL_TEST_LOG_MESSAGE_MAX=${EL_CONFIG_LOG_MESSAGE_MAX}")
if(ELEMENTAL_TEST_IMAGE)
  set_source_files_properties(log_unit_test.cpp
    TARGET_DIRECTORY ${ELEMENTAL_TEST_IMAGE}
    PROPERTIES ${log_unit_properties})
else()
  set_source_files_properties(log_unit_test.cpp PROPERTIES ${log_unit_properties})
endif()
elemental_add_test(log.unit
  SOURCES log_unit_test.cpp log_cutoff_test.cpp log_support.c
  LIBRARIES elemental::log)

# What only the writer sink and the memory sink promise.
elemental_add_test(log.sinks
  SOURCES log_sinks_test.cpp log_support.c
  LIBRARIES elemental::log)

# Two sinks in one program, one of them a POSIX descriptor.
if(EL_CONFIG_IO_POSIX AND EL_CONFIG_TIME_POSIX)
  elemental_add_test(log.posix
    SOURCES log_posix_test.cpp
    LIBRARIES elemental::log elemental::io_posix elemental::time_posix)
endif()

# Contract checks fire, in whichever way the configuration makes them fire.
# Death tests: host only, and only with assertions compiled in.
if(EL_CONFIG_ASSERT AND ELEMENTAL_DEATH_TESTS)
  elemental_add_test(log.violations
    SOURCES log_violations_test.cpp log_support.c
    LIBRARIES elemental::log)
endif()

# EL_LOG_CUTOFF removes call sites: a call below it leaves neither a call
# nor its format string in the object file. The compiler is run directly,
# as C11 and as C++17, without optimization and at -Os. Host only.
if(NOT ELEMENTAL_TEST_IMAGE AND CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
  get_property(cutoff_include_dirs GLOBAL PROPERTY ELEMENTAL_PUBLIC_INCLUDE_DIRS)
  set(cutoff_compiler_c11 "${CMAKE_C_COMPILER}")
  set(cutoff_flags_c11 -std=c11)
  set(cutoff_compiler_cxx17 "${CMAKE_CXX_COMPILER}")
  set(cutoff_flags_cxx17 -x c++ -std=c++17)
  set(cutoff_kept_default debug info warn error)
  set(cutoff_kept_EL_LOG_LEVEL_DEBUG debug info warn error)
  set(cutoff_kept_EL_LOG_LEVEL_INFO info warn error)
  set(cutoff_kept_EL_LOG_LEVEL_WARN warn error)
  set(cutoff_kept_EL_LOG_LEVEL_ERROR error)
  set(cutoff_kept_EL_LOG_LEVEL_OFF "")
  foreach(language IN ITEMS c11 cxx17)
    foreach(optimization IN ITEMS O0 Os)
      foreach(cutoff IN ITEMS default EL_LOG_LEVEL_DEBUG EL_LOG_LEVEL_INFO EL_LOG_LEVEL_WARN
                              EL_LOG_LEVEL_ERROR EL_LOG_LEVEL_OFF)
        string(REPLACE "EL_LOG_LEVEL_" "" cutoff_name "${cutoff}")
        string(TOLOWER "${cutoff_name}" cutoff_name)
        add_test(NAME log.cutoff_check.${language}.${optimization}.${cutoff_name}
          COMMAND "${CMAKE_COMMAND}"
                  -D "COMPILER=${cutoff_compiler_${language}}"
                  -D "FLAGS=${cutoff_flags_${language}};-${optimization}"
                  -D "SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/cutoff_check.c"
                  -D "OUTPUT=${CMAKE_CURRENT_BINARY_DIR}/cutoff_${language}_${optimization}_${cutoff_name}.o"
                  -D "INCLUDES=${cutoff_include_dirs}"
                  -D "CUTOFF=${cutoff}"
                  -D "KEPT=${cutoff_kept_${cutoff}}"
                  -P "${CMAKE_CURRENT_SOURCE_DIR}/cutoff_check.cmake")
      endforeach()
    endforeach()
  endforeach()
endif()
