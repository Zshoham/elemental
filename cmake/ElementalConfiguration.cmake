# Resolve Elemental's configuration once, for both CMake and C.
#
# Included by the top-level CMakeLists.txt, in its scope. Afterwards:
#
#   EL_CONFIG_<SYMBOL>            every resolved Kconfig symbol, as a variable
#   ELEMENTAL_CONFIG_INCLUDE_DIRS   directory holding elemental_config.h
#   ELEMENTAL_CONFIG_TARGET         target that regenerates the header
#   ELEMENTAL_CONFIG_HEADER         <build>/kconfig/elemental_config.h
#   ELEMENTAL_CONFIG_CMAKE_FILE     the same values as CMake set() commands
#   ELEMENTAL_CONFIG_FILE           <build>/kconfig/.config, the mutable input
#
# The work is done by the pinned Kconfiglib fork's CMake package. This file
# only locates and verifies that dependency, feeds it the toolchain facts the
# Kconfig tree needs, and rejects input Kconfig would silently rewrite.

include("${CMAKE_CURRENT_LIST_DIR}/KconfiglibPin.cmake")

set(ELEMENTAL_KCONFIGLIB_DIR "${PROJECT_SOURCE_DIR}/.deps/kconfiglib" CACHE PATH
    "Checkout of the pinned Kconfiglib fork (see tools/provision.cmake)")
option(ELEMENTAL_KCONFIGLIB_VERIFY
       "Require ELEMENTAL_KCONFIGLIB_DIR to be at the pinned revision" ON)
set(ELEMENTAL_DEFCONFIG "" CACHE FILEPATH
    "Kconfig fragment that seeds the configuration of a new build directory")

# --- Locate and verify Kconfiglib -------------------------------------------

if(NOT EXISTS "${ELEMENTAL_KCONFIGLIB_DIR}/cmake/Kconfig.cmake")
  message(FATAL_ERROR
    "Kconfiglib was not found in ${ELEMENTAL_KCONFIGLIB_DIR}.\n"
    "Provision the pinned fork once (the only step that needs the network):\n"
    "  cmake -P ${PROJECT_SOURCE_DIR}/tools/provision.cmake\n"
    "or point ELEMENTAL_KCONFIGLIB_DIR at an existing checkout of\n"
    "  ${ELEMENTAL_KCONFIGLIB_REPOSITORY}\n"
    "at revision ${ELEMENTAL_KCONFIGLIB_REVISION}.")
endif()

find_package(Python3 3.9 COMPONENTS Interpreter REQUIRED)

if(ELEMENTAL_KCONFIGLIB_VERIFY)
  find_package(Git QUIET)
  if(NOT Git_FOUND OR NOT EXISTS "${ELEMENTAL_KCONFIGLIB_DIR}/.git")
    message(FATAL_ERROR
      "Cannot verify the Kconfiglib revision: ${ELEMENTAL_KCONFIGLIB_DIR} "
      "is not a git checkout, or git is unavailable.\n"
      "Use tools/provision.cmake, or pass -DELEMENTAL_KCONFIGLIB_VERIFY=OFF "
      "to accept an unverified copy.")
  endif()

  execute_process(
    COMMAND "${GIT_EXECUTABLE}" -C "${ELEMENTAL_KCONFIGLIB_DIR}" rev-parse HEAD
    OUTPUT_VARIABLE _elemental_kconfiglib_head
    OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE _elemental_result)
  if(NOT _elemental_result EQUAL 0
     OR NOT _elemental_kconfiglib_head STREQUAL ELEMENTAL_KCONFIGLIB_REVISION)
    message(FATAL_ERROR
      "Kconfiglib in ${ELEMENTAL_KCONFIGLIB_DIR} is at revision "
      "'${_elemental_kconfiglib_head}', but Elemental pins\n"
      "  ${ELEMENTAL_KCONFIGLIB_REVISION}\n"
      "Run: cmake -P ${PROJECT_SOURCE_DIR}/tools/provision.cmake")
  endif()

  execute_process(
    COMMAND "${GIT_EXECUTABLE}" -C "${ELEMENTAL_KCONFIGLIB_DIR}"
            status --porcelain --untracked-files=no
    OUTPUT_VARIABLE _elemental_kconfiglib_changes
    OUTPUT_STRIP_TRAILING_WHITESPACE)
  if(_elemental_kconfiglib_changes)
    message(FATAL_ERROR
      "Kconfiglib in ${ELEMENTAL_KCONFIGLIB_DIR} has local modifications, so "
      "it is not the pinned revision:\n${_elemental_kconfiglib_changes}")
  endif()
