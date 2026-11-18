# Proposal: the seams between modules

Status: Draft, not reviewed.
Author, date: from the API review of 2026-11-18 (main at 7768415)

This changes four existing modules and adds one backend. It adds no module.

- **`io`**: the contract suite takes a reader and a writer as two halves,
  either of which may be absent, and accepts a reader that stalls after its
  content instead of ending. A new backend, **`io_zephyr`**, puts a Zephyr
  UART behind `el_writer` (sending by polling) and `el_reader` (receiving
  by interrupt, into a ring).
- **`log`**: an application composes a message itself, in the buffer the
  sink lends (`el_log_begin`, `el_log_end`); kept records are sized at
  compile time (`EL_LOG_RECORD_SIZE`); the deferred sink owns its queue;
  one logger makes no records at all (`el_logger_off`).
- **`alloc`**: an optional operation changes an allocation's size where it
  is (`resize`, `el_alloc_resize`), and `el_grow_buf` uses it.
- **The `alloc`, `time` and `log` contract suites**: `reset` becomes
  optional, as it does in io's.
- **[conventions.md](../conventions.md)**: one rule for how an object takes
  its storage.

Two other items from the same review change no interface and are being
done on their own branches: a build-time check of printf formats for
consumers, and documentation brought in line with the code (the naming
rule, the dependency graph, the growth hook of `el_buf` stated as private).

Labels:

