# fmt_printf.c is its own object, so a program that never calls
# el_fmt_printf() does not link it.
elemental_add_module(fmt
  SOURCES src/fmt.c src/fmt_printf.c
  DEPENDS status)

# No jump tables: for a dense switch, or an if chain it rebuilds into one,
# GCC for ARMv6-M at -Os emits calls to libgcc's __gnu_thumb1_case_uqi, a
# compiler runtime helper Elemental does not depend on
# (tests/zephyr/allowed-undefined.txt). Seen on the printf parser with the
# Zephyr SDK's GCC 14.3.0 for qemu_cortex_m0.
target_compile_options(elemental_fmt PRIVATE
  $<$<C_COMPILER_ID:GNU,Clang>:-fno-jump-tables>)

if(COMMAND elemental_add_test)
  add_subdirectory(tests)
endif()