else()
  message(WARNING
    "ELEMENTAL_KCONFIGLIB_VERIFY is OFF: using an unverified Kconfiglib from "
    "${ELEMENTAL_KCONFIGLIB_DIR}.")
endif()

# Every Kconfiglib process runs with these. PYTHONPATH makes module resolution
# independent of whatever kconfiglib the interpreter has installed (upstream
# and the fork share a version number), and PYTHONPYCACHEPREFIX keeps bytecode
# out of the dependency checkout and inside the build tree.
set(_elemental_kconfig_dir "${PROJECT_BINARY_DIR}/kconfig")
set(_elemental_python_env
    "PYTHONPATH=${ELEMENTAL_KCONFIGLIB_DIR}"
    "PYTHONPYCACHEPREFIX=${_elemental_kconfig_dir}/pycache")

# Prove where "import kconfiglib" resolves, rather than assuming it.
file(REAL_PATH "${ELEMENTAL_KCONFIGLIB_DIR}/kconfiglib.py" _elemental_expected_module)
execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env ${_elemental_python_env}
          "${Python3_EXECUTABLE}" -c
"import os, sys, kconfiglib
print(os.path.realpath(kconfiglib.__file__))
sys.exit(0 if hasattr(kconfiglib.Kconfig, 'write_cmake_config') else 3)"
  WORKING_DIRECTORY "${PROJECT_BINARY_DIR}"
  OUTPUT_VARIABLE _elemental_imported_module
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_VARIABLE _elemental_error
  RESULT_VARIABLE _elemental_result)
if(NOT _elemental_result EQUAL 0
   OR NOT _elemental_imported_module STREQUAL _elemental_expected_module)
  message(FATAL_ERROR
    "Python does not import kconfiglib from the pinned checkout.\n"
    "  expected: ${_elemental_expected_module}\n"
    "  imported: ${_elemental_imported_module}\n"
    "  (exit ${_elemental_result}) ${_elemental_error}")
endif()

# --- Toolchain facts the Kconfig tree depends on ----------------------------

# Capabilities are probed, not asserted by the user: a backend that needs
# <unistd.h> cannot be selected for a toolchain that does not have it.
include(CheckIncludeFile)
check_include_file(unistd.h ELEMENTAL_HAVE_UNISTD_H)
if(ELEMENTAL_HAVE_UNISTD_H)
  set(_elemental_platform_posix y)
else()
  set(_elemental_platform_posix n)
endif()

# snprintf() that converts long long, for fmt's FMT_LIBC. A hosted C library
# has it (C99 7.19.6). Zephyr's picolibc converts long long only at some
# printf levels, which Zephyr's own Kconfig chooses, so there the answer is
# that configuration.
if(COMMAND zephyr_library)
  if(CONFIG_PICOLIBC_IO_LONG_LONG OR CONFIG_PICOLIBC_IO_FLOAT)
    set(_elemental_platform_snprintf y)
  else()
    set(_elemental_platform_snprintf n)
  endif()
else()
  include(CheckSymbolExists)
  check_symbol_exists(snprintf stdio.h ELEMENTAL_HAVE_SNPRINTF)
  if(ELEMENTAL_HAVE_SNPRINTF)
    set(_elemental_platform_snprintf y)
  else()
    set(_elemental_platform_snprintf n)
  endif()
endif()

# Kconfig symbols are written without a prefix (IO, ASSERT). Generated names
# get EL_CONFIG_, so Elemental's macros and CMake variables cannot collide
# with a host project that also uses Kconfig and its default CONFIG_ prefix.
set(_elemental_kconfig_env
    "CONFIG_=EL_CONFIG_"
    "EL_PLATFORM_POSIX=${_elemental_platform_posix}"
    "EL_PLATFORM_SNPRINTF=${_elemental_platform_snprintf}"
    ${_elemental_python_env})

# --- Seed handling ----------------------------------------------------------

