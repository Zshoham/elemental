# Proposal: the seams between modules

Status: Accepted 2026-11-23 and implemented as experimental the same day
([roadmap M5a](../roadmap.md#m5a--the-seams-between-modules)). Decisions
3, 4, 7, 8 and 9 were settled in review on 2026-11-22; 1, 2, 5 and 6 were
accepted as recommended when implementation was asked for
([Decisions](#decisions-for-the-owner)). Where the implementation differs
from the text below: `el_realloc` of a size-0 allocation to size 0 returns
the zero-size pointer; a null device is `EL_ERR_IO` from
`el_zephyr_uart_irq_init`, not a contract violation; and
`el_zephyr_uart_irq_len` is the reading thread's (or `on_arrival`'s), not
any context's. Corrected after review: `reader_then` is the io fixture's
last member, after the members it had before, not between
`reader_with_content` and `reader_stalled`, so that a fixture initialized
in the old order keeps its meaning; after `el_realloc` succeeds only the
returned pointer is used, even if it is the old address, as after C's
`realloc()`; `on_arrival` may also run synchronously inside
`el_zephyr_uart_irq_init`, on the initializing thread (Zephyr's CMSDK
driver calls its handler from `uart_irq_rx_enable()` when a byte is
waiting), so it and its `ctx` must be ready before init, and a call means
bytes arrived, queued or counted as dropped, not that the ring holds any;
the receiver and its storage stay alive and in place while the reader is
used, after deinit too; and `CONFIG_UART_INTERRUPT_DRIVEN` is a build-wide
option, so the polled sender works on any ready device in such a build,
including one without interrupt-driven receive. Corrected after the second
review of `io_zephyr`: the polled writer promises that each byte reaches
`uart_poll_out()`, not that the device kept it (lossless only where the
driver's `poll_out` waits for the device; Zephyr's RTT UART skips bytes);
`el_zephyr_uart_irq_init` also fails with `EL_ERR_IO` when an
asynchronous receive is active, and its `EL_ERR_IO` covers only what the
driver reports, so the caller selects an instance with interrupt-driven
receive; bytes held before init are discarded only where the driver has
`poll_in`; and the receiver is supported only on drivers that call the
callback from the device's interrupt, not from a work queue.
Split after acceptance (2026-11-23): the backend below called `io_zephyr`
is two libraries. The receive side is `ring_zephyr_uart`
(`<elemental/ring_zephyr_uart.h>`, `RING_ZEPHYR_UART`, probe
`PLATFORM_ZEPHYR_UART_IRQ`): `el_zephyr_uart_irq` fills a ring the caller
sets up and passes to `el_zephyr_uart_irq_init(uart, dev, ring,
on_arrival, ctx)`, and the reader is `el_ring_as_reader(ring)`;
`el_zephyr_uart_irq_as_reader`, `el_zephyr_uart_irq_len` and the `storage,
cap` parameters are gone, a failed init leaves the ring open and
untouched, and deinit closes it. The writer is `io_zephyr_uart`
(`<elemental/io_zephyr_uart.h>`, `IO_ZEPHYR_UART`, probe
`PLATFORM_ZEPHYR_UART`, `CONFIG_SERIAL` only), unchanged. Why: the reading
was the ring's, and the UART code was only a producer filling it; device
plumbing does not belong in io's backends, and the polled writer is a
genuine `el_writer` that needs neither interrupts nor `ring`. The text below
is as accepted.
Author, date: from the API review of 2026-11-18 (main at 7768415)

This changes four existing modules and adds one backend. It adds no module.

- **`io`**: the contract suite takes a reader and a writer as two halves,
  either of which may be absent, and accepts a reader that stalls after its
  content instead of ending. A new backend, **`io_zephyr`**, puts a Zephyr
  UART behind `el_writer` (`el_zephyr_uart_poll`, sending by polling) and
  `el_reader` (`el_zephyr_uart_irq`, receiving by interrupt into a ring,
  and telling the application when bytes arrive).
- **`log`**: an application composes a message itself, with a function
  that writes into the buffer the sink lends (`el_log_compose`); each sink
  that keeps records sizes its storage at compile time
  (`EL_LOG_MEM_SINK_STORAGE_SIZE`, `EL_LOG_DEFERRED_SINK_STORAGE_SIZE`);
  the deferred sink owns its queue, in one block of storage; one logger
  makes no records at all (`el_logger_off`).
- **`alloc`**: an optional operation changes an allocation's size, moving
  it only if the allocator must (`realloc`, `el_realloc`), and
  `el_grow_buf` uses it.
- **The `alloc`, `time` and `log` contract suites**: `reset` becomes
  optional, as it does in io's.
- **[conventions.md](../conventions.md)** is not changed: with the deferred
  sink owning its queue, every object takes its storage one of two ways,
  and a rule waits for a module that has to choose.

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
  application's function is handed what a sink lends, under the rules a
  caller already follows; the begin and end calls stay inside the library.

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
   a borrowed `el_ring *`). The deferred sink is the only one that borrows
   a container, and the only one whose caller passes two storage areas
   whose sizes must agree by hand.

## What changes

| Where | Change | Existing code |
|---|---|---|
| io contract | Reader and writer halves; `reader_then`; `reset` optional | Fixtures compile and pass unchanged |
| `io_zephyr` (new) | `el_zephyr_uart_poll`, `el_zephyr_uart_irq` | New |
| log | `el_log_compose`, `el_log_compose_fn`; `EL_LOG_KEPT` | Additions |
| log | `EL_LOG_MEM_SINK_STORAGE_SIZE`, `EL_LOG_DEFERRED_SINK_STORAGE_SIZE` | Additions; the `_record_size()` functions stay, for other languages and for storage of mixed lengths |
| log | The deferred sink owns its queue; `EL_LOG_DEFERRED_SINK_STORAGE_SIZE`, `el_log_deferred_sink_pending_bytes` | `el_log_deferred_sink_init` takes one storage block and the longest message instead of an `el_ring *` and record storage |
| log | `el_logger_off` | Addition |
| alloc | `realloc` operation; `el_realloc` | Added at the end of the table: designated initializers leave it null |
| alloc, time, log contracts | `reset` optional | Fixtures compile and pass unchanged |

Every module touched is experimental, so the breaking change in log needs
no deprecation ([api-stability.md](../process/api-stability.md)).

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
deferred sink owns its queue, in one array sized at compile time.

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

static uint8_t deferred_storage[EL_LOG_DEFERRED_SINK_STORAGE_SIZE(WAITING, MESSAGE_MAX)];
static el_log_deferred_sink deferred;
static el_logger isr_log; /* given to the application's interrupt handlers */

static el_zephyr_uart_poll console_out;
static uint8_t line_storage[32 + MESSAGE_MAX]; /* the line's start, estimated, then the message */
static el_buf line;
static el_log_writer_sink console;
static el_logger app_log;

static uint8_t rx_storage[256];
static el_zephyr_uart_irq sensor;
static K_SEM_DEFINE(arrived, 0, 1);

void handle(const uint8_t *bytes, size_t len);
void app_main(void);

/* Runs in the sensor UART's receive interrupt. */
static void on_arrival(void *ctx)
{
    k_sem_give(ctx);
}

void app_main(void)
{
    if (el_zephyr_uart_poll_init(&console_out, DEVICE_DT_GET(DT_CHOSEN(zephyr_console))) != EL_OK) {
        return; /* no console to log to */
    }
    el_buf_init(&line, line_storage, sizeof(line_storage));
    el_log_writer_sink_init(&console, el_zephyr_uart_poll_as_writer(&console_out), &line);
    el_logger_init(&app_log, el_log_writer_sink_as_sink(&console), el_zephyr_clock(), "app",
                   EL_LOG_LEVEL_INFO);

    el_log_deferred_sink_init(&deferred, deferred_storage, sizeof(deferred_storage), MESSAGE_MAX);
    el_logger_init(&isr_log, el_log_deferred_sink_as_sink(&deferred), el_zephyr_clock(), "isr",
                   EL_LOG_LEVEL_INFO);

    if (el_zephyr_uart_irq_init(&sensor, DEVICE_DT_GET(DT_NODELABEL(uart2)), rx_storage,
                                sizeof(rx_storage), on_arrival, &arrived) != EL_OK) {
        EL_LOG_ERROR(&app_log, "the sensor's UART is not ready, or cannot receive by interrupt");
        return;
    }
    const el_reader in = el_zephyr_uart_irq_as_reader(&sensor);

    uint32_t dropped = 0;
    for (;;) {
        /* Bytes arrived, or 100 ms passed: read until there are none. */
        (void)k_sem_take(&arrived, K_MSEC(100));
        uint8_t chunk[32];
        size_t got;
        while (el_read(in, chunk, sizeof(chunk), &got) == EL_OK) {
            handle(chunk, got);
        }
        /* The receive handler counts what did not fit; this thread reports it. */
        const uint32_t now = el_zephyr_uart_irq_dropped(&sensor);
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
| Code the application writes for its UARTs | A console writer (17 lines) and the receive side (174 lines) | A four-line interrupt hook that gives the application's semaphore |
| What it declares for the deferred sink | Queue storage, an `el_ring`, record storage, the sink | One storage array, the sink |
| How it sizes them | A number from the README | `EL_LOG_DEFERRED_SINK_STORAGE_SIZE(WAITING, MESSAGE_MAX)`, a constant. The console's line is still an estimate: its start grows with the uptime's digits and the tag |
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
typedef struct report_args {
    const point *at;
    uint32_t speed;
} report_args;

/* The message, written where the sink keeps or sends it. */
static el_status report_message(void *ctx, el_buf *msg)
{
    const report_args *const args = ctx;
    EL_TRY(el_fmt_printf(msg, "at "));
    EL_TRY(point_print(msg, args->at));
    return el_fmt_printf(msg, " speed %" PRIu32, args->speed);
}

void report(const el_logger *log, const point *at, uint32_t speed)
{
    report_args args = {at, speed};
    el_log_compose(log, EL_LOG_LEVEL_INFO, report_message, &args);
}

typedef struct path {
    const point *points;
    size_t count;
} path;

static el_status path_message(void *ctx, el_buf *msg)
{
    const path *const p = ctx;
    EL_TRY(el_fmt_printf(msg, "path"));
    for (size_t i = 0; i < p->count; i++) {
        EL_TRY(el_buf_append(msg, " ", 1));
        EL_TRY(point_print(msg, &p->points[i]));
    }
    return EL_OK;
}

/* Detail at DEBUG, removed with the EL_LOG_* calls by EL_LOG_CUTOFF. */
void trace(const el_logger *log, const point *points, size_t count)
{
    if (EL_LOG_KEPT(EL_LOG_LEVEL_DEBUG)) {
        path p = {points, count};
        el_log_compose(log, EL_LOG_LEVEL_DEBUG, path_message, &p);
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
| Work done when the level is filtered | The point is formatted, then dropped | None: `el_log_compose` returns before calling the function |
| Casts at the call site | Two | None |
| A message as long as a loop | Not possible without a scratch buffer as long | Appended item by item; a cut message is reported to the sink as cut |
| What the call site writes | One call | A function, and a struct for its arguments: the library, not the caller, ends every record it begins |
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
buffer is the arena's latest allocation, so the arena's `realloc`
operation extends it in place at each growth, and 64 bytes are enough.

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

static el_zephyr_uart_poll sender;
static el_zephyr_uart_irq receiver;
static K_SEM_DEFINE(arrived, 0, 1);
/* Twice the most the suite sends before it takes: a byte the ring drops
 * would fail a correct sender. */
static uint8_t received[2 * EL_IO_CONTRACT_SIZE_MAX];
static int receiving;
static size_t sent;  /* accepted by the sender since the stream was made */
static size_t taken; /* handed to the suite by writer_take */

/* Runs in uart2's receive interrupt. */
static void on_arrival(void *ctx)
{
    k_sem_give(ctx);
}

static void reset(void *ctx)
{
    (void)ctx;
    if (receiving) {
        el_zephyr_uart_irq_deinit(&receiver);
        receiving = 0;
    }
}

static el_reader receive(void)
{
    k_sem_reset(&arrived); /* nothing left over from the previous stream */
    if (el_zephyr_uart_irq_init(&receiver, DEVICE_DT_GET(DT_NODELABEL(uart2)), received,
                                sizeof(received), on_arrival, &arrived) != EL_OK ||
        el_zephyr_uart_poll_init(&sender, DEVICE_DT_GET(DT_NODELABEL(uart1))) != EL_OK) {
        abort(); /* the wire is not there: nothing could be verified */
    }
    receiving = 1;
    sent = 0;
    taken = 0;
    return el_zephyr_uart_irq_as_reader(&receiver);
}

/* Until `len` bytes wait in the receiver. The wire loses nothing. */
static void await(size_t len)
{
    while (el_zephyr_uart_irq_len(&receiver) < len) {
        (void)k_sem_take(&arrived, K_MSEC(10));
    }
}

static el_reader reader_with_content(void *ctx, const uint8_t *content, size_t len)
{
    (void)ctx;
    const el_reader reader = receive();
    if (el_write_all(el_zephyr_uart_poll_as_writer(&sender), content, len, NULL) != EL_OK) {
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
    const el_status status = el_write(el_zephyr_uart_poll_as_writer(&sender), data, len, n);
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
    el_status_ignore(el_read(el_zephyr_uart_irq_as_reader(&receiver), out, cap, &n));
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
optional operation in six lines. A block never moves between classes, so
within its class the operation keeps it where it is; for another class it
returns null, and `el_realloc` moves it:

```c
static void *size_classes_realloc(void *ctx, void *ptr, el_layout layout, size_t new_size)
{
    (void)ctx;
    const int now = class_of(layout);
    const int then = class_of(el_layout_make(new_size, layout.align));
    return now == then ? ptr : NULL;
}

static const el_allocator_ops size_classes_ops = {
    .alloc = size_classes_alloc,
    .free = size_classes_free,
    .realloc = size_classes_realloc,
};
```

An allocator without the operation leaves it out and keeps working:
`el_realloc` allocates, copies and frees for it, and `el_grow_buf` then
does what it does today.

## The changes

### io: the contract suite in two halves

```c
/*
 * What a reader does once it has given its content: the fixture's
 * reader_then. 0 is EL_IO_CONTRACT_THEN_ENDS, so a fixture written before
 * the field existed keeps its meaning. Constants of a uint32_t field, as
 * the alloc suite's EL_ALLOC_CONTRACT_RECLAIMS_* are.
 */
/* EL_ERR_END_OF_STREAM, for good: memory, a pipe whose write end is
 * closed, a closed ring. */
#define EL_IO_CONTRACT_THEN_ENDS 0u
/* EL_ERR_WOULD_BLOCK: not ended, and more may come. A UART, an open
 * socket, a ring whose producer has not closed it. */
#define EL_IO_CONTRACT_THEN_STALLS 1u

/*
 * At least one half. A half that is there has its required functions;
 * every test of an absent half is reported as skipped.
 */
typedef struct el_io_contract_fixture {
    const char *name;
    void *ctx;

    /* Optional. Release the stream the previous request made. */
    void (*reset)(void *ctx);

    /*
     * The reader half. Every byte of `content` can be read once
     * reader_with_content returns: the suite allows a reader 64 stalls in
     * a row, not the time a wire takes (Example 4 waits for its bytes).
     */
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
`examples/uart_ring` and adds a sender. The header includes no Zephyr
header: `struct device` is declared and used by pointer only, as
`time_zephyr.h` uses none of Zephyr's types.

```c
struct device; /* Zephyr's, <zephyr/device.h>: used by pointer only */

/*
 * Polled. A writer whose every write passes each byte to uart_poll_out():
 * one write returns once the device has taken them all, waiting as the
 * driver waits (on most, a busy loop). Never EL_ERR_WOULD_BLOCK,
 * EL_ERR_NO_SPACE, EL_ERR_IO or EL_ERR_END_OF_STREAM. Not for interrupt
 * handlers: it waits. It may share a device with an el_zephyr_uart_irq.
 */
typedef struct el_zephyr_uart_poll {
    const struct device *dev;
} el_zephyr_uart_poll;

/* EL_OK, or EL_ERR_IO: the device is not ready (device_is_ready()). */
EL_NODISCARD el_status el_zephyr_uart_poll_init(el_zephyr_uart_poll *uart,
                                                const struct device *dev);
el_writer el_zephyr_uart_poll_as_writer(el_zephyr_uart_poll *uart);

/*
 * Interrupt-driven. From init on, the device's receive interrupt copies
 * what arrives into a ring over the caller's storage; bytes the device held
 * before init are discarded. What does not fit is dropped and counted: the
 * handler cannot wait. The reader takes from the ring:
 *
 *   EL_OK                 1 <= *n <= cap bytes, oldest first.
 *   EL_ERR_WOULD_BLOCK    Nothing has arrived. Not an end: a running
 *                         receiver has none.
 *   EL_ERR_END_OF_STREAM  After deinit, once every byte received before it
 *                         has been read.
 *
 * Waiting is the application's. `on_arrival`, if not null, is called as
 * on_arrival(ctx) from the receive interrupt, once per interrupt that took
 * bytes from the device, after they are in the ring (or counted as
 * dropped). It runs in interrupt context and must not block: k_sem_give(),
 * k_event_post(), k_poll_signal_raise() and k_work_submit() may be called
 * from it. It says that bytes arrived, not how many, and not that they are
 * still there: a thread it wakes reads until EL_ERR_WOULD_BLOCK. `ctx` is
 * borrowed until deinit returns.
 */
typedef struct el_zephyr_uart_irq {
    const struct device *dev;
    el_ring rx;                    /* the handler writes, the reading thread reads */
    void (*on_arrival)(void *ctx); /* the handler's */
    void *on_arrival_ctx;
    el_ring_word dropped; /* the handler's */
} el_zephyr_uart_irq;

/*
 * EL_OK: receiving. EL_ERR_IO: the device is not ready, or its driver has no
 * interrupt-driven receive (device_is_ready() tells the two apart); nothing
 * runs, and the reader reports EL_ERR_END_OF_STREAM: init sets the ring up
 * closed before anything that can fail.
 */
EL_NODISCARD el_status el_zephyr_uart_irq_init(el_zephyr_uart_irq *uart, const struct device *dev,
                                               void *storage, size_t cap,
                                               void (*on_arrival)(void *ctx), void *ctx);

/* Stop receiving, after an init that returned EL_OK: the interrupt is
 * disabled, on_arrival is not called once this returns, and the ring is
 * closed. */
void el_zephyr_uart_irq_deinit(el_zephyr_uart_irq *uart);

el_reader el_zephyr_uart_irq_as_reader(el_zephyr_uart_irq *uart);

/* Bytes received and not yet read: at least this many can be read now. */
size_t el_zephyr_uart_irq_len(const el_zephyr_uart_irq *uart);

/* Bytes dropped because the ring was full, since init, modulo 2^32: compare
 * two readings by subtraction. Any context. */
uint32_t el_zephyr_uart_irq_dropped(const el_zephyr_uart_irq *uart);
```

- **Names: one type for each of Zephyr's mechanisms, not for each
  direction.** Zephyr has three UART APIs, polling, interrupt-driven and
  asynchronous
  ([UART](https://docs.zephyrproject.org/latest/hardware/peripherals/uart.html)),
  and keeps one callback per device for each
  (`uart_irq_callback_user_data_set`, `uart_callback_set`). An
  interrupt-driven sender and receiver on one UART must therefore be one
  object that owns the callback: when the sender comes, it joins this type
  as `el_zephyr_uart_irq_as_writer`, as `el_posix_fd` has both handles.
  `el_zephyr_uart_async` is the name kept for the asynchronous API. Names
  per direction (`_poll_tx`, `_irq_rx`) would need a hidden shared object
  for the second interrupt-driven half. None of the names is one libedit's
  `histedit.h` declares.
- **Waiting.** [ring.h](../../modules/ring/include/elemental/ring.h) says
  the ring wakes nobody, and that what a reader waits on is the platform's
  and the application's. The backend owns the receive handler, so it is
  the one that knows when bytes have arrived; it says so through
  `on_arrival` and nothing else. The application owns the semaphore, or
  event, signal or work item, so the backend chooses no primitive.
- **No layout depends on Zephyr's configuration.** An embedded
  `struct k_sem` would: it is 16 to 48 bytes on ARMv7-M with `CONFIG_POLL`,
  `CONFIG_OBJ_CORE_SEM`, `CONFIG_OBJ_CORE_STATS`, object tracking and
  `CONFIG_WAITQ_SCALABLE`, and a `k_timeout_t` parameter is 4 or 8 bytes
  with `CONFIG_TIMEOUT_64BIT` (*computed* from the struct text in Zephyr's
  `kernel.h` and `kernel_structs.h`, not from a Zephyr build). That is
  harmless where Elemental is built inside the application, and corrupts
  memory silently for an installed package built with another
  configuration, which [ADR 0004](../adr/0004-private-configuration.md)
  rules out. *Computed*: `el_zephyr_uart_irq` is 36 bytes on ARMv7-M in
  every configuration, `el_zephyr_uart_poll` 4.
- **Contexts.** The receive handler is the ring's producer and calls
  `on_arrival`. One thread reads and calls init and deinit. The receiver
  must not move from init to deinit (the ring's rule, and the handler
  holds its address). A sender may be used from one thread at a time.
  Deinit is safe on one CPU; on several, another CPU may still be inside
  the handler when it returns, so multi-core targets are not supported
  until that is shown.
- **No lost wake-up.** The handler puts bytes in the ring before it calls
  `on_arrival`, so bytes stored after the thread's last read always have a
  call after them; a semaphore with a limit of 1 saturates (Example 1).
- **Configuration.** `IO_ZEPHYR`, default on, depends on `IO`, `RING` and a
  new probed fact, `PLATFORM_ZEPHYR_UART_IRQ`: a Zephyr build with
  `CONFIG_SERIAL` and `CONFIG_UART_INTERRUPT_DRIVEN`, read from Zephyr's
  configuration as `PLATFORM_ZEPHYR_CRC` is
  ([porting.md](../process/porting.md#making-a-backend-selectable-only-where-it-can-build)).
  So a driver without interrupt-driven receive cannot use the polled
  sender either; splitting the library waits for someone who needs it.
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
[Open questions](#open-questions). It would grow
`el_zephyr_uart_irq_init` by a transmit ring.

### log: composing a message

In `<elemental/log.h>`, so other languages have it too:

```c
/*
 * Writes a record's message into `message`, the buffer the logger's sink
 * lends, with el_buf functions only: el_buf_append(), el_fmt_printf(), a
 * formatter of your own, el_buf_reserve() with el_buf_commit(), and
 * el_buf_truncate() back to no less than the length the buffer had when
 * it was called. Returns EL_OK if the message is whole; any other status,
 * such as the first failure of what it called, means it is cut.
 *
 * It runs while the sink holds the record open: it must not log through a
 * logger whose records reach the same sink, as a sink's own operations
 * must not. Called through C: it must not unwind.
 */
typedef el_status (*el_log_compose_fn)(void *ctx, el_buf *message);

/*
 * Log the message `compose` writes, at `level` (below EL_LOG_LEVEL_OFF).
 *
 * If `level` is below the logger's minimum, nothing happens: the clock is
 * not read and `compose` is not called. Otherwise the record is begun; if
 * the sink refuses it, it is lost and counted, and `compose` is not
 * called. Otherwise compose(ctx, message) writes the message into the
 * buffer the sink lent, and the record is ended. A status other than EL_OK
 * reaches the sink as EL_ERR_NO_SPACE, a cut message, whatever the failure
 * was. A record the sink does not take is lost, and the sink counts it.
 * Like every log call, it cannot fail.
 *
 * `ctx` is passed to `compose` unchanged, and may be null.
 */
void el_log_compose(const el_logger *logger, el_log_level level, el_log_compose_fn compose,
                    void *ctx);
```

In `<elemental/log_printf.h>`, and `EL_LOG` is defined with it, so the
two cannot disagree:

```c
/*
 * A constant: whether calls at `level` are kept under EL_LOG_CUTOFF. Not
 * the logger's run-time level, which el_log_compose() checks. Composed
 * messages inside `if (EL_LOG_KEPT(level)) { ... }` are cut off with the
 * EL_LOG_* calls.
 */
#define EL_LOG_KEPT(level) ((level) >= EL_LOG_CUTOFF)
```

The call inside `if (EL_LOG_KEPT(level))` is removed by the cut-off as the
macros' calls are. The compose function is then an unused function like any
other: an optimizing compiler drops a static one and its text, and at `-O0`
GCC keeps it.

- **The library begins and ends every record.** The three steps
  `el_log_vprintf` takes with private functions (`el_log_record_start_`,
  `el_log_sink_begin_`, `el_log_sink_end_`) stay private, and
  `el_log_compose` takes them around the caller's function. A sink is never
  left with a record open by a caller's early return, so a sink that holds
  something from begin to end (a lock, to share a lending sink between
  threads) stays possible. If ADR 0022's first revisit condition adds a
  record-at-once sink operation, no call site changes.
- **A function and a context, not a begin/end pair.** It is the pattern
  every operation table already uses, so a binding wraps it as it wraps
  them ([conventions.md](../conventions.md#other-languages)). The price is
  at the call site: a function, and a struct for its arguments, in place
  of straight-line code (Example 2).
- **Logging from inside the function to the same sink** is a contract
  violation. Today it would corrupt silently: the writer sink's begin
  truncates its line, the deferred sink's begin sets its message up again
  over the record being built. The three sinks in this repository catch
  it: each keeps whether a record is open, one field, and its begin asserts
  that none is.
- **`compose` returns any status** so that a formatter's own status can be
  passed on as it is, including an application's negative one. Only
  `EL_OK` means whole; the status is not returned, because a log call
  cannot fail.
- A truncate below the message's start is a contract violation, caught
  when the record is ended (the check `el_log_sink_end_` already makes).
- The record is in `el_log_compose`'s frame, as it is in
  `el_log_vprintf`'s; there is no object for the caller to hold.

### log: sizing kept records at compile time

In `<elemental/log_mem.h>`; the deferred sink's is below, with its init.

```c
/*
 * Private: the fields the memory sink keeps before each record's message.
 * Public only so that EL_LOG_MEM_SINK_STORAGE_SIZE is a constant; not API.
 */
typedef struct el_log_mem_sink_entry_ {
    el_ticks timestamp;
    const char *tag;
    size_t len;
    uint32_t ticks_per_second;
    el_log_level level;
} el_log_mem_sink_entry_;

/*
 * The bytes of storage that keep `count` records, each with a message of
 * `message_max` bytes. A constant expression when its arguments are, for
 * a static array; it does not check for overflow, which
 * el_log_mem_sink_record_size() asserts.
 */
#define EL_LOG_MEM_SINK_STORAGE_SIZE(count, message_max) \
    ((size_t)(count) * (sizeof(el_log_mem_sink_entry_) + (size_t)(message_max)))

/* One record's bytes, as a function: for languages that cannot use the
 * macro, and for storage sized for records of different lengths. */
size_t el_log_mem_sink_record_size(size_t message_len);
```

- **The macro answers the caller's question**, how much storage for n
  records of m bytes, not how big one record is; so there is one macro
  for each sink, and no record-size macro. A record's own fields are 24
  bytes on a 32-bit target and 32 on a 64-bit host, because of padding,
  so only `sizeof` of the sink's struct gives the size everywhere: the
  struct is in the header for that, and nothing else.
- **One macro for each sink, in its header.** How a sink keeps a record is
  the sink's: either may add a field (a sequence number, a copy of the
  tag) without changing the other, or `log.h`, which sink authors read as
  the contract.
- **The functions stay.** An interface is never only reachable through a
  macro ([conventions.md](../conventions.md#abi), rule 6); Rust and Python
  size storage with them.
- Today's private `mem_entry` and `deferred_entry` become these two
  structs, unchanged; the two sinks do not share one.

### log: the deferred sink owns its queue

In `<elemental/log_deferred.h>`:

```c
/* Private: the fields the deferred sink keeps before each record's
 * message. Public only so that the macro below is a constant; not API. */
typedef struct el_log_deferred_sink_entry_ {
    el_ticks timestamp;
    const char *tag;
    size_t len;
    uint32_t ticks_per_second;
    el_log_level level;
} el_log_deferred_sink_entry_;

/*
 * The bytes the deferred sink needs for `waiting` records that may wait,
 * each with a message of `message_max` bytes: the area one record is built
 * in, then the queue, each record its fields and its message. A constant
 * expression when its arguments are, for a static array; it does not check
 * for overflow, which el_log_deferred_sink_record_size() asserts.
 */
#define EL_LOG_DEFERRED_SINK_STORAGE_SIZE(waiting, message_max) \
    (((size_t)(waiting) + 1u) *                                 \
     (sizeof(el_log_deferred_sink_entry_) + (size_t)(message_max)))

typedef struct el_log_deferred_sink {
    el_ring queue;   /* the sink's own: the logging side writes, the drain reads */
    uint8_t *record; /* the record begun is built here: its fields, then its message */
    size_t record_cap;
    el_buf message;
    el_ring_word lost;
} el_log_deferred_sink;

/*
 * Hand records on through a queue kept in the `storage_cap` bytes at
 * `storage`, of any alignment. The first
 * el_log_deferred_sink_record_size(message_max) bytes are where each record
 * is built, so a message of more than `message_max` bytes is lost; the rest
 * is the queue. Size it with EL_LOG_DEFERRED_SINK_STORAGE_SIZE(). A queue of
 * records shorter than `message_max` needs fewer bytes than that macro says.
 * `storage` may be null only if `storage_cap` is 0, which makes a sink that
 * loses every record. A `storage_cap` above 0 and below
 * el_log_deferred_sink_record_size(message_max) is a contract violation;
 * with assertions off, a sink that loses every record.
 *
 * Called before either side is used. The sink must not move afterwards.
 */
void el_log_deferred_sink_init(el_log_deferred_sink *sink, void *storage, size_t storage_cap,
                               size_t message_max);

/*
 * The bytes of records waiting now, at least: the logging side can only
 * make it larger. The writing side's, as el_ring_len() is the ring's
 * consumer's.
 */
size_t el_log_deferred_sink_pending_bytes(const el_log_deferred_sink *sink);
```

- **One block and the longest message, not two storage areas.** Two
  `(void *, size_t)` pairs side by side compile when swapped and run: a
  queue meant for 8 records of 64-byte messages then holds 1, and swapping
  only the sizes overruns the record storage (*measured* on the host, GCC
  15.3). Here a valid call always has `storage_cap` of at least
  `el_log_deferred_sink_record_size(message_max)`, so above `message_max`,
  and a swapped call is never valid: 1720 of 1720 sampled swaps were caught
  by the assertion (*measured*); with assertions off the sink loses every
  record and writes nothing out of bounds (checked under ASan and UBSan).
  The one swap not caught is with `message_max` 0, a sink of no storage.
- **The record area is not in the contract.** It exists because `el_ring`
  copies bytes in, so a record is built elsewhere first. Zephyr's
  `mpsc_pbuf` and Rust's bbqueue build records in place. Taking
  `message_max` rather than a record pointer lets the area go, if `el_ring`
  ever reserves in place, without changing the init.
- **The queue stays observable.** Every queue in Zephyr (`ring_buf`,
  `k_msgq`, `mpsc_pbuf`), FreeRTOS and Rust's `heapless` reports its fill
  through a function, and none asks its caller to build the inner
  container. Today's tests read the ring with `el_ring_len` eleven times;
  they read `el_log_deferred_sink_pending_bytes` instead. A high-water mark
  is not added: it costs the logging side a compare and a store per record,
  and can be added later without a break.
- **Moving.** No change in practice: a logger holds the sink's address
  from `el_logger_init` on, as it does today.
- **What tests change** (read, not run): `log_deferred_test.cpp`, the
  stress test, the violations test and `fixture_deferred.c` make one init
  call. The test of record storage smaller than a record's fields goes:
  that storage can no longer be given. The violations test loses "a null
  ring" and gains "a size between 0 and one record's fields".
  `modules/log/README.md` and `examples/uart_ring` change to match.

The rule "the ring is set up by the caller, before or after, and is empty
when the first record is logged" disappears with the ring. *Computed*: the
sink grows by the ring less a pointer, 16 bytes on a 32-bit target
(*measured* on x86-64: 64 bytes and the caller's 40-byte ring before, 96
after), and the caller's `el_ring` goes away, so the total is unchanged
(before the one field that marks a record open, above).

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

A static constant inside `log.c`: minimum `EL_LOG_LEVEL_OFF`, a frequency
of 1, a sink whose begin refuses every record and a clock that reads 0.
Neither is reached through the constant. They are harmless rather than
asserting because a logger may be copied (`log.h`): a copy of
`*el_logger_off()` whose level is lowered is a logger whose every record
is lost, not a trap, and nothing depends on assertions being compiled in.
The conventions' rule that a null logger is a contract violation stays.

### alloc: changing an allocation's size

```c
typedef struct el_allocator_ops {
    void *(*alloc)(void *ctx, el_layout layout);
    void (*free)(void *ctx, void *ptr, el_layout layout);

    /*
     * Optional: null if the allocator can do no better than allocate, copy
     * and free, which el_realloc() then does.
     *
     * Change an allocation's size, moving it if the allocator chooses.
     *
     * Called with: ptr and layout exactly those of a live allocation from
     *              this ctx; new_size >= 1 and different from layout.size.
     * Must: return the allocation's address, ptr or another multiple of
     *       layout.align, with new_size bytes there whose first
     *       min(layout.size, new_size) are the allocation's; the
     *       allocation at ptr has then ended unless ptr is returned. Or
     *       return NULL and change nothing: el_realloc() then allocates,
     *       copies and frees, so NULL means "no better than that", not
     *       only "no memory".
     */
    void *(*realloc)(void *ctx, void *ptr, el_layout layout, size_t new_size);
} el_allocator_ops;

/*
 * Change the allocation at `ptr`, made from `allocator` with `layout`, to
 * `new_size` bytes, moving it if need be. Returns its address, `ptr` or
 * another; its first min(layout.size, new_size) bytes are what they were.
 * From then on it is freed with el_layout_make(new_size, layout.align).
 * NULL if it cannot be done: nothing changed, and the allocation is still
 * at `ptr` with `layout`.
 *
 * The allocator's realloc operation is asked first. Without one, or if it
 * returns NULL, a new allocation is made, the old one's bytes copied into
 * it, and the old one freed.
 *
 * A new size equal to layout.size returns `ptr` without consulting the
 * allocator. A layout of size 0 (ptr null or the zero-size pointer) is
 * el_alloc() of the new size; a new size of 0 frees the allocation and
 * returns the zero-size pointer. No operation is called with a size of 0.
 */
EL_NODISCARD void *el_realloc(el_allocator allocator, void *ptr, el_layout layout,
                              size_t new_size);
```

- **One operation that may move, not one that only resizes in place.**
  Rust had in-place `grow_in_place` and `shrink_in_place`
  ([1.43 `AllocRef`](https://doc.rust-lang.org/1.43.0/std/alloc/trait.AllocRef.html))
  and replaced them with `grow` and `shrink`, which may move
  ([`Allocator`](https://doc.rust-lang.org/std/alloc/trait.Allocator.html)).
  Zig's table has an in-place `resize` and, since 0.14, `remap`, which may
  move
  ([`Allocator.zig`](https://raw.githubusercontent.com/ziglang/zig/0.14.1/lib/std/mem/Allocator.zig)).
  An arena answers in place and the C library with `realloc()`, so one
  table change covers both; an in-place operation would need a second
  change for the C library.
- **Null from the operation means "do it yourself"**, as Zig's `remap`.
  An implementation answers only what it does better than allocate, copy
  and free, and none repeats that fallback. The cost: an allocator that is
  out of memory is asked twice.
- **The name.** `el_realloc` and `realloc` pair as `el_alloc` and `alloc`
  do, and a null result is the one failure conventions.md allows a
  pointer to report (Errors, rule 2). Checked against libedit's
  `histedit.h` when implemented ([ADR 0006](../adr/0006-c11-and-naming.md)).
- **The arenas.** The latest allocation grows in place while the storage
  has room, and shrinking it gives the tail back. Any other allocation
  shrinks in place, giving nothing back; growing it returns null, and
  `el_realloc` moves it.
- **The libc backend** answers with C's `realloc()` for an alignment up to
  `max_align_t`, which `malloc()` served; above it, null, since C has no
  aligned `realloc()`. A `realloc()` that fails keeps the old block, as
  the operation must.
- **The checking allocator** checks that `ptr` and `layout` name a live
  allocation, as its free does, calls `el_realloc` on its inner allocator,
  and on success records the new pointer and layout, so a later free with
  the old pointer or size is caught.
- **The contract suite** tests `el_realloc` on every allocator (bytes kept,
  the new layout frees, a failure changes nothing), and the operation
  directly when the handle has one (alignment of a moved result, null
  changes nothing). The fixture needs no new field.
- **`el_grow_buf`** grows with `el_realloc` to the size it wants, then to
  exactly what is needed, as today it allocates. Where `el_realloc` falls
  back, it copies the old capacity rather than the length: at most what
  the buffer held room for.
- **The Rust sketch** (`tests/interop/rust_alloc_sketch.rs`) mirrors
  `el_allocator_ops`, which C reads, so it gains the field as
  `Option<unsafe extern "C" fn(...)>` to keep the layout. Its Rust
  allocator can answer with `GlobalAlloc::realloc`, Rust's counterpart.

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
- **A rule in conventions.md for how an object takes storage.** Once the
  deferred sink owns its queue, every object takes `(void *storage,
  size_t cap)` or a borrowed `el_buf *`, and each header says which. An
  object that lays out its own structure in the bytes (a ring, an arena,
  kept records) takes the first; one that appends bytes or text through
  `io`'s buffer functions (a buffered writer, the writer sink's line)
  takes the second, which `alloc` cannot, since `io` depends on `alloc`.
  A rule drawn from these few would bind every later module; it
  is written when a module has to choose (`frame`, or asynchronous io),
  with that module as one more case.
- **Unsupported printf formats in log calls.** They are contract
  violations, and trap by default. The build-time check closes most of the
  gap; whether a log call should degrade instead is a decision below.
- **The sink contract, flush, the time representation, and the status
  vocabulary.** The review found nothing to change in them.

## Dependencies and cost

| | |
|---|---|
| Elemental modules | One new edge: `io_zephyr` depends on `io` and `ring`. log and alloc gain no dependency |
| Runtime symbols (freestanding) | None new. `io_zephyr` uses Zephyr's UART calls, like `time_zephyr` uses its kernel; the semaphore an application gives in `on_arrival` is its own |
| Allocation | None new. `el_realloc` is called only by code that already allocates |
| Per-object storage | `el_log_deferred_sink` grows by the ring less a pointer, and the caller's `el_ring` goes away; each of the three sinks gains one field marking a record open; `el_allocator_ops` grows by a pointer, once per allocator type, in read-only data. `el_log_compose` keeps the record in its own frame, as `el_log_vprintf` does. `el_zephyr_uart_irq` is 36 bytes on ARMv7-M (*computed*) in every Zephyr configuration |
| New status codes | None |
| Kconfig | `IO_ZEPHYR` and the probed fact `PLATFORM_ZEPHYR_UART_IRQ` |
| Code size | Not measured. `el_log_compose` shares the begin and end steps with `el_log_vprintf`; the realloc operations, `el_realloc` and `io_zephyr` are new code. To be measured with `code_size.sh` |

## Contracts

The new and changed headers above state each one; in summary:

- **Ownership and lifetime.** The UART objects are the caller's. The
  receiver stays in place from init to deinit, and borrows `on_arrival`'s
  `ctx` until deinit returns. `el_log_compose` borrows `ctx` for the call.
  `el_logger_off()` is valid for the whole program. The deferred sink
  borrows its one storage block, and must not move once a logger uses it.
  After `el_realloc` succeeds, the allocation is the returned pointer with
  the new layout; the old pointer is not used again unless it was returned.
- **Errors.** `el_realloc`: the allocation, or NULL with nothing changed.
  The UART reader: `EL_OK`, `EL_ERR_WOULD_BLOCK`, or `EL_ERR_END_OF_STREAM`
  after deinit or a failed init. The two UART inits: `EL_OK` or
  `EL_ERR_IO`. A write to the sender cannot fail. Log calls still cannot
  fail.
- **Concurrency and context.** The UART receiver is two contexts by
  design, like the ring: its handler, which also calls `on_arrival`, and
  one reading thread; one CPU only. The sender
  follows the default, one context at a time. `el_log_compose` is as
  interrupt-safe as the logger's sink and clock and the caller's function,
  exactly like `el_log_printf`.
- **Contract violations.** A null logger or compose function; logging,
  from inside a compose function, to the sink it is writing into (caught
  by the three sinks here); a truncate below the message's start; deferred
  sink storage above 0 bytes and below one record of `message_max`; a
  realloc operation that returns a pointer not aligned to the layout; a
  receiver used after deinit except to read what remains.

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

Revised in review, 2026-11-22, at 87ba633, against those headers with the
declarations in [The changes](#the-changes) as revised added, with GCC
15.3.0 and Clang 21.1.8 and the same flags: Examples 1, 2 ("after"), 3, 4
and 6 (inside the size classes example's source), and both sizing macros
as array sizes. The Zephyr examples were compiled against stand-in headers
as before; `io_zephyr.h` itself includes none. The figures on the deferred
sink's swapped arguments were *measured* on the host against a sketch of
its implementation, outside the tree.

What the implementation must show:

- **io contract.** Every existing fixture passes as it is; the buffered
  writer fixtures pass without a reader; the new test is seen red against a
  reader that reports an end when it is momentarily empty.
- **io_zephyr.** The contract suite, both halves, on `mps2/an385` under
  QEMU over the joined UARTs: an emulator run. Its own tests: bytes held
  before init are discarded; a full ring drops and counts; `on_arrival` is
  called after the bytes are readable, and not after deinit returns;
  deinit ends the reader after what was received. Built for the other
  boards, with the symbol check; the header is compiled alone as C11 and
  C++11 on the host, which it can be because it includes nothing of
  Zephyr's.
- **log.** `el_log_compose` reads no clock and calls nothing below the
  minimum, and does not call the function when the sink refuses (counted,
  as `log.unit` counts readers today); a cut status of any value reaches
  the sink as `EL_ERR_NO_SPACE`; a truncate below the start dies; logging
  to the same sink from inside the function dies, for each of the three
  sinks; the cut-off check (`log.cutoff_check.*`) shows that a composed
  message under `EL_LOG_KEPT` leaves no call, and, in an optimized build,
  no string (at `-O0` a compiler may keep the unused static compose
  function and its text); a storage of
  `EL_LOG_MEM_SINK_STORAGE_SIZE(n, m)` keeps n records and loses the next; a
  deferred sink over `EL_LOG_DEFERRED_SINK_STORAGE_SIZE(n, m)` holds n
  records of m bytes and loses the next, and `pending_bytes` follows; its
  storage too small for one record, and `storage_cap` and `message_max`
  swapped, die; each macro equals its function; the
  deferred sink's stress test runs unchanged in substance; a copy of
  `*el_logger_off()` with its level lowered loses every record and reads
  0 from its clock.
- **alloc.** The suite's `el_realloc` tests against both arenas, the libc
  heap (with its operation: growth that moves, and alignment above
  `max_align_t` falling back), the checker, the size classes example, and
  an allocator without the operation; seen red against an arena whose
  operation moves the tail wrongly, and against an operation that returns
  null after changing something. The checker dies on a free with the old
  pointer or size after a move. Example 3 as a test, holding 60 bytes in a
  100-byte arena. The Rust sketch runs with the field present and null.
- Every preset, Twister, the symbol check, and the ledgers, as for any
  change ([preflight](../../.agents/skills/preflight/SKILL.md)).

## Order of work

Four parts on their own branches, merged in this order:

1. **The contract suites**: io's halves and `reader_then`; `reset`
   optional in all four. Nothing depends on anything else here.
2. **log**: `el_log_compose`, `EL_LOG_KEPT`, the sinks' open-record check,
   the per-sink sizing macros, the deferred sink's own queue in one block
   with `pending_bytes`, `el_logger_off`. Independent of 1.
3. **alloc**: the operation, `el_realloc`, the arenas, the libc backend,
   the checker, the suite, the Rust sketch, then `el_grow_buf`.
   Independent of 1 and 2.
4. **io_zephyr**: needs 1 for its fixture. Then `examples/uart_ring` is
   rewritten on it.

Then the records: ADR 0024, an ADR for the allocator operation that may
move (it reverses the alloc proposal's deferral), the README module table,
build.md's options, CHANGELOG, and a roadmap entry. Parts 1 to 3 can be
built in parallel.

## Decisions for the owner

1. **An unsupported printf format in a log call: trap, or degrade?**
   Today a contract violation, so a trap in the default configuration.
   Recommended: keep the trap. The build-time check catches literal formats
   in every target that uses it, and degrading would make a bug in a format
   string a silent oddity in a log.
2. **io_zephyr's first scope.** Recommended: a polled sender and an
   interrupt-driven receiver, as above. A non-blocking sender waits for a
   consumer.
3. **Hand the sink's loan to applications.** *Decided 2026-11-22*: through
   a function the library calls between begin and end
   (`el_log_compose`), not a public begin/end pair. A pair would let any
   call site leave a sink with a record open, which rules out sinks that
   hold something from begin to end and puts every call site on the
   lending model ADR 0022 may revisit.
4. **Change an allocation's size now**, although the alloc proposal
   deferred it. *Decided 2026-11-22*: yes, as an operation that may move
   (`realloc`, `el_realloc`), not one that only resizes in place, which
   the C library could not implement and Rust and Zig both outgrew.
5. **Record ADR 0024, "An `el_buf` keeps what it holds."** Recommended:
   yes, with the prototype's evidence, so the question is not reopened
   without new evidence.
6. **Where it goes in the roadmap.** Recommended: a milestone M5a, before
   M6. `frame`'s glue is to run over the UART example, which becomes
   `io_zephyr`, and its tests will use the new fixture halves.
7. **How kept records are sized.** *Decided 2026-11-22*: a macro for each
   sink in its own header that sizes its storage
   (`EL_LOG_MEM_SINK_STORAGE_SIZE`, `EL_LOG_DEFERRED_SINK_STORAGE_SIZE`),
   no macro for one record; the functions kept beside them;
   not one record format in `log.h`.
8. **How an object takes storage, and the deferred sink's init.**
   *Decided 2026-11-22*: the deferred sink takes one storage block and the
   longest message, `(sink, storage, storage_cap, message_max)`, sized by
   `EL_LOG_DEFERRED_SINK_STORAGE_SIZE(waiting, message_max)`, and reports
   its queue's fill with `el_log_deferred_sink_pending_bytes`. Not two
   storage pairs, which can be swapped unnoticed. No storage rule in
   conventions.md yet ([What is not changed](#what-is-not-changed-and-why)).
9. **io_zephyr's names and how its receiver wakes a thread.**
   *Decided 2026-11-22*: one type for each of Zephyr's UART mechanisms,
   `el_zephyr_uart_poll` and `el_zephyr_uart_irq` (`el_zephyr_uart_async`
   kept), because Zephyr allows one callback per device; and an arrival
   callback the receive interrupt calls, with the application owning the
   semaphore, so no public layout or signature depends on Zephyr's
   configuration. Not an embedded `struct k_sem` and a `_wait`.

## Open questions

- **Waking the thread that drains a deferred sink.** Today it polls, or the
  application wraps the deferred sink in a sink that forwards both calls
  and then gives a semaphore; the deferred sink's lent buffer makes that
  wrapper zero-copy. A callback in the deferred sink would save it, and
  can be added without changing the init:
  `el_log_deferred_sink_set_notify(sink, void (*notify)(void *ctx), ctx)`,
  called before the sink is used, its function called from the logging
  side and so interrupt-safe, as `el_zephyr_uart_irq`'s `on_arrival` is.
  FreeRTOS adds the same to its message buffer as
  `xMessageBufferCreateStaticWithCallback`. Wait for a second application
  that writes the wrapper.
- **A non-blocking sender.** One that fills the FIFO from the transmit
  interrupt out of a ring, and stalls with `EL_ERR_WOULD_BLOCK` when the
  ring is full: the inner writer `io_buffered.h` describes. Wait for a
  console that must not block.
- **io_zephyr beyond QEMU.** Whether Renode can join two UARTs of the
  Cortex-M0+ board (open since the ring proposal), and whether Zephyr's UART
  emulator would let `native_sim` run the suite.
