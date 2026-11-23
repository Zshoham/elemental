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
| `el_log_level`, `el_log_record`, `el_log_sink`, `el_log_sink_write`, `el_logger`, `el_logger_init`, `el_logger_set_level`, `el_logger_off`, `el_log_write`, `el_log_compose_fn`, `el_log_compose` | The contract: not variadic, no global | [`elemental/log.h`](include/elemental/log.h) |
| `EL_LOG_DEBUG`, `EL_LOG_INFO`, `EL_LOG_WARN`, `EL_LOG_ERROR`, `EL_LOG`, `el_log_printf`, `el_log_vprintf`, `EL_LOG_CUTOFF`, `EL_LOG_KEPT` | The C call site | [`elemental/log_printf.h`](include/elemental/log_printf.h) |
| `el_log_writer_sink` | A sink: lines of text to an `el_writer` | [`elemental/log_writer.h`](include/elemental/log_writer.h) |
| `el_log_mem_sink`, `EL_LOG_MEM_SINK_STORAGE_SIZE` | A sink: records kept in caller memory | [`elemental/log_mem.h`](include/elemental/log_mem.h) |
| `el_log_deferred_sink`, `EL_LOG_DEFERRED_SINK_STORAGE_SIZE` | A sink: records handed to another context through a queue it keeps in the caller's storage, and the function that drains them | [`elemental/log_deferred.h`](include/elemental/log_deferred.h) |
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
- **Message length.** A log call has no buffer of its own. `EL_LOG_*`,
  `el_log_printf` and `el_log_compose` write the message straight into the
  buffer the sink lends: the writer sink's line, the memory sink's
  storage, the deferred sink's record area. That buffer is
  the only limit, and a grown one has none but its allocator. A message
  it cannot take whole is cut by `fmt`'s item rule (`el_log_write`: at a
  byte), and the sink counts the record as lost.
