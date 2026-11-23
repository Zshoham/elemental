# ring

A first-in, first-out buffer of bytes over caller-provided storage, filled
by one context while another empties it, with no lock: an interrupt handler
and a thread, or two threads. Its two sides are also an `el_writer` and an
`el_reader`.

| | |
|---|---|
| Stability | Experimental ([policy](../../docs/process/api-stability.md)) |
| Targets | `elemental::ring`, `elemental::ring_zephyr_uart` |
| Depends on | `status` (assertions), `io` (the handle types). Runtime: `memcpy`. No `__atomic_*` symbol. |
| Allocates | No |
| Kconfig | `RING`, needs the probed `COMPILER_ATOMICS`; `RING_ZEPHYR_UART` |

## What is here

| Piece | Kind | Header |
|---|---|---|
| `el_ring`, `el_ring_init` | The object, over the caller's storage | [`elemental/ring.h`](include/elemental/ring.h) |
| `el_ring_write`, `el_ring_room`, `el_ring_close`, `el_ring_as_writer` | The producer's side | the same |
| `el_ring_read`, `el_ring_len`, `el_ring_as_reader` | The consumer's side | the same |
| `el_zephyr_uart_irq` (`RING_ZEPHYR_UART`) | Platform backend: a Zephyr UART's receive interrupt as the producer of the caller's ring | [`elemental/ring_zephyr_uart.h`](backends/zephyr_uart/include/elemental/ring_zephyr_uart.h), [PORT.md](backends/zephyr_uart/PORT.md) |

