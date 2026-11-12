# Evidence that the C contracts are usable from other languages. These are
# not bindings. Each one is labeled with what it actually verifies.

string(FIND "${CMAKE_C_FLAGS}" "-fsanitize" sanitizer_flag_position)
set(sanitized FALSE)
if(NOT sanitizer_flag_position EQUAL -1)
  set(sanitized TRUE)
endif()

# --- C++: compiled, linked, and run -----------------------------------------
if(EL_CONFIG_IO)
  add_executable(test_interop_cxx cxx_smoke.cpp)
  if(EL_CONFIG_ASSERT_HANDLER_APPLICATION)
    target_sources(test_interop_cxx PRIVATE
      "${PROJECT_SOURCE_DIR}/tests/support/assert_handler_abort.c")
  endif()
  set_target_properties(test_interop_cxx PROPERTIES
    CXX_STANDARD 11 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
  target_link_libraries(test_interop_cxx PRIVATE elemental::io)
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(test_interop_cxx PRIVATE
      $<$<COMPILE_LANGUAGE:CXX>:-Wall;-Wextra;-Wpedantic;-Werror>)
  endif()
  add_test(NAME interop.cxx COMMAND test_interop_cxx)
endif()

# --- Rust: compiled, linked against the C libraries, and run ----------------
# Skipped under sanitizers: the instrumented C libraries need a sanitizer
# runtime that a plain rustc link does not provide.
find_program(ELEMENTAL_RUSTC rustc)
if(ELEMENTAL_RUSTC AND EL_CONFIG_IO AND EL_CONFIG_FMT AND EL_CONFIG_CRC AND EL_CONFIG_BYTES
   AND NOT sanitized)
  set(rust_binary "${CMAKE_CURRENT_BINARY_DIR}/rust_sketch")
  add_custom_command(
    OUTPUT "${rust_binary}"
    COMMAND "${ELEMENTAL_RUSTC}" --edition 2021 -D warnings
            -o "${rust_binary}" "${CMAKE_CURRENT_SOURCE_DIR}/rust_sketch.rs"
            -L "native=$<TARGET_FILE_DIR:elemental_io>"
            -L "native=$<TARGET_FILE_DIR:elemental_status>"
            -L "native=$<TARGET_FILE_DIR:elemental_fmt>"
            -L "native=$<TARGET_FILE_DIR:elemental_crc>"
            -L "native=$<TARGET_FILE_DIR:elemental_bytes>"
            -l static=elemental_io -l static=elemental_fmt -l static=elemental_crc
            -l static=elemental_bytes -l static=elemental_status
    DEPENDS rust_sketch.rs elemental_io elemental_fmt elemental_crc elemental_bytes
            elemental_status
    COMMENT "Compiling and linking the Rust interop sketch"
    VERBATIM)
  add_custom_target(interop_rust_sketch ALL DEPENDS "${rust_binary}")
  add_test(NAME interop.rust COMMAND "${rust_binary}")
else()
  message(STATUS "Rust interop sketch not built (no rustc, a module it uses disabled, or sanitizers on)")
endif()

# The allocator contract next to Rust's GlobalAlloc, in both directions.
if(ELEMENTAL_RUSTC AND EL_CONFIG_IO_ALLOC AND NOT sanitized)
  set(rust_alloc_binary "${CMAKE_CURRENT_BINARY_DIR}/rust_alloc_sketch")
  add_custom_command(
    OUTPUT "${rust_alloc_binary}"
    COMMAND "${ELEMENTAL_RUSTC}" --edition 2021 -D warnings
            -o "${rust_alloc_binary}" "${CMAKE_CURRENT_SOURCE_DIR}/rust_alloc_sketch.rs"
            -L "native=$<TARGET_FILE_DIR:elemental_alloc>"
            -L "native=$<TARGET_FILE_DIR:elemental_io>"
            -L "native=$<TARGET_FILE_DIR:elemental_status>"
            -l static=elemental_io -l static=elemental_alloc
            -l static=elemental_status
    DEPENDS rust_alloc_sketch.rs elemental_io elemental_alloc elemental_status
    COMMENT "Compiling and linking the Rust allocator sketch"
    VERBATIM)
  add_custom_target(interop_rust_alloc_sketch ALL DEPENDS "${rust_alloc_binary}")
  add_test(NAME interop.rust_alloc COMMAND "${rust_alloc_binary}")
else()
  message(STATUS "Rust allocator sketch not built (no rustc, IO_ALLOC disabled, or sanitizers on)")
endif()

# --- Python: compile check only ---------------------------------------------
# The extension source is compiled against the CPython headers. It is not
# linked or imported: that needs Elemental built as position-independent
# code, which is a property of the consumer's build (see docs/conventions.md).
find_package(Python3 COMPONENTS Interpreter Development.Module QUIET)
if(Python3_Development.Module_FOUND AND EL_CONFIG_IO)
  add_library(interop_python_sketch OBJECT python_ext_sketch.c)
  target_link_libraries(interop_python_sketch PRIVATE Python3::Module elemental::io)
  elemental_target_c_standard(interop_python_sketch)
  if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(interop_python_sketch PRIVATE -Wall -Wextra -Werror)
  endif()
else()
  message(STATUS "Python interop sketch not compiled (no CPython development headers)")
endif()
