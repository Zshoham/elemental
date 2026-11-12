#!/usr/bin/env python3
"""Check what a set of static libraries needs from outside itself.

    check_symbols.py --nm NM --allow FILE [--allow FILE...] ARCHIVE...

Lists the symbols the archives leave undefined, after removing the ones they
define for each other, and fails if any is missing from the allowlist. This
is how an unexpected dependency on a C library, a compiler runtime library,
or a platform shows up before it becomes a link error on someone's target.

An allowlist has one symbol per line; '#' starts a comment. Each --allow
adds a list.
"""

import argparse
import subprocess
import sys


def symbols(nm, archive, option):
    output = subprocess.run(
        [nm, option, "--format=posix", archive],
        check=True, stdout=subprocess.PIPE).stdout.decode()
    names = set()
    for line in output.splitlines():
        fields = line.split()
        # "name type [value size]"; archive member headers end with ':'.
        if len(fields) >= 2 and not line.rstrip().endswith(":"):
            names.add(fields[0])
    return names


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--nm", required=True)
    parser.add_argument("--allow", required=True, action="append")
    parser.add_argument("archives", nargs="+")
    args = parser.parse_args()

    allowed = set()
    for path in args.allow:
        with open(path, encoding="utf-8") as stream:
            allowed |= {line.split("#")[0].strip() for line in stream}
    allowed.discard("")

    defined = set()
    undefined = {}
    for archive in args.archives:
        defined |= symbols(args.nm, archive, "--defined-only")
        for name in symbols(args.nm, archive, "--undefined-only"):
            undefined.setdefault(name, []).append(archive)

    external = {name: users for name, users in undefined.items() if name not in defined}
    unexpected = sorted(set(external) - allowed)

    print("external symbols required: {}".format(", ".join(sorted(external)) or "none"))
    if unexpected:
        for name in unexpected:
            sys.stderr.write("unexpected undefined symbol '{}' in {}\n".format(
                name, ", ".join(external[name])))
        sys.stderr.write("If the dependency is intended, add it to {} with a reason.\n"
                         .format(args.allow[0]))
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
