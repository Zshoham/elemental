# Building, configuring, and consuming Elemental

## Requirements

| Tool | Baseline | Why this baseline | Tested with |
|---|---|---|---|
| CMake | 3.22 | Ships with Ubuntu 22.04 LTS. Needs `PROJECT_IS_TOP_LEVEL` and presets schema 3 (3.21). The Kconfiglib fork's package needs 3.15. | 3.22.6 and 4.4.5 |
| C compiler | C11 | See [ADR 0006](adr/0006-c11-and-naming.md). | GCC 16.2.1, Clang 23.1.4 |
| C++ compiler | C++17, tests only | GoogleTest 1.18 requires it ([ADR 0008](adr/0008-googletest.md)). The libraries are C and need no C++ compiler. | GCC 16.2.1, Clang 23.1.4 |
| Python | 3.9 | The Kconfiglib fork's stated minimum. Standard library only. | 3.14.9 only |
| Git | any | Provisioning, and verifying the pinned revisions. | 2.56.0 |
| Ninja | any | Used by the presets. Other generators work with plain `cmake -B`. | 1.13.2 |

Optional, each enabling a check that is otherwise skipped with a message:
`rustc` (Rust interop sketch) and CPython development headers (Python sketch
compile check). The cross build needs `ld.lld`, `llvm-ar`, and `llvm-nm`.

Installing an Elemental package and using it needs only CMake and a C
compiler. Building only the libraries (`-DELEMENTAL_BUILD_TESTS=OFF`, which
is the default for a subproject) needs neither a C++ compiler nor
GoogleTest.

## Dependencies

Elemental has two build-time dependencies, each pinned to a commit. Neither
is part of what Elemental installs, and neither is needed at run time.

| Dependency | Needed for | Pin |
|---|---|---|
| A Kconfiglib fork | Every build that runs Kconfig | [cmake/KconfiglibPin.cmake](../cmake/KconfiglibPin.cmake) |
| GoogleTest 1.18.0 | Building Elemental's tests | [cmake/GoogleTestPin.cmake](../cmake/GoogleTestPin.cmake) |

Kconfiglib is used through the fork's own CMake package
(`kconfig_configure()`); Elemental does not parse `.config` files or
generate headers itself.

Provision both once:

```sh
cmake -P tools/provision.cmake
```

This clones each repository into `.deps/` and checks out the pinned commit.
`-DELEMENTAL_PROVISION_TESTS=OFF` skips GoogleTest. It is the only step that
uses the network. Configure and build read
that checkout and fetch nothing, so once it exists, any number of build
directories can be created offline. For a mirror or an air-gapped machine:

```sh
cmake -DELEMENTAL_KCONFIGLIB_SOURCE=/path/or/url/of/a/clone -P tools/provision.cmake
cmake -DELEMENTAL_KCONFIGLIB_DIR=/shared/kconfiglib -P tools/provision.cmake
```

The commit is still required to match, so the content is identical wherever
it came from. A build finds the checkout through the `ELEMENTAL_KCONFIGLIB_DIR`
cache variable, which defaults to `.deps/kconfiglib` in the Elemental tree.

**Every configure verifies the dependency**, because the obvious failure is
quiet: the fork and upstream Kconfiglib both report version 14.1.0, and a
distribution's upstream package is importable from the same interpreter.

1. The checkout's `HEAD` is the pinned commit.
2. It has no modified tracked files.
3. Python, run the way the build runs it, imports `kconfiglib` from that
   checkout and not from anywhere else, and the module has the fork's CMake
   output support.

Kconfiglib processes run with `PYTHONPATH` set to the checkout and
`PYTHONPYCACHEPREFIX` set inside the build tree, so the result does not
depend on installed packages and nothing is written into the checkout.
`-DELEMENTAL_KCONFIGLIB_VERIFY=OFF` accepts an unverified copy, with a
warning; it exists for packagers who vendor a tarball.

**GoogleTest** is built from the checkout in `.deps/googletest` as part of
the test build, never fetched and never taken from the system. It is
therefore compiled with the same compiler and flags as the tests, sanitizers
included. A configure that builds tests checks that the checkout is at the
pinned commit; a copy without git metadata is accepted with a note. It is
found through `ELEMENTAL_GOOGLETEST_DIR`, and the provisioning script takes
`ELEMENTAL_GOOGLETEST_SOURCE` and `ELEMENTAL_GOOGLETEST_DIR` like the
Kconfiglib options above. gMock is not built.

