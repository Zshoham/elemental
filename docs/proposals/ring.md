# Proposal: ring, and logging from an interrupt handler

Status: Accepted (2026-11-17), with the owner's [decisions](#decisions),
and implemented as experimental:
[modules/ring](../../modules/ring/README.md) and the deferred sink in
[modules/log](../../modules/log/README.md#logging-from-an-interrupt-handler).
Author, date: design pass for M5, 2026-11-17

Milestone: [M5](../roadmap.md#m5--ring-and-interrupt-context). Plan entry:
[module-plan.md](../module-plan.md#added-by-this-plan). The decision on
atomics the roadmap asks for is
[ADR 0023](../adr/0023-atomics-and-interrupt-context.md), accepted with
this document. Two things were proposed: a new module, `ring`, and one
addition to [`log`](../../modules/log/README.md), the deferred sink that
M4 left for this milestone.

Labels as in [architecture.md](../architecture.md): **Source fact** is
something read in a cited source ([Sources](#sources)), **Measured** is a
number produced in this pass with its method, and everything else is a
recommendation. The examples were compiled and run against a prototype
kept outside the repository ([Verification](#verification)).

Three decisions were the owner's and are listed first under
[Decisions](#decisions). The owner took the recommendation in each:
atomics are loads and stores of one word; a full ring's writer returns
`EL_ERR_WOULD_BLOCK`; the ring pipe example is retired.

## As accepted and implemented

The proposal below is kept as written for review. What changed between it
and [modules/ring](../../modules/ring/README.md) and the deferred sink:

- **A side loads its own word relaxed**, and only the other side's with
  acquire. The prototype loaded every word with acquire. A read or a write
  is now one acquire load and one release store, the two barriers on Arm
  that [ADR 0023](../adr/0023-atomics-and-interrupt-context.md) counts.
- **`el_ring_room` is 0 once the ring is closed**, since it is what a write
  would take; the sketch below left closing out of it.
- **The two streams are their own object**, `ring_io.c`, so a program that
  calls only `el_ring_read` and `el_ring_write` links no operation table.
- **Sizes**, measured on the module as built for the test images:
  `ring` 496 bytes of text on the Cortex-M0+ and 454 on the Cortex-M3,
  against the prototype's 578 and 538 below; the deferred sink 536 and
  552, against 512 and 514. Undefined: `memcpy` only.
- **The UART example** is [examples/uart_ring](../../examples/uart_ring):
  `uart_reader`, Example 1's handler and thread with the semaphore wait,
  the counts the handler keeps (interrupts, bytes, bytes dropped), and
  `uart_reader_start`, which drops bytes the device held before the
  reader listened. Its test, `example.uart_ring`, is in `ring`'s image in a
  scenario of its own, `elemental.tests.ring.uart`, on `mps2/an385` only:
  `ELEMENTAL_ZEPHYR_UART_WIRE` adds the QEMU wire below and
  `CONFIG_UART_INTERRUPT_DRIVEN` the interrupt. Its three tests: lines
  sent out of UART1 are read through the `el_reader` as sent, with
  interrupts taken and the reader having waited; the handler's records
  arrive through the deferred sink only when the thread drains them, each
  timed by a clock read in the handler; and a 16-byte ring sent 24 bytes
  with nobody reading keeps the first 16, drops 8, and says so in records.
- **The stress tests** give up after 30 seconds without progress, so a
  ring broken on purpose fails them and does not hang them. A third,
  `RingStressClose`, closes the ring 20 000 times while the reader spins
  on it: the only test that can see a reader which looks at the close
  after the bytes rather than before.
- **The `tsan` preset** runs the tests of `ring` and of the deferred sink
  only, as recommended.
- **`examples/consume_installed`** runs the installed io contract suite
  against the installed `ring`, with the fixture `ring`'s own tests use.
- **`time_zephyr` was read from an interrupt handler** by the UART test,
  under QEMU: [PORT.md](../../modules/time/backends/zephyr/PORT.md) says so
  now.

What was run on the module, and what was not, is in
[build.md](../build.md#what-is-verified); what each kind of evidence shows
is in [ring's README](../../modules/ring/README.md#what-the-tests-show-and-what-they-do-not).

## Problem

A device's receive interrupt has bytes to hand over and nowhere to put
them: the code that wants them runs later, in a thread. The same handler
cannot log, because every sink `log` has writes to a stream, and a handler
must not wait for one. Both need a first-in, first-out buffer that one
context fills while another empties it.

Every firmware project writes that buffer again, and it is easy to write
with a data race. Elemental has one, the ring pipe example
(`examples/backend_ring`, retired by this proposal), whose header says it
is not safe between a thread and an interrupt handler: both sides update
one count.

## Usage examples

### Example 1: a receive interrupt fills a ring, a thread reads it

A Zephyr UART with interrupt-driven receive. The handler is the ring's
producer and the thread its consumer; the thread reads through an
`el_reader`, so the code behind it does not know there is a UART.

```c
typedef struct uart_rx {
    const struct device *dev;
    el_ring ring;         /* the handler writes, a thread reads */
    struct k_sem ready;   /* given by the handler when bytes arrive */
    const el_logger *log; /* the handler's */
    uint32_t interrupts;  /* handler only */
} uart_rx;

static void uart_rx_isr(const struct device *dev, void *user_data)
{
    uart_rx *const rx = user_data;
    rx->interrupts++;
    uart_irq_update(dev);
    while (uart_irq_rx_ready(dev) > 0) {
        uint8_t bytes[8];
        const int got = uart_fifo_read(dev, bytes, sizeof(bytes));
        if (got <= 0) {
            break;
        }
        const size_t kept = el_ring_write(&rx->ring, bytes, (size_t)got);
        if (kept < (size_t)got) {
            EL_LOG_WARN(rx->log, "%u bytes dropped, the ring is full", (unsigned)((size_t)got - kept));
        }
        if (bytes[got - 1] == '\n') {
            EL_LOG_INFO(rx->log, "end of line, interrupt %u", (unsigned)rx->interrupts);
        }
    }
    k_sem_give(&rx->ready);
}
```

The thread, condensed from the prototype's `main`:

```c
static uint8_t rx_storage[64];

    el_ring_init(&rx.ring, rx_storage, sizeof(rx_storage));
    k_sem_init(&rx.ready, 0, 1);
    uart_irq_callback_user_data_set(rx_dev, uart_rx_isr, &rx);
    uart_irq_rx_enable(rx_dev);
    const el_reader in = el_ring_as_reader(&rx.ring);

        while (line_read < len) {
            size_t n;
            const el_status status = el_read(in, received + total, len - line_read, &n);
            if (status == EL_ERR_WOULD_BLOCK) {
                stalls++;
                if (k_sem_take(&rx.ready, K_MSEC(500)) != 0) {
                    EL_LOG_ERROR(&log, "timed out after %u bytes", (unsigned)total);
                    break;
                }
                continue;
            }
            total += n;
            line_read += n;
        }
```

Run on `mps2/an385` under QEMU, an **emulator run**, with two lines sent
out of a second UART that QEMU joins to this one
([Verification](#the-uart-under-qemu)):

```
[     0.000000] I uart2: end of line, interrupt 43
[     0.000000] I uart2: end of line, interrupt 83
[     0.000000] I main: 84 bytes read through el_reader, as sent; 83 interrupts, 27 stalls, 0 records lost
```

**Friction found.** The ring does not wake anyone. The handler gives a
semaphore and the thread takes it, both written by the application: what
to wait on is the platform's, and a ring that took it as an operation
would be a different object on every platform. An empty ring is
`EL_ERR_WOULD_BLOCK`, as every stalled stream is.

The first version of this example sent both lines before reading either.
The 64-byte ring was then a 32-byte one and filled; the handler dropped
bytes and said so, in records that reached the console afterwards:

```
[     0.000000] W uart2: 1 bytes dropped, the ring is full
```

### Example 2: the handler logs, the thread writes

The awkward case: the handler in Example 1 logs, and must not wait for the
console. Its logger's sink is a **deferred sink**, which copies each record
into a ring. The thread hands the records on to the sink that writes them.

```c
static uint8_t log_ring_storage[256];
static uint8_t log_record_storage[96];

    /* The handler's logger: records wait in a ring for this thread. */
    el_ring log_ring;
    el_ring_init(&log_ring, log_ring_storage, sizeof(log_ring_storage));
    el_log_deferred_sink deferred;
    el_log_deferred_sink_init(&deferred, &log_ring, log_record_storage, sizeof(log_record_storage));
    el_logger isr_log;
    el_logger_init(&isr_log, el_log_deferred_sink_as_sink(&deferred), el_zephyr_clock(), "uart2",
                   EL_LOG_LEVEL_INFO);

    /* This thread's: lines on the console. */
    char line_storage[120];
    el_buf line;
    el_buf_init(&line, line_storage, sizeof(line_storage));
    el_log_writer_sink console;
    el_log_writer_sink_init(&console, console_out, &line);

    /* ... and, whenever the thread gets round to it: */
    el_log_deferred_sink_drain(&deferred, el_log_writer_sink_as_sink(&console));
```

The handler's call site is the one every other caller writes,
`EL_LOG_WARN(rx->log, ...)`: it is given a logger and does not know the
sink defers.

When the thread does not keep up, records are lost and counted, and the
handler never waits. The same code on the host, with a manual clock and
eight records logged into a ring that holds about five, printed:

```
[     0.750000] I uart: received 0 bytes
[     1.000000] I uart: received 1 bytes
[     1.250000] I uart: received 2 bytes
[     1.500000] I uart: received 3 bytes
[     1.750000] I uart: received 4 bytes
5 written, 3 lost
```

**Friction found.** Three pieces of storage for one deferred logger: the
ring, the record being built, and the line of the sink that finally writes
it. Each is a real limit the application chooses: how many records may
wait, how long a message may be, how long a line may be. The second could
be hidden by building the record on the stack, which is what
[ADR 0022](../adr/0022-log-sink-lends-the-buffer.md) moved away from.

**One context logs through a deferred sink at a time.** If a thread and a
handler both log through it, the thread's calls can be interrupted
part-way by the handler's. The application excludes one from the other, by
masking the interrupt around the thread's calls, or gives each its own
sink and ring. This is the rule every sink has
([log.h](../../modules/log/include/elemental/log.h)); it matters more here
because an interrupt cannot be asked to wait.

### Example 3: two threads on a host

One thread produces, one consumes, and the consumer learns that the
producer has finished. This is also the shape of the stress test.

```c
static void *produce(void *arg)
{
    el_ring *const ring = arg;
    const el_writer out = el_ring_as_writer(ring);
    size_t sent = 0;
    while (sent < TOTAL) {
        /* ... fill chunk with the next len bytes ... */
        size_t n;
        const el_status status = el_write_all(out, chunk, len, &n);
        sent += n;
        if (status == EL_ERR_WOULD_BLOCK) {
            sched_yield(); /* full: the consumer is behind */
        }
    }
    el_ring_close(ring);
    return NULL;
}

    /* The consumer. */
    const el_reader in = el_ring_as_reader(&ring);
    for (;;) {
        size_t n;
        const el_status status = el_read(in, chunk, sizeof(chunk), &n);
        if (status == EL_ERR_END_OF_STREAM) {
            break;
        }
        if (status == EL_ERR_WOULD_BLOCK) {
            sched_yield();
            continue;
        }
        /* ... check the n bytes ... */
    }
```

Four million bytes arrived in order through rings of 1, 7, 64 and 4096
bytes, under ThreadSanitizer with GCC and with Clang, with no report.

## Backend example

`ring` has no backend: it is computation on caller memory, the same on
every platform. Its two adapters are themselves implementations of
`el_reader` and `el_writer`, and Example 1's UART is the backend example
an `io` port would copy: a device's interrupt on one side of a ring, a
handle on the other.

## Why not something that exists

- **The ring pipe example.** One context only: its reader and its writer
  both update `count`. This module is that example with the count
  replaced by two indices, each written by one side.
- **Zephyr's `ring_buf`.** *Source fact:* it is safe for one producer and
  one consumer without locking, in the same way
  (`doc/kernel/data_structures/ring_buffers.rst`, v4.5.0). It exists only
  on Zephyr, and is not a stream: the code that reads it could not also
  read a pipe or a socket on a host.
- **A message queue or a pipe of the kernel's** (`k_msgq`, `k_pipe`).
  Only on Zephyr; they wait and wake, which an interrupt handler's side
  does not need and a host test cannot use.
- **`el_buf`.** A buffer with one owner: it has a length, not two ends,
  and nothing in it may be touched by a second context.

## Dependencies and cost

| | |
|---|---|
| Elemental modules | `ring`: `status` (assertions), `io` (the handle types its adapters return). The deferred sink adds `ring` to `log`, behind an option |
| Runtime symbols (freestanding) | `memcpy`. No `__atomic_*` symbol. *Measured* |
| Allocation | None |
| Per-object storage | 32-bit: `el_ring` 20 bytes and the caller's storage, every byte of which holds data; `el_log_deferred_sink` 32 bytes, the record storage, and a ring, in which a record takes 24 bytes and its message. *Measured* |
| Stack | From `el_log_printf` to `el_ring_write` 192 bytes, against 160 to the writer sink's operation; formatting a decimal is the deeper path, 336 bytes. *Measured* |
| New status codes | None |

*Measured* on 2026-11-17: the prototype compiled for Cortex-M0,
Cortex-M0+ and Cortex-M3 by the Zephyr SDK 1.0.1's GCC 14.3.0 at `-Os`,
assertions trapping. Text bytes: `ring` 578 on the two ARMv6-M cores and
538 on the Cortex-M3; the deferred sink 512 and 514. Undefined symbols,
after those the libraries define for each other: `memcpy`. Stack is the
frames GCC's `-fstack-usage` reports for the Cortex-M0+, summed along the
call path, without `memcpy`'s.

## Contracts

- **Ownership and lifetime:** the caller provides the `el_ring` and its
  storage and keeps both alive while either side uses the ring. The ring
  borrows the storage; nothing is released. A ring must not be moved or
  copied once either side uses it, and it is set up before the second
  context can see it. The deferred sink borrows its ring and its record
  storage the same way.
- **Two sides.** Each function belongs to the producer's side
  (`el_ring_write`, `el_ring_room`, `el_ring_close`, the writer handle) or
  the consumer's (`el_ring_read`, `el_ring_len`, the reader handle). One
  context at a time may use each side, and the two sides may run at the
  same moment. That is the whole concurrency contract.
- **Errors:** the plain functions cannot fail: they return how many bytes
  moved, and 0 means full or empty. The handles follow `io`:

  | | Writer | Reader |
  |---|---|---|
  | Bytes moved | `EL_OK`, `1 <= *n <= len` | `EL_OK`, `1 <= *n <= cap` |
  | Full / empty | `EL_ERR_WOULD_BLOCK` | `EL_ERR_WOULD_BLOCK` |
  | Closed | `EL_ERR_END_OF_STREAM` | `EL_ERR_END_OF_STREAM`, once what was held is read |

  A full ring is a stream that stalls, not storage that ran out
  ([decision 2](#decisions)).
- **Closing** is the producer's: it will write no more. Bytes already in
  the ring are still read. Nothing reopens a ring but setting it up again.
- **Bytes are copied in and out**; no pointer into the storage is handed
  to either side.
- **The deferred sink:** a record goes into the ring whole or not at all.
  One that does not fit, in the record storage or in the ring, is lost,
  counted, and reported to the logger's caller as nothing: a log call
  cannot fail. The count may be read from any context. Draining gives the
  destination every record that was in the ring when the call began, and
  no more, so the call is bounded however fast records arrive.
- **Execution context:** nothing here blocks, allocates, or takes a lock.
  Every function of `ring` may be called from an interrupt handler, under
  the two-sides rule. A log call through the deferred sink may be made
  from one when the logger's clock allows it. Draining calls the
  destination sink, and may be made only where that sink may be used: a
  thread, for a sink that writes to a stream.
- **Contract violations**, asserted: a null ring or sink; null storage
  with a nonzero capacity; a capacity above `SIZE_MAX / 2`; a null buffer
  with a nonzero size.
- **Exceptions to docs/conventions.md:** two, both in
  [ADR 0023](../adr/0023-atomics-and-interrupt-context.md).
  The object is used by two contexts at once, where the default is one;
  and it may not be moved while in use, where the default allows moving an
  object no handle refers to.

## Interface model

Plain functions on a caller-owned object
([ADR 0002](../adr/0002-interface-model.md), first mechanism): a ring has
one implementation. Its adapters hand out `io`'s handles. Nothing is
chosen at build time.

```c
/* elemental/ring.h */
#ifdef __cplusplus
typedef size_t el_ring_word; /* same size and alignment: asserted in ring.c */
#else
typedef _Atomic size_t el_ring_word; /* one side writes it, the other reads it */
#endif

typedef struct el_ring {
    uint8_t *storage;
    size_t cap;
    el_ring_word head;   /* the consumer's: where the oldest byte is */
    el_ring_word tail;   /* the producer's: where the next byte goes */
    el_ring_word closed; /* the producer's: 1 once it will write no more */
} el_ring;

/* `storage` may be null only if `cap` is 0. Every byte of it holds data. */
void el_ring_init(el_ring *ring, void *storage, size_t cap);

/* The producer's side. */
size_t el_ring_write(el_ring *ring, const void *data, size_t len); /* bytes taken, 0 to len */
size_t el_ring_room(const el_ring *ring); /* what a write would take now, at least */
void el_ring_close(el_ring *ring);
el_writer el_ring_as_writer(el_ring *ring);

/* The consumer's side. */
size_t el_ring_read(el_ring *ring, void *buf, size_t cap); /* bytes given, 0 to cap */
size_t el_ring_len(const el_ring *ring); /* what a read would give now, at least */
el_reader el_ring_as_reader(el_ring *ring);

/* elemental/log_deferred.h */
size_t el_log_deferred_sink_record_size(size_t message_len);
void el_log_deferred_sink_init(el_log_deferred_sink *sink, el_ring *ring, void *record,
                               size_t record_cap);
el_log_sink el_log_deferred_sink_as_sink(el_log_deferred_sink *sink);           /* the logging side */
size_t el_log_deferred_sink_drain(el_log_deferred_sink *sink, el_log_sink to);  /* the writing side */
uint32_t el_log_deferred_sink_lost(const el_log_deferred_sink *sink);           /* either */
```

**Any capacity, and all of it.** The two indices count modulo twice the
capacity, so that their difference is the number of bytes held, from 0 to
the capacity: no byte is kept free to tell full from empty, the capacity
need not be a power of two, and finding a position is a comparison and a
subtraction. A division would be a call to `__aeabi_uidivmod` on a
Cortex-M0. *Source fact:* Zephyr's `ring_buf` counts the same way.

**`room` and `len` are lower bounds.** The producer's `el_ring_room` can
only be made larger by the consumer, so room that was seen is still there
for the next write. The deferred sink relies on it to write a record whole.

**The plain functions return a count.** A function that cannot fail
returns its result ([conventions.md](../conventions.md#errors), rule 3),
and a handler reads better for it: `kept < got` is the overrun.

### The deferred sink, and ADR 0022's question

[ADR 0022](../adr/0022-log-sink-lends-the-buffer.md) made a sink lend the
buffer a message is written into, and asks to be revisited if this sink
"needs a record's message in a ring it does not own as one contiguous
buffer". **It does not.**

- **Logging.** `begin` lends the record storage after the room for the
  record's fields, as the memory sink lends its storage. The message is
  formatted there. `end` writes the fields in front of it and copies
  fields and message into the ring with one `el_ring_write`, so the other
  side sees the record whole or not yet. The ring stays a ring of bytes: a
  record may lie across the end of its storage.
- **Draining.** The fields are read out of the ring; the destination sink
  begins the record and lends its own buffer; the message is read from
  the ring straight into that buffer; the destination ends the record. The
  draining side needs no storage of its own.

The cost is one copy of each message more than a sink that writes at once
makes, and the record storage. The alternative is a ring that can lend a
contiguous region, which either wastes the bytes before the storage's end
or makes the ring one of records, not bytes. No consumer needs that.

This bears on ADR 0022, which awaits the owner's review, only as evidence:
the pair of operations fits the one sink it was most likely not to fit.

**The record in the ring** is the memory sink's: time, tag pointer,
length, frequency, level, then the message. The tag is kept as a pointer,
as the contract allows. The frequency is kept in every record, 4 bytes,
because one sink may serve loggers with different clocks; this answers the
[log proposal's](log.md#open-questions) open question.

## Configuration

| Option | Default | Use case | Why not the linker / a parameter |
|---|---|---|---|
| `RING` | on | The module. Selects `IO`; depends on `COMPILER_ATOMICS` | As every module |
| `COMPILER_ATOMICS` | probed | Not settable. The compiler accepts an acquire load and a release store of an `_Atomic size_t` | A property of the toolchain. It makes a toolchain without them fail at configuration, as [module-plan.md](../module-plan.md#4-risks-and-what-would-change-the-plan) asks |
| `LOG_DEFERRED` | on where `RING` is | The deferred sink, the one part of `log` that needs `ring`. Off, `log` builds and links no ring | The linker cannot help where `ring` cannot be compiled. It is `IO_ALLOC`'s pattern |

Every toolchain verified today passes the probe, so `LOG_DEFERRED` is off
only in a build that turns `RING` off. The simpler alternative is for
`LOG` to select `RING` and to depend on the probe itself; then a toolchain
without the two operations loses all of `log`, not one sink.

## What the evidence will and will not show

The roadmap asks for this to be recorded honestly. The same text goes
into [build.md](../build.md#what-is-verified) with the results.

| Evidence | Shows | Does not show |
|---|---|---|
| Unit tests and the io contract suite, one context | The ring's arithmetic: wrap, full, empty, every split | Anything about two contexts |
| The stress test, two host threads, x86-64 | Bytes arrive whole and in order while both sides run on two cores | Ordering on a processor that reorders more than x86-64 |
| The same under ThreadSanitizer | No data race in C's terms: every word two threads touch is atomic, and the acquire and release orders are what make each byte's write come before its read | What a processor does. It is a model of C's rules, run on x86-64 |
| The interrupt-driven UART on `mps2/an385` under QEMU | A real exception entry preempts the thread on one core, the handler writes, the thread reads the same bytes | Timing, and overruns from a handler that is late. Every point at which a handler could preempt: QEMU runs translated blocks of instructions, and at which boundaries it takes an interrupt was not examined. Two cores |
| The symbol check on Cortex-M0, Cortex-M0+, Cortex-M3 | The compiler turned the two operations into instructions, not calls | That those instructions are enough on hardware |
| Hardware | Not done. No board | |

*Measured*, on the stress test's sensitivity, with the ring broken on
purpose in two ways. With its words made plain `size_t`, four million
bytes still arrived in order on this x86-64 machine, and ThreadSanitizer
reported three data races. With the words atomic and every order relaxed,
the bytes arrived in order again, and it reported one, in the copy. So the
check of the bytes alone would pass either broken ring here; the sanitizer
is the part of the test that fails.

## Verification

- **Unit tests** (`ring.unit`): empty, full, wrap at every offset for
  capacities 0, 1, 2 and a larger one; a write or read split by the end of
  the storage; `room` and `len` after each step; close; a capacity of 0.
- **The io contract suite** against the adapters, with the ring pipe's
  fixture: `writer_stalled` and `reader_stalled`; a ring neither ends nor
  reports `EL_ERR_NO_SPACE` after an exact count, so those two tests are
  skipped, as they are for the ring pipe today.
- **The stress test** (`ring.stress`): Example 3, in the `gcc` and `clang`
  presets, and under ThreadSanitizer. `sanitize` is AddressSanitizer,
  which cannot be combined with it, so this needs a fourth sanitizer
  configuration: a `tsan` preset that builds and runs the `ring` and
  deferred-sink tests only. Host only: it needs threads.
- **The deferred sink**: the log sink contract suite, the sink draining
  into a memory sink, with every capability; its own tests for a record
  across the end of the ring, a record too long for the record storage, a
  full ring, the lost count, a drain bounded by what was there, a
  destination that refuses or cuts; and a stress test of logging in one
  thread while another drains.
- **Death tests** for each contract violation, host only.
- **Broken on purpose:** an index that is not atomic, and orders that are
  relaxed (the stress test under ThreadSanitizer must report each); one
  byte kept free; a count that
  wraps at the capacity; close seen before the last bytes; a record
  published in two writes.

### The UART under QEMU

The roadmap asks for an interrupt-driven UART example on a Zephyr
Cortex-M target under QEMU, with the deferred sink used from its handler.

The example is [Example 1](#example-1-a-receive-interrupt-fills-a-ring-a-thread-reads-it),
under `examples/`, built only in a Zephyr build with interrupt-driven
UARTs. It is not a shipped backend
([module-plan.md](../module-plan.md#considered-and-rejected)).

Its test needs bytes to arrive. `mps2/an385` has three UARTs, and QEMU
connects each to whatever the command line says. The test image adds two
ends of one socket, so that what the image sends on UART1 arrives on
UART2:

```cmake
list(APPEND QEMU_EXTRA_FLAGS
  -chardev socket,id=wire0,path=uart-wire.sock,server=on,wait=off -serial chardev:wire0
  -chardev socket,id=wire1,path=uart-wire.sock -serial chardev:wire1)
```

The test is a GoogleTest test in `ring`'s image, registered only where
that wire exists: it sends lines out of UART1, reads them through the
`el_reader`, and checks the bytes, that interrupts were taken, and the
records the handler logged. The console stays on UART0, so Twister reads
the report as before.

| Target | Toolchain | Level intended |
|---|---|---|
| x86-64 Linux | GCC, Clang, Clang with ASan and UBSan | Run: unit, contract, deferred sink, death tests |
| x86-64 Linux | GCC and Clang with ThreadSanitizer | Run: the two stress tests |
| `native_sim` | host GCC, 32-bit | Run, without the stress tests and the UART |
| `mps2/an385` | Zephyr SDK GCC | Emulator run of the unit and contract tests and of the UART example, interrupts taken; symbol check |
| `nucleo_g0b1re_renode` | Zephyr SDK GCC | Emulator run of the unit and contract tests, no interrupt; symbol check |
| `qemu_cortex_m0` | Zephyr SDK GCC | Symbol check |

What was run for this proposal, on a prototype outside the repository, on
2026-11-17:

- Examples 2 and 3 on x86-64 with GCC 16.2.1 and Clang 23.1.5, `-std=c11`
  with the repository's warnings as errors; Example 2 also under
  AddressSanitizer and UndefinedBehaviorSanitizer; Example 3 under
  ThreadSanitizer with both compilers, and against the ring broken on
  purpose in the two ways above.
- The io contract suite against the prototype's adapters, with the ring
  pipe's fixture renamed: 10 passed, 2 skipped, under AddressSanitizer.
- The two headers as C++11 with GCC and Clang.
- The prototype compiled for Cortex-M0, Cortex-M0+ and Cortex-M3: sizes
  and undefined symbols, above.
- Examples 1 and 2 together as a Zephyr v4.5.0 application for
  `mps2/an385`, run under QEMU 11.1.3: an **emulator run**, started by
  hand through the build's `run` target. The handler read Zephyr's clock
  through `el_zephyr_clock()`, which
  [PORT.md](../../modules/time/backends/zephyr/PORT.md) records as not yet
  run from a handler.

Not run: anything under Twister, anything on `native_sim` or under
Renode, the log sink contract suite against the deferred sink, any death
test, and anything on hardware.

## What changes elsewhere

- **conventions.md** says no module contains an atomic. It will name
  `ring` and point to ADR 0023.
- **The ring pipe example** is retired ([decision 3](#decisions)). Its
  fixture and tests move to `modules/ring`. `examples/consume_installed`
  runs the installed contract suite against `elemental::ring` with a
  fixture of its own. [architecture.md](../architecture.md#3-interfaces-and-backends)
  quotes the example as the implementer's side; it will quote the ring's
  adapter, which is the same code.
- **M6** says the frame glue runs "over the ring pipe": over the ring.
- **`time_zephyr`'s PORT.md** records reading the clock from a handler as
  not run. It will have been, under QEMU.

## Evidence required before stable

As [api-stability.md](../process/api-stability.md). Specific to `ring`: a
run on a real Cortex-M board with a real interrupt, since the emulator
does not show timing; a second consumer not written to demonstrate it
(the frame glue of M6, or async discovery); and one use between two cores
of an Arm processor, the case no evidence here covers.

## Decisions

**For the owner.** Each changes something that later modules inherit.

1. **Atomics** ([ADR 0023](../adr/0023-atomics-and-interrupt-context.md)).
   Recommended: C11 acquire loads and release stores of one word, each
   word written by one context, nothing else; checked by the symbol check.
   The alternative is a critical-section operation the caller supplies to
   every ring: any toolchain, and several producers, at the price of an
   operation in every ring and interrupts masked on every read and write.
2. **A full ring's writer returns `EL_ERR_WOULD_BLOCK`**, as the ring pipe
   example does. [ADR 0009](../adr/0009-capacity-status.md) and the plan
   name "a full ring" among the things that report `EL_ERR_NO_SPACE`.
   Recommended against that sentence: a writer cannot give a ring more
   storage, and the room comes back when the other side reads, which is
   what `io` calls a stream that stalls. `el_write_all` and `fmt`'s
   buffered printf already resume after it. `EL_ERR_NO_SPACE` stays the
   status of the deferred sink, a container that is full. The plain
   functions return a count and no status either way.
3. **The ring pipe example is retired**, not rebased. With adapters and
   close in `el_ring`, a ring pipe built on it would be a second set of
   the same adapters. The cost of retiring: `io` no longer has an example
   backend that builds on a host; its example is the UART, on Zephyr only,
   and `io_posix` is the backend a host port reads.

**Recommendations carried by the proposal**, cheap to change while `ring`
is experimental:

4. Any capacity, every byte used; indices modulo twice the capacity.
5. `el_ring_close`, on the producer's side only. Its consumers are the io
   contract suite, whose reader must end, and a host thread that finishes.
6. The plain functions return counts; only the handles return statuses.
7. One context at a time logs through a deferred sink. Several are the
   application's to exclude, as for every sink.
8. A record that does not fit is lost whole, as in the memory sink, and
   not logged short.
9. The deferred sink builds a record in storage the caller gives, and the
   ring stays a ring of bytes: ADR 0022's revisit condition is not met.
10. Draining takes its destination on each call, is bounded by what was
    in the ring, and returns the number of records handed on.
11. `COMPILER_ATOMICS` is probed; `LOG_DEFERRED` follows `IO_ALLOC`.
12. The UART is an example, tested in `ring`'s image on `mps2/an385` only,
    over two UARTs that QEMU joins.
13. A `tsan` preset, for the two stress tests.

## Open questions

- **An interrupt run on the Cortex-M0+ board.** Renode can join two UARTs
  too. Not planned: the roadmap asks for QEMU, and ARMv6-M is covered by
  the symbol check and by the ring's tests on that board without an
  interrupt. Worth doing if a later milestone needs the UART there.
- **A message that does not fit: lost, or logged short.** ADR 0022 lists
  it. A handler's record may be the one most worth keeping short.
- **Waking the consumer** is the application's here. Async discovery (M9)
  uses this UART and will say whether a ring should carry a notification.
- **Lending a region of the ring** to read or write in place, as Zephyr's
  claim functions do. No consumer; the frame glue (M6) is the likely one.
- **Skipping bytes** on the consumer's side without copying them. The
  drain copies a refused message into 16 bytes of stack to drop it.

## Sources

- Zephyr v4.5.0 (`41d10b5973`), read in the local workspace:
  `include/zephyr/sys/ring_buffer.h`,
  `doc/kernel/data_structures/ring_buffers.rst`,
  `arch/arm/core/cortex_m/Kconfig` (`ATOMIC_OPERATIONS_C`),
  `include/zephyr/drivers/uart.h`, `boards/arm/mps2/mps2_base.dtsi`,
  `cmake/emu/qemu.cmake`.
- Zephyr SDK 1.0.1, GCC 14.3.0 (`arm-zephyr-eabi`): the code and symbols
  it produced for the prototype, as measured above.
- Clang 23.1.5, `--target=thumbv6m-none-eabi`: the same, for the two
  atomic operations.
- QEMU 11.1.3, `qemu-system-arm -machine mps2-an385`: as run. Its source
  was not read.
