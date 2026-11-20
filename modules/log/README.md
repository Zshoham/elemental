# log

Logging for applications: a logger with a level, a tag and a clock; a sink
interface that decides where records go; a sink that writes lines to a
writer, one that keeps records in memory, and one that hands them to
another context; printf-style call sites with a compile-time cut-off; and a
contract suite for sinks.

Elemental's own modules never log. This is for applications, and for
backends kept outside the repository.

| | |
|---|---|
| Stability | Experimental ([policy](../../docs/process/api-stability.md)) |
| Targets | `elemental::log`, `elemental::log_contract` |
| Depends on | `status`, `io`, `fmt`, `time`; `ring` for the deferred sink (`LOG_DEFERRED`). Runtime: `memcpy`. |
| Allocates | No |
| Kconfig | `LOG`, `LOG_DEFERRED` |

## What is here

| Piece | Kind | Header |
|---|---|---|
| `el_log_level`, `el_log_record`, `el_log_sink`, `el_log_sink_write`, `el_logger`, `el_logger_init`, `el_logger_set_level`, `el_log_write` | The contract: not variadic, no global | [`elemental/log.h`](include/elemental/log.h) |
| `EL_LOG_DEBUG`, `EL_LOG_INFO`, `EL_LOG_WARN`, `EL_LOG_ERROR`, `EL_LOG`, `el_log_printf`, `el_log_vprintf`, `EL_LOG_CUTOFF` | The C call site | [`elemental/log_printf.h`](include/elemental/log_printf.h) |
| `el_log_writer_sink` | A sink: lines of text to an `el_writer` | [`elemental/log_writer.h`](include/elemental/log_writer.h) |
| `el_log_mem_sink` | A sink: records kept in caller memory | [`elemental/log_mem.h`](include/elemental/log_mem.h) |
| `el_log_deferred_sink` | A sink: records handed to another context through an `el_ring`, and the function that drains them | [`elemental/log_deferred.h`](include/elemental/log_deferred.h) |
| The log sink contract suite | GoogleTest tests, shipped as source | [`elemental/log_contract.hpp`](contract/include/elemental/log_contract.hpp), fixture in [`elemental/log_contract.h`](contract/include/elemental/log_contract.h) |

Start with `log.h`. The design, the examples that shaped it, and what was
taken from the owner's earlier `mlog` prototype are in the
[proposal](../../docs/proposals/log.md); the cut-off is
[ADR 0021](../../docs/adr/0021-log-cutoff.md), and why a message is written
into the sink's buffer is [ADR 0022](../../docs/adr/0022-log-sink-lends-the-buffer.md).
The deferred sink came with `ring`, in the
[ring proposal](../../docs/proposals/ring.md).

## Using it

Three objects are set up, and each is something the application decides:
where bytes go, how a record becomes a line, and whose records these are.

```c
el_posix_fd err;
el_posix_fd_init(&err, STDERR_FILENO);

char line_storage[160];
el_buf line;
el_buf_init(&line, line_storage, sizeof(line_storage));
el_log_writer_sink sink;
el_log_writer_sink_init(&sink, el_posix_fd_as_writer(&err), &line);

el_logger log;
el_logger_init(&log, el_log_writer_sink_as_sink(&sink), el_posix_clock(), "link",
               EL_LOG_LEVEL_INFO);

EL_LOG_INFO(&log, "starting, %d attempts", 2);
EL_LOG_WARN(&log, "%s did not answer, attempt %d of %d", peer, attempt, attempts);
```

```
[ 33601.929899] I link: starting, 2 attempts
[ 33601.929915] W link: 10.0.0.7 did not answer, attempt 1 of 2
```

A component takes a `const el_logger *` and logs through it; it does not
know where the records go. There is no default logger: an application that
wants one reachable from every file declares it, in
[three lines](../../docs/proposals/log.md#a-default-logger-is-the-applications).

- **Levels.** Debug, info, warn, error. A logger lets through its minimum
  and above; `EL_LOG_LEVEL_OFF` as the minimum lets nothing through. Below
  the minimum a call formats nothing and reads no clock.
- **Tags.** A logger's tag names the part of the program it belongs to.
  One logger per part, each with its own level, is how one part is made
  quiet and another verbose.
- **Time.** The logger reads the clock it was given, once per record. A
  sink never reads a clock: the record carries the reading and the clock's
  frequency ([ADR 0020](../../docs/adr/0020-time-representation.md)).
- **A log call cannot fail.** It returns nothing. A record the sink does
  not take is lost, and the sink counts it:
  `el_log_writer_sink_lost()`, `el_log_mem_sink_lost()`,
  `el_log_deferred_sink_lost()`.
- **Message length.** A log call has no buffer of its own. `EL_LOG_*` and
  `el_log_printf` format the message straight into the buffer the sink
  lends: the writer sink's line, the memory sink's storage, the deferred
  sink's record storage. That buffer is
  the only limit, and a grown one has none but its allocator. A message
  it cannot take whole is cut by `fmt`'s item rule (`el_log_write`: at a
  byte), and the sink counts the record as lost.
