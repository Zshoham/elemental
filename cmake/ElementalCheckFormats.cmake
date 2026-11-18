# Check literal printf formats in the target's C and C++ compilations.
#
# elemental_check_formats(target [LIBC] [FUNCTIONS name:format_arg ...])
# Format argument positions start at 1.

function(elemental_check_formats target)
  cmake_parse_arguments(PARSE_ARGV 1 arg "LIBC" "" "FUNCTIONS")
  if(arg_UNPARSED_ARGUMENTS OR arg_KEYWORDS_MISSING_VALUES)
    message(FATAL_ERROR
      "elemental_check_formats(${target}): expected [LIBC] [FUNCTIONS name:format_arg ...]")
  endif()
  if(NOT TARGET "${target}")
    message(FATAL_ERROR "elemental_check_formats(${target}): target does not exist")
  endif()
  get_target_property(type "${target}" TYPE)
  get_target_property(imported "${target}" IMPORTED)
  get_target_property(alias "${target}" ALIASED_TARGET)
  if(alias OR imported OR NOT type MATCHES "^(EXECUTABLE|(STATIC|SHARED|MODULE|OBJECT)_LIBRARY)$")
    message(FATAL_ERROR "elemental_check_formats(${target}): expected a target that compiles sources")
  endif()
  get_target_property(checked "${target}" ELEMENTAL_CHECK_FORMATS)
  if(checked)
    message(FATAL_ERROR "elemental_check_formats(${target}): already enabled")
  endif()
  if(NOT CMAKE_GENERATOR MATCHES "Ninja|Makefiles")
    message(FATAL_ERROR "elemental_check_formats requires a Ninja or Makefiles generator")
  endif()

  set(options "")
  if(arg_LIBC)
    list(APPEND options --libc)
  endif()
  foreach(function IN LISTS arg_FUNCTIONS)
    if(NOT function MATCHES "^[A-Za-z_][A-Za-z0-9_]*:[1-9][0-9]*$")
      message(FATAL_ERROR "elemental_check_formats(${target}): expected name:format_arg, got '${function}'")
    endif()
    list(APPEND options --function "${function}")
  endforeach()

  # Installed beside this module. In a source tree it lives under tools/.
  set(checker "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/check_formats.py")
  if(NOT EXISTS "${checker}")
    set(checker "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../tools/check_formats.py")
  endif()
  find_package(Python3 3.9 REQUIRED COMPONENTS Interpreter)

  get_property(languages GLOBAL PROPERTY ENABLED_LANGUAGES)
  foreach(lang IN ITEMS C CXX)
    if(NOT lang IN_LIST languages)
      continue()
    endif()
    if(NOT CMAKE_${lang}_COMPILER_ID MATCHES "^(GNU|Clang|AppleClang)$"
       OR CMAKE_${lang}_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
      message(FATAL_ERROR "elemental_check_formats requires a GCC- or Clang-style ${lang} compiler")
    endif()
    set(launcher "${Python3_EXECUTABLE}" "${checker}" ${options} --compile)
    if(lang STREQUAL "CXX")
      list(APPEND launcher --cxx)
    endif()
    list(APPEND launcher --)
    get_target_property(previous "${target}" ${lang}_COMPILER_LAUNCHER)
    if(previous)
      list(APPEND launcher ${previous})
    endif()
    set_property(TARGET "${target}" PROPERTY ${lang}_COMPILER_LAUNCHER
                 ${launcher} --compiler)
  endforeach()

  # Makefiles do not rebuild objects just because a launcher command changed.
  # Put the checker's revision and options in the compile flags so that
  # opting in or changing the rules checks existing objects too.
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${checker}")
  file(SHA256 "${checker}" revision)
  string(SHA256 signature "${revision};${options}")
  target_compile_definitions("${target}" PRIVATE
    "$<$<COMPILE_LANGUAGE:C,CXX>:EL_FORMAT_CHECK_SIGNATURE=${signature}>")
  set_property(TARGET "${target}" PROPERTY ELEMENTAL_CHECK_FORMATS TRUE)
endfunction()
