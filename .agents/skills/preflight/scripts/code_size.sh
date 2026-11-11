#!/bin/sh
# Measure the numbers in docs/build.md, "Code size of the assertion
# configurations": text bytes of each Elemental library built for a Zephyr
# Cortex-M3 board (mps2/an385) by the Zephyr SDK's GCC at Zephyr's default
# size optimization, in the three assertion configurations.
#
#   .agents/skills/preflight/scripts/code_size.sh
#
# Needs the Zephyr workspace of docs/build.md, "Tests on Zephyr", with its
# virtual environment active and ZEPHYR_BASE set. Builds in a temporary directory and leaves the
# source tree and build/ alone.
set -eu
cd "$(dirname "$0")/../../../.."
app=$(pwd)/tests/zephyr

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

# Every configuration keeps the test image's IO_POSIX setting.
for configuration in off trap application; do
    cp "$app/elemental_defconfig" "$work/${configuration}_defconfig"
done
printf '# EL_CONFIG_ASSERT is not set\n' >> "$work/off_defconfig"
printf 'EL_CONFIG_ASSERT_HANDLER_APPLICATION=y\n' >> "$work/application_defconfig"

cd "${ZEPHYR_BASE:?set ZEPHYR_BASE to the Zephyr checkout of the workspace}"
for configuration in off trap application; do
    west build -b mps2/an385 -d "$work/$configuration" "$app" -- \
        -DELEMENTAL_ZEPHYR_TESTS=OFF \
        -DELEMENTAL_DEFCONFIG="$work/${configuration}_defconfig" > "$work/log" 2>&1 ||
        { cat "$work/log"; exit 1; }
    size=$(sed -n 's/^CMAKE_NM:FILEPATH=\(.*\)nm$/\1size/p' "$work/$configuration/CMakeCache.txt")
    for library in $(find "$work/$configuration/elemental/modules" -name 'libelemental_*.a' | sort); do
        text=$("$size" -t "$library" | awk '/TOTALS/ { print $1 }')
        printf '%-12s %-28s %6s text bytes\n' "$configuration" "$(basename "$library")" "$text"
    done
done
