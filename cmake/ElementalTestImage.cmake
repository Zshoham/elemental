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

# Test groups. An image may hold only some of the tests, for a board too
# small for all of them (tests/zephyr/CMakeLists.txt). The application sets:
#
#   ELEMENTAL_TEST_GROUPS        the group names;
#   ELEMENTAL_TEST_GROUP_<group> each group's test-name prefixes: a test
#                                belongs to a group if its <name> is a prefix
#                                or starts with "<prefix>.";
#   ELEMENTAL_TEST_GROUP         the group this image holds, or "all".
#
# Every test must belong to exactly one group, so that a test a new module
# registers cannot silently go missing from the split images.

# The group `name` belongs to, in `out`.
function(_elemental_test_group name out)
  set(found "")
  foreach(group IN LISTS ELEMENTAL_TEST_GROUPS)
    foreach(prefix IN LISTS ELEMENTAL_TEST_GROUP_${group})
      string(LENGTH "${prefix}." length)
      string(SUBSTRING "${name}." 0 ${length} head)
      if(head STREQUAL "${prefix}.")
        if(found AND NOT found STREQUAL group)
          message(FATAL_ERROR "test ${name} is in both test groups ${found} and ${group}")
        endif()
        set(found "${group}")
      endif()
    endforeach()
  endforeach()
  set(${out} "${found}" PARENT_SCOPE)
endfunction()

# elemental_add_test(<name> SOURCES <file>... [LIBRARIES <target>...])
#
# The same registration as on the host, but the sources are added to the
# image, if the test is in the image's group. GoogleTest reports each test by
# suite and name; <name> only selects the group.
function(elemental_add_test name)
  cmake_parse_arguments(PARSE_ARGV 1 arg "" "" "SOURCES;LIBRARIES")
  if(arg_UNPARSED_ARGUMENTS OR NOT arg_SOURCES)
    message(FATAL_ERROR "elemental_add_test(${name}): expected SOURCES <file>... [LIBRARIES <target>...]")
  endif()
  if(ELEMENTAL_TEST_GROUPS)
    _elemental_test_group("${name}" group)
    if(NOT group)
      message(FATAL_ERROR "test ${name} is in no test group: add its module to a group "
                          "in tests/zephyr/CMakeLists.txt (docs/build.md, \"Memory the test image needs\")")
    endif()
    if(NOT ELEMENTAL_TEST_GROUP STREQUAL "all" AND NOT group STREQUAL ELEMENTAL_TEST_GROUP)
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
