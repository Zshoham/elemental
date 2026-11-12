# Elemental's tests as one Zephyr application, run by Twister
# (docs/build.md, "Tests on Zephyr").
#
# Elemental is added as a subproject with its tests on. Every test a module
# registers with elemental_add_test() is compiled into this image
# (cmake/ElementalTestImage.cmake), so a new module's tests are included
# without a change here.

cmake_minimum_required(VERSION 3.20.0)
# boards/: nucleo_g0b1re_renode, the ARMv6-M board the tests run on.
list(APPEND BOARD_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(elemental_tests LANGUAGES C CXX)

# Off for a board too small for GoogleTest: the libraries are still built
# for it and their symbols checked (testcase.yaml, elemental.symbols.*).
option(ELEMENTAL_ZEPHYR_TESTS "Compile Elemental's tests into the image" ON)

get_filename_component(ELEMENTAL_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
if(ELEMENTAL_ZEPHYR_TESTS)
  target_sources(app PRIVATE src/main.cpp)
  if(NOT CONFIG_ARCH_POSIX)
    target_sources(app PRIVATE src/libc_support.c)
  endif()
  set(ELEMENTAL_BUILD_TESTS ON)
  set(ELEMENTAL_BUILD_EXAMPLES ON)
  set(ELEMENTAL_TEST_IMAGE app)
else()
  target_sources(app PRIVATE src/no_tests.c)
endif()
# A variant: one of the repository's configs/<name>_defconfig, applied on
# top of this image's own elemental_defconfig, so the image runs the same
# configurations as the host presets (testcase.yaml).
set(ELEMENTAL_ZEPHYR_VARIANT "" CACHE STRING
    "Name of a configs/<name>_defconfig to apply on top of elemental_defconfig")
set(defconfig "${CMAKE_CURRENT_SOURCE_DIR}/elemental_defconfig")
if(ELEMENTAL_ZEPHYR_VARIANT)
  set(variant "${ELEMENTAL_ROOT}/configs/${ELEMENTAL_ZEPHYR_VARIANT}_defconfig")
  if(NOT EXISTS "${variant}")
    message(FATAL_ERROR "ELEMENTAL_ZEPHYR_VARIANT: ${variant} does not exist")
  endif()
  file(READ "${defconfig}" base_text)
  file(READ "${variant}" variant_text)
  set(defconfig "${CMAKE_CURRENT_BINARY_DIR}/elemental_${ELEMENTAL_ZEPHYR_VARIANT}_defconfig")
  file(WRITE "${defconfig}" "${base_text}${variant_text}")
endif()
set(ELEMENTAL_DEFCONFIG "${defconfig}" CACHE FILEPATH
    "Kconfig fragment that seeds Elemental's configuration in this image")
# Elemental's own sources with its strict warnings, as errors, under the
# board's compiler. (Zephyr's flags come later on the command line and set
# -std=c17; that the sources are C11 is checked by the host presets.)
set(ELEMENTAL_WARNINGS_AS_ERRORS ON)
add_subdirectory("${ELEMENTAL_ROOT}" elemental)

# Zephyr keeps the flags that describe the board (CPU, ABI, C library) in
# zephyr_interface rather than in global flags. Every library Elemental's
# build defines, GoogleTest included, is compiled with them.
function(elemental_use_board_flags directory)
  get_property(targets DIRECTORY "${directory}" PROPERTY BUILDSYSTEM_TARGETS)
  foreach(target IN LISTS targets)
    get_target_property(type ${target} TYPE)
    if(type STREQUAL "STATIC_LIBRARY")
      target_link_libraries(${target} PRIVATE zephyr_interface)
    endif()
  endforeach()
  get_property(subdirectories DIRECTORY "${directory}" PROPERTY SUBDIRECTORIES)
  foreach(subdirectory IN LISTS subdirectories)
    elemental_use_board_flags("${subdirectory}")
  endforeach()
endfunction()
elemental_use_board_flags("${ELEMENTAL_ROOT}")

# The symbol check: what Elemental's libraries need from outside themselves,
# compiled for this Cortex-M board by its toolchain. Anything not in
# allowed-undefined.txt fails the build.
if(CONFIG_ARM)
  get_property(modules GLOBAL PROPERTY ELEMENTAL_MODULES)
  set(archives "")
  foreach(module IN LISTS modules)
    list(APPEND archives "$<TARGET_FILE:elemental_${module}>")
  endforeach()
  add_custom_target(elemental_symbols ALL
    COMMAND "${PYTHON_EXECUTABLE}" "${ELEMENTAL_ROOT}/tools/check_symbols.py"
            --nm "${CMAKE_NM}"
            --allow "${CMAKE_CURRENT_SOURCE_DIR}/allowed-undefined.txt"
            ${archives}
    COMMENT "Checking the symbols Elemental's libraries leave undefined"
    VERBATIM)
  list(TRANSFORM modules PREPEND elemental_)
  add_dependencies(elemental_symbols ${modules})
endif()
