#!/bin/sh
# Format Elemental's C and C++ sources with clang-format, using .clang-format.
#
#   tools/format.sh          rewrite files in place
#   tools/format.sh --check  report files that would change, exit 1 if any
#
# Not enforced in CI: clang-format's output differs between releases, so a
# gate would fail on tool version rather than on style.
set -eu
cd "$(dirname "$0")/.."

mode="-i"
if [ "${1:-}" = "--check" ]; then
    mode="--dry-run --Werror"
fi

# Tracked files plus new ones that are not ignored.
git ls-files --cached --others --exclude-standard -- '*.c' '*.h' '*.cpp' '*.hpp' |
    xargs clang-format $mode
