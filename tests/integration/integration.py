#!/usr/bin/env python3
"""Integration checks: Elemental as seen from outside its own build.

Each case configures and builds one or more throwaway projects in a work
directory and asserts on the outcome. They cover what unit tests cannot: how
a module is consumed, what configuration does to the build, and whether bad
input is diagnosed.

    integration.py CASE --source DIR --work DIR --generator NAME
                        --c-compiler PATH --cxx-compiler PATH
                        --kconfiglib DIR --googletest DIR

Standard library only; Python 3.9 or later. Exit status 0 on success, 1 on a
failed check.
"""

import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path

ARGS = None


class Failure(Exception):
    pass


def check(condition, message):
    if not condition:
        raise Failure(message)


def run(command, cwd=None, expect_failure=False):
    """Run a command and return its combined output as text."""
    command = [str(part) for part in command]
    result = subprocess.run(command, cwd=cwd, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT)
    text = result.stdout.decode("utf-8", errors="replace")
    if (result.returncode != 0) != expect_failure:
        raise Failure("{}:\n  {}\n{}".format(
            "expected this to fail, but it succeeded" if expect_failure
            else "command failed (exit {})".format(result.returncode),
            " ".join(command), text))
    return text


def said(output, fragment):
    """True if a tool's output contains 'fragment', however it was wrapped.

    CMake reflows message text, and where it breaks lines differs between
    versions, so messages are compared with whitespace collapsed.
    """
    return " ".join(fragment.split()) in " ".join(output.split())


def configure(source, build, *definitions, expect_failure=False, toolchain=True):
    command = ["cmake", "-S", source, "-B", build, "-G", ARGS.generator,
               "-DELEMENTAL_KCONFIGLIB_DIR={}".format(ARGS.kconfiglib)]
    if toolchain:
        command.append("-DCMAKE_C_COMPILER={}".format(ARGS.c_compiler))
    command.extend(definitions)
    return run(command, expect_failure=expect_failure)


def build(build_dir, *arguments, expect_failure=False):
    return run(["cmake", "--build", build_dir, *arguments],
               expect_failure=expect_failure)


def target_names(build_dir):
    """The build system's target names, for either Ninja or Makefiles."""
    names = set()
    for line in build(build_dir, "--target", "help").splitlines():
        name = line.replace("...", " ").split(":")[0].split()
        if name:
            names.add(name[0])
    return names


def find_one(root, name):
    matches = sorted(Path(root).rglob(name))
    check(len(matches) == 1,
          "expected exactly one {} under {}, found {}".format(name, root, matches))
    return matches[0]


def exists_under(root, name):
    return any(Path(root).rglob(name))


def write(path, text):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text)
    return path


def config_header(build_dir):
    return find_one(build_dir, "elemental_config.h").read_text()


def copy_source_tree(destination):
    """A private copy of the source tree, for cases that must edit it."""
    shutil.copytree(
        ARGS.source, destination,
        ignore=shutil.ignore_patterns("build", "build-*", ".deps", ".git",
                                      "__pycache__"))
    return Path(destination)


# ---- Cases -----------------------------------------------------------------

def case_subdirectory(work):
    """One module, consumed with add_subdirectory()."""
    source = ARGS.source / "examples" / "consume_subdirectory"
    build_dir = work / "build"
    configure(source, build_dir, "-DELEMENTAL_SOURCE_DIR={}".format(ARGS.source))
    build(build_dir)

    output = run([find_one(build_dir, "message_builder")])
    check("record 2: elemental" in output, "unexpected output:\n" + output)

    # Only what the application links was built.
    check(exists_under(build_dir, "libelemental_io.a"), "io was not built")
    check(exists_under(build_dir, "libelemental_status.a"),
          "status, a dependency of io, was not built")
    check(not exists_under(build_dir, "io_contract.cpp.o"),
          "the io contract suite was compiled although nothing links it")
    # io does not depend on alloc: an application that links only io builds
    # no allocator (roadmap M2; the grow writer is io_alloc's).
    check(not exists_under(build_dir, "libelemental_alloc.a"),
          "alloc was built although the application links only io")
    check(not exists_under(build_dir, "libelemental_io_posix.a"),
          "io_posix was built although the application's defconfig disables it")

    # The application's defconfig was applied.
    check("EL_CONFIG_IO_POSIX" not in config_header(build_dir),
          "the application's defconfig was not applied")

    # As a subproject, Elemental adds libraries and nothing else: no tests,
    # no examples, and no short target names in the application's namespace.
    targets = target_names(build_dir)
    for unwanted in ("test_status_unit", "example_ring_pipe", "menuconfig"):
        check(unwanted not in targets,
              "the subproject build defines the target '{}'".format(unwanted))
    check("elemental_config_menuconfig" in targets,
          "the namespaced menuconfig target is missing")


