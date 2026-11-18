# The conversions and printf into a buffer and a buffered writer, against
# literal expectations, the proposal's examples included. These are the tests
# meant to run on a target too, in every printf configuration.
elemental_add_test(fmt.unit
  SOURCES fmt_test.cpp fmt_printf_test.cpp fmt_printf_buffered_test.cpp
  LIBRARIES elemental::fmt)

# printf into grown storage: only with IO_ALLOC.
if(EL_CONFIG_IO_ALLOC)
  elemental_add_test(fmt.unit.grown
    SOURCES fmt_printf_grown_test.cpp
    LIBRARIES elemental::fmt elemental::alloc)
endif()

# Host only (not in a test image such as tests/zephyr): the printf front end
# against glibc's snprintf, which a target's libc does not match, and decimal
# conversion against the compiler's own division, two million values deep.
if(NOT ELEMENTAL_TEST_IMAGE)
  elemental_add_test(fmt.oracle
    SOURCES fmt_oracle_test.cpp
    LIBRARIES elemental::fmt)
endif()

# With assertions compiled out there is nothing to observe, and without a
# process to fork (on a target) there is no death test.
if(EL_CONFIG_ASSERT AND ELEMENTAL_DEATH_TESTS)
  elemental_add_test(fmt.violations
    SOURCES fmt_violations_test.cpp
    LIBRARIES elemental::fmt)
  # Which specifications are violations depends on the printf option.
  if(EL_CONFIG_FMT_PRINTF_LIBC)
    target_compile_definitions(test_fmt_violations PRIVATE FMT_TEST_PRINTF_LIBC=1)
  else()
    target_compile_definitions(test_fmt_violations PRIVATE FMT_TEST_PRINTF_LIBC=0)
  endif()
endif()

# FMT_PRINTF_LIBC: the wider set against the platform's snprintf. Also in a
# test image, where it runs against that C library (picolibc on Zephyr).
if(EL_CONFIG_FMT_PRINTF_LIBC)
  elemental_add_test(fmt.libc
    SOURCES fmt_libc_test.cpp
    LIBRARIES elemental::fmt)
endif()

# The format attribute: a correct call compiles with -Werror=format, and a
# mismatched argument is rejected with a format diagnostic. The compiler is
# run directly, so each check is one compiler invocation. Host only.
if(NOT ELEMENTAL_TEST_IMAGE AND CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
  foreach(check IN ITEMS accepts rejects)
    add_test(NAME fmt.format_check.${check}
      COMMAND "${CMAKE_COMMAND}"
              -D "COMPILER=${CMAKE_C_COMPILER}"
              -D "SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/format_check_${check}.c"
              -D "INCLUDES=${PROJECT_SOURCE_DIR}/modules/fmt/include;${PROJECT_SOURCE_DIR}/modules/io/include;${PROJECT_SOURCE_DIR}/modules/status/include"
              -D "EXPECT=${check}"
              -P "${CMAKE_CURRENT_SOURCE_DIR}/format_check.cmake")
  endforeach()
endif()

# Literal formats through the target's preprocessor. The oracle runs the
# checked calls against the builtin parser and snprintf.
if(NOT ELEMENTAL_TEST_IMAGE AND CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
  set(check_cases accepts rejects libc cxx build)
  set(oracle_arg "")
  if(NOT EL_CONFIG_FMT_PRINTF_LIBC)
    add_executable(test_fmt_portable_builtin portable_check_oracle.c)
    target_link_libraries(test_fmt_portable_builtin PRIVATE elemental::fmt)
    elemental_target_c_standard(test_fmt_portable_builtin)
    elemental_target_warnings(test_fmt_portable_builtin)
    if(EL_CONFIG_ASSERT_HANDLER_APPLICATION)
      target_sources(test_fmt_portable_builtin PRIVATE
        "${PROJECT_SOURCE_DIR}/tests/support/assert_handler_abort.c")
    endif()
    list(APPEND check_cases oracle)
    set(oracle_arg --oracle "$<TARGET_FILE:test_fmt_portable_builtin>")
  endif()
  foreach(check IN LISTS check_cases)
    add_test(NAME fmt.portable_check.${check}
      COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/portable_check.py" ${check}
              --source "${PROJECT_SOURCE_DIR}"
              --compiler "${CMAKE_C_COMPILER}"
              --cxx-compiler "${CMAKE_CXX_COMPILER}"
              --generator "${CMAKE_GENERATOR}"
              --build "${CMAKE_CURRENT_BINARY_DIR}/portable_check_build"
              --kconfiglib "${ELEMENTAL_KCONFIGLIB_DIR}"
              ${oracle_arg})
  endforeach()
endif()
