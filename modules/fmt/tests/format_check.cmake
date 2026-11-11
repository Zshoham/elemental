# Compiles SOURCE with -Werror=format and checks the outcome against EXPECT:
# "accepts" must compile; "rejects" must fail, with a format diagnostic, so
# that an unrelated error (a missing header) does not pass for a rejection.
set(include_flags "")
foreach(dir IN LISTS INCLUDES)
  list(APPEND include_flags "-I${dir}")
endforeach()

execute_process(
  COMMAND "${COMPILER}" -std=c11 -fsyntax-only -Wformat -Werror=format
          ${include_flags} "${SOURCE}"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output
  ERROR_VARIABLE output)

if(EXPECT STREQUAL "accepts")
  if(NOT result EQUAL 0)
    message(FATAL_ERROR "expected a clean compile of ${SOURCE}:\n${output}")
  endif()
elseif(EXPECT STREQUAL "rejects")
  if(result EQUAL 0)
    message(FATAL_ERROR "expected ${SOURCE} to be rejected, but it compiled")
  endif()
  # GCC: [-Werror=format=]; Clang: [-Werror,-Wformat]
  if(NOT output MATCHES "Werror=format|Wformat")
    message(FATAL_ERROR "rejected, but not by the format check:\n${output}")
  endif()
else()
  message(FATAL_ERROR "EXPECT must be accepts or rejects")
endif()
