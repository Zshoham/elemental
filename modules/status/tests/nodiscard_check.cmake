# Compiles SOURCE with -Werror=unused-result and checks the outcome against
# EXPECT: "accepts" must compile; "rejects" must fail, with an unused-result
# diagnostic, so that an unrelated error (a missing header) does not pass for
# a rejection. FLAGS selects the language and its standard. The file is
# compiled, not only parsed: GCC reports a dropped result after parsing.
set(include_flags "")
foreach(dir IN LISTS INCLUDES)
  list(APPEND include_flags "-I${dir}")
endforeach()

execute_process(
  COMMAND "${COMPILER}" ${FLAGS} -c -o "${OUTPUT}" -Werror=unused-result
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
  # GCC: [-Werror=unused-result]; Clang: [-Werror,-Wunused-result]
  if(NOT output MATCHES "Wunused-result|Werror=unused-result")
    message(FATAL_ERROR "rejected, but not for a dropped result:\n${output}")
  endif()
else()
  message(FATAL_ERROR "EXPECT must be accepts or rejects")
endif()
