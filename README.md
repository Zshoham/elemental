# Elemental

Small, modular, implementation-independent C interfaces for native
applications, embedded Linux, and microcontrollers.

Each module is a written contract, a portable implementation where one can
exist, and a GoogleTest suite that states the contract in executable form. Modules
are independent: use one without the others, without a runtime, and without
a global heap.

Elemental offers better interfaces for jobs libc and platform libraries do.
It is not a C library, a runtime, or a framework
([what that means](docs/architecture.md#1-project-boundaries)).

## Status

Version 0.1, first milestone. Everything is **experimental**.

| Module | What | Freestanding |
|---|---|---|
| [`status`](modules/status/README.md) | Status codes and contract assertions | Yes |
| [`io`](modules/io/README.md) | Byte stream reader and writer interfaces; streams over caller-provided memory; a contract test suite for backends | Yes, needs `memcpy` |
| [`io_posix`](modules/io/backends/posix/PORT.md) | Reader and writer over a file descriptor | No |
| [`alloc`](modules/alloc/README.md) | Allocator interface; bump and stack arenas over caller-provided storage; a contract test suite and a checking allocator | Yes |
| [`alloc_libc`](modules/alloc/backends/libc/PORT.md) | Allocator over the C library heap | No |
| [`io_alloc`](modules/io_alloc/README.md) | A writer that grows through an allocator | Yes, needs `memcpy` |
| [`fmt`](modules/fmt/README.md) | Text formatting into caller memory: integers, strings, hex dumps, a printf subset | Yes, needs `memcpy` |
| [`bytes`](modules/bytes/README.md) | Little- and big-endian load and store at any alignment; byte swap; C23-style bit counting | Yes |
| [`crc`](modules/crc/README.md) | CRC-32/ISO-HDLC and CRC-16/IBM-3740, incremental | Yes |

Logging, async, and IPC are designs or open questions, not code.
See the [roadmap](docs/roadmap.md).

Licensed under the [Apache License 2.0](LICENSE).

## Quick start

Needs CMake ≥ 3.22, a C11 compiler, Python ≥ 3.9, git, and Ninja. The tests
also need a C++17 compiler; the libraries do not.

```sh
cmake -P tools/provision.cmake     # once: fetches the pinned Kconfig tool and GoogleTest
cmake --preset gcc                 # or: clang, sanitize, assert-off
cmake --build --preset gcc
ctest --preset gcc
```

The host is Linux. Microcontroller targets are Zephyr targets: the same
tests run as a Zephyr application under Twister
([docs/build.md](docs/build.md#tests-on-zephyr)).

The first command is the only one that uses the network. Without the
presets: `cmake -B build && cmake --build build && ctest --test-dir build`.

## A taste

```c
#include <elemental/io.h>
#include <elemental/io_mem.h>

static uint8_t frame[32];                 /* storage is yours */
el_mem_writer writer;
el_mem_writer_init(&writer, frame, sizeof(frame));

size_t n;
el_status status = el_write_all(el_mem_writer_as_writer(&writer), "hello", 5, &n);
if (status == EL_ERR_END_OF_STREAM) {
    /* the buffer is full; n bytes fit */
}
```

Code written against `el_writer` works unchanged with memory, a file
descriptor, or a backend of your own.

## Using it in your project

| | You need | Start at |
|---|---|---|
| An installed package: `find_package(Elemental)` | CMake and a C compiler. No Kconfig, no Python, no GoogleTest. | [examples/consume_installed](examples/consume_installed) |
| A subproject: `add_subdirectory()` | Also Python and the provisioned Kconfig tool | [examples/consume_subdirectory](examples/consume_subdirectory) |
| Copied sources, no CMake | A C compiler and a four-line config header | [docs/build.md](docs/build.md#source-drop) |

```cmake
find_package(Elemental 0.1 REQUIRED COMPONENTS io)
target_link_libraries(app PRIVATE elemental::io)
```

## Configuration

Compile-time configuration uses Kconfig. Defaults work; change them with a
defconfig or interactively:

```sh
cmake -B build -DELEMENTAL_DEFCONFIG=configs/variant_defconfig
cmake --build build --target menuconfig
```

Configuration decides which modules are built and how they behave. It never
changes a public type or signature, so a consumer's code does not depend on
it. Details in [docs/build.md](docs/build.md#configuration).

## Documentation

| If you want to | Read |
|---|---|
| Understand the design and its tradeoffs | [docs/architecture.md](docs/architecture.md) |
| Write or review an interface | [docs/conventions.md](docs/conventions.md) |
| Build, configure, install, test on Zephyr; see what is verified | [docs/build.md](docs/build.md) |
| Implement a backend | [modules/io/README.md](modules/io/README.md#implementing-a-backend), [modules/alloc/README.md](modules/alloc/README.md#implementing-an-allocator), [docs/process/porting.md](docs/process/porting.md) |
| Propose a module | [docs/process/new-module.md](docs/process/new-module.md) |
| Know what may change | [docs/process/api-stability.md](docs/process/api-stability.md) |
| See why a decision was made | [docs/adr/](docs/adr/README.md) |
| See what is next | [docs/roadmap.md](docs/roadmap.md) |

## Layout

```
modules/<name>/   one module: Kconfig, build file, headers, sources, tests
examples/         a backend written from scratch, and two consumer projects
tests/            header, interop, and integration checks; the Zephyr test application
cmake/  tools/    build support
configs/          defconfigs
docs/             design, conventions, process, decisions
```