set(_elemental_config "${_elemental_kconfig_dir}/.config")
set(_elemental_defconfig_args "")
set(_elemental_seed_id "")
if(ELEMENTAL_DEFCONFIG)
  get_filename_component(_elemental_defconfig "${ELEMENTAL_DEFCONFIG}"
                         ABSOLUTE BASE_DIR "${CMAKE_BINARY_DIR}")
  if(NOT EXISTS "${_elemental_defconfig}")
    message(FATAL_ERROR
      "ELEMENTAL_DEFCONFIG does not exist: ${_elemental_defconfig}")
  endif()
  file(SHA256 "${_elemental_defconfig}" _elemental_seed_hash)
  set(_elemental_seed_id "${_elemental_defconfig}@${_elemental_seed_hash}")
  set(_elemental_defconfig_args DEFCONFIG "${_elemental_defconfig}")
  # Edits to the seed file should at least produce the warning below.
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
               "${_elemental_defconfig}")
endif()

# A defconfig only seeds a build directory that has no configuration yet.
# Afterwards <build>/kconfig/.config is the single mutable input, so a
# different or edited defconfig would otherwise be ignored without a word.
if(EXISTS "${_elemental_config}")
  if(NOT "${ELEMENTAL_CONFIG_SEED}" STREQUAL "${_elemental_seed_id}")
    message(WARNING
      "ELEMENTAL_DEFCONFIG changed after this build directory was "
      "configured, and is NOT applied: the existing configuration wins.\n"
      "  configuration: ${_elemental_config}\n"
      "To start over from the defconfig, delete that file and re-run CMake. "
      "To keep the current configuration, ignore this warning.")
  endif()
  set(_elemental_check_input "${_elemental_config}" --existing)
else()
  set(ELEMENTAL_CONFIG_SEED "${_elemental_seed_id}" CACHE INTERNAL
      "The defconfig (path@sha256) that seeded this build's configuration")
  set(_elemental_check_input "${_elemental_defconfig}")
endif()

# --- Reject input Kconfig would silently rewrite ----------------------------

if(_elemental_check_input)
  execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env ${_elemental_kconfig_env}
            "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/kconfig_check.py"
            "${PROJECT_SOURCE_DIR}/Kconfig" ${_elemental_check_input}
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
    ERROR_VARIABLE _elemental_error
    RESULT_VARIABLE _elemental_result)
  if(NOT _elemental_result EQUAL 0)
    message(FATAL_ERROR "Invalid Elemental configuration.\n${_elemental_error}")
  endif()
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
               "${PROJECT_SOURCE_DIR}/tools/kconfig_check.py")
endif()

# --- Generate ---------------------------------------------------------------

include("${ELEMENTAL_KCONFIGLIB_DIR}/cmake/Kconfig.cmake")
kconfig_configure(
  KCONFIG "${PROJECT_SOURCE_DIR}/Kconfig"
  CONFIG "${_elemental_config}"
  ${_elemental_defconfig_args}
  BINARY_DIR "${_elemental_kconfig_dir}"
  PYTHON_EXECUTABLE "${Python3_EXECUTABLE}"
  NAME elemental_config
  ENV ${_elemental_kconfig_env})

set(ELEMENTAL_CONFIG_INCLUDE_DIRS "${KCONFIG_INCLUDE_DIRS}")
set(ELEMENTAL_CONFIG_TARGET "${KCONFIG_TARGET}")
set(ELEMENTAL_CONFIG_HEADER "${KCONFIG_HEADER_FILE}")
set(ELEMENTAL_CONFIG_CMAKE_FILE "${KCONFIG_CMAKE_FILE}")
set(ELEMENTAL_CONFIG_FILE "${KCONFIG_CONFIG_FILE}")

# Short names for the interactive tools, only where Elemental owns the target
# namespace. As a subproject, use elemental_config_menuconfig and friends.
if(PROJECT_IS_TOP_LEVEL)
  foreach(_elemental_tool menuconfig guiconfig savedefconfig)
    add_custom_target(${_elemental_tool})
    add_dependencies(${_elemental_tool} elemental_config_${_elemental_tool})
  endforeach()
endif()

message(STATUS "Elemental configuration: ${ELEMENTAL_CONFIG_FILE}")