The design, the examples that shaped it, and what the evidence does and
does not show are in the [proposal](../../docs/proposals/ring.md). The
rule for atomics, and why an interrupt handler is one more context, is
[ADR 0023](../../docs/adr/0023-atomics-and-interrupt-context.md). The
deferred log sink built on the ring is in
[`log`](../log/README.md#logging-from-an-interrupt-handler).

## Using it

A receive interrupt has bytes and nowhere to put them; the code that wants
them runs later, in a thread. The handler writes, the thread reads:

```c
static uint8_t storage[64];
static el_ring ring;

/* Before the interrupt is enabled. */
el_ring_init(&ring, storage, sizeof(storage));

/* In the handler: the producer. */
const size_t kept = el_ring_write(&ring, bytes, got);
if (kept < got) {
    /* overrun: the ring is full, and a handler cannot wait */
}

/* In the thread: the consumer, through a stream. */
const el_reader in = el_ring_as_reader(&ring);
size_t n;
const el_status status = el_read(in, buf, sizeof(buf), &n);
if (status == EL_ERR_WOULD_BLOCK) {
    /* nothing yet: wait on whatever the handler signals */
}
```

[examples/uart_ring](../../examples/uart_ring) is this, complete, for a
Zephyr UART: the receive handler, through `ring_zephyr_uart`, which fills
the example's ring, and the semaphore the thread waits on. Logging from an
interrupt handler through a deferred sink is its timer's, in
[isr_tick.c](../../examples/uart_ring/isr_tick.c).

- **Two sides.** Each function belongs to the producer's side or the
  consumer's. One context at a time may use each side, and the two sides
  may run at the same moment. That is the whole concurrency contract. Two
  producers, a thread and a handler that both write, need the
  application's exclusion: the thread masks the interrupt around its
  writes.
- **Any capacity, and all of it.** The storage need not be a power of two,
  and every byte of it holds data.
- **The plain functions cannot fail.** They return how many bytes moved; 0
  means full, empty, or closed. `el_ring_room` and `el_ring_len` are lower
  bounds: room the producer saw is still there for its next write, so a
  producer that must write all of something or none of it checks first.
- **The streams stall.** A full ring's writer and an empty ring's reader
  return `EL_ERR_WOULD_BLOCK`, never `EL_ERR_NO_SPACE`: a writer cannot
  give a ring more storage, and the room comes back when the other side
  reads. `el_write_all` and `el_read_exact` resume after it as for any
  stream.
- **Closing is the producer's.** After `el_ring_close` the writer reports
  `EL_ERR_END_OF_STREAM`, and the reader reports it once every byte
  written before the close has been read. Only setting the ring up again
  reopens it.
- **The ring wakes nobody.** What the consumer waits on when the ring is
  empty is the platform's, and the application's to write.
- **Set up first, then leave it where it is.** A ring is set up before the
  second context can see it, and is not moved or copied while either side
  uses it.

### Contract violations

Nothing here reports a failure but the two streams, whose statuses are
above. What only a bug can produce is asserted
([conventions.md](../../docs/conventions.md#arguments)):

| Contract violation, asserted | |
|---|---|
| A null ring | A calling bug |
| Null storage with a nonzero capacity | A calling bug |
| A capacity above `SIZE_MAX / 2` | The indices count to twice the capacity |
| A null buffer with a nonzero size | A calling bug |

Two contexts on one side at once is a contract violation that nothing can
assert: it is a data race.

## Atomics, and no compiler helper

Each of the ring's three shared words has one context that writes it. The
only atomic operations are a load and a store of a word, which every
verified compiler turns into instructions: on Arm an `ldr` or `str` and a
`dmb`. There is no read-modify-write, which on a Cortex-M0 would be a call
to `__atomic_fetch_add_4`, a function nothing provides. The Zephyr symbol
check fails on any `__atomic_*` symbol
([build.md](../../docs/build.md#the-symbol-check)).

The two indices count modulo twice the capacity, so their difference is
the number of bytes held and a position is a comparison and a subtraction:
no division, which on a Cortex-M0 would be a call to `__aeabi_uidivmod`.

*Measured* (2026-11-17, Zephyr SDK GCC 14.3.0, Zephyr's default size
optimization, assertions trapping): `libelemental_ring.a` is 496 bytes of
text for the Cortex-M0+ and 454 for the Cortex-M3, of which the two
streams are 108 and 98; its only undefined symbol is `memcpy`, and it
holds eight `dmb` instructions. The deferred sink in `log` is 536 and 552
bytes.

In C the words are `_Atomic size_t`. A C++ translation unit sees `size_t`
of the same size and alignment, which the library asserts, because C++ has
no `_Atomic`; C++ code uses the functions and never the fields.

Clang for ARMv6-M (`thumbv6m-none-eabi`) calls `__atomic_load_4` and
`__atomic_store_4` unless given the target feature `atomics-32`; that
combination is not supported, and the symbol check would report it. No Arm
target here is built with Clang.

## What the tests show, and what they do not

| Evidence | Shows | Does not show |
|---|---|---|
| Unit tests and the io contract suite, one context | The ring's arithmetic: wrap, full, empty, every split | Anything about two contexts |
| The stress test, two host threads, x86-64 | Bytes arrive whole and in order while both sides run on two cores | Ordering on a processor that reorders more than x86-64 |
| The same under ThreadSanitizer (`tsan` preset) | No data race in C's terms | What a processor does: it is a model of C's rules, run on x86-64 |
| The UART example on `mps2/an385` under QEMU | A real exception entry preempts the thread on one core; the handler writes, the thread reads the same bytes | Timing; every point at which a handler could preempt; two cores |
| The symbol check on Cortex-M0, Cortex-M0+, Cortex-M3 | The two operations are instructions, not calls | That those instructions are enough on hardware |
| Hardware | Not done | |

With the ring broken on purpose (its words plain, or atomic with every
order relaxed) the stress test's bytes still arrive in order on x86-64;
only ThreadSanitizer reports it. The run that counts is the `tsan`
preset's.

## Tests

| Test | What it checks |
|---|---|
| `ring.contract.*` | The io contract suite against the ring's writer and reader, on streams that start in the middle of the storage and wrap. 10 run, 3 skipped: a ring stalls when full, so it neither ends nor reports `EL_ERR_NO_SPACE` after an exact count, and its reader here ends (it is closed after the content). A second fixture, reader half only, leaves the ring open after the content, so that its reader stalls instead of ending (`EL_IO_CONTRACT_THEN_STALLS`): 4 run, 9 skipped (no writer half; the reader's end is not there to check; and no `reader_stalled`, which the closed fixture has). |
| `ring.unit.*` | A new ring; every byte of the storage used; partial writes and reads and their counts; `room` and `len` after each step; bytes in order from every value of the indices and in every size, for capacities 1, 2, 3, 7 and 16, with the bytes around the storage untouched; a ring kept full across a hundred laps; a ring with no storage; a size of 0 with a null buffer; close, twice, and what it leaves readable; setting up again; the statuses of the two streams when full, empty and closed. |
| `ring.stress.*` | A thread on each side, through rings of 1, 7, 64 and 4096 bytes: a million bytes through the plain functions, and 200 000 through the streams with the reader ending after the last byte; and 20 000 closes, each just after the last bytes, with the reader spinning on the empty ring (`RingStressClose`). Host only. Fails, and does not hang, against a ring that stops moving bytes or never stops. Under the `tsan` preset, ThreadSanitizer reports a ring whose words are not atomic or whose orders are relaxed. |
| `ring.violations.*` | Each documented contract violation stops the program. Death tests, host only. |
| `ring_zephyr_uart.platform.*`, `ring_zephyr_uart.wire.*`, `ring_zephyr_uart.contract.*` | See [PORT.md](backends/zephyr_uart/PORT.md). Zephyr only, on `mps2/an385`: the first in the images holding `ring`'s tests, the other two in the `elemental.tests.ring.uart` image, over UART1 joined to UART2 under QEMU. |
| `example.uart_ring.*` | [examples/uart_ring](../../examples/uart_ring): see the table above. Zephyr, `mps2/an385` under QEMU only. |

## Known gaps

Deferred until a consumer needs them: lending a region of the storage to
read or write in place; skipping bytes on the consumer's side without
copying them; a notification for the consumer, which is the application's
today; several producers or consumers. Not run on hardware, and not
between two cores of an Arm processor
([proposal](../../docs/proposals/ring.md#evidence-required-before-stable)).
