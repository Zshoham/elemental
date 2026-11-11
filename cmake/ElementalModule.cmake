# The rules every Elemental library target follows, in one place.

# Strict diagnostics for Elemental's own sources. Applied PRIVATE, per target:
# nothing here reaches a consumer's compile lines, and nothing is set globally.
function(elemental_target_warnings target)
  if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(${target} PRIVATE
      -Wall -Wextra -Wpedantic
      -Wconversion -Wsign-conversion -Wdouble-promotion
      -Wshadow -Wundef -Wcast-qual -Wcast-align -Wpointer-arith
      -Wvla -Wformat=2 -Wimplicit-fallthrough -Wnull-dereference
      -Wswitch-enum -Wredundant-decls -Wwrite-strings
      # Warnings that exist only for C. Test executables also hold C++.
      $<$<COMPILE_LANGUAGE:C>:-Wstrict-prototypes;-Wmissing-prototypes;-Wold-style-definition>)
    if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
      target_compile_options(${target} PRIVATE
        -Wlogical-op -Wduplicated-cond -Wduplicated-branches
        $<$<COMPILE_LANGUAGE:C>:-Wjump-misses-init>)
    endif()
    if(ELEMENTAL_WARNINGS_AS_ERRORS)
      target_compile_options(${target} PRIVATE -Werror)
    endif()
  elseif(MSVC)
    target_compile_options(${target} PRIVATE /W4)
    if(ELEMENTAL_WARNINGS_AS_ERRORS)
      target_compile_options(${target} PRIVATE /WX)
    endif()
  endif()
endfunction()

# C11 without extensions, for one target.
function(elemental_target_c_standard target)
  set_target_properties(${target} PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF)
endfunction()

# elemental_add_module(<name> SOURCES <file>... [DEPENDS <module>...])
#
# Defines the static library elemental_<name>, reachable as elemental::<name>
# both in this build and from an installed package. Call it from the module's
# own directory: public headers are taken from ./include.
#
#   - The generated configuration header is a PRIVATE include. Public headers
#     never see it (docs/adr/0004-private-configuration.md).
#   - DEPENDS names other Elemental modules and is PUBLIC. A dependency that
#     is not enabled is an error here, not a missing symbol at link time.
function(elemental_add_module name)
  cmake_parse_arguments(PARSE_ARGV 1 arg "" "" "SOURCES;DEPENDS")
  if(arg_UNPARSED_ARGUMENTS OR NOT arg_SOURCES)
    message(FATAL_ERROR "elemental_add_module(${name}): expected SOURCES <file>... [DEPENDS <module>...]")
  endif()

  set(target elemental_${name})
  add_library(${target} STATIC ${arg_SOURCES})
  add_library(elemental::${name} ALIAS ${target})
  set_target_properties(${target} PROPERTIES EXPORT_NAME ${name})
  elemental_target_c_standard(${target})
  elemental_target_warnings(${target})

  target_include_directories(${target}
    PUBLIC
      $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
      $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE
      ${ELEMENTAL_CONFIG_INCLUDE_DIRS})
  add_dependencies(${target} ${ELEMENTAL_CONFIG_TARGET})

  foreach(dependency IN LISTS arg_DEPENDS)
    if(NOT TARGET elemental::${dependency})
      message(FATAL_ERROR
        "Elemental module '${name}' depends on '${dependency}', which is not "
        "part of this configuration. Its Kconfig entry must 'select' or "
        "'depends on' the symbol that enables '${dependency}'.")
    endif()
    target_link_libraries(${target} PUBLIC elemental::${dependency})
  endforeach()

  set_property(GLOBAL APPEND PROPERTY ELEMENTAL_MODULES ${name})
  set_property(GLOBAL APPEND PROPERTY ELEMENTAL_PUBLIC_INCLUDE_DIRS
               "${CMAKE_CURRENT_SOURCE_DIR}/include")

  if(ELEMENTAL_INSTALL)
    install(TARGETS ${target} EXPORT ElementalTargets
            ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
    install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
  endif()
endfunction()

# elemental_add_contract_suite(<name> SOURCES <file>... DEPENDS <module>...)
#
# A contract test suite: the promises of an interface, written as GoogleTest
# tests that any implementation can be run against. Reachable as
# elemental::<name>, in this build and from an installed package.
#
# A suite is distributed as source, not as an archive. It is compiled into
# the test executable that links it, with that build's compiler, flags, and
# GoogleTest, so it cannot disagree with them. The executable provides
# GTest::gtest; Elemental does not ship one.
#
# Call it from the suite's own directory: public headers are taken from
# ./include.
function(elemental_add_contract_suite name)
  cmake_parse_arguments(PARSE_ARGV 1 arg "" "" "SOURCES;DEPENDS")
  if(arg_UNPARSED_ARGUMENTS OR NOT arg_SOURCES OR NOT arg_DEPENDS)
    message(FATAL_ERROR "elemental_add_contract_suite(${name}): expected SOURCES <file>... DEPENDS <module>...")
  endif()

  set(target elemental_${name})
  set(source_destination "${CMAKE_INSTALL_DATADIR}/elemental/${name}")
  add_library(${target} INTERFACE)
  add_library(elemental::${name} ALIAS ${target})
  set_target_properties(${target} PROPERTIES EXPORT_NAME ${name})
  target_compile_features(${target} INTERFACE cxx_std_17)
  target_include_directories(${target} INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)

  foreach(source IN LISTS arg_SOURCES)
    get_filename_component(source_name "${source}" NAME)
    target_sources(${target} INTERFACE
      $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/${source}>
      $<INSTALL_INTERFACE:${source_destination}/${source_name}>)
  endforeach()

  foreach(dependency IN LISTS arg_DEPENDS)
    target_link_libraries(${target} INTERFACE elemental::${dependency})
  endforeach()
  # In this build GoogleTest is a target of the build, which an export set
  # cannot name. The installed package adds the same link by name
  # (cmake/ElementalConfig.cmake.in).
  target_link_libraries(${target} INTERFACE $<BUILD_INTERFACE:GTest::gtest>)

  set_property(GLOBAL APPEND PROPERTY ELEMENTAL_CONTRACT_SUITES ${name})
  set_property(GLOBAL APPEND PROPERTY ELEMENTAL_PUBLIC_INCLUDE_DIRS
               "${CMAKE_CURRENT_SOURCE_DIR}/include")

  if(ELEMENTAL_INSTALL)
    install(TARGETS ${target} EXPORT ElementalTargets)
    install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
    install(FILES ${arg_SOURCES} DESTINATION "${source_destination}")
  endif()
endfunction()
