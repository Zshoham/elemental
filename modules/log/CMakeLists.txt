# The deferred sink (el_log_deferred_sink) is part of log when LOG_DEFERRED
# is on. It is the only thing in log that needs ring, so its source and the
# dependency come and go together. Each source is its own object, so a
# program links only what it uses: the logger, the printf call site, each
# sink.
set(log_sources src/log.c src/log_printf.c src/log_writer.c src/log_mem.c)
set(log_dependencies status io fmt time)
set(log_omitted_headers "")
if(EL_CONFIG_LOG_DEFERRED)
  list(APPEND log_sources src/log_deferred.c)
  list(APPEND log_dependencies ring)
else()
  list(APPEND log_omitted_headers elemental/log_deferred.h)
endif()

elemental_add_module(log
  SOURCES ${log_sources}
  DEPENDS ${log_dependencies}
  OMIT_HEADERS ${log_omitted_headers})

# No jump tables, as modules/fmt: for a dense switch GCC for ARMv6-M at -Os
# calls libgcc's __gnu_thumb1_case_uqi, a compiler runtime helper Elemental
# does not depend on (tests/zephyr/allowed-undefined.txt).
target_compile_options(elemental_log PRIVATE
  $<$<C_COMPILER_ID:GNU,Clang>:-fno-jump-tables>)

add_subdirectory(contract)

if(COMMAND elemental_add_test)
  add_subdirectory(tests)
endif()
