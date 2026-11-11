# An application that vendors Elemental's source tree and uses one module.
#
# Needs at configure time: Python >= 3.9 and the pinned Kconfiglib (run
# `cmake -P tools/provision.cmake` once in the Elemental tree).
cmake_minimum_required(VERSION 3.22)
project(consume_subdirectory LANGUAGES C)

# Here the tree is two directories up. In a real project it is a vendored
# copy, a git submodule, or a FetchContent download.
set(ELEMENTAL_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../.." CACHE PATH
    "The Elemental source tree")

# The application decides the configuration. This one only needs streams
# over memory, so its defconfig turns the POSIX backend off.
set(ELEMENTAL_DEFCONFIG "${CMAKE_CURRENT_SOURCE_DIR}/elemental_defconfig" CACHE FILEPATH "")

# EXCLUDE_FROM_ALL: build only what this project links. Elemental adds no
# tests, examples, or install rules when it is not the top-level project.
add_subdirectory("${ELEMENTAL_SOURCE_DIR}" elemental EXCLUDE_FROM_ALL)

add_executable(message_builder main.c)
target_link_libraries(message_builder PRIVATE elemental::io)
