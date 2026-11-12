# Elemental from the outside: consumption, configuration, and diagnostics.
# Each case builds throwaway projects under this directory; see the
# docstrings in integration.py for what each one asserts.
set(cases subdirectory installed raw_source regenerate isolation diagnostics provenance
          module_subsets choices)

foreach(case IN LISTS cases)
  add_test(
    NAME integration.${case}
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/integration.py" ${case}
            --source "${PROJECT_SOURCE_DIR}"
            --work "${CMAKE_CURRENT_BINARY_DIR}/${case}"
            --generator "${CMAKE_GENERATOR}"
            --c-compiler "${CMAKE_C_COMPILER}"
            --cxx-compiler "${CMAKE_CXX_COMPILER}"
            --kconfiglib "${ELEMENTAL_KCONFIGLIB_DIR}"
            --googletest "${ELEMENTAL_GOOGLETEST_DIR}")
  set_tests_properties(integration.${case} PROPERTIES LABELS integration TIMEOUT 600)
endforeach()
