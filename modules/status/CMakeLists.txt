elemental_add_module(status
  SOURCES src/status.c)

if(COMMAND elemental_add_test)
  elemental_add_test(status.unit
    SOURCES tests/status_test.cpp tests/status_cases.c
    LIBRARIES elemental::status)
endif()

# EL_NODISCARD: using a status compiles with -Werror=unused-result, and a
# call that drops one is rejected with an unused-result diagnostic. The
# compiler is run directly, once per language: as C11 the macro is the GNU
# attribute, as C++17 and as C23 it is [[nodiscard]]. Host only.
if(COMMAND elemental_add_test AND NOT ELEMENTAL_TEST_IMAGE
   AND CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
  include(CheckCCompilerFlag)
  check_c_compiler_flag(-std=c23 ELEMENTAL_COMPILER_HAS_STD_C23)
  set(nodiscard_languages c11 cxx17)
  set(nodiscard_compiler_c11 "${CMAKE_C_COMPILER}")
  set(nodiscard_flags_c11 -std=c11)
  set(nodiscard_compiler_cxx17 "${CMAKE_CXX_COMPILER}")
  set(nodiscard_flags_cxx17 -x c++ -std=c++17)
  if(ELEMENTAL_COMPILER_HAS_STD_C23)
    list(APPEND nodiscard_languages c23)
    set(nodiscard_compiler_c23 "${CMAKE_C_COMPILER}")
    set(nodiscard_flags_c23 -std=c23)
  endif()
  foreach(language IN LISTS nodiscard_languages)
    foreach(check IN ITEMS accepts rejects)
      add_test(NAME status.nodiscard_check.${language}.${check}
        COMMAND "${CMAKE_COMMAND}"
                -D "COMPILER=${nodiscard_compiler_${language}}"
                -D "FLAGS=${nodiscard_flags_${language}}"
                -D "SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/nodiscard_check_${check}.c"
                -D "OUTPUT=${CMAKE_CURRENT_BINARY_DIR}/nodiscard_check_${language}_${check}.o"
                -D "INCLUDES=${CMAKE_CURRENT_SOURCE_DIR}/include"
                -D "EXPECT=${check}"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/nodiscard_check.cmake")
    endforeach()
  endforeach()
endif()
