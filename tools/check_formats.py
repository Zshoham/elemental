#!/usr/bin/env python3
"""Check literal Elemental printf formats, or launch a checked compilation.

    check_formats.py [--libc] [--function name:argument] -- cc -E args...
    check_formats.py [--libc] [--function name:argument] --compile -- [launcher ...] --compiler cc args...

The first form reads the named preprocessor's output. The second takes a
normal GCC/Clang compile command and runs it only after the check passes.
"""

import argparse
import re
import shlex
import subprocess
import sys
from pathlib import Path
from typing import NamedTuple

FUNCTIONS = {
    "el_fmt_printf": 2,
    "el_fmt_vprintf": 2,
    "el_fmt_printf_buffered": 2,
    "el_fmt_vprintf_buffered": 2,
    "el_log_printf": 3,
    "el_log_vprintf": 3,
    # EL_LOG and EL_LOG_* expand to el_log_printf.
}

# C/C++ preprocessing tokens relevant to calls. Recognize comments and both
# kinds of string literal before looking for identifiers or punctuation.
TOKEN = re.compile(
    r'(?P<directive>^[ \t]*\#[^\n]*)'
    r'|(?P<space>\s+|//[^\n]*|/\*.*?\*/)'
    r'|(?P<raw>(?:u8|[uUL])?R"(?P<delimiter>[^\s()\\]{0,16})\(.*?\)(?P=delimiter)")'
    r'|(?P<string>(?:u8|[uUL])?"(?:\\.|[^"\\])*")'
    r"|(?P<char>(?:u8|[uUL])?'(?:\\.|[^'\\])*')"
    r"|(?P<number>(?:[0-9]|\.[0-9])(?:[eEpP][+-]|'(?=\w)|[\w.])*)"
    r'|(?P<identifier>[^\W\d]\w*)'
    r'|(?P<punct>::|<=>|<=|<%|%>|<:|:>|[^\s])',
    re.MULTILINE | re.DOTALL,
)
LINE_MARKER = re.compile(r'#\s+(\d+)\s+"((?:\\.|[^"\\])*)".*')
LIMIT = re.compile(r"#define (__SIZE_MAX__|__INT_MAX__) (0x[0-9a-fA-F]+|[0-9]+)[uUlL]*$")
ESCAPE = re.compile(
    r"\\(x[0-9a-fA-F]+|[0-7]{1,3}|u[0-9a-fA-F]{4}|U[0-9a-fA-F]{8}|.)", re.DOTALL
)
SIMPLE_ESCAPE = dict(zip("abfnrtv", "\a\b\f\n\r\t\v"))


class Token(NamedTuple):
    kind: str
    spelling: str
    file: str
    line: int


def tokens(source, limits):
    file, line, end = "<input>", 1, 0
    clang, raw_lines = False, 0
    for match in TOKEN.finditer(source):
        start = match.start()
        gap_lines = source.count("\n", end, start)
        if gap_lines:
            line += gap_lines
            raw_lines = 0
        spelling = match[0]
        end = match.end()
        kind = match.lastgroup
        if kind == "directive":
            directive = spelling.lstrip()
            marker = LINE_MARKER.fullmatch(directive)
            if marker:
                file = re.sub(r'\\([\\"])', r'\1', marker[2])
                line = int(marker[1]) - 1  # applies to the next line
            elif file == "<built-in>":
                limit = LIMIT.fullmatch(directive)
                if limit:
                    limits[limit[1]] = int(limit[2], 0)
                clang |= directive == "#define __clang__ 1"
        elif kind != "space":
            # GCC can emit a line marker in the middle of a call for tokens
            # from a system macro, including PRIu64 and imported EL_LOG.
            yield Token(kind, spelling, file, line + raw_lines)
        spelling_lines = spelling.count("\n")
        if kind == "raw" and clang:
            # Clang's line padding already accounts for these newlines.
            # Keep them for any tokens on the raw string's closing line.
            raw_lines += spelling_lines
        elif spelling_lines:
            line += spelling_lines
            raw_lines = 0


# A comma in the middle operand of ?: is part of that operand.
CLOSE = {"(": ")", "[": "]", "{": "}", "<%": "%>", "<:": ":>", "?": ":"}