- **Formats** are what [`el_fmt_printf`](../fmt/README.md) accepts in the
  build. Add `elemental_check_formats(app)` to check literal formats in
  `EL_LOG`, all four level macros, and calls to `el_log_printf` and
  `el_log_vprintf`. Calls below `EL_LOG_CUTOFF` are checked too. See
  [build.md](../../docs/build.md#checking-printf-formats).

Other languages call `el_log_write(logger, level, message, len)`, which is
not variadic.

### The compile-time cut-off

Define `EL_LOG_CUTOFF` for a target or a file, before `log_printf.h` is
included, and every `EL_LOG_*` call below that level is not in the
program: no call, no format string.

```cmake
target_compile_definitions(app PRIVATE EL_LOG_CUTOFF=EL_LOG_LEVEL_INFO)
```

It is the consumer's own macro, like `NDEBUG`. Elemental's configuration
never sets it and the library has no compile-time level, so the two cannot
disagree ([ADR 0021](../../docs/adr/0021-log-cutoff.md)). The arguments of
a call that is cut are not evaluated; the call is still compiled first, so
its format is checked either way. Only the macros honor it.

### Choosing a sink

| Sink | A record becomes | Storage | For |
|---|---|---|---|
| `el_log_writer_sink` | One line, passed to an `el_writer` with one `el_write_all` | An `el_buf` the caller lends it, fixed or grown; a line it cannot hold is cut and still ends in a newline | A console, a file, a descriptor: a writer that does not stall |
| `el_log_mem_sink` | A record kept until cleared | The caller's bytes, where the message is formatted and kept; a record that does not fit is lost whole | Tests of code that logs; looking afterwards |
| `el_log_deferred_sink` | A record in an `el_ring`, until another context drains it into a sink that writes | A ring, and storage to build one record in; a record that does not fit either is lost whole | An interrupt handler; a thread that must not wait for a console |
| Your own | Whatever its two operations do with it | Yours, and a buffer to lend | Another format, another destination, several at once |

The writer sink's line is fixed: seconds in six columns and microseconds
of the record's time, the level's letter, the tag and a colon unless the
tag is empty, the message as it is, a newline. It holds nothing between
records, so there is nothing to flush; a buffered writer under it is
flushed by whoever created that writer
([ADR 0014](../../docs/adr/0014-flush-on-the-concrete-type.md)). When its
writer stalls or fails part-way the stream is left mid-line.

The line buffer is borrowed by pointer, and the sink empties it when it
begins each record: while the sink is in use the buffer is used for
nothing else. Fixed storage bounds a line; a grown buffer
(`el_grow_buf_init`, `elemental/io_alloc.h`) lets a line be as long as its
allocator allows, keeps the capacity of the longest line it has held, and
is freed by its owner once the sink is no longer used.

### Logging from an interrupt handler

A handler must not wait for a console. Give its logger a deferred sink:
each record is copied into a ring, and a thread hands the records on to
the sink that writes them, whenever it gets round to it.

```c
static uint8_t ring_storage[256];
static uint8_t record_storage[96];
static el_ring ring;
static el_log_deferred_sink deferred;
static el_logger isr_log;

el_ring_init(&ring, ring_storage, sizeof(ring_storage));
el_log_deferred_sink_init(&deferred, &ring, record_storage, sizeof(record_storage));
el_logger_init(&isr_log, el_log_deferred_sink_as_sink(&deferred), el_zephyr_clock(), "uart2",
               EL_LOG_LEVEL_INFO);

/* In the handler: the call site every other caller writes. */
EL_LOG_WARN(&isr_log, "%u bytes dropped, the ring is full", dropped);

/* In a thread: hand what has arrived to the sink that writes. */
el_log_deferred_sink_drain(&deferred, el_log_writer_sink_as_sink(&console));
```

- **Three sizes are yours.** The ring: how many records may wait. The
  record storage: how long a message may be. And the line of the sink
  that finally writes. `el_log_deferred_sink_record_size(len)` is what one
  record takes in the first two; on a 32-bit target that is 24 bytes and
  the message.
- **A record goes in whole or not at all.** One that does not fit, in the
  record storage or in the room the ring has left, is lost and counted
  (`el_log_deferred_sink_lost()`, readable from any context). The handler
  never waits. Nothing is formatted for a record the ring has no room to
  begin.
- **Records stay records.** The time was read where the record was logged,
  by the logger's clock, which must be one that may be read there; the
  sink that drains formats the line.
- **A drain is bounded.** It hands on what was in the ring when it began
  and returns how many; records logged meanwhile wait for the next one. It
  calls the destination sink, so it runs where that sink may: a thread.
- **One context logs through it at a time,** and one drains. A thread and
  a handler that both log through one deferred sink are two: mask the
  interrupt around the thread's calls, or give each its own sink and ring.

[examples/uart_ring](../../examples/uart_ring) logs from a UART's receive
interrupt this way, on Zephyr. What its run under an emulator shows, and
does not, is in [ring's README](../ring/README.md#what-the-tests-show-and-what-they-do-not).

### Testing code that logs

Give it a logger over a memory sink and a manual clock, and read the
records back as records:

```c
el_manual_clock clock;
el_manual_clock_init(&clock, 1000);
uint8_t storage[512];
el_log_mem_sink kept;
el_log_mem_sink_init(&kept, storage, sizeof(storage));
el_logger log;
el_logger_init(&log, el_log_mem_sink_as_sink(&kept), el_manual_clock_as_clock(&clock), "net",
               EL_LOG_LEVEL_WARN);

connect_with_retry(&log, &clock, 3);

el_log_record record;
el_log_mem_sink_get(&kept, 3, &record);
assert(record.level == EL_LOG_LEVEL_ERROR && record.timestamp == 3000);
```

`el_log_mem_sink_record_size(len)` says how many bytes one record takes,
for sizing the storage. A record's message is not terminated: compare its
length and bytes.

### Failures and contract violations

| Reported, never asserted | How |
|---|---|
| A sink that cannot take a record | The sink's begin or end returns its status; through a logger the record is lost and the sink counts it |
| A line the writer sink's buffer cannot hold: fixed storage too small, or an allocator that refused | `EL_ERR_NO_SPACE`; the cut line went out |
| A writer that stalls or fails under the writer sink | The writer's status; the stream may hold part of the line |
| A record that does not fit the memory sink | `EL_ERR_NO_SPACE`; nothing kept, nothing overwritten |
| A record that does not fit the deferred sink's record storage, or the room its ring has left | `EL_ERR_NO_SPACE`; nothing of it is in the ring |
| A message longer than the sink's buffer can take | Cut, and the sink is told: the writer sink writes the cut line, the memory sink and the deferred sink keep nothing; all count it lost |
| A record the sink a deferred sink is drained into does not take | Lost there, and counted by that sink; the drain goes on to the next |

| Contract violation, asserted | |
|---|---|
| A null logger, record, tag, sink object, or writer sink line buffer; a handle that was never made, or one with an operation missing | A calling bug |
| A sink that begins a record without lending a buffer | A broken sink |
| A null message with a nonzero length, even when the record would not be made | A calling bug |
| A record's level of `EL_LOG_LEVEL_OFF` or above; a minimum above it | Levels are constants, not input |
| A record with a frequency of 0 | A broken logger or sink driver |
| Reading a memory sink's record that is not there | A calling bug |
| A null deferred sink or ring; draining into a handle that was never made | A calling bug |
| A format the build's printf does not accept | [`fmt`](../fmt/README.md)'s own |

## Implementing a sink

1. Define your state and the two operations of `el_log_sink_ops`. `begin`
   lends the `el_buf` the message is written into; `end` receives the
   record with its message in that buffer, and whether it was cut. The
   "called with" and "must" lists in `log.h` are the whole contract: the
   record is valid, you keep the message where it is or copy it, and you
   return `EL_OK` only for a record you took whole.
2. Put it in a `static const` table and write a function that returns a
   handle.
3. Write a fixture in C and instantiate the contract suite with it.
4. Write tests for what only your sink promises.

[examples/backend_log_tee](../../examples/backend_log_tee) is a complete
sink done this way: one that gives every record to two others. It is also
why the operations return a status although a log call does not, and it
shows the price of the buffer: a sink that only passes records on still
needs one of its own to lend.

## The contract suite

`ElLogContract` checks what the interface promises, against any sink a
fixture describes:

```cpp
#include <elemental/log_contract.hpp>

#include "my_fixture.h"

INSTANTIATE_TEST_SUITE_P(MySink, ElLogContract, testing::Values(&my_fixture));
```

Where a sink delivers is its own business, so the fixture also says how to
look there: how many records arrived, and the message of each. Those are
required. What else can be read back is optional, and a fixture leaves out
what its destination does not hold exactly:

| Capability | With it the suite checks |
|---|---|
| `level`, `tag` | Each arrives as written |
| `time` | The timestamp and its frequency arrive exactly |
| `sink_full_after` | A record the sink cannot take is refused with a status, is not delivered in whole or in part, and the earlier ones are intact |

Without any capability, the suite also gives the sink messages through a
logger's `el_log_printf`, formatted straight into the buffer the sink
lends.

| Implementation | Tests run | Skipped, and why |
|---|---|---|
| Memory sink | 10 | 0 |
| Writer sink, over an `el_buf` | 9 | 1: a line holds seconds and microseconds, not ticks and their frequency |
| Deferred sink, drained into a memory sink | 10 | 0 |
| Tee example, over two memory sinks | 9 | 1: it has no storage that fills up with records |

Deliberately broken sinks were used to check the suite itself; the results
are in [docs/build.md](../../docs/build.md#what-is-verified).

## Configuration

| Option | Default | |
|---|---|---|
| `LOG` | on | The module. Selects `FMT`, `TIME` and `IO`. |
| `LOG_DEFERRED` | on where `RING` is | The deferred sink, the one part of `log` that needs `ring`. Off, `log` builds and links no ring. |

`EL_LOG_CUTOFF` is not a Kconfig option.

## Tests

| Test | What it checks |
|---|---|
| `log.contract.sinks.*` | The contract suite against the memory sink and the writer sink. |
| `log.unit.*` | Level names; what a record holds, begun without its message and ended with it; that nothing happens below a logger's minimum; that the clock is read once per record made and its frequency once at set-up; that a record refused at begin or end is lost without further effect; that a message is copied, or formatted, into the sink's buffer after what the sink put there, and cut where that buffer ends with the sink told; that nothing is formatted below the minimum or when the sink does not begin the record; and, in a file compiled with `EL_LOG_CUTOFF` set, that cut calls neither log nor evaluate their arguments. |
| `log.sinks.*` | The writer sink's line for every field, the cut line at every storage size, a printf message formatted into the line and cut by it, one write per line, a failing and a stalling writer, the lost count; the memory sink's fields, order, exact fit, a printf message formatted where it is kept, lost records, clear, and storage of any alignment. |
| `log.sinks.grown.*` | The writer sink over a grown buffer: a 1 000-byte message goes out whole, through `el_log_sink_write` and through `EL_LOG_WARN`, and the buffer keeps its capacity; an allocator that refuses cuts the line, which still ends in a newline, and counts it lost; one that gives nothing loses every record. With `IO_ALLOC`. |
| `log.contract.deferred.*` | The contract suite against the deferred sink, drained into a memory sink each time the suite looks. With `LOG_DEFERRED`. |
| `log.deferred.*` | The deferred sink, one context driving both sides: a record waits in the ring until drained and arrives with every field, each with its own clock's frequency; order and the count a drain returns; a record across the end of the ring's storage, a hundred laps; exact fit in the ring and in the record storage; a record too long for the record storage, and one the ring has no room for, lost whole and counted with nothing of them in the ring; a destination that refuses a record at begin or at end still gets the next one whole; a message longer than the destination's buffer arrives cut, and is said to be; the message is read into the buffer the destination lends; a drain takes only what was in the ring when it began, against a destination that logs back into the same sink; printf messages formatted in the record storage; storage of any alignment. |
| `log.deferred.stress.*` | A thread logs 100 000 records while another drains, through rings of 200 and 4096 bytes: every record that arrives is whole and later than the one before, and every record logged arrived or was counted as lost. Host only; the run that can fail on a data race is the `tsan` preset's. |
| `log.posix.*` | Two sinks in one program: a memory sink, and a pipe through the writer sink, timed by the system's clock. Host only. |
| `log.cutoff_check.*` | For each cut-off, as C11 and C++17, at `-O0` and `-Os`: the object file holds the format strings of the calls that are kept and none of those that are cut, and with everything cut it does not refer to `el_log_printf`. Host only. |
| `log.violations.*`, `log.violations.deferred.*` | Each documented contract violation stops the program. Death tests, host only. |

## Known gaps

- **The deferred sink loses a record that does not fit, whole.** A
  handler's record may be the one most worth keeping short; logging it cut
  is an open question of the [ring proposal](../../docs/proposals/ring.md#open-questions).
  It has run from an interrupt handler under an emulator only.
- **A cut line does not say so.** The sink is told a message was cut and
  counts the record as lost, but the writer sink's line carries no mark.
- **No source location** in a record, and no sink into Zephyr's own
  logging: both were in `mlog` and wait for a consumer.
- **A failure with context** ([ADR 0019](../../docs/adr/0019-failure-context.md),
  open) would be logged as text; nothing here formats one.
