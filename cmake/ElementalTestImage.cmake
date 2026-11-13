# Tests compiled into one image, for a target that cannot run a test
# executable per test: the Zephyr test application (tests/zephyr).
#
# Included by ElementalTesting.cmake when ELEMENTAL_TEST_IMAGE names the
# image's target. The application supplies main(), and the flags that
# describe its target: GoogleTest and every Elemental library are compiled
# with them (tests/zephyr/CMakeLists.txt).

# GoogleTest from the same pinned checkout as the host build, as one
# translation unit, with no file system: that turns off death tests, output
# redirection, and the XML and JSON reports, which a target has nowhere to
# write. gMock is not built.
add_library(gtest STATIC EXCLUDE_FROM_ALL
  "${ELEMENTAL_GOOGLETEST_DIR}/googletest/src/gtest-all.cc")
target_include_directories(gtest
  SYSTEM PUBLIC "${ELEMENTAL_GOOGLETEST_DIR}/googletest/include"
  PRIVATE "${ELEMENTAL_GOOGLETEST_DIR}/googletest")
target_compile_definitions(gtest PUBLIC GTEST_HAS_FILE_SYSTEM=0)
target_compile_features(gtest PUBLIC cxx_std_17)
add_library(GTest::gtest ALIAS gtest)

set(ELEMENTAL_DEATH_TESTS OFF)

# Test modules. An image may hold one module's tests only, so that each
# image stays within a small board's RAM (tests/zephyr/CMakeLists.txt). The
# application sets:
#
#   ELEMENTAL_TEST_MODULES        the module names;
#   ELEMENTAL_TEST_MODULE_<m>     the test-name prefixes of module <m>'s
#                                 tests: a test belongs to it if its <name>
#                                 is a prefix or starts with "<prefix>.";
#   ELEMENTAL_TEST_MODULE         the module this image holds, or "all".
#
# Every test must belong to exactly one module, so that a test cannot
# silently go missing from the per-module images.

# The module `name` belongs to, in `out`.
function(_elemental_test_module name out)
  set(found "")
  foreach(module IN LISTS ELEMENTAL_TEST_MODULES)
    foreach(prefix IN LISTS ELEMENTAL_TEST_MODULE_${module})
      string(LENGTH "${prefix}." length)
      string(SUBSTRING "${name}." 0 ${length} head)
      if(head STREQUAL "${prefix}.")
        if(found AND NOT found STREQUAL module)
          message(FATAL_ERROR "test ${name} is in the images of both ${found} and ${module}")
        endif()
        set(found "${module}")
      endif()
    endforeach()
  endforeach()
  set(${out} "${found}" PARENT_SCOPE)
endfunction()

# elemental_add_test(<name> SOURCES <file>... [LIBRARIES <target>...])
#
# The same registration as on the host, but the sources are added to the
# image, if the test is in the image's module. GoogleTest reports each test
# by suite and name; <name> only selects the image.
function(elemental_add_test name)
  cmake_parse_arguments(PARSE_ARGV 1 arg "" "" "SOURCES;LIBRARIES")
  if(arg_UNPARSED_ARGUMENTS OR NOT arg_SOURCES)
    message(FATAL_ERROR "elemental_add_test(${name}): expected SOURCES <file>... [LIBRARIES <target>...]")
  endif()
  if(ELEMENTAL_TEST_MODULES)
    _elemental_test_module("${name}" module)
    if(NOT module)
      message(FATAL_ERROR "test ${name} belongs to no test image: add its prefix to "
                          "ELEMENTAL_TEST_MODULE_<module> in tests/zephyr/CMakeLists.txt, "
                          "or a new module there and in testcase.yaml")
    endif()
    if(NOT ELEMENTAL_TEST_MODULE STREQUAL "all" AND NOT module STREQUAL ELEMENTAL_TEST_MODULE)
      return()
    endif()
  endif()
  target_sources(${ELEMENTAL_TEST_IMAGE} PRIVATE ${arg_SOURCES})
  target_link_libraries(${ELEMENTAL_TEST_IMAGE} PRIVATE ${arg_LIBRARIES} GTest::gtest)
  if(EL_CONFIG_ASSERT_HANDLER_APPLICATION)
    target_sources(${ELEMENTAL_TEST_IMAGE} PRIVATE
      "${PROJECT_SOURCE_DIR}/tests/support/assert_handler_abort.c")
  endif()
endfunction()
