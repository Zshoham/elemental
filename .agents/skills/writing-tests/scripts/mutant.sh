#!/bin/sh
# Does any test notice when a source file is broken?
#
#   .agents/skills/writing-tests/scripts/mutant.sh <file> <old text> <new text> [<ctest regex>]
#   .agents/skills/writing-tests/scripts/mutant.sh
#
# Copies the source tree to build/mutant/src, replaces the first occurrence
# of <old text> in <file> there, builds the copy, and runs its tests (all
# but the integration cases, or only those matching <ctest regex>). The real
# source tree is never changed. The copy and its build are kept, so the next
# mutant only rebuilds what differs.
#
# Exit 0: the mutant was caught (a test failed, or the build rejected it).
# Exit 1: the mutant survived. Nothing tests the behavior it breaks.
# Exit 2: <old text> was not found, or the unmodified tree does not pass.
#
# With no arguments it builds and tests the unmodified copy, which must pass
# (exit 0): that shows the harness itself is sound. Run it first.
#
# Needs the provisioned dependencies in .deps/ and rsync.
set -eu
cd "$(dirname "$0")/../../../.."
root=$(pwd)
work=build/mutant
mkdir -p "$work"

# Tracked files plus new ones that are not ignored; deletions are mirrored.
git ls-files --cached --others --exclude-standard > "$work/files"
rsync -a --delete --files-from="$work/files" . "$work/src"

# The copy restores the last mutant's file with its original, older time.
# Touch it, or the build would keep the object compiled from the mutant.
if [ -f "$work/mutated" ]; then
    touch "$work/src/$(cat "$work/mutated")"
    rm "$work/mutated"
fi

if [ $# -ge 3 ]; then
    python3 - "$work/src/$1" "$2" "$3" <<'PY' || exit 2
import pathlib, sys
path, old, new = sys.argv[1:4]
target = pathlib.Path(path)
text = target.read_text()
if old not in text:
    sys.exit("mutation target not found in {}: {}".format(path, old))
target.write_text(text.replace(old, new, 1))
PY
    printf '%s\n' "$1" > "$work/mutated"
elif [ $# -ne 0 ]; then
    sed -n '2,6p' "$0" >&2
    exit 2
fi

built=yes
{
    cmake -S "$work/src" -B "$work/build" -G Ninja \
        -DELEMENTAL_KCONFIGLIB_DIR="$root/.deps/kconfiglib" \
        -DELEMENTAL_GOOGLETEST_DIR="$root/.deps/googletest" &&
        cmake --build "$work/build"
} > "$work/build.log" 2>&1 || built=no

if [ "$built" = no ]; then
    if [ $# -eq 0 ]; then
        echo "the unmodified tree does not build; see $work/build.log" >&2
        exit 2
    fi
    echo "caught: the build rejected it"
    grep -m 5 'error' "$work/build.log" | sed 's/^/    /' || true
    exit 0
fi

status=0
ctest --test-dir "$work/build" -LE integration ${4:+-R "$4"} \
    > "$work/test.log" 2>&1 || status=$?

if [ $# -eq 0 ]; then
    [ "$status" -eq 0 ] && { echo "unmodified tree: all tests pass"; exit 0; }
    echo "the unmodified tree fails its tests; see $work/test.log" >&2
    exit 2
fi
if [ "$status" -ne 0 ]; then
    echo "caught by:"
    grep -v '(Skipped)$' "$work/test.log" | sed -n 's/^[[:space:]]*[0-9]* - \(.*\)/    \1/p'
    exit 0
fi
echo "SURVIVED: no test notices this change"
exit 1