def format_argument(stream, start, argument, cxx):
    """Find an argument boundary without splitting nested expressions."""
    groups = []
    index = start
    while index < len(stream):
        text = stream[index].spelling
        if text in CLOSE:
            groups.append(CLOSE[text])
        elif groups:
            if text == groups[-1]:
                groups.pop()
        elif text in (",", ")"):
            if argument == 1:
                return stream[start:index]
            if text == ")":
                break
            start, argument = index + 1, argument - 1
        elif cxx and text == "<" and index > start and stream[index - 1].kind == "identifier":
            # A top-level comma after this could be in template arguments.
            # Parentheses around that expression make the boundary clear.
            return ()
        index += 1
    return ()


def unescape(match):
    escape = match[1]
    if escape[0] in "xuU" and len(escape) > 1:
        value = int(escape[1:], 16)
        return chr(value & 255) if escape[0] == "x" else chr(value & 0x10ffff)
    if escape[0] in "01234567":
        return chr(int(escape, 8) & 255)
    return SIMPLE_ESCAPE.get(escape, "\x1b" if escape == "e" else escape)


def literal(argument):
    """Read a possibly parenthesized sequence of narrow string literals."""
    start, end = 0, len(argument)
    while end - start >= 2 and argument[start].spelling == "(" and argument[end - 1].spelling == ")":
        start, end = start + 1, end - 1
    if start == end:
        return None

    parts = []
    for token in argument[start:end]:
        spelling = token.spelling
        if token.kind == "string" and spelling.startswith(('"', 'u8"')):
            parts.append(ESCAPE.sub(unescape, spelling[spelling.index('"') + 1:-1]))
        elif token.kind == "raw" and spelling.startswith(('R"', 'u8R"')):
            opening = spelling.index("(")
            closing = len(spelling) - opening + spelling.index('"')
            parts.append(spelling[opening + 1:closing - 1])
        else:
            # A variable, expression, wide literal or user-defined literal.
            return None
    return "".join(parts).split("\0", 1)[0]


# Mirrors parse_spec and is_portable in modules/fmt/src/fmt_printf.c.
# A bare dot has precision zero. Repeated flags set the same bit.
SPEC = re.compile(r"%([-+ #0]*)(\*?)([0-9]*)(\.(?:\*|[0-9]*))?(hh|ll|[hlzjtL])?(.)?", re.DOTALL)
INTEGERS = "diuxX"
PORTABLE_LENGTHS = (None, "hh", "h", "l", "ll", "z")


def number_overflows(digits, size_max):
    if size_max is None:
        return False
    value = 0
    for digit in digits:
        if value > (size_max - 9) // 10:
            return True
        value = value * 10 + int(digit)
    return False


def exceeds(digits, limit):
    digits = digits.lstrip("0")
    limit = str(limit)
    return (len(digits), digits) > (len(limit), limit)


def disallowed(match, libc, limits):
    flags, width_star, width, precision, length, conversion = match.groups()
    if conversion is None:
        return "incomplete specification"

    digits = precision[1:] if precision and precision != ".*" else ""
    if any(number_overflows(field, limits.get("__SIZE_MAX__")) for field in (width, digits)):
        return "width or precision overflows the formatter's size_t parser"

    if conversion == "%":
        return None if match[0] == "%%" else "a literal percent must be written as %%"

    if conversion in "cs":
        if length:
            return f"length {length!r} is unsupported on %{conversion}"
        if set(flags) - ({"-"} if libc else set()):
            return f"flags {flags!r} are unsupported on %{conversion}"
        if conversion == "c" and precision:
            return "precision is unsupported on %c"
    elif conversion in INTEGERS and not libc:
        if set(flags) - {"0"}:
            return f"flags {flags!r} are outside the portable subset"
        if precision:
            return "integer precision is outside the portable subset"
        if length not in PORTABLE_LENGTHS:
            return f"length {length!r} is outside the portable subset"
    elif libc:
        # The numeric cases of kind_of in fmt_printf_libc.c. The compiler
        # still checks the arguments and the C library's format rules.
        if conversion in INTEGERS + "o":
            lengths = (*PORTABLE_LENGTHS, "j", "t")
        elif conversion in "fFeEgGaA":
            lengths = (None, "l", "L")
        elif conversion == "p":
            lengths = (None,)
        else:
            return f"conversion %{conversion} is unsupported"
        if length not in lengths:
            return f"length {length!r} is unsupported on %{conversion}"
        portable = (conversion in INTEGERS and length in PORTABLE_LENGTHS
                    and set(flags) <= {"0"} and not precision and not width_star)
        int_max = limits.get("__INT_MAX__")
        if (not portable and int_max is not None
                and any(exceeds(field, int_max) for field in (width, digits))):
            return "width or precision exceeds INT_MAX for a libc conversion"
    else:
        return f"conversion %{conversion} is outside the portable subset"

    if width_star and not libc:
        return "'*' width is outside the portable subset"
    if width_star and width:
        return "digits cannot follow a '*' width"
    return None


