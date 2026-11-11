#!/bin/sh
# Measure the numbers in docs/build.md, "Code size of the assertion
# configurations": text bytes of each freestanding library on the Cortex-M3
# toolchain at MinSizeRel, in the three assertion configurations.
#
#   .agents/skills/preflight/scripts/code_size.sh
#
# Builds in a temporary directory and leaves the source tree and build/ alone.
# Needs the provisioned Kconfiglib, clang, ld.lld, llvm-ar, and llvm-size.
set -eu
cd "$(dirname "$0")/../../../.."

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

printf '# EL_CONFIG_ASSERT is not set\n' > "$work/off_defconfig"
: > "$work/trap_defconfig"
printf 'EL_CONFIG_ASSERT_HANDLER_APPLICATION=y\n' > "$work/application_defconfig"

for configuration in off trap application; do
    cmake -S . -B "$work/$configuration" -G Ninja \
        --toolchain cmake/toolchains/clang-cortex-m3.cmake \
        -DCMAKE_BUILD_TYPE=MinSizeRel \
        -DELEMENTAL_DEFCONFIG="$work/${configuration}_defconfig" > "$work/log" 2>&1 ||
        { cat "$work/log"; exit 1; }
    cmake --build "$work/$configuration" > "$work/log" 2>&1 ||
        { cat "$work/log"; exit 1; }
    for library in $(find "$work/$configuration/modules" -name 'libelemental_*.a' | sort); do
        text=$(llvm-size -t "$library" | awk '/TOTALS/ { print $1 }')
        printf '%-12s %-28s %6s text bytes\n' "$configuration" "$(basename "$library")" "$text"
    done
done
