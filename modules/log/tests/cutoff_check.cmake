# Compiles SOURCE with EL_LOG_CUTOFF set to CUTOFF (or not at all when
# CUTOFF is "default") and reads the object file: the format string of each
# level in KEPT must be in it, and that of every other level must not. With
# nothing kept, the object must not refer to el_log_printf or
# el_log_compose at all.
#
# The same holds for the text of each level's composed message, which a
# compose function writes and a call under EL_LOG_KEPT makes, with one
# difference: the compose function is a separate function, and a compiler
# that does not optimize keeps a static function nothing calls any more, with
# its text. So a cut-off message's text must be absent only from an
# optimized object. Whether each level's call is there is checked on its
# own, optimized or not: SOURCE is compiled again once per level with
# CUTOFF_CHECK_PROBE set to it, which leaves that level's compose call
# alone in the object, and the object must refer to el_log_compose exactly
# when the level is in KEPT.
#
# FLAGS carries the language and the optimization level; the check is run
# without optimization too, because the cut-off must not depend on it
# (docs/adr/0021-log-cutoff.md).
cmake_minimum_required(VERSION 3.22)

set(include_flags "")
foreach(dir IN LISTS INCLUDES)
  list(APPEND include_flags "-I${dir}")
endforeach()
set(definitions "")
if(NOT CUTOFF STREQUAL "default")
  list(APPEND definitions "-DEL_LOG_CUTOFF=${CUTOFF}")
endif()
set(optimized TRUE)
if("-O0" IN_LIST FLAGS)
  set(optimized FALSE)
endif()

# Compile SOURCE into `object` with the extra definitions that follow.
function(compile object)
  execute_process(
    COMMAND "${COMPILER}" ${FLAGS} -c -o "${object}" ${definitions} ${ARGN} ${include_flags}
            "${SOURCE}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE output)
  if(NOT result EQUAL 0)
    message(FATAL_ERROR "expected a clean compile of ${SOURCE}:\n${output}")
  endif()
endfunction()

compile("${OUTPUT}")

file(STRINGS "${OUTPUT}" strings REGEX "cutoff-marker-|el_log_printf|el_log_compose")
foreach(kind IN ITEMS "" "compose-")
  foreach(level IN ITEMS debug info warn error)
    set(what "${kind}${level} call")
    string(FIND "${strings}" "cutoff-marker-${kind}${level}" found)
    if(level IN_LIST KEPT)
      if(found EQUAL -1)
        message(FATAL_ERROR "EL_LOG_CUTOFF=${CUTOFF}: the ${what} is missing from the object")
      endif()
    elseif(NOT found EQUAL -1 AND (optimized OR kind STREQUAL ""))
      message(FATAL_ERROR "EL_LOG_CUTOFF=${CUTOFF}: the ${what}'s text is still in the object")
    endif()
  endforeach()
endforeach()

foreach(function IN ITEMS el_log_printf el_log_compose)
  string(FIND "${strings}" "${function}" found)
  if(KEPT)
    if(found EQUAL -1)
      message(FATAL_ERROR "EL_LOG_CUTOFF=${CUTOFF}: the object does not call ${function}")
    endif()
  elseif(NOT found EQUAL -1)
    message(FATAL_ERROR "EL_LOG_CUTOFF=${CUTOFF}: the object still refers to ${function}")
  endif()
endforeach()

foreach(level IN ITEMS debug info warn error)
  string(TOUPPER "${level}" level_upper)
  set(probe "${OUTPUT}.${level}.o")
  compile("${probe}" "-DCUTOFF_CHECK_PROBE=EL_LOG_LEVEL_${level_upper}")
  file(STRINGS "${probe}" probe_strings REGEX "el_log_compose")
  string(FIND "${probe_strings}" "el_log_compose" found)
  if(level IN_LIST KEPT)
    if(found EQUAL -1)
      message(FATAL_ERROR "EL_LOG_CUTOFF=${CUTOFF}: the compose-${level} call is missing")
    endif()
  elseif(NOT found EQUAL -1)
    message(FATAL_ERROR "EL_LOG_CUTOFF=${CUTOFF}: the compose-${level} call is still made")
  endif()
endforeach()
