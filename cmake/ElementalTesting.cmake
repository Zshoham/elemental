# Test support: GoogleTest, and the one way a test executable is defined.
#
# Included by the top-level CMakeLists.txt when Elemental builds its own
# tests. On the host each test is an executable registered with CTest. In
# the Zephyr test application every test is compiled into one image instead
# (ElementalTestImage.cmake). Nothing here reaches a consumer: tests are off
# when Elemental is an ordinary subproject, and an installed package carries
# no GoogleTest.

include("${CMAKE_CURRENT_LIST_DIR}/GoogleTestPin.cmake")

# --- Locate GoogleTest -------------------------------------------------------
# The pinned checkout is built here, with this build's compiler and flags, so
# the framework and the tests always agree (sanitizers included).
set(ELEMENTAL_GOOGLETEST_DIR "${PROJECT_SOURCE_DIR}/.deps/googletest" CACHE PATH
    "Checkout of the pinned GoogleTest (see tools/provision.cmake)")

if(NOT EXISTS "${ELEMENTAL_GOOGLETEST_DIR}/CMakeLists.txt")
  message(FATAL_ERROR
    "GoogleTest was not found in ${ELEMENTAL_GOOGLETEST_DIR}.\n"
    "Elemental's tests need it. Provision it once (the only step that needs "
    "the network):\n"
    "  cmake -P ${PROJECT_SOURCE_DIR}/tools/provision.cmake\n"
    "or point ELEMENTAL_GOOGLETEST_DIR at an existing checkout of\n"
    "  ${ELEMENTAL_GOOGLETEST_REPOSITORY}\n"
    "at revision ${ELEMENTAL_GOOGLETEST_REVISION},\n"
    "or configure with -DELEMENTAL_BUILD_TESTS=OFF: the libraries do not need it.")
endif()

# The revision is checked when it can be. A copy without git metadata (a
# release archive, a vendored tree) is accepted with a note.
find_package(Git QUIET)
if(Git_FOUND AND EXISTS "${ELEMENTAL_GOOGLETEST_DIR}/.git")
  execute_process(
    COMMAND "${GIT_EXECUTABLE}" -C "${ELEMENTAL_GOOGLETEST_DIR}" rev-parse HEAD
    OUTPUT_VARIABLE _elemental_googletest_head
    OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE _elemental_result)
  if(NOT _elemental_result EQUAL 0
     OR NOT _elemental_googletest_head STREQUAL ELEMENTAL_GOOGLETEST_REVISION)
    message(FATAL_ERROR
      "GoogleTest in ${ELEMENTAL_GOOGLETEST_DIR} is at revision "
      "'${_elemental_googletest_head}', but Elemental pins\n"
      "  ${ELEMENTAL_GOOGLETEST_REVISION}\n"
      "Run: cmake -P ${PROJECT_SOURCE_DIR}/tools/provision.cmake")
  endif()
else()
  message(STATUS
    "GoogleTest in ${ELEMENTAL_GOOGLETEST_DIR} is not a git checkout; "
    "its revision is not verified")
endif()

if(ELEMENTAL_TEST_IMAGE)
  include("${CMAKE_CURRENT_LIST_DIR}/ElementalTestImage.cmake")
  return()
endif()

# Death tests fork a child process, which only a host can do.
set(ELEMENTAL_DEATH_TESTS ON)

# Elemental's install must not install GoogleTest, and gMock is not used.
set(INSTALL_GTEST OFF)
set(BUILD_GMOCK OFF)
add_subdirectory("${ELEMENTAL_GOOGLETEST_DIR}" "${PROJECT_BINARY_DIR}/googletest"
                 EXCLUDE_FROM_ALL)

# Treat GoogleTest's headers as system headers, so Elemental's strict
# warnings apply to the tests and not to the framework.
foreach(_elemental_target gtest gtest_main)
  get_target_property(_elemental_includes ${_elemental_target}
                      INTERFACE_INCLUDE_DIRECTORIES)
  set_target_properties(${_elemental_target} PROPERTIES
    INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${_elemental_includes}")
endforeach()

include(GoogleTest)

# elemental_add_test(<name> SOURCES <file>... [LIBRARIES <target>...])
#
# A GoogleTest executable. Sources may be C++ (the tests) and C (fixtures and
# helpers written against the C interfaces). GoogleTest's main() is linked
# in. Each TEST becomes one CTest test named <name>.<Suite>.<Test>.
#
# When the configuration routes assertion failures to the application, every
# executable must define el_assert_failed(). Tests get one that reports and
# aborts, which is also what a death test needs.
function(elemental_add_test name)
  cmake_parse_arguments(PARSE_ARGV 1 arg "" "" "SOURCES;LIBRARIES")
  if(arg_UNPARSED_ARGUMENTS OR NOT arg_SOURCES)
    message(FATAL_ERROR "elemental_add_test(${name}): expected SOURCES <file>... [LIBRARIES <target>...]")
  endif()

  string(REPLACE "." "_" target "test_${name}")
  add_executable(${target} ${arg_SOURCES})
  target_link_libraries(${target} PRIVATE ${arg_LIBRARIES} GTest::gtest_main)
  if(EL_CONFIG_ASSERT_HANDLER_APPLICATION)
    target_sources(${target} PRIVATE
      "${PROJECT_SOURCE_DIR}/tests/support/assert_handler_abort.c")
  endif()
  elemental_target_c_standard(${target})
  set_target_properties(${target} PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)
  elemental_target_warnings(${target})

  # PRE_TEST: tests are listed when ctest runs, not as a build step, so a
  # build never executes a test binary.
  gtest_discover_tests(${target}
    TEST_PREFIX "${name}."
    DISCOVERY_MODE PRE_TEST)
endfunction()
