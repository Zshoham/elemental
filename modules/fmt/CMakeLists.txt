# fmt_printf.c and fmt_printf_buffered.c are their own objects, so a program
# that never calls printf links neither, and one that prints only into
# buffers links no buffered-writer code. fmt_printf_libc.c is the
# FMT_PRINTF_LIBC option's bridge to snprintf(), built only with it.
set(fmt_sources src/fmt.c src/fmt_printf.c src/fmt_printf_buffered.c)
if(EL_CONFIG_FMT_PRINTF_LIBC)
  list(APPEND fmt_sources src/fmt_printf_libc.c)
endif()
elemental_add_module(fmt
  SOURCES ${fmt_sources}
  DEPENDS status io)

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
