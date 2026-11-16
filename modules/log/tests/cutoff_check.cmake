# Compiles SOURCE with EL_LOG_CUTOFF set to CUTOFF (or not at all when
# CUTOFF is "default") and reads the object file: the format string of each
# level in KEPT must be in it, and that of every other level must not. With
# nothing kept, the object must not refer to el_log_printf at all. FLAGS
# carries the language and the optimization level; the check is run without
# optimization too, because the cut-off must not depend on it
# (docs/adr/0021-log-cutoff.md).
set(include_flags "")
foreach(dir IN LISTS INCLUDES)
  list(APPEND include_flags "-I${dir}")
endforeach()
set(definitions "")
if(NOT CUTOFF STREQUAL "default")
  list(APPEND definitions "-DEL_LOG_CUTOFF=${CUTOFF}")
endif()

execute_process(
  COMMAND "${COMPILER}" ${FLAGS} -c -o "${OUTPUT}" ${definitions} ${include_flags} "${SOURCE}"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output
  ERROR_VARIABLE output)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "expected a clean compile of ${SOURCE}:\n${output}")
endif()

file(STRINGS "${OUTPUT}" strings REGEX "cutoff-marker-|el_log_printf")
foreach(level IN ITEMS debug info warn error)
  string(FIND "${strings}" "cutoff-marker-${level}" found)
  if(level IN_LIST KEPT)
    if(found EQUAL -1)
      message(FATAL_ERROR "EL_LOG_CUTOFF=${CUTOFF}: the ${level} call is missing from the object")
    endif()
  elseif(NOT found EQUAL -1)
    message(FATAL_ERROR "EL_LOG_CUTOFF=${CUTOFF}: the ${level} call's format string is still in the object")
  endif()
endforeach()

string(FIND "${strings}" "el_log_printf" found)
if(KEPT)
  if(found EQUAL -1)
    message(FATAL_ERROR "EL_LOG_CUTOFF=${CUTOFF}: the object does not call el_log_printf")
  endif()
elseif(NOT found EQUAL -1)
  message(FATAL_ERROR "EL_LOG_CUTOFF=${CUTOFF}: the object still refers to el_log_printf")
endif()
