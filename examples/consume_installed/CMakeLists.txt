# An application and a contract suite run that use an installed Elemental.
#
# Nothing here runs Kconfig or needs Python: the installed package is one
# frozen configuration.
#
#   cmake -S . -B build -DCMAKE_PREFIX_PATH=<elemental install prefix>
cmake_minimum_required(VERSION 3.22)
project(consume_installed LANGUAGES C CXX)

# Components are module names. Asking for one the package was configured
# without fails here, with a message naming it.
find_package(Elemental 0.1 REQUIRED COMPONENTS io io_posix io_contract ring log time_posix)

# The application author's view: copy standard input to standard output,
# and log what goes wrong to standard error. This part needs no C++ and no
# GoogleTest.
add_executable(el_cat main.c)
target_link_libraries(el_cat PRIVATE elemental::io_posix elemental::log elemental::time_posix)

# The backend author's view: the contract suite the package installs, run
# from outside Elemental's repository. Here it runs against the package's
# own ring; a backend kept outside the repository is verified the same way,
# with the fixture its author writes.
#
# The suite is GoogleTest source, compiled here with this project's compiler
# and GoogleTest. Elemental does not ship GoogleTest, so the project brings
# its own: an installed one, or a source tree built as part of this build.
set(GOOGLETEST_SOURCE_DIR "" CACHE PATH
    "Build GoogleTest from this source tree instead of finding an installed one")
if(GOOGLETEST_SOURCE_DIR)
  set(INSTALL_GTEST OFF)
  set(BUILD_GMOCK OFF)
  add_subdirectory("${GOOGLETEST_SOURCE_DIR}" googletest EXCLUDE_FROM_ALL)
else()
  find_package(GTest REQUIRED)
endif()

# The fixture is the one Elemental's own tests use for the ring.
add_executable(ring_contract
  backend_contract_test.cpp
  ../../modules/ring/tests/fixture_ring.c)
target_include_directories(ring_contract PRIVATE ../../modules/ring/tests)
target_link_libraries(ring_contract PRIVATE elemental::ring elemental::io_contract GTest::gtest_main)