- **Measured**: produced for this proposal; the method is stated with it.
- **Compile check**: the example was compiled, not run
  ([Verification](#verification)).
- **Computed**: arithmetic from sizes, not measured.
- Everything else is a recommendation.

What this changes that was decided before:

- The [alloc proposal](alloc.md) deferred resizing in place until a
  consumer needed it. `el_grow_buf` needs it (Example 3).
- The [ring proposal](ring.md) had the deferred sink borrow a ring its
  caller made. The sink owns it here.
- [ADR 0022](../adr/0022-log-sink-lends-the-buffer.md) is unchanged. An
  application is handed what a sink lends, under the rules a caller already
  follows.

## Problem

The modules are consistent inside and awkward where they meet. Each item
below is something an application or backend author meets in today's code.

1. **A write-only stream cannot run the io contract suite alone.** The
   fixture requires a reader and a writer. The three buffered-writer
   fixtures fill the reader slot with an `el_mem_reader`, and so test it
   three more times. **A reader without an end cannot pass at all**: four
   reader tests expect `EL_ERR_END_OF_STREAM` after the content, and a
   UART's reader, while it runs, has none.
2. **Zephyr has a clock backend and no io backend.** An application that
   logs to its console writes the console writer. One that reads a UART
   writes the receive side: [examples/uart_ring](../../examples/uart_ring)
   is 174 lines of it.
3. **A component cannot put its own formatter's output in a log message.**
   It formats into a scratch buffer of guessed size, even when the logger
   would drop the record, then copies it in with `%.*s` and two casts
   (Example 2).
4. **Kept records are sized by functions,** so a static array is sized
   from a number copied out of the README: "24 bytes and the message, on a
   32-bit target". The deferred sink makes its caller build an `el_ring`
   that nothing else may use.
5. **A logger that logs nothing still needs a sink and a clock,** never
   used, for a component that must be handed one.
6. **A grown buffer over an arena wastes half of it.** `el_grow_buf`
   allocates new storage and frees the old, which an arena does not
   reclaim. *Measured* in Example 3: a 60-byte message needs a 112-byte
   arena.
7. **Objects take storage three ways** (`(ptr, cap)`, a borrowed `el_buf *`,
   a borrowed `el_ring *`) with no stated rule, so each module chooses
   again.

## What changes

| Where | Change | Existing code |
|---|---|---|
| io contract | Reader and writer halves; `reader_then`; `reset` optional | Fixtures compile and pass unchanged |
| `io_zephyr` (new) | `el_zephyr_uart_tx`, `el_zephyr_uart_rx` | New |
| log | `el_log_begin`, `el_log_end`, `el_log_entry`; `EL_LOG_ENABLED` | Additions |
| log | `EL_LOG_RECORD_SIZE`, `el_log_record_header` | `el_log_mem_sink_record_size` and `el_log_deferred_sink_record_size` are removed |
| log | The deferred sink owns its queue | `el_log_deferred_sink_init` takes storage instead of an `el_ring *` |
| log | `el_logger_off` | Addition |
| alloc | `resize` operation; `el_alloc_resize` | Added at the end of the table: designated initializers leave it null |
| alloc, time, log contracts | `reset` optional | Fixtures compile and pass unchanged |
| conventions.md | How an object takes storage | The deferred sink was the one object outside the rule |

Every module touched is experimental, so the two breaking changes in log
need no deprecation ([api-stability.md](../process/api-stability.md)).

## The user experience, before and after

Examples 1, 3 and 4 are complete files; 2 and 6 are excerpts of complete
files; 5 sets fixture declarations side by side. Each "before" compiles
against today's headers, each "after" against the declarations in
[The changes](#the-changes); Zephyr names come from small stand-in headers
([Verification](#verification)).

### Example 1: a Zephyr application that logs from handlers and reads a UART

Before. The application writes the console writer and, in
`uart_reader.c`, the whole receive side.

```c
/*
 * Before: a Zephyr application logs to the console from a thread and from
 * its interrupt handlers, and reads a sensor on a second UART. It writes
 * the console writer below, and the whole receive side itself
 * (examples/uart_ring/uart_reader.c and .h, 174 lines), and sizes the
 * deferred sink's storage from a number in the documentation.
 */
#include <stddef.h>
#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>

#include <elemental/log_deferred.h>
#include <elemental/log_printf.h>
#include <elemental/log_writer.h>
#include <elemental/ring.h>
#include <elemental/time_zephyr.h>

#include "uart_reader.h" /* the application's: handler, ring, semaphore, counts */

/* The console, polled out a byte at a time. */
typedef struct console_writer {
    const struct device *dev;
} console_writer;

static el_status console_write(void *ctx, const void *data, size_t len, size_t *n)
{
    const console_writer *const console = ctx;
    const unsigned char *const bytes = data;
    for (size_t i = 0; i < len; i++) {
        uart_poll_out(console->dev, bytes[i]);
    }
    *n = len;
    return EL_OK;
}

static const el_writer_ops console_ops = {.write = console_write};

/* "24 bytes and the message, on a 32-bit target" (modules/log/README.md). */
static uint8_t queue_storage[8 * (24 + 64)];
static uint8_t record_storage[24 + 64];
static el_ring queue;
static el_log_deferred_sink deferred;
static el_logger isr_log; /* given to the application's interrupt handlers */

static console_writer console_out;
static uint8_t line_storage[96];
static el_buf line;
static el_log_writer_sink console;
static el_logger app_log;

static uint8_t rx_storage[256];
static uart_reader sensor;

void handle(const uint8_t *bytes, size_t len);
void app_main(void);

void app_main(void)
{
    const struct device *const sensor_uart = DEVICE_DT_GET(DT_NODELABEL(uart2));
    console_out.dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));
    if (!device_is_ready(console_out.dev) || !device_is_ready(sensor_uart)) {
        return; /* no console to log to, or no sensor to read */
    }
    el_buf_init(&line, line_storage, sizeof(line_storage));
    el_log_writer_sink_init(&console, (el_writer){.ctx = &console_out, .ops = &console_ops},
                            &line);
    el_logger_init(&app_log, el_log_writer_sink_as_sink(&console), el_zephyr_clock(), "app",
                   EL_LOG_LEVEL_INFO);

    el_ring_init(&queue, queue_storage, sizeof(queue_storage));
    el_log_deferred_sink_init(&deferred, &queue, record_storage, sizeof(record_storage));
    el_logger_init(&isr_log, el_log_deferred_sink_as_sink(&deferred), el_zephyr_clock(), "isr",
                   EL_LOG_LEVEL_INFO);

    uart_reader_start(&sensor, sensor_uart, rx_storage, sizeof(rx_storage), &isr_log);
    const el_reader in = uart_reader_as_reader(&sensor);

    for (;;) {
        if (uart_reader_wait(&sensor, K_MSEC(100))) {
            uint8_t chunk[32];
            size_t got;
            while (el_read(in, chunk, sizeof(chunk), &got) == EL_OK) {
                handle(chunk, got);
            }
        }
        el_log_deferred_sink_drain(&deferred, el_log_writer_sink_as_sink(&console));
    }
}
```

After. Both directions of a UART come from `elemental::io_zephyr`, and the
deferred sink owns its queue, sized at compile time.

```c
/*
 * After: the same application. Both directions of a UART come from
 * elemental::io_zephyr, and the deferred sink owns its queue, sized at
 * compile time.
 */
#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/kernel.h>

#include <elemental/io_zephyr.h>
#include <elemental/log_deferred.h>
#include <elemental/log_printf.h>
#include <elemental/log_writer.h>
#include <elemental/time_zephyr.h>

#define MESSAGE_MAX 64 /* the longest message an interrupt handler logs */
#define WAITING 8      /* records that may wait for this thread */

static uint8_t queue[WAITING * EL_LOG_RECORD_SIZE(MESSAGE_MAX)];
static uint8_t record[EL_LOG_RECORD_SIZE(MESSAGE_MAX)];
static el_log_deferred_sink deferred;
static el_logger isr_log; /* given to the application's interrupt handlers */

static el_zephyr_uart_tx console_out;
static uint8_t line_storage[32 + MESSAGE_MAX]; /* the line's start, then the message */
static el_buf line;
static el_log_writer_sink console;
static el_logger app_log;

static uint8_t rx_storage[256];
static el_zephyr_uart_rx sensor;

void handle(const uint8_t *bytes, size_t len);
void app_main(void);

void app_main(void)
{
    if (el_zephyr_uart_tx_init(&console_out, DEVICE_DT_GET(DT_CHOSEN(zephyr_console))) != EL_OK) {
        return; /* no console to log to */
    }
    el_buf_init(&line, line_storage, sizeof(line_storage));
    el_log_writer_sink_init(&console, el_zephyr_uart_tx_as_writer(&console_out), &line);
    el_logger_init(&app_log, el_log_writer_sink_as_sink(&console), el_zephyr_clock(), "app",
                   EL_LOG_LEVEL_INFO);

    el_log_deferred_sink_init(&deferred, queue, sizeof(queue), record, sizeof(record));
    el_logger_init(&isr_log, el_log_deferred_sink_as_sink(&deferred), el_zephyr_clock(), "isr",
                   EL_LOG_LEVEL_INFO);

    if (el_zephyr_uart_rx_init(&sensor, DEVICE_DT_GET(DT_NODELABEL(uart2)), rx_storage,
                               sizeof(rx_storage)) != EL_OK) {
        EL_LOG_ERROR(&app_log, "the sensor's UART is not ready, or cannot receive by interrupt");
        return;
    }
    const el_reader in = el_zephyr_uart_rx_as_reader(&sensor);

    uint32_t dropped = 0;
    for (;;) {
        if (el_zephyr_uart_rx_wait(&sensor, K_MSEC(100))) {
            uint8_t chunk[32];
            size_t got;
            while (el_read(in, chunk, sizeof(chunk), &got) == EL_OK) {
                handle(chunk, got);
            }
        }
        /* The receive handler counts what did not fit; this thread reports it. */
        const uint32_t now = el_zephyr_uart_rx_dropped(&sensor);
        if (now != dropped) {
            EL_LOG_WARN(&app_log, "%" PRIu32 " bytes dropped, the receive ring was full",
                        now - dropped);
            dropped = now;
        }
        el_log_deferred_sink_drain(&deferred, el_log_writer_sink_as_sink(&console));
    }
}
```

| | Before | After |
|---|---|---|
| Code the application writes for its UARTs | A console writer (17 lines) and the receive side (174 lines) | None |
| What it declares for the deferred sink | Queue storage, an `el_ring`, record storage, the sink | Queue storage, record storage, the sink |
| How it sizes them | A number from the README | `EL_LOG_RECORD_SIZE(MESSAGE_MAX)`, a constant |
| Bytes the receive ring had no room for | Counted and logged by the application's handler | Counted by the backend, read and logged by the thread |

### Example 2: a message composed with the application's formatter, and a quiet component

Before.

```c
typedef struct point {
    int32_t x;
    int32_t y;
} point;

/* The application's formatter, used for reports and for logs alike. */
EL_NODISCARD el_status point_print(el_buf *out, const point *p);

el_status point_print(el_buf *out, const point *p)
{
    return el_fmt_printf(out, "(%" PRId32 ",%" PRId32 ")", p->x, p->y);
}

/*
 * The point goes through a scratch buffer whose size is a guess. It is
 * formatted even when the logger lets INFO through to nothing, because
 * nothing can ask. Then it is copied into the message, with two casts.
 */
void report(const el_logger *log, const point *at, uint32_t speed)
{
    uint8_t scratch[32];
    el_buf text;
    el_buf_init(&text, scratch, sizeof(scratch));
    el_status_ignore(point_print(&text, at)); /* logged cut, if it did not fit */
    EL_LOG_INFO(log, "at %.*s speed %" PRIu32, (int)el_buf_len(&text),
                (const char *)el_buf_data(&text), speed);
}

/* A component that must be handed a logger. */
typedef struct radio {
    const el_logger *log;
} radio;

void radio_init(radio *r, const el_logger *log);

/* A logger that logs nothing still needs a sink and a clock, never used. */
void radio_quiet(radio *r)
{
    static el_manual_clock never_read;
    static el_log_mem_sink never_given;
    static el_logger quiet;
    el_manual_clock_init(&never_read, 1);
    el_log_mem_sink_init(&never_given, NULL, 0);
    el_logger_init(&quiet, el_log_mem_sink_as_sink(&never_given),
                   el_manual_clock_as_clock(&never_read), "", EL_LOG_LEVEL_OFF);
    radio_init(r, &quiet);
}
```

After. `point` and `point_print` are as before.

```c
void report(const el_logger *log, const point *at, uint32_t speed)
{
    el_log_entry entry;
    el_buf *const msg = el_log_begin(log, EL_LOG_LEVEL_INFO, &entry);
    if (msg == NULL) {
        return; /* below the logger's level, or its sink refused: nothing formatted */
    }
    el_status fit = el_fmt_printf(msg, "at ");
    if (fit == EL_OK) {
        fit = point_print(msg, at);
    }
    if (fit == EL_OK) {
        fit = el_fmt_printf(msg, " speed %" PRIu32, speed);
    }
    el_log_end(&entry, fit);
}

/* Detail at DEBUG, removed with the EL_LOG_* calls by EL_LOG_CUTOFF. */
void trace(const el_logger *log, const point *path, size_t count)
{
    if (EL_LOG_ENABLED(EL_LOG_LEVEL_DEBUG)) {
        el_log_entry entry;
        el_buf *const msg = el_log_begin(log, EL_LOG_LEVEL_DEBUG, &entry);
        if (msg != NULL) {
            el_status fit = el_fmt_printf(msg, "path");
            for (size_t i = 0; i < count && fit == EL_OK; i++) {
                fit = el_buf_append(msg, " ", 1);
                if (fit == EL_OK) {
                    fit = point_print(msg, &path[i]);
                }
            }
            el_log_end(&entry, fit);
        }
    }
}

void radio_quiet(radio *r)
{
    radio_init(r, el_logger_off());
}
```

| | Before | After |
|---|---|---|
| Scratch buffer for the formatter's output | Sized by guess | None: the message is formatted where the sink keeps or sends it |
| Work done when the level is filtered | The point is formatted, then dropped | None: `el_log_begin` returns null first |
| Casts at the call site | Two | None |
| A message as long as a loop | Not possible without a scratch buffer as long | Appended item by item; a cut message is reported to the sink as cut |
| A logger that logs nothing | A clock, a sink, a logger, three calls | `el_logger_off()` |

### Example 3: a grown buffer over a small arena

The code is the same before and after; only what the arena can hold
changes.

```c
/*
 * A message of six ten-byte fields, built in a grown buffer over a 100-byte
 * arena. The same code before and after: only what the arena can hold
 * changes.
 */
#include <stdint.h>
#include <stdio.h>

#include <elemental/alloc_arena.h>
#include <elemental/io_alloc.h>

int main(void)
{
    static uint8_t storage[100];
    el_bump_arena arena;
    el_bump_arena_init(&arena, storage, sizeof(storage));

    el_grow_buf body;
    el_buf *const msg = el_grow_buf_init(&body, el_bump_arena_as_allocator(&arena));
    el_status status = EL_OK;
    for (int field = 0; field < 6 && status == EL_OK; field++) {
        status = el_buf_append(msg, "0123456789", 10);
    }
    printf("%s, %zu of 60 bytes\n", el_status_name(status), el_buf_len(msg));
    el_grow_buf_deinit(&body);
    return 0;
}
```

*Measured* today: built against the gcc preset's libraries at 7768415 and
run on the host (GCC 16.2.1), it prints `no_space, 40 of 60 bytes`. With
the arena's size varied, 112 bytes is the least that holds the message:
the buffer grows to 16, 32 and 64 bytes, each a new allocation, and a bump
arena reclaims none of the old ones (16 + 32 + 64). *Computed* after: the
buffer is the arena's latest allocation, so each growth extends it in
place, and 64 bytes are enough.

### Example 4: a backend author's fixture for a UART

Before: not possible without faking an end, which
[porting.md](../process/porting.md) forbids. After: the reader half
declares that its reader stalls after its content, and the suite checks
that instead of an end.

```c
/*
 * Contract fixture for elemental::io_zephyr on mps2/an385 under QEMU, where
 * uart1's output is uart2's input (tests/zephyr/CMakeLists.txt). Both
 * halves: the reader is uart2's receiver, which has no end while it runs;
 * the writer is uart1's sender, and what it sent is taken back from uart2.
 */
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include <zephyr/device.h>
#include <zephyr/kernel.h>

#include <elemental/io_contract.h>
#include <elemental/io_zephyr.h>

static el_zephyr_uart_tx sender;
static el_zephyr_uart_rx receiver;
static uint8_t received[EL_IO_CONTRACT_SIZE_MAX];
static int receiving;
static size_t sent;  /* accepted by the sender since the stream was made */
static size_t taken; /* handed to the suite by writer_take */

static void reset(void *ctx)
{
    (void)ctx;
    if (receiving) {
        el_zephyr_uart_rx_deinit(&receiver);
        receiving = 0;
    }
}

static el_reader receive(void)
{
    if (el_zephyr_uart_rx_init(&receiver, DEVICE_DT_GET(DT_NODELABEL(uart2)), received,
                               sizeof(received)) != EL_OK ||
        el_zephyr_uart_tx_init(&sender, DEVICE_DT_GET(DT_NODELABEL(uart1))) != EL_OK) {
        abort(); /* the wire is not there: nothing could be verified */
    }
    receiving = 1;
    sent = 0;
    taken = 0;
    return el_zephyr_uart_rx_as_reader(&receiver);
}

/* Until `len` bytes wait in the receiver. The wire loses nothing. */
static void await(size_t len)
{
    while (el_zephyr_uart_rx_len(&receiver) < len) {
        (void)el_zephyr_uart_rx_wait(&receiver, K_MSEC(10));
    }
}

static el_reader reader_with_content(void *ctx, const uint8_t *content, size_t len)
{
    (void)ctx;
    const el_reader reader = receive();
    if (el_write_all(el_zephyr_uart_tx_as_writer(&sender), content, len, NULL) != EL_OK) {
        abort(); /* the wire is broken: nothing could be verified */
    }
    await(len);
    return reader;
}

static el_reader reader_stalled(void *ctx)
{
    (void)ctx;
    return receive(); /* nothing sent: nothing to give, and not ended */
}

/* The sender, counted, so that writer_take knows what to wait for. */
static el_status counted_write(void *ctx, const void *data, size_t len, size_t *n)
{
    (void)ctx;
    const el_status status = el_write(el_zephyr_uart_tx_as_writer(&sender), data, len, n);
    sent += *n;
    return status;
}

static const el_writer_ops counted_ops = {.write = counted_write};

static el_writer writer_with_room(void *ctx, size_t room)
{
    (void)ctx;
    (void)room; /* a polled sender takes every byte it is given */
    (void)receive();
    return (el_writer){.ctx = NULL, .ops = &counted_ops};
}

static size_t writer_take(void *ctx, uint8_t *out, size_t cap)
{
    (void)ctx;
    await(sent - taken);
    size_t n = 0;
    el_status_ignore(el_read(el_zephyr_uart_rx_as_reader(&receiver), out, cap, &n));
    taken += n;
    return n;
}

const el_io_contract_fixture io_zephyr_fixture = {
    .name = "io_zephyr, uart1 to uart2",
    .reset = reset,
    .reader_with_content = reader_with_content,
    .reader_then = EL_IO_CONTRACT_THEN_STALLS,
    .reader_stalled = reader_stalled,
    .writer_with_room = writer_with_room,
    .writer_take = writer_take,
};
```

The counting writer forwards each result unchanged, so what the suite
checks is the sender's behavior. It is the fixture observing the stream,
not a capability made up.

### Example 5: smaller fixtures

A write-only implementation leaves the reader half out instead of
borrowing a memory reader, and a fixture with nothing to release leaves out
`reset`:

```c
/* Before (modules/io/tests/fixture_buffered.c) */
const el_io_contract_fixture io_fixture_buffered_fixed = {
    .name = "el_buffered_writer (fixed, 16 bytes)",
    .ctx = NULL,
    .reset = reset,
    .reader_with_content = io_fixture_buffered_reader, /* an el_mem_reader, tested again */
    .writer_with_room = fixed_with_room,
    .writer_take = io_fixture_buffered_take,
    .writer_stalled = fixed_stalled,
};

/* After: the suite reports the reader tests as skipped, "no reader". */
const el_io_contract_fixture io_fixture_buffered_fixed = {
    .name = "el_buffered_writer (fixed, 16 bytes)",
    .reset = reset,
    .writer_with_room = fixed_with_room,
    .writer_take = io_fixture_buffered_take,
    .writer_stalled = fixed_stalled,
};

/* Before (examples/backend_step_clock/step_clock_fixture.c) */
static void reset(void *ctx)
{
    (void)ctx; /* The clock holds nothing to release. */
}

/* After: no reset function, and no .reset in the fixture. */
```

### Example 6: an allocator that can grow in place

The size classes example (examples/backend_size_classes) gains the
optional operation in seven lines. In place means in the same class,
because a block never moves between classes:

```c
static el_status size_classes_resize(void *ctx, void *ptr, el_layout layout, size_t new_size)
{
    (void)ctx;
    (void)ptr;
    const int now = class_of(layout);
    const int then = class_of(el_layout_make(new_size, layout.align));
    return now == then ? EL_OK : EL_ERR_NO_SPACE;
}

static const el_allocator_ops size_classes_ops = {
    .alloc = size_classes_alloc,
    .free = size_classes_free,
    .resize = size_classes_resize,
};
```

An allocator that cannot resize leaves the operation out and keeps working:
`el_alloc_resize` reports `EL_ERR_NO_SPACE` for it, and `el_grow_buf` then
does what it does today.

## The changes

### io: the contract suite in two halves

```c
/*
 * What a reader does once it has given its content: the fixture's
 * reader_then. 0 is EL_IO_CONTRACT_THEN_ENDS, so a fixture written before
 * the field existed keeps its meaning.
 */
enum {
    /* EL_ERR_END_OF_STREAM, for good: memory, a pipe whose write end is
     * closed, a closed ring. */
    EL_IO_CONTRACT_THEN_ENDS = 0,
    /* EL_ERR_WOULD_BLOCK: not ended, and more may come. A UART, an open
     * socket, a ring whose producer has not closed it. */
    EL_IO_CONTRACT_THEN_STALLS = 1
};

/*
 * At least one half. A half that is there has its required functions;
 * every test of an absent half is reported as skipped.
 */
typedef struct el_io_contract_fixture {
    const char *name;
    void *ctx;

    /* Optional. Release the stream the previous request made. */
    void (*reset)(void *ctx);

    /* The reader half. */
    el_reader (*reader_with_content)(void *ctx, const uint8_t *content, size_t len);
    uint32_t reader_then;                   /* EL_IO_CONTRACT_THEN_* */
    el_reader (*reader_stalled)(void *ctx); /* optional, as now */

    /* The writer half. writer_take is required with writer_with_room. */
    el_writer (*writer_with_room)(void *ctx, size_t room);
    size_t (*writer_take)(void *ctx, uint8_t *out, size_t cap);
    el_writer (*writer_stalled)(void *ctx);                    /* optional, as now */
    el_writer (*writer_ending_after)(void *ctx, size_t room); /* optional, as now */
    el_writer (*writer_full_after)(void *ctx, size_t room);   /* optional, as now */
} el_io_contract_fixture;
```

The suite:

- `ReaderDeliversItsContentInOrderThenEnds` becomes
  `ReaderDeliversItsContentInOrder`, and then expects `reader_then`'s
  status. `ReaderZeroCapacityReadIsANoOp` and
  `ReadExactFillsTheBufferOrReportsAShortStream` expect it too: a short
  stream that stalls reports `EL_ERR_WOULD_BLOCK` with every byte it had.
- `ReaderEndOfStreamIsPermanent` is skipped for a reader that stalls.
- New: `ReaderThatStallsAfterItsContentHasNotEnded`, for `THEN_STALLS`:
  after the content, reads return `EL_ERR_WOULD_BLOCK` with `*n == 0`,
  several times, never `EL_ERR_END_OF_STREAM`. A device reader that
  reports an end whenever its FIFO is momentarily empty is a classic bug;
  nothing checks for it today.
- `reset` is optional in `el_alloc_contract_fixture`,
  `el_time_contract_fixture` and `el_log_contract_fixture` too.

Twelve tests become thirteen. The skip counts in the module READMEs and
PORT.md files change with them; the buffered-writer fixtures stop testing
the memory reader.

### io_zephyr: a UART as a writer and a reader

A backend library, `elemental::io_zephyr`, header `<elemental/io_zephyr.h>`,
under `modules/io/backends/zephyr/`. It promotes the receive side of
`examples/uart_ring` and adds a sender.

```c
/*
 * Sending. One write passes every byte to uart_poll_out(): it returns once
 * the device has taken them all, waiting as the driver waits (on most, a
 * busy loop). Never EL_ERR_WOULD_BLOCK, EL_ERR_NO_SPACE, EL_ERR_IO or
 * EL_ERR_END_OF_STREAM. Not for interrupt handlers: it waits.
 */
typedef struct el_zephyr_uart_tx {
    const struct device *dev;
} el_zephyr_uart_tx;

/* EL_OK, or EL_ERR_IO: the device is not ready (device_is_ready()). */
EL_NODISCARD el_status el_zephyr_uart_tx_init(el_zephyr_uart_tx *tx, const struct device *dev);
el_writer el_zephyr_uart_tx_as_writer(el_zephyr_uart_tx *tx);

/*
 * Receiving. From init on, the device's receive interrupt copies what
 * arrives into a ring over the caller's storage; bytes the device held
 * before init are discarded. What does not fit is dropped and counted: the
 * handler cannot wait. The reader takes from the ring:
 *
 *   EL_OK                 1 <= *n <= cap bytes, oldest first.
 *   EL_ERR_WOULD_BLOCK    Nothing has arrived. Not an end: a running
 *                         receiver has none.
 *   EL_ERR_END_OF_STREAM  After deinit, once every byte received before it
 *                         has been read.
 */
typedef struct el_zephyr_uart_rx {
    const struct device *dev;
    el_ring ring;          /* the handler writes, the reading thread reads */
    struct k_sem arrived;  /* given by the handler when bytes arrive */
    el_ring_word dropped;  /* the handler's */
} el_zephyr_uart_rx;

/*
 * EL_OK: receiving. EL_ERR_IO: the device is not ready, or its driver has no
 * interrupt-driven receive (device_is_ready() tells the two apart); nothing
 * runs, and the reader reports EL_ERR_END_OF_STREAM.
 */
EL_NODISCARD el_status el_zephyr_uart_rx_init(el_zephyr_uart_rx *rx, const struct device *dev,
                                              void *storage, size_t cap);

/* Stop receiving, after an init that returned EL_OK: the interrupt is
 * disabled and the ring is closed. */
void el_zephyr_uart_rx_deinit(el_zephyr_uart_rx *rx);

el_reader el_zephyr_uart_rx_as_reader(el_zephyr_uart_rx *rx);

/* Bytes received and not yet read: at least this many can be read now. */
size_t el_zephyr_uart_rx_len(const el_zephyr_uart_rx *rx);

/* Wait until bytes arrive, or for `timeout`. False if the time ran out. */
bool el_zephyr_uart_rx_wait(el_zephyr_uart_rx *rx, k_timeout_t timeout);

/* Bytes dropped because the ring was full, since init. Any context. */
uint32_t el_zephyr_uart_rx_dropped(const el_zephyr_uart_rx *rx);
```

- **Waiting.** [ring.h](../../modules/ring/include/elemental/ring.h) says
  the ring wakes nobody, and that what a reader waits on is the platform's.
  Here the platform is Zephyr and the backend owns the handler, so the
  backend is the one that can give the semaphore. The ring still wakes
  nobody.
- **Contexts.** The receive handler is the ring's producer. One thread
  reads, waits, and calls init and deinit. The receiver must not move from
  init to deinit (the ring's rule, and the handler holds its address). A
  sender may be used from one thread at a time.
- **Configuration.** `IO_ZEPHYR`, default on, depends on `IO`, `RING` and a
  new probed fact, `PLATFORM_ZEPHYR_UART_IRQ`: a Zephyr build with
  `CONFIG_SERIAL` and `CONFIG_UART_INTERRUPT_DRIVEN`, read from Zephyr's
  configuration as `PLATFORM_ZEPHYR_CRC` is
  ([porting.md](../process/porting.md#making-a-backend-selectable-only-where-it-can-build)).
- **Errors.** Only setting up can fail: a device that is not ready, or a
  driver whose `uart_irq_callback_user_data_set` refuses (`-ENOTSUP`).
  Both are `EL_ERR_IO` from init, and `device_is_ready()` tells them apart,
  so there is no errno accessor. Once running, nothing fails:
  `uart_poll_out` returns nothing, and an empty FIFO ends the handler's
  loop. The one detail kept is the drop count.
- **What happens to examples/uart_ring.** Its receive side becomes this
  backend. The example keeps what it shows that the backend does not:
  logging from an interrupt handler through the deferred sink. Its handler
  becomes a `k_timer` expiry function, which Zephyr runs in interrupt
  context, since the UART's handler is now the backend's and Elemental's
  own code never logs.

A non-blocking, interrupt-driven sender is not part of this; see
[Open questions](#open-questions).

### log: composing a message

In `<elemental/log.h>`, so other languages have it too:

```c
/*
 * A record being composed: el_log_begin() starts it, el_log_end() ends it.
 * The caller provides it, usually on the stack. The fields are not API.
 */
typedef struct el_log_entry {
    el_log_sink sink;
    el_log_record record;
    el_buf *message;
    size_t start;
} el_log_entry;

/*
 * Start a record at `level` (below EL_LOG_LEVEL_OFF) and return the buffer
 * its message goes into: the one the logger's sink lends. Null when no
 * record is made: `level` is below the logger's minimum (the clock is not
 * read), or the sink refused to begin it (and counted it as lost).
 *
 * After a non-null return, write the message with el_buf functions only:
 * el_buf_append(), el_fmt_printf(), a formatter of your own,
 * el_buf_reserve() with el_buf_commit(), and el_buf_truncate() back to no
 * less than the length the buffer had when el_log_begin() returned. Then
 * call el_log_end() once, from the same context, before anything else is
 * given to the logger's sink.
 */
EL_NODISCARD el_buf *el_log_begin(const el_logger *logger, el_log_level level,
                                  el_log_entry *entry);

/*
 * End the record el_log_begin() started. `fit` is EL_OK if the message is
 * whole; any other status means it was cut, and the sink is told
 * EL_ERR_NO_SPACE. A record the sink does not take is lost, and the sink
 * counts it.
 */
void el_log_end(el_log_entry *entry, el_status fit);
```

In `<elemental/log_printf.h>`:

```c
/*
 * A constant: whether calls at `level` are kept under EL_LOG_CUTOFF.
 * Composed messages inside `if (EL_LOG_ENABLED(level)) { ... }` are cut
 * off with the EL_LOG_* calls.
 */
#define EL_LOG_ENABLED(level) ((level) >= EL_LOG_CUTOFF)
```

- These are the three steps `el_log_vprintf` already takes with private
  functions (`el_log_record_start_`, `el_log_sink_begin_`,
  `el_log_sink_end_`), made public. `el_log_vprintf` becomes
  `el_log_begin`, `el_fmt_vprintf`, `el_log_end`; the sink contract does
  not change.
- `fit` takes any status so that a formatter's own status can be passed
  as it is, including an application's negative one; only `EL_OK` means
  whole.
- A truncate below the message's start is a contract violation, caught at
  `el_log_end` (the check `el_log_sink_end_` already makes).
- *Computed*: `el_log_entry` is 48 bytes on a 32-bit target, on the stack
  of a composing call.

### log: sizing kept records at compile time

In `<elemental/log.h>`:

```c
/*
 * How the memory sink and the deferred sink keep a record: these fields,
 * then the message's bytes, packed, and copied in and out, so storage of
 * any alignment works. Public so that storage can be sized at compile time;
 * the fields are not API.
 */
typedef struct el_log_record_header {
    el_ticks timestamp;
    const char *tag;
    size_t len;
    uint32_t ticks_per_second;
    el_log_level level;
} el_log_record_header;

/* The bytes one kept record takes: its fields and a message of
 * `message_len` bytes. A constant expression when `message_len` is. */
#define EL_LOG_RECORD_SIZE(message_len) (sizeof(el_log_record_header) + (size_t)(message_len))
```

`el_log_mem_sink_record_size()` and `el_log_deferred_sink_record_size()`
are removed. The two sinks' private `mem_entry` and `deferred_entry`, which
are the same struct field for field, become this one, written and read by
one pair of private functions in `log_private.h`.

### log: the deferred sink owns its queue

```c
typedef struct el_log_deferred_sink {
    el_ring queue;   /* the sink's own: the logging side writes, the drain reads */
    uint8_t *record; /* the record begun is built here: its fields, then its message */
    size_t record_cap;
    el_buf message;
    el_ring_word lost;
} el_log_deferred_sink;

/*
 * Hand records on through a queue over the `queue_cap` bytes at `queue`,
 * building each in the `record_cap` bytes at `record`. Size both with
 * EL_LOG_RECORD_SIZE(): the record storage for the longest message, the
 * queue for as many such records as may wait. Either may be null with a
 * size of 0, which makes a sink that loses every record.
 *
 * Called before either side is used. The sink must not move afterwards.
 */
void el_log_deferred_sink_init(el_log_deferred_sink *sink, void *queue, size_t queue_cap,
                               void *record, size_t record_cap);
```

The rule "the ring is set up by the caller, before or after, and is empty
when the first record is logged" disappears with the ring. *Computed*: the
sink grows by the ring less a pointer, 16 bytes on a 32-bit target, and
the caller's `el_ring` goes away, so the total is unchanged.

### log: the logger that makes no records

```c
/*
 * The logger that makes no records, for a component that must be handed a
 * logger in a program that wants none of its records. Its minimum is
 * EL_LOG_LEVEL_OFF: every call returns at once, reads no clock and gives
 * nothing to a sink. Constant, valid for the whole program, and usable
 * from any context.
 */
const el_logger *el_logger_off(void);
```

A static constant inside `log.c`, with a sink and a clock that assert if
they are ever called. The conventions' rule that a null logger is a
contract violation stays.

### alloc: changing an allocation's size in place

```c
typedef struct el_allocator_ops {
    void *(*alloc)(void *ctx, el_layout layout);
    void (*free)(void *ctx, void *ptr, el_layout layout);

    /*
     * Optional: null if the allocator cannot change an allocation's size
     * where it is.
     *
     * Called with: ptr and layout exactly those of a live allocation from
     *              this ctx; new_size >= 1 and different from layout.size.
     * Must: return EL_OK, the allocation now new_size bytes at the same
     *       address and its first min(layout.size, new_size) bytes
     *       unchanged; or return EL_ERR_NO_SPACE and change nothing.
     */
    el_status (*resize)(void *ctx, void *ptr, el_layout layout, size_t new_size);
} el_allocator_ops;

/*
 * Make the allocation at `ptr`, made from `allocator` with `layout`,
 * `new_size` bytes without moving it.
 *
 *   EL_OK            It is new_size bytes now, at the same address. Free it
 *                    with el_layout_make(new_size, layout.align).
 *   EL_ERR_NO_SPACE  Not possible where it is, or the allocator cannot
 *                    resize at all. Nothing changed: allocate, copy and
 *                    free instead.
 *
 * A new size equal to layout.size is EL_OK without consulting the
 * allocator. A layout or a new size of 0 is EL_ERR_NO_SPACE: the zero-size
 * pointer never reaches an allocator.
 */
EL_NODISCARD el_status el_alloc_resize(el_allocator allocator, void *ptr, el_layout layout,
                                       size_t new_size);
```

- **The name.** libedit's `histedit.h` declares `el_resize`, which
  [ADR 0006](../adr/0006-c11-and-naming.md) rules out, hence
  `el_alloc_resize`.
- **The arenas.** Both resize their latest allocation: it grows while the
  storage has room, and shrinking it gives the tail back. Any other
  allocation shrinks in place, giving nothing back, and does not grow. The
  libc backend leaves the operation out: C has no in-place `realloc`.
- **The checking allocator** forwards it and, on `EL_OK`, records the new
  layout, so a free with the old size is caught.
- **The contract suite** tests it when the handle has the operation, so
  the fixture needs no new field: a resize that succeeds keeps the address
  and the bytes; one that fails changes nothing; a resized allocation is
  freed with its new size.
- **`el_grow_buf`** tries `el_alloc_resize` to the size it wants, then to
  exactly what is needed, and only then allocates, copies and frees, as
  today (Example 3).
- **The Rust sketch** (`tests/interop/rust_alloc_sketch.rs`) mirrors
  `el_allocator_ops` and gains the field.

### conventions.md: how an object takes storage

A new rule under "Storage, size, and alignment":

> **How an object takes storage depends on what it is for.**
>
> - Storage the object lays out itself (records, a ring's bytes, an
>   arena, a fixed buffer): `(void *storage, size_t cap)`, of any
>   alignment unless the header says otherwise. Where each element carries
>   overhead, a constant-expression macro sizes it (`EL_LOG_RECORD_SIZE`).
> - Storage whose growth the caller decides, fixed or grown: a borrowed
>   `el_buf *` (the buffered writer, the writer sink's line).
> - A container that nothing else uses: embedded in the object, and made
>   from `(storage, cap)` (the deferred sink's queue, the UART receiver's
>   ring).
> - Another object the caller also uses: a handle, by value (a writer, a
>   clock, a sink, an allocator).

Every object in the repository follows it once the deferred sink owns its
queue.

## What is not changed, and why

- **One formatting target for printf.** The review asked whether
  `el_fmt_printf` and `el_fmt_printf_buffered` could become one target. A
  prototype made a buffered writer lend an `el_buf` that passes bytes on
  when it needs room (branch `t3/unify-buffer-format-target`, kept outside
  main; built by a delegated agent, whose numbers these are). It worked:
  the gcc, clang and assert-off presets passed, and sanitize with leak
  detection off; its io and fmt code was 40 bytes larger on a Cortex-M0
  (Zephyr SDK GCC 14.3.0, `-Os`, object text). But it made every
  `el_buf *` possibly a stream: an append that failed on a stalled stream
  had already passed on earlier bytes (`abcdefgh` became `cdefgh`), offsets
  went stale, and log needed a run-time check to refuse such a buffer. That
  puts a hazard into the most used type to remove one duplicate function
  family. Recommended: record it as ADR 0024, "An `el_buf` keeps what it
  holds". Revisit when a second reusable formatter needs to reach both a
  buffer and a stream; then the target is a handle of its own (`ctx` and
  `ops`, made from an `el_buf` or a buffered writer), not a mode of `el_buf`.
- **Division-free arithmetic in `bytes`.** `alloc`, `time` and `fmt` each
  have their own multiply or divide without a compiler helper. Sharing them
  would add three dependency edges, and `status` is the only module every
  other may depend on. Three small, tested copies cost less.
- **`el_grow_buf_init` returning `el_buf *`.** It is the one `_init` that
  returns something, and an `el_grow_buf_as_buf()` would match the `_as_`
  rule. The call site is one line today and no consumer has needed the
  buffer again later; not worth the churn.
- **Unsupported printf formats in log calls.** They are contract
  violations, and trap by default. The build-time check closes most of the
  gap; whether a log call should degrade instead is a decision below.
- **The sink contract, flush, the time representation, and the status
  vocabulary.** The review found nothing to change in them.

## Dependencies and cost

| | |
|---|---|
| Elemental modules | One new edge: `io_zephyr` depends on `io` and `ring`. log and alloc gain no dependency |
| Runtime symbols (freestanding) | None new. `io_zephyr` uses Zephyr's UART and semaphore calls, like `time_zephyr` uses its kernel |
| Allocation | None new. `el_alloc_resize` is called only by code that already allocates |
| Per-object storage | `el_log_deferred_sink` grows by the ring less a pointer, and the caller's `el_ring` goes away; `el_allocator_ops` grows by a pointer, once per allocator type, in read-only data; `el_log_entry` (*computed*: 48 bytes on 32-bit) is on the stack only while a message is composed |
| New status codes | None |
| Kconfig | `IO_ZEPHYR` and the probed fact `PLATFORM_ZEPHYR_UART_IRQ` |
| Code size | Not measured. `el_log_begin` and `el_log_end` move code out of `el_log_vprintf` rather than add it; the resize operations and `io_zephyr` are new code. To be measured with `code_size.sh` |

## Contracts

The new and changed headers above state each one; in summary:

- **Ownership and lifetime.** `el_log_entry` and the UART objects are the
  caller's. The receiver stays in place from init to deinit. An entry lives
  from `el_log_begin` to `el_log_end`. `el_logger_off()` is valid for the
  whole program.
- **Errors.** `el_alloc_resize`: `EL_OK` or `EL_ERR_NO_SPACE`. The UART
  reader: `EL_OK`, `EL_ERR_WOULD_BLOCK`, or `EL_ERR_END_OF_STREAM` after
  deinit. The two UART inits: `EL_OK` or `EL_ERR_IO`. A write to the
  sender cannot fail. Log calls still cannot fail.
- **Concurrency and context.** The UART receiver is two contexts by
  design, like the ring: its handler and one reading thread. The sender and
  the composing calls follow the default, one context at a time.
  `el_log_begin` and `el_log_end` are as interrupt-safe as the logger's
  sink and clock, exactly like `el_log_printf`.
- **Contract violations.** A null entry or logger; `el_log_end` without a
  matching `el_log_begin`; a truncate below the message's start; a resize
  operation that returns anything but the two statuses; a receiver used
  after deinit except to read what remains.

## Verification

How the examples were checked (*compile check*): each "before" against the
headers of main at 7768415, each "after" against copies of those headers
with the declarations in [The changes](#the-changes) added, with GCC 16.2.1
and Clang 23.1.5, `-std=c11 -Wall -Wextra -Wpedantic -Wconversion
-Wsign-conversion -Wshadow -Werror`. Zephyr's `device.h`, `kernel.h` and
`drivers/uart.h` were stand-in headers declaring only the names the
examples use, so the Zephyr examples are compile checks of Elemental's side
only. Example 3 was also linked and run, as *measured* above. Nothing in
"after" has been run.

What the implementation must show:

- **io contract.** Every existing fixture passes as it is; the buffered
  writer fixtures pass without a reader; the new test is seen red against a
  reader that reports an end when it is momentarily empty.
- **io_zephyr.** The contract suite, both halves, on `mps2/an385` under
  QEMU over the joined UARTs: an emulator run. Its own tests: bytes held
  before init are discarded; a full ring drops and counts; deinit ends the
  reader after what was received; a wait times out. Built for the other
  boards, with the symbol check.
- **log.** `el_log_begin` reads no clock below the minimum and formats
  nothing when the sink refuses (counted, as `log.unit` counts readers
  today); a truncate below the start dies; the cut-off check
  (`log.cutoff_check.*`) shows a composed message under
  `EL_LOG_ENABLED` leaves no string; a storage of exactly n times
  `EL_LOG_RECORD_SIZE(m)` keeps n records and loses the next; the deferred
  sink's stress test runs unchanged in substance; `el_logger_off()` never
  reaches its sink or clock.
- **alloc.** The suite's resize tests against both arenas, the libc heap
  (skipped: no operation), the checker and the size classes example, and
  seen red against an arena whose resize moves the tail wrongly; Example 3
  as a test, holding 60 bytes in a 100-byte arena.
- Every preset, Twister, the symbol check, and the ledgers, as for any
  change ([preflight](../../.agents/skills/preflight/SKILL.md)).

## Order of work

Four parts on their own branches, merged in this order:

1. **The contract suites**: io's halves and `reader_then`; `reset`
   optional in all four. Nothing depends on anything else here.
2. **log**: composing, `EL_LOG_RECORD_SIZE` with the shared record
   functions, the deferred sink's own queue, `el_logger_off`. Independent
   of 1.
3. **alloc**: the operation, the arenas, the checker, the suite, then
   `el_grow_buf`. Independent of 1 and 2.
4. **io_zephyr**: needs 1 for its fixture. Then `examples/uart_ring` is
   rewritten on it.

Then the records: the storage rule in conventions.md, ADR 0024, the README
module table, build.md's options, CHANGELOG, and a roadmap entry. Parts 1
to 3 can be built in parallel.

## Decisions for the owner

1. **An unsupported printf format in a log call: trap, or degrade?**
   Today a contract violation, so a trap in the default configuration.
   Recommended: keep the trap. The build-time check catches literal formats
   in every target that uses it, and degrading would make a bug in a format
   string a silent oddity in a log.
2. **io_zephyr's first scope.** Recommended: a polled sender and an
   interrupt-driven receiver, as above. A non-blocking sender waits for a
   consumer.
3. **Hand the sink's loan to applications** (`el_log_begin`,
   `el_log_end`). Recommended: yes. It is what `el_log_printf` already does,
   made public under the same rules.
4. **Resize in place now**, although the alloc proposal deferred it.
   Recommended: yes; `el_grow_buf` is the consumer.
5. **Record ADR 0024, "An `el_buf` keeps what it holds."** Recommended:
   yes, with the prototype's evidence, so the question is not reopened
   without new evidence.
6. **Where it goes in the roadmap.** Recommended: a milestone M5a, before
   M6. `frame`'s glue is to run over the UART example, which becomes
   `io_zephyr`, and its tests will use the new fixture halves.

## Open questions

- **Waking the thread that drains a deferred sink.** Today it polls, or the
  application wraps the deferred sink in a sink that forwards both calls
  and then gives a semaphore, which `begin`/`end` makes zero-copy. A
  callback in the deferred sink would save that wrapper. Wait for a second
  application that writes it.
- **A non-blocking sender.** One that fills the FIFO from the transmit
  interrupt out of a ring, and stalls with `EL_ERR_WOULD_BLOCK` when the
  ring is full: the inner writer `io_buffered.h` describes. Wait for a
  console that must not block.
- **io_zephyr beyond QEMU.** Whether Renode can join two UARTs of the
  Cortex-M0+ board (open since the ring proposal), and whether Zephyr's UART
  emulator would let `native_sim` run the suite.
