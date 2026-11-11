# io

Byte stream reader and writer interfaces, helpers built on them, and readers
and writers over caller-provided memory.

| | |
|---|---|
| Stability | Experimental ([policy](../../docs/process/api-stability.md)) |
| Targets | `elemental::io`, `elemental::io_contract`, `elemental::io_posix` |
| Depends on | `status`. Runtime: `memcpy`. |
| Allocates | No |
| Kconfig | `IO`, `IO_POSIX` |

## What is here

| Piece | Kind | Header |
|---|---|---|
| `el_reader`, `el_writer`, `el_read`, `el_write`, `el_read_exact`, `el_write_all` | Interface contract | [`elemental/io.h`](include/elemental/io.h) |
| `el_mem_reader`, `el_mem_writer` | Portable implementation | [`elemental/io_mem.h`](include/elemental/io_mem.h) |
| The io contract suite | GoogleTest tests, shipped as source | [`elemental/io_contract.hpp`](contract/include/elemental/io_contract.hpp), fixture in [`elemental/io_contract.h`](contract/include/elemental/io_contract.h) |
| `el_posix_fd` | Platform backend | [`elemental/io_posix.h`](backends/posix/include/elemental/io_posix.h), [PORT.md](backends/posix/PORT.md) |

Start with `io.h`: the contract fits on one screen and is stated once, there.
The reasoning is in
[architecture.md](../../docs/architecture.md#63-byte-io--implemented-with-known-gaps).

## Using it

[examples/consume_subdirectory/main.c](../../examples/consume_subdirectory/main.c)
frames records into a fixed buffer and parses them back.
[examples/consume_installed/main.c](../../examples/consume_installed/main.c)
copies standard input to standard output and handles a stream that ends,
stalls, or fails.

## Implementing a backend

1. Define your state struct and one operation per interface
   (`el_reader_ops.read`, `el_writer_ops.write`). The "called with" and
   "must" lists in `io.h` are the whole contract. Your operation never has to
   validate arguments.
2. Put the operations in a `static const` table and write a function that
   returns a handle pairing it with your state.
3. Write a fixture, in C, and instantiate the contract suite with it. That
   is one line of C++.
4. Write tests for what only your backend promises.
5. If it lives in this repository, add a `PORT.md`
   ([porting.md](../../docs/process/porting.md)).

[examples/backend_ring](../../examples/backend_ring) is a complete backend
done this way, in under 100 lines, with its fixture and both kinds of test.
[examples/consume_installed](../../examples/consume_installed) builds the
same backend against an installed Elemental, as a backend kept outside this
repository would be.

## The contract suite

`ElIoContract` is a GoogleTest suite that checks what the interface
promises, against any implementation described by a fixture:

```cpp
#include <elemental/io_contract.hpp>

#include "my_fixture.h"

INSTANTIATE_TEST_SUITE_P(MyBackend, ElIoContract, testing::Values(&my_fixture));
```

Link `elemental::io_contract` and a GoogleTest `main()`. Each promise is one
test, named `MyBackend/ElIoContract.<Promise>/0`.

The suite is distributed as source and compiled into the test that links it,
with that build's compiler and GoogleTest. Elemental does not ship
GoogleTest; inside this repository `elemental_add_test()` supplies it, and an
outside project brings its own
([example](../../examples/consume_installed/CMakeLists.txt)).

It needs C++17 and a hosted C++ library. On a microcontroller it runs
through Zephyr: the Zephyr test application runs it against the memory
streams and the ring pipe on `native_sim` and on an emulated Cortex-M3
([build.md](../../docs/build.md#tests-on-zephyr)).

A fixture is a C struct with four required functions and three optional
capabilities. An
implementation that cannot be put into a state (a memory stream cannot
stall) leaves that capability null, and the suite reports the test as
skipped rather than passed.

| Implementation | Tests run | Skipped, and why |
|---|---|---|
| Memory streams | 9 | 2: cannot stall |
| POSIX, blocking pipe | 8 | 3: cannot stall; a pipe cannot end after an exact count |
| POSIX, non-blocking pipe | 10 | 1: a pipe cannot end after an exact count |
| Ring pipe example | 10 | 1: stalls when full instead of ending |
| Grow writer (`io_alloc`) | 8 | 3: cannot stall; reports `EL_ERR_NO_SPACE`, not permanent, instead of ending |

What the suite does not cover is deliberate: behavior that belongs to one
backend. Deliberately broken variants of the ring pipe were used to check
the suite itself. It caught dropped bytes, reordering, a missing
wrap-around, and a stall reported as end of stream. It did not catch
"accepts writes after its write side is closed", because closing a write
side is not part of the interface; the ring pipe's own test covers that.

## Tests

| Test | What it checks |
|---|---|
| `io.contract.mem.*` | The contract suite against the memory streams. |
| `io.helpers.*` | `el_read_exact` and `el_write_all` against a scripted stream: every interleaving of progress, would-block, and failure, and the bound on attempts. |
| `io.violations.*` | Each documented contract violation stops the program, for callers and for implementations. Death tests, so they hold in every assertion configuration that has assertions on. |
| `io_posix.contract.*`, `io_posix.platform.*` | See [PORT.md](backends/posix/PORT.md). |

Each `TEST` is one CTest test: `ctest --preset gcc -R io.helpers` runs a
group, and a test binary takes GoogleTest's own flags, such as
`--gtest_filter`.

## Known gaps

No flush, buffered reading, peek, vectored I/O, seek, or close. Each waits
for a consumer that needs it; see the roadmap. Nothing here is designed for
operations that outlive the call that started them.
