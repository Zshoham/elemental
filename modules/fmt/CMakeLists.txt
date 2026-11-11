# fmt_printf.c is its own object, so a program that never calls
# el_fmt_printf() does not link it.
elemental_add_module(fmt
  SOURCES src/fmt.c src/fmt_printf.c
  DEPENDS status)

if(ELEMENTAL_BUILD_TESTS AND NOT CMAKE_CROSSCOMPILING)
  add_subdirectory(tests)
endif()
