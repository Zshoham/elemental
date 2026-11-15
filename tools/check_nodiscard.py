#!/usr/bin/env python3
"""Check that every function declared to return el_status is EL_NODISCARD.

A status must be used (docs/conventions.md, "Errors"), and the compiler can
only say so where the declaration carries EL_NODISCARD. This reads every
header under the given directories and reports each declaration of a function
returning el_status that lacks it. Operation-table members are function
pointers, which the attribute does not cover, and are not declarations of
this form.

    check_nodiscard.py <directory>...

Exits 0 when every declaration has it, 1 otherwise.
"""

import pathlib
import re
import sys

# A declaration at the start of a line: optional specifiers, then the return
# type el_status and a function name. Comment lines start with " *" or "/*",
# and members of a struct are indented, so neither matches.
DECLARATION = re.compile(r"^(?P<specifiers>(?:[A-Za-z_][A-Za-z0-9_]*\s+)*)el_status\s+(?P<name>\w+)\s*\(")


def missing(path):
    found = []
    for number, line in enumerate(path.read_text().splitlines(), start=1):
        match = DECLARATION.match(line)
        if match and "EL_NODISCARD" not in match.group("specifiers").split():
            found.append((number, match.group("name")))
    return found


def main(argv):
    if len(argv) < 2:
        sys.exit(__doc__)

    headers = 0
    failures = 0
    for directory in argv[1:]:
        for path in sorted(pathlib.Path(directory).rglob("*.h")):
            headers += 1
            for number, name in missing(path):
                failures += 1
                print(f"{path}:{number}: {name}() returns el_status and is not EL_NODISCARD")

    if failures:
        print(f"{failures} declaration(s) without EL_NODISCARD")
        return 1
    print(f"{headers} headers: every function that returns el_status is EL_NODISCARD")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
