# Each source is its own object, so a program links only what it uses: the
# logger, the printf call site, each sink.
elemental_add_module(log
  SOURCES src/log.c src/log_printf.c src/log_writer.c src/log_mem.c
  DEPENDS status io fmt time)

# No jump tables, as modules/fmt: for a dense switch GCC for ARMv6-M at -Os
# calls libgcc's __gnu_thumb1_case_uqi, a compiler runtime helper Elemental
# does not depend on (tests/zephyr/allowed-undefined.txt).
target_compile_options(elemental_log PRIVATE
  $<$<C_COMPILER_ID:GNU,Clang>:-fno-jump-tables>)

add_subdirectory(contract)

if(COMMAND elemental_add_test)
  add_subdirectory(tests)
endif()
