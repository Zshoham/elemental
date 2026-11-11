#!/usr/bin/env python3
"""Reject configuration input that Kconfig would otherwise change silently.

    kconfig_check.py KCONFIG CONFIG [--existing]

Kconfig resolves a configuration by quietly dropping assignments it cannot
honor: a symbol whose dependencies are unmet becomes 'n', a selected symbol
becomes 'y', an unknown name is ignored. That is the right behavior for an
interactive editor and the wrong one for a build, where a typo or an
unsupported selection should stop the build and say why.

This script loads CONFIG with Kconfiglib's own parser and evaluator, then
reports every assignment whose resolved value differs from the assigned one.
It does not parse .config files or evaluate dependencies itself.

A defconfig is a statement of intent and is held to that standard exactly.
With --existing, CONFIG is a build directory's full .config, which people
edit by hand: turning an option off there leaves the settings of everything
that depended on it behind. Those leftovers are how Kconfig files normally
look and are not reported. Asking for something that cannot be had still is.

Exit status is 0 when the input is acceptable, 1 otherwise.
"""

import sys

import kconfiglib


def _reason(sym, wanted):
    """Explain why 'sym' did not get the value 'wanted'."""
    if not any(node.prompt for node in sym.nodes):
        return ("it has no prompt: its value is derived (from the toolchain "
                "or from other symbols) and cannot be assigned")

    if sym.choice is not None:
        selected = sym.choice.selection
        return "choice '{}' resolved to {}".format(
            sym.choice.name or "<unnamed>",
            selected.name if selected is not None else "no selection")

    if sym.orig_type in (kconfiglib.BOOL, kconfiglib.TRISTATE):
        if wanted == "y":
            return "unsatisfied dependencies: {}".format(
                kconfiglib.expr_str(sym.direct_dep))
        return "it is selected by: {}".format(
            kconfiglib.expr_str(sym.rev_dep))

    return "the value is outside the symbol's range or malformed"


def _turned_off_by_user(expr):
    """True if 'expr' names an option that the loaded file explicitly disables."""
    for item in kconfiglib.expr_items(expr):
        if isinstance(item, kconfiglib.Choice):
            # A choice member depends on its choice, which carries the real
            # dependencies.
            if _turned_off_by_user(item.direct_dep):
                return True
        elif (item.user_value == 0 and item.tri_value == 0
              and any(node.prompt for node in item.nodes)):
            return True
    return False


def _left_behind(sym):
    """True if 'sym' is off only because the user turned off a prerequisite.

    That is: its dependencies are unmet, and one of the options it depends on
    is explicitly set to 'n' in the same file.
    """
    if sym.orig_type not in (kconfiglib.BOOL, kconfiglib.TRISTATE):
        return False
    if kconfiglib.expr_value(sym.direct_dep) != 0:
        return False
    return _turned_off_by_user(sym.direct_dep)


def main():
    arguments = [argument for argument in sys.argv[1:] if argument != "--existing"]
    existing = len(arguments) != len(sys.argv) - 1
    if len(arguments) != 2:
        sys.exit(__doc__)
    kconfig_path, config_path = arguments

    kconf = kconfiglib.Kconfig(
        kconfig_path, warn_to_stderr=False, suppress_traceback=True)
    # Off by default in Kconfiglib; a misspelled symbol is exactly the kind of
    # input this script exists to catch.
    kconf.warn_assign_undef = True
    kconf.load_config(config_path)

    prefix = kconf.config_prefix
    problems = list(kconf.warnings)

    for sym in kconf.unique_defined_syms:
        if sym.user_value is None:
            continue
        if sym.orig_type in (kconfiglib.BOOL, kconfiglib.TRISTATE):
            wanted = kconfiglib.TRI_TO_STR[sym.user_value]
        else:
            wanted = sym.user_value
        if wanted != sym.str_value:
            if existing and wanted != "n" and _left_behind(sym):
                continue
            problems.append(
                "{}{} is assigned '{}' but resolves to '{}': {}".format(
                    prefix, sym.name, wanted, sym.str_value,
                    _reason(sym, wanted)))

    if problems:
        sys.stderr.write(
            "{} cannot be applied as written:\n".format(config_path))
        for problem in problems:
            sys.stderr.write("  - {}\n".format(problem))
        sys.exit(1)


if __name__ == "__main__":
    main()