- **Formats** are what [`el_fmt_printf`](../fmt/README.md) accepts in the
  build. Add `elemental_check_formats(app)` to check literal formats in
  `EL_LOG`, all four level macros, and calls to `el_log_printf` and
  `el_log_vprintf`. Calls below `EL_LOG_CUTOFF` are checked too. See
  [build.md](../../docs/build.md#checking-printf-formats).

Other languages call `el_log_write(logger, level, message, len)`, which is
not variadic, or `el_log_compose`, below.

### A message of the application's own

A message that is not a format string, such as a value the application
already prints with its own function, is written by a function of the
caller's, into the buffer the sink lends:

```c
static el_status point_message(void *ctx, el_buf *message)
{
    const point *const p = ctx;
    EL_TRY(el_fmt_printf(message, "at "));
    return point_print(message, p); /* the application's formatter, onto an el_buf */
}

el_log_compose(&log, EL_LOG_LEVEL_INFO, point_message, &position);
```

`el_log_compose` makes the record as `el_log_printf` does: nothing happens
below the logger's level, so the function is not called and no clock is
read; if the sink refuses the record when it begins it, the function is not
called either. A sink that refuses only when it ends the record (a memory
sink with room for the record's fields but not its whole message, a writer
that fails) does so after the function has run, side effects included.
The library begins the record and ends it, on the sink that began it even
if the function changed the logger, so the function has no scratch buffer
to size and no record to leave open. It returns `EL_OK` for a whole
message and any other status, an application's negative one included, for
a cut one, which the sink is told as `EL_ERR_NO_SPACE`. It runs in the
caller's context, on its stack: from an interrupt handler it must not
block or allocate, whatever the sink and clock allow. It must not log to
the sink it is writing into: that is a contract violation, and the three
sinks here check it, as the writer sink also checks its writer logging
into it. It may take back what it appended, down to where it started; the
check of that is on the length it returns with, so a function that cuts
below its start and appends again is not caught. To cut a composed
message off with the `EL_LOG_*` calls, write the call under
`if (EL_LOG_KEPT(EL_LOG_LEVEL_DEBUG)) { ... }`.

### A logger that logs nothing

A component that must be handed a logger, in a program that wants none of
its records, is given `el_logger_off()`: its minimum is
`EL_LOG_LEVEL_OFF`, so every call returns at once, before any clock or
sink. It is a constant for the whole program. A copy of it with a lowered
level loses every record instead of trapping; its sink refuses them at
begin.

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
its format is checked either way. Only the macros honor it. A message
composed with `el_log_compose` has no macro: its call goes under
`if (EL_LOG_KEPT(EL_LOG_LEVEL_DEBUG))`, with the level as a constant, so
below the cut-off the call is not in the program. An optimizing compiler
then drops a `static` compose
function nothing else calls, and its text; one with external linkage, or
one still called at a level that is kept, stays.

### Choosing a sink

| Sink | A record becomes | Storage | For |
|---|---|---|---|
| `el_log_writer_sink` | One line, passed to an `el_writer` with one `el_write_all` | An `el_buf` the caller lends it, fixed or grown; a line it cannot hold is cut and still ends in a newline | A console, a file, a descriptor: a writer that does not stall |
| `el_log_mem_sink` | A record kept until cleared | The caller's bytes, where the message is formatted and kept; a record that does not fit is lost whole | Tests of code that logs; looking afterwards |
| `el_log_deferred_sink` | A record in a queue the sink keeps in the caller's storage, until another context drains it into a sink that writes | One block: the area one record is built in, then the queue; a record that does not fit either is lost whole | An interrupt handler; a thread that must not wait for a console |
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
each record is copied into a queue the sink keeps, and a thread hands the
records on to the sink that writes them, whenever it gets round to it.

```c
#define MESSAGE_MAX 64 /* the longest message the handler logs */
#define WAITING 8      /* records that may wait for the thread */

static uint8_t deferred_storage[EL_LOG_DEFERRED_SINK_STORAGE_SIZE(WAITING, MESSAGE_MAX)];
static el_log_deferred_sink deferred;
static el_logger isr_log;

el_log_deferred_sink_init(&deferred, deferred_storage, sizeof(deferred_storage), MESSAGE_MAX);
el_logger_init(&isr_log, el_log_deferred_sink_as_sink(&deferred), el_zephyr_clock(), "uart2",
               EL_LOG_LEVEL_INFO);

/* In the handler: the call site every other caller writes. */
EL_LOG_WARN(&isr_log, "%u bytes dropped, the queue is full", dropped);

/* In a thread: hand what has arrived to the sink that writes. */
el_log_deferred_sink_drain(&deferred, el_log_writer_sink_as_sink(&console));
```

- **Three sizes are yours.** How many records may wait, how long a message
  may be, and the line of the sink that finally writes. The first two are
  one block of storage: `EL_LOG_DEFERRED_SINK_STORAGE_SIZE(waiting,
  message_max)` is its size, the area one record is built in and then the
  queue. `el_log_deferred_sink_record_size(len)` is what one record takes
  in either, as a function, for records of different lengths; on a 32-bit
  target that is 24 bytes and the message. The sink keeps the queue: the
  caller sets up no ring, and `el_log_deferred_sink_pending_bytes()` says
  how many bytes of records wait. The block must be at least one record
  long (`storage_cap` of 0 is a sink that loses everything), which also
  catches `storage_cap` and `message_max` given in the wrong order. A block
  of exactly one record, `EL_LOG_DEFERRED_SINK_STORAGE_SIZE(0, m)`, has no
  queue, and loses every record too.
- **A record goes in whole or not at all.** One that does not fit, in the
  record area or in the room the queue has left, is lost and counted
  (`el_log_deferred_sink_lost()`, readable from any context). The handler
  never waits. Nothing is formatted for a record the queue has no room to
  begin.
- **Records stay records.** The time was read where the record was logged,
  by the logger's clock, which must be one that may be read there; the
  sink that drains formats the line.
- **A drain is bounded.** It hands on what was in the queue when it began
  and returns how many; records logged meanwhile wait for the next one. It
  calls the destination sink, so it runs where that sink may: a thread.
- **One context logs through it at a time,** and one drains. A thread and
  a handler that both log through one deferred sink are two: mask the
  interrupt around the thread's calls, or give each its own sink and storage.

[examples/uart_ring](../../examples/uart_ring) logs from an interrupt
handler this way on Zephyr, a `k_timer`'s
([isr_tick.c](../../examples/uart_ring/isr_tick.c)); its UART receive
handler only wakes the thread. What its run under an emulator shows, and
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

`EL_LOG_MEM_SINK_STORAGE_SIZE(count, message_max)` says how many bytes of
storage keep that many records, for a static array; `el_log_mem_sink_record_size(len)`
is one record's bytes, as a function. A record's message is not terminated:
compare its length and bytes.

### Failures and contract violations

| Reported, never asserted | How |
|---|---|
| A sink that cannot take a record | The sink's begin or end returns its status; through a logger the record is lost and the sink counts it |
| A line the writer sink's buffer cannot hold: fixed storage too small, or an allocator that refused | `EL_ERR_NO_SPACE`; the cut line went out |
| A writer that stalls or fails under the writer sink | The writer's status; the stream may hold part of the line |
| A record that does not fit the memory sink | `EL_ERR_NO_SPACE`; nothing kept, nothing overwritten |
| A record that does not fit the deferred sink's record area, or the room its queue has left | `EL_ERR_NO_SPACE`; nothing of it is in the queue |
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
| A null deferred sink; deferred sink storage above 0 bytes and below one record of `message_max` (which is how swapped `storage_cap` and `message_max` are caught; with assertions off, a sink that loses every record); a deferred queue, the storage after that one record, of more than `SIZE_MAX / 2` bytes (the ring's own limit); draining into a handle that was never made | A calling bug |
| A null compose function, even when the record would not be made | A calling bug |
| A compose function that returns with the buffer shorter than it was when it was called. One that truncates below that length and appends again breaks the same rule, but is not caught: only the length it returns with is checked | A calling bug |
| Logging, from inside a compose function, into the sink it is writing into, or, from the writer sink's own writer, into that writer sink: the writer, memory and deferred sinks each check that no record is open when one begins, and the writer sink keeps its record open until its writer has returned | A calling bug: the second record would be built over the first |
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
needs one of its own to lend. A sink that holds a record open from
`begin` to `end` can check, as the three here do, that none is open when
one begins: a compose function that logs into its own sink would corrupt
it silently. The tee does not: a check is an `EL_ASSERT`, which is for
sources inside this repository (`elemental/assert.h` needs the library's
private configuration).

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
required. `reset` is optional: leave it out when the next `sink` request
sets everything up afresh, as the memory sink's and the tee's fixtures do.
What else can be read back is optional, and a fixture leaves out
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
| `log.unit.*` | Level names; what a record holds, begun without its message and ended with it; that nothing happens below a logger's minimum; that the clock is read once per record made and its frequency once at set-up; that a record refused at begin or end is lost without further effect; that a message is copied, or formatted, into the sink's buffer after what the sink put there, and cut where that buffer ends with the sink told; that nothing is formatted below the minimum or when the sink does not begin the record; that a logger whose minimum is `EL_LOG_LEVEL_OFF` reads no clock and begins nothing through any of its three calls at any level; for `el_log_compose`: that a message is written into the buffer the sink lent after what it put there, that the clock is read once per record and the function is called only between begin and end, not below the minimum and not for a record the sink refuses at begin, that any status but `EL_OK` (an application's negative one, `INT32_MIN`) reaches the sink as `EL_ERR_NO_SPACE` with what was written kept, that a message the buffer cannot take is cut and the sink told, that taking back what was appended down to the starting length is allowed, that a compose function may log to another sink, and that the record ends on the sink that began it when the function changes the logger; for `el_logger_off()`: that it is one constant with a minimum of `EL_LOG_LEVEL_OFF`, that nothing is composed or formatted through it, and that a copy with its level lowered loses every record and reads a clock of 0; and, in a file compiled with `EL_LOG_CUTOFF` set, that cut calls neither log nor evaluate their arguments, and that `EL_LOG_KEPT` is a constant that guards a composed message. |
| `log.sinks.*` | The writer sink's line for every field, the cut line at every storage size, a printf message formatted into the line and cut by it, one write per line, a failing and a stalling writer, the lost count; the memory sink's fields, order, exact fit, a printf message formatted where it is kept, lost records, clear, and storage of any alignment; a refused record leaves no record open; `EL_LOG_MEM_SINK_STORAGE_SIZE` equal to the function's size for n records, usable as an array size, and storage of exactly that size keeps n records and loses the next. |
| `log.sinks.grown.*` | The writer sink over a grown buffer: a 1 000-byte message goes out whole, through `el_log_sink_write` and through `EL_LOG_WARN`, and the buffer keeps its capacity; an allocator that refuses cuts the line, which still ends in a newline, and counts it lost; one that gives nothing loses every record. With `IO_ALLOC`. |
| `log.contract.deferred.*` | The contract suite against the deferred sink, drained into a memory sink each time the suite looks. With `LOG_DEFERRED`. |
| `log.deferred.*` | The deferred sink, one context driving both sides: a record waits in the queue until drained and arrives with every field, each with its own clock's frequency; order and the count a drain returns; a record across the end of the queue's storage, a hundred laps; exact fit in the queue and in the record area; a record too long for the record area, and one the queue has no room for, lost whole and counted with nothing of them in the queue; `message_max` 0: empty records kept and drained, others lost; storage of exactly one record, and no storage: every record lost; a destination that refuses a record at begin or at end still gets the next one whole; a message longer than the destination's buffer arrives cut, and is said to be; the message is read into the buffer the destination lends; a drain takes only what was in the queue when it began, against a destination that logs back into the same sink; printf messages formatted in the record area; storage of any alignment; `EL_LOG_DEFERRED_SINK_STORAGE_SIZE` equal to the function's size for n records and the area, usable as an array size, and storage of exactly that size holding n records of m bytes and losing the next, with `el_log_deferred_sink_pending_bytes` following. |
| `log.deferred.stress.*` | A thread logs 100 000 records while another drains, through queues of 200 and 4096 bytes: every record that arrives is whole and later than the one before, and every record logged arrived or was counted as lost. Host only; the run that can fail on a data race is the `tsan` preset's. |
| `log.deferred.assert_off.*` | With assertions compiled out only: storage of more than 0 and less than one record, and `storage_cap` and `message_max` swapped, lose every record and write no byte of the storage. |
| `log.posix.*` | Two sinks in one program: a memory sink, and a pipe through the writer sink, timed by the system's clock. Host only. |
| `log.cutoff_check.*` | For each cut-off, as C11 and C++17, at `-O0` and `-Os`: the object file holds the format strings of the calls that are kept and none of those that are cut, and with everything cut it does not refer to `el_log_printf` or `el_log_compose`. The same for the text of a composed message, whose compose function is separate: it is absent only from the optimized objects, because a compiler that does not optimize keeps an unused static function. Each level's compose call is also compiled alone, so that at either optimization the object refers to `el_log_compose` exactly when that level is kept. Host only. |
| `log.violations.*`, `log.violations.deferred.*` | Each documented contract violation stops the program, including a null compose function, a truncate below the message's start, logging into the sink being written to from inside a compose function, for each of the three sinks, a writer sink's writer logging into it before it has written the line, a null deferred sink given to any of its functions, and a deferred sink given storage between 0 and one record, or its two sizes swapped. Death tests, host only. |

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
