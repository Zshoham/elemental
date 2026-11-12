# Elemental: small, modular, implementation-independent interfaces.
#
# This file is the single CMake entry point, for Elemental's own development
# and for add_subdirectory()/FetchContent consumers alike. As a subproject it
# defines library targets and nothing else: no tests, examples, install
# rules, or short tool target names. See docs/build.md.

# 3.22 is what Ubuntu 22.04 LTS ships. Needed: PROJECT_IS_TOP_LEVEL (3.21),
# presets schema 3 (3.21), file(REAL_PATH) (3.19).
cmake_minimum_required(VERSION 3.22)

project(Elemental
  VERSION 0.1.0
  DESCRIPTION "Small, modular, implementation-independent interfaces"
  LANGUAGES C)

option(ELEMENTAL_BUILD_TESTS "Build Elemental's tests" ${PROJECT_IS_TOP_LEVEL})
option(ELEMENTAL_BUILD_EXAMPLES "Build Elemental's examples" ${PROJECT_IS_TOP_LEVEL})
option(ELEMENTAL_INSTALL "Generate install rules and the CMake package" ${PROJECT_IS_TOP_LEVEL})
option(ELEMENTAL_WARNINGS_AS_ERRORS "Treat warnings in Elemental's own sources as errors" OFF)

# One build may contain one Elemental. Two copies would each define the same
# symbols, possibly from different configurations, and the linker would pick
# one without saying which.
if(TARGET elemental::status)
  message(FATAL_ERROR
    "Elemental targets already exist in this build (from find_package(Elemental) "
    "or another copy of the source tree). Use exactly one.")
endif()

# Install paths are needed only by install rules. Not including the module
# otherwise also keeps a Zephyr build, which hides the target's pointer size
# from it, free of its warning.
if(ELEMENTAL_INSTALL)
  include(GNUInstallDirs)
endif()
include("${CMAKE_CURRENT_LIST_DIR}/cmake/ElementalConfiguration.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/cmake/ElementalModule.cmake")

# Elemental is C. Its tests are GoogleTest, so building them needs a C++17
# compiler and the pinned GoogleTest; building the libraries needs neither.
# Tests are built for the host, or, when the Zephyr test application
# (tests/zephyr) adds Elemental and names its image in ELEMENTAL_TEST_IMAGE,
# into that image. Any other cross build gets libraries only. A language has
# to be enabled in the top-level directory, so it happens here. Afterwards
# elemental_add_test() exists exactly when tests are being registered.
if(ELEMENTAL_BUILD_TESTS AND (ELEMENTAL_TEST_IMAGE OR NOT CMAKE_CROSSCOMPILING))
  enable_testing()
  enable_language(CXX)
  include("${CMAKE_CURRENT_LIST_DIR}/cmake/ElementalTesting.cmake")
endif()

# Modules, in dependency order. A module directory is added when its Kconfig
# symbol is enabled, so CMake targets and C macros follow the same decision.
# The symbol is the directory name in upper case.
set(ELEMENTAL_MODULE_DIRECTORIES status alloc io bytes crc fmt)
foreach(module IN LISTS ELEMENTAL_MODULE_DIRECTORIES)
  string(TOUPPER "${module}" module_symbol)
  if(EL_CONFIG_${module_symbol})
    add_subdirectory(modules/${module})
  endif()
endforeach()

if(ELEMENTAL_BUILD_EXAMPLES)
  add_subdirectory(examples)
endif()
# Repository checks run on the host only.
if(ELEMENTAL_BUILD_TESTS AND NOT CMAKE_CROSSCOMPILING)
  add_subdirectory(tests)
endif()

if(ELEMENTAL_INSTALL)
  include("${CMAKE_CURRENT_LIST_DIR}/cmake/ElementalInstall.cmake")
endif()
