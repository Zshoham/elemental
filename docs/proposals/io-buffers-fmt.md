# Proposal: buffers in io, and fmt reworked on them

Status: Draft, revised after an independent review (2026-11-13)
Author, date: design pass, 2026-11-13; revised the same day

This changes two existing modules rather than adding one.

- **`io`** gets one memory buffer type, `el_buf`, a grown variant,
  `el_grow_buf`, and a buffered writer built on them. Together they replace
  `el_mem_writer` and `el_grow_writer`. A buffered reader is sketched, and
  is to be proposed again with `frame`.
- **`fmt`** shrinks to pure number conversions. Its printf front end
  formats into an `el_buf` or a buffered writer, so `fmt` now depends on
  `io`. The platform-formatter option moves from digit generation to
  printf.

The direction is the owner's ([Problem](#problem)). The owner has since
decided the points the review raised ([Review and
revisions](#review-and-revisions)). What is still open is listed in
[Decisions for the owner](#decisions-for-the-owner).

Labels are as in [architecture.md](../architecture.md):

- **Source fact**: something read in a cited source ([Sources](#sources)).
- **Measured**: a number produced in this pass. The method is stated with
  it.
- Everything else is a recommendation.

Milestones are the [roadmap's](../roadmap.md). The decisions this would
reverse or replace:

- [fmt proposal decision 3](fmt.md#decisions) ("its own cursor");
- module-plan's ["Where stream glue lives"](../module-plan.md#3-decisions-that-must-come-first);
- [ADR 0011](../adr/0011-formatting-truncation.md);
- the [fmt amendment](fmt.md#amendment-2026-11-12) (`FMT_BUILTIN` /
  `FMT_LIBC`).

## Review and revisions

An independent review of the first draft took place on 2026-11-13. The
owner decided its open findings the same day.

**A. Buffered writer failures, with no latched error.** In the draft,
`write` broke `io.h` in three ways:

- a write-through that met `EL_ERR_END_OF_STREAM` left the buffer empty,
  so a later small write succeeded, which breaks permanence;
- the pass-on attempt swallowed `EL_ERR_IO` and still returned `EL_OK`;
- grown storage was left undefined.

Now the pass-on is one attempt. Any status other than
`EL_ERR_WOULD_BLOCK` is returned with nothing taken. A would-block leads to
buffering (or to growth, under B).

**B. Grown storage in a buffered writer grows instead of waiting, and the
allocator sets the bound.** When the stream would block, a grown buffer
asks its allocator for more room. No limit lives in the buffer.

**C. `el_buf` is four words, and grown storage is its own type.**
`el_buf` is `{data, len, cap, grow}`. `el_grow_buf` (in `io_alloc.h`) holds
an `el_buf` and the allocator. A fixed buffer has no `deinit`. This fixes
two review findings:

- `el_buf_deinit` in `io_buf.h` referenced `el_free`, which defeated the
  link-time claim;
- use after `deinit` could not be detected for fixed storage.

The owner also accepted these:

1. A rule for what the owner may do with an `el_buf` an adapter borrows.
2. `el_buf_data_mut` for back-patching.
3. `el_buf_append` is whole-or-nothing, and the partial form is private.
4. A `pos` in the buffered writer, so a partial flush does not move bytes.
5. New measurements on current `main`, including the libc option, and the
   contract suite run against the prototype.
6. The buffered reader and Example 4 are provisional.
7. A list of smaller corrections: the portable subset is decided by the
   whole specification; overflow is a status; and a rename to
   `EL_FMT_INT_LEN_MAX`.

Each is applied below. Two places where this revision had to interpret the
owner's decisions are stated with them, in
[Buffered writer failures](#buffered-writer-failures-decision-a) and
[decision 17](#decisions-for-the-owner).

## Problem

There are three copies of one idea: a buffer with a capacity and a length,
plus a rule for appending to it.

| Today | Storage | When it is full |
|---|---|---|
| `el_mem_writer` (`io_mem.h`) | Caller memory | Takes what fits, then `EL_ERR_END_OF_STREAM` |
| `el_grow_writer` (`io_alloc.h`, `IO_ALLOC`) | Allocator, grows | Grows; `EL_ERR_NO_SPACE` if refused |
| `el_fmt` (`fmt.h`) | Caller memory | Cut at a byte (text) or an item (numbers); sticky |
| *M7's buffered writer* (planned) | Either | Flush downstream |

They differ in two ways only: where the storage comes from, and what
happens when it is full. Each copy has its own init, length accessor and
status vocabulary. `el_fmt` exists apart only because
[fmt decision 3](fmt.md#interface-model) kept `fmt` free of `io`.

That separation costs more than a struct:

- **printf cannot print to a stream.** Printing to a UART needs a stack
  buffer, a format call, and then `el_write_all`. Output longer than the
  stack buffer is cut.
- **`fmt` does too much.** It is a cursor, a truncation state machine,
  padding, a hex dumper and a printf, all over its own cursor. Only the
  conversions themselves were hard. They need a divide by 10 without a
  helper on ARMv6-M ([fmt proposal](fmt.md#dependencies-and-cost)).
- **The platform option sits at the wrong layer.** `FMT_LIBC` hands only
  digit generation to `snprintf`. It saves 104 bytes in an image that links
  printf anyway ([modules/fmt](../../modules/fmt/README.md#configuration)).
  It does not give what the owner wanted: the platform's own formatting
  (floats, `%p`, flags) through Elemental's interface.

The owner approved this direction:

1. `io` owns buffers, with fixed or allocator-grown storage and two append
   rules. A memory endpoint and a buffered adapter are different things
   that share the buffer.
2. The `fmt` core becomes pure conversions into a caller's scratch array.
3. printf formats into an io buffer or a buffered writer. It keeps "a
   number is whole or absent", and `fmt` may depend on `io`.
4. The platform option moves to printf: built-in subset (the default), or
   libc, which hands each conversion outside that subset to `snprintf`.

## The model in one table

| | What it is | Where the bytes end up | Type |
|---|---|---|---|
| **Buffer** | Storage plus a length. Not a stream. | In the buffer, for its owner to read | `el_buf` (fixed); `el_grow_buf` (holds an `el_buf` that grows) |
| **Memory endpoint** | A stream that ends in memory | In the buffer | `el_buf_as_writer(buf)` |
| **Buffered adapter** | A buffer in front of another stream | In the other stream | `el_buffered_writer`; `el_buffered_reader` (provisional) |

Storage is orthogonal to all three. Everything takes an `el_buf *`:

- **Fixed:** `el_buf_init(&b, storage, cap)`.
- **Grown:** `el_buf *b = el_grow_buf_init(&g, allocator)`.

An adapter borrows an `el_buf`, so it works with either storage, with one
init function.

printf targets the two things that know their free space: a buffer, and a
buffered writer. A bare `el_writer` does not know its free space, so printf
cannot keep "whole or absent" on it.

`el_mem_reader` stays as it is, a cursor over **const** bytes. A buffer's
storage is mutable, so folding the reader into `el_buf` would cast away
`const` on flash tables and received frames.

## Usage examples

Each example compiled on 2026-11-13 with Clang 23.1.4
(`-std=c11 -Wall -Wextra -Wpedantic -Wformat=2 -Werror -fsyntax-only`)
against the header sketches below.

Examples 1, 2, 2b, 3 and 4 also ran on the host, linked against a
throwaway prototype of these headers. They ran under GCC 16.2.1 with
AddressSanitizer and UndefinedBehaviorSanitizer, with their outputs as the
text of each example states. The prototype stays out of the tree, as the
[process](../process/new-module.md#the-path) asks.

### Example 1: a log line formatted into a fixed buffer, sent to two sinks

This is how M4's writer sink could emit one record. The record's fields
belong to this example.

```c
#include <inttypes.h>
#include <stdarg.h>
#include <stdint.h>

#include <elemental/fmt_printf.h>
#include <elemental/io.h>
#include <elemental/io_buf.h>

typedef struct log_out {
    el_writer console;  /* a UART, say */
    el_writer file;     /* an io_posix descriptor */
    uint32_t truncated; /* lines that did not fit */
} log_out;

/* One line, formatted once, sent to both sinks. Returns the first failure. */
el_status log_line(log_out *out, uint64_t ticks, const char *level,
                   const char *format, ...) EL_FMT_PRINTF_CHECK(4, 5);

el_status log_line(log_out *out, uint64_t ticks, const char *level,
                   const char *format, ...)
{
    char storage[96];
    el_buf line;
    el_buf_init(&line, storage, sizeof storage);

    el_status st = el_fmt_printf(&line, "%10" PRIu64 " %s: ", ticks, level);
    if (st == EL_OK) {
        va_list args;
        va_start(args, format);
        st = el_fmt_vprintf(&line, format, args);
        va_end(args);
    }
    if (st != EL_OK) {
        out->truncated++; /* still sent: a cut line beats no line */
    }
    /* End the line; a full one gets its last byte replaced. */
    if (el_buf_append(&line, "\n", 1) != EL_OK) {
        el_buf_data_mut(&line)[el_buf_len(&line) - 1] = '\n';
    }

    const el_status a = el_write_all(out->console, el_buf_data(&line), el_buf_len(&line), NULL);
    const el_status b = el_write_all(out->file, el_buf_data(&line), el_buf_len(&line), NULL);
    return a != EL_OK ? a : b;
}
```

When run with a message longer than the line, the result was 96 bytes:
the header, the message cut at its last fitting byte with that byte
replaced, and `'\n'`. `truncated` was 1.

What changed from [fmt Example 1](fmt.md#example-1-a-log-line-into-a-stack-buffer-the-common-case):

- **The two calls are chained** (`if (st == EL_OK)`). Truncation stops
  within one printf call and does not carry over to the next (decision 7).
  The chain is what keeps `"count= ok"` from appearing after a dropped
  count.
- **No byte is held back** for a terminator or for the newline.
- **Formatted once, sent twice.**

### Example 2: printf straight to a non-blocking UART through a buffered writer

```c
#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>

#include <elemental/fmt_printf.h>
#include <elemental/io_buf.h>
#include <elemental/io_buffered.h>

/* The application's UART writer: takes what the transmit FIFO has room for
 * and returns EL_ERR_WOULD_BLOCK when it is full. Never waits. */
el_writer uart_writer(void);

#define LINE_MAX 64 /* the longest report line */

static uint8_t console_storage[256];
static el_buf console_buf;
static el_buffered_writer console;
uint32_t reports_dropped; /* for the application to show */

void console_init(void)
{
    el_buf_init(&console_buf, console_storage, sizeof console_storage);
    el_buffered_writer_init(&console, uart_writer(), &console_buf);
}

/* From the main loop. Never blocks. A report that finds no room for a
 * LINE_MAX line is dropped whole; a longer one streams, and a stall in the
 * middle of it leaves the UART mid-line. */
void report(uint32_t uptime_s, const char *name, size_t name_len, int32_t temp_mc)
{
    /* Room for a whole line first, passing earlier lines on if need be.
     * After this, a line of at most LINE_MAX bytes cannot reach the UART
     * before it is complete. */
    if (el_buffered_writer_reserve(&console, LINE_MAX) != EL_OK) {
        reports_dropped++;
        return;
    }
    const size_t mark = el_buf_len(&console_buf);
    if (el_fmt_printf_buffered(&console, "%8" PRIu32 " %.*s %" PRId32 " mC\n", uptime_s,
                               (int)name_len, name, temp_mc) != EL_OK) {
        /* Only possible for a line over LINE_MAX. If nothing was passed on
         * since the mark, take the partial line back. */
        if (el_buf_len(&console_buf) - el_buffered_writer_pending(&console) <= mark) {
            el_buf_truncate(&console_buf, mark);
        }
        reports_dropped++;
    }
    (void)el_buffered_writer_flush(&console); /* what the FIFO takes now */
}

/* From the UART's "transmit FIFO has room" event, in the main loop. */
void console_poll(void)
{
    (void)el_buffered_writer_flush(&console);
}
```

When run with the UART stalled, six reports were buffered and none were
dropped. After `console_poll` with the FIFO draining, all 150 bytes went
out, each line whole.

This example settles the resumability problem
([below](#printf-into-a-buffered-writer-the-resumability-problem)):

- printf into a buffered writer streams. When the buffer cannot take the
  next item, printf passes buffered bytes on to make room, so a line
  longer than the whole buffer still goes out.
- If the UART stalls mid-call, printf stops at an item boundary and returns
  `EL_ERR_WOULD_BLOCK`. Everything it accepted stays buffered.
- Reserve-first makes each line of at most `LINE_MAX` bytes all-or-nothing.
- `el_buf_truncate` back to the mark is the rollback, allowed while those
  bytes have not been passed on ([Borrowing](#what-the-owner-may-do-while-an-adapter-borrows-the-buffer)).

### Example 2b: grown storage that absorbs a burst instead of waiting

```c
#include <stddef.h>
#include <stdint.h>

#include <elemental/alloc_arena.h>
#include <elemental/fmt_printf.h>
#include <elemental/io_alloc.h>
#include <elemental/io_buffered.h>

/* A trace stream on Linux: a non-blocking pipe to a slower reader. Bursts
 * should be absorbed in memory rather than dropped, but never more than
 * 16 KiB of it: the arena is the bound, not the buffer. */
static uint8_t trace_arena_storage[16384];
static el_bump_arena trace_arena;
static el_grow_buf trace_storage;
static el_writer trace_pipe;
static el_buffered_writer trace;

void trace_init(el_writer nonblocking_pipe)
{
    trace_pipe = nonblocking_pipe;
    el_bump_arena_init(&trace_arena, trace_arena_storage, sizeof trace_arena_storage);
    el_buf *const buf = el_grow_buf_init(&trace_storage, el_bump_arena_as_allocator(&trace_arena));
    el_buffered_writer_init(&trace, trace_pipe, buf);
}

/* EL_ERR_WOULD_BLOCK: the pipe is full and the arena is spent; the event
 * was cut at an item, or dropped if nothing of it fitted. */
el_status trace_event(uint32_t id, const char *what)
{
    const el_status st = el_fmt_printf_buffered(&trace, "%u %s\n", (unsigned)id, what);
    (void)el_buffered_writer_flush(&trace); /* pass on what the pipe takes */
    return st;
}

/* When the pipe has drained (and the buffer with it), start the arena over:
 * a bump arena gets nothing back from the frees growth makes. */
void trace_idle(void)
{
    if (el_buffered_writer_flush(&trace) == EL_OK) {
        el_grow_buf_deinit(&trace_storage);
        el_bump_arena_reset(&trace_arena);
        el_buffered_writer_init(&trace, trace_pipe,
                                el_grow_buf_init(&trace_storage,
                                                 el_bump_arena_as_allocator(&trace_arena)));
    }
}
```

When run with the pipe full throughout, 830 events (8 190 bytes) were
absorbed before the arena refused. The next event returned
`EL_ERR_WOULD_BLOCK`, and when the pipe drained, all 8 190 bytes reached
it.

The arena held about twice what the buffer held. That is growth over an
allocator that does not reclaim, the same cost as today's grow writer
([io README](../../modules/io/README.md)).

### Example 3: a message of unknown size in an allocator-grown buffer

```c
#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>

#include <elemental/alloc.h>
#include <elemental/alloc_arena.h>
#include <elemental/fmt_printf.h>
#include <elemental/io_alloc.h>
#include <elemental/io_buf.h>

typedef struct reading {
    uint16_t sensor;
    int32_t value;
} reading;

/* Somewhere the finished message goes, as (pointer, length). */
el_status publish(const uint8_t *msg, size_t len);

/* {"r":[[1,-40],[2,1250],...]} for any number of readings. */
el_status publish_readings(const reading *r, size_t count, el_allocator scratch)
{
    el_grow_buf storage;
    el_buf *const msg = el_grow_buf_init(&storage, scratch); /* allocates nothing yet */

    el_status st = el_buf_append(msg, "{\"r\":[", 6);
    for (size_t i = 0; i < count && st == EL_OK; i++) {
        st = el_fmt_printf(msg, "%s[%" PRIu16 ",%" PRId32 "]", i == 0 ? "" : ",",
                           r[i].sensor, r[i].value);
    }
    if (st == EL_OK) {
        st = el_buf_append(msg, "]}", 2);
    }
    if (st == EL_OK) { /* EL_ERR_NO_SPACE: the allocator ran out */
        st = publish(el_buf_data(msg), el_buf_len(msg));
    }
    el_grow_buf_deinit(&storage); /* frees with the capacity it allocated */
    return st;
}

/* A caller with a bounded arena: at most 2 KiB of message, whatever count is. */
static uint8_t arena_storage[2048];

el_status publish_from_arena(const reading *r, size_t count)
{
    el_stack_arena arena;
    el_stack_arena_init(&arena, arena_storage, sizeof arena_storage);
    return publish_readings(r, count, el_stack_arena_as_allocator(&arena));
}
```

When run, 3 readings gave a 28-byte message. 300 readings returned
`EL_ERR_NO_SPACE` over the 2 KiB arena.

The same printf call works on fixed and grown storage, because both are an
`el_buf *`. [ADR 0012](../adr/0012-sized-free.md)'s hazard is to free with
the length instead of the capacity. That hazard stays inside
`el_grow_buf_deinit`, which frees with `el_layout_make(cap, 1)`
([ADR 0013](../adr/0013-allocation-layout.md)).

The [alloc proposal's](alloc.md#example-2-a-growable-buffer) `read_to_end`
reads straight into the buffer's spare room:

```c
for (;;) {
    st = el_buf_reserve(out, 64);
    if (st != EL_OK) return st;
    size_t n = 0;
    st = el_read(in, el_buf_spare(out), el_buf_room(out), &n);
    if (st == EL_ERR_END_OF_STREAM) return EL_OK;
    if (st != EL_OK) return st;
    el_buf_commit(out, n);
}
```

### Example 4 (provisional): a frame parser reading through a buffered reader

**Provisional.** The buffered reader is to be proposed again with `frame`
(M6), whose COBS decoder may want a different primitive than `peek(min)`
([open question 1](#open-questions)). This proposal accepts only one thing
about readers: that `el_buf`'s spare/commit, plus a front removal private
to `io`, are enough to build one. Example 4 is that evidence. It ran on the
host and parsed three frames fed one byte per call, with a would-block
after each byte.

```c
#include <stddef.h>
#include <stdint.h>

#include <elemental/bytes.h>
#include <elemental/io.h>
#include <elemental/io_buf.h>
#include <elemental/io_buffered.h>

/* Frames on a byte stream: a 16-bit big-endian length, then the payload. */
#define PAYLOAD_MAX 256u

typedef struct frame_rx {
    el_buf buf;
    el_buffered_reader in;
    uint8_t storage[2 + PAYLOAD_MAX];
} frame_rx;

/* The handler borrows the payload for the call. */
typedef void (*frame_handler)(void *ctx, const uint8_t *payload, size_t len);

void frame_rx_init(frame_rx *rx, el_reader source)
{
    el_buf_init(&rx->buf, rx->storage, sizeof rx->storage);
    el_buffered_reader_init(&rx->in, source, &rx->buf);
}

/*
 * Hands every complete frame that has arrived to `handle`. Returns
 * EL_ERR_WOULD_BLOCK when the source has nothing more for now; call again
 * later, and a frame that was half received continues where it stopped.
 * Returns EL_ERR_NO_SPACE for a length above PAYLOAD_MAX: the stream cannot
 * be trusted after that, so the caller drops the connection.
 */
el_status frame_rx_poll(frame_rx *rx, frame_handler handle, void *ctx)
{
    for (;;) {
        const uint8_t *p;
        size_t have;
        el_status st = el_buffered_reader_peek(&rx->in, 2, &p, &have);
        if (st != EL_OK) {
            return st;
        }
        const size_t len = el_bytes_load_be16(p);
        if (len > PAYLOAD_MAX) {
            return EL_ERR_NO_SPACE;
        }
        st = el_buffered_reader_peek(&rx->in, 2 + len, &p, &have);
        if (st != EL_OK) {
            return st;
        }
        handle(ctx, p + 2, len);
        el_buffered_reader_consume(&rx->in, 2 + len);
    }
}
```

### From another language: the core, without printf

ABI rule 5 keeps variadics out of contracts, so a binding calls the
conversions and the buffer directly:

```c
el_status put_id(el_buf *out, uint32_t id)
{
    char digits[EL_FMT_INT_LEN_MAX];
    const size_t n = el_fmt_dec_u64(digits, sizeof digits, id);
    el_status st = el_buf_append(out, "id=", 3);
    if (st == EL_OK) {
        st = el_buf_append(out, digits, n);
    }
    return st;
}
```

```rust
let mut digits = [0u8; EL_FMT_INT_LEN_MAX];
let n = unsafe { el_fmt_dec_u64(digits.as_mut_ptr().cast(), digits.len(), u64::MAX) };
assert_eq!(&digits[..n], b"18446744073709551615");
```

## Backend example

There is no new interface, so there is no new backend. The buffered writer
implements `el_writer` by wrapping another implementation. It runs the io
contract suite like any backend
([Verification](#verification); results measured against the prototype).
What a platform writes for it is the inner stream, such as Example 2's UART
writer. That is an ordinary `io` backend.

## Why not something that exists

| Candidate | What it does | What is insufficient, or what was taken |
|---|---|---|
| Keep today's three types | — | The duplication in [Problem](#problem). printf cannot reach a stream. |
| Rust `BufWriter`, `BufRead` (1.90.0) | *Source fact:* a write that does not fit flushes first; a write at least the capacity goes straight to the inner writer; drop flushes and ignores errors. `fill_buf`/`consume` are required methods of `BufRead`; `flush` is a required method of `Write`. | **Taken:** write-through for large writes, and peek and consume. **Not taken:** flush-first. Here a write that does not fit makes *one* pass-on attempt (decision A), because the inner may be non-blocking. **Not taken:** flush on drop (C has no destructor). **Not taken:** flush and peek in the interface ([decision 5](#flush-and-el_writer_ops)). |
| Rust `core::fmt::Write` (1.90.0) | *Source fact:* `write_str` returns `fmt::Error`, a unit struct with no information. `io::Write::write_fmt` keeps the real `io::Error` in an `Adapter` beside the formatter. | Same shape as this proposal. printf returns the stream's real status, which C can do directly. |
| Go `bufio.Writer`, `bufio.Reader`, `bytes.Buffer` (go1.23.0) | *Source fact:* after a write error, `bufio.Writer` accepts nothing more and `Flush` returns the error, forever. `Flush` is on the concrete type, not `io.Writer`. `Peek` returns `ErrBufferFull` past the buffer. `bytes.Buffer` grows to at least twice its capacity, keeps a read offset, and panics with `ErrTooLarge`. | **Taken:** flush on the concrete type, peek failing past capacity, and doubling growth. **Not taken:** Go's sticky error for every error. Here only `EL_ERR_END_OF_STREAM`, which io.h already makes permanent, is remembered (decision 17). **Not taken:** a read offset in the buffer type (decision 2). |
| Pigweed `pw_stream`, `pw_string` (`1b610f4`) | *Source fact:* `Writer` has no `Flush`. `Write` is all or nothing (`RESOURCE_EXHAUSTED`: "No data was written"). `StringBuilder` is always NUL-terminated, has a sticky status cleared by `clear_status()`, cuts strings, and never truncates numbers. No buffered stream in `pw_stream`. | Confirms whole-or-absent numbers, a whole-or-nothing append (`el_buf_append`), and flush outside the interface. Its NUL and sticky status are what this proposal drops (decisions 7, 9). |
| Zephyr `cbprintf` (v4.5.0) | *Source fact:* formats through a per-character callback `int (*)(int c, void *ctx)`; a negative return aborts formatting and is returned from `cbprintf()`. | It can print to a stream, but a stall leaves an arbitrary prefix: a number can be cut mid-digit. Zephyr only, and variadic. |
| C++ `std::format_to_n` | *Source fact (cppreference oldid 189663):* writes at most n characters and returns the untruncated size. | Cuts at any character. The untruncated size still has no consumer (ADR 0011). |
| `snprintf` for everything | — | As in [the fmt proposal](fmt.md#why-not-something-that-exists). It survives here as one conversion at a time behind the libc option. |

## Interface

Header sketches: a design, not final text. They follow
[conventions.md](../conventions.md):

- caller-provided storage;
- public layout, not public API;
- statuses for what input can cause, assertions for what only a bug can;
- `size_t` sizes;
- no enums in signatures.

### `<elemental/io_buf.h>`: the buffer

```c
#include <stddef.h>
#include <stdint.h>

#include <elemental/io.h>

typedef struct el_buf {
    uint8_t *data;
    size_t len;
    size_t cap;
    /* Null for fixed storage. Set by el_grow_buf_init; make room >= min. */
    el_status (*grow)(struct el_buf *b, size_t min);
} el_buf;

/* Fixed storage: the `cap` bytes at `storage`, borrowed until the buffer is
 * no longer used. `storage` may be null only if `cap` is 0. There is no
 * deinit: a fixed buffer owns nothing. An el_buf of all zeros, or one
 * initialized with (NULL, 0), is valid and always full. */
void el_buf_init(el_buf *b, void *storage, size_t cap);

/* The contents, bytes [0, len). data is null while a grown buffer has not
 * allocated (len is then 0, which every Elemental function accepts with a
 * null pointer). Valid until the next call that can append, grow, compact
 * or remove bytes. data_mut is the same pointer, for patching bytes
 * already written: a length prefix, a COBS code byte, a line ending. */
const uint8_t *el_buf_data(const el_buf *b);
uint8_t *el_buf_data_mut(el_buf *b);
size_t el_buf_len(const el_buf *b);
size_t el_buf_room(const el_buf *b);   /* free bytes now, without growing */

/* Append whole or nothing. EL_OK, or EL_ERR_NO_SPACE with nothing appended:
 * fixed storage is too small, the allocator refused, or len_now + len
 * overflows. `data` must not point into this buffer's storage (asserted). */
el_status el_buf_append(el_buf *b, const void *data, size_t len);

/* Writing in place. reserve: make room >= min, growing if it can; EL_OK, or
 * EL_ERR_NO_SPACE with nothing changed (also when len + min overflows).
 * spare: the first free byte (null while a grown buffer has not allocated);
 * room() bytes may be written there. commit: those n bytes are now part of
 * the contents (n <= room, asserted). */
el_status el_buf_reserve(el_buf *b, size_t min);
uint8_t *el_buf_spare(el_buf *b);
void el_buf_commit(el_buf *b, size_t n);

/* Keep the first `len` bytes (len <= el_buf_len, asserted). Storage is
 * kept. Undoes a partly formatted record. */
void el_buf_truncate(el_buf *b, size_t len);

/* The memory endpoint: an el_writer whose bytes end up in `b`. One attempt
 * takes what fits, growing first if it can: EL_OK with 1 <= *n, or
 * EL_ERR_NO_SPACE with *n == 0. Never would-block, never ends. */
el_writer el_buf_as_writer(el_buf *b);
```

### `<elemental/io_alloc.h>`: grown storage (only with `IO_ALLOC`)

```c
#include <elemental/alloc.h>
#include <elemental/io_buf.h>

typedef struct el_grow_buf {
    el_buf buf;             /* first: the grow hook recovers the allocator */
    el_allocator allocator;
} el_grow_buf;

/*
 * Start an empty buffer that grows from `allocator`, and return it: data
 * null, len 0, cap 0, no allocation yet. Growth is to twice the capacity,
 * or to what is needed if more, at least 16 bytes; if that is refused, it
 * asks once more for exactly what is needed. Allocations are made and
 * freed with el_layout_make(cap, 1) (ADR 0013). The allocator decides how
 * far it grows; the buffer has no limit of its own.
 */
el_buf *el_grow_buf_init(el_grow_buf *g, el_allocator allocator);

/* Free the storage. The el_buf is then a zero-capacity fixed buffer, always
 * full, until el_grow_buf_init again. A second deinit is asserted. */
void el_grow_buf_deinit(el_grow_buf *g);
```

Notes on the buffer:

- **Four words** (16 bytes on a 32-bit target, 24 for `el_grow_buf`).
  `io_buf.h` names no allocator type, and a fixed buffer has nothing to
  release. So `io_buf.c` references neither `el_alloc` nor `el_free`, and a
  program that never calls `el_grow_buf_init` links no allocator code
  ([Cost](#dependencies-and-cost)).
- **The hook.** `el_buf_reserve` calls `grow` when it is not null.
  `el_grow_buf_init` sets it to a function in `io_alloc.c`. That function
  converts the `el_buf *` back to its `el_grow_buf *`, which is valid
  because `buf` is the first member (C11 6.7.2.1p15).
- **What fails and how.**
  - A null object, null storage with `cap > 0`, or null data with
    `len > 0` is a contract violation.
  - A `data` pointer into the buffer's own storage is a contract violation:
    growth or compaction would move it before the copy. It is asserted by
    an address range check.
  - `len + min` that overflows is `EL_ERR_NO_SPACE`, because sizes may
    come from input.
- **The "append what fits" form is private to `io`**
  (`el_buf_append_some_`). The memory endpoint's `write` uses it. printf's
  text path does not need it: it reserves, writes what fits into
  `el_buf_spare`, and commits.
- **Removing bytes from the front is private to `io` too**
  (`el_buf_drop_front_`, one `memmove`). The buffered writer uses it when
  it compacts, and the provisional reader uses it before a peek.
- **No read offset.** The contents are always `[0, len)` (decision 2).
- **No terminator, no sticky state** (decisions 7, 9).

### `<elemental/io_buffered.h>`: the buffered writer

```c
#include <elemental/io.h>
#include <elemental/io_buf.h>

typedef struct el_buffered_writer {
    el_buf *buf;     /* borrowed: [pos, len) accepted and not yet passed on */
    size_t pos;      /* bytes at the front already passed on */
    el_writer inner;
} el_buffered_writer;

/* `buf` is borrowed until the writer is no longer used; whatever it already
 * holds is passed on first. Fixed or grown storage. */
void el_buffered_writer_init(el_buffered_writer *w, el_writer inner, el_buf *buf);

/* Bytes accepted and not yet passed on. */
size_t el_buffered_writer_pending(const el_buffered_writer *w);

/* Pass every pending byte on: el_write_all to `inner`. EL_OK: none pending.
 * Otherwise inner's status (WOULD_BLOCK, END_OF_STREAM, IO, NO_SPACE); bytes
 * passed on before it are no longer pending. Resumable. Never grows, never
 * moves bytes (it advances pos, and both reset to 0 once none are pending).
 * Never waits on its own. */
el_status el_buffered_writer_flush(el_buffered_writer *w);

/* Make room >= min at the tail: pass bytes on, one attempt at a time, only
 * until there is room; then compact; then grow (grown storage).
 *   EL_OK                 room >= min.
 *   EL_ERR_WOULD_BLOCK    the stream stalled and the allocator refused (or
 *                           storage is fixed); room returns as the stream
 *                           drains.
 *   EL_ERR_NO_SPACE       min exceeds what the buffer can hold even
 *                           drained: fixed with min > cap, or grown and the
 *                           allocator refused.
 *   inner's END_OF_STREAM, IO, NO_SPACE: returned as they are. */
el_status el_buffered_writer_reserve(el_buffered_writer *w, size_t min);

/* The adapter as a writer. See "Buffered writer failures" below. */
el_writer el_buffered_writer_as_writer(el_buffered_writer *w);
```

### Buffered writer failures (decision A)

A `write` of `len` bytes through `el_buffered_writer_as_writer`:

1. **It fits** in the free room at the tail. It is buffered, `EL_OK`, and
   the inner writer is not called.
2. **It does not fit, and bytes are pending.** The writer makes **one**
   attempt to pass pending bytes on. That attempt either moves at least one
   byte, or moves nothing and returns a status (io.h's rule).
   - A status other than `EL_ERR_WOULD_BLOCK` (`EL_ERR_IO`,
     `EL_ERR_END_OF_STREAM`, `EL_ERR_NO_SPACE`) is returned, with
     `*n == 0` and nothing taken.
   - Otherwise go to step 4.
3. **It does not fit, nothing is pending, and `len >= cap`.** The write
   goes straight to the inner writer, one attempt.
   - Success, or any status other than `EL_ERR_WOULD_BLOCK`, is returned
     as it is.
   - On `EL_ERR_WOULD_BLOCK` the write falls back to step 4.
   - A grown buffer that has not allocated yet (cap 0) skips this step and
     grows instead. Its starting state is therefore never a permanent
     write-through.
   - A *fixed* buffer of capacity 0 is a pass-through writer by this rule.
     The suite was run against one.
4. **Make room.**
   1. If bytes were passed on, compact them out of the front (one
      `memmove`).
   2. If the write still does not fit and the storage is grown, ask the
      allocator for room for all of it (decision B).
   3. Buffer what fits.
   4. If nothing fits, return `EL_ERR_WOULD_BLOCK` with `*n == 0`. Nothing
      fits only when the stream refused, so space returns when it drains.
      That is io.h's definition of would-block. `EL_ERR_NO_SPACE` would
      tell the caller to supply storage, which would not help a fixed
      buffer.

**`EL_ERR_END_OF_STREAM` stays permanent.** Once the inner writer has
reported it, every later `write`, `flush` and `reserve` returns it without
asking again, including a write that would fit.

That needs remembered state: a write that fits never calls the inner
writer. This revision keeps it without a field. `pos` is set to `SIZE_MAX`,
a value it cannot otherwise hold. It is a latch for exactly the one status
`io.h` requires to be permanent. `EL_ERR_IO`, `EL_ERR_WOULD_BLOCK` and
`EL_ERR_NO_SPACE` are not remembered, and a writer recovers from them as
its inner writer does (tested).

The owner decided "no latched error". This is the narrowest reading
compatible with permanence, and it is decision 17.

**Grown storage (decision B):**

- A grown buffer grows only where a fixed one would have to wait:
  - after a pass-on that would-blocked or left too little room (step 4);
  - in `reserve` when `min` exceeds what draining can free.
- `flush` never grows.
- Growth stops where the allocator refuses (Example 2b). An arena is the
  natural bound.

**Compaction (accepted item 4).**

- `flush` and a successful pass-on advance `pos`. When nothing is pending,
  `pos` and `len` reset to 0.
- Bytes move only when `write` or `reserve` needs tail room that is held
  by already-passed bytes. They move after passing on whatever the inner
  takes, so the move is as small as it can be.

### What the owner may do while an adapter borrows the buffer

While a buffered writer (or the provisional reader) borrows an `el_buf`,
its owner may only:

- call `el_buf_data`, `el_buf_len` and `el_buf_room`, which read;
- for a buffered writer, call `el_buf_truncate(buf, mark)`, but only back
  to a `mark` whose bytes have not been passed on. That is, `mark` must be
  at least `el_buf_len(buf) - el_buffered_writer_pending(&w)`. This is the
  rollback for the reserve-first pattern (Example 2).

Anything else is a contract violation: appending, committing, truncating
past the passed-on point, re-initializing, or `el_grow_buf_deinit`.

The writer checks the cheap part on every call (`pos <= len`). A truncation
below what was passed on is therefore asserted at the next call. The rest
is the owner's to keep.

### `<elemental/io_buffered.h>`, provisional: the buffered reader

To be proposed again with `frame` (M6). The sketch below is the prototype
that Example 4 and the contract suite ran against:

```c
typedef struct el_buffered_reader {
    el_buf *buf;     /* borrowed: [pos, len) read ahead and not consumed */
    size_t pos;
    el_reader inner;
} el_buffered_reader;

void el_buffered_reader_init(el_buffered_reader *r, el_reader inner, el_buf *buf);
/* At least `min` unconsumed bytes, reading as needed; *data/*len always
 * written. EL_OK; EL_ERR_NO_SPACE (min exceeds what the buffer can hold);
 * or inner's status with what is available. min == 0 does not read. */
el_status el_buffered_reader_peek(el_buffered_reader *r, size_t min,
                                  const uint8_t **data, size_t *len);
void el_buffered_reader_consume(el_buffered_reader *r, size_t n);
el_reader el_buffered_reader_as_reader(el_buffered_reader *r);
```

### `<elemental/fmt.h>`: pure conversions

```c
#include <stddef.h>
#include <stdint.h>

/* Bytes the longest conversion writes: "-9223372036854775808" and
 * "18446744073709551615" are 20. */
enum { EL_FMT_INT_LEN_MAX = 20 };

enum { EL_FMT_UPPER = 1 }; /* hex digits A-F */

/*
 * Write `value` at out[0..n) and return n (1 to 20). No sign but '-', no
 * padding, no prefix, no terminator. `cap` must be at least
 * EL_FMT_INT_LEN_MAX (asserted), so these cannot fail. The output equals
 * C's printf for PRIu64, PRId64, PRIx64 / PRIX64.
 */
size_t el_fmt_dec_u64(char *out, size_t cap, uint64_t value);
size_t el_fmt_dec_i64(char *out, size_t cap, int64_t value);
size_t el_fmt_hex_u64(char *out, size_t cap, uint64_t value, uint32_t flags);
```

These conversions are interrupt-safe and reentrant without conditions. They
have no object, take no lock, do not allocate, and have no configuration.
`cap` is there so that a binding, or a caller who got the size wrong, hits
an assertion rather than an overrun.

### `<elemental/fmt_printf.h>`: printf into a buffer or a buffered writer

```c
#include <stdarg.h>

#include <elemental/io_buf.h>
#include <elemental/io_buffered.h>

#define EL_FMT_PRINTF_CHECK(format_arg, first_arg) /* unchanged */

/* Into a buffer. EL_OK, or EL_ERR_NO_SPACE: the buffer holds what fitted,
 * by the item rule below. */
el_status el_fmt_printf(el_buf *out, const char *format, ...) EL_FMT_PRINTF_CHECK(2, 3);
el_status el_fmt_vprintf(el_buf *out, const char *format, va_list args)
    EL_FMT_PRINTF_CHECK(2, 0);

/* Into a buffered writer: streams, making room as el_buffered_writer_reserve
 * does. EL_OK (all accepted; some may still be pending), or reserve's
 * status for the item that did not fit. */
el_status el_fmt_printf_buffered(el_buffered_writer *out, const char *format, ...)
    EL_FMT_PRINTF_CHECK(2, 3);
el_status el_fmt_vprintf_buffered(el_buffered_writer *out, const char *format, va_list args)
    EL_FMT_PRINTF_CHECK(2, 0);
```

The two pairs share one parser. Each entry point passes the parser a
`reserve` function for its target. `el_fmt_printf_buffered` lives in its
own object file. A program that prints only into buffers therefore links
no buffered-writer code, and that takes no word in any struct
([Cost](#dependencies-and-cost)).

**The item rule** applies to both targets. The output is a prefix of what
unlimited room would hold:

- An **item** is one conversion, or one run of literal text.
- **A number is whole or absent.** That covers its sign, padding, digits
  and, with the libc option, any prefix, decimal point and exponent. For a
  buffered writer, "absent" means the number never entered the buffer.
  Once it has, passing on may split it across writes, but the stream
  receives it whole unless the stream itself fails.
- **Text is cut at the last byte that fits.** This covers literal text and
  `%s` and `%c` with their padding.
- **Within one call, nothing follows the first item that did not fit.**
  Nothing carries over to the next call (decision 7).
- **No terminator.**
- **A fixed buffered writer of capacity 0** accepts no item: printf needs a
  buffer.

**The accepted formats depend on the configuration** (decision 10). The
portable subset is decided by the **whole specification**, not by the
conversion letter:

| Specification | `FMT_PRINTF_BUILTIN` (default) | `FMT_PRINTF_LIBC` |
|---|---|---|
| In the portable subset: conversion `d i u x X` with only the `0` flag, a decimal width, length `hh h l ll z` or none, and no precision. `c` with a decimal width. `s` with a decimal width and `.N` or `.*`. `%%`. Examples: `%05d`, `%8s`, `%.*s`. | Built-in code | **The same built-in code.** It never reaches `snprintf`. |
| `s` or `c` with the `-` flag or a `*` width (`%-8s`, `%*c`) | Contract violation | Elemental's text code, reachable only in this option |
| Any other specification of `d i u x X o f F e E g G a A p`, such as `%+d`, `%-5d`, `%.3d`, `% d`, `%#x`, `%jd`, `%.2f`, `%p` | Contract violation | `snprintf`, one conversion at a time, whole or absent |
| `%n`, `%lc`, `%ls`, anything else | Contract violation | Contract violation |

**A format within the portable subset never reaches `snprintf`, in either
option.** It is therefore interrupt-safe into a fixed buffer under both.
`log` (M4) and an interrupt-context sink (M5) can rely on that, whatever
the image's configuration.

## Flush and `el_writer_ops`

The roadmap requires this decision to be recorded "with the log example
that decided it" ([M7](../roadmap.md#m7--io-second-iteration)). The log
module does not exist yet. The decision is therefore **provisional until
M4's writer sink exists**, and so is its ADR (to be written).

**The test, from module-plan:** flush becomes an operation only if a
holder of a bare `el_writer` handle must flush.

**The planned log example does not need it.**

- The writer sink is handed an `el_writer`. It formats each record into
  its own `el_buf` and writes it with one `el_write_all` (Example 1). There
  is nothing to flush.
- Where a buffered writer is used, as in Examples 2 and 2b, the code that
  created it owns it, and calls `el_buffered_writer_flush` on the concrete
  type.

**The case that would need it** is a writer that wraps another and is
itself handed out as a bare handle. An example is a future `frame` encoder
writer over a buffered UART. If `frame` (M6) needs that, flush becomes an
optional operation at the end of `el_writer_ops`. A null operation means
"nothing to flush" and returns `EL_OK`, as module-plan says.

**Why not add it now?**

- Rust made `flush` a required method of `Write` (source fact), so every
  writer implements or forwards it.
- Go keeps `Flush` off `io.Writer`, and Pigweed's `Writer` has none
  (source facts).
- Adding it would mean an optional operation, a front function, a suite
  capability, forwarding in every wrapper, and a non-blocking flush
  contract that nothing in the tree exercises.

## printf into a buffered writer: the resumability problem

[`fmt.h`](../../modules/fmt/include/elemental/fmt.h) says a formatter
cannot resume inside a number. That stays true. A printf call holds a
`va_list` and a position in the format, and neither survives the return to
the caller.

What changes is that the formatter writes into a buffer that knows its
free space, and only whole items enter it:

1. Before each item, printf asks for room through
   `el_buffered_writer_reserve`: all of a number, or at least one byte of
   text.
2. If the stream stalls and the storage cannot grow, printf stops before
   the item. A text item keeps the part that fit. The call returns the
   status.
3. Everything accepted is pending or already passed on. It is a prefix that
   ends on an item. **Resuming means resuming the flush.**

**Measured on the prototype:**

- `"t=%u h=%u p=%u\n"` went into an 8-byte buffer in front of a writer that
  takes 3 bytes per call and stalls after 10.
- The stream received `"t=1234567 "`, `"h=89 p="` stayed pending, and the
  call returned `EL_ERR_WOULD_BLOCK`.
- The third number was absent, not cut.

**A line that stalls mid-way leaves the stream mid-line.** Three remedies,
all shown:

- **Reserve first** (Example 2). A line of at most the reserved length
  then never reaches the stream mid-line.
- **Roll back** (Example 2). `el_buf_truncate(buf, mark)` takes back a
  partial line while none of it has been passed on. That is exactly when
  reserve-first made the line fit. Once bytes have been passed on, they
  cannot be taken back.
- **Format into an `el_buf` first, then write** (Example 1).

## Where the truncation rule lives

The item rule belongs to printf, in `fmt_printf.h`. The buffer offers the
primitives it is built from:

- `el_buf_append` and `el_buf_reserve` for whole-or-nothing;
- `el_buf_spare` and `el_buf_commit` for text cut at a byte.

The buffer has no notion of an item.

**An ADR to be written supersedes ADR 0011**, with this draft:

> **Formatting appends to an io buffer; output is a prefix that ends on an
> item.** printf formats into an `el_buf` or an `el_buffered_writer`; each
> knows its free space, so an item enters only if it fits. When it does
> not fit, the call returns `EL_ERR_NO_SPACE`, or for a buffered writer the
> status that kept room from being made. What was accepted is a prefix of
> the unlimited output: numbers whole or absent, text cut at the last byte,
> nothing after the first item that did not fit, within the call. Nothing
> carries over to the next call. Callers that compose several calls check
> each status, or roll back with `el_buf_truncate`. No terminator is
> written. *Cost:* the "check only the last call" convenience of ADR 0011 is
> gone; a buffered stream that stalls mid-line is left mid-line unless the
> caller reserved the line first. *Revisit when:* the same as ADR 0011's,
> plus a consumer that composes many calls and keeps getting the chain
> wrong.

## What `fmt` drops and keeps

| Today | After | Why |
|---|---|---|
| `el_fmt`, `el_fmt_init`, `el_fmt_len` | Gone; `el_buf` | The cursor was the duplicate |
| `el_fmt_str`, `el_fmt_cstr` | Gone; `el_buf_append` (whole) | Appending bytes is the buffer's job. Text cut at a byte remains printf's (`%s`, `%.*s`). |
| `el_fmt_dec_u64`, `_i64`, `el_fmt_hex_u64` with `width`, `flags` | Pure conversions without width or zero-pad. `EL_FMT_UPPER` stays. | Padding needs a destination with free space. printf keeps width and `0`. |
| `el_fmt_hex_bytes` | Dropped (decision 8) | No consumer outside fmt's own example and tests. It comes back with one. |
| NUL termination | Dropped (decision 9) | Lengths never include a terminator (conventions, Storage rule 4) |
| Sticky truncation across calls | Per call only (decision 7) | — |
| `el_fmt_printf(el_fmt *, ...)` | `el_fmt_printf(el_buf *, ...)` plus the `_buffered` pair | Targets that know their free space |
| `FMT_BUILTIN` / `FMT_LIBC` (digits) | `FMT_PRINTF_BUILTIN` / `FMT_PRINTF_LIBC` | [Below](#the-platform-option-on-printf) |
| `EL_FMT_PRINTF_CHECK` | Kept | — |

## The platform option on printf

**Built-in** (default) is today's parser and subset, retargeted. It parses
only the portable syntax. Anything else fails the conversion check and is
asserted.

**Libc** works like this:

- Elemental's parser walks the format and fetches each argument with the
  type its specification names.
- A specification in the portable subset takes the built-in path.
- `%s` and `%c` with `-` or a `*` width take Elemental's text code.
- Every other numeric specification is rebuilt, with `*` values written
  in, and handed to `snprintf` with that one argument. The result enters
  the target whole, or not at all.

Handing the whole format to `vsnprintf` would cut numbers and lose the item
boundary.

Details:

- **Fitting.** `snprintf` writes into a 32-byte stack scratch first. A
  result that fits there is reserved and copied, which is exact. A longer
  one is formatted again in place after reserving its length plus one,
  because `snprintf` writes a terminator. So an extended conversion of 32
  bytes or more needs one more free byte than its length. Only conversions
  outside the portable subset are affected (decision 10).
- **No second copy is avoided.** The portable subset always uses the
  built-in code, so a libc-option image links both the built-in
  conversions and `snprintf`. Identical output for the common set is then
  true by construction, not by test. Today's `FMT_LIBC` saved 104 bytes by
  avoiding that copy, and that saving is given up.
- **Probe.** `FMT_PRINTF_LIBC` depends on the existing `PLATFORM_SNPRINTF`
  probe. Floats are whatever the C library does, and the contract says so.
- **Execution context.**
  - The portable subset is interrupt-safe in both options.
  - An extended conversion is as safe as the C library's `snprintf`.
  - The note moves from `fmt.h` to `fmt_printf.h`.

**[ADR 0004](../adr/0004-private-configuration.md).** Signatures, layouts
and macros stay identical in every configuration. A format that is correct
under libc is still a contract violation under built-in. **An ADR to be
written** settles this: "A private choice may widen what a function
accepts, never change it". It has five rules:

1. The default accepts the least.
2. For every input that every option accepts, the output is identical, and
   tested identical in each configuration. Here this holds by construction:
   it is the same code.
3. Inputs outside the default's set are contract violations under it,
   asserted where cheap.
4. The public header documents each set and names the option.
5. Code that must build against any configuration (`log`, a library over
   `fmt`) uses only the common set.

How it is tested:

- `fmt.unit` (portable subset, literal expectations) runs in both
  configurations.
- A new `fmt.libc` suite runs only with `FMT_PRINTF_LIBC`. It covers the
  extended conversions against glibc and the item rule at every capacity,
  and its own cases for `-` and `*` on `%s` and `%c`.
  - On Zephyr it runs against picolibc. Float cases run only where the
    test sees `CONFIG_PICOLIBC_IO_FLOAT`.
- `fmt.violations` asserts `%+d` under built-in, and `%n` under libc.

## Dependencies and cost

| | |
|---|---|
| Elemental modules | `io`: `status`; `alloc` with `IO_ALLOC` (unchanged). `fmt`: `status`, **`io`** (new). |
| Runtime symbols (freestanding) | `memcpy` (as today) and **`memmove`** (new: compaction). With `FMT_PRINTF_LIBC`, also `snprintf`, `strchr`, `strlen`. |
| Allocation | Only `el_grow_buf`, through the allocator it is given |
| Per-object storage (32-bit) | `el_buf` 16 bytes; `el_grow_buf` 24; `el_buffered_writer` 16; `el_buffered_reader` 16. Was: `el_mem_writer` 12, `el_grow_writer` 20, `el_fmt` 16. |
| New status codes | None |

**`memmove`.** *Source fact* (GCC 16.2.1 manual, "Language Standards
Supported by GCC"): "GCC requires the freestanding environment provide
`memcpy`, `memmove`, `memset` and `memcmp`". It joins
`tests/zephyr/allowed-undefined.txt` (decision 11).

**`memset` is not needed, but was nearly emitted.** The prototype first
zero-initialized the printf parser's specification struct with `{0}`. On
ARMv6-M, GCC 14.3.0 turned that into a `memset` call. Initializing it field
by field removed the call. The Zephyr symbol check is what would catch a
regression.

### Code size

**Measured** on 2026-11-13, on `main` at `33dca06` (after ADR 0013) and
on the revised prototype.

**Method:**

- **Compiler:** the Zephyr SDK 1.0.1's GCC 14.3.0, `-Os -mthumb
  -ffreestanding -fno-jump-tables -ffunction-sections`, assertions
  trapping.
- **Objects** are each source compiled alone; this is not a Zephyr build.
  The same commands reproduce `build.md`'s Cortex-M3 figures for `main`:
  - the grow writer, 378 bytes;
  - `io` without it, 574 (build.md: 578);
  - `fmt`, 1 705.
- **Images** are linked with `--gc-sections`, a startup stub, and either a
  byte-loop `memcpy`/`memmove` stub, or the SDK's prebuilt picolibc
  where marked.
- **The prototype** is functional but not tuned.

Library objects, text bytes:

| | Cortex-M0 | Cortex-M3 |
|---|---|---|
| **`main`:** `io.c` + `io_mem.c` + grow writer | 328 + 238 + 388 = 954 | 356 + 218 + 378 = 952 |
| **`main`:** `fmt.c` + digits + `fmt_printf.c` | 775 + 398 + 704 = 1 877 | 723 + 290 + 692 = 1 705 |
| **Proposed:** `io_buf.c` | 412 | 370 |
| **Proposed:** `io_alloc.c` (`el_grow_buf`) | 270 | 286 |
| **Proposed:** `io_buffered.c` (writer 620, provisional reader 348, on M0) | 968 | 958 |
| **Proposed:** `fmt.c` (three conversions) | 510 | 414 |
| **Proposed:** `fmt_printf.c`, built-in / libc parser | 1 100 / 1 426 | 1 054 / 1 392 |
| **Proposed:** `fmt_printf_buffered.c` | 72 | 72 |
| **Proposed:** `fmt_printf_libc.c` (libc only) | 662 | 644 |

Linked images, Cortex-M0. Each prints `"%10lu %s: %d\n"` and sends it with
`el_write_all`, unless stated:

| Program | Text bytes |
|---|---|
| `main`: into a 64-byte `el_fmt` | 1 759 |
| Proposed: into a 64-byte fixed `el_buf` | 1 975 (+216) |
| … with growth called directly instead of through `el_buf.grow` | 2 303 (the hook saves 328) |
| … with printf's target reserve called directly instead of through the pointer the entry point passes | 2 279 (the pointer saves 304) |
| … with neither | 2 607 |
| Proposed: through a 64-byte fixed buffered writer, then flush | 2 367 |
| Proposed: into an `el_grow_buf` over a bump arena, then deinit | 2 584 |

What the image numbers show:

- **The two pointers** (decision 12) save 632 bytes together for a
  fixed-buffer program.
  - `el_buf.grow` costs one word per buffer. Without it, `el_buf_reserve`
    would have to name the growth code.
  - printf's target pointer costs no struct word. It is an argument
    between the two entry points and the shared parser.
- **The remaining +216 bytes** come from three things:
  - the conversions writing into scratch and being copied;
  - out-of-line buffer accessors called across the module boundary;
  - a wider specification record in the parser.

The libc option, linked against the SDK's prebuilt picolibc:

| Program (Cortex-M0) | picolibc default `vfprintf` (`__d_vfprintf`) | `vfprintf=__i_vfprintf` (integer only) |
|---|---|---|
| Built-in option, the same line as above | 1 975 | — |
| Libc option, `"%10lu %s: %+d\n"` (`%+d` goes to `snprintf`) | 10 440 | 4 444 |
| Libc option, `"%10lu %s: %+d %.2f\n"` | 10 448 | — |
| No Elemental `fmt`: `snprintf` of `"%10lu %s: %+d\n"` into a stack array, then `el_write_all` | 7 696 | 1 699 |

So the libc option costs about 2.7 KB of Elemental code above calling
`snprintf` directly, under either picolibc printf:

- the built-in conversions and parser;
- the 520-byte `snprintf` bridge;
- the buffer.

Most of an image's printf cost is the C library's choice of `vfprintf`.
Picolibc's default double version is about 6 KB more than its integer
version.

**Not measured:**

- other assertion configurations;
- Cortex-M3 images;
- a Zephyr build of the libc option (picolibc there is built by Zephyr
  with its own `PICOLIBC_IO_*` level);
- cycle counts.

## Contracts

- **Ownership and lifetime.**
  - The caller owns every object. An `el_buf` borrows fixed storage until
    it is no longer used. An `el_grow_buf` owns its storage until
    `el_grow_buf_deinit`.
  - An adapter borrows its `el_buf` and its inner handle while it is used.
    What the `el_buf`'s owner may do meanwhile is
    [above](#what-the-owner-may-do-while-an-adapter-borrows-the-buffer).
  - `el_buf_data`, `el_buf_data_mut`, `el_buf_spare` and a peek's `*data`
    are valid until the next call that can append, grow, compact or remove
    bytes.
  - An object may move while no handle or adapter refers to it.
    `el_grow_buf` may not move while its `el_buf *` is in use.
- **Errors.**
  - The buffer: `EL_ERR_NO_SPACE` only, never permanent.
  - The buffered writer: as [above](#buffered-writer-failures-decision-a).
  - printf: its target's statuses, by the item rule.
- **Concurrency.** None internal. One object, one context. An adapter and
  the `el_buf` it borrows count as one object.
- **Execution context.**
  - Fixed `el_buf`, the conversions, and portable-subset printf into a
    fixed buffer are interrupt-safe under both printf options.
  - `el_grow_buf` is usable wherever its allocator may be used.
  - Adapters are as the inner stream allows, and add no waiting.
  - Extended (libc) conversions are as safe as `snprintf`.
- **Contract violations (asserted).**
  - Null objects or handles; null storage with a nonzero capacity; null
    data with a nonzero length.
  - `data` pointing into the buffer's own storage.
  - `commit` past the room; `truncate` past the length; `consume` past
    what is available.
  - The owner using a borrowed `el_buf` beyond the rule above; this is
    checked at the adapter's next call where cheap.
  - A conversion `cap` below `EL_FMT_INT_LEN_MAX`; unknown flag bits.
  - A null format or `%s` argument; a specification outside the
    configuration's set.
  - A second `el_grow_buf_deinit`.
- **Exceptions to [conventions.md](../conventions.md).** One:
  `el_buffered_reader_peek` (provisional) returns data with a non-`EL_OK`
  status, as `el_read_exact` reports progress with one. The draft's other
  exception, `el_buf_append` reporting progress and status together, is
  gone: `el_buf_append` is whole-or-nothing.

## Interface model

The buffer and the buffered writer are concrete types (mechanism 1 of
[ADR 0002](../adr/0002-interface-model.md)), entered into the existing
handles with `_as_writer` / `_as_reader`. `el_buf.grow` is a storage hook,
not an interface: only `io_alloc.c` sets it, and it is not a documented
extension point.

Build time decides `IO_ALLOC` (whether `el_grow_buf` exists) and the printf
option. Everything else is per instance.

## Configuration

| Option | Default | Use case | Why not the linker / a parameter |
|---|---|---|---|
| `IO_ALLOC` (new help text) | y | `el_grow_buf`; the only part of `io` that needs `alloc` | A dependency edge, which the linker cannot remove |
| `FMT` | y, now `select IO` | — | — |
| Choice `FMT_PRINTF`: `FMT_PRINTF_BUILTIN` (default) / `FMT_PRINTF_LIBC`, replacing `FMT_DIGITS` | built-in | Libc: the image already links the C library's printf, or needs floats, `%p` or flags, through Elemental's truncation rule | It changes what a function accepts, once per image. Renamed so that an old defconfig naming `FMT_LIBC` gets the default and a Kconfig warning, not silently wider semantics. |

## Verification

### Run on the prototype (2026-11-13)

**Host:** GCC 16.2.1 with AddressSanitizer and UndefinedBehaviorSanitizer,
GoogleTest v1.18.0 (the repository's provisioned copy).

**The io contract suite** was copied unchanged, plus one proposed optional
capability, `writer_full_after(room)`. It describes a writer that takes
exactly `room` bytes and then reports `EL_ERR_NO_SPACE`, not permanently.
Its test checks that the writer keeps what fitted and repeats
`EL_ERR_NO_SPACE`.

12 tests per fixture, 72 in all. **53 passed, 19 skipped, none failed.**

| Fixture | Passed | Skipped, and why |
|---|---|---|
| Fixed `el_buf` as a writer, memory reader | 9 | 3: cannot stall (reader, writer); does not end |
| `el_grow_buf` as a writer, over a stack arena wrapped in the checking allocator | 8 | 4: as above, and cannot be made full after an exact count |
| Buffered writer, fixed 16 bytes, over the ring pipe example | 9 | 3: the memory reader cannot stall; does not end; never full at an exact count |
| Buffered writer, grown, over the ring pipe. Stalled = ring full **and** a 256-byte arena spent (decision B). | 9 | 3: as above |
| Buffered writer, fixed capacity 0 (pass-through), over a writer that ends after N bytes | 9 | 3: cannot stall; never full at an exact count |
| Buffered reader (provisional), fixed 16 bytes, over the ring pipe | 9 | 3: the writer-side capabilities of its fixed-buffer writer |

**Unit checks:**

- **Built-in option: 47 checks, all passed.** Each of decision A's cases:
  - write-through meeting `END_OF_STREAM`, and later writes;
  - `END_OF_STREAM` met by a pass-on, and a later write that would fit;
  - `IO` and `NO_SPACE` from a pass-on, returned with nothing taken, and
    recovery from `IO`;
  - `WOULD_BLOCK` leading to buffering what fits;
  - a write-through that would-blocks, falling back to buffering.

  Decision B's cases:
  - a cap-0 grown buffer that grows instead of writing through;
  - growing while the stream would block, until the arena refuses;
  - `reserve` beyond the allocator returning `NO_SPACE`;
  - `flush` not growing;
  - `deinit` leaving an always-full buffer;
  - `len + min` overflow returning `NO_SPACE`.

  Item 4: a partial flush moves no byte, and the next write compacts.
  Also checked: reserve-first rollback, streaming printf, a stalled
  printf, whole-or-absent numbers, and Example 1's line ending.
- **Libc option: 385 checks, all passed.** Everything above, plus 13
  extended specifications compared with glibc `snprintf` at every capacity
  by the item rule, the one-byte edge included. Also `-` and `*` on `%s`
  and `%c`.
- **Built-in rejects `%+d`:** the program stops at the assertion (trap).

**Mutation:** five deliberately broken buffered writers. Each was caught.

| Mutant | Caught by |
|---|---|
| Remembering no `END_OF_STREAM` | 2 checks |
| The pass-on swallowing `IO` | `el_write`'s progress-or-status assertion, which traps |
| A write-through returning `WOULD_BLOCK` instead of buffering | 2 checks |
| Grown storage that never grows | 5 checks |
| `flush` compacting on every partial flush | 2 checks |

**Headers:** each prototype header compiled alone as C11, and `io_buf.h`
also as C++11, with `-Wall -Wextra -Wpedantic -Werror`.

### Planned for the implementation

- **The io contract suite** gains `writer_full_after`. The fixtures above
  move into `modules/io/tests`, replacing `io.contract.mem` and
  `io.contract.grow`. The ring-pipe fixtures stay under
  `examples/backend_ring`'s pattern.
- **Unit tests** move from the prototype's checks into GoogleTest. The
  existing grow-writer cases (`io.unit.grow`) carry over to `el_grow_buf`.
- **fmt tests:**
  - conversions against `/` and `%` (the existing 2×10⁶-value check);
  - printf on both targets with the existing literal expectations, minus
    the terminator and typed-call padding cases;
  - the glibc oracle at every capacity, for a buffer, and for a buffered
    writer over a writer that stalls after k bytes, for every k;
  - `fmt.libc` as above;
  - death tests for each violation.
- **Rust:** `rust_sketch.rs` calls the conversions and `el_buf_append`.
  `rust_alloc_sketch.rs` uses `el_grow_buf`.
- **Zephyr:**
  - the new tests run in the image;
  - the `variant` scenario switches to `FMT_PRINTF_LIBC`;
  - `allowed-undefined.FMT_LIBC.txt` becomes
    `allowed-undefined.FMT_PRINTF_LIBC.txt` (`snprintf`, `strchr`,
    `strlen`), and `memmove` joins the base list;
  - the symbol check on `qemu_cortex_m0` guards against `memset` and jump
    tables.

| Target | Toolchain | Level intended |
|---|---|---|
| Linux x86-64 | GCC, Clang, sanitizers | Run |
| Zephyr `mps2/an385` (ARMv7-M) | Zephyr SDK GCC | Symbol check, emulator run |
| Zephyr `qemu_cortex_m0`, `nucleo_g0b1re_renode` (ARMv6-M) | Zephyr SDK GCC | Symbol check; emulator run on the Renode board |

## Migration

**Code that changes.**

- **`modules/io`:**
  - `io_mem.h`/`.c` lose `el_mem_writer`.
  - New `io_buf.h`/`.c` and `io_buffered.h`/`.c`, and a private
    `io_private.h`.
  - `io_alloc.h`/`.c` become `el_grow_buf`.
  - `io.h`'s status table: `EL_ERR_NO_SPACE` is no longer "only from
    writers that allocate". `EL_ERR_END_OF_STREAM` no longer gives "its
    buffer is full" as its writer example.
  - Kconfig help, `CMakeLists.txt`, all tests, and the README.
- **`modules/fmt`:**
  - All of `fmt.h`, `fmt_printf.h` and `src/`.
  - New `fmt_printf_buffered.c` and `fmt_printf_libc.c`.
  - `fmt_digits_*.c` go.
  - Kconfig (`select IO`, the new choice); `DEPENDS status io`.
  - Tests and the README.
- **Elsewhere:**
  - `examples/consume_subdirectory/main.c`;
  - `tests/interop/rust_sketch.rs` and `rust_alloc_sketch.rs`;
  - `tests/integration/integration.py`;
  - `cmake/ElementalConfiguration.cmake` (a comment);
  - `configs/variant_defconfig`;
  - `tests/zephyr/allowed-undefined*.txt` and `testcase.yaml`.

**Docs that change.**

- `CHANGELOG.md`; `README.md`; `docs/build.md` (options, code size, test
  totals, the verified matrix).
- `docs/architecture.md`:
  - §6.3: buffering is no longer "deliberately absent";
  - the dependency graph gains `fmt → io`.
- `docs/conventions.md`: Errors rule 4 names `el_read_exact` and
  `el_write_all` as the only optional outputs. That stays true, because
  `el_buf_append` no longer has an optional output. No change is needed
  there.
- `docs/module-plan.md` (proposed [below](#roadmap-and-module-plan)).
- A note at the top of `docs/proposals/fmt.md` and `alloc.md`.
- Three ADRs to be written:
  - superseding 0011;
  - flush outside the operation table (provisional until M4);
  - private choices that widen a contract.
- `docs/roadmap.md` is the owner's.

**Implementation order.** Each step is one reviewable branch that passes
preflight on its own:

1. **`el_buf` and `el_grow_buf` replace both writers.**
   - Append, reserve/spare/commit, truncate, `data_mut`, and the endpoint.
   - Fixtures, the new suite capability, and tests.
   - `consume_subdirectory` and `rust_alloc_sketch`.
2. **`el_buffered_writer`** with decisions A and B.
   - Contract instantiations; `memmove` on the allowlist.
   - The flush ADR, provisional.
3. **`fmt` on `io`, built-in only.**
   - Conversions; printf on both targets.
   - Remove `el_fmt`, the hex dump and `FMT_DIGITS`.
   - The ADR superseding 0011.
   - Steps 3 and 4 may merge together, to avoid a moment with no libc
     option.
4. **`FMT_PRINTF_LIBC`.**
   - `fmt.libc`, the Zephyr variant scenario, the allowlist rename.
   - The widening ADR.
5. **The buffered reader**, proposed again and built with `frame` in M6
   (decision 14).

## Roadmap and module-plan

Proposed text for the owner to apply.

**Roadmap.** Add a milestone between M3 and M4:

> **M3a — Buffers and formatting, second pass.** Depends on M2, M3.
> `el_buf` and `el_grow_buf` replace `el_mem_writer` and `el_grow_writer`;
> the buffered writer with flush; `fmt` reduced to conversions, with printf
> into a buffer or a buffered writer; the platform option moved to printf.
> Acceptance: [ ] the proposal accepted and the three ADRs recorded (flush
> provisional); [ ] the io contract suite passes for both storages and the
> buffered writer, with `writer_full_after`; [ ] printf output identical on
> the host and on Zephyr Cortex-M3 and Cortex-M0+ in both printf options,
> for the portable subset; [ ] the Zephyr symbol check shows `memcpy` and
> `memmove` only by default; [ ] Example 2 runs on a Zephyr target against
> a writer that stalls.

Other roadmap changes:

- **M4** depends on M3a. Its acceptance gains: "the flush decision
  confirmed or reopened by the writer sink".
- **M6** gains the buffered reader, proposed with `frame`.
- **M7** shrinks to:
  - the flush-in-operations check with `frame`'s evidence;
  - any revision M4 and M6 show.

**Module-plan:**

- **Catalogue:** `fmt`'s hard dependencies become `status`, `io`.
- **The `fmt` entry:** replace "The core does not format into an
  `el_writer` … *Open:* … `el_mem_writer`" with:

  > The core is pure conversions into a caller's scratch array. printf
  > formats into an `el_buf` or an `el_buffered_writer` from `io`, which
  > know their free space, so a number is still whole or absent; it never
  > writes to a bare `el_writer`.
- **The io second-iteration entry:** the buffer and the buffered writer
  (this proposal); the buffered reader with `frame`; flush on the concrete
  type, per the provisional ADR.
- **"Where stream glue lives":** append:

  > `fmt` depends on `io` after all (owner, date): its output targets are
  > io buffers, and the glue holds real logic.
- **The dependency graph:** add `fmt --> io`. The first "no cycles" bullet
  becomes:

  > `fmt` writes into an io buffer; io does not depend on `fmt`, and
  > backends never log.

## Alternatives considered

| Alternative | Why rejected |
|---|---|
| **`el_buf` carrying the allocator** (the first draft: six words, `el_buf_deinit` in `io_buf.h`) | `deinit` had to name `el_free`, so every buffer user linked it. Fixed storage got a `deinit` whose misuse could not be detected. Replaced by `el_grow_buf` (decision C). |
| **A "when full" hook that also flushes**, making the buffered writer a buffer with a flush hook | It erases the endpoint/adapter distinction the owner asked for. Code holding an `el_buf *` could trigger I/O. |
| **`el_buf` as a queue with a read offset** (Go's `bytes.Buffer`) | Every reader of `el_buf_data` would handle an offset. The adapters keep their own `pos` instead. |
| **Adapters embed their storage**, with an init per storage kind | Combinatorial inits. Borrowing an `el_buf *` keeps storage orthogonal. |
| **A public "append what fits"** | The first draft had it. Its only callers are the endpoint and printf's text path, which use a private helper and spare/commit. A public partial append reports progress and status together, which was a conventions exception. |
| **printf into the buffered writer's buffer only, no streaming** | Output longer than the buffer would be cut |
| **Resumable printf** | A variadic function must `va_end` its list before it returns (C11 7.16.1.3) |
| **Sticky truncation flag in `el_buf`** | Decision 7: incoherent behind a buffered writer, which has already passed bytes on |
| **A latched error for every inner status** (Go's `bufio`) | The owner decided against it. `IO` and `NO_SPACE` are not permanent in io.h, and remembering them would turn a recoverable failure into a permanent one. Only `END_OF_STREAM` is remembered (decision 17). |
| **Mapping the inner `END_OF_STREAM` to `EL_ERR_IO`**, so that nothing needs remembering | Keeps io.h's rules without state, but tells the caller "the environment failed, try again" when the peer is gone for good. Recorded as the alternative in decision 17. |
| **Flush in `el_writer_ops` now** | Decision 5 |
| **The libc option delegating the portable subset too** | The first draft considered it. Identical output would rest on tests, a `%0Nd` with N ≥ 32 would differ at the edge of a buffer, and the portable subset would lose its interrupt safety under libc. |
| **Libc delegating the whole format to `vsnprintf`** | Cuts numbers and loses the item boundary |
| **Conversions that take an `el_buf`** | The core would depend on `io` and stop being trivially bindable |

## Evidence required before stable

The baseline in [api-stability.md](../process/api-stability.md), and:

- `log` (M4) formats through Example 1's shape or a buffered writer, and
  confirms or reopens the flush decision. A second consumer not written for
  this proposal uses `el_buf`.
- `frame` (M6) re-proposes and uses the buffered reader.
- The libc option ran on Zephyr with `PICOLIBC_IO_FLOAT` at least once.
- io's promotion still waits for async discovery (M9). Buffer ownership is
  added to what M9 must look at.

## Review: three seats

**Application author.**

| Friction found | Fixed by |
|---|---|
| Printing to a UART was a stack buffer and `el_write_all`, cut at the buffer's size | `el_fmt_printf_buffered` streams (Example 2) |
| A stalled line leaves half a line on the stream | Reserve first, and roll back with `el_buf_truncate` while nothing was passed on (Example 2) |
| A burst into a non-blocking stream is dropped | Grown storage absorbs it up to the allocator's limit (Example 2b) |
| Without sticky truncation, a later call can follow a dropped number | Chain on the status (Example 1) |
| A growing buffer and a fixed one needed different code | Both are an `el_buf *` (Example 3) |
| The newline on a full line needed truncate-then-append | `el_buf_data_mut` (Example 1) |

**Backend author.**

| Friction found | Fixed by |
|---|---|
| A UART writer that would-blocks forced every caller to keep resume state | The buffered writer keeps it. The UART writer stays a plain single-attempt `el_writer`. |
| Does a backend now implement flush? | No (decision 5, provisional) |
| Does the buffered writer keep io.h's rules over any inner writer? | Decision A, the contract suite over three inner writers, and the mutation table |

**Maintainer.**

| Friction found | Fixed by |
|---|---|
| `fmt` gains a dependency | Accepted by the owner. There is no cycle. |
| Fixed-buffer programs paid for the allocator and the buffered writer | The hook and the target pointer, 632 bytes measured (decision 12) |
| `el_buf_deinit` named `el_free` | `el_grow_buf` (decision C) |
| A private choice now changes what printf accepts | The widening ADR, and the portable subset by construction |
| `memmove`; a near-miss `memset` | Decision 11; the symbol check |
| Hard to change later | `el_buf`'s four words; the two printf targets; the item rule; flush outside the operation table |

Names: `el_buf_*`, `el_grow_buf_*`, `el_buffered_writer_*`,
`el_buffered_reader_*` and `el_fmt_*`. None equals a name in libedit's
`histedit.h`.

## Decisions for the owner

**Decided** (owner, 2026-11-13):

1. `el_buf` replaces `el_mem_writer` and `el_grow_writer`; `el_mem_reader`
   stays.
2. `el_buf` has no read offset.
3. A full memory buffer reports `EL_ERR_NO_SPACE`, not
   `EL_ERR_END_OF_STREAM`.
4. Adapters borrow an `el_buf *`.
5. Flush stays a function of `el_buffered_writer`. **Provisional until
   M4's writer sink exists.** Its ADR is to be written.
6. printf into a buffered writer streams, and stops at an item on a stall.
7. Truncation is per call, not sticky. This partly reverses ADR 0011.
8. The `fmt` core is three pure conversions. Width, zero-pad,
   `el_fmt_str`/`cstr` and the hex dump are dropped.
9. No NUL termination. This reverses fmt decision 1's termination rule.
10. The option moves to printf: `FMT_PRINTF_BUILTIN` / `FMT_PRINTF_LIBC`.
    The portable subset, decided by whole specification, is built-in code
    in both options. The widening ADR is to be written.
11. `memmove` joins the allowlist.
12. Growth is reached through `el_buf.grow`, and printf's target reserve
    through a pointer the entry point passes. This saves 328 + 304 bytes,
    measured, at the cost of one word in `el_buf` and none elsewhere.
    Revised by decision C.
13. Names: `el_buf`, `el_buf_append` (whole-or-nothing; "what fits" is
    private), `el_buf_data_mut`, `el_grow_buf`, `el_buffered_writer`,
    `el_fmt_printf_buffered`, `EL_FMT_INT_LEN_MAX`. Headers `io_buf.h`,
    `io_alloc.h`, `io_buffered.h`.
14. The buffered reader is provisional, proposed again with `frame`.
15. `fmt` depends on `io`. This reverses fmt decision 3 and module-plan's
    "Where stream glue lives".
- **A.** Buffered writer failures: one pass-on attempt, and only
  `WOULD_BLOCK` leads to buffering.
- **B.** Grown storage grows instead of waiting, bounded by the allocator.
- **C.** `el_buf` is four words, and grown storage is `el_grow_buf`.

**Still open:**

16. **Roadmap: an M3a before M4** containing steps 1–4, with the reader in
    M6 and M7 shrunk to match. *Recommended.* The owner applies it.
17. **How `EL_ERR_END_OF_STREAM` stays permanent without a latched error.**
    A buffered write that fits never calls the inner writer, so permanence
    needs one remembered fact.
    - *Recommended:* the sentinel `pos == SIZE_MAX`, set only on the inner
      writer's `END_OF_STREAM`, with no new field. `IO`, `NO_SPACE` and
      `WOULD_BLOCK` are never remembered.
    - *Alternative:* report the inner `END_OF_STREAM` from `write` as
      `EL_ERR_IO`, which needs no memory and is never permanent. Only
      `flush` would then return `END_OF_STREAM`. That loses the "peer is
      gone" meaning for holders of the handle.

    One related reading also needs confirming. "A write-through that takes
    nothing falls back to buffering what fits" is applied to
    `WOULD_BLOCK` only. A write-through that meets `END_OF_STREAM`, `IO`
    or `NO_SPACE` returns it, as the pass-on does. Buffering after
    `END_OF_STREAM` would accept bytes that can never leave.
18. **The status when a write finds no room** (stream stalled, allocator
    refused or storage fixed). *Recommended:* `EL_ERR_WOULD_BLOCK`. Space
    returns as the stream drains, which is io.h's definition, and the
    caller already handles it for any non-blocking stream.
    `EL_ERR_NO_SPACE` is kept for requests no draining can satisfy:
    `reserve(min)` beyond the capacity or the allocator.

## Open questions

1. **COBS and `peek(min)`.** A delimiter scan does not know its `min`. A
   `fill` primitive may suit `frame` better. Settled when the reader is
   proposed again in M6.
2. **A `%s` longer than the buffer, through a buffered writer,** is copied
   through the buffer in capacity-sized pieces. Writing it straight
   through when nothing is pending would save the copy. This is an
   implementation detail.
3. **The libc option's float output on picolibc** at
   `PICOLIBC_IO_LONG_LONG` has not been looked at. The contract says
   "whatever the C library writes".
4. **The libc option in a Zephyr build** is unmeasured. The image numbers
   above use the SDK's prebuilt picolibc, not Zephyr's.

## Sources

Pinned, read on 2026-11-13. The source facts above cite these.

- **Rust 1.90.0**, `github.com/rust-lang/rust` tag `1.90.0`:
  - `library/std/src/io/buffered/bufwriter.rs`:
    - 73–76 (`panicked`);
    - 145–150 (`into_inner`);
    - 365–376 (`write_cold`: flush first, then write-through at capacity);
    - 526–538 (`write`);
    - 651–653 (`flush`);
    - 681–687 (drop flushes, "we ignore a failed flush").
  - `library/std/src/io/mod.rs`:
    - 615–652 (`default_write_fmt`'s `Adapter`);
    - 1677, 1801 (`Write`; `flush` required);
    - 2308, 2344, 2361 (`BufRead`; `fill_buf`, `consume` required).
  - `library/core/src/fmt/mod.rs`:
    - 70–85 (`Error` carries no information);
    - 107;
    - 151 (`write_str`).
- **Go go1.23.0**, `github.com/golang/go` tag `go1.23.0`:
  - `src/bufio/bufio.go`:
    - 570–572 (errors are sticky);
    - 632 (`Flush` on `*Writer`);
    - 132–135 (`Peek`, `ErrBufferFull`).
  - `src/io/io.go`, 99–101.
  - `src/bytes/buffer.go`:
    - 126–155 (`grow`);
    - 229–247 (`growSlice`, at least doubles);
    - 44, 174 (`ErrTooLarge`).
- **Pigweed**, `github.com/google/pigweed` commit
  `1b610f4d8c1ba74808a552844474f3f620913a3f`:
  - `pw_stream/public/pw_stream/stream.h`, 147–151 and 172–177; no `Flush`
    in the file, and no buffered stream in the directory;
  - `pw_string/public/pw_string/string_builder.h`, 39–41, 130–133 and
    186–187;
  - `pw_string/public/pw_string/type_to_string.h`, 46–47.
- **Zephyr v4.5.0** (`41d10b59730`, local workspace):
  - `include/zephyr/sys/cbprintf.h`, 262–278;
  - `lib/os/cbprintf_complete.c`, 1387–1395;
  - `lib/libc/picolibc/Kconfig`, 40–63.
- **cppreference**, `std::format_to_n`, oldid 189663.
- **GCC 16.2.1 manual** (`gcc.info`), "Language Standards Supported by
  GCC": the freestanding environment must provide `memcpy`, `memmove`,
  `memset` and `memcmp`.
- **C11 (N1570):**
  - 6.7.2.1p15: a pointer to a structure, converted, points to its first
    member, and back;
  - 7.16.1.3: `va_end` before the function returns.
- **Elemental `main` at `33dca06`:**
  - `modules/alloc/include/elemental/alloc.h`: `el_layout`,
    `el_layout_make`, `el_alloc(a, layout)`, `el_free(a, ptr, layout)`;
  - [ADR 0013](../adr/0013-allocation-layout.md).