def install_elemental(work, name, *definitions):
    build_dir = work / (name + "-build")
    prefix = work / (name + "-prefix")
    configure(ARGS.source, build_dir,
              "-DELEMENTAL_BUILD_TESTS=OFF", "-DELEMENTAL_BUILD_EXAMPLES=OFF",
              "-DCMAKE_INSTALL_PREFIX={}".format(prefix), *definitions)
    build(build_dir)
    run(["cmake", "--install", build_dir])
    return prefix


def case_installed(work):
    """Install, then consume with find_package() and no Kconfig."""
    prefix = install_elemental(work, "default")

    for name in ("status.h", "assert.h", "assert_handler.h", "io.h", "io_mem.h",
                 "io_posix.h", "io_contract.h", "io_contract.hpp",
                 "io_contract.cpp", "elemental_config.h",
                 "alloc.h", "alloc_arena.h", "alloc_libc.h", "io_alloc.h",
                 "alloc_contract.h", "alloc_contract.hpp",
                 "alloc_contract.cpp", "alloc_checker.c",
                 "libelemental_status.a", "libelemental_io.a",
                 "libelemental_io_posix.a", "libelemental_alloc.a",
                 "libelemental_alloc_libc.a", "libelemental_io_alloc.a",
                 "ElementalConfig.cmake", "ElementalConfigVersion.cmake",
                 "ElementalTargets.cmake", "ElementalConfiguration.cmake",
                 "elemental.config"):
        check(exists_under(prefix, name), "not installed: " + name)

    # GoogleTest is the consumer's to provide: the package ships none.
    for name in ("gtest.h", "libgtest.a", "libgtest_main.a"):
        check(not exists_under(prefix, name), "installed GoogleTest: " + name)

    # Nothing from the build or source tree leaks into the package.
    for path in Path(prefix).rglob("*.cmake"):
        text = path.read_text()
        check(str(work) not in text.replace(str(prefix), ""),
              "{} refers to the build tree".format(path))
        check("kconfig::" not in text,
              "{} refers to a build-only Kconfig target".format(path))

    consumer_source = ARGS.source / "examples" / "consume_installed"
    consumer = work / "consumer"
    # No ELEMENTAL_KCONFIGLIB_DIR: the consumer must not need it. GoogleTest
    # is the consumer's own, built from source here so that the case does not
    # depend on one being installed on the machine.
    consumer_definitions = [
        "-DCMAKE_C_COMPILER={}".format(ARGS.c_compiler),
        "-DCMAKE_CXX_COMPILER={}".format(ARGS.cxx_compiler),
        "-DGOOGLETEST_SOURCE_DIR={}".format(ARGS.googletest)]
    run(["cmake", "-S", consumer_source, "-B", consumer, "-G", ARGS.generator,
         "-DCMAKE_PREFIX_PATH={}".format(prefix)] + consumer_definitions)
    build(consumer)

    # The consumer ran neither Kconfig nor Python.
    cache = (consumer / "CMakeCache.txt").read_text()
    check("Python3_EXECUTABLE" not in cache, "the consumer looked for Python")
    check(not exists_under(consumer, "elemental_config.h"),
          "the consumer generated a configuration header")

    # The application: large enough to need many reads and partial writes.
    payload = bytes((i * 7 + i // 251) % 256 for i in range(300000))
    result = subprocess.run([str(find_one(consumer, "el_cat"))], input=payload,
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    check(result.returncode == 0, "el_cat failed: " + result.stderr.decode())
    check(result.stdout == payload, "el_cat did not reproduce its input")

    # The out-of-tree backend, against the installed contract suite.
    output = run([find_one(consumer, "ring_pipe_contract")])
    check("[  PASSED  ] 10 tests." in output and "[  SKIPPED ] 1 test" in output,
          "contract suite output:\n" + output)

    # A package configured without a module says so when asked for it.
    defconfig = write(work / "no_posix_defconfig",
                      "# EL_CONFIG_IO_POSIX is not set\n")
    reduced = install_elemental(work, "no-posix",
                                "-DELEMENTAL_DEFCONFIG={}".format(defconfig))
    check(not exists_under(reduced, "libelemental_io_posix.a"),
          "io_posix installed although disabled")
    output = run(["cmake", "-S", consumer_source, "-B", work / "consumer-no-posix",
                  "-G", ARGS.generator,
                  "-DCMAKE_PREFIX_PATH={}".format(reduced)] + consumer_definitions,
                 expect_failure=True)
    check(said(output, "configured without the 'io_posix' module"),
          "missing-component diagnostic not found:\n" + output)

    # One build may contain one Elemental, in either order.
    for order, first, second in (
            ("subdirectory-then-package",
             'add_subdirectory("{}" elemental)'.format(ARGS.source.as_posix()),
             "find_package(Elemental REQUIRED)"),
            ("package-then-subdirectory",
             "find_package(Elemental REQUIRED)",
             'add_subdirectory("{}" elemental)'.format(ARGS.source.as_posix()))):
        project = work / order
        write(project / "CMakeLists.txt",
              "cmake_minimum_required(VERSION 3.22)\n"
              "project(mixed LANGUAGES C)\n{}\n{}\n".format(first, second))
        output = configure(project, project / "build",
                           "-DCMAKE_PREFIX_PATH={}".format(prefix),
                           expect_failure=True)
        check(said(output, "already") and said(output, "Elemental"),
              "{}: mixing was not diagnosed:\n{}".format(order, output))


def case_raw_source(work):
    """A module's sources, a hand-written config header, and a compiler."""
    write(work / "elemental_config.h",
          "/* Written by hand: no Kconfig, no CMake. */\n"
          "#define EL_CONFIG_STATUS 1\n"
          "#define EL_CONFIG_ASSERT 1\n"
          "#define EL_CONFIG_ASSERT_HANDLER_TRAP 1\n"
          "#define EL_CONFIG_IO 1\n")
    modules = ARGS.source / "modules"
    binary = work / "message_builder"
    run([ARGS.c_compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
         "-I", work,
         "-I", modules / "status" / "include",
         "-I", modules / "io" / "include",
         modules / "status" / "src" / "status.c",
         modules / "io" / "src" / "io.c",
         modules / "io" / "src" / "io_mem.c",
         ARGS.source / "examples" / "consume_subdirectory" / "main.c",
         "-o", binary])
    output = run([binary])
    check("record 2: elemental" in output, "unexpected output:\n" + output)


def case_regenerate(work):
    """Configuration and Kconfig changes reach the header, CMake, and objects."""
    source = copy_source_tree(work / "source")
    build_dir = work / "build"
    configure(source, build_dir,
              "-DELEMENTAL_BUILD_TESTS=OFF", "-DELEMENTAL_BUILD_EXAMPLES=OFF")
    build(build_dir)

    config = build_dir / "kconfig" / ".config"
    io_object = find_one(build_dir, "io.c.o")
    check("EL_CONFIG_IO_POSIX 1" in config_header(build_dir), "unexpected defaults")
    check(exists_under(build_dir, "libelemental_io_posix.a"), "io_posix not built")

    def edit_config(old, new):
        text = config.read_text()
        check(old in text, "'{}' not in {}".format(old, config))
        config.write_text(text.replace(old, new))

    # 1. A CMake decision: disabling a module removes its target. Only
    #    `cmake --build` is run; regeneration is the build system's job.
    edit_config("EL_CONFIG_IO_POSIX=y", "# EL_CONFIG_IO_POSIX is not set")
    build(build_dir)
    check("EL_CONFIG_IO_POSIX" not in config_header(build_dir),
          "the header was not regenerated after editing .config")
    output = build(build_dir, "--target", "elemental_io_posix", expect_failure=True)
    check("elemental_io_posix" in output, "unexpected error:\n" + output)

    # 2. A C decision: the header changes and dependent objects recompile.
    #    Editing one line leaves the handler choice, which depends on ASSERT,
    #    behind in the file. That is ordinary and must not be an error.
    before = io_object.stat().st_mtime_ns
    edit_config("EL_CONFIG_ASSERT=y", "# EL_CONFIG_ASSERT is not set")
    build(build_dir)
    header = config_header(build_dir)
    check("EL_CONFIG_ASSERT" not in header,
          "ASSERT still enabled in the header:\n" + header)
    check(io_object.stat().st_mtime_ns != before,
          "io.c was not recompiled after its configuration changed")

    # 3. A sourced Kconfig file changes: a new symbol appears.
    kconfig = source / "modules" / "status" / "Kconfig"
    kconfig.write_text(kconfig.read_text() +
                       '\nconfig REGENERATION_PROBE\n\tbool "probe"\n\tdefault y\n')
    build(build_dir)
    check("EL_CONFIG_REGENERATION_PROBE 1" in config_header(build_dir),
          "a change to a sourced Kconfig file did not regenerate the header")

    # 4. Nothing left to do: regeneration does not trigger itself.
    if ARGS.generator == "Ninja":
        output = build(build_dir)
        check("no work to do" in output, "the build is not idempotent:\n" + output)


def tree_snapshot(root, skip):
    snapshot = {}
    for directory, subdirectories, files in os.walk(root):
        subdirectories[:] = [
            name for name in subdirectories
            if name not in (".git", ".deps")
            and not name.startswith("build")
            and Path(directory, name).resolve() not in skip]
        for name in files:
            path = Path(directory, name)
            stat = path.stat()
            snapshot[str(path.relative_to(root))] = (stat.st_size, stat.st_mtime_ns)
    return snapshot


def case_isolation(work):
    """Build directories do not share configuration; the source tree stays clean."""
    skip = {work.resolve()}
    before = tree_snapshot(ARGS.source, skip)

    common = ("-DELEMENTAL_BUILD_TESTS=OFF", "-DELEMENTAL_BUILD_EXAMPLES=OFF")
    trap = work / "trap"
    application = work / "application"
    configure(ARGS.source, trap, *common)
    build(trap)
    trap_header = config_header(trap)
    trap_config = (trap / "kconfig" / ".config").read_text()

    configure(ARGS.source, application, *common,
              "-DELEMENTAL_DEFCONFIG={}".format(
                  ARGS.source / "configs" / "variant_defconfig"))
    build(application)

    check("EL_CONFIG_ASSERT_HANDLER_TRAP 1" in trap_header,
          "unexpected default configuration")
    check("EL_CONFIG_ASSERT_HANDLER_APPLICATION 1" in config_header(application),
          "the defconfig was not applied")

    # Configuring the second directory changed nothing in the first.
    check(config_header(trap) == trap_header, "a header changed in another build")
    check((trap / "kconfig" / ".config").read_text() == trap_config,
          "a .config changed in another build")

    # The two libraries really differ in the way their configuration says.
    def undefined_symbols(build_dir):
        return run(["nm", "--undefined-only", find_one(build_dir, "libelemental_io.a")])

    check("el_assert_failed" in undefined_symbols(application),
          "the application-handler build does not call el_assert_failed")
    check("el_assert_failed" not in undefined_symbols(trap),
          "the trap build refers to el_assert_failed")

    after = tree_snapshot(ARGS.source, skip)
    created = sorted(set(after) - set(before))
    changed = sorted(name for name in before if name in after and after[name] != before[name])
    check(not created and not changed,
          "the source tree was modified: created {}, changed {}".format(created, changed))


def case_diagnostics(work):
    """Unsupported and conflicting selections stop the build and say why."""
    common = ("-DELEMENTAL_BUILD_TESTS=OFF", "-DELEMENTAL_BUILD_EXAMPLES=OFF")

    def expect_rejected(name, defconfig_text, expected, *extra):
        defconfig = write(work / (name + "_defconfig"), defconfig_text)
        output = configure(ARGS.source, work / name, *common, *extra,
                           "-DELEMENTAL_DEFCONFIG={}".format(defconfig),
                           expect_failure=True)
        for fragment in expected:
            check(said(output, fragment),
                  "{}: expected '{}' in:\n{}".format(name, fragment, output))
        check(not exists_under(work / name, "elemental_config.h"),
              name + ": a header was generated from a rejected configuration")

    # A misspelled symbol.
    expect_rejected("typo", "EL_CONFIG_ASERT=n\n",
                    ["Invalid Elemental configuration", "ASERT", "undefined symbol"])

    # Disabling a module another enabled module needs.
    expect_rejected("conflict", "# EL_CONFIG_STATUS is not set\n",
                    ["EL_CONFIG_STATUS is assigned 'n' but resolves to 'y'",
                     "selected by", "IO"])

    # Two members of one choice.
    expect_rejected("choice",
                    "EL_CONFIG_ASSERT_HANDLER_TRAP=y\n"
                    "EL_CONFIG_ASSERT_HANDLER_APPLICATION=y\n",
                    ["EL_CONFIG_ASSERT_HANDLER_TRAP is assigned 'y' but resolves to 'n'",
                     "choice"])

    # A backend the toolchain cannot build. The probe result is forced, to
    # stand in for a toolchain without <unistd.h>.
    expect_rejected("unsupported", "EL_CONFIG_IO_POSIX=y\n",
                    ["EL_CONFIG_IO_POSIX is assigned 'y' but resolves to 'n'",
                     "unsatisfied dependencies", "PLATFORM_POSIX"],
                    "-DELEMENTAL_HAVE_UNISTD_H=")

    # Claiming a toolchain capability instead of having it.
    expect_rejected("derived", "EL_CONFIG_PLATFORM_POSIX=y\n",
                    ["EL_CONFIG_PLATFORM_POSIX", "has no prompt"],
                    "-DELEMENTAL_HAVE_UNISTD_H=")

    # A defconfig that does not exist.
    output = configure(ARGS.source, work / "missing", *common,
                       "-DELEMENTAL_DEFCONFIG={}".format(work / "no_such_defconfig"),
                       expect_failure=True)
    check(said(output, "ELEMENTAL_DEFCONFIG does not exist"), output)

    # A defconfig seeds a build directory once. Changing it later must not
    # be ignored silently.
    seeded = work / "seeded"
    first = write(work / "first_defconfig", "# EL_CONFIG_IO_POSIX is not set\n")
    second = write(work / "second_defconfig", "# EL_CONFIG_ASSERT is not set\n")
    configure(ARGS.source, seeded, *common, "-DELEMENTAL_DEFCONFIG={}".format(first))
    header = config_header(seeded)
    output = configure(ARGS.source, seeded, *common,
                       "-DELEMENTAL_DEFCONFIG={}".format(second))
    check(said(output, "is NOT applied"), "no warning for a changed defconfig:\n" + output)
    check(config_header(seeded) == header,
          "a changed defconfig silently replaced the configuration")

    # A hand-edited .config is checked too, not only the seed.
    config = seeded / "kconfig" / ".config"
    original = config.read_text()
    config.write_text(original + "EL_CONFIG_NO_SUCH_OPTION=y\n")
    output = configure(ARGS.source, seeded, *common,
                       "-DELEMENTAL_DEFCONFIG={}".format(first), expect_failure=True)
    check(said(output, "NO_SUCH_OPTION"), output)

    # Asking an existing .config for a backend the toolchain cannot build is
    # an error, where a defconfig's leftovers (see `regenerate`) are not.
    bare = work / "bare"
    configure(ARGS.source, bare, *common, "-DELEMENTAL_HAVE_UNISTD_H=")
    bare_config = bare / "kconfig" / ".config"
    bare_config.write_text(bare_config.read_text() + "EL_CONFIG_IO_POSIX=y\n")
    output = configure(ARGS.source, bare, *common, expect_failure=True)
    check(said(output, "unsatisfied dependencies") and said(output, "PLATFORM_POSIX"),
          output)


def case_provenance(work):
    """The build uses its pinned dependencies, or it does not configure."""
    common = ("-DELEMENTAL_BUILD_TESTS=OFF", "-DELEMENTAL_BUILD_EXAMPLES=OFF",
              "-DCMAKE_C_COMPILER={}".format(ARGS.c_compiler))

    def configure_with(kconfiglib, name, expect_failure=True):
        return run(["cmake", "-S", ARGS.source, "-B", work / name, "-G", ARGS.generator,
                    "-DELEMENTAL_KCONFIGLIB_DIR={}".format(kconfiglib), *common],
                   expect_failure=expect_failure)

    # The checkout the outer build uses is the pinned one, from the fork.
    pin = (ARGS.source / "cmake" / "KconfiglibPin.cmake").read_text()
    head = run(["git", "-C", ARGS.kconfiglib, "rev-parse", "HEAD"]).strip()
    check(head in pin, "the Kconfiglib checkout ({}) is not the pinned revision".format(head))
    check("github.com/zshoham/kconfiglib" in pin, "the pin does not name the fork")

    # Missing.
    output = configure_with(work / "nowhere", "missing")
    check(said(output, "tools/provision.cmake"), "no provisioning hint:\n" + output)

    # A different revision of the same repository.
    older = work / "older"
    run(["git", "clone", "--quiet", ARGS.kconfiglib, older])
    run(["git", "-C", older, "-c", "advice.detachedHead=false", "checkout", "--quiet",
         "HEAD~1"])
    output = configure_with(older, "older-build")
    check(said(output, "Elemental pins"), "a wrong revision was accepted:\n" + output)

    # The right revision, modified.
    modified = work / "modified"
    run(["git", "clone", "--quiet", ARGS.kconfiglib, modified])
    run(["git", "-C", modified, "-c", "advice.detachedHead=false", "checkout", "--quiet",
         head])
    with open(modified / "kconfiglib.py", "a") as stream:
        stream.write("\n# local change\n")
    output = configure_with(modified, "modified-build")
    check(said(output, "local modifications"), "a modified checkout was accepted:\n" + output)

    # An unmodified clone at the pinned revision is accepted wherever it is,
    # and the configuration is generated by that copy, not by a kconfiglib
    # that happens to be installed for the interpreter.
    clean = work / "clean"
    run(["git", "clone", "--quiet", ARGS.kconfiglib, clean])
    run(["git", "-C", clean, "-c", "advice.detachedHead=false", "checkout", "--quiet", head])
    configure_with(clean, "clean-build", expect_failure=False)
    check(exists_under(work / "clean-build" / "kconfig" / "pycache", "kconfiglib*.pyc"),
          "kconfiglib was not imported from the given checkout")
    check(not exists_under(clean, "__pycache__"),
          "bytecode was written into the Kconfiglib checkout")

    # GoogleTest is needed for the tests and only for them. The checkout the
    # outer build uses is the pinned one.
    pin = (ARGS.source / "cmake" / "GoogleTestPin.cmake").read_text()
    head = run(["git", "-C", ARGS.googletest, "rev-parse", "HEAD"]).strip()
    check(head in pin, "the GoogleTest checkout ({}) is not the pinned revision".format(head))

    # Missing: building the tests says how to get it, and that the libraries
    # do not need it. Building only the libraries does not look for it.
    with_tests = ("-DELEMENTAL_BUILD_EXAMPLES=OFF",
                  "-DCMAKE_C_COMPILER={}".format(ARGS.c_compiler),
                  "-DCMAKE_CXX_COMPILER={}".format(ARGS.cxx_compiler),
                  "-DELEMENTAL_KCONFIGLIB_DIR={}".format(ARGS.kconfiglib),
                  "-DELEMENTAL_GOOGLETEST_DIR={}".format(work / "nowhere"))
    output = run(["cmake", "-S", ARGS.source, "-B", work / "no-googletest",
                  "-G", ARGS.generator, *with_tests], expect_failure=True)
    check(said(output, "tools/provision.cmake") and said(output, "ELEMENTAL_BUILD_TESTS=OFF"),
          "no hint for a missing GoogleTest:\n" + output)
    run(["cmake", "-S", ARGS.source, "-B", work / "libraries-only", "-G", ARGS.generator,
         "-DELEMENTAL_BUILD_TESTS=OFF", *with_tests])


def subset_symbols():
    """Each module's and backend's Kconfig symbol, by the naming convention
    (docs/process/new-module.md): modules/<m> is enabled by <M>, and
    modules/<m>/backends/<b> by <M>_<B>."""
    names = []
    for module in sorted((ARGS.source / "modules").iterdir()):
        if not (module / "Kconfig").is_file():
            continue
        names.append(module.name.upper())
        backends = module / "backends"
        if backends.is_dir():
            names.extend("{}_{}".format(module.name, backend.name).upper()
                         for backend in sorted(backends.iterdir())
                         if backend.is_dir())
    return names


def load_kconfig(work):
    """Elemental's Kconfig tree, with the toolchain facts CMake probes for
    this compiler: a libraries-only configure, whose cache records them."""
    probe = work / "probe"
    configure(ARGS.source, probe, "-DELEMENTAL_BUILD_TESTS=OFF")
    facts = None
    for line in (probe / "CMakeCache.txt").read_text().splitlines():
        if line.startswith("ELEMENTAL_PROBED_FACTS:"):
            facts = line.split("=", 1)[1]
    check(facts, "the CMake cache has no ELEMENTAL_PROBED_FACTS")
    for fact in facts.split(";"):
        name, value = fact.split("=", 1)
        os.environ[name] = value

    sys.path.insert(0, str(ARGS.kconfiglib))
    import kconfiglib  # pylint: disable=import-outside-toplevel
    return kconfiglib, kconfiglib.Kconfig(str(ARGS.source / "Kconfig"), warn=False)


def case_module_subsets(work):
    """Each module alone, with only what its Kconfig entry pulls in, builds and passes its tests."""
    kconfiglib, kconf = load_kconfig(work)
    units = subset_symbols()
    for name in units:
        check(name in kconf.syms and kconf.syms[name].nodes,
              "modules/ layout names {} but no Kconfig symbol defines it".format(name))

    def pulled_in(name):
        """`name`, and every module its Kconfig entry depends on or selects."""
        wanted, pending = set(), [name]
        while pending:
            sym = kconf.syms[pending.pop()]
            if sym.name in wanted:
                continue
            wanted.add(sym.name)
            items = list(kconfiglib.expr_items(sym.direct_dep))
            items += [select[0] for select in sym.selects]
            pending.extend(item.name for item in items
                           if isinstance(item, kconfiglib.Symbol) and item.name in units)
        return wanted

    for name in units:
        enabled = pulled_in(name)
        # Everything else is turned off explicitly. If Kconfig turns one back
        # on (an undeclared select), configuration stops with the reason.
        defconfig = write(work / (name.lower() + "_defconfig"), "".join(
            "EL_CONFIG_{}=y\n".format(unit) if unit in enabled
            else "# EL_CONFIG_{} is not set\n".format(unit)
            for unit in units))
        build_dir = work / name.lower()
        configure(ARGS.source, build_dir,
                  "-DELEMENTAL_DEFCONFIG={}".format(defconfig),
                  "-DCMAKE_CXX_COMPILER={}".format(ARGS.cxx_compiler),
                  "-DELEMENTAL_GOOGLETEST_DIR={}".format(ARGS.googletest),
                  "-DELEMENTAL_WARNINGS_AS_ERRORS=ON")
        build(build_dir)

        # Exactly the enabled libraries were built.
        for unit in units:
            built = exists_under(build_dir, "libelemental_{}.a".format(unit.lower()))
            check(built == (unit in enabled),
                  "{} alone: {} was {}built".format(
                      name, unit, "" if built else "not "))

        # Its tests, and everything else that builds with this subset, pass.
        # The integration cases are left out: they configure their own trees.
        run(["ctest", "--test-dir", build_dir, "-LE", "integration",
             "--output-on-failure", "-j", str(os.cpu_count() or 2)])


def case_choices(work):
    """Every option of every Kconfig choice that this compiler can select builds and passes its module's tests."""
    kconfiglib, kconf = load_kconfig(work)
    check(kconf.unique_choices, "the Kconfig tree has no choice")
    modules = ARGS.source / "modules"

    tested, unavailable = [], []
    for choice in kconf.unique_choices:
        # The module that owns the choice is the one whose Kconfig defines it.
        defined_in = (ARGS.source / choice.nodes[0].filename).resolve()
        check(modules in defined_in.parents,
              "choice {} is not defined under modules/".format(choice.name))
        module = defined_in.relative_to(modules).parts[0]

        for option in choice.syms:
            if option.visibility == 0:
                # Only a probed toolchain fact may hide an option. Anything
                # else would leave the option untested on every host.
                facts = [item for item in kconfiglib.expr_items(option.direct_dep)
                         if isinstance(item, kconfiglib.Symbol)
                         and not any(node.prompt for node in item.nodes)
                         and item.tri_value == 0]
                check(facts, "{} cannot be selected here, and no probed fact explains "
                      "why: {}".format(option.name,
                                       kconfiglib.expr_str(option.direct_dep)))
                unavailable.append("{} (needs {})".format(
                    option.name, ", ".join(fact.name for fact in facts)))
                continue

            name = option.name.lower()
            defconfig = write(work / (name + "_defconfig"),
                              "EL_CONFIG_{}=y\n".format(option.name))
            build_dir = work / name
            configure(ARGS.source, build_dir,
                      "-DELEMENTAL_DEFCONFIG={}".format(defconfig),
                      "-DCMAKE_CXX_COMPILER={}".format(ARGS.cxx_compiler),
                      "-DELEMENTAL_GOOGLETEST_DIR={}".format(ARGS.googletest),
                      "-DELEMENTAL_WARNINGS_AS_ERRORS=ON")
            check("#define EL_CONFIG_{} 1".format(option.name) in config_header(build_dir),
                  "{} was asked for but not configured".format(option.name))

            # Only the owning module's tests are built and run.
            targets = sorted(target for target in target_names(build_dir)
                             if target.startswith("test_{}_".format(module)))
            check(targets, "module {} has no test executable".format(module))
            build(build_dir, "--target", *targets)
            output = run(["ctest", "--test-dir", build_dir, "-R", r"^{}\.".format(module),
                          "--output-on-failure", "-j", str(os.cpu_count() or 2)])
            check(said(output, "100% tests passed"),
                  "{}: not every {} test passed:\n{}".format(option.name, module, output))
            tested.append(option.name)

    print("tested: " + ", ".join(tested))
    if unavailable:
        print("not selectable with this compiler: " + ", ".join(unavailable))


CASES = {
    "subdirectory": case_subdirectory,
    "installed": case_installed,
    "raw_source": case_raw_source,
    "regenerate": case_regenerate,
    "isolation": case_isolation,
    "diagnostics": case_diagnostics,
    "provenance": case_provenance,
    "module_subsets": case_module_subsets,
    "choices": case_choices,
}


def main():
    global ARGS
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("case", choices=sorted(CASES))
    parser.add_argument("--source", required=True, type=Path)
    parser.add_argument("--work", required=True, type=Path)
    parser.add_argument("--generator", required=True)
    parser.add_argument("--c-compiler", required=True)
    parser.add_argument("--cxx-compiler", required=True)
    parser.add_argument("--googletest", required=True, type=Path)
    parser.add_argument("--kconfiglib", required=True, type=Path)
    ARGS = parser.parse_args()
    ARGS.source = ARGS.source.resolve()

    work = ARGS.work.resolve()
    if work.exists():
        shutil.rmtree(work)
    work.mkdir(parents=True)

    try:
        CASES[ARGS.case](work)
    except Failure as failure:
        sys.stderr.write("FAILED {}: {}\n".format(ARGS.case, failure))
        return 1
    print("ok: " + CASES[ARGS.case].__doc__.strip())
    return 0


if __name__ == "__main__":
    sys.exit(main())
