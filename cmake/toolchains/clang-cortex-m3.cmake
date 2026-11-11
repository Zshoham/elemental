# Freestanding ARM Cortex-M3, built with Clang and linked with LLD.
#
# Needs only an LLVM installation with the ARM backend: clang, ld.lld,
# llvm-ar, llvm-ranlib. There is no C library, no vendor SDK, and no
# compiler runtime library (compiler-rt builtins or libgcc) on the link line,
# which is the point: whatever Elemental needs from its environment shows up
# as an undefined symbol and is checked against an allowlist (tests/cross).
#
# Nothing is linked into an image with this toolchain: it builds the
# libraries and checks their symbols.

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)

find_program(ELEMENTAL_CLANG clang REQUIRED)
set(CMAKE_C_COMPILER "${ELEMENTAL_CLANG}")
set(CMAKE_C_COMPILER_TARGET thumbv7m-none-eabi)

find_program(CMAKE_AR llvm-ar REQUIRED)
find_program(CMAKE_RANLIB llvm-ranlib REQUIRED)

# Without a C library there is nothing to link a test executable against.
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# These describe the target, so they belong to the toolchain rather than to
# any project. -ffreestanding: no hosted library is assumed.
# -ffunction-sections/-fdata-sections with --gc-sections: the image carries
# only what it uses, which is what makes the size numbers meaningful.
set(CMAKE_C_FLAGS_INIT
    "-mcpu=cortex-m3 -mthumb -ffreestanding -ffunction-sections -fdata-sections")
set(CMAKE_EXE_LINKER_FLAGS_INIT "-nostdlib -fuse-ld=lld -Wl,--gc-sections")

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
