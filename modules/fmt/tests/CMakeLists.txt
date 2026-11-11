# The core calls and the printf front end, against literal expectations.
# These are the tests meant to run on a target too.
elemental_add_test(fmt.unit
  SOURCES fmt_test.cpp fmt_printf_test.cpp
  LIBRARIES elemental::fmt)

# Host only: the printf front end against glibc's snprintf, and decimal
# conversion against the compiler's own division.
elemental_add_test(fmt.oracle
  SOURCES fmt_oracle_test.cpp
  LIBRARIES elemental::fmt)

# With assertions compiled out there is nothing to observe.
if(EL_CONFIG_ASSERT)
  elemental_add_test(fmt.violations
    SOURCES fmt_violations_test.cpp
    LIBRARIES elemental::fmt)
endif()

# The format attribute: a correct call compiles with -Werror=format, and a
# mismatched argument is rejected with a format diagnostic. The compiler is
# run directly, so each check is one compiler invocation.
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
  foreach(check IN ITEMS accepts rejects)
    add_test(NAME fmt.format_check.${check}
      COMMAND "${CMAKE_COMMAND}"
              -D "COMPILER=${CMAKE_C_COMPILER}"
              -D "SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/format_check_${check}.c"
              -D "INCLUDES=${PROJECT_SOURCE_DIR}/modules/fmt/include;${PROJECT_SOURCE_DIR}/modules/status/include"
              -D "EXPECT=${check}"
              -P "${CMAKE_CURRENT_SOURCE_DIR}/format_check.cmake")
  endforeach()
endif()
