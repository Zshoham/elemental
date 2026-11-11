# An application and an out-of-tree backend that use an installed Elemental.
#
# Nothing here runs Kconfig or needs Python: the installed package is one
# frozen configuration.
#
#   cmake -S . -B build -DCMAKE_PREFIX_PATH=<elemental install prefix>
cmake_minimum_required(VERSION 3.22)
project(consume_installed LANGUAGES C CXX)

# Components are module names. Asking for one the package was configured
# without fails here, with a message naming it.
find_package(Elemental 0.1 REQUIRED COMPONENTS io io_posix io_contract)

# The application author's view: copy standard input to standard output.
# This part needs no C++ and no GoogleTest.
add_executable(el_cat main.c)
target_link_libraries(el_cat PRIVATE elemental::io_posix)

# The backend author's view: a backend kept outside Elemental's repository,
# verified with the contract suite the package installs.
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

add_executable(ring_pipe_contract
  backend_contract_test.cpp
  ../backend_ring/ring_pipe.c
  ../backend_ring/ring_pipe_fixture.c)
target_include_directories(ring_pipe_contract PRIVATE ../backend_ring)
target_link_libraries(ring_pipe_contract PRIVATE elemental::io_contract GTest::gtest_main)
