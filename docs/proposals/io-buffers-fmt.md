# Proposal: buffers in io, and fmt reworked on them

Status: Draft, for the owner's review
Author, date: design pass, 2026-11-13

This changes two existing modules rather than adding one. **`io`** gets one
memory buffer type, `el_buf`, and two buffered adapters built on it. That
buffer replaces `el_mem_writer` and `el_grow_writer`. **`fmt`** shrinks to
pure number conversions. Its printf front end formats into an `el_buf` or a
buffered writer, so `fmt` now depends on `io`. The platform-formatter
option moves from digit generation to printf.

The direction is the owner's ([Problem](#problem)). This document develops
it, measures it, and challenges two points:

- the buffered reader should be built with `frame`, not now (decision 14);
- truncation should not be sticky across calls (decision 7).

Labels are as in [architecture.md](../architecture.md):

- **Source fact**: something read in a cited source ([Sources](#sources)).
- **Measured**: a number produced in this pass. The method is stated with it.
- Everything else is a recommendation.

Milestones are the [roadmap's](../roadmap.md). The decisions this would
reverse or replace:

- [fmt proposal decision 3](fmt.md#decisions) ("its own cursor");
- module-plan's ["Where stream glue lives"](../module-plan.md#3-decisions-that-must-come-first);
- [ADR 0011](../adr/0011-formatting-truncation.md);
- the [fmt amendment](fmt.md#amendment-2026-11-12) (`FMT_BUILTIN` / `FMT_LIBC`).

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
  digit generation to `snprintf`, behind a private split
  (`fmt_digits_builtin.c` / `fmt_digits_libc.c`). It saves 104 bytes in an
  image that links printf anyway ([modules/fmt](../../modules/fmt/README.md#configuration)).
  It does not give what the owner wanted: the platform's own formatting
  (floats, `%p`, flags) through Elemental's interface.

The owner approved this direction:

1. `io` owns buffers. One type has fixed or allocator-grown storage, with
   two append rules. A memory endpoint and a buffered adapter are different
   things that share it.
2. The `fmt` core becomes pure conversions into a caller's scratch array.
3. printf formats into an io buffer or a buffered writer. It keeps "a
   number is whole or absent", and `fmt` may depend on `io`.
4. The platform option moves to printf: built-in subset (the default), or
   libc, which hands each conversion to `snprintf`.

## The model in one table

Three things share one buffer. They must not be confused:

| | What it is | Where the bytes end up | Type |
|---|---|---|---|
| **Buffer** | Storage plus a length and two append rules. Not a stream. | In the buffer, for the owner to read | `el_buf` |
| **Memory endpoint** | A stream that ends in memory: an `el_writer` whose sink is a buffer | In the buffer | `el_buf_as_writer(&buf)` |
| **Buffered adapter** | A buffer in front of another stream. It passes bytes on (flush) or reads ahead (peek). | In the other stream | `el_buffered_writer`, `el_buffered_reader` |

Storage is orthogonal to all three:

- `el_buf_init(&b, storage, cap)`: fixed caller memory.
- `el_buf_init_alloc(&b, allocator)`: grown from an allocator.

An adapter borrows an `el_buf`, so it gets both storage kinds without an
init function for each.

printf targets the two things that know their free space: a buffer, and a
buffered writer. A bare `el_writer` does not know its free space, so printf
cannot keep "whole or absent" on it.

`el_mem_reader` stays as it is, a cursor over **const** bytes. A buffer's
storage is mutable, so folding the reader into `el_buf` would cast away
`const` on flash tables and received frames (decision 1).

## Usage examples

Each example compiled on 2026-11-13 with Clang 23.1.4
(`-std=c11 -Wall -Wextra -Wpedantic -Wformat=2 -Werror -fsyntax-only`)
against the header sketches below.

Examples 3 and 4 also ran on the host, linked against a throwaway prototype
of these headers, under AddressSanitizer and UndefinedBehaviorSanitizer:

- Example 4 parsed three frames fed one byte per call, with a would-block
  after each byte.
- Example 3 built a 28-byte message from 3 readings, and returned
  `EL_ERR_NO_SPACE` for 300 readings over its 2 KiB arena.

The prototype stays out of the tree, as the
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
    /* End the line even when it is full, over its last byte. */
    if (el_buf_room(&line) == 0) {
        el_buf_truncate(&line, el_buf_len(&line) - 1);
    }
    (void)el_buf_append_whole(&line, "\n", 1);

    const el_status a = el_write_all(out->console, el_buf_data(&line), el_buf_len(&line), NULL);
    const el_status b = el_write_all(out->file, el_buf_data(&line), el_buf_len(&line), NULL);
    return a != EL_OK ? a : b;
}
```

What changed from [fmt Example 1](fmt.md#example-1-a-log-line-into-a-stack-buffer-the-common-case):

- **The two calls are chained** (`if (st == EL_OK)`). Truncation is sticky
  within one printf call, not across calls (decision 7). The chain is what
  keeps `"count= ok"` from appearing after a dropped count.
- **No byte is held back** for a terminator, and none for the newline.
  `el_buf_truncate` overwrites the last byte instead.
- **Formatted once, sent twice.** The buffer does not care how many sinks
  read it.

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

/* From the main loop. Never blocks: a report that finds no room is dropped
 * whole, and one that is longer than LINE_MAX is cut at an item. */
void report(uint32_t uptime_s, const char *name, size_t name_len, int32_t temp_mc)
{
    /* Room for a whole line first, passing earlier lines on if need be.
     * After this, the printf below cannot reach the UART mid-line. */
    if (el_buffered_writer_reserve(&console, LINE_MAX) != EL_OK) {
        reports_dropped++;
        return;
    }
    (void)el_fmt_printf_buffered(&console, "%8" PRIu32 " %.*s %" PRId32 " mC\n", uptime_s,
                                 (int)name_len, name, temp_mc);
    (void)el_buffered_writer_flush(&console); /* what the FIFO takes now */
}

/* From the UART's "transmit FIFO has room" event, in the main loop. */
void console_poll(void)
{
    (void)el_buffered_writer_flush(&console);
}
```

This example is where the resumability problem gets settled
([below](#printf-into-a-buffered-writer-the-resumability-problem)). printf
into a buffered writer streams. When the buffer cannot take the next item,
printf passes buffered bytes on to make room. A line longer than the whole
buffer still goes out.

If the UART stalls mid-call, printf stops at an item boundary and returns
`EL_ERR_WOULD_BLOCK`. Everything it accepted stays buffered for the next
flush. The reserve-first pattern above makes each line all-or-nothing
whenever the line fits in `LINE_MAX`. It needs no new function.

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
    el_buf msg;
    el_buf_init_alloc(&msg, scratch); /* allocates nothing yet */

    el_status st = el_buf_append_whole(&msg, "{\"r\":[", 6);
    for (size_t i = 0; i < count && st == EL_OK; i++) {
        st = el_fmt_printf(&msg, "%s[%" PRIu16 ",%" PRId32 "]", i == 0 ? "" : ",",
                           r[i].sensor, r[i].value);
    }
    if (st == EL_OK) {
        st = el_buf_append_whole(&msg, "]}", 2);
    }
    if (st == EL_OK) { /* EL_ERR_NO_SPACE: the allocator ran out */
        st = publish(el_buf_data(&msg), el_buf_len(&msg));
    }
    el_buf_deinit(&msg); /* frees with the capacity it allocated */
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

The same printf call works on fixed and grown storage. A grown buffer
returns `EL_ERR_NO_SPACE` only when the allocator refuses.

[ADR 0012](../adr/0012-sized-free.md)'s hazard is to free with the length
instead of the capacity. That hazard stays inside `el_buf_deinit`, as it did
inside `el_grow_writer_deinit`.

The [alloc proposal](alloc.md#example-2-a-growable-buffer) has
`read_to_end`. With this API it reads straight into the buffer's spare room,
with no 64-byte bounce chunk:

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

### Example 4: a frame parser reading through a buffered reader with peek

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

A half-received frame lives in the reader, not in the caller. That is what
makes `EL_ERR_WOULD_BLOCK` cheap to handle here, and what "keeps the
would-block handling in one place", as the
[`frame` plan entry](../module-plan.md#named-by-the-owner) asks.

`EL_ERR_INVALID_DATA` does not exist yet (M6), so the example borrows
`EL_ERR_NO_SPACE` for an oversized length. That is honest only in the sense
that the frame does not fit the buffer.

This example is length-prefix framing. COBS, which scans for a delimiter,
fits `peek(min)` less well: see [open questions](#open-questions).

### From another language: the core, without printf

ABI rule 5 keeps variadics out of contracts, so a binding calls the
conversions and the buffer directly:

```c
el_status put_id(el_buf *out, uint32_t id)
{
    char digits[EL_FMT_INT_MAX];
    const size_t n = el_fmt_dec_u64(digits, sizeof digits, id);
    el_status st = el_buf_append_whole(out, "id=", 3);
    if (st == EL_OK) {
        st = el_buf_append_whole(out, digits, n);
    }
    return st;
}
```

```rust
let mut digits = [0u8; EL_FMT_INT_MAX];
let n = unsafe { el_fmt_dec_u64(digits.as_mut_ptr().cast(), digits.len(), u64::MAX) };
assert_eq!(&digits[..n], b"18446744073709551615");
```

A conversion has no object and no state, and it cannot fail. That is the
whole binding story for `fmt`.

## Backend example

There is no new interface, so there is no new backend. The adapters are
implementations of `el_writer` and `el_reader` that wrap another
implementation, and they run the io contract suite like any backend
([Verification](#verification)). What a platform writes for them is the
inner stream, such as Example 2's UART writer. That is an ordinary `io`
backend ([modules/io](../../modules/io/README.md#implementing-a-backend)).

## Why not something that exists

| Candidate | What it does | What is insufficient, or what was taken |
|---|---|---|
| Keep today's three types | — | The duplication in [Problem](#problem). printf cannot reach a stream. |
| Rust `BufWriter`, `BufRead` (1.90.0) | *Source fact:* a write that does not fit flushes first; a write at least the capacity goes straight to the inner writer; drop flushes and ignores errors. `fill_buf` and `consume` are required methods of the `BufRead` trait. `flush` is a required method of `Write`. | **Taken:** flush-then-buffer, write-through for large writes, and peek and consume. **Not taken:** flush on drop (C has no destructor, and ignoring errors is what Rust has to do there). **Not taken:** flush and peek in the interface. Rust can make flush a required method because every writer is written in one language with default methods. Here every wrapper would have to forward it ([decision 5](#flush-and-el_writer_ops)). |
| Rust `core::fmt::Write` (1.90.0) | *Source fact:* `write_str` returns `fmt::Error`, a unit struct with no information. `io::Write::write_fmt` keeps the real `io::Error` in an `Adapter` beside the formatter. | Same shape as this proposal. printf returns the stream's real status, which C can do directly, so no adapter is needed. |
| Go `bufio.Writer`, `bufio.Reader`, `bytes.Buffer` (go1.23.0) | *Source fact:* after a write error, `bufio.Writer` accepts nothing more, and `Flush` returns the error, forever. `Flush` is a method of the concrete type, not of `io.Writer`. `Peek` returns `ErrBufferFull` when n exceeds the buffer. `bytes.Buffer` grows to at least twice its capacity. It is a queue with a read offset, and it panics with `ErrTooLarge`. | **Taken:** flush on the concrete type, peek failing past capacity (here `EL_ERR_NO_SPACE`), and doubling growth. **Not taken:** the sticky error, because io's statuses already carry permanence (`EL_ERR_END_OF_STREAM` stays). **Not taken:** the read offset (decision 2). |
| Pigweed `pw_stream`, `pw_string` (`1b610f4`) | *Source fact:* `Writer` has no `Flush`. `Write` is all or nothing (`RESOURCE_EXHAUSTED`: "No data was written"). `StringBuilder` is always NUL-terminated, has a sticky status cleared by `clear_status()`, cuts strings, and never truncates numbers. There is no buffered stream in `pw_stream`. | Confirms whole-or-absent numbers and flush outside the interface. Its NUL and sticky status are what this proposal drops (decisions 7, 9). Write all-or-nothing contradicts io's partial progress, which is settled. |
| Zephyr `cbprintf` (v4.5.0) | *Source fact:* formats through a per-character callback `int (*)(int c, void *ctx)`. A negative return aborts formatting and is returned from `cbprintf()` (`include/zephyr/sys/cbprintf.h` 262–278; `lib/os/cbprintf_complete.c` 1387–1395). | It can print to a stream, but a stall leaves an arbitrary prefix: a number can be cut mid-digit. Zephyr only, and variadic. |
| C++ `std::format_to_n` | *Source fact (cppreference oldid 189663):* writes at most n characters and returns the untruncated size. | Cuts at any character. The untruncated size is the `snprintf` return value that [ADR 0011](../adr/0011-formatting-truncation.md) found no consumer for. Still none. |
| `snprintf` for everything | — | As in [the fmt proposal](fmt.md#why-not-something-that-exists): cuts numbers, returns count-or-negative, is not on every target, and is variadic. It survives here as one conversion at a time behind the libc option. |

## Interface

Header sketches. Each is a design, not final text. They follow
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

struct el_allocator_ops; /* from <elemental/alloc.h>, which this header does not need */

typedef struct el_buf {
    uint8_t *data;
    size_t len;
    size_t cap;
    void *alloc_ctx;                          /* grown storage: the allocator */
    const struct el_allocator_ops *alloc_ops; /* null for fixed storage */
    void (*grow)(struct el_buf *b, size_t min); /* set only by el_buf_init_alloc */
} el_buf;

/* Fixed storage: the `cap` bytes at `storage`, borrowed until the buffer is
 * no longer used. `storage` may be null only if `cap` is 0 (always full). */
void el_buf_init(el_buf *b, void *storage, size_t cap);

/* Release grown storage; nothing for fixed. The buffer must not be used
 * again until it is initialized again. Always safe to call. */
void el_buf_deinit(el_buf *b);

const uint8_t *el_buf_data(const el_buf *b); /* bytes [0, len); moves when grown */
size_t el_buf_len(const el_buf *b);
size_t el_buf_room(const el_buf *b);         /* free bytes now, without growing */

/* Append what fits: grows first if it can, then takes min(len, room) bytes.
 * EL_OK: all `len` appended. EL_ERR_NO_SPACE: *n < len were appended.
 * `n` is optional, as for el_write_all. */
el_status el_buf_append(el_buf *b, const void *data, size_t len, size_t *n);

/* Append whole or nothing. EL_OK, or EL_ERR_NO_SPACE with nothing appended. */
el_status el_buf_append_whole(el_buf *b, const void *data, size_t len);

/* Writing in place. reserve: make room >= min, growing if it can; EL_OK or
 * EL_ERR_NO_SPACE (nothing changed). spare: the first free byte; room()
 * bytes may be written there. commit: those n bytes are now part of the
 * contents (n <= room, asserted). */
el_status el_buf_reserve(el_buf *b, size_t min);
uint8_t *el_buf_spare(el_buf *b);
void el_buf_commit(el_buf *b, size_t n);

/* Keep the first `len` bytes (len <= el_buf_len, asserted). truncate(b, 0)
 * empties it; storage is kept. Undoes a partly formatted record. */
void el_buf_truncate(el_buf *b, size_t len);

/* The memory endpoint: an el_writer whose bytes end up in `b`. One attempt
 * appends what fits; EL_OK with 1 <= *n, or EL_ERR_NO_SPACE with *n == 0
 * when the buffer is full and cannot grow. Never would-block, never ends. */
el_writer el_buf_as_writer(el_buf *b);
```

`<elemental/io_alloc.h>` (only with `IO_ALLOC`):

```c
#include <elemental/alloc.h>
#include <elemental/io_buf.h>

/* Grown storage: starts empty and allocates nothing until the first append.
 * Grows to twice the capacity, or to what is needed if more, at least 16
 * bytes; asks once more for exactly what is needed if that is refused.
 * Allocates with (el_layout){.size = cap, .align = 1} and frees with the
 * same (ADR 0013). The allocator must outlive the buffer. */
void el_buf_init_alloc(el_buf *b, el_allocator allocator);
```

Notes on the buffer:

- **Allocation with the layout API.** These calls are written against the
  layout API being introduced in parallel (ADR 0013):
  - `el_alloc(a, layout)`;
  - `el_free(a, ptr, layout)`;
  - `(el_layout){cap, 1}`.

  Growth reuses today's grow-writer policy unchanged.
- **The allocator type is forward-declared.** The layout is the same in
  every configuration (ADR 0004). `io_buf.h` therefore needs no `alloc`
  header, and `io` still builds without `alloc` when `IO_ALLOC` is off.
- **`grow` is a field, not a direct call.** Growth is reached through a
  pointer that only `el_buf_init_alloc` sets. A program that never creates a
  grown buffer therefore links none of the allocator path, even with
  `IO_ALLOC` on. Measured on Cortex-M0, together with the same measure for
  printf's target: 392 bytes saved ([Cost](#dependencies-and-cost)).
  The cost is one word per buffer (24 bytes on a 32-bit target, against 12
  for `el_mem_writer`).
- **Read position.** There is none. The contents are always `[0, len)`
  (decision 2). Removing bytes from the front is private to `io`: the
  buffered writer uses it after a partial flush, and the reader uses it to
  compact before a peek. It costs a `memmove` (decision 11).
- **No terminator, no sticky state** (decisions 7, 9). A C string is
  `el_buf_append_whole(b, "", 1)`, and the terminator then counts in `len`.

### `<elemental/io_buffered.h>`: the buffered adapters

```c
#include <elemental/io.h>
#include <elemental/io_buf.h>

typedef struct el_buffered_writer {
    el_buf *buf;     /* borrowed: bytes accepted and not yet passed on */
    el_writer inner;
} el_buffered_writer;

/* `buf` is borrowed until the writer is no longer used; whatever it already
 * holds is passed on first. Fixed or grown storage both work. */
void el_buffered_writer_init(el_buffered_writer *w, el_writer inner, el_buf *buf);

/* Pass every buffered byte on: el_write_all to `inner`. EL_OK: the buffer
 * is empty. Otherwise `inner`'s status (EL_ERR_WOULD_BLOCK, _END_OF_STREAM,
 * _IO, _NO_SPACE); bytes passed on before it are gone from the buffer and
 * the rest wait. Resumable: call again. Never waits on its own. */
el_status el_buffered_writer_flush(el_buffered_writer *w);

/* Make room >= min, passing on only as many bytes as that needs. If `min`
 * exceeds the capacity, the buffer grows (grown storage) or this returns
 * EL_ERR_NO_SPACE (fixed). Otherwise as flush. */
el_status el_buffered_writer_reserve(el_buffered_writer *w, size_t min);

/* The adapter as a writer. A write that fits is buffered. One that does not
 * makes one attempt to pass buffered bytes on, then buffers what fits; with
 * an empty buffer, a write of at least the capacity goes straight to
 * `inner`. Progress or status: EL_OK with *n >= 1, or `inner`'s status
 * with *n == 0 when the buffer is full and `inner` took nothing. */
el_writer el_buffered_writer_as_writer(el_buffered_writer *w);

typedef struct el_buffered_reader {
    el_buf *buf;     /* borrowed: [pos, len) read ahead and not consumed */
    size_t pos;
    el_reader inner;
} el_buffered_reader;

/* `buf` is borrowed; bytes it already holds are read first. */
void el_buffered_reader_init(el_buffered_reader *r, el_reader inner, el_buf *buf);

/* Make at least `min` unconsumed bytes available, reading from `inner` as
 * many times as needed; *data and *len are always written with what is
 * available (valid until the next call on `r` or its buffer).
 *   EL_OK                 *len >= min.
 *   EL_ERR_NO_SPACE       min exceeds what the buffer can hold (fixed
 *                           storage, or the allocator refused).
 *   inner's status        WOULD_BLOCK, END_OF_STREAM, IO: *len < min, and
 *                           what was read stays buffered. Resumable.
 * min == 0 returns what is buffered without reading. */
el_status el_buffered_reader_peek(el_buffered_reader *r, size_t min,
                                  const uint8_t **data, size_t *len);

/* Drop `n` peeked bytes (n <= available, asserted). */
void el_buffered_reader_consume(el_buffered_reader *r, size_t n);

/* The adapter as a reader: copies buffered bytes; when empty, refills with
 * one attempt, or reads straight into the caller's buffer if it is at
 * least the capacity. Never EL_ERR_NO_SPACE. */
el_reader el_buffered_reader_as_reader(el_buffered_reader *r);
```

There is no `deinit` on either adapter: neither owns anything. Flushing
before the writer is dropped is the caller's job. C has no destructor, so
Rust's flush-on-drop is impossible here, and so is its silent error
dropping.

### `<elemental/fmt.h>`: pure conversions

```c
#include <stddef.h>
#include <stdint.h>

/* Bytes the longest conversion writes: "-9223372036854775808" and
 * "18446744073709551615" are 20. */
enum { EL_FMT_INT_MAX = 20 };

enum { EL_FMT_UPPER = 1 }; /* hex digits A-F */

/*
 * Write `value` at out[0..n) and return n (1 to 20). No sign but '-', no
 * padding, no prefix, no terminator. `cap` must be at least EL_FMT_INT_MAX
 * (asserted), so these cannot fail. The output equals C's printf for
 * PRIu64, PRId64, PRIx64 / PRIX64.
 */
size_t el_fmt_dec_u64(char *out, size_t cap, uint64_t value);
size_t el_fmt_dec_i64(char *out, size_t cap, int64_t value);
size_t el_fmt_hex_u64(char *out, size_t cap, uint64_t value, uint32_t flags);
```

These are interrupt-safe and reentrant without conditions. They have no
object, take no lock, do not allocate, and have no configuration. They
always use the shift-and-add conversion: the libc option no longer reaches
them (decision 10).

`cap` is redundant in C, where the caller writes `char d[EL_FMT_INT_MAX]`.
It is there so that a binding, or a caller who got the size wrong, hits an
assertion rather than an overrun.

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

/* Into a buffered writer: streams, passing bytes on when the buffer cannot
 * take the next item. EL_OK (all of it accepted; some may still be
 * buffered), EL_ERR_NO_SPACE (an item larger than the buffer can hold), or
 * the inner writer's status while making room. */
el_status el_fmt_printf_buffered(el_buffered_writer *out, const char *format, ...)
    EL_FMT_PRINTF_CHECK(2, 3);
el_status el_fmt_vprintf_buffered(el_buffered_writer *out, const char *format, va_list args)
    EL_FMT_PRINTF_CHECK(2, 0);
```

**The item rule** applies to both targets. The output is a prefix of what
unlimited room would hold:

- An **item** is one conversion, or one run of literal text.
- **A number is whole or absent.** That covers its sign, padding, digits,
  and, with the libc option, any prefix, decimal point and exponent. For a
  buffered writer, "absent" means the number never entered the buffer. Once
  it has, flushing may split it across writes, but the stream receives it
  whole unless the stream itself fails.
- **Text is cut at the last byte that fits.** This covers literal text and
  `%s` and `%c` with their padding.
- **Within one call, nothing follows the first item that did not fit.**
  Nothing is sticky across calls (decision 7).
- No terminator is written.

**The accepted formats** depend on the configuration (decision 10):

| | `FMT_PRINTF_BUILTIN` (default) | `FMT_PRINTF_LIBC` |
|---|---|---|
| Accepted | Today's portable subset: `d i u x X c s %`, flag `0` on integers, decimal width, `.N` / `.*` on `%s`, length `hh h l ll z` | The portable subset, plus every other C11 conversion (`o f F e E g G a A p`), flags `- + # space`, `*` width, precision on numbers, length `j t L` |
| Not accepted (contract violation, asserted) | Everything else | `%n`, `%lc`, `%ls` |
| Output | For every accepted format, exactly what `snprintf` writes given room | Portable subset: identical to built-in, by construction (the same code). The rest: what this C library's `snprintf` writes for that one conversion. |
| Who converts | `el_fmt_dec_*`, `el_fmt_hex_u64` | The portable subset: the same built-in code. Every conversion outside it: `snprintf`, one at a time. Text (`%s`, `%c`, `%%`, and the `-` flag and width on them) stays Elemental's, so the cut-at-a-byte rule is exact. |

## Flush and `el_writer_ops`

The roadmap requires this decision to be recorded "with the log example
that decided it" ([M7](../roadmap.md#m7--io-second-iteration)). The log
module does not exist yet. The example is therefore M4's writer sink as
[module-plan](../module-plan.md#named-by-the-owner) plans it, and as
[Example 1](#example-1-a-log-line-formatted-into-a-fixed-buffer-sent-to-two-sinks)
writes it.

**The test, from module-plan:** flush becomes an operation only if a
holder of a bare `el_writer` handle must flush.

**The log example does not need it.** The writer sink is handed an
`el_writer` (a UART, a descriptor). It formats each record into its own
`el_buf` and writes it with one `el_write_all`. So "one write per record"
holds without any buffered writer between the sink and the device, and
there is nothing to flush.

Where a buffered writer is used, as in Example 2, the code that created it
owns it and calls `el_buffered_writer_flush` on the concrete type. A sink
that wants batching owns its own buffered writer, made over the raw
`el_writer` it was given. Again, the flush is on a concrete type it holds.

**The case that would need it** is a writer that wraps another and is
itself handed out as a bare handle. An example is a future `frame` encoder
writer over a buffered UART, whose user must push a finished frame out. If
`frame` (M6) needs that, flush becomes an optional operation at the end of
`el_writer_ops`:

- a null operation means "nothing to flush" and returns `EL_OK`, as
  module-plan already says;
- `el_flush(el_writer)` is the front function;
- each wrapper forwards it.

This is decision 5. It needs an ADR (to be written): "Flush is a function
of the buffered writer, not an operation of `el_writer`". The
"revisit when" is the frame case above.

**Why not add it now anyway?** Rust made `flush` a required method of
`Write` (source fact). Every in-memory and wrapping writer then has to
implement it, and every adapter has to forward it.

Here, adding it would mean:

- a new optional operation;
- a front function;
- a contract-suite capability;
- forwarding in every wrapper;
- a non-blocking flush contract ("progress or status" for flush) that
  nothing in the tree exercises.

Go keeps `Flush` off `io.Writer`. So does Pigweed's `Writer` (source facts).

## printf into a buffered writer: the resumability problem

[`fmt.h`](../../modules/fmt/include/elemental/fmt.h) explains why it never
writes to a stream: "a stream can stall halfway through a number, and a
formatter cannot resume there". That stays true. A printf call holds a
`va_list` and a position in the format string. Neither can survive the
return to the caller, so a printf call cannot be resumed.

What changes is that the formatter no longer writes to the stream. It
writes into a buffer that knows its free space, and only whole items enter
it.

1. Before each item, printf asks for room: all of a number, or at least
   one byte of text.
2. If the buffered writer lacks room, it passes buffered bytes on with
   single attempts, only until there is room
   (`el_buffered_writer_reserve`).
3. If the inner writer stalls (`EL_ERR_WOULD_BLOCK`) or fails, printf
   stops before the item. A text item keeps the part that fit. The call
   returns that status.
4. Everything accepted is in the buffer or already passed on. It is a
   prefix of the full output that ends on an item. **Resuming is resuming
   the flush**, which `el_buffered_writer_flush` already supports. The rest
   of the format is dropped, as truncation drops it in a fixed buffer.

The prototype showed it. `"t=%u h=%u p=%u\n"` went into an 8-byte buffer in
front of a writer that takes 3 bytes per call and stalls after 10. The
stream received `"t=1234567 "`, the buffer kept `"h=89 p="`, and the call
returned `EL_ERR_WOULD_BLOCK`. The third number was absent, not cut.

The case left: **a line that stalls mid-way leaves the stream mid-line**,
and the next line would join it. Two remedies, both shown in the examples:

- **Reserve first.** `el_buffered_writer_reserve(&w, LINE_MAX)` before the
  printf (Example 2). Then a line of at most `LINE_MAX` bytes never reaches
  the stream mid-line. A stall drops it whole.
- **Format into an `el_buf` first, then write** (Example 1). The record is
  then the unit, and `el_write_all` is resumable at any byte.

A rollback ("undo this printf") is impossible once bytes have been passed
on. For a plain buffer it is `el_buf_truncate(b, mark)`.

## Where the truncation rule lives

The item rule (numbers whole or absent, text cut at a byte) belongs to
printf, in `fmt_printf.h`. It applies to whichever of the two targets is
given.

The buffer offers the two primitive rules the item rule is built from:

- "append what fits" (`el_buf_append`), for text;
- "append whole or nothing" (`el_buf_append_whole`, or `el_buf_reserve`
  then commit), for numbers.

The buffer has no notion of an item, so the rule cannot drift into `io`.

ADR 0011 cannot simply be amended. It records that formatting "does not
depend on `io`", sticky truncation across calls, and NUL termination, and
this proposal reverses all three. **An ADR to be written supersedes it**,
with this draft:

> **Formatting appends to an io buffer; output is a prefix that ends on an
> item.** printf formats into an `el_buf` or an `el_buffered_writer`. Each
> knows its free space, so an item enters only if it fits. When it does not
> fit, the call returns `EL_ERR_NO_SPACE`, or the inner writer's status for
> a buffered writer. What was accepted is a prefix of the unlimited output:
> numbers whole or absent, text cut at the last byte, nothing after the
> first item that did not fit, within the call. Nothing is sticky across
> calls. Callers that compose several calls check each status or roll back
> with `el_buf_truncate`. No terminator is written. *Cost:* the
> "check only the last call" convenience of ADR 0011 is gone; a buffered
> stream that stalls mid-line is left mid-line unless the caller reserved
> the line first. *Revisit when:* the same as ADR 0011's, plus a consumer
> that composes many calls and keeps getting the chain wrong.

## What `fmt` drops and keeps

| Today | After | Why |
|---|---|---|
| `el_fmt`, `el_fmt_init`, `el_fmt_len` | Gone; `el_buf` | The cursor was the duplicate |
| `el_fmt_str`, `el_fmt_cstr` | Gone; `el_buf_append` (cut at a byte) or `el_buf_append_whole` | Appending bytes is the buffer's job. `cstr` saved a `strlen`, which the caller has. |
| `el_fmt_dec_u64`, `_i64`, `el_fmt_hex_u64` with `width`, `flags` | Kept as pure conversions, without width or zero-pad. `EL_FMT_UPPER` stays. | Padding needs a destination with free space, which a scratch array is not. printf keeps width and `0`. A binding pads in its own language. |
| `el_fmt_hex_bytes` | Dropped (decision 8) | No consumer outside fmt's own example and tests. It is a four-line loop over `el_fmt_printf(b, i ? " %02x" : "%02x", p[i])`. It returns with a consumer (a hex-dump log helper in M4, say), as a function on `el_buf` in `fmt`. |
| NUL termination | Dropped (decision 9) | Lengths never include a terminator (conventions, Storage rule 4). Rust and `el_write_all` never wanted it. It cost every buffer a byte. |
| Sticky truncation across calls | Per call only (decision 7) | A buffer used as a stream endpoint, or behind a buffered writer, cannot be "sticky until reset" in any useful sense |
| `el_fmt_printf(el_fmt *, ...)` | `el_fmt_printf(el_buf *, ...)` plus the `_buffered` pair | Targets that know their free space |
| `FMT_BUILTIN` / `FMT_LIBC` (digits) | `FMT_PRINTF_BUILTIN` / `FMT_PRINTF_LIBC` | [Below](#the-platform-option-on-printf) |
| `EL_FMT_PRINTF_CHECK` | Kept | — |

## The platform option on printf

**Built-in** (default) is today's parser and subset, retargeted. **Libc**
works like this:

- Elemental's parser still walks the format and fetches each argument with
  the type its conversion names.
- A conversion in the portable subset takes the built-in path, so its
  output cannot differ between the options.
- Every other numeric conversion's spec (`%-+08.3f`, say), with `*` values
  written in, goes to `snprintf` with that one argument.
- The result enters the buffer whole, or not at all.
- A `%s` or `%c` stays Elemental's.

The libc option therefore no longer avoids a second copy of the digit code,
which was the only measured benefit of today's `FMT_LIBC` (−104 bytes where
printf is linked anyway). That is given up on purpose: identical output for
the common set is then true by construction rather than by test.

Handing the whole format to `vsnprintf` would cut numbers and lose the item
boundary. That was already the reason in the
[fmt amendment](fmt.md#amendment-2026-11-12).

Details that a reviewer will ask about:

- **Fitting.** `snprintf` writes into a 32-byte stack scratch first. If the
  result fits there, it is reserved and copied, so it is exact.

  A longer result (a wide field, or a `%f` of a large double) is formatted
  again straight into the buffer's spare room after reserving its length
  plus one, because `snprintf` must write a terminator. So in libc mode, an
  extended conversion of 32 bytes or more needs **one more free byte** than
  its length to be written. It concerns only conversions that built-in does
  not accept, so rule 2 below still holds. Decision 10 accepts it; the
  alternative is a scratch as large as any conversion, which has no bound.
- **Probe.** `FMT_PRINTF_LIBC` depends on the existing `PLATFORM_SNPRINTF`
  (it converts `long long`). Floats are whatever the C library does: with
  picolibc at `PICOLIBC_IO_LONG_LONG` that is not a real float conversion.
  That is the platform's behavior, reported as it is, and the contract says
  so ("what this C library's `snprintf` writes").
- **Execution context.** In libc mode, printf is exactly as safe in an
  interrupt or signal handler as the C library's `snprintf`. The note moves
  from `fmt.h` to `fmt_printf.h`. The conversions in `fmt.h` become
  unconditionally safe.

**[ADR 0004](../adr/0004-private-configuration.md).** Signatures, layouts
and macros stay identical in every configuration, so the letter of the ADR
holds. The spirit is stretched: a format that is correct under libc is a
contract violation under built-in. Today's private choices change *how*
(`CRC_*`, `FMT_DIGITS`). This one changes *what is accepted*.

**An ADR to be written** settles it: "A private choice may widen what a
function accepts, never change it". It has five rules:

1. The default option accepts the least.
2. For every input that every option accepts, the output is identical, and
   tested identical in each configuration.
3. Inputs outside the default's set are contract violations under it,
   asserted where the check is cheap (here it is the parser's).
4. The public header documents each set and names the option.
5. Code that must build against any configuration (a library over `fmt`,
   `log` itself) uses only the common set.

It amends ADR 0004 rather than replacing it.

It is contracted and tested like this:

- The header's subset table above.
- `fmt.unit` (portable subset, literal expectations) runs in both
  configurations and must give identical bytes.
- A new `fmt.libc` suite runs only with `FMT_PRINTF_LIBC`:
  - on the host, each extended conversion is checked against glibc
    `snprintf` and the prefix rule at every capacity;
  - on Zephyr, the same runs against picolibc, with float cases only where
    the test sees `CONFIG_PICOLIBC_IO_FLOAT`.
- `fmt.violations` asserts `%f` under built-in and `%n` under libc.
- `integration.choices` builds both, as today.

## Dependencies and cost

| | |
|---|---|
| Elemental modules | `io`: `status`; `alloc` with `IO_ALLOC` (unchanged). `fmt`: `status`, **`io`** (new). |
| Runtime symbols (freestanding) | `memcpy` (as today) and **`memmove`** (new, for compaction). `snprintf` with `FMT_PRINTF_LIBC`. |
| Allocation | Only `el_buf_init_alloc` buffers, through the allocator they are given |
| Per-object storage (32-bit) | `el_buf` 24 bytes; `el_buffered_writer` 12; `el_buffered_reader` 16. Was: `el_mem_writer` 12, `el_grow_writer` 20, `el_fmt` 16. |
| New status codes | None |

**`memmove`.** *Source fact* (GCC 16.2.1 manual, "Language Standards
Supported by GCC"): "GCC requires the freestanding environment provide
`memcpy`, `memmove`, `memset` and `memcmp`". It is therefore the same kind
of dependency as `memcpy`, which the allowlist already justifies this way.
It joins `tests/zephyr/allowed-undefined.txt` (decision 11).

**Code size. Measured** on 2026-11-13 with the Zephyr SDK's GCC 14.3.0
(`arm-zephyr-eabi`):

- **Flags:** `-Os -mthumb -ffreestanding -fno-jump-tables
  -ffunction-sections`, assertions trapping.
- **What was compiled:** each source compiled alone. This is not a Zephyr
  build.
- **Checking the method:** the same commands for Cortex-M3 reproduce
  [build.md](../build.md#code-size-of-the-assertion-configurations)'s
  figures:
  - `fmt` 1 705 bytes, exactly;
  - the grow writer 332, exactly;
  - `io` without it 574 (build.md records 578).
- **The prototype** is functional but not tuned. Its numbers are an upper
  estimate for an implementation written with the repository's care.

Library objects, text bytes, Cortex-M0 (ARMv6-M):

| Today | Bytes | Proposed (prototype) | Bytes |
|---|---|---|---|
| `io.c` | 328 | `io.c` (unchanged) | 328 |
| `io_mem.c`: reader 122, writer 116 | 238 | `io_mem.c`, reader only | ~122 |
| `io_alloc.c` (grow writer) | 342 | `io_buf.c` (fixed and grown buffer, endpoint) | 640 |
| — | — | `io_buffered.c` (writer 336, reader 336, both adapters) | 660 |
| `fmt.c` (cursor, typed calls, hex dump) | 775 | `fmt.c` (three conversions) | 510 |
| `fmt_digits_builtin.c` | 398 | (inside `fmt.c`) | — |
| `fmt_printf.c` | 704 | `fmt_printf.c` (both targets) | 1 144 |
| **Total** | **2 785** | **Total** | **3 404**, of which 660 is the new buffered adapters |

Linked images on Cortex-M0, measured with `--gc-sections`, a
`memcpy`/`memmove` stub, and nothing else:

| Program | Today | Proposed |
|---|---|---|
| `printf` of `"%10lu %s: %d\n"` into a 64-byte stack buffer, then `el_write_all` | 1 759 | 1 959 (+200), with growth and the buffered-writer path reached through pointers |
| The same, with growth and the buffered writer called directly from the buffer and printf | — | 2 351 (+592) |
| The same line printed through a 64-byte buffered writer and flushed (streams; today has no equivalent) | — | 2 135 |

What the numbers say:

- The pointers (`el_buf.grow`, and printf's target holding its `reserve`
  function) are what keep a fixed-buffer program from paying for the
  allocator and the buffered writer. They are recommended (decision 12).
- The remaining +200 bytes are the conversions writing into a scratch
  array and being copied, plus the out-of-line buffer accessors that printf
  calls across the module boundary. That is the price of the layering.

Not measured:

- the libc printf option;
- assertions off and the application handler;
- Cortex-M3 images;
- cycle counts.

[build.md's](../build.md#code-size-of-the-assertion-configurations) table is
rewritten from `code_size.sh` when the implementation lands.

## Contracts

- **Ownership and lifetime.**
  - The caller owns every object. An `el_buf` borrows fixed storage until
    it is no longer used. A grown buffer owns its storage until
    `el_buf_deinit`.
  - An adapter borrows its `el_buf` and its inner handle for as long as it
    is used.
  - `el_buf_data`, `el_buf_spare` and a peek's `*data` are valid until the
    next call that can append, grow, compact or consume.
  - An object may move while no handle made from it is in use, and while
    no adapter borrows it (conventions, Storage rule 3).
- **Errors.**
  - The buffer: `EL_ERR_NO_SPACE` only, and never permanent. For a fixed
    buffer, room returns after `el_buf_truncate`. For a grown one, also
    when the allocator has room again.
  - The adapters: the inner stream's statuses unchanged, plus
    `EL_ERR_NO_SPACE` where a request exceeds the buffer.
  - printf: the target's statuses, by the item rule.
- **Concurrency.** None internal. One object, one context. An adapter and
  the `el_buf` it borrows count as one object.
- **Execution context.**
  - Fixed `el_buf` and the conversions: interrupt-safe (no blocking, no
    allocation, no lock), under the one-context rule.
  - Grown `el_buf`: wherever its allocator may be used.
  - Adapters: whatever the inner stream allows. They add no waiting.
  - printf: built-in into a fixed buffer is interrupt-safe; libc is as safe
    as `snprintf`.
- **Contract violations (asserted).**
  - Null objects and handles; null storage with a nonzero capacity.
  - Null data with a nonzero length.
  - `commit` past the room; `truncate` past the length; `consume` past
    what is available.
  - A conversion `cap` below `EL_FMT_INT_MAX`; unknown flag bits.
  - A null format, a null `%s` argument, a conversion outside the
    configuration's set.
  - Use after `el_buf_deinit`.
- **Exceptions to [conventions.md](../conventions.md).** Two:
  - `el_buf_append` reports progress and a status together, like
    `el_write_all`. It is a loop in effect, and it says so. The single-attempt
    form is the endpoint's `write`.
  - `el_buffered_reader_peek` returns data with a non-`EL_OK` status, by
    the same rule.

## Interface model

The buffer and the adapters are concrete types (mechanism 1 of
[ADR 0002](../adr/0002-interface-model.md)). Each has `_as_writer` /
`_as_reader` to enter the existing handles. Nothing new varies by platform,
so nothing new is an interface. Any number of buffers and adapters coexist,
and adapters stack: a buffered writer over a buffered writer is legal and
pointless.

Build time decides `IO_ALLOC` (whether grown buffers exist) and the printf
option. Everything else is per instance: storage kind, capacity, inner
stream.

## Configuration

| Option | Default | Use case | Why not the linker / a parameter |
|---|---|---|---|
| `IO_ALLOC` (unchanged meaning, new help) | y | Grown buffers; the only part of `io` that needs `alloc` | A dependency edge, not a size question: the linker cannot drop a library dependency |
| `FMT` | y, now `select IO` | — | — |
| Choice `FMT_PRINTF`: `FMT_PRINTF_BUILTIN` (default) / `FMT_PRINTF_LIBC`, replacing `FMT_DIGITS` | builtin | Libc: the image already links the C library's printf, or needs floats, `%p` or flags, through Elemental's truncation rule | It changes what a function accepts, decided once per image. A parameter would put the variadic C library on every call path. The option is renamed so that an old defconfig naming `FMT_LIBC` gets the default and a Kconfig warning, not silently wider semantics. |

The buffered adapters get no option: the linker leaves them out of a
program that does not call them.

## Verification

- **io contract suite.**
  - The suite gains one optional capability, `writer_full_after(room)`: a
    writer that takes exactly `room` bytes and then reports
    `EL_ERR_NO_SPACE` until storage is released, with a test that it keeps
    what fitted.
  - Today a writer that fills up with `EL_ERR_NO_SPACE` is covered only by
    skips. Both buffer fixtures (fixed, grown) provide the capability.
- **Implementations it runs against**, replacing `io.contract.mem` and
  `io.contract.grow`:

| Implementation | Expected, of 12 tests (not run) | Change |
|---|---|---|
| `el_mem_reader` + fixed `el_buf` | 9 run, 3 skipped (cannot stall, twice; does not end) | Was 9 / 2 of 11, with the writer ending. The ending test is now skipped; the new full test runs. |
| `el_mem_reader` + grown `el_buf` over the checking allocator | 9 run, 3 skipped | Was 8 / 3 of 11 |
| Buffered writer over the ring pipe example (reader side: a memory reader until step 5) | 10 run, 2 skipped (stalls instead of ending; never full) | New. It can stall, because the inner can. |
| Buffered writer over a scripted stream that also ends | The ending test runs | New |

- **Unit tests.**
  - The buffer: growth sizes, the exact-size retry, append versus
    append-whole at every boundary, reserve/spare/commit, truncate, and
    every buffer freed with its capacity under the checking allocator.
    These are the existing `io.unit.grow` cases, moved.
  - The adapters: write-through threshold; flush resuming after each of
    would-block, end and error at every byte; reserve passing on only what
    it needs; peek at, below and above capacity; compaction; reading
    through after peek.
- **fmt.**
  - Conversions against `/` and `%` (the existing 2×10⁶-value check).
  - printf on both targets with the existing literal expectations,
    rewritten with the terminator and padding-on-typed-calls cases removed.
  - The glibc oracle by the prefix rule at every capacity, for a buffer
    and for a buffered writer over a writer that stalls after k bytes, for
    every k.
  - The mutation checks from M3 (divide, whole-or-absent, parser) carry
    over. The sticky and terminator mutations go with their features.
- **Death tests** for each violation listed in Contracts.
- **Rust.**
  - `rust_sketch.rs` calls the conversions and `el_buf_append_whole`
    (above).
  - `rust_alloc_sketch.rs` uses `el_buf_init_alloc` in place of
    `el_grow_writer`.
- **Zephyr.**
  - The new tests are registered with `elemental_add_test()` and run in
    the image.
  - The `variant` scenario switches to `FMT_PRINTF_LIBC`.
  - `allowed-undefined.FMT_LIBC.txt` becomes
    `allowed-undefined.FMT_PRINTF_LIBC.txt`, and `memmove` joins the base
    list.
  - The symbol check on `qemu_cortex_m0` is what shows that GCC emitted no
    other helper. Today `-fno-jump-tables` on `fmt` exists for that reason
    and stays.

| Target | Toolchain | Level intended |
|---|---|---|
| Linux x86-64 | GCC, Clang, sanitizers | Run |
| Zephyr `mps2/an385` (ARMv7-M) | Zephyr SDK GCC | Symbol check, emulator run |
| Zephyr `qemu_cortex_m0`, `nucleo_g0b1re_renode` (ARMv6-M) | Zephyr SDK GCC | Symbol check; emulator run on the Renode board |

## Migration

**Code that changes.**

- `modules/io`:
  - `io_mem.h`/`.c` lose `el_mem_writer`.
  - New `io_buf.h`/`io_buf.c` and `io_buffered.h`/`io_buffered.c`.
  - `io_alloc.h`/`.c` become `el_buf_init_alloc` and the growth path.
  - `io.h`'s status table: `EL_ERR_NO_SPACE` is no longer "only from
    writers that allocate". `EL_ERR_END_OF_STREAM` no longer gives "its
    buffer is full" as its writer example.
  - Kconfig help, `CMakeLists.txt`, every test file in `tests/`, and the
    README.
- `modules/fmt`:
  - All of `fmt.h`, `fmt_printf.h` and `src/`.
  - `fmt_digits_*.c` and `fmt_private.h`'s digits split go.
  - `fmt_printf_libc.c` is new.
  - Kconfig (`select IO`, the new choice); `CMakeLists.txt` (`DEPENDS
    status io`).
  - All tests and the README.
- `examples/consume_subdirectory/main.c` (uses `el_mem_writer`).
- `tests/interop/rust_sketch.rs`, `rust_alloc_sketch.rs`.
- `tests/integration/integration.py`:
  - expected suite summaries;
  - the `IO_ALLOC` comment;
  - choices discovery picks up the renamed choice.
- `cmake/ElementalConfiguration.cmake` (comment naming `FMT_LIBC`).
- `configs/variant_defconfig`.
- `tests/zephyr/allowed-undefined*.txt` and `testcase.yaml`.

**Docs that change.**

- `CHANGELOG.md`: every public change above.
- `README.md` module table.
- `docs/build.md`: the options table, code size, test totals and the
  verified matrix, from the run that lands each step.
- `docs/architecture.md`:
  - §6.3: buffered reading and flush are no longer "deliberately absent".
  - The dependency graph gains `fmt → io`.
  - The "Logic, separated from effects" text, if it cites fmt.
- `docs/module-plan.md`, as proposed [below](#roadmap-and-module-plan).
- `docs/proposals/fmt.md` and `alloc.md`: a note at the top pointing here,
  as `alloc.md`'s "As accepted and implemented" does. No rewriting of
  history.
- `docs/adr/`, three records to be written:
  - superseding 0011;
  - flush outside the operation table;
  - private choices that widen a contract.
- `docs/roadmap.md` is the owner's.

**Implementation order**, each step one reviewable branch that passes
preflight on its own:

1. **`el_buf` replaces both writers.**
   - Fixed and grown storage, append, append-whole, reserve, spare,
     commit, truncate and the endpoint writer.
   - Fixtures, tests and the new contract capability.
   - `consume_subdirectory` and `rust_alloc_sketch`.
   - `fmt` is untouched.
2. **`el_buffered_writer`.**
   - Flush and reserve, contract instantiations, `memmove` on the
     allowlist.
   - The flush ADR.
3. **`fmt` on `io`, built-in only.**
   - Pure conversions; printf on both targets; `el_fmt` and the hex dump
     removed; `FMT_DIGITS` removed.
   - The ADR superseding 0011; `rust_sketch`.
   - Between steps 3 and 4 there is briefly no libc option. That is
     acceptable on `main` for one merge, or 3 and 4 merge together.
4. **`FMT_PRINTF_LIBC`.**
   - The libc delegation and the `fmt.libc` suite.
   - The Zephyr variant scenario and the allowlist rename.
   - The ADR on widening choices.
5. **`el_buffered_reader`**, with `frame` in M6 (decision 14). Its design
   is fixed here so that step 1's `el_buf` already serves it.

## Roadmap and module-plan

Proposed text for the owner to apply. This document does not edit them.

**Roadmap.** A milestone between M3 and M4:

> **M3a — Buffers and formatting, second pass.** Depends on M2, M3.
> `el_buf` replaces `el_mem_writer` and `el_grow_writer`; the buffered
> writer with flush; `fmt` reduced to conversions, with printf into a
> buffer or a buffered writer; the platform option moved to printf.
> Acceptance: [ ] the proposal accepted and the three ADRs recorded;
> [ ] io contract suite passes for both buffer storages and the buffered
> writer, with the new `writer_full_after` capability; [ ] printf output
> byte-identical on the host and on Zephyr Cortex-M3 and Cortex-M0+ in both
> printf options, for the common subset; [ ] the Zephyr symbol check shows
> `memcpy` and `memmove` only by default; [ ] Example 2 runs on a Zephyr
> target against a writer that stalls (QEMU UART or a scripted writer).

Other roadmap changes:

- **M4** depends on M3a, and its writer sink uses Example 1's shape.
- **M7** shrinks to:
  - the buffered reader with peek, built with `frame`, or moved into M6
    outright;
  - the flush-in-operations check with `frame`'s evidence;
  - any revision M4 and M6 show.

  The acceptance item "the decision on flush in the operation table is
  recorded, with the log example" moves to M3a, as its ADR.

**Module-plan.**

- **Catalogue:** the `fmt` row's hard dependencies become `status`, `io`.
  The `io`, second iteration row names `fmt` printf as its first consumer,
  beside `log` and `frame`.
- **The `fmt` entry:** replace "The core does not format into an
  `el_writer` … *Open:* whether to use its own small cursor or
  `el_mem_writer`" with:

  > The core is pure conversions into a caller's scratch array. printf
  > formats into an `el_buf` or an `el_buffered_writer` from `io`, which
  > know their free space, so a number is still whole or absent; it never
  > writes to a bare `el_writer`.
- **The `io`, second iteration entry:** the buffer, the buffered writer
  (this proposal), and the buffered reader (designed here, built with
  `frame`). Flush on the concrete type, per the ADR.
- **"Where stream glue lives":** append:

  > `fmt` depends on `io` after all (owner, date): its output targets are
  > io buffers, and the glue holds real logic, item-wise fitting and making
  > room by flushing.
- **The dependency graph:** add `fmt --> io`. The "no cycles" paragraph's
  first bullet becomes:

  > `fmt` writes into an io buffer; io does not depend on `fmt`, and
  > backends never log.

## Alternatives considered

| Alternative | Why rejected |
|---|---|
| **A "when full" hook in `el_buf`** (fixed, grow, or flush), making the buffered writer just a buffer with a flush hook. One printf entry point. | It erases the distinction the owner asked for: `el_buf_data` on a flushing buffer would return bytes that are about to leave, and any code holding an `el_buf *` could trigger I/O. Two printf entry points are cheaper than that ambiguity. |
| **`el_buf` as a queue with a read offset** (Go's `bytes.Buffer`), with a reader handle | Every reader of `el_buf_data` would handle an offset, and append would need a compaction policy. A reader over a buffer someone else may append to cannot keep `EL_ERR_END_OF_STREAM` permanent. The two adapters that need front removal keep their own offset or compact privately. |
| **Fold `el_mem_reader` into `el_buf`** | Loses `const`: a reader over a flash table would need a cast (decision 1) |
| **Adapters embed their storage**, with `_init` and `_init_alloc` each | Four init functions for two adapters, and a fifth combination as soon as a third adapter exists. Borrowing an `el_buf` keeps storage orthogonal, at the cost of one more object to keep alive (decision 4). |
| **printf into the buffered writer's buffer only, no streaming** | Output longer than the buffer would be cut. A 300-byte `%s` into a 64-byte UART buffer would print 64 bytes, which is not what "printf to a UART" means. Streaming costs one `reserve` function. |
| **Resumable printf** (save the format position and a `va_copy`) | A variadic function must `va_end` its list before it returns (C11 7.16.1.3), so the arguments cannot outlive the call. Resuming would need the arguments captured into a package, which is Zephyr's `cbprintf_package`: a different, larger design with no consumer. |
| **Sticky truncation flag in `el_buf`** | Decision 7. For a buffer behind a buffered writer, "refuse everything until reset" has no reset that makes sense, because bytes already left. For a plain buffer it saves a chain of `if`s that bindings write anyway (Rust's `?`). |
| **Keep NUL termination as a buffer option** | An option for one byte, with a length rule that differs by option. Callers who need a C string append one. |
| **Flush in `el_writer_ops` now** | Decision 5 |
| **Libc option delegating the whole format to `vsnprintf`** | Cuts numbers and loses the item boundary. Already rejected by the fmt amendment. |
| **Keep the digits split as well as the printf option** (two choices) | The digits split's measured value is ±104 / 1 268 bytes. It was a stand-in for the platform option the owner wanted. Two choices for one module doubles `integration.choices` for no consumer. |
| **Conversions that take a destination `el_buf`** (`el_fmt_dec_u64(el_buf *, value)`) | The core would then depend on `io` too, and stop being trivially bindable and interrupt-safe with no conditions. One `el_buf_append_whole` after a scratch conversion costs a 20-byte copy. |

## Evidence required before stable

The baseline in [api-stability.md](../process/api-stability.md), and:

- `log` (M4) formats through Example 1's shape or through a buffered
  writer, and a second consumer not written for this proposal uses
  `el_buf`.
- `frame` (M6) has used the buffered reader. Its COBS decoder decided
  whether `peek(min)` is the right primitive (open question 1), and whether
  flush must enter `el_writer_ops`.
- The libc option ran on Zephyr with `PICOLIBC_IO_FLOAT` at least once,
  with its float output recorded as the platform's.
- io's promotion still waits for async discovery (M9). This adds buffer
  ownership to what M9 must look at, as module-plan's risk already says.

## Review: three seats

**Application author.**

| Friction found | Fixed by |
|---|---|
| Printing to a UART meant a stack buffer, a format call, and `el_write_all`, cut at the buffer's size | `el_fmt_printf_buffered` streams (Example 2) |
| A buffered stream that stalls mid-line leaves half a line | Reserve the line first (Example 2), or format a record into an `el_buf` then write it (Example 1). Documented in `fmt_printf.h`. |
| Without sticky truncation, a later call can follow a dropped number | Chain on the status (Example 1), or `el_buf_truncate` to a mark. The cost is stated in the ADR draft. |
| A growing buffer and a fixed one needed different code | One type. Only the init differs (Example 3). |
| Reading a stream to its end bounced through a chunk | `el_buf_reserve` / `el_buf_spare` / `el_buf_commit` |
| The trailing-newline trick held back two bytes | No terminator; `el_buf_truncate` overwrites the last byte |

**Backend author.**

| Friction found | Fixed by |
|---|---|
| A UART writer that would-blocks forced every caller to keep resume state | The buffered writer keeps it. The UART writer stays a plain single-attempt `el_writer`. |
| Does a backend now have to implement flush? | No: flush is not an operation (decision 5) |
| How does a buffered adapter run the contract suite? | Like any backend, over an inner stream that can stall; table in Verification |

**Maintainer.**

| Friction found | Fixed by |
|---|---|
| `fmt` gains a dependency | Accepted by the owner. `io` is below `fmt`, and nothing in `io` needs `fmt`, so there is no cycle. |
| Fixed-buffer programs paid for the allocator and the buffered writer (+592 bytes, measured) | Growth and the printf target's reserve reached through pointers (+200, measured; decision 12) |
| A private choice now changes what printf accepts | An ADR to be written, with the five rules above, and tests per configuration |
| `memmove` is a new runtime symbol | GCC already requires it of freestanding environments (source fact); decision 11 |
| Hard to change later | `el_buf`'s layout (6 words); printf's two targets; the item rule; flush outside the operation table. The first two are the decisions below. |

Names: `el_buf_*`, `el_buffered_writer_*`, `el_buffered_reader_*`, and
`el_fmt_*` follow `el_<module-ish noun>_<verb>`, as `el_mem_reader` does.
None equals a name in libedit's `histedit.h`: libedit has `el_init`,
`el_get`, `el_set`, `el_push`, and others, but no `el_buf*` and no
`el_fmt*`.

## Decisions for the owner

Each has a recommendation. The ones that reverse an accepted decision are
marked.

1. **`el_buf` replaces `el_mem_writer` and `el_grow_writer`; `el_mem_reader`
   stays** as a reader over `const` bytes. *Recommended.* Folding the reader
   in would cast away `const`.
2. **`el_buf` is an append buffer, with no read offset.** The adapters keep
   their own offset or compact privately. *Recommended* over a Go-style
   queue.
3. **A full memory buffer reports `EL_ERR_NO_SPACE`, not
   `EL_ERR_END_OF_STREAM`**, through its writer handle, in both storages.
   *Recommended.* ADR 0009 defines `NO_SPACE` as "a caller buffer that is
   too small". `END_OF_STREAM` predates it, and it is false once a buffer
   can be truncated and reused. This changes what `el_write_all` returns to
   today's `el_mem_writer` users.
4. **Adapters borrow an `el_buf`** (one init each) rather than embedding
   storage with an init per storage kind. *Recommended.*
5. **Flush stays a function of `el_buffered_writer`, not an operation in
   `el_writer_ops`**, decided by M4's planned writer sink, which never holds
   a buffered writer it did not create. Revisit with `frame`'s encoder
   writer. An ADR is to be written. *Recommended.*
6. **printf into a buffered writer streams.** On a stall or failure it stops
   at an item, keeps what it accepted buffered, and returns the inner
   status. Line atomicity is the caller's, by reserve-first.
   *Recommended* over "no streaming" and over a resumable printf.
7. **Truncation is per call, not sticky across calls.** *Recommended.*
   *Reverses part of ADR 0011.* The alternative is a sticky flag in
   `el_buf`, cleared by `el_buf_truncate`. It is cheap for plain buffers
   and incoherent behind a buffered writer.
8. **The `fmt` core is three pure conversions with `cap` asserted ≥
   `EL_FMT_INT_MAX`.** It drops width and zero-pad on typed calls,
   `el_fmt_str`/`cstr`, and the hex dump. *Recommended.* The hex dump can
   return with a consumer.
9. **No NUL termination anywhere.** *Recommended.* *Reverses fmt
   decision 1's termination rule* (ADR 0011).
10. **The platform option moves to printf**: `FMT_PRINTF_BUILTIN` (default)
    / `FMT_PRINTF_LIBC`, replacing `FMT_DIGITS`. Libc delegates the
    conversions outside the portable subset one at a time, with the
    one-byte edge for those of 32 bytes or more. The portable subset and
    text stay Elemental's code in both options. An ADR on private choices that
    widen a contract is to be written. *Recommended.* *Replaces the
    2026-11-12 fmt amendment.*
11. **`memmove` joins `memcpy` on the Zephyr allowlist.** *Recommended*, on
    GCC's own freestanding requirement. The alternative is a forward copy
    loop, which GCC may turn back into `memmove` anyway.
12. **Growth and printf's target are reached through pointers**, so that a
    fixed-buffer program links neither the allocator path nor the buffered
    writer. It costs one word in `el_buf`. *Recommended* (+200 instead of
    +592 bytes, measured).
13. **Names:**
    - `el_buf`, `el_buf_append` (what fits), `el_buf_append_whole`;
    - `el_buffered_writer`, `el_buffered_reader`;
    - `el_fmt_printf` / `el_fmt_printf_buffered`;
    - headers `io_buf.h`, `io_buffered.h`, with `io_alloc.h` kept for
      `el_buf_init_alloc`.

    *Recommended.* The owner may prefer a shorter `el_bufw`/`el_bufr` or
    `_to` suffixes: the names are cheap to change now and expensive later.
14. **Build the buffered reader with `frame` (M6), not in this milestone.**
    Its design is fixed here. *Recommended.* Its consumer is `frame`, and
    COBS may want a different primitive than `peek(min)` (open question 1).
    *Challenges the owner's direction*, which asked for both adapters now.
    If both are wanted now, step 5 joins M3a at about 336 bytes and one test
    file.
15. **`fmt` depends on `io`.** *Recommended* (the owner already accepted
    it). *Reverses fmt decision 3 and module-plan's "Where stream glue
    lives"*, with the text above.
16. **Roadmap: an M3a before M4** containing steps 1 to 4. M7 shrinks to the
    reader (with `frame`) and the flush re-check. *Recommended*; the owner
    applies it.

## Open questions

1. **COBS and `peek(min)`.** A delimiter scan does not know its `min`.
   With `peek(have + 1)` it rescans and reads one byte per call at worst.
   A `fill` primitive ("read once more into spare room, return all
   available") may be what `frame` wants instead of, or beside, `peek`.
   Settled by M6's COBS example, which is why decision 14 defers the
   reader.
2. **A `%s` longer than the buffer, through a buffered writer,** is copied
   through the buffer in capacity-sized pieces. Writing it straight through
   when the buffer is empty would save the copy, as the writer handle does.
   That is an implementation detail with no contract effect, left to step 3.
3. **The libc option's float output on picolibc** at `PICOLIBC_IO_LONG_LONG`
   (no float support) has not been looked at. The contract says "whatever
   the C library writes". Whether Elemental should refuse `%f` when the
   probe says floats are absent is a question for step 4.
4. **The libc option's code size** is unmeasured. The fmt amendment
   measured +1 268 bytes for picolibc's integer printf in an image with
   nothing else formatting. Floats will be more.

## Sources

Pinned, read on 2026-11-13. The source facts above cite these.

- Rust 1.90.0, `github.com/rust-lang/rust` tag `1.90.0`:
  - `library/std/src/io/buffered/bufwriter.rs`:
    - 73–76 (`panicked`);
    - 145–150 (`into_inner`);
    - 365–376 (`write_cold`: flush first, then write-through at capacity);
    - 526–538 (`write`);
    - 651–653 (`flush`);
    - 681–687 (drop flushes, "we ignore a failed flush").
  - `library/std/src/io/mod.rs`:
    - 615–652 (`default_write_fmt`'s `Adapter` keeps the `io::Error`);
    - 1677, 1801 (`Write`; `flush` required);
    - 2308, 2344, 2361 (`BufRead`; `fill_buf`, `consume` required).
  - `library/core/src/fmt/mod.rs`:
    - 70–85 (`Error` carries no information);
    - 107 (`pub struct Error;`);
    - 151 (`write_str`).
- Go go1.23.0, `github.com/golang/go` tag `go1.23.0`:
  - `src/bufio/bufio.go`:
    - 570–572 (errors are sticky);
    - 632 (`Flush` on `*Writer`);
    - 132–135 (`Peek`, `ErrBufferFull`).
  - `src/io/io.go`, 99–101 (`io.Writer` has only `Write`).
  - `src/bytes/buffer.go`:
    - 126–155 (`grow`, slides data down);
    - 229–247 (`growSlice`, at least doubles);
    - 44, 174 (`ErrTooLarge`).
- Pigweed, `github.com/google/pigweed` commit
  `1b610f4d8c1ba74808a552844474f3f620913a3f`:
  - `pw_stream/public/pw_stream/stream.h`:
    - 147–151, 172–177 (`Write` all or nothing; no `Flush` in the file);
    - the directory has no buffered stream.
  - `pw_string/public/pw_string/string_builder.h`:
    - 39–41 (always NUL-terminated);
    - 130–133 (sticky status until `clear_status()`);
    - 186–187 (strings cut).
  - `pw_string/public/pw_string/type_to_string.h`, 46–47 (numbers never
    truncated).
- Zephyr v4.5.0 (`41d10b59730`, local workspace):
  - `include/zephyr/sys/cbprintf.h`, 262–278 (`cbprintf_cb`, a negative
    return aborts);
  - `lib/os/cbprintf_complete.c`, 1387–1395 (`OUTC` returns the callback's
    error);
  - `lib/libc/picolibc/Kconfig`, 40–63 (the picolibc printf levels).
- cppreference, `std::format_to_n`, oldid 189663 (returns the untruncated
  size).
- GCC 16.2.1 manual (`gcc.info`), "Language Standards Supported by GCC":
  the freestanding environment must provide `memcpy`, `memmove`, `memset`
  and `memcmp`.
- C11 (N1570) 7.16.1.3: each `va_start` and `va_copy` is matched by a
  `va_end` in the same function invocation, before it returns.
