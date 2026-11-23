# io

Byte stream reader and writer interfaces, helpers built on them, a reader
over caller-provided memory, and `el_buf`, the memory buffer that a stream
can end in.

| | |
|---|---|
| Stability | Experimental ([policy](../../docs/process/api-stability.md)) |
| Targets | `elemental::io`, `elemental::io_contract`, `elemental::io_posix`, `elemental::io_zephyr_uart` |
| Depends on | `status`; `alloc` when `IO_ALLOC` is on. Runtime: `memcpy`, `memmove`; `io_zephyr_uart` calls Zephyr's device and UART API. |
| Allocates | Only `el_grow_buf`, through the allocator it is given |
| Kconfig | `IO`, `IO_POSIX`, `IO_ALLOC`, `IO_ZEPHYR_UART` |

## What is here

| Piece | Kind | Header |
|---|---|---|
| `el_reader`, `el_writer`, `el_read`, `el_write`, `el_read_exact`, `el_write_all` | Interface contract | [`elemental/io.h`](include/elemental/io.h) |
| `el_mem_reader` | Portable implementation of `el_reader` | [`elemental/io_mem.h`](include/elemental/io_mem.h) |
| `el_buf`, and `el_buf_as_writer`, the memory endpoint | A buffer, and a portable implementation of `el_writer` | [`elemental/io_buf.h`](include/elemental/io_buf.h) |
| `el_grow_buf` (`IO_ALLOC`) | Buffer storage that grows, over an allocator | [`elemental/io_alloc.h`](include/elemental/io_alloc.h) |
| `el_buffered_writer` | A buffer in front of another writer: flush, reserve, pending, and an `el_writer` | [`elemental/io_buffered.h`](include/elemental/io_buffered.h) |
| The io contract suite | GoogleTest tests, shipped as source | [`elemental/io_contract.hpp`](contract/include/elemental/io_contract.hpp), fixture in [`elemental/io_contract.h`](contract/include/elemental/io_contract.h) |
| `el_posix_fd` | Platform backend | [`elemental/io_posix.h`](backends/posix/include/elemental/io_posix.h), [PORT.md](backends/posix/PORT.md) |
| `el_zephyr_uart_poll` (`IO_ZEPHYR_UART`) | Platform backend: a Zephyr UART as a polled writer | [`elemental/io_zephyr_uart.h`](backends/zephyr_uart/include/elemental/io_zephyr_uart.h), [PORT.md](backends/zephyr_uart/PORT.md) |

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

