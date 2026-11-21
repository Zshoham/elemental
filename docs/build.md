# Building, configuring, and consuming Elemental

## Requirements

| Tool | Baseline | Why this baseline | Tested with |
|---|---|---|---|
| CMake | 3.22 | Ships with Ubuntu 22.04 LTS. Needs `PROJECT_IS_TOP_LEVEL` and presets schema 3 (3.21). The Kconfiglib fork's package needs 3.15. | 3.22.6 and 4.4.5 |
| C compiler | C11 | See [ADR 0006](adr/0006-c11-and-naming.md). | GCC 16.2.1, Clang 23.1.4 |
| C++ compiler | C++17, tests only | GoogleTest 1.18 requires it ([ADR 0008](adr/0008-googletest.md)). The libraries are C and need no C++ compiler. | GCC 16.2.1, Clang 23.1.4 |
| Python | 3.9 | The Kconfiglib fork's stated minimum. Standard library only. | 3.9.23 and 3.14.9 |
| Git | any | Provisioning, and verifying the pinned revisions. | 2.56.0 |
| Ninja | any | Used by the presets. Other generators work with plain `cmake -B`. | 1.13.2 |

The host is Linux; no other operating system is supported
([roadmap](roadmap.md#deferred-deliberately)).

Optional, each enabling a check that is otherwise skipped with a message:
`rustc` (Rust interop sketch) and CPython development headers (Python sketch
compile check). Tests on a microcontroller need a Zephyr workspace
([Tests on Zephyr](#tests-on-zephyr)).

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
| `sanitize` | Clang with AddressSanitizer and UndefinedBehaviorSanitizer, configured with [configs/variant_defconfig](../configs/variant_defconfig): one non-default option of every Kconfig choice (the application assertion handler, 256-entry CRC tables, `FMT_PRINTF_LIBC`). Its test environment sets `allocator_may_return_null=1`, because the allocator contract suite asks the C library for impossible sizes and expects `NULL`, which AddressSanitizer otherwise turns into a fatal error. (`CMakePresets.json` cannot carry this comment: `$comment` needs presets schema 10, CMake 3.31.) |
| `tsan` | Clang with ThreadSanitizer, default configuration. Its test preset runs only the tests of `ring` and of the deferred log sink, two groups of which run a thread on each side (`ring.stress`, `log.deferred.stress`): the one place two contexts touch one object. ThreadSanitizer cannot be combined with AddressSanitizer, so it is a preset of its own. No death tests. |
| `assert-off` | GCC with [configs/assert_off_defconfig](../configs/assert_off_defconfig): contract assertions compiled out. Every test except the death tests runs, so no behavior depends on an assertion being there. |

**Configurations.** `gcc`, `clang` and `tsan` run the defaults, `sanitize`
one non-default option of every choice, and `assert-off` the configuration
without assertions. Each is one defconfig under [configs/](../configs). A
new Kconfig choice adds an alternative to `variant_defconfig`
([new-module.md](process/new-module.md#configuration-options)), preferably
one that builds both on the host and on Zephyr. Every option of every
choice is covered by `integration.choices`, which needs no change for a new
choice: it enumerates the choices with Kconfiglib, and for each option this
compiler can select configures a build with only that option set, builds
the owning module's tests, and runs them. An option it cannot select must
be hidden by a probed fact, which it names. With GCC 16 it tests the seven
options of `ASSERT_HANDLER`, `CRC_IMPLEMENTATION`, and `FMT_PRINTF` this
host has; with Clang, the same less `CRC_GCC_BUILTIN`. No host can select
`CRC_ZEPHYR`, which the Zephyr scenario `elemental.tests.crc_zephyr`
covers. Module
subsets are covered by `integration.module_subsets`: each module and
backend alone, with only what its Kconfig entry depends on or selects,
must configure, build exactly those libraries, and pass its tests.

Every preset is a Linux host build. Microcontroller targets are built and
tested through Zephyr ([Tests on Zephyr](#tests-on-zephyr)).

Each preset builds in `build/<preset>`. Useful subsets:

```sh
ctest --preset gcc -LE integration     # unit, contract, interop: about a second
ctest --preset gcc -L integration      # consumption and configuration: ~2 min
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
([modules/io/tests/fixture_buf.c](../modules/io/tests/fixture_buf.c)).

### Compiler flags

Elemental's build files set no global flags and no global include
directories. Strict diagnostics are attached to Elemental's own targets,
`PRIVATE`, by `elemental_target_warnings()` in
[cmake/ElementalModule.cmake](../cmake/ElementalModule.cmake); nothing
reaches a consumer's compile lines. `-Werror` is opt-in
(`ELEMENTAL_WARNINGS_AS_ERRORS`), on in the presets and off for consumers,
whose compilers Elemental has not seen.

Flags that describe the build environment rather than the project are set
where they belong: sanitizer flags in the `sanitize` preset, a board's
target flags in Zephyr's `zephyr_interface`.

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
| `STATUS`, `IO`, `RING`, `ALLOC`, `FMT`, `BYTES`, `CRC`, `TIME`, `LOG` | on | Build only the modules a product uses. `LOG` selects `FMT`, `TIME` and `IO`; `RING` selects `IO` and needs `COMPILER_ATOMICS`. The compile-time log cut-off is not an option here: it is the consumer's macro `EL_LOG_CUTOFF` ([ADR 0021](adr/0021-log-cutoff.md)). |
| `CRC_TABLE_4BIT` / `_8BIT` / `CRC_GCC_BUILTIN` / `CRC_ZEPHYR` | 4-bit | What computes the CRCs: 96 bytes of tables or 1.5 KiB, GCC 15's CRC builtins, or Zephyr's `sys/crc.h`. Results are identical ([modules/crc](../modules/crc/README.md#configuration)). |
| `FMT_PRINTF_BUILTIN` / `FMT_PRINTF_LIBC` | built-in | What printf accepts. `FMT_PRINTF_LIBC` adds C's other numeric conversions (floats, `%p`, flags, precision, `*`) through the C library's `snprintf`, one at a time, whole or absent; the portable subset is built-in code under both ([modules/fmt](../modules/fmt/README.md#configuration), [ADR 0016](adr/0016-private-choices-that-widen.md)). |
| `IO_POSIX` | on where `<unistd.h>` exists | The POSIX backend cannot be built for a target without it. |
| `IO_ALLOC` | on where `ALLOC` is on | `el_grow_buf`, buffer storage that grows, part of `elemental::io`. Off, io builds and links no allocator. |
| `LOG_DEFERRED` | on where `RING` is on | `el_log_deferred_sink`, the sink for an interrupt handler, part of `elemental::log`. Off, log builds and links no ring. |
| `ALLOC_LIBC` | on where `<unistd.h>` exists | The C library heap backend needs a hosted C library; the POSIX probe stands in for one, Linux being the only host. |
| `TIME_POSIX` | on where `<unistd.h>` exists | The POSIX clock needs `clock_gettime`. |
| `TIME_ZEPHYR` | on in a Zephyr build | The Zephyr clock needs the kernel. It cannot be selected anywhere else. |
| `ASSERT` | on | Size-constrained images that were validated with it on. See [code size](#code-size-of-the-assertion-configurations). |
| `ASSERT_HANDLER_TRAP` / `_APPLICATION` | trap | Route failures into a product's fault handling, or observe them in tests. |
| `PLATFORM_POSIX` | probed | Not settable. CMake checks for `<unistd.h>` and passes the answer in. |
| `PLATFORM_SNPRINTF` | probed | Not settable. A hosted C library's `snprintf`, or on Zephyr a picolibc printf level that converts `long long`. |
| `COMPILER_GCC_CRC_BUILTINS` | probed | Not settable. CMake compiles a call to `__builtin_rev_crc32_data8` and `__builtin_crc16_data8`. |
| `PLATFORM_ZEPHYR_CRC` | probed | Not settable. A Zephyr build with `CONFIG_CRC` and without `CONFIG_CRC_HW_HANDLER`, read from Zephyr's configuration. |
| `PLATFORM_ZEPHYR` | probed | Not settable. The build is a Zephyr application's: Zephyr's `zephyr_library()` command exists. |
| `COMPILER_ATOMICS` | probed | Not settable. CMake compiles an acquire load and a release store of an `_Atomic size_t` ([ADR 0023](adr/0023-atomics-and-interrupt-context.md)). That they are instructions and not calls is the [symbol check](#the-symbol-check)'s to show. |

An option needs a use case to be added
([new-module.md](process/new-module.md)). An option whose effect the linker
already provides, such as leaving out a function nobody calls, is not added.

### Changing the configuration

**Defaults.** Configure with nothing. Every option has a working default.

**From a file, non-interactively.** A defconfig lists only what differs from
the defaults:

```sh
cmake -B build -DELEMENTAL_DEFCONFIG=configs/variant_defconfig
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
| To check printf literals | plus Python ≥ 3.9 | uses the same Python | call the checker directly |
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

### Checking printf formats

An installed package and a CMake subproject both provide
`elemental_check_formats()`. Call it after defining your target.

```cmake
target_link_libraries(app PRIVATE elemental::log)
elemental_check_formats(app FUNCTIONS report:2)
```

It checks literal formats passed to the fmt and log printf functions,
including calls through `EL_LOG` and the four level macros. The default
is the [portable subset](../modules/fmt/include/elemental/fmt_printf.h),
accepted with either formatter. Pass `LIBC` if the target requires
`FMT_PRINTF_LIBC`. Both modes reject `%n`, `%lc` and `%ls`.
`FUNCTIONS` adds wrapper names and their format argument positions,
counting from 1. Omit it if the target uses only Elemental's entry points.
Use `EL_FMT_PRINTF_CHECK` on your wrappers for the compiler's separate
argument type check.

The function uses a compiler launcher with Ninja or Makefiles, GCC or
Clang, and host Python ≥ 3.9. Python is looked up when the function is
called. The launcher preprocesses each compilation with that target's
compiler, flags and headers. This uses the target C library's actual
`<inttypes.h>` definitions, including when cross-compiling. It reads
adjacent literals as one format and reports violations at their source
lines. The build stops if any are found.

The checker matches direct calls by name and reads narrow strings with
an ASCII-compatible execution character set. It skips computed formats
and indirect calls. In C++, enclose an argument that
contains a template argument list in parentheses so its boundary is
visible to the checker. The compiler still checks argument types and the
formatter still checks its contract at run time.

For a source drop, run `python3 tools/check_formats.py -- cc -E -dD ...`
with the source's usual compiler flags and include paths.

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

## Tests on Zephyr

Elemental supports a microcontroller only through Zephyr
([ADR 0010](adr/0010-zephyr-targets.md)).
[tests/zephyr](../tests/zephyr/CMakeLists.txt) is a Zephyr application that
adds Elemental as a subproject and compiles every test registered with
`elemental_add_test()` into one image. Twister runs it.

### The workspace

| Part | Pin |
|---|---|
| Zephyr | `v4.5.0`, commit `41d10b59730406bbab4ca9f1fbbe9e96aafb4966` |
| West modules | `cmsis` `512cc7e895e8`, `cmsis_6` `1c1840af7a7e`, `picolibc` `01254932e8e8`, `hal_nordic` `c708b9fe1b7a` (for `qemu_cortex_m0` only), `hal_stm32` `c95501991862` (for `nucleo_g0b1re_renode` only; 1.3 GB even fetched shallow); nothing else |
| Zephyr SDK | 1.0.1, the minimal bundle with only the `arm-zephyr-eabi` toolchain (GCC 14.3.0); no SDK host tools |
| Renode | 1.17.0, the Linux portable release (`renode-1.17.0.linux-portable.tar.gz`, SHA-256 `4ba7c68b59e2447f188ef4b4b112fcccd2802582c460beedceb63b84e5605a5f`), unpacked to `~/zephyrproject/renode` (184 MB) and put on `PATH`. No root, and no system .NET or Mono. |
| Host tools | `qemu-system-arm`, `dtc`, `gperf`, and a 32-bit capable `gcc` for `native_sim`, from the distribution |

Set up once, without root (the CI job does the same):

```sh
python3 -m venv ~/zephyrproject/.venv && . ~/zephyrproject/.venv/bin/activate
pip install west
west init -m https://github.com/zephyrproject-rtos/zephyr --mr v4.5.0 ~/zephyrproject
cd ~/zephyrproject
west config manifest.project-filter -- '-.*,+cmsis,+cmsis_6,+picolibc,+hal_nordic,+hal_stm32'
west update --narrow -o=--depth=1
pip install -r zephyr/scripts/requirements-base.txt \
  -r zephyr/scripts/requirements-build-test.txt -r zephyr/scripts/requirements-run-test.txt
west sdk install -t arm-zephyr-eabi -H
mkdir renode && curl -L https://github.com/renode/renode/releases/download/v1.17.0/renode-1.17.0.linux-portable.tar.gz |
  tar -xz -C renode --strip-components=1   # then check the SHA-256 above
export PATH=~/zephyrproject/renode:$PATH
```

Elemental's own dependencies are provisioned as for a host build
(`cmake -P tools/provision.cmake`): the application uses the same Kconfiglib
and GoogleTest checkouts in `.deps/`.

### Running

From inside the workspace, with its environment active:

```sh
west twister -T <elemental>/tests/zephyr --board-root <elemental>/tests/zephyr/boards \
  --integration -O <output directory>
```

`--board-root` names the directory that holds `nucleo_g0b1re_renode`;
without it Twister does not know that board. Without `renode` on `PATH`,
Twister builds that board's image and does not run it.

| Scenario | Board | What it does |
|---|---|---|
| `elemental.tests.<module>`: `status`, `alloc`, `io`, `ring`, `fmt`, `bytes`, `crc`, `time`, `log` | `native_sim` | One module's tests, in a 32-bit Linux process. |
| `elemental.tests.<module>` | `mps2/an385` | One module's tests, on an emulated Cortex-M3 (ARMv7-M) under QEMU; symbol check. |
| `elemental.tests.<module>` | `nucleo_g0b1re_renode` | One module's tests, on a modified Renode model of the STM32G0B1, a Cortex-M0+ (ARMv6-M); symbol check. |
| `elemental.tests.ring.uart` | `mps2/an385` | The `ring` image with the [UART example](../examples/uart_ring)'s test: `ELEMENTAL_ZEPHYR_UART_WIRE=ON` makes QEMU join the board's UART1 and UART2 through a socket, and `CONFIG_UART_INTERRUPT_DRIVEN=y` gives the receive interrupt. The one scenario in which an interrupt handler runs Elemental code. |
| `elemental.tests.variant` | `mps2/an385` | Every test, in one image, with [configs/variant_defconfig](../configs/variant_defconfig) applied on top of the image's own defconfig (`ELEMENTAL_ZEPHYR_VARIANT=variant`). |
| `elemental.tests.assert_off` | `mps2/an385` | The same with [configs/assert_off_defconfig](../configs/assert_off_defconfig). |
| `elemental.tests.fmt.variant` | `nucleo_g0b1re_renode` | The `fmt` image with `variant_defconfig`: the libc printf option (`FMT_PRINTF_LIBC`) on ARMv6-M. |
| `elemental.tests.crc_zephyr` | `mps2/an385` | The same with [configs/crc_zephyr_defconfig](../configs/crc_zephyr_defconfig) and `CONFIG_CRC=y`: `crc` through Zephyr's functions, the one choice option no host can select. |
| `elemental.symbols.armv6m` | `qemu_cortex_m0` | Build only: the libraries for a Cortex-M0 (ARMv6-M), and the symbol check. The test image does not fit this board. Kept because it needs no emulator and no `hal_stm32`. |

**`nucleo_g0b1re_renode`** is a board defined in this repository
([tests/zephyr/boards](../tests/zephyr/boards/st/nucleo_g0b1re_renode/README.md)):
Zephyr's `nucleo_g0b1re` (STM32G0B1RE, 512 KB flash, 144 KB RAM), run on
Renode's stock STM32G0 model with three changes, each commented in
[stm32g0b1.repl](../tests/zephyr/boards/st/nucleo_g0b1re_renode/support/stm32g0b1.repl):
flash 128 KB to 512 KB and RAM 48 KB to 144 KB (the stock model is an
STM32G071), and the CPU declared a Cortex-M0+ instead of a Cortex-M0. The
board's devicetree is the stock one with the HSI48 clock off, because the
model's clock controller never reports it ready. A run on it is an
**emulator run on a modified model**: not a hardware run, and not a model
Renode or Zephyr maintains. The tests use the core, SysTick, and USART2;
Renode reports accesses to two blocks it does not model (`SYSCFG`, `DBG`)
and one unmodelled flash-controller bit, all during start-up, none read
back as anything the tests depend on.

One board by hand: `west build -b mps2/an385 <elemental>/tests/zephyr`, then
`west build -t run`. The image prints GoogleTest's report and then the
memory the run needed. On `native_sim` the process does not exit when the
tests finish; Twister stops it.

### What is in the image

- **The tests.** On the host each test is an executable; here
  `elemental_add_test()` adds its sources to the image instead
  ([cmake/ElementalTestImage.cmake](../cmake/ElementalTestImage.cmake)).
  A module registers its tests once, inside `if(COMMAND elemental_add_test)`,
  and they run in both places.
- **Not the death tests.** A death test forks a process, which a Zephyr
  target cannot. `io.violations` is registered only where
  `ELEMENTAL_DEATH_TESTS` is on, which is the host. Contract violations are
  therefore checked on the host only.
- **Not `io_posix`, `alloc_libc`, or `time_posix`.** The application's
  [defconfig](../tests/zephyr/elemental_defconfig) turns them off.
- **`time_zephyr`, and its tests, only here.** It is the one backend a host
  cannot build ([PORT.md](../modules/time/backends/zephyr/PORT.md)): its
  contract fixture makes time pass with `k_sleep()`.
- **GoogleTest** from the same pinned checkout as the host build, compiled
  into the image as one file with `GTEST_HAS_FILE_SYSTEM=0`: no death tests,
  no output redirection, no XML report. Exceptions and RTTI stay off, as
  Zephyr builds C++ by default. The C++ library is the SDK's libstdc++ on
  picolibc (`CONFIG_REQUIRES_FULL_LIBCPP`).
- Two functions picolibc lacks on these boards, `isatty()` and
  `gettimeofday()`, supplied by the application
  ([libc_support.c](../tests/zephyr/src/libc_support.c)).
- A report of skipped tests in the form Twister counts. Twister's `gtest`
  harness does not recognise GoogleTest's `[ SKIPPED ]` and would fail the
  run ([main.cpp](../tests/zephyr/src/main.cpp)).

Every library in Elemental's build, GoogleTest included, is compiled with
the board's flags from `zephyr_interface`, and Elemental's own sources with
its strict warnings as errors. Zephyr's flags come later on the command line
and include `-std=c17`, so that the sources are C11 is checked by the host
presets only.

### Memory the test image needs

**One image per module.** GoogleTest's registry and per-test records take
heap for each test, so a board's RAM bounds how many tests one image can
hold. Since M3a each module's tests are their own image
(`ELEMENTAL_TEST_MODULE` in [tests/zephyr/CMakeLists.txt](../tests/zephyr/CMakeLists.txt)),
run as the scenario `elemental.tests.<module>` on every board, so a failing
module is its own line in Twister's report. An example's tests are in the
image of the module they test (the UART example with `ring`, the size
classes with `alloc`). The configuration scenarios (`variant`, `assert_off`,
`crc_zephyr`) build all the tests into one image (`ELEMENTAL_TEST_MODULE`
`all`, the default) on `mps2/an385`, which has the RAM for it. A test whose
name is in no module's list stops the configure, so **a new module adds its
name to `ELEMENTAL_TEST_MODULES` there and its scenario to
[testcase.yaml](../tests/zephyr/testcase.yaml).** A module whose image
outgrows the Cortex-M0+ board (peak heap near what static RAM leaves of its
147 456 bytes, less the margin below) splits its tests across two images
the same way.

M3a first ran the Cortex-M0+ board as a single image (it aborted at start,
out of heap: the whole image needs 122 336 bytes of peak heap on
`mps2/an385`), then as two and three module groups; the per-module images
replaced those on the owner's decision.

**M4, 2026-11-16, measured in the repository's own checkout (a different
path from M3a's worktree and from the M3 figures further below; the path
moves the heap figures by several KB, as explained there).** Flash, static
RAM and peak heap in bytes; main stack used of 4 096. Tests are
GoogleTest's count, skipped included. The six images that existed in M3b
(2026-11-15) measure exactly what they did then:

| Image | Tests | `mps2/an385`: flash, RAM, heap, stack | `nucleo_g0b1re_renode`: flash (of 524 288), RAM, heap, stack |
|---|---|---|---|
| `status` | 17 | 234 800, 15 576, 6 832, 1 324 | 245 812, 13 536, 7 236, 1 364 |
| `alloc` | 58 | 268 064, 41 904, 29 648, 1 836 | 279 652, 39 912, 31 376, 1 948 |
| `io` | 129 | 343 756, 17 744, 56 928, 2 636 | 355 744, 15 704, 60 788, 2 732 |
| `ring` | 26 | 264 300, 15 880, 15 432, 2 636 | 275 728, 13 840, 16 396, 2 732 |
| `ring`, with the UART example (`ring.uart`) | 29 | 277 176, 18 272, 16 472, 2 636 | not built |
| `fmt` | 32 | 268 348, 19 656, 13 912, 1 772 | 279 216, 17 616, 14 584, 1 892 |
| `bytes` | 38 | 283 220, 15 552, 12 888, 1 324 | 293 624, 13 512, 13 664, 1 364 |
| `crc` | 8 | 230 728, 15 552, 3 680, 1 324 | 241 868, 13 512, 3 872, 1 364 |
| `time` | 42 | 258 352, 15 728, 20 000, 1 796 | 269 892, 13 688, 21 228, 1 900 |
| `log` | 139 | 355 588, 35 912, 50 584, 2 100 | 367 920, 33 872, 53 760, 2 188 |
| All, `variant` | 497 | 692 452, 68 968, 181 480, 2 636 | not built |

The `log` and `variant` rows were measured again on 2026-11-17, after the
writer sink's three grown-buffer tests (`log.sinks.grown`) were added:
their arena, output and message are 11 241 bytes of static storage, most
of the 11 272 bytes of static RAM the `log` image gained (85 tests, 19 864
bytes of static RAM and 32 704 of heap on `mps2/an385` before). The other
images were unchanged.

The `time`, `log` and `variant` rows were measured again later on
2026-11-17, after the clock-taking conversions (one test) and a sink that
lends its buffer ([ADR 0022](adr/0022-log-sink-lends-the-buffer.md), 20
more tests in the image) were added. Before, `time` was 41 tests,
257 108, 15 720, 19 496, 1 796 and 268 680, 13 680, 20 692, 1 900; `log`
88 tests, 308 812, 31 136, 34 352, 2 508 and 320 752, 29 096, 36 452,
2 712; `variant` 435 tests, 633 756, 64 176, 161 976, 2 636.

M5 (2026-11-17) added the two `ring` rows and changed three. `io` lost the
ring pipe example's 16 tests to `ring` (145 tests, 350 228, 18 024,
63 576, 2 636 and 362 208, 15 984, 67 908, 2 732 before). `log` gained the
deferred sink's 31: their rings and storage are 4 568 bytes of static
RAM, and the heap grew by about 10 KB (108 tests, 326 436, 31 344, 40 232,
2 100 and 338 624, 29 304, 42 700, 2 188 before). `variant` was 456 tests,
652 076, 64 392, 168 288, 2 636. The other images measured the same.
`ring.uart` is `ring`'s image with the example's three tests, its UART
driver's interrupt-driven code, and a 1 KiB log ring.

`io` is the largest: about 61 KB of heap of the about 131 KB its static RAM
leaves on the Cortex-M0+. `alloc`'s static RAM is its fixtures' storage.
`log` uses the most stack after `io`, 2 188 of 4 096 bytes on the
Cortex-M0+, most of it the tests' and GoogleTest's own frames. A log call
has no message buffer of its own: summed by GCC's `-fstack-usage` for the
Cortex-M0+ at `-Os`, the frames from `el_log_printf` down to the writer
sink's operation are 160 bytes, against 296 when the message was
formatted in 128 bytes of `el_log_printf`'s stack (2 712 bytes for the
image then). `el_log_write`'s path grew instead, from 112 bytes to 192,
because the message is copied into the sink's buffer in a frame of its
own. The image
with every test (`variant`, before M4 306 tests, 519 592 bytes of flash and
116 432 of peak heap) no longer fits the Cortex-M0+ board's 147 456 bytes
of RAM by a wide margin, which is what the per-module images are for.
M3b added 12 tests to `status` and 2 to `io`. In M3a's worktree
(2026-11-13) the `status` image, then 5 tests, took 224 984 bytes of flash
and 3 024 of heap on `mps2/an385`, and `io`, then 143 tests, 348 092 and
70 416.

**Twister's run time** (`--integration -j 8`, this machine, fresh output
directory each time): 170.6 s for M3a's first run with one image per
board (7 configurations, the Cortex-M0+ image failing at start), 175.4 s
for the per-module layout (22 configurations, all passing), 184.6 s
for M4's 28 configurations, and 260.1 s for M5's 33 (`-j 8`, compiler
temporaries on the home volume). The extra
builds cost little because each module image compiles only its own tests;
both runs had Zephyr's build cache available, so read them as "about".

The figures below are M3's, for the single image that fitted every board then.

Zephyr SDK GCC 14.3.0, Zephyr's default size optimization, 203 tests.
The flash and heap figures are approximate: they change with the path of
the checkout, which reaches the image through GoogleTest's file names and
messages. The same sources in two directories differed by about 1 KB of
flash and up to 16 KB of peak heap. Read them as "about", and keep the
headroom below with that margin in mind. The figures below were taken in
the same checkout directory as the earlier ones they are compared with.

| | `mps2/an385` (Cortex-M3) | `nucleo_g0b1re_renode` (Cortex-M0+) |
|---|---|---|
| Flash (code and read-only data) | 440 676 | 451 680 of 524 288 |
| RAM, static | 43 688 | 41 696 |
| Heap, peak during the run | 76 576 | 81 592 |
| Main stack, used of 4 096 | 2 636 | 2 732 |

So the image needs about 441 KB of flash and 120 KB of RAM on the
Cortex-M3, and about 452 KB and 123 KB on the Cortex-M0+ (the static figure
includes the stack; the heap is whatever RAM is left). The Cortex-M0+ board
has the real STM32G0B1's 144 KB, which leaves about 21 KB of RAM and 71 KB
of flash for the tests still to come. The 11 byte-swap and bit-counting
tests added about 18 KB of flash and 3.7 KB of peak heap; the 21 tests of
rotate, bit reversal, bit fields, and alignment, with the 3 of allocation
layouts, added about 45 KB of flash and 2.5 KB of peak heap. The 47 tests of `fmt`, `bytes`, and
`crc` added about 59 KB of flash, almost all of it GoogleTest assertions
instantiated in the tests (the three libraries are under 2 KB), and about
15 KB of peak heap, GoogleTest's registry and the tests' strings; they keep
no static buffers. One of them had briefly taken the main stack to 3 580
bytes: a `std::mt19937_64` (2.5 KB of state) on the stack, replaced by a
small generator. The model was not enlarged past the
real part, so a test image that fits it fits the board. The allocator tests added the most: the arena and size-class fixtures' static storage (about 28 KB), and the contract suite's bookkeeping on the heap. It does not fit
`qemu_cortex_m0` (256 KB flash, 16 KB RAM), which is why that board only
builds the libraries. The default 1 KB main stack is not enough: the first
run on `mps2/an385` overflowed it silently, and two contract tests failed
on corrupted buffers. The stack is now 4 KB, and the stack sentinel is on.

### The symbol check

Every build of the application for an ARM board runs
[tools/check_symbols.py](../tools/check_symbols.py) over Elemental's
libraries: the symbols they leave undefined, after removing those they
define for each other, must be in
[allowed-undefined.txt](../tests/zephyr/allowed-undefined.txt), or the build
fails. The list has three entries: `memcpy`, `memmove` (the buffered
writer compacts its buffer with it, since M3a), and `el_assert_failed` in
the configuration that asks the application to define it. A symbol that only
one Kconfig choice needs is listed in `allowed-undefined.<SYMBOL>.txt`,
which the build reads only when `EL_CONFIG_<SYMBOL>` is set: `snprintf`, in
[allowed-undefined.FMT_PRINTF_LIBC.txt](../tests/zephyr/allowed-undefined.FMT_PRINTF_LIBC.txt),
Zephyr's `crc32_ieee_update` and `crc16_itu_t`, in
[allowed-undefined.CRC_ZEPHYR.txt](../tests/zephyr/allowed-undefined.CRC_ZEPHYR.txt),
and the kernel's `z_impl_k_uptime_ticks`, in
[allowed-undefined.TIME_ZEPHYR.txt](../tests/zephyr/allowed-undefined.TIME_ZEPHYR.txt),
so the other configurations still fail on them. `TIME_ZEPHYR` is on in
every Zephyr build, so that last symbol is allowed in all of them, for the
one library whose job is to call the kernel.

It has caught one helper so far: GCC 14.3.0 for ARMv6-M at `-Os` compiles a
dense `switch`, or an `if` chain it rebuilds into one, as a jump table that
calls libgcc's `__gnu_thumb1_case_uqi`. `fmt`'s printf parser hit it on
`qemu_cortex_m0`, and `fmt` is now compiled with `-fno-jump-tables`
([modules/fmt/CMakeLists.txt](../modules/fmt/CMakeLists.txt)). A module with
a large `switch` will meet the same.

| Board | CPU | Toolchain | Elemental needs |
|---|---|---|---|
| `mps2/an385` | Cortex-M3, ARMv7-M | Zephyr SDK GCC 14.3.0 | `memcpy`, `memmove`; `time_zephyr` the kernel's uptime; with `FMT_PRINTF_LIBC` (`variant`) also `snprintf` |
| `nucleo_g0b1re_renode` | Cortex-M0+, ARMv6-M | Zephyr SDK GCC 14.3.0 | `memcpy`, `memmove`; `time_zephyr` the kernel's uptime |
| `qemu_cortex_m0` | Cortex-M0, ARMv6-M | Zephyr SDK GCC 14.3.0 | `memcpy`, `memmove`; `time_zephyr` the kernel's uptime |

No compiler runtime helper (`__aeabi_*`, `__atomic_*`) is needed on either.
A module that divides 64-bit values or uses atomics will show up on the
Cortex-M0 first. `ring` and the deferred log sink use atomics, and need
none: their only atomic operations are a load and a store of one word,
which GCC 14.3.0 compiles to an `ldr` or `str` and a `dmb` on all three
boards. A read-modify-write such as `atomic_fetch_add` would be a call to
`__atomic_fetch_add_4` on ARMv6-M, which nothing provides
([ADR 0023](adr/0023-atomics-and-interrupt-context.md)). `ring` divides
nothing either: its indices count modulo twice the capacity, so a
position is a comparison and a subtraction.
`alloc` leaves no symbol undefined on either: `el_layout_array` checks
`count * size` for overflow by splitting each operand into halves, without
a division or a double-width multiply, which on the Cortex-M0 would be
`__aeabi_uidiv` and `__aeabi_lmul`. `time` converts ticks to and from
seconds and milliseconds the same way, by shift and subtract and from
16-bit halves: with `/` and `*` on its 64-bit values it would need
`__aeabi_uldivmod` and `__aeabi_lmul`, 504 bytes of libgcc
([time proposal](proposals/time.md#dependencies-and-cost)). `log` adds
nothing: `memcpy`, in the memory sink and the deferred sink. `ring` adds
nothing: `memcpy`.

### Verification levels

Five kinds of evidence, in increasing strength:

| Level | What it shows | Here |
|---|---|---|
| Compile check | The sources are valid for the target. | Every board above. |
| Symbol check | What the libraries need from outside. | `mps2/an385`, `nucleo_g0b1re_renode`, `qemu_cortex_m0` |
| Link check | A complete image can be produced. | The test image, `mps2/an385`, `nucleo_g0b1re_renode`, and `native_sim` |
| Emulator run | The code behaves correctly on an emulated CPU. | The test image on `mps2/an385` under QEMU (Cortex-M3), and on `nucleo_g0b1re_renode` under Renode 1.17.0, a modified model (Cortex-M0+) |
| Hardware test | It works on a real device. | **Not done.** No board. |

`native_sim` runs the tests on the host CPU in Zephyr's environment: it is
evidence about Zephyr integration, not about a target CPU.

### Code size of the assertion configurations

`libelemental_io.a` built for `mps2/an385` (Cortex-M3) by the Zephyr SDK's
GCC 14.3.0 at Zephyr's default size optimization, text bytes, without and
with grown buffer storage (`el_grow_buf`, `IO_ALLOC`, on by default).
Measured 2026-11-15 on M3b's branch; `io` includes `el_buf` and the
buffered writer (before M3a, with the memory and grow writers: 578 and 956
bytes, trap):

| Configuration | `IO_ALLOC` off | `IO_ALLOC` on | `el_grow_buf` |
|---|---|---|---|
| Assertions off | 1 134 | 1 396 | 262 |
| Trap in place (default) | 1 648 | 1 934 | 286 |
| Application handler | 2 121 | 2 456 | 335 |

M3b's status handling changed these by two assertions and nothing else:
`io` is 8 bytes larger with the trap and 20 with the handler, where the
buffered writer now asserts that an append it has made room for succeeded
instead of dropping its status, and is unchanged with assertions off.
`EL_TRY` in `io` and `fmt`, and `EL_NODISCARD` on every declaration, cost
nothing: every other number here is what it was on 2026-11-13.

The other libraries in the same build. `ring` and `log` as M5 left them,
measured 2026-11-17; `time` M4's, measured 2026-11-16; the rest are what
they were on 2026-11-15:

| Configuration | `alloc` | `status` | `ring` | `fmt` | `bytes` | `crc` | `time` | `time_zephyr` | `log` |
|---|---|---|---|---|---|---|---|---|---|
| Assertions off | 380 | 93 | 400 | 1 658 | 1 084 | 212 | 364 | 32 | 1 647 |
| Trap in place (default) | 572 | 93 | 454 | 1 744 | 1 318 | 224 | 438 | 32 | 1 917 |
| Application handler | 798 | 93 | 579 | 1 921 | 1 642 | 255 | 570 | 32 | 2 322 |

`time` is the front functions, the two conversions, and the manual clock,
each its own object; the conversions are most of it, because they divide
and multiply 64-bit values without the compiler's helpers. `log` is the
logger, the printf call site, the writer sink, and the memory sink, each
its own object, so a program that logs to one kind of sink links the
other's code only if it uses it. Measured again on 2026-11-17, when the
writer sink came to borrow its line buffer: 2 bytes larger with
assertions off, 6 with the trap and 8 with the handler (887, 1 031 and
1 295 before), for the check of the borrowed pointer and a newline that
is now appended before it is cut to fit. The other libraries measured the
same.

Measured again later on 2026-11-17, after the clock-taking conversions
and a sink that lends its buffer
([ADR 0022](adr/0022-log-sink-lends-the-buffer.md)). `time` grew by 68
bytes in every configuration (296, 370 and 502 before), for the two
conversions in a clock's own unit. `log` grew by 292, 324 and 348 bytes
(889, 1 037 and 1 303 before): each sink has two operations, the writer sink keeps
where its line's start ends, the memory sink lends its storage through
an `el_buf`, and the begin, append and end steps are shared by
`el_log_write`, `el_log_sink_write` and `el_log_vprintf`. The other
libraries measured the same.

M5 (2026-11-17): `ring` is new, the ring and its two streams, each its
own object; the streams are 98 bytes of it with the trap. `log` grew by
466, 556 and 671 bytes (1 181, 1 361 and 1 651 before) for the deferred
sink, which is in `elemental::log` with `LOG_DEFERRED` on, the default
where `RING` is, and is linked only by a program that uses it. The other
libraries measured the same.

`crc` is measured with its default 16-entry tables; with `CRC_TABLE_8BIT`
it is 1 624 bytes (trap), and with `CRC_ZEPHYR` 20 bytes plus 164 of
Zephyr's own code and table. `CRC_GCC_BUILTIN` cannot be built by this
GCC 14.3.0; on x86-64, GCC 16.2.1 at `-Os` makes it 1 925 bytes against
312 for the 16-entry tables. `bytes` grew by 370 bytes with byte swap and
bit counting, portable code on this core except CLZ, and by 722 more with
rotate, bit reversal, bit fields, and alignment (1 318 bytes in all, trap). `fmt` is measured with `FMT_PRINTF_BUILTIN`: the three
conversions, the printf parser, and the buffered-writer entry points, each
its own object, so a program links only what it calls (M3's `fmt`, with
`el_fmt` and the hex dump, was 1 705, trap). `FMT_PRINTF_LIBC` was not
measured in a Zephyr build here; the proposal's prototype measurements are
in [its Code size section](proposals/io-buffers-fmt.md#code-size). Text includes read-only data such as the CRC tables.

The handler configuration pays for a call, a file name, and a line number
per check. These are measurements of one compiler at one optimization
level, recorded as the evidence for the `ASSERT` option's help text.
Measure with `.agents/skills/preflight/scripts/code_size.sh`, which builds
the application without tests in each configuration.

## What is verified

On one x86-64 Linux machine, 2026-11-17. "Passed" means the command was run
and exited successfully. The five host presets were run again on
2026-11-20, after the literal-format checks were added, with the same
compiler versions and LeakSanitizer on; the Zephyr columns are from the
earlier runs.

| Check | GCC 16.2.1 | Clang 23.1.5 | Clang + ASan/UBSan | Zephyr `native_sim` | Zephyr `mps2/an385` (QEMU) | Zephyr `nucleo_g0b1re_renode` (Renode, modified model) | Zephyr `qemu_cortex_m0` |
|---|---|---|---|---|---|---|---|
| Build with strict warnings as errors | passed | passed | passed | passed (libraries) | passed (libraries) | passed (libraries) | passed (libraries) |
| Unit and helper tests | passed | passed | passed | passed | passed | passed (per module) | not run |
| Contract violations stop the program (`io.violations`, `io.violations.grow`, `ring.violations`, `alloc.violations`, `fmt.violations`, `bytes.violations`, `crc.violations`, `time.violations`, `log.violations`, `log.violations.deferred`) | passed, trap | passed, trap | passed, application handler | n/a, no death tests | n/a, no death tests | n/a, no death tests | not run |
| io contract suite: fixed `el_buf` and the memory reader | passed | passed | passed | passed | passed | passed (per module) | not run |
| io contract suite: POSIX, blocking and non-blocking | passed | passed | passed | n/a | n/a | n/a | n/a |
| io contract suite: `ring`'s writer and reader | passed | passed | passed | passed | passed | passed (per module) | not run |
| ring: unit tests, one context driving both sides | passed | passed | passed | passed | passed | passed (per module) | not run |
| ring and the deferred sink: a thread on each side (`ring.stress`, `log.deferred.stress`) | passed | passed | passed; and under ThreadSanitizer, `tsan` preset: passed | n/a, host only | n/a, host only | n/a, host only | n/a |
| log: the deferred sink, one context driving both sides (`log.deferred`) | passed | passed | passed | passed | passed | passed (per module) | not run |
| A receive interrupt fills a ring and logs through the deferred sink; a thread reads through an `el_reader` and drains (`example.uart_ring`) | n/a | n/a | n/a | n/a | passed, `elemental.tests.ring.uart`: QEMU joins UART1 to UART2 | not run | n/a |
| io contract suite: `el_grow_buf` (`IO_ALLOC`); the buffered writer, fixed, capacity 0 and grown | passed | passed | passed | passed | passed | passed (per module) | not run |
| Allocator contract suite: bump and stack arenas | passed | passed | passed | passed | passed | passed (per module) | not run |
| Allocator contract suite: libc heap | passed | passed | passed | n/a | n/a | n/a | n/a |
| Allocator contract suite: size classes example | passed | passed | passed | passed | passed | passed (per module) | not run |
| libc heap platform tests | passed | passed | passed | n/a | n/a | n/a | n/a |
| POSIX platform tests | passed | passed | passed | n/a | n/a | n/a | n/a |
| Public headers as C11 and C++11 | passed | passed | passed | not run | not run | not run | not run |
| status: `EL_TRY`, `EL_TRY_GOTO`, `EL_TRY_NEG`, `EL_ENSURE`, and `el_status_ignore()` in C functions, for Elemental's codes and an application's; a negative status through `io` unchanged | passed | passed | passed | passed | passed | passed (per module) | not run |
| status: a dropped status is rejected by the compiler, as C11, C++17, and C23 (`status.nodiscard_check.*`); every function declared to return `el_status` is `EL_NODISCARD` (`headers.nodiscard`) | passed | passed | passed | n/a, host only | n/a, host only | n/a, host only | n/a |
| C++ interop, run | passed | passed | passed | not run | not run | not run | not run |
| fmt: conversions and printf subset into a buffer and a buffered writer, literal expectations; proposal Examples 1–3 and 2b (Example 2 against a scripted stalling UART) | passed | passed | passed | passed | passed | passed (per module) | not run |
| fmt: printf subset against glibc `snprintf`, every capacity and through a buffered writer stalling after every k; decimal against division | passed | passed | passed | n/a, host only | n/a, host only | n/a, host only | n/a |
| fmt: format attribute accepts correct calls, rejects a mismatch | passed | passed | passed | n/a | n/a | n/a | n/a |
| fmt and log: portable literal formats (`fmt.portable_check.*`, `integration.installed`), including a CMake build rejected at the call site | passed | passed | passed | not run | not run | not run | not run |
| fmt: checked portable literals against the builtin formatter and `snprintf` | passed | passed | n/a, libc option | n/a, host only | n/a, host only | n/a, host only | n/a |
| bytes: byte-order vectors at every alignment | passed | passed | passed | passed | passed | passed (per module) | not run |
| crc: catalogue check values and generated vectors, configured tables | passed | passed | passed | passed | passed | passed (per module) | not run |
| crc: 256-entry tables (`variant_defconfig`) | not this configuration | not this configuration | passed | not this configuration | passed, `elemental.tests.variant` | not this configuration | n/a |
| fmt: every fmt test with `FMT_PRINTF_LIBC` (`variant_defconfig`), `fmt.libc` against glibc's and picolibc's `snprintf` (floats skipped at picolibc's integer level) | not this configuration | not this configuration | passed, oracle included | not this configuration | passed, `elemental.tests.variant` | passed, `elemental.tests.fmt.variant` | n/a |
| crc, fmt: every option the compiler can select (`integration.choices`) | passed: 4-bit, 8-bit, builtin; both printf options | passed: 4-bit, 8-bit; both printf options | passed: 4-bit, 8-bit; both printf options | n/a | n/a | n/a | n/a |
| crc: Zephyr's `sys/crc.h` (`CRC_ZEPHYR`) | n/a | n/a | n/a | not run | passed, `elemental.tests.crc_zephyr` | not run | n/a |
| bytes: byte swap, bit counting, rotate, bit fields, and alignment, builtins and portable code | passed | passed | passed | passed (portable: 32-bit x86) | passed (CLZ builtin, portable popcount) | passed (per module) | not run |
| time: conversions at their edges and against division, and in a clock's own unit; the front functions; the manual clock | passed | passed | passed | passed | passed | passed (per module) | not run |
| Clock contract suite: manual clock; stepping clock example | passed | passed | passed | passed | passed | passed (per module) | not run |
| Clock contract suite and platform tests: POSIX (`time_posix`) | passed | passed | passed | n/a | n/a | n/a | n/a |
| Clock contract suite and platform tests: the Zephyr kernel's uptime (`time_zephyr`) | n/a | n/a | n/a | passed, 100 ticks per second | passed, 100 ticks per second | passed (per module), 10 000 ticks per second | not run |
| log: the logger and the printf call site, formatting into the buffer the sink lends; a file compiled with `EL_LOG_CUTOFF`; the writer sink's line, over fixed and grown buffers, and the memory sink's storage | passed | passed | passed | passed | passed | passed (per module) | not run |
| Log sink contract suite: memory sink, writer sink over an `el_buf`, deferred sink, tee example | passed | passed | passed | passed | passed | passed (per module) | not run |
| log: two sinks in one program, one of them a POSIX descriptor (`log.posix`); `el_cat` logging to standard error (`integration.installed`) | passed | passed | passed | n/a | n/a | n/a | n/a |
| log: `EL_LOG_CUTOFF` removes the call and its format string from the object, C11 and C++17, `-O0` and `-Os` (`log.cutoff_check.*`) | passed | passed | passed | n/a, host only | n/a, host only | n/a, host only | n/a |
| Rust interop, run (io and allocator sketches; the io sketch also calls fmt, crc, bytes) | passed | passed | skipped | not run | not run | not run | not run |
| Python sketch, compile only | passed | passed | passed | not run | not run | not run | not run |
| Integration (9 cases, including `module_subsets` and `choices`) | passed | passed | passed | n/a | n/a | n/a | n/a |
| Every test with assertions compiled out (`assert_off_defconfig`) | passed, `assert-off` preset | n/a | n/a | not run | passed, `elemental.tests.assert_off` | not run | n/a |
| Symbol check | n/a | n/a | n/a | n/a | passed; `memcpy`, `memmove`; the kernel's uptime for `time_zephyr`; `snprintf` allowed only with `FMT_PRINTF_LIBC`; no `__atomic_*` | passed; no `__atomic_*` | passed; no `__atomic_*` |

The hosted presets run 665 GoogleTest tests, of which 35 are reported as
skipped because a fixture lacks an optional capability
([io](../modules/io/README.md#the-contract-suite),
[alloc](../modules/alloc/README.md#the-contract-suite),
[time](../modules/time/README.md#the-contract-suite),
[log](../modules/log/README.md#the-contract-suite)), plus the interop
and integration cases, two format-attribute, six literal-format, six must-use
and twenty-four log cut-off checks, and the `EL_NODISCARD` declaration check:
716 CTest tests in all. `sanitize` runs 720: its `variant_defconfig` adds
`fmt.libc` and the libc option's violations and omits the builtin format
oracle. The sanitizer build leaves out the two Rust sketches because a
plain `rustc` link has no sanitizer runtime. `assert-off` runs 634: 716
less the 82 tests of the `*.violations` binaries, which are not built
without assertions. These presets each report 35 skips. `tsan` runs 68
with two skips: the tests of `ring` and of the deferred sink, without the
death tests. M5 added 62 GoogleTest tests: `ring`'s 40, the deferred sink's 38,
less the ring pipe example's 16.

`integration.module_subsets` builds each module and backend alone, except
`time_zephyr`, which no host can build: it is left out because a probed
fact (`PLATFORM_ZEPHYR`) hides it, the only reason the case accepts, and
its own platform's tests cover it.

The Zephyr tests are 489 GoogleTest tests: the hosted ones without the
POSIX and C library backends', the death tests, and the host-only
`fmt.oracle`, `bytes.portable`, `log.posix`, `ring.stress` and
`log.deferred.stress`, and with the nine of `time_zephyr`, which run only
there; 497 in the `variant` scenario, with `fmt.libc`; and the three of
`example.uart_ring` in `elemental.tests.ring.uart`. Twister (2026-11-17)
reports 32 test configurations passed and `elemental.symbols.armv6m`
built, with no warnings:

- `elemental.tests.<module>` on `native_sim`, `mps2/an385` and
  `nucleo_g0b1re_renode`, passed and skipped by GoogleTest's count, the
  same on each board: `status` 17 and 0, `alloc` 55 and 3, `io` 114 and
  15, `ring` 24 and 2, `fmt` 32 and 0, `bytes` 38 and 0, `crc` 8 and 0,
  `time` 40 and 2, `log` 137 and 2 (489 together);
- `ring`'s image with the UART example on `mps2/an385`
  (`elemental.tests.ring.uart`): 27 passed and 2 skipped;
- the full image on `mps2/an385`: `assert_off` and `crc_zephyr` 465 passed
  and 24 skipped, `variant` 472 and 25 (`fmt.libc`'s float test skipped at
  picolibc's integer level);
- the `fmt` image with `variant_defconfig` on `nucleo_g0b1re_renode`
  (`elemental.tests.fmt.variant`): 39 passed and 1 skipped, the same float
  test. 292 696 B of flash, 17 616 B of static RAM, 16 996 B of peak heap.

Twister adds one case per image for the image itself. Zephyr v4.5.0, SDK
1.0.1, GCC 14.3.0 for Arm, the host's GCC 16.2.1 for `native_sim`, QEMU
from the host, Renode 1.17.0.

The same Twister matrix passed on 2026-11-21 in an Ubuntu 24.04 container
using the CI workflow's pinned SDK and Renode.

`fmt`'s literal-expectation tests (`fmt.unit`) pass unchanged on
`mps2/an385` and `nucleo_g0b1re_renode`, so the portable subset's output is
byte-identical on the host, an emulated ARMv7-M and an emulated ARMv6-M
with `FMT_PRINTF_BUILTIN`, and on the host and the ARMv7-M with
`FMT_PRINTF_LIBC` (`sanitize`, `variant`). No scenario runs
`FMT_PRINTF_LIBC` on the Cortex-M0+. The ARMv6-M evidence is an emulator
run on a modified model, not hardware.

Additionally, each run once by hand and not part of the automated suite:

- The whole test suite with CMake 3.22.6, the declared minimum, using the
  Unix Makefiles generator and no preset.
- The whole test suite with Python 3.9.23, the declared minimum, first on
  `PATH` and passed to the `gcc` preset as `Python3_EXECUTABLE`: 90 of 90.
- A fresh configure, build, and the non-integration tests inside a network
  namespace with no network, after provisioning.
- `menuconfig` started through its build target under a pseudo-terminal,
  showed the Elemental menu, and quit cleanly; `savedefconfig` wrote back
  the one-line defconfig the `sanitize` preset was seeded from. (Done
  before the move to GoogleTest; that change did not touch configuration.)
- Six deliberately broken variants of the ring pipe against the contract
  suite ([modules/io/README.md](../modules/io/README.md#the-contract-suite)).
- Fifteen deliberately broken variants for M2, with
  `.agents/skills/writing-tests/scripts/mutant.sh`, all caught: the size
  classes example ignoring alignment, overlapping blocks, and never reusing
  freed blocks; a bump arena exceeding its bound and changing state on a
  failed request; a stack arena that never rolls back; the front function
  passing size 0 on, and a weakened overflow check; the libc backend using
  `malloc` for large alignments; the checking allocator ignoring a wrong
  size; the grow writer freeing its length instead of its capacity, skipping
  the exact-size retry, and reporting end of stream instead of no space; the
  bump arena dropping its ownership check; and `io` linking `alloc`, caught
  by `integration.subdirectory` (run by hand, since the script leaves out
  integration tests).
- Sixteen deliberately broken variants for byte swap, bit counting, and
  the CRC builtin option (2026-11-12): fifteen caught, one equivalent (a
  `swap32` mask bit that the shift already clears). The two CRC builtin
  mutants were caught by `integration.choices`, run by hand on the mutated
  copy, because `mutant.sh` leaves out integration tests.
- Twelve deliberately broken variants for rotate, bit fields, and alignment
  (2026-11-13), all caught: a mask one bit short, a field not masked before
  it is set, `align_up` off by one and treating only 0 as aligned, the
  field range check written as `shift + width` (which wraps), a 64-bit
  rotate right with the wrong count and one that adjusts by 31 instead of
  32, a bit reversal without its byte swap, the portable 64-bit shift
  shifting by the full width, `trailing_ones` counting zeros, a
  `count_zeros` off by one, and the alignment and mask assertions removed.
- Twenty-seven deliberately broken variants for M3, all caught (run on
  this branch before it was rebased onto M2; the M3 sources did not change
  in the rebase): the
  shift-and-add divide by 10 without its correction step (32- and 64-bit)
  and without its 64-bit shift; each of the four sticky-truncation checks;
  a number allowed to overrun by its padding or by one byte; a missing
  terminator; the printf parser ignoring the `0` flag, a `%s` precision, or
  `hh`, and accepting a precision on `%d`; a hex dump without separators;
  wrong entries and a missing final inversion in both CRC table sizes;
  swapped halves and byte orders in `bytes`, and a store one byte too wide;
  and the null checks removed from `crc`, `bytes`, and `fmt`. The last three
  first survived, because a null access crashes on the host anyway; the
  death tests of these modules now accept only a death by the check (a
  trap or an abort), not `SIGSEGV`.
- Fifteen deliberately broken variants for M3b's status handling
  (2026-11-15), fourteen caught: `EL_TRY` and `EL_TRY_GOTO` letting a
  negative status through; `EL_TRY` evaluating its expression twice;
  `EL_TRY_GOTO` not assigning on success; `EL_TRY_NEG` returning on zero,
  and holding its value in an `el_status` (a wide count then reads as
  negative); `EL_ENSURE` inverted, and evaluating its status when the
  condition holds; `EL_NODISCARD` emptied in each of its three branches
  (C11, C++17, C23), each caught by that language's compile check; a
  declaration losing `EL_NODISCARD`, caught by `headers.nodiscard`; and
  `el_read` and the buffered writer turning an application's negative
  status into `EL_ERR_IO`. Not caught: a `(void)` cast in place of
  `el_status_ignore()` in a C source. GCC warns about it, and only a build
  with warnings as errors (every preset) fails; `mutant.sh` builds without
  them.
- Eighteen deliberately broken variants for M4's `time` (2026-11-16), all
  caught in the end: a manual clock that goes back and one that does not
  start at 0; a stepping clock whose frequency changes, that goes back,
  and that wraps at the top; a POSIX clock reading `CLOCK_REALTIME`,
  stating microseconds, and dropping the nanoseconds; `el_ticks_from_ms`
  without its rounding; the divide without its carry, a multiply missing a
  partial product, and microseconds scaled by the wrong power of ten; and
  each assertion removed. Two removed null checks first survived, because
  a null access crashes on the host anyway: `time.violations` and
  `log.violations` now accept only a death by the check, as the M3 modules'
  do.
- Sixty-three deliberately broken variants for M4's `log` (2026-11-16).
  Fifty-six were caught as written, among them: a logger letting its own minimum's level
  be filtered, reading the clock's frequency per record, and stamping
  nothing; `el_log_printf` formatting below the logger's level and logging
  its whole buffer; the writer sink's padding, level letters, tag, lost
  count, and newline, a writer's failure swallowed, and a line not started
  afresh; a memory sink that keeps the caller's pointer, reorders, loses a
  field, overruns its storage, reports `EL_OK` for a record it dropped, and
  miscounts; a tee that starves its second sink or returns the wrong
  status; the cut-off comparing against the wrong level (caught by
  `log.cutoff_check.*` and by the file compiled with a cut-off); and each
  assertion removed. The five that survived were all code that duplicated
  a check made elsewhere, and were removed rather than tested: a level
  check in `el_log_write` and in `el_log_vprintf` that `el_log_sink_write`
  always makes, a status the newline step already sets when a message is
  cut, a storage check `el_buf_init` makes, and a status for a sink with no
  storage that its prefix already reports. Two more first survived for
  want of a test, which was added: a null message through a sink that does
  not read it, and a tee whose two sinks fail differently.
- Five deliberately broken variants of the writer sink once it borrowed
  its line buffer (2026-11-17), all caught: the message cut to the room
  before the buffer is asked to grow, the old newline rule (no room left
  means a line too long), a newline appended without dropping the last
  byte, no check of a null line buffer, and a message cut for want of an
  allocator reported as `EL_OK`. The last is caught only by
  `log.sinks.grown`: over fixed storage a cut message leaves no room for
  the newline, which reports the cut anyway.
- Sixteen deliberately broken variants once a sink lent the buffer for a
  record's message ([ADR 0022](adr/0022-log-sink-lends-the-buffer.md),
  2026-11-17), fifteen caught by a test: `el_log_vprintf` reporting a cut
  message as whole, and formatting when the sink did not begin the
  record; a message taken from the start of the lent buffer instead of
  after the sink's own bytes; `el_log_sink_write` without its cut at a
  byte, and reporting a cut message as whole; a memory sink that keeps a
  cut message, and one that does not count a record it could not begin;
  a writer sink that ignores a cut message, keeps a message after a cut
  start of the line, and does not empty its line when it begins; no check
  for a missing `end` operation; a tee that reports a cut message as
  taken; and the two clock-taking conversions using a fixed frequency.
  The one that survived, no check that a sink lent a buffer, duplicated
  `el_buf_len()`'s own null check, which still stops such a sink, and was
  removed rather than tested.
- Twenty-one deliberately broken variants for M5's `ring` and deferred
  sink (2026-11-17), all caught in the end. Eighteen with
  `mutant.sh`: a ring that keeps one byte free, whose indices wrap at the
  capacity instead of twice it, whose room and write ignore the close,
  whose writer stalls after the close, whose reader ends at the close
  before the last bytes, and whose full writer reports `EL_ERR_NO_SPACE`;
  a deferred sink that publishes a record in two writes, does not count a
  lost record, keeps a cut message, writes a record the ring has no room
  for, drains while the ring keeps filling, leaves a refused record or the
  rest of a cut message in the ring, gives a cut message as whole, and
  drops the record's frequency. Two first survived. A reader that looks at
  the close after finding the ring empty, instead of before, is wrong only
  when the other thread writes and closes between its two loads; the
  stress tests close four times a run and missed it, and
  `RingStressClose`, which closes 20 000 times, was added and caught it in
  three runs of three. A begin that does not check the ring's room loses
  the same records at end, after formatting them; a test of begin itself
  was added. The indices wrapping at the capacity also first hung one
  stress test, whose reader got bytes forever; the readers now stop at
  more bytes than were written. Three more against the two-thread tests
  under ThreadSanitizer, built as the `tsan` preset is: the ring's words
  plain `size_t`, atomic with every order relaxed, and only the tail
  stored relaxed. Each passed GoogleTest's own checks, the bytes arriving
  whole and in order, and each was reported as a data race (10, 6 and 3
  reports in `ring.stress`; 5, 2 and 2 in `log.deferred.stress`): on
  x86-64 the sanitizer is the part of these tests that fails.
- Eight deliberately broken variants of M3's `FMT_LIBC` (since removed), built with
  `variant_defconfig`, all caught: swapped and doubled hex cases, a scratch
  buffer too small for the digits, a dropped digit, the digits always in
  decimal, a number allowed to overrun by a byte, and a number copied in
  part when it does not fit. `%lu` for `%llu` first survived, since `long`
  is 64 bits on x86-64; the format literals are now in the `snprintf` call,
  so `-Wformat` rejects it on every target.

Not verified:

- A passing GitHub Actions run of the current workflow. The November 20,
  2026 run built the host presets, but the CMake policy error stopped the
  log cut-off checks and Zephyr stopped at SDK setup before Twister.
- Any compiler other than those above, including MSVC and vendor compilers.
  The MSVC branches in the build files and in `assert.h` are untested.
- Any host operating system other than Linux. None is supported.
- **Any real hardware.** The Cortex-M evidence is an emulated Cortex-M3
  (QEMU), an emulated Cortex-M0+ on a modified Renode model of the
  STM32G0B1, and a symbol check for a Cortex-M0.
- The STM32G0B1's peripherals beyond what the tests touch: the Renode
  model is an STM32G071's, enlarged, and was not checked against the G0B1
  reference manual.
- Contract violations on a target: death tests run on the host only.
- **A clock on a real board.** `time_zephyr` was run under two emulators
  and on `native_sim`, at 100 and 10 000 kernel ticks per second; an
  emulator's timer shows that the port reads the kernel's uptime in the
  right unit, not that a board keeps time. It was read from an interrupt
  handler on `mps2/an385` under QEMU only.
- **What the ring's evidence does not show.** The UART run is an emulator
  run on one core: a real exception entry preempts the reading thread and
  the bytes and records come through, but it does not show timing, an
  overrun by a handler that is late, or every point at which a handler
  could preempt (QEMU runs translated blocks of instructions; at which
  boundaries it takes an interrupt was not examined). Nothing shows memory
  ordering between two cores of an Arm processor: the two-thread tests run
  on x86-64, which orders more strongly, and ThreadSanitizer checks C's
  rules, not a processor. No interrupt was taken on the Cortex-M0+ board,
  where the ring's tests run in one context. No hardware.
- The log cut-off with any
  compiler but the host's GCC and Clang: `log.cutoff_check.*` is host
  only, so that the Zephyr SDK's GCC 14.3.0 drops a cut call was not
  checked on an object, only that such calls do not run
  (`ElLogCutoff.*`, on every board).
- The context prototype of M3b
  ([status handling proposal](proposals/status-handling.md#context)) and
  the `time` and `log` prototypes' measurements were made outside the
  repository; their sources are not kept.
- Anything executed in the Python sketch.
