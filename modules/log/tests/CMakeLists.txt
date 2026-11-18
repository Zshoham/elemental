# Interface semantics: the shared contract suite, against both sinks.
elemental_add_test(log.contract.sinks
  SOURCES contract_sinks_test.cpp fixture_mem.c fixture_writer.c
  LIBRARIES elemental::log_contract)

# The logger and the printf call site: when a record is made, what it
# holds, when the clock is read, how a message reaches the sink's buffer.
elemental_add_test(log.unit
  SOURCES log_unit_test.cpp log_cutoff_test.cpp log_support.c
  LIBRARIES elemental::log)

# What only the writer sink and the memory sink promise.
elemental_add_test(log.sinks
  SOURCES log_sinks_test.cpp log_support.c
  LIBRARIES elemental::log)

# The writer sink over grown storage: a line as long as the allocator
# allows.
if(EL_CONFIG_IO_ALLOC)
  elemental_add_test(log.sinks.grown
    SOURCES log_writer_grown_test.cpp
    LIBRARIES elemental::log elemental::alloc)
endif()

# The deferred sink: the contract suite, drained into a memory sink; what
# only it promises; and, on a host, a thread on each of its sides (the run
# that can fail on a data race is the `tsan` preset's, docs/build.md).
if(EL_CONFIG_LOG_DEFERRED)
  elemental_add_test(log.contract.deferred
    SOURCES contract_deferred_test.cpp fixture_deferred.c
    LIBRARIES elemental::log_contract)
  elemental_add_test(log.deferred
    SOURCES log_deferred_test.cpp log_support.c
    LIBRARIES elemental::log)
  if(NOT ELEMENTAL_TEST_IMAGE)
    find_package(Threads REQUIRED)
    elemental_add_test(log.deferred.stress
      SOURCES log_deferred_stress_test.cpp stress_support.c
      LIBRARIES elemental::log Threads::Threads)
  endif()
  if(EL_CONFIG_ASSERT AND ELEMENTAL_DEATH_TESTS)
    elemental_add_test(log.violations.deferred
      SOURCES log_deferred_violations_test.cpp log_support.c
      LIBRARIES elemental::log)
  endif()
endif()

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