def check(source, functions, libc, cxx):
    limits = {}
    stream = list(tokens(source, limits))
    failures = 0
    for index, token in enumerate(stream):
        if token.kind != "identifier" or token.spelling not in functions:
            continue
        opening = index + 1
        # C also permits (function)(...) and redundant parentheses around it.
        previous = index - 1
        while (previous >= 0 and opening < len(stream)
               and stream[previous].spelling == "(" and stream[opening].spelling == ")"):
            previous, opening = previous - 1, opening + 1
        if opening == len(stream) or stream[opening].spelling != "(":
            continue
        argument = format_argument(stream, opening + 1, functions[token.spelling], cxx)
        format_string = literal(argument)
        if format_string is None:
            continue
        for match in SPEC.finditer(format_string):
            reason = disallowed(match, libc, limits)
            if reason:
                print(f"{token.file}:{token.line}: error: {token.spelling}: "
                      f"{match[0]!r} in format {format_string!r}: {reason}", file=sys.stderr)
                failures += 1
    return bool(failures)


def expand_response_files(arguments):
    for arg in arguments:
        if arg.startswith("@") and Path(arg[1:]).is_file():
            yield from expand_response_files(shlex.split(Path(arg[1:]).read_text()))
        else:
            yield arg


def preprocess_command(compiler):
    """Keep target, language and preprocessor flags, drop compiler outputs."""
    arguments = iter(expand_response_files(compiler[1:]))
    command = [compiler[0], "-E", "-dD"]
    for arg in arguments:
        if arg in ("-o", "-MF", "-MT", "-MQ", "-MJ", "--serialize-diagnostics"):
            next(arguments, None)
        elif arg == "-Xclang":
            option = next(arguments)
            if option != "-emit-pch":
                command.extend((arg, option))
        elif (arg in ("-c", "-MD", "-MMD", "-MP", "-MG", "-P", "-dM", "-save-temps")
              or arg.startswith(("-MF", "-MT", "-MQ", "-MJ", "-save-temps="))):
            continue
        else:
            command.append(arg)
    return command


def function_argument(value):
    match = re.fullmatch(r"([A-Za-z_]\w*):([1-9][0-9]*)", value, re.ASCII)
    if not match:
        raise argparse.ArgumentTypeError("expected name:argument (one-based)")
    return match[1], int(match[2])


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--libc", action="store_true")
    parser.add_argument("--cxx", action="store_true")
    parser.add_argument("--function", action="append", default=[], type=function_argument)
    parser.add_argument("--compile", action="store_true")
    parser.add_argument("command", nargs=argparse.REMAINDER)
    args = parser.parse_args()
    command = args.command
    if command[:1] == ["--"]:
        command = command[1:]
    functions = dict(args.function)
    functions.update(FUNCTIONS)

    if args.compile:
        split = command.index("--compiler")
        launcher, compiler = command[:split], command[split + 1:]
        command = preprocess_command(compiler)
    if not command:
        parser.error("a compiler command is required")
    result = subprocess.run(command, capture_output=True)
    if result.returncode or not args.compile:
        sys.stderr.buffer.write(result.stderr)
    if result.returncode:
        return result.returncode
    if check(result.stdout.decode("utf-8", errors="surrogateescape"), functions, args.libc, args.cxx):
        return 1
    return subprocess.call(launcher + compiler) if args.compile else 0


if __name__ == "__main__":
    sys.exit(main())
