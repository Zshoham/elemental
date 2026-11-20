#!/usr/bin/env python3
"""Host tests of the checker, its builtin oracle, and the CMake build."""

import argparse
import os
import re
import subprocess
import sys
from pathlib import Path


def run(command, code=0, env=None):
    result = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                            text=True, env=env)
    if result.returncode != code:
        raise SystemExit(f"expected exit {code} from {command}, got {result.returncode}:\n"
                         f"{result.stdout}")
    return result.stdout


def fixture(args):
    options = {"libc": ["--libc"], "cxx": ["--cxx"],
               "rejects": ["--function", "print_here:2"]}
    cxx = args.case == "cxx"
    name = "portable_check_" + args.case + (".cpp" if cxx else ".c")
    source = args.fixtures / name
    compiler = args.cxx_compiler if cxx else args.compiler
    standard = "c++17" if cxx else "c11"
    expected = []
    for number, line in enumerate(source.read_text().splitlines(), 1):
        marker = re.search(r"// reject( next)?: (.*)", line)
        if marker:
            for fragments in marker[2].split("; "):
                expected.append((number + bool(marker[1]), fragments.strip().split("|")))
    output = run([sys.executable, args.checker, *options.get(args.case, []), "--", compiler,
                  "-E", "-dD", f"-std={standard}",
                  *[f"-I{args.source / 'modules' / module / 'include'}"
                    for module in ("fmt", "io", "status", "log", "time")],
                  source], code=int(bool(expected)))
    diagnostics = re.findall(rf"^{re.escape(str(source))}:(\d+): error: (.*)$", output, re.M)
    if len(diagnostics) != len(expected):
        raise SystemExit(f"expected {len(expected)} diagnostics, saw {len(diagnostics)}:\n{output}")
    for (line, fragments), (actual_line, diagnostic) in zip(expected, diagnostics):
        if int(actual_line) != line or any(part not in diagnostic for part in fragments):
            raise SystemExit(f"expected {fragments} at {source}:{line}:\n{output}")


def cmake_build(args):
    # Force response files on Ninja. The failing literal comes from a
    # source-specific definition, and only a target definition enables it.
    env = dict(os.environ, CMAKE_NINJA_FORCE_RESPONSE_FILE="1")
    configure = ["cmake", "-S", args.fixtures / "portable_check_project",
                 "-B", args.build, "-G", args.generator,
                 f"-DCMAKE_C_COMPILER={args.compiler}",
                 f"-DELEMENTAL_SOURCE_DIR={args.source}",
                 f"-DELEMENTAL_KCONFIGLIB_DIR={args.kconfiglib}"]
    build = ["cmake", "--build", args.build, "--target", "checked"]
    run([*configure, "-DCHECK_LIBC=ON"], env=env)
    run(build, env=env)
    run([*configure, "-DCHECK_LIBC=OFF"], env=env)
    output = run(build, code=1 if "Ninja" in args.generator else 2, env=env)
    if not re.search(r"caller\.c:\d+: error: report: '%f' in format 'value %f': conversion",
                     output):
        raise SystemExit(f"expected the checker to reject the compilation:\n{output}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("case", choices=["accepts", "rejects", "libc", "cxx", "oracle", "build"])
    parser.add_argument("--source", required=True, type=Path)
    parser.add_argument("--compiler", required=True)
    parser.add_argument("--cxx-compiler", required=True)
    parser.add_argument("--generator", required=True)
    parser.add_argument("--build", required=True, type=Path)
    parser.add_argument("--kconfiglib", required=True)
    parser.add_argument("--oracle")
    args = parser.parse_args()
    args.checker = args.source / "tools" / "check_formats.py"
    args.fixtures = args.source / "modules" / "fmt" / "tests"

    if args.case == "build":
        cmake_build(args)
    elif args.case == "oracle":
        run([sys.executable, args.checker, "--", args.compiler, "-E", "-dD", "-std=c11",
             *[f"-I{args.source / 'modules' / module / 'include'}"
               for module in ("fmt", "io", "status")],
             args.fixtures / "portable_check_oracle.c"])
        run([args.oracle])
    else:
        fixture(args)


if __name__ == "__main__":
    main()