The two sides of [`ring`](../ring/README.md) are streams done this way:
[ring_io.c](../ring/src/ring_io.c) is the two operations and the two
handles, in under 60 lines, with its
[fixture](../ring/tests/fixture_ring.c) and both kinds of test.
[`ring_zephyr_uart`](../ring/backends/zephyr_uart/PORT.md) has a Zephyr
UART's receive interrupt fill such a ring, whose reader is the `el_reader`, and
[examples/consume_installed](../../examples/consume_installed) runs the
contract suite against an installed Elemental, as a backend kept outside
this repository would.

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
streams and the buffered writer on `native_sim`, an emulated
Cortex-M3, and an emulated Cortex-M0+, as io's own test image, and against
the ring in ring's
([build.md](../../docs/build.md#tests-on-zephyr)).

A fixture is a C struct in two halves, a reader and a writer, and at least
one of them: a write-only implementation leaves the reader half out
instead of borrowing a memory reader, and the suite reports each test of
the absent half as skipped, "no reader" or "no writer". Of the reader half,
`reader_then` says what the reader does once its content is read: ends
(`EL_IO_CONTRACT_THEN_ENDS`, the default) or stalls with
`EL_ERR_WOULD_BLOCK` (`EL_IO_CONTRACT_THEN_STALLS`), as a UART's receiver
does while it runs; the suite checks the one the fixture declares. `reset`
is optional. An implementation that cannot be put into a state (a memory
stream cannot stall) leaves that capability null, and the suite reports
the test as skipped rather than passed. `writer_full_after` describes a
writer that takes an exact count and then reports `EL_ERR_NO_SPACE`, not
permanently, as a full memory buffer does.

The suite has 13 tests. What each implementation runs:

| Implementation | Tests run | Skipped, and why |
|---|---|---|
| Fixed `el_buf` as a writer, and the memory reader | 9 | 4: cannot stall (reader, writer); does not end; its reader ends, so the stall after content is not checked |
| `el_grow_buf` as a writer (`IO_ALLOC`) | 4 | 9: no reader (6); cannot stall; does not end; cannot be made full after an exact count |
| POSIX, blocking pipe | 8 | 5: cannot stall (reader, writer); a pipe cannot end, or be full without stalling, after an exact count; its reader ends |
| POSIX, non-blocking pipe | 10 | 3: a pipe cannot end, or be full without stalling, after an exact count; its reader ends |
| Zephyr UART, both ends: `io_zephyr_uart`'s polled sender on UART1, and the reader of a ring that `ring_zephyr_uart` fills from UART2, under QEMU on `mps2/an385` (`ring_zephyr_uart.contract`, in `elemental.tests.ring.uart`) | 9 | 4: a running receiver has no end to check; a UART cannot end, or be full, after an exact count, and the polled writer cannot stall |
| [`ring`](../ring/README.md)'s writer and reader, closed after the content | 10 | 3: stalls when full instead of ending or reporting `EL_ERR_NO_SPACE`; its reader ends |
| `ring`'s reader, not closed after the content (`reader_then` stalls) | 4 | 9: no writer (7); a reader that stalls has no end to check; `reader_stalled` is `ring`'s other fixture's |
| Buffered writer, fixed 16 bytes, in front of a sink that stalls | 5 | 8: no reader (6); accepts what its sink has not, so cannot end or be full after an exact count |
| Buffered writer, fixed capacity 0 (pass-through), in front of a sink that stalls or ends | 6 | 7: no reader (6); cannot be full after an exact count |
| Buffered writer, grown over a 256-byte arena (`IO_ALLOC`); stalled is the sink full **and** the arena spent | 5 | 8: as for fixed storage |

What the suite does not cover is deliberate: behavior that belongs to one
backend. Deliberately broken variants of a ring pipe, the example that
[`ring`](../ring/README.md) has since replaced, were used to check the
suite itself. It caught dropped bytes, reordering, a missing wrap-around,
and a stall reported as end of stream. It did not catch "accepts writes
after its write side is closed", because closing a write side is not part
of the interface; ring's own tests cover that.

## Buffers (`io_buf`, `io_alloc`)

`el_buf` is storage plus a length: the bytes `[0, len)` of `cap`. It is not
a stream. Its owner appends whole or nothing (`el_buf_append`), writes in
place (`el_buf_reserve`, `el_buf_spare`, `el_buf_commit`), patches bytes
already written (`el_buf_data_mut`), and takes bytes back off the end
(`el_buf_truncate`). `el_buf_as_writer` makes it the end of a stream: the
memory endpoint, which takes what fits and then reports `EL_ERR_NO_SPACE`,
not permanent, so the same write succeeds once the owner makes room.

Storage is the caller's choice, and everything that takes an `el_buf *`
works with either:

```c
uint8_t storage[64];
el_buf line;
el_buf_init(&line, storage, sizeof storage);           /* fixed: no deinit */

el_grow_buf body;
el_buf *msg = el_grow_buf_init(&body, allocator);      /* grown */
el_status status = el_buf_append(msg, data, len);      /* EL_ERR_NO_SPACE: refused */
el_grow_buf_deinit(&body);
```

`el_grow_buf` (`IO_ALLOC`) grows to twice its capacity, or to what is needed
if that is more, at least 16 bytes. If the allocator refuses, it asks once
more for exactly what is needed. The allocator decides how far it grows; the
buffer has no limit of its own. Storage is allocated, resized and freed with
`el_layout_make(cap, 1)` ([ADR 0013](../../docs/adr/0013-allocation-layout.md)).
Each growth is `el_realloc()`
([ADR 0025](../../docs/adr/0025-an-allocation-changes-size-and-may-move.md)):
a buffer that is its arena's latest allocation grows in place and, grown to
n bytes, has used n bytes of the arena. Once something else is allocated
after it, growth moves it, and over a bump arena it has used up to about
2n.

`el_grow_buf` is the one part of io that needs `alloc`, so it is built into
`elemental::io` only when the Kconfig symbol `IO_ALLOC` is on (the default,
where `ALLOC` is on), and then `elemental::io` links `elemental::alloc`. With
`IO_ALLOC` off, io builds and links no allocator, which
`integration.subdirectory` checks. Even with it on, `io_buf.c` names no
allocator function, so a program that only uses fixed buffers links none.
Growth goes through the buffer's `grow` hook, which is private to io. Only
`el_grow_buf_init` installs it. It is not an extension point. Code outside
io must not set or call it.

`el_buf` and `el_grow_buf` replaced `el_mem_writer` and `el_grow_writer` in
milestone M3a ([proposal](../../docs/proposals/io-buffers-fmt.md)).

## The buffered writer (`io_buffered`)

`el_buffered_writer` borrows an `el_buf`, fixed or grown, and puts it in
front of an inner writer, such as a UART writer that takes what its FIFO
has room for and would-blocks when it is full:

```c
static uint8_t console_storage[256];
static el_buf console_buf;
static el_buffered_writer console;

el_buf_init(&console_buf, console_storage, sizeof console_storage);
el_buffered_writer_init(&console, uart_writer(), &console_buf);
/* el_write_all(el_buffered_writer_as_writer(&console), ...), or printf */
(void)el_buffered_writer_flush(&console); /* from the main loop */
```

A write that fits is buffered without calling the inner writer. One that
does not makes **one** attempt to pass pending bytes on; a failure other
than would-block from it is returned with nothing taken. With nothing
pending, a write of at least the capacity goes straight through. Then the
writer compacts, grows if the storage can, and buffers what fits; if
nothing fits, the stream refused, and it returns `EL_ERR_WOULD_BLOCK`.
Only `EL_ERR_END_OF_STREAM` is remembered, so that it stays permanent; the
writer recovers from the others as its inner writer does. A fixed buffer of
capacity 0 makes a pass-through writer. The full rules are in
[io_buffered.h](include/elemental/io_buffered.h).

- `el_buffered_writer_flush` passes every pending byte on, resumably. It
  never grows and never moves bytes.
- `el_buffered_writer_reserve(w, min)` makes room for a record before it is
  written, passing bytes on only until there is room. A record of at most
  `min` bytes then reaches the stream whole.
- `el_buffered_writer_pending` is what was accepted and not passed on. The
  owner may roll back a record with `el_buf_truncate` to a mark at or after
  `el_buf_len(buf) - pending`. A mark taken before a call stays valid only
  if the record fit the room reserved before it; a longer one may compact
  the buffer, and is left as it streamed.

Grown storage grows only where fixed storage would wait, and its allocator
is the bound. Flush is a function of this type, not an operation of
`el_writer` ([ADR 0014](../../docs/adr/0014-flush-on-the-concrete-type.md),
provisional until M4's writer sink exists).

## Tests

| Test | What it checks |
|---|---|
| `io.contract.buf.*` | The contract suite against the memory reader and a fixed `el_buf` as a writer. |
| `io.unit.buf.*` | Whole-or-nothing append, overflow, writing in place, truncation, the all-zeros buffer, and the memory endpoint. |
| `io.contract.buffered.*`, `io.contract.buffered_grown.*` | The contract suite against the buffered writer: fixed 16 bytes and capacity 0 in front of a sink that stalls or ends; grown over an arena (`IO_ALLOC`). |
| `io.unit.buffered.*`, `io.unit.buffered_grown.*` | Each case of a write that does not fit, the one remembered status, write-through, the pass-through writer, partial flush and compaction, reserve, rollback; growth where fixed storage would wait, bounded by the allocator. |
| `io.helpers.*` | `el_read_exact` and `el_write_all` against a scripted stream: every interleaving of progress, would-block, and failure, and the bound on attempts. |
| `io.violations.*` | Each documented contract violation stops the program, for callers and for implementations. Death tests, so they hold in every assertion configuration that has assertions on. |
| `io.contract.grow_buf.*` | The io contract suite against an `el_grow_buf` as a writer, over a stack arena wrapped in the checking allocator. Only with `IO_ALLOC`. |
| `io.unit.grow_buf.*` | Growth sizes, the exact-size retry, whole-or-nothing append and a partial endpoint write at the limit, `EL_ERR_NO_SPACE` and recovery from it, the always-full buffer after deinit; every block freed with the size it was allocated with. Only with `IO_ALLOC`. |
| `io.violations.grow.*` | Each documented contract violation of `el_grow_buf` stops the program. Death tests, host only. |
| `io_posix.contract.*`, `io_posix.platform.*` | See [PORT.md](backends/posix/PORT.md). |
| `io_zephyr_uart.platform.*` | See [PORT.md](backends/zephyr_uart/PORT.md). Zephyr only, in the images holding `io`'s tests on `mps2/an385`. The writer over a real UART is tested with the receiver, in `ring_zephyr_uart.wire` and `ring_zephyr_uart.contract`. |

Each `TEST` is one CTest test: `ctest --preset gcc -R io.helpers` runs a
group, and a test binary takes GoogleTest's own flags, such as
`--gtest_filter`.

## Known gaps

No buffered reading or peek (with `frame`, M6), no flush through an
`el_writer` handle ([ADR 0014](../../docs/adr/0014-flush-on-the-concrete-type.md)),
no vectored I/O, seek, or close. Each waits
for a consumer that needs it; see the roadmap. Nothing here is designed for
operations that outlive the call that started them.
