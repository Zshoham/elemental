# log

Logging for applications: a logger with a level, a tag and a clock; a sink
interface that decides where records go; a sink that writes lines to a
writer and one that keeps records in memory; printf-style call sites with a
compile-time cut-off; and a contract suite for sinks.

Elemental's own modules never log. This is for applications, and for
backends kept outside the repository.

| | |
|---|---|
| Stability | Experimental ([policy](../../docs/process/api-stability.md)) |
| Targets | `elemental::log`, `elemental::log_contract` |
| Depends on | `status`, `io`, `fmt`, `time`. Runtime: `memcpy`. |
| Allocates | No |
| Kconfig | `LOG` |

## What is here

| Piece | Kind | Header |
|---|---|---|
| `el_log_level`, `el_log_record`, `el_log_sink`, `el_log_sink_write`, `el_logger`, `el_logger_init`, `el_logger_set_level`, `el_log_write` | The contract: not variadic, no global | [`elemental/log.h`](include/elemental/log.h) |
| `EL_LOG_DEBUG`, `EL_LOG_INFO`, `EL_LOG_WARN`, `EL_LOG_ERROR`, `EL_LOG`, `el_log_printf`, `el_log_vprintf`, `EL_LOG_CUTOFF` | The C call site | [`elemental/log_printf.h`](include/elemental/log_printf.h) |
| `el_log_writer_sink` | A sink: lines of text to an `el_writer` | [`elemental/log_writer.h`](include/elemental/log_writer.h) |
| `el_log_mem_sink` | A sink: records kept in caller memory | [`elemental/log_mem.h`](include/elemental/log_mem.h) |
| The log sink contract suite | GoogleTest tests, shipped as source | [`elemental/log_contract.hpp`](contract/include/elemental/log_contract.hpp), fixture in [`elemental/log_contract.h`](contract/include/elemental/log_contract.h) |

Start with `log.h`. The design, the examples that shaped it, and what was
taken from the owner's earlier `mlog` prototype are in the
[proposal](../../docs/proposals/log.md); the cut-off is
[ADR 0021](../../docs/adr/0021-log-cutoff.md), and why a message is written
into the sink's buffer is [ADR 0022](../../docs/adr/0022-log-sink-lends-the-buffer.md).

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
  `el_log_writer_sink_lost()`, `el_log_mem_sink_lost()`.
- **Message length.** A log call has no buffer of its own. `EL_LOG_*` and
  `el_log_printf` format the message straight into the buffer the sink
  lends: the writer sink's line, the memory sink's storage. That buffer is
  the only limit, and a grown one has none but its allocator. A message
  it cannot take whole is cut by `fmt`'s item rule (`el_log_write`: at a
  byte), and the sink counts the record as lost.
- **Formats** are what [`el_fmt_printf`](../fmt/README.md) accepts in the
  build.

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
| A message longer than the sink's buffer can take | Cut, and the sink is told: the writer sink writes the cut line, the memory sink keeps nothing; both count it lost |

| Contract violation, asserted | |
|---|---|
| A null logger, record, tag, sink object, or writer sink line buffer; a handle that was never made, or one with an operation missing | A calling bug |
| A sink that begins a record without lending a buffer | A broken sink |
| A null message with a nonzero length, even when the record would not be made | A calling bug |
| A record's level of `EL_LOG_LEVEL_OFF` or above; a minimum above it | Levels are constants, not input |
| A record with a frequency of 0 | A broken logger or sink driver |
| Reading a memory sink's record that is not there | A calling bug |
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
| Tee example, over two memory sinks | 9 | 1: it has no storage that fills up with records |

Deliberately broken sinks were used to check the suite itself; the results
are in [docs/build.md](../../docs/build.md#what-is-verified).

## Configuration

| Option | Default | |
|---|---|---|
| `LOG` | on | The module. Selects `FMT`, `TIME` and `IO`. |

`EL_LOG_CUTOFF` is not a Kconfig option.

## Tests

| Test | What it checks |
|---|---|
| `log.contract.sinks.*` | The contract suite against the memory sink and the writer sink. |
| `log.unit.*` | Level names; what a record holds, begun without its message and ended with it; that nothing happens below a logger's minimum; that the clock is read once per record made and its frequency once at set-up; that a record refused at begin or end is lost without further effect; that a message is copied, or formatted, into the sink's buffer after what the sink put there, and cut where that buffer ends with the sink told; that nothing is formatted below the minimum or when the sink does not begin the record; and, in a file compiled with `EL_LOG_CUTOFF` set, that cut calls neither log nor evaluate their arguments. |
| `log.sinks.*` | The writer sink's line for every field, the cut line at every storage size, a printf message formatted into the line and cut by it, one write per line, a failing and a stalling writer, the lost count; the memory sink's fields, order, exact fit, a printf message formatted where it is kept, lost records, clear, and storage of any alignment. |
| `log.sinks.grown.*` | The writer sink over a grown buffer: a 1 000-byte message goes out whole, through `el_log_sink_write` and through `EL_LOG_WARN`, and the buffer keeps its capacity; an allocator that refuses cuts the line, which still ends in a newline, and counts it lost; one that gives nothing loses every record. With `IO_ALLOC`. |
| `log.posix.*` | Two sinks in one program: a memory sink, and a pipe through the writer sink, timed by the system's clock. Host only. |
| `log.cutoff_check.*` | For each cut-off, as C11 and C++17, at `-O0` and `-Os`: the object file holds the format strings of the calls that are kept and none of those that are cut, and with everything cut it does not refer to `el_log_printf`. Host only. |
| `log.violations.*` | Each documented contract violation stops the program. Death tests, host only. |

## Known gaps

- **No sink for an interrupt handler.** A log call is as safe there as its
  sink and clock. The deferred sink, which copies records into a ring for
  a task to write, is [M5](../../docs/roadmap.md#m5--ring-and-interrupt-context).
- **A cut line does not say so.** The sink is told a message was cut and
  counts the record as lost, but the writer sink's line carries no mark.
- **No source location** in a record, and no sink into Zephyr's own
  logging: both were in `mlog` and wait for a consumer.
- **A failure with context** ([ADR 0019](../../docs/adr/0019-failure-context.md),
  open) would be logged as text; nothing here formats one.
