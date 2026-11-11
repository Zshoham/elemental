# Provision Elemental's build-time dependencies at their pinned revisions:
#
#   Kconfiglib   the configuration system (always needed)
#   GoogleTest   the test framework (needed only to build Elemental's tests)
#
#   cmake -P tools/provision.cmake
#
# This is the only step that uses the network. Configure and build never fetch
# anything; they read the checkouts this script leaves behind.
#
# Options (pass with -D before -P). <NAME> is KCONFIGLIB or GOOGLETEST:
#   ELEMENTAL_<NAME>_DIR     Where to put the checkout.
#                            Default: <elemental>/.deps/<name>
#   ELEMENTAL_<NAME>_SOURCE  Clone from this URL or local repository instead
#                            of the pinned URL, for mirrors and air-gapped
#                            machines. The pinned commit is still required,
#                            so the content is identical.
#   ELEMENTAL_PROVISION_TESTS=OFF  Skip GoogleTest.

cmake_minimum_required(VERSION 3.22)

get_filename_component(_root "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
include("${_root}/cmake/KconfiglibPin.cmake")
include("${_root}/cmake/GoogleTestPin.cmake")

find_program(_git git REQUIRED)

function(_git_in dir out_var)
  execute_process(
    COMMAND "${_git}" -C "${dir}" ${ARGN}
    RESULT_VARIABLE _result
    OUTPUT_VARIABLE _output
    ERROR_VARIABLE _error
    OUTPUT_STRIP_TRAILING_WHITESPACE)
  if(NOT _result EQUAL 0)
    message(FATAL_ERROR "git ${ARGN} failed in ${dir}:\n${_error}")
  endif()
  set(${out_var} "${_output}" PARENT_SCOPE)
endfunction()

# _provision(<display name> <checkout dir> <clone source> <pinned revision>)
function(_provision name dir source revision)
  if(EXISTS "${dir}/.git")
    _git_in("${dir}" _head rev-parse HEAD)
    if(_head STREQUAL revision)
      message(STATUS "${name} already provisioned at ${revision}")
      message(STATUS "  ${dir}")
      return()
    endif()
    message(STATUS "${name} checkout is at ${_head}; moving it to the pinned revision")
    execute_process(
      COMMAND "${_git}" -C "${dir}" fetch --quiet "${source}"
      RESULT_VARIABLE _result)
    if(NOT _result EQUAL 0)
      message(FATAL_ERROR "Could not fetch from ${source}")
    endif()
  elseif(EXISTS "${dir}")
    message(FATAL_ERROR
      "${dir} exists but is not a git checkout. "
      "Remove it, or choose another directory for ${name}.")
  else()
    message(STATUS "Cloning ${name} from ${source}")
    execute_process(
      COMMAND "${_git}" clone --quiet --no-checkout "${source}" "${dir}"
      RESULT_VARIABLE _result)
    if(NOT _result EQUAL 0)
      message(FATAL_ERROR "Could not clone ${source}")
    endif()
  endif()

  _git_in("${dir}" _ignored -c advice.detachedHead=false checkout --quiet --detach
          "${revision}")
  _git_in("${dir}" _head rev-parse HEAD)
  if(NOT _head STREQUAL revision)
    message(FATAL_ERROR "Checked out ${_head}, expected ${revision}")
  endif()

  message(STATUS "${name} provisioned")
  message(STATUS "  source:   ${source}")
  message(STATUS "  revision: ${_head}")
  message(STATUS "  path:     ${dir}")
endfunction()

if(NOT ELEMENTAL_KCONFIGLIB_DIR)
  set(ELEMENTAL_KCONFIGLIB_DIR "${_root}/.deps/kconfiglib")
endif()
if(NOT ELEMENTAL_KCONFIGLIB_SOURCE)
  set(ELEMENTAL_KCONFIGLIB_SOURCE "${ELEMENTAL_KCONFIGLIB_REPOSITORY}")
endif()
_provision(Kconfiglib "${ELEMENTAL_KCONFIGLIB_DIR}" "${ELEMENTAL_KCONFIGLIB_SOURCE}"
           "${ELEMENTAL_KCONFIGLIB_REVISION}")

if(NOT DEFINED ELEMENTAL_PROVISION_TESTS OR ELEMENTAL_PROVISION_TESTS)
  if(NOT ELEMENTAL_GOOGLETEST_DIR)
    set(ELEMENTAL_GOOGLETEST_DIR "${_root}/.deps/googletest")
  endif()
  if(NOT ELEMENTAL_GOOGLETEST_SOURCE)
    set(ELEMENTAL_GOOGLETEST_SOURCE "${ELEMENTAL_GOOGLETEST_REPOSITORY}")
  endif()
  _provision(GoogleTest "${ELEMENTAL_GOOGLETEST_DIR}" "${ELEMENTAL_GOOGLETEST_SOURCE}"
             "${ELEMENTAL_GOOGLETEST_REVISION}")
endif()
