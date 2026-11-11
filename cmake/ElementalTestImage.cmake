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

# elemental_add_test(<name> SOURCES <file>... [LIBRARIES <target>...])
#
# The same registration as on the host, but the sources are added to the
# image. GoogleTest reports each test by suite and name; <name> is not used.
function(elemental_add_test name)
  cmake_parse_arguments(PARSE_ARGV 1 arg "" "" "SOURCES;LIBRARIES")
  if(arg_UNPARSED_ARGUMENTS OR NOT arg_SOURCES)
    message(FATAL_ERROR "elemental_add_test(${name}): expected SOURCES <file>... [LIBRARIES <target>...]")
  endif()
  target_sources(${ELEMENTAL_TEST_IMAGE} PRIVATE ${arg_SOURCES})
  target_link_libraries(${ELEMENTAL_TEST_IMAGE} PRIVATE ${arg_LIBRARIES} GTest::gtest)
  if(EL_CONFIG_ASSERT_HANDLER_APPLICATION)
    target_sources(${ELEMENTAL_TEST_IMAGE} PRIVATE
      "${PROJECT_SOURCE_DIR}/tests/support/assert_handler_abort.c")
  endif()
endfunction()
