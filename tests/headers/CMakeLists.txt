# Every public C header must compile on its own, be safe to include twice,
# and be valid C11 and C++11, with strict diagnostics. The check is the
# build: one translation unit per header and language. Headers are
# discovered, so a new header is covered without touching this file.
#
# "*.h" is the C interface. A contract suite's "*.hpp" is C++17 with
# GoogleTest, by design, and is exercised by the tests that use it.

# The only headers allowed to depend on the configuration. Adding to this
# list is a design decision: see docs/adr/0004-private-configuration.md.
set(config_dependent_headers elemental/assert.h)

get_property(include_dirs GLOBAL PROPERTY ELEMENTAL_PUBLIC_INCLUDE_DIRS)
get_property(modules GLOBAL PROPERTY ELEMENTAL_MODULES)

# The headers of backends this build cannot have, because they need their
# platform to build, but which include none of their platform's headers, so
# a host compiles them: io_zephyr_uart.h and ring_zephyr_uart.h declare
# `struct device` and nothing more. Each is checked here, against its
# module, when the build has that module, unless the build has the backend
# and its directory is already in the list.
foreach(module io ring)
  set(dir "${PROJECT_SOURCE_DIR}/modules/${module}/backends/zephyr_uart/include")
  if(TARGET elemental::${module} AND NOT dir IN_LIST include_dirs)
    list(APPEND include_dirs "${dir}")
  endif()
endforeach()

set(headers "")
foreach(dir IN LISTS include_dirs)
  file(GLOB_RECURSE found RELATIVE "${dir}" CONFIGURE_DEPENDS "${dir}/*.h")
  list(APPEND headers ${found})
endforeach()
list(SORT headers)
# Headers of parts of a module that are switched off (OMIT_HEADERS).
get_property(omitted GLOBAL PROPERTY ELEMENTAL_OMITTED_HEADERS)
if(omitted)
  list(REMOVE_ITEM headers ${omitted})
endif()

function(write_header_check header extension out_var)
  string(MAKE_C_IDENTIFIER "${header}" id)
  set(file "${CMAKE_CURRENT_BINARY_DIR}/${id}.${extension}")
  set(text "")
  if(header IN_LIST config_dependent_headers)
    string(APPEND text "#include \"elemental_config.h\"\n")
  endif()
  string(APPEND text
    "#include <${header}>\n#include <${header}>\n"
    "typedef int el_header_check_${id};\n")
  file(CONFIGURE OUTPUT "${file}" CONTENT "${text}" @ONLY)
  set(${out_var} "${file}" PARENT_SCOPE)
endfunction()

set(strict_flags -Wall -Wextra -Wpedantic -Werror)

set(c_sources "")
foreach(header IN LISTS headers)
  write_header_check("${header}" c source)
  list(APPEND c_sources "${source}")
endforeach()

add_library(header_check_c OBJECT ${c_sources})
elemental_target_c_standard(header_check_c)
elemental_target_warnings(header_check_c)
target_include_directories(header_check_c PRIVATE
  ${ELEMENTAL_CONFIG_INCLUDE_DIRS} ${include_dirs})
add_dependencies(header_check_c ${ELEMENTAL_CONFIG_TARGET})
foreach(module IN LISTS modules)
  target_link_libraries(header_check_c PRIVATE elemental::${module})
endforeach()
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(header_check_c PRIVATE ${strict_flags})
endif()

set(cxx_sources "")
foreach(header IN LISTS headers)
  write_header_check("${header}" cpp source)
  list(APPEND cxx_sources "${source}")
endforeach()

add_library(header_check_cxx OBJECT ${cxx_sources})
set_target_properties(header_check_cxx PROPERTIES
  CXX_STANDARD 11 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
target_include_directories(header_check_cxx PRIVATE
  ${ELEMENTAL_CONFIG_INCLUDE_DIRS} ${include_dirs})
add_dependencies(header_check_cxx ${ELEMENTAL_CONFIG_TARGET})
foreach(module IN LISTS modules)
  target_link_libraries(header_check_cxx PRIVATE elemental::${module})
endforeach()
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(header_check_cxx PRIVATE ${strict_flags})
endif()

# A status must be used, and the compiler can only say so where the
# declaration carries EL_NODISCARD. Every header under modules/ and
# examples/ is read, private ones included; new headers are covered without
# touching this file.
add_test(NAME headers.nodiscard
  COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/check_nodiscard.py"
          "${PROJECT_SOURCE_DIR}/modules" "${PROJECT_SOURCE_DIR}/examples")
