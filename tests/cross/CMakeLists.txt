# What is verified for a cross build, and at what level:
#
#   compile check   building the libraries with the cross compiler
#   cross.symbols   symbol check: what the libraries need from outside
#
# Nothing is linked into an image and nothing is run: the tests are
# GoogleTest, which needs a hosted C++ library that a bare freestanding
# toolchain does not have. Running the tests on a microcontroller target is
# planned through Zephyr (docs/roadmap.md). This is not a hardware test, and
# not an emulator test either.

# Every module this configuration enables. Platform backends are disabled by
# Kconfig for a toolchain that cannot build them, so whatever is left claims
# to be freestanding, and a new module is checked without touching this file.
find_program(ELEMENTAL_NM llvm-nm REQUIRED)
get_property(modules GLOBAL PROPERTY ELEMENTAL_MODULES)
set(archives "")
foreach(module IN LISTS modules)
  list(APPEND archives "$<TARGET_FILE:elemental_${module}>")
endforeach()
add_test(
  NAME cross.symbols
  COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/check_symbols.py"
          --nm "${ELEMENTAL_NM}"
          --allow "${CMAKE_CURRENT_SOURCE_DIR}/allowed-undefined.txt"
          ${archives})