To move either dependency to a new revision: edit the pin, run the
provisioning script, run every preset.

## Development commands

```sh
cmake --preset gcc && cmake --build --preset gcc && ctest --preset gcc
```

| Preset | What it is |
|---|---|
| `gcc` | GCC, default configuration, warnings as errors. |
| `clang` | Clang, same. |
| `sanitize` | Clang with AddressSanitizer and UndefinedBehaviorSanitizer, and the application assertion handler instead of the default trap. |
| `cortex-m3` | Freestanding cross build with Clang and LLD: compile and symbol check. No tests run on the target. |

Each preset builds in `build/<preset>`. Useful subsets:

```sh
ctest --preset gcc -LE integration     # unit, contract, interop: about a second
ctest --preset gcc -L integration      # consumption and configuration: ~30 s
ctest --preset gcc -R io.helpers       # one group of tests
tools/format.sh                        # clang-format, in place
```

### Tests

Tests are [GoogleTest](https://github.com/google/googletest). A test
executable is defined with `elemental_add_test()`
([cmake/ElementalTesting.cmake](../cmake/ElementalTesting.cmake)), which
links GoogleTest's `main()` and registers every `TEST` with CTest as
`<name>.<Suite>.<Test>`. Tests are listed when `ctest` runs, so a build
never executes a test binary. A test binary can also be run directly with
GoogleTest's flags:

```sh
build/gcc/modules/io/tests/test_io_helpers --gtest_filter='IoReadExact.*'
```

Tests are C++17. Fixtures and helpers that describe a C implementation stay
in C and are linked into the same executable
([modules/io/tests/fixture_mem.c](../modules/io/tests/fixture_mem.c)).

### Compiler flags

Elemental's build files set no global flags and no global include
directories. Strict diagnostics are attached to Elemental's own targets,
`PRIVATE`, by `elemental_target_warnings()` in
[cmake/ElementalModule.cmake](../cmake/ElementalModule.cmake); nothing
reaches a consumer's compile lines. `-Werror` is opt-in
(`ELEMENTAL_WARNINGS_AS_ERRORS`), on in the presets and off for consumers,
whose compilers Elemental has not seen.

Flags that describe the build environment rather than the project are set
where they belong: sanitizer flags in the `sanitize` preset, target flags in
the toolchain file.

## Configuration

### One resolved configuration

```
Kconfig + modules/*/Kconfig        what can be configured
<build>/kconfig/.config            what is selected (mutable, per build dir)
            |
     kconfig_configure()           the fork's CMake package, at configure time
            |
   +--------+-------------------------+
   |                                  |
elemental_config.cmake          elemental_config.h
EL_CONFIG_* variables         EL_CONFIG_* macros
which targets exist             what the sources compile
```

Both outputs come from the same run over the same input, so a CMake decision
and a C `#ifdef` cannot disagree. Kconfig symbols are written without a
prefix (`IO_POSIX`); everything generated is named `EL_CONFIG_<SYMBOL>`,
so that Elemental can be embedded in a project that uses Kconfig with the
conventional `CONFIG_` prefix.

All of it lives under `<build>/kconfig/`. The source tree is never written
to. Build directories share nothing, so two with different configurations
can sit side by side.

### The options

| Symbol | Default | Use case |
|---|---|---|
| `STATUS`, `IO` | on | Build only the modules a product uses. |
| `IO_POSIX` | on where `<unistd.h>` exists | The POSIX backend cannot be built for a target without it. |
| `ASSERT` | on | Size-constrained images that were validated with it on. See [code size](#code-size-of-the-assertion-configurations). |
| `ASSERT_HANDLER_TRAP` / `_APPLICATION` | trap | Route failures into a product's fault handling, or observe them in tests. |
| `PLATFORM_POSIX` | probed | Not settable. CMake checks for `<unistd.h>` and passes the answer in. |

An option needs a use case to be added
([new-module.md](process/new-module.md)). An option whose effect the linker
already provides, such as leaving out a function nobody calls, is not added.

### Changing the configuration

**Defaults.** Configure with nothing. Every option has a working default.

**From a file, non-interactively.** A defconfig lists only what differs from
the defaults:

```sh
cmake -B build -DELEMENTAL_DEFCONFIG=configs/assert_application_defconfig
```

It seeds a build directory that has no configuration yet. After that,
`<build>/kconfig/.config` is the single mutable input. Edit that file, or
delete it and re-run CMake to start again from the defconfig. Passing a
different or modified defconfig to an already configured directory is
reported with a warning rather than silently ignored.

**Interactively.**

```sh
cmake --build build --target menuconfig        # also: guiconfig
cmake --build build --target savedefconfig     # writes <build>/kconfig/elemental_config.defconfig
```

`savedefconfig` produces the minimal file to check in under `configs/`.
`menuconfig` needs a terminal with `TERM` set. When
Elemental is a subproject these targets are named
`elemental_config_menuconfig` and so on, to stay out of the application's
target namespace.

**Regeneration is automatic.** After any change to `.config` or to a Kconfig
file, just build. CMake re-runs, the header and the CMake variables are
regenerated, targets that are no longer enabled disappear, and sources that
include the header recompile. *Checked* by `integration.regenerate`, which
also checks that a second build has nothing to do.

### Diagnostics

Kconfig resolves a configuration by quietly dropping what it cannot honor.
For a build that is the wrong behavior, so the input is checked first, using
Kconfiglib's own evaluator ([tools/kconfig_check.py](../tools/kconfig_check.py)),
and configuration stops with the reason:

```
Invalid Elemental configuration.
  my_defconfig cannot be applied as written:
    - EL_CONFIG_IO_POSIX is assigned 'y' but resolves to 'n':
      unsatisfied dependencies: IO && PLATFORM_POSIX
```

Reported: an unknown symbol, a selection whose dependencies are unmet,
disabling something another enabled module selects, two members of one
choice, an attempt to assign a probed symbol, a value out of range.

A defconfig is held to this exactly. An existing `.config` is allowed one
thing a defconfig is not: settings left behind by turning off the option
they depend on, which is how hand-edited Kconfig files normally look.
*Checked* by `integration.diagnostics`.

## Consuming Elemental

There are three supported ways. They differ in who runs Kconfig.

| | Installed package | Subproject | Source drop |
|---|---|---|---|
| How | `find_package(Elemental)` | `add_subdirectory()` or FetchContent of the repository root | Copy `modules/<m>/` into your tree |
| Kconfig runs in | Nobody's build. Frozen at install. | Elemental's subdirectory, at configure time | Nowhere |
| Consumer needs | CMake, C compiler | plus Python ≥ 3.9 and the provisioned Kconfiglib | A C compiler |
| To run a contract suite | plus a C++17 compiler and the consumer's own GoogleTest | the same | not supported |
| Configuration chosen by | Whoever built the package | The application, with `ELEMENTAL_DEFCONFIG` | A config header you supply |
| Example, checked by | `examples/consume_installed`, `integration.installed` | `examples/consume_subdirectory`, `integration.subdirectory` | `integration.raw_source` |

**The simplest way to use a module without running Kconfig is the installed
package.**

### Installed package

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release -DELEMENTAL_BUILD_TESTS=OFF \
      -DELEMENTAL_BUILD_EXAMPLES=OFF
cmake --build build
cmake --install build --prefix /opt/elemental
```

```cmake
find_package(Elemental 0.1 REQUIRED COMPONENTS io io_posix)
target_link_libraries(app PRIVATE elemental::io_posix)
```

Components are module names. Requesting one the package was built without
fails at `find_package`, naming the module and the package's configuration
file.

A contract suite is also a component (`io_contract`). It is installed as
source, under `share/elemental/<suite>/`, and compiled into the test
executable that links it. The package does not include GoogleTest: the
consumer defines `GTest::gtest`, from `find_package(GTest)` or from a source
tree in its own build.
[examples/consume_installed](../examples/consume_installed/CMakeLists.txt)
shows both.

Without CMake, link `lib/libelemental_<module>.a` followed by the libraries
of the modules it depends on. There is no pkg-config file.

### Subproject

```cmake
set(ELEMENTAL_DEFCONFIG "${CMAKE_CURRENT_SOURCE_DIR}/elemental_defconfig" CACHE FILEPATH "")
add_subdirectory(third_party/elemental EXCLUDE_FROM_ALL)
target_link_libraries(app PRIVATE elemental::io)
```

As a subproject Elemental defines its library targets and nothing else: no
tests, no examples, no install rules, no short tool targets. With
`EXCLUDE_FROM_ALL`, only the modules the application links are compiled.

### Source drop

A module's sources need one thing the build would otherwise generate: a file
named `elemental_config.h` on the include path. Write it by hand from the
module's Kconfig, or copy one from any build directory.

```c
/* elemental_config.h */
#define EL_CONFIG_STATUS 1
#define EL_CONFIG_ASSERT 1
#define EL_CONFIG_ASSERT_HANDLER_TRAP 1
#define EL_CONFIG_IO 1
```

```sh
cc -std=c11 -I. -Imodules/status/include -Imodules/io/include \
   modules/status/src/status.c modules/io/src/io.c modules/io/src/io_mem.c app.c
```

A `bool` symbol that is on is defined as `1`; one that is off is not defined.

## Configuration, installed artifacts, and compatibility

An installed Elemental is **one configuration, frozen**. The package records
it in three places:

| File | For |
|---|---|
| `include/elemental_config.h` | C sources that want to test `EL_CONFIG_*`. Not included by any other public header. |
| `lib/cmake/Elemental/ElementalConfiguration.cmake` | Loaded by `find_package`: every `EL_CONFIG_*` as a CMake variable, listed in `Elemental_CONFIG_SYMBOLS`. |
| `lib/cmake/Elemental/elemental.config` | The `.config` it was built from, as `Elemental_CONFIG_FILE`. |

What configuration changes in the installed artifacts:

| Changes | Does not change |
|---|---|
| Which libraries and headers are installed (per module). | Any public type, layout, or function signature. |
| The compiled behavior of the libraries (assertions on or off, what a failure does). | The public headers' contents, apart from `elemental_config.h`. |
| Whether the application must define `el_assert_failed()`. | |

That second column is a rule, not an accident
([ADR 0004](adr/0004-private-configuration.md)), and it is what makes mixing
tractable: two configurations of the same version are source- and
link-compatible for a consumer, and differ only in behavior.

A consumer that depends on a particular configuration can say so:

```cmake
find_package(Elemental 0.1 REQUIRED COMPONENTS io)
if(NOT EL_CONFIG_ASSERT)
  message(FATAL_ERROR "This product requires Elemental built with contract assertions")
endif()
```

**Mixing is refused where the build can see it.**

| Situation | Outcome |
|---|---|
| A needed module is not in the package | `find_package` fails, naming it. |
| `add_subdirectory` and `find_package` in one build, either order | Configuration fails: one build, one Elemental. |
| The package was built for a different pointer size, or a different minor version | `find_package` rejects it. |
| The application-handler configuration, and no `el_assert_failed()` | Link error naming the symbol. |

The first two rows are *checked* by `integration.installed`. The third is
the behavior of the version file CMake generates, and the fourth follows
from the library referring to a symbol it does not define, which
`integration.isolation` confirms; neither was exercised end to end.

**Not detected:** two differently configured Elemental archives handed
directly to a linker outside CMake, for instance one bundled inside a
prebuilt third-party library. Static linking resolves each symbol from the
first archive that has it and says nothing. Today that mixes behavior, not
layouts, because of the rule above. Other build settings that are not
Kconfig options (optimization, `-fPIC`, sanitizers) are the builder's
responsibility as with any C library.

## Freestanding and cross builds

### Toolchain

[cmake/toolchains/clang-cortex-m3.cmake](../cmake/toolchains/clang-cortex-m3.cmake):
Clang targeting `thumbv7m-none-eabi`, `-mcpu=cortex-m3`, `-ffreestanding`.
No C library, no vendor SDK, and no compiler runtime library is involved.
That is deliberate: anything Elemental needs from its environment appears as
an undefined symbol.

The target was chosen because the tools were at hand. A GNU Arm toolchain
was not available and is untested.

### Runtime dependencies

Elemental's freestanding libraries (`status`, `io`) need one symbol from
their environment: `memcpy`. GCC and Clang require it of every freestanding
environment regardless.

This is enforced: `cross.symbols` lists what the libraries leave undefined
and fails on anything not in
[allowed-undefined.txt](../tests/cross/allowed-undefined.txt). On a target
that needs soft-float or 64-bit division helpers, the compiler will ask for
those too; nothing in the slice does.

### Verification levels

These are five different kinds of evidence. The cross build provides the
first two.

| Level | What it shows | Here |
|---|---|---|
| Compile check | The sources are valid for the target. | Building the `cortex-m3` preset. |
| Symbol check | What the libraries need from outside. | `cross.symbols` |
| Link check | A complete image can be produced with the stated runtime. | **Not done.** |
| Emulator run | The code behaves correctly on an emulated CPU. | **Not done.** |
| Hardware test | It works on a real device. | **Not done.** |

The tests are GoogleTest, which needs a hosted C++ library, so they do not
run on a bare freestanding target
([ADR 0008](adr/0008-googletest.md)). An earlier revision linked a bare
Cortex-M3 image and ran a C contract suite under QEMU; that was given up
with the move to GoogleTest. Running the tests on microcontroller targets is
planned through Zephyr, which is the next milestone
([roadmap](roadmap.md#m1--close-the-verification-gaps)). Until then, every
behavioral result in this document comes from an x86-64 Linux host.

### Code size of the assertion configurations

`libelemental_io.a`, Cortex-M3, Clang 23.1.4, `MinSizeRel`, text bytes:

| Configuration | Bytes |
|---|---|
| Assertions off | 484 |
| Trap in place (default) | 662 |
| Application handler | 1084 |

The handler configuration pays for a call, a file name, and a line number
per check. These are measurements of one compiler at one optimization
level, recorded as the evidence for the `ASSERT` option's help text.

## What is verified

On one x86-64 Linux machine, 2026-11-11. "Passed" means the command was run
and exited successfully.

| Check | GCC 16.2.1 | Clang 23.1.4 | Clang + ASan/UBSan | Cortex-M3 |
|---|---|---|---|---|
| Build with strict warnings as errors | passed | passed | passed | passed (libraries) |
| Unit and helper tests | passed | passed | passed | not run |
| Contract violations stop the program (`io.violations`) | passed, trap | passed, trap | passed, application handler | not run |
| io contract suite: memory streams | passed | passed | passed | not run |
| io contract suite: POSIX, blocking and non-blocking | passed | passed | passed | n/a |
| io contract suite: ring pipe example | passed | passed | passed | not run |
| POSIX platform tests | passed | passed | passed | n/a |
| Public headers as C11 and C++11 | passed | passed | passed | not run |
| C++ interop, run | passed | passed | passed | not run |
| Rust interop, run | passed | passed | skipped | not run |
| Python sketch, compile only | passed | passed | passed | not run |
| Integration (7 cases) | passed | passed | passed | not run |
| Symbol check | n/a | n/a | n/a | passed |

The hosted presets run 81 GoogleTest tests, of which 7 are reported as
skipped because a fixture lacks an optional capability
([why](../modules/io/README.md#the-contract-suite)), plus the interop and
integration tests: 90 CTest tests in all, 89 under sanitizers. The Rust
sketch is skipped under sanitizers because a plain `rustc` link has no
sanitizer runtime. The `cortex-m3` preset has one test, the symbol check.

Additionally, each run once by hand and not part of the automated suite:

- The whole test suite with CMake 3.22.6, the declared minimum, using the
  Unix Makefiles generator and no preset.
- A fresh configure, build, and the non-integration tests inside a network
  namespace with no network, after provisioning.
- `menuconfig` started through its build target under a pseudo-terminal,
  showed the Elemental menu, and quit cleanly; `savedefconfig` wrote back
  the one-line defconfig the `sanitize` preset was seeded from. (Done
  before the move to GoogleTest; that change did not touch configuration.)
- Six deliberately broken variants of the ring pipe against the contract
  suite ([modules/io/README.md](../modules/io/README.md#the-contract-suite)).

Not verified:

- **The CI workflow has never run.** It is syntactically valid YAML and
  mirrors the commands above; nothing more is known about it.
- Any compiler other than the two above, including the older GCC and Clang
  the CI image would use, MSVC, and vendor compilers. The MSVC branches in
  the build files and in `assert.h` are untested.
- Any operating system other than Linux. The POSIX backend is expected to
  work elsewhere and has not been tried.
- Any Python other than 3.14.9.
- **Any behavior on a non-host target.** No emulator, no hardware. For the
  Cortex-M3 the evidence is that the libraries compile and need only
  `memcpy`.
- The contract suite or any test under Zephyr.
- A GNU Arm toolchain.
- Anything executed in the Python sketch.
