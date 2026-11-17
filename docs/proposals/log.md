# Proposal: log

Status: Accepted (2026-11-16), with the owner's [decisions](#decisions),
and implemented as experimental: [modules/log](../../modules/log/README.md).
Author, date: design pass for M4, 2026-11-16; accepted the same day

Milestone: [M4](../roadmap.md#m4--time-and-logging). Sketch it starts
from: [architecture.md §6.4](../architecture.md#64-logging--proposed).
Plan entry: [module-plan.md](../module-plan.md#named-by-the-owner). It
depends on the [time proposal](time.md) and on
[ADR 0020](../adr/0020-time-representation.md); the compile-time cut-off is
[ADR 0021](../adr/0021-log-cutoff.md), accepted with this document.

This is also the review of the owner's earlier `mlog` prototype that the
roadmap asks for: [what was taken and what was left](#what-mlog-has-and-what-was-taken).

Labels as in [architecture.md](../architecture.md): **Source fact** is
something read in a cited source ([Sources](#sources)), **Measured** is a
number produced in this pass with its method, and everything else is a
recommendation. The examples were compiled and run against a prototype
before the module existed ([Verification](#verification)); what the module
itself was run on is in [build.md](../build.md#what-is-verified).

## As accepted and implemented

The proposal below is kept as written for review. What changed between it
and [modules/log](../../modules/log/README.md):

- **`el_log_mem_sink_record_size(message_len)`** is new: the bytes one
  record takes, so that the storage can be sized, and so that a contract
  fixture can make a sink that is full after a given number of records.
- **The level of a call is checked once, in `el_log_sink_write`.** A level
  that is not a record's level is never below a logger's minimum, so every
  such call reaches that check; a second one in `el_log_write` could not be
  told apart by any test and was removed. A null message with a length is
  checked in `el_log_write` as well, so that it is caught even when the
  record is not made.
- **The contract suite has nine tests.** Its fixture reads back what a
  sink delivered: the count and each message are required; `level`, `tag`,
  `time`, and `sink_full_after` are optional capabilities. Every record the
  suite writes has the tag `contract` and a printable message, so that a
  sink that produces text can be sized and read back.
- **The writer sink's statuses** are as the header says: `EL_OK`;
  `EL_ERR_NO_SPACE` for a line cut to its storage, which still goes out
  ending in a newline; or the writer's status.
- **The consumer example is `el_cat`**
  ([examples/consume_installed](../../examples/consume_installed/main.c)),
  which logs what goes wrong to standard error. The backend example is
  [examples/backend_log_tee](../../examples/backend_log_tee), the tee sink
  below.
- **The cut-off check** runs as C11 and as C++17, at `-O0` and `-Os`, for
  every cut-off (`log.cutoff_check.*`), and a file of the unit tests is
  compiled with a cut-off to show that cut calls do not evaluate their
  arguments.
- **`configs/variant_defconfig` sets `LOG_MESSAGE_MAX=48`**, so the
  `sanitize` preset and the Zephyr `variant` scenario test a limit other
  than the default.
- *Later (2026-11-17):* **the sink lends the buffer the message is written
  into** ([ADR 0022](../adr/0022-log-sink-lends-the-buffer.md)), reversing
  decision 3. `el_log_sink_ops` has `begin` and `end` in place of `write`,
  `el_log_printf` formats into the sink's buffer, and `LOG_MESSAGE_MAX` is
  gone, with its line in `variant_defconfig`. The writer sink takes its
  line as an `el_buf *`, fixed or grown. The code below, the tee sink's
  included, is the design as it was proposed; the headers are the current
  contract.

## Problem

An application built on Elemental has no way to say what it is doing that
works on both a host and a microcontroller, goes where the application
decides, and can be read back by a test. Today the examples call `fprintf`
on the host and nothing on a target.

Elemental's own modules never log
([conventions.md](../conventions.md#errors), rule 6). This module is for
applications, and for backends kept outside the repository.

## What `mlog` has, and what was taken

*Source fact:* `mlog`, the owner's working copy (`include/mlog/mlog.h`,
`src/mlog.c`, the `membuf` and `zephyr` backends), read in full on
2026-11-16. Not a repository, so there is no revision to cite.

| `mlog` | Taken | In Elemental |
|---|---|---|
| The front end knows nothing about where logs go; a backend is a table of functions and a context pointer | Yes | `el_log_sink`, a handle |
| A logger is `{backend, ctx, min_level}`, a public struct with no allocation | Yes | `el_logger`: a sink, a clock, a tag, a minimum level |
| The core does no allocation, no I/O, and no locking | Yes | The same |
| Runtime filtering by the logger's minimum level | Yes | `level_min`, checked before anything is formatted |
| Compile-time stripping, `MLOG_STATIC_LEVEL` | Yes | `EL_LOG_CUTOFF` ([ADR 0021](../adr/0021-log-cutoff.md)) |
| `MLOG_INFO_TO(logger, fmt, ...)` and its five siblings | Yes | `EL_LOG_INFO(logger, fmt, ...)` and three siblings, in a header of their own |
| Modules as strings, `MLOG_MODULE` at the call site | Yes, moved | The logger's `tag`. One logger per part of the program, each with its own level |
| `is_enabled(ctx, level, module)`, per-module filtering in the backend | No | A logger per module with its own `level_min` does this with no hook |
| `write` receives `fmt` and a `va_list`, so the backend decides when to format | No | The contract is not variadic ([conventions.md](../conventions.md#abi), rule 5): a sink receives finished bytes. Cost: a sink cannot put off formatting, as Zephyr's deferred mode does |
| Six levels and `OFF` | Four and `OFF` | Debug, info, warn, error. Trace and fatal have no consumer; nothing here aborts |
| The record's `file`, `line`, `func` | No | Each is a string in the image per call site; the message says what happened. No consumer has asked |
| A process-wide default logger; `MLOG_INFO(fmt, ...)` with no logger | No | It is global state. An application that wants one writes it in three lines ([below](#a-default-logger-is-the-applications)) |
| A null logger is a no-op | No | A null object is a contract violation. A logger at `EL_LOG_LEVEL_OFF` logs nothing |
| `flush` | No | No sink here holds anything back ([Flush](#flush-adr-0014)) |
| `close` | No | An interface has no destroy operation ([conventions.md](../conventions.md#ownership-and-lifetime), rule 4) |
| `mlog_level_name()` | Yes | `el_log_level_name()`, lower case as `el_status_name()` |
| No timestamp; the backend adds one | Changed | The logger reads the clock it was given, once per record |
| `membuf`: lines into a caller buffer, records that do not fit are dropped and counted | Yes, as records | `el_log_mem_sink` keeps records, not text, so a test reads a level and a time without parsing. Lines in memory are the writer sink over an `el_buf` |
| The Zephyr backend, into Zephyr's logging | Not in this pass | A sink a port can write; it formats eagerly in `mlog` too |
| The spdlog and CPython backends, the Python module | No | Outside Elemental's scope ([roadmap.md](../roadmap.md#deferred-deliberately)) |
| Thread safety of `write` is the backend's | Yes | The sink's. The logger is immutable once set up |

## Usage examples

### Example 1: a program that logs to standard error

```c
#include <elemental/io_posix.h>
#include <elemental/log.h>
#include <elemental/log_printf.h>
#include <elemental/log_writer.h>
#include <elemental/time_posix.h>

/* A component logs through the logger it is given. */
static int link_connect(const el_logger *log, const char *peer, int attempts)
{
    for (int attempt = 1; attempt <= attempts; attempt++) {
        EL_LOG_DEBUG(log, "dialling %s", peer);
        EL_LOG_WARN(log, "%s did not answer, attempt %d of %d", peer, attempt, attempts);
    }
    EL_LOG_ERROR(log, "gave up on %s", peer);
    return -1;
}

int main(void)
{
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
    return link_connect(&log, "10.0.0.7", 2) == 0 ? 0 : 1;
}
```

It printed:

```
[ 33601.929899] I link: starting, 2 attempts
[ 33601.929915] W link: 10.0.0.7 did not answer, attempt 1 of 2
[ 33601.929920] W link: 10.0.0.7 did not answer, attempt 2 of 2
[ 33601.929924] E link: gave up on 10.0.0.7
```

Three objects are set up, and each is something the application decides:
where bytes go, how a record becomes a line, and whose records these are.
The debug calls did nothing, because the logger's minimum is info; built
with `EL_LOG_CUTOFF=EL_LOG_LEVEL_INFO` they are not in the program.

### Example 2: two sinks, and a test that reads what was logged

The awkward case: the code under test logs, the test must see what and
when, and whoever runs the test wants to read it too. Time is the test's,
so the timestamps are exact.

```c
/* The code under test. Each failed attempt costs a second. */
static int connect_with_retry(const el_logger *log, el_manual_clock *clock, int attempts)
{
    for (int attempt = 1; attempt <= attempts; attempt++) {
        el_manual_clock_advance(clock, 1000);
        EL_LOG_WARN(log, "attempt %d of %d failed", attempt, attempts);
    }
    EL_LOG_ERROR(log, "gave up after %d attempts", attempts);
    return -1;
}

int main(void)
{
    el_manual_clock clock;
    el_manual_clock_init(&clock, 1000);

    /* Sink one: records, kept in memory for the test to read. */
    uint8_t storage[512];
    el_log_mem_sink kept;
    el_log_mem_sink_init(&kept, storage, sizeof(storage));

    /* Sink two: lines on standard error, for whoever runs the test. */
    el_posix_fd err;
    el_posix_fd_init(&err, STDERR_FILENO);
    char line_storage[96];
    el_buf line;
    el_buf_init(&line, line_storage, sizeof(line_storage));
    el_log_writer_sink shown;
    el_log_writer_sink_init(&shown, el_posix_fd_as_writer(&err), &line);

    /* Both, behind one logger. */
    tee_sink both;
    tee_sink_init(&both, el_log_mem_sink_as_sink(&kept), el_log_writer_sink_as_sink(&shown));
    el_logger log;
    el_logger_init(&log, tee_sink_as_sink(&both), el_manual_clock_as_clock(&clock), "net",
                   EL_LOG_LEVEL_WARN);

    EL_LOG_INFO(&log, "below the logger's level: no record");
    assert(connect_with_retry(&log, &clock, 3) == -1);

    assert(el_log_mem_sink_count(&kept) == 4);
    assert(el_log_mem_sink_lost(&kept) == 0);

    el_log_record record;
    el_log_mem_sink_get(&kept, 3, &record);
    assert(record.level == EL_LOG_LEVEL_ERROR);
    assert(record.timestamp == 3000 && record.ticks_per_second == 1000);
    assert(strcmp(record.tag, "net") == 0);
    static const char expected[] = "gave up after 3 attempts";
    assert(record.len == strlen(expected) && memcmp(record.message, expected, record.len) == 0);
    return 0;
}
```

It passed, and standard error showed:

```
[     1.000000] W net: attempt 1 of 3 failed
[     2.000000] W net: attempt 2 of 3 failed
[     3.000000] W net: attempt 3 of 3 failed
[     3.000000] E net: gave up after 3 attempts
```

**Friction found.** A record's message is not terminated, so the test
compares a length and bytes. That is the rule for every string with a
length ([conventions.md](../conventions.md#storage-size-and-alignment),
rule 4), and a terminator would cost every sink a byte per record.

## Backend example

A sink that gives every record to two others: `tee_sink` of Example 2, as
someone outside this repository would write it.

```c
#include <elemental/log.h>

typedef struct tee_sink {
    el_log_sink first;
    el_log_sink second;
} tee_sink;

static el_status tee_write(void *ctx, const el_log_record *record)
{
    const tee_sink *const tee = ctx;
    /* Both get the record, whatever the first one makes of it. */
    const el_status first = el_log_sink_write(tee->first, record);
    const el_status second = el_log_sink_write(tee->second, record);
    return first != EL_OK ? first : second;
}

static const el_log_sink_ops tee_ops = {.write = tee_write};

void tee_sink_init(tee_sink *tee, el_log_sink first, el_log_sink second)
{
    tee->first = first;
    tee->second = second;
}

el_log_sink tee_sink_as_sink(tee_sink *tee)
{
    const el_log_sink handle = {tee, &tee_ops};
    return handle;
}
```

One operation. This is also why it returns a status although a log call
does not: a sink that drives other sinks needs to know what they did.

## Why not something that exists

- **`fprintf(stderr, ...)`.** Not on a target, one destination, and a test
  cannot read it back.
- **Zephyr's logging.** Only on Zephyr, and its call sites are Zephyr's
  macros. A sink into it is a port's to write.
- **`el_fmt_printf` to a writer the application passes around.** It is
  what the writer sink does. What is missing is the level, the time, and
  one place that decides whether a record is made at all.
- **`mlog` itself, vendored.** Its contract is variadic, its default
  logger is a global, and it has no time. The table above is what it
  becomes under Elemental's rules.

## Dependencies and cost

| | |
|---|---|
| Elemental modules | `status`, `io`, `fmt`, `time`. `ring` from M5, for the deferred sink |
| Runtime symbols (freestanding) | `memcpy` (the memory sink). *Measured* |
| Allocation | None |
| Per-object storage | 32-bit: `el_logger` 28 bytes; `el_log_writer_sink` 16 and the caller's line buffer, an `el_buf` (16) with its storage; `el_log_mem_sink` 20 and the caller's storage, 24 bytes per record kept plus its message. *Measured* on 2026-11-17 for the Cortex-M0+ by the Zephyr SDK 1.0.1's GCC 14.3.0 (the writer sink was 28 while it held its `el_buf` itself) |
| Stack | `LOG_MESSAGE_MAX` bytes (128 by default) in `el_log_printf`, where the message is formatted. *Measured:* 296 bytes from `el_log_printf` to the writer sink's operation, before `fmt` and the writer |
| New status codes | None |

*Measured* on 2026-11-16: the prototype built for `nucleo_g0b1re_renode`
(Cortex-M0+) by the Zephyr SDK 1.0.1's GCC 14.3.0 with Zephyr's flags for
the board (`-Os`), assertions trapping. Text bytes per object, each linked
only if something in it is called:

| Object | Bytes | Holds |
|---|---|---|
| `log` | 306 | the logger, `el_log_write`, `el_log_sink_write`, level names |
| `log_printf` | 92 | `el_log_printf`, `el_log_vprintf` |
| `log_writer` | 339 | the writer sink |
| `log_mem` | 296 | the memory sink |

The same build ran on that board under Renode with a clock over Zephyr's
uptime (the [time proposal's](time.md#backend-example) backend example),
an **emulator run** on the modified model:

```
[     0.000000] I app: started
[     0.250300] W app: step 1, 2501 ticks later
[     0.500600] W app: step 2, 2503 ticks later
[     0.750900] W app: step 3, 2503 ticks later
[     0.751100] E app: a line that is too long for the ninety-six bytes of storage this sink wa
```

## Contracts

- **Ownership and lifetime:** the application owns the sink objects, the
  clock, the logger, and every buffer. A logger borrows its sink, clock,
  and tag for as long as it is used. A record is borrowed for the call:
  a sink copies the message bytes it keeps. The tag is the one field a
  sink may keep by pointer, because the logger's own borrow of it lasts
  as long as any record made from it is kept: in practice a string
  literal.
- **A log call cannot fail.** `el_log_write` and `el_log_printf` return
  nothing. A record the sink does not take is lost, and the sink counts
  it (`el_log_writer_sink_lost`, `el_log_mem_sink_lost`): detail stays
  with the object ([conventions.md](../conventions.md#errors), rule 6).
  A call site that had to handle a status would be a call site nobody
  writes.
- **A sink's write can fail**, and returns `el_status`. `EL_OK` means the
  whole record was taken. Anything else means it was lost; a sink that
  produces text may already have passed part of a line on.
- **The writer sink** builds each line in an `el_buf` the caller lends
  it, fixed or grown, and passes it on with one `el_write_all`: `EL_OK`;
  `EL_ERR_NO_SPACE` when the line was cut because the buffer could not
  hold it, fixed storage too small or an allocator that refused, and the
  cut line, still ending in a newline, went out; or the writer's status, with the stream possibly mid-line.
  It is for a writer that does not stall, such as a blocking descriptor.
  A stream that can stall wants the deferred sink of M5.
- **The memory sink** keeps a record whole or not at all:
  `EL_ERR_NO_SPACE` when it does not fit in what is left.
- **A message longer than `LOG_MESSAGE_MAX`** is cut by `fmt`'s item rule
  ([ADR 0015](../adr/0015-formatting-into-io-buffers.md)) and logged cut.
- **Time:** the logger reads its clock once for each record that passes
  its level, in `el_log_write`, and nowhere else. A sink never reads a
  clock: the record carries the reading and the clock's frequency
  ([ADR 0020](../adr/0020-time-representation.md)).
- **Levels:** a record's level is below `EL_LOG_LEVEL_OFF`. A logger
  whose minimum is `EL_LOG_LEVEL_OFF` makes no records.
- **The cut-off:** a call through the `EL_LOG_*` macros below
  `EL_LOG_CUTOFF` does not evaluate its arguments
  ([ADR 0021](../adr/0021-log-cutoff.md)).
- **Concurrency:** `el_log_write` and `el_log_printf` take a
  `const el_logger *` and keep no state, so one logger may be used from
  several contexts when its sink and clock may. The sinks here are one
  context at a time, like every Elemental object; a program that logs
  from two threads gives each its own sink or wraps one in its own lock.
- **Execution context:** a log call blocks only if the sink's writer
  does. From an interrupt handler: not in this milestone (M5, the
  deferred sink).
- **Reentrancy:** a sink's operation must not log through a logger that
  leads back to the same sink.
- **Contract violations**, asserted: a null logger, sink operation, tag,
  or record; a null message with a nonzero length; a level of
  `EL_LOG_LEVEL_OFF` or above on a record; `fmt`'s own for the format.
- **Exceptions to docs/conventions.md:** one.
  `EL_LOG_CUTOFF` makes a public macro's expansion depend on something
  outside the header ([ADR 0021](../adr/0021-log-cutoff.md)). The
  variadic `el_log_printf` is the allowed kind: a C convenience in its own
  header over a non-variadic core (ABI rule 5).

## Interface model

Sinks are handles ([ADR 0002](../adr/0002-interface-model.md), second
mechanism): several coexist, and a logger must not know which it has. The
logger is a plain object. Nothing is chosen at build time.

```c
/* elemental/log.h: the contract. Not variadic, no global. */
typedef uint8_t el_log_level;
enum {
    EL_LOG_LEVEL_DEBUG = 0,
    EL_LOG_LEVEL_INFO = 1,
    EL_LOG_LEVEL_WARN = 2,
    EL_LOG_LEVEL_ERROR = 3,
    EL_LOG_LEVEL_OFF = 4 /* not a record's level; as a minimum: log nothing */
};
const char *el_log_level_name(el_log_level level);

typedef struct el_log_record {
    el_ticks timestamp;        /* the logger's clock when the record was made */
    uint32_t ticks_per_second; /* that clock's frequency, at least 1 */
    el_log_level level;        /* below EL_LOG_LEVEL_OFF */
    const char *tag;           /* the logger's tag: NUL-terminated, never null */
    const char *message;       /* `len` bytes, not terminated */
    size_t len;
} el_log_record;

typedef struct el_log_sink_ops {
    /*
     * Take one record.
     *
     * Called with: record not null, its fields as described above;
     *       message not null unless len is 0.
     * Must: return EL_OK having taken the whole record, or another status,
     *       and then the record is lost. Copy what it keeps of `message`:
     *       it is not valid after the call.
     */
    el_status (*write)(void *ctx, const el_log_record *record);
} el_log_sink_ops;

typedef struct el_log_sink {
    void *ctx;
    const el_log_sink_ops *ops;
} el_log_sink;

EL_NODISCARD el_status el_log_sink_write(el_log_sink sink, const el_log_record *record);

void el_logger_init(el_logger *logger, el_log_sink sink, el_clock clock, const char *tag,
                    el_log_level level_min);
void el_logger_set_level(el_logger *logger, el_log_level level_min);
void el_log_write(const el_logger *logger, el_log_level level, const char *message, size_t len);

/* elemental/log_printf.h: the C call site. */
void el_log_printf(const el_logger *logger, el_log_level level, const char *format, ...);
void el_log_vprintf(const el_logger *logger, el_log_level level, const char *format, va_list args);
EL_LOG_DEBUG(logger, format, ...)   EL_LOG_INFO(...)   EL_LOG_WARN(...)   EL_LOG_ERROR(...)

/* elemental/log_writer.h */
void el_log_writer_sink_init(el_log_writer_sink *sink, el_writer out, el_buf *line);
el_log_sink el_log_writer_sink_as_sink(el_log_writer_sink *sink);
uint32_t el_log_writer_sink_lost(const el_log_writer_sink *sink);

/* elemental/log_mem.h */
void el_log_mem_sink_init(el_log_mem_sink *sink, void *storage, size_t cap);
el_log_sink el_log_mem_sink_as_sink(el_log_mem_sink *sink);
size_t el_log_mem_sink_count(const el_log_mem_sink *sink);
void el_log_mem_sink_get(const el_log_mem_sink *sink, size_t index, el_log_record *record);
uint32_t el_log_mem_sink_lost(const el_log_mem_sink *sink);
void el_log_mem_sink_clear(el_log_mem_sink *sink);
```

**The writer sink's line** is fixed: seconds in a field of six,
microseconds, the level's letter (`D I W E`), the tag and a colon unless
the tag is empty, the message, a newline. A program that wants another
format writes a sink: it receives the record, and the writer sink is
about forty lines to copy from.

**Where the message is formatted.** `el_log_printf` checks the logger's
level, formats into `LOG_MESSAGE_MAX` bytes on its own stack, and calls
`el_log_write`. The alternative is scratch storage the caller gives the
logger. That would make the limit a parameter and the stack use visible,
and it would make the logger an object with state: one context at a time,
and two buffers to provide before the first line of Example 1. The owner
chose the stack (decision 3).

### A default logger is the application's

`mlog`'s short form, `MLOG_INFO(fmt, ...)`, needs a logger that every file
can reach. Elemental provides none, because it would be a global. An
application that wants one has it in three lines, and owns the choice:

```c
/* app_log.h */
extern el_logger app_log;
#define APP_INFO(...) EL_LOG_INFO(&app_log, __VA_ARGS__)
```

## Configuration

| Option | Default | Use case | Why not the linker / a parameter |
|---|---|---|---|
| `LOG` | on | The module. Selects `FMT`, `TIME`, `IO` | As every module |
| `LOG_MESSAGE_MAX` | 128 | The longest message `el_log_printf` formats, which is also its stack use. Lower it for small stacks, raise it for long messages | The linker cannot size a buffer. A parameter was decision 3's alternative. It is private: it changes how long a message may be, not any type or signature |

`EL_LOG_CUTOFF` is **not** a Kconfig option. It is a macro the consumer
defines for its own target or file, and Elemental's configuration never
sets it ([ADR 0021](../adr/0021-log-cutoff.md)).

## The cut-off

The roadmap asks that the compile-time cut-off remove call sites, shown by
a size or symbol check.

```c
#ifndef EL_LOG_CUTOFF
#define EL_LOG_CUTOFF EL_LOG_LEVEL_DEBUG /* keep everything */
#endif

#define EL_LOG(logger, level, ...)                         \
    do {                                                   \
        if ((level) >= EL_LOG_CUTOFF) {                    \
            el_log_printf((logger), (level), __VA_ARGS__); \
        }                                                  \
    } while (0)
```

The condition is a constant, so the compiler drops the call, the format
string, and the evaluation of the arguments. The call is still compiled
first: a format that no longer matches its arguments is reported whether
or not the call is kept, as an assertion's condition is
([conventions.md](../conventions.md#assertions-and-recoverable-failures)).

*Measured* on Example 1's translation unit, x86-64, GCC 16.2.1 and Clang
23.1.5: text bytes of the object, whether the string `"dialling %s"` is
in it, and the number of references to `el_log_printf`.

| Compiler | `EL_LOG_CUTOFF` | Text | `"dialling %s"` | Calls |
|---|---|---|---|---|
| GCC `-Os` | not defined | 545 | present | 4 |
| GCC `-Os` | `EL_LOG_LEVEL_INFO` | 505 | absent | 3 |
| GCC `-Os` | `EL_LOG_LEVEL_OFF` | 307 | absent | 0 |
| GCC `-O0` | `EL_LOG_LEVEL_INFO` | 641 (696 without) | absent | 3 |
| Clang `-Os` | `EL_LOG_LEVEL_INFO` | 475 (524 without) | absent | 3 |
| Clang `-O0` | `EL_LOG_LEVEL_INFO` | 646 (680 without) | absent | 3 |

Both compilers remove the call without optimization too. The test in the
repository would be this check, made by CMake on a compiled object.

The library has no compile-time level. It has nothing to disagree with
the caller about, so there is no mismatch to detect and no link-time
guard, which is the simplification
[module-plan.md](../module-plan.md#3-decisions-that-must-come-first)
recommended.

## Context for a failure

The roadmap asks that M3b's context decision be made, or that this
proposal say why `log` does not need it. **It does not need it.**

A sink receives a level, a time, a tag, and bytes. A failure with context
([ADR 0019](../adr/0019-failure-context.md), proposed, left open by the
owner) would reach a log as text in a message: a function that formats
the record into an io buffer, called by the application before it logs.
Neither the sink interface nor the record changes for it. What `log`
would add, when that decision is made, is one such function, and one
finding from the status proposal's measurements bears on it: a trace of
four hops is about 250 bytes, more than `LOG_MESSAGE_MAX`, so it is
logged as one record per hop or with a longer limit.

## Flush (ADR 0014)

The roadmap asks that the writer sink confirm or reopen the provisional
decision to keep flush out of `el_writer_ops`.

On this design it confirms it. The writer sink builds a line in the
buffer it borrows and passes it on with one `el_write_all`: it holds nothing between
records, so it has nothing to flush. A program that wants fewer system
calls puts an `el_buffered_writer` under the sink; it created that writer,
and flushes it through the concrete type. No code here holds a bare
`el_writer` it needs to flush.

For the same reason a sink has no flush operation, though `mlog`'s backend
has one. The first sink that holds records back is M5's deferred sink, and
draining it is a function of that type.

The implementation has to show the same before
[ADR 0014](../adr/0014-flush-on-the-concrete-type.md) stops being
provisional.

## Verification

- **Contract tests** (`ElLogContract`, `log_contract`, defined with
  `elemental_add_contract_suite()`): a fixture makes a sink that takes at
  least `n` records of up to `m` bytes, and says how many were delivered
  and what the `i`-th one's message was. The suite checks: records arrive
  in the order written; the message arrives byte for byte; the sink
  copies, shown by reusing the caller's buffer between records; an empty
  message is a record; `EL_OK` is returned for each. With capabilities,
  null where a sink cannot say: the level arrives; the timestamp and
  frequency arrive; the tag arrives; and for a sink that can be made to
  take exactly `n` records, the next is refused with a status other than
  `EL_OK` and the first `n` are intact.
- **Implementations to run it against:** the memory sink (every
  capability); the writer sink over an `el_buf`, its fixture parsing the
  lines (no exact timestamp below a microsecond, full after `n` by the
  buffer filling); the tee example over two memory sinks.
- **Backend-specific tests:** the writer sink's line format, the cut line
  and its count, a failing writer; the memory sink's count, order, lost
  count, and clear.
- **Unit tests:** level filtering before formatting (a format with a null
  `%s` argument below the level is never read); the clock is read exactly
  once per record made and not at all for one filtered, with a clock that
  counts its reads; a message cut at `LOG_MESSAGE_MAX`; level names.
- **Two sinks in one program:** the memory sink and a POSIX descriptor
  through the writer sink, as Example 2, on the host.
- **The cut-off:** the check above, on a compiled object, for the default
  and two cut-offs.
- **Death tests:** each contract violation.
- **Broken on purpose:** a sink that keeps the caller's pointer, one that
  reorders, one that reports `EL_OK` for a record it dropped.

| Target | Toolchain | Level intended |
|---|---|---|
| x86-64 Linux | GCC, Clang, Clang with sanitizers | Run, all of the above |
| `native_sim` | host GCC, 32-bit | Run, without the POSIX descriptor |
| `mps2/an385`, `nucleo_g0b1re_renode` | Zephyr SDK GCC | Emulator run of the unit and contract tests; symbol check |
| `qemu_cortex_m0` | Zephyr SDK GCC | Symbol check |

What was run for this proposal, on a prototype outside the repository:
both examples and the backend example on x86-64 (GCC 16.2.1, `-std=c11`
with the repository's warnings as errors; Example 2 also under the address
and undefined-behavior sanitizers), the headers as C++11, the cut-off
check, and the Cortex-M0+ build and emulator run above.

## Evidence required before stable

As [api-stability.md](../process/api-stability.md). Specific to `log`: the
deferred sink exists and is used from an interrupt handler (M5), since it
is the sink most likely to change the record; a sink written outside the
repository; and the cut-off used by a consumer in a release build.

## Decisions

The owner accepted the proposal on 2026-11-16 and chose decision 3. The
others are this proposal's recommendations, named to the owner when it was
accepted and not decided one by one; each is cheap to change while `log` is
experimental.

1. **The cut-off is `EL_LOG_CUTOFF`, a macro the consumer defines**, not a
   Kconfig option and not a compile definition carried by Elemental's
   target ([ADR 0021](../adr/0021-log-cutoff.md)).
2. **The logger has a tag**, `mlog`'s module name moved from a macro at
   the call site to a field. It costs a pointer in the logger and the
   record. The plan's sketch of a record had only level, time, and bytes.
3. **A message is formatted on `el_log_printf`'s stack**, at most
   `LOG_MESSAGE_MAX` bytes, a private Kconfig option. *Owner, 2026-11-16*,
   against scratch storage the caller gives the logger. *Reversed
   2026-11-17* by [ADR 0022](../adr/0022-log-sink-lends-the-buffer.md): the
   sink lends the buffer.
4. **Four levels**: debug, info, warn, error. `mlog` has trace and fatal
   as well.
5. **Left out of `mlog`**: the default logger, source location in the
   record, `is_enabled`, `flush` and `close`, formatting put off to the
   backend. Reasons in the table.

Also carried: a log call returns nothing and the sink counts what it lost;
the sink's operation returns a status; the memory sink keeps records, not
lines; the writer sink's line format is fixed.

## Open questions

- **A cut message does not say so.** A flag in the record would let a sink
  mark it. Left out until someone is misled by one.
- **The deferred sink's record** (M5): whether a record copied into a ring
  keeps the tag as a pointer, as the contract allows, and where the
  frequency is stored once instead of per record.
- **A sink into Zephyr's logging**, the one `mlog` backend not carried
  over: a port's to write, or an example under `tests/zephyr`.
- **Hex dumps and other structured payloads**: `fmt` dropped its hex dump
  in M3a; nothing here brings one back.

## Sources

- `mlog`, the owner's working copy, read in full on 2026-11-16:
  `include/mlog/mlog.h`, `src/mlog.c`, `backends/membuf/`,
  `backends/zephyr/`, `README.md`.
- Zephyr v4.5.0 (`41d10b5973`), read in the local workspace:
  `include/zephyr/logging/log_ctrl.h` (`log_set_timestamp_func`).
